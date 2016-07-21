/*
 * Copyright © 2013 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "i915_drv.h"
#include "intel_drv.h"
#include "i915_vgpu.h"

#include <linux/pm_runtime.h>

#define FORCEWAKE_ACK_TIMEOUT_MS 50

#define __raw_posting_read(dev_priv__, reg__) (void)__raw_i915_read32((dev_priv__), (reg__))

static const char * const forcewake_domain_names[] = {
	"render",
	"blitter",
	"media",
};

const char *
intel_uncore_forcewake_domain_to_str(const enum forcewake_domain_id id)
{
	BUILD_BUG_ON(ARRAY_SIZE(forcewake_domain_names) != FW_DOMAIN_ID_COUNT);

	if (id >= 0 && id < FW_DOMAIN_ID_COUNT)
		return forcewake_domain_names[id];

	WARN_ON(id);

	return "unknown";
}

static inline void
fw_domain_reset(const struct intel_uncore_forcewake_domain *d)
{
	WARN_ON(!i915_mmio_reg_valid(d->reg_set));
	__raw_i915_write32(d->i915, d->reg_set, d->val_reset);
}

static inline void
fw_domain_arm_timer(struct intel_uncore_forcewake_domain *d)
{
	d->wake_count++;
	hrtimer_start_range_ns(&d->timer,
			       ktime_set(0, NSEC_PER_MSEC),
			       NSEC_PER_MSEC,
			       HRTIMER_MODE_REL);
}

static inline void
fw_domain_wait_ack_clear(const struct intel_uncore_forcewake_domain *d)
{
	if (wait_for_atomic((__raw_i915_read32(d->i915, d->reg_ack) &
			     FORCEWAKE_KERNEL) == 0,
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("%s: timed out waiting for forcewake ack to clear.\n",
			  intel_uncore_forcewake_domain_to_str(d->id));
}

static inline void
fw_domain_get(const struct intel_uncore_forcewake_domain *d)
{
	__raw_i915_write32(d->i915, d->reg_set, d->val_set);
}

static inline void
fw_domain_wait_ack(const struct intel_uncore_forcewake_domain *d)
{
	if (wait_for_atomic((__raw_i915_read32(d->i915, d->reg_ack) &
			     FORCEWAKE_KERNEL),
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("%s: timed out waiting for forcewake ack request.\n",
			  intel_uncore_forcewake_domain_to_str(d->id));
}

static inline void
fw_domain_put(const struct intel_uncore_forcewake_domain *d)
{
	__raw_i915_write32(d->i915, d->reg_set, d->val_clear);
}

static inline void
fw_domain_posting_read(const struct intel_uncore_forcewake_domain *d)
{
	/* something from same cacheline, but not from the set register */
	if (i915_mmio_reg_valid(d->reg_post))
		__raw_posting_read(d->i915, d->reg_post);
}

static void
fw_domains_get(struct drm_i915_private *dev_priv, enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;

	for_each_fw_domain_masked(d, fw_domains, dev_priv) {
		fw_domain_wait_ack_clear(d);
		fw_domain_get(d);
	}

	for_each_fw_domain_masked(d, fw_domains, dev_priv)
		fw_domain_wait_ack(d);
}

static void
fw_domains_put(struct drm_i915_private *dev_priv, enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;

	for_each_fw_domain_masked(d, fw_domains, dev_priv) {
		fw_domain_put(d);
		fw_domain_posting_read(d);
	}
}

static void
fw_domains_posting_read(struct drm_i915_private *dev_priv)
{
	struct intel_uncore_forcewake_domain *d;

	/* No need to do for all, just do for first found */
	for_each_fw_domain(d, dev_priv) {
		fw_domain_posting_read(d);
		break;
	}
}

static void
fw_domains_reset(struct drm_i915_private *dev_priv, enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;

	if (dev_priv->uncore.fw_domains == 0)
		return;

	for_each_fw_domain_masked(d, fw_domains, dev_priv)
		fw_domain_reset(d);

	fw_domains_posting_read(dev_priv);
}

static void __gen6_gt_wait_for_thread_c0(struct drm_i915_private *dev_priv)
{
	/* w/a for a sporadic read returning 0 by waiting for the GT
	 * thread to wake up.
	 */
	if (wait_for_atomic_us((__raw_i915_read32(dev_priv, GEN6_GT_THREAD_STATUS_REG) &
				GEN6_GT_THREAD_STATUS_CORE_MASK) == 0, 500))
		DRM_ERROR("GT thread status wait timed out\n");
}

static void fw_domains_get_with_thread_status(struct drm_i915_private *dev_priv,
					      enum forcewake_domains fw_domains)
{
	fw_domains_get(dev_priv, fw_domains);

	/* WaRsForcewakeWaitTC0:snb,ivb,hsw,bdw,vlv */
	__gen6_gt_wait_for_thread_c0(dev_priv);
}

static void gen6_gt_check_fifodbg(struct drm_i915_private *dev_priv)
{
	u32 gtfifodbg;

	gtfifodbg = __raw_i915_read32(dev_priv, GTFIFODBG);
	if (WARN(gtfifodbg, "GT wake FIFO error 0x%x\n", gtfifodbg))
		__raw_i915_write32(dev_priv, GTFIFODBG, gtfifodbg);
}

static void fw_domains_put_with_fifo(struct drm_i915_private *dev_priv,
				     enum forcewake_domains fw_domains)
{
	fw_domains_put(dev_priv, fw_domains);
	gen6_gt_check_fifodbg(dev_priv);
}

static inline u32 fifo_free_entries(struct drm_i915_private *dev_priv)
{
	u32 count = __raw_i915_read32(dev_priv, GTFIFOCTL);

	return count & GT_FIFO_FREE_ENTRIES_MASK;
}

static int __gen6_gt_wait_for_fifo(struct drm_i915_private *dev_priv)
{
	int ret = 0;

	/* On VLV, FIFO will be shared by both SW and HW.
	 * So, we need to read the FREE_ENTRIES everytime */
	if (IS_VALLEYVIEW(dev_priv))
		dev_priv->uncore.fifo_count = fifo_free_entries(dev_priv);

	if (dev_priv->uncore.fifo_count < GT_FIFO_NUM_RESERVED_ENTRIES) {
		int loop = 500;
		u32 fifo = fifo_free_entries(dev_priv);

		while (fifo <= GT_FIFO_NUM_RESERVED_ENTRIES && loop--) {
			udelay(10);
			fifo = fifo_free_entries(dev_priv);
		}
		if (WARN_ON(loop < 0 && fifo <= GT_FIFO_NUM_RESERVED_ENTRIES))
			++ret;
		dev_priv->uncore.fifo_count = fifo;
	}
	dev_priv->uncore.fifo_count--;

	return ret;
}

static enum hrtimer_restart
intel_uncore_fw_release_timer(struct hrtimer *timer)
{
	struct intel_uncore_forcewake_domain *domain =
	       container_of(timer, struct intel_uncore_forcewake_domain, timer);
	unsigned long irqflags;

	assert_rpm_device_not_suspended(domain->i915);

	spin_lock_irqsave(&domain->i915->uncore.lock, irqflags);
	if (WARN_ON(domain->wake_count == 0))
		domain->wake_count++;

	if (--domain->wake_count == 0)
		domain->i915->uncore.funcs.force_wake_put(domain->i915,
							  1 << domain->id);

	spin_unlock_irqrestore(&domain->i915->uncore.lock, irqflags);

	return HRTIMER_NORESTART;
}

void intel_uncore_forcewake_reset(struct drm_i915_private *dev_priv,
				  bool restore)
{
	unsigned long irqflags;
	struct intel_uncore_forcewake_domain *domain;
	int retry_count = 100;
	enum forcewake_domains fw = 0, active_domains;

	/* Hold uncore.lock across reset to prevent any register access
	 * with forcewake not set correctly. Wait until all pending
	 * timers are run before holding.
	 */
	while (1) {
		active_domains = 0;

		for_each_fw_domain(domain, dev_priv) {
			if (hrtimer_cancel(&domain->timer) == 0)
				continue;

			intel_uncore_fw_release_timer(&domain->timer);
		}

		spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

		for_each_fw_domain(domain, dev_priv) {
			if (hrtimer_active(&domain->timer))
				active_domains |= domain->mask;
		}

		if (active_domains == 0)
			break;

		if (--retry_count == 0) {
			DRM_ERROR("Timed out waiting for forcewake timers to finish\n");
			break;
		}

		spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
		cond_resched();
	}

	WARN_ON(active_domains);

	for_each_fw_domain(domain, dev_priv)
		if (domain->wake_count)
			fw |= domain->mask;

	if (fw)
		dev_priv->uncore.funcs.force_wake_put(dev_priv, fw);

	fw_domains_reset(dev_priv, FORCEWAKE_ALL);

	if (restore) { /* If reset with a user forcewake, try to restore */
		if (fw)
			dev_priv->uncore.funcs.force_wake_get(dev_priv, fw);

		if (IS_GEN6(dev_priv) || IS_GEN7(dev_priv))
			dev_priv->uncore.fifo_count =
				fifo_free_entries(dev_priv);
	}

	if (!restore)
		assert_forcewakes_inactive(dev_priv);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static u64 gen9_edram_size(struct drm_i915_private *dev_priv)
{
	const unsigned int ways[8] = { 4, 8, 12, 16, 16, 16, 16, 16 };
	const unsigned int sets[4] = { 1, 1, 2, 2 };
	const u32 cap = dev_priv->edram_cap;

	return EDRAM_NUM_BANKS(cap) *
		ways[EDRAM_WAYS_IDX(cap)] *
		sets[EDRAM_SETS_IDX(cap)] *
		1024 * 1024;
}

u64 intel_uncore_edram_size(struct drm_i915_private *dev_priv)
{
	if (!HAS_EDRAM(dev_priv))
		return 0;

	/* The needed capability bits for size calculation
	 * are not there with pre gen9 so return 128MB always.
	 */
	if (INTEL_GEN(dev_priv) < 9)
		return 128 * 1024 * 1024;

	return gen9_edram_size(dev_priv);
}

static void intel_uncore_edram_detect(struct drm_i915_private *dev_priv)
{
	if (IS_HASWELL(dev_priv) ||
	    IS_BROADWELL(dev_priv) ||
	    INTEL_GEN(dev_priv) >= 9) {
		dev_priv->edram_cap = __raw_i915_read32(dev_priv,
							HSW_EDRAM_CAP);

		/* NB: We can't write IDICR yet because we do not have gt funcs
		 * set up */
	} else {
		dev_priv->edram_cap = 0;
	}

	if (HAS_EDRAM(dev_priv))
		DRM_INFO("Found %lluMB of eDRAM\n",
			 intel_uncore_edram_size(dev_priv) / (1024 * 1024));
}

static bool
fpga_check_for_unclaimed_mmio(struct drm_i915_private *dev_priv)
{
	u32 dbg;

	dbg = __raw_i915_read32(dev_priv, FPGA_DBG);
	if (likely(!(dbg & FPGA_DBG_RM_NOCLAIM)))
		return false;

	__raw_i915_write32(dev_priv, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);

	return true;
}

static bool
vlv_check_for_unclaimed_mmio(struct drm_i915_private *dev_priv)
{
	u32 cer;

	cer = __raw_i915_read32(dev_priv, CLAIM_ER);
	if (likely(!(cer & (CLAIM_ER_OVERFLOW | CLAIM_ER_CTR_MASK))))
		return false;

	__raw_i915_write32(dev_priv, CLAIM_ER, CLAIM_ER_CLR);

	return true;
}

static bool
check_for_unclaimed_mmio(struct drm_i915_private *dev_priv)
{
	if (HAS_FPGA_DBG_UNCLAIMED(dev_priv))
		return fpga_check_for_unclaimed_mmio(dev_priv);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return vlv_check_for_unclaimed_mmio(dev_priv);

	return false;
}

static void __intel_uncore_early_sanitize(struct drm_i915_private *dev_priv,
					  bool restore_forcewake)
{
	/* clear out unclaimed reg detection bit */
	if (check_for_unclaimed_mmio(dev_priv))
		DRM_DEBUG("unclaimed mmio detected on uncore init, clearing\n");

	/* clear out old GT FIFO errors */
	if (IS_GEN6(dev_priv) || IS_GEN7(dev_priv))
		__raw_i915_write32(dev_priv, GTFIFODBG,
				   __raw_i915_read32(dev_priv, GTFIFODBG));

	/* WaDisableShadowRegForCpd:chv */
	if (IS_CHERRYVIEW(dev_priv)) {
		__raw_i915_write32(dev_priv, GTFIFOCTL,
				   __raw_i915_read32(dev_priv, GTFIFOCTL) |
				   GT_FIFO_CTL_BLOCK_ALL_POLICY_STALL |
				   GT_FIFO_CTL_RC6_POLICY_STALL);
	}

	intel_uncore_forcewake_reset(dev_priv, restore_forcewake);
}

void intel_uncore_early_sanitize(struct drm_i915_private *dev_priv,
				 bool restore_forcewake)
{
	__intel_uncore_early_sanitize(dev_priv, restore_forcewake);
	i915_check_and_clear_faults(dev_priv);
}

void intel_uncore_sanitize(struct drm_i915_private *dev_priv)
{
	i915.enable_rc6 = sanitize_rc6_option(dev_priv, i915.enable_rc6);

	/* BIOS often leaves RC6 enabled, but disable it for hw init */
	intel_disable_gt_powersave(dev_priv);
}

static void __intel_uncore_forcewake_get(struct drm_i915_private *dev_priv,
					 enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;

	if (!dev_priv->uncore.funcs.force_wake_get)
		return;

	fw_domains &= dev_priv->uncore.fw_domains;

	for_each_fw_domain_masked(domain, fw_domains, dev_priv) {
		if (domain->wake_count++)
			fw_domains &= ~domain->mask;
	}

	if (fw_domains)
		dev_priv->uncore.funcs.force_wake_get(dev_priv, fw_domains);
}

/**
 * intel_uncore_forcewake_get - grab forcewake domain references
 * @dev_priv: i915 device instance
 * @fw_domains: forcewake domains to get reference on
 *
 * This function can be used get GT's forcewake domain references.
 * Normal register access will handle the forcewake domains automatically.
 * However if some sequence requires the GT to not power down a particular
 * forcewake domains this function should be called at the beginning of the
 * sequence. And subsequently the reference should be dropped by symmetric
 * call to intel_unforce_forcewake_put(). Usually caller wants all the domains
 * to be kept awake so the @fw_domains would be then FORCEWAKE_ALL.
 */
void intel_uncore_forcewake_get(struct drm_i915_private *dev_priv,
				enum forcewake_domains fw_domains)
{
	unsigned long irqflags;

	if (!dev_priv->uncore.funcs.force_wake_get)
		return;

	assert_rpm_wakelock_held(dev_priv);

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	__intel_uncore_forcewake_get(dev_priv, fw_domains);
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

/**
 * intel_uncore_forcewake_get__locked - grab forcewake domain references
 * @dev_priv: i915 device instance
 * @fw_domains: forcewake domains to get reference on
 *
 * See intel_uncore_forcewake_get(). This variant places the onus
 * on the caller to explicitly handle the dev_priv->uncore.lock spinlock.
 */
void intel_uncore_forcewake_get__locked(struct drm_i915_private *dev_priv,
					enum forcewake_domains fw_domains)
{
	assert_spin_locked(&dev_priv->uncore.lock);

	if (!dev_priv->uncore.funcs.force_wake_get)
		return;

	__intel_uncore_forcewake_get(dev_priv, fw_domains);
}

static void __intel_uncore_forcewake_put(struct drm_i915_private *dev_priv,
					 enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;

	if (!dev_priv->uncore.funcs.force_wake_put)
		return;

	fw_domains &= dev_priv->uncore.fw_domains;

	for_each_fw_domain_masked(domain, fw_domains, dev_priv) {
		if (WARN_ON(domain->wake_count == 0))
			continue;

		if (--domain->wake_count)
			continue;

		fw_domain_arm_timer(domain);
	}
}

/**
 * intel_uncore_forcewake_put - release a forcewake domain reference
 * @dev_priv: i915 device instance
 * @fw_domains: forcewake domains to put references
 *
 * This function drops the device-level forcewakes for specified
 * domains obtained by intel_uncore_forcewake_get().
 */
void intel_uncore_forcewake_put(struct drm_i915_private *dev_priv,
				enum forcewake_domains fw_domains)
{
	unsigned long irqflags;

	if (!dev_priv->uncore.funcs.force_wake_put)
		return;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	__intel_uncore_forcewake_put(dev_priv, fw_domains);
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

/**
 * intel_uncore_forcewake_put__locked - grab forcewake domain references
 * @dev_priv: i915 device instance
 * @fw_domains: forcewake domains to get reference on
 *
 * See intel_uncore_forcewake_put(). This variant places the onus
 * on the caller to explicitly handle the dev_priv->uncore.lock spinlock.
 */
void intel_uncore_forcewake_put__locked(struct drm_i915_private *dev_priv,
					enum forcewake_domains fw_domains)
{
	assert_spin_locked(&dev_priv->uncore.lock);

	if (!dev_priv->uncore.funcs.force_wake_put)
		return;

	__intel_uncore_forcewake_put(dev_priv, fw_domains);
}

void assert_forcewakes_inactive(struct drm_i915_private *dev_priv)
{
	struct intel_uncore_forcewake_domain *domain;

	if (!dev_priv->uncore.funcs.force_wake_get)
		return;

	for_each_fw_domain(domain, dev_priv)
		WARN_ON(domain->wake_count);
}

/* We give fast paths for the really cool registers */
#define NEEDS_FORCE_WAKE(reg) ((reg) < 0x40000)

#define __gen6_reg_read_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd; \
	if (NEEDS_FORCE_WAKE(offset)) \
		__fwd = FORCEWAKE_RENDER; \
	else \
		__fwd = 0; \
	__fwd; \
})

#define REG_RANGE(reg, start, end) ((reg) >= (start) && (reg) < (end))

#define FORCEWAKE_VLV_RENDER_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x2000, 0x4000) || \
	 REG_RANGE((reg), 0x5000, 0x8000) || \
	 REG_RANGE((reg), 0xB000, 0x12000) || \
	 REG_RANGE((reg), 0x2E000, 0x30000))

#define FORCEWAKE_VLV_MEDIA_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x12000, 0x14000) || \
	 REG_RANGE((reg), 0x22000, 0x24000) || \
	 REG_RANGE((reg), 0x30000, 0x40000))

#define __vlv_reg_read_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	if (!NEEDS_FORCE_WAKE(offset)) \
		__fwd = 0; \
	else if (FORCEWAKE_VLV_RENDER_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_RENDER; \
	else if (FORCEWAKE_VLV_MEDIA_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_MEDIA; \
	__fwd; \
})

static const i915_reg_t gen8_shadowed_regs[] = {
	GEN6_RPNSWREQ,
	GEN6_RC_VIDEO_FREQ,
	RING_TAIL(RENDER_RING_BASE),
	RING_TAIL(GEN6_BSD_RING_BASE),
	RING_TAIL(VEBOX_RING_BASE),
	RING_TAIL(BLT_RING_BASE),
	/* TODO: Other registers are not yet used */
};

static bool is_gen8_shadowed(u32 offset)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(gen8_shadowed_regs); i++)
		if (offset == gen8_shadowed_regs[i].reg)
			return true;

	return false;
}

#define __gen8_reg_write_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd; \
	if (NEEDS_FORCE_WAKE(offset) && !is_gen8_shadowed(offset)) \
		__fwd = FORCEWAKE_RENDER; \
	else \
		__fwd = 0; \
	__fwd; \
})

#define FORCEWAKE_CHV_RENDER_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x2000, 0x4000) || \
	 REG_RANGE((reg), 0x5200, 0x8000) || \
	 REG_RANGE((reg), 0x8300, 0x8500) || \
	 REG_RANGE((reg), 0xB000, 0xB480) || \
	 REG_RANGE((reg), 0xE000, 0xE800))

#define FORCEWAKE_CHV_MEDIA_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x8800, 0x8900) || \
	 REG_RANGE((reg), 0xD000, 0xD800) || \
	 REG_RANGE((reg), 0x12000, 0x14000) || \
	 REG_RANGE((reg), 0x1A000, 0x1C000) || \
	 REG_RANGE((reg), 0x1E800, 0x1EA00) || \
	 REG_RANGE((reg), 0x30000, 0x38000))

#define FORCEWAKE_CHV_COMMON_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x4000, 0x5000) || \
	 REG_RANGE((reg), 0x8000, 0x8300) || \
	 REG_RANGE((reg), 0x8500, 0x8600) || \
	 REG_RANGE((reg), 0x9000, 0xB000) || \
	 REG_RANGE((reg), 0xF000, 0x10000))

#define __chv_reg_read_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	if (!NEEDS_FORCE_WAKE(offset)) \
		__fwd = 0; \
	else if (FORCEWAKE_CHV_RENDER_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_RENDER; \
	else if (FORCEWAKE_CHV_MEDIA_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_MEDIA; \
	else if (FORCEWAKE_CHV_COMMON_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_RENDER | FORCEWAKE_MEDIA; \
	__fwd; \
})

#define __chv_reg_write_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	if (!NEEDS_FORCE_WAKE(offset) || is_gen8_shadowed(offset)) \
		__fwd = 0; \
	else if (FORCEWAKE_CHV_RENDER_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_RENDER; \
	else if (FORCEWAKE_CHV_MEDIA_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_MEDIA; \
	else if (FORCEWAKE_CHV_COMMON_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_RENDER | FORCEWAKE_MEDIA; \
	__fwd; \
})

#define FORCEWAKE_GEN9_UNCORE_RANGE_OFFSET(reg) \
	REG_RANGE((reg), 0xB00,  0x2000)

#define FORCEWAKE_GEN9_RENDER_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x2000, 0x2700) || \
	 REG_RANGE((reg), 0x3000, 0x4000) || \
	 REG_RANGE((reg), 0x5200, 0x8000) || \
	 REG_RANGE((reg), 0x8140, 0x8160) || \
	 REG_RANGE((reg), 0x8300, 0x8500) || \
	 REG_RANGE((reg), 0x8C00, 0x8D00) || \
	 REG_RANGE((reg), 0xB000, 0xB480) || \
	 REG_RANGE((reg), 0xE000, 0xE900) || \
	 REG_RANGE((reg), 0x24400, 0x24800))

#define FORCEWAKE_GEN9_MEDIA_RANGE_OFFSET(reg) \
	(REG_RANGE((reg), 0x8130, 0x8140) || \
	 REG_RANGE((reg), 0x8800, 0x8A00) || \
	 REG_RANGE((reg), 0xD000, 0xD800) || \
	 REG_RANGE((reg), 0x12000, 0x14000) || \
	 REG_RANGE((reg), 0x1A000, 0x1EA00) || \
	 REG_RANGE((reg), 0x30000, 0x40000))

#define FORCEWAKE_GEN9_COMMON_RANGE_OFFSET(reg) \
	REG_RANGE((reg), 0x9400, 0x9800)

#define FORCEWAKE_GEN9_BLITTER_RANGE_OFFSET(reg) \
	((reg) < 0x40000 && \
	 !FORCEWAKE_GEN9_UNCORE_RANGE_OFFSET(reg) && \
	 !FORCEWAKE_GEN9_RENDER_RANGE_OFFSET(reg) && \
	 !FORCEWAKE_GEN9_MEDIA_RANGE_OFFSET(reg) && \
	 !FORCEWAKE_GEN9_COMMON_RANGE_OFFSET(reg))

#define SKL_NEEDS_FORCE_WAKE(reg) \
	((reg) < 0x40000 && !FORCEWAKE_GEN9_UNCORE_RANGE_OFFSET(reg))

#define __gen9_reg_read_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd; \
	if (!SKL_NEEDS_FORCE_WAKE(offset)) \
		__fwd = 0; \
	else if (FORCEWAKE_GEN9_RENDER_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_RENDER; \
	else if (FORCEWAKE_GEN9_MEDIA_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_MEDIA; \
	else if (FORCEWAKE_GEN9_COMMON_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_RENDER | FORCEWAKE_MEDIA; \
	else \
		__fwd = FORCEWAKE_BLITTER; \
	__fwd; \
})

static const i915_reg_t gen9_shadowed_regs[] = {
	RING_TAIL(RENDER_RING_BASE),
	RING_TAIL(GEN6_BSD_RING_BASE),
	RING_TAIL(VEBOX_RING_BASE),
	RING_TAIL(BLT_RING_BASE),
	GEN6_RPNSWREQ,
	GEN6_RC_VIDEO_FREQ,
	/* TODO: Other registers are not yet used */
};

static bool is_gen9_shadowed(u32 offset)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(gen9_shadowed_regs); i++)
		if (offset == gen9_shadowed_regs[i].reg)
			return true;

	return false;
}

#define __gen9_reg_write_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd; \
	if (!SKL_NEEDS_FORCE_WAKE(offset) || is_gen9_shadowed(offset)) \
		__fwd = 0; \
	else if (FORCEWAKE_GEN9_RENDER_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_RENDER; \
	else if (FORCEWAKE_GEN9_MEDIA_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_MEDIA; \
	else if (FORCEWAKE_GEN9_COMMON_RANGE_OFFSET(offset)) \
		__fwd = FORCEWAKE_RENDER | FORCEWAKE_MEDIA; \
	else \
		__fwd = FORCEWAKE_BLITTER; \
	__fwd; \
})

static void
ilk_dummy_write(struct drm_i915_private *dev_priv)
{
	/* WaIssueDummyWriteToWakeupFromRC6:ilk Issue a dummy write to wake up
	 * the chip from rc6 before touching it for real. MI_MODE is masked,
	 * hence harmless to write 0 into. */
	__raw_i915_write32(dev_priv, MI_MODE, 0);
}

static void
__unclaimed_reg_debug(struct drm_i915_private *dev_priv,
		      const i915_reg_t reg,
		      const bool read,
		      const bool before)
{
	if (WARN(check_for_unclaimed_mmio(dev_priv),
		 "Unclaimed register detected %s %s register 0x%x\n",
		 before ? "before" : "after",
		 read ? "reading" : "writing to",
		 i915_mmio_reg_offset(reg)))
		i915.mmio_debug--; /* Only report the first N failures */
}

static inline void
unclaimed_reg_debug(struct drm_i915_private *dev_priv,
		    const i915_reg_t reg,
		    const bool read,
		    const bool before)
{
	if (likely(!i915.mmio_debug))
		return;

	__unclaimed_reg_debug(dev_priv, reg, read, before);
}

#define GEN2_READ_HEADER(x) \
	u##x val = 0; \
	assert_rpm_wakelock_held(dev_priv);

#define GEN2_READ_FOOTER \
	trace_i915_reg_rw(false, reg, val, sizeof(val), trace); \
	return val

#define __gen2_read(x) \
static u##x \
gen2_read##x(struct drm_i915_private *dev_priv, i915_reg_t reg, bool trace) { \
	GEN2_READ_HEADER(x); \
	val = __raw_i915_read##x(dev_priv, reg); \
	GEN2_READ_FOOTER; \
}

#define __gen5_read(x) \
static u##x \
gen5_read##x(struct drm_i915_private *dev_priv, i915_reg_t reg, bool trace) { \
	GEN2_READ_HEADER(x); \
	ilk_dummy_write(dev_priv); \
	val = __raw_i915_read##x(dev_priv, reg); \
	GEN2_READ_FOOTER; \
}

__gen5_read(8)
__gen5_read(16)
__gen5_read(32)
__gen5_read(64)
__gen2_read(8)
__gen2_read(16)
__gen2_read(32)
__gen2_read(64)

#undef __gen5_read
#undef __gen2_read

#undef GEN2_READ_FOOTER
#undef GEN2_READ_HEADER

#define GEN6_READ_HEADER(x) \
	u32 offset = i915_mmio_reg_offset(reg); \
	unsigned long irqflags; \
	u##x val = 0; \
	assert_rpm_wakelock_held(dev_priv); \
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags); \
	unclaimed_reg_debug(dev_priv, reg, true, true)

#define GEN6_READ_FOOTER \
	unclaimed_reg_debug(dev_priv, reg, true, false); \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags); \
	trace_i915_reg_rw(false, reg, val, sizeof(val), trace); \
	return val

static inline void __force_wake_auto(struct drm_i915_private *dev_priv,
				     enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;

	if (WARN_ON(!fw_domains))
		return;

	/* Ideally GCC would be constant-fold and eliminate this loop */
	for_each_fw_domain_masked(domain, fw_domains, dev_priv) {
		if (domain->wake_count) {
			fw_domains &= ~domain->mask;
			continue;
		}

		fw_domain_arm_timer(domain);
	}

	if (fw_domains)
		dev_priv->uncore.funcs.force_wake_get(dev_priv, fw_domains);
}

#define __gen6_read(x) \
static u##x \
gen6_read##x(struct drm_i915_private *dev_priv, i915_reg_t reg, bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_READ_HEADER(x); \
	fw_engine = __gen6_reg_read_fw_domains(offset); \
	if (fw_engine) \
		__force_wake_auto(dev_priv, fw_engine); \
	val = __raw_i915_read##x(dev_priv, reg); \
	GEN6_READ_FOOTER; \
}

#define __vlv_read(x) \
static u##x \
vlv_read##x(struct drm_i915_private *dev_priv, i915_reg_t reg, bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_READ_HEADER(x); \
	fw_engine = __vlv_reg_read_fw_domains(offset); \
	if (fw_engine) \
		__force_wake_auto(dev_priv, fw_engine); \
	val = __raw_i915_read##x(dev_priv, reg); \
	GEN6_READ_FOOTER; \
}

#define __chv_read(x) \
static u##x \
chv_read##x(struct drm_i915_private *dev_priv, i915_reg_t reg, bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_READ_HEADER(x); \
	fw_engine = __chv_reg_read_fw_domains(offset); \
	if (fw_engine) \
		__force_wake_auto(dev_priv, fw_engine); \
	val = __raw_i915_read##x(dev_priv, reg); \
	GEN6_READ_FOOTER; \
}

#define __gen9_read(x) \
static u##x \
gen9_read##x(struct drm_i915_private *dev_priv, i915_reg_t reg, bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_READ_HEADER(x); \
	fw_engine = __gen9_reg_read_fw_domains(offset); \
	if (fw_engine) \
		__force_wake_auto(dev_priv, fw_engine); \
	val = __raw_i915_read##x(dev_priv, reg); \
	GEN6_READ_FOOTER; \
}

__gen9_read(8)
__gen9_read(16)
__gen9_read(32)
__gen9_read(64)
__chv_read(8)
__chv_read(16)
__chv_read(32)
__chv_read(64)
__vlv_read(8)
__vlv_read(16)
__vlv_read(32)
__vlv_read(64)
__gen6_read(8)
__gen6_read(16)
__gen6_read(32)
__gen6_read(64)

#undef __gen9_read
#undef __chv_read
#undef __vlv_read
#undef __gen6_read
#undef GEN6_READ_FOOTER
#undef GEN6_READ_HEADER

#define VGPU_READ_HEADER(x) \
	unsigned long irqflags; \
	u##x val = 0; \
	assert_rpm_device_not_suspended(dev_priv); \
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags)

#define VGPU_READ_FOOTER \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags); \
	trace_i915_reg_rw(false, reg, val, sizeof(val), trace); \
	return val

#define __vgpu_read(x) \
static u##x \
vgpu_read##x(struct drm_i915_private *dev_priv, i915_reg_t reg, bool trace) { \
	VGPU_READ_HEADER(x); \
	val = __raw_i915_read##x(dev_priv, reg); \
	VGPU_READ_FOOTER; \
}

__vgpu_read(8)
__vgpu_read(16)
__vgpu_read(32)
__vgpu_read(64)

#undef __vgpu_read
#undef VGPU_READ_FOOTER
#undef VGPU_READ_HEADER

#define GEN2_WRITE_HEADER \
	trace_i915_reg_rw(true, reg, val, sizeof(val), trace); \
	assert_rpm_wakelock_held(dev_priv); \

#define GEN2_WRITE_FOOTER

#define __gen2_write(x) \
static void \
gen2_write##x(struct drm_i915_private *dev_priv, i915_reg_t reg, u##x val, bool trace) { \
	GEN2_WRITE_HEADER; \
	__raw_i915_write##x(dev_priv, reg, val); \
	GEN2_WRITE_FOOTER; \
}

#define __gen5_write(x) \
static void \
gen5_write##x(struct drm_i915_private *dev_priv, i915_reg_t reg, u##x val, bool trace) { \
	GEN2_WRITE_HEADER; \
	ilk_dummy_write(dev_priv); \
	__raw_i915_write##x(dev_priv, reg, val); \
	GEN2_WRITE_FOOTER; \
}

__gen5_write(8)
__gen5_write(16)
__gen5_write(32)
__gen5_write(64)
__gen2_write(8)
__gen2_write(16)
__gen2_write(32)
__gen2_write(64)

#undef __gen5_write
#undef __gen2_write

#undef GEN2_WRITE_FOOTER
#undef GEN2_WRITE_HEADER

#define GEN6_WRITE_HEADER \
	u32 offset = i915_mmio_reg_offset(reg); \
	unsigned long irqflags; \
	trace_i915_reg_rw(true, reg, val, sizeof(val), trace); \
	assert_rpm_wakelock_held(dev_priv); \
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags); \
	unclaimed_reg_debug(dev_priv, reg, false, true)

#define GEN6_WRITE_FOOTER \
	unclaimed_reg_debug(dev_priv, reg, false, false); \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags)

#define __gen6_write(x) \
static void \
gen6_write##x(struct drm_i915_private *dev_priv, i915_reg_t reg, u##x val, bool trace) { \
	u32 __fifo_ret = 0; \
	GEN6_WRITE_HEADER; \
	if (NEEDS_FORCE_WAKE(offset)) { \
		__fifo_ret = __gen6_gt_wait_for_fifo(dev_priv); \
	} \
	__raw_i915_write##x(dev_priv, reg, val); \
	if (unlikely(__fifo_ret)) { \
		gen6_gt_check_fifodbg(dev_priv); \
	} \
	GEN6_WRITE_FOOTER; \
}

#define __hsw_write(x) \
static void \
hsw_write##x(struct drm_i915_private *dev_priv, i915_reg_t reg, u##x val, bool trace) { \
	u32 __fifo_ret = 0; \
	GEN6_WRITE_HEADER; \
	if (NEEDS_FORCE_WAKE(offset)) { \
		__fifo_ret = __gen6_gt_wait_for_fifo(dev_priv); \
	} \
	__raw_i915_write##x(dev_priv, reg, val); \
	if (unlikely(__fifo_ret)) { \
		gen6_gt_check_fifodbg(dev_priv); \
	} \
	GEN6_WRITE_FOOTER; \
}

#define __gen8_write(x) \
static void \
gen8_write##x(struct drm_i915_private *dev_priv, i915_reg_t reg, u##x val, bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_WRITE_HEADER; \
	fw_engine = __gen8_reg_write_fw_domains(offset); \
	if (fw_engine) \
		__force_wake_auto(dev_priv, fw_engine); \
	__raw_i915_write##x(dev_priv, reg, val); \
	GEN6_WRITE_FOOTER; \
}

#define __chv_write(x) \
static void \
chv_write##x(struct drm_i915_private *dev_priv, i915_reg_t reg, u##x val, bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_WRITE_HEADER; \
	fw_engine = __chv_reg_write_fw_domains(offset); \
	if (fw_engine) \
		__force_wake_auto(dev_priv, fw_engine); \
	__raw_i915_write##x(dev_priv, reg, val); \
	GEN6_WRITE_FOOTER; \
}

#define __gen9_write(x) \
static void \
gen9_write##x(struct drm_i915_private *dev_priv, i915_reg_t reg, u##x val, \
		bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_WRITE_HEADER; \
	fw_engine = __gen9_reg_write_fw_domains(offset); \
	if (fw_engine) \
		__force_wake_auto(dev_priv, fw_engine); \
	__raw_i915_write##x(dev_priv, reg, val); \
	GEN6_WRITE_FOOTER; \
}

__gen9_write(8)
__gen9_write(16)
__gen9_write(32)
__gen9_write(64)
__chv_write(8)
__chv_write(16)
__chv_write(32)
__chv_write(64)
__gen8_write(8)
__gen8_write(16)
__gen8_write(32)
__gen8_write(64)
__hsw_write(8)
__hsw_write(16)
__hsw_write(32)
__hsw_write(64)
__gen6_write(8)
__gen6_write(16)
__gen6_write(32)
__gen6_write(64)

#undef __gen9_write
#undef __chv_write
#undef __gen8_write
#undef __hsw_write
#undef __gen6_write
#undef GEN6_WRITE_FOOTER
#undef GEN6_WRITE_HEADER

#define VGPU_WRITE_HEADER \
	unsigned long irqflags; \
	trace_i915_reg_rw(true, reg, val, sizeof(val), trace); \
	assert_rpm_device_not_suspended(dev_priv); \
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags)

#define VGPU_WRITE_FOOTER \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags)

#define __vgpu_write(x) \
static void vgpu_write##x(struct drm_i915_private *dev_priv, \
			  i915_reg_t reg, u##x val, bool trace) { \
	VGPU_WRITE_HEADER; \
	__raw_i915_write##x(dev_priv, reg, val); \
	VGPU_WRITE_FOOTER; \
}

__vgpu_write(8)
__vgpu_write(16)
__vgpu_write(32)
__vgpu_write(64)

#undef __vgpu_write
#undef VGPU_WRITE_FOOTER
#undef VGPU_WRITE_HEADER

#define ASSIGN_WRITE_MMIO_VFUNCS(x) \
do { \
	dev_priv->uncore.funcs.mmio_writeb = x##_write8; \
	dev_priv->uncore.funcs.mmio_writew = x##_write16; \
	dev_priv->uncore.funcs.mmio_writel = x##_write32; \
	dev_priv->uncore.funcs.mmio_writeq = x##_write64; \
} while (0)

#define ASSIGN_READ_MMIO_VFUNCS(x) \
do { \
	dev_priv->uncore.funcs.mmio_readb = x##_read8; \
	dev_priv->uncore.funcs.mmio_readw = x##_read16; \
	dev_priv->uncore.funcs.mmio_readl = x##_read32; \
	dev_priv->uncore.funcs.mmio_readq = x##_read64; \
} while (0)


static void fw_domain_init(struct drm_i915_private *dev_priv,
			   enum forcewake_domain_id domain_id,
			   i915_reg_t reg_set,
			   i915_reg_t reg_ack)
{
	struct intel_uncore_forcewake_domain *d;

	if (WARN_ON(domain_id >= FW_DOMAIN_ID_COUNT))
		return;

	d = &dev_priv->uncore.fw_domain[domain_id];

	WARN_ON(d->wake_count);

	d->wake_count = 0;
	d->reg_set = reg_set;
	d->reg_ack = reg_ack;

	if (IS_GEN6(dev_priv)) {
		d->val_reset = 0;
		d->val_set = FORCEWAKE_KERNEL;
		d->val_clear = 0;
	} else {
		/* WaRsClearFWBitsAtReset:bdw,skl */
		d->val_reset = _MASKED_BIT_DISABLE(0xffff);
		d->val_set = _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL);
		d->val_clear = _MASKED_BIT_DISABLE(FORCEWAKE_KERNEL);
	}

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		d->reg_post = FORCEWAKE_ACK_VLV;
	else if (IS_GEN6(dev_priv) || IS_GEN7(dev_priv) || IS_GEN8(dev_priv))
		d->reg_post = ECOBUS;

	d->i915 = dev_priv;
	d->id = domain_id;

	BUILD_BUG_ON(FORCEWAKE_RENDER != (1 << FW_DOMAIN_ID_RENDER));
	BUILD_BUG_ON(FORCEWAKE_BLITTER != (1 << FW_DOMAIN_ID_BLITTER));
	BUILD_BUG_ON(FORCEWAKE_MEDIA != (1 << FW_DOMAIN_ID_MEDIA));

	d->mask = 1 << domain_id;

	hrtimer_init(&d->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	d->timer.function = intel_uncore_fw_release_timer;

	dev_priv->uncore.fw_domains |= (1 << domain_id);

	fw_domain_reset(d);
}

static void intel_uncore_fw_domains_init(struct drm_i915_private *dev_priv)
{
	if (INTEL_INFO(dev_priv)->gen <= 5)
		return;

	if (IS_GEN9(dev_priv)) {
		dev_priv->uncore.funcs.force_wake_get = fw_domains_get;
		dev_priv->uncore.funcs.force_wake_put = fw_domains_put;
		fw_domain_init(dev_priv, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE_RENDER_GEN9,
			       FORCEWAKE_ACK_RENDER_GEN9);
		fw_domain_init(dev_priv, FW_DOMAIN_ID_BLITTER,
			       FORCEWAKE_BLITTER_GEN9,
			       FORCEWAKE_ACK_BLITTER_GEN9);
		fw_domain_init(dev_priv, FW_DOMAIN_ID_MEDIA,
			       FORCEWAKE_MEDIA_GEN9, FORCEWAKE_ACK_MEDIA_GEN9);
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		dev_priv->uncore.funcs.force_wake_get = fw_domains_get;
		if (!IS_CHERRYVIEW(dev_priv))
			dev_priv->uncore.funcs.force_wake_put =
				fw_domains_put_with_fifo;
		else
			dev_priv->uncore.funcs.force_wake_put = fw_domains_put;
		fw_domain_init(dev_priv, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE_VLV, FORCEWAKE_ACK_VLV);
		fw_domain_init(dev_priv, FW_DOMAIN_ID_MEDIA,
			       FORCEWAKE_MEDIA_VLV, FORCEWAKE_ACK_MEDIA_VLV);
	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		dev_priv->uncore.funcs.force_wake_get =
			fw_domains_get_with_thread_status;
		if (IS_HASWELL(dev_priv))
			dev_priv->uncore.funcs.force_wake_put =
				fw_domains_put_with_fifo;
		else
			dev_priv->uncore.funcs.force_wake_put = fw_domains_put;
		fw_domain_init(dev_priv, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE_MT, FORCEWAKE_ACK_HSW);
	} else if (IS_IVYBRIDGE(dev_priv)) {
		u32 ecobus;

		/* IVB configs may use multi-threaded forcewake */

		/* A small trick here - if the bios hasn't configured
		 * MT forcewake, and if the device is in RC6, then
		 * force_wake_mt_get will not wake the device and the
		 * ECOBUS read will return zero. Which will be
		 * (correctly) interpreted by the test below as MT
		 * forcewake being disabled.
		 */
		dev_priv->uncore.funcs.force_wake_get =
			fw_domains_get_with_thread_status;
		dev_priv->uncore.funcs.force_wake_put =
			fw_domains_put_with_fifo;

		/* We need to init first for ECOBUS access and then
		 * determine later if we want to reinit, in case of MT access is
		 * not working. In this stage we don't know which flavour this
		 * ivb is, so it is better to reset also the gen6 fw registers
		 * before the ecobus check.
		 */

		__raw_i915_write32(dev_priv, FORCEWAKE, 0);
		__raw_posting_read(dev_priv, ECOBUS);

		fw_domain_init(dev_priv, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE_MT, FORCEWAKE_MT_ACK);

		spin_lock_irq(&dev_priv->uncore.lock);
		fw_domains_get_with_thread_status(dev_priv, FORCEWAKE_ALL);
		ecobus = __raw_i915_read32(dev_priv, ECOBUS);
		fw_domains_put_with_fifo(dev_priv, FORCEWAKE_ALL);
		spin_unlock_irq(&dev_priv->uncore.lock);

		if (!(ecobus & FORCEWAKE_MT_ENABLE)) {
			DRM_INFO("No MT forcewake available on Ivybridge, this can result in issues\n");
			DRM_INFO("when using vblank-synced partial screen updates.\n");
			fw_domain_init(dev_priv, FW_DOMAIN_ID_RENDER,
				       FORCEWAKE, FORCEWAKE_ACK);
		}
	} else if (IS_GEN6(dev_priv)) {
		dev_priv->uncore.funcs.force_wake_get =
			fw_domains_get_with_thread_status;
		dev_priv->uncore.funcs.force_wake_put =
			fw_domains_put_with_fifo;
		fw_domain_init(dev_priv, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE, FORCEWAKE_ACK);
	}

	/* All future platforms are expected to require complex power gating */
	WARN_ON(dev_priv->uncore.fw_domains == 0);
}

void intel_uncore_init(struct drm_i915_private *dev_priv)
{
	i915_check_vgpu(dev_priv);

	intel_uncore_edram_detect(dev_priv);
	intel_uncore_fw_domains_init(dev_priv);
	__intel_uncore_early_sanitize(dev_priv, false);

	dev_priv->uncore.unclaimed_mmio_check = 1;

	switch (INTEL_INFO(dev_priv)->gen) {
	default:
	case 9:
		ASSIGN_WRITE_MMIO_VFUNCS(gen9);
		ASSIGN_READ_MMIO_VFUNCS(gen9);
		break;
	case 8:
		if (IS_CHERRYVIEW(dev_priv)) {
			ASSIGN_WRITE_MMIO_VFUNCS(chv);
			ASSIGN_READ_MMIO_VFUNCS(chv);

		} else {
			ASSIGN_WRITE_MMIO_VFUNCS(gen8);
			ASSIGN_READ_MMIO_VFUNCS(gen6);
		}
		break;
	case 7:
	case 6:
		if (IS_HASWELL(dev_priv)) {
			ASSIGN_WRITE_MMIO_VFUNCS(hsw);
		} else {
			ASSIGN_WRITE_MMIO_VFUNCS(gen6);
		}

		if (IS_VALLEYVIEW(dev_priv)) {
			ASSIGN_READ_MMIO_VFUNCS(vlv);
		} else {
			ASSIGN_READ_MMIO_VFUNCS(gen6);
		}
		break;
	case 5:
		ASSIGN_WRITE_MMIO_VFUNCS(gen5);
		ASSIGN_READ_MMIO_VFUNCS(gen5);
		break;
	case 4:
	case 3:
	case 2:
		ASSIGN_WRITE_MMIO_VFUNCS(gen2);
		ASSIGN_READ_MMIO_VFUNCS(gen2);
		break;
	}

	if (intel_vgpu_active(dev_priv)) {
		ASSIGN_WRITE_MMIO_VFUNCS(vgpu);
		ASSIGN_READ_MMIO_VFUNCS(vgpu);
	}

	i915_check_and_clear_faults(dev_priv);
}
#undef ASSIGN_WRITE_MMIO_VFUNCS
#undef ASSIGN_READ_MMIO_VFUNCS

void intel_uncore_fini(struct drm_i915_private *dev_priv)
{
	/* Paranoia: make sure we have disabled everything before we exit. */
	intel_uncore_sanitize(dev_priv);
	intel_uncore_forcewake_reset(dev_priv, false);
}

#define GEN_RANGE(l, h) GENMASK((h) - 1, (l) - 1)

static const struct register_whitelist {
	i915_reg_t offset_ldw, offset_udw;
	uint32_t size;
	/* supported gens, 0x10 for 4, 0x30 for 4 and 5, etc. */
	uint32_t gen_bitmask;
} whitelist[] = {
	{ .offset_ldw = RING_TIMESTAMP(RENDER_RING_BASE),
	  .offset_udw = RING_TIMESTAMP_UDW(RENDER_RING_BASE),
	  .size = 8, .gen_bitmask = GEN_RANGE(4, 9) },
};

int i915_reg_read_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_reg_read *reg = data;
	struct register_whitelist const *entry = whitelist;
	unsigned size;
	i915_reg_t offset_ldw, offset_udw;
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(whitelist); i++, entry++) {
		if (i915_mmio_reg_offset(entry->offset_ldw) == (reg->offset & -entry->size) &&
		    (INTEL_INFO(dev)->gen_mask & entry->gen_bitmask))
			break;
	}

	if (i == ARRAY_SIZE(whitelist))
		return -EINVAL;

	/* We use the low bits to encode extra flags as the register should
	 * be naturally aligned (and those that are not so aligned merely
	 * limit the available flags for that register).
	 */
	offset_ldw = entry->offset_ldw;
	offset_udw = entry->offset_udw;
	size = entry->size;
	size |= reg->offset ^ i915_mmio_reg_offset(offset_ldw);

	intel_runtime_pm_get(dev_priv);

	switch (size) {
	case 8 | 1:
		reg->val = I915_READ64_2x32(offset_ldw, offset_udw);
		break;
	case 8:
		reg->val = I915_READ64(offset_ldw);
		break;
	case 4:
		reg->val = I915_READ(offset_ldw);
		break;
	case 2:
		reg->val = I915_READ16(offset_ldw);
		break;
	case 1:
		reg->val = I915_READ8(offset_ldw);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

out:
	intel_runtime_pm_put(dev_priv);
	return ret;
}

static int i915_reset_complete(struct pci_dev *pdev)
{
	u8 gdrst;
	pci_read_config_byte(pdev, I915_GDRST, &gdrst);
	return (gdrst & GRDOM_RESET_STATUS) == 0;
}

static int i915_do_reset(struct drm_i915_private *dev_priv, unsigned engine_mask)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;

	/* assert reset for at least 20 usec */
	pci_write_config_byte(pdev, I915_GDRST, GRDOM_RESET_ENABLE);
	udelay(20);
	pci_write_config_byte(pdev, I915_GDRST, 0);

	return wait_for(i915_reset_complete(pdev), 500);
}

static int g4x_reset_complete(struct pci_dev *pdev)
{
	u8 gdrst;
	pci_read_config_byte(pdev, I915_GDRST, &gdrst);
	return (gdrst & GRDOM_RESET_ENABLE) == 0;
}

static int g33_do_reset(struct drm_i915_private *dev_priv, unsigned engine_mask)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	pci_write_config_byte(pdev, I915_GDRST, GRDOM_RESET_ENABLE);
	return wait_for(g4x_reset_complete(pdev), 500);
}

static int g4x_do_reset(struct drm_i915_private *dev_priv, unsigned engine_mask)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	int ret;

	pci_write_config_byte(pdev, I915_GDRST,
			      GRDOM_RENDER | GRDOM_RESET_ENABLE);
	ret =  wait_for(g4x_reset_complete(pdev), 500);
	if (ret)
		return ret;

	/* WaVcpClkGateDisableForMediaReset:ctg,elk */
	I915_WRITE(VDECCLK_GATE_D, I915_READ(VDECCLK_GATE_D) | VCP_UNIT_CLOCK_GATE_DISABLE);
	POSTING_READ(VDECCLK_GATE_D);

	pci_write_config_byte(pdev, I915_GDRST,
			      GRDOM_MEDIA | GRDOM_RESET_ENABLE);
	ret =  wait_for(g4x_reset_complete(pdev), 500);
	if (ret)
		return ret;

	/* WaVcpClkGateDisableForMediaReset:ctg,elk */
	I915_WRITE(VDECCLK_GATE_D, I915_READ(VDECCLK_GATE_D) & ~VCP_UNIT_CLOCK_GATE_DISABLE);
	POSTING_READ(VDECCLK_GATE_D);

	pci_write_config_byte(pdev, I915_GDRST, 0);

	return 0;
}

static int ironlake_do_reset(struct drm_i915_private *dev_priv,
			     unsigned engine_mask)
{
	int ret;

	I915_WRITE(ILK_GDSR,
		   ILK_GRDOM_RENDER | ILK_GRDOM_RESET_ENABLE);
	ret = intel_wait_for_register(dev_priv,
				      ILK_GDSR, ILK_GRDOM_RESET_ENABLE, 0,
				      500);
	if (ret)
		return ret;

	I915_WRITE(ILK_GDSR,
		   ILK_GRDOM_MEDIA | ILK_GRDOM_RESET_ENABLE);
	ret = intel_wait_for_register(dev_priv,
				      ILK_GDSR, ILK_GRDOM_RESET_ENABLE, 0,
				      500);
	if (ret)
		return ret;

	I915_WRITE(ILK_GDSR, 0);

	return 0;
}

/* Reset the hardware domains (GENX_GRDOM_*) specified by mask */
static int gen6_hw_domain_reset(struct drm_i915_private *dev_priv,
				u32 hw_domain_mask)
{
	/* GEN6_GDRST is not in the gt power well, no need to check
	 * for fifo space for the write or forcewake the chip for
	 * the read
	 */
	__raw_i915_write32(dev_priv, GEN6_GDRST, hw_domain_mask);

	/* Spin waiting for the device to ack the reset requests */
	return intel_wait_for_register_fw(dev_priv,
					  GEN6_GDRST, hw_domain_mask, 0,
					  500);
}

/**
 * gen6_reset_engines - reset individual engines
 * @dev_priv: i915 device
 * @engine_mask: mask of intel_ring_flag() engines or ALL_ENGINES for full reset
 *
 * This function will reset the individual engines that are set in engine_mask.
 * If you provide ALL_ENGINES as mask, full global domain reset will be issued.
 *
 * Note: It is responsibility of the caller to handle the difference between
 * asking full domain reset versus reset for all available individual engines.
 *
 * Returns 0 on success, nonzero on error.
 */
static int gen6_reset_engines(struct drm_i915_private *dev_priv,
			      unsigned engine_mask)
{
	struct intel_engine_cs *engine;
	const u32 hw_engine_mask[I915_NUM_ENGINES] = {
		[RCS] = GEN6_GRDOM_RENDER,
		[BCS] = GEN6_GRDOM_BLT,
		[VCS] = GEN6_GRDOM_MEDIA,
		[VCS2] = GEN8_GRDOM_MEDIA2,
		[VECS] = GEN6_GRDOM_VECS,
	};
	u32 hw_mask;
	int ret;

	if (engine_mask == ALL_ENGINES) {
		hw_mask = GEN6_GRDOM_FULL;
	} else {
		hw_mask = 0;
		for_each_engine_masked(engine, dev_priv, engine_mask)
			hw_mask |= hw_engine_mask[engine->id];
	}

	ret = gen6_hw_domain_reset(dev_priv, hw_mask);

	intel_uncore_forcewake_reset(dev_priv, true);

	return ret;
}

/**
 * intel_wait_for_register_fw - wait until register matches expected state
 * @dev_priv: the i915 device
 * @reg: the register to read
 * @mask: mask to apply to register value
 * @value: expected value
 * @timeout_ms: timeout in millisecond
 *
 * This routine waits until the target register @reg contains the expected
 * @value after applying the @mask, i.e. it waits until ::
 *
 *     (I915_READ_FW(reg) & mask) == value
 *
 * Otherwise, the wait will timeout after @timeout_ms milliseconds.
 *
 * Note that this routine assumes the caller holds forcewake asserted, it is
 * not suitable for very long waits. See intel_wait_for_register() if you
 * wish to wait without holding forcewake for the duration (i.e. you expect
 * the wait to be slow).
 *
 * Returns 0 if the register matches the desired condition, or -ETIMEOUT.
 */
int intel_wait_for_register_fw(struct drm_i915_private *dev_priv,
			       i915_reg_t reg,
			       const u32 mask,
			       const u32 value,
			       const unsigned long timeout_ms)
{
#define done ((I915_READ_FW(reg) & mask) == value)
	int ret = wait_for_us(done, 2);
	if (ret)
		ret = wait_for(done, timeout_ms);
	return ret;
#undef done
}

/**
 * intel_wait_for_register - wait until register matches expected state
 * @dev_priv: the i915 device
 * @reg: the register to read
 * @mask: mask to apply to register value
 * @value: expected value
 * @timeout_ms: timeout in millisecond
 *
 * This routine waits until the target register @reg contains the expected
 * @value after applying the @mask, i.e. it waits until ::
 *
 *     (I915_READ(reg) & mask) == value
 *
 * Otherwise, the wait will timeout after @timeout_ms milliseconds.
 *
 * Returns 0 if the register matches the desired condition, or -ETIMEOUT.
 */
int intel_wait_for_register(struct drm_i915_private *dev_priv,
			    i915_reg_t reg,
			    const u32 mask,
			    const u32 value,
			    const unsigned long timeout_ms)
{

	unsigned fw =
		intel_uncore_forcewake_for_reg(dev_priv, reg, FW_REG_READ);
	int ret;

	intel_uncore_forcewake_get(dev_priv, fw);
	ret = wait_for_us((I915_READ_FW(reg) & mask) == value, 2);
	intel_uncore_forcewake_put(dev_priv, fw);
	if (ret)
		ret = wait_for((I915_READ_NOTRACE(reg) & mask) == value,
			       timeout_ms);

	return ret;
}

static int gen8_request_engine_reset(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	int ret;

	I915_WRITE_FW(RING_RESET_CTL(engine->mmio_base),
		      _MASKED_BIT_ENABLE(RESET_CTL_REQUEST_RESET));

	ret = intel_wait_for_register_fw(dev_priv,
					 RING_RESET_CTL(engine->mmio_base),
					 RESET_CTL_READY_TO_RESET,
					 RESET_CTL_READY_TO_RESET,
					 700);
	if (ret)
		DRM_ERROR("%s: reset request timeout\n", engine->name);

	return ret;
}

static void gen8_unrequest_engine_reset(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	I915_WRITE_FW(RING_RESET_CTL(engine->mmio_base),
		      _MASKED_BIT_DISABLE(RESET_CTL_REQUEST_RESET));
}

static int gen8_reset_engines(struct drm_i915_private *dev_priv,
			      unsigned engine_mask)
{
	struct intel_engine_cs *engine;

	for_each_engine_masked(engine, dev_priv, engine_mask)
		if (gen8_request_engine_reset(engine))
			goto not_ready;

	return gen6_reset_engines(dev_priv, engine_mask);

not_ready:
	for_each_engine_masked(engine, dev_priv, engine_mask)
		gen8_unrequest_engine_reset(engine);

	return -EIO;
}

typedef int (*reset_func)(struct drm_i915_private *, unsigned engine_mask);

static reset_func intel_get_gpu_reset(struct drm_i915_private *dev_priv)
{
	if (!i915.reset)
		return NULL;

	if (INTEL_INFO(dev_priv)->gen >= 8)
		return gen8_reset_engines;
	else if (INTEL_INFO(dev_priv)->gen >= 6)
		return gen6_reset_engines;
	else if (IS_GEN5(dev_priv))
		return ironlake_do_reset;
	else if (IS_G4X(dev_priv))
		return g4x_do_reset;
	else if (IS_G33(dev_priv))
		return g33_do_reset;
	else if (INTEL_INFO(dev_priv)->gen >= 3)
		return i915_do_reset;
	else
		return NULL;
}

int intel_gpu_reset(struct drm_i915_private *dev_priv, unsigned engine_mask)
{
	reset_func reset;
	int ret;

	reset = intel_get_gpu_reset(dev_priv);
	if (reset == NULL)
		return -ENODEV;

	/* If the power well sleeps during the reset, the reset
	 * request may be dropped and never completes (causing -EIO).
	 */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);
	ret = reset(dev_priv, engine_mask);
	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

	return ret;
}

bool intel_has_gpu_reset(struct drm_i915_private *dev_priv)
{
	return intel_get_gpu_reset(dev_priv) != NULL;
}

int intel_guc_reset(struct drm_i915_private *dev_priv)
{
	int ret;
	unsigned long irqflags;

	if (!HAS_GUC(dev_priv))
		return -EINVAL;

	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	ret = gen6_hw_domain_reset(dev_priv, GEN9_GRDOM_GUC);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

	return ret;
}

bool intel_uncore_unclaimed_mmio(struct drm_i915_private *dev_priv)
{
	return check_for_unclaimed_mmio(dev_priv);
}

bool
intel_uncore_arm_unclaimed_mmio_detection(struct drm_i915_private *dev_priv)
{
	if (unlikely(i915.mmio_debug ||
		     dev_priv->uncore.unclaimed_mmio_check <= 0))
		return false;

	if (unlikely(intel_uncore_unclaimed_mmio(dev_priv))) {
		DRM_DEBUG("Unclaimed register detected, "
			  "enabling oneshot unclaimed register reporting. "
			  "Please use i915.mmio_debug=N for more information.\n");
		i915.mmio_debug++;
		dev_priv->uncore.unclaimed_mmio_check--;
		return true;
	}

	return false;
}

static enum forcewake_domains
intel_uncore_forcewake_for_read(struct drm_i915_private *dev_priv,
				i915_reg_t reg)
{
	enum forcewake_domains fw_domains;

	if (intel_vgpu_active(dev_priv))
		return 0;

	switch (INTEL_GEN(dev_priv)) {
	case 9:
		fw_domains = __gen9_reg_read_fw_domains(i915_mmio_reg_offset(reg));
		break;
	case 8:
		if (IS_CHERRYVIEW(dev_priv))
			fw_domains = __chv_reg_read_fw_domains(i915_mmio_reg_offset(reg));
		else
			fw_domains = __gen6_reg_read_fw_domains(i915_mmio_reg_offset(reg));
		break;
	case 7:
	case 6:
		if (IS_VALLEYVIEW(dev_priv))
			fw_domains = __vlv_reg_read_fw_domains(i915_mmio_reg_offset(reg));
		else
			fw_domains = __gen6_reg_read_fw_domains(i915_mmio_reg_offset(reg));
		break;
	default:
		MISSING_CASE(INTEL_INFO(dev_priv)->gen);
	case 5: /* forcewake was introduced with gen6 */
	case 4:
	case 3:
	case 2:
		return 0;
	}

	WARN_ON(fw_domains & ~dev_priv->uncore.fw_domains);

	return fw_domains;
}

static enum forcewake_domains
intel_uncore_forcewake_for_write(struct drm_i915_private *dev_priv,
				 i915_reg_t reg)
{
	enum forcewake_domains fw_domains;

	if (intel_vgpu_active(dev_priv))
		return 0;

	switch (INTEL_GEN(dev_priv)) {
	case 9:
		fw_domains = __gen9_reg_write_fw_domains(i915_mmio_reg_offset(reg));
		break;
	case 8:
		if (IS_CHERRYVIEW(dev_priv))
			fw_domains = __chv_reg_write_fw_domains(i915_mmio_reg_offset(reg));
		else
			fw_domains = __gen8_reg_write_fw_domains(i915_mmio_reg_offset(reg));
		break;
	case 7:
	case 6:
		fw_domains = FORCEWAKE_RENDER;
		break;
	default:
		MISSING_CASE(INTEL_INFO(dev_priv)->gen);
	case 5:
	case 4:
	case 3:
	case 2:
		return 0;
	}

	WARN_ON(fw_domains & ~dev_priv->uncore.fw_domains);

	return fw_domains;
}

/**
 * intel_uncore_forcewake_for_reg - which forcewake domains are needed to access
 * 				    a register
 * @dev_priv: pointer to struct drm_i915_private
 * @reg: register in question
 * @op: operation bitmask of FW_REG_READ and/or FW_REG_WRITE
 *
 * Returns a set of forcewake domains required to be taken with for example
 * intel_uncore_forcewake_get for the specified register to be accessible in the
 * specified mode (read, write or read/write) with raw mmio accessors.
 *
 * NOTE: On Gen6 and Gen7 write forcewake domain (FORCEWAKE_RENDER) requires the
 * callers to do FIFO management on their own or risk losing writes.
 */
enum forcewake_domains
intel_uncore_forcewake_for_reg(struct drm_i915_private *dev_priv,
			       i915_reg_t reg, unsigned int op)
{
	enum forcewake_domains fw_domains = 0;

	WARN_ON(!op);

	if (op & FW_REG_READ)
		fw_domains = intel_uncore_forcewake_for_read(dev_priv, reg);

	if (op & FW_REG_WRITE)
		fw_domains |= intel_uncore_forcewake_for_write(dev_priv, reg);

	return fw_domains;
}
