/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxkicksync
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxkicksync
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
#include <linux/uaccess.h>

#include "osfunc.h"
#include "img_defs.h"

#include "rgxkicksync.h"


#include "common_rgxkicksync_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>






/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXCreateKickSyncContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATEKICKSYNCCONTEXT *psRGXCreateKickSyncContextIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATEKICKSYNCCONTEXT *psRGXCreateKickSyncContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPrivData = psRGXCreateKickSyncContextIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;
	RGX_SERVER_KICKSYNC_CONTEXT * psKickSyncContextInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXCreateKickSyncContextOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &hPrivDataInt,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
											IMG_TRUE);
					if(psRGXCreateKickSyncContextOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXCreateKickSyncContext_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXCreateKickSyncContextOUT->eError =
		PVRSRVRGXCreateKickSyncContextKM(psConnection, OSGetDevData(psConnection),
					hPrivDataInt,
					&psKickSyncContextInt);
	/* Exit early if bridged call fails */
	if(psRGXCreateKickSyncContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateKickSyncContext_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRGXCreateKickSyncContextOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRGXCreateKickSyncContextOUT->hKickSyncContext,
							(void *) psKickSyncContextInt,
							PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&PVRSRVRGXDestroyKickSyncContextKM);
	if (psRGXCreateKickSyncContextOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RGXCreateKickSyncContext_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RGXCreateKickSyncContext_exit:

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

	if (psRGXCreateKickSyncContextOUT->eError != PVRSRV_OK)
	{
		if (psKickSyncContextInt)
		{
			PVRSRVRGXDestroyKickSyncContextKM(psKickSyncContextInt);
		}
	}


	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDestroyKickSyncContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYKICKSYNCCONTEXT *psRGXDestroyKickSyncContextIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYKICKSYNCCONTEXT *psRGXDestroyKickSyncContextOUT,
					 CONNECTION_DATA *psConnection)
{









	/* Lock over handle destruction. */
	LockHandle();





	psRGXDestroyKickSyncContextOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyKickSyncContextIN->hKickSyncContext,
					PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT);
	if ((psRGXDestroyKickSyncContextOUT->eError != PVRSRV_OK) &&
	    (psRGXDestroyKickSyncContextOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRGXDestroyKickSyncContext: %s",
		        PVRSRVGetErrorStringKM(psRGXDestroyKickSyncContextOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RGXDestroyKickSyncContext_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RGXDestroyKickSyncContext_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRGXKickSync(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXKICKSYNC *psRGXKickSyncIN,
					  PVRSRV_BRIDGE_OUT_RGXKICKSYNC *psRGXKickSyncOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hKickSyncContext = psRGXKickSyncIN->hKickSyncContext;
	RGX_SERVER_KICKSYNC_CONTEXT * psKickSyncContextInt = NULL;
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

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXKickSyncIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) +
			(psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) +
			(psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) +
			(psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
			(psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) +
			(psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
			(psRGXKickSyncIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) +
			(psRGXKickSyncIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *)) +
			(psRGXKickSyncIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) +
			(32 * sizeof(IMG_CHAR)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXKickSyncIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXKickSyncIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXKickSyncOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXKickSync_exit;
			}
		}
	}

	if (psRGXKickSyncIN->ui32ClientFenceCount != 0)
	{
		psFenceUFOSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickSyncIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hFenceUFOSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hFenceUFOSyncPrimBlockInt2, psRGXKickSyncIN->phFenceUFOSyncPrimBlock, psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickSyncOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickSync_exit;
				}
			}
	if (psRGXKickSyncIN->ui32ClientFenceCount != 0)
	{
		ui32FenceSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32FenceSyncOffsetInt, psRGXKickSyncIN->pui32FenceSyncOffset, psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickSyncOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickSync_exit;
				}
			}
	if (psRGXKickSyncIN->ui32ClientFenceCount != 0)
	{
		ui32FenceValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32FenceValueInt, psRGXKickSyncIN->pui32FenceValue, psRGXKickSyncIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickSyncOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickSync_exit;
				}
			}
	if (psRGXKickSyncIN->ui32ClientUpdateCount != 0)
	{
		psUpdateUFOSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hUpdateUFOSyncPrimBlockInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hUpdateUFOSyncPrimBlockInt2, psRGXKickSyncIN->phUpdateUFOSyncPrimBlock, psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickSyncOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickSync_exit;
				}
			}
	if (psRGXKickSyncIN->ui32ClientUpdateCount != 0)
	{
		ui32UpdateSyncOffsetInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32UpdateSyncOffsetInt, psRGXKickSyncIN->pui32UpdateSyncOffset, psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickSyncOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickSync_exit;
				}
			}
	if (psRGXKickSyncIN->ui32ClientUpdateCount != 0)
	{
		ui32UpdateValueInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32UpdateValueInt, psRGXKickSyncIN->pui32UpdateValue, psRGXKickSyncIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickSyncOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickSync_exit;
				}
			}
	if (psRGXKickSyncIN->ui32ServerSyncCount != 0)
	{
		ui32ServerSyncFlagsInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickSyncIN->ui32ServerSyncCount * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXKickSyncIN->ui32ServerSyncCount * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ServerSyncFlagsInt, psRGXKickSyncIN->pui32ServerSyncFlags, psRGXKickSyncIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXKickSyncOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickSync_exit;
				}
			}
	if (psRGXKickSyncIN->ui32ServerSyncCount != 0)
	{
		psServerSyncInt = (SERVER_SYNC_PRIMITIVE **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXKickSyncIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *);
		hServerSyncInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psRGXKickSyncIN->ui32ServerSyncCount * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psRGXKickSyncIN->ui32ServerSyncCount * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hServerSyncInt2, psRGXKickSyncIN->phServerSync, psRGXKickSyncIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psRGXKickSyncOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickSync_exit;
				}
			}
	
	{
		uiUpdateFenceNameInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += 32 * sizeof(IMG_CHAR);
	}

			/* Copy the data over */
			if (32 * sizeof(IMG_CHAR) > 0)
			{
				if ( OSCopyFromUser(NULL, uiUpdateFenceNameInt, psRGXKickSyncIN->puiUpdateFenceName, 32 * sizeof(IMG_CHAR)) != PVRSRV_OK )
				{
					psRGXKickSyncOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXKickSync_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXKickSyncOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psKickSyncContextInt,
											hKickSyncContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT,
											IMG_TRUE);
					if(psRGXKickSyncOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickSync_exit;
					}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncIN->ui32ClientFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psFenceUFOSyncPrimBlockInt[i],
											hFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickSyncOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickSync_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psUpdateUFOSyncPrimBlockInt[i],
											hUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
											IMG_TRUE);
					if(psRGXKickSyncOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickSync_exit;
					}
				}
		}
	}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncIN->ui32ServerSyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psServerSyncInt[i],
											hServerSyncInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
											IMG_TRUE);
					if(psRGXKickSyncOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXKickSync_exit;
					}
				}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXKickSyncOUT->eError =
		PVRSRVRGXKickSyncKM(
					psKickSyncContextInt,
					psRGXKickSyncIN->ui32ClientCacheOpSeqNum,
					psRGXKickSyncIN->ui32ClientFenceCount,
					psFenceUFOSyncPrimBlockInt,
					ui32FenceSyncOffsetInt,
					ui32FenceValueInt,
					psRGXKickSyncIN->ui32ClientUpdateCount,
					psUpdateUFOSyncPrimBlockInt,
					ui32UpdateSyncOffsetInt,
					ui32UpdateValueInt,
					psRGXKickSyncIN->ui32ServerSyncCount,
					ui32ServerSyncFlagsInt,
					psServerSyncInt,
					psRGXKickSyncIN->i32CheckFenceFD,
					psRGXKickSyncIN->i32TimelineFenceFD,
					&psRGXKickSyncOUT->i32UpdateFenceFD,
					uiUpdateFenceNameInt,
					psRGXKickSyncIN->ui32ExtJobRef);




RGXKickSync_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psKickSyncContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hKickSyncContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT);
						}
				}





	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncIN->ui32ClientFenceCount;i++)
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

		for (i=0;i<psRGXKickSyncIN->ui32ClientUpdateCount;i++)
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

		for (i=0;i<psRGXKickSyncIN->ui32ServerSyncCount;i++)
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




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;

PVRSRV_ERROR InitRGXKICKSYNCBridge(void);
PVRSRV_ERROR DeinitRGXKICKSYNCBridge(void);

/*
 * Register all RGXKICKSYNC functions with services
 */
PVRSRV_ERROR InitRGXKICKSYNCBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC, PVRSRV_BRIDGE_RGXKICKSYNC_RGXCREATEKICKSYNCCONTEXT, PVRSRVBridgeRGXCreateKickSyncContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC, PVRSRV_BRIDGE_RGXKICKSYNC_RGXDESTROYKICKSYNCCONTEXT, PVRSRVBridgeRGXDestroyKickSyncContext,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC, PVRSRV_BRIDGE_RGXKICKSYNC_RGXKICKSYNC, PVRSRVBridgeRGXKickSync,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all rgxkicksync functions with services
 */
PVRSRV_ERROR DeinitRGXKICKSYNCBridge(void)
{
	return PVRSRV_OK;
}
