/*************************************************************************/ /*!
@File
@Title          device configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Memory heaps device specific configuration
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

//#warning 

#ifndef __RGXHEAPCONFIG_H__
#define __RGXHEAPCONFIG_H__

#include "rgxdefs_km.h"

#define DEV_DEVICE_TYPE			PVRSRV_DEVICE_TYPE_RGX
#define DEV_DEVICE_CLASS		PVRSRV_DEVICE_CLASS_3D

#define DEV_MAJOR_VERSION		1
#define DEV_MINOR_VERSION		0

/*      
	RGX Device Virtual Address Space Definitions:

	Notes:
	Base addresses have to be a multiple of 4MiB
	
	RGX_PDSCODEDATA_HEAP_BASE and RGX_USCCODE_HEAP_BASE will be programmed, on a
	global basis, into RGX_CR_PDS_EXEC_BASE and RGX_CR_USC_CODE_BASE_*
	respectively.
	Therefore if clients use multiple configs they must still be consistent with
	their definitions for these heaps.
*/

#if RGX_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS == 40

	/* Start at 128 Kb. Size of 256 Mb */
//	#define RGX_3DPARAMETERS_HEAP_BASE			IMG_UINT64_C(0x0000020000)
//  #define RGX_3DPARAMETERS_HEAP_SIZE			IMG_UINT64_C(0x0010000000)

	/* Start at 4GiB. Size of 512 GiB */
	#define RGX_GENERAL_HEAP_BASE				IMG_UINT64_C(0x0100000000)
    #define RGX_GENERAL_HEAP_SIZE				IMG_UINT64_C(0x8000000000)

	/* start at 516 GiB. Size of 32 GiB */
	#define RGX_BIF_TILING_NUM_HEAPS            4
	#define RGX_BIF_TILING_HEAP_SIZE            IMG_UINT64_C(0x0200000000)
	#define RGX_BIF_TILING_HEAP_1_BASE          IMG_UINT64_C(0x8100000000)
	#define RGX_BIF_TILING_HEAP_2_BASE          (RGX_BIF_TILING_HEAP_1_BASE + RGX_BIF_TILING_HEAP_SIZE)
	#define RGX_BIF_TILING_HEAP_3_BASE          (RGX_BIF_TILING_HEAP_2_BASE + RGX_BIF_TILING_HEAP_SIZE)
	#define RGX_BIF_TILING_HEAP_4_BASE          (RGX_BIF_TILING_HEAP_3_BASE + RGX_BIF_TILING_HEAP_SIZE)

	/* Start at 600GiB. Size of 4 GiB */
	#define RGX_PDSCODEDATA_HEAP_BASE			IMG_UINT64_C(0x9600000000)
    #define RGX_PDSCODEDATA_HEAP_SIZE			IMG_UINT64_C(0x0100000000)
 
	/* Start at 800GiB. Size of 4 GiB */
	#define RGX_USCCODE_HEAP_BASE				IMG_UINT64_C(0xC800000000)
    #define RGX_USCCODE_HEAP_SIZE				IMG_UINT64_C(0x0100000000)
 
	/* Start at 903GiB. Size of 4 GiB */
	#define RGX_FIRMWARE_HEAP_BASE				IMG_UINT64_C(0xE1C0000000)
    #define RGX_FIRMWARE_HEAP_SIZE				IMG_UINT64_C(0x0100000000)

	/* Start at 912GiB. Size of 16 GiB. 16GB aligned to match RGX_CR_ISP_PIXEL_BASE */
    #define RGX_TQ3DPARAMETERS_HEAP_BASE		IMG_UINT64_C(0xE400000000)
    #define RGX_TQ3DPARAMETERS_HEAP_SIZE		IMG_UINT64_C(0x0400000000)

	/* Size of 16 * 4 KB (think about large page systems .. */
#if defined(FIX_HW_BRN_37200)
    #define RGX_HWBRN37200_HEAP_BASE				IMG_UINT64_C(0xFFFFF00000)
    #define RGX_HWBRN37200_HEAP_SIZE				IMG_UINT64_C(0x0000100000)
#endif

	/* Start at 928GiB. Size of 4 GiB */
	#define RGX_DOPPLER_HEAP_BASE				IMG_UINT64_C(0xE800000000)
	#define RGX_DOPPLER_HEAP_SIZE				IMG_UINT64_C(0x0100000000)

	/* Start at 932GiB. Size of 4 GiB */
	#define RGX_DOPPLER_OVERFLOW_HEAP_BASE		IMG_UINT64_C(0xE900000000)
	#define RGX_DOPPLER_OVERFLOW_HEAP_SIZE		IMG_UINT64_C(0x0100000000)
	
	/* signal we've identified the core by the build */
	#define RGX_CORE_IDENTIFIED
#endif /* RGX_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS == 40 */

#if !defined(RGX_CORE_IDENTIFIED)
	#error "rgxheapconfig.h: ERROR: unspecified RGX Core version"
#endif

/* /\********************************************************************************* */
/*  * */
/*  * Heap overlap check */
/*  * */
/*  ********************************************************************************\/ */
/* #if defined(SUPPORT_RGX_GENERAL_MAPPING_HEAP) */
/* 	#if ((RGX_GENERAL_MAPPING_HEAP_BASE + RGX_GENERAL_MAPPING_HEAP_SIZE) >= RGX_GENERAL_HEAP_BASE) */
/* 		#error "rgxheapconfig.h: ERROR: RGX_GENERAL_MAPPING_HEAP overlaps RGX_GENERAL_HEAP" */
/* 	#endif */
/* #endif */

/* #if ((RGX_GENERAL_HEAP_BASE + RGX_GENERAL_HEAP_SIZE) >= RGX_3DPARAMETERS_HEAP_BASE) */
/* 	#error "rgxheapconfig.h: ERROR: RGX_GENERAL_HEAP overlaps RGX_3DPARAMETERS_HEAP" */
/* #endif */

/* #if ((RGX_3DPARAMETERS_HEAP_BASE + RGX_3DPARAMETERS_HEAP_SIZE) >= RGX_TADATA_HEAP_BASE) */
/* 	#error "rgxheapconfig.h: ERROR: RGX_3DPARAMETERS_HEAP overlaps RGX_TADATA_HEAP" */
/* #endif */

/* #if ((RGX_TADATA_HEAP_BASE + RGX_TADATA_HEAP_SIZE) >= RGX_SYNCINFO_HEAP_BASE) */
/* 	#error "rgxheapconfig.h: ERROR: RGX_TADATA_HEAP overlaps RGX_SYNCINFO_HEAP" */
/* #endif */

/* #if ((RGX_SYNCINFO_HEAP_BASE + RGX_SYNCINFO_HEAP_SIZE) >= RGX_PDSPIXEL_CODEDATA_HEAP_BASE) */
/* 	#error "rgxheapconfig.h: ERROR: RGX_SYNCINFO_HEAP overlaps RGX_PDSPIXEL_CODEDATA_HEAP" */
/* #endif */

/* #if ((RGX_PDSPIXEL_CODEDATA_HEAP_BASE + RGX_PDSPIXEL_CODEDATA_HEAP_SIZE) >= RGX_KERNEL_CODE_HEAP_BASE) */
/* 	#error "rgxheapconfig.h: ERROR: RGX_PDSPIXEL_CODEDATA_HEAP overlaps RGX_KERNEL_CODE_HEAP" */
/* #endif */

/* #if ((RGX_KERNEL_CODE_HEAP_BASE + RGX_KERNEL_CODE_HEAP_SIZE) >= RGX_PDSVERTEX_CODEDATA_HEAP_BASE) */
/* 	#error "rgxheapconfig.h: ERROR: RGX_KERNEL_CODE_HEAP overlaps RGX_PDSVERTEX_CODEDATA_HEAP" */
/* #endif */

/* #if ((RGX_PDSVERTEX_CODEDATA_HEAP_BASE + RGX_PDSVERTEX_CODEDATA_HEAP_SIZE) >= RGX_KERNEL_DATA_HEAP_BASE) */
/* 	#error "rgxheapconfig.h: ERROR: RGX_PDSVERTEX_CODEDATA_HEAP overlaps RGX_KERNEL_DATA_HEAP" */
/* #endif */

/* #if ((RGX_KERNEL_DATA_HEAP_BASE + RGX_KERNEL_DATA_HEAP_SIZE) >= RGX_PIXELSHADER_HEAP_BASE) */
/* 	#error "rgxheapconfig.h: ERROR: RGX_KERNEL_DATA_HEAP overlaps RGX_PIXELSHADER_HEAP" */
/* #endif */

/* #if ((RGX_PIXELSHADER_HEAP_BASE + RGX_PIXELSHADER_HEAP_SIZE) >= RGX_VERTEXSHADER_HEAP_BASE) */
/* 	#error "rgxheapconfig.h: ERROR: RGX_PIXELSHADER_HEAP overlaps RGX_VERTEXSHADER_HEAP" */
/* #endif */

/* #if ((RGX_VERTEXSHADER_HEAP_BASE + RGX_VERTEXSHADER_HEAP_SIZE) < RGX_VERTEXSHADER_HEAP_BASE) */
/* 	#error "rgxheapconfig.h: ERROR: RGX_VERTEXSHADER_HEAP_BASE size cause wraparound" */
/* #endif */

#endif /* __RGXHEAPCONFIG_H__ */

/*****************************************************************************
 End of file (rgxheapconfig.h)
*****************************************************************************/
