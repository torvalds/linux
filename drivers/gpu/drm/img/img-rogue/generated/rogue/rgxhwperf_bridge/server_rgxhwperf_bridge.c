/*******************************************************************************
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
*******************************************************************************/

#include <linux/uaccess.h>

#include "img_defs.h"

#include "rgxhwperf.h"
#include "rgx_fwif_km.h"

#include "common_rgxhwperf_bridge.h"

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

/* ***************************************************************************
 * Server-side bridge entry points
 */

static IMG_INT
PVRSRVBridgeRGXCtrlHWPerf(IMG_UINT32 ui32DispatchTableEntry,
			  IMG_UINT8 * psRGXCtrlHWPerfIN_UI8,
			  IMG_UINT8 * psRGXCtrlHWPerfOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCTRLHWPERF *psRGXCtrlHWPerfIN =
	    (PVRSRV_BRIDGE_IN_RGXCTRLHWPERF *) IMG_OFFSET_ADDR(psRGXCtrlHWPerfIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCTRLHWPERF *psRGXCtrlHWPerfOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCTRLHWPERF *) IMG_OFFSET_ADDR(psRGXCtrlHWPerfOUT_UI8, 0);

	psRGXCtrlHWPerfOUT->eError =
	    PVRSRVRGXCtrlHWPerfKM(psConnection, OSGetDevNode(psConnection),
				  psRGXCtrlHWPerfIN->ui32StreamId,
				  psRGXCtrlHWPerfIN->bToggle, psRGXCtrlHWPerfIN->ui64Mask);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXConfigEnableHWPerfCounters(IMG_UINT32 ui32DispatchTableEntry,
					  IMG_UINT8 * psRGXConfigEnableHWPerfCountersIN_UI8,
					  IMG_UINT8 * psRGXConfigEnableHWPerfCountersOUT_UI8,
					  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCONFIGENABLEHWPERFCOUNTERS *psRGXConfigEnableHWPerfCountersIN =
	    (PVRSRV_BRIDGE_IN_RGXCONFIGENABLEHWPERFCOUNTERS *)
	    IMG_OFFSET_ADDR(psRGXConfigEnableHWPerfCountersIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCONFIGENABLEHWPERFCOUNTERS *psRGXConfigEnableHWPerfCountersOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCONFIGENABLEHWPERFCOUNTERS *)
	    IMG_OFFSET_ADDR(psRGXConfigEnableHWPerfCountersOUT_UI8, 0);

	RGX_HWPERF_CONFIG_CNTBLK *psBlockConfigsInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen * sizeof(RGX_HWPERF_CONFIG_CNTBLK)) +
	    0;

	if (unlikely
	    (psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen > RGXFWIF_HWPERF_CTRL_BLKS_MAX))
	{
		psRGXConfigEnableHWPerfCountersOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXConfigEnableHWPerfCounters_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXConfigEnableHWPerfCountersIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer =
			    (IMG_BYTE *) (void *)psRGXConfigEnableHWPerfCountersIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXConfigEnableHWPerfCountersOUT->eError =
				    PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXConfigEnableHWPerfCounters_exit;
			}
		}
	}

	if (psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen != 0)
	{
		psBlockConfigsInt =
		    (RGX_HWPERF_CONFIG_CNTBLK *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen *
		    sizeof(RGX_HWPERF_CONFIG_CNTBLK);
	}

	/* Copy the data over */
	if (psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen * sizeof(RGX_HWPERF_CONFIG_CNTBLK) > 0)
	{
		if (OSCopyFromUser
		    (NULL, psBlockConfigsInt,
		     (const void __user *)psRGXConfigEnableHWPerfCountersIN->psBlockConfigs,
		     psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen *
		     sizeof(RGX_HWPERF_CONFIG_CNTBLK)) != PVRSRV_OK)
		{
			psRGXConfigEnableHWPerfCountersOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXConfigEnableHWPerfCounters_exit;
		}
	}

	psRGXConfigEnableHWPerfCountersOUT->eError =
	    PVRSRVRGXConfigEnableHWPerfCountersKM(psConnection, OSGetDevNode(psConnection),
						  psRGXConfigEnableHWPerfCountersIN->ui32ArrayLen,
						  psBlockConfigsInt);

RGXConfigEnableHWPerfCounters_exit:

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXControlHWPerfBlocks(IMG_UINT32 ui32DispatchTableEntry,
				   IMG_UINT8 * psRGXControlHWPerfBlocksIN_UI8,
				   IMG_UINT8 * psRGXControlHWPerfBlocksOUT_UI8,
				   CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCONTROLHWPERFBLOCKS *psRGXControlHWPerfBlocksIN =
	    (PVRSRV_BRIDGE_IN_RGXCONTROLHWPERFBLOCKS *)
	    IMG_OFFSET_ADDR(psRGXControlHWPerfBlocksIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCONTROLHWPERFBLOCKS *psRGXControlHWPerfBlocksOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCONTROLHWPERFBLOCKS *)
	    IMG_OFFSET_ADDR(psRGXControlHWPerfBlocksOUT_UI8, 0);

	IMG_UINT16 *ui16BlockIDsInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (psRGXControlHWPerfBlocksIN->ui32ArrayLen * sizeof(IMG_UINT16)) + 0;

	if (unlikely(psRGXControlHWPerfBlocksIN->ui32ArrayLen > RGXFWIF_HWPERF_CTRL_BLKS_MAX))
	{
		psRGXControlHWPerfBlocksOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXControlHWPerfBlocks_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXControlHWPerfBlocksIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRGXControlHWPerfBlocksIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXControlHWPerfBlocksOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXControlHWPerfBlocks_exit;
			}
		}
	}

	if (psRGXControlHWPerfBlocksIN->ui32ArrayLen != 0)
	{
		ui16BlockIDsInt = (IMG_UINT16 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXControlHWPerfBlocksIN->ui32ArrayLen * sizeof(IMG_UINT16);
	}

	/* Copy the data over */
	if (psRGXControlHWPerfBlocksIN->ui32ArrayLen * sizeof(IMG_UINT16) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui16BlockIDsInt,
		     (const void __user *)psRGXControlHWPerfBlocksIN->pui16BlockIDs,
		     psRGXControlHWPerfBlocksIN->ui32ArrayLen * sizeof(IMG_UINT16)) != PVRSRV_OK)
		{
			psRGXControlHWPerfBlocksOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXControlHWPerfBlocks_exit;
		}
	}

	psRGXControlHWPerfBlocksOUT->eError =
	    PVRSRVRGXControlHWPerfBlocksKM(psConnection, OSGetDevNode(psConnection),
					   psRGXControlHWPerfBlocksIN->bEnable,
					   psRGXControlHWPerfBlocksIN->ui32ArrayLen,
					   ui16BlockIDsInt);

RGXControlHWPerfBlocks_exit:

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXConfigCustomCounters(IMG_UINT32 ui32DispatchTableEntry,
				    IMG_UINT8 * psRGXConfigCustomCountersIN_UI8,
				    IMG_UINT8 * psRGXConfigCustomCountersOUT_UI8,
				    CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCONFIGCUSTOMCOUNTERS *psRGXConfigCustomCountersIN =
	    (PVRSRV_BRIDGE_IN_RGXCONFIGCUSTOMCOUNTERS *)
	    IMG_OFFSET_ADDR(psRGXConfigCustomCountersIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCONFIGCUSTOMCOUNTERS *psRGXConfigCustomCountersOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCONFIGCUSTOMCOUNTERS *)
	    IMG_OFFSET_ADDR(psRGXConfigCustomCountersOUT_UI8, 0);

	IMG_UINT32 *ui32CustomCounterIDsInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32)) + 0;

	if (unlikely
	    (psRGXConfigCustomCountersIN->ui16NumCustomCounters > RGX_HWPERF_MAX_CUSTOM_CNTRS))
	{
		psRGXConfigCustomCountersOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXConfigCustomCounters_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXConfigCustomCountersIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRGXConfigCustomCountersIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXConfigCustomCountersOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXConfigCustomCounters_exit;
			}
		}
	}

	if (psRGXConfigCustomCountersIN->ui16NumCustomCounters != 0)
	{
		ui32CustomCounterIDsInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32CustomCounterIDsInt,
		     (const void __user *)psRGXConfigCustomCountersIN->pui32CustomCounterIDs,
		     psRGXConfigCustomCountersIN->ui16NumCustomCounters * sizeof(IMG_UINT32)) !=
		    PVRSRV_OK)
		{
			psRGXConfigCustomCountersOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXConfigCustomCounters_exit;
		}
	}

	psRGXConfigCustomCountersOUT->eError =
	    PVRSRVRGXConfigCustomCountersKM(psConnection, OSGetDevNode(psConnection),
					    psRGXConfigCustomCountersIN->ui16CustomBlockID,
					    psRGXConfigCustomCountersIN->ui16NumCustomCounters,
					    ui32CustomCounterIDsInt);

RGXConfigCustomCounters_exit:

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXGetHWPerfBvncFeatureFlags(IMG_UINT32 ui32DispatchTableEntry,
					 IMG_UINT8 * psRGXGetHWPerfBvncFeatureFlagsIN_UI8,
					 IMG_UINT8 * psRGXGetHWPerfBvncFeatureFlagsOUT_UI8,
					 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXGETHWPERFBVNCFEATUREFLAGS *psRGXGetHWPerfBvncFeatureFlagsIN =
	    (PVRSRV_BRIDGE_IN_RGXGETHWPERFBVNCFEATUREFLAGS *)
	    IMG_OFFSET_ADDR(psRGXGetHWPerfBvncFeatureFlagsIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXGETHWPERFBVNCFEATUREFLAGS *psRGXGetHWPerfBvncFeatureFlagsOUT =
	    (PVRSRV_BRIDGE_OUT_RGXGETHWPERFBVNCFEATUREFLAGS *)
	    IMG_OFFSET_ADDR(psRGXGetHWPerfBvncFeatureFlagsOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psRGXGetHWPerfBvncFeatureFlagsIN);

	psRGXGetHWPerfBvncFeatureFlagsOUT->eError =
	    PVRSRVRGXGetHWPerfBvncFeatureFlagsKM(psConnection, OSGetDevNode(psConnection),
						 &psRGXGetHWPerfBvncFeatureFlagsOUT->sBVNC);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitRGXHWPERFBridge(void);
PVRSRV_ERROR DeinitRGXHWPERFBridge(void);

/*
 * Register all RGXHWPERF functions with services
 */
PVRSRV_ERROR InitRGXHWPERFBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF, PVRSRV_BRIDGE_RGXHWPERF_RGXCTRLHWPERF,
			      PVRSRVBridgeRGXCtrlHWPerf, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF,
			      PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGENABLEHWPERFCOUNTERS,
			      PVRSRVBridgeRGXConfigEnableHWPerfCounters, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF,
			      PVRSRV_BRIDGE_RGXHWPERF_RGXCONTROLHWPERFBLOCKS,
			      PVRSRVBridgeRGXControlHWPerfBlocks, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF,
			      PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGCUSTOMCOUNTERS,
			      PVRSRVBridgeRGXConfigCustomCounters, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF,
			      PVRSRV_BRIDGE_RGXHWPERF_RGXGETHWPERFBVNCFEATUREFLAGS,
			      PVRSRVBridgeRGXGetHWPerfBvncFeatureFlags, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxhwperf functions with services
 */
PVRSRV_ERROR DeinitRGXHWPERFBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF, PVRSRV_BRIDGE_RGXHWPERF_RGXCTRLHWPERF);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF,
				PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGENABLEHWPERFCOUNTERS);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF,
				PVRSRV_BRIDGE_RGXHWPERF_RGXCONTROLHWPERFBLOCKS);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF,
				PVRSRV_BRIDGE_RGXHWPERF_RGXCONFIGCUSTOMCOUNTERS);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXHWPERF,
				PVRSRV_BRIDGE_RGXHWPERF_RGXGETHWPERFBVNCFEATUREFLAGS);

	return PVRSRV_OK;
}
