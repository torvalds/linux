/*
 * Copyright © 2014 Intel Corporation
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
#ifndef _INTEL_UC_H_
#define _INTEL_UC_H_

#include "intel_uc_fw.h"
#include "intel_guc_fwif.h"
#include "i915_guc_reg.h"
#include "intel_ringbuffer.h"
#include "intel_guc_ct.h"
#include "intel_guc_log.h"
#include "i915_vma.h"
#include "intel_huc.h"

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
struct i915_guc_client {
	struct i915_vma *vma;
	void *vaddr;
	struct i915_gem_context *owner;
	struct intel_guc *guc;

	uint32_t engines;		/* bitmap of (host) engine ids	*/
	uint32_t priority;
	u32 stage_id;
	uint32_t proc_desc_offset;

	u16 doorbell_id;
	unsigned long doorbell_offset;

	spinlock_t wq_lock;
	/* Per-engine counts of GuC submissions */
	uint64_t submissions[I915_NUM_ENGINES];
};

struct intel_guc {
	struct intel_uc_fw fw;
	struct intel_guc_log log;
	struct intel_guc_ct ct;

	/* Log snapshot if GuC errors during load */
	struct drm_i915_gem_object *load_err_log;

	/* intel_guc_recv interrupt related state */
	bool interrupts_enabled;

	struct i915_vma *ads_vma;
	struct i915_vma *stage_desc_pool;
	void *stage_desc_pool_vaddr;
	struct ida stage_ids;

	struct i915_guc_client *execbuf_client;

	DECLARE_BITMAP(doorbell_bitmap, GUC_NUM_DOORBELLS);
	uint32_t db_cacheline;		/* Cyclic counter mod pagesize	*/

	/* GuC's FW specific registers used in MMIO send */
	struct {
		u32 base;
		unsigned int count;
		enum forcewake_domains fw_domains;
	} send_regs;

	/* To serialize the intel_guc_send actions */
	struct mutex send_mutex;

	/* GuC's FW specific send function */
	int (*send)(struct intel_guc *guc, const u32 *data, u32 len);

	/* GuC's FW specific notify function */
	void (*notify)(struct intel_guc *guc);
};

/* intel_uc.c */
void intel_uc_sanitize_options(struct drm_i915_private *dev_priv);
void intel_uc_init_early(struct drm_i915_private *dev_priv);
void intel_uc_init_mmio(struct drm_i915_private *dev_priv);
void intel_uc_init_fw(struct drm_i915_private *dev_priv);
void intel_uc_fini_fw(struct drm_i915_private *dev_priv);
int intel_uc_init_hw(struct drm_i915_private *dev_priv);
void intel_uc_fini_hw(struct drm_i915_private *dev_priv);
int intel_guc_sample_forcewake(struct intel_guc *guc);
int intel_guc_send_nop(struct intel_guc *guc, const u32 *action, u32 len);
int intel_guc_send_mmio(struct intel_guc *guc, const u32 *action, u32 len);
int intel_guc_auth_huc(struct intel_guc *guc, u32 rsa_offset);

static inline int intel_guc_send(struct intel_guc *guc, const u32 *action, u32 len)
{
	return guc->send(guc, action, len);
}

static inline void intel_guc_notify(struct intel_guc *guc)
{
	guc->notify(guc);
}

/* intel_guc_loader.c */
int intel_guc_select_fw(struct intel_guc *guc);
int intel_guc_init_hw(struct intel_guc *guc);
int intel_guc_suspend(struct drm_i915_private *dev_priv);
int intel_guc_resume(struct drm_i915_private *dev_priv);
u32 intel_guc_wopcm_size(struct drm_i915_private *dev_priv);

/* i915_guc_submission.c */
int i915_guc_submission_init(struct drm_i915_private *dev_priv);
int i915_guc_submission_enable(struct drm_i915_private *dev_priv);
void i915_guc_submission_disable(struct drm_i915_private *dev_priv);
void i915_guc_submission_fini(struct drm_i915_private *dev_priv);
struct i915_vma *intel_guc_allocate_vma(struct intel_guc *guc, u32 size);

static inline u32 guc_ggtt_offset(struct i915_vma *vma)
{
	u32 offset = i915_ggtt_offset(vma);
	GEM_BUG_ON(offset < GUC_WOPCM_TOP);
	GEM_BUG_ON(range_overflows_t(u64, offset, vma->size, GUC_GGTT_TOP));
	return offset;
}

#endif
