/*************************************************************************/ /*!
@File
@Title          RGX firmware interface structures used by pvrsrvkm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX firmware interface structures used by pvrsrvkm
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

#if !defined (__RGX_FWIF_KM_H__)
#define __RGX_FWIF_KM_H__

#include "img_types.h"
#include "rgx_fwif_shared.h"
#include "rgxdefs_km.h"
#include "pvr_debug.h"
#include "dllist.h"
#include "rgx_firmware_processor.h"

#if	!defined(__KERNEL__)
/* The following defines the offsets for the KCCB, KCCBCtl, FWCCB and FWCCBCtl
 * for the various guests in a virtualisation environment. It is assumed that each
 * guest is built the same way and so all their offsets will match. If the code
 * at host level changes and the offsets change, the defines here need to be updated.
 */

#if defined(RGX_FEATURE_META)
#define RGXFWIF_GUEST_OFFSET_KCCB           (RGXFW_SEGMMU_DATA_BASE_ADDRESS | \
                                             RGXFW_SEGMMU_DATA_META_CACHED | \
                                             RGXFW_SEGMMU_DATA_VIVT_SLC_UNCACHED | \
                                             0x2000U)
#define RGXFWIF_GUEST_OFFSET_KCCBCTL        (RGXFW_SEGMMU_DATA_BASE_ADDRESS | \
                                             RGXFW_SEGMMU_DATA_META_UNCACHED | \
                                             RGXFW_SEGMMU_DATA_VIVT_SLC_UNCACHED | \
											 0x0280U)
#define RGXFWIF_GUEST_OFFSET_FWCCB          (RGXFW_SEGMMU_DATA_BASE_ADDRESS | \
                                             RGXFW_SEGMMU_DATA_META_UNCACHED | \
                                             RGXFW_SEGMMU_DATA_VIVT_SLC_UNCACHED | \
											 0x0300U)
#define RGXFWIF_GUEST_OFFSET_FWCCBCTL       (RGXFW_SEGMMU_DATA_BASE_ADDRESS | \
                                             RGXFW_SEGMMU_DATA_META_UNCACHED | \
                                             RGXFW_SEGMMU_DATA_VIVT_SLC_UNCACHED | \
											 0x02C0U)
#else
/* In case of MIPS we will need to define proper values for these offsets */
#define RGXFWIF_GUEST_OFFSET_KCCB           (0x0)
#define RGXFWIF_GUEST_OFFSET_KCCBCTL        (0x0)
#define RGXFWIF_GUEST_OFFSET_FWCCB          (0x0)
#define RGXFWIF_GUEST_OFFSET_FWCCBCTL       (0x0)
#endif

#endif

#if defined(RGX_FIRMWARE)
typedef DLLIST_NODE							RGXFWIF_DLLIST_NODE;
#else
typedef struct {RGXFWIF_DEV_VIRTADDR p;
                  RGXFWIF_DEV_VIRTADDR n;}	RGXFWIF_DLLIST_NODE;
#endif

typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_SIGBUFFER;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_TRACEBUF;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_HWPERFBUF;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_HWRINFOBUF;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_RUNTIME_CFG;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_GPU_UTIL_FWCB;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_REG_CFG;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_HWPERF_CTL;
typedef RGXFWIF_DEV_VIRTADDR				PRGX_HWPERF_CONFIG_CNTBLK;
typedef RGXFWIF_DEV_VIRTADDR				PRGX_HWPERF_SELECT_CUSTOM_CNTRS;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_CCB_CTL;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_CCB;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_FWMEMCONTEXT;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_FWCOMMONCONTEXT;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_ZSBUFFER;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_INIT;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_COMMONCTX_STATE;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_RF_CMD;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_COMPCHECKS;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_ALIGNCHECK;
typedef RGXFWIF_DEV_VIRTADDR				PRGXFWIF_CORE_CLK_RATE;

/*!
 * This number is used to represent an invalid page catalogue physical address
 */
#define RGXFWIF_INVALID_PC_PHYADDR 0xFFFFFFFFFFFFFFFFLLU

/*!
    Firmware memory context.
*/
typedef struct _RGXFWIF_FWMEMCONTEXT_
{
	IMG_DEV_PHYADDR			RGXFW_ALIGN sPCDevPAddr;	/*!< device physical address of context's page catalogue */
	IMG_INT32				uiPageCatBaseRegID;	/*!< associated page catalog base register (-1 == unallocated) */
	IMG_UINT32				uiBreakpointAddr; /*!< breakpoint address */
	IMG_UINT32				uiBPHandlerAddr;  /*!< breakpoint handler address */
	IMG_UINT32				uiBreakpointCtl; /*!< DM and enable control for BP */

#if defined(SUPPORT_GPUVIRT_VALIDATION)
    IMG_UINT32              ui32OSid;
    IMG_BOOL                bOSidAxiProt;
#endif

} UNCACHED_ALIGN RGXFWIF_FWMEMCONTEXT;


/*!
 * 	FW context state flags
 */
#define	RGXFWIF_CONTEXT_TAFLAGS_NEED_RESUME			(0x00000001)
#define	RGXFWIF_CONTEXT_RENDERFLAGS_NEED_RESUME		(0x00000002)
#define RGXFWIF_CONTEXT_CDMFLAGS_NEED_RESUME		(0x00000004)
#define RGXFWIF_CONTEXT_SHGFLAGS_NEED_RESUME		(0x00000008)
#define RGXFWIF_CONTEXT_TDMFLAGS_CONTEXT_STORED		(0x00000010)
#define RGXFWIF_CONTEXT_ALLFLAGS_NEED_RESUME		(0x0000001F)


typedef struct _RGXFWIF_TACTX_STATE_
{
	/* FW-accessible TA state which must be written out to memory on context store */
	IMG_UINT64	RGXFW_ALIGN uTAReg_VDM_CALL_STACK_POINTER;		 /* To store in mid-TA */
	IMG_UINT64	RGXFW_ALIGN uTAReg_VDM_CALL_STACK_POINTER_Init;	 /* Initial value (in case is 'lost' due to a lock-up */
	IMG_UINT64	RGXFW_ALIGN uTAReg_VDM_BATCH;
	IMG_UINT64	RGXFW_ALIGN uTAReg_VBS_SO_PRIM0;
	IMG_UINT64	RGXFW_ALIGN uTAReg_VBS_SO_PRIM1;
	IMG_UINT64	RGXFW_ALIGN uTAReg_VBS_SO_PRIM2;
	IMG_UINT64	RGXFW_ALIGN uTAReg_VBS_SO_PRIM3;
#if defined(SUPPORT_VDM_CONTEXT_STORE_BUFFER_AB)
	IMG_UINT16	RGXFW_ALIGN ui16TACurrentIdx;
#endif
} UNCACHED_ALIGN RGXFWIF_TACTX_STATE;


typedef struct _RGXFWIF_3DCTX_STATE_
{
	/* FW-accessible ISP state which must be written out to memory on context store */
	IMG_UINT32	RGXFW_ALIGN au3DReg_ISP_STORE[64];
	IMG_UINT64	RGXFW_ALIGN u3DReg_PM_DEALLOCATED_MASK_STATUS;
	IMG_UINT64	RGXFW_ALIGN u3DReg_PM_PDS_MTILEFREE_STATUS;
} UNCACHED_ALIGN RGXFWIF_3DCTX_STATE;



typedef struct _RGXFWIF_COMPUTECTX_STATE_
{
	IMG_BOOL	RGXFW_ALIGN	bBufferB;
} RGXFWIF_COMPUTECTX_STATE;


typedef struct _RGXFWIF_VRDMCTX_STATE_
{
	/* FW-accessible TA state which must be written out to memory on context store */
	IMG_UINT64	RGXFW_ALIGN uVRDMReg_VRM_CALL_STACK_POINTER;
	IMG_UINT64	RGXFW_ALIGN uVRDMReg_VRM_BATCH;
} UNCACHED_ALIGN RGXFWIF_VRDMCTX_STATE;


typedef struct _RGXFWIF_FWCOMMONCONTEXT_
{
	/*
		Used by bg and irq context
	*/
	/* CCB details for this firmware context */
	PRGXFWIF_CCCB_CTL		psCCBCtl;				/*!< CCB control */
	PRGXFWIF_CCCB			psCCB;					/*!< CCB base */
	RGXFWIF_DMA_ADDR		sCCBMetaDMAAddr;

	/*
		Used by the bg context only
	*/
	RGXFWIF_DLLIST_NODE		RGXFW_ALIGN sWaitingNode;			/*!< List entry for the waiting list */

	/*
		Used by the irq context only
	*/
	RGXFWIF_DLLIST_NODE		sRunNode;				/*!< List entry for the run list */

	PRGXFWIF_FWMEMCONTEXT	psFWMemContext;			/*!< Memory context */

	/* Context suspend state */
	PRGXFWIF_COMMONCTX_STATE	RGXFW_ALIGN psContextState;		/*!< TA/3D context suspend state, read/written by FW */

	/* Framework state
	 */
	PRGXFWIF_RF_CMD		RGXFW_ALIGN psRFCmd;		/*!< Register updates for Framework */

	/*
	 * 	Flags e.g. for context switching
	 */
	IMG_UINT32				ui32Flags;
	IMG_UINT32				ui32Priority;
	IMG_UINT32				ui32PrioritySeqNum;
	IMG_UINT64		RGXFW_ALIGN 	ui64MCUFenceAddr;

	/* References to the host side originators */
	IMG_UINT32				ui32ServerCommonContextID;			/*!< the Server Common Context */
	IMG_UINT32				ui32PID;							/*!< associated process ID */

	/* Statistic updates waiting to be passed back to the host... */
	IMG_BOOL				bStatsPending;						/*!< True when some stats are pending */
	IMG_INT32				i32StatsNumStores;					/*!< Number of stores on this context since last update */
	IMG_INT32				i32StatsNumOutOfMemory;				/*!< Number of OOMs on this context since last update */
	IMG_INT32				i32StatsNumPartialRenders;			/*!< Number of PRs on this context since last update */
	RGXFWIF_DM				eDM;								/*!< Data Master type */
	IMG_UINT64        RGXFW_ALIGN           ui64WaitSignalAddress;        /*!< Device Virtual Address of the signal the context is waiting on */
	RGXFWIF_DLLIST_NODE                     sWaitSignalNode;              /*!< List entry for the wait-signal list */
	RGXFWIF_DLLIST_NODE		RGXFW_ALIGN  sBufStalledNode;		/*!< List entry for the buffer stalled list */
	IMG_UINT64    		    RGXFW_ALIGN  ui64CBufQueueCtrlAddr;	/*!< Address of the circular buffer queue pointers */
	IMG_UINT64    		    RGXFW_ALIGN  ui64ResumeSignalAddr;	/*!< Address of the Services Signal for resuming the buffer */
	IMG_BOOL    		    bReadOffsetNeedsReset;				/*!< Following HWR circular buffer read-offset needs resetting */
} UNCACHED_ALIGN RGXFWIF_FWCOMMONCONTEXT;

/*!
	Firmware render context.
*/
typedef struct _RGXFWIF_FWRENDERCONTEXT_
{
	RGXFWIF_FWCOMMONCONTEXT	sTAContext;				/*!< Firmware context for the TA */
	RGXFWIF_FWCOMMONCONTEXT	s3DContext;				/*!< Firmware context for the 3D */

	/*
	 * Note: The following fields keep track of OOM and partial render statistics.
	 * Because these data structures are allocated cache-incoherent,
	 * and because these fields are updated by the firmware,
	 * the host will read valid values only after an SLC flush/inval.
	 * This is only guaranteed to happen while destroying the render-context.
	 */

	/* The following variable has been reused to avoid breaking compatibility.
	 *
	 * It was previously:
	 * IMG_UINT32 ui32TotalNumPartialRenders; Total number of partial renders
	 *
	 * And is changed to:
	 */
	IMG_UINT32			ui32WorkEstCCBSubmitted; /*!< Number of commands submitted to the WorkEst FW CCB */

	IMG_UINT32			ui32TotalNumOutOfMemory;	/*!< Total number of OOMs */

} UNCACHED_ALIGN RGXFWIF_FWRENDERCONTEXT;

/*!
	Firmware render context.
*/
typedef struct _RGXFWIF_FWRAYCONTEXT_
{
	IMG_UINT32				ui32ActiveFCMask; /* move here to avoid that fwrayctx and shgctx have the same addr */
	IMG_UINT32				ui32NextFC;
	RGXFWIF_FWCOMMONCONTEXT	sSHGContext;				/*!< Firmware context for the SHG */
	RGXFWIF_FWCOMMONCONTEXT	sRTUContext;				/*!< Firmware context for the RTU */
	PRGXFWIF_CCCB_CTL		psCCBCtl[DPX_MAX_RAY_CONTEXTS];
	PRGXFWIF_CCCB			psCCB[DPX_MAX_RAY_CONTEXTS];
} UNCACHED_ALIGN RGXFWIF_FWRAYCONTEXT;

#define RGXFWIF_INVALID_FRAME_CONTEXT (0xFFFFFFFF)

/*!
	BIF tiling mode
*/
typedef enum _RGXFWIF_BIFTILINGMODE_
{
	RGXFWIF_BIFTILINGMODE_NONE      = 0,
	RGXFWIF_BIFTILINGMODE_256x16    = 0,
	RGXFWIF_BIFTILINGMODE_512x8     = 1
} RGXFWIF_BIFTILINGMODE;

/*!
	BIF requester selection
*/
typedef enum _RGXFWIF_BIFREQ_
{
	RGXFWIF_BIFREQ_TA		= 0,
	RGXFWIF_BIFREQ_3D		= 1,
	RGXFWIF_BIFREQ_CDM		= 2,
	RGXFWIF_BIFREQ_2D		= 3,
	RGXFWIF_BIFREQ_TDM		= 3,
	RGXFWIF_BIFREQ_HOST		= 4,
	RGXFWIF_BIFREQ_RTU		= 5,
	RGXFWIF_BIFREQ_SHG		= 6,
	RGXFWIF_BIFREQ_MAX		= 7
} RGXFWIF_BIFREQ;

typedef enum _RGXFWIF_PM_DM_
{
	RGXFWIF_PM_DM_TA	= 0,
	RGXFWIF_PM_DM_3D	= 1,
} RGXFWIF_PM_DM;

typedef enum _RGXFWIF_RPM_DM_
{
	RGXFWIF_RPM_DM_SHF	= 0,
	RGXFWIF_RPM_DM_SHG	= 1,
	RGXFWIF_RPM_DM_MAX,
} RGXFWIF_RPM_DM;

/*!
 ******************************************************************************
 * Kernel CCB control for RGX
 *****************************************************************************/
typedef struct _RGXFWIF_CCB_CTL_
{
	volatile IMG_UINT32		ui32WriteOffset;		/*!< write offset into array of commands (MUST be aligned to 16 bytes!) */
	volatile IMG_UINT32		ui32ReadOffset;			/*!< read offset into array of commands */
	IMG_UINT32				ui32WrapMask;			/*!< Offset wrapping mask (Total capacity of the CCB - 1) */
	IMG_UINT32				ui32CmdSize;			/*!< size of each command in bytes */
} UNCACHED_ALIGN RGXFWIF_CCB_CTL;

/*!
 ******************************************************************************
 * Kernel CCB command structure for RGX
 *****************************************************************************/

#define RGXFWIF_MMUCACHEDATA_FLAGS_PT      (0x1) /* MMU_CTRL_INVAL_PT_EN */
#define RGXFWIF_MMUCACHEDATA_FLAGS_PD      (0x2) /* MMU_CTRL_INVAL_PD_EN */
#define RGXFWIF_MMUCACHEDATA_FLAGS_PC      (0x4) /* MMU_CTRL_INVAL_PC_EN */

#if !defined(__KERNEL)

#if !defined(RGX_FEATURE_SLC_VIVT)
#define RGXFWIF_MMUCACHEDATA_FLAGS_PMTLB   (0x10) /* can't use PM_TLB0 bit from BIFPM_CTRL reg because it collides with PT bit from BIF_CTRL reg */
#define RGXFWIF_MMUCACHEDATA_FLAGS_TLB     (RGXFWIF_MMUCACHEDATA_FLAGS_PMTLB | 0x8) /* BIF_CTRL_INVAL_TLB1_EN */
#define RGXFWIF_MMUCACHEDATA_FLAGS_CTX(C)  (0x0) /* not used */
#define RGXFWIF_MMUCACHEDATA_FLAGS_CTX_ALL (0x0) /* not used */

#else /* RGX_FEATURE_SLC_VIVT */
#define RGXFWIF_MMUCACHEDATA_FLAGS_PMTLB   (0x0) /* not used */
#define RGXFWIF_MMUCACHEDATA_FLAGS_TLB     (0x0) /* not used */
#define RGXFWIF_MMUCACHEDATA_FLAGS_CTX(C)  ((C) << 0x3) /* MMU_CTRL_INVAL_CONTEXT_SHIFT */
#define RGXFWIF_MMUCACHEDATA_FLAGS_CTX_ALL (0x800) /* MMU_CTRL_INVAL_ALL_CONTEXTS_EN */
#endif

#else
#define RGXFWIF_MMUCACHEDATA_FLAGS_PMTLB   (0x10) /* can't use PM_TLB0 bit from BIFPM_CTRL reg because it collides with PT bit from BIF_CTRL reg */
#define RGXFWIF_MMUCACHEDATA_FLAGS_TLB     (RGXFWIF_MMUCACHEDATA_FLAGS_PMTLB | 0x8) /* BIF_CTRL_INVAL_TLB1_EN */
#define RGXFWIF_MMUCACHEDATA_FLAGS_CTX(C)  ((C) << 0x3) /* MMU_CTRL_INVAL_CONTEXT_SHIFT */
#define RGXFWIF_MMUCACHEDATA_FLAGS_CTX_ALL (0x800) /* MMU_CTRL_INVAL_ALL_CONTEXTS_EN */
#endif

#define RGXFWIF_MMUCACHEDATA_FLAGS_INTERRUPT (0x4000000) /* indicates FW should interrupt the host */

typedef struct _RGXFWIF_MMUCACHEDATA_
{
	PRGXFWIF_FWMEMCONTEXT		psMemoryContext;
	IMG_UINT32					ui32Flags;
	RGXFWIF_DEV_VIRTADDR		sMMUCacheSync;
	IMG_UINT32					ui32MMUCacheSyncUpdateValue;
} RGXFWIF_MMUCACHEDATA;

typedef struct _RGXFWIF_SLCBPCTLDATA_
{
	IMG_BOOL               bSetBypassed;        /*!< Should SLC be/not be bypassed for indicated units? */
	IMG_UINT32             uiFlags;             /*!< Units to enable/disable */
} RGXFWIF_SLCBPCTLDATA;

#define RGXFWIF_BPDATA_FLAGS_WRITE	(1 << 0)
#define RGXFWIF_BPDATA_FLAGS_CTL	(1 << 1)
#define RGXFWIF_BPDATA_FLAGS_REGS	(1 << 2)

typedef struct _RGXFWIF_FWBPDATA_
{
	PRGXFWIF_FWMEMCONTEXT	psFWMemContext;			/*!< Memory context */
	IMG_UINT32		ui32BPAddr;			/*!< Breakpoint address */
	IMG_UINT32		ui32HandlerAddr;		/*!< Breakpoint handler */
	IMG_UINT32		ui32BPDM;			/*!< Breakpoint control */
	IMG_BOOL		bEnable;
	IMG_UINT32		ui32Flags;
	IMG_UINT32		ui32TempRegs;		/*!< Number of temporary registers to overallocate */
	IMG_UINT32		ui32SharedRegs;		/*!< Number of shared registers to overallocate */
} RGXFWIF_BPDATA;

#define RGXFWIF_KCCB_CMD_KICK_DATA_MAX_NUM_CLEANUP_CTLS 4

typedef struct _RGXFWIF_KCCB_CMD_KICK_DATA_
{
	PRGXFWIF_FWCOMMONCONTEXT	psContext;			/*!< address of the firmware context */
	IMG_UINT32					ui32CWoffUpdate;	/*!< Client CCB woff update */
	IMG_UINT32					ui32NumCleanupCtl;		/*!< number of CleanupCtl pointers attached */
	PRGXFWIF_CLEANUP_CTL		apsCleanupCtl[RGXFWIF_KCCB_CMD_KICK_DATA_MAX_NUM_CLEANUP_CTLS]; /*!< CleanupCtl structures associated with command */
	PRGXFWIF_WORKLOAD_DATA		sWorkloadDataFWAddress;   /*!< deprecated, kept for compatibility. */
	IMG_UINT32					ui32WorkEstCmdHeaderOffset; /*!< offset to the CmdHeader which houses the workload estimation kick data. */
} RGXFWIF_KCCB_CMD_KICK_DATA;

typedef struct _RGXFWIF_KCCB_CMD_FENCE_DATA_
{
	RGXFWIF_DEV_VIRTADDR sSyncObjDevVAddr;
	IMG_UINT32 uiUpdateVal;
} RGXFWIF_KCCB_CMD_SYNC_DATA;

typedef enum _RGXFWIF_CLEANUP_TYPE_
{
	RGXFWIF_CLEANUP_FWCOMMONCONTEXT,		/*!< FW common context cleanup */
	RGXFWIF_CLEANUP_HWRTDATA,				/*!< FW HW RT data cleanup */
	RGXFWIF_CLEANUP_FREELIST,				/*!< FW freelist cleanup */
	RGXFWIF_CLEANUP_ZSBUFFER,				/*!< FW ZS Buffer cleanup */
	RGXFWIF_CLEANUP_HWFRAMEDATA,			/*!< FW RPM/RTU frame data */
	RGXFWIF_CLEANUP_RPM_FREELIST,			/*!< FW RPM freelist */
} RGXFWIF_CLEANUP_TYPE;

#define RGXFWIF_CLEANUP_RUN		(1 << 0)	/*!< The requested cleanup command has run on the FW */
#define RGXFWIF_CLEANUP_BUSY	(1 << 1)	/*!< The requested resource is busy */

typedef struct _RGXFWIF_CLEANUP_REQUEST_
{
	RGXFWIF_CLEANUP_TYPE			eCleanupType;			/*!< Cleanup type */
	union {
		PRGXFWIF_FWCOMMONCONTEXT 	psContext;				/*!< FW common context to cleanup */
		PRGXFWIF_HWRTDATA 			psHWRTData;				/*!< HW RT to cleanup */
		PRGXFWIF_FREELIST 			psFreelist;				/*!< Freelist to cleanup */
		PRGXFWIF_ZSBUFFER 			psZSBuffer;				/*!< ZS Buffer to cleanup */
		PRGXFWIF_RAY_FRAME_DATA		psHWFrameData;			/*!< RPM/RTU frame data to cleanup */
		PRGXFWIF_RPM_FREELIST 		psRPMFreelist;			/*!< RPM Freelist to cleanup */
	} uCleanupData;
	RGXFWIF_DEV_VIRTADDR						sSyncObjDevVAddr;		/*!< sync primitive used to indicate state of the request */
} RGXFWIF_CLEANUP_REQUEST;

typedef enum _RGXFWIF_POWER_TYPE_
{
	RGXFWIF_POW_OFF_REQ = 1,
	RGXFWIF_POW_FORCED_IDLE_REQ,
	RGXFWIF_POW_NUMDUST_CHANGE,
	RGXFWIF_POW_APM_LATENCY_CHANGE
} RGXFWIF_POWER_TYPE;

typedef enum
{
	RGXFWIF_OS_ONLINE = 1,
	RGXFWIF_OS_OFFLINE
} RGXFWIF_OS_STATE_CHANGE;

typedef struct _RGXFWIF_POWER_REQUEST_
{
	RGXFWIF_POWER_TYPE				ePowType;				/*!< Type of power request */
	union
	{
		IMG_UINT32					ui32NumOfDusts;			/*!< Number of active Dusts */
		IMG_BOOL					bForced;				/*!< If the operation is mandatory */
		IMG_BOOL					bCancelForcedIdle;		/*!< If the operation is to cancel previously forced idle */
		IMG_UINT32					ui32ActivePMLatencyms;		/*!< Number of milliseconds to set APM latency */
	} uPoweReqData;
	IMG_BOOL						bNotifyTimeout;			/*!< Notify the FW that the host has timed out waiting for a response to an idling.
															     It's placed here as an extension of the struct for backwards compatibility reasons */
} RGXFWIF_POWER_REQUEST;

typedef struct _RGXFWIF_SLCFLUSHINVALDATA_
{
	PRGXFWIF_FWCOMMONCONTEXT psContext; /*!< Context to fence on (only useful when bDMContext == TRUE) */
	IMG_BOOL    bInval;                 /*!< Invalidate the cache as well as flushing */
	IMG_BOOL    bDMContext;             /*!< The data to flush/invalidate belongs to a specific DM context */
	RGXFWIF_DM  eDM;                    /*!< DM to flush entries for (only useful when bDMContext == TRUE) */
} RGXFWIF_SLCFLUSHINVALDATA;

typedef struct _RGXFWIF_STATEFLAG_CTRL_
{
	IMG_BOOL    bSetNotClear; /*!< Set or clear config flags */
	IMG_UINT32  ui32Config;   /*!< Mask of config flags to change */
	RGXFWIF_DEV_VIRTADDR sSyncObjDevVAddr;	/*!< sync primitive used to return the status */
} RGXFWIF_STATEFLAG_CTRL;

typedef struct _RGXFWIF_HCS_CTL_
{
	IMG_UINT32  ui32HCSDeadlineMS;  /* New number of milliseconds C/S is allowed to last */
} RGXFWIF_HCS_CTL;

typedef struct _RGXFWIF_HWPERF_CTRL_
{
	IMG_BOOL	 			bToggle; 	/*!< Toggle masked bits or apply full mask? */
	IMG_UINT64	RGXFW_ALIGN	ui64Mask;   /*!< Mask of events to toggle */
} RGXFWIF_HWPERF_CTRL;

typedef struct _RGXFWIF_HWPERF_CONFIG_ENABLE_BLKS_
{
	IMG_UINT32                ui32NumBlocks;    /*!< Number of RGX_HWPERF_CONFIG_CNTBLK in the array */
	PRGX_HWPERF_CONFIG_CNTBLK sBlockConfigs;    /*!< Address of the RGX_HWPERF_CONFIG_CNTBLK array */
} RGXFWIF_HWPERF_CONFIG_ENABLE_BLKS;

typedef struct _RGXFWIF_CORECLKSPEEDCHANGE_DATA_
{
	IMG_UINT32	ui32NewClockSpeed; 			/*!< New clock speed */
} RGXFWIF_CORECLKSPEEDCHANGE_DATA;

#define RGXFWIF_HWPERF_CTRL_BLKS_MAX	16

typedef struct _RGXFWIF_HWPERF_CTRL_BLKS_
{
	IMG_BOOL	bEnable;
	IMG_UINT32	ui32NumBlocks;                              /*!< Number of block IDs in the array */
	IMG_UINT16	aeBlockIDs[RGXFWIF_HWPERF_CTRL_BLKS_MAX];   /*!< Array of RGX_HWPERF_CNTBLK_ID values */
} RGXFWIF_HWPERF_CTRL_BLKS;


typedef struct _RGXFWIF_HWPERF_SELECT_CUSTOM_CNTRS_
{
	IMG_UINT16                      ui16CustomBlock;
	IMG_UINT16                      ui16NumCounters;
	PRGX_HWPERF_SELECT_CUSTOM_CNTRS sCustomCounterIDs;
} RGXFWIF_HWPERF_SELECT_CUSTOM_CNTRS;

typedef struct _RGXFWIF_ZSBUFFER_BACKING_DATA_
{
	RGXFWIF_DEV_VIRTADDR	sZSBufferFWDevVAddr; 				/*!< ZS-Buffer FW address */
	IMG_UINT32				bDone;								/*!< action backing/unbacking succeeded */
} RGXFWIF_ZSBUFFER_BACKING_DATA;

typedef struct
{
	IMG_UINT32 ui32IsolationPriorityThreshold;
} RGXFWIF_OSID_ISOLATION_GROUP_DATA;

/*
 * Flags to pass in the unused bits of the page size grow request
 */
#define RGX_FREELIST_GSDATA_RPM_RESTART_EN		(1 << 31)		/*!< Restart RPM after freelist grow command */
#define RGX_FREELIST_GSDATA_RPM_PAGECNT_MASK	(0x3FFFFFU)		/*!< Mask for page count. */

typedef struct _RGXFWIF_FREELIST_GS_DATA_
{
	RGXFWIF_DEV_VIRTADDR	sFreeListFWDevVAddr; 				/*!< Freelist FW address */
	IMG_UINT32				ui32DeltaSize;						/*!< Amount of the Freelist change */
	IMG_UINT32				ui32NewSize;						/*!< New amount of pages on the freelist */
} RGXFWIF_FREELIST_GS_DATA;

#define RGXFWIF_FREELISTS_RECONSTRUCTION_FAILED_FLAG 0x80000000

typedef struct _RGXFWIF_FREELISTS_RECONSTRUCTION_DATA_
{
	IMG_UINT32			ui32FreelistsCount;
	IMG_UINT32			aui32FreelistIDs[MAX_HW_TA3DCONTEXTS * RGXFW_MAX_FREELISTS];
} RGXFWIF_FREELISTS_RECONSTRUCTION_DATA;


typedef struct _RGXFWIF_SIGNAL_UPDATE_DATA_
{
	IMG_DEV_VIRTADDR RGXFW_ALIGN       sDevSignalAddress; /*!< device virtual address of the updated signal */
	PRGXFWIF_FWMEMCONTEXT              psFWMemContext; /*!< Memory context */
} UNCACHED_ALIGN RGXFWIF_SIGNAL_UPDATE_DATA;


typedef struct _RGXFWIF_WRITE_OFFSET_UPDATE_DATA_
{
	PRGXFWIF_FWCOMMONCONTEXT  psContext; /*!< Context to that may need to be resumed following write offset update */
} UNCACHED_ALIGN RGXFWIF_WRITE_OFFSET_UPDATE_DATA;

typedef struct _RGXFWIF_WORKEST_FWCCB_CMD_
{
	IMG_UINT64 RGXFW_ALIGN ui64ReturnDataIndex; /*!< Index for return data array */
	IMG_UINT64 RGXFW_ALIGN ui64CyclesTaken;     /*!< The cycles the workload took on the hardware */
} RGXFWIF_WORKEST_FWCCB_CMD;


/*!
 ******************************************************************************
 * Proactive DVFS Structures
 *****************************************************************************/
#define NUM_OPP_VALUES 16

typedef struct _PDVFS_OPP_
{
	IMG_UINT32			ui32Volt; /* V  */
	IMG_UINT32			ui32Freq; /* Hz */
} UNCACHED_ALIGN PDVFS_OPP;

typedef struct _RGXFWIF_PDVFS_OPP_
{
	PDVFS_OPP		asOPPValues[NUM_OPP_VALUES];
	IMG_UINT32		ui32MaxOPPPoint;
} UNCACHED_ALIGN RGXFWIF_PDVFS_OPP;

typedef struct _RGXFWIF_PDVFS_OPP_DATA_
{
	RGXFWIF_PDVFS_OPP sPDFVSOppInfo;
} UNCACHED_ALIGN RGXFWIF_PDVFS_OPP_DATA;

typedef struct _RGXFWIF_PDVFS_MAX_FREQ_DATA_
{
	IMG_UINT32 ui32MaxOPPPoint;
} UNCACHED_ALIGN RGXFWIF_PDVFS_MAX_FREQ_DATA;

/*!
 ******************************************************************************
 * Register configuration structures
 *****************************************************************************/

#define RGXFWIF_REG_CFG_MAX_SIZE 512

typedef enum _RGXFWIF_REGDATA_CMD_TYPE_
{
	RGXFWIF_REGCFG_CMD_ADD 				= 101,
	RGXFWIF_REGCFG_CMD_CLEAR 			= 102,
	RGXFWIF_REGCFG_CMD_ENABLE 			= 103,
	RGXFWIF_REGCFG_CMD_DISABLE 			= 104
} RGXFWIF_REGDATA_CMD_TYPE;

typedef struct _RGXFWIF_REGCONFIG_DATA_
{
	RGXFWIF_REGDATA_CMD_TYPE         eCmdType;
	RGXFWIF_REG_CFG_TYPE             eRegConfigType;
	RGXFWIF_REG_CFG_REC RGXFW_ALIGN  sRegConfig;

} RGXFWIF_REGCONFIG_DATA;

typedef struct _RGXFWIF_REG_CFG_
{
	IMG_UINT8            RGXFW_ALIGN  aui8NumRegsType[RGXFWIF_REG_CFG_TYPE_ALL];
	RGXFWIF_REG_CFG_REC	 RGXFW_ALIGN  asRegConfigs[RGXFWIF_REG_CFG_MAX_SIZE];
} UNCACHED_ALIGN RGXFWIF_REG_CFG;

typedef struct _RGXFWIF_REGISTER_GUESTOS_OFFSETS_
{
	IMG_UINT32                        ui32OSid;
	RGXFWIF_DEV_VIRTADDR RGXFW_ALIGN  sKCCBCtl;
	RGXFWIF_DEV_VIRTADDR              sKCCB;
	RGXFWIF_DEV_VIRTADDR              sFirmwareCCBCtl;
	RGXFWIF_DEV_VIRTADDR              sFirmwareCCB;
} UNCACHED_ALIGN RGXFWIF_REGISTER_GUESTOS_OFFSETS;

/* OSid Scheduling Priority Change */
typedef struct _RGXFWIF_OSID_PRIORITY_DATA_
{
	IMG_UINT32			ui32OSidNum;
	IMG_UINT32			ui32Priority;
} RGXFWIF_OSID_PRIORITY_DATA;

typedef struct
{
	IMG_UINT32 ui32OSid;
	RGXFWIF_OS_STATE_CHANGE eNewOSState;
} UNCACHED_ALIGN RGXFWIF_OS_STATE_CHANGE_DATA;

typedef struct
{
	PRGXFWIF_INIT		sOSInit;
}  RGXFW_ALIGN RGXFWIF_OS_CONFIG_DATA;

typedef enum _RGXFWIF_KCCB_CMD_TYPE_
{
	RGXFWIF_KCCB_CMD_KICK						= 101,
	RGXFWIF_KCCB_CMD_MMUCACHE					= 102,
	RGXFWIF_KCCB_CMD_BP							= 104,
	RGXFWIF_KCCB_CMD_SLCBPCTL   				= 106, /*!< slc bypass control. Requires sSLCBPCtlData. For validation */
	RGXFWIF_KCCB_CMD_SYNC       				= 107, /*!< host sync command. Requires sSyncData. */
	RGXFWIF_KCCB_CMD_SLCFLUSHINVAL				= 108, /*!< slc flush and invalidation request */
	RGXFWIF_KCCB_CMD_CLEANUP					= 109, /*!< Requests cleanup of a FW resource (type specified in the command data) */
	RGXFWIF_KCCB_CMD_POW						= 110, /*!< Power request */
	RGXFWIF_KCCB_CMD_HWPERF_UPDATE_CONFIG			= 111, /*!< Configure HWPerf events (to be generated) and HWPerf buffer address (if required) */
	RGXFWIF_KCCB_CMD_HWPERF_CONFIG_ENABLE_BLKS	= 112, /*!< Configure, clear and enable multiple HWPerf blocks */
	RGXFWIF_KCCB_CMD_HWPERF_CTRL_BLKS			= 113, /*!< Enable or disable multiple HWPerf blocks (reusing existing configuration) */
	RGXFWIF_KCCB_CMD_CORECLKSPEEDCHANGE			= 114, /*!< CORE clock speed change event */
	RGXFWIF_KCCB_CMD_ZSBUFFER_BACKING_UPDATE	= 115, /*!< Backing for on-demand ZS-Buffer done */
	RGXFWIF_KCCB_CMD_ZSBUFFER_UNBACKING_UPDATE	= 116, /*!< Unbacking for on-demand ZS-Buffer done */
	RGXFWIF_KCCB_CMD_FREELIST_GROW_UPDATE		= 117, /*!< Freelist Grow done */
	RGXFWIF_KCCB_CMD_FREELIST_SHRINK_UPDATE		= 118, /*!< Freelist Shrink done */
	RGXFWIF_KCCB_CMD_FREELISTS_RECONSTRUCTION_UPDATE	= 119, /*!< Freelists Reconstruction done */
	RGXFWIF_KCCB_CMD_HEALTH_CHECK               = 120, /*!< Health check request */
	RGXFWIF_KCCB_CMD_REGCONFIG                  = 121,
	RGXFWIF_KCCB_CMD_HWPERF_SELECT_CUSTOM_CNTRS = 122, /*!< Configure the custom counters for HWPerf */
	RGXFWIF_KCCB_CMD_HWPERF_CONFIG_ENABLE_BLKS_DIRECT	= 123, /*!< Configure, clear and enable multiple HWPerf blocks during the init process*/
	RGXFWIF_KCCB_CMD_LOGTYPE_UPDATE             = 124, /*!< Ask the firmware to update its cached ui32LogType value from the (shared) tracebuf control structure */
	RGXFWIF_KCCB_CMD_WORKEST_CLEAR_BUFFER		= 125,
	RGXFWIF_KCCB_CMD_PDVFS_PASS_OPP				= 126,
	RGXFWIF_KCCB_CMD_PDVFS_LIMIT_MAX_FREQ				= 127,
	RGXFWIF_KCCB_CMD_PDVFS_REQUEST_REACTIVE_UPDATE	= 129,
	RGXFWIF_KCCB_CMD_DOPPLER_MEMORY_GROW		= 130,

	RGXFWIF_KCCB_CMD_NOTIFY_SIGNAL_UPDATE       = 131, /*!< Informs the firmware that the host has performed a signal update */

	RGXFWIF_KCCB_CMD_NOTIFY_WRITE_OFFSET_UPDATE	= 132, /*!< Informs the firmware that the host has added more data to a CDM2 Circular Buffer */

	RGXFWIF_KCCB_CMD_OSID_PRIORITY_CHANGE       = 133, /*!< Changes the relative scheduling priority for a particular OSid. It can only be serviced for the Host DDK */
	RGXFWIF_KCCB_CMD_STATEFLAGS_CTRL            = 134, /*!< Set or clear firmware state flags */
	RGXFWIF_KCCB_CMD_HCS_SET_DEADLINE           = 135, /*!< Set hard context switching deadline */
	RGXFWIF_KCCB_CMD_OS_ISOLATION_GROUP_CHANGE  = 136, /*!< Changes the configuration of (or even disables) the OSid Isolation scheduling group. It can only be serviced for the Host DDK */
	RGXFWIF_KCCB_CMD_OS_ONLINE_STATE_CONFIGURE  = 137, /*!< Informs the FW that a Guest OS has come online / offline. It can only be serviced for the Host DDK */
	RGXFWIF_KCCB_CMD_OS_CFG_INIT                = 138, /*!< First kick of the DDK which initializes all OS specific data on the FW */
} RGXFWIF_KCCB_CMD_TYPE;

/* Kernel CCB command packet */
typedef struct _RGXFWIF_KCCB_CMD_
{
	RGXFWIF_KCCB_CMD_TYPE  eCmdType; /*!< Command type */
	RGXFWIF_DM             eDM;      /*!< DM associated with the command */

	union
	{
		RGXFWIF_KCCB_CMD_KICK_DATA			sCmdKickData;			/*!< Data for Kick command */
		RGXFWIF_MMUCACHEDATA				sMMUCacheData;			/*!< Data for MMUCACHE command */
		RGXFWIF_BPDATA						sBPData;				/*!< Data for Breakpoint Commands */
		RGXFWIF_SLCBPCTLDATA       			sSLCBPCtlData;  		/*!< Data for SLC Bypass Control */
		RGXFWIF_KCCB_CMD_SYNC_DATA 			sSyncData;          	/*!< Data for host sync commands */
		RGXFWIF_SLCFLUSHINVALDATA			sSLCFlushInvalData;		/*!< Data for SLC Flush/Inval commands */
		RGXFWIF_CLEANUP_REQUEST				sCleanupData; 			/*!< Data for cleanup commands */
		RGXFWIF_POWER_REQUEST				sPowData;				/*!< Data for power request commands */
		RGXFWIF_HWPERF_CTRL					sHWPerfCtrl;			/*!< Data for HWPerf control command */
		RGXFWIF_HWPERF_CONFIG_ENABLE_BLKS	sHWPerfCfgEnableBlks;	/*!< Data for HWPerf configure, clear and enable performance counter block command */
		RGXFWIF_HWPERF_CTRL_BLKS			sHWPerfCtrlBlks;		/*!< Data for HWPerf enable or disable performance counter block commands */
		RGXFWIF_HWPERF_SELECT_CUSTOM_CNTRS  sHWPerfSelectCstmCntrs; /*!< Data for HWPerf configure the custom counters to read */
		RGXFWIF_CORECLKSPEEDCHANGE_DATA		sCORECLKSPEEDCHANGEData;/*!< Data for CORE clock speed change */
		RGXFWIF_ZSBUFFER_BACKING_DATA		sZSBufferBackingData;	/*!< Feedback for Z/S Buffer backing/unbacking */
		RGXFWIF_FREELIST_GS_DATA			sFreeListGSData;		/*!< Feedback for Freelist grow/shrink */
		RGXFWIF_FREELISTS_RECONSTRUCTION_DATA	sFreeListsReconstructionData;	/*!< Feedback for Freelists reconstruction */
		RGXFWIF_REGCONFIG_DATA				sRegConfigData;			/*!< Data for custom register configuration */
		RGXFWIF_REGISTER_GUESTOS_OFFSETS    sRegisterGuestOsOffests;/*!< Data for registering a guestOS with the FW */
		RGXFWIF_SIGNAL_UPDATE_DATA          sSignalUpdateData;      /*!< Data for informing the FW about the signal update */
		RGXFWIF_WRITE_OFFSET_UPDATE_DATA    sWriteOffsetUpdateData; /*!< Data for informing the FW about the write offset update */
		RGXFWIF_PDVFS_OPP_DATA				sPDVFSOppData;
		RGXFWIF_PDVFS_MAX_FREQ_DATA			sPDVFSMaxFreqData;
		RGXFWIF_OSID_PRIORITY_DATA			sCmdOSidPriorityData;	/*!< Data for updating an OSid priority */
		RGXFWIF_STATEFLAG_CTRL				sStateFlagCtrl;			/*!< Data for StateFlag control command */
		RGXFWIF_HCS_CTL						sHCSCtrl;				/*!< Data for Hard Context Switching */
		RGXFWIF_OSID_ISOLATION_GROUP_DATA   sCmdOSidIsolationData;  /*!< Data for updating the OSid isolation group */
		RGXFWIF_OS_STATE_CHANGE_DATA        sCmdOSOnlineStateData;  /*!< Data for updating the Guest Online states */
		RGXFWIF_OS_CONFIG_DATA              sCmdOSConfigData;       /*!< Data for the OS-specific initialization part of the FW */
	} UNCACHED_ALIGN uCmdData;
} UNCACHED_ALIGN RGXFWIF_KCCB_CMD;

RGX_FW_STRUCT_SIZE_ASSERT(RGXFWIF_KCCB_CMD);

/*!
 ******************************************************************************
 * Firmware CCB command structure for RGX
 *****************************************************************************/

typedef struct _RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING_DATA_
{
	IMG_UINT32				ui32ZSBufferID;
	IMG_BOOL				bPopulate;
} RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING_DATA;

typedef struct _RGXFWIF_FWCCB_CMD_FREELIST_GS_DATA_
{
	IMG_UINT32				ui32FreelistID;
} RGXFWIF_FWCCB_CMD_FREELIST_GS_DATA;

typedef struct _RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION_DATA_
{
	IMG_UINT32			ui32FreelistsCount;
	IMG_UINT32			ui32HwrCounter;
	IMG_UINT32			aui32FreelistIDs[MAX_HW_TA3DCONTEXTS * RGXFW_MAX_FREELISTS];
} RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION_DATA;

typedef struct _RGXFWIF_FWCCB_CMD_CONTEXT_RESET_DATA_
{
	IMG_UINT32						ui32ServerCommonContextID;	/*!< Context affected by the reset */
	RGXFWIF_CONTEXT_RESET_REASON	eResetReason;				/*!< Reason for reset */
	IMG_UINT32						ui32ResetJobRef;			/*!< Job ref running at the time of reset */
	IMG_BOOL						bPageFault;					/*!< Did a page fault happen */
	IMG_UINT64 RGXFW_ALIGN			ui64PCAddress;				/*!< At what page catalog address */
} RGXFWIF_FWCCB_CMD_CONTEXT_RESET_DATA;

typedef enum _RGXFWIF_FWCCB_CMD_TYPE_
{
	RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING				= 101, 	/*!< Requests ZSBuffer to be backed with physical pages */
	RGXFWIF_FWCCB_CMD_ZSBUFFER_UNBACKING			= 102, 	/*!< Requests ZSBuffer to be unbacked */
	RGXFWIF_FWCCB_CMD_FREELIST_GROW					= 103, 	/*!< Requests an on-demand freelist grow/shrink */
	RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION		= 104, 	/*!< Requests freelists reconstruction */
	RGXFWIF_FWCCB_CMD_CONTEXT_RESET_NOTIFICATION	= 105,	/*!< Notifies host of a HWR event on a context */
	RGXFWIF_FWCCB_CMD_DEBUG_DUMP					= 106,	/*!< Requests an on-demand debug dump */
	RGXFWIF_FWCCB_CMD_UPDATE_STATS					= 107,	/*!< Requests an on-demand update on process stats */

	RGXFWIF_FWCCB_CMD_DOPPLER_MEMORY_GROW			= 108, 	/*!< Requests an on-demand RPM freelist grow */
	RGXFWIF_FWCCB_CMD_WORKLOAD_FINISHED				= 109,	/*!< Supplies data for the workload matching algorithm */
	RGXFWIF_FWCCB_CMD_CORE_CLK_RATE_CHANGE			= 110,
	RGXFWIF_FWCCB_CMD_PDVFS_FREEMEM					= 111,
} RGXFWIF_FWCCB_CMD_TYPE;

typedef enum
{
    RGXFWIF_FWCCB_CMD_UPDATE_NUM_PARTIAL_RENDERS=1,		/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32TotalNumPartialRenders stat */
    RGXFWIF_FWCCB_CMD_UPDATE_NUM_OUT_OF_MEMORY,			/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32TotalNumOutOfMemory stat */
    RGXFWIF_FWCCB_CMD_UPDATE_NUM_TA_STORES,				/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32NumTAStores stat */
    RGXFWIF_FWCCB_CMD_UPDATE_NUM_3D_STORES,				/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32Num3DStores stat */
    RGXFWIF_FWCCB_CMD_UPDATE_NUM_SH_STORES,				/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32NumSHStores stat */
    RGXFWIF_FWCCB_CMD_UPDATE_NUM_CDM_STORES				/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32NumCDMStores stat */
} RGXFWIF_FWCCB_CMD_UPDATE_STATS_TYPE;


/* Firmware CCB command packet */

typedef struct
{
    RGXFWIF_FWCCB_CMD_UPDATE_STATS_TYPE		eElementToUpdate;			/*!< Element to update */
    IMG_PID									pidOwner;					/*!< The pid of the process whose stats are being updated */
    IMG_INT32								i32AdjustmentValue;			/*!< Adjustment to be made to the statistic */
} RGXFWIF_FWCCB_CMD_UPDATE_STATS_DATA;
/*!
 ******************************************************************************
 * Workload Estimation Structures
 *****************************************************************************/

typedef struct
{
	IMG_UINT64 RGXFW_ALIGN /*uintptr_t DEVMEM_MEMDESC*/	ui64WorkloadDataMemdesc;
} RGXFWIF_FWCCB_CMD_WORKLOAD_FINISHED_DATA;

/*!
 ******************************************************************************
 * Proactive DVFS Structures
 *****************************************************************************/

typedef struct _RGXFWIF_FWCCB_CMD_CORE_CLK_RATE_CHANGE_DATA_
{
	IMG_UINT32 ui32CoreClkRate;
} UNCACHED_ALIGN RGXFWIF_FWCCB_CMD_CORE_CLK_RATE_CHANGE_DATA;

typedef struct _RGXFWIF_FWCCB_CMD_PDVFS_FREEMEM_DATA_
{
	IMG_UINT64 RGXFW_ALIGN ui64MemDesc;
} UNCACHED_ALIGN RGXFWIF_FWCCB_CMD_PDVFS_FREEMEM_DATA;

typedef struct _RGXFWIF_FWCCB_CMD_
{
	RGXFWIF_FWCCB_CMD_TYPE					eCmdType;	/*!< Command type */
	union
	{
		RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING_DATA				sCmdZSBufferBacking;			/*!< Data for Z/S-Buffer on-demand (un)backing*/
		RGXFWIF_FWCCB_CMD_FREELIST_GS_DATA					sCmdFreeListGS;					/*!< Data for on-demand freelist grow/shrink */
		RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION_DATA		sCmdFreeListsReconstruction;	/*!< Data for freelists reconstruction */
		RGXFWIF_FWCCB_CMD_CONTEXT_RESET_DATA				sCmdContextResetNotification;	/*!< Data for context reset notification */
		RGXFWIF_FWCCB_CMD_UPDATE_STATS_DATA                 sCmdUpdateStatsData;            /*!< Data for updating process stats */
		RGXFWIF_FWCCB_CMD_WORKLOAD_FINISHED_DATA			sCmdWorkEstWorkloadFinished;			/*!< Data for workload matching */
		RGXFWIF_FWCCB_CMD_CORE_CLK_RATE_CHANGE_DATA			sCmdCoreClkRateChange;
		RGXFWIF_FWCCB_CMD_PDVFS_FREEMEM_DATA				sCmdPDVFSFreeMem;
	} RGXFW_ALIGN uCmdData;
} RGXFW_ALIGN RGXFWIF_FWCCB_CMD;

RGX_FW_STRUCT_SIZE_ASSERT(RGXFWIF_FWCCB_CMD);

/*!
 ******************************************************************************
 * Signature and Checksums Buffer
 *****************************************************************************/
typedef struct _RGXFWIF_SIGBUF_CTL_
{
	PRGXFWIF_SIGBUFFER		sBuffer;			/*!< Ptr to Signature Buffer memory */
	IMG_UINT32				ui32LeftSizeInRegs;	/*!< Amount of space left for storing regs in the buffer */
} UNCACHED_ALIGN RGXFWIF_SIGBUF_CTL;

/*!
 ******************************************************************************
 * Updated configuration post FW data init.
 *****************************************************************************/
typedef struct _RGXFWIF_RUNTIME_CFG_
{
	IMG_UINT32         ui32ActivePMLatencyms;      /* APM latency in ms before signalling IDLE to the host */
	IMG_BOOL           bActivePMLatencyPersistant; /* If set, APM latency does not reset to system default each GPU power transition */
	IMG_UINT32         ui32CoreClockSpeed;         /* Core clock speed, currently only used to calculate timer ticks */
	IMG_UINT32         ui32DefaultDustsNumInit;    /* Last number of dusts change requested by the host */
	PRGXFWIF_HWPERFBUF sHWPerfBuf;                 /* On-demand allocated HWPerf buffer address, to be passed to the FW */
} RGXFWIF_RUNTIME_CFG;

/*!
 *****************************************************************************
 * Control data for RGX
 *****************************************************************************/

#define RGXFWIF_HWR_DEBUG_DUMP_ALL (99999)

#if defined(PDUMP)

#define RGXFWIF_PID_FILTER_MAX_NUM_PIDS 32

typedef enum _RGXFWIF_PID_FILTER_MODE_
{
	RGXFW_PID_FILTER_INCLUDE_ALL_EXCEPT,
	RGXFW_PID_FILTER_EXCLUDE_ALL_EXCEPT
} RGXFWIF_PID_FILTER_MODE;

typedef struct _RGXFWIF_PID_FILTER_ITEM_
{
	IMG_PID uiPID;
	IMG_UINT32 ui32OSID;
} RGXFW_ALIGN RGXFWIF_PID_FILTER_ITEM;

typedef struct _RGXFWIF_PID_FILTER_
{
	RGXFWIF_PID_FILTER_MODE eMode;
	/* each process in the filter list is specified by a PID and OS ID pair.
	 * each PID and OS pair is an item in the items array (asItems).
	 * if the array contains less than RGXFWIF_PID_FILTER_MAX_NUM_PIDS entries
	 * then it must be terminated by an item with pid of zero.
	 */
	RGXFWIF_PID_FILTER_ITEM asItems[RGXFWIF_PID_FILTER_MAX_NUM_PIDS];
} RGXFW_ALIGN RGXFWIF_PID_FILTER;
#endif

typedef struct _RGXFWIF_INIT_
{
	IMG_DEV_PHYADDR         RGXFW_ALIGN sFaultPhysAddr;

	IMG_DEV_VIRTADDR        RGXFW_ALIGN sPDSExecBase;
	IMG_DEV_VIRTADDR        RGXFW_ALIGN sUSCExecBase;
	IMG_DEV_VIRTADDR        RGXFW_ALIGN sResultDumpBase;
	IMG_DEV_VIRTADDR        RGXFW_ALIGN sDPXControlStreamBase;
	IMG_DEV_VIRTADDR        RGXFW_ALIGN sRTUHeapBase;
	IMG_DEV_VIRTADDR        RGXFW_ALIGN sTDMTPUYUVCeoffsHeapBase;

	IMG_BOOL                bFirstTA;
	IMG_BOOL                bFirstRender;
	IMG_BOOL                bFrameworkAfterInit;
	IMG_BOOL                bDisableFilterHWPerfCustomCounter;
	RGXFWIF_DEV_VIRTADDR    sPowerSync;
	IMG_UINT32              ui32FilterFlags;

	/* Kernel CCB */
	PRGXFWIF_CCB_CTL        psKernelCCBCtl;
	PRGXFWIF_CCB            psKernelCCB;

	/* Firmware CCB */
	PRGXFWIF_CCB_CTL        psFirmwareCCBCtl;
	PRGXFWIF_CCB            psFirmwareCCB;

	RGXFWIF_SIGBUF_CTL	asSigBufCtl[RGXFWIF_DM_DEFAULT_MAX];

	IMG_BOOL                bEnableLogging;
	IMG_UINT32              ui32ConfigFlags;        /*!< Configuration flags from host */
	IMG_UINT32              ui32BreakpointTemps;
	IMG_UINT32              ui32BreakpointShareds;
	IMG_UINT32              ui32HWRDebugDumpLimit;

	RGXFWIF_BIFTILINGMODE   eBifTilingMode;
	struct
	{
		IMG_UINT64 uiBase;
		IMG_UINT64 uiLen;
		IMG_UINT64 uiXStride;
	}                       RGXFW_ALIGN sBifTilingCfg[RGXFWIF_NUM_BIF_TILING_CONFIGS];

	PRGXFWIF_RUNTIME_CFG    sRuntimeCfg;

	PRGXFWIF_TRACEBUF       sTraceBufCtl;
	IMG_UINT64              RGXFW_ALIGN ui64HWPerfFilter;

	PRGXFWIF_HWRINFOBUF     sRGXFWIfHWRInfoBufCtl;
	PRGXFWIF_GPU_UTIL_FWCB  sGpuUtilFWCbCtl;
	PRGXFWIF_REG_CFG        sRegCfg;
	PRGXFWIF_HWPERF_CTL     sHWPerfCtl;

	RGXFWIF_DEV_VIRTADDR    sAlignChecks;

	/* Core clock speed at FW boot time */
	IMG_UINT32              ui32InitialCoreClockSpeed;

	/* APM latency in ms before signalling IDLE to the host */
	IMG_UINT32              ui32ActivePMLatencyms;

	/* Flag to be set by the Firmware after successful start */
	IMG_BOOL                bFirmwareStarted;

	IMG_UINT32              ui32MarkerVal;

	IMG_UINT32              ui32FirmwareStartedTimeStamp;

	IMG_UINT32              ui32JonesDisableMask;

	/* Compatibility checks to be populated by the Firmware */
	RGXFWIF_COMPCHECKS      sRGXCompChecks;

	RGXFWIF_DMA_ADDR        sCorememDataStore;

	FW_PERF_CONF            eFirmwarePerf;

	IMG_DEV_VIRTADDR        RGXFW_ALIGN sSLC3FenceDevVAddr;

	RGXFWIF_DEV_VIRTADDR    sT1Stack;

	RGXFWIF_PDVFS_OPP       sPDVFSOPPInfo;

	/**
	 * FW Pointer to memory containing core clock rate in Hz.
	 * Firmware (PDVFS) updates the memory when running on non primary FW thread
	 * to communicate to host driver.
	 */
	PRGXFWIF_CORE_CLK_RATE  sCoreClockRate;

#if defined(PDUMP)
	RGXFWIF_PID_FILTER      sPIDFilter;
#endif

	/* Workload Estimation Firmware CCB */
	PRGXFWIF_CCB_CTL        psWorkEstFirmwareCCBCtl;
	PRGXFWIF_CCB            psWorkEstFirmwareCCB;

} UNCACHED_ALIGN RGXFWIF_INIT;


/*!
 ******************************************************************************
 * Client CCB commands which are only required by the kernel
 *****************************************************************************/
typedef struct _RGXFWIF_CMD_PRIORITY_
{
    IMG_UINT32				ui32Priority;
} RGXFWIF_CMD_PRIORITY;

#endif /*  __RGX_FWIF_KM_H__ */

/******************************************************************************
 End of file (rgx_fwif_km.h)
******************************************************************************/


