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

#ifndef INCLUDED_NVUTIL_H
#define INCLUDED_NVUTIL_H

//###########################################################################
//############################### INCLUDES ##################################
//###########################################################################

#include "nvcommon.h"
#include "nvos.h"

#if defined(__cplusplus)
extern "C"
{
#endif

//###########################################################################
//############################### PROTOTYPES ################################
//###########################################################################

/**
 * parse a string into an unsigned integer.
 *
 * @param s - pointer to the string (null-terminated)
 * @param endptr - if not NULL this returns pointing to the first character
 *                 in the string that was not used in the conversion.
 * @param base - must be 0, 10, or 16.
 *                 10: the number is parsed as a base 10 number
 *                 16: the number is parsed as a base 16 number (0x ignored)
 *                 0: base 10 unless there is a leading 0x
 */
unsigned long int
NvUStrtoul(
        const char *s, 
        char **endptr, 
        int base);

/**
 * Sames NvUStrtoul, execpt can parse a 64 bit unsigned integer.
 */
unsigned long long int
NvUStrtoull(
        const char *s, 
        char **endptr, 
        int base);

/**
 * parse a string into a signed integer.
 *
 * @param s - pointer to the string (null-terminated)
 * @param endptr - if not NULL this returns pointing to the first character
 *                 in the string that was not used in the conversion.
 * @param base - must be 0, 10, or 16.
 *                 10: the number is parsed as a base 10 number
 *                 16: the number is parsed as a base 16 number (0x ignored)
 *                 0: base 10 unless there is a leading 0x
 */
long int
NvUStrtol(
        const char *s, 
        char **endptr, 
        int base);

/**
 * concatenate 2 strings.
 *
 * Note: dest is always left null terminated even if src exceeds n.
 *
 * @param dest - string to concatenate to
 * @param src  - string to add to the end of dest
 * @param n    - At most n chars from src (plus a NUL) are appended to dest
 */
void
NvUStrncat(
        char *dest, 
        const char *src, 
        size_t n);

/**
 * returns a pointer to the first occurence of str2 in str1.
 *
 * This function returns NULL if no match is found. If the length of str2 is
 * zero, then str1 is returned.
 *
 * @param str1 - string to be scanned
 * @param str2 - string containing the sequence of characters to match
 */
char *
NvUStrstr(
        const char *str1, 
        const char *str2);

/**
 * converts strings between code pages
 *
 * @param pDest - the destination buffer
 * @param DestCodePage - the target code page
 * @param DestSize - size of the destination buffer, in bytes
 * @param pSrc - the source string
 * @param SrcSize - the size of the source buffer, in bytes, or zero if the
*       string is NULL-terminated
 * @param SrcCodePage - the source string's code page
 *
 * @returns The length of the destination string and NULL termination, in bytes
 *
 * If pDest is NULL, this function will return the number of bytes, including
 * NULL termination, of the destination buffer required to store the converted
 * string.
 *
 * If pDest is specified, up to DestSize bytes of the code-page converted
 * string will be written to it.  If the destination buffer is too small to
 * store the converted string, the string will be truncated and a
 * NULL-terminator added.
 *
 * If either SrcCodePage or DestCodePage is NvOsCodePage_Unknown, the system's
 * default code page will be used for the conversion.
 */
size_t
NvUStrlConvertCodePage(void *pDest,
                       size_t DestSize,
                       NvOsCodePage DestCodePage,
                       const void *pSrc,
                       size_t SrcSize,
                       NvOsCodePage SrcCodePage);

/**
 * dynamically allocate zeroed memory (uses NvOsAlloc())
 *
 * @param size number of bytes to allocate
 * @returns NULL on failure
 * @returns pointer to zeroed memory on success (must be freed with NvOsFree)
 */
static NV_INLINE void *
NvUAlloc0(size_t size)
{
    void *p = NvOsAlloc(size);
    if (p)
        NvOsMemset(p,0,size);
    return p;
}

/**
 * Finds the lowest set bit.
 *
 * @param bits The bits to look through
 * @param nBits The number of bits wide
 */
NvU32
NvULowestBitSet( NvU32 bits, NvU32 nBits );

#if defined(__cplusplus)
}
#endif

#endif // INCLUDED_NVUTIL_H
