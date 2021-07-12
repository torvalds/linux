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
#include "abi/guc_errors_abi.h"
#include "abi/guc_communication_mmio_abi.h"
#include "abi/guc_communication_ctb_abi.h"
#include "abi/guc_messages_abi.h"

#define GUC_CLIENT_PRIORITY_KMD_HIGH	0
#define GUC_CLIENT_PRIORITY_HIGH	1
#define GUC_CLIENT_PRIORITY_KMD_NORMAL	2
#define GUC_CLIENT_PRIORITY_NORMAL	3
#define GUC_CLIENT_PRIORITY_NUM		4

#define GUC_MAX_STAGE_DESCRIPTORS	1024
#define	GUC_INVALID_STAGE_ID		GUC_MAX_STAGE_DESCRIPTORS

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

#define GUC_WQ_SIZE			(PAGE_SIZE * 2)

/* Work queue item header definitions */
#define WQ_STATUS_ACTIVE		1
#define WQ_STATUS_SUSPENDED		2
#define WQ_STATUS_CMD_ERROR		3
#define WQ_STATUS_ENGINE_ID_NOT_USED	4
#define WQ_STATUS_SUSPENDED_FROM_RESET	5
#define WQ_TYPE_SHIFT			0
#define   WQ_TYPE_BATCH_BUF		(0x1 << WQ_TYPE_SHIFT)
#define   WQ_TYPE_PSEUDO		(0x2 << WQ_TYPE_SHIFT)
#define   WQ_TYPE_INORDER		(0x3 << WQ_TYPE_SHIFT)
#define   WQ_TYPE_NOOP			(0x4 << WQ_TYPE_SHIFT)
#define WQ_TARGET_SHIFT			10
#define WQ_LEN_SHIFT			16
#define WQ_NO_WCFLUSH_WAIT		(1 << 27)
#define WQ_PRESENT_WORKLOAD		(1 << 28)

#define WQ_RING_TAIL_SHIFT		20
#define WQ_RING_TAIL_MAX		0x7FF	/* 2^11 QWords */
#define WQ_RING_TAIL_MASK		(WQ_RING_TAIL_MAX << WQ_RING_TAIL_SHIFT)

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
#define   GUC_LOG_DPC_SHIFT		6
#define   GUC_LOG_DPC_MASK	        (0x7 << GUC_LOG_DPC_SHIFT)
#define   GUC_LOG_ISR_SHIFT		9
#define   GUC_LOG_ISR_MASK	        (0x7 << GUC_LOG_ISR_SHIFT)
#define   GUC_LOG_BUF_ADDR_SHIFT	12

#define GUC_CTL_WA			1
#define GUC_CTL_FEATURE			2
#define   GUC_CTL_DISABLE_SCHEDULER	(1 << 14)

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
	u32 reserved[30];
} __packed;

/* engine id and context id is packed into guc_execlist_context.context_id*/
#define GUC_ELC_CTXID_OFFSET		0
#define GUC_ELC_ENGINE_OFFSET		29

/* The execlist context including software and HW information */
struct guc_execlist_context {
	u32 context_desc;
	u32 context_id;
	u32 ring_status;
	u32 ring_lrca;
	u32 ring_begin;
	u32 ring_end;
	u32 ring_next_free_location;
	u32 ring_current_tail_pointer_value;
	u8 engine_state_submit_value;
	u8 engine_state_wait_value;
	u16 pagefault_count;
	u16 engine_submit_queue_count;
} __packed;

/*
 * This structure describes a stage set arranged for a particular communication
 * between uKernel (GuC) and Driver (KMD). Technically, this is known as a
 * "GuC Context descriptor" in the specs, but we use the term "stage descriptor"
 * to avoid confusion with all the other things already named "context" in the
 * driver. A static pool of these descriptors are stored inside a GEM object
 * (stage_desc_pool) which is held for the entire lifetime of our interaction
 * with the GuC, being allocated before the GuC is loaded with its firmware.
 */
struct guc_stage_desc {
	u32 sched_common_area;
	u32 stage_id;
	u32 pas_id;
	u8 engines_used;
	u64 db_trigger_cpu;
	u32 db_trigger_uk;
	u64 db_trigger_phy;
	u16 db_id;

	struct guc_execlist_context lrc[GUC_MAX_ENGINES_NUM];

	u8 attribute;

	u32 priority;

	u32 wq_sampled_tail_offset;
	u32 wq_total_submit_enqueues;

	u32 process_desc;
	u32 wq_addr;
	u32 wq_size;

	u32 engine_presence;

	u8 engine_suspended;

	u8 reserved0[3];
	u64 reserved1[1];

	u64 desc_private;
} __packed;

#define GUC_POWER_UNSPECIFIED	0
#define GUC_POWER_D0		1
#define GUC_POWER_D1		2
#define GUC_POWER_D2		3
#define GUC_POWER_D3		4

/* Scheduling policy settings */

/* Reset engine upon preempt failure */
#define POLICY_RESET_ENGINE		(1<<0)
/* Preempt to idle on quantum expiry */
#define POLICY_PREEMPT_TO_IDLE		(1<<1)

#define POLICY_MAX_NUM_WI 15
#define POLICY_DEFAULT_DPC_PROMOTE_TIME_US 500000
#define POLICY_DEFAULT_EXECUTION_QUANTUM_US 1000000
#define POLICY_DEFAULT_PREEMPTION_TIME_US 500000
#define POLICY_DEFAULT_FAULT_TIME_US 250000

struct guc_policy {
	/* Time for one workload to execute. (in micro seconds) */
	u32 execution_quantum;
	/* Time to wait for a preemption request to completed before issuing a
	 * reset. (in micro seconds). */
	u32 preemption_time;
	/* How much time to allow to run after the first fault is observed.
	 * Then preempt afterwards. (in micro seconds) */
	u32 fault_time;
	u32 policy_flags;
	u32 reserved[8];
} __packed;

struct guc_policies {
	struct guc_policy policy[GUC_CLIENT_PRIORITY_NUM][GUC_MAX_ENGINE_CLASSES];
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

/* Clients info */
struct guc_ct_pool_entry {
	struct guc_ct_buffer_desc desc;
	u32 reserved[7];
} __packed;

#define GUC_CT_POOL_SIZE	2

struct guc_clients_info {
	u32 clients_num;
	u32 reserved0[13];
	u32 ct_pool_addr;
	u32 ct_pool_count;
	u32 reserved[4];
} __packed;

/* GuC Additional Data Struct */
struct guc_ads {
	struct guc_mmio_reg_set reg_state_list[GUC_MAX_ENGINE_CLASSES][GUC_MAX_INSTANCES_PER_CLASS];
	u32 reserved0;
	u32 scheduler_policies;
	u32 gt_system_info;
	u32 clients_info;
	u32 control_data;
	u32 golden_context_lrca[GUC_MAX_ENGINE_CLASSES];
	u32 eng_state_size[GUC_MAX_ENGINE_CLASSES];
	u32 private_data;
	u32 reserved[15];
} __packed;

/* GuC logging structures */

enum guc_log_buffer_type {
	GUC_ISR_LOG_BUFFER,
	GUC_DPC_LOG_BUFFER,
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

#define __INTEL_GUC_MSG_GET(T, m) \
	(((m) & INTEL_GUC_MSG_ ## T ## _MASK) >> INTEL_GUC_MSG_ ## T ## _SHIFT)
#define INTEL_GUC_MSG_TO_TYPE(m)	__INTEL_GUC_MSG_GET(TYPE, m)
#define INTEL_GUC_MSG_TO_DATA(m)	__INTEL_GUC_MSG_GET(DATA, m)
#define INTEL_GUC_MSG_TO_CODE(m)	__INTEL_GUC_MSG_GET(CODE, m)

#define __INTEL_GUC_MSG_TYPE_IS(T, m) \
	(INTEL_GUC_MSG_TO_TYPE(m) == INTEL_GUC_MSG_TYPE_ ## T)
#define INTEL_GUC_MSG_IS_REQUEST(m)	__INTEL_GUC_MSG_TYPE_IS(REQUEST, m)
#define INTEL_GUC_MSG_IS_RESPONSE(m)	__INTEL_GUC_MSG_TYPE_IS(RESPONSE, m)

#define INTEL_GUC_MSG_IS_RESPONSE_SUCCESS(m) \
	 (typecheck(u32, (m)) && \
	  ((m) & (INTEL_GUC_MSG_TYPE_MASK | INTEL_GUC_MSG_CODE_MASK)) == \
	  ((INTEL_GUC_MSG_TYPE_RESPONSE << INTEL_GUC_MSG_TYPE_SHIFT) | \
	   (INTEL_GUC_RESPONSE_STATUS_SUCCESS << INTEL_GUC_MSG_CODE_SHIFT)))

/* This action will be programmed in C1BC - SOFT_SCRATCH_15_REG */
enum intel_guc_recv_message {
	INTEL_GUC_RECV_MSG_CRASH_DUMP_POSTED = BIT(1),
	INTEL_GUC_RECV_MSG_FLUSH_LOG_BUFFER = BIT(3)
};

#endif
