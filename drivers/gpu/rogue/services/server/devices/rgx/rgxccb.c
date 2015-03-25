/*************************************************************************/ /*!
@File
@Title          RGX CCb routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX CCB routines
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

#include "pvr_debug.h"
#include "rgxdevice.h"
#include "pdump_km.h"
#include "allocmem.h"
#include "devicemem.h"
#include "rgxfwutils.h"
#include "osfunc.h"
#include "rgxccb.h"
#include "rgx_memallocflags.h"
#include "devicemem_pdump.h"
#include "pvr_debug.h"
#include "dllist.h"
#include "rgx_fwif_shared.h"
#include "rgxtimerquery.h"
#if defined(LINUX)
#include "trace_events.h"
#endif

/*
 *  Defines the number of fence updates to record so that future fences in the CCB
 *  can be checked to see if they are already known to be satisfied. The value has
 *  implications for memory and host CPU usage and so should be tuned by using
 *  firmware performance measurements to trade these off against performance gains.
 *
 *  Must be a power of 2!
 */
#define RGX_CCCB_FENCE_UPDATE_LIST_SIZE  (64)


struct _RGX_CLIENT_CCB_ {
	volatile RGXFWIF_CCCB_CTL	*psClientCCBCtrl;			/*!< CPU mapping of the CCB control structure used by the fw */
	IMG_UINT8					*pui8ClientCCB;				/*!< CPU mapping of the CCB */
	DEVMEM_MEMDESC 				*psClientCCBMemDesc;		/*!< MemDesc for the CCB */
	DEVMEM_MEMDESC 				*psClientCCBCtrlMemDesc;		/*!< MemDesc for the CCB control */
	IMG_UINT32					ui32HostWriteOffset;		/*!< CCB write offset from the driver side */
	IMG_UINT32					ui32LastPDumpWriteOffset;			/*!< CCB write offset from the last time we submitted a command in capture range */
	IMG_UINT32					ui32LastROff;				/*!< Last CCB Read offset to help detect any CCB wedge */
	IMG_UINT32					ui32LastWOff;				/*!< Last CCB Write offset to help detect any CCB wedge */
	IMG_UINT32					ui32ByteCount;				/*!< Count of the number of bytes written to CCCB */
	IMG_UINT32					ui32LastByteCount;			/*!< Last value of ui32ByteCount to help detect any CCB wedge */
	IMG_UINT32					ui32Size;					/*!< Size of the CCB */
	DLLIST_NODE					sNode;						/*!< Node used to store this CCB on the per connection list */
	PDUMP_CONNECTION_DATA		*psPDumpConnectionData;		/*!< Pointer to the per connection data in which we reside */
	IMG_PVOID					hTransition;				/*!< Handle for Transition callback */
	IMG_CHAR					szName[MAX_CLIENT_CCB_NAME];/*!< Name of this client CCB */
	RGX_SERVER_COMMON_CONTEXT   *psServerCommonContext;     /*!< Parent server common context that this CCB belongs to */
#if defined REDUNDANT_SYNCS_DEBUG
	IMG_UINT32					ui32UpdateWriteIndex;		/*!< Next position to overwrite in Fence Update List */
	RGXFWIF_UFO					asFenceUpdateList[RGX_CCCB_FENCE_UPDATE_LIST_SIZE];  /*!< Cache of recent updates written in this CCB */
#endif
};

static PVRSRV_ERROR _RGXCCBPDumpTransition(IMG_PVOID *pvData, IMG_BOOL bInto, IMG_BOOL bContinuous)
{
	RGX_CLIENT_CCB *psClientCCB = (RGX_CLIENT_CCB *) pvData;
	
	IMG_UINT32 ui32PDumpFlags = bContinuous ? PDUMP_FLAGS_CONTINUOUS:0;

	/*
		We're about to Transition into capture range and we've submitted
		new commands since the last time we entered capture range so drain
		the CCB as required
	*/
	if (bInto)
	{
		volatile RGXFWIF_CCCB_CTL *psCCBCtl = psClientCCB->psClientCCBCtrl;
		PVRSRV_ERROR eError;

		/*
			Wait for the FW to catch up (retry will get pushed back out services
			client where we wait on the event object and try again later)
		*/
		if (psClientCCB->psClientCCBCtrl->ui32ReadOffset != psClientCCB->ui32HostWriteOffset)
		{
			return PVRSRV_ERROR_RETRY;
		}

		/*
			We drain whenever capture range is entered. Even if no commands
			have been issued while where out of capture range we have to wait for
			operations that we might have issued in the last capture range
			to finish so the sync prim update that will happen after all the
			PDumpTransition callbacks have been called doesn't clobber syncs
			which the FW is currently working on.
			Although this is suboptimal, while out of capture range for every
			persistent operation we serialise the PDump script processing and
			the FW, there is no easy solution.
			Not all modules that work on syncs register a PDumpTransition and
			thus we have no way of knowing if we can skip drain and the sync
			prim dump or not.
		*/
		PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags,
							  "cCCB(%s@%p): Draining rgxfw_roff == woff (%d)",
							  psClientCCB->szName,
							  psClientCCB,
							  psClientCCB->ui32LastPDumpWriteOffset);

		eError = DevmemPDumpDevmemPol32(psClientCCB->psClientCCBCtrlMemDesc,
										offsetof(RGXFWIF_CCCB_CTL, ui32ReadOffset),
										psClientCCB->ui32LastPDumpWriteOffset,
										0xffffffff,
										PDUMP_POLL_OPERATOR_EQUAL,
										ui32PDumpFlags);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "_RGXCCBPDumpTransition: problem pdumping POL for cCCBCtl (%d)", eError));
		}
		PVR_ASSERT(eError == PVRSRV_OK);

		/*
			If new command(s) have been written out of capture range then we
			need to fast forward past uncaptured operations.
		*/
		if (psClientCCB->ui32LastPDumpWriteOffset != psClientCCB->ui32HostWriteOffset)
		{
			/*
				There are commands that where not captured so after the
				simulation drain (above) we also need to fast-forward pass
				those commands so the FW can start with the 1st command
				which is in the new capture range
			 */
			psCCBCtl->ui32ReadOffset = psClientCCB->ui32HostWriteOffset;
			psCCBCtl->ui32DepOffset = psClientCCB->ui32HostWriteOffset;
			psCCBCtl->ui32WriteOffset = psClientCCB->ui32HostWriteOffset;
	
			PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags,
								  "cCCB(%s@%p): Fast-forward from %d to %d",
								  psClientCCB->szName,
								  psClientCCB,
								  psClientCCB->ui32LastPDumpWriteOffset,
								  psClientCCB->ui32HostWriteOffset);
	
			DevmemPDumpLoadMem(psClientCCB->psClientCCBCtrlMemDesc,
							   0,
							   sizeof(RGXFWIF_CCCB_CTL),
							   ui32PDumpFlags);
							   
			/*
				Although we've entered capture range we might not do any work
				on this CCB so update the ui32LastPDumpWriteOffset to reflect
				where we got to for next so we start the drain from where we
				got to last time
			*/
			psClientCCB->ui32LastPDumpWriteOffset = psClientCCB->ui32HostWriteOffset;
		}
	}
	return PVRSRV_OK;
}

PVRSRV_ERROR RGXCreateCCB(PVRSRV_DEVICE_NODE	*psDeviceNode,
						  IMG_UINT32			ui32CCBSizeLog2,
						  CONNECTION_DATA		*psConnectionData,
						  const IMG_CHAR		*pszName,
						  RGX_SERVER_COMMON_CONTEXT *psServerCommonContext,
						  RGX_CLIENT_CCB		**ppsClientCCB,
						  DEVMEM_MEMDESC 		**ppsClientCCBMemDesc,
						  DEVMEM_MEMDESC 		**ppsClientCCBCtrlMemDesc)
{
	PVRSRV_ERROR	eError;
	DEVMEM_FLAGS_T	uiClientCCBMemAllocFlags, uiClientCCBCtlMemAllocFlags;
	IMG_UINT32		ui32AllocSize = (1U << ui32CCBSizeLog2);
	RGX_CLIENT_CCB	*psClientCCB;

	psClientCCB = OSAllocMem(sizeof(*psClientCCB));
	if (psClientCCB == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}
	psClientCCB->psServerCommonContext = psServerCommonContext;

	uiClientCCBMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
	                            PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(META_CACHED) |
								PVRSRV_MEMALLOCFLAG_GPU_READABLE |
								PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
								PVRSRV_MEMALLOCFLAG_CPU_READABLE |
								PVRSRV_MEMALLOCFLAG_UNCACHED |
								PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
								PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE;

	uiClientCCBCtlMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
								PVRSRV_MEMALLOCFLAG_GPU_READABLE |
								PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
								PVRSRV_MEMALLOCFLAG_CPU_READABLE |
								
								PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE | 
								PVRSRV_MEMALLOCFLAG_UNCACHED |
								PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
								PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE;

	PDUMPCOMMENT("Allocate RGXFW cCCB");
	eError = DevmemFwAllocateExportable(psDeviceNode,
										ui32AllocSize,
										uiClientCCBMemAllocFlags,
										"FirmwareClientCCB",
										&psClientCCB->psClientCCBMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateCCBKM: Failed to allocate RGX client CCB (%s)",
				PVRSRVGetErrorStringKM(eError)));
		goto fail_alloc_ccb;
	}

	eError = DevmemAcquireCpuVirtAddr(psClientCCB->psClientCCBMemDesc,
									  (IMG_VOID **) &psClientCCB->pui8ClientCCB);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateCCBKM: Failed to map RGX client CCB (%s)",
				PVRSRVGetErrorStringKM(eError)));
		goto fail_map_ccb;
	}

	PDUMPCOMMENT("Allocate RGXFW cCCB control");
	eError = DevmemFwAllocateExportable(psDeviceNode,
										sizeof(RGXFWIF_CCCB_CTL),
										uiClientCCBCtlMemAllocFlags,
										"FirmwareClientCCBControl",
										&psClientCCB->psClientCCBCtrlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateCCBKM: Failed to allocate RGX client CCB control (%s)",
				PVRSRVGetErrorStringKM(eError)));
		goto fail_alloc_ccbctrl;
	}

	eError = DevmemAcquireCpuVirtAddr(psClientCCB->psClientCCBCtrlMemDesc,
									  (IMG_VOID **) &psClientCCB->psClientCCBCtrl);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateCCBKM: Failed to map RGX client CCB (%s)",
				PVRSRVGetErrorStringKM(eError)));
		goto fail_map_ccbctrl;
	}

	psClientCCB->psClientCCBCtrl->ui32WriteOffset = 0;
	psClientCCB->psClientCCBCtrl->ui32ReadOffset = 0;
	psClientCCB->psClientCCBCtrl->ui32DepOffset = 0;
	psClientCCB->psClientCCBCtrl->ui32WrapMask = ui32AllocSize - 1;
	OSSNPrintf(psClientCCB->szName, MAX_CLIENT_CCB_NAME, "%s-P%lu-T%lu-%s",
									pszName,
									(unsigned long) OSGetCurrentProcessIDKM(),
									(unsigned long) OSGetCurrentThreadIDKM(),
									OSGetCurrentProcessNameKM());

	PDUMPCOMMENT("cCCB control");
	DevmemPDumpLoadMem(psClientCCB->psClientCCBCtrlMemDesc,
					   0,
					   sizeof(RGXFWIF_CCCB_CTL),
					   PDUMP_FLAGS_CONTINUOUS);
	PVR_ASSERT(eError == PVRSRV_OK);

	psClientCCB->ui32HostWriteOffset = 0;
	psClientCCB->ui32LastPDumpWriteOffset = 0;
	psClientCCB->ui32Size = ui32AllocSize;
	psClientCCB->ui32LastROff = ui32AllocSize - 1;
	psClientCCB->ui32ByteCount = 0;
	psClientCCB->ui32LastByteCount = 0;

#if defined REDUNDANT_SYNCS_DEBUG
	psClientCCB->ui32UpdateWriteIndex = 0;
	OSMemSet(psClientCCB->asFenceUpdateList, 0, sizeof(psClientCCB->asFenceUpdateList));
#endif

	eError = PDumpRegisterTransitionCallback(psConnectionData->psPDumpConnectionData,
											  _RGXCCBPDumpTransition,
											  psClientCCB,
											  &psClientCCB->hTransition);
	if (eError != PVRSRV_OK)
	{
		goto fail_pdumpreg;
	}
	/*
		Note:
		Due to resman the connection structure could be freed before the client
		CCB so rather then saving off the connection structure save the PDump
		specific memory which is refcounted to ensure it's not freed too early
	*/
	psClientCCB->psPDumpConnectionData = psConnectionData->psPDumpConnectionData;
	PDUMPCOMMENT("New RGXFW cCCB(%s@%p) created",
				 psClientCCB->szName,
				 psClientCCB);

	*ppsClientCCB = psClientCCB;
	*ppsClientCCBMemDesc = psClientCCB->psClientCCBMemDesc;
	*ppsClientCCBCtrlMemDesc = psClientCCB->psClientCCBCtrlMemDesc;
	return PVRSRV_OK;

fail_pdumpreg:
	DevmemReleaseCpuVirtAddr(psClientCCB->psClientCCBCtrlMemDesc);
fail_map_ccbctrl:
	DevmemFwFree(psClientCCB->psClientCCBCtrlMemDesc);
fail_alloc_ccbctrl:
	DevmemReleaseCpuVirtAddr(psClientCCB->psClientCCBMemDesc);
fail_map_ccb:
	DevmemFwFree(psClientCCB->psClientCCBMemDesc);
fail_alloc_ccb:
	OSFreeMem(psClientCCB);
fail_alloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

IMG_VOID RGXDestroyCCB(RGX_CLIENT_CCB *psClientCCB)
{
	PDumpUnregisterTransitionCallback(psClientCCB->hTransition);
	DevmemReleaseCpuVirtAddr(psClientCCB->psClientCCBCtrlMemDesc);
	DevmemFwFree(psClientCCB->psClientCCBCtrlMemDesc);
	DevmemReleaseCpuVirtAddr(psClientCCB->psClientCCBMemDesc);
	DevmemFwFree(psClientCCB->psClientCCBMemDesc);
	OSFreeMem(psClientCCB);
}


static PVRSRV_ERROR _RGXAcquireCCB(RGX_CLIENT_CCB	*psClientCCB,
								   IMG_UINT32		ui32CmdSize,
								   IMG_PVOID		*ppvBufferSpace)
{
	IMG_UINT32 ui32FreeSpace;

#if defined(PDUMP)
	/* Wait for sufficient CCB space to become available */
	PDUMPCOMMENTWITHFLAGS(0, "Wait for %u bytes to become available according cCCB Ctl (woff=%x) for %s",
							ui32CmdSize, psClientCCB->ui32HostWriteOffset,
							psClientCCB->szName);
	DevmemPDumpCBP(psClientCCB->psClientCCBCtrlMemDesc,
	               offsetof(RGXFWIF_CCCB_CTL, ui32ReadOffset),
	               psClientCCB->ui32HostWriteOffset,
	               ui32CmdSize,
	               psClientCCB->ui32Size);
#endif

	ui32FreeSpace = GET_CCB_SPACE(psClientCCB->ui32HostWriteOffset,
								  psClientCCB->psClientCCBCtrl->ui32ReadOffset,
								  psClientCCB->ui32Size);

	/* Don't allow all the space to be used */
	if (ui32FreeSpace > ui32CmdSize)
	{
		*ppvBufferSpace = (IMG_PVOID) (psClientCCB->pui8ClientCCB +
									   psClientCCB->ui32HostWriteOffset);
		return PVRSRV_OK;
	}

	return PVRSRV_ERROR_RETRY;
}

/******************************************************************************
 FUNCTION	: RGXAcquireCCB

 PURPOSE	: Obtains access to write some commands to a CCB

 PARAMETERS	: psClientCCB		- The client CCB
			  ui32CmdSize		- How much space is required
			  ppvBufferSpace	- Pointer to space in the buffer
			  bPDumpContinuous  - Should this be PDump continuous?

 RETURNS	: PVRSRV_ERROR
******************************************************************************/
IMG_INTERNAL PVRSRV_ERROR RGXAcquireCCB(RGX_CLIENT_CCB *psClientCCB,
										IMG_UINT32		ui32CmdSize,
										IMG_PVOID		*ppvBufferSpace,
										IMG_BOOL		bPDumpContinuous)
{
	PVRSRV_ERROR eError;
	IMG_UINT32	ui32PDumpFlags	= bPDumpContinuous ? PDUMP_FLAGS_CONTINUOUS : 0;
	IMG_BOOL	bInCaptureRange;
	IMG_BOOL	bPdumpEnabled;

	PDumpIsCaptureFrameKM(&bInCaptureRange);
	bPdumpEnabled = (bInCaptureRange || bPDumpContinuous);

	/*
		PDumpSetFrame will detect as we Transition into capture range for
		frame based data but if we are PDumping continuous data then we
		need to inform the PDump layer ourselves
	*/
	if (bPDumpContinuous && !bInCaptureRange)
	{
		eError = PDumpTransition(psClientCCB->psPDumpConnectionData, IMG_TRUE, IMG_TRUE);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	/* Check that the CCB can hold this command + padding */
	if ((ui32CmdSize + PADDING_COMMAND_SIZE + 1) > psClientCCB->ui32Size)
	{
		PVR_DPF((PVR_DBG_ERROR, "Command size (%d bytes) too big for CCB (%d bytes)\n",
								ui32CmdSize, psClientCCB->ui32Size));
		return PVRSRV_ERROR_CMD_TOO_BIG;
	}

	/*
		Check we don't overflow the end of the buffer and make sure we have
		enough for the padding command.
	*/
	if ((psClientCCB->ui32HostWriteOffset + ui32CmdSize + PADDING_COMMAND_SIZE) >
		psClientCCB->ui32Size)
	{
		RGXFWIF_CCB_CMD_HEADER *psHeader;
		IMG_VOID *pvHeader;
		PVRSRV_ERROR eError;
		IMG_UINT32 ui32Remain = psClientCCB->ui32Size - psClientCCB->ui32HostWriteOffset;

		/* We're at the end of the buffer without enough contiguous space */
		eError = _RGXAcquireCCB(psClientCCB,
								ui32Remain,
								&pvHeader);
		if (eError != PVRSRV_OK)
		{
			/*
				It's possible no commands have been processed in which case as we
				can fail the padding allocation due to that fact we never allow
				the client CCB to be full
			*/
			return eError;
		}
		psHeader = pvHeader;
		psHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_PADDING;
		psHeader->ui32CmdSize = ui32Remain - sizeof(RGXFWIF_CCB_CMD_HEADER);

		PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags, "cCCB(%p): Padding cmd %d", psClientCCB, psHeader->ui32CmdSize);
		if (bPdumpEnabled)
		{
			DevmemPDumpLoadMem(psClientCCB->psClientCCBMemDesc,
							   psClientCCB->ui32HostWriteOffset,
							   ui32Remain,
							   ui32PDumpFlags);
		}
				
		UPDATE_CCB_OFFSET(psClientCCB->ui32HostWriteOffset,
						  ui32Remain,
						  psClientCCB->ui32Size);
		psClientCCB->ui32ByteCount += ui32Remain;
	}

	return _RGXAcquireCCB(psClientCCB,
						  ui32CmdSize,
						  ppvBufferSpace);
}

/******************************************************************************
 FUNCTION	: RGXReleaseCCB

 PURPOSE	: Release a CCB that we have been writing to.

 PARAMETERS	: psDevData			- device data
  			  psCCB				- the CCB

 RETURNS	: None
******************************************************************************/
IMG_INTERNAL IMG_VOID RGXReleaseCCB(RGX_CLIENT_CCB *psClientCCB,
									IMG_UINT32		ui32CmdSize,
									IMG_BOOL		bPDumpContinuous)
{
	IMG_UINT32	ui32PDumpFlags	= bPDumpContinuous ? PDUMP_FLAGS_CONTINUOUS : 0;
	IMG_BOOL	bInCaptureRange;
	IMG_BOOL	bPdumpEnabled;

	PDumpIsCaptureFrameKM(&bInCaptureRange);
	bPdumpEnabled = (bInCaptureRange || bPDumpContinuous);

	/* Dump the CCB data */
	if (bPdumpEnabled)
	{
		DevmemPDumpLoadMem(psClientCCB->psClientCCBMemDesc,
						   psClientCCB->ui32HostWriteOffset,
						   ui32CmdSize,
						   ui32PDumpFlags);
	}
	
	/*
	 *  Check if there have been any fences written that will already be
	 *  satistified by a previously written update in this CCB.
	 */
#if defined REDUNDANT_SYNCS_DEBUG
	{
		IMG_UINT8  *pui8BufferStart = (IMG_PVOID)((IMG_UINTPTR_T)psClientCCB->pui8ClientCCB + psClientCCB->ui32HostWriteOffset);
		IMG_UINT8  *pui8BufferEnd   = (IMG_PVOID)((IMG_UINTPTR_T)psClientCCB->pui8ClientCCB + psClientCCB->ui32HostWriteOffset + ui32CmdSize);

		/* Walk through the commands in this section of CCB being released... */
		while (pui8BufferStart < pui8BufferEnd)
		{
			RGXFWIF_CCB_CMD_HEADER  *psCmdHeader = (RGXFWIF_CCB_CMD_HEADER *) pui8BufferStart;

			if (psCmdHeader->eCmdType == RGXFWIF_CCB_CMD_TYPE_UPDATE)
			{
				/* If an UPDATE then record the value incase a later fence depends on it. */
				IMG_UINT32  ui32NumUpdates = psCmdHeader->ui32CmdSize / sizeof(RGXFWIF_UFO);
				IMG_UINT32  i;

				for (i = 0;  i < ui32NumUpdates;  i++)
				{
					RGXFWIF_UFO  *psUFOPtr = ((RGXFWIF_UFO*)(pui8BufferStart + sizeof(RGXFWIF_CCB_CMD_HEADER))) + i;
					
					psClientCCB->asFenceUpdateList[psClientCCB->ui32UpdateWriteIndex++] = *psUFOPtr;
					psClientCCB->ui32UpdateWriteIndex &= (RGX_CCCB_FENCE_UPDATE_LIST_SIZE-1);
				}
			}
			else if (psCmdHeader->eCmdType == RGXFWIF_CCB_CMD_TYPE_FENCE)
			{
				IMG_UINT32  ui32NumFences = psCmdHeader->ui32CmdSize / sizeof(RGXFWIF_UFO);
				IMG_UINT32  i;
				
				for (i = 0;  i < ui32NumFences;  i++)
				{
					RGXFWIF_UFO  *psUFOPtr = ((RGXFWIF_UFO*)(pui8BufferStart + sizeof(RGXFWIF_CCB_CMD_HEADER))) + i;
					IMG_UINT32  ui32UpdateIndex;

					/* Check recently queued updates to see if this fence will be satisfied by the time it is checked. */
					for (ui32UpdateIndex = 0;  ui32UpdateIndex < RGX_CCCB_FENCE_UPDATE_LIST_SIZE;  ui32UpdateIndex++)
					{
						RGXFWIF_UFO  *psUpdatePtr = &psClientCCB->asFenceUpdateList[ui32UpdateIndex];
							
						if (psUFOPtr->puiAddrUFO.ui32Addr == psUpdatePtr->puiAddrUFO.ui32Addr  &&
							psUFOPtr->ui32Value == psUpdatePtr->ui32Value)
						{
							PVR_DPF((PVR_DBG_WARNING, "Redundant fence found in cCCB(%p) - 0x%x -> 0x%x",
									psClientCCB, psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value));
							//psUFOPtr->puiAddrUFO.ui32Addr = 0;
							break;
						}
					}
				}
			}
			else if (psCmdHeader->eCmdType == RGXFWIF_CCB_CMD_TYPE_FENCE_PR)
			{
				IMG_UINT32  ui32NumFences = psCmdHeader->ui32CmdSize / sizeof(RGXFWIF_UFO);
				IMG_UINT32  i;
				
				for (i = 0;  i < ui32NumFences;  i++)
				{
					RGXFWIF_UFO  *psUFOPtr = ((RGXFWIF_UFO*)(pui8BufferStart + sizeof(RGXFWIF_CCB_CMD_HEADER))) + i;
					IMG_UINT32  ui32UpdateIndex;
							
					/* Check recently queued updates to see if this fence will be satisfied by the time it is checked. */
					for (ui32UpdateIndex = 0;  ui32UpdateIndex < RGX_CCCB_FENCE_UPDATE_LIST_SIZE;  ui32UpdateIndex++)
					{
						RGXFWIF_UFO  *psUpdatePtr = &psClientCCB->asFenceUpdateList[ui32UpdateIndex];
						
						/*
						 *  The PR-fence will be met if the update value is >= the required fence value. E.g.
						 *  the difference between the update value and fence value is positive.
						 */
						if (psUFOPtr->puiAddrUFO.ui32Addr == psUpdatePtr->puiAddrUFO.ui32Addr  &&
							((psUpdatePtr->ui32Value - psUFOPtr->ui32Value) & (1U << 31)) == 0)
						{
							PVR_DPF((PVR_DBG_WARNING, "Redundant PR fence found in cCCB(%p) - 0x%x -> 0x%x",
									psClientCCB, psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value));
							//psUFOPtr->puiAddrUFO.ui32Addr = 0;
							break;
						}
					}
				}
			}

			/* Move to the next command in this section of CCB being released... */
			pui8BufferStart += sizeof(RGXFWIF_CCB_CMD_HEADER) + psCmdHeader->ui32CmdSize;
		}
	}
#endif /* REDUNDANT_SYNCS_DEBUG */

	/*
	 * Update the CCB write offset.
	 */
	UPDATE_CCB_OFFSET(psClientCCB->ui32HostWriteOffset,
					  ui32CmdSize,
					  psClientCCB->ui32Size);
	psClientCCB->ui32ByteCount += ui32CmdSize;

	/*
		PDumpSetFrame will detect as we Transition out of capture range for
		frame based data but if we are PDumping continuous data then we
		need to inform the PDump layer ourselves
	*/
	if (bPDumpContinuous && !bInCaptureRange)
	{
		PVRSRV_ERROR eError;

		/* Only Transitioning into capture range can cause an error */
		eError = PDumpTransition(psClientCCB->psPDumpConnectionData, IMG_FALSE, IMG_TRUE);
		PVR_ASSERT(eError == PVRSRV_OK);
	}

	if (bPdumpEnabled)
	{
		/* Update the PDump write offset to show we PDumped this command */
		psClientCCB->ui32LastPDumpWriteOffset = psClientCCB->ui32HostWriteOffset;
	}

#if defined(NO_HARDWARE)
	/*
		The firmware is not running, it cannot update these; we do here instead.
	*/
	psClientCCB->psClientCCBCtrl->ui32ReadOffset = psClientCCB->ui32HostWriteOffset;
	psClientCCB->psClientCCBCtrl->ui32DepOffset = psClientCCB->ui32HostWriteOffset;
#endif
}

IMG_UINT32 RGXGetHostWriteOffsetCCB(RGX_CLIENT_CCB *psClientCCB)
{
	return psClientCCB->ui32HostWriteOffset;
}

#define SUPPORT_DUMP_CLIENT_CCB_COMMANDS_DBG_LEVEL PVR_DBG_ERROR
#define CHECK_COMMAND(cmd, fenceupdate) \
				case RGXFWIF_CCB_CMD_TYPE_##cmd: \
						PVR_DPF((SUPPORT_DUMP_CLIENT_CCB_COMMANDS_DBG_LEVEL, #cmd " command (%d bytes)", psHeader->ui32CmdSize)); \
						bFenceUpdate = fenceupdate; \
						break

static IMG_VOID _RGXClientCCBDumpCommands(RGX_CLIENT_CCB *psClientCCB,
										  IMG_UINT32 ui32Offset,
										  IMG_UINT32 ui32ByteCount)
{
#if defined(SUPPORT_DUMP_CLIENT_CCB_COMMANDS)
	IMG_UINT8 *pui8Ptr = psClientCCB->pui8ClientCCB + ui32Offset;
	IMG_UINT32 ui32ConsumeSize = ui32ByteCount;

	while (ui32ConsumeSize)
	{
		RGXFWIF_CCB_CMD_HEADER *psHeader = (RGXFWIF_CCB_CMD_HEADER *) pui8Ptr;
		IMG_BOOL bFenceUpdate = IMG_FALSE;

		PVR_DPF((SUPPORT_DUMP_CLIENT_CCB_COMMANDS_DBG_LEVEL, "@offset 0x%08x", pui8Ptr - psClientCCB->pui8ClientCCB));
		switch(psHeader->eCmdType)
		{
			CHECK_COMMAND(TA, IMG_FALSE);
			CHECK_COMMAND(3D, IMG_FALSE);
			CHECK_COMMAND(CDM, IMG_FALSE);
			CHECK_COMMAND(TQ_3D, IMG_FALSE);
			CHECK_COMMAND(TQ_2D, IMG_FALSE);
			CHECK_COMMAND(3D_PR, IMG_FALSE);
			CHECK_COMMAND(NULL, IMG_FALSE);
			CHECK_COMMAND(SHG, IMG_FALSE);
			CHECK_COMMAND(RTU, IMG_FALSE);
			CHECK_COMMAND(RTU_FC, IMG_FALSE);
			CHECK_COMMAND(PRE_TIMESTAMP, IMG_FALSE);
			CHECK_COMMAND(POST_TIMESTAMP, IMG_FALSE);
			CHECK_COMMAND(FENCE, IMG_TRUE);
			CHECK_COMMAND(UPDATE, IMG_TRUE);
			CHECK_COMMAND(RMW_UPDATE, IMG_TRUE);
			CHECK_COMMAND(FENCE_PR, IMG_TRUE);
			CHECK_COMMAND(PADDING, IMG_FALSE);
			default:
				PVR_DPF((SUPPORT_DUMP_CLIENT_CCB_COMMANDS_DBG_LEVEL, "Unknown command!"));
				break;
		}
		pui8Ptr += sizeof(*psHeader);
		if (bFenceUpdate)
		{
			IMG_UINT32 j;
			RGXFWIF_UFO *psUFOPtr = (RGXFWIF_UFO *) pui8Ptr;
			for (j=0;j<psHeader->ui32CmdSize/sizeof(RGXFWIF_UFO);j++)
			{
				PVR_DPF((SUPPORT_DUMP_CLIENT_CCB_COMMANDS_DBG_LEVEL, "Addr = 0x%08x, value = 0x%08x",
							psUFOPtr[j].puiAddrUFO.ui32Addr, psUFOPtr[j].ui32Value));
			}
		}
		else
		{
			IMG_UINT32 *pui32Ptr = (IMG_UINT32 *) pui8Ptr;
			IMG_UINT32 ui32Remain = psHeader->ui32CmdSize/sizeof(IMG_UINT32);
			while(ui32Remain)
			{
				if (ui32Remain >= 4)
				{
					PVR_DPF((SUPPORT_DUMP_CLIENT_CCB_COMMANDS_DBG_LEVEL, "0x%08x 0x%08x 0x%08x 0x%08x",
							pui32Ptr[0], pui32Ptr[1], pui32Ptr[2], pui32Ptr[3]));
					pui32Ptr += 4;
					ui32Remain -= 4;
				}
				if (ui32Remain == 3)
				{
					PVR_DPF((SUPPORT_DUMP_CLIENT_CCB_COMMANDS_DBG_LEVEL, "0x%08x 0x%08x 0x%08x",
							pui32Ptr[0], pui32Ptr[1], pui32Ptr[2]));
					pui32Ptr += 3;
					ui32Remain -= 3;
				}
				if (ui32Remain == 2)
				{
					PVR_DPF((SUPPORT_DUMP_CLIENT_CCB_COMMANDS_DBG_LEVEL, "0x%08x 0x%08x",
							pui32Ptr[0], pui32Ptr[1]));
					pui32Ptr += 2;
					ui32Remain -= 2;
				}
				if (ui32Remain == 1)
				{
					PVR_DPF((SUPPORT_DUMP_CLIENT_CCB_COMMANDS_DBG_LEVEL, "0x%08x",
							pui32Ptr[0]));
					pui32Ptr += 1;
					ui32Remain -= 1;
				}
			}
		}
		pui8Ptr += psHeader->ui32CmdSize;
		ui32ConsumeSize -= sizeof(*psHeader) + psHeader->ui32CmdSize;
	}
#else
	PVR_UNREFERENCED_PARAMETER(psClientCCB);
	PVR_UNREFERENCED_PARAMETER(ui32Offset);
	PVR_UNREFERENCED_PARAMETER(ui32ByteCount);
#endif
}

/*
	Workout how much space this command will require
*/
PVRSRV_ERROR RGXCmdHelperInitCmdCCB(RGX_CLIENT_CCB 			*psClientCCB,
                                    IMG_UINT32				ui32ClientFenceCount,
                                    PRGXFWIF_UFO_ADDR		*pauiFenceUFOAddress,
                                    IMG_UINT32				*paui32FenceValue,
                                    IMG_UINT32				ui32ClientUpdateCount,
                                    PRGXFWIF_UFO_ADDR		*pauiUpdateUFOAddress,
                                    IMG_UINT32				*paui32UpdateValue,
                                    IMG_UINT32				ui32ServerSyncCount,
                                    IMG_UINT32				*paui32ServerSyncFlags,
                                    SERVER_SYNC_PRIMITIVE	**papsServerSyncs,
                                    IMG_UINT32				ui32CmdSize,
                                    IMG_PBYTE				pui8DMCmd,
                                    PRGXFWIF_TIMESTAMP_ADDR * ppPreAddr,
                                    PRGXFWIF_TIMESTAMP_ADDR * ppPostAddr,
                                    PRGXFWIF_UFO_ADDR       * ppRMWUFOAddr,
                                    RGXFWIF_CCB_CMD_TYPE	eType,
                                    IMG_BOOL				bPDumpContinuous,
                                    IMG_CHAR			 	*pszCommandName,
                                    RGX_CCB_CMD_HELPER_DATA	*psCmdHelperData)
{
	IMG_UINT32 ui32FenceCount;
	IMG_UINT32 ui32UpdateCount;
	IMG_UINT32 i;

	/* Save the data we require in the submit call */
	psCmdHelperData->psClientCCB = psClientCCB;
	psCmdHelperData->bPDumpContinuous = bPDumpContinuous;
	psCmdHelperData->pszCommandName = pszCommandName;

	/* Client sync data */
	psCmdHelperData->ui32ClientFenceCount = ui32ClientFenceCount;
	psCmdHelperData->pauiFenceUFOAddress = pauiFenceUFOAddress;
	psCmdHelperData->paui32FenceValue = paui32FenceValue;
	psCmdHelperData->ui32ClientUpdateCount = ui32ClientUpdateCount;
	psCmdHelperData->pauiUpdateUFOAddress = pauiUpdateUFOAddress;
	psCmdHelperData->paui32UpdateValue = paui32UpdateValue;

	/* Server sync data */
	psCmdHelperData->ui32ServerSyncCount = ui32ServerSyncCount;
	psCmdHelperData->paui32ServerSyncFlags = paui32ServerSyncFlags;
	psCmdHelperData->papsServerSyncs = papsServerSyncs;

	/* Command data */
	psCmdHelperData->ui32CmdSize = ui32CmdSize;
	psCmdHelperData->pui8DMCmd = pui8DMCmd;
	psCmdHelperData->eType = eType;

	PDUMPCOMMENTWITHFLAGS((bPDumpContinuous) ? PDUMP_FLAGS_CONTINUOUS : 0,
			"%s Command Server Init on FWCtx %08x", pszCommandName,
			FWCommonContextGetFWAddress(psClientCCB->psServerCommonContext).ui32Addr);

	/* Init the generated data members */
	psCmdHelperData->ui32ServerFenceCount = 0;
	psCmdHelperData->ui32ServerUpdateCount = 0;
	psCmdHelperData->ui32PreTimeStampCmdSize = 0;
	psCmdHelperData->ui32PostTimeStampCmdSize = 0;
	psCmdHelperData->ui32RMWUFOCmdSize = 0;


	if (ppPreAddr && (ppPreAddr->ui32Addr != 0))
	{

		psCmdHelperData->pPreTimestampAddr       = * ppPreAddr;
		psCmdHelperData->ui32PreTimeStampCmdSize = sizeof(RGXFWIF_CCB_CMD_HEADER)
			+ ((sizeof(RGXFWIF_DEV_VIRTADDR) + RGXFWIF_FWALLOC_ALIGN - 1) & ~(RGXFWIF_FWALLOC_ALIGN  - 1));
	}

	if (ppPostAddr && (ppPostAddr->ui32Addr != 0))
	{
		psCmdHelperData->pPostTimestampAddr       = * ppPostAddr;
		psCmdHelperData->ui32PostTimeStampCmdSize = sizeof(RGXFWIF_CCB_CMD_HEADER)
			+ ((sizeof(RGXFWIF_DEV_VIRTADDR) + RGXFWIF_FWALLOC_ALIGN - 1) & ~(RGXFWIF_FWALLOC_ALIGN  - 1));
	}

	if (ppRMWUFOAddr && (ppRMWUFOAddr->ui32Addr != 0))
	{
		psCmdHelperData->pRMWUFOAddr       = * ppRMWUFOAddr;
		psCmdHelperData->ui32RMWUFOCmdSize = sizeof(RGXFWIF_CCB_CMD_HEADER) + sizeof(RGXFWIF_UFO);
	}


	/* Workout how many fence and update's this command will have */
	for (i = 0; i < ui32ServerSyncCount; i++)
	{
		if (paui32ServerSyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK)
		{
			/* Server syncs must fence */
			psCmdHelperData->ui32ServerFenceCount++;
		}
		if (paui32ServerSyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE)
		{
			psCmdHelperData->ui32ServerUpdateCount++;
		}
	}

	/* Total fence command size (header plus command data) */
	ui32FenceCount = ui32ClientFenceCount + psCmdHelperData->ui32ServerFenceCount;
	if (ui32FenceCount)
	{
		psCmdHelperData->ui32FenceCmdSize = RGX_CCB_FWALLOC_ALIGN((ui32FenceCount * sizeof(RGXFWIF_UFO)) +
																  sizeof(RGXFWIF_CCB_CMD_HEADER));
	}
	else
	{
		psCmdHelperData->ui32FenceCmdSize = 0;
	}

	/* Total DM command size (header plus command data) */
	psCmdHelperData->ui32DMCmdSize = RGX_CCB_FWALLOC_ALIGN(ui32CmdSize +
														   sizeof(RGXFWIF_CCB_CMD_HEADER));

	/* Total update command size (header plus command data) */
	ui32UpdateCount = ui32ClientUpdateCount + psCmdHelperData->ui32ServerUpdateCount;
	if (ui32UpdateCount)
	{
		psCmdHelperData->ui32UpdateCmdSize = RGX_CCB_FWALLOC_ALIGN((ui32UpdateCount * sizeof(RGXFWIF_UFO)) +
																   sizeof(RGXFWIF_CCB_CMD_HEADER));
	}
	else
	{
		psCmdHelperData->ui32UpdateCmdSize = 0;
	}

	return PVRSRV_OK;
}


/*
	Reserve space in the CCB and fill in the command and client sync data
*/
PVRSRV_ERROR RGXCmdHelperAcquireCmdCCB(IMG_UINT32 ui32CmdCount,
									   RGX_CCB_CMD_HELPER_DATA *asCmdHelperData,
									   IMG_BOOL *pbKickRequired)
{
	IMG_UINT32 ui32BeforeWOff = asCmdHelperData[0].psClientCCB->ui32HostWriteOffset;
	IMG_UINT32 ui32AllocSize = 0;
	IMG_UINT32 i;
	IMG_UINT8 *pui8StartPtr;
	PVRSRV_ERROR eError;

	*pbKickRequired = IMG_FALSE;

	/*
		Workout how much space we need for all the command(s)
	*/
	ui32AllocSize = RGXCmdHelperGetCommandSize(ui32CmdCount, asCmdHelperData);


	for (i = 0; i < ui32CmdCount; i++)
	{
		if (asCmdHelperData[0].bPDumpContinuous != asCmdHelperData[i].bPDumpContinuous)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: PDump continuous is not consistent (%s != %s) for command %d",
					 __FUNCTION__,
					 asCmdHelperData[0].bPDumpContinuous?"IMG_TRUE":"IMG_FALSE",
					 asCmdHelperData[i].bPDumpContinuous?"IMG_TRUE":"IMG_FALSE",
					 ui32CmdCount));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	/*
		Acquire space in the CCB for all the command(s).
	*/
	eError = RGXAcquireCCB(asCmdHelperData[0].psClientCCB,
						   ui32AllocSize,
						   (IMG_PVOID *)&pui8StartPtr,
						   asCmdHelperData[0].bPDumpContinuous);	
	if (eError != PVRSRV_OK)
	{
		/* Failed so bail out and allow the client side to retry */
		if (asCmdHelperData[0].psClientCCB->ui32HostWriteOffset != ui32BeforeWOff)
		{
			*pbKickRequired = IMG_TRUE;
		}
		return eError;
	}



	/*
		For each command fill in the fence, DM, and update command

		Note:
		We only fill in the client fences here, the server fences (and updates)
		will be filled in together at the end. This is because we might fail the
		kernel CCB alloc and would then have to rollback the server syncs if
		we took the operation here
	*/
	for (i = 0; i < ui32CmdCount; i++)
	{
		RGX_CCB_CMD_HELPER_DATA *psCmdHelperData = & asCmdHelperData[i];
		IMG_UINT8 *pui8CmdPtr;
		IMG_UINT8 *pui8ServerFenceStart = 0;
		IMG_UINT8 *pui8ServerUpdateStart = 0;
#if defined(PDUMP)
		IMG_UINT32 ui32CtxAddr = FWCommonContextGetFWAddress(asCmdHelperData->psClientCCB->psServerCommonContext).ui32Addr;
		IMG_UINT32 ui32CcbWoff = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(asCmdHelperData->psClientCCB->psServerCommonContext));
#endif

		if (psCmdHelperData->ui32ClientFenceCount+psCmdHelperData->ui32ClientUpdateCount != 0)
		{
			PDUMPCOMMENT("Start of %s client syncs for cmd[%d] on FWCtx %08x Woff 0x%x bytes",
					psCmdHelperData->psClientCCB->szName, i, ui32CtxAddr, ui32CcbWoff);
		}



		/*
			Create the fence command.
		*/
		if (psCmdHelperData->ui32FenceCmdSize)
		{
			RGXFWIF_CCB_CMD_HEADER *psHeader;
			IMG_UINT k;

			/* Fences are at the start of the command */
			pui8CmdPtr = pui8StartPtr;

			psHeader = (RGXFWIF_CCB_CMD_HEADER *) pui8CmdPtr;
			psHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_FENCE;
			psHeader->ui32CmdSize = psCmdHelperData->ui32FenceCmdSize - sizeof(RGXFWIF_CCB_CMD_HEADER);
			pui8CmdPtr += sizeof(RGXFWIF_CCB_CMD_HEADER);

			/* Fill in the client fences */
			for (k = 0; k < psCmdHelperData->ui32ClientFenceCount; k++)
			{
				RGXFWIF_UFO *psUFOPtr = (RGXFWIF_UFO *) pui8CmdPtr;
	
				psUFOPtr->puiAddrUFO = psCmdHelperData->pauiFenceUFOAddress[k];
				psUFOPtr->ui32Value = psCmdHelperData->paui32FenceValue[k];
				pui8CmdPtr += sizeof(RGXFWIF_UFO);

#if defined SYNC_COMMAND_DEBUG
				PVR_DPF((PVR_DBG_ERROR, "%s client sync fence - 0x%x -> 0x%x",
						psCmdHelperData->psClientCCB->szName, psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value));
#endif
				PDUMPCOMMENT(".. %s client sync fence - 0x%x -> 0x%x",
						psCmdHelperData->psClientCCB->szName, psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value);


			}
			pui8ServerFenceStart = pui8CmdPtr;
		}

		/* jump over the Server fences */
		pui8CmdPtr = pui8StartPtr + psCmdHelperData->ui32FenceCmdSize;


		/*
		  Create the pre DM timestamp commands. Pre and Post timestamp commands are supposed to
		  sandwich the DM cmd. The padding code with the CCB wrap upsets the FW if we don't have
		  the task type bit cleared for POST_TIMESTAMPs. That's why we have 2 different cmd types.
		*/
		if (psCmdHelperData->ui32PreTimeStampCmdSize != 0)
		{
			RGXWriteTimestampCommand(& pui8CmdPtr,
			                         RGXFWIF_CCB_CMD_TYPE_PRE_TIMESTAMP,
			                         psCmdHelperData->pPreTimestampAddr);
		}
	
		/*
			Create the DM command
		*/
		if (psCmdHelperData->ui32DMCmdSize)
		{
			RGXFWIF_CCB_CMD_HEADER *psHeader;

			psHeader = (RGXFWIF_CCB_CMD_HEADER *) pui8CmdPtr;
			psHeader->eCmdType = psCmdHelperData->eType;
			psHeader->ui32CmdSize = psCmdHelperData->ui32DMCmdSize - sizeof(RGXFWIF_CCB_CMD_HEADER);



			pui8CmdPtr += sizeof(RGXFWIF_CCB_CMD_HEADER);


			OSMemCopy(pui8CmdPtr, psCmdHelperData->pui8DMCmd, psCmdHelperData->ui32CmdSize);
			pui8CmdPtr += psCmdHelperData->ui32CmdSize;
		}



		if (psCmdHelperData->ui32PostTimeStampCmdSize != 0)
		{
			RGXWriteTimestampCommand(& pui8CmdPtr,
			                         RGXFWIF_CCB_CMD_TYPE_POST_TIMESTAMP,
			                         psCmdHelperData->pPostTimestampAddr);
		}


		if (psCmdHelperData->ui32RMWUFOCmdSize != 0)
		{
			RGXFWIF_CCB_CMD_HEADER * psHeader;
			RGXFWIF_UFO            * psUFO;

			psHeader = (RGXFWIF_CCB_CMD_HEADER *) pui8CmdPtr;
			psHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_RMW_UPDATE;
			psHeader->ui32CmdSize = psCmdHelperData->ui32RMWUFOCmdSize - sizeof(RGXFWIF_CCB_CMD_HEADER);
			pui8CmdPtr += sizeof(RGXFWIF_CCB_CMD_HEADER);

			psUFO = (RGXFWIF_UFO *) pui8CmdPtr;
			psUFO->puiAddrUFO = psCmdHelperData->pRMWUFOAddr;
			
			pui8CmdPtr += sizeof(RGXFWIF_UFO);
		}
	

		/*
			Create the update command.
			
			Note:
			We only fill in the client updates here, the server updates (and fences)
			will be filled in together at the end
		*/
		if (psCmdHelperData->ui32UpdateCmdSize)
		{
			RGXFWIF_CCB_CMD_HEADER *psHeader;
			IMG_UINT k;

			psHeader = (RGXFWIF_CCB_CMD_HEADER *) pui8CmdPtr;
			psHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_UPDATE;
			psHeader->ui32CmdSize = psCmdHelperData->ui32UpdateCmdSize - sizeof(RGXFWIF_CCB_CMD_HEADER);
			pui8CmdPtr += sizeof(RGXFWIF_CCB_CMD_HEADER);

			/* Fill in the client updates */
			for (k = 0; k < psCmdHelperData->ui32ClientUpdateCount; k++)
			{
				RGXFWIF_UFO *psUFOPtr = (RGXFWIF_UFO *) pui8CmdPtr;
	
				psUFOPtr->puiAddrUFO = psCmdHelperData->pauiUpdateUFOAddress[k];
				psUFOPtr->ui32Value = psCmdHelperData->paui32UpdateValue[k];
				pui8CmdPtr += sizeof(RGXFWIF_UFO);

#if defined SYNC_COMMAND_DEBUG
				PVR_DPF((PVR_DBG_ERROR, "%s client sync update - 0x%x -> 0x%x",
						psCmdHelperData->psClientCCB->szName, psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value));
#endif
				PDUMPCOMMENT(".. %s client sync update - 0x%x -> 0x%x",
						psCmdHelperData->psClientCCB->szName, psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value);

			}
			pui8ServerUpdateStart = pui8CmdPtr;
		}
	
		/* Save the server sync fence & update offsets for submit time */
		psCmdHelperData->pui8ServerFenceStart  = pui8ServerFenceStart;
		psCmdHelperData->pui8ServerUpdateStart = pui8ServerUpdateStart;
	
		/* Save start for sanity checking at submit time */
		psCmdHelperData->pui8StartPtr = pui8StartPtr;

		/* Set the start pointer for the next iteration around the loop */
		pui8StartPtr +=
			psCmdHelperData->ui32FenceCmdSize         +
			psCmdHelperData->ui32PreTimeStampCmdSize  +
			psCmdHelperData->ui32DMCmdSize            +
			psCmdHelperData->ui32PostTimeStampCmdSize +
			psCmdHelperData->ui32RMWUFOCmdSize        + 
			psCmdHelperData->ui32UpdateCmdSize;

		if (psCmdHelperData->ui32ClientFenceCount+psCmdHelperData->ui32ClientUpdateCount != 0)
		{
			PDUMPCOMMENT("End of %s client syncs for cmd[%d] on FWCtx %08x Woff 0x%x bytes",
					psCmdHelperData->psClientCCB->szName, i, ui32CtxAddr, ui32CcbWoff);
		}
		else
		{
			PDUMPCOMMENT("No %s client syncs for cmd[%d] on FWCtx %08x Woff 0x%x bytes",
					psCmdHelperData->psClientCCB->szName, i, ui32CtxAddr, ui32CcbWoff);
		}
	}

	*pbKickRequired = IMG_TRUE;
	return PVRSRV_OK;
}

/*
	Fill in the server syncs data and release the CCB space
*/
IMG_VOID RGXCmdHelperReleaseCmdCCB(IMG_UINT32 ui32CmdCount,
								   RGX_CCB_CMD_HELPER_DATA *asCmdHelperData,
								   const IMG_CHAR *pcszDMName,
								   IMG_UINT32 ui32CtxAddr)
{
	IMG_UINT32 ui32AllocSize = 0;
	IMG_UINT32 i;

	/*
		Workout how much space we need for all the command(s)
	*/
	ui32AllocSize = RGXCmdHelperGetCommandSize(ui32CmdCount, asCmdHelperData);

   /*
		For each command fill in the server sync info
	*/
	for (i=0;i<ui32CmdCount;i++)
	{
		RGX_CCB_CMD_HELPER_DATA *psCmdHelperData = &asCmdHelperData[i];
		IMG_UINT8 *pui8ServerFenceStart = psCmdHelperData->pui8ServerFenceStart;
		IMG_UINT8 *pui8ServerUpdateStart = psCmdHelperData->pui8ServerUpdateStart;
		IMG_UINT32 j;

		/* Now fill in the server fence and updates together */
		for (j = 0; j < psCmdHelperData->ui32ServerSyncCount; j++)
		{
			RGXFWIF_UFO *psUFOPtr;
			IMG_UINT32 ui32UpdateValue;
			IMG_UINT32 ui32FenceValue;
			PVRSRV_ERROR eError;
			IMG_BOOL bFence = ((psCmdHelperData->paui32ServerSyncFlags[j] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK)!=0)?IMG_TRUE:IMG_FALSE;
			IMG_BOOL bUpdate = ((psCmdHelperData->paui32ServerSyncFlags[j] & PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE)!=0)?IMG_TRUE:IMG_FALSE;

			eError = PVRSRVServerSyncQueueHWOpKM(psCmdHelperData->papsServerSyncs[j],
												 bUpdate,
												 &ui32FenceValue,
												 &ui32UpdateValue);
			/* This function can't fail */
			PVR_ASSERT(eError == PVRSRV_OK);
	
			/*
				As server syncs always fence (we have a check in RGXCmcdHelperInitCmdCCB
				which ensures the client is playing ball) the filling in of the fence
				is unconditional.
			*/
			if (bFence)
			{
				PVR_ASSERT(pui8ServerFenceStart != 0);

				psUFOPtr = (RGXFWIF_UFO *) pui8ServerFenceStart;
				psUFOPtr->puiAddrUFO.ui32Addr = ServerSyncGetFWAddr(psCmdHelperData->papsServerSyncs[j]);
				psUFOPtr->ui32Value = ui32FenceValue;
				pui8ServerFenceStart += sizeof(RGXFWIF_UFO);

#if defined(LINUX)
				trace_rogue_fence_checks(pcszDMName,
										 ui32CtxAddr,
										 psCmdHelperData->psClientCCB->ui32HostWriteOffset + ui32AllocSize,
										 1,
										 &psUFOPtr->puiAddrUFO,
										 &psUFOPtr->ui32Value);
#endif
			}
	
			/* If there is an update then fill that in as well */
			if (bUpdate)
			{
				PVR_ASSERT(pui8ServerUpdateStart != 0);

				psUFOPtr = (RGXFWIF_UFO *) pui8ServerUpdateStart;
				psUFOPtr->puiAddrUFO.ui32Addr = ServerSyncGetFWAddr(psCmdHelperData->papsServerSyncs[j]);
				psUFOPtr->ui32Value = ui32UpdateValue;
				pui8ServerUpdateStart += sizeof(RGXFWIF_UFO);

#if defined(LINUX)
				trace_rogue_fence_updates(pcszDMName,
										  ui32CtxAddr,
										  psCmdHelperData->psClientCCB->ui32HostWriteOffset + ui32AllocSize,
										  1,
										  &psUFOPtr->puiAddrUFO,
										  &psUFOPtr->ui32Value);
#endif

#if defined(NO_HARDWARE)
				/*
					There is no FW so the host has to do any Sync updates
					(client sync updates are done in the client
				*/
				PVRSRVServerSyncPrimSetKM(psCmdHelperData->papsServerSyncs[j], ui32UpdateValue);
#endif
			}
		}

#if defined(LINUX)
		trace_rogue_fence_checks(pcszDMName,
								 ui32CtxAddr,
								 psCmdHelperData->psClientCCB->ui32HostWriteOffset + ui32AllocSize,
								 psCmdHelperData->ui32ClientFenceCount,
								 psCmdHelperData->pauiFenceUFOAddress,
								 psCmdHelperData->paui32FenceValue);
		trace_rogue_fence_updates(pcszDMName,
								  ui32CtxAddr,
								  psCmdHelperData->psClientCCB->ui32HostWriteOffset + ui32AllocSize,
								  psCmdHelperData->ui32ClientUpdateCount,
								  psCmdHelperData->pauiUpdateUFOAddress,
								  psCmdHelperData->paui32UpdateValue);
#endif

		if (psCmdHelperData->ui32ServerSyncCount)
		{
			/*
				Do some sanity checks to ensure we did the point math right
			*/
			if (pui8ServerFenceStart != 0)
			{
				PVR_ASSERT(pui8ServerFenceStart ==
						   (psCmdHelperData->pui8StartPtr +
						   psCmdHelperData->ui32FenceCmdSize));
			}

			if (pui8ServerUpdateStart != 0)
			{
				PVR_ASSERT(pui8ServerUpdateStart ==
				           psCmdHelperData->pui8StartPtr             +
				           psCmdHelperData->ui32FenceCmdSize         +
				           psCmdHelperData->ui32PreTimeStampCmdSize  +
				           psCmdHelperData->ui32DMCmdSize            +
				           psCmdHelperData->ui32RMWUFOCmdSize        +
				           psCmdHelperData->ui32PostTimeStampCmdSize +
				           psCmdHelperData->ui32UpdateCmdSize);
			}
		}
	
		/*
			All the commands have been filled in so release the CCB space.
			The FW still won't run this command until we kick it
		*/
		PDUMPCOMMENTWITHFLAGS((psCmdHelperData->bPDumpContinuous) ? PDUMP_FLAGS_CONTINUOUS : 0,
				"%s Command Server Release on FWCtx %08x",
				psCmdHelperData->pszCommandName, ui32CtxAddr);
	}

	_RGXClientCCBDumpCommands(asCmdHelperData[0].psClientCCB,
							  asCmdHelperData[0].psClientCCB->ui32HostWriteOffset,
							  ui32AllocSize);

	RGXReleaseCCB(asCmdHelperData[0].psClientCCB, 
				  ui32AllocSize,
				  asCmdHelperData[0].bPDumpContinuous);
}


IMG_UINT32 RGXCmdHelperGetCommandSize(IMG_UINT32              ui32CmdCount,
                                      RGX_CCB_CMD_HELPER_DATA *asCmdHelperData)
{
	IMG_UINT32 ui32AllocSize = 0;
	IMG_UINT32 i;

	/*
		Workout how much space we need for all the command(s)
	*/
	for (i = 0; i < ui32CmdCount; i++)
	{
		ui32AllocSize +=
			asCmdHelperData[i].ui32FenceCmdSize         +
			asCmdHelperData[i].ui32DMCmdSize            +
			asCmdHelperData[i].ui32UpdateCmdSize        +
			asCmdHelperData[i].ui32PreTimeStampCmdSize  +
			asCmdHelperData[i].ui32PostTimeStampCmdSize +
			asCmdHelperData[i].ui32RMWUFOCmdSize;
	}

	return ui32AllocSize;
}


static IMG_PCCHAR _CCBCmdTypename(RGXFWIF_CCB_CMD_TYPE cmdType)
{
	static const IMG_CHAR* aCCBCmdName[18] = { "TA", "3D", "CDM", "TQ_3D", "TQ_2D",
	                                           "3D_PR", "NULL", "SHG", "RTU", "RTU_FC",
	                                           "PRE_TIMESTAMP",
	                                           "FENCE", "UPDATE", "RMW_UPDATE",
	                                           "FENCE_PR", "PRIORITY",
	                                           "POST_TIMESTAMP", "PADDING"};
	IMG_UINT32	cmdStrIdx = 17;

	PVR_ASSERT( (cmdType == RGXFWIF_CCB_CMD_TYPE_TA)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_3D)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_CDM)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_TQ_3D)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_TQ_2D)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_3D_PR)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_NULL)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_SHG)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_RTU)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_RTU_FC)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_PRE_TIMESTAMP)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_POST_TIMESTAMP)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_FENCE)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_UPDATE)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_RMW_UPDATE)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_FENCE_PR)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_PRIORITY)
	            || (cmdType == RGXFWIF_CCB_CMD_TYPE_PADDING));

	if( cmdType !=  RGXFWIF_CCB_CMD_TYPE_PADDING)
	{
		cmdStrIdx = ((IMG_UINT32)cmdType & ~RGX_CCB_TYPE_TASK) - (RGXFWIF_CCB_CMD_TYPE_TA & ~RGX_CCB_TYPE_TASK);
	}

	return aCCBCmdName[cmdStrIdx];
}

PVRSRV_ERROR CheckForStalledCCB(RGX_CLIENT_CCB  *psCurrentClientCCB)
{
	volatile RGXFWIF_CCCB_CTL	*psClientCCBCtrl = psCurrentClientCCB->psClientCCBCtrl;
	IMG_UINT32 					ui32SampledRdOff = psClientCCBCtrl->ui32ReadOffset;
	IMG_UINT32 					ui32SampledWrOff = psCurrentClientCCB->ui32HostWriteOffset;
	PVRSRV_ERROR				eError = PVRSRV_OK;

	if (ui32SampledRdOff > psClientCCBCtrl->ui32WrapMask  ||
		ui32SampledWrOff > psClientCCBCtrl->ui32WrapMask)
	{
		PVR_DPF((PVR_DBG_WARNING, "CheckForStalledCCB: CCCB has invalid offset (ROFF=%d WOFF=%d)",
				ui32SampledRdOff, ui32SampledWrOff));
		return  PVRSRV_ERROR_INVALID_OFFSET;
	}

	if (ui32SampledRdOff != ui32SampledWrOff &&
				psCurrentClientCCB->ui32LastROff != psCurrentClientCCB->ui32LastWOff &&
				ui32SampledRdOff == psCurrentClientCCB->ui32LastROff &&
				(psCurrentClientCCB->ui32ByteCount - psCurrentClientCCB->ui32LastByteCount) < psCurrentClientCCB->ui32Size)
	{
		//RGXFWIF_DEV_VIRTADDR v = {0};
		//DumpStalledCCBCommand(v,psCurrentClientCCB,IMG_NULL);

		/* Don't log this by default unless debugging since a higher up
		 * function will log the stalled condition. Helps avoid double
		 *  messages in the log.
		 */
		PVR_DPF((PVR_DBG_MESSAGE, "CheckForStalledCCB: CCCB has not progressed (ROFF=%d WOFF=%d)",
				ui32SampledRdOff, ui32SampledWrOff));
		eError =  PVRSRV_ERROR_CCCB_STALLED;
	}

	psCurrentClientCCB->ui32LastROff = ui32SampledRdOff;
	psCurrentClientCCB->ui32LastWOff = ui32SampledWrOff;
	psCurrentClientCCB->ui32LastByteCount = psCurrentClientCCB->ui32ByteCount;

	return eError;
}

IMG_VOID DumpStalledCCBCommand(PRGXFWIF_FWCOMMONCONTEXT sFWCommonContext,
							   RGX_CLIENT_CCB  *psCurrentClientCCB,
							   DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	volatile RGXFWIF_CCCB_CTL	  *psClientCCBCtrl = psCurrentClientCCB->psClientCCBCtrl;
	IMG_UINT8					  *pui8ClientCCBBuff = psCurrentClientCCB->pui8ClientCCB;
	volatile IMG_UINT8		   	  *pui8Ptr;
	IMG_UINT32 					  ui32SampledRdOff = psClientCCBCtrl->ui32ReadOffset;
	IMG_UINT32 					  ui32SampledDepOff = psClientCCBCtrl->ui32DepOffset;
	IMG_UINT32 					  ui32SampledWrOff = psCurrentClientCCB->ui32HostWriteOffset;

	pui8Ptr = pui8ClientCCBBuff + ui32SampledRdOff;

	if ((ui32SampledRdOff == ui32SampledDepOff) &&
		(ui32SampledRdOff != ui32SampledWrOff))
	{
		volatile RGXFWIF_CCB_CMD_HEADER *psCommandHeader = (RGXFWIF_CCB_CMD_HEADER *)(pui8ClientCCBBuff + ui32SampledRdOff);
		RGXFWIF_CCB_CMD_TYPE 	eCommandType = psCommandHeader->eCmdType;
		volatile IMG_UINT8				*pui8Ptr = (IMG_UINT8 *)psCommandHeader;

		/* CCB is stalled on a fence... */
		if ((eCommandType == RGXFWIF_CCB_CMD_TYPE_FENCE) || (eCommandType == RGXFWIF_CCB_CMD_TYPE_FENCE_PR))
		{
			RGXFWIF_UFO *psUFOPtr = (RGXFWIF_UFO *)(pui8Ptr + sizeof(*psCommandHeader));
			IMG_UINT32 jj;

			/* Display details of the fence object on which the context is pending */
			PVR_DUMPDEBUG_LOG(("FWCtx 0x%08X @ %d (%s) pending on %s:",
							   sFWCommonContext.ui32Addr,
							   ui32SampledRdOff,
							   (IMG_PCHAR)&psCurrentClientCCB->szName,
							   _CCBCmdTypename(eCommandType)));
			for (jj=0; jj<psCommandHeader->ui32CmdSize/sizeof(RGXFWIF_UFO); jj++)
			{
				PVR_DUMPDEBUG_LOG(("  Addr:0x%08x  Value=0x%08x",psUFOPtr[jj].puiAddrUFO.ui32Addr, psUFOPtr[jj].ui32Value));
			}

			/* Advance psCommandHeader past the FENCE to the next command header (this will be the TA/3D command that is fenced) */
			pui8Ptr = (IMG_UINT8 *)psUFOPtr + psCommandHeader->ui32CmdSize;
			psCommandHeader = (RGXFWIF_CCB_CMD_HEADER *)pui8Ptr;
			if( (IMG_UINTPTR_T)psCommandHeader != ((IMG_UINTPTR_T)pui8ClientCCBBuff + ui32SampledWrOff))
			{
				PVR_DUMPDEBUG_LOG((" FWCtx 0x%08X fenced command is of type %s",sFWCommonContext.ui32Addr, _CCBCmdTypename(psCommandHeader->eCmdType)));
				/* Advance psCommandHeader past the TA/3D to the next command header (this will possibly be an UPDATE) */
				pui8Ptr += sizeof(*psCommandHeader) + psCommandHeader->ui32CmdSize;
				psCommandHeader = (RGXFWIF_CCB_CMD_HEADER *)pui8Ptr;
				/* If the next command is an update, display details of that so we can see what would then become unblocked */
				if( (IMG_UINTPTR_T)psCommandHeader != ((IMG_UINTPTR_T)pui8ClientCCBBuff + ui32SampledWrOff))
				{
					eCommandType = psCommandHeader->eCmdType;

					if (eCommandType == RGXFWIF_CCB_CMD_TYPE_UPDATE)
					{
						psUFOPtr = (RGXFWIF_UFO *)((IMG_UINT8 *)psCommandHeader + sizeof(*psCommandHeader));
						PVR_DUMPDEBUG_LOG((" preventing %s:",_CCBCmdTypename(eCommandType)));
						for (jj=0; jj<psCommandHeader->ui32CmdSize/sizeof(RGXFWIF_UFO); jj++)
						{
							PVR_DUMPDEBUG_LOG(("  Addr:0x%08x  Value=0x%08x",psUFOPtr[jj].puiAddrUFO.ui32Addr, psUFOPtr[jj].ui32Value));
						}
					}
				}
				else
				{
					PVR_DUMPDEBUG_LOG((" FWCtx 0x%08X has no further commands",sFWCommonContext.ui32Addr));
				}
			}
			else
			{
				PVR_DUMPDEBUG_LOG((" FWCtx 0x%08X has no further commands",sFWCommonContext.ui32Addr));
			}
		}
	}
}

/******************************************************************************
 End of file (rgxccb.c)
******************************************************************************/
