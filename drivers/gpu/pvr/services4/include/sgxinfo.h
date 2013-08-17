/*************************************************************************/ /*!
@Title          sgx services structures/functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    inline functions/structures shared across UM and KM services components
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
#if !defined (__SGXINFO_H__)
#define __SGXINFO_H__

#include "sgxscript.h"
#include "servicesint.h"
#include "services.h"
#include "sgxapi_km.h"
#include "sgx_mkif_km.h"


#define SGX_MAX_DEV_DATA			24
#define	SGX_MAX_INIT_MEM_HANDLES	18


typedef struct _SGX_BRIDGE_INFO_FOR_SRVINIT
{
	IMG_DEV_PHYADDR sPDDevPAddr;
	PVRSRV_HEAP_INFO asHeapInfo[PVRSRV_MAX_CLIENT_HEAPS];
} SGX_BRIDGE_INFO_FOR_SRVINIT;


typedef enum _SGXMKIF_CMD_TYPE_
{
	SGXMKIF_CMD_TA				= 0,
	SGXMKIF_CMD_TRANSFER		= 1,
	SGXMKIF_CMD_2D				= 2,
	SGXMKIF_CMD_POWER			= 3,
	SGXMKIF_CMD_CONTEXTSUSPEND	= 4,
	SGXMKIF_CMD_CLEANUP			= 5,
	SGXMKIF_CMD_GETMISCINFO		= 6,
	SGXMKIF_CMD_PROCESS_QUEUES	= 7,
	SGXMKIF_CMD_DATABREAKPOINT	= 8,
	SGXMKIF_CMD_SETHWPERFSTATUS	= 9,
 	SGXMKIF_CMD_FLUSHPDCACHE	= 10,
 	SGXMKIF_CMD_MAX				= 11,

	SGXMKIF_CMD_FORCE_I32   	= -1,

} SGXMKIF_CMD_TYPE;


typedef struct _SGX_BRIDGE_INIT_INFO_
{
	IMG_HANDLE	hKernelCCBMemInfo;
	IMG_HANDLE	hKernelCCBCtlMemInfo;
	IMG_HANDLE	hKernelCCBEventKickerMemInfo;
	IMG_HANDLE	hKernelSGXHostCtlMemInfo;
	IMG_HANDLE	hKernelSGXTA3DCtlMemInfo;
#if defined(FIX_HW_BRN_31272) || defined(FIX_HW_BRN_31780) || defined(FIX_HW_BRN_33920)
	IMG_HANDLE	hKernelSGXPTLAWriteBackMemInfo;
#endif
	IMG_HANDLE	hKernelSGXMiscMemInfo;

	IMG_UINT32	aui32HostKickAddr[SGXMKIF_CMD_MAX];

	SGX_INIT_SCRIPTS sScripts;

	IMG_UINT32	ui32ClientBuildOptions;
	SGX_MISCINFO_STRUCT_SIZES	sSGXStructSizes;

#if defined(SGX_SUPPORT_HWPROFILING)
	IMG_HANDLE	hKernelHWProfilingMemInfo;
#endif
#if defined(SUPPORT_SGX_HWPERF)
	IMG_HANDLE	hKernelHWPerfCBMemInfo;
#endif
	IMG_HANDLE	hKernelTASigBufferMemInfo;
	IMG_HANDLE	hKernel3DSigBufferMemInfo;


#if defined(FIX_HW_BRN_31542) || defined(FIX_HW_BRN_36513)
	IMG_HANDLE hKernelClearClipWAVDMStreamMemInfo;
	IMG_HANDLE hKernelClearClipWAIndexStreamMemInfo;
	IMG_HANDLE hKernelClearClipWAPDSMemInfo;
	IMG_HANDLE hKernelClearClipWAUSEMemInfo;
	IMG_HANDLE hKernelClearClipWAParamMemInfo;
	IMG_HANDLE hKernelClearClipWAPMPTMemInfo;
	IMG_HANDLE hKernelClearClipWATPCMemInfo;
	IMG_HANDLE hKernelClearClipWAPSGRgnHdrMemInfo;
#endif

#if defined(SGX_FEATURE_VDM_CONTEXT_SWITCH) && defined(FIX_HW_BRN_31559)
	IMG_HANDLE	hKernelVDMSnapShotBufferMemInfo;
	IMG_HANDLE	hKernelVDMCtrlStreamBufferMemInfo;
#endif
#if defined(SGX_FEATURE_VDM_CONTEXT_SWITCH) && \
	defined(FIX_HW_BRN_33657) && defined(SUPPORT_SECURE_33657_FIX)
	IMG_HANDLE	hKernelVDMStateUpdateBufferMemInfo;
#endif
#if defined(PVRSRV_USSE_EDM_STATUS_DEBUG)
	IMG_HANDLE	hKernelEDMStatusBufferMemInfo;
#endif

	IMG_UINT32 ui32EDMTaskReg0;
	IMG_UINT32 ui32EDMTaskReg1;

	IMG_UINT32 ui32ClkGateCtl;
	IMG_UINT32 ui32ClkGateCtl2;
	IMG_UINT32 ui32ClkGateStatusReg;
	IMG_UINT32 ui32ClkGateStatusMask;
#if defined(SGX_FEATURE_MP)
	IMG_UINT32 ui32MasterClkGateStatusReg;
	IMG_UINT32 ui32MasterClkGateStatusMask;
	IMG_UINT32 ui32MasterClkGateStatus2Reg;
	IMG_UINT32 ui32MasterClkGateStatus2Mask;
#endif /* SGX_FEATURE_MP */

	IMG_UINT32 ui32CacheControl;

	IMG_UINT32	asInitDevData[SGX_MAX_DEV_DATA];
	IMG_HANDLE	asInitMemHandles[SGX_MAX_INIT_MEM_HANDLES];

} SGX_BRIDGE_INIT_INFO;


typedef struct _SGX_DEVICE_SYNC_LIST_
{
	PSGXMKIF_HWDEVICE_SYNC_LIST	psHWDeviceSyncList;

	IMG_HANDLE				hKernelHWSyncListMemInfo;
	PVRSRV_CLIENT_MEM_INFO	*psHWDeviceSyncListClientMemInfo;
	PVRSRV_CLIENT_MEM_INFO	*psAccessResourceClientMemInfo;

	volatile IMG_UINT32		*pui32Lock;

	struct _SGX_DEVICE_SYNC_LIST_	*psNext;

	/* Must be the last variable in the structure */
	IMG_UINT32			ui32NumSyncObjects;
	IMG_HANDLE			ahSyncHandles[1];
} SGX_DEVICE_SYNC_LIST, *PSGX_DEVICE_SYNC_LIST;


typedef struct _SGX_INTERNEL_STATUS_UPDATE_
{
	CTL_STATUS				sCtlStatus;
	IMG_HANDLE				hKernelMemInfo;
} SGX_INTERNEL_STATUS_UPDATE;


typedef struct _SGX_CCB_KICK_
{
	SGXMKIF_COMMAND		sCommand;
	IMG_HANDLE	hCCBKernelMemInfo;

	IMG_UINT32	ui32NumDstSyncObjects;
	IMG_HANDLE	hKernelHWSyncListMemInfo;

	/* DST syncs */
	IMG_HANDLE	*pahDstSyncHandles;

	IMG_UINT32	ui32NumTAStatusVals;
	IMG_UINT32	ui32Num3DStatusVals;

#if defined(SUPPORT_SGX_NEW_STATUS_VALS)
	SGX_INTERNEL_STATUS_UPDATE	asTAStatusUpdate[SGX_MAX_TA_STATUS_VALS];
	SGX_INTERNEL_STATUS_UPDATE	as3DStatusUpdate[SGX_MAX_3D_STATUS_VALS];
#else
	IMG_HANDLE	ahTAStatusSyncInfo[SGX_MAX_TA_STATUS_VALS];
	IMG_HANDLE	ah3DStatusSyncInfo[SGX_MAX_3D_STATUS_VALS];
#endif

	IMG_BOOL	bFirstKickOrResume;
#if defined(NO_HARDWARE) || defined(PDUMP)
	IMG_BOOL	bTerminateOrAbort;
#endif
	IMG_BOOL	bLastInScene;

	/* CCB offset of data structure associated with this kick */
	IMG_UINT32	ui32CCBOffset;

#if defined(SUPPORT_SGX_GENERALISED_SYNCOBJECTS)
	/* SRC and DST syncs */
	IMG_UINT32	ui32NumTASrcSyncs;
	IMG_HANDLE	ahTASrcKernelSyncInfo[SGX_MAX_TA_SRC_SYNCS];
	IMG_UINT32	ui32NumTADstSyncs;
	IMG_HANDLE	ahTADstKernelSyncInfo[SGX_MAX_TA_DST_SYNCS];
	IMG_UINT32	ui32Num3DSrcSyncs;
	IMG_HANDLE	ah3DSrcKernelSyncInfo[SGX_MAX_3D_SRC_SYNCS];
#else
	/* SRC syncs */
	IMG_UINT32	ui32NumSrcSyncs;
	IMG_HANDLE	ahSrcKernelSyncInfo[SGX_MAX_SRC_SYNCS_TA];
#endif

	/* TA/3D dependency data */
	IMG_BOOL	bTADependency;
	IMG_HANDLE	hTA3DSyncInfo;

	IMG_HANDLE	hTASyncInfo;
	IMG_HANDLE	h3DSyncInfo;
#if defined(PDUMP)
	IMG_UINT32	ui32CCBDumpWOff;
#endif
#if defined(NO_HARDWARE)
	IMG_UINT32	ui32WriteOpsPendingVal;
#endif
	IMG_HANDLE	hDevMemContext;
} SGX_CCB_KICK;


/*!
 ******************************************************************************
 * shared client/kernel device information structure for SGX
 *****************************************************************************/
#define SGX_KERNEL_USE_CODE_BASE_INDEX		15


/*!
 ******************************************************************************
 * Client device information structure for SGX
 *****************************************************************************/
typedef struct _SGX_CLIENT_INFO_
{
	IMG_UINT32					ui32ProcessID;			/*!< ID of process controlling SGX device */
	IMG_VOID					*pvProcess;				/*!< pointer to OS specific 'process' structure */
	PVRSRV_MISC_INFO			sMiscInfo;				/*!< Misc. Information, inc. SOC specifics */

	IMG_UINT32					asDevData[SGX_MAX_DEV_DATA];

} SGX_CLIENT_INFO;

/*!
 ******************************************************************************
 * Internal device information structure for SGX
 *****************************************************************************/
typedef struct _SGX_INTERNAL_DEVINFO_
{
	IMG_UINT32			ui32Flags;
	IMG_HANDLE			hHostCtlKernelMemInfoHandle;
	IMG_BOOL			bForcePTOff;
} SGX_INTERNAL_DEVINFO;


typedef struct _SGX_INTERNAL_DEVINFO_KM_
{
	IMG_UINT32			ui32Flags;
	IMG_HANDLE			hHostCtlKernelMemInfoHandle;
	IMG_BOOL			bForcePTOff;
} SGX_INTERNAL_DEVINFO_KM;


#if defined(TRANSFER_QUEUE)
typedef struct _PVRSRV_TRANSFER_SGX_KICK_
{
	IMG_HANDLE		hCCBMemInfo;
	IMG_UINT32		ui32SharedCmdCCBOffset;

	IMG_DEV_VIRTADDR 	sHWTransferContextDevVAddr;

	IMG_HANDLE		hTASyncInfo;
	IMG_HANDLE		h3DSyncInfo;

	IMG_UINT32		ui32NumSrcSync;
	IMG_HANDLE		ahSrcSyncInfo[SGX_MAX_TRANSFER_SYNC_OPS];

	IMG_UINT32		ui32NumDstSync;
	IMG_HANDLE		ahDstSyncInfo[SGX_MAX_TRANSFER_SYNC_OPS];

	IMG_UINT32		ui32Flags;

	IMG_UINT32		ui32PDumpFlags;
#if defined(PDUMP)
	IMG_UINT32		ui32CCBDumpWOff;
#endif
	IMG_HANDLE		hDevMemContext;
} PVRSRV_TRANSFER_SGX_KICK, *PPVRSRV_TRANSFER_SGX_KICK;

#if defined(SGX_FEATURE_2D_HARDWARE)
typedef struct _PVRSRV_2D_SGX_KICK_
{
	IMG_HANDLE		hCCBMemInfo;
	IMG_UINT32		ui32SharedCmdCCBOffset;

	IMG_DEV_VIRTADDR 	sHW2DContextDevVAddr;

	IMG_UINT32		ui32NumSrcSync;
	IMG_HANDLE		ahSrcSyncInfo[SGX_MAX_2D_SRC_SYNC_OPS];

	/* need to be able to check reads and writes on dest, and update writes */
	IMG_HANDLE 		hDstSyncInfo;

	/* need to be able to check reads and writes on TA ops, and update writes */
	IMG_HANDLE		hTASyncInfo;

	/* need to be able to check reads and writes on 2D ops, and update writes */
	IMG_HANDLE		h3DSyncInfo;

	IMG_UINT32		ui32PDumpFlags;
#if defined(PDUMP)
	IMG_UINT32		ui32CCBDumpWOff;
#endif
	IMG_HANDLE		hDevMemContext;
} PVRSRV_2D_SGX_KICK, *PPVRSRV_2D_SGX_KICK;
#endif	/* defined(SGX_FEATURE_2D_HARDWARE) */
#endif	/* defined(TRANSFER_QUEUE) */


#endif /* __SGXINFO_H__ */
/******************************************************************************
 End of file (sgxinfo.h)
******************************************************************************/
