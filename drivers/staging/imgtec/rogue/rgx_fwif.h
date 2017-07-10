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

#include "rgx_firmware_processor.h"
#include "rgx_fwif_shared.h"

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
#define RGXFWIF_LOG_TYPE_GROUP_RPM		0x00001000
#define RGXFWIF_LOG_TYPE_GROUP_DMA		0x00002000
#define RGXFWIF_LOG_TYPE_GROUP_DEBUG	0x80000000
#define RGXFWIF_LOG_TYPE_GROUP_MASK		0x80003FFE
#define RGXFWIF_LOG_TYPE_MASK			0x80003FFF

/* String used in pvrdebug -h output */
#define RGXFWIF_LOG_GROUPS_STRING_LIST   "main,mts,cleanup,csw,bif,pm,rtd,spm,pow,hwr,hwp,rpm,dma,debug"

/* Table entry to map log group strings to log type value */
typedef struct {
	const IMG_CHAR* pszLogGroupName;
	IMG_UINT32      ui32LogGroupType;
} RGXFWIF_LOG_GROUP_MAP_ENTRY;

/*
  Macro for use with the RGXFWIF_LOG_GROUP_MAP_ENTRY type to create a lookup
  table where needed. Keep log group names short, no more than 20 chars.
*/
#define RGXFWIF_LOG_GROUP_NAME_VALUE_MAP { "none",    RGXFWIF_LOG_TYPE_NONE }, \
                                         { "main",    RGXFWIF_LOG_TYPE_GROUP_MAIN }, \
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
                                         { "rpm",     RGXFWIF_LOG_TYPE_GROUP_RPM }, \
                                         { "dma",     RGXFWIF_LOG_TYPE_GROUP_DMA }, \
                                         { "debug",   RGXFWIF_LOG_TYPE_GROUP_DEBUG }


/* Used in print statements to display log group state, one %s per group defined */
#define RGXFWIF_LOG_ENABLED_GROUPS_LIST_PFSPEC  "%s%s%s%s%s%s%s%s%s%s%s%s%s%s"

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
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_RPM)		?("rpm ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_DMA)		?("dma ")		:("")),		\
                                                (((types) & RGXFWIF_LOG_TYPE_GROUP_DEBUG)	?("debug ")		:(""))


/*! Logging function */
typedef void (*PFN_RGXFW_LOG) (const IMG_CHAR* pszFmt, ...);


/************************************************************************
* RGX FW signature checks
************************************************************************/
#define RGXFW_SIG_BUFFER_SIZE_MIN       (1024)

/*!
 ******************************************************************************
 * HWPERF
 *****************************************************************************/
/* Size of the Firmware L1 HWPERF buffer in bytes (2MB). Accessed by the
 * Firmware and host driver. */
#define RGXFW_HWPERF_L1_SIZE_MIN        (16U)
#define RGXFW_HWPERF_L1_SIZE_DEFAULT    (2048U)
#define RGXFW_HWPERF_L1_SIZE_MAX        (12288U)

/* This padding value must always be large enough to hold the biggest
 * variable sized packet. */
#define RGXFW_HWPERF_L1_PADDING_DEFAULT (RGX_HWPERF_MAX_PACKET_SIZE)


/*!
 ******************************************************************************
 * Trace Buffer
 *****************************************************************************/

/*! Number of elements on each line when dumping the trace buffer */
#define RGXFW_TRACE_BUFFER_LINESIZE	(30)

/*! Total size of RGXFWIF_TRACEBUF dword (needs to be a multiple of RGXFW_TRACE_BUFFER_LINESIZE) */
#define RGXFW_TRACE_BUFFER_SIZE		(400*RGXFW_TRACE_BUFFER_LINESIZE)
#define RGXFW_TRACE_BUFFER_ASSERT_SIZE 200
#if defined(RGXFW_META_SUPPORT_2ND_THREAD)
#define RGXFW_THREAD_NUM 2
#else
#define RGXFW_THREAD_NUM 1
#endif

#define RGXFW_POLL_TYPE_SET 0x80000000

typedef struct _RGXFWIF_ASSERTBUF_
{
	IMG_CHAR	szPath[RGXFW_TRACE_BUFFER_ASSERT_SIZE];
	IMG_CHAR	szInfo[RGXFW_TRACE_BUFFER_ASSERT_SIZE];
	IMG_UINT32	ui32LineNum;
} UNCACHED_ALIGN RGXFWIF_ASSERTBUF;

typedef struct _RGXFWIF_TRACEBUF_SPACE_
{
	IMG_UINT32			ui32TracePointer;

#if defined (RGX_FIRMWARE)
	IMG_UINT32 *pui32RGXFWIfTraceBuffer;		/* To be used by firmware for writing into trace buffer */
#else
	RGXFWIF_DEV_VIRTADDR pui32RGXFWIfTraceBuffer;
#endif
	IMG_PUINT32             pui32TraceBuffer;	/* To be used by host when reading from trace buffer */

	RGXFWIF_ASSERTBUF	sAssertBuf;
} UNCACHED_ALIGN RGXFWIF_TRACEBUF_SPACE;

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
#define RGXFWIF_HWR_ANALYSIS_DONE	(0x1 << 2)	/*!< Tells if the analysis of a GPU lockup has already been performed */
#define RGXFWIF_HWR_GENERAL_LOCKUP	(0x1 << 3)	/*!< Tells if a DM unrelated lockup has been detected */
#define RGXFWIF_HWR_DM_RUNNING_OK	(0x1 << 4)	/*!< Tells if at least one DM is running without being close to a lockup */
#define RGXFWIF_HWR_DM_STALLING		(0x1 << 5)	/*!< Tells if at least one DM is close to lockup */
typedef IMG_UINT32 RGXFWIF_HWR_STATEFLAGS;

/* Firmware per-DM HWR states */
#define RGXFWIF_DM_STATE_WORKING 					(0x00)		/*!< DM is working if all flags are cleared */
#define RGXFWIF_DM_STATE_READY_FOR_HWR 				(0x1 << 0)	/*!< DM is idle and ready for HWR */
#define RGXFWIF_DM_STATE_NEEDS_SKIP					(0x1 << 2)	/*!< DM need to skip to next cmd before resuming processing */
#define RGXFWIF_DM_STATE_NEEDS_PR_CLEANUP			(0x1 << 3)	/*!< DM need partial render cleanup before resuming processing */
#define RGXFWIF_DM_STATE_NEEDS_TRACE_CLEAR			(0x1 << 4)	/*!< DM need to increment Recovery Count once fully recovered */
#define RGXFWIF_DM_STATE_GUILTY_LOCKUP				(0x1 << 5)	/*!< DM was identified as locking up and causing HWR */
#define RGXFWIF_DM_STATE_INNOCENT_LOCKUP			(0x1 << 6)	/*!< DM was innocently affected by another lockup which caused HWR */
#define RGXFWIF_DM_STATE_GUILTY_OVERRUNING			(0x1 << 7)	/*!< DM was identified as over-running and causing HWR */
#define RGXFWIF_DM_STATE_INNOCENT_OVERRUNING		(0x1 << 8)	/*!< DM was innocently affected by another DM over-running which caused HWR */

/* Per-OSid States */
#define RGXFW_OS_STATE_ACTIVE_OS						(1 << 0)    /*!< Non active operating systems should not be served by the FW */
#define RGXFW_OS_STATE_FREELIST_OK						(1 << 1)    /*!< Pending freelist reconstruction from that particular OS */
#define RGXFW_OS_STATE_OFFLOADING						(1 << 2)    /*!< Transient state while all the OS resources in the FW are cleaned up */
#define RGXFW_OS_STATE_GROW_REQUEST_PENDING				(1 << 3)    /*!< Signifies whether a request to grow a freelist is pending completion */

typedef IMG_UINT32 RGXFWIF_HWR_RECOVERYFLAGS;

typedef struct _RGXFWIF_TRACEBUF_
{
    IMG_UINT32				ui32LogType;
	volatile RGXFWIF_POW_STATE		ePowState;
	RGXFWIF_TRACEBUF_SPACE	sTraceBuf[RGXFW_THREAD_NUM];

	IMG_UINT32				aui32HwrDmLockedUpCount[RGXFWIF_DM_DEFAULT_MAX];
	IMG_UINT32				aui32HwrDmOverranCount[RGXFWIF_DM_DEFAULT_MAX];
	IMG_UINT32				aui32HwrDmRecoveredCount[RGXFWIF_DM_DEFAULT_MAX];
	IMG_UINT32				aui32HwrDmFalseDetectCount[RGXFWIF_DM_DEFAULT_MAX];
	IMG_UINT32				ui32HwrCounter;

	IMG_UINT32				aui32CrPollAddr[RGXFW_THREAD_NUM];
	IMG_UINT32				aui32CrPollMask[RGXFW_THREAD_NUM];

	RGXFWIF_HWR_STATEFLAGS		ui32HWRStateFlags;
	RGXFWIF_HWR_RECOVERYFLAGS	aui32HWRRecoveryFlags[RGXFWIF_DM_DEFAULT_MAX];

	volatile IMG_UINT32		ui32HWPerfRIdx;
	volatile IMG_UINT32		ui32HWPerfWIdx;
	volatile IMG_UINT32		ui32HWPerfWrapCount;
	IMG_UINT32				ui32HWPerfSize;       /* Constant after setup, needed in FW */
	IMG_UINT32				ui32HWPerfDropCount;  /* The number of times the FW drops a packet due to buffer full */

	/* These next three items are only valid at runtime when the FW is built
	 * with RGX_HWPERF_UTILIZATION & RGX_HWPERF_DROP_TRACKING defined
	 * in rgxfw_hwperf.c */
	IMG_UINT32				ui32HWPerfUt;         /* Buffer utilisation, high watermark of bytes in use */
	IMG_UINT32				ui32FirstDropOrdinal;/* The ordinal of the first packet the FW dropped */
	IMG_UINT32              ui32LastDropOrdinal; /* The ordinal of the last packet the FW dropped */

	volatile IMG_UINT32			aui32InterruptCount[RGXFW_THREAD_NUM]; /*!< Interrupt count from Threads > */
	IMG_UINT32				ui32KCCBCmdsExecuted;
	IMG_UINT64 RGXFW_ALIGN			ui64StartIdleTime;
	IMG_UINT32				ui32PowMonEnergy;	/* Non-volatile power monitor energy count */

#define RGXFWIF_MAX_PCX 16
	IMG_UINT32				ui32T1PCX[RGXFWIF_MAX_PCX];
	IMG_UINT32				ui32T1PCXWOff;

	IMG_UINT32                  ui32OSStateFlags[RGXFW_NUM_OS];		/*!< State flags for each Operating System > */

	IMG_UINT32				ui32MMUFlushCounter;
} UNCACHED_ALIGN RGXFWIF_TRACEBUF;


/*!
 ******************************************************************************
 * GPU Utilisation
 *****************************************************************************/
#define RGXFWIF_GPU_STATS_MAX_VALUE_OF_STATE  10000

#define RGXFWIF_GPU_UTIL_STATE_ACTIVE_LOW     (0U)
#define RGXFWIF_GPU_UTIL_STATE_IDLE           (1U)
#define RGXFWIF_GPU_UTIL_STATE_ACTIVE_HIGH    (2U)
#define RGXFWIF_GPU_UTIL_STATE_BLOCKED        (3U)
#define RGXFWIF_GPU_UTIL_STATE_NUM            (4U)

#define RGXFWIF_GPU_UTIL_TIME_MASK            IMG_UINT64_C(0xFFFFFFFFFFFFFFFC)
#define RGXFWIF_GPU_UTIL_STATE_MASK           IMG_UINT64_C(0x0000000000000003)

#define RGXFWIF_GPU_UTIL_GET_TIME(word)       ((word) & RGXFWIF_GPU_UTIL_TIME_MASK)
#define RGXFWIF_GPU_UTIL_GET_STATE(word)      ((word) & RGXFWIF_GPU_UTIL_STATE_MASK)

/* The OS timestamps computed by the FW are approximations of the real time,
 * which means they could be slightly behind or ahead the real timer on the Host.
 * In some cases we can perform subtractions between FW approximated
 * timestamps and real OS timestamps, so we need a form of protection against
 * negative results if for instance the FW one is a bit ahead of time.
 */
#define RGXFWIF_GPU_UTIL_GET_PERIOD(newtime,oldtime) \
	((newtime) > (oldtime) ? ((newtime) - (oldtime)) : 0)

#define RGXFWIF_GPU_UTIL_MAKE_WORD(time,state) \
	(RGXFWIF_GPU_UTIL_GET_TIME(time) | RGXFWIF_GPU_UTIL_GET_STATE(state))


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
#define RGXFWIF_TIME_CORR_ARRAY_SIZE            256
#define RGXFWIF_TIME_CORR_CURR_INDEX(seqcount)  ((seqcount) % RGXFWIF_TIME_CORR_ARRAY_SIZE)

/* Make sure the timer correlation array size is a power of 2 */
static_assert((RGXFWIF_TIME_CORR_ARRAY_SIZE & (RGXFWIF_TIME_CORR_ARRAY_SIZE - 1)) == 0,
			  "RGXFWIF_TIME_CORR_ARRAY_SIZE must be a power of two");

typedef struct _RGXFWIF_GPU_UTIL_FWCB_
{
	RGXFWIF_TIME_CORR sTimeCorr[RGXFWIF_TIME_CORR_ARRAY_SIZE];
	IMG_UINT32        ui32TimeCorrSeqCount;

	/* Last GPU state + OS time of the last state update */
	IMG_UINT64 RGXFW_ALIGN ui64LastWord;

	/* Counters for the amount of time the GPU was active/idle/blocked */
	IMG_UINT64 RGXFW_ALIGN aui64StatsCounters[RGXFWIF_GPU_UTIL_STATE_NUM];
} UNCACHED_ALIGN RGXFWIF_GPU_UTIL_FWCB;


/*!
 ******************************************************************************
 * HWR Data
 *****************************************************************************/
typedef enum _RGX_HWRTYPE_
{
	RGX_HWRTYPE_UNKNOWNFAILURE  = 0,
	RGX_HWRTYPE_OVERRUN         = 1,
	RGX_HWRTYPE_POLLFAILURE     = 2,
	RGX_HWRTYPE_BIF0FAULT       = 3,
	RGX_HWRTYPE_BIF1FAULT       = 4,
	RGX_HWRTYPE_TEXASBIF0FAULT	= 5,
	RGX_HWRTYPE_DPXMMUFAULT		= 6,
	RGX_HWRTYPE_MMUFAULT        = 7,
	RGX_HWRTYPE_MMUMETAFAULT    = 8,
} RGX_HWRTYPE;

#define RGXFWIF_HWRTYPE_BIF_BANK_GET(eHWRType) ((eHWRType == RGX_HWRTYPE_BIF0FAULT) ? 0 : 1 )

#define RGXFWIF_HWRTYPE_PAGE_FAULT_GET(eHWRType) ((eHWRType == RGX_HWRTYPE_BIF0FAULT      ||       \
                                                   eHWRType == RGX_HWRTYPE_BIF1FAULT      ||       \
                                                   eHWRType == RGX_HWRTYPE_TEXASBIF0FAULT ||       \
                                                   eHWRType == RGX_HWRTYPE_MMUFAULT       ||       \
                                                   eHWRType == RGX_HWRTYPE_MMUMETAFAULT) ? 1 : 0 )

typedef struct _RGX_BIFINFO_
{
	IMG_UINT64	RGXFW_ALIGN		ui64BIFReqStatus;
	IMG_UINT64	RGXFW_ALIGN		ui64BIFMMUStatus;
	IMG_UINT64	RGXFW_ALIGN		ui64PCAddress; /*!< phys address of the page catalogue */
} RGX_BIFINFO;

typedef struct _RGX_MMUINFO_
{
	IMG_UINT64	RGXFW_ALIGN		ui64MMUStatus;
	IMG_UINT64	RGXFW_ALIGN		ui64PCAddress; /*!< phys address of the page catalogue */
} RGX_MMUINFO;

typedef struct _RGX_POLLINFO_
{
	IMG_UINT32	ui32ThreadNum;
	IMG_UINT32 	ui32CrPollAddr;
	IMG_UINT32 	ui32CrPollMask;
} UNCACHED_ALIGN RGX_POLLINFO;

typedef struct _RGX_HWRINFO_
{
	union
	{
		RGX_BIFINFO  sBIFInfo;
		RGX_MMUINFO  sMMUInfo;
		RGX_POLLINFO sPollInfo;
	} uHWRData;

	IMG_UINT64 RGXFW_ALIGN ui64CRTimer;
	IMG_UINT64 RGXFW_ALIGN ui64OSTimer;
	IMG_UINT32             ui32FrameNum;
	IMG_UINT32             ui32PID;
	IMG_UINT32             ui32ActiveHWRTData;
	IMG_UINT32             ui32HWRNumber;
	IMG_UINT32             ui32EventStatus;
	IMG_UINT32             ui32HWRRecoveryFlags;
	RGX_HWRTYPE            eHWRType;
	RGXFWIF_DM             eDM;
	IMG_UINT64 RGXFW_ALIGN ui64CRTimeOfKick;
	IMG_UINT64 RGXFW_ALIGN ui64CRTimeHWResetStart;
	IMG_UINT64 RGXFW_ALIGN ui64CRTimeHWResetFinish;
	IMG_UINT64 RGXFW_ALIGN ui64CRTimeFreelistReady;
} UNCACHED_ALIGN RGX_HWRINFO;

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
	IMG_UINT32	ui32DDReqCount;
} UNCACHED_ALIGN RGXFWIF_HWRINFOBUF;


#define RGXFWIF_CTXSWITCH_PROFILE_FAST_EN		(1)
#define RGXFWIF_CTXSWITCH_PROFILE_MEDIUM_EN		(2)
#define RGXFWIF_CTXSWITCH_PROFILE_SLOW_EN		(3)
#define RGXFWIF_CTXSWITCH_PROFILE_NODELAY_EN	(4)

/*!
 ******************************************************************************
 * RGX firmware Init Config Data
 *****************************************************************************/
#define RGXFWIF_INICFG_CTXSWITCH_TA_EN				(0x1 << 0)
#define RGXFWIF_INICFG_CTXSWITCH_3D_EN				(0x1 << 1)
#define RGXFWIF_INICFG_CTXSWITCH_CDM_EN				(0x1 << 2)
#define RGXFWIF_INICFG_CTXSWITCH_MODE_RAND			(0x1 << 3)
#define RGXFWIF_INICFG_CTXSWITCH_SRESET_EN			(0x1 << 4)
#define RGXFWIF_INICFG_RSVD							(0x1 << 5)
#define RGXFWIF_INICFG_POW_RASCALDUST				(0x1 << 6)
#define RGXFWIF_INICFG_HWPERF_EN					(0x1 << 7)
#define RGXFWIF_INICFG_HWR_EN						(0x1 << 8)
#define RGXFWIF_INICFG_CHECK_MLIST_EN				(0x1 << 9)
#define RGXFWIF_INICFG_DISABLE_CLKGATING_EN 		(0x1 << 10)
#define RGXFWIF_INICFG_POLL_COUNTERS_EN				(0x1 << 11)
#define RGXFWIF_INICFG_VDM_CTX_STORE_MODE_INDEX		(RGX_CR_VDM_CONTEXT_STORE_MODE_MODE_INDEX << 12)
#define RGXFWIF_INICFG_VDM_CTX_STORE_MODE_INSTANCE	(RGX_CR_VDM_CONTEXT_STORE_MODE_MODE_INSTANCE << 12)
#define RGXFWIF_INICFG_VDM_CTX_STORE_MODE_LIST		(RGX_CR_VDM_CONTEXT_STORE_MODE_MODE_LIST << 12)
#define RGXFWIF_INICFG_VDM_CTX_STORE_MODE_CLRMSK	(0xFFFFCFFFU)
#define RGXFWIF_INICFG_VDM_CTX_STORE_MODE_SHIFT		(12)
#define RGXFWIF_INICFG_SHG_BYPASS_EN				(0x1 << 14)
#define RGXFWIF_INICFG_RTU_BYPASS_EN				(0x1 << 15)
#define RGXFWIF_INICFG_REGCONFIG_EN					(0x1 << 16)
#define RGXFWIF_INICFG_ASSERT_ON_OUTOFMEMORY		(0x1 << 17)
#define RGXFWIF_INICFG_HWP_DISABLE_FILTER			(0x1 << 18)
#define RGXFWIF_INICFG_CUSTOM_PERF_TIMER_EN			(0x1 << 19)
#define RGXFWIF_INICFG_CDM_KILL_MODE_RAND_EN		(0x1 << 20)
#define RGXFWIF_INICFG_DISABLE_DM_OVERLAP			(0x1 << 21)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT		(22)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_FAST		(RGXFWIF_CTXSWITCH_PROFILE_FAST_EN << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_MEDIUM		(RGXFWIF_CTXSWITCH_PROFILE_MEDIUM_EN << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_SLOW		(RGXFWIF_CTXSWITCH_PROFILE_SLOW_EN << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_NODELAY	(RGXFWIF_CTXSWITCH_PROFILE_NODELAY_EN << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_MASK		(0x7 << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define RGXFWIF_INICFG_METAT1_SHIFT					(25)
#define RGXFWIF_INICFG_METAT1_MAIN					(RGX_META_T1_MAIN  << RGXFWIF_INICFG_METAT1_SHIFT)
#define RGXFWIF_INICFG_METAT1_DUMMY					(RGX_META_T1_DUMMY << RGXFWIF_INICFG_METAT1_SHIFT)
#define RGXFWIF_INICFG_METAT1_ENABLED				(RGXFWIF_INICFG_METAT1_MAIN | RGXFWIF_INICFG_METAT1_DUMMY)
#define RGXFWIF_INICFG_METAT1_MASK					(RGXFWIF_INICFG_METAT1_ENABLED >> RGXFWIF_INICFG_METAT1_SHIFT)
#define RGXFWIF_INICFG_ASSERT_ON_HWR_TRIGGER		(0x1 << 27)
#define RGXFWIF_INICFG_WORKEST_V1					(0x1 << 28)
#define RGXFWIF_INICFG_WORKEST_V2					(0x1 << 29)
#define RGXFWIF_INICFG_PDVFS_V1						(0x1 << 30)
#define RGXFWIF_INICFG_PDVFS_V2						(0x1 << 31)
#define RGXFWIF_INICFG_ALL							(0xFFFFFFDFU)

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
	RGX_ACTIVEPM_DEFAULT = 2
} RGX_ACTIVEPM_CONF;

typedef enum
{
	RGX_RD_POWER_ISLAND_FORCE_OFF = 0,
	RGX_RD_POWER_ISLAND_FORCE_ON = 1,
	RGX_RD_POWER_ISLAND_DEFAULT = 2
} RGX_RD_POWER_ISLAND_CONF;

typedef enum
{
	RGX_META_T1_OFF   = 0x0,           /*!< No thread 1 running (unless 2nd thread is used for HWPerf) */
	RGX_META_T1_MAIN  = 0x1,           /*!< Run the main thread 0 code on thread 1 (and vice versa if 2nd thread is used for HWPerf) */
	RGX_META_T1_DUMMY = 0x2            /*!< Run dummy test code on thread 1 */
} RGX_META_T1_CONF;

/*!
 ******************************************************************************
 * Querying DM state
 *****************************************************************************/

typedef enum _RGXFWIF_DM_STATE_
{
	RGXFWIF_DM_STATE_NORMAL			= 0,
	RGXFWIF_DM_STATE_LOCKEDUP		= 1
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

