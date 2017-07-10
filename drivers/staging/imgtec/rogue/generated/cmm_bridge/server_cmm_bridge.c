/*************************************************************************/ /*!
@File
@Title          Server bridge for cmm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for cmm
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

#include <stddef.h>
#include <asm/uaccess.h>

#include "img_defs.h"

#include "pmr.h"
#include "devicemem_server.h"


#include "common_cmm_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>



#if !defined(EXCLUDE_CMM_BRIDGE)



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeDevmemIntExportCtx(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTEXPORTCTX *psDevmemIntExportCtxIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTEXPORTCTX *psDevmemIntExportCtxOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hContext = psDevmemIntExportCtxIN->hContext;
	DEVMEMINT_CTX * psContextInt = NULL;
	IMG_HANDLE hPMR = psDevmemIntExportCtxIN->hPMR;
	PMR * psPMRInt = NULL;
	DEVMEMINT_CTX_EXPORT * psContextExportInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psDevmemIntExportCtxOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psContextInt,
											hContext,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX,
											IMG_TRUE);
					if(psDevmemIntExportCtxOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto DevmemIntExportCtx_exit;
					}
				}





				{
					/* Look up the address from the handle */
					psDevmemIntExportCtxOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psDevmemIntExportCtxOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto DevmemIntExportCtx_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psDevmemIntExportCtxOUT->eError =
		DevmemIntExportCtx(
					psContextInt,
					psPMRInt,
					&psContextExportInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntExportCtxOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntExportCtx_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psDevmemIntExportCtxOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psDevmemIntExportCtxOUT->hContextExport,
							(void *) psContextExportInt,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&DevmemIntUnexportCtx);
	if (psDevmemIntExportCtxOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto DevmemIntExportCtx_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



DevmemIntExportCtx_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hContext,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
						}
				}





				{
					/* Unreference the previously looked up handle */
						if(psPMRInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

	if (psDevmemIntExportCtxOUT->eError != PVRSRV_OK)
	{
		if (psContextExportInt)
		{
			DevmemIntUnexportCtx(psContextExportInt);
		}
	}


	return 0;
}


static IMG_INT
PVRSRVBridgeDevmemIntUnexportCtx(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTUNEXPORTCTX *psDevmemIntUnexportCtxIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTUNEXPORTCTX *psDevmemIntUnexportCtxOUT,
					 CONNECTION_DATA *psConnection)
{









	/* Lock over handle destruction. */
	LockHandle();





	psDevmemIntUnexportCtxOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psDevmemIntUnexportCtxIN->hContextExport,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT);
	if ((psDevmemIntUnexportCtxOUT->eError != PVRSRV_OK) &&
	    (psDevmemIntUnexportCtxOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeDevmemIntUnexportCtx: %s",
		        PVRSRVGetErrorStringKM(psDevmemIntUnexportCtxOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto DevmemIntUnexportCtx_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



DevmemIntUnexportCtx_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeDevmemIntAcquireRemoteCtx(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTACQUIREREMOTECTX *psDevmemIntAcquireRemoteCtxIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTACQUIREREMOTECTX *psDevmemIntAcquireRemoteCtxOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMR = psDevmemIntAcquireRemoteCtxIN->hPMR;
	PMR * psPMRInt = NULL;
	DEVMEMINT_CTX * psContextInt = NULL;
	IMG_HANDLE hPrivDataInt = NULL;





	psDevmemIntAcquireRemoteCtxOUT->hContext = NULL;


	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psDevmemIntAcquireRemoteCtxOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psDevmemIntAcquireRemoteCtxOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto DevmemIntAcquireRemoteCtx_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psDevmemIntAcquireRemoteCtxOUT->eError =
		DevmemIntAcquireRemoteCtx(
					psPMRInt,
					&psContextInt,
					&hPrivDataInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntAcquireRemoteCtxOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntAcquireRemoteCtx_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psDevmemIntAcquireRemoteCtxOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psDevmemIntAcquireRemoteCtxOUT->hContext,
							(void *) psContextInt,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,(PFN_HANDLE_RELEASE)&DevmemIntCtxDestroy);
	if (psDevmemIntAcquireRemoteCtxOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto DevmemIntAcquireRemoteCtx_exit;
	}






	psDevmemIntAcquireRemoteCtxOUT->eError = PVRSRVAllocSubHandleUnlocked(psConnection->psHandleBase,

							&psDevmemIntAcquireRemoteCtxOUT->hPrivData,
							(void *) hPrivDataInt,
							PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psDevmemIntAcquireRemoteCtxOUT->hContext);
	if (psDevmemIntAcquireRemoteCtxOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto DevmemIntAcquireRemoteCtx_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



DevmemIntAcquireRemoteCtx_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psPMRInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

	if (psDevmemIntAcquireRemoteCtxOUT->eError != PVRSRV_OK)
	{
		/* Lock over handle creation cleanup. */
		LockHandle();
		if (psDevmemIntAcquireRemoteCtxOUT->hContext)
		{


			PVRSRV_ERROR eError = PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
						(IMG_HANDLE) psDevmemIntAcquireRemoteCtxOUT->hContext,
						PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
			if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
			{
				PVR_DPF((PVR_DBG_ERROR,
				        "PVRSRVBridgeDevmemIntAcquireRemoteCtx: %s",
				        PVRSRVGetErrorStringKM(eError)));
			}
			/* Releasing the handle should free/destroy/release the resource.
			 * This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psContextInt = NULL;
		}


		/* Release now we have cleaned up creation handles. */
		UnlockHandle();
		if (psContextInt)
		{
			DevmemIntCtxDestroy(psContextInt);
		}
	}


	return 0;
}




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;
#endif /* EXCLUDE_CMM_BRIDGE */

#if !defined(EXCLUDE_CMM_BRIDGE)
PVRSRV_ERROR InitCMMBridge(void);
PVRSRV_ERROR DeinitCMMBridge(void);

/*
 * Register all CMM functions with services
 */
PVRSRV_ERROR InitCMMBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_CMM, PVRSRV_BRIDGE_CMM_DEVMEMINTEXPORTCTX, PVRSRVBridgeDevmemIntExportCtx,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CMM, PVRSRV_BRIDGE_CMM_DEVMEMINTUNEXPORTCTX, PVRSRVBridgeDevmemIntUnexportCtx,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CMM, PVRSRV_BRIDGE_CMM_DEVMEMINTACQUIREREMOTECTX, PVRSRVBridgeDevmemIntAcquireRemoteCtx,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all cmm functions with services
 */
PVRSRV_ERROR DeinitCMMBridge(void)
{
	return PVRSRV_OK;
}
#else /* EXCLUDE_CMM_BRIDGE */
/* This bridge is conditional on EXCLUDE_CMM_BRIDGE - when defined,
 * do not populate the dispatch table with its functions
 */
#define InitCMMBridge() \
	PVRSRV_OK

#define DeinitCMMBridge() \
	PVRSRV_OK

#endif /* EXCLUDE_CMM_BRIDGE */
