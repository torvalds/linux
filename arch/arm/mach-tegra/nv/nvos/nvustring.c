/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "nvutil.h"
#include "nvassert.h"

//===========================================================================
// NvUIsdigit() - like the standard isdigit function
//===========================================================================
static int NvUIsdigit(int c)
{
    return (c>='0' && c<='9');
}

//===========================================================================
// NvUIsxdigit() - like the standard isxdigit function
//===========================================================================
static int NvUIsxdigit(int c)
{
    return (c>='0' && c<='9') || (c>='A' && c<='F') || (c>='a' && c<='f');
}

//===========================================================================
// NvUCharToXDigit() - convert a hex character to its value
//===========================================================================
static int NvUCharToXDigit(int c)
{
    return (c>='0' && c<='9') ? c - '0' :
           (c>='a' && c<='f') ? c - 'a' + 10 :
           (c>='A' && c<='F') ? c - 'A' + 10 : -1;
}

//===========================================================================
// NvUStrtoull() - like the standard strtoull function
//===========================================================================
unsigned long long int NvUStrtoull(const char *s, char **endptr, int base)
{
    int neg = 0;
    unsigned long long int val = 0;

    NV_ASSERT(s);
    NV_ASSERT(base==0 || base==10 || base==16);

    if (*s == '-') {
        s++;
        neg = 1;
    }
    if (s[0]=='0' && (s[1]=='x' || s[1]=='X')) {
        if (base == 10) {
            if (endptr) {
                *endptr = (char*)s+1;
                return val;
            }
        }
        s += 2;
        base = 16;
    }

    if (base == 16) {
        while (NvUIsxdigit(*s)) {
            val <<= 4;
            val +=  NvUCharToXDigit(*s);
            s++;
        }
    } else {
        while (NvUIsdigit(*s)) {
            val *= 10;
            val += NvUCharToXDigit(*s);
            s++;
        }
    }

    if (endptr) {
        *endptr = (char*)s;
    }
    return neg ? ((~val)+1) : val;
}

//===========================================================================
// NvUStrtoul() - like the standard strtoul function
//===========================================================================
unsigned long int NvUStrtoul(const char *s, char **endptr, int base)
{
    return (unsigned long)NvUStrtoull( s, endptr, base );
}

//===========================================================================
// NvUStrtol() - like the standard strtol function
//===========================================================================
long int NvUStrtol(const char *s, char **endptr, int base)
{
    return (long int)NvUStrtoul(s,endptr,base);
}

//===========================================================================
// NvUStrncat() - like the standard strcat function
//===========================================================================
void NvUStrncat(char *dest, const char *src, size_t n)
{
    while(*dest) dest++;
    while(*src && n--) {
        *(dest++) = *(src++);
    }
    *dest = 0;
}

//===========================================================================
// NvUStrstr() - like the standard strstr function
//===========================================================================
char *
NvUStrstr( const char *str1, const char *str2 )
{
    char s2;
    NvU32 len;

    NV_ASSERT( str1 );
    NV_ASSERT( str2 );
    
    s2 = *str2++;

    // empty string case
    if (!s2) {
        return (char *)str1;
    }
    
    len = NvOsStrlen(str2);
    do {
        char s1;

        do {
            s1 = *str1++;
            if (!s1) {
                return (char *)0;
            }
        } while (s1 != s2);
    } while (NvOsStrncmp(str1, str2, len) != 0);

    return (char *)(str1 - 1);
}

//===========================================================================
// NvUStrlConvertCodePage() - see definition in nvutil.h
//   Lots of static helper functions to get/put characters in various
//   code pages.  For reference on the encodings, see:
//   http://en.wikipedia.org/wiki/Windows-1252
//   http://en.wikipedia.org/wiki/UTF-8
//   http://en.wikipedia.org/wiki/UTF-16
//===========================================================================
typedef const void* (*StrGetFn)(const void*, NvU32*, size_t*);
typedef size_t (*StrPutFn)(void*, NvU32);

static const void*
NvUStr_GetUtf8Coding(const void *pSrc, 
                     NvU32 *Coding,
                     size_t *SrcSize)
{
    const char *pCh = (const char *)pSrc;
    NvU32 tmp = 0;
    NvU8 ch;

    if (!*SrcSize)
    {
        *Coding = 0;
        return pSrc;
    }
    else
    {
        ch = (NvU8)*pCh++;
        *SrcSize = *SrcSize-1;
    }

    if (*SrcSize && (ch & 0x80))
    {
        tmp = ((ch>>4) & 0x3);
        if (tmp)
          tmp--;
        tmp = (ch & (0x1f>>tmp));
        do
        {
            ch = (NvU8)*pCh++;
            tmp<<=6;
            tmp |= (ch & 0x3f);
            *SrcSize = *SrcSize - 1;
        } while (*SrcSize && ((NvU8)*pCh & 0xc0)==0x80);

    }
    else
    {
        tmp = (NvU32)(ch&0x7f);
    }

    *Coding = tmp;
    return (const void *)pCh;
}

static const void*
NvUStr_GetUtf16Coding(const void *pSrc,
                      NvU32 *Coding,
                      size_t *SrcSize)
{
    const wchar_t *pCh = (const wchar_t *)pSrc;
    NvU32 tmp = 0;

    if (*SrcSize<2)
    {
        *Coding = 0;
        *SrcSize = 0;
        return pSrc;
    }

    tmp = (NvU32) *pCh++;
    *SrcSize = *SrcSize - 2;

    if ((*SrcSize>1) && ((tmp & 0xd800UL) == 0xd800UL))
    {
        tmp = 0x10000UL + (((tmp & 0x3ff)<<10) | (((NvU32)*pCh++) & 0x3ffUL));
        *SrcSize = *SrcSize - 2;
    }

    *Coding = tmp;
    return (const void *)pCh;
}

static const NvU16 Windows1252EscapeRemapTable[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x2C26, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02Dc, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178 };

static const void*
NvUStr_GetWindows1252Coding(const void *pSrc,
                            NvU32 *Coding,
                            size_t *SrcSize)
{
    //  the following table is used to remap windows-1252 codings 0x80-0x9f to
    //  the closest unicode codings.  reference:
    // http://en.wikipedia.org/wiki/Windows-1252

    const char *pCh = (const char *)pSrc;
    NvU32 tmp;
    
    if (!*SrcSize)
    {
        *Coding = 0;
        return pSrc;
    }
    tmp = (NvU32)*pCh++;
    *SrcSize = *SrcSize - 1;

    if (tmp>=0x80 && tmp<0xA0) tmp =
        (NvU32) Windows1252EscapeRemapTable[tmp-0x80];

    return (const void *) pCh;
}

static size_t
NvUStr_PutUtf8Coding(void *pDest,
                     NvU32 Coding)
{
    unsigned int bytes;
    unsigned int i;
    unsigned int mask;
    unsigned int shift;
    NvU8 *pCh = (NvU8 *)pDest;

    if (Coding < 0x80)
        bytes = 1;
    else if (Coding < 0x800UL)
        bytes = 2;
    else if (Coding < 0x10000UL)
        bytes = 3;
    else
        bytes = 4;

    if (pCh)
    {
        mask = 0x7f;
        if (bytes>1)
        {
            mask >>= bytes;
        }
        shift = (bytes-1)*6;
        i = bytes;
        while (i--)
        {
            *pCh++ = (((~((mask<<1)|1))&0xff) | 
                         ((Coding>>shift) & mask));
            shift -= 6;
            mask = 0x3f;
        }
    }
    
    return (size_t)bytes;
}

static size_t
NvUStr_PutUtf16Coding(void *pDest,
                      NvU32 Coding)

{
    size_t bytes = (Coding > 0x10000UL) ? 4 : 2;
    NvU16 *pCh = (NvU16 *)pDest;

    if (pCh)
    {
        if (bytes==4)
        {
            Coding -= 0x10000UL;
            *pCh++ = (NvU16) (0xd800UL | ((Coding>>10)&0x3ffUL));
            *pCh++ = (NvU16) (0xdb00UL | (Coding & 0x3ffUL));
        }
        else
            *pCh++ = (NvU16) (Coding & 0xffffUL);
    }

    return bytes;
}

static size_t
NvUStr_PutWindows1252Coding(void *pDest,
                            NvU32 Coding)
{
    NvU8 *pCh = (NvU8 *)pDest;
    unsigned int i;

    if (pCh)
    {
        if ((Coding<0x80UL) || ((Coding<0x100UL)&&(Coding>0x9FUL)))
            *pCh++ = (NvU8)(Coding & 0xff);
        else
        {
            for (i=0; i<32 && (NvU32)Windows1252EscapeRemapTable[i]!=Coding; i++) { }
            *pCh++ = ((i==32) ? 0x90 : ((0x80+i) & 0xff));
        }
    }
    return 1;
}

size_t
NvUStrlConvertCodePage(void *pDest,
                       size_t DestSize,
                       NvOsCodePage DestCodePage,
                       const void *pSrc,
                       size_t SrcSize,
                       NvOsCodePage SrcCodePage)
{
    StrGetFn GetChar = NULL;
    StrPutFn PutChar = NULL;
    char *pStr = (char *)pDest;
    size_t OutputSize = 0;
    size_t CodeSize = 0;
    size_t Remain = SrcSize;
    NvU32 Coding;

    if (!pSrc)
        return 0;
    //  to simplify down-stream code paths, if the source is NULL-terminated
    //  (SrcSize==0), or the destination is NULL, set the corresponding sizes
    //  to ~0 (effectively infinite, since memory will be filled before the
    //  size limit is reached)
    if (!pDest)
        DestSize = (size_t)~0;
    if (!Remain)
        Remain = (size_t)~0;

    if (DestCodePage == NvOsCodePage_Unknown)
        DestCodePage = NvOsStrGetSystemCodePage();
    if (SrcCodePage == NvOsCodePage_Unknown)
        SrcCodePage = NvOsStrGetSystemCodePage();

    switch (DestCodePage)
    {
    case NvOsCodePage_Utf8:
        PutChar = NvUStr_PutUtf8Coding; break;
    case NvOsCodePage_Utf16:
        PutChar = NvUStr_PutUtf16Coding; break;
    case NvOsCodePage_Windows1252:
        PutChar = NvUStr_PutWindows1252Coding; break;
    default:
        NV_ASSERT(!"Unsupported destination code page");
        return 0;
    }

    //  the NULL terminator in Unicode is 0; compute the size of the terminator
    //  in the destination coding by calling the PutChar routine once with
    //  coding zero.
    OutputSize = PutChar(NULL, 0);
    if (OutputSize > DestSize)
      return 0;

    switch (SrcCodePage)
    {
    case NvOsCodePage_Utf8: GetChar =
        NvUStr_GetUtf8Coding; break;
    case NvOsCodePage_Utf16: GetChar =
        NvUStr_GetUtf16Coding; break;
    case NvOsCodePage_Windows1252: GetChar =
        NvUStr_GetWindows1252Coding; break;
    default:
        NV_ASSERT(!"Unsupported source code page");
        return 0;
    }

    //  optimized path for conversions of the lower 128 ASCII characters
    if (( (DestCodePage == NvOsCodePage_Utf8) || 
          (DestCodePage == NvOsCodePage_Windows1252)) &&
        (SrcCodePage == NvOsCodePage_Utf16))
    {
        const NvU16 *pCh = (const NvU16 *)pSrc;
        while (*pCh && (*pCh<0x80) && (OutputSize < DestSize) && Remain)
        {
            if (pStr)
                *pStr++ = (char)*pCh;
            OutputSize++;
            Remain -= 2;
            pCh++;
        }
        pSrc = (const void *)pCh;
    }
    else if ((DestCodePage == NvOsCodePage_Utf16) &&
             ( (SrcCodePage == NvOsCodePage_Utf8) ||
               (SrcCodePage == NvOsCodePage_Windows1252)))
    {
        const NvU8 *pCh = (const NvU8 *)pSrc;
        wchar_t    *pStrW = (wchar_t *)pStr;
        while (*pCh && (*pCh<0x80) && (OutputSize < DestSize) && Remain)
        {
            if (pStrW)
                *pStrW++ = (wchar_t)*pCh;
            OutputSize+=2;
            Remain--;
            pCh++;
        }
        pStr = (char *)pStrW;
        pSrc = (const void *)pCh;
    }

    pSrc = GetChar(pSrc, &Coding, &Remain);
    //  All the GetChar* functions return a NULL coding when insufficient
    //  source bytes remain, so we don't need to check it in the loop
    while (Coding)
    {
        CodeSize = PutChar(NULL, Coding);
        if (pStr)
        {
            if ((OutputSize + CodeSize)<=DestSize)
            {
                pStr += PutChar(pStr, Coding);
                OutputSize += CodeSize;
            }
            else
                break;
        }
        else
            OutputSize += CodeSize;

        pSrc = GetChar(pSrc, &Coding, &Remain);
    }
    if (pStr)
    {
        pStr += PutChar(pStr, 0);
    }

    return OutputSize;
}

NvU32
NvULowestBitSet( NvU32 bits, NvU32 nBits )
{
    NvU32 ret = 0;

    if( nBits > 16 )
    {
        if( !(bits & 0xffff) )
        {
            ret += 16;
            bits >>= 16;
        }
    }

    if( nBits > 8 )
    {
        if( !(bits & 0xff) )
        {
            ret += 8;
            bits >>= 8;
        }
    }

    if( !(bits & 0xf) )
    {
        ret += 4;
        bits >>= 4;
    }

    if( !(bits & 0x3) )
    {
        ret += 2;
        bits >>= 2;
    }

    return ret + ((bits & 1) ? 0 : 1 );
}
