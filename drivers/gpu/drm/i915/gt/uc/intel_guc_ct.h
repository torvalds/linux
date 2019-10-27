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

/** Represents pair of command transport buffers.
 *
 * Buffers go in pairs to allow bi-directional communication.
 * To simplify the code we place both of them in the same vma.
 * Buffers from the same pair must share unique owner id.
 *
 * @vma: pointer to the vma with pair of CT buffers
 * @ctbs: buffers for sending(0) and receiving(1) commands
 * @owner: unique identifier
 * @next_fence: fence to be used with next send command
 */
struct intel_guc_ct_channel {
	struct i915_vma *vma;
	struct intel_guc_ct_buffer ctbs[2];
	u32 owner;
	u32 next_fence;
	bool enabled;
};

/** Holds all command transport channels.
 *
 * @host_channel: main channel used by the host
 */
struct intel_guc_ct {
	struct intel_guc_ct_channel host_channel;
	/* other channels are tbd */

	/** @lock: protects pending requests list */
	spinlock_t lock;

	/** @pending_requests: list of requests waiting for response */
	struct list_head pending_requests;

	/** @incoming_requests: list of incoming requests */
	struct list_head incoming_requests;

	/** @worker: worker for handling incoming requests */
	struct work_struct worker;
};

void intel_guc_ct_init_early(struct intel_guc_ct *ct);
int intel_guc_ct_init(struct intel_guc_ct *ct);
void intel_guc_ct_fini(struct intel_guc_ct *ct);
int intel_guc_ct_enable(struct intel_guc_ct *ct);
void intel_guc_ct_disable(struct intel_guc_ct *ct);

static inline void intel_guc_ct_stop(struct intel_guc_ct *ct)
{
	ct->host_channel.enabled = false;
}

int intel_guc_send_ct(struct intel_guc *guc, const u32 *action, u32 len,
		      u32 *response_buf, u32 response_buf_size);
void intel_guc_to_host_event_handler_ct(struct intel_guc *guc);

#endif /* _INTEL_GUC_CT_H_ */
