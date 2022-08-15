/**************************************************************************/ /*!
@File           physmem_dmabuf.h
@Title          Header for dmabuf PMR factory
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management. This module is responsible for
                implementing the function callbacks importing Ion allocations
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
*/ /***************************************************************************/

#if !defined(PHYSMEM_DMABUF_H)
#define PHYSMEM_DMABUF_H

#include <linux/dma-buf.h>

#if defined(__KERNEL__) && defined(__linux__) && !defined(__GENKSYMS__)
#define __pvrsrv_defined_struct_enum__
#include <services_kernel_client.h>
#endif

#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "connection_server.h"

#include "pmr.h"

typedef PVRSRV_ERROR (*PFN_DESTROY_DMABUF_PMR)(PHYS_HEAP *psHeap,
                                               struct dma_buf_attachment *psAttachment);

PVRSRV_ERROR
PhysmemCreateNewDmaBufBackedPMR(PHYS_HEAP *psHeap,
                                struct dma_buf_attachment *psAttachment,
                                PFN_DESTROY_DMABUF_PMR pfnDestroy,
                                PVRSRV_MEMALLOCFLAGS_T uiFlags,
                                IMG_DEVMEM_SIZE_T uiChunkSize,
                                IMG_UINT32 ui32NumPhysChunks,
                                IMG_UINT32 ui32NumVirtChunks,
                                IMG_UINT32 *pui32MappingTable,
		                        IMG_UINT32 ui32NameSize,
		                        const IMG_CHAR pszName[DEVMEM_ANNOTATION_MAX_LEN],
                                PMR **ppsPMRPtr);

struct dma_buf *
PhysmemGetDmaBuf(PMR *psPMR);

PVRSRV_ERROR
PhysmemExportDmaBuf(CONNECTION_DATA *psConnection,
                    PVRSRV_DEVICE_NODE *psDevNode,
                    PMR *psPMR,
                    IMG_INT *piFd);

PVRSRV_ERROR
PhysmemImportDmaBuf(CONNECTION_DATA *psConnection,
                    PVRSRV_DEVICE_NODE *psDevNode,
                    IMG_INT fd,
                    PVRSRV_MEMALLOCFLAGS_T uiFlags,
                    IMG_UINT32 ui32NameSize,
                    const IMG_CHAR pszName[DEVMEM_ANNOTATION_MAX_LEN],
                    PMR **ppsPMRPtr,
                    IMG_DEVMEM_SIZE_T *puiSize,
                    IMG_DEVMEM_ALIGN_T *puiAlign);

PVRSRV_ERROR
PhysmemImportDmaBufLocked(CONNECTION_DATA *psConnection,
                          PVRSRV_DEVICE_NODE *psDevNode,
                          IMG_INT fd,
                          PVRSRV_MEMALLOCFLAGS_T uiFlags,
                          IMG_UINT32 ui32NameSize,
                          const IMG_CHAR pszName[DEVMEM_ANNOTATION_MAX_LEN],
                          PMR **ppsPMRPtr,
                          IMG_DEVMEM_SIZE_T *puiSize,
                          IMG_DEVMEM_ALIGN_T *puiAlign);

PVRSRV_ERROR
PhysmemImportSparseDmaBuf(CONNECTION_DATA *psConnection,
                          PVRSRV_DEVICE_NODE *psDevNode,
                          IMG_INT fd,
                          PVRSRV_MEMALLOCFLAGS_T uiFlags,
                          IMG_DEVMEM_SIZE_T uiChunkSize,
                          IMG_UINT32 ui32NumPhysChunks,
                          IMG_UINT32 ui32NumVirtChunks,
                          IMG_UINT32 *pui32MappingTable,
                          IMG_UINT32 ui32NameSize,
                          const IMG_CHAR pszName[DEVMEM_ANNOTATION_MAX_LEN],
                          PMR **ppsPMRPtr,
                          IMG_DEVMEM_SIZE_T *puiSize,
                          IMG_DEVMEM_ALIGN_T *puiAlign);

#endif /* !defined(PHYSMEM_DMABUF_H) */
