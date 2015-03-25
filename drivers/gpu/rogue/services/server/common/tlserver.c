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

#include "tlintern.h"
#include "tlserver.h"

#include "tltestdefs.h"

#define NO_STREAM_WAIT_PERIOD 2000
#define NO_DATA_WAIT_PERIOD   1000
#define NO_ACQUIRE            0xffffffffU

/*
 * Transport Layer Client API Kernel-Mode bridge implementation
 */
PVRSRV_ERROR
TLServerConnectKM(CONNECTION_DATA *psConnection)
{
	PVR_DPF_ENTERED;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLServerDisconnectKM(CONNECTION_DATA *psConnection)
{
	PVR_DPF_ENTERED;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLServerOpenStreamKM(IMG_PCHAR  	 	   pszName,
			   	     IMG_UINT32 		   ui32Mode,
			   	     PTL_STREAM_DESC* 	   ppsSD,
			   	     DEVMEM_EXPORTCOOKIE** ppsBufCookie)
{
	PVRSRV_ERROR 	eError = PVRSRV_OK;
	PVRSRV_ERROR 	eErrorEO = PVRSRV_OK;
	PTL_SNODE		psNode = 0;
	TL_STREAM_DESC* psNewSD = 0;
	IMG_HANDLE 		hEvent;
	PTL_GLOBAL_DATA psGD = TLGGD();

#if defined(PVR_DPF_FUNCTION_TRACE_ON)
    PVR_DPF((PVR_DBG_CALLTRACE, "--> %s:%d entered (%s, %x)", __func__, __LINE__, pszName, ui32Mode));
#endif

	PVR_ASSERT(pszName);

	psNode = TLFindStreamNodeByName(pszName);
	if ((psNode == NULL) && (ui32Mode & PVRSRV_STREAM_FLAG_OPEN_WAIT))
	{	/* Blocking code to wait for stream to be created if it does not exist
	     */
		eError = OSEventObjectOpen(psGD->hTLEventObj, &hEvent);
		PVR_LOGR_IF_ERROR(eError, "OSEventObjectOpen");

		do
		{
			if ((psNode = TLFindStreamNodeByName(pszName)) == NULL)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "Stream %s does not exist, waiting...", pszName));
				/* Will exit OK or with timeout, both cases safe to ignore */
				eErrorEO = OSEventObjectWaitTimeout(hEvent, NO_STREAM_WAIT_PERIOD);
			}
		}
		while ((psNode == NULL) && (eErrorEO == PVRSRV_OK));

		eError = OSEventObjectClose(hEvent);
		PVR_LOGR_IF_ERROR(eError, "OSEventObjectClose");
	}

	/* Make sure we have found a stream node after wait/search */
	if (psNode == NULL)
	{
		/* Did we exit the wait with timeout, inform caller */
		if (eErrorEO == PVRSRV_ERROR_TIMEOUT)
		{
			PVR_DPF_RETURN_RC(eErrorEO);
		}
		PVR_DPF((PVR_DBG_ERROR, "Stream does not exist"));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_NOT_FOUND);
	}

	// Only one client/descriptor per stream supported
	if (psNode->psDesc != NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "Can not open stream, stream already opened"));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_ALREADY_OPEN);
	}

	// Create an event handle for this client to wait on when no data in stream
	// buffer.
	eError = OSEventObjectOpen(psNode->hDataEventObj, &hEvent);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "Not possible to open node's event object"));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_UNABLE_TO_CREATE_EVENT);
	}

	psNewSD = TLMakeStreamDesc(psNode, ui32Mode, hEvent);
	if (!psNewSD)
	{
		PVR_DPF((PVR_DBG_ERROR, "Not possible to make a new stream descriptor"));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_OUT_OF_MEMORY);
	}

	TLGGD()->uiClientCnt++;

	// Copy the export cookie back to the user mode API to enable access to
	// the stream buffer from user-mode process.
	*ppsBufCookie = TLStreamGetBufferCookie(psNode->psStream);

	psNode->psDesc = psNewSD;
	*ppsSD = psNewSD;

	PVR_DPF((PVR_DBG_VERBOSE, 
			 "TLServerOpenStreamKM evList=%p, evObj=%p", 
			 psNode->hDataEventObj, 
			 psNode->psDesc->hDataEvent));

	PVR_DPF_RETURN_OK;
}

PVRSRV_ERROR
TLServerCloseStreamKM(PTL_STREAM_DESC psSD)
{
	PVRSRV_ERROR    eError = PVRSRV_OK;
	PTL_GLOBAL_DATA psGD = TLGGD();
	PTL_SNODE		psNode = 0;

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

	// Close and free the event handle resource used by this descriptor
	eError = OSEventObjectClose(psSD->hDataEvent);
	if (eError != PVRSRV_OK)
	{
		// Log error but continue as it seems best
		PVR_DPF((PVR_DBG_ERROR, "OSEventObjectClose() failed error %d", eError));
		eError = PVRSRV_ERROR_UNABLE_TO_DESTROY_EVENT;
	}

	// Remove descriptor from stream object/list
	TLRemoveDescAndTryFreeStreamNode(psNode);

	// Free the stream descriptor object
	OSFREEMEM(psSD);

	// Assert the counter is sane after input data validated.
	PVR_ASSERT(TLGGD()->uiClientCnt > 0);
	TLGGD()->uiClientCnt--;

	PVR_DPF_RETURN_RC(eError);
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

	// Does stream still exist?
	if (psNode->psStream == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_RESOURCE_UNAVAILABLE);
	}

	//PVR_DPF((PVR_DBG_VERBOSE, "TLServerAcquireDataKM evList=%p, evObj=%p", psSD->psNode->hDataEventObj, psSD->hDataEvent));

	/* If stream still exists (has not be nulled in the mean time),
	 * check for data in the associated stream buffer, sleep/wait if none
	 */
	while ((psNode->psStream != NULL) &&
	       ((uiTmpLen = TLStreamAcquireReadPos(psNode->psStream, &uiTmpOffset)) == 0) &&
	       (!(psSD->ui32Flags&PVRSRV_STREAM_FLAG_ACQUIRE_NONBLOCKING)) )
	{
		PVR_DPF((PVR_DBG_VERBOSE, "TLAcquireDataKM sleeping..."));

		// Loop around if EndOfStream (nothing to read) and wait times out,
		// exit loop if not time out but data is ready for client
		while ((psNode->psStream != NULL) && TLStreamEOS(psNode->psStream))
		{
			eError = OSEventObjectWaitTimeout(psSD->hDataEvent, NO_DATA_WAIT_PERIOD);
			if (eError != PVRSRV_OK)
			{
				/* Return timeout or other error condition to the caller who
				 * can choose to call again if desired. We don't block
				 * Indefinitely as we want the user mode application to have a
				 * chance to break out and end if it needs to, so we return the
				 * time out error code. */
				PVR_DPF_RETURN_RC(eError);
			}
		}
	}

	/* Check we have been woken up because the stream has been destroyed? */
	if (psNode->psStream == NULL)
	{
		PVR_DPF((PVR_DBG_VERBOSE, "TLAcquireDataKM awake, but stream now NULL"));
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_RESOURCE_UNAVAILABLE);
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

	// Does stream still exist?
	if (psNode->psStream == NULL)
	{
		PVR_DPF_RETURN_RC(PVRSRV_ERROR_RESOURCE_UNAVAILABLE);
	}

	PVR_DPF((PVR_DBG_VERBOSE, "TLReleaseDataKM uiReadOffset=%d, uiReadLen=%d", uiReadOffset, uiReadLen));

	// Move read position on to free up space in stream buffer
	TLStreamAdvanceReadPos(psNode->psStream, uiReadLen);

	PVR_DPF_RETURN_OK;
}

/*****************************************************************************
 End of file (tlserver.c)
*****************************************************************************/

