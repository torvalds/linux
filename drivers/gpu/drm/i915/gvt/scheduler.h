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
 */

#ifndef _GVT_SCHEDULER_H_
#define _GVT_SCHEDULER_H_

struct intel_gvt_workload_scheduler {
	struct list_head workload_q_head[I915_NUM_ENGINES];
};

struct intel_vgpu_workload {
	struct intel_vgpu *vgpu;
	int ring_id;
	struct drm_i915_gem_request *req;
	/* if this workload has been dispatched to i915? */
	bool dispatched;
	int status;

	struct intel_vgpu_mm *shadow_mm;

	/* different submission model may need different handler */
	int (*prepare)(struct intel_vgpu_workload *);
	int (*complete)(struct intel_vgpu_workload *);
	struct list_head list;

	/* execlist context information */
	struct execlist_ctx_descriptor_format ctx_desc;
	struct execlist_ring_context *ring_context;
	unsigned long rb_head, rb_tail, rb_ctl, rb_start;
	struct intel_vgpu_elsp_dwords elsp_dwords;
	bool emulate_schedule_in;
	atomic_t shadow_ctx_active;
	wait_queue_head_t shadow_ctx_status_wq;
	u64 ring_context_gpa;
};

#define workload_q_head(vgpu, ring_id) \
	(&(vgpu->workload_q_head[ring_id]))

#define queue_workload(workload) \
	list_add_tail(&workload->list, \
	workload_q_head(workload->vgpu, workload->ring_id))

#endif
