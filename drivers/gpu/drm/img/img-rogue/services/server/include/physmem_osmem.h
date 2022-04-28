/*************************************************************************/ /*!
@File           physmem_osmem.h
@Title          OS memory PMR factory API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of Services memory management.  This file defines the
                OS memory PMR factory API that must be defined so that the
                common & device layer code in the Services Server can allocate
                new PMRs back with pages from the OS page allocator. Applicable
                for UMA based platforms, such platforms must implement this API
                in the OS Porting layer, in the "env" directory for that
                system.

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

#ifndef PHYSMEM_OSMEM_H
#define PHYSMEM_OSMEM_H

/* include/ */
#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"

/* services/server/include/ */
#include "pmr.h"
#include "pmr_impl.h"
#include "connection_server.h"

/*************************************************************************/ /*!
@Function       PhysmemNewOSRamBackedPMR
@Description    Rogue Services will call this function to allocate GPU device
                memory from the PMR factory supported by the OS DDK port. This
                factory typically obtains physical memory from the kernel/OS
                API that allocates memory from the default heap of shared
                system memory available on the platform. The allocated memory
                must be page-aligned and be a whole number of pages.
                After allocating the required memory, the implementation must
                then call PMRCreatePMR() to obtain the PMR structure that
                describes this allocation to the upper layers of the Services.
                memory management sub-system.
                NB. Implementation of this function is mandatory. If shared
                system memory is not to be used in the OS port then the
                implementation must return PVRSRV_ERROR_NOT_SUPPORTED.

@Input          psPhysHeap       the phys heap
@Input          psConnection     the connection to the originator process
@Input          uiSize           the size of the allocation
                                 (must be a multiple of page size)
@Input          uiChunkSize      when sparse allocations are requested,
                                 this is the allocated chunk size.
                                 For regular allocations, this will be
                                 the same as uiSize.
                                 (must be a multiple of page size)
@Input          ui32NumPhysChunks  when sparse allocations are requested,
                                   this is the number of physical chunks
                                   to be allocated.
                                   For regular allocations, this will be 1.
@Input          ui32NumVirtChunks  when sparse allocations are requested,
                                   this is the number of virtual chunks
                                   covering the sparse allocation.
                                   For regular allocations, this will be 1.
@Input          pui32MappingTable  when sparse allocations are requested,
                                   this is the list of the indices of
                                   each physically-backed virtual chunk
                                   For regular allocations, this will
                                   be NULL.
@Input          uiLog2PageSize   the physical pagesize in log2(bytes).
@Input          uiFlags          the allocation flags.
@Input          pszAnnotation    string describing the PMR (for debug).
                                 This should be passed into the function
                                 PMRCreatePMR().
@Input          uiPid            The process ID that this allocation should
                                 be associated with.
@Output         ppsPMROut        pointer to the PMR created for the
                                 new allocation
@Input          ui32PDumpFlags   the pdump flags.
@Return         PVRSRV_OK on success, a failure code otherwise.
*/ /**************************************************************************/
PVRSRV_ERROR
PhysmemNewOSRamBackedPMR(PHYS_HEAP *psPhysHeap,
						 CONNECTION_DATA *psConnection,
                         IMG_DEVMEM_SIZE_T uiSize,
                         IMG_DEVMEM_SIZE_T uiChunkSize,
                         IMG_UINT32 ui32NumPhysChunks,
                         IMG_UINT32 ui32NumVirtChunks,
                         IMG_UINT32 *pui32MappingTable,
                         IMG_UINT32 uiLog2PageSize,
                         PVRSRV_MEMALLOCFLAGS_T uiFlags,
                         const IMG_CHAR *pszAnnotation,
                         IMG_PID uiPid,
                         PMR **ppsPMROut,
                         IMG_UINT32 ui32PDumpFlags);

#endif /* PHYSMEM_OSMEM_H */
