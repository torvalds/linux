/*************************************************************************/ /*!
@File
@Title          Functions for BVNC manipulating

@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Utility functions used internally by device memory management
                code.
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

/******************************************************************************
 * RGX Version packed into 24-bit (BNC) and string (V) to be used by Compatibility Check
 *****************************************************************************/

#include "rgx_compat_bvnc.h"

IMG_VOID rgx_bvnc_packed(IMG_UINT32 *pui32OutBNC, IMG_CHAR *pszOutV, IMG_UINT32 ui32OutVMaxLen, 
								IMG_UINT32 ui32B, IMG_CHAR *pszV, IMG_UINT32 ui32N, IMG_UINT32 ui32C)
{
#if 0
	IMG_UINT32 i = ui32OutVMaxLen;
#endif
	IMG_UINT32 ui32InVLen = 0;
	IMG_UINT32 ui32V = 0;

	*pui32OutBNC = (((ui32B & 0xFF) << 16) | ((ui32N & 0xFF) << 8) |
												(ui32C & 0xFF));

	/* Using dword accesses instead of byte accesses when forming V part of BVNC */
	ui32OutVMaxLen = ui32OutVMaxLen;
	while (pszV[ui32InVLen])
	{
		ui32V |= ((((IMG_UINT32)pszV[ui32InVLen]) & 0xFF) << (ui32InVLen*8));
		ui32InVLen++;
	}

	*((IMG_UINT32 *)pszOutV) = ui32V;

#if 0
	for (i = 0; i < (ui32OutVMaxLen + 1); i++)
		pszOutV[i] = '\0';

	while ((ui32OutVMaxLen > 0) && *pszV)
	{
		*pszOutV++ = *pszV++;
		ui32OutVMaxLen--;
	}
#endif
}

IMG_VOID rgx_bvnc_pack_hw(IMG_UINT32 *pui32OutBNC, IMG_CHAR *pszOutV, IMG_UINT32 ui32OutVMaxLen, 
								IMG_UINT32 ui32B, IMG_CHAR *pszFwV, IMG_UINT32 ui32V, IMG_UINT32 ui32N, IMG_UINT32 ui32C)
{
	IMG_UINT32 i = ui32OutVMaxLen;
	IMG_CHAR *pszPointer;

	*pui32OutBNC = (((ui32B & 0xFF) << 16) | ((ui32N & 0xFF) << 8) |
												(ui32C & 0xFF));

	for (i = 0; i < (ui32OutVMaxLen + 1); i++)
		pszOutV[i] = '\0';

	/* find out whether pszFwV is integer number or not */
	pszPointer = pszFwV;
	while (*pszPointer)
	{
		if ((*pszPointer < '0') || (*pszPointer > '9'))
		{
			break;
		}
		pszPointer++;
	}

	if (*pszPointer)
	{
		/* pszFwV is not a number, so taking V from it */
		pszPointer = pszFwV;
		while ((ui32OutVMaxLen > 0) && *pszPointer)
		{
			*pszOutV++ = *pszPointer++;
			ui32OutVMaxLen--;
		}
	}
	else
	{
		/* pszFwV is a number, taking V from ui32V */
		IMG_CHAR aszBuf[4];

		pszPointer = aszBuf;

		if (ui32V > 99)
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
		
		pszPointer = aszBuf;
		while ((ui32OutVMaxLen > 0) && *pszPointer)
		{
			*pszOutV++ = *pszPointer++;
			ui32OutVMaxLen--;
		}
	}
}

