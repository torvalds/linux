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
 *    Kevin Tian <kevin.tian@intel.com>
 *    Eddie Dong <eddie.dong@intel.com>
 *
 * Contributors:
 *    Niu Bing <bing.niu@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#ifndef _GVT_H_
#define _GVT_H_

#include "debug.h"
#include "hypercall.h"
#include "mmio.h"

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
	u32 mmio_size;
};

/* GM resources owned by a vGPU */
struct intel_vgpu_gm {
	u64 aperture_sz;
	u64 hidden_sz;
	struct drm_mm_node low_gm_node;
	struct drm_mm_node high_gm_node;
};

#define INTEL_GVT_MAX_NUM_FENCES 32

/* Fences owned by a vGPU */
struct intel_vgpu_fence {
	struct drm_i915_fence_reg *regs[INTEL_GVT_MAX_NUM_FENCES];
	u32 base;
	u32 size;
};

struct intel_vgpu {
	struct intel_gvt *gvt;
	int id;
	unsigned long handle; /* vGPU handle used by hypervisor MPT modules */

	struct intel_vgpu_fence fence;
	struct intel_vgpu_gm gm;
};

struct intel_gvt_gm {
	unsigned long vgpu_allocated_low_gm_size;
	unsigned long vgpu_allocated_high_gm_size;
};

struct intel_gvt_fence {
	unsigned long vgpu_allocated_fence_num;
};

#define INTEL_GVT_MMIO_HASH_BITS 9

struct intel_gvt_mmio {
	u32 *mmio_attribute;
	DECLARE_HASHTABLE(mmio_info_table, INTEL_GVT_MMIO_HASH_BITS);
};

struct intel_gvt {
	struct mutex lock;
	bool initialized;

	struct drm_i915_private *dev_priv;
	struct idr vgpu_idr;	/* vGPU IDR pool */

	struct intel_gvt_device_info device_info;
	struct intel_gvt_gm gm;
	struct intel_gvt_fence fence;
	struct intel_gvt_mmio mmio;
};

/* Aperture/GM space definitions for GVT device */
#define gvt_aperture_sz(gvt)	  (gvt->dev_priv->ggtt.mappable_end)
#define gvt_aperture_pa_base(gvt) (gvt->dev_priv->ggtt.mappable_base)

#define gvt_ggtt_gm_sz(gvt)	  (gvt->dev_priv->ggtt.base.total)
#define gvt_hidden_sz(gvt)	  (gvt_ggtt_gm_sz(gvt) - gvt_aperture_sz(gvt))

#define gvt_aperture_gmadr_base(gvt) (0)
#define gvt_aperture_gmadr_end(gvt) (gvt_aperture_gmadr_base(gvt) \
				     + gvt_aperture_sz(gvt) - 1)

#define gvt_hidden_gmadr_base(gvt) (gvt_aperture_gmadr_base(gvt) \
				    + gvt_aperture_sz(gvt))
#define gvt_hidden_gmadr_end(gvt) (gvt_hidden_gmadr_base(gvt) \
				   + gvt_hidden_sz(gvt) - 1)

#define gvt_fence_sz(gvt) (gvt->dev_priv->num_fence_regs)

/* Aperture/GM space definitions for vGPU */
#define vgpu_aperture_offset(vgpu)	((vgpu)->gm.low_gm_node.start)
#define vgpu_hidden_offset(vgpu)	((vgpu)->gm.high_gm_node.start)
#define vgpu_aperture_sz(vgpu)		((vgpu)->gm.aperture_sz)
#define vgpu_hidden_sz(vgpu)		((vgpu)->gm.hidden_sz)

#define vgpu_aperture_pa_base(vgpu) \
	(gvt_aperture_pa_base(vgpu->gvt) + vgpu_aperture_offset(vgpu))

#define vgpu_ggtt_gm_sz(vgpu) ((vgpu)->gm.aperture_sz + (vgpu)->gm.hidden_sz)

#define vgpu_aperture_pa_end(vgpu) \
	(vgpu_aperture_pa_base(vgpu) + vgpu_aperture_sz(vgpu) - 1)

#define vgpu_aperture_gmadr_base(vgpu) (vgpu_aperture_offset(vgpu))
#define vgpu_aperture_gmadr_end(vgpu) \
	(vgpu_aperture_gmadr_base(vgpu) + vgpu_aperture_sz(vgpu) - 1)

#define vgpu_hidden_gmadr_base(vgpu) (vgpu_hidden_offset(vgpu))
#define vgpu_hidden_gmadr_end(vgpu) \
	(vgpu_hidden_gmadr_base(vgpu) + vgpu_hidden_sz(vgpu) - 1)

#define vgpu_fence_base(vgpu) (vgpu->fence.base)
#define vgpu_fence_sz(vgpu) (vgpu->fence.size)

struct intel_vgpu_creation_params {
	__u64 handle;
	__u64 low_gm_sz;  /* in MB */
	__u64 high_gm_sz; /* in MB */
	__u64 fence_sz;
	__s32 primary;
	__u64 vgpu_id;
};

int intel_vgpu_alloc_resource(struct intel_vgpu *vgpu,
			      struct intel_vgpu_creation_params *param);
void intel_vgpu_free_resource(struct intel_vgpu *vgpu);
void intel_vgpu_write_fence(struct intel_vgpu *vgpu,
	u32 fence, u64 value);

#include "mpt.h"

#endif
