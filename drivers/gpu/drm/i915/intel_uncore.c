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

#include <asm/iosf_mbi.h>
#include <linux/pm_runtime.h>

#define FORCEWAKE_ACK_TIMEOUT_MS 50
#define GT_FIFO_TIMEOUT_MS	 10

#define __raw_posting_read(dev_priv__, reg__) (void)__raw_i915_read32((dev_priv__), (reg__))

static const char * const forcewake_domain_names[] = {
	"render",
	"blitter",
	"media",
	"vdbox0",
	"vdbox1",
	"vdbox2",
	"vdbox3",
	"vebox0",
	"vebox1",
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
fw_domain_reset(struct drm_i915_private *i915,
		const struct intel_uncore_forcewake_domain *d)
{
	__raw_i915_write32(i915, d->reg_set, i915->uncore.fw_reset);
}

static inline void
fw_domain_arm_timer(struct intel_uncore_forcewake_domain *d)
{
	d->wake_count++;
	hrtimer_start_range_ns(&d->timer,
			       NSEC_PER_MSEC,
			       NSEC_PER_MSEC,
			       HRTIMER_MODE_REL);
}

static inline int
__wait_for_ack(const struct drm_i915_private *i915,
	       const struct intel_uncore_forcewake_domain *d,
	       const u32 ack,
	       const u32 value)
{
	return wait_for_atomic((__raw_i915_read32(i915, d->reg_ack) & ack) == value,
			       FORCEWAKE_ACK_TIMEOUT_MS);
}

static inline int
wait_ack_clear(const struct drm_i915_private *i915,
	       const struct intel_uncore_forcewake_domain *d,
	       const u32 ack)
{
	return __wait_for_ack(i915, d, ack, 0);
}

static inline int
wait_ack_set(const struct drm_i915_private *i915,
	     const struct intel_uncore_forcewake_domain *d,
	     const u32 ack)
{
	return __wait_for_ack(i915, d, ack, ack);
}

static inline void
fw_domain_wait_ack_clear(const struct drm_i915_private *i915,
			 const struct intel_uncore_forcewake_domain *d)
{
	if (wait_ack_clear(i915, d, FORCEWAKE_KERNEL))
		DRM_ERROR("%s: timed out waiting for forcewake ack to clear.\n",
			  intel_uncore_forcewake_domain_to_str(d->id));
}

enum ack_type {
	ACK_CLEAR = 0,
	ACK_SET
};

static int
fw_domain_wait_ack_with_fallback(const struct drm_i915_private *i915,
				 const struct intel_uncore_forcewake_domain *d,
				 const enum ack_type type)
{
	const u32 ack_bit = FORCEWAKE_KERNEL;
	const u32 value = type == ACK_SET ? ack_bit : 0;
	unsigned int pass;
	bool ack_detected;

	/*
	 * There is a possibility of driver's wake request colliding
	 * with hardware's own wake requests and that can cause
	 * hardware to not deliver the driver's ack message.
	 *
	 * Use a fallback bit toggle to kick the gpu state machine
	 * in the hope that the original ack will be delivered along with
	 * the fallback ack.
	 *
	 * This workaround is described in HSDES #1604254524
	 */

	pass = 1;
	do {
		wait_ack_clear(i915, d, FORCEWAKE_KERNEL_FALLBACK);

		__raw_i915_write32(i915, d->reg_set,
				   _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL_FALLBACK));
		/* Give gt some time to relax before the polling frenzy */
		udelay(10 * pass);
		wait_ack_set(i915, d, FORCEWAKE_KERNEL_FALLBACK);

		ack_detected = (__raw_i915_read32(i915, d->reg_ack) & ack_bit) == value;

		__raw_i915_write32(i915, d->reg_set,
				   _MASKED_BIT_DISABLE(FORCEWAKE_KERNEL_FALLBACK));
	} while (!ack_detected && pass++ < 10);

	DRM_DEBUG_DRIVER("%s had to use fallback to %s ack, 0x%x (passes %u)\n",
			 intel_uncore_forcewake_domain_to_str(d->id),
			 type == ACK_SET ? "set" : "clear",
			 __raw_i915_read32(i915, d->reg_ack),
			 pass);

	return ack_detected ? 0 : -ETIMEDOUT;
}

static inline void
fw_domain_wait_ack_clear_fallback(const struct drm_i915_private *i915,
				  const struct intel_uncore_forcewake_domain *d)
{
	if (likely(!wait_ack_clear(i915, d, FORCEWAKE_KERNEL)))
		return;

	if (fw_domain_wait_ack_with_fallback(i915, d, ACK_CLEAR))
		fw_domain_wait_ack_clear(i915, d);
}

static inline void
fw_domain_get(struct drm_i915_private *i915,
	      const struct intel_uncore_forcewake_domain *d)
{
	__raw_i915_write32(i915, d->reg_set, i915->uncore.fw_set);
}

static inline void
fw_domain_wait_ack_set(const struct drm_i915_private *i915,
		       const struct intel_uncore_forcewake_domain *d)
{
	if (wait_ack_set(i915, d, FORCEWAKE_KERNEL))
		DRM_ERROR("%s: timed out waiting for forcewake ack request.\n",
			  intel_uncore_forcewake_domain_to_str(d->id));
}

static inline void
fw_domain_wait_ack_set_fallback(const struct drm_i915_private *i915,
				const struct intel_uncore_forcewake_domain *d)
{
	if (likely(!wait_ack_set(i915, d, FORCEWAKE_KERNEL)))
		return;

	if (fw_domain_wait_ack_with_fallback(i915, d, ACK_SET))
		fw_domain_wait_ack_set(i915, d);
}

static inline void
fw_domain_put(const struct drm_i915_private *i915,
	      const struct intel_uncore_forcewake_domain *d)
{
	__raw_i915_write32(i915, d->reg_set, i915->uncore.fw_clear);
}

static void
fw_domains_get(struct drm_i915_private *i915, enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;
	unsigned int tmp;

	GEM_BUG_ON(fw_domains & ~i915->uncore.fw_domains);

	for_each_fw_domain_masked(d, fw_domains, i915, tmp) {
		fw_domain_wait_ack_clear(i915, d);
		fw_domain_get(i915, d);
	}

	for_each_fw_domain_masked(d, fw_domains, i915, tmp)
		fw_domain_wait_ack_set(i915, d);

	i915->uncore.fw_domains_active |= fw_domains;
}

static void
fw_domains_get_with_fallback(struct drm_i915_private *i915,
			     enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;
	unsigned int tmp;

	GEM_BUG_ON(fw_domains & ~i915->uncore.fw_domains);

	for_each_fw_domain_masked(d, fw_domains, i915, tmp) {
		fw_domain_wait_ack_clear_fallback(i915, d);
		fw_domain_get(i915, d);
	}

	for_each_fw_domain_masked(d, fw_domains, i915, tmp)
		fw_domain_wait_ack_set_fallback(i915, d);

	i915->uncore.fw_domains_active |= fw_domains;
}

static void
fw_domains_put(struct drm_i915_private *i915, enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;
	unsigned int tmp;

	GEM_BUG_ON(fw_domains & ~i915->uncore.fw_domains);

	for_each_fw_domain_masked(d, fw_domains, i915, tmp)
		fw_domain_put(i915, d);

	i915->uncore.fw_domains_active &= ~fw_domains;
}

static void
fw_domains_reset(struct drm_i915_private *i915,
		 enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;
	unsigned int tmp;

	if (!fw_domains)
		return;

	GEM_BUG_ON(fw_domains & ~i915->uncore.fw_domains);

	for_each_fw_domain_masked(d, fw_domains, i915, tmp)
		fw_domain_reset(i915, d);
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

static inline u32 fifo_free_entries(struct drm_i915_private *dev_priv)
{
	u32 count = __raw_i915_read32(dev_priv, GTFIFOCTL);

	return count & GT_FIFO_FREE_ENTRIES_MASK;
}

static void __gen6_gt_wait_for_fifo(struct drm_i915_private *dev_priv)
{
	u32 n;

	/* On VLV, FIFO will be shared by both SW and HW.
	 * So, we need to read the FREE_ENTRIES everytime */
	if (IS_VALLEYVIEW(dev_priv))
		n = fifo_free_entries(dev_priv);
	else
		n = dev_priv->uncore.fifo_count;

	if (n <= GT_FIFO_NUM_RESERVED_ENTRIES) {
		if (wait_for_atomic((n = fifo_free_entries(dev_priv)) >
				    GT_FIFO_NUM_RESERVED_ENTRIES,
				    GT_FIFO_TIMEOUT_MS)) {
			DRM_DEBUG("GT_FIFO timeout, entries: %u\n", n);
			return;
		}
	}

	dev_priv->uncore.fifo_count = n - 1;
}

static enum hrtimer_restart
intel_uncore_fw_release_timer(struct hrtimer *timer)
{
	struct intel_uncore_forcewake_domain *domain =
	       container_of(timer, struct intel_uncore_forcewake_domain, timer);
	struct drm_i915_private *dev_priv =
		container_of(domain, struct drm_i915_private, uncore.fw_domain[domain->id]);
	unsigned long irqflags;

	assert_rpm_device_not_suspended(dev_priv);

	if (xchg(&domain->active, false))
		return HRTIMER_RESTART;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	if (WARN_ON(domain->wake_count == 0))
		domain->wake_count++;

	if (--domain->wake_count == 0)
		dev_priv->uncore.funcs.force_wake_put(dev_priv, domain->mask);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);

	return HRTIMER_NORESTART;
}

/* Note callers must have acquired the PUNIT->PMIC bus, before calling this. */
static void intel_uncore_forcewake_reset(struct drm_i915_private *dev_priv,
					 bool restore)
{
	unsigned long irqflags;
	struct intel_uncore_forcewake_domain *domain;
	int retry_count = 100;
	enum forcewake_domains fw, active_domains;

	iosf_mbi_assert_punit_acquired();

	/* Hold uncore.lock across reset to prevent any register access
	 * with forcewake not set correctly. Wait until all pending
	 * timers are run before holding.
	 */
	while (1) {
		unsigned int tmp;

		active_domains = 0;

		for_each_fw_domain(domain, dev_priv, tmp) {
			smp_store_mb(domain->active, false);
			if (hrtimer_cancel(&domain->timer) == 0)
				continue;

			intel_uncore_fw_release_timer(&domain->timer);
		}

		spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

		for_each_fw_domain(domain, dev_priv, tmp) {
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

	fw = dev_priv->uncore.fw_domains_active;
	if (fw)
		dev_priv->uncore.funcs.force_wake_put(dev_priv, fw);

	fw_domains_reset(dev_priv, dev_priv->uncore.fw_domains);

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
gen6_check_for_fifo_debug(struct drm_i915_private *dev_priv)
{
	u32 fifodbg;

	fifodbg = __raw_i915_read32(dev_priv, GTFIFODBG);

	if (unlikely(fifodbg)) {
		DRM_DEBUG_DRIVER("GTFIFODBG = 0x08%x\n", fifodbg);
		__raw_i915_write32(dev_priv, GTFIFODBG, fifodbg);
	}

	return fifodbg;
}

static bool
check_for_unclaimed_mmio(struct drm_i915_private *dev_priv)
{
	bool ret = false;

	if (HAS_FPGA_DBG_UNCLAIMED(dev_priv))
		ret |= fpga_check_for_unclaimed_mmio(dev_priv);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		ret |= vlv_check_for_unclaimed_mmio(dev_priv);

	if (IS_GEN6(dev_priv) || IS_GEN7(dev_priv))
		ret |= gen6_check_for_fifo_debug(dev_priv);

	return ret;
}

static void __intel_uncore_early_sanitize(struct drm_i915_private *dev_priv,
					  bool restore_forcewake)
{
	/* clear out unclaimed reg detection bit */
	if (check_for_unclaimed_mmio(dev_priv))
		DRM_DEBUG("unclaimed mmio detected on uncore init, clearing\n");

	/* WaDisableShadowRegForCpd:chv */
	if (IS_CHERRYVIEW(dev_priv)) {
		__raw_i915_write32(dev_priv, GTFIFOCTL,
				   __raw_i915_read32(dev_priv, GTFIFOCTL) |
				   GT_FIFO_CTL_BLOCK_ALL_POLICY_STALL |
				   GT_FIFO_CTL_RC6_POLICY_STALL);
	}

	iosf_mbi_punit_acquire();
	intel_uncore_forcewake_reset(dev_priv, restore_forcewake);
	iosf_mbi_punit_release();
}

void intel_uncore_suspend(struct drm_i915_private *dev_priv)
{
	iosf_mbi_punit_acquire();
	iosf_mbi_unregister_pmic_bus_access_notifier_unlocked(
		&dev_priv->uncore.pmic_bus_access_nb);
	intel_uncore_forcewake_reset(dev_priv, false);
	iosf_mbi_punit_release();
}

void intel_uncore_resume_early(struct drm_i915_private *dev_priv)
{
	__intel_uncore_early_sanitize(dev_priv, true);
	iosf_mbi_register_pmic_bus_access_notifier(
		&dev_priv->uncore.pmic_bus_access_nb);
	i915_check_and_clear_faults(dev_priv);
}

void intel_uncore_runtime_resume(struct drm_i915_private *dev_priv)
{
	iosf_mbi_register_pmic_bus_access_notifier(
		&dev_priv->uncore.pmic_bus_access_nb);
}

void intel_uncore_sanitize(struct drm_i915_private *dev_priv)
{
	/* BIOS often leaves RC6 enabled, but disable it for hw init */
	intel_sanitize_gt_powersave(dev_priv);
}

static void __intel_uncore_forcewake_get(struct drm_i915_private *dev_priv,
					 enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;
	unsigned int tmp;

	fw_domains &= dev_priv->uncore.fw_domains;

	for_each_fw_domain_masked(domain, fw_domains, dev_priv, tmp) {
		if (domain->wake_count++) {
			fw_domains &= ~domain->mask;
			domain->active = true;
		}
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
 * intel_uncore_forcewake_user_get - claim forcewake on behalf of userspace
 * @dev_priv: i915 device instance
 *
 * This function is a wrapper around intel_uncore_forcewake_get() to acquire
 * the GT powerwell and in the process disable our debugging for the
 * duration of userspace's bypass.
 */
void intel_uncore_forcewake_user_get(struct drm_i915_private *dev_priv)
{
	spin_lock_irq(&dev_priv->uncore.lock);
	if (!dev_priv->uncore.user_forcewake.count++) {
		intel_uncore_forcewake_get__locked(dev_priv, FORCEWAKE_ALL);

		/* Save and disable mmio debugging for the user bypass */
		dev_priv->uncore.user_forcewake.saved_mmio_check =
			dev_priv->uncore.unclaimed_mmio_check;
		dev_priv->uncore.user_forcewake.saved_mmio_debug =
			i915_modparams.mmio_debug;

		dev_priv->uncore.unclaimed_mmio_check = 0;
		i915_modparams.mmio_debug = 0;
	}
	spin_unlock_irq(&dev_priv->uncore.lock);
}

/**
 * intel_uncore_forcewake_user_put - release forcewake on behalf of userspace
 * @dev_priv: i915 device instance
 *
 * This function complements intel_uncore_forcewake_user_get() and releases
 * the GT powerwell taken on behalf of the userspace bypass.
 */
void intel_uncore_forcewake_user_put(struct drm_i915_private *dev_priv)
{
	spin_lock_irq(&dev_priv->uncore.lock);
	if (!--dev_priv->uncore.user_forcewake.count) {
		if (intel_uncore_unclaimed_mmio(dev_priv))
			dev_info(dev_priv->drm.dev,
				 "Invalid mmio detected during user access\n");

		dev_priv->uncore.unclaimed_mmio_check =
			dev_priv->uncore.user_forcewake.saved_mmio_check;
		i915_modparams.mmio_debug =
			dev_priv->uncore.user_forcewake.saved_mmio_debug;

		intel_uncore_forcewake_put__locked(dev_priv, FORCEWAKE_ALL);
	}
	spin_unlock_irq(&dev_priv->uncore.lock);
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
	lockdep_assert_held(&dev_priv->uncore.lock);

	if (!dev_priv->uncore.funcs.force_wake_get)
		return;

	__intel_uncore_forcewake_get(dev_priv, fw_domains);
}

static void __intel_uncore_forcewake_put(struct drm_i915_private *dev_priv,
					 enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;
	unsigned int tmp;

	fw_domains &= dev_priv->uncore.fw_domains;

	for_each_fw_domain_masked(domain, fw_domains, dev_priv, tmp) {
		if (WARN_ON(domain->wake_count == 0))
			continue;

		if (--domain->wake_count) {
			domain->active = true;
			continue;
		}

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
	lockdep_assert_held(&dev_priv->uncore.lock);

	if (!dev_priv->uncore.funcs.force_wake_put)
		return;

	__intel_uncore_forcewake_put(dev_priv, fw_domains);
}

void assert_forcewakes_inactive(struct drm_i915_private *dev_priv)
{
	if (!dev_priv->uncore.funcs.force_wake_get)
		return;

	WARN(dev_priv->uncore.fw_domains_active,
	     "Expected all fw_domains to be inactive, but %08x are still on\n",
	     dev_priv->uncore.fw_domains_active);
}

void assert_forcewakes_active(struct drm_i915_private *dev_priv,
			      enum forcewake_domains fw_domains)
{
	if (!dev_priv->uncore.funcs.force_wake_get)
		return;

	assert_rpm_wakelock_held(dev_priv);

	fw_domains &= dev_priv->uncore.fw_domains;
	WARN(fw_domains & ~dev_priv->uncore.fw_domains_active,
	     "Expected %08x fw_domains to be active, but %08x are off\n",
	     fw_domains, fw_domains & ~dev_priv->uncore.fw_domains_active);
}

/* We give fast paths for the really cool registers */
#define NEEDS_FORCE_WAKE(reg) ((reg) < 0x40000)

#define GEN11_NEEDS_FORCE_WAKE(reg) \
	((reg) < 0x40000 || ((reg) >= 0x1c0000 && (reg) < 0x1dc000))

#define __gen6_reg_read_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd; \
	if (NEEDS_FORCE_WAKE(offset)) \
		__fwd = FORCEWAKE_RENDER; \
	else \
		__fwd = 0; \
	__fwd; \
})

static int fw_range_cmp(u32 offset, const struct intel_forcewake_range *entry)
{
	if (offset < entry->start)
		return -1;
	else if (offset > entry->end)
		return 1;
	else
		return 0;
}

/* Copied and "macroized" from lib/bsearch.c */
#define BSEARCH(key, base, num, cmp) ({                                 \
	unsigned int start__ = 0, end__ = (num);                        \
	typeof(base) result__ = NULL;                                   \
	while (start__ < end__) {                                       \
		unsigned int mid__ = start__ + (end__ - start__) / 2;   \
		int ret__ = (cmp)((key), (base) + mid__);               \
		if (ret__ < 0) {                                        \
			end__ = mid__;                                  \
		} else if (ret__ > 0) {                                 \
			start__ = mid__ + 1;                            \
		} else {                                                \
			result__ = (base) + mid__;                      \
			break;                                          \
		}                                                       \
	}                                                               \
	result__;                                                       \
})

static enum forcewake_domains
find_fw_domain(struct drm_i915_private *dev_priv, u32 offset)
{
	const struct intel_forcewake_range *entry;

	entry = BSEARCH(offset,
			dev_priv->uncore.fw_domains_table,
			dev_priv->uncore.fw_domains_table_entries,
			fw_range_cmp);

	if (!entry)
		return 0;

	/*
	 * The list of FW domains depends on the SKU in gen11+ so we
	 * can't determine it statically. We use FORCEWAKE_ALL and
	 * translate it here to the list of available domains.
	 */
	if (entry->domains == FORCEWAKE_ALL)
		return dev_priv->uncore.fw_domains;

	WARN(entry->domains & ~dev_priv->uncore.fw_domains,
	     "Uninitialized forcewake domain(s) 0x%x accessed at 0x%x\n",
	     entry->domains & ~dev_priv->uncore.fw_domains, offset);

	return entry->domains;
}

#define GEN_FW_RANGE(s, e, d) \
	{ .start = (s), .end = (e), .domains = (d) }

#define HAS_FWTABLE(dev_priv) \
	(INTEL_GEN(dev_priv) >= 9 || \
	 IS_CHERRYVIEW(dev_priv) || \
	 IS_VALLEYVIEW(dev_priv))

/* *Must* be sorted by offset ranges! See intel_fw_table_check(). */
static const struct intel_forcewake_range __vlv_fw_ranges[] = {
	GEN_FW_RANGE(0x2000, 0x3fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x5000, 0x7fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xb000, 0x11fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x12000, 0x13fff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x22000, 0x23fff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x2e000, 0x2ffff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x30000, 0x3ffff, FORCEWAKE_MEDIA),
};

#define __fwtable_reg_read_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	if (NEEDS_FORCE_WAKE((offset))) \
		__fwd = find_fw_domain(dev_priv, offset); \
	__fwd; \
})

#define __gen11_fwtable_reg_read_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	if (GEN11_NEEDS_FORCE_WAKE((offset))) \
		__fwd = find_fw_domain(dev_priv, offset); \
	__fwd; \
})

/* *Must* be sorted by offset! See intel_shadow_table_check(). */
static const i915_reg_t gen8_shadowed_regs[] = {
	RING_TAIL(RENDER_RING_BASE),	/* 0x2000 (base) */
	GEN6_RPNSWREQ,			/* 0xA008 */
	GEN6_RC_VIDEO_FREQ,		/* 0xA00C */
	RING_TAIL(GEN6_BSD_RING_BASE),	/* 0x12000 (base) */
	RING_TAIL(VEBOX_RING_BASE),	/* 0x1a000 (base) */
	RING_TAIL(BLT_RING_BASE),	/* 0x22000 (base) */
	/* TODO: Other registers are not yet used */
};

static const i915_reg_t gen11_shadowed_regs[] = {
	RING_TAIL(RENDER_RING_BASE),		/* 0x2000 (base) */
	GEN6_RPNSWREQ,				/* 0xA008 */
	GEN6_RC_VIDEO_FREQ,			/* 0xA00C */
	RING_TAIL(BLT_RING_BASE),		/* 0x22000 (base) */
	RING_TAIL(GEN11_BSD_RING_BASE),		/* 0x1C0000 (base) */
	RING_TAIL(GEN11_BSD2_RING_BASE),	/* 0x1C4000 (base) */
	RING_TAIL(GEN11_VEBOX_RING_BASE),	/* 0x1C8000 (base) */
	RING_TAIL(GEN11_BSD3_RING_BASE),	/* 0x1D0000 (base) */
	RING_TAIL(GEN11_BSD4_RING_BASE),	/* 0x1D4000 (base) */
	RING_TAIL(GEN11_VEBOX2_RING_BASE),	/* 0x1D8000 (base) */
	/* TODO: Other registers are not yet used */
};

static int mmio_reg_cmp(u32 key, const i915_reg_t *reg)
{
	u32 offset = i915_mmio_reg_offset(*reg);

	if (key < offset)
		return -1;
	else if (key > offset)
		return 1;
	else
		return 0;
}

#define __is_genX_shadowed(x) \
static bool is_gen##x##_shadowed(u32 offset) \
{ \
	const i915_reg_t *regs = gen##x##_shadowed_regs; \
	return BSEARCH(offset, regs, ARRAY_SIZE(gen##x##_shadowed_regs), \
		       mmio_reg_cmp); \
}

__is_genX_shadowed(8)
__is_genX_shadowed(11)

#define __gen8_reg_write_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd; \
	if (NEEDS_FORCE_WAKE(offset) && !is_gen8_shadowed(offset)) \
		__fwd = FORCEWAKE_RENDER; \
	else \
		__fwd = 0; \
	__fwd; \
})

/* *Must* be sorted by offset ranges! See intel_fw_table_check(). */
static const struct intel_forcewake_range __chv_fw_ranges[] = {
	GEN_FW_RANGE(0x2000, 0x3fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x4000, 0x4fff, FORCEWAKE_RENDER | FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x5200, 0x7fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8000, 0x82ff, FORCEWAKE_RENDER | FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x8300, 0x84ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8500, 0x85ff, FORCEWAKE_RENDER | FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x8800, 0x88ff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x9000, 0xafff, FORCEWAKE_RENDER | FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0xb000, 0xb47f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xd000, 0xd7ff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0xe000, 0xe7ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xf000, 0xffff, FORCEWAKE_RENDER | FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x12000, 0x13fff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x1a000, 0x1bfff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x1e800, 0x1e9ff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x30000, 0x37fff, FORCEWAKE_MEDIA),
};

#define __fwtable_reg_write_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	if (NEEDS_FORCE_WAKE((offset)) && !is_gen8_shadowed(offset)) \
		__fwd = find_fw_domain(dev_priv, offset); \
	__fwd; \
})

#define __gen11_fwtable_reg_write_fw_domains(offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	if (GEN11_NEEDS_FORCE_WAKE((offset)) && !is_gen11_shadowed(offset)) \
		__fwd = find_fw_domain(dev_priv, offset); \
	__fwd; \
})

/* *Must* be sorted by offset ranges! See intel_fw_table_check(). */
static const struct intel_forcewake_range __gen9_fw_ranges[] = {
	GEN_FW_RANGE(0x0, 0xaff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0xb00, 0x1fff, 0), /* uncore range */
	GEN_FW_RANGE(0x2000, 0x26ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x2700, 0x2fff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x3000, 0x3fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x4000, 0x51ff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x5200, 0x7fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8000, 0x812f, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x8130, 0x813f, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x8140, 0x815f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8160, 0x82ff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x8300, 0x84ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8500, 0x87ff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x8800, 0x89ff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x8a00, 0x8bff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x8c00, 0x8cff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8d00, 0x93ff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x9400, 0x97ff, FORCEWAKE_RENDER | FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x9800, 0xafff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0xb000, 0xb47f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xb480, 0xcfff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0xd000, 0xd7ff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0xd800, 0xdfff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0xe000, 0xe8ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xe900, 0x11fff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x12000, 0x13fff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x14000, 0x19fff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x1a000, 0x1e9ff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x1ea00, 0x243ff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x24400, 0x247ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x24800, 0x2ffff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x30000, 0x3ffff, FORCEWAKE_MEDIA),
};

/* *Must* be sorted by offset ranges! See intel_fw_table_check(). */
static const struct intel_forcewake_range __gen11_fw_ranges[] = {
	GEN_FW_RANGE(0x0, 0xaff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0xb00, 0x1fff, 0), /* uncore range */
	GEN_FW_RANGE(0x2000, 0x26ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x2700, 0x2fff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x3000, 0x3fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x4000, 0x51ff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x5200, 0x7fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8000, 0x813f, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x8140, 0x815f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8160, 0x82ff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x8300, 0x84ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8500, 0x8bff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x8c00, 0x8cff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8d00, 0x93ff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x9400, 0x97ff, FORCEWAKE_ALL),
	GEN_FW_RANGE(0x9800, 0xafff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0xb000, 0xb47f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xb480, 0xdfff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0xe000, 0xe8ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xe900, 0x243ff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x24400, 0x247ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x24800, 0x3ffff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x40000, 0x1bffff, 0),
	GEN_FW_RANGE(0x1c0000, 0x1c3fff, FORCEWAKE_MEDIA_VDBOX0),
	GEN_FW_RANGE(0x1c4000, 0x1c7fff, FORCEWAKE_MEDIA_VDBOX1),
	GEN_FW_RANGE(0x1c8000, 0x1cbfff, FORCEWAKE_MEDIA_VEBOX0),
	GEN_FW_RANGE(0x1cc000, 0x1cffff, FORCEWAKE_BLITTER),
	GEN_FW_RANGE(0x1d0000, 0x1d3fff, FORCEWAKE_MEDIA_VDBOX2),
	GEN_FW_RANGE(0x1d4000, 0x1d7fff, FORCEWAKE_MEDIA_VDBOX3),
	GEN_FW_RANGE(0x1d8000, 0x1dbfff, FORCEWAKE_MEDIA_VEBOX1)
};

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
	if (WARN(check_for_unclaimed_mmio(dev_priv) && !before,
		 "Unclaimed %s register 0x%x\n",
		 read ? "read from" : "write to",
		 i915_mmio_reg_offset(reg)))
		/* Only report the first N failures */
		i915_modparams.mmio_debug--;
}

static inline void
unclaimed_reg_debug(struct drm_i915_private *dev_priv,
		    const i915_reg_t reg,
		    const bool read,
		    const bool before)
{
	if (likely(!i915_modparams.mmio_debug))
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

static noinline void ___force_wake_auto(struct drm_i915_private *dev_priv,
					enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;
	unsigned int tmp;

	GEM_BUG_ON(fw_domains & ~dev_priv->uncore.fw_domains);

	for_each_fw_domain_masked(domain, fw_domains, dev_priv, tmp)
		fw_domain_arm_timer(domain);

	dev_priv->uncore.funcs.force_wake_get(dev_priv, fw_domains);
}

static inline void __force_wake_auto(struct drm_i915_private *dev_priv,
				     enum forcewake_domains fw_domains)
{
	if (WARN_ON(!fw_domains))
		return;

	/* Turn on all requested but inactive supported forcewake domains. */
	fw_domains &= dev_priv->uncore.fw_domains;
	fw_domains &= ~dev_priv->uncore.fw_domains_active;

	if (fw_domains)
		___force_wake_auto(dev_priv, fw_domains);
}

#define __gen_read(func, x) \
static u##x \
func##_read##x(struct drm_i915_private *dev_priv, i915_reg_t reg, bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_READ_HEADER(x); \
	fw_engine = __##func##_reg_read_fw_domains(offset); \
	if (fw_engine) \
		__force_wake_auto(dev_priv, fw_engine); \
	val = __raw_i915_read##x(dev_priv, reg); \
	GEN6_READ_FOOTER; \
}
#define __gen6_read(x) __gen_read(gen6, x)
#define __fwtable_read(x) __gen_read(fwtable, x)
#define __gen11_fwtable_read(x) __gen_read(gen11_fwtable, x)

__gen11_fwtable_read(8)
__gen11_fwtable_read(16)
__gen11_fwtable_read(32)
__gen11_fwtable_read(64)
__fwtable_read(8)
__fwtable_read(16)
__fwtable_read(32)
__fwtable_read(64)
__gen6_read(8)
__gen6_read(16)
__gen6_read(32)
__gen6_read(64)

#undef __gen11_fwtable_read
#undef __fwtable_read
#undef __gen6_read
#undef GEN6_READ_FOOTER
#undef GEN6_READ_HEADER

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
__gen2_write(8)
__gen2_write(16)
__gen2_write(32)

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
	GEN6_WRITE_HEADER; \
	if (NEEDS_FORCE_WAKE(offset)) \
		__gen6_gt_wait_for_fifo(dev_priv); \
	__raw_i915_write##x(dev_priv, reg, val); \
	GEN6_WRITE_FOOTER; \
}

#define __gen_write(func, x) \
static void \
func##_write##x(struct drm_i915_private *dev_priv, i915_reg_t reg, u##x val, bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_WRITE_HEADER; \
	fw_engine = __##func##_reg_write_fw_domains(offset); \
	if (fw_engine) \
		__force_wake_auto(dev_priv, fw_engine); \
	__raw_i915_write##x(dev_priv, reg, val); \
	GEN6_WRITE_FOOTER; \
}
#define __gen8_write(x) __gen_write(gen8, x)
#define __fwtable_write(x) __gen_write(fwtable, x)
#define __gen11_fwtable_write(x) __gen_write(gen11_fwtable, x)

__gen11_fwtable_write(8)
__gen11_fwtable_write(16)
__gen11_fwtable_write(32)
__fwtable_write(8)
__fwtable_write(16)
__fwtable_write(32)
__gen8_write(8)
__gen8_write(16)
__gen8_write(32)
__gen6_write(8)
__gen6_write(16)
__gen6_write(32)

#undef __gen11_fwtable_write
#undef __fwtable_write
#undef __gen8_write
#undef __gen6_write
#undef GEN6_WRITE_FOOTER
#undef GEN6_WRITE_HEADER

#define ASSIGN_WRITE_MMIO_VFUNCS(i915, x) \
do { \
	(i915)->uncore.funcs.mmio_writeb = x##_write8; \
	(i915)->uncore.funcs.mmio_writew = x##_write16; \
	(i915)->uncore.funcs.mmio_writel = x##_write32; \
} while (0)

#define ASSIGN_READ_MMIO_VFUNCS(i915, x) \
do { \
	(i915)->uncore.funcs.mmio_readb = x##_read8; \
	(i915)->uncore.funcs.mmio_readw = x##_read16; \
	(i915)->uncore.funcs.mmio_readl = x##_read32; \
	(i915)->uncore.funcs.mmio_readq = x##_read64; \
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

	WARN_ON(!i915_mmio_reg_valid(reg_set));
	WARN_ON(!i915_mmio_reg_valid(reg_ack));

	d->wake_count = 0;
	d->reg_set = reg_set;
	d->reg_ack = reg_ack;

	d->id = domain_id;

	BUILD_BUG_ON(FORCEWAKE_RENDER != (1 << FW_DOMAIN_ID_RENDER));
	BUILD_BUG_ON(FORCEWAKE_BLITTER != (1 << FW_DOMAIN_ID_BLITTER));
	BUILD_BUG_ON(FORCEWAKE_MEDIA != (1 << FW_DOMAIN_ID_MEDIA));
	BUILD_BUG_ON(FORCEWAKE_MEDIA_VDBOX0 != (1 << FW_DOMAIN_ID_MEDIA_VDBOX0));
	BUILD_BUG_ON(FORCEWAKE_MEDIA_VDBOX1 != (1 << FW_DOMAIN_ID_MEDIA_VDBOX1));
	BUILD_BUG_ON(FORCEWAKE_MEDIA_VDBOX2 != (1 << FW_DOMAIN_ID_MEDIA_VDBOX2));
	BUILD_BUG_ON(FORCEWAKE_MEDIA_VDBOX3 != (1 << FW_DOMAIN_ID_MEDIA_VDBOX3));
	BUILD_BUG_ON(FORCEWAKE_MEDIA_VEBOX0 != (1 << FW_DOMAIN_ID_MEDIA_VEBOX0));
	BUILD_BUG_ON(FORCEWAKE_MEDIA_VEBOX1 != (1 << FW_DOMAIN_ID_MEDIA_VEBOX1));


	d->mask = BIT(domain_id);

	hrtimer_init(&d->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	d->timer.function = intel_uncore_fw_release_timer;

	dev_priv->uncore.fw_domains |= BIT(domain_id);

	fw_domain_reset(dev_priv, d);
}

static void intel_uncore_fw_domains_init(struct drm_i915_private *dev_priv)
{
	if (INTEL_GEN(dev_priv) <= 5 || intel_vgpu_active(dev_priv))
		return;

	if (IS_GEN6(dev_priv)) {
		dev_priv->uncore.fw_reset = 0;
		dev_priv->uncore.fw_set = FORCEWAKE_KERNEL;
		dev_priv->uncore.fw_clear = 0;
	} else {
		/* WaRsClearFWBitsAtReset:bdw,skl */
		dev_priv->uncore.fw_reset = _MASKED_BIT_DISABLE(0xffff);
		dev_priv->uncore.fw_set = _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL);
		dev_priv->uncore.fw_clear = _MASKED_BIT_DISABLE(FORCEWAKE_KERNEL);
	}

	if (INTEL_GEN(dev_priv) >= 11) {
		int i;

		dev_priv->uncore.funcs.force_wake_get = fw_domains_get;
		dev_priv->uncore.funcs.force_wake_put = fw_domains_put;
		fw_domain_init(dev_priv, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE_RENDER_GEN9,
			       FORCEWAKE_ACK_RENDER_GEN9);
		fw_domain_init(dev_priv, FW_DOMAIN_ID_BLITTER,
			       FORCEWAKE_BLITTER_GEN9,
			       FORCEWAKE_ACK_BLITTER_GEN9);
		for (i = 0; i < I915_MAX_VCS; i++) {
			if (!HAS_ENGINE(dev_priv, _VCS(i)))
				continue;

			fw_domain_init(dev_priv, FW_DOMAIN_ID_MEDIA_VDBOX0 + i,
				       FORCEWAKE_MEDIA_VDBOX_GEN11(i),
				       FORCEWAKE_ACK_MEDIA_VDBOX_GEN11(i));
		}
		for (i = 0; i < I915_MAX_VECS; i++) {
			if (!HAS_ENGINE(dev_priv, _VECS(i)))
				continue;

			fw_domain_init(dev_priv, FW_DOMAIN_ID_MEDIA_VEBOX0 + i,
				       FORCEWAKE_MEDIA_VEBOX_GEN11(i),
				       FORCEWAKE_ACK_MEDIA_VEBOX_GEN11(i));
		}
	} else if (IS_GEN9(dev_priv) || IS_GEN10(dev_priv)) {
		dev_priv->uncore.funcs.force_wake_get =
			fw_domains_get_with_fallback;
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
		dev_priv->uncore.funcs.force_wake_put = fw_domains_put;
		fw_domain_init(dev_priv, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE_VLV, FORCEWAKE_ACK_VLV);
		fw_domain_init(dev_priv, FW_DOMAIN_ID_MEDIA,
			       FORCEWAKE_MEDIA_VLV, FORCEWAKE_ACK_MEDIA_VLV);
	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		dev_priv->uncore.funcs.force_wake_get =
			fw_domains_get_with_thread_status;
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
		dev_priv->uncore.funcs.force_wake_put = fw_domains_put;

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
		fw_domains_get_with_thread_status(dev_priv, FORCEWAKE_RENDER);
		ecobus = __raw_i915_read32(dev_priv, ECOBUS);
		fw_domains_put(dev_priv, FORCEWAKE_RENDER);
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
		dev_priv->uncore.funcs.force_wake_put = fw_domains_put;
		fw_domain_init(dev_priv, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE, FORCEWAKE_ACK);
	}

	/* All future platforms are expected to require complex power gating */
	WARN_ON(dev_priv->uncore.fw_domains == 0);
}

#define ASSIGN_FW_DOMAINS_TABLE(d) \
{ \
	dev_priv->uncore.fw_domains_table = \
			(struct intel_forcewake_range *)(d); \
	dev_priv->uncore.fw_domains_table_entries = ARRAY_SIZE((d)); \
}

static int i915_pmic_bus_access_notifier(struct notifier_block *nb,
					 unsigned long action, void *data)
{
	struct drm_i915_private *dev_priv = container_of(nb,
			struct drm_i915_private, uncore.pmic_bus_access_nb);

	switch (action) {
	case MBI_PMIC_BUS_ACCESS_BEGIN:
		/*
		 * forcewake all now to make sure that we don't need to do a
		 * forcewake later which on systems where this notifier gets
		 * called requires the punit to access to the shared pmic i2c
		 * bus, which will be busy after this notification, leading to:
		 * "render: timed out waiting for forcewake ack request."
		 * errors.
		 *
		 * The notifier is unregistered during intel_runtime_suspend(),
		 * so it's ok to access the HW here without holding a RPM
		 * wake reference -> disable wakeref asserts for the time of
		 * the access.
		 */
		disable_rpm_wakeref_asserts(dev_priv);
		intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);
		enable_rpm_wakeref_asserts(dev_priv);
		break;
	case MBI_PMIC_BUS_ACCESS_END:
		intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
		break;
	}

	return NOTIFY_OK;
}

void intel_uncore_init(struct drm_i915_private *dev_priv)
{
	i915_check_vgpu(dev_priv);

	intel_uncore_edram_detect(dev_priv);
	intel_uncore_fw_domains_init(dev_priv);
	__intel_uncore_early_sanitize(dev_priv, false);

	dev_priv->uncore.unclaimed_mmio_check = 1;
	dev_priv->uncore.pmic_bus_access_nb.notifier_call =
		i915_pmic_bus_access_notifier;

	if (IS_GEN(dev_priv, 2, 4) || intel_vgpu_active(dev_priv)) {
		ASSIGN_WRITE_MMIO_VFUNCS(dev_priv, gen2);
		ASSIGN_READ_MMIO_VFUNCS(dev_priv, gen2);
	} else if (IS_GEN5(dev_priv)) {
		ASSIGN_WRITE_MMIO_VFUNCS(dev_priv, gen5);
		ASSIGN_READ_MMIO_VFUNCS(dev_priv, gen5);
	} else if (IS_GEN(dev_priv, 6, 7)) {
		ASSIGN_WRITE_MMIO_VFUNCS(dev_priv, gen6);

		if (IS_VALLEYVIEW(dev_priv)) {
			ASSIGN_FW_DOMAINS_TABLE(__vlv_fw_ranges);
			ASSIGN_READ_MMIO_VFUNCS(dev_priv, fwtable);
		} else {
			ASSIGN_READ_MMIO_VFUNCS(dev_priv, gen6);
		}
	} else if (IS_GEN8(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv)) {
			ASSIGN_FW_DOMAINS_TABLE(__chv_fw_ranges);
			ASSIGN_WRITE_MMIO_VFUNCS(dev_priv, fwtable);
			ASSIGN_READ_MMIO_VFUNCS(dev_priv, fwtable);

		} else {
			ASSIGN_WRITE_MMIO_VFUNCS(dev_priv, gen8);
			ASSIGN_READ_MMIO_VFUNCS(dev_priv, gen6);
		}
	} else if (IS_GEN(dev_priv, 9, 10)) {
		ASSIGN_FW_DOMAINS_TABLE(__gen9_fw_ranges);
		ASSIGN_WRITE_MMIO_VFUNCS(dev_priv, fwtable);
		ASSIGN_READ_MMIO_VFUNCS(dev_priv, fwtable);
	} else {
		ASSIGN_FW_DOMAINS_TABLE(__gen11_fw_ranges);
		ASSIGN_WRITE_MMIO_VFUNCS(dev_priv, gen11_fwtable);
		ASSIGN_READ_MMIO_VFUNCS(dev_priv, gen11_fwtable);
	}

	iosf_mbi_register_pmic_bus_access_notifier(
		&dev_priv->uncore.pmic_bus_access_nb);
}

void intel_uncore_fini(struct drm_i915_private *dev_priv)
{
	/* Paranoia: make sure we have disabled everything before we exit. */
	intel_uncore_sanitize(dev_priv);

	iosf_mbi_punit_acquire();
	iosf_mbi_unregister_pmic_bus_access_notifier_unlocked(
		&dev_priv->uncore.pmic_bus_access_nb);
	intel_uncore_forcewake_reset(dev_priv, false);
	iosf_mbi_punit_release();
}

static const struct reg_whitelist {
	i915_reg_t offset_ldw;
	i915_reg_t offset_udw;
	u16 gen_mask;
	u8 size;
} reg_read_whitelist[] = { {
	.offset_ldw = RING_TIMESTAMP(RENDER_RING_BASE),
	.offset_udw = RING_TIMESTAMP_UDW(RENDER_RING_BASE),
	.gen_mask = INTEL_GEN_MASK(4, 11),
	.size = 8
} };

int i915_reg_read_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_reg_read *reg = data;
	struct reg_whitelist const *entry;
	unsigned int flags;
	int remain;
	int ret = 0;

	entry = reg_read_whitelist;
	remain = ARRAY_SIZE(reg_read_whitelist);
	while (remain) {
		u32 entry_offset = i915_mmio_reg_offset(entry->offset_ldw);

		GEM_BUG_ON(!is_power_of_2(entry->size));
		GEM_BUG_ON(entry->size > 8);
		GEM_BUG_ON(entry_offset & (entry->size - 1));

		if (INTEL_INFO(dev_priv)->gen_mask & entry->gen_mask &&
		    entry_offset == (reg->offset & -entry->size))
			break;
		entry++;
		remain--;
	}

	if (!remain)
		return -EINVAL;

	flags = reg->offset & (entry->size - 1);

	intel_runtime_pm_get(dev_priv);
	if (entry->size == 8 && flags == I915_REG_READ_8B_WA)
		reg->val = I915_READ64_2x32(entry->offset_ldw,
					    entry->offset_udw);
	else if (entry->size == 8 && flags == 0)
		reg->val = I915_READ64(entry->offset_ldw);
	else if (entry->size == 4 && flags == 0)
		reg->val = I915_READ(entry->offset_ldw);
	else if (entry->size == 2 && flags == 0)
		reg->val = I915_READ16(entry->offset_ldw);
	else if (entry->size == 1 && flags == 0)
		reg->val = I915_READ8(entry->offset_ldw);
	else
		ret = -EINVAL;
	intel_runtime_pm_put(dev_priv);

	return ret;
}

static void gen3_stop_engine(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;
	const u32 base = engine->mmio_base;
	const i915_reg_t mode = RING_MI_MODE(base);

	I915_WRITE_FW(mode, _MASKED_BIT_ENABLE(STOP_RING));
	if (intel_wait_for_register_fw(dev_priv,
				       mode,
				       MODE_IDLE,
				       MODE_IDLE,
				       500))
		DRM_DEBUG_DRIVER("%s: timed out on STOP_RING\n",
				 engine->name);

	I915_WRITE_FW(RING_HEAD(base), I915_READ_FW(RING_TAIL(base)));
	POSTING_READ_FW(RING_HEAD(base)); /* paranoia */

	I915_WRITE_FW(RING_HEAD(base), 0);
	I915_WRITE_FW(RING_TAIL(base), 0);
	POSTING_READ_FW(RING_TAIL(base));

	/* The ring must be empty before it is disabled */
	I915_WRITE_FW(RING_CTL(base), 0);

	/* Check acts as a post */
	if (I915_READ_FW(RING_HEAD(base)) != 0)
		DRM_DEBUG_DRIVER("%s: ring head not parked\n",
				 engine->name);
}

static void i915_stop_engines(struct drm_i915_private *dev_priv,
			      unsigned engine_mask)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	if (INTEL_GEN(dev_priv) < 3)
		return;

	for_each_engine_masked(engine, dev_priv, engine_mask, id)
		gen3_stop_engine(engine);
}

static bool i915_in_reset(struct pci_dev *pdev)
{
	u8 gdrst;

	pci_read_config_byte(pdev, I915_GDRST, &gdrst);
	return gdrst & GRDOM_RESET_STATUS;
}

static int i915_do_reset(struct drm_i915_private *dev_priv, unsigned engine_mask)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	int err;

	/* Assert reset for at least 20 usec, and wait for acknowledgement. */
	pci_write_config_byte(pdev, I915_GDRST, GRDOM_RESET_ENABLE);
	usleep_range(50, 200);
	err = wait_for(i915_in_reset(pdev), 500);

	/* Clear the reset request. */
	pci_write_config_byte(pdev, I915_GDRST, 0);
	usleep_range(50, 200);
	if (!err)
		err = wait_for(!i915_in_reset(pdev), 500);

	return err;
}

static bool g4x_reset_complete(struct pci_dev *pdev)
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

	/* WaVcpClkGateDisableForMediaReset:ctg,elk */
	I915_WRITE(VDECCLK_GATE_D,
		   I915_READ(VDECCLK_GATE_D) | VCP_UNIT_CLOCK_GATE_DISABLE);
	POSTING_READ(VDECCLK_GATE_D);

	pci_write_config_byte(pdev, I915_GDRST,
			      GRDOM_MEDIA | GRDOM_RESET_ENABLE);
	ret =  wait_for(g4x_reset_complete(pdev), 500);
	if (ret) {
		DRM_DEBUG_DRIVER("Wait for media reset failed\n");
		goto out;
	}

	pci_write_config_byte(pdev, I915_GDRST,
			      GRDOM_RENDER | GRDOM_RESET_ENABLE);
	ret =  wait_for(g4x_reset_complete(pdev), 500);
	if (ret) {
		DRM_DEBUG_DRIVER("Wait for render reset failed\n");
		goto out;
	}

out:
	pci_write_config_byte(pdev, I915_GDRST, 0);

	I915_WRITE(VDECCLK_GATE_D,
		   I915_READ(VDECCLK_GATE_D) & ~VCP_UNIT_CLOCK_GATE_DISABLE);
	POSTING_READ(VDECCLK_GATE_D);

	return ret;
}

static int ironlake_do_reset(struct drm_i915_private *dev_priv,
			     unsigned engine_mask)
{
	int ret;

	I915_WRITE(ILK_GDSR, ILK_GRDOM_RENDER | ILK_GRDOM_RESET_ENABLE);
	ret = intel_wait_for_register(dev_priv,
				      ILK_GDSR, ILK_GRDOM_RESET_ENABLE, 0,
				      500);
	if (ret) {
		DRM_DEBUG_DRIVER("Wait for render reset failed\n");
		goto out;
	}

	I915_WRITE(ILK_GDSR, ILK_GRDOM_MEDIA | ILK_GRDOM_RESET_ENABLE);
	ret = intel_wait_for_register(dev_priv,
				      ILK_GDSR, ILK_GRDOM_RESET_ENABLE, 0,
				      500);
	if (ret) {
		DRM_DEBUG_DRIVER("Wait for media reset failed\n");
		goto out;
	}

out:
	I915_WRITE(ILK_GDSR, 0);
	POSTING_READ(ILK_GDSR);
	return ret;
}

/* Reset the hardware domains (GENX_GRDOM_*) specified by mask */
static int gen6_hw_domain_reset(struct drm_i915_private *dev_priv,
				u32 hw_domain_mask)
{
	int err;

	/* GEN6_GDRST is not in the gt power well, no need to check
	 * for fifo space for the write or forcewake the chip for
	 * the read
	 */
	__raw_i915_write32(dev_priv, GEN6_GDRST, hw_domain_mask);

	/* Wait for the device to ack the reset requests */
	err = intel_wait_for_register_fw(dev_priv,
					  GEN6_GDRST, hw_domain_mask, 0,
					  500);
	if (err)
		DRM_DEBUG_DRIVER("Wait for 0x%08x engines reset failed\n",
				 hw_domain_mask);

	return err;
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

	if (engine_mask == ALL_ENGINES) {
		hw_mask = GEN6_GRDOM_FULL;
	} else {
		unsigned int tmp;

		hw_mask = 0;
		for_each_engine_masked(engine, dev_priv, engine_mask, tmp)
			hw_mask |= hw_engine_mask[engine->id];
	}

	return gen6_hw_domain_reset(dev_priv, hw_mask);
}

/**
 * __intel_wait_for_register_fw - wait until register matches expected state
 * @dev_priv: the i915 device
 * @reg: the register to read
 * @mask: mask to apply to register value
 * @value: expected value
 * @fast_timeout_us: fast timeout in microsecond for atomic/tight wait
 * @slow_timeout_ms: slow timeout in millisecond
 * @out_value: optional placeholder to hold registry value
 *
 * This routine waits until the target register @reg contains the expected
 * @value after applying the @mask, i.e. it waits until ::
 *
 *     (I915_READ_FW(reg) & mask) == value
 *
 * Otherwise, the wait will timeout after @slow_timeout_ms milliseconds.
 * For atomic context @slow_timeout_ms must be zero and @fast_timeout_us
 * must be not larger than 20,0000 microseconds.
 *
 * Note that this routine assumes the caller holds forcewake asserted, it is
 * not suitable for very long waits. See intel_wait_for_register() if you
 * wish to wait without holding forcewake for the duration (i.e. you expect
 * the wait to be slow).
 *
 * Returns 0 if the register matches the desired condition, or -ETIMEOUT.
 */
int __intel_wait_for_register_fw(struct drm_i915_private *dev_priv,
				 i915_reg_t reg,
				 u32 mask,
				 u32 value,
				 unsigned int fast_timeout_us,
				 unsigned int slow_timeout_ms,
				 u32 *out_value)
{
	u32 uninitialized_var(reg_value);
#define done (((reg_value = I915_READ_FW(reg)) & mask) == value)
	int ret;

	/* Catch any overuse of this function */
	might_sleep_if(slow_timeout_ms);
	GEM_BUG_ON(fast_timeout_us > 20000);

	ret = -ETIMEDOUT;
	if (fast_timeout_us && fast_timeout_us <= 20000)
		ret = _wait_for_atomic(done, fast_timeout_us, 0);
	if (ret && slow_timeout_ms)
		ret = wait_for(done, slow_timeout_ms);

	if (out_value)
		*out_value = reg_value;

	return ret;
#undef done
}

/**
 * __intel_wait_for_register - wait until register matches expected state
 * @dev_priv: the i915 device
 * @reg: the register to read
 * @mask: mask to apply to register value
 * @value: expected value
 * @fast_timeout_us: fast timeout in microsecond for atomic/tight wait
 * @slow_timeout_ms: slow timeout in millisecond
 * @out_value: optional placeholder to hold registry value
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
int __intel_wait_for_register(struct drm_i915_private *dev_priv,
			    i915_reg_t reg,
			    u32 mask,
			    u32 value,
			    unsigned int fast_timeout_us,
			    unsigned int slow_timeout_ms,
			    u32 *out_value)
{
	unsigned fw =
		intel_uncore_forcewake_for_reg(dev_priv, reg, FW_REG_READ);
	u32 reg_value;
	int ret;

	might_sleep();

	spin_lock_irq(&dev_priv->uncore.lock);
	intel_uncore_forcewake_get__locked(dev_priv, fw);

	ret = __intel_wait_for_register_fw(dev_priv,
					   reg, mask, value,
					   fast_timeout_us, 0, &reg_value);

	intel_uncore_forcewake_put__locked(dev_priv, fw);
	spin_unlock_irq(&dev_priv->uncore.lock);

	if (ret)
		ret = __wait_for(reg_value = I915_READ_NOTRACE(reg),
				 (reg_value & mask) == value,
				 slow_timeout_ms * 1000, 10, 1000);

	if (out_value)
		*out_value = reg_value;

	return ret;
}

static int gen8_reset_engine_start(struct intel_engine_cs *engine)
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

static void gen8_reset_engine_cancel(struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	I915_WRITE_FW(RING_RESET_CTL(engine->mmio_base),
		      _MASKED_BIT_DISABLE(RESET_CTL_REQUEST_RESET));
}

static int gen8_reset_engines(struct drm_i915_private *dev_priv,
			      unsigned engine_mask)
{
	struct intel_engine_cs *engine;
	unsigned int tmp;

	for_each_engine_masked(engine, dev_priv, engine_mask, tmp)
		if (gen8_reset_engine_start(engine))
			goto not_ready;

	return gen6_reset_engines(dev_priv, engine_mask);

not_ready:
	for_each_engine_masked(engine, dev_priv, engine_mask, tmp)
		gen8_reset_engine_cancel(engine);

	return -EIO;
}

typedef int (*reset_func)(struct drm_i915_private *, unsigned engine_mask);

static reset_func intel_get_gpu_reset(struct drm_i915_private *dev_priv)
{
	if (!i915_modparams.reset)
		return NULL;

	if (INTEL_GEN(dev_priv) >= 8)
		return gen8_reset_engines;
	else if (INTEL_GEN(dev_priv) >= 6)
		return gen6_reset_engines;
	else if (IS_GEN5(dev_priv))
		return ironlake_do_reset;
	else if (IS_G4X(dev_priv))
		return g4x_do_reset;
	else if (IS_G33(dev_priv) || IS_PINEVIEW(dev_priv))
		return g33_do_reset;
	else if (INTEL_GEN(dev_priv) >= 3)
		return i915_do_reset;
	else
		return NULL;
}

int intel_gpu_reset(struct drm_i915_private *dev_priv, unsigned engine_mask)
{
	reset_func reset = intel_get_gpu_reset(dev_priv);
	int retry;
	int ret;

	might_sleep();

	/* If the power well sleeps during the reset, the reset
	 * request may be dropped and never completes (causing -EIO).
	 */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);
	for (retry = 0; retry < 3; retry++) {

		/* We stop engines, otherwise we might get failed reset and a
		 * dead gpu (on elk). Also as modern gpu as kbl can suffer
		 * from system hang if batchbuffer is progressing when
		 * the reset is issued, regardless of READY_TO_RESET ack.
		 * Thus assume it is best to stop engines on all gens
		 * where we have a gpu reset.
		 *
		 * WaMediaResetMainRingCleanup:ctg,elk (presumably)
		 *
		 * FIXME: Wa for more modern gens needs to be validated
		 */
		i915_stop_engines(dev_priv, engine_mask);

		ret = -ENODEV;
		if (reset)
			ret = reset(dev_priv, engine_mask);
		if (ret != -ETIMEDOUT)
			break;

		cond_resched();
	}
	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

	return ret;
}

bool intel_has_gpu_reset(struct drm_i915_private *dev_priv)
{
	return intel_get_gpu_reset(dev_priv) != NULL;
}

bool intel_has_reset_engine(struct drm_i915_private *dev_priv)
{
	return (dev_priv->info.has_reset_engine &&
		i915_modparams.reset >= 2);
}

int intel_reset_guc(struct drm_i915_private *dev_priv)
{
	int ret;

	GEM_BUG_ON(!HAS_GUC(dev_priv));

	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);
	ret = gen6_hw_domain_reset(dev_priv, GEN9_GRDOM_GUC);
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
	if (unlikely(i915_modparams.mmio_debug ||
		     dev_priv->uncore.unclaimed_mmio_check <= 0))
		return false;

	if (unlikely(intel_uncore_unclaimed_mmio(dev_priv))) {
		DRM_DEBUG("Unclaimed register detected, "
			  "enabling oneshot unclaimed register reporting. "
			  "Please use i915.mmio_debug=N for more information.\n");
		i915_modparams.mmio_debug++;
		dev_priv->uncore.unclaimed_mmio_check--;
		return true;
	}

	return false;
}

static enum forcewake_domains
intel_uncore_forcewake_for_read(struct drm_i915_private *dev_priv,
				i915_reg_t reg)
{
	u32 offset = i915_mmio_reg_offset(reg);
	enum forcewake_domains fw_domains;

	if (INTEL_GEN(dev_priv) >= 11) {
		fw_domains = __gen11_fwtable_reg_read_fw_domains(offset);
	} else if (HAS_FWTABLE(dev_priv)) {
		fw_domains = __fwtable_reg_read_fw_domains(offset);
	} else if (INTEL_GEN(dev_priv) >= 6) {
		fw_domains = __gen6_reg_read_fw_domains(offset);
	} else {
		WARN_ON(!IS_GEN(dev_priv, 2, 5));
		fw_domains = 0;
	}

	WARN_ON(fw_domains & ~dev_priv->uncore.fw_domains);

	return fw_domains;
}

static enum forcewake_domains
intel_uncore_forcewake_for_write(struct drm_i915_private *dev_priv,
				 i915_reg_t reg)
{
	u32 offset = i915_mmio_reg_offset(reg);
	enum forcewake_domains fw_domains;

	if (INTEL_GEN(dev_priv) >= 11) {
		fw_domains = __gen11_fwtable_reg_write_fw_domains(offset);
	} else if (HAS_FWTABLE(dev_priv) && !IS_VALLEYVIEW(dev_priv)) {
		fw_domains = __fwtable_reg_write_fw_domains(offset);
	} else if (IS_GEN8(dev_priv)) {
		fw_domains = __gen8_reg_write_fw_domains(offset);
	} else if (IS_GEN(dev_priv, 6, 7)) {
		fw_domains = FORCEWAKE_RENDER;
	} else {
		WARN_ON(!IS_GEN(dev_priv, 2, 5));
		fw_domains = 0;
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

	if (intel_vgpu_active(dev_priv))
		return 0;

	if (op & FW_REG_READ)
		fw_domains = intel_uncore_forcewake_for_read(dev_priv, reg);

	if (op & FW_REG_WRITE)
		fw_domains |= intel_uncore_forcewake_for_write(dev_priv, reg);

	return fw_domains;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_uncore.c"
#include "selftests/intel_uncore.c"
#endif
