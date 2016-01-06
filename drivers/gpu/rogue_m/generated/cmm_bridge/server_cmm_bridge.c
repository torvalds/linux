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

#if defined (SUPPORT_AUTH)
#include "osauth.h"
#endif

#include <linux/slab.h>


static PVRSRV_ERROR ReleaseDevMemIntCtxExport(IMG_VOID *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);

	return PVRSRV_OK;
}


/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeDevmemIntCtxExport(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTCTXEXPORT *psDevmemIntCtxExportIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTCTXEXPORT *psDevmemIntCtxExportOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_CTX * psDevMemServerContextInt = IMG_NULL;
	DEVMEMINT_CTX_EXPORT * psDevMemIntCtxExportInt = IMG_NULL;
	IMG_HANDLE hDevMemIntCtxExportInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psDevmemIntCtxExportOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psDevMemServerContextInt,
											psDevmemIntCtxExportIN->hDevMemServerContext,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
					if(psDevmemIntCtxExportOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntCtxExport_exit;
					}
				}


	psDevmemIntCtxExportOUT->eError =
		DevmemIntCtxExport(
					psDevMemServerContextInt,
					&psDevMemIntCtxExportInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntCtxExportOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxExport_exit;
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
	psDevmemIntCtxExportOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&hDevMemIntCtxExportInt,
							(IMG_VOID *) psDevMemIntCtxExportInt,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_SHARED
							,(PFN_HANDLE_RELEASE)&DevmemIntCtxUnexport);
	if (psDevmemIntCtxExportOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxExport_exit;
	}

	psDevmemIntCtxExportOUT->eError = PVRSRVAllocHandle(KERNEL_HANDLE_BASE,
							&psDevmemIntCtxExportOUT->hDevMemIntCtxExport,
							(IMG_VOID *) psDevMemIntCtxExportInt,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
							(PFN_HANDLE_RELEASE)&ReleaseDevMemIntCtxExport);
	if (psDevmemIntCtxExportOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxExport_exit;
	}



DevmemIntCtxExport_exit:
	if (psDevmemIntCtxExportOUT->eError != PVRSRV_OK)
	{
		if (psDevmemIntCtxExportOUT->hDevMemIntCtxExport)
		{
			PVRSRV_ERROR eError = PVRSRVReleaseHandle(KERNEL_HANDLE_BASE,
						(IMG_HANDLE) psDevmemIntCtxExportOUT->hDevMemIntCtxExport,
						PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT);

			/* Releasing the handle should free/destroy/release the resource. This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

		}

		if (hDevMemIntCtxExportInt)
		{
			PVRSRV_ERROR eError = PVRSRVReleaseHandle(psConnection->psHandleBase,
						hDevMemIntCtxExportInt,
						PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT);

			/* Releasing the handle should free/destroy/release the resource. This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psDevMemIntCtxExportInt = IMG_NULL;
		}

		if (psDevMemIntCtxExportInt)
		{
			DevmemIntCtxUnexport(psDevMemIntCtxExportInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntCtxUnexport(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTCTXUNEXPORT *psDevmemIntCtxUnexportIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTCTXUNEXPORT *psDevmemIntCtxUnexportOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_CTX_EXPORT * psDevMemIntCtxExportInt = IMG_NULL;
	IMG_HANDLE hDevMemIntCtxExportInt = IMG_NULL;

	PVR_UNREFERENCED_PARAMETER(psConnection);







	psDevmemIntCtxUnexportOUT->eError =
		PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
					(IMG_VOID **) &psDevMemIntCtxExportInt,
					(IMG_HANDLE) psDevmemIntCtxUnexportIN->hDevMemIntCtxExport,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT);
	PVR_ASSERT(psDevmemIntCtxUnexportOUT->eError == PVRSRV_OK);

	/*
	 * Find the connection specific handle that represents the same data
	 * as the cross process handle as releasing it will actually call the
	 * data's real release function (see the function where the cross
	 * process handle is allocated for more details).
	 */
	psDevmemIntCtxUnexportOUT->eError =
		PVRSRVFindHandle(psConnection->psHandleBase,
					&hDevMemIntCtxExportInt,
					psDevMemIntCtxExportInt,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT);
	PVR_ASSERT(psDevmemIntCtxUnexportOUT->eError == PVRSRV_OK);

	psDevmemIntCtxUnexportOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					hDevMemIntCtxExportInt,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT);
	PVR_ASSERT((psDevmemIntCtxUnexportOUT->eError == PVRSRV_OK) || (psDevmemIntCtxUnexportOUT->eError == PVRSRV_ERROR_RETRY));

	psDevmemIntCtxUnexportOUT->eError =
		PVRSRVReleaseHandle(KERNEL_HANDLE_BASE,
					(IMG_HANDLE) psDevmemIntCtxUnexportIN->hDevMemIntCtxExport,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT);
	if ((psDevmemIntCtxUnexportOUT->eError != PVRSRV_OK) && (psDevmemIntCtxUnexportOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto DevmemIntCtxUnexport_exit;
	}



DevmemIntCtxUnexport_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntCtxImport(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTCTXIMPORT *psDevmemIntCtxImportIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTCTXIMPORT *psDevmemIntCtxImportOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_CTX_EXPORT * psDevMemIntCtxExportInt = IMG_NULL;
	DEVMEMINT_CTX * psDevMemServerContextInt = IMG_NULL;
	IMG_HANDLE hPrivDataInt = IMG_NULL;



	psDevmemIntCtxImportOUT->hDevMemServerContext = IMG_NULL;




				{
					/* Look up the address from the handle */
					psDevmemIntCtxImportOUT->eError =
						PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
											(IMG_VOID **) &psDevMemIntCtxExportInt,
											psDevmemIntCtxImportIN->hDevMemIntCtxExport,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT);
					if(psDevmemIntCtxImportOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntCtxImport_exit;
					}
				}


	psDevmemIntCtxImportOUT->eError =
		DevmemIntCtxImport(
					psDevMemIntCtxExportInt,
					&psDevMemServerContextInt,
					&hPrivDataInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntCtxImportOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxImport_exit;
	}


	psDevmemIntCtxImportOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDevmemIntCtxImportOUT->hDevMemServerContext,
							(IMG_VOID *) psDevMemServerContextInt,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&DevmemIntCtxDestroy);
	if (psDevmemIntCtxImportOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxImport_exit;
	}


	psDevmemIntCtxImportOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psDevmemIntCtxImportOUT->hPrivData,
							(IMG_VOID *) hPrivDataInt,
							PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,psDevmemIntCtxImportOUT->hDevMemServerContext);
	if (psDevmemIntCtxImportOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxImport_exit;
	}




DevmemIntCtxImport_exit:
	if (psDevmemIntCtxImportOUT->eError != PVRSRV_OK)
	{
		if (psDevmemIntCtxImportOUT->hDevMemServerContext)
		{
			PVRSRV_ERROR eError = PVRSRVReleaseHandle(psConnection->psHandleBase,
						(IMG_HANDLE) psDevmemIntCtxImportOUT->hDevMemServerContext,
						PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);

			/* Releasing the handle should free/destroy/release the resource. This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psDevMemServerContextInt = IMG_NULL;
		}


		if (psDevMemServerContextInt)
		{
			DevmemIntCtxDestroy(psDevMemServerContextInt);
		}
	}


	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */


PVRSRV_ERROR InitCMMBridge(IMG_VOID);
PVRSRV_ERROR DeinitCMMBridge(IMG_VOID);

/*
 * Register all CMM functions with services
 */
PVRSRV_ERROR InitCMMBridge(IMG_VOID)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_CMM, PVRSRV_BRIDGE_CMM_DEVMEMINTCTXEXPORT, PVRSRVBridgeDevmemIntCtxExport,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CMM, PVRSRV_BRIDGE_CMM_DEVMEMINTCTXUNEXPORT, PVRSRVBridgeDevmemIntCtxUnexport,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CMM, PVRSRV_BRIDGE_CMM_DEVMEMINTCTXIMPORT, PVRSRVBridgeDevmemIntCtxImport,
					IMG_NULL, IMG_NULL,
					0, 0);


	return PVRSRV_OK;
}

/*
 * Unregister all cmm functions with services
 */
PVRSRV_ERROR DeinitCMMBridge(IMG_VOID)
{
	return PVRSRV_OK;
}

