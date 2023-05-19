/*************************************************************************/ /*!
@File
@Title          RGX CCB routines
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
#include "dllist.h"
#if defined(__linux__)
#include "trace_events.h"
#endif
#include "sync_checkpoint_external.h"
#include "sync_checkpoint.h"
#include "rgxutils.h"
#include "info_page.h"
#include "rgxtimerquery.h"

#if defined(PVRSRV_FORCE_FLUSH_CCCB_ON_KICK)
#include "cache_km.h"
#endif

/*
 *  Uncomment PVRSRV_ENABLE_CCCB_UTILISATION_INFO define for verbose
 *  info and statistics regarding CCB usage.
 */
//#define PVRSRV_ENABLE_CCCB_UTILISATION_INFO

/* Default threshold (as a percentage) for the PVRSRV_ENABLE_CCCB_UTILISATION_INFO feature. */
#define PVRSRV_ENABLE_CCCB_UTILISATION_INFO_THRESHOLD  (90)

/*
 * Defines the number of fence updates to record so that future fences in the
 * CCB. Can be checked to see if they are already known to be satisfied.
 */
#define RGX_CCCB_FENCE_UPDATE_LIST_SIZE  (32)

#define RGX_UFO_PTR_ADDR(ufoptr) \
	(((ufoptr)->puiAddrUFO.ui32Addr) & 0xFFFFFFFC)

#define GET_CCB_SPACE(WOff, ROff, CCBSize) \
	((((ROff) - (WOff)) + ((CCBSize) - 1)) & ((CCBSize) - 1))

#define UPDATE_CCB_OFFSET(Off, PacketSize, CCBSize) \
	(Off) = (((Off) + (PacketSize)) & ((CCBSize) - 1))

#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)

#define PVRSRV_CLIENT_CCCB_UTILISATION_WARNING_THRESHOLD 0x1
#define PVRSRV_CLIENT_CCCB_UTILISATION_WARNING_ACQUIRE_FAILED 0x2
#define PVRSRV_CLIENT_CCCB_UTILISATION_WARNING_FULL_CCB 0x4

typedef struct _RGX_CLIENT_CCB_UTILISATION_
{
	/* the threshold in bytes.
	 * when the CCB utilisation hits the threshold then we will print
	 * a warning message.
	 */
	IMG_UINT32 ui32ThresholdBytes;
	/* Maximum cCCB usage at some point in time */
	IMG_UINT32 ui32HighWaterMark;
	/* keep track of the warnings already printed.
	 * bit mask of PVRSRV_CLIENT_CCCB_UTILISATION_WARNING_xyz
	 */
	IMG_UINT32 ui32Warnings;
	/* Keep track how many times CCB was full.
	 * Counters are reset after every grow.
	 */
	IMG_UINT32 ui32CCBFull;
	IMG_UINT32 ui32CCBAcquired;
} RGX_CLIENT_CCB_UTILISATION;

#endif /* PVRSRV_ENABLE_CCCB_UTILISATION_INFO */

struct _RGX_CLIENT_CCB_ {
	volatile RGXFWIF_CCCB_CTL	*psClientCCBCtrl;				/*!< CPU mapping of the CCB control structure used by the fw */
	void						*pvClientCCB;					/*!< CPU mapping of the CCB */
	DEVMEM_MEMDESC				*psClientCCBMemDesc;			/*!< MemDesc for the CCB */
	DEVMEM_MEMDESC				*psClientCCBCtrlMemDesc;		/*!< MemDesc for the CCB control */
	IMG_UINT32					ui32HostWriteOffset;			/*!< CCB write offset from the driver side */
	IMG_UINT32					ui32LastPDumpWriteOffset;		/*!< CCB write offset from the last time we submitted a command in capture range */
	IMG_UINT32					ui32FinishedPDumpWriteOffset;	/*!< Trails LastPDumpWriteOffset for last finished command, used for HW CB driven DMs */
	IMG_UINT32					ui32LastROff;					/*!< Last CCB Read offset to help detect any CCB wedge */
	IMG_UINT32					ui32LastWOff;					/*!< Last CCB Write offset to help detect any CCB wedge */
	IMG_UINT32					ui32ByteCount;					/*!< Count of the number of bytes written to CCCB */
	IMG_UINT32					ui32LastByteCount;				/*!< Last value of ui32ByteCount to help detect any CCB wedge */
	IMG_UINT32					ui32Size;						/*!< Size of the CCB */
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	POS_LOCK					hCCBGrowLock;					/*!< Prevents CCB Grow while DumpCCB() is called and vice versa */
	IMG_UINT32					ui32VirtualAllocSize;			/*!< Virtual size of the CCB */
	IMG_UINT32					ui32ChunkSize;					/*!< CCB Sparse allocation chunk size */
	IMG_PUINT32					pui32MappingTable;				/*!< Mapping table for sparse allocation of the CCB */
#endif
	DLLIST_NODE					sNode;							/*!< Node used to store this CCB on the per connection list */
	PDUMP_CONNECTION_DATA		*psPDumpConnectionData;			/*!< Pointer to the per connection data in which we reside */
	void						*hTransition;					/*!< Handle for Transition callback */
	IMG_CHAR					szName[MAX_CLIENT_CCB_NAME];	/*!< Name of this client CCB */
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;			/*!< Parent server common context that this CCB belongs to */
#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)
	RGX_CCB_REQUESTOR_TYPE		eRGXCCBRequestor;
	RGX_CLIENT_CCB_UTILISATION	sUtilisation;					/*!< CCB utilisation data */
#endif
#if defined(DEBUG)
	IMG_UINT32					ui32UpdateEntries;				/*!< Number of Fence Updates in asFenceUpdateList */
	RGXFWIF_UFO					asFenceUpdateList[RGX_CCCB_FENCE_UPDATE_LIST_SIZE]; /*!< List of recent updates written in this CCB */
#endif
	IMG_UINT32					ui32CCBFlags;                   /*!< Bitmask for various flags relating to CCB. Bit defines in rgxccb.h */
};

/* Forms a table, with array of strings for each requestor type (listed in RGX_CCB_REQUESTORS X macro), to be used for
   DevMemAllocation comments and PDump comments. Each tuple in the table consists of 3 strings:
	{ "FwClientCCB:" <requestor_name>, "FwClientCCBControl:" <requestor_name>, <requestor_name> },
   The first string being used as comment when allocating ClientCCB for the given requestor, the second for CCBControl
   structure, and the 3rd one for use in PDUMP comments. The number of tuples in the table must adhere to the following
   build assert. */
const IMG_CHAR *const aszCCBRequestors[][3] =
{
#define REQUESTOR_STRING(prefix,req) #prefix ":" #req
#define FORM_REQUESTOR_TUPLE(req) { REQUESTOR_STRING(FwClientCCB,req), REQUESTOR_STRING(FwClientCCBControl,req), #req },
	RGX_CCB_REQUESTORS(FORM_REQUESTOR_TUPLE)
#undef FORM_REQUESTOR_TUPLE
};

PVRSRV_ERROR RGXCCBPDumpDrainCCB(RGX_CLIENT_CCB *psClientCCB,
						IMG_UINT32 ui32PDumpFlags)
{

	IMG_UINT32 ui32PollOffset;
#if defined(PDUMP)
	PVRSRV_RGXDEV_INFO *psDevInfo = FWCommonContextGetRGXDevInfo(psClientCCB->psServerCommonContext);
#endif

	if (BIT_ISSET(psClientCCB->ui32CCBFlags, CCB_FLAGS_CCB_STATE_OPEN))
	{
		/* Draining CCB on a command that hasn't finished, and FW isn't expected
		 * to have updated Roff up to Woff. Only drain to the first
		 * finished command prior to this. The Roff for this
		 * is stored in ui32FinishedPDumpWriteOffset.
		 */
		ui32PollOffset = psClientCCB->ui32FinishedPDumpWriteOffset;

		PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode,
							  ui32PDumpFlags,
							  "cCCB(%s@%p): Draining open CCB rgxfw_roff < woff (%d)",
							  psClientCCB->szName,
							  psClientCCB,
							  ui32PollOffset);
	}
	else
	{
		/* Command to a finished CCB stream and FW is drained to empty
		 * out remaining commands until R==W.
		 */
		ui32PollOffset = psClientCCB->ui32LastPDumpWriteOffset;

		PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode,
							  ui32PDumpFlags,
							  "cCCB(%s@%p): Draining CCB rgxfw_roff == woff (%d)",
							  psClientCCB->szName,
							  psClientCCB,
							  ui32PollOffset);
	}

	return DevmemPDumpDevmemPol32(psClientCCB->psClientCCBCtrlMemDesc,
									offsetof(RGXFWIF_CCCB_CTL, ui32ReadOffset),
									ui32PollOffset,
									0xffffffff,
									PDUMP_POLL_OPERATOR_EQUAL,
									ui32PDumpFlags);
}

/******************************************************************************
 FUNCTION	: RGXCCBPDumpSyncCCB

 PURPOSE	: Synchronise Client CCBs from both live and playback contexts.
               Waits for live-FW to empty live-CCB.
               Waits for sim-FW to empty sim-CCB by adding POL

 PARAMETERS	: psClientCCB		- The client CCB
              ui32PDumpFlags    - PDump flags

 RETURNS	: PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXCCBPDumpSyncCCB(RGX_CLIENT_CCB *psClientCCB, IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	/* Wait for the live FW to catch up/empty CCB. This is done by returning
	 * retry which will get pushed back out to Services client where it
	 * waits on the event object and then resubmits the command.
	 */
	if (psClientCCB->psClientCCBCtrl->ui32ReadOffset != psClientCCB->ui32HostWriteOffset)
	{
		return PVRSRV_ERROR_RETRY;
	}

	/* Wait for the sim FW to catch up/empty sim CCB.
	 * We drain whenever capture range is entered, even if no commands
	 * have been issued on this CCB when out of capture range. We have to
	 * wait for commands that might have been issued in the last capture
	 * range to finish so the connection's sync block snapshot dumped after
	 * all the PDumpTransition callbacks have been execute doesn't clobber
	 * syncs which the sim FW is currently working on.
	 *
	 * Although this is sub-optimal for play-back - while out of capture
	 * range for every continuous operation we synchronise the sim
	 * play-back processing the script and the sim FW, there is no easy
	 * solution. Not all modules that work with syncs register a
	 * PDumpTransition callback and thus we have no way of knowing if we
	 * can skip this sim CCB drain and sync block dump or not.
	 */

	eError = RGXCCBPDumpDrainCCB(psClientCCB, ui32PDumpFlags);
	PVR_LOG_IF_ERROR(eError, "RGXCCBPDumpDrainCCB");
	PVR_ASSERT(eError == PVRSRV_OK);

	/* Live CCB and simulation CCB now empty, FW idle on CCB in both
	 * contexts.
	 */
	return PVRSRV_OK;
}

/******************************************************************************
 FUNCTION	: RGXCCBPDumpFastForwardCCB

 PURPOSE	: Fast-forward sim-CCB and live-CCB offsets to live app-thread
              values.
               This helps to skip any commands submitted when out of capture
               range and start with first command in capture range in both
               live and playback contexts. In case of Block mode, this helps
               to playback any intermediate PDump block directly after first
               block.


 PARAMETERS	: psClientCCB		- The client CCB
			  ui32PDumpFlags    - PDump flags

 RETURNS	: void
******************************************************************************/
static void RGXCCBPDumpFastForwardCCB(RGX_CLIENT_CCB *psClientCCB, IMG_UINT32 ui32PDumpFlags)
{
	volatile RGXFWIF_CCCB_CTL *psCCBCtl = psClientCCB->psClientCCBCtrl;
#if defined(PDUMP)
	PVRSRV_RGXDEV_INFO *psDevInfo = FWCommonContextGetRGXDevInfo(psClientCCB->psServerCommonContext);
#endif

	/* Make sure that we have synced live-FW and live-App threads */
	PVR_ASSERT(psCCBCtl->ui32ReadOffset == psClientCCB->ui32HostWriteOffset);

	psCCBCtl->ui32ReadOffset = psClientCCB->ui32HostWriteOffset;
	psCCBCtl->ui32DepOffset = psClientCCB->ui32HostWriteOffset;
	psCCBCtl->ui32WriteOffset = psClientCCB->ui32HostWriteOffset;
#if defined(SUPPORT_AGP)
	psCCBCtl->ui32ReadOffset2 = psClientCCB->ui32HostWriteOffset;
#if defined(SUPPORT_AGP4)
	psCCBCtl->ui32ReadOffset3 = psClientCCB->ui32HostWriteOffset;
	psCCBCtl->ui32ReadOffset4 = psClientCCB->ui32HostWriteOffset;
#endif
#endif

	PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode,
			ui32PDumpFlags,
			"cCCB(%s@%p): Fast-forward from %d to %d",
			psClientCCB->szName,
			psClientCCB,
			psClientCCB->ui32LastPDumpWriteOffset,
			psClientCCB->ui32HostWriteOffset);

	DevmemPDumpLoadMem(psClientCCB->psClientCCBCtrlMemDesc,
			0,
			sizeof(RGXFWIF_CCCB_CTL),
			ui32PDumpFlags);

	/* Although we've entered capture range for this process connection
	 * we might not do any work on this CCB so update the
	 * ui32LastPDumpWriteOffset to reflect where we got to for next
	 * time so we start the drain from where we got to last time.
	 */
	psClientCCB->ui32LastPDumpWriteOffset = psClientCCB->ui32HostWriteOffset;

}

static PVRSRV_ERROR _RGXCCBPDumpTransition(void *pvData, void *pvDevice, PDUMP_TRANSITION_EVENT eEvent, IMG_UINT32 ui32PDumpFlags)
{
	RGX_CLIENT_CCB *psClientCCB = (RGX_CLIENT_CCB *) pvData;
#if defined(PDUMP)
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *) pvDevice;
#endif
	PVRSRV_ERROR eError;

	/* Block mode:
	 * Here is block structure at transition (ui32BlockLength=N frames):
	 *
	 * ...
	 * ...
	 * PDUMP_BLOCK_START_0x0000000x{
	 *   <Fast-forward sim-CCCB>
	 *   <Re-dump SyncBlocks>
	 *   ...
	 *   ...
	 *   ... (N frames data)
	 *   ...
	 *   ...
	 *   <(1) Drain sim-KCCB>                     ''|
	 *   <(2) Sync live and sim CCCB>               |
	 * }PDUMP_BLOCK_END_0x0000000x                  | <- BlockTransition Steps
	 *   <(3) Split MAIN and BLOCK stream script>   |
	 * PDUMP_BLOCK_START_0x0000000y{                |
	 *   <(4) Fast-forward sim-CCCB>                |
	 *   <(5) Re-dump SyncBlocks>                 ,,|
	 *   ...
	 *   ...
	 *   ... (N frames data)
	 *   ...
	 *   ...
	 *   <Drain sim-KCCB>
	 *   <Sync live and sim CCCB>
	 * }PDUMP_BLOCK_END_0x0000000y
	 * ...
	 * ...
	 *
	 * Steps (3) and (5) are done in pdump_server.c
	 * */
	switch (eEvent)
	{
		case PDUMP_TRANSITION_EVENT_RANGE_ENTERED:
			{
				/* We're about to transition into capture range and we've submitted
				 * new commands since the last time we entered capture range so drain
				 * the live CCB and simulation (sim) CCB as required, i.e. leave CCB
				 * idle in both live and sim contexts.
				 * This requires the host driver to ensure the live FW & the sim FW
				 * have both emptied out the remaining commands until R==W (CCB empty).
				 */

				eError = RGXCCBPDumpSyncCCB(psClientCCB, ui32PDumpFlags);
				PVR_RETURN_IF_ERROR(eError);

				if (psClientCCB->ui32LastPDumpWriteOffset != psClientCCB->ui32HostWriteOffset)
				{
					/* If new commands have been written when out of capture range in
					 * the live CCB then we need to fast forward the sim CCBCtl
					 * offsets past uncaptured commands. This is done by PDUMPing
					 * the CCBCtl memory to align sim values with the live CCBCtl
					 * values. Both live and sim FWs can start with the 1st command
					 * which is in the new capture range.
					 */
					RGXCCBPDumpFastForwardCCB(psClientCCB, ui32PDumpFlags);
				}
				break;
			}
		case PDUMP_TRANSITION_EVENT_RANGE_EXITED:
			{
				/* Nothing to do */
				break;
			}
		case PDUMP_TRANSITION_EVENT_BLOCK_FINISHED:
			{
				/* (1) Drain KCCB from current block before starting new:
				 *
				 * At playback, this will ensure that sim-FW drains all commands in KCCB
				 * belongs to current block before 'jumping' to any future commands (from
				 * next block). This will synchronise script-thread and sim-FW thread KCCBs
				 * at end of each pdump block.
				 *
				 * This will additionally force redump of KCCBCtl structure at start of next/new block.
				 * */

#if defined(PDUMP)
				eError = RGXPdumpDrainKCCB(psDevInfo, psDevInfo->psKernelCCBCtl->ui32WriteOffset);
				PVR_LOG_RETURN_IF_ERROR(eError, "RGXPdumpDrainKCCB");
#endif

				/* (2) Synchronise Client CCBs from live and playback contexts before starting new block:
				 *
				 * This operation will,
				 * a. Force synchronisation between app-thread and live-FW thread (i.e. Wait
				 *    for live-FW to empty live Client CCB).
				 *
				 * b. Next, it will dump poll command to drain Client CCB at end of every
				 *    pdump block. At playback time this will synchronise sim-FW and
				 *    script-thread Client CCBs at end of each block.
				 *
				 * This is to ensure that all commands in CCB from current block are processed
				 * before moving on to future commands.
				 * */

				eError = RGXCCBPDumpSyncCCB(psClientCCB, ui32PDumpFlags);
				PVR_RETURN_IF_ERROR(eError);
				break;
			}
		case PDUMP_TRANSITION_EVENT_BLOCK_STARTED:
			{
				/* (4) Fast-forward CCB write offsets to current live values:
				 *
				 * We have already synchronised live-FW and app-thread above at end of each
				 * block (in Step 2a above), now fast-forward Client CCBCtl write offsets to that of
				 * current app-thread values at start of every block. This will allow us to
				 * skip any intermediate pdump blocks and start with last (or any next) block
				 * immediately after first pdump block.
				 * */

				RGXCCBPDumpFastForwardCCB(psClientCCB, ui32PDumpFlags);
				break;
			}
		case PDUMP_TRANSITION_EVENT_NONE:
			/* Invalid event for transition */
		default:
			{
				/* Unknown Transition event */
				return PVRSRV_ERROR_INVALID_PARAMS;
			}
	}
	return PVRSRV_OK;
}

#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)

static INLINE void _RGXInitCCBUtilisation(RGX_CLIENT_CCB *psClientCCB)
{
	psClientCCB->sUtilisation.ui32HighWaterMark = 0; /* initialize ui32HighWaterMark level to zero */
	psClientCCB->sUtilisation.ui32ThresholdBytes = (psClientCCB->ui32Size *
							PVRSRV_ENABLE_CCCB_UTILISATION_INFO_THRESHOLD)	/ 100;
	psClientCCB->sUtilisation.ui32Warnings = 0;
	psClientCCB->sUtilisation.ui32CCBAcquired = 0;
	psClientCCB->sUtilisation.ui32CCBFull = 0;
}

static INLINE void _RGXCCBUtilisationEvent(RGX_CLIENT_CCB *psClientCCB,
						IMG_UINT32 ui32WarningType,
						IMG_UINT32 ui32CmdSize)
{
	/* in VERBOSE mode we will print a message for each different
	 * event type as they happen.
	 */
#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)
	if (!(psClientCCB->sUtilisation.ui32Warnings & ui32WarningType))
	{
		if (ui32WarningType == PVRSRV_CLIENT_CCCB_UTILISATION_WARNING_ACQUIRE_FAILED)
		{
			PVR_LOG(("Failed to acquire CCB space for %u byte command:", ui32CmdSize));
		}

		PVR_LOG(("%s: Client CCB (%s) watermark (%u) hit %d%% of its allocation size (%u)",
									__func__,
									psClientCCB->szName,
									psClientCCB->sUtilisation.ui32HighWaterMark,
									psClientCCB->sUtilisation.ui32HighWaterMark * 100 / psClientCCB->ui32Size,
									psClientCCB->ui32Size));

		/* record that we have issued a warning of this type */
		psClientCCB->sUtilisation.ui32Warnings |= ui32WarningType;
	}
#else
	PVR_UNREFERENCED_PARAMETER(psClientCCB);
	PVR_UNREFERENCED_PARAMETER(ui32WarningType);
	PVR_UNREFERENCED_PARAMETER(ui32CmdSize);
#endif
}

/* Check the current CCB utilisation. Print a one-time warning message if it is above the
 * specified threshold
 */
static INLINE void _RGXCheckCCBUtilisation(RGX_CLIENT_CCB *psClientCCB)
{
	/* Print a warning message if the cCCB watermark is above the threshold value */
	if (psClientCCB->sUtilisation.ui32HighWaterMark >= psClientCCB->sUtilisation.ui32ThresholdBytes)
	{
		_RGXCCBUtilisationEvent(psClientCCB,
					PVRSRV_CLIENT_CCCB_UTILISATION_WARNING_THRESHOLD,
					0);
	}
}

/* Update the cCCB high watermark level if necessary */
static void _RGXUpdateCCBUtilisation(RGX_CLIENT_CCB *psClientCCB)
{
	IMG_UINT32 ui32FreeSpace, ui32MemCurrentUsage;

	ui32FreeSpace = GET_CCB_SPACE(psClientCCB->ui32HostWriteOffset,
									  psClientCCB->psClientCCBCtrl->ui32ReadOffset,
									  psClientCCB->ui32Size);
	ui32MemCurrentUsage = psClientCCB->ui32Size - ui32FreeSpace;

	if (ui32MemCurrentUsage > psClientCCB->sUtilisation.ui32HighWaterMark)
	{
		psClientCCB->sUtilisation.ui32HighWaterMark = ui32MemCurrentUsage;

		/* The high water mark has increased. Check if it is above the
		 * threshold so we can print a warning if necessary.
		 */
		_RGXCheckCCBUtilisation(psClientCCB);
	}
}

#endif /* PVRSRV_ENABLE_CCCB_UTILISATION_INFO */

PVRSRV_ERROR RGXCreateCCB(PVRSRV_RGXDEV_INFO	*psDevInfo,
						  IMG_UINT32			ui32CCBSizeLog2,
						  IMG_UINT32			ui32CCBMaxSizeLog2,
						  IMG_UINT32			ui32ContextFlags,
						  CONNECTION_DATA		*psConnectionData,
						  RGX_CCB_REQUESTOR_TYPE		eRGXCCBRequestor,
						  RGX_SERVER_COMMON_CONTEXT *psServerCommonContext,
						  RGX_CLIENT_CCB		**ppsClientCCB,
						  DEVMEM_MEMDESC		**ppsClientCCBMemDesc,
						  DEVMEM_MEMDESC		**ppsClientCCBCtrlMemDesc)
{
	PVRSRV_ERROR	eError = PVRSRV_OK;
	PVRSRV_MEMALLOCFLAGS_T	uiClientCCBMemAllocFlags, uiClientCCBCtlMemAllocFlags;
	IMG_UINT32		ui32FWMainLog2PageSize = DevmemGetHeapLog2PageSize(psDevInfo->psFirmwareMainHeap);
	IMG_UINT32		ui32ChunkSize = (1U << ui32FWMainLog2PageSize);
	IMG_UINT32		ui32AllocSize = MAX((1U << ui32CCBSizeLog2), ui32ChunkSize);
	IMG_UINT32		ui32MinAllocSize = MAX((1U << MIN_SAFE_CCB_SIZE_LOG2), ui32ChunkSize);
	RGX_CLIENT_CCB	*psClientCCB;
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	IMG_UINT32		ui32NumChunks = ui32AllocSize / ui32ChunkSize;
	IMG_UINT32		ui32VirtualAllocSize = (1U << ui32CCBMaxSizeLog2);
	IMG_UINT32		ui32NumVirtChunks = ui32VirtualAllocSize / ui32ChunkSize;
	IMG_UINT32		i;

	/* For the allocation request to be valid, at least one page is required.
	 * This is relevant on systems where the page size is greater than the client CCB size. */
	ui32NumVirtChunks = MAX(1, ui32NumVirtChunks);
	PVR_ASSERT((ui32ChunkSize >= (1U << PAGE_SHIFT)));
#else
	PVR_UNREFERENCED_PARAMETER(ui32CCBMaxSizeLog2);
#endif /* defined(PVRSRV_ENABLE_CCCB_GROW) */

	/* All client CCBs should be at-least of the "minimum" size and not to exceed "maximum" */
	if ((ui32CCBSizeLog2 < MIN_SAFE_CCB_SIZE_LOG2) ||
		(ui32CCBSizeLog2 > MAX_SAFE_CCB_SIZE_LOG2))
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: %s CCB size is invalid (%d). Should be from %d to %d",
		         __func__,
		         aszCCBRequestors[eRGXCCBRequestor][REQ_PDUMP_COMMENT],
		         ui32CCBSizeLog2, MIN_SAFE_CCB_SIZE_LOG2, MAX_SAFE_CCB_SIZE_LOG2));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	if ((ui32CCBMaxSizeLog2 < ui32CCBSizeLog2) ||
		(ui32CCBMaxSizeLog2 > MAX_SAFE_CCB_SIZE_LOG2))
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: %s CCB maximum size is invalid (%d). Should be from %d to %d",
		         __func__,
		         aszCCBRequestors[eRGXCCBRequestor][REQ_PDUMP_COMMENT],
		         ui32CCBMaxSizeLog2, ui32CCBSizeLog2, MAX_SAFE_CCB_SIZE_LOG2));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
#endif

	psClientCCB = OSAllocMem(sizeof(*psClientCCB));
	if (psClientCCB == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}
	psClientCCB->psServerCommonContext = psServerCommonContext;

#if defined(PVRSRV_ENABLE_CCCB_GROW)
	psClientCCB->ui32VirtualAllocSize = 0;
	psClientCCB->pui32MappingTable = NULL;
	psClientCCB->ui32ChunkSize = ui32ChunkSize;
#endif

	uiClientCCBMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
								PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
								PVRSRV_MEMALLOCFLAG_GPU_READABLE |
								PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
								PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC |
								PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
								PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
								PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN);

	uiClientCCBCtlMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
								PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
								PVRSRV_MEMALLOCFLAG_GPU_READABLE |
								PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
								PVRSRV_MEMALLOCFLAG_GPU_UNCACHED |
								PVRSRV_MEMALLOCFLAG_CPU_UNCACHED |
								PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
								PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
								PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN);

	/* If connection data indicates Sync Lockup Recovery (SLR) should be disabled,
	 * or if the caller has set ui32ContextFlags to disable SLR for this context,
	 * indicate this in psClientCCB->ui32CCBFlags.
	 */
	if ((psConnectionData->ui32ClientFlags & SRV_FLAGS_CLIENT_SLR_DISABLED) ||
	    (ui32ContextFlags & RGX_CONTEXT_FLAG_DISABLESLR))
	{
		BIT_SET(psClientCCB->ui32CCBFlags, CCB_FLAGS_SLR_DISABLED);
	}

	PDUMPCOMMENT(psDevInfo->psDeviceNode, "Allocate RGXFW cCCB");
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	if (BITMASK_HAS(psDevInfo->ui32DeviceFlags, RGXKM_DEVICE_STATE_CCB_GROW_EN))
	{
		PHYS_HEAP *psPhysHeap = psDevInfo->psDeviceNode->apsPhysHeap[PVRSRV_PHYS_HEAP_FW_MAIN];
		PHYS_HEAP_POLICY uiHeapPolicy = PhysHeapGetPolicy(psPhysHeap);

		psClientCCB->ui32VirtualAllocSize = ui32VirtualAllocSize;

		if (uiHeapPolicy != PHYS_HEAP_POLICY_ALLOC_ALLOW_NONCONTIG)
		{
			psClientCCB->pui32MappingTable = NULL;
			/*
			 * On LMA sparse memory can't be mapped to kernel without support for non physically
			 * sparse allocations.
			 * To work around this whole ccb memory is allocated at once as contiguous.
			 */
			eError = DevmemFwAllocate(psDevInfo,
									ui32VirtualAllocSize,
									uiClientCCBMemAllocFlags,
									aszCCBRequestors[eRGXCCBRequestor][REQ_RGX_FW_CLIENT_CCB_STRING],
									&psClientCCB->psClientCCBMemDesc);
		}
		else
		{
			/*
			 * Growing CCB is doubling the size. Last grow would require only ui32NumVirtChunks/2 new chunks
			 * because another ui32NumVirtChunks/2 is already allocated.
			 * Sometimes initial chunk count would be higher (when CCB size is equal to CCB maximum size) so MAX is needed.
			 */
			psClientCCB->pui32MappingTable = OSAllocMem(MAX(ui32NumChunks, ui32NumVirtChunks/2) * sizeof(IMG_UINT32));
			if (psClientCCB->pui32MappingTable == NULL)
			{
				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto fail_alloc_mtable;
			}

			for (i = 0; i < ui32NumChunks; i++)
			{
				psClientCCB->pui32MappingTable[i] = i;
			}

			eError = DevmemFwAllocateSparse(psDevInfo,
											ui32VirtualAllocSize,
											ui32NumChunks,
											ui32NumVirtChunks,
											psClientCCB->pui32MappingTable,
											uiClientCCBMemAllocFlags,
											aszCCBRequestors[eRGXCCBRequestor][REQ_RGX_FW_CLIENT_CCB_STRING],
											&psClientCCB->psClientCCBMemDesc);
		}
	}

	if (eError != PVRSRV_OK)
	{
		OSFreeMem(psClientCCB->pui32MappingTable);
		psClientCCB->pui32MappingTable = NULL;
		psClientCCB->ui32VirtualAllocSize = 0;
	}

	if (!BITMASK_HAS(psDevInfo->ui32DeviceFlags, RGXKM_DEVICE_STATE_CCB_GROW_EN) ||
	    (eError != PVRSRV_OK))
#endif /* defined(PVRSRV_ENABLE_CCCB_GROW) */
	{
		/* Allocate ui32AllocSize, or the next best POT allocation */
		do
		{
			eError = DevmemFwAllocate(psDevInfo,
									ui32AllocSize,
									uiClientCCBMemAllocFlags,
									aszCCBRequestors[eRGXCCBRequestor][REQ_RGX_FW_CLIENT_CCB_STRING],
									&psClientCCB->psClientCCBMemDesc);
			if (eError != PVRSRV_OK)
			{
				/* Failed to allocate - ensure CCB grow is disabled from
				 * now on for this device.
				 */
				BITMASK_UNSET(psDevInfo->ui32DeviceFlags, RGXKM_DEVICE_STATE_CCB_GROW_EN);

				/* Failed to allocate, try next POT down */
				ui32AllocSize >>= 1;
			}
		} while ((eError != PVRSRV_OK) && (ui32AllocSize > ui32MinAllocSize));
	}

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed to allocate RGX client CCB (%s)",
		         __func__,
		         PVRSRVGetErrorString(eError)));
		goto fail_alloc_ccb;
	}

	OSSNPrintf(psClientCCB->szName, MAX_CLIENT_CCB_NAME, "%s-P%lu-T%lu-%s",
									aszCCBRequestors[eRGXCCBRequestor][REQ_PDUMP_COMMENT],
									(unsigned long) OSGetCurrentClientProcessIDKM(),
									(unsigned long) OSGetCurrentClientThreadIDKM(),
									OSGetCurrentClientProcessNameKM());

	if (ui32AllocSize < (1U << ui32CCBSizeLog2))
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Unable to allocate %d bytes for RGX client CCB (%s) but allocated %d bytes",
		         __func__,
		         (1U << ui32CCBSizeLog2),
		         psClientCCB->szName,
		         ui32AllocSize));
	}

	eError = DevmemAcquireCpuVirtAddr(psClientCCB->psClientCCBMemDesc,
									  &psClientCCB->pvClientCCB);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed to map RGX client CCB (%s)",
		         __func__,
		         PVRSRVGetErrorString(eError)));
		goto fail_map_ccb;
	}

	PDUMPCOMMENT(psDevInfo->psDeviceNode, "Allocate RGXFW cCCB control");
	eError = DevmemFwAllocate(psDevInfo,
										sizeof(RGXFWIF_CCCB_CTL),
										uiClientCCBCtlMemAllocFlags,
										aszCCBRequestors[eRGXCCBRequestor][REQ_RGX_FW_CLIENT_CCB_CONTROL_STRING],
										&psClientCCB->psClientCCBCtrlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed to allocate RGX client CCB control (%s)",
		         __func__,
		         PVRSRVGetErrorString(eError)));
		goto fail_alloc_ccbctrl;
	}


	eError = DevmemAcquireCpuVirtAddr(psClientCCB->psClientCCBCtrlMemDesc,
									  (void **) &psClientCCB->psClientCCBCtrl);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed to map RGX client CCB control (%s)",
		         __func__,
		         PVRSRVGetErrorString(eError)));
		goto fail_map_ccbctrl;
	}

	/* psClientCCBCtrlMemDesc was zero alloc'd so no need to initialise offsets. */
	psClientCCB->psClientCCBCtrl->ui32WrapMask = ui32AllocSize - 1;

	PDUMPCOMMENT(psDevInfo->psDeviceNode, "cCCB control");
	DevmemPDumpLoadMem(psClientCCB->psClientCCBCtrlMemDesc,
					   0,
					   sizeof(RGXFWIF_CCCB_CTL),
					   PDUMP_FLAGS_CONTINUOUS);
	PVR_ASSERT(eError == PVRSRV_OK);

	psClientCCB->ui32HostWriteOffset = 0;
	psClientCCB->ui32LastPDumpWriteOffset = 0;
	psClientCCB->ui32FinishedPDumpWriteOffset = 0;
	psClientCCB->ui32Size = ui32AllocSize;
	psClientCCB->ui32LastROff = ui32AllocSize - 1;
	psClientCCB->ui32ByteCount = 0;
	psClientCCB->ui32LastByteCount = 0;
	BIT_UNSET(psClientCCB->ui32CCBFlags, CCB_FLAGS_CCB_STATE_OPEN);

#if defined(PVRSRV_ENABLE_CCCB_GROW)
	eError = OSLockCreate(&psClientCCB->hCCBGrowLock);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed to create hCCBGrowLock (%s)",
		         __func__,
		         PVRSRVGetErrorString(eError)));
		goto fail_create_ccbgrow_lock;
	}
#endif
#if defined(DEBUG)
	psClientCCB->ui32UpdateEntries = 0;
#endif

#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)
	_RGXInitCCBUtilisation(psClientCCB);
	psClientCCB->eRGXCCBRequestor = eRGXCCBRequestor;
#endif
	eError = PDumpRegisterTransitionCallback(psConnectionData->psPDumpConnectionData,
											  _RGXCCBPDumpTransition,
											  psClientCCB,
											  psDevInfo,
											  &psClientCCB->hTransition);
	if (eError != PVRSRV_OK)
	{
		goto fail_pdumpreg;
	}

	/*
	 * Note:
	 * Save the PDump specific structure, which is ref counted unlike
	 * the connection data, to ensure it's not freed too early
	 */
	psClientCCB->psPDumpConnectionData = psConnectionData->psPDumpConnectionData;
	PDUMPCOMMENT(psDevInfo->psDeviceNode,
				 "New RGXFW cCCB(%s@%p) created",
				 psClientCCB->szName,
				 psClientCCB);

	*ppsClientCCB = psClientCCB;
	*ppsClientCCBMemDesc = psClientCCB->psClientCCBMemDesc;
	*ppsClientCCBCtrlMemDesc = psClientCCB->psClientCCBCtrlMemDesc;
	return PVRSRV_OK;

fail_pdumpreg:
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	OSLockDestroy(psClientCCB->hCCBGrowLock);
fail_create_ccbgrow_lock:
#endif
	DevmemReleaseCpuVirtAddr(psClientCCB->psClientCCBCtrlMemDesc);
fail_map_ccbctrl:
	DevmemFwUnmapAndFree(psDevInfo, psClientCCB->psClientCCBCtrlMemDesc);
fail_alloc_ccbctrl:
	DevmemReleaseCpuVirtAddr(psClientCCB->psClientCCBMemDesc);
fail_map_ccb:
	DevmemFwUnmapAndFree(psDevInfo, psClientCCB->psClientCCBMemDesc);
#if defined(PVRSRV_ENABLE_CCCB_GROW)
fail_alloc_ccb:
	if (psClientCCB->pui32MappingTable)
	{
		OSFreeMem(psClientCCB->pui32MappingTable);
	}
fail_alloc_mtable:
#else
fail_alloc_ccb:
#endif
	OSFreeMem(psClientCCB);
fail_alloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

void RGXDestroyCCB(PVRSRV_RGXDEV_INFO *psDevInfo, RGX_CLIENT_CCB *psClientCCB)
{
#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)
	if (psClientCCB->sUtilisation.ui32CCBFull)
	{
		PVR_LOG(("CCBUtilisationInfo: GPU %s command buffer was full %d times out of %d. "
				"This is not an error but the application may not run optimally.",
				aszCCBRequestors[psClientCCB->eRGXCCBRequestor][REQ_PDUMP_COMMENT],
				psClientCCB->sUtilisation.ui32CCBFull,
				psClientCCB->sUtilisation.ui32CCBAcquired));
	}
#endif
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	OSLockDestroy(psClientCCB->hCCBGrowLock);
#endif
	PDumpUnregisterTransitionCallback(psClientCCB->hTransition);
	DevmemReleaseCpuVirtAddr(psClientCCB->psClientCCBCtrlMemDesc);
	DevmemFwUnmapAndFree(psDevInfo, psClientCCB->psClientCCBCtrlMemDesc);
	DevmemReleaseCpuVirtAddr(psClientCCB->psClientCCBMemDesc);
	DevmemFwUnmapAndFree(psDevInfo, psClientCCB->psClientCCBMemDesc);
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	if (psClientCCB->pui32MappingTable)
	{
		OSFreeMem(psClientCCB->pui32MappingTable);
	}
#endif
	OSFreeMem(psClientCCB);
}

#if defined(PVRSRV_ENABLE_CCCB_GROW)
static PVRSRV_ERROR _RGXCCBMemChangeSparse(RGX_CLIENT_CCB *psClientCCB,
										  IMG_UINT32 ui32AllocPageCount)
{
	PVRSRV_ERROR eError;
	IMG_UINT32	 i;

#ifdef PVRSRV_UNMAP_ON_SPARSE_CHANGE
	DevmemReleaseCpuVirtAddr(psClientCCB->psClientCCBMemDesc);
#endif

	for (i = 0; i < ui32AllocPageCount; i++)
	{
		psClientCCB->pui32MappingTable[i] = ui32AllocPageCount + i;
	}

	/* Double the CCB size (CCB must be POT) by adding ui32AllocPageCount new pages */
	eError = DeviceMemChangeSparse(psClientCCB->psClientCCBMemDesc,
									ui32AllocPageCount,
									psClientCCB->pui32MappingTable,
									0,
									NULL,
#if !defined(PVRSRV_UNMAP_ON_SPARSE_CHANGE)
									SPARSE_MAP_CPU_ADDR |
#endif
									SPARSE_RESIZE_ALLOC);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXAcquireCCB: Failed to grow RGX client CCB (%s)",
				PVRSRVGetErrorString(eError)));

#ifdef PVRSRV_UNMAP_ON_SPARSE_CHANGE
		if (DevmemAcquireCpuVirtAddr(psClientCCB->psClientCCBMemDesc,
									&psClientCCB->pvClientCCB) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXAcquireCCB: Failed to reacquire CCB mapping"));
			psClientCCB->pvClientCCB = NULL;
		}
#endif

		return eError;
	}

#ifdef PVRSRV_UNMAP_ON_SPARSE_CHANGE
	eError = DevmemAcquireCpuVirtAddr(psClientCCB->psClientCCBMemDesc,
									&psClientCCB->pvClientCCB);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXAcquireCCB: Failed to map RGX client CCB (%s)",
				PVRSRVGetErrorString(eError)));
		return eError;
	}
#endif

	return PVRSRV_OK;
}
#endif /* defined(PVRSRV_ENABLE_CCCB_GROW) */

PVRSRV_ERROR RGXCheckSpaceCCB(RGX_CLIENT_CCB *psClientCCB, IMG_UINT32 ui32CmdSize)
{
	IMG_UINT32 ui32FreeSpace;

	/* Check that the CCB can hold this command + padding */
	if ((ui32CmdSize + PADDING_COMMAND_SIZE + 1) > psClientCCB->ui32Size)
	{
		PVR_DPF((PVR_DBG_ERROR, "Command size (%d bytes) too big for CCB"
		        " (%d bytes)", ui32CmdSize, psClientCCB->ui32Size));
		return PVRSRV_ERROR_CMD_TOO_BIG;
	}

	/*
		Check we don't overflow the end of the buffer and make sure we have
		enough space for the padding command. If we don't have enough space
		(including the minimum amount for the padding command) we need to make
		sure we insert a padding command now and wrap before adding the main
		command.
	*/
	if ((psClientCCB->ui32HostWriteOffset + ui32CmdSize + PADDING_COMMAND_SIZE) <= psClientCCB->ui32Size)
	{
		ui32FreeSpace = GET_CCB_SPACE(psClientCCB->ui32HostWriteOffset,
		                              psClientCCB->psClientCCBCtrl->ui32ReadOffset,
		                              psClientCCB->ui32Size);

		/* Don't allow all the space to be used */
		if (ui32FreeSpace > ui32CmdSize)
		{
			return PVRSRV_OK;
		}

		goto e_retry;
	}
	else
	{
		IMG_UINT32 ui32Remain = psClientCCB->ui32Size - psClientCCB->ui32HostWriteOffset;

		ui32FreeSpace = GET_CCB_SPACE(psClientCCB->ui32HostWriteOffset,
		                              psClientCCB->psClientCCBCtrl->ui32ReadOffset,
		                              psClientCCB->ui32Size);

		/* Check there is space for both the command and the padding command */
		if (ui32FreeSpace > ui32Remain + ui32CmdSize)
		{
			return PVRSRV_OK;
		}

		goto e_retry;
	}

e_retry:
#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)
	_RGXCCBUtilisationEvent(psClientCCB,
	            PVRSRV_CLIENT_CCCB_UTILISATION_WARNING_FULL_CCB,
	            ui32CmdSize);
#endif  /* PVRSRV_ENABLE_CCCB_UTILISATION_INFO */

	return PVRSRV_ERROR_RETRY;
}

/******************************************************************************
 FUNCTION	: RGXAcquireCCB

 PURPOSE	: Obtains access to write some commands to a CCB

 PARAMETERS	: psClientCCB		- The client CCB
			  ui32CmdSize		- How much space is required
			  ppvBufferSpace	- Pointer to space in the buffer
			  ui32PDumpFlags - Should this be PDump continuous?

 RETURNS	: PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR RGXAcquireCCB(RGX_CLIENT_CCB *psClientCCB,
										IMG_UINT32		ui32CmdSize,
										void			**ppvBufferSpace,
										IMG_UINT32		ui32PDumpFlags)
{
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	IMG_UINT32	ui32RetryCount = 2;
#endif

#if defined(PDUMP)
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo = FWCommonContextGetRGXDevInfo(psClientCCB->psServerCommonContext);
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	IMG_BOOL	bPDumpEnabled = PDumpCheckFlagsWrite(psDeviceNode, ui32PDumpFlags);
	IMG_BOOL	bPDumpFlagsContinuous = PDUMP_IS_CONTINUOUS(ui32PDumpFlags);

	/*
		PDumpSetFrame will detect as we Transition into capture range for
		frame based data but if we are PDumping continuous data then we
		need to inform the PDump layer ourselves

		First check is to confirm we are in continuous mode
		Second check is to confirm the pdump client is connected and ready.
		Third check is to confirm we are not in capture range.
	*/
	if (bPDumpFlagsContinuous &&
		bPDumpEnabled &&
		!PDumpCheckFlagsWrite(psDeviceNode, PDUMP_FLAGS_NONE))
	{
		eError = PDumpTransition(psDeviceNode,
		                         psClientCCB->psPDumpConnectionData,
		                         PDUMP_TRANSITION_EVENT_RANGE_ENTERED,
		                         ui32PDumpFlags);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}
#endif

	/* Check that the CCB can hold this command + padding */
	if ((ui32CmdSize + PADDING_COMMAND_SIZE + 1) > psClientCCB->ui32Size)
	{
		PVR_DPF((PVR_DBG_ERROR, "Command size (%d bytes) too big for CCB (%d bytes)",
								ui32CmdSize, psClientCCB->ui32Size));
		return PVRSRV_ERROR_CMD_TOO_BIG;
	}

#if defined(PVRSRV_ENABLE_CCCB_GROW)
	while (ui32RetryCount--)
#endif
	{
#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)
		psClientCCB->sUtilisation.ui32CCBAcquired++;
#endif

		/*
			Check we don't overflow the end of the buffer and make sure we have
			enough space for the padding command. We don't have enough space (including the
			minimum amount for the padding command) we will need to make sure we insert a
			padding command now and wrap before adding the main command.
		*/
		if ((psClientCCB->ui32HostWriteOffset + ui32CmdSize + PADDING_COMMAND_SIZE) <= psClientCCB->ui32Size)
		{
			/* The command can fit without wrapping... */
			IMG_UINT32 ui32FreeSpace;

#if defined(PDUMP)
			/* Wait for sufficient CCB space to become available */
			PDUMPCOMMENTWITHFLAGS(psDeviceNode, 0,
								  "Wait for %u bytes to become available according cCCB Ctl (woff=%x) for %s",
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

			/* Can command fit? */
			if (ui32FreeSpace > ui32CmdSize)
			{
				*ppvBufferSpace = IMG_OFFSET_ADDR(psClientCCB->pvClientCCB, psClientCCB->ui32HostWriteOffset);
				return PVRSRV_OK;
			}
			/* There is not enough free space in CCB. */
			goto e_retry;
		}
		else
		{
			/*
				We're at the end of the buffer without enough contiguous space.
				The command cannot fit without wrapping, we need to insert a
				padding command and wrap. We need to do this in one go otherwise
				we would be leaving unflushed commands and forcing the client to
				deal with flushing the padding command but not the command they
				wanted to write. Therefore we either do all or nothing.
			*/
			RGXFWIF_CCB_CMD_HEADER *psHeader;
			IMG_UINT32 ui32FreeSpace;
			IMG_UINT32 ui32Remain = psClientCCB->ui32Size - psClientCCB->ui32HostWriteOffset;

#if defined(PVRSRV_ENABLE_CCCB_GROW)
			/* Check this is a growable CCB */
			if (psClientCCB->ui32VirtualAllocSize > 0)
			{
				PVRSRV_RGXDEV_INFO *psDevInfo = FWCommonContextGetRGXDevInfo(psClientCCB->psServerCommonContext);

				ui32FreeSpace = GET_CCB_SPACE(psClientCCB->ui32HostWriteOffset,
											psClientCCB->psClientCCBCtrl->ui32ReadOffset,
											psClientCCB->ui32Size);
				/*
				 * Check if CCB should grow or be wrapped.
				 * Wrap CCB if there is no need for grow (CCB is half empty) or CCB can't grow,
				 * and when is free space for command and padding.
				 */
				if (((ui32FreeSpace > psClientCCB->ui32Size/2) || (psClientCCB->ui32Size == psClientCCB->ui32VirtualAllocSize)) &&
					(ui32FreeSpace > ui32Remain + ui32CmdSize))
				{
					/* Wrap CCB */
					psHeader = IMG_OFFSET_ADDR(psClientCCB->pvClientCCB, psClientCCB->ui32HostWriteOffset);
					psHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_PADDING;
					psHeader->ui32CmdSize = ui32Remain - sizeof(RGXFWIF_CCB_CMD_HEADER);

#if defined(PDUMP)
					PDUMPCOMMENTWITHFLAGS(psDeviceNode, ui32PDumpFlags,
										  "cCCB(%p): Padding cmd %d", psClientCCB, psHeader->ui32CmdSize);
					if (bPDumpEnabled)
					{
						DevmemPDumpLoadMem(psClientCCB->psClientCCBMemDesc,
										psClientCCB->ui32HostWriteOffset,
										ui32Remain,
										ui32PDumpFlags);
					}
#endif

					*ppvBufferSpace = psClientCCB->pvClientCCB;
					return PVRSRV_OK;
				}
				else if ((psClientCCB->ui32Size < psClientCCB->ui32VirtualAllocSize) &&
				         (psClientCCB->ui32HostWriteOffset >= psClientCCB->psClientCCBCtrl->ui32ReadOffset))
				{
					/* Grow CCB */
					PHYS_HEAP *psPhysHeap = psDevInfo->psDeviceNode->apsPhysHeap[PVRSRV_PHYS_HEAP_FW_MAIN];
					PHYS_HEAP_POLICY uiHeapPolicy = PhysHeapGetPolicy(psPhysHeap);
					PVRSRV_ERROR eErr = PVRSRV_OK;

					/* Something went wrong if we are here a second time */
					PVR_ASSERT(ui32RetryCount != 0);
					OSLockAcquire(psClientCCB->hCCBGrowLock);

					/*
					 * On LMA sparse memory can't be mapped to kernel without support for non physically
					 * sparse allocations.
					 * To work around this whole ccb memory was allocated at once as contiguous.
					 * In such case below sparse change is not needed because memory is already allocated.
					 */
					if (uiHeapPolicy == PHYS_HEAP_POLICY_ALLOC_ALLOW_NONCONTIG)
					{
						IMG_UINT32 ui32AllocChunkCount = psClientCCB->ui32Size / psClientCCB->ui32ChunkSize;

						eErr = _RGXCCBMemChangeSparse(psClientCCB, ui32AllocChunkCount);
					}

					/* Setup new CCB size */
					if (eErr == PVRSRV_OK)
					{
						psClientCCB->ui32Size += psClientCCB->ui32Size;
					}
					else
					{
						PVR_LOG(("%s: Client CCB (%s) grow failed (%s)", __func__, psClientCCB->szName, PVRSRVGetErrorString(eErr)));
						OSLockRelease(psClientCCB->hCCBGrowLock);
						goto e_retry;
					}

#if defined(PDUMP)
					PDUMPCOMMENTWITHFLAGS(psDeviceNode, ui32PDumpFlags, "cCCB update for grow");
					if (bPDumpEnabled)
					{
						DevmemPDumpLoadMem(psClientCCB->psClientCCBCtrlMemDesc,
											offsetof(RGXFWIF_CCCB_CTL, ui32WrapMask),
											sizeof(psClientCCB->psClientCCBCtrl->ui32WrapMask),
											ui32PDumpFlags);
						DevmemPDumpLoadMem(psClientCCB->psClientCCBMemDesc,
											offsetof(RGX_CLIENT_CCB, ui32Size),
											sizeof(psClientCCB->ui32Size),
											ui32PDumpFlags);
					}
#endif /* defined(PVRSRV_ENABLE_CCCB_GROW) */

#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)
					PVR_LOG(("%s: Client CCB (%s) grew to %u", __func__, psClientCCB->szName, psClientCCB->ui32Size));
					/* Reset counters */
					_RGXInitCCBUtilisation(psClientCCB);
#endif

					/* CCB doubled the size so retry now. */
					OSLockRelease(psClientCCB->hCCBGrowLock);
				}
				else
				{
					/* CCB can't grow anymore and can't be wrapped */
#if defined(PDUMP)
					/* Wait for sufficient CCB space to become available */
					PDUMPCOMMENTWITHFLAGS(psDeviceNode, 0,
										  "Wait for %u bytes to become available according cCCB Ctl (woff=%x) for %s",
										  ui32Remain, psClientCCB->ui32HostWriteOffset,
										  psClientCCB->szName);
					DevmemPDumpCBP(psClientCCB->psClientCCBCtrlMemDesc,
								offsetof(RGXFWIF_CCCB_CTL, ui32ReadOffset),
								psClientCCB->ui32HostWriteOffset,
								ui32Remain,
								psClientCCB->ui32Size);
					PDUMPCOMMENTWITHFLAGS(psDeviceNode, 0,
										  "Wait for %u bytes to become available according cCCB Ctl (woff=%x) for %s",
										  ui32CmdSize, 0 /*ui32HostWriteOffset after wrap */,
										  psClientCCB->szName);
					DevmemPDumpCBP(psClientCCB->psClientCCBCtrlMemDesc,
								offsetof(RGXFWIF_CCCB_CTL, ui32ReadOffset),
								0 /*ui32HostWriteOffset after wrap */,
								ui32CmdSize,
								psClientCCB->ui32Size);
					/* CCB has now space for our command so try wrapping again. Retry now. */
#else /* defined(PDUMP) */
					goto e_retry;
#endif /* defined(PDUMP) */
				}
			}
			else
#endif /* defined(PVRSRV_ENABLE_CCCB_GROW) */
			{
#if defined(PDUMP)
				/* Wait for sufficient CCB space to become available */
				PDUMPCOMMENTWITHFLAGS(psDeviceNode, 0,
									  "Wait for %u bytes to become available according cCCB Ctl (woff=%x) for %s",
									  ui32Remain, psClientCCB->ui32HostWriteOffset,
									  psClientCCB->szName);
				DevmemPDumpCBP(psClientCCB->psClientCCBCtrlMemDesc,
							offsetof(RGXFWIF_CCCB_CTL, ui32ReadOffset),
							psClientCCB->ui32HostWriteOffset,
							ui32Remain,
							psClientCCB->ui32Size);
				PDUMPCOMMENTWITHFLAGS(psDeviceNode, 0,
									  "Wait for %u bytes to become available according cCCB Ctl (woff=%x) for %s",
									  ui32CmdSize, 0 /*ui32HostWriteOffset after wrap */,
									  psClientCCB->szName);
				DevmemPDumpCBP(psClientCCB->psClientCCBCtrlMemDesc,
							offsetof(RGXFWIF_CCCB_CTL, ui32ReadOffset),
							0 /*ui32HostWriteOffset after wrap */,
							ui32CmdSize,
							psClientCCB->ui32Size);
#endif
				ui32FreeSpace = GET_CCB_SPACE(psClientCCB->ui32HostWriteOffset,
											psClientCCB->psClientCCBCtrl->ui32ReadOffset,
											psClientCCB->ui32Size);

				if (ui32FreeSpace > ui32Remain + ui32CmdSize)
				{
					psHeader = IMG_OFFSET_ADDR(psClientCCB->pvClientCCB, psClientCCB->ui32HostWriteOffset);
					psHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_PADDING;
					psHeader->ui32CmdSize = ui32Remain - sizeof(RGXFWIF_CCB_CMD_HEADER);
#if defined(PDUMP)
					PDUMPCOMMENTWITHFLAGS(psDeviceNode, ui32PDumpFlags, "cCCB(%p): Padding cmd %d", psClientCCB, psHeader->ui32CmdSize);
					if (bPDumpEnabled)
					{
						DevmemPDumpLoadMem(psClientCCB->psClientCCBMemDesc,
										psClientCCB->ui32HostWriteOffset,
										ui32Remain,
										ui32PDumpFlags);
					}
#endif

					*ppvBufferSpace = psClientCCB->pvClientCCB;
					return PVRSRV_OK;
				}

				goto e_retry;
			}
		}
	}
e_retry:
#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)
	psClientCCB->sUtilisation.ui32CCBFull++;
	_RGXCCBUtilisationEvent(psClientCCB,
				PVRSRV_CLIENT_CCCB_UTILISATION_WARNING_ACQUIRE_FAILED,
				ui32CmdSize);
#endif /* PVRSRV_ENABLE_CCCB_UTILISATION_INFO */
	return PVRSRV_ERROR_RETRY;
}

/******************************************************************************
 FUNCTION	: RGXReleaseCCB

 PURPOSE	: Release a CCB that we have been writing to.

 PARAMETERS	: psDevData			- device data
			  psCCB				- the CCB

 RETURNS	: None
******************************************************************************/
void RGXReleaseCCB(RGX_CLIENT_CCB *psClientCCB,
								IMG_UINT32		ui32CmdSize,
								IMG_UINT32		ui32PDumpFlags)
{
#if defined(PDUMP)
	PVRSRV_RGXDEV_INFO *psDevInfo = FWCommonContextGetRGXDevInfo(psClientCCB->psServerCommonContext);
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	IMG_BOOL	bPDumpEnabled = PDumpCheckFlagsWrite(psDeviceNode, ui32PDumpFlags);
	IMG_BOOL	bPDumpFlagsContinuous = PDUMP_IS_CONTINUOUS(ui32PDumpFlags);
#endif

#if defined(PVRSRV_ENABLE_CCCB_GROW)
	OSLockAcquire(psClientCCB->hCCBGrowLock);
#endif
	/*
	 * If a padding command was needed then we should now move ui32HostWriteOffset
	 * forward. The command has already be dumped (if bPDumpEnabled).
	 */
	if ((psClientCCB->ui32HostWriteOffset + ui32CmdSize + PADDING_COMMAND_SIZE) > psClientCCB->ui32Size)
	{
		IMG_UINT32 ui32Remain = psClientCCB->ui32Size - psClientCCB->ui32HostWriteOffset;

		UPDATE_CCB_OFFSET(psClientCCB->ui32HostWriteOffset,
						  ui32Remain,
						  psClientCCB->ui32Size);
		psClientCCB->ui32ByteCount += ui32Remain;
	}

#if defined(PDUMP)
	/* Dump the CCB data */
	if (bPDumpEnabled)
	{
		DevmemPDumpLoadMem(psClientCCB->psClientCCBMemDesc,
						   psClientCCB->ui32HostWriteOffset,
						   ui32CmdSize,
						   ui32PDumpFlags);
	}
#endif

	/*
	 * Check if there any fences being written that will already be
	 * satisfied by the last written update command in this CCB. At the
	 * same time we can ASSERT that all sync addresses are not NULL.
	 */
#if defined(DEBUG)
	{
		void *pvBufferStart = IMG_OFFSET_ADDR(psClientCCB->pvClientCCB, psClientCCB->ui32HostWriteOffset);
		void *pvBufferEnd   = IMG_OFFSET_ADDR(psClientCCB->pvClientCCB, psClientCCB->ui32HostWriteOffset + ui32CmdSize);
		IMG_BOOL  bMessagePrinted  = IMG_FALSE;

		/* Walk through the commands in this section of CCB being released... */
		while (pvBufferStart < pvBufferEnd)
		{
			RGXFWIF_CCB_CMD_HEADER *psCmdHeader = pvBufferStart;

			if (psCmdHeader->eCmdType == RGXFWIF_CCB_CMD_TYPE_UPDATE)
			{
				/* If an UPDATE then record the values in case an adjacent fence uses it. */
				IMG_UINT32  ui32NumUFOs = psCmdHeader->ui32CmdSize / sizeof(RGXFWIF_UFO);
				RGXFWIF_UFO *psUFOPtr   = IMG_OFFSET_ADDR(pvBufferStart, sizeof(RGXFWIF_CCB_CMD_HEADER));

				psClientCCB->ui32UpdateEntries = 0;
				while (ui32NumUFOs-- > 0)
				{
					PVR_ASSERT(psUFOPtr->puiAddrUFO.ui32Addr != 0);
					if (psClientCCB->ui32UpdateEntries < RGX_CCCB_FENCE_UPDATE_LIST_SIZE)
					{
						psClientCCB->asFenceUpdateList[psClientCCB->ui32UpdateEntries++] = *psUFOPtr++;
					}
				}
			}
			else if (psCmdHeader->eCmdType == RGXFWIF_CCB_CMD_TYPE_FENCE)
			{
				/* If a FENCE then check the values against the last UPDATE issued. */
				IMG_UINT32  ui32NumUFOs = psCmdHeader->ui32CmdSize / sizeof(RGXFWIF_UFO);
				RGXFWIF_UFO *psUFOPtr   = IMG_OFFSET_ADDR(pvBufferStart, sizeof(RGXFWIF_CCB_CMD_HEADER));

				while (ui32NumUFOs-- > 0)
				{
					PVR_ASSERT(psUFOPtr->puiAddrUFO.ui32Addr != 0);

					if (bMessagePrinted == IMG_FALSE)
					{
						RGXFWIF_UFO *psUpdatePtr = psClientCCB->asFenceUpdateList;
						IMG_UINT32  ui32UpdateIndex;

						for (ui32UpdateIndex = 0; ui32UpdateIndex < psClientCCB->ui32UpdateEntries; ui32UpdateIndex++)
						{
							if (PVRSRV_UFO_IS_SYNC_CHECKPOINT(psUFOPtr))
							{
								if (RGX_UFO_PTR_ADDR(psUFOPtr) == RGX_UFO_PTR_ADDR(psUpdatePtr))
								{
									PVR_DPF((PVR_DBG_MESSAGE, "Redundant sync checkpoint check found in cCCB(%p) - 0x%x -> 0x%x",
											psClientCCB, RGX_UFO_PTR_ADDR(psUFOPtr), psUFOPtr->ui32Value));
									bMessagePrinted = IMG_TRUE;
									break;
								}
							}
							else
							{
								if (psUFOPtr->puiAddrUFO.ui32Addr == psUpdatePtr->puiAddrUFO.ui32Addr  &&
									psUFOPtr->ui32Value == psUpdatePtr->ui32Value)
								{
									PVR_DPF((PVR_DBG_MESSAGE, "Redundant fence check found in cCCB(%p) - 0x%x -> 0x%x",
											psClientCCB, psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value));
									bMessagePrinted = IMG_TRUE;
									break;
								}
							}
							psUpdatePtr++;
						}
					}

					psUFOPtr++;
				}
			}
			else if (psCmdHeader->eCmdType == RGXFWIF_CCB_CMD_TYPE_FENCE_PR  ||
					 psCmdHeader->eCmdType == RGXFWIF_CCB_CMD_TYPE_UNFENCED_UPDATE)
			{
				/* For all other UFO ops check the UFO address is not NULL. */
				IMG_UINT32  ui32NumUFOs = psCmdHeader->ui32CmdSize / sizeof(RGXFWIF_UFO);
				RGXFWIF_UFO *psUFOPtr   = IMG_OFFSET_ADDR(pvBufferStart, sizeof(RGXFWIF_CCB_CMD_HEADER));

				while (ui32NumUFOs-- > 0)
				{
					PVR_ASSERT(psUFOPtr->puiAddrUFO.ui32Addr != 0);
					psUFOPtr++;
				}
			}

			/* Move to the next command in this section of CCB being released... */
			pvBufferStart = IMG_OFFSET_ADDR(pvBufferStart, sizeof(RGXFWIF_CCB_CMD_HEADER) + psCmdHeader->ui32CmdSize);
		}
	}
#endif /* REDUNDANT_SYNCS_DEBUG */


#if defined(PVRSRV_FORCE_FLUSH_CCCB_ON_KICK)
	{
		DEVMEM_MEMDESC* psClientCCBMemDesc = psClientCCB->psClientCCBMemDesc;
		void *pvClientCCBAddr = psClientCCB->pvClientCCB;
		PMR *psClientCCBMemDescPMR = NULL;
		IMG_DEVMEM_OFFSET_T uiPMROffset;

		DevmemGetPMRData(psClientCCBMemDesc,
			             (IMG_HANDLE*)&psClientCCBMemDescPMR,
			             &uiPMROffset);

		CacheOpValExec(psClientCCBMemDescPMR,
					   (IMG_UINT64)(uintptr_t) pvClientCCBAddr,
					   uiPMROffset,
					   psClientCCBMemDesc->uiAllocSize,
					   PVRSRV_CACHE_OP_FLUSH);

	}
#endif
	/*
	 * Update the CCB write offset.
	 */
	UPDATE_CCB_OFFSET(psClientCCB->ui32HostWriteOffset,
					  ui32CmdSize,
					  psClientCCB->ui32Size);
	psClientCCB->ui32ByteCount += ui32CmdSize;

#if defined(PVRSRV_ENABLE_CCCB_UTILISATION_INFO)
	_RGXUpdateCCBUtilisation(psClientCCB);
#endif
	/*
		PDumpSetFrame will detect as we Transition out of capture range for
		frame based data but if we are PDumping continuous data then we
		need to inform the PDump layer ourselves

		First check is to confirm we are in continuous mode
		Second check is to confirm the pdump client is connected and ready.
		Third check is to confirm we are not in capture range.
	*/
#if defined(PDUMP)
	if (bPDumpFlagsContinuous &&
		bPDumpEnabled &&
		!PDumpCheckFlagsWrite(psDeviceNode, PDUMP_FLAGS_NONE))
	{
		PVRSRV_ERROR eError;

		/* Only Transitioning into capture range can cause an error */
		eError = PDumpTransition(psDeviceNode,
		                         psClientCCB->psPDumpConnectionData,
		                         PDUMP_TRANSITION_EVENT_RANGE_EXITED,
		                         ui32PDumpFlags);
		PVR_ASSERT(eError == PVRSRV_OK);
	}

	if (bPDumpEnabled)
	{
		if (!BIT_ISSET(psClientCCB->ui32CCBFlags, CCB_FLAGS_CCB_STATE_OPEN))
		{
			/* Store offset to last finished CCB command. This offset can
			 * be needed when appending commands to a non finished CCB.
			 */
			psClientCCB->ui32FinishedPDumpWriteOffset = psClientCCB->ui32LastPDumpWriteOffset;
		}

		/* Update the PDump write offset to show we PDumped this command */
		psClientCCB->ui32LastPDumpWriteOffset = psClientCCB->ui32HostWriteOffset;
	}
#endif

#if defined(NO_HARDWARE)
	/*
		The firmware is not running, it cannot update these; we do here instead.
	*/
	psClientCCB->psClientCCBCtrl->ui32ReadOffset = psClientCCB->ui32HostWriteOffset;
	psClientCCB->psClientCCBCtrl->ui32DepOffset = psClientCCB->ui32HostWriteOffset;
#if defined(SUPPORT_AGP)
	psClientCCB->psClientCCBCtrl->ui32ReadOffset2 = psClientCCB->ui32HostWriteOffset;
#if defined(SUPPORT_AGP4)
	psClientCCB->psClientCCBCtrl->ui32ReadOffset3 = psClientCCB->ui32HostWriteOffset;
	psClientCCB->psClientCCBCtrl->ui32ReadOffset4 = psClientCCB->ui32HostWriteOffset;
#endif
#endif
#endif

#if defined(PVRSRV_ENABLE_CCCB_GROW)
	OSLockRelease(psClientCCB->hCCBGrowLock);
#endif
}

IMG_UINT32 RGXGetHostWriteOffsetCCB(RGX_CLIENT_CCB *psClientCCB)
{
	return psClientCCB->ui32HostWriteOffset;
}

IMG_UINT32 RGXGetWrapMaskCCB(RGX_CLIENT_CCB *psClientCCB)
{
	return psClientCCB->ui32Size-1;
}

void RGXSetCCBFlags(RGX_CLIENT_CCB *psClientCCB,
					IMG_UINT32		ui32Flags)
{
	if ((ui32Flags & RGX_CONTEXT_FLAG_DISABLESLR))
	{
		BIT_SET(psClientCCB->ui32CCBFlags, CCB_FLAGS_SLR_DISABLED);
	}
	else
	{
		BIT_UNSET(psClientCCB->ui32CCBFlags, CCB_FLAGS_SLR_DISABLED);
	}
}

void RGXCmdHelperInitCmdCCB_CommandSize(PVRSRV_RGXDEV_INFO *psDevInfo,
                                        IMG_UINT64 ui64FBSCEntryMask,
                                        IMG_UINT32 ui32ClientFenceCount,
                                        IMG_UINT32 ui32ClientUpdateCount,
                                        IMG_UINT32 ui32CmdSize,
                                        PRGXFWIF_TIMESTAMP_ADDR   *ppPreAddr,
                                        PRGXFWIF_TIMESTAMP_ADDR   *ppPostAddr,
                                        PRGXFWIF_UFO_ADDR         *ppRMWUFOAddr,
                                        RGX_CCB_CMD_HELPER_DATA *psCmdHelperData)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	IMG_BOOL bCacheInval = IMG_TRUE;
	/* Init the generated data members */
	psCmdHelperData->ui32FBSCInvalCmdSize = 0;
	psCmdHelperData->ui64FBSCEntryMask = 0;
	psCmdHelperData->ui32FenceCmdSize = 0;
	psCmdHelperData->ui32UpdateCmdSize = 0;
	psCmdHelperData->ui32PreTimeStampCmdSize = 0;
	psCmdHelperData->ui32PostTimeStampCmdSize = 0;
	psCmdHelperData->ui32RMWUFOCmdSize = 0;

	/* Only compile if RGX_FEATURE_PDS_INSTRUCTION_CACHE_AUTO_INVALIDATE is defined to avoid
	 * compilation errors on rogue cores.
	 */
#if defined(RGX_FEATURE_PDS_INSTRUCTION_CACHE_AUTO_INVALIDATE)
	bCacheInval = !(PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, PDS_INSTRUCTION_CACHE_AUTO_INVALIDATE) &&
				    PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, USC_INSTRUCTION_CACHE_AUTO_INVALIDATE) &&
				    PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, TDM_SLC_MMU_AUTO_CACHE_OPS) &&
				    PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, GEOM_SLC_MMU_AUTO_CACHE_OPS) &&
				    PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, FRAG_SLC_MMU_AUTO_CACHE_OPS) &&
				    PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, COMPUTE_SLC_MMU_AUTO_CACHE_OPS)) ||
				    RGX_IS_BRN_SUPPORTED(psDevInfo, 71960) ||
				    RGX_IS_BRN_SUPPORTED(psDevInfo, 72143);
#else
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
#endif

	/* Total FBSC invalidate command size (header plus command data) */
	if (bCacheInval)
	{
		if (ui64FBSCEntryMask != 0)
		{
			psCmdHelperData->ui32FBSCInvalCmdSize =
			        RGX_CCB_FWALLOC_ALIGN(sizeof(psCmdHelperData->ui64FBSCEntryMask) +
			                              sizeof(RGXFWIF_CCB_CMD_HEADER));
			psCmdHelperData->ui64FBSCEntryMask = ui64FBSCEntryMask;
		}
	}

	/* total DM command size (header plus command data) */

	psCmdHelperData->ui32DMCmdSize =
	        RGX_CCB_FWALLOC_ALIGN(ui32CmdSize + sizeof(RGXFWIF_CCB_CMD_HEADER));

	if (ui32ClientFenceCount != 0)
	{
		psCmdHelperData->ui32FenceCmdSize =
		        RGX_CCB_FWALLOC_ALIGN(ui32ClientFenceCount * sizeof(RGXFWIF_UFO) +
		                              sizeof(RGXFWIF_CCB_CMD_HEADER));
	}

	if (ui32ClientUpdateCount != 0)
	{
		psCmdHelperData->ui32UpdateCmdSize =
		        RGX_CCB_FWALLOC_ALIGN(ui32ClientUpdateCount * sizeof(RGXFWIF_UFO) +
		                              sizeof(RGXFWIF_CCB_CMD_HEADER));
	}

	if (ppPreAddr && (ppPreAddr->ui32Addr != 0))
	{
		psCmdHelperData->ui32PreTimeStampCmdSize = sizeof(RGXFWIF_CCB_CMD_HEADER)
			+ PVR_ALIGN(sizeof(RGXFWIF_DEV_VIRTADDR), RGXFWIF_FWALLOC_ALIGN);
	}

	if (ppPostAddr && (ppPostAddr->ui32Addr != 0))
	{
		psCmdHelperData->ui32PostTimeStampCmdSize = sizeof(RGXFWIF_CCB_CMD_HEADER)
			+ PVR_ALIGN(sizeof(RGXFWIF_DEV_VIRTADDR), RGXFWIF_FWALLOC_ALIGN);
	}

	if (ppRMWUFOAddr && (ppRMWUFOAddr->ui32Addr != 0))
	{
		psCmdHelperData->ui32RMWUFOCmdSize = sizeof(RGXFWIF_CCB_CMD_HEADER) + sizeof(RGXFWIF_UFO);
	}
}

/*
	Work out how much space this command will require
*/
void RGXCmdHelperInitCmdCCB_OtherData(RGX_CLIENT_CCB            *psClientCCB,
                                      IMG_UINT32                ui32ClientFenceCount,
                                      PRGXFWIF_UFO_ADDR         *pauiFenceUFOAddress,
                                      IMG_UINT32                *paui32FenceValue,
                                      IMG_UINT32                ui32ClientUpdateCount,
                                      PRGXFWIF_UFO_ADDR         *pauiUpdateUFOAddress,
                                      IMG_UINT32                *paui32UpdateValue,
                                      IMG_UINT32                ui32CmdSize,
                                      IMG_PBYTE                 pui8DMCmd,
                                      PRGXFWIF_TIMESTAMP_ADDR   *ppPreAddr,
                                      PRGXFWIF_TIMESTAMP_ADDR   *ppPostAddr,
                                      PRGXFWIF_UFO_ADDR         *ppRMWUFOAddr,
                                      RGXFWIF_CCB_CMD_TYPE      eType,
                                      IMG_UINT32                ui32ExtJobRef,
                                      IMG_UINT32                ui32IntJobRef,
                                      IMG_UINT32                ui32PDumpFlags,
                                      RGXFWIF_WORKEST_KICK_DATA *psWorkEstKickData,
                                      IMG_CHAR                  *pszCommandName,
                                      IMG_BOOL                  bCCBStateOpen,
                                      RGX_CCB_CMD_HELPER_DATA   *psCmdHelperData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = NULL;

	/* Job reference values */
	psCmdHelperData->ui32ExtJobRef = ui32ExtJobRef;
	psCmdHelperData->ui32IntJobRef = ui32IntJobRef;

	/* Save the data we require in the submit call */
	psCmdHelperData->psClientCCB = psClientCCB;
#if defined(PDUMP)
	psCmdHelperData->ui32PDumpFlags = ui32PDumpFlags;
	psDevInfo = FWCommonContextGetRGXDevInfo(psCmdHelperData->psClientCCB->psServerCommonContext);
#else
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
#endif
	psCmdHelperData->pszCommandName = pszCommandName;
	if (bCCBStateOpen)
	{
		BIT_SET(psCmdHelperData->psClientCCB->ui32CCBFlags, CCB_FLAGS_CCB_STATE_OPEN);
	}
	else
	{
		BIT_UNSET(psCmdHelperData->psClientCCB->ui32CCBFlags, CCB_FLAGS_CCB_STATE_OPEN);
	}

	/* Client sync data */
	psCmdHelperData->ui32ClientFenceCount = ui32ClientFenceCount;
	psCmdHelperData->pauiFenceUFOAddress = pauiFenceUFOAddress;
	psCmdHelperData->paui32FenceValue = paui32FenceValue;
	psCmdHelperData->ui32ClientUpdateCount = ui32ClientUpdateCount;
	psCmdHelperData->pauiUpdateUFOAddress = pauiUpdateUFOAddress;
	psCmdHelperData->paui32UpdateValue = paui32UpdateValue;

	/* Command data */
	psCmdHelperData->ui32CmdSize = ui32CmdSize;
	psCmdHelperData->pui8DMCmd = pui8DMCmd;
	psCmdHelperData->eType = eType;

	if (ppPreAddr)
	{
		psCmdHelperData->pPreTimestampAddr = *ppPreAddr;
	}

	if (ppPostAddr)
	{
		psCmdHelperData->pPostTimestampAddr = *ppPostAddr;
	}

	if (ppRMWUFOAddr)
	{
		psCmdHelperData->pRMWUFOAddr = *ppRMWUFOAddr;
	}

	PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode, ui32PDumpFlags,
			"%s Command Server Init on FWCtx %08x", pszCommandName,
			FWCommonContextGetFWAddress(psClientCCB->psServerCommonContext).ui32Addr);

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		/* Workload Data added */
		psCmdHelperData->psWorkEstKickData = psWorkEstKickData;
	}
#endif
}

/*
	Work out how much space this command will require
*/
void RGXCmdHelperInitCmdCCB(PVRSRV_RGXDEV_INFO		*psDevInfo,
                            RGX_CLIENT_CCB            *psClientCCB,
                            IMG_UINT64                ui64FBSCEntryMask,
                            IMG_UINT32                ui32ClientFenceCount,
                            PRGXFWIF_UFO_ADDR         *pauiFenceUFOAddress,
                            IMG_UINT32                *paui32FenceValue,
                            IMG_UINT32                ui32ClientUpdateCount,
                            PRGXFWIF_UFO_ADDR         *pauiUpdateUFOAddress,
                            IMG_UINT32                *paui32UpdateValue,
                            IMG_UINT32                ui32CmdSize,
                            IMG_PBYTE                 pui8DMCmd,
                            PRGXFWIF_TIMESTAMP_ADDR   *ppPreAddr,
                            PRGXFWIF_TIMESTAMP_ADDR   *ppPostAddr,
                            PRGXFWIF_UFO_ADDR         *ppRMWUFOAddr,
                            RGXFWIF_CCB_CMD_TYPE      eType,
                            IMG_UINT32                ui32ExtJobRef,
                            IMG_UINT32                ui32IntJobRef,
                            IMG_UINT32                ui32PDumpFlags,
                            RGXFWIF_WORKEST_KICK_DATA *psWorkEstKickData,
                            IMG_CHAR                  *pszCommandName,
                            IMG_BOOL                  bCCBStateOpen,
                            RGX_CCB_CMD_HELPER_DATA   *psCmdHelperData)
{
	RGXCmdHelperInitCmdCCB_CommandSize(psDevInfo,
	                                 ui64FBSCEntryMask,
	                                 ui32ClientFenceCount,
	                                 ui32ClientUpdateCount,
	                                 ui32CmdSize,
	                                 ppPreAddr,
	                                 ppPostAddr,
	                                 ppRMWUFOAddr,
	                                 psCmdHelperData);

	RGXCmdHelperInitCmdCCB_OtherData(psClientCCB,
	                                 ui32ClientFenceCount,
	                                 pauiFenceUFOAddress,
	                                 paui32FenceValue,
	                                 ui32ClientUpdateCount,
	                                 pauiUpdateUFOAddress,
	                                 paui32UpdateValue,
	                                 ui32CmdSize,
	                                 pui8DMCmd,
	                                 ppPreAddr,
	                                 ppPostAddr,
	                                 ppRMWUFOAddr,
	                                 eType,
	                                 ui32ExtJobRef,
	                                 ui32IntJobRef,
	                                 ui32PDumpFlags,
	                                 psWorkEstKickData,
	                                 pszCommandName,
	                                 bCCBStateOpen,
	                                 psCmdHelperData);
}

/*
	Reserve space in the CCB and fill in the command and client sync data
*/
PVRSRV_ERROR RGXCmdHelperAcquireCmdCCB(IMG_UINT32 ui32CmdCount,
									   RGX_CCB_CMD_HELPER_DATA *asCmdHelperData)
{
	const IMG_UINT32 ui32MaxUFOCmdSize = RGX_CCB_FWALLOC_ALIGN((RGXFWIF_CCB_CMD_MAX_UFOS * sizeof(RGXFWIF_UFO)) +
	                                                           sizeof(RGXFWIF_CCB_CMD_HEADER));
	IMG_UINT32 ui32AllocSize = 0;
	IMG_UINT32 i;
	void *pvStartPtr;
	PVRSRV_ERROR eError;
#if defined(PDUMP)
	PVRSRV_RGXDEV_INFO *psDevInfo = FWCommonContextGetRGXDevInfo(asCmdHelperData->psClientCCB->psServerCommonContext);
#endif

	/*
		Check the number of fences & updates are valid.
	*/
	for (i = 0; i < ui32CmdCount; i++)
	{
		RGX_CCB_CMD_HELPER_DATA *psCmdHelperData = &asCmdHelperData[i];

		if (psCmdHelperData->ui32FenceCmdSize > ui32MaxUFOCmdSize ||
		    psCmdHelperData->ui32UpdateCmdSize > ui32MaxUFOCmdSize)
		{
			return PVRSRV_ERROR_TOO_MANY_SYNCS;
		}
	}

	/*
		Work out how much space we need for all the command(s)
	*/
	ui32AllocSize = RGXCmdHelperGetCommandSize(ui32CmdCount, asCmdHelperData);

#if defined(PDUMP)
	for (i = 0; i < ui32CmdCount; i++)
	{
		if ((asCmdHelperData[0].ui32PDumpFlags ^ asCmdHelperData[i].ui32PDumpFlags) & PDUMP_FLAGS_CONTINUOUS)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: PDump continuous is not consistent (%s != %s) for command %d",
					 __func__,
					 PDUMP_IS_CONTINUOUS(asCmdHelperData[0].ui32PDumpFlags)?"IMG_TRUE":"IMG_FALSE",
					 PDUMP_IS_CONTINUOUS(asCmdHelperData[i].ui32PDumpFlags)?"IMG_TRUE":"IMG_FALSE",
					 ui32CmdCount));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}
#endif

	/*
		Acquire space in the CCB for all the command(s).
	*/
	eError = RGXAcquireCCB(asCmdHelperData[0].psClientCCB,
						   ui32AllocSize,
						   &pvStartPtr,
						   asCmdHelperData[0].ui32PDumpFlags);
	if (unlikely(eError != PVRSRV_OK))
	{
		return eError;
	}

	/*
		For each command fill in the fence, DM, and update command

	*/
	for (i = 0; i < ui32CmdCount; i++)
	{
		RGX_CCB_CMD_HELPER_DATA *psCmdHelperData = & asCmdHelperData[i];
		void *pvCmdPtr;
#if defined(PDUMP)
		IMG_UINT32 ui32CtxAddr = FWCommonContextGetFWAddress(asCmdHelperData->psClientCCB->psServerCommonContext).ui32Addr;
		IMG_UINT32 ui32CcbWoff = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(asCmdHelperData->psClientCCB->psServerCommonContext));
#endif

		if (psCmdHelperData->ui32ClientFenceCount+psCmdHelperData->ui32ClientUpdateCount != 0)
		{
			PDUMPCOMMENT(psDevInfo->psDeviceNode,
						 "Start of %s client syncs for cmd[%d] on FWCtx %08x Woff 0x%x bytes",
						 psCmdHelperData->psClientCCB->szName, i, ui32CtxAddr, ui32CcbWoff);
		}

		pvCmdPtr = pvStartPtr;

		/*
			Create the fence command.
		*/
		if (psCmdHelperData->ui32FenceCmdSize)
		{
			RGXFWIF_CCB_CMD_HEADER *psHeader;
			IMG_UINT k, uiNextValueIndex;

			psHeader = pvCmdPtr;
			psHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_FENCE;

			psHeader->ui32CmdSize = psCmdHelperData->ui32FenceCmdSize - sizeof(RGXFWIF_CCB_CMD_HEADER);
			psHeader->ui32ExtJobRef = psCmdHelperData->ui32ExtJobRef;
			psHeader->ui32IntJobRef = psCmdHelperData->ui32IntJobRef;
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
			if (!PVRSRV_VZ_MODE_IS(GUEST))
			{
				psHeader->sWorkEstKickData.ui16ReturnDataIndex = 0;
				psHeader->sWorkEstKickData.ui64Deadline = 0;
				psHeader->sWorkEstKickData.ui32CyclesPrediction = 0;
			}
#endif

			pvCmdPtr = IMG_OFFSET_ADDR(pvCmdPtr, sizeof(RGXFWIF_CCB_CMD_HEADER));

			/* Fill in the client fences */
			uiNextValueIndex = 0;
			for (k = 0; k < psCmdHelperData->ui32ClientFenceCount; k++)
			{
				RGXFWIF_UFO *psUFOPtr = pvCmdPtr;

				psUFOPtr->puiAddrUFO = psCmdHelperData->pauiFenceUFOAddress[k];

				if (PVRSRV_UFO_IS_SYNC_CHECKPOINT(psUFOPtr))
				{
					psUFOPtr->ui32Value = PVRSRV_SYNC_CHECKPOINT_SIGNALLED;
				}
				else
				{
					/* Only increment uiNextValueIndex for non sync checkpoints
					 * (as paui32FenceValue only contains values for sync prims)
					 */
					psUFOPtr->ui32Value = psCmdHelperData->paui32FenceValue[uiNextValueIndex++];
				}
				pvCmdPtr = IMG_OFFSET_ADDR(pvCmdPtr, sizeof(RGXFWIF_UFO));

#if defined(SYNC_COMMAND_DEBUG)
				PVR_DPF((PVR_DBG_ERROR, "%s client sync fence - 0x%x -> 0x%x",
						psCmdHelperData->psClientCCB->szName, psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value));
#endif
				PDUMPCOMMENT(psDevInfo->psDeviceNode,
							 ".. %s client sync fence - 0x%x -> 0x%x",
							 psCmdHelperData->psClientCCB->szName,
							 psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value);


			}
		}

		/*
			Create the FBSC invalidate command.
		*/
		if (psCmdHelperData->ui32FBSCInvalCmdSize)
		{
			RGXFWIF_CCB_CMD_HEADER *psHeader;
			IMG_UINT64 *pui64FBSCInvalCmdData;

			/* pui8CmdPtr */

			psHeader = pvCmdPtr;
			psHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_FBSC_INVALIDATE;

			psHeader->ui32CmdSize = psCmdHelperData->ui32FBSCInvalCmdSize - sizeof(RGXFWIF_CCB_CMD_HEADER);
			psHeader->ui32ExtJobRef = psCmdHelperData->ui32ExtJobRef;
			psHeader->ui32IntJobRef = psCmdHelperData->ui32IntJobRef;
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
			if (!PVRSRV_VZ_MODE_IS(GUEST))
			{
				psHeader->sWorkEstKickData.ui16ReturnDataIndex = 0;
				psHeader->sWorkEstKickData.ui64Deadline = 0;
				psHeader->sWorkEstKickData.ui32CyclesPrediction = 0;
			}
#endif
			pui64FBSCInvalCmdData = IMG_OFFSET_ADDR(psHeader, sizeof(RGXFWIF_CCB_CMD_HEADER));
			*pui64FBSCInvalCmdData = psCmdHelperData->ui64FBSCEntryMask;
			/* leap over the FBSC invalidate command */
			pvCmdPtr = IMG_OFFSET_ADDR(pvCmdPtr, psCmdHelperData->ui32FBSCInvalCmdSize);

		}

		/*
		  Create the pre DM timestamp commands. Pre and Post timestamp commands are supposed to
		  sandwich the DM cmd. The padding code with the CCB wrap upsets the FW if we don't have
		  the task type bit cleared for POST_TIMESTAMPs. That's why we have 2 different cmd types.
		*/
		if (psCmdHelperData->ui32PreTimeStampCmdSize != 0)
		{
			RGXWriteTimestampCommand(&pvCmdPtr,
			                         RGXFWIF_CCB_CMD_TYPE_PRE_TIMESTAMP,
			                         psCmdHelperData->pPreTimestampAddr);
		}

		/*
			Create the DM command
		*/
		if (psCmdHelperData->ui32DMCmdSize)
		{
			RGXFWIF_CCB_CMD_HEADER *psHeader;

			psHeader = pvCmdPtr;
			psHeader->eCmdType = psCmdHelperData->eType;

			psHeader->ui32CmdSize = psCmdHelperData->ui32DMCmdSize - sizeof(RGXFWIF_CCB_CMD_HEADER);
			psHeader->ui32ExtJobRef = psCmdHelperData->ui32ExtJobRef;
			psHeader->ui32IntJobRef = psCmdHelperData->ui32IntJobRef;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
			if (!PVRSRV_VZ_MODE_IS(GUEST))
			{
				if (psCmdHelperData->psWorkEstKickData != NULL &&
					RGXIsValidWorkloadEstCCBCommand(psCmdHelperData->eType))
				{
					psHeader->sWorkEstKickData = *psCmdHelperData->psWorkEstKickData;
				}
				else
				{
					psHeader->sWorkEstKickData.ui16ReturnDataIndex = 0;
					psHeader->sWorkEstKickData.ui64Deadline = 0;
					psHeader->sWorkEstKickData.ui32CyclesPrediction = 0;
				}
			}
#endif

			pvCmdPtr = IMG_OFFSET_ADDR(pvCmdPtr, sizeof(RGXFWIF_CCB_CMD_HEADER));

			/* The buffer is write-combine, so no special device memory treatment required. */
			OSCachedMemCopy(pvCmdPtr, psCmdHelperData->pui8DMCmd, psCmdHelperData->ui32CmdSize);
			pvCmdPtr = IMG_OFFSET_ADDR(pvCmdPtr, psCmdHelperData->ui32CmdSize);
		}


		if (psCmdHelperData->ui32PostTimeStampCmdSize != 0)
		{
			RGXWriteTimestampCommand(&pvCmdPtr,
			                         RGXFWIF_CCB_CMD_TYPE_POST_TIMESTAMP,
			                         psCmdHelperData->pPostTimestampAddr);
		}


		if (psCmdHelperData->ui32RMWUFOCmdSize != 0)
		{
			RGXFWIF_CCB_CMD_HEADER * psHeader;
			RGXFWIF_UFO            * psUFO;

			psHeader = (RGXFWIF_CCB_CMD_HEADER *) pvCmdPtr;
			psHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_RMW_UPDATE;
			psHeader->ui32CmdSize = psCmdHelperData->ui32RMWUFOCmdSize - sizeof(RGXFWIF_CCB_CMD_HEADER);
			psHeader->ui32ExtJobRef = psCmdHelperData->ui32ExtJobRef;
			psHeader->ui32IntJobRef = psCmdHelperData->ui32IntJobRef;
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
			if (!PVRSRV_VZ_MODE_IS(GUEST))
			{
				psHeader->sWorkEstKickData.ui16ReturnDataIndex = 0;
				psHeader->sWorkEstKickData.ui64Deadline = 0;
				psHeader->sWorkEstKickData.ui32CyclesPrediction = 0;
			}
#endif
			pvCmdPtr = IMG_OFFSET_ADDR(pvCmdPtr, sizeof(RGXFWIF_CCB_CMD_HEADER));

			psUFO = (RGXFWIF_UFO *) pvCmdPtr;
			psUFO->puiAddrUFO = psCmdHelperData->pRMWUFOAddr;

			pvCmdPtr = IMG_OFFSET_ADDR(pvCmdPtr, sizeof(RGXFWIF_UFO));
		}

		/*
			Create the update command.
		*/
		if (psCmdHelperData->ui32UpdateCmdSize)
		{
			RGXFWIF_CCB_CMD_HEADER *psHeader;
			IMG_UINT k, uiNextValueIndex;

			psHeader = pvCmdPtr;
			psHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_UPDATE;
			psHeader->ui32CmdSize = psCmdHelperData->ui32UpdateCmdSize - sizeof(RGXFWIF_CCB_CMD_HEADER);
			psHeader->ui32ExtJobRef = psCmdHelperData->ui32ExtJobRef;
			psHeader->ui32IntJobRef = psCmdHelperData->ui32IntJobRef;
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
			if (!PVRSRV_VZ_MODE_IS(GUEST))
			{
				psHeader->sWorkEstKickData.ui16ReturnDataIndex = 0;
				psHeader->sWorkEstKickData.ui64Deadline = 0;
				psHeader->sWorkEstKickData.ui32CyclesPrediction = 0;
			}
#endif
			pvCmdPtr = IMG_OFFSET_ADDR(pvCmdPtr, sizeof(RGXFWIF_CCB_CMD_HEADER));

			/* Fill in the client updates */
			uiNextValueIndex = 0;
			for (k = 0; k < psCmdHelperData->ui32ClientUpdateCount; k++)
			{
				RGXFWIF_UFO *psUFOPtr = pvCmdPtr;

				psUFOPtr->puiAddrUFO = psCmdHelperData->pauiUpdateUFOAddress[k];
				if (PVRSRV_UFO_IS_SYNC_CHECKPOINT(psUFOPtr))
				{
					psUFOPtr->ui32Value = PVRSRV_SYNC_CHECKPOINT_SIGNALLED;
				}
				else
				{
					/* Only increment uiNextValueIndex for non sync checkpoints
					 * (as paui32UpdateValue only contains values for sync prims)
					 */
					psUFOPtr->ui32Value = psCmdHelperData->paui32UpdateValue[uiNextValueIndex++];
				}
				pvCmdPtr = IMG_OFFSET_ADDR(pvCmdPtr, sizeof(RGXFWIF_UFO));

#if defined(SYNC_COMMAND_DEBUG)
				PVR_DPF((PVR_DBG_ERROR, "%s client sync update - 0x%x -> 0x%x",
						psCmdHelperData->psClientCCB->szName, psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value));
#endif
				PDUMPCOMMENT(psDevInfo->psDeviceNode,
							 ".. %s client sync update - 0x%x -> 0x%x",
							 psCmdHelperData->psClientCCB->szName,
							 psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value);

			}
		}

		/* Set the start pointer for the next iteration around the loop */
		pvStartPtr = IMG_OFFSET_ADDR(pvStartPtr,
			psCmdHelperData->ui32FenceCmdSize         +
			psCmdHelperData->ui32FBSCInvalCmdSize     +
			psCmdHelperData->ui32PreTimeStampCmdSize  +
			psCmdHelperData->ui32DMCmdSize            +
			psCmdHelperData->ui32PostTimeStampCmdSize +
			psCmdHelperData->ui32RMWUFOCmdSize        +
			psCmdHelperData->ui32UpdateCmdSize        );

		if (psCmdHelperData->ui32ClientFenceCount+psCmdHelperData->ui32ClientUpdateCount != 0)
		{
			PDUMPCOMMENT(psDevInfo->psDeviceNode,
						 "End of %s client syncs for cmd[%d] on FWCtx %08x Woff 0x%x bytes",
						 psCmdHelperData->psClientCCB->szName, i, ui32CtxAddr, ui32CcbWoff);
		}
		else
		{
			PDUMPCOMMENT(psDevInfo->psDeviceNode,
						 "No %s client syncs for cmd[%d] on FWCtx %08x Woff 0x%x bytes",
						 psCmdHelperData->psClientCCB->szName, i, ui32CtxAddr, ui32CcbWoff);
		}
	}

	return PVRSRV_OK;
}

/*
	Fill in the server syncs data and release the CCB space
*/
void RGXCmdHelperReleaseCmdCCB(IMG_UINT32 ui32CmdCount,
							   RGX_CCB_CMD_HELPER_DATA *asCmdHelperData,
							   const IMG_CHAR *pcszDMName,
							   IMG_UINT32 ui32CtxAddr)
{
	IMG_UINT32 ui32AllocSize = 0;
	IMG_UINT32 i;
#if defined(__linux__)
	IMG_BOOL bTraceChecks = trace_rogue_are_fence_checks_traced();
	IMG_BOOL bTraceUpdates = trace_rogue_are_fence_updates_traced();
#endif

	/*
		Work out how much space we need for all the command(s)
	*/
	ui32AllocSize = RGXCmdHelperGetCommandSize(ui32CmdCount, asCmdHelperData);
	/*
		For each command fill in the server sync info
	*/
	for (i=0;i<ui32CmdCount;i++)
	{
		RGX_CCB_CMD_HELPER_DATA *psCmdHelperData = &asCmdHelperData[i];
#if defined(PDUMP) || defined(__linux__)
		PVRSRV_RGXDEV_INFO *psDevInfo = FWCommonContextGetRGXDevInfo(psCmdHelperData->psClientCCB->psServerCommonContext);
#endif

#if (!defined(__linux__) || !defined(PDUMP))
		PVR_UNREFERENCED_PARAMETER(psCmdHelperData);
#endif

#if defined(__linux__)
		if (bTraceChecks)
		{
			trace_rogue_fence_checks(psCmdHelperData->pszCommandName,
									 pcszDMName,
									 psDevInfo->psDeviceNode->sDevId.ui32InternalID,
									 ui32CtxAddr,
									 psCmdHelperData->psClientCCB->ui32HostWriteOffset + ui32AllocSize,
									 psCmdHelperData->ui32ClientFenceCount,
									 psCmdHelperData->pauiFenceUFOAddress,
									 psCmdHelperData->paui32FenceValue);
		}
		if (bTraceUpdates)
		{
			trace_rogue_fence_updates(psCmdHelperData->pszCommandName,
									  pcszDMName,
									  psDevInfo->psDeviceNode->sDevId.ui32InternalID,
									  ui32CtxAddr,
									  psCmdHelperData->psClientCCB->ui32HostWriteOffset + ui32AllocSize,
									  psCmdHelperData->ui32ClientUpdateCount,
									  psCmdHelperData->pauiUpdateUFOAddress,
									  psCmdHelperData->paui32UpdateValue);
		}
#endif

		/*
			All the commands have been filled in so release the CCB space.
			The FW still won't run this command until we kick it
		*/
		PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode,
				psCmdHelperData->ui32PDumpFlags,
				"%s Command Server Release on FWCtx %08x",
				psCmdHelperData->pszCommandName, ui32CtxAddr);
	}

	RGXReleaseCCB(asCmdHelperData[0].psClientCCB,
				  ui32AllocSize,
				  asCmdHelperData[0].ui32PDumpFlags);

	BIT_UNSET(asCmdHelperData[0].psClientCCB->ui32CCBFlags, CCB_FLAGS_CCB_STATE_OPEN);
}

IMG_UINT32 RGXCmdHelperGetCommandSize(IMG_UINT32              ui32CmdCount,
                                      RGX_CCB_CMD_HELPER_DATA *asCmdHelperData)
{
	IMG_UINT32 ui32AllocSize = 0;
	IMG_UINT32 i;

	/*
		Work out how much space we need for all the command(s)
	*/
	for (i = 0; i < ui32CmdCount; i++)
	{
		ui32AllocSize +=
			asCmdHelperData[i].ui32FenceCmdSize          +
			asCmdHelperData[i].ui32FBSCInvalCmdSize      +
			asCmdHelperData[i].ui32DMCmdSize             +
			asCmdHelperData[i].ui32UpdateCmdSize         +
			asCmdHelperData[i].ui32PreTimeStampCmdSize   +
			asCmdHelperData[i].ui32PostTimeStampCmdSize  +
			asCmdHelperData[i].ui32RMWUFOCmdSize;
	}

	return ui32AllocSize;
}

/* Work out how much of an offset there is to a specific command. */
IMG_UINT32 RGXCmdHelperGetCommandOffset(RGX_CCB_CMD_HELPER_DATA *asCmdHelperData,
                                        IMG_UINT32              ui32Cmdindex)
{
	IMG_UINT32 ui32Offset = 0;
	IMG_UINT32 i;

	for (i = 0; i < ui32Cmdindex; i++)
	{
		ui32Offset +=
			asCmdHelperData[i].ui32FenceCmdSize          +
			asCmdHelperData[i].ui32FBSCInvalCmdSize      +
			asCmdHelperData[i].ui32DMCmdSize             +
			asCmdHelperData[i].ui32UpdateCmdSize         +
			asCmdHelperData[i].ui32PreTimeStampCmdSize   +
			asCmdHelperData[i].ui32PostTimeStampCmdSize  +
			asCmdHelperData[i].ui32RMWUFOCmdSize;
	}

	return ui32Offset;
}

/* Returns the offset of the data master command from a write offset */
IMG_UINT32 RGXCmdHelperGetDMCommandHeaderOffset(RGX_CCB_CMD_HELPER_DATA *psCmdHelperData)
{
	return psCmdHelperData->ui32FenceCmdSize +
		   psCmdHelperData->ui32PreTimeStampCmdSize +
		   psCmdHelperData->ui32FBSCInvalCmdSize;
}

static const char *_CCBCmdTypename(RGXFWIF_CCB_CMD_TYPE cmdType)
{
	switch (cmdType)
	{
		case RGXFWIF_CCB_CMD_TYPE_GEOM: return "TA";
		case RGXFWIF_CCB_CMD_TYPE_3D: return "3D";
		case RGXFWIF_CCB_CMD_TYPE_3D_PR: return "3D_PR";
		case RGXFWIF_CCB_CMD_TYPE_CDM: return "CDM";
		case RGXFWIF_CCB_CMD_TYPE_TQ_3D: return "TQ_3D";
		case RGXFWIF_CCB_CMD_TYPE_TQ_2D: return "TQ_2D";
		case RGXFWIF_CCB_CMD_TYPE_TQ_TDM: return "TQ_TDM";
		case RGXFWIF_CCB_CMD_TYPE_FBSC_INVALIDATE: return "FBSC_INVALIDATE";
		case RGXFWIF_CCB_CMD_TYPE_NULL: return "NULL";
		case RGXFWIF_CCB_CMD_TYPE_FENCE: return "FENCE";
		case RGXFWIF_CCB_CMD_TYPE_UPDATE: return "UPDATE";
		case RGXFWIF_CCB_CMD_TYPE_FENCE_PR: return "FENCE_PR";
		case RGXFWIF_CCB_CMD_TYPE_PRIORITY: return "PRIORITY";
		case RGXFWIF_CCB_CMD_TYPE_UNFENCED_UPDATE: return "UNFENCED_UPDATE";
		case RGXFWIF_CCB_CMD_TYPE_PRE_TIMESTAMP: return "PRE_TIMESTAMP";
		case RGXFWIF_CCB_CMD_TYPE_RMW_UPDATE: return "RMW_UPDATE";
		case RGXFWIF_CCB_CMD_TYPE_POST_TIMESTAMP: return "POST_TIMESTAMP";
		case RGXFWIF_CCB_CMD_TYPE_UNFENCED_RMW_UPDATE: return "UNFENCED_RMW_UPDATE";
		case RGXFWIF_CCB_CMD_TYPE_PADDING: return "PADDING";

		default:
			PVR_ASSERT(IMG_FALSE);
		break;
	}

	return "INVALID";
}

PVRSRV_ERROR CheckForStalledCCB(PVRSRV_DEVICE_NODE *psDevNode, RGX_CLIENT_CCB *psCurrentClientCCB, RGX_KICK_TYPE_DM eKickTypeDM)
{
	volatile RGXFWIF_CCCB_CTL	*psClientCCBCtrl;
	IMG_UINT32					ui32SampledRdOff, ui32SampledDpOff, ui32SampledWrOff, ui32WrapMask;
	PVRSRV_ERROR				eError = PVRSRV_OK;

	if (psCurrentClientCCB == NULL)
	{
		PVR_DPF((PVR_DBG_WARNING, "CheckForStalledCCB: CCCB is NULL"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

#if defined(PVRSRV_ENABLE_CCCB_GROW)
	/* If CCB grow is enabled, take the lock while sampling offsets
	 * (to guard against a grow happening mid-sample)
	 */
	OSLockAcquire(psCurrentClientCCB->hCCBGrowLock);
#endif
	/* NB. use psCurrentClientCCB->ui32Size as basis for wrap mask (rather than psClientCCBCtrl->ui32WrapMask)
	 * as if CCB grow happens, psCurrentClientCCB->ui32Size will have been updated but
	 * psClientCCBCtrl->ui32WrapMask is only updated once the firmware sees the CCB has grown.
	 * If we use the wrong value, we might incorrectly determine that the offsets are invalid.
	 */
	ui32WrapMask = RGXGetWrapMaskCCB(psCurrentClientCCB);
	psClientCCBCtrl = psCurrentClientCCB->psClientCCBCtrl;
	ui32SampledRdOff = psClientCCBCtrl->ui32ReadOffset;
	ui32SampledDpOff = psClientCCBCtrl->ui32DepOffset;
	ui32SampledWrOff = psCurrentClientCCB->ui32HostWriteOffset;
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	OSLockRelease(psCurrentClientCCB->hCCBGrowLock);
#endif

	if (ui32SampledRdOff > ui32WrapMask ||
		ui32SampledDpOff > ui32WrapMask ||
		ui32SampledWrOff > ui32WrapMask)
	{
		PVR_DPF((PVR_DBG_WARNING, "CheckForStalledCCB: CCCB has invalid offset (ROFF=%d DOFF=%d WOFF=%d)",
				ui32SampledRdOff, ui32SampledDpOff, ui32SampledWrOff));
		return PVRSRV_ERROR_INVALID_OFFSET;
	}

	if (ui32SampledRdOff != ui32SampledWrOff &&
				psCurrentClientCCB->ui32LastROff != psCurrentClientCCB->ui32LastWOff &&
				ui32SampledRdOff == psCurrentClientCCB->ui32LastROff &&
				(psCurrentClientCCB->ui32ByteCount - psCurrentClientCCB->ui32LastByteCount) < psCurrentClientCCB->ui32Size)
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO*)psDevNode->pvDevice;

		/* Only log a stalled CCB if GPU is idle (any state other than POW_ON is considered idle).
		 * Guest drivers do not initialize psRGXFWIfFwSysData, so they assume FW internal state is ON. */
		if (((psDevInfo->psRGXFWIfFwSysData == NULL) || (psDevInfo->psRGXFWIfFwSysData->ePowState != RGXFWIF_POW_ON)) &&
			(psDevInfo->ui32SLRHoldoffCounter == 0))
		{
			static __maybe_unused const char *pszStalledAction =
#if defined(PVRSRV_STALLED_CCB_ACTION)
					"force";
#else
					"warn";
#endif
			/* Don't log this by default unless debugging since a higher up
			 * function will log the stalled condition. Helps avoid double
			 * messages in the log.
			 */
			PVR_DPF((PVR_DBG_ERROR, "%s (%s): CCCB has not progressed (ROFF=%d DOFF=%d WOFF=%d) for \"%s\"",
					__func__, pszStalledAction, ui32SampledRdOff,
					ui32SampledDpOff, ui32SampledWrOff,
					psCurrentClientCCB->szName));
			eError = PVRSRV_ERROR_CCCB_STALLED;

			{
				void				*pvClientCCBBuff = psCurrentClientCCB->pvClientCCB;
				RGXFWIF_CCB_CMD_HEADER	*psCommandHeader = IMG_OFFSET_ADDR(pvClientCCBBuff, ui32SampledRdOff);
				PVRSRV_RGXDEV_INFO		*psDevInfo = FWCommonContextGetRGXDevInfo(psCurrentClientCCB->psServerCommonContext);

				/* Special case - if readOffset is on a PADDING packet, CCB has wrapped.
				 * In this case, skip over the PADDING packet.
				 */
				if (psCommandHeader->eCmdType == RGXFWIF_CCB_CMD_TYPE_PADDING)
				{
					psCommandHeader = IMG_OFFSET_ADDR(pvClientCCBBuff,
					                                             ((ui32SampledRdOff +
					                                               psCommandHeader->ui32CmdSize +
					                                               sizeof(RGXFWIF_CCB_CMD_HEADER))
					                                              & psCurrentClientCCB->psClientCCBCtrl->ui32WrapMask));
				}

				/* Only try to recover a 'stalled' context (ie one waiting on a fence), as some work (eg compute) could
				 * take a long time to complete, during which time the CCB ptrs would not advance.
				 */
				if (((psCommandHeader->eCmdType == RGXFWIF_CCB_CMD_TYPE_FENCE) ||
				     (psCommandHeader->eCmdType == RGXFWIF_CCB_CMD_TYPE_FENCE_PR)) &&
				    (psCommandHeader != IMG_OFFSET_ADDR(pvClientCCBBuff, ui32SampledWrOff)))
				{
					/* Acquire the cCCB recovery lock */
					OSLockAcquire(psDevInfo->hCCBRecoveryLock);

					if (!psDevInfo->pvEarliestStalledClientCCB)
					{
						psDevInfo->pvEarliestStalledClientCCB = (void*)psCurrentClientCCB;
						psDevInfo->ui32OldestSubmissionOrdinal = psCommandHeader->ui32IntJobRef;
					}
					else
					{
						/* Check if this fence cmd header has an older submission stamp than the one we are currently considering unblocking
						 * (account for submission stamp wrap by checking diff is less than 0x80000000) - if it is older, then this becomes
						 * our preferred fence to be unblocked/
						 */
						if ((psCommandHeader->ui32IntJobRef < psDevInfo->ui32OldestSubmissionOrdinal) &&
						    ((psDevInfo->ui32OldestSubmissionOrdinal - psCommandHeader->ui32IntJobRef) < 0x8000000))
						{
							psDevInfo->pvEarliestStalledClientCCB = (void*)psCurrentClientCCB;
							psDevInfo->ui32OldestSubmissionOrdinal = psCommandHeader->ui32IntJobRef;
						}
					}

					/* Release the cCCB recovery lock */
					OSLockRelease(psDevInfo->hCCBRecoveryLock);
				}
			}
		}
	}

	psCurrentClientCCB->ui32LastROff = ui32SampledRdOff;
	psCurrentClientCCB->ui32LastWOff = ui32SampledWrOff;
	psCurrentClientCCB->ui32LastByteCount = psCurrentClientCCB->ui32ByteCount;

	return eError;
}

void DumpCCB(PVRSRV_RGXDEV_INFO *psDevInfo,
			PRGXFWIF_FWCOMMONCONTEXT sFWCommonContext,
			RGX_CLIENT_CCB *psCurrentClientCCB,
			DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
			void *pvDumpDebugFile)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	volatile RGXFWIF_CCCB_CTL *psClientCCBCtrl;
	void *pvClientCCBBuff;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32DepOffset;
	IMG_UINT32 ui32EndOffset;
	IMG_UINT32 ui32WrapMask;
	IMG_CHAR * pszState = "Ready";

	/* Ensure hCCBGrowLock is acquired before reading
	 * psCurrentClientCCB->pvClientCCB as a CCB grow
	 * could remap the virtual addresses.
	 */
#if defined(PVRSRV_ENABLE_CCCB_GROW)
	OSLockAcquire(psCurrentClientCCB->hCCBGrowLock);
#endif
	psClientCCBCtrl = psCurrentClientCCB->psClientCCBCtrl;
	pvClientCCBBuff = psCurrentClientCCB->pvClientCCB;
	ui32EndOffset = psCurrentClientCCB->ui32HostWriteOffset;
	OSMemoryBarrier(NULL);
	ui32Offset = psClientCCBCtrl->ui32ReadOffset;
	ui32DepOffset = psClientCCBCtrl->ui32DepOffset;
	/* NB. Use psCurrentClientCCB->ui32Size as basis for wrap mask (rather
	 * than psClientCCBCtrl->ui32WrapMask) as if CCB grow happened,
	 * psCurrentClientCCB->ui32Size will have been updated but
	 * psClientCCBCtrl->ui32WrapMask is only updated once the firmware
	 * sees the CCB has grown. If we use the wrong value, ui32NextOffset
	 * can end up being wrapped prematurely and pointing to garbage.
	 */
	ui32WrapMask = RGXGetWrapMaskCCB(psCurrentClientCCB);

	PVR_DUMPDEBUG_LOG("FWCtx 0x%08X (%s)", sFWCommonContext.ui32Addr, psCurrentClientCCB->szName);
	if (ui32Offset == ui32EndOffset)
	{
		PVR_DUMPDEBUG_LOG("  `--<Empty>");
	}

	while (ui32Offset != ui32EndOffset)
	{
		RGXFWIF_CCB_CMD_HEADER *psCmdHeader = IMG_OFFSET_ADDR(pvClientCCBBuff, ui32Offset);
		IMG_UINT32 ui32NextOffset = (ui32Offset + psCmdHeader->ui32CmdSize + sizeof(RGXFWIF_CCB_CMD_HEADER)) & ui32WrapMask;
		IMG_BOOL bLastCommand = (ui32NextOffset == ui32EndOffset)? IMG_TRUE: IMG_FALSE;
		IMG_BOOL bLastUFO;
		#define CCB_SYNC_INFO_LEN 80
		IMG_CHAR pszSyncInfo[CCB_SYNC_INFO_LEN];
		IMG_UINT32 ui32NoOfUpdates, i;
		RGXFWIF_UFO *psUFOPtr;

		ui32NoOfUpdates = psCmdHeader->ui32CmdSize / sizeof(RGXFWIF_UFO);
		psUFOPtr = IMG_OFFSET_ADDR(pvClientCCBBuff, ui32Offset + sizeof(RGXFWIF_CCB_CMD_HEADER));
		pszSyncInfo[0] = '\0';

		if (ui32Offset == ui32DepOffset)
		{
			pszState = "Waiting";
		}

		PVR_DUMPDEBUG_LOG("  %s--%s %s @ %u Int=%u Ext=%u",
			bLastCommand? "`": "|",
			pszState, _CCBCmdTypename(psCmdHeader->eCmdType),
			ui32Offset, psCmdHeader->ui32IntJobRef, psCmdHeader->ui32ExtJobRef
			);

		/* switch on type and write checks and updates */
		switch (psCmdHeader->eCmdType)
		{
			case RGXFWIF_CCB_CMD_TYPE_UPDATE:
			case RGXFWIF_CCB_CMD_TYPE_UNFENCED_UPDATE:
			case RGXFWIF_CCB_CMD_TYPE_FENCE:
			case RGXFWIF_CCB_CMD_TYPE_FENCE_PR:
			{
				for (i = 0; i < ui32NoOfUpdates; i++, psUFOPtr++)
				{
					bLastUFO = (ui32NoOfUpdates-1 == i)? IMG_TRUE: IMG_FALSE;

					if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
					{
						if (PVRSRV_UFO_IS_SYNC_CHECKPOINT(psUFOPtr))
						{
							SyncCheckpointRecordLookup(psDeviceNode, psUFOPtr->puiAddrUFO.ui32Addr,
										pszSyncInfo, CCB_SYNC_INFO_LEN);
						}
						else
						{
							SyncRecordLookup(psDeviceNode, psUFOPtr->puiAddrUFO.ui32Addr,
										pszSyncInfo, CCB_SYNC_INFO_LEN);
						}
					}

					PVR_DUMPDEBUG_LOG("  %s  %s--Addr:0x%08x Val=0x%08x %s",
						bLastCommand? " ": "|",
						bLastUFO? "`": "|",
						psUFOPtr->puiAddrUFO.ui32Addr, psUFOPtr->ui32Value,
						pszSyncInfo
						);
				}
				break;
			}
			case RGXFWIF_CCB_CMD_TYPE_RMW_UPDATE:
			case RGXFWIF_CCB_CMD_TYPE_UNFENCED_RMW_UPDATE:
			{
				for (i = 0; i < ui32NoOfUpdates; i++, psUFOPtr++)
				{
					bLastUFO = (ui32NoOfUpdates-1 == i)? IMG_TRUE: IMG_FALSE;

					if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_FULL_SYNC_TRACKING_ENABLED)
					{
						if (PVRSRV_UFO_IS_SYNC_CHECKPOINT(psUFOPtr))
						{
							SyncCheckpointRecordLookup(psDeviceNode, psUFOPtr->puiAddrUFO.ui32Addr,
										pszSyncInfo, CCB_SYNC_INFO_LEN);
						}
						else
						{
							SyncRecordLookup(psDeviceNode, psUFOPtr->puiAddrUFO.ui32Addr,
										pszSyncInfo, CCB_SYNC_INFO_LEN);
						}
					}

					PVR_DUMPDEBUG_LOG("  %s  %s--Addr:0x%08x Val++ %s",
						bLastCommand? " ": "|",
						bLastUFO? "`": "|",
						psUFOPtr->puiAddrUFO.ui32Addr,
						pszSyncInfo
						);
				}
				break;
			}
			default:
				break;
		}
		ui32Offset = ui32NextOffset;
	}

#if defined(PVRSRV_ENABLE_CCCB_GROW)
	OSLockRelease(psCurrentClientCCB->hCCBGrowLock);
#endif
}

void DumpFirstCCBCmd(PRGXFWIF_FWCOMMONCONTEXT sFWCommonContext,
				RGX_CLIENT_CCB *psCurrentClientCCB,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	volatile RGXFWIF_CCCB_CTL	*psClientCCBCtrl = psCurrentClientCCB->psClientCCBCtrl;
	void					*pvClientCCBBuff = psCurrentClientCCB->pvClientCCB;
	IMG_UINT32					ui32SampledRdOff = psClientCCBCtrl->ui32ReadOffset;
	IMG_UINT32					ui32SampledDepOff = psClientCCBCtrl->ui32DepOffset;
	IMG_UINT32					ui32SampledWrOff = psCurrentClientCCB->ui32HostWriteOffset;

	if ((ui32SampledRdOff == ui32SampledDepOff) &&
		(ui32SampledRdOff != ui32SampledWrOff))
	{
		volatile RGXFWIF_CCB_CMD_HEADER *psCommandHeader = IMG_OFFSET_ADDR(pvClientCCBBuff, ui32SampledRdOff);
		RGXFWIF_CCB_CMD_TYPE	eCommandType = psCommandHeader->eCmdType;
		volatile void				*pvPtr = psCommandHeader;

		/* CCB is stalled on a fence... */
		if ((eCommandType == RGXFWIF_CCB_CMD_TYPE_FENCE) || (eCommandType == RGXFWIF_CCB_CMD_TYPE_FENCE_PR))
		{
#if defined(SUPPORT_FW_VIEW_EXTRA_DEBUG)
			PVRSRV_RGXDEV_INFO *psDevInfo = FWCommonContextGetRGXDevInfo(psCurrentClientCCB->psServerCommonContext);
			IMG_UINT32 ui32Val;
#endif
			RGXFWIF_UFO *psUFOPtr = IMG_OFFSET_ADDR(pvPtr, sizeof(*psCommandHeader));
			IMG_UINT32 jj;

			/* Display details of the fence object on which the context is pending */
			PVR_DUMPDEBUG_LOG("FWCtx 0x%08X @ %d (%s) pending on %s:",
							   sFWCommonContext.ui32Addr,
							   ui32SampledRdOff,
							   psCurrentClientCCB->szName,
							   _CCBCmdTypename(eCommandType));
			for (jj=0; jj<psCommandHeader->ui32CmdSize/sizeof(RGXFWIF_UFO); jj++)
			{
#if !defined(SUPPORT_FW_VIEW_EXTRA_DEBUG)
				PVR_DUMPDEBUG_LOG("  Addr:0x%08x  Value=0x%08x",psUFOPtr[jj].puiAddrUFO.ui32Addr, psUFOPtr[jj].ui32Value);
#else
				ui32Val = 0;
				RGXReadFWModuleAddr(psDevInfo, psUFOPtr[jj].puiAddrUFO.ui32Addr, &ui32Val);
				PVR_DUMPDEBUG_LOG("  Addr:0x%08x Value(Host)=0x%08x Value(FW)=0x%08x",
				                   psUFOPtr[jj].puiAddrUFO.ui32Addr,
				                   psUFOPtr[jj].ui32Value, ui32Val);
#endif
			}

			/* Advance psCommandHeader past the FENCE to the next command header (this will be the TA/3D command that is fenced) */
			pvPtr = IMG_OFFSET_ADDR(psUFOPtr, psCommandHeader->ui32CmdSize);
			psCommandHeader = pvPtr;
			if (psCommandHeader != IMG_OFFSET_ADDR(pvClientCCBBuff, ui32SampledWrOff))
			{
				PVR_DUMPDEBUG_LOG(" FWCtx 0x%08X fenced command is of type %s",sFWCommonContext.ui32Addr, _CCBCmdTypename(psCommandHeader->eCmdType));
				/* Advance psCommandHeader past the TA/3D to the next command header (this will possibly be an UPDATE) */
				pvPtr = IMG_OFFSET_ADDR(pvPtr, sizeof(*psCommandHeader) + psCommandHeader->ui32CmdSize);
				psCommandHeader = pvPtr;
				/* If the next command is an update, display details of that so we can see what would then become unblocked */
				if (psCommandHeader != IMG_OFFSET_ADDR(pvClientCCBBuff, ui32SampledWrOff))
				{
					eCommandType = psCommandHeader->eCmdType;

					if (eCommandType == RGXFWIF_CCB_CMD_TYPE_UPDATE)
					{
						psUFOPtr = IMG_OFFSET_ADDR(psCommandHeader, sizeof(*psCommandHeader));
						PVR_DUMPDEBUG_LOG(" preventing %s:",_CCBCmdTypename(eCommandType));
						for (jj=0; jj<psCommandHeader->ui32CmdSize/sizeof(RGXFWIF_UFO); jj++)
						{
#if !defined(SUPPORT_FW_VIEW_EXTRA_DEBUG)
							PVR_DUMPDEBUG_LOG("  Addr:0x%08x  Value=0x%08x",psUFOPtr[jj].puiAddrUFO.ui32Addr, psUFOPtr[jj].ui32Value);
#else
							ui32Val = 0;
							RGXReadFWModuleAddr(psDevInfo, psUFOPtr[jj].puiAddrUFO.ui32Addr, &ui32Val);
							PVR_DUMPDEBUG_LOG("  Addr:0x%08x Value(Host)=0x%08x Value(FW)=0x%08x",
							                   psUFOPtr[jj].puiAddrUFO.ui32Addr,
							                   psUFOPtr[jj].ui32Value,
							                   ui32Val);
#endif
						}
					}
				}
				else
				{
					PVR_DUMPDEBUG_LOG(" FWCtx 0x%08X has no further commands",sFWCommonContext.ui32Addr);
				}
			}
			else
			{
				PVR_DUMPDEBUG_LOG(" FWCtx 0x%08X has no further commands",sFWCommonContext.ui32Addr);
			}
		}
	}
}

void DumpStalledContextInfo(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGX_CLIENT_CCB *psStalledClientCCB;

	PVR_ASSERT(psDevInfo);

	psStalledClientCCB = (RGX_CLIENT_CCB *)psDevInfo->pvEarliestStalledClientCCB;

	if (psStalledClientCCB)
	{
		volatile RGXFWIF_CCCB_CTL *psClientCCBCtrl = psStalledClientCCB->psClientCCBCtrl;
		IMG_UINT32 ui32SampledDepOffset = psClientCCBCtrl->ui32DepOffset;
		void                 *pvPtr = IMG_OFFSET_ADDR(psStalledClientCCB->pvClientCCB, ui32SampledDepOffset);
		RGXFWIF_CCB_CMD_HEADER    *psCommandHeader = pvPtr;
		RGXFWIF_CCB_CMD_TYPE      eCommandType = psCommandHeader->eCmdType;

		if ((eCommandType == RGXFWIF_CCB_CMD_TYPE_FENCE) || (eCommandType == RGXFWIF_CCB_CMD_TYPE_FENCE_PR))
		{
			RGXFWIF_UFO *psUFOPtr = IMG_OFFSET_ADDR(pvPtr, sizeof(*psCommandHeader));
			IMG_UINT32 jj;
			IMG_UINT32 ui32NumUnsignalledUFOs = 0;
			IMG_UINT32 ui32UnsignalledUFOVaddrs[PVRSRV_MAX_SYNCS];

#if defined(PVRSRV_STALLED_CCB_ACTION)
			if (!psDevInfo->psRGXFWIfFwOsData->sSLRLogFirst.aszCCBName[0])
			{
				OSClockMonotonicns64(&psDevInfo->psRGXFWIfFwOsData->sSLRLogFirst.ui64Timestamp);
				psDevInfo->psRGXFWIfFwOsData->sSLRLogFirst.ui32NumUFOs = (IMG_UINT32)(psCommandHeader->ui32CmdSize/sizeof(RGXFWIF_UFO));
				psDevInfo->psRGXFWIfFwOsData->sSLRLogFirst.ui32FWCtxAddr = FWCommonContextGetFWAddress(psStalledClientCCB->psServerCommonContext).ui32Addr;
				OSStringLCopy(psDevInfo->psRGXFWIfFwOsData->sSLRLogFirst.aszCCBName,
				              psStalledClientCCB->szName,
				              MAX_CLIENT_CCB_NAME);
			}
			else
			{
				OSClockMonotonicns64(&psDevInfo->psRGXFWIfFwOsData->sSLRLog[psDevInfo->psRGXFWIfFwOsData->ui8SLRLogWp].ui64Timestamp);
				psDevInfo->psRGXFWIfFwOsData->sSLRLog[psDevInfo->psRGXFWIfFwOsData->ui8SLRLogWp].ui32NumUFOs = (IMG_UINT32)(psCommandHeader->ui32CmdSize/sizeof(RGXFWIF_UFO));
				psDevInfo->psRGXFWIfFwOsData->sSLRLog[psDevInfo->psRGXFWIfFwOsData->ui8SLRLogWp].ui32FWCtxAddr = FWCommonContextGetFWAddress(psStalledClientCCB->psServerCommonContext).ui32Addr;
				OSStringLCopy(psDevInfo->psRGXFWIfFwOsData->sSLRLog[psDevInfo->psRGXFWIfFwOsData->ui8SLRLogWp].aszCCBName,
				              psStalledClientCCB->szName,
				              MAX_CLIENT_CCB_NAME);
				psDevInfo->psRGXFWIfFwOsData->ui8SLRLogWp = (psDevInfo->psRGXFWIfFwOsData->ui8SLRLogWp + 1) % PVR_SLR_LOG_ENTRIES;
			}
			psDevInfo->psRGXFWIfFwOsData->ui32ForcedUpdatesRequested++;
			/* flush write buffers for psRGXFWIfFwOsData */
			OSWriteMemoryBarrier(&psDevInfo->psRGXFWIfFwOsData->sSLRLog[psDevInfo->psRGXFWIfFwOsData->ui8SLRLogWp]);
#endif
			PVR_LOG(("Fence found on context 0x%x '%s' @ %d has %d UFOs",
			         FWCommonContextGetFWAddress(psStalledClientCCB->psServerCommonContext).ui32Addr,
			         psStalledClientCCB->szName, ui32SampledDepOffset,
			         (IMG_UINT32)(psCommandHeader->ui32CmdSize/sizeof(RGXFWIF_UFO))));

			for (jj=0; jj<psCommandHeader->ui32CmdSize/sizeof(RGXFWIF_UFO); jj++)
			{
				if (PVRSRV_UFO_IS_SYNC_CHECKPOINT((RGXFWIF_UFO *)&psUFOPtr[jj]))
				{
					IMG_UINT32 ui32ReadValue = SyncCheckpointStateFromUFO(psDevInfo->psDeviceNode,
					                                           psUFOPtr[jj].puiAddrUFO.ui32Addr);
					PVR_LOG(("  %d/%d FWAddr 0x%x requires 0x%x (currently 0x%x)", jj+1,
							   (IMG_UINT32)(psCommandHeader->ui32CmdSize/sizeof(RGXFWIF_UFO)),
							   psUFOPtr[jj].puiAddrUFO.ui32Addr,
							   psUFOPtr[jj].ui32Value,
							   ui32ReadValue));
					/* If fence is unmet, dump debug info on it */
					if (ui32ReadValue != psUFOPtr[jj].ui32Value)
					{
						/* Add to our list to pass to pvr_sync */
						ui32UnsignalledUFOVaddrs[ui32NumUnsignalledUFOs] = psUFOPtr[jj].puiAddrUFO.ui32Addr;
						ui32NumUnsignalledUFOs++;
					}
				}
				else
				{
					PVR_LOG(("  %d/%d FWAddr 0x%x requires 0x%x", jj+1,
							   (IMG_UINT32)(psCommandHeader->ui32CmdSize/sizeof(RGXFWIF_UFO)),
							   psUFOPtr[jj].puiAddrUFO.ui32Addr,
							   psUFOPtr[jj].ui32Value));
				}
			}
#if defined(SUPPORT_NATIVE_FENCE_SYNC) || defined(SUPPORT_FALLBACK_FENCE_SYNC)
			if (ui32NumUnsignalledUFOs > 0)
			{
				IMG_UINT32 ui32NumSyncsOwned;
				PVRSRV_ERROR eErr = SyncCheckpointDumpInfoOnStalledUFOs(ui32NumUnsignalledUFOs, &ui32UnsignalledUFOVaddrs[0], &ui32NumSyncsOwned);

				PVR_LOG_IF_ERROR(eErr, "SyncCheckpointDumpInfoOnStalledUFOs() call failed.");
			}
#endif
#if defined(PVRSRV_STALLED_CCB_ACTION)
			if (BIT_ISSET(psStalledClientCCB->ui32CCBFlags, CCB_FLAGS_SLR_DISABLED))
			{
				PRGXFWIF_FWCOMMONCONTEXT psContext = FWCommonContextGetFWAddress(psStalledClientCCB->psServerCommonContext);

				PVR_LOG(("SLR disabled for FWCtx 0x%08X", psContext.ui32Addr));
			}
			else
			{
				if (ui32NumUnsignalledUFOs > 0)
				{
					RGXFWIF_KCCB_CMD sSignalFencesCmd;

					sSignalFencesCmd.eCmdType = RGXFWIF_KCCB_CMD_FORCE_UPDATE;
					sSignalFencesCmd.ui32KCCBFlags = 0;
					sSignalFencesCmd.uCmdData.sForceUpdateData.psContext = FWCommonContextGetFWAddress(psStalledClientCCB->psServerCommonContext);
					sSignalFencesCmd.uCmdData.sForceUpdateData.ui32CCBFenceOffset = ui32SampledDepOffset;

					PVR_LOG(("Forced update command issued for FWCtx 0x%08X", sSignalFencesCmd.uCmdData.sForceUpdateData.psContext.ui32Addr));

					RGXScheduleCommand(FWCommonContextGetRGXDevInfo(psStalledClientCCB->psServerCommonContext),
					                   RGXFWIF_DM_GP,
					                   &sSignalFencesCmd,
					                   PDUMP_FLAGS_CONTINUOUS);
				}
			}
#endif
		}
		psDevInfo->pvEarliestStalledClientCCB = NULL;
	}
}

/******************************************************************************
 End of file (rgxccb.c)
******************************************************************************/
