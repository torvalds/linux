// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_hw_engine.h"

#include <drm/drm_managed.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_execlist.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_topology.h"
#include "xe_hw_fence.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_reg_sr.h"
#include "xe_sched_job.h"
#include "xe_wa.h"

#include "gt/intel_engine_regs.h"
#include "gt/intel_gt_regs.h"
#include "i915_reg.h"

#define MAX_MMIO_BASES 3
struct engine_info {
	const char *name;
	unsigned int class : 8;
	unsigned int instance : 8;
	enum xe_force_wake_domains domain;
	u32 mmio_base;
};

static const struct engine_info engine_infos[] = {
	[XE_HW_ENGINE_RCS0] = {
		.name = "rcs0",
		.class = XE_ENGINE_CLASS_RENDER,
		.instance = 0,
		.domain = XE_FW_RENDER,
		.mmio_base = RENDER_RING_BASE,
	},
	[XE_HW_ENGINE_BCS0] = {
		.name = "bcs0",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 0,
		.domain = XE_FW_RENDER,
		.mmio_base = BLT_RING_BASE,
	},
	[XE_HW_ENGINE_BCS1] = {
		.name = "bcs1",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 1,
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS1_RING_BASE,
	},
	[XE_HW_ENGINE_BCS2] = {
		.name = "bcs2",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 2,
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS2_RING_BASE,
	},
	[XE_HW_ENGINE_BCS3] = {
		.name = "bcs3",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 3,
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS3_RING_BASE,
	},
	[XE_HW_ENGINE_BCS4] = {
		.name = "bcs4",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 4,
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS4_RING_BASE,
	},
	[XE_HW_ENGINE_BCS5] = {
		.name = "bcs5",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 5,
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS5_RING_BASE,
	},
	[XE_HW_ENGINE_BCS6] = {
		.name = "bcs6",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 6,
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS6_RING_BASE,
	},
	[XE_HW_ENGINE_BCS7] = {
		.name = "bcs7",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 7,
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS7_RING_BASE,
	},
	[XE_HW_ENGINE_BCS8] = {
		.name = "bcs8",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 8,
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS8_RING_BASE,
	},

	[XE_HW_ENGINE_VCS0] = {
		.name = "vcs0",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 0,
		.domain = XE_FW_MEDIA_VDBOX0,
		.mmio_base = GEN11_BSD_RING_BASE,
	},
	[XE_HW_ENGINE_VCS1] = {
		.name = "vcs1",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 1,
		.domain = XE_FW_MEDIA_VDBOX1,
		.mmio_base = GEN11_BSD2_RING_BASE,
	},
	[XE_HW_ENGINE_VCS2] = {
		.name = "vcs2",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 2,
		.domain = XE_FW_MEDIA_VDBOX2,
		.mmio_base = GEN11_BSD3_RING_BASE,
	},
	[XE_HW_ENGINE_VCS3] = {
		.name = "vcs3",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 3,
		.domain = XE_FW_MEDIA_VDBOX3,
		.mmio_base = GEN11_BSD4_RING_BASE,
	},
	[XE_HW_ENGINE_VCS4] = {
		.name = "vcs4",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 4,
		.domain = XE_FW_MEDIA_VDBOX4,
		.mmio_base = XEHP_BSD5_RING_BASE,
	},
	[XE_HW_ENGINE_VCS5] = {
		.name = "vcs5",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 5,
		.domain = XE_FW_MEDIA_VDBOX5,
		.mmio_base = XEHP_BSD6_RING_BASE,
	},
	[XE_HW_ENGINE_VCS6] = {
		.name = "vcs6",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 6,
		.domain = XE_FW_MEDIA_VDBOX6,
		.mmio_base = XEHP_BSD7_RING_BASE,
	},
	[XE_HW_ENGINE_VCS7] = {
		.name = "vcs7",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 7,
		.domain = XE_FW_MEDIA_VDBOX7,
		.mmio_base = XEHP_BSD8_RING_BASE,
	},
	[XE_HW_ENGINE_VECS0] = {
		.name = "vecs0",
		.class = XE_ENGINE_CLASS_VIDEO_ENHANCE,
		.instance = 0,
		.domain = XE_FW_MEDIA_VEBOX0,
		.mmio_base = GEN11_VEBOX_RING_BASE,
	},
	[XE_HW_ENGINE_VECS1] = {
		.name = "vecs1",
		.class = XE_ENGINE_CLASS_VIDEO_ENHANCE,
		.instance = 1,
		.domain = XE_FW_MEDIA_VEBOX1,
		.mmio_base = GEN11_VEBOX2_RING_BASE,
	},
	[XE_HW_ENGINE_VECS2] = {
		.name = "vecs2",
		.class = XE_ENGINE_CLASS_VIDEO_ENHANCE,
		.instance = 2,
		.domain = XE_FW_MEDIA_VEBOX2,
		.mmio_base = XEHP_VEBOX3_RING_BASE,
	},
	[XE_HW_ENGINE_VECS3] = {
		.name = "vecs3",
		.class = XE_ENGINE_CLASS_VIDEO_ENHANCE,
		.instance = 3,
		.domain = XE_FW_MEDIA_VEBOX3,
		.mmio_base = XEHP_VEBOX4_RING_BASE,
	},
	[XE_HW_ENGINE_CCS0] = {
		.name = "ccs0",
		.class = XE_ENGINE_CLASS_COMPUTE,
		.instance = 0,
		.domain = XE_FW_RENDER,
		.mmio_base = GEN12_COMPUTE0_RING_BASE,
	},
	[XE_HW_ENGINE_CCS1] = {
		.name = "ccs1",
		.class = XE_ENGINE_CLASS_COMPUTE,
		.instance = 1,
		.domain = XE_FW_RENDER,
		.mmio_base = GEN12_COMPUTE1_RING_BASE,
	},
	[XE_HW_ENGINE_CCS2] = {
		.name = "ccs2",
		.class = XE_ENGINE_CLASS_COMPUTE,
		.instance = 2,
		.domain = XE_FW_RENDER,
		.mmio_base = GEN12_COMPUTE2_RING_BASE,
	},
	[XE_HW_ENGINE_CCS3] = {
		.name = "ccs3",
		.class = XE_ENGINE_CLASS_COMPUTE,
		.instance = 3,
		.domain = XE_FW_RENDER,
		.mmio_base = GEN12_COMPUTE3_RING_BASE,
	},
};

static void hw_engine_fini(struct drm_device *drm, void *arg)
{
	struct xe_hw_engine *hwe = arg;

	if (hwe->exl_port)
		xe_execlist_port_destroy(hwe->exl_port);
	xe_lrc_finish(&hwe->kernel_lrc);

	xe_bo_unpin_map_no_vm(hwe->hwsp);

	hwe->gt = NULL;
}

static void hw_engine_mmio_write32(struct xe_hw_engine *hwe, u32 reg, u32 val)
{
	XE_BUG_ON(reg & hwe->mmio_base);
	xe_force_wake_assert_held(gt_to_fw(hwe->gt), hwe->domain);

	xe_mmio_write32(hwe->gt, reg + hwe->mmio_base, val);
}

static u32 hw_engine_mmio_read32(struct xe_hw_engine *hwe, u32 reg)
{
	XE_BUG_ON(reg & hwe->mmio_base);
	xe_force_wake_assert_held(gt_to_fw(hwe->gt), hwe->domain);

	return xe_mmio_read32(hwe->gt, reg + hwe->mmio_base);
}

void xe_hw_engine_enable_ring(struct xe_hw_engine *hwe)
{
	u32 ccs_mask =
		xe_hw_engine_mask_per_class(hwe->gt, XE_ENGINE_CLASS_COMPUTE);

	if (hwe->class == XE_ENGINE_CLASS_COMPUTE && ccs_mask & BIT(0))
		xe_mmio_write32(hwe->gt, GEN12_RCU_MODE.reg,
				_MASKED_BIT_ENABLE(GEN12_RCU_MODE_CCS_ENABLE));

	hw_engine_mmio_write32(hwe, RING_HWSTAM(0).reg, ~0x0);
	hw_engine_mmio_write32(hwe, RING_HWS_PGA(0).reg,
			       xe_bo_ggtt_addr(hwe->hwsp));
	hw_engine_mmio_write32(hwe, RING_MODE_GEN7(0).reg,
			       _MASKED_BIT_ENABLE(GEN11_GFX_DISABLE_LEGACY_MODE));
	hw_engine_mmio_write32(hwe, RING_MI_MODE(0).reg,
			       _MASKED_BIT_DISABLE(STOP_RING));
	hw_engine_mmio_read32(hwe, RING_MI_MODE(0).reg);
}

static void hw_engine_init_early(struct xe_gt *gt, struct xe_hw_engine *hwe,
				 enum xe_hw_engine_id id)
{
	const struct engine_info *info;

	if (WARN_ON(id >= ARRAY_SIZE(engine_infos) || !engine_infos[id].name))
		return;

	if (!(gt->info.engine_mask & BIT(id)))
		return;

	info = &engine_infos[id];

	XE_BUG_ON(hwe->gt);

	hwe->gt = gt;
	hwe->class = info->class;
	hwe->instance = info->instance;
	hwe->mmio_base = info->mmio_base;
	hwe->domain = info->domain;
	hwe->name = info->name;
	hwe->fence_irq = &gt->fence_irq[info->class];
	hwe->engine_id = id;

	xe_reg_sr_init(&hwe->reg_sr, hwe->name, gt_to_xe(gt));
	xe_wa_process_engine(hwe);

	xe_reg_sr_init(&hwe->reg_whitelist, hwe->name, gt_to_xe(gt));
	xe_reg_whitelist_process_engine(hwe);
}

static int hw_engine_init(struct xe_gt *gt, struct xe_hw_engine *hwe,
			  enum xe_hw_engine_id id)
{
	struct xe_device *xe = gt_to_xe(gt);
	int err;

	XE_BUG_ON(id >= ARRAY_SIZE(engine_infos) || !engine_infos[id].name);
	XE_BUG_ON(!(gt->info.engine_mask & BIT(id)));

	xe_reg_sr_apply_mmio(&hwe->reg_sr, gt);
	xe_reg_sr_apply_whitelist(&hwe->reg_whitelist, hwe->mmio_base, gt);

	hwe->hwsp = xe_bo_create_locked(xe, gt, NULL, SZ_4K, ttm_bo_type_kernel,
					XE_BO_CREATE_VRAM_IF_DGFX(gt) |
					XE_BO_CREATE_GGTT_BIT);
	if (IS_ERR(hwe->hwsp)) {
		err = PTR_ERR(hwe->hwsp);
		goto err_name;
	}

	err = xe_bo_pin(hwe->hwsp);
	if (err)
		goto err_unlock_put_hwsp;

	err = xe_bo_vmap(hwe->hwsp);
	if (err)
		goto err_unpin_hwsp;

	xe_bo_unlock_no_vm(hwe->hwsp);

	err = xe_lrc_init(&hwe->kernel_lrc, hwe, NULL, NULL, SZ_16K);
	if (err)
		goto err_hwsp;

	if (!xe_device_guc_submission_enabled(xe)) {
		hwe->exl_port = xe_execlist_port_create(xe, hwe);
		if (IS_ERR(hwe->exl_port)) {
			err = PTR_ERR(hwe->exl_port);
			goto err_kernel_lrc;
		}
	}

	if (xe_device_guc_submission_enabled(xe))
		xe_hw_engine_enable_ring(hwe);

	/* We reserve the highest BCS instance for USM */
	if (xe->info.supports_usm && hwe->class == XE_ENGINE_CLASS_COPY)
		gt->usm.reserved_bcs_instance = hwe->instance;

	err = drmm_add_action_or_reset(&xe->drm, hw_engine_fini, hwe);
	if (err)
		return err;

	return 0;

err_unpin_hwsp:
	xe_bo_unpin(hwe->hwsp);
err_unlock_put_hwsp:
	xe_bo_unlock_no_vm(hwe->hwsp);
	xe_bo_put(hwe->hwsp);
err_kernel_lrc:
	xe_lrc_finish(&hwe->kernel_lrc);
err_hwsp:
	xe_bo_put(hwe->hwsp);
err_name:
	hwe->name = NULL;

	return err;
}

static void hw_engine_setup_logical_mapping(struct xe_gt *gt)
{
	int class;

	/* FIXME: Doing a simple logical mapping that works for most hardware */
	for (class = 0; class < XE_ENGINE_CLASS_MAX; ++class) {
		struct xe_hw_engine *hwe;
		enum xe_hw_engine_id id;
		int logical_instance = 0;

		for_each_hw_engine(hwe, gt, id)
			if (hwe->class == class)
				hwe->logical_instance = logical_instance++;
	}
}

static void read_fuses(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 media_fuse;
	u16 vdbox_mask;
	u16 vebox_mask;
	u32 bcs_mask;
	int i, j;

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	/*
	 * FIXME: Hack job, thinking we should have table of vfuncs for each
	 * class which picks the correct vfunc based on IP version.
	 */

	media_fuse = xe_mmio_read32(gt, GEN11_GT_VEBOX_VDBOX_DISABLE.reg);
	if (GRAPHICS_VERx100(xe) < 1250)
		media_fuse = ~media_fuse;

	vdbox_mask = media_fuse & GEN11_GT_VDBOX_DISABLE_MASK;
	vebox_mask = (media_fuse & GEN11_GT_VEBOX_DISABLE_MASK) >>
		      GEN11_GT_VEBOX_DISABLE_SHIFT;

	for (i = XE_HW_ENGINE_VCS0, j = 0; i <= XE_HW_ENGINE_VCS7; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		if (!(BIT(j) & vdbox_mask)) {
			gt->info.engine_mask &= ~BIT(i);
			drm_info(&xe->drm, "vcs%u fused off\n", j);
		}
	}

	for (i = XE_HW_ENGINE_VECS0, j = 0; i <= XE_HW_ENGINE_VECS3; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		if (!(BIT(j) & vebox_mask)) {
			gt->info.engine_mask &= ~BIT(i);
			drm_info(&xe->drm, "vecs%u fused off\n", j);
		}
	}

	bcs_mask = xe_mmio_read32(gt, GEN10_MIRROR_FUSE3.reg);
	bcs_mask = REG_FIELD_GET(GEN12_MEML3_EN_MASK, bcs_mask);

	for (i = XE_HW_ENGINE_BCS1, j = 0; i <= XE_HW_ENGINE_BCS8; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		if (!(BIT(j/2) & bcs_mask)) {
			gt->info.engine_mask &= ~BIT(i);
			drm_info(&xe->drm, "bcs%u fused off\n", j);
		}
	}

	/* TODO: compute engines */
}

int xe_hw_engines_init_early(struct xe_gt *gt)
{
	int i;

	read_fuses(gt);

	for (i = 0; i < ARRAY_SIZE(gt->hw_engines); i++)
		hw_engine_init_early(gt, &gt->hw_engines[i], i);

	return 0;
}

int xe_hw_engines_init(struct xe_gt *gt)
{
	int err;
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	for_each_hw_engine(hwe, gt, id) {
		err = hw_engine_init(gt, hwe, id);
		if (err)
			return err;
	}

	hw_engine_setup_logical_mapping(gt);

	return 0;
}

void xe_hw_engine_handle_irq(struct xe_hw_engine *hwe, u16 intr_vec)
{
	wake_up_all(&gt_to_xe(hwe->gt)->ufence_wq);

	if (hwe->irq_handler)
		hwe->irq_handler(hwe, intr_vec);

	if (intr_vec & GT_RENDER_USER_INTERRUPT)
		xe_hw_fence_irq_run(hwe->fence_irq);
}

void xe_hw_engine_print_state(struct xe_hw_engine *hwe, struct drm_printer *p)
{
	if (!xe_hw_engine_is_valid(hwe))
		return;

	drm_printf(p, "%s (physical), logical instance=%d\n", hwe->name,
		hwe->logical_instance);
	drm_printf(p, "\tForcewake: domain 0x%x, ref %d\n",
		hwe->domain,
		xe_force_wake_ref(gt_to_fw(hwe->gt), hwe->domain));
	drm_printf(p, "\tMMIO base: 0x%08x\n", hwe->mmio_base);

	drm_printf(p, "\tHWSTAM: 0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_HWSTAM(0).reg));
	drm_printf(p, "\tRING_HWS_PGA: 0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_HWS_PGA(0).reg));

	drm_printf(p, "\tRING_EXECLIST_STATUS_LO: 0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_EXECLIST_STATUS_LO(0).reg));
	drm_printf(p, "\tRING_EXECLIST_STATUS_HI: 0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_EXECLIST_STATUS_HI(0).reg));
	drm_printf(p, "\tRING_EXECLIST_SQ_CONTENTS_LO: 0x%08x\n",
		hw_engine_mmio_read32(hwe,
					 RING_EXECLIST_SQ_CONTENTS(0).reg));
	drm_printf(p, "\tRING_EXECLIST_SQ_CONTENTS_HI: 0x%08x\n",
		hw_engine_mmio_read32(hwe,
					 RING_EXECLIST_SQ_CONTENTS(0).reg) + 4);
	drm_printf(p, "\tRING_EXECLIST_CONTROL: 0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_EXECLIST_CONTROL(0).reg));

	drm_printf(p, "\tRING_START: 0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_START(0).reg));
	drm_printf(p, "\tRING_HEAD:  0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_HEAD(0).reg) & HEAD_ADDR);
	drm_printf(p, "\tRING_TAIL:  0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_TAIL(0).reg) & TAIL_ADDR);
	drm_printf(p, "\tRING_CTL: 0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_CTL(0).reg));
	drm_printf(p, "\tRING_MODE: 0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_MI_MODE(0).reg));
	drm_printf(p, "\tRING_MODE_GEN7: 0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_MODE_GEN7(0).reg));

	drm_printf(p, "\tRING_IMR:   0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_IMR(0).reg));
	drm_printf(p, "\tRING_ESR:   0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_ESR(0).reg));
	drm_printf(p, "\tRING_EMR:   0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_EMR(0).reg));
	drm_printf(p, "\tRING_EIR:   0x%08x\n",
		hw_engine_mmio_read32(hwe, RING_EIR(0).reg));

        drm_printf(p, "\tACTHD:  0x%08x_%08x\n",
		hw_engine_mmio_read32(hwe, RING_ACTHD_UDW(0).reg),
		hw_engine_mmio_read32(hwe, RING_ACTHD(0).reg));
        drm_printf(p, "\tBBADDR: 0x%08x_%08x\n",
		hw_engine_mmio_read32(hwe, RING_BBADDR_UDW(0).reg),
		hw_engine_mmio_read32(hwe, RING_BBADDR(0).reg));
        drm_printf(p, "\tDMA_FADDR: 0x%08x_%08x\n",
		hw_engine_mmio_read32(hwe, RING_DMA_FADD_UDW(0).reg),
		hw_engine_mmio_read32(hwe, RING_DMA_FADD(0).reg));

	drm_printf(p, "\tIPEIR: 0x%08x\n",
		hw_engine_mmio_read32(hwe, IPEIR(0).reg));
	drm_printf(p, "\tIPEHR: 0x%08x\n\n",
		hw_engine_mmio_read32(hwe, IPEHR(0).reg));

	if (hwe->class == XE_ENGINE_CLASS_COMPUTE)
		drm_printf(p, "\tGEN12_RCU_MODE: 0x%08x\n",
			xe_mmio_read32(hwe->gt, GEN12_RCU_MODE.reg));

}

u32 xe_hw_engine_mask_per_class(struct xe_gt *gt,
				enum xe_engine_class engine_class)
{
	u32 mask = 0;
	enum xe_hw_engine_id id;

	for (id = 0; id < XE_NUM_HW_ENGINES; ++id) {
		if (engine_infos[id].class == engine_class &&
		    gt->info.engine_mask & BIT(id))
			mask |= BIT(engine_infos[id].instance);
	}
	return mask;
}

bool xe_hw_engine_is_reserved(struct xe_hw_engine *hwe)
{
	struct xe_gt *gt = hwe->gt;
	struct xe_device *xe = gt_to_xe(gt);

	return xe->info.supports_usm && hwe->class == XE_ENGINE_CLASS_COPY &&
		hwe->instance == gt->usm.reserved_bcs_instance;
}
