/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_GUC_SUBMISSION_H_
#define _INTEL_GUC_SUBMISSION_H_

#include <linux/spinlock.h>

#include "gt/intel_engine_types.h"

#include "i915_gem.h"
#include "i915_selftest.h"

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
	struct intel_guc *guc;

	/* bitmap of (host) engine ids */
	u32 priority;
	u32 stage_id;
	u32 proc_desc_offset;

	u16 doorbell_id;
	unsigned long doorbell_offset;

	/* Protects GuC client's WQ access */
	spinlock_t wq_lock;

	/* For testing purposes, use nop WQ items instead of real ones */
	I915_SELFTEST_DECLARE(bool use_nop_wqi);
};

void intel_guc_submission_init_early(struct intel_guc *guc);
int intel_guc_submission_init(struct intel_guc *guc);
int intel_guc_submission_enable(struct intel_guc *guc);
void intel_guc_submission_disable(struct intel_guc *guc);
void intel_guc_submission_fini(struct intel_guc *guc);
int intel_guc_preempt_work_create(struct intel_guc *guc);
void intel_guc_preempt_work_destroy(struct intel_guc *guc);

#endif
