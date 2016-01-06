/*************************************************************************/ /*!
@File
@Title          Server bridge for pdump
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for pdump
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

#include "devicemem_server.h"
#include "pdump_km.h"


#include "common_pdump_bridge.h"

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
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeDevmemPDumpBitmap(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMPDUMPBITMAP *psDevmemPDumpBitmapIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMPDUMPBITMAP *psDevmemPDumpBitmapOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDeviceNodeInt = IMG_NULL;
	IMG_CHAR *uiFileNameInt = IMG_NULL;
	DEVMEMINT_CTX * psDevmemCtxInt = IMG_NULL;




	
	{
		uiFileNameInt = OSAllocMem(PVRSRV_PDUMP_MAX_FILENAME_SIZE * sizeof(IMG_CHAR));
		if (!uiFileNameInt)
		{
			psDevmemPDumpBitmapOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DevmemPDumpBitmap_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDevmemPDumpBitmapIN->puiFileName, PVRSRV_PDUMP_MAX_FILENAME_SIZE * sizeof(IMG_CHAR))
				|| (OSCopyFromUser(NULL, uiFileNameInt, psDevmemPDumpBitmapIN->puiFileName,
				PVRSRV_PDUMP_MAX_FILENAME_SIZE * sizeof(IMG_CHAR)) != PVRSRV_OK) )
			{
				psDevmemPDumpBitmapOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DevmemPDumpBitmap_exit;
			}



				{
					/* Look up the address from the handle */
					psDevmemPDumpBitmapOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &hDeviceNodeInt,
											psDevmemPDumpBitmapIN->hDeviceNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psDevmemPDumpBitmapOUT->eError != PVRSRV_OK)
					{
						goto DevmemPDumpBitmap_exit;
					}
				}


				{
					/* Look up the address from the handle */
					psDevmemPDumpBitmapOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_VOID **) &psDevmemCtxInt,
											psDevmemPDumpBitmapIN->hDevmemCtx,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
					if(psDevmemPDumpBitmapOUT->eError != PVRSRV_OK)
					{
						goto DevmemPDumpBitmap_exit;
					}
				}


	psDevmemPDumpBitmapOUT->eError =
		DevmemIntPDumpBitmap(
					hDeviceNodeInt,
					uiFileNameInt,
					psDevmemPDumpBitmapIN->ui32FileOffset,
					psDevmemPDumpBitmapIN->ui32Width,
					psDevmemPDumpBitmapIN->ui32Height,
					psDevmemPDumpBitmapIN->ui32StrideInBytes,
					psDevmemPDumpBitmapIN->sDevBaseAddr,
					psDevmemCtxInt,
					psDevmemPDumpBitmapIN->ui32Size,
					psDevmemPDumpBitmapIN->ePixelFormat,
					psDevmemPDumpBitmapIN->ui32AddrMode,
					psDevmemPDumpBitmapIN->ui32PDumpFlags);




DevmemPDumpBitmap_exit:
	if (uiFileNameInt)
		OSFreeMem(uiFileNameInt);

	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpComment(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PVRSRVPDUMPCOMMENT *psPVRSRVPDumpCommentIN,
					  PVRSRV_BRIDGE_OUT_PVRSRVPDUMPCOMMENT *psPVRSRVPDumpCommentOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_CHAR *uiCommentInt = IMG_NULL;

	PVR_UNREFERENCED_PARAMETER(psConnection);



	
	{
		uiCommentInt = OSAllocMem(PVRSRV_PDUMP_MAX_COMMENT_SIZE * sizeof(IMG_CHAR));
		if (!uiCommentInt)
		{
			psPVRSRVPDumpCommentOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto PVRSRVPDumpComment_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPVRSRVPDumpCommentIN->puiComment, PVRSRV_PDUMP_MAX_COMMENT_SIZE * sizeof(IMG_CHAR))
				|| (OSCopyFromUser(NULL, uiCommentInt, psPVRSRVPDumpCommentIN->puiComment,
				PVRSRV_PDUMP_MAX_COMMENT_SIZE * sizeof(IMG_CHAR)) != PVRSRV_OK) )
			{
				psPVRSRVPDumpCommentOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto PVRSRVPDumpComment_exit;
			}



	psPVRSRVPDumpCommentOUT->eError =
		PDumpCommentKM(
					uiCommentInt,
					psPVRSRVPDumpCommentIN->ui32Flags);




PVRSRVPDumpComment_exit:
	if (uiCommentInt)
		OSFreeMem(uiCommentInt);

	return 0;
}

static IMG_INT
PVRSRVBridgePVRSRVPDumpSetFrame(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PVRSRVPDUMPSETFRAME *psPVRSRVPDumpSetFrameIN,
					  PVRSRV_BRIDGE_OUT_PVRSRVPDUMPSETFRAME *psPVRSRVPDumpSetFrameOUT,
					 CONNECTION_DATA *psConnection)
{







	psPVRSRVPDumpSetFrameOUT->eError =
		PDumpSetFrameKM(psConnection,
					psPVRSRVPDumpSetFrameIN->ui32Frame);





	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */


PVRSRV_ERROR InitPDUMPBridge(IMG_VOID);
PVRSRV_ERROR DeinitPDUMPBridge(IMG_VOID);

/*
 * Register all PDUMP functions with services
 */
PVRSRV_ERROR InitPDUMPBridge(IMG_VOID)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP, PVRSRV_BRIDGE_PDUMP_DEVMEMPDUMPBITMAP, PVRSRVBridgeDevmemPDumpBitmap,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP, PVRSRV_BRIDGE_PDUMP_PVRSRVPDUMPCOMMENT, PVRSRVBridgePVRSRVPDumpComment,
					IMG_NULL, IMG_NULL,
					0, 0);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP, PVRSRV_BRIDGE_PDUMP_PVRSRVPDUMPSETFRAME, PVRSRVBridgePVRSRVPDumpSetFrame,
					IMG_NULL, IMG_NULL,
					0, 0);


	return PVRSRV_OK;
}

/*
 * Unregister all pdump functions with services
 */
PVRSRV_ERROR DeinitPDUMPBridge(IMG_VOID)
{
	return PVRSRV_OK;
}

