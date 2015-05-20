/*************************************************************************/ /*!
@Title          Direct client bridge for pdumpmm
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

#include "client_pdumpmm_bridge.h"
#include "img_defs.h"
#include "pvr_debug.h"

/* Module specific includes */
#include "pdump.h"
#include "pdumpdefs.h"
#include "pvrsrv_memallocflags.h"
#include "devicemem_typedefs.h"

#include "devicemem_server.h"
#include "pmr.h"
#include "physmem.h"


IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRPDumpLoadMem(IMG_HANDLE hBridge,
							     IMG_HANDLE hPMR,
							     IMG_DEVMEM_OFFSET_T uiOffset,
							     IMG_DEVMEM_SIZE_T uiSize,
							     IMG_UINT32 ui32PDumpFlags,
							     IMG_BOOL bbZero)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMRPDumpLoadMem(
					psPMRInt,
					uiOffset,
					uiSize,
					ui32PDumpFlags,
					bbZero);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRPDumpLoadMemValue32(IMG_HANDLE hBridge,
								    IMG_HANDLE hPMR,
								    IMG_DEVMEM_OFFSET_T uiOffset,
								    IMG_UINT32 ui32Value,
								    IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMRPDumpLoadMemValue32(
					psPMRInt,
					uiOffset,
					ui32Value,
					ui32PDumpFlags);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRPDumpLoadMemValue64(IMG_HANDLE hBridge,
								    IMG_HANDLE hPMR,
								    IMG_DEVMEM_OFFSET_T uiOffset,
								    IMG_UINT64 ui64Value,
								    IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMRPDumpLoadMemValue64(
					psPMRInt,
					uiOffset,
					ui64Value,
					ui32PDumpFlags);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRPDumpSaveToFile(IMG_HANDLE hBridge,
								IMG_HANDLE hPMR,
								IMG_DEVMEM_OFFSET_T uiOffset,
								IMG_DEVMEM_SIZE_T uiSize,
								IMG_UINT32 ui32ArraySize,
								const IMG_CHAR *puiFileName)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMRPDumpSaveToFile(
					psPMRInt,
					uiOffset,
					uiSize,
					ui32ArraySize,
					puiFileName);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRPDumpSymbolicAddr(IMG_HANDLE hBridge,
								  IMG_HANDLE hPMR,
								  IMG_DEVMEM_OFFSET_T uiOffset,
								  IMG_UINT32 ui32MemspaceNameLen,
								  IMG_CHAR *puiMemspaceName,
								  IMG_UINT32 ui32SymbolicAddrLen,
								  IMG_CHAR *puiSymbolicAddr,
								  IMG_DEVMEM_OFFSET_T *puiNewOffset,
								  IMG_DEVMEM_OFFSET_T *puiNextSymName)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMR_PDumpSymbolicAddr(
					psPMRInt,
					uiOffset,
					ui32MemspaceNameLen,
					puiMemspaceName,
					ui32SymbolicAddrLen,
					puiSymbolicAddr,
					puiNewOffset,
					puiNextSymName);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRPDumpPol32(IMG_HANDLE hBridge,
							   IMG_HANDLE hPMR,
							   IMG_DEVMEM_OFFSET_T uiOffset,
							   IMG_UINT32 ui32Value,
							   IMG_UINT32 ui32Mask,
							   PDUMP_POLL_OPERATOR eOperator,
							   IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMRPDumpPol32(
					psPMRInt,
					uiOffset,
					ui32Value,
					ui32Mask,
					eOperator,
					ui32PDumpFlags);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgePMRPDumpCBP(IMG_HANDLE hBridge,
							 IMG_HANDLE hPMR,
							 IMG_DEVMEM_OFFSET_T uiReadOffset,
							 IMG_DEVMEM_OFFSET_T uiWriteOffset,
							 IMG_DEVMEM_SIZE_T uiPacketSize,
							 IMG_DEVMEM_SIZE_T uiBufferSize)
{
	PVRSRV_ERROR eError;
	PMR * psPMRInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psPMRInt = (PMR *) hPMR;

	eError =
		PMRPDumpCBP(
					psPMRInt,
					uiReadOffset,
					uiWriteOffset,
					uiPacketSize,
					uiBufferSize);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeDevmemIntPDumpSaveToFileVirtual(IMG_HANDLE hBridge,
									     IMG_HANDLE hDevmemServerContext,
									     IMG_DEV_VIRTADDR sAddress,
									     IMG_DEVMEM_SIZE_T uiSize,
									     IMG_UINT32 ui32ArraySize,
									     const IMG_CHAR *puiFileName,
									     IMG_UINT32 ui32FileOffset,
									     IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;
	DEVMEMINT_CTX * psDevmemServerContextInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psDevmemServerContextInt = (DEVMEMINT_CTX *) hDevmemServerContext;

	eError =
		DevmemIntPDumpSaveToFileVirtual(
					psDevmemServerContextInt,
					sAddress,
					uiSize,
					ui32ArraySize,
					puiFileName,
					ui32FileOffset,
					ui32PDumpFlags);

	return eError;
}

