/*************************************************************************/ /*!
@File			rgx_fwif.h
@Title          RGX firmware interface structures
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX firmware interface structures used by srvinit and server
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

#if !defined (__RGX_FWIF_H__)
#define __RGX_FWIF_H__

#include "rgx_meta.h"
#include "rgx_fwif_shared.h"

#include "pvr_tlcommon.h"

/*************************************************************************/ /*!
 Logging type
*/ /**************************************************************************/
#define RGXFWIF_LOG_TYPE_NONE			0x00000000
#define RGXFWIF_LOG_TYPE_TRACE			0x00000001
#define RGXFWIF_LOG_TYPE_GROUP_MAIN		0x00000002
#define RGXFWIF_LOG_TYPE_GROUP_MTS		0x00000004
#define RGXFWIF_LOG_TYPE_GROUP_CLEANUP	0x00000008
#define RGXFWIF_LOG_TYPE_GROUP_CSW		0x00000010
#define RGXFWIF_LOG_TYPE_GROUP_BIF		0x00000020
#define RGXFWIF_LOG_TYPE_GROUP_PM		0x00000040
#define RGXFWIF_LOG_TYPE_GROUP_RTD		0x00000080
#define RGXFWIF_LOG_TYPE_GROUP_SPM		0x00000100
#define RGXFWIF_LOG_TYPE_GROUP_POW		0x00000200
#define RGXFWIF_LOG_TYPE_GROUP_HWR		0x00000400
#define RGXFWIF_LOG_TYPE_GROUP_HWP		0x00000800
#define RGXFWIF_LOG_TYPE_GROUP_DEBUG	0x80000000
#define RGXFWIF_LOG_TYPE_GROUP_MASK		0x80000FFE
#define RGXFWIF_LOG_TYPE_MASK			0x80000FFF

/* String used in pvrdebug -h output */
#define RGXFWIF_LOG_GROUPS_STRING_LIST   "main,mts,cleanup,csw,bif,pm,rtd,spm,pow,hwr,hwp"

/* Table entry to map log group strings to log type value */
typedef struct {
	const IMG_CHAR* pszLogGroupName;
	IMG_UINT32      ui32LogGroupType;
} RGXFWIF_LOG_GROUP_MAP_ENTRY;

/*
  Macro for use with the RGXFWIF_LOG_GROUP_MAP_ENTRY type to create a lookup
  table where needed. Keep log group names short, no more than 20 chars.
*/
#define RGXFWIF_LOG_GROUP_NAME_VALUE_MAP { "main",    RGXFWIF_LOG_TYPE_GROUP_MAIN }, \
                                         { "mts",     RGXFWIF_LOG_TYPE_GROUP_MTS }, \
                                         { "cleanup", RGXFWIF_LOG_TYPE_GROUP_CLEANUP }, \
                                         { "csw",     RGXFWIF_LOG_TYPE_GROUP_CSW }, \
                                         { "bif",     RGXFWIF_LOG_TYPE_GROUP_BIF }, \
                                         { "pm",      RGXFWIF_LOG_TYPE_GROUP_PM }, \
                                         { "rtd",     RGXFWIF_LOG_TYPE_GROUP_RTD }, \
                                         { "spm",     RGXFWIF_LOG_TYPE_GROUP_SPM }, \
                                         { "pow",     RGXFWIF_LOG_TYPE_GROUP_POW }, \
                                         { "hwr",     RGXFWIF_LOG_TYPE_GROUP_HWR }, \
                                         { "hwp",     RGXFWIF_LOG_TYPE_GROUP_HWP }, \
                                         { "debug",   RGXFWIF_LOG_TYPE_GROUP_DEBUG }


/* Used in print statements to display log group state, one %s per group defined */
#define RGXFWIF_LOG_ENABLED_GROUPS_LIST_PFSPEC  "%s%s%s%s%s%s%s%s%s%s%s%s"

/* Used in a print statement to display log group state, one per group */
#define RGXFWIF_LOG_ENABLED_GROUPS_LIST(types)  (((types) & RGXFWIF_LOG_TYPE_GROUP_MAIN)	?("main ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_MTS)		?("mts ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_CLEANUP)	?("cleanup ")	:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_CSW)		?("csw ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_BIF)		?("bif ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_PM)		?("pm ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_RTD)		?("rtd ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_SPM)		?("spm ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_POW)		?("pow ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_HWR)		?("hwr ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_HWP)		?("hwp ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_DEBUG)	?("debug ")		:(""))


/*! Logging function */
typedef IMG_VOID (*PFN_RGXFW_LOG) (const IMG_CHAR* pszFmt, ...);

/*!
 ******************************************************************************
 * HWPERF
 *****************************************************************************/
/* Size of the Firmware L1 HWPERF buffer in bytes (256KB). Accessed by the
 * Firmware and host driver. */
#define RGXFW_HWPERF_L1_SIZE_MIN		(0x004000)
#define RGXFW_HWPERF_L1_SIZE_DEFAULT    (0x040000)
#define RGXFW_HWPERF_L1_SIZE_MAX        (0xC00000)
/* This padding value must always be greater than or equal to 
 * RGX_HWPERF_V2_MAX_PACKET_SIZE for all valid BVNCs. This is asserted in 
 * rgxsrvinit.c. This macro is defined with a constant to avoid a KM 
 * dependency */
#define RGXFW_HWPERF_L1_PADDING_DEFAULT (0x800)

/*!
 ******************************************************************************
 * Trace Buffer
 *****************************************************************************/

/*! Number of elements on each line when dumping the trace buffer */
#define RGXFW_TRACE_BUFFER_LINESIZE	(30)

/*! Total size of RGXFWIF_TRACEBUF dword (needs to be a multiple of RGXFW_TRACE_BUFFER_LINESIZE) */
#define RGXFW_TRACE_BUFFER_SIZE		(400*RGXFW_TRACE_BUFFER_LINESIZE)
#define RGXFW_TRACE_BUFFER_ASSERT_SIZE 200
#define RGXFW_THREAD_NUM 1

#define RGXFW_POLL_TYPE_SET 0x80000000

typedef struct _RGXFWIF_ASSERTBUF_
{
	IMG_CHAR	szPath[RGXFW_TRACE_BUFFER_ASSERT_SIZE];
	IMG_CHAR	szInfo[RGXFW_TRACE_BUFFER_ASSERT_SIZE];
	IMG_UINT32	ui32LineNum;
}RGXFWIF_ASSERTBUF;

typedef struct _RGXFWIF_TRACEBUF_SPACE_
{
	IMG_UINT32			ui32TracePointer;
	IMG_UINT32			aui32TraceBuffer[RGXFW_TRACE_BUFFER_SIZE];
	RGXFWIF_ASSERTBUF	sAssertBuf;
} RGXFWIF_TRACEBUF_SPACE;

#define RGXFWIF_POW_STATES \
  X(RGXFWIF_POW_OFF)			/* idle and handshaked with the host (ready to full power down) */ \
  X(RGXFWIF_POW_ON)				/* running HW mds */ \
  X(RGXFWIF_POW_FORCED_IDLE)	/* forced idle */ \
  X(RGXFWIF_POW_IDLE)			/* idle waiting for host handshake */

typedef enum _RGXFWIF_POW_STATE_
{
#define X(NAME) NAME,
	RGXFWIF_POW_STATES
#undef X
} RGXFWIF_POW_STATE;

/* Firmware HWR states */
#define RGXFWIF_HWR_HARDWARE_OK		(0x1 << 0)	/*!< Tells if the HW state is ok or locked up */
#define RGXFWIF_HWR_FREELIST_OK		(0x1 << 1)	/*!< Tells if the freelists are ok or being reconstructed */
#define RGXFWIF_HWR_ANALYSIS_DONE	(0x1 << 2)	/*!< Tells if the analysis of a GPU lockup has already been performed */
#define RGXFWIF_HWR_GENERAL_LOCKUP	(0x1 << 3)	/*!< Tells if a DM unrelated lockup has been detected */
typedef IMG_UINT32 RGXFWIF_HWR_STATEFLAGS;

/* Firmware per-DM HWR states */
#define RGXFWIF_DM_STATE_WORKING 					(0x00)		/*!< DM is working if all flags are cleared */
#define RGXFWIF_DM_STATE_READY_FOR_HWR 				(0x1 << 0)	/*!< DM is idle and ready for HWR */
#define RGXFWIF_DM_STATE_NEEDS_FL_RECONSTRUCTION	(0x1 << 1)	/*!< DM need FL reconstruction before resuming processing */
#define RGXFWIF_DM_STATE_NEEDS_SKIP					(0x1 << 2)	/*!< DM need to skip to next cmd before resuming processing */
#define RGXFWIF_DM_STATE_NEEDS_PR_CLEANUP			(0x1 << 3)	/*!< DM need partial render cleanup before resuming processing */
#define RGXFWIF_DM_STATE_NEEDS_TRACE_CLEAR			(0x1 << 4)	/*!< DM need to increment Recovery Count once fully recovered */
#define RGXFWIF_DM_STATE_GUILTY_LOCKUP				(0x1 << 5)	/*!< DM was identified as locking up and causing HWR */
#define RGXFWIF_DM_STATE_INNOCENT_LOCKUP			(0x1 << 6)	/*!< DM was innocently affected by another lockup which caused HWR */
#define RGXFWIF_DM_STATE_GUILTY_OVERRUNING			(0x1 << 7)	/*!< DM was identified as over-running and causing HWR */
#define RGXFWIF_DM_STATE_INNOCENT_OVERRUNING		(0x1 << 8)	/*!< DM was innocently affected by another DM over-running which caused HWR */
typedef IMG_UINT32 RGXFWIF_HWR_RECOVERYFLAGS;

typedef struct _RGXFWIF_TRACEBUF_
{
    IMG_UINT32				ui32LogType;
	RGXFWIF_POW_STATE		ePowState;
	RGXFWIF_TRACEBUF_SPACE	sTraceBuf[RGXFW_THREAD_NUM];

	IMG_UINT16				aui16HwrDmLockedUpCount[RGXFWIF_DM_MAX];
	IMG_UINT16				aui16HwrDmOverranCount[RGXFWIF_DM_MAX];
	IMG_UINT16				aui16HwrDmRecoveredCount[RGXFWIF_DM_MAX];
	IMG_UINT16				aui16HwrDmFalseDetectCount[RGXFWIF_DM_MAX];
	IMG_UINT32				ui32HwrCounter;
	RGXFWIF_DEV_VIRTADDR	apsHwrDmFWCommonContext[RGXFWIF_DM_MAX];

	IMG_UINT32				aui32CrPollAddr[RGXFW_THREAD_NUM];
	IMG_UINT32				aui32CrPollMask[RGXFW_THREAD_NUM];

	RGXFWIF_HWR_STATEFLAGS		ui32HWRStateFlags;
	RGXFWIF_HWR_RECOVERYFLAGS	aui32HWRRecoveryFlags[RGXFWIF_HWDM_MAX];

	volatile IMG_UINT32		ui32HWPerfRIdx;
	volatile IMG_UINT32		ui32HWPerfWIdx;
	volatile IMG_UINT32		ui32HWPerfWrapCount;
	IMG_UINT32				ui32HWPerfSize;      /* Constant after setup, needed in FW */
	IMG_UINT32				ui32HWPerfDropCount; /* The number of times the FW drops a packet due to buffer full */
	
	/* These next three items are only valid at runtime when the FW is built
	 * with RGX_HWPERF_UTILIZATION defined in rgxfw_hwperf.c */
	IMG_UINT32				ui32HWPerfUt;        /* Buffer utilisation, high watermark of bytes in use */
	IMG_UINT32				ui32FirstDropOrdinal;/* The ordinal of the first packet the FW dropped */
	IMG_UINT32              ui32LastDropOrdinal; /* The ordinal of the last packet the FW dropped */

	IMG_UINT32				ui32InterruptCount;
	IMG_UINT32				ui32KCCBCmdsExecuted;
    IMG_UINT64 RGXFW_ALIGN	ui64StartIdleTime;
} RGXFWIF_TRACEBUF;

/*!
 ******************************************************************************
 * GPU Utilization FW CB
 *****************************************************************************/
#define RGXFWIF_GPU_STATS_WINDOW_SIZE_US			500000			/*!< Time window considered for active/idle/blocked statistics */
#define RGXFWIF_GPU_STATS_STATE_CHG_PER_SEC			1000			/*!< Expected number of maximum GPU state changes per second */
#define RGXFWIF_GPU_STATS_MAX_VALUE_OF_STATE		10000

#define RGXFWIF_GPU_UTIL_FWCB_SIZE	((RGXFWIF_GPU_STATS_WINDOW_SIZE_US * RGXFWIF_GPU_STATS_STATE_CHG_PER_SEC) / 1000000)

#define RGXFWIF_GPU_UTIL_FWCB_TYPE_CRTIME		IMG_UINT64_C(0x0)
#define RGXFWIF_GPU_UTIL_FWCB_TYPE_POWER_ON		IMG_UINT64_C(0x1)
#define RGXFWIF_GPU_UTIL_FWCB_TYPE_POWER_OFF	IMG_UINT64_C(0x2)
#define RGXFWIF_GPU_UTIL_FWCB_TYPE_END_CRTIME	IMG_UINT64_C(0x3)
#define RGXFWIF_GPU_UTIL_FWCB_TYPE_MASK			IMG_UINT64_C(0xC000000000000000)
#define RGXFWIF_GPU_UTIL_FWCB_TYPE_SHIFT		(62)

#define RGXFWIF_GPU_UTIL_FWCB_STATE_ACTIVE_LOW	IMG_UINT64_C(0x0)
#define RGXFWIF_GPU_UTIL_FWCB_STATE_IDLE		IMG_UINT64_C(0x1)
#define RGXFWIF_GPU_UTIL_FWCB_STATE_ACTIVE_HIGH	IMG_UINT64_C(0x2)
#define RGXFWIF_GPU_UTIL_FWCB_STATE_BLOCKED		IMG_UINT64_C(0x3)
#define RGXFWIF_GPU_UTIL_FWCB_STATE_MASK		IMG_UINT64_C(0x3000000000000000)
#define RGXFWIF_GPU_UTIL_FWCB_STATE_SHIFT		(60)

#define RGXFWIF_GPU_UTIL_FWCB_ID_MASK			IMG_UINT64_C(0x0FFF000000000000)
#define RGXFWIF_GPU_UTIL_FWCB_ID_SHIFT			(48)

#define RGXFWIF_GPU_UTIL_FWCB_CR_TIMER_MASK		IMG_UINT64_C(0x0000FFFFFFFFFFFF)
#define RGXFWIF_GPU_UTIL_FWCB_OS_TIMER_MASK		IMG_UINT64_C(0x0FFFFFFFFFFFFFFF)
#define RGXFWIF_GPU_UTIL_FWCB_TIMER_SHIFT		(0)

#define RGXFWIF_GPU_UTIL_FWCB_ENTRY_TYPE(entry)		(((entry)&RGXFWIF_GPU_UTIL_FWCB_TYPE_MASK)>>RGXFWIF_GPU_UTIL_FWCB_TYPE_SHIFT)
#define RGXFWIF_GPU_UTIL_FWCB_ENTRY_STATE(entry)	(((entry)&RGXFWIF_GPU_UTIL_FWCB_STATE_MASK)>>RGXFWIF_GPU_UTIL_FWCB_STATE_SHIFT)
#define RGXFWIF_GPU_UTIL_FWCB_ENTRY_ID(entry)		(((entry)&RGXFWIF_GPU_UTIL_FWCB_ID_MASK)>>RGXFWIF_GPU_UTIL_FWCB_ID_SHIFT)
#define RGXFWIF_GPU_UTIL_FWCB_ENTRY_CR_TIMER(entry)	(((entry)&RGXFWIF_GPU_UTIL_FWCB_CR_TIMER_MASK)>>RGXFWIF_GPU_UTIL_FWCB_TIMER_SHIFT)
#define RGXFWIF_GPU_UTIL_FWCB_ENTRY_OS_TIMER(entry)	(((entry)&RGXFWIF_GPU_UTIL_FWCB_OS_TIMER_MASK)>>RGXFWIF_GPU_UTIL_FWCB_TIMER_SHIFT)

/* It can never happen that the GPU is reported as powered off and active at the same time,
 * use this combination to mark the entry as reserved */
#define RGXFWIF_GPU_UTIL_FWCB_RESERVED	\
	( (RGXFWIF_GPU_UTIL_FWCB_TYPE_POWER_OFF <<  RGXFWIF_GPU_UTIL_FWCB_TYPE_SHIFT) |	\
	  (RGXFWIF_GPU_UTIL_FWCB_STATE_ACTIVE_LOW << RGXFWIF_GPU_UTIL_FWCB_STATE_SHIFT) )

#define RGXFWIF_GPU_UTIL_FWCB_ENTRY_ADD(cb, crtimer, state) do {														\
		/* Combine all the information about current state transition into a single 64-bit word */						\
		(cb)->aui64CB[(cb)->ui32WriteOffset] =												\
			(((IMG_UINT64)(crtimer) << RGXFWIF_GPU_UTIL_FWCB_TIMER_SHIFT) & RGXFWIF_GPU_UTIL_FWCB_CR_TIMER_MASK) |		\
			(((IMG_UINT64)(state) << RGXFWIF_GPU_UTIL_FWCB_STATE_SHIFT) & RGXFWIF_GPU_UTIL_FWCB_STATE_MASK) |			\
			(((IMG_UINT64)(cb)->ui32CurrentDVFSId << RGXFWIF_GPU_UTIL_FWCB_ID_SHIFT) & RGXFWIF_GPU_UTIL_FWCB_ID_MASK);	\
		/* Make sure the value is written to the memory before advancing write offset */								\
		RGXFW_MEM_FENCE();																								\
		/* Advance the CB write offset */																				\
		(cb)->ui32WriteOffset++;																						\
		if((cb)->ui32WriteOffset >= RGXFWIF_GPU_UTIL_FWCB_SIZE)															\
		{																												\
			(cb)->ui32WriteOffset = 0;																					\
		}																												\
		/* Cache current transition in cached memory */																	\
		(cb)->ui32LastGpuUtilState = (state);																			\
	} while (0)


/* The timer correlation array must be big enough to ensure old entries won't be
 * overwritten before all the HWPerf events linked to those entries are processed
 * by the MISR. The update frequency of this array depends on how fast the system
 * can change state (basically how small the APM latency is) and perform DVFS transitions.
 *
 * The minimum size is 2 (not 1) to avoid race conditions between the FW reading
 * an entry while the Host is updating it. With 2 entries in the worst case the FW
 * will read old data, which is still quite ok if the Host is updating the timer
 * correlation at that time.
 */
#define RGXFWIF_TIME_CORR_ARRAY_SIZE	256

typedef IMG_UINT64 RGXFWIF_GPU_UTIL_FWCB_ENTRY;

typedef struct _RGXFWIF_GPU_UTIL_FWCB_
{
	RGXFWIF_TIME_CORR	sTimeCorr[RGXFWIF_TIME_CORR_ARRAY_SIZE];
	IMG_UINT32			ui32TimeCorrCurrent;
	IMG_UINT32			ui32WriteOffset;
	IMG_UINT32			ui32LastGpuUtilState;
	IMG_UINT32			ui32CurrentDVFSId;
	RGXFWIF_GPU_UTIL_FWCB_ENTRY	RGXFW_ALIGN aui64CB[RGXFWIF_GPU_UTIL_FWCB_SIZE];
} RGXFWIF_GPU_UTIL_FWCB;

/* HWR Data */
typedef enum _RGX_HWRTYPE_
{
	RGX_HWRTYPE_UNKNOWNFAILURE 	= 0,
	RGX_HWRTYPE_OVERRUN 		= 1,
	RGX_HWRTYPE_POLLFAILURE 	= 2,
#if !defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
	RGX_HWRTYPE_BIF0FAULT	 	= 3,
	RGX_HWRTYPE_BIF1FAULT	 	= 4,
#if defined(RGX_FEATURE_CLUSTER_GROUPING)
	RGX_HWRTYPE_TEXASBIF0FAULT	= 5,
#endif
#else
	RGX_HWRTYPE_MMUFAULT	 	= 6,
	RGX_HWRTYPE_MMUMETAFAULT	= 7,
#endif
} RGX_HWRTYPE;

#define RGXFWIF_BIFFAULTBIT_GET(ui32BIFMMUStatus) \
		((ui32BIFMMUStatus & ~RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_FAULT_CLRMSK) >> RGX_CR_BIF_FAULT_BANK0_MMU_STATUS_FAULT_SHIFT)
#define RGXFWIF_MMUFAULTBIT_GET(ui32BIFMMUStatus) \
		((ui32BIFMMUStatus & ~RGX_CR_MMU_FAULT_STATUS_FAULT_CLRMSK) >> RGX_CR_MMU_FAULT_STATUS_FAULT_SHIFT)

#define RGXFWIF_HWRTYPE_BIF_BANK_GET(eHWRType) ((eHWRType == RGX_HWRTYPE_BIF0FAULT) ? 0 : 1 )

typedef struct _RGX_BIFINFO_
{
	IMG_UINT64	RGXFW_ALIGN		ui64BIFReqStatus;
	IMG_UINT64	RGXFW_ALIGN		ui64BIFMMUStatus;
	IMG_UINT64	RGXFW_ALIGN		ui64PCAddress; /*!< phys address of the page catalogue */
} RGX_BIFINFO;

typedef struct _RGX_MMUINFO_
{
	IMG_UINT64	RGXFW_ALIGN		ui64MMUStatus;
} RGX_MMUINFO;

typedef struct _RGX_POLLINFO_
{
	IMG_UINT32	ui32ThreadNum;
	IMG_UINT32 	ui32CrPollAddr;
	IMG_UINT32 	ui32CrPollMask;
} RGX_POLLINFO;

typedef struct _RGX_HWRINFO_
{
	union
	{
		RGX_BIFINFO		sBIFInfo;
		RGX_MMUINFO		sMMUInfo;
		RGX_POLLINFO	sPollInfo;
	} uHWRData;

	IMG_UINT64	RGXFW_ALIGN		ui64CRTimer;
	IMG_UINT32					ui32FrameNum;
	IMG_UINT32					ui32PID;
	IMG_UINT32					ui32ActiveHWRTData;
	IMG_UINT32					ui32HWRNumber;
	IMG_UINT32					ui32EventStatus;
	IMG_UINT32					ui32HWRRecoveryFlags;
	RGX_HWRTYPE 				eHWRType;

	RGXFWIF_DM					eDM;
} RGX_HWRINFO;

#define RGXFWIF_HWINFO_MAX_FIRST 8							/* Number of first HWR logs recorded (never overwritten by newer logs) */
#define RGXFWIF_HWINFO_MAX_LAST 8							/* Number of latest HWR logs (older logs are overwritten by newer logs) */
#define RGXFWIF_HWINFO_MAX (RGXFWIF_HWINFO_MAX_FIRST + RGXFWIF_HWINFO_MAX_LAST)	/* Total number of HWR logs stored in a buffer */
#define RGXFWIF_HWINFO_LAST_INDEX (RGXFWIF_HWINFO_MAX - 1)	/* Index of the last log in the HWR log buffer */
typedef struct _RGXFWIF_HWRINFOBUF_
{
	RGX_HWRINFO sHWRInfo[RGXFWIF_HWINFO_MAX];

	IMG_UINT32	ui32FirstCrPollAddr[RGXFW_THREAD_NUM];
	IMG_UINT32	ui32FirstCrPollMask[RGXFW_THREAD_NUM];
	IMG_UINT32	ui32WriteIndex;
	IMG_BOOL	bDDReqIssued;
} RGXFWIF_HWRINFOBUF;

/*! RGX firmware Init Config Data */
#define RGXFWIF_INICFG_CTXSWITCH_TA_EN		(0x1 << 0)
#define RGXFWIF_INICFG_CTXSWITCH_3D_EN		(0x1 << 1)
#define RGXFWIF_INICFG_CTXSWITCH_CDM_EN		(0x1 << 2)
#define RGXFWIF_INICFG_CTXSWITCH_MODE_RAND	(0x1 << 3)
#define RGXFWIF_INICFG_CTXSWITCH_SRESET_EN	(0x1 << 4)
#define RGXFWIF_INICFG_RSVD					(0x1 << 5)
#define RGXFWIF_INICFG_POW_RASCALDUST		(0x1 << 6)
#define RGXFWIF_INICFG_HWPERF_EN			(0x1 << 7)
#define RGXFWIF_INICFG_HWR_EN				(0x1 << 8)
#define RGXFWIF_INICFG_CHECK_MLIST_EN		(0x1 << 9)
#define RGXFWIF_INICFG_DISABLE_CLKGATING_EN (0x1 << 10)
#define RGXFWIF_INICFG_POLL_COUNTERS_EN		(0x1 << 11)
#define RGXFWIF_INICFG_VDM_CTX_STORE_MODE_INDEX		(RGX_CR_VDM_CONTEXT_STORE_MODE_MODE_INDEX << 12)
#define RGXFWIF_INICFG_VDM_CTX_STORE_MODE_INSTANCE	(RGX_CR_VDM_CONTEXT_STORE_MODE_MODE_INSTANCE << 12)
#define RGXFWIF_INICFG_VDM_CTX_STORE_MODE_LIST		(RGX_CR_VDM_CONTEXT_STORE_MODE_MODE_LIST << 12)
#define RGXFWIF_INICFG_VDM_CTX_STORE_MODE_CLRMSK	(0xFFFFCFFFU)
#define RGXFWIF_INICFG_VDM_CTX_STORE_MODE_SHIFT		(12)
#define RGXFWIF_INICFG_SHG_BYPASS_EN		(0x1 << 14)
#define RGXFWIF_INICFG_RTU_BYPASS_EN		(0x1 << 15)
#define RGXFWIF_INICFG_REGCONFIG_EN		(0x1 << 16)
#define RGXFWIF_INICFG_ASSERT_ON_OUTOFMEMORY	(0x1 << 17)
#define RGXFWIF_INICFG_HWP_DISABLE_FILTER  (0x1 << 18)
#define RGXFWIF_INICFG_ALL					(0x0007FFDFU)
#define RGXFWIF_SRVCFG_DISABLE_PDP_EN 		(0x1 << 31)
#define RGXFWIF_SRVCFG_ALL					(0x80000000U)
#define RGXFWIF_FILTCFG_TRUNCATE_HALF		(0x1 << 3)
#define RGXFWIF_FILTCFG_TRUNCATE_INT		(0x1 << 2)
#define RGXFWIF_FILTCFG_NEW_FILTER_MODE		(0x1 << 1)

#define RGXFWIF_INICFG_CTXSWITCH_DM_ALL		(RGXFWIF_INICFG_CTXSWITCH_TA_EN | \
											 RGXFWIF_INICFG_CTXSWITCH_3D_EN | \
											 RGXFWIF_INICFG_CTXSWITCH_CDM_EN)

#define RGXFWIF_INICFG_CTXSWITCH_CLRMSK		~(RGXFWIF_INICFG_CTXSWITCH_DM_ALL | \
											 RGXFWIF_INICFG_CTXSWITCH_MODE_RAND | \
											 RGXFWIF_INICFG_CTXSWITCH_SRESET_EN)

typedef enum
{
	RGX_ACTIVEPM_FORCE_OFF = 0,
	RGX_ACTIVEPM_FORCE_ON = 1,
	RGX_ACTIVEPM_DEFAULT = 3
} RGX_ACTIVEPM_CONF;

/*!
 ******************************************************************************
 * Querying DM state
 *****************************************************************************/

typedef enum _RGXFWIF_DM_STATE_
{
	RGXFWIF_DM_STATE_NORMAL			= 0,
	RGXFWIF_DM_STATE_LOCKEDUP		= 1,

} RGXFWIF_DM_STATE;

typedef struct
{
	IMG_UINT16  ui16RegNum;				/*!< Register number */
	IMG_UINT16  ui16IndirectRegNum;		/*!< Indirect register number (or 0 if not used) */
	IMG_UINT16  ui16IndirectStartVal;	/*!< Start value for indirect register */
	IMG_UINT16  ui16IndirectEndVal;		/*!< End value for indirect register */
} RGXFW_REGISTER_LIST;

#endif /*  __RGX_FWIF_H__ */

/******************************************************************************
 End of file (rgx_fwif.h)
******************************************************************************/

