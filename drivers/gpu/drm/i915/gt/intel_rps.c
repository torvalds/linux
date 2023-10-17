// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/string_helpers.h>

#include <drm/i915_drm.h>

#include "display/intel_display.h"
#include "display/intel_display_irq.h"
#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_reg.h"
#include "intel_breadcrumbs.h"
#include "intel_gt.h"
#include "intel_gt_clock_utils.h"
#include "intel_gt_irq.h"
#include "intel_gt_pm.h"
#include "intel_gt_pm_irq.h"
#include "intel_gt_print.h"
#include "intel_gt_regs.h"
#include "intel_mchbar_regs.h"
#include "intel_pcode.h"
#include "intel_rps.h"
#include "vlv_sideband.h"
#include "../../../platform/x86/intel_ips.h"

#define BUSY_MAX_EI	20u /* ms */

/*
 * Lock protecting IPS related data structures
 */
static DEFINE_SPINLOCK(mchdev_lock);

static struct intel_gt *rps_to_gt(struct intel_rps *rps)
{
	return container_of(rps, struct intel_gt, rps);
}

static struct drm_i915_private *rps_to_i915(struct intel_rps *rps)
{
	return rps_to_gt(rps)->i915;
}

static struct intel_uncore *rps_to_uncore(struct intel_rps *rps)
{
	return rps_to_gt(rps)->uncore;
}

static struct intel_guc_slpc *rps_to_slpc(struct intel_rps *rps)
{
	struct intel_gt *gt = rps_to_gt(rps);

	return &gt->uc.guc.slpc;
}

static bool rps_uses_slpc(struct intel_rps *rps)
{
	struct intel_gt *gt = rps_to_gt(rps);

	return intel_uc_uses_guc_slpc(&gt->uc);
}

static u32 rps_pm_sanitize_mask(struct intel_rps *rps, u32 mask)
{
	return mask & ~rps->pm_intrmsk_mbz;
}

static void set(struct intel_uncore *uncore, i915_reg_t reg, u32 val)
{
	intel_uncore_write_fw(uncore, reg, val);
}

static void rps_timer(struct timer_list *t)
{
	struct intel_rps *rps = from_timer(rps, t, timer);
	struct intel_gt *gt = rps_to_gt(rps);
	struct intel_engine_cs *engine;
	ktime_t dt, last, timestamp;
	enum intel_engine_id id;
	s64 max_busy[3] = {};

	timestamp = 0;
	for_each_engine(engine, gt, id) {
		s64 busy;
		int i;

		dt = intel_engine_get_busy_time(engine, &timestamp);
		last = engine->stats.rps;
		engine->stats.rps = dt;

		busy = ktime_to_ns(ktime_sub(dt, last));
		for (i = 0; i < ARRAY_SIZE(max_busy); i++) {
			if (busy > max_busy[i])
				swap(busy, max_busy[i]);
		}
	}
	last = rps->pm_timestamp;
	rps->pm_timestamp = timestamp;

	if (intel_rps_is_active(rps)) {
		s64 busy;
		int i;

		dt = ktime_sub(timestamp, last);

		/*
		 * Our goal is to evaluate each engine independently, so we run
		 * at the lowest clocks required to sustain the heaviest
		 * workload. However, a task may be split into sequential
		 * dependent operations across a set of engines, such that
		 * the independent contributions do not account for high load,
		 * but overall the task is GPU bound. For example, consider
		 * video decode on vcs followed by colour post-processing
		 * on vecs, followed by general post-processing on rcs.
		 * Since multi-engines being active does imply a single
		 * continuous workload across all engines, we hedge our
		 * bets by only contributing a factor of the distributed
		 * load into our busyness calculation.
		 */
		busy = max_busy[0];
		for (i = 1; i < ARRAY_SIZE(max_busy); i++) {
			if (!max_busy[i])
				break;

			busy += div_u64(max_busy[i], 1 << i);
		}
		GT_TRACE(gt,
			 "busy:%lld [%d%%], max:[%lld, %lld, %lld], interval:%d\n",
			 busy, (int)div64_u64(100 * busy, dt),
			 max_busy[0], max_busy[1], max_busy[2],
			 rps->pm_interval);

		if (100 * busy > rps->power.up_threshold * dt &&
		    rps->cur_freq < rps->max_freq_softlimit) {
			rps->pm_iir |= GEN6_PM_RP_UP_THRESHOLD;
			rps->pm_interval = 1;
			queue_work(gt->i915->unordered_wq, &rps->work);
		} else if (100 * busy < rps->power.down_threshold * dt &&
			   rps->cur_freq > rps->min_freq_softlimit) {
			rps->pm_iir |= GEN6_PM_RP_DOWN_THRESHOLD;
			rps->pm_interval = 1;
			queue_work(gt->i915->unordered_wq, &rps->work);
		} else {
			rps->last_adj = 0;
		}

		mod_timer(&rps->timer,
			  jiffies + msecs_to_jiffies(rps->pm_interval));
		rps->pm_interval = min(rps->pm_interval * 2, BUSY_MAX_EI);
	}
}

static void rps_start_timer(struct intel_rps *rps)
{
	rps->pm_timestamp = ktime_sub(ktime_get(), rps->pm_timestamp);
	rps->pm_interval = 1;
	mod_timer(&rps->timer, jiffies + 1);
}

static void rps_stop_timer(struct intel_rps *rps)
{
	del_timer_sync(&rps->timer);
	rps->pm_timestamp = ktime_sub(ktime_get(), rps->pm_timestamp);
	cancel_work_sync(&rps->work);
}

static u32 rps_pm_mask(struct intel_rps *rps, u8 val)
{
	u32 mask = 0;

	/* We use UP_EI_EXPIRED interrupts for both up/down in manual mode */
	if (val > rps->min_freq_softlimit)
		mask |= (GEN6_PM_RP_UP_EI_EXPIRED |
			 GEN6_PM_RP_DOWN_THRESHOLD |
			 GEN6_PM_RP_DOWN_TIMEOUT);

	if (val < rps->max_freq_softlimit)
		mask |= GEN6_PM_RP_UP_EI_EXPIRED | GEN6_PM_RP_UP_THRESHOLD;

	mask &= rps->pm_events;

	return rps_pm_sanitize_mask(rps, ~mask);
}

static void rps_reset_ei(struct intel_rps *rps)
{
	memset(&rps->ei, 0, sizeof(rps->ei));
}

static void rps_enable_interrupts(struct intel_rps *rps)
{
	struct intel_gt *gt = rps_to_gt(rps);

	GEM_BUG_ON(rps_uses_slpc(rps));

	GT_TRACE(gt, "interrupts:on rps->pm_events: %x, rps_pm_mask:%x\n",
		 rps->pm_events, rps_pm_mask(rps, rps->last_freq));

	rps_reset_ei(rps);

	spin_lock_irq(gt->irq_lock);
	gen6_gt_pm_enable_irq(gt, rps->pm_events);
	spin_unlock_irq(gt->irq_lock);

	intel_uncore_write(gt->uncore,
			   GEN6_PMINTRMSK, rps_pm_mask(rps, rps->last_freq));
}

static void gen6_rps_reset_interrupts(struct intel_rps *rps)
{
	gen6_gt_pm_reset_iir(rps_to_gt(rps), GEN6_PM_RPS_EVENTS);
}

static void gen11_rps_reset_interrupts(struct intel_rps *rps)
{
	while (gen11_gt_reset_one_iir(rps_to_gt(rps), 0, GEN11_GTPM))
		;
}

static void rps_reset_interrupts(struct intel_rps *rps)
{
	struct intel_gt *gt = rps_to_gt(rps);

	spin_lock_irq(gt->irq_lock);
	if (GRAPHICS_VER(gt->i915) >= 11)
		gen11_rps_reset_interrupts(rps);
	else
		gen6_rps_reset_interrupts(rps);

	rps->pm_iir = 0;
	spin_unlock_irq(gt->irq_lock);
}

static void rps_disable_interrupts(struct intel_rps *rps)
{
	struct intel_gt *gt = rps_to_gt(rps);

	intel_uncore_write(gt->uncore,
			   GEN6_PMINTRMSK, rps_pm_sanitize_mask(rps, ~0u));

	spin_lock_irq(gt->irq_lock);
	gen6_gt_pm_disable_irq(gt, GEN6_PM_RPS_EVENTS);
	spin_unlock_irq(gt->irq_lock);

	intel_synchronize_irq(gt->i915);

	/*
	 * Now that we will not be generating any more work, flush any
	 * outstanding tasks. As we are called on the RPS idle path,
	 * we will reset the GPU to minimum frequencies, so the current
	 * state of the worker can be discarded.
	 */
	cancel_work_sync(&rps->work);

	rps_reset_interrupts(rps);
	GT_TRACE(gt, "interrupts:off\n");
}

static const struct cparams {
	u16 i;
	u16 t;
	u16 m;
	u16 c;
} cparams[] = {
	{ 1, 1333, 301, 28664 },
	{ 1, 1066, 294, 24460 },
	{ 1, 800, 294, 25192 },
	{ 0, 1333, 276, 27605 },
	{ 0, 1066, 276, 27605 },
	{ 0, 800, 231, 23784 },
};

static void gen5_rps_init(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_uncore *uncore = rps_to_uncore(rps);
	u8 fmax, fmin, fstart;
	u32 rgvmodectl;
	int c_m, i;

	if (i915->fsb_freq <= 3200)
		c_m = 0;
	else if (i915->fsb_freq <= 4800)
		c_m = 1;
	else
		c_m = 2;

	for (i = 0; i < ARRAY_SIZE(cparams); i++) {
		if (cparams[i].i == c_m && cparams[i].t == i915->mem_freq) {
			rps->ips.m = cparams[i].m;
			rps->ips.c = cparams[i].c;
			break;
		}
	}

	rgvmodectl = intel_uncore_read(uncore, MEMMODECTL);

	/* Set up min, max, and cur for interrupt handling */
	fmax = (rgvmodectl & MEMMODE_FMAX_MASK) >> MEMMODE_FMAX_SHIFT;
	fmin = (rgvmodectl & MEMMODE_FMIN_MASK);
	fstart = (rgvmodectl & MEMMODE_FSTART_MASK) >>
		MEMMODE_FSTART_SHIFT;
	drm_dbg(&i915->drm, "fmax: %d, fmin: %d, fstart: %d\n",
		fmax, fmin, fstart);

	rps->min_freq = fmax;
	rps->efficient_freq = fstart;
	rps->max_freq = fmin;
}

static unsigned long
__ips_chipset_val(struct intel_ips *ips)
{
	struct intel_uncore *uncore =
		rps_to_uncore(container_of(ips, struct intel_rps, ips));
	unsigned long now = jiffies_to_msecs(jiffies), dt;
	unsigned long result;
	u64 total, delta;

	lockdep_assert_held(&mchdev_lock);

	/*
	 * Prevent division-by-zero if we are asking too fast.
	 * Also, we don't get interesting results if we are polling
	 * faster than once in 10ms, so just return the saved value
	 * in such cases.
	 */
	dt = now - ips->last_time1;
	if (dt <= 10)
		return ips->chipset_power;

	/* FIXME: handle per-counter overflow */
	total = intel_uncore_read(uncore, DMIEC);
	total += intel_uncore_read(uncore, DDREC);
	total += intel_uncore_read(uncore, CSIEC);

	delta = total - ips->last_count1;

	result = div_u64(div_u64(ips->m * delta, dt) + ips->c, 10);

	ips->last_count1 = total;
	ips->last_time1 = now;

	ips->chipset_power = result;

	return result;
}

static unsigned long ips_mch_val(struct intel_uncore *uncore)
{
	unsigned int m, x, b;
	u32 tsfs;

	tsfs = intel_uncore_read(uncore, TSFS);
	x = intel_uncore_read8(uncore, TR1);

	b = tsfs & TSFS_INTR_MASK;
	m = (tsfs & TSFS_SLOPE_MASK) >> TSFS_SLOPE_SHIFT;

	return m * x / 127 - b;
}

static int _pxvid_to_vd(u8 pxvid)
{
	if (pxvid == 0)
		return 0;

	if (pxvid >= 8 && pxvid < 31)
		pxvid = 31;

	return (pxvid + 2) * 125;
}

static u32 pvid_to_extvid(struct drm_i915_private *i915, u8 pxvid)
{
	const int vd = _pxvid_to_vd(pxvid);

	if (INTEL_INFO(i915)->is_mobile)
		return max(vd - 1125, 0);

	return vd;
}

static void __gen5_ips_update(struct intel_ips *ips)
{
	struct intel_uncore *uncore =
		rps_to_uncore(container_of(ips, struct intel_rps, ips));
	u64 now, delta, dt;
	u32 count;

	lockdep_assert_held(&mchdev_lock);

	now = ktime_get_raw_ns();
	dt = now - ips->last_time2;
	do_div(dt, NSEC_PER_MSEC);

	/* Don't divide by 0 */
	if (dt <= 10)
		return;

	count = intel_uncore_read(uncore, GFXEC);
	delta = count - ips->last_count2;

	ips->last_count2 = count;
	ips->last_time2 = now;

	/* More magic constants... */
	ips->gfx_power = div_u64(delta * 1181, dt * 10);
}

static void gen5_rps_update(struct intel_rps *rps)
{
	spin_lock_irq(&mchdev_lock);
	__gen5_ips_update(&rps->ips);
	spin_unlock_irq(&mchdev_lock);
}

static unsigned int gen5_invert_freq(struct intel_rps *rps,
				     unsigned int val)
{
	/* Invert the frequency bin into an ips delay */
	val = rps->max_freq - val;
	val = rps->min_freq + val;

	return val;
}

static int __gen5_rps_set(struct intel_rps *rps, u8 val)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	u16 rgvswctl;

	lockdep_assert_held(&mchdev_lock);

	rgvswctl = intel_uncore_read16(uncore, MEMSWCTL);
	if (rgvswctl & MEMCTL_CMD_STS) {
		drm_dbg(&rps_to_i915(rps)->drm,
			"gpu busy, RCS change rejected\n");
		return -EBUSY; /* still busy with another command */
	}

	/* Invert the frequency bin into an ips delay */
	val = gen5_invert_freq(rps, val);

	rgvswctl =
		(MEMCTL_CMD_CHFREQ << MEMCTL_CMD_SHIFT) |
		(val << MEMCTL_FREQ_SHIFT) |
		MEMCTL_SFCAVM;
	intel_uncore_write16(uncore, MEMSWCTL, rgvswctl);
	intel_uncore_posting_read16(uncore, MEMSWCTL);

	rgvswctl |= MEMCTL_CMD_STS;
	intel_uncore_write16(uncore, MEMSWCTL, rgvswctl);

	return 0;
}

static int gen5_rps_set(struct intel_rps *rps, u8 val)
{
	int err;

	spin_lock_irq(&mchdev_lock);
	err = __gen5_rps_set(rps, val);
	spin_unlock_irq(&mchdev_lock);

	return err;
}

static unsigned long intel_pxfreq(u32 vidfreq)
{
	int div = (vidfreq & 0x3f0000) >> 16;
	int post = (vidfreq & 0x3000) >> 12;
	int pre = (vidfreq & 0x7);

	if (!pre)
		return 0;

	return div * 133333 / (pre << post);
}

static unsigned int init_emon(struct intel_uncore *uncore)
{
	u8 pxw[16];
	int i;

	/* Disable to program */
	intel_uncore_write(uncore, ECR, 0);
	intel_uncore_posting_read(uncore, ECR);

	/* Program energy weights for various events */
	intel_uncore_write(uncore, SDEW, 0x15040d00);
	intel_uncore_write(uncore, CSIEW0, 0x007f0000);
	intel_uncore_write(uncore, CSIEW1, 0x1e220004);
	intel_uncore_write(uncore, CSIEW2, 0x04000004);

	for (i = 0; i < 5; i++)
		intel_uncore_write(uncore, PEW(i), 0);
	for (i = 0; i < 3; i++)
		intel_uncore_write(uncore, DEW(i), 0);

	/* Program P-state weights to account for frequency power adjustment */
	for (i = 0; i < 16; i++) {
		u32 pxvidfreq = intel_uncore_read(uncore, PXVFREQ(i));
		unsigned int freq = intel_pxfreq(pxvidfreq);
		unsigned int vid =
			(pxvidfreq & PXVFREQ_PX_MASK) >> PXVFREQ_PX_SHIFT;
		unsigned int val;

		val = vid * vid * freq / 1000 * 255;
		val /= 127 * 127 * 900;

		pxw[i] = val;
	}
	/* Render standby states get 0 weight */
	pxw[14] = 0;
	pxw[15] = 0;

	for (i = 0; i < 4; i++) {
		intel_uncore_write(uncore, PXW(i),
				   pxw[i * 4 + 0] << 24 |
				   pxw[i * 4 + 1] << 16 |
				   pxw[i * 4 + 2] <<  8 |
				   pxw[i * 4 + 3] <<  0);
	}

	/* Adjust magic regs to magic values (more experimental results) */
	intel_uncore_write(uncore, OGW0, 0);
	intel_uncore_write(uncore, OGW1, 0);
	intel_uncore_write(uncore, EG0, 0x00007f00);
	intel_uncore_write(uncore, EG1, 0x0000000e);
	intel_uncore_write(uncore, EG2, 0x000e0000);
	intel_uncore_write(uncore, EG3, 0x68000300);
	intel_uncore_write(uncore, EG4, 0x42000000);
	intel_uncore_write(uncore, EG5, 0x00140031);
	intel_uncore_write(uncore, EG6, 0);
	intel_uncore_write(uncore, EG7, 0);

	for (i = 0; i < 8; i++)
		intel_uncore_write(uncore, PXWL(i), 0);

	/* Enable PMON + select events */
	intel_uncore_write(uncore, ECR, 0x80000019);

	return intel_uncore_read(uncore, LCFUSE02) & LCFUSE_HIV_MASK;
}

static bool gen5_rps_enable(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_uncore *uncore = rps_to_uncore(rps);
	u8 fstart, vstart;
	u32 rgvmodectl;

	spin_lock_irq(&mchdev_lock);

	rgvmodectl = intel_uncore_read(uncore, MEMMODECTL);

	/* Enable temp reporting */
	intel_uncore_write16(uncore, PMMISC,
			     intel_uncore_read16(uncore, PMMISC) | MCPPCE_EN);
	intel_uncore_write16(uncore, TSC1,
			     intel_uncore_read16(uncore, TSC1) | TSE);

	/* 100ms RC evaluation intervals */
	intel_uncore_write(uncore, RCUPEI, 100000);
	intel_uncore_write(uncore, RCDNEI, 100000);

	/* Set max/min thresholds to 90ms and 80ms respectively */
	intel_uncore_write(uncore, RCBMAXAVG, 90000);
	intel_uncore_write(uncore, RCBMINAVG, 80000);

	intel_uncore_write(uncore, MEMIHYST, 1);

	/* Set up min, max, and cur for interrupt handling */
	fstart = (rgvmodectl & MEMMODE_FSTART_MASK) >>
		MEMMODE_FSTART_SHIFT;

	vstart = (intel_uncore_read(uncore, PXVFREQ(fstart)) &
		  PXVFREQ_PX_MASK) >> PXVFREQ_PX_SHIFT;

	intel_uncore_write(uncore,
			   MEMINTREN,
			   MEMINT_CX_SUPR_EN | MEMINT_EVAL_CHG_EN);

	intel_uncore_write(uncore, VIDSTART, vstart);
	intel_uncore_posting_read(uncore, VIDSTART);

	rgvmodectl |= MEMMODE_SWMODE_EN;
	intel_uncore_write(uncore, MEMMODECTL, rgvmodectl);

	if (wait_for_atomic((intel_uncore_read(uncore, MEMSWCTL) &
			     MEMCTL_CMD_STS) == 0, 10))
		drm_err(&uncore->i915->drm,
			"stuck trying to change perf mode\n");
	mdelay(1);

	__gen5_rps_set(rps, rps->cur_freq);

	rps->ips.last_count1 = intel_uncore_read(uncore, DMIEC);
	rps->ips.last_count1 += intel_uncore_read(uncore, DDREC);
	rps->ips.last_count1 += intel_uncore_read(uncore, CSIEC);
	rps->ips.last_time1 = jiffies_to_msecs(jiffies);

	rps->ips.last_count2 = intel_uncore_read(uncore, GFXEC);
	rps->ips.last_time2 = ktime_get_raw_ns();

	spin_lock(&i915->irq_lock);
	ilk_enable_display_irq(i915, DE_PCU_EVENT);
	spin_unlock(&i915->irq_lock);

	spin_unlock_irq(&mchdev_lock);

	rps->ips.corr = init_emon(uncore);

	return true;
}

static void gen5_rps_disable(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_uncore *uncore = rps_to_uncore(rps);
	u16 rgvswctl;

	spin_lock_irq(&mchdev_lock);

	spin_lock(&i915->irq_lock);
	ilk_disable_display_irq(i915, DE_PCU_EVENT);
	spin_unlock(&i915->irq_lock);

	rgvswctl = intel_uncore_read16(uncore, MEMSWCTL);

	/* Ack interrupts, disable EFC interrupt */
	intel_uncore_rmw(uncore, MEMINTREN, MEMINT_EVAL_CHG_EN, 0);
	intel_uncore_write(uncore, MEMINTRSTS, MEMINT_EVAL_CHG);

	/* Go back to the starting frequency */
	__gen5_rps_set(rps, rps->idle_freq);
	mdelay(1);
	rgvswctl |= MEMCTL_CMD_STS;
	intel_uncore_write(uncore, MEMSWCTL, rgvswctl);
	mdelay(1);

	spin_unlock_irq(&mchdev_lock);
}

static u32 rps_limits(struct intel_rps *rps, u8 val)
{
	u32 limits;

	/*
	 * Only set the down limit when we've reached the lowest level to avoid
	 * getting more interrupts, otherwise leave this clear. This prevents a
	 * race in the hw when coming out of rc6: There's a tiny window where
	 * the hw runs at the minimal clock before selecting the desired
	 * frequency, if the down threshold expires in that window we will not
	 * receive a down interrupt.
	 */
	if (GRAPHICS_VER(rps_to_i915(rps)) >= 9) {
		limits = rps->max_freq_softlimit << 23;
		if (val <= rps->min_freq_softlimit)
			limits |= rps->min_freq_softlimit << 14;
	} else {
		limits = rps->max_freq_softlimit << 24;
		if (val <= rps->min_freq_softlimit)
			limits |= rps->min_freq_softlimit << 16;
	}

	return limits;
}

static void rps_set_power(struct intel_rps *rps, int new_power)
{
	struct intel_gt *gt = rps_to_gt(rps);
	struct intel_uncore *uncore = gt->uncore;
	u32 ei_up = 0, ei_down = 0;

	lockdep_assert_held(&rps->power.mutex);

	if (new_power == rps->power.mode)
		return;

	/* Note the units here are not exactly 1us, but 1280ns. */
	switch (new_power) {
	case LOW_POWER:
		ei_up = 16000;
		ei_down = 32000;
		break;

	case BETWEEN:
		ei_up = 13000;
		ei_down = 32000;
		break;

	case HIGH_POWER:
		ei_up = 10000;
		ei_down = 32000;
		break;
	}

	/* When byt can survive without system hang with dynamic
	 * sw freq adjustments, this restriction can be lifted.
	 */
	if (IS_VALLEYVIEW(gt->i915))
		goto skip_hw_write;

	GT_TRACE(gt,
		 "changing power mode [%d], up %d%% @ %dus, down %d%% @ %dus\n",
		 new_power,
		 rps->power.up_threshold, ei_up,
		 rps->power.down_threshold, ei_down);

	set(uncore, GEN6_RP_UP_EI,
	    intel_gt_ns_to_pm_interval(gt, ei_up * 1000));
	set(uncore, GEN6_RP_UP_THRESHOLD,
	    intel_gt_ns_to_pm_interval(gt,
				       ei_up * rps->power.up_threshold * 10));

	set(uncore, GEN6_RP_DOWN_EI,
	    intel_gt_ns_to_pm_interval(gt, ei_down * 1000));
	set(uncore, GEN6_RP_DOWN_THRESHOLD,
	    intel_gt_ns_to_pm_interval(gt,
				       ei_down *
				       rps->power.down_threshold * 10));

	set(uncore, GEN6_RP_CONTROL,
	    (GRAPHICS_VER(gt->i915) > 9 ? 0 : GEN6_RP_MEDIA_TURBO) |
	    GEN6_RP_MEDIA_HW_NORMAL_MODE |
	    GEN6_RP_MEDIA_IS_GFX |
	    GEN6_RP_ENABLE |
	    GEN6_RP_UP_BUSY_AVG |
	    GEN6_RP_DOWN_IDLE_AVG);

skip_hw_write:
	rps->power.mode = new_power;
}

static void gen6_rps_set_thresholds(struct intel_rps *rps, u8 val)
{
	int new_power;

	new_power = rps->power.mode;
	switch (rps->power.mode) {
	case LOW_POWER:
		if (val > rps->efficient_freq + 1 &&
		    val > rps->cur_freq)
			new_power = BETWEEN;
		break;

	case BETWEEN:
		if (val <= rps->efficient_freq &&
		    val < rps->cur_freq)
			new_power = LOW_POWER;
		else if (val >= rps->rp0_freq &&
			 val > rps->cur_freq)
			new_power = HIGH_POWER;
		break;

	case HIGH_POWER:
		if (val < (rps->rp1_freq + rps->rp0_freq) >> 1 &&
		    val < rps->cur_freq)
			new_power = BETWEEN;
		break;
	}
	/* Max/min bins are special */
	if (val <= rps->min_freq_softlimit)
		new_power = LOW_POWER;
	if (val >= rps->max_freq_softlimit)
		new_power = HIGH_POWER;

	mutex_lock(&rps->power.mutex);
	if (rps->power.interactive)
		new_power = HIGH_POWER;
	rps_set_power(rps, new_power);
	mutex_unlock(&rps->power.mutex);
}

void intel_rps_mark_interactive(struct intel_rps *rps, bool interactive)
{
	GT_TRACE(rps_to_gt(rps), "mark interactive: %s\n",
		 str_yes_no(interactive));

	mutex_lock(&rps->power.mutex);
	if (interactive) {
		if (!rps->power.interactive++ && intel_rps_is_active(rps))
			rps_set_power(rps, HIGH_POWER);
	} else {
		GEM_BUG_ON(!rps->power.interactive);
		rps->power.interactive--;
	}
	mutex_unlock(&rps->power.mutex);
}

static int gen6_rps_set(struct intel_rps *rps, u8 val)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 swreq;

	GEM_BUG_ON(rps_uses_slpc(rps));

	if (GRAPHICS_VER(i915) >= 9)
		swreq = GEN9_FREQUENCY(val);
	else if (IS_HASWELL(i915) || IS_BROADWELL(i915))
		swreq = HSW_FREQUENCY(val);
	else
		swreq = (GEN6_FREQUENCY(val) |
			 GEN6_OFFSET(0) |
			 GEN6_AGGRESSIVE_TURBO);
	set(uncore, GEN6_RPNSWREQ, swreq);

	GT_TRACE(rps_to_gt(rps), "set val:%x, freq:%d, swreq:%x\n",
		 val, intel_gpu_freq(rps, val), swreq);

	return 0;
}

static int vlv_rps_set(struct intel_rps *rps, u8 val)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	int err;

	vlv_punit_get(i915);
	err = vlv_punit_write(i915, PUNIT_REG_GPU_FREQ_REQ, val);
	vlv_punit_put(i915);

	GT_TRACE(rps_to_gt(rps), "set val:%x, freq:%d\n",
		 val, intel_gpu_freq(rps, val));

	return err;
}

static int rps_set(struct intel_rps *rps, u8 val, bool update)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	int err;

	if (val == rps->last_freq)
		return 0;

	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		err = vlv_rps_set(rps, val);
	else if (GRAPHICS_VER(i915) >= 6)
		err = gen6_rps_set(rps, val);
	else
		err = gen5_rps_set(rps, val);
	if (err)
		return err;

	if (update && GRAPHICS_VER(i915) >= 6)
		gen6_rps_set_thresholds(rps, val);
	rps->last_freq = val;

	return 0;
}

void intel_rps_unpark(struct intel_rps *rps)
{
	if (!intel_rps_is_enabled(rps))
		return;

	GT_TRACE(rps_to_gt(rps), "unpark:%x\n", rps->cur_freq);

	/*
	 * Use the user's desired frequency as a guide, but for better
	 * performance, jump directly to RPe as our starting frequency.
	 */
	mutex_lock(&rps->lock);

	intel_rps_set_active(rps);
	intel_rps_set(rps,
		      clamp(rps->cur_freq,
			    rps->min_freq_softlimit,
			    rps->max_freq_softlimit));

	mutex_unlock(&rps->lock);

	rps->pm_iir = 0;
	if (intel_rps_has_interrupts(rps))
		rps_enable_interrupts(rps);
	if (intel_rps_uses_timer(rps))
		rps_start_timer(rps);

	if (GRAPHICS_VER(rps_to_i915(rps)) == 5)
		gen5_rps_update(rps);
}

void intel_rps_park(struct intel_rps *rps)
{
	int adj;

	if (!intel_rps_is_enabled(rps))
		return;

	if (!intel_rps_clear_active(rps))
		return;

	if (intel_rps_uses_timer(rps))
		rps_stop_timer(rps);
	if (intel_rps_has_interrupts(rps))
		rps_disable_interrupts(rps);

	if (rps->last_freq <= rps->idle_freq)
		return;

	/*
	 * The punit delays the write of the frequency and voltage until it
	 * determines the GPU is awake. During normal usage we don't want to
	 * waste power changing the frequency if the GPU is sleeping (rc6).
	 * However, the GPU and driver is now idle and we do not want to delay
	 * switching to minimum voltage (reducing power whilst idle) as we do
	 * not expect to be woken in the near future and so must flush the
	 * change by waking the device.
	 *
	 * We choose to take the media powerwell (either would do to trick the
	 * punit into committing the voltage change) as that takes a lot less
	 * power than the render powerwell.
	 */
	intel_uncore_forcewake_get(rps_to_uncore(rps), FORCEWAKE_MEDIA);
	rps_set(rps, rps->idle_freq, false);
	intel_uncore_forcewake_put(rps_to_uncore(rps), FORCEWAKE_MEDIA);

	/*
	 * Since we will try and restart from the previously requested
	 * frequency on unparking, treat this idle point as a downclock
	 * interrupt and reduce the frequency for resume. If we park/unpark
	 * more frequently than the rps worker can run, we will not respond
	 * to any EI and never see a change in frequency.
	 *
	 * (Note we accommodate Cherryview's limitation of only using an
	 * even bin by applying it to all.)
	 */
	adj = rps->last_adj;
	if (adj < 0)
		adj *= 2;
	else /* CHV needs even encode values */
		adj = -2;
	rps->last_adj = adj;
	rps->cur_freq = max_t(int, rps->cur_freq + adj, rps->min_freq);
	if (rps->cur_freq < rps->efficient_freq) {
		rps->cur_freq = rps->efficient_freq;
		rps->last_adj = 0;
	}

	GT_TRACE(rps_to_gt(rps), "park:%x\n", rps->cur_freq);
}

u32 intel_rps_get_boost_frequency(struct intel_rps *rps)
{
	struct intel_guc_slpc *slpc;

	if (rps_uses_slpc(rps)) {
		slpc = rps_to_slpc(rps);

		return slpc->boost_freq;
	} else {
		return intel_gpu_freq(rps, rps->boost_freq);
	}
}

static int rps_set_boost_freq(struct intel_rps *rps, u32 val)
{
	bool boost = false;

	/* Validate against (static) hardware limits */
	val = intel_freq_opcode(rps, val);
	if (val < rps->min_freq || val > rps->max_freq)
		return -EINVAL;

	mutex_lock(&rps->lock);
	if (val != rps->boost_freq) {
		rps->boost_freq = val;
		boost = atomic_read(&rps->num_waiters);
	}
	mutex_unlock(&rps->lock);
	if (boost)
		queue_work(rps_to_gt(rps)->i915->unordered_wq, &rps->work);

	return 0;
}

int intel_rps_set_boost_frequency(struct intel_rps *rps, u32 freq)
{
	struct intel_guc_slpc *slpc;

	if (rps_uses_slpc(rps)) {
		slpc = rps_to_slpc(rps);

		return intel_guc_slpc_set_boost_freq(slpc, freq);
	} else {
		return rps_set_boost_freq(rps, freq);
	}
}

void intel_rps_dec_waiters(struct intel_rps *rps)
{
	struct intel_guc_slpc *slpc;

	if (rps_uses_slpc(rps)) {
		slpc = rps_to_slpc(rps);

		intel_guc_slpc_dec_waiters(slpc);
	} else {
		atomic_dec(&rps->num_waiters);
	}
}

void intel_rps_boost(struct i915_request *rq)
{
	struct intel_guc_slpc *slpc;

	if (i915_request_signaled(rq) || i915_request_has_waitboost(rq))
		return;

	/* Serializes with i915_request_retire() */
	if (!test_and_set_bit(I915_FENCE_FLAG_BOOST, &rq->fence.flags)) {
		struct intel_rps *rps = &READ_ONCE(rq->engine)->gt->rps;

		if (rps_uses_slpc(rps)) {
			slpc = rps_to_slpc(rps);

			if (slpc->min_freq_softlimit >= slpc->boost_freq)
				return;

			/* Return if old value is non zero */
			if (!atomic_fetch_inc(&slpc->num_waiters)) {
				GT_TRACE(rps_to_gt(rps), "boost fence:%llx:%llx\n",
					 rq->fence.context, rq->fence.seqno);
				queue_work(rps_to_gt(rps)->i915->unordered_wq,
					   &slpc->boost_work);
			}

			return;
		}

		if (atomic_fetch_inc(&rps->num_waiters))
			return;

		if (!intel_rps_is_active(rps))
			return;

		GT_TRACE(rps_to_gt(rps), "boost fence:%llx:%llx\n",
			 rq->fence.context, rq->fence.seqno);

		if (READ_ONCE(rps->cur_freq) < rps->boost_freq)
			queue_work(rps_to_gt(rps)->i915->unordered_wq, &rps->work);

		WRITE_ONCE(rps->boosts, rps->boosts + 1); /* debug only */
	}
}

int intel_rps_set(struct intel_rps *rps, u8 val)
{
	int err;

	lockdep_assert_held(&rps->lock);
	GEM_BUG_ON(val > rps->max_freq);
	GEM_BUG_ON(val < rps->min_freq);

	if (intel_rps_is_active(rps)) {
		err = rps_set(rps, val, true);
		if (err)
			return err;

		/*
		 * Make sure we continue to get interrupts
		 * until we hit the minimum or maximum frequencies.
		 */
		if (intel_rps_has_interrupts(rps)) {
			struct intel_uncore *uncore = rps_to_uncore(rps);

			set(uncore,
			    GEN6_RP_INTERRUPT_LIMITS, rps_limits(rps, val));

			set(uncore, GEN6_PMINTRMSK, rps_pm_mask(rps, val));
		}
	}

	rps->cur_freq = val;
	return 0;
}

static u32 intel_rps_read_state_cap(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_uncore *uncore = rps_to_uncore(rps);

	if (IS_PONTEVECCHIO(i915))
		return intel_uncore_read(uncore, PVC_RP_STATE_CAP);
	else if (IS_XEHPSDV(i915))
		return intel_uncore_read(uncore, XEHPSDV_RP_STATE_CAP);
	else if (IS_GEN9_LP(i915))
		return intel_uncore_read(uncore, BXT_RP_STATE_CAP);
	else
		return intel_uncore_read(uncore, GEN6_RP_STATE_CAP);
}

static void
mtl_get_freq_caps(struct intel_rps *rps, struct intel_rps_freq_caps *caps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	u32 rp_state_cap = rps_to_gt(rps)->type == GT_MEDIA ?
				intel_uncore_read(uncore, MTL_MEDIAP_STATE_CAP) :
				intel_uncore_read(uncore, MTL_RP_STATE_CAP);
	u32 rpe = rps_to_gt(rps)->type == GT_MEDIA ?
			intel_uncore_read(uncore, MTL_MPE_FREQUENCY) :
			intel_uncore_read(uncore, MTL_GT_RPE_FREQUENCY);

	/* MTL values are in units of 16.67 MHz */
	caps->rp0_freq = REG_FIELD_GET(MTL_RP0_CAP_MASK, rp_state_cap);
	caps->min_freq = REG_FIELD_GET(MTL_RPN_CAP_MASK, rp_state_cap);
	caps->rp1_freq = REG_FIELD_GET(MTL_RPE_MASK, rpe);
}

static void
__gen6_rps_get_freq_caps(struct intel_rps *rps, struct intel_rps_freq_caps *caps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 rp_state_cap;

	rp_state_cap = intel_rps_read_state_cap(rps);

	/* static values from HW: RP0 > RP1 > RPn (min_freq) */
	if (IS_GEN9_LP(i915)) {
		caps->rp0_freq = (rp_state_cap >> 16) & 0xff;
		caps->rp1_freq = (rp_state_cap >>  8) & 0xff;
		caps->min_freq = (rp_state_cap >>  0) & 0xff;
	} else {
		caps->rp0_freq = (rp_state_cap >>  0) & 0xff;
		if (GRAPHICS_VER(i915) >= 10)
			caps->rp1_freq = REG_FIELD_GET(RPE_MASK,
						       intel_uncore_read(to_gt(i915)->uncore,
						       GEN10_FREQ_INFO_REC));
		else
			caps->rp1_freq = (rp_state_cap >>  8) & 0xff;
		caps->min_freq = (rp_state_cap >> 16) & 0xff;
	}

	if (IS_GEN9_BC(i915) || GRAPHICS_VER(i915) >= 11) {
		/*
		 * In this case rp_state_cap register reports frequencies in
		 * units of 50 MHz. Convert these to the actual "hw unit", i.e.
		 * units of 16.67 MHz
		 */
		caps->rp0_freq *= GEN9_FREQ_SCALER;
		caps->rp1_freq *= GEN9_FREQ_SCALER;
		caps->min_freq *= GEN9_FREQ_SCALER;
	}
}

/**
 * gen6_rps_get_freq_caps - Get freq caps exposed by HW
 * @rps: the intel_rps structure
 * @caps: returned freq caps
 *
 * Returned "caps" frequencies should be converted to MHz using
 * intel_gpu_freq()
 */
void gen6_rps_get_freq_caps(struct intel_rps *rps, struct intel_rps_freq_caps *caps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	if (IS_METEORLAKE(i915))
		return mtl_get_freq_caps(rps, caps);
	else
		return __gen6_rps_get_freq_caps(rps, caps);
}

static void gen6_rps_init(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_rps_freq_caps caps;

	gen6_rps_get_freq_caps(rps, &caps);
	rps->rp0_freq = caps.rp0_freq;
	rps->rp1_freq = caps.rp1_freq;
	rps->min_freq = caps.min_freq;

	/* hw_max = RP0 until we check for overclocking */
	rps->max_freq = rps->rp0_freq;

	rps->efficient_freq = rps->rp1_freq;
	if (IS_HASWELL(i915) || IS_BROADWELL(i915) ||
	    IS_GEN9_BC(i915) || GRAPHICS_VER(i915) >= 11) {
		u32 ddcc_status = 0;
		u32 mult = 1;

		if (IS_GEN9_BC(i915) || GRAPHICS_VER(i915) >= 11)
			mult = GEN9_FREQ_SCALER;
		if (snb_pcode_read(rps_to_gt(rps)->uncore,
				   HSW_PCODE_DYNAMIC_DUTY_CYCLE_CONTROL,
				   &ddcc_status, NULL) == 0)
			rps->efficient_freq =
				clamp_t(u32,
					((ddcc_status >> 8) & 0xff) * mult,
					rps->min_freq,
					rps->max_freq);
	}
}

static bool rps_reset(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	/* force a reset */
	rps->power.mode = -1;
	rps->last_freq = -1;

	if (rps_set(rps, rps->min_freq, true)) {
		drm_err(&i915->drm, "Failed to reset RPS to initial values\n");
		return false;
	}

	rps->cur_freq = rps->min_freq;
	return true;
}

/* See the Gen9_GT_PM_Programming_Guide doc for the below */
static bool gen9_rps_enable(struct intel_rps *rps)
{
	struct intel_gt *gt = rps_to_gt(rps);
	struct intel_uncore *uncore = gt->uncore;

	/* Program defaults and thresholds for RPS */
	if (GRAPHICS_VER(gt->i915) == 9)
		intel_uncore_write_fw(uncore, GEN6_RC_VIDEO_FREQ,
				      GEN9_FREQUENCY(rps->rp1_freq));

	intel_uncore_write_fw(uncore, GEN6_RP_IDLE_HYSTERSIS, 0xa);

	rps->pm_events = GEN6_PM_RP_UP_THRESHOLD | GEN6_PM_RP_DOWN_THRESHOLD;

	return rps_reset(rps);
}

static bool gen8_rps_enable(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);

	intel_uncore_write_fw(uncore, GEN6_RC_VIDEO_FREQ,
			      HSW_FREQUENCY(rps->rp1_freq));

	intel_uncore_write_fw(uncore, GEN6_RP_IDLE_HYSTERSIS, 10);

	rps->pm_events = GEN6_PM_RP_UP_THRESHOLD | GEN6_PM_RP_DOWN_THRESHOLD;

	return rps_reset(rps);
}

static bool gen6_rps_enable(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);

	/* Power down if completely idle for over 50ms */
	intel_uncore_write_fw(uncore, GEN6_RP_DOWN_TIMEOUT, 50000);
	intel_uncore_write_fw(uncore, GEN6_RP_IDLE_HYSTERSIS, 10);

	rps->pm_events = (GEN6_PM_RP_UP_THRESHOLD |
			  GEN6_PM_RP_DOWN_THRESHOLD |
			  GEN6_PM_RP_DOWN_TIMEOUT);

	return rps_reset(rps);
}

static int chv_rps_max_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_gt *gt = rps_to_gt(rps);
	u32 val;

	val = vlv_punit_read(i915, FB_GFX_FMAX_AT_VMAX_FUSE);

	switch (gt->info.sseu.eu_total) {
	case 8:
		/* (2 * 4) config */
		val >>= FB_GFX_FMAX_AT_VMAX_2SS4EU_FUSE_SHIFT;
		break;
	case 12:
		/* (2 * 6) config */
		val >>= FB_GFX_FMAX_AT_VMAX_2SS6EU_FUSE_SHIFT;
		break;
	case 16:
		/* (2 * 8) config */
	default:
		/* Setting (2 * 8) Min RP0 for any other combination */
		val >>= FB_GFX_FMAX_AT_VMAX_2SS8EU_FUSE_SHIFT;
		break;
	}

	return val & FB_GFX_FREQ_FUSE_MASK;
}

static int chv_rps_rpe_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val;

	val = vlv_punit_read(i915, PUNIT_GPU_DUTYCYCLE_REG);
	val >>= PUNIT_GPU_DUTYCYCLE_RPE_FREQ_SHIFT;

	return val & PUNIT_GPU_DUTYCYCLE_RPE_FREQ_MASK;
}

static int chv_rps_guar_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val;

	val = vlv_punit_read(i915, FB_GFX_FMAX_AT_VMAX_FUSE);

	return val & FB_GFX_FREQ_FUSE_MASK;
}

static u32 chv_rps_min_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val;

	val = vlv_punit_read(i915, FB_GFX_FMIN_AT_VMIN_FUSE);
	val >>= FB_GFX_FMIN_AT_VMIN_FUSE_SHIFT;

	return val & FB_GFX_FREQ_FUSE_MASK;
}

static bool chv_rps_enable(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val;

	/* 1: Program defaults and thresholds for RPS*/
	intel_uncore_write_fw(uncore, GEN6_RP_DOWN_TIMEOUT, 1000000);
	intel_uncore_write_fw(uncore, GEN6_RP_UP_THRESHOLD, 59400);
	intel_uncore_write_fw(uncore, GEN6_RP_DOWN_THRESHOLD, 245000);
	intel_uncore_write_fw(uncore, GEN6_RP_UP_EI, 66000);
	intel_uncore_write_fw(uncore, GEN6_RP_DOWN_EI, 350000);

	intel_uncore_write_fw(uncore, GEN6_RP_IDLE_HYSTERSIS, 10);

	/* 2: Enable RPS */
	intel_uncore_write_fw(uncore, GEN6_RP_CONTROL,
			      GEN6_RP_MEDIA_HW_NORMAL_MODE |
			      GEN6_RP_MEDIA_IS_GFX |
			      GEN6_RP_ENABLE |
			      GEN6_RP_UP_BUSY_AVG |
			      GEN6_RP_DOWN_IDLE_AVG);

	rps->pm_events = (GEN6_PM_RP_UP_THRESHOLD |
			  GEN6_PM_RP_DOWN_THRESHOLD |
			  GEN6_PM_RP_DOWN_TIMEOUT);

	/* Setting Fixed Bias */
	vlv_punit_get(i915);

	val = VLV_OVERRIDE_EN | VLV_SOC_TDP_EN | CHV_BIAS_CPU_50_SOC_50;
	vlv_punit_write(i915, VLV_TURBO_SOC_OVERRIDE, val);

	val = vlv_punit_read(i915, PUNIT_REG_GPU_FREQ_STS);

	vlv_punit_put(i915);

	/* RPS code assumes GPLL is used */
	drm_WARN_ONCE(&i915->drm, (val & GPLLENABLE) == 0,
		      "GPLL not enabled\n");

	drm_dbg(&i915->drm, "GPLL enabled? %s\n",
		str_yes_no(val & GPLLENABLE));
	drm_dbg(&i915->drm, "GPU status: 0x%08x\n", val);

	return rps_reset(rps);
}

static int vlv_rps_guar_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val, rp1;

	val = vlv_nc_read(i915, IOSF_NC_FB_GFX_FREQ_FUSE);

	rp1 = val & FB_GFX_FGUARANTEED_FREQ_FUSE_MASK;
	rp1 >>= FB_GFX_FGUARANTEED_FREQ_FUSE_SHIFT;

	return rp1;
}

static int vlv_rps_max_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val, rp0;

	val = vlv_nc_read(i915, IOSF_NC_FB_GFX_FREQ_FUSE);

	rp0 = (val & FB_GFX_MAX_FREQ_FUSE_MASK) >> FB_GFX_MAX_FREQ_FUSE_SHIFT;
	/* Clamp to max */
	rp0 = min_t(u32, rp0, 0xea);

	return rp0;
}

static int vlv_rps_rpe_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val, rpe;

	val = vlv_nc_read(i915, IOSF_NC_FB_GFX_FMAX_FUSE_LO);
	rpe = (val & FB_FMAX_VMIN_FREQ_LO_MASK) >> FB_FMAX_VMIN_FREQ_LO_SHIFT;
	val = vlv_nc_read(i915, IOSF_NC_FB_GFX_FMAX_FUSE_HI);
	rpe |= (val & FB_FMAX_VMIN_FREQ_HI_MASK) << 5;

	return rpe;
}

static int vlv_rps_min_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val;

	val = vlv_punit_read(i915, PUNIT_REG_GPU_LFM) & 0xff;
	/*
	 * According to the BYT Punit GPU turbo HAS 1.1.6.3 the minimum value
	 * for the minimum frequency in GPLL mode is 0xc1. Contrary to this on
	 * a BYT-M B0 the above register contains 0xbf. Moreover when setting
	 * a frequency Punit will not allow values below 0xc0. Clamp it 0xc0
	 * to make sure it matches what Punit accepts.
	 */
	return max_t(u32, val, 0xc0);
}

static bool vlv_rps_enable(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val;

	intel_uncore_write_fw(uncore, GEN6_RP_DOWN_TIMEOUT, 1000000);
	intel_uncore_write_fw(uncore, GEN6_RP_UP_THRESHOLD, 59400);
	intel_uncore_write_fw(uncore, GEN6_RP_DOWN_THRESHOLD, 245000);
	intel_uncore_write_fw(uncore, GEN6_RP_UP_EI, 66000);
	intel_uncore_write_fw(uncore, GEN6_RP_DOWN_EI, 350000);

	intel_uncore_write_fw(uncore, GEN6_RP_IDLE_HYSTERSIS, 10);

	intel_uncore_write_fw(uncore, GEN6_RP_CONTROL,
			      GEN6_RP_MEDIA_TURBO |
			      GEN6_RP_MEDIA_HW_NORMAL_MODE |
			      GEN6_RP_MEDIA_IS_GFX |
			      GEN6_RP_ENABLE |
			      GEN6_RP_UP_BUSY_AVG |
			      GEN6_RP_DOWN_IDLE_CONT);

	/* WaGsvRC0ResidencyMethod:vlv */
	rps->pm_events = GEN6_PM_RP_UP_EI_EXPIRED;

	vlv_punit_get(i915);

	/* Setting Fixed Bias */
	val = VLV_OVERRIDE_EN | VLV_SOC_TDP_EN | VLV_BIAS_CPU_125_SOC_875;
	vlv_punit_write(i915, VLV_TURBO_SOC_OVERRIDE, val);

	val = vlv_punit_read(i915, PUNIT_REG_GPU_FREQ_STS);

	vlv_punit_put(i915);

	/* RPS code assumes GPLL is used */
	drm_WARN_ONCE(&i915->drm, (val & GPLLENABLE) == 0,
		      "GPLL not enabled\n");

	drm_dbg(&i915->drm, "GPLL enabled? %s\n",
		str_yes_no(val & GPLLENABLE));
	drm_dbg(&i915->drm, "GPU status: 0x%08x\n", val);

	return rps_reset(rps);
}

static unsigned long __ips_gfx_val(struct intel_ips *ips)
{
	struct intel_rps *rps = container_of(ips, typeof(*rps), ips);
	struct intel_uncore *uncore = rps_to_uncore(rps);
	unsigned int t, state1, state2;
	u32 pxvid, ext_v;
	u64 corr, corr2;

	lockdep_assert_held(&mchdev_lock);

	pxvid = intel_uncore_read(uncore, PXVFREQ(rps->cur_freq));
	pxvid = (pxvid >> 24) & 0x7f;
	ext_v = pvid_to_extvid(rps_to_i915(rps), pxvid);

	state1 = ext_v;

	/* Revel in the empirically derived constants */

	/* Correction factor in 1/100000 units */
	t = ips_mch_val(uncore);
	if (t > 80)
		corr = t * 2349 + 135940;
	else if (t >= 50)
		corr = t * 964 + 29317;
	else /* < 50 */
		corr = t * 301 + 1004;

	corr = div_u64(corr * 150142 * state1, 10000) - 78642;
	corr2 = div_u64(corr, 100000) * ips->corr;

	state2 = div_u64(corr2 * state1, 10000);
	state2 /= 100; /* convert to mW */

	__gen5_ips_update(ips);

	return ips->gfx_power + state2;
}

static bool has_busy_stats(struct intel_rps *rps)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, rps_to_gt(rps), id) {
		if (!intel_engine_supports_stats(engine))
			return false;
	}

	return true;
}

void intel_rps_enable(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_uncore *uncore = rps_to_uncore(rps);
	bool enabled = false;

	if (!HAS_RPS(i915))
		return;

	if (rps_uses_slpc(rps))
		return;

	intel_gt_check_clock_frequency(rps_to_gt(rps));

	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);
	if (rps->max_freq <= rps->min_freq)
		/* leave disabled, no room for dynamic reclocking */;
	else if (IS_CHERRYVIEW(i915))
		enabled = chv_rps_enable(rps);
	else if (IS_VALLEYVIEW(i915))
		enabled = vlv_rps_enable(rps);
	else if (GRAPHICS_VER(i915) >= 9)
		enabled = gen9_rps_enable(rps);
	else if (GRAPHICS_VER(i915) >= 8)
		enabled = gen8_rps_enable(rps);
	else if (GRAPHICS_VER(i915) >= 6)
		enabled = gen6_rps_enable(rps);
	else if (IS_IRONLAKE_M(i915))
		enabled = gen5_rps_enable(rps);
	else
		MISSING_CASE(GRAPHICS_VER(i915));
	intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);
	if (!enabled)
		return;

	GT_TRACE(rps_to_gt(rps),
		 "min:%x, max:%x, freq:[%d, %d], thresholds:[%u, %u]\n",
		 rps->min_freq, rps->max_freq,
		 intel_gpu_freq(rps, rps->min_freq),
		 intel_gpu_freq(rps, rps->max_freq),
		 rps->power.up_threshold,
		 rps->power.down_threshold);

	GEM_BUG_ON(rps->max_freq < rps->min_freq);
	GEM_BUG_ON(rps->idle_freq > rps->max_freq);

	GEM_BUG_ON(rps->efficient_freq < rps->min_freq);
	GEM_BUG_ON(rps->efficient_freq > rps->max_freq);

	if (has_busy_stats(rps))
		intel_rps_set_timer(rps);
	else if (GRAPHICS_VER(i915) >= 6 && GRAPHICS_VER(i915) <= 11)
		intel_rps_set_interrupts(rps);
	else
		/* Ironlake currently uses intel_ips.ko */ {}

	intel_rps_set_enabled(rps);
}

static void gen6_rps_disable(struct intel_rps *rps)
{
	set(rps_to_uncore(rps), GEN6_RP_CONTROL, 0);
}

void intel_rps_disable(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	if (!intel_rps_is_enabled(rps))
		return;

	intel_rps_clear_enabled(rps);
	intel_rps_clear_interrupts(rps);
	intel_rps_clear_timer(rps);

	if (GRAPHICS_VER(i915) >= 6)
		gen6_rps_disable(rps);
	else if (IS_IRONLAKE_M(i915))
		gen5_rps_disable(rps);
}

static int byt_gpu_freq(struct intel_rps *rps, int val)
{
	/*
	 * N = val - 0xb7
	 * Slow = Fast = GPLL ref * N
	 */
	return DIV_ROUND_CLOSEST(rps->gpll_ref_freq * (val - 0xb7), 1000);
}

static int byt_freq_opcode(struct intel_rps *rps, int val)
{
	return DIV_ROUND_CLOSEST(1000 * val, rps->gpll_ref_freq) + 0xb7;
}

static int chv_gpu_freq(struct intel_rps *rps, int val)
{
	/*
	 * N = val / 2
	 * CU (slow) = CU2x (fast) / 2 = GPLL ref * N / 2
	 */
	return DIV_ROUND_CLOSEST(rps->gpll_ref_freq * val, 2 * 2 * 1000);
}

static int chv_freq_opcode(struct intel_rps *rps, int val)
{
	/* CHV needs even values */
	return DIV_ROUND_CLOSEST(2 * 1000 * val, rps->gpll_ref_freq) * 2;
}

int intel_gpu_freq(struct intel_rps *rps, int val)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	if (GRAPHICS_VER(i915) >= 9)
		return DIV_ROUND_CLOSEST(val * GT_FREQUENCY_MULTIPLIER,
					 GEN9_FREQ_SCALER);
	else if (IS_CHERRYVIEW(i915))
		return chv_gpu_freq(rps, val);
	else if (IS_VALLEYVIEW(i915))
		return byt_gpu_freq(rps, val);
	else if (GRAPHICS_VER(i915) >= 6)
		return val * GT_FREQUENCY_MULTIPLIER;
	else
		return val;
}

int intel_freq_opcode(struct intel_rps *rps, int val)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	if (GRAPHICS_VER(i915) >= 9)
		return DIV_ROUND_CLOSEST(val * GEN9_FREQ_SCALER,
					 GT_FREQUENCY_MULTIPLIER);
	else if (IS_CHERRYVIEW(i915))
		return chv_freq_opcode(rps, val);
	else if (IS_VALLEYVIEW(i915))
		return byt_freq_opcode(rps, val);
	else if (GRAPHICS_VER(i915) >= 6)
		return DIV_ROUND_CLOSEST(val, GT_FREQUENCY_MULTIPLIER);
	else
		return val;
}

static void vlv_init_gpll_ref_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	rps->gpll_ref_freq =
		vlv_get_cck_clock(i915, "GPLL ref",
				  CCK_GPLL_CLOCK_CONTROL,
				  i915->czclk_freq);

	drm_dbg(&i915->drm, "GPLL reference freq: %d kHz\n",
		rps->gpll_ref_freq);
}

static void vlv_rps_init(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	vlv_iosf_sb_get(i915,
			BIT(VLV_IOSF_SB_PUNIT) |
			BIT(VLV_IOSF_SB_NC) |
			BIT(VLV_IOSF_SB_CCK));

	vlv_init_gpll_ref_freq(rps);

	rps->max_freq = vlv_rps_max_freq(rps);
	rps->rp0_freq = rps->max_freq;
	drm_dbg(&i915->drm, "max GPU freq: %d MHz (%u)\n",
		intel_gpu_freq(rps, rps->max_freq), rps->max_freq);

	rps->efficient_freq = vlv_rps_rpe_freq(rps);
	drm_dbg(&i915->drm, "RPe GPU freq: %d MHz (%u)\n",
		intel_gpu_freq(rps, rps->efficient_freq), rps->efficient_freq);

	rps->rp1_freq = vlv_rps_guar_freq(rps);
	drm_dbg(&i915->drm, "RP1(Guar Freq) GPU freq: %d MHz (%u)\n",
		intel_gpu_freq(rps, rps->rp1_freq), rps->rp1_freq);

	rps->min_freq = vlv_rps_min_freq(rps);
	drm_dbg(&i915->drm, "min GPU freq: %d MHz (%u)\n",
		intel_gpu_freq(rps, rps->min_freq), rps->min_freq);

	vlv_iosf_sb_put(i915,
			BIT(VLV_IOSF_SB_PUNIT) |
			BIT(VLV_IOSF_SB_NC) |
			BIT(VLV_IOSF_SB_CCK));
}

static void chv_rps_init(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	vlv_iosf_sb_get(i915,
			BIT(VLV_IOSF_SB_PUNIT) |
			BIT(VLV_IOSF_SB_NC) |
			BIT(VLV_IOSF_SB_CCK));

	vlv_init_gpll_ref_freq(rps);

	rps->max_freq = chv_rps_max_freq(rps);
	rps->rp0_freq = rps->max_freq;
	drm_dbg(&i915->drm, "max GPU freq: %d MHz (%u)\n",
		intel_gpu_freq(rps, rps->max_freq), rps->max_freq);

	rps->efficient_freq = chv_rps_rpe_freq(rps);
	drm_dbg(&i915->drm, "RPe GPU freq: %d MHz (%u)\n",
		intel_gpu_freq(rps, rps->efficient_freq), rps->efficient_freq);

	rps->rp1_freq = chv_rps_guar_freq(rps);
	drm_dbg(&i915->drm, "RP1(Guar) GPU freq: %d MHz (%u)\n",
		intel_gpu_freq(rps, rps->rp1_freq), rps->rp1_freq);

	rps->min_freq = chv_rps_min_freq(rps);
	drm_dbg(&i915->drm, "min GPU freq: %d MHz (%u)\n",
		intel_gpu_freq(rps, rps->min_freq), rps->min_freq);

	vlv_iosf_sb_put(i915,
			BIT(VLV_IOSF_SB_PUNIT) |
			BIT(VLV_IOSF_SB_NC) |
			BIT(VLV_IOSF_SB_CCK));

	drm_WARN_ONCE(&i915->drm, (rps->max_freq | rps->efficient_freq |
				   rps->rp1_freq | rps->min_freq) & 1,
		      "Odd GPU freq values\n");
}

static void vlv_c0_read(struct intel_uncore *uncore, struct intel_rps_ei *ei)
{
	ei->ktime = ktime_get_raw();
	ei->render_c0 = intel_uncore_read(uncore, VLV_RENDER_C0_COUNT);
	ei->media_c0 = intel_uncore_read(uncore, VLV_MEDIA_C0_COUNT);
}

static u32 vlv_wa_c0_ei(struct intel_rps *rps, u32 pm_iir)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	const struct intel_rps_ei *prev = &rps->ei;
	struct intel_rps_ei now;
	u32 events = 0;

	if ((pm_iir & GEN6_PM_RP_UP_EI_EXPIRED) == 0)
		return 0;

	vlv_c0_read(uncore, &now);

	if (prev->ktime) {
		u64 time, c0;
		u32 render, media;

		time = ktime_us_delta(now.ktime, prev->ktime);

		time *= rps_to_i915(rps)->czclk_freq;

		/* Workload can be split between render + media,
		 * e.g. SwapBuffers being blitted in X after being rendered in
		 * mesa. To account for this we need to combine both engines
		 * into our activity counter.
		 */
		render = now.render_c0 - prev->render_c0;
		media = now.media_c0 - prev->media_c0;
		c0 = max(render, media);
		c0 *= 1000 * 100 << 8; /* to usecs and scale to threshold% */

		if (c0 > time * rps->power.up_threshold)
			events = GEN6_PM_RP_UP_THRESHOLD;
		else if (c0 < time * rps->power.down_threshold)
			events = GEN6_PM_RP_DOWN_THRESHOLD;
	}

	rps->ei = now;
	return events;
}

static void rps_work(struct work_struct *work)
{
	struct intel_rps *rps = container_of(work, typeof(*rps), work);
	struct intel_gt *gt = rps_to_gt(rps);
	struct drm_i915_private *i915 = rps_to_i915(rps);
	bool client_boost = false;
	int new_freq, adj, min, max;
	u32 pm_iir = 0;

	spin_lock_irq(gt->irq_lock);
	pm_iir = fetch_and_zero(&rps->pm_iir) & rps->pm_events;
	client_boost = atomic_read(&rps->num_waiters);
	spin_unlock_irq(gt->irq_lock);

	/* Make sure we didn't queue anything we're not going to process. */
	if (!pm_iir && !client_boost)
		goto out;

	mutex_lock(&rps->lock);
	if (!intel_rps_is_active(rps)) {
		mutex_unlock(&rps->lock);
		return;
	}

	pm_iir |= vlv_wa_c0_ei(rps, pm_iir);

	adj = rps->last_adj;
	new_freq = rps->cur_freq;
	min = rps->min_freq_softlimit;
	max = rps->max_freq_softlimit;
	if (client_boost)
		max = rps->max_freq;

	GT_TRACE(gt,
		 "pm_iir:%x, client_boost:%s, last:%d, cur:%x, min:%x, max:%x\n",
		 pm_iir, str_yes_no(client_boost),
		 adj, new_freq, min, max);

	if (client_boost && new_freq < rps->boost_freq) {
		new_freq = rps->boost_freq;
		adj = 0;
	} else if (pm_iir & GEN6_PM_RP_UP_THRESHOLD) {
		if (adj > 0)
			adj *= 2;
		else /* CHV needs even encode values */
			adj = IS_CHERRYVIEW(gt->i915) ? 2 : 1;

		if (new_freq >= rps->max_freq_softlimit)
			adj = 0;
	} else if (client_boost) {
		adj = 0;
	} else if (pm_iir & GEN6_PM_RP_DOWN_TIMEOUT) {
		if (rps->cur_freq > rps->efficient_freq)
			new_freq = rps->efficient_freq;
		else if (rps->cur_freq > rps->min_freq_softlimit)
			new_freq = rps->min_freq_softlimit;
		adj = 0;
	} else if (pm_iir & GEN6_PM_RP_DOWN_THRESHOLD) {
		if (adj < 0)
			adj *= 2;
		else /* CHV needs even encode values */
			adj = IS_CHERRYVIEW(gt->i915) ? -2 : -1;

		if (new_freq <= rps->min_freq_softlimit)
			adj = 0;
	} else { /* unknown event */
		adj = 0;
	}

	/*
	 * sysfs frequency limits may have snuck in while
	 * servicing the interrupt
	 */
	new_freq += adj;
	new_freq = clamp_t(int, new_freq, min, max);

	if (intel_rps_set(rps, new_freq)) {
		drm_dbg(&i915->drm, "Failed to set new GPU frequency\n");
		adj = 0;
	}
	rps->last_adj = adj;

	mutex_unlock(&rps->lock);

out:
	spin_lock_irq(gt->irq_lock);
	gen6_gt_pm_unmask_irq(gt, rps->pm_events);
	spin_unlock_irq(gt->irq_lock);
}

void gen11_rps_irq_handler(struct intel_rps *rps, u32 pm_iir)
{
	struct intel_gt *gt = rps_to_gt(rps);
	const u32 events = rps->pm_events & pm_iir;

	lockdep_assert_held(gt->irq_lock);

	if (unlikely(!events))
		return;

	GT_TRACE(gt, "irq events:%x\n", events);

	gen6_gt_pm_mask_irq(gt, events);

	rps->pm_iir |= events;
	queue_work(gt->i915->unordered_wq, &rps->work);
}

void gen6_rps_irq_handler(struct intel_rps *rps, u32 pm_iir)
{
	struct intel_gt *gt = rps_to_gt(rps);
	u32 events;

	events = pm_iir & rps->pm_events;
	if (events) {
		spin_lock(gt->irq_lock);

		GT_TRACE(gt, "irq events:%x\n", events);

		gen6_gt_pm_mask_irq(gt, events);
		rps->pm_iir |= events;

		queue_work(gt->i915->unordered_wq, &rps->work);
		spin_unlock(gt->irq_lock);
	}

	if (GRAPHICS_VER(gt->i915) >= 8)
		return;

	if (pm_iir & PM_VEBOX_USER_INTERRUPT)
		intel_engine_cs_irq(gt->engine[VECS0], pm_iir >> 10);

	if (pm_iir & PM_VEBOX_CS_ERROR_INTERRUPT)
		drm_dbg(&rps_to_i915(rps)->drm,
			"Command parser error, pm_iir 0x%08x\n", pm_iir);
}

void gen5_rps_irq_handler(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	u32 busy_up, busy_down, max_avg, min_avg;
	u8 new_freq;

	spin_lock(&mchdev_lock);

	intel_uncore_write16(uncore,
			     MEMINTRSTS,
			     intel_uncore_read(uncore, MEMINTRSTS));

	intel_uncore_write16(uncore, MEMINTRSTS, MEMINT_EVAL_CHG);
	busy_up = intel_uncore_read(uncore, RCPREVBSYTUPAVG);
	busy_down = intel_uncore_read(uncore, RCPREVBSYTDNAVG);
	max_avg = intel_uncore_read(uncore, RCBMAXAVG);
	min_avg = intel_uncore_read(uncore, RCBMINAVG);

	/* Handle RCS change request from hw */
	new_freq = rps->cur_freq;
	if (busy_up > max_avg)
		new_freq++;
	else if (busy_down < min_avg)
		new_freq--;
	new_freq = clamp(new_freq,
			 rps->min_freq_softlimit,
			 rps->max_freq_softlimit);

	if (new_freq != rps->cur_freq && !__gen5_rps_set(rps, new_freq))
		rps->cur_freq = new_freq;

	spin_unlock(&mchdev_lock);
}

void intel_rps_init_early(struct intel_rps *rps)
{
	mutex_init(&rps->lock);
	mutex_init(&rps->power.mutex);

	INIT_WORK(&rps->work, rps_work);
	timer_setup(&rps->timer, rps_timer, 0);

	atomic_set(&rps->num_waiters, 0);
}

void intel_rps_init(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	if (rps_uses_slpc(rps))
		return;

	if (IS_CHERRYVIEW(i915))
		chv_rps_init(rps);
	else if (IS_VALLEYVIEW(i915))
		vlv_rps_init(rps);
	else if (GRAPHICS_VER(i915) >= 6)
		gen6_rps_init(rps);
	else if (IS_IRONLAKE_M(i915))
		gen5_rps_init(rps);

	/* Derive initial user preferences/limits from the hardware limits */
	rps->max_freq_softlimit = rps->max_freq;
	rps_to_gt(rps)->defaults.max_freq = rps->max_freq_softlimit;
	rps->min_freq_softlimit = rps->min_freq;
	rps_to_gt(rps)->defaults.min_freq = rps->min_freq_softlimit;

	/* After setting max-softlimit, find the overclock max freq */
	if (GRAPHICS_VER(i915) == 6 || IS_IVYBRIDGE(i915) || IS_HASWELL(i915)) {
		u32 params = 0;

		snb_pcode_read(rps_to_gt(rps)->uncore, GEN6_READ_OC_PARAMS, &params, NULL);
		if (params & BIT(31)) { /* OC supported */
			drm_dbg(&i915->drm,
				"Overclocking supported, max: %dMHz, overclock: %dMHz\n",
				(rps->max_freq & 0xff) * 50,
				(params & 0xff) * 50);
			rps->max_freq = params & 0xff;
		}
	}

	/* Set default thresholds in % */
	rps->power.up_threshold = 95;
	rps_to_gt(rps)->defaults.rps_up_threshold = rps->power.up_threshold;
	rps->power.down_threshold = 85;
	rps_to_gt(rps)->defaults.rps_down_threshold = rps->power.down_threshold;

	/* Finally allow us to boost to max by default */
	rps->boost_freq = rps->max_freq;
	rps->idle_freq = rps->min_freq;

	/* Start in the middle, from here we will autotune based on workload */
	rps->cur_freq = rps->efficient_freq;

	rps->pm_intrmsk_mbz = 0;

	/*
	 * SNB,IVB,HSW can while VLV,CHV may hard hang on looping batchbuffer
	 * if GEN6_PM_UP_EI_EXPIRED is masked.
	 *
	 * TODO: verify if this can be reproduced on VLV,CHV.
	 */
	if (GRAPHICS_VER(i915) <= 7)
		rps->pm_intrmsk_mbz |= GEN6_PM_RP_UP_EI_EXPIRED;

	if (GRAPHICS_VER(i915) >= 8 && GRAPHICS_VER(i915) < 11)
		rps->pm_intrmsk_mbz |= GEN8_PMINTR_DISABLE_REDIRECT_TO_GUC;

	/* GuC needs ARAT expired interrupt unmasked */
	if (intel_uc_uses_guc_submission(&rps_to_gt(rps)->uc))
		rps->pm_intrmsk_mbz |= ARAT_EXPIRED_INTRMSK;
}

void intel_rps_sanitize(struct intel_rps *rps)
{
	if (rps_uses_slpc(rps))
		return;

	if (GRAPHICS_VER(rps_to_i915(rps)) >= 6)
		rps_disable_interrupts(rps);
}

u32 intel_rps_read_rpstat(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	i915_reg_t rpstat;

	rpstat = (GRAPHICS_VER(i915) >= 12) ? GEN12_RPSTAT1 : GEN6_RPSTAT1;

	return intel_uncore_read(rps_to_gt(rps)->uncore, rpstat);
}

static u32 intel_rps_get_cagf(struct intel_rps *rps, u32 rpstat)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 cagf;

	if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 70))
		cagf = REG_FIELD_GET(MTL_CAGF_MASK, rpstat);
	else if (GRAPHICS_VER(i915) >= 12)
		cagf = REG_FIELD_GET(GEN12_CAGF_MASK, rpstat);
	else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		cagf = REG_FIELD_GET(RPE_MASK, rpstat);
	else if (GRAPHICS_VER(i915) >= 9)
		cagf = REG_FIELD_GET(GEN9_CAGF_MASK, rpstat);
	else if (IS_HASWELL(i915) || IS_BROADWELL(i915))
		cagf = REG_FIELD_GET(HSW_CAGF_MASK, rpstat);
	else if (GRAPHICS_VER(i915) >= 6)
		cagf = REG_FIELD_GET(GEN6_CAGF_MASK, rpstat);
	else
		cagf = gen5_invert_freq(rps, REG_FIELD_GET(MEMSTAT_PSTATE_MASK, rpstat));

	return cagf;
}

static u32 __read_cagf(struct intel_rps *rps, bool take_fw)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_uncore *uncore = rps_to_uncore(rps);
	i915_reg_t r = INVALID_MMIO_REG;
	u32 freq;

	/*
	 * For Gen12+ reading freq from HW does not need a forcewake and
	 * registers will return 0 freq when GT is in RC6
	 */
	if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 70)) {
		r = MTL_MIRROR_TARGET_WP1;
	} else if (GRAPHICS_VER(i915) >= 12) {
		r = GEN12_RPSTAT1;
	} else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915)) {
		vlv_punit_get(i915);
		freq = vlv_punit_read(i915, PUNIT_REG_GPU_FREQ_STS);
		vlv_punit_put(i915);
	} else if (GRAPHICS_VER(i915) >= 6) {
		r = GEN6_RPSTAT1;
	} else {
		r = MEMSTAT_ILK;
	}

	if (i915_mmio_reg_valid(r))
		freq = take_fw ? intel_uncore_read(uncore, r) : intel_uncore_read_fw(uncore, r);

	return intel_rps_get_cagf(rps, freq);
}

static u32 read_cagf(struct intel_rps *rps)
{
	return __read_cagf(rps, true);
}

u32 intel_rps_read_actual_frequency(struct intel_rps *rps)
{
	struct intel_runtime_pm *rpm = rps_to_uncore(rps)->rpm;
	intel_wakeref_t wakeref;
	u32 freq = 0;

	with_intel_runtime_pm_if_in_use(rpm, wakeref)
		freq = intel_gpu_freq(rps, read_cagf(rps));

	return freq;
}

u32 intel_rps_read_actual_frequency_fw(struct intel_rps *rps)
{
	return intel_gpu_freq(rps, __read_cagf(rps, false));
}

static u32 intel_rps_read_punit_req(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	struct intel_runtime_pm *rpm = rps_to_uncore(rps)->rpm;
	intel_wakeref_t wakeref;
	u32 freq = 0;

	with_intel_runtime_pm_if_in_use(rpm, wakeref)
		freq = intel_uncore_read(uncore, GEN6_RPNSWREQ);

	return freq;
}

static u32 intel_rps_get_req(u32 pureq)
{
	u32 req = pureq >> GEN9_SW_REQ_UNSLICE_RATIO_SHIFT;

	return req;
}

u32 intel_rps_read_punit_req_frequency(struct intel_rps *rps)
{
	u32 freq = intel_rps_get_req(intel_rps_read_punit_req(rps));

	return intel_gpu_freq(rps, freq);
}

u32 intel_rps_get_requested_frequency(struct intel_rps *rps)
{
	if (rps_uses_slpc(rps))
		return intel_rps_read_punit_req_frequency(rps);
	else
		return intel_gpu_freq(rps, rps->cur_freq);
}

u32 intel_rps_get_max_frequency(struct intel_rps *rps)
{
	struct intel_guc_slpc *slpc = rps_to_slpc(rps);

	if (rps_uses_slpc(rps))
		return slpc->max_freq_softlimit;
	else
		return intel_gpu_freq(rps, rps->max_freq_softlimit);
}

/**
 * intel_rps_get_max_raw_freq - returns the max frequency in some raw format.
 * @rps: the intel_rps structure
 *
 * Returns the max frequency in a raw format. In newer platforms raw is in
 * units of 50 MHz.
 */
u32 intel_rps_get_max_raw_freq(struct intel_rps *rps)
{
	struct intel_guc_slpc *slpc = rps_to_slpc(rps);
	u32 freq;

	if (rps_uses_slpc(rps)) {
		return DIV_ROUND_CLOSEST(slpc->rp0_freq,
					 GT_FREQUENCY_MULTIPLIER);
	} else {
		freq = rps->max_freq;
		if (GRAPHICS_VER(rps_to_i915(rps)) >= 9) {
			/* Convert GT frequency to 50 MHz units */
			freq /= GEN9_FREQ_SCALER;
		}
		return freq;
	}
}

u32 intel_rps_get_rp0_frequency(struct intel_rps *rps)
{
	struct intel_guc_slpc *slpc = rps_to_slpc(rps);

	if (rps_uses_slpc(rps))
		return slpc->rp0_freq;
	else
		return intel_gpu_freq(rps, rps->rp0_freq);
}

u32 intel_rps_get_rp1_frequency(struct intel_rps *rps)
{
	struct intel_guc_slpc *slpc = rps_to_slpc(rps);

	if (rps_uses_slpc(rps))
		return slpc->rp1_freq;
	else
		return intel_gpu_freq(rps, rps->rp1_freq);
}

u32 intel_rps_get_rpn_frequency(struct intel_rps *rps)
{
	struct intel_guc_slpc *slpc = rps_to_slpc(rps);

	if (rps_uses_slpc(rps))
		return slpc->min_freq;
	else
		return intel_gpu_freq(rps, rps->min_freq);
}

static void rps_frequency_dump(struct intel_rps *rps, struct drm_printer *p)
{
	struct intel_gt *gt = rps_to_gt(rps);
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	struct intel_rps_freq_caps caps;
	u32 rp_state_limits;
	u32 gt_perf_status;
	u32 rpmodectl, rpinclimit, rpdeclimit;
	u32 rpstat, cagf, reqf;
	u32 rpcurupei, rpcurup, rpprevup;
	u32 rpcurdownei, rpcurdown, rpprevdown;
	u32 rpupei, rpupt, rpdownei, rpdownt;
	u32 pm_ier, pm_imr, pm_isr, pm_iir, pm_mask;

	rp_state_limits = intel_uncore_read(uncore, GEN6_RP_STATE_LIMITS);
	gen6_rps_get_freq_caps(rps, &caps);
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

	rpstat = intel_rps_read_rpstat(rps);
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
		   str_yes_no(rpmodectl & GEN6_RP_MEDIA_TURBO));
	drm_printf(p, "HW control enabled: %s\n",
		   str_yes_no(rpmodectl & GEN6_RP_ENABLE));
	drm_printf(p, "SW control enabled: %s\n",
		   str_yes_no((rpmodectl & GEN6_RP_MEDIA_MODE_MASK) == GEN6_RP_MEDIA_SW_MODE));

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

	drm_printf(p, "Lowest (RPN) frequency: %dMHz\n",
		   intel_gpu_freq(rps, caps.min_freq));
	drm_printf(p, "Nominal (RP1) frequency: %dMHz\n",
		   intel_gpu_freq(rps, caps.rp1_freq));
	drm_printf(p, "Max non-overclocked (RP0) frequency: %dMHz\n",
		   intel_gpu_freq(rps, caps.rp0_freq));
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
}

static void slpc_frequency_dump(struct intel_rps *rps, struct drm_printer *p)
{
	struct intel_gt *gt = rps_to_gt(rps);
	struct intel_uncore *uncore = gt->uncore;
	struct intel_rps_freq_caps caps;
	u32 pm_mask;

	gen6_rps_get_freq_caps(rps, &caps);
	pm_mask = intel_uncore_read(uncore, GEN6_PMINTRMSK);

	drm_printf(p, "PM MASK=0x%08x\n", pm_mask);
	drm_printf(p, "pm_intrmsk_mbz: 0x%08x\n",
		   rps->pm_intrmsk_mbz);
	drm_printf(p, "RPSTAT1: 0x%08x\n", intel_rps_read_rpstat(rps));
	drm_printf(p, "RPNSWREQ: %dMHz\n", intel_rps_get_requested_frequency(rps));
	drm_printf(p, "Lowest (RPN) frequency: %dMHz\n",
		   intel_gpu_freq(rps, caps.min_freq));
	drm_printf(p, "Nominal (RP1) frequency: %dMHz\n",
		   intel_gpu_freq(rps, caps.rp1_freq));
	drm_printf(p, "Max non-overclocked (RP0) frequency: %dMHz\n",
		   intel_gpu_freq(rps, caps.rp0_freq));
	drm_printf(p, "Current freq: %d MHz\n",
		   intel_rps_get_requested_frequency(rps));
	drm_printf(p, "Actual freq: %d MHz\n",
		   intel_rps_read_actual_frequency(rps));
	drm_printf(p, "Min freq: %d MHz\n",
		   intel_rps_get_min_frequency(rps));
	drm_printf(p, "Boost freq: %d MHz\n",
		   intel_rps_get_boost_frequency(rps));
	drm_printf(p, "Max freq: %d MHz\n",
		   intel_rps_get_max_frequency(rps));
	drm_printf(p,
		   "efficient (RPe) frequency: %d MHz\n",
		   intel_gpu_freq(rps, caps.rp1_freq));
}

void gen6_rps_frequency_dump(struct intel_rps *rps, struct drm_printer *p)
{
	if (rps_uses_slpc(rps))
		return slpc_frequency_dump(rps, p);
	else
		return rps_frequency_dump(rps, p);
}

static int set_max_freq(struct intel_rps *rps, u32 val)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	int ret = 0;

	mutex_lock(&rps->lock);

	val = intel_freq_opcode(rps, val);
	if (val < rps->min_freq ||
	    val > rps->max_freq ||
	    val < rps->min_freq_softlimit) {
		ret = -EINVAL;
		goto unlock;
	}

	if (val > rps->rp0_freq)
		drm_dbg(&i915->drm, "User requested overclocking to %d\n",
			intel_gpu_freq(rps, val));

	rps->max_freq_softlimit = val;

	val = clamp_t(int, rps->cur_freq,
		      rps->min_freq_softlimit,
		      rps->max_freq_softlimit);

	/*
	 * We still need *_set_rps to process the new max_delay and
	 * update the interrupt limits and PMINTRMSK even though
	 * frequency request may be unchanged.
	 */
	intel_rps_set(rps, val);

unlock:
	mutex_unlock(&rps->lock);

	return ret;
}

int intel_rps_set_max_frequency(struct intel_rps *rps, u32 val)
{
	struct intel_guc_slpc *slpc = rps_to_slpc(rps);

	if (rps_uses_slpc(rps))
		return intel_guc_slpc_set_max_freq(slpc, val);
	else
		return set_max_freq(rps, val);
}

u32 intel_rps_get_min_frequency(struct intel_rps *rps)
{
	struct intel_guc_slpc *slpc = rps_to_slpc(rps);

	if (rps_uses_slpc(rps))
		return slpc->min_freq_softlimit;
	else
		return intel_gpu_freq(rps, rps->min_freq_softlimit);
}

/**
 * intel_rps_get_min_raw_freq - returns the min frequency in some raw format.
 * @rps: the intel_rps structure
 *
 * Returns the min frequency in a raw format. In newer platforms raw is in
 * units of 50 MHz.
 */
u32 intel_rps_get_min_raw_freq(struct intel_rps *rps)
{
	struct intel_guc_slpc *slpc = rps_to_slpc(rps);
	u32 freq;

	if (rps_uses_slpc(rps)) {
		return DIV_ROUND_CLOSEST(slpc->min_freq,
					 GT_FREQUENCY_MULTIPLIER);
	} else {
		freq = rps->min_freq;
		if (GRAPHICS_VER(rps_to_i915(rps)) >= 9) {
			/* Convert GT frequency to 50 MHz units */
			freq /= GEN9_FREQ_SCALER;
		}
		return freq;
	}
}

static int set_min_freq(struct intel_rps *rps, u32 val)
{
	int ret = 0;

	mutex_lock(&rps->lock);

	val = intel_freq_opcode(rps, val);
	if (val < rps->min_freq ||
	    val > rps->max_freq ||
	    val > rps->max_freq_softlimit) {
		ret = -EINVAL;
		goto unlock;
	}

	rps->min_freq_softlimit = val;

	val = clamp_t(int, rps->cur_freq,
		      rps->min_freq_softlimit,
		      rps->max_freq_softlimit);

	/*
	 * We still need *_set_rps to process the new min_delay and
	 * update the interrupt limits and PMINTRMSK even though
	 * frequency request may be unchanged.
	 */
	intel_rps_set(rps, val);

unlock:
	mutex_unlock(&rps->lock);

	return ret;
}

int intel_rps_set_min_frequency(struct intel_rps *rps, u32 val)
{
	struct intel_guc_slpc *slpc = rps_to_slpc(rps);

	if (rps_uses_slpc(rps))
		return intel_guc_slpc_set_min_freq(slpc, val);
	else
		return set_min_freq(rps, val);
}

u8 intel_rps_get_up_threshold(struct intel_rps *rps)
{
	return rps->power.up_threshold;
}

static int rps_set_threshold(struct intel_rps *rps, u8 *threshold, u8 val)
{
	int ret;

	if (val > 100)
		return -EINVAL;

	ret = mutex_lock_interruptible(&rps->lock);
	if (ret)
		return ret;

	if (*threshold == val)
		goto out_unlock;

	*threshold = val;

	/* Force reset. */
	rps->last_freq = -1;
	mutex_lock(&rps->power.mutex);
	rps->power.mode = -1;
	mutex_unlock(&rps->power.mutex);

	intel_rps_set(rps, clamp(rps->cur_freq,
				 rps->min_freq_softlimit,
				 rps->max_freq_softlimit));

out_unlock:
	mutex_unlock(&rps->lock);

	return ret;
}

int intel_rps_set_up_threshold(struct intel_rps *rps, u8 threshold)
{
	return rps_set_threshold(rps, &rps->power.up_threshold, threshold);
}

u8 intel_rps_get_down_threshold(struct intel_rps *rps)
{
	return rps->power.down_threshold;
}

int intel_rps_set_down_threshold(struct intel_rps *rps, u8 threshold)
{
	return rps_set_threshold(rps, &rps->power.down_threshold, threshold);
}

static void intel_rps_set_manual(struct intel_rps *rps, bool enable)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	u32 state = enable ? GEN9_RPSWCTL_ENABLE : GEN9_RPSWCTL_DISABLE;

	/* Allow punit to process software requests */
	intel_uncore_write(uncore, GEN6_RP_CONTROL, state);
}

void intel_rps_raise_unslice(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);

	mutex_lock(&rps->lock);

	if (rps_uses_slpc(rps)) {
		/* RP limits have not been initialized yet for SLPC path */
		struct intel_rps_freq_caps caps;

		gen6_rps_get_freq_caps(rps, &caps);

		intel_rps_set_manual(rps, true);
		intel_uncore_write(uncore, GEN6_RPNSWREQ,
				   ((caps.rp0_freq <<
				   GEN9_SW_REQ_UNSLICE_RATIO_SHIFT) |
				   GEN9_IGNORE_SLICE_RATIO));
		intel_rps_set_manual(rps, false);
	} else {
		intel_rps_set(rps, rps->rp0_freq);
	}

	mutex_unlock(&rps->lock);
}

void intel_rps_lower_unslice(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);

	mutex_lock(&rps->lock);

	if (rps_uses_slpc(rps)) {
		/* RP limits have not been initialized yet for SLPC path */
		struct intel_rps_freq_caps caps;

		gen6_rps_get_freq_caps(rps, &caps);

		intel_rps_set_manual(rps, true);
		intel_uncore_write(uncore, GEN6_RPNSWREQ,
				   ((caps.min_freq <<
				   GEN9_SW_REQ_UNSLICE_RATIO_SHIFT) |
				   GEN9_IGNORE_SLICE_RATIO));
		intel_rps_set_manual(rps, false);
	} else {
		intel_rps_set(rps, rps->min_freq);
	}

	mutex_unlock(&rps->lock);
}

static u32 rps_read_mmio(struct intel_rps *rps, i915_reg_t reg32)
{
	struct intel_gt *gt = rps_to_gt(rps);
	intel_wakeref_t wakeref;
	u32 val;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref)
		val = intel_uncore_read(gt->uncore, reg32);

	return val;
}

bool rps_read_mask_mmio(struct intel_rps *rps,
			i915_reg_t reg32, u32 mask)
{
	return rps_read_mmio(rps, reg32) & mask;
}

/* External interface for intel_ips.ko */

static struct drm_i915_private __rcu *ips_mchdev;

/*
 * Tells the intel_ips driver that the i915 driver is now loaded, if
 * IPS got loaded first.
 *
 * This awkward dance is so that neither module has to depend on the
 * other in order for IPS to do the appropriate communication of
 * GPU turbo limits to i915.
 */
static void
ips_ping_for_i915_load(void)
{
	void (*link)(void);

	link = symbol_get(ips_link_to_i915_driver);
	if (link) {
		link();
		symbol_put(ips_link_to_i915_driver);
	}
}

void intel_rps_driver_register(struct intel_rps *rps)
{
	struct intel_gt *gt = rps_to_gt(rps);

	/*
	 * We only register the i915 ips part with intel-ips once everything is
	 * set up, to avoid intel-ips sneaking in and reading bogus values.
	 */
	if (GRAPHICS_VER(gt->i915) == 5) {
		GEM_BUG_ON(ips_mchdev);
		rcu_assign_pointer(ips_mchdev, gt->i915);
		ips_ping_for_i915_load();
	}
}

void intel_rps_driver_unregister(struct intel_rps *rps)
{
	if (rcu_access_pointer(ips_mchdev) == rps_to_i915(rps))
		rcu_assign_pointer(ips_mchdev, NULL);
}

static struct drm_i915_private *mchdev_get(void)
{
	struct drm_i915_private *i915;

	rcu_read_lock();
	i915 = rcu_dereference(ips_mchdev);
	if (i915 && !kref_get_unless_zero(&i915->drm.ref))
		i915 = NULL;
	rcu_read_unlock();

	return i915;
}

/**
 * i915_read_mch_val - return value for IPS use
 *
 * Calculate and return a value for the IPS driver to use when deciding whether
 * we have thermal and power headroom to increase CPU or GPU power budget.
 */
unsigned long i915_read_mch_val(void)
{
	struct drm_i915_private *i915;
	unsigned long chipset_val = 0;
	unsigned long graphics_val = 0;
	intel_wakeref_t wakeref;

	i915 = mchdev_get();
	if (!i915)
		return 0;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		struct intel_ips *ips = &to_gt(i915)->rps.ips;

		spin_lock_irq(&mchdev_lock);
		chipset_val = __ips_chipset_val(ips);
		graphics_val = __ips_gfx_val(ips);
		spin_unlock_irq(&mchdev_lock);
	}

	drm_dev_put(&i915->drm);
	return chipset_val + graphics_val;
}
EXPORT_SYMBOL_GPL(i915_read_mch_val);

/**
 * i915_gpu_raise - raise GPU frequency limit
 *
 * Raise the limit; IPS indicates we have thermal headroom.
 */
bool i915_gpu_raise(void)
{
	struct drm_i915_private *i915;
	struct intel_rps *rps;

	i915 = mchdev_get();
	if (!i915)
		return false;

	rps = &to_gt(i915)->rps;

	spin_lock_irq(&mchdev_lock);
	if (rps->max_freq_softlimit < rps->max_freq)
		rps->max_freq_softlimit++;
	spin_unlock_irq(&mchdev_lock);

	drm_dev_put(&i915->drm);
	return true;
}
EXPORT_SYMBOL_GPL(i915_gpu_raise);

/**
 * i915_gpu_lower - lower GPU frequency limit
 *
 * IPS indicates we're close to a thermal limit, so throttle back the GPU
 * frequency maximum.
 */
bool i915_gpu_lower(void)
{
	struct drm_i915_private *i915;
	struct intel_rps *rps;

	i915 = mchdev_get();
	if (!i915)
		return false;

	rps = &to_gt(i915)->rps;

	spin_lock_irq(&mchdev_lock);
	if (rps->max_freq_softlimit > rps->min_freq)
		rps->max_freq_softlimit--;
	spin_unlock_irq(&mchdev_lock);

	drm_dev_put(&i915->drm);
	return true;
}
EXPORT_SYMBOL_GPL(i915_gpu_lower);

/**
 * i915_gpu_busy - indicate GPU business to IPS
 *
 * Tell the IPS driver whether or not the GPU is busy.
 */
bool i915_gpu_busy(void)
{
	struct drm_i915_private *i915;
	bool ret;

	i915 = mchdev_get();
	if (!i915)
		return false;

	ret = to_gt(i915)->awake;

	drm_dev_put(&i915->drm);
	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_busy);

/**
 * i915_gpu_turbo_disable - disable graphics turbo
 *
 * Disable graphics turbo by resetting the max frequency and setting the
 * current frequency to the default.
 */
bool i915_gpu_turbo_disable(void)
{
	struct drm_i915_private *i915;
	struct intel_rps *rps;
	bool ret;

	i915 = mchdev_get();
	if (!i915)
		return false;

	rps = &to_gt(i915)->rps;

	spin_lock_irq(&mchdev_lock);
	rps->max_freq_softlimit = rps->min_freq;
	ret = !__gen5_rps_set(&to_gt(i915)->rps, rps->min_freq);
	spin_unlock_irq(&mchdev_lock);

	drm_dev_put(&i915->drm);
	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_turbo_disable);

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_rps.c"
#include "selftest_slpc.c"
#endif
