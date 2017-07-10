/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxtq2
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxtq2
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

#include "rgxtdmtransfer.h"


#include "common_rgxtq2_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>


#include "rgx_bvnc_defs_km.h"




/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXTDMCreateTransferContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXTDMCREATETRANSFERCONTEXT *psRGXTDMCreateTransferContextIN,
					  PVRSRV_BRIDGE_OUT_RGXTDMCREATETRANSFERCONTEXT *psRGXTDMCreateTransferContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_BYTE *psFrameworkCmdInt = NULL;
	IMG_HANDLE hPrivData = psRGXTDMCreateTransferContextIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;
	RGX_SERVER_TQ_TDM_CONTEXT * psTransferContextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXTDMCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE)) +
			0;

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMCreateTransferContextOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMCreateTransferContext_exit;
		}
	}




	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXTDMCreateTransferContextIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXTDMCreateTransferContextIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXTDMCreateTransferContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXTDMCreateTransferContext_exit;
			}
		}
	}

	if (psRGXTDMCreateTransferContextIN->ui32FrameworkCmdize != 0)
	{
		psFrameworkCmdInt = (IMG_BYTE*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE);
	}

			/* Copy the data over */
			if (psRGXTDMCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE) > 0)
			{
				if ( OSCopyFromUser(NULL, psFrameworkCmdInt, psRGXTDMCreateTransferContextIN->psFrameworkCmd, psRGXTDMCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE)) != PVRSRV_OK )
				{
					psRGXTDMCreateTransferContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMCreateTransferContext_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXTDMCreateTransferContextOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &hPrivDataInt,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
											IMG_TRUE);
					if(psRGXTDMCreateTransferContextOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXTDMCreateTransferContext_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXTDMCreateTransferContextOUT->eError =
		PVRSRVRGXTDMCreateTransferContextKM(psConnection, OSGetDevData(psConnection),
					psRGXTDMCreateTransferContextIN->ui32Priority,
					psRGXTDMCreateTransferContextIN->sMCUFenceAddr,
					psRGXTDMCreateTransferContextIN->ui32FrameworkCmdize,
					psFrameworkCmdInt,
					hPrivDataInt,
					&psTransferContextInt);
	/* Exit early if bridged call fails */
	if(psRGXTDMCreateTransferContextOUT->eError != PVRSRV_OK)
	{
		goto RGXTDMCreateTransferContext_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXTDMCreateTransferContextOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXTDMCreateTransferContextOUT->hTransferContext,
							(void *) psTransferContextInt,
							PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&PVRSRVRGXTDMDestroyTransferContextKM);
	if (psRGXTDMCreateTransferContextOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXTDMCreateTransferContext_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXTDMCreateTransferContext_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(hPrivDataInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

	if (psRGXTDMCreateTransferContextOUT->eError != PVRSRV_OK)
	{
		if (psTransferContextInt)
		{
			PVRSRVRGXTDMDestroyTransferContextKM(psTransferContextInt);
		}
	}

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if(pArrayArgsBuffer)
#else
	if(!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXTDMDestroyTransferContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXTDMDESTROYTRANSFERCONTEXT *psRGXTDMDestroyTransferContextIN,
					  PVRSRV_BRIDGE_OUT_RGXTDMDESTROYTRANSFERCONTEXT *psRGXTDMDestroyTransferContextOUT,
					 CONNECTION_DATA *psConnection)
{


	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMDestroyTransferContextOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMDestroyTransferContext_exit;
		}
	}







	/* Lock over handle destruction. */
	LockHandle();





	psRGXTDMDestroyTransferContextOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXTDMDestroyTransferContextIN->hTransferContext,
					PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT);
	if ((psRGXTDMDestroyTransferContextOUT->eError != PVRSRV_OK) &&
	    (psRGXTDMDestroyTransferContextOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXTDMDestroyTransferContext: %s",
		        PVRSRVGetErrorStringKM(psRGXTDMDestroyTransferContextOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXTDMDestroyTransferContext_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXTDMDestroyTransferContext_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXTDMSubmitTransfer(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXTDMSUBMITTRANSFER *psRGXTDMSubmitTransferIN,
					  PVRSRV_BRIDGE_OUT_RGXTDMSUBMITTRANSFER *psRGXTDMSubmitTransferOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hTransferContext = psRGXTDMSubmitTransferIN->hTransferContext;
	RGX_SERVER_TQ_TDM_CONTEXT * psTransferContextInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psFenceUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hFenceUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32FenceSyncOffsetInt = NULL;
	IMG_UINT32 *ui32FenceValueInt = NULL;
	SYNC_PRIMITIVE_BLOCK * *psUpdateUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hUpdateUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32UpdateSyncOffsetInt = NULL;
	IMG_UINT32 *ui32UpdateValueInt = NULL;
	IMG_UINT32 *ui32ServerSyncFlagsInt = NULL;
	SERVER_SYNC_PRIMITIVE * *psServerSyncInt = NULL;
	IMG_HANDLE *hServerSyncInt2 = NULL;
	IMG_CHAR *uiUpdateFenceNameInt = NULL;
	IMG_UINT8 *ui8FWCommandInt = NULL;
	IMG_UINT32 *ui32SyncPMRFlagsInt = NULL;
	PMR * *psSyncPMRsInt = NULL;
	IMG_HANDLE *hSyncPMRsInt2 = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) +
			(psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) +
			(psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) +
			(psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) +
			(psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXTDMSubmitTransferIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) +
			(psRGXTDMSubmitTransferIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *)) +
			(psRGXTDMSubmitTransferIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) +
			(32 * sizeof(IMG_CHAR)) +
			(psRGXTDMSubmitTransferIN->ui32CommandSize * sizeof(IMG_UINT8)) +
			(psRGXTDMSubmitTransferIN->ui32SyncPMRCount * sizeof(IMG_UINT32)) +
			(psRGXTDMSubmitTransferIN->ui32SyncPMRCount * sizeof(PMR *)) +
			(psRGXTDMSubmitTransferIN->ui32SyncPMRCount * sizeof(IMG_HANDLE)) +
			0;

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMSubmitTransfer_exit;
		}
	}




	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXTDMSubmitTransferIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXTDMSubmitTransferIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXTDMSubmitTransfer_exit;
			}
		}
	}

	if (psRGXTDMSubmitTransferIN->ui32ClientFenceCount != 0)
	{
		psFenceUFOSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hFenceUFOSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hFenceUFOSyncPrimBlockInt2, psRGXTDMSubmitTransferIN->phFenceUFOSyncPrimBlock, psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	if (psRGXTDMSubmitTransferIN->ui32ClientFenceCount != 0)
	{
		ui32FenceSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32FenceSyncOffsetInt, psRGXTDMSubmitTransferIN->pui32FenceSyncOffset, psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	if (psRGXTDMSubmitTransferIN->ui32ClientFenceCount != 0)
	{
		ui32FenceValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32FenceValueInt, psRGXTDMSubmitTransferIN->pui32FenceValue, psRGXTDMSubmitTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	if (psRGXTDMSubmitTransferIN->ui32ClientUpdateCount != 0)
	{
		psUpdateUFOSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hUpdateUFOSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hUpdateUFOSyncPrimBlockInt2, psRGXTDMSubmitTransferIN->phUpdateUFOSyncPrimBlock, psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	if (psRGXTDMSubmitTransferIN->ui32ClientUpdateCount != 0)
	{
		ui32UpdateSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32UpdateSyncOffsetInt, psRGXTDMSubmitTransferIN->pui32UpdateSyncOffset, psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	if (psRGXTDMSubmitTransferIN->ui32ClientUpdateCount != 0)
	{
		ui32UpdateValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32UpdateValueInt, psRGXTDMSubmitTransferIN->pui32UpdateValue, psRGXTDMSubmitTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	if (psRGXTDMSubmitTransferIN->ui32ServerSyncCount != 0)
	{
		ui32ServerSyncFlagsInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ServerSyncCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32ServerSyncCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ServerSyncFlagsInt, psRGXTDMSubmitTransferIN->pui32ServerSyncFlags, psRGXTDMSubmitTransferIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	if (psRGXTDMSubmitTransferIN->ui32ServerSyncCount != 0)
	{
		psServerSyncInt = (SERVER_SYNC_PRIMITIVE **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *);
		hServerSyncInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32ServerSyncCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32ServerSyncCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hServerSyncInt2, psRGXTDMSubmitTransferIN->phServerSync, psRGXTDMSubmitTransferIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	
	{
		uiUpdateFenceNameInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += 32 * sizeof(IMG_CHAR);
	}

			/* Copy the data over */
			if (32 * sizeof(IMG_CHAR) > 0)
			{
				if ( OSCopyFromUser(NULL, uiUpdateFenceNameInt, psRGXTDMSubmitTransferIN->puiUpdateFenceName, 32 * sizeof(IMG_CHAR)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	if (psRGXTDMSubmitTransferIN->ui32CommandSize != 0)
	{
		ui8FWCommandInt = (IMG_UINT8*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32CommandSize * sizeof(IMG_UINT8);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32CommandSize * sizeof(IMG_UINT8) > 0)
			{
				if ( OSCopyFromUser(NULL, ui8FWCommandInt, psRGXTDMSubmitTransferIN->pui8FWCommand, psRGXTDMSubmitTransferIN->ui32CommandSize * sizeof(IMG_UINT8)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	if (psRGXTDMSubmitTransferIN->ui32SyncPMRCount != 0)
	{
		ui32SyncPMRFlagsInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32SyncPMRCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32SyncPMRCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32SyncPMRFlagsInt, psRGXTDMSubmitTransferIN->pui32SyncPMRFlags, psRGXTDMSubmitTransferIN->ui32SyncPMRCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}
	if (psRGXTDMSubmitTransferIN->ui32SyncPMRCount != 0)
	{
		psSyncPMRsInt = (PMR **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32SyncPMRCount * sizeof(PMR *);
		hSyncPMRsInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXTDMSubmitTransferIN->ui32SyncPMRCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXTDMSubmitTransferIN->ui32SyncPMRCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hSyncPMRsInt2, psRGXTDMSubmitTransferIN->phSyncPMRs, psRGXTDMSubmitTransferIN->ui32SyncPMRCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXTDMSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXTDMSubmitTransfer_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXTDMSubmitTransferOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psTransferContextInt,
											hTransferContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT,
											IMG_TRUE);
					if(psRGXTDMSubmitTransferOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXTDMSubmitTransfer_exit;
					}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXTDMSubmitTransferIN->ui32ClientFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXTDMSubmitTransferOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psFenceUFOSyncPrimBlockInt[i],
											hFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXTDMSubmitTransferOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXTDMSubmitTransfer_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXTDMSubmitTransferIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXTDMSubmitTransferOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psUpdateUFOSyncPrimBlockInt[i],
											hUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXTDMSubmitTransferOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXTDMSubmitTransfer_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXTDMSubmitTransferIN->ui32ServerSyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXTDMSubmitTransferOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psServerSyncInt[i],
											hServerSyncInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
											IMG_TRUE);
					if(psRGXTDMSubmitTransferOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXTDMSubmitTransfer_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXTDMSubmitTransferIN->ui32SyncPMRCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXTDMSubmitTransferOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psSyncPMRsInt[i],
											hSyncPMRsInt2[i],
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psRGXTDMSubmitTransferOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXTDMSubmitTransfer_exit;
					}
				}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXTDMSubmitTransferOUT->eError =
		PVRSRVRGXTDMSubmitTransferKM(
					psTransferContextInt,
					psRGXTDMSubmitTransferIN->ui32PDumpFlags,
					psRGXTDMSubmitTransferIN->ui32ClientCacheOpSeqNum,
					psRGXTDMSubmitTransferIN->ui32ClientFenceCount,
					psFenceUFOSyncPrimBlockInt,
					ui32FenceSyncOffsetInt,
					ui32FenceValueInt,
					psRGXTDMSubmitTransferIN->ui32ClientUpdateCount,
					psUpdateUFOSyncPrimBlockInt,
					ui32UpdateSyncOffsetInt,
					ui32UpdateValueInt,
					psRGXTDMSubmitTransferIN->ui32ServerSyncCount,
					ui32ServerSyncFlagsInt,
					psServerSyncInt,
					psRGXTDMSubmitTransferIN->i32CheckFenceFD,
					psRGXTDMSubmitTransferIN->i32UpdateTimelineFD,
					&psRGXTDMSubmitTransferOUT->i32UpdateFenceFD,
					uiUpdateFenceNameInt,
					psRGXTDMSubmitTransferIN->ui32CommandSize,
					ui8FWCommandInt,
					psRGXTDMSubmitTransferIN->ui32ExternalJobReference,
					psRGXTDMSubmitTransferIN->ui32SyncPMRCount,
					ui32SyncPMRFlagsInt,
					psSyncPMRsInt);




RGXTDMSubmitTransfer_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psTransferContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hTransferContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT);
						}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXTDMSubmitTransferIN->ui32ClientFenceCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psFenceUFOSyncPrimBlockInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXTDMSubmitTransferIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psUpdateUFOSyncPrimBlockInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXTDMSubmitTransferIN->ui32ServerSyncCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psServerSyncInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hServerSyncInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
						}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXTDMSubmitTransferIN->ui32SyncPMRCount;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psSyncPMRsInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hSyncPMRsInt2[i],
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
		}
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if(pArrayArgsBuffer)
#else
	if(!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXTDMSetTransferContextPriority(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPRIORITY *psRGXTDMSetTransferContextPriorityIN,
					  PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPRIORITY *psRGXTDMSetTransferContextPriorityOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hTransferContext = psRGXTDMSetTransferContextPriorityIN->hTransferContext;
	RGX_SERVER_TQ_TDM_CONTEXT * psTransferContextInt = NULL;


	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMSetTransferContextPriorityOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMSetTransferContextPriority_exit;
		}
	}





	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXTDMSetTransferContextPriorityOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psTransferContextInt,
											hTransferContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT,
											IMG_TRUE);
					if(psRGXTDMSetTransferContextPriorityOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXTDMSetTransferContextPriority_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXTDMSetTransferContextPriorityOUT->eError =
		PVRSRVRGXTDMSetTransferContextPriorityKM(psConnection, OSGetDevData(psConnection),
					psTransferContextInt,
					psRGXTDMSetTransferContextPriorityIN->ui32Priority);




RGXTDMSetTransferContextPriority_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psTransferContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hTransferContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXTDMNotifyWriteOffsetUpdate(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXTDMNOTIFYWRITEOFFSETUPDATE *psRGXTDMNotifyWriteOffsetUpdateIN,
					  PVRSRV_BRIDGE_OUT_RGXTDMNOTIFYWRITEOFFSETUPDATE *psRGXTDMNotifyWriteOffsetUpdateOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hTransferContext = psRGXTDMNotifyWriteOffsetUpdateIN->hTransferContext;
	RGX_SERVER_TQ_TDM_CONTEXT * psTransferContextInt = NULL;


	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMNotifyWriteOffsetUpdateOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMNotifyWriteOffsetUpdate_exit;
		}
	}





	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXTDMNotifyWriteOffsetUpdateOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psTransferContextInt,
											hTransferContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT,
											IMG_TRUE);
					if(psRGXTDMNotifyWriteOffsetUpdateOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXTDMNotifyWriteOffsetUpdate_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXTDMNotifyWriteOffsetUpdateOUT->eError =
		PVRSRVRGXTDMNotifyWriteOffsetUpdateKM(
					psTransferContextInt,
					psRGXTDMNotifyWriteOffsetUpdateIN->ui32PDumpFlags);




RGXTDMNotifyWriteOffsetUpdate_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psTransferContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hTransferContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;

PVRSRV_ERROR InitRGXTQ2Bridge(void);
PVRSRV_ERROR DeinitRGXTQ2Bridge(void);

/*
 * Register all RGXTQ2 functions with services
 */
PVRSRV_ERROR InitRGXTQ2Bridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2, PVRSRV_BRIDGE_RGXTQ2_RGXTDMCREATETRANSFERCONTEXT, PVRSRVBridgeRGXTDMCreateTransferContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2, PVRSRV_BRIDGE_RGXTQ2_RGXTDMDESTROYTRANSFERCONTEXT, PVRSRVBridgeRGXTDMDestroyTransferContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2, PVRSRV_BRIDGE_RGXTQ2_RGXTDMSUBMITTRANSFER, PVRSRVBridgeRGXTDMSubmitTransfer,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2, PVRSRV_BRIDGE_RGXTQ2_RGXTDMSETTRANSFERCONTEXTPRIORITY, PVRSRVBridgeRGXTDMSetTransferContextPriority,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2, PVRSRV_BRIDGE_RGXTQ2_RGXTDMNOTIFYWRITEOFFSETUPDATE, PVRSRVBridgeRGXTDMNotifyWriteOffsetUpdate,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all rgxtq2 functions with services
 */
PVRSRV_ERROR DeinitRGXTQ2Bridge(void)
{
	return PVRSRV_OK;
}
