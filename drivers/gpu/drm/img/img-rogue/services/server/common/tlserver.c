/*************************************************************************/ /*!
@File
@Title          KM server Transport Layer implementation
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Main bridge APIs for Transport Layer client functions
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

#include "img_defs.h"

/*#define PVR_DPF_FUNCTION_TRACE_ON 1*/
#undef PVR_DPF_FUNCTION_TRACE_ON
#include "pvr_debug.h"

#include "connection_server.h"
#include "allocmem.h"
#include "devicemem.h"

#include "tlintern.h"
#include "tlstream.h"
#include "tlserver.h"

#include "pvrsrv_tlstreams.h"
#define NO_STREAM_WAIT_PERIOD_US 2000000ULL
#define NO_DATA_WAIT_PERIOD_US    500000ULL
#define NO_ACQUIRE               0xffffffffU


/*
 * Transport Layer Client API Kernel-Mode bridge implementation
 */
PVRSRV_ERROR
TLServerOpenStreamKM(const IMG_CHAR*   pszName,
                     IMG_UINT32        ui32Mode,
                     PTL_STREAM_DESC*  ppsSD,
                     PMR**             ppsTLPMR)
{
	PVRSRV_ERROR	eError = PVRSRV_OK;
	PVRSRV_ERROR	eErrorEO = PVRSRV_OK;
	PTL_SNODE		psNode;
	PTL_STREAM		psStream;
	TL_STREAM_DESC *psNewSD = NULL;
	IMG_HANDLE		hEvent;
	IMG_BOOL		bIsWriteOnly = ui32Mode & PVRSRV_STREAM_FLAG_OPEN_WO ?
	                               IMG_TRUE : IMG_FALSE;
	IMG_BOOL		bResetOnOpen = ui32Mode & PVRSRV_STREAM_FLAG_RESET_ON_OPEN ?
	                               IMG_TRUE : IMG_FALSE;
	IMG_BOOL		bNoOpenCB    = ui32Mode & PVRSRV_STREAM_FLAG_IGNORE_OPEN_CALLBACK ?
	                               IMG_TRUE : IMG_FALSE;
	PTL_GLOBAL_DATA psGD = TLGGD();

#if defined(PVR_DPF_FUNCTION_TRACE_ON)
	PVR_DPF((PVR_DBG_CALLTRACE, "--> %s:%d entered (%s, %x)", __func__, __LINE__, pszName, ui32Mode));
#endif

	PVR_ASSERT(pszName);

	/* Acquire TL_GLOBAL_DATA lock here, as if the following TLFindStreamNodeByName
	 * returns NON NULL PTL_SNODE, we try updating the global data client count and
	 * PTL_SNODE's psRDesc and we want to make sure the TL_SNODE is valid (eg. has
	 * not been deleted) while we are updating it
	 */
	OSLockAcquire (psGD->hTLGDLock);

	psNode = TLFindStreamNodeByName(pszName);
	if ((psNode == NULL) && (ui32Mode & PVRSRV_STREAM_FLAG_OPEN_WAIT))
	{	/* Blocking code to wait for stream to be created if it does not exist */
		eError = OSEventObjectOpen(psGD->hTLEventObj, &hEvent);
		PVR_LOG_GOTO_IF_ERROR (eError, "OSEventObjectOpen", e0);

		do
		{
			if ((psNode = TLFindStreamNodeByName(pszName)) == NULL)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "Stream %s does not exist, waiting...", pszName));

				/* Release TL_GLOBAL_DATA lock before sleeping */
				OSLockRelease (psGD->hTLGDLock);

				/* Will exit OK or with timeout, both cases safe to ignore */
				eErrorEO = OSEventObjectWaitTimeout(hEvent, NO_STREAM_WAIT_PERIOD_US);

				/* Acquire lock after waking up */
				OSLockAcquire (psGD->hTLGDLock);
			}
		}
		while ((psNode == NULL) && (eErrorEO == PVRSRV_OK));

		eError = OSEventObjectClose(hEvent);
		PVR_LOG_GOTO_IF_ERROR (eError, "OSEventObjectClose", e0);
	}

	/* Make sure we have found a stream node after wait/search */
	if (psNode == NULL)
	{
		/* Did we exit the wait with timeout, inform caller */
		if (eErrorEO == PVRSRV_ERROR_TIMEOUT)
		{
			eError = eErrorEO;
		}
		else
		{
			eError = PVRSRV_ERROR_NOT_FOUND;
			PVR_DPF((PVR_DBG_ERROR, "Stream \"%s\" does not exist", pszName));
		}
		goto e0;
	}

	psStream = psNode->psStream;

	/* Allocate memory for the stream. The memory will be allocated with the
	 * first call. */
	eError = TLAllocSharedMemIfNull(psStream);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to allocate memory for stream"
				" \"%s\"", pszName));
		goto e0;
	}

	if (bIsWriteOnly)
	{

		/* If psWDesc == NULL it means that this is the first attempt
		 * to open stream for write. If yes create the descriptor or increment
		 * reference count otherwise. */
		if (psNode->psWDesc == NULL)
		{
			psNewSD = TLMakeStreamDesc(psNode, ui32Mode, NULL);
			psNode->psWDesc = psNewSD;
		}
		else
		{
			psNewSD = psNode->psWDesc;
			psNode->psWDesc->uiRefCount++;
		}

		PVR_LOG_GOTO_IF_NOMEM(psNewSD, eError, e0);

		psNode->uiWRefCount++;
	}
	else
	{
		/* Only one reader per stream supported */
		if (psNode->psRDesc != NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "Cannot open \"%s\" stream, stream already"
			        " opened", pszName));
			eError = PVRSRV_ERROR_ALREADY_OPEN;
			goto e0;
		}

		/* Create an event handle for this client to wait on when no data in
		 * stream buffer. */
		eError = OSEventObjectOpen(psNode->hReadEventObj, &hEvent);
		if (eError != PVRSRV_OK)
		{
			PVR_LOG_ERROR(eError, "OSEventObjectOpen");
			eError = PVRSRV_ERROR_UNABLE_TO_CREATE_EVENT;
			goto e0;
		}

		psNewSD = TLMakeStreamDesc(psNode, ui32Mode, hEvent);
		psNode->psRDesc = psNewSD;

		if (!psNewSD)
		{
			PVR_DPF((PVR_DBG_ERROR, "Not possible to make a new stream descriptor"));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto e1;
		}

		PVR_DPF((PVR_DBG_VERBOSE,
		        "TLServerOpenStreamKM evList=%p, evObj=%p",
		        psNode->hReadEventObj,
		        psNode->psRDesc->hReadEvent));
	}

	/* Copy the import handle back to the user mode API to enable access to
	 * the stream buffer from user-mode process. */
	eError = DevmemLocalGetImportHandle(TLStreamGetBufferPointer(psStream),
	                                    (void**) ppsTLPMR);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemLocalGetImportHandle", e2);

	psGD->uiClientCnt++;

	/* Global data updated. Now release global lock */
	OSLockRelease (psGD->hTLGDLock);

	*ppsSD = psNewSD;

	if (bResetOnOpen)
	{
		TLStreamReset(psStream);
	}

	/* This callback is executed only on reader open. There are some actions
	 * executed on reader open that don't make much sense for writers e.g.
	 * injection on time synchronisation packet into the stream. */
	if (!bIsWriteOnly && psStream->pfOnReaderOpenCallback != NULL && !bNoOpenCB)
	{
		psStream->pfOnReaderOpenCallback(psStream->pvOnReaderOpenUserData);
	}

	/* psNode->uiWRefCount is set to '1' on stream create so the first open
	 * is '2'. */
	if (bIsWriteOnly && psStream->psNotifStream != NULL &&
	    psNode->uiWRefCount == 2)
	{
		TLStreamMarkStreamOpen(psStream);
	}

	PVR_DPF((PVR_DBG_MESSAGE, "%s: Stream %s opened for %s", __func__, pszName,
	        ui32Mode & PVRSRV_STREAM_FLAG_OPEN_WO ? "write" : "read"));

	PVR_DPF_RETURN_OK;

e2:
	OSFreeMem(psNewSD);
e1:
	if (!bIsWriteOnly)
		OSEventObjectClose(hEvent);
e0:
	OSLockRelease (psGD->hTLGDLock);
	PVR_DPF_RETURN_RC (eError);
}

PVRSRV_ERROR
TLServerCloseStreamKM(PTL_STREAM_DESC psSD)
{
	PVRSRV_ERROR    eError = PVRSRV_OK;
	PTL_GLOBAL_DATA psGD = TLGGD();
	PTL_SNODE		psNode;
	PTL_STREAM	psStream;
	IMG_BOOL	bDestroyStream;
	IMG_BOOL	bIsWriteOnly = psSD->ui32Flags & PVRSRV_STREAM_FLAG_OPEN_WO ?
	                           IMG_TRUE : IMG_FALSE;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psSD);

	/* Quick exit if there are no streams */
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}

	/* Check stream still valid */
	psNode = TLFindStreamNodeByDesc(psSD);
	if ((psNode == NULL) || (psNode != psSD->psNode))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}

	/* Since the descriptor is valid, the stream should not have been made NULL */
	PVR_ASSERT (psNode->psStream);

	/* Save the stream's reference in-case its destruction is required after this
	 * client is removed */
	psStream = psNode->psStream;

	/* Acquire TL_GLOBAL_DATA lock as the following TLRemoveDescAndTryFreeStreamNode
	 * call will update the TL_SNODE's descriptor value */
	OSLockAcquire (psGD->hTLGDLock);

	/* Close event handle because event object list might be destroyed in
	 * TLUnrefDescAndTryFreeStreamNode(). */
	if (!bIsWriteOnly)
	{
		/* Reset the read position on close if the stream requires it. */
		TLStreamResetReadPos(psStream);

		/* Close and free the event handle resource used by this descriptor */
		eError = OSEventObjectClose(psSD->hReadEvent);
		if (eError != PVRSRV_OK)
		{
			/* Log error but continue as it seems best */
			PVR_LOG_ERROR(eError, "OSEventObjectClose");
			eError = PVRSRV_ERROR_UNABLE_TO_DESTROY_EVENT;
		}
	}
	else if (psNode->uiWRefCount == 2 && psStream->psNotifStream != NULL)
	{
		/* psNode->uiWRefCount is set to '1' on stream create so the last close
		 * before destruction is '2'. */
		TLStreamMarkStreamClose(psStream);
	}

	/* Remove descriptor from stream object/list */
	bDestroyStream = TLUnrefDescAndTryFreeStreamNode (psNode, psSD);

	/* Check the counter is sensible after input data validated. */
	PVR_ASSERT(psGD->uiClientCnt > 0);
	psGD->uiClientCnt--;

	OSLockRelease (psGD->hTLGDLock);

	/* Destroy the stream if its TL_SNODE was removed from TL_GLOBAL_DATA */
	if (bDestroyStream)
	{
		TLStreamDestroy (psStream);
		psStream = NULL;
	}

	PVR_DPF((PVR_DBG_VERBOSE, "%s: Stream closed", __func__));

	/* Free the descriptor if ref count reaches 0. */
	if (psSD->uiRefCount == 0)
	{
		/* Free the stream descriptor object */
		OSFreeMem(psSD);
	}

	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR
TLServerReserveStreamKM(PTL_STREAM_DESC psSD,
                        IMG_UINT32* ui32BufferOffset,
                        IMG_UINT32 ui32Size,
                        IMG_UINT32 ui32SizeMin,
                        IMG_UINT32* pui32Available)
{
	TL_GLOBAL_DATA* psGD = TLGGD();
	PTL_SNODE psNode;
	IMG_UINT8* pui8Buffer = NULL;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psSD);

	if (!(psSD->ui32Flags & PVRSRV_STREAM_FLAG_OPEN_WO))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	/* Quick exit if there are no streams */
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	/* Acquire the global lock. We have to be sure that no one modifies
	 * the list while we are looking for our stream. */
	OSLockAcquire(psGD->hTLGDLock);
	/* Check stream still valid */
	psNode = TLFindAndGetStreamNodeByDesc(psSD);
	OSLockRelease(psGD->hTLGDLock);

	if ((psNode == NULL) || (psNode != psSD->psNode))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}


	/* Since we have a valid stream descriptor, the stream should not have been
	 * made NULL by any producer context. */
	PVR_ASSERT (psNode->psStream);

	/* The TL writers that currently land here are at a very low to none risk
	 * to breach max TL packet size constraint (even if there is no reader
	 * connected to the TL stream and hence eventually will cause the TL stream
	 * to be full). Hence no need to know the status of TL stream reader
	 * connection.
	 */
	eError = TLStreamReserve2(psNode->psStream, &pui8Buffer, ui32Size,
				  ui32SizeMin, pui32Available, NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "Failed to reserve %u (%u, %u) bytes in the stream, error %s.",
				ui32Size, ui32SizeMin, *pui32Available, PVRSRVGETERRORSTRING(eError)));
	}
	else if (pui8Buffer == NULL)
	{
		PVR_DPF((PVR_DBG_WARNING, "Not enough space in the stream."));
		eError = PVRSRV_ERROR_STREAM_FULL;
	}
	else
	{
		*ui32BufferOffset = pui8Buffer - psNode->psStream->pbyBuffer;
		PVR_ASSERT(*ui32BufferOffset < psNode->psStream->ui32Size);
	}

	OSLockAcquire(psGD->hTLGDLock);
	TLReturnStreamNode(psNode);
	OSLockRelease(psGD->hTLGDLock);

	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR
TLServerCommitStreamKM(PTL_STREAM_DESC psSD,
                       IMG_UINT32 ui32Size)
{
	TL_GLOBAL_DATA*	psGD = TLGGD();
	PTL_SNODE psNode;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psSD);

	if (!(psSD->ui32Flags & PVRSRV_STREAM_FLAG_OPEN_WO))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	/* Quick exit if there are no streams */
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	/* Acquire the global lock. We have to be sure that no one modifies
	 * the list while we are looking for our stream. */
	OSLockAcquire(psGD->hTLGDLock);
	/* Check stream still valid */
	psNode = TLFindAndGetStreamNodeByDesc(psSD);
	OSLockRelease(psGD->hTLGDLock);

	if ((psNode == NULL) || (psNode != psSD->psNode))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}

	/* Since we have a valid stream descriptor, the stream should not have been
	 * made NULL by any producer context. */
	PVR_ASSERT (psNode->psStream);

	eError = TLStreamCommit(psNode->psStream, ui32Size);
	PVR_LOG_IF_ERROR(eError, "TLStreamCommit");

	OSLockAcquire(psGD->hTLGDLock);
	TLReturnStreamNode(psNode);
	OSLockRelease(psGD->hTLGDLock);

	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR
TLServerDiscoverStreamsKM(const IMG_CHAR *pszNamePattern,
                          IMG_UINT32 ui32Size,
                          IMG_CHAR *pszStreams,
                          IMG_UINT32 *pui32NumFound)
{
	PTL_SNODE psNode = NULL;
	IMG_CHAR (*paszStreams)[PRVSRVTL_MAX_STREAM_NAME_SIZE] =
			(IMG_CHAR (*)[PRVSRVTL_MAX_STREAM_NAME_SIZE]) (void *)pszStreams;

	if (*pszNamePattern == '\0')
		return PVRSRV_ERROR_INVALID_PARAMS;

	if (ui32Size % PRVSRVTL_MAX_STREAM_NAME_SIZE != 0)
		return PVRSRV_ERROR_INVALID_PARAMS;

	/* Quick exit if there are no streams */
	if (TLGGD()->psHead == NULL)
	{
		*pui32NumFound = 0;
		return PVRSRV_OK;
	}

	OSLockAcquire(TLGGD()->hTLGDLock);

	*pui32NumFound = TLDiscoverStreamNodes(pszNamePattern, paszStreams,
	                                  ui32Size / PRVSRVTL_MAX_STREAM_NAME_SIZE);

	/* Find "tlctrl" stream and reset it */
	psNode = TLFindStreamNodeByName(PVRSRV_TL_CTLR_STREAM);
	if (psNode != NULL)
		TLStreamReset(psNode->psStream);

	OSLockRelease(TLGGD()->hTLGDLock);

	return PVRSRV_OK;
}

PVRSRV_ERROR
TLServerAcquireDataKM(PTL_STREAM_DESC psSD,
                      IMG_UINT32*     puiReadOffset,
                      IMG_UINT32*     puiReadLen)
{
	PVRSRV_ERROR    eError = PVRSRV_OK;
	TL_GLOBAL_DATA* psGD = TLGGD();
	IMG_UINT32      uiTmpOffset;
	IMG_UINT32      uiTmpLen = 0;
	PTL_SNODE       psNode;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psSD);

	TL_COUNTER_INC(psSD->ui32AcquireCount);

	/* Quick exit if there are no streams */
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	/* Check stream still valid */
	psNode = TLFindStreamNodeByDesc(psSD);
	if ((psNode == NULL) || (psNode != psSD->psNode))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}

	/* If we are here, the stream will never be made NULL until this context itself
	 * calls TLRemoveDescAndTryFreeStreamNode(). This is because the producer will
	 * fail to make the stream NULL (by calling TLTryRemoveStreamAndFreeStreamNode)
	 * when a valid stream descriptor is present (i.e. a client is connected).
	 * Hence, no checks for stream being NON NULL are required after this. */
	PVR_ASSERT (psNode->psStream);

	psSD->ui32ReadLen = 0;	/* Handle NULL read returns */

	do
	{
		uiTmpLen = TLStreamAcquireReadPos(psNode->psStream, psSD->ui32Flags & PVRSRV_STREAM_FLAG_DISABLE_PRODUCER_CALLBACK, &uiTmpOffset);

		/* Check we have not already exceeded read limit with just offset
		 * regardless of data length to ensure the client sees the RC */
		if (psSD->ui32Flags & PVRSRV_STREAM_FLAG_READ_LIMIT)
		{
			/* Check to see if we are reading beyond the read limit */
			if (uiTmpOffset >= psSD->ui32ReadLimit)
			{
				PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_READLIMIT_REACHED);
			}
		}

		if (uiTmpLen > 0)
		{ /* Data found */

			/* Check we have not already exceeded read limit offset+len */
			if (psSD->ui32Flags & PVRSRV_STREAM_FLAG_READ_LIMIT)
			{
				/* Adjust the read length if it goes beyond the read limit
				 * limit always guaranteed to be on packet */
				if ((uiTmpOffset + uiTmpLen) >= psSD->ui32ReadLimit)
				{
					uiTmpLen = psSD->ui32ReadLimit - uiTmpOffset;
				}
			}

			*puiReadOffset = uiTmpOffset;
			*puiReadLen = uiTmpLen;
			psSD->ui32ReadLen = uiTmpLen;	/* Save the original data length in the stream desc */
			PVR_DPF_RETURN_OK;
		}
		else if (!(psSD->ui32Flags & PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING))
		{ /* No data found blocking */

			/* Instead of doing a complete sleep for `NO_DATA_WAIT_PERIOD_US` us, we sleep in chunks
			 * of 168 ms. In a "deferred" signal scenario from writer, this gives us a chance to
			 * wake-up (timeout) early and continue reading in-case some data is available */
			IMG_UINT64 ui64WaitInChunksUs = MIN(NO_DATA_WAIT_PERIOD_US, 168000ULL);
			IMG_BOOL bDataFound = IMG_FALSE;

			TL_COUNTER_INC(psSD->ui32NoDataSleep);

			LOOP_UNTIL_TIMEOUT(NO_DATA_WAIT_PERIOD_US)
			{
				eError = OSEventObjectWaitTimeout(psSD->hReadEvent, ui64WaitInChunksUs);
				if (eError == PVRSRV_OK)
				{
					bDataFound = IMG_TRUE;
					TL_COUNTER_INC(psSD->ui32Signalled);
					break;
				}
				else if (eError == PVRSRV_ERROR_TIMEOUT)
				{
					if (TLStreamOutOfData(psNode->psStream))
					{
						/* Return on timeout if stream empty, else let while exit and return data */
						continue;
					}
					else
					{
						bDataFound = IMG_TRUE;
						TL_COUNTER_INC(psSD->ui32TimeoutData);
						PVR_DPF((PVR_DBG_MESSAGE, "%s: Data found at timeout. Current BuffUt = %u",
												 __func__, TLStreamGetUT(psNode->psStream)));
						break;
					}
				}
				else
				{ /* Some other system error with event objects */
					PVR_DPF_RETURN_RC(eError);
				}
			} END_LOOP_UNTIL_TIMEOUT();

			if (bDataFound)
			{
				continue;
			}
			else
			{
				TL_COUNTER_INC(psSD->ui32TimeoutEmpty);
				return PVRSRV_ERROR_TIMEOUT;
			}
		}
		else
		{ /* No data non-blocking */
			TL_COUNTER_INC(psSD->ui32NoData);

			/* When no-data in non-blocking mode, uiReadOffset should be set to NO_ACQUIRE
			 * signifying there's no need of Release call */
			*puiReadOffset = NO_ACQUIRE;
			*puiReadLen = 0;
			PVR_DPF_RETURN_OK;
		}
	}
	while (1);
}

PVRSRV_ERROR
TLServerReleaseDataKM(PTL_STREAM_DESC psSD,
                      IMG_UINT32      uiReadOffset,
                      IMG_UINT32      uiReadLen)
{
	TL_GLOBAL_DATA* psGD = TLGGD();
	PTL_SNODE psNode;

	PVR_DPF_ENTERED;

	/* Unreferenced in release builds */
	PVR_UNREFERENCED_PARAMETER(uiReadOffset);

	PVR_ASSERT(psSD);

	/* Quick exit if there are no streams */
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	if ((uiReadLen % PVRSRVTL_PACKET_ALIGNMENT != 0))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	/* Check stream still valid */
	psNode = TLFindStreamNodeByDesc(psSD);
	if ((psNode == NULL) || (psNode != psSD->psNode))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}

	/* Since we have a valid stream descriptor, the stream should not have been
	 * made NULL by any producer context. */
	PVR_ASSERT (psNode->psStream);

	PVR_DPF((PVR_DBG_VERBOSE, "TLReleaseDataKM uiReadOffset=%d, uiReadLen=%d", uiReadOffset, uiReadLen));

	/* Move read position on to free up space in stream buffer */
	PVR_DPF_RETURN_RC(TLStreamAdvanceReadPos(psNode->psStream, uiReadLen, psSD->ui32ReadLen));
}

PVRSRV_ERROR
TLServerWriteDataKM(PTL_STREAM_DESC psSD,
                    IMG_UINT32 ui32Size,
                    IMG_BYTE* pui8Data)
{
	TL_GLOBAL_DATA* psGD = TLGGD();
	PTL_SNODE psNode;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psSD);

	if (!(psSD->ui32Flags & PVRSRV_STREAM_FLAG_OPEN_WO))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	/* Quick exit if there are no streams */
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	OSLockAcquire(psGD->hTLGDLock);
	/* Check stream still valid */
	psNode = TLFindAndGetStreamNodeByDesc(psSD);
	OSLockRelease(psGD->hTLGDLock);

	if ((psNode == NULL) || (psNode != psSD->psNode))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}

	/* Since we have a valid stream descriptor, the stream should not have been
	 * made NULL by any producer context. */
	PVR_ASSERT (psNode->psStream);

	eError = TLStreamWrite(psNode->psStream, pui8Data, ui32Size);
	PVR_LOG_IF_ERROR(eError, "TLStreamWrite");

	OSLockAcquire(psGD->hTLGDLock);
	TLReturnStreamNode(psNode);
	OSLockRelease(psGD->hTLGDLock);

	PVR_DPF_RETURN_RC(eError);
}

/******************************************************************************
 End of file (tlserver.c)
******************************************************************************/
