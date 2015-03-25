/*************************************************************************/ /*!
@File
@Title          RGX Circular Command Buffer functionality.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX Circular Command Buffer functionality.
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

#if !defined(__RGXCCB_H__)
#define __RGXCCB_H__

#include "devicemem.h"
#include "device.h"
#include "rgxdevice.h"
#include "sync_server.h"
#include "connection_server.h"
#include "rgx_fwif_shared.h"
#include "rgxdebug.h"

#define MAX_CLIENT_CCB_NAME	30

typedef struct _RGX_CLIENT_CCB_ RGX_CLIENT_CCB;

/*
	This structure is declared here as it's allocated on the heap by
	the callers
*/

typedef struct _RGX_CCB_CMD_HELPER_DATA_ {
	/* Data setup at command init time */
	RGX_CLIENT_CCB  		*psClientCCB;
	IMG_CHAR 				*pszCommandName;
	IMG_BOOL 				bPDumpContinuous;
	
	IMG_UINT32				ui32ClientFenceCount;
	PRGXFWIF_UFO_ADDR		*pauiFenceUFOAddress;
	IMG_UINT32				*paui32FenceValue;
	IMG_UINT32				ui32ClientUpdateCount;
	PRGXFWIF_UFO_ADDR		*pauiUpdateUFOAddress;
	IMG_UINT32				*paui32UpdateValue;

	IMG_UINT32				ui32ServerSyncCount;
	IMG_UINT32				*paui32ServerSyncFlags;
	SERVER_SYNC_PRIMITIVE	**papsServerSyncs;

	RGXFWIF_KCCB_CMD_TYPE	eType;
	IMG_UINT32				ui32CmdSize;
	IMG_UINT8				*pui8DMCmd;
	IMG_UINT32				ui32FenceCmdSize;
	IMG_UINT32				ui32DMCmdSize;
	IMG_UINT32				ui32UpdateCmdSize;

	/* timestamp commands */
	PRGXFWIF_TIMESTAMP_ADDR pPreTimestampAddr;
	IMG_UINT32              ui32PreTimeStampCmdSize;
	PRGXFWIF_TIMESTAMP_ADDR pPostTimestampAddr;
	IMG_UINT32              ui32PostTimeStampCmdSize;
	PRGXFWIF_UFO_ADDR       pRMWUFOAddr;
	IMG_UINT32              ui32RMWUFOCmdSize;

	/* Data setup at command acquire time */
	IMG_UINT8				*pui8StartPtr;
	IMG_UINT8				*pui8ServerUpdateStart;
	IMG_UINT8				*pui8ServerFenceStart;
	IMG_UINT32				ui32ServerFenceCount;
	IMG_UINT32				ui32ServerUpdateCount;

} RGX_CCB_CMD_HELPER_DATA;

#define PADDING_COMMAND_SIZE	(sizeof(RGXFWIF_CCB_CMD_HEADER))

PVRSRV_ERROR RGXCreateCCB(PVRSRV_DEVICE_NODE	*psDeviceNode,
						  IMG_UINT32			ui32CCBSizeLog2,
						  CONNECTION_DATA		*psConnectionData,
						  const IMG_CHAR		*pszName,
						  RGX_SERVER_COMMON_CONTEXT *psServerCommonContext,
						  RGX_CLIENT_CCB		**ppsClientCCB,
						  DEVMEM_MEMDESC 		**ppsClientCCBMemDesc,
						  DEVMEM_MEMDESC 		**ppsClientCCBCtlMemDesc);

IMG_VOID RGXDestroyCCB(RGX_CLIENT_CCB *psClientCCB);

PVRSRV_ERROR RGXAcquireCCB(RGX_CLIENT_CCB *psClientCCB,
										IMG_UINT32		ui32CmdSize,
										IMG_PVOID		*ppvBufferSpace,
										IMG_BOOL		bPDumpContinuous);

IMG_INTERNAL IMG_VOID RGXReleaseCCB(RGX_CLIENT_CCB *psClientCCB,
									IMG_UINT32		ui32CmdSize,
									IMG_BOOL		bPDumpContinuous);

IMG_UINT32 RGXGetHostWriteOffsetCCB(RGX_CLIENT_CCB *psClientCCB);

PVRSRV_ERROR RGXCmdHelperInitCmdCCB(RGX_CLIENT_CCB       *psClientCCB,
                                    IMG_UINT32           ui32ClientFenceCount,
                                    PRGXFWIF_UFO_ADDR    *pauiFenceUFOAddress,
                                    IMG_UINT32           *paui32FenceValue,
                                    IMG_UINT32           ui32ClientUpdateCount,
                                    PRGXFWIF_UFO_ADDR    *pauiUpdateUFOAddress,
                                    IMG_UINT32           *paui32UpdateValue,
                                    IMG_UINT32           ui32ServerSyncCount,
                                    IMG_UINT32           *paui32ServerSyncFlags,
                                    SERVER_SYNC_PRIMITIVE **pasServerSyncs,
                                    IMG_UINT32           ui32CmdSize,
                                    IMG_UINT8            *pui8DMCmd,
                                    PRGXFWIF_TIMESTAMP_ADDR * ppPreAddr,
                                    PRGXFWIF_TIMESTAMP_ADDR * ppPostAddr,
                                    RGXFWIF_DEV_VIRTADDR    * ppRMWUFOAddr,
                                    RGXFWIF_CCB_CMD_TYPE eType,
                                    IMG_BOOL             bPDumpContinuous,
                                    IMG_CHAR             *pszCommandName,
                                    RGX_CCB_CMD_HELPER_DATA *psCmdHelperData);

PVRSRV_ERROR RGXCmdHelperAcquireCmdCCB(IMG_UINT32 ui32CmdCount,
									   RGX_CCB_CMD_HELPER_DATA *asCmdHelperData,
									   IMG_BOOL *pbKickRequired);

IMG_VOID RGXCmdHelperReleaseCmdCCB(IMG_UINT32 ui32CmdCount,
								   RGX_CCB_CMD_HELPER_DATA *asCmdHelperData,
								   const IMG_CHAR *pcszDMName,
								   IMG_UINT32 ui32CtxAddr);
								   
IMG_UINT32 RGXCmdHelperGetCommandSize(IMG_UINT32 ui32CmdCount,
								   RGX_CCB_CMD_HELPER_DATA *asCmdHelperData);

IMG_VOID DumpStalledCCBCommand(PRGXFWIF_FWCOMMONCONTEXT sFWCommonContext, RGX_CLIENT_CCB  *psCurrentClientCCB, DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf);

PVRSRV_ERROR CheckForStalledCCB(RGX_CLIENT_CCB  *psCurrentClientCCB);
#endif /* __RGXCCB_H__ */
