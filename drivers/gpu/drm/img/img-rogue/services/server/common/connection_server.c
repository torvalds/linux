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
#include "osfunc.h"
#include "tlstream.h"

/* PID associated with Connection currently being purged by Cleanup thread */
static IMG_PID gCurrentPurgeConnectionPid;

static PVRSRV_ERROR ConnectionDataDestroy(CONNECTION_DATA *psConnection)
{
	PVRSRV_ERROR eError;
	PROCESS_HANDLE_BASE *psProcessHandleBase;
	IMG_UINT64 ui64MaxBridgeTime;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	if (psPVRSRVData->bUnload)
	{
		/* driver is unloading so do not allow the bridge lock to be released */
		ui64MaxBridgeTime = 0;
	}
	else
	{
		ui64MaxBridgeTime = CONNECTION_DEFERRED_CLEANUP_TIMESLICE_NS;
	}

	PVR_ASSERT(psConnection != NULL);
	PVR_LOG_RETURN_IF_INVALID_PARAM(psConnection, "psConnection");

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
		/* acquire the lock now to ensure unref and removal from the
		 * hash table is atomic.
		 * if the refcount becomes zero then the lock needs to be held
		 * until the entry is removed from the hash table.
		 */
		OSLockAcquire(psPVRSRVData->hProcessHandleBase_Lock);

		/* In case the refcount becomes 0 we can remove the process handle base */
		if (OSAtomicDecrement(&psProcessHandleBase->iRefCount) == 0)
		{
			uintptr_t uiHashValue;

			uiHashValue = HASH_Remove(psPVRSRVData->psProcessHandleBase_Table, psConnection->pid);
			OSLockRelease(psPVRSRVData->hProcessHandleBase_Lock);

			if (!uiHashValue)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Failed to remove handle base from hash table.",
						__func__));
				return PVRSRV_ERROR_UNABLE_TO_REMOVE_HASH_VALUE;
			}

			eError = PVRSRVFreeKernelHandles(psProcessHandleBase->psHandleBase);
			PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVFreeKernelHandles");

			eError = PVRSRVFreeHandleBase(psProcessHandleBase->psHandleBase, ui64MaxBridgeTime);
			PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVFreeHandleBase:1");

			OSFreeMem(psProcessHandleBase);
		}
		else
		{
			OSLockRelease(psPVRSRVData->hProcessHandleBase_Lock);
		}

		psConnection->psProcessHandleBase = NULL;
	}

	/* Free handle base for this connection */
	if (psConnection->psHandleBase != NULL)
	{
		eError = PVRSRVFreeHandleBase(psConnection->psHandleBase, ui64MaxBridgeTime);
		/*
		 * If we get PVRSRV_ERROR_RETRY we need to pass this back to the caller
		 * who will schedule a retry.
		 * Do not log this as it is an expected exception.
		 * This can occur if the Firmware is still processing a workload from
		 * the client when a tear-down request is received.
		 * Retrying will allow the in-flight work to be completed and the
		 * tear-down request can be completed when the FW is no longer busy.
		 */
		if (PVRSRV_ERROR_RETRY == eError)
		{
			return eError;
		}
		else
		{
			PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVFreeHandleBase:2");
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
		PVR_LOG_RETURN_IF_ERROR(eError, "OSConnectionPrivateDataDeInit");

		psConnection->hOsPrivateData = NULL;
	}

	/* Close the PID stats entry as late as possible to catch all frees */
#if defined(PVRSRV_ENABLE_PROCESS_STATS) && !defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
	if (psConnection->hProcessStats != NULL)
	{
		PVRSRVStatsDeregisterProcess(psConnection->hProcessStats);
		psConnection->hProcessStats = NULL;
	}
#endif

	OSFreeMemNoStats(psConnection);

	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVCommonConnectionConnect(void **ppvPrivData, void *pvOSData)
{
	CONNECTION_DATA *psConnection;
	PVRSRV_ERROR eError;
	PROCESS_HANDLE_BASE *psProcessHandleBase;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	/* Allocate connection data area, no stats since process not registered yet */
	psConnection = OSAllocZMemNoStats(sizeof(*psConnection));
	PVR_LOG_RETURN_IF_NOMEM(psConnection, "psConnection");

	/* Allocate process statistics as early as possible to catch all allocs */
#if defined(PVRSRV_ENABLE_PROCESS_STATS) && !defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
	eError = PVRSRVStatsRegisterProcess(&psConnection->hProcessStats);
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVStatsRegisterProcess", failure);
#endif

	/* Call environment specific connection data init function */
	eError = OSConnectionPrivateDataInit(&psConnection->hOsPrivateData, pvOSData);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSConnectionPrivateDataInit", failure);

	psConnection->pid = OSGetCurrentClientProcessIDKM();
	psConnection->vpid = OSGetCurrentVirtualProcessID();
	psConnection->tid = (IMG_UINT32)OSGetCurrentClientThreadIDKM();
	OSStringLCopy(psConnection->pszProcName, OSGetCurrentClientProcessNameKM(), PVRSRV_CONNECTION_PROCESS_NAME_LEN);

#if defined(SUPPORT_DMA_TRANSFER)
	OSLockCreate(&psConnection->hDmaReqLock);

	eError = OSEventObjectCreate("Dma transfer cleanup event object",
															 &psConnection->hDmaEventObject);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSEventObjectCreate", failure);

	OSAtomicWrite(&psConnection->ui32NumDmaTransfersInFlight, 0);
	psConnection->bAcceptDmaRequests = IMG_TRUE;
#endif

#if defined(DEBUG) || defined(PDUMP)
	PVR_LOG(("%s connected", psConnection->pszProcName));
#endif

	/* Register this connection with the sync core */
	eError = SyncRegisterConnection(&psConnection->psSyncConnectionData);
	PVR_LOG_GOTO_IF_ERROR(eError, "SyncRegisterConnection", failure);

	/*
	 * Register this connection and Sync PDump callback with
	 * the pdump core. Pass in the Sync connection data.
	 */
	eError = PDumpRegisterConnection(psConnection->psSyncConnectionData,
	                                  SyncConnectionPDumpSyncBlocks,
	                                  &psConnection->psPDumpConnectionData);
	PVR_LOG_GOTO_IF_ERROR(eError, "PDumpRegisterConnection", failure);

	/* Allocate handle base for this connection */
	eError = PVRSRVAllocHandleBase(&psConnection->psHandleBase,
	                               PVRSRV_HANDLE_BASE_TYPE_CONNECTION);
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVAllocHandleBase", failure);

	/* Try to get process handle base if it already exists */
	OSLockAcquire(psPVRSRVData->hProcessHandleBase_Lock);
	psProcessHandleBase = (PROCESS_HANDLE_BASE*) HASH_Retrieve(PVRSRVGetPVRSRVData()->psProcessHandleBase_Table,
	                                                           psConnection->pid);

	/* In case there is none we are going to allocate one */
	if (psProcessHandleBase == NULL)
	{
		psProcessHandleBase = OSAllocZMem(sizeof(PROCESS_HANDLE_BASE));
		PVR_LOG_GOTO_IF_NOMEM(psProcessHandleBase, eError, failureLock);

		OSAtomicWrite(&psProcessHandleBase->iRefCount, 0);

		/* Allocate handle base for this process */
		eError = PVRSRVAllocHandleBase(&psProcessHandleBase->psHandleBase,
		                               PVRSRV_HANDLE_BASE_TYPE_PROCESS);
		if (eError != PVRSRV_OK)
		{
			PVR_LOG_ERROR(eError, "PVRSRVAllocHandleBase");
			OSFreeMem(psProcessHandleBase);
			goto failureLock;
		}

		/* Insert the handle base into the global hash table */
		if (!HASH_Insert(PVRSRVGetPVRSRVData()->psProcessHandleBase_Table,
		                 psConnection->pid,
		                 (uintptr_t) psProcessHandleBase))
		{
			PVRSRVFreeHandleBase(psProcessHandleBase->psHandleBase, 0);

			OSFreeMem(psProcessHandleBase);
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_UNABLE_TO_INSERT_HASH_VALUE, failureLock);
		}
	}
	OSAtomicIncrement(&psProcessHandleBase->iRefCount);

	OSLockRelease(psPVRSRVData->hProcessHandleBase_Lock);

	/* hConnectionsLock now resides in PVRSRV_DEVICE_NODE */
	{
		PVRSRV_DEVICE_NODE	*psDevNode = OSGetDevNode(psConnection);

		OSLockAcquire(psDevNode->hConnectionsLock);
		dllist_add_to_tail(&psDevNode->sConnections, &psConnection->sConnectionListNode);
		OSLockRelease(psDevNode->hConnectionsLock);
	}

	psConnection->psProcessHandleBase = psProcessHandleBase;

	*ppvPrivData = psConnection;

	return PVRSRV_OK;

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

	gCurrentPurgeConnectionPid = psConnectionData->pid;

	eErrorConnection = ConnectionDataDestroy(psConnectionData);
	if (eErrorConnection != PVRSRV_OK)
	{
		if (eErrorConnection == PVRSRV_ERROR_RETRY)
		{
			PVR_DPF((PVR_DBG_MESSAGE,
				 "%s: Failed to purge connection data %p "
				 "(deferring destruction)",
				 __func__,
				 psConnectionData));
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE,
			 "%s: Connection data %p deferred destruction finished",
			 __func__,
			 psConnectionData));
	}

	/* Check if possible resize the global handle base */
	eErrorKernel = PVRSRVPurgeHandles(KERNEL_HANDLE_BASE);
	PVR_LOG_IF_ERROR(eErrorKernel, "PVRSRVPurgeHandles");

	gCurrentPurgeConnectionPid = 0;

	return eErrorConnection;
}

#if defined(SUPPORT_DMA_TRANSFER)
static void WaitForOutstandingDma(CONNECTION_DATA *psConnectionData)
{

	PVRSRV_ERROR eError;
	IMG_HANDLE hEvent;
	IMG_UINT32 ui32Tries = 100;

#if defined(DMA_VERBOSE)
	PVR_DPF((PVR_DBG_ERROR,
					"Waiting on %d DMA transfers in flight...", OSAtomicRead(&psConnectionData->ui32NumDmaTransfersInFlight)));
#endif

	eError = OSEventObjectOpen(psConnectionData->hDmaEventObject, &hEvent);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to open event object", __func__));
		return;
	}

	while (OSAtomicRead(&psConnectionData->ui32NumDmaTransfersInFlight) != 0)
	{
		/*
		#define DMA_TRANSFER_TIMEOUT_US (5000000ULL)

		This currently doesn't work properly. Wait time is not as requested.
		Using OSSleepms instead

		OSEventObjectWaitKernel(hEvent, DMA_TRANSFER_TIMEOUT_US);
		*/
		OSSleepms(50);
		if (!ui32Tries)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Timeout while waiting on outstanding DMA transfers!", __func__));
			break;
		}

		ui32Tries--;
	}

	OSEventObjectClose(hEvent);
}
#endif

void PVRSRVCommonConnectionDisconnect(void *pvDataPtr)
{
	CONNECTION_DATA *psConnectionData = pvDataPtr;
	PVRSRV_DEVICE_NODE *psDevNode = OSGetDevNode(psConnectionData);

	OSLockAcquire(psDevNode->hConnectionsLock);
	dllist_remove_node(&psConnectionData->sConnectionListNode);
	OSLockRelease(psDevNode->hConnectionsLock);

	/* Notify the PDump core if the pdump control client is disconnecting */
	if (psConnectionData->ui32ClientFlags & SRV_FLAGS_PDUMPCTRL)
	{
		PDumpDisconnectionNotify();
	}
#if defined(SUPPORT_DMA_TRANSFER)
	OSLockAcquire(psConnectionData->hDmaReqLock);

	psConnectionData->bAcceptDmaRequests = IMG_FALSE;

	OSLockRelease(psConnectionData->hDmaReqLock);

	WaitForOutstandingDma(psConnectionData);

	OSEventObjectDestroy(psConnectionData->hDmaEventObject);
	OSLockDestroy(psConnectionData->hDmaReqLock);
#endif

#if defined(DEBUG) || defined(PDUMP)
	PVR_LOG(("%s disconnected", psConnectionData->pszProcName));
#endif

#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
	if (PVRSRVGetPVRSRVData()->eServicesState == PVRSRV_SERVICES_STATE_OK)
#endif
	{
		/* Defer the release of the connection data */
		psConnectionData->sCleanupThreadFn.pfnFree = _CleanupThreadPurgeConnectionData;
		psConnectionData->sCleanupThreadFn.pvData = psConnectionData;
		psConnectionData->sCleanupThreadFn.bDependsOnHW = IMG_FALSE;
		CLEANUP_THREAD_SET_RETRY_COUNT(&psConnectionData->sCleanupThreadFn,
		                               CLEANUP_THREAD_RETRY_COUNT_DEFAULT);
		PVRSRVCleanupThreadAddWork(&psConnectionData->sCleanupThreadFn);
	}
}

IMG_PID PVRSRVGetPurgeConnectionPid(void)
{
	return gCurrentPurgeConnectionPid;
}

/* Prefix for debug messages about Active Connections */
#define DEBUG_DUMP_CONNECTION_FORMAT_STR " P%d-V%d-T%d-%s,"
#define CONNECTIONS_PREFIX               "Connections Device ID:%u(%d)"
#define MAX_CONNECTIONS_PREFIX            (29)
#define MAX_DEBUG_DUMP_CONNECTION_STR_LEN (1+10+10+10+7+PVRSRV_CONNECTION_PROCESS_NAME_LEN)
#define MAX_DEBUG_DUMP_STRING_LEN         (1+MAX_CONNECTIONS_PREFIX+(3*MAX_DEBUG_DUMP_CONNECTION_STR_LEN))

void PVRSRVConnectionDebugNotify(DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                                 void *pvDumpDebugFile)
{
	PDLLIST_NODE pNext, pNode;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_NODE *psDevNode;
	IMG_BOOL bFoundConnections = IMG_FALSE;

	/*
	 * Connections are linked to each device-node within
	 * PVRSRV_DATA->psDeviceNodeList. Traverse this and display each connection
	 * within device node.
	 */
	for (psDevNode = psPVRSRVData->psDeviceNodeList;
	     psDevNode != NULL;
	     psDevNode = psDevNode->psNext)
	{
		/* We must check for an initialised device before accessing its mutex.
		 * The mutex is initialised as part of DeviceInitialize() which occurs
		 * on first access to the device node.
		 */
		if (psDevNode->eDevState != PVRSRV_DEVICE_STATE_ACTIVE)
		{
			continue;
		}

		bFoundConnections = IMG_TRUE;

		OSLockAcquire(psDevNode->hConnectionsLock);
		if (dllist_is_empty(&psDevNode->sConnections))
		{
			PVR_DUMPDEBUG_LOG(CONNECTIONS_PREFIX " No active connections",
				              (unsigned char)psDevNode->sDevId.ui32InternalID,
				              (unsigned char)psDevNode->sDevId.i32OsDeviceID);
		}
		else
		{
			IMG_CHAR sActiveConnections[MAX_DEBUG_DUMP_STRING_LEN];
			IMG_UINT16 i, uiPos = 0;
			IMG_BOOL bPrinted = IMG_FALSE;
			size_t uiSize = sizeof(sActiveConnections);

			IMG_CHAR szTmpConBuff[MAX_CONNECTIONS_PREFIX + 1];
			i = OSSNPrintf(szTmpConBuff,
				           MAX_CONNECTIONS_PREFIX,
				           CONNECTIONS_PREFIX,
				           (unsigned char)psDevNode->sDevId.ui32InternalID,
				           (unsigned char)psDevNode->sDevId.i32OsDeviceID);
			OSStringLCopy(sActiveConnections+uiPos, szTmpConBuff, uiSize);

			/* Move the write offset to the end of the current string */
			uiPos += i;
			/* Update the amount of remaining space available to copy into */
			uiSize -= i;

			dllist_foreach_node(&psDevNode->sConnections, pNode, pNext)
			{
				CONNECTION_DATA *sData = IMG_CONTAINER_OF(pNode, CONNECTION_DATA, sConnectionListNode);

				IMG_CHAR sTmpBuff[MAX_DEBUG_DUMP_CONNECTION_STR_LEN];
				i = OSSNPrintf(sTmpBuff, MAX_DEBUG_DUMP_CONNECTION_STR_LEN,
					DEBUG_DUMP_CONNECTION_FORMAT_STR, sData->pid, sData->vpid, sData->tid, sData->pszProcName);
				i = MIN(MAX_DEBUG_DUMP_CONNECTION_STR_LEN, i);
				bPrinted = IMG_FALSE;

				OSStringLCopy(sActiveConnections+uiPos, sTmpBuff, uiSize);

				/* Move the write offset to the end of the current string */
				uiPos += i;
				/* Update the amount of remaining space available to copy into */
				uiSize -= i;

				/* If there is not enough space to add another connection to this line, output the line */
				if (uiSize <= MAX_DEBUG_DUMP_CONNECTION_STR_LEN)
				{
					PVR_DUMPDEBUG_LOG("%s", sActiveConnections);

					/*
					 * Remove the "Connections:" prefix from the buffer.
					 * Leave the subsequent buffer contents indented by the same
					 * amount to aid in interpreting the debug output.
					 */
					uiPos = sizeof(CONNECTIONS_PREFIX) - 1;
					/* Reset the amount of space available to copy into */
					uiSize = MAX_DEBUG_DUMP_STRING_LEN - uiPos;
					bPrinted = IMG_TRUE;
				}
			}

			/* Only print the current line if it hasn't already been printed */
			if (!bPrinted)
			{
				// Strip of the final comma
				sActiveConnections[OSStringNLength(sActiveConnections, MAX_DEBUG_DUMP_STRING_LEN) - 1] = '\0';
				PVR_DUMPDEBUG_LOG("%s", sActiveConnections);
			}
#undef MAX_DEBUG_DUMP_STRING_LEN
#undef MAX_DEBUG_DUMP_CONNECTIONS_PER_LINE
		}
		OSLockRelease(psDevNode->hConnectionsLock);
	}

	/* Check to see if we have displayed anything from the loop above */
	if (bFoundConnections == IMG_FALSE)
	{
		PVR_DUMPDEBUG_LOG("Connections: No Devices: No active connections");
	}
}
