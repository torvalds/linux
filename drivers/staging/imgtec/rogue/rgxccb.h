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
#include "rgxdefs_km.h"
#include "pvr_notifier.h"

#define MAX_CLIENT_CCB_NAME	30
#define SYNC_FLAG_MASK_ALL  IMG_UINT32_MAX

typedef struct _RGX_CLIENT_CCB_ RGX_CLIENT_CCB;

/*
	This structure is declared here as it's allocated on the heap by
	the callers
*/

typedef struct _RGX_CCB_CMD_HELPER_DATA_ {
	/* Data setup at command init time */
	RGX_CLIENT_CCB  			*psClientCCB;
	IMG_CHAR 					*pszCommandName;
	IMG_UINT32 					ui32PDumpFlags;
	
	IMG_UINT32					ui32ClientFenceCount;
	PRGXFWIF_UFO_ADDR			*pauiFenceUFOAddress;
	IMG_UINT32					*paui32FenceValue;
	IMG_UINT32					ui32ClientUpdateCount;
	PRGXFWIF_UFO_ADDR			*pauiUpdateUFOAddress;
	IMG_UINT32					*paui32UpdateValue;

	IMG_UINT32					ui32ServerSyncCount;
	IMG_UINT32					*paui32ServerSyncFlags;
	IMG_UINT32					ui32ServerSyncFlagMask;
	SERVER_SYNC_PRIMITIVE		**papsServerSyncs;
	
	RGXFWIF_CCB_CMD_TYPE		eType;
	IMG_UINT32					ui32CmdSize;
	IMG_UINT8					*pui8DMCmd;
	IMG_UINT32					ui32FenceCmdSize;
	IMG_UINT32					ui32DMCmdSize;
	IMG_UINT32					ui32UpdateCmdSize;
	IMG_UINT32					ui32UnfencedUpdateCmdSize;

	/* timestamp commands */
	PRGXFWIF_TIMESTAMP_ADDR     pPreTimestampAddr;
	IMG_UINT32                  ui32PreTimeStampCmdSize;
	PRGXFWIF_TIMESTAMP_ADDR     pPostTimestampAddr;
	IMG_UINT32                  ui32PostTimeStampCmdSize;
	PRGXFWIF_UFO_ADDR           pRMWUFOAddr;
	IMG_UINT32                  ui32RMWUFOCmdSize;

	/* Data setup at command acquire time */
	IMG_UINT8					*pui8StartPtr;
	IMG_UINT8					*pui8ServerUpdateStart;
	IMG_UINT8					*pui8ServerUnfencedUpdateStart;
	IMG_UINT8					*pui8ServerFenceStart;
	IMG_UINT32					ui32ServerFenceCount;
	IMG_UINT32					ui32ServerUpdateCount;
	IMG_UINT32					ui32ServerUnfencedUpdateCount;

	/* Job reference fields */
	IMG_UINT32					ui32ExtJobRef;
	IMG_UINT32					ui32IntJobRef;

	/* Workload kick information */
	RGXFWIF_WORKEST_KICK_DATA	*psWorkEstKickData;
} RGX_CCB_CMD_HELPER_DATA;

#define PADDING_COMMAND_SIZE	(sizeof(RGXFWIF_CCB_CMD_HEADER))


#define RGX_CCB_REQUESTORS(TYPE) \
	/* for debugging purposes */ TYPE(UNDEF)	\
	TYPE(TA)	\
	TYPE(3D)	\
	TYPE(CDM)	\
	TYPE(SH)	\
	TYPE(RS)	\
	TYPE(TQ_3D)	\
	TYPE(TQ_2D)	\
	TYPE(TQ_TDM)    \
	TYPE(KICKSYNC)	\
	/* Only used for validating the number of entries in this list */ TYPE(FIXED_COUNT)	\
	TYPE(FC0)	\
	TYPE(FC1)	\
	TYPE(FC2)	\
	TYPE(FC3)	\

/* Forms an enum constant for each type present in RGX_CCB_REQUESTORS list. The enum is mainly used as
   an index to the aszCCBRequestors table defined in rgxccb.c. The total number of enums must adhere
   to the following build assert.
*/
typedef enum _RGX_CCB_REQUESTOR_TYPE_
{
#define CONSTRUCT_ENUM(req) REQ_TYPE_##req,
	RGX_CCB_REQUESTORS (CONSTRUCT_ENUM)
#undef CONSTRUCT_ENUM
	
	/* should always be at the end */
	REQ_TYPE_TOTAL_COUNT,
} RGX_CCB_REQUESTOR_TYPE;

/*	The number of enum constants in the above table is always equal to those provided in the RGX_CCB_REQUESTORS X macro list.
	In an event of change in value of DPX_MAX_RAY_CONTEXTS to say 'n', appropriate entry/entries up to FC[n-1] must be added to
	the RGX_CCB_REQUESTORS list.
*/
static_assert(REQ_TYPE_TOTAL_COUNT == REQ_TYPE_FIXED_COUNT + DPX_MAX_RAY_CONTEXTS + 1,
			  "Mismatch between DPX_MAX_RAY_CONTEXTS and RGX_CCB_REQUESTOR_TYPE enum");

/* Tuple describing the columns of the following table */
typedef enum _RGX_CCB_REQUESTOR_TUPLE_
{
	REQ_RGX_FW_CLIENT_CCB_STRING,          /* Index to comment to be dumped in DevMemAllocs when allocating FirmwareClientCCB for this requestor */
	REQ_RGX_FW_CLIENT_CCB_CONTROL_STRING,  /* Index to comment to be dumped in DevMemAllocs when allocating FirmwareClientCCBControl for this requestor */
	REQ_PDUMP_COMMENT,                     /* Index to comment to be dumped in PDUMPs */

	/* should always be at the end */
	REQ_TUPLE_CARDINALITY,
} RGX_CCB_REQUESTOR_TUPLE;

/*	Table containing an array of strings for each requestor type in the list of RGX_CCB_REQUESTORS. In addition to its use in
	this module (rgxccb.c), this table is also used to access string to be dumped in PDUMP comments, hence, marking it extern for
	use in other modules.
*/
extern IMG_CHAR *const aszCCBRequestors[][REQ_TUPLE_CARDINALITY];

PVRSRV_ERROR RGXCCBPDumpDrainCCB(RGX_CLIENT_CCB *psClientCCB,
					IMG_UINT32 ui32PDumpFlags);

PVRSRV_ERROR RGXCreateCCB(PVRSRV_RGXDEV_INFO	*psDevInfo,
						  IMG_UINT32			ui32CCBSizeLog2,
						  CONNECTION_DATA		*psConnectionData,
						  RGX_CCB_REQUESTOR_TYPE	eCCBRequestor,
						  RGX_SERVER_COMMON_CONTEXT *psServerCommonContext,
						  RGX_CLIENT_CCB		**ppsClientCCB,
						  DEVMEM_MEMDESC 		**ppsClientCCBMemDesc,
						  DEVMEM_MEMDESC 		**ppsClientCCBCtlMemDesc);

void RGXDestroyCCB(PVRSRV_RGXDEV_INFO *psDevInfo, RGX_CLIENT_CCB *psClientCCB);

PVRSRV_ERROR RGXAcquireCCB(RGX_CLIENT_CCB *psClientCCB,
										IMG_UINT32		ui32CmdSize,
										void			**ppvBufferSpace,
										IMG_UINT32		ui32PDumpFlags);

IMG_INTERNAL void RGXReleaseCCB(RGX_CLIENT_CCB *psClientCCB,
								IMG_UINT32		ui32CmdSize,
								IMG_UINT32		ui32PDumpFlags);

IMG_UINT32 RGXGetHostWriteOffsetCCB(RGX_CLIENT_CCB *psClientCCB);

PVRSRV_ERROR RGXCmdHelperInitCmdCCB(RGX_CLIENT_CCB            *psClientCCB,
                                    IMG_UINT32                ui32ClientFenceCount,
                                    PRGXFWIF_UFO_ADDR         *pauiFenceUFOAddress,
                                    IMG_UINT32                *paui32FenceValue,
                                    IMG_UINT32                ui32ClientUpdateCount,
                                    PRGXFWIF_UFO_ADDR         *pauiUpdateUFOAddress,
                                    IMG_UINT32                *paui32UpdateValue,
                                    IMG_UINT32                ui32ServerSyncCount,
                                    IMG_UINT32                *paui32ServerSyncFlags,
                                    IMG_UINT32                ui32ServerSyncFlagMask,
                                    SERVER_SYNC_PRIMITIVE     **papsServerSyncs,
                                    IMG_UINT32                ui32CmdSize,
                                    IMG_PBYTE                 pui8DMCmd,
                                    PRGXFWIF_TIMESTAMP_ADDR   *ppPreAddr,
                                    PRGXFWIF_TIMESTAMP_ADDR   *ppPostAddr,
                                    PRGXFWIF_UFO_ADDR         *ppRMWUFOAddr,
                                    RGXFWIF_CCB_CMD_TYPE      eType,
                                    IMG_UINT32                ui32ExtJobRef,
                                    IMG_UINT32                ui32IntJobRef,
                                    IMG_UINT32                ui32PDumpFlags,
                                    RGXFWIF_WORKEST_KICK_DATA *psWorkEstKickData,
                                    IMG_CHAR                  *pszCommandName,
                                    RGX_CCB_CMD_HELPER_DATA   *psCmdHelperData);

PVRSRV_ERROR RGXCmdHelperAcquireCmdCCB(IMG_UINT32 ui32CmdCount,
									   RGX_CCB_CMD_HELPER_DATA *asCmdHelperData);

void RGXCmdHelperReleaseCmdCCB(IMG_UINT32 ui32CmdCount,
							   RGX_CCB_CMD_HELPER_DATA *asCmdHelperData,
							   const IMG_CHAR *pcszDMName,
							   IMG_UINT32 ui32CtxAddr);

IMG_UINT32 RGXCmdHelperGetCommandSize(IMG_UINT32 ui32CmdCount,
								   RGX_CCB_CMD_HELPER_DATA *asCmdHelperData);

IMG_UINT32 RGXCmdHelperGetCommandOffset(RGX_CCB_CMD_HELPER_DATA *asCmdHelperData,
                                        IMG_UINT32              ui32Cmdindex);

IMG_UINT32 RGXCmdHelperGetDMCommandHeaderOffset(RGX_CCB_CMD_HELPER_DATA *psCmdHelperData);

void DumpStalledCCBCommand(PRGXFWIF_FWCOMMONCONTEXT sFWCommonContext,
				RGX_CLIENT_CCB  *psCurrentClientCCB,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile);
#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) || defined(PVRSRV_ENABLE_FULL_CCB_DUMP)
void DumpCCB(PVRSRV_RGXDEV_INFO *psDevInfo,
			PRGXFWIF_FWCOMMONCONTEXT sFWCommonContext,
			RGX_CLIENT_CCB *psCurrentClientCCB,
			DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
			void *pvDumpDebugFile);
#endif

PVRSRV_ERROR CheckForStalledCCB(RGX_CLIENT_CCB  *psCurrentClientCCB, RGX_KICK_TYPE_DM eKickTypeDM);
#endif /* __RGXCCB_H__ */
