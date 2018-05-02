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
 *    Anhua Xu
 *    Kevin Tian <kevin.tian@intel.com>
 *
 * Contributors:
 *    Min He <min.he@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#ifndef __GVT_SCHED_POLICY__
#define __GVT_SCHED_POLICY__

struct intel_gvt_sched_policy_ops {
	int (*init)(struct intel_gvt *gvt);
	void (*clean)(struct intel_gvt *gvt);
	int (*init_vgpu)(struct intel_vgpu *vgpu);
	void (*clean_vgpu)(struct intel_vgpu *vgpu);
	void (*start_schedule)(struct intel_vgpu *vgpu);
	void (*stop_schedule)(struct intel_vgpu *vgpu);
};

void intel_gvt_schedule(struct intel_gvt *gvt);

int intel_gvt_init_sched_policy(struct intel_gvt *gvt);

void intel_gvt_clean_sched_policy(struct intel_gvt *gvt);

int intel_vgpu_init_sched_policy(struct intel_vgpu *vgpu);

void intel_vgpu_clean_sched_policy(struct intel_vgpu *vgpu);

void intel_vgpu_start_schedule(struct intel_vgpu *vgpu);

void intel_vgpu_stop_schedule(struct intel_vgpu *vgpu);

void intel_gvt_kick_schedule(struct intel_gvt *gvt);

#endif
