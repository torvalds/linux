/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <drm/i915_drm.h>

#include "i915_drv.h"
#include "intel_gt.h"
#include "intel_gt_irq.h"
#include "intel_gt_pm_irq.h"
#include "intel_rps.h"
#include "intel_sideband.h"
#include "../../../platform/x86/intel_ips.h"

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

static u32 rps_pm_sanitize_mask(struct intel_rps *rps, u32 mask)
{
	return mask & ~rps->pm_intrmsk_mbz;
}

static inline void set(struct intel_uncore *uncore, i915_reg_t reg, u32 val)
{
	intel_uncore_write_fw(uncore, reg, val);
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

	rps_reset_ei(rps);

	if (IS_VALLEYVIEW(gt->i915))
		/* WaGsvRC0ResidencyMethod:vlv */
		rps->pm_events = GEN6_PM_RP_UP_EI_EXPIRED;
	else
		rps->pm_events = (GEN6_PM_RP_UP_THRESHOLD |
				  GEN6_PM_RP_DOWN_THRESHOLD |
				  GEN6_PM_RP_DOWN_TIMEOUT);

	spin_lock_irq(&gt->irq_lock);
	gen6_gt_pm_enable_irq(gt, rps->pm_events);
	spin_unlock_irq(&gt->irq_lock);

	set(gt->uncore, GEN6_PMINTRMSK, rps_pm_mask(rps, rps->cur_freq));
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

	spin_lock_irq(&gt->irq_lock);
	if (INTEL_GEN(gt->i915) >= 11)
		gen11_rps_reset_interrupts(rps);
	else
		gen6_rps_reset_interrupts(rps);

	rps->pm_iir = 0;
	spin_unlock_irq(&gt->irq_lock);
}

static void rps_disable_interrupts(struct intel_rps *rps)
{
	struct intel_gt *gt = rps_to_gt(rps);

	rps->pm_events = 0;

	set(gt->uncore, GEN6_PMINTRMSK, rps_pm_sanitize_mask(rps, ~0u));

	spin_lock_irq(&gt->irq_lock);
	gen6_gt_pm_disable_irq(gt, GEN6_PM_RPS_EVENTS);
	spin_unlock_irq(&gt->irq_lock);

	intel_synchronize_irq(gt->i915);

	/*
	 * Now that we will not be generating any more work, flush any
	 * outstanding tasks. As we are called on the RPS idle path,
	 * we will reset the GPU to minimum frequencies, so the current
	 * state of the worker can be discarded.
	 */
	cancel_work_sync(&rps->work);

	rps_reset_interrupts(rps);
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
	DRM_DEBUG_DRIVER("fmax: %d, fmin: %d, fstart: %d\n",
			 fmax, fmin, fstart);

	rps->min_freq = fmax;
	rps->max_freq = fmin;

	rps->idle_freq = rps->min_freq;
	rps->cur_freq = rps->idle_freq;
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

static bool gen5_rps_set(struct intel_rps *rps, u8 val)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	u16 rgvswctl;

	lockdep_assert_held(&mchdev_lock);

	rgvswctl = intel_uncore_read16(uncore, MEMSWCTL);
	if (rgvswctl & MEMCTL_CMD_STS) {
		DRM_DEBUG("gpu busy, RCS change rejected\n");
		return false; /* still busy with another command */
	}

	/* Invert the frequency bin into an ips delay */
	val = rps->max_freq - val;
	val = rps->min_freq + val;

	rgvswctl =
		(MEMCTL_CMD_CHFREQ << MEMCTL_CMD_SHIFT) |
		(val << MEMCTL_FREQ_SHIFT) |
		MEMCTL_SFCAVM;
	intel_uncore_write16(uncore, MEMSWCTL, rgvswctl);
	intel_uncore_posting_read16(uncore, MEMSWCTL);

	rgvswctl |= MEMCTL_CMD_STS;
	intel_uncore_write16(uncore, MEMSWCTL, rgvswctl);

	return true;
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
		DRM_ERROR("stuck trying to change perf mode\n");
	mdelay(1);

	gen5_rps_set(rps, rps->cur_freq);

	rps->ips.last_count1 = intel_uncore_read(uncore, DMIEC);
	rps->ips.last_count1 += intel_uncore_read(uncore, DDREC);
	rps->ips.last_count1 += intel_uncore_read(uncore, CSIEC);
	rps->ips.last_time1 = jiffies_to_msecs(jiffies);

	rps->ips.last_count2 = intel_uncore_read(uncore, GFXEC);
	rps->ips.last_time2 = ktime_get_raw_ns();

	spin_unlock_irq(&mchdev_lock);

	rps->ips.corr = init_emon(uncore);

	return true;
}

static void gen5_rps_disable(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);
	u16 rgvswctl;

	spin_lock_irq(&mchdev_lock);

	rgvswctl = intel_uncore_read16(uncore, MEMSWCTL);

	/* Ack interrupts, disable EFC interrupt */
	intel_uncore_write(uncore, MEMINTREN,
			   intel_uncore_read(uncore, MEMINTREN) &
			   ~MEMINT_EVAL_CHG_EN);
	intel_uncore_write(uncore, MEMINTRSTS, MEMINT_EVAL_CHG);
	intel_uncore_write(uncore, DEIER,
			   intel_uncore_read(uncore, DEIER) & ~DE_PCU_EVENT);
	intel_uncore_write(uncore, DEIIR, DE_PCU_EVENT);
	intel_uncore_write(uncore, DEIMR,
			   intel_uncore_read(uncore, DEIMR) | DE_PCU_EVENT);

	/* Go back to the starting frequency */
	gen5_rps_set(rps, rps->idle_freq);
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
	if (INTEL_GEN(rps_to_i915(rps)) >= 9) {
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
	struct intel_uncore *uncore = rps_to_uncore(rps);
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 threshold_up = 0, threshold_down = 0; /* in % */
	u32 ei_up = 0, ei_down = 0;

	lockdep_assert_held(&rps->power.mutex);

	if (new_power == rps->power.mode)
		return;

	/* Note the units here are not exactly 1us, but 1280ns. */
	switch (new_power) {
	case LOW_POWER:
		/* Upclock if more than 95% busy over 16ms */
		ei_up = 16000;
		threshold_up = 95;

		/* Downclock if less than 85% busy over 32ms */
		ei_down = 32000;
		threshold_down = 85;
		break;

	case BETWEEN:
		/* Upclock if more than 90% busy over 13ms */
		ei_up = 13000;
		threshold_up = 90;

		/* Downclock if less than 75% busy over 32ms */
		ei_down = 32000;
		threshold_down = 75;
		break;

	case HIGH_POWER:
		/* Upclock if more than 85% busy over 10ms */
		ei_up = 10000;
		threshold_up = 85;

		/* Downclock if less than 60% busy over 32ms */
		ei_down = 32000;
		threshold_down = 60;
		break;
	}

	/* When byt can survive without system hang with dynamic
	 * sw freq adjustments, this restriction can be lifted.
	 */
	if (IS_VALLEYVIEW(i915))
		goto skip_hw_write;

	set(uncore, GEN6_RP_UP_EI, GT_INTERVAL_FROM_US(i915, ei_up));
	set(uncore, GEN6_RP_UP_THRESHOLD,
	    GT_INTERVAL_FROM_US(i915, ei_up * threshold_up / 100));

	set(uncore, GEN6_RP_DOWN_EI, GT_INTERVAL_FROM_US(i915, ei_down));
	set(uncore, GEN6_RP_DOWN_THRESHOLD,
	    GT_INTERVAL_FROM_US(i915, ei_down * threshold_down / 100));

	set(uncore, GEN6_RP_CONTROL,
	    (INTEL_GEN(i915) > 9 ? 0 : GEN6_RP_MEDIA_TURBO) |
	    GEN6_RP_MEDIA_HW_NORMAL_MODE |
	    GEN6_RP_MEDIA_IS_GFX |
	    GEN6_RP_ENABLE |
	    GEN6_RP_UP_BUSY_AVG |
	    GEN6_RP_DOWN_IDLE_AVG);

skip_hw_write:
	rps->power.mode = new_power;
	rps->power.up_threshold = threshold_up;
	rps->power.down_threshold = threshold_down;
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
	mutex_lock(&rps->power.mutex);
	if (interactive) {
		if (!rps->power.interactive++ && rps->active)
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

	if (INTEL_GEN(i915) >= 9)
		swreq = GEN9_FREQUENCY(val);
	else if (IS_HASWELL(i915) || IS_BROADWELL(i915))
		swreq = HSW_FREQUENCY(val);
	else
		swreq = (GEN6_FREQUENCY(val) |
			 GEN6_OFFSET(0) |
			 GEN6_AGGRESSIVE_TURBO);
	set(uncore, GEN6_RPNSWREQ, swreq);

	return 0;
}

static int vlv_rps_set(struct intel_rps *rps, u8 val)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	int err;

	vlv_punit_get(i915);
	err = vlv_punit_write(i915, PUNIT_REG_GPU_FREQ_REQ, val);
	vlv_punit_put(i915);

	return err;
}

static int rps_set(struct intel_rps *rps, u8 val, bool update)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	int err;

	if (INTEL_GEN(i915) < 6)
		return 0;

	if (val == rps->last_freq)
		return 0;

	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		err = vlv_rps_set(rps, val);
	else
		err = gen6_rps_set(rps, val);
	if (err)
		return err;

	if (update)
		gen6_rps_set_thresholds(rps, val);
	rps->last_freq = val;

	return 0;
}

void intel_rps_unpark(struct intel_rps *rps)
{
	u8 freq;

	if (!rps->enabled)
		return;

	/*
	 * Use the user's desired frequency as a guide, but for better
	 * performance, jump directly to RPe as our starting frequency.
	 */
	mutex_lock(&rps->lock);
	rps->active = true;
	freq = max(rps->cur_freq, rps->efficient_freq),
	freq = clamp(freq, rps->min_freq_softlimit, rps->max_freq_softlimit);
	intel_rps_set(rps, freq);
	rps->last_adj = 0;
	mutex_unlock(&rps->lock);

	if (INTEL_GEN(rps_to_i915(rps)) >= 6)
		rps_enable_interrupts(rps);

	if (IS_GEN(rps_to_i915(rps), 5))
		gen5_rps_update(rps);
}

void intel_rps_park(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	if (!rps->enabled)
		return;

	if (INTEL_GEN(i915) >= 6)
		rps_disable_interrupts(rps);

	rps->active = false;
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
}

void intel_rps_boost(struct i915_request *rq)
{
	struct intel_rps *rps = &rq->engine->gt->rps;
	unsigned long flags;

	if (i915_request_signaled(rq) || !rps->active)
		return;

	/* Serializes with i915_request_retire() */
	spin_lock_irqsave(&rq->lock, flags);
	if (!i915_request_has_waitboost(rq) &&
	    !dma_fence_is_signaled_locked(&rq->fence)) {
		set_bit(I915_FENCE_FLAG_BOOST, &rq->fence.flags);

		if (!atomic_fetch_inc(&rps->num_waiters) &&
		    READ_ONCE(rps->cur_freq) < rps->boost_freq)
			schedule_work(&rps->work);

		atomic_inc(&rps->boosts);
	}
	spin_unlock_irqrestore(&rq->lock, flags);
}

int intel_rps_set(struct intel_rps *rps, u8 val)
{
	int err;

	lockdep_assert_held(&rps->lock);
	GEM_BUG_ON(val > rps->max_freq);
	GEM_BUG_ON(val < rps->min_freq);

	if (rps->active) {
		err = rps_set(rps, val, true);
		if (err)
			return err;

		/*
		 * Make sure we continue to get interrupts
		 * until we hit the minimum or maximum frequencies.
		 */
		if (INTEL_GEN(rps_to_i915(rps)) >= 6) {
			struct intel_uncore *uncore = rps_to_uncore(rps);

			set(uncore,
			    GEN6_RP_INTERRUPT_LIMITS, rps_limits(rps, val));

			set(uncore, GEN6_PMINTRMSK, rps_pm_mask(rps, val));
		}
	}

	rps->cur_freq = val;
	return 0;
}

static void gen6_rps_init(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_uncore *uncore = rps_to_uncore(rps);

	/* All of these values are in units of 50MHz */

	/* static values from HW: RP0 > RP1 > RPn (min_freq) */
	if (IS_GEN9_LP(i915)) {
		u32 rp_state_cap = intel_uncore_read(uncore, BXT_RP_STATE_CAP);

		rps->rp0_freq = (rp_state_cap >> 16) & 0xff;
		rps->rp1_freq = (rp_state_cap >>  8) & 0xff;
		rps->min_freq = (rp_state_cap >>  0) & 0xff;
	} else {
		u32 rp_state_cap = intel_uncore_read(uncore, GEN6_RP_STATE_CAP);

		rps->rp0_freq = (rp_state_cap >>  0) & 0xff;
		rps->rp1_freq = (rp_state_cap >>  8) & 0xff;
		rps->min_freq = (rp_state_cap >> 16) & 0xff;
	}

	/* hw_max = RP0 until we check for overclocking */
	rps->max_freq = rps->rp0_freq;

	rps->efficient_freq = rps->rp1_freq;
	if (IS_HASWELL(i915) || IS_BROADWELL(i915) ||
	    IS_GEN9_BC(i915) || INTEL_GEN(i915) >= 10) {
		u32 ddcc_status = 0;

		if (sandybridge_pcode_read(i915,
					   HSW_PCODE_DYNAMIC_DUTY_CYCLE_CONTROL,
					   &ddcc_status, NULL) == 0)
			rps->efficient_freq =
				clamp_t(u8,
					(ddcc_status >> 8) & 0xff,
					rps->min_freq,
					rps->max_freq);
	}

	if (IS_GEN9_BC(i915) || INTEL_GEN(i915) >= 10) {
		/* Store the frequency values in 16.66 MHZ units, which is
		 * the natural hardware unit for SKL
		 */
		rps->rp0_freq *= GEN9_FREQ_SCALER;
		rps->rp1_freq *= GEN9_FREQ_SCALER;
		rps->min_freq *= GEN9_FREQ_SCALER;
		rps->max_freq *= GEN9_FREQ_SCALER;
		rps->efficient_freq *= GEN9_FREQ_SCALER;
	}
}

static bool rps_reset(struct intel_rps *rps)
{
	/* force a reset */
	rps->power.mode = -1;
	rps->last_freq = -1;

	if (rps_set(rps, rps->min_freq, true)) {
		DRM_ERROR("Failed to reset RPS to initial values\n");
		return false;
	}

	rps->cur_freq = rps->min_freq;
	return true;
}

/* See the Gen9_GT_PM_Programming_Guide doc for the below */
static bool gen9_rps_enable(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_uncore *uncore = rps_to_uncore(rps);

	/* Program defaults and thresholds for RPS */
	if (IS_GEN(i915, 9))
		intel_uncore_write_fw(uncore, GEN6_RC_VIDEO_FREQ,
				      GEN9_FREQUENCY(rps->rp1_freq));

	/* 1 second timeout */
	intel_uncore_write_fw(uncore, GEN6_RP_DOWN_TIMEOUT,
			      GT_INTERVAL_FROM_US(i915, 1000000));

	intel_uncore_write_fw(uncore, GEN6_RP_IDLE_HYSTERSIS, 0xa);

	return rps_reset(rps);
}

static bool gen8_rps_enable(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);

	intel_uncore_write_fw(uncore, GEN6_RC_VIDEO_FREQ,
			      HSW_FREQUENCY(rps->rp1_freq));

	/* NB: Docs say 1s, and 1000000 - which aren't equivalent */
	intel_uncore_write_fw(uncore, GEN6_RP_DOWN_TIMEOUT,
			      100000000 / 128); /* 1 second timeout */

	intel_uncore_write_fw(uncore, GEN6_RP_IDLE_HYSTERSIS, 10);

	return rps_reset(rps);
}

static bool gen6_rps_enable(struct intel_rps *rps)
{
	struct intel_uncore *uncore = rps_to_uncore(rps);

	/* Power down if completely idle for over 50ms */
	intel_uncore_write_fw(uncore, GEN6_RP_DOWN_TIMEOUT, 50000);
	intel_uncore_write_fw(uncore, GEN6_RP_IDLE_HYSTERSIS, 10);

	return rps_reset(rps);
}

static int chv_rps_max_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val;

	val = vlv_punit_read(i915, FB_GFX_FMAX_AT_VMAX_FUSE);

	switch (RUNTIME_INFO(i915)->sseu.eu_total) {
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

	/* Setting Fixed Bias */
	vlv_punit_get(i915);

	val = VLV_OVERRIDE_EN | VLV_SOC_TDP_EN | CHV_BIAS_CPU_50_SOC_50;
	vlv_punit_write(i915, VLV_TURBO_SOC_OVERRIDE, val);

	val = vlv_punit_read(i915, PUNIT_REG_GPU_FREQ_STS);

	vlv_punit_put(i915);

	/* RPS code assumes GPLL is used */
	drm_WARN_ONCE(&i915->drm, (val & GPLLENABLE) == 0,
		      "GPLL not enabled\n");

	DRM_DEBUG_DRIVER("GPLL enabled? %s\n", yesno(val & GPLLENABLE));
	DRM_DEBUG_DRIVER("GPU status: 0x%08x\n", val);

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

	vlv_punit_get(i915);

	/* Setting Fixed Bias */
	val = VLV_OVERRIDE_EN | VLV_SOC_TDP_EN | VLV_BIAS_CPU_125_SOC_875;
	vlv_punit_write(i915, VLV_TURBO_SOC_OVERRIDE, val);

	val = vlv_punit_read(i915, PUNIT_REG_GPU_FREQ_STS);

	vlv_punit_put(i915);

	/* RPS code assumes GPLL is used */
	drm_WARN_ONCE(&i915->drm, (val & GPLLENABLE) == 0,
		      "GPLL not enabled\n");

	DRM_DEBUG_DRIVER("GPLL enabled? %s\n", yesno(val & GPLLENABLE));
	DRM_DEBUG_DRIVER("GPU status: 0x%08x\n", val);

	return rps_reset(rps);
}

static unsigned long __ips_gfx_val(struct intel_ips *ips)
{
	struct intel_rps *rps = container_of(ips, typeof(*rps), ips);
	struct intel_uncore *uncore = rps_to_uncore(rps);
	unsigned long t, corr, state1, corr2, state2;
	u32 pxvid, ext_v;

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

	corr = corr * 150142 * state1 / 10000 - 78642;
	corr /= 100000;
	corr2 = corr * ips->corr;

	state2 = corr2 * state1 / 10000;
	state2 /= 100; /* convert to mW */

	__gen5_ips_update(ips);

	return ips->gfx_power + state2;
}

void intel_rps_enable(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	struct intel_uncore *uncore = rps_to_uncore(rps);

	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);
	if (IS_CHERRYVIEW(i915))
		rps->enabled = chv_rps_enable(rps);
	else if (IS_VALLEYVIEW(i915))
		rps->enabled = vlv_rps_enable(rps);
	else if (INTEL_GEN(i915) >= 9)
		rps->enabled = gen9_rps_enable(rps);
	else if (INTEL_GEN(i915) >= 8)
		rps->enabled = gen8_rps_enable(rps);
	else if (INTEL_GEN(i915) >= 6)
		rps->enabled = gen6_rps_enable(rps);
	else if (IS_IRONLAKE_M(i915))
		rps->enabled = gen5_rps_enable(rps);
	intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);
	if (!rps->enabled)
		return;

	drm_WARN_ON(&i915->drm, rps->max_freq < rps->min_freq);
	drm_WARN_ON(&i915->drm, rps->idle_freq > rps->max_freq);

	drm_WARN_ON(&i915->drm, rps->efficient_freq < rps->min_freq);
	drm_WARN_ON(&i915->drm, rps->efficient_freq > rps->max_freq);
}

static void gen6_rps_disable(struct intel_rps *rps)
{
	set(rps_to_uncore(rps), GEN6_RP_CONTROL, 0);
}

void intel_rps_disable(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	rps->enabled = false;

	if (INTEL_GEN(i915) >= 6)
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

	if (INTEL_GEN(i915) >= 9)
		return DIV_ROUND_CLOSEST(val * GT_FREQUENCY_MULTIPLIER,
					 GEN9_FREQ_SCALER);
	else if (IS_CHERRYVIEW(i915))
		return chv_gpu_freq(rps, val);
	else if (IS_VALLEYVIEW(i915))
		return byt_gpu_freq(rps, val);
	else
		return val * GT_FREQUENCY_MULTIPLIER;
}

int intel_freq_opcode(struct intel_rps *rps, int val)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	if (INTEL_GEN(i915) >= 9)
		return DIV_ROUND_CLOSEST(val * GEN9_FREQ_SCALER,
					 GT_FREQUENCY_MULTIPLIER);
	else if (IS_CHERRYVIEW(i915))
		return chv_freq_opcode(rps, val);
	else if (IS_VALLEYVIEW(i915))
		return byt_freq_opcode(rps, val);
	else
		return DIV_ROUND_CLOSEST(val, GT_FREQUENCY_MULTIPLIER);
}

static void vlv_init_gpll_ref_freq(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	rps->gpll_ref_freq =
		vlv_get_cck_clock(i915, "GPLL ref",
				  CCK_GPLL_CLOCK_CONTROL,
				  i915->czclk_freq);

	DRM_DEBUG_DRIVER("GPLL reference freq: %d kHz\n", rps->gpll_ref_freq);
}

static void vlv_rps_init(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val;

	vlv_iosf_sb_get(i915,
			BIT(VLV_IOSF_SB_PUNIT) |
			BIT(VLV_IOSF_SB_NC) |
			BIT(VLV_IOSF_SB_CCK));

	vlv_init_gpll_ref_freq(rps);

	val = vlv_punit_read(i915, PUNIT_REG_GPU_FREQ_STS);
	switch ((val >> 6) & 3) {
	case 0:
	case 1:
		i915->mem_freq = 800;
		break;
	case 2:
		i915->mem_freq = 1066;
		break;
	case 3:
		i915->mem_freq = 1333;
		break;
	}
	DRM_DEBUG_DRIVER("DDR speed: %d MHz\n", i915->mem_freq);

	rps->max_freq = vlv_rps_max_freq(rps);
	rps->rp0_freq = rps->max_freq;
	DRM_DEBUG_DRIVER("max GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(rps, rps->max_freq),
			 rps->max_freq);

	rps->efficient_freq = vlv_rps_rpe_freq(rps);
	DRM_DEBUG_DRIVER("RPe GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(rps, rps->efficient_freq),
			 rps->efficient_freq);

	rps->rp1_freq = vlv_rps_guar_freq(rps);
	DRM_DEBUG_DRIVER("RP1(Guar Freq) GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(rps, rps->rp1_freq),
			 rps->rp1_freq);

	rps->min_freq = vlv_rps_min_freq(rps);
	DRM_DEBUG_DRIVER("min GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(rps, rps->min_freq),
			 rps->min_freq);

	vlv_iosf_sb_put(i915,
			BIT(VLV_IOSF_SB_PUNIT) |
			BIT(VLV_IOSF_SB_NC) |
			BIT(VLV_IOSF_SB_CCK));
}

static void chv_rps_init(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 val;

	vlv_iosf_sb_get(i915,
			BIT(VLV_IOSF_SB_PUNIT) |
			BIT(VLV_IOSF_SB_NC) |
			BIT(VLV_IOSF_SB_CCK));

	vlv_init_gpll_ref_freq(rps);

	val = vlv_cck_read(i915, CCK_FUSE_REG);

	switch ((val >> 2) & 0x7) {
	case 3:
		i915->mem_freq = 2000;
		break;
	default:
		i915->mem_freq = 1600;
		break;
	}
	DRM_DEBUG_DRIVER("DDR speed: %d MHz\n", i915->mem_freq);

	rps->max_freq = chv_rps_max_freq(rps);
	rps->rp0_freq = rps->max_freq;
	DRM_DEBUG_DRIVER("max GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(rps, rps->max_freq),
			 rps->max_freq);

	rps->efficient_freq = chv_rps_rpe_freq(rps);
	DRM_DEBUG_DRIVER("RPe GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(rps, rps->efficient_freq),
			 rps->efficient_freq);

	rps->rp1_freq = chv_rps_guar_freq(rps);
	DRM_DEBUG_DRIVER("RP1(Guar) GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(rps, rps->rp1_freq),
			 rps->rp1_freq);

	rps->min_freq = chv_rps_min_freq(rps);
	DRM_DEBUG_DRIVER("min GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(rps, rps->min_freq),
			 rps->min_freq);

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
	bool client_boost = false;
	int new_freq, adj, min, max;
	u32 pm_iir = 0;

	spin_lock_irq(&gt->irq_lock);
	pm_iir = fetch_and_zero(&rps->pm_iir);
	client_boost = atomic_read(&rps->num_waiters);
	spin_unlock_irq(&gt->irq_lock);

	/* Make sure we didn't queue anything we're not going to process. */
	if ((pm_iir & rps->pm_events) == 0 && !client_boost)
		goto out;

	mutex_lock(&rps->lock);

	pm_iir |= vlv_wa_c0_ei(rps, pm_iir);

	adj = rps->last_adj;
	new_freq = rps->cur_freq;
	min = rps->min_freq_softlimit;
	max = rps->max_freq_softlimit;
	if (client_boost)
		max = rps->max_freq;
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

	rps->last_adj = adj;

	/*
	 * Limit deboosting and boosting to keep ourselves at the extremes
	 * when in the respective power modes (i.e. slowly decrease frequencies
	 * while in the HIGH_POWER zone and slowly increase frequencies while
	 * in the LOW_POWER zone). On idle, we will hit the timeout and drop
	 * to the next level quickly, and conversely if busy we expect to
	 * hit a waitboost and rapidly switch into max power.
	 */
	if ((adj < 0 && rps->power.mode == HIGH_POWER) ||
	    (adj > 0 && rps->power.mode == LOW_POWER))
		rps->last_adj = 0;

	/* sysfs frequency interfaces may have snuck in while servicing the
	 * interrupt
	 */
	new_freq += adj;
	new_freq = clamp_t(int, new_freq, min, max);

	if (intel_rps_set(rps, new_freq)) {
		DRM_DEBUG_DRIVER("Failed to set new GPU frequency\n");
		rps->last_adj = 0;
	}

	mutex_unlock(&rps->lock);

out:
	spin_lock_irq(&gt->irq_lock);
	gen6_gt_pm_unmask_irq(gt, rps->pm_events);
	spin_unlock_irq(&gt->irq_lock);
}

void gen11_rps_irq_handler(struct intel_rps *rps, u32 pm_iir)
{
	struct intel_gt *gt = rps_to_gt(rps);
	const u32 events = rps->pm_events & pm_iir;

	lockdep_assert_held(&gt->irq_lock);

	if (unlikely(!events))
		return;

	gen6_gt_pm_mask_irq(gt, events);

	rps->pm_iir |= events;
	schedule_work(&rps->work);
}

void gen6_rps_irq_handler(struct intel_rps *rps, u32 pm_iir)
{
	struct intel_gt *gt = rps_to_gt(rps);

	if (pm_iir & rps->pm_events) {
		spin_lock(&gt->irq_lock);
		gen6_gt_pm_mask_irq(gt, pm_iir & rps->pm_events);
		rps->pm_iir |= pm_iir & rps->pm_events;
		schedule_work(&rps->work);
		spin_unlock(&gt->irq_lock);
	}

	if (INTEL_GEN(gt->i915) >= 8)
		return;

	if (pm_iir & PM_VEBOX_USER_INTERRUPT)
		intel_engine_signal_breadcrumbs(gt->engine[VECS0]);

	if (pm_iir & PM_VEBOX_CS_ERROR_INTERRUPT)
		DRM_DEBUG("Command parser error, pm_iir 0x%08x\n", pm_iir);
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

	if (new_freq != rps->cur_freq && gen5_rps_set(rps, new_freq))
		rps->cur_freq = new_freq;

	spin_unlock(&mchdev_lock);
}

void intel_rps_init_early(struct intel_rps *rps)
{
	mutex_init(&rps->lock);
	mutex_init(&rps->power.mutex);

	INIT_WORK(&rps->work, rps_work);

	atomic_set(&rps->num_waiters, 0);
}

void intel_rps_init(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);

	if (IS_CHERRYVIEW(i915))
		chv_rps_init(rps);
	else if (IS_VALLEYVIEW(i915))
		vlv_rps_init(rps);
	else if (INTEL_GEN(i915) >= 6)
		gen6_rps_init(rps);
	else if (IS_IRONLAKE_M(i915))
		gen5_rps_init(rps);

	/* Derive initial user preferences/limits from the hardware limits */
	rps->max_freq_softlimit = rps->max_freq;
	rps->min_freq_softlimit = rps->min_freq;

	/* After setting max-softlimit, find the overclock max freq */
	if (IS_GEN(i915, 6) || IS_IVYBRIDGE(i915) || IS_HASWELL(i915)) {
		u32 params = 0;

		sandybridge_pcode_read(i915, GEN6_READ_OC_PARAMS,
				       &params, NULL);
		if (params & BIT(31)) { /* OC supported */
			DRM_DEBUG_DRIVER("Overclocking supported, max: %dMHz, overclock: %dMHz\n",
					 (rps->max_freq & 0xff) * 50,
					 (params & 0xff) * 50);
			rps->max_freq = params & 0xff;
		}
	}

	/* Finally allow us to boost to max by default */
	rps->boost_freq = rps->max_freq;
	rps->idle_freq = rps->min_freq;
	rps->cur_freq = rps->idle_freq;

	rps->pm_intrmsk_mbz = 0;

	/*
	 * SNB,IVB,HSW can while VLV,CHV may hard hang on looping batchbuffer
	 * if GEN6_PM_UP_EI_EXPIRED is masked.
	 *
	 * TODO: verify if this can be reproduced on VLV,CHV.
	 */
	if (INTEL_GEN(i915) <= 7)
		rps->pm_intrmsk_mbz |= GEN6_PM_RP_UP_EI_EXPIRED;

	if (INTEL_GEN(i915) >= 8 && INTEL_GEN(i915) < 11)
		rps->pm_intrmsk_mbz |= GEN8_PMINTR_DISABLE_REDIRECT_TO_GUC;
}

u32 intel_rps_get_cagf(struct intel_rps *rps, u32 rpstat)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 cagf;

	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		cagf = (rpstat >> 8) & 0xff;
	else if (INTEL_GEN(i915) >= 9)
		cagf = (rpstat & GEN9_CAGF_MASK) >> GEN9_CAGF_SHIFT;
	else if (IS_HASWELL(i915) || IS_BROADWELL(i915))
		cagf = (rpstat & HSW_CAGF_MASK) >> HSW_CAGF_SHIFT;
	else
		cagf = (rpstat & GEN6_CAGF_MASK) >> GEN6_CAGF_SHIFT;

	return cagf;
}

static u32 read_cagf(struct intel_rps *rps)
{
	struct drm_i915_private *i915 = rps_to_i915(rps);
	u32 freq;

	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915)) {
		vlv_punit_get(i915);
		freq = vlv_punit_read(i915, PUNIT_REG_GPU_FREQ_STS);
		vlv_punit_put(i915);
	} else {
		freq = intel_uncore_read(rps_to_gt(rps)->uncore, GEN6_RPSTAT1);
	}

	return intel_rps_get_cagf(rps, freq);
}

u32 intel_rps_read_actual_frequency(struct intel_rps *rps)
{
	struct intel_runtime_pm *rpm = rps_to_gt(rps)->uncore->rpm;
	intel_wakeref_t wakeref;
	u32 freq = 0;

	with_intel_runtime_pm_if_in_use(rpm, wakeref)
		freq = intel_gpu_freq(rps, read_cagf(rps));

	return freq;
}

/* External interface for intel_ips.ko */

static struct drm_i915_private __rcu *ips_mchdev;

/**
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
	if (IS_GEN(gt->i915, 5)) {
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
	if (!kref_get_unless_zero(&i915->drm.ref))
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
		struct intel_ips *ips = &i915->gt.rps.ips;

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

	rps = &i915->gt.rps;

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

	rps = &i915->gt.rps;

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

	ret = i915->gt.awake;

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

	rps = &i915->gt.rps;

	spin_lock_irq(&mchdev_lock);
	rps->max_freq_softlimit = rps->min_freq;
	ret = gen5_rps_set(&i915->gt.rps, rps->min_freq);
	spin_unlock_irq(&mchdev_lock);

	drm_dev_put(&i915->drm);
	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_turbo_disable);
