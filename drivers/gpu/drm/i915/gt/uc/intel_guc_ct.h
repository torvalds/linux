/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2016-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_CT_H_
#define _INTEL_GUC_CT_H_

#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "intel_guc_fwif.h"

struct i915_vma;
struct intel_guc;

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
 * @desc: pointer to the buffer descriptor
 * @cmds: pointer to the commands buffer
 */
struct intel_guc_ct_buffer {
	struct guc_ct_buffer_desc *desc;
	u32 *cmds;
};


/** Top-level structure for Command Transport related data
 *
 * Includes a pair of CT buffers for bi-directional communication and tracking
 * for the H2G and G2H requests sent and received through the buffers.
 */
struct intel_guc_ct {
	struct i915_vma *vma;
	bool enabled;

	/* buffers for sending(0) and receiving(1) commands */
	struct intel_guc_ct_buffer ctbs[2];

	struct {
		u32 next_fence; /* fence to be used with next request to send */

		spinlock_t lock; /* protects pending requests list */
		struct list_head pending; /* requests waiting for response */

		struct list_head incoming; /* incoming requests */
		struct work_struct worker; /* handler for incoming requests */
	} requests;
};

void intel_guc_ct_init_early(struct intel_guc_ct *ct);
int intel_guc_ct_init(struct intel_guc_ct *ct);
void intel_guc_ct_fini(struct intel_guc_ct *ct);
int intel_guc_ct_enable(struct intel_guc_ct *ct);
void intel_guc_ct_disable(struct intel_guc_ct *ct);

static inline bool intel_guc_ct_enabled(struct intel_guc_ct *ct)
{
	return ct->enabled;
}

int intel_guc_ct_send(struct intel_guc_ct *ct, const u32 *action, u32 len,
		      u32 *response_buf, u32 response_buf_size);
void intel_guc_ct_event_handler(struct intel_guc_ct *ct);

#endif /* _INTEL_GUC_CT_H_ */
