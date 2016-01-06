/*************************************************************************/ /*!
@File
@Title          Server bridge for pvrtl
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for pvrtl
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

#include "tlserver.h"


#include "common_pvrtl_bridge.h"

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




/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeTLConnect(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_TLCONNECT *psTLConnectIN,
					  PVRSRV_BRIDGE_OUT_TLCONNECT *psTLConnectOUT,
					 CONNECTION_DATA *psConnection)
{

	PVR_UNREFERENCED_PARAMETER(psTLConnectIN);






	psTLConnectOUT->eError =
		TLServerConnectKM(psConnection
					);





	return 0;
}

static IMG_INT
PVRSRVBridgeTLDisconnect(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_TLDISCONNECT *psTLDisconnectIN,
					  PVRSRV_BRIDGE_OUT_TLDISCONNECT *psTLDisconnectOUT,
					 CONNECTION_DATA *psConnection)
{

	PVR_UNREFERENCED_PARAMETER(psTLDisconnectIN);






	psTLDisconnectOUT->eError =
		TLServerDisconnectKM(psConnection
					);





	return 0;
}

static IMG_INT
PVRSRVBridgeTLOpenStream(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_TLOPENSTREAM *psTLOpenStreamIN,
					  PVRSRV_BRIDGE_OUT_TLOPENSTREAM *psTLOpenStreamOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_CHAR *uiNameInt = IMG_NULL;
	TL_STREAM_DESC * psSDInt = IMG_NULL;
	DEVMEM_EXPORTCOOKIE * psClientBUFExportCookieInt = IMG_NULL;



	psTLOpenStreamOUT->hSD = IMG_NULL;

	
	{
		uiNameInt = OSAllocMem(PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR));
		if (!uiNameInt)
		{
			psTLOpenStreamOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto TLOpenStream_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psTLOpenStreamIN->puiName, PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR))
				|| (OSCopyFromUser(NULL, uiNameInt, psTLOpenStreamIN->puiName,
				PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR)) != PVRSRV_OK) )
			{
				psTLOpenStreamOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto TLOpenStream_exit;
			}



	psTLOpenStreamOUT->eError =
		TLServerOpenStreamKM(
					uiNameInt,
					psTLOpenStreamIN->ui32Mode,
					&psSDInt,
					&psClientBUFExportCookieInt);
	/* Exit early if bridged call fails */
	if(psTLOpenStreamOUT->eError != PVRSRV_OK)
	{
		goto TLOpenStream_exit;
	}


	psTLOpenStreamOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psTLOpenStreamOUT->hSD,
							(IMG_VOID *) psSDInt,
							PVRSRV_HANDLE_TYPE_PVR_TL_SD,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&TLServerCloseStreamKM);
	if (psTLOpenStreamOUT->eError != PVRSRV_OK)
	{
		goto TLOpenStream_exit;
	}


	psTLOpenStreamOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psTLOpenStreamOUT->hClientBUFExportCookie,
							(IMG_VOID *) psClientBUFExportCookieInt,
							PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psTLOpenStreamOUT->hSD);
	if (psTLOpenStreamOUT->eError != PVRSRV_OK)
	{
		goto TLOpenStream_exit;
	}




TLOpenStream_exit:
	if (psTLOpenStreamOUT->eError != PVRSRV_OK)
	{
		if (psTLOpenStreamOUT->hSD)
		{
			PVRSRV_ERROR eError = PVRSRVReleaseHandle(psConnection->psHandleBase,
						(IMG_HANDLE) psTLOpenStreamOUT->hSD,
						PVRSRV_HANDLE_TYPE_PVR_TL_SD);

			/* Releasing the handle should free/destroy/release the resource. This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psSDInt = IMG_NULL;
		}


		if (psSDInt)
		{
			TLServerCloseStreamKM(psSDInt);
		}
	}

	if (uiNameInt)
		OSFreeMem(uiNameInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeTLCloseStream(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_TLCLOSESTREAM *psTLCloseStreamIN,
					  PVRSRV_BRIDGE_OUT_TLCLOSESTREAM *psTLCloseStreamOUT,
					 CONNECTION_DATA *psConnection)
{









	psTLCloseStreamOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psTLCloseStreamIN->hSD,
					PVRSRV_HANDLE_TYPE_PVR_TL_SD);
	if ((psTLCloseStreamOUT->eError != PVRSRV_OK) && (psTLCloseStreamOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto TLCloseStream_exit;
	}



TLCloseStream_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeTLAcquireData(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_TLACQUIREDATA *psTLAcquireDataIN,
					  PVRSRV_BRIDGE_OUT_TLACQUIREDATA *psTLAcquireDataOUT,
					 CONNECTION_DATA *psConnection)
{
	TL_STREAM_DESC * psSDInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psTLAcquireDataOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psSDInt,
											psTLAcquireDataIN->hSD,
											PVRSRV_HANDLE_TYPE_PVR_TL_SD);
					if(psTLAcquireDataOUT->eError != PVRSRV_OK)
					{
						goto TLAcquireData_exit;
					}
				}


	psTLAcquireDataOUT->eError =
		TLServerAcquireDataKM(
					psSDInt,
					&psTLAcquireDataOUT->ui32ReadOffset,
					&psTLAcquireDataOUT->ui32ReadLen);




TLAcquireData_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeTLReleaseData(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_TLRELEASEDATA *psTLReleaseDataIN,
					  PVRSRV_BRIDGE_OUT_TLRELEASEDATA *psTLReleaseDataOUT,
					 CONNECTION_DATA *psConnection)
{
	TL_STREAM_DESC * psSDInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psTLReleaseDataOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psSDInt,
											psTLReleaseDataIN->hSD,
											PVRSRV_HANDLE_TYPE_PVR_TL_SD);
					if(psTLReleaseDataOUT->eError != PVRSRV_OK)
					{
						goto TLReleaseData_exit;
					}
				}


	psTLReleaseDataOUT->eError =
		TLServerReleaseDataKM(
					psSDInt,
					psTLReleaseDataIN->ui32ReadOffset,
					psTLReleaseDataIN->ui32ReadLen);




TLReleaseData_exit:

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */


PVRSRV_ERROR InitPVRTLBridge(IMG_VOID);
PVRSRV_ERROR DeinitPVRTLBridge(IMG_VOID);

/*
 * Register all PVRTL functions with services
 */
PVRSRV_ERROR InitPVRTLBridge(IMG_VOID)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLCONNECT, PVRSRVBridgeTLConnect,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLDISCONNECT, PVRSRVBridgeTLDisconnect,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLOPENSTREAM, PVRSRVBridgeTLOpenStream,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLCLOSESTREAM, PVRSRVBridgeTLCloseStream,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLACQUIREDATA, PVRSRVBridgeTLAcquireData,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PVRTL, PVRSRV_BRIDGE_PVRTL_TLRELEASEDATA, PVRSRVBridgeTLReleaseData,
					IMG_NULL, IMG_NULL,
					0, 0);


	return PVRSRV_OK;
}

/*
 * Unregister all pvrtl functions with services
 */
PVRSRV_ERROR DeinitPVRTLBridge(IMG_VOID)
{
	return PVRSRV_OK;
}

