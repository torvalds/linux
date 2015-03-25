/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxinit
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxinit
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
#include <asm/uaccess.h>

#include "img_defs.h"

#include "rgxinit.h"


#include "common_rgxinit_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#if defined (SUPPORT_AUTH)
#include "osauth.h"
#endif

#include <linux/slab.h>

/* ***************************************************************************
 * Bridge proxy functions
 */



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXInitAllocFWImgMem(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXINITALLOCFWIMGMEM *psRGXInitAllocFWImgMemIN,
					 PVRSRV_BRIDGE_OUT_RGXINITALLOCFWIMGMEM *psRGXInitAllocFWImgMemOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	DEVMEM_EXPORTCOOKIE * psFWCodeAllocServerExportCookieInt = IMG_NULL;
	DEVMEM_EXPORTCOOKIE * psFWDataAllocServerExportCookieInt = IMG_NULL;
	DEVMEM_EXPORTCOOKIE * psFWCorememAllocServerExportCookieInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXINIT_RGXINITALLOCFWIMGMEM);





				{
					/* Look up the address from the handle */
					psRGXInitAllocFWImgMemOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXInitAllocFWImgMemIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
					{
						goto RGXInitAllocFWImgMem_exit;
					}

				}

	psRGXInitAllocFWImgMemOUT->eError =
		PVRSRVRGXInitAllocFWImgMemKM(
					hDevNodeInt,
					psRGXInitAllocFWImgMemIN->uiFWCodeLen,
					psRGXInitAllocFWImgMemIN->uiFWDataLen,
					psRGXInitAllocFWImgMemIN->uiFWCoremem,
					&psFWCodeAllocServerExportCookieInt,
					&psRGXInitAllocFWImgMemOUT->sFWCodeDevVAddrBase,
					&psFWDataAllocServerExportCookieInt,
					&psRGXInitAllocFWImgMemOUT->sFWDataDevVAddrBase,
					&psFWCorememAllocServerExportCookieInt,
					&psRGXInitAllocFWImgMemOUT->sFWCorememDevVAddrBase,
					&psRGXInitAllocFWImgMemOUT->sFWCorememMetaVAddrBase);
	/* Exit early if bridged call fails */
	if(psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
	{
		goto RGXInitAllocFWImgMem_exit;
	}

	psRGXInitAllocFWImgMemOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXInitAllocFWImgMemOUT->hFWCodeAllocServerExportCookie,
							(IMG_HANDLE) psFWCodeAllocServerExportCookieInt,
							PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
	{
		goto RGXInitAllocFWImgMem_exit;
	}
	psRGXInitAllocFWImgMemOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXInitAllocFWImgMemOUT->hFWDataAllocServerExportCookie,
							(IMG_HANDLE) psFWDataAllocServerExportCookieInt,
							PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
	{
		goto RGXInitAllocFWImgMem_exit;
	}
	psRGXInitAllocFWImgMemOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXInitAllocFWImgMemOUT->hFWCorememAllocServerExportCookie,
							(IMG_HANDLE) psFWCorememAllocServerExportCookieInt,
							PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
	{
		goto RGXInitAllocFWImgMem_exit;
	}


RGXInitAllocFWImgMem_exit:
	if (psRGXInitAllocFWImgMemOUT->eError != PVRSRV_OK)
	{
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeRGXInitFirmware(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXINITFIRMWARE *psRGXInitFirmwareIN,
					 PVRSRV_BRIDGE_OUT_RGXINITFIRMWARE *psRGXInitFirmwareOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	IMG_UINT32 *ui32RGXFWAlignChecksInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXINIT_RGXINITFIRMWARE);




	if (psRGXInitFirmwareIN->ui32RGXFWAlignChecksSize != 0)
	{
		ui32RGXFWAlignChecksInt = OSAllocMem(psRGXInitFirmwareIN->ui32RGXFWAlignChecksSize * sizeof(IMG_UINT32));
		if (!ui32RGXFWAlignChecksInt)
		{
			psRGXInitFirmwareOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXInitFirmware_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXInitFirmwareIN->pui32RGXFWAlignChecks, psRGXInitFirmwareIN->ui32RGXFWAlignChecksSize * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32RGXFWAlignChecksInt, psRGXInitFirmwareIN->pui32RGXFWAlignChecks,
				psRGXInitFirmwareIN->ui32RGXFWAlignChecksSize * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXInitFirmwareOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXInitFirmware_exit;
			}

				{
					/* Look up the address from the handle */
					psRGXInitFirmwareOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXInitFirmwareIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXInitFirmwareOUT->eError != PVRSRV_OK)
					{
						goto RGXInitFirmware_exit;
					}

				}

	psRGXInitFirmwareOUT->eError =
		PVRSRVRGXInitFirmwareKM(
					hDevNodeInt,
					&psRGXInitFirmwareOUT->spsRGXFwInit,
					psRGXInitFirmwareIN->bEnableSignatureChecks,
					psRGXInitFirmwareIN->ui32SignatureChecksBufSize,
					psRGXInitFirmwareIN->ui32HWPerfFWBufSizeKB,
					psRGXInitFirmwareIN->ui64HWPerfFilter,
					psRGXInitFirmwareIN->ui32RGXFWAlignChecksSize,
					ui32RGXFWAlignChecksInt,
					psRGXInitFirmwareIN->ui32ConfigFlags,
					psRGXInitFirmwareIN->ui32LogType,
					psRGXInitFirmwareIN->ui32FilterFlags,
					&psRGXInitFirmwareIN->sClientBVNC);



RGXInitFirmware_exit:
	if (ui32RGXFWAlignChecksInt)
		OSFreeMem(ui32RGXFWAlignChecksInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXInitLoadFWImage(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXINITLOADFWIMAGE *psRGXInitLoadFWImageIN,
					 PVRSRV_BRIDGE_OUT_RGXINITLOADFWIMAGE *psRGXInitLoadFWImageOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psImgDestImportInt = IMG_NULL;
	IMG_HANDLE hImgDestImportInt2 = IMG_NULL;
	PMR * psImgSrcImportInt = IMG_NULL;
	IMG_HANDLE hImgSrcImportInt2 = IMG_NULL;
	PMR * psSigImportInt = IMG_NULL;
	IMG_HANDLE hSigImportInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXINIT_RGXINITLOADFWIMAGE);





				{
					/* Look up the address from the handle */
					psRGXInitLoadFWImageOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hImgDestImportInt2,
											psRGXInitLoadFWImageIN->hImgDestImport,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psRGXInitLoadFWImageOUT->eError != PVRSRV_OK)
					{
						goto RGXInitLoadFWImage_exit;
					}

					/* Look up the data from the resman address */
					psRGXInitLoadFWImageOUT->eError = ResManFindPrivateDataByPtr(hImgDestImportInt2, (IMG_VOID **) &psImgDestImportInt);

					if(psRGXInitLoadFWImageOUT->eError != PVRSRV_OK)
					{
						goto RGXInitLoadFWImage_exit;
					}
				}

				{
					/* Look up the address from the handle */
					psRGXInitLoadFWImageOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hImgSrcImportInt2,
											psRGXInitLoadFWImageIN->hImgSrcImport,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psRGXInitLoadFWImageOUT->eError != PVRSRV_OK)
					{
						goto RGXInitLoadFWImage_exit;
					}

					/* Look up the data from the resman address */
					psRGXInitLoadFWImageOUT->eError = ResManFindPrivateDataByPtr(hImgSrcImportInt2, (IMG_VOID **) &psImgSrcImportInt);

					if(psRGXInitLoadFWImageOUT->eError != PVRSRV_OK)
					{
						goto RGXInitLoadFWImage_exit;
					}
				}

				{
					/* Look up the address from the handle */
					psRGXInitLoadFWImageOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSigImportInt2,
											psRGXInitLoadFWImageIN->hSigImport,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psRGXInitLoadFWImageOUT->eError != PVRSRV_OK)
					{
						goto RGXInitLoadFWImage_exit;
					}

					/* Look up the data from the resman address */
					psRGXInitLoadFWImageOUT->eError = ResManFindPrivateDataByPtr(hSigImportInt2, (IMG_VOID **) &psSigImportInt);

					if(psRGXInitLoadFWImageOUT->eError != PVRSRV_OK)
					{
						goto RGXInitLoadFWImage_exit;
					}
				}

	psRGXInitLoadFWImageOUT->eError =
		PVRSRVRGXInitLoadFWImageKM(
					psImgDestImportInt,
					psImgSrcImportInt,
					psRGXInitLoadFWImageIN->ui64ImgLen,
					psSigImportInt,
					psRGXInitLoadFWImageIN->ui64SigLen);



RGXInitLoadFWImage_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXInitDevPart2(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXINITDEVPART2 *psRGXInitDevPart2IN,
					 PVRSRV_BRIDGE_OUT_RGXINITDEVPART2 *psRGXInitDevPart2OUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	RGX_INIT_COMMAND *psInitScriptInt = IMG_NULL;
	RGX_INIT_COMMAND *psDbgScriptInt = IMG_NULL;
	RGX_INIT_COMMAND *psDbgBusScriptInt = IMG_NULL;
	RGX_INIT_COMMAND *psDeinitScriptInt = IMG_NULL;
	DEVMEM_EXPORTCOOKIE * psFWCodeAllocServerExportCookieInt = IMG_NULL;
	DEVMEM_EXPORTCOOKIE * psFWDataAllocServerExportCookieInt = IMG_NULL;
	DEVMEM_EXPORTCOOKIE * psFWCorememAllocServerExportCookieInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXINIT_RGXINITDEVPART2);




	
	{
		psInitScriptInt = OSAllocMem(RGX_MAX_INIT_COMMANDS * sizeof(RGX_INIT_COMMAND));
		if (!psInitScriptInt)
		{
			psRGXInitDevPart2OUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXInitDevPart2_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXInitDevPart2IN->psInitScript, RGX_MAX_INIT_COMMANDS * sizeof(RGX_INIT_COMMAND))
				|| (OSCopyFromUser(NULL, psInitScriptInt, psRGXInitDevPart2IN->psInitScript,
				RGX_MAX_INIT_COMMANDS * sizeof(RGX_INIT_COMMAND)) != PVRSRV_OK) )
			{
				psRGXInitDevPart2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXInitDevPart2_exit;
			}
	
	{
		psDbgScriptInt = OSAllocMem(RGX_MAX_INIT_COMMANDS * sizeof(RGX_INIT_COMMAND));
		if (!psDbgScriptInt)
		{
			psRGXInitDevPart2OUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXInitDevPart2_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXInitDevPart2IN->psDbgScript, RGX_MAX_INIT_COMMANDS * sizeof(RGX_INIT_COMMAND))
				|| (OSCopyFromUser(NULL, psDbgScriptInt, psRGXInitDevPart2IN->psDbgScript,
				RGX_MAX_INIT_COMMANDS * sizeof(RGX_INIT_COMMAND)) != PVRSRV_OK) )
			{
				psRGXInitDevPart2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXInitDevPart2_exit;
			}
	
	{
		psDbgBusScriptInt = OSAllocMem(RGX_MAX_DBGBUS_COMMANDS * sizeof(RGX_INIT_COMMAND));
		if (!psDbgBusScriptInt)
		{
			psRGXInitDevPart2OUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXInitDevPart2_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXInitDevPart2IN->psDbgBusScript, RGX_MAX_DBGBUS_COMMANDS * sizeof(RGX_INIT_COMMAND))
				|| (OSCopyFromUser(NULL, psDbgBusScriptInt, psRGXInitDevPart2IN->psDbgBusScript,
				RGX_MAX_DBGBUS_COMMANDS * sizeof(RGX_INIT_COMMAND)) != PVRSRV_OK) )
			{
				psRGXInitDevPart2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXInitDevPart2_exit;
			}
	
	{
		psDeinitScriptInt = OSAllocMem(RGX_MAX_DEINIT_COMMANDS * sizeof(RGX_INIT_COMMAND));
		if (!psDeinitScriptInt)
		{
			psRGXInitDevPart2OUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXInitDevPart2_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXInitDevPart2IN->psDeinitScript, RGX_MAX_DEINIT_COMMANDS * sizeof(RGX_INIT_COMMAND))
				|| (OSCopyFromUser(NULL, psDeinitScriptInt, psRGXInitDevPart2IN->psDeinitScript,
				RGX_MAX_DEINIT_COMMANDS * sizeof(RGX_INIT_COMMAND)) != PVRSRV_OK) )
			{
				psRGXInitDevPart2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXInitDevPart2_exit;
			}

				{
					/* Look up the address from the handle */
					psRGXInitDevPart2OUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXInitDevPart2IN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXInitDevPart2OUT->eError != PVRSRV_OK)
					{
						goto RGXInitDevPart2_exit;
					}

				}

				{
					/* Look up the address from the handle */
					psRGXInitDevPart2OUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &psFWCodeAllocServerExportCookieInt,
											psRGXInitDevPart2IN->hFWCodeAllocServerExportCookie,
											PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE);
					if(psRGXInitDevPart2OUT->eError != PVRSRV_OK)
					{
						goto RGXInitDevPart2_exit;
					}

				}

				{
					/* Look up the address from the handle */
					psRGXInitDevPart2OUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &psFWDataAllocServerExportCookieInt,
											psRGXInitDevPart2IN->hFWDataAllocServerExportCookie,
											PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE);
					if(psRGXInitDevPart2OUT->eError != PVRSRV_OK)
					{
						goto RGXInitDevPart2_exit;
					}

				}

				{
					/* Look up the address from the handle */
					psRGXInitDevPart2OUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &psFWCorememAllocServerExportCookieInt,
											psRGXInitDevPart2IN->hFWCorememAllocServerExportCookie,
											PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE);
					if(psRGXInitDevPart2OUT->eError != PVRSRV_OK)
					{
						goto RGXInitDevPart2_exit;
					}

				}

	psRGXInitDevPart2OUT->eError =
		PVRSRVRGXInitDevPart2KM(
					hDevNodeInt,
					psInitScriptInt,
					psDbgScriptInt,
					psDbgBusScriptInt,
					psDeinitScriptInt,
					psRGXInitDevPart2IN->ui32ui32KernelCatBaseIdReg,
					psRGXInitDevPart2IN->ui32KernelCatBaseId,
					psRGXInitDevPart2IN->ui32KernelCatBaseReg,
					psRGXInitDevPart2IN->ui32KernelCatBaseWordSize,
					psRGXInitDevPart2IN->ui32KernelCatBaseAlignShift,
					psRGXInitDevPart2IN->ui32KernelCatBaseShift,
					psRGXInitDevPart2IN->ui64KernelCatBaseMask,
					psRGXInitDevPart2IN->ui32DeviceFlags,
					psRGXInitDevPart2IN->ui32RGXActivePMConf,
					psFWCodeAllocServerExportCookieInt,
					psFWDataAllocServerExportCookieInt,
					psFWCorememAllocServerExportCookieInt);
	/* Exit early if bridged call fails */
	if(psRGXInitDevPart2OUT->eError != PVRSRV_OK)
	{
		goto RGXInitDevPart2_exit;
	}

	psRGXInitDevPart2OUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXInitDevPart2IN->hFWCodeAllocServerExportCookie,
					PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE);
	psRGXInitDevPart2OUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXInitDevPart2IN->hFWDataAllocServerExportCookie,
					PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE);
	psRGXInitDevPart2OUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXInitDevPart2IN->hFWCorememAllocServerExportCookie,
					PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE);


RGXInitDevPart2_exit:
	if (psInitScriptInt)
		OSFreeMem(psInitScriptInt);
	if (psDbgScriptInt)
		OSFreeMem(psDbgScriptInt);
	if (psDbgBusScriptInt)
		OSFreeMem(psDbgBusScriptInt);
	if (psDeinitScriptInt)
		OSFreeMem(psDeinitScriptInt);

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterRGXINITFunctions(IMG_VOID);
IMG_VOID UnregisterRGXINITFunctions(IMG_VOID);

/*
 * Register all RGXINIT functions with services
 */
PVRSRV_ERROR RegisterRGXINITFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT_RGXINITALLOCFWIMGMEM, PVRSRVBridgeRGXInitAllocFWImgMem);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT_RGXINITFIRMWARE, PVRSRVBridgeRGXInitFirmware);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT_RGXINITLOADFWIMAGE, PVRSRVBridgeRGXInitLoadFWImage);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXINIT_RGXINITDEVPART2, PVRSRVBridgeRGXInitDevPart2);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxinit functions with services
 */
IMG_VOID UnregisterRGXINITFunctions(IMG_VOID)
{
}
