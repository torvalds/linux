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

#if !defined(RGX_FWIF_KM_H)
#define RGX_FWIF_KM_H

#include "img_types.h"
#include "rgx_fwif_shared.h"
#include "rgxdefs_km.h"
#include "dllist.h"
#include "rgx_hwperf.h"
#include "rgx_mips.h"
#include "rgxheapconfig.h"


/*************************************************************************/ /*!
 Logging type
*/ /**************************************************************************/
#define RGXFWIF_LOG_TYPE_NONE			0x00000000U
#define RGXFWIF_LOG_TYPE_TRACE			0x00000001U
#define RGXFWIF_LOG_TYPE_GROUP_MAIN		0x00000002U
#define RGXFWIF_LOG_TYPE_GROUP_MTS		0x00000004U
#define RGXFWIF_LOG_TYPE_GROUP_CLEANUP	0x00000008U
#define RGXFWIF_LOG_TYPE_GROUP_CSW		0x00000010U
#define RGXFWIF_LOG_TYPE_GROUP_BIF		0x00000020U
#define RGXFWIF_LOG_TYPE_GROUP_PM		0x00000040U
#define RGXFWIF_LOG_TYPE_GROUP_RTD		0x00000080U
#define RGXFWIF_LOG_TYPE_GROUP_SPM		0x00000100U
#define RGXFWIF_LOG_TYPE_GROUP_POW		0x00000200U
#define RGXFWIF_LOG_TYPE_GROUP_HWR		0x00000400U
#define RGXFWIF_LOG_TYPE_GROUP_HWP		0x00000800U
#define RGXFWIF_LOG_TYPE_GROUP_RPM		0x00001000U
#define RGXFWIF_LOG_TYPE_GROUP_DMA		0x00002000U
#define RGXFWIF_LOG_TYPE_GROUP_MISC		0x00004000U
#define RGXFWIF_LOG_TYPE_GROUP_DEBUG	0x80000000U
#define RGXFWIF_LOG_TYPE_GROUP_MASK		0x80007FFEU
#define RGXFWIF_LOG_TYPE_MASK			0x80007FFFU

/* String used in pvrdebug -h output */
#define RGXFWIF_LOG_GROUPS_STRING_LIST   "main,mts,cleanup,csw,bif,pm,rtd,spm,pow,hwr,hwp,rpm,dma,misc,debug"

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
                                         { "misc",    RGXFWIF_LOG_TYPE_GROUP_MISC }, \
                                         { "debug",   RGXFWIF_LOG_TYPE_GROUP_DEBUG }


/* Used in print statements to display log group state, one %s per group defined */
#define RGXFWIF_LOG_ENABLED_GROUPS_LIST_PFSPEC  "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"

/* Used in a print statement to display log group state, one per group */
#define RGXFWIF_LOG_ENABLED_GROUPS_LIST(types)  ((((types) & RGXFWIF_LOG_TYPE_GROUP_MAIN) != 0U)	?("main ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_MTS) != 0U)		?("mts ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_CLEANUP) != 0U)	?("cleanup ")	:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_CSW) != 0U)		?("csw ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_BIF) != 0U)		?("bif ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_PM) != 0U)		?("pm ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_RTD) != 0U)		?("rtd ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_SPM) != 0U)		?("spm ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_POW) != 0U)		?("pow ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_HWR) != 0U)		?("hwr ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_HWP) != 0U)		?("hwp ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_RPM) != 0U)		?("rpm ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_DMA) != 0U)		?("dma ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_MISC) != 0U)	?("misc ")		:("")),		\
                                                ((((types) & RGXFWIF_LOG_TYPE_GROUP_DEBUG) != 0U)	?("debug ")		:(""))


/************************************************************************
* RGX FW signature checks
************************************************************************/
#define RGXFW_SIG_BUFFER_SIZE_MIN       (8192)

#define RGXFWIF_TIMEDIFF_ID			((0x1UL << 28) | RGX_CR_TIMER)

/*!
 ******************************************************************************
 * Trace Buffer
 *****************************************************************************/

/*! Min, Max, and Default size of RGXFWIF_TRACEBUF_SPACE in DWords */
#define RGXFW_TRACE_BUF_MIN_SIZE_IN_DWORDS      8192U  /*  32KB */
#define RGXFW_TRACE_BUF_DEFAULT_SIZE_IN_DWORDS 12000U  /* ~48KB */
#define RGXFW_TRACE_BUF_MAX_SIZE_IN_DWORDS     32768U  /* 128KB */

#define RGXFW_TRACE_BUFFER_ASSERT_SIZE 200U
#if defined(RGXFW_META_SUPPORT_2ND_THREAD)
#define RGXFW_THREAD_NUM 2U
#else
#define RGXFW_THREAD_NUM 1U
#endif

#define RGXFW_POLL_TYPE_SET 0x80000000U

#define RGXFW_PROCESS_NAME_LEN	(16)

typedef struct
{
	IMG_CHAR	szPath[RGXFW_TRACE_BUFFER_ASSERT_SIZE];
	IMG_CHAR	szInfo[RGXFW_TRACE_BUFFER_ASSERT_SIZE];
	IMG_UINT32	ui32LineNum;
} UNCACHED_ALIGN RGXFWIF_FILE_INFO_BUF;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_FILE_INFO_BUF) == 408,
				"RGXFWIF_FILE_INFO_BUF is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*!
 * @Defgroup SRVAndFWTracing Services and Firmware Tracing data interface
 * @Brief The document groups/lists the data structures and the interfaces related to Services and Firmware Tracing
 * @{
 */

/*!
 * @Brief Firmware trace buffer details
 */
typedef struct
{
	IMG_UINT32                         ui32TracePointer;          /*!< Trace pointer (write index into Trace Buffer) */
	IMG_UINT32                         ui32WrapCount;             /*!< Number of times the Trace Buffer has wrapped */

#if defined(RGX_FIRMWARE)
	IMG_UINT32                         *pui32RGXFWIfTraceBuffer;  /*!< Trace buffer address (FW address), to be used by firmware for writing into trace buffer */
#else
	RGXFWIF_DEV_VIRTADDR               pui32RGXFWIfTraceBuffer;   /*!< Trace buffer address (FW address)*/
#endif
	IMG_PUINT32 RGXFW_ALIGN            pui32TraceBuffer;          /*!< Trace buffer address (Host address), to be used by host when reading from trace buffer */

	RGXFWIF_FILE_INFO_BUF RGXFW_ALIGN  sAssertBuf;
} UNCACHED_ALIGN RGXFWIF_TRACEBUF_SPACE;

/*! @} End of Defgroup SRVAndFWTracing */

#define RGXFWIF_FWFAULTINFO_MAX		(8U)			/* Total number of FW fault logs stored */

typedef struct
{
	IMG_UINT64 RGXFW_ALIGN	ui64CRTimer;
	IMG_UINT64 RGXFW_ALIGN	ui64OSTimer;
	IMG_UINT32 RGXFW_ALIGN	ui32Data;
	IMG_UINT32 ui32Reserved;
	RGXFWIF_FILE_INFO_BUF	sFaultBuf;
} UNCACHED_ALIGN RGX_FWFAULTINFO;


#define RGXFWIF_POW_STATES \
  X(RGXFWIF_POW_OFF)			/* idle and handshaked with the host (ready to full power down) */ \
  X(RGXFWIF_POW_ON)				/* running HW commands */ \
  X(RGXFWIF_POW_FORCED_IDLE)	/* forced idle */ \
  X(RGXFWIF_POW_IDLE)			/* idle waiting for host handshake */

typedef enum
{
#define X(NAME) NAME,
	RGXFWIF_POW_STATES
#undef X
} RGXFWIF_POW_STATE;

/* Firmware HWR states */
#define RGXFWIF_HWR_HARDWARE_OK			(IMG_UINT32_C(0x1) << 0U)	/*!< The HW state is ok or locked up */
#define RGXFWIF_HWR_RESET_IN_PROGRESS	(IMG_UINT32_C(0x1) << 1U)	/*!< Tells if a HWR reset is in progress */
#define RGXFWIF_HWR_GENERAL_LOCKUP		(IMG_UINT32_C(0x1) << 3U)	/*!< A DM unrelated lockup has been detected */
#define RGXFWIF_HWR_DM_RUNNING_OK		(IMG_UINT32_C(0x1) << 4U)	/*!< At least one DM is running without being close to a lockup */
#define RGXFWIF_HWR_DM_STALLING			(IMG_UINT32_C(0x1) << 5U)	/*!< At least one DM is close to lockup */
#define RGXFWIF_HWR_FW_FAULT			(IMG_UINT32_C(0x1) << 6U)	/*!< The FW has faulted and needs to restart */
#define RGXFWIF_HWR_RESTART_REQUESTED	(IMG_UINT32_C(0x1) << 7U)	/*!< The FW has requested the host to restart it */

#define RGXFWIF_PHR_STATE_SHIFT			(8U)
#define RGXFWIF_PHR_RESTART_REQUESTED	(IMG_UINT32_C(1) << RGXFWIF_PHR_STATE_SHIFT)	/*!< The FW has requested the host to restart it, per PHR configuration */
#define RGXFWIF_PHR_RESTART_FINISHED	(IMG_UINT32_C(2) << RGXFWIF_PHR_STATE_SHIFT)	/*!< A PHR triggered GPU reset has just finished */
#define RGXFWIF_PHR_RESTART_MASK		(RGXFWIF_PHR_RESTART_REQUESTED | RGXFWIF_PHR_RESTART_FINISHED)

#define RGXFWIF_PHR_MODE_OFF			(0UL)
#define RGXFWIF_PHR_MODE_RD_RESET		(1UL)
#define RGXFWIF_PHR_MODE_FULL_RESET		(2UL)

typedef IMG_UINT32 RGXFWIF_HWR_STATEFLAGS;

/* Firmware per-DM HWR states */
#define RGXFWIF_DM_STATE_WORKING					(0x00U)		/*!< DM is working if all flags are cleared */
#define RGXFWIF_DM_STATE_READY_FOR_HWR				(IMG_UINT32_C(0x1) << 0)	/*!< DM is idle and ready for HWR */
#define RGXFWIF_DM_STATE_NEEDS_SKIP					(IMG_UINT32_C(0x1) << 2)	/*!< DM need to skip to next cmd before resuming processing */
#define RGXFWIF_DM_STATE_NEEDS_PR_CLEANUP			(IMG_UINT32_C(0x1) << 3)	/*!< DM need partial render cleanup before resuming processing */
#define RGXFWIF_DM_STATE_NEEDS_TRACE_CLEAR			(IMG_UINT32_C(0x1) << 4)	/*!< DM need to increment Recovery Count once fully recovered */
#define RGXFWIF_DM_STATE_GUILTY_LOCKUP				(IMG_UINT32_C(0x1) << 5)	/*!< DM was identified as locking up and causing HWR */
#define RGXFWIF_DM_STATE_INNOCENT_LOCKUP			(IMG_UINT32_C(0x1) << 6)	/*!< DM was innocently affected by another lockup which caused HWR */
#define RGXFWIF_DM_STATE_GUILTY_OVERRUNING			(IMG_UINT32_C(0x1) << 7)	/*!< DM was identified as over-running and causing HWR */
#define RGXFWIF_DM_STATE_INNOCENT_OVERRUNING		(IMG_UINT32_C(0x1) << 8)	/*!< DM was innocently affected by another DM over-running which caused HWR */
#define RGXFWIF_DM_STATE_HARD_CONTEXT_SWITCH		(IMG_UINT32_C(0x1) << 9)	/*!< DM was forced into HWR as it delayed more important workloads */
#define RGXFWIF_DM_STATE_GPU_ECC_HWR				(IMG_UINT32_C(0x1) << 10)	/*!< DM was forced into HWR due to an uncorrected GPU ECC error */

/* Firmware's connection state */
typedef IMG_UINT32 RGXFWIF_CONNECTION_FW_STATE;
#define RGXFW_CONNECTION_FW_OFFLINE		0U	/*!< Firmware is offline */
#define RGXFW_CONNECTION_FW_READY		1U	/*!< Firmware is initialised */
#define RGXFW_CONNECTION_FW_ACTIVE		2U	/*!< Firmware connection is fully established */
#define RGXFW_CONNECTION_FW_OFFLOADING	3U	/*!< Firmware is clearing up connection data */
#define RGXFW_CONNECTION_FW_STATE_COUNT	4U

/* OS' connection state */
typedef enum
{
	RGXFW_CONNECTION_OS_OFFLINE = 0,	/*!< OS is offline */
	RGXFW_CONNECTION_OS_READY,			/*!< OS's KM driver is setup and waiting */
	RGXFW_CONNECTION_OS_ACTIVE,			/*!< OS connection is fully established */
	RGXFW_CONNECTION_OS_STATE_COUNT
} RGXFWIF_CONNECTION_OS_STATE;

typedef struct
{
	IMG_UINT			bfOsState		: 3;
	IMG_UINT			bfFLOk			: 1;
	IMG_UINT			bfFLGrowPending	: 1;
	IMG_UINT			bfReserved		: 27;
} RGXFWIF_OS_RUNTIME_FLAGS;

typedef IMG_UINT32 RGXFWIF_HWR_RECOVERYFLAGS;

#if defined(PVRSRV_STALLED_CCB_ACTION)
#define PVR_SLR_LOG_ENTRIES 10U
#define PVR_SLR_LOG_STRLEN  30 /*!< MAX_CLIENT_CCB_NAME not visible to this header */

typedef struct
{
	IMG_UINT64 RGXFW_ALIGN	ui64Timestamp;
	IMG_UINT32				ui32FWCtxAddr;
	IMG_UINT32				ui32NumUFOs;
	IMG_CHAR				aszCCBName[PVR_SLR_LOG_STRLEN];
} UNCACHED_ALIGN RGXFWIF_SLR_ENTRY;
#endif

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
#define MAX_THREAD_NUM 2

static_assert(RGXFW_THREAD_NUM <= MAX_THREAD_NUM,
				"RGXFW_THREAD_NUM is outside of allowable range for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*!
 * @InGroup SRVAndFWTracing
 * @Brief Firmware trace control data
 */
typedef struct
{
	IMG_UINT32              ui32LogType;                  /*!< FW trace log group configuration */
#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
	RGXFWIF_TRACEBUF_SPACE  sTraceBuf[MAX_THREAD_NUM];  /*!< FW Trace buffer */
#else
	RGXFWIF_TRACEBUF_SPACE  sTraceBuf[RGXFW_THREAD_NUM];  /*!< FW Trace buffer */
#endif
	IMG_UINT32              ui32TraceBufSizeInDWords;     /*!< FW Trace buffer size in dwords, Member initialised only when sTraceBuf is actually allocated
															(in RGXTraceBufferInitOnDemandResources) */
	IMG_UINT32              ui32TracebufFlags;            /*!< Compatibility and other flags */
} UNCACHED_ALIGN RGXFWIF_TRACEBUF;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_TRACEBUF) == 880,
				"RGXFWIF_TRACEBUF is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*! @Brief Firmware system data shared with the Host driver */
typedef struct
{
	IMG_UINT32                 ui32ConfigFlags;                       /*!< Configuration flags from host */
	IMG_UINT32                 ui32ConfigFlagsExt;                    /*!< Extended configuration flags from host */
	volatile RGXFWIF_POW_STATE ePowState;
	volatile IMG_UINT32        ui32HWPerfRIdx;
	volatile IMG_UINT32        ui32HWPerfWIdx;
	volatile IMG_UINT32        ui32HWPerfWrapCount;
	IMG_UINT32                 ui32HWPerfSize;                        /*!< Constant after setup, needed in FW */
	IMG_UINT32                 ui32HWPerfDropCount;                   /*!< The number of times the FW drops a packet due to buffer full */

	/* ui32HWPerfUt, ui32FirstDropOrdinal, ui32LastDropOrdinal only valid when FW is built with
	 * RGX_HWPERF_UTILIZATION & RGX_HWPERF_DROP_TRACKING defined in rgxfw_hwperf.c */
	IMG_UINT32                 ui32HWPerfUt;                          /*!< Buffer utilisation, high watermark of bytes in use */
	IMG_UINT32                 ui32FirstDropOrdinal;                  /*!< The ordinal of the first packet the FW dropped */
	IMG_UINT32                 ui32LastDropOrdinal;                   /*!< The ordinal of the last packet the FW dropped */
	RGXFWIF_OS_RUNTIME_FLAGS   asOsRuntimeFlagsMirror[RGXFW_MAX_NUM_OSIDS];/*!< State flags for each Operating System mirrored from Fw coremem */
	RGX_FWFAULTINFO            sFaultInfo[RGXFWIF_FWFAULTINFO_MAX];   /*!< Firmware fault info */
	IMG_UINT32                 ui32FWFaults;                          /*!< Firmware faults count */
#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
	IMG_UINT32                 aui32CrPollAddr[MAX_THREAD_NUM];     /*!< Failed poll address */
	IMG_UINT32                 aui32CrPollMask[MAX_THREAD_NUM];     /*!< Failed poll mask */
	IMG_UINT32                 aui32CrPollCount[MAX_THREAD_NUM];    /*!< Failed poll count */
#else
	IMG_UINT32                 aui32CrPollAddr[RGXFW_THREAD_NUM];     /*!< Failed poll address */
	IMG_UINT32                 aui32CrPollMask[RGXFW_THREAD_NUM];     /*!< Failed poll mask */
	IMG_UINT32                 aui32CrPollCount[RGXFW_THREAD_NUM];    /*!< Failed poll count */
#endif
	IMG_UINT64 RGXFW_ALIGN     ui64StartIdleTime;

#if defined(SUPPORT_RGXFW_STATS_FRAMEWORK) && !defined(SUPPORT_OPEN_SOURCE_DRIVER)
#define RGXFWIF_STATS_FRAMEWORK_LINESIZE    (8)
#define RGXFWIF_STATS_FRAMEWORK_MAX         (2048*RGXFWIF_STATS_FRAMEWORK_LINESIZE)
	IMG_UINT32 RGXFW_ALIGN     aui32FWStatsBuf[RGXFWIF_STATS_FRAMEWORK_MAX];
#endif
	RGXFWIF_HWR_STATEFLAGS     ui32HWRStateFlags; /*!< Firmware's Current HWR state */
	RGXFWIF_HWR_RECOVERYFLAGS  aui32HWRRecoveryFlags[RGXFWIF_DM_MAX]; /*!< Each DM's HWR state */
	IMG_UINT32                 ui32FwSysDataFlags;                      /*!< Compatibility and other flags */
	IMG_UINT32                 ui32McConfig;                            /*!< Identify whether MC config is P-P or P-S */
} UNCACHED_ALIGN RGXFWIF_SYSDATA;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_SYSDATA) == 3624,
				"RGXFWIF_SYSDATA is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER == 3624");
#endif


/*!
 * @InGroup ContextSwitching
 * @Brief Firmware per-os data and configuration
 */
typedef struct
{
	IMG_UINT32                 ui32FwOsConfigFlags;                   /*!< Configuration flags from an OS */
	IMG_UINT32                 ui32FWSyncCheckMark;                   /*!< Markers to signal that the host should perform a full sync check */
	IMG_UINT32                 ui32HostSyncCheckMark;                  /*!< Markers to signal that the Firmware should perform a full sync check */
#if defined(PVRSRV_STALLED_CCB_ACTION) || defined(SUPPORT_OPEN_SOURCE_DRIVER)
	IMG_UINT32                 ui32ForcedUpdatesRequested;
	IMG_UINT8                  ui8SLRLogWp;
	RGXFWIF_SLR_ENTRY          sSLRLogFirst;
	RGXFWIF_SLR_ENTRY          sSLRLog[PVR_SLR_LOG_ENTRIES];
	IMG_UINT64 RGXFW_ALIGN     ui64LastForcedUpdateTime;
#endif
#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
	volatile IMG_UINT32        aui32InterruptCount[MAX_THREAD_NUM]; /*!< Interrupt count from Threads > */
#else
	volatile IMG_UINT32        aui32InterruptCount[RGXFW_THREAD_NUM]; /*!< Interrupt count from Threads > */
#endif
	IMG_UINT32                 ui32KCCBCmdsExecuted;                  /*!< Executed Kernel CCB command count */
	RGXFWIF_DEV_VIRTADDR       sPowerSync;                            /*!< Sync prim used to signal the host the power off state */
	IMG_UINT32                 ui32FwOsDataFlags;                       /*!< Compatibility and other flags */
} UNCACHED_ALIGN RGXFWIF_OSDATA;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_OSDATA) == 584,
				"RGXFWIF_OSDATA is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/* Firmware trace time-stamp field breakup */

/* RGX_CR_TIMER register read (48 bits) value*/
#define RGXFWT_TIMESTAMP_TIME_SHIFT                   (0U)
#define RGXFWT_TIMESTAMP_TIME_CLRMSK                  (IMG_UINT64_C(0xFFFF000000000000))

/* Extra debug-info (16 bits) */
#define RGXFWT_TIMESTAMP_DEBUG_INFO_SHIFT             (48U)
#define RGXFWT_TIMESTAMP_DEBUG_INFO_CLRMSK            ~RGXFWT_TIMESTAMP_TIME_CLRMSK


/* Debug-info sub-fields */
/* Bit 0: RGX_CR_EVENT_STATUS_MMU_PAGE_FAULT bit from RGX_CR_EVENT_STATUS register */
#define RGXFWT_DEBUG_INFO_MMU_PAGE_FAULT_SHIFT        (0U)
#define RGXFWT_DEBUG_INFO_MMU_PAGE_FAULT_SET          (1U << RGXFWT_DEBUG_INFO_MMU_PAGE_FAULT_SHIFT)

/* Bit 1: RGX_CR_BIF_MMU_ENTRY_PENDING bit from RGX_CR_BIF_MMU_ENTRY register */
#define RGXFWT_DEBUG_INFO_MMU_ENTRY_PENDING_SHIFT     (1U)
#define RGXFWT_DEBUG_INFO_MMU_ENTRY_PENDING_SET       (1U << RGXFWT_DEBUG_INFO_MMU_ENTRY_PENDING_SHIFT)

/* Bit 2: RGX_CR_SLAVE_EVENT register is non-zero */
#define RGXFWT_DEBUG_INFO_SLAVE_EVENTS_SHIFT          (2U)
#define RGXFWT_DEBUG_INFO_SLAVE_EVENTS_SET            (1U << RGXFWT_DEBUG_INFO_SLAVE_EVENTS_SHIFT)

/* Bit 3-15: Unused bits */

#define RGXFWT_DEBUG_INFO_STR_MAXLEN                  64
#define RGXFWT_DEBUG_INFO_STR_PREPEND                 " (debug info: "
#define RGXFWT_DEBUG_INFO_STR_APPEND                  ")"

/* Table of debug info sub-field's masks and corresponding message strings
 * to be appended to firmware trace
 *
 * Mask     : 16 bit mask to be applied to debug-info field
 * String   : debug info message string
 */

#define RGXFWT_DEBUG_INFO_MSKSTRLIST \
/*Mask,                                           String*/ \
X(RGXFWT_DEBUG_INFO_MMU_PAGE_FAULT_SET,      "mmu pf") \
X(RGXFWT_DEBUG_INFO_MMU_ENTRY_PENDING_SET,   "mmu pending") \
X(RGXFWT_DEBUG_INFO_SLAVE_EVENTS_SET,        "slave events")

/*!
 ******************************************************************************
 * HWR Data
 *****************************************************************************/
/*!
 * @Defgroup HWRInfo FW HWR shared data interface
 * @Brief Types grouping data structures and defines used in realising the HWR record.
 * @{
 */
/*! @Brief HWR Lockup types */
typedef enum
{
	RGX_HWRTYPE_UNKNOWNFAILURE = 0, /*!< Unknown failure */
	RGX_HWRTYPE_OVERRUN        = 1, /*!< DM overrun */
	RGX_HWRTYPE_POLLFAILURE    = 2, /*!< Poll failure */
	RGX_HWRTYPE_BIF0FAULT      = 3, /*!< BIF0 fault */
	RGX_HWRTYPE_BIF1FAULT      = 4, /*!< BIF1 fault */
	RGX_HWRTYPE_TEXASBIF0FAULT = 5, /*!< TEXASBIF0 fault */
	RGX_HWRTYPE_MMUFAULT       = 6, /*!< MMU fault */
	RGX_HWRTYPE_MMUMETAFAULT   = 7, /*!< MMU META fault */
	RGX_HWRTYPE_MIPSTLBFAULT   = 8, /*!< MIPS TLB fault */
	RGX_HWRTYPE_ECCFAULT       = 9, /*!< ECC fault */
	RGX_HWRTYPE_MMURISCVFAULT  = 10, /*!< MMU RISCV fault */
} RGX_HWRTYPE;

#define RGXFWIF_HWRTYPE_BIF_BANK_GET(eHWRType) (((eHWRType) == RGX_HWRTYPE_BIF0FAULT) ? 0 : 1)

#define RGXFWIF_HWRTYPE_PAGE_FAULT_GET(eHWRType) ((((eHWRType) == RGX_HWRTYPE_BIF0FAULT)      ||       \
                                                   ((eHWRType) == RGX_HWRTYPE_BIF1FAULT)      ||       \
                                                   ((eHWRType) == RGX_HWRTYPE_TEXASBIF0FAULT) ||       \
                                                   ((eHWRType) == RGX_HWRTYPE_MMUFAULT)       ||       \
                                                   ((eHWRType) == RGX_HWRTYPE_MMUMETAFAULT)   ||       \
                                                   ((eHWRType) == RGX_HWRTYPE_MIPSTLBFAULT)   ||       \
                                                   ((eHWRType) == RGX_HWRTYPE_MMURISCVFAULT)) ? true : false)

typedef struct
{
	IMG_UINT64	RGXFW_ALIGN		ui64BIFReqStatus; /*!< BIF request status */
	IMG_UINT64	RGXFW_ALIGN		ui64BIFMMUStatus; /*!< MMU status */
	IMG_UINT64	RGXFW_ALIGN		ui64PCAddress; /*!< phys address of the page catalogue */
	IMG_UINT64	RGXFW_ALIGN		ui64Reserved;
} RGX_BIFINFO;

typedef struct
{
	IMG_UINT32 ui32FaultGPU; /*!< ECC fault in GPU */
} RGX_ECCINFO;

typedef struct
{
	IMG_UINT64	RGXFW_ALIGN		aui64MMUStatus[2]; /*!< MMU status */
	IMG_UINT64	RGXFW_ALIGN		ui64PCAddress; /*!< phys address of the page catalogue */
	IMG_UINT64	RGXFW_ALIGN		ui64Reserved;
} RGX_MMUINFO;

typedef struct
{
	IMG_UINT32	ui32ThreadNum; /*!< Thread ID performing poll operation */
	IMG_UINT32	ui32CrPollAddr; /*!< CR Poll Address */
	IMG_UINT32	ui32CrPollMask; /*!< CR Poll mask */
	IMG_UINT32	ui32CrPollLastValue; /*!< CR Poll last value */
	IMG_UINT64	RGXFW_ALIGN ui64Reserved;
} UNCACHED_ALIGN RGX_POLLINFO;

typedef struct
{
	IMG_UINT32 ui32BadVAddr; /*!< VA address */
	IMG_UINT32 ui32EntryLo;
} RGX_TLBINFO;

/*! @Brief Structure to keep information specific to a lockup e.g. DM, timer, lockup type etc. */
typedef struct
{
	union
	{
		RGX_BIFINFO  sBIFInfo; /*!< BIF failure details */
		RGX_MMUINFO  sMMUInfo; /*!< MMU failure details */
		RGX_POLLINFO sPollInfo; /*!< Poll failure details */
		RGX_TLBINFO  sTLBInfo; /*!< TLB failure details */
		RGX_ECCINFO  sECCInfo; /*!< ECC failure details */
	} uHWRData;

	IMG_UINT64 RGXFW_ALIGN ui64CRTimer; /*!< Timer value at the time of lockup */
	IMG_UINT64 RGXFW_ALIGN ui64OSTimer; /*!< OS timer value at the time of lockup */
	IMG_UINT32             ui32FrameNum; /*!< Frame number of the workload */
	IMG_UINT32             ui32PID; /*!< PID belonging to the workload */
	IMG_UINT32             ui32ActiveHWRTData; /*!< HWRT data of the workload */
	IMG_UINT32             ui32HWRNumber; /*!< HWR number */
	IMG_UINT32             ui32EventStatus; /*!< Core specific event status register at the time of lockup */
	IMG_UINT32             ui32HWRRecoveryFlags; /*!< DM state flags */
	RGX_HWRTYPE            eHWRType; /*!< Type of lockup */
	RGXFWIF_DM             eDM; /*!< Recovery triggered for the DM */
	IMG_UINT32             ui32CoreID; /*!< Core ID of the GPU */
	IMG_UINT64 RGXFW_ALIGN ui64CRTimeOfKick; /*!< Workload kick time */
	IMG_UINT64 RGXFW_ALIGN ui64CRTimeHWResetStart; /*!< HW reset start time */
	IMG_UINT64 RGXFW_ALIGN ui64CRTimeHWResetFinish; /*!< HW reset stop time */
	IMG_UINT64 RGXFW_ALIGN ui64CRTimeFreelistReady; /*!< freelist ready time on the last HWR */
	IMG_CHAR   RGXFW_ALIGN szProcName[RGXFW_PROCESS_NAME_LEN]; /*!< User process name */
} UNCACHED_ALIGN RGX_HWRINFO;

#define RGXFWIF_HWINFO_MAX_FIRST 8U							/* Number of first HWR logs recorded (never overwritten by newer logs) */
#define RGXFWIF_HWINFO_MAX_LAST 8U							/* Number of latest HWR logs (older logs are overwritten by newer logs) */
#define RGXFWIF_HWINFO_MAX (RGXFWIF_HWINFO_MAX_FIRST + RGXFWIF_HWINFO_MAX_LAST)	/* Total number of HWR logs stored in a buffer */
#define RGXFWIF_HWINFO_LAST_INDEX (RGXFWIF_HWINFO_MAX - 1U)	/* Index of the last log in the HWR log buffer */

/*! @Brief Firmware HWR information structure allocated by the Services and used by the Firmware to update recovery information. */
typedef struct
{
	RGX_HWRINFO sHWRInfo[RGXFWIF_HWINFO_MAX]; /*!< Max number of recovery record */
	IMG_UINT32  ui32HwrCounter; /*!< HWR counter used in FL reconstruction */
	IMG_UINT32  ui32WriteIndex; /*!< Index for updating recovery information in sHWRInfo */
	IMG_UINT32  ui32DDReqCount; /*!< Count of DebugDump requested to the host after recovery */
	IMG_UINT32  ui32HWRInfoBufFlags; /* Compatibility and other flags */
	IMG_UINT32  aui32HwrDmLockedUpCount[RGXFWIF_DM_MAX]; /*!< Lockup count for each DM */
	IMG_UINT32  aui32HwrDmOverranCount[RGXFWIF_DM_MAX]; /*!< Overrun count for each DM */
	IMG_UINT32  aui32HwrDmRecoveredCount[RGXFWIF_DM_MAX]; /*!< Lockup + Overrun count for each DM */
	IMG_UINT32  aui32HwrDmFalseDetectCount[RGXFWIF_DM_MAX]; /*!< False lockup detection count for each DM */
} UNCACHED_ALIGN RGXFWIF_HWRINFOBUF;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_HWRINFOBUF) == 2336,
				"RGXFWIF_HWRINFOBUF is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*! @} End of HWRInfo */

#define RGXFWIF_CTXSWITCH_PROFILE_FAST_EN		(IMG_UINT32_C(0x1))
#define RGXFWIF_CTXSWITCH_PROFILE_MEDIUM_EN		(IMG_UINT32_C(0x2))
#define RGXFWIF_CTXSWITCH_PROFILE_SLOW_EN		(IMG_UINT32_C(0x3))
#define RGXFWIF_CTXSWITCH_PROFILE_NODELAY_EN	(IMG_UINT32_C(0x4))

#define RGXFWIF_CDM_ARBITRATION_TASK_DEMAND_EN	(IMG_UINT32_C(0x1))
#define RGXFWIF_CDM_ARBITRATION_ROUND_ROBIN_EN	(IMG_UINT32_C(0x2))

#define RGXFWIF_ISP_SCHEDMODE_VER1_IPP			(IMG_UINT32_C(0x1))
#define RGXFWIF_ISP_SCHEDMODE_VER2_ISP			(IMG_UINT32_C(0x2))
/*!
 ******************************************************************************
 * RGX firmware Init Config Data
 *****************************************************************************/

/* Flag definitions affecting the firmware globally */
#define RGXFWIF_INICFG_CTXSWITCH_MODE_RAND				(IMG_UINT32_C(0x1) << 0)
#define RGXFWIF_INICFG_CTXSWITCH_SRESET_EN				(IMG_UINT32_C(0x1) << 1)
#define RGXFWIF_INICFG_HWPERF_EN						(IMG_UINT32_C(0x1) << 2)
#define RGXFWIF_INICFG_DM_KILL_MODE_RAND_EN				(IMG_UINT32_C(0x1) << 3)
#define RGXFWIF_INICFG_POW_RASCALDUST					(IMG_UINT32_C(0x1) << 4)
/* 5 unused */
#define RGXFWIF_INICFG_FBCDC_V3_1_EN					(IMG_UINT32_C(0x1) << 6)
#define RGXFWIF_INICFG_CHECK_MLIST_EN					(IMG_UINT32_C(0x1) << 7)
#define RGXFWIF_INICFG_DISABLE_CLKGATING_EN				(IMG_UINT32_C(0x1) << 8)
/* 9 unused */
/* 10 unused */
/* 11 unused */
#define RGXFWIF_INICFG_REGCONFIG_EN						(IMG_UINT32_C(0x1) << 12)
#define RGXFWIF_INICFG_ASSERT_ON_OUTOFMEMORY			(IMG_UINT32_C(0x1) << 13)
#define RGXFWIF_INICFG_HWP_DISABLE_FILTER				(IMG_UINT32_C(0x1) << 14)
/* 15 unused */
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT			(16)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_FAST			(RGXFWIF_CTXSWITCH_PROFILE_FAST_EN << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_MEDIUM			(RGXFWIF_CTXSWITCH_PROFILE_MEDIUM_EN << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_SLOW			(RGXFWIF_CTXSWITCH_PROFILE_SLOW_EN << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_NODELAY		(RGXFWIF_CTXSWITCH_PROFILE_NODELAY_EN << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define RGXFWIF_INICFG_CTXSWITCH_PROFILE_MASK			(IMG_UINT32_C(0x7) << RGXFWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define RGXFWIF_INICFG_DISABLE_DM_OVERLAP				(IMG_UINT32_C(0x1) << 19)
#define RGXFWIF_INICFG_ASSERT_ON_HWR_TRIGGER			(IMG_UINT32_C(0x1) << 20)
#define RGXFWIF_INICFG_FABRIC_COHERENCY_ENABLED			(IMG_UINT32_C(0x1) << 21)
#define RGXFWIF_INICFG_VALIDATE_IRQ						(IMG_UINT32_C(0x1) << 22)
#define RGXFWIF_INICFG_DISABLE_PDP_EN					(IMG_UINT32_C(0x1) << 23)
#define RGXFWIF_INICFG_SPU_POWER_STATE_MASK_CHANGE_EN	(IMG_UINT32_C(0x1) << 24)
#define RGXFWIF_INICFG_WORKEST							(IMG_UINT32_C(0x1) << 25)
#define RGXFWIF_INICFG_PDVFS							(IMG_UINT32_C(0x1) << 26)
#define RGXFWIF_INICFG_CDM_ARBITRATION_SHIFT			(27)
#define RGXFWIF_INICFG_CDM_ARBITRATION_TASK_DEMAND		(RGXFWIF_CDM_ARBITRATION_TASK_DEMAND_EN << RGXFWIF_INICFG_CDM_ARBITRATION_SHIFT)
#define RGXFWIF_INICFG_CDM_ARBITRATION_ROUND_ROBIN		(RGXFWIF_CDM_ARBITRATION_ROUND_ROBIN_EN << RGXFWIF_INICFG_CDM_ARBITRATION_SHIFT)
#define RGXFWIF_INICFG_CDM_ARBITRATION_MASK				(IMG_UINT32_C(0x3) << RGXFWIF_INICFG_CDM_ARBITRATION_SHIFT)
#define RGXFWIF_INICFG_ISPSCHEDMODE_SHIFT				(29)
#define RGXFWIF_INICFG_ISPSCHEDMODE_NONE				(0)
#define RGXFWIF_INICFG_ISPSCHEDMODE_VER1_IPP			(RGXFWIF_ISP_SCHEDMODE_VER1_IPP << RGXFWIF_INICFG_ISPSCHEDMODE_SHIFT)
#define RGXFWIF_INICFG_ISPSCHEDMODE_VER2_ISP			(RGXFWIF_ISP_SCHEDMODE_VER2_ISP << RGXFWIF_INICFG_ISPSCHEDMODE_SHIFT)
#define RGXFWIF_INICFG_ISPSCHEDMODE_MASK				(RGXFWIF_INICFG_ISPSCHEDMODE_VER1_IPP |\
                                                         RGXFWIF_INICFG_ISPSCHEDMODE_VER2_ISP)
#define RGXFWIF_INICFG_VALIDATE_SOCUSC_TIMER			(IMG_UINT32_C(0x1) << 31)

#define RGXFWIF_INICFG_ALL								(0xFFFFFFFFU)

/* Extended Flag definitions affecting the firmware globally */
#define RGXFWIF_INICFG_EXT_TFBC_CONTROL_SHIFT			(0)
/* [7]   YUV10 override
 * [6:4] Quality
 * [3]   Quality enable
 * [2:1] Compression scheme
 * [0]   Lossy group */
#define RGXFWIF_INICFG_EXT_TFBC_CONTROL_MASK			(IMG_UINT32_C(0xFF)) /* RGX_CR_TFBC_COMPRESSION_CONTROL_MASKFULL */
#define RGXFWIF_INICFG_EXT_ALL							(RGXFWIF_INICFG_EXT_TFBC_CONTROL_MASK)

#define RGXFWIF_INICFG_SYS_CTXSWITCH_CLRMSK				~(RGXFWIF_INICFG_CTXSWITCH_MODE_RAND | \
														  RGXFWIF_INICFG_CTXSWITCH_SRESET_EN)

/* Flag definitions affecting only workloads submitted by a particular OS */

/*!
 * @AddToGroup ContextSwitching
 * @{
 * @Name Per-OS DM context switch configuration flags
 * @{
 */
#define RGXFWIF_INICFG_OS_CTXSWITCH_TDM_EN				(IMG_UINT32_C(0x1) << 0) /*!< Enables TDM context switch */
#define RGXFWIF_INICFG_OS_CTXSWITCH_GEOM_EN				(IMG_UINT32_C(0x1) << 1) /*!< Enables GEOM DM context switch */
#define RGXFWIF_INICFG_OS_CTXSWITCH_3D_EN				(IMG_UINT32_C(0x1) << 2) /*!< Enables FRAG DM context switch */
#define RGXFWIF_INICFG_OS_CTXSWITCH_CDM_EN				(IMG_UINT32_C(0x1) << 3) /*!< Enables CDM context switch */

#define RGXFWIF_INICFG_OS_LOW_PRIO_CS_TDM				(IMG_UINT32_C(0x1) << 4)
#define RGXFWIF_INICFG_OS_LOW_PRIO_CS_GEOM				(IMG_UINT32_C(0x1) << 5)
#define RGXFWIF_INICFG_OS_LOW_PRIO_CS_3D				(IMG_UINT32_C(0x1) << 6)
#define RGXFWIF_INICFG_OS_LOW_PRIO_CS_CDM				(IMG_UINT32_C(0x1) << 7)

#define RGXFWIF_INICFG_OS_ALL							(0xFFU)

#define RGXFWIF_INICFG_OS_CTXSWITCH_DM_ALL				(RGXFWIF_INICFG_OS_CTXSWITCH_GEOM_EN | \
														 RGXFWIF_INICFG_OS_CTXSWITCH_3D_EN | \
														 RGXFWIF_INICFG_OS_CTXSWITCH_CDM_EN | \
														 RGXFWIF_INICFG_OS_CTXSWITCH_TDM_EN)

#define RGXFWIF_INICFG_OS_CTXSWITCH_CLRMSK				~(RGXFWIF_INICFG_OS_CTXSWITCH_DM_ALL)

/*!
 * @} End of Per-OS Context switch configuration flags
 * @} End of AddToGroup ContextSwitching
 */

#define RGXFWIF_FILTCFG_TRUNCATE_HALF					(IMG_UINT32_C(0x1) << 3)
#define RGXFWIF_FILTCFG_TRUNCATE_INT					(IMG_UINT32_C(0x1) << 2)
#define RGXFWIF_FILTCFG_NEW_FILTER_MODE					(IMG_UINT32_C(0x1) << 1)

typedef IMG_UINT32 RGX_ACTIVEPM_CONF;
#define RGX_ACTIVEPM_FORCE_OFF	0U
#define RGX_ACTIVEPM_FORCE_ON	1U
#define RGX_ACTIVEPM_DEFAULT	2U

typedef IMG_UINT32 RGX_RD_POWER_ISLAND_CONF;
#define RGX_RD_POWER_ISLAND_FORCE_OFF	0U
#define RGX_RD_POWER_ISLAND_FORCE_ON	1U
#define RGX_RD_POWER_ISLAND_DEFAULT		2U

#if defined(RGX_FW_IRQ_OS_COUNTERS)
/* Unused registers re-purposed for storing counters of the Firmware's
 * interrupts for each OS
 */
#define IRQ_COUNTER_STORAGE_REGS                        \
		0x2028U, /* RGX_CR_PM_TA_MMU_FSTACK         */  \
		0x2050U, /* RGX_CR_PM_3D_MMU_FSTACK         */  \
		0x2030U, /* RGX_CR_PM_START_OF_MMU_TACONTEXT*/  \
		0x2058U, /* RGX_CR_PM_START_OF_MMU_3DCONTEXT*/  \
		0x2058U, /* RGX_CR_PM_START_OF_MMU_3DCONTEXT*/  \
		0x2058U, /* RGX_CR_PM_START_OF_MMU_3DCONTEXT*/  \
		0x2058U, /* RGX_CR_PM_START_OF_MMU_3DCONTEXT*/  \
		0x2058U, /* RGX_CR_PM_START_OF_MMU_3DCONTEXT*/
#endif

typedef struct
{
	IMG_UINT16 ui16RegNum;				/*!< Register number */
	IMG_UINT16 ui16IndirectRegNum;		/*!< Indirect register number (or 0 if not used) */
	IMG_UINT16 ui16IndirectStartVal;	/*!< Start value for indirect register */
	IMG_UINT16 ui16IndirectEndVal;		/*!< End value for indirect register */
} RGXFW_REGISTER_LIST;

#if defined(RGX_FIRMWARE)
typedef DLLIST_NODE							RGXFWIF_DLLIST_NODE;
#else
typedef struct {RGXFWIF_DEV_VIRTADDR p;
                RGXFWIF_DEV_VIRTADDR n;}	RGXFWIF_DLLIST_NODE;
#endif

typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_SIGBUFFER;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_TRACEBUF;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_SYSDATA;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_OSDATA;
#if defined(SUPPORT_TBI_INTERFACE)
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_TBIBUF;
#endif
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_HWPERFBUF;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_HWRINFOBUF;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_RUNTIME_CFG;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_GPU_UTIL_FWCB;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_REG_CFG;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_HWPERF_CTL;
typedef RGXFWIF_DEV_VIRTADDR  PRGX_HWPERF_CONFIG_MUX_CNTBLK;
typedef RGXFWIF_DEV_VIRTADDR  PRGX_HWPERF_CONFIG_CNTBLK;
typedef RGXFWIF_DEV_VIRTADDR  PRGX_HWPERF_SELECT_CUSTOM_CNTRS;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_CCB_CTL;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_CCB;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_CCB_RTN_SLOTS;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_FWMEMCONTEXT;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_FWCOMMONCONTEXT;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_ZSBUFFER;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_COMMONCTX_STATE;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_CORE_CLK_RATE;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_COUNTERBUFFER;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_FIRMWAREGCOVBUFFER;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_CCCB;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_CCCB_CTL;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_FREELIST;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_HWRTDATA;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_TIMESTAMP_ADDR;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_RF_CMD;

#if defined(SUPPORT_FW_HOST_SIDE_RECOVERY)
/*!
 * @Brief Buffer to store KM active client contexts
 */
typedef struct
{
	PRGXFWIF_FWCOMMONCONTEXT	psContext;			/*!< address of the firmware context */
} RGXFWIF_ACTIVE_CONTEXT_BUF_DATA;
#endif

/*!
 * This number is used to represent an invalid page catalogue physical address
 */
#define RGXFWIF_INVALID_PC_PHYADDR 0xFFFFFFFFFFFFFFFFLLU

/*!
 * This number is used to represent an unallocated set of page catalog base registers
 */
#define RGXFW_BIF_INVALID_PCSET 0xFFFFFFFFU

/*!
 * Firmware memory context.
 */
typedef struct
{
	IMG_DEV_PHYADDR			RGXFW_ALIGN sPCDevPAddr;	/*!< device physical address of context's page catalogue */
	IMG_UINT32				uiPageCatBaseRegSet;	/*!< associated page catalog base register (RGXFW_BIF_INVALID_PCSET == unallocated) */
	IMG_UINT32				uiBreakpointAddr; /*!< breakpoint address */
	IMG_UINT32				uiBPHandlerAddr; /*!< breakpoint handler address */
	IMG_UINT32				uiBreakpointCtl; /*!< DM and enable control for BP */
	IMG_UINT32				ui32FwMemCtxFlags; /*!< Compatibility and other flags */

#if defined(SUPPORT_CUSTOM_OSID_EMISSION) && !defined(SUPPORT_OPEN_SOURCE_DRIVER)
	IMG_UINT32              ui32OSid;
	IMG_BOOL                bOSidAxiProt;
#endif

} UNCACHED_ALIGN RGXFWIF_FWMEMCONTEXT;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_FWMEMCONTEXT) == 32,
				"RGXFWIF_FWMEMCONTEXT is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*!
 * FW context state flags
 */
#define RGXFWIF_CONTEXT_FLAGS_NEED_RESUME				(0x00000001U)
#define RGXFWIF_CONTEXT_FLAGS_MC_NEED_RESUME_MASKFULL	(0x000000FFU)
#define RGXFWIF_CONTEXT_FLAGS_TDM_HEADER_STALE			(0x00000100U)
#define RGXFWIF_CONTEXT_FLAGS_LAST_KICK_SECURE			(0x00000200U)

/*!
 * @InGroup ContextSwitching
 * @Brief Firmware GEOM/TA context suspend state (per GEOM core)
 */
typedef struct
{
	/* FW-accessible TA state which must be written out to memory on context store */
	IMG_UINT64	RGXFW_ALIGN uTAReg_VDM_CALL_STACK_POINTER;		 /*!< VDM control stream stack pointer, to store in mid-TA */
	IMG_UINT64	RGXFW_ALIGN uTAReg_VDM_CALL_STACK_POINTER_Init;	 /*!< Initial value of VDM control stream stack pointer (in case is 'lost' due to a lock-up) */
	IMG_UINT32	uTAReg_VBS_SO_PRIM[4];
	IMG_UINT16	ui16TACurrentIdx;
} UNCACHED_ALIGN RGXFWIF_TACTX_STATE_PER_GEOM;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
#define MAX_GEOM_CORE_SIZE 4

static_assert(RGX_NUM_GEOM_CORES <= MAX_GEOM_CORE_SIZE,
				"RGX_NUM_GEOM_CORES is outside of allowable range for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*!
 * @InGroup ContextSwitching
 * @Brief Firmware GEOM/TA context suspend states for all GEOM cores
 */
typedef struct
{
	/*! FW-accessible TA state which must be written out to memory on context store */
#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
	RGXFWIF_TACTX_STATE_PER_GEOM asGeomCore[MAX_GEOM_CORE_SIZE];
#else
	RGXFWIF_TACTX_STATE_PER_GEOM asGeomCore[RGX_NUM_GEOM_CORES];
#endif
} UNCACHED_ALIGN RGXFWIF_TACTX_STATE;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_TACTX_STATE) == 160,
				"RGXFWIF_TACTX_STATE is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*!
 * @InGroup ContextSwitching
 * @Brief Firmware FRAG/3D context suspend state
 */
typedef struct
{
	/* FW-accessible ISP state which must be written out to memory on context store */
	IMG_UINT32	u3DReg_PM_DEALLOCATED_MASK_STATUS; /*!< PM deallocation status */
	IMG_UINT32	u3DReg_PM_PDS_MTILEFREE_STATUS; /*!< Macro-tiles (MTs) finished status */
	IMG_UINT32	ui32CtxStateFlags;	/*!< Compatibility and other flags */
	/* au3DReg_ISP_STORE should be the last element of the structure
	 * as this is an array whose size is determined at runtime
	 * after detecting the RGX core */
	IMG_UINT32	au3DReg_ISP_STORE[]; /*!< ISP state (per-pipe) */
} UNCACHED_ALIGN RGXFWIF_3DCTX_STATE;

static_assert(sizeof(RGXFWIF_3DCTX_STATE) <= 16U,
              "Size of structure RGXFWIF_3DCTX_STATE exceeds maximum expected size.");

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_3DCTX_STATE) == 16,
				"RGXFWIF_3DCTX_STATE is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

#define RGXFWIF_CTX_USING_BUFFER_A		(0)
#define RGXFWIF_CTX_USING_BUFFER_B		(1U)

typedef struct
{
	IMG_UINT32	ui32CtxStateFlags; /*!< Target buffer and other flags */
} RGXFWIF_COMPUTECTX_STATE;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_COMPUTECTX_STATE) == 4,
				"RGXFWIF_COMPUTECTX_STATE is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

#define RGXFWIF_CONTEXT_MISC_FLAGS_STATS_PENDING   (1U << 0)
#define RGXFWIF_CONTEXT_MISC_FLAGS_HAS_DEFER_COUNT (1U << 1)

/*!
 * @InGroup WorkloadContexts
 * @Brief Firmware Common Context (or FWCC)
 */
typedef struct RGXFWIF_FWCOMMONCONTEXT_
{
	/* CCB details for this firmware context */
	PRGXFWIF_CCCB_CTL		psCCBCtl;				/*!< CCB control */
	PRGXFWIF_CCCB			psCCB;					/*!< CCB base */
	RGXFWIF_DMA_ADDR		sCCBMetaDMAAddr;

	/* Context suspend state */
	PRGXFWIF_COMMONCTX_STATE	RGXFW_ALIGN psContextState;		/*!< TA/3D context suspend state, read/written by FW */

	/* Flags e.g. for context switching */
	IMG_UINT32				ui32FWComCtxFlags;
	IMG_INT32				i32Priority;  /*!< Priority level */
	IMG_UINT32				ui32PrioritySeqNum;

	/* Framework state */
	PRGXFWIF_RF_CMD			RGXFW_ALIGN psRFCmd;		/*!< Register updates for Framework */

	/* Misc and compatibility flags */
	IMG_UINT32				ui32MiscFlags;

	/* Statistic updates waiting to be passed back to the host... */
	IMG_INT32				i32StatsNumStores;		/*!< Number of stores on this context since last update */
	IMG_INT32				i32StatsNumOutOfMemory;		/*!< Number of OOMs on this context since last update */
	IMG_INT32				i32StatsNumPartialRenders;	/*!< Number of PRs on this context since last update */
	RGXFWIF_DM				eDM;				/*!< Data Master type */
	IMG_UINT64				RGXFW_ALIGN  ui64WaitSignalAddress;	/*!< Device Virtual Address of the signal the context is waiting on */
	RGXFWIF_DLLIST_NODE		RGXFW_ALIGN  sWaitSignalNode;			/*!< List entry for the wait-signal list */
	RGXFWIF_DLLIST_NODE		RGXFW_ALIGN  sBufStalledNode;			/*!< List entry for the buffer stalled list */
	IMG_UINT64				RGXFW_ALIGN  ui64CBufQueueCtrlAddr;	/*!< Address of the circular buffer queue pointers */

	IMG_UINT64				RGXFW_ALIGN  ui64RobustnessAddress;
	IMG_UINT32				ui32MaxDeadlineMS;			/*!< Max HWR deadline limit in ms */
	bool					bReadOffsetNeedsReset;			/*!< Following HWR circular buffer read-offset needs resetting */

	RGXFWIF_DLLIST_NODE		RGXFW_ALIGN sWaitingNode;		/*!< List entry for the waiting list */
	RGXFWIF_DLLIST_NODE		RGXFW_ALIGN sRunNode;			/*!< List entry for the run list */
	RGXFWIF_UFO				sLastFailedUFO;			/*!< UFO that last failed (or NULL) */

	PRGXFWIF_FWMEMCONTEXT	psFWMemContext;					/*!< Memory context */

	/* References to the host side originators */
	IMG_UINT32				ui32ServerCommonContextID;	/*!< the Server Common Context */
	IMG_UINT32				ui32PID;			/*!< associated process ID */

	IMG_BOOL				bGeomOOMDisabled;		/*!< True when Geom DM OOM is not allowed */
	IMG_CHAR				szProcName[RGXFW_PROCESS_NAME_LEN];	/*!< User process name */

	IMG_UINT32				ui32DeferCount;		/*!< Number of context defers before forced scheduling of context */
} UNCACHED_ALIGN RGXFWIF_FWCOMMONCONTEXT;

static_assert(sizeof(RGXFWIF_FWCOMMONCONTEXT) <= 256U,
              "Size of structure RGXFWIF_FWCOMMONCONTEXT exceeds maximum expected size.");

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_FWCOMMONCONTEXT) == 168,
				"RGXFWIF_FWCOMMONCONTEXT is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

typedef IMG_UINT64 RGXFWIF_TRP_CHECKSUM_TQ[RGX_TRP_MAX_NUM_CORES][1];
typedef IMG_UINT64 RGXFWIF_TRP_CHECKSUM_2D[RGX_TRP_MAX_NUM_CORES][2];
typedef IMG_UINT64 RGXFWIF_TRP_CHECKSUM_3D[RGX_TRP_MAX_NUM_CORES][4];
typedef IMG_UINT64 RGXFWIF_TRP_CHECKSUM_GEOM[RGX_TRP_MAX_NUM_CORES][2];

/*!
 * @InGroup WorkloadContexts
 * @Brief Firmware render context.
 */
typedef struct
{
	RGXFWIF_FWCOMMONCONTEXT	sTAContext;				/*!< Firmware context for the TA */
	RGXFWIF_FWCOMMONCONTEXT	s3DContext;				/*!< Firmware context for the 3D */

	RGXFWIF_STATIC_RENDERCONTEXT_STATE sStaticRenderContextState;

	IMG_UINT32			ui32WorkEstCCBSubmitted; /*!< Number of commands submitted to the WorkEst FW CCB */

	IMG_UINT32			ui32FwRenderCtxFlags; /*!< Compatibility and other flags */

#if defined(SUPPORT_TRP)
	RGXFWIF_TRP_CHECKSUM_3D		aui64TRPChecksums3D;	/*!< Used by Firmware to store checksums during 3D WRR */
	RGXFWIF_TRP_CHECKSUM_GEOM	aui64TRPChecksumsGeom;	/*!< Used by Firmware to store checksums during TA WRR */
	RGXFWIF_DM			eTRPGeomCoreAffinity; /* !< Represent the DM affinity for pending 2nd TRP pass of GEOM otherwise points RGXFWIF_DM_MAX. */
#endif
} UNCACHED_ALIGN RGXFWIF_FWRENDERCONTEXT;

/*!
	Firmware compute context.
*/
typedef struct
{
	RGXFWIF_FWCOMMONCONTEXT sCDMContext;				/*!< Firmware context for the CDM */

	RGXFWIF_STATIC_COMPUTECONTEXT_STATE sStaticComputeContextState;

	IMG_UINT32			ui32WorkEstCCBSubmitted; /*!< Number of commands submitted to the WorkEst FW CCB */

	IMG_UINT32 ui32ComputeCtxFlags; /*!< Compatibility and other flags */

	IMG_UINT32		ui32WGPState;
	IMG_UINT32		aui32WGPChecksum[RGX_WGP_MAX_NUM_CORES];
} UNCACHED_ALIGN RGXFWIF_FWCOMPUTECONTEXT;

/*!
	Firmware TDM context.
*/
typedef struct
{
	RGXFWIF_FWCOMMONCONTEXT	sTDMContext;				/*!< Firmware context for the TDM */

	IMG_UINT32			ui32WorkEstCCBSubmitted; /*!< Number of commands submitted to the WorkEst FW CCB */

} UNCACHED_ALIGN RGXFWIF_FWTDMCONTEXT;

/*!
 * @InGroup WorkloadContexts
 * @Brief Firmware transfer context.
 */
typedef struct
{
	RGXFWIF_FWCOMMONCONTEXT	sTQContext;			 /*!< Firmware context for TQ3D */

#if defined(SUPPORT_TRP)
	IMG_UINT32				ui32TRPState;		/*!< Used by Firmware to track current state of a protected kick */
	RGXFWIF_TRP_CHECKSUM_TQ aui64TRPChecksumsTQ;/*!< Used by Firmware to store checksums during TQ WRR */
#endif
} UNCACHED_ALIGN RGXFWIF_FWTRANSFERCONTEXT;

/*!
 ******************************************************************************
 * Defines for CMD_TYPE corruption detection and forward compatibility check
 *****************************************************************************/

/* CMD_TYPE 32bit contains:
 * 31:16	Reserved for magic value to detect corruption (16 bits)
 * 15		Reserved for RGX_CCB_TYPE_TASK (1 bit)
 * 14:0		Bits available for CMD_TYPEs (15 bits) */


/* Magic value to detect corruption */
#define RGX_CMD_MAGIC_DWORD			IMG_UINT32_C(0x2ABC)
#define RGX_CMD_MAGIC_DWORD_MASK	(0xFFFF0000U)
#define RGX_CMD_MAGIC_DWORD_SHIFT	(16U)
#define RGX_CMD_MAGIC_DWORD_SHIFTED	(RGX_CMD_MAGIC_DWORD << RGX_CMD_MAGIC_DWORD_SHIFT)

/*!
 * @InGroup KCCBTypes ClientCCBTypes
 * @Brief Generic CCB control structure
 */
typedef struct
{
	volatile IMG_UINT32		ui32WriteOffset;		/*!< write offset into array of commands (MUST be aligned to 16 bytes!) */
	volatile IMG_UINT32		ui32ReadOffset;			/*!< read offset into array of commands */
	IMG_UINT32				ui32WrapMask;			/*!< Offset wrapping mask (Total capacity of the CCB - 1) */
	IMG_UINT32				ui32CmdSize;			/*!< size of each command in bytes */
} UNCACHED_ALIGN RGXFWIF_CCB_CTL;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_CCB_CTL) == 16,
				"RGXFWIF_CCB_CTL is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*!
 * @Defgroup KCCBTypes Kernel CCB data interface
 * @Brief Types grouping data structures and defines used in realising the KCCB functionality
 * @{
 */

#define RGXFWIF_MMUCACHEDATA_FLAGS_PT      (0x1U) /* MMU_CTRL_INVAL_PT_EN */
#define RGXFWIF_MMUCACHEDATA_FLAGS_PD      (0x2U) /* MMU_CTRL_INVAL_PD_EN */
#define RGXFWIF_MMUCACHEDATA_FLAGS_PC      (0x4U) /* MMU_CTRL_INVAL_PC_EN */

#if !defined(__KERNEL__)

#if !defined(RGX_FEATURE_SLC_VIVT)
#define RGXFWIF_MMUCACHEDATA_FLAGS_PMTLB   (0x10U) /* can't use PM_TLB0 bit from BIFPM_CTRL reg because it collides with PT bit from BIF_CTRL reg */
#define RGXFWIF_MMUCACHEDATA_FLAGS_TLB     (RGXFWIF_MMUCACHEDATA_FLAGS_PMTLB | 0x8U) /* BIF_CTRL_INVAL_TLB1_EN */
#define RGXFWIF_MMUCACHEDATA_FLAGS_CTX_ALL (0x0U) /* not used */

#else /* RGX_FEATURE_SLC_VIVT */
#define RGXFWIF_MMUCACHEDATA_FLAGS_PMTLB   (0x0U) /* not used */
#define RGXFWIF_MMUCACHEDATA_FLAGS_TLB     (0x0U) /* not used */
#define RGXFWIF_MMUCACHEDATA_FLAGS_CTX_ALL (0x800U) /* MMU_CTRL_INVAL_ALL_CONTEXTS_EN */
#endif

#else
#define RGXFWIF_MMUCACHEDATA_FLAGS_PMTLB   (0x10U) /* can't use PM_TLB0 bit from BIFPM_CTRL reg because it collides with PT bit from BIF_CTRL reg */
#define RGXFWIF_MMUCACHEDATA_FLAGS_TLB     (RGXFWIF_MMUCACHEDATA_FLAGS_PMTLB | 0x8U) /* BIF_CTRL_INVAL_TLB1_EN */
#define RGXFWIF_MMUCACHEDATA_FLAGS_CTX_ALL (0x800U) /* MMU_CTRL_INVAL_ALL_CONTEXTS_EN */
#endif

#define RGXFWIF_MMUCACHEDATA_FLAGS_INTERRUPT (0x4000000U) /* indicates FW should interrupt the host */

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_MMUCACHE type command
 */
typedef struct
{
	IMG_UINT32            ui32CacheFlags;
	RGXFWIF_DEV_VIRTADDR  sMMUCacheSync;
	IMG_UINT32            ui32MMUCacheSyncUpdateValue;
} RGXFWIF_MMUCACHEDATA;

#define RGXFWIF_BPDATA_FLAGS_ENABLE (1U << 0)
#define RGXFWIF_BPDATA_FLAGS_WRITE  (1U << 1)
#define RGXFWIF_BPDATA_FLAGS_CTL    (1U << 2)
#define RGXFWIF_BPDATA_FLAGS_REGS   (1U << 3)

typedef struct
{
	PRGXFWIF_FWMEMCONTEXT		psFWMemContext;		/*!< Memory context */
	IMG_UINT32					ui32BPAddr;			/*!< Breakpoint address */
	IMG_UINT32					ui32HandlerAddr;	/*!< Breakpoint handler */
	IMG_UINT32					ui32BPDM;			/*!< Breakpoint control */
	IMG_UINT32					ui32BPDataFlags;
	IMG_UINT32					ui32TempRegs;		/*!< Number of temporary registers to overallocate */
	IMG_UINT32					ui32SharedRegs;		/*!< Number of shared registers to overallocate */
	IMG_UINT64 RGXFW_ALIGN		ui64SpillAddr;
	RGXFWIF_DM					eDM;				/*!< DM associated with the breakpoint */
} RGXFWIF_BPDATA;

#define RGXFWIF_KCCB_CMD_KICK_DATA_MAX_NUM_CLEANUP_CTLS (RGXFWIF_PRBUFFER_MAXSUPPORTED + 1U) /* +1 is RTDATASET cleanup */

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_KICK type command
 */
typedef struct
{
	PRGXFWIF_FWCOMMONCONTEXT	psContext;			/*!< address of the firmware context */
	IMG_UINT32					ui32CWoffUpdate;	/*!< Client CCB woff update */
	IMG_UINT32					ui32CWrapMaskUpdate; /*!< Client CCB wrap mask update after CCCB growth */
	IMG_UINT32					ui32NumCleanupCtl;		/*!< number of CleanupCtl pointers attached */
	PRGXFWIF_CLEANUP_CTL		apsCleanupCtl[RGXFWIF_KCCB_CMD_KICK_DATA_MAX_NUM_CLEANUP_CTLS]; /*!< CleanupCtl structures associated with command */
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	IMG_UINT32					ui32WorkEstCmdHeaderOffset; /*!< offset to the CmdHeader which houses the workload estimation kick data. */
#endif
} RGXFWIF_KCCB_CMD_KICK_DATA;

/*!
 * @Brief Command data for @Ref RGXFWIF_KCCB_CMD_COMBINED_TA_3D_KICK type command
 */
typedef struct
{
	RGXFWIF_KCCB_CMD_KICK_DATA	sTACmdKickData; /*!< GEOM DM kick command data */
	RGXFWIF_KCCB_CMD_KICK_DATA	s3DCmdKickData; /*!< FRAG DM kick command data */
} RGXFWIF_KCCB_CMD_COMBINED_TA_3D_KICK_DATA;

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_FORCE_UPDATE type command
 */
typedef struct
{
	PRGXFWIF_FWCOMMONCONTEXT	psContext;			/*!< address of the firmware context */
	IMG_UINT32					ui32CCBFenceOffset;	/*!< Client CCB fence offset */
} RGXFWIF_KCCB_CMD_FORCE_UPDATE_DATA;

/*!
 * @Brief Resource types supported by \ref RGXFWIF_KCCB_CMD_CLEANUP type command
 */
typedef enum
{
	RGXFWIF_CLEANUP_FWCOMMONCONTEXT,		/*!< FW common context cleanup */
	RGXFWIF_CLEANUP_HWRTDATA,				/*!< FW HW RT data cleanup */
	RGXFWIF_CLEANUP_FREELIST,				/*!< FW freelist cleanup */
	RGXFWIF_CLEANUP_ZSBUFFER,				/*!< FW ZS Buffer cleanup */
} RGXFWIF_CLEANUP_TYPE;

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_CLEANUP type command
 */
typedef struct
{
	RGXFWIF_CLEANUP_TYPE			eCleanupType;			/*!< Cleanup type */
	union {
		PRGXFWIF_FWCOMMONCONTEXT	psContext;				/*!< FW common context to cleanup */
		PRGXFWIF_HWRTDATA			psHWRTData;				/*!< HW RT to cleanup */
		PRGXFWIF_FREELIST			psFreelist;				/*!< Freelist to cleanup */
		PRGXFWIF_ZSBUFFER			psZSBuffer;				/*!< ZS Buffer to cleanup */
	} uCleanupData;
} RGXFWIF_CLEANUP_REQUEST;

/*!
 * @Brief Type of power requests supported in \ref RGXFWIF_KCCB_CMD_POW type command
 */
typedef enum
{
	RGXFWIF_POW_OFF_REQ = 1,           /*!< GPU power-off request */
	RGXFWIF_POW_FORCED_IDLE_REQ,       /*!< Force-idle related request */
	RGXFWIF_POW_NUM_UNITS_CHANGE,      /*!< Request to change default powered scalable units */
	RGXFWIF_POW_APM_LATENCY_CHANGE     /*!< Request to change the APM latency period */
} RGXFWIF_POWER_TYPE;

/*!
 * @Brief Supported force-idle related requests with \ref RGXFWIF_POW_FORCED_IDLE_REQ type request
 */
typedef enum
{
	RGXFWIF_POWER_FORCE_IDLE = 1,      /*!< Request to force-idle GPU */
	RGXFWIF_POWER_CANCEL_FORCED_IDLE,  /*!< Request to cancel a previously successful force-idle transition */
	RGXFWIF_POWER_HOST_TIMEOUT,        /*!< Notification that host timed-out waiting for force-idle state */
} RGXFWIF_POWER_FORCE_IDLE_TYPE;

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_POW type command
 */
typedef struct
{
	RGXFWIF_POWER_TYPE					ePowType;					/*!< Type of power request */
	union
	{
		IMG_UINT32						ui32NumOfDusts;			/*!< Number of active Dusts */
		IMG_BOOL						bForced;				/*!< If the operation is mandatory */
		RGXFWIF_POWER_FORCE_IDLE_TYPE	ePowRequestType;		/*!< Type of Request. Consolidating Force Idle, Cancel Forced Idle, Host Timeout */
	} uPowerReqData;
} RGXFWIF_POWER_REQUEST;

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_SLCFLUSHINVAL type command
 */
typedef struct
{
	PRGXFWIF_FWCOMMONCONTEXT psContext; /*!< Context to fence on (only useful when bDMContext == TRUE) */
	IMG_BOOL    bInval;                 /*!< Invalidate the cache as well as flushing */
	IMG_BOOL    bDMContext;             /*!< The data to flush/invalidate belongs to a specific DM context */
	IMG_UINT64	RGXFW_ALIGN ui64Address;	/*!< Optional address of range (only useful when bDMContext == FALSE) */
	IMG_UINT64	RGXFW_ALIGN ui64Size;		/*!< Optional size of range (only useful when bDMContext == FALSE) */
} RGXFWIF_SLCFLUSHINVALDATA;

typedef enum
{
	RGXFWIF_HWPERF_CTRL_TOGGLE = 0,
	RGXFWIF_HWPERF_CTRL_SET    = 1,
	RGXFWIF_HWPERF_CTRL_EMIT_FEATURES_EV = 2
} RGXFWIF_HWPERF_UPDATE_CONFIG;

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_HWPERF_UPDATE_CONFIG type command
 */
typedef struct
{
	RGXFWIF_HWPERF_UPDATE_CONFIG eOpCode; /*!< Control operation code */
	IMG_UINT64	RGXFW_ALIGN	ui64Mask;   /*!< Mask of events to toggle */
} RGXFWIF_HWPERF_CTRL;

typedef struct
{
	IMG_UINT32                ui32NumBlocks;    /*!< Number of RGX_HWPERF_CONFIG_MUX_CNTBLK in the array */
	PRGX_HWPERF_CONFIG_MUX_CNTBLK sBlockConfigs;    /*!< Address of the RGX_HWPERF_CONFIG_MUX_CNTBLK array */
} RGXFWIF_HWPERF_CONFIG_ENABLE_BLKS;

typedef struct
{
	IMG_UINT32                ui32NumBlocks;    /*!< Number of RGX_HWPERF_CONFIG_CNTBLK in the array */
	PRGX_HWPERF_CONFIG_CNTBLK sBlockConfigs;    /*!< Address of the RGX_HWPERF_CONFIG_CNTBLK array */
} RGXFWIF_HWPERF_CONFIG_DA_BLKS;

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_CORECLKSPEEDCHANGE type command
 */
typedef struct
{
	IMG_UINT32	ui32NewClockSpeed;			/*!< New clock speed */
} RGXFWIF_CORECLKSPEEDCHANGE_DATA;

#define RGXFWIF_HWPERF_CTRL_BLKS_MAX	16U

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_HWPERF_CTRL_BLKS type command
 */
typedef struct
{
	bool		bEnable;
	IMG_UINT32	ui32NumBlocks;                              /*!< Number of block IDs in the array */
	IMG_UINT16	aeBlockIDs[RGXFWIF_HWPERF_CTRL_BLKS_MAX];   /*!< Array of RGX_HWPERF_CNTBLK_ID values */
} RGXFWIF_HWPERF_CTRL_BLKS;


typedef struct
{
	IMG_UINT16                      ui16CustomBlock;
	IMG_UINT16                      ui16NumCounters;
	PRGX_HWPERF_SELECT_CUSTOM_CNTRS sCustomCounterIDs;
} RGXFWIF_HWPERF_SELECT_CUSTOM_CNTRS;

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_ZSBUFFER_BACKING_UPDATE & \ref RGXFWIF_KCCB_CMD_ZSBUFFER_UNBACKING_UPDATE type commands
 */
typedef struct
{
	RGXFWIF_DEV_VIRTADDR	sZSBufferFWDevVAddr;				/*!< ZS-Buffer FW address */
	IMG_BOOL				bDone;								/*!< action backing/unbacking succeeded */
} RGXFWIF_ZSBUFFER_BACKING_DATA;

#if defined(SUPPORT_VALIDATION)
typedef struct
{
	IMG_UINT32 ui32RegWidth;
	IMG_BOOL   bWriteOp;
	IMG_UINT32 ui32RegAddr;
	IMG_UINT64 RGXFW_ALIGN ui64RegVal;
} RGXFWIF_RGXREG_DATA;

typedef struct
{
	IMG_UINT64 ui64BaseAddress;
	PRGXFWIF_FWCOMMONCONTEXT psContext;
	IMG_UINT32 ui32Size;
} RGXFWIF_GPUMAP_DATA;
#endif

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_FREELIST_GROW_UPDATE type command
 */
typedef struct
{
	RGXFWIF_DEV_VIRTADDR	sFreeListFWDevVAddr;				/*!< Freelist FW address */
	IMG_UINT32				ui32DeltaPages;						/*!< Amount of the Freelist change */
	IMG_UINT32				ui32NewPages;						/*!< New amount of pages on the freelist (including ready pages) */
	IMG_UINT32              ui32ReadyPages;                     /*!< Number of ready pages to be held in reserve until OOM */
} RGXFWIF_FREELIST_GS_DATA;

#define RGXFWIF_MAX_FREELISTS_TO_RECONSTRUCT         (MAX_HW_TA3DCONTEXTS * RGXFW_MAX_FREELISTS * 2U)
#define RGXFWIF_FREELISTS_RECONSTRUCTION_FAILED_FLAG 0x80000000U

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_FREELISTS_RECONSTRUCTION_UPDATE type command
 */
typedef struct
{
	IMG_UINT32			ui32FreelistsCount;
	IMG_UINT32			aui32FreelistIDs[RGXFWIF_MAX_FREELISTS_TO_RECONSTRUCT];
} RGXFWIF_FREELISTS_RECONSTRUCTION_DATA;

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_NOTIFY_WRITE_OFFSET_UPDATE type command
 */
typedef struct
{
	PRGXFWIF_FWCOMMONCONTEXT  psContext; /*!< Context to that may need to be resumed following write offset update */
} UNCACHED_ALIGN RGXFWIF_WRITE_OFFSET_UPDATE_DATA;

/*!
 ******************************************************************************
 * Proactive DVFS Structures
 *****************************************************************************/
#define NUM_OPP_VALUES 16

typedef struct
{
	IMG_UINT32			ui32Volt; /* V  */
	IMG_UINT32			ui32Freq; /* Hz */
} UNCACHED_ALIGN PDVFS_OPP;

typedef struct
{
	PDVFS_OPP		asOPPValues[NUM_OPP_VALUES];
#if defined(DEBUG)
	IMG_UINT32		ui32MinOPPPoint;
#endif
	IMG_UINT32		ui32MaxOPPPoint;
} UNCACHED_ALIGN RGXFWIF_PDVFS_OPP;

typedef struct
{
	IMG_UINT32 ui32MaxOPPPoint;
} UNCACHED_ALIGN RGXFWIF_PDVFS_MAX_FREQ_DATA;

typedef struct
{
	IMG_UINT32 ui32MinOPPPoint;
} UNCACHED_ALIGN RGXFWIF_PDVFS_MIN_FREQ_DATA;

/*!
 ******************************************************************************
 * Register configuration structures
 *****************************************************************************/

#define RGXFWIF_REG_CFG_MAX_SIZE 512

typedef enum
{
	RGXFWIF_REGCFG_CMD_ADD				= 101,
	RGXFWIF_REGCFG_CMD_CLEAR			= 102,
	RGXFWIF_REGCFG_CMD_ENABLE			= 103,
	RGXFWIF_REGCFG_CMD_DISABLE			= 104
} RGXFWIF_REGDATA_CMD_TYPE;

typedef enum
{
	RGXFWIF_REG_CFG_TYPE_PWR_ON=0,      /* Sidekick power event */
	RGXFWIF_REG_CFG_TYPE_DUST_CHANGE,   /* Rascal / dust power event */
	RGXFWIF_REG_CFG_TYPE_TA,            /* TA kick */
	RGXFWIF_REG_CFG_TYPE_3D,            /* 3D kick */
	RGXFWIF_REG_CFG_TYPE_CDM,           /* Compute kick */
	RGXFWIF_REG_CFG_TYPE_TLA,           /* TLA kick */
	RGXFWIF_REG_CFG_TYPE_TDM,           /* TDM kick */
	RGXFWIF_REG_CFG_TYPE_ALL            /* Applies to all types. Keep as last element */
} RGXFWIF_REG_CFG_TYPE;

typedef struct
{
	IMG_UINT64		ui64Addr;
	IMG_UINT64		ui64Mask;
	IMG_UINT64		ui64Value;
} RGXFWIF_REG_CFG_REC;

typedef struct
{
	RGXFWIF_REGDATA_CMD_TYPE         eCmdType;
	RGXFWIF_REG_CFG_TYPE             eRegConfigType;
	RGXFWIF_REG_CFG_REC RGXFW_ALIGN  sRegConfig;

} RGXFWIF_REGCONFIG_DATA;

typedef struct
{
	/**
	 * PDump WRW command write granularity is 32 bits.
	 * Add padding to ensure array size is 32 bit granular.
	 */
	IMG_UINT8           RGXFW_ALIGN  aui8NumRegsType[PVR_ALIGN((IMG_UINT32)RGXFWIF_REG_CFG_TYPE_ALL,sizeof(IMG_UINT32))];
	RGXFWIF_REG_CFG_REC RGXFW_ALIGN  asRegConfigs[RGXFWIF_REG_CFG_MAX_SIZE];
} UNCACHED_ALIGN RGXFWIF_REG_CFG;

typedef enum
{
	RGXFWIF_OS_ONLINE = 1,
	RGXFWIF_OS_OFFLINE
} RGXFWIF_OS_STATE_CHANGE;

/*!
 * @Brief Command data for \ref RGXFWIF_KCCB_CMD_OS_ONLINE_STATE_CONFIGURE type command
 */
typedef struct
{
	IMG_UINT32 ui32DriverID;
	RGXFWIF_OS_STATE_CHANGE eNewOSState;
} UNCACHED_ALIGN RGXFWIF_OS_STATE_CHANGE_DATA;

typedef enum
{
	RGXFWIF_PWR_COUNTER_DUMP_START = 1,
	RGXFWIF_PWR_COUNTER_DUMP_STOP,
	RGXFWIF_PWR_COUNTER_DUMP_SAMPLE,
} RGXFWIF_COUNTER_DUMP_REQUEST;

typedef struct
{
	RGXFWIF_COUNTER_DUMP_REQUEST eCounterDumpRequest;
}  RGXFW_ALIGN RGXFWIF_COUNTER_DUMP_DATA;

/*!
 * @Brief List of command types supported by the Kernel CCB
 */
typedef enum
{
	/* Common commands */
	RGXFWIF_KCCB_CMD_KICK								= 101U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< DM workload kick command */
	RGXFWIF_KCCB_CMD_MMUCACHE							= 102U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< MMU cache invalidation request */
	RGXFWIF_KCCB_CMD_BP									= 103U | RGX_CMD_MAGIC_DWORD_SHIFTED,
	RGXFWIF_KCCB_CMD_SLCFLUSHINVAL						= 104U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< SLC flush and invalidation request */
	RGXFWIF_KCCB_CMD_CLEANUP							= 105U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Requests cleanup of a FW resource (type specified in the command data) */
	RGXFWIF_KCCB_CMD_ZSBUFFER_BACKING_UPDATE			= 106U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Backing for on-demand ZS-Buffer done */
	RGXFWIF_KCCB_CMD_ZSBUFFER_UNBACKING_UPDATE			= 107U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Unbacking for on-demand ZS-Buffer done */
	RGXFWIF_KCCB_CMD_FREELIST_GROW_UPDATE				= 108U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Freelist Grow done */
	RGXFWIF_KCCB_CMD_FREELISTS_RECONSTRUCTION_UPDATE	= 109U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Freelists Reconstruction done */
	RGXFWIF_KCCB_CMD_NOTIFY_WRITE_OFFSET_UPDATE			= 110U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Informs the firmware that the host has added more data to a CDM2 Circular Buffer */
	RGXFWIF_KCCB_CMD_HEALTH_CHECK						= 111U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Health check request */
	RGXFWIF_KCCB_CMD_FORCE_UPDATE						= 112U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Forcing signalling of all unmet UFOs for a given CCB offset */
	RGXFWIF_KCCB_CMD_COMBINED_TA_3D_KICK				= 113U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< There is a TA and a 3D command in this single kick */
	RGXFWIF_KCCB_CMD_OS_ONLINE_STATE_CONFIGURE			= 114U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Informs the FW that a Guest OS has come online / offline. */

	/* Commands only permitted to the native or host OS */
	RGXFWIF_KCCB_CMD_POW								= 200U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Power request */
	RGXFWIF_KCCB_CMD_REGCONFIG							= 201U | RGX_CMD_MAGIC_DWORD_SHIFTED,
	RGXFWIF_KCCB_CMD_CORECLKSPEEDCHANGE					= 202U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Core clock speed change event */
	RGXFWIF_KCCB_CMD_LOGTYPE_UPDATE						= 203U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Ask the firmware to update its cached ui32LogType value from the (shared) tracebuf control structure */
	RGXFWIF_KCCB_CMD_PDVFS_LIMIT_MAX_FREQ				= 204U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Set a maximum frequency/OPP point */
	RGXFWIF_KCCB_CMD_VZ_DRV_ARRAY_CHANGE				= 205U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Changes the priority/group for a particular driver. It can only be serviced for the Host DDK */
	RGXFWIF_KCCB_CMD_STATEFLAGS_CTRL					= 206U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Set or clear firmware state flags */
	RGXFWIF_KCCB_CMD_PDVFS_LIMIT_MIN_FREQ				= 207U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Set a minimum frequency/OPP point */
	RGXFWIF_KCCB_CMD_PHR_CFG							= 208U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Configure Periodic Hardware Reset behaviour */
#if defined(SUPPORT_VALIDATION)
	RGXFWIF_KCCB_CMD_RGXREG								= 209U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Read RGX Register from FW */
#endif
	RGXFWIF_KCCB_CMD_WDG_CFG							= 210U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Configure Safety Firmware Watchdog */
	RGXFWIF_KCCB_CMD_COUNTER_DUMP						= 211U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Controls counter dumping in the FW */
#if defined(SUPPORT_VALIDATION)
	RGXFWIF_KCCB_CMD_GPUMAP								= 212U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Request a FW GPU mapping which is written into by the FW with a pattern */
#endif

	/* HWPerf commands */
	RGXFWIF_KCCB_CMD_HWPERF_UPDATE_CONFIG				= 300U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Configure HWPerf events (to be generated) and HWPerf buffer address (if required) */
	RGXFWIF_KCCB_CMD_HWPERF_CONFIG_BLKS					= 301U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Configure directly addressable counters for HWPerf */
	RGXFWIF_KCCB_CMD_HWPERF_CTRL_BLKS					= 302U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Enable or disable multiple HWPerf blocks (reusing existing configuration) */
	RGXFWIF_KCCB_CMD_HWPERF_CONFIG_ENABLE_BLKS			= 303U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Configure, clear and enable multiple HWPerf blocks */
	RGXFWIF_KCCB_CMD_HWPERF_SELECT_CUSTOM_CNTRS			= 304U | RGX_CMD_MAGIC_DWORD_SHIFTED, /*!< Configure the custom counters for HWPerf */

} RGXFWIF_KCCB_CMD_TYPE;

#define RGXFWIF_LAST_ALLOWED_GUEST_KCCB_CMD (RGXFWIF_KCCB_CMD_POW - 1)

/*! @Brief Kernel CCB command packet */
typedef struct
{
	RGXFWIF_KCCB_CMD_TYPE  eCmdType;      /*!< Command type */
	IMG_UINT32             ui32KCCBFlags; /*!< Compatibility and other flags */

	/* NOTE: Make sure that uCmdData is the last member of this struct
	 * This is to calculate actual command size for device mem copy.
	 * (Refer RGXGetCmdMemCopySize())
	 * */
	union
	{
		RGXFWIF_KCCB_CMD_KICK_DATA			sCmdKickData;			/*!< Data for Kick command */
		RGXFWIF_KCCB_CMD_COMBINED_TA_3D_KICK_DATA	sCombinedTA3DCmdKickData;	/*!< Data for combined TA/3D Kick command */
		RGXFWIF_MMUCACHEDATA				sMMUCacheData;			/*!< Data for MMU cache command */
		RGXFWIF_BPDATA						sBPData;				/*!< Data for Breakpoint Commands */
		RGXFWIF_SLCFLUSHINVALDATA			sSLCFlushInvalData;		/*!< Data for SLC Flush/Inval commands */
		RGXFWIF_CLEANUP_REQUEST				sCleanupData;			/*!< Data for cleanup commands */
		RGXFWIF_POWER_REQUEST				sPowData;				/*!< Data for power request commands */
		RGXFWIF_HWPERF_CTRL					sHWPerfCtrl;			/*!< Data for HWPerf control command */
		RGXFWIF_HWPERF_CONFIG_ENABLE_BLKS	sHWPerfCfgEnableBlks;	/*!< Data for HWPerf configure, clear and enable performance counter block command */
		RGXFWIF_HWPERF_CTRL_BLKS			sHWPerfCtrlBlks;		/*!< Data for HWPerf enable or disable performance counter block commands */
		RGXFWIF_HWPERF_SELECT_CUSTOM_CNTRS	sHWPerfSelectCstmCntrs;	/*!< Data for HWPerf configure the custom counters to read */
		RGXFWIF_HWPERF_CONFIG_DA_BLKS		sHWPerfCfgDABlks;		/*!< Data for HWPerf configure Directly Addressable blocks */
		RGXFWIF_CORECLKSPEEDCHANGE_DATA		sCoreClkSpeedChangeData;/*!< Data for core clock speed change */
		RGXFWIF_ZSBUFFER_BACKING_DATA		sZSBufferBackingData;	/*!< Feedback for Z/S Buffer backing/unbacking */
		RGXFWIF_FREELIST_GS_DATA			sFreeListGSData;		/*!< Feedback for Freelist grow/shrink */
		RGXFWIF_FREELISTS_RECONSTRUCTION_DATA	sFreeListsReconstructionData;	/*!< Feedback for Freelists reconstruction */
		RGXFWIF_REGCONFIG_DATA				sRegConfigData;			/*!< Data for custom register configuration */
		RGXFWIF_WRITE_OFFSET_UPDATE_DATA    sWriteOffsetUpdateData; /*!< Data for informing the FW about the write offset update */
		RGXFWIF_PDVFS_MAX_FREQ_DATA			sPDVFSMaxFreqData;		/*!< Data for setting the max frequency/OPP */
		RGXFWIF_PDVFS_MIN_FREQ_DATA			sPDVFSMinFreqData;		/*!< Data for setting the min frequency/OPP */
		RGXFWIF_OS_STATE_CHANGE_DATA        sCmdOSOnlineStateData;  /*!< Data for updating the Guest Online states */
		RGXFWIF_DEV_VIRTADDR                sTBIBuffer;             /*!< Dev address for TBI buffer allocated on demand */
		RGXFWIF_COUNTER_DUMP_DATA			sCounterDumpConfigData; /*!< Data for dumping of register ranges */
		RGXFWIF_KCCB_CMD_FORCE_UPDATE_DATA  sForceUpdateData;       /*!< Data for signalling all unmet fences for a given CCB */
#if defined(SUPPORT_VALIDATION) && !defined(SUPPORT_OPEN_SOURCE_DRIVER)
		RGXFWIF_RGXREG_DATA                 sFwRgxData;             /*!< Data for reading off an RGX register */
		RGXFWIF_GPUMAP_DATA                 sGPUMapData;            /*!< Data for requesting a FW GPU mapping which is written into by the FW with a pattern */
#endif
	} UNCACHED_ALIGN uCmdData;
} UNCACHED_ALIGN RGXFWIF_KCCB_CMD;

RGX_FW_STRUCT_SIZE_ASSERT(RGXFWIF_KCCB_CMD);

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_KCCB_CMD) == 64,
				"RGXFWIF_KCCB_CMD is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*! @} End of KCCBTypes */

/*!
 * @Defgroup FWCCBTypes Firmware CCB data interface
 * @Brief Types grouping data structures and defines used in realising the Firmware CCB functionality
 * @{
 */

/*!
 ******************************************************************************
 * @Brief Command data of the \ref RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING and the
 * \ref RGXFWIF_FWCCB_CMD_ZSBUFFER_UNBACKING Firmware CCB commands
 *****************************************************************************/
typedef struct
{
	IMG_UINT32				ui32ZSBufferID; /*!< ZS buffer ID */
} RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING_DATA;

/*!
 ******************************************************************************
 * @Brief Command data of the \ref RGXFWIF_FWCCB_CMD_FREELIST_GROW Firmware CCB
 * command
 *****************************************************************************/
typedef struct
{
	IMG_UINT32				ui32FreelistID; /*!< Freelist ID */
} RGXFWIF_FWCCB_CMD_FREELIST_GS_DATA;

/*!
 ******************************************************************************
 * @Brief Command data of the \ref RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION
 * Firmware CCB command
 *****************************************************************************/
typedef struct
{
	IMG_UINT32			ui32FreelistsCount;                                     /*!< Freelists count */
	IMG_UINT32			ui32HwrCounter;                                         /*!< HWR counter */
	IMG_UINT32			aui32FreelistIDs[RGXFWIF_MAX_FREELISTS_TO_RECONSTRUCT]; /*!< Array of freelist IDs to reconstruct */
} RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION_DATA;

#define RGXFWIF_FWCCB_CMD_CONTEXT_RESET_FLAG_PF			(1U<<0)	/*!< 1 if a page fault happened */
#define RGXFWIF_FWCCB_CMD_CONTEXT_RESET_FLAG_ALL_CTXS	(1U<<1)	/*!< 1 if applicable to all contexts */

/*!
 ******************************************************************************
 * @Brief Command data of the \ref RGXFWIF_FWCCB_CMD_CONTEXT_RESET_NOTIFICATION
 * Firmware CCB command
 *****************************************************************************/
typedef struct
{
	IMG_UINT32						ui32ServerCommonContextID;	/*!< Context affected by the reset */
	RGX_CONTEXT_RESET_REASON		eResetReason;				/*!< Reason for reset */
	RGXFWIF_DM						eDM;						/*!< Data Master affected by the reset */
	IMG_UINT32						ui32ResetJobRef;			/*!< Job ref running at the time of reset */
	IMG_UINT32						ui32Flags;					/*!< RGXFWIF_FWCCB_CMD_CONTEXT_RESET_FLAG bitfield  */
	IMG_UINT64 RGXFW_ALIGN			ui64PCAddress;				/*!< At what page catalog address */
	IMG_DEV_VIRTADDR RGXFW_ALIGN	sFaultAddress;				/*!< Page fault address (only when applicable) */
} RGXFWIF_FWCCB_CMD_CONTEXT_RESET_DATA;

/*!
 ******************************************************************************
 * @Brief Command data of the \ref RGXFWIF_FWCCB_CMD_CONTEXT_FW_PF_NOTIFICATION
 * Firmware CCB command
 *****************************************************************************/
typedef struct
{
	IMG_DEV_VIRTADDR sFWFaultAddr;	/*!< Page fault address */
} RGXFWIF_FWCCB_CMD_FW_PAGEFAULT_DATA;

/*!
 ******************************************************************************
 * List of command types supported by the Firmware CCB
 *****************************************************************************/
typedef enum
{
	RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING              = 101U | RGX_CMD_MAGIC_DWORD_SHIFTED,   /*!< Requests ZSBuffer to be backed with physical pages
	                                                                                          \n Command data: RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING_DATA */
	RGXFWIF_FWCCB_CMD_ZSBUFFER_UNBACKING            = 102U | RGX_CMD_MAGIC_DWORD_SHIFTED,   /*!< Requests ZSBuffer to be unbacked
	                                                                                          \n Command data: RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING_DATA */
	RGXFWIF_FWCCB_CMD_FREELIST_GROW                 = 103U | RGX_CMD_MAGIC_DWORD_SHIFTED,   /*!< Requests an on-demand freelist grow
	                                                                                          \n Command data: RGXFWIF_FWCCB_CMD_FREELIST_GS_DATA */
	RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION      = 104U | RGX_CMD_MAGIC_DWORD_SHIFTED,   /*!< Requests freelists reconstruction
	                                                                                          \n Command data: RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION_DATA */
	RGXFWIF_FWCCB_CMD_CONTEXT_RESET_NOTIFICATION    = 105U | RGX_CMD_MAGIC_DWORD_SHIFTED,   /*!< Notifies host of a HWR event on a context
	                                                                                          \n Command data: RGXFWIF_FWCCB_CMD_CONTEXT_RESET_DATA */
	RGXFWIF_FWCCB_CMD_DEBUG_DUMP                    = 106U | RGX_CMD_MAGIC_DWORD_SHIFTED,   /*!< Requests an on-demand debug dump
	                                                                                          \n Command data: None */
	RGXFWIF_FWCCB_CMD_UPDATE_STATS                  = 107U | RGX_CMD_MAGIC_DWORD_SHIFTED,   /*!< Requests an on-demand update on process stats
	                                                                                          \n Command data: RGXFWIF_FWCCB_CMD_UPDATE_STATS_DATA */

	RGXFWIF_FWCCB_CMD_CORE_CLK_RATE_CHANGE          = 108U | RGX_CMD_MAGIC_DWORD_SHIFTED,
	RGXFWIF_FWCCB_CMD_REQUEST_GPU_RESTART           = 109U | RGX_CMD_MAGIC_DWORD_SHIFTED,   /*!< Requests GPU restart
	                                                                                          \n Command data: None */
#if defined(SUPPORT_VALIDATION)
	RGXFWIF_FWCCB_CMD_REG_READ                      = 110U | RGX_CMD_MAGIC_DWORD_SHIFTED,
#if defined(SUPPORT_SOC_TIMER)
	RGXFWIF_FWCCB_CMD_SAMPLE_TIMERS                 = 111U | RGX_CMD_MAGIC_DWORD_SHIFTED,
#endif
#endif
	RGXFWIF_FWCCB_CMD_CONTEXT_FW_PF_NOTIFICATION    = 112U | RGX_CMD_MAGIC_DWORD_SHIFTED,   /*!< Notifies host of a FW pagefault
	                                                                                          \n Command data: RGXFWIF_FWCCB_CMD_FW_PAGEFAULT_DATA */
} RGXFWIF_FWCCB_CMD_TYPE;

/*!
 ******************************************************************************
 * List of the various stats of the process to update/increment
 *****************************************************************************/
typedef enum
{
	RGXFWIF_FWCCB_CMD_UPDATE_NUM_PARTIAL_RENDERS=1,		/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32TotalNumPartialRenders stat */
	RGXFWIF_FWCCB_CMD_UPDATE_NUM_OUT_OF_MEMORY,			/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32TotalNumOutOfMemory stat */
	RGXFWIF_FWCCB_CMD_UPDATE_NUM_TA_STORES,				/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32NumTAStores stat */
	RGXFWIF_FWCCB_CMD_UPDATE_NUM_3D_STORES,				/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32Num3DStores stat */
	RGXFWIF_FWCCB_CMD_UPDATE_NUM_CDM_STORES,			/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32NumCDMStores stat */
	RGXFWIF_FWCCB_CMD_UPDATE_NUM_TDM_STORES				/*!< PVRSRVStatsUpdateRenderContextStats should increase the value of the ui32NumTDMStores stat */
} RGXFWIF_FWCCB_CMD_UPDATE_STATS_TYPE;

/*!
 ******************************************************************************
 * @Brief Command data of the \ref RGXFWIF_FWCCB_CMD_UPDATE_STATS Firmware CCB
 * command
 *****************************************************************************/
typedef struct
{
	RGXFWIF_FWCCB_CMD_UPDATE_STATS_TYPE		eElementToUpdate;			/*!< Element to update */
	IMG_PID									pidOwner;					/*!< The pid of the process whose stats are being updated */
	IMG_INT32								i32AdjustmentValue;			/*!< Adjustment to be made to the statistic */
} RGXFWIF_FWCCB_CMD_UPDATE_STATS_DATA;

typedef struct
{
	IMG_UINT32 ui32CoreClkRate;
} UNCACHED_ALIGN RGXFWIF_FWCCB_CMD_CORE_CLK_RATE_CHANGE_DATA;

#if defined(SUPPORT_VALIDATION)
typedef struct
{
	IMG_UINT64 ui64RegValue;
} RGXFWIF_FWCCB_CMD_RGXREG_READ_DATA;

#if defined(SUPPORT_SOC_TIMER)
typedef struct
{
	IMG_UINT64 ui64timerGray;
	IMG_UINT64 ui64timerBinary;
	IMG_UINT64 aui64uscTimers[RGX_FEATURE_NUM_CLUSTERS];
}  RGXFWIF_FWCCB_CMD_SAMPLE_TIMERS_DATA;
#endif
#endif

/*!
 ******************************************************************************
 * @Brief Firmware CCB command structure
 *****************************************************************************/
typedef struct
{
	RGXFWIF_FWCCB_CMD_TYPE  eCmdType;       /*!< Command type */
	IMG_UINT32              ui32FWCCBFlags; /*!< Compatibility and other flags */

	union
	{
		RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING_DATA				sCmdZSBufferBacking;			/*!< Data for Z/S-Buffer on-demand (un)backing*/
		RGXFWIF_FWCCB_CMD_FREELIST_GS_DATA					sCmdFreeListGS;					/*!< Data for on-demand freelist grow/shrink */
		RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION_DATA		sCmdFreeListsReconstruction;	/*!< Data for freelists reconstruction */
		RGXFWIF_FWCCB_CMD_CONTEXT_RESET_DATA				sCmdContextResetNotification;	/*!< Data for context reset notification */
		RGXFWIF_FWCCB_CMD_UPDATE_STATS_DATA					sCmdUpdateStatsData;			/*!< Data for updating process stats */
		RGXFWIF_FWCCB_CMD_CORE_CLK_RATE_CHANGE_DATA			sCmdCoreClkRateChange;
		RGXFWIF_FWCCB_CMD_FW_PAGEFAULT_DATA					sCmdFWPagefault;				/*!< Data for context reset notification */
#if defined(SUPPORT_VALIDATION) && !defined(SUPPORT_OPEN_SOURCE_DRIVER)
		RGXFWIF_FWCCB_CMD_RGXREG_READ_DATA					sCmdRgxRegReadData;
#if defined(SUPPORT_SOC_TIMER)
		RGXFWIF_FWCCB_CMD_SAMPLE_TIMERS_DATA				sCmdTimers;
#endif
#endif
	} RGXFW_ALIGN uCmdData;
} RGXFW_ALIGN RGXFWIF_FWCCB_CMD;

RGX_FW_STRUCT_SIZE_ASSERT(RGXFWIF_FWCCB_CMD);

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_FWCCB_CMD) == 48,
				"RGXFWIF_FWCCB_CMD is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*! @} End of FWCCBTypes */

/*!
 ******************************************************************************
 * Workload estimation Firmware CCB command structure for RGX
 *****************************************************************************/
typedef struct
{
	IMG_UINT16 ui16ReturnDataIndex; /*!< Index for return data array */
	IMG_UINT32 ui32CyclesTaken;     /*!< The cycles the workload took on the hardware */
} RGXFWIF_WORKEST_FWCCB_CMD;

/*!
 * @Defgroup ClientCCBTypes Client CCB data interface
 * @Brief Types grouping data structures and defines used in realising Client CCB commands/functionality
 * @{
 */

/* Required memory alignment for 64-bit variables accessible by Meta
  (The gcc meta aligns 64-bit variables to 64-bit; therefore, memory shared
   between the host and meta that contains 64-bit variables has to maintain
   this alignment) */
#define RGXFWIF_FWALLOC_ALIGN	sizeof(IMG_UINT64)

#define RGX_CCB_TYPE_TASK			(IMG_UINT32_C(1) << 15)
#define RGX_CCB_FWALLOC_ALIGN(size)	(PVR_ALIGN(size, RGXFWIF_FWALLOC_ALIGN))

typedef IMG_UINT32 RGXFWIF_CCB_CMD_TYPE;

/*!
 * @Name Client CCB command types
 * @{
 */
#define RGXFWIF_CCB_CMD_TYPE_GEOM			(201U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK) /*!< TA DM command */
#define RGXFWIF_CCB_CMD_TYPE_TQ_3D			(202U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK) /*!< 3D DM command for TQ operation */
#define RGXFWIF_CCB_CMD_TYPE_3D				(203U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK) /*!< 3D DM command */
#define RGXFWIF_CCB_CMD_TYPE_3D_PR			(204U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK) /*!< 3D DM command for Partial render */
#define RGXFWIF_CCB_CMD_TYPE_CDM			(205U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK) /*!< Compute DM command */
#define RGXFWIF_CCB_CMD_TYPE_TQ_TDM			(206U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK) /*!< TDM command */
#define RGXFWIF_CCB_CMD_TYPE_FBSC_INVALIDATE (207U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK)
#define RGXFWIF_CCB_CMD_TYPE_TQ_2D			(208U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK) /*!< 2D DM command for TQ operation */
#define RGXFWIF_CCB_CMD_TYPE_PRE_TIMESTAMP	(209U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK)
#define RGXFWIF_CCB_CMD_TYPE_NULL			(210U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK)
#define RGXFWIF_CCB_CMD_TYPE_ABORT			(211U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK)

/* Leave a gap between CCB specific commands and generic commands */
#define RGXFWIF_CCB_CMD_TYPE_FENCE          (212U | RGX_CMD_MAGIC_DWORD_SHIFTED) /*!< Fence dependencies of a command */
#define RGXFWIF_CCB_CMD_TYPE_UPDATE         (213U | RGX_CMD_MAGIC_DWORD_SHIFTED) /*!< Fence updates of a command */
#define RGXFWIF_CCB_CMD_TYPE_RMW_UPDATE     (214U | RGX_CMD_MAGIC_DWORD_SHIFTED) /*!< Fence updates related to workload resources */
#define RGXFWIF_CCB_CMD_TYPE_FENCE_PR       (215U | RGX_CMD_MAGIC_DWORD_SHIFTED) /*!< Fence dependencies of a PR command */
#define RGXFWIF_CCB_CMD_TYPE_PRIORITY       (216U | RGX_CMD_MAGIC_DWORD_SHIFTED) /*!< Context priority update command */
/* Pre and Post timestamp commands are supposed to sandwich the DM cmd. The
   padding code with the CCB wrap upsets the FW if we don't have the task type
   bit cleared for POST_TIMESTAMPs. That's why we have 2 different cmd types.
*/
#define RGXFWIF_CCB_CMD_TYPE_POST_TIMESTAMP (217U | RGX_CMD_MAGIC_DWORD_SHIFTED)
#define RGXFWIF_CCB_CMD_TYPE_UNFENCED_UPDATE (218U | RGX_CMD_MAGIC_DWORD_SHIFTED) /*!< Unfenced fence updates of a command */
#define RGXFWIF_CCB_CMD_TYPE_UNFENCED_RMW_UPDATE (219U | RGX_CMD_MAGIC_DWORD_SHIFTED) /*!< Unfenced fence updates related to workload resources */

#if defined(SUPPORT_VALIDATION)
#define RGXFWIF_CCB_CMD_TYPE_REG_READ (220U | RGX_CMD_MAGIC_DWORD_SHIFTED)
#endif

#define RGXFWIF_CCB_CMD_TYPE_PADDING	(221U | RGX_CMD_MAGIC_DWORD_SHIFTED) /*!< Skip without action type command */
#define RGXFWIF_CCB_CMD_TYPE_VK_TIMESTAMP	(223U | RGX_CMD_MAGIC_DWORD_SHIFTED | RGX_CCB_TYPE_TASK) /*!< Process a vulkan timestamp */
/*! @} End of Client CCB command types */

typedef struct
{
	/* Index for the KM Workload estimation return data array */
	IMG_UINT16 RGXFW_ALIGN         ui16ReturnDataIndex;
	/* Predicted time taken to do the work in cycles */
	IMG_UINT32 RGXFW_ALIGN         ui32CyclesPrediction;
	/* Deadline for the workload (in usecs) */
	IMG_UINT64 RGXFW_ALIGN         ui64Deadline;
} RGXFWIF_WORKEST_KICK_DATA;

/*! @Brief Command header of a command in the client CCB buffer.
 *
 *  Followed by this header is the command-data specific to the
 *  command-type as specified in the header.
 */
typedef struct
{
	RGXFWIF_CCB_CMD_TYPE					eCmdType;      /*!< Command data type following this command header */
	IMG_UINT32								ui32CmdSize;   /*!< Size of the command following this header */
	IMG_UINT32								ui32ExtJobRef; /*!< external job reference - provided by client and used in debug for tracking submitted work */
	IMG_UINT32								ui32IntJobRef; /*!< internal job reference - generated by services and used in debug for tracking submitted work */
#if defined(SUPPORT_WORKLOAD_ESTIMATION) && !defined(SUPPORT_OPEN_SOURCE_DRIVER)
	RGXFWIF_WORKEST_KICK_DATA RGXFW_ALIGN	sWorkEstKickData; /*!< Workload Estimation - Workload Estimation Data */
#endif
} RGXFWIF_CCB_CMD_HEADER;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_CCB_CMD_HEADER) == 16,
				"RGXFWIF_CCB_CMD_HEADER is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*
 ******************************************************************************
 * Client CCB commands which are only required by the kernel
 *****************************************************************************/

/*! @Brief Command data for \ref RGXFWIF_CCB_CMD_TYPE_PRIORITY type client CCB command */
typedef struct
{
	IMG_INT32              i32Priority; /*!< Priority level */
} RGXFWIF_CMD_PRIORITY;

/*! @} End of ClientCCBTypes */

/*!
 ******************************************************************************
 * Signature and Checksums Buffer
 *****************************************************************************/
typedef struct
{
	PRGXFWIF_SIGBUFFER		sBuffer;			/*!< Ptr to Signature Buffer memory */
	IMG_UINT32				ui32LeftSizeInRegs;	/*!< Amount of space left for storing regs in the buffer */
} UNCACHED_ALIGN RGXFWIF_SIGBUF_CTL;

typedef struct
{
	PRGXFWIF_COUNTERBUFFER	sBuffer;			/*!< Ptr to counter dump buffer */
	IMG_UINT32				ui32SizeInDwords;	/*!< Amount of space for storing in the buffer */
} UNCACHED_ALIGN RGXFWIF_COUNTER_DUMP_CTL;

typedef struct
{
	PRGXFWIF_FIRMWAREGCOVBUFFER	sBuffer;		/*!< Ptr to firmware gcov buffer */
	IMG_UINT32					ui32Size;		/*!< Amount of space for storing in the buffer */
} UNCACHED_ALIGN RGXFWIF_FIRMWARE_GCOV_CTL;

/*!
 *****************************************************************************
 * RGX Compatibility checks
 *****************************************************************************/

/* WARNING: Whenever the layout of RGXFWIF_COMPCHECKS_BVNC changes, the
	following define should be increased by 1 to indicate to the
	compatibility logic that layout has changed. */
#define RGXFWIF_COMPCHECKS_LAYOUT_VERSION 3

typedef struct
{
	IMG_UINT32	ui32LayoutVersion; /* WARNING: This field must be defined as first one in this structure */
	IMG_UINT64	RGXFW_ALIGN ui64BVNC;
} UNCACHED_ALIGN RGXFWIF_COMPCHECKS_BVNC;

typedef struct
{
	IMG_UINT8	ui8OsCountSupport;
} UNCACHED_ALIGN RGXFWIF_INIT_OPTIONS;

#define RGXFWIF_COMPCHECKS_BVNC_DECLARE_AND_INIT(name) \
	RGXFWIF_COMPCHECKS_BVNC (name) = { \
		RGXFWIF_COMPCHECKS_LAYOUT_VERSION, \
		0, \
	}
#define RGXFWIF_COMPCHECKS_BVNC_INIT(name) \
	do { \
		(name).ui32LayoutVersion = RGXFWIF_COMPCHECKS_LAYOUT_VERSION; \
		(name).ui64BVNC = 0; \
	} while (false)

typedef struct
{
	RGXFWIF_COMPCHECKS_BVNC		sHWBVNC;				/*!< hardware BVNC (from the RGX registers) */
	RGXFWIF_COMPCHECKS_BVNC		sFWBVNC;				/*!< firmware BVNC */
	IMG_UINT32					ui32FWProcessorVersion;	/*!< identifier of the FW processor version */
	IMG_UINT32					ui32DDKVersion;			/*!< software DDK version */
	IMG_UINT32					ui32DDKBuild;			/*!< software DDK build no. */
	IMG_UINT32					ui32BuildOptions;		/*!< build options bit-field */
	RGXFWIF_INIT_OPTIONS		sInitOptions;			/*!< initialisation options bit-field */
	IMG_BOOL					bUpdated;				/*!< Information is valid */
} UNCACHED_ALIGN RGXFWIF_COMPCHECKS;

/*! @Brief Firmware Runtime configuration data \ref RGXFWIF_RUNTIME_CFG
 * allocated by services and used by the Firmware on boot
 **/
typedef struct
{
	IMG_UINT32         ui32ActivePMLatencyms;                    /*!< APM latency in ms before signalling IDLE to the host */
	IMG_UINT32         ui32RuntimeCfgFlags;                      /*!< Compatibility and other flags */
	IMG_BOOL           bActivePMLatencyPersistant;               /*!< If set, APM latency does not reset to system default each GPU power transition */
	IMG_UINT32         ui32CoreClockSpeed;                       /*!< Core clock speed, currently only used to calculate timer ticks */
	IMG_UINT32         ui32DefaultDustsNumInit;                  /*!< Last number of dusts change requested by the host */
	IMG_UINT32         ui32PHRMode;                              /*!< Periodic Hardware Reset configuration values */
	IMG_UINT32         ui32HCSDeadlineMS;                        /*!< New number of milliseconds C/S is allowed to last */
	IMG_UINT32         ui32WdgPeriodUs;                          /*!< The watchdog period in microseconds */
	IMG_UINT32         aui32DriverPriority[RGXFW_MAX_NUM_OSIDS]; /*!< Array of priorities per OS */
	IMG_UINT32         aui32DriverIsolationGroup[RGXFW_MAX_NUM_OSIDS]; /*!< Array of isolation groups per OS */

	PRGXFWIF_HWPERFBUF sHWPerfBuf;                               /*!< On-demand allocated HWPerf buffer address, to be passed to the FW */
#if defined(SUPPORT_VALIDATION) && !defined(SUPPORT_OPEN_SOURCE_DRIVER)
	IMG_BOOL           bInjectFWFault;                           /*!< Injecting firmware fault to validate recovery through Host */
#endif
} RGXFWIF_RUNTIME_CFG;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_RUNTIME_CFG) == 68,
				"RGXFWIF_RUNTIME_CFG is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*!
 *****************************************************************************
 * Control data for RGX
 *****************************************************************************/

#define RGXFWIF_HWR_DEBUG_DUMP_ALL (99999U)

#if defined(PDUMP)

#define RGXFWIF_PID_FILTER_MAX_NUM_PIDS 32U

typedef enum
{
	RGXFW_PID_FILTER_INCLUDE_ALL_EXCEPT,
	RGXFW_PID_FILTER_EXCLUDE_ALL_EXCEPT
} RGXFWIF_PID_FILTER_MODE;

typedef struct
{
	IMG_PID uiPID;
	IMG_UINT32 ui32DriverID;
} RGXFW_ALIGN RGXFWIF_PID_FILTER_ITEM;

typedef struct
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

#if defined(SUPPORT_SECURITY_VALIDATION)
#define RGXFWIF_SECURE_ACCESS_TEST_READ_WRITE_FW_DATA  (0x1U << 0)
#define RGXFWIF_SECURE_ACCESS_TEST_READ_WRITE_FW_CODE  (0x1U << 1)
#define RGXFWIF_SECURE_ACCESS_TEST_RUN_FROM_NONSECURE  (0x1U << 2)
#define RGXFWIF_SECURE_ACCESS_TEST_RUN_FROM_SECURE     (0x1U << 3)
#endif

typedef enum
{
	RGXFWIF_TPU_DM_PDM = 0,
	RGXFWIF_TPU_DM_VDM = 1,
	RGXFWIF_TPU_DM_CDM = 2,
	RGXFWIF_TPU_DM_TDM = 3,
	RGXFWIF_TPU_DM_LAST
} RGXFWIF_TPU_DM;

typedef enum
{
	RGXFWIF_GPIO_VAL_OFF           = 0, /*!< No GPIO validation */
	RGXFWIF_GPIO_VAL_GENERAL       = 1, /*!< Simple test case that
	                                         initiates by sending data via the
	                                         GPIO and then sends back any data
	                                         received over the GPIO */
	RGXFWIF_GPIO_VAL_AP            = 2, /*!< More complex test case that writes
	                                         and reads data across the entire
	                                         GPIO AP address range.*/
#if defined(SUPPORT_STRIP_RENDERING)
	RGXFWIF_GPIO_VAL_SR_BASIC      = 3, /*!< Strip Rendering AP based basic test.*/
	RGXFWIF_GPIO_VAL_SR_COMPLEX    = 4, /*!< Strip Rendering AP based complex test.*/
#endif
	RGXFWIF_GPIO_VAL_TESTBENCH     = 5, /*!< Validates the GPIO Testbench. */
	RGXFWIF_GPIO_VAL_LOOPBACK      = 6, /*!< Send and then receive each byte
	                                         in the range 0-255. */
	RGXFWIF_GPIO_VAL_LOOPBACK_LITE = 7, /*!< Send and then receive each power-of-2
	                                         byte in the range 0-255. */
	RGXFWIF_GPIO_VAL_LAST
} RGXFWIF_GPIO_VAL_MODE;

typedef IMG_UINT32 FW_PERF_CONF;
#define FW_PERF_CONF_NONE			0U
#define FW_PERF_CONF_ICACHE			1U
#define FW_PERF_CONF_DCACHE			2U
#define FW_PERF_CONF_JTLB_INSTR		5U
#define FW_PERF_CONF_INSTRUCTIONS	6U

typedef enum
{
	FW_BOOT_STAGE_TLB_INIT_FAILURE = -2,
	FW_BOOT_STAGE_NOT_AVAILABLE = -1,
	FW_BOOT_NOT_STARTED = 0,
	FW_BOOT_BLDR_STARTED = 1,
	FW_BOOT_CACHE_DONE,
	FW_BOOT_TLB_DONE,
	FW_BOOT_MAIN_STARTED,
	FW_BOOT_ALIGNCHECKS_DONE,
	FW_BOOT_INIT_DONE,
} FW_BOOT_STAGE;

/*!
 * @AddToGroup KCCBTypes
 * @{
 * @Name Kernel CCB return slot responses
 * @{
 * Usage of bit-fields instead of bare integers
 * allows FW to possibly pack-in several responses for each single kCCB command.
 */

#define RGXFWIF_KCCB_RTN_SLOT_CMD_EXECUTED   (1U << 0) /*!< Command executed (return status from FW) */
#define RGXFWIF_KCCB_RTN_SLOT_CLEANUP_BUSY   (1U << 1) /*!< A cleanup was requested but resource busy */
#define RGXFWIF_KCCB_RTN_SLOT_POLL_FAILURE   (1U << 2) /*!< Poll failed in FW for a HW operation to complete */

#define RGXFWIF_KCCB_RTN_SLOT_NO_RESPONSE            0x0U      /*!< Reset value of a kCCB return slot (set by host) */
/*!
 * @} End of Name Kernel CCB return slot responses
 * @} End of AddToGroup KCCBTypes
 */

/*! @Brief OS connection data \ref RGXFWIF_CONNECTION_CTL allocated
 * by services and used to track OS state in Firmware and Services
 **/
typedef struct
{
	/* Fw-Os connection states */
	volatile RGXFWIF_CONNECTION_FW_STATE eConnectionFwState;    /*!< Firmware-OS connection state */
	volatile RGXFWIF_CONNECTION_OS_STATE eConnectionOsState;    /*!< Services-OS connection state */
	volatile IMG_UINT32                  ui32AliveFwToken;      /*!< OS Alive token updated by Firmware */
	volatile IMG_UINT32                  ui32AliveOsToken;      /*!< OS Alive token updated by Services */
} UNCACHED_ALIGN RGXFWIF_CONNECTION_CTL;

/*! @Brief Firmware OS Initialization data \ref RGXFWIF_OSINIT
 * allocated by services and used by the Firmware on boot
 **/
typedef struct
{
	/* Kernel CCB */
	PRGXFWIF_CCB_CTL        psKernelCCBCtl; /*!< Kernel CCB Control */
	PRGXFWIF_CCB            psKernelCCB; /*!<  Kernel CCB */
	PRGXFWIF_CCB_RTN_SLOTS  psKernelCCBRtnSlots; /*!<  Kernel CCB return slots */

	/* Firmware CCB */
	PRGXFWIF_CCB_CTL        psFirmwareCCBCtl; /*!<  Firmware CCB control */
	PRGXFWIF_CCB            psFirmwareCCB; /*!<  Firmware CCB */

	/* Workload Estimation Firmware CCB */
	PRGXFWIF_CCB_CTL        psWorkEstFirmwareCCBCtl; /*!<  Workload estimation control */
	PRGXFWIF_CCB            psWorkEstFirmwareCCB; /*!<  Workload estimation buffer */

	PRGXFWIF_HWRINFOBUF     sRGXFWIfHWRInfoBufCtl; /*!<  HWRecoveryInfo control */

	IMG_UINT32              ui32HWRDebugDumpLimit; /*!< Firmware debug dump maximum limit */

	PRGXFWIF_OSDATA         sFwOsData; /*!<  Firmware per-os shared data */

	RGXFWIF_COMPCHECKS      sRGXCompChecks; /*!< Compatibility checks to be populated by the Firmware */

} UNCACHED_ALIGN RGXFWIF_OSINIT;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_OSINIT) == 104,
				"RGXFWIF_OSINIT is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*! @Brief Firmware System Initialization data \ref RGXFWIF_SYSINIT
 * allocated by services and used by the Firmware on boot
 **/
typedef struct
{
	RGX_MIPS_STATE          sMIPSState; /*!< MIPS Debug Data; this must be the first member in the structure */

	IMG_DEV_PHYADDR         RGXFW_ALIGN sFaultPhysAddr; /*!< Fault read address */

	IMG_DEV_VIRTADDR        RGXFW_ALIGN sPDSExecBase; /*!< PDS execution base */
	IMG_DEV_VIRTADDR        RGXFW_ALIGN sUSCExecBase; /*!< USC execution base */
	IMG_DEV_VIRTADDR        RGXFW_ALIGN sFBCDCStateTableBase; /*!< FBCDC bindless texture state table base */
	IMG_DEV_VIRTADDR        RGXFW_ALIGN sFBCDCLargeStateTableBase;
	IMG_DEV_VIRTADDR        RGXFW_ALIGN sTextureHeapBase; /*!< Texture state base */

	IMG_UINT64              RGXFW_ALIGN ui64HWPerfFilter; /*! Event filter for Firmware events */

	IMG_DEV_VIRTADDR        RGXFW_ALIGN sSLC3FenceDevVAddr;

	IMG_UINT32              RGXFW_ALIGN aui32TPUTrilinearFracMask[RGXFWIF_TPU_DM_LAST];

	RGXFWIF_SIGBUF_CTL      asSigBufCtl[RGXFWIF_DM_MAX]; /*!< Signature and Checksum Buffers for DMs */

	RGXFWIF_PDVFS_OPP       sPDVFSOPPInfo;

	RGXFWIF_DMA_ADDR        sCorememDataStore; /*!< Firmware coremem data */

	RGXFWIF_COUNTER_DUMP_CTL sCounterDumpCtl;

#if defined(SUPPORT_FIRMWARE_GCOV)
	RGXFWIF_FIRMWARE_GCOV_CTL sFirmwareGcovCtl; /*!< Firmware gcov buffer control */
#endif

	IMG_UINT32              ui32FilterFlags;

	PRGXFWIF_RUNTIME_CFG    sRuntimeCfg; /*!<  Firmware Runtime configuration */

	PRGXFWIF_TRACEBUF       sTraceBufCtl; /*!<  Firmware Trace buffer control */
	PRGXFWIF_SYSDATA        sFwSysData; /*!< Firmware System shared data */
#if defined(SUPPORT_TBI_INTERFACE)
	PRGXFWIF_TBIBUF         sTBIBuf; /*!< Tbi log buffer */
#endif

	PRGXFWIF_GPU_UTIL_FWCB  sGpuUtilFWCbCtl; /*!< GPU utilization buffer */
	PRGXFWIF_REG_CFG        sRegCfg; /*!< Firmware register user configuration */
	PRGXFWIF_HWPERF_CTL     sHWPerfCtl; /*!< HWPerf counter block configuration.*/

	RGXFWIF_DEV_VIRTADDR    sAlignChecks; /*!< Array holding Server structures alignment data */

	IMG_UINT32              ui32InitialCoreClockSpeed; /*!< Core clock speed at FW boot time */

	IMG_UINT32              ui32InitialActivePMLatencyms; /*!< APM latency in ms before signalling IDLE to the host */

	IMG_BOOL                bFirmwareStarted; /*!< Flag to be set by the Firmware after successful start */

	IMG_UINT32              ui32MarkerVal; /*!< Host/FW Trace synchronisation Partition Marker */

	IMG_UINT32              ui32FirmwareStartedTimeStamp; /*!< Firmware initialization complete time */

	IMG_UINT32              ui32JonesDisableMask;

	FW_PERF_CONF            eFirmwarePerf; /*!< Firmware performance counter config */

	/**
	 * FW Pointer to memory containing core clock rate in Hz.
	 * Firmware (PDVFS) updates the memory when running on non primary FW thread
	 * to communicate to host driver.
	 */
	PRGXFWIF_CORE_CLK_RATE  sCoreClockRate;

#if defined(PDUMP)
	RGXFWIF_PID_FILTER      sPIDFilter;
#endif

	RGXFWIF_GPIO_VAL_MODE   eGPIOValidationMode;

	RGX_HWPERF_BVNC         sBvncKmFeatureFlags; /*!< Used in HWPerf for decoding BVNC Features*/

#if defined(SUPPORT_SECURITY_VALIDATION)
	IMG_UINT32              ui32SecurityTestFlags;
	RGXFWIF_DEV_VIRTADDR    pbSecureBuffer;
	RGXFWIF_DEV_VIRTADDR    pbNonSecureBuffer;
#endif

#if defined(SUPPORT_FW_HOST_SIDE_RECOVERY)
	RGXFWIF_DEV_VIRTADDR    sActiveContextBufBase; /*!< Active context buffer base */
#endif

#if defined(SUPPORT_GPUVIRT_VALIDATION_MTS)
	/*
	 * Used when validation is enabled to allow the host to check
	 * that MTS sent the correct sideband in response to a kick
	 * from a given OSes schedule register.
	 * Testing is enabled if RGXFWIF_KICK_TEST_ENABLED_BIT is set
	 *
	 * Set by the host to:
	 * (osid << RGXFWIF_KICK_TEST_OSID_SHIFT) | RGXFWIF_KICK_TEST_ENABLED_BIT
	 * reset to 0 by FW when kicked by the given OSid
	 */
	IMG_UINT32              ui32OSKickTest;
#endif

	/* Value to write into RGX_CR_TFBC_COMPRESSION_CONTROL */
	IMG_UINT32              ui32TFBCCompressionControl;

#if defined(SUPPORT_AUTOVZ)
	IMG_UINT32              ui32VzWdgPeriod;
#endif
#if defined(SUPPORT_FW_HOST_SIDE_RECOVERY)
	/* notify firmware power-up on host-side recovery */
	IMG_BOOL                bFwHostRecoveryMode;
#endif
} UNCACHED_ALIGN RGXFWIF_SYSINIT;

static_assert(offsetof(RGXFWIF_SYSINIT, sMIPSState) == 0,
				"sMIPSState is not the first member of the RGXFWIF_SYSINIT struct");

#if defined(SUPPORT_GPUVIRT_VALIDATION_MTS)
#define RGXFWIF_KICK_TEST_ENABLED_BIT  0x1
#define RGXFWIF_KICK_TEST_OSID_SHIFT   0x1
#endif

/*!
 *****************************************************************************
 * Timer correlation shared data and defines
 *****************************************************************************/

typedef struct
{
	IMG_UINT64 RGXFW_ALIGN ui64OSTimeStamp;
	IMG_UINT64 RGXFW_ALIGN ui64OSMonoTimeStamp;
	IMG_UINT64 RGXFW_ALIGN ui64CRTimeStamp;

	/* Utility variable used to convert CR timer deltas to OS timer deltas (nS),
	 * where the deltas are relative to the timestamps above:
	 * deltaOS = (deltaCR * K) >> decimal_shift, see full explanation below */
	IMG_UINT64 RGXFW_ALIGN ui64CRDeltaToOSDeltaKNs;

	IMG_UINT32             ui32CoreClockSpeed;
	IMG_UINT32             ui32Reserved;
} UNCACHED_ALIGN RGXFWIF_TIME_CORR;


/* The following macros are used to help converting FW timestamps to the Host
 * time domain. On the FW the RGX_CR_TIMER counter is used to keep track of
 * time; it increments by 1 every 256 GPU clock ticks, so the general
 * formula to perform the conversion is:
 *
 * [ GPU clock speed in Hz, if (scale == 10^9) then deltaOS is in nS,
 *   otherwise if (scale == 10^6) then deltaOS is in uS ]
 *
 *             deltaCR * 256                                   256 * scale
 *  deltaOS = --------------- * scale = deltaCR * K    [ K = --------------- ]
 *             GPUclockspeed                                  GPUclockspeed
 *
 * The actual K is multiplied by 2^20 (and deltaCR * K is divided by 2^20)
 * to get some better accuracy and to avoid returning 0 in the integer
 * division 256000000/GPUfreq if GPUfreq is greater than 256MHz.
 * This is the same as keeping K as a decimal number.
 *
 * The maximum deltaOS is slightly more than 5hrs for all GPU frequencies
 * (deltaCR * K is more or less a constant), and it's relative to the base
 * OS timestamp sampled as a part of the timer correlation data.
 * This base is refreshed on GPU power-on, DVFS transition and periodic
 * frequency calibration (executed every few seconds if the FW is doing
 * some work), so as long as the GPU is doing something and one of these
 * events is triggered then deltaCR * K will not overflow and deltaOS will be
 * correct.
 */

#define RGXFWIF_CRDELTA_TO_OSDELTA_ACCURACY_SHIFT  (20)

#define RGXFWIF_GET_DELTA_OSTIME_NS(deltaCR, K) \
	(((deltaCR) * (K)) >> RGXFWIF_CRDELTA_TO_OSDELTA_ACCURACY_SHIFT)


/*!
 ******************************************************************************
 * GPU Utilisation
 *****************************************************************************/

/* See rgx_common.h for a list of GPU states */
#define RGXFWIF_GPU_UTIL_TIME_MASK       (IMG_UINT64_C(0xFFFFFFFFFFFFFFFF) & ~RGXFWIF_GPU_UTIL_STATE_MASK)

#define RGXFWIF_GPU_UTIL_GET_TIME(word)  ((word) & RGXFWIF_GPU_UTIL_TIME_MASK)
#define RGXFWIF_GPU_UTIL_GET_STATE(word) ((word) & RGXFWIF_GPU_UTIL_STATE_MASK)

/* The OS timestamps computed by the FW are approximations of the real time,
 * which means they could be slightly behind or ahead the real timer on the Host.
 * In some cases we can perform subtractions between FW approximated
 * timestamps and real OS timestamps, so we need a form of protection against
 * negative results if for instance the FW one is a bit ahead of time.
 */
#define RGXFWIF_GPU_UTIL_GET_PERIOD(newtime,oldtime) \
	(((newtime) > (oldtime)) ? ((newtime) - (oldtime)) : 0U)

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
#define RGXFWIF_TIME_CORR_ARRAY_SIZE            256U
#define RGXFWIF_TIME_CORR_CURR_INDEX(seqcount)  ((seqcount) % RGXFWIF_TIME_CORR_ARRAY_SIZE)

/* Make sure the timer correlation array size is a power of 2 */
static_assert((RGXFWIF_TIME_CORR_ARRAY_SIZE & (RGXFWIF_TIME_CORR_ARRAY_SIZE - 1U)) == 0U,
			  "RGXFWIF_TIME_CORR_ARRAY_SIZE must be a power of two");

typedef struct
{
	RGXFWIF_TIME_CORR      sTimeCorr[RGXFWIF_TIME_CORR_ARRAY_SIZE];
	IMG_UINT32             ui32TimeCorrSeqCount;

	/* Compatibility and other flags */
	IMG_UINT32             ui32GpuUtilFlags;

	/* Last GPU state + OS time of the last state update */
	IMG_UINT64 RGXFW_ALIGN ui64GpuLastWord;
	/* Counters for the amount of time the GPU was active/idle/blocked */
	IMG_UINT64 RGXFW_ALIGN aui64GpuStatsCounters[RGXFWIF_GPU_UTIL_STATE_NUM];

	/* Last GPU DM per-OS states + OS time of the last state update */
	IMG_UINT64 RGXFW_ALIGN aaui64DMOSLastWord[RGXFWIF_DM_MAX][RGXFW_MAX_NUM_OSIDS];
	/* Counters for the amount of time the GPU DMs were active/idle/blocked */
	IMG_UINT64 RGXFW_ALIGN aaaui64DMOSStatsCounters[RGXFWIF_DM_MAX][RGXFW_MAX_NUM_OSIDS][RGXFWIF_GPU_UTIL_STATE_NUM];
} UNCACHED_ALIGN RGXFWIF_GPU_UTIL_FWCB;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_GPU_UTIL_FWCB) == 12584,
				"RGXFWIF_GPU_UTIL_FWCB is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

typedef struct
{
	IMG_UINT32           ui32RenderTargetIndex;		//Render number
	IMG_UINT32           ui32CurrentRenderTarget;	//index in RTA
	IMG_UINT32           ui32ActiveRenderTargets;	//total active RTs
	IMG_UINT32           ui32CumulActiveRenderTargets;   //total active RTs from the first TA kick, for OOM
	RGXFWIF_DEV_VIRTADDR sValidRenderTargets;  //Array of valid RT indices
	RGXFWIF_DEV_VIRTADDR sRTANumPartialRenders;  //Array of number of occurred partial renders per render target
	IMG_UINT32           ui32MaxRTs;   //Number of render targets in the array
	IMG_UINT32           ui32RTACtlFlags; /* Compatibility and other flags */
} UNCACHED_ALIGN RGXFWIF_RTA_CTL;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_RTA_CTL) == 32,
				"RGXFWIF_RTA_CTL is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*!
 * @InGroup RenderTarget
 * @Brief Firmware Freelist holding usage state of the Parameter Buffers
 */
typedef struct
{
	IMG_DEV_VIRTADDR	RGXFW_ALIGN psFreeListDevVAddr;	/*!< Freelist page table base */
	IMG_UINT64		RGXFW_ALIGN ui64CurrentDevVAddr;/*!< Freelist page table entry for current free page  */
	IMG_UINT32		ui32CurrentStackTop;		/*!< Freelist current free page  */
	IMG_UINT32		ui32MaxPages;			/*!< Max no. of pages can be added to the freelist */
	IMG_UINT32		ui32GrowPages;			/*!< No pages to add in each freelist grow */
	IMG_UINT32		ui32CurrentPages;		/*!< Total no. of pages made available to the PM HW */
	IMG_UINT32		ui32AllocatedPageCount;		/*!< No. of pages allocated by PM HW */
	IMG_UINT32		ui32AllocatedMMUPageCount;	/*!< No. of pages allocated for GPU MMU for PM*/
#if defined(SUPPORT_SHADOW_FREELISTS) && !defined(SUPPORT_OPEN_SOURCE_DRIVER)
	IMG_UINT32		ui32HWRCounter;
	PRGXFWIF_FWMEMCONTEXT	psFWMemContext;
#endif
	IMG_UINT32		ui32FreeListID;			/*!< Unique Freelist ID */
	IMG_BOOL		bGrowPending;			/*!< Freelist grow is pending */
	IMG_UINT32		ui32ReadyPages;			/*!< Reserved pages to be used only on PM OOM event */
	IMG_UINT32		ui32FreelistFlags;		/*!< Compatibility and other flags */
#if defined(SUPPORT_AGP) || defined(SUPPORT_OPEN_SOURCE_DRIVER)
	IMG_UINT32		ui32PmGlobalPb;			/*!< PM Global PB on which Freelist is loaded */
#endif
} UNCACHED_ALIGN RGXFWIF_FREELIST;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_FREELIST) == 64,
				"RGXFWIF_FREELIST is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*!
 ******************************************************************************
 * HWRTData
 *****************************************************************************/

/* HWRTData flags */
/* Deprecated flags 1:0 */
#define HWRTDATA_HAS_LAST_TA              (IMG_UINT32_C(1) << 2)
#define HWRTDATA_PARTIAL_RENDERED         (IMG_UINT32_C(1) << 3)
#define HWRTDATA_DISABLE_TILE_REORDERING  (IMG_UINT32_C(1) << 4)
#define HWRTDATA_NEED_BRN65101_BLIT       (IMG_UINT32_C(1) << 5)
#define HWRTDATA_FIRST_BRN65101_STRIP     (IMG_UINT32_C(1) << 6)
#define HWRTDATA_NEED_BRN67182_2ND_RENDER (IMG_UINT32_C(1) << 7)
#if defined(SUPPORT_AGP)
#define HWRTDATA_GLOBAL_PB_NUMBER_BIT0    (IMG_UINT32_C(1) << 8)
#if defined(SUPPORT_AGP4)
#define HWRTDATA_GLOBAL_PB_NUMBER_BIT1    (IMG_UINT32_C(1) << 9)
#endif
#define HWRTDATA_GEOM_NEEDS_RESUME        (IMG_UINT32_C(1) << 10)
#endif

typedef enum
{
	RGXFWIF_RTDATA_STATE_NONE = 0,
	RGXFWIF_RTDATA_STATE_KICKTA,
	RGXFWIF_RTDATA_STATE_KICKTAFIRST,
	RGXFWIF_RTDATA_STATE_TAFINISHED,
	RGXFWIF_RTDATA_STATE_KICK3D,
	RGXFWIF_RTDATA_STATE_3DFINISHED,
	RGXFWIF_RTDATA_STATE_3DCONTEXTSTORED,
	RGXFWIF_RTDATA_STATE_TAOUTOFMEM,
	RGXFWIF_RTDATA_STATE_PARTIALRENDERFINISHED,
	/* In case of HWR, we can't set the RTDATA state to NONE,
	 * as this will cause any TA to become a first TA.
	 * To ensure all related TA's are skipped, we use the HWR state */
	RGXFWIF_RTDATA_STATE_HWR,
	RGXFWIF_RTDATA_STATE_UNKNOWN = 0x7FFFFFFFU
} RGXFWIF_RTDATA_STATE;

typedef struct
{
	IMG_BOOL							bTACachesNeedZeroing;

	IMG_UINT32							ui32ScreenPixelMax;
	IMG_UINT64							RGXFW_ALIGN ui64MultiSampleCtl;
	IMG_UINT64							ui64FlippedMultiSampleCtl;
	IMG_UINT32							ui32TPCStride;
	IMG_UINT32							ui32TPCSize;
	IMG_UINT32							ui32TEScreen;
	IMG_UINT32							ui32MTileStride;
	IMG_UINT32							ui32TEAA;
	IMG_UINT32							ui32TEMTILE1;
	IMG_UINT32							ui32TEMTILE2;
	IMG_UINT32							ui32ISPMergeLowerX;
	IMG_UINT32							ui32ISPMergeLowerY;
	IMG_UINT32							ui32ISPMergeUpperX;
	IMG_UINT32							ui32ISPMergeUpperY;
	IMG_UINT32							ui32ISPMergeScaleX;
	IMG_UINT32							ui32ISPMergeScaleY;
	IMG_UINT32							uiRgnHeaderSize;
	IMG_UINT32							ui32ISPMtileSize;
} UNCACHED_ALIGN RGXFWIF_HWRTDATA_COMMON;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_HWRTDATA_COMMON) == 88,
				"RGXFWIF_HWRTDATA_COMMON is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
#define MAX_FREELISTS_SIZE 3

static_assert(RGXFW_MAX_FREELISTS <= MAX_FREELISTS_SIZE,
				"RGXFW_MAX_FREELISTS is outside of allowable range for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/*!
 * @InGroup RenderTarget
 * @Brief Firmware Render Target data i.e. HWRTDATA used to hold the PM context
 */
typedef struct
{
	IMG_DEV_VIRTADDR		RGXFW_ALIGN psPMMListDevVAddr;			/*!< MList Data Store */

	IMG_UINT64			RGXFW_ALIGN ui64VCECatBase[1];			/*!< VCE Page Catalogue base */
	IMG_UINT64			RGXFW_ALIGN ui64VCELastCatBase[1];
	IMG_UINT64			RGXFW_ALIGN ui64TECatBase[1];			/*!< TE Page Catalogue base */
	IMG_UINT64			RGXFW_ALIGN ui64TELastCatBase[1];
	IMG_UINT64			RGXFW_ALIGN ui64AlistCatBase;			/*!< Alist Page Catalogue base */
	IMG_UINT64			RGXFW_ALIGN ui64AlistLastCatBase;

	IMG_UINT64			RGXFW_ALIGN ui64PMAListStackPointer;		/*!< Freelist page table entry for current Mlist page  */
	IMG_UINT32			ui32PMMListStackPointer;			/*!< Current Mlist page */

	RGXFWIF_DEV_VIRTADDR		sHWRTDataCommonFwAddr;				/*!< Render target dimension dependent data */

	IMG_UINT32			ui32HWRTDataFlags;
	RGXFWIF_RTDATA_STATE		eState;						/*!< Current workload processing state of HWRTDATA */

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
	PRGXFWIF_FREELIST		RGXFW_ALIGN apsFreeLists[MAX_FREELISTS_SIZE];	/*!< Freelist to use */
	IMG_UINT32			aui32FreeListHWRSnapshot[MAX_FREELISTS_SIZE];
#else
	PRGXFWIF_FREELIST		RGXFW_ALIGN apsFreeLists[RGXFW_MAX_FREELISTS];	/*!< Freelist to use */
	IMG_UINT32			aui32FreeListHWRSnapshot[RGXFW_MAX_FREELISTS];
#endif

	IMG_DEV_VIRTADDR		RGXFW_ALIGN psVHeapTableDevVAddr;		/*!< VHeap table base */

	RGXFWIF_RTA_CTL			sRTACtl;					/*!< Render target array data */

	IMG_DEV_VIRTADDR		RGXFW_ALIGN sTailPtrsDevVAddr;			/*!< Tail pointers base */
	IMG_DEV_VIRTADDR		RGXFW_ALIGN sMacrotileArrayDevVAddr;		/*!< Macrotiling array base */
	IMG_DEV_VIRTADDR		RGXFW_ALIGN sRgnHeaderDevVAddr;			/*!< Region headers base */
	IMG_DEV_VIRTADDR		RGXFW_ALIGN sRTCDevVAddr;			/*!< Render target cache base */
#if defined(RGX_FIRMWARE)
	struct RGXFWIF_FWCOMMONCONTEXT_* RGXFW_ALIGN psOwnerGeom;
#else
	RGXFWIF_DEV_VIRTADDR		RGXFW_ALIGN pui32OwnerGeomNotUsedByHost;
#endif
#if defined(SUPPORT_TRP) && !defined(SUPPORT_OPEN_SOURCE_DRIVER)
	IMG_UINT32			ui32KickFlagsCopy;
	IMG_UINT32			ui32TRPState;		/*!< Used by Firmware to track current state of a protected kick */
	IMG_UINT32			ui32TEPageCopy;
	IMG_UINT32			ui32VCEPageCopy;
#endif
#if defined(SUPPORT_AGP) || defined(SUPPORT_OPEN_SOURCE_DRIVER)
	IMG_BOOL			bTACachesNeedZeroing;
#endif

	RGXFWIF_CLEANUP_CTL		RGXFW_ALIGN_DCACHEL sCleanupState;					/*!< Render target clean up state */
} RGXFW_ALIGN_DCACHEL RGXFWIF_HWRTDATA;

#if defined(SUPPORT_OPEN_SOURCE_DRIVER)
static_assert(sizeof(RGXFWIF_HWRTDATA) == 256,
				"RGXFWIF_HWRTDATA is incorrect size for SUPPORT_OPEN_SOURCE_DRIVER");
#endif

/* Sync_checkpoint firmware object.
 * This is the FW-addressable structure use to hold the sync checkpoint's
 * state and other information which needs to be accessed by the firmware.
 */
typedef struct
{
	IMG_UINT32	ui32State;          /*!< Holds the current state of the sync checkpoint */
	IMG_UINT32	ui32FwRefCount;     /*!< Holds the FW reference count (num of fences/updates processed) */
} SYNC_CHECKPOINT_FW_OBJ;

/* Bit mask Firmware can use to test if a checkpoint has signalled or errored */
#define SYNC_CHECKPOINT_SIGNALLED_MASK (0x1 << 0)

#define RGXFWIF_KM_GENERAL_HEAP_TDM_SECURE_QUEUE_OFFSET_BYTES      RGX_HEAP_KM_GENERAL_RESERVED_REGION_OFFSET
#define RGXFWIF_KM_GENERAL_HEAP_TDM_SECURE_QUEUE_MAX_SIZE_BYTES    2048U

#define RGXFWIF_KM_GENERAL_HEAP_CDM_SECURE_QUEUE_OFFSET_BYTES      (RGXFWIF_KM_GENERAL_HEAP_TDM_SECURE_QUEUE_OFFSET_BYTES + RGXFWIF_KM_GENERAL_HEAP_TDM_SECURE_QUEUE_MAX_SIZE_BYTES)
#define RGXFWIF_KM_GENERAL_HEAP_CDM_SECURE_QUEUE_MAX_SIZE_BYTES    2048U

#define RGXFWIF_KM_GENERAL_HEAP_TOTAL_BYTES                        (RGXFWIF_KM_GENERAL_HEAP_TDM_SECURE_QUEUE_MAX_SIZE_BYTES + RGXFWIF_KM_GENERAL_HEAP_CDM_SECURE_QUEUE_MAX_SIZE_BYTES)

#define RGXFWIF_TDM_SECURE_QUEUE_VADDR                             (RGX_GENERAL_HEAP_BASE + RGXFWIF_KM_GENERAL_HEAP_TDM_SECURE_QUEUE_OFFSET_BYTES)
#define RGXFWIF_CDM_SECURE_QUEUE_VADDR                             (RGX_GENERAL_HEAP_BASE + RGXFWIF_KM_GENERAL_HEAP_CDM_SECURE_QUEUE_OFFSET_BYTES)

/*!
 ******************************************************************************
 * Virtualisation and Security
 *****************************************************************************/
#define FW_OSID							(0U)
#define MMU_CONTEXT_MAPPING_FWPRIV		(0U) /* FW code/private data */

#if defined(SECURE_FW_CODE_OSID)
/* software workaround for SoCs without fw_code, fw_priv_data signals, MIPS only */
#if defined(RGX_FEATURE_META)
#error "SECURE_FW_CODE_OSID is not supported on META cores"
#elif defined(RGX_FEATURE_RISCV_FW_PROCESSOR)
#error "SECURE_FW_CODE_OSID is not supported on RISC-V cores"
#elif (RGX_NUM_DRIVERS_SUPPORTED > 1)
#error "SECURE_FW_CODE_OSID is not supported on virtualization drivers"
#elif (SECURE_FW_CODE_OSID + 1 > 2)
#define MIPS_FW_CODE_OSID				(SECURE_FW_CODE_OSID)
#else
#define MIPS_FW_CODE_OSID				(1U)
#endif
#endif /* defined(SECURE_FW_CODE_OSID) */

static_assert((RGX_FW_HEAP_OSID_ASSIGNMENT == RGX_FW_HEAP_USES_FIRMWARE_OSID),
			  " Invalid RGX_FW_HEAP_OSID_ASSIGNMENT value. Rogue cores support only the RGX_FW_HEAP_USES_FIRMWARE_OSID config");

/* Firmware and Host driver share the same OSID */
#define FW_HEAP_OSID					(FW_OSID)

#if (RGX_FW_HEAP_OSID_ASSIGNMENT == RGX_FW_HEAP_USES_FIRMWARE_OSID) || defined(RGX_FEATURE_MIPS)
/* The Firmware accesses its private code & data and the interface
 * memory it shares with the KM drivers using the same MMU context */
#define MMU_CONTEXT_MAPPING_FWIF		MMU_CONTEXT_MAPPING_FWPRIV
#else
/* The Firmware accesses the interface memory it shares
 * with the KM drivers using a reserved MMU context */
#define MMU_CONTEXT_MAPPING_FWIF		(7U)
#endif

#if defined(RGX_NUM_DRIVERS_SUPPORTED) && (RGX_NUM_DRIVERS_SUPPORTED > 1)
/* virtualization without security support */
#define DRIVER_ID(osid)					(osid)
#define OSID(did)						(did)
#else
#define DRIVER_ID(osid)					(0U)
#define OSID(did)						(did)
#endif /* (RGX_NUM_DRIVERS_SUPPORTED > 1) */

#if defined(RGX_NUM_DRIVERS_SUPPORTED) && (RGX_NUM_DRIVERS_SUPPORTED > 1)

#define FOREACH_SUPPORTED_DRIVER(did)               for ((did)=RGXFW_HOST_DRIVER_ID; (did) < RGX_NUM_DRIVERS_SUPPORTED; (did)++)

#if defined(__KERNEL__)
/* Driver implementation */
#define FOREACH_ACTIVE_DRIVER(devinfo, did)        FOREACH_SUPPORTED_DRIVER(did)                                \
                                                   {                                                            \
                                                   if (devinfo->psRGXFWIfFwSysData->asOsRuntimeFlagsMirror[did].bfOsState != RGXFW_CONNECTION_FW_ACTIVE) continue;

#define END_FOREACH_ACTIVE_DRIVER                  }

#else
/* Firmware implementation */
#define FOREACH_ACTIVE_DRIVER(did)                 do {                                                                      \
                                                   unsigned int idx;                                                         \
                                                   for ((idx)=RGXFW_HOST_DRIVER_ID, (did)=gsRGXFWCtl.aui32ActiveDrivers[0U]; \
                                                        (idx) < RGXFW_NUM_ACTIVE_DRIVERS;                                         \
                                                        ++(idx), (did)=gsRGXFWCtl.aui32ActiveDrivers[(idx)])  {

#define END_FOREACH_ACTIVE_DRIVER                    }} while (false);
#endif /* defined(__KERNEL__) */


#else
#define FOREACH_SUPPORTED_DRIVER(did)               for ((did)=RGXFW_HOST_DRIVER_ID; (did) <= RGXFW_HOST_DRIVER_ID; (did)++)

#define FOREACH_ACTIVE_DRIVER(did)                  FOREACH_SUPPORTED_DRIVER(did)
#define END_FOREACH_ACTIVE_DRIVER

#endif /* (RGX_NUM_DRIVERS_SUPPORTED > 1) */

#define FOREACH_VALIDATION_OSID(osid)              for ((osid)=0; (osid) < GPUVIRT_VALIDATION_NUM_OS; (osid)++)
#define FOREACH_HW_OSID(osid)                      for ((osid)=0; (osid) < RGXFW_MAX_NUM_OSIDS; (osid)++)
#define FOREACH_DRIVER_RAW_HEAP(did)               for ((did)=RGX_FIRST_RAW_HEAP_DRIVER_ID; (did) < RGX_NUM_DRIVERS_SUPPORTED; (did)++)

#endif /* RGX_FWIF_KM_H */

/******************************************************************************
 End of file (rgx_fwif_km.h)
******************************************************************************/
