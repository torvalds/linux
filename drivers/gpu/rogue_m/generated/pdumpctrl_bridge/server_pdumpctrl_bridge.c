/*************************************************************************/ /*!
@File
@Title          Server bridge for pdumpctrl
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for pdumpctrl
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

#include "pdump_km.h"


#include "common_pdumpctrl_bridge.h"

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

#include "lock.h"



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgePVRSRVPDumpIsCapturing(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PVRSRVPDUMPISCAPTURING *psPVRSRVPDumpIsCapturingIN,
					  PVRSRV_BRIDGE_OUT_PVRSRVPDUMPISCAPTURING *psPVRSRVPDumpIsCapturingOUT,
					 CONNECTION_DATA *psConnection)
{

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psPVRSRVPDumpIsCapturingIN);






	psPVRSRVPDumpIsCapturingOUT->eError =
		PDumpIsCaptureFrameKM(
					&psPVRSRVPDumpIsCapturingOUT->bIsCapturing);





	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpGetFrame(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PVRSRVPDUMPGETFRAME *psPVRSRVPDumpGetFrameIN,
					  PVRSRV_BRIDGE_OUT_PVRSRVPDUMPGETFRAME *psPVRSRVPDumpGetFrameOUT,
					 CONNECTION_DATA *psConnection)
{

	PVR_UNREFERENCED_PARAMETER(psPVRSRVPDumpGetFrameIN);






	psPVRSRVPDumpGetFrameOUT->eError =
		PDumpGetFrameKM(psConnection,
					&psPVRSRVPDumpGetFrameOUT->ui32Frame);





	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpSetDefaultCaptureParams(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PVRSRVPDUMPSETDEFAULTCAPTUREPARAMS *psPVRSRVPDumpSetDefaultCaptureParamsIN,
					  PVRSRV_BRIDGE_OUT_PVRSRVPDUMPSETDEFAULTCAPTUREPARAMS *psPVRSRVPDumpSetDefaultCaptureParamsOUT,
					 CONNECTION_DATA *psConnection)
{

	PVR_UNREFERENCED_PARAMETER(psConnection);






	psPVRSRVPDumpSetDefaultCaptureParamsOUT->eError =
		PDumpSetDefaultCaptureParamsKM(
					psPVRSRVPDumpSetDefaultCaptureParamsIN->ui32Mode,
					psPVRSRVPDumpSetDefaultCaptureParamsIN->ui32Start,
					psPVRSRVPDumpSetDefaultCaptureParamsIN->ui32End,
					psPVRSRVPDumpSetDefaultCaptureParamsIN->ui32Interval,
					psPVRSRVPDumpSetDefaultCaptureParamsIN->ui32MaxParamFileSize);





	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpIsLastCaptureFrame(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PVRSRVPDUMPISLASTCAPTUREFRAME *psPVRSRVPDumpIsLastCaptureFrameIN,
					  PVRSRV_BRIDGE_OUT_PVRSRVPDUMPISLASTCAPTUREFRAME *psPVRSRVPDumpIsLastCaptureFrameOUT,
					 CONNECTION_DATA *psConnection)
{

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psPVRSRVPDumpIsLastCaptureFrameIN);






	psPVRSRVPDumpIsLastCaptureFrameOUT->eError =
		PDumpIsLastCaptureFrameKM(
					);





	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpStartInitPhase(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PVRSRVPDUMPSTARTINITPHASE *psPVRSRVPDumpStartInitPhaseIN,
					  PVRSRV_BRIDGE_OUT_PVRSRVPDUMPSTARTINITPHASE *psPVRSRVPDumpStartInitPhaseOUT,
					 CONNECTION_DATA *psConnection)
{

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psPVRSRVPDumpStartInitPhaseIN);






	psPVRSRVPDumpStartInitPhaseOUT->eError =
		PDumpStartInitPhaseKM(
					);





	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpStopInitPhase(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PVRSRVPDUMPSTOPINITPHASE *psPVRSRVPDumpStopInitPhaseIN,
					  PVRSRV_BRIDGE_OUT_PVRSRVPDUMPSTOPINITPHASE *psPVRSRVPDumpStopInitPhaseOUT,
					 CONNECTION_DATA *psConnection)
{

	PVR_UNREFERENCED_PARAMETER(psConnection);






	psPVRSRVPDumpStopInitPhaseOUT->eError =
		PDumpStopInitPhaseKM(
					psPVRSRVPDumpStopInitPhaseIN->eModuleID);





	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static POS_LOCK pPDUMPCTRLBridgeLock;
static IMG_BYTE pbyPDUMPCTRLBridgeBuffer[20 +  8];

PVRSRV_ERROR InitPDUMPCTRLBridge(IMG_VOID);
PVRSRV_ERROR DeinitPDUMPCTRLBridge(IMG_VOID);

/*
 * Register all PDUMPCTRL functions with services
 */
PVRSRV_ERROR InitPDUMPCTRLBridge(IMG_VOID)
{
	PVR_LOGR_IF_ERROR(OSLockCreate(&pPDUMPCTRLBridgeLock, LOCK_TYPE_PASSIVE), "OSLockCreate");

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL, PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPISCAPTURING, PVRSRVBridgePVRSRVPDumpIsCapturing,
					pPDUMPCTRLBridgeLock, pbyPDUMPCTRLBridgeBuffer,
					20,  8);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL, PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPGETFRAME, PVRSRVBridgePVRSRVPDumpGetFrame,
					pPDUMPCTRLBridgeLock, pbyPDUMPCTRLBridgeBuffer,
					20,  8);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL, PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPSETDEFAULTCAPTUREPARAMS, PVRSRVBridgePVRSRVPDumpSetDefaultCaptureParams,
					pPDUMPCTRLBridgeLock, pbyPDUMPCTRLBridgeBuffer,
					20,  8);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL, PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPISLASTCAPTUREFRAME, PVRSRVBridgePVRSRVPDumpIsLastCaptureFrame,
					pPDUMPCTRLBridgeLock, pbyPDUMPCTRLBridgeBuffer,
					20,  8);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL, PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPSTARTINITPHASE, PVRSRVBridgePVRSRVPDumpStartInitPhase,
					pPDUMPCTRLBridgeLock, pbyPDUMPCTRLBridgeBuffer,
					20,  8);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL, PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPSTOPINITPHASE, PVRSRVBridgePVRSRVPDumpStopInitPhase,
					pPDUMPCTRLBridgeLock, pbyPDUMPCTRLBridgeBuffer,
					20,  8);


	return PVRSRV_OK;
}

/*
 * Unregister all pdumpctrl functions with services
 */
PVRSRV_ERROR DeinitPDUMPCTRLBridge(IMG_VOID)
{
	PVR_LOGR_IF_ERROR(OSLockDestroy(pPDUMPCTRLBridgeLock), "OSLockDestroy");
	return PVRSRV_OK;
}

