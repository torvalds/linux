// SPDX-License-Identifier: MIT

/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/seq_file.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_gt.h"
#include "intel_gt_clock_utils.h"
#include "intel_gt_debugfs.h"
#include "intel_gt_pm.h"
#include "intel_gt_pm_debugfs.h"
#include "intel_gt_regs.h"
#include "intel_llc.h"
#include "intel_pcode.h"
#include "intel_rc6.h"
#include "intel_rps.h"
#include "intel_runtime_pm.h"
#include "intel_uncore.h"
#include "vlv_sideband.h"

int intel_gt_pm_debugfs_forcewake_user_open(struct intel_gt *gt)
{
	atomic_inc(&gt->user_wakeref);
	intel_gt_pm_get(gt);
	if (GRAPHICS_VER(gt->i915) >= 6)
		intel_uncore_forcewake_user_get(gt->uncore);

	return 0;
}

int intel_gt_pm_debugfs_forcewake_user_release(struct intel_gt *gt)
{
	if (GRAPHICS_VER(gt->i915) >= 6)
		intel_uncore_forcewake_user_put(gt->uncore);
	intel_gt_pm_put(gt);
	atomic_dec(&gt->user_wakeref);

	return 0;
}

static int forcewake_user_open(struct inode *inode, struct file *file)
{
	struct intel_gt *gt = inode->i_private;

	return intel_gt_pm_debugfs_forcewake_user_open(gt);
}

static int forcewake_user_release(struct inode *inode, struct file *file)
{
	struct intel_gt *gt = inode->i_private;

	return intel_gt_pm_debugfs_forcewake_user_release(gt);
}

static const struct file_operations forcewake_user_fops = {
	.owner = THIS_MODULE,
	.open = forcewake_user_open,
	.release = forcewake_user_release,
};

static int fw_domains_show(struct seq_file *m, void *data)
{
	struct intel_gt *gt = m->private;
	struct intel_uncore *uncore = gt->uncore;
	struct intel_uncore_forcewake_domain *fw_domain;
	unsigned int tmp;

	seq_printf(m, "user.bypass_count = %u\n",
		   uncore->user_forcewake_count);

	for_each_fw_domain(fw_domain, uncore, tmp)
		seq_printf(m, "%s.wake_count = %u\n",
			   intel_uncore_forcewake_domain_to_str(fw_domain->id),
			   READ_ONCE(fw_domain->wake_count));

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(fw_domains);

static void print_rc6_res(struct seq_file *m,
			  const char *title,
			  const i915_reg_t reg)
{
	struct intel_gt *gt = m->private;
	intel_wakeref_t wakeref;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		seq_printf(m, "%s %u (%llu us)\n", title,
			   intel_uncore_read(gt->uncore, reg),
			   intel_rc6_residency_us(&gt->rc6, reg));
}

static int vlv_drpc(struct seq_file *m)
{
	struct intel_gt *gt = m->private;
	struct intel_uncore *uncore = gt->uncore;
	u32 rcctl1, pw_status;

	pw_status = intel_uncore_read(uncore, VLV_GTLC_PW_STATUS);
	rcctl1 = intel_uncore_read(uncore, GEN6_RC_CONTROL);

	seq_printf(m, "RC6 Enabled: %s\n",
		   yesno(rcctl1 & (GEN7_RC_CTL_TO_MODE |
					GEN6_RC_CTL_EI_MODE(1))));
	seq_printf(m, "Render Power Well: %s\n",
		   (pw_status & VLV_GTLC_PW_RENDER_STATUS_MASK) ? "Up" : "Down");
	seq_printf(m, "Media Power Well: %s\n",
		   (pw_status & VLV_GTLC_PW_MEDIA_STATUS_MASK) ? "Up" : "Down");

	print_rc6_res(m, "Render RC6 residency since boot:", VLV_GT_RENDER_RC6);
	print_rc6_res(m, "Media RC6 residency since boot:", VLV_GT_MEDIA_RC6);

	return fw_domains_show(m, NULL);
}

static int gen6_drpc(struct seq_file *m)
{
	struct intel_gt *gt = m->private;
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	u32 gt_core_status, rcctl1, rc6vids = 0;
	u32 gen9_powergate_enable = 0, gen9_powergate_status = 0;

	gt_core_status = intel_uncore_read_fw(uncore, GEN6_GT_CORE_STATUS);

	rcctl1 = intel_uncore_read(uncore, GEN6_RC_CONTROL);
	if (GRAPHICS_VER(i915) >= 9) {
		gen9_powergate_enable =
			intel_uncore_read(uncore, GEN9_PG_ENABLE);
		gen9_powergate_status =
			intel_uncore_read(uncore, GEN9_PWRGT_DOMAIN_STATUS);
	}

	if (GRAPHICS_VER(i915) <= 7)
		snb_pcode_read(i915, GEN6_PCODE_READ_RC6VIDS, &rc6vids, NULL);

	seq_printf(m, "RC1e Enabled: %s\n",
		   yesno(rcctl1 & GEN6_RC_CTL_RC1e_ENABLE));
	seq_printf(m, "RC6 Enabled: %s\n",
		   yesno(rcctl1 & GEN6_RC_CTL_RC6_ENABLE));
	if (GRAPHICS_VER(i915) >= 9) {
		seq_printf(m, "Render Well Gating Enabled: %s\n",
			   yesno(gen9_powergate_enable & GEN9_RENDER_PG_ENABLE));
		seq_printf(m, "Media Well Gating Enabled: %s\n",
			   yesno(gen9_powergate_enable & GEN9_MEDIA_PG_ENABLE));
	}
	seq_printf(m, "Deep RC6 Enabled: %s\n",
		   yesno(rcctl1 & GEN6_RC_CTL_RC6p_ENABLE));
	seq_printf(m, "Deepest RC6 Enabled: %s\n",
		   yesno(rcctl1 & GEN6_RC_CTL_RC6pp_ENABLE));
	seq_puts(m, "Current RC state: ");
	switch (gt_core_status & GEN6_RCn_MASK) {
	case GEN6_RC0:
		if (gt_core_status & GEN6_CORE_CPD_STATE_MASK)
			seq_puts(m, "Core Power Down\n");
		else
			seq_puts(m, "on\n");
		break;
	case GEN6_RC3:
		seq_puts(m, "RC3\n");
		break;
	case GEN6_RC6:
		seq_puts(m, "RC6\n");
		break;
	case GEN6_RC7:
		seq_puts(m, "RC7\n");
		break;
	default:
		seq_puts(m, "Unknown\n");
		break;
	}

	seq_printf(m, "Core Power Down: %s\n",
		   yesno(gt_core_status & GEN6_CORE_CPD_STATE_MASK));
	if (GRAPHICS_VER(i915) >= 9) {
		seq_printf(m, "Render Power Well: %s\n",
			   (gen9_powergate_status &
			    GEN9_PWRGT_RENDER_STATUS_MASK) ? "Up" : "Down");
		seq_printf(m, "Media Power Well: %s\n",
			   (gen9_powergate_status &
			    GEN9_PWRGT_MEDIA_STATUS_MASK) ? "Up" : "Down");
	}

	/* Not exactly sure what this is */
	print_rc6_res(m, "RC6 \"Locked to RPn\" residency since boot:",
		      GEN6_GT_GFX_RC6_LOCKED);
	print_rc6_res(m, "RC6 residency since boot:", GEN6_GT_GFX_RC6);
	print_rc6_res(m, "RC6+ residency since boot:", GEN6_GT_GFX_RC6p);
	print_rc6_res(m, "RC6++ residency since boot:", GEN6_GT_GFX_RC6pp);

	if (GRAPHICS_VER(i915) <= 7) {
		seq_printf(m, "RC6   voltage: %dmV\n",
			   GEN6_DECODE_RC6_VID(((rc6vids >> 0) & 0xff)));
		seq_printf(m, "RC6+  voltage: %dmV\n",
			   GEN6_DECODE_RC6_VID(((rc6vids >> 8) & 0xff)));
		seq_printf(m, "RC6++ voltage: %dmV\n",
			   GEN6_DECODE_RC6_VID(((rc6vids >> 16) & 0xff)));
	}

	return fw_domains_show(m, NULL);
}

static int ilk_drpc(struct seq_file *m)
{
	struct intel_gt *gt = m->private;
	struct intel_uncore *uncore = gt->uncore;
	u32 rgvmodectl, rstdbyctl;
	u16 crstandvid;

	rgvmodectl = intel_uncore_read(uncore, MEMMODECTL);
	rstdbyctl = intel_uncore_read(uncore, RSTDBYCTL);
	crstandvid = intel_uncore_read16(uncore, CRSTANDVID);

	seq_printf(m, "HD boost: %s\n", yesno(rgvmodectl & MEMMODE_BOOST_EN));
	seq_printf(m, "Boost freq: %d\n",
		   (rgvmodectl & MEMMODE_BOOST_FREQ_MASK) >>
		   MEMMODE_BOOST_FREQ_SHIFT);
	seq_printf(m, "HW control enabled: %s\n",
		   yesno(rgvmodectl & MEMMODE_HWIDLE_EN));
	seq_printf(m, "SW control enabled: %s\n",
		   yesno(rgvmodectl & MEMMODE_SWMODE_EN));
	seq_printf(m, "Gated voltage change: %s\n",
		   yesno(rgvmodectl & MEMMODE_RCLK_GATE));
	seq_printf(m, "Starting frequency: P%d\n",
		   (rgvmodectl & MEMMODE_FSTART_MASK) >> MEMMODE_FSTART_SHIFT);
	seq_printf(m, "Max P-state: P%d\n",
		   (rgvmodectl & MEMMODE_FMAX_MASK) >> MEMMODE_FMAX_SHIFT);
	seq_printf(m, "Min P-state: P%d\n", (rgvmodectl & MEMMODE_FMIN_MASK));
	seq_printf(m, "RS1 VID: %d\n", (crstandvid & 0x3f));
	seq_printf(m, "RS2 VID: %d\n", ((crstandvid >> 8) & 0x3f));
	seq_printf(m, "Render standby enabled: %s\n",
		   yesno(!(rstdbyctl & RCX_SW_EXIT)));
	seq_puts(m, "Current RS state: ");
	switch (rstdbyctl & RSX_STATUS_MASK) {
	case RSX_STATUS_ON:
		seq_puts(m, "on\n");
		break;
	case RSX_STATUS_RC1:
		seq_puts(m, "RC1\n");
		break;
	case RSX_STATUS_RC1E:
		seq_puts(m, "RC1E\n");
		break;
	case RSX_STATUS_RS1:
		seq_puts(m, "RS1\n");
		break;
	case RSX_STATUS_RS2:
		seq_puts(m, "RS2 (RC6)\n");
		break;
	case RSX_STATUS_RS3:
		seq_puts(m, "RC3 (RC6+)\n");
		break;
	default:
		seq_puts(m, "unknown\n");
		break;
	}

	return 0;
}

static int drpc_show(struct seq_file *m, void *unused)
{
	struct intel_gt *gt = m->private;
	struct drm_i915_private *i915 = gt->i915;
	intel_wakeref_t wakeref;
	int err = -ENODEV;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref) {
		if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
			err = vlv_drpc(m);
		else if (GRAPHICS_VER(i915) >= 6)
			err = gen6_drpc(m);
		else
			err = ilk_drpc(m);
	}

	return err;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(drpc);

void intel_gt_pm_frequency_dump(struct intel_gt *gt, struct drm_printer *p)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	struct intel_rps *rps = &gt->rps;
	intel_wakeref_t wakeref;

	wakeref = intel_runtime_pm_get(uncore->rpm);

	if (GRAPHICS_VER(i915) == 5) {
		u16 rgvswctl = intel_uncore_read16(uncore, MEMSWCTL);
		u16 rgvstat = intel_uncore_read16(uncore, MEMSTAT_ILK);

		drm_printf(p, "Requested P-state: %d\n", (rgvswctl >> 8) & 0xf);
		drm_printf(p, "Requested VID: %d\n", rgvswctl & 0x3f);
		drm_printf(p, "Current VID: %d\n", (rgvstat & MEMSTAT_VID_MASK) >>
			   MEMSTAT_VID_SHIFT);
		drm_printf(p, "Current P-state: %d\n",
			   (rgvstat & MEMSTAT_PSTATE_MASK) >> MEMSTAT_PSTATE_SHIFT);
	} else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915)) {
		u32 rpmodectl, freq_sts;

		rpmodectl = intel_uncore_read(uncore, GEN6_RP_CONTROL);
		drm_printf(p, "Video Turbo Mode: %s\n",
			   yesno(rpmodectl & GEN6_RP_MEDIA_TURBO));
		drm_printf(p, "HW control enabled: %s\n",
			   yesno(rpmodectl & GEN6_RP_ENABLE));
		drm_printf(p, "SW control enabled: %s\n",
			   yesno((rpmodectl & GEN6_RP_MEDIA_MODE_MASK) ==
				 GEN6_RP_MEDIA_SW_MODE));

		vlv_punit_get(i915);
		freq_sts = vlv_punit_read(i915, PUNIT_REG_GPU_FREQ_STS);
		vlv_punit_put(i915);

		drm_printf(p, "PUNIT_REG_GPU_FREQ_STS: 0x%08x\n", freq_sts);
		drm_printf(p, "DDR freq: %d MHz\n", i915->mem_freq);

		drm_printf(p, "actual GPU freq: %d MHz\n",
			   intel_gpu_freq(rps, (freq_sts >> 8) & 0xff));

		drm_printf(p, "current GPU freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->cur_freq));

		drm_printf(p, "max GPU freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->max_freq));

		drm_printf(p, "min GPU freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->min_freq));

		drm_printf(p, "idle GPU freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->idle_freq));

		drm_printf(p, "efficient (RPe) frequency: %d MHz\n",
			   intel_gpu_freq(rps, rps->efficient_freq));
	} else if (GRAPHICS_VER(i915) >= 6) {
		u32 rp_state_limits;
		u32 gt_perf_status;
		u32 rp_state_cap;
		u32 rpmodectl, rpinclimit, rpdeclimit;
		u32 rpstat, cagf, reqf;
		u32 rpcurupei, rpcurup, rpprevup;
		u32 rpcurdownei, rpcurdown, rpprevdown;
		u32 rpupei, rpupt, rpdownei, rpdownt;
		u32 pm_ier, pm_imr, pm_isr, pm_iir, pm_mask;
		int max_freq;

		rp_state_limits = intel_uncore_read(uncore, GEN6_RP_STATE_LIMITS);
		rp_state_cap = intel_rps_read_state_cap(rps);
		if (IS_GEN9_LP(i915))
			gt_perf_status = intel_uncore_read(uncore, BXT_GT_PERF_STATUS);
		else
			gt_perf_status = intel_uncore_read(uncore, GEN6_GT_PERF_STATUS);

		/* RPSTAT1 is in the GT power well */
		intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);

		reqf = intel_uncore_read(uncore, GEN6_RPNSWREQ);
		if (GRAPHICS_VER(i915) >= 9) {
			reqf >>= 23;
		} else {
			reqf &= ~GEN6_TURBO_DISABLE;
			if (IS_HASWELL(i915) || IS_BROADWELL(i915))
				reqf >>= 24;
			else
				reqf >>= 25;
		}
		reqf = intel_gpu_freq(rps, reqf);

		rpmodectl = intel_uncore_read(uncore, GEN6_RP_CONTROL);
		rpinclimit = intel_uncore_read(uncore, GEN6_RP_UP_THRESHOLD);
		rpdeclimit = intel_uncore_read(uncore, GEN6_RP_DOWN_THRESHOLD);

		rpstat = intel_uncore_read(uncore, GEN6_RPSTAT1);
		rpcurupei = intel_uncore_read(uncore, GEN6_RP_CUR_UP_EI) & GEN6_CURICONT_MASK;
		rpcurup = intel_uncore_read(uncore, GEN6_RP_CUR_UP) & GEN6_CURBSYTAVG_MASK;
		rpprevup = intel_uncore_read(uncore, GEN6_RP_PREV_UP) & GEN6_CURBSYTAVG_MASK;
		rpcurdownei = intel_uncore_read(uncore, GEN6_RP_CUR_DOWN_EI) & GEN6_CURIAVG_MASK;
		rpcurdown = intel_uncore_read(uncore, GEN6_RP_CUR_DOWN) & GEN6_CURBSYTAVG_MASK;
		rpprevdown = intel_uncore_read(uncore, GEN6_RP_PREV_DOWN) & GEN6_CURBSYTAVG_MASK;

		rpupei = intel_uncore_read(uncore, GEN6_RP_UP_EI);
		rpupt = intel_uncore_read(uncore, GEN6_RP_UP_THRESHOLD);

		rpdownei = intel_uncore_read(uncore, GEN6_RP_DOWN_EI);
		rpdownt = intel_uncore_read(uncore, GEN6_RP_DOWN_THRESHOLD);

		cagf = intel_rps_read_actual_frequency(rps);

		intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);

		if (GRAPHICS_VER(i915) >= 11) {
			pm_ier = intel_uncore_read(uncore, GEN11_GPM_WGBOXPERF_INTR_ENABLE);
			pm_imr = intel_uncore_read(uncore, GEN11_GPM_WGBOXPERF_INTR_MASK);
			/*
			 * The equivalent to the PM ISR & IIR cannot be read
			 * without affecting the current state of the system
			 */
			pm_isr = 0;
			pm_iir = 0;
		} else if (GRAPHICS_VER(i915) >= 8) {
			pm_ier = intel_uncore_read(uncore, GEN8_GT_IER(2));
			pm_imr = intel_uncore_read(uncore, GEN8_GT_IMR(2));
			pm_isr = intel_uncore_read(uncore, GEN8_GT_ISR(2));
			pm_iir = intel_uncore_read(uncore, GEN8_GT_IIR(2));
		} else {
			pm_ier = intel_uncore_read(uncore, GEN6_PMIER);
			pm_imr = intel_uncore_read(uncore, GEN6_PMIMR);
			pm_isr = intel_uncore_read(uncore, GEN6_PMISR);
			pm_iir = intel_uncore_read(uncore, GEN6_PMIIR);
		}
		pm_mask = intel_uncore_read(uncore, GEN6_PMINTRMSK);

		drm_printf(p, "Video Turbo Mode: %s\n",
			   yesno(rpmodectl & GEN6_RP_MEDIA_TURBO));
		drm_printf(p, "HW control enabled: %s\n",
			   yesno(rpmodectl & GEN6_RP_ENABLE));
		drm_printf(p, "SW control enabled: %s\n",
			   yesno((rpmodectl & GEN6_RP_MEDIA_MODE_MASK) ==
				 GEN6_RP_MEDIA_SW_MODE));

		drm_printf(p, "PM IER=0x%08x IMR=0x%08x, MASK=0x%08x\n",
			   pm_ier, pm_imr, pm_mask);
		if (GRAPHICS_VER(i915) <= 10)
			drm_printf(p, "PM ISR=0x%08x IIR=0x%08x\n",
				   pm_isr, pm_iir);
		drm_printf(p, "pm_intrmsk_mbz: 0x%08x\n",
			   rps->pm_intrmsk_mbz);
		drm_printf(p, "GT_PERF_STATUS: 0x%08x\n", gt_perf_status);
		drm_printf(p, "Render p-state ratio: %d\n",
			   (gt_perf_status & (GRAPHICS_VER(i915) >= 9 ? 0x1ff00 : 0xff00)) >> 8);
		drm_printf(p, "Render p-state VID: %d\n",
			   gt_perf_status & 0xff);
		drm_printf(p, "Render p-state limit: %d\n",
			   rp_state_limits & 0xff);
		drm_printf(p, "RPSTAT1: 0x%08x\n", rpstat);
		drm_printf(p, "RPMODECTL: 0x%08x\n", rpmodectl);
		drm_printf(p, "RPINCLIMIT: 0x%08x\n", rpinclimit);
		drm_printf(p, "RPDECLIMIT: 0x%08x\n", rpdeclimit);
		drm_printf(p, "RPNSWREQ: %dMHz\n", reqf);
		drm_printf(p, "CAGF: %dMHz\n", cagf);
		drm_printf(p, "RP CUR UP EI: %d (%lldns)\n",
			   rpcurupei,
			   intel_gt_pm_interval_to_ns(gt, rpcurupei));
		drm_printf(p, "RP CUR UP: %d (%lldns)\n",
			   rpcurup, intel_gt_pm_interval_to_ns(gt, rpcurup));
		drm_printf(p, "RP PREV UP: %d (%lldns)\n",
			   rpprevup, intel_gt_pm_interval_to_ns(gt, rpprevup));
		drm_printf(p, "Up threshold: %d%%\n",
			   rps->power.up_threshold);
		drm_printf(p, "RP UP EI: %d (%lldns)\n",
			   rpupei, intel_gt_pm_interval_to_ns(gt, rpupei));
		drm_printf(p, "RP UP THRESHOLD: %d (%lldns)\n",
			   rpupt, intel_gt_pm_interval_to_ns(gt, rpupt));

		drm_printf(p, "RP CUR DOWN EI: %d (%lldns)\n",
			   rpcurdownei,
			   intel_gt_pm_interval_to_ns(gt, rpcurdownei));
		drm_printf(p, "RP CUR DOWN: %d (%lldns)\n",
			   rpcurdown,
			   intel_gt_pm_interval_to_ns(gt, rpcurdown));
		drm_printf(p, "RP PREV DOWN: %d (%lldns)\n",
			   rpprevdown,
			   intel_gt_pm_interval_to_ns(gt, rpprevdown));
		drm_printf(p, "Down threshold: %d%%\n",
			   rps->power.down_threshold);
		drm_printf(p, "RP DOWN EI: %d (%lldns)\n",
			   rpdownei, intel_gt_pm_interval_to_ns(gt, rpdownei));
		drm_printf(p, "RP DOWN THRESHOLD: %d (%lldns)\n",
			   rpdownt, intel_gt_pm_interval_to_ns(gt, rpdownt));

		max_freq = (IS_GEN9_LP(i915) ? rp_state_cap >> 0 :
			    rp_state_cap >> 16) & 0xff;
		max_freq *= (IS_GEN9_BC(i915) ||
			     GRAPHICS_VER(i915) >= 11 ? GEN9_FREQ_SCALER : 1);
		drm_printf(p, "Lowest (RPN) frequency: %dMHz\n",
			   intel_gpu_freq(rps, max_freq));

		max_freq = (rp_state_cap & 0xff00) >> 8;
		max_freq *= (IS_GEN9_BC(i915) ||
			     GRAPHICS_VER(i915) >= 11 ? GEN9_FREQ_SCALER : 1);
		drm_printf(p, "Nominal (RP1) frequency: %dMHz\n",
			   intel_gpu_freq(rps, max_freq));

		max_freq = (IS_GEN9_LP(i915) ? rp_state_cap >> 16 :
			    rp_state_cap >> 0) & 0xff;
		max_freq *= (IS_GEN9_BC(i915) ||
			     GRAPHICS_VER(i915) >= 11 ? GEN9_FREQ_SCALER : 1);
		drm_printf(p, "Max non-overclocked (RP0) frequency: %dMHz\n",
			   intel_gpu_freq(rps, max_freq));
		drm_printf(p, "Max overclocked frequency: %dMHz\n",
			   intel_gpu_freq(rps, rps->max_freq));

		drm_printf(p, "Current freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->cur_freq));
		drm_printf(p, "Actual freq: %d MHz\n", cagf);
		drm_printf(p, "Idle freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->idle_freq));
		drm_printf(p, "Min freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->min_freq));
		drm_printf(p, "Boost freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->boost_freq));
		drm_printf(p, "Max freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->max_freq));
		drm_printf(p,
			   "efficient (RPe) frequency: %d MHz\n",
			   intel_gpu_freq(rps, rps->efficient_freq));
	} else {
		drm_puts(p, "no P-state info available\n");
	}

	drm_printf(p, "Current CD clock frequency: %d kHz\n", i915->cdclk.hw.cdclk);
	drm_printf(p, "Max CD clock frequency: %d kHz\n", i915->max_cdclk_freq);
	drm_printf(p, "Max pixel clock frequency: %d kHz\n", i915->max_dotclk_freq);

	intel_runtime_pm_put(uncore->rpm, wakeref);
}

static int frequency_show(struct seq_file *m, void *unused)
{
	struct intel_gt *gt = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	intel_gt_pm_frequency_dump(gt, &p);

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(frequency);

static int llc_show(struct seq_file *m, void *data)
{
	struct intel_gt *gt = m->private;
	struct drm_i915_private *i915 = gt->i915;
	const bool edram = GRAPHICS_VER(i915) > 8;
	struct intel_rps *rps = &gt->rps;
	unsigned int max_gpu_freq, min_gpu_freq;
	intel_wakeref_t wakeref;
	int gpu_freq, ia_freq;

	seq_printf(m, "LLC: %s\n", yesno(HAS_LLC(i915)));
	seq_printf(m, "%s: %uMB\n", edram ? "eDRAM" : "eLLC",
		   i915->edram_size_mb);

	min_gpu_freq = rps->min_freq;
	max_gpu_freq = rps->max_freq;
	if (IS_GEN9_BC(i915) || GRAPHICS_VER(i915) >= 11) {
		/* Convert GT frequency to 50 HZ units */
		min_gpu_freq /= GEN9_FREQ_SCALER;
		max_gpu_freq /= GEN9_FREQ_SCALER;
	}

	seq_puts(m, "GPU freq (MHz)\tEffective CPU freq (MHz)\tEffective Ring freq (MHz)\n");

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);
	for (gpu_freq = min_gpu_freq; gpu_freq <= max_gpu_freq; gpu_freq++) {
		ia_freq = gpu_freq;
		snb_pcode_read(i915, GEN6_PCODE_READ_MIN_FREQ_TABLE,
			       &ia_freq, NULL);
		seq_printf(m, "%d\t\t%d\t\t\t\t%d\n",
			   intel_gpu_freq(rps,
					  (gpu_freq *
					   (IS_GEN9_BC(i915) ||
					    GRAPHICS_VER(i915) >= 11 ?
					    GEN9_FREQ_SCALER : 1))),
			   ((ia_freq >> 0) & 0xff) * 100,
			   ((ia_freq >> 8) & 0xff) * 100);
	}
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);

	return 0;
}

static bool llc_eval(void *data)
{
	struct intel_gt *gt = data;

	return HAS_LLC(gt->i915);
}

DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(llc);

static const char *rps_power_to_str(unsigned int power)
{
	static const char * const strings[] = {
		[LOW_POWER] = "low power",
		[BETWEEN] = "mixed",
		[HIGH_POWER] = "high power",
	};

	if (power >= ARRAY_SIZE(strings) || !strings[power])
		return "unknown";

	return strings[power];
}

static int rps_boost_show(struct seq_file *m, void *data)
{
	struct intel_gt *gt = m->private;
	struct drm_i915_private *i915 = gt->i915;
	struct intel_rps *rps = &gt->rps;

	seq_printf(m, "RPS enabled? %s\n", yesno(intel_rps_is_enabled(rps)));
	seq_printf(m, "RPS active? %s\n", yesno(intel_rps_is_active(rps)));
	seq_printf(m, "GPU busy? %s, %llums\n",
		   yesno(gt->awake),
		   ktime_to_ms(intel_gt_get_awake_time(gt)));
	seq_printf(m, "Boosts outstanding? %d\n",
		   atomic_read(&rps->num_waiters));
	seq_printf(m, "Interactive? %d\n", READ_ONCE(rps->power.interactive));
	seq_printf(m, "Frequency requested %d, actual %d\n",
		   intel_gpu_freq(rps, rps->cur_freq),
		   intel_rps_read_actual_frequency(rps));
	seq_printf(m, "  min hard:%d, soft:%d; max soft:%d, hard:%d\n",
		   intel_gpu_freq(rps, rps->min_freq),
		   intel_gpu_freq(rps, rps->min_freq_softlimit),
		   intel_gpu_freq(rps, rps->max_freq_softlimit),
		   intel_gpu_freq(rps, rps->max_freq));
	seq_printf(m, "  idle:%d, efficient:%d, boost:%d\n",
		   intel_gpu_freq(rps, rps->idle_freq),
		   intel_gpu_freq(rps, rps->efficient_freq),
		   intel_gpu_freq(rps, rps->boost_freq));

	seq_printf(m, "Wait boosts: %d\n", READ_ONCE(rps->boosts));

	if (GRAPHICS_VER(i915) >= 6 && intel_rps_is_active(rps)) {
		struct intel_uncore *uncore = gt->uncore;
		u32 rpup, rpupei;
		u32 rpdown, rpdownei;

		intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);
		rpup = intel_uncore_read_fw(uncore, GEN6_RP_CUR_UP) & GEN6_RP_EI_MASK;
		rpupei = intel_uncore_read_fw(uncore, GEN6_RP_CUR_UP_EI) & GEN6_RP_EI_MASK;
		rpdown = intel_uncore_read_fw(uncore, GEN6_RP_CUR_DOWN) & GEN6_RP_EI_MASK;
		rpdownei = intel_uncore_read_fw(uncore, GEN6_RP_CUR_DOWN_EI) & GEN6_RP_EI_MASK;
		intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);

		seq_printf(m, "\nRPS Autotuning (current \"%s\" window):\n",
			   rps_power_to_str(rps->power.mode));
		seq_printf(m, "  Avg. up: %d%% [above threshold? %d%%]\n",
			   rpup && rpupei ? 100 * rpup / rpupei : 0,
			   rps->power.up_threshold);
		seq_printf(m, "  Avg. down: %d%% [below threshold? %d%%]\n",
			   rpdown && rpdownei ? 100 * rpdown / rpdownei : 0,
			   rps->power.down_threshold);
	} else {
		seq_puts(m, "\nRPS Autotuning inactive\n");
	}

	return 0;
}

static bool rps_eval(void *data)
{
	struct intel_gt *gt = data;

	return HAS_RPS(gt->i915);
}

DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(rps_boost);

void intel_gt_pm_debugfs_register(struct intel_gt *gt, struct dentry *root)
{
	static const struct intel_gt_debugfs_file files[] = {
		{ "drpc", &drpc_fops, NULL },
		{ "frequency", &frequency_fops, NULL },
		{ "forcewake", &fw_domains_fops, NULL },
		{ "forcewake_user", &forcewake_user_fops, NULL},
		{ "llc", &llc_fops, llc_eval },
		{ "rps_boost", &rps_boost_fops, rps_eval },
	};

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), gt);
}
