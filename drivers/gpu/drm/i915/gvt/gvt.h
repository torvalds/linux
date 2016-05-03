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
#include "reg.h"
#include "interrupt.h"
#include "gtt.h"
#include "display.h"
#include "edid.h"
#include "execlist.h"
#include "scheduler.h"
#include "sched_policy.h"
#include "render.h"
#include "cmd_parser.h"

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
	u32 cfg_space_size;
	u32 mmio_size;
	u32 mmio_bar;
	unsigned long msi_cap_offset;
	u32 gtt_start_offset;
	u32 gtt_entry_size;
	u32 gtt_entry_size_shift;
	int gmadr_bytes_in_cmd;
	u32 max_surface_size;
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

struct intel_vgpu_mmio {
	void *vreg;
	void *sreg;
	bool disable_warn_untrack;
};

#define INTEL_GVT_MAX_CFG_SPACE_SZ 256
#define INTEL_GVT_MAX_BAR_NUM 4

struct intel_vgpu_pci_bar {
	u64 size;
	bool tracked;
};

struct intel_vgpu_cfg_space {
	unsigned char virtual_cfg_space[INTEL_GVT_MAX_CFG_SPACE_SZ];
	struct intel_vgpu_pci_bar bar[INTEL_GVT_MAX_BAR_NUM];
};

#define vgpu_cfg_space(vgpu) ((vgpu)->cfg_space.virtual_cfg_space)

#define INTEL_GVT_MAX_PIPE 4

struct intel_vgpu_irq {
	bool irq_warn_once[INTEL_GVT_EVENT_MAX];
	DECLARE_BITMAP(flip_done_event[INTEL_GVT_MAX_PIPE],
		       INTEL_GVT_EVENT_MAX);
};

struct intel_vgpu_opregion {
	void *va;
	u32 gfn[INTEL_GVT_OPREGION_PAGES];
	struct page *pages[INTEL_GVT_OPREGION_PAGES];
};

#define vgpu_opregion(vgpu) (&(vgpu->opregion))

#define INTEL_GVT_MAX_PORT 5

struct intel_vgpu_display {
	struct intel_vgpu_i2c_edid i2c_edid;
	struct intel_vgpu_port ports[INTEL_GVT_MAX_PORT];
	struct intel_vgpu_sbi sbi;
};

struct intel_vgpu {
	struct intel_gvt *gvt;
	int id;
	unsigned long handle; /* vGPU handle used by hypervisor MPT modules */
	bool active;
	bool resetting;
	void *sched_data;

	struct intel_vgpu_fence fence;
	struct intel_vgpu_gm gm;
	struct intel_vgpu_cfg_space cfg_space;
	struct intel_vgpu_mmio mmio;
	struct intel_vgpu_irq irq;
	struct intel_vgpu_gtt gtt;
	struct intel_vgpu_opregion opregion;
	struct intel_vgpu_display display;
	struct intel_vgpu_execlist execlist[I915_NUM_ENGINES];
	struct list_head workload_q_head[I915_NUM_ENGINES];
	struct kmem_cache *workloads;
	atomic_t running_workload_num;
	DECLARE_BITMAP(tlb_handle_pending, I915_NUM_ENGINES);
	struct i915_gem_context *shadow_ctx;
	struct notifier_block shadow_ctx_notifier_block;
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

struct intel_gvt_firmware {
	void *cfg_space;
	void *mmio;
	bool firmware_loaded;
};

struct intel_gvt_opregion {
	void *opregion_va;
	u32 opregion_pa;
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
	struct intel_gvt_firmware firmware;
	struct intel_gvt_irq irq;
	struct intel_gvt_gtt gtt;
	struct intel_gvt_opregion opregion;
	struct intel_gvt_workload_scheduler scheduler;
	DECLARE_HASHTABLE(cmd_table, GVT_CMD_HASH_BITS);

	struct task_struct *service_thread;
	wait_queue_head_t service_thread_wq;
	unsigned long service_request;
};

enum {
	INTEL_GVT_REQUEST_EMULATE_VBLANK = 0,
};

static inline void intel_gvt_request_service(struct intel_gvt *gvt,
		int service)
{
	set_bit(service, (void *)&gvt->service_request);
	wake_up(&gvt->service_thread_wq);
}

void intel_gvt_free_firmware(struct intel_gvt *gvt);
int intel_gvt_load_firmware(struct intel_gvt *gvt);

/* Aperture/GM space definitions for GVT device */
#define gvt_aperture_sz(gvt)	  (gvt->dev_priv->ggtt.mappable_end)
#define gvt_aperture_pa_base(gvt) (gvt->dev_priv->ggtt.mappable_base)

#define gvt_ggtt_gm_sz(gvt)	  (gvt->dev_priv->ggtt.base.total)
#define gvt_ggtt_sz(gvt) \
	((gvt->dev_priv->ggtt.base.total >> PAGE_SHIFT) << 3)
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

/* Macros for easily accessing vGPU virtual/shadow register */
#define vgpu_vreg(vgpu, reg) \
	(*(u32 *)(vgpu->mmio.vreg + INTEL_GVT_MMIO_OFFSET(reg)))
#define vgpu_vreg8(vgpu, reg) \
	(*(u8 *)(vgpu->mmio.vreg + INTEL_GVT_MMIO_OFFSET(reg)))
#define vgpu_vreg16(vgpu, reg) \
	(*(u16 *)(vgpu->mmio.vreg + INTEL_GVT_MMIO_OFFSET(reg)))
#define vgpu_vreg64(vgpu, reg) \
	(*(u64 *)(vgpu->mmio.vreg + INTEL_GVT_MMIO_OFFSET(reg)))
#define vgpu_sreg(vgpu, reg) \
	(*(u32 *)(vgpu->mmio.sreg + INTEL_GVT_MMIO_OFFSET(reg)))
#define vgpu_sreg8(vgpu, reg) \
	(*(u8 *)(vgpu->mmio.sreg + INTEL_GVT_MMIO_OFFSET(reg)))
#define vgpu_sreg16(vgpu, reg) \
	(*(u16 *)(vgpu->mmio.sreg + INTEL_GVT_MMIO_OFFSET(reg)))
#define vgpu_sreg64(vgpu, reg) \
	(*(u64 *)(vgpu->mmio.sreg + INTEL_GVT_MMIO_OFFSET(reg)))

#define for_each_active_vgpu(gvt, vgpu, id) \
	idr_for_each_entry((&(gvt)->vgpu_idr), (vgpu), (id)) \
		for_each_if(vgpu->active)

static inline void intel_vgpu_write_pci_bar(struct intel_vgpu *vgpu,
					    u32 offset, u32 val, bool low)
{
	u32 *pval;

	/* BAR offset should be 32 bits algiend */
	offset = rounddown(offset, 4);
	pval = (u32 *)(vgpu_cfg_space(vgpu) + offset);

	if (low) {
		/*
		 * only update bit 31 - bit 4,
		 * leave the bit 3 - bit 0 unchanged.
		 */
		*pval = (val & GENMASK(31, 4)) | (*pval & GENMASK(3, 0));
	}
}

struct intel_vgpu *intel_gvt_create_vgpu(struct intel_gvt *gvt,
					 struct intel_vgpu_creation_params *
					 param);

void intel_gvt_destroy_vgpu(struct intel_vgpu *vgpu);

/* validating GM functions */
#define vgpu_gmadr_is_aperture(vgpu, gmadr) \
	((gmadr >= vgpu_aperture_gmadr_base(vgpu)) && \
	 (gmadr <= vgpu_aperture_gmadr_end(vgpu)))

#define vgpu_gmadr_is_hidden(vgpu, gmadr) \
	((gmadr >= vgpu_hidden_gmadr_base(vgpu)) && \
	 (gmadr <= vgpu_hidden_gmadr_end(vgpu)))

#define vgpu_gmadr_is_valid(vgpu, gmadr) \
	 ((vgpu_gmadr_is_aperture(vgpu, gmadr) || \
	  (vgpu_gmadr_is_hidden(vgpu, gmadr))))

#define gvt_gmadr_is_aperture(gvt, gmadr) \
	 ((gmadr >= gvt_aperture_gmadr_base(gvt)) && \
	  (gmadr <= gvt_aperture_gmadr_end(gvt)))

#define gvt_gmadr_is_hidden(gvt, gmadr) \
	  ((gmadr >= gvt_hidden_gmadr_base(gvt)) && \
	   (gmadr <= gvt_hidden_gmadr_end(gvt)))

#define gvt_gmadr_is_valid(gvt, gmadr) \
	  (gvt_gmadr_is_aperture(gvt, gmadr) || \
	    gvt_gmadr_is_hidden(gvt, gmadr))

bool intel_gvt_ggtt_validate_range(struct intel_vgpu *vgpu, u64 addr, u32 size);
int intel_gvt_ggtt_gmadr_g2h(struct intel_vgpu *vgpu, u64 g_addr, u64 *h_addr);
int intel_gvt_ggtt_gmadr_h2g(struct intel_vgpu *vgpu, u64 h_addr, u64 *g_addr);
int intel_gvt_ggtt_index_g2h(struct intel_vgpu *vgpu, unsigned long g_index,
			     unsigned long *h_index);
int intel_gvt_ggtt_h2g_index(struct intel_vgpu *vgpu, unsigned long h_index,
			     unsigned long *g_index);

int intel_vgpu_emulate_cfg_read(void *__vgpu, unsigned int offset,
		void *p_data, unsigned int bytes);

int intel_vgpu_emulate_cfg_write(void *__vgpu, unsigned int offset,
		void *p_data, unsigned int bytes);

void intel_gvt_clean_opregion(struct intel_gvt *gvt);
int intel_gvt_init_opregion(struct intel_gvt *gvt);

void intel_vgpu_clean_opregion(struct intel_vgpu *vgpu);
int intel_vgpu_init_opregion(struct intel_vgpu *vgpu, u32 gpa);

int intel_vgpu_emulate_opregion_request(struct intel_vgpu *vgpu, u32 swsci);

#include "mpt.h"

#endif
