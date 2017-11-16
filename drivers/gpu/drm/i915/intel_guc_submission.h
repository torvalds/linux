/*
 * Copyright © 2014-2017 Intel Corporation
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
 *
 */

#ifndef _I915_GUC_SUBMISSION_H_
#define _I915_GUC_SUBMISSION_H_

#include <linux/spinlock.h>

#include "i915_gem.h"

struct drm_i915_private;

/*
 * This structure primarily describes the GEM object shared with the GuC.
 * The specs sometimes refer to this object as a "GuC context", but we use
 * the term "client" to avoid confusion with hardware contexts. This
 * GEM object is held for the entire lifetime of our interaction with
 * the GuC, being allocated before the GuC is loaded with its firmware.
 * Because there's no way to update the address used by the GuC after
 * initialisation, the shared object must stay pinned into the GGTT as
 * long as the GuC is in use. We also keep the first page (only) mapped
 * into kernel address space, as it includes shared data that must be
 * updated on every request submission.
 *
 * The single GEM object described here is actually made up of several
 * separate areas, as far as the GuC is concerned. The first page (kept
 * kmap'd) includes the "process descriptor" which holds sequence data for
 * the doorbell, and one cacheline which actually *is* the doorbell; a
 * write to this will "ring the doorbell" (i.e. send an interrupt to the
 * GuC). The subsequent  pages of the client object constitute the work
 * queue (a circular array of work items), again described in the process
 * descriptor. Work queue pages are mapped momentarily as required.
 */
struct intel_guc_client {
	struct i915_vma *vma;
	void *vaddr;
	struct i915_gem_context *owner;
	struct intel_guc *guc;

	/* bitmap of (host) engine ids */
	u32 engines;
	u32 priority;
	u32 stage_id;
	u32 proc_desc_offset;

	u16 doorbell_id;
	unsigned long doorbell_offset;

	/* Protects GuC client's WQ access */
	spinlock_t wq_lock;
	/* Per-engine counts of GuC submissions */
	u64 submissions[I915_NUM_ENGINES];
};

int intel_guc_submission_init(struct intel_guc *guc);
int intel_guc_submission_enable(struct intel_guc *guc);
void intel_guc_submission_disable(struct intel_guc *guc);
void intel_guc_submission_fini(struct intel_guc *guc);

#endif
