/*************************************************************************/ /*!
@File
@Title          Services Transport Layer UM Client API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    User mode Transport layer API for clients.
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
 
#ifndef __PVR_TL_H__
#define __PVR_TL_H__

#if defined (__cplusplus)
extern "C" {
#endif


#include "img_defs.h"
#include "pvrsrv_error.h"
#include "services.h"

#include "pvr_tlcommon.h"


/**************************************************************************/ /*!
 @Function		PVRSRVTLConnect
 @Description	Initialise connection to Services kernel server transport layer
 @Output		ppsConnection	Address of a pointer to a connection object
 @Return        PVRSRV_ERROR:	for system error codes
*/ /***************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVTLConnect(PVRSRV_CONNECTION **ppsConnection);


/**************************************************************************/ /*!
 @Function		PVRSRVTLDisconnect
 @Description	Disconnect from the Services kernel server transport layer
 @Input			psConnection	Pointer to connection object as returned from
 	 	 	 	 	 	 	 	PVRSRVTLConnect()
 @Return        PVRSRV_ERROR:	for system error codes
*/ /***************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVTLDisconnect(PVRSRV_CONNECTION* psConnection);


/**************************************************************************/ /*!
 @Function		PVRSRVTLOpenStream
 @Description	Open a descriptor onto an existing PVR transport stream. If
				the stream does not exist it returns a NOT_FOUND error unless
				the OPEN_WAIT flag is supplied. In this case it will wait for
				the stream to be created. If it is not created in the wait
				period a TIMEOUT error is returned.
 @Input			psConnection	Address of a pointer to a connection object
 @Input			pszName			Address of the stream name string, no longer
 	 	 	 	 	 	 	 	than PRVSRVTL_MAX_STREAM_NAME_SIZE.
 @Input			ui32Mode    Flags defined in pvr_tlcommon.h
							ACQUIRE_NONBLOCKING: Results in non-blocking reads
							    on stream. Reads are blocking by default
                            OPEN_WAIT: Causes open to wait for a brief moment
                                if the stream does not exist
 @Output		phSD		Address of a pointer to an stream object
 @Return 		PVRSRV_ERROR_NOT_FOUND:        when named stream not found
 @Return		PVRSRV_ERROR_ALREADY_OPEN:     stream already open by another
 @Return		PVRSRV_ERROR_STREAM_ERROR:     internal driver state error
 @Return        PVRSRV_ERROR_TIMEOUT:          block timed out, stream not found
 @Return		PVRSRV_ERROR:			       for other system codes
*/ /***************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVTLOpenStream(PVRSRV_CONNECTION* psConnection,
		IMG_PCHAR    pszName,
		IMG_UINT32   ui32Mode,
		PVRSRVTL_SD* phSD);


/**************************************************************************/ /*!
 @Function		PVRSRVTLCloseStream
 @Description	Close and release the stream connection to Services kernel
				server transport layer. Any outstanding Acquire will be
				released.
 @Input			psConnection	Address of a pointer to a connection object
 @Input			hSD				Handle of the stream object to close
 @Return		PVRSRV_ERROR_HANDLE_NOT_FOUND: when SD handle is not known
 @Return		PVRSRV_ERROR_STREAM_ERROR: 	  internal driver state error
 @Return		PVRSRV_ERROR:				  for system codes
*/ /***************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVTLCloseStream(PVRSRV_CONNECTION* psConnection,
		PVRSRVTL_SD hSD);


/**************************************************************************/ /*!
 @Function		PVRSRVTLAcquireData
 @Description	When there is data available in the stream buffer this call
 	 	 	 	returns with the address and length of the data buffer the
 	 	 	 	client can safely read. This buffer may contain one or more
 	 	 	 	packets of data.
 	 	 	 	If no data is available then this call blocks until it becomes
 	 	 	 	available. However if the stream has been destroyed while
 	 	 	 	waiting then a resource unavailable error will be returned
 	 	 	 	to the caller. Clients must pair this call with a
 	 	 	 	ReleaseData call.
 @Input			psConnection	Address of a pointer to a connection object
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
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVTLAcquireData(PVRSRV_CONNECTION* psConnection,
		PVRSRVTL_SD hSD,
		IMG_PBYTE*  ppPacketBuf,
		IMG_UINT32* puiBufLen);


/**************************************************************************/ /*!
 @Function		PVRSRVTLReleaseData
 @Description	Called after client has read the stream data out of the buffer
 	 	 	 	The data is subsequently flushed from the stream buffer to make
 	 	 	 	room for more data packets from the stream source.
 @Input			psConnection	Address of a pointer to a connection object
 @Input			hSD				Handle of the stream object to read
 @Return		PVRSRV_ERROR_RESOURCE_UNAVAILABLE: when stream no longer exists
 @Return		PVRSRV_ERROR_HANDLE_NOT_FOUND:   when SD handle not known to TL
 @Return		PVRSRV_ERROR_STREAM_ERROR: 	     internal driver state error
 @Return		PVRSRV_ERROR_RETRY:				 acquire not called beforehand
 @Return		PVRSRV_ERROR:	                 for system codes
*/ /***************************************************************************/
IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVTLReleaseData(PVRSRV_CONNECTION* psConnection,
		PVRSRVTL_SD hSD);






/**************************************************************************/ /*!
 @Function		PVRSRVTLTestIoctl
 @Description	INTERNAL USE: DEBUG builds only
*/ /***************************************************************************/
IMG_EXPORT PVRSRV_ERROR IMG_CALLCONV PVRSRVTLTestIoctl(
		PVRSRV_CONNECTION* psConnection,
		IMG_UINT32	uiCmd, IMG_BYTE* pbIn1, IMG_UINT32	uiIn2,
		IMG_UINT32*	puiOut1, IMG_UINT32* puiOut2);




#if defined (__cplusplus)
}
#endif

#endif /* __PVR_TL_H__ */

/******************************************************************************
 End of file (pvr_tl.h)
******************************************************************************/
