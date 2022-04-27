/*******************************************************************************
@File
@Title          Direct client bridge for rgxpdump
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the client side of the bridge for rgxpdump
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

#include "client_rgxpdump_bridge.h"
#include "img_defs.h"
#include "pvr_debug.h"

/* Module specific includes */
#include "rgx_bridge.h"

#include "rgxpdump.h"

IMG_INTERNAL PVRSRV_ERROR BridgePDumpTraceBuffer(IMG_HANDLE hBridge, IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	eError =
	    PVRSRVPDumpTraceBufferKM(NULL, (PVRSRV_DEVICE_NODE *) ((void *)hBridge),
				     ui32PDumpFlags);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgePDumpSignatureBuffer(IMG_HANDLE hBridge, IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	eError =
	    PVRSRVPDumpSignatureBufferKM(NULL, (PVRSRV_DEVICE_NODE *) ((void *)hBridge),
					 ui32PDumpFlags);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgePDumpComputeCRCSignatureCheck(IMG_HANDLE hBridge,
							      IMG_UINT32 ui32PDumpFlags)
{
#if defined(SUPPORT_VALIDATION)
	PVRSRV_ERROR eError;

	eError =
	    PVRSRVPDumpComputeCRCSignatureCheckKM(NULL, (PVRSRV_DEVICE_NODE *) ((void *)hBridge),
						  ui32PDumpFlags);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
#endif
}

IMG_INTERNAL PVRSRV_ERROR BridgePDumpCRCSignatureCheck(IMG_HANDLE hBridge,
						       IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	eError =
	    PVRSRVPDumpCRCSignatureCheckKM(NULL, (PVRSRV_DEVICE_NODE *) ((void *)hBridge),
					   ui32PDumpFlags);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgePDumpValCheckPreCommand(IMG_HANDLE hBridge,
							IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	eError =
	    PVRSRVPDumpValCheckPreCommandKM(NULL, (PVRSRV_DEVICE_NODE *) ((void *)hBridge),
					    ui32PDumpFlags);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgePDumpValCheckPostCommand(IMG_HANDLE hBridge,
							 IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	eError =
	    PVRSRVPDumpValCheckPostCommandKM(NULL, (PVRSRV_DEVICE_NODE *) ((void *)hBridge),
					     ui32PDumpFlags);

	return eError;
}
