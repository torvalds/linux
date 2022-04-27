/*************************************************************************/ /*!
@File
@Title          X Device Memory Management core internal
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Internal interface for extended device memory management.
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

#ifndef DEVICEMEMX_H
#define DEVICEMEMX_H

#include "img_types.h"
#include "devicemem_typedefs.h"
#include "devicemem_utils.h"
#include "pdumpdefs.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "osfunc.h"

/* DevmemXAllocPhysical()
 *
 * Allocate physical device memory and return a physical
 * descriptor for it.
 */
PVRSRV_ERROR
DevmemXAllocPhysical(DEVMEM_CONTEXT *psCtx,
                    IMG_UINT32 uiNumPages,
                    IMG_UINT32 uiLog2PageSize,
                    PVRSRV_MEMALLOCFLAGS_T uiFlags,
                    const IMG_CHAR *pszText,
                    DEVMEMX_PHYSDESC **ppsPhysDesc);

/* DevmemXReleasePhysical()
 *
 * Removes a physical device allocation if all references
 * to it are dropped, otherwise just decreases the refcount.
 */
void
DevmemXReleasePhysical(DEVMEMX_PHYSDESC *psPhysDesc);

/* DevmemAllocVirtualAddr()
 *
 * Reserve a requested device virtual range and return
 * a virtual descriptor for it.
 */
IMG_INTERNAL PVRSRV_ERROR
DevmemXAllocVirtualAddr(DEVMEM_HEAP* hHeap,
                   IMG_UINT32 uiNumPages,
                   PVRSRV_MEMALLOCFLAGS_T uiFlags,
                   const IMG_CHAR *pszText,
                   IMG_DEV_VIRTADDR sVirtAddr,
                   DEVMEMX_VIRTDESC **ppsVirtDesc);

/* DevmemAllocVirtual()
 *
 * Allocate and reserve a device virtual range and return
 * a virtual descriptor for it.
 */
PVRSRV_ERROR
DevmemXAllocVirtual(DEVMEM_HEAP* hHeap,
                   IMG_UINT32 uiNumPages,
                   PVRSRV_MEMALLOCFLAGS_T uiFlags,
                   const IMG_CHAR *pszText,
                   DEVMEMX_VIRTDESC **ppsVirtDesc,
                   IMG_DEV_VIRTADDR *psVirtAddr);

/* DevmemXFreeVirtual()
 *
 * Removes a device virtual range if all mappings on it
 * have been removed.
 */
PVRSRV_ERROR
DevmemXFreeVirtual(DEVMEMX_VIRTDESC *psVirtDesc);

/* DevmemXMapVirtualRange()
 *
 * Map memory from a physical descriptor into a virtual range.
 */
PVRSRV_ERROR
DevmemXMapVirtualRange(IMG_UINT32 ui32PageCount,
                      DEVMEMX_PHYSDESC *psPhysDesc,
                      IMG_UINT32 ui32PhysOffset,
                      DEVMEMX_VIRTDESC *psVirtDesc,
                      IMG_UINT32 ui32VirtOffset);

/* DevmemXUnmapVirtualRange()
 *
 * Unmap pages from a device virtual range.
 */
PVRSRV_ERROR
DevmemXUnmapVirtualRange(IMG_UINT32 ui32PageCount,
                        DEVMEMX_VIRTDESC *psVirtDesc,
                        IMG_UINT32 ui32VirtPgOffset);

/* DevmemXMapPhysicalToCPU()
 *
 * Map a full physical descriptor to CPU space.
 */
PVRSRV_ERROR
DevmemXMapPhysicalToCPU(DEVMEMX_PHYSDESC *psMemAllocPhys,
                       IMG_CPU_VIRTADDR *psVirtAddr);

/* DevmemXUnmapPhysicalToCPU()
 *
 * Remove the CPU mapping from the descriptor.
 */
PVRSRV_ERROR
DevmemXUnmapPhysicalToCPU(DEVMEMX_PHYSDESC *psMemAllocPhys);

/* DevmemXReacquireCpuVirtAddr()
 *
 * Reacquire the CPU mapping by incrementing the refcount.
 */
void
DevmemXReacquireCpuVirtAddr(DEVMEMX_PHYSDESC *psPhysDesc,
                            void **ppvCpuVirtAddr);

/* DevmemXReleaseCpuVirtAddr()
 *
 * Release CPU mapping by decrementing the refcount.
 */
void
DevmemXReleaseCpuVirtAddr(DEVMEMX_PHYSDESC *psPhysDesc);

/* DevmemXCreateDevmemMemDescVA()
 *
 * (Deprecated)
 *
 * Create a devmem memdesc from a virtual address.
 * Always destroy with DevmemXFreeDevmemMemDesc().
 */

PVRSRV_ERROR
DevmemXCreateDevmemMemDescVA(const IMG_DEV_VIRTADDR sVirtualAddress,
                             DEVMEM_MEMDESC **ppsMemDesc);

/* DevmemXCreateDevmemMemDesc()
 *
 * Create a devmem memdesc from a physical and
 * virtual descriptor.
 * Always destroy with DevmemXFreeDevmemMemDesc().
 */

PVRSRV_ERROR
DevmemXCreateDevmemMemDesc(DEVMEMX_PHYSDESC *psPhysDesc,
                           DEVMEMX_VIRTDESC *psVirtDesc,
                           DEVMEM_MEMDESC **ppsMemDesc);

/* DevmemXFreeDevmemMemDesc()
 *
 * Free the memdesc again. Has no impact on the underlying
 * physical and virtual descriptors.
 */
PVRSRV_ERROR
DevmemXFreeDevmemMemDesc(DEVMEM_MEMDESC *psMemDesc);

PVRSRV_ERROR
DevmemXFlagCompatibilityCheck(PVRSRV_MEMALLOCFLAGS_T uiPhysFlags,
                              PVRSRV_MEMALLOCFLAGS_T uiVirtFlags);

PVRSRV_ERROR
DevmemXPhysDescAlloc(DEVMEMX_PHYSDESC **ppsPhysDesc);

void
DevmemXPhysDescInit(DEVMEMX_PHYSDESC *psPhysDesc,
                    IMG_HANDLE hPMR,
                    IMG_UINT32 uiNumPages,
                    IMG_UINT32 uiLog2PageSize,
                    PVRSRV_MEMALLOCFLAGS_T uiFlags,
                    IMG_HANDLE hBridge);

void
DevmemXPhysDescFree(DEVMEMX_PHYSDESC *psPhysDesc);

void
DevmemXPhysDescAcquire(DEVMEMX_PHYSDESC *psPhysDesc,
                       IMG_UINT32 uiAcquireCount);
void
DevmemXPhysDescRelease(DEVMEMX_PHYSDESC *psPhysDesc,
                       IMG_UINT32 uiReleaseCount);

#if !defined(__KERNEL__)
IMG_INTERNAL PVRSRV_ERROR
DevmemXGetImportUID(DEVMEMX_PHYSDESC *psMemDescPhys,
                    IMG_UINT64       *pui64UID);
#endif

#endif /* DEVICEMEMX_H */
