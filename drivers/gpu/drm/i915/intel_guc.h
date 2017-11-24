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

#ifndef _INTEL_GUC_H_
#define _INTEL_GUC_H_

#include "intel_uncore.h"
#include "intel_guc_fw.h"
#include "intel_guc_fwif.h"
#include "intel_guc_ct.h"
#include "intel_guc_log.h"
#include "intel_guc_reg.h"
#include "intel_uc_fw.h"
#include "i915_vma.h"

struct guc_preempt_work {
	struct work_struct work;
	struct intel_engine_cs *engine;
};

/*
 * Top level structure of GuC. It handles firmware loading and manages client
 * pool and doorbells. intel_guc owns a intel_guc_client to replace the legacy
 * ExecList submission.
 */
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
	struct i915_vma *shared_data;
	void *shared_data_vaddr;

	struct intel_guc_client *execbuf_client;
	struct intel_guc_client *preempt_client;

	struct guc_preempt_work preempt_work[I915_NUM_ENGINES];
	struct workqueue_struct *preempt_wq;

	DECLARE_BITMAP(doorbell_bitmap, GUC_NUM_DOORBELLS);
	/* Cyclic counter mod pagesize	*/
	u32 db_cacheline;

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

static
inline int intel_guc_send(struct intel_guc *guc, const u32 *action, u32 len)
{
	return guc->send(guc, action, len);
}

static inline void intel_guc_notify(struct intel_guc *guc)
{
	guc->notify(guc);
}

/*
 * GuC does not allow any gfx GGTT address that falls into range [0, WOPCM_TOP),
 * which is reserved for Boot ROM, SRAM and WOPCM. Currently this top address is
 * 512K. In order to exclude 0-512K address space from GGTT, all gfx objects
 * used by GuC is pinned with PIN_OFFSET_BIAS along with size of WOPCM.
 */
static inline u32 guc_ggtt_offset(struct i915_vma *vma)
{
	u32 offset = i915_ggtt_offset(vma);

	GEM_BUG_ON(offset < GUC_WOPCM_TOP);
	GEM_BUG_ON(range_overflows_t(u64, offset, vma->size, GUC_GGTT_TOP));

	return offset;
}

void intel_guc_init_early(struct intel_guc *guc);
void intel_guc_init_send_regs(struct intel_guc *guc);
void intel_guc_init_params(struct intel_guc *guc);
int intel_guc_send_nop(struct intel_guc *guc, const u32 *action, u32 len);
int intel_guc_send_mmio(struct intel_guc *guc, const u32 *action, u32 len);
int intel_guc_sample_forcewake(struct intel_guc *guc);
int intel_guc_auth_huc(struct intel_guc *guc, u32 rsa_offset);
int intel_guc_suspend(struct drm_i915_private *dev_priv);
int intel_guc_resume(struct drm_i915_private *dev_priv);
struct i915_vma *intel_guc_allocate_vma(struct intel_guc *guc, u32 size);
u32 intel_guc_wopcm_size(struct drm_i915_private *dev_priv);

#endif
