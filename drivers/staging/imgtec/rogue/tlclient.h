/*************************************************************************/ /*!
@File           tlclient.h
@Title          Services Transport Layer shared API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Transport layer common API used in both clients and server
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
 
#ifndef TLCLIENT_H_
#define TLCLIENT_H_


#include "img_defs.h"
#include "pvrsrv_error.h"


/* This value is used for the hSrvHandle argument in the client API when
 * called directly from the kernel which will lead to a direct bridge access.
 */
#define DIRECT_BRIDGE_HANDLE	((IMG_HANDLE)0xDEADBEEFU)


/**************************************************************************/ /*!
 @Function		TLClientOpenStream
 @Description	Open a descriptor onto an existing kernel transport stream.
 @Input			hSrvHandle    	Address of a pointer to a connection object
 @Input			pszName			Address of the stream name string, no longer
 	 	 	 	 	 	 	 	than PRVSRVTL_MAX_STREAM_NAME_SIZE.
 @Input			ui32Mode		Unused
 @Output		phSD			Address of a pointer to an stream object
 @Return 		PVRSRV_ERROR_NOT_FOUND:        when named stream not found
 @Return		PVRSRV_ERROR_ALREADY_OPEN:     stream already open by another
 @Return		PVRSRV_ERROR_STREAM_ERROR:     internal driver state error
 @Return        PVRSRV_ERROR_TIMEOUT:          block timed out, stream not found
 @Return		PVRSRV_ERROR:			       for other system codes
*/ /***************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR TLClientOpenStream(IMG_HANDLE hSrvHandle,
		const IMG_CHAR* pszName,
		IMG_UINT32   ui32Mode,
		IMG_HANDLE*  phSD);


/**************************************************************************/ /*!
 @Function		TLClientCloseStream
 @Description	Close and release the stream connection to Services kernel
				server transport layer. Any outstanding Acquire will be
				released.
 @Input			hSrvHandle      Address of a pointer to a connection object
 @Input			hSD				Handle of the stream object to close
 @Return		PVRSRV_ERROR_HANDLE_NOT_FOUND: when SD handle is not known
 @Return		PVRSRV_ERROR_STREAM_ERROR: 	  internal driver state error
 @Return		PVRSRV_ERROR:				  for system codes
*/ /***************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR TLClientCloseStream(IMG_HANDLE hSrvHandle,
		IMG_HANDLE hSD);

/**************************************************************************/ /*!
 @Function      TLClientDiscoverStreams
 @Description   Finds all streams that's name starts with pszNamePattern and
                ends with a number.
 @Input         hSrvHandle      Address of a pointer to a connection object
 @Input         pszNamePattern  Name pattern. Must be beginning of a string.
 @Output        pui32Streams    Array of numbers from end of the discovered
                names.
 @inOut         pui32Count      When input max number of number that can fit
                                into pui32Streams. When output number of
                                discovered streams.
 @Return		PVRSRV_ERROR    for system codes
*/ /***************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR TLClientDiscoverStreams(IMG_HANDLE hSrvHandle,
		const IMG_CHAR *pszNamePattern,
		IMG_UINT32 *pui32Streams,
		IMG_UINT32 *pui32NumFound);

/**************************************************************************/ /*!
 @Function      TLClientReserveStream
 @Description   Reserves a region with given size in the stream. If the stream
                is already reserved the function will return an error.
 @Input         hSrvHandle      Address of a pointer to a connection object
 @Input         hSD             Handle of the stream object to close
 @Output        ppui8Data       pointer to the buffer
 @Input         ui32Size        size of the data
 @Return
*/ /***************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR TLClientReserveStream(IMG_HANDLE hSrvHandle,
		IMG_HANDLE hSD,
		IMG_UINT8 **ppui8Data,
		IMG_UINT32 ui32Size);

/**************************************************************************/ /*!
 @Function      TLClientStreamReserve2
 @Description   Reserves a region with given size in the stream. If the stream
                is already reserved the function will return an error.
 @Input         hSrvHandle      Address of a pointer to a connection object
 @Input         hSD             Handle of the stream object to close
 @Output        ppui8Data       pointer to the buffer
 @Input         ui32Size        size of the data
 @Input         ui32SizeMin     minimum size of the data
 @Input         ui32Available   available space in buffer
 @Return
*/ /***************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR TLClientReserveStream2(IMG_HANDLE hSrvHandle,
		IMG_HANDLE hSD,
		IMG_UINT8 **ppui8Data,
		IMG_UINT32 ui32Size,
		IMG_UINT32 ui32SizeMin,
		IMG_UINT32 *pui32Available);

/**************************************************************************/ /*!
 @Function      TLClientStreamCommit
 @Description   Commits previously reserved region in the stream and therefore
                allows next reserves.
                This function call has to be preceded by the call to
                TLClientReserveStream or TLClientReserveStream2.
 @Input         hSrvHandle      Address of a pointer to a connection object
 @Input         hSD             Handle of the stream object to close
 @Input         ui32Size        Size of the data
 @Return
*/ /***************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR TLClientCommitStream(IMG_HANDLE hSrvHandle,
		IMG_HANDLE hSD,
		IMG_UINT32 ui32Size);

/**************************************************************************/ /*!
 @Function		TLClientAcquireData
 @Description	When there is data available in the stream buffer this call
 	 	 	 	returns with the address and length of the data buffer the
 	 	 	 	client can safely read. This buffer may contain one or more
 	 	 	 	packets of data.
 	 	 	 	If no data is available then this call blocks until it becomes
 	 	 	 	available. However if the stream has been destroyed while
 	 	 	 	waiting then a resource unavailable error will be returned
 	 	 	 	to the caller. Clients must pair this call with a
 	 	 	 	ReleaseData call.
 @Input			hSrvHandle  	Address of a pointer to a connection object
 @Input			hSD				Handle of the stream object to read
 @Output		ppPacketBuf		Address of a pointer to an byte buffer. On exit
								pointer contains address of buffer to read from
 @Output		puiBufLen		Pointer to an integer. On exit it is the size
								of the data to read from the packet buffer
 @Return		PVRSRV_ERROR_RESOURCE_UNAVAILABLE: when stream no longer exists
 @Return		PVRSRV_ERROR_HANDLE_NOT_FOUND:     when SD handle not known
 @Return		PVRSRV_ERROR_STREAM_ERROR: 	       internal driver state error
 @Return		PVRSRV_ERROR_RETRY:				   release not called beforehand
 @Return        PVRSRV_ERROR_TIMEOUT:              block timed out, no data
 @Return		PVRSRV_ERROR:					   for other system codes
*/ /***************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR TLClientAcquireData(IMG_HANDLE hSrvHandle,
		IMG_HANDLE  hSD,
		IMG_PBYTE*  ppPacketBuf,
		IMG_UINT32* puiBufLen);


/**************************************************************************/ /*!
 @Function		TLClientReleaseData
 @Description	Called after client has read the stream data out of the buffer
 	 	 	 	The data is subsequently flushed from the stream buffer to make
 	 	 	 	room for more data packets from the stream source.
 @Input			hSrvHandle  	Address of a pointer to a connection object
 @Input			hSD				Handle of the stream object to read
 @Return		PVRSRV_ERROR_RESOURCE_UNAVAILABLE: when stream no longer exists
 @Return		PVRSRV_ERROR_HANDLE_NOT_FOUND:   when SD handle not known to TL
 @Return		PVRSRV_ERROR_STREAM_ERROR: 	     internal driver state error
 @Return		PVRSRV_ERROR_RETRY:				 acquire not called beforehand
 @Return		PVRSRV_ERROR:	                 for system codes
*/ /***************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR TLClientReleaseData(IMG_HANDLE hSrvHandle,
		IMG_HANDLE hSD);

/**************************************************************************/ /*!
 @Function      TLClientWriteData
 @Description   Writes data to the stream.
 @Input         hSrvHandle      Address of a pointer to a connection object
 @Input         hSD             Handle of the stream object to read
 @Input         ui32Size        Size of the data
 @Input         pui8Data        Pointer to data
*/ /***************************************************************************/
IMG_INTERNAL
PVRSRV_ERROR TLClientWriteData(IMG_HANDLE hSrvHandle,
		IMG_HANDLE hSD,
		IMG_UINT32 ui32Size,
		IMG_BYTE *pui8Data);


#endif /* TLCLIENT_H_ */

/******************************************************************************
 End of file (tlclient.h)
******************************************************************************/
