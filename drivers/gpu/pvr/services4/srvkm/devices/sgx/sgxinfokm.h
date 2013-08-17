/*************************************************************************/ /*!
@Title          SGX kernel services structues/functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Structures and inline functions for KM services component
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
#ifndef __SGXINFOKM_H__
#define __SGXINFOKM_H__

#include "sgxdefs.h"
#include "device.h"
#include "power.h"
#include "sysconfig.h"
#include "sgxscript.h"
#include "sgxinfo.h"

#if defined (__cplusplus)
extern "C" {
#endif

/****************************************************************************/
/* kernel only defines: 													*/
/****************************************************************************/
/* SGXDeviceMap Flag defines */
#define		SGX_HOSTPORT_PRESENT			0x00000001UL


/*
	SGX PDUMP register bank name (prefix)
*/
#define SGX_PDUMPREG_NAME		"SGXREG"

/****************************************************************************/
/* kernel only structures: 													*/
/****************************************************************************/

/*Forward declaration*/
typedef struct _PVRSRV_STUB_PBDESC_ PVRSRV_STUB_PBDESC;


typedef struct _PVRSRV_SGX_CCB_INFO_ *PPVRSRV_SGX_CCB_INFO;

typedef struct _PVRSRV_SGXDEV_INFO_
{
	PVRSRV_DEVICE_TYPE		eDeviceType;
	PVRSRV_DEVICE_CLASS		eDeviceClass;

	IMG_UINT8				ui8VersionMajor;
	IMG_UINT8				ui8VersionMinor;
	IMG_UINT32				ui32CoreConfig;
	IMG_UINT32				ui32CoreFlags;

	/* Kernel mode linear address of device registers */
	IMG_PVOID				pvRegsBaseKM;

#if defined(SGX_FEATURE_HOST_PORT)
	/* Kernel mode linear address of host port */
	IMG_PVOID				pvHostPortBaseKM;
	/* HP size */
	IMG_UINT32				ui32HPSize;
	/* HP syspaddr */
	IMG_SYS_PHYADDR			sHPSysPAddr;
#endif

	/* FIXME: The alloc for this should go through OSAllocMem in future */
	IMG_HANDLE				hRegMapping;

	/* System physical address of device registers*/
	IMG_SYS_PHYADDR			sRegsPhysBase;
	/*  Register region size in bytes */
	IMG_UINT32				ui32RegSize;

#if defined(SUPPORT_EXTERNAL_SYSTEM_CACHE)
	/* external system cache register region size in bytes */
	IMG_UINT32				ui32ExtSysCacheRegsSize;
	/* external system cache register device relative physical address */
	IMG_DEV_PHYADDR			sExtSysCacheRegsDevPBase;
	/* ptr to page table  */
	IMG_UINT32				*pui32ExtSystemCacheRegsPT;
	/* handle to page table alloc/mapping */
	IMG_HANDLE				hExtSystemCacheRegsPTPageOSMemHandle;
	/* sys phys addr of PT */
	IMG_SYS_PHYADDR			sExtSystemCacheRegsPTSysPAddr;
#endif

	/*  SGX clock speed */
	IMG_UINT32				ui32CoreClockSpeed;
	IMG_UINT32				ui32uKernelTimerClock;
	IMG_BOOL				bSGXIdle;

	PVRSRV_STUB_PBDESC		*psStubPBDescListKM;


	/* kernel memory context info */
	IMG_DEV_PHYADDR			sKernelPDDevPAddr;

	IMG_UINT32				ui32HeapCount;			/*!< heap count */
	IMG_VOID				*pvDeviceMemoryHeap;
	PPVRSRV_KERNEL_MEM_INFO	psKernelCCBMemInfo;			/*!< meminfo for CCB in device accessible memory */
	PVRSRV_SGX_KERNEL_CCB	*psKernelCCB;			/*!< kernel mode linear address of CCB in device accessible memory */
	PPVRSRV_SGX_CCB_INFO	psKernelCCBInfo;		/*!< CCB information structure */
	PPVRSRV_KERNEL_MEM_INFO	psKernelCCBCtlMemInfo;	/*!< meminfo for CCB control in device accessible memory */
	PVRSRV_SGX_CCB_CTL		*psKernelCCBCtl;		/*!< kernel mode linear address of CCB control in device accessible memory */
	PPVRSRV_KERNEL_MEM_INFO psKernelCCBEventKickerMemInfo; /*!< meminfo for kernel CCB event kicker */
	IMG_UINT32				*pui32KernelCCBEventKicker; /*!< kernel mode linear address of kernel CCB event kicker */
#if defined(PDUMP)
	IMG_UINT32				ui32KernelCCBEventKickerDumpVal; /*!< pdump copy of the kernel CCB event kicker */
#endif /* PDUMP */
 	PVRSRV_KERNEL_MEM_INFO	*psKernelSGXMiscMemInfo;	/*!< kernel mode linear address of SGX misc info buffer */
	IMG_UINT32				aui32HostKickAddr[SGXMKIF_CMD_MAX];		/*!< ukernel host kick offests */
#if defined(SGX_SUPPORT_HWPROFILING)
	PPVRSRV_KERNEL_MEM_INFO psKernelHWProfilingMemInfo;
#endif
	PPVRSRV_KERNEL_MEM_INFO		psKernelHWPerfCBMemInfo;		/*!< Meminfo for hardware performace circular buffer */
	PPVRSRV_KERNEL_MEM_INFO		psKernelTASigBufferMemInfo;		/*!< Meminfo for TA signature buffer */
	PPVRSRV_KERNEL_MEM_INFO		psKernel3DSigBufferMemInfo;		/*!< Meminfo for 3D signature buffer */
#if defined(SGX_FEATURE_VDM_CONTEXT_SWITCH) && defined(FIX_HW_BRN_31559)
	PPVRSRV_KERNEL_MEM_INFO	psKernelVDMSnapShotBufferMemInfo; /*!< Meminfo for dummy snapshot buffer */
	PPVRSRV_KERNEL_MEM_INFO	psKernelVDMCtrlStreamBufferMemInfo; /*!< Meminfo for dummy control stream */
#endif
#if defined(SGX_FEATURE_VDM_CONTEXT_SWITCH) && \
	defined(FIX_HW_BRN_33657) && defined(SUPPORT_SECURE_33657_FIX)
	PPVRSRV_KERNEL_MEM_INFO	psKernelVDMStateUpdateBufferMemInfo; /*!< Meminfo for state update buffer */
#endif
#if defined(PVRSRV_USSE_EDM_STATUS_DEBUG)
	PPVRSRV_KERNEL_MEM_INFO	psKernelEDMStatusBufferMemInfo; /*!< Meminfo for EDM status buffer */
#endif
	/* Client reference count */
	IMG_UINT32				ui32ClientRefCount;

	/* cache control word for micro kernel cache flush/invalidates */
	IMG_UINT32				ui32CacheControl;

	/* client-side build options */
	IMG_UINT32				ui32ClientBuildOptions;

	/* client-side microkernel structure sizes */
	SGX_MISCINFO_STRUCT_SIZES	sSGXStructSizes;

	/*
		if we don't preallocate the pagetables we must 
		insert newly allocated page tables dynamically 
	*/
	IMG_VOID				*pvMMUContextList;

	/* Copy of registry ForcePTOff entry */
	IMG_BOOL				bForcePTOff;

	IMG_UINT32				ui32EDMTaskReg0;
	IMG_UINT32				ui32EDMTaskReg1;

	IMG_UINT32				ui32ClkGateCtl;
	IMG_UINT32				ui32ClkGateCtl2;
	IMG_UINT32				ui32ClkGateStatusReg;
	IMG_UINT32				ui32ClkGateStatusMask;
#if defined(SGX_FEATURE_MP)
	IMG_UINT32				ui32MasterClkGateStatusReg;
	IMG_UINT32				ui32MasterClkGateStatusMask;
	IMG_UINT32				ui32MasterClkGateStatus2Reg;
	IMG_UINT32				ui32MasterClkGateStatus2Mask;
#endif /* SGX_FEATURE_MP */
	SGX_INIT_SCRIPTS		sScripts;

	/* Members associated with dummy PD needed for BIF reset */
	IMG_HANDLE 				hBIFResetPDOSMemHandle;
	IMG_DEV_PHYADDR 		sBIFResetPDDevPAddr;
	IMG_DEV_PHYADDR 		sBIFResetPTDevPAddr;
	IMG_DEV_PHYADDR 		sBIFResetPageDevPAddr;
	IMG_UINT32				*pui32BIFResetPD;
	IMG_UINT32				*pui32BIFResetPT;


#if defined(SUPPORT_HW_RECOVERY)
	/* Timeout callback handle */
	IMG_HANDLE				hTimer;
	/* HW recovery Time stamp */
	IMG_UINT32				ui32TimeStamp;
#endif

	/* Number of SGX resets */
	IMG_UINT32				ui32NumResets;

	/* host control */
	PVRSRV_KERNEL_MEM_INFO			*psKernelSGXHostCtlMemInfo;
	SGXMKIF_HOST_CTL				*psSGXHostCtl;

	/* TA/3D control */
	PVRSRV_KERNEL_MEM_INFO			*psKernelSGXTA3DCtlMemInfo;

#if defined(FIX_HW_BRN_31272) || defined(FIX_HW_BRN_31780) || defined(FIX_HW_BRN_33920)
	PVRSRV_KERNEL_MEM_INFO			*psKernelSGXPTLAWriteBackMemInfo;
#endif

	IMG_UINT32				ui32Flags;

	/* memory tiling range usage */
	IMG_UINT32				ui32MemTilingUsage;

	#if defined(PDUMP)
	PVRSRV_SGX_PDUMP_CONTEXT	sPDContext;
	#endif

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
	/* SGX MMU dummy page details */
	IMG_VOID				*pvDummyPTPageCpuVAddr;
	IMG_DEV_PHYADDR			sDummyPTDevPAddr;
	IMG_HANDLE				hDummyPTPageOSMemHandle;
	IMG_VOID				*pvDummyDataPageCpuVAddr;
	IMG_DEV_PHYADDR 		sDummyDataDevPAddr;
	IMG_HANDLE				hDummyDataPageOSMemHandle;
#endif
#if defined(PDUMP)
	PDUMP_MMU_ATTRIB sMMUAttrib;
#endif
	IMG_UINT32				asSGXDevData[SGX_MAX_DEV_DATA];

#if defined(FIX_HW_BRN_31620)
	/* Dummy page refs */
	IMG_VOID			*pvBRN31620DummyPageCpuVAddr;
	IMG_HANDLE			hBRN31620DummyPageOSMemHandle;
	IMG_DEV_PHYADDR			sBRN31620DummyPageDevPAddr;

	/* Dummy PT refs */
	IMG_VOID			*pvBRN31620DummyPTCpuVAddr;
	IMG_HANDLE			hBRN31620DummyPTOSMemHandle;
	IMG_DEV_PHYADDR			sBRN31620DummyPTDevPAddr;

	IMG_HANDLE			hKernelMMUContext;
#endif

} PVRSRV_SGXDEV_INFO;


typedef struct _SGX_TIMING_INFORMATION_
{
	IMG_UINT32			ui32CoreClockSpeed;
	IMG_UINT32			ui32HWRecoveryFreq;
	IMG_BOOL			bEnableActivePM;
	IMG_UINT32			ui32ActivePowManLatencyms;
	IMG_UINT32			ui32uKernelFreq;
} SGX_TIMING_INFORMATION;

/* FIXME Rename this structure to sg more generalised as it's been extended*/
/* SGX device map */
typedef struct _SGX_DEVICE_MAP_
{
	IMG_UINT32				ui32Flags;

	/* Registers */
	IMG_SYS_PHYADDR			sRegsSysPBase;
	IMG_CPU_PHYADDR			sRegsCpuPBase;
	IMG_CPU_VIRTADDR		pvRegsCpuVBase;
	IMG_UINT32				ui32RegsSize;

#if defined(SGX_FEATURE_HOST_PORT)
	IMG_SYS_PHYADDR			sHPSysPBase;
	IMG_CPU_PHYADDR			sHPCpuPBase;
	IMG_UINT32				ui32HPSize;
#endif

	/* Local Device Memory Region: (if present) */
	IMG_SYS_PHYADDR			sLocalMemSysPBase;
	IMG_DEV_PHYADDR			sLocalMemDevPBase;
	IMG_CPU_PHYADDR			sLocalMemCpuPBase;
	IMG_UINT32				ui32LocalMemSize;

#if defined(SUPPORT_EXTERNAL_SYSTEM_CACHE)
	IMG_UINT32				ui32ExtSysCacheRegsSize;
	IMG_DEV_PHYADDR			sExtSysCacheRegsDevPBase;
#endif

	/* device interrupt IRQ */
	IMG_UINT32				ui32IRQ;

#if !defined(SGX_DYNAMIC_TIMING_INFO)
	/* timing information*/
	SGX_TIMING_INFORMATION	sTimingInfo;
#endif
#if defined(PDUMP)
	/* pdump memory region name */
	IMG_CHAR				*pszPDumpDevName;
#endif
} SGX_DEVICE_MAP;


struct _PVRSRV_STUB_PBDESC_
{
	IMG_UINT32		ui32RefCount;
	IMG_UINT32		ui32TotalPBSize;
	PVRSRV_KERNEL_MEM_INFO  *psSharedPBDescKernelMemInfo;
	PVRSRV_KERNEL_MEM_INFO  *psHWPBDescKernelMemInfo;
	PVRSRV_KERNEL_MEM_INFO	**ppsSubKernelMemInfos;
	IMG_UINT32		ui32SubKernelMemInfosCount;
	IMG_HANDLE		hDevCookie;
	PVRSRV_KERNEL_MEM_INFO  *psBlockKernelMemInfo;
	PVRSRV_KERNEL_MEM_INFO  *psHWBlockKernelMemInfo;
	IMG_DEV_VIRTADDR	sHWPBDescDevVAddr;
	PVRSRV_STUB_PBDESC	*psNext;
	PVRSRV_STUB_PBDESC	**ppsThis;
};

/*!
 ******************************************************************************
 * CCB control structure for SGX
 *****************************************************************************/
typedef struct _PVRSRV_SGX_CCB_INFO_
{
	PVRSRV_KERNEL_MEM_INFO	*psCCBMemInfo;			/*!< meminfo for CCB in device accessible memory */
	PVRSRV_KERNEL_MEM_INFO	*psCCBCtlMemInfo;		/*!< meminfo for CCB control in device accessible memory */
	SGXMKIF_COMMAND		*psCommands;			/*!< linear address of the array of commands */
	IMG_UINT32				*pui32WriteOffset;		/*!< linear address of the write offset into array of commands */
	volatile IMG_UINT32		*pui32ReadOffset;		/*!< linear address of the read offset into array of commands */
#if defined(PDUMP)
	IMG_UINT32				ui32CCBDumpWOff;		/*!< for pdumping */
#endif
} PVRSRV_SGX_CCB_INFO;


typedef struct _SGX_BRIDGE_INIT_INFO_KM_
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

#if defined(PVRSRV_USSE_EDM_STATUS_DEBUG)
	IMG_HANDLE	hKernelEDMStatusBufferMemInfo;
#endif

	IMG_UINT32 ui32EDMTaskReg0;
	IMG_UINT32 ui32EDMTaskReg1;

	IMG_UINT32 ui32ClkGateStatusReg;
	IMG_UINT32 ui32ClkGateStatusMask;
#if defined(SGX_FEATURE_MP)
//	IMG_UINT32 ui32MasterClkGateStatusReg;
//	IMG_UINT32 ui32MasterClkGateStatusMask;
//	IMG_UINT32 ui32MasterClkGateStatus2Reg;
//	IMG_UINT32 ui32MasterClkGateStatus2Mask;
#endif /* SGX_FEATURE_MP */

	IMG_UINT32 ui32CacheControl;

	IMG_UINT32	asInitDevData[SGX_MAX_DEV_DATA];
	IMG_HANDLE	asInitMemHandles[SGX_MAX_INIT_MEM_HANDLES];

} SGX_BRIDGE_INIT_INFO_KM;


typedef struct _SGX_INTERNEL_STATUS_UPDATE_KM_
{
	CTL_STATUS				sCtlStatus;
	IMG_HANDLE				hKernelMemInfo;
} SGX_INTERNEL_STATUS_UPDATE_KM;


typedef struct _SGX_CCB_KICK_KM_
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
	SGX_INTERNEL_STATUS_UPDATE_KM	asTAStatusUpdate[SGX_MAX_TA_STATUS_VALS];
	SGX_INTERNEL_STATUS_UPDATE_KM	as3DStatusUpdate[SGX_MAX_3D_STATUS_VALS];
#else
	IMG_HANDLE	ahTAStatusSyncInfo[SGX_MAX_TA_STATUS_VALS];
	IMG_HANDLE	ah3DStatusSyncInfo[SGX_MAX_3D_STATUS_VALS];
#endif

	IMG_BOOL	bFirstKickOrResume;
#if defined(NO_HARDWARE) || defined(PDUMP)
	IMG_BOOL	bTerminateOrAbort;
#endif

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
} SGX_CCB_KICK_KM;


#if defined(TRANSFER_QUEUE)
typedef struct _PVRSRV_TRANSFER_SGX_KICK_KM_
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
} PVRSRV_TRANSFER_SGX_KICK_KM, *PPVRSRV_TRANSFER_SGX_KICK_KM;

#if defined(SGX_FEATURE_2D_HARDWARE)
typedef struct _PVRSRV_2D_SGX_KICK_KM_
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
} PVRSRV_2D_SGX_KICK_KM, *PPVRSRV_2D_SGX_KICK_KM;
#endif	/* defined(SGX_FEATURE_2D_HARDWARE) */
#endif /* #if defined(TRANSFER_QUEUE) */

/****************************************************************************/
/* kernel only functions prototypes 										*/
/****************************************************************************/
PVRSRV_ERROR SGXRegisterDevice (PVRSRV_DEVICE_NODE *psDeviceNode);

IMG_VOID SGXOSTimer(IMG_VOID *pvData);

IMG_VOID SGXReset(PVRSRV_SGXDEV_INFO	*psDevInfo,
				  IMG_BOOL				bHardwareRecovery,
				  IMG_UINT32			ui32PDUMPFlags);

IMG_VOID SGXInitClocks(PVRSRV_SGXDEV_INFO	*psDevInfo,
					   IMG_UINT32			ui32PDUMPFlags);

PVRSRV_ERROR SGXInitialise(PVRSRV_SGXDEV_INFO	*psDevInfo,
						   IMG_BOOL				bHardwareRecovery);
PVRSRV_ERROR SGXDeinitialise(IMG_HANDLE hDevCookie);

PVRSRV_ERROR SGXPrePowerState(IMG_HANDLE				hDevHandle, 
							  PVRSRV_DEV_POWER_STATE	eNewPowerState, 
							  PVRSRV_DEV_POWER_STATE	eCurrentPowerState);

PVRSRV_ERROR SGXPostPowerState(IMG_HANDLE				hDevHandle, 
							   PVRSRV_DEV_POWER_STATE	eNewPowerState, 
							   PVRSRV_DEV_POWER_STATE	eCurrentPowerState);

PVRSRV_ERROR SGXPreClockSpeedChange(IMG_HANDLE				hDevHandle,
									IMG_BOOL				bIdleDevice,
									PVRSRV_DEV_POWER_STATE	eCurrentPowerState);

PVRSRV_ERROR SGXPostClockSpeedChange(IMG_HANDLE				hDevHandle,
									 IMG_BOOL				bIdleDevice,
									 PVRSRV_DEV_POWER_STATE	eCurrentPowerState);

IMG_VOID SGXPanic(PVRSRV_SGXDEV_INFO	*psDevInfo);

IMG_VOID SGXDumpDebugInfo (PVRSRV_SGXDEV_INFO	*psDevInfo,
						   IMG_BOOL				bDumpSGXRegs);

PVRSRV_ERROR SGXDevInitCompatCheck(PVRSRV_DEVICE_NODE *psDeviceNode);

#if defined(SGX_DYNAMIC_TIMING_INFO)
IMG_VOID SysGetSGXTimingInformation(SGX_TIMING_INFORMATION *psSGXTimingInfo);
#endif

/****************************************************************************/
/* kernel only functions: 													*/
/****************************************************************************/
#if defined(NO_HARDWARE)
static INLINE IMG_VOID NoHardwareGenerateEvent(PVRSRV_SGXDEV_INFO		*psDevInfo,
												IMG_UINT32 ui32StatusRegister,
												IMG_UINT32 ui32StatusValue,
												IMG_UINT32 ui32StatusMask)
{
	IMG_UINT32 ui32RegVal;

	ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, ui32StatusRegister);

	ui32RegVal &= ~ui32StatusMask;
	ui32RegVal |= (ui32StatusValue & ui32StatusMask);

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, ui32StatusRegister, ui32RegVal);
}
#endif

#if defined(__cplusplus)
}
#endif

#endif /* __SGXINFOKM_H__ */

/*****************************************************************************
 End of file (sgxinfokm.h)
*****************************************************************************/
