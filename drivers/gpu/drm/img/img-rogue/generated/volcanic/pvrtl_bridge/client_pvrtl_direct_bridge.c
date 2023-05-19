/*******************************************************************************
@File
@Title          Direct client bridge for pvrtl
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the client side of the bridge for pvrtl
                which is used in calls from Server context.
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
*******************************************************************************/

#include "client_pvrtl_bridge.h"
#include "img_defs.h"
#include "pvr_debug.h"

/* Module specific includes */
#include "devicemem_typedefs.h"
#include "pvrsrv_tlcommon.h"

#include "tlserver.h"

IMG_INTERNAL PVRSRV_ERROR BridgeTLOpenStream(IMG_HANDLE hBridge,
					     const IMG_CHAR * puiName,
					     IMG_UINT32 ui32Mode,
					     IMG_HANDLE * phSD, IMG_HANDLE * phTLPMR)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC *psSDInt = NULL;
	PMR *psTLPMRInt = NULL;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	eError = TLServerOpenStreamKM(puiName, ui32Mode, &psSDInt, &psTLPMRInt);

	*phSD = psSDInt;
	*phTLPMR = psTLPMRInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeTLCloseStream(IMG_HANDLE hBridge, IMG_HANDLE hSD)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC *psSDInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSDInt = (TL_STREAM_DESC *) hSD;

	eError = TLServerCloseStreamKM(psSDInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeTLAcquireData(IMG_HANDLE hBridge,
					      IMG_HANDLE hSD,
					      IMG_UINT32 * pui32ReadOffset,
					      IMG_UINT32 * pui32ReadLen)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC *psSDInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSDInt = (TL_STREAM_DESC *) hSD;

	eError = TLServerAcquireDataKM(psSDInt, pui32ReadOffset, pui32ReadLen);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeTLReleaseData(IMG_HANDLE hBridge,
					      IMG_HANDLE hSD,
					      IMG_UINT32 ui32ReadOffset, IMG_UINT32 ui32ReadLen)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC *psSDInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSDInt = (TL_STREAM_DESC *) hSD;

	eError = TLServerReleaseDataKM(psSDInt, ui32ReadOffset, ui32ReadLen);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeTLDiscoverStreams(IMG_HANDLE hBridge,
						  const IMG_CHAR * puiNamePattern,
						  IMG_UINT32 ui32Size,
						  IMG_CHAR * puiStreams, IMG_UINT32 * pui32NumFound)
{
	PVRSRV_ERROR eError;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	eError = TLServerDiscoverStreamsKM(puiNamePattern, ui32Size, puiStreams, pui32NumFound);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeTLReserveStream(IMG_HANDLE hBridge,
						IMG_HANDLE hSD,
						IMG_UINT32 * pui32BufferOffset,
						IMG_UINT32 ui32Size,
						IMG_UINT32 ui32SizeMin, IMG_UINT32 * pui32Available)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC *psSDInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSDInt = (TL_STREAM_DESC *) hSD;

	eError =
	    TLServerReserveStreamKM(psSDInt,
				    pui32BufferOffset, ui32Size, ui32SizeMin, pui32Available);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeTLCommitStream(IMG_HANDLE hBridge,
					       IMG_HANDLE hSD, IMG_UINT32 ui32ReqSize)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC *psSDInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSDInt = (TL_STREAM_DESC *) hSD;

	eError = TLServerCommitStreamKM(psSDInt, ui32ReqSize);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeTLWriteData(IMG_HANDLE hBridge,
					    IMG_HANDLE hSD,
					    IMG_UINT32 ui32Size, IMG_BYTE * pui8Data)
{
	PVRSRV_ERROR eError;
	TL_STREAM_DESC *psSDInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSDInt = (TL_STREAM_DESC *) hSD;

	eError = TLServerWriteDataKM(psSDInt, ui32Size, pui8Data);

	return eError;
}
