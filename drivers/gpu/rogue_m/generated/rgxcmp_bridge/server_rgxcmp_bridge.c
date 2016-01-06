/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxcmp
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxcmp
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

#include "rgxcompute.h"


#include "common_rgxcmp_bridge.h"

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
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXCreateComputeContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCREATECOMPUTECONTEXT *psRGXCreateComputeContextIN,
					  PVRSRV_BRIDGE_OUT_RGXCREATECOMPUTECONTEXT *psRGXCreateComputeContextOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	IMG_BYTE *psFrameworkCmdInt = IMG_NULL;
	IMG_HANDLE hPrivDataInt = IMG_NULL;
	RGX_SERVER_COMPUTE_CONTEXT * psComputeContextInt = IMG_NULL;




	if (psRGXCreateComputeContextIN->ui32FrameworkCmdize != 0)
	{
		psFrameworkCmdInt = OSAllocMem(psRGXCreateComputeContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE));
		if (!psFrameworkCmdInt)
		{
			psRGXCreateComputeContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXCreateComputeContext_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXCreateComputeContextIN->psFrameworkCmd, psRGXCreateComputeContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, psFrameworkCmdInt, psRGXCreateComputeContextIN->psFrameworkCmd,
				psRGXCreateComputeContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXCreateComputeContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXCreateComputeContext_exit;
			}



				{
					/* Look up the address from the handle */
					psRGXCreateComputeContextOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDevNodeInt,
											psRGXCreateComputeContextIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXCreateComputeContextOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateComputeContext_exit;
					}
				}


				{
					/* Look up the address from the handle */
					psRGXCreateComputeContextOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hPrivDataInt,
											psRGXCreateComputeContextIN->hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
					if(psRGXCreateComputeContextOUT->eError != PVRSRV_OK)
					{
						goto RGXCreateComputeContext_exit;
					}
				}


	psRGXCreateComputeContextOUT->eError =
		PVRSRVRGXCreateComputeContextKM(psConnection,
					hDevNodeInt,
					psRGXCreateComputeContextIN->ui32Priority,
					psRGXCreateComputeContextIN->sMCUFenceAddr,
					psRGXCreateComputeContextIN->ui32FrameworkCmdize,
					psFrameworkCmdInt,
					hPrivDataInt,
					&psComputeContextInt);
	/* Exit early if bridged call fails */
	if(psRGXCreateComputeContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateComputeContext_exit;
	}


	psRGXCreateComputeContextOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psRGXCreateComputeContextOUT->hComputeContext,
							(IMG_VOID *) psComputeContextInt,
							PVRSRV_HANDLE_TYPE_RGX_SERVER_COMPUTE_CONTEXT,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&PVRSRVRGXDestroyComputeContextKM);
	if (psRGXCreateComputeContextOUT->eError != PVRSRV_OK)
	{
		goto RGXCreateComputeContext_exit;
	}




RGXCreateComputeContext_exit:
	if (psRGXCreateComputeContextOUT->eError != PVRSRV_OK)
	{
		if (psComputeContextInt)
		{
			PVRSRVRGXDestroyComputeContextKM(psComputeContextInt);
		}
	}

	if (psFrameworkCmdInt)
		OSFreeMem(psFrameworkCmdInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyComputeContext(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDESTROYCOMPUTECONTEXT *psRGXDestroyComputeContextIN,
					  PVRSRV_BRIDGE_OUT_RGXDESTROYCOMPUTECONTEXT *psRGXDestroyComputeContextOUT,
					 CONNECTION_DATA *psConnection)
{









	psRGXDestroyComputeContextOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psRGXDestroyComputeContextIN->hComputeContext,
					PVRSRV_HANDLE_TYPE_RGX_SERVER_COMPUTE_CONTEXT);
	if ((psRGXDestroyComputeContextOUT->eError != PVRSRV_OK) && (psRGXDestroyComputeContextOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto RGXDestroyComputeContext_exit;
	}



RGXDestroyComputeContext_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXKickCDM(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXKICKCDM *psRGXKickCDMIN,
					  PVRSRV_BRIDGE_OUT_RGXKICKCDM *psRGXKickCDMOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_COMPUTE_CONTEXT * psComputeContextInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientFenceUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClientFenceUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientFenceOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32ClientFenceValueInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientUpdateUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClientUpdateUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientUpdateOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32ClientUpdateValueInt = IMG_NULL;
	IMG_UINT32 *ui32ServerSyncFlagsInt = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * *psServerSyncsInt = IMG_NULL;
	IMG_HANDLE *hServerSyncsInt2 = IMG_NULL;
	IMG_BYTE *psDMCmdInt = IMG_NULL;




	if (psRGXKickCDMIN->ui32ClientFenceCount != 0)
	{
		psClientFenceUFOSyncPrimBlockInt = OSAllocMem(psRGXKickCDMIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientFenceUFOSyncPrimBlockInt)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
		hClientFenceUFOSyncPrimBlockInt2 = OSAllocMem(psRGXKickCDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE));
		if (!hClientFenceUFOSyncPrimBlockInt2)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickCDMIN->phClientFenceUFOSyncPrimBlock, psRGXKickCDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientFenceUFOSyncPrimBlockInt2, psRGXKickCDMIN->phClientFenceUFOSyncPrimBlock,
				psRGXKickCDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickCDM_exit;
			}
	if (psRGXKickCDMIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceOffsetInt = OSAllocMem(psRGXKickCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceOffsetInt)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickCDMIN->pui32ClientFenceOffset, psRGXKickCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceOffsetInt, psRGXKickCDMIN->pui32ClientFenceOffset,
				psRGXKickCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickCDM_exit;
			}
	if (psRGXKickCDMIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceValueInt = OSAllocMem(psRGXKickCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceValueInt)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickCDMIN->pui32ClientFenceValue, psRGXKickCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceValueInt, psRGXKickCDMIN->pui32ClientFenceValue,
				psRGXKickCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickCDM_exit;
			}
	if (psRGXKickCDMIN->ui32ClientUpdateCount != 0)
	{
		psClientUpdateUFOSyncPrimBlockInt = OSAllocMem(psRGXKickCDMIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientUpdateUFOSyncPrimBlockInt)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
		hClientUpdateUFOSyncPrimBlockInt2 = OSAllocMem(psRGXKickCDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE));
		if (!hClientUpdateUFOSyncPrimBlockInt2)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickCDMIN->phClientUpdateUFOSyncPrimBlock, psRGXKickCDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientUpdateUFOSyncPrimBlockInt2, psRGXKickCDMIN->phClientUpdateUFOSyncPrimBlock,
				psRGXKickCDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickCDM_exit;
			}
	if (psRGXKickCDMIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateOffsetInt = OSAllocMem(psRGXKickCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateOffsetInt)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickCDMIN->pui32ClientUpdateOffset, psRGXKickCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateOffsetInt, psRGXKickCDMIN->pui32ClientUpdateOffset,
				psRGXKickCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickCDM_exit;
			}
	if (psRGXKickCDMIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateValueInt = OSAllocMem(psRGXKickCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateValueInt)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickCDMIN->pui32ClientUpdateValue, psRGXKickCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateValueInt, psRGXKickCDMIN->pui32ClientUpdateValue,
				psRGXKickCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickCDM_exit;
			}
	if (psRGXKickCDMIN->ui32ServerSyncCount != 0)
	{
		ui32ServerSyncFlagsInt = OSAllocMem(psRGXKickCDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32));
		if (!ui32ServerSyncFlagsInt)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickCDMIN->pui32ServerSyncFlags, psRGXKickCDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ServerSyncFlagsInt, psRGXKickCDMIN->pui32ServerSyncFlags,
				psRGXKickCDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickCDM_exit;
			}
	if (psRGXKickCDMIN->ui32ServerSyncCount != 0)
	{
		psServerSyncsInt = OSAllocMem(psRGXKickCDMIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psServerSyncsInt)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
		hServerSyncsInt2 = OSAllocMem(psRGXKickCDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE));
		if (!hServerSyncsInt2)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickCDMIN->phServerSyncs, psRGXKickCDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hServerSyncsInt2, psRGXKickCDMIN->phServerSyncs,
				psRGXKickCDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickCDM_exit;
			}
	if (psRGXKickCDMIN->ui32CmdSize != 0)
	{
		psDMCmdInt = OSAllocMem(psRGXKickCDMIN->ui32CmdSize * sizeof(IMG_BYTE));
		if (!psDMCmdInt)
		{
			psRGXKickCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickCDMIN->psDMCmd, psRGXKickCDMIN->ui32CmdSize * sizeof(IMG_BYTE))
				|| (OSCopyFromUser(NULL, psDMCmdInt, psRGXKickCDMIN->psDMCmd,
				psRGXKickCDMIN->ui32CmdSize * sizeof(IMG_BYTE)) != PVRSRV_OK) )
			{
				psRGXKickCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickCDM_exit;
			}



				{
					/* Look up the address from the handle */
					psRGXKickCDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psComputeContextInt,
											psRGXKickCDMIN->hComputeContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_COMPUTE_CONTEXT);
					if(psRGXKickCDMOUT->eError != PVRSRV_OK)
					{
						goto RGXKickCDM_exit;
					}
				}


	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickCDMIN->ui32ClientFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickCDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClientFenceUFOSyncPrimBlockInt[i],
											hClientFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickCDMOUT->eError != PVRSRV_OK)
					{
						goto RGXKickCDM_exit;
					}
				}

		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickCDMIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickCDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClientUpdateUFOSyncPrimBlockInt[i],
											hClientUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickCDMOUT->eError != PVRSRV_OK)
					{
						goto RGXKickCDM_exit;
					}
				}

		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickCDMIN->ui32ServerSyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickCDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psServerSyncsInt[i],
											hServerSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psRGXKickCDMOUT->eError != PVRSRV_OK)
					{
						goto RGXKickCDM_exit;
					}
				}

		}
	}

	psRGXKickCDMOUT->eError =
		PVRSRVRGXKickCDMKM(
					psComputeContextInt,
					psRGXKickCDMIN->ui32ClientFenceCount,
					psClientFenceUFOSyncPrimBlockInt,
					ui32ClientFenceOffsetInt,
					ui32ClientFenceValueInt,
					psRGXKickCDMIN->ui32ClientUpdateCount,
					psClientUpdateUFOSyncPrimBlockInt,
					ui32ClientUpdateOffsetInt,
					ui32ClientUpdateValueInt,
					psRGXKickCDMIN->ui32ServerSyncCount,
					ui32ServerSyncFlagsInt,
					psServerSyncsInt,
					psRGXKickCDMIN->ui32CmdSize,
					psDMCmdInt,
					psRGXKickCDMIN->bbPDumpContinuous,
					psRGXKickCDMIN->ui32ExternalJobReference,
					psRGXKickCDMIN->ui32InternalJobReference);




RGXKickCDM_exit:
	if (psClientFenceUFOSyncPrimBlockInt)
		OSFreeMem(psClientFenceUFOSyncPrimBlockInt);
	if (hClientFenceUFOSyncPrimBlockInt2)
		OSFreeMem(hClientFenceUFOSyncPrimBlockInt2);
	if (ui32ClientFenceOffsetInt)
		OSFreeMem(ui32ClientFenceOffsetInt);
	if (ui32ClientFenceValueInt)
		OSFreeMem(ui32ClientFenceValueInt);
	if (psClientUpdateUFOSyncPrimBlockInt)
		OSFreeMem(psClientUpdateUFOSyncPrimBlockInt);
	if (hClientUpdateUFOSyncPrimBlockInt2)
		OSFreeMem(hClientUpdateUFOSyncPrimBlockInt2);
	if (ui32ClientUpdateOffsetInt)
		OSFreeMem(ui32ClientUpdateOffsetInt);
	if (ui32ClientUpdateValueInt)
		OSFreeMem(ui32ClientUpdateValueInt);
	if (ui32ServerSyncFlagsInt)
		OSFreeMem(ui32ServerSyncFlagsInt);
	if (psServerSyncsInt)
		OSFreeMem(psServerSyncsInt);
	if (hServerSyncsInt2)
		OSFreeMem(hServerSyncsInt2);
	if (psDMCmdInt)
		OSFreeMem(psDMCmdInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXFlushComputeData(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXFLUSHCOMPUTEDATA *psRGXFlushComputeDataIN,
					  PVRSRV_BRIDGE_OUT_RGXFLUSHCOMPUTEDATA *psRGXFlushComputeDataOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_COMPUTE_CONTEXT * psComputeContextInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXFlushComputeDataOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psComputeContextInt,
											psRGXFlushComputeDataIN->hComputeContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_COMPUTE_CONTEXT);
					if(psRGXFlushComputeDataOUT->eError != PVRSRV_OK)
					{
						goto RGXFlushComputeData_exit;
					}
				}


	psRGXFlushComputeDataOUT->eError =
		PVRSRVRGXFlushComputeDataKM(
					psComputeContextInt);




RGXFlushComputeData_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXSetComputeContextPriority(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXSETCOMPUTECONTEXTPRIORITY *psRGXSetComputeContextPriorityIN,
					  PVRSRV_BRIDGE_OUT_RGXSETCOMPUTECONTEXTPRIORITY *psRGXSetComputeContextPriorityOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_COMPUTE_CONTEXT * psComputeContextInt = IMG_NULL;







				{
					/* Look up the address from the handle */
					psRGXSetComputeContextPriorityOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psComputeContextInt,
											psRGXSetComputeContextPriorityIN->hComputeContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_COMPUTE_CONTEXT);
					if(psRGXSetComputeContextPriorityOUT->eError != PVRSRV_OK)
					{
						goto RGXSetComputeContextPriority_exit;
					}
				}


	psRGXSetComputeContextPriorityOUT->eError =
		PVRSRVRGXSetComputeContextPriorityKM(psConnection,
					psComputeContextInt,
					psRGXSetComputeContextPriorityIN->ui32Priority);




RGXSetComputeContextPriority_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXKickSyncCDM(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXKICKSYNCCDM *psRGXKickSyncCDMIN,
					  PVRSRV_BRIDGE_OUT_RGXKICKSYNCCDM *psRGXKickSyncCDMOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_SERVER_COMPUTE_CONTEXT * psComputeContextInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientFenceUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClientFenceUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientFenceOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32ClientFenceValueInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * *psClientUpdateUFOSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE *hClientUpdateUFOSyncPrimBlockInt2 = IMG_NULL;
	IMG_UINT32 *ui32ClientUpdateOffsetInt = IMG_NULL;
	IMG_UINT32 *ui32ClientUpdateValueInt = IMG_NULL;
	IMG_UINT32 *ui32ServerSyncFlagsInt = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * *psServerSyncsInt = IMG_NULL;
	IMG_HANDLE *hServerSyncsInt2 = IMG_NULL;
	IMG_INT32 *i32CheckFenceFDsInt = IMG_NULL;




	if (psRGXKickSyncCDMIN->ui32ClientFenceCount != 0)
	{
		psClientFenceUFOSyncPrimBlockInt = OSAllocMem(psRGXKickSyncCDMIN->ui32ClientFenceCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientFenceUFOSyncPrimBlockInt)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
		hClientFenceUFOSyncPrimBlockInt2 = OSAllocMem(psRGXKickSyncCDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE));
		if (!hClientFenceUFOSyncPrimBlockInt2)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncCDMIN->phClientFenceUFOSyncPrimBlock, psRGXKickSyncCDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientFenceUFOSyncPrimBlockInt2, psRGXKickSyncCDMIN->phClientFenceUFOSyncPrimBlock,
				psRGXKickSyncCDMIN->ui32ClientFenceCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncCDM_exit;
			}
	if (psRGXKickSyncCDMIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceOffsetInt = OSAllocMem(psRGXKickSyncCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceOffsetInt)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncCDMIN->pui32ClientFenceOffset, psRGXKickSyncCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceOffsetInt, psRGXKickSyncCDMIN->pui32ClientFenceOffset,
				psRGXKickSyncCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncCDM_exit;
			}
	if (psRGXKickSyncCDMIN->ui32ClientFenceCount != 0)
	{
		ui32ClientFenceValueInt = OSAllocMem(psRGXKickSyncCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32));
		if (!ui32ClientFenceValueInt)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncCDMIN->pui32ClientFenceValue, psRGXKickSyncCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientFenceValueInt, psRGXKickSyncCDMIN->pui32ClientFenceValue,
				psRGXKickSyncCDMIN->ui32ClientFenceCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncCDM_exit;
			}
	if (psRGXKickSyncCDMIN->ui32ClientUpdateCount != 0)
	{
		psClientUpdateUFOSyncPrimBlockInt = OSAllocMem(psRGXKickSyncCDMIN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psClientUpdateUFOSyncPrimBlockInt)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
		hClientUpdateUFOSyncPrimBlockInt2 = OSAllocMem(psRGXKickSyncCDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE));
		if (!hClientUpdateUFOSyncPrimBlockInt2)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncCDMIN->phClientUpdateUFOSyncPrimBlock, psRGXKickSyncCDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hClientUpdateUFOSyncPrimBlockInt2, psRGXKickSyncCDMIN->phClientUpdateUFOSyncPrimBlock,
				psRGXKickSyncCDMIN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncCDM_exit;
			}
	if (psRGXKickSyncCDMIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateOffsetInt = OSAllocMem(psRGXKickSyncCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateOffsetInt)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncCDMIN->pui32ClientUpdateOffset, psRGXKickSyncCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateOffsetInt, psRGXKickSyncCDMIN->pui32ClientUpdateOffset,
				psRGXKickSyncCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncCDM_exit;
			}
	if (psRGXKickSyncCDMIN->ui32ClientUpdateCount != 0)
	{
		ui32ClientUpdateValueInt = OSAllocMem(psRGXKickSyncCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32));
		if (!ui32ClientUpdateValueInt)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncCDMIN->pui32ClientUpdateValue, psRGXKickSyncCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ClientUpdateValueInt, psRGXKickSyncCDMIN->pui32ClientUpdateValue,
				psRGXKickSyncCDMIN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncCDM_exit;
			}
	if (psRGXKickSyncCDMIN->ui32ServerSyncCount != 0)
	{
		ui32ServerSyncFlagsInt = OSAllocMem(psRGXKickSyncCDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32));
		if (!ui32ServerSyncFlagsInt)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncCDMIN->pui32ServerSyncFlags, psRGXKickSyncCDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ServerSyncFlagsInt, psRGXKickSyncCDMIN->pui32ServerSyncFlags,
				psRGXKickSyncCDMIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncCDM_exit;
			}
	if (psRGXKickSyncCDMIN->ui32ServerSyncCount != 0)
	{
		psServerSyncsInt = OSAllocMem(psRGXKickSyncCDMIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psServerSyncsInt)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
		hServerSyncsInt2 = OSAllocMem(psRGXKickSyncCDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE));
		if (!hServerSyncsInt2)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncCDMIN->phServerSyncs, psRGXKickSyncCDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hServerSyncsInt2, psRGXKickSyncCDMIN->phServerSyncs,
				psRGXKickSyncCDMIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncCDM_exit;
			}
	if (psRGXKickSyncCDMIN->ui32NumCheckFenceFDs != 0)
	{
		i32CheckFenceFDsInt = OSAllocMem(psRGXKickSyncCDMIN->ui32NumCheckFenceFDs * sizeof(IMG_INT32));
		if (!i32CheckFenceFDsInt)
		{
			psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto RGXKickSyncCDM_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psRGXKickSyncCDMIN->pi32CheckFenceFDs, psRGXKickSyncCDMIN->ui32NumCheckFenceFDs * sizeof(IMG_INT32))
				|| (OSCopyFromUser(NULL, i32CheckFenceFDsInt, psRGXKickSyncCDMIN->pi32CheckFenceFDs,
				psRGXKickSyncCDMIN->ui32NumCheckFenceFDs * sizeof(IMG_INT32)) != PVRSRV_OK) )
			{
				psRGXKickSyncCDMOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXKickSyncCDM_exit;
			}



				{
					/* Look up the address from the handle */
					psRGXKickSyncCDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psComputeContextInt,
											psRGXKickSyncCDMIN->hComputeContext,
											PVRSRV_HANDLE_TYPE_RGX_SERVER_COMPUTE_CONTEXT);
					if(psRGXKickSyncCDMOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncCDM_exit;
					}
				}


	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncCDMIN->ui32ClientFenceCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncCDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClientFenceUFOSyncPrimBlockInt[i],
											hClientFenceUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickSyncCDMOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncCDM_exit;
					}
				}

		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncCDMIN->ui32ClientUpdateCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncCDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psClientUpdateUFOSyncPrimBlockInt[i],
											hClientUpdateUFOSyncPrimBlockInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psRGXKickSyncCDMOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncCDM_exit;
					}
				}

		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psRGXKickSyncCDMIN->ui32ServerSyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psRGXKickSyncCDMOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psServerSyncsInt[i],
											hServerSyncsInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psRGXKickSyncCDMOUT->eError != PVRSRV_OK)
					{
						goto RGXKickSyncCDM_exit;
					}
				}

		}
	}

	psRGXKickSyncCDMOUT->eError =
		PVRSRVRGXKickSyncCDMKM(
					psComputeContextInt,
					psRGXKickSyncCDMIN->ui32ClientFenceCount,
					psClientFenceUFOSyncPrimBlockInt,
					ui32ClientFenceOffsetInt,
					ui32ClientFenceValueInt,
					psRGXKickSyncCDMIN->ui32ClientUpdateCount,
					psClientUpdateUFOSyncPrimBlockInt,
					ui32ClientUpdateOffsetInt,
					ui32ClientUpdateValueInt,
					psRGXKickSyncCDMIN->ui32ServerSyncCount,
					ui32ServerSyncFlagsInt,
					psServerSyncsInt,
					psRGXKickSyncCDMIN->ui32NumCheckFenceFDs,
					i32CheckFenceFDsInt,
					psRGXKickSyncCDMIN->i32UpdateFenceFD,
					psRGXKickSyncCDMIN->bbPDumpContinuous);




RGXKickSyncCDM_exit:
	if (psClientFenceUFOSyncPrimBlockInt)
		OSFreeMem(psClientFenceUFOSyncPrimBlockInt);
	if (hClientFenceUFOSyncPrimBlockInt2)
		OSFreeMem(hClientFenceUFOSyncPrimBlockInt2);
	if (ui32ClientFenceOffsetInt)
		OSFreeMem(ui32ClientFenceOffsetInt);
	if (ui32ClientFenceValueInt)
		OSFreeMem(ui32ClientFenceValueInt);
	if (psClientUpdateUFOSyncPrimBlockInt)
		OSFreeMem(psClientUpdateUFOSyncPrimBlockInt);
	if (hClientUpdateUFOSyncPrimBlockInt2)
		OSFreeMem(hClientUpdateUFOSyncPrimBlockInt2);
	if (ui32ClientUpdateOffsetInt)
		OSFreeMem(ui32ClientUpdateOffsetInt);
	if (ui32ClientUpdateValueInt)
		OSFreeMem(ui32ClientUpdateValueInt);
	if (ui32ServerSyncFlagsInt)
		OSFreeMem(ui32ServerSyncFlagsInt);
	if (psServerSyncsInt)
		OSFreeMem(psServerSyncsInt);
	if (hServerSyncsInt2)
		OSFreeMem(hServerSyncsInt2);
	if (i32CheckFenceFDsInt)
		OSFreeMem(i32CheckFenceFDsInt);

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */


PVRSRV_ERROR InitRGXCMPBridge(IMG_VOID);
PVRSRV_ERROR DeinitRGXCMPBridge(IMG_VOID);

/*
 * Register all RGXCMP functions with services
 */
PVRSRV_ERROR InitRGXCMPBridge(IMG_VOID)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXCMP, PVRSRV_BRIDGE_RGXCMP_RGXCREATECOMPUTECONTEXT, PVRSRVBridgeRGXCreateComputeContext,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXCMP, PVRSRV_BRIDGE_RGXCMP_RGXDESTROYCOMPUTECONTEXT, PVRSRVBridgeRGXDestroyComputeContext,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXCMP, PVRSRV_BRIDGE_RGXCMP_RGXKICKCDM, PVRSRVBridgeRGXKickCDM,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXCMP, PVRSRV_BRIDGE_RGXCMP_RGXFLUSHCOMPUTEDATA, PVRSRVBridgeRGXFlushComputeData,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXCMP, PVRSRV_BRIDGE_RGXCMP_RGXSETCOMPUTECONTEXTPRIORITY, PVRSRVBridgeRGXSetComputeContextPriority,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXCMP, PVRSRV_BRIDGE_RGXCMP_RGXKICKSYNCCDM, PVRSRVBridgeRGXKickSyncCDM,
					IMG_NULL, IMG_NULL,
					0, 0);


	return PVRSRV_OK;
}

/*
 * Unregister all rgxcmp functions with services
 */
PVRSRV_ERROR DeinitRGXCMPBridge(IMG_VOID)
{
	return PVRSRV_OK;
}

