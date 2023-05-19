/*******************************************************************************
@File
@Title          Server bridge for htbuffer
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for htbuffer
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

#include <linux/uaccess.h>

#include "img_defs.h"

#include "htbserver.h"

#include "common_htbuffer_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#if defined(SUPPORT_RGX)
#include "rgx_bridge.h"
#endif
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>

#include "lock.h"

#if !defined(EXCLUDE_HTBUFFER_BRIDGE)

/* ***************************************************************************
 * Server-side bridge entry points
 */

static_assert(HTB_FLAG_NUM_EL <= IMG_UINT32_MAX,
	      "HTB_FLAG_NUM_EL must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeHTBControl(IMG_UINT32 ui32DispatchTableEntry,
		       IMG_UINT8 * psHTBControlIN_UI8,
		       IMG_UINT8 * psHTBControlOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_HTBCONTROL *psHTBControlIN =
	    (PVRSRV_BRIDGE_IN_HTBCONTROL *) IMG_OFFSET_ADDR(psHTBControlIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_HTBCONTROL *psHTBControlOUT =
	    (PVRSRV_BRIDGE_OUT_HTBCONTROL *) IMG_OFFSET_ADDR(psHTBControlOUT_UI8, 0);

	IMG_UINT32 *ui32GroupEnableInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psHTBControlIN->ui32NumGroups * sizeof(IMG_UINT32)) + 0;

	if (unlikely(psHTBControlIN->ui32NumGroups > HTB_FLAG_NUM_EL))
	{
		psHTBControlOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto HTBControl_exit;
	}

	PVR_UNREFERENCED_PARAMETER(psConnection);

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psHTBControlOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto HTBControl_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psHTBControlIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psHTBControlIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psHTBControlOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto HTBControl_exit;
			}
		}
	}

	if (psHTBControlIN->ui32NumGroups != 0)
	{
		ui32GroupEnableInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psHTBControlIN->ui32NumGroups * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psHTBControlIN->ui32NumGroups * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32GroupEnableInt,
		     (const void __user *)psHTBControlIN->pui32GroupEnable,
		     psHTBControlIN->ui32NumGroups * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psHTBControlOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto HTBControl_exit;
		}
	}

	psHTBControlOUT->eError =
	    HTBControlKM(psHTBControlIN->ui32NumGroups,
			 ui32GroupEnableInt,
			 psHTBControlIN->ui32LogLevel,
			 psHTBControlIN->ui32EnablePID,
			 psHTBControlIN->ui32LogMode, psHTBControlIN->ui32OpMode);

HTBControl_exit:

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psHTBControlOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

static POS_LOCK pHTBUFFERBridgeLock;

#endif /* EXCLUDE_HTBUFFER_BRIDGE */

#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
PVRSRV_ERROR InitHTBUFFERBridge(void);
void DeinitHTBUFFERBridge(void);

/*
 * Register all HTBUFFER functions with services
 */
PVRSRV_ERROR InitHTBUFFERBridge(void)
{
	PVR_LOG_RETURN_IF_ERROR(OSLockCreate(&pHTBUFFERBridgeLock), "OSLockCreate");

	SetDispatchTableEntry(PVRSRV_BRIDGE_HTBUFFER, PVRSRV_BRIDGE_HTBUFFER_HTBCONTROL,
			      PVRSRVBridgeHTBControl, pHTBUFFERBridgeLock);

	return PVRSRV_OK;
}

/*
 * Unregister all htbuffer functions with services
 */
void DeinitHTBUFFERBridge(void)
{
	OSLockDestroy(pHTBUFFERBridgeLock);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_HTBUFFER, PVRSRV_BRIDGE_HTBUFFER_HTBCONTROL);

}
#else /* EXCLUDE_HTBUFFER_BRIDGE */
/* This bridge is conditional on EXCLUDE_HTBUFFER_BRIDGE - when defined,
 * do not populate the dispatch table with its functions
 */
#define InitHTBUFFERBridge() \
	PVRSRV_OK

#define DeinitHTBUFFERBridge()

#endif /* EXCLUDE_HTBUFFER_BRIDGE */
