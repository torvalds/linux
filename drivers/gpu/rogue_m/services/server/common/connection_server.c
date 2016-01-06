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

#include "handle.h"
#include "pvrsrv.h"
#include "connection_server.h"
#include "osconnection_server.h"
#include "allocmem.h"
#include "pvr_debug.h"
#include "sync_server.h"
#include "process_stats.h"
#include "pdump_km.h"
#include "lists.h"

/* PID associated with Connection currently being purged by Cleanup thread */
static IMG_PID gCurrentPurgeConnectionPid = 0;

static PVRSRV_ERROR ConnectionDataDestroy(CONNECTION_DATA *psConnection)
{
	PVRSRV_ERROR eError;

	if (psConnection == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "ConnectionDestroy: Missing connection!"));
		PVR_ASSERT(0);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Close the process statistics */
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	if (psConnection->hProcessStats != IMG_NULL)
	{
		PVRSRVStatsDeregisterProcess(psConnection->hProcessStats);
		psConnection->hProcessStats = IMG_NULL;
	}
#endif

	/* Free handle base for this connection */
	if (psConnection->psHandleBase != IMG_NULL)
	{
		PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
		IMG_UINT64 ui64MaxBridgeTime;

		if(psPVRSRVData->bUnload)
		{
			/* driver is unloading so do not allow the bridge lock to be released */
			ui64MaxBridgeTime = 0;
		}
		else
		{
			ui64MaxBridgeTime = CONNECTION_DEFERRED_CLEANUP_TIMESLICE_NS;
		}

		eError = PVRSRVFreeHandleBase(psConnection->psHandleBase, ui64MaxBridgeTime);
		if (eError != PVRSRV_OK)
		{
			if (eError != PVRSRV_ERROR_RETRY)
			{
				PVR_DPF((PVR_DBG_ERROR,
					 "ConnectionDataDestroy: Couldn't free handle base for connection (%d)",
					 eError));
			}

			return eError;
		}

		psConnection->psHandleBase = IMG_NULL;
	}

	if (psConnection->psSyncConnectionData != IMG_NULL)
	{
		SyncUnregisterConnection(psConnection->psSyncConnectionData);
		psConnection->psSyncConnectionData = IMG_NULL;
	}

	if (psConnection->psPDumpConnectionData != IMG_NULL)
	{
		PDumpUnregisterConnection(psConnection->psPDumpConnectionData);
		psConnection->psPDumpConnectionData = IMG_NULL;
	}

	/* Call environment specific connection data deinit function */
	if (psConnection->hOsPrivateData != IMG_NULL)
	{
		eError = OSConnectionPrivateDataDeInit(psConnection->hOsPrivateData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
				 "PVRSRVConnectionDataDestroy: OSConnectionPrivateDataDeInit failed (%d)",
				 eError));

			return eError;
		}

		psConnection->hOsPrivateData = IMG_NULL;
	}

	OSFreeMem(psConnection);

	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVConnectionConnect(IMG_PVOID *ppvPrivData, IMG_PVOID pvOSData)
{
	CONNECTION_DATA *psConnection;
	PVRSRV_ERROR eError;

	/* Allocate connection data area */
	psConnection = OSAllocZMem(sizeof(*psConnection));
	if (psConnection == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVConnectionConnect: Couldn't allocate connection data"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* Call environment specific connection data init function */
	eError = OSConnectionPrivateDataInit(&psConnection->hOsPrivateData, pvOSData);
	if (eError != PVRSRV_OK)
	{
		 PVR_DPF((PVR_DBG_ERROR,
			  "PVRSRVConnectionConnect: OSConnectionPrivateDataInit failed (%d)",
			  eError));
		goto failure;
	}

	psConnection->pid = OSGetCurrentProcessID();

	/* Register this connection with the sync core */
	eError = SyncRegisterConnection(&psConnection->psSyncConnectionData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVConnectionConnect: Couldn't register the sync data"));
		goto failure;
	}

	/*
	 * Register this connection with the pdump core. Pass in the sync connection data
	 * as it will be needed later when we only get passed in the PDump connection data.
	 */
	eError = PDumpRegisterConnection(psConnection->psSyncConnectionData,
					 &psConnection->psPDumpConnectionData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVConnectionConnect: Couldn't register the PDump data"));
		goto failure;
	}

	/* Allocate handle base for this connection */
	eError = PVRSRVAllocHandleBase(&psConnection->psHandleBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVConnectionConnect: Couldn't allocate handle base for connection (%d)",
			 eError));
		goto failure;
	}

	/* Allocate process statistics */
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	eError = PVRSRVStatsRegisterProcess(&psConnection->hProcessStats);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVConnectionConnect: Couldn't register process statistics (%d)",
			 eError));
		goto failure;
	}
#endif

	*ppvPrivData = psConnection;

	return eError;

failure:
	ConnectionDataDestroy(psConnection);

	return eError;
}

static PVRSRV_ERROR _CleanupThreadPurgeConnectionData(void *pvConnectionData)
{
	PVRSRV_ERROR eErrorConnection, eErrorKernel;
	CONNECTION_DATA *psConnectionData = pvConnectionData;

	OSAcquireBridgeLock();

	gCurrentPurgeConnectionPid = psConnectionData->pid;

	eErrorConnection = ConnectionDataDestroy(psConnectionData);
	if (eErrorConnection != PVRSRV_OK)
	{
		if (eErrorConnection == PVRSRV_ERROR_RETRY)
		{
			PVR_DPF((PVR_DBG_MESSAGE,
				 "_CleanupThreadPurgeConnectionData: Failed to purge connection data %p "
				 "(deferring destruction)",
				 psConnectionData));
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE,
			 "_CleanupThreadPurgeConnectionData: Connection data %p deferred destruction finished",
			 psConnectionData));
	}

	/* Check if possible resize the global handle base */
	eErrorKernel = PVRSRVPurgeHandles(KERNEL_HANDLE_BASE);
	if (eErrorKernel != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "_CleanupThreadPurgeConnectionData: Purge of global handle pool failed (%d)",
			 eErrorKernel));
	}

	gCurrentPurgeConnectionPid = 0;

	OSReleaseBridgeLock();

	return eErrorConnection;
}

void PVRSRVConnectionDisconnect(void *pvDataPtr)
{
	CONNECTION_DATA *psConnectionData = pvDataPtr;

	/* Notify the PDump core if the pdump control client is disconnecting */
	if (psConnectionData->ui32ClientFlags & SRV_FLAGS_PDUMPCTRL)
	{
		PDumpDisconnectionNotify();
	}

	/* Defer the release of the connection data */
	psConnectionData->sCleanupThreadFn.pfnFree = _CleanupThreadPurgeConnectionData;
	psConnectionData->sCleanupThreadFn.pvData = psConnectionData;
	psConnectionData->sCleanupThreadFn.ui32RetryCount = CLEANUP_THREAD_RETRY_COUNT_DEFAULT;
	PVRSRVCleanupThreadAddWork(&psConnectionData->sCleanupThreadFn);
}

PVRSRV_ERROR PVRSRVConnectionInit(void)
{
	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVConnectionDeInit(void)
{
	return PVRSRV_OK;
}

IMG_PID PVRSRVGetPurgeConnectionPid(void)
{
	return gCurrentPurgeConnectionPid;
}
