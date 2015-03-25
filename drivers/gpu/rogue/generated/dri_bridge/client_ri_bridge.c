/*************************************************************************/ /*!
@Title          Direct client bridge for ri
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#include "client_ri_bridge.h"
#include "img_defs.h"
#include "pvr_debug.h"

/* Module specific includes */
#include "ri_typedefs.h"

#include "ri_server.h"


IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRIWritePMREntry(IMG_HANDLE hBridge,
							     IMG_HANDLE hPMRHandle,
							     IMG_UINT32 ui32TextASize,
							     const IMG_CHAR *puiTextA,
							     IMG_SIZE_T uiLogicalSize)
{
	PVRSRV_ERROR eError;
	PMR * psPMRHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRHandleInt = (PMR *) hPMRHandle;

	eError =
		RIWritePMREntryKM(
					psPMRHandleInt,
					ui32TextASize,
					puiTextA,
					uiLogicalSize);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRIWriteMEMDESCEntry(IMG_HANDLE hBridge,
								 IMG_HANDLE hPMRHandle,
								 IMG_UINT32 ui32TextBSize,
								 const IMG_CHAR *puiTextB,
								 IMG_SIZE_T uiOffset,
								 IMG_SIZE_T uiSize,
								 IMG_BOOL bIsImport,
								 IMG_BOOL bIsExportable,
								 IMG_HANDLE *phRIHandle)
{
	PVRSRV_ERROR eError;
	PMR * psPMRHandleInt;
	RI_HANDLE psRIHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRHandleInt = (PMR *) hPMRHandle;

	eError =
		RIWriteMEMDESCEntryKM(
					psPMRHandleInt,
					ui32TextBSize,
					puiTextB,
					uiOffset,
					uiSize,
					bIsImport,
					bIsExportable,
					&psRIHandleInt);

	*phRIHandle = psRIHandleInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRIUpdateMEMDESCAddr(IMG_HANDLE hBridge,
								 IMG_HANDLE hRIHandle,
								 IMG_DEV_VIRTADDR sAddr)
{
	PVRSRV_ERROR eError;
	RI_HANDLE psRIHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psRIHandleInt = (RI_HANDLE) hRIHandle;

	eError =
		RIUpdateMEMDESCAddrKM(
					psRIHandleInt,
					sAddr);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRIDeleteMEMDESCEntry(IMG_HANDLE hBridge,
								  IMG_HANDLE hRIHandle)
{
	PVRSRV_ERROR eError;
	RI_HANDLE psRIHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psRIHandleInt = (RI_HANDLE) hRIHandle;

	eError =
		RIDeleteMEMDESCEntryKM(
					psRIHandleInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRIDumpList(IMG_HANDLE hBridge,
							IMG_HANDLE hPMRHandle)
{
	PVRSRV_ERROR eError;
	PMR * psPMRHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRHandleInt = (PMR *) hPMRHandle;

	eError =
		RIDumpListKM(
					psPMRHandleInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRIDumpAll(IMG_HANDLE hBridge)
{
	PVRSRV_ERROR eError;
	PVR_UNREFERENCED_PARAMETER(hBridge);


	eError =
		RIDumpAllKM(
					);
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeRIDumpProcess(IMG_HANDLE hBridge,
							   IMG_PID ui32Pid)
{
	PVRSRV_ERROR eError;
	PVR_UNREFERENCED_PARAMETER(hBridge);


	eError =
		RIDumpProcessKM(
					ui32Pid);

	return eError;
}

