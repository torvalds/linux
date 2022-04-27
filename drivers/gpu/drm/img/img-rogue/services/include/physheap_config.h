/*************************************************************************/ /*!
@File           physheap_config.h
@Title          Physical heap Config API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Physical heap configs are created in the system layer and
                stored against each device node for use in the Services Server
                common layer.
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

#ifndef PHYSHEAP_CONFIG_H
#define PHYSHEAP_CONFIG_H

#include "img_types.h"
#include "pvrsrv_memallocflags.h"
#include "pvrsrv_memalloc_physheap.h"

typedef IMG_UINT32 PHYS_HEAP_USAGE_FLAGS;

#define PHYS_HEAP_USAGE_GPU_LOCAL      (1<<PVRSRV_PHYS_HEAP_GPU_LOCAL)
#define PHYS_HEAP_USAGE_CPU_LOCAL      (1<<PVRSRV_PHYS_HEAP_CPU_LOCAL)
#define PHYS_HEAP_USAGE_FW_MAIN        (1<<PVRSRV_PHYS_HEAP_FW_MAIN)
#define PHYS_HEAP_USAGE_FW_CONFIG      (1<<PVRSRV_PHYS_HEAP_FW_CONFIG)
#define PHYS_HEAP_USAGE_EXTERNAL       (1<<PVRSRV_PHYS_HEAP_EXTERNAL)
#define PHYS_HEAP_USAGE_GPU_PRIVATE    (1<<PVRSRV_PHYS_HEAP_GPU_PRIVATE)
#define PHYS_HEAP_USAGE_GPU_COHERENT   (1<<PVRSRV_PHYS_HEAP_GPU_COHERENT)
#define PHYS_HEAP_USAGE_GPU_SECURE     (1<<PVRSRV_PHYS_HEAP_GPU_SECURE)
#define PHYS_HEAP_USAGE_FW_CODE        (1<<PVRSRV_PHYS_HEAP_FW_CODE)
#define PHYS_HEAP_USAGE_FW_PRIV_DATA   (1<<PVRSRV_PHYS_HEAP_FW_PRIV_DATA)
#define PHYS_HEAP_USAGE_WRAP           (1<<30)
#define PHYS_HEAP_USAGE_DISPLAY        (1<<31)

typedef void (*CpuPAddrToDevPAddr)(IMG_HANDLE hPrivData,
                                   IMG_UINT32 ui32NumOfAddr,
                                   IMG_DEV_PHYADDR *psDevPAddr,
                                   IMG_CPU_PHYADDR *psCpuPAddr);

typedef void (*DevPAddrToCpuPAddr)(IMG_HANDLE hPrivData,
                                   IMG_UINT32 ui32NumOfAddr,
                                   IMG_CPU_PHYADDR *psCpuPAddr,
                                   IMG_DEV_PHYADDR *psDevPAddr);

/*! Structure used to hold function pointers used for run-time physical address
 * translation by Services. Gives flexibility to allow the CPU and GPU to see
 * the same pool of physical RAM and different physical bus addresses.
 * Both fields must be valid functions even if the conversion is simple.
 */
typedef struct _PHYS_HEAP_FUNCTIONS_
{
	/*! Translate CPU physical address to device physical address */
	CpuPAddrToDevPAddr	pfnCpuPAddrToDevPAddr;
	/*! Translate device physical address to CPU physical address */
	DevPAddrToCpuPAddr	pfnDevPAddrToCpuPAddr;
} PHYS_HEAP_FUNCTIONS;

/*! Type conveys the class of physical heap to instantiate within Services
 * for the physical pool of memory. */
typedef enum _PHYS_HEAP_TYPE_
{
	PHYS_HEAP_TYPE_UNKNOWN = 0,     /*!< Not a valid value for any config */
	PHYS_HEAP_TYPE_UMA,             /*!< Heap represents OS managed physical memory heap
	                                     i.e. system RAM. Unified Memory Architecture
	                                     physmem_osmem PMR factory */
	PHYS_HEAP_TYPE_LMA,             /*!< Heap represents physical memory pool managed by
	                                     Services i.e. carve out from system RAM or local
	                                     card memory. Local Memory Architecture
	                                     physmem_lma PMR factory */
	PHYS_HEAP_TYPE_DMA,             /*!< Heap represents a physical memory pool managed by
	                                     Services, alias of LMA and is only used on
	                                     VZ non-native system configurations for
	                                     a heap used for PHYS_HEAP_USAGE_FW_MAIN tagged
	                                     buffers */
#if defined(SUPPORT_WRAP_EXTMEMOBJECT)
	PHYS_HEAP_TYPE_WRAP,            /*!< Heap used to group UM buffers given
	                                     to Services. Integrity OS port only. */
#endif
} PHYS_HEAP_TYPE;

/*! Structure used to describe a physical Heap supported by a system. A
 * system layer module can declare multiple physical heaps for different
 * purposes. At a minimum a system must provide one physical heap tagged for
 * PHYS_HEAP_USAGE_GPU_LOCAL use.
 * A heap represents a discrete pool of physical memory and how it is managed,
 * as well as associating other properties and address translation logic.
 * The structure fields sStartAddr, sCardBase and uiSize must be given valid
 * values for LMA and DMA physical heaps types.
 */
typedef struct _PHYS_HEAP_CONFIG_
{
	PHYS_HEAP_TYPE        eType;                /*!< Class of heap and PMR factory used */
	IMG_CHAR*             pszPDumpMemspaceName; /*!< Name given to the heap's symbolic memory
	                                                 space in a PDUMP enabled driver */
	PHYS_HEAP_FUNCTIONS*  psMemFuncs;           /*!< Physical address translation functions */

	IMG_CPU_PHYADDR       sStartAddr;           /*!< CPU Physical base address of memory region */
	IMG_DEV_PHYADDR       sCardBase;            /*!< Device physical base address of memory
	                                                 region as seen from the PoV of the GPU */
	IMG_UINT64            uiSize;               /*!< Size of memory region in bytes */

	IMG_HANDLE            hPrivData;            /*!< System layer private data shared with
	                                                 psMemFuncs */

	PHYS_HEAP_USAGE_FLAGS ui32UsageFlags;       /*!< Supported uses flags, conveys the type of
	                                                 buffers the physical heap can be used for */
} PHYS_HEAP_CONFIG;

#endif
