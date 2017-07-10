/*************************************************************************/ /*!
@Title          Integer log2 and related functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef LOG2_H
#define LOG2_H

#include "img_defs.h"

/**************************************************************************/ /*!
@Description    Determine if a number is a power of two.
@Input          n
@Return         True if n is a power of 2, false otherwise. True if n == 0.
*/ /***************************************************************************/
static INLINE IMG_BOOL IsPower2(uint32_t n)
{
	/* C++ needs this cast. */
	return (IMG_BOOL)((n & (n - 1)) == 0);
}

/**************************************************************************/ /*!
@Description    Determine if a number is a power of two.
@Input          n
@Return         True if n is a power of 2, false otherwise. True if n == 0.
*/ /***************************************************************************/
static INLINE IMG_BOOL IsPower2_64(uint64_t n)
{
	/* C++ needs this cast. */
	return (IMG_BOOL)((n & (n - 1)) == 0);
}

/**************************************************************************/ /*!
@Description    Round a non-power-of-two number up to the next power of two.
@Input          n
@Return         n rounded up to the next power of two. If n is zero or
                already a power of two, return n unmodified.
*/ /***************************************************************************/
static INLINE uint32_t RoundUpToNextPowerOfTwo(uint32_t n)
{
	n--;
	n |= n >> 1;  /* handle  2 bit numbers */
	n |= n >> 2;  /* handle  4 bit numbers */
	n |= n >> 4;  /* handle  8 bit numbers */
	n |= n >> 8;  /* handle 16 bit numbers */
	n |= n >> 16; /* handle 32 bit numbers */
	n++;

	return n;
}

/**************************************************************************/ /*!
@Description    Round a non-power-of-two number up to the next power of two.
@Input          n
@Return         n rounded up to the next power of two. If n is zero or
                already a power of two, return n unmodified.
*/ /***************************************************************************/
static INLINE uint64_t RoundUpToNextPowerOfTwo_64(uint64_t n)
{
	n--;
	n |= n >> 1;  /* handle  2 bit numbers */
	n |= n >> 2;  /* handle  4 bit numbers */
	n |= n >> 4;  /* handle  8 bit numbers */
	n |= n >> 8;  /* handle 16 bit numbers */
	n |= n >> 16; /* handle 32 bit numbers */
	n |= n >> 32; /* handle 64 bit numbers */
	n++;

	return n;
}

/**************************************************************************/ /*!
@Description    Compute floor(log2(n))
@Input          n
@Return         log2(n) rounded down to the nearest integer. Returns 0 if n == 0
*/ /***************************************************************************/
static INLINE uint32_t FloorLog2(uint32_t n)
{
	uint32_t log2 = 0;

	while (n >>= 1)
		log2++;

	return log2;
}

/**************************************************************************/ /*!
@Description    Compute floor(log2(n))
@Input          n
@Return         log2(n) rounded down to the nearest integer. Returns 0 if n == 0
*/ /***************************************************************************/
static INLINE uint32_t FloorLog2_64(uint64_t n)
{
	uint32_t log2 = 0;

	while (n >>= 1)
		log2++;

	return log2;
}

/**************************************************************************/ /*!
@Description    Compute ceil(log2(n))
@Input          n
@Return         log2(n) rounded up to the nearest integer. Returns 0 if n == 0
*/ /***************************************************************************/
static INLINE uint32_t CeilLog2(uint32_t n)
{
	uint32_t log2 = 0;

	if(n == 0)
		return 0;

	n--; /* Handle powers of 2 */

	while(n)
	{
		log2++;
		n >>= 1;
	}

	return log2;
}

/**************************************************************************/ /*!
@Description    Compute ceil(log2(n))
@Input          n
@Return         log2(n) rounded up to the nearest integer. Returns 0 if n == 0
*/ /***************************************************************************/
static INLINE uint32_t CeilLog2_64(uint64_t n)
{
	uint32_t log2 = 0;

	if(n == 0)
		return 0;

	n--; /* Handle powers of 2 */

	while(n)
	{
		log2++;
		n >>= 1;
	}

	return log2;
}

/**************************************************************************/ /*!
@Description    Compute log2(n) for exact powers of two only
@Input          n                   Must be a power of two
@Return         log2(n)
*/ /***************************************************************************/
static INLINE uint32_t ExactLog2(uint32_t n)
{
	static const uint32_t b[] =
		{ 0xAAAAAAAA, 0xCCCCCCCC, 0xF0F0F0F0, 0xFF00FF00, 0xFFFF0000};
	uint32_t r = (n & b[0]) != 0;

	r |= (uint32_t) ((n & b[4]) != 0) << 4;
	r |= (uint32_t) ((n & b[3]) != 0) << 3;
	r |= (uint32_t) ((n & b[2]) != 0) << 2;
	r |= (uint32_t) ((n & b[1]) != 0) << 1;

	return r;
}

/**************************************************************************/ /*!
@Description    Compute log2(n) for exact powers of two only
@Input          n                   Must be a power of two
@Return         log2(n)
*/ /***************************************************************************/
static INLINE uint32_t ExactLog2_64(uint64_t n)
{
	static const uint64_t b[] =
		{ 0xAAAAAAAAAAAAAAAAULL, 0xCCCCCCCCCCCCCCCCULL,
		  0xF0F0F0F0F0F0F0F0ULL, 0xFF00FF00FF00FF00ULL,
		  0xFFFF0000FFFF0000ULL, 0xFFFFFFFF00000000ULL };
	uint32_t r = (n & b[0]) != 0;

	r |= (uint32_t) ((n & b[5]) != 0) << 5;
	r |= (uint32_t) ((n & b[4]) != 0) << 4;
	r |= (uint32_t) ((n & b[3]) != 0) << 3;
	r |= (uint32_t) ((n & b[2]) != 0) << 2;
	r |= (uint32_t) ((n & b[1]) != 0) << 1;

	return r;
}

#endif /* LOG2_H */
