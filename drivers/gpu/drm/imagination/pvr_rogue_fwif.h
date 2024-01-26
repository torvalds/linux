/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_FWIF_H
#define PVR_ROGUE_FWIF_H

#include <linux/bits.h>
#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "pvr_rogue_defs.h"
#include "pvr_rogue_fwif_common.h"
#include "pvr_rogue_fwif_shared.h"

/*
 ****************************************************************************
 * Logging type
 ****************************************************************************
 */
#define ROGUE_FWIF_LOG_TYPE_NONE 0x00000000U
#define ROGUE_FWIF_LOG_TYPE_TRACE 0x00000001U
#define ROGUE_FWIF_LOG_TYPE_GROUP_MAIN 0x00000002U
#define ROGUE_FWIF_LOG_TYPE_GROUP_MTS 0x00000004U
#define ROGUE_FWIF_LOG_TYPE_GROUP_CLEANUP 0x00000008U
#define ROGUE_FWIF_LOG_TYPE_GROUP_CSW 0x00000010U
#define ROGUE_FWIF_LOG_TYPE_GROUP_BIF 0x00000020U
#define ROGUE_FWIF_LOG_TYPE_GROUP_PM 0x00000040U
#define ROGUE_FWIF_LOG_TYPE_GROUP_RTD 0x00000080U
#define ROGUE_FWIF_LOG_TYPE_GROUP_SPM 0x00000100U
#define ROGUE_FWIF_LOG_TYPE_GROUP_POW 0x00000200U
#define ROGUE_FWIF_LOG_TYPE_GROUP_HWR 0x00000400U
#define ROGUE_FWIF_LOG_TYPE_GROUP_HWP 0x00000800U
#define ROGUE_FWIF_LOG_TYPE_GROUP_RPM 0x00001000U
#define ROGUE_FWIF_LOG_TYPE_GROUP_DMA 0x00002000U
#define ROGUE_FWIF_LOG_TYPE_GROUP_MISC 0x00004000U
#define ROGUE_FWIF_LOG_TYPE_GROUP_DEBUG 0x80000000U
#define ROGUE_FWIF_LOG_TYPE_GROUP_MASK 0x80007FFEU
#define ROGUE_FWIF_LOG_TYPE_MASK 0x80007FFFU

/* String used in pvrdebug -h output */
#define ROGUE_FWIF_LOG_GROUPS_STRING_LIST \
	"main,mts,cleanup,csw,bif,pm,rtd,spm,pow,hwr,hwp,rpm,dma,misc,debug"

/* Table entry to map log group strings to log type value */
struct rogue_fwif_log_group_map_entry {
	const char *log_group_name;
	u32 log_group_type;
};

/*
 ****************************************************************************
 * ROGUE FW signature checks
 ****************************************************************************
 */
#define ROGUE_FW_SIG_BUFFER_SIZE_MIN (8192)

#define ROGUE_FWIF_TIMEDIFF_ID ((0x1UL << 28) | ROGUE_CR_TIMER)

/*
 ****************************************************************************
 * Trace Buffer
 ****************************************************************************
 */

/* Default size of ROGUE_FWIF_TRACEBUF_SPACE in DWords */
#define ROGUE_FW_TRACE_BUF_DEFAULT_SIZE_IN_DWORDS 12000U
#define ROGUE_FW_TRACE_BUFFER_ASSERT_SIZE 200U
#define ROGUE_FW_THREAD_NUM 1U
#define ROGUE_FW_THREAD_MAX 2U

#define ROGUE_FW_POLL_TYPE_SET 0x80000000U

struct rogue_fwif_file_info_buf {
	char path[ROGUE_FW_TRACE_BUFFER_ASSERT_SIZE];
	char info[ROGUE_FW_TRACE_BUFFER_ASSERT_SIZE];
	u32 line_num;
	u32 padding;
} __aligned(8);

struct rogue_fwif_tracebuf_space {
	u32 trace_pointer;

	u32 trace_buffer_fw_addr;

	/* To be used by host when reading from trace buffer */
	u32 *trace_buffer;

	struct rogue_fwif_file_info_buf assert_buf;
} __aligned(8);

/* Total number of FW fault logs stored */
#define ROGUE_FWIF_FWFAULTINFO_MAX (8U)

struct rogue_fw_fault_info {
	aligned_u64 cr_timer;
	aligned_u64 os_timer;

	u32 data __aligned(8);
	u32 reserved;
	struct rogue_fwif_file_info_buf fault_buf;
} __aligned(8);

enum rogue_fwif_pow_state {
	ROGUE_FWIF_POW_OFF, /* idle and ready to full power down */
	ROGUE_FWIF_POW_ON, /* running HW commands */
	ROGUE_FWIF_POW_FORCED_IDLE, /* forced idle */
	ROGUE_FWIF_POW_IDLE, /* idle waiting for host handshake */
};

/* Firmware HWR states */
/* The HW state is ok or locked up */
#define ROGUE_FWIF_HWR_HARDWARE_OK BIT(0)
/* Tells if a HWR reset is in progress */
#define ROGUE_FWIF_HWR_RESET_IN_PROGRESS BIT(1)
/* A DM unrelated lockup has been detected */
#define ROGUE_FWIF_HWR_GENERAL_LOCKUP BIT(3)
/* At least one DM is running without being close to a lockup */
#define ROGUE_FWIF_HWR_DM_RUNNING_OK BIT(4)
/* At least one DM is close to lockup */
#define ROGUE_FWIF_HWR_DM_STALLING BIT(5)
/* The FW has faulted and needs to restart */
#define ROGUE_FWIF_HWR_FW_FAULT BIT(6)
/* The FW has requested the host to restart it */
#define ROGUE_FWIF_HWR_RESTART_REQUESTED BIT(7)

#define ROGUE_FWIF_PHR_STATE_SHIFT (8U)
/* The FW has requested the host to restart it, per PHR configuration */
#define ROGUE_FWIF_PHR_RESTART_REQUESTED ((1) << ROGUE_FWIF_PHR_STATE_SHIFT)
/* A PHR triggered GPU reset has just finished */
#define ROGUE_FWIF_PHR_RESTART_FINISHED ((2) << ROGUE_FWIF_PHR_STATE_SHIFT)
#define ROGUE_FWIF_PHR_RESTART_MASK \
	(ROGUE_FWIF_PHR_RESTART_REQUESTED | ROGUE_FWIF_PHR_RESTART_FINISHED)

#define ROGUE_FWIF_PHR_MODE_OFF (0UL)
#define ROGUE_FWIF_PHR_MODE_RD_RESET (1UL)
#define ROGUE_FWIF_PHR_MODE_FULL_RESET (2UL)

/* Firmware per-DM HWR states */
/* DM is working if all flags are cleared */
#define ROGUE_FWIF_DM_STATE_WORKING (0)
/* DM is idle and ready for HWR */
#define ROGUE_FWIF_DM_STATE_READY_FOR_HWR BIT(0)
/* DM need to skip to next cmd before resuming processing */
#define ROGUE_FWIF_DM_STATE_NEEDS_SKIP BIT(2)
/* DM need partial render cleanup before resuming processing */
#define ROGUE_FWIF_DM_STATE_NEEDS_PR_CLEANUP BIT(3)
/* DM need to increment Recovery Count once fully recovered */
#define ROGUE_FWIF_DM_STATE_NEEDS_TRACE_CLEAR BIT(4)
/* DM was identified as locking up and causing HWR */
#define ROGUE_FWIF_DM_STATE_GUILTY_LOCKUP BIT(5)
/* DM was innocently affected by another lockup which caused HWR */
#define ROGUE_FWIF_DM_STATE_INNOCENT_LOCKUP BIT(6)
/* DM was identified as over-running and causing HWR */
#define ROGUE_FWIF_DM_STATE_GUILTY_OVERRUNING BIT(7)
/* DM was innocently affected by another DM over-running which caused HWR */
#define ROGUE_FWIF_DM_STATE_INNOCENT_OVERRUNING BIT(8)
/* DM was forced into HWR as it delayed more important workloads */
#define ROGUE_FWIF_DM_STATE_HARD_CONTEXT_SWITCH BIT(9)
/* DM was forced into HWR due to an uncorrected GPU ECC error */
#define ROGUE_FWIF_DM_STATE_GPU_ECC_HWR BIT(10)

/* Firmware's connection state */
enum rogue_fwif_connection_fw_state {
	/* Firmware is offline */
	ROGUE_FW_CONNECTION_FW_OFFLINE = 0,
	/* Firmware is initialised */
	ROGUE_FW_CONNECTION_FW_READY,
	/* Firmware connection is fully established */
	ROGUE_FW_CONNECTION_FW_ACTIVE,
	/* Firmware is clearing up connection data*/
	ROGUE_FW_CONNECTION_FW_OFFLOADING,
	ROGUE_FW_CONNECTION_FW_STATE_COUNT
};

/* OS' connection state */
enum rogue_fwif_connection_os_state {
	/* OS is offline */
	ROGUE_FW_CONNECTION_OS_OFFLINE = 0,
	/* OS's KM driver is setup and waiting */
	ROGUE_FW_CONNECTION_OS_READY,
	/* OS connection is fully established */
	ROGUE_FW_CONNECTION_OS_ACTIVE,
	ROGUE_FW_CONNECTION_OS_STATE_COUNT
};

struct rogue_fwif_os_runtime_flags {
	unsigned int os_state : 3;
	unsigned int fl_ok : 1;
	unsigned int fl_grow_pending : 1;
	unsigned int isolated_os : 1;
	unsigned int reserved : 26;
};

#define PVR_SLR_LOG_ENTRIES 10
/* MAX_CLIENT_CCB_NAME not visible to this header */
#define PVR_SLR_LOG_STRLEN 30

struct rogue_fwif_slr_entry {
	aligned_u64 timestamp;
	u32 fw_ctx_addr;
	u32 num_ufos;
	char ccb_name[PVR_SLR_LOG_STRLEN];
	char padding[2];
} __aligned(8);

#define MAX_THREAD_NUM 2

/* firmware trace control data */
struct rogue_fwif_tracebuf {
	u32 log_type;
	struct rogue_fwif_tracebuf_space tracebuf[MAX_THREAD_NUM];
	/*
	 * Member initialised only when sTraceBuf is actually allocated (in
	 * ROGUETraceBufferInitOnDemandResources)
	 */
	u32 tracebuf_size_in_dwords;
	/* Compatibility and other flags */
	u32 tracebuf_flags;
} __aligned(8);

/* firmware system data shared with the Host driver */
struct rogue_fwif_sysdata {
	/* Configuration flags from host */
	u32 config_flags;
	/* Extended configuration flags from host */
	u32 config_flags_ext;
	enum rogue_fwif_pow_state pow_state;
	u32 hw_perf_ridx;
	u32 hw_perf_widx;
	u32 hw_perf_wrap_count;
	/* Constant after setup, needed in FW */
	u32 hw_perf_size;
	/* The number of times the FW drops a packet due to buffer full */
	u32 hw_perf_drop_count;

	/*
	 * ui32HWPerfUt, ui32FirstDropOrdinal, ui32LastDropOrdinal only valid
	 * when FW is built with ROGUE_HWPERF_UTILIZATION &
	 * ROGUE_HWPERF_DROP_TRACKING defined in rogue_fw_hwperf.c
	 */
	/* Buffer utilisation, high watermark of bytes in use */
	u32 hw_perf_ut;
	/* The ordinal of the first packet the FW dropped */
	u32 first_drop_ordinal;
	/* The ordinal of the last packet the FW dropped */
	u32 last_drop_ordinal;
	/* State flags for each Operating System mirrored from Fw coremem */
	struct rogue_fwif_os_runtime_flags
		os_runtime_flags_mirror[ROGUE_FW_MAX_NUM_OS];

	struct rogue_fw_fault_info fault_info[ROGUE_FWIF_FWFAULTINFO_MAX];
	u32 fw_faults;
	u32 cr_poll_addr[MAX_THREAD_NUM];
	u32 cr_poll_mask[MAX_THREAD_NUM];
	u32 cr_poll_count[MAX_THREAD_NUM];
	aligned_u64 start_idle_time;

#if defined(SUPPORT_ROGUE_FW_STATS_FRAMEWORK)
#	define ROGUE_FWIF_STATS_FRAMEWORK_LINESIZE (8)
#	define ROGUE_FWIF_STATS_FRAMEWORK_MAX \
		(2048 * ROGUE_FWIF_STATS_FRAMEWORK_LINESIZE)
	u32 fw_stats_buf[ROGUE_FWIF_STATS_FRAMEWORK_MAX] __aligned(8);
#endif
	u32 hwr_state_flags;
	u32 hwr_recovery_flags[PVR_FWIF_DM_MAX];
	/* Compatibility and other flags */
	u32 fw_sys_data_flags;
	/* Identify whether MC config is P-P or P-S */
	u32 mc_config;
} __aligned(8);

/* per-os firmware shared data */
struct rogue_fwif_osdata {
	/* Configuration flags from an OS */
	u32 fw_os_config_flags;
	/* Markers to signal that the host should perform a full sync check */
	u32 fw_sync_check_mark;
	u32 host_sync_check_mark;

	u32 forced_updates_requested;
	u8 slr_log_wp;
	struct rogue_fwif_slr_entry slr_log_first;
	struct rogue_fwif_slr_entry slr_log[PVR_SLR_LOG_ENTRIES];
	aligned_u64 last_forced_update_time;

	/* Interrupt count from Threads > */
	u32 interrupt_count[MAX_THREAD_NUM];
	u32 kccb_cmds_executed;
	u32 power_sync_fw_addr;
	/* Compatibility and other flags */
	u32 fw_os_data_flags;
	u32 padding;
} __aligned(8);

/* Firmware trace time-stamp field breakup */

/* ROGUE_CR_TIMER register read (48 bits) value*/
#define ROGUE_FWT_TIMESTAMP_TIME_SHIFT (0U)
#define ROGUE_FWT_TIMESTAMP_TIME_CLRMSK (0xFFFF000000000000ull)

/* Extra debug-info (16 bits) */
#define ROGUE_FWT_TIMESTAMP_DEBUG_INFO_SHIFT (48U)
#define ROGUE_FWT_TIMESTAMP_DEBUG_INFO_CLRMSK ~ROGUE_FWT_TIMESTAMP_TIME_CLRMSK

/* Debug-info sub-fields */
/*
 * Bit 0: ROGUE_CR_EVENT_STATUS_MMU_PAGE_FAULT bit from ROGUE_CR_EVENT_STATUS
 * register
 */
#define ROGUE_FWT_DEBUG_INFO_MMU_PAGE_FAULT_SHIFT (0U)
#define ROGUE_FWT_DEBUG_INFO_MMU_PAGE_FAULT_SET \
	BIT(ROGUE_FWT_DEBUG_INFO_MMU_PAGE_FAULT_SHIFT)

/* Bit 1: ROGUE_CR_BIF_MMU_ENTRY_PENDING bit from ROGUE_CR_BIF_MMU_ENTRY register */
#define ROGUE_FWT_DEBUG_INFO_MMU_ENTRY_PENDING_SHIFT (1U)
#define ROGUE_FWT_DEBUG_INFO_MMU_ENTRY_PENDING_SET \
	BIT(ROGUE_FWT_DEBUG_INFO_MMU_ENTRY_PENDING_SHIFT)

/* Bit 2: ROGUE_CR_SLAVE_EVENT register is non-zero */
#define ROGUE_FWT_DEBUG_INFO_SLAVE_EVENTS_SHIFT (2U)
#define ROGUE_FWT_DEBUG_INFO_SLAVE_EVENTS_SET \
	BIT(ROGUE_FWT_DEBUG_INFO_SLAVE_EVENTS_SHIFT)

/* Bit 3-15: Unused bits */

#define ROGUE_FWT_DEBUG_INFO_STR_MAXLEN 64
#define ROGUE_FWT_DEBUG_INFO_STR_PREPEND " (debug info: "
#define ROGUE_FWT_DEBUG_INFO_STR_APPEND ")"

/*
 ******************************************************************************
 * HWR Data
 ******************************************************************************
 */
enum rogue_hwrtype {
	ROGUE_HWRTYPE_UNKNOWNFAILURE = 0,
	ROGUE_HWRTYPE_OVERRUN = 1,
	ROGUE_HWRTYPE_POLLFAILURE = 2,
	ROGUE_HWRTYPE_BIF0FAULT = 3,
	ROGUE_HWRTYPE_BIF1FAULT = 4,
	ROGUE_HWRTYPE_TEXASBIF0FAULT = 5,
	ROGUE_HWRTYPE_MMUFAULT = 6,
	ROGUE_HWRTYPE_MMUMETAFAULT = 7,
	ROGUE_HWRTYPE_MIPSTLBFAULT = 8,
	ROGUE_HWRTYPE_ECCFAULT = 9,
	ROGUE_HWRTYPE_MMURISCVFAULT = 10,
};

#define ROGUE_FWIF_HWRTYPE_BIF_BANK_GET(hwr_type) \
	(((hwr_type) == ROGUE_HWRTYPE_BIF0FAULT) ? 0 : 1)

#define ROGUE_FWIF_HWRTYPE_PAGE_FAULT_GET(hwr_type)       \
	((((hwr_type) == ROGUE_HWRTYPE_BIF0FAULT) ||      \
	  ((hwr_type) == ROGUE_HWRTYPE_BIF1FAULT) ||      \
	  ((hwr_type) == ROGUE_HWRTYPE_TEXASBIF0FAULT) || \
	  ((hwr_type) == ROGUE_HWRTYPE_MMUFAULT) ||       \
	  ((hwr_type) == ROGUE_HWRTYPE_MMUMETAFAULT) ||   \
	  ((hwr_type) == ROGUE_HWRTYPE_MIPSTLBFAULT) ||   \
	  ((hwr_type) == ROGUE_HWRTYPE_MMURISCVFAULT))    \
		 ? true                                   \
		 : false)

struct rogue_bifinfo {
	aligned_u64 bif_req_status;
	aligned_u64 bif_mmu_status;
	aligned_u64 pc_address; /* phys address of the page catalogue */
	aligned_u64 reserved;
};

struct rogue_eccinfo {
	u32 fault_gpu;
};

struct rogue_mmuinfo {
	aligned_u64 mmu_status[2];
	aligned_u64 pc_address; /* phys address of the page catalogue */
	aligned_u64 reserved;
};

struct rogue_pollinfo {
	u32 thread_num;
	u32 cr_poll_addr;
	u32 cr_poll_mask;
	u32 cr_poll_last_value;
	aligned_u64 reserved;
} __aligned(8);

struct rogue_tlbinfo {
	u32 bad_addr;
	u32 entry_lo;
};

struct rogue_hwrinfo {
	union {
		struct rogue_bifinfo bif_info;
		struct rogue_mmuinfo mmu_info;
		struct rogue_pollinfo poll_info;
		struct rogue_tlbinfo tlb_info;
		struct rogue_eccinfo ecc_info;
	} hwr_data;

	aligned_u64 cr_timer;
	aligned_u64 os_timer;
	u32 frame_num;
	u32 pid;
	u32 active_hwrt_data;
	u32 hwr_number;
	u32 event_status;
	u32 hwr_recovery_flags;
	enum rogue_hwrtype hwr_type;
	u32 dm;
	u32 core_id;
	aligned_u64 cr_time_of_kick;
	aligned_u64 cr_time_hw_reset_start;
	aligned_u64 cr_time_hw_reset_finish;
	aligned_u64 cr_time_freelist_ready;
	aligned_u64 reserved[2];
} __aligned(8);

/* Number of first HWR logs recorded (never overwritten by newer logs) */
#define ROGUE_FWIF_HWINFO_MAX_FIRST 8U
/* Number of latest HWR logs (older logs are overwritten by newer logs) */
#define ROGUE_FWIF_HWINFO_MAX_LAST 8U
/* Total number of HWR logs stored in a buffer */
#define ROGUE_FWIF_HWINFO_MAX \
	(ROGUE_FWIF_HWINFO_MAX_FIRST + ROGUE_FWIF_HWINFO_MAX_LAST)
/* Index of the last log in the HWR log buffer */
#define ROGUE_FWIF_HWINFO_LAST_INDEX (ROGUE_FWIF_HWINFO_MAX - 1U)

struct rogue_fwif_hwrinfobuf {
	struct rogue_hwrinfo hwr_info[ROGUE_FWIF_HWINFO_MAX];
	u32 hwr_counter;
	u32 write_index;
	u32 dd_req_count;
	u32 hwr_info_buf_flags; /* Compatibility and other flags */
	u32 hwr_dm_locked_up_count[PVR_FWIF_DM_MAX];
	u32 hwr_dm_overran_count[PVR_FWIF_DM_MAX];
	u32 hwr_dm_recovered_count[PVR_FWIF_DM_MAX];
	u32 hwr_dm_false_detect_count[PVR_FWIF_DM_MAX];
} __aligned(8);

#define ROGUE_FWIF_CTXSWITCH_PROFILE_FAST_EN (1)
#define ROGUE_FWIF_CTXSWITCH_PROFILE_MEDIUM_EN (2)
#define ROGUE_FWIF_CTXSWITCH_PROFILE_SLOW_EN (3)
#define ROGUE_FWIF_CTXSWITCH_PROFILE_NODELAY_EN (4)

#define ROGUE_FWIF_CDM_ARBITRATION_TASK_DEMAND_EN (1)
#define ROGUE_FWIF_CDM_ARBITRATION_ROUND_ROBIN_EN (2)

#define ROGUE_FWIF_ISP_SCHEDMODE_VER1_IPP (1)
#define ROGUE_FWIF_ISP_SCHEDMODE_VER2_ISP (2)
/*
 ******************************************************************************
 * ROGUE firmware Init Config Data
 ******************************************************************************
 */

/* Flag definitions affecting the firmware globally */
#define ROGUE_FWIF_INICFG_CTXSWITCH_MODE_RAND BIT(0)
#define ROGUE_FWIF_INICFG_CTXSWITCH_SRESET_EN BIT(1)
#define ROGUE_FWIF_INICFG_HWPERF_EN BIT(2)
#define ROGUE_FWIF_INICFG_DM_KILL_MODE_RAND_EN BIT(3)
#define ROGUE_FWIF_INICFG_POW_RASCALDUST BIT(4)
/* Bit 5 is reserved. */
#define ROGUE_FWIF_INICFG_FBCDC_V3_1_EN BIT(6)
#define ROGUE_FWIF_INICFG_CHECK_MLIST_EN BIT(7)
#define ROGUE_FWIF_INICFG_DISABLE_CLKGATING_EN BIT(8)
/* Bit 9 is reserved. */
/* Bit 10 is reserved. */
/* Bit 11 is reserved. */
#define ROGUE_FWIF_INICFG_REGCONFIG_EN BIT(12)
#define ROGUE_FWIF_INICFG_ASSERT_ON_OUTOFMEMORY BIT(13)
#define ROGUE_FWIF_INICFG_HWP_DISABLE_FILTER BIT(14)
/* Bit 15 is reserved. */
#define ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_SHIFT (16)
#define ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_FAST \
	(ROGUE_FWIF_CTXSWITCH_PROFILE_FAST_EN    \
	 << ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_MEDIUM \
	(ROGUE_FWIF_CTXSWITCH_PROFILE_MEDIUM_EN    \
	 << ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_SLOW \
	(ROGUE_FWIF_CTXSWITCH_PROFILE_SLOW_EN    \
	 << ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_NODELAY \
	(ROGUE_FWIF_CTXSWITCH_PROFILE_NODELAY_EN    \
	 << ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_MASK \
	(7 << ROGUE_FWIF_INICFG_CTXSWITCH_PROFILE_SHIFT)
#define ROGUE_FWIF_INICFG_DISABLE_DM_OVERLAP BIT(19)
#define ROGUE_FWIF_INICFG_ASSERT_ON_HWR_TRIGGER BIT(20)
#define ROGUE_FWIF_INICFG_FABRIC_COHERENCY_ENABLED BIT(21)
#define ROGUE_FWIF_INICFG_VALIDATE_IRQ BIT(22)
#define ROGUE_FWIF_INICFG_DISABLE_PDP_EN BIT(23)
#define ROGUE_FWIF_INICFG_SPU_POWER_STATE_MASK_CHANGE_EN BIT(24)
#define ROGUE_FWIF_INICFG_WORKEST BIT(25)
#define ROGUE_FWIF_INICFG_PDVFS BIT(26)
#define ROGUE_FWIF_INICFG_CDM_ARBITRATION_SHIFT (27)
#define ROGUE_FWIF_INICFG_CDM_ARBITRATION_TASK_DEMAND \
	(ROGUE_FWIF_CDM_ARBITRATION_TASK_DEMAND_EN    \
	 << ROGUE_FWIF_INICFG_CDM_ARBITRATION_SHIFT)
#define ROGUE_FWIF_INICFG_CDM_ARBITRATION_ROUND_ROBIN \
	(ROGUE_FWIF_CDM_ARBITRATION_ROUND_ROBIN_EN    \
	 << ROGUE_FWIF_INICFG_CDM_ARBITRATION_SHIFT)
#define ROGUE_FWIF_INICFG_CDM_ARBITRATION_MASK \
	(3 << ROGUE_FWIF_INICFG_CDM_ARBITRATION_SHIFT)
#define ROGUE_FWIF_INICFG_ISPSCHEDMODE_SHIFT (29)
#define ROGUE_FWIF_INICFG_ISPSCHEDMODE_NONE (0)
#define ROGUE_FWIF_INICFG_ISPSCHEDMODE_VER1_IPP \
	(ROGUE_FWIF_ISP_SCHEDMODE_VER1_IPP      \
	 << ROGUE_FWIF_INICFG_ISPSCHEDMODE_SHIFT)
#define ROGUE_FWIF_INICFG_ISPSCHEDMODE_VER2_ISP \
	(ROGUE_FWIF_ISP_SCHEDMODE_VER2_ISP      \
	 << ROGUE_FWIF_INICFG_ISPSCHEDMODE_SHIFT)
#define ROGUE_FWIF_INICFG_ISPSCHEDMODE_MASK        \
	(ROGUE_FWIF_INICFG_ISPSCHEDMODE_VER1_IPP | \
	 ROGUE_FWIF_INICFG_ISPSCHEDMODE_VER2_ISP)
#define ROGUE_FWIF_INICFG_VALIDATE_SOCUSC_TIMER BIT(31)

#define ROGUE_FWIF_INICFG_ALL (0xFFFFFFFFU)

/* Extended Flag definitions affecting the firmware globally */
#define ROGUE_FWIF_INICFG_EXT_TFBC_CONTROL_SHIFT (0)
/* [7]   YUV10 override
 * [6:4] Quality
 * [3]   Quality enable
 * [2:1] Compression scheme
 * [0]   Lossy group
 */
#define ROGUE_FWIF_INICFG_EXT_TFBC_CONTROL_MASK (0xFF)
#define ROGUE_FWIF_INICFG_EXT_ALL (ROGUE_FWIF_INICFG_EXT_TFBC_CONTROL_MASK)

/* Flag definitions affecting only workloads submitted by a particular OS */
#define ROGUE_FWIF_INICFG_OS_CTXSWITCH_TDM_EN BIT(0)
#define ROGUE_FWIF_INICFG_OS_CTXSWITCH_GEOM_EN BIT(1)
#define ROGUE_FWIF_INICFG_OS_CTXSWITCH_FRAG_EN BIT(2)
#define ROGUE_FWIF_INICFG_OS_CTXSWITCH_CDM_EN BIT(3)

#define ROGUE_FWIF_INICFG_OS_LOW_PRIO_CS_TDM BIT(4)
#define ROGUE_FWIF_INICFG_OS_LOW_PRIO_CS_GEOM BIT(5)
#define ROGUE_FWIF_INICFG_OS_LOW_PRIO_CS_FRAG BIT(6)
#define ROGUE_FWIF_INICFG_OS_LOW_PRIO_CS_CDM BIT(7)

#define ROGUE_FWIF_INICFG_OS_ALL (0xFF)

#define ROGUE_FWIF_INICFG_OS_CTXSWITCH_DM_ALL     \
	(ROGUE_FWIF_INICFG_OS_CTXSWITCH_TDM_EN |  \
	 ROGUE_FWIF_INICFG_OS_CTXSWITCH_GEOM_EN | \
	 ROGUE_FWIF_INICFG_OS_CTXSWITCH_FRAG_EN |   \
	 ROGUE_FWIF_INICFG_OS_CTXSWITCH_CDM_EN)

#define ROGUE_FWIF_INICFG_OS_CTXSWITCH_CLRMSK \
	~(ROGUE_FWIF_INICFG_OS_CTXSWITCH_DM_ALL)

#define ROGUE_FWIF_FILTCFG_TRUNCATE_HALF BIT(3)
#define ROGUE_FWIF_FILTCFG_TRUNCATE_INT BIT(2)
#define ROGUE_FWIF_FILTCFG_NEW_FILTER_MODE BIT(1)

enum rogue_activepm_conf {
	ROGUE_ACTIVEPM_FORCE_OFF = 0,
	ROGUE_ACTIVEPM_FORCE_ON = 1,
	ROGUE_ACTIVEPM_DEFAULT = 2
};

enum rogue_rd_power_island_conf {
	ROGUE_RD_POWER_ISLAND_FORCE_OFF = 0,
	ROGUE_RD_POWER_ISLAND_FORCE_ON = 1,
	ROGUE_RD_POWER_ISLAND_DEFAULT = 2
};

struct rogue_fw_register_list {
	/* Register number */
	u16 reg_num;
	/* Indirect register number (or 0 if not used) */
	u16 indirect_reg_num;
	/* Start value for indirect register */
	u16 indirect_start_val;
	/* End value for indirect register */
	u16 indirect_end_val;
};

struct rogue_fwif_dllist_node {
	u32 p;
	u32 n;
};

/*
 * This number is used to represent an invalid page catalogue physical address
 */
#define ROGUE_FWIF_INVALID_PC_PHYADDR 0xFFFFFFFFFFFFFFFFLLU

/* This number is used to represent unallocated page catalog base register */
#define ROGUE_FW_BIF_INVALID_PCSET 0xFFFFFFFFU

/* Firmware memory context. */
struct rogue_fwif_fwmemcontext {
	/* device physical address of context's page catalogue */
	aligned_u64 pc_dev_paddr;
	/*
	 * associated page catalog base register (ROGUE_FW_BIF_INVALID_PCSET ==
	 * unallocated)
	 */
	u32 page_cat_base_reg_set;
	/* breakpoint address */
	u32 breakpoint_addr;
	/* breakpoint handler address */
	u32 bp_handler_addr;
	/* DM and enable control for BP */
	u32 breakpoint_ctl;
	/* Compatibility and other flags */
	u32 fw_mem_ctx_flags;
	u32 padding;
} __aligned(8);

/*
 * FW context state flags
 */
#define ROGUE_FWIF_CONTEXT_FLAGS_NEED_RESUME (0x00000001U)
#define ROGUE_FWIF_CONTEXT_FLAGS_MC_NEED_RESUME_MASKFULL (0x000000FFU)
#define ROGUE_FWIF_CONTEXT_FLAGS_TDM_HEADER_STALE (0x00000100U)
#define ROGUE_FWIF_CONTEXT_FLAGS_LAST_KICK_SECURE (0x00000200U)

#define ROGUE_NUM_GEOM_CORES_MAX 4

/*
 * FW-accessible TA state which must be written out to memory on context store
 */
struct rogue_fwif_geom_ctx_state_per_geom {
	/* To store in mid-TA */
	aligned_u64 geom_reg_vdm_call_stack_pointer;
	/* Initial value (in case is 'lost' due to a lock-up */
	aligned_u64 geom_reg_vdm_call_stack_pointer_init;
	u32 geom_reg_vbs_so_prim[4];
	u16 geom_current_idx;
	u16 padding[3];
} __aligned(8);

struct rogue_fwif_geom_ctx_state {
	/* FW-accessible TA state which must be written out to memory on context store */
	struct rogue_fwif_geom_ctx_state_per_geom geom_core[ROGUE_NUM_GEOM_CORES_MAX];
} __aligned(8);

/*
 * FW-accessible ISP state which must be written out to memory on context store
 */
struct rogue_fwif_frag_ctx_state {
	u32 frag_reg_pm_deallocated_mask_status;
	u32 frag_reg_dm_pds_mtilefree_status;
	/* Compatibility and other flags */
	u32 ctx_state_flags;
	/*
	 * frag_reg_isp_store should be the last element of the structure as this
	 * is an array whose size is determined at runtime after detecting the
	 * ROGUE core
	 */
	u32 frag_reg_isp_store[];
} __aligned(8);

#define ROGUE_FWIF_CTX_USING_BUFFER_A (0)
#define ROGUE_FWIF_CTX_USING_BUFFER_B (1U)

struct rogue_fwif_compute_ctx_state {
	u32 ctx_state_flags; /* Target buffer and other flags */
};

struct rogue_fwif_fwcommoncontext {
	/* CCB details for this firmware context */
	u32 ccbctl_fw_addr; /* CCB control */
	u32 ccb_fw_addr; /* CCB base */
	struct rogue_fwif_dma_addr ccb_meta_dma_addr;

	/* Context suspend state */
	/* geom/frag context suspend state, read/written by FW */
	u32 context_state_addr __aligned(8);

	/* Flags e.g. for context switching */
	u32 fw_com_ctx_flags;
	u32 priority;
	u32 priority_seq_num;

	/* Framework state */
	/* Register updates for Framework */
	u32 rf_cmd_addr __aligned(8);

	/* Statistic updates waiting to be passed back to the host... */
	/* True when some stats are pending */
	bool stats_pending __aligned(4);
	/* Number of stores on this context since last update */
	s32 stats_num_stores;
	/* Number of OOMs on this context since last update */
	s32 stats_num_out_of_memory;
	/* Number of PRs on this context since last update */
	s32 stats_num_partial_renders;
	/* Data Master type */
	u32 dm;
	/* Device Virtual Address of the signal the context is waiting on */
	aligned_u64 wait_signal_address;
	/* List entry for the wait-signal list */
	struct rogue_fwif_dllist_node wait_signal_node __aligned(8);
	/* List entry for the buffer stalled list */
	struct rogue_fwif_dllist_node buf_stalled_node __aligned(8);
	/* Address of the circular buffer queue pointers */
	aligned_u64 cbuf_queue_ctrl_addr;

	aligned_u64 robustness_address;
	/* Max HWR deadline limit in ms */
	u32 max_deadline_ms;
	/* Following HWR circular buffer read-offset needs resetting */
	bool read_offset_needs_reset;

	/* List entry for the waiting list */
	struct rogue_fwif_dllist_node waiting_node __aligned(8);
	/* List entry for the run list */
	struct rogue_fwif_dllist_node run_node __aligned(8);
	/* UFO that last failed (or NULL) */
	struct rogue_fwif_ufo last_failed_ufo;

	/* Memory context */
	u32 fw_mem_context_fw_addr;

	/* References to the host side originators */
	/* the Server Common Context */
	u32 server_common_context_id;
	/* associated process ID */
	u32 pid;

	/* True when Geom DM OOM is not allowed */
	bool geom_oom_disabled __aligned(4);
} __aligned(8);

/* Firmware render context. */
struct rogue_fwif_fwrendercontext {
	/* Geometry firmware context. */
	struct rogue_fwif_fwcommoncontext geom_context;
	/* Fragment firmware context. */
	struct rogue_fwif_fwcommoncontext frag_context;

	struct rogue_fwif_static_rendercontext_state static_render_context_state;

	/* Number of commands submitted to the WorkEst FW CCB */
	u32 work_est_ccb_submitted;

	/* Compatibility and other flags */
	u32 fw_render_ctx_flags;
} __aligned(8);

/* Firmware compute context. */
struct rogue_fwif_fwcomputecontext {
	/* Firmware context for the CDM */
	struct rogue_fwif_fwcommoncontext cdm_context;

	struct rogue_fwif_static_computecontext_state
		static_compute_context_state;

	/* Number of commands submitted to the WorkEst FW CCB */
	u32 work_est_ccb_submitted;

	/* Compatibility and other flags */
	u32 compute_ctx_flags;

	u32 wgp_state;
	u32 wgp_checksum;
	u32 core_mask_a;
	u32 core_mask_b;
} __aligned(8);

/* Firmware TDM context. */
struct rogue_fwif_fwtdmcontext {
	/* Firmware context for the TDM */
	struct rogue_fwif_fwcommoncontext tdm_context;

	/* Number of commands submitted to the WorkEst FW CCB */
	u32 work_est_ccb_submitted;
} __aligned(8);

/* Firmware TQ3D context. */
struct rogue_fwif_fwtransfercontext {
	/* Firmware context for TQ3D. */
	struct rogue_fwif_fwcommoncontext tq_context;
} __aligned(8);

/*
 ******************************************************************************
 * Defines for CMD_TYPE corruption detection and forward compatibility check
 ******************************************************************************
 */

/*
 * CMD_TYPE 32bit contains:
 * 31:16	Reserved for magic value to detect corruption (16 bits)
 * 15		Reserved for ROGUE_CCB_TYPE_TASK (1 bit)
 * 14:0		Bits available for CMD_TYPEs (15 bits)
 */

/* Magic value to detect corruption */
#define ROGUE_CMD_MAGIC_DWORD (0x2ABC)
#define ROGUE_CMD_MAGIC_DWORD_MASK (0xFFFF0000U)
#define ROGUE_CMD_MAGIC_DWORD_SHIFT (16U)
#define ROGUE_CMD_MAGIC_DWORD_SHIFTED \
	(ROGUE_CMD_MAGIC_DWORD << ROGUE_CMD_MAGIC_DWORD_SHIFT)

/* Kernel CCB control for ROGUE */
struct rogue_fwif_ccb_ctl {
	/* write offset into array of commands (MUST be aligned to 16 bytes!) */
	u32 write_offset;
	/* Padding to ensure read and write offsets are in separate cache lines. */
	u8 padding[128 - sizeof(u32)];
	/* read offset into array of commands */
	u32 read_offset;
	/* Offset wrapping mask (Total capacity of the CCB - 1) */
	u32 wrap_mask;
	/* size of each command in bytes */
	u32 cmd_size;
	u32 padding2;
} __aligned(8);

/* Kernel CCB command structure for ROGUE */

#define ROGUE_FWIF_MMUCACHEDATA_FLAGS_PT (0x1U) /* MMU_CTRL_INVAL_PT_EN */
#define ROGUE_FWIF_MMUCACHEDATA_FLAGS_PD (0x2U) /* MMU_CTRL_INVAL_PD_EN */
#define ROGUE_FWIF_MMUCACHEDATA_FLAGS_PC (0x4U) /* MMU_CTRL_INVAL_PC_EN */

/*
 * can't use PM_TLB0 bit from BIFPM_CTRL reg because it collides with PT
 * bit from BIF_CTRL reg
 */
#define ROGUE_FWIF_MMUCACHEDATA_FLAGS_PMTLB (0x10)
/* BIF_CTRL_INVAL_TLB1_EN */
#define ROGUE_FWIF_MMUCACHEDATA_FLAGS_TLB \
	(ROGUE_FWIF_MMUCACHEDATA_FLAGS_PMTLB | 0x8)
/* MMU_CTRL_INVAL_ALL_CONTEXTS_EN */
#define ROGUE_FWIF_MMUCACHEDATA_FLAGS_CTX_ALL (0x800)

/* indicates FW should interrupt the host */
#define ROGUE_FWIF_MMUCACHEDATA_FLAGS_INTERRUPT (0x4000000U)

struct rogue_fwif_mmucachedata {
	u32 cache_flags;
	u32 mmu_cache_sync_fw_addr;
	u32 mmu_cache_sync_update_value;
};

#define ROGUE_FWIF_BPDATA_FLAGS_ENABLE BIT(0)
#define ROGUE_FWIF_BPDATA_FLAGS_WRITE BIT(1)
#define ROGUE_FWIF_BPDATA_FLAGS_CTL BIT(2)
#define ROGUE_FWIF_BPDATA_FLAGS_REGS BIT(3)

struct rogue_fwif_bpdata {
	/* Memory context */
	u32 fw_mem_context_fw_addr;
	/* Breakpoint address */
	u32 bp_addr;
	/* Breakpoint handler */
	u32 bp_handler_addr;
	/* Breakpoint control */
	u32 bp_dm;
	u32 bp_data_flags;
	/* Number of temporary registers to overallocate */
	u32 temp_regs;
	/* Number of shared registers to overallocate */
	u32 shared_regs;
	/* DM associated with the breakpoint */
	u32 dm;
};

#define ROGUE_FWIF_KCCB_CMD_KICK_DATA_MAX_NUM_CLEANUP_CTLS \
	(ROGUE_FWIF_PRBUFFER_MAXSUPPORTED + 1U) /* +1 is RTDATASET cleanup */

struct rogue_fwif_kccb_cmd_kick_data {
	/* address of the firmware context */
	u32 context_fw_addr;
	/* Client CCB woff update */
	u32 client_woff_update;
	/* Client CCB wrap mask update after CCCB growth */
	u32 client_wrap_mask_update;
	/* number of CleanupCtl pointers attached */
	u32 num_cleanup_ctl;
	/* CleanupCtl structures associated with command */
	u32 cleanup_ctl_fw_addr
		[ROGUE_FWIF_KCCB_CMD_KICK_DATA_MAX_NUM_CLEANUP_CTLS];
	/*
	 * offset to the CmdHeader which houses the workload estimation kick
	 * data.
	 */
	u32 work_est_cmd_header_offset;
};

struct rogue_fwif_kccb_cmd_combined_geom_frag_kick_data {
	struct rogue_fwif_kccb_cmd_kick_data geom_cmd_kick_data;
	struct rogue_fwif_kccb_cmd_kick_data frag_cmd_kick_data;
};

struct rogue_fwif_kccb_cmd_force_update_data {
	/* address of the firmware context */
	u32 context_fw_addr;
	/* Client CCB fence offset */
	u32 ccb_fence_offset;
};

enum rogue_fwif_cleanup_type {
	/* FW common context cleanup */
	ROGUE_FWIF_CLEANUP_FWCOMMONCONTEXT,
	/* FW HW RT data cleanup */
	ROGUE_FWIF_CLEANUP_HWRTDATA,
	/* FW freelist cleanup */
	ROGUE_FWIF_CLEANUP_FREELIST,
	/* FW ZS Buffer cleanup */
	ROGUE_FWIF_CLEANUP_ZSBUFFER,
};

struct rogue_fwif_cleanup_request {
	/* Cleanup type */
	enum rogue_fwif_cleanup_type cleanup_type;
	union {
		/* FW common context to cleanup */
		u32 context_fw_addr;
		/* HW RT to cleanup */
		u32 hwrt_data_fw_addr;
		/* Freelist to cleanup */
		u32 freelist_fw_addr;
		/* ZS Buffer to cleanup */
		u32 zs_buffer_fw_addr;
	} cleanup_data;
};

enum rogue_fwif_power_type {
	ROGUE_FWIF_POW_OFF_REQ = 1,
	ROGUE_FWIF_POW_FORCED_IDLE_REQ,
	ROGUE_FWIF_POW_NUM_UNITS_CHANGE,
	ROGUE_FWIF_POW_APM_LATENCY_CHANGE
};

enum rogue_fwif_power_force_idle_type {
	ROGUE_FWIF_POWER_FORCE_IDLE = 1,
	ROGUE_FWIF_POWER_CANCEL_FORCED_IDLE,
	ROGUE_FWIF_POWER_HOST_TIMEOUT,
};

struct rogue_fwif_power_request {
	/* Type of power request */
	enum rogue_fwif_power_type pow_type;
	union {
		/* Number of active Dusts */
		u32 num_of_dusts;
		/* If the operation is mandatory */
		bool forced __aligned(4);
		/*
		 * Type of Request. Consolidating Force Idle, Cancel Forced
		 * Idle, Host Timeout
		 */
		enum rogue_fwif_power_force_idle_type pow_request_type;
	} power_req_data;
};

struct rogue_fwif_slcflushinvaldata {
	/* Context to fence on (only useful when bDMContext == TRUE) */
	u32 context_fw_addr;
	/* Invalidate the cache as well as flushing */
	bool inval __aligned(4);
	/* The data to flush/invalidate belongs to a specific DM context */
	bool dm_context __aligned(4);
	/* Optional address of range (only useful when bDMContext == FALSE) */
	aligned_u64 address;
	/* Optional size of range (only useful when bDMContext == FALSE) */
	aligned_u64 size;
};

enum rogue_fwif_hwperf_update_config {
	ROGUE_FWIF_HWPERF_CTRL_TOGGLE = 0,
	ROGUE_FWIF_HWPERF_CTRL_SET = 1,
	ROGUE_FWIF_HWPERF_CTRL_EMIT_FEATURES_EV = 2
};

struct rogue_fwif_hwperf_ctrl {
	enum rogue_fwif_hwperf_update_config opcode; /* Control operation code */
	aligned_u64 mask; /* Mask of events to toggle */
};

struct rogue_fwif_hwperf_config_enable_blks {
	/* Number of ROGUE_HWPERF_CONFIG_MUX_CNTBLK in the array */
	u32 num_blocks;
	/* Address of the ROGUE_HWPERF_CONFIG_MUX_CNTBLK array */
	u32 block_configs_fw_addr;
};

struct rogue_fwif_hwperf_config_da_blks {
	/* Number of ROGUE_HWPERF_CONFIG_CNTBLK in the array */
	u32 num_blocks;
	/* Address of the ROGUE_HWPERF_CONFIG_CNTBLK array */
	u32 block_configs_fw_addr;
};

struct rogue_fwif_coreclkspeedchange_data {
	u32 new_clock_speed; /* New clock speed */
};

#define ROGUE_FWIF_HWPERF_CTRL_BLKS_MAX 16

struct rogue_fwif_hwperf_ctrl_blks {
	bool enable;
	/* Number of block IDs in the array */
	u32 num_blocks;
	/* Array of ROGUE_HWPERF_CNTBLK_ID values */
	u16 block_ids[ROGUE_FWIF_HWPERF_CTRL_BLKS_MAX];
};

struct rogue_fwif_hwperf_select_custom_cntrs {
	u16 custom_block;
	u16 num_counters;
	u32 custom_counter_ids_fw_addr;
};

struct rogue_fwif_zsbuffer_backing_data {
	u32 zs_buffer_fw_addr; /* ZS-Buffer FW address */

	bool done __aligned(4); /* action backing/unbacking succeeded */
};

struct rogue_fwif_freelist_gs_data {
	/* Freelist FW address */
	u32 freelist_fw_addr;
	/* Amount of the Freelist change */
	u32 delta_pages;
	/* New amount of pages on the freelist (including ready pages) */
	u32 new_pages;
	/* Number of ready pages to be held in reserve until OOM */
	u32 ready_pages;
};

#define MAX_FREELISTS_SIZE 3
#define MAX_HW_GEOM_FRAG_CONTEXTS_SIZE 3

#define ROGUE_FWIF_MAX_FREELISTS_TO_RECONSTRUCT \
	(MAX_HW_GEOM_FRAG_CONTEXTS_SIZE * MAX_FREELISTS_SIZE * 2U)
#define ROGUE_FWIF_FREELISTS_RECONSTRUCTION_FAILED_FLAG 0x80000000U

struct rogue_fwif_freelists_reconstruction_data {
	u32 freelist_count;
	u32 freelist_ids[ROGUE_FWIF_MAX_FREELISTS_TO_RECONSTRUCT];
};

struct rogue_fwif_write_offset_update_data {
	/*
	 * Context to that may need to be resumed following write offset update
	 */
	u32 context_fw_addr;
} __aligned(8);

/*
 ******************************************************************************
 * Proactive DVFS Structures
 ******************************************************************************
 */
#define NUM_OPP_VALUES 16

struct pdvfs_opp {
	u32 volt; /* V  */
	u32 freq; /* Hz */
} __aligned(8);

struct rogue_fwif_pdvfs_opp {
	struct pdvfs_opp opp_values[NUM_OPP_VALUES];
	u32 min_opp_point;
	u32 max_opp_point;
} __aligned(8);

struct rogue_fwif_pdvfs_max_freq_data {
	u32 max_opp_point;
} __aligned(8);

struct rogue_fwif_pdvfs_min_freq_data {
	u32 min_opp_point;
} __aligned(8);

/*
 ******************************************************************************
 * Register configuration structures
 ******************************************************************************
 */

#define ROGUE_FWIF_REG_CFG_MAX_SIZE 512

enum rogue_fwif_regdata_cmd_type {
	ROGUE_FWIF_REGCFG_CMD_ADD = 101,
	ROGUE_FWIF_REGCFG_CMD_CLEAR = 102,
	ROGUE_FWIF_REGCFG_CMD_ENABLE = 103,
	ROGUE_FWIF_REGCFG_CMD_DISABLE = 104
};

enum rogue_fwif_reg_cfg_type {
	/* Sidekick power event */
	ROGUE_FWIF_REG_CFG_TYPE_PWR_ON = 0,
	/* Rascal / dust power event */
	ROGUE_FWIF_REG_CFG_TYPE_DUST_CHANGE,
	/* Geometry kick */
	ROGUE_FWIF_REG_CFG_TYPE_GEOM,
	/* Fragment kick */
	ROGUE_FWIF_REG_CFG_TYPE_FRAG,
	/* Compute kick */
	ROGUE_FWIF_REG_CFG_TYPE_CDM,
	/* TLA kick */
	ROGUE_FWIF_REG_CFG_TYPE_TLA,
	/* TDM kick */
	ROGUE_FWIF_REG_CFG_TYPE_TDM,
	/* Applies to all types. Keep as last element */
	ROGUE_FWIF_REG_CFG_TYPE_ALL
};

struct rogue_fwif_reg_cfg_rec {
	u64 sddr;
	u64 mask;
	u64 value;
};

struct rogue_fwif_regconfig_data {
	enum rogue_fwif_regdata_cmd_type cmd_type;
	enum rogue_fwif_reg_cfg_type reg_config_type;
	struct rogue_fwif_reg_cfg_rec reg_config __aligned(8);
};

struct rogue_fwif_reg_cfg {
	/*
	 * PDump WRW command write granularity is 32 bits.
	 * Add padding to ensure array size is 32 bit granular.
	 */
	u8 num_regs_type[ALIGN((u32)ROGUE_FWIF_REG_CFG_TYPE_ALL,
			       sizeof(u32))] __aligned(8);
	struct rogue_fwif_reg_cfg_rec
		reg_configs[ROGUE_FWIF_REG_CFG_MAX_SIZE] __aligned(8);
} __aligned(8);

enum rogue_fwif_os_state_change {
	ROGUE_FWIF_OS_ONLINE = 1,
	ROGUE_FWIF_OS_OFFLINE
};

struct rogue_fwif_os_state_change_data {
	u32 osid;
	enum rogue_fwif_os_state_change new_os_state;
} __aligned(8);

enum rogue_fwif_counter_dump_request {
	ROGUE_FWIF_PWR_COUNTER_DUMP_START = 1,
	ROGUE_FWIF_PWR_COUNTER_DUMP_STOP,
	ROGUE_FWIF_PWR_COUNTER_DUMP_SAMPLE,
};

struct rogue_fwif_counter_dump_data {
	enum rogue_fwif_counter_dump_request counter_dump_request;
} __aligned(8);

enum rogue_fwif_kccb_cmd_type {
	/* Common commands */
	ROGUE_FWIF_KCCB_CMD_KICK = 101U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	ROGUE_FWIF_KCCB_CMD_MMUCACHE = 102U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	ROGUE_FWIF_KCCB_CMD_BP = 103U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* SLC flush and invalidation request */
	ROGUE_FWIF_KCCB_CMD_SLCFLUSHINVAL = 105U |
					    ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/*
	 * Requests cleanup of a FW resource (type specified in the command
	 * data)
	 */
	ROGUE_FWIF_KCCB_CMD_CLEANUP = 106U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Power request */
	ROGUE_FWIF_KCCB_CMD_POW = 107U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Backing for on-demand ZS-Buffer done */
	ROGUE_FWIF_KCCB_CMD_ZSBUFFER_BACKING_UPDATE =
		108U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Unbacking for on-demand ZS-Buffer done */
	ROGUE_FWIF_KCCB_CMD_ZSBUFFER_UNBACKING_UPDATE =
		109U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Freelist Grow done */
	ROGUE_FWIF_KCCB_CMD_FREELIST_GROW_UPDATE =
		110U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Freelists Reconstruction done */
	ROGUE_FWIF_KCCB_CMD_FREELISTS_RECONSTRUCTION_UPDATE =
		112U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/*
	 * Informs the firmware that the host has added more data to a CDM2
	 * Circular Buffer
	 */
	ROGUE_FWIF_KCCB_CMD_NOTIFY_WRITE_OFFSET_UPDATE =
		114U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Health check request */
	ROGUE_FWIF_KCCB_CMD_HEALTH_CHECK = 115U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Forcing signalling of all unmet UFOs for a given CCB offset */
	ROGUE_FWIF_KCCB_CMD_FORCE_UPDATE = 116U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,

	/* There is a geometry and a fragment command in this single kick */
	ROGUE_FWIF_KCCB_CMD_COMBINED_GEOM_FRAG_KICK = 117U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Informs the FW that a Guest OS has come online / offline. */
	ROGUE_FWIF_KCCB_CMD_OS_ONLINE_STATE_CONFIGURE	= 118U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,

	/* Commands only permitted to the native or host OS */
	ROGUE_FWIF_KCCB_CMD_REGCONFIG = 200U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,

	/* Configure HWPerf events (to be generated) and HWPerf buffer address (if required) */
	ROGUE_FWIF_KCCB_CMD_HWPERF_UPDATE_CONFIG = 201U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,

	/* Enable or disable multiple HWPerf blocks (reusing existing configuration) */
	ROGUE_FWIF_KCCB_CMD_HWPERF_CTRL_BLKS = 203U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Core clock speed change event */
	ROGUE_FWIF_KCCB_CMD_CORECLKSPEEDCHANGE = 204U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,

	/*
	 * Ask the firmware to update its cached ui32LogType value from the (shared)
	 * tracebuf control structure
	 */
	ROGUE_FWIF_KCCB_CMD_LOGTYPE_UPDATE = 206U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Set a maximum frequency/OPP point */
	ROGUE_FWIF_KCCB_CMD_PDVFS_LIMIT_MAX_FREQ = 207U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/*
	 * Changes the relative scheduling priority for a particular OSid. It can
	 * only be serviced for the Host DDK
	 */
	ROGUE_FWIF_KCCB_CMD_OSID_PRIORITY_CHANGE = 208U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Set or clear firmware state flags */
	ROGUE_FWIF_KCCB_CMD_STATEFLAGS_CTRL = 209U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,

	/* Set a minimum frequency/OPP point */
	ROGUE_FWIF_KCCB_CMD_PDVFS_LIMIT_MIN_FREQ = 212U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Configure Periodic Hardware Reset behaviour */
	ROGUE_FWIF_KCCB_CMD_PHR_CFG = 213U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,

	/* Configure Safety Firmware Watchdog */
	ROGUE_FWIF_KCCB_CMD_WDG_CFG = 215U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Controls counter dumping in the FW */
	ROGUE_FWIF_KCCB_CMD_COUNTER_DUMP = 216U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Configure, clear and enable multiple HWPerf blocks */
	ROGUE_FWIF_KCCB_CMD_HWPERF_CONFIG_ENABLE_BLKS = 217U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Configure the custom counters for HWPerf */
	ROGUE_FWIF_KCCB_CMD_HWPERF_SELECT_CUSTOM_CNTRS = 218U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,

	/* Configure directly addressable counters for HWPerf */
	ROGUE_FWIF_KCCB_CMD_HWPERF_CONFIG_BLKS = 220U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
};

#define ROGUE_FWIF_LAST_ALLOWED_GUEST_KCCB_CMD \
	(ROGUE_FWIF_KCCB_CMD_REGCONFIG - 1)

/* Kernel CCB command packet */
struct rogue_fwif_kccb_cmd {
	/* Command type */
	enum rogue_fwif_kccb_cmd_type cmd_type;
	/* Compatibility and other flags */
	u32 kccb_flags;

	/*
	 * NOTE: Make sure that uCmdData is the last member of this struct
	 * This is to calculate actual command size for device mem copy.
	 * (Refer ROGUEGetCmdMemCopySize())
	 */
	union {
		/* Data for Kick command */
		struct rogue_fwif_kccb_cmd_kick_data cmd_kick_data;
		/* Data for combined geom/frag Kick command */
		struct rogue_fwif_kccb_cmd_combined_geom_frag_kick_data
			combined_geom_frag_cmd_kick_data;
		/* Data for MMU cache command */
		struct rogue_fwif_mmucachedata mmu_cache_data;
		/* Data for Breakpoint Commands */
		struct rogue_fwif_bpdata bp_data;
		/* Data for SLC Flush/Inval commands */
		struct rogue_fwif_slcflushinvaldata slc_flush_inval_data;
		/* Data for cleanup commands */
		struct rogue_fwif_cleanup_request cleanup_data;
		/* Data for power request commands */
		struct rogue_fwif_power_request pow_data;
		/* Data for HWPerf control command */
		struct rogue_fwif_hwperf_ctrl hw_perf_ctrl;
		/*
		 * Data for HWPerf configure, clear and enable performance
		 * counter block command
		 */
		struct rogue_fwif_hwperf_config_enable_blks
			hw_perf_cfg_enable_blks;
		/*
		 * Data for HWPerf enable or disable performance counter block
		 * commands
		 */
		struct rogue_fwif_hwperf_ctrl_blks hw_perf_ctrl_blks;
		/* Data for HWPerf configure the custom counters to read */
		struct rogue_fwif_hwperf_select_custom_cntrs
			hw_perf_select_cstm_cntrs;
		/* Data for HWPerf configure Directly Addressable blocks */
		struct rogue_fwif_hwperf_config_da_blks hw_perf_cfg_da_blks;
		/* Data for core clock speed change */
		struct rogue_fwif_coreclkspeedchange_data
			core_clk_speed_change_data;
		/* Feedback for Z/S Buffer backing/unbacking */
		struct rogue_fwif_zsbuffer_backing_data zs_buffer_backing_data;
		/* Feedback for Freelist grow/shrink */
		struct rogue_fwif_freelist_gs_data free_list_gs_data;
		/* Feedback for Freelists reconstruction*/
		struct rogue_fwif_freelists_reconstruction_data
			free_lists_reconstruction_data;
		/* Data for custom register configuration */
		struct rogue_fwif_regconfig_data reg_config_data;
		/* Data for informing the FW about the write offset update */
		struct rogue_fwif_write_offset_update_data
			write_offset_update_data;
		/* Data for setting the max frequency/OPP */
		struct rogue_fwif_pdvfs_max_freq_data pdvfs_max_freq_data;
		/* Data for setting the min frequency/OPP */
		struct rogue_fwif_pdvfs_min_freq_data pdvfs_min_freq_data;
		/* Data for updating the Guest Online states */
		struct rogue_fwif_os_state_change_data cmd_os_online_state_data;
		/* Dev address for TBI buffer allocated on demand */
		u32 tbi_buffer_fw_addr;
		/* Data for dumping of register ranges */
		struct rogue_fwif_counter_dump_data counter_dump_config_data;
		/* Data for signalling all unmet fences for a given CCB */
		struct rogue_fwif_kccb_cmd_force_update_data force_update_data;
	} cmd_data __aligned(8);
} __aligned(8);

PVR_FW_STRUCT_SIZE_ASSERT(struct rogue_fwif_kccb_cmd);

/*
 ******************************************************************************
 * Firmware CCB command structure for ROGUE
 ******************************************************************************
 */

struct rogue_fwif_fwccb_cmd_zsbuffer_backing_data {
	u32 zs_buffer_id;
};

struct rogue_fwif_fwccb_cmd_freelist_gs_data {
	u32 freelist_id;
};

struct rogue_fwif_fwccb_cmd_freelists_reconstruction_data {
	u32 freelist_count;
	u32 hwr_counter;
	u32 freelist_ids[ROGUE_FWIF_MAX_FREELISTS_TO_RECONSTRUCT];
};

/* 1 if a page fault happened */
#define ROGUE_FWIF_FWCCB_CMD_CONTEXT_RESET_FLAG_PF BIT(0)
/* 1 if applicable to all contexts */
#define ROGUE_FWIF_FWCCB_CMD_CONTEXT_RESET_FLAG_ALL_CTXS BIT(1)

struct rogue_fwif_fwccb_cmd_context_reset_data {
	/* Context affected by the reset */
	u32 server_common_context_id;
	/* Reason for reset */
	enum rogue_context_reset_reason reset_reason;
	/* Data Master affected by the reset */
	u32 dm;
	/* Job ref running at the time of reset */
	u32 reset_job_ref;
	/* ROGUE_FWIF_FWCCB_CMD_CONTEXT_RESET_FLAG bitfield */
	u32 flags;
	/* At what page catalog address */
	aligned_u64 pc_address;
	/* Page fault address (only when applicable) */
	aligned_u64 fault_address;
};

struct rogue_fwif_fwccb_cmd_fw_pagefault_data {
	/* Page fault address */
	u64 fw_fault_addr;
};

enum rogue_fwif_fwccb_cmd_type {
	/* Requests ZSBuffer to be backed with physical pages */
	ROGUE_FWIF_FWCCB_CMD_ZSBUFFER_BACKING = 101U |
						ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Requests ZSBuffer to be unbacked */
	ROGUE_FWIF_FWCCB_CMD_ZSBUFFER_UNBACKING = 102U |
						  ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Requests an on-demand freelist grow/shrink */
	ROGUE_FWIF_FWCCB_CMD_FREELIST_GROW = 103U |
					     ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Requests freelists reconstruction */
	ROGUE_FWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION =
		104U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Notifies host of a HWR event on a context */
	ROGUE_FWIF_FWCCB_CMD_CONTEXT_RESET_NOTIFICATION =
		105U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Requests an on-demand debug dump */
	ROGUE_FWIF_FWCCB_CMD_DEBUG_DUMP = 106U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	/* Requests an on-demand update on process stats */
	ROGUE_FWIF_FWCCB_CMD_UPDATE_STATS = 107U |
					    ROGUE_CMD_MAGIC_DWORD_SHIFTED,

	ROGUE_FWIF_FWCCB_CMD_CORE_CLK_RATE_CHANGE =
		108U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
	ROGUE_FWIF_FWCCB_CMD_REQUEST_GPU_RESTART =
		109U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,

	/* Notifies host of a FW pagefault */
	ROGUE_FWIF_FWCCB_CMD_CONTEXT_FW_PF_NOTIFICATION =
		112U | ROGUE_CMD_MAGIC_DWORD_SHIFTED,
};

enum rogue_fwif_fwccb_cmd_update_stats_type {
	/*
	 * PVRSRVStatsUpdateRenderContextStats should increase the value of the
	 * ui32TotalNumPartialRenders stat
	 */
	ROGUE_FWIF_FWCCB_CMD_UPDATE_NUM_PARTIAL_RENDERS = 1,
	/*
	 * PVRSRVStatsUpdateRenderContextStats should increase the value of the
	 * ui32TotalNumOutOfMemory stat
	 */
	ROGUE_FWIF_FWCCB_CMD_UPDATE_NUM_OUT_OF_MEMORY,
	/*
	 * PVRSRVStatsUpdateRenderContextStats should increase the value of the
	 * ui32NumGeomStores stat
	 */
	ROGUE_FWIF_FWCCB_CMD_UPDATE_NUM_GEOM_STORES,
	/*
	 * PVRSRVStatsUpdateRenderContextStats should increase the value of the
	 * ui32NumFragStores stat
	 */
	ROGUE_FWIF_FWCCB_CMD_UPDATE_NUM_FRAG_STORES,
	/*
	 * PVRSRVStatsUpdateRenderContextStats should increase the value of the
	 * ui32NumCDMStores stat
	 */
	ROGUE_FWIF_FWCCB_CMD_UPDATE_NUM_CDM_STORES,
	/*
	 * PVRSRVStatsUpdateRenderContextStats should increase the value of the
	 * ui32NumTDMStores stat
	 */
	ROGUE_FWIF_FWCCB_CMD_UPDATE_NUM_TDM_STORES
};

struct rogue_fwif_fwccb_cmd_update_stats_data {
	/* Element to update */
	enum rogue_fwif_fwccb_cmd_update_stats_type element_to_update;
	/* The pid of the process whose stats are being updated */
	u32 pid_owner;
	/* Adjustment to be made to the statistic */
	s32 adjustment_value;
};

struct rogue_fwif_fwccb_cmd_core_clk_rate_change_data {
	u32 core_clk_rate;
} __aligned(8);

struct rogue_fwif_fwccb_cmd {
	/* Command type */
	enum rogue_fwif_fwccb_cmd_type cmd_type;
	/* Compatibility and other flags */
	u32 fwccb_flags;

	union {
		/* Data for Z/S-Buffer on-demand (un)backing*/
		struct rogue_fwif_fwccb_cmd_zsbuffer_backing_data
			cmd_zs_buffer_backing;
		/* Data for on-demand freelist grow/shrink */
		struct rogue_fwif_fwccb_cmd_freelist_gs_data cmd_free_list_gs;
		/* Data for freelists reconstruction */
		struct rogue_fwif_fwccb_cmd_freelists_reconstruction_data
			cmd_freelists_reconstruction;
		/* Data for context reset notification */
		struct rogue_fwif_fwccb_cmd_context_reset_data
			cmd_context_reset_notification;
		/* Data for updating process stats */
		struct rogue_fwif_fwccb_cmd_update_stats_data
			cmd_update_stats_data;
		struct rogue_fwif_fwccb_cmd_core_clk_rate_change_data
			cmd_core_clk_rate_change;
		struct rogue_fwif_fwccb_cmd_fw_pagefault_data cmd_fw_pagefault;
	} cmd_data __aligned(8);
} __aligned(8);

PVR_FW_STRUCT_SIZE_ASSERT(struct rogue_fwif_fwccb_cmd);

/*
 ******************************************************************************
 * Workload estimation Firmware CCB command structure for ROGUE
 ******************************************************************************
 */
struct rogue_fwif_workest_fwccb_cmd {
	/* Index for return data array */
	u16 return_data_index;
	/* The cycles the workload took on the hardware */
	u32 cycles_taken;
};

/*
 ******************************************************************************
 * Client CCB commands for ROGUE
 ******************************************************************************
 */

/*
 * Required memory alignment for 64-bit variables accessible by Meta
 * (The gcc meta aligns 64-bit variables to 64-bit; therefore, memory shared
 * between the host and meta that contains 64-bit variables has to maintain
 * this alignment)
 */
#define ROGUE_FWIF_FWALLOC_ALIGN sizeof(u64)

#define ROGUE_CCB_TYPE_TASK BIT(15)
#define ROGUE_CCB_FWALLOC_ALIGN(size)                \
	(((size) + (ROGUE_FWIF_FWALLOC_ALIGN - 1)) & \
	 ~(ROGUE_FWIF_FWALLOC_ALIGN - 1))

#define ROGUE_FWIF_CCB_CMD_TYPE_GEOM \
	(201U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)
#define ROGUE_FWIF_CCB_CMD_TYPE_TQ_3D \
	(202U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)
#define ROGUE_FWIF_CCB_CMD_TYPE_FRAG \
	(203U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)
#define ROGUE_FWIF_CCB_CMD_TYPE_FRAG_PR \
	(204U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)
#define ROGUE_FWIF_CCB_CMD_TYPE_CDM \
	(205U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)
#define ROGUE_FWIF_CCB_CMD_TYPE_TQ_TDM \
	(206U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)
#define ROGUE_FWIF_CCB_CMD_TYPE_FBSC_INVALIDATE \
	(207U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)
#define ROGUE_FWIF_CCB_CMD_TYPE_TQ_2D \
	(208U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)
#define ROGUE_FWIF_CCB_CMD_TYPE_PRE_TIMESTAMP \
	(209U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)
#define ROGUE_FWIF_CCB_CMD_TYPE_NULL \
	(210U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)
#define ROGUE_FWIF_CCB_CMD_TYPE_ABORT \
	(211U | ROGUE_CMD_MAGIC_DWORD_SHIFTED | ROGUE_CCB_TYPE_TASK)

/* Leave a gap between CCB specific commands and generic commands */
#define ROGUE_FWIF_CCB_CMD_TYPE_FENCE (212U | ROGUE_CMD_MAGIC_DWORD_SHIFTED)
#define ROGUE_FWIF_CCB_CMD_TYPE_UPDATE (213U | ROGUE_CMD_MAGIC_DWORD_SHIFTED)
#define ROGUE_FWIF_CCB_CMD_TYPE_RMW_UPDATE \
	(214U | ROGUE_CMD_MAGIC_DWORD_SHIFTED)
#define ROGUE_FWIF_CCB_CMD_TYPE_FENCE_PR (215U | ROGUE_CMD_MAGIC_DWORD_SHIFTED)
#define ROGUE_FWIF_CCB_CMD_TYPE_PRIORITY (216U | ROGUE_CMD_MAGIC_DWORD_SHIFTED)
/*
 * Pre and Post timestamp commands are supposed to sandwich the DM cmd. The
 * padding code with the CCB wrap upsets the FW if we don't have the task type
 * bit cleared for POST_TIMESTAMPs. That's why we have 2 different cmd types.
 */
#define ROGUE_FWIF_CCB_CMD_TYPE_POST_TIMESTAMP \
	(217U | ROGUE_CMD_MAGIC_DWORD_SHIFTED)
#define ROGUE_FWIF_CCB_CMD_TYPE_UNFENCED_UPDATE \
	(218U | ROGUE_CMD_MAGIC_DWORD_SHIFTED)
#define ROGUE_FWIF_CCB_CMD_TYPE_UNFENCED_RMW_UPDATE \
	(219U | ROGUE_CMD_MAGIC_DWORD_SHIFTED)

#define ROGUE_FWIF_CCB_CMD_TYPE_PADDING (221U | ROGUE_CMD_MAGIC_DWORD_SHIFTED)

struct rogue_fwif_workest_kick_data {
	/* Index for the KM Workload estimation return data array */
	u16 return_data_index __aligned(8);
	/* Predicted time taken to do the work in cycles */
	u32 cycles_prediction __aligned(8);
	/* Deadline for the workload */
	aligned_u64 deadline;
};

struct rogue_fwif_ccb_cmd_header {
	u32 cmd_type;
	u32 cmd_size;
	/*
	 * external job reference - provided by client and used in debug for
	 * tracking submitted work
	 */
	u32 ext_job_ref;
	/*
	 * internal job reference - generated by services and used in debug for
	 * tracking submitted work
	 */
	u32 int_job_ref;
	/* Workload Estimation - Workload Estimation Data */
	struct rogue_fwif_workest_kick_data work_est_kick_data __aligned(8);
};

/*
 ******************************************************************************
 * Client CCB commands which are only required by the kernel
 ******************************************************************************
 */
struct rogue_fwif_cmd_priority {
	s32 priority;
};

/*
 ******************************************************************************
 * Signature and Checksums Buffer
 ******************************************************************************
 */
struct rogue_fwif_sigbuf_ctl {
	/* Ptr to Signature Buffer memory */
	u32 buffer_fw_addr;
	/* Amount of space left for storing regs in the buffer */
	u32 left_size_in_regs;
} __aligned(8);

struct rogue_fwif_counter_dump_ctl {
	/* Ptr to counter dump buffer */
	u32 buffer_fw_addr;
	/* Amount of space for storing in the buffer */
	u32 size_in_dwords;
} __aligned(8);

struct rogue_fwif_firmware_gcov_ctl {
	/* Ptr to firmware gcov buffer */
	u32 buffer_fw_addr;
	/* Amount of space for storing in the buffer */
	u32 size;
} __aligned(8);

/*
 *****************************************************************************
 * ROGUE Compatibility checks
 *****************************************************************************
 */

/*
 * WARNING: Whenever the layout of ROGUE_FWIF_COMPCHECKS_BVNC changes, the
 * following define should be increased by 1 to indicate to the compatibility
 * logic that layout has changed.
 */
#define ROGUE_FWIF_COMPCHECKS_LAYOUT_VERSION 3

struct rogue_fwif_compchecks_bvnc {
	/* WARNING: This field must be defined as first one in this structure */
	u32 layout_version;
	aligned_u64 bvnc;
} __aligned(8);

struct rogue_fwif_init_options {
	u8 os_count_support;
	u8 padding[7];
} __aligned(8);

#define ROGUE_FWIF_COMPCHECKS_BVNC_DECLARE_AND_INIT(name) \
	struct rogue_fwif_compchecks_bvnc(name) = {       \
		ROGUE_FWIF_COMPCHECKS_LAYOUT_VERSION,     \
		0,                                        \
	}

static inline void rogue_fwif_compchecks_bvnc_init(struct rogue_fwif_compchecks_bvnc *compchecks)
{
	compchecks->layout_version = ROGUE_FWIF_COMPCHECKS_LAYOUT_VERSION;
	compchecks->bvnc = 0;
}

struct rogue_fwif_compchecks {
	/* hardware BVNC (from the ROGUE registers) */
	struct rogue_fwif_compchecks_bvnc hw_bvnc;
	/* firmware BVNC */
	struct rogue_fwif_compchecks_bvnc fw_bvnc;
	/* identifier of the FW processor version */
	u32 fw_processor_version;
	/* software DDK version */
	u32 ddk_version;
	/* software DDK build no. */
	u32 ddk_build;
	/* build options bit-field */
	u32 build_options;
	/* initialisation options bit-field */
	struct rogue_fwif_init_options init_options;
	/* Information is valid */
	bool updated __aligned(4);
	u32 padding;
} __aligned(8);

/*
 ******************************************************************************
 * Updated configuration post FW data init.
 ******************************************************************************
 */
struct rogue_fwif_runtime_cfg {
	/* APM latency in ms before signalling IDLE to the host */
	u32 active_pm_latency_ms;
	/* Compatibility and other flags */
	u32 runtime_cfg_flags;
	/*
	 * If set, APM latency does not reset to system default each GPU power
	 * transition
	 */
	bool active_pm_latency_persistant __aligned(4);
	/* Core clock speed, currently only used to calculate timer ticks */
	u32 core_clock_speed;
	/* Last number of dusts change requested by the host */
	u32 default_dusts_num_init;
	/* Periodic Hardware Reset configuration values */
	u32 phr_mode;
	/* New number of milliseconds C/S is allowed to last */
	u32 hcs_deadline_ms;
	/* The watchdog period in microseconds */
	u32 wdg_period_us;
	/* Array of priorities per OS */
	u32 osid_priority[ROGUE_FW_MAX_NUM_OS];
	/* On-demand allocated HWPerf buffer address, to be passed to the FW */
	u32 hwperf_buf_fw_addr;

	bool padding __aligned(4);
};

/*
 *****************************************************************************
 * Control data for ROGUE
 *****************************************************************************
 */

#define ROGUE_FWIF_HWR_DEBUG_DUMP_ALL (99999U)

enum rogue_fwif_tpu_dm {
	ROGUE_FWIF_TPU_DM_PDM = 0,
	ROGUE_FWIF_TPU_DM_VDM = 1,
	ROGUE_FWIF_TPU_DM_CDM = 2,
	ROGUE_FWIF_TPU_DM_TDM = 3,
	ROGUE_FWIF_TPU_DM_LAST
};

enum rogue_fwif_gpio_val_mode {
	/* No GPIO validation */
	ROGUE_FWIF_GPIO_VAL_OFF = 0,
	/*
	 * Simple test case that initiates by sending data via the GPIO and then
	 * sends back any data received over the GPIO
	 */
	ROGUE_FWIF_GPIO_VAL_GENERAL = 1,
	/*
	 * More complex test case that writes and reads data across the entire
	 * GPIO AP address range.
	 */
	ROGUE_FWIF_GPIO_VAL_AP = 2,
	/* Validates the GPIO Testbench. */
	ROGUE_FWIF_GPIO_VAL_TESTBENCH = 5,
	/* Send and then receive each byte in the range 0-255. */
	ROGUE_FWIF_GPIO_VAL_LOOPBACK = 6,
	/* Send and then receive each power-of-2 byte in the range 0-255. */
	ROGUE_FWIF_GPIO_VAL_LOOPBACK_LITE = 7,
	ROGUE_FWIF_GPIO_VAL_LAST
};

enum fw_perf_conf {
	FW_PERF_CONF_NONE = 0,
	FW_PERF_CONF_ICACHE = 1,
	FW_PERF_CONF_DCACHE = 2,
	FW_PERF_CONF_JTLB_INSTR = 5,
	FW_PERF_CONF_INSTRUCTIONS = 6
};

enum fw_boot_stage {
	FW_BOOT_STAGE_TLB_INIT_FAILURE = -2,
	FW_BOOT_STAGE_NOT_AVAILABLE = -1,
	FW_BOOT_NOT_STARTED = 0,
	FW_BOOT_BLDR_STARTED = 1,
	FW_BOOT_CACHE_DONE,
	FW_BOOT_TLB_DONE,
	FW_BOOT_MAIN_STARTED,
	FW_BOOT_ALIGNCHECKS_DONE,
	FW_BOOT_INIT_DONE,
};

/*
 * Kernel CCB return slot responses. Usage of bit-fields instead of bare
 * integers allows FW to possibly pack-in several responses for each single kCCB
 * command.
 */
/* Command executed (return status from FW) */
#define ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED BIT(0)
/* A cleanup was requested but resource busy */
#define ROGUE_FWIF_KCCB_RTN_SLOT_CLEANUP_BUSY BIT(1)
/* Poll failed in FW for a HW operation to complete */
#define ROGUE_FWIF_KCCB_RTN_SLOT_POLL_FAILURE BIT(2)
/* Reset value of a kCCB return slot (set by host) */
#define ROGUE_FWIF_KCCB_RTN_SLOT_NO_RESPONSE 0x0U

struct rogue_fwif_connection_ctl {
	/* Fw-Os connection states */
	enum rogue_fwif_connection_fw_state connection_fw_state;
	enum rogue_fwif_connection_os_state connection_os_state;
	u32 alive_fw_token;
	u32 alive_os_token;
} __aligned(8);

struct rogue_fwif_osinit {
	/* Kernel CCB */
	u32 kernel_ccbctl_fw_addr;
	u32 kernel_ccb_fw_addr;
	u32 kernel_ccb_rtn_slots_fw_addr;

	/* Firmware CCB */
	u32 firmware_ccbctl_fw_addr;
	u32 firmware_ccb_fw_addr;

	/* Workload Estimation Firmware CCB */
	u32 work_est_firmware_ccbctl_fw_addr;
	u32 work_est_firmware_ccb_fw_addr;

	u32 rogue_fwif_hwr_info_buf_ctl_fw_addr;

	u32 hwr_debug_dump_limit;

	u32 fw_os_data_fw_addr;

	/* Compatibility checks to be populated by the Firmware */
	struct rogue_fwif_compchecks rogue_comp_checks;
} __aligned(8);

/* BVNC Features */
struct rogue_hwperf_bvnc_block {
	/* Counter block ID, see ROGUE_HWPERF_CNTBLK_ID */
	u16 block_id;

	/* Number of counters in this block type */
	u16 num_counters;

	/* Number of blocks of this type */
	u16 num_blocks;

	u16 reserved;
};

#define ROGUE_HWPERF_MAX_BVNC_LEN (24)

#define ROGUE_HWPERF_MAX_BVNC_BLOCK_LEN (16U)

/* BVNC Features */
struct rogue_hwperf_bvnc {
	/* BVNC string */
	char bvnc_string[ROGUE_HWPERF_MAX_BVNC_LEN];
	/* See ROGUE_HWPERF_FEATURE_FLAGS */
	u32 bvnc_km_feature_flags;
	/* Number of blocks described in aBvncBlocks */
	u16 num_bvnc_blocks;
	/* Number of GPU cores present */
	u16 bvnc_gpu_cores;
	/* Supported Performance Blocks for BVNC */
	struct rogue_hwperf_bvnc_block
		bvnc_blocks[ROGUE_HWPERF_MAX_BVNC_BLOCK_LEN];
};

PVR_FW_STRUCT_SIZE_ASSERT(struct rogue_hwperf_bvnc);

struct rogue_fwif_sysinit {
	/* Fault read address */
	aligned_u64 fault_phys_addr;

	/* PDS execution base */
	aligned_u64 pds_exec_base;
	/* UCS execution base */
	aligned_u64 usc_exec_base;
	/* FBCDC bindless texture state table base */
	aligned_u64 fbcdc_state_table_base;
	aligned_u64 fbcdc_large_state_table_base;
	/* Texture state base */
	aligned_u64 texture_heap_base;

	/* Event filter for Firmware events */
	u64 hw_perf_filter;

	aligned_u64 slc3_fence_dev_addr;

	u32 tpu_trilinear_frac_mask[ROGUE_FWIF_TPU_DM_LAST] __aligned(8);

	/* Signature and Checksum Buffers for DMs */
	struct rogue_fwif_sigbuf_ctl sigbuf_ctl[PVR_FWIF_DM_MAX];

	struct rogue_fwif_pdvfs_opp pdvfs_opp_info;

	struct rogue_fwif_dma_addr coremem_data_store;

	struct rogue_fwif_counter_dump_ctl counter_dump_ctl;

	u32 filter_flags;

	u32 runtime_cfg_fw_addr;

	u32 trace_buf_ctl_fw_addr;
	u32 fw_sys_data_fw_addr;

	u32 gpu_util_fw_cb_ctl_fw_addr;
	u32 reg_cfg_fw_addr;
	u32 hwperf_ctl_fw_addr;

	u32 align_checks;

	/* Core clock speed at FW boot time */
	u32 initial_core_clock_speed;

	/* APM latency in ms before signalling IDLE to the host */
	u32 active_pm_latency_ms;

	/* Flag to be set by the Firmware after successful start */
	bool firmware_started __aligned(4);

	/* Host/FW Trace synchronisation Partition Marker */
	u32 marker_val;

	/* Firmware initialization complete time */
	u32 firmware_started_timestamp;

	u32 jones_disable_mask;

	/* Firmware performance counter config */
	enum fw_perf_conf firmware_perf;

	/*
	 * FW Pointer to memory containing core clock rate in Hz.
	 * Firmware (PDVFS) updates the memory when running on non primary FW
	 * thread to communicate to host driver.
	 */
	u32 core_clock_rate_fw_addr;

	enum rogue_fwif_gpio_val_mode gpio_validation_mode;

	/* Used in HWPerf for decoding BVNC Features */
	struct rogue_hwperf_bvnc bvnc_km_feature_flags;

	/* Value to write into ROGUE_CR_TFBC_COMPRESSION_CONTROL */
	u32 tfbc_compression_control;
} __aligned(8);

/*
 *****************************************************************************
 * Timer correlation shared data and defines
 *****************************************************************************
 */

struct rogue_fwif_time_corr {
	aligned_u64 os_timestamp;
	aligned_u64 os_mono_timestamp;
	aligned_u64 cr_timestamp;

	/*
	 * Utility variable used to convert CR timer deltas to OS timer deltas
	 * (nS), where the deltas are relative to the timestamps above:
	 * deltaOS = (deltaCR * K) >> decimal_shift, see full explanation below
	 */
	aligned_u64 cr_delta_to_os_delta_kns;

	u32 core_clock_speed;
	u32 reserved;
} __aligned(8);

/*
 * The following macros are used to help converting FW timestamps to the Host
 * time domain. On the FW the ROGUE_CR_TIMER counter is used to keep track of
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

#define ROGUE_FWIF_CRDELTA_TO_OSDELTA_ACCURACY_SHIFT (20)

#define ROGUE_FWIF_GET_DELTA_OSTIME_NS(delta_cr, k) \
	(((delta_cr) * (k)) >> ROGUE_FWIF_CRDELTA_TO_OSDELTA_ACCURACY_SHIFT)

/*
 ******************************************************************************
 * GPU Utilisation
 ******************************************************************************
 */

/* See rogue_common.h for a list of GPU states */
#define ROGUE_FWIF_GPU_UTIL_TIME_MASK \
	(0xFFFFFFFFFFFFFFFFull & ~ROGUE_FWIF_GPU_UTIL_STATE_MASK)

#define ROGUE_FWIF_GPU_UTIL_GET_TIME(word) \
	((word)(&ROGUE_FWIF_GPU_UTIL_TIME_MASK))
#define ROGUE_FWIF_GPU_UTIL_GET_STATE(word) \
	((word)(&ROGUE_FWIF_GPU_UTIL_STATE_MASK))

/*
 * The OS timestamps computed by the FW are approximations of the real time,
 * which means they could be slightly behind or ahead the real timer on the
 * Host. In some cases we can perform subtractions between FW approximated
 * timestamps and real OS timestamps, so we need a form of protection against
 * negative results if for instance the FW one is a bit ahead of time.
 */
#define ROGUE_FWIF_GPU_UTIL_GET_PERIOD(newtime, oldtime) \
	(((newtime) > (oldtime)) ? ((newtime) - (oldtime)) : 0U)

#define ROGUE_FWIF_GPU_UTIL_MAKE_WORD(time, state) \
	(ROGUE_FWIF_GPU_UTIL_GET_TIME(time) |      \
	 ROGUE_FWIF_GPU_UTIL_GET_STATE(state))

/*
 * The timer correlation array must be big enough to ensure old entries won't be
 * overwritten before all the HWPerf events linked to those entries are
 * processed by the MISR. The update frequency of this array depends on how fast
 * the system can change state (basically how small the APM latency is) and
 * perform DVFS transitions.
 *
 * The minimum size is 2 (not 1) to avoid race conditions between the FW reading
 * an entry while the Host is updating it. With 2 entries in the worst case the
 * FW will read old data, which is still quite ok if the Host is updating the
 * timer correlation at that time.
 */
#define ROGUE_FWIF_TIME_CORR_ARRAY_SIZE 256U
#define ROGUE_FWIF_TIME_CORR_CURR_INDEX(seqcount) \
	((seqcount) % ROGUE_FWIF_TIME_CORR_ARRAY_SIZE)

/* Make sure the timer correlation array size is a power of 2 */
static_assert((ROGUE_FWIF_TIME_CORR_ARRAY_SIZE &
	       (ROGUE_FWIF_TIME_CORR_ARRAY_SIZE - 1U)) == 0U,
	      "ROGUE_FWIF_TIME_CORR_ARRAY_SIZE must be a power of two");

struct rogue_fwif_gpu_util_fwcb {
	struct rogue_fwif_time_corr time_corr[ROGUE_FWIF_TIME_CORR_ARRAY_SIZE];
	u32 time_corr_seq_count;

	/* Compatibility and other flags */
	u32 gpu_util_flags;

	/* Last GPU state + OS time of the last state update */
	aligned_u64 last_word;

	/* Counters for the amount of time the GPU was active/idle/blocked */
	aligned_u64 stats_counters[PVR_FWIF_GPU_UTIL_STATE_NUM];
} __aligned(8);

struct rogue_fwif_rta_ctl {
	/* Render number */
	u32 render_target_index;
	/* index in RTA */
	u32 current_render_target;
	/* total active RTs */
	u32 active_render_targets;
	/* total active RTs from the first TA kick, for OOM */
	u32 cumul_active_render_targets;
	/* Array of valid RT indices */
	u32 valid_render_targets_fw_addr;
	/* Array of number of occurred partial renders per render target */
	u32 rta_num_partial_renders_fw_addr;
	/* Number of render targets in the array */
	u32 max_rts;
	/* Compatibility and other flags */
	u32 rta_ctl_flags;
} __aligned(8);

struct rogue_fwif_freelist {
	aligned_u64 freelist_dev_addr;
	aligned_u64 current_dev_addr;
	u32 current_stack_top;
	u32 max_pages;
	u32 grow_pages;
	/* HW pages */
	u32 current_pages;
	u32 allocated_page_count;
	u32 allocated_mmu_page_count;
	u32 freelist_id;

	bool grow_pending __aligned(4);
	/* Pages that should be used only when OOM is reached */
	u32 ready_pages;
	/* Compatibility and other flags */
	u32 freelist_flags;
	/* PM Global PB on which Freelist is loaded */
	u32 pm_global_pb;
	u32 padding;
} __aligned(8);

/*
 ******************************************************************************
 * HWRTData
 ******************************************************************************
 */

/* HWRTData flags */
/* Deprecated flags 1:0 */
#define HWRTDATA_HAS_LAST_GEOM BIT(2)
#define HWRTDATA_PARTIAL_RENDERED BIT(3)
#define HWRTDATA_DISABLE_TILE_REORDERING BIT(4)
#define HWRTDATA_NEED_BRN65101_BLIT BIT(5)
#define HWRTDATA_FIRST_BRN65101_STRIP BIT(6)
#define HWRTDATA_NEED_BRN67182_2ND_RENDER BIT(7)

enum rogue_fwif_rtdata_state {
	ROGUE_FWIF_RTDATA_STATE_NONE = 0,
	ROGUE_FWIF_RTDATA_STATE_KICK_GEOM,
	ROGUE_FWIF_RTDATA_STATE_KICK_GEOM_FIRST,
	ROGUE_FWIF_RTDATA_STATE_GEOM_FINISHED,
	ROGUE_FWIF_RTDATA_STATE_KICK_FRAG,
	ROGUE_FWIF_RTDATA_STATE_FRAG_FINISHED,
	ROGUE_FWIF_RTDATA_STATE_FRAG_CONTEXT_STORED,
	ROGUE_FWIF_RTDATA_STATE_GEOM_OUTOFMEM,
	ROGUE_FWIF_RTDATA_STATE_PARTIALRENDERFINISHED,
	/*
	 * In case of HWR, we can't set the RTDATA state to NONE, as this will
	 * cause any TA to become a first TA. To ensure all related TA's are
	 * skipped, we use the HWR state
	 */
	ROGUE_FWIF_RTDATA_STATE_HWR,
	ROGUE_FWIF_RTDATA_STATE_UNKNOWN = 0x7FFFFFFFU
};

struct rogue_fwif_hwrtdata_common {
	bool geom_caches_need_zeroing __aligned(4);

	u32 screen_pixel_max;
	aligned_u64 multi_sample_ctl;
	u64 flipped_multi_sample_ctl;
	u32 tpc_stride;
	u32 tpc_size;
	u32 te_screen;
	u32 mtile_stride;
	u32 teaa;
	u32 te_mtile1;
	u32 te_mtile2;
	u32 isp_merge_lower_x;
	u32 isp_merge_lower_y;
	u32 isp_merge_upper_x;
	u32 isp_merge_upper_y;
	u32 isp_merge_scale_x;
	u32 isp_merge_scale_y;
	u32 rgn_header_size;
	u32 isp_mtile_size;
	u32 padding;
} __aligned(8);

struct rogue_fwif_hwrtdata {
	/* MList Data Store */
	aligned_u64 pm_mlist_dev_addr;

	aligned_u64 vce_cat_base[4];
	aligned_u64 vce_last_cat_base[4];
	aligned_u64 te_cat_base[4];
	aligned_u64 te_last_cat_base[4];
	aligned_u64 alist_cat_base;
	aligned_u64 alist_last_cat_base;

	aligned_u64 pm_alist_stack_pointer;
	u32 pm_mlist_stack_pointer;

	u32 hwrt_data_common_fw_addr;

	u32 hwrt_data_flags;
	enum rogue_fwif_rtdata_state state;

	u32 freelists_fw_addr[MAX_FREELISTS_SIZE] __aligned(8);
	u32 freelist_hwr_snapshot[MAX_FREELISTS_SIZE];

	aligned_u64 vheap_table_dev_addr;

	struct rogue_fwif_rta_ctl rta_ctl;

	aligned_u64 tail_ptrs_dev_addr;
	aligned_u64 macrotile_array_dev_addr;
	aligned_u64 rgn_header_dev_addr;
	aligned_u64 rtc_dev_addr;

	u32 owner_geom_not_used_by_host __aligned(8);

	bool geom_caches_need_zeroing __aligned(4);

	struct rogue_fwif_cleanup_ctl cleanup_state __aligned(64);
} __aligned(8);

/*
 ******************************************************************************
 * Sync checkpoints
 ******************************************************************************
 */

#define PVR_SYNC_CHECKPOINT_UNDEF 0x000
#define PVR_SYNC_CHECKPOINT_ACTIVE 0xac1     /* Checkpoint has not signaled. */
#define PVR_SYNC_CHECKPOINT_SIGNALED 0x519   /* Checkpoint has signaled. */
#define PVR_SYNC_CHECKPOINT_ERRORED 0xeff    /* Checkpoint has been errored. */

#include "pvr_rogue_fwif_check.h"

#endif /* PVR_ROGUE_FWIF_H */
