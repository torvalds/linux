/*******************************************************************************
@File
@Title          Server bridge for rgxregconfig
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxregconfig
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

#include "rgxregconfig.h"

#include "common_rgxregconfig_bridge.h"

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

#if !defined(EXCLUDE_RGXREGCONFIG_BRIDGE)

/* ***************************************************************************
 * Server-side bridge entry points
 */

static IMG_INT
PVRSRVBridgeRGXSetRegConfigType(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psRGXSetRegConfigTypeIN_UI8,
				IMG_UINT8 * psRGXSetRegConfigTypeOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXSETREGCONFIGTYPE *psRGXSetRegConfigTypeIN =
	    (PVRSRV_BRIDGE_IN_RGXSETREGCONFIGTYPE *) IMG_OFFSET_ADDR(psRGXSetRegConfigTypeIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_RGXSETREGCONFIGTYPE *psRGXSetRegConfigTypeOUT =
	    (PVRSRV_BRIDGE_OUT_RGXSETREGCONFIGTYPE *) IMG_OFFSET_ADDR(psRGXSetRegConfigTypeOUT_UI8,
								      0);

	psRGXSetRegConfigTypeOUT->eError =
	    PVRSRVRGXSetRegConfigTypeKM(psConnection, OSGetDevNode(psConnection),
					psRGXSetRegConfigTypeIN->ui8RegPowerIsland);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXAddRegconfig(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psRGXAddRegconfigIN_UI8,
			    IMG_UINT8 * psRGXAddRegconfigOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXADDREGCONFIG *psRGXAddRegconfigIN =
	    (PVRSRV_BRIDGE_IN_RGXADDREGCONFIG *) IMG_OFFSET_ADDR(psRGXAddRegconfigIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXADDREGCONFIG *psRGXAddRegconfigOUT =
	    (PVRSRV_BRIDGE_OUT_RGXADDREGCONFIG *) IMG_OFFSET_ADDR(psRGXAddRegconfigOUT_UI8, 0);

	psRGXAddRegconfigOUT->eError =
	    PVRSRVRGXAddRegConfigKM(psConnection, OSGetDevNode(psConnection),
				    psRGXAddRegconfigIN->ui32RegAddr,
				    psRGXAddRegconfigIN->ui64RegValue,
				    psRGXAddRegconfigIN->ui64RegMask);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXClearRegConfig(IMG_UINT32 ui32DispatchTableEntry,
			      IMG_UINT8 * psRGXClearRegConfigIN_UI8,
			      IMG_UINT8 * psRGXClearRegConfigOUT_UI8,
			      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCLEARREGCONFIG *psRGXClearRegConfigIN =
	    (PVRSRV_BRIDGE_IN_RGXCLEARREGCONFIG *) IMG_OFFSET_ADDR(psRGXClearRegConfigIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCLEARREGCONFIG *psRGXClearRegConfigOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCLEARREGCONFIG *) IMG_OFFSET_ADDR(psRGXClearRegConfigOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psRGXClearRegConfigIN);

	psRGXClearRegConfigOUT->eError =
	    PVRSRVRGXClearRegConfigKM(psConnection, OSGetDevNode(psConnection));

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXEnableRegConfig(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psRGXEnableRegConfigIN_UI8,
			       IMG_UINT8 * psRGXEnableRegConfigOUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXENABLEREGCONFIG *psRGXEnableRegConfigIN =
	    (PVRSRV_BRIDGE_IN_RGXENABLEREGCONFIG *) IMG_OFFSET_ADDR(psRGXEnableRegConfigIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXENABLEREGCONFIG *psRGXEnableRegConfigOUT =
	    (PVRSRV_BRIDGE_OUT_RGXENABLEREGCONFIG *) IMG_OFFSET_ADDR(psRGXEnableRegConfigOUT_UI8,
								     0);

	PVR_UNREFERENCED_PARAMETER(psRGXEnableRegConfigIN);

	psRGXEnableRegConfigOUT->eError =
	    PVRSRVRGXEnableRegConfigKM(psConnection, OSGetDevNode(psConnection));

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDisableRegConfig(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psRGXDisableRegConfigIN_UI8,
				IMG_UINT8 * psRGXDisableRegConfigOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXDISABLEREGCONFIG *psRGXDisableRegConfigIN =
	    (PVRSRV_BRIDGE_IN_RGXDISABLEREGCONFIG *) IMG_OFFSET_ADDR(psRGXDisableRegConfigIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_RGXDISABLEREGCONFIG *psRGXDisableRegConfigOUT =
	    (PVRSRV_BRIDGE_OUT_RGXDISABLEREGCONFIG *) IMG_OFFSET_ADDR(psRGXDisableRegConfigOUT_UI8,
								      0);

	PVR_UNREFERENCED_PARAMETER(psRGXDisableRegConfigIN);

	psRGXDisableRegConfigOUT->eError =
	    PVRSRVRGXDisableRegConfigKM(psConnection, OSGetDevNode(psConnection));

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

#endif /* EXCLUDE_RGXREGCONFIG_BRIDGE */

#if !defined(EXCLUDE_RGXREGCONFIG_BRIDGE)
PVRSRV_ERROR InitRGXREGCONFIGBridge(void);
PVRSRV_ERROR DeinitRGXREGCONFIGBridge(void);

/*
 * Register all RGXREGCONFIG functions with services
 */
PVRSRV_ERROR InitRGXREGCONFIGBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXREGCONFIG,
			      PVRSRV_BRIDGE_RGXREGCONFIG_RGXSETREGCONFIGTYPE,
			      PVRSRVBridgeRGXSetRegConfigType, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXREGCONFIG,
			      PVRSRV_BRIDGE_RGXREGCONFIG_RGXADDREGCONFIG,
			      PVRSRVBridgeRGXAddRegconfig, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXREGCONFIG,
			      PVRSRV_BRIDGE_RGXREGCONFIG_RGXCLEARREGCONFIG,
			      PVRSRVBridgeRGXClearRegConfig, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXREGCONFIG,
			      PVRSRV_BRIDGE_RGXREGCONFIG_RGXENABLEREGCONFIG,
			      PVRSRVBridgeRGXEnableRegConfig, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXREGCONFIG,
			      PVRSRV_BRIDGE_RGXREGCONFIG_RGXDISABLEREGCONFIG,
			      PVRSRVBridgeRGXDisableRegConfig, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxregconfig functions with services
 */
PVRSRV_ERROR DeinitRGXREGCONFIGBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXREGCONFIG,
				PVRSRV_BRIDGE_RGXREGCONFIG_RGXSETREGCONFIGTYPE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXREGCONFIG,
				PVRSRV_BRIDGE_RGXREGCONFIG_RGXADDREGCONFIG);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXREGCONFIG,
				PVRSRV_BRIDGE_RGXREGCONFIG_RGXCLEARREGCONFIG);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXREGCONFIG,
				PVRSRV_BRIDGE_RGXREGCONFIG_RGXENABLEREGCONFIG);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXREGCONFIG,
				PVRSRV_BRIDGE_RGXREGCONFIG_RGXDISABLEREGCONFIG);

	return PVRSRV_OK;
}
#else /* EXCLUDE_RGXREGCONFIG_BRIDGE */
/* This bridge is conditional on EXCLUDE_RGXREGCONFIG_BRIDGE - when defined,
 * do not populate the dispatch table with its functions
 */
#define InitRGXREGCONFIGBridge() \
	PVRSRV_OK

#define DeinitRGXREGCONFIGBridge() \
	PVRSRV_OK

#endif /* EXCLUDE_RGXREGCONFIG_BRIDGE */
