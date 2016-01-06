/*************************************************************************/ /*!
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
*/ /**************************************************************************/

#include <stddef.h>
#include <asm/uaccess.h>

#include "img_defs.h"

#include "devicemem_server.h"
#include "pmr.h"
#include "devicemem_heapcfg.h"
#include "physmem.h"


#include "common_mm_bridge.h"

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


static PVRSRV_ERROR ReleasePMRExport(IMG_VOID *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);

	return PVRSRV_OK;
}
static PVRSRV_ERROR ReleasePMRExportOut(IMG_VOID *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);

	return PVRSRV_OK;
}


/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgePMRExportPMR(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMREXPORTPMR *psPMRExportPMRIN,
					  PVRSRV_BRIDGE_OUT_PMREXPORTPMR *psPMRExportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	PMR_EXPORT * psPMRExportInt = IMG_NULL;
	IMG_HANDLE hPMRExportInt = IMG_NULL;





	PMRLock();


				{
					/* Look up the address from the handle */
					psPMRExportPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psPMRInt,
											psPMRExportPMRIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRExportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRExportPMR_exit;
					}
				}


	psPMRExportPMROUT->eError =
		PMRExportPMR(
					psPMRInt,
					&psPMRExportInt,
					&psPMRExportPMROUT->ui64Size,
					&psPMRExportPMROUT->ui32Log2Contig,
					&psPMRExportPMROUT->ui64Password);
	/* Exit early if bridged call fails */
	if(psPMRExportPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PMRExportPMR_exit;
	}
	PMRUnlock();


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
	psPMRExportPMROUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&hPMRExportInt,
							(IMG_VOID *) psPMRExportInt,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_SHARED
							,(PFN_HANDLE_RELEASE)&PMRUnexportPMR);
	if (psPMRExportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRExportPMR_exit;
	}

	psPMRExportPMROUT->eError = PVRSRVAllocHandle(KERNEL_HANDLE_BASE,
							&psPMRExportPMROUT->hPMRExport,
							(IMG_VOID *) psPMRExportInt,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
							(PFN_HANDLE_RELEASE)&ReleasePMRExport);
	if (psPMRExportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRExportPMR_exit;
	}



PMRExportPMR_exit:
	if (psPMRExportPMROUT->eError != PVRSRV_OK)
	{
		if (psPMRExportPMROUT->hPMRExport)
		{
			PVRSRV_ERROR eError = PVRSRVReleaseHandle(KERNEL_HANDLE_BASE,
						(IMG_HANDLE) psPMRExportPMROUT->hPMRExport,
						PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);

			/* Releasing the handle should free/destroy/release the resource. This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

		}

		if (hPMRExportInt)
		{
			PVRSRV_ERROR eError = PVRSRVReleaseHandle(psConnection->psHandleBase,
						hPMRExportInt,
						PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);

			/* Releasing the handle should free/destroy/release the resource. This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psPMRExportInt = IMG_NULL;
		}

		if (psPMRExportInt)
		{
			PMRUnexportPMR(psPMRExportInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgePMRUnexportPMR(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRUNEXPORTPMR *psPMRUnexportPMRIN,
					  PVRSRV_BRIDGE_OUT_PMRUNEXPORTPMR *psPMRUnexportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	PMR_EXPORT * psPMRExportInt = IMG_NULL;
	IMG_HANDLE hPMRExportInt = IMG_NULL;

	PVR_UNREFERENCED_PARAMETER(psConnection);




	PMRLock();



	psPMRUnexportPMROUT->eError =
		PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
					(IMG_VOID **) &psPMRExportInt,
					(IMG_HANDLE) psPMRUnexportPMRIN->hPMRExport,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	PVR_ASSERT(psPMRUnexportPMROUT->eError == PVRSRV_OK);

	/*
	 * Find the connection specific handle that represents the same data
	 * as the cross process handle as releasing it will actually call the
	 * data's real release function (see the function where the cross
	 * process handle is allocated for more details).
	 */
	psPMRUnexportPMROUT->eError =
		PVRSRVFindHandle(psConnection->psHandleBase,
					&hPMRExportInt,
					psPMRExportInt,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	PVR_ASSERT(psPMRUnexportPMROUT->eError == PVRSRV_OK);

	psPMRUnexportPMROUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					hPMRExportInt,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	PVR_ASSERT((psPMRUnexportPMROUT->eError == PVRSRV_OK) || (psPMRUnexportPMROUT->eError == PVRSRV_ERROR_RETRY));

	psPMRUnexportPMROUT->eError =
		PVRSRVReleaseHandle(KERNEL_HANDLE_BASE,
					(IMG_HANDLE) psPMRUnexportPMRIN->hPMRExport,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	if ((psPMRUnexportPMROUT->eError != PVRSRV_OK) && (psPMRUnexportPMROUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		PMRUnlock();
		goto PMRUnexportPMR_exit;
	}

	PMRUnlock();


PMRUnexportPMR_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRGetUID(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRGETUID *psPMRGetUIDIN,
					  PVRSRV_BRIDGE_OUT_PMRGETUID *psPMRGetUIDOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;





	PMRLock();


				{
					/* Look up the address from the handle */
					psPMRGetUIDOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psPMRInt,
											psPMRGetUIDIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRGetUIDOUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRGetUID_exit;
					}
				}


	psPMRGetUIDOUT->eError =
		PMRGetUID(
					psPMRInt,
					&psPMRGetUIDOUT->ui64UID);
	PMRUnlock();




PMRGetUID_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRMakeServerExportClientExport(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRMAKESERVEREXPORTCLIENTEXPORT *psPMRMakeServerExportClientExportIN,
					  PVRSRV_BRIDGE_OUT_PMRMAKESERVEREXPORTCLIENTEXPORT *psPMRMakeServerExportClientExportOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEM_EXPORTCOOKIE * psPMRServerExportInt = IMG_NULL;
	PMR_EXPORT * psPMRExportOutInt = IMG_NULL;
	IMG_HANDLE hPMRExportOutInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psPMRMakeServerExportClientExportOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psPMRServerExportInt,
											psPMRMakeServerExportClientExportIN->hPMRServerExport,
											PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE);
					if(psPMRMakeServerExportClientExportOUT->eError != PVRSRV_OK)
					{
						goto PMRMakeServerExportClientExport_exit;
					}
				}


	psPMRMakeServerExportClientExportOUT->eError =
		PMRMakeServerExportClientExport(
					psPMRServerExportInt,
					&psPMRExportOutInt,
					&psPMRMakeServerExportClientExportOUT->ui64Size,
					&psPMRMakeServerExportClientExportOUT->ui32Log2Contig,
					&psPMRMakeServerExportClientExportOUT->ui64Password);
	/* Exit early if bridged call fails */
	if(psPMRMakeServerExportClientExportOUT->eError != PVRSRV_OK)
	{
		goto PMRMakeServerExportClientExport_exit;
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
	psPMRMakeServerExportClientExportOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&hPMRExportOutInt,
							(IMG_VOID *) psPMRExportOutInt,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_SHARED
							,(PFN_HANDLE_RELEASE)&PMRUnmakeServerExportClientExport);
	if (psPMRMakeServerExportClientExportOUT->eError != PVRSRV_OK)
	{
		goto PMRMakeServerExportClientExport_exit;
	}

	psPMRMakeServerExportClientExportOUT->eError = PVRSRVAllocHandle(KERNEL_HANDLE_BASE,
							&psPMRMakeServerExportClientExportOUT->hPMRExportOut,
							(IMG_VOID *) psPMRExportOutInt,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
							(PFN_HANDLE_RELEASE)&ReleasePMRExportOut);
	if (psPMRMakeServerExportClientExportOUT->eError != PVRSRV_OK)
	{
		goto PMRMakeServerExportClientExport_exit;
	}



PMRMakeServerExportClientExport_exit:
	if (psPMRMakeServerExportClientExportOUT->eError != PVRSRV_OK)
	{
		if (psPMRMakeServerExportClientExportOUT->hPMRExportOut)
		{
			PVRSRV_ERROR eError = PVRSRVReleaseHandle(KERNEL_HANDLE_BASE,
						(IMG_HANDLE) psPMRMakeServerExportClientExportOUT->hPMRExportOut,
						PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);

			/* Releasing the handle should free/destroy/release the resource. This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

		}

		if (hPMRExportOutInt)
		{
			PVRSRV_ERROR eError = PVRSRVReleaseHandle(psConnection->psHandleBase,
						hPMRExportOutInt,
						PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);

			/* Releasing the handle should free/destroy/release the resource. This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psPMRExportOutInt = IMG_NULL;
		}

		if (psPMRExportOutInt)
		{
			PMRUnmakeServerExportClientExport(psPMRExportOutInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgePMRUnmakeServerExportClientExport(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRUNMAKESERVEREXPORTCLIENTEXPORT *psPMRUnmakeServerExportClientExportIN,
					  PVRSRV_BRIDGE_OUT_PMRUNMAKESERVEREXPORTCLIENTEXPORT *psPMRUnmakeServerExportClientExportOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR_EXPORT * psPMRExportInt = IMG_NULL;
	IMG_HANDLE hPMRExportInt = IMG_NULL;

	PVR_UNREFERENCED_PARAMETER(psConnection);







	psPMRUnmakeServerExportClientExportOUT->eError =
		PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
					(IMG_VOID **) &psPMRExportInt,
					(IMG_HANDLE) psPMRUnmakeServerExportClientExportIN->hPMRExport,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	PVR_ASSERT(psPMRUnmakeServerExportClientExportOUT->eError == PVRSRV_OK);

	/*
	 * Find the connection specific handle that represents the same data
	 * as the cross process handle as releasing it will actually call the
	 * data's real release function (see the function where the cross
	 * process handle is allocated for more details).
	 */
	psPMRUnmakeServerExportClientExportOUT->eError =
		PVRSRVFindHandle(psConnection->psHandleBase,
					&hPMRExportInt,
					psPMRExportInt,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	PVR_ASSERT(psPMRUnmakeServerExportClientExportOUT->eError == PVRSRV_OK);

	psPMRUnmakeServerExportClientExportOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					hPMRExportInt,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	PVR_ASSERT((psPMRUnmakeServerExportClientExportOUT->eError == PVRSRV_OK) || (psPMRUnmakeServerExportClientExportOUT->eError == PVRSRV_ERROR_RETRY));

	psPMRUnmakeServerExportClientExportOUT->eError =
		PVRSRVReleaseHandle(KERNEL_HANDLE_BASE,
					(IMG_HANDLE) psPMRUnmakeServerExportClientExportIN->hPMRExport,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
	if ((psPMRUnmakeServerExportClientExportOUT->eError != PVRSRV_OK) && (psPMRUnmakeServerExportClientExportOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto PMRUnmakeServerExportClientExport_exit;
	}



PMRUnmakeServerExportClientExport_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRImportPMR(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRIMPORTPMR *psPMRImportPMRIN,
					  PVRSRV_BRIDGE_OUT_PMRIMPORTPMR *psPMRImportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	PMR_EXPORT * psPMRExportInt = IMG_NULL;
	PMR * psPMRInt = IMG_NULL;




#if defined (SUPPORT_AUTH)
	psPMRImportPMROUT->eError = OSCheckAuthentication(psConnection, 1);
	if (psPMRImportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRImportPMR_exit;
	}
#endif

	PMRLock();


				{
					/* Look up the address from the handle */
					psPMRImportPMROUT->eError =
						PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
											(IMG_VOID **) &psPMRExportInt,
											psPMRImportPMRIN->hPMRExport,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT);
					if(psPMRImportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRImportPMR_exit;
					}
				}


	psPMRImportPMROUT->eError =
		PMRImportPMR(
					psPMRExportInt,
					psPMRImportPMRIN->ui64uiPassword,
					psPMRImportPMRIN->ui64uiSize,
					psPMRImportPMRIN->ui32uiLog2Contig,
					&psPMRInt);
	/* Exit early if bridged call fails */
	if(psPMRImportPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PMRImportPMR_exit;
	}
	PMRUnlock();


	psPMRImportPMROUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psPMRImportPMROUT->hPMR,
							(IMG_VOID *) psPMRInt,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&PMRUnrefPMR);
	if (psPMRImportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRImportPMR_exit;
	}




PMRImportPMR_exit:
	if (psPMRImportPMROUT->eError != PVRSRV_OK)
	{
		if (psPMRInt)
		{
			PMRUnrefPMR(psPMRInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntCtxCreate(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTCTXCREATE *psDevmemIntCtxCreateIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTCTXCREATE *psDevmemIntCtxCreateOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	DEVMEMINT_CTX * psDevMemServerContextInt = IMG_NULL;
	IMG_HANDLE hPrivDataInt = IMG_NULL;



	psDevmemIntCtxCreateOUT->hDevMemServerContext = IMG_NULL;




				{
					/* Look up the address from the handle */
					psDevmemIntCtxCreateOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDeviceNodeInt,
											psDevmemIntCtxCreateIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntCtxCreate_exit;
					}
				}


	psDevmemIntCtxCreateOUT->eError =
		DevmemIntCtxCreate(
					hDeviceNodeInt,
					&psDevMemServerContextInt,
					&hPrivDataInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxCreate_exit;
	}


	psDevmemIntCtxCreateOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDevmemIntCtxCreateOUT->hDevMemServerContext,
							(IMG_VOID *) psDevMemServerContextInt,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&DevmemIntCtxDestroy);
	if (psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxCreate_exit;
	}


	psDevmemIntCtxCreateOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psDevmemIntCtxCreateOUT->hPrivData,
							(IMG_VOID *) hPrivDataInt,
							PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,psDevmemIntCtxCreateOUT->hDevMemServerContext);
	if (psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntCtxCreate_exit;
	}




DevmemIntCtxCreate_exit:
	if (psDevmemIntCtxCreateOUT->eError != PVRSRV_OK)
	{
		if (psDevmemIntCtxCreateOUT->hDevMemServerContext)
		{
			PVRSRV_ERROR eError = PVRSRVReleaseHandle(psConnection->psHandleBase,
						(IMG_HANDLE) psDevmemIntCtxCreateOUT->hDevMemServerContext,
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

static IMG_INT
PVRSRVBridgeDevmemIntCtxDestroy(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTCTXDESTROY *psDevmemIntCtxDestroyIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTCTXDESTROY *psDevmemIntCtxDestroyOUT,
					 CONNECTION_DATA *psConnection)
{









	psDevmemIntCtxDestroyOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDevmemIntCtxDestroyIN->hDevmemServerContext,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
	if ((psDevmemIntCtxDestroyOUT->eError != PVRSRV_OK) && (psDevmemIntCtxDestroyOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto DevmemIntCtxDestroy_exit;
	}



DevmemIntCtxDestroy_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntHeapCreate(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTHEAPCREATE *psDevmemIntHeapCreateIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTHEAPCREATE *psDevmemIntHeapCreateOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_CTX * psDevmemCtxInt = IMG_NULL;
	DEVMEMINT_HEAP * psDevmemHeapPtrInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psDevmemIntHeapCreateOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psDevmemCtxInt,
											psDevmemIntHeapCreateIN->hDevmemCtx,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
					if(psDevmemIntHeapCreateOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntHeapCreate_exit;
					}
				}


	psDevmemIntHeapCreateOUT->eError =
		DevmemIntHeapCreate(
					psDevmemCtxInt,
					psDevmemIntHeapCreateIN->sHeapBaseAddr,
					psDevmemIntHeapCreateIN->uiHeapLength,
					psDevmemIntHeapCreateIN->ui32Log2DataPageSize,
					&psDevmemHeapPtrInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntHeapCreateOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntHeapCreate_exit;
	}


	psDevmemIntHeapCreateOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDevmemIntHeapCreateOUT->hDevmemHeapPtr,
							(IMG_VOID *) psDevmemHeapPtrInt,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&DevmemIntHeapDestroy);
	if (psDevmemIntHeapCreateOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntHeapCreate_exit;
	}




DevmemIntHeapCreate_exit:
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
					  PVRSRV_BRIDGE_IN_DEVMEMINTHEAPDESTROY *psDevmemIntHeapDestroyIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTHEAPDESTROY *psDevmemIntHeapDestroyOUT,
					 CONNECTION_DATA *psConnection)
{









	psDevmemIntHeapDestroyOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDevmemIntHeapDestroyIN->hDevmemHeap,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
	if ((psDevmemIntHeapDestroyOUT->eError != PVRSRV_OK) && (psDevmemIntHeapDestroyOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto DevmemIntHeapDestroy_exit;
	}



DevmemIntHeapDestroy_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntMapPMR(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTMAPPMR *psDevmemIntMapPMRIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTMAPPMR *psDevmemIntMapPMROUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_HEAP * psDevmemServerHeapInt = IMG_NULL;
	DEVMEMINT_RESERVATION * psReservationInt = IMG_NULL;
	PMR * psPMRInt = IMG_NULL;
	DEVMEMINT_MAPPING * psMappingInt = IMG_NULL;





	PMRLock();


				{
					/* Look up the address from the handle */
					psDevmemIntMapPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psDevmemServerHeapInt,
											psDevmemIntMapPMRIN->hDevmemServerHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
					if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto DevmemIntMapPMR_exit;
					}
				}


				{
					/* Look up the address from the handle */
					psDevmemIntMapPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psReservationInt,
											psDevmemIntMapPMRIN->hReservation,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
					if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto DevmemIntMapPMR_exit;
					}
				}


				{
					/* Look up the address from the handle */
					psDevmemIntMapPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psPMRInt,
											psDevmemIntMapPMRIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto DevmemIntMapPMR_exit;
					}
				}


	psDevmemIntMapPMROUT->eError =
		DevmemIntMapPMR(
					psDevmemServerHeapInt,
					psReservationInt,
					psPMRInt,
					psDevmemIntMapPMRIN->uiMapFlags,
					&psMappingInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntMapPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto DevmemIntMapPMR_exit;
	}
	PMRUnlock();


	psDevmemIntMapPMROUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDevmemIntMapPMROUT->hMapping,
							(IMG_VOID *) psMappingInt,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_MAPPING,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&DevmemIntUnmapPMR);
	if (psDevmemIntMapPMROUT->eError != PVRSRV_OK)
	{
		goto DevmemIntMapPMR_exit;
	}




DevmemIntMapPMR_exit:
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
					  PVRSRV_BRIDGE_IN_DEVMEMINTUNMAPPMR *psDevmemIntUnmapPMRIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTUNMAPPMR *psDevmemIntUnmapPMROUT,
					 CONNECTION_DATA *psConnection)
{





	PMRLock();




	psDevmemIntUnmapPMROUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDevmemIntUnmapPMRIN->hMapping,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_MAPPING);
	if ((psDevmemIntUnmapPMROUT->eError != PVRSRV_OK) && (psDevmemIntUnmapPMROUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		PMRUnlock();
		goto DevmemIntUnmapPMR_exit;
	}

	PMRUnlock();


DevmemIntUnmapPMR_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntReserveRange(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTRESERVERANGE *psDevmemIntReserveRangeIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTRESERVERANGE *psDevmemIntReserveRangeOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_HEAP * psDevmemServerHeapInt = IMG_NULL;
	DEVMEMINT_RESERVATION * psReservationInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psDevmemIntReserveRangeOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psDevmemServerHeapInt,
											psDevmemIntReserveRangeIN->hDevmemServerHeap,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP);
					if(psDevmemIntReserveRangeOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntReserveRange_exit;
					}
				}


	psDevmemIntReserveRangeOUT->eError =
		DevmemIntReserveRange(
					psDevmemServerHeapInt,
					psDevmemIntReserveRangeIN->sAddress,
					psDevmemIntReserveRangeIN->uiLength,
					&psReservationInt);
	/* Exit early if bridged call fails */
	if(psDevmemIntReserveRangeOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntReserveRange_exit;
	}


	psDevmemIntReserveRangeOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psDevmemIntReserveRangeOUT->hReservation,
							(IMG_VOID *) psReservationInt,
							PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&DevmemIntUnreserveRange);
	if (psDevmemIntReserveRangeOUT->eError != PVRSRV_OK)
	{
		goto DevmemIntReserveRange_exit;
	}




DevmemIntReserveRange_exit:
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
					  PVRSRV_BRIDGE_IN_DEVMEMINTUNRESERVERANGE *psDevmemIntUnreserveRangeIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTUNRESERVERANGE *psDevmemIntUnreserveRangeOUT,
					 CONNECTION_DATA *psConnection)
{









	psDevmemIntUnreserveRangeOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psDevmemIntUnreserveRangeIN->hReservation,
					PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION);
	if ((psDevmemIntUnreserveRangeOUT->eError != PVRSRV_OK) && (psDevmemIntUnreserveRangeOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto DevmemIntUnreserveRange_exit;
	}



DevmemIntUnreserveRange_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePhysmemNewRamBackedPMR(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PHYSMEMNEWRAMBACKEDPMR *psPhysmemNewRamBackedPMRIN,
					  PVRSRV_BRIDGE_OUT_PHYSMEMNEWRAMBACKEDPMR *psPhysmemNewRamBackedPMROUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	IMG_BOOL *bMappingTableInt = IMG_NULL;
	PMR * psPMRPtrInt = IMG_NULL;




	if (psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks != 0)
	{
		bMappingTableInt = OSAllocMem(psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks * sizeof(IMG_BOOL));
		if (!bMappingTableInt)
		{
			psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto PhysmemNewRamBackedPMR_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPhysmemNewRamBackedPMRIN->pbMappingTable, psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks * sizeof(IMG_BOOL))
				|| (OSCopyFromUser(NULL, bMappingTableInt, psPhysmemNewRamBackedPMRIN->pbMappingTable,
				psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks * sizeof(IMG_BOOL)) != PVRSRV_OK) )
			{
				psPhysmemNewRamBackedPMROUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto PhysmemNewRamBackedPMR_exit;
			}

	PMRLock();


				{
					/* Look up the address from the handle */
					psPhysmemNewRamBackedPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDeviceNodeInt,
											psPhysmemNewRamBackedPMRIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PhysmemNewRamBackedPMR_exit;
					}
				}


	psPhysmemNewRamBackedPMROUT->eError =
		PhysmemNewRamBackedPMR(
					hDeviceNodeInt,
					psPhysmemNewRamBackedPMRIN->uiSize,
					psPhysmemNewRamBackedPMRIN->uiChunkSize,
					psPhysmemNewRamBackedPMRIN->ui32NumPhysChunks,
					psPhysmemNewRamBackedPMRIN->ui32NumVirtChunks,
					bMappingTableInt,
					psPhysmemNewRamBackedPMRIN->ui32Log2PageSize,
					psPhysmemNewRamBackedPMRIN->uiFlags,
					&psPMRPtrInt);
	/* Exit early if bridged call fails */
	if(psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PhysmemNewRamBackedPMR_exit;
	}
	PMRUnlock();


	psPhysmemNewRamBackedPMROUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psPhysmemNewRamBackedPMROUT->hPMRPtr,
							(IMG_VOID *) psPMRPtrInt,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&PMRUnrefPMR);
	if (psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK)
	{
		goto PhysmemNewRamBackedPMR_exit;
	}




PhysmemNewRamBackedPMR_exit:
	if (psPhysmemNewRamBackedPMROUT->eError != PVRSRV_OK)
	{
		if (psPMRPtrInt)
		{
			PMRUnrefPMR(psPMRPtrInt);
		}
	}

	if (bMappingTableInt)
		OSFreeMem(bMappingTableInt);

	return 0;
}

static IMG_INT
PVRSRVBridgePMRLocalImportPMR(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRLOCALIMPORTPMR *psPMRLocalImportPMRIN,
					  PVRSRV_BRIDGE_OUT_PMRLOCALIMPORTPMR *psPMRLocalImportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psExtHandleInt = IMG_NULL;
	PMR * psPMRInt = IMG_NULL;





	PMRLock();


				{
					/* Look up the address from the handle */
					psPMRLocalImportPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psExtHandleInt,
											psPMRLocalImportPMRIN->hExtHandle,
											PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT);
					if(psPMRLocalImportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRLocalImportPMR_exit;
					}
				}


	psPMRLocalImportPMROUT->eError =
		PMRLocalImportPMR(
					psExtHandleInt,
					&psPMRInt,
					&psPMRLocalImportPMROUT->uiSize,
					&psPMRLocalImportPMROUT->sAlign);
	/* Exit early if bridged call fails */
	if(psPMRLocalImportPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PMRLocalImportPMR_exit;
	}
	PMRUnlock();


	psPMRLocalImportPMROUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psPMRLocalImportPMROUT->hPMR,
							(IMG_VOID *) psPMRInt,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&PMRUnrefPMR);
	if (psPMRLocalImportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRLocalImportPMR_exit;
	}




PMRLocalImportPMR_exit:
	if (psPMRLocalImportPMROUT->eError != PVRSRV_OK)
	{
		if (psPMRInt)
		{
			PMRUnrefPMR(psPMRInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgePMRUnrefPMR(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRUNREFPMR *psPMRUnrefPMRIN,
					  PVRSRV_BRIDGE_OUT_PMRUNREFPMR *psPMRUnrefPMROUT,
					 CONNECTION_DATA *psConnection)
{





	PMRLock();




	psPMRUnrefPMROUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psPMRUnrefPMRIN->hPMR,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	if ((psPMRUnrefPMROUT->eError != PVRSRV_OK) && (psPMRUnrefPMROUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		PMRUnlock();
		goto PMRUnrefPMR_exit;
	}

	PMRUnlock();


PMRUnrefPMR_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemSLCFlushInvalRequest(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMSLCFLUSHINVALREQUEST *psDevmemSLCFlushInvalRequestIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMSLCFLUSHINVALREQUEST *psDevmemSLCFlushInvalRequestOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	PMR * psPmrInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psDevmemSLCFlushInvalRequestOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDeviceNodeInt,
											psDevmemSLCFlushInvalRequestIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psDevmemSLCFlushInvalRequestOUT->eError != PVRSRV_OK)
					{
						goto DevmemSLCFlushInvalRequest_exit;
					}
				}


				{
					/* Look up the address from the handle */
					psDevmemSLCFlushInvalRequestOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psPmrInt,
											psDevmemSLCFlushInvalRequestIN->hPmr,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psDevmemSLCFlushInvalRequestOUT->eError != PVRSRV_OK)
					{
						goto DevmemSLCFlushInvalRequest_exit;
					}
				}


	psDevmemSLCFlushInvalRequestOUT->eError =
		DevmemSLCFlushInvalRequest(
					hDeviceNodeInt,
					psPmrInt);




DevmemSLCFlushInvalRequest_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIsVDevAddrValid(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMISVDEVADDRVALID *psDevmemIsVDevAddrValidIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMISVDEVADDRVALID *psDevmemIsVDevAddrValidOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_CTX * psDevmemCtxInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psDevmemIsVDevAddrValidOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psDevmemCtxInt,
											psDevmemIsVDevAddrValidIN->hDevmemCtx,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
					if(psDevmemIsVDevAddrValidOUT->eError != PVRSRV_OK)
					{
						goto DevmemIsVDevAddrValid_exit;
					}
				}


	psDevmemIsVDevAddrValidOUT->eError =
		DevmemIntIsVDevAddrValid(
					psDevmemCtxInt,
					psDevmemIsVDevAddrValidIN->sAddress);




DevmemIsVDevAddrValid_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeHeapCfgHeapConfigCount(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_HEAPCFGHEAPCONFIGCOUNT *psHeapCfgHeapConfigCountIN,
					  PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCONFIGCOUNT *psHeapCfgHeapConfigCountOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psHeapCfgHeapConfigCountOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDeviceNodeInt,
											psHeapCfgHeapConfigCountIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psHeapCfgHeapConfigCountOUT->eError != PVRSRV_OK)
					{
						goto HeapCfgHeapConfigCount_exit;
					}
				}


	psHeapCfgHeapConfigCountOUT->eError =
		HeapCfgHeapConfigCount(
					hDeviceNodeInt,
					&psHeapCfgHeapConfigCountOUT->ui32NumHeapConfigs);




HeapCfgHeapConfigCount_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeHeapCfgHeapCount(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_HEAPCFGHEAPCOUNT *psHeapCfgHeapCountIN,
					  PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCOUNT *psHeapCfgHeapCountOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psHeapCfgHeapCountOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDeviceNodeInt,
											psHeapCfgHeapCountIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psHeapCfgHeapCountOUT->eError != PVRSRV_OK)
					{
						goto HeapCfgHeapCount_exit;
					}
				}


	psHeapCfgHeapCountOUT->eError =
		HeapCfgHeapCount(
					hDeviceNodeInt,
					psHeapCfgHeapCountIN->ui32HeapConfigIndex,
					&psHeapCfgHeapCountOUT->ui32NumHeaps);




HeapCfgHeapCount_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeHeapCfgHeapConfigName(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_HEAPCFGHEAPCONFIGNAME *psHeapCfgHeapConfigNameIN,
					  PVRSRV_BRIDGE_OUT_HEAPCFGHEAPCONFIGNAME *psHeapCfgHeapConfigNameOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	IMG_CHAR *puiHeapConfigNameInt = IMG_NULL;


	psHeapCfgHeapConfigNameOUT->puiHeapConfigName = psHeapCfgHeapConfigNameIN->puiHeapConfigName;


	if (psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz != 0)
	{
		puiHeapConfigNameInt = OSAllocMem(psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz * sizeof(IMG_CHAR));
		if (!puiHeapConfigNameInt)
		{
			psHeapCfgHeapConfigNameOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto HeapCfgHeapConfigName_exit;
		}
	}




				{
					/* Look up the address from the handle */
					psHeapCfgHeapConfigNameOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDeviceNodeInt,
											psHeapCfgHeapConfigNameIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psHeapCfgHeapConfigNameOUT->eError != PVRSRV_OK)
					{
						goto HeapCfgHeapConfigName_exit;
					}
				}


	psHeapCfgHeapConfigNameOUT->eError =
		HeapCfgHeapConfigName(
					hDeviceNodeInt,
					psHeapCfgHeapConfigNameIN->ui32HeapConfigIndex,
					psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz,
					puiHeapConfigNameInt);



	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psHeapCfgHeapConfigNameOUT->puiHeapConfigName, (psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz * sizeof(IMG_CHAR))) 
		|| (OSCopyToUser(NULL, psHeapCfgHeapConfigNameOUT->puiHeapConfigName, puiHeapConfigNameInt,
		(psHeapCfgHeapConfigNameIN->ui32HeapConfigNameBufSz * sizeof(IMG_CHAR))) != PVRSRV_OK) )
	{
		psHeapCfgHeapConfigNameOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto HeapCfgHeapConfigName_exit;
	}


HeapCfgHeapConfigName_exit:
	if (puiHeapConfigNameInt)
		OSFreeMem(puiHeapConfigNameInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeHeapCfgHeapDetails(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_HEAPCFGHEAPDETAILS *psHeapCfgHeapDetailsIN,
					  PVRSRV_BRIDGE_OUT_HEAPCFGHEAPDETAILS *psHeapCfgHeapDetailsOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	IMG_CHAR *puiHeapNameOutInt = IMG_NULL;


	psHeapCfgHeapDetailsOUT->puiHeapNameOut = psHeapCfgHeapDetailsIN->puiHeapNameOut;


	if (psHeapCfgHeapDetailsIN->ui32HeapNameBufSz != 0)
	{
		puiHeapNameOutInt = OSAllocMem(psHeapCfgHeapDetailsIN->ui32HeapNameBufSz * sizeof(IMG_CHAR));
		if (!puiHeapNameOutInt)
		{
			psHeapCfgHeapDetailsOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto HeapCfgHeapDetails_exit;
		}
	}




				{
					/* Look up the address from the handle */
					psHeapCfgHeapDetailsOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDeviceNodeInt,
											psHeapCfgHeapDetailsIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psHeapCfgHeapDetailsOUT->eError != PVRSRV_OK)
					{
						goto HeapCfgHeapDetails_exit;
					}
				}


	psHeapCfgHeapDetailsOUT->eError =
		HeapCfgHeapDetails(
					hDeviceNodeInt,
					psHeapCfgHeapDetailsIN->ui32HeapConfigIndex,
					psHeapCfgHeapDetailsIN->ui32HeapIndex,
					psHeapCfgHeapDetailsIN->ui32HeapNameBufSz,
					puiHeapNameOutInt,
					&psHeapCfgHeapDetailsOUT->sDevVAddrBase,
					&psHeapCfgHeapDetailsOUT->uiHeapLength,
					&psHeapCfgHeapDetailsOUT->ui32Log2DataPageSizeOut,
					&psHeapCfgHeapDetailsOUT->ui32Log2ImportAlignmentOut);



	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psHeapCfgHeapDetailsOUT->puiHeapNameOut, (psHeapCfgHeapDetailsIN->ui32HeapNameBufSz * sizeof(IMG_CHAR))) 
		|| (OSCopyToUser(NULL, psHeapCfgHeapDetailsOUT->puiHeapNameOut, puiHeapNameOutInt,
		(psHeapCfgHeapDetailsIN->ui32HeapNameBufSz * sizeof(IMG_CHAR))) != PVRSRV_OK) )
	{
		psHeapCfgHeapDetailsOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto HeapCfgHeapDetails_exit;
	}


HeapCfgHeapDetails_exit:
	if (puiHeapNameOutInt)
		OSFreeMem(puiHeapNameOutInt);

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */


PVRSRV_ERROR InitMMBridge(IMG_VOID);
PVRSRV_ERROR DeinitMMBridge(IMG_VOID);

/*
 * Register all MM functions with services
 */
PVRSRV_ERROR InitMMBridge(IMG_VOID)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMREXPORTPMR, PVRSRVBridgePMRExportPMR,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNEXPORTPMR, PVRSRVBridgePMRUnexportPMR,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRGETUID, PVRSRVBridgePMRGetUID,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRMAKESERVEREXPORTCLIENTEXPORT, PVRSRVBridgePMRMakeServerExportClientExport,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNMAKESERVEREXPORTCLIENTEXPORT, PVRSRVBridgePMRUnmakeServerExportClientExport,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRIMPORTPMR, PVRSRVBridgePMRImportPMR,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTCTXCREATE, PVRSRVBridgeDevmemIntCtxCreate,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTCTXDESTROY, PVRSRVBridgeDevmemIntCtxDestroy,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTHEAPCREATE, PVRSRVBridgeDevmemIntHeapCreate,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTHEAPDESTROY, PVRSRVBridgeDevmemIntHeapDestroy,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTMAPPMR, PVRSRVBridgeDevmemIntMapPMR,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTUNMAPPMR, PVRSRVBridgeDevmemIntUnmapPMR,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTRESERVERANGE, PVRSRVBridgeDevmemIntReserveRange,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMINTUNRESERVERANGE, PVRSRVBridgeDevmemIntUnreserveRange,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PHYSMEMNEWRAMBACKEDPMR, PVRSRVBridgePhysmemNewRamBackedPMR,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRLOCALIMPORTPMR, PVRSRVBridgePMRLocalImportPMR,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_PMRUNREFPMR, PVRSRVBridgePMRUnrefPMR,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMSLCFLUSHINVALREQUEST, PVRSRVBridgeDevmemSLCFlushInvalRequest,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_DEVMEMISVDEVADDRVALID, PVRSRVBridgeDevmemIsVDevAddrValid,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCONFIGCOUNT, PVRSRVBridgeHeapCfgHeapConfigCount,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCOUNT, PVRSRVBridgeHeapCfgHeapCount,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPCONFIGNAME, PVRSRVBridgeHeapCfgHeapConfigName,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MM, PVRSRV_BRIDGE_MM_HEAPCFGHEAPDETAILS, PVRSRVBridgeHeapCfgHeapDetails,
					IMG_NULL, IMG_NULL,
					0, 0);


	return PVRSRV_OK;
}

/*
 * Unregister all mm functions with services
 */
PVRSRV_ERROR DeinitMMBridge(IMG_VOID)
{
	return PVRSRV_OK;
}

