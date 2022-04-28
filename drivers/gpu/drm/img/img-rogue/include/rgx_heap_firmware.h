/*************************************************************************/ /*!
@File
@Title          RGX FW heap definitions
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

#if !defined(RGX_HEAP_FIRMWARE_H)
#define RGX_HEAP_FIRMWARE_H

/* Start at 903GiB. Size of 32MB per OSID (see rgxheapconfig.h)
 * NOTE:
 *      The firmware heaps bases and sizes are defined here to
 *      simplify #include dependencies, see rgxheapconfig.h
 *      for the full RGX virtual address space layout.
 */

/*
 * The Config heap holds initialisation data shared between the
 * the driver and firmware (e.g. pointers to the KCCB and FWCCB).
 * The Main Firmware heap size is adjusted accordingly but most
 * of the map / unmap functions must take into consideration
 * the entire range (i.e. main and config heap).
 */
#define RGX_FIRMWARE_NUMBER_OF_FW_HEAPS              (2)
#define RGX_FIRMWARE_HEAP_SHIFT                      RGX_FW_HEAP_SHIFT
#define RGX_FIRMWARE_RAW_HEAP_BASE                   (0xE1C0000000ULL)
#define RGX_FIRMWARE_RAW_HEAP_SIZE                   (IMG_UINT32_C(1) << RGX_FIRMWARE_HEAP_SHIFT)

/* To enable the firmware to compute the exact address of structures allocated by the KM
 * in the Fw Config subheap, regardless of the KM's page size (and PMR granularity),
 * objects allocated consecutively but from different PMRs (due to differing memalloc flags)
 * are allocated with a 64kb offset. This way, all structures will be located at the same base
 * addresses when the KM is running with a page size of 4k, 16k or 64k.  */
#define RGX_FIRMWARE_CONFIG_HEAP_ALLOC_GRANULARITY    (IMG_UINT32_C(0x10000))

/* Ensure the heap can hold 3 PMRs of maximum supported granularity (192KB):
 * 1st PMR: RGXFWIF_CONNECTION_CTL
 * 2nd PMR: RGXFWIF_OSINIT
 * 3rd PMR: RGXFWIF_SYSINIT */
#define RGX_FIRMWARE_CONFIG_HEAP_SIZE                (3*RGX_FIRMWARE_CONFIG_HEAP_ALLOC_GRANULARITY)

#define RGX_FIRMWARE_META_MAIN_HEAP_SIZE             (RGX_FIRMWARE_RAW_HEAP_SIZE - RGX_FIRMWARE_CONFIG_HEAP_SIZE)
/*
 * MIPS FW needs space in the Main heap to map GPU memory.
 * This space is taken from the MAIN heap, to avoid creating a new heap.
 */
#define RGX_FIRMWARE_MIPS_GPU_MAP_RESERVED_SIZE_NORMAL       (IMG_UINT32_C(0x100000)) /* 1MB */
#define RGX_FIRMWARE_MIPS_GPU_MAP_RESERVED_SIZE_BRN65101     (IMG_UINT32_C(0x400000)) /* 4MB */

#define RGX_FIRMWARE_MIPS_MAIN_HEAP_SIZE_NORMAL      (RGX_FIRMWARE_RAW_HEAP_SIZE -  RGX_FIRMWARE_CONFIG_HEAP_SIZE - \
                                                      RGX_FIRMWARE_MIPS_GPU_MAP_RESERVED_SIZE_NORMAL)

#define RGX_FIRMWARE_MIPS_MAIN_HEAP_SIZE_BRN65101    (RGX_FIRMWARE_RAW_HEAP_SIZE -  RGX_FIRMWARE_CONFIG_HEAP_SIZE - \
                                                      RGX_FIRMWARE_MIPS_GPU_MAP_RESERVED_SIZE_BRN65101)

#if !defined(__KERNEL__)
#if defined(FIX_HW_BRN_65101)
#define RGX_FIRMWARE_MIPS_GPU_MAP_RESERVED_SIZE      RGX_FIRMWARE_MIPS_GPU_MAP_RESERVED_SIZE_BRN65101
#define RGX_FIRMWARE_MIPS_MAIN_HEAP_SIZE             RGX_FIRMWARE_MIPS_MAIN_HEAP_SIZE_BRN65101

#include "img_defs.h"
static_assert((RGX_FIRMWARE_RAW_HEAP_SIZE) >= IMG_UINT32_C(0x800000), "MIPS GPU map size cannot be increased due to BRN65101 with a small FW heap");

#else
#define RGX_FIRMWARE_MIPS_GPU_MAP_RESERVED_SIZE      RGX_FIRMWARE_MIPS_GPU_MAP_RESERVED_SIZE_NORMAL
#define RGX_FIRMWARE_MIPS_MAIN_HEAP_SIZE             RGX_FIRMWARE_MIPS_MAIN_HEAP_SIZE_NORMAL
#endif
#endif /* !defined(__KERNEL__) */

/* Host sub-heap order: MAIN + CONFIG */
#define RGX_FIRMWARE_HOST_MAIN_HEAP_BASE             RGX_FIRMWARE_RAW_HEAP_BASE
#define RGX_FIRMWARE_HOST_CONFIG_HEAP_BASE           (RGX_FIRMWARE_HOST_MAIN_HEAP_BASE + \
                                                      RGX_FIRMWARE_RAW_HEAP_SIZE - \
                                                      RGX_FIRMWARE_CONFIG_HEAP_SIZE)

/* Guest sub-heap order: CONFIG + MAIN */
#define RGX_FIRMWARE_GUEST_CONFIG_HEAP_BASE          RGX_FIRMWARE_RAW_HEAP_BASE
#define RGX_FIRMWARE_GUEST_MAIN_HEAP_BASE            (RGX_FIRMWARE_GUEST_CONFIG_HEAP_BASE + \
                                                      RGX_FIRMWARE_CONFIG_HEAP_SIZE)

/*
 * The maximum configurable size via RGX_FW_HEAP_SHIFT is 32MiB (1<<25) and
 * the minimum is 4MiB (1<<22); the default firmware heap size is set to
 * maximum 32MiB.
 */
#if defined(RGX_FW_HEAP_SHIFT) && (RGX_FW_HEAP_SHIFT < 22 || RGX_FW_HEAP_SHIFT > 25)
#error "RGX_FW_HEAP_SHIFT is outside valid range [22, 25]"
#endif

#endif /* RGX_HEAP_FIRMWARE_H */
