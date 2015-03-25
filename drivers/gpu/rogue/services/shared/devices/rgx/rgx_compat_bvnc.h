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

#if !defined (__RGX_COMPAT_BVNC_H__)
#define __RGX_COMPAT_BVNC_H__

#include "img_types.h"

/******************************************************************************
 * RGX Version packed into 24-bit (BNC) and string (V) to be used by Compatibility Check
 *****************************************************************************/

#define RGX_BVNC_PACK_MASK_B 0x00FF0000
#define RGX_BVNC_PACK_MASK_N 0x0000FF00
#define RGX_BVNC_PACK_MASK_C 0x000000FF

#define RGX_BVNC_PACKED_EXTR_B(BVNC) (((BVNC).ui32BNC >> 16) & 0xFF)
#define RGX_BVNC_PACKED_EXTR_V(BVNC) ((BVNC).aszV)
#define RGX_BVNC_PACKED_EXTR_N(BVNC) (((BVNC).ui32BNC >> 8) & 0xFF)
#define RGX_BVNC_PACKED_EXTR_C(BVNC) (((BVNC).ui32BNC >> 0) & 0xFF)

#define RGX_BVNC_EQUAL(L,R,all,version,lenmax,bnc,v) do {													\
										(lenmax) = IMG_FALSE;												\
										(bnc) = IMG_FALSE;													\
										(v) = IMG_FALSE;													\
										(version) = ((L).ui32LayoutVersion == (R).ui32LayoutVersion);		\
										if (version)														\
										{																	\
											(lenmax) = ((L).ui32VLenMax == (R).ui32VLenMax);				\
										}																	\
										if (lenmax)															\
										{																	\
											(bnc) = ((L).ui32BNC == (R).ui32BNC);							\
										}																	\
										if (bnc)															\
										{																	\
											(L).aszV[(L).ui32VLenMax] = '\0';								\
											(R).aszV[(R).ui32VLenMax] = '\0';								\
											(v) = (OSStringCompare((L).aszV, (R).aszV)==0);					\
										}																	\
										(all) = (version) && (lenmax) && (bnc) && (v);						\
									} while (0)

IMG_VOID rgx_bvnc_packed(IMG_UINT32 *pui32OutBNC, IMG_CHAR *pszOutV, IMG_UINT32 ui32OutVMaxLen,
							IMG_UINT32 ui32B, IMG_CHAR *pszV, IMG_UINT32 ui32N, IMG_UINT32 ui32C);
IMG_VOID rgx_bvnc_pack_hw(IMG_UINT32 *pui32OutBNC, IMG_CHAR *pszOutV, IMG_UINT32 ui32OutVMaxLen,
							IMG_UINT32 ui32B, IMG_CHAR *pszFwV, IMG_UINT32 ui32V, IMG_UINT32 ui32N, IMG_UINT32 ui32C);

#endif /*  __RGX_COMPAT_BVNC_H__ */

/******************************************************************************
 End of file (rgx_compat_bvnc.h)
******************************************************************************/

