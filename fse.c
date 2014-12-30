/* ******************************************************************
   FSE : Finite State Entropy coder
   Copyright (C) 2013-2014, Yann Collet.
   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */

#ifndef FSE_COMMONDEFS_ONLY

/****************************************************************
*  Tuning parameters
****************************************************************/
/* MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect
*  Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache */
#define FSE_MAX_MEMORY_USAGE 14
#define FSE_DEFAULT_MEMORY_USAGE 13

/* FSE_MAX_SYMBOL_VALUE :
*  Maximum symbol value authorized.
*  Required for proper stack allocation */
#define FSE_MAX_SYMBOL_VALUE 255

/* FSE_ILP :
*  Determine if the algorithm tries to explicitly exploit ILP
*  (Instruction Level Parallelism)
*  Default : Recommended */
#define FSE_ILP 1


/****************************************************************
*  Generic function type & suffix (C template emulation)
****************************************************************/
#define FSE_FUNCTION_TYPE BYTE
#define FSE_FUNCTION_EXTENSION

#endif   // !FSE_COMMONDEFS_ONLY


/****************************************************************
*  Includes
****************************************************************/
#include <stdlib.h>    // malloc, free
#include <string.h>    // memcpy, memset
#include <stdio.h>     // printf (debug)
#include "fse_static.h"


/****************************************************************
*  Basic Types
*****************************************************************/
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   // C99
# include <stdint.h>
typedef  uint8_t BYTE;
typedef uint16_t U16;
typedef  int16_t S16;
typedef uint32_t U32;
typedef  int32_t S32;
typedef uint64_t U64;
typedef  int64_t S64;
#else
typedef unsigned char       BYTE;
typedef unsigned short      U16;
typedef   signed short      S16;
typedef unsigned int        U32;
typedef   signed int        S32;
typedef unsigned long long  U64;
typedef   signed long long  S64;
#endif


/****************************************************************
*  Constants
*****************************************************************/
#define FSE_MAX_TABLELOG  (FSE_MAX_MEMORY_USAGE-2)
#define FSE_MAX_TABLESIZE (1U<<FSE_MAX_TABLELOG)
#define FSE_MAXTABLESIZE_MASK (FSE_MAX_TABLESIZE-1)
#define FSE_DEFAULT_TABLELOG (FSE_DEFAULT_MEMORY_USAGE-2)
#define FSE_MIN_TABLELOG 5

#define FSE_TABLELOG_ABSOLUTE_MAX 15
#if FSE_MAX_TABLELOG > FSE_TABLELOG_ABSOLUTE_MAX
#error "FSE_MAX_TABLELOG > FSE_TABLELOG_ABSOLUTE_MAX is not supported"
#endif

static const U32 FSE_blockHeaderSize = 4;


/****************************************************************
*  Compiler specifics
****************************************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  define FORCE_INLINE static __forceinline
#  include <intrin.h>                    /* For Visual 2005 */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4214)        /* disable: C4214: non-int bitfields */
#else
#  define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#  ifdef __GNUC__
#    define FORCE_INLINE static inline __attribute__((always_inline))
#  else
#    define FORCE_INLINE static inline
#  endif
#endif


/****************************************************************
*  Error Management
****************************************************************/
#define FSE_STATIC_ASSERT(c) { enum { FSE_static_assert = 1/(int)(!!(c)) }; }   /* use only *after* variable declarations */



/****************************************************************
*  Complex types
****************************************************************/
typedef struct
{
    int  deltaFindState;
    U16  maxState;
    BYTE minBitsOut;
    /* one byte padding */
} FSE_symbolCompressionTransform;

typedef struct
{
    U32 fakeTable[FSE_CTABLE_SIZE_U32(FSE_MAX_TABLELOG, FSE_MAX_SYMBOL_VALUE)];   /* compatible with FSE_compressU16() */
} CTable_max_t;


/****************************************************************
*  Internal functions
****************************************************************/
FORCE_INLINE unsigned FSE_highbit (register U32 val)
{
#   if defined(_MSC_VER)   // Visual
    unsigned long r;
    _BitScanReverse ( &r, val );
    return (unsigned) r;
#   elif defined(__GNUC__) && (GCC_VERSION >= 304)   // GCC Intrinsic
    return 31 - __builtin_clz (val);
#   else   // Software version
    static const unsigned DeBruijnClz[32] = { 0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30, 8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31 };
    U32 v = val;
    unsigned r;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    r = DeBruijnClz[ (U32) (v * 0x07C4ACDDU) >> 27];
    return r;
#   endif
}


#ifndef FSE_COMMONDEFS_ONLY

//static unsigned FSE_64bits(void) { return sizeof(void*)==8; }

unsigned FSE_isError(size_t code) { return (code > (size_t)(-FSE_ERROR_maxCode)); }

#define FSE_GENERATE_STRING(STRING) #STRING,
static const char* FSE_errorStrings[] = { FSE_LIST_ERRORS(FSE_GENERATE_STRING) };

const char* FSE_getErrorName(size_t code)
{
    static const char* codeError = "Unspecified error code";
    if (FSE_isError(code)) return FSE_errorStrings[-(int)(code)];
    return codeError;
}

static short FSE_abs(short a)
{
    return a<0? -a : a;
}


/****************************************************************
*  Header bitstream management
****************************************************************/
size_t FSE_headerBound(unsigned maxSymbolValue, unsigned tableLog)
{
    size_t maxHeaderSize = (((maxSymbolValue+1) * tableLog) >> 3) + 1;
    return maxSymbolValue ? maxHeaderSize : FSE_MAX_HEADERSIZE;
}

static size_t FSE_writeHeader_generic (void* header, size_t headerBufferSize,
                                       const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog,
                                       unsigned safeWrite)
{
    BYTE* const ostart = (BYTE*) header;
    BYTE* out = ostart;
    BYTE* const oend = ostart + headerBufferSize;
    int nbBits;
    const int tableSize = 1 << tableLog;
    int remaining;
    int threshold;
    U32 bitStream;
    int bitCount;
    unsigned charnum = 0;
    int previous0 = 0;

    // HeaderId (normal case)
    bitStream = 2;
    bitCount  = 2;
    // Table Size
    bitStream += (tableLog-FSE_MIN_TABLELOG) << bitCount;
    bitCount  += 4;

    // Init
    remaining = tableSize+1;   // +1 for extra accuracy
    threshold = tableSize;
    nbBits = tableLog+1;

    while (remaining>1)   // stops at 1
    {
        if (previous0)
        {
            unsigned start = charnum;
            while (!normalizedCounter[charnum]) charnum++;
            while (charnum >= start+24)
            {
                start+=24;
                bitStream += 0xFFFF<<bitCount;
                if ((!safeWrite) && (out > oend-2)) return (size_t)-FSE_ERROR_GENERIC;   // Buffer overflow
                *(U16*)out=(U16)bitStream;
                out+=2;
                bitStream>>=16;
            }
            while (charnum >= start+3)
            {
                start+=3;
                bitStream += 3 << bitCount;
                bitCount += 2;
            }
            bitStream += (charnum-start) << bitCount;
            bitCount += 2;
            if (bitCount>16)
            {
                if ((!safeWrite) && (out > oend - 2)) return (size_t)-FSE_ERROR_GENERIC;   // Buffer overflow
                *(U16*)out = (U16)bitStream;
                out += 2;
                bitStream >>= 16;
                bitCount -= 16;
            }
        }
        {
            short count = normalizedCounter[charnum++];
            const short max = (short)((2*threshold-1)-remaining);
            remaining -= FSE_abs(count);
            if (remaining<0) return (size_t)-FSE_ERROR_GENERIC;
            count++;   // +1 for extra accuracy
            if (count>=threshold) count += max;   // [0..max[ [max..threshold[ (...) [threshold+max 2*threshold[
            bitStream += count << bitCount;
            bitCount  += nbBits;
            bitCount  -= (count<max);
            previous0 = (count==1);
            while (remaining<threshold) nbBits--, threshold>>=1;
        }
        if (bitCount>16)
        {
            if ((!safeWrite) && (out > oend - 2)) return (size_t)-FSE_ERROR_GENERIC;   // Buffer overflow
            *(U16*)out = (U16)bitStream;
            out += 2;
            bitStream >>= 16;
            bitCount -= 16;
        }
    }

    if ((!safeWrite) && (out > oend - 2)) return (size_t)-FSE_ERROR_GENERIC;   // Buffer overflow
    * (U16*) out = (U16) bitStream;
    out+= (bitCount+7) /8;

    if (charnum > maxSymbolValue + 1) return (size_t)-FSE_ERROR_GENERIC;   // Too many symbols written

    return (out-ostart);
}


size_t FSE_writeHeader (void* header, size_t headerBufferSize, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog)
{
    if (tableLog > FSE_MAX_TABLELOG) return (size_t)-FSE_ERROR_GENERIC;   // Unsupported
    if (tableLog < FSE_MIN_TABLELOG) return (size_t)-FSE_ERROR_GENERIC;   // Unsupported

    if (headerBufferSize < FSE_headerBound(maxSymbolValue, tableLog))
        return FSE_writeHeader_generic(header, headerBufferSize, normalizedCounter, maxSymbolValue, tableLog, 0);

    return FSE_writeHeader_generic(header, headerBufferSize, normalizedCounter, maxSymbolValue, tableLog, 1);
}


size_t FSE_readHeader (short* normalizedCounter, unsigned* maxSymbolValuePtr, unsigned* tableLogPtr, const void* header)
{
    const BYTE* const istart = (const BYTE*) header;
    const BYTE* ip = (const BYTE*) header;
    int nbBits;
    int remaining;
    int threshold;
    U32 bitStream;
    int bitCount;
    unsigned charnum = 0;
    int previous0 = 0;

    bitStream = * (U32*) ip;
    bitStream >>= 2;
    nbBits = (bitStream & 0xF) + FSE_MIN_TABLELOG;   // read tableLog
    if (nbBits > FSE_TABLELOG_ABSOLUTE_MAX) return (size_t)-FSE_ERROR_tableLog_tooLarge;
    bitStream >>= 4;
    *tableLogPtr = nbBits;
    remaining = (1<<nbBits)+1;
    threshold = 1<<nbBits;
    nbBits++;
    bitCount = 6;

    while (remaining>1)
    {
        if (previous0)
        {
            unsigned n0 = charnum;
            while ((bitStream & 0xFFFF) == 0xFFFF)
            {
                n0+=24;
                ip+=2;
                bitStream = (*(U32*)ip) >> bitCount;
            }
            while ((bitStream & 3) == 3)
            {
                n0+=3;
                bitStream>>=2;
                bitCount+=2;
            }
            n0 += bitStream & 3;
            bitCount += 2;
            while (charnum < n0) normalizedCounter[charnum++] = 0;
            ip += bitCount>>3;
            bitCount &= 7;
            bitStream = (*(U32*)ip) >> bitCount;
        }
        {
            const short max = (short)((2*threshold-1)-remaining);
            short count;

            if ((bitStream & (threshold-1)) < (U32)max)
            {
                count = (short)(bitStream & (threshold-1));
                bitCount   += nbBits-1;
            }
            else
            {
                count = (short)(bitStream & (2*threshold-1));
                if (count >= threshold) count -= max;
                bitCount   += nbBits;
            }

            count--;   // extra accuracy
            remaining -= FSE_abs(count);
            normalizedCounter[charnum++] = count;
            previous0 = !count;
            while (remaining < threshold)
            {
                nbBits--;
                threshold >>= 1;
            }

            ip += bitCount>>3;
            bitCount &= 7;
            bitStream = (*(U32*)ip) >> bitCount;
        }
    }
    *maxSymbolValuePtr = charnum-1;
    if (remaining < 1) return (size_t)-FSE_ERROR_GENERIC;

    ip += bitCount>0;
    return ip-istart;
}


/****************************************************************
*  FSE Compression Code
****************************************************************/
/*
CTable is a variable size structure which contains :
    U16 tableLog;
    U16 maxSymbolValue;
    U16 nextStateNumber[1 << tableLog];                         // This size is variable
    FSE_symbolCompressionTransform symbolTT[maxSymbolValue+1];  // This size is variable
Allocation is manual, since C standard does not support variable-size structures.
*/

size_t FSE_sizeof_CTable (unsigned maxSymbolValue, unsigned tableLog)
{
    size_t size;
    FSE_STATIC_ASSERT((size_t)FSE_CTABLE_SIZE_U32(FSE_MAX_TABLELOG, FSE_MAX_SYMBOL_VALUE)*4 >= sizeof(CTable_max_t));   /* A compilation error here means FSE_CTABLE_SIZE_U32 is not large enough */
    if (tableLog > FSE_MAX_TABLELOG) return (size_t)-FSE_ERROR_GENERIC;
    size = FSE_CTABLE_SIZE_U32 (tableLog, maxSymbolValue) * sizeof(U32);
    return size;
}

void* FSE_createCTable (unsigned maxSymbolValue, unsigned tableLog)
{
    size_t size;
    if (tableLog > FSE_TABLELOG_ABSOLUTE_MAX) tableLog = FSE_TABLELOG_ABSOLUTE_MAX;
    size = FSE_CTABLE_SIZE_U32 (tableLog, maxSymbolValue) * sizeof(U32);
    return malloc(size);
}

void  FSE_freeCTable (void* CTable)
{
    free(CTable);
}

/* Emergency distribution strategy (fallback); compression will suffer a lot ; consider increasing table size */
static void FSE_emergencyDistrib(short* normalizedCounter, int maxSymbolValue, short points)
{
    int s=0;
    while (points)
    {
        if (normalizedCounter[s] > 1)
        {
            normalizedCounter[s]--;
            points--;
        }
        s++;
        if (s>maxSymbolValue) s=0;
    }
}

/* fallback distribution (corner case); compression will suffer a bit ; consider increasing table size */
static void FSE_distribNpts(short* normalizedCounter, int maxSymbolValue, short points)
{
    int s;
    int rank[5] = {0};
    int fallback=0;

    /* Sort 4 largest (they'll absorb normalization rounding) */
    for (s=1; s<=maxSymbolValue; s++)
    {
        int i, b=3;
        if (b>=s) b=s-1;
        while ((b>=0) && (normalizedCounter[s]>normalizedCounter[rank[b]])) b--;
        for (i=3; i>b; i--) rank[i+1] = rank[i];
        rank[b+1]=s;
    }

    /* Distribute points */
    s = 0;
    while (points)
    {
        short limit = normalizedCounter[rank[s+1]]+1;
        if (normalizedCounter[rank[s]] >= limit + points )
        {
            normalizedCounter[rank[s]] -= points;
            break;
        }
        points -= normalizedCounter[rank[s]] - limit;
        normalizedCounter[rank[s]] = limit;
        s++;
        if (s==3)
        {
            short reduction = points>>2;
            if (fallback)
            {
                FSE_emergencyDistrib(normalizedCounter, maxSymbolValue, points);    // Fallback mode
                return;
            }
            if (reduction < 1) reduction=1;
            if (reduction >= normalizedCounter[rank[3]]) reduction=normalizedCounter[rank[3]]-1;
            fallback = (reduction==0);
            normalizedCounter[rank[3]]-=reduction;
            points-=reduction;
            s=0;
        }
    }
}


unsigned FSE_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue)
{
    U32 tableLog = maxTableLog;
    if (tableLog==0) tableLog = FSE_DEFAULT_TABLELOG;
    if ((FSE_highbit((U32)(srcSize - 1)) - 2) < tableLog) tableLog = FSE_highbit((U32)(srcSize - 1)) - 2;   /* Accuracy can be reduced */
    if ((FSE_highbit(maxSymbolValue)+1) > tableLog) tableLog = FSE_highbit(maxSymbolValue)+1;   /* Need a minimum to safely represent all symbol values */
    if (tableLog < FSE_MIN_TABLELOG) tableLog = FSE_MIN_TABLELOG;
    if (tableLog > FSE_MAX_TABLELOG) tableLog = FSE_MAX_TABLELOG;
    return tableLog;
}


size_t FSE_normalizeCount (short* normalizedCounter, unsigned tableLog,
                           const unsigned* count, size_t total,
                           unsigned maxSymbolValue)
{
    /* Sanity checks */
    if (tableLog==0) tableLog = FSE_DEFAULT_TABLELOG;
    if (tableLog < FSE_MIN_TABLELOG) return (size_t)-FSE_ERROR_GENERIC;   /* Unsupported size */
    if (tableLog > FSE_MAX_TABLELOG) return (size_t)-FSE_ERROR_GENERIC;   /* Unsupported size */

    {
        U32 const rtbTable[] = {     0, 473195, 504333, 520860, 550000, 700000, 750000, 830000 };
        U64 const scale = 62 - tableLog;
        U64 const step = ((U64)1<<62) / total;   /* <== here, one division ! */
        U64 const vStep = 1ULL<<(scale-20);
        int stillToDistribute = 1<<tableLog;
        unsigned s;
        unsigned largest=0;
        short largestP=0;
        U32 lowThreshold = (U32)(total >> tableLog);

        for (s=0; s<=maxSymbolValue; s++)
        {
            if (count[s] == total) return 0;
            if (count[s] == 0)
            {
                normalizedCounter[s]=0;
                continue;
            }
            if (count[s] <= lowThreshold)
            {
                normalizedCounter[s] = -1;
                stillToDistribute--;
            }
            else
            {
                short proba = (short)((count[s]*step) >> scale);
                if (proba<8)
                {
                    U64 restToBeat;
                    restToBeat = vStep * rtbTable[proba];
                    proba += (count[s]*step) - ((U64)proba<<scale) > restToBeat;
                }
                if (proba > largestP)
                {
                    largestP=proba;
                    largest=s;
                }
                normalizedCounter[s] = proba;
                stillToDistribute -= proba;
            }
        }
        if ((int)normalizedCounter[largest] <= -stillToDistribute+8)   /* largest cant accommodate that amount */
            FSE_distribNpts(normalizedCounter, maxSymbolValue, (short)(-stillToDistribute));   /* Fallback */
        else normalizedCounter[largest] += (short)stillToDistribute;
    }

#if 0
    {   /* Print Table (debug) */
        int s;
        for (s=0; s<=maxSymbolValue; s++)
            printf("%3i: %4i \n", s, normalizedCounter[s]);
        getchar();
    }
#endif

    return tableLog;
}


size_t FSE_buildCTable_rawUncompression (void* CTable, unsigned nbBits)
{
    const unsigned tableSize = 1 << nbBits;
    const unsigned tableMask = tableSize - 1;
    const unsigned maxSymbolValue = tableMask;
    U16* tableU16 = ( (U16*) CTable) + 2;
    FSE_symbolCompressionTransform* symbolTT = (FSE_symbolCompressionTransform*) (tableU16 + tableSize);
    unsigned s;

    /* Sanity checks */
    if (nbBits < 1) return (size_t)-FSE_ERROR_GENERIC;             /* min size */
    if (((size_t)CTable) & 3) return (size_t)-FSE_ERROR_GENERIC;   /* Must be allocated of 4 bytes boundaries */

    // header
    tableU16[-2] = (U16) nbBits;
    tableU16[-1] = (U16) maxSymbolValue;

    // Build table
    for (s=0; s<tableSize; s++)
        tableU16[s] = (U16)s;

    // Build Symbol Transformation Table
    for (s=0; s<=maxSymbolValue; s++)
    {
        symbolTT[s].minBitsOut = (BYTE)nbBits;
        symbolTT[s].deltaFindState = 0;
        symbolTT[s].maxState = (U16)( (tableSize*2) - 1);   // ensures state <= maxState
    }

    return 0;
}


size_t FSE_buildCTable_singleSymbol (void* CTable, BYTE symbolValue)
{
    const unsigned tableSize = 1;
    U16* tableU16 = ( (U16*) CTable) + 2;
    FSE_symbolCompressionTransform* symbolTT = (FSE_symbolCompressionTransform*) (tableU16 + tableSize);

    // Init checks
    if (((size_t)CTable) & 3) return (size_t)-FSE_ERROR_GENERIC;   // Must be allocated of 4 bytes boundaries

    // header
    tableU16[-2] = (U16) 0;
    tableU16[-1] = (U16) symbolValue;

    // Build table
    tableU16[0] = 0;
    tableU16[1] = 0;   // overwriting symbolTT[0].deltaFindState

    // Build Symbol Transformation Table
    {
        symbolTT[symbolValue].minBitsOut = 0;
        symbolTT[symbolValue].deltaFindState = 0;
        symbolTT[symbolValue].maxState = (U16)(2*tableSize-1);   // ensures state <= maxState
    }

    return 0;
}


void FSE_initCStream(FSE_CStream_t* bitC, void* start)
{
    bitC->bitContainer = 0;
    bitC->bitPos = 0;
    bitC->startPtr = (char*)start;
    bitC->ptr = bitC->startPtr + 4;
}

void   FSE_initCStreamCustom(FSE_CStream_t* bitC, void* start)
{
    bitC->bitContainer = 0;
    bitC->bitPos = 0;
    bitC->startPtr = (char*)start;
    bitC->ptr = bitC->startPtr;
}

void FSE_initCState(FSE_CState_t* statePtr, const void* CTable)
{
    const U32 tableLog = ( (U16*) CTable) [0];
    statePtr->value = (ptrdiff_t)1<<tableLog;
    statePtr->stateTable = ((const U16*) CTable) + 2;
    statePtr->symbolTT = ((const U16*)(statePtr->stateTable)) + ((size_t)1<<tableLog);
    statePtr->stateLog = tableLog;
}

void FSE_addBits(FSE_CStream_t* bitC, size_t value, unsigned nbBits)
{
    static const unsigned mask[] = { 0, 1, 3, 7, 0xF, 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF, 0x1FFFF, 0x3FFFF, 0x7FFFF, 0xFFFFF, 0x1FFFFF, 0x3FFFFF, 0x7FFFFF,  0xFFFFFF, 0x1FFFFFF };   // up to 25 bits
    bitC->bitContainer |= (value & mask[nbBits]) << bitC->bitPos;
    bitC->bitPos += nbBits;
}

void FSE_encodeByte(FSE_CStream_t* bitC, FSE_CState_t* statePtr, BYTE symbol)
{
    const FSE_symbolCompressionTransform* const symbolTT = (const FSE_symbolCompressionTransform*) statePtr->symbolTT;
    const U16* const stateTable = (const U16*) statePtr->stateTable;
    int nbBitsOut  = symbolTT[symbol].minBitsOut;
    nbBitsOut -= (int)((symbolTT[symbol].maxState - statePtr->value) >> 31);
    FSE_addBits(bitC, statePtr->value, nbBitsOut);
    statePtr->value = stateTable[ (statePtr->value >> nbBitsOut) + symbolTT[symbol].deltaFindState];
}

void FSE_flushBits(FSE_CStream_t* bitC)
{
    size_t nbBytes = bitC->bitPos >> 3;
    *(size_t*)(bitC->ptr) = bitC->bitContainer;
    bitC->bitPos &= 7;
    bitC->ptr += nbBytes;
    bitC->bitContainer >>= nbBytes*8;
}

void FSE_flushCState(FSE_CStream_t* bitC, const FSE_CState_t* statePtr)
{
    FSE_addBits(bitC, statePtr->value, statePtr->stateLog);
    FSE_flushBits(bitC);
}


size_t FSE_closeCStream(FSE_CStream_t* bitC, U32 optInfo)
{
    char* endPtr;
    U32 descriptor;
    U32 unusedBits;

    FSE_flushBits(bitC);

    endPtr = bitC->ptr;
    endPtr += bitC->bitPos > 0;
    unusedBits = 8-bitC->bitPos;
    if (unusedBits==8) unusedBits = 0;

    descriptor = (U32)(endPtr - bitC->startPtr) << 8;
    descriptor += unusedBits << 5;
    descriptor += optInfo;   // optional field [0-31]
    *(U32*)(bitC->startPtr) = descriptor;

    return (endPtr-bitC->startPtr);
}

size_t FSE_bitSizeofCStream(const FSE_CStream_t* bitC)
{
    return ((bitC->ptr - bitC->startPtr) * 8) + bitC->bitPos;
}


FORCE_INLINE size_t FSE_compress_usingCTable_generic (void* dst, size_t dstSize,
                                                      const void* src, size_t srcSize,
                                                      const void* CTable, int ilp)
{
    const BYTE* const istart = (const BYTE*) src;
    const BYTE* ip;
    const BYTE* const iend = istart + srcSize;

    int nbStreams = 1 + ilp;         // ilp : Instruction Level Parallelism
    FSE_CStream_t bitC;
    FSE_CState_t CState1, CState2;


    // init
    (void)dstSize;   // objective : ensure it fits into dstBuffer (Todo)
    FSE_initCStream(&bitC, dst);
    FSE_initCState(&CState1, CTable);
    CState2 = CState1;

    ip=iend;

    // join to even
    if (srcSize & 1)
    {
        FSE_encodeByte(&bitC, &CState1, *--ip);
        FSE_flushBits(&bitC);
    }

    // join to mod 4
    if ((sizeof(size_t)*8 > FSE_MAX_TABLELOG*4+7 ) && (srcSize & 2))   // test bit 2
    {
        FSE_encodeByte(&bitC, &CState1, *--ip);
        if (ilp) FSE_encodeByte(&bitC, &CState2, *--ip);
        else FSE_encodeByte(&bitC, &CState1, *--ip);
        FSE_flushBits(&bitC);
    }

    // 2 or 4 encoding per loop
    while (ip>istart)
    {
        FSE_encodeByte(&bitC, &CState1, *--ip);

        if (sizeof(size_t)*8 < FSE_MAX_TABLELOG*2+7 )   // this test must be static
            FSE_flushBits(&bitC);

        if (ilp) FSE_encodeByte(&bitC, &CState2, *--ip);
        else FSE_encodeByte(&bitC, &CState1, *--ip);

        if (sizeof(size_t)*8 > FSE_MAX_TABLELOG*4+7 )   // this test must be static
        {
            FSE_encodeByte(&bitC, &CState1, *--ip);
            if (ilp) FSE_encodeByte(&bitC, &CState2, *--ip);
            else FSE_encodeByte(&bitC, &CState1, *--ip);
        }

        FSE_flushBits(&bitC);
    }

    FSE_flushCState(&bitC, &CState2);
    FSE_flushCState(&bitC, &CState1);
    return FSE_closeCStream(&bitC, nbStreams);
}


size_t FSE_compress_usingCTable (void* dst, size_t dstSize, const void* src, size_t srcSize, const void* CTable)
{
    return FSE_compress_usingCTable_generic(dst, dstSize, src, srcSize, CTable, FSE_ILP);
}


static size_t FSE_writeSingleChar (BYTE *out, BYTE symbol)
{
    *out++=1;     // Header means ==> 1 symbol repeated across the whole sequence
    *out=symbol;
    return 2;
}

static size_t FSE_noCompression (BYTE* out, const BYTE* in, size_t isize)
{
    *out++=0;     // Header means ==> uncompressed
    memcpy (out, in, isize);
    return (isize+1);
}


size_t FSE_compressBound(size_t size) { return FSE_COMPRESSBOUND(size); }


size_t FSE_compress2 (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog)
{
    const BYTE* const istart = (const BYTE*) src;
    const BYTE* ip = istart;

    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + dstSize;

    U32   count[FSE_MAX_SYMBOL_VALUE+1];
    S16   norm[FSE_MAX_SYMBOL_VALUE+1];
    CTable_max_t CTable;
    size_t errorCode;

    // early out
    if (dstSize < FSE_compressBound(srcSize)) return (size_t)-FSE_ERROR_dstSize_tooSmall;
    if (srcSize <= 1) return FSE_noCompression (ostart, istart, srcSize);
    if (!maxSymbolValue) maxSymbolValue = FSE_MAX_SYMBOL_VALUE;
    if (!tableLog) tableLog = FSE_DEFAULT_TABLELOG;

    // Scan input and build symbol stats
    errorCode = (int)FSE_count (count, ip, srcSize, &maxSymbolValue);
    if (FSE_isError(errorCode)) return errorCode;
    if (errorCode == srcSize) return FSE_writeSingleChar (ostart, *istart);
    if (errorCode < ((srcSize * 7) >> 10)) return FSE_noCompression (ostart, istart, srcSize);   // Heuristic : not compressible enough

    tableLog = FSE_optimalTableLog(tableLog, srcSize, maxSymbolValue);
    errorCode = (int)FSE_normalizeCount (norm, tableLog, count, srcSize, maxSymbolValue);
    if (FSE_isError(errorCode)) return errorCode;

    // Write table description header
    errorCode = FSE_writeHeader (op, FSE_MAX_HEADERSIZE, norm, maxSymbolValue, tableLog);
    if (FSE_isError(errorCode)) return errorCode;
    op += errorCode;

    // Compress
    errorCode = FSE_buildCTable (&CTable, norm, maxSymbolValue, tableLog);
    if (FSE_isError(errorCode)) return errorCode;
    op += FSE_compress_usingCTable(op, oend - op, ip, srcSize, &CTable);

    // check compressibility
    if ( (size_t)(op-ostart) >= srcSize-1 )
        return FSE_noCompression (ostart, istart, srcSize);

    return op-ostart;
}


size_t FSE_compress (void* dst, size_t dstSize, const void* src, size_t srcSize)
{
    return FSE_compress2(dst, dstSize, src, (U32)srcSize, FSE_MAX_SYMBOL_VALUE, FSE_DEFAULT_TABLELOG);
}


/*********************************************************
*  Decompression (Byte symbols)
*********************************************************/
typedef struct
{
    U16  newState;
    BYTE symbol;
    BYTE nbBits;
} FSE_decode_t;   /* size == U32 */

int FSE_decompressRaw (void* out, int originalSize, const BYTE* in)
{
    memcpy (out, in+1, originalSize);
    return originalSize+1;
}

int FSE_decompressSingleSymbol (void* out, int osize, const BYTE symbol)
{
    memset (out, symbol, osize);
    return 2;
}


/* FSE_getDStreamSize
 * Read the header of an FSE block to provide its size in bytes (including its header).
 * The header block must be provided as source input.
 * Header block size is 4 bytes.
 * If srcSize is not large enough, function result will be an errorCode.
 */
size_t FSE_getDStreamSize(const void* srcBuffer, size_t srcSize, unsigned* optInfo)
{
    U32 header;
    if (srcSize < FSE_blockHeaderSize) return (size_t)-FSE_ERROR_srcSize_tooSmall;
    header = *(U32*)srcBuffer;
    *optInfo = header & 31;
    return header >> 8;
}


/* FSE_initDStream
 * Initialize a FSE_DStream_t.
 * srcBuffer must point at the beginning of an FSE block, starting with its header.
 * The function result is the size of the FSE_block.
 * If srcSize is not large enough, the function will return an errorCode;
 */
size_t FSE_initDStream(FSE_DStream_t* bitD, unsigned* optInfo, const void* srcBuffer, size_t srcSize)
{
    U32 header;
    U32 blockSize;
    if (srcSize < FSE_blockHeaderSize) return (size_t)-FSE_ERROR_srcSize_tooSmall;

    header = *(U32*)srcBuffer;
    *optInfo = header & 31;
    blockSize = header >> 8;
    if (blockSize > srcSize) return (size_t)-FSE_ERROR_srcSize_tooSmall;

    bitD->endPtr = (char*)srcBuffer;
    bitD->ptr    = (char*)srcBuffer + blockSize - 4;
    bitD->bitContainer = *(U32*)(bitD->ptr);
    bitD->bitsConsumed = (header >> 5) & 7;

    return blockSize;
}


/* FSE_initDStreamCustom
 * Initialize a FSE_DStream_t without standard header.
 * srcBuffer must point at the beginning of an FSE block (without header)
 * The function result is the size of the FSE_block (srcSize, which is trusted).
 */
size_t FSE_initDStreamCustom(FSE_DStream_t* bitD, const void* srcBuffer, size_t srcSize, size_t unusedBits)
{
    bitD->endPtr = (char*)srcBuffer - 4;   /* to be changed */
    bitD->ptr    = (char*)srcBuffer + srcSize - 4;
    bitD->bitContainer = *(U32*)(bitD->ptr);
    bitD->bitsConsumed = (U32)unusedBits;

    return srcSize;
}


/* FSE_readBits
 * Read next n bits from the bitContainer.
 * Use the fast variant *only* if n > 0.
 * Note : for this function to work properly, don't read more than maxNbBits==25
 * return : value extracted.
 */
U32 FSE_readBits(FSE_DStream_t* bitD, U32 nbBits)
{
    U32 value = ((bitD->bitContainer << bitD->bitsConsumed) >> 1) >> (31-nbBits);
    bitD->bitsConsumed += nbBits;
    return value;
}

U32 FSE_readBitsFast(FSE_DStream_t* bitD, U32 nbBits)   /* only if nbBits >= 1 */
{
    U32 value = (bitD->bitContainer << bitD->bitsConsumed) >> (32-nbBits);
    bitD->bitsConsumed += nbBits;
    return value;
}

void FSE_reloadDStream(FSE_DStream_t* bitD)
{
    bitD->ptr -= bitD->bitsConsumed >> 3;
    bitD->bitsConsumed &= 7;
    bitD->bitContainer = * (U32*) (bitD->ptr);
}

U32  FSE_reloadIsSafe(const FSE_DStream_t* bitD)
{
    return (bitD->ptr > (bitD->endPtr + 8));
}

/* FSE_initDState
 */
void FSE_initDState(FSE_DState_t* DStatePtr, FSE_DStream_t* bitD, const void* DTable, unsigned tableLog)
{
    DStatePtr->state = FSE_readBits(bitD, tableLog);
    FSE_reloadDStream(bitD);
    DStatePtr->table = DTable;
}

BYTE FSE_decodeSymbol(FSE_DState_t* DStatePtr, FSE_DStream_t* bitD)
{
    const FSE_decode_t DInfo = ((const FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    const U32  nbBits = DInfo.nbBits;
    BYTE symbol = DInfo.symbol;
    U32  lowBits = FSE_readBits(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

BYTE FSE_decodeSymbolFast(FSE_DState_t* DStatePtr, FSE_DStream_t* bitD, unsigned fast)
{
    const FSE_decode_t DInfo = ((const FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    const U32 nbBits = DInfo.nbBits;
    BYTE symbol = DInfo.symbol;
    U32 lowBits = fast ? FSE_readBitsFast(bitD, nbBits) : FSE_readBits(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

/* FSE_endOfBitStream
   Tells if bitD has reached end of bitStream or not */
size_t FSE_endOfBitStream(const FSE_DStream_t* bitD, const FSE_DState_t* DStatePtr)
{
    return ((bitD->ptr == bitD->endPtr) && (DStatePtr->state==0) && (bitD->bitsConsumed==0));
}


FORCE_INLINE size_t FSE_decompressStreams_usingDTable_generic(
    unsigned char* dest, const unsigned originalSize,
    const void* compressed, unsigned maxCompressedSize,
    const void* DTable, const unsigned tableLog, unsigned safe, unsigned nbStates, unsigned fast)
{
    BYTE* op = (BYTE*) dest;
    BYTE* const oend = op + originalSize;
    BYTE* const olimit = oend-1;

    U32 optInfo;
    FSE_DStream_t bitD;
    FSE_DState_t state1, state2;

    /* Init */
    const size_t blockSize = FSE_initDStream(&bitD, &optInfo, compressed, safe ? maxCompressedSize : (size_t)(-1));   /* replaced last arg by maxCompressed Size */
    if (FSE_isError(blockSize)) return (size_t)-FSE_ERROR_GENERIC;

    (void)maxCompressedSize; (void)safe; (void)fast;
    FSE_initDState(&state1, &bitD, DTable, tableLog);
    if (nbStates==2) FSE_initDState(&state2, &bitD, DTable, tableLog);


    /* 2 symbols per loop */
    while (op<olimit)
    {
        if (nbStates==2) *op++ = FSE_decodeSymbolFast(&state2, &bitD, fast);
        else *op++ = FSE_decodeSymbolFast(&state1, &bitD, fast);

        if (FSE_MAX_TABLELOG*2+7 > sizeof(U32)*8)    /* This test must be static */
            FSE_reloadDStream(&bitD);

        *op++ = FSE_decodeSymbolFast(&state1, &bitD, fast);
        FSE_reloadDStream(&bitD);
    }

    // last symbol
    if (op<oend)
    {
        *op++ = FSE_decodeSymbolFast(&state1, &bitD, fast);
        FSE_reloadDStream(&bitD);
    }

    if (!FSE_endOfBitStream(&bitD, &state1)) return (size_t)-FSE_ERROR_GENERIC;

    return blockSize;
}

U32 FSE_getOptInfo(const void* buffer)
{
    U32 header = * (U32*)buffer;
    return header&31;
}

static size_t FSE_decompress_usingDTable_generic(
    unsigned char* dest, const unsigned originalSize, const void* compressed, unsigned maxCompressedSize,
    const void* DTable, const unsigned tableLog, unsigned safe, unsigned fast)
{
    U32 nbStates = FSE_getOptInfo(compressed);
    if (nbStates==2)
        return FSE_decompressStreams_usingDTable_generic(dest, originalSize, compressed, maxCompressedSize, DTable, tableLog, safe, 2, fast);
    if (nbStates==1)
        return FSE_decompressStreams_usingDTable_generic(dest, originalSize, compressed, maxCompressedSize, DTable, tableLog, safe, 1, fast);
    return -1;   // should not happen
}

FORCE_INLINE size_t FSE_decompress_usingDTable_fastModeSelector(
    unsigned char* dest, const unsigned originalSize, const void* compressed, unsigned maxCompressedSize,
    const void* DTable, const unsigned tableLog, unsigned safe, unsigned fast)
{
    if (fast) return FSE_decompress_usingDTable_generic(dest, originalSize, compressed, maxCompressedSize, DTable, tableLog, safe, 1);
    return FSE_decompress_usingDTable_generic(dest, originalSize, compressed, maxCompressedSize, DTable, tableLog, safe, 0);
}

size_t FSE_decompress_usingDTable (unsigned char* dest, const unsigned originalSize, const void* compressed, const void* DTable, const unsigned tableLog, unsigned fast)
{
    return FSE_decompress_usingDTable_fastModeSelector(dest, originalSize, compressed, 0, DTable, tableLog, 0, fast);
}

int FSE_decompress_usingDTable_safe (unsigned char* dest, const unsigned originalSize, const void* compressed, unsigned maxCompressedSize, const void* DTable, const unsigned tableLog, unsigned fast)
{
    return FSE_decompress_usingDTable_fastModeSelector(dest, originalSize, compressed, maxCompressedSize, DTable, tableLog, 1, fast);
}


size_t FSE_decompress(void* dst, size_t originalSize, const void* compressed, size_t maxCompressedSize)
{
    const BYTE* const istart = (const BYTE*)compressed;
    const BYTE* ip = istart;
    short counting[FSE_MAX_SYMBOL_VALUE+1];
    FSE_decode_t DTable[FSE_MAX_TABLESIZE];
    BYTE  headerId;
    unsigned maxSymbolValue;
    unsigned tableLog;
    size_t errorCode, fastMode;
    const unsigned safe = 0;   /* TBD */

    if ((safe) && (maxCompressedSize<2)) return (size_t)-FSE_ERROR_srcSize_tooSmall;   // too small input size

    // headerId early outs
    headerId = ip[0] & 3;
    if (ip[0]==0)   // Raw (uncompressed) data
    {
        if (safe && maxCompressedSize < originalSize + 1) return -1;
        return FSE_decompressRaw (dst, originalSize, istart);
    }
    if (ip[0]==1) return FSE_decompressSingleSymbol (dst, originalSize, istart[1]);
    if (headerId!=2) return (size_t)-FSE_ERROR_GENERIC;   // unused headerId

    // normal FSE decoding mode
    errorCode = FSE_readHeader (counting, &maxSymbolValue, &tableLog, istart);
    if (FSE_isError(errorCode)) return (size_t)-FSE_ERROR_GENERIC;
    ip += errorCode;

    fastMode = FSE_buildDTable (DTable, counting, maxSymbolValue, tableLog);
    if (FSE_isError(fastMode)) return (size_t)-FSE_ERROR_GENERIC;

    if (safe) errorCode = FSE_decompress_usingDTable_safe (dst, originalSize, ip, maxCompressedSize, DTable, tableLog, fastMode);
    else errorCode = FSE_decompress_usingDTable (dst, originalSize, ip, DTable, tableLog, fastMode);
    if (FSE_isError(errorCode)) return (size_t)-FSE_ERROR_GENERIC;
    ip += errorCode;

    return ip-istart;
}


#endif   // FSE_COMMONDEFS_ONLY

/*
  2nd part of the file
  designed to be included
  for type-specific functions (template equivalent in C)
  Objective is to write such functions only once, for better maintenance
*/

// checks
#ifndef FSE_FUNCTION_EXTENSION
#  error "FSE_FUNCTION_EXTENSION must be defined"
#endif
#ifndef FSE_FUNCTION_TYPE
#  error "FSE_FUNCTION_TYPE must be defined"
#endif

// Function names
#define FSE_CAT(X,Y) X##Y
#define FSE_FUNCTION_NAME(X,Y) FSE_CAT(X,Y)
#define FSE_TYPE_NAME(X,Y) FSE_CAT(X,Y)


// Functions
size_t FSE_FUNCTION_NAME(FSE_count_generic, FSE_FUNCTION_EXTENSION) (unsigned* count, const FSE_FUNCTION_TYPE* source, size_t sourceSize, unsigned* maxSymbolValuePtr, unsigned safe)
{
    const FSE_FUNCTION_TYPE* ip = source;
    const FSE_FUNCTION_TYPE* const iend = ip+sourceSize;
    unsigned maxSymbolValue = *maxSymbolValuePtr;
    unsigned max=0;
    int s;

    U32 Counting1[FSE_MAX_SYMBOL_VALUE+1] = { 0 };
    U32 Counting2[FSE_MAX_SYMBOL_VALUE+1] = { 0 };
    U32 Counting3[FSE_MAX_SYMBOL_VALUE+1] = { 0 };
    U32 Counting4[FSE_MAX_SYMBOL_VALUE+1] = { 0 };

    // Init checks
    if (!sourceSize)
    {
        memset(count, 0, (maxSymbolValue + 1) * sizeof(FSE_FUNCTION_TYPE));
        *maxSymbolValuePtr = 0;
        return 0;
    }
    if (maxSymbolValue > FSE_MAX_SYMBOL_VALUE) return (size_t)-FSE_ERROR_GENERIC;   // maxSymbolValue too large : unsupported
    if (!maxSymbolValue) maxSymbolValue = FSE_MAX_SYMBOL_VALUE;            // 0: default

    if (safe)
    {
        // check input value in this variant, to avoid count table overflow
        while (ip < iend-3)
        {
            if (*ip>maxSymbolValue) return (size_t)-FSE_ERROR_GENERIC; Counting1[*ip++]++;
            if (*ip>maxSymbolValue) return (size_t)-FSE_ERROR_GENERIC; Counting2[*ip++]++;
            if (*ip>maxSymbolValue) return (size_t)-FSE_ERROR_GENERIC; Counting3[*ip++]++;
            if (*ip>maxSymbolValue) return (size_t)-FSE_ERROR_GENERIC; Counting4[*ip++]++;
        }
    }
    else
    {
        U32 cached = *(U32 *)ip; ip += 4;
        while (ip < iend-15)
        {
            U32 c = cached; cached = *(U32 *)ip; ip += 4;
            Counting1[(BYTE) c     ]++;
            Counting2[(BYTE)(c>>8) ]++;
            Counting3[(BYTE)(c>>16)]++;
            Counting4[       c>>24 ]++;
            c = cached; cached = *(U32 *)ip; ip += 4;
            Counting1[(BYTE) c     ]++;
            Counting2[(BYTE)(c>>8) ]++;
            Counting3[(BYTE)(c>>16)]++;
            Counting4[       c>>24 ]++;
            c = cached; cached = *(U32 *)ip; ip += 4;
            Counting1[(BYTE) c     ]++;
            Counting2[(BYTE)(c>>8) ]++;
            Counting3[(BYTE)(c>>16)]++;
            Counting4[       c>>24 ]++;
            c = cached; cached = *(U32 *)ip; ip += 4;
            Counting1[(BYTE) c     ]++;
            Counting2[(BYTE)(c>>8) ]++;
            Counting3[(BYTE)(c>>16)]++;
            Counting4[       c>>24 ]++;
        }
        ip-=4;
    }
    while (ip<iend) { if ((safe) && (*ip>maxSymbolValue)) return (size_t)-FSE_ERROR_GENERIC; Counting1[*ip++]++; }

    for (s=0; s<=(int)maxSymbolValue; s++)
    {
        count[s] = Counting1[s] + Counting2[s] + Counting3[s] + Counting4[s];
        //max = count[s] > max ? count[s] : max;
        if (count[s] > max) max = count[s];
    }

    while (!count[maxSymbolValue]) maxSymbolValue--;
    *maxSymbolValuePtr = maxSymbolValue;
    return (int)max;
}

/* hidden fast variant (unsafe) */
size_t FSE_FUNCTION_NAME(FSE_countFast, FSE_FUNCTION_EXTENSION) (unsigned* count, const FSE_FUNCTION_TYPE* source, size_t sourceSize, unsigned* maxSymbolValuePtr)
{
    return FSE_FUNCTION_NAME(FSE_count_generic, FSE_FUNCTION_EXTENSION) (count, source, sourceSize, maxSymbolValuePtr, 0);
}

size_t FSE_FUNCTION_NAME(FSE_count, FSE_FUNCTION_EXTENSION) (unsigned* count, const FSE_FUNCTION_TYPE* source, size_t sourceSize, unsigned* maxSymbolValuePtr)
{
    if ((sizeof(FSE_FUNCTION_TYPE)==1) && (*maxSymbolValuePtr >= 255))
    {
        *maxSymbolValuePtr = 255;
        return FSE_FUNCTION_NAME(FSE_count_generic, FSE_FUNCTION_EXTENSION) (count, source, sourceSize, maxSymbolValuePtr, 0);
    }
    return FSE_FUNCTION_NAME(FSE_count_generic, FSE_FUNCTION_EXTENSION) (count, source, sourceSize, maxSymbolValuePtr, 1);
}


static U32 FSE_tableStep(U32 tableSize) { return (tableSize>>1) + (tableSize>>3) + 3; }

size_t FSE_FUNCTION_NAME(FSE_buildCTable, FSE_FUNCTION_EXTENSION)
(void* CTable, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog)
{
    const unsigned tableSize = 1 << tableLog;
    const unsigned tableMask = tableSize - 1;
    U16* tableU16 = ( (U16*) CTable) + 2;
    FSE_symbolCompressionTransform* symbolTT = (FSE_symbolCompressionTransform*) (tableU16 + tableSize);
    const unsigned step = FSE_tableStep(tableSize);
    unsigned cumul[FSE_MAX_SYMBOL_VALUE+2];
    U32 position = 0;
    FSE_FUNCTION_TYPE tableSymbol[FSE_MAX_TABLESIZE];
    U32 highThreshold = tableSize-1;
    unsigned symbol;
    unsigned i;

    // Init checks
    if (((size_t)CTable) & 3) return (size_t)-FSE_ERROR_GENERIC;   // Must be allocated of 4 bytes boundaries

    // header
    tableU16[-2] = (U16) tableLog;
    tableU16[-1] = (U16) maxSymbolValue;

    // For explanations on how to distribute symbol values over the table :
    // http://fastcompression.blogspot.fr/2014/02/fse-distributing-symbol-values.html

    // symbol start positions
    cumul[0] = 0;
    for (i=1; i<=maxSymbolValue+1; i++)
    {
        if (normalizedCounter[i-1]==-1)   // Low prob symbol
        {
            cumul[i] = cumul[i-1] + 1;
            tableSymbol[highThreshold--] = (FSE_FUNCTION_TYPE)(i-1);
        }
        else
            cumul[i] = cumul[i-1] + normalizedCounter[i-1];
    }
    cumul[maxSymbolValue+1] = tableSize+1;

    // Spread symbols
    for (symbol=0; symbol<=maxSymbolValue; symbol++)
    {
        int nbOccurences;
        for (nbOccurences=0; nbOccurences<normalizedCounter[symbol]; nbOccurences++)
        {
            tableSymbol[position] = (FSE_FUNCTION_TYPE)symbol;
            position = (position + step) & tableMask;
            while (position > highThreshold) position = (position + step) & tableMask;   // Lowprob area
        }
    }

    if (position!=0) return (size_t)-FSE_ERROR_GENERIC;   // Must have gone through all positions

    // Build table
    for (i=0; i<tableSize; i++)
    {
        FSE_FUNCTION_TYPE s = tableSymbol[i];
        tableU16[cumul[s]++] = (U16) (tableSize+i);   // Table U16 : sorted by symbol order; gives next state value
    }

    // Build Symbol Transformation Table
    {
        unsigned s;
        unsigned total = 0;
        for (s=0; s<=maxSymbolValue; s++)
        {
            switch (normalizedCounter[s])
            {
            case 0:
                break;
            case -1:
            case 1:
                symbolTT[s].minBitsOut = (BYTE)tableLog;
                symbolTT[s].deltaFindState = total - 1;
                total ++;
                symbolTT[s].maxState = (U16)( (tableSize*2) - 1);   // ensures state <= maxState
                break;
            default :
                symbolTT[s].minBitsOut = (BYTE)( (tableLog-1) - FSE_highbit (normalizedCounter[s]-1) );
                symbolTT[s].deltaFindState = total - normalizedCounter[s];
                total +=  normalizedCounter[s];
                symbolTT[s].maxState = (U16)( (normalizedCounter[s] << (symbolTT[s].minBitsOut+1)) - 1);
            }
        }
    }

    return 0;
}


#define FSE_DECODE_TYPE FSE_TYPE_NAME(FSE_decode_t, FSE_FUNCTION_EXTENSION)

void* FSE_FUNCTION_NAME(FSE_createDTable, FSE_FUNCTION_EXTENSION) (unsigned tableLog)
{
    if (tableLog > FSE_TABLELOG_ABSOLUTE_MAX) tableLog = FSE_TABLELOG_ABSOLUTE_MAX;
    return malloc( (1<<tableLog) * sizeof (FSE_DECODE_TYPE) );
}

void FSE_FUNCTION_NAME(FSE_freeDTable, FSE_FUNCTION_EXTENSION) (void* DTable)
{
    free(DTable);
}


size_t FSE_FUNCTION_NAME(FSE_buildDTable, FSE_FUNCTION_EXTENSION)
(void* DTable, const short* const normalizedCounter, unsigned maxSymbolValue, unsigned tableLog)
{
    FSE_DECODE_TYPE* const tableDecode = (FSE_DECODE_TYPE*) DTable;
    const U32 tableSize = 1 << tableLog;
    const U32 tableMask = tableSize-1;
    const U32 step = FSE_tableStep(tableSize);
    U16 symbolNext[FSE_MAX_SYMBOL_VALUE+1];
    U32 position = 0;
    U32 highThreshold = tableSize-1;
    const S16 largeLimit= 1 << (tableLog-1);
    U32 noLarge = 1;
    U32 s;

    // Checks
    if (maxSymbolValue > FSE_MAX_SYMBOL_VALUE) return (size_t)-FSE_ERROR_maxSymbolValue_tooLarge;
    if (tableLog > FSE_MAX_TABLELOG) return (size_t)-FSE_ERROR_tableLog_tooLarge;

    // Init, lay down lowprob symbols
    for (s=0; s<=maxSymbolValue; s++)
    {
        if (normalizedCounter[s]==-1)
        {
            tableDecode[highThreshold--].symbol = (FSE_FUNCTION_TYPE)s;
            symbolNext[s] = 1;
        }
        else
        {
            if (normalizedCounter[s] >= largeLimit) noLarge=0;
            symbolNext[s] = normalizedCounter[s];
        }
    }

    // Spread symbols
    for (s=0; s<=maxSymbolValue; s++)
    {
        int i;
        for (i=0; i<normalizedCounter[s]; i++)
        {
            tableDecode[position].symbol = (FSE_FUNCTION_TYPE)s;
            position = (position + step) & tableMask;
            while (position > highThreshold) position = (position + step) & tableMask;   // lowprob area
        }
    }

    if (position!=0) return (size_t)-FSE_ERROR_GENERIC;   // position must reach all cells once, otherwise normalizedCounter is incorrect

    // Build table Decoding table
    {
        U32 i;
        for (i=0; i<tableSize; i++)
        {
            FSE_FUNCTION_TYPE symbol = tableDecode[i].symbol;
            U16 nextState = symbolNext[symbol]++;
            tableDecode[i].nbBits = (BYTE) (tableLog - FSE_highbit (nextState) );
            tableDecode[i].newState = (U16) ( (nextState << tableDecode[i].nbBits) - tableSize);
        }
    }

    return noLarge;
}
