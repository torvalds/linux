/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2016-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_CT_H_
#define _INTEL_GUC_CT_H_

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/stackdepot.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/wait.h>

#include "intel_guc_fwif.h"

struct i915_vma;
struct intel_guc;
struct drm_printer;

/**
 * DOC: Command Transport (CT).
 *
 * Buffer based command transport is a replacement for MMIO based mechanism.
 * It can be used to perform both host-2-guc and guc-to-host communication.
 */

/** Represents single command transport buffer.
 *
 * A single command transport buffer consists of two parts, the header
 * record (command transport buffer descriptor) and the actual buffer which
 * holds the commands.
 *
 * @lock: protects access to the commands buffer and buffer descriptor
 * @desc: pointer to the buffer descriptor
 * @cmds: pointer to the commands buffer
 * @size: size of the commands buffer in dwords
 * @resv_space: reserved space in buffer in dwords
 * @head: local shadow copy of head in dwords
 * @tail: local shadow copy of tail in dwords
 * @space: local shadow copy of space in dwords
 * @broken: flag to indicate if descriptor data is broken
 */
struct intel_guc_ct_buffer {
	spinlock_t lock;
	struct guc_ct_buffer_desc *desc;
	u32 *cmds;
	u32 size;
	u32 resv_space;
	u32 tail;
	u32 head;
	atomic_t space;
	bool broken;
};

/** Top-level structure for Command Transport related data
 *
 * Includes a pair of CT buffers for bi-directional communication and tracking
 * for the H2G and G2H requests sent and received through the buffers.
 */
struct intel_guc_ct {
	struct i915_vma *vma;
	bool enabled;

	/* buffers for sending and receiving commands */
	struct {
		struct intel_guc_ct_buffer send;
		struct intel_guc_ct_buffer recv;
	} ctbs;

	struct tasklet_struct receive_tasklet;

	/** @wq: wait queue for g2h chanenl */
	wait_queue_head_t wq;

	struct {
		u16 last_fence; /* last fence used to send request */

		spinlock_t lock; /* protects pending requests list */
		struct list_head pending; /* requests waiting for response */

		struct list_head incoming; /* incoming requests */
		struct work_struct worker; /* handler for incoming requests */

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
		struct {
			u16 fence;
			u16 action;
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GUC)
			depot_stack_handle_t stack;
#endif
		} lost_and_found[SZ_16];
#endif
	} requests;

	/** @stall_time: time of first time a CTB submission is stalled */
	ktime_t stall_time;

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GUC)
	int dead_ct_reason;
	bool dead_ct_reported;
	struct work_struct dead_ct_worker;
#endif
};

long intel_guc_ct_max_queue_time_jiffies(void);

void intel_guc_ct_init_early(struct intel_guc_ct *ct);
int intel_guc_ct_init(struct intel_guc_ct *ct);
void intel_guc_ct_fini(struct intel_guc_ct *ct);
int intel_guc_ct_enable(struct intel_guc_ct *ct);
void intel_guc_ct_disable(struct intel_guc_ct *ct);

static inline void intel_guc_ct_sanitize(struct intel_guc_ct *ct)
{
	ct->enabled = false;
}

static inline bool intel_guc_ct_enabled(struct intel_guc_ct *ct)
{
	return ct->enabled;
}

#define INTEL_GUC_CT_SEND_NB		BIT(31)
#define INTEL_GUC_CT_SEND_G2H_DW_SHIFT	0
#define INTEL_GUC_CT_SEND_G2H_DW_MASK	(0xff << INTEL_GUC_CT_SEND_G2H_DW_SHIFT)
#define MAKE_SEND_FLAGS(len) ({ \
	typeof(len) len_ = (len); \
	GEM_BUG_ON(!FIELD_FIT(INTEL_GUC_CT_SEND_G2H_DW_MASK, len_)); \
	(FIELD_PREP(INTEL_GUC_CT_SEND_G2H_DW_MASK, len_) | INTEL_GUC_CT_SEND_NB); \
})
int intel_guc_ct_send(struct intel_guc_ct *ct, const u32 *action, u32 len,
		      u32 *response_buf, u32 response_buf_size, u32 flags);
void intel_guc_ct_event_handler(struct intel_guc_ct *ct);

void intel_guc_ct_print_info(struct intel_guc_ct *ct, struct drm_printer *p);

#endif /* _INTEL_GUC_CT_H_ */
