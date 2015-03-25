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
#include "pvrsrv_error.h"
#include "osfunc.h"

#include "pvr_tlcommon.h"
#include "tlintern.h"
#include "tlstream.h"

/* To debug buffer utilisation enable this macro here and
 * define PVRSRV_NEED_PVR_TRACE in the server pvr_debug.c and in tutils.c
 * before the inclusion of pvr_debug.h. Issue pvrtutils 6 on target to see
 * stream buffer utilisation. */
//#define TL_BUFFER_UTILIZATION 1

#define EVENT_OBJECT_TIMEOUT_MS 1000

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
	return MIN(ui32AvSpace, IMG_UINT16_MAX);
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

/******************************************************************************* 
 * TL Server public API implementation.
 ******************************************************************************/
PVRSRV_ERROR
TLStreamCreate(IMG_HANDLE *phStream,
			   IMG_CHAR *szStreamName,
			   IMG_UINT32 ui32Size,
			   IMG_UINT32 ui32StreamFlags,
               TL_STREAM_SOURCECB pfProducerCB,
               IMG_PVOID pvProducerUD)
{
	PTL_STREAM     psTmp;
	PVRSRV_ERROR   eError;
	IMG_HANDLE     hEventList;
	PTL_SNODE      psn = 0;
	IMG_CHAR       pszBufferLabel[PRVSRVTL_MAX_STREAM_NAME_SIZE+20];

	DEVMEM_FLAGS_T uiMemFlags =  PVRSRV_MEMALLOCFLAG_CPU_READABLE |
								 PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE | 
								 PVRSRV_MEMALLOCFLAG_GPU_READABLE |
								 PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
								 PVRSRV_MEMALLOCFLAG_UNCACHED | /* GPU & CPU */
								 PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
								 PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE;

	PVR_DPF_ENTERED;
	/* Sanity checks:  */
	/* non NULL handler required */
	if ( NULL == phStream ) 
	{ 
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	if (OSStringLength(szStreamName) >= PRVSRVTL_MAX_STREAM_NAME_SIZE) 
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	/* Check if there already exists a stream with this name. */
	psn = TLFindStreamNodeByName( szStreamName );
	if ( IMG_NULL != psn )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_ALREADY_EXISTS);
	}
	
	/* Allocate stream structure container (stream struct) for the new stream */
	psTmp = OSAllocZMem(sizeof(TL_STREAM)) ;
	if ( NULL == psTmp ) 
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_OUT_OF_MEMORY);
	}

	OSStringCopy(psTmp->szName, szStreamName);

	if ( ui32StreamFlags & TL_FLAG_FORCE_FLUSH )
	{
		psTmp->bWaitForEmptyOnDestroy = IMG_TRUE;
	}

	psTmp->bNoSignalOnCommit = (ui32StreamFlags&TL_FLAG_NO_SIGNAL_ON_COMMIT) ?  IMG_TRUE : IMG_FALSE;

	if ( ui32StreamFlags & TL_FLAG_DROP_DATA ) 
	{
		if ( ui32StreamFlags & TL_FLAG_BLOCKING_RESERVE ) 
		{
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto e0;
		}
		psTmp->bDrop = IMG_TRUE;
	}
	else if ( ui32StreamFlags & TL_FLAG_BLOCKING_RESERVE ) 
    {	/* Additional synchronization object required for this kind of stream */
        psTmp->bBlock = IMG_TRUE;

		eError = OSEventObjectCreate(NULL, &psTmp->hProducerEventObj);
		if (eError != PVRSRV_OK)
		{
			goto e0;
		}
		/* Create an event handle for this kind of stream */
		eError = OSEventObjectOpen(psTmp->hProducerEventObj, &psTmp->hProducerEvent);
		if (eError != PVRSRV_OK)
		{
			goto e1;
		}
    }

	/* Remember producer supplied CB and data for later */
	psTmp->pfProducerCallback = (IMG_VOID(*)(IMG_VOID))pfProducerCB;
	psTmp->pvProducerUserData = pvProducerUD;

	/* Round the requested bytes to a multiple of array elements' size, eg round 3 to 4 */
	psTmp->ui32Size = PVRSRVTL_ALIGN(ui32Size);
	psTmp->ui32Read = 0;
	psTmp->ui32Write = 0;
	psTmp->ui32Pending = NOTHING_PENDING;

	OSSNPrintf(pszBufferLabel, sizeof(pszBufferLabel), "TLStreamBuf-%s", szStreamName);

	/* Allocate memory for the circular buffer and export it to user space. */
	eError = DevmemAllocateExportable( IMG_NULL,
									   (IMG_HANDLE) TLGetGlobalRgxDevice(),
									   (IMG_DEVMEM_SIZE_T)psTmp->ui32Size,
									   (IMG_DEVMEM_ALIGN_T) OSGetPageSize(),
									   uiMemFlags | PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE,
									   pszBufferLabel,
									   &psTmp->psStreamMemDesc);
	PVR_LOGG_IF_ERROR(eError, "DevmemAllocateExportable", e2);

	eError = DevmemAcquireCpuVirtAddr( psTmp->psStreamMemDesc, (IMG_VOID**) &psTmp->pbyBuffer );
	PVR_LOGG_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", e3);

	eError = DevmemExport(psTmp->psStreamMemDesc, &(psTmp->sExportCookie));
	PVR_LOGG_IF_ERROR(eError, "DevmemExport", e4);

	/* Synchronization object to synchronize with user side data transfers. */
	eError = OSEventObjectCreate(psTmp->szName, &hEventList);
	if (eError != PVRSRV_OK)
	{
		goto e5;
	}

	/* Stream created, now reset the reference count to 1 */
	psTmp->uiRefCount = 1;

//Thread Safety: Not yet implemented		eError = OSLockCreate(&psTmp->hLock, LOCK_TYPE_PASSIVE);
//Thread Safety: Not yet implemented		if (eError != PVRSRV_OK)
//Thread Safety: Not yet implemented		{
//Thread Safety: Not yet implemented			goto e6;
//Thread Safety: Not yet implemented		}

	/* Now remember the stream in the global TL structures */
	psn = TLMakeSNode(hEventList, (TL_STREAM *)psTmp, 0);
	if (psn == NULL)
	{
		eError=PVRSRV_ERROR_OUT_OF_MEMORY;
		goto e7;
	}
	TLAddStreamNode(psn);

	/* Best effort signal, client wait timeout will ultimately let it find the
	 * new stream if this fails, acceptable to avoid cleanup as it is tricky
	 * at this point */
	(void) OSEventObjectSignal(TLGGD()->hTLEventObj);

	/* Pass the newly created stream handle back to caller */
	*phStream = (IMG_HANDLE)psTmp;
	PVR_DPF_RETURN_OK;

e7:
//Thread Safety: Not yet implemented		OSLockDestroy(psTmp->hLock);
//Thread Safety: Not yet implemented e6:
	OSEventObjectDestroy(hEventList);
e5:
	DevmemUnexport(psTmp->psStreamMemDesc, &(psTmp->sExportCookie));
e4:
	DevmemReleaseCpuVirtAddr( psTmp->psStreamMemDesc );
e3:
	DevmemFree(psTmp->psStreamMemDesc);
e2:
	OSEventObjectClose(psTmp->hProducerEvent);
e1:
	OSEventObjectDestroy(psTmp->hProducerEventObj);
e0:
	OSFREEMEM(psTmp);
	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR
TLStreamOpen(IMG_HANDLE *phStream,
             IMG_CHAR   *szStreamName)
{
 	PTL_SNODE  psTmpSNode;

	PVR_DPF_ENTERED;

	if ( IMG_NULL == phStream || IMG_NULL == szStreamName )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	/* Search for a stream node with a matching stream name */
	psTmpSNode = TLFindStreamNodeByName(szStreamName);

	if ( IMG_NULL == psTmpSNode )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_FOUND);
	}
	else
	{ /* Found a stream to open. lock and increase reference count */

//Thread Safety: Not yet implemented	OSLockAcquire(psTmpStream->hLock);
		psTmpSNode->psStream->uiRefCount++;
		*phStream = (IMG_HANDLE)psTmpSNode->psStream;
//Thread Safety: Not yet implemented	OSLockRelease(psTmpStream->hLock);

		PVR_DPF_RETURN_VAL(PVRSRV_OK);
	}
}

IMG_VOID 
TLStreamClose(IMG_HANDLE hStream)
{
	PTL_STREAM	psTmp;

	PVR_DPF_ENTERED;

	if ( IMG_NULL == hStream )
	{
		PVR_DPF((PVR_DBG_WARNING,
				 "TLStreamClose failed as NULL stream handler passed, nothing done.\n"));
		PVR_DPF_RETURN;
	}

	psTmp = (PTL_STREAM)hStream;

	/* Decrement reference counter */	
//Thread Safety: Not yet implemented	OSLockAcquire(psTmp->hLock);
	psTmp->uiRefCount--;
//Thread Safety: Not yet implemented	OSLockRelease(psTmp->hLock);

	/* The stream is still being used in other context(s) do not destroy anything */
	if ( 0 != psTmp->uiRefCount )
	{
		PVR_DPF_RETURN;
	}
	else
	{
		if ( psTmp->bWaitForEmptyOnDestroy == IMG_TRUE )
		{
			while (psTmp->ui32Read != psTmp->ui32Write)
			{
				OSEventObjectWaitTimeout(psTmp->hProducerEvent,
										 EVENT_OBJECT_TIMEOUT_MS);
			}
		}
		/* First remove it from the global structures to prevent access
		 * while it is being free'd. Lock it?
		 */
		TLRemoveStreamAndTryFreeStreamNode(psTmp->psNode);

//Thread Safety: Not yet implemented			OSLockDestroy(psTmp->hLock);

		// In block-while-reserve streams those not be NULL 
		if ( IMG_TRUE == psTmp->bBlock ) 
		{
			OSEventObjectClose(psTmp->hProducerEvent);
			OSEventObjectDestroy(psTmp->hProducerEventObj);
		}

		DevmemUnexport(psTmp->psStreamMemDesc, &psTmp->sExportCookie);
		DevmemReleaseCpuVirtAddr(psTmp->psStreamMemDesc);
		DevmemFree(psTmp->psStreamMemDesc);

		OSFREEMEM(psTmp);
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
	IMG_UINT32 *ui32Buf, ui32LRead, ui32LWrite, ui32LPending, lReqSizeAligned, lReqSizeActual;
	IMG_INT pad, iFreeSpace;

	PVR_DPF_ENTERED;
	if (pui32AvSpace) *pui32AvSpace = 0;

	if (( IMG_NULL == hStream ))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	psTmp = (PTL_STREAM)hStream;

	/* Assert used as the packet type parameter is currently only provided
	 * by the TL APIs, not the calling client */
	PVR_ASSERT((PVRSRVTL_PACKETTYPE_UNDEF < ePacketType) && (PVRSRVTL_PACKETTYPE_LAST >= ePacketType));

	/* The buffer is only used in "rounded" (aligned) chunks */
	lReqSizeAligned = PVRSRVTL_ALIGN(ui32ReqSize);

	/* Get a local copy of the stream buffer parameters */
	ui32LRead  = psTmp->ui32Read ;
	ui32LWrite = psTmp->ui32Write ;
	ui32LPending = psTmp->ui32Pending ;

	/*  Multiple pending reserves are not supported. */
	if ( NOTHING_PENDING != ui32LPending )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_READY);
	}

	if ( IMG_UINT16_MAX < lReqSizeAligned )
	{
		psTmp->ui32Pending = NOTHING_PENDING;
		if (pui32AvSpace)
		{
			*pui32AvSpace = suggestAllocSize(ui32LRead, ui32LWrite, psTmp->ui32Size, ui32ReqSizeMin);
		}
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_FULL);
	}

	/* Prevent other threads from entering this region before we are done.
	 * Not exactly a lock... */
	psTmp->ui32Pending = 0;

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
			psTmp->ui32Pending = NOTHING_PENDING;
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_MISUSE);
		}
		while ( ( cbSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size)
		         <(IMG_INT) lReqSizeActual ) )
		{
			OSEventObjectWait(psTmp->hProducerEvent);
			// update local copies.
			ui32LRead  = psTmp->ui32Read ;
			ui32LWrite = psTmp->ui32Write ;
		}
	}

	/* The easy case: buffer has enough space to hold the requested packet (data + header) 
	 */
	iFreeSpace = cbSpaceLeft(ui32LRead, ui32LWrite, psTmp->ui32Size);
	if (  iFreeSpace >=(IMG_INT) lReqSizeActual )
	{
		if ( pad ) 
		{ 
			/* Inserting padding packet. */
			ui32Buf = (IMG_UINT32*)&psTmp->pbyBuffer[ui32LWrite];
			*ui32Buf = PVRSRVTL_SET_PACKET_PADDING(pad-sizeof(PVRSRVTL_PACKETHDR)) ;

			/* CAUTION: the used pad value should always result in a properly 
			 *          aligned ui32LWrite pointer, which in this case is 0 */
			ui32LWrite = (ui32LWrite + pad) % psTmp->ui32Size;
			/* Detect unaligned pad value */
			PVR_ASSERT( ui32LWrite == 0);
		}
		/* Insert size-stamped packet header */
		ui32Buf = (IMG_UINT32*)&psTmp->pbyBuffer[ui32LWrite];

		*ui32Buf = PVRSRVTL_SET_PACKET_HDR(ui32ReqSize, ePacketType);

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
			psTmp->ui32Pending = NOTHING_PENDING;
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_MISUSE);
		}
#endif

		/* No data overwriting, insert write_failed flag and return */
		if (psTmp->bDrop) 
		{
			/* Caller should not try to use ppui8Data,
			 * NULLify to give user a chance of avoiding memory corruption */
			ppui8Data = IMG_NULL;

			/* This flag should not be inserted two consecutive times, so 
			 * check the last ui32 in case it was a packet drop packet. */
			ui32Buf =  ui32LWrite 
					  ? 
					    (IMG_UINT32*)&psTmp->pbyBuffer[ui32LWrite - sizeof(PVRSRVTL_PACKETHDR)]
					   : // Previous four bytes are not guaranteed to be a packet header...
					    (IMG_UINT32*)&psTmp->pbyBuffer[psTmp->ui32Size - PVRSRVTL_PACKET_ALIGNMENT];

			if ( PVRSRVTL_PACKETTYPE_MOST_RECENT_WRITE_FAILED
				 != 
				 GET_PACKET_TYPE( (PVRSRVTL_PACKETHDR*)ui32Buf ) )
			{
				/* Insert size-stamped packet header */
				ui32Buf = (IMG_UINT32*)&psTmp->pbyBuffer[ui32LWrite];
				*ui32Buf = PVRSRVTL_SET_PACKET_WRITE_FAILED ;
				ui32LWrite += sizeof(PVRSRVTL_PACKETHDR);
				iFreeSpace -= sizeof(PVRSRVTL_PACKETHDR);
			}

			psTmp->ui32Write = ui32LWrite;
			psTmp->ui32Pending = NOTHING_PENDING;
			if (pui32AvSpace)
			{
				*pui32AvSpace = suggestAllocSize(ui32LRead, ui32LWrite, psTmp->ui32Size, ui32ReqSizeMin);
			}
			PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_FULL);
		} 
	}
	/* Update stream. */
	psTmp->ui32Write = ui32LWrite ;
	psTmp->ui32Pending = ui32LPending ;

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

	if ( IMG_NULL == hStream )
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
	ui32ReqSize = PVRSRVTL_ALIGN(ui32ReqSize);

	/* Sanity check. ReqSize + packet header size. */
	if ( ui32LPending != ui32ReqSize + sizeof(PVRSRVTL_PACKETHDR) )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_MISUSE);
	}

	/* Update pointer to written data. */
	ui32LWrite = (ui32LWrite + ui32LPending) % psTmp->ui32Size;

	/* and reset LPending to 0 since data are now submitted  */
	ui32LPending = NOTHING_PENDING;

	/* If  we have transitioned from an empty buffer to a non-empty buffer,
	 * signal any consumers that may be waiting. */
	if (ui32OldWrite == ui32LRead && !psTmp->bNoSignalOnCommit)
	{
		/* Signal consumers that may be waiting */
		eError = OSEventObjectSignal(psTmp->psNode->hDataEventObj);
		if ( eError != PVRSRV_OK)
		{
			PVR_DPF_RETURN_RC(eError);
		}
	}

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

	/* Update stream buffer parameters to match local copies */
	psTmp->ui32Write = ui32LWrite ;
	psTmp->ui32Pending = ui32LPending ;

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLStreamWrite(IMG_HANDLE hStream, IMG_UINT8 *pui8Src, IMG_UINT32 ui32Size)
{
	IMG_BYTE *pbyDest = IMG_NULL;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	if ( IMG_NULL == hStream )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	eError = TLStreamReserve(hStream, &pbyDest, ui32Size);
	if ( PVRSRV_OK != eError ) 
	{	
		PVR_DPF_RETURN_RC(eError);
	}
	else
	{
		PVR_ASSERT ( pbyDest != NULL );
		OSMemCopy((IMG_VOID*)pbyDest, (IMG_VOID*)pui8Src, ui32Size);
		eError = TLStreamCommit(hStream, ui32Size);
		if ( PVRSRV_OK != eError ) 
		{	
			PVR_DPF_RETURN_RC(eError);
		}
	}
	PVR_DPF_RETURN_OK;
}

IMG_VOID TLStreamInfo(PTL_STREAM_INFO psInfo)
{
 	IMG_DEVMEM_SIZE_T actual_req_size;
	IMG_DEVMEM_ALIGN_T align = 4; /* Low dummy value so the real value can be obtained */

 	actual_req_size = 2; 
	DevmemExportalignAdjustSizeAndAlign(IMG_NULL, &actual_req_size, &align);

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

	if ( IMG_NULL == psStream )
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

	if ( IMG_NULL == psStream )
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}
	psTmp = (PTL_STREAM)psStream;
	
	/* Signal clients only when data is available to read */
	if (psTmp->ui32Read != psTmp->ui32Write)
	{
		eError = OSEventObjectSignal(psTmp->psNode->hDataEventObj);
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

IMG_VOID
TLStreamAdvanceReadPos(PTL_STREAM psStream, IMG_UINT32 uiReadLen)
{
	PVR_DPF_ENTERED;

	PVR_ASSERT(psStream);

	/* Get a local copy of the stream buffer parameters */
	psStream->ui32Read = (psStream->ui32Read + uiReadLen) % psStream->ui32Size;

	/* If this is a blocking reserve stream, 
	 * notify reserves that may be pending */
	if(psStream->bBlock)
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

DEVMEM_EXPORTCOOKIE*
TLStreamGetBufferCookie(PTL_STREAM psStream)
{
	PVR_DPF_ENTERED;

	PVR_ASSERT(psStream);

	PVR_DPF_RETURN_VAL(&psStream->sExportCookie);
}

IMG_BOOL
TLStreamEOS(PTL_STREAM psStream)
{
	PVR_DPF_ENTERED;

	PVR_ASSERT(psStream);

	/* If both pointers are equal then the buffer is empty */
	PVR_DPF_RETURN_VAL( psStream->ui32Read == psStream->ui32Write );
}

