/*************************************************************************/ /*!
@File
@Title          RGX Device virtual memory map
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

#ifndef RGXHEAPCONFIG_H
#define RGXHEAPCONFIG_H

#include "rgxdefs_km.h"


#define RGX_HEAP_SIZE_4KiB       IMG_UINT64_C(0x0000001000)
#define RGX_HEAP_SIZE_64KiB      IMG_UINT64_C(0x0000010000)
#define RGX_HEAP_SIZE_256KiB     IMG_UINT64_C(0x0000040000)

#define RGX_HEAP_SIZE_1MiB       IMG_UINT64_C(0x0000100000)
#define RGX_HEAP_SIZE_2MiB       IMG_UINT64_C(0x0000200000)
#define RGX_HEAP_SIZE_4MiB       IMG_UINT64_C(0x0000400000)
#define RGX_HEAP_SIZE_16MiB      IMG_UINT64_C(0x0001000000)
#define RGX_HEAP_SIZE_256MiB     IMG_UINT64_C(0x0010000000)

#define RGX_HEAP_SIZE_1GiB       IMG_UINT64_C(0x0040000000)
#define RGX_HEAP_SIZE_2GiB       IMG_UINT64_C(0x0080000000)
#define RGX_HEAP_SIZE_4GiB       IMG_UINT64_C(0x0100000000)
#define RGX_HEAP_SIZE_16GiB      IMG_UINT64_C(0x0400000000)
#define RGX_HEAP_SIZE_32GiB      IMG_UINT64_C(0x0800000000)
#define RGX_HEAP_SIZE_64GiB      IMG_UINT64_C(0x1000000000)
#define RGX_HEAP_SIZE_128GiB     IMG_UINT64_C(0x2000000000)
#define RGX_HEAP_SIZE_256GiB     IMG_UINT64_C(0x4000000000)
#define RGX_HEAP_SIZE_512GiB     IMG_UINT64_C(0x8000000000)

/*
	RGX Device Virtual Address Space Definitions

	This file defines the RGX virtual address heaps that are used in
	application memory contexts. It also shows where the Firmware memory heap
	fits into this, but the firmware heap is only ever created in the
	Services KM/server component.

	RGX_PDSCODEDATA_HEAP_BASE and RGX_USCCODE_HEAP_BASE will be programmed,
	on a global basis, into RGX_CR_PDS_EXEC_BASE and RGX_CR_USC_CODE_BASE_*
	respectively. Therefore if clients use multiple configs they must still
	be consistent with their definitions for these heaps.

	Shared virtual memory (GENERAL_SVM) support requires half of the address
	space (512 GiB) be reserved for SVM allocations to mirror application CPU
	addresses. However, if BRN_65273 WA is active in which case the SVM heap
	is disabled. This is reflected in the device connection capability bits
	returned to user space.

	The GENERAL non-SVM region is 512 GiB to 768 GiB and is shared between the
	general (4KiB) heap and the general non-4K heap. The first 128 GiB is used
	for the GENERAL_HEAP (4KiB) and the last 32 GiB is used for the
	GENERAL_NON4K_HEAP. This heap has a default page-size of 16K.
	AppHint PVRSRV_APPHINT_GENERALNON4KHEAPPAGESIZE can be used to forced it
	to these values: 4K,64K,256K,1M,2M.

	The heaps defined for BRN_65273 _replace_ the non-BRN equivalents below
	when this BRN WA is active on affected cores. This is different to most
	other BRNs and hence has been given its own header file for clarity,
	see below. This	is a special case, other BRNs that need 1 or 2 additional
	heaps should be added to this file, like BRN_63142 below.
	NOTE: All regular heaps below greater than 1GB require a BRN_65273 WA heap.

	Base addresses have to be a multiple of 4MiB
	Heaps must not start at 0x0000000000, as this is reserved for internal
	use within device memory layer.
	Range comments, those starting in column 0 below are a section heading of
	sorts and are above the heaps in that range. Often this is the reserved
	size of the heap within the range.
*/

/* This BRN requires a different virtual memory map from the standard one
 * defined in this file below. Hence the alternative heap definitions for this
 * BRN are provided in a separate file for clarity. */
#include "rgxheapconfig_65273.h"


/* 0x00_0000_0000 ************************************************************/

/* 0x00_0000_0000 - 0x00_0040_0000 **/
	/* 0 MiB to 4 MiB, size of 4 MiB : RESERVED **/

	/* BRN_65273 TQ3DPARAMETERS base 0x0000010000 */
	/* BRN_65273 GENERAL base        0x65C0000000 */
	/* BRN_65273 GENERAL_NON4K base  0x73C0000000 */

/* 0x00_0040_0000 - 0x7F_FFC0_0000 **/
	/* 4 MiB to 512 GiB, size of 512 GiB less 4 MiB : GENERAL_SVM_HEAP **/
	#define RGX_GENERAL_SVM_HEAP_BASE           IMG_UINT64_C(0x0000400000)
	#define RGX_GENERAL_SVM_HEAP_SIZE           (RGX_HEAP_SIZE_512GiB - RGX_HEAP_SIZE_4MiB)


/* 0x80_0000_0000 ************************************************************/

/* 0x80_0000_0000 - 0x9F_FFFF_FFFF **/
	/* 512 GiB to 640 GiB, size of 128 GiB : GENERAL_HEAP **/
	#define RGX_GENERAL_HEAP_BASE               IMG_UINT64_C(0x8000000000)
	#define RGX_GENERAL_HEAP_SIZE               RGX_HEAP_SIZE_128GiB

	/* BRN_65273 PDSCODEDATA base    0xA800000000 */

/* 0xA0_0000_0000 - 0xAF_FFFF_FFFF **/
	/* 640 GiB to 704 GiB, size of 64 GiB : FREE **/

/* B0_0000_0000 - 0xB7_FFFF_FFFF **/
	/* 704 GiB to 736 GiB, size of 32 GiB : FREE **/

	/* BRN_65273 USCCODE base        0xBA00000000 */

/* 0xB8_0000_0000 - 0xBF_FFFF_FFFF **/
	/* 736 GiB to 768 GiB, size of 32 GiB : GENERAL_NON4K_HEAP **/
	#define RGX_GENERAL_NON4K_HEAP_BASE         IMG_UINT64_C(0xB800000000)
	#define RGX_GENERAL_NON4K_HEAP_SIZE         RGX_HEAP_SIZE_32GiB


/* 0xC0_0000_0000 ************************************************************/

/* 0xC0_0000_0000 - 0xD9_FFFF_FFFF **/
	/* 768 GiB to 872 GiB, size of 104 GiB : FREE **/

/* 0xDA_0000_0000 - 0xDA_FFFF_FFFF **/
	/* 872 GiB to 876 GiB, size of 4 GiB : PDSCODEDATA_HEAP **/
	#define RGX_PDSCODEDATA_HEAP_BASE           IMG_UINT64_C(0xDA00000000)
	#define RGX_PDSCODEDATA_HEAP_SIZE           RGX_HEAP_SIZE_4GiB

/* 0xDB_0000_0000 - 0xDB_FFFF_FFFF **/
	/* 876 GiB to 880 GiB, size of 256 MiB (reserved 4GiB) : BRN **/
	/* HWBRN63142 workaround requires Region Header memory to be at the top
	   of a 16GiB aligned range. This is so when masked with 0x03FFFFFFFF the
	   address will avoid aliasing PB addresses. Start at 879.75GiB. Size of 256MiB. */
	#define RGX_RGNHDR_BRN_63142_HEAP_BASE      IMG_UINT64_C(0xDBF0000000)
	#define RGX_RGNHDR_BRN_63142_HEAP_SIZE      RGX_HEAP_SIZE_256MiB

/* 0xDC_0000_0000 - 0xDF_FFFF_FFFF **/
	/* 880 GiB to 896 GiB, size of 16 GiB : FREE **/

/* 0xE0_0000_0000 - 0xE0_FFFF_FFFF **/
	/* 896 GiB to 900 GiB, size of 4 GiB : USCCODE_HEAP **/
	#define RGX_USCCODE_HEAP_BASE               IMG_UINT64_C(0xE000000000)
	#define RGX_USCCODE_HEAP_SIZE               RGX_HEAP_SIZE_4GiB

/* 0xE1_0000_0000 - 0xE1_BFFF_FFFF **/
	/* 900 GiB to 903 GiB, size of 3 GiB : RESERVED **/

/* 0xE1_C000_000 - 0xE1_FFFF_FFFF **/
	/* 903 GiB to 904 GiB, reserved 1 GiB, : FIRMWARE_HEAP **/

	/* Firmware heaps defined in rgx_heap_firmware.h as they are not present in
	   application memory contexts, see:
	    RGX_FIRMWARE_RAW_HEAP_BASE
	    RGX_FIRMWARE_RAW_HEAP_SIZE
	   See header for other sub-heaps details
	*/

/* 0xE2_0000_0000 - 0xE3_FFFF_FFFF **/
	/* 904 GiB to 912 GiB, size of 8 GiB : FREE **/

	/* BRN_65273 VISIBILITY_TEST base 0xE400000000 */

/* 0xE4_0000_0000 - 0xE7_FFFF_FFFF **/
	/* 912 GiB to 928 GiB, size 16 GiB : TQ3DPARAMETERS_HEAP **/
	/* Aligned to match RGX_CR_ISP_PIXEL_BASE at 16 GiB */
	#define RGX_TQ3DPARAMETERS_HEAP_BASE        IMG_UINT64_C(0xE400000000)
	#define RGX_TQ3DPARAMETERS_HEAP_SIZE        RGX_HEAP_SIZE_16GiB

/* 0xE8_0000_0000 - 0xE8_FFFF_FFFF **/
	/* 928 GiB to 932 GiB, size of 4 GiB : FREE **/

/* 0xE9_0000_0000 - 0xE9_3FFF_FFFF **/
	/* 932 GiB to 933 GiB, size of 1 GiB : VK_CAPT_REPLAY_HEAP **/
	#define RGX_VK_CAPT_REPLAY_HEAP_BASE        IMG_UINT64_C(0xE900000000)
	#define RGX_VK_CAPT_REPLAY_HEAP_SIZE        RGX_HEAP_SIZE_1GiB

/* 0xE9_4000_0000 - 0xE9_FFFF_FFFF **/
	/* 933 GiB to 936 GiB, size of 3 GiB : FREE **/

/* 0xEA_0000_0000 - 0xEA_0000_0FFF **/
	/* 936 GiB to 937 GiB, size of min heap size : SIGNALS_HEAP **/
	/* CDM Signals heap (31 signals less one reserved for Services).
	 * Size 960B rounded up to minimum heap size */
	#define RGX_SIGNALS_HEAP_BASE               IMG_UINT64_C(0xEA00000000)
	#define RGX_SIGNALS_HEAP_SIZE               DEVMEM_HEAP_MINIMUM_SIZE

/* 0xEA_4000_0000 - 0xEA_FFFF_FFFF **/
	/* 937 GiB to 940 GiB, size of 3 GiB : FREE **/

/* 0xEB_0000_0000 - 0xED_FFFF_FFFF **/
	/* 940 GiB to 952 GiB, size of 12 GiB : RESERVED VOLCANIC  **/

/* 0xEE_0000_0000 - 0xEE_3FFF_FFFF **/
	/* 952 GiB to 953 GiB, size of 1 GiB : CMP_MISSION_RMW_HEAP **/
	#define RGX_CMP_MISSION_RMW_HEAP_BASE       IMG_UINT64_C(0xEE00000000)
	#define RGX_CMP_MISSION_RMW_HEAP_SIZE       RGX_HEAP_SIZE_1GiB

/* 0xEE_4000_0000 - 0xEE_FFFF_FFFF **/
	/* 953 GiB to 956 GiB, size of 3 GiB : RESERVED **/

/* 0xEF_0000_0000 - 0xEF_3FFF_FFFF **/
	/* 956 GiB to 957 GiB, size of 1 GiB : CMP_SAFETY_RMW_HEAP **/
	#define RGX_CMP_SAFETY_RMW_HEAP_BASE        IMG_UINT64_C(0xEF00000000)
	#define RGX_CMP_SAFETY_RMW_HEAP_SIZE        RGX_HEAP_SIZE_1GiB

/* 0xEF_4000_0000 - 0xEF_FFFF_FFFF **/
	/* 957 GiB to 960 GiB, size of 3 GiB : RESERVED **/

/* 0xF0_0000_0000 - 0xF0_FFFF_FFFF **/
	/* 960 GiB to 964 GiB, size of 4 GiB : RESERVED VOLCANIC **/

/* 0xF1_0000_0000 - 0xF1_FFFF_FFFF **/
	/* 964 GiB to 968 GiB, size of 4 GiB : FREE **/

/* 0xF2_0000_0000 - 0xF2_001F_FFFF **/
	/* 968 GiB to 969 GiB, size of 2 MiB : VISIBILITY_TEST_HEAP **/
	#define RGX_VISIBILITY_TEST_HEAP_BASE       IMG_UINT64_C(0xF200000000)
	#define RGX_VISIBILITY_TEST_HEAP_SIZE       RGX_HEAP_SIZE_2MiB

/* 0xF2_4000_0000 - 0xF2_FFFF_FFFF **/
	/* 969 GiB to 972 GiB, size of 3 GiB : FREE **/

	/* BRN_65273 MMU_INIA base 0xF800000000 */
	/* BRN_65273 MMU_INIB base 0xF900000000 */

/* 0xF3_0000_0000 - 0xFF_FFFF_FFFF **/
	/* 972 GiB to 1024 GiB, size of 52 GiB : FREE **/



/* 0xFF_FFFF_FFFF ************************************************************/

/*	End of RGX Device Virtual Address Space definitions */

#endif /* RGXHEAPCONFIG_H */

/******************************************************************************
 End of file (rgxheapconfig.h)
******************************************************************************/
