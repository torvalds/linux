/*
 * Copyright © 2016-2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _INTEL_GUC_CT_H_
#define _INTEL_GUC_CT_H_

struct intel_guc;
struct i915_vma;

#include "intel_guc_fwif.h"

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

#endif /* _INTEL_GUC_CT_H_ */
