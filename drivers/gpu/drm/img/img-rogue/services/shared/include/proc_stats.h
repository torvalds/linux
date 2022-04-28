/*************************************************************************/ /*!
@File
@Title          Process and driver statistic definitions
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

#ifndef PROC_STATS_H
#define PROC_STATS_H

/* X-Macro for Process stat keys */
#define PVRSRV_PROCESS_STAT_KEY \
	X(PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS, "Connections") \
	X(PVRSRV_PROCESS_STAT_TYPE_MAX_CONNECTIONS, "ConnectionsMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_RC_OOMS, "RenderContextOutOfMemoryEvents") \
	X(PVRSRV_PROCESS_STAT_TYPE_RC_PRS, "RenderContextPartialRenders") \
	X(PVRSRV_PROCESS_STAT_TYPE_RC_GROWS, "RenderContextGrows") \
	X(PVRSRV_PROCESS_STAT_TYPE_RC_PUSH_GROWS, "RenderContextPushGrows") \
	X(PVRSRV_PROCESS_STAT_TYPE_RC_TA_STORES, "RenderContextTAStores") \
	X(PVRSRV_PROCESS_STAT_TYPE_RC_3D_STORES, "RenderContext3DStores") \
	X(PVRSRV_PROCESS_STAT_TYPE_RC_CDM_STORES, "RenderContextCDMStores") \
	X(PVRSRV_PROCESS_STAT_TYPE_RC_TDM_STORES, "RenderContextTDMStores") \
	X(PVRSRV_PROCESS_STAT_TYPE_ZSBUFFER_REQS_BY_APP, "ZSBufferRequestsByApp") \
	X(PVRSRV_PROCESS_STAT_TYPE_ZSBUFFER_REQS_BY_FW, "ZSBufferRequestsByFirmware") \
	X(PVRSRV_PROCESS_STAT_TYPE_FREELIST_GROW_REQS_BY_APP, "FreeListGrowRequestsByApp") \
	X(PVRSRV_PROCESS_STAT_TYPE_FREELIST_GROW_REQS_BY_FW, "FreeListGrowRequestsByFirmware") \
	X(PVRSRV_PROCESS_STAT_TYPE_FREELIST_PAGES_INIT, "FreeListInitialPages") \
	X(PVRSRV_PROCESS_STAT_TYPE_FREELIST_MAX_PAGES, "FreeListMaxPages") \
	X(PVRSRV_PROCESS_STAT_TYPE_KMALLOC, "MemoryUsageKMalloc") \
	X(PVRSRV_PROCESS_STAT_TYPE_KMALLOC_MAX, "MemoryUsageKMallocMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_VMALLOC, "MemoryUsageVMalloc") \
	X(PVRSRV_PROCESS_STAT_TYPE_VMALLOC_MAX, "MemoryUsageVMallocMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA, "MemoryUsageAllocPTMemoryUMA") \
	X(PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA_MAX, "MemoryUsageAllocPTMemoryUMAMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA, "MemoryUsageVMapPTUMA") \
	X(PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA_MAX, "MemoryUsageVMapPTUMAMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA, "MemoryUsageAllocPTMemoryLMA") \
	X(PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA_MAX, "MemoryUsageAllocPTMemoryLMAMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA, "MemoryUsageIORemapPTLMA") \
	X(PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA_MAX, "MemoryUsageIORemapPTLMAMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES, "MemoryUsageAllocGPUMemLMA") \
	X(PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES_MAX, "MemoryUsageAllocGPUMemLMAMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES, "MemoryUsageAllocGPUMemUMA") \
	X(PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES_MAX, "MemoryUsageAllocGPUMemUMAMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES, "MemoryUsageMappedGPUMemUMA/LMA") \
	X(PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES_MAX, "MemoryUsageMappedGPUMemUMA/LMAMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT, "MemoryUsageDmaBufImport") \
	X(PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT_MAX, "MemoryUsageDmaBufImportMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_TOTAL, "MemoryUsageTotal") \
	X(PVRSRV_PROCESS_STAT_TYPE_TOTAL_MAX, "MemoryUsageTotalMax") \
	X(PVRSRV_PROCESS_STAT_TYPE_OOM_VIRTMEM_COUNT, "MemoryOOMCountDeviceVirtual") \
	X(PVRSRV_PROCESS_STAT_TYPE_OOM_PHYSMEM_COUNT, "MemoryOOMCountPhysicalHeap") \
	X(PVRSRV_PROCESS_STAT_TYPE_INVALID_VIRTMEM, "MemoryOOMCountDeviceVirtualAtAddress")


/* X-Macro for Driver stat keys */
#define PVRSRV_DRIVER_STAT_KEY \
	X(PVRSRV_DRIVER_STAT_TYPE_KMALLOC, "MemoryUsageKMalloc") \
	X(PVRSRV_DRIVER_STAT_TYPE_KMALLOC_MAX, "MemoryUsageKMallocMax") \
	X(PVRSRV_DRIVER_STAT_TYPE_VMALLOC, "MemoryUsageVMalloc") \
	X(PVRSRV_DRIVER_STAT_TYPE_VMALLOC_MAX, "MemoryUsageVMallocMax") \
	X(PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_UMA, "MemoryUsageAllocPTMemoryUMA") \
	X(PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_UMA_MAX, "MemoryUsageAllocPTMemoryUMAMax") \
	X(PVRSRV_DRIVER_STAT_TYPE_VMAP_PT_UMA, "MemoryUsageVMapPTUMA") \
	X(PVRSRV_DRIVER_STAT_TYPE_VMAP_PT_UMA_MAX, "MemoryUsageVMapPTUMAMax") \
	X(PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_LMA, "MemoryUsageAllocPTMemoryLMA") \
	X(PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_LMA_MAX, "MemoryUsageAllocPTMemoryLMAMax") \
	X(PVRSRV_DRIVER_STAT_TYPE_IOREMAP_PT_LMA, "MemoryUsageIORemapPTLMA") \
	X(PVRSRV_DRIVER_STAT_TYPE_IOREMAP_PT_LMA_MAX, "MemoryUsageIORemapPTLMAMax") \
	X(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_LMA, "MemoryUsageAllocGPUMemLMA") \
	X(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_LMA_MAX, "MemoryUsageAllocGPUMemLMAMax") \
	X(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA, "MemoryUsageAllocGPUMemUMA") \
	X(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA_MAX, "MemoryUsageAllocGPUMemUMAMax") \
	X(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA_POOL, "MemoryUsageAllocGPUMemUMAPool") \
	X(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA_POOL_MAX, "MemoryUsageAllocGPUMemUMAPoolMax") \
	X(PVRSRV_DRIVER_STAT_TYPE_MAPPED_GPUMEM_UMA_LMA, "MemoryUsageMappedGPUMemUMA/LMA") \
	X(PVRSRV_DRIVER_STAT_TYPE_MAPPED_GPUMEM_UMA_LMA_MAX, "MemoryUsageMappedGPUMemUMA/LMAMax") \
	X(PVRSRV_DRIVER_STAT_TYPE_DMA_BUF_IMPORT, "MemoryUsageDmaBufImport") \
	X(PVRSRV_DRIVER_STAT_TYPE_DMA_BUF_IMPORT_MAX, "MemoryUsageDmaBufImportMax")


typedef enum {
#define X(stat_type, stat_str) stat_type,
	PVRSRV_PROCESS_STAT_KEY
#undef X
	PVRSRV_PROCESS_STAT_TYPE_COUNT
}PVRSRV_PROCESS_STAT_TYPE;

typedef enum {
#define X(stat_type, stat_str) stat_type,
	PVRSRV_DRIVER_STAT_KEY
#undef X
	PVRSRV_DRIVER_STAT_TYPE_COUNT
}PVRSRV_DRIVER_STAT_TYPE;

extern const IMG_CHAR *const pszProcessStatType[];

extern const IMG_CHAR *const pszDriverStatType[];

#endif // PROC_STATS_H
