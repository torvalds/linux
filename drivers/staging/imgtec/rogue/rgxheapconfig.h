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

//#warning FIXME:  add the MMU specialisation defines here (or in hwdefs, perhaps?)

#ifndef __RGXHEAPCONFIG_H__
#define __RGXHEAPCONFIG_H__

#include "rgxdefs_km.h"

/*
	RGX Device Virtual Address Space Definitions
	NOTES:
		Base addresses have to be a multiple of 4MiB

		RGX_PDSCODEDATA_HEAP_BASE and RGX_USCCODE_HEAP_BASE will be programmed,
		on a global basis, into RGX_CR_PDS_EXEC_BASE and RGX_CR_USC_CODE_BASE_*
		respectively. Therefore if clients use multiple configs they must still
		be consistent with their definitions for these heaps.

		Shared virtual memory (GENERAL_SVM) support requires half of the address
		space be reserved for SVM allocations unless BRN fixes are required in
		which case the SVM heap is disabled. This is reflected in the device
		connection capability bits returned to userspace.
		
		Variable page-size heap (GENERAL_NON4K) support splits available fixed
		4K page-size heap (GENERAL) address space in half. The actual page size
		defaults to 16K; AppHint PVRSRV_APPHINT_GENERAL_NON4K_HEAP_PAGE_SIZE
		can be used to forced it to these values: 4K,64K,256K,1M,2M.
*/

	/* Start at 4 MiB Size of 512 GiB less 4 MiB (managed by OS/Services) */
	#define RGX_GENERAL_SVM_HEAP_BASE			IMG_UINT64_C(0x0000400000)
	#define RGX_GENERAL_SVM_HEAP_SIZE			IMG_UINT64_C(0x7FFFC00000)

	/* Start at 512GiB. Size of 256 GiB */
	#define RGX_GENERAL_HEAP_BASE				IMG_UINT64_C(0x8000000000)
	#define RGX_GENERAL_HEAP_SIZE				IMG_UINT64_C(0x4000000000)

	/* Start at 768GiB. Size of 64 GiB */
	#define RGX_GENERAL_NON4K_HEAP_BASE			IMG_UINT64_C(0xC000000000)
	#define RGX_GENERAL_NON4K_HEAP_SIZE			IMG_UINT64_C(0x1000000000)

	/* Start at 832 GiB. Size of 32 GiB */
	#define RGX_BIF_TILING_NUM_HEAPS			4
	#define RGX_BIF_TILING_HEAP_SIZE			IMG_UINT64_C(0x0200000000)
	#define RGX_BIF_TILING_HEAP_1_BASE			IMG_UINT64_C(0xD000000000)
	#define RGX_BIF_TILING_HEAP_2_BASE			(RGX_BIF_TILING_HEAP_1_BASE + RGX_BIF_TILING_HEAP_SIZE)
	#define RGX_BIF_TILING_HEAP_3_BASE			(RGX_BIF_TILING_HEAP_2_BASE + RGX_BIF_TILING_HEAP_SIZE)
	#define RGX_BIF_TILING_HEAP_4_BASE			(RGX_BIF_TILING_HEAP_3_BASE + RGX_BIF_TILING_HEAP_SIZE)

	/* HWBRN52402 workaround requires PDS memory to be below 16GB. Start at 8GB. Size of 4GB. */
	#define RGX_PDSCODEDATA_BRN_52402_HEAP_BASE	IMG_UINT64_C(0x0200000000)
	#define RGX_PDSCODEDATA_BRN_52402_HEAP_SIZE	IMG_UINT64_C(0x0100000000)

	/* Start at 872 GiB. Size of 4 GiB */
	#define RGX_PDSCODEDATA_HEAP_BASE			IMG_UINT64_C(0xDA00000000)
    #define RGX_PDSCODEDATA_HEAP_SIZE			IMG_UINT64_C(0x0100000000)

	/* HWBRN63142 workaround requires Region Header memory to be at the top
	   of a 16GB aligned range. This is so when masked with 0x03FFFFFFFF the
	   address will avoid aliasing PB addresses. Start at 879.75GB. Size of 256MB. */
	#define RGX_RGNHDR_BRN_63142_HEAP_BASE		IMG_UINT64_C(0xDBF0000000)
	#define RGX_RGNHDR_BRN_63142_HEAP_SIZE		IMG_UINT64_C(0x0010000000)

	/* Start at 880 GiB, Size of 1 MiB */
	#define RGX_VISTEST_HEAP_BASE				IMG_UINT64_C(0xDC00000000)
	#define RGX_VISTEST_HEAP_SIZE				IMG_UINT64_C(0x0000100000)

	/* HWBRN52402 workaround requires PDS memory to be below 16GB. Start at 12GB. Size of 4GB. */
	#define RGX_USCCODE_BRN_52402_HEAP_BASE		IMG_UINT64_C(0x0300000000)
	#define RGX_USCCODE_BRN_52402_HEAP_SIZE		IMG_UINT64_C(0x0100000000)

	/* Start at 896 GiB Size of 4 GiB */
	#define RGX_USCCODE_HEAP_BASE				IMG_UINT64_C(0xE000000000)
	#define RGX_USCCODE_HEAP_SIZE				IMG_UINT64_C(0x0100000000)


	/* Start at 903GiB. Size of 32MB per OSID (defined in rgxdefs_km.h)
	   #define RGX_FIRMWARE_HEAP_BASE			IMG_UINT64_C(0xE1C0000000)
	   #define RGX_FIRMWARE_HEAP_SIZE			(1<<RGX_FW_HEAP_SHIFT) 
	   #define RGX_FIRMWARE_HEAP_SHIFT			RGX_FW_HEAP_SHIFT */

	/* HWBRN52402 & HWBRN55091 workarounds requires TQ memory to be below 16GB and 16GB aligned. Start at 0GB. Size of 8GB. */
	#define RGX_TQ3DPARAMETERS_BRN_52402_55091_HEAP_BASE		IMG_UINT64_C(0x0000000000)
	#define RGX_TQ3DPARAMETERS_BRN_52402_55091_HEAP_SIZE		IMG_UINT64_C(0x0200000000)

	/* Start at 912GiB. Size of 16 GiB. 16GB aligned to match RGX_CR_ISP_PIXEL_BASE */
	#define RGX_TQ3DPARAMETERS_HEAP_BASE		IMG_UINT64_C(0xE400000000)
	#define RGX_TQ3DPARAMETERS_HEAP_SIZE		IMG_UINT64_C(0x0400000000)

	/* Size of 16 * 4 KB (think about large page systems .. */
  	#define RGX_HWBRN37200_HEAP_BASE				IMG_UINT64_C(0xFFFFF00000)
   	#define RGX_HWBRN37200_HEAP_SIZE				IMG_UINT64_C(0x0000100000)

	/* Start at 928GiB. Size of 4 GiB */
	#define RGX_DOPPLER_HEAP_BASE				IMG_UINT64_C(0xE800000000)
	#define RGX_DOPPLER_HEAP_SIZE				IMG_UINT64_C(0x0100000000)

	/* Start at 932GiB. Size of 4 GiB */
	#define RGX_DOPPLER_OVERFLOW_HEAP_BASE		IMG_UINT64_C(0xE900000000)
	#define RGX_DOPPLER_OVERFLOW_HEAP_SIZE		IMG_UINT64_C(0x0100000000)

	/* Start at 936GiB. Two groups of 128 KBytes that must follow each other in this order. */
	#define RGX_SERVICES_SIGNALS_HEAP_BASE		IMG_UINT64_C(0xEA00000000)
	#define RGX_SERVICES_SIGNALS_HEAP_SIZE		IMG_UINT64_C(0x0000020000)

	#define RGX_SIGNALS_HEAP_BASE				IMG_UINT64_C(0xEA00020000)
	#define RGX_SIGNALS_HEAP_SIZE				IMG_UINT64_C(0x0000020000)

	/* TDM TPU YUV coeffs - can be reduced to a single page */
	#define RGX_TDM_TPU_YUV_COEFFS_HEAP_BASE	IMG_UINT64_C(0xEA00080000)
	#define RGX_TDM_TPU_YUV_COEFFS_HEAP_SIZE	IMG_UINT64_C(0x0000040000)


#endif /* __RGXHEAPCONFIG_H__ */

/*****************************************************************************
 End of file (rgxheapconfig.h)
*****************************************************************************/
