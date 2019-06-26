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
#include "i915_utils.h"
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
	spinlock_t irq_lock;
	unsigned int msg_enabled_mask;

	struct {
		bool enabled;
		void (*reset)(struct drm_i915_private *i915);
		void (*enable)(struct drm_i915_private *i915);
		void (*disable)(struct drm_i915_private *i915);
	} interrupts;

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
	int (*send)(struct intel_guc *guc, const u32 *data, u32 len,
		    u32 *response_buf, u32 response_buf_size);

	/* GuC's FW specific event handler function */
	void (*handler)(struct intel_guc *guc);

	/* GuC's FW specific notify function */
	void (*notify)(struct intel_guc *guc);
};

static
inline int intel_guc_send(struct intel_guc *guc, const u32 *action, u32 len)
{
	return guc->send(guc, action, len, NULL, 0);
}

static inline int
intel_guc_send_and_receive(struct intel_guc *guc, const u32 *action, u32 len,
			   u32 *response_buf, u32 response_buf_size)
{
	return guc->send(guc, action, len, response_buf, response_buf_size);
}

static inline void intel_guc_notify(struct intel_guc *guc)
{
	guc->notify(guc);
}

static inline void intel_guc_to_host_event_handler(struct intel_guc *guc)
{
	guc->handler(guc);
}

/* GuC addresses above GUC_GGTT_TOP also don't map through the GTT */
#define GUC_GGTT_TOP	0xFEE00000

/**
 * intel_guc_ggtt_offset() - Get and validate the GGTT offset of @vma
 * @guc: intel_guc structure.
 * @vma: i915 graphics virtual memory area.
 *
 * GuC does not allow any gfx GGTT address that falls into range
 * [0, ggtt.pin_bias), which is reserved for Boot ROM, SRAM and WOPCM.
 * Currently, in order to exclude [0, ggtt.pin_bias) address space from
 * GGTT, all gfx objects used by GuC are allocated with intel_guc_allocate_vma()
 * and pinned with PIN_OFFSET_BIAS along with the value of ggtt.pin_bias.
 *
 * Return: GGTT offset of the @vma.
 */
static inline u32 intel_guc_ggtt_offset(struct intel_guc *guc,
					struct i915_vma *vma)
{
	u32 offset = i915_ggtt_offset(vma);

	GEM_BUG_ON(offset < i915_ggtt_pin_bias(vma));
	GEM_BUG_ON(range_overflows_t(u64, offset, vma->size, GUC_GGTT_TOP));

	return offset;
}

void intel_guc_init_early(struct intel_guc *guc);
void intel_guc_init_send_regs(struct intel_guc *guc);
void intel_guc_init_params(struct intel_guc *guc);
int intel_guc_init_misc(struct intel_guc *guc);
int intel_guc_init(struct intel_guc *guc);
void intel_guc_fini(struct intel_guc *guc);
void intel_guc_fini_misc(struct intel_guc *guc);
int intel_guc_send_nop(struct intel_guc *guc, const u32 *action, u32 len,
		       u32 *response_buf, u32 response_buf_size);
int intel_guc_send_mmio(struct intel_guc *guc, const u32 *action, u32 len,
			u32 *response_buf, u32 response_buf_size);
void intel_guc_to_host_event_handler(struct intel_guc *guc);
void intel_guc_to_host_event_handler_nop(struct intel_guc *guc);
int intel_guc_to_host_process_recv_msg(struct intel_guc *guc,
				       const u32 *payload, u32 len);
int intel_guc_sample_forcewake(struct intel_guc *guc);
int intel_guc_auth_huc(struct intel_guc *guc, u32 rsa_offset);
int intel_guc_suspend(struct intel_guc *guc);
int intel_guc_resume(struct intel_guc *guc);
struct i915_vma *intel_guc_allocate_vma(struct intel_guc *guc, u32 size);

static inline bool intel_guc_is_loaded(struct intel_guc *guc)
{
	return intel_uc_fw_is_loaded(&guc->fw);
}

static inline int intel_guc_sanitize(struct intel_guc *guc)
{
	intel_uc_fw_sanitize(&guc->fw);
	return 0;
}

static inline void intel_guc_enable_msg(struct intel_guc *guc, u32 mask)
{
	spin_lock_irq(&guc->irq_lock);
	guc->msg_enabled_mask |= mask;
	spin_unlock_irq(&guc->irq_lock);
}

static inline void intel_guc_disable_msg(struct intel_guc *guc, u32 mask)
{
	spin_lock_irq(&guc->irq_lock);
	guc->msg_enabled_mask &= ~mask;
	spin_unlock_irq(&guc->irq_lock);
}

int intel_guc_reset_engine(struct intel_guc *guc,
			   struct intel_engine_cs *engine);

#endif
