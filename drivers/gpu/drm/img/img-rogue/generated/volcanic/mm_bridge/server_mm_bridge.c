/*******************************************************************************
@File
@Title          Server bridge for mm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for mm
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
*******************************************************************************/

#include <linux/uaccess.h>

#include "img_defs.h"

#include "pvrsrv_memalloc_physheap.h"
#include "devicemem.h"
#include "devicemem_server.h"
#include "pmr.h"
#include "devicemem_heapcfg.h"
#include "physmem.h"
#include "devicemem_utils.h"
#include "process_stats.h"

#include "common_mm_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#if defined(SUPPORT_RGX)
#include "rgx_bridge.h"
#endif
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>

static PVRSRV_ERROR ReleasePMRExport(void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);

	return PVRSRV_OK;
}

/* ***************************************************************************
 * Server-side bridge entry points
 */

static PVRSRV_ERROR _PMRExportPMRpsPMRExportIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PMRUnexportPMR((PMR_EXPORT *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgePMRExportPMR(IMG_UINT32 ui32DispatchTableEntry,
			 IMG_UINT8 * psPMRExportPMRIN_UI8,
			 IMG_UINT8 * psPMRExportPMROUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PMREXPORTPMR *psPMRExportPMRIN =
	    (PVRSRV_BRIDGE_IN_PMREXPORTPMR *) IMG_OFFSET_ADDR(psPMRExportPMRIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PMREXPORTPMR *psPMRExportPMROUT =
	    (PVRSRV_BRIDGE_OUT_PMREXPORTPMR *) IMG_OFFSET_ADDR(psPMRExportPMROUT_UI8, 0);

	IMG_HANDLE hPMR = psPMRExportPMRIN->hPMR;
	PMR *psPMRInt = NULL;
	PMR_EXPORT *psPMRExportInt = NULL;
	IMG_HANDLE hPMRExportInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psPMRExportPMROUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psPMRExportPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto PMRExportPMR_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psPMRExportPMROUT->eError =
	    PMRExportPMR(psPMRInt,
			 &psPMRExportInt,
			 &psPMRExportPMROUT->ui64Size,
			 &psPMRExportPMROUT->ui32Log2Contig, &psPMRExportPMROUT->ui64Password);
	/* Exit early if bridged call fails */
	if (unlikely(psPMRExportPMROUT->eError != PVRSRV_OK))
	{
		goto PMRExportPMR_exit;
	}

	/*
	 * For cases where we need a cross process handle we actually allocate two.
	 *
	 * The first one is a connection specific handle and it gets given the real
	 * release function. This handle does *NOT* get returned to the caller. It's
	 * purpose is to release any leaked resources when we either have a bad or
	 * abnormally terminated client. If we didn't do this then the resource
	 * wouldn't be freed until driver unload. If the resource is freed normally,
	 * this handle can be looked up via the cross process handle and then
	 * released accordingly.
	 *
	 * The second one is a cross process handle and it gets given a noop release
	 * function. This handle does get returned to the caller.
	 */

	/* Lock over handle creation. */
	LockHandle(psConnection->psProcessHandleBase->psHandleBase);

	psPMRExportPMROUT->eError =
	    PVRSRVAllocHandleUnlocked(psConnection->psProcessHandleBase->psHandleBase,
				      &hPMRExportInt, (void *)psPMRExportInt,
				      PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT,
				      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				      (PFN_HANDLE_RELEASE) & _PMRExportPMRpsPMRExportIntRelease);
	if (unlikely(psPMRExportPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);
		goto PMRExportPMR_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);

	/* Lock over handle creation. */
	LockHandle(KERNEL_HANDLE_BASE);
	psPMRExportPMROUT->eError = PVRSRVAllocHandleUnlocked(KERNEL_HANDLE_BASE,
							      &psPMRExportPMROUT->hPMRExport,
							      (void *)psPMRExportInt,
							      PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT,
							      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
							      (PFN_HANDLE_RELEASE) &
							      ReleasePMRExport);
	if (unlikely(psPMRExportPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(KERNEL_HANDLE_BASE);
		goto PMRExportPMR_exit;
	}
	/* Release now we have created handles. */
	UnlockHandle(KERNEL_HANDLE_BASE);

PMRExportPMR_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psPMRInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psPMRExportPMROUT->eError != PVRSRV_OK)
	{
		if (psPMRExportPMROUT->hPMRExport)
		{
			PVRSRV_ERROR eError;

			/* Lock over handle creation cleanup. */
			LockHandle(KERNEL_HANDLE_BASE);

			eError = PVRSRVDestroyHandleUnlocked(KERNEL_HANDLE_BASE,
							     (IMG_HANDLE) psPMRExportPMROUT->
							     hPMRExport,
							     PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
			if (unlikely((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY)))
			{
				PVR_DPF((PVR_DBG_ERROR,
					 "%s: %s", __func__, PVRSRVGetErrorString(eError)));
			}
			/* Releasing the handle should free/destroy/release the resource.
			 * This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Release now we have cleaned up creation handles. */
			UnlockHandle(KERNEL_HANDLE_BASE);

		}

		if (hPMRExportInt)
		{
			PVRSRV_ERROR eError;
			/* Lock over handle creation cleanup. */
			LockHandle(psConnection->psProcessHandleBase->psHandleBase);

			eError =
			    PVRSRVDestroyHandleUnlocked(psConnection->psProcessHandleBase->
							psHandleBase, hPMRExportInt,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
			if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
			{
				PVR_DPF((PVR_DBG_ERROR,
					 "%s: %s", __func__, PVRSRVGetErrorString(eError)));
			}
			/* Releasing the handle should free/destroy/release the resource.
			 * This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psPMRExportInt = NULL;
			/* Release now we have cleaned up creation handles. */
			UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);
		}

		if (psPMRExportInt)
		{
			LockHandle(KERNEL_HANDLE_BASE);
			PMRUnexportPMR(psPMRExportInt);
			UnlockHandle(KERNEL_HANDLE_BASE);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgePMRUnexportPMR(IMG_UINT32 ui32DispatchTableEntry,
			   IMG_UINT8 * psPMRUnexportPMRIN_UI8,
			   IMG_UINT8 * psPMRUnexportPMROUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PMRUNEXPORTPMR *psPMRUnexportPMRIN =
	    (PVRSRV_BRIDGE_IN_PMRUNEXPORTPMR *) IMG_OFFSET_ADDR(psPMRUnexportPMRIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PMRUNEXPORTPMR *psPMRUnexportPMROUT =
	    (PVRSRV_BRIDGE_OUT_PMRUNEXPORTPMR *) IMG_OFFSET_ADDR(psPMRUnexportPMROUT_UI8, 0);

	PMR_EXPORT *psPMRExportInt = NULL;
	IMG_HANDLE hPMRExportInt = NULL;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	/* Lock over handle destruction. */
	LockHandle(KERNEL_HANDLE_BASE);
	psPMRUnexportPMROUT->eError =
	    PVRSRVLookupHandleUnlocked(KERNEL_HANDLE_BASE,
				       (void **)&psPMRExportInt,
				       (IMG_HANDLE) psPMRUnexportPMRIN->hPMRExport,
				       PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT, IMG_FALSE);
	if (unlikely(psPMRUnexportPMROUT->eError != PVRSRV_OK))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psPMRUnexportPMROUT->eError)));
	}
	PVR_ASSERT(psPMRUnexportPMROUT->eError == PVRSRV_OK);

	/* Release now we have destroyed handles. */
	UnlockHandle(KERNEL_HANDLE_BASE);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psProcessHandleBase->psHandleBase);
	/*
	 * Find the connection specific handle that represents the same data
	 * as the cross process handle as releasing it will actually call the
	 * data's real release function (see the function where the cross
	 * process handle is allocated for more details).
	 */
	psPMRUnexportPMROUT->eError =
	    PVRSRVFindHandleUnlocked(psConnection->psProcessHandleBase->psHandleBase,
				     &hPMRExportInt,
				     psPMRExportInt, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	if (unlikely(psPMRUnexportPMROUT->eError != PVRSRV_OK))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psPMRUnexportPMROUT->eError)));
	}
	PVR_ASSERT(psPMRUnexportPMROUT->eError == PVRSRV_OK);

	psPMRUnexportPMROUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psProcessHandleBase->psHandleBase,
					      hPMRExportInt, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	if (unlikely((psPMRUnexportPMROUT->eError != PVRSRV_OK) &&
		     (psPMRUnexportPMROUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psPMRUnexportPMROUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psPMRUnexportPMROUT->eError)));
	}
	PVR_ASSERT((psPMRUnexportPMROUT->eError == PVRSRV_OK) ||
		   (psPMRUnexportPMROUT->eError == PVRSRV_ERROR_RETRY));
	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);

	/* Lock over handle destruction. */
	LockHandle(KERNEL_HANDLE_BASE);

	psPMRUnexportPMROUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(KERNEL_HANDLE_BASE,
					      (IMG_HANDLE) psPMRUnexportPMRIN->hPMRExport,
					      PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	if (unlikely((psPMRUnexportPMROUT->eError != PVRSRV_OK) &&
		     (psPMRUnexportPMROUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psPMRUnexportPMROUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psPMRUnexportPMROUT->eError)));
		UnlockHandle(KERNEL_HANDLE_BASE);
		goto PMRUnexportPMR_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(KERNEL_HANDLE_BASE);

PMRUnexportPMR_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRGetUID(IMG_UINT32 ui32DispatchTableEntry,
		      IMG_UINT8 * psPMRGetUIDIN_UI8,
		      IMG_UINT8 * psPMRGetUIDOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PMRGETUID *psPMRGetUIDIN =
	    (PVRSRV_BRIDGE_IN_PMRGETUID *) IMG_OFFSET_ADDR(psPMRGetUIDIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PMRGETUID *psPMRGetUIDOUT =
	    (PVRSRV_BRIDGE_OUT_PMRGETUID *) IMG_OFFSET_ADDR(psPMRGetUIDOUT_UI8, 0);

	IMG_HANDLE hPMR = psPMRGetUIDIN->hPMR;
	PMR *psPMRInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psPMRGetUIDOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psPMRGetUIDOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto PMRGetUID_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psPMRGetUIDOUT->eError = PMRGetUID(psPMRInt, &psPMRGetUIDOUT->ui64UID);

PMRGetUID_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psPMRInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static PVRSRV_ERROR _PMRMakeLocalImportHandlepsExtMemIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PMRUnmakeLocalImportHandle((PMR *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgePMRMakeLocalImportHandle(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psPMRMakeLocalImportHandleIN_UI8,
				     IMG_UINT8 * psPMRMakeLocalImportHandleOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PMRMAKELOCALIMPORTHANDLE *psPMRMakeLocalImportHandleIN =
	    (PVRSRV_BRIDGE_IN_PMRMAKELOCALIMPORTHANDLE *)
	    IMG_OFFSET_ADDR(psPMRMakeLocalImportHandleIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PMRMAKELOCALIMPORTHANDLE *psPMRMakeLocalImportHandleOUT =
	    (PVRSRV_BRIDGE_OUT_PMRMAKELOCALIMPORTHANDLE *)
	    IMG_OFFSET_ADDR(psPMRMakeLocalImportHandleOUT_UI8, 0);

	IMG_HANDLE hBuffer = psPMRMakeLocalImportHandleIN->hBuffer;
	PMR *psBufferInt = NULL;
	PMR *psExtMemInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psPMRMakeLocalImportHandleOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psBufferInt,
				       hBuffer,
				       PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE, IMG_TRUE);
	if (unlikely(psPMRMakeLocalImportHandleOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto PMRMakeLocalImportHandle_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psPMRMakeLocalImportHandleOUT->eError = PMRMakeLocalImportHandle(psBufferInt, &psExtMemInt);
	/* Exit early if bridged call fails */
	if (unlikely(psPMRMakeLocalImportHandleOUT->eError != PVRSRV_OK))
	{
		goto PMRMakeLocalImportHandle_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psProcessHandleBase->psHandleBase);

	psPMRMakeLocalImportHandleOUT->eError =
	    PVRSRVAllocHandleUnlocked(psConnection->psProcessHandleBase->psHandleBase,
				      &psPMRMakeLocalImportHandleOUT->hExtMem, (void *)psExtMemInt,
				      PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT,
				      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				      (PFN_HANDLE_RELEASE) &
				      _PMRMakeLocalImportHandlepsExtMemIntRelease);
	if (unlikely(psPMRMakeLocalImportHandleOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);
		goto PMRMakeLocalImportHandle_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);

PMRMakeLocalImportHandle_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psBufferInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hBuffer, PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psPMRMakeLocalImportHandleOUT->eError != PVRSRV_OK)
	{
		if (psExtMemInt)
		{
			LockHandle(KERNEL_HANDLE_BASE);
			PMRUnmakeLocalImportHandle(psExtMemInt);
			UnlockHandle(KERNEL_HANDLE_BASE);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgePMRUnmakeLocalImportHandle(IMG_UINT32 ui32DispatchTableEntry,
				       IMG_UINT8 * psPMRUnmakeLocalImportHandleIN_UI8,
				       IMG_UINT8 * psPMRUnmakeLocalImportHandleOUT_UI8,
				       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PMRUNMAKELOCALIMPORTHANDLE *psPMRUnmakeLocalImportHandleIN =
	    (PVRSRV_BRIDGE_IN_PMRUNMAKELOCALIMPORTHANDLE *)
	    IMG_OFFSET_ADDR(psPMRUnmakeLocalImportHandleIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PMRUNMAKELOCALIMPORTHANDLE *psPMRUnmakeLocalImportHandleOUT =
	    (PVRSRV_BRIDGE_OUT_PMRUNMAKELOCALIMPORTHANDLE *)
	    IMG_OFFSET_ADDR(psPMRUnmakeLocalImportHandleOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psProcessHandleBase->psHandleBase);

	psPMRUnmakeLocalImportHandleOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psProcessHandleBase->psHandleBase,
					      (IMG_HANDLE) psPMRUnmakeLocalImportHandleIN->hExtMem,
					      PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT);
	if (unlikely((psPMRUnmakeLocalImportHandleOUT->eError != PVRSRV_OK) &&
		     (psPMRUnmakeLocalImportHandleOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psPMRUnmakeLocalImportHandleOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psPMRUnmakeLocalImportHandleOUT->eError)));
		UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);
		goto PMRUnmakeLocalImportHandle_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);

PMRUnmakeLocalImportHandle_exit:

	return 0;
}

static PVRSRV_ERROR _PMRImportPMRpsPMRIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PMRUnrefPMR((PMR *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgePMRImportPMR(IMG_UINT32 ui32DispatchTableEntry,
			 IMG_UINT8 * psPMRImportPMRIN_UI8,
			 IMG_UINT8 * psPMRImportPMROUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PMRIMPORTPMR *psPMRImportPMRIN =
	    (PVRSRV_BRIDGE_IN_PMRIMPORTPMR *) IMG_OFFSET_ADDR(psPMRImportPMRIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PMRIMPORTPMR *psPMRImportPMROUT =
	    (PVRSRV_BRIDGE_OUT_PMRIMPORTPMR *) IMG_OFFSET_ADDR(psPMRImportPMROUT_UI8, 0);

	IMG_HANDLE hPMRExport = psPMRImportPMRIN->hPMRExport;
	PMR_EXPORT *psPMRExportInt = NULL;
	PMR *psPMRInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(KERNEL_HANDLE_BASE);

	/* Look up the address from the handle */
	psPMRImportPMROUT->eError =
	    PVRSRVLookupHandleUnlocked(KERNEL_HANDLE_BASE,
				       (void **)&psPMRExportInt,
				       hPMRExport, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT, IMG_TRUE);
	if (unlikely(psPMRImportPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(KERNEL_HANDLE_BASE);
		goto PMRImportPMR_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(KERNEL_HANDLE_BASE);

	psPMRImportPMROUT->eError =
	    PhysmemImportPMR(psConnection, OSGetDevNode(psConnection),
			     psPMRExportInt,
			     psPMRImportPMRIN->ui64uiPassword,
			     psPMRImportPMRIN->ui64uiSize,
			     psPMRImportPMRIN->ui32uiLog2Contig, &psPMRInt);
	/* Exit early if bridged call fails */
	if (unlikely(psPMRImportPMROUT->eError != PVRSRV_OK))
	{
		goto PMRImportPMR_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psPMRImportPMROUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
							      &psPMRImportPMROUT->hPMR,
							      (void *)psPMRInt,
							      PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
							      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
							      (PFN_HANDLE_RELEASE) &
							      _PMRImportPMRpsPMRIntRelease);
	if (unlikely(psPMRImportPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto PMRImportPMR_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

PMRImportPMR_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(KERNEL_HANDLE_BASE);

	/* Unreference the previously looked up handle */
	if (psPMRExportInt)
	{
		PVRSRVReleaseHandleUnlocked(KERNEL_HANDLE_BASE,
					    hPMRExport, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(KERNEL_HANDLE_BASE);

	if (psPMRImportPMROUT->eError != PVRSRV_OK)
	{
		if (psPMRInt)
		{
			LockHandle(KERNEL_HANDLE_BASE);
			PMRUnrefPMR(psPMRInt);
			UnlockHandle(KERNEL_HANDLE_BASE);
		}
	}

	return 0;
}

static PVRSRV_ERROR _PMRLocalImportPMRpsPMRIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PMRUnrefPMR((PMR *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgePMRLocalImportPMR(IMG_UINT32 ui32DispatchTableEntry,
			      IMG_UINT8 * psPMRLocalImportPMRIN_UI8,
			      IMG_UINT8 * psPMRLocalImportPMROUT_UI8,
			      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PMRLOCALIMPORTPMR *psPMRLocalImportPMRIN =
	    (PVRSRV_BRIDGE_IN_PMRLOCALIMPORTPMR *) IMG_OFFSET_ADDR(psPMRLocalImportPMRIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PMRLOCALIMPORTPMR *psPMRLocalImportPMROUT =
	    (PVRSRV_BRIDGE_OUT_PMRLOCALIMPORTPMR *) IMG_OFFSET_ADDR(psPMRLocalImportPMROUT_UI8, 0);

	IMG_HANDLE hExtHandle = psPMRLocalImportPMRIN->hExtHandle;
	PMR *psExtHandleInt = NULL;
	PMR *psPMRInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psProcessHandleBase->psHandleBase);

	/* Look up the address from the handle */
	psPMRLocalImportPMROUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psProcessHandleBase->psHandleBase,
				       (void **)&psExtHandleInt,
				       hExtHandle, PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT, IMG_TRUE);
	if (unlikely(psPMRLocalImportPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);
		goto PMRLocalImportPMR_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);

	psPMRLocalImportPMROUT->eError =
	    PMRLocalImportPMR(psExtHandleInt,
			      &psPMRInt,
			      &psPMRLocalImportPMROUT->uiSize, &psPMRLocalImportPMROUT->uiAlign);
	/* Exit early if bridged call fails */
	if (unlikely(psPMRLocalImportPMROUT->eError != PVRSRV_OK))
	{
		goto PMRLocalImportPMR_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psPMRLocalImportPMROUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								   &psPMRLocalImportPMROUT->hPMR,
								   (void *)psPMRInt,
								   PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
								   PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								   (PFN_HANDLE_RELEASE) &
								   _PMRLocalImportPMRpsPMRIntRelease);
	if (unlikely(psPMRLocalImportPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto PMRLocalImportPMR_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

PMRLocalImportPMR_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psProcessHandleBase->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psExtHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psProcessHandleBase->psHandleBase,
					    hExtHandle, PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psProcessHandleBase->psHandleBase);

	if (psPMRLocalImportPMROUT->eError != PVRSRV_OK)
	{
		if (psPMRInt)
		{
			LockHandle(KERNEL_HANDLE_BASE);
			PMRUnrefPMR(psPMRInt);
			UnlockHandle(KERNEL_HANDLE_BASE);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgePMRUnrefPMR(IMG_UINT32 ui32DispatchTableEntry,
			IMG_UINT8 * psPMRUnrefPMRIN_UI8,
			IMG_UINT8 * psPMRUnrefPMROUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PMRUNREFPMR *psPMRUnrefPMRIN =
	    (PVRSRV_BRIDGE_IN_PMRUNREFPMR *) IMG_OFFSET_ADDR(psPMRUnrefPMRIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PMRUNREFPMR *psPMRUnrefPMROUT =
	    (PVRSRV_BRIDGE_OUT_PMRUNREFPMR *) IMG_OFFSET_ADDR(psPMRUnrefPMROUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psPMRUnrefPMROUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psPMRUnrefPMRIN->hPMR,
					      PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	if (unlikely((psPMRUnrefPMROUT->eError != PVRSRV_OK) &&
		     (psPMRUnrefPMROUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psPMRUnrefPMROUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psPMRUnrefPMROUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto PMRUnrefPMR_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

PMRUnrefPMR_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRUnrefUnlockPMR(IMG_UINT32 ui32DispatchTableEntry,
			      IMG_UINT8 * psPMRUnrefUnlockPMRIN_UI8,
			      IMG_UINT8 * psPMRUnrefUnlockPMROUT_UI8,
			      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PMRUNREFUNLOCKPMR *psPMRUnrefUnlockPMRIN =
	    (PVRSRV_BRIDGE_IN_PMRUNREFUNLOCKPMR *) IMG_OFFSET_ADDR(psPMRUnrefUnlockPMRIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PMRUNREFUNLOCKPMR *psPMRUnrefUnlockPMROUT =
	    (PVRSRV_BRIDGE_OUT_PMRUNREFUNLOCKPMR *) IMG_OFFSET_ADDR(psPMRUnrefUnlockPMROUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psPMRUnrefUnlockPMROUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psPMRUnrefUnlockPMRIN->hPMR,
					      PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	if (unlikely((psPMRUnrefUnlockPMROUT->eError != PVRSRV_OK) &&
		     (psPMRUnrefUnlockPMROUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psPMRUnrefUnlockPMROUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psPMRUnrefUnlockPMROUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto PMRUnrefUnlockPMR_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

PMRUnrefUnlockPMR_exit:

	return 0;
}

static PVRSRV_ERROR _PhysmemNewRamBackedPMRpsPMRPtrIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PMRUnrefPMR((PMR *) pvData);
	return eError;
}

static_assert(PMR_MAX_SUPPORTED_4K_PAGE_COUNT <= IMG_UINT32_MAX,
	      "PMR_MAX_SUPPORTED_4K_PAGE_COUNT must not be larger than IMG_UINT32_MAX");
static_assert(DEVMEM_ANNOTATION_MAX_LEN <= IMG_UINT32_MAX,
	      "DEVMEM_ANNOTATION_MAX_LEN must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgePhysmemNewRamBackedPMR(IMG_UINT32 ui32DispatchTableEntry,
				   IMG_UINT8 * psPhysmemNewRamBackedPMRIN_UI8,
				   IMG_UINT8 * psPhysmemNewRamBackedPMROUT_UI8,
				   CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PHYSMEMNEWRAMBACKEDPMR *psPhysmemNewRamBackedPMRIN =
	    (PVRSRV_BRIDGE_IN_PHYSMEMNEWRAMBACKEDPMR *)
	    IMG_OFFSET_ADDR(psPhysmemNewRamBackedPMRIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PHYSMEMNEWRAMBACKEDPMR *psPhysmemNewRamBackedPMROUT =
	    (PVRSRV_BRIDGE_OUT_PHYSMEMNEWRAMBACKEDPMR *)
	    IMG_OFFSET_ADDR(psPhysmemNewRamBackedPMROUT_UI8, 0);

	IMG_UINT32 *ui32MappingTableInt = NULL;
	IMG_CHAR *uiAnnotationInt = NULL;
	PMR *psPMRPtrInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psPhysmemNewRamBackedPMRIN->ui32NumPhysChunks * sizeof(IMG_UINT32)) +
	    ((IMG_UINT64) psPhysmemNewRamBackedPMRIN->ui32AnnotationLength * sizeof(IMG_CHAR)) + 0;

	if (unlikely
	    (psPhysmemNewRamBackedPMRIN->ui32NumPhysChunks > PMR_MAX_SUPPORTED_4K_PAGE_COUNT))
	{
		psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto PhysmemNewRamBackedPMR_exit;
	}

	if (unlikely(psPhysmemNewRamBackedPMRIN->ui32AnnotationLength > DEVMEM_ANNOTATION_MAX_LEN))
	{
		psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto PhysmemNewRamBackedPMR_exit;
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto PhysmemNewRamBackedPMR_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psPhysmemNewRamBackedPMRIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psPhysmemNewRamBackedPMRIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto PhysmemNewRamBackedPMR_exit;
			}
		}
	}

	if (psPhysmemNewRamBackedPMRIN->ui32NumPhysChunks != 0)
	{
		ui32MappingTableInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psPhysmemNewRamBackedPMRIN->ui32NumPhysChunks * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psPhysmemNewRamBackedPMRIN->ui32NumPhysChunks * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32MappingTableInt,
		     (const void __user *)psPhysmemNewRamBackedPMRIN->pui32MappingTable,
		     psPhysmemNewRamBackedPMRIN->ui32NumPhysChunks * sizeof(IMG_UINT32)) !=
		    PVRSRV_OK)
		{
			psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto PhysmemNewRamBackedPMR_exit;
		}
	}
	if (psPhysmemNewRamBackedPMRIN->ui32AnnotationLength != 0)
	{
		uiAnnotationInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psPhysmemNewRamBackedPMRIN->ui32AnnotationLength * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (psPhysmemNewRamBackedPMRIN->ui32AnnotationLength * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiAnnotationInt,
		     (const void __user *)psPhysmemNewRamBackedPMRIN->puiAnnotation,
		     psPhysmemNewRamBackedPMRIN->ui32AnnotationLength * sizeof(IMG_CHAR)) !=
		    PVRSRV_OK)
		{
			psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto PhysmemNewRamBackedPMR_exit;
		}
		((IMG_CHAR *)
		 uiAnnotationInt)[(psPhysmemNewRamBackedPMRIN->ui32AnnotationLength *
				   sizeof(IMG_CHAR)) - 1] = '\0';
	}

	psPhysmemNewRamBackedPMROUT->eError =
	    PhysmemNewRamBackedPMR(psConnection, OSGetDevNode(psConnection),
				   psPhysmemNewRamBackedPMRIN->uiSize,
				   psPhysmemNewRamBackedPMRIN->ui32NumPhysChunks,
				   psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks,
				   ui32MappingTableInt,
				   psPhysmemNewRamBackedPMRIN->ui32Log2PageSize,
				   psPhysmemNewRamBackedPMRIN->uiFlags,
				   psPhysmemNewRamBackedPMRIN->ui32AnnotationLength,
				   uiAnnotationInt,
				   psPhysmemNewRamBackedPMRIN->ui32PID,
				   &psPMRPtrInt,
				   psPhysmemNewRamBackedPMRIN->ui32PDumpFlags,
				   &psPhysmemNewRamBackedPMROUT->uiOutFlags);
	/* Exit early if bridged call fails */
	if (unlikely(psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK))
	{
		goto PhysmemNewRamBackedPMR_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psPhysmemNewRamBackedPMROUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
									&psPhysmemNewRamBackedPMROUT->
									hPMRPtr,
									(void *)psPMRPtrInt,
									PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
									PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
									(PFN_HANDLE_RELEASE) &
									_PhysmemNewRamBackedPMRpsPMRPtrIntRelease);
	if (unlikely(psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto PhysmemNewRamBackedPMR_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

PhysmemNewRamBackedPMR_exit:

	if (psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK)
	{
		if (psPMRPtrInt)
		{
			LockHandle(KERNEL_HANDLE_BASE);
			PMRUnrefPMR(psPMRPtrInt);
			UnlockHandle(KERNEL_HANDLE_BASE);
		}
	}

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psPhysmemNewRamBackedPMROUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static PVRSRV_ERROR _DevmemIntCtxCreatepsDevMemServerContextIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = DevmemIntCtxDestroy((DEVMEMINT_CTX *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeDevmemIntCtxCreate(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psDevmemIntCtxCreateIN_UI8,
			       IMG_UINT8 * psDevmemIntCtxCreateOUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTCTXCREATE *psDevmemIntCtxCreateIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTCTXCREATE *) IMG_OFFSET_ADDR(psDevmemIntCtxCreateIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTCTXCREATE *psDevmemIntCtxCreateOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTCTXCREATE *) IMG_OFFSET_ADDR(psDevmemIntCtxCreateOUT_UI8,
								     0);

	DEVMEMINT_CTX *psDevMemServerContextInt = NULL;
	IMG_HANDLE hPrivDataInt = NULL;

	psDevmemIntCtxCreateOUT->hDevMemServerContext = NULL;

	psDevmemIntCtxCreateOUT->eError =
	    DevmemIntCtxCreate(psConnection, OSGetDevNode(psConnection),
			       psDevmemIntCtxCreateIN->bbKernelMemoryCtx,
			       &psDevMemServerContextInt,
			       &hPrivDataInt, &psDevmemIntCtxCreateOUT->ui32CPUCacheLineSize);
	/* Exit early if bridged call fails */
	if (unlikely(psDevmemIntCtxCreateOUT->eError != PVRSRV_OK))
	{
		goto DevmemIntCtxCreate_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psDevmemIntCtxCreateOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								    &psDevmemIntCtxCreateOUT->
								    hDevMemServerContext,
								    (void *)
								    psDevMemServerContextInt,
								    PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX,
								    PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								    (PFN_HANDLE_RELEASE) &
								    _DevmemIntCtxCreatepsDevMemServerContextIntRelease);
	if (unlikely(psDevmemIntCtxCreateOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntCtxCreate_exit;
	}

	psDevmemIntCtxCreateOUT->eError = PVRSRVAllocSubHandleUnlocked(psConnection->psHandleBase,
								       &psDevmemIntCtxCreateOUT->
								       hPrivData,
								       (void *)hPrivDataInt,
								       PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
								       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								       psDevmemIntCtxCreateOUT->
								       hDevMemServerContext);
	if (unlikely(psDevmemIntCtxCreateOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntCtxCreate_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

DevmemIntCtxCreate_exit:

	if (psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
	{
		if (psDevmemIntCtxCreateOUT->hDevMemServerContext)
		{
			PVRSRV_ERROR eError;

			/* Lock over handle creation cleanup. */
			LockHandle(psConnection->psHandleBase);

			eError = PVRSRVDestroyHandleUnlocked(psConnection->psHandleBase,
							     (IMG_HANDLE) psDevmemIntCtxCreateOUT->
							     hDevMemServerContext,
							     PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
			if (unlikely((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY)))
			{
				PVR_DPF((PVR_DBG_ERROR,
					 "%s: %s", __func__, PVRSRVGetErrorString(eError)));
			}
			/* Releasing the handle should free/destroy/release the resource.
			 * This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psDevMemServerContextInt = NULL;
			/* Release now we have cleaned up creation handles. */
			UnlockHandle(psConnection->psHandleBase);

		}

		if (psDevMemServerContextInt)
		{
			DevmemIntCtxDestroy(psDevMemServerContextInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntCtxDestroy(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psDevmemIntCtxDestroyIN_UI8,
				IMG_UINT8 * psDevmemIntCtxDestroyOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTCTXDESTROY *psDevmemIntCtxDestroyIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTCTXDESTROY *) IMG_OFFSET_ADDR(psDevmemIntCtxDestroyIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTCTXDESTROY *psDevmemIntCtxDestroyOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTCTXDESTROY *) IMG_OFFSET_ADDR(psDevmemIntCtxDestroyOUT_UI8,
								      0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psDevmemIntCtxDestroyOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psDevmemIntCtxDestroyIN->
					      hDevmemServerContext,
					      PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
	if (unlikely
	    ((psDevmemIntCtxDestroyOUT->eError != PVRSRV_OK)
	     && (psDevmemIntCtxDestroyOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL)
	     && (psDevmemIntCtxDestroyOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psDevmemIntCtxDestroyOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntCtxDestroy_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

DevmemIntCtxDestroy_exit:

	return 0;
}

static PVRSRV_ERROR _DevmemIntHeapCreatepsDevmemHeapPtrIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = DevmemIntHeapDestroy((DEVMEMINT_HEAP *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeDevmemIntHeapCreate(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psDevmemIntHeapCreateIN_UI8,
				IMG_UINT8 * psDevmemIntHeapCreateOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTHEAPCREATE *psDevmemIntHeapCreateIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTHEAPCREATE *) IMG_OFFSET_ADDR(psDevmemIntHeapCreateIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTHEAPCREATE *psDevmemIntHeapCreateOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTHEAPCREATE *) IMG_OFFSET_ADDR(psDevmemIntHeapCreateOUT_UI8,
								      0);

	IMG_HANDLE hDevmemCtx = psDevmemIntHeapCreateIN->hDevmemCtx;
	DEVMEMINT_CTX *psDevmemCtxInt = NULL;
	DEVMEMINT_HEAP *psDevmemHeapPtrInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemIntHeapCreateOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psDevmemCtxInt,
				       hDevmemCtx, PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX, IMG_TRUE);
	if (unlikely(psDevmemIntHeapCreateOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntHeapCreate_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemIntHeapCreateOUT->eError =
	    DevmemIntHeapCreate(psDevmemCtxInt,
				psDevmemIntHeapCreateIN->ui32HeapConfigIndex,
				psDevmemIntHeapCreateIN->ui32HeapIndex,
				psDevmemIntHeapCreateIN->sHeapBaseAddr,
				psDevmemIntHeapCreateIN->ui32Log2DataPageSize, &psDevmemHeapPtrInt);
	/* Exit early if bridged call fails */
	if (unlikely(psDevmemIntHeapCreateOUT->eError != PVRSRV_OK))
	{
		goto DevmemIntHeapCreate_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psDevmemIntHeapCreateOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								     &psDevmemIntHeapCreateOUT->
								     hDevmemHeapPtr,
								     (void *)psDevmemHeapPtrInt,
								     PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP,
								     PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								     (PFN_HANDLE_RELEASE) &
								     _DevmemIntHeapCreatepsDevmemHeapPtrIntRelease);
	if (unlikely(psDevmemIntHeapCreateOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntHeapCreate_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

DevmemIntHeapCreate_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psDevmemCtxInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hDevmemCtx, PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psDevmemIntHeapCreateOUT->eError != PVRSRV_OK)
	{
		if (psDevmemHeapPtrInt)
		{
			DevmemIntHeapDestroy(psDevmemHeapPtrInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntHeapDestroy(IMG_UINT32 ui32DispatchTableEntry,
				 IMG_UINT8 * psDevmemIntHeapDestroyIN_UI8,
				 IMG_UINT8 * psDevmemIntHeapDestroyOUT_UI8,
				 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTHEAPDESTROY *psDevmemIntHeapDestroyIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTHEAPDESTROY *) IMG_OFFSET_ADDR(psDevmemIntHeapDestroyIN_UI8,
								      0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTHEAPDESTROY *psDevmemIntHeapDestroyOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTHEAPDESTROY *)
	    IMG_OFFSET_ADDR(psDevmemIntHeapDestroyOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psDevmemIntHeapDestroyOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psDevmemIntHeapDestroyIN->hDevmemHeap,
					      PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
	if (unlikely((psDevmemIntHeapDestroyOUT->eError != PVRSRV_OK) &&
		     (psDevmemIntHeapDestroyOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psDevmemIntHeapDestroyOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psDevmemIntHeapDestroyOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntHeapDestroy_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

DevmemIntHeapDestroy_exit:

	return 0;
}

static PVRSRV_ERROR _DevmemIntMapPMRpsMappingIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = DevmemIntUnmapPMR((DEVMEMINT_MAPPING *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeDevmemIntMapPMR(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psDevmemIntMapPMRIN_UI8,
			    IMG_UINT8 * psDevmemIntMapPMROUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTMAPPMR *psDevmemIntMapPMRIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTMAPPMR *) IMG_OFFSET_ADDR(psDevmemIntMapPMRIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTMAPPMR *psDevmemIntMapPMROUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTMAPPMR *) IMG_OFFSET_ADDR(psDevmemIntMapPMROUT_UI8, 0);

	IMG_HANDLE hDevmemServerHeap = psDevmemIntMapPMRIN->hDevmemServerHeap;
	DEVMEMINT_HEAP *psDevmemServerHeapInt = NULL;
	IMG_HANDLE hReservation = psDevmemIntMapPMRIN->hReservation;
	DEVMEMINT_RESERVATION *psReservationInt = NULL;
	IMG_HANDLE hPMR = psDevmemIntMapPMRIN->hPMR;
	PMR *psPMRInt = NULL;
	DEVMEMINT_MAPPING *psMappingInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemIntMapPMROUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psDevmemServerHeapInt,
				       hDevmemServerHeap,
				       PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP, IMG_TRUE);
	if (unlikely(psDevmemIntMapPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntMapPMR_exit;
	}

	/* Look up the address from the handle */
	psDevmemIntMapPMROUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psReservationInt,
				       hReservation,
				       PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION, IMG_TRUE);
	if (unlikely(psDevmemIntMapPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntMapPMR_exit;
	}

	/* Look up the address from the handle */
	psDevmemIntMapPMROUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psDevmemIntMapPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntMapPMR_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemIntMapPMROUT->eError =
	    DevmemIntMapPMR(psDevmemServerHeapInt,
			    psReservationInt,
			    psPMRInt, psDevmemIntMapPMRIN->uiMapFlags, &psMappingInt);
	/* Exit early if bridged call fails */
	if (unlikely(psDevmemIntMapPMROUT->eError != PVRSRV_OK))
	{
		goto DevmemIntMapPMR_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psDevmemIntMapPMROUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								 &psDevmemIntMapPMROUT->hMapping,
								 (void *)psMappingInt,
								 PVRSRV_HANDLE_TYPE_DEVMEMINT_MAPPING,
								 PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								 (PFN_HANDLE_RELEASE) &
								 _DevmemIntMapPMRpsMappingIntRelease);
	if (unlikely(psDevmemIntMapPMROUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntMapPMR_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

DevmemIntMapPMR_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psDevmemServerHeapInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hDevmemServerHeap, PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
	}

	/* Unreference the previously looked up handle */
	if (psReservationInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hReservation, PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
	}

	/* Unreference the previously looked up handle */
	if (psPMRInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psDevmemIntMapPMROUT->eError != PVRSRV_OK)
	{
		if (psMappingInt)
		{
			DevmemIntUnmapPMR(psMappingInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntUnmapPMR(IMG_UINT32 ui32DispatchTableEntry,
			      IMG_UINT8 * psDevmemIntUnmapPMRIN_UI8,
			      IMG_UINT8 * psDevmemIntUnmapPMROUT_UI8,
			      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTUNMAPPMR *psDevmemIntUnmapPMRIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTUNMAPPMR *) IMG_OFFSET_ADDR(psDevmemIntUnmapPMRIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTUNMAPPMR *psDevmemIntUnmapPMROUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTUNMAPPMR *) IMG_OFFSET_ADDR(psDevmemIntUnmapPMROUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psDevmemIntUnmapPMROUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psDevmemIntUnmapPMRIN->hMapping,
					      PVRSRV_HANDLE_TYPE_DEVMEMINT_MAPPING);
	if (unlikely((psDevmemIntUnmapPMROUT->eError != PVRSRV_OK) &&
		     (psDevmemIntUnmapPMROUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psDevmemIntUnmapPMROUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psDevmemIntUnmapPMROUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntUnmapPMR_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

DevmemIntUnmapPMR_exit:

	return 0;
}

static PVRSRV_ERROR _DevmemIntReserveRangepsReservationIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = DevmemIntUnreserveRange((DEVMEMINT_RESERVATION *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeDevmemIntReserveRange(IMG_UINT32 ui32DispatchTableEntry,
				  IMG_UINT8 * psDevmemIntReserveRangeIN_UI8,
				  IMG_UINT8 * psDevmemIntReserveRangeOUT_UI8,
				  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTRESERVERANGE *psDevmemIntReserveRangeIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTRESERVERANGE *)
	    IMG_OFFSET_ADDR(psDevmemIntReserveRangeIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTRESERVERANGE *psDevmemIntReserveRangeOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTRESERVERANGE *)
	    IMG_OFFSET_ADDR(psDevmemIntReserveRangeOUT_UI8, 0);

	IMG_HANDLE hDevmemServerHeap = psDevmemIntReserveRangeIN->hDevmemServerHeap;
	DEVMEMINT_HEAP *psDevmemServerHeapInt = NULL;
	DEVMEMINT_RESERVATION *psReservationInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemIntReserveRangeOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psDevmemServerHeapInt,
				       hDevmemServerHeap,
				       PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP, IMG_TRUE);
	if (unlikely(psDevmemIntReserveRangeOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntReserveRange_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemIntReserveRangeOUT->eError =
	    DevmemIntReserveRange(psDevmemServerHeapInt,
				  psDevmemIntReserveRangeIN->sAddress,
				  psDevmemIntReserveRangeIN->uiLength, &psReservationInt);
	/* Exit early if bridged call fails */
	if (unlikely(psDevmemIntReserveRangeOUT->eError != PVRSRV_OK))
	{
		goto DevmemIntReserveRange_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psDevmemIntReserveRangeOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								       &psDevmemIntReserveRangeOUT->
								       hReservation,
								       (void *)psReservationInt,
								       PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION,
								       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								       (PFN_HANDLE_RELEASE) &
								       _DevmemIntReserveRangepsReservationIntRelease);
	if (unlikely(psDevmemIntReserveRangeOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntReserveRange_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

DevmemIntReserveRange_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psDevmemServerHeapInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hDevmemServerHeap, PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psDevmemIntReserveRangeOUT->eError != PVRSRV_OK)
	{
		if (psReservationInt)
		{
			DevmemIntUnreserveRange(psReservationInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntUnreserveRange(IMG_UINT32 ui32DispatchTableEntry,
				    IMG_UINT8 * psDevmemIntUnreserveRangeIN_UI8,
				    IMG_UINT8 * psDevmemIntUnreserveRangeOUT_UI8,
				    CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTUNRESERVERANGE *psDevmemIntUnreserveRangeIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTUNRESERVERANGE *)
	    IMG_OFFSET_ADDR(psDevmemIntUnreserveRangeIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTUNRESERVERANGE *psDevmemIntUnreserveRangeOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTUNRESERVERANGE *)
	    IMG_OFFSET_ADDR(psDevmemIntUnreserveRangeOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psDevmemIntUnreserveRangeOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psDevmemIntUnreserveRangeIN->
					      hReservation,
					      PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
	if (unlikely
	    ((psDevmemIntUnreserveRangeOUT->eError != PVRSRV_OK)
	     && (psDevmemIntUnreserveRangeOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL)
	     && (psDevmemIntUnreserveRangeOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psDevmemIntUnreserveRangeOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntUnreserveRange_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

DevmemIntUnreserveRange_exit:

	return 0;
}

static_assert(PMR_MAX_SUPPORTED_4K_PAGE_COUNT <= IMG_UINT32_MAX,
	      "PMR_MAX_SUPPORTED_4K_PAGE_COUNT must not be larger than IMG_UINT32_MAX");
static_assert(PMR_MAX_SUPPORTED_4K_PAGE_COUNT <= IMG_UINT32_MAX,
	      "PMR_MAX_SUPPORTED_4K_PAGE_COUNT must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeChangeSparseMem(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psChangeSparseMemIN_UI8,
			    IMG_UINT8 * psChangeSparseMemOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_CHANGESPARSEMEM *psChangeSparseMemIN =
	    (PVRSRV_BRIDGE_IN_CHANGESPARSEMEM *) IMG_OFFSET_ADDR(psChangeSparseMemIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_CHANGESPARSEMEM *psChangeSparseMemOUT =
	    (PVRSRV_BRIDGE_OUT_CHANGESPARSEMEM *) IMG_OFFSET_ADDR(psChangeSparseMemOUT_UI8, 0);

	IMG_HANDLE hSrvDevMemHeap = psChangeSparseMemIN->hSrvDevMemHeap;
	DEVMEMINT_HEAP *psSrvDevMemHeapInt = NULL;
	IMG_HANDLE hPMR = psChangeSparseMemIN->hPMR;
	PMR *psPMRInt = NULL;
	IMG_UINT32 *ui32AllocPageIndicesInt = NULL;
	IMG_UINT32 *ui32FreePageIndicesInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psChangeSparseMemIN->ui32AllocPageCount * sizeof(IMG_UINT32)) +
	    ((IMG_UINT64) psChangeSparseMemIN->ui32FreePageCount * sizeof(IMG_UINT32)) + 0;

	if (unlikely(psChangeSparseMemIN->ui32AllocPageCount > PMR_MAX_SUPPORTED_4K_PAGE_COUNT))
	{
		psChangeSparseMemOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto ChangeSparseMem_exit;
	}

	if (unlikely(psChangeSparseMemIN->ui32FreePageCount > PMR_MAX_SUPPORTED_4K_PAGE_COUNT))
	{
		psChangeSparseMemOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto ChangeSparseMem_exit;
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psChangeSparseMemOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto ChangeSparseMem_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psChangeSparseMemIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psChangeSparseMemIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psChangeSparseMemOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto ChangeSparseMem_exit;
			}
		}
	}

	if (psChangeSparseMemIN->ui32AllocPageCount != 0)
	{
		ui32AllocPageIndicesInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psChangeSparseMemIN->ui32AllocPageCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psChangeSparseMemIN->ui32AllocPageCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32AllocPageIndicesInt,
		     (const void __user *)psChangeSparseMemIN->pui32AllocPageIndices,
		     psChangeSparseMemIN->ui32AllocPageCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psChangeSparseMemOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto ChangeSparseMem_exit;
		}
	}
	if (psChangeSparseMemIN->ui32FreePageCount != 0)
	{
		ui32FreePageIndicesInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psChangeSparseMemIN->ui32FreePageCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psChangeSparseMemIN->ui32FreePageCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32FreePageIndicesInt,
		     (const void __user *)psChangeSparseMemIN->pui32FreePageIndices,
		     psChangeSparseMemIN->ui32FreePageCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psChangeSparseMemOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto ChangeSparseMem_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psChangeSparseMemOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSrvDevMemHeapInt,
				       hSrvDevMemHeap, PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP, IMG_TRUE);
	if (unlikely(psChangeSparseMemOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto ChangeSparseMem_exit;
	}

	/* Look up the address from the handle */
	psChangeSparseMemOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psChangeSparseMemOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto ChangeSparseMem_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psChangeSparseMemOUT->eError =
	    DevmemIntChangeSparse(psSrvDevMemHeapInt,
				  psPMRInt,
				  psChangeSparseMemIN->ui32AllocPageCount,
				  ui32AllocPageIndicesInt,
				  psChangeSparseMemIN->ui32FreePageCount,
				  ui32FreePageIndicesInt,
				  psChangeSparseMemIN->ui32SparseFlags,
				  psChangeSparseMemIN->uiFlags,
				  psChangeSparseMemIN->sDevVAddr,
				  psChangeSparseMemIN->ui64CPUVAddr);

ChangeSparseMem_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSrvDevMemHeapInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSrvDevMemHeap, PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
	}

	/* Unreference the previously looked up handle */
	if (psPMRInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psChangeSparseMemOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntMapPages(IMG_UINT32 ui32DispatchTableEntry,
			      IMG_UINT8 * psDevmemIntMapPagesIN_UI8,
			      IMG_UINT8 * psDevmemIntMapPagesOUT_UI8,
			      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTMAPPAGES *psDevmemIntMapPagesIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTMAPPAGES *) IMG_OFFSET_ADDR(psDevmemIntMapPagesIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTMAPPAGES *psDevmemIntMapPagesOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTMAPPAGES *) IMG_OFFSET_ADDR(psDevmemIntMapPagesOUT_UI8, 0);

	IMG_HANDLE hReservation = psDevmemIntMapPagesIN->hReservation;
	DEVMEMINT_RESERVATION *psReservationInt = NULL;
	IMG_HANDLE hPMR = psDevmemIntMapPagesIN->hPMR;
	PMR *psPMRInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemIntMapPagesOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psReservationInt,
				       hReservation,
				       PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION, IMG_TRUE);
	if (unlikely(psDevmemIntMapPagesOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntMapPages_exit;
	}

	/* Look up the address from the handle */
	psDevmemIntMapPagesOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psDevmemIntMapPagesOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntMapPages_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemIntMapPagesOUT->eError =
	    DevmemIntMapPages(psReservationInt,
			      psPMRInt,
			      psDevmemIntMapPagesIN->ui32PageCount,
			      psDevmemIntMapPagesIN->ui32PhysicalPgOffset,
			      psDevmemIntMapPagesIN->uiFlags, psDevmemIntMapPagesIN->sDevVAddr);

DevmemIntMapPages_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psReservationInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hReservation, PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
	}

	/* Unreference the previously looked up handle */
	if (psPMRInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntUnmapPages(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psDevmemIntUnmapPagesIN_UI8,
				IMG_UINT8 * psDevmemIntUnmapPagesOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTUNMAPPAGES *psDevmemIntUnmapPagesIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTUNMAPPAGES *) IMG_OFFSET_ADDR(psDevmemIntUnmapPagesIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTUNMAPPAGES *psDevmemIntUnmapPagesOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTUNMAPPAGES *) IMG_OFFSET_ADDR(psDevmemIntUnmapPagesOUT_UI8,
								      0);

	IMG_HANDLE hReservation = psDevmemIntUnmapPagesIN->hReservation;
	DEVMEMINT_RESERVATION *psReservationInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemIntUnmapPagesOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psReservationInt,
				       hReservation,
				       PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION, IMG_TRUE);
	if (unlikely(psDevmemIntUnmapPagesOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntUnmapPages_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemIntUnmapPagesOUT->eError =
	    DevmemIntUnmapPages(psReservationInt,
				psDevmemIntUnmapPagesIN->sDevVAddr,
				psDevmemIntUnmapPagesIN->ui32PageCount);

DevmemIntUnmapPages_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psReservationInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hReservation, PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIsVDevAddrValid(IMG_UINT32 ui32DispatchTableEntry,
				  IMG_UINT8 * psDevmemIsVDevAddrValidIN_UI8,
				  IMG_UINT8 * psDevmemIsVDevAddrValidOUT_UI8,
				  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMISVDEVADDRVALID *psDevmemIsVDevAddrValidIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMISVDEVADDRVALID *)
	    IMG_OFFSET_ADDR(psDevmemIsVDevAddrValidIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMISVDEVADDRVALID *psDevmemIsVDevAddrValidOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMISVDEVADDRVALID *)
	    IMG_OFFSET_ADDR(psDevmemIsVDevAddrValidOUT_UI8, 0);

	IMG_HANDLE hDevmemCtx = psDevmemIsVDevAddrValidIN->hDevmemCtx;
	DEVMEMINT_CTX *psDevmemCtxInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemIsVDevAddrValidOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psDevmemCtxInt,
				       hDevmemCtx, PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX, IMG_TRUE);
	if (unlikely(psDevmemIsVDevAddrValidOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIsVDevAddrValid_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemIsVDevAddrValidOUT->eError =
	    DevmemIntIsVDevAddrValid(psConnection, OSGetDevNode(psConnection),
				     psDevmemCtxInt, psDevmemIsVDevAddrValidIN->sAddress);

DevmemIsVDevAddrValid_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psDevmemCtxInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hDevmemCtx, PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

#if defined(RGX_FEATURE_FBCDC)

static IMG_INT
PVRSRVBridgeDevmemInvalidateFBSCTable(IMG_UINT32 ui32DispatchTableEntry,
				      IMG_UINT8 * psDevmemInvalidateFBSCTableIN_UI8,
				      IMG_UINT8 * psDevmemInvalidateFBSCTableOUT_UI8,
				      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINVALIDATEFBSCTABLE *psDevmemInvalidateFBSCTableIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINVALIDATEFBSCTABLE *)
	    IMG_OFFSET_ADDR(psDevmemInvalidateFBSCTableIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMINVALIDATEFBSCTABLE *psDevmemInvalidateFBSCTableOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINVALIDATEFBSCTABLE *)
	    IMG_OFFSET_ADDR(psDevmemInvalidateFBSCTableOUT_UI8, 0);

	IMG_HANDLE hDevmemCtx = psDevmemInvalidateFBSCTableIN->hDevmemCtx;
	DEVMEMINT_CTX *psDevmemCtxInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemInvalidateFBSCTableOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psDevmemCtxInt,
				       hDevmemCtx, PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX, IMG_TRUE);
	if (unlikely(psDevmemInvalidateFBSCTableOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemInvalidateFBSCTable_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemInvalidateFBSCTableOUT->eError =
	    DevmemIntInvalidateFBSCTable(psDevmemCtxInt,
					 psDevmemInvalidateFBSCTableIN->ui64FBSCEntries);

DevmemInvalidateFBSCTable_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psDevmemCtxInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hDevmemCtx, PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

#else
#define PVRSRVBridgeDevmemInvalidateFBSCTable NULL
#endif

static IMG_INT
PVRSRVBridgeHeapCfgHeapConfigCount(IMG_UINT32 ui32DispatchTableEntry,
				   IMG_UINT8 * psHeapCfgHeapConfigCountIN_UI8,
				   IMG_UINT8 * psHeapCfgHeapConfigCountOUT_UI8,
				   CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_HEAPCFGHEAPCONFIGCOUNT *psHeapCfgHeapConfigCountIN =
	    (PVRSRV_BRIDGE_IN_HEAPCFGHEAPCONFIGCOUNT *)
	    IMG_OFFSET_ADDR(psHeapCfgHeapConfigCountIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCONFIGCOUNT *psHeapCfgHeapConfigCountOUT =
	    (PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCONFIGCOUNT *)
	    IMG_OFFSET_ADDR(psHeapCfgHeapConfigCountOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psHeapCfgHeapConfigCountIN);

	psHeapCfgHeapConfigCountOUT->eError =
	    HeapCfgHeapConfigCount(psConnection, OSGetDevNode(psConnection),
				   &psHeapCfgHeapConfigCountOUT->ui32NumHeapConfigs);

	return 0;
}

static IMG_INT
PVRSRVBridgeHeapCfgHeapCount(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psHeapCfgHeapCountIN_UI8,
			     IMG_UINT8 * psHeapCfgHeapCountOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_HEAPCFGHEAPCOUNT *psHeapCfgHeapCountIN =
	    (PVRSRV_BRIDGE_IN_HEAPCFGHEAPCOUNT *) IMG_OFFSET_ADDR(psHeapCfgHeapCountIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCOUNT *psHeapCfgHeapCountOUT =
	    (PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCOUNT *) IMG_OFFSET_ADDR(psHeapCfgHeapCountOUT_UI8, 0);

	psHeapCfgHeapCountOUT->eError =
	    HeapCfgHeapCount(psConnection, OSGetDevNode(psConnection),
			     psHeapCfgHeapCountIN->ui32HeapConfigIndex,
			     &psHeapCfgHeapCountOUT->ui32NumHeaps);

	return 0;
}

static_assert(DEVMEM_HEAPNAME_MAXLENGTH <= IMG_UINT32_MAX,
	      "DEVMEM_HEAPNAME_MAXLENGTH must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeHeapCfgHeapConfigName(IMG_UINT32 ui32DispatchTableEntry,
				  IMG_UINT8 * psHeapCfgHeapConfigNameIN_UI8,
				  IMG_UINT8 * psHeapCfgHeapConfigNameOUT_UI8,
				  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_HEAPCFGHEAPCONFIGNAME *psHeapCfgHeapConfigNameIN =
	    (PVRSRV_BRIDGE_IN_HEAPCFGHEAPCONFIGNAME *)
	    IMG_OFFSET_ADDR(psHeapCfgHeapConfigNameIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCONFIGNAME *psHeapCfgHeapConfigNameOUT =
	    (PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCONFIGNAME *)
	    IMG_OFFSET_ADDR(psHeapCfgHeapConfigNameOUT_UI8, 0);

	IMG_CHAR *puiHeapConfigNameInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz * sizeof(IMG_CHAR)) +
	    0;

	if (psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz > DEVMEM_HEAPNAME_MAXLENGTH)
	{
		psHeapCfgHeapConfigNameOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto HeapCfgHeapConfigName_exit;
	}

	psHeapCfgHeapConfigNameOUT->puiHeapConfigName =
	    psHeapCfgHeapConfigNameIN->puiHeapConfigName;

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psHeapCfgHeapConfigNameOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto HeapCfgHeapConfigName_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psHeapCfgHeapConfigNameIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psHeapCfgHeapConfigNameIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psHeapCfgHeapConfigNameOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto HeapCfgHeapConfigName_exit;
			}
		}
	}

	if (psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz != 0)
	{
		puiHeapConfigNameInt =
		    (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz * sizeof(IMG_CHAR);
	}

	psHeapCfgHeapConfigNameOUT->eError =
	    HeapCfgHeapConfigName(psConnection, OSGetDevNode(psConnection),
				  psHeapCfgHeapConfigNameIN->ui32HeapConfigIndex,
				  psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz,
				  puiHeapConfigNameInt);
	/* Exit early if bridged call fails */
	if (unlikely(psHeapCfgHeapConfigNameOUT->eError != PVRSRV_OK))
	{
		goto HeapCfgHeapConfigName_exit;
	}

	/* If dest ptr is non-null and we have data to copy */
	if ((puiHeapConfigNameInt) &&
	    ((psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz * sizeof(IMG_CHAR)) > 0))
	{
		if (unlikely
		    (OSCopyToUser
		     (NULL, (void __user *)psHeapCfgHeapConfigNameOUT->puiHeapConfigName,
		      puiHeapConfigNameInt,
		      (psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz * sizeof(IMG_CHAR))) !=
		     PVRSRV_OK))
		{
			psHeapCfgHeapConfigNameOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto HeapCfgHeapConfigName_exit;
		}
	}

HeapCfgHeapConfigName_exit:

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psHeapCfgHeapConfigNameOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static_assert(DEVMEM_HEAPNAME_MAXLENGTH <= IMG_UINT32_MAX,
	      "DEVMEM_HEAPNAME_MAXLENGTH must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeHeapCfgHeapDetails(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psHeapCfgHeapDetailsIN_UI8,
			       IMG_UINT8 * psHeapCfgHeapDetailsOUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_HEAPCFGHEAPDETAILS *psHeapCfgHeapDetailsIN =
	    (PVRSRV_BRIDGE_IN_HEAPCFGHEAPDETAILS *) IMG_OFFSET_ADDR(psHeapCfgHeapDetailsIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_HEAPCFGHEAPDETAILS *psHeapCfgHeapDetailsOUT =
	    (PVRSRV_BRIDGE_OUT_HEAPCFGHEAPDETAILS *) IMG_OFFSET_ADDR(psHeapCfgHeapDetailsOUT_UI8,
								     0);

	IMG_CHAR *puiHeapNameOutInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psHeapCfgHeapDetailsIN->ui32HeapNameBufSz * sizeof(IMG_CHAR)) + 0;

	if (psHeapCfgHeapDetailsIN->ui32HeapNameBufSz > DEVMEM_HEAPNAME_MAXLENGTH)
	{
		psHeapCfgHeapDetailsOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto HeapCfgHeapDetails_exit;
	}

	psHeapCfgHeapDetailsOUT->puiHeapNameOut = psHeapCfgHeapDetailsIN->puiHeapNameOut;

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psHeapCfgHeapDetailsOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto HeapCfgHeapDetails_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psHeapCfgHeapDetailsIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psHeapCfgHeapDetailsIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psHeapCfgHeapDetailsOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto HeapCfgHeapDetails_exit;
			}
		}
	}

	if (psHeapCfgHeapDetailsIN->ui32HeapNameBufSz != 0)
	{
		puiHeapNameOutInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psHeapCfgHeapDetailsIN->ui32HeapNameBufSz * sizeof(IMG_CHAR);
	}

	psHeapCfgHeapDetailsOUT->eError =
	    HeapCfgHeapDetails(psConnection, OSGetDevNode(psConnection),
			       psHeapCfgHeapDetailsIN->ui32HeapConfigIndex,
			       psHeapCfgHeapDetailsIN->ui32HeapIndex,
			       psHeapCfgHeapDetailsIN->ui32HeapNameBufSz,
			       puiHeapNameOutInt,
			       &psHeapCfgHeapDetailsOUT->sDevVAddrBase,
			       &psHeapCfgHeapDetailsOUT->uiHeapLength,
			       &psHeapCfgHeapDetailsOUT->uiReservedRegionLength,
			       &psHeapCfgHeapDetailsOUT->ui32Log2DataPageSizeOut,
			       &psHeapCfgHeapDetailsOUT->ui32Log2ImportAlignmentOut);
	/* Exit early if bridged call fails */
	if (unlikely(psHeapCfgHeapDetailsOUT->eError != PVRSRV_OK))
	{
		goto HeapCfgHeapDetails_exit;
	}

	/* If dest ptr is non-null and we have data to copy */
	if ((puiHeapNameOutInt) &&
	    ((psHeapCfgHeapDetailsIN->ui32HeapNameBufSz * sizeof(IMG_CHAR)) > 0))
	{
		if (unlikely
		    (OSCopyToUser
		     (NULL, (void __user *)psHeapCfgHeapDetailsOUT->puiHeapNameOut,
		      puiHeapNameOutInt,
		      (psHeapCfgHeapDetailsIN->ui32HeapNameBufSz * sizeof(IMG_CHAR))) != PVRSRV_OK))
		{
			psHeapCfgHeapDetailsOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto HeapCfgHeapDetails_exit;
		}
	}

HeapCfgHeapDetails_exit:

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psHeapCfgHeapDetailsOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntRegisterPFNotifyKM(IMG_UINT32 ui32DispatchTableEntry,
					IMG_UINT8 * psDevmemIntRegisterPFNotifyKMIN_UI8,
					IMG_UINT8 * psDevmemIntRegisterPFNotifyKMOUT_UI8,
					CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMINTREGISTERPFNOTIFYKM *psDevmemIntRegisterPFNotifyKMIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMINTREGISTERPFNOTIFYKM *)
	    IMG_OFFSET_ADDR(psDevmemIntRegisterPFNotifyKMIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMINTREGISTERPFNOTIFYKM *psDevmemIntRegisterPFNotifyKMOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMINTREGISTERPFNOTIFYKM *)
	    IMG_OFFSET_ADDR(psDevmemIntRegisterPFNotifyKMOUT_UI8, 0);

	IMG_HANDLE hDevmemCtx = psDevmemIntRegisterPFNotifyKMIN->hDevmemCtx;
	DEVMEMINT_CTX *psDevmemCtxInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemIntRegisterPFNotifyKMOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psDevmemCtxInt,
				       hDevmemCtx, PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX, IMG_TRUE);
	if (unlikely(psDevmemIntRegisterPFNotifyKMOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemIntRegisterPFNotifyKM_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemIntRegisterPFNotifyKMOUT->eError =
	    DevmemIntRegisterPFNotifyKM(psDevmemCtxInt, psDevmemIntRegisterPFNotifyKMIN->bRegister);

DevmemIntRegisterPFNotifyKM_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psDevmemCtxInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hDevmemCtx, PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static_assert(PVRSRV_PHYS_HEAP_LAST <= IMG_UINT32_MAX,
	      "PVRSRV_PHYS_HEAP_LAST must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgePhysHeapGetMemInfo(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psPhysHeapGetMemInfoIN_UI8,
			       IMG_UINT8 * psPhysHeapGetMemInfoOUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PHYSHEAPGETMEMINFO *psPhysHeapGetMemInfoIN =
	    (PVRSRV_BRIDGE_IN_PHYSHEAPGETMEMINFO *) IMG_OFFSET_ADDR(psPhysHeapGetMemInfoIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PHYSHEAPGETMEMINFO *psPhysHeapGetMemInfoOUT =
	    (PVRSRV_BRIDGE_OUT_PHYSHEAPGETMEMINFO *) IMG_OFFSET_ADDR(psPhysHeapGetMemInfoOUT_UI8,
								     0);

	PVRSRV_PHYS_HEAP *eaPhysHeapIDInt = NULL;
	PHYS_HEAP_MEM_STATS *pasapPhysHeapMemStatsInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psPhysHeapGetMemInfoIN->ui32PhysHeapCount * sizeof(PVRSRV_PHYS_HEAP)) +
	    ((IMG_UINT64) psPhysHeapGetMemInfoIN->ui32PhysHeapCount * sizeof(PHYS_HEAP_MEM_STATS)) +
	    0;

	if (unlikely(psPhysHeapGetMemInfoIN->ui32PhysHeapCount > PVRSRV_PHYS_HEAP_LAST))
	{
		psPhysHeapGetMemInfoOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto PhysHeapGetMemInfo_exit;
	}

	psPhysHeapGetMemInfoOUT->pasapPhysHeapMemStats =
	    psPhysHeapGetMemInfoIN->pasapPhysHeapMemStats;

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psPhysHeapGetMemInfoOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto PhysHeapGetMemInfo_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psPhysHeapGetMemInfoIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psPhysHeapGetMemInfoIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psPhysHeapGetMemInfoOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto PhysHeapGetMemInfo_exit;
			}
		}
	}

	if (psPhysHeapGetMemInfoIN->ui32PhysHeapCount != 0)
	{
		eaPhysHeapIDInt =
		    (PVRSRV_PHYS_HEAP *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psPhysHeapGetMemInfoIN->ui32PhysHeapCount * sizeof(PVRSRV_PHYS_HEAP);
	}

	/* Copy the data over */
	if (psPhysHeapGetMemInfoIN->ui32PhysHeapCount * sizeof(PVRSRV_PHYS_HEAP) > 0)
	{
		if (OSCopyFromUser
		    (NULL, eaPhysHeapIDInt,
		     (const void __user *)psPhysHeapGetMemInfoIN->peaPhysHeapID,
		     psPhysHeapGetMemInfoIN->ui32PhysHeapCount * sizeof(PVRSRV_PHYS_HEAP)) !=
		    PVRSRV_OK)
		{
			psPhysHeapGetMemInfoOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto PhysHeapGetMemInfo_exit;
		}
	}
	if (psPhysHeapGetMemInfoIN->ui32PhysHeapCount != 0)
	{
		pasapPhysHeapMemStatsInt =
		    (PHYS_HEAP_MEM_STATS *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psPhysHeapGetMemInfoIN->ui32PhysHeapCount * sizeof(PHYS_HEAP_MEM_STATS);
	}

	psPhysHeapGetMemInfoOUT->eError =
	    PVRSRVPhysHeapGetMemInfoKM(psConnection, OSGetDevNode(psConnection),
				       psPhysHeapGetMemInfoIN->ui32PhysHeapCount,
				       eaPhysHeapIDInt, pasapPhysHeapMemStatsInt);
	/* Exit early if bridged call fails */
	if (unlikely(psPhysHeapGetMemInfoOUT->eError != PVRSRV_OK))
	{
		goto PhysHeapGetMemInfo_exit;
	}

	/* If dest ptr is non-null and we have data to copy */
	if ((pasapPhysHeapMemStatsInt) &&
	    ((psPhysHeapGetMemInfoIN->ui32PhysHeapCount * sizeof(PHYS_HEAP_MEM_STATS)) > 0))
	{
		if (unlikely
		    (OSCopyToUser
		     (NULL, (void __user *)psPhysHeapGetMemInfoOUT->pasapPhysHeapMemStats,
		      pasapPhysHeapMemStatsInt,
		      (psPhysHeapGetMemInfoIN->ui32PhysHeapCount * sizeof(PHYS_HEAP_MEM_STATS))) !=
		     PVRSRV_OK))
		{
			psPhysHeapGetMemInfoOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto PhysHeapGetMemInfo_exit;
		}
	}

PhysHeapGetMemInfo_exit:

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psPhysHeapGetMemInfoOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeGetDefaultPhysicalHeap(IMG_UINT32 ui32DispatchTableEntry,
				   IMG_UINT8 * psGetDefaultPhysicalHeapIN_UI8,
				   IMG_UINT8 * psGetDefaultPhysicalHeapOUT_UI8,
				   CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_GETDEFAULTPHYSICALHEAP *psGetDefaultPhysicalHeapIN =
	    (PVRSRV_BRIDGE_IN_GETDEFAULTPHYSICALHEAP *)
	    IMG_OFFSET_ADDR(psGetDefaultPhysicalHeapIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_GETDEFAULTPHYSICALHEAP *psGetDefaultPhysicalHeapOUT =
	    (PVRSRV_BRIDGE_OUT_GETDEFAULTPHYSICALHEAP *)
	    IMG_OFFSET_ADDR(psGetDefaultPhysicalHeapOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psGetDefaultPhysicalHeapIN);

	psGetDefaultPhysicalHeapOUT->eError =
	    PVRSRVGetDefaultPhysicalHeapKM(psConnection, OSGetDevNode(psConnection),
					   &psGetDefaultPhysicalHeapOUT->eHeap);

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemGetFaultAddress(IMG_UINT32 ui32DispatchTableEntry,
				  IMG_UINT8 * psDevmemGetFaultAddressIN_UI8,
				  IMG_UINT8 * psDevmemGetFaultAddressOUT_UI8,
				  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMGETFAULTADDRESS *psDevmemGetFaultAddressIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMGETFAULTADDRESS *)
	    IMG_OFFSET_ADDR(psDevmemGetFaultAddressIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMGETFAULTADDRESS *psDevmemGetFaultAddressOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMGETFAULTADDRESS *)
	    IMG_OFFSET_ADDR(psDevmemGetFaultAddressOUT_UI8, 0);

	IMG_HANDLE hDevmemCtx = psDevmemGetFaultAddressIN->hDevmemCtx;
	DEVMEMINT_CTX *psDevmemCtxInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemGetFaultAddressOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psDevmemCtxInt,
				       hDevmemCtx, PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX, IMG_TRUE);
	if (unlikely(psDevmemGetFaultAddressOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemGetFaultAddress_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemGetFaultAddressOUT->eError =
	    DevmemIntGetFaultAddress(psConnection, OSGetDevNode(psConnection),
				     psDevmemCtxInt, &psDevmemGetFaultAddressOUT->sFaultAddress);

DevmemGetFaultAddress_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psDevmemCtxInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hDevmemCtx, PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)

static IMG_INT
PVRSRVBridgePVRSRVStatsUpdateOOMStat(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psPVRSRVStatsUpdateOOMStatIN_UI8,
				     IMG_UINT8 * psPVRSRVStatsUpdateOOMStatOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PVRSRVSTATSUPDATEOOMSTAT *psPVRSRVStatsUpdateOOMStatIN =
	    (PVRSRV_BRIDGE_IN_PVRSRVSTATSUPDATEOOMSTAT *)
	    IMG_OFFSET_ADDR(psPVRSRVStatsUpdateOOMStatIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PVRSRVSTATSUPDATEOOMSTAT *psPVRSRVStatsUpdateOOMStatOUT =
	    (PVRSRV_BRIDGE_OUT_PVRSRVSTATSUPDATEOOMSTAT *)
	    IMG_OFFSET_ADDR(psPVRSRVStatsUpdateOOMStatOUT_UI8, 0);

	psPVRSRVStatsUpdateOOMStatOUT->eError =
	    PVRSRVStatsUpdateOOMStat(psConnection, OSGetDevNode(psConnection),
				     psPVRSRVStatsUpdateOOMStatIN->ui32ui32StatType,
				     psPVRSRVStatsUpdateOOMStatIN->ui32pid);

	return 0;
}

#else
#define PVRSRVBridgePVRSRVStatsUpdateOOMStat NULL
#endif

static PVRSRV_ERROR _DevmemXIntReserveRangepsReservationIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = DevmemXIntUnreserveRange((DEVMEMXINT_RESERVATION *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeDevmemXIntReserveRange(IMG_UINT32 ui32DispatchTableEntry,
				   IMG_UINT8 * psDevmemXIntReserveRangeIN_UI8,
				   IMG_UINT8 * psDevmemXIntReserveRangeOUT_UI8,
				   CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMXINTRESERVERANGE *psDevmemXIntReserveRangeIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMXINTRESERVERANGE *)
	    IMG_OFFSET_ADDR(psDevmemXIntReserveRangeIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMXINTRESERVERANGE *psDevmemXIntReserveRangeOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMXINTRESERVERANGE *)
	    IMG_OFFSET_ADDR(psDevmemXIntReserveRangeOUT_UI8, 0);

	IMG_HANDLE hDevmemServerHeap = psDevmemXIntReserveRangeIN->hDevmemServerHeap;
	DEVMEMINT_HEAP *psDevmemServerHeapInt = NULL;
	DEVMEMXINT_RESERVATION *psReservationInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemXIntReserveRangeOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psDevmemServerHeapInt,
				       hDevmemServerHeap,
				       PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP, IMG_TRUE);
	if (unlikely(psDevmemXIntReserveRangeOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemXIntReserveRange_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemXIntReserveRangeOUT->eError =
	    DevmemXIntReserveRange(psDevmemServerHeapInt,
				   psDevmemXIntReserveRangeIN->sAddress,
				   psDevmemXIntReserveRangeIN->uiLength, &psReservationInt);
	/* Exit early if bridged call fails */
	if (unlikely(psDevmemXIntReserveRangeOUT->eError != PVRSRV_OK))
	{
		goto DevmemXIntReserveRange_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psDevmemXIntReserveRangeOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
									&psDevmemXIntReserveRangeOUT->
									hReservation,
									(void *)psReservationInt,
									PVRSRV_HANDLE_TYPE_DEVMEMXINT_RESERVATION,
									PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
									(PFN_HANDLE_RELEASE) &
									_DevmemXIntReserveRangepsReservationIntRelease);
	if (unlikely(psDevmemXIntReserveRangeOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemXIntReserveRange_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

DevmemXIntReserveRange_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psDevmemServerHeapInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hDevmemServerHeap, PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psDevmemXIntReserveRangeOUT->eError != PVRSRV_OK)
	{
		if (psReservationInt)
		{
			DevmemXIntUnreserveRange(psReservationInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemXIntUnreserveRange(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psDevmemXIntUnreserveRangeIN_UI8,
				     IMG_UINT8 * psDevmemXIntUnreserveRangeOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMXINTUNRESERVERANGE *psDevmemXIntUnreserveRangeIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMXINTUNRESERVERANGE *)
	    IMG_OFFSET_ADDR(psDevmemXIntUnreserveRangeIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMXINTUNRESERVERANGE *psDevmemXIntUnreserveRangeOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMXINTUNRESERVERANGE *)
	    IMG_OFFSET_ADDR(psDevmemXIntUnreserveRangeOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psDevmemXIntUnreserveRangeOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psDevmemXIntUnreserveRangeIN->
					      hReservation,
					      PVRSRV_HANDLE_TYPE_DEVMEMXINT_RESERVATION);
	if (unlikely
	    ((psDevmemXIntUnreserveRangeOUT->eError != PVRSRV_OK)
	     && (psDevmemXIntUnreserveRangeOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL)
	     && (psDevmemXIntUnreserveRangeOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psDevmemXIntUnreserveRangeOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemXIntUnreserveRange_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

DevmemXIntUnreserveRange_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemXIntMapPages(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psDevmemXIntMapPagesIN_UI8,
			       IMG_UINT8 * psDevmemXIntMapPagesOUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMXINTMAPPAGES *psDevmemXIntMapPagesIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMXINTMAPPAGES *) IMG_OFFSET_ADDR(psDevmemXIntMapPagesIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVMEMXINTMAPPAGES *psDevmemXIntMapPagesOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMXINTMAPPAGES *) IMG_OFFSET_ADDR(psDevmemXIntMapPagesOUT_UI8,
								     0);

	IMG_HANDLE hReservation = psDevmemXIntMapPagesIN->hReservation;
	DEVMEMXINT_RESERVATION *psReservationInt = NULL;
	IMG_HANDLE hPMR = psDevmemXIntMapPagesIN->hPMR;
	PMR *psPMRInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemXIntMapPagesOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psReservationInt,
				       hReservation,
				       PVRSRV_HANDLE_TYPE_DEVMEMXINT_RESERVATION, IMG_TRUE);
	if (unlikely(psDevmemXIntMapPagesOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemXIntMapPages_exit;
	}

	/* Look up the address from the handle */
	psDevmemXIntMapPagesOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psDevmemXIntMapPagesOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemXIntMapPages_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemXIntMapPagesOUT->eError =
	    DevmemXIntMapPages(psReservationInt,
			       psPMRInt,
			       psDevmemXIntMapPagesIN->ui32PageCount,
			       psDevmemXIntMapPagesIN->ui32PhysPageOffset,
			       psDevmemXIntMapPagesIN->uiFlags,
			       psDevmemXIntMapPagesIN->ui32VirtPageOffset);

DevmemXIntMapPages_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psReservationInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hReservation,
					    PVRSRV_HANDLE_TYPE_DEVMEMXINT_RESERVATION);
	}

	/* Unreference the previously looked up handle */
	if (psPMRInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemXIntUnmapPages(IMG_UINT32 ui32DispatchTableEntry,
				 IMG_UINT8 * psDevmemXIntUnmapPagesIN_UI8,
				 IMG_UINT8 * psDevmemXIntUnmapPagesOUT_UI8,
				 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVMEMXINTUNMAPPAGES *psDevmemXIntUnmapPagesIN =
	    (PVRSRV_BRIDGE_IN_DEVMEMXINTUNMAPPAGES *) IMG_OFFSET_ADDR(psDevmemXIntUnmapPagesIN_UI8,
								      0);
	PVRSRV_BRIDGE_OUT_DEVMEMXINTUNMAPPAGES *psDevmemXIntUnmapPagesOUT =
	    (PVRSRV_BRIDGE_OUT_DEVMEMXINTUNMAPPAGES *)
	    IMG_OFFSET_ADDR(psDevmemXIntUnmapPagesOUT_UI8, 0);

	IMG_HANDLE hReservation = psDevmemXIntUnmapPagesIN->hReservation;
	DEVMEMXINT_RESERVATION *psReservationInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevmemXIntUnmapPagesOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psReservationInt,
				       hReservation,
				       PVRSRV_HANDLE_TYPE_DEVMEMXINT_RESERVATION, IMG_TRUE);
	if (unlikely(psDevmemXIntUnmapPagesOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevmemXIntUnmapPages_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevmemXIntUnmapPagesOUT->eError =
	    DevmemXIntUnmapPages(psReservationInt,
				 psDevmemXIntUnmapPagesIN->ui32VirtPageOffset,
				 psDevmemXIntUnmapPagesIN->ui32PageCount);

DevmemXIntUnmapPages_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psReservationInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hReservation,
					    PVRSRV_HANDLE_TYPE_DEVMEMXINT_RESERVATION);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitMMBridge(void);
void DeinitMMBridge(void);

/*
 * Register all MM functions with services
 */
PVRSRV_ERROR InitMMBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMREXPORTPMR,
			      PVRSRVBridgePMRExportPMR, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNEXPORTPMR,
			      PVRSRVBridgePMRUnexportPMR, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRGETUID, PVRSRVBridgePMRGetUID,
			      NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRMAKELOCALIMPORTHANDLE,
			      PVRSRVBridgePMRMakeLocalImportHandle, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNMAKELOCALIMPORTHANDLE,
			      PVRSRVBridgePMRUnmakeLocalImportHandle, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRIMPORTPMR,
			      PVRSRVBridgePMRImportPMR, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRLOCALIMPORTPMR,
			      PVRSRVBridgePMRLocalImportPMR, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNREFPMR,
			      PVRSRVBridgePMRUnrefPMR, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNREFUNLOCKPMR,
			      PVRSRVBridgePMRUnrefUnlockPMR, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PHYSMEMNEWRAMBACKEDPMR,
			      PVRSRVBridgePhysmemNewRamBackedPMR, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTCTXCREATE,
			      PVRSRVBridgeDevmemIntCtxCreate, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTCTXDESTROY,
			      PVRSRVBridgeDevmemIntCtxDestroy, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTHEAPCREATE,
			      PVRSRVBridgeDevmemIntHeapCreate, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTHEAPDESTROY,
			      PVRSRVBridgeDevmemIntHeapDestroy, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTMAPPMR,
			      PVRSRVBridgeDevmemIntMapPMR, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTUNMAPPMR,
			      PVRSRVBridgeDevmemIntUnmapPMR, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTRESERVERANGE,
			      PVRSRVBridgeDevmemIntReserveRange, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTUNRESERVERANGE,
			      PVRSRVBridgeDevmemIntUnreserveRange, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_CHANGESPARSEMEM,
			      PVRSRVBridgeChangeSparseMem, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTMAPPAGES,
			      PVRSRVBridgeDevmemIntMapPages, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTUNMAPPAGES,
			      PVRSRVBridgeDevmemIntUnmapPages, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMISVDEVADDRVALID,
			      PVRSRVBridgeDevmemIsVDevAddrValid, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINVALIDATEFBSCTABLE,
			      PVRSRVBridgeDevmemInvalidateFBSCTable, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCONFIGCOUNT,
			      PVRSRVBridgeHeapCfgHeapConfigCount, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCOUNT,
			      PVRSRVBridgeHeapCfgHeapCount, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCONFIGNAME,
			      PVRSRVBridgeHeapCfgHeapConfigName, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPDETAILS,
			      PVRSRVBridgeHeapCfgHeapDetails, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTREGISTERPFNOTIFYKM,
			      PVRSRVBridgeDevmemIntRegisterPFNotifyKM, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PHYSHEAPGETMEMINFO,
			      PVRSRVBridgePhysHeapGetMemInfo, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_GETDEFAULTPHYSICALHEAP,
			      PVRSRVBridgeGetDefaultPhysicalHeap, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMGETFAULTADDRESS,
			      PVRSRVBridgeDevmemGetFaultAddress, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PVRSRVSTATSUPDATEOOMSTAT,
			      PVRSRVBridgePVRSRVStatsUpdateOOMStat, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMXINTRESERVERANGE,
			      PVRSRVBridgeDevmemXIntReserveRange, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMXINTUNRESERVERANGE,
			      PVRSRVBridgeDevmemXIntUnreserveRange, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMXINTMAPPAGES,
			      PVRSRVBridgeDevmemXIntMapPages, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMXINTUNMAPPAGES,
			      PVRSRVBridgeDevmemXIntUnmapPages, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all mm functions with services
 */
void DeinitMMBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMREXPORTPMR);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNEXPORTPMR);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRGETUID);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRMAKELOCALIMPORTHANDLE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNMAKELOCALIMPORTHANDLE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRIMPORTPMR);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRLOCALIMPORTPMR);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNREFPMR);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNREFUNLOCKPMR);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PHYSMEMNEWRAMBACKEDPMR);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTCTXCREATE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTCTXDESTROY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTHEAPCREATE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTHEAPDESTROY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTMAPPMR);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTUNMAPPMR);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTRESERVERANGE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTUNRESERVERANGE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_CHANGESPARSEMEM);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTMAPPAGES);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTUNMAPPAGES);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMISVDEVADDRVALID);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINVALIDATEFBSCTABLE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCONFIGCOUNT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCOUNT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCONFIGNAME);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPDETAILS);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTREGISTERPFNOTIFYKM);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PHYSHEAPGETMEMINFO);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_GETDEFAULTPHYSICALHEAP);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMGETFAULTADDRESS);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PVRSRVSTATSUPDATEOOMSTAT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMXINTRESERVERANGE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMXINTUNRESERVERANGE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMXINTMAPPAGES);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMXINTUNMAPPAGES);

}
