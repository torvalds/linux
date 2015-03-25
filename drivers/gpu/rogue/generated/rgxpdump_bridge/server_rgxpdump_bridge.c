/*************************************************************************/ /*!
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
*/ /**************************************************************************/

#include <stddef.h>
#include <asm/uaccess.h>

#include "img_defs.h"

#include "rgxpdump.h"


#include "common_rgxpdump_bridge.h"

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
PVRSRVBridgePDumpTraceBuffer(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PDUMPTRACEBUFFER *psPDumpTraceBufferIN,
					 PVRSRV_BRIDGE_OUT_PDUMPTRACEBUFFER *psPDumpTraceBufferOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXPDUMP_PDUMPTRACEBUFFER);





				{
					/* Look up the address from the handle */
					psPDumpTraceBufferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceNodeInt,
											psPDumpTraceBufferIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psPDumpTraceBufferOUT->eError != PVRSRV_OK)
					{
						goto PDumpTraceBuffer_exit;
					}

				}

	psPDumpTraceBufferOUT->eError =
		PVRSRVPDumpTraceBufferKM(
					hDeviceNodeInt,
					psPDumpTraceBufferIN->ui32PDumpFlags);



PDumpTraceBuffer_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePDumpSignatureBuffer(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PDUMPSIGNATUREBUFFER *psPDumpSignatureBufferIN,
					 PVRSRV_BRIDGE_OUT_PDUMPSIGNATUREBUFFER *psPDumpSignatureBufferOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_RGXPDUMP_PDUMPSIGNATUREBUFFER);





				{
					/* Look up the address from the handle */
					psPDumpSignatureBufferOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDeviceNodeInt,
											psPDumpSignatureBufferIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psPDumpSignatureBufferOUT->eError != PVRSRV_OK)
					{
						goto PDumpSignatureBuffer_exit;
					}

				}

	psPDumpSignatureBufferOUT->eError =
		PVRSRVPDumpSignatureBufferKM(
					hDeviceNodeInt,
					psPDumpSignatureBufferIN->ui32PDumpFlags);



PDumpSignatureBuffer_exit:

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterRGXPDUMPFunctions(IMG_VOID);
IMG_VOID UnregisterRGXPDUMPFunctions(IMG_VOID);

/*
 * Register all RGXPDUMP functions with services
 */
PVRSRV_ERROR RegisterRGXPDUMPFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP_PDUMPTRACEBUFFER, PVRSRVBridgePDumpTraceBuffer);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXPDUMP_PDUMPSIGNATUREBUFFER, PVRSRVBridgePDumpSignatureBuffer);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxpdump functions with services
 */
IMG_VOID UnregisterRGXPDUMPFunctions(IMG_VOID)
{
}
