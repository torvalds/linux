/*************************************************************************/ /*!
@Title          Kernel side command queue functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#include "services_headers.h"
#include "pvr_bridge_km.h"

#include "lists.h"
#include "ttrace.h"

/*
 * The number of commands of each type which can be in flight at once.
 */
#if defined(SUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED)
#define DC_NUM_COMMANDS_PER_TYPE		2
#else
#define DC_NUM_COMMANDS_PER_TYPE		1
#endif

/*
 * List of private command processing function pointer tables and command
 * complete tables for a device in the system.
 * Each table is allocated when the device registers its private command
 * processing functions.
 */
typedef struct _DEVICE_COMMAND_DATA_
{
	PFN_CMD_PROC			pfnCmdProc;
	PCOMMAND_COMPLETE_DATA	apsCmdCompleteData[DC_NUM_COMMANDS_PER_TYPE];
	IMG_UINT32				ui32CCBOffset;
	IMG_UINT32				ui32MaxDstSyncCount;	/*!< Maximum number of dest syncs */
	IMG_UINT32				ui32MaxSrcSyncCount;	/*!< Maximum number of source syncs */
} DEVICE_COMMAND_DATA;


#if defined(__linux__) && defined(__KERNEL__)

#include "proc.h"

/*****************************************************************************
 FUNCTION	:	ProcSeqShowQueue

 PURPOSE	:	Print the content of queue element  to /proc file
				(See env/linux/proc.c:CreateProcReadEntrySeq)

 PARAMETERS	:	sfile - /proc seq_file
				el - Element to print
*****************************************************************************/
void ProcSeqShowQueue(struct seq_file *sfile,void* el)
{
	PVRSRV_QUEUE_INFO *psQueue = (PVRSRV_QUEUE_INFO*)el;
	IMG_INT cmds = 0;
	IMG_SIZE_T uReadOffset;
	IMG_SIZE_T uWriteOffset;
	PVRSRV_COMMAND *psCmd;

	if(el == PVR_PROC_SEQ_START_TOKEN)
	{
		seq_printf( sfile,
					"Command Queues\n"
					"Queue    CmdPtr      Pid Command Size DevInd  DSC  SSC  #Data ...\n");
		return;
	}

	uReadOffset = psQueue->uReadOffset;
	uWriteOffset = psQueue->uWriteOffset;

	while (uReadOffset != uWriteOffset)
	{
		psCmd= (PVRSRV_COMMAND *)((IMG_UINTPTR_T)psQueue->pvLinQueueKM + uReadOffset);

		seq_printf(sfile, "%p %p  %5u  %6u  %3u  %5u   %2u   %2u    %3u  \n",
							psQueue,
							psCmd,
					 		psCmd->ui32ProcessID,
							psCmd->CommandType,
							psCmd->uCmdSize,
							psCmd->ui32DevIndex,
							psCmd->ui32DstSyncCount,
							psCmd->ui32SrcSyncCount,
							psCmd->uDataSize);
		{
			IMG_UINT32 i;
			for (i = 0; i < psCmd->ui32SrcSyncCount; i++)
			{
				PVRSRV_SYNC_DATA *psSyncData = psCmd->psSrcSync[i].psKernelSyncInfoKM->psSyncData;
				seq_printf(sfile, "  Sync %u: ROP/ROC: 0x%x/0x%x WOP/WOC: 0x%x/0x%x ROC-VA: 0x%x WOC-VA: 0x%x\n",
									i,
									psCmd->psSrcSync[i].ui32ReadOps2Pending,
									psSyncData->ui32ReadOps2Complete,
									psCmd->psSrcSync[i].ui32WriteOpsPending,
									psSyncData->ui32WriteOpsComplete,
									psCmd->psSrcSync[i].psKernelSyncInfoKM->sReadOps2CompleteDevVAddr.uiAddr,
									psCmd->psSrcSync[i].psKernelSyncInfoKM->sWriteOpsCompleteDevVAddr.uiAddr);
			}
		}

		/* taken from UPDATE_QUEUE_ROFF in queue.h */
		uReadOffset += psCmd->uCmdSize;
		uReadOffset &= psQueue->uQueueSize - 1;
		cmds++;
	}

	if (cmds == 0)
	{
		seq_printf(sfile, "%p <empty>\n", psQueue);
	}
}

/*****************************************************************************
 FUNCTION	:	ProcSeqOff2ElementQueue

 PURPOSE	:	Transale offset to element (/proc stuff)

 PARAMETERS	:	sfile - /proc seq_file
				off - the offset into the buffer

 RETURNS    :   element to print
*****************************************************************************/
void* ProcSeqOff2ElementQueue(struct seq_file * sfile, loff_t off)
{
	PVRSRV_QUEUE_INFO *psQueue = IMG_NULL;
	SYS_DATA *psSysData;

	PVR_UNREFERENCED_PARAMETER(sfile);

	if(!off)
	{
		return PVR_PROC_SEQ_START_TOKEN;
	}


	psSysData = SysAcquireDataNoCheck();
	if (psSysData != IMG_NULL)
	{
		for (psQueue = psSysData->psQueueList; (((--off) > 0) && (psQueue != IMG_NULL)); psQueue = psQueue->psNextKM);
	}

	return psQueue;
}
#endif /* __linux__ && __KERNEL__ */

/*!
 * Macro to return space in given command queue
 */
#define GET_SPACE_IN_CMDQ(psQueue)										\
	((((psQueue)->uReadOffset - (psQueue)->uWriteOffset)				\
	+ ((psQueue)->uQueueSize - 1)) & ((psQueue)->uQueueSize - 1))

/*!
 * Macro to Write Offset in given command queue
 */
#define UPDATE_QUEUE_WOFF(psQueue, uSize)							\
	(psQueue)->uWriteOffset = ((psQueue)->uWriteOffset + (uSize))	\
	& ((psQueue)->uQueueSize - 1);

/*!
 * Check if an ops complete value has gone past the pending value.
 * This can happen when dummy processing multiple operations, e.g. hardware recovery.
 */
#define SYNCOPS_STALE(ui32OpsComplete, ui32OpsPending)					\
	((ui32OpsComplete) >= (ui32OpsPending))

/*!
****************************************************************************
 @Function	: PVRSRVGetWriteOpsPending

 @Description	: Gets the next operation to wait for in a sync object

 @Input	: psSyncInfo	- pointer to sync information struct
 @Input	: bIsReadOp		- Is this a read or write op

 @Return : Next op value
*****************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVGetWriteOpsPending)
#endif
static INLINE
IMG_UINT32 PVRSRVGetWriteOpsPending(PVRSRV_KERNEL_SYNC_INFO *psSyncInfo, IMG_BOOL bIsReadOp)
{
	IMG_UINT32 ui32WriteOpsPending;

	if(bIsReadOp)
	{
		ui32WriteOpsPending = psSyncInfo->psSyncData->ui32WriteOpsPending;
	}
	else
	{
		/*
			Note: This needs to be atomic and is provided the
			kernel driver is single threaded (non-rentrant)
		*/
		ui32WriteOpsPending = psSyncInfo->psSyncData->ui32WriteOpsPending++;
	}

	return ui32WriteOpsPending;
}

/*!
*****************************************************************************
 @Function	: PVRSRVGetReadOpsPending

 @Description	: Gets the number of pending read ops

 @Input	: psSyncInfo	- pointer to sync information struct
 @Input : bIsReadOp		- Is this a read or write op

 @Return : Next op value
*****************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVGetReadOpsPending)
#endif
static INLINE
IMG_UINT32 PVRSRVGetReadOpsPending(PVRSRV_KERNEL_SYNC_INFO *psSyncInfo, IMG_BOOL bIsReadOp)
{
	IMG_UINT32 ui32ReadOpsPending;

	if(bIsReadOp)
	{
		ui32ReadOpsPending = psSyncInfo->psSyncData->ui32ReadOps2Pending++;
	}
	else
	{
		ui32ReadOpsPending = psSyncInfo->psSyncData->ui32ReadOps2Pending;
	}

	return ui32ReadOpsPending;
}

static IMG_VOID QueueDumpCmdComplete(COMMAND_COMPLETE_DATA *psCmdCompleteData,
									 IMG_UINT32				i,
									 IMG_BOOL				bIsSrc)
{
	PVRSRV_SYNC_OBJECT	*psSyncObject;

	psSyncObject = bIsSrc ? psCmdCompleteData->psSrcSync : psCmdCompleteData->psDstSync;

	if (psCmdCompleteData->bInUse)
	{
		PVR_LOG(("\t%s %u: ROC DevVAddr:0x%X ROP:0x%x ROC:0x%x, WOC DevVAddr:0x%X WOP:0x%x WOC:0x%x",
				bIsSrc ? "SRC" : "DEST", i,
				psSyncObject[i].psKernelSyncInfoKM->sReadOps2CompleteDevVAddr.uiAddr,
				psSyncObject[i].psKernelSyncInfoKM->psSyncData->ui32ReadOps2Pending,
				psSyncObject[i].psKernelSyncInfoKM->psSyncData->ui32ReadOps2Complete,
				psSyncObject[i].psKernelSyncInfoKM->sWriteOpsCompleteDevVAddr.uiAddr,
				psSyncObject[i].psKernelSyncInfoKM->psSyncData->ui32WriteOpsPending,
				psSyncObject[i].psKernelSyncInfoKM->psSyncData->ui32WriteOpsComplete))
	}
	else
	{
		PVR_LOG(("\t%s %u: (Not in use)", bIsSrc ? "SRC" : "DEST", i))
	}
}


static IMG_VOID QueueDumpDebugInfo_ForEachCb(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if (psDeviceNode->sDevId.eDeviceClass == PVRSRV_DEVICE_CLASS_DISPLAY)
	{
		IMG_UINT32				ui32CmdCounter, ui32SyncCounter;
		SYS_DATA				*psSysData;
		DEVICE_COMMAND_DATA		*psDeviceCommandData;
		PCOMMAND_COMPLETE_DATA	psCmdCompleteData;

		SysAcquireData(&psSysData);

		psDeviceCommandData = psSysData->apsDeviceCommandData[psDeviceNode->sDevId.ui32DeviceIndex];

		if (psDeviceCommandData != IMG_NULL)
		{
			for (ui32CmdCounter = 0; ui32CmdCounter < DC_NUM_COMMANDS_PER_TYPE; ui32CmdCounter++)
			{
				psCmdCompleteData = psDeviceCommandData[DC_FLIP_COMMAND].apsCmdCompleteData[ui32CmdCounter];

				PVR_LOG(("Flip Command Complete Data %u for display device %u:",
						ui32CmdCounter, psDeviceNode->sDevId.ui32DeviceIndex))

				for (ui32SyncCounter = 0;
					 ui32SyncCounter < psCmdCompleteData->ui32SrcSyncCount;
					 ui32SyncCounter++)
				{
					QueueDumpCmdComplete(psCmdCompleteData, ui32SyncCounter, IMG_TRUE);
				}

				for (ui32SyncCounter = 0;
					 ui32SyncCounter < psCmdCompleteData->ui32DstSyncCount;
					 ui32SyncCounter++)
				{
					QueueDumpCmdComplete(psCmdCompleteData, ui32SyncCounter, IMG_FALSE);
				}
			}
		}
		else
		{
			PVR_LOG(("There is no Command Complete Data for display device %u", psDeviceNode->sDevId.ui32DeviceIndex))
		}
	}
}


IMG_VOID QueueDumpDebugInfo(IMG_VOID)
{
	SYS_DATA	*psSysData;
	SysAcquireData(&psSysData);
	List_PVRSRV_DEVICE_NODE_ForEach(psSysData->psDeviceNodeList, &QueueDumpDebugInfo_ForEachCb);
}


/*****************************************************************************
	Kernel-side functions of User->Kernel transitions
******************************************************************************/

static IMG_SIZE_T NearestPower2(IMG_SIZE_T uValue)
{
	IMG_SIZE_T uTemp, uResult = 1;

	if(!uValue)
		return 0;

	uTemp = uValue - 1;
	while(uTemp)
	{
		uResult <<= 1;
		uTemp >>= 1;
	}

	return uResult;
}


/*!
******************************************************************************

 @Function	PVRSRVCreateCommandQueueKM

 @Description
 Creates a new command queue into which render/blt commands etc can be
 inserted.

 @Input    uQueueSize :

 @Output   ppsQueueInfo :

 @Return   PVRSRV_ERROR  :

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateCommandQueueKM(IMG_SIZE_T uQueueSize,
													 PVRSRV_QUEUE_INFO **ppsQueueInfo)
{
	PVRSRV_QUEUE_INFO	*psQueueInfo;
	IMG_SIZE_T			uPower2QueueSize = NearestPower2(uQueueSize);
	SYS_DATA			*psSysData;
	PVRSRV_ERROR		eError;
	IMG_HANDLE			hMemBlock;

	SysAcquireData(&psSysData);

	/* allocate an internal queue info structure */
	eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					 sizeof(PVRSRV_QUEUE_INFO),
					 (IMG_VOID **)&psQueueInfo, &hMemBlock,
					 "Queue Info");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateCommandQueueKM: Failed to alloc queue struct"));
		goto ErrorExit;
	}
	OSMemSet(psQueueInfo, 0, sizeof(PVRSRV_QUEUE_INFO));

	psQueueInfo->hMemBlock[0] = hMemBlock;
	psQueueInfo->ui32ProcessID = OSGetCurrentProcessIDKM();

	/* allocate the command queue buffer - allow for overrun */
	eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					 uPower2QueueSize + PVRSRV_MAX_CMD_SIZE,
					 &psQueueInfo->pvLinQueueKM, &hMemBlock,
					 "Command Queue");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateCommandQueueKM: Failed to alloc queue buffer"));
		goto ErrorExit;
	}

	psQueueInfo->hMemBlock[1] = hMemBlock;
	psQueueInfo->pvLinQueueUM = psQueueInfo->pvLinQueueKM;

	/* Sanity check: Should be zeroed by OSMemSet */
	PVR_ASSERT(psQueueInfo->uReadOffset == 0);
	PVR_ASSERT(psQueueInfo->uWriteOffset == 0);

	psQueueInfo->uQueueSize = uPower2QueueSize;

	/* if this is the first q, create a lock resource for the q list */
	if (psSysData->psQueueList == IMG_NULL)
	{
		eError = OSCreateResource(&psSysData->sQProcessResource);
		if (eError != PVRSRV_OK)
		{
			goto ErrorExit;
		}
	}

	/* Ensure we don't corrupt queue list, by blocking access */
	eError = OSLockResource(&psSysData->sQProcessResource,
							KERNEL_ID);
	if (eError != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	psQueueInfo->psNextKM = psSysData->psQueueList;
	psSysData->psQueueList = psQueueInfo;

	eError = OSUnlockResource(&psSysData->sQProcessResource, KERNEL_ID);
	if (eError != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	*ppsQueueInfo = psQueueInfo;

	return PVRSRV_OK;

ErrorExit:

	if(psQueueInfo)
	{
		if(psQueueInfo->pvLinQueueKM)
		{
			OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
						psQueueInfo->uQueueSize,
						psQueueInfo->pvLinQueueKM,
						psQueueInfo->hMemBlock[1]);
			psQueueInfo->pvLinQueueKM = IMG_NULL;
		}

		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					sizeof(PVRSRV_QUEUE_INFO),
					psQueueInfo,
					psQueueInfo->hMemBlock[0]);
		/*not nulling pointer, out of scope*/
	}

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVDestroyCommandQueueKM

 @Description	Destroys a command queue

 @Input		psQueueInfo :

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyCommandQueueKM(PVRSRV_QUEUE_INFO *psQueueInfo)
{
	PVRSRV_QUEUE_INFO	*psQueue;
	SYS_DATA			*psSysData;
	PVRSRV_ERROR		eError;
	IMG_BOOL			bTimeout = IMG_TRUE;

	SysAcquireData(&psSysData);

	psQueue = psSysData->psQueueList;

	/* PRQA S 3415,4109 1 */ /* macro format critical - leave alone */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		if(psQueueInfo->uReadOffset == psQueueInfo->uWriteOffset)
		{
			bTimeout = IMG_FALSE;
			break;
		}
		OSSleepms(1);
	} END_LOOP_UNTIL_TIMEOUT();

	if (bTimeout)
	{
		/* The command queue could not be flushed within the timeout period.
		Allow the queue to be destroyed before returning the error code. */
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDestroyCommandQueueKM : Failed to empty queue"));
		eError = PVRSRV_ERROR_CANNOT_FLUSH_QUEUE;
		goto ErrorExit;
	}

	/* Ensure we don't corrupt queue list, by blocking access */
	eError = OSLockResource(&psSysData->sQProcessResource,
								KERNEL_ID);
	if (eError != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	if(psQueue == psQueueInfo)
	{
		psSysData->psQueueList = psQueueInfo->psNextKM;

		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					NearestPower2(psQueueInfo->uQueueSize) + PVRSRV_MAX_CMD_SIZE,
					psQueueInfo->pvLinQueueKM,
					psQueueInfo->hMemBlock[1]);
		psQueueInfo->pvLinQueueKM = IMG_NULL;
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					sizeof(PVRSRV_QUEUE_INFO),
					psQueueInfo,
					psQueueInfo->hMemBlock[0]);
		/* PRQA S 3199 1 */ /* see note */
		psQueueInfo = IMG_NULL; /*it's a copy on stack, but null it because the function doesn't end right here*/
	}
	else
	{
		while(psQueue)
		{
			if(psQueue->psNextKM == psQueueInfo)
			{
				psQueue->psNextKM = psQueueInfo->psNextKM;

				OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
							psQueueInfo->uQueueSize,
							psQueueInfo->pvLinQueueKM,
							psQueueInfo->hMemBlock[1]);
				psQueueInfo->pvLinQueueKM = IMG_NULL;
				OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
							sizeof(PVRSRV_QUEUE_INFO),
							psQueueInfo,
							psQueueInfo->hMemBlock[0]);
				/* PRQA S 3199 1 */ /* see note */
				psQueueInfo = IMG_NULL; /*it's a copy on stack, but null it because the function doesn't end right here*/
				break;
			}
			psQueue = psQueue->psNextKM;
		}

		if(!psQueue)
		{
			eError = OSUnlockResource(&psSysData->sQProcessResource, KERNEL_ID);
			if (eError != PVRSRV_OK)
			{
				goto ErrorExit;
			}
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto ErrorExit;
		}
	}

	/*  unlock the Q list lock resource */
	eError = OSUnlockResource(&psSysData->sQProcessResource, KERNEL_ID);
	if (eError != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	/*  if the Q list is now empty, destroy the Q list lock resource */
	if (psSysData->psQueueList == IMG_NULL)
	{
		eError = OSDestroyResource(&psSysData->sQProcessResource);
		if (eError != PVRSRV_OK)
		{
			goto ErrorExit;
		}
	}

ErrorExit:

	return eError;
}


/*!
*****************************************************************************

 @Function	: PVRSRVGetQueueSpaceKM

 @Description	: Waits for queue access rights and checks for available space in
			  queue for task param structure

 @Input	: psQueue	   	- pointer to queue information struct
 @Input : ui32ParamSize - size of task data structure
 @Output : ppvSpace

 @Return	: PVRSRV_ERROR
*****************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetQueueSpaceKM(PVRSRV_QUEUE_INFO *psQueue,
												IMG_SIZE_T uParamSize,
												IMG_VOID **ppvSpace)
{
	IMG_BOOL bTimeout = IMG_TRUE;

	/*	round to 4byte units */
	uParamSize =  (uParamSize + 3) & 0xFFFFFFFC;

	if (uParamSize > PVRSRV_MAX_CMD_SIZE)
	{
		PVR_DPF((PVR_DBG_WARNING,"PVRSRVGetQueueSpace: max command size is %d bytes", PVRSRV_MAX_CMD_SIZE));
		return PVRSRV_ERROR_CMD_TOO_BIG;
	}

	/* PRQA S 3415,4109 1 */ /* macro format critical - leave alone */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		if (GET_SPACE_IN_CMDQ(psQueue) > uParamSize)
		{
			bTimeout = IMG_FALSE;
			break;
		}
		OSSleepms(1);
	} END_LOOP_UNTIL_TIMEOUT();

	if (bTimeout == IMG_TRUE)
	{
		*ppvSpace = IMG_NULL;

		return PVRSRV_ERROR_CANNOT_GET_QUEUE_SPACE;
	}
	else
	{
		*ppvSpace = (IMG_VOID *)((IMG_UINTPTR_T)psQueue->pvLinQueueUM + psQueue->uWriteOffset);
	}

	return PVRSRV_OK;
}


/*!
*****************************************************************************
 @Function	PVRSRVInsertCommandKM

 @Description :
			command insertion utility
			 - waits for space in the queue for a new command
			 - fills in generic command information
			 - returns a pointer to the caller who's expected to then fill
			 	in the private data.
			The caller should follow PVRSRVInsertCommand with PVRSRVSubmitCommand
			which will update the queue's write offset so the command can be
			executed.

 @Input		psQueue : pointer to queue information struct

 @Output	ppvCmdData : holds pointer to space in queue for private cmd data

 @Return	PVRSRV_ERROR
*****************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVInsertCommandKM(PVRSRV_QUEUE_INFO	*psQueue,
												PVRSRV_COMMAND		**ppsCommand,
												IMG_UINT32			ui32DevIndex,
												IMG_UINT16			CommandType,
												IMG_UINT32			ui32DstSyncCount,
												PVRSRV_KERNEL_SYNC_INFO	*apsDstSync[],
												IMG_UINT32			ui32SrcSyncCount,
												PVRSRV_KERNEL_SYNC_INFO	*apsSrcSync[],
												IMG_SIZE_T			uDataByteSize,
												PFN_QUEUE_COMMAND_COMPLETE pfnCommandComplete,
												IMG_HANDLE			hCallbackData)
{
	PVRSRV_ERROR 	eError;
	PVRSRV_COMMAND	*psCommand;
	IMG_SIZE_T		uCommandSize;
	IMG_UINT32		i;
	SYS_DATA *psSysData;
	DEVICE_COMMAND_DATA *psDeviceCommandData;

	/* Check that we've got enough space in our command complete data for this command */
	SysAcquireData(&psSysData);
	psDeviceCommandData = psSysData->apsDeviceCommandData[ui32DevIndex];

	if ((psDeviceCommandData[CommandType].ui32MaxDstSyncCount < ui32DstSyncCount) ||
	   (psDeviceCommandData[CommandType].ui32MaxSrcSyncCount < ui32SrcSyncCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVInsertCommandKM: Too many syncs"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Round up to nearest 32 bit size so pointer arithmetic works */
	uDataByteSize = (uDataByteSize + 3UL) & ~3UL;

	/*  calc. command size */
	uCommandSize = sizeof(PVRSRV_COMMAND)
					+ ((ui32DstSyncCount + ui32SrcSyncCount) * sizeof(PVRSRV_SYNC_OBJECT))
					+ uDataByteSize;

	/* wait for space in queue */
	eError = PVRSRVGetQueueSpaceKM (psQueue, uCommandSize, (IMG_VOID**)&psCommand);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	psCommand->ui32ProcessID	= OSGetCurrentProcessIDKM();

	/* setup the command */
	psCommand->uCmdSize			= uCommandSize; /* this may change if cmd shrinks */
	psCommand->ui32DevIndex 	= ui32DevIndex;
	psCommand->CommandType 		= CommandType;
	psCommand->ui32DstSyncCount	= ui32DstSyncCount;
	psCommand->ui32SrcSyncCount	= ui32SrcSyncCount;
	/* override QAC warning about stricter pointers */
	/* PRQA S 3305 END_PTR_ASSIGNMENTS */
	psCommand->psDstSync		= (PVRSRV_SYNC_OBJECT*)(((IMG_UINTPTR_T)psCommand) + sizeof(PVRSRV_COMMAND));


	psCommand->psSrcSync		= (PVRSRV_SYNC_OBJECT*)(((IMG_UINTPTR_T)psCommand->psDstSync)
								+ (ui32DstSyncCount * sizeof(PVRSRV_SYNC_OBJECT)));

	psCommand->pvData			= (PVRSRV_SYNC_OBJECT*)(((IMG_UINTPTR_T)psCommand->psSrcSync)
								+ (ui32SrcSyncCount * sizeof(PVRSRV_SYNC_OBJECT)));
/* PRQA L:END_PTR_ASSIGNMENTS */

	psCommand->uDataSize		= uDataByteSize;/* this may change if cmd shrinks */

	psCommand->pfnCommandComplete = pfnCommandComplete;
	psCommand->hCallbackData = hCallbackData;

	PVR_TTRACE(PVRSRV_TRACE_GROUP_QUEUE, PVRSRV_TRACE_CLASS_CMD_START, QUEUE_TOKEN_INSERTKM);
	PVR_TTRACE_UI32(PVRSRV_TRACE_GROUP_QUEUE, PVRSRV_TRACE_CLASS_NONE,
			QUEUE_TOKEN_COMMAND_TYPE, CommandType);

	/* setup dst sync objects and their sync dependencies */
	for (i=0; i<ui32DstSyncCount; i++)
	{
		PVR_TTRACE_SYNC_OBJECT(PVRSRV_TRACE_GROUP_QUEUE, QUEUE_TOKEN_DST_SYNC,
						apsDstSync[i], PVRSRV_SYNCOP_SAMPLE);

		psCommand->psDstSync[i].psKernelSyncInfoKM = apsDstSync[i];
		psCommand->psDstSync[i].ui32WriteOpsPending = PVRSRVGetWriteOpsPending(apsDstSync[i], IMG_FALSE);
		psCommand->psDstSync[i].ui32ReadOps2Pending = PVRSRVGetReadOpsPending(apsDstSync[i], IMG_FALSE);

		PVRSRVKernelSyncInfoIncRef(apsDstSync[i], IMG_NULL);

		PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVInsertCommandKM: Dst %u RO-VA:0x%x WO-VA:0x%x ROP:0x%x WOP:0x%x",
				i, psCommand->psDstSync[i].psKernelSyncInfoKM->sReadOps2CompleteDevVAddr.uiAddr,
				psCommand->psDstSync[i].psKernelSyncInfoKM->sWriteOpsCompleteDevVAddr.uiAddr,
				psCommand->psDstSync[i].ui32ReadOps2Pending,
				psCommand->psDstSync[i].ui32WriteOpsPending));
	}

	/* setup src sync objects and their sync dependencies */
	for (i=0; i<ui32SrcSyncCount; i++)
	{
		PVR_TTRACE_SYNC_OBJECT(PVRSRV_TRACE_GROUP_QUEUE, QUEUE_TOKEN_DST_SYNC,
						apsSrcSync[i], PVRSRV_SYNCOP_SAMPLE);

		psCommand->psSrcSync[i].psKernelSyncInfoKM = apsSrcSync[i];
		psCommand->psSrcSync[i].ui32WriteOpsPending = PVRSRVGetWriteOpsPending(apsSrcSync[i], IMG_TRUE);
		psCommand->psSrcSync[i].ui32ReadOps2Pending = PVRSRVGetReadOpsPending(apsSrcSync[i], IMG_TRUE);

		PVRSRVKernelSyncInfoIncRef(apsSrcSync[i], IMG_NULL);

		PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVInsertCommandKM: Src %u RO-VA:0x%x WO-VA:0x%x ROP:0x%x WOP:0x%x",
				i, psCommand->psSrcSync[i].psKernelSyncInfoKM->sReadOps2CompleteDevVAddr.uiAddr,
				psCommand->psSrcSync[i].psKernelSyncInfoKM->sWriteOpsCompleteDevVAddr.uiAddr,
				psCommand->psSrcSync[i].ui32ReadOps2Pending,
				psCommand->psSrcSync[i].ui32WriteOpsPending));
	}
	PVR_TTRACE(PVRSRV_TRACE_GROUP_QUEUE, PVRSRV_TRACE_CLASS_CMD_END, QUEUE_TOKEN_INSERTKM);

	/* return pointer to caller to fill out private data */
	*ppsCommand = psCommand;

	return PVRSRV_OK;
}


/*!
*******************************************************************************
 @Function	: PVRSRVSubmitCommandKM

 @Description :
 			 updates the queue's write offset so the command can be executed.

 @Input	: psQueue 		-	queue command is in
 @Input	: psCommand

 @Return : PVRSRV_ERROR
******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSubmitCommandKM(PVRSRV_QUEUE_INFO *psQueue,
												PVRSRV_COMMAND *psCommand)
{
	/* override QAC warnings about stricter pointers */
	/* PRQA S 3305 END_PTR_ASSIGNMENTS2 */
	/* patch pointers in the command to be kernel pointers */
	if (psCommand->ui32DstSyncCount > 0)
	{
		psCommand->psDstSync = (PVRSRV_SYNC_OBJECT*)(((IMG_UINTPTR_T)psQueue->pvLinQueueKM)
									+ psQueue->uWriteOffset + sizeof(PVRSRV_COMMAND));
	}

	if (psCommand->ui32SrcSyncCount > 0)
	{
		psCommand->psSrcSync = (PVRSRV_SYNC_OBJECT*)(((IMG_UINTPTR_T)psQueue->pvLinQueueKM)
									+ psQueue->uWriteOffset + sizeof(PVRSRV_COMMAND)
									+ (psCommand->ui32DstSyncCount * sizeof(PVRSRV_SYNC_OBJECT)));
	}

	psCommand->pvData = (PVRSRV_SYNC_OBJECT*)(((IMG_UINTPTR_T)psQueue->pvLinQueueKM)
									+ psQueue->uWriteOffset + sizeof(PVRSRV_COMMAND)
									+ (psCommand->ui32DstSyncCount * sizeof(PVRSRV_SYNC_OBJECT))
									+ (psCommand->ui32SrcSyncCount * sizeof(PVRSRV_SYNC_OBJECT)));

/* PRQA L:END_PTR_ASSIGNMENTS2 */

	/* update write offset before releasing access lock */
	UPDATE_QUEUE_WOFF(psQueue, psCommand->uCmdSize);

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	CheckIfSyncIsQueued

 @Description	Check if the specificed sync object is already queued and
                can safely be given to the display controller.
                This check is required as a 3rd party displayclass device can
                have several flips "in flight" and we need to ensure that we
                keep their pipeline full and don't deadlock waiting for them
                to complete an operation on a surface.

 @Input		psSysData : system data
 @Input		psCmdData : COMMAND_COMPLETE_DATA structure

 @Return	PVRSRV_ERROR

******************************************************************************/
static
PVRSRV_ERROR CheckIfSyncIsQueued(PVRSRV_SYNC_OBJECT *psSync, COMMAND_COMPLETE_DATA *psCmdData)
{
	IMG_UINT32 k;
 
	if (psCmdData->bInUse)
	{
		for (k=0;k<psCmdData->ui32SrcSyncCount;k++)
		{
			if (psSync->psKernelSyncInfoKM == psCmdData->psSrcSync[k].psKernelSyncInfoKM)
			{
				PVRSRV_SYNC_DATA *psSyncData = psSync->psKernelSyncInfoKM->psSyncData;
				IMG_UINT32 ui32WriteOpsComplete = psSyncData->ui32WriteOpsComplete;

				/*
					We still need to ensure that we don't we don't give a command
					to the display controller if writes are outstanding on it
				*/
				if (ui32WriteOpsComplete == psSync->ui32WriteOpsPending)
				{
					return PVRSRV_OK;
				}
				else
				{
					if (SYNCOPS_STALE(ui32WriteOpsComplete, psSync->ui32WriteOpsPending))
					{
						PVR_DPF((PVR_DBG_WARNING,
								"CheckIfSyncIsQueued: Stale syncops psSyncData:0x%p ui32WriteOpsComplete:0x%x ui32WriteOpsPending:0x%x",
								psSyncData, ui32WriteOpsComplete, psSync->ui32WriteOpsPending));
						return PVRSRV_OK;
					}
				}
			}
		}
	}
	return PVRSRV_ERROR_FAILED_DEPENDENCIES;
}

/*!
******************************************************************************

 @Function	PVRSRVProcessCommand

 @Description	Tries to process a command

 @Input		psSysData : system data
 @Input		psCommand : PVRSRV_COMMAND structure
 @Input		bFlush : Check for stale dependencies (only used for HW recovery)

 @Return	PVRSRV_ERROR

******************************************************************************/
static
PVRSRV_ERROR PVRSRVProcessCommand(SYS_DATA			*psSysData,
								  PVRSRV_COMMAND	*psCommand,
								  IMG_BOOL			bFlush)
{
	PVRSRV_SYNC_OBJECT		*psWalkerObj;
	PVRSRV_SYNC_OBJECT		*psEndObj;
	IMG_UINT32				i;
	COMMAND_COMPLETE_DATA	*psCmdCompleteData;
	PVRSRV_ERROR			eError = PVRSRV_OK;
	IMG_UINT32				ui32WriteOpsComplete;
	IMG_UINT32				ui32ReadOpsComplete;
	DEVICE_COMMAND_DATA		*psDeviceCommandData;
	IMG_UINT32				ui32CCBOffset;

	/* satisfy sync dependencies on the DST(s) */
	psWalkerObj = psCommand->psDstSync;
	psEndObj = psWalkerObj + psCommand->ui32DstSyncCount;
	while (psWalkerObj < psEndObj)
	{
		PVRSRV_SYNC_DATA *psSyncData = psWalkerObj->psKernelSyncInfoKM->psSyncData;

		ui32WriteOpsComplete = psSyncData->ui32WriteOpsComplete;
		ui32ReadOpsComplete = psSyncData->ui32ReadOps2Complete;
		/* fail if reads or writes are not up to date */
		if ((ui32WriteOpsComplete != psWalkerObj->ui32WriteOpsPending)
		||	(ui32ReadOpsComplete != psWalkerObj->ui32ReadOps2Pending))
		{
			if (!bFlush ||
				!SYNCOPS_STALE(ui32WriteOpsComplete, psWalkerObj->ui32WriteOpsPending) ||
				!SYNCOPS_STALE(ui32ReadOpsComplete, psWalkerObj->ui32ReadOps2Pending))
			{
				return PVRSRV_ERROR_FAILED_DEPENDENCIES;
			}
		}

		psWalkerObj++;
	}

	/* satisfy sync dependencies on the SRC(s) */
	psWalkerObj = psCommand->psSrcSync;
	psEndObj = psWalkerObj + psCommand->ui32SrcSyncCount;
	while (psWalkerObj < psEndObj)
	{
		PVRSRV_SYNC_DATA *psSyncData = psWalkerObj->psKernelSyncInfoKM->psSyncData;

		ui32ReadOpsComplete = psSyncData->ui32ReadOps2Complete;
		ui32WriteOpsComplete = psSyncData->ui32WriteOpsComplete;
		/* fail if writes are not up to date */
		if ((ui32WriteOpsComplete != psWalkerObj->ui32WriteOpsPending)
		|| (ui32ReadOpsComplete != psWalkerObj->ui32ReadOps2Pending))
		{
			if (!bFlush &&
				SYNCOPS_STALE(ui32WriteOpsComplete, psWalkerObj->ui32WriteOpsPending) &&
				SYNCOPS_STALE(ui32ReadOpsComplete, psWalkerObj->ui32ReadOps2Pending))
			{
				PVR_DPF((PVR_DBG_WARNING,
						"PVRSRVProcessCommand: Stale syncops psSyncData:0x%p ui32WriteOpsComplete:0x%x ui32WriteOpsPending:0x%x",
						psSyncData, ui32WriteOpsComplete, psWalkerObj->ui32WriteOpsPending));
			}

			if (!bFlush ||
				!SYNCOPS_STALE(ui32WriteOpsComplete, psWalkerObj->ui32WriteOpsPending) ||
				!SYNCOPS_STALE(ui32ReadOpsComplete, psWalkerObj->ui32ReadOps2Pending))
			{
				IMG_UINT32 j;
				PVRSRV_ERROR eError;
				IMG_BOOL bFound = IMG_FALSE;

				psDeviceCommandData = psSysData->apsDeviceCommandData[psCommand->ui32DevIndex];
				for (j=0;j<DC_NUM_COMMANDS_PER_TYPE;j++)
				{
					eError = CheckIfSyncIsQueued(psWalkerObj, psDeviceCommandData[psCommand->CommandType].apsCmdCompleteData[j]);

					if (eError == PVRSRV_OK)
					{
						bFound = IMG_TRUE;
					}
				}
				if (!bFound)
					return PVRSRV_ERROR_FAILED_DEPENDENCIES;
			}
		}
		psWalkerObj++;
	}

	/* validate device type */
	if (psCommand->ui32DevIndex >= SYS_DEVICE_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR,
					"PVRSRVProcessCommand: invalid DeviceType 0x%x",
					psCommand->ui32DevIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* fish out the appropriate storage structure for the duration of the command */
	psDeviceCommandData = psSysData->apsDeviceCommandData[psCommand->ui32DevIndex];
	ui32CCBOffset = psDeviceCommandData[psCommand->CommandType].ui32CCBOffset;
	psCmdCompleteData = psDeviceCommandData[psCommand->CommandType].apsCmdCompleteData[ui32CCBOffset];
	if (psCmdCompleteData->bInUse)
	{
		/* can use this to protect against concurrent execution of same command */
		return PVRSRV_ERROR_FAILED_DEPENDENCIES;
	}

	/* mark the structure as in use */
	psCmdCompleteData->bInUse = IMG_TRUE;

	/* copy src updates over */
	psCmdCompleteData->ui32DstSyncCount = psCommand->ui32DstSyncCount;
	for (i=0; i<psCommand->ui32DstSyncCount; i++)
	{
		psCmdCompleteData->psDstSync[i] = psCommand->psDstSync[i];

		PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVProcessCommand: Dst %u RO-VA:0x%x WO-VA:0x%x ROP:0x%x WOP:0x%x (CCB:%u)",
				i, psCmdCompleteData->psDstSync[i].psKernelSyncInfoKM->sReadOps2CompleteDevVAddr.uiAddr,
				psCmdCompleteData->psDstSync[i].psKernelSyncInfoKM->sWriteOpsCompleteDevVAddr.uiAddr,
				psCmdCompleteData->psDstSync[i].ui32ReadOps2Pending,
				psCmdCompleteData->psDstSync[i].ui32WriteOpsPending,
				ui32CCBOffset));
	}

	psCmdCompleteData->pfnCommandComplete = psCommand->pfnCommandComplete;
	psCmdCompleteData->hCallbackData = psCommand->hCallbackData;

	/* copy dst updates over */
	psCmdCompleteData->ui32SrcSyncCount = psCommand->ui32SrcSyncCount;
	for (i=0; i<psCommand->ui32SrcSyncCount; i++)
	{
		psCmdCompleteData->psSrcSync[i] = psCommand->psSrcSync[i];

		PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVProcessCommand: Src %u RO-VA:0x%x WO-VA:0x%x ROP:0x%x WOP:0x%x (CCB:%u)",
				i, psCmdCompleteData->psSrcSync[i].psKernelSyncInfoKM->sReadOps2CompleteDevVAddr.uiAddr,
				psCmdCompleteData->psSrcSync[i].psKernelSyncInfoKM->sWriteOpsCompleteDevVAddr.uiAddr,
				psCmdCompleteData->psSrcSync[i].ui32ReadOps2Pending,
				psCmdCompleteData->psSrcSync[i].ui32WriteOpsPending,
				ui32CCBOffset));
	}

	/*
		call the cmd specific handler:
		it should:
		 - check the cmd specific dependencies
		 - setup private cmd complete structure
		 - execute cmd on HW
		 - store psCmdCompleteData `cookie' and later pass as
			argument to Generic Command Complete Callback

		n.b. ui32DataSize (packet size) is useful for packet validation
	*/
	if (psDeviceCommandData[psCommand->CommandType].pfnCmdProc((IMG_HANDLE)psCmdCompleteData,
															   (IMG_UINT32)psCommand->uDataSize,
															   psCommand->pvData) == IMG_FALSE)
	{
		/*
			clean-up:
			free cmd complete structure
		*/
		psCmdCompleteData->bInUse = IMG_FALSE;
		eError = PVRSRV_ERROR_CMD_NOT_PROCESSED;
	}
	
	/* Increment the CCB offset */
	psDeviceCommandData[psCommand->CommandType].ui32CCBOffset = (ui32CCBOffset + 1) % DC_NUM_COMMANDS_PER_TYPE;

	return eError;
}


static IMG_VOID PVRSRVProcessQueues_ForEachCb(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if (psDeviceNode->bReProcessDeviceCommandComplete &&
		psDeviceNode->pfnDeviceCommandComplete != IMG_NULL)
	{
		(*psDeviceNode->pfnDeviceCommandComplete)(psDeviceNode);
	}
}

/*!
******************************************************************************

 @Function	PVRSRVProcessQueues

 @Description	Tries to process a command from each Q

 @input ui32CallerID - used to distinguish between async ISR/DPC type calls
 						the synchronous services driver
 @input	bFlush - flush commands with stale dependencies (only used for HW recovery)

 @Return	PVRSRV_ERROR

******************************************************************************/

IMG_EXPORT
PVRSRV_ERROR PVRSRVProcessQueues(IMG_BOOL	bFlush)
{
	PVRSRV_QUEUE_INFO 	*psQueue;
	SYS_DATA			*psSysData;
	PVRSRV_COMMAND 		*psCommand;
/*	PVRSRV_DEVICE_NODE	*psDeviceNode;*/

	SysAcquireData(&psSysData);

	/* Ensure we don't corrupt queue list, by blocking access. This is required for OSs where
	    multiple ISR threads may exist simultaneously (eg WinXP DPC routines)
	*/
	while (OSLockResource(&psSysData->sQProcessResource, ISR_ID) != PVRSRV_OK)
	{
		OSWaitus(1);
	};
	
	psQueue = psSysData->psQueueList;

	if(!psQueue)
	{
		PVR_DPF((PVR_DBG_MESSAGE,"No Queues installed - cannot process commands"));
	}

	if (bFlush)
	{
		PVRSRVSetDCState(DC_STATE_FLUSH_COMMANDS);
	}

	while (psQueue)
	{
		while (psQueue->uReadOffset != psQueue->uWriteOffset)
		{
			psCommand = (PVRSRV_COMMAND*)((IMG_UINTPTR_T)psQueue->pvLinQueueKM + psQueue->uReadOffset);

			if (PVRSRVProcessCommand(psSysData, psCommand, bFlush) == PVRSRV_OK)
			{
				/* processed cmd so update queue */
				UPDATE_QUEUE_ROFF(psQueue, psCommand->uCmdSize)
				continue;
			}

			break;
		}
		psQueue = psQueue->psNextKM;
	}

	if (bFlush)
	{
		PVRSRVSetDCState(DC_STATE_NO_FLUSH_COMMANDS);
	}

	/* Re-process command complete handlers if necessary. */
	List_PVRSRV_DEVICE_NODE_ForEach(psSysData->psDeviceNodeList,
									&PVRSRVProcessQueues_ForEachCb);

	OSUnlockResource(&psSysData->sQProcessResource, ISR_ID);

	return PVRSRV_OK;
}

#if defined(SUPPORT_CUSTOM_SWAP_OPERATIONS)
/*!
******************************************************************************

 @Function	PVRSRVCommandCompleteKM

 @Description	Updates non-private command complete sync objects

 @Input		hCmdCookie : command cookie
 @Input		bScheduleMISR : obsolete parameter

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_INTERNAL
IMG_VOID PVRSRVFreeCommandCompletePacketKM(IMG_HANDLE	hCmdCookie,
										   IMG_BOOL		bScheduleMISR)
{
	COMMAND_COMPLETE_DATA	*psCmdCompleteData = (COMMAND_COMPLETE_DATA *)hCmdCookie;
	SYS_DATA				*psSysData;

	PVR_UNREFERENCED_PARAMETER(bScheduleMISR);

	SysAcquireData(&psSysData);

	/* free command complete storage */
	psCmdCompleteData->bInUse = IMG_FALSE;

	/* FIXME: This may cause unrelated devices to be woken up. */
	PVRSRVScheduleDeviceCallbacks();

	/* the MISR is always scheduled, regardless of bScheduleMISR */
	OSScheduleMISR(psSysData);
}

#endif /* (SUPPORT_CUSTOM_SWAP_OPERATIONS) */


/*!
******************************************************************************

 @Function	PVRSRVCommandCompleteKM

 @Description	Updates non-private command complete sync objects

 @Input		hCmdCookie : command cookie
 @Input		bScheduleMISR : boolean to schedule MISR

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
IMG_VOID PVRSRVCommandCompleteKM(IMG_HANDLE	hCmdCookie,
								 IMG_BOOL	bScheduleMISR)
{
	IMG_UINT32				i;
	COMMAND_COMPLETE_DATA	*psCmdCompleteData = (COMMAND_COMPLETE_DATA *)hCmdCookie;
	SYS_DATA				*psSysData;

	SysAcquireData(&psSysData);

	PVR_TTRACE(PVRSRV_TRACE_GROUP_QUEUE, PVRSRV_TRACE_CLASS_CMD_COMP_START,
			QUEUE_TOKEN_COMMAND_COMPLETE);

	/* update DST(s) syncs */
	for (i=0; i<psCmdCompleteData->ui32DstSyncCount; i++)
	{
		psCmdCompleteData->psDstSync[i].psKernelSyncInfoKM->psSyncData->ui32WriteOpsComplete++;

		PVRSRVKernelSyncInfoDecRef(psCmdCompleteData->psDstSync[i].psKernelSyncInfoKM, IMG_NULL);

		PVR_TTRACE_SYNC_OBJECT(PVRSRV_TRACE_GROUP_QUEUE, QUEUE_TOKEN_UPDATE_DST,
					  psCmdCompleteData->psDstSync[i].psKernelSyncInfoKM,
					  PVRSRV_SYNCOP_COMPLETE);

		PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVCommandCompleteKM: Dst %u RO-VA:0x%x WO-VA:0x%x ROP:0x%x WOP:0x%x",
				i, psCmdCompleteData->psDstSync[i].psKernelSyncInfoKM->sReadOps2CompleteDevVAddr.uiAddr,
				psCmdCompleteData->psDstSync[i].psKernelSyncInfoKM->sWriteOpsCompleteDevVAddr.uiAddr,
				psCmdCompleteData->psDstSync[i].ui32ReadOps2Pending,
				psCmdCompleteData->psDstSync[i].ui32WriteOpsPending));
	}

	/* update SRC(s) syncs */
	for (i=0; i<psCmdCompleteData->ui32SrcSyncCount; i++)
	{
		psCmdCompleteData->psSrcSync[i].psKernelSyncInfoKM->psSyncData->ui32ReadOps2Complete++;

		PVRSRVKernelSyncInfoDecRef(psCmdCompleteData->psSrcSync[i].psKernelSyncInfoKM, IMG_NULL);

		PVR_TTRACE_SYNC_OBJECT(PVRSRV_TRACE_GROUP_QUEUE, QUEUE_TOKEN_UPDATE_SRC,
					  psCmdCompleteData->psSrcSync[i].psKernelSyncInfoKM,
					  PVRSRV_SYNCOP_COMPLETE);

		PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVCommandCompleteKM: Src %u RO-VA:0x%x WO-VA:0x%x ROP:0x%x WOP:0x%x",
				i, psCmdCompleteData->psSrcSync[i].psKernelSyncInfoKM->sReadOps2CompleteDevVAddr.uiAddr,
				psCmdCompleteData->psSrcSync[i].psKernelSyncInfoKM->sWriteOpsCompleteDevVAddr.uiAddr,
				psCmdCompleteData->psSrcSync[i].ui32ReadOps2Pending,
				psCmdCompleteData->psSrcSync[i].ui32WriteOpsPending));
	}

	PVR_TTRACE(PVRSRV_TRACE_GROUP_QUEUE, PVRSRV_TRACE_CLASS_CMD_COMP_END,
			QUEUE_TOKEN_COMMAND_COMPLETE);

	if (psCmdCompleteData->pfnCommandComplete)
	{
		psCmdCompleteData->pfnCommandComplete(psCmdCompleteData->hCallbackData);
	}

	/* free command complete storage */
	psCmdCompleteData->bInUse = IMG_FALSE;

	/* FIXME: This may cause unrelated devices to be woken up. */
	PVRSRVScheduleDeviceCallbacks();

	if(bScheduleMISR)
	{
		OSScheduleMISR(psSysData);
	}
}




/*!
******************************************************************************

 @Function	PVRSRVRegisterCmdProcListKM

 @Description

 registers a list of private command processing functions with the Command
 Queue Manager

 @Input		ui32DevIndex : device index

 @Input		 ppfnCmdProcList : function ptr table of private command processors

 @Input		ui32MaxSyncsPerCmd : max number of syncobjects used by command

 @Input		ui32CmdCount : number of entries in function ptr table

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRegisterCmdProcListKM(IMG_UINT32		ui32DevIndex,
										 PFN_CMD_PROC	*ppfnCmdProcList,
										 IMG_UINT32		ui32MaxSyncsPerCmd[][2],
										 IMG_UINT32		ui32CmdCount)
{
	SYS_DATA				*psSysData;
	PVRSRV_ERROR			eError;
	IMG_UINT32				ui32CmdCounter, ui32CmdTypeCounter;
	IMG_SIZE_T				ui32AllocSize;
	DEVICE_COMMAND_DATA		*psDeviceCommandData;
	COMMAND_COMPLETE_DATA	*psCmdCompleteData;

	/* validate device type */
	if(ui32DevIndex >= SYS_DEVICE_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR,
					"PVRSRVRegisterCmdProcListKM: invalid DeviceType 0x%x",
					ui32DevIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* acquire system data structure */
	SysAcquireData(&psSysData);

	/* array of pointers for each command store */
	ui32AllocSize = ui32CmdCount * sizeof(*psDeviceCommandData);
	eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
						ui32AllocSize,
						(IMG_VOID **)&psDeviceCommandData, IMG_NULL,
						"Array of Pointers for Command Store");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterCmdProcListKM: Failed to alloc CC data"));
		goto ErrorExit;
	}

	psSysData->apsDeviceCommandData[ui32DevIndex] = psDeviceCommandData;

	for (ui32CmdTypeCounter = 0; ui32CmdTypeCounter < ui32CmdCount; ui32CmdTypeCounter++)
	{
		psDeviceCommandData[ui32CmdTypeCounter].pfnCmdProc = ppfnCmdProcList[ui32CmdTypeCounter];
		psDeviceCommandData[ui32CmdTypeCounter].ui32CCBOffset = 0;
		psDeviceCommandData[ui32CmdTypeCounter].ui32MaxDstSyncCount = ui32MaxSyncsPerCmd[ui32CmdTypeCounter][0];
		psDeviceCommandData[ui32CmdTypeCounter].ui32MaxSrcSyncCount = ui32MaxSyncsPerCmd[ui32CmdTypeCounter][1];
		for (ui32CmdCounter = 0; ui32CmdCounter < DC_NUM_COMMANDS_PER_TYPE; ui32CmdCounter++)
		{
			/*
				allocate storage for the sync update on command complete
			*/
			ui32AllocSize = sizeof(COMMAND_COMPLETE_DATA) /* space for one GENERIC_CMD_COMPLETE */
						  + ((ui32MaxSyncsPerCmd[ui32CmdTypeCounter][0]
						  +	ui32MaxSyncsPerCmd[ui32CmdTypeCounter][1])
						  * sizeof(PVRSRV_SYNC_OBJECT));	 /* space for max sync objects */

			eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
								ui32AllocSize,
								(IMG_VOID **)&psCmdCompleteData,
								IMG_NULL,
								"Command Complete Data");
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterCmdProcListKM: Failed to alloc cmd %d", ui32CmdTypeCounter));
				goto ErrorExit;
			}
			
			psDeviceCommandData[ui32CmdTypeCounter].apsCmdCompleteData[ui32CmdCounter] = psCmdCompleteData;
			
			/* clear memory */
			OSMemSet(psCmdCompleteData, 0x00, ui32AllocSize);

			/* setup sync pointers */
			psCmdCompleteData->psDstSync = (PVRSRV_SYNC_OBJECT*)
											(((IMG_UINTPTR_T)psCmdCompleteData)
											+ sizeof(COMMAND_COMPLETE_DATA));
			psCmdCompleteData->psSrcSync = (PVRSRV_SYNC_OBJECT*)
											(((IMG_UINTPTR_T)psCmdCompleteData->psDstSync)
											+ (sizeof(PVRSRV_SYNC_OBJECT) * ui32MaxSyncsPerCmd[ui32CmdTypeCounter][0]));

			psCmdCompleteData->ui32AllocSize = (IMG_UINT32)ui32AllocSize;
		}
	}

	return PVRSRV_OK;

ErrorExit:

	/* clean-up if things went wrong */
	if (PVRSRVRemoveCmdProcListKM(ui32DevIndex, ui32CmdCount) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"PVRSRVRegisterCmdProcListKM: Failed to clean up after error, device 0x%x",
				ui32DevIndex));
	}
	
	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVRemoveCmdProcListKM

 @Description

 removes a list of private command processing functions and data from the
 Queue Manager

 @Input		ui32DevIndex : device index

 @Input		ui32CmdCount : number of entries in function ptr table

 @Return	PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRemoveCmdProcListKM(IMG_UINT32 ui32DevIndex,
									   IMG_UINT32 ui32CmdCount)
{
	SYS_DATA				*psSysData;
	IMG_UINT32				ui32CmdTypeCounter, ui32CmdCounter;
	DEVICE_COMMAND_DATA		*psDeviceCommandData;
	COMMAND_COMPLETE_DATA	*psCmdCompleteData;
	IMG_SIZE_T				ui32AllocSize;

	/* validate device type */
	if(ui32DevIndex >= SYS_DEVICE_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"PVRSRVRemoveCmdProcListKM: invalid DeviceType 0x%x",
				ui32DevIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* acquire system data structure */
	SysAcquireData(&psSysData);

	psDeviceCommandData = psSysData->apsDeviceCommandData[ui32DevIndex];
	if(psDeviceCommandData != IMG_NULL)
	{
		for (ui32CmdTypeCounter = 0; ui32CmdTypeCounter < ui32CmdCount; ui32CmdTypeCounter++)
		{
			for (ui32CmdCounter = 0; ui32CmdCounter < DC_NUM_COMMANDS_PER_TYPE; ui32CmdCounter++)
			{
				psCmdCompleteData = psDeviceCommandData[ui32CmdTypeCounter].apsCmdCompleteData[ui32CmdCounter];
				
				/* free the cmd complete structure array entries */
				if (psCmdCompleteData != IMG_NULL)
				{
					PVR_ASSERT(psCmdCompleteData->bInUse == IMG_FALSE);
					OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, psCmdCompleteData->ui32AllocSize,
							  psCmdCompleteData, IMG_NULL);
					psDeviceCommandData[ui32CmdTypeCounter].apsCmdCompleteData[ui32CmdCounter] = IMG_NULL;
				}
			}
		}

		/* free the cmd complete structure array for the device */
		ui32AllocSize = ui32CmdCount * sizeof(*psDeviceCommandData);
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, ui32AllocSize, psDeviceCommandData, IMG_NULL);
		psSysData->apsDeviceCommandData[ui32DevIndex] = IMG_NULL;
	}

	return PVRSRV_OK;
}

/******************************************************************************
 End of file (queue.c)
******************************************************************************/
