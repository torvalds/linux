// SPDX-License-Identifier: MIT

/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/seq_file.h>
#include <linux/string_helpers.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_gt.h"
#include "intel_gt_clock_utils.h"
#include "intel_gt_debugfs.h"
#include "intel_gt_pm.h"
#include "intel_gt_pm_debugfs.h"
#include "intel_gt_regs.h"
#include "intel_llc.h"
#include "intel_mchbar_regs.h"
#include "intel_pcode.h"
#include "intel_rc6.h"
#include "intel_rps.h"
#include "intel_runtime_pm.h"
#include "intel_uncore.h"
#include "vlv_iosf_sb.h"

void intel_gt_pm_debugfs_forcewake_user_open(struct intel_gt *gt)
{
	atomic_inc(&gt->user_wakeref);
	intel_gt_pm_get_untracked(gt);
	if (GRAPHICS_VER(gt->i915) >= 6)
		intel_uncore_forcewake_user_get(gt->uncore);
}

void intel_gt_pm_debugfs_forcewake_user_release(struct intel_gt *gt)
{
	if (GRAPHICS_VER(gt->i915) >= 6)
		intel_uncore_forcewake_user_put(gt->uncore);
	intel_gt_pm_put_untracked(gt);
	atomic_dec(&gt->user_wakeref);
}

static int forcewake_user_open(struct inode *inode, struct file *file)
{
	struct intel_gt *gt = inode->i_private;

	intel_gt_pm_debugfs_forcewake_user_open(gt);

	return 0;
}

static int forcewake_user_release(struct inode *inode, struct file *file)
{
	struct intel_gt *gt = inode->i_private;

	intel_gt_pm_debugfs_forcewake_user_release(gt);

	return 0;
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

	spin_lock_irq(&uncore->lock);

	seq_printf(m, "user.bypass_count = %u\n",
		   uncore->user_forcewake_count);

	for_each_fw_domain(fw_domain, uncore, tmp)
		seq_printf(m, "%s.wake_count = %u\n",
			   intel_uncore_forcewake_domain_to_str(fw_domain->id),
			   READ_ONCE(fw_domain->wake_count));

	spin_unlock_irq(&uncore->lock);

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(fw_domains);

static int vlv_drpc(struct seq_file *m)
{
	struct intel_gt *gt = m->private;
	struct intel_uncore *uncore = gt->uncore;
	u32 rcctl1, pw_status, mt_fwake_req;

	mt_fwake_req = intel_uncore_read_fw(uncore, FORCEWAKE_MT);
	pw_status = intel_uncore_read(uncore, VLV_GTLC_PW_STATUS);
	rcctl1 = intel_uncore_read(uncore, GEN6_RC_CONTROL);

	seq_printf(m, "RC6 Enabled: %s\n",
		   str_yes_no(rcctl1 & (GEN7_RC_CTL_TO_MODE |
					GEN6_RC_CTL_EI_MODE(1))));
	seq_printf(m, "Multi-threaded Forcewake Request: 0x%x\n", mt_fwake_req);
	seq_printf(m, "Render Power Well: %s\n",
		   (pw_status & VLV_GTLC_PW_RENDER_STATUS_MASK) ? "Up" : "Down");
	seq_printf(m, "Media Power Well: %s\n",
		   (pw_status & VLV_GTLC_PW_MEDIA_STATUS_MASK) ? "Up" : "Down");

	intel_rc6_print_residency(m, "Render RC6 residency since boot:", INTEL_RC6_RES_RC6);
	intel_rc6_print_residency(m, "Media RC6 residency since boot:", INTEL_RC6_RES_VLV_MEDIA);

	return fw_domains_show(m, NULL);
}

static int gen6_drpc(struct seq_file *m)
{
	struct intel_gt *gt = m->private;
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	u32 gt_core_status, mt_fwake_req, rcctl1, rc6vids = 0;
	u32 gen9_powergate_enable = 0, gen9_powergate_status = 0;

	mt_fwake_req = intel_uncore_read_fw(uncore, FORCEWAKE_MT);
	gt_core_status = intel_uncore_read_fw(uncore, GEN6_GT_CORE_STATUS);

	rcctl1 = intel_uncore_read(uncore, GEN6_RC_CONTROL);
	if (GRAPHICS_VER(i915) >= 9) {
		gen9_powergate_enable =
			intel_uncore_read(uncore, GEN9_PG_ENABLE);
		gen9_powergate_status =
			intel_uncore_read(uncore, GEN9_PWRGT_DOMAIN_STATUS);
	}

	if (GRAPHICS_VER(i915) <= 7)
		snb_pcode_read(gt->uncore, GEN6_PCODE_READ_RC6VIDS, &rc6vids, NULL);

	seq_printf(m, "RC1e Enabled: %s\n",
		   str_yes_no(rcctl1 & GEN6_RC_CTL_RC1e_ENABLE));
	seq_printf(m, "RC6 Enabled: %s\n",
		   str_yes_no(rcctl1 & GEN6_RC_CTL_RC6_ENABLE));
	if (GRAPHICS_VER(i915) >= 9) {
		seq_printf(m, "Render Well Gating Enabled: %s\n",
			   str_yes_no(gen9_powergate_enable & GEN9_RENDER_PG_ENABLE));
		seq_printf(m, "Media Well Gating Enabled: %s\n",
			   str_yes_no(gen9_powergate_enable & GEN9_MEDIA_PG_ENABLE));
	}
	seq_printf(m, "Deep RC6 Enabled: %s\n",
		   str_yes_no(rcctl1 & GEN6_RC_CTL_RC6p_ENABLE));
	seq_printf(m, "Deepest RC6 Enabled: %s\n",
		   str_yes_no(rcctl1 & GEN6_RC_CTL_RC6pp_ENABLE));
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
		   str_yes_no(gt_core_status & GEN6_CORE_CPD_STATE_MASK));
	seq_printf(m, "Multi-threaded Forcewake Request: 0x%x\n", mt_fwake_req);
	if (GRAPHICS_VER(i915) >= 9) {
		seq_printf(m, "Render Power Well: %s\n",
			   (gen9_powergate_status &
			    GEN9_PWRGT_RENDER_STATUS_MASK) ? "Up" : "Down");
		seq_printf(m, "Media Power Well: %s\n",
			   (gen9_powergate_status &
			    GEN9_PWRGT_MEDIA_STATUS_MASK) ? "Up" : "Down");
	}

	/* Not exactly sure what this is */
	intel_rc6_print_residency(m, "RC6 \"Locked to RPn\" residency since boot:",
				  INTEL_RC6_RES_RC6_LOCKED);
	intel_rc6_print_residency(m, "RC6 residency since boot:", INTEL_RC6_RES_RC6);
	intel_rc6_print_residency(m, "RC6+ residency since boot:", INTEL_RC6_RES_RC6p);
	intel_rc6_print_residency(m, "RC6++ residency since boot:", INTEL_RC6_RES_RC6pp);

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

	seq_printf(m, "HD boost: %s\n",
		   str_yes_no(rgvmodectl & MEMMODE_BOOST_EN));
	seq_printf(m, "Boost freq: %d\n",
		   (rgvmodectl & MEMMODE_BOOST_FREQ_MASK) >>
		   MEMMODE_BOOST_FREQ_SHIFT);
	seq_printf(m, "HW control enabled: %s\n",
		   str_yes_no(rgvmodectl & MEMMODE_HWIDLE_EN));
	seq_printf(m, "SW control enabled: %s\n",
		   str_yes_no(rgvmodectl & MEMMODE_SWMODE_EN));
	seq_printf(m, "Gated voltage change: %s\n",
		   str_yes_no(rgvmodectl & MEMMODE_RCLK_GATE));
	seq_printf(m, "Starting frequency: P%d\n",
		   (rgvmodectl & MEMMODE_FSTART_MASK) >> MEMMODE_FSTART_SHIFT);
	seq_printf(m, "Max P-state: P%d\n",
		   (rgvmodectl & MEMMODE_FMAX_MASK) >> MEMMODE_FMAX_SHIFT);
	seq_printf(m, "Min P-state: P%d\n", (rgvmodectl & MEMMODE_FMIN_MASK));
	seq_printf(m, "RS1 VID: %d\n", (crstandvid & 0x3f));
	seq_printf(m, "RS2 VID: %d\n", ((crstandvid >> 8) & 0x3f));
	seq_printf(m, "Render standby enabled: %s\n",
		   str_yes_no(!(rstdbyctl & RCX_SW_EXIT)));
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

static int mtl_drpc(struct seq_file *m)
{
	struct intel_gt *gt = m->private;
	struct intel_uncore *uncore = gt->uncore;
	u32 gt_core_status, rcctl1, mt_fwake_req;
	u32 mtl_powergate_enable = 0, mtl_powergate_status = 0;

	mt_fwake_req = intel_uncore_read_fw(uncore, FORCEWAKE_MT);
	gt_core_status = intel_uncore_read(uncore, MTL_MIRROR_TARGET_WP1);

	rcctl1 = intel_uncore_read(uncore, GEN6_RC_CONTROL);
	mtl_powergate_enable = intel_uncore_read(uncore, GEN9_PG_ENABLE);
	mtl_powergate_status = intel_uncore_read(uncore,
						 GEN9_PWRGT_DOMAIN_STATUS);

	seq_printf(m, "RC6 Enabled: %s\n",
		   str_yes_no(rcctl1 & GEN6_RC_CTL_RC6_ENABLE));
	if (gt->type == GT_MEDIA) {
		seq_printf(m, "Media Well Gating Enabled: %s\n",
			   str_yes_no(mtl_powergate_enable & GEN9_MEDIA_PG_ENABLE));
	} else {
		seq_printf(m, "Render Well Gating Enabled: %s\n",
			   str_yes_no(mtl_powergate_enable & GEN9_RENDER_PG_ENABLE));
	}

	seq_puts(m, "Current RC state: ");
	switch (REG_FIELD_GET(MTL_CC_MASK, gt_core_status)) {
	case MTL_CC0:
		seq_puts(m, "RC0\n");
		break;
	case MTL_CC6:
		seq_puts(m, "RC6\n");
		break;
	default:
		seq_puts(m, "Unknown\n");
		break;
	}

	seq_printf(m, "Multi-threaded Forcewake Request: 0x%x\n", mt_fwake_req);
	if (gt->type == GT_MEDIA)
		seq_printf(m, "Media Power Well: %s\n",
			   (mtl_powergate_status &
			    GEN9_PWRGT_MEDIA_STATUS_MASK) ? "Up" : "Down");
	else
		seq_printf(m, "Render Power Well: %s\n",
			   (mtl_powergate_status &
			    GEN9_PWRGT_RENDER_STATUS_MASK) ? "Up" : "Down");

	/* Works for both render and media gt's */
	intel_rc6_print_residency(m, "RC6 residency since boot:", INTEL_RC6_RES_RC6);

	return fw_domains_show(m, NULL);
}

static int drpc_show(struct seq_file *m, void *unused)
{
	struct intel_gt *gt = m->private;
	struct drm_i915_private *i915 = gt->i915;
	intel_wakeref_t wakeref;
	int err = -ENODEV;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref) {
		if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 70))
			err = mtl_drpc(m);
		else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
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
			   REG_FIELD_GET(MEMSTAT_PSTATE_MASK, rgvstat));
	} else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915)) {
		u32 rpmodectl, freq_sts;

		rpmodectl = intel_uncore_read(uncore, GEN6_RP_CONTROL);
		drm_printf(p, "Video Turbo Mode: %s\n",
			   str_yes_no(rpmodectl & GEN6_RP_MEDIA_TURBO));
		drm_printf(p, "HW control enabled: %s\n",
			   str_yes_no(rpmodectl & GEN6_RP_ENABLE));
		drm_printf(p, "SW control enabled: %s\n",
			   str_yes_no((rpmodectl & GEN6_RP_MEDIA_MODE_MASK) == GEN6_RP_MEDIA_SW_MODE));

		vlv_iosf_sb_get(&i915->drm, BIT(VLV_IOSF_SB_PUNIT));
		freq_sts = vlv_iosf_sb_read(&i915->drm, VLV_IOSF_SB_PUNIT, PUNIT_REG_GPU_FREQ_STS);
		vlv_iosf_sb_put(&i915->drm, BIT(VLV_IOSF_SB_PUNIT));

		drm_printf(p, "PUNIT_REG_GPU_FREQ_STS: 0x%08x\n", freq_sts);

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
		gen6_rps_frequency_dump(rps, p);
	} else {
		drm_puts(p, "no P-state info available\n");
	}

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

	seq_printf(m, "LLC: %s\n", str_yes_no(HAS_LLC(i915)));
	seq_printf(m, "%s: %uMB\n", edram ? "eDRAM" : "eLLC",
		   i915->edram_size_mb);

	min_gpu_freq = rps->min_freq;
	max_gpu_freq = rps->max_freq;
	if (IS_GEN9_BC(i915) || GRAPHICS_VER(i915) >= 11) {
		/* Convert GT frequency to 50 HZ units */
		min_gpu_freq /= GEN9_FREQ_SCALER;
		max_gpu_freq /= GEN9_FREQ_SCALER;
	}

	seq_puts(m, "GPU freq (MHz)\tEffective GPU freq (MHz)\tEffective Ring freq (MHz)\n");

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);
	for (gpu_freq = min_gpu_freq; gpu_freq <= max_gpu_freq; gpu_freq++) {
		ia_freq = gpu_freq;
		snb_pcode_read(gt->uncore, GEN6_PCODE_READ_MIN_FREQ_TABLE,
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

	seq_printf(m, "RPS enabled? %s\n",
		   str_yes_no(intel_rps_is_enabled(rps)));
	seq_printf(m, "RPS active? %s\n",
		   str_yes_no(intel_rps_is_active(rps)));
	seq_printf(m, "GPU busy? %s, %llums\n",
		   str_yes_no(gt->awake),
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

	if (intel_guc_slpc_is_used(gt_to_guc(gt)))
		return false;
	else
		return HAS_RPS(gt->i915);
}

DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(rps_boost);

static int perf_limit_reasons_get(void *data, u64 *val)
{
	struct intel_gt *gt = data;
	intel_wakeref_t wakeref;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		*val = intel_uncore_read(gt->uncore, intel_gt_perf_limit_reasons_reg(gt));

	return 0;
}

static int perf_limit_reasons_clear(void *data, u64 val)
{
	struct intel_gt *gt = data;
	intel_wakeref_t wakeref;

	/*
	 * Clear the upper 16 "log" bits, the lower 16 "status" bits are
	 * read-only. The upper 16 "log" bits are identical to the lower 16
	 * "status" bits except that the "log" bits remain set until cleared.
	 */
	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		intel_uncore_rmw(gt->uncore, intel_gt_perf_limit_reasons_reg(gt),
				 GT0_PERF_LIMIT_REASONS_LOG_MASK, 0);

	return 0;
}

static bool perf_limit_reasons_eval(void *data)
{
	struct intel_gt *gt = data;

	return i915_mmio_reg_valid(intel_gt_perf_limit_reasons_reg(gt));
}

DEFINE_SIMPLE_ATTRIBUTE(perf_limit_reasons_fops, perf_limit_reasons_get,
			perf_limit_reasons_clear, "0x%llx\n");

void intel_gt_pm_debugfs_register(struct intel_gt *gt, struct dentry *root)
{
	static const struct intel_gt_debugfs_file files[] = {
		{ "drpc", &drpc_fops, NULL },
		{ "frequency", &frequency_fops, NULL },
		{ "forcewake", &fw_domains_fops, NULL },
		{ "forcewake_user", &forcewake_user_fops, NULL},
		{ "llc", &llc_fops, llc_eval },
		{ "rps_boost", &rps_boost_fops, rps_eval },
		{ "perf_limit_reasons", &perf_limit_reasons_fops, perf_limit_reasons_eval },
	};

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), gt);
}
