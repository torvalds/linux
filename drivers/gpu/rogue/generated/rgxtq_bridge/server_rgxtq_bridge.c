/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxtq
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxtq
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

#include "rgxtransfer.h"


#include "common_rgxtq_bridge.h"

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

static PVRSRV_ERROR
RGXDestroyTransferContextResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXCreateTransferContext(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCREATETRANSFERCONTEXT *psRGXCreateTransferContextIN,
					 PVRSRV_BRIDGE_OUT_RGXCREATETRANSFERCONTEXT *psRGXCreateTransferContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	IMG_BYTE *psFrameworkCmdInt = IMG_NULL;
	IMG_HANDLE hPrivDataInt = IMG_NULL;
	RGX_SERVER_TQ_CONTEXT * psTransferContextInt = IMG_NULL;
	IMG_HANDLE hTransferContextInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTQ_RGXCREATETRANSFERCONTEXT);




	if (psRGXCreateTransferContextIN->ui32FrameworkCmdize != 0)
	{
		psFrameworkCmdInt = OSAllocMem(psRGXCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE));
		if (!psFrameworkCmdInt)
		{
			psRGXCreateTransferContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXCreateTransferContext_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXCreateTransferContextIN->psFrameworkCmd, psRGXCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, psFrameworkCmdInt, psRGXCreateTransferContextIN->psFrameworkCmd,
				psRGXCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXCreateTransferContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXCreateTransferContext_exit;
			}

				{
					/* Look up the address from the handle */
					psRGXCreateTransferContextOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXCreateTransferContextIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXCreateTransferContextOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateTransferContext_exit;
					}

				}

				{
					/* Look up the address from the handle */
					psRGXCreateTransferContextOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPrivDataInt,
											psRGXCreateTransferContextIN->hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
					if(psRGXCreateTransferContextOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateTransferContext_exit;
					}

				}

	psRGXCreateTransferContextOUT->eError =
		PVRSRVRGXCreateTransferContextKM(psConnection,
					hDevNodeInt,
					psRGXCreateTransferContextIN->ui32Priority,
					psRGXCreateTransferContextIN->sMCUFenceAddr,
					psRGXCreateTransferContextIN->ui32FrameworkCmdize,
					psFrameworkCmdInt,
					hPrivDataInt,
					&psTransferContextInt);
	/* Exit early if bridged call fails */
	if(psRGXCreateTransferContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateTransferContext_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hTransferContextInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_RGX_SERVER_TQ_CONTEXT,
												psTransferContextInt,
												(RESMAN_FREE_FN)&PVRSRVRGXDestroyTransferContextKM);
	if (hTransferContextInt2 == IMG_NULL)
	{
		psRGXCreateTransferContextOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto RGXCreateTransferContext_exit;
	}
	psRGXCreateTransferContextOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateTransferContextOUT->hTransferContext,
							(IMG_HANDLE) hTransferContextInt2,
							PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psRGXCreateTransferContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateTransferContext_exit;
	}


RGXCreateTransferContext_exit:
	if (psRGXCreateTransferContextOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hTransferContextInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hTransferContextInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psTransferContextInt)
		{
			PVRSRVRGXDestroyTransferContextKM(psTransferContextInt);
		}
	}

	if (psFrameworkCmdInt)
		OSFreeMem(psFrameworkCmdInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyTransferContext(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXDESTROYTRANSFERCONTEXT *psRGXDestroyTransferContextIN,
					 PVRSRV_BRIDGE_OUT_RGXDESTROYTRANSFERCONTEXT *psRGXDestroyTransferContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hTransferContextInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTQ_RGXDESTROYTRANSFERCONTEXT);





				{
					/* Look up the address from the handle */
					psRGXDestroyTransferContextOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hTransferContextInt2,
											psRGXDestroyTransferContextIN->hTransferContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT);
					if(psRGXDestroyTransferContextOUT->eError != PVRSRV_OK)
					{
						goto RGXDestroyTransferContext_exit;
					}

				}

	psRGXDestroyTransferContextOUT->eError = RGXDestroyTransferContextResManProxy(hTransferContextInt2);
	/* Exit early if bridged call fails */
	if(psRGXDestroyTransferContextOUT->eError != PVRSRV_OK)
	{
		goto RGXDestroyTransferContext_exit;
	}

	psRGXDestroyTransferContextOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyTransferContextIN->hTransferContext,
					PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT);


RGXDestroyTransferContext_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXSubmitTransfer(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXSUBMITTRANSFER *psRGXSubmitTransferIN,
					 PVRSRV_BRIDGE_OUT_RGXSUBMITTRANSFER *psRGXSubmitTransferOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_TQ_CONTEXT * psTransferContextInt = IMG_NULL;
	IMG_HANDLE hTransferContextInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientFenceCountInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * **psFenceUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE **hFenceUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 **ui32FenceSyncOffsetInt = IMG_NULL;
	IMG_UINT32 **ui32FenceValueInt = IMG_NULL;
	IMG_UINT32 *ui32ClientUpdateCountInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * **psUpdateUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE **hUpdateUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 **ui32UpdateSyncOffsetInt = IMG_NULL;
	IMG_UINT32 **ui32UpdateValueInt = IMG_NULL;
	IMG_UINT32 *ui32ServerSyncCountInt = IMG_NULL;
	IMG_UINT32 **ui32ServerSyncFlagsInt = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * **psServerSyncInt = IMG_NULL;
	IMG_HANDLE **hServerSyncInt2 = IMG_NULL;
	IMG_INT32 *i32FenceFDsInt = IMG_NULL;
	IMG_UINT32 *ui32CommandSizeInt = IMG_NULL;
	IMG_UINT8 **ui8FWCommandInt = IMG_NULL;
	IMG_UINT32 *ui32TQPrepareFlagsInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTQ_RGXSUBMITTRANSFER);




	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		ui32ClientFenceCountInt = OSAllocMem(psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceCountInt)
		{
			psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXSubmitTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXSubmitTransferIN->pui32ClientFenceCount, psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceCountInt, psRGXSubmitTransferIN->pui32ClientFenceCount,
				psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		IMG_UINT32 ui32Pass=0;
		IMG_UINT32 i;
		IMG_UINT32 ui32AllocSize=0;
		IMG_UINT32 ui32Size;
		IMG_UINT8 *pui8Ptr = IMG_NULL;
		IMG_UINT32 ui32AllocSize2=0;
		IMG_UINT32 ui32Size2;
		IMG_UINT8 *pui8Ptr2 = IMG_NULL;

		/*
			Two pass loop, 1st find out the size and 2nd allocation and set offsets.
			Keeps allocation cost down and simplifies the free path
		*/
		for (ui32Pass=0;ui32Pass<2;ui32Pass++)
		{
			ui32Size = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(SYNC_PRIMITIVE_BLOCK * *);
			ui32Size2 = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_HANDLE *);
			if (ui32Pass == 0)
			{
				ui32AllocSize += ui32Size;
				ui32AllocSize2 += ui32Size2;
			}
			else
			{
				pui8Ptr = OSAllocMem(ui32AllocSize);
				if (pui8Ptr == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				psFenceUFOSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK * **) pui8Ptr;
				pui8Ptr += ui32Size;
				pui8Ptr2 = OSAllocMem(ui32AllocSize2);
				if (pui8Ptr2 == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				hFenceUFOSyncPrimBlockInt2 = (IMG_HANDLE **) pui8Ptr2;
				pui8Ptr2 += ui32Size2;
			}
			
			for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
			{
				ui32Size = ui32ClientFenceCountInt[i] * sizeof(SYNC_PRIMITIVE_BLOCK *);		
				ui32Size2 = ui32ClientFenceCountInt[i] * sizeof(IMG_HANDLE);		
				if (ui32Size)
				{
					if (ui32Pass == 0)
					{
						ui32AllocSize += ui32Size;
						ui32AllocSize2 += ui32Size2;
					}
					else
					{
						psFenceUFOSyncPrimBlockInt[i] = (SYNC_PRIMITIVE_BLOCK * *) pui8Ptr;
						pui8Ptr += ui32Size;
						hFenceUFOSyncPrimBlockInt2[i] = (IMG_HANDLE *) pui8Ptr2;
						pui8Ptr2 += ui32Size2;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_HANDLE **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			/* Copy the pointer over from the client side */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) &psRGXSubmitTransferIN->phFenceUFOSyncPrimBlock[i], sizeof(IMG_HANDLE **))
				|| (OSCopyFromUser(NULL, &psPtr, &psRGXSubmitTransferIN->phFenceUFOSyncPrimBlock[i],
				sizeof(IMG_HANDLE **)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPtr, (psRGXSubmitTransferIN->pui32ClientFenceCount[i] * sizeof(IMG_HANDLE)))
				|| (OSCopyFromUser(NULL, (hFenceUFOSyncPrimBlockInt2[i]), psPtr,
				(psRGXSubmitTransferIN->pui32ClientFenceCount[i] * sizeof(IMG_HANDLE))) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
		}
	}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		IMG_UINT32 ui32Pass=0;
		IMG_UINT32 i;
		IMG_UINT32 ui32AllocSize=0;
		IMG_UINT32 ui32Size;
		IMG_UINT8 *pui8Ptr = IMG_NULL;

		/*
			Two pass loop, 1st find out the size and 2nd allocation and set offsets.
			Keeps allocation cost down and simplifies the free path
		*/
		for (ui32Pass=0;ui32Pass<2;ui32Pass++)
		{
			ui32Size = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32 *);
			if (ui32Pass == 0)
			{
				ui32AllocSize += ui32Size;
			}
			else
			{
				pui8Ptr = OSAllocMem(ui32AllocSize);
				if (pui8Ptr == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				ui32FenceSyncOffsetInt = (IMG_UINT32 **) pui8Ptr;
				pui8Ptr += ui32Size;
			}
			
			for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
			{
				ui32Size = ui32ClientFenceCountInt[i] * sizeof(IMG_UINT32);		
				if (ui32Size)
				{
					if (ui32Pass == 0)
					{
						ui32AllocSize += ui32Size;
					}
					else
					{
						ui32FenceSyncOffsetInt[i] = (IMG_UINT32 *) pui8Ptr;
						pui8Ptr += ui32Size;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_UINT32 **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			/* Copy the pointer over from the client side */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) &psRGXSubmitTransferIN->pui32FenceSyncOffset[i], sizeof(IMG_UINT32 **))
				|| (OSCopyFromUser(NULL, &psPtr, &psRGXSubmitTransferIN->pui32FenceSyncOffset[i],
				sizeof(IMG_UINT32 **)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPtr, (psRGXSubmitTransferIN->pui32ClientFenceCount[i] * sizeof(IMG_UINT32)))
				|| (OSCopyFromUser(NULL, (ui32FenceSyncOffsetInt[i]), psPtr,
				(psRGXSubmitTransferIN->pui32ClientFenceCount[i] * sizeof(IMG_UINT32))) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
		}
	}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		IMG_UINT32 ui32Pass=0;
		IMG_UINT32 i;
		IMG_UINT32 ui32AllocSize=0;
		IMG_UINT32 ui32Size;
		IMG_UINT8 *pui8Ptr = IMG_NULL;

		/*
			Two pass loop, 1st find out the size and 2nd allocation and set offsets.
			Keeps allocation cost down and simplifies the free path
		*/
		for (ui32Pass=0;ui32Pass<2;ui32Pass++)
		{
			ui32Size = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32 *);
			if (ui32Pass == 0)
			{
				ui32AllocSize += ui32Size;
			}
			else
			{
				pui8Ptr = OSAllocMem(ui32AllocSize);
				if (pui8Ptr == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				ui32FenceValueInt = (IMG_UINT32 **) pui8Ptr;
				pui8Ptr += ui32Size;
			}
			
			for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
			{
				ui32Size = ui32ClientFenceCountInt[i] * sizeof(IMG_UINT32);		
				if (ui32Size)
				{
					if (ui32Pass == 0)
					{
						ui32AllocSize += ui32Size;
					}
					else
					{
						ui32FenceValueInt[i] = (IMG_UINT32 *) pui8Ptr;
						pui8Ptr += ui32Size;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_UINT32 **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			/* Copy the pointer over from the client side */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) &psRGXSubmitTransferIN->pui32FenceValue[i], sizeof(IMG_UINT32 **))
				|| (OSCopyFromUser(NULL, &psPtr, &psRGXSubmitTransferIN->pui32FenceValue[i],
				sizeof(IMG_UINT32 **)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPtr, (psRGXSubmitTransferIN->pui32ClientFenceCount[i] * sizeof(IMG_UINT32)))
				|| (OSCopyFromUser(NULL, (ui32FenceValueInt[i]), psPtr,
				(psRGXSubmitTransferIN->pui32ClientFenceCount[i] * sizeof(IMG_UINT32))) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
		}
	}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		ui32ClientUpdateCountInt = OSAllocMem(psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateCountInt)
		{
			psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXSubmitTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXSubmitTransferIN->pui32ClientUpdateCount, psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateCountInt, psRGXSubmitTransferIN->pui32ClientUpdateCount,
				psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		IMG_UINT32 ui32Pass=0;
		IMG_UINT32 i;
		IMG_UINT32 ui32AllocSize=0;
		IMG_UINT32 ui32Size;
		IMG_UINT8 *pui8Ptr = IMG_NULL;
		IMG_UINT32 ui32AllocSize2=0;
		IMG_UINT32 ui32Size2;
		IMG_UINT8 *pui8Ptr2 = IMG_NULL;

		/*
			Two pass loop, 1st find out the size and 2nd allocation and set offsets.
			Keeps allocation cost down and simplifies the free path
		*/
		for (ui32Pass=0;ui32Pass<2;ui32Pass++)
		{
			ui32Size = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(SYNC_PRIMITIVE_BLOCK * *);
			ui32Size2 = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_HANDLE *);
			if (ui32Pass == 0)
			{
				ui32AllocSize += ui32Size;
				ui32AllocSize2 += ui32Size2;
			}
			else
			{
				pui8Ptr = OSAllocMem(ui32AllocSize);
				if (pui8Ptr == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				psUpdateUFOSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK * **) pui8Ptr;
				pui8Ptr += ui32Size;
				pui8Ptr2 = OSAllocMem(ui32AllocSize2);
				if (pui8Ptr2 == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				hUpdateUFOSyncPrimBlockInt2 = (IMG_HANDLE **) pui8Ptr2;
				pui8Ptr2 += ui32Size2;
			}
			
			for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
			{
				ui32Size = ui32ClientUpdateCountInt[i] * sizeof(SYNC_PRIMITIVE_BLOCK *);		
				ui32Size2 = ui32ClientUpdateCountInt[i] * sizeof(IMG_HANDLE);		
				if (ui32Size)
				{
					if (ui32Pass == 0)
					{
						ui32AllocSize += ui32Size;
						ui32AllocSize2 += ui32Size2;
					}
					else
					{
						psUpdateUFOSyncPrimBlockInt[i] = (SYNC_PRIMITIVE_BLOCK * *) pui8Ptr;
						pui8Ptr += ui32Size;
						hUpdateUFOSyncPrimBlockInt2[i] = (IMG_HANDLE *) pui8Ptr2;
						pui8Ptr2 += ui32Size2;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_HANDLE **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			/* Copy the pointer over from the client side */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) &psRGXSubmitTransferIN->phUpdateUFOSyncPrimBlock[i], sizeof(IMG_HANDLE **))
				|| (OSCopyFromUser(NULL, &psPtr, &psRGXSubmitTransferIN->phUpdateUFOSyncPrimBlock[i],
				sizeof(IMG_HANDLE **)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPtr, (psRGXSubmitTransferIN->pui32ClientUpdateCount[i] * sizeof(IMG_HANDLE)))
				|| (OSCopyFromUser(NULL, (hUpdateUFOSyncPrimBlockInt2[i]), psPtr,
				(psRGXSubmitTransferIN->pui32ClientUpdateCount[i] * sizeof(IMG_HANDLE))) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
		}
	}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		IMG_UINT32 ui32Pass=0;
		IMG_UINT32 i;
		IMG_UINT32 ui32AllocSize=0;
		IMG_UINT32 ui32Size;
		IMG_UINT8 *pui8Ptr = IMG_NULL;

		/*
			Two pass loop, 1st find out the size and 2nd allocation and set offsets.
			Keeps allocation cost down and simplifies the free path
		*/
		for (ui32Pass=0;ui32Pass<2;ui32Pass++)
		{
			ui32Size = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32 *);
			if (ui32Pass == 0)
			{
				ui32AllocSize += ui32Size;
			}
			else
			{
				pui8Ptr = OSAllocMem(ui32AllocSize);
				if (pui8Ptr == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				ui32UpdateSyncOffsetInt = (IMG_UINT32 **) pui8Ptr;
				pui8Ptr += ui32Size;
			}
			
			for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
			{
				ui32Size = ui32ClientUpdateCountInt[i] * sizeof(IMG_UINT32);		
				if (ui32Size)
				{
					if (ui32Pass == 0)
					{
						ui32AllocSize += ui32Size;
					}
					else
					{
						ui32UpdateSyncOffsetInt[i] = (IMG_UINT32 *) pui8Ptr;
						pui8Ptr += ui32Size;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_UINT32 **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			/* Copy the pointer over from the client side */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) &psRGXSubmitTransferIN->pui32UpdateSyncOffset[i], sizeof(IMG_UINT32 **))
				|| (OSCopyFromUser(NULL, &psPtr, &psRGXSubmitTransferIN->pui32UpdateSyncOffset[i],
				sizeof(IMG_UINT32 **)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPtr, (psRGXSubmitTransferIN->pui32ClientUpdateCount[i] * sizeof(IMG_UINT32)))
				|| (OSCopyFromUser(NULL, (ui32UpdateSyncOffsetInt[i]), psPtr,
				(psRGXSubmitTransferIN->pui32ClientUpdateCount[i] * sizeof(IMG_UINT32))) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
		}
	}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		IMG_UINT32 ui32Pass=0;
		IMG_UINT32 i;
		IMG_UINT32 ui32AllocSize=0;
		IMG_UINT32 ui32Size;
		IMG_UINT8 *pui8Ptr = IMG_NULL;

		/*
			Two pass loop, 1st find out the size and 2nd allocation and set offsets.
			Keeps allocation cost down and simplifies the free path
		*/
		for (ui32Pass=0;ui32Pass<2;ui32Pass++)
		{
			ui32Size = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32 *);
			if (ui32Pass == 0)
			{
				ui32AllocSize += ui32Size;
			}
			else
			{
				pui8Ptr = OSAllocMem(ui32AllocSize);
				if (pui8Ptr == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				ui32UpdateValueInt = (IMG_UINT32 **) pui8Ptr;
				pui8Ptr += ui32Size;
			}
			
			for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
			{
				ui32Size = ui32ClientUpdateCountInt[i] * sizeof(IMG_UINT32);		
				if (ui32Size)
				{
					if (ui32Pass == 0)
					{
						ui32AllocSize += ui32Size;
					}
					else
					{
						ui32UpdateValueInt[i] = (IMG_UINT32 *) pui8Ptr;
						pui8Ptr += ui32Size;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_UINT32 **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			/* Copy the pointer over from the client side */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) &psRGXSubmitTransferIN->pui32UpdateValue[i], sizeof(IMG_UINT32 **))
				|| (OSCopyFromUser(NULL, &psPtr, &psRGXSubmitTransferIN->pui32UpdateValue[i],
				sizeof(IMG_UINT32 **)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPtr, (psRGXSubmitTransferIN->pui32ClientUpdateCount[i] * sizeof(IMG_UINT32)))
				|| (OSCopyFromUser(NULL, (ui32UpdateValueInt[i]), psPtr,
				(psRGXSubmitTransferIN->pui32ClientUpdateCount[i] * sizeof(IMG_UINT32))) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
		}
	}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		ui32ServerSyncCountInt = OSAllocMem(psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32));
		if (!ui32ServerSyncCountInt)
		{
			psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXSubmitTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXSubmitTransferIN->pui32ServerSyncCount, psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ServerSyncCountInt, psRGXSubmitTransferIN->pui32ServerSyncCount,
				psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		IMG_UINT32 ui32Pass=0;
		IMG_UINT32 i;
		IMG_UINT32 ui32AllocSize=0;
		IMG_UINT32 ui32Size;
		IMG_UINT8 *pui8Ptr = IMG_NULL;

		/*
			Two pass loop, 1st find out the size and 2nd allocation and set offsets.
			Keeps allocation cost down and simplifies the free path
		*/
		for (ui32Pass=0;ui32Pass<2;ui32Pass++)
		{
			ui32Size = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32 *);
			if (ui32Pass == 0)
			{
				ui32AllocSize += ui32Size;
			}
			else
			{
				pui8Ptr = OSAllocMem(ui32AllocSize);
				if (pui8Ptr == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				ui32ServerSyncFlagsInt = (IMG_UINT32 **) pui8Ptr;
				pui8Ptr += ui32Size;
			}
			
			for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
			{
				ui32Size = ui32ServerSyncCountInt[i] * sizeof(IMG_UINT32);		
				if (ui32Size)
				{
					if (ui32Pass == 0)
					{
						ui32AllocSize += ui32Size;
					}
					else
					{
						ui32ServerSyncFlagsInt[i] = (IMG_UINT32 *) pui8Ptr;
						pui8Ptr += ui32Size;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_UINT32 **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			/* Copy the pointer over from the client side */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) &psRGXSubmitTransferIN->pui32ServerSyncFlags[i], sizeof(IMG_UINT32 **))
				|| (OSCopyFromUser(NULL, &psPtr, &psRGXSubmitTransferIN->pui32ServerSyncFlags[i],
				sizeof(IMG_UINT32 **)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPtr, (psRGXSubmitTransferIN->pui32ServerSyncCount[i] * sizeof(IMG_UINT32)))
				|| (OSCopyFromUser(NULL, (ui32ServerSyncFlagsInt[i]), psPtr,
				(psRGXSubmitTransferIN->pui32ServerSyncCount[i] * sizeof(IMG_UINT32))) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
		}
	}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		IMG_UINT32 ui32Pass=0;
		IMG_UINT32 i;
		IMG_UINT32 ui32AllocSize=0;
		IMG_UINT32 ui32Size;
		IMG_UINT8 *pui8Ptr = IMG_NULL;
		IMG_UINT32 ui32AllocSize2=0;
		IMG_UINT32 ui32Size2;
		IMG_UINT8 *pui8Ptr2 = IMG_NULL;

		/*
			Two pass loop, 1st find out the size and 2nd allocation and set offsets.
			Keeps allocation cost down and simplifies the free path
		*/
		for (ui32Pass=0;ui32Pass<2;ui32Pass++)
		{
			ui32Size = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(SERVER_SYNC_PRIMITIVE * *);
			ui32Size2 = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_HANDLE *);
			if (ui32Pass == 0)
			{
				ui32AllocSize += ui32Size;
				ui32AllocSize2 += ui32Size2;
			}
			else
			{
				pui8Ptr = OSAllocMem(ui32AllocSize);
				if (pui8Ptr == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				psServerSyncInt = (SERVER_SYNC_PRIMITIVE * **) pui8Ptr;
				pui8Ptr += ui32Size;
				pui8Ptr2 = OSAllocMem(ui32AllocSize2);
				if (pui8Ptr2 == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				hServerSyncInt2 = (IMG_HANDLE **) pui8Ptr2;
				pui8Ptr2 += ui32Size2;
			}
			
			for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
			{
				ui32Size = ui32ServerSyncCountInt[i] * sizeof(SERVER_SYNC_PRIMITIVE *);		
				ui32Size2 = ui32ServerSyncCountInt[i] * sizeof(IMG_HANDLE);		
				if (ui32Size)
				{
					if (ui32Pass == 0)
					{
						ui32AllocSize += ui32Size;
						ui32AllocSize2 += ui32Size2;
					}
					else
					{
						psServerSyncInt[i] = (SERVER_SYNC_PRIMITIVE * *) pui8Ptr;
						pui8Ptr += ui32Size;
						hServerSyncInt2[i] = (IMG_HANDLE *) pui8Ptr2;
						pui8Ptr2 += ui32Size2;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_HANDLE **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			/* Copy the pointer over from the client side */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) &psRGXSubmitTransferIN->phServerSync[i], sizeof(IMG_HANDLE **))
				|| (OSCopyFromUser(NULL, &psPtr, &psRGXSubmitTransferIN->phServerSync[i],
				sizeof(IMG_HANDLE **)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPtr, (psRGXSubmitTransferIN->pui32ServerSyncCount[i] * sizeof(IMG_HANDLE)))
				|| (OSCopyFromUser(NULL, (hServerSyncInt2[i]), psPtr,
				(psRGXSubmitTransferIN->pui32ServerSyncCount[i] * sizeof(IMG_HANDLE))) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
		}
	}
	if (psRGXSubmitTransferIN->ui32NumFenceFDs != 0)
	{
		i32FenceFDsInt = OSAllocMem(psRGXSubmitTransferIN->ui32NumFenceFDs * sizeof(IMG_INT32));
		if (!i32FenceFDsInt)
		{
			psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXSubmitTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXSubmitTransferIN->pi32FenceFDs, psRGXSubmitTransferIN->ui32NumFenceFDs * sizeof(IMG_INT32))
				|| (OSCopyFromUser(NULL, i32FenceFDsInt, psRGXSubmitTransferIN->pi32FenceFDs,
				psRGXSubmitTransferIN->ui32NumFenceFDs * sizeof(IMG_INT32)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		ui32CommandSizeInt = OSAllocMem(psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32));
		if (!ui32CommandSizeInt)
		{
			psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXSubmitTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXSubmitTransferIN->pui32CommandSize, psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32CommandSizeInt, psRGXSubmitTransferIN->pui32CommandSize,
				psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		IMG_UINT32 ui32Pass=0;
		IMG_UINT32 i;
		IMG_UINT32 ui32AllocSize=0;
		IMG_UINT32 ui32Size;
		IMG_UINT8 *pui8Ptr = IMG_NULL;

		/*
			Two pass loop, 1st find out the size and 2nd allocation and set offsets.
			Keeps allocation cost down and simplifies the free path
		*/
		for (ui32Pass=0;ui32Pass<2;ui32Pass++)
		{
			ui32Size = psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT8 *);
			if (ui32Pass == 0)
			{
				ui32AllocSize += ui32Size;
			}
			else
			{
				pui8Ptr = OSAllocMem(ui32AllocSize);
				if (pui8Ptr == IMG_NULL)
				{
					psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto RGXSubmitTransfer_exit;
				}
				ui8FWCommandInt = (IMG_UINT8 **) pui8Ptr;
				pui8Ptr += ui32Size;
			}
			
			for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
			{
				ui32Size = ui32CommandSizeInt[i] * sizeof(IMG_UINT8);		
				if (ui32Size)
				{
					if (ui32Pass == 0)
					{
						ui32AllocSize += ui32Size;
					}
					else
					{
						ui8FWCommandInt[i] = (IMG_UINT8 *) pui8Ptr;
						pui8Ptr += ui32Size;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_UINT8 **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			/* Copy the pointer over from the client side */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) &psRGXSubmitTransferIN->pui8FWCommand[i], sizeof(IMG_UINT8 **))
				|| (OSCopyFromUser(NULL, &psPtr, &psRGXSubmitTransferIN->pui8FWCommand[i],
				sizeof(IMG_UINT8 **)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPtr, (psRGXSubmitTransferIN->pui32CommandSize[i] * sizeof(IMG_UINT8)))
				|| (OSCopyFromUser(NULL, (ui8FWCommandInt[i]), psPtr,
				(psRGXSubmitTransferIN->pui32CommandSize[i] * sizeof(IMG_UINT8))) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}
		}
	}
	if (psRGXSubmitTransferIN->ui32PrepareCount != 0)
	{
		ui32TQPrepareFlagsInt = OSAllocMem(psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32));
		if (!ui32TQPrepareFlagsInt)
		{
			psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXSubmitTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXSubmitTransferIN->pui32TQPrepareFlags, psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32TQPrepareFlagsInt, psRGXSubmitTransferIN->pui32TQPrepareFlags,
				psRGXSubmitTransferIN->ui32PrepareCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXSubmitTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer_exit;
			}

				{
					/* Look up the address from the handle */
					psRGXSubmitTransferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hTransferContextInt2,
											psRGXSubmitTransferIN->hTransferContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT);
					if(psRGXSubmitTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXSubmitTransfer_exit;
					}

					/* Look up the data from the resman address */
					psRGXSubmitTransferOUT->eError = ResManFindPrivateDataByPtr(hTransferContextInt2, (IMG_VOID **) &psTransferContextInt);

					if(psRGXSubmitTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXSubmitTransfer_exit;
					}
				}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			IMG_UINT32 j;
			for (j=0;j<ui32ClientFenceCountInt[i];j++)
			{
				{
					/* Look up the address from the handle */
					psRGXSubmitTransferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hFenceUFOSyncPrimBlockInt2[i][j],
											hFenceUFOSyncPrimBlockInt2[i][j],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXSubmitTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXSubmitTransfer_exit;
					}

					/* Look up the data from the resman address */
					psRGXSubmitTransferOUT->eError = ResManFindPrivateDataByPtr(hFenceUFOSyncPrimBlockInt2[i][j], (IMG_VOID **) &psFenceUFOSyncPrimBlockInt[i][j]);

					if(psRGXSubmitTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXSubmitTransfer_exit;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			IMG_UINT32 j;
			for (j=0;j<ui32ClientUpdateCountInt[i];j++)
			{
				{
					/* Look up the address from the handle */
					psRGXSubmitTransferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hUpdateUFOSyncPrimBlockInt2[i][j],
											hUpdateUFOSyncPrimBlockInt2[i][j],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXSubmitTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXSubmitTransfer_exit;
					}

					/* Look up the data from the resman address */
					psRGXSubmitTransferOUT->eError = ResManFindPrivateDataByPtr(hUpdateUFOSyncPrimBlockInt2[i][j], (IMG_VOID **) &psUpdateUFOSyncPrimBlockInt[i][j]);

					if(psRGXSubmitTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXSubmitTransfer_exit;
					}
				}
			}
		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXSubmitTransferIN->ui32PrepareCount;i++)
		{
			IMG_UINT32 j;
			for (j=0;j<ui32ServerSyncCountInt[i];j++)
			{
				{
					/* Look up the address from the handle */
					psRGXSubmitTransferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hServerSyncInt2[i][j],
											hServerSyncInt2[i][j],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psRGXSubmitTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXSubmitTransfer_exit;
					}

					/* Look up the data from the resman address */
					psRGXSubmitTransferOUT->eError = ResManFindPrivateDataByPtr(hServerSyncInt2[i][j], (IMG_VOID **) &psServerSyncInt[i][j]);

					if(psRGXSubmitTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXSubmitTransfer_exit;
					}
				}
			}
		}
	}

	psRGXSubmitTransferOUT->eError =
		PVRSRVRGXSubmitTransferKM(
					psTransferContextInt,
					psRGXSubmitTransferIN->ui32PrepareCount,
					ui32ClientFenceCountInt,
					psFenceUFOSyncPrimBlockInt,
					ui32FenceSyncOffsetInt,
					ui32FenceValueInt,
					ui32ClientUpdateCountInt,
					psUpdateUFOSyncPrimBlockInt,
					ui32UpdateSyncOffsetInt,
					ui32UpdateValueInt,
					ui32ServerSyncCountInt,
					ui32ServerSyncFlagsInt,
					psServerSyncInt,
					psRGXSubmitTransferIN->ui32NumFenceFDs,
					i32FenceFDsInt,
					ui32CommandSizeInt,
					ui8FWCommandInt,
					ui32TQPrepareFlagsInt,
					psRGXSubmitTransferIN->ui32ExternalJobReference,
					psRGXSubmitTransferIN->ui32InternalJobReference);



RGXSubmitTransfer_exit:
	if (ui32ClientFenceCountInt)
		OSFreeMem(ui32ClientFenceCountInt);
	if (psFenceUFOSyncPrimBlockInt)
		OSFreeMem(psFenceUFOSyncPrimBlockInt);
	if (hFenceUFOSyncPrimBlockInt2)
		OSFreeMem(hFenceUFOSyncPrimBlockInt2);
	if (ui32FenceSyncOffsetInt)
		OSFreeMem(ui32FenceSyncOffsetInt);
	if (ui32FenceValueInt)
		OSFreeMem(ui32FenceValueInt);
	if (ui32ClientUpdateCountInt)
		OSFreeMem(ui32ClientUpdateCountInt);
	if (psUpdateUFOSyncPrimBlockInt)
		OSFreeMem(psUpdateUFOSyncPrimBlockInt);
	if (hUpdateUFOSyncPrimBlockInt2)
		OSFreeMem(hUpdateUFOSyncPrimBlockInt2);
	if (ui32UpdateSyncOffsetInt)
		OSFreeMem(ui32UpdateSyncOffsetInt);
	if (ui32UpdateValueInt)
		OSFreeMem(ui32UpdateValueInt);
	if (ui32ServerSyncCountInt)
		OSFreeMem(ui32ServerSyncCountInt);
	if (ui32ServerSyncFlagsInt)
		OSFreeMem(ui32ServerSyncFlagsInt);
	if (psServerSyncInt)
		OSFreeMem(psServerSyncInt);
	if (hServerSyncInt2)
		OSFreeMem(hServerSyncInt2);
	if (i32FenceFDsInt)
		OSFreeMem(i32FenceFDsInt);
	if (ui32CommandSizeInt)
		OSFreeMem(ui32CommandSizeInt);
	if (ui8FWCommandInt)
		OSFreeMem(ui8FWCommandInt);
	if (ui32TQPrepareFlagsInt)
		OSFreeMem(ui32TQPrepareFlagsInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXSetTransferContextPriority(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPRIORITY *psRGXSetTransferContextPriorityIN,
					 PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPRIORITY *psRGXSetTransferContextPriorityOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_TQ_CONTEXT * psTransferContextInt = IMG_NULL;
	IMG_HANDLE hTransferContextInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTQ_RGXSETTRANSFERCONTEXTPRIORITY);





				{
					/* Look up the address from the handle */
					psRGXSetTransferContextPriorityOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hTransferContextInt2,
											psRGXSetTransferContextPriorityIN->hTransferContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT);
					if(psRGXSetTransferContextPriorityOUT->eError != PVRSRV_OK)
					{
						goto RGXSetTransferContextPriority_exit;
					}

					/* Look up the data from the resman address */
					psRGXSetTransferContextPriorityOUT->eError = ResManFindPrivateDataByPtr(hTransferContextInt2, (IMG_VOID **) &psTransferContextInt);

					if(psRGXSetTransferContextPriorityOUT->eError != PVRSRV_OK)
					{
						goto RGXSetTransferContextPriority_exit;
					}
				}

	psRGXSetTransferContextPriorityOUT->eError =
		PVRSRVRGXSetTransferContextPriorityKM(psConnection,
					psTransferContextInt,
					psRGXSetTransferContextPriorityIN->ui32Priority);



RGXSetTransferContextPriority_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXKickSyncTransfer(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXKICKSYNCTRANSFER *psRGXKickSyncTransferIN,
					 PVRSRV_BRIDGE_OUT_RGXKICKSYNCTRANSFER *psRGXKickSyncTransferOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_TQ_CONTEXT * psTransferContextInt = IMG_NULL;
	IMG_HANDLE hTransferContextInt2 = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientFenceUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClientFenceUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientFenceSyncOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32ClientFenceValueInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientUpdateUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClientUpdateUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientUpdateSyncOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32ClientUpdateValueInt = IMG_NULL;
	IMG_UINT32 *ui32ServerSyncFlagsInt = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * *psServerSyncsInt = IMG_NULL;
	IMG_HANDLE *hServerSyncsInt2 = IMG_NULL;
	IMG_INT32 *i32FenceFDsInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXTQ_RGXKICKSYNCTRANSFER);




	if (psRGXKickSyncTransferIN->ui32ClientFenceCount != 0)
	{
		psClientFenceUFOSyncPrimBlockInt = OSAllocMem(psRGXKickSyncTransferIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientFenceUFOSyncPrimBlockInt)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
		hClientFenceUFOSyncPrimBlockInt2 = OSAllocMem(psRGXKickSyncTransferIN->ui32ClientFenceCount * sizeof(IMG_HANDLE));
		if (!hClientFenceUFOSyncPrimBlockInt2)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTransferIN->phClientFenceUFOSyncPrimBlock, psRGXKickSyncTransferIN->ui32ClientFenceCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientFenceUFOSyncPrimBlockInt2, psRGXKickSyncTransferIN->phClientFenceUFOSyncPrimBlock,
				psRGXKickSyncTransferIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTransfer_exit;
			}
	if (psRGXKickSyncTransferIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceSyncOffsetInt = OSAllocMem(psRGXKickSyncTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceSyncOffsetInt)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTransferIN->pui32ClientFenceSyncOffset, psRGXKickSyncTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceSyncOffsetInt, psRGXKickSyncTransferIN->pui32ClientFenceSyncOffset,
				psRGXKickSyncTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTransfer_exit;
			}
	if (psRGXKickSyncTransferIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceValueInt = OSAllocMem(psRGXKickSyncTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceValueInt)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTransferIN->pui32ClientFenceValue, psRGXKickSyncTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceValueInt, psRGXKickSyncTransferIN->pui32ClientFenceValue,
				psRGXKickSyncTransferIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTransfer_exit;
			}
	if (psRGXKickSyncTransferIN->ui32ClientUpdateCount != 0)
	{
		psClientUpdateUFOSyncPrimBlockInt = OSAllocMem(psRGXKickSyncTransferIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientUpdateUFOSyncPrimBlockInt)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
		hClientUpdateUFOSyncPrimBlockInt2 = OSAllocMem(psRGXKickSyncTransferIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE));
		if (!hClientUpdateUFOSyncPrimBlockInt2)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTransferIN->phClientUpdateUFOSyncPrimBlock, psRGXKickSyncTransferIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientUpdateUFOSyncPrimBlockInt2, psRGXKickSyncTransferIN->phClientUpdateUFOSyncPrimBlock,
				psRGXKickSyncTransferIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTransfer_exit;
			}
	if (psRGXKickSyncTransferIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateSyncOffsetInt = OSAllocMem(psRGXKickSyncTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateSyncOffsetInt)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTransferIN->pui32ClientUpdateSyncOffset, psRGXKickSyncTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateSyncOffsetInt, psRGXKickSyncTransferIN->pui32ClientUpdateSyncOffset,
				psRGXKickSyncTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTransfer_exit;
			}
	if (psRGXKickSyncTransferIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateValueInt = OSAllocMem(psRGXKickSyncTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateValueInt)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTransferIN->pui32ClientUpdateValue, psRGXKickSyncTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateValueInt, psRGXKickSyncTransferIN->pui32ClientUpdateValue,
				psRGXKickSyncTransferIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTransfer_exit;
			}
	if (psRGXKickSyncTransferIN->ui32ServerSyncCount != 0)
	{
		ui32ServerSyncFlagsInt = OSAllocMem(psRGXKickSyncTransferIN->ui32ServerSyncCount * sizeof(IMG_UINT32));
		if (!ui32ServerSyncFlagsInt)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTransferIN->pui32ServerSyncFlags, psRGXKickSyncTransferIN->ui32ServerSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ServerSyncFlagsInt, psRGXKickSyncTransferIN->pui32ServerSyncFlags,
				psRGXKickSyncTransferIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTransfer_exit;
			}
	if (psRGXKickSyncTransferIN->ui32ServerSyncCount != 0)
	{
		psServerSyncsInt = OSAllocMem(psRGXKickSyncTransferIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psServerSyncsInt)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
		hServerSyncsInt2 = OSAllocMem(psRGXKickSyncTransferIN->ui32ServerSyncCount * sizeof(IMG_HANDLE));
		if (!hServerSyncsInt2)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTransferIN->phServerSyncs, psRGXKickSyncTransferIN->ui32ServerSyncCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hServerSyncsInt2, psRGXKickSyncTransferIN->phServerSyncs,
				psRGXKickSyncTransferIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTransfer_exit;
			}
	if (psRGXKickSyncTransferIN->ui32NumFenceFDs != 0)
	{
		i32FenceFDsInt = OSAllocMem(psRGXKickSyncTransferIN->ui32NumFenceFDs * sizeof(IMG_INT32));
		if (!i32FenceFDsInt)
		{
			psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncTransfer_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncTransferIN->pi32FenceFDs, psRGXKickSyncTransferIN->ui32NumFenceFDs * sizeof(IMG_INT32))
				|| (OSCopyFromUser(NULL, i32FenceFDsInt, psRGXKickSyncTransferIN->pi32FenceFDs,
				psRGXKickSyncTransferIN->ui32NumFenceFDs * sizeof(IMG_INT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncTransfer_exit;
			}

				{
					/* Look up the address from the handle */
					psRGXKickSyncTransferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hTransferContextInt2,
											psRGXKickSyncTransferIN->hTransferContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT);
					if(psRGXKickSyncTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTransfer_exit;
					}

					/* Look up the data from the resman address */
					psRGXKickSyncTransferOUT->eError = ResManFindPrivateDataByPtr(hTransferContextInt2, (IMG_VOID **) &psTransferContextInt);

					if(psRGXKickSyncTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTransfer_exit;
					}
				}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncTransferIN->ui32ClientFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncTransferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hClientFenceUFOSyncPrimBlockInt2[i],
											hClientFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickSyncTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTransfer_exit;
					}

					/* Look up the data from the resman address */
					psRGXKickSyncTransferOUT->eError = ResManFindPrivateDataByPtr(hClientFenceUFOSyncPrimBlockInt2[i], (IMG_VOID **) &psClientFenceUFOSyncPrimBlockInt[i]);

					if(psRGXKickSyncTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTransfer_exit;
					}
				}
		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncTransferIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncTransferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hClientUpdateUFOSyncPrimBlockInt2[i],
											hClientUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickSyncTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTransfer_exit;
					}

					/* Look up the data from the resman address */
					psRGXKickSyncTransferOUT->eError = ResManFindPrivateDataByPtr(hClientUpdateUFOSyncPrimBlockInt2[i], (IMG_VOID **) &psClientUpdateUFOSyncPrimBlockInt[i]);

					if(psRGXKickSyncTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTransfer_exit;
					}
				}
		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncTransferIN->ui32ServerSyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncTransferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hServerSyncsInt2[i],
											hServerSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psRGXKickSyncTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTransfer_exit;
					}

					/* Look up the data from the resman address */
					psRGXKickSyncTransferOUT->eError = ResManFindPrivateDataByPtr(hServerSyncsInt2[i], (IMG_VOID **) &psServerSyncsInt[i]);

					if(psRGXKickSyncTransferOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncTransfer_exit;
					}
				}
		}
	}

	psRGXKickSyncTransferOUT->eError =
		PVRSRVRGXKickSyncTransferKM(
					psTransferContextInt,
					psRGXKickSyncTransferIN->ui32ClientFenceCount,
					psClientFenceUFOSyncPrimBlockInt,
					ui32ClientFenceSyncOffsetInt,
					ui32ClientFenceValueInt,
					psRGXKickSyncTransferIN->ui32ClientUpdateCount,
					psClientUpdateUFOSyncPrimBlockInt,
					ui32ClientUpdateSyncOffsetInt,
					ui32ClientUpdateValueInt,
					psRGXKickSyncTransferIN->ui32ServerSyncCount,
					ui32ServerSyncFlagsInt,
					psServerSyncsInt,
					psRGXKickSyncTransferIN->ui32NumFenceFDs,
					i32FenceFDsInt,
					psRGXKickSyncTransferIN->ui32TQPrepareFlags);



RGXKickSyncTransfer_exit:
	if (psClientFenceUFOSyncPrimBlockInt)
		OSFreeMem(psClientFenceUFOSyncPrimBlockInt);
	if (hClientFenceUFOSyncPrimBlockInt2)
		OSFreeMem(hClientFenceUFOSyncPrimBlockInt2);
	if (ui32ClientFenceSyncOffsetInt)
		OSFreeMem(ui32ClientFenceSyncOffsetInt);
	if (ui32ClientFenceValueInt)
		OSFreeMem(ui32ClientFenceValueInt);
	if (psClientUpdateUFOSyncPrimBlockInt)
		OSFreeMem(psClientUpdateUFOSyncPrimBlockInt);
	if (hClientUpdateUFOSyncPrimBlockInt2)
		OSFreeMem(hClientUpdateUFOSyncPrimBlockInt2);
	if (ui32ClientUpdateSyncOffsetInt)
		OSFreeMem(ui32ClientUpdateSyncOffsetInt);
	if (ui32ClientUpdateValueInt)
		OSFreeMem(ui32ClientUpdateValueInt);
	if (ui32ServerSyncFlagsInt)
		OSFreeMem(ui32ServerSyncFlagsInt);
	if (psServerSyncsInt)
		OSFreeMem(psServerSyncsInt);
	if (hServerSyncsInt2)
		OSFreeMem(hServerSyncsInt2);
	if (i32FenceFDsInt)
		OSFreeMem(i32FenceFDsInt);

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterRGXTQFunctions(IMG_VOID);
IMG_VOID UnregisterRGXTQFunctions(IMG_VOID);

/*
 * Register all RGXTQ functions with services
 */
PVRSRV_ERROR RegisterRGXTQFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ_RGXCREATETRANSFERCONTEXT, PVRSRVBridgeRGXCreateTransferContext);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ_RGXDESTROYTRANSFERCONTEXT, PVRSRVBridgeRGXDestroyTransferContext);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ_RGXSUBMITTRANSFER, PVRSRVBridgeRGXSubmitTransfer);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ_RGXSETTRANSFERCONTEXTPRIORITY, PVRSRVBridgeRGXSetTransferContextPriority);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ_RGXKICKSYNCTRANSFER, PVRSRVBridgeRGXKickSyncTransfer);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxtq functions with services
 */
IMG_VOID UnregisterRGXTQFunctions(IMG_VOID)
{
}
