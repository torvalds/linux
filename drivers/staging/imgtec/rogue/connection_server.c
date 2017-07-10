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
#include "osfunc.h"
#include "tlstream.h"

/* PID associated with Connection currently being purged by Cleanup thread */
static IMG_PID gCurrentPurgeConnectionPid = 0;

static PVRSRV_ERROR ConnectionDataDestroy(CONNECTION_DATA *psConnection)
{
	PVRSRV_ERROR eError;
	PROCESS_HANDLE_BASE *psProcessHandleBase;
	IMG_UINT64 ui64MaxBridgeTime;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	if(psPVRSRVData->bUnload)
	{
		/* driver is unloading so do not allow the bridge lock to be released */
		ui64MaxBridgeTime = 0;
	}
	else
	{
		ui64MaxBridgeTime = CONNECTION_DEFERRED_CLEANUP_TIMESLICE_NS;
	}

	if (psConnection == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "ConnectionDestroy: Missing connection!"));
		PVR_ASSERT(0);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Close the process statistics */
#if defined(PVRSRV_ENABLE_PROCESS_STATS) && !defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
	if (psConnection->hProcessStats != NULL)
	{
		PVRSRVStatsDeregisterProcess(psConnection->hProcessStats);
		psConnection->hProcessStats = NULL;
	}
#endif

	/* Close HWPerfClient stream here even though we created it in
	 * PVRSRVConnectKM(). */
	if (psConnection->hClientTLStream)
	{
		TLStreamClose(psConnection->hClientTLStream);
		psConnection->hClientTLStream = NULL;
		PVR_DPF((PVR_DBG_MESSAGE, "Destroyed private stream."));
	}

	/* Get process handle base to decrement the refcount */
	psProcessHandleBase = psConnection->psProcessHandleBase;

	if (psProcessHandleBase != NULL)
	{
		/* In case the refcount becomes 0 we can remove the process handle base */
		if (OSAtomicDecrement(&psProcessHandleBase->iRefCount) == 0)
		{
			uintptr_t uiHashValue;

			OSLockAcquire(psPVRSRVData->hProcessHandleBase_Lock);
			uiHashValue = HASH_Remove(psPVRSRVData->psProcessHandleBase_Table, psConnection->pid);
			OSLockRelease(psPVRSRVData->hProcessHandleBase_Lock);

			if (!uiHashValue)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Failed to remove handle base from hash table.",
						__func__));
				return PVRSRV_ERROR_UNABLE_TO_REMOVE_HASH_VALUE;
			}

			eError = PVRSRVFreeHandleBase(psProcessHandleBase->psHandleBase, ui64MaxBridgeTime);
			if (eError != PVRSRV_OK)
			{
				if (eError != PVRSRV_ERROR_RETRY)
				{
					PVR_DPF((PVR_DBG_ERROR,
						 "ConnectionDataDestroy: Couldn't free handle base for process (%d)",
						 eError));
				}

				return eError;
			}

			OSFreeMem(psProcessHandleBase);
		}

		psConnection->psProcessHandleBase = NULL;
	}

	/* Free handle base for this connection */
	if (psConnection->psHandleBase != NULL)
	{
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

		psConnection->psHandleBase = NULL;
	}

	if (psConnection->psSyncConnectionData != NULL)
	{
		SyncUnregisterConnection(psConnection->psSyncConnectionData);
		psConnection->psSyncConnectionData = NULL;
	}

	if (psConnection->psPDumpConnectionData != NULL)
	{
		PDumpUnregisterConnection(psConnection->psPDumpConnectionData);
		psConnection->psPDumpConnectionData = NULL;
	}

	/* Call environment specific connection data deinit function */
	if (psConnection->hOsPrivateData != NULL)
	{
		eError = OSConnectionPrivateDataDeInit(psConnection->hOsPrivateData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
				 "PVRSRVConnectionDataDestroy: OSConnectionPrivateDataDeInit failed (%d)",
				 eError));

			return eError;
		}

		psConnection->hOsPrivateData = NULL;
	}

	OSFreeMem(psConnection);

	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVConnectionConnect(void **ppvPrivData, void *pvOSData)
{
	CONNECTION_DATA *psConnection;
	PVRSRV_ERROR eError;
	PROCESS_HANDLE_BASE *psProcessHandleBase;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	/* Allocate connection data area */
	psConnection = OSAllocZMem(sizeof(*psConnection));
	if (psConnection == NULL)
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

	psConnection->pid = OSGetCurrentClientProcessIDKM();

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
	eError = PVRSRVAllocHandleBase(&psConnection->psHandleBase,
	                               PVRSRV_HANDLE_BASE_TYPE_CONNECTION);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVConnectionConnect: Couldn't allocate handle base for connection (%d)",
			 eError));
		goto failure;
	}

	/* Try to get process handle base if it already exists */
	OSLockAcquire(psPVRSRVData->hProcessHandleBase_Lock);
	psProcessHandleBase = (PROCESS_HANDLE_BASE*) HASH_Retrieve(PVRSRVGetPVRSRVData()->psProcessHandleBase_Table,
	                                                           psConnection->pid);

	/* In case there is none we are going to allocate one */
	if (psProcessHandleBase == NULL)
	{
		psProcessHandleBase = OSAllocZMem(sizeof(PROCESS_HANDLE_BASE));
		if (psProcessHandleBase == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to allocate handle base, oom.",
					__func__));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto failureLock;
		}

		/* Allocate handle base for this process */
		eError = PVRSRVAllocHandleBase(&psProcessHandleBase->psHandleBase,
		                               PVRSRV_HANDLE_BASE_TYPE_PROCESS);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Couldn't allocate handle base for process (%d)",
			         __func__,
			         eError));
			OSFreeMem(psProcessHandleBase);
			goto failureLock;
		}

		/* Insert the handle base into the global hash table */
		if (!HASH_Insert(PVRSRVGetPVRSRVData()->psProcessHandleBase_Table,
		                 psConnection->pid,
		                 (uintptr_t) psProcessHandleBase))
		{

			eError = PVRSRV_ERROR_UNABLE_TO_INSERT_HASH_VALUE;

			PVRSRVFreeHandleBase(psProcessHandleBase->psHandleBase, 0);

			OSFreeMem(psProcessHandleBase);
			goto failureLock;
		}
	}
	OSLockRelease(psPVRSRVData->hProcessHandleBase_Lock);

	psConnection->psProcessHandleBase = psProcessHandleBase;

	OSAtomicIncrement(&psProcessHandleBase->iRefCount);

	/* Allocate process statistics */
#if defined(PVRSRV_ENABLE_PROCESS_STATS) && !defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
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

failureLock:
	OSLockRelease(psPVRSRVData->hProcessHandleBase_Lock);
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
#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
	if (PVRSRVGetPVRSRVData()->eServicesState == PVRSRV_SERVICES_STATE_OK)
#endif
	{
		/* Defer the release of the connection data */
		psConnectionData->sCleanupThreadFn.pfnFree = _CleanupThreadPurgeConnectionData;
		psConnectionData->sCleanupThreadFn.pvData = psConnectionData;
		psConnectionData->sCleanupThreadFn.ui32RetryCount = CLEANUP_THREAD_RETRY_COUNT_DEFAULT;
		psConnectionData->sCleanupThreadFn.bDependsOnHW = IMG_FALSE;
		PVRSRVCleanupThreadAddWork(&psConnectionData->sCleanupThreadFn);
	}
}

IMG_PID PVRSRVGetPurgeConnectionPid(void)
{
	return gCurrentPurgeConnectionPid;
}
