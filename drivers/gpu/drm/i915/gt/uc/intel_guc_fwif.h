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

#define GUC_MAX_LRC_DESCRIPTORS		65535
#define	GUC_INVALID_LRC_ID		GUC_MAX_LRC_DESCRIPTORS

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
#define GUC_RESERVED_CLASS		4
#define GUC_LAST_ENGINE_CLASS		GUC_RESERVED_CLASS
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
#define   GUC_LOG_VALID			(1 << 0)
#define   GUC_LOG_NOTIFY_ON_HALF_FULL	(1 << 1)
#define   GUC_LOG_ALLOC_IN_MEGABYTE	(1 << 3)
#define   GUC_LOG_CRASH_SHIFT		4
#define   GUC_LOG_CRASH_MASK		(0x3 << GUC_LOG_CRASH_SHIFT)
#define   GUC_LOG_DEBUG_SHIFT		6
#define   GUC_LOG_DEBUG_MASK	        (0xF << GUC_LOG_DEBUG_SHIFT)
#define   GUC_LOG_BUF_ADDR_SHIFT	12

#define GUC_CTL_WA			1
#define GUC_CTL_FEATURE			2
#define   GUC_CTL_DISABLE_SCHEDULER	(1 << 14)
#define   GUC_CTL_ENABLE_SLPC		BIT(2)

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

static inline u8 engine_class_to_guc_class(u8 class)
{
	BUILD_BUG_ON(GUC_RENDER_CLASS != RENDER_CLASS);
	BUILD_BUG_ON(GUC_BLITTER_CLASS != COPY_ENGINE_CLASS);
	BUILD_BUG_ON(GUC_VIDEO_CLASS != VIDEO_DECODE_CLASS);
	BUILD_BUG_ON(GUC_VIDEOENHANCE_CLASS != VIDEO_ENHANCEMENT_CLASS);
	GEM_BUG_ON(class > MAX_ENGINE_CLASS || class == OTHER_CLASS);

	return class;
}

static inline u8 guc_class_to_engine_class(u8 guc_class)
{
	GEM_BUG_ON(guc_class > GUC_LAST_ENGINE_CLASS);
	GEM_BUG_ON(guc_class == GUC_RESERVED_CLASS);

	return guc_class;
}

/* Work item for submitting workloads into work queue of GuC. */
struct guc_wq_item {
	u32 header;
	u32 context_desc;
	u32 submit_element_info;
	u32 fence_id;
} __packed;

struct guc_process_desc {
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

#define CONTEXT_REGISTRATION_FLAG_KMD	BIT(0)

#define CONTEXT_POLICY_DEFAULT_EXECUTION_QUANTUM_US 1000000
#define CONTEXT_POLICY_DEFAULT_PREEMPTION_TIME_US 500000

/* Preempt to idle on quantum expiry */
#define CONTEXT_POLICY_FLAG_PREEMPT_TO_IDLE	BIT(0)

/*
 * GuC Context registration descriptor.
 * FIXME: This is only required to exist during context registration.
 * The current 1:1 between guc_lrc_desc and LRCs for the lifetime of the LRC
 * is not required.
 */
struct guc_lrc_desc {
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

#define GUC_POWER_UNSPECIFIED	0
#define GUC_POWER_D0		1
#define GUC_POWER_D1		2
#define GUC_POWER_D2		3
#define GUC_POWER_D3		4

/* Scheduling policy settings */

#define GLOBAL_POLICY_MAX_NUM_WI 15

/* Don't reset an engine upon preemption failure */
#define GLOBAL_POLICY_DISABLE_ENGINE_RESET				BIT(0)

#define GLOBAL_POLICY_DEFAULT_DPC_PROMOTE_TIME_US 500000

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
#define GUC_REGSET_MASKED		(1 << 0)
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
	u32 reserved[15];
} __packed;

/* GuC logging structures */

enum guc_log_buffer_type {
	GUC_DEBUG_LOG_BUFFER,
	GUC_CRASH_DUMP_LOG_BUFFER,
	GUC_MAX_LOG_BUFFER
};

/**
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

struct guc_ctx_report {
	u32 report_return_status;
	u32 reserved1[64];
	u32 affected_count;
	u32 reserved2[2];
} __packed;

/* GuC Shared Context Data Struct */
struct guc_shared_ctx_data {
	u32 addr_of_last_preempted_data_low;
	u32 addr_of_last_preempted_data_high;
	u32 addr_of_last_preempted_data_high_tmp;
	u32 padding;
	u32 is_mapped_to_proxy;
	u32 proxy_ctx_id;
	u32 engine_reset_ctx_id;
	u32 media_reset_count;
	u32 reserved1[8];
	u32 uk_last_ctx_switch_reason;
	u32 was_reset;
	u32 lrca_gpu_addr;
	u64 execlist_ctx;
	u32 reserved2[66];
	struct guc_ctx_report preempt_ctx_report[GUC_MAX_ENGINES_NUM];
} __packed;

/* This action will be programmed in C1BC - SOFT_SCRATCH_15_REG */
enum intel_guc_recv_message {
	INTEL_GUC_RECV_MSG_CRASH_DUMP_POSTED = BIT(1),
	INTEL_GUC_RECV_MSG_FLUSH_LOG_BUFFER = BIT(3)
};

#endif
