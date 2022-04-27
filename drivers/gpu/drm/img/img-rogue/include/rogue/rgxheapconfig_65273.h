/*************************************************************************/ /*!
@File
@Title          RGX Device virtual memory map for BRN_65273.
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

#ifndef RGXHEAPCONFIG_65273_H
#define RGXHEAPCONFIG_65273_H

/*
	RGX Device Virtual Address Space Definitions

	This file defines the RGX virtual address replacement heaps that are used in
	in application memory contexts for BRN_65273.

	The heaps defined for BRN_65273 _replace_ the non-BRN equivalents when this
	BRN WA is active on affected cores. This is different to most other BRNs
	and hence has been given its own header file for clarity. The SVM_HEAP is
	also disabled and unavailable when the WA is active. This is reflected
	in the device connection capability bits returned to user space.
	NOTE: All regular heaps in rgxheapconfig.h greater than 1GB require
	      a BRN_65273 WA heap.

	Base addresses must have to be a multiple of 4MiB
	Heaps must not start at 0x0000000000, as this is reserved for internal
	use within device memory layer.
	Range comments, those starting in column 0 below are a section heading of
	sorts and are above the heaps in that range.
*/


/* 0x00_0000_0000 ************************************************************/

/* 0x00_0001_0000 - 0x00_3FFF_FFFF **/
	/* HWBRN65273 workaround requires TQ memory to start at 64 KiB and use a
	 * unique single 0.99GiB PCE entry. */
	#define RGX_TQ3DPARAMETERS_BRN_65273_HEAP_BASE  IMG_UINT64_C(0x0000010000)
	#define RGX_TQ3DPARAMETERS_BRN_65273_HEAP_SIZE  (RGX_HEAP_SIZE_1GiB - RGX_HEAP_SIZE_64KiB)

/* 0x65_C000_0000 - 0x66_3FFF_FFFF **/
	/* HWBRN65273 workaround requires General Heap to use a unique PCE entry for each GiB in range */
	#define RGX_GENERAL_BRN_65273_HEAP_BASE         IMG_UINT64_C(0x65C0000000)
	#define RGX_GENERAL_BRN_65273_HEAP_SIZE         RGX_HEAP_SIZE_2GiB

/* 0x73_C000_0000 - 0x74_3FFF_FFFF **/
	/* HWBRN65273 workaround requires Non4K memory to use a unique PCE entry for each GiB in range */
	#define RGX_GENERAL_NON4K_BRN_65273_HEAP_BASE   IMG_UINT64_C(0x73C0000000)
	#define RGX_GENERAL_NON4K_BRN_65273_HEAP_SIZE   RGX_HEAP_SIZE_2GiB


/* 0x80_0000_0000 ************************************************************/

/* 0xA8_0000_0000 - 0xA8_3FFF_FFFF **/
	/* HWBRN65273 workaround requires PDS memory to use a unique single 1GiB PCE entry. */
	#define RGX_PDSCODEDATA_BRN_65273_HEAP_BASE     IMG_UINT64_C(0xA800000000)
	#define RGX_PDSCODEDATA_BRN_65273_HEAP_SIZE     RGX_HEAP_SIZE_1GiB

/* 0xBA_0000_0000 - 0xBA_3FFF_FFFF **/
	/* HWBRN65273 workaround requires USC memory to use a unique single 1GiB PCE entry. */
	#define RGX_USCCODE_BRN_65273_HEAP_BASE         IMG_UINT64_C(0xBA00000000)
	#define RGX_USCCODE_BRN_65273_HEAP_SIZE         RGX_HEAP_SIZE_1GiB


/* 0xC0_0000_0000 ************************************************************/

/* 0xE4_0000_0000 - 0xE4_001F_FFFF **/
	/* HWBRN65273 workaround requires USC memory to use a unique single 1GiB PCE entry. */
	#define RGX_VISIBILITY_TEST_BRN_65273_HEAP_BASE IMG_UINT64_C(0xE400000000)
	#define RGX_VISIBILITY_TEST_BRN_65273_HEAP_SIZE RGX_HEAP_SIZE_2MiB

/* 0xF8_0000_0000 - 0xF9_FFFF_FFFF **/
	/* HWBRN65273 workaround requires two Region Header buffers 4GiB apart. */
	#define RGX_MMU_INIA_BRN_65273_HEAP_BASE        IMG_UINT64_C(0xF800000000)
	#define RGX_MMU_INIA_BRN_65273_HEAP_SIZE        RGX_HEAP_SIZE_1GiB
	#define RGX_MMU_INIB_BRN_65273_HEAP_BASE        IMG_UINT64_C(0xF900000000)
	#define RGX_MMU_INIB_BRN_65273_HEAP_SIZE        RGX_HEAP_SIZE_1GiB


/* 0xFF_FFFF_FFFF ************************************************************/

/*	End of RGX Device Virtual Address Space definitions */

#endif /* RGXHEAPCONFIG_65273_H */

/******************************************************************************
 End of file (rgxheapconfig_65273.h)
******************************************************************************/
