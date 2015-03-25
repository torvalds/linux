/*************************************************************************/ /*!
@File
@Title          Server bridge for regconfig
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for regconfig
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

#include "rgxregconfig.h"


#include "common_regconfig_bridge.h"

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



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXSetRegConfigPI(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXSETREGCONFIGPI *psRGXSetRegConfigPIIN,
					 PVRSRV_BRIDGE_OUT_RGXSETREGCONFIGPI *psRGXSetRegConfigPIOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_REGCONFIG_RGXSETREGCONFIGPI);





				{
					/* Look up the address from the handle */
					psRGXSetRegConfigPIOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXSetRegConfigPIIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXSetRegConfigPIOUT->eError != PVRSRV_OK)
					{
						goto RGXSetRegConfigPI_exit;
					}

				}

	psRGXSetRegConfigPIOUT->eError =
		PVRSRVRGXSetRegConfigPIKM(
					hDevNodeInt,
					psRGXSetRegConfigPIIN->ui8RegPowerIsland);



RGXSetRegConfigPI_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXAddRegconfig(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXADDREGCONFIG *psRGXAddRegconfigIN,
					 PVRSRV_BRIDGE_OUT_RGXADDREGCONFIG *psRGXAddRegconfigOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_REGCONFIG_RGXADDREGCONFIG);





				{
					/* Look up the address from the handle */
					psRGXAddRegconfigOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXAddRegconfigIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXAddRegconfigOUT->eError != PVRSRV_OK)
					{
						goto RGXAddRegconfig_exit;
					}

				}

	psRGXAddRegconfigOUT->eError =
		PVRSRVRGXAddRegConfigKM(
					hDevNodeInt,
					psRGXAddRegconfigIN->ui32RegAddr,
					psRGXAddRegconfigIN->ui64RegValue);



RGXAddRegconfig_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXClearRegConfig(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXCLEARREGCONFIG *psRGXClearRegConfigIN,
					 PVRSRV_BRIDGE_OUT_RGXCLEARREGCONFIG *psRGXClearRegConfigOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_REGCONFIG_RGXCLEARREGCONFIG);





				{
					/* Look up the address from the handle */
					psRGXClearRegConfigOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXClearRegConfigIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXClearRegConfigOUT->eError != PVRSRV_OK)
					{
						goto RGXClearRegConfig_exit;
					}

				}

	psRGXClearRegConfigOUT->eError =
		PVRSRVRGXClearRegConfigKM(
					hDevNodeInt);



RGXClearRegConfig_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXEnableRegConfig(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXENABLEREGCONFIG *psRGXEnableRegConfigIN,
					 PVRSRV_BRIDGE_OUT_RGXENABLEREGCONFIG *psRGXEnableRegConfigOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_REGCONFIG_RGXENABLEREGCONFIG);





				{
					/* Look up the address from the handle */
					psRGXEnableRegConfigOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXEnableRegConfigIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXEnableRegConfigOUT->eError != PVRSRV_OK)
					{
						goto RGXEnableRegConfig_exit;
					}

				}

	psRGXEnableRegConfigOUT->eError =
		PVRSRVRGXEnableRegConfigKM(
					hDevNodeInt);



RGXEnableRegConfig_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDisableRegConfig(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_RGXDISABLEREGCONFIG *psRGXDisableRegConfigIN,
					 PVRSRV_BRIDGE_OUT_RGXDISABLEREGCONFIG *psRGXDisableRegConfigOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_REGCONFIG_RGXDISABLEREGCONFIG);





				{
					/* Look up the address from the handle */
					psRGXDisableRegConfigOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psRGXDisableRegConfigIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psRGXDisableRegConfigOUT->eError != PVRSRV_OK)
					{
						goto RGXDisableRegConfig_exit;
					}

				}

	psRGXDisableRegConfigOUT->eError =
		PVRSRVRGXDisableRegConfigKM(
					hDevNodeInt);



RGXDisableRegConfig_exit:

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterREGCONFIGFunctions(IMG_VOID);
IMG_VOID UnregisterREGCONFIGFunctions(IMG_VOID);

/*
 * Register all REGCONFIG functions with services
 */
PVRSRV_ERROR RegisterREGCONFIGFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_REGCONFIG_RGXSETREGCONFIGPI, PVRSRVBridgeRGXSetRegConfigPI);
	SetDispatchTableEntry(PVRSRV_BRIDGE_REGCONFIG_RGXADDREGCONFIG, PVRSRVBridgeRGXAddRegconfig);
	SetDispatchTableEntry(PVRSRV_BRIDGE_REGCONFIG_RGXCLEARREGCONFIG, PVRSRVBridgeRGXClearRegConfig);
	SetDispatchTableEntry(PVRSRV_BRIDGE_REGCONFIG_RGXENABLEREGCONFIG, PVRSRVBridgeRGXEnableRegConfig);
	SetDispatchTableEntry(PVRSRV_BRIDGE_REGCONFIG_RGXDISABLEREGCONFIG, PVRSRVBridgeRGXDisableRegConfig);

	return PVRSRV_OK;
}

/*
 * Unregister all regconfig functions with services
 */
IMG_VOID UnregisterREGCONFIGFunctions(IMG_VOID)
{
}
