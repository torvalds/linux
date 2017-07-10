/*************************************************************************/ /*!
@File           rgxsignals.c
@Title          RGX Signals routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX Signals routines
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

#include "rgxsignals.h"

#include "rgxmem.h"
#include "rgx_fwif_km.h"
#include "mmu_common.h"
#include "devicemem.h"
#include "rgxfwutils.h"


IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXNotifySignalUpdateKM(CONNECTION_DATA *psConnection,
	                                   PVRSRV_DEVICE_NODE	*psDeviceNode,
	                                   IMG_HANDLE hMemCtxPrivData,
	                                   IMG_DEV_VIRTADDR sDevSignalAddress)
{
	DEVMEM_MEMDESC *psFWMemContextMemDesc;
	RGXFWIF_KCCB_CMD sKCCBCmd;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);

	/* Schedule the firmware command */
	sKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_NOTIFY_SIGNAL_UPDATE;
	sKCCBCmd.uCmdData.sSignalUpdateData.sDevSignalAddress = sDevSignalAddress;
	RGXSetFirmwareAddress(&sKCCBCmd.uCmdData.sSignalUpdateData.psFWMemContext,
	                      psFWMemContextMemDesc,
	                      0, RFW_FWADDR_NOREF_FLAG);

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand((PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice,
		                            RGXFWIF_DM_GP,
		                            &sKCCBCmd,
		                            sizeof(sKCCBCmd),
		                            0,
		                            PDUMP_FLAGS_NONE);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXNotifySignalUpdateKM: Failed to schedule the FW command %d (%s)",
				eError, PVRSRVGETERRORSTRING(eError)));
	}

	return eError;
}
