/*************************************************************************/ /*!
@File           info_page_km.c
@Title          Kernel/User space shared memory
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements general purpose shared memory between kernel driver
                and user mode.
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

#include "info_page_defs.h"
#include "info_page.h"
#include "pvrsrv.h"
#include "devicemem.h"
#include "pmr.h"

PVRSRV_ERROR InfoPageCreate(PVRSRV_DATA *psData)
{
    const PVRSRV_MEMALLOCFLAGS_T uiMemFlags = PVRSRV_MEMALLOCFLAG_CPU_READABLE |
                                              PVRSRV_MEMALLOCFLAG_CPU_CACHE_INCOHERENT |
                                              PVRSRV_MEMALLOCFLAG_CPU_UNCACHED |
                                              PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
                                              PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(CPU_LOCAL);
    PVRSRV_ERROR eError;

    PVR_ASSERT(psData != NULL);

    /* Allocate single page of memory for driver information page */
    eError = DevmemAllocateExportable(psData->psHostMemDeviceNode,
                                      OSGetPageSize(),
                                      OSGetPageSize(),
                                      OSGetPageShift(),
                                      uiMemFlags,
                                      "PVRSRVInfoPage",
                                      &psData->psInfoPageMemDesc);
    PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAllocateExportable", e0);

    eError =  DevmemAcquireCpuVirtAddr(psData->psInfoPageMemDesc,
                                       (void **) &psData->pui32InfoPage);
    PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAllocateExportable", e0);

    /* Look-up the memory descriptor PMR handle */
    eError = DevmemLocalGetImportHandle(psData->psInfoPageMemDesc,
                                        (void **) &psData->psInfoPagePMR);
    PVR_LOG_GOTO_IF_ERROR(eError, "DevmemLocalGetImportHandle", e0);

    eError = OSLockCreate(&psData->hInfoPageLock);
    PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", e0);

    /* Because the memory is allocated read only we need to explicitly set it to
     * 0. The reason for this is that if the memory is allocated with
     * ZERO_ON_ALLOC a WRITEABLE attribute is implicitly added to the flags (see
     * DevmemValidateParams()). */
    OSCachedMemSet(psData->pui32InfoPage, 0, OSGetPageSize());

    return PVRSRV_OK;

e0:
    InfoPageDestroy(psData);
    return eError;
}

void InfoPageDestroy(PVRSRV_DATA *psData)
{
    if (psData->psInfoPageMemDesc)
    {
        if (psData->pui32InfoPage != NULL)
        {
            DevmemReleaseCpuVirtAddr(psData->psInfoPageMemDesc);
            psData->pui32InfoPage = NULL;
        }

        DevmemFree(psData->psInfoPageMemDesc);
        psData->psInfoPageMemDesc = NULL;
    }

    if (psData->hInfoPageLock)
    {
        OSLockDestroy(psData->hInfoPageLock);
        psData->hInfoPageLock = NULL;
    }
}

PVRSRV_ERROR PVRSRVAcquireInfoPageKM(PMR **ppsPMR)
{
    PVRSRV_DATA *psData = PVRSRVGetPVRSRVData();

    PVR_LOG_RETURN_IF_FALSE(psData->psInfoPageMemDesc != NULL, "invalid MEMDESC"
                      " handle", PVRSRV_ERROR_INVALID_PARAMS);
    PVR_LOG_RETURN_IF_FALSE(psData->psInfoPagePMR != NULL, "invalid PMR handle",
                      PVRSRV_ERROR_INVALID_PARAMS);

    /* Copy the PMR import handle back */
    *ppsPMR = psData->psInfoPagePMR;

    /* Mark the PMR such that no layout changes can happen
     * This is a fixed layout created during early stages of
     * driver loading and shouldn't change later */
    PMR_SetLayoutFixed(psData->psInfoPagePMR, IMG_TRUE);

    return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVReleaseInfoPageKM(PMR *ppsPMR)
{
    /* Nothing to do here as PMR is singleton */
    PVR_UNREFERENCED_PARAMETER(ppsPMR);
    return PVRSRV_OK;
}
