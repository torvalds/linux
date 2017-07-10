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
#include <stddef.h>

#include "img_defs.h"

//#define PVR_DPF_FUNCTION_TRACE_ON 1
#undef PVR_DPF_FUNCTION_TRACE_ON
#include "pvr_debug.h"

#include "connection_server.h"
#include "allocmem.h"
#include "devicemem.h"

#include "tlintern.h"
#include "tlstream.h"
#include "tlserver.h"

#define NO_STREAM_WAIT_PERIOD 2000000ULL
#define NO_DATA_WAIT_PERIOD   1000000ULL
#define NO_ACQUIRE            0xffffffffU

#include "rgxhwperf.h"

/*
 * Transport Layer Client API Kernel-Mode bridge implementation
 */
PVRSRV_ERROR
TLServerOpenStreamKM(const IMG_CHAR*  	 	   pszName,
			   	     IMG_UINT32 		   ui32Mode,
			   	     PTL_STREAM_DESC* 	   ppsSD,
			   	     PMR** 				   ppsTLPMR)
{
	PVRSRV_ERROR 	eError = PVRSRV_OK;
	PVRSRV_ERROR 	eErrorEO = PVRSRV_OK;
	PTL_SNODE		psNode = 0;
	TL_STREAM_DESC* psNewSD = 0;
	IMG_HANDLE 		hEvent;
	IMG_BOOL		bIsWriteOnly = ui32Mode & PVRSRV_STREAM_FLAG_OPEN_WO ?
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
		PVR_LOGG_IF_ERROR (eError, "OSEventObjectOpen", e0);

		do
		{
			if ((psNode = TLFindStreamNodeByName(pszName)) == NULL)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "Stream %s does not exist, waiting...", pszName));
				
				/* Release TL_GLOBAL_DATA lock before sleeping */
				OSLockRelease (psGD->hTLGDLock);

				/* Will exit OK or with timeout, both cases safe to ignore */
				eErrorEO = OSEventObjectWaitTimeout(hEvent, NO_STREAM_WAIT_PERIOD);
				
				/* Acquire lock after waking up */
				OSLockAcquire (psGD->hTLGDLock);
			}
		}
		while ((psNode == NULL) && (eErrorEO == PVRSRV_OK));

		eError = OSEventObjectClose(hEvent);
		PVR_LOGG_IF_ERROR (eError, "OSEventObjectClose", e0);
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

	/* Allocate memory for the stream. The memory will be allocated with the
	 * first call. */
	eError = TLAllocSharedMemIfNull(psNode->psStream);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to allocate memory for stream"
				" \"%s\"", pszName));
		return eError;
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

		if (!psNewSD)
		{
			PVR_DPF((PVR_DBG_ERROR, "Not possible to make a new stream"
			        " writer descriptor"));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto e1;
		}

		psNode->uiWRefCount++;
	}
	else
	{
		// Only one reader per stream supported
		if (psNode->psRDesc != NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "Cannot open \"%s\" stream, stream already"
			        " opened", pszName));
			eError = PVRSRV_ERROR_ALREADY_OPEN;
			goto e0;
		}

		// Create an event handle for this client to wait on when no data in
		// stream buffer.
		eError = OSEventObjectOpen(psNode->hReadEventObj, &hEvent);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Not possible to open node's event object"));
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

	// Copy the import handle back to the user mode API to enable access to
	// the stream buffer from user-mode process.
	eError = DevmemLocalGetImportHandle(TLStreamGetBufferPointer(psNode->psStream), (void**) ppsTLPMR);
	PVR_LOGG_IF_ERROR(eError, "DevmemLocalGetImportHandle", e2);

	psGD->uiClientCnt++;

	/* Global data updated. Now release global lock */
	OSLockRelease (psGD->hTLGDLock);

	*ppsSD = psNewSD;

	/* This callback is executed only on reader open. There are some actions
	 * executed on reader open that don't make much sense for writers e.g.
	 * injection on time synchronisation packet into the stream. */
	if (!bIsWriteOnly && psNode->psStream->pfOnReaderOpenCallback != NULL)
	{
		psNode->psStream->pfOnReaderOpenCallback(
		        psNode->psStream->pvOnReaderOpenUserData);
	}

	if (bIsWriteOnly)
	{
		/* Sending HWPerf event from TL is a temporary solution and this
		 * will change once TL is expanded by event allowing to signal
		 * stream opening. */
		RGX_HWPERF_HOST_CTRL(CLIENT_STREAM_OPEN,
		                     OSGetCurrentClientProcessIDKM());
	}

	PVR_DPF((PVR_DBG_MESSAGE, "%s: Stream %s opened for %s", __func__, pszName,
	        ui32Mode & PVRSRV_STREAM_FLAG_OPEN_WO ? "write" : "read"));

	PVR_DPF_RETURN_OK;

e2:
	OSFreeMem(psNewSD);
e1:
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
	PTL_SNODE		psNode = 0;
	PTL_STREAM	psStream;
	IMG_BOOL	bDestroyStream;
	IMG_BOOL	bIsWriteOnly = psSD->ui32Flags & PVRSRV_STREAM_FLAG_OPEN_WO ?
	                           IMG_TRUE : IMG_FALSE;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psSD);

	// Sanity check, quick exit if there are no streams
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}

	// Check stream still valid
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
		// Close and free the event handle resource used by this descriptor
		eError = OSEventObjectClose(psSD->hReadEvent);
		if (eError != PVRSRV_OK)
		{
			// Log error but continue as it seems best
			PVR_DPF((PVR_DBG_ERROR, "OSEventObjectClose() failed error %d",
			        eError));
			eError = PVRSRV_ERROR_UNABLE_TO_DESTROY_EVENT;
		}
	}
	else
	{
		/* Sending HWPerf event from TL is a temporary solution and this
		 * will change once TL is expanded by event allowing to signal
		 * stream closing. */
		RGX_HWPERF_HOST_CTRL(CLIENT_STREAM_CLOSE,
		                     OSGetCurrentClientProcessIDKM());
	}

	// Remove descriptor from stream object/list
	bDestroyStream = TLUnrefDescAndTryFreeStreamNode (psNode, psSD);

	// Assert the counter is sane after input data validated.
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
		// Free the stream descriptor object
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
	PTL_SNODE psNode = 0;
	IMG_UINT8* pui8Buffer = NULL;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psSD);

	if (!(psSD->ui32Flags & PVRSRV_STREAM_FLAG_OPEN_WO))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	// Sanity check, quick exit if there are no streams
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	/* Acquire the global lock. We have to be sure that no one modifies
	 * the list while we are looking for our stream. */
	OSLockAcquire(psGD->hTLGDLock);
	// Check stream still valid
	psNode = TLFindAndGetStreamNodeByDesc(psSD);
	OSLockRelease(psGD->hTLGDLock);

	if ((psNode == NULL) || (psNode != psSD->psNode))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}


	/* Since we have a valid stream descriptor, the stream should not have been
	 * made NULL by any producer context. */
	PVR_ASSERT (psNode->psStream);

	eError = TLStreamReserve2(psNode->psStream, &pui8Buffer, ui32Size,
	                          ui32SizeMin, pui32Available);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "Failed to reserve the stream (%d).", eError));
	}
	else if (pui8Buffer == NULL)
	{
		PVR_DPF((PVR_DBG_WARNING, "Not enough space in the stream."));
		eError = PVRSRV_ERROR_STREAM_RESERVE_TOO_BIG;
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
	PTL_SNODE psNode = 0;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psSD);

	if (!(psSD->ui32Flags & PVRSRV_STREAM_FLAG_OPEN_WO))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	// Sanity check, quick exit if there are no streams
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	/* Acquire the global lock. We have to be sure that no one modifies
	 * the list while we are looking for our stream. */
	OSLockAcquire(psGD->hTLGDLock);
	// Check stream still valid
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
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to commit data into stream."));
	}

	OSLockAcquire(psGD->hTLGDLock);
	TLReturnStreamNode(psNode);
	OSLockRelease(psGD->hTLGDLock);

	PVR_DPF_RETURN_RC(eError);
}

PVRSRV_ERROR
TLServerDiscoverStreamsKM(const IMG_CHAR *pszNamePattern,
                          IMG_UINT32 ui32Max,
                          IMG_UINT32 *pui32Streams,
                          IMG_UINT32 *pui32NumFound)
{
	if (*pszNamePattern == '\0')
		return PVRSRV_ERROR_INVALID_PARAMS;

	// Sanity check, quick exit if there are no streams
	if (TLGGD()->psHead == NULL)
	{
		*pui32NumFound = 0;
		return PVRSRV_OK;
	}

	OSLockAcquire(TLGGD()->hTLGDLock);
	*pui32NumFound = TLDiscoverStreamNodes(pszNamePattern, pui32Streams,
	                                       ui32Max);
	OSLockRelease(TLGGD()->hTLGDLock);

	return PVRSRV_OK;
}

PVRSRV_ERROR
TLServerAcquireDataKM(PTL_STREAM_DESC psSD,
		   	   		  IMG_UINT32*	  puiReadOffset,
		   	   		  IMG_UINT32* 	  puiReadLen)
{
	PVRSRV_ERROR 		eError = PVRSRV_OK;
	TL_GLOBAL_DATA*		psGD = TLGGD();
	IMG_UINT32		    uiTmpOffset = NO_ACQUIRE;
	IMG_UINT32  		uiTmpLen = 0;
	PTL_SNODE			psNode = 0;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psSD);

	// Sanity check, quick exit if there are no streams
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	// Check stream still valid
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
	
	//PVR_DPF((PVR_DBG_VERBOSE, "TLServerAcquireDataKM evList=%p, evObj=%p", psSD->psNode->hReadEventObj, psSD->hReadEvent));

	/* Check for data in the associated stream buffer, sleep/wait if none */
	while (((uiTmpLen = TLStreamAcquireReadPos(psNode->psStream, &uiTmpOffset)) == 0) &&
	       (!(psSD->ui32Flags&PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING)) )
	{
		PVR_DPF((PVR_DBG_VERBOSE, "TLAcquireDataKM sleeping..."));

		// Loop around if EndOfStream (nothing to read) and wait times out,
		// exit loop if not time out but data is ready for client
		while (TLStreamEOS(psNode->psStream))
		{
			eError = OSEventObjectWaitTimeout(psSD->hReadEvent, NO_DATA_WAIT_PERIOD);
			if (eError != PVRSRV_OK)
			{
				/* Return timeout or other error condition to the caller who
				 * can choose to call again if desired. We don't block
				 * Indefinitely as we want the user mode application to have a
				 * chance to break out and end if it needs to, so we return the
				 * time out error code. */
				PVR_DPF((PVR_DBG_VERBOSE, "TL Server timed out"));
				PVR_DPF_RETURN_RC(eError);
			}
			else
			{
				PVR_DPF((PVR_DBG_VERBOSE, "TL Server signalled"));
			}
		}
	}

	/* Data available now if we reach here in blocking more or we take the
	 * values as is in non-blocking mode which might be all zeros. */
	*puiReadOffset = uiTmpOffset;
	*puiReadLen = uiTmpLen;

	PVR_DPF((PVR_DBG_VERBOSE, "TLAcquireDataKM return offset=%d, len=%d bytes", *puiReadOffset, *puiReadLen));

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLServerReleaseDataKM(PTL_STREAM_DESC psSD,
		 	 		  IMG_UINT32  	  uiReadOffset,
		 	 		  IMG_UINT32  	  uiReadLen)
{
	TL_GLOBAL_DATA*		psGD = TLGGD();
	PTL_SNODE			psNode = 0;

	PVR_DPF_ENTERED;

	/* Unreferenced in release builds */
	PVR_UNREFERENCED_PARAMETER(uiReadOffset);

	PVR_ASSERT(psSD);

	// Sanity check, quick exit if there are no streams
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	// Check stream still valid
	psNode = TLFindStreamNodeByDesc(psSD);
	if ((psNode == NULL) || (psNode != psSD->psNode))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_HANDLE_NOT_FOUND);
	}

	/* Since we have a valid stream descriptor, the stream should not have been
	 * made NULL by any producer context. */
	PVR_ASSERT (psNode->psStream);

	PVR_DPF((PVR_DBG_VERBOSE, "TLReleaseDataKM uiReadOffset=%d, uiReadLen=%d", uiReadOffset, uiReadLen));

	// Move read position on to free up space in stream buffer
	TLStreamAdvanceReadPos(psNode->psStream, uiReadLen);

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLServerWriteDataKM(PTL_STREAM_DESC psSD,
                    IMG_UINT32 ui32Size,
                    IMG_BYTE* pui8Data)
{
	TL_GLOBAL_DATA* psGD = TLGGD();
	PTL_SNODE psNode = 0;
	PVRSRV_ERROR eError;

	PVR_DPF_ENTERED;

	PVR_ASSERT(psSD);

	if (!(psSD->ui32Flags & PVRSRV_STREAM_FLAG_OPEN_WO))
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_INVALID_PARAMS);
	}

	// Sanity check, quick exit if there are no streams
	if (psGD->psHead == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_STREAM_ERROR);
	}

	OSLockAcquire(psGD->hTLGDLock);
	// Check stream still valid
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
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to write data to the stream (%d).",
		        eError));
	}

	OSLockAcquire(psGD->hTLGDLock);
	TLReturnStreamNode(psNode);
	OSLockRelease(psGD->hTLGDLock);

	PVR_DPF_RETURN_RC(eError);
}

/*****************************************************************************
 End of file (tlserver.c)
*****************************************************************************/

