/*************************************************************************/ /*!
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
*/ /**************************************************************************/

#include <stddef.h>
#include <linux/uaccess.h>

#include "osfunc.h"
#include "img_defs.h"

#include "htbserver.h"


#include "common_htbuffer_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>

#include "lock.h"


#if !defined(EXCLUDE_HTBUFFER_BRIDGE)



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeHTBConfigure(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_HTBCONFIGURE *psHTBConfigureIN,
					  PVRSRV_BRIDGE_OUT_HTBCONFIGURE *psHTBConfigureOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_CHAR *uiNameInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psHTBConfigureIN->ui32NameSize * sizeof(IMG_CHAR)) +
			0;


	PVR_UNREFERENCED_PARAMETER(psConnection);



	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psHTBConfigureIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psHTBConfigureIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psHTBConfigureOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto HTBConfigure_exit;
			}
		}
	}

	if (psHTBConfigureIN->ui32NameSize != 0)
	{
		uiNameInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psHTBConfigureIN->ui32NameSize * sizeof(IMG_CHAR);
	}

			/* Copy the data over */
			if (psHTBConfigureIN->ui32NameSize * sizeof(IMG_CHAR) > 0)
			{
				if ( OSCopyFromUser(NULL, uiNameInt, psHTBConfigureIN->puiName, psHTBConfigureIN->ui32NameSize * sizeof(IMG_CHAR)) != PVRSRV_OK )
				{
					psHTBConfigureOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto HTBConfigure_exit;
				}
			}


	psHTBConfigureOUT->eError =
		HTBConfigureKM(
					psHTBConfigureIN->ui32NameSize,
					uiNameInt,
					psHTBConfigureIN->ui32BufferSize);




HTBConfigure_exit:



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
PVRSRVBridgeHTBControl(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_HTBCONTROL *psHTBControlIN,
					  PVRSRV_BRIDGE_OUT_HTBCONTROL *psHTBControlOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_UINT32 *ui32GroupEnableInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psHTBControlIN->ui32NumGroups * sizeof(IMG_UINT32)) +
			0;


	PVR_UNREFERENCED_PARAMETER(psConnection);



	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psHTBControlIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psHTBControlIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psHTBControlOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto HTBControl_exit;
			}
		}
	}

	if (psHTBControlIN->ui32NumGroups != 0)
	{
		ui32GroupEnableInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psHTBControlIN->ui32NumGroups * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psHTBControlIN->ui32NumGroups * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32GroupEnableInt, psHTBControlIN->pui32GroupEnable, psHTBControlIN->ui32NumGroups * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psHTBControlOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto HTBControl_exit;
				}
			}


	psHTBControlOUT->eError =
		HTBControlKM(
					psHTBControlIN->ui32NumGroups,
					ui32GroupEnableInt,
					psHTBControlIN->ui32LogLevel,
					psHTBControlIN->ui32EnablePID,
					psHTBControlIN->ui32LogMode,
					psHTBControlIN->ui32OpMode);




HTBControl_exit:



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
PVRSRVBridgeHTBLog(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_HTBLOG *psHTBLogIN,
					  PVRSRV_BRIDGE_OUT_HTBLOG *psHTBLogOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_UINT32 *ui32ArgsInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psHTBLogIN->ui32NumArgs * sizeof(IMG_UINT32)) +
			0;


	PVR_UNREFERENCED_PARAMETER(psConnection);



	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psHTBLogIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psHTBLogIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psHTBLogOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto HTBLog_exit;
			}
		}
	}

	if (psHTBLogIN->ui32NumArgs != 0)
	{
		ui32ArgsInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psHTBLogIN->ui32NumArgs * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psHTBLogIN->ui32NumArgs * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32ArgsInt, psHTBLogIN->pui32Args, psHTBLogIN->ui32NumArgs * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psHTBLogOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto HTBLog_exit;
				}
			}


	psHTBLogOUT->eError =
		HTBLogKM(
					psHTBLogIN->ui32PID,
					psHTBLogIN->ui32TimeStamp,
					psHTBLogIN->ui32SF,
					psHTBLogIN->ui32NumArgs,
					ui32ArgsInt);




HTBLog_exit:



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

static POS_LOCK pHTBUFFERBridgeLock;
static IMG_BOOL bUseLock = IMG_TRUE;
#endif /* EXCLUDE_HTBUFFER_BRIDGE */

#if !defined(EXCLUDE_HTBUFFER_BRIDGE)
PVRSRV_ERROR InitHTBUFFERBridge(void);
PVRSRV_ERROR DeinitHTBUFFERBridge(void);

/*
 * Register all HTBUFFER functions with services
 */
PVRSRV_ERROR InitHTBUFFERBridge(void)
{
	PVR_LOGR_IF_ERROR(OSLockCreate(&pHTBUFFERBridgeLock, LOCK_TYPE_PASSIVE), "OSLockCreate");

	SetDispatchTableEntry(PVRSRV_BRIDGE_HTBUFFER, PVRSRV_BRIDGE_HTBUFFER_HTBCONFIGURE, PVRSRVBridgeHTBConfigure,
					pHTBUFFERBridgeLock, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_HTBUFFER, PVRSRV_BRIDGE_HTBUFFER_HTBCONTROL, PVRSRVBridgeHTBControl,
					pHTBUFFERBridgeLock, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_HTBUFFER, PVRSRV_BRIDGE_HTBUFFER_HTBLOG, PVRSRVBridgeHTBLog,
					pHTBUFFERBridgeLock, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all htbuffer functions with services
 */
PVRSRV_ERROR DeinitHTBUFFERBridge(void)
{
	PVR_LOGR_IF_ERROR(OSLockDestroy(pHTBUFFERBridgeLock), "OSLockDestroy");
	return PVRSRV_OK;
}
#else /* EXCLUDE_HTBUFFER_BRIDGE */
/* This bridge is conditional on EXCLUDE_HTBUFFER_BRIDGE - when defined,
 * do not populate the dispatch table with its functions
 */
#define InitHTBUFFERBridge() \
	PVRSRV_OK

#define DeinitHTBUFFERBridge() \
	PVRSRV_OK

#endif /* EXCLUDE_HTBUFFER_BRIDGE */
