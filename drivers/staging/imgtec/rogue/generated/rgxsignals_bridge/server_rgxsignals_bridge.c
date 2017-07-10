/*************************************************************************/ /*!
@File
@Title          Server bridge for rgxsignals
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxsignals
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

#include "rgxsignals.h"


#include "common_rgxsignals_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>


#include "rgx_bvnc_defs_km.h"




/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeRGXNotifySignalUpdate(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RGXNOTIFYSIGNALUPDATE *psRGXNotifySignalUpdateIN,
					  PVRSRV_BRIDGE_OUT_RGXNOTIFYSIGNALUPDATE *psRGXNotifySignalUpdateOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPrivData = psRGXNotifySignalUpdateIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;


	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevData(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
			!psDeviceNode->pfnCheckDeviceFeature(psDeviceNode, RGX_FEATURE_SIGNAL_SNOOPING_BIT_MASK))
		{
			psRGXNotifySignalUpdateOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXNotifySignalUpdate_exit;
		}
	}





	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRGXNotifySignalUpdateOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &hPrivDataInt,
											hPrivData,
											PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
											IMG_TRUE);
					if(psRGXNotifySignalUpdateOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RGXNotifySignalUpdate_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRGXNotifySignalUpdateOUT->eError =
		PVRSRVRGXNotifySignalUpdateKM(psConnection, OSGetDevData(psConnection),
					hPrivDataInt,
					psRGXNotifySignalUpdateIN->sDevSignalAddress);




RGXNotifySignalUpdate_exit:

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


	return 0;
}




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;

PVRSRV_ERROR InitRGXSIGNALSBridge(void);
PVRSRV_ERROR DeinitRGXSIGNALSBridge(void);

/*
 * Register all RGXSIGNALS functions with services
 */
PVRSRV_ERROR InitRGXSIGNALSBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXSIGNALS, PVRSRV_BRIDGE_RGXSIGNALS_RGXNOTIFYSIGNALUPDATE, PVRSRVBridgeRGXNotifySignalUpdate,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all rgxsignals functions with services
 */
PVRSRV_ERROR DeinitRGXSIGNALSBridge(void)
{
	return PVRSRV_OK;
}
