/*******************************************************************************
@File
@Title          Server bridge for rgxpdump
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxpdump
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

#include "rgxpdump.h"

#include "common_rgxpdump_bridge.h"

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
PVRSRVBridgePDumpTraceBuffer(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psPDumpTraceBufferIN_UI8,
			     IMG_UINT8 * psPDumpTraceBufferOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PDUMPTRACEBUFFER *psPDumpTraceBufferIN =
	    (PVRSRV_BRIDGE_IN_PDUMPTRACEBUFFER *) IMG_OFFSET_ADDR(psPDumpTraceBufferIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PDUMPTRACEBUFFER *psPDumpTraceBufferOUT =
	    (PVRSRV_BRIDGE_OUT_PDUMPTRACEBUFFER *) IMG_OFFSET_ADDR(psPDumpTraceBufferOUT_UI8, 0);

	psPDumpTraceBufferOUT->eError =
	    PVRSRVPDumpTraceBufferKM(psConnection, OSGetDevNode(psConnection),
				     psPDumpTraceBufferIN->ui32PDumpFlags);

	return 0;
}

static IMG_INT
PVRSRVBridgePDumpSignatureBuffer(IMG_UINT32 ui32DispatchTableEntry,
				 IMG_UINT8 * psPDumpSignatureBufferIN_UI8,
				 IMG_UINT8 * psPDumpSignatureBufferOUT_UI8,
				 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PDUMPSIGNATUREBUFFER *psPDumpSignatureBufferIN =
	    (PVRSRV_BRIDGE_IN_PDUMPSIGNATUREBUFFER *) IMG_OFFSET_ADDR(psPDumpSignatureBufferIN_UI8,
								      0);
	PVRSRV_BRIDGE_OUT_PDUMPSIGNATUREBUFFER *psPDumpSignatureBufferOUT =
	    (PVRSRV_BRIDGE_OUT_PDUMPSIGNATUREBUFFER *)
	    IMG_OFFSET_ADDR(psPDumpSignatureBufferOUT_UI8, 0);

	psPDumpSignatureBufferOUT->eError =
	    PVRSRVPDumpSignatureBufferKM(psConnection, OSGetDevNode(psConnection),
					 psPDumpSignatureBufferIN->ui32PDumpFlags);

	return 0;
}

#if defined(SUPPORT_VALIDATION)

static IMG_INT
PVRSRVBridgePDumpComputeCRCSignatureCheck(IMG_UINT32 ui32DispatchTableEntry,
					  IMG_UINT8 * psPDumpComputeCRCSignatureCheckIN_UI8,
					  IMG_UINT8 * psPDumpComputeCRCSignatureCheckOUT_UI8,
					  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PDUMPCOMPUTECRCSIGNATURECHECK *psPDumpComputeCRCSignatureCheckIN =
	    (PVRSRV_BRIDGE_IN_PDUMPCOMPUTECRCSIGNATURECHECK *)
	    IMG_OFFSET_ADDR(psPDumpComputeCRCSignatureCheckIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PDUMPCOMPUTECRCSIGNATURECHECK *psPDumpComputeCRCSignatureCheckOUT =
	    (PVRSRV_BRIDGE_OUT_PDUMPCOMPUTECRCSIGNATURECHECK *)
	    IMG_OFFSET_ADDR(psPDumpComputeCRCSignatureCheckOUT_UI8, 0);

	psPDumpComputeCRCSignatureCheckOUT->eError =
	    PVRSRVPDumpComputeCRCSignatureCheckKM(psConnection, OSGetDevNode(psConnection),
						  psPDumpComputeCRCSignatureCheckIN->
						  ui32PDumpFlags);

	return 0;
}

#else
#define PVRSRVBridgePDumpComputeCRCSignatureCheck NULL
#endif

static IMG_INT
PVRSRVBridgePDumpCRCSignatureCheck(IMG_UINT32 ui32DispatchTableEntry,
				   IMG_UINT8 * psPDumpCRCSignatureCheckIN_UI8,
				   IMG_UINT8 * psPDumpCRCSignatureCheckOUT_UI8,
				   CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PDUMPCRCSIGNATURECHECK *psPDumpCRCSignatureCheckIN =
	    (PVRSRV_BRIDGE_IN_PDUMPCRCSIGNATURECHECK *)
	    IMG_OFFSET_ADDR(psPDumpCRCSignatureCheckIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PDUMPCRCSIGNATURECHECK *psPDumpCRCSignatureCheckOUT =
	    (PVRSRV_BRIDGE_OUT_PDUMPCRCSIGNATURECHECK *)
	    IMG_OFFSET_ADDR(psPDumpCRCSignatureCheckOUT_UI8, 0);

	psPDumpCRCSignatureCheckOUT->eError =
	    PVRSRVPDumpCRCSignatureCheckKM(psConnection, OSGetDevNode(psConnection),
					   psPDumpCRCSignatureCheckIN->ui32PDumpFlags);

	return 0;
}

static IMG_INT
PVRSRVBridgePDumpValCheckPreCommand(IMG_UINT32 ui32DispatchTableEntry,
				    IMG_UINT8 * psPDumpValCheckPreCommandIN_UI8,
				    IMG_UINT8 * psPDumpValCheckPreCommandOUT_UI8,
				    CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PDUMPVALCHECKPRECOMMAND *psPDumpValCheckPreCommandIN =
	    (PVRSRV_BRIDGE_IN_PDUMPVALCHECKPRECOMMAND *)
	    IMG_OFFSET_ADDR(psPDumpValCheckPreCommandIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PDUMPVALCHECKPRECOMMAND *psPDumpValCheckPreCommandOUT =
	    (PVRSRV_BRIDGE_OUT_PDUMPVALCHECKPRECOMMAND *)
	    IMG_OFFSET_ADDR(psPDumpValCheckPreCommandOUT_UI8, 0);

	psPDumpValCheckPreCommandOUT->eError =
	    PVRSRVPDumpValCheckPreCommandKM(psConnection, OSGetDevNode(psConnection),
					    psPDumpValCheckPreCommandIN->ui32PDumpFlags);

	return 0;
}

static IMG_INT
PVRSRVBridgePDumpValCheckPostCommand(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psPDumpValCheckPostCommandIN_UI8,
				     IMG_UINT8 * psPDumpValCheckPostCommandOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PDUMPVALCHECKPOSTCOMMAND *psPDumpValCheckPostCommandIN =
	    (PVRSRV_BRIDGE_IN_PDUMPVALCHECKPOSTCOMMAND *)
	    IMG_OFFSET_ADDR(psPDumpValCheckPostCommandIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PDUMPVALCHECKPOSTCOMMAND *psPDumpValCheckPostCommandOUT =
	    (PVRSRV_BRIDGE_OUT_PDUMPVALCHECKPOSTCOMMAND *)
	    IMG_OFFSET_ADDR(psPDumpValCheckPostCommandOUT_UI8, 0);

	psPDumpValCheckPostCommandOUT->eError =
	    PVRSRVPDumpValCheckPostCommandKM(psConnection, OSGetDevNode(psConnection),
					     psPDumpValCheckPostCommandIN->ui32PDumpFlags);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitRGXPDUMPBridge(void);
PVRSRV_ERROR DeinitRGXPDUMPBridge(void);

/*
 * Register all RGXPDUMP functions with services
 */
PVRSRV_ERROR InitRGXPDUMPBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP, PVRSRV_BRIDGE_RGXPDUMP_PDUMPTRACEBUFFER,
			      PVRSRVBridgePDumpTraceBuffer, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP, PVRSRV_BRIDGE_RGXPDUMP_PDUMPSIGNATUREBUFFER,
			      PVRSRVBridgePDumpSignatureBuffer, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP,
			      PVRSRV_BRIDGE_RGXPDUMP_PDUMPCOMPUTECRCSIGNATURECHECK,
			      PVRSRVBridgePDumpComputeCRCSignatureCheck, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP, PVRSRV_BRIDGE_RGXPDUMP_PDUMPCRCSIGNATURECHECK,
			      PVRSRVBridgePDumpCRCSignatureCheck, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP,
			      PVRSRV_BRIDGE_RGXPDUMP_PDUMPVALCHECKPRECOMMAND,
			      PVRSRVBridgePDumpValCheckPreCommand, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP,
			      PVRSRV_BRIDGE_RGXPDUMP_PDUMPVALCHECKPOSTCOMMAND,
			      PVRSRVBridgePDumpValCheckPostCommand, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxpdump functions with services
 */
PVRSRV_ERROR DeinitRGXPDUMPBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP, PVRSRV_BRIDGE_RGXPDUMP_PDUMPTRACEBUFFER);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP,
				PVRSRV_BRIDGE_RGXPDUMP_PDUMPSIGNATUREBUFFER);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP,
				PVRSRV_BRIDGE_RGXPDUMP_PDUMPCOMPUTECRCSIGNATURECHECK);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP,
				PVRSRV_BRIDGE_RGXPDUMP_PDUMPCRCSIGNATURECHECK);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP,
				PVRSRV_BRIDGE_RGXPDUMP_PDUMPVALCHECKPRECOMMAND);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP,
				PVRSRV_BRIDGE_RGXPDUMP_PDUMPVALCHECKPOSTCOMMAND);

	return PVRSRV_OK;
}
