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

/* 64bit endian converting macros */
#if defined(__BIG_ENDIAN__)
#define RGX_INT64_TO_BE(N) (N)
#define RGX_INT64_FROM_BE(N) (N)
#define RGX_INT32_TO_BE(N) (N)
#define RGX_INT32_FROM_BE(N) (N)
#else
#define RGX_INT64_TO_BE(N)        \
	((((N) >> 56)   & 0xff)       \
	 | (((N) >> 40) & 0xff00)     \
	 | (((N) >> 24) & 0xff0000)   \
	 | (((N) >> 8)  & 0xff000000) \
	 | ((N)                << 56) \
	 | (((N) & 0xff00)     << 40) \
	 | (((N) & 0xff0000)   << 24) \
	 | (((N) & 0xff000000) << 8))
#define RGX_INT64_FROM_BE(N) RGX_INT64_TO_BE(N)

#define RGX_INT32_TO_BE(N)   \
	((((N) >> 24)  & 0xff)   \
	 | (((N) >> 8) & 0xff00) \
	 | ((N)           << 24) \
	 | (((N & 0xff00) << 8)))
#define RGX_INT32_FROM_BE(N) RGX_INT32_TO_BE(N)
#endif

/******************************************************************************
 * RGX Version packed into 24-bit (BNC) and string (V) to be used by Compatibility Check
 *****************************************************************************/

#define RGX_BVNC_PACK_SHIFT_B 32
#define RGX_BVNC_PACK_SHIFT_N 16
#define RGX_BVNC_PACK_SHIFT_C 0

#define RGX_BVNC_PACK_MASK_B (IMG_UINT64_C(0x0000FFFF00000000))
#define RGX_BVNC_PACK_MASK_N (IMG_UINT64_C(0x00000000FFFF0000))
#define RGX_BVNC_PACK_MASK_C (IMG_UINT64_C(0x000000000000FFFF))

#define RGX_BVNC_PACKED_EXTR_B(BVNC) ((IMG_UINT32)(((BVNC).ui64BNC & RGX_BVNC_PACK_MASK_B) >> RGX_BVNC_PACK_SHIFT_B))
#define RGX_BVNC_PACKED_EXTR_V(BVNC) ((BVNC).aszV)
#define RGX_BVNC_PACKED_EXTR_N(BVNC) ((IMG_UINT32)(((BVNC).ui64BNC & RGX_BVNC_PACK_MASK_N) >> RGX_BVNC_PACK_SHIFT_N))
#define RGX_BVNC_PACKED_EXTR_C(BVNC) ((IMG_UINT32)(((BVNC).ui64BNC & RGX_BVNC_PACK_MASK_C) >> RGX_BVNC_PACK_SHIFT_C))

#if !defined(RGX_SKIP_BVNC_CHECK)
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
											(bnc) = ((L).ui64BNC == (R).ui64BNC);							\
										}																	\
										if (bnc)															\
										{																	\
											(L).aszV[(L).ui32VLenMax] = '\0';								\
											(R).aszV[(R).ui32VLenMax] = '\0';								\
											(v) = (OSStringCompare((L).aszV, (R).aszV)==0);					\
										}																	\
										(all) = (version) && (lenmax) && (bnc) && (v);						\
									} while (0)
#else
#define RGX_BVNC_EQUAL(L,R,all,version,lenmax,bnc,v)														\
						(all) 		= IMG_TRUE;																\
						(version) 	= IMG_TRUE;																\
						(lenmax) 	= IMG_TRUE;																\
						(bnc) 		= IMG_TRUE;																\
						(v) 		= IMG_TRUE;																\

#endif

void rgx_bvnc_packed(IMG_UINT64 *pui64OutBNC, IMG_CHAR *pszOutV, IMG_UINT32 ui32OutVMaxLen,
					 IMG_UINT32 ui32B, IMG_CHAR *pszV, IMG_UINT32 ui32N, IMG_UINT32 ui32C);
void rgx_bvnc_pack_hw(IMG_UINT64 *pui64OutBNC, IMG_CHAR *pszOutV, IMG_UINT32 ui32OutVMaxLen,
					  IMG_UINT32 ui32B, IMG_UINT32 ui32V, IMG_UINT32 ui32N, IMG_UINT32 ui32C);

#endif /*  __RGX_COMPAT_BVNC_H__ */

/******************************************************************************
 End of file (rgx_compat_bvnc.h)
******************************************************************************/

