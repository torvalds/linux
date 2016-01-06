/*************************************************************************/ /*!
@File
@Title          Shared device memory management PDump functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements common (client & server) PDump functions for the
                memory management code
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

#if defined PDUMP

#include "allocmem.h"
#include "img_types.h"
#include "pvrsrv_error.h"
#include "pdump.h"
#include "devicemem_utils.h"
#include "devicemem_pdump.h"
#include "client_pdumpmm_bridge.h"

IMG_INTERNAL IMG_VOID
DevmemPDumpLoadMem(DEVMEM_MEMDESC *psMemDesc,
                   IMG_DEVMEM_OFFSET_T uiOffset,
                   IMG_DEVMEM_SIZE_T uiSize,
                   PDUMP_FLAGS_T uiPDumpFlags)
{
    PVRSRV_ERROR eError;

    PVR_ASSERT(uiOffset + uiSize <= psMemDesc->psImport->uiSize);

    eError = BridgePMRPDumpLoadMem(psMemDesc->psImport->hBridge,
                                   psMemDesc->psImport->hPMR,
                                   psMemDesc->uiOffset + uiOffset,
                                   uiSize,
                                   uiPDumpFlags,
                                   IMG_FALSE);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);
}

IMG_INTERNAL IMG_VOID
DevmemPDumpLoadZeroMem(DEVMEM_MEMDESC *psMemDesc,
                   IMG_DEVMEM_OFFSET_T uiOffset,
                   IMG_DEVMEM_SIZE_T uiSize,
                   PDUMP_FLAGS_T uiPDumpFlags)
{
    PVRSRV_ERROR eError;

    PVR_ASSERT(uiOffset + uiSize <= psMemDesc->psImport->uiSize);

    eError = BridgePMRPDumpLoadMem(psMemDesc->psImport->hBridge,
                                   psMemDesc->psImport->hPMR,
                                   psMemDesc->uiOffset + uiOffset,
                                   uiSize,
                                   uiPDumpFlags,
                                   IMG_TRUE);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);
}

IMG_INTERNAL IMG_VOID
DevmemPDumpLoadMemValue32(DEVMEM_MEMDESC *psMemDesc,
                        IMG_DEVMEM_OFFSET_T uiOffset,
                        IMG_UINT32 ui32Value,
                        PDUMP_FLAGS_T uiPDumpFlags)
{
    PVRSRV_ERROR eError;

    eError = BridgePMRPDumpLoadMemValue32(psMemDesc->psImport->hBridge,
                                        psMemDesc->psImport->hPMR,
                                        psMemDesc->uiOffset + uiOffset,
                                        ui32Value,
                                        uiPDumpFlags);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);
}

IMG_INTERNAL IMG_VOID
DevmemPDumpLoadMemValue64(DEVMEM_MEMDESC *psMemDesc,
                        IMG_DEVMEM_OFFSET_T uiOffset,
                        IMG_UINT64 ui64Value,
                        PDUMP_FLAGS_T uiPDumpFlags)
{
    PVRSRV_ERROR eError;

    eError = BridgePMRPDumpLoadMemValue64(psMemDesc->psImport->hBridge,
                                          psMemDesc->psImport->hPMR,
                                          psMemDesc->uiOffset + uiOffset,
                                          ui64Value,
                                          uiPDumpFlags);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);
}

/* FIXME: This should be server side only */
IMG_INTERNAL PVRSRV_ERROR
DevmemPDumpPageCatBaseToSAddr(DEVMEM_MEMDESC		*psMemDesc,
							  IMG_DEVMEM_OFFSET_T	*puiMemOffset,
							  IMG_CHAR				*pszName,
							  IMG_UINT32			ui32Size)
{
    PVRSRV_ERROR		eError;
	IMG_CHAR			aszMemspaceName[100];
	IMG_CHAR			aszSymbolicName[100];
	IMG_DEVMEM_OFFSET_T uiNextSymName;

	*puiMemOffset += psMemDesc->uiOffset;

    eError = BridgePMRPDumpSymbolicAddr(psMemDesc->psImport->hBridge,
										psMemDesc->psImport->hPMR,
										*puiMemOffset,
										sizeof(aszMemspaceName),
										&aszMemspaceName[0],
										sizeof(aszSymbolicName),
										&aszSymbolicName[0],
										puiMemOffset,
										&uiNextSymName);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);

	OSSNPrintf(pszName, ui32Size, "%s:%s", &aszMemspaceName[0], &aszSymbolicName[0]);
	return eError;
}

IMG_INTERNAL IMG_VOID
DevmemPDumpSaveToFile(DEVMEM_MEMDESC *psMemDesc,
                      IMG_DEVMEM_OFFSET_T uiOffset,
                      IMG_DEVMEM_SIZE_T uiSize,
                      const IMG_CHAR *pszFilename)
{
    PVRSRV_ERROR eError;

    eError = BridgePMRPDumpSaveToFile(psMemDesc->psImport->hBridge,
									  psMemDesc->psImport->hPMR,
									  psMemDesc->uiOffset + uiOffset,
									  uiSize,
									  OSStringLength(pszFilename) + 1,
									  pszFilename);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);
}

/* FIXME: Remove? */
IMG_INTERNAL IMG_VOID
DevmemPDumpSaveToFileVirtual(DEVMEM_MEMDESC *psMemDesc,
                             IMG_DEVMEM_OFFSET_T uiOffset,
                             IMG_DEVMEM_SIZE_T uiSize,
                             const IMG_CHAR *pszFilename,
							 IMG_UINT32 ui32FileOffset,
							 IMG_UINT32	ui32PdumpFlags)
{
    PVRSRV_ERROR eError;
    IMG_DEV_VIRTADDR sDevAddrStart;

    sDevAddrStart = psMemDesc->psImport->sDeviceImport.sDevVAddr;
    sDevAddrStart.uiAddr += psMemDesc->uiOffset;
    sDevAddrStart.uiAddr += uiOffset;

    eError = BridgeDevmemIntPDumpSaveToFileVirtual(psMemDesc->psImport->hBridge,
                                                   psMemDesc->psImport->sDeviceImport.psHeap->psCtx->hDevMemServerContext,
                                                   sDevAddrStart,
                                                   uiSize,
                                                   OSStringLength(pszFilename) + 1,
                                                   pszFilename,
												   ui32FileOffset,
												   ui32PdumpFlags);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed with error %d",
				__FUNCTION__, eError));
	}
    PVR_ASSERT(eError == PVRSRV_OK);
}


IMG_INTERNAL PVRSRV_ERROR
DevmemPDumpDevmemPol32(const DEVMEM_MEMDESC *psMemDesc,
                       IMG_DEVMEM_OFFSET_T uiOffset,
                       IMG_UINT32 ui32Value,
                       IMG_UINT32 ui32Mask,
                       PDUMP_POLL_OPERATOR eOperator,
                       PDUMP_FLAGS_T ui32PDumpFlags)
{
    PVRSRV_ERROR eError;
    IMG_DEVMEM_SIZE_T uiNumBytes;

    uiNumBytes = 4;

    if (psMemDesc->uiOffset + uiOffset + uiNumBytes >= psMemDesc->psImport->uiSize)
    {
        eError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE;
        goto e0;
    }

    eError = BridgePMRPDumpPol32(psMemDesc->psImport->hBridge,
                                 psMemDesc->psImport->hPMR,
                                 psMemDesc->uiOffset + uiOffset,
                                 ui32Value,
                                 ui32Mask,
                                 eOperator,
                                 ui32PDumpFlags);
    if (eError != PVRSRV_OK)
    {
        goto e0;
    }

    return PVRSRV_OK;

    /*
      error exit paths follow
    */

 e0:
    PVR_ASSERT(eError != PVRSRV_OK);
    return eError;
}

IMG_INTERNAL PVRSRV_ERROR
DevmemPDumpCBP(const DEVMEM_MEMDESC *psMemDesc,
				IMG_DEVMEM_OFFSET_T uiReadOffset,
				IMG_DEVMEM_OFFSET_T uiWriteOffset,
				IMG_DEVMEM_SIZE_T uiPacketSize,
				IMG_DEVMEM_SIZE_T uiBufferSize)
{
	PVRSRV_ERROR eError;

	if ((psMemDesc->uiOffset + uiReadOffset) > psMemDesc->psImport->uiSize)
	{
		eError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE;
		goto e0;
	}

	eError = BridgePMRPDumpCBP(psMemDesc->psImport->hBridge,
							   psMemDesc->psImport->hPMR,
							   psMemDesc->uiOffset + uiReadOffset,
							   uiWriteOffset,
							   uiPacketSize,
							   uiBufferSize);
	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	return PVRSRV_OK;

e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

#endif /* PDUMP */

