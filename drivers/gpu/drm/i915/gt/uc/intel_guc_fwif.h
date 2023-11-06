/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_FWIF_H
#define _INTEL_GUC_FWIF_H

#include <linux/bits.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include "gt/intel_engine_types.h"

#include "abi/guc_actions_abi.h"
#include "abi/guc_actions_slpc_abi.h"
#include "abi/guc_errors_abi.h"
#include "abi/guc_communication_mmio_abi.h"
#include "abi/guc_communication_ctb_abi.h"
#include "abi/guc_klvs_abi.h"
#include "abi/guc_messages_abi.h"

/* Payload length only i.e. don't include G2H header length */
#define G2H_LEN_DW_SCHED_CONTEXT_MODE_SET	2
#define G2H_LEN_DW_DEREGISTER_CONTEXT		1

#define GUC_CONTEXT_DISABLE		0
#define GUC_CONTEXT_ENABLE		1

#define GUC_CLIENT_PRIORITY_KMD_HIGH	0
#define GUC_CLIENT_PRIORITY_HIGH	1
#define GUC_CLIENT_PRIORITY_KMD_NORMAL	2
#define GUC_CLIENT_PRIORITY_NORMAL	3
#define GUC_CLIENT_PRIORITY_NUM		4

#define GUC_MAX_CONTEXT_ID		65535
#define	GUC_INVALID_CONTEXT_ID		GUC_MAX_CONTEXT_ID

#define GUC_RENDER_CLASS		0
#define GUC_VIDEO_CLASS			1
#define GUC_VIDEOENHANCE_CLASS		2
#define GUC_BLITTER_CLASS		3
#define GUC_COMPUTE_CLASS		4
#define GUC_GSC_OTHER_CLASS		5
#define GUC_LAST_ENGINE_CLASS		GUC_GSC_OTHER_CLASS
#define GUC_MAX_ENGINE_CLASSES		16
#define GUC_MAX_INSTANCES_PER_CLASS	32

#define GUC_DOORBELL_INVALID		256

/*
 * Work queue item header definitions
 *
 * Work queue is circular buffer used to submit complex (multi-lrc) submissions
 * to the GuC. A work queue item is an entry in the circular buffer.
 */
#define WQ_STATUS_ACTIVE		1
#define WQ_STATUS_SUSPENDED		2
#define WQ_STATUS_CMD_ERROR		3
#define WQ_STATUS_ENGINE_ID_NOT_USED	4
#define WQ_STATUS_SUSPENDED_FROM_RESET	5
#define WQ_TYPE_BATCH_BUF		0x1
#define WQ_TYPE_PSEUDO			0x2
#define WQ_TYPE_INORDER			0x3
#define WQ_TYPE_NOOP			0x4
#define WQ_TYPE_MULTI_LRC		0x5
#define WQ_TYPE_MASK			GENMASK(7, 0)
#define WQ_LEN_MASK			GENMASK(26, 16)

#define WQ_GUC_ID_MASK			GENMASK(15, 0)
#define WQ_RING_TAIL_MASK		GENMASK(28, 18)

#define GUC_STAGE_DESC_ATTR_ACTIVE	BIT(0)
#define GUC_STAGE_DESC_ATTR_PENDING_DB	BIT(1)
#define GUC_STAGE_DESC_ATTR_KERNEL	BIT(2)
#define GUC_STAGE_DESC_ATTR_PREEMPT	BIT(3)
#define GUC_STAGE_DESC_ATTR_RESET	BIT(4)
#define GUC_STAGE_DESC_ATTR_WQLOCKED	BIT(5)
#define GUC_STAGE_DESC_ATTR_PCH		BIT(6)
#define GUC_STAGE_DESC_ATTR_TERMINATED	BIT(7)

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
/* Verbosity range-check limits, without the shift */
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

#define GUC_CTL_MAX_DWORDS		(SOFT_SCRATCH_COUNT - 2) /* [1..14] */

/* Generic GT SysInfo data types */
#define GUC_GENERIC_GT_SYSINFO_SLICE_ENABLED		0
#define GUC_GENERIC_GT_SYSINFO_VDBOX_SFC_SUPPORT_MASK	1
#define GUC_GENERIC_GT_SYSINFO_DOORBELL_COUNT_PER_SQIDI	2
#define GUC_GENERIC_GT_SYSINFO_MAX			16

/*
 * The class goes in bits [0..2] of the GuC ID, the instance in bits [3..6].
 * Bit 7 can be used for operations that apply to all engine classes&instances.
 */
#define GUC_ENGINE_CLASS_SHIFT		0
#define GUC_ENGINE_CLASS_MASK		(0x7 << GUC_ENGINE_CLASS_SHIFT)
#define GUC_ENGINE_INSTANCE_SHIFT	3
#define GUC_ENGINE_INSTANCE_MASK	(0xf << GUC_ENGINE_INSTANCE_SHIFT)
#define GUC_ENGINE_ALL_INSTANCES	BIT(7)

#define MAKE_GUC_ID(class, instance) \
	(((class) << GUC_ENGINE_CLASS_SHIFT) | \
	 ((instance) << GUC_ENGINE_INSTANCE_SHIFT))

#define GUC_ID_TO_ENGINE_CLASS(guc_id) \
	(((guc_id) & GUC_ENGINE_CLASS_MASK) >> GUC_ENGINE_CLASS_SHIFT)
#define GUC_ID_TO_ENGINE_INSTANCE(guc_id) \
	(((guc_id) & GUC_ENGINE_INSTANCE_MASK) >> GUC_ENGINE_INSTANCE_SHIFT)

#define SLPC_EVENT(id, c) (\
FIELD_PREP(HOST2GUC_PC_SLPC_REQUEST_MSG_1_EVENT_ID, id) | \
FIELD_PREP(HOST2GUC_PC_SLPC_REQUEST_MSG_1_EVENT_ARGC, c) \
)

/* the GuC arrays don't include OTHER_CLASS */
static u8 engine_class_guc_class_map[] = {
	[RENDER_CLASS]            = GUC_RENDER_CLASS,
	[COPY_ENGINE_CLASS]       = GUC_BLITTER_CLASS,
	[VIDEO_DECODE_CLASS]      = GUC_VIDEO_CLASS,
	[VIDEO_ENHANCEMENT_CLASS] = GUC_VIDEOENHANCE_CLASS,
	[OTHER_CLASS]             = GUC_GSC_OTHER_CLASS,
	[COMPUTE_CLASS]           = GUC_COMPUTE_CLASS,
};

static u8 guc_class_engine_class_map[] = {
	[GUC_RENDER_CLASS]       = RENDER_CLASS,
	[GUC_BLITTER_CLASS]      = COPY_ENGINE_CLASS,
	[GUC_VIDEO_CLASS]        = VIDEO_DECODE_CLASS,
	[GUC_VIDEOENHANCE_CLASS] = VIDEO_ENHANCEMENT_CLASS,
	[GUC_COMPUTE_CLASS]      = COMPUTE_CLASS,
	[GUC_GSC_OTHER_CLASS]    = OTHER_CLASS,
};

static inline u8 engine_class_to_guc_class(u8 class)
{
	BUILD_BUG_ON(ARRAY_SIZE(engine_class_guc_class_map) != MAX_ENGINE_CLASS + 1);
	GEM_BUG_ON(class > MAX_ENGINE_CLASS);

	return engine_class_guc_class_map[class];
}

static inline u8 guc_class_to_engine_class(u8 guc_class)
{
	BUILD_BUG_ON(ARRAY_SIZE(guc_class_engine_class_map) != GUC_LAST_ENGINE_CLASS + 1);
	GEM_BUG_ON(guc_class > GUC_LAST_ENGINE_CLASS);

	return guc_class_engine_class_map[guc_class];
}

/* Work item for submitting workloads into work queue of GuC. */
struct guc_wq_item {
	u32 header;
	u32 context_desc;
	u32 submit_element_info;
	u32 fence_id;
} __packed;

struct guc_process_desc_v69 {
	u32 stage_id;
	u64 db_base_addr;
	u32 head;
	u32 tail;
	u32 error_offset;
	u64 wq_base_addr;
	u32 wq_size_bytes;
	u32 wq_status;
	u32 engine_presence;
	u32 priority;
	u32 reserved[36];
} __packed;

struct guc_sched_wq_desc {
	u32 head;
	u32 tail;
	u32 error_offset;
	u32 wq_status;
	u32 reserved[28];
} __packed;

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

/* Preempt to idle on quantum expiry */
#define CONTEXT_POLICY_FLAG_PREEMPT_TO_IDLE_V69	BIT(0)

/*
 * GuC Context registration descriptor.
 * FIXME: This is only required to exist during context registration.
 * The current 1:1 between guc_lrc_desc and LRCs for the lifetime of the LRC
 * is not required.
 */
struct guc_lrc_desc_v69 {
	u32 hw_context_desc;
	u32 slpm_perf_mode_hint;	/* SPLC v1 only */
	u32 slpm_freq_hint;
	u32 engine_submit_mask;		/* In logical space */
	u8 engine_class;
	u8 reserved0[3];
	u32 priority;
	u32 process_desc;
	u32 wq_addr;
	u32 wq_size;
	u32 context_flags;		/* CONTEXT_REGISTRATION_* */
	/* Time for one workload to execute. (in micro seconds) */
	u32 execution_quantum;
	/* Time to wait for a preemption request to complete before issuing a
	 * reset. (in micro seconds).
	 */
	u32 preemption_timeout;
	u32 policy_flags;		/* CONTEXT_POLICY_* */
	u32 reserved1[19];
} __packed;

/* 32-bit KLV structure as used by policy updates and others */
struct guc_klv_generic_dw_t {
	u32 kl;
	u32 value;
} __packed;

/* Format of the UPDATE_CONTEXT_POLICIES H2G data packet */
struct guc_update_context_policy_header {
	u32 action;
	u32 ctx_id;
} __packed;

struct guc_update_context_policy {
	struct guc_update_context_policy_header header;
	struct guc_klv_generic_dw_t klv[GUC_CONTEXT_POLICIES_KLV_NUM_IDS];
} __packed;

/* Format of the UPDATE_SCHEDULING_POLICIES H2G data packet */
struct guc_update_scheduling_policy_header {
	u32 action;
} __packed;

/*
 * Can't dynmically allocate memory for the scheduling policy KLV because
 * it will be sent from within the reset path. Need a fixed size lump on
 * the stack instead :(.
 *
 * Currently, there is only one KLV defined, which has 1 word of KL + 2 words of V.
 */
#define MAX_SCHEDULING_POLICY_SIZE 3

struct guc_update_scheduling_policy {
	struct guc_update_scheduling_policy_header header;
	u32 data[MAX_SCHEDULING_POLICY_SIZE];
} __packed;

#define GUC_POWER_UNSPECIFIED	0
#define GUC_POWER_D0		1
#define GUC_POWER_D1		2
#define GUC_POWER_D2		3
#define GUC_POWER_D3		4

/* Scheduling policy settings */

#define GLOBAL_SCHEDULE_POLICY_RC_YIELD_DURATION	100	/* in ms */
#define GLOBAL_SCHEDULE_POLICY_RC_YIELD_RATIO		50	/* in percent */

#define GLOBAL_POLICY_MAX_NUM_WI 15

/* Don't reset an engine upon preemption failure */
#define GLOBAL_POLICY_DISABLE_ENGINE_RESET				BIT(0)

#define GLOBAL_POLICY_DEFAULT_DPC_PROMOTE_TIME_US 500000

/*
 * GuC converts the timeout to clock ticks internally. Different platforms have
 * different GuC clocks. Thus, the maximum value before overflow is platform
 * dependent. Current worst case scenario is about 110s. So, the spec says to
 * limit to 100s to be safe.
 */
#define GUC_POLICY_MAX_EXEC_QUANTUM_US		(100 * 1000 * 1000UL)
#define GUC_POLICY_MAX_PREEMPT_TIMEOUT_US	(100 * 1000 * 1000UL)

static inline u32 guc_policy_max_exec_quantum_ms(void)
{
	BUILD_BUG_ON(GUC_POLICY_MAX_EXEC_QUANTUM_US >= UINT_MAX);
	return GUC_POLICY_MAX_EXEC_QUANTUM_US / 1000;
}

static inline u32 guc_policy_max_preempt_timeout_ms(void)
{
	BUILD_BUG_ON(GUC_POLICY_MAX_PREEMPT_TIMEOUT_US >= UINT_MAX);
	return GUC_POLICY_MAX_PREEMPT_TIMEOUT_US / 1000;
}

struct guc_policies {
	u32 submission_queue_depth[GUC_MAX_ENGINE_CLASSES];
	/* In micro seconds. How much time to allow before DPC processing is
	 * called back via interrupt (to prevent DPC queue drain starving).
	 * Typically 1000s of micro seconds (example only, not granularity). */
	u32 dpc_promote_time;

	/* Must be set to take these new values. */
	u32 is_valid;

	/* Max number of WIs to process per call. A large value may keep CS
	 * idle. */
	u32 max_num_work_items;

	u32 global_flags;
	u32 reserved[4];
} __packed;

/* GuC MMIO reg state struct */
struct guc_mmio_reg {
	u32 offset;
	u32 value;
	u32 flags;
#define GUC_REGSET_MASKED		BIT(0)
#define GUC_REGSET_NEEDS_STEERING	BIT(1)
#define GUC_REGSET_MASKED_WITH_VALUE	BIT(2)
#define GUC_REGSET_RESTORE_ONLY		BIT(3)
#define GUC_REGSET_STEERING_GROUP       GENMASK(15, 12)
#define GUC_REGSET_STEERING_INSTANCE    GENMASK(23, 20)
	u32 mask;
} __packed;

/* GuC register sets */
struct guc_mmio_reg_set {
	u32 address;
	u16 count;
	u16 reserved;
} __packed;

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

/*Register-types of GuC capture register lists */
enum guc_capture_type {
	GUC_CAPTURE_LIST_TYPE_GLOBAL = 0,
	GUC_CAPTURE_LIST_TYPE_ENGINE_CLASS,
	GUC_CAPTURE_LIST_TYPE_ENGINE_INSTANCE,
	GUC_CAPTURE_LIST_TYPE_MAX,
};

/* Class indecies for capture_class and capture_instance arrays */
enum {
	GUC_CAPTURE_LIST_CLASS_RENDER_COMPUTE = 0,
	GUC_CAPTURE_LIST_CLASS_VIDEO = 1,
	GUC_CAPTURE_LIST_CLASS_VIDEOENHANCE = 2,
	GUC_CAPTURE_LIST_CLASS_BLITTER = 3,
	GUC_CAPTURE_LIST_CLASS_GSC_OTHER = 4,
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
	u32 reserved2;
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

/* GuC logging structures */

enum guc_log_buffer_type {
	GUC_DEBUG_LOG_BUFFER,
	GUC_CRASH_DUMP_LOG_BUFFER,
	GUC_CAPTURE_LOG_BUFFER,
	GUC_MAX_LOG_BUFFER
};

/*
 * struct guc_log_buffer_state - GuC log buffer state
 *
 * Below state structure is used for coordination of retrieval of GuC firmware
 * logs. Separate state is maintained for each log buffer type.
 * read_ptr points to the location where i915 read last in log buffer and
 * is read only for GuC firmware. write_ptr is incremented by GuC with number
 * of bytes written for each log entry and is read only for i915.
 * When any type of log buffer becomes half full, GuC sends a flush interrupt.
 * GuC firmware expects that while it is writing to 2nd half of the buffer,
 * first half would get consumed by Host and then get a flush completed
 * acknowledgment from Host, so that it does not end up doing any overwrite
 * causing loss of logs. So when buffer gets half filled & i915 has requested
 * for interrupt, GuC will set flush_to_file field, set the sampled_write_ptr
 * to the value of write_ptr and raise the interrupt.
 * On receiving the interrupt i915 should read the buffer, clear flush_to_file
 * field and also update read_ptr with the value of sample_write_ptr, before
 * sending an acknowledgment to GuC. marker & version fields are for internal
 * usage of GuC and opaque to i915. buffer_full_cnt field is incremented every
 * time GuC detects the log buffer overflow.
 */
struct guc_log_buffer_state {
	u32 marker[2];
	u32 read_ptr;
	u32 write_ptr;
	u32 size;
	u32 sampled_write_ptr;
	u32 wrap_offset;
	union {
		struct {
			u32 flush_to_file:1;
			u32 buffer_full_cnt:4;
			u32 reserved:27;
		};
		u32 flags;
	};
	u32 version;
} __packed;

/* This action will be programmed in C1BC - SOFT_SCRATCH_15_REG */
enum intel_guc_recv_message {
	INTEL_GUC_RECV_MSG_CRASH_DUMP_POSTED = BIT(1),
	INTEL_GUC_RECV_MSG_EXCEPTION = BIT(30),
};

#endif
