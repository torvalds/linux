/*************************************************************************/ /*!
@File
@Title          Physmem header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for common entry point for creation of RAM backed PMR's
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

#ifndef SRVSRV_PHYSMEM_H
#define SRVSRV_PHYSMEM_H

/* include/ */
#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "connection_server.h"

/* services/server/include/ */
#include "pmr.h"
#include "pmr_impl.h"

/* Valid values for TC_MEMORY_CONFIG configuration option */
#define TC_MEMORY_LOCAL			(1)
#define TC_MEMORY_HOST			(2)
#define TC_MEMORY_HYBRID		(3)

/* Valid values for the PLATO_MEMORY_CONFIG configuration option */
#define PLATO_MEMORY_LOCAL		(1)
#define PLATO_MEMORY_HOST		(2)
#define PLATO_MEMORY_HYBRID		(3)

/*************************************************************************/ /*!
@Function       DevPhysMemAlloc
@Description    Allocate memory from device specific heaps directly.
@Input          psDevNode             device node to operate on
@Input          ui32MemSize           Size of the memory to be allocated
@Input          u8Value               Value to be initialised to.
@Input          bInitPage             Flag to control initialisation
@Input          pszDevSpace           PDUMP memory space in which the
                                        allocation is to be done
@Input          pszSymbolicAddress    Symbolic name of the allocation
@Input          phHandlePtr           PDUMP handle to the allocation
@Input          uiPid                 PID of the process owning the allocation
                                        (or PVR_SYS_ALLOC_PID if the allocation
                                        belongs to the driver)
@Output         hMemHandle            Handle to the allocated memory
@Output         psDevPhysAddr         Device Physical address of allocated
                                        page
@Return         PVRSRV_OK if the allocation is successful
*/ /**************************************************************************/
PVRSRV_ERROR
DevPhysMemAlloc(PVRSRV_DEVICE_NODE *psDevNode,
                IMG_UINT32 ui32MemSize,
                IMG_UINT32 ui32Log2Align,
                const IMG_UINT8 u8Value,
                IMG_BOOL bInitPage,
#if defined(PDUMP)
                const IMG_CHAR *pszDevSpace,
                const IMG_CHAR *pszSymbolicAddress,
                IMG_HANDLE *phHandlePtr,
#endif
                IMG_PID uiPid,
                IMG_HANDLE hMemHandle,
                IMG_DEV_PHYADDR *psDevPhysAddr);

/*************************************************************************/ /*!
@Function       DevPhysMemFree
@Description    Free memory to device specific heaps directly.
@Input          psDevNode             device node to operate on
@Input          hPDUMPMemHandle       Pdump handle to allocated memory
@Input          hMemHandle            Devmem handle to allocated memory
@Return         None
*/ /**************************************************************************/
void
DevPhysMemFree(PVRSRV_DEVICE_NODE *psDevNode,
#if defined(PDUMP)
               IMG_HANDLE hPDUMPMemHandle,
#endif
               IMG_HANDLE hMemHandle);

/*
 * PhysmemNewRamBackedPMR
 *
 * This function will create a RAM backed PMR using the device specific
 * callback, this allows control at a per-devicenode level to select the
 * memory source thus supporting mixed UMA/LMA systems.
 *
 * The size must be a multiple of page size. The page size is specified in
 * log2. It should be regarded as a minimum contiguity of which the
 * resulting memory must be a multiple. It may be that this should be a fixed
 * number. It may be that the allocation size needs to be a multiple of some
 * coarser "page size" than that specified in the page size argument.
 * For example, take an OS whose page granularity is a fixed 16kB, but the
 * caller requests memory in page sizes of 4kB. The request can be satisfied
 * if and only if the SIZE requested is a multiple of 16kB. If the arguments
 * supplied are such that this OS cannot grant the request,
 * PVRSRV_ERROR_INVALID_PARAMS will be returned.
 *
 * The caller should supply storage of a pointer. Upon successful return a
 * PMR object will have been created and a pointer to it returned in the
 * PMROut argument.
 *
 * A PMR successfully created should be destroyed with PhysmemUnrefPMR.
 *
 * Note that this function may cause memory allocations and on some operating
 * systems this may cause scheduling events, so it is important that this
 * function be called with interrupts enabled and in a context where
 * scheduling events and memory allocations are permitted.
 *
 * The flags may be used by the implementation to change its behaviour if
 * required. The flags will also be stored in the PMR as immutable metadata
 * and returned to mmu_common when it asks for it.
 *
 * The PID specified is used to tie this allocation to the process context
 * that the allocation is made on behalf of.
 */
PVRSRV_ERROR
PhysmemNewRamBackedPMR(CONNECTION_DATA * psConnection,
                       PVRSRV_DEVICE_NODE *psDevNode,
                       IMG_DEVMEM_SIZE_T uiSize,
                       IMG_UINT32 ui32NumPhysChunks,
                       IMG_UINT32 ui32NumVirtChunks,
                       IMG_UINT32 *pui32MappingTable,
                       IMG_UINT32 uiLog2PageSize,
                       PVRSRV_MEMALLOCFLAGS_T uiFlags,
                       IMG_UINT32 uiAnnotationLength,
                       const IMG_CHAR *pszAnnotation,
                       IMG_PID uiPid,
                       PMR **ppsPMROut,
                       IMG_UINT32 ui32PDumpFlags,
                       PVRSRV_MEMALLOCFLAGS_T *puiPMRFlags);

PVRSRV_ERROR
PhysmemNewRamBackedPMR_direct(CONNECTION_DATA * psConnection,
							  PVRSRV_DEVICE_NODE *psDevNode,
							  IMG_DEVMEM_SIZE_T uiSize,
							  IMG_UINT32 ui32NumPhysChunks,
							  IMG_UINT32 ui32NumVirtChunks,
							  IMG_UINT32 *pui32MappingTable,
							  IMG_UINT32 uiLog2PageSize,
							  PVRSRV_MEMALLOCFLAGS_T uiFlags,
							  IMG_UINT32 uiAnnotationLength,
							  const IMG_CHAR *pszAnnotation,
							  IMG_PID uiPid,
							  PMR **ppsPMROut,
							  IMG_UINT32 ui32PDumpFlags,
							  PVRSRV_MEMALLOCFLAGS_T *puiPMRFlags);

/*************************************************************************/ /*!
@Function       PhysmemImportPMR
@Description    Import PMR a previously exported PMR
@Input          psPMRExport           The exported PMR token
@Input          uiPassword            Authorisation password
                                      for the PMR being imported
@Input          uiSize                Size of the PMR being imported
                                      (for verification)
@Input          uiLog2Contig          Log2 continuity of the PMR being
                                      imported (for verification)
@Output         ppsPMR                The imported PMR
@Return         PVRSRV_ERROR_PMR_NOT_PERMITTED if not for the same device
                PVRSRV_ERROR_PMR_WRONG_PASSWORD_OR_STALE_PMR if password incorrect
                PVRSRV_ERROR_PMR_MISMATCHED_ATTRIBUTES if size or contiguity incorrect
                PVRSRV_OK if successful
*/ /**************************************************************************/
PVRSRV_ERROR
PhysmemImportPMR(CONNECTION_DATA *psConnection,
                 PVRSRV_DEVICE_NODE *psDevNode,
                 PMR_EXPORT *psPMRExport,
                 PMR_PASSWORD_T uiPassword,
                 PMR_SIZE_T uiSize,
                 PMR_LOG2ALIGN_T uiLog2Contig,
                 PMR **ppsPMR);

/*************************************************************************/ /*!
@Function       PVRSRVGetDefaultPhysicalHeapKM
@Description    For the specified device, get the physical heap used for
                allocations when the PVRSRV_PHYS_HEAP_DEFAULT
                physical heap hint is set in memalloc flags.
@Output         peHeap                 Default Heap return value
@Return         PVRSRV_OK if successful
*/ /**************************************************************************/
PVRSRV_ERROR
PVRSRVGetDefaultPhysicalHeapKM(CONNECTION_DATA *psConnection,
                               PVRSRV_DEVICE_NODE *psDevNode,
                               PVRSRV_PHYS_HEAP *peHeap);

/*************************************************************************/ /*!
@Function       PVRSRVPhysHeapGetMemInfoKM
@Description    Get the memory usage statistics for a given physical heap ID
@Input          ui32PhysHeapCount      Physical Heap count
@Input          paePhysHeapID          Array of Physical Heap ID's
@Output         paPhysHeapMemStats     Buffer to hold the memory statistics
@Return         PVRSRV_OK if successful
*/ /**************************************************************************/
PVRSRV_ERROR
PVRSRVPhysHeapGetMemInfoKM(CONNECTION_DATA *psConnection,
                           PVRSRV_DEVICE_NODE *psDevNode,
                           IMG_UINT32 ui32PhysHeapCount,
                           PVRSRV_PHYS_HEAP *paePhysHeapID,
                           PHYS_HEAP_MEM_STATS *paPhysHeapMemStats);

#endif /* SRVSRV_PHYSMEM_H */
