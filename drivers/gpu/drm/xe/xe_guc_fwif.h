/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GUC_FWIF_H
#define _XE_GUC_FWIF_H

#include <linux/bits.h>

#include "abi/guc_klvs_abi.h"

#define G2H_LEN_DW_SCHED_CONTEXT_MODE_SET	4
#define G2H_LEN_DW_DEREGISTER_CONTEXT		3
#define G2H_LEN_DW_TLB_INVALIDATE		3

#define GUC_CONTEXT_DISABLE		0
#define GUC_CONTEXT_ENABLE		1

#define GUC_CLIENT_PRIORITY_KMD_HIGH	0
#define GUC_CLIENT_PRIORITY_HIGH	1
#define GUC_CLIENT_PRIORITY_KMD_NORMAL	2
#define GUC_CLIENT_PRIORITY_NORMAL	3
#define GUC_CLIENT_PRIORITY_NUM		4

#define GUC_RENDER_ENGINE		0
#define GUC_VIDEO_ENGINE		1
#define GUC_BLITTER_ENGINE		2
#define GUC_VIDEOENHANCE_ENGINE		3
#define GUC_VIDEO_ENGINE2		4
#define GUC_MAX_ENGINES_NUM		(GUC_VIDEO_ENGINE2 + 1)

#define GUC_RENDER_CLASS		0
#define GUC_VIDEO_CLASS			1
#define GUC_VIDEOENHANCE_CLASS		2
#define GUC_BLITTER_CLASS		3
#define GUC_COMPUTE_CLASS		4
#define GUC_GSC_OTHER_CLASS		5
#define GUC_LAST_ENGINE_CLASS		GUC_GSC_OTHER_CLASS
#define GUC_MAX_ENGINE_CLASSES		16
#define GUC_MAX_INSTANCES_PER_CLASS	32

/* Helper for context registration H2G */
struct guc_ctxt_registration_info {
	u32 flags;
	u32 context_idx;
	u32 engine_class;
	u32 engine_submit_mask;
	u32 wq_desc_lo;
	u32 wq_desc_hi;
	u32 wq_base_lo;
	u32 wq_base_hi;
	u32 wq_size;
	u32 hwlrca_lo;
	u32 hwlrca_hi;
};
#define CONTEXT_REGISTRATION_FLAG_KMD	BIT(0)

/* 32-bit KLV structure as used by policy updates and others */
struct guc_klv_generic_dw_t {
	u32 kl;
	u32 value;
} __packed;

/* Format of the UPDATE_CONTEXT_POLICIES H2G data packet */
struct guc_update_exec_queue_policy_header {
	u32 action;
	u32 guc_id;
} __packed;

struct guc_update_exec_queue_policy {
	struct guc_update_exec_queue_policy_header header;
	struct guc_klv_generic_dw_t klv[GUC_CONTEXT_POLICIES_KLV_NUM_IDS];
} __packed;

/* GUC_CTL_* - Parameters for loading the GuC */
#define GUC_CTL_LOG_PARAMS		0
#define   GUC_LOG_VALID			BIT(0)
#define   GUC_LOG_NOTIFY_ON_HALF_FULL	BIT(1)
#define   GUC_LOG_CAPTURE_ALLOC_UNITS	BIT(2)
#define   GUC_LOG_LOG_ALLOC_UNITS	BIT(3)
#define   GUC_LOG_CRASH_SHIFT		4
#define   GUC_LOG_CRASH_MASK		(0x3 << GUC_LOG_CRASH_SHIFT)
#define   GUC_LOG_DEBUG_SHIFT		6
#define   GUC_LOG_DEBUG_MASK	        (0xF << GUC_LOG_DEBUG_SHIFT)
#define   GUC_LOG_CAPTURE_SHIFT		10
#define   GUC_LOG_CAPTURE_MASK	        (0x3 << GUC_LOG_CAPTURE_SHIFT)
#define   GUC_LOG_BUF_ADDR_SHIFT	12

#define GUC_CTL_WA			1
#define   GUC_WA_GAM_CREDITS		BIT(10)
#define   GUC_WA_DUAL_QUEUE		BIT(11)
#define   GUC_WA_RCS_RESET_BEFORE_RC6	BIT(13)
#define   GUC_WA_CONTEXT_ISOLATION	BIT(15)
#define   GUC_WA_PRE_PARSER		BIT(14)
#define   GUC_WA_HOLD_CCS_SWITCHOUT	BIT(17)
#define   GUC_WA_POLLCS			BIT(18)
#define   GUC_WA_RENDER_RST_RC6_EXIT	BIT(19)
#define   GUC_WA_RCS_REGS_IN_CCS_REGS_LIST	BIT(21)

#define GUC_CTL_FEATURE			2
#define   GUC_CTL_ENABLE_SLPC		BIT(2)
#define   GUC_CTL_DISABLE_SCHEDULER	BIT(14)

#define GUC_CTL_DEBUG			3
#define   GUC_LOG_VERBOSITY_SHIFT	0
#define   GUC_LOG_VERBOSITY_LOW		(0 << GUC_LOG_VERBOSITY_SHIFT)
#define   GUC_LOG_VERBOSITY_MED		(1 << GUC_LOG_VERBOSITY_SHIFT)
#define   GUC_LOG_VERBOSITY_HIGH	(2 << GUC_LOG_VERBOSITY_SHIFT)
#define   GUC_LOG_VERBOSITY_ULTRA	(3 << GUC_LOG_VERBOSITY_SHIFT)
#define	  GUC_LOG_VERBOSITY_MIN		0
#define	  GUC_LOG_VERBOSITY_MAX		3
#define	  GUC_LOG_VERBOSITY_MASK	0x0000000f
#define	  GUC_LOG_DESTINATION_MASK	(3 << 4)
#define   GUC_LOG_DISABLED		(1 << 6)
#define   GUC_PROFILE_ENABLED		(1 << 7)

#define GUC_CTL_ADS			4
#define   GUC_ADS_ADDR_SHIFT		1
#define   GUC_ADS_ADDR_MASK		(0xFFFFF << GUC_ADS_ADDR_SHIFT)

#define GUC_CTL_DEVID			5

#define GUC_CTL_MAX_DWORDS		14

/* Scheduling policy settings */

#define GLOBAL_POLICY_MAX_NUM_WI 15

/* Don't reset an engine upon preemption failure */
#define GLOBAL_POLICY_DISABLE_ENGINE_RESET				BIT(0)

#define GLOBAL_POLICY_DEFAULT_DPC_PROMOTE_TIME_US 500000

struct guc_policies {
	u32 submission_queue_depth[GUC_MAX_ENGINE_CLASSES];
	/*
	 * In micro seconds. How much time to allow before DPC processing is
	 * called back via interrupt (to prevent DPC queue drain starving).
	 * Typically 1000s of micro seconds (example only, not granularity).
	 */
	u32 dpc_promote_time;

	/* Must be set to take these new values. */
	u32 is_valid;

	/*
	 * Max number of WIs to process per call. A large value may keep CS
	 * idle.
	 */
	u32 max_num_work_items;

	u32 global_flags;
	u32 reserved[4];
} __packed;

/* GuC MMIO reg state struct */
struct guc_mmio_reg {
	u32 offset;
	u32 value;
	u32 flags;
	u32 mask;
#define GUC_REGSET_MASKED		BIT(0)
#define GUC_REGSET_MASKED_WITH_VALUE	BIT(2)
#define GUC_REGSET_RESTORE_ONLY		BIT(3)
} __packed;

/* GuC register sets */
struct guc_mmio_reg_set {
	u32 address;
	u16 count;
	u16 reserved;
} __packed;

/* Generic GT SysInfo data types */
#define GUC_GENERIC_GT_SYSINFO_SLICE_ENABLED		0
#define GUC_GENERIC_GT_SYSINFO_VDBOX_SFC_SUPPORT_MASK	1
#define GUC_GENERIC_GT_SYSINFO_DOORBELL_COUNT_PER_SQIDI	2
#define GUC_GENERIC_GT_SYSINFO_MAX			16

/* HW info */
struct guc_gt_system_info {
	u8 mapping_table[GUC_MAX_ENGINE_CLASSES][GUC_MAX_INSTANCES_PER_CLASS];
	u32 engine_enabled_masks[GUC_MAX_ENGINE_CLASSES];
	u32 generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_MAX];
} __packed;

enum {
	GUC_CAPTURE_LIST_INDEX_PF = 0,
	GUC_CAPTURE_LIST_INDEX_VF = 1,
	GUC_CAPTURE_LIST_INDEX_MAX = 2,
};

/* GuC Additional Data Struct */
struct guc_ads {
	struct guc_mmio_reg_set reg_state_list[GUC_MAX_ENGINE_CLASSES][GUC_MAX_INSTANCES_PER_CLASS];
	u32 reserved0;
	u32 scheduler_policies;
	u32 gt_system_info;
	u32 reserved1;
	u32 control_data;
	u32 golden_context_lrca[GUC_MAX_ENGINE_CLASSES];
	u32 eng_state_size[GUC_MAX_ENGINE_CLASSES];
	u32 private_data;
	u32 um_init_data;
	u32 capture_instance[GUC_CAPTURE_LIST_INDEX_MAX][GUC_MAX_ENGINE_CLASSES];
	u32 capture_class[GUC_CAPTURE_LIST_INDEX_MAX][GUC_MAX_ENGINE_CLASSES];
	u32 capture_global[GUC_CAPTURE_LIST_INDEX_MAX];
	u32 reserved[14];
} __packed;

/* Engine usage stats */
struct guc_engine_usage_record {
	u32 current_context_index;
	u32 last_switch_in_stamp;
	u32 reserved0;
	u32 total_runtime;
	u32 reserved1[4];
} __packed;

struct guc_engine_usage {
	struct guc_engine_usage_record engines[GUC_MAX_ENGINE_CLASSES][GUC_MAX_INSTANCES_PER_CLASS];
} __packed;

/* This action will be programmed in C1BC - SOFT_SCRATCH_15_REG */
enum xe_guc_recv_message {
	XE_GUC_RECV_MSG_CRASH_DUMP_POSTED = BIT(1),
	XE_GUC_RECV_MSG_EXCEPTION = BIT(30),
};

/* Page fault structures */
struct access_counter_desc {
	u32 dw0;
#define ACCESS_COUNTER_TYPE	BIT(0)
#define ACCESS_COUNTER_SUBG_LO	GENMASK(31, 1)

	u32 dw1;
#define ACCESS_COUNTER_SUBG_HI	BIT(0)
#define ACCESS_COUNTER_RSVD0	GENMASK(2, 1)
#define ACCESS_COUNTER_ENG_INSTANCE	GENMASK(8, 3)
#define ACCESS_COUNTER_ENG_CLASS	GENMASK(11, 9)
#define ACCESS_COUNTER_ASID	GENMASK(31, 12)

	u32 dw2;
#define ACCESS_COUNTER_VFID	GENMASK(5, 0)
#define ACCESS_COUNTER_RSVD1	GENMASK(7, 6)
#define ACCESS_COUNTER_GRANULARITY	GENMASK(10, 8)
#define ACCESS_COUNTER_RSVD2	GENMASK(16, 11)
#define ACCESS_COUNTER_VIRTUAL_ADDR_RANGE_LO	GENMASK(31, 17)

	u32 dw3;
#define ACCESS_COUNTER_VIRTUAL_ADDR_RANGE_HI	GENMASK(31, 0)
} __packed;

enum guc_um_queue_type {
	GUC_UM_HW_QUEUE_PAGE_FAULT = 0,
	GUC_UM_HW_QUEUE_PAGE_FAULT_RESPONSE,
	GUC_UM_HW_QUEUE_ACCESS_COUNTER,
	GUC_UM_HW_QUEUE_MAX
};

struct guc_um_queue_params {
	u64 base_dpa;
	u32 base_ggtt_address;
	u32 size_in_bytes;
	u32 rsvd[4];
} __packed;

struct guc_um_init_params {
	u64 page_response_timeout_in_us;
	u32 rsvd[6];
	struct guc_um_queue_params queue_params[GUC_UM_HW_QUEUE_MAX];
} __packed;

enum xe_guc_fault_reply_type {
	PFR_ACCESS = 0,
	PFR_ENGINE,
	PFR_VFID,
	PFR_ALL,
	PFR_INVALID
};

enum xe_guc_response_desc_type {
	TLB_INVALIDATION_DESC = 0,
	FAULT_RESPONSE_DESC
};

struct xe_guc_pagefault_desc {
	u32 dw0;
#define PFD_FAULT_LEVEL		GENMASK(2, 0)
#define PFD_SRC_ID		GENMASK(10, 3)
#define PFD_RSVD_0		GENMASK(17, 11)
#define XE2_PFD_TRVA_FAULT	BIT(18)
#define PFD_ENG_INSTANCE	GENMASK(24, 19)
#define PFD_ENG_CLASS		GENMASK(27, 25)
#define PFD_PDATA_LO		GENMASK(31, 28)

	u32 dw1;
#define PFD_PDATA_HI		GENMASK(11, 0)
#define PFD_PDATA_HI_SHIFT	4
#define PFD_ASID		GENMASK(31, 12)

	u32 dw2;
#define PFD_ACCESS_TYPE		GENMASK(1, 0)
#define PFD_FAULT_TYPE		GENMASK(3, 2)
#define PFD_VFID		GENMASK(9, 4)
#define PFD_RSVD_1		GENMASK(11, 10)
#define PFD_VIRTUAL_ADDR_LO	GENMASK(31, 12)
#define PFD_VIRTUAL_ADDR_LO_SHIFT 12

	u32 dw3;
#define PFD_VIRTUAL_ADDR_HI	GENMASK(31, 0)
#define PFD_VIRTUAL_ADDR_HI_SHIFT 32
} __packed;

struct xe_guc_pagefault_reply {
	u32 dw0;
#define PFR_VALID		BIT(0)
#define PFR_SUCCESS		BIT(1)
#define PFR_REPLY		GENMASK(4, 2)
#define PFR_RSVD_0		GENMASK(9, 5)
#define PFR_DESC_TYPE		GENMASK(11, 10)
#define PFR_ASID		GENMASK(31, 12)

	u32 dw1;
#define PFR_VFID		GENMASK(5, 0)
#define PFR_RSVD_1		BIT(6)
#define PFR_ENG_INSTANCE	GENMASK(12, 7)
#define PFR_ENG_CLASS		GENMASK(15, 13)
#define PFR_PDATA		GENMASK(31, 16)

	u32 dw2;
#define PFR_RSVD_2		GENMASK(31, 0)
} __packed;

struct xe_guc_acc_desc {
	u32 dw0;
#define ACC_TYPE	BIT(0)
#define ACC_TRIGGER	0
#define ACC_NOTIFY	1
#define ACC_SUBG_LO	GENMASK(31, 1)

	u32 dw1;
#define ACC_SUBG_HI	BIT(0)
#define ACC_RSVD0	GENMASK(2, 1)
#define ACC_ENG_INSTANCE	GENMASK(8, 3)
#define ACC_ENG_CLASS	GENMASK(11, 9)
#define ACC_ASID	GENMASK(31, 12)

	u32 dw2;
#define ACC_VFID	GENMASK(5, 0)
#define ACC_RSVD1	GENMASK(7, 6)
#define ACC_GRANULARITY	GENMASK(10, 8)
#define ACC_RSVD2	GENMASK(16, 11)
#define ACC_VIRTUAL_ADDR_RANGE_LO	GENMASK(31, 17)

	u32 dw3;
#define ACC_VIRTUAL_ADDR_RANGE_HI	GENMASK(31, 0)
} __packed;

#endif
