/*************************************************************************/ /*!
@File
@Title          Server side connection management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Handles connections coming from the client and the management
                connection based information
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

#include "resman.h"
#include "handle.h"
#include "pvrsrv.h"
#include "connection_server.h"
#include "osconnection_server.h"
#include "allocmem.h"
#include "pvr_debug.h"
#include "sync_server.h"
#include "process_stats.h"
#include "pdump_km.h"

/*!
******************************************************************************

 @Function	FreeConnectionData

 @Description	Free a connection data area

 @Input		psConnection - pointer to connection data area

 @Return	Error code, or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR FreeConnectionData(CONNECTION_DATA *psConnection)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(psConnection != IMG_NULL);

	if (psConnection == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreeConnectionData: invalid parameter"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Free handle base for this connection */
	if (psConnection->psHandleBase != IMG_NULL)
	{
		eError = PVRSRVFreeHandleBase(psConnection->psHandleBase);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "FreeConnectionData: Couldn't free handle base for connection (%d)", eError));
			return eError;
		}
	}

	/* Call environment specific per process deinit function */
	eError = OSConnectionPrivateDataDeInit(psConnection->hOsPrivateData);
	if (eError != PVRSRV_OK)
	{
		 PVR_DPF((PVR_DBG_ERROR, "FreeConnectionData: OSConnectionPrivateDataDeInit failed (%d)", eError));
		return eError;
	}

	OSFreeMem(psConnection);

	return PVRSRV_OK;
}

/* PVRSRVConnectionConnect*/
PVRSRV_ERROR PVRSRVConnectionConnect(IMG_PVOID *ppvPrivData, IMG_PVOID pvOSData)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	CONNECTION_DATA *psConnection;
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* Allocate per-process data area */
	psConnection = OSAllocMem(sizeof(*psConnection));
	if (psConnection == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVConnectionConnect: Couldn't allocate per-process data (%d)", eError));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psConnection, 0, sizeof(*psConnection));

	/* Call environment specific per process init function */
	eError = OSConnectionPrivateDataInit(&psConnection->hOsPrivateData, pvOSData);
	if (eError != PVRSRV_OK)
	{
		 PVR_DPF((PVR_DBG_ERROR, "PVRSRVConnectionConnect: OSConnectionPrivateDataInit failed (%d)", eError));
		goto failure;
	}

	/* Register this connection with the sync core */
	eError = SyncRegisterConnection(&psConnection->psSyncConnectionData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVConnectionConnect: Couldn't register the sync data"));
		goto failure;
	}	

	/*
		Register this connection with the pdump core.
		Pass in the sync connection data as it will be needed later when we
		only get passed in the PDump connection data.
	*/
	eError = PDumpRegisterConnection(psConnection->psSyncConnectionData,
									 &psConnection->psPDumpConnectionData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVConnectionConnect: Couldn't register the PDump data"));
		goto failure;
	}

	/* Allocate handle base for this process */
	eError = PVRSRVAllocHandleBase(&psConnection->psHandleBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVConnectionConnect: Couldn't allocate handle base for process (%d)", eError));
		goto failure;
	}

	/* Create a resource manager context for the process */
	eError = PVRSRVResManConnect(psPVRSRVData->hResManDeferContext, &psConnection->hResManContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVConnectionConnect: Couldn't register with the resource manager"));
		goto failure;
	}
	
	/* Allocate process statistics */
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	eError = PVRSRVStatsRegisterProcess(&psConnection->hProcessStats);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVConnectionConnect: Couldn't register process statistics (%d)", eError));
		goto failure;
	}
#endif

	*ppvPrivData = psConnection;

	return eError;

failure:
	(IMG_VOID)FreeConnectionData(psConnection);
	return eError;
}

/* PVRSRVConnectionDisconnect */
IMG_VOID PVRSRVConnectionDisconnect(IMG_PVOID pvDataPtr)
{
	PVRSRV_ERROR eError;
	CONNECTION_DATA *psConnection = pvDataPtr;

	/* Close the process statistics */
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	PVRSRVStatsDeregisterProcess(psConnection->hProcessStats);
	psConnection->hProcessStats = 0;
#endif

	/* Close the Resource Manager connection */
	PVRSRVResManDisconnect(psConnection->hResManContext);

	/*
		Unregister with the sync core. Logically this is after resman to
		ensure that any sync block that haven't been freed by the app will
		be freed by resman 1st.
		However, due to the fact the resman can defer the free the Sync core
		needs to handle the case where the connection data is destroyed while
		Sync blocks are still in it.
	*/
	SyncUnregisterConnection(psConnection->psSyncConnectionData);

	/*
		Unregister with the PDump core, see the note above about logical order
		and refcounting as it also applies to the PDump connection data
	*/
	PDumpUnregisterConnection(psConnection->psPDumpConnectionData);

	/* Free the connection data */
	eError = FreeConnectionData(psConnection);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVConnectionDisconnect: Error freeing per-process data"));
	}

	eError = PVRSRVPurgeHandles(KERNEL_HANDLE_BASE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVConnectionDisconnect: Purge of global handle pool failed (%d)", eError));
	}
}

/* PVRSRVConnectionInit */
PVRSRV_ERROR PVRSRVConnectionInit(IMG_VOID)
{
	return PVRSRV_OK;
}

/* PVRSRVConnectionDeInit */
PVRSRV_ERROR PVRSRVConnectionDeInit(IMG_VOID)
{
	return PVRSRV_OK;
}
