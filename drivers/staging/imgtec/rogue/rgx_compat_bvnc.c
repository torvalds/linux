/*************************************************************************/ /*!
@File           rgx_compact_bvnc.c
@Title          BVNC compatibility check utilities
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Utility functions used for packing BNC and V.
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

#include "rgx_compat_bvnc.h"
#if defined(RGX_FIRMWARE)
#include "rgxfw_utils.h"
#elif !defined(RGX_BUILD_BINARY)
#include "pvr_debug.h"
#endif

#if defined(RGX_FIRMWARE)
#define PVR_COMPAT_ASSERT RGXFW_ASSERT
#elif !defined(RGX_BUILD_BINARY)
#define PVR_COMPAT_ASSERT PVR_ASSERT
#else
#include <assert.h>
#define PVR_COMPAT_ASSERT assert
#endif

/**************************************************************************//**
 * C library strlen function.
 *****************************************************************************/
static INLINE IMG_UINT32 OSStringLength(const IMG_CHAR* pszInput)
{
	const IMG_CHAR* pszTemp = pszInput;

	while (*pszTemp)
		pszTemp++;

	return (pszTemp - pszInput);
}

/**************************************************************************//**
 * Utility function for packing BNC
 *****************************************************************************/
static INLINE IMG_UINT64 rgx_bnc_pack(IMG_UINT32 ui32B, IMG_UINT32 ui32N,
														IMG_UINT32 ui32C)
{
	/*
	 * Test for input B, N and C exceeding max bit width.
	 */
	PVR_COMPAT_ASSERT((ui32B & (~(RGX_BVNC_PACK_MASK_B >> RGX_BVNC_PACK_SHIFT_B))) == 0);
	PVR_COMPAT_ASSERT((ui32N & (~(RGX_BVNC_PACK_MASK_N >> RGX_BVNC_PACK_SHIFT_N))) == 0);
	PVR_COMPAT_ASSERT((ui32C & (~(RGX_BVNC_PACK_MASK_C >> RGX_BVNC_PACK_SHIFT_C))) == 0);

	return (((IMG_UINT64)ui32B << RGX_BVNC_PACK_SHIFT_B) |
			((IMG_UINT64)ui32N << RGX_BVNC_PACK_SHIFT_N) |
			((IMG_UINT64)ui32C << RGX_BVNC_PACK_SHIFT_C));
}

/**************************************************************************//**
 * Utility function for packing BNC and V to be used by compatibility check.
 * BNC is packed into 48 bit format.
 * If the array pointed to by pszV is a string that is shorter than 
 * ui32OutVMaxLen characters, null characters are appended to the copy in the
 * array pointed to by pszOutV, until 'ui32OutVMaxLen' characters in all have
 * been written.
 *
 * @param:      pui64OutBNC       Output containing packed BNC.
 * @param       pszOutV           Output containing version string.
 * @param       ui32OutVMaxLen    Max characters that can be written to 
                                  pszOutV (excluding terminating null character)
 * @param       ui32B             Input 'B' value
 * @param       pszV              Input 'V' string
 * @param       ui32N             Input 'N' value
 * @param       ui32C             Input 'C' value
 * @return      None
 *****************************************************************************/
void rgx_bvnc_packed(IMG_UINT64 *pui64OutBNC, IMG_CHAR *pszOutV, IMG_UINT32 ui32OutVMaxLen,
					 IMG_UINT32 ui32B, IMG_CHAR *pszV, IMG_UINT32 ui32N, IMG_UINT32 ui32C)
{
	*pui64OutBNC = rgx_bnc_pack(ui32B, ui32N, ui32C);

	if (!pszOutV)
		return;

	if (pszV)
	{
		/*
		 * Assert can fail for two reasons
		 * 1. Caller is passing invalid 'V' string or
		 * 2. Dest buffer does not have enough memory allocated for max 'V' size.
		 */
		PVR_COMPAT_ASSERT(OSStringLength(pszV) <= ui32OutVMaxLen);

		
		for (; ui32OutVMaxLen > 0 && *pszV != '\0'; --ui32OutVMaxLen)
		{
			/* When copying the V, omit any characters as these would cause
			 * the compatibility check against the V read from HW to fail
			 */
			if (*pszV && (*pszV >= '0') && (*pszV <='9'))
			{
				*pszOutV++ = *pszV++;
			}
			else
			{
				pszV++;
			}
		}
	}

	do
	{
		*pszOutV++ = '\0';
	}while(ui32OutVMaxLen-- > 0);
}

/**************************************************************************//**
 * Utility function for packing BNC and V to be used by compatibility check.
 * Input B,N and C is packed into 48 bit format.
 * Input V is converted into string. If number of characters required to
 * represent 16 bit wide version number is less than ui32OutVMaxLen, than null
 * characters are appended to pszOutV, until ui32OutVMaxLen characters in all 
 * have been written.
 *
 * @param:      pui64OutBNC       Output containing packed BNC.
 * @param       pszOutV           Output containing version string.
 * @param       ui32OutVMaxLen    Max characters that can be written to 
                                  pszOutV (excluding terminating null character)
 * @param       ui32B             Input 'B' value (16 bit wide)
 * @param       ui32V             Input 'V' value (16 bit wide)
 * @param       ui32N             Input 'N' value (16 bit wide)
 * @param       ui32C             Input 'C' value (16 bit wide)
 * @return     .None
 *****************************************************************************/
void rgx_bvnc_pack_hw(IMG_UINT64 *pui64OutBNC, IMG_CHAR *pszOutV, IMG_UINT32 ui32OutVMaxLen,
					  IMG_UINT32 ui32B, IMG_UINT32 ui32V, IMG_UINT32 ui32N, IMG_UINT32 ui32C)
{
	/*
	 * Allocate space for max digits required to represent 16 bit wide version
	 * number (including NULL terminating character).
	 */
	IMG_CHAR aszBuf[6];
	IMG_CHAR *pszPointer = aszBuf;

	*pui64OutBNC = rgx_bnc_pack(ui32B, ui32N, ui32C);

	if (!pszOutV)
		return;

	/*
	 * Function only supports 16 bits wide version number.
	 */
	PVR_COMPAT_ASSERT((ui32V & ~0xFFFF) == 0);

	if (ui32V > 9999)
		pszPointer+=5;
	else if (ui32V > 999)
		pszPointer+=4;
	else if (ui32V > 99)
		pszPointer+=3;
	else if (ui32V > 9)
		pszPointer+=2;
	else
		pszPointer+=1;
	
	*pszPointer-- = '\0';
	*pszPointer = '0';
	
	while (ui32V > 0)
	{
		*pszPointer-- = (ui32V % 10) + '0';
		ui32V /= 10;
	}

	for (pszPointer = aszBuf; ui32OutVMaxLen > 0 && *pszPointer != '\0'; --ui32OutVMaxLen)
		*pszOutV++ = *pszPointer++;

	/*
	 * Append NULL characters.
	 */
	do
	{
		*pszOutV++ = '\0';
	}while(ui32OutVMaxLen-- > 0);
}

