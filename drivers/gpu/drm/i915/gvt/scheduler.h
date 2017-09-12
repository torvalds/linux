/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 * Contributors:
 *    Ping Gao <ping.a.gao@intel.com>
 *    Tina Zhang <tina.zhang@intel.com>
 *    Chanbin Du <changbin.du@intel.com>
 *    Min He <min.he@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *    Zhenyu Wang <zhenyuw@linux.intel.com>
 *
 */

#ifndef _GVT_SCHEDULER_H_
#define _GVT_SCHEDULER_H_

struct intel_gvt_workload_scheduler {
	struct intel_vgpu *current_vgpu;
	struct intel_vgpu *next_vgpu;
	struct intel_vgpu_workload *current_workload[I915_NUM_ENGINES];
	bool need_reschedule;

	spinlock_t mmio_context_lock;
	/* can be null when owner is host */
	struct intel_vgpu *engine_owner[I915_NUM_ENGINES];

	wait_queue_head_t workload_complete_wq;
	struct task_struct *thread[I915_NUM_ENGINES];
	wait_queue_head_t waitq[I915_NUM_ENGINES];

	void *sched_data;
	struct intel_gvt_sched_policy_ops *sched_ops;
};

#define INDIRECT_CTX_ADDR_MASK 0xffffffc0
#define INDIRECT_CTX_SIZE_MASK 0x3f
struct shadow_indirect_ctx {
	struct drm_i915_gem_object *obj;
	unsigned long guest_gma;
	unsigned long shadow_gma;
	void *shadow_va;
	uint32_t size;
};

#define PER_CTX_ADDR_MASK 0xfffff000
struct shadow_per_ctx {
	unsigned long guest_gma;
	unsigned long shadow_gma;
	unsigned valid;
};

struct intel_shadow_wa_ctx {
	struct shadow_indirect_ctx indirect_ctx;
	struct shadow_per_ctx per_ctx;

};

struct intel_vgpu_workload {
	struct intel_vgpu *vgpu;
	int ring_id;
	struct drm_i915_gem_request *req;
	/* if this workload has been dispatched to i915? */
	bool dispatched;
	bool shadowed;
	int status;

	struct intel_vgpu_mm *shadow_mm;

	/* different submission model may need different handler */
	int (*prepare)(struct intel_vgpu_workload *);
	int (*complete)(struct intel_vgpu_workload *);
	struct list_head list;

	DECLARE_BITMAP(pending_events, INTEL_GVT_EVENT_MAX);
	void *shadow_ring_buffer_va;

	/* execlist context information */
	struct execlist_ctx_descriptor_format ctx_desc;
	struct execlist_ring_context *ring_context;
	unsigned long rb_head, rb_tail, rb_ctl, rb_start, rb_len;
	bool restore_inhibit;
	struct intel_vgpu_elsp_dwords elsp_dwords;
	bool emulate_schedule_in;
	atomic_t shadow_ctx_active;
	wait_queue_head_t shadow_ctx_status_wq;
	u64 ring_context_gpa;

	/* shadow batch buffer */
	struct list_head shadow_bb;
	struct intel_shadow_wa_ctx wa_ctx;
};

/* Intel shadow batch buffer is a i915 gem object */
struct intel_shadow_bb_entry {
	struct list_head list;
	struct drm_i915_gem_object *obj;
	void *va;
	unsigned long len;
	u32 *bb_start_cmd_va;
};

#define workload_q_head(vgpu, ring_id) \
	(&(vgpu->submission.workload_q_head[ring_id]))

#define queue_workload(workload) do { \
	list_add_tail(&workload->list, \
	workload_q_head(workload->vgpu, workload->ring_id)); \
	wake_up(&workload->vgpu->gvt-> \
	scheduler.waitq[workload->ring_id]); \
} while (0)

int intel_gvt_init_workload_scheduler(struct intel_gvt *gvt);

void intel_gvt_clean_workload_scheduler(struct intel_gvt *gvt);

void intel_gvt_wait_vgpu_idle(struct intel_vgpu *vgpu);

int intel_vgpu_setup_submission(struct intel_vgpu *vgpu);

void intel_vgpu_clean_submission(struct intel_vgpu *vgpu);

int intel_vgpu_select_submission_ops(struct intel_vgpu *vgpu,
				     unsigned int interface);

extern const struct intel_vgpu_submission_ops
intel_vgpu_execlist_submission_ops;

struct intel_vgpu_workload *
intel_vgpu_create_workload(struct intel_vgpu *vgpu, int ring_id,
			   struct execlist_ctx_descriptor_format *desc);

void intel_vgpu_destroy_workload(struct intel_vgpu_workload *workload);

#endif
