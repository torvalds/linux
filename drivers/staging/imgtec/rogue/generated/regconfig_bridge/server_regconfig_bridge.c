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

#include <linux/slab.h>



#if !defined(EXCLUDE_REGCONFIG_BRIDGE)



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXSetRegConfigType(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXSETREGCONFIGTYPE *psRGXSetRegConfigTypeIN,
					  PVRSRV_BRIDGE_OUT_RGXSETREGCONFIGTYPE *psRGXSetRegConfigTypeOUT,
					 CONNECTION_DATA *psConnection)
{








	psRGXSetRegConfigTypeOUT->eError =
		PVRSRVRGXSetRegConfigTypeKM(psConnection, OSGetDevData(psConnection),
					psRGXSetRegConfigTypeIN->ui8RegPowerIsland);








	return 0;
}


static IMG_INT
PVRSRVBridgeRGXAddRegconfig(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXADDREGCONFIG *psRGXAddRegconfigIN,
					  PVRSRV_BRIDGE_OUT_RGXADDREGCONFIG *psRGXAddRegconfigOUT,
					 CONNECTION_DATA *psConnection)
{








	psRGXAddRegconfigOUT->eError =
		PVRSRVRGXAddRegConfigKM(psConnection, OSGetDevData(psConnection),
					psRGXAddRegconfigIN->ui32RegAddr,
					psRGXAddRegconfigIN->ui64RegValue,
					psRGXAddRegconfigIN->ui64RegMask);








	return 0;
}


static IMG_INT
PVRSRVBridgeRGXClearRegConfig(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXCLEARREGCONFIG *psRGXClearRegConfigIN,
					  PVRSRV_BRIDGE_OUT_RGXCLEARREGCONFIG *psRGXClearRegConfigOUT,
					 CONNECTION_DATA *psConnection)
{



	PVR_UNREFERENCED_PARAMETER(psRGXClearRegConfigIN);





	psRGXClearRegConfigOUT->eError =
		PVRSRVRGXClearRegConfigKM(psConnection, OSGetDevData(psConnection)
					);








	return 0;
}


static IMG_INT
PVRSRVBridgeRGXEnableRegConfig(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXENABLEREGCONFIG *psRGXEnableRegConfigIN,
					  PVRSRV_BRIDGE_OUT_RGXENABLEREGCONFIG *psRGXEnableRegConfigOUT,
					 CONNECTION_DATA *psConnection)
{



	PVR_UNREFERENCED_PARAMETER(psRGXEnableRegConfigIN);





	psRGXEnableRegConfigOUT->eError =
		PVRSRVRGXEnableRegConfigKM(psConnection, OSGetDevData(psConnection)
					);








	return 0;
}


static IMG_INT
PVRSRVBridgeRGXDisableRegConfig(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXDISABLEREGCONFIG *psRGXDisableRegConfigIN,
					  PVRSRV_BRIDGE_OUT_RGXDISABLEREGCONFIG *psRGXDisableRegConfigOUT,
					 CONNECTION_DATA *psConnection)
{



	PVR_UNREFERENCED_PARAMETER(psRGXDisableRegConfigIN);





	psRGXDisableRegConfigOUT->eError =
		PVRSRVRGXDisableRegConfigKM(psConnection, OSGetDevData(psConnection)
					);








	return 0;
}




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;
#endif /* EXCLUDE_REGCONFIG_BRIDGE */

#if !defined(EXCLUDE_REGCONFIG_BRIDGE)
PVRSRV_ERROR InitREGCONFIGBridge(void);
PVRSRV_ERROR DeinitREGCONFIGBridge(void);

/*
 * Register all REGCONFIG functions with services
 */
PVRSRV_ERROR InitREGCONFIGBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_REGCONFIG, PVRSRV_BRIDGE_REGCONFIG_RGXSETREGCONFIGTYPE, PVRSRVBridgeRGXSetRegConfigType,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_REGCONFIG, PVRSRV_BRIDGE_REGCONFIG_RGXADDREGCONFIG, PVRSRVBridgeRGXAddRegconfig,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_REGCONFIG, PVRSRV_BRIDGE_REGCONFIG_RGXCLEARREGCONFIG, PVRSRVBridgeRGXClearRegConfig,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_REGCONFIG, PVRSRV_BRIDGE_REGCONFIG_RGXENABLEREGCONFIG, PVRSRVBridgeRGXEnableRegConfig,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_REGCONFIG, PVRSRV_BRIDGE_REGCONFIG_RGXDISABLEREGCONFIG, PVRSRVBridgeRGXDisableRegConfig,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all regconfig functions with services
 */
PVRSRV_ERROR DeinitREGCONFIGBridge(void)
{
	return PVRSRV_OK;
}
#else /* EXCLUDE_REGCONFIG_BRIDGE */
/* This bridge is conditional on EXCLUDE_REGCONFIG_BRIDGE - when defined,
 * do not populate the dispatch table with its functions
 */
#define InitREGCONFIGBridge() \
	PVRSRV_OK

#define DeinitREGCONFIGBridge() \
	PVRSRV_OK

#endif /* EXCLUDE_REGCONFIG_BRIDGE */
