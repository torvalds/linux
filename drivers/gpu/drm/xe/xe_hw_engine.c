// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_hw_engine.h"

#include <linux/nospec.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <uapi/drm/xe_drm.h>
#include <generated/xe_wa_oob.h>

#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_irq_regs.h"
#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_configfs.h"
#include "xe_device.h"
#include "xe_execlist.h"
#include "xe_force_wake.h"
#include "xe_gsc.h"
#include "xe_gt.h"
#include "xe_gt_ccs_mode.h"
#include "xe_gt_clock.h"
#include "xe_gt_printk.h"
#include "xe_gt_mcr.h"
#include "xe_gt_topology.h"
#include "xe_guc_capture.h"
#include "xe_hw_engine_group.h"
#include "xe_hw_fence.h"
#include "xe_irq.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_reg_sr.h"
#include "xe_reg_whitelist.h"
#include "xe_rtp.h"
#include "xe_sched_job.h"
#include "xe_sriov.h"
#include "xe_tuning.h"
#include "xe_uc_fw.h"
#include "xe_wa.h"

#define MAX_MMIO_BASES 3
struct engine_info {
	const char *name;
	unsigned int class : 8;
	unsigned int instance : 8;
	unsigned int irq_offset : 8;
	enum xe_force_wake_domains domain;
	u32 mmio_base;
};

static const struct engine_info engine_infos[] = {
	[XE_HW_ENGINE_RCS0] = {
		.name = "rcs0",
		.class = XE_ENGINE_CLASS_RENDER,
		.instance = 0,
		.irq_offset = ilog2(INTR_RCS0),
		.domain = XE_FW_RENDER,
		.mmio_base = RENDER_RING_BASE,
	},
	[XE_HW_ENGINE_BCS0] = {
		.name = "bcs0",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 0,
		.irq_offset = ilog2(INTR_BCS(0)),
		.domain = XE_FW_RENDER,
		.mmio_base = BLT_RING_BASE,
	},
	[XE_HW_ENGINE_BCS1] = {
		.name = "bcs1",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 1,
		.irq_offset = ilog2(INTR_BCS(1)),
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS1_RING_BASE,
	},
	[XE_HW_ENGINE_BCS2] = {
		.name = "bcs2",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 2,
		.irq_offset = ilog2(INTR_BCS(2)),
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS2_RING_BASE,
	},
	[XE_HW_ENGINE_BCS3] = {
		.name = "bcs3",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 3,
		.irq_offset = ilog2(INTR_BCS(3)),
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS3_RING_BASE,
	},
	[XE_HW_ENGINE_BCS4] = {
		.name = "bcs4",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 4,
		.irq_offset = ilog2(INTR_BCS(4)),
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS4_RING_BASE,
	},
	[XE_HW_ENGINE_BCS5] = {
		.name = "bcs5",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 5,
		.irq_offset = ilog2(INTR_BCS(5)),
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS5_RING_BASE,
	},
	[XE_HW_ENGINE_BCS6] = {
		.name = "bcs6",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 6,
		.irq_offset = ilog2(INTR_BCS(6)),
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS6_RING_BASE,
	},
	[XE_HW_ENGINE_BCS7] = {
		.name = "bcs7",
		.class = XE_ENGINE_CLASS_COPY,
		.irq_offset = ilog2(INTR_BCS(7)),
		.instance = 7,
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS7_RING_BASE,
	},
	[XE_HW_ENGINE_BCS8] = {
		.name = "bcs8",
		.class = XE_ENGINE_CLASS_COPY,
		.instance = 8,
		.irq_offset = ilog2(INTR_BCS8),
		.domain = XE_FW_RENDER,
		.mmio_base = XEHPC_BCS8_RING_BASE,
	},

	[XE_HW_ENGINE_VCS0] = {
		.name = "vcs0",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 0,
		.irq_offset = 32 + ilog2(INTR_VCS(0)),
		.domain = XE_FW_MEDIA_VDBOX0,
		.mmio_base = BSD_RING_BASE,
	},
	[XE_HW_ENGINE_VCS1] = {
		.name = "vcs1",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 1,
		.irq_offset = 32 + ilog2(INTR_VCS(1)),
		.domain = XE_FW_MEDIA_VDBOX1,
		.mmio_base = BSD2_RING_BASE,
	},
	[XE_HW_ENGINE_VCS2] = {
		.name = "vcs2",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 2,
		.irq_offset = 32 + ilog2(INTR_VCS(2)),
		.domain = XE_FW_MEDIA_VDBOX2,
		.mmio_base = BSD3_RING_BASE,
	},
	[XE_HW_ENGINE_VCS3] = {
		.name = "vcs3",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 3,
		.irq_offset = 32 + ilog2(INTR_VCS(3)),
		.domain = XE_FW_MEDIA_VDBOX3,
		.mmio_base = BSD4_RING_BASE,
	},
	[XE_HW_ENGINE_VCS4] = {
		.name = "vcs4",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 4,
		.irq_offset = 32 + ilog2(INTR_VCS(4)),
		.domain = XE_FW_MEDIA_VDBOX4,
		.mmio_base = XEHP_BSD5_RING_BASE,
	},
	[XE_HW_ENGINE_VCS5] = {
		.name = "vcs5",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 5,
		.irq_offset = 32 + ilog2(INTR_VCS(5)),
		.domain = XE_FW_MEDIA_VDBOX5,
		.mmio_base = XEHP_BSD6_RING_BASE,
	},
	[XE_HW_ENGINE_VCS6] = {
		.name = "vcs6",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 6,
		.irq_offset = 32 + ilog2(INTR_VCS(6)),
		.domain = XE_FW_MEDIA_VDBOX6,
		.mmio_base = XEHP_BSD7_RING_BASE,
	},
	[XE_HW_ENGINE_VCS7] = {
		.name = "vcs7",
		.class = XE_ENGINE_CLASS_VIDEO_DECODE,
		.instance = 7,
		.irq_offset = 32 + ilog2(INTR_VCS(7)),
		.domain = XE_FW_MEDIA_VDBOX7,
		.mmio_base = XEHP_BSD8_RING_BASE,
	},
	[XE_HW_ENGINE_VECS0] = {
		.name = "vecs0",
		.class = XE_ENGINE_CLASS_VIDEO_ENHANCE,
		.instance = 0,
		.irq_offset = 32 + ilog2(INTR_VECS(0)),
		.domain = XE_FW_MEDIA_VEBOX0,
		.mmio_base = VEBOX_RING_BASE,
	},
	[XE_HW_ENGINE_VECS1] = {
		.name = "vecs1",
		.class = XE_ENGINE_CLASS_VIDEO_ENHANCE,
		.instance = 1,
		.irq_offset = 32 + ilog2(INTR_VECS(1)),
		.domain = XE_FW_MEDIA_VEBOX1,
		.mmio_base = VEBOX2_RING_BASE,
	},
	[XE_HW_ENGINE_VECS2] = {
		.name = "vecs2",
		.class = XE_ENGINE_CLASS_VIDEO_ENHANCE,
		.instance = 2,
		.irq_offset = 32 + ilog2(INTR_VECS(2)),
		.domain = XE_FW_MEDIA_VEBOX2,
		.mmio_base = XEHP_VEBOX3_RING_BASE,
	},
	[XE_HW_ENGINE_VECS3] = {
		.name = "vecs3",
		.class = XE_ENGINE_CLASS_VIDEO_ENHANCE,
		.instance = 3,
		.irq_offset = 32 + ilog2(INTR_VECS(3)),
		.domain = XE_FW_MEDIA_VEBOX3,
		.mmio_base = XEHP_VEBOX4_RING_BASE,
	},
	[XE_HW_ENGINE_CCS0] = {
		.name = "ccs0",
		.class = XE_ENGINE_CLASS_COMPUTE,
		.instance = 0,
		.irq_offset = ilog2(INTR_CCS(0)),
		.domain = XE_FW_RENDER,
		.mmio_base = COMPUTE0_RING_BASE,
	},
	[XE_HW_ENGINE_CCS1] = {
		.name = "ccs1",
		.class = XE_ENGINE_CLASS_COMPUTE,
		.instance = 1,
		.irq_offset = ilog2(INTR_CCS(1)),
		.domain = XE_FW_RENDER,
		.mmio_base = COMPUTE1_RING_BASE,
	},
	[XE_HW_ENGINE_CCS2] = {
		.name = "ccs2",
		.class = XE_ENGINE_CLASS_COMPUTE,
		.instance = 2,
		.irq_offset = ilog2(INTR_CCS(2)),
		.domain = XE_FW_RENDER,
		.mmio_base = COMPUTE2_RING_BASE,
	},
	[XE_HW_ENGINE_CCS3] = {
		.name = "ccs3",
		.class = XE_ENGINE_CLASS_COMPUTE,
		.instance = 3,
		.irq_offset = ilog2(INTR_CCS(3)),
		.domain = XE_FW_RENDER,
		.mmio_base = COMPUTE3_RING_BASE,
	},
	[XE_HW_ENGINE_GSCCS0] = {
		.name = "gsccs0",
		.class = XE_ENGINE_CLASS_OTHER,
		.instance = OTHER_GSC_INSTANCE,
		.domain = XE_FW_GSC,
		.mmio_base = GSCCS_RING_BASE,
	},
};

static void hw_engine_fini(void *arg)
{
	struct xe_hw_engine *hwe = arg;

	if (hwe->exl_port)
		xe_execlist_port_destroy(hwe->exl_port);

	hwe->gt = NULL;
}

/**
 * xe_hw_engine_mmio_write32() - Write engine register
 * @hwe: engine
 * @reg: register to write into
 * @val: desired 32-bit value to write
 *
 * This function will write val into an engine specific register.
 * Forcewake must be held by the caller.
 *
 */
void xe_hw_engine_mmio_write32(struct xe_hw_engine *hwe,
			       struct xe_reg reg, u32 val)
{
	xe_gt_assert(hwe->gt, !(reg.addr & hwe->mmio_base));
	xe_force_wake_assert_held(gt_to_fw(hwe->gt), hwe->domain);

	reg.addr += hwe->mmio_base;

	xe_mmio_write32(&hwe->gt->mmio, reg, val);
}

/**
 * xe_hw_engine_mmio_read32() - Read engine register
 * @hwe: engine
 * @reg: register to read from
 *
 * This function will read from an engine specific register.
 * Forcewake must be held by the caller.
 *
 * Return: value of the 32-bit register.
 */
u32 xe_hw_engine_mmio_read32(struct xe_hw_engine *hwe, struct xe_reg reg)
{
	xe_gt_assert(hwe->gt, !(reg.addr & hwe->mmio_base));
	xe_force_wake_assert_held(gt_to_fw(hwe->gt), hwe->domain);

	reg.addr += hwe->mmio_base;

	return xe_mmio_read32(&hwe->gt->mmio, reg);
}

void xe_hw_engine_enable_ring(struct xe_hw_engine *hwe)
{
	u32 ccs_mask =
		xe_hw_engine_mask_per_class(hwe->gt, XE_ENGINE_CLASS_COMPUTE);
	u32 ring_mode = _MASKED_BIT_ENABLE(GFX_DISABLE_LEGACY_MODE);

	if (hwe->class == XE_ENGINE_CLASS_COMPUTE && ccs_mask)
		xe_mmio_write32(&hwe->gt->mmio, RCU_MODE,
				_MASKED_BIT_ENABLE(RCU_MODE_CCS_ENABLE));

	xe_hw_engine_mmio_write32(hwe, RING_HWSTAM(0), ~0x0);
	xe_hw_engine_mmio_write32(hwe, RING_HWS_PGA(0),
				  xe_bo_ggtt_addr(hwe->hwsp));

	if (xe_device_has_msix(gt_to_xe(hwe->gt)))
		ring_mode |= _MASKED_BIT_ENABLE(GFX_MSIX_INTERRUPT_ENABLE);
	xe_hw_engine_mmio_write32(hwe, RING_MODE(0), ring_mode);
	xe_hw_engine_mmio_write32(hwe, RING_MI_MODE(0),
				  _MASKED_BIT_DISABLE(STOP_RING));
	xe_hw_engine_mmio_read32(hwe, RING_MI_MODE(0));
}

static bool xe_hw_engine_match_fixed_cslice_mode(const struct xe_gt *gt,
						 const struct xe_hw_engine *hwe)
{
	return xe_gt_ccs_mode_enabled(gt) &&
	       xe_rtp_match_first_render_or_compute(gt, hwe);
}

static bool xe_rtp_cfeg_wmtp_disabled(const struct xe_gt *gt,
				      const struct xe_hw_engine *hwe)
{
	if (GRAPHICS_VER(gt_to_xe(gt)) < 20)
		return false;

	if (hwe->class != XE_ENGINE_CLASS_COMPUTE &&
	    hwe->class != XE_ENGINE_CLASS_RENDER)
		return false;

	return xe_mmio_read32(&hwe->gt->mmio, XEHP_FUSE4) & CFEG_WMTP_DISABLE;
}

void
xe_hw_engine_setup_default_lrc_state(struct xe_hw_engine *hwe)
{
	struct xe_gt *gt = hwe->gt;
	const u8 mocs_write_idx = gt->mocs.uc_index;
	const u8 mocs_read_idx = gt->mocs.uc_index;
	u32 blit_cctl_val = REG_FIELD_PREP(BLIT_CCTL_DST_MOCS_MASK, mocs_write_idx) |
			    REG_FIELD_PREP(BLIT_CCTL_SRC_MOCS_MASK, mocs_read_idx);
	struct xe_rtp_process_ctx ctx = XE_RTP_PROCESS_CTX_INITIALIZER(hwe);
	const struct xe_rtp_entry_sr lrc_setup[] = {
		/*
		 * Some blitter commands do not have a field for MOCS, those
		 * commands will use MOCS index pointed by BLIT_CCTL.
		 * BLIT_CCTL registers are needed to be programmed to un-cached.
		 */
		{ XE_RTP_NAME("BLIT_CCTL_default_MOCS"),
		  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, XE_RTP_END_VERSION_UNDEFINED),
			       ENGINE_CLASS(COPY)),
		  XE_RTP_ACTIONS(FIELD_SET(BLIT_CCTL(0),
				 BLIT_CCTL_DST_MOCS_MASK |
				 BLIT_CCTL_SRC_MOCS_MASK,
				 blit_cctl_val,
				 XE_RTP_ACTION_FLAG(ENGINE_BASE)))
		},
		/* Disable WMTP if HW doesn't support it */
		{ XE_RTP_NAME("DISABLE_WMTP_ON_UNSUPPORTED_HW"),
		  XE_RTP_RULES(FUNC(xe_rtp_cfeg_wmtp_disabled)),
		  XE_RTP_ACTIONS(FIELD_SET(CS_CHICKEN1(0),
					   PREEMPT_GPGPU_LEVEL_MASK,
					   PREEMPT_GPGPU_THREAD_GROUP_LEVEL)),
		  XE_RTP_ENTRY_FLAG(FOREACH_ENGINE)
		},
	};

	xe_rtp_process_to_sr(&ctx, lrc_setup, ARRAY_SIZE(lrc_setup), &hwe->reg_lrc);
}

static void
hw_engine_setup_default_state(struct xe_hw_engine *hwe)
{
	struct xe_gt *gt = hwe->gt;
	struct xe_device *xe = gt_to_xe(gt);
	/*
	 * RING_CMD_CCTL specifies the default MOCS entry that will be
	 * used by the command streamer when executing commands that
	 * don't have a way to explicitly specify a MOCS setting.
	 * The default should usually reference whichever MOCS entry
	 * corresponds to uncached behavior, although use of a WB cached
	 * entry is recommended by the spec in certain circumstances on
	 * specific platforms.
	 * Bspec: 72161
	 */
	const u8 mocs_write_idx = gt->mocs.uc_index;
	const u8 mocs_read_idx = hwe->class == XE_ENGINE_CLASS_COMPUTE && IS_DGFX(xe) &&
				 (GRAPHICS_VER(xe) >= 20 || xe->info.platform == XE_PVC) ?
				 gt->mocs.wb_index : gt->mocs.uc_index;
	u32 ring_cmd_cctl_val = REG_FIELD_PREP(CMD_CCTL_WRITE_OVERRIDE_MASK, mocs_write_idx) |
				REG_FIELD_PREP(CMD_CCTL_READ_OVERRIDE_MASK, mocs_read_idx);
	struct xe_rtp_process_ctx ctx = XE_RTP_PROCESS_CTX_INITIALIZER(hwe);
	const struct xe_rtp_entry_sr engine_entries[] = {
		{ XE_RTP_NAME("RING_CMD_CCTL_default_MOCS"),
		  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(1200, XE_RTP_END_VERSION_UNDEFINED)),
		  XE_RTP_ACTIONS(FIELD_SET(RING_CMD_CCTL(0),
					   CMD_CCTL_WRITE_OVERRIDE_MASK |
					   CMD_CCTL_READ_OVERRIDE_MASK,
					   ring_cmd_cctl_val,
					   XE_RTP_ACTION_FLAG(ENGINE_BASE)))
		},
		/*
		 * To allow the GSC engine to go idle on MTL we need to enable
		 * idle messaging and set the hysteresis value (we use 0xA=5us
		 * as recommended in spec). On platforms after MTL this is
		 * enabled by default.
		 */
		{ XE_RTP_NAME("MTL GSCCS IDLE MSG enable"),
		  XE_RTP_RULES(MEDIA_VERSION(1300), ENGINE_CLASS(OTHER)),
		  XE_RTP_ACTIONS(CLR(RING_PSMI_CTL(0),
				     IDLE_MSG_DISABLE,
				     XE_RTP_ACTION_FLAG(ENGINE_BASE)),
				 FIELD_SET(RING_PWRCTX_MAXCNT(0),
					   IDLE_WAIT_TIME,
					   0xA,
					   XE_RTP_ACTION_FLAG(ENGINE_BASE)))
		},
		/* Enable Priority Mem Read */
		{ XE_RTP_NAME("Priority_Mem_Read"),
		  XE_RTP_RULES(GRAPHICS_VERSION_RANGE(2001, XE_RTP_END_VERSION_UNDEFINED)),
		  XE_RTP_ACTIONS(SET(CSFE_CHICKEN1(0), CS_PRIORITY_MEM_READ,
				     XE_RTP_ACTION_FLAG(ENGINE_BASE)))
		},
		/* Use Fixed slice CCS mode */
		{ XE_RTP_NAME("RCU_MODE_FIXED_SLICE_CCS_MODE"),
		  XE_RTP_RULES(FUNC(xe_hw_engine_match_fixed_cslice_mode)),
		  XE_RTP_ACTIONS(FIELD_SET(RCU_MODE, RCU_MODE_FIXED_SLICE_CCS_MODE,
					   RCU_MODE_FIXED_SLICE_CCS_MODE))
		},
	};

	xe_rtp_process_to_sr(&ctx, engine_entries, ARRAY_SIZE(engine_entries), &hwe->reg_sr);
}

static const struct engine_info *find_engine_info(enum xe_engine_class class, int instance)
{
	const struct engine_info *info;
	enum xe_hw_engine_id id;

	for (id = 0; id < XE_NUM_HW_ENGINES; ++id) {
		info = &engine_infos[id];
		if (info->class == class && info->instance == instance)
			return info;
	}

	return NULL;
}

static u16 get_msix_irq_offset(struct xe_gt *gt, enum xe_engine_class class)
{
	/* For MSI-X, hw engines report to offset of engine instance zero */
	const struct engine_info *info = find_engine_info(class, 0);

	xe_gt_assert(gt, info);

	return info ? info->irq_offset : 0;
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

	xe_gt_assert(gt, !hwe->gt);

	hwe->gt = gt;
	hwe->class = info->class;
	hwe->instance = info->instance;
	hwe->mmio_base = info->mmio_base;
	hwe->irq_offset = xe_device_has_msix(gt_to_xe(gt)) ?
		get_msix_irq_offset(gt, info->class) :
		info->irq_offset;
	hwe->domain = info->domain;
	hwe->name = info->name;
	hwe->fence_irq = &gt->fence_irq[info->class];
	hwe->engine_id = id;

	hwe->eclass = &gt->eclass[hwe->class];
	if (!hwe->eclass->sched_props.job_timeout_ms) {
		hwe->eclass->sched_props.job_timeout_ms = 5 * 1000;
		hwe->eclass->sched_props.job_timeout_min = XE_HW_ENGINE_JOB_TIMEOUT_MIN;
		hwe->eclass->sched_props.job_timeout_max = XE_HW_ENGINE_JOB_TIMEOUT_MAX;
		hwe->eclass->sched_props.timeslice_us = 1 * 1000;
		hwe->eclass->sched_props.timeslice_min = XE_HW_ENGINE_TIMESLICE_MIN;
		hwe->eclass->sched_props.timeslice_max = XE_HW_ENGINE_TIMESLICE_MAX;
		hwe->eclass->sched_props.preempt_timeout_us = XE_HW_ENGINE_PREEMPT_TIMEOUT;
		hwe->eclass->sched_props.preempt_timeout_min = XE_HW_ENGINE_PREEMPT_TIMEOUT_MIN;
		hwe->eclass->sched_props.preempt_timeout_max = XE_HW_ENGINE_PREEMPT_TIMEOUT_MAX;

		/*
		 * The GSC engine can accept submissions while the GSC shim is
		 * being reset, during which time the submission is stalled. In
		 * the worst case, the shim reset can take up to the maximum GSC
		 * command execution time (250ms), so the request start can be
		 * delayed by that much; the request itself can take that long
		 * without being preemptible, which means worst case it can
		 * theoretically take up to 500ms for a preemption to go through
		 * on the GSC engine. Adding to that an extra 100ms as a safety
		 * margin, we get a minimum recommended timeout of 600ms.
		 * The preempt_timeout value can't be tuned for OTHER_CLASS
		 * because the class is reserved for kernel usage, so we just
		 * need to make sure that the starting value is above that
		 * threshold; since our default value (640ms) is greater than
		 * 600ms, the only way we can go below is via a kconfig setting.
		 * If that happens, log it in dmesg and update the value.
		 */
		if (hwe->class == XE_ENGINE_CLASS_OTHER) {
			const u32 min_preempt_timeout = 600 * 1000;
			if (hwe->eclass->sched_props.preempt_timeout_us < min_preempt_timeout) {
				hwe->eclass->sched_props.preempt_timeout_us = min_preempt_timeout;
				xe_gt_notice(gt, "Increasing preempt_timeout for GSC to 600ms\n");
			}
		}

		/* Record default props */
		hwe->eclass->defaults = hwe->eclass->sched_props;
	}

	xe_reg_sr_init(&hwe->reg_sr, hwe->name, gt_to_xe(gt));
	xe_tuning_process_engine(hwe);
	xe_wa_process_engine(hwe);
	hw_engine_setup_default_state(hwe);

	xe_reg_sr_init(&hwe->reg_whitelist, hwe->name, gt_to_xe(gt));
	xe_reg_whitelist_process_engine(hwe);
}

static void adjust_idledly(struct xe_hw_engine *hwe)
{
	struct xe_gt *gt = hwe->gt;
	u32 idledly, maxcnt;
	u32 idledly_units_ps = 8 * gt->info.timestamp_base;
	u32 maxcnt_units_ns = 640;
	bool inhibit_switch = 0;

	if (!IS_SRIOV_VF(gt_to_xe(hwe->gt)) && XE_GT_WA(gt, 16023105232)) {
		idledly = xe_mmio_read32(&gt->mmio, RING_IDLEDLY(hwe->mmio_base));
		maxcnt = xe_mmio_read32(&gt->mmio, RING_PWRCTX_MAXCNT(hwe->mmio_base));

		inhibit_switch = idledly & INHIBIT_SWITCH_UNTIL_PREEMPTED;
		idledly = REG_FIELD_GET(IDLE_DELAY, idledly);
		idledly = DIV_ROUND_CLOSEST(idledly * idledly_units_ps, 1000);
		maxcnt = REG_FIELD_GET(IDLE_WAIT_TIME, maxcnt);
		maxcnt *= maxcnt_units_ns;

		if (xe_gt_WARN_ON(gt, idledly >= maxcnt || inhibit_switch)) {
			idledly = DIV_ROUND_CLOSEST(((maxcnt - 1) * maxcnt_units_ns),
						    idledly_units_ps);
			idledly = DIV_ROUND_CLOSEST(idledly, 1000);
			xe_mmio_write32(&gt->mmio, RING_IDLEDLY(hwe->mmio_base), idledly);
		}
	}
}

static int hw_engine_init(struct xe_gt *gt, struct xe_hw_engine *hwe,
			  enum xe_hw_engine_id id)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_tile *tile = gt_to_tile(gt);
	int err;

	xe_gt_assert(gt, id < ARRAY_SIZE(engine_infos) && engine_infos[id].name);
	xe_gt_assert(gt, gt->info.engine_mask & BIT(id));

	xe_reg_sr_apply_mmio(&hwe->reg_sr, gt);

	hwe->hwsp = xe_managed_bo_create_pin_map(xe, tile, SZ_4K,
						 XE_BO_FLAG_VRAM_IF_DGFX(tile) |
						 XE_BO_FLAG_GGTT |
						 XE_BO_FLAG_GGTT_INVALIDATE);
	if (IS_ERR(hwe->hwsp)) {
		err = PTR_ERR(hwe->hwsp);
		goto err_name;
	}

	if (!xe_device_uc_enabled(xe)) {
		hwe->exl_port = xe_execlist_port_create(xe, hwe);
		if (IS_ERR(hwe->exl_port)) {
			err = PTR_ERR(hwe->exl_port);
			goto err_hwsp;
		}
	} else {
		/* GSCCS has a special interrupt for reset */
		if (hwe->class == XE_ENGINE_CLASS_OTHER)
			hwe->irq_handler = xe_gsc_hwe_irq_handler;

		if (!IS_SRIOV_VF(xe))
			xe_hw_engine_enable_ring(hwe);
	}

	/* We reserve the highest BCS instance for USM */
	if (xe->info.has_usm && hwe->class == XE_ENGINE_CLASS_COPY)
		gt->usm.reserved_bcs_instance = hwe->instance;

	/* Ensure IDLEDLY is lower than MAXCNT */
	adjust_idledly(hwe);

	return devm_add_action_or_reset(xe->drm.dev, hw_engine_fini, hwe);

err_hwsp:
	xe_bo_unpin_map_no_vm(hwe->hwsp);
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

static void read_media_fuses(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 media_fuse;
	u16 vdbox_mask;
	u16 vebox_mask;
	int i, j;

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	media_fuse = xe_mmio_read32(&gt->mmio, GT_VEBOX_VDBOX_DISABLE);

	/*
	 * Pre-Xe_HP platforms had register bits representing absent engines,
	 * whereas Xe_HP and beyond have bits representing present engines.
	 * Invert the polarity on old platforms so that we can use common
	 * handling below.
	 */
	if (GRAPHICS_VERx100(xe) < 1250)
		media_fuse = ~media_fuse;

	vdbox_mask = REG_FIELD_GET(GT_VDBOX_DISABLE_MASK, media_fuse);
	vebox_mask = REG_FIELD_GET(GT_VEBOX_DISABLE_MASK, media_fuse);

	for (i = XE_HW_ENGINE_VCS0, j = 0; i <= XE_HW_ENGINE_VCS7; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		if (!(BIT(j) & vdbox_mask)) {
			gt->info.engine_mask &= ~BIT(i);
			xe_gt_info(gt, "vcs%u fused off\n", j);
		}
	}

	for (i = XE_HW_ENGINE_VECS0, j = 0; i <= XE_HW_ENGINE_VECS3; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		if (!(BIT(j) & vebox_mask)) {
			gt->info.engine_mask &= ~BIT(i);
			xe_gt_info(gt, "vecs%u fused off\n", j);
		}
	}
}

static void read_copy_fuses(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	u32 bcs_mask;

	if (GRAPHICS_VERx100(xe) < 1260 || GRAPHICS_VERx100(xe) >= 1270)
		return;

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	bcs_mask = xe_mmio_read32(&gt->mmio, MIRROR_FUSE3);
	bcs_mask = REG_FIELD_GET(MEML3_EN_MASK, bcs_mask);

	/* BCS0 is always present; only BCS1-BCS8 may be fused off */
	for (int i = XE_HW_ENGINE_BCS1, j = 0; i <= XE_HW_ENGINE_BCS8; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		if (!(BIT(j / 2) & bcs_mask)) {
			gt->info.engine_mask &= ~BIT(i);
			xe_gt_info(gt, "bcs%u fused off\n", j);
		}
	}
}

static void read_compute_fuses_from_dss(struct xe_gt *gt)
{
	/*
	 * CCS fusing based on DSS masks only applies to platforms that can
	 * have more than one CCS.
	 */
	if (hweight64(gt->info.engine_mask &
		      GENMASK_ULL(XE_HW_ENGINE_CCS3, XE_HW_ENGINE_CCS0)) <= 1)
		return;

	/*
	 * CCS availability on Xe_HP is inferred from the presence of DSS in
	 * each quadrant.
	 */
	for (int i = XE_HW_ENGINE_CCS0, j = 0; i <= XE_HW_ENGINE_CCS3; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		if (!xe_gt_topology_has_dss_in_quadrant(gt, j)) {
			gt->info.engine_mask &= ~BIT(i);
			xe_gt_info(gt, "ccs%u fused off\n", j);
		}
	}
}

static void read_compute_fuses_from_reg(struct xe_gt *gt)
{
	u32 ccs_mask;

	ccs_mask = xe_mmio_read32(&gt->mmio, XEHP_FUSE4);
	ccs_mask = REG_FIELD_GET(CCS_EN_MASK, ccs_mask);

	for (int i = XE_HW_ENGINE_CCS0, j = 0; i <= XE_HW_ENGINE_CCS3; ++i, ++j) {
		if (!(gt->info.engine_mask & BIT(i)))
			continue;

		if ((ccs_mask & BIT(j)) == 0) {
			gt->info.engine_mask &= ~BIT(i);
			xe_gt_info(gt, "ccs%u fused off\n", j);
		}
	}
}

static void read_compute_fuses(struct xe_gt *gt)
{
	if (GRAPHICS_VER(gt_to_xe(gt)) >= 20)
		read_compute_fuses_from_reg(gt);
	else
		read_compute_fuses_from_dss(gt);
}

static void check_gsc_availability(struct xe_gt *gt)
{
	if (!(gt->info.engine_mask & BIT(XE_HW_ENGINE_GSCCS0)))
		return;

	/*
	 * The GSCCS is only used to communicate with the GSC FW, so if we don't
	 * have the FW there is nothing we need the engine for and can therefore
	 * skip its initialization.
	 */
	if (!xe_uc_fw_is_available(&gt->uc.gsc.fw)) {
		gt->info.engine_mask &= ~BIT(XE_HW_ENGINE_GSCCS0);

		/* interrupts where previously enabled, so turn them off */
		xe_mmio_write32(&gt->mmio, GUNIT_GSC_INTR_ENABLE, 0);
		xe_mmio_write32(&gt->mmio, GUNIT_GSC_INTR_MASK, ~0);

		xe_gt_dbg(gt, "GSC FW not used, disabling gsccs\n");
	}
}

static void check_sw_disable(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	u64 sw_allowed = xe_configfs_get_engines_allowed(to_pci_dev(xe->drm.dev));
	enum xe_hw_engine_id id;

	for (id = 0; id < XE_NUM_HW_ENGINES; ++id) {
		if (!(gt->info.engine_mask & BIT(id)))
			continue;

		if (!(sw_allowed & BIT(id))) {
			gt->info.engine_mask &= ~BIT(id);
			xe_gt_info(gt, "%s disabled via configfs\n",
				   engine_infos[id].name);
		}
	}
}

int xe_hw_engines_init_early(struct xe_gt *gt)
{
	int i;

	read_media_fuses(gt);
	read_copy_fuses(gt);
	read_compute_fuses(gt);
	check_gsc_availability(gt);
	check_sw_disable(gt);

	BUILD_BUG_ON(XE_HW_ENGINE_PREEMPT_TIMEOUT < XE_HW_ENGINE_PREEMPT_TIMEOUT_MIN);
	BUILD_BUG_ON(XE_HW_ENGINE_PREEMPT_TIMEOUT > XE_HW_ENGINE_PREEMPT_TIMEOUT_MAX);

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
	err = xe_hw_engine_setup_groups(gt);
	if (err)
		return err;

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

/**
 * xe_hw_engine_snapshot_capture - Take a quick snapshot of the HW Engine.
 * @hwe: Xe HW Engine.
 * @q: The exec queue object.
 *
 * This can be printed out in a later stage like during dev_coredump
 * analysis.
 *
 * Returns: a Xe HW Engine snapshot object that must be freed by the
 * caller, using `xe_hw_engine_snapshot_free`.
 */
struct xe_hw_engine_snapshot *
xe_hw_engine_snapshot_capture(struct xe_hw_engine *hwe, struct xe_exec_queue *q)
{
	struct xe_hw_engine_snapshot *snapshot;
	struct __guc_capture_parsed_output *node;

	if (!xe_hw_engine_is_valid(hwe))
		return NULL;

	snapshot = kzalloc(sizeof(*snapshot), GFP_ATOMIC);

	if (!snapshot)
		return NULL;

	snapshot->name = kstrdup(hwe->name, GFP_ATOMIC);
	snapshot->hwe = hwe;
	snapshot->logical_instance = hwe->logical_instance;
	snapshot->forcewake.domain = hwe->domain;
	snapshot->forcewake.ref = xe_force_wake_ref(gt_to_fw(hwe->gt),
						    hwe->domain);
	snapshot->mmio_base = hwe->mmio_base;
	snapshot->kernel_reserved = xe_hw_engine_is_reserved(hwe);

	/* no more VF accessible data below this point */
	if (IS_SRIOV_VF(gt_to_xe(hwe->gt)))
		return snapshot;

	if (q) {
		/* If got guc capture, set source to GuC */
		node = xe_guc_capture_get_matching_and_lock(q);
		if (node) {
			struct xe_device *xe = gt_to_xe(hwe->gt);
			struct xe_devcoredump *coredump = &xe->devcoredump;

			coredump->snapshot.matched_node = node;
			xe_gt_dbg(hwe->gt, "Found and locked GuC-err-capture node");
			return snapshot;
		}
	}

	/* otherwise, do manual capture */
	xe_engine_manual_capture(hwe, snapshot);
	xe_gt_dbg(hwe->gt, "Proceeding with manual engine snapshot");

	return snapshot;
}

/**
 * xe_hw_engine_snapshot_free - Free all allocated objects for a given snapshot.
 * @snapshot: Xe HW Engine snapshot object.
 *
 * This function free all the memory that needed to be allocated at capture
 * time.
 */
void xe_hw_engine_snapshot_free(struct xe_hw_engine_snapshot *snapshot)
{
	struct xe_gt *gt;
	if (!snapshot)
		return;

	gt = snapshot->hwe->gt;
	/*
	 * xe_guc_capture_put_matched_nodes is called here and from
	 * xe_devcoredump_snapshot_free, to cover the 2 calling paths
	 * of hw_engines - debugfs and devcoredump free.
	 */
	xe_guc_capture_put_matched_nodes(&gt->uc.guc);

	kfree(snapshot->name);
	kfree(snapshot);
}

/**
 * xe_hw_engine_print - Xe HW Engine Print.
 * @hwe: Hardware Engine.
 * @p: drm_printer.
 *
 * This function quickly capture a snapshot and immediately print it out.
 */
void xe_hw_engine_print(struct xe_hw_engine *hwe, struct drm_printer *p)
{
	struct xe_hw_engine_snapshot *snapshot;

	snapshot = xe_hw_engine_snapshot_capture(hwe, NULL);
	xe_engine_snapshot_print(snapshot, p);
	xe_hw_engine_snapshot_free(snapshot);
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

	if (hwe->class == XE_ENGINE_CLASS_OTHER)
		return true;

	/* Check for engines disabled by ccs_mode setting */
	if (xe_gt_ccs_mode_enabled(gt) &&
	    hwe->class == XE_ENGINE_CLASS_COMPUTE &&
	    hwe->logical_instance >= gt->ccs_mode)
		return true;

	return xe->info.has_usm && hwe->class == XE_ENGINE_CLASS_COPY &&
		hwe->instance == gt->usm.reserved_bcs_instance;
}

const char *xe_hw_engine_class_to_str(enum xe_engine_class class)
{
	switch (class) {
	case XE_ENGINE_CLASS_RENDER:
		return "rcs";
	case XE_ENGINE_CLASS_VIDEO_DECODE:
		return "vcs";
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		return "vecs";
	case XE_ENGINE_CLASS_COPY:
		return "bcs";
	case XE_ENGINE_CLASS_OTHER:
		return "other";
	case XE_ENGINE_CLASS_COMPUTE:
		return "ccs";
	case XE_ENGINE_CLASS_MAX:
		break;
	}

	return NULL;
}

u64 xe_hw_engine_read_timestamp(struct xe_hw_engine *hwe)
{
	return xe_mmio_read64_2x32(&hwe->gt->mmio, RING_TIMESTAMP(hwe->mmio_base));
}

enum xe_force_wake_domains xe_hw_engine_to_fw_domain(struct xe_hw_engine *hwe)
{
	return engine_infos[hwe->engine_id].domain;
}

static const enum xe_engine_class user_to_xe_engine_class[] = {
	[DRM_XE_ENGINE_CLASS_RENDER] = XE_ENGINE_CLASS_RENDER,
	[DRM_XE_ENGINE_CLASS_COPY] = XE_ENGINE_CLASS_COPY,
	[DRM_XE_ENGINE_CLASS_VIDEO_DECODE] = XE_ENGINE_CLASS_VIDEO_DECODE,
	[DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE] = XE_ENGINE_CLASS_VIDEO_ENHANCE,
	[DRM_XE_ENGINE_CLASS_COMPUTE] = XE_ENGINE_CLASS_COMPUTE,
};

/**
 * xe_hw_engine_lookup() - Lookup hardware engine for class:instance
 * @xe: xe device
 * @eci: engine class and instance
 *
 * This function will find a hardware engine for given engine
 * class and instance.
 *
 * Return: If found xe_hw_engine pointer, NULL otherwise.
 */
struct xe_hw_engine *
xe_hw_engine_lookup(struct xe_device *xe,
		    struct drm_xe_engine_class_instance eci)
{
	struct xe_gt *gt = xe_device_get_gt(xe, eci.gt_id);
	unsigned int idx;

	if (eci.engine_class >= ARRAY_SIZE(user_to_xe_engine_class))
		return NULL;

	if (!gt)
		return NULL;

	idx = array_index_nospec(eci.engine_class,
				 ARRAY_SIZE(user_to_xe_engine_class));

	return xe_gt_hw_engine(xe_device_get_gt(xe, eci.gt_id),
			       user_to_xe_engine_class[idx],
			       eci.engine_instance, true);
}
