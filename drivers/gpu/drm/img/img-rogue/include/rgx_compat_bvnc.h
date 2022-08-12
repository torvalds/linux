/*************************************************************************/ /*!
@File           rgx_compat_bvnc.h
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

#if !defined(RGX_COMPAT_BVNC_H)
#define RGX_COMPAT_BVNC_H

#include "img_types.h"

#if defined(RGX_FIRMWARE)               /* Services firmware */
# include "rgxfw_utils.h"
# define PVR_COMPAT_ASSERT RGXFW_ASSERT
#elif !defined(RGX_BUILD_BINARY)        /* Services host driver code */
# include "pvr_debug.h"
# define PVR_COMPAT_ASSERT PVR_ASSERT
#else                                   /* FW user-mode tools */
# include <assert.h>
# define PVR_COMPAT_ASSERT assert
#endif

/* 64bit endian conversion macros */
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
	 | (((N) >> 8)  & 0xff000000U) \
	 | ((N)                << 56) \
	 | (((N) & 0xff00)     << 40) \
	 | (((N) & 0xff0000)   << 24) \
	 | (((N) & 0xff000000U) << 8))
#define RGX_INT64_FROM_BE(N) RGX_INT64_TO_BE(N)

#define RGX_INT32_TO_BE(N)   \
	((((N) >> 24)  & 0xff)   \
	 | (((N) >> 8) & 0xff00) \
	 | ((N)           << 24) \
	 | ((((N) & 0xff00) << 8)))
#define RGX_INT32_FROM_BE(N) RGX_INT32_TO_BE(N)
#endif

/******************************************************************************
 * RGX Version packed into 64-bit (BVNC) to be used by Compatibility Check
 *****************************************************************************/

#define RGX_BVNC_PACK_SHIFT_B 48
#define RGX_BVNC_PACK_SHIFT_V 32
#define RGX_BVNC_PACK_SHIFT_N 16
#define RGX_BVNC_PACK_SHIFT_C 0

#define RGX_BVNC_PACK_MASK_B (IMG_UINT64_C(0xFFFF000000000000))
#define RGX_BVNC_PACK_MASK_V (IMG_UINT64_C(0x0000FFFF00000000))
#define RGX_BVNC_PACK_MASK_N (IMG_UINT64_C(0x00000000FFFF0000))
#define RGX_BVNC_PACK_MASK_C (IMG_UINT64_C(0x000000000000FFFF))

#define RGX_BVNC_PACKED_EXTR_B(BVNC) ((IMG_UINT32)(((BVNC) & RGX_BVNC_PACK_MASK_B) >> RGX_BVNC_PACK_SHIFT_B))
#define RGX_BVNC_PACKED_EXTR_V(BVNC) ((IMG_UINT32)(((BVNC) & RGX_BVNC_PACK_MASK_V) >> RGX_BVNC_PACK_SHIFT_V))
#define RGX_BVNC_PACKED_EXTR_N(BVNC) ((IMG_UINT32)(((BVNC) & RGX_BVNC_PACK_MASK_N) >> RGX_BVNC_PACK_SHIFT_N))
#define RGX_BVNC_PACKED_EXTR_C(BVNC) ((IMG_UINT32)(((BVNC) & RGX_BVNC_PACK_MASK_C) >> RGX_BVNC_PACK_SHIFT_C))

#define RGX_BVNC_EQUAL(L,R,all,version,bvnc) do {															\
										(bvnc) = IMG_FALSE;													\
										(version) = ((L).ui32LayoutVersion == (R).ui32LayoutVersion);		\
										if (version)														\
										{																	\
											(bvnc) = ((L).ui64BVNC == (R).ui64BVNC);						\
										}																	\
										(all) = (version) && (bvnc);										\
									} while (false)


/**************************************************************************//**
 * Utility function for packing BVNC
 *****************************************************************************/
static inline IMG_UINT64 rgx_bvnc_pack(IMG_UINT32 ui32B, IMG_UINT32 ui32V, IMG_UINT32 ui32N, IMG_UINT32 ui32C)
{
	/*
	 * Test for input B, V, N and C exceeding max bit width.
	 */
	PVR_COMPAT_ASSERT((ui32B & (~(RGX_BVNC_PACK_MASK_B >> RGX_BVNC_PACK_SHIFT_B))) == 0U);
	PVR_COMPAT_ASSERT((ui32V & (~(RGX_BVNC_PACK_MASK_V >> RGX_BVNC_PACK_SHIFT_V))) == 0U);
	PVR_COMPAT_ASSERT((ui32N & (~(RGX_BVNC_PACK_MASK_N >> RGX_BVNC_PACK_SHIFT_N))) == 0U);
	PVR_COMPAT_ASSERT((ui32C & (~(RGX_BVNC_PACK_MASK_C >> RGX_BVNC_PACK_SHIFT_C))) == 0U);

	return (((IMG_UINT64)ui32B << RGX_BVNC_PACK_SHIFT_B) |
			((IMG_UINT64)ui32V << RGX_BVNC_PACK_SHIFT_V) |
			((IMG_UINT64)ui32N << RGX_BVNC_PACK_SHIFT_N) |
			((IMG_UINT64)ui32C << RGX_BVNC_PACK_SHIFT_C));
}


#endif /* RGX_COMPAT_BVNC_H */

/******************************************************************************
 End of file (rgx_compat_bvnc.h)
******************************************************************************/
