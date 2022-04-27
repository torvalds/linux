/*******************************************************************************
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
*******************************************************************************/

#include <linux/uaccess.h>

#include "img_defs.h"

#include "pdump_km.h"

#include "common_pdumpctrl_bridge.h"

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

/* ***************************************************************************
 * Server-side bridge entry points
 */

static IMG_INT
PVRSRVBridgePVRSRVPDumpGetState(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psPVRSRVPDumpGetStateIN_UI8,
				IMG_UINT8 * psPVRSRVPDumpGetStateOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PVRSRVPDUMPGETSTATE *psPVRSRVPDumpGetStateIN =
	    (PVRSRV_BRIDGE_IN_PVRSRVPDUMPGETSTATE *) IMG_OFFSET_ADDR(psPVRSRVPDumpGetStateIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_PVRSRVPDUMPGETSTATE *psPVRSRVPDumpGetStateOUT =
	    (PVRSRV_BRIDGE_OUT_PVRSRVPDUMPGETSTATE *) IMG_OFFSET_ADDR(psPVRSRVPDumpGetStateOUT_UI8,
								      0);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psPVRSRVPDumpGetStateIN);

	psPVRSRVPDumpGetStateOUT->eError = PDumpGetStateKM(&psPVRSRVPDumpGetStateOUT->ui64State);

	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpGetFrame(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psPVRSRVPDumpGetFrameIN_UI8,
				IMG_UINT8 * psPVRSRVPDumpGetFrameOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PVRSRVPDUMPGETFRAME *psPVRSRVPDumpGetFrameIN =
	    (PVRSRV_BRIDGE_IN_PVRSRVPDUMPGETFRAME *) IMG_OFFSET_ADDR(psPVRSRVPDumpGetFrameIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_PVRSRVPDUMPGETFRAME *psPVRSRVPDumpGetFrameOUT =
	    (PVRSRV_BRIDGE_OUT_PVRSRVPDUMPGETFRAME *) IMG_OFFSET_ADDR(psPVRSRVPDumpGetFrameOUT_UI8,
								      0);

	PVR_UNREFERENCED_PARAMETER(psPVRSRVPDumpGetFrameIN);

	psPVRSRVPDumpGetFrameOUT->eError =
	    PDumpGetFrameKM(psConnection, OSGetDevNode(psConnection),
			    &psPVRSRVPDumpGetFrameOUT->ui32Frame);

	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpSetDefaultCaptureParams(IMG_UINT32 ui32DispatchTableEntry,
					       IMG_UINT8 *
					       psPVRSRVPDumpSetDefaultCaptureParamsIN_UI8,
					       IMG_UINT8 *
					       psPVRSRVPDumpSetDefaultCaptureParamsOUT_UI8,
					       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PVRSRVPDUMPSETDEFAULTCAPTUREPARAMS *psPVRSRVPDumpSetDefaultCaptureParamsIN
	    =
	    (PVRSRV_BRIDGE_IN_PVRSRVPDUMPSETDEFAULTCAPTUREPARAMS *)
	    IMG_OFFSET_ADDR(psPVRSRVPDumpSetDefaultCaptureParamsIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PVRSRVPDUMPSETDEFAULTCAPTUREPARAMS
	    *psPVRSRVPDumpSetDefaultCaptureParamsOUT =
	    (PVRSRV_BRIDGE_OUT_PVRSRVPDUMPSETDEFAULTCAPTUREPARAMS *)
	    IMG_OFFSET_ADDR(psPVRSRVPDumpSetDefaultCaptureParamsOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psConnection);

	psPVRSRVPDumpSetDefaultCaptureParamsOUT->eError =
	    PDumpSetDefaultCaptureParamsKM(psPVRSRVPDumpSetDefaultCaptureParamsIN->ui32Mode,
					   psPVRSRVPDumpSetDefaultCaptureParamsIN->ui32Start,
					   psPVRSRVPDumpSetDefaultCaptureParamsIN->ui32End,
					   psPVRSRVPDumpSetDefaultCaptureParamsIN->ui32Interval,
					   psPVRSRVPDumpSetDefaultCaptureParamsIN->
					   ui32MaxParamFileSize);

	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpIsLastCaptureFrame(IMG_UINT32 ui32DispatchTableEntry,
					  IMG_UINT8 * psPVRSRVPDumpIsLastCaptureFrameIN_UI8,
					  IMG_UINT8 * psPVRSRVPDumpIsLastCaptureFrameOUT_UI8,
					  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PVRSRVPDUMPISLASTCAPTUREFRAME *psPVRSRVPDumpIsLastCaptureFrameIN =
	    (PVRSRV_BRIDGE_IN_PVRSRVPDUMPISLASTCAPTUREFRAME *)
	    IMG_OFFSET_ADDR(psPVRSRVPDumpIsLastCaptureFrameIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PVRSRVPDUMPISLASTCAPTUREFRAME *psPVRSRVPDumpIsLastCaptureFrameOUT =
	    (PVRSRV_BRIDGE_OUT_PVRSRVPDUMPISLASTCAPTUREFRAME *)
	    IMG_OFFSET_ADDR(psPVRSRVPDumpIsLastCaptureFrameOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psPVRSRVPDumpIsLastCaptureFrameIN);

	psPVRSRVPDumpIsLastCaptureFrameOUT->eError =
	    PDumpIsLastCaptureFrameKM(&psPVRSRVPDumpIsLastCaptureFrameOUT->bpbIsLastCaptureFrame);

	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpForceCaptureStop(IMG_UINT32 ui32DispatchTableEntry,
					IMG_UINT8 * psPVRSRVPDumpForceCaptureStopIN_UI8,
					IMG_UINT8 * psPVRSRVPDumpForceCaptureStopOUT_UI8,
					CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PVRSRVPDUMPFORCECAPTURESTOP *psPVRSRVPDumpForceCaptureStopIN =
	    (PVRSRV_BRIDGE_IN_PVRSRVPDUMPFORCECAPTURESTOP *)
	    IMG_OFFSET_ADDR(psPVRSRVPDumpForceCaptureStopIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PVRSRVPDUMPFORCECAPTURESTOP *psPVRSRVPDumpForceCaptureStopOUT =
	    (PVRSRV_BRIDGE_OUT_PVRSRVPDUMPFORCECAPTURESTOP *)
	    IMG_OFFSET_ADDR(psPVRSRVPDumpForceCaptureStopOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psPVRSRVPDumpForceCaptureStopIN);

	psPVRSRVPDumpForceCaptureStopOUT->eError = PDumpForceCaptureStopKM();

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

static POS_LOCK pPDUMPCTRLBridgeLock;

PVRSRV_ERROR InitPDUMPCTRLBridge(void);
PVRSRV_ERROR DeinitPDUMPCTRLBridge(void);

/*
 * Register all PDUMPCTRL functions with services
 */
PVRSRV_ERROR InitPDUMPCTRLBridge(void)
{
	PVR_LOG_RETURN_IF_ERROR(OSLockCreate(&pPDUMPCTRLBridgeLock), "OSLockCreate");

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL, PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPGETSTATE,
			      PVRSRVBridgePVRSRVPDumpGetState, pPDUMPCTRLBridgeLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL, PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPGETFRAME,
			      PVRSRVBridgePVRSRVPDumpGetFrame, pPDUMPCTRLBridgeLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL,
			      PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPSETDEFAULTCAPTUREPARAMS,
			      PVRSRVBridgePVRSRVPDumpSetDefaultCaptureParams, pPDUMPCTRLBridgeLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL,
			      PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPISLASTCAPTUREFRAME,
			      PVRSRVBridgePVRSRVPDumpIsLastCaptureFrame, pPDUMPCTRLBridgeLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL,
			      PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPFORCECAPTURESTOP,
			      PVRSRVBridgePVRSRVPDumpForceCaptureStop, pPDUMPCTRLBridgeLock);

	return PVRSRV_OK;
}

/*
 * Unregister all pdumpctrl functions with services
 */
PVRSRV_ERROR DeinitPDUMPCTRLBridge(void)
{
	PVR_LOG_RETURN_IF_ERROR(OSLockDestroy(pPDUMPCTRLBridgeLock), "OSLockDestroy");

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL,
				PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPGETSTATE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL,
				PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPGETFRAME);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL,
				PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPSETDEFAULTCAPTUREPARAMS);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL,
				PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPISLASTCAPTUREFRAME);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPCTRL,
				PVRSRV_BRIDGE_PDUMPCTRL_PVRSRVPDUMPFORCECAPTURESTOP);

	return PVRSRV_OK;
}
