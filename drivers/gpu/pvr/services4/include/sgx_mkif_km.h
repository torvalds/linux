/*************************************************************************/ /*!
@Title          SGX microkernel interface structures used by srvkm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    SGX microkernel interface structures used by srvkm
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

#if !defined (__SGX_MKIF_KM_H__)
#define __SGX_MKIF_KM_H__

#include "img_types.h"
#include "servicesint.h"
#include "sgxapi_km.h"


#if !defined (SGX_MP_CORE_SELECT)
/* MP register control macros */
#if defined(SGX_FEATURE_MP)
	#define SGX_REG_BANK_SHIFT 			(14)
	#define SGX_REG_BANK_SIZE 			(1 << SGX_REG_BANK_SHIFT)
	#define SGX_REG_BANK_BASE_INDEX		(2)
	#define	SGX_REG_BANK_MASTER_INDEX	(1)
	#define SGX_MP_CORE_SELECT(x,i) 	(x + ((i + SGX_REG_BANK_BASE_INDEX) * SGX_REG_BANK_SIZE))
	#define SGX_MP_MASTER_SELECT(x) 	(x + (SGX_REG_BANK_MASTER_INDEX * SGX_REG_BANK_SIZE))
#else
	#define SGX_MP_CORE_SELECT(x,i) 	(x)
#endif /* SGX_FEATURE_MP */
#endif


/*!
 ******************************************************************************
 * CCB command structure for SGX
 *****************************************************************************/
typedef struct _SGXMKIF_COMMAND_
{
	IMG_UINT32				ui32ServiceAddress;		/*!< address of the USE command handler */
	IMG_UINT32				ui32CacheControl;		/*!< See SGXMKIF_CC_INVAL_* */
	IMG_UINT32				ui32Data[6];			/*!< array of other command control words */
} SGXMKIF_COMMAND;


/*!
 ******************************************************************************
 * CCB array of commands for SGX
 *****************************************************************************/
typedef struct _PVRSRV_SGX_KERNEL_CCB_
{
	SGXMKIF_COMMAND		asCommands[256];		/*!< array of commands */
} PVRSRV_SGX_KERNEL_CCB;


/*!
 ******************************************************************************
 * CCB control for SGX
 *****************************************************************************/
typedef struct _PVRSRV_SGX_CCB_CTL_
{
	IMG_UINT32				ui32WriteOffset;		/*!< write offset into array of commands (MUST be alligned to 16 bytes!) */
	IMG_UINT32				ui32ReadOffset;			/*!< read offset into array of commands */
} PVRSRV_SGX_CCB_CTL;


/*!
 *****************************************************************************
 * Control data for SGX
 *****************************************************************************/
typedef struct _SGXMKIF_HOST_CTL_
{
#if defined(PVRSRV_USSE_EDM_BREAKPOINTS)
	IMG_UINT32				ui32BreakpointDisable;
	IMG_UINT32				ui32Continue;
#endif

	volatile IMG_UINT32		ui32InitStatus;				/*!< Microkernel Initialisation status */
	volatile IMG_UINT32		ui32PowerStatus;			/*!< Microkernel Power Management status */
	volatile IMG_UINT32		ui32CleanupStatus;			/*!< Microkernel Resource Management status */
#if defined(FIX_HW_BRN_28889)
	volatile IMG_UINT32		ui32InvalStatus;			/*!< Microkernel BIF Cache Invalidate status */
#endif
#if defined(SUPPORT_HW_RECOVERY)
	IMG_UINT32				ui32uKernelDetectedLockups;	/*!< counter relating to the number of lockups the uKernel has detected */
	IMG_UINT32				ui32HostDetectedLockups;	/*!< counter relating to the number of lockups the host has detected */
	IMG_UINT32				ui32HWRecoverySampleRate;	/*!< SGX lockup detection rate (in multiples of the timer period) */
#endif /* SUPPORT_HW_RECOVERY*/
	IMG_UINT32				ui32uKernelTimerClock;		/*!< SGX ukernel timer period (in clocks) */
	IMG_UINT32				ui32ActivePowManSampleRate;	/*!< SGX Active Power latency period (in multiples of the timer period) */
	IMG_UINT32				ui32InterruptFlags; 		/*!< Interrupt flags - PVRSRV_USSE_EDM_INTERRUPT_xxx */
	IMG_UINT32				ui32InterruptClearFlags; 	/*!< Interrupt clear flags - PVRSRV_USSE_EDM_INTERRUPT_xxx */
	IMG_UINT32				ui32BPSetClearSignal;		/*!< Breakpoint set/cear signal */

	IMG_UINT32				ui32NumActivePowerEvents;	/*!< counter for the number of active power events */

	IMG_UINT32				ui32TimeWraps;				/*!< to count time wraps in the Timer task*/
	IMG_UINT32				ui32HostClock;				/*!< Host clock value at microkernel power-up time */
	IMG_UINT32				ui32AssertFail;				/*!< Microkernel assert failure code */

#if defined(SGX_FEATURE_EXTENDED_PERF_COUNTERS)
	IMG_UINT32				aui32PerfGroup[PVRSRV_SGX_HWPERF_NUM_COUNTERS];	/*!< Specifies the HW's active group selectors */
	IMG_UINT32				aui32PerfBit[PVRSRV_SGX_HWPERF_NUM_COUNTERS];	/*!< Specifies the HW's active bit selectors */
	IMG_UINT32				ui32PerfCounterBitSelect;						/*!< Specifies the HW's counter bit selectors */
	IMG_UINT32				ui32PerfSumMux;									/*!< Specifies the HW's sum_mux selectors */
#else
	IMG_UINT32				ui32PerfGroup;									/*!< Specifies the HW's active group */
#endif /* SGX_FEATURE_EXTENDED_PERF_COUNTERS */

	IMG_UINT32				ui32OpenCLDelayCount;			/* Counter to keep track OpenCL task completion time in units of regular task time out events */
	IMG_UINT32				ui32InterruptCount;
} SGXMKIF_HOST_CTL;

/*
 * TA queue Kick flags
 */
/* Set in DoKickKM to indicate the command is ready to be processed */
#define	SGXMKIF_CMDTA_CTRLFLAGS_READY			0x00000001
/*!
 ******************************************************************************
 * Shared TA command structure.
 * This structure is part of the TA command structure proper (SGXMKIF_CMDTA),
 * and is accessed from the kernel part of the driver and the microkernel.
 * There shouldn't be a need to access it from user space.
 *****************************************************************************/
typedef struct _SGXMKIF_CMDTA_SHARED_
{
	IMG_UINT32			ui32CtrlFlags;
	
	IMG_UINT32			ui32NumTAStatusVals;
	IMG_UINT32			ui32Num3DStatusVals;

	/* KEEP THESE 4 VARIABLES TOGETHER FOR UKERNEL BLOCK LOAD */
	IMG_UINT32			ui32TATQSyncWriteOpsPendingVal;
	IMG_DEV_VIRTADDR	sTATQSyncWriteOpsCompleteDevVAddr;
	IMG_UINT32			ui32TATQSyncReadOpsPendingVal;
	IMG_DEV_VIRTADDR	sTATQSyncReadOpsCompleteDevVAddr;

	/* KEEP THESE 4 VARIABLES TOGETHER FOR UKERNEL BLOCK LOAD */
	IMG_UINT32			ui323DTQSyncWriteOpsPendingVal;
	IMG_DEV_VIRTADDR	s3DTQSyncWriteOpsCompleteDevVAddr;
	IMG_UINT32			ui323DTQSyncReadOpsPendingVal;
	IMG_DEV_VIRTADDR	s3DTQSyncReadOpsCompleteDevVAddr;

	/* sync criteria used for TA/3D dependency synchronisation */
	PVRSRV_DEVICE_SYNC_OBJECT	sTA3DDependency;

#if defined(SUPPORT_SGX_GENERALISED_SYNCOBJECTS)
	/* SRC and DST syncs */
	IMG_UINT32					ui32NumTASrcSyncs;
	PVRSRV_DEVICE_SYNC_OBJECT	asTASrcSyncs[SGX_MAX_TA_SRC_SYNCS];
	IMG_UINT32					ui32NumTADstSyncs;
	PVRSRV_DEVICE_SYNC_OBJECT	asTADstSyncs[SGX_MAX_TA_DST_SYNCS];
	IMG_UINT32					ui32Num3DSrcSyncs;
	PVRSRV_DEVICE_SYNC_OBJECT	as3DSrcSyncs[SGX_MAX_3D_SRC_SYNCS];
#else
	/* source dependency details */
	IMG_UINT32			ui32NumSrcSyncs;
	PVRSRV_DEVICE_SYNC_OBJECT	asSrcSyncs[SGX_MAX_SRC_SYNCS_TA];
#endif

	CTL_STATUS			sCtlTAStatusInfo[SGX_MAX_TA_STATUS_VALS];
	CTL_STATUS			sCtl3DStatusInfo[SGX_MAX_3D_STATUS_VALS];

} SGXMKIF_CMDTA_SHARED;

/*
 * Services internal TQ limits
 */
#define SGXTQ_MAX_STATUS						SGX_MAX_TRANSFER_STATUS_VALS + 2

/*
 * Transfer queue Kick flags
 */
/* if set the uKernel won't update the sync objects on completion*/
#define SGXMKIF_TQFLAGS_NOSYNCUPDATE			0x00000001
/* if set the kernel won't advance the pending values*/
#define SGXMKIF_TQFLAGS_KEEPPENDING				0x00000002
/* in services equivalent for the same client flags*/
#define SGXMKIF_TQFLAGS_TATQ_SYNC				0x00000004
#define SGXMKIF_TQFLAGS_3DTQ_SYNC				0x00000008
#if defined(SGX_FEATURE_FAST_RENDER_CONTEXT_SWITCH)
#define SGXMKIF_TQFLAGS_CTXSWITCH				0x00000010
#endif
/* if set uKernel only updates syncobjects / status values*/
#define SGXMKIF_TQFLAGS_DUMMYTRANSFER			0x00000020

/*!
 ******************************************************************************
 * Shared Transfer Queue command structure.
 * This structure is placed at the start of the TQ command structure proper
 * (SGXMKIF_TRANSFERCMD), and is accessed from the kernel part of the driver
 * and the microkernel.
 *****************************************************************************/
typedef struct _SGXMKIF_TRANSFERCMD_SHARED_
{
	/* need to be able to check read and write ops on src, and update reads */

 	IMG_UINT32			ui32NumSrcSyncs;
 	PVRSRV_DEVICE_SYNC_OBJECT	asSrcSyncs[SGX_MAX_SRC_SYNCS_TQ];
	/* need to be able to check reads and writes on dest, and update writes */

 	IMG_UINT32			ui32NumDstSyncs;
 	PVRSRV_DEVICE_SYNC_OBJECT	asDstSyncs[SGX_MAX_DST_SYNCS_TQ];	
	/* KEEP THESE 4 VARIABLES TOGETHER FOR UKERNEL BLOCK LOAD */
	IMG_UINT32		ui32TASyncWriteOpsPendingVal;
	IMG_DEV_VIRTADDR	sTASyncWriteOpsCompleteDevVAddr;
	IMG_UINT32		ui32TASyncReadOpsPendingVal;
	IMG_DEV_VIRTADDR	sTASyncReadOpsCompleteDevVAddr;

	/* KEEP THESE 4 VARIABLES TOGETHER FOR UKERNEL BLOCK LOAD */
	IMG_UINT32		ui323DSyncWriteOpsPendingVal;
	IMG_DEV_VIRTADDR	s3DSyncWriteOpsCompleteDevVAddr;
	IMG_UINT32		ui323DSyncReadOpsPendingVal;
	IMG_DEV_VIRTADDR	s3DSyncReadOpsCompleteDevVAddr;

	IMG_UINT32 		ui32NumStatusVals;
	CTL_STATUS  	sCtlStatusInfo[SGXTQ_MAX_STATUS];
} SGXMKIF_TRANSFERCMD_SHARED, *PSGXMKIF_TRANSFERCMD_SHARED;


#if defined(SGX_FEATURE_2D_HARDWARE)
typedef struct _SGXMKIF_2DCMD_SHARED_ {
	/* need to be able to check read and write ops on src, and update reads */
	IMG_UINT32			ui32NumSrcSync;
	PVRSRV_DEVICE_SYNC_OBJECT	sSrcSyncData[SGX_MAX_2D_SRC_SYNC_OPS];

	/* need to be able to check reads and writes on dest, and update writes */
	PVRSRV_DEVICE_SYNC_OBJECT	sDstSyncData;

	/* need to be able to check reads and writes on TA ops, and update writes */
	PVRSRV_DEVICE_SYNC_OBJECT	sTASyncData;

	/* need to be able to check reads and writes on 2D ops, and update writes */
	PVRSRV_DEVICE_SYNC_OBJECT	s3DSyncData;

	IMG_UINT32 		ui32NumStatusVals;
	CTL_STATUS  	sCtlStatusInfo[SGXTQ_MAX_STATUS];
} SGXMKIF_2DCMD_SHARED, *PSGXMKIF_2DCMD_SHARED;
#endif /* SGX_FEATURE_2D_HARDWARE */


typedef struct _SGXMKIF_HWDEVICE_SYNC_LIST_
{
	IMG_DEV_VIRTADDR	sAccessDevAddr;
	IMG_UINT32			ui32NumSyncObjects;
	/* Must be the last variable in the structure */
	PVRSRV_DEVICE_SYNC_OBJECT	asSyncData[1];
} SGXMKIF_HWDEVICE_SYNC_LIST, *PSGXMKIF_HWDEVICE_SYNC_LIST;


/*!
 *****************************************************************************
 * Microkernel initialisation status
 *****************************************************************************/
#define PVRSRV_USSE_EDM_INIT_COMPLETE			(1UL << 0)	/*!< ukernel initialisation complete */

/*!
 *****************************************************************************
 * Microkernel power status definitions
 *****************************************************************************/
#define PVRSRV_USSE_EDM_POWMAN_IDLE_COMPLETE				(1UL << 2)	/*!< Signal from ukernel->Host indicating SGX is idle */
#define PVRSRV_USSE_EDM_POWMAN_POWEROFF_COMPLETE			(1UL << 3)	/*!< Signal from ukernel->Host indicating SGX can be powered down */
#define PVRSRV_USSE_EDM_POWMAN_POWEROFF_RESTART_IMMEDIATE	(1UL << 4)	/*!< Signal from ukernel->Host indicating there is work to do immediately */
#define PVRSRV_USSE_EDM_POWMAN_NO_WORK						(1UL << 5)	/*!< Signal from ukernel->Host indicating no work to do */

/*!
 *****************************************************************************
 * EDM interrupt defines
 *****************************************************************************/
#define PVRSRV_USSE_EDM_INTERRUPT_HWR			(1UL << 0)	/*!< EDM requesting hardware recovery */
#define PVRSRV_USSE_EDM_INTERRUPT_ACTIVE_POWER	(1UL << 1)	/*!< EDM requesting to be powered down */
#define PVRSRV_USSE_EDM_INTERRUPT_IDLE			(1UL << 2)	/*!< EDM indicating SGX idle */

/*!
 *****************************************************************************
 * EDM Resource management defines
 *****************************************************************************/
#define PVRSRV_USSE_EDM_CLEANUPCMD_COMPLETE 	(1UL << 0)	/*!< Signal from EDM->Host indicating clean-up request completion */
#define PVRSRV_USSE_EDM_CLEANUPCMD_BUSY		 	(1UL << 1)	/*!< Signal from EDM->Host indicating clean-up is blocked as the resource is busy */
#define PVRSRV_USSE_EDM_CLEANUPCMD_DONE		 	(1UL << 2)	/*!< Signal from EDM->Host indicating clean-up has been done */

#if defined(FIX_HW_BRN_28889)
/*!
 *****************************************************************************
 * EDM BIF Cache Invalidate defines
 *****************************************************************************/
#define PVRSRV_USSE_EDM_BIF_INVAL_COMPLETE 		(1UL << 0)	/*!< Signal from EDM->Host indicating the BIF invalidate has started */
#endif

/*!
 ****************************************************************************
 * EDM / uKernel Get misc info defines
 ****************************************************************************
 */
#define PVRSRV_USSE_MISCINFO_READY		0x1UL
#define PVRSRV_USSE_MISCINFO_GET_STRUCT_SIZES	0x2UL	/*!< If set, getmiscinfo ukernel func returns structure sizes */
#if defined(SUPPORT_SGX_EDM_MEMORY_DEBUG)
#define PVRSRV_USSE_MISCINFO_MEMREAD			0x4UL	/*!< If set, getmiscinfo ukernel func reads arbitrary device mem */
#define PVRSRV_USSE_MISCINFO_MEMWRITE			0x8UL	/*!< If set, getmiscinfo ukernel func writes arbitrary device mem */
#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
#define PVRSRV_USSE_MISCINFO_MEMREAD_FAIL		0x1UL << 31	/* If set, ukernel was unable to read from the mem context */
#endif
#endif


/* Cleanup command control word */
#define	PVRSRV_CLEANUPCMD_RT		0x1U
#define	PVRSRV_CLEANUPCMD_RC		0x2U
#define	PVRSRV_CLEANUPCMD_TC		0x3U
#define	PVRSRV_CLEANUPCMD_2DC		0x4U
#define	PVRSRV_CLEANUPCMD_PB		0x5U

/* Power command control word */
#define PVRSRV_POWERCMD_POWEROFF	0x1U
#define PVRSRV_POWERCMD_IDLE		0x2U
#define PVRSRV_POWERCMD_RESUME		0x3U

/* Context suspend command control word */
#define PVRSRV_CTXSUSPCMD_SUSPEND	0x1U
#define PVRSRV_CTXSUSPCMD_RESUME	0x2U


#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
#define SGX_BIF_DIR_LIST_INDEX_EDM	(SGX_FEATURE_BIF_NUM_DIRLISTS - 1)
#else
#define SGX_BIF_DIR_LIST_INDEX_EDM	(0)
#endif

/*!
 ******************************************************************************
 * microkernel cache control requests
 ******************************************************************************/
#define	SGXMKIF_CC_INVAL_BIF_PT	0x1
#define	SGXMKIF_CC_INVAL_BIF_PD	0x2
#define SGXMKIF_CC_INVAL_BIF_SL	0x4
#define SGXMKIF_CC_INVAL_DATA	0x8


/*!
 ******************************************************************************
 * SGX microkernel interface structure sizes
 ******************************************************************************/
typedef struct _SGX_MISCINFO_STRUCT_SIZES_
{
#if defined (SGX_FEATURE_2D_HARDWARE)
	IMG_UINT32	ui32Sizeof_2DCMD;
	IMG_UINT32	ui32Sizeof_2DCMD_SHARED;
#endif
	IMG_UINT32	ui32Sizeof_CMDTA;
	IMG_UINT32	ui32Sizeof_CMDTA_SHARED;
	IMG_UINT32	ui32Sizeof_TRANSFERCMD;
	IMG_UINT32	ui32Sizeof_TRANSFERCMD_SHARED;
	IMG_UINT32	ui32Sizeof_3DREGISTERS;
	IMG_UINT32	ui32Sizeof_HWPBDESC;
	IMG_UINT32	ui32Sizeof_HWRENDERCONTEXT;
	IMG_UINT32	ui32Sizeof_HWRENDERDETAILS;
	IMG_UINT32	ui32Sizeof_HWRTDATA;
	IMG_UINT32	ui32Sizeof_HWRTDATASET;
	IMG_UINT32	ui32Sizeof_HWTRANSFERCONTEXT;
	IMG_UINT32	ui32Sizeof_HOST_CTL;
	IMG_UINT32	ui32Sizeof_COMMAND;
} SGX_MISCINFO_STRUCT_SIZES;


#if defined(SUPPORT_SGX_EDM_MEMORY_DEBUG)
/*!
 *****************************************************************************
 * SGX misc info for accessing device memory from ukernel
 *****************************************************************************
 */
typedef struct _PVRSRV_SGX_MISCINFO_MEMACCESS
{
	IMG_DEV_VIRTADDR	sDevVAddr;		/*!< dev virtual addr for mem access */
	IMG_DEV_PHYADDR		sPDDevPAddr;	/*!< device physical addr of PD for the mem heap */
} PVRSRV_SGX_MISCINFO_MEMACCESS;
#endif

/*!
 *****************************************************************************
 * SGX Misc Info structure used in the microkernel
 * PVRSRV_SGX_MISCINFO_FEATURES is defined in sgxapi_km.h
 ****************************************************************************/
typedef struct _PVRSRV_SGX_MISCINFO_INFO
{
	IMG_UINT32						ui32MiscInfoFlags;
	PVRSRV_SGX_MISCINFO_FEATURES	sSGXFeatures;		/*!< external info for client */
	SGX_MISCINFO_STRUCT_SIZES		sSGXStructSizes;	/*!< internal info: microkernel structure sizes */
#if defined(SUPPORT_SGX_EDM_MEMORY_DEBUG)
	PVRSRV_SGX_MISCINFO_MEMACCESS	sSGXMemAccessSrc;	/*!< internal info: for reading dev memory */
	PVRSRV_SGX_MISCINFO_MEMACCESS	sSGXMemAccessDest;	/*!< internal info: for writing dev memory */
#endif
} PVRSRV_SGX_MISCINFO_INFO;

#ifdef PVRSRV_USSE_EDM_STATUS_DEBUG
/*!
 *****************************************************************************
 * Number of entries in the microkernel status buffer
 *****************************************************************************/
#define SGXMK_TRACE_BUFFER_SIZE 512
#endif /* PVRSRV_USSE_EDM_STATUS_DEBUG */

#define SGXMKIF_HWPERF_CB_SIZE					0x100	/* must be 2^n*/

/*!
 *****************************************************************************
 * One entry in the HWPerf Circular Buffer.
 *****************************************************************************/
typedef struct _SGXMKIF_HWPERF_CB_ENTRY_
{
	IMG_UINT32	ui32FrameNo;
	IMG_UINT32	ui32PID;
	IMG_UINT32	ui32RTData;
	IMG_UINT32	ui32Type;
	IMG_UINT32	ui32Ordinal;
	IMG_UINT32	ui32Info;
	IMG_UINT32	ui32TimeWraps;
	IMG_UINT32	ui32Time;
	/* NOTE: There should always be at least as many 3D cores as TA cores. */
	IMG_UINT32	ui32Counters[SGX_FEATURE_MP_CORE_COUNT_3D][PVRSRV_SGX_HWPERF_NUM_COUNTERS];
	IMG_UINT32	ui32MiscCounters[SGX_FEATURE_MP_CORE_COUNT_3D][PVRSRV_SGX_HWPERF_NUM_MISC_COUNTERS];
} SGXMKIF_HWPERF_CB_ENTRY;

/*!
 *****************************************************************************
 * The HWPerf Circular Buffer.
 *****************************************************************************/
typedef struct _SGXMKIF_HWPERF_CB_
{
	IMG_UINT32				ui32Woff;
	IMG_UINT32				ui32Roff;
	IMG_UINT32				ui32Ordinal;
	SGXMKIF_HWPERF_CB_ENTRY psHWPerfCBData[SGXMKIF_HWPERF_CB_SIZE];
} SGXMKIF_HWPERF_CB;


#endif /*  __SGX_MKIF_KM_H__ */

/******************************************************************************
 End of file (sgx_mkif_km.h)
******************************************************************************/


