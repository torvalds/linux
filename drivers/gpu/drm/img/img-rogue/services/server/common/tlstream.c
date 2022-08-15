/*************************************************************************/ /*!
@File
@Title          Transport Layer kernel side API implementation.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Transport Layer API implementation.
                These functions are provided to driver components.
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

//#define PVR_DPF_FUNCTION_TRACE_ON 1
#undef PVR_DPF_FUNCTION_TRACE_ON
#include "pvr_debug.h"

#include "allocmem.h"
#include "devicemem.h"
#include "pvrsrv_error.h"
#include "osfunc.h"
#include "log2.h"

#include "tlintern.h"
#include "tlstream.h"

#include "pvrsrv.h"

#define EVENT_OBJECT_TIMEOUT_US 1000000ULL
#define READ_PENDING_TIMEOUT_US 100000ULL

/*! Compute maximum TL packet size for this stream. Max packet size will be
 * minimum of PVRSRVTL_MAX_PACKET_SIZE and (BufferSize / 2.5). This computation
 * is required to avoid a corner case that was observed when TL buffer size is
 * smaller than twice of TL max packet size and read, write index are positioned
 * in such a way that the TL packet (write packet + padding packet) size is may
 * be bigger than the buffer size itself.
 */
#define GET_TL_MAX_PACKET_SIZE( bufSize ) PVRSRVTL_ALIGN( MIN( PVRSRVTL_MAX_PACKET_SIZE, ( 2 * bufSize ) / 5 ) )

/* Given the state of the buffer it returns a number of bytes that the client
 * can use for a successful allocation. */
static INLINE IMG_UINT32 suggestAllocSize(IMG_UINT32 ui32LRead,
                                          IMG_UINT32 ui32LWrite,
                                          IMG_UINT32 ui32CBSize,
                                          IMG_UINT32 ui32ReqSizeMin,
                                          IMG_UINT32 ui32MaxPacketSize)
{
	IMG_UINT32 ui32AvSpace = 0;

	/* This could be written in fewer lines using the ? operator but it
		would not be kind to potential readers of this source at all. */
	if (ui32LRead > ui32LWrite)                          /* Buffer WRAPPED */
	{
		if ((ui32LRead - ui32LWrite) > (sizeof(PVRSRVTL_PACKETHDR) + ui32ReqSizeMin + (IMG_INT) BUFFER_RESERVED_SPACE))
		{
			ui32AvSpace = ui32LRead - ui32LWrite - sizeof(PVRSRVTL_PACKETHDR) - (IMG_INT) BUFFER_RESERVED_SPACE;
		}
	}
	else                                                  /* Normal, no wrap */
	{
		if ((ui32CBSize - ui32LWrite) > (sizeof(PVRSRVTL_PACKETHDR) + ui32ReqSizeMin + (IMG_INT) BUFFER_RESERVED_SPACE))
		{
			ui32AvSpace = ui32CBSize - ui32LWrite - sizeof(PVRSRVTL_PACKETHDR) - (IMG_INT) BUFFER_RESERVED_SPACE;
		}
		else if ((ui32LRead - 0) > (sizeof(PVRSRVTL_PACKETHDR) + ui32ReqSizeMin + (IMG_INT) BUFFER_RESERVED_SPACE))
		{
			ui32AvSpace = ui32LRead - sizeof(PVRSRVTL_PACKETHDR) - (IMG_INT) BUFFER_RESERVED_SPACE;
		}
	}
	/* The max size of a TL packet currently is UINT16. adjust accordingly */
	return MIN(ui32AvSpace, ui32MaxPacketSize);
}

/* Returns bytes left in the buffer. Negative if there is not any.
 * two 8b aligned values are reserved, one for the write failed buffer flag
 * and one to be able to distinguish the buffer full state to the buffer
 * empty state.
 * Always returns free space -8 even when the "write failed" packet may be
 * already in the stream before this write. */
static INLINE IMG_INT
circbufSpaceLeft(IMG_UINT32 ui32Read, IMG_UINT32 ui32Write, IMG_UINT32 ui32size)
{
	/* We need to reserve 8b (one packet) in the buffer to be able to tell empty
	 * buffers from full buffers and one more for packet write fail packet */
	if (ui32Read > ui32Write)
	{
		return (IMG_INT)ui32Read - (IMG_INT)ui32Write - (IMG_INT)BUFFER_RESERVED_SPACE;
	}
	else
	{
		return (IMG_INT)ui32size - ((IMG_INT)ui32Write - (IMG_INT)ui32Read) - (IMG_INT)BUFFER_RESERVED_SPACE;
	}
}

IMG_UINT32 TLStreamGetUT(IMG_HANDLE hStream)
{
	PTL_STREAM psStream = (PTL_STREAM) hStream;
	IMG_UINT32 ui32LRead = psStream->ui32Read, ui32LWrite = psStream->ui32Write;

	if (ui32LWrite >= ui32LRead)
	{
		return (ui32LWrite-ui32LRead);
	}
	else
	{
		return (psStream->ui32Size-ui32LRead+ui32LWrite);
	}
}

PVRSRV_ERROR TLAllocSharedMemIfNull(IMG_HANDLE hStream)
{
	PTL_STREAM psStream = (PTL_STREAM) hStream;
	PVRSRV_ERROR eError;

	/* CPU Local memory used as these buffers are not accessed by the device.
	 * CPU Uncached write combine memory used to improve write performance,
	 * memory barrier added in TLStreamCommit to ensure data written to memory
	 * before CB write point is updated before consumption by the reader.
	 */
	IMG_CHAR pszBufferLabel[PRVSRVTL_MAX_STREAM_NAME_SIZE + 20];
	PVRSRV_MEMALLOCFLAGS_T uiMemFlags = PVRSRV_MEMALLOCFLAG_CPU_READABLE |
	                                    PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
	                                    PVRSRV_MEMALLOCFLAG_GPU_READABLE |
	                                    PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC |
	                                    PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
	                                    PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(CPU_LOCAL); /* TL for now is only used by host driver, so cpulocal mem suffices */

	/* Exit if memory has already been allocated. */
	if (psStream->pbyBuffer != NULL)
		return PVRSRV_OK;

	OSSNPrintf(pszBufferLabel, sizeof(pszBufferLabel), "TLStreamBuf-%s",
	           psStream->szName);


	/* Use HostMemDeviceNode instead of psStream->psDevNode to benefit from faster
	 * accesses to CPU local memory. When the framework to access CPU_LOCAL device
	 * memory from GPU is fixed, we'll switch back to use psStream->psDevNode for
	 * TL buffers */
	eError = DevmemAllocateExportable((IMG_HANDLE)PVRSRVGetPVRSRVData()->psHostMemDeviceNode,
	                                  (IMG_DEVMEM_SIZE_T) psStream->ui32Size,
	                                  (IMG_DEVMEM_ALIGN_T) OSGetPageSize(),
	                                  ExactLog2(OSGetPageSize()),
	                                  uiMemFlags,
	                                  pszBufferLabel,
	                                  &psStream->psStreamMemDesc);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAllocateExportable", e0);

	eError = DevmemAcquireCpuVirtAddr(psStream->psStreamMemDesc,
	                                  (void**) &psStream->pbyBuffer);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", e1);

	return PVRSRV_OK;

e1:
	DevmemFree(psStream->psStreamMemDesc);
e0:
	return eError;
}

void TLFreeSharedMem(IMG_HANDLE hStream)
{
	PTL_STREAM psStream = (PTL_STREAM) hStream;

	if (psStream->pbyBuffer != NULL)
	{
		DevmemReleaseCpuVirtAddr(psStream->psStreamMemDesc);
		psStream->pbyBuffer = NULL;
	}
	if (psStream->psStreamMemDesc != NULL)
	{
		DevmemFree(psStream->psStreamMemDesc);
		psStream->psStreamMemDesc = NULL;
	}
}

/* Special space left routine for TL_FLAG_PERMANENT_NO_WRAP streams */
static INLINE IMG_UINT
bufSpaceLeft(IMG_UINT32 ui32Read, IMG_UINT32 ui32Write, IMG_UINT32 ui32size)
{
	 /* buffers from full buffers and one more for packet write fail packet */
	PVR_ASSERT(ui32Read<=ui32Write);
	return ui32size - ui32Write;
}

/*******************************************************************************
 * TL Server public API implementation.
 ******************************************************************************/
PVRSRV_ERROR
TLStreamCreate(IMG_HANDLE *phStream,
               const IMG_CHAR *szStreamName,
               IMG_UINT32 ui32Size,
               IMG_UINT32 ui32StreamFlags,
               TL_STREAM_ONREADEROPENCB pfOnReaderOpenCB,
               void *pvOnReaderOpenUD,
               TL_STREAM_SOURCECB pfProducerCB,
               void *pvProducerUD)
{
	PTL_STREAM    psTmp;
	PVRSRV_ERROR  eError;
	IMG_HANDLE    hEventList;
	PTL_SNODE     psn;
	TL_OPMODE     eOpMode;

	PVR_DPF_ENTERED;
	/* Parameter checks: non NULL handler required */
	if (NULL == phStream)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	if (szStreamName == NULL || *szStreamName == '\0' ||
	    OSStringLength(szStreamName) >= PRVSRVTL_MAX_STREAM_NAME_SIZE)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	eOpMode = ui32StreamFlags & TL_OPMODE_MASK;
	if (( eOpMode <= TL_OPMODE_UNDEF ) || ( eOpMode >= TL_OPMODE_LAST ))
	{
		PVR_DPF((PVR_DBG_ERROR, "OpMode for TL stream is invalid"));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	/* Acquire TL_GLOBAL_DATA lock here because, if the following TLFindStreamNodeByName()
	 * returns NULL, a new TL_SNODE will be added to TL_GLOBAL_DATA's TL_SNODE list */
	OSLockAcquire (TLGGD()->hTLGDLock);

	/* Check if there already exists a stream with this name. */
	psn = TLFindStreamNodeByName( szStreamName );
	if (NULL != psn)
	{
		eError = PVRSRV_ERROR_ALREADY_EXISTS;
		goto e0;
	}

	/* Allocate stream structure container (stream struct) for the new stream */
	psTmp = OSAllocZMem(sizeof(TL_STREAM));
	if (NULL == psTmp)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	OSStringLCopy(psTmp->szName, szStreamName, PRVSRVTL_MAX_STREAM_NAME_SIZE);

	if (ui32StreamFlags & TL_FLAG_FORCE_FLUSH)
	{
		psTmp->bWaitForEmptyOnDestroy = IMG_TRUE;
	}

	psTmp->bNoSignalOnCommit = (ui32StreamFlags&TL_FLAG_NO_SIGNAL_ON_COMMIT) ? IMG_TRUE : IMG_FALSE;
	psTmp->bNoWrapPermanent = (ui32StreamFlags&TL_FLAG_PERMANENT_NO_WRAP) ? IMG_TRUE : IMG_FALSE;

	psTmp->eOpMode = eOpMode;
	if (psTmp->eOpMode == TL_OPMODE_BLOCK)
	{
		/* Only allow drop properties to be mixed with no-wrap type streams
		 * since space does not become available when reads take place hence
		 * no point blocking.
		 */
		if (psTmp->bNoWrapPermanent)
		{
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto e1;
		}
	}

	/* Additional synchronisation object required for some streams e.g. blocking */
	eError = OSEventObjectCreate(NULL, &psTmp->hProducerEventObj);
	PVR_GOTO_IF_ERROR(eError, e1);
	/* Create an event handle for this kind of stream */
	eError = OSEventObjectOpen(psTmp->hProducerEventObj, &psTmp->hProducerEvent);
	PVR_GOTO_IF_ERROR(eError, e2);

	psTmp->pfOnReaderOpenCallback = pfOnReaderOpenCB;
	psTmp->pvOnReaderOpenUserData = pvOnReaderOpenUD;
	/* Remember producer supplied CB and data for later */
	psTmp->pfProducerCallback = (void(*)(void))pfProducerCB;
	psTmp->pvProducerUserData = pvProducerUD;

	psTmp->psNotifStream = NULL;

	/* Round the requested bytes to a multiple of array elements' size, eg round 3 to 4 */
	psTmp->ui32Size = PVRSRVTL_ALIGN(ui32Size);

	/* Signalling from TLStreamCommit is deferred until buffer is slightly (~12%) filled */
	psTmp->ui32ThresholdUsageForSignal = psTmp->ui32Size >> 3;
	psTmp->ui32MaxPacketSize = GET_TL_MAX_PACKET_SIZE(psTmp->ui32Size);
	psTmp->ui32Read = 0;
	psTmp->ui32Write = 0;
	psTmp->ui32Pending = NOTHING_PENDING;
	psTmp->bReadPending = IMG_FALSE;
	psTmp->bSignalPending = IMG_FALSE;

#if defined(TL_BUFFER_STATS)
	OSAtomicWrite(&psTmp->bNoReaderSinceFirstReserve, 0);
	/* Setting MAX possible value for "minimum" time to full,
	 * helps in the logic which calculates this time */
	psTmp->ui32MinTimeToFullInUs = IMG_UINT32_MAX;
#endif

	/* Memory will be allocated on first connect to the stream */
	if (!(ui32StreamFlags & TL_FLAG_ALLOCATE_ON_FIRST_OPEN))
	{
		/* Allocate memory for the circular buffer and export it to user space. */
		eError = TLAllocSharedMemIfNull(psTmp);
		PVR_LOG_GOTO_IF_ERROR(eError, "TLAllocSharedMem", e3);
	}

	/* Synchronisation object to synchronise with user side data transfers. */
	eError = OSEventObjectCreate(psTmp->szName, &hEventList);
	PVR_GOTO_IF_ERROR(eError, e4);

	eError = OSLockCreate (&psTmp->hStreamWLock);
	PVR_GOTO_IF_ERROR(eError, e5);

	eError = OSLockCreate (&psTmp->hReadLock);
	PVR_GOTO_IF_ERROR(eError, e6);

	/* Now remember the stream in the global TL structures */
	psn = TLMakeSNode(hEventList, (TL_STREAM *)psTmp, NULL);
	PVR_GOTO_IF_NOMEM(psn, eError, e7);

	/* Stream node created, now reset the write reference count to 1
	 * (i.e. this context's reference) */
	psn->uiWRefCount = 1;

	TLAddStreamNode(psn);

	/* Release TL_GLOBAL_DATA lock as the new TL_SNODE is now added to the list */
	OSLockRelease (TLGGD()->hTLGDLock);

	/* Best effort signal, client wait timeout will ultimately let it find the
	 * new stream if this fails, acceptable to avoid clean-up as it is tricky
	 * at this point */
	(void) OSEventObjectSignal(TLGGD()->hTLEventObj);

	/* Pass the newly created stream handle back to caller */
	*phStream = (IMG_HANDLE)psTmp;
	PVR_DPF_RETURN_OK;

e7:
	OSLockDestroy(psTmp->hReadLock);
e6:
	OSLockDestroy(psTmp->hStreamWLock);
e5:
	OSEventObjectDestroy(hEventList);
e4:
	TLFreeSharedMem(psTmp);
e3:
	OSEventObjectClose(psTmp->hProducerEvent);
e2:
	OSEventObjectDestroy(psTmp->hProducerEventObj);
e1:
	OSFreeMem(psTmp);
e0:
	OSLockRelease (TLGGD()->hTLGDLock);

	PVR_DPF_RETURN_RC(eError);
}

void TLStreamReset(IMG_HANDLE hStream)
{
	PTL_STREAM psStream = (PTL_STREAM) hStream;

	PVR_ASSERT(psStream != NULL);

	OSLockAcquire(psStream->hStreamWLock);

	while (psStream->ui32Pending != NOTHING_PENDING)
	{
		PVRSRV_ERROR eError;

		/* We're in the middle of a write so we cannot reset the stream.
		 * We are going to wait until the data is committed. Release lock while
		 * we're here. */
		OSLockRelease(psStream->hStreamWLock);

		/* Event when psStream->bNoSignalOnCommit is set we can still use
		 * the timeout capability of event object API (time in us). */
		eError = OSEventObjectWaitTimeout(psStream->psNode->hReadEventObj, 100);
		if (eError != PVRSRV_ERROR_TIMEOUT && eError != PVRSRV_OK)
		{
			PVR_LOG_RETURN_VOID_IF_ERROR(eError, "OSEventObjectWaitTimeout");
		}

		OSLockAcquire(psStream->hStreamWLock);

		/* Either timeout occurred or the stream has been signalled.
		 * If former we have to check if the data was committed and if latter
		 * if the stream hasn't been re-reserved. Either way we have to go
		 * back to the condition.
		 * If the stream has been released we'll exit with the lock held so
		 * we can finally go and reset the stream. */
	}

	psStream->ui32Read = 0;
	psStream->ui32Write = 0;
	/* we know that ui32Pending already has correct value (no need to set) */

	OSLockRelease(psStream->hStreamWLock);
}

PVRSRV_ERROR
TLStreamSetNotifStream(IMG_HANDLE hStream, IMG_HANDLE hNotifStream)
{
	PTL_STREAM psStream = (PTL_STREAM) hStream;

	if (hStream == NULL || hNotifStream == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	psStream->psNotifStream = (PTL_STREAM) hNotifStream;

	return PVRSRV_OK;
}

PVRSRV_ERROR
TLStreamReconfigure(
		IMG_HANDLE hStream,
		IMG_UINT32 ui32StreamFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PTL_STREAM psTmp;
	TL_OPMODE eOpMode;

	PVR_DPF_ENTERED;

	if (NULL == hStream)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	eOpMode = ui32StreamFlags & TL_OPMODE_MASK;
	if (( eOpMode <= TL_OPMODE_UNDEF ) || ( eOpMode >= TL_OPMODE_LAST ))
	{
		PVR_DPF((PVR_DBG_ERROR, "OpMode for TL stream is invalid"));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	psTmp = (PTL_STREAM)hStream;

	/* Prevent the TL Stream buffer from being written to
	 * while its mode is being reconfigured
	 */
	OSLockAcquire (psTmp->hStreamWLock);
	if (NOTHING_PENDING != psTmp->ui32Pending)
	{
		OSLockRelease (psTmp->hStreamWLock);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_READY);
	}
	psTmp->ui32Pending = 0;
	OSLockRelease (psTmp->hStreamWLock);

	psTmp->eOpMode = eOpMode;
	if (psTmp->eOpMode == TL_OPMODE_BLOCK)
	{
		/* Only allow drop properties to be mixed with no-wrap type streams
		 * since space does not become available when reads take place hence
		 * no point blocking.
		 */
		if (psTmp->bNoWrapPermanent)
		{
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto e1;
		}
	}

	OSLockAcquire (psTmp->hStreamWLock);
	psTmp->ui32Pending = NOTHING_PENDING;
	OSLockRelease (psTmp->hStreamWLock);
e1:
	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR
TLStreamOpen(IMG_HANDLE     *phStream,
             const IMG_CHAR *szStreamName)
{
	PTL_SNODE psTmpSNode;

	PVR_DPF_ENTERED;

	if (NULL == phStream || NULL == szStreamName)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	/* Acquire the TL_GLOBAL_DATA lock first to ensure,
	 * the TL_STREAM while returned and being modified,
	 * is not deleted by some other context */
	OSLockAcquire (TLGGD()->hTLGDLock);

	/* Search for a stream node with a matching stream name */
	psTmpSNode = TLFindStreamNodeByName(szStreamName);

	if (NULL == psTmpSNode)
	{
		OSLockRelease (TLGGD()->hTLGDLock);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_FOUND);
	}

	if (psTmpSNode->psStream->psNotifStream != NULL &&
	    psTmpSNode->uiWRefCount == 1)
	{
		TLStreamMarkStreamOpen(psTmpSNode->psStream);
	}

	/* The TL_SNODE->uiWRefCount governs the presence of this node in the
	 * TL_GLOBAL_DATA list i.e. when uiWRefCount falls to zero we try removing
	 * this node from the TL_GLOBAL_DATA list. Hence, is protected using the
	 * TL_GLOBAL_DATA lock and not TL_STREAM lock */
	psTmpSNode->uiWRefCount++;

	OSLockRelease (TLGGD()->hTLGDLock);

	/* Return the stream handle to the caller */
	*phStream = (IMG_HANDLE)psTmpSNode->psStream;

	PVR_DPF_RETURN_VAL(PVRSRV_OK);
}

void
TLStreamClose(IMG_HANDLE hStream)
{
	PTL_STREAM	psTmp;
	IMG_BOOL	bDestroyStream;

	PVR_DPF_ENTERED;

	if (NULL == hStream)
	{
		PVR_DPF((PVR_DBG_WARNING,
				 "TLStreamClose failed as NULL stream handler passed, nothing done."));
		PVR_DPF_RETURN;
	}

	psTmp = (PTL_STREAM)hStream;

	/* Acquire TL_GLOBAL_DATA lock for updating the reference count as this will be required
	 * in-case this TL_STREAM node is to be deleted */
	OSLockAcquire (TLGGD()->hTLGDLock);

	/* Decrement write reference counter of the stream */
	psTmp->psNode->uiWRefCount--;

	if (0 != psTmp->psNode->uiWRefCount)
	{
		/* The stream is still being used in other context(s) do not destroy
		 * anything */

		/* uiWRefCount == 1 means that stream was closed for write. Next
		 * close is pairing TLStreamCreate(). Send notification to indicate
		 * that no writer are connected to the stream any more. */
		if (psTmp->psNotifStream != NULL && psTmp->psNode->uiWRefCount == 1)
		{
			TLStreamMarkStreamClose(psTmp);
		}

		OSLockRelease (TLGGD()->hTLGDLock);
		PVR_DPF_RETURN;
	}
	else
	{
		/* Now we try removing this TL_STREAM from TL_GLOBAL_DATA */

		if (psTmp->bWaitForEmptyOnDestroy)
		{
			/* We won't require the TL_STREAM lock to be acquired here for accessing its read
			 * and write offsets. REASON: We are here because there is no producer context
			 * referencing this TL_STREAM, hence its ui32Write offset won't be changed now.
			 * Also, the update of ui32Read offset is not protected by locks */
			while (psTmp->ui32Read != psTmp->ui32Write)
			{
				/* Release lock before sleeping */
				OSLockRelease (TLGGD()->hTLGDLock);

				OSEventObjectWaitTimeout(psTmp->hProducerEvent, EVENT_OBJECT_TIMEOUT_US);

				OSLockAcquire (TLGGD()->hTLGDLock);

				/* Ensure destruction of stream is still required */
				if (0 != psTmp->psNode->uiWRefCount)
				{
					OSLockRelease (TLGGD()->hTLGDLock);
					PVR_DPF_RETURN;
				}
			}
		}

		/* Try removing the stream from TL_GLOBAL_DATA */
		bDestroyStream = TLTryRemoveStreamAndFreeStreamNode (psTmp->psNode);

		OSLockRelease (TLGGD()->hTLGDLock);

		if (bDestroyStream)
		{
			/* Destroy the stream if it was removed from TL_GLOBAL_DATA */
			TLStreamDestroy (psTmp);
			psTmp = NULL;
		}
		PVR_DPF_RETURN;
	}
}

/*
 * DoTLSetPacketHeader
 *
 * Ensure that whenever we update a Header we always add the RESERVED field
 */
static inline void DoTLSetPacketHeader(PVRSRVTL_PPACKETHDR, IMG_UINT32);
static inline void
DoTLSetPacketHeader(PVRSRVTL_PPACKETHDR pHdr,
				IMG_UINT32 ui32Val)
{
	PVR_ASSERT(((size_t)pHdr & (size_t)(PVRSRVTL_PACKET_ALIGNMENT - 1)) == 0);

	/* Check that this is a correctly aligned packet header. */
	if (((size_t)pHdr & (size_t)(PVRSRVTL_PACKET_ALIGNMENT - 1)) != 0)
	{
		/* Should return an error because the header is misaligned */
		PVR_DPF((PVR_DBG_ERROR, "%s: Misaligned header @ %p", __func__, pHdr));
		pHdr->uiTypeSize = ui32Val;
	}
	else
	{
		pHdr->uiTypeSize = ui32Val;
		pHdr->uiReserved = PVRSRVTL_PACKETHDR_RESERVED;
	}
}

static PVRSRV_ERROR
DoTLStreamReserve(IMG_HANDLE hStream,
				IMG_UINT8 **ppui8Data,
				IMG_UINT32 ui32ReqSize,
				IMG_UINT32 ui32ReqSizeMin,
				PVRSRVTL_PACKETTYPE ePacketType,
				IMG_UINT32* pui32AvSpace,
				IMG_UINT32* pui32Flags)
{
	PTL_STREAM psTmp;
	IMG_UINT32 *pui32Buf, ui32LRead, ui32LWrite, ui32LPending, lReqSizeAligned, lReqSizeActual, ui32CreateFreeSpace;
	IMG_UINT32 ui32InputFlags = 0;
	IMG_INT pad, iFreeSpace;
	IMG_UINT8 *pui8IncrRead = NULL;
	PVRSRVTL_PPACKETHDR pHdr;

	PVR_DPF_ENTERED;
	if (pui32AvSpace) *pui32AvSpace = 0;
	if (pui32Flags)
	{
		ui32InputFlags = *pui32Flags;
		*pui32Flags = 0;
	}

	if (NULL == hStream)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	psTmp = (PTL_STREAM)hStream;

	/* Assert used as the packet type parameter is currently only provided
	 * by the TL APIs, not the calling client */
	PVR_ASSERT((PVRSRVTL_PACKETTYPE_UNDEF < ePacketType) && (PVRSRVTL_PACKETTYPE_LAST >= ePacketType));

	/* The buffer is only used in "rounded" (aligned) chunks */
	lReqSizeAligned = PVRSRVTL_ALIGN(ui32ReqSize);

	/* Lock the stream before reading it's pending value, because if pending is set
	 * to NOTHING_PENDING, we update the pending value such that subsequent calls to
	 * this function from other context(s) fail with PVRSRV_ERROR_NOT_READY */
	OSLockAcquire (psTmp->hStreamWLock);

#if defined(TL_BUFFER_STATS)
	/* If writing into an empty buffer, start recording time-to-full */
	if (psTmp->ui32Read == psTmp->ui32Write)
	{
		OSAtomicWrite(&psTmp->bNoReaderSinceFirstReserve, 1);
		psTmp->ui32TimeStart = OSClockus();
	}

	if (ui32ReqSize > psTmp->ui32MaxReserveWatermark)
	{
		psTmp->ui32MaxReserveWatermark = ui32ReqSize;
	}
#endif

	/* Get a local copy of the stream buffer parameters */
	ui32LRead  = psTmp->ui32Read;
	ui32LWrite = psTmp->ui32Write;
	ui32LPending = psTmp->ui32Pending;

	/* Multiple pending reserves are not supported. */
	if (NOTHING_PENDING != ui32LPending)
	{
		OSLockRelease (psTmp->hStreamWLock);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_READY);
	}

	if (psTmp->ui32MaxPacketSize < lReqSizeAligned)
	{
		PVR_DPF((PVR_DBG_ERROR, "Requested Size: %u > TL Max Packet size: %u", lReqSizeAligned, psTmp->ui32MaxPacketSize));
		psTmp->ui32Pending = NOTHING_PENDING;
		if (pui32AvSpace)
		{
			*pui32AvSpace = suggestAllocSize(ui32LRead, ui32LWrite, psTmp->ui32Size, ui32ReqSizeMin, psTmp->ui32MaxPacketSize);
			if (*pui32AvSpace == 0 && psTmp->eOpMode == TL_OPMODE_DROP_OLDEST)
			{
				*pui32AvSpace = psTmp->ui32MaxPacketSize;
				PVR_DPF((PVR_DBG_MESSAGE, "Opmode is Drop_Oldest, so Available Space changed to: %u", *pui32AvSpace));
			}
		}
		OSLockRelease (psTmp->hStreamWLock);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_TLPACKET_SIZE_LIMIT_EXCEEDED);
	}

	/* Prevent other threads from entering this region before we are done
	 * updating the pending value and write offset (in case of padding). This
	 * is not exactly a lock but a signal for other contexts that there is a
	 * TLStreamCommit operation pending on this stream */
	psTmp->ui32Pending = 0;

	OSLockRelease (psTmp->hStreamWLock);

	/* If there is enough contiguous space following the current Write
	 * position then no padding is required */
	if (  psTmp->ui32Size
		< ui32LWrite + lReqSizeAligned + sizeof(PVRSRVTL_PACKETHDR) )
	{
		pad = psTmp->ui32Size - ui32LWrite;
	}
	else
	{
		pad = 0;
	}

	lReqSizeActual = lReqSizeAligned + sizeof(PVRSRVTL_PACKETHDR) + pad;
	if (psTmp->bNoWrapPermanent)
	{
		iFreeSpace = bufSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size);
	}
	else
	{
		iFreeSpace = circbufSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size);
	}

	if (iFreeSpace < (IMG_INT) lReqSizeActual)
	{
		/* If this is a blocking reserve and there is not enough space then wait. */
		if (psTmp->eOpMode == TL_OPMODE_BLOCK)
		{
			/* Stream create should stop us entering here when
			 * psTmp->bNoWrapPermanent is true as it does not make sense to
			 * block on permanent data streams. */
			PVR_ASSERT(psTmp->bNoWrapPermanent == IMG_FALSE);
			while ( ( circbufSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size)
				 <(IMG_INT) lReqSizeActual ) )
			{
				/* The TL bridge is lockless now, so changing to OSEventObjectWait() */
				OSEventObjectWait(psTmp->hProducerEvent);
				// update local copies.
				ui32LRead  = psTmp->ui32Read;
				ui32LWrite = psTmp->ui32Write;
			}
		}
		/* Data overwriting, also insert PACKETS_DROPPED flag into existing packet */
		else if (psTmp->eOpMode == TL_OPMODE_DROP_OLDEST)
		{
			OSLockAcquire(psTmp->hReadLock);

			while (psTmp->bReadPending)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "Waiting for the pending read operation to complete."));
				OSLockRelease(psTmp->hReadLock);
#if defined(TL_BUFFER_STATS)
				TL_COUNTER_INC(psTmp->ui32CntWriteWaits);
#endif
				(void) OSEventObjectWaitTimeout(psTmp->hProducerEvent, READ_PENDING_TIMEOUT_US);
				OSLockAcquire(psTmp->hReadLock);
			}

#if defined(TL_BUFFER_STATS)
			TL_COUNTER_INC(psTmp->ui32CntWriteSuccesses);
#endif
			ui32LRead = psTmp->ui32Read;

			if ( circbufSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size)
			     < (IMG_INT) lReqSizeActual )
			{
				ui32CreateFreeSpace = 5 * (psTmp->ui32Size / 100);
				if (ui32CreateFreeSpace < lReqSizeActual)
				{
					ui32CreateFreeSpace = lReqSizeActual;
				}

				while (ui32CreateFreeSpace > (IMG_UINT32)circbufSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size))
				{
					pui8IncrRead = &psTmp->pbyBuffer[ui32LRead];
					ui32LRead += (sizeof(PVRSRVTL_PACKETHDR) + PVRSRVTL_ALIGN( GET_PACKET_DATA_LEN(pui8IncrRead) ));

					/* Check if buffer needs to wrap */
					if (ui32LRead >= psTmp->ui32Size)
					{
						ui32LRead = 0;
					}
				}
				psTmp->ui32Read = ui32LRead;
				pui8IncrRead = &psTmp->pbyBuffer[psTmp->ui32Read];

				pHdr = GET_PACKET_HDR(pui8IncrRead);
				DoTLSetPacketHeader(pHdr, SET_PACKETS_DROPPED(pHdr));
			}
			/* else fall through as there is enough space now to write the data */

			OSLockRelease(psTmp->hReadLock);
			/* If we accepted a flag var set the OVERWRITE bit*/
			if (pui32Flags) *pui32Flags |= TL_FLAG_OVERWRITE_DETECTED;
		}
		/* No data overwriting, insert write_failed flag and return */
		else if (psTmp->eOpMode == TL_OPMODE_DROP_NEWER)
		{
			/* Caller should not try to use ppui8Data,
			 * NULLify to give user a chance of avoiding memory corruption */
			*ppui8Data = NULL;

			/* This flag should not be inserted two consecutive times, so
			 * check the last ui32 in case it was a packet drop packet. */
			pui32Buf = ui32LWrite
					  ?
					    (void *)&psTmp->pbyBuffer[ui32LWrite - sizeof(PVRSRVTL_PACKETHDR)]
					   : // Previous four bytes are not guaranteed to be a packet header...
					    (void *)&psTmp->pbyBuffer[psTmp->ui32Size - PVRSRVTL_PACKET_ALIGNMENT];

			pHdr = GET_PACKET_HDR(pui32Buf);
			if ( PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED
				 !=
				 GET_PACKET_TYPE( pHdr ) && (ui32InputFlags & TL_FLAG_NO_WRITE_FAILED) == 0)
			{
				/* Insert size-stamped packet header */
				pui32Buf = (void *)&psTmp->pbyBuffer[ui32LWrite];
				pHdr = GET_PACKET_HDR(pui32Buf);
				DoTLSetPacketHeader(pHdr, PVRSRVTL_SET_PACKET_WRITE_FAILED);
				ui32LWrite += sizeof(PVRSRVTL_PACKETHDR);
				ui32LWrite %= psTmp->ui32Size;
				iFreeSpace -= sizeof(PVRSRVTL_PACKETHDR);
			}

			OSLockAcquire (psTmp->hStreamWLock);
			psTmp->ui32Write = ui32LWrite;
			psTmp->ui32Pending = NOTHING_PENDING;
			OSLockRelease (psTmp->hStreamWLock);

			if (pui32AvSpace)
			{
				*pui32AvSpace = suggestAllocSize(ui32LRead, ui32LWrite, psTmp->ui32Size, ui32ReqSizeMin, psTmp->ui32MaxPacketSize);
			}

			/* Inform call of permanent stream misuse, no space left,
			 * the size of the stream will need to be increased. */
			if (psTmp->bNoWrapPermanent)
			{
				PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_NOT_ENOUGH_SPACE);
			}

			PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_FULL);
		}
	}

	/* The easy case: buffer has enough space to hold the requested packet (data + header) */

	/* Should we treat the buffer as non-circular buffer? */
	if (psTmp->bNoWrapPermanent)
	{
		iFreeSpace = bufSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size);
	}
	else
	{
		iFreeSpace = circbufSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size);
	}

	if (iFreeSpace >= (IMG_INT) lReqSizeActual)
	{
		if (pad)
		{
			/* Inserting padding packet. */
			pui32Buf = (void *)&psTmp->pbyBuffer[ui32LWrite];
			pHdr = GET_PACKET_HDR(pui32Buf);
			DoTLSetPacketHeader(pHdr,
				PVRSRVTL_SET_PACKET_PADDING(pad-sizeof(PVRSRVTL_PACKETHDR)));

			/* CAUTION: the used pad value should always result in a properly
			 *          aligned ui32LWrite pointer, which in this case is 0 */
			ui32LWrite = (ui32LWrite + pad) % psTmp->ui32Size;
			/* Detect unaligned pad value */
			PVR_ASSERT(ui32LWrite == 0);
		}
		/* Insert size-stamped packet header */
		pui32Buf = (void *) &psTmp->pbyBuffer[ui32LWrite];

		pHdr = GET_PACKET_HDR(pui32Buf);
		DoTLSetPacketHeader(pHdr,
			PVRSRVTL_SET_PACKET_HDR(ui32ReqSize, ePacketType));

		/* return the next position in the buffer to the user */
		*ppui8Data = &psTmp->pbyBuffer[ ui32LWrite+sizeof(PVRSRVTL_PACKETHDR) ];

		/* update pending offset: size stamp + data */
		ui32LPending = lReqSizeAligned + sizeof(PVRSRVTL_PACKETHDR);
	}
	else
	{
		OSLockAcquire (psTmp->hStreamWLock);
		psTmp->ui32Pending = NOTHING_PENDING;
		OSLockRelease (psTmp->hStreamWLock);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	/* Acquire stream lock for updating stream parameters */
	OSLockAcquire (psTmp->hStreamWLock);
	psTmp->ui32Write = ui32LWrite;
	psTmp->ui32Pending = ui32LPending;
	OSLockRelease (psTmp->hStreamWLock);

#if defined(TL_BUFFER_STATS)
	TL_COUNTER_INC(psTmp->ui32CntNumWriteSuccess);
#endif

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLStreamReserve(IMG_HANDLE hStream,
				IMG_UINT8 **ppui8Data,
				IMG_UINT32 ui32Size)
{
	return DoTLStreamReserve(hStream, ppui8Data, ui32Size, ui32Size, PVRSRVTL_PACKETTYPE_DATA, NULL, NULL);
}

PVRSRV_ERROR
TLStreamReserve2(IMG_HANDLE hStream,
                IMG_UINT8  **ppui8Data,
                IMG_UINT32 ui32Size,
                IMG_UINT32 ui32SizeMin,
                IMG_UINT32* pui32Available,
                IMG_BOOL* pbIsReaderConnected)
{
	PVRSRV_ERROR eError;

	eError = DoTLStreamReserve(hStream, ppui8Data, ui32Size, ui32SizeMin, PVRSRVTL_PACKETTYPE_DATA, pui32Available, NULL);
	if (eError != PVRSRV_OK && pbIsReaderConnected != NULL)
	{
		*pbIsReaderConnected = TLStreamIsOpenForReading(hStream);
	}

	return eError;
}

PVRSRV_ERROR
TLStreamReserveReturnFlags(IMG_HANDLE hStream,
						   IMG_UINT8  **ppui8Data,
						   IMG_UINT32 ui32Size,
						   IMG_UINT32* pui32Flags)
{
	return DoTLStreamReserve(hStream, ppui8Data, ui32Size, ui32Size, PVRSRVTL_PACKETTYPE_DATA, NULL, pui32Flags);
}

PVRSRV_ERROR
TLStreamCommit(IMG_HANDLE hStream, IMG_UINT32 ui32ReqSize)
{
	PTL_STREAM psTmp;
	IMG_UINT32 ui32LRead, ui32OldWrite, ui32LWrite, ui32LPending;
	PVRSRV_ERROR eError;

#if defined(TL_BUFFER_STATS)
	IMG_UINT32 ui32UnreadBytes;
#endif

	PVR_DPF_ENTERED;

	if (NULL == hStream)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	psTmp = (PTL_STREAM)hStream;

	/* Get a local copy of the stream buffer parameters */
	ui32LRead = psTmp->ui32Read;
	ui32LWrite = psTmp->ui32Write;
	ui32LPending = psTmp->ui32Pending;

	ui32OldWrite = ui32LWrite;

	// Space in buffer is aligned
	ui32ReqSize = PVRSRVTL_ALIGN(ui32ReqSize) + sizeof(PVRSRVTL_PACKETHDR);

	/* Check pending reserver and ReqSize + packet header size. */
	if ((ui32LPending == NOTHING_PENDING) || (ui32ReqSize > ui32LPending))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_MISUSE);
	}

	/* Update pointer to written data. */
	ui32LWrite = (ui32LWrite + ui32ReqSize) % psTmp->ui32Size;

	/* and reset LPending to 0 since data are now submitted */
	ui32LPending = NOTHING_PENDING;

#if defined(TL_BUFFER_STATS)
	/* Calculate new number of bytes unread */
	if (ui32LWrite > ui32LRead)
	{
		ui32UnreadBytes = (ui32LWrite-ui32LRead);
	}
	else if (ui32LWrite < ui32LRead)
	{
		ui32UnreadBytes = (psTmp->ui32Size-ui32LRead+ui32LWrite);
	}
	else
	{ /* else equal, ignore */
		ui32UnreadBytes = 0;
	}

	/* Calculate high water mark for debug purposes */
	if (ui32UnreadBytes > psTmp->ui32BufferUt)
	{
		psTmp->ui32BufferUt = ui32UnreadBytes;
	}
#endif

	/* Memory barrier required to ensure prior data written by writer is
	 * flushed from WC buffer to main memory. */
	OSWriteMemoryBarrier(NULL);

	/* Acquire stream lock to ensure other context(s) (if any)
	 * wait on the lock (in DoTLStreamReserve) for consistent values
	 * of write offset and pending value */
	OSLockAcquire (psTmp->hStreamWLock);

	/* Update stream buffer parameters to match local copies */
	psTmp->ui32Write = ui32LWrite;
	psTmp->ui32Pending = ui32LPending;

	/* Ensure write pointer is flushed */
	OSWriteMemoryBarrier(&psTmp->ui32Write);

	TL_COUNTER_ADD(psTmp->ui32ProducerByteCount, ui32ReqSize);
	TL_COUNTER_INC(psTmp->ui32NumCommits);

#if defined(TL_BUFFER_STATS)
	/* IF there has been no-reader since first reserve on an empty-buffer,
	 * AND current utilisation is considerably high (90%), calculate the
	 * time taken to fill up the buffer */
	if ((OSAtomicRead(&psTmp->bNoReaderSinceFirstReserve) == 1) &&
	    (TLStreamGetUT(psTmp) >= 90 * psTmp->ui32Size/100))
	{
		IMG_UINT32 ui32TimeToFullInUs = OSClockus() - psTmp->ui32TimeStart;
		if (psTmp->ui32MinTimeToFullInUs > ui32TimeToFullInUs)
		{
			psTmp->ui32MinTimeToFullInUs = ui32TimeToFullInUs;
		}
		/* Following write ensures ui32MinTimeToFullInUs doesn't lose its
		 * real (expected) value in case there is no reader until next Commit call */
		OSAtomicWrite(&psTmp->bNoReaderSinceFirstReserve, 0);
	}
#endif

	if (!psTmp->bNoSignalOnCommit)
	{
		/* If we have transitioned from an empty buffer to a non-empty buffer, we
		 * must signal possibly waiting consumer. BUT, let the signal be "deferred"
		 * until buffer is at least 'ui32ThresholdUsageForSignal' bytes full. This
		 * avoids a race between OSEventObjectSignal and OSEventObjectWaitTimeout
		 * (in TLServerAcquireDataKM), where a "signal" might happen before "wait",
		 * resulting into signal being lost and stream-reader waiting even though
		 * buffer is no-more empty */
		if (ui32OldWrite == ui32LRead)
		{
			psTmp->bSignalPending = IMG_TRUE;
		}

		if (psTmp->bSignalPending && (TLStreamGetUT(psTmp) >= psTmp->ui32ThresholdUsageForSignal))
		{
			TL_COUNTER_INC(psTmp->ui32SignalsSent);
			psTmp->bSignalPending = IMG_FALSE;

			/* Signal consumers that may be waiting */
			eError = OSEventObjectSignal(psTmp->psNode->hReadEventObj);
			if (eError != PVRSRV_OK)
			{
				OSLockRelease (psTmp->hStreamWLock);
				PVR_DPF_RETURN_RC(eError);
			}
		}
		else
		{
			TL_COUNTER_INC(psTmp->ui32SignalNotSent);
		}
	}
	OSLockRelease (psTmp->hStreamWLock);

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLStreamWrite(IMG_HANDLE hStream, IMG_UINT8 *pui8Src, IMG_UINT32 ui32Size)
{
	IMG_BYTE *pbyDest = NULL;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	if (NULL == hStream)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	eError = TLStreamReserve(hStream, &pbyDest, ui32Size);
	if (PVRSRV_OK != eError)
	{
		PVR_DPF_RETURN_RC(eError);
	}
	else
	{
		OSDeviceMemCopy((void*)pbyDest, (void*)pui8Src, ui32Size);
		eError = TLStreamCommit(hStream, ui32Size);
		if (PVRSRV_OK != eError)
		{
			PVR_DPF_RETURN_RC(eError);
		}
	}

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLStreamWriteRetFlags(IMG_HANDLE hStream, IMG_UINT8 *pui8Src, IMG_UINT32 ui32Size, IMG_UINT32 *pui32Flags){
	IMG_BYTE *pbyDest = NULL;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	if (NULL == hStream)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	eError = TLStreamReserveReturnFlags(hStream, &pbyDest, ui32Size, pui32Flags);
	if (PVRSRV_OK != eError)
	{
		PVR_DPF_RETURN_RC(eError);
	}
	else
	{
		OSDeviceMemCopy((void*)pbyDest, (void*)pui8Src, ui32Size);
		eError = TLStreamCommit(hStream, ui32Size);
		if (PVRSRV_OK != eError)
		{
			PVR_DPF_RETURN_RC(eError);
		}
	}

	PVR_DPF_RETURN_OK;
}

void TLStreamInfo(IMG_HANDLE hStream, PTL_STREAM_INFO psInfo)
{
	IMG_DEVMEM_SIZE_T actual_req_size;
	IMG_DEVMEM_ALIGN_T align = 4; /* Low fake value so the real value can be obtained */

	actual_req_size = 2;
	/* ignore error as OSGetPageShift() should always return correct value */
	(void) DevmemExportalignAdjustSizeAndAlign(OSGetPageShift(), &actual_req_size, &align);

	psInfo->headerSize = sizeof(PVRSRVTL_PACKETHDR);
	psInfo->minReservationSize = sizeof(IMG_UINT32);
	psInfo->pageSize = (IMG_UINT32)(actual_req_size);
	psInfo->pageAlign = (IMG_UINT32)(align);
	psInfo->maxTLpacketSize = ((PTL_STREAM)hStream)->ui32MaxPacketSize;
}

PVRSRV_ERROR
TLStreamMarkEOS(IMG_HANDLE psStream, IMG_BOOL bRemoveOld)
{
	PTL_STREAM   psTmp;
	PVRSRV_ERROR eError;
	IMG_UINT8*   pData;

	PVR_DPF_ENTERED;

	if (NULL == psStream)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	psTmp = (PTL_STREAM)psStream;

	/* Do not support EOS packets on permanent stream buffers at present,
	 * EOS is best used with streams where data is consumed. */
	if (psTmp->bNoWrapPermanent)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_MISUSE);
	}

	if (bRemoveOld)
	{
		eError = DoTLStreamReserve(psStream, &pData, 0, 0, PVRSRVTL_PACKETTYPE_MARKER_EOS_REMOVEOLD, NULL, NULL);
	}
	else
	{
	eError = DoTLStreamReserve(psStream, &pData, 0, 0, PVRSRVTL_PACKETTYPE_MARKER_EOS, NULL, NULL);
	}

	if (PVRSRV_OK != eError)
	{
		PVR_DPF_RETURN_RC(eError);
	}

	PVR_DPF_RETURN_RC(TLStreamCommit(psStream, 0));
}


static PVRSRV_ERROR
_TLStreamMarkOC(IMG_HANDLE hStream, PVRSRVTL_PACKETTYPE ePacketType)
{
	PVRSRV_ERROR eError;
	PTL_STREAM psStream = hStream;
	IMG_UINT32 ui32Size;
	IMG_UINT8 *pData;

	PVR_DPF_ENTERED;

	if (NULL == psStream)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	if (NULL == psStream->psNotifStream)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_NOTIF_STREAM);
	}

	ui32Size = OSStringLength(psStream->szName) + 1;

	eError = DoTLStreamReserve(psStream->psNotifStream, &pData, ui32Size,
	                           ui32Size, ePacketType, NULL, NULL);
	if (PVRSRV_OK != eError)
	{
		PVR_DPF_RETURN_RC(eError);
	}

	OSDeviceMemCopy(pData, psStream->szName, ui32Size);

	PVR_DPF_RETURN_RC(TLStreamCommit(psStream->psNotifStream, ui32Size));
}

PVRSRV_ERROR
TLStreamMarkStreamOpen(IMG_HANDLE psStream)
{
	return _TLStreamMarkOC(psStream, PVRSRVTL_PACKETTYPE_STREAM_OPEN_FOR_WRITE);
}

PVRSRV_ERROR
TLStreamMarkStreamClose(IMG_HANDLE psStream)
{
	return _TLStreamMarkOC(psStream, PVRSRVTL_PACKETTYPE_STREAM_CLOSE_FOR_WRITE);
}

PVRSRV_ERROR
TLStreamSync(IMG_HANDLE psStream)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PTL_STREAM   psTmp;

	PVR_DPF_ENTERED;

	if (NULL == psStream)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	psTmp = (PTL_STREAM)psStream;

	/* If read client exists and has opened stream in blocking mode,
	 * signal when data is available to read. */
	if (psTmp->psNode->psRDesc &&
		 (!(psTmp->psNode->psRDesc->ui32Flags & PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING)) &&
			psTmp->ui32Read != psTmp->ui32Write)
	{
		TL_COUNTER_INC(psTmp->ui32ManSyncs);
		eError = OSEventObjectSignal(psTmp->psNode->hReadEventObj);
	}

	PVR_DPF_RETURN_RC(eError);
}

IMG_BOOL
TLStreamIsOpenForReading(IMG_HANDLE hStream)
{
	PTL_STREAM psTmp;

	PVR_DPF_ENTERED;

	PVR_ASSERT(hStream);
	psTmp = (PTL_STREAM)hStream;

	PVR_DPF_RETURN_VAL(psTmp->psNode->psRDesc != NULL);
}

IMG_BOOL
TLStreamOutOfData(IMG_HANDLE hStream)
{
	PTL_STREAM psTmp;

	PVR_DPF_ENTERED;

	PVR_ASSERT(hStream);
	psTmp = (PTL_STREAM)hStream;

	/* If both pointers are equal then the buffer is empty */
	PVR_DPF_RETURN_VAL(psTmp->ui32Read == psTmp->ui32Write);
}


PVRSRV_ERROR
TLStreamResetProducerByteCount(IMG_HANDLE hStream, IMG_UINT32 ui32Value)
{
	PTL_STREAM   psTmp;
	IMG_UINT32   ui32LRead, ui32LWrite;
	PVRSRV_ERROR eErr = PVRSRV_OK;

	PVR_DPF_ENTERED;

	PVR_ASSERT(hStream);
	psTmp = (PTL_STREAM)hStream;
	ui32LRead = psTmp->ui32Read;
	ui32LWrite = psTmp->ui32Write;

	if (ui32LRead != ui32LWrite)
	{
		eErr = PVRSRV_ERROR_STREAM_MISUSE;
	}
#if defined(TL_BUFFER_STATS)
	psTmp->ui32ProducerByteCount = ui32Value;
#else
	PVR_UNREFERENCED_PARAMETER(ui32Value);
#endif
	PVR_DPF_RETURN_RC(eErr);
}
/*
 * Internal stream APIs to server part of Transport Layer, declared in
 * header tlintern.h. Direct pointers to stream objects are used here as
 * these functions are internal.
 */
IMG_UINT32
TLStreamAcquireReadPos(PTL_STREAM psStream,
                       IMG_BOOL bDisableCallback,
                       IMG_UINT32* puiReadOffset)
{
	IMG_UINT32 uiReadLen = 0;
	IMG_UINT32 ui32LRead, ui32LWrite;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psStream);
	PVR_ASSERT(puiReadOffset);

	if (psStream->eOpMode == TL_OPMODE_DROP_OLDEST)
	{
		if (!OSTryLockAcquire(psStream->hReadLock))
		{
			/*
			 * This is a normal event when the system is under load.
			 * An example of how to produce this is to run testrunner /
			 * regression/ddk_test_seq2_host_fw_mem.conf with HTB / pvrhtbd
			 * configured as
			 *
			 * # pvrdebug -log trace -loggroups main,pow,debug \
			 * -hostloggroups main,ctrl,sync,brg -hostlogtype dropoldest
			 *
			 * # pvrhtbd -hostloggroups main,ctrl,sync,brg
			 *
			 * We will see a small number of these collisions but as this is
			 * an expected calling path, and an expected return code, we drop
			 * the severity to just be a debug MESSAGE instead of WARNING
			 */
			PVR_DPF((PVR_DBG_MESSAGE,
			    "%s: Read lock on stream '%s' is acquired by some writer, "
			    "hence reader failed to acquire read lock.", __func__,
			    psStream->szName));
#if defined(TL_BUFFER_STATS)
			TL_COUNTER_INC(psStream->ui32CntReadFails);
#endif
			PVR_DPF_RETURN_VAL(0);
		}
	}

#if defined(TL_BUFFER_STATS)
	TL_COUNTER_INC(psStream->ui32CntReadSuccesses);
#endif

	/* Grab a local copy */
	ui32LRead = psStream->ui32Read;
	ui32LWrite = psStream->ui32Write;

	if (psStream->eOpMode == TL_OPMODE_DROP_OLDEST)
	{
		psStream->bReadPending = IMG_TRUE;
		OSLockRelease(psStream->hReadLock);
	}

	/* No data available and CB defined - try and get data */
	if ((ui32LRead == ui32LWrite) && psStream->pfProducerCallback && !bDisableCallback)
	{
		PVRSRV_ERROR eRc;
		IMG_UINT32   ui32Resp = 0;

		eRc = ((TL_STREAM_SOURCECB)psStream->pfProducerCallback)(psStream, TL_SOURCECB_OP_CLIENT_EOS,
				&ui32Resp, psStream->pvProducerUserData);
		PVR_LOG_IF_ERROR(eRc, "TLStream->pfProducerCallback");

		ui32LWrite = psStream->ui32Write;
	}

	/* No data available... */
	if (ui32LRead == ui32LWrite)
	{
		if (psStream->eOpMode == TL_OPMODE_DROP_OLDEST)
		{
			psStream->bReadPending = IMG_FALSE;
		}
		PVR_DPF_RETURN_VAL(0);
	}

#if defined(TL_BUFFER_STATS)
	/* The moment reader knows it will see a non-zero data, it marks its presence in writer's eyes */
	OSAtomicWrite (&psStream->bNoReaderSinceFirstReserve, 0);
#endif

	/* Data is available to read... */
	*puiReadOffset = ui32LRead;

	/*PVR_DPF((PVR_DBG_VERBOSE,
	 *		"TLStreamAcquireReadPos Start before: Write:%d, Read:%d, size:%d",
	 *		ui32LWrite, ui32LRead, psStream->ui32Size));
	 */

	if (ui32LRead > ui32LWrite)
	{	/* CB has wrapped around. */
		PVR_ASSERT(!psStream->bNoWrapPermanent);
		/* Return the first contiguous piece of memory, ie [ReadLen,EndOfBuffer]
		 * and let a subsequent AcquireReadPos read the rest of the Buffer */
		/*PVR_DPF((PVR_DBG_VERBOSE, "TLStreamAcquireReadPos buffer has wrapped"));*/
		uiReadLen = psStream->ui32Size - ui32LRead;
		TL_COUNTER_INC(psStream->ui32AcquireRead2);
	}
	else
	{	/* CB has not wrapped */
		uiReadLen = ui32LWrite - ui32LRead;
		TL_COUNTER_INC(psStream->ui32AcquireRead1);
	}

	PVR_DPF_RETURN_VAL(uiReadLen);
}

PVRSRV_ERROR
TLStreamAdvanceReadPos(PTL_STREAM psStream,
                       IMG_UINT32 uiReadLen,
                       IMG_UINT32 uiOrigReadLen)
{
	IMG_UINT32 uiNewReadPos;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psStream);

	/*
	 * This API does not use Read lock as 'bReadPending' is sufficient
	 * to keep Read index safe by preventing a write from updating the
	 * index and 'bReadPending' itself is safe as it can only be modified
	 * by readers and there can be only one reader in action at a time.
	 */

	/* Update the read offset by the length provided in a circular manner.
	 * Assuming the update to be atomic hence, avoiding use of locks
	 */
	uiNewReadPos = (psStream->ui32Read + uiReadLen) % psStream->ui32Size;

	/* Must validate length is on a packet boundary, for
	 * TLReleaseDataLess calls.
	 */
	if (uiReadLen != uiOrigReadLen) /* buffer not empty */
	{
		PVRSRVTL_PPACKETHDR psHdr = GET_PACKET_HDR(psStream->pbyBuffer+uiNewReadPos);
		PVRSRVTL_PACKETTYPE eType = GET_PACKET_TYPE(psHdr);

		if ((psHdr->uiReserved != PVRSRVTL_PACKETHDR_RESERVED) ||
			(eType == PVRSRVTL_PACKETTYPE_UNDEF) ||
			(eType >= PVRSRVTL_PACKETTYPE_LAST))
		{
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_ALIGNMENT);
		}
		/* else OK, on a packet boundary */
	}
	/* else no check needed */

	psStream->ui32Read = uiNewReadPos;

	if (psStream->eOpMode == TL_OPMODE_DROP_OLDEST)
	{
		psStream->bReadPending = IMG_FALSE;
	}

	/* notify reserves that may be pending */
	/* The producer event object is used to signal the StreamReserve if the TL
	 * Buffer is in blocking mode and is full.
	 * Previously this event was only signalled if the buffer was created in
	 * blocking mode. Since the buffer mode can now change dynamically the event
	 * is signalled every time to avoid any potential race where the signal is
	 * required, but not produced.
	 */
	{
		PVRSRV_ERROR eError;
		eError = OSEventObjectSignal(psStream->hProducerEventObj);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING,
					 "Error in TLStreamAdvanceReadPos: OSEventObjectSignal returned:%u",
					 eError));
			/* We've failed to notify the producer event. This means there may
			 * be a delay in generating more data to be consumed until the next
			 * Write() generating action occurs.
			 */
		}
	}

	PVR_DPF((PVR_DBG_VERBOSE,
			 "TLStreamAdvanceReadPos Read now at: %d",
			psStream->ui32Read));
	PVR_DPF_RETURN_OK;
}

void
TLStreamResetReadPos(PTL_STREAM psStream)
{
	PVR_DPF_ENTERED;

	PVR_ASSERT(psStream);

	if (psStream->bNoWrapPermanent)
	{

		/* Update the read offset by the length provided in a circular manner.
		 * Assuming the update to be atomic hence, avoiding use of locks */
		psStream->ui32Read = 0;

		PVR_DPF((PVR_DBG_VERBOSE,
				 "TLStreamResetReadPos Read now at: %d",
					psStream->ui32Read));
	}
	else
	{
		/* else for other stream types this is a no-op */
		PVR_DPF((PVR_DBG_VERBOSE,
				"No need to reset read position of circular tlstream"));
	}

	PVR_DPF_RETURN;
}

void
TLStreamDestroy (PTL_STREAM psStream)
{
	PVR_ASSERT (psStream);

	OSLockDestroy (psStream->hStreamWLock);
	OSLockDestroy (psStream->hReadLock);

	OSEventObjectClose(psStream->hProducerEvent);
	OSEventObjectDestroy(psStream->hProducerEventObj);

	TLFreeSharedMem(psStream);
	OSFreeMem(psStream);
}

DEVMEM_MEMDESC*
TLStreamGetBufferPointer(PTL_STREAM psStream)
{
	PVR_DPF_ENTERED;

	PVR_ASSERT(psStream);

	PVR_DPF_RETURN_VAL(psStream->psStreamMemDesc);
}
