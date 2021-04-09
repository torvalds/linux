/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxhwperf
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxhwperf
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

#include "rgxhwperf.h"


#include "common_rgxhwperf_bridge.h"

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
PVRSRVBridgeRGXCtrlHWPerf(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCTRLHWPERF *psRGXCtrlHWPerfIN,
					  PVRSRV_BRIDGE_OUT_RGXCTRLHWPERF *psRGXCtrlHWPerfOUT,
					 CONNECTION_DATA *psConnection)
{








	psRGXCtrlHWPerfOUT->eError =
		PVRSRVRGXCtrlHWPerfKM(psConnection, OSGetDevData(psConnection),
					psRGXCtrlHWPerfIN->ui32StreamId,
					psRGXCtrlHWPerfIN->bToggle,
					psRGXCtrlHWPerfIN->ui64Mask);








	return 0;
}


static IMG_INT
PVRSRVBridgeRGXConfigEnableHWPerfCounters(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCONFIGENABLEHWPERFCOUNTERS *psRGXConfigEnableHWPerfCountersIN,
					  PVRSRV_BRIDGE_OUT_RGXCONFIGENABLEHWPERFCOUNTERS *psRGXConfigEnableHWPerfCountersOUT,
					 CONNECTION_DATA *psConnection)
{
	RGX_HWPERF_CONFIG_CNTBLK *psBlockConfigsInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen * sizeof(RGX_HWPERF_CONFIG_CNTBLK)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXConfigEnableHWPerfCountersIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXConfigEnableHWPerfCountersIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXConfigEnableHWPerfCountersOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXConfigEnableHWPerfCounters_exit;
			}
		}
	}

	if (psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen != 0)
	{
		psBlockConfigsInt = (RGX_HWPERF_CONFIG_CNTBLK*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen * sizeof(RGX_HWPERF_CONFIG_CNTBLK);
	}

			/* Copy the data over */
			if (psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen * sizeof(RGX_HWPERF_CONFIG_CNTBLK) > 0)
			{
				if ( OSCopyFromUser(NULL, psBlockConfigsInt, psRGXConfigEnableHWPerfCountersIN->psBlockConfigs, psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen * sizeof(RGX_HWPERF_CONFIG_CNTBLK)) != PVRSRV_OK )
				{
					psRGXConfigEnableHWPerfCountersOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXConfigEnableHWPerfCounters_exit;
				}
			}


	psRGXConfigEnableHWPerfCountersOUT->eError =
		PVRSRVRGXConfigEnableHWPerfCountersKM(psConnection, OSGetDevData(psConnection),
					psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen,
					psBlockConfigsInt);




RGXConfigEnableHWPerfCounters_exit:



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
PVRSRVBridgeRGXCtrlHWPerfCounters(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCTRLHWPERFCOUNTERS *psRGXCtrlHWPerfCountersIN,
					  PVRSRV_BRIDGE_OUT_RGXCTRLHWPERFCOUNTERS *psRGXCtrlHWPerfCountersOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_UINT16 *ui16BlockIDsInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXCtrlHWPerfCountersIN->ui32ArrayLen * sizeof(IMG_UINT16)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXCtrlHWPerfCountersIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXCtrlHWPerfCountersIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXCtrlHWPerfCountersOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXCtrlHWPerfCounters_exit;
			}
		}
	}

	if (psRGXCtrlHWPerfCountersIN->ui32ArrayLen != 0)
	{
		ui16BlockIDsInt = (IMG_UINT16*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXCtrlHWPerfCountersIN->ui32ArrayLen * sizeof(IMG_UINT16);
	}

			/* Copy the data over */
			if (psRGXCtrlHWPerfCountersIN->ui32ArrayLen * sizeof(IMG_UINT16) > 0)
			{
				if ( OSCopyFromUser(NULL, ui16BlockIDsInt, psRGXCtrlHWPerfCountersIN->pui16BlockIDs, psRGXCtrlHWPerfCountersIN->ui32ArrayLen * sizeof(IMG_UINT16)) != PVRSRV_OK )
				{
					psRGXCtrlHWPerfCountersOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXCtrlHWPerfCounters_exit;
				}
			}


	psRGXCtrlHWPerfCountersOUT->eError =
		PVRSRVRGXCtrlHWPerfCountersKM(psConnection, OSGetDevData(psConnection),
					psRGXCtrlHWPerfCountersIN->bEnable,
					psRGXCtrlHWPerfCountersIN->ui32ArrayLen,
					ui16BlockIDsInt);




RGXCtrlHWPerfCounters_exit:



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
PVRSRVBridgeRGXConfigCustomCounters(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCONFIGCUSTOMCOUNTERS *psRGXConfigCustomCountersIN,
					  PVRSRV_BRIDGE_OUT_RGXCONFIGCUSTOMCOUNTERS *psRGXConfigCustomCountersOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_UINT32 *ui32CustomCounterIDsInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRGXConfigCustomCountersIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRGXConfigCustomCountersIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRGXConfigCustomCountersOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXConfigCustomCounters_exit;
			}
		}
	}

	if (psRGXConfigCustomCountersIN->ui16NumCustomCounters != 0)
	{
		ui32CustomCounterIDsInt = (IMG_UINT32*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32);
	}

			/* Copy the data over */
			if (psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32) > 0)
			{
				if ( OSCopyFromUser(NULL, ui32CustomCounterIDsInt, psRGXConfigCustomCountersIN->pui32CustomCounterIDs, psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32)) != PVRSRV_OK )
				{
					psRGXConfigCustomCountersOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXConfigCustomCounters_exit;
				}
			}


	psRGXConfigCustomCountersOUT->eError =
		PVRSRVRGXConfigCustomCountersKM(psConnection, OSGetDevData(psConnection),
					psRGXConfigCustomCountersIN->ui16CustomBlockID,
					psRGXConfigCustomCountersIN->ui16NumCustomCounters,
					ui32CustomCounterIDsInt);




RGXConfigCustomCounters_exit:



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

PVRSRV_ERROR InitRGXHWPERFBridge(void);
PVRSRV_ERROR DeinitRGXHWPERFBridge(void);

/*
 * Register all RGXHWPERF functions with services
 */
PVRSRV_ERROR InitRGXHWPERFBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF, PVRSRV_BRIDGE_RGXHWPERF_RGXCTRLHWPERF, PVRSRVBridgeRGXCtrlHWPerf,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF, PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGENABLEHWPERFCOUNTERS, PVRSRVBridgeRGXConfigEnableHWPerfCounters,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF, PVRSRV_BRIDGE_RGXHWPERF_RGXCTRLHWPERFCOUNTERS, PVRSRVBridgeRGXCtrlHWPerfCounters,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF, PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGCUSTOMCOUNTERS, PVRSRVBridgeRGXConfigCustomCounters,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all rgxhwperf functions with services
 */
PVRSRV_ERROR DeinitRGXHWPERFBridge(void)
{
	return PVRSRV_OK;
}
