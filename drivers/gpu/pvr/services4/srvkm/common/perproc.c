/*************************************************************************/ /*!
@Title          Per-process storage
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Manage per-process storage
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

#include "services_headers.h"
#include "resman.h"
#include "handle.h"
#include "perproc.h"
#include "osperproc.h"
#if defined(TTRACE)
#include "ttrace.h"
#endif

#define	HASH_TAB_INIT_SIZE 32

static HASH_TABLE *psHashTab = IMG_NULL;

/*!
******************************************************************************

 @Function	FreePerProcData

 @Description	Free a per-process data area

 @Input		psPerProc - pointer to per-process data area

 @Return	Error code, or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR FreePerProcessData(PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_ERROR eError;
	IMG_UINTPTR_T uiPerProc;

	PVR_ASSERT(psPerProc != IMG_NULL);

	if (psPerProc == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreePerProcessData: invalid parameter"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	uiPerProc = HASH_Remove(psHashTab, (IMG_UINTPTR_T)psPerProc->ui32PID);
	if (uiPerProc == 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreePerProcessData: Couldn't find process in per-process data hash table"));
		/*
		 * We must have failed early in the per-process data area
		 * creation, before the process ID was set.
		 */
		PVR_ASSERT(psPerProc->ui32PID == 0);
	}
	else
	{
		PVR_ASSERT((PVRSRV_PER_PROCESS_DATA *)uiPerProc == psPerProc);
		PVR_ASSERT(((PVRSRV_PER_PROCESS_DATA *)uiPerProc)->ui32PID == psPerProc->ui32PID);
	}

	/* Free handle base for this process */
	if (psPerProc->psHandleBase != IMG_NULL)
	{
		eError = PVRSRVFreeHandleBase(psPerProc->psHandleBase);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "FreePerProcessData: Couldn't free handle base for process (%d)", eError));
			return eError;
		}
	}

	/* Release handle for per-process data area */
	if (psPerProc->hPerProcData != IMG_NULL)
	{
		eError = PVRSRVReleaseHandle(KERNEL_HANDLE_BASE, psPerProc->hPerProcData, PVRSRV_HANDLE_TYPE_PERPROC_DATA);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "FreePerProcessData: Couldn't release per-process data handle (%d)", eError));
			return eError;
		}
	}

	/* Call environment specific per process deinit function */
	eError = OSPerProcessPrivateDataDeInit(psPerProc->hOsPrivateData);
	if (eError != PVRSRV_OK)
	{
		 PVR_DPF((PVR_DBG_ERROR, "FreePerProcessData: OSPerProcessPrivateDataDeInit failed (%d)", eError));
		return eError;
	}

	eError = OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
		sizeof(*psPerProc),
		psPerProc,
		psPerProc->hBlockAlloc);
	/*not nulling pointer, copy on stack*/
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreePerProcessData: Couldn't free per-process data (%d)", eError));
		return eError;
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	PVRSRVPerProcessData
 
 @Description	Return per-process data area

 @Input		ui32PID - process ID
 
 @Return	Pointer to per-process data area, or IMG_NULL on error.

******************************************************************************/
PVRSRV_PER_PROCESS_DATA *PVRSRVPerProcessData(IMG_UINT32 ui32PID)
{
	PVRSRV_PER_PROCESS_DATA *psPerProc;

	PVR_ASSERT(psHashTab != IMG_NULL);

	/* Look for existing per-process data area */
	psPerProc = (PVRSRV_PER_PROCESS_DATA *)HASH_Retrieve(psHashTab, (IMG_UINTPTR_T)ui32PID);
	return psPerProc;
}


/*!
******************************************************************************

 @Function	PVRSRVPerProcessDataConnect
 
 @Description	Allocate per-process data area, or increment refcount if one
 				already exists for this PID.

 @Input		ui32PID - process ID
 			ppsPerProc - Pointer to per-process data area
 
 @Return	PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVPerProcessDataConnect(IMG_UINT32	ui32PID, IMG_UINT32 ui32Flags)
{
	PVRSRV_PER_PROCESS_DATA *psPerProc;
	IMG_HANDLE hBlockAlloc;
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (psHashTab == IMG_NULL)
	{
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	/* Look for existing per-process data area */
	psPerProc = (PVRSRV_PER_PROCESS_DATA *)HASH_Retrieve(psHashTab, (IMG_UINTPTR_T)ui32PID);

	if (psPerProc == IMG_NULL)
	{
		/* Allocate per-process data area */
		eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
							sizeof(*psPerProc),
							(IMG_PVOID *)&psPerProc,
							&hBlockAlloc,
							"Per Process Data");
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataConnect: Couldn't allocate per-process data (%d)", eError));
			return eError;
		}
		OSMemSet(psPerProc, 0, sizeof(*psPerProc));
		psPerProc->hBlockAlloc = hBlockAlloc;

		if (!HASH_Insert(psHashTab, (IMG_UINTPTR_T)ui32PID, (IMG_UINTPTR_T)psPerProc))
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataConnect: Couldn't insert per-process data into hash table"));
			eError = PVRSRV_ERROR_INSERT_HASH_TABLE_DATA_FAILED;
			goto failure;
		}

		psPerProc->ui32PID = ui32PID;
		psPerProc->ui32RefCount = 0;

#if defined(SUPPORT_PDUMP_MULTI_PROCESS)
		if (ui32Flags == SRV_FLAGS_PDUMP_ACTIVE)
		{
			psPerProc->bPDumpActive = IMG_TRUE;
		}
#else
		PVR_UNREFERENCED_PARAMETER(ui32Flags);
#endif

		/* Call environment specific per process init function */
		eError = OSPerProcessPrivateDataInit(&psPerProc->hOsPrivateData);
		if (eError != PVRSRV_OK)
		{
			 PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataConnect: OSPerProcessPrivateDataInit failed (%d)", eError));
			goto failure;
		}

		/* Allocate a handle for the per-process data area */
		eError = PVRSRVAllocHandle(KERNEL_HANDLE_BASE,
								   &psPerProc->hPerProcData,
								   psPerProc,
								   PVRSRV_HANDLE_TYPE_PERPROC_DATA,
								   PVRSRV_HANDLE_ALLOC_FLAG_NONE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataConnect: Couldn't allocate handle for per-process data (%d)", eError));
			goto failure;
		}

		/* Allocate handle base for this process */
		eError = PVRSRVAllocHandleBase(&psPerProc->psHandleBase);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataConnect: Couldn't allocate handle base for process (%d)", eError));
			goto failure;
		}

		/* Set per-process handle options */
		eError = OSPerProcessSetHandleOptions(psPerProc->psHandleBase);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataConnect: Couldn't set handle options (%d)", eError));
			goto failure;
		}
		
		/* Create a resource manager context for the process */
		eError = PVRSRVResManConnect(psPerProc, &psPerProc->hResManContext);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataConnect: Couldn't register with the resource manager"));
			goto failure;
		}
#if defined (TTRACE)
		PVRSRVTimeTraceBufferCreate(ui32PID);
#endif
	}
	
	psPerProc->ui32RefCount++;
	PVR_DPF((PVR_DBG_MESSAGE,
			"PVRSRVPerProcessDataConnect: Process 0x%x has ref-count %d",
			ui32PID, psPerProc->ui32RefCount));

	return eError;

failure:
	(IMG_VOID)FreePerProcessData(psPerProc);
	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVPerProcessDataDisconnect
 
 @Description	Decrement refcount for per-process data area, 
 				and free the resources if necessary.

 @Input		ui32PID - process ID
 
 @Return	IMG_VOID

******************************************************************************/
IMG_VOID PVRSRVPerProcessDataDisconnect(IMG_UINT32	ui32PID)
{
	PVRSRV_ERROR eError;
	PVRSRV_PER_PROCESS_DATA *psPerProc;

	PVR_ASSERT(psHashTab != IMG_NULL);

	psPerProc = (PVRSRV_PER_PROCESS_DATA *)HASH_Retrieve(psHashTab, (IMG_UINTPTR_T)ui32PID);
	if (psPerProc == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataDealloc: Couldn't locate per-process data for PID %u", ui32PID));
	}
	else
	{
		psPerProc->ui32RefCount--;
		if (psPerProc->ui32RefCount == 0)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVPerProcessDataDisconnect: "
					"Last close from process 0x%x received", ui32PID));

			/* Close the Resource Manager connection */
			PVRSRVResManDisconnect(psPerProc->hResManContext, IMG_FALSE);

#if defined (TTRACE)
			PVRSRVTimeTraceBufferDestroy(ui32PID);
#endif

			/* Free the per-process data */
			eError = FreePerProcessData(psPerProc);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataDisconnect: Error freeing per-process data"));
			}
		}
	}

	eError = PVRSRVPurgeHandles(KERNEL_HANDLE_BASE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataDisconnect: Purge of global handle pool failed (%d)", eError));
	}
}


/*!
******************************************************************************

 @Function	PVRSRVPerProcessDataInit

 @Description	Initialise per-process data management

 @Return	Error code, or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVPerProcessDataInit(IMG_VOID)
{
	PVR_ASSERT(psHashTab == IMG_NULL);

	/* Create hash table */
	psHashTab = HASH_Create(HASH_TAB_INIT_SIZE);
	if (psHashTab == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVPerProcessDataInit: Couldn't create per-process data hash table"));
		return PVRSRV_ERROR_UNABLE_TO_CREATE_HASH_TABLE;
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVPerProcessDataDeInit

 @Description	De-initialise per-process data management

 @Return	Error code, or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVPerProcessDataDeInit(IMG_VOID)
{
	/* Destroy per-process data area hash table */
	if (psHashTab != IMG_NULL)
	{
		/* Free the hash table */
		HASH_Delete(psHashTab);
		psHashTab = IMG_NULL;
	}

	return PVRSRV_OK;
}

/******************************************************************************
 End of file (perproc.c)
******************************************************************************/
