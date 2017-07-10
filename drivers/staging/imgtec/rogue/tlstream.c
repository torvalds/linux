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

#include "pvrsrv_tlcommon.h"
#include "tlintern.h"
#include "tlstream.h"

/* To debug buffer utilisation enable this macro here and
 * define PVRSRV_NEED_PVR_TRACE in the server pvr_debug.c and in tutils.c
 * before the inclusion of pvr_debug.h. Issue pvrtutils 6 on target to see
 * stream buffer utilisation. */
//#define TL_BUFFER_UTILIZATION 1

#define EVENT_OBJECT_TIMEOUT_US 1000000ULL

/* Given the state of the buffer it returns a number of bytes that the client
 * can use for a successful allocation. */
static INLINE IMG_UINT32 suggestAllocSize(IMG_UINT32 ui32LRead,
										IMG_UINT32 ui32LWrite, 
										IMG_UINT32 ui32CBSize,
						                IMG_UINT32 ui32ReqSizeMin)
{
	IMG_UINT32 ui32AvSpace = 0;
	
	/* This could be written in fewer lines using the ? operator but it  
		would not be kind to potential readers of this source at all. */ 
	if ( ui32LRead > ui32LWrite )                          /* Buffer WRAPPED */
	{
		if ( (ui32LRead - ui32LWrite) > (sizeof(PVRSRVTL_PACKETHDR) + ui32ReqSizeMin + (IMG_INT) BUFFER_RESERVED_SPACE) )
		{
			ui32AvSpace =  ui32LRead - ui32LWrite - sizeof(PVRSRVTL_PACKETHDR) - (IMG_INT) BUFFER_RESERVED_SPACE;
		}
	}
	else                                                  /* Normal, no wrap */
	{
		if ( (ui32CBSize - ui32LWrite) > (sizeof(PVRSRVTL_PACKETHDR) + ui32ReqSizeMin + (IMG_INT) BUFFER_RESERVED_SPACE) )
		{
			ui32AvSpace =  ui32CBSize - ui32LWrite - sizeof(PVRSRVTL_PACKETHDR) - (IMG_INT) BUFFER_RESERVED_SPACE;
		}
		else if ( (ui32LRead - 0) > (sizeof(PVRSRVTL_PACKETHDR) + ui32ReqSizeMin + (IMG_INT) BUFFER_RESERVED_SPACE) )
		{
			ui32AvSpace =  ui32LRead - sizeof(PVRSRVTL_PACKETHDR) - (IMG_INT) BUFFER_RESERVED_SPACE;
		}
	}
    /* The max size of a TL packet currently is UINT16. adjust accordingly */
	return MIN(ui32AvSpace, PVRSRVTL_MAX_PACKET_SIZE);
}

/* Returns bytes left in the buffer. Negative if there is not any.
 * two 4b aligned values are reserved, one for the write failed buffer flag
 * and one to be able to distinguish the buffer full state to the buffer
 * empty state.
 * Always returns free space -8 even when the "write failed" packet may be
 * already in the stream before this write. */
static INLINE IMG_INT
cbSpaceLeft(IMG_UINT32 ui32Read, IMG_UINT32 ui32Write, IMG_UINT32 ui32size)
{
	/* We need to reserve 4b (one packet) in the buffer to be able to tell empty 
	 * buffers from full buffers and one more for packet write fail packet */
	if ( ui32Read > ui32Write )
	{
		return (IMG_INT) ui32Read - (IMG_INT)ui32Write - (IMG_INT) BUFFER_RESERVED_SPACE;
	}
	else
	{
		return (IMG_INT)ui32size - ((IMG_INT)ui32Write - (IMG_INT)ui32Read) - (IMG_INT) BUFFER_RESERVED_SPACE;
	}
}   

PVRSRV_ERROR TLAllocSharedMemIfNull(IMG_HANDLE hStream)
{
	PTL_STREAM psStream = (PTL_STREAM) hStream;
	PVRSRV_ERROR eError;

	IMG_CHAR pszBufferLabel[PRVSRVTL_MAX_STREAM_NAME_SIZE + 20];
	DEVMEM_FLAGS_T uiMemFlags = PVRSRV_MEMALLOCFLAG_CPU_READABLE |
	                            PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
	                            PVRSRV_MEMALLOCFLAG_GPU_READABLE |
	                            PVRSRV_MEMALLOCFLAG_CPU_UNCACHED |
	                            PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE;

	/* Exit if memory has already been allocated. */
	if (psStream->pbyBuffer != NULL)
		return PVRSRV_OK;

	OSSNPrintf(pszBufferLabel, sizeof(pszBufferLabel), "TLStreamBuf-%s",
	           psStream->szName);

	eError = DevmemAllocateExportable((IMG_HANDLE) TLGetGlobalRgxDevice(),
	                                  (IMG_DEVMEM_SIZE_T) psStream->ui32Size,
	                                  (IMG_DEVMEM_ALIGN_T) OSGetPageSize(),
	                                  ExactLog2(OSGetPageSize()),
	                                  uiMemFlags,
	                                  pszBufferLabel,
	                                  &psStream->psStreamMemDesc);
	PVR_LOGG_IF_ERROR(eError, "DevmemAllocateExportable", e0);

	eError = DevmemAcquireCpuVirtAddr(psStream->psStreamMemDesc,
	                                  (void**) &psStream->pbyBuffer);
	PVR_LOGG_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", e1);

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

/******************************************************************************* 
 * TL Server public API implementation.
 ******************************************************************************/
PVRSRV_ERROR
TLStreamCreate(IMG_HANDLE *phStream,
			   IMG_CHAR *szStreamName,
			   IMG_UINT32 ui32Size,
			   IMG_UINT32 ui32StreamFlags,
               TL_STREAM_ONREADEROPENCB pfOnReaderOpenCB,
               void *pvOnRederOpenUD,
               TL_STREAM_SOURCECB pfProducerCB,
               void *pvProducerUD)
{
	PTL_STREAM     psTmp;
	PVRSRV_ERROR   eError;
	IMG_HANDLE     hEventList;
	PTL_SNODE      psn = 0;

	PVR_DPF_ENTERED;
	/* Sanity checks:  */
	/* non NULL handler required */
	if ( NULL == phStream )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	if (szStreamName == NULL || *szStreamName == '\0' ||
	    OSStringLength(szStreamName) >= PRVSRVTL_MAX_STREAM_NAME_SIZE)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	/* Acquire TL_GLOBAL_DATA lock here because, if the following TLFindStreamNodeByName()
	 * returns NULL, a new TL_SNODE will be added to TL_GLOBAL_DATA's TL_SNODE list */
	OSLockAcquire (TLGGD()->hTLGDLock);

	/* Check if there already exists a stream with this name. */
	psn = TLFindStreamNodeByName( szStreamName );
	if ( NULL != psn )
	{
		eError = PVRSRV_ERROR_ALREADY_EXISTS;
		goto e0;
	}

	/* Allocate stream structure container (stream struct) for the new stream */
	psTmp = OSAllocZMem(sizeof(TL_STREAM)) ;
	if ( NULL == psTmp ) 
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e0;
	}

	OSStringCopy(psTmp->szName, szStreamName);

	if ( ui32StreamFlags & TL_FLAG_FORCE_FLUSH )
	{
		psTmp->bWaitForEmptyOnDestroy = IMG_TRUE;
	}

	psTmp->bNoSignalOnCommit = (ui32StreamFlags&TL_FLAG_NO_SIGNAL_ON_COMMIT) ?  IMG_TRUE : IMG_FALSE;

	if ( ui32StreamFlags & TL_FLAG_RESERVE_DROP_NEWER ) 
	{
		if ( ui32StreamFlags & TL_FLAG_RESERVE_BLOCK ) 
		{
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto e1;
		}
		psTmp->bDrop = IMG_TRUE;
	}
	else if ( ui32StreamFlags & TL_FLAG_RESERVE_BLOCK ) 
    {	/* Additional synchronization object required for this kind of stream */
        psTmp->bBlock = IMG_TRUE;
    }

	eError = OSEventObjectCreate(NULL, &psTmp->hProducerEventObj);
	if (eError != PVRSRV_OK)
	{
		goto e1;
	}
	/* Create an event handle for this kind of stream */
	eError = OSEventObjectOpen(psTmp->hProducerEventObj, &psTmp->hProducerEvent);
	if (eError != PVRSRV_OK)
	{
		goto e2;
	}

	psTmp->pfOnReaderOpenCallback = pfOnReaderOpenCB;
	psTmp->pvOnReaderOpenUserData = pvOnRederOpenUD;
	/* Remember producer supplied CB and data for later */
	psTmp->pfProducerCallback = (void(*)(void))pfProducerCB;
	psTmp->pvProducerUserData = pvProducerUD;

	/* Round the requested bytes to a multiple of array elements' size, eg round 3 to 4 */
	psTmp->ui32Size = PVRSRVTL_ALIGN(ui32Size);
	psTmp->ui32Read = 0;
	psTmp->ui32Write = 0;
	psTmp->ui32Pending = NOTHING_PENDING;

	/* Memory will be allocated on first connect to the stream */
	if (!(ui32StreamFlags & TL_FLAG_ALLOCATE_ON_FIRST_OPEN))
	{
		/* Allocate memory for the circular buffer and export it to user space. */
		eError = TLAllocSharedMemIfNull(psTmp);
		PVR_LOGG_IF_ERROR(eError, "TLAllocSharedMem", e3);
	}

	/* Synchronisation object to synchronise with user side data transfers. */
	eError = OSEventObjectCreate(psTmp->szName, &hEventList);
	if (eError != PVRSRV_OK)
	{
		goto e4;
	}

	eError = OSLockCreate (&psTmp->hStreamLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto e5;
	}

	/* Now remember the stream in the global TL structures */
	psn = TLMakeSNode(hEventList, (TL_STREAM *)psTmp, 0);
	if (psn == NULL)
	{
		eError=PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e6;
	}

	/* Stream node created, now reset the write reference count to 1
	 * (i.e. this context's reference) */
	psn->uiWRefCount = 1;

	TLAddStreamNode(psn);

	/* Release TL_GLOBAL_DATA lock as the new TL_SNODE is now added to the list */
	OSLockRelease (TLGGD()->hTLGDLock);

	/* Best effort signal, client wait timeout will ultimately let it find the
	 * new stream if this fails, acceptable to avoid cleanup as it is tricky
	 * at this point */
	(void) OSEventObjectSignal(TLGGD()->hTLEventObj);

	/* Pass the newly created stream handle back to caller */
	*phStream = (IMG_HANDLE)psTmp;
	PVR_DPF_RETURN_OK;

e6:
	OSLockDestroy(psTmp->hStreamLock);
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

PVRSRV_ERROR
TLStreamReconfigure(
		IMG_HANDLE hStream,
		IMG_UINT32 ui32StreamFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PTL_STREAM psTmp;

	PVR_DPF_ENTERED;

	if ( NULL == hStream )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	psTmp = (PTL_STREAM)hStream;

	/* Prevent the TL Stream buffer from being written to
	 * while its mode is being reconfigured
	 */
	OSLockAcquire (psTmp->hStreamLock);
	if ( NOTHING_PENDING != psTmp->ui32Pending )
	{
		OSLockRelease (psTmp->hStreamLock);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_READY);
	}
	psTmp->ui32Pending = 0;
	OSLockRelease (psTmp->hStreamLock);

	if ( ui32StreamFlags & TL_FLAG_RESERVE_DROP_NEWER )
	{
		if ( ui32StreamFlags & TL_FLAG_RESERVE_BLOCK )
		{
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto e1;
		}
		psTmp->bDrop = IMG_TRUE;
		psTmp->bBlock = IMG_FALSE;
	}
	else if ( ui32StreamFlags & TL_FLAG_RESERVE_BLOCK )
	{
		psTmp->bBlock = IMG_TRUE;
		psTmp->bDrop = IMG_FALSE;
    }

e1:
	OSLockAcquire (psTmp->hStreamLock);
	psTmp->ui32Pending = NOTHING_PENDING;
	OSLockRelease (psTmp->hStreamLock);

	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR
TLStreamOpen(IMG_HANDLE *phStream,
             IMG_CHAR   *szStreamName)
{
 	PTL_SNODE  psTmpSNode;

	PVR_DPF_ENTERED;

	if ( NULL == phStream || NULL == szStreamName )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	/* Acquire the TL_GLOBAL_DATA lock first to ensure,
	 * the TL_STREAM while returned and being modified,
	 * is not deleted by some other context */
	OSLockAcquire (TLGGD()->hTLGDLock);
	
	/* Search for a stream node with a matching stream name */
	psTmpSNode = TLFindStreamNodeByName(szStreamName);

	if ( NULL == psTmpSNode )
	{
		OSLockRelease (TLGGD()->hTLGDLock);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_FOUND);
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

	if ( NULL == hStream )
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

	if ( 0 != psTmp->psNode->uiWRefCount )
	{	/* The stream is still being used in other context(s) do not destroy anything */
		OSLockRelease (TLGGD()->hTLGDLock);
		PVR_DPF_RETURN;
	}
	else
	{
		/* Now we try removing this TL_STREAM from TL_GLOBAL_DATA */

		if ( psTmp->bWaitForEmptyOnDestroy == IMG_TRUE )
		{
			/* We won't require the TL_STREAM lock to be acquired here for accessing its read
			 * and write offsets. REASON: We are here because there is no producer context
			 * referencing this TL_STREAM, hence its ui32Write offset won't be changed now.
			 * Also, the updation of ui32Read offset is not protected by locks */
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

static PVRSRV_ERROR
DoTLStreamReserve(IMG_HANDLE hStream,
				IMG_UINT8 **ppui8Data, 
				IMG_UINT32 ui32ReqSize,
                IMG_UINT32 ui32ReqSizeMin,
				PVRSRVTL_PACKETTYPE ePacketType,
				IMG_UINT32* pui32AvSpace)
{
	PTL_STREAM psTmp;
	IMG_UINT32 *pui32Buf, ui32LRead, ui32LWrite, ui32LPending, lReqSizeAligned, lReqSizeActual;
	IMG_INT pad, iFreeSpace;

	PVR_DPF_ENTERED;
	if (pui32AvSpace) *pui32AvSpace = 0;

	if (( NULL == hStream ))
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
	OSLockAcquire (psTmp->hStreamLock);

	/* Get a local copy of the stream buffer parameters */
	ui32LRead  = psTmp->ui32Read ;
	ui32LWrite = psTmp->ui32Write ;
	ui32LPending = psTmp->ui32Pending ;

	/*  Multiple pending reserves are not supported. */
	if ( NOTHING_PENDING != ui32LPending )
	{
		OSLockRelease (psTmp->hStreamLock);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_READY);
	}

	if ( PVRSRVTL_MAX_PACKET_SIZE < lReqSizeAligned )
	{
		psTmp->ui32Pending = NOTHING_PENDING;
		if (pui32AvSpace)
		{
			*pui32AvSpace = suggestAllocSize(ui32LRead, ui32LWrite, psTmp->ui32Size, ui32ReqSizeMin);
		}
		OSLockRelease (psTmp->hStreamLock);
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG);
	}

	/* Prevent other threads from entering this region before we are done updating
	 * the pending value and write offset (incase of padding). This is not exactly 
	 * a lock but a signal for other contexts that there is a TLStreamCommit operation
	 * pending on this stream */
	psTmp->ui32Pending = 0;

	OSLockRelease (psTmp->hStreamLock);

	/* If there is enough contiguous space following the current Write
	 * position then no padding is required */
	if (  psTmp->ui32Size
		< ui32LWrite + lReqSizeAligned + sizeof(PVRSRVTL_PACKETHDR) )
	{
		pad = psTmp->ui32Size - ui32LWrite;
	}
	else
	{
		pad = 0 ;
	}

	lReqSizeActual = lReqSizeAligned + sizeof(PVRSRVTL_PACKETHDR) + pad ;
	/* If this is a blocking reserve and there is not enough space then wait. */
	if( psTmp->bBlock )
	{
		if( psTmp->ui32Size < lReqSizeActual )
		{
			/* Acquire stream lock for updating pending value */
			OSLockAcquire (psTmp->hStreamLock);
			psTmp->ui32Pending = NOTHING_PENDING;
			OSLockRelease (psTmp->hStreamLock);
			
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_MISUSE);
		}
		while ( ( cbSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size)
		         <(IMG_INT) lReqSizeActual ) )
		{
			/* The TL should never drop the Bridge Lock as this can lead to
			 * deadlocks.  See comment for TLStreamReconfigure.
			 */
			OSEventObjectWaitAndHoldBridgeLock(psTmp->hProducerEvent);
			// update local copies.
			ui32LRead  = psTmp->ui32Read ;
			ui32LWrite = psTmp->ui32Write ;
		}
	}

	iFreeSpace = cbSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size);
	
	/* The easy case: buffer has enough space to hold the requested packet (data + header) */
	if (  iFreeSpace >=(IMG_INT) lReqSizeActual )
	{
		if ( pad ) 
		{ 
			/* Inserting padding packet. */
			pui32Buf = (IMG_UINT32*)&psTmp->pbyBuffer[ui32LWrite];
			*pui32Buf = PVRSRVTL_SET_PACKET_PADDING(pad-sizeof(PVRSRVTL_PACKETHDR)) ;

			/* CAUTION: the used pad value should always result in a properly 
			 *          aligned ui32LWrite pointer, which in this case is 0 */
			ui32LWrite = (ui32LWrite + pad) % psTmp->ui32Size;
			/* Detect unaligned pad value */
			PVR_ASSERT( ui32LWrite == 0);
		}
		/* Insert size-stamped packet header */
		pui32Buf = (IMG_UINT32*) &psTmp->pbyBuffer[ui32LWrite];

		*pui32Buf = PVRSRVTL_SET_PACKET_HDR(ui32ReqSize, ePacketType);

		/* return the next position in the buffer to the user */
		*ppui8Data =  &psTmp->pbyBuffer[ ui32LWrite+sizeof(PVRSRVTL_PACKETHDR) ] ;

		/* update pending offset: size stamp + data  */
		ui32LPending = lReqSizeAligned + sizeof(PVRSRVTL_PACKETHDR) ;
	}
	/* The not so easy case: not enough space, decide how to handle data */
	else
	{

#if defined(DEBUG)
		/* Sanity check that the user is not trying to add more data than the
		 * buffer size. Conditionally compile it out to ensure this check has
		 * no impact to release performance */
		if ( lReqSizeAligned+sizeof(PVRSRVTL_PACKETHDR) > psTmp->ui32Size )
		{
			OSLockAcquire (psTmp->hStreamLock);
			psTmp->ui32Pending = NOTHING_PENDING;
			OSLockRelease (psTmp->hStreamLock);
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_MISUSE);
		}
#endif

		/* No data overwriting, insert write_failed flag and return */
		if (psTmp->bDrop) 
		{
			/* Caller should not try to use ppui8Data,
			 * NULLify to give user a chance of avoiding memory corruption */
			ppui8Data = NULL;

			/* This flag should not be inserted two consecutive times, so 
			 * check the last ui32 in case it was a packet drop packet. */
			pui32Buf =  ui32LWrite 
					  ? 
					    (IMG_UINT32*)&psTmp->pbyBuffer[ui32LWrite - sizeof(PVRSRVTL_PACKETHDR)]
					   : // Previous four bytes are not guaranteed to be a packet header...
					    (IMG_UINT32*)&psTmp->pbyBuffer[psTmp->ui32Size - PVRSRVTL_PACKET_ALIGNMENT];

			if ( PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED
				 != 
				 GET_PACKET_TYPE( (PVRSRVTL_PACKETHDR*)pui32Buf ) )
			{
				/* Insert size-stamped packet header */
				pui32Buf = (IMG_UINT32*)&psTmp->pbyBuffer[ui32LWrite];
				*pui32Buf = PVRSRVTL_SET_PACKET_WRITE_FAILED ;
				ui32LWrite += sizeof(PVRSRVTL_PACKETHDR);
				ui32LWrite %= psTmp->ui32Size;
				iFreeSpace -= sizeof(PVRSRVTL_PACKETHDR);
			}

			OSLockAcquire (psTmp->hStreamLock);
			psTmp->ui32Write = ui32LWrite;
			psTmp->ui32Pending = NOTHING_PENDING;
			OSLockRelease (psTmp->hStreamLock);
			
			if (pui32AvSpace)
			{
				*pui32AvSpace = suggestAllocSize(ui32LRead, ui32LWrite, psTmp->ui32Size, ui32ReqSizeMin);
			}
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG);
		} 
	}

	/* Acquire stream lock for updating stream parameters */
	OSLockAcquire (psTmp->hStreamLock);
	psTmp->ui32Write = ui32LWrite ;
	psTmp->ui32Pending = ui32LPending ;
	OSLockRelease (psTmp->hStreamLock);

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLStreamReserve(IMG_HANDLE hStream,
				IMG_UINT8 **ppui8Data,
				IMG_UINT32 ui32Size)
{
	return DoTLStreamReserve(hStream, ppui8Data, ui32Size, ui32Size, PVRSRVTL_PACKETTYPE_DATA, NULL);
}

PVRSRV_ERROR
TLStreamReserve2(IMG_HANDLE hStream,
                IMG_UINT8  **ppui8Data,
                IMG_UINT32 ui32Size,
                IMG_UINT32 ui32SizeMin,
                IMG_UINT32* pui32Available)
{
	return DoTLStreamReserve(hStream, ppui8Data, ui32Size, ui32SizeMin, PVRSRVTL_PACKETTYPE_DATA, pui32Available);
}

PVRSRV_ERROR
TLStreamCommit(IMG_HANDLE hStream, IMG_UINT32 ui32ReqSize)
{
	PTL_STREAM psTmp;
	IMG_UINT32 ui32LRead, ui32OldWrite, ui32LWrite, ui32LPending;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	if ( NULL == hStream )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	psTmp = (PTL_STREAM)hStream;

	/* Get a local copy of the stream buffer parameters */
	ui32LRead = psTmp->ui32Read ;
	ui32LWrite = psTmp->ui32Write ;
	ui32LPending = psTmp->ui32Pending ;

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

	/* and reset LPending to 0 since data are now submitted  */
	ui32LPending = NOTHING_PENDING;

	/* Calculate high water mark for debug purposes */
#if defined(TL_BUFFER_UTILIZATION)
	{
		IMG_UINT32 tmp = 0;
		if (ui32LWrite > ui32LRead)
		{
			tmp = (ui32LWrite-ui32LRead);
		}
		else if (ui32LWrite < ui32LRead)
		{
			tmp = (psTmp->ui32Size-ui32LRead+ui32LWrite);
		} /* else equal, ignore */

		if (tmp > psTmp->ui32BufferUt)
		{
			psTmp->ui32BufferUt = tmp;
		}
	}
#endif

	/* Acquire stream lock to ensure other context(s) (if any)
	 * wait on the lock (in DoTLStreamReserve) for consistent values
	 * of write offset and pending value */
	OSLockAcquire (psTmp->hStreamLock);

	/* Update stream buffer parameters to match local copies */
	psTmp->ui32Write = ui32LWrite ;
	psTmp->ui32Pending = ui32LPending ;

	OSLockRelease (psTmp->hStreamLock);

	/* If  we have transitioned from an empty buffer to a non-empty buffer,
	 * signal any consumers that may be waiting */
	if (ui32OldWrite == ui32LRead && !psTmp->bNoSignalOnCommit)
	{
		/* Signal consumers that may be waiting */
		eError = OSEventObjectSignal(psTmp->psNode->hReadEventObj);
		if ( eError != PVRSRV_OK)
		{
			PVR_DPF_RETURN_RC(eError);
		}
	}
	
	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLStreamWrite(IMG_HANDLE hStream, IMG_UINT8 *pui8Src, IMG_UINT32 ui32Size)
{
	IMG_BYTE *pbyDest = NULL;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	if ( NULL == hStream )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	eError = TLStreamReserve(hStream, &pbyDest, ui32Size);
	if ( PVRSRV_OK != eError ) 
	{	
		PVR_DPF_RETURN_RC(eError);
	}
	else if ( pbyDest )
	{
		OSDeviceMemCopy((void*)pbyDest, (void*)pui8Src, ui32Size);
		eError = TLStreamCommit(hStream, ui32Size);
		if ( PVRSRV_OK != eError ) 
		{	
			PVR_DPF_RETURN_RC(eError);
		}
	}
	else
	{
		/* A NULL ptr returned from TLStreamReserve indicates the TL buffer is full */
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG);
	}
	PVR_DPF_RETURN_OK;
}

void TLStreamInfo(PTL_STREAM_INFO psInfo)
{
 	IMG_DEVMEM_SIZE_T actual_req_size;
	IMG_DEVMEM_ALIGN_T align = 4; /* Low dummy value so the real value can be obtained */

 	actual_req_size = 2; 
	DevmemExportalignAdjustSizeAndAlign(OSGetPageShift(), &actual_req_size, &align);

	psInfo->headerSize = sizeof(PVRSRVTL_PACKETHDR);
	psInfo->minReservationSize = sizeof(IMG_UINT32);
	psInfo->pageSize = (IMG_UINT32)(actual_req_size);
	psInfo->pageAlign = (IMG_UINT32)(align);
}

PVRSRV_ERROR
TLStreamMarkEOS(IMG_HANDLE psStream)
{
	PVRSRV_ERROR eError;
	IMG_UINT8* pData;

	PVR_DPF_ENTERED;

	if ( NULL == psStream )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	eError = DoTLStreamReserve(psStream, &pData, 0, 0, PVRSRVTL_PACKETTYPE_MARKER_EOS, NULL);
	if ( PVRSRV_OK !=  eError )
	{
		PVR_DPF_RETURN_RC(eError);
	}

	PVR_DPF_RETURN_RC(TLStreamCommit(psStream, 0));
}

PVRSRV_ERROR
TLStreamSync(IMG_HANDLE psStream)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PTL_STREAM   psTmp;

	PVR_DPF_ENTERED;

	if ( NULL == psStream )
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
		eError = OSEventObjectSignal(psTmp->psNode->hReadEventObj);
	}

	PVR_DPF_RETURN_RC(eError);
}

/*
 * Internal stream APIs to server part of Transport Layer, declared in
 * header tlintern.h. Direct pointers to stream objects are used here as
 * these functions are internal.
 */
IMG_UINT32
TLStreamAcquireReadPos(PTL_STREAM psStream, IMG_UINT32* puiReadOffset)
{
	IMG_UINT32 uiReadLen = 0;
	IMG_UINT32 ui32LRead, ui32LWrite;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psStream);
	PVR_ASSERT(puiReadOffset);

	/* Grab a local copy */
	ui32LRead = psStream->ui32Read;
	ui32LWrite = psStream->ui32Write;

	/* No data available and CB defined - try and get data */
	if ((ui32LRead == ui32LWrite) && psStream->pfProducerCallback)
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
		PVR_DPF_RETURN_VAL(0);
	}

	/* Data is available to read... */
	*puiReadOffset = ui32LRead;

	/*PVR_DPF((PVR_DBG_VERBOSE,
	 *		"TLStreamAcquireReadPos Start before: Write:%d, Read:%d, size:%d",
	 *		ui32LWrite, ui32LRead, psStream->ui32Size));
	 */

	if ( ui32LRead > ui32LWrite )
	{	/* CB has wrapped around. 
		 * Return the first contiguous piece of memory, ie [ReadLen,EndOfBuffer]
		 * and let a subsequent AcquireReadPos read the rest of the Buffer */
		/*PVR_DPF((PVR_DBG_VERBOSE, "TLStreamAcquireReadPos buffer has wrapped"));*/
		uiReadLen = psStream->ui32Size - ui32LRead;
	}
	else
	{	// CB has not wrapped
		uiReadLen = ui32LWrite - ui32LRead;
	}

	PVR_DPF_RETURN_VAL(uiReadLen);
}

void
TLStreamAdvanceReadPos(PTL_STREAM psStream, IMG_UINT32 uiReadLen)
{
	PVR_DPF_ENTERED;

	PVR_ASSERT(psStream);

	/* Update the read offset by the length provided in a circular manner.
	 * Assuming the updation to be atomic hence, avoiding use of locks */
	psStream->ui32Read = (psStream->ui32Read + uiReadLen) % psStream->ui32Size;

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
		if ( eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING,
					 "Error in TLStreamAdvanceReadPos: OSEventObjectSignal returned:%u",
					 eError));
		}
	}

	PVR_DPF((PVR_DBG_VERBOSE,
			 "TLStreamAdvanceReadPos Read now at: %d",
			psStream->ui32Read));
	PVR_DPF_RETURN;
}

void
TLStreamDestroy (PTL_STREAM psStream)
{
	PVR_ASSERT (psStream);
	
	OSLockDestroy (psStream->hStreamLock);

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

IMG_BOOL
TLStreamEOS(PTL_STREAM psStream)
{
	PVR_DPF_ENTERED;

	PVR_ASSERT(psStream);

	/* If both pointers are equal then the buffer is empty */
	PVR_DPF_RETURN_VAL( psStream->ui32Read == psStream->ui32Write );
}

