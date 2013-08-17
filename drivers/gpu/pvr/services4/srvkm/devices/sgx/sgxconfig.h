/*************************************************************************/ /*!
@Title          device configuration
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

#ifndef __SGXCONFIG_H__
#define __SGXCONFIG_H__

#include "sgxdefs.h"

#define DEV_DEVICE_TYPE			PVRSRV_DEVICE_TYPE_SGX
#define DEV_DEVICE_CLASS		PVRSRV_DEVICE_CLASS_3D

#define DEV_MAJOR_VERSION		1
#define DEV_MINOR_VERSION		0

#if defined(SUPPORT_EXTERNAL_SYSTEM_CACHE)
#define SGX_KERNEL_DATA_HEAP_OFFSET		0x00001000
#else
#define SGX_KERNEL_DATA_HEAP_OFFSET		0x00000000
#endif


#if SGX_FEATURE_ADDRESS_SPACE_SIZE == 32
#if defined(FIX_HW_BRN_31620)
	#if defined(SGX_FEATURE_2D_HARDWARE)
	#define SGX_2D_HEAP_BASE					 0x04000000
	#define SGX_2D_HEAP_SIZE					(0x08000000-0x04000000-0x00001000)
	#endif

	#define SGX_GENERAL_HEAP_BASE				 0x08000000
	#define SGX_GENERAL_HEAP_SIZE				(0xB8000000-0x00001000)

	/*
	 * For hybrid PB we have to split virtual PB range between the shared
	 * PB and percontext PB due to the fact we only have one heap config
	 * per device.
	 * If hybrid PB is enabled we split the space according to HYBRID_SHARED_PB_SIZE.
	 * i.e. HYBRID_SHARED_PB_SIZE defines the size of the shared PB and the
	 * remainder is the size of the percontext PB.
	 * If hybrid PB is not enabled then we still create both heaps (helps keep
	 * the code clean) and define the size of the unused one to 0
	 */

	#define SGX_3DPARAMETERS_HEAP_SIZE			0x10000000

	/* By default we split the PB 50/50 */
#if !defined(HYBRID_SHARED_PB_SIZE)
	#define HYBRID_SHARED_PB_SIZE				(SGX_3DPARAMETERS_HEAP_SIZE >> 1)
#endif
#if defined(SUPPORT_HYBRID_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			(HYBRID_SHARED_PB_SIZE)
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(HYBRID_SHARED_PB_SIZE-0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - SGX_SHARED_3DPARAMETERS_SIZE - 0x00001000)
#else
#if defined(SUPPORT_PERCONTEXT_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			0
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		0
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
#endif
#if defined(SUPPORT_SHARED_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			SGX_3DPARAMETERS_HEAP_SIZE
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		0
#endif
#endif

	#define SGX_SHARED_3DPARAMETERS_HEAP_BASE		 0xC0000000
	/* Size is defined above */

	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE		 (SGX_SHARED_3DPARAMETERS_HEAP_BASE + SGX_SHARED_3DPARAMETERS_SIZE)
	/* Size is defined above */

	#define SGX_TADATA_HEAP_BASE				 0xD0000000
	#define SGX_TADATA_HEAP_SIZE				(0x0D000000-0x00001000)

	#define SGX_SYNCINFO_HEAP_BASE				 0xE0000000
	#define SGX_SYNCINFO_HEAP_SIZE				(0x01000000-0x00001000)

	#define SGX_PDSPIXEL_CODEDATA_HEAP_BASE		 0xE4000000
	#define SGX_PDSPIXEL_CODEDATA_HEAP_SIZE		(0x02000000-0x00001000)

	#define SGX_KERNEL_CODE_HEAP_BASE			 0xE8000000
	#define SGX_KERNEL_CODE_HEAP_SIZE			(0x00080000-0x00001000)

	#define SGX_PDSVERTEX_CODEDATA_HEAP_BASE	 0xEC000000
	#define SGX_PDSVERTEX_CODEDATA_HEAP_SIZE	(0x01C00000-0x00001000)

	#define SGX_KERNEL_DATA_HEAP_BASE		 	(0xF0000000+SGX_KERNEL_DATA_HEAP_OFFSET)
	#define SGX_KERNEL_DATA_HEAP_SIZE			(0x03000000-(0x00001000+SGX_KERNEL_DATA_HEAP_OFFSET))

	/* Actual Pixel and Vertex shared heaps sizes may be reduced by
	 * override - see SGX_USE_CODE_SEGMENT_RANGE_BITS.*/
	#define SGX_PIXELSHADER_HEAP_BASE			 0xF4000000
	#define SGX_PIXELSHADER_HEAP_SIZE			(0x05000000-0x00001000)
	
	#define SGX_VERTEXSHADER_HEAP_BASE			 0xFC000000
	#define SGX_VERTEXSHADER_HEAP_SIZE			(0x02000000-0x00001000)
#else /* FIX_HW_BRN_31620 */
	#if defined(SGX_FEATURE_2D_HARDWARE)
	#define SGX_2D_HEAP_BASE					 0x00100000
	#define SGX_2D_HEAP_SIZE					(0x08000000-0x00100000-0x00001000)
	#endif

	#if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
	#define SGX_GENERAL_MAPPING_HEAP_BASE		 0x08000000
	#define SGX_GENERAL_MAPPING_HEAP_SIZE		(0x08000000-0x00001000)
	#endif

	#if !defined(SUPPORT_MEMORY_TILING)
		#define SGX_GENERAL_HEAP_BASE				 0x10000000
		#define SGX_GENERAL_HEAP_SIZE				(0xC2000000-0x00001000)
	#else
		#include <sgx_msvdx_defs.h>
		/* Create heaps with memory tiling enabled.
	 	 * SGX HW limit is 10 heaps.
	 	 */
	 	/* Tiled heap space is taken from general heap */
	 	#define SGX_GENERAL_HEAP_BASE				 0x10000000
		#define SGX_GENERAL_HEAP_SIZE				(0xB5000000-0x00001000)

		#define SGX_VPB_TILED_HEAP_STRIDE			TILING_TILE_STRIDE_2K
		#define SGX_VPB_TILED_HEAP_BASE		 0xC5000000
		#define SGX_VPB_TILED_HEAP_SIZE	(0x0D000000-0x00001000)

		/* Check tiled heap base alignment */
		#if((SGX_VPB_TILED_HEAP_BASE & SGX_BIF_TILING_ADDR_INV_MASK) != 0)
		#error "sgxconfig.h: SGX_VPB_TILED_HEAP has insufficient alignment"
		#endif

	#endif /* SUPPORT_MEMORY_TILING */

	/*
	 * For hybrid PB we have to split virtual PB range between the shared
	 * PB and percontext PB due to the fact we only have one heap config
	 * per device.
	 * If hybrid PB is enabled we split the space according to HYBRID_SHARED_PB_SIZE.
	 * i.e. HYBRID_SHARED_PB_SIZE defines the size of the shared PB and the
	 * remainder is the size of the percontext PB.
	 * If hybrid PB is not enabled then we still create both heaps (helps keep
	 * the code clean) and define the size of the unused one to 0
	 */

	#define SGX_3DPARAMETERS_HEAP_SIZE			0x10000000

	/* By default we split the PB 50/50 */
#if !defined(HYBRID_SHARED_PB_SIZE)
	#define HYBRID_SHARED_PB_SIZE				(SGX_3DPARAMETERS_HEAP_SIZE >> 1)
#endif
#if defined(SUPPORT_HYBRID_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			(HYBRID_SHARED_PB_SIZE)
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(HYBRID_SHARED_PB_SIZE-0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - SGX_SHARED_3DPARAMETERS_SIZE - 0x00001000)
#else
#if defined(SUPPORT_PERCONTEXT_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			0
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		0
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
#endif
#if defined(SUPPORT_SHARED_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			SGX_3DPARAMETERS_HEAP_SIZE
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		0
#endif
#endif

	#define SGX_SHARED_3DPARAMETERS_HEAP_BASE		 0xD2000000
	/* Size is defined above */

	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE		 (SGX_SHARED_3DPARAMETERS_HEAP_BASE + SGX_SHARED_3DPARAMETERS_SIZE)
	/* Size is defined above */

	#define SGX_TADATA_HEAP_BASE				 0xE2000000
	#define SGX_TADATA_HEAP_SIZE				(0x0D000000-0x00001000)

	#define SGX_SYNCINFO_HEAP_BASE				 0xEF000000
	#define SGX_SYNCINFO_HEAP_SIZE				(0x01000000-0x00001000)

	#define SGX_PDSPIXEL_CODEDATA_HEAP_BASE		 0xF0000000
	#define SGX_PDSPIXEL_CODEDATA_HEAP_SIZE		(0x02000000-0x00001000)

	#define SGX_KERNEL_CODE_HEAP_BASE			 0xF2000000
	#define SGX_KERNEL_CODE_HEAP_SIZE			(0x00080000-0x00001000)

	#define SGX_PDSVERTEX_CODEDATA_HEAP_BASE	 0xF2400000
	#define SGX_PDSVERTEX_CODEDATA_HEAP_SIZE	(0x01C00000-0x00001000)

	#define SGX_KERNEL_DATA_HEAP_BASE		 	(0xF4000000+SGX_KERNEL_DATA_HEAP_OFFSET)
	#define SGX_KERNEL_DATA_HEAP_SIZE			(0x05000000-(0x00001000+SGX_KERNEL_DATA_HEAP_OFFSET))

	/* Actual Pixel and Vertex shared heaps sizes may be reduced by
	 * override - see SGX_USE_CODE_SEGMENT_RANGE_BITS.*/
	#define SGX_PIXELSHADER_HEAP_BASE			 0xF9000000
	#define SGX_PIXELSHADER_HEAP_SIZE			(0x05000000-0x00001000)
	
	#define SGX_VERTEXSHADER_HEAP_BASE			 0xFE000000
	#define SGX_VERTEXSHADER_HEAP_SIZE			(0x02000000-0x00001000)
#endif /* FIX_HW_BRN_31620 */
	/* signal we've identified the core by the build */
	#define SGX_CORE_IDENTIFIED
#endif /* SGX_FEATURE_ADDRESS_SPACE_SIZE == 32 */

#if SGX_FEATURE_ADDRESS_SPACE_SIZE == 28

#if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
	#define SGX_GENERAL_MAPPING_HEAP_BASE		 0x00001000
	#define SGX_GENERAL_MAPPING_HEAP_SIZE		(0x01800000-0x00001000-0x00001000)

	#define SGX_GENERAL_HEAP_BASE				 0x01800000
	#define SGX_GENERAL_HEAP_SIZE				(0x07000000-0x00001000)

#else
	#define SGX_GENERAL_HEAP_BASE				 0x00001000
#if defined(SUPPORT_LARGE_GENERAL_HEAP)
	#define SGX_GENERAL_HEAP_SIZE				(0x0B800000-0x00001000-0x00001000)
#else
	#define SGX_GENERAL_HEAP_SIZE				(0x08800000-0x00001000-0x00001000)
#endif
#endif

	/*
	 * For hybrid PB we have to split virtual PB range between the shared
	 * PB and percontext PB due to the fact we only have one heap config
	 * per device.
 	 * If hybrid PB is enabled we split the space according to HYBRID_SHARED_PB_SIZE.
	 * i.e. HYBRID_SHARED_PB_SIZE defines the size of the shared PB and the
	 * remainder is the size of the percontext PB.
	 * If hybrid PB is not enabled then we still create both heaps (helps keep
	 * the code clean) and define the size of the unused one to 0
	 */
#if defined(SUPPORT_LARGE_GENERAL_HEAP)
	#define SGX_3DPARAMETERS_HEAP_SIZE			0x01000000
#else
	#define SGX_3DPARAMETERS_HEAP_SIZE			0x04000000
#endif

	/* By default we split the PB 50/50 */
#if !defined(HYBRID_SHARED_PB_SIZE)
	#define HYBRID_SHARED_PB_SIZE				(SGX_3DPARAMETERS_HEAP_SIZE >> 1)
#endif
#if defined(SUPPORT_HYBRID_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			(HYBRID_SHARED_PB_SIZE)
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(HYBRID_SHARED_PB_SIZE-0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - SGX_SHARED_3DPARAMETERS_SIZE - 0x00001000)
#else
#if defined(SUPPORT_PERCONTEXT_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			0
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		0
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
#endif
#if defined(SUPPORT_SHARED_PB)
	#define SGX_SHARED_3DPARAMETERS_SIZE			SGX_3DPARAMETERS_HEAP_SIZE
	#define SGX_SHARED_3DPARAMETERS_HEAP_SIZE		(SGX_3DPARAMETERS_HEAP_SIZE - 0x00001000)
	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE		0
#endif
#endif

#if defined(SUPPORT_LARGE_GENERAL_HEAP)
	#define SGX_SHARED_3DPARAMETERS_HEAP_BASE		 0x0B800000
#else
	#define SGX_SHARED_3DPARAMETERS_HEAP_BASE		 0x08800000
#endif

	/* Size is defined above */

	#define SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE		 (SGX_SHARED_3DPARAMETERS_HEAP_BASE + SGX_SHARED_3DPARAMETERS_SIZE)
	/* Size is defined above */

	#define SGX_TADATA_HEAP_BASE				 0x0C800000
	#define SGX_TADATA_HEAP_SIZE				(0x01000000-0x00001000)

	#define SGX_SYNCINFO_HEAP_BASE				 0x0D800000
	#define SGX_SYNCINFO_HEAP_SIZE				(0x00400000-0x00001000)

	#define SGX_PDSPIXEL_CODEDATA_HEAP_BASE		 0x0DC00000
	#define SGX_PDSPIXEL_CODEDATA_HEAP_SIZE		(0x00800000-0x00001000)

	#define SGX_KERNEL_CODE_HEAP_BASE			 0x0E400000
	#define SGX_KERNEL_CODE_HEAP_SIZE			(0x00080000-0x00001000)

	#define SGX_PDSVERTEX_CODEDATA_HEAP_BASE	 0x0E800000
	#define SGX_PDSVERTEX_CODEDATA_HEAP_SIZE	(0x00800000-0x00001000)

	#define SGX_KERNEL_DATA_HEAP_BASE			(0x0F000000+SGX_KERNEL_DATA_HEAP_OFFSET)
	#define SGX_KERNEL_DATA_HEAP_SIZE			(0x00400000-(0x00001000+SGX_KERNEL_DATA_HEAP_OFFSET))

	#define SGX_PIXELSHADER_HEAP_BASE			 0x0F400000
	#define SGX_PIXELSHADER_HEAP_SIZE			(0x00500000-0x00001000)

	#define SGX_VERTEXSHADER_HEAP_BASE			 0x0FC00000
	#define SGX_VERTEXSHADER_HEAP_SIZE			(0x00200000-0x00001000)

	/* signal we've identified the core by the build */
	#define SGX_CORE_IDENTIFIED

#endif /* SGX_FEATURE_ADDRESS_SPACE_SIZE == 28 */

#if !defined(SGX_CORE_IDENTIFIED)
	#error "sgxconfig.h: ERROR: unspecified SGX Core version"
#endif

/*********************************************************************************
 *
 * SGX_PDSPIXEL_CODEDATA_HEAP_BASE + 64MB range must include PDSVERTEX_CODEDATA and KERNEL_CODE heaps
 *
 ********************************************************************************/
#if !defined (SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE)
	#if ((SGX_KERNEL_CODE_HEAP_BASE + SGX_KERNEL_CODE_HEAP_SIZE - SGX_PDSPIXEL_CODEDATA_HEAP_BASE) >  0x4000000)
	 	#error "sgxconfig.h: ERROR: SGX_KERNEL_CODE_HEAP_BASE out of range of SGX_PDSPIXEL_CODEDATA_HEAP_BASE"
	#endif
	
	#if ((SGX_PDSVERTEX_CODEDATA_HEAP_BASE + SGX_PDSVERTEX_CODEDATA_HEAP_SIZE - SGX_PDSPIXEL_CODEDATA_HEAP_BASE) >  0x4000000)
	 	#error "sgxconfig.h: ERROR: SGX_PDSVERTEX_CODEDATA_HEAP_BASE out of range of SGX_PDSPIXEL_CODEDATA_HEAP_BASE"
	#endif
#endif	

/*********************************************************************************
 *
 * The General Mapping heap must be within the 2D requestor range of the 2D heap base
 *
 ********************************************************************************/
#if defined(SGX_FEATURE_2D_HARDWARE) && defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
	#if ((SGX_GENERAL_MAPPING_HEAP_BASE + SGX_GENERAL_MAPPING_HEAP_SIZE - SGX_2D_HEAP_BASE) >= EUR_CR_BIF_TWOD_REQ_BASE_ADDR_MASK)
		#error "sgxconfig.h: ERROR: SGX_GENERAL_MAPPING_HEAP inaccessable by 2D requestor"
	#endif
#endif

/*********************************************************************************
 *
 * The kernel code heap base must be aligned to a USSE code page
 *
 ********************************************************************************/
#if defined (EURASIA_USE_CODE_PAGE_SIZE)
	#if ((SGX_KERNEL_CODE_HEAP_BASE & (EURASIA_USE_CODE_PAGE_SIZE - 1)) != 0)
		#error "sgxconfig.h: ERROR: Kernel code heap base misalignment"
	#endif
#endif

/*********************************************************************************
 *
 * Heap overlap check
 *
 ********************************************************************************/
#if defined(SGX_FEATURE_2D_HARDWARE)
	#if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
		#if ((SGX_2D_HEAP_BASE + SGX_2D_HEAP_SIZE) >= SGX_GENERAL_MAPPING_HEAP_BASE)
			#error "sgxconfig.h: ERROR: SGX_2D_HEAP overlaps SGX_GENERAL_MAPPING_HEAP"
		#endif
	#else
		#if ((SGX_2D_HEAP_BASE + SGX_2D_HEAP_SIZE) >= SGX_GENERAL_HEAP_BASE)
			#error "sgxconfig.h: ERROR: SGX_2D_HEAP overlaps SGX_GENERAL_HEAP_BASE"
		#endif
	#endif
#endif

#if defined(SUPPORT_SGX_GENERAL_MAPPING_HEAP)
	#if ((SGX_GENERAL_MAPPING_HEAP_BASE + SGX_GENERAL_MAPPING_HEAP_SIZE) >= SGX_GENERAL_HEAP_BASE)
		#error "sgxconfig.h: ERROR: SGX_GENERAL_MAPPING_HEAP overlaps SGX_GENERAL_HEAP"
	#endif
#endif

#if defined(SUPPORT_HYBRID_PB)
	#if ((HYBRID_SHARED_PB_SIZE + 0x000001000) > SGX_3DPARAMETERS_HEAP_SIZE)
		#error "sgxconfig.h: ERROR: HYBRID_SHARED_PB_SIZE too large"
	#endif
#endif

#if defined(SUPPORT_MEMORY_TILING)
	#if ((SGX_GENERAL_HEAP_BASE + SGX_GENERAL_HEAP_SIZE) >= SGX_VPB_TILED_HEAP_BASE)
		#error "sgxconfig.h: ERROR: SGX_GENERAL_HEAP overlaps SGX_VPB_TILED_HEAP"
	#endif
	#if ((SGX_VPB_TILED_HEAP_BASE + SGX_VPB_TILED_HEAP_SIZE) >= SGX_SHARED_3DPARAMETERS_HEAP_BASE)
		#error "sgxconfig.h: ERROR: SGX_VPB_TILED_HEAP overlaps SGX_3DPARAMETERS_HEAP"
	#endif
#else
	#if ((SGX_GENERAL_HEAP_BASE + SGX_GENERAL_HEAP_SIZE) >= SGX_SHARED_3DPARAMETERS_HEAP_BASE)
		#error "sgxconfig.h: ERROR: SGX_GENERAL_HEAP overlaps SGX_3DPARAMETERS_HEAP"
	#endif
#endif

#if (((SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE + SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE) >= SGX_TADATA_HEAP_BASE) && (SGX_PERCONTEXT_3DPARAMETERS_HEAP_SIZE > 0))
	#error "sgxconfig.h: ERROR: SGX_PERCONTEXT_3DPARAMETERS_HEAP_BASE overlaps SGX_TADATA_HEAP"
#endif

#if ((SGX_TADATA_HEAP_BASE + SGX_TADATA_HEAP_SIZE) >= SGX_SYNCINFO_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_TADATA_HEAP overlaps SGX_SYNCINFO_HEAP"
#endif

#if ((SGX_SYNCINFO_HEAP_BASE + SGX_SYNCINFO_HEAP_SIZE) >= SGX_PDSPIXEL_CODEDATA_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_SYNCINFO_HEAP overlaps SGX_PDSPIXEL_CODEDATA_HEAP"
#endif

#if ((SGX_PDSPIXEL_CODEDATA_HEAP_BASE + SGX_PDSPIXEL_CODEDATA_HEAP_SIZE) >= SGX_KERNEL_CODE_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_PDSPIXEL_CODEDATA_HEAP overlaps SGX_KERNEL_CODE_HEAP"
#endif

#if ((SGX_KERNEL_CODE_HEAP_BASE + SGX_KERNEL_CODE_HEAP_SIZE) >= SGX_PDSVERTEX_CODEDATA_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_KERNEL_CODE_HEAP overlaps SGX_PDSVERTEX_CODEDATA_HEAP"
#endif

#if ((SGX_PDSVERTEX_CODEDATA_HEAP_BASE + SGX_PDSVERTEX_CODEDATA_HEAP_SIZE) >= SGX_KERNEL_DATA_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_PDSVERTEX_CODEDATA_HEAP overlaps SGX_KERNEL_DATA_HEAP"
#endif

#if ((SGX_KERNEL_DATA_HEAP_BASE + SGX_KERNEL_DATA_HEAP_SIZE) >= SGX_PIXELSHADER_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_KERNEL_DATA_HEAP overlaps SGX_PIXELSHADER_HEAP"
#endif

#if ((SGX_PIXELSHADER_HEAP_BASE + SGX_PIXELSHADER_HEAP_SIZE) >= SGX_VERTEXSHADER_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_PIXELSHADER_HEAP overlaps SGX_VERTEXSHADER_HEAP"
#endif

#if ((SGX_VERTEXSHADER_HEAP_BASE + SGX_VERTEXSHADER_HEAP_SIZE) < SGX_VERTEXSHADER_HEAP_BASE)
	#error "sgxconfig.h: ERROR: SGX_VERTEXSHADER_HEAP_BASE size cause wraparound"
#endif

#endif /* __SGXCONFIG_H__ */

/*****************************************************************************
 End of file (sgxconfig.h)
*****************************************************************************/
