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

#ifndef _GVT_H_
#define _GVT_H_

#include "debug.h"
#include "hypercall.h"

#define GVT_MAX_VGPU 8

enum {
	INTEL_GVT_HYPERVISOR_XEN = 0,
	INTEL_GVT_HYPERVISOR_KVM,
};

struct intel_gvt_host {
	bool initialized;
	int hypervisor_type;
	struct intel_gvt_mpt *mpt;
};

extern struct intel_gvt_host intel_gvt_host;

/* Describe per-platform limitations. */
struct intel_gvt_device_info {
	u32 max_support_vgpus;
	/* This data structure will grow bigger in GVT device model patches */
};

struct intel_vgpu {
	struct intel_gvt *gvt;
	int id;
	unsigned long handle; /* vGPU handle used by hypervisor MPT modules */
};

struct intel_gvt {
	struct mutex lock;
	bool initialized;

	struct drm_i915_private *dev_priv;
	struct idr vgpu_idr;	/* vGPU IDR pool */

	struct intel_gvt_device_info device_info;
};

#include "mpt.h"

#endif
