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

#include <linux/pm_runtime.h>
#include <asm/iosf_mbi.h>

#include "i915_drv.h"
#include "i915_trace.h"
#include "i915_vgpu.h"
#include "intel_pm.h"

#define FORCEWAKE_ACK_TIMEOUT_MS 50
#define GT_FIFO_TIMEOUT_MS	 10

#define __raw_posting_read(...) ((void)__raw_uncore_read32(__VA_ARGS__))

void
intel_uncore_mmio_debug_init_early(struct intel_uncore_mmio_debug *mmio_debug)
{
	spin_lock_init(&mmio_debug->lock);
	mmio_debug->unclaimed_mmio_check = 1;
}

static void mmio_debug_suspend(struct intel_uncore_mmio_debug *mmio_debug)
{
	lockdep_assert_held(&mmio_debug->lock);

	/* Save and disable mmio debugging for the user bypass */
	if (!mmio_debug->suspend_count++) {
		mmio_debug->saved_mmio_check = mmio_debug->unclaimed_mmio_check;
		mmio_debug->unclaimed_mmio_check = 0;
	}
}

static void mmio_debug_resume(struct intel_uncore_mmio_debug *mmio_debug)
{
	lockdep_assert_held(&mmio_debug->lock);

	if (!--mmio_debug->suspend_count)
		mmio_debug->unclaimed_mmio_check = mmio_debug->saved_mmio_check;
}

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

#define fw_ack(d) readl((d)->reg_ack)
#define fw_set(d, val) writel(_MASKED_BIT_ENABLE((val)), (d)->reg_set)
#define fw_clear(d, val) writel(_MASKED_BIT_DISABLE((val)), (d)->reg_set)

static inline void
fw_domain_reset(const struct intel_uncore_forcewake_domain *d)
{
	/*
	 * We don't really know if the powerwell for the forcewake domain we are
	 * trying to reset here does exist at this point (engines could be fused
	 * off in ICL+), so no waiting for acks
	 */
	/* WaRsClearFWBitsAtReset:bdw,skl */
	fw_clear(d, 0xffff);
}

static inline void
fw_domain_arm_timer(struct intel_uncore_forcewake_domain *d)
{
	GEM_BUG_ON(d->uncore->fw_domains_timer & d->mask);
	d->uncore->fw_domains_timer |= d->mask;
	d->wake_count++;
	hrtimer_start_range_ns(&d->timer,
			       NSEC_PER_MSEC,
			       NSEC_PER_MSEC,
			       HRTIMER_MODE_REL);
}

static inline int
__wait_for_ack(const struct intel_uncore_forcewake_domain *d,
	       const u32 ack,
	       const u32 value)
{
	return wait_for_atomic((fw_ack(d) & ack) == value,
			       FORCEWAKE_ACK_TIMEOUT_MS);
}

static inline int
wait_ack_clear(const struct intel_uncore_forcewake_domain *d,
	       const u32 ack)
{
	return __wait_for_ack(d, ack, 0);
}

static inline int
wait_ack_set(const struct intel_uncore_forcewake_domain *d,
	     const u32 ack)
{
	return __wait_for_ack(d, ack, ack);
}

static inline void
fw_domain_wait_ack_clear(const struct intel_uncore_forcewake_domain *d)
{
	if (wait_ack_clear(d, FORCEWAKE_KERNEL)) {
		DRM_ERROR("%s: timed out waiting for forcewake ack to clear.\n",
			  intel_uncore_forcewake_domain_to_str(d->id));
		add_taint_for_CI(d->uncore->i915, TAINT_WARN); /* CI now unreliable */
	}
}

enum ack_type {
	ACK_CLEAR = 0,
	ACK_SET
};

static int
fw_domain_wait_ack_with_fallback(const struct intel_uncore_forcewake_domain *d,
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
	 * This workaround is described in HSDES #1604254524 and it's known as:
	 * WaRsForcewakeAddDelayForAck:skl,bxt,kbl,glk,cfl,cnl,icl
	 * although the name is a bit misleading.
	 */

	pass = 1;
	do {
		wait_ack_clear(d, FORCEWAKE_KERNEL_FALLBACK);

		fw_set(d, FORCEWAKE_KERNEL_FALLBACK);
		/* Give gt some time to relax before the polling frenzy */
		udelay(10 * pass);
		wait_ack_set(d, FORCEWAKE_KERNEL_FALLBACK);

		ack_detected = (fw_ack(d) & ack_bit) == value;

		fw_clear(d, FORCEWAKE_KERNEL_FALLBACK);
	} while (!ack_detected && pass++ < 10);

	DRM_DEBUG_DRIVER("%s had to use fallback to %s ack, 0x%x (passes %u)\n",
			 intel_uncore_forcewake_domain_to_str(d->id),
			 type == ACK_SET ? "set" : "clear",
			 fw_ack(d),
			 pass);

	return ack_detected ? 0 : -ETIMEDOUT;
}

static inline void
fw_domain_wait_ack_clear_fallback(const struct intel_uncore_forcewake_domain *d)
{
	if (likely(!wait_ack_clear(d, FORCEWAKE_KERNEL)))
		return;

	if (fw_domain_wait_ack_with_fallback(d, ACK_CLEAR))
		fw_domain_wait_ack_clear(d);
}

static inline void
fw_domain_get(const struct intel_uncore_forcewake_domain *d)
{
	fw_set(d, FORCEWAKE_KERNEL);
}

static inline void
fw_domain_wait_ack_set(const struct intel_uncore_forcewake_domain *d)
{
	if (wait_ack_set(d, FORCEWAKE_KERNEL)) {
		DRM_ERROR("%s: timed out waiting for forcewake ack request.\n",
			  intel_uncore_forcewake_domain_to_str(d->id));
		add_taint_for_CI(d->uncore->i915, TAINT_WARN); /* CI now unreliable */
	}
}

static inline void
fw_domain_wait_ack_set_fallback(const struct intel_uncore_forcewake_domain *d)
{
	if (likely(!wait_ack_set(d, FORCEWAKE_KERNEL)))
		return;

	if (fw_domain_wait_ack_with_fallback(d, ACK_SET))
		fw_domain_wait_ack_set(d);
}

static inline void
fw_domain_put(const struct intel_uncore_forcewake_domain *d)
{
	fw_clear(d, FORCEWAKE_KERNEL);
}

static void
fw_domains_get(struct intel_uncore *uncore, enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;
	unsigned int tmp;

	GEM_BUG_ON(fw_domains & ~uncore->fw_domains);

	for_each_fw_domain_masked(d, fw_domains, uncore, tmp) {
		fw_domain_wait_ack_clear(d);
		fw_domain_get(d);
	}

	for_each_fw_domain_masked(d, fw_domains, uncore, tmp)
		fw_domain_wait_ack_set(d);

	uncore->fw_domains_active |= fw_domains;
}

static void
fw_domains_get_with_fallback(struct intel_uncore *uncore,
			     enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;
	unsigned int tmp;

	GEM_BUG_ON(fw_domains & ~uncore->fw_domains);

	for_each_fw_domain_masked(d, fw_domains, uncore, tmp) {
		fw_domain_wait_ack_clear_fallback(d);
		fw_domain_get(d);
	}

	for_each_fw_domain_masked(d, fw_domains, uncore, tmp)
		fw_domain_wait_ack_set_fallback(d);

	uncore->fw_domains_active |= fw_domains;
}

static void
fw_domains_put(struct intel_uncore *uncore, enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;
	unsigned int tmp;

	GEM_BUG_ON(fw_domains & ~uncore->fw_domains);

	for_each_fw_domain_masked(d, fw_domains, uncore, tmp)
		fw_domain_put(d);

	uncore->fw_domains_active &= ~fw_domains;
}

static void
fw_domains_reset(struct intel_uncore *uncore,
		 enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *d;
	unsigned int tmp;

	if (!fw_domains)
		return;

	GEM_BUG_ON(fw_domains & ~uncore->fw_domains);

	for_each_fw_domain_masked(d, fw_domains, uncore, tmp)
		fw_domain_reset(d);
}

static inline u32 gt_thread_status(struct intel_uncore *uncore)
{
	u32 val;

	val = __raw_uncore_read32(uncore, GEN6_GT_THREAD_STATUS_REG);
	val &= GEN6_GT_THREAD_STATUS_CORE_MASK;

	return val;
}

static void __gen6_gt_wait_for_thread_c0(struct intel_uncore *uncore)
{
	/*
	 * w/a for a sporadic read returning 0 by waiting for the GT
	 * thread to wake up.
	 */
	drm_WARN_ONCE(&uncore->i915->drm,
		      wait_for_atomic_us(gt_thread_status(uncore) == 0, 5000),
		      "GT thread status wait timed out\n");
}

static void fw_domains_get_with_thread_status(struct intel_uncore *uncore,
					      enum forcewake_domains fw_domains)
{
	fw_domains_get(uncore, fw_domains);

	/* WaRsForcewakeWaitTC0:snb,ivb,hsw,bdw,vlv */
	__gen6_gt_wait_for_thread_c0(uncore);
}

static inline u32 fifo_free_entries(struct intel_uncore *uncore)
{
	u32 count = __raw_uncore_read32(uncore, GTFIFOCTL);

	return count & GT_FIFO_FREE_ENTRIES_MASK;
}

static void __gen6_gt_wait_for_fifo(struct intel_uncore *uncore)
{
	u32 n;

	/* On VLV, FIFO will be shared by both SW and HW.
	 * So, we need to read the FREE_ENTRIES everytime */
	if (IS_VALLEYVIEW(uncore->i915))
		n = fifo_free_entries(uncore);
	else
		n = uncore->fifo_count;

	if (n <= GT_FIFO_NUM_RESERVED_ENTRIES) {
		if (wait_for_atomic((n = fifo_free_entries(uncore)) >
				    GT_FIFO_NUM_RESERVED_ENTRIES,
				    GT_FIFO_TIMEOUT_MS)) {
			drm_dbg(&uncore->i915->drm,
				"GT_FIFO timeout, entries: %u\n", n);
			return;
		}
	}

	uncore->fifo_count = n - 1;
}

static enum hrtimer_restart
intel_uncore_fw_release_timer(struct hrtimer *timer)
{
	struct intel_uncore_forcewake_domain *domain =
	       container_of(timer, struct intel_uncore_forcewake_domain, timer);
	struct intel_uncore *uncore = domain->uncore;
	unsigned long irqflags;

	assert_rpm_device_not_suspended(uncore->rpm);

	if (xchg(&domain->active, false))
		return HRTIMER_RESTART;

	spin_lock_irqsave(&uncore->lock, irqflags);

	uncore->fw_domains_timer &= ~domain->mask;

	GEM_BUG_ON(!domain->wake_count);
	if (--domain->wake_count == 0)
		uncore->funcs.force_wake_put(uncore, domain->mask);

	spin_unlock_irqrestore(&uncore->lock, irqflags);

	return HRTIMER_NORESTART;
}

/* Note callers must have acquired the PUNIT->PMIC bus, before calling this. */
static unsigned int
intel_uncore_forcewake_reset(struct intel_uncore *uncore)
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

		for_each_fw_domain(domain, uncore, tmp) {
			smp_store_mb(domain->active, false);
			if (hrtimer_cancel(&domain->timer) == 0)
				continue;

			intel_uncore_fw_release_timer(&domain->timer);
		}

		spin_lock_irqsave(&uncore->lock, irqflags);

		for_each_fw_domain(domain, uncore, tmp) {
			if (hrtimer_active(&domain->timer))
				active_domains |= domain->mask;
		}

		if (active_domains == 0)
			break;

		if (--retry_count == 0) {
			drm_err(&uncore->i915->drm, "Timed out waiting for forcewake timers to finish\n");
			break;
		}

		spin_unlock_irqrestore(&uncore->lock, irqflags);
		cond_resched();
	}

	drm_WARN_ON(&uncore->i915->drm, active_domains);

	fw = uncore->fw_domains_active;
	if (fw)
		uncore->funcs.force_wake_put(uncore, fw);

	fw_domains_reset(uncore, uncore->fw_domains);
	assert_forcewakes_inactive(uncore);

	spin_unlock_irqrestore(&uncore->lock, irqflags);

	return fw; /* track the lost user forcewake domains */
}

static bool
fpga_check_for_unclaimed_mmio(struct intel_uncore *uncore)
{
	u32 dbg;

	dbg = __raw_uncore_read32(uncore, FPGA_DBG);
	if (likely(!(dbg & FPGA_DBG_RM_NOCLAIM)))
		return false;

	__raw_uncore_write32(uncore, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);

	return true;
}

static bool
vlv_check_for_unclaimed_mmio(struct intel_uncore *uncore)
{
	u32 cer;

	cer = __raw_uncore_read32(uncore, CLAIM_ER);
	if (likely(!(cer & (CLAIM_ER_OVERFLOW | CLAIM_ER_CTR_MASK))))
		return false;

	__raw_uncore_write32(uncore, CLAIM_ER, CLAIM_ER_CLR);

	return true;
}

static bool
gen6_check_for_fifo_debug(struct intel_uncore *uncore)
{
	u32 fifodbg;

	fifodbg = __raw_uncore_read32(uncore, GTFIFODBG);

	if (unlikely(fifodbg)) {
		drm_dbg(&uncore->i915->drm, "GTFIFODBG = 0x08%x\n", fifodbg);
		__raw_uncore_write32(uncore, GTFIFODBG, fifodbg);
	}

	return fifodbg;
}

static bool
check_for_unclaimed_mmio(struct intel_uncore *uncore)
{
	bool ret = false;

	lockdep_assert_held(&uncore->debug->lock);

	if (uncore->debug->suspend_count)
		return false;

	if (intel_uncore_has_fpga_dbg_unclaimed(uncore))
		ret |= fpga_check_for_unclaimed_mmio(uncore);

	if (intel_uncore_has_dbg_unclaimed(uncore))
		ret |= vlv_check_for_unclaimed_mmio(uncore);

	if (intel_uncore_has_fifo(uncore))
		ret |= gen6_check_for_fifo_debug(uncore);

	return ret;
}

static void forcewake_early_sanitize(struct intel_uncore *uncore,
				     unsigned int restore_forcewake)
{
	GEM_BUG_ON(!intel_uncore_has_forcewake(uncore));

	/* WaDisableShadowRegForCpd:chv */
	if (IS_CHERRYVIEW(uncore->i915)) {
		__raw_uncore_write32(uncore, GTFIFOCTL,
				     __raw_uncore_read32(uncore, GTFIFOCTL) |
				     GT_FIFO_CTL_BLOCK_ALL_POLICY_STALL |
				     GT_FIFO_CTL_RC6_POLICY_STALL);
	}

	iosf_mbi_punit_acquire();
	intel_uncore_forcewake_reset(uncore);
	if (restore_forcewake) {
		spin_lock_irq(&uncore->lock);
		uncore->funcs.force_wake_get(uncore, restore_forcewake);

		if (intel_uncore_has_fifo(uncore))
			uncore->fifo_count = fifo_free_entries(uncore);
		spin_unlock_irq(&uncore->lock);
	}
	iosf_mbi_punit_release();
}

void intel_uncore_suspend(struct intel_uncore *uncore)
{
	if (!intel_uncore_has_forcewake(uncore))
		return;

	iosf_mbi_punit_acquire();
	iosf_mbi_unregister_pmic_bus_access_notifier_unlocked(
		&uncore->pmic_bus_access_nb);
	uncore->fw_domains_saved = intel_uncore_forcewake_reset(uncore);
	iosf_mbi_punit_release();
}

void intel_uncore_resume_early(struct intel_uncore *uncore)
{
	unsigned int restore_forcewake;

	if (intel_uncore_unclaimed_mmio(uncore))
		drm_dbg(&uncore->i915->drm, "unclaimed mmio detected on resume, clearing\n");

	if (!intel_uncore_has_forcewake(uncore))
		return;

	restore_forcewake = fetch_and_zero(&uncore->fw_domains_saved);
	forcewake_early_sanitize(uncore, restore_forcewake);

	iosf_mbi_register_pmic_bus_access_notifier(&uncore->pmic_bus_access_nb);
}

void intel_uncore_runtime_resume(struct intel_uncore *uncore)
{
	if (!intel_uncore_has_forcewake(uncore))
		return;

	iosf_mbi_register_pmic_bus_access_notifier(&uncore->pmic_bus_access_nb);
}

static void __intel_uncore_forcewake_get(struct intel_uncore *uncore,
					 enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;
	unsigned int tmp;

	fw_domains &= uncore->fw_domains;

	for_each_fw_domain_masked(domain, fw_domains, uncore, tmp) {
		if (domain->wake_count++) {
			fw_domains &= ~domain->mask;
			domain->active = true;
		}
	}

	if (fw_domains)
		uncore->funcs.force_wake_get(uncore, fw_domains);
}

/**
 * intel_uncore_forcewake_get - grab forcewake domain references
 * @uncore: the intel_uncore structure
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
void intel_uncore_forcewake_get(struct intel_uncore *uncore,
				enum forcewake_domains fw_domains)
{
	unsigned long irqflags;

	if (!uncore->funcs.force_wake_get)
		return;

	assert_rpm_wakelock_held(uncore->rpm);

	spin_lock_irqsave(&uncore->lock, irqflags);
	__intel_uncore_forcewake_get(uncore, fw_domains);
	spin_unlock_irqrestore(&uncore->lock, irqflags);
}

/**
 * intel_uncore_forcewake_user_get - claim forcewake on behalf of userspace
 * @uncore: the intel_uncore structure
 *
 * This function is a wrapper around intel_uncore_forcewake_get() to acquire
 * the GT powerwell and in the process disable our debugging for the
 * duration of userspace's bypass.
 */
void intel_uncore_forcewake_user_get(struct intel_uncore *uncore)
{
	spin_lock_irq(&uncore->lock);
	if (!uncore->user_forcewake_count++) {
		intel_uncore_forcewake_get__locked(uncore, FORCEWAKE_ALL);
		spin_lock(&uncore->debug->lock);
		mmio_debug_suspend(uncore->debug);
		spin_unlock(&uncore->debug->lock);
	}
	spin_unlock_irq(&uncore->lock);
}

/**
 * intel_uncore_forcewake_user_put - release forcewake on behalf of userspace
 * @uncore: the intel_uncore structure
 *
 * This function complements intel_uncore_forcewake_user_get() and releases
 * the GT powerwell taken on behalf of the userspace bypass.
 */
void intel_uncore_forcewake_user_put(struct intel_uncore *uncore)
{
	spin_lock_irq(&uncore->lock);
	if (!--uncore->user_forcewake_count) {
		spin_lock(&uncore->debug->lock);
		mmio_debug_resume(uncore->debug);

		if (check_for_unclaimed_mmio(uncore))
			drm_info(&uncore->i915->drm,
				 "Invalid mmio detected during user access\n");
		spin_unlock(&uncore->debug->lock);

		intel_uncore_forcewake_put__locked(uncore, FORCEWAKE_ALL);
	}
	spin_unlock_irq(&uncore->lock);
}

/**
 * intel_uncore_forcewake_get__locked - grab forcewake domain references
 * @uncore: the intel_uncore structure
 * @fw_domains: forcewake domains to get reference on
 *
 * See intel_uncore_forcewake_get(). This variant places the onus
 * on the caller to explicitly handle the dev_priv->uncore.lock spinlock.
 */
void intel_uncore_forcewake_get__locked(struct intel_uncore *uncore,
					enum forcewake_domains fw_domains)
{
	lockdep_assert_held(&uncore->lock);

	if (!uncore->funcs.force_wake_get)
		return;

	__intel_uncore_forcewake_get(uncore, fw_domains);
}

static void __intel_uncore_forcewake_put(struct intel_uncore *uncore,
					 enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;
	unsigned int tmp;

	fw_domains &= uncore->fw_domains;

	for_each_fw_domain_masked(domain, fw_domains, uncore, tmp) {
		GEM_BUG_ON(!domain->wake_count);

		if (--domain->wake_count) {
			domain->active = true;
			continue;
		}

		uncore->funcs.force_wake_put(uncore, domain->mask);
	}
}

/**
 * intel_uncore_forcewake_put - release a forcewake domain reference
 * @uncore: the intel_uncore structure
 * @fw_domains: forcewake domains to put references
 *
 * This function drops the device-level forcewakes for specified
 * domains obtained by intel_uncore_forcewake_get().
 */
void intel_uncore_forcewake_put(struct intel_uncore *uncore,
				enum forcewake_domains fw_domains)
{
	unsigned long irqflags;

	if (!uncore->funcs.force_wake_put)
		return;

	spin_lock_irqsave(&uncore->lock, irqflags);
	__intel_uncore_forcewake_put(uncore, fw_domains);
	spin_unlock_irqrestore(&uncore->lock, irqflags);
}

/**
 * intel_uncore_forcewake_flush - flush the delayed release
 * @uncore: the intel_uncore structure
 * @fw_domains: forcewake domains to flush
 */
void intel_uncore_forcewake_flush(struct intel_uncore *uncore,
				  enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;
	unsigned int tmp;

	if (!uncore->funcs.force_wake_put)
		return;

	fw_domains &= uncore->fw_domains;
	for_each_fw_domain_masked(domain, fw_domains, uncore, tmp) {
		WRITE_ONCE(domain->active, false);
		if (hrtimer_cancel(&domain->timer))
			intel_uncore_fw_release_timer(&domain->timer);
	}
}

/**
 * intel_uncore_forcewake_put__locked - grab forcewake domain references
 * @uncore: the intel_uncore structure
 * @fw_domains: forcewake domains to get reference on
 *
 * See intel_uncore_forcewake_put(). This variant places the onus
 * on the caller to explicitly handle the dev_priv->uncore.lock spinlock.
 */
void intel_uncore_forcewake_put__locked(struct intel_uncore *uncore,
					enum forcewake_domains fw_domains)
{
	lockdep_assert_held(&uncore->lock);

	if (!uncore->funcs.force_wake_put)
		return;

	__intel_uncore_forcewake_put(uncore, fw_domains);
}

void assert_forcewakes_inactive(struct intel_uncore *uncore)
{
	if (!uncore->funcs.force_wake_get)
		return;

	drm_WARN(&uncore->i915->drm, uncore->fw_domains_active,
		 "Expected all fw_domains to be inactive, but %08x are still on\n",
		 uncore->fw_domains_active);
}

void assert_forcewakes_active(struct intel_uncore *uncore,
			      enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;
	unsigned int tmp;

	if (!IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM))
		return;

	if (!uncore->funcs.force_wake_get)
		return;

	spin_lock_irq(&uncore->lock);

	assert_rpm_wakelock_held(uncore->rpm);

	fw_domains &= uncore->fw_domains;
	drm_WARN(&uncore->i915->drm, fw_domains & ~uncore->fw_domains_active,
		 "Expected %08x fw_domains to be active, but %08x are off\n",
		 fw_domains, fw_domains & ~uncore->fw_domains_active);

	/*
	 * Check that the caller has an explicit wakeref and we don't mistake
	 * it for the auto wakeref.
	 */
	for_each_fw_domain_masked(domain, fw_domains, uncore, tmp) {
		unsigned int actual = READ_ONCE(domain->wake_count);
		unsigned int expect = 1;

		if (uncore->fw_domains_timer & domain->mask)
			expect++; /* pending automatic release */

		if (drm_WARN(&uncore->i915->drm, actual < expect,
			     "Expected domain %d to be held awake by caller, count=%d\n",
			     domain->id, actual))
			break;
	}

	spin_unlock_irq(&uncore->lock);
}

/* We give fast paths for the really cool registers */
#define NEEDS_FORCE_WAKE(reg) ((reg) < 0x40000)

#define __gen6_reg_read_fw_domains(uncore, offset) \
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
find_fw_domain(struct intel_uncore *uncore, u32 offset)
{
	const struct intel_forcewake_range *entry;

	entry = BSEARCH(offset,
			uncore->fw_domains_table,
			uncore->fw_domains_table_entries,
			fw_range_cmp);

	if (!entry)
		return 0;

	/*
	 * The list of FW domains depends on the SKU in gen11+ so we
	 * can't determine it statically. We use FORCEWAKE_ALL and
	 * translate it here to the list of available domains.
	 */
	if (entry->domains == FORCEWAKE_ALL)
		return uncore->fw_domains;

	drm_WARN(&uncore->i915->drm, entry->domains & ~uncore->fw_domains,
		 "Uninitialized forcewake domain(s) 0x%x accessed at 0x%x\n",
		 entry->domains & ~uncore->fw_domains, offset);

	return entry->domains;
}

#define GEN_FW_RANGE(s, e, d) \
	{ .start = (s), .end = (e), .domains = (d) }

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

#define __fwtable_reg_read_fw_domains(uncore, offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	if (NEEDS_FORCE_WAKE((offset))) \
		__fwd = find_fw_domain(uncore, offset); \
	__fwd; \
})

#define __gen11_fwtable_reg_read_fw_domains(uncore, offset) \
	find_fw_domain(uncore, offset)

#define __gen12_fwtable_reg_read_fw_domains(uncore, offset) \
	find_fw_domain(uncore, offset)

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

static const i915_reg_t gen12_shadowed_regs[] = {
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
__is_genX_shadowed(12)

static enum forcewake_domains
gen6_reg_write_fw_domains(struct intel_uncore *uncore, i915_reg_t reg)
{
	return FORCEWAKE_RENDER;
}

#define __gen8_reg_write_fw_domains(uncore, offset) \
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

#define __fwtable_reg_write_fw_domains(uncore, offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	if (NEEDS_FORCE_WAKE((offset)) && !is_gen8_shadowed(offset)) \
		__fwd = find_fw_domain(uncore, offset); \
	__fwd; \
})

#define __gen11_fwtable_reg_write_fw_domains(uncore, offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	const u32 __offset = (offset); \
	if (!is_gen11_shadowed(__offset)) \
		__fwd = find_fw_domain(uncore, __offset); \
	__fwd; \
})

#define __gen12_fwtable_reg_write_fw_domains(uncore, offset) \
({ \
	enum forcewake_domains __fwd = 0; \
	const u32 __offset = (offset); \
	if (!is_gen12_shadowed(__offset)) \
		__fwd = find_fw_domain(uncore, __offset); \
	__fwd; \
})

/* *Must* be sorted by offset ranges! See intel_fw_table_check(). */
static const struct intel_forcewake_range __gen9_fw_ranges[] = {
	GEN_FW_RANGE(0x0, 0xaff, FORCEWAKE_GT),
	GEN_FW_RANGE(0xb00, 0x1fff, 0), /* uncore range */
	GEN_FW_RANGE(0x2000, 0x26ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x2700, 0x2fff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x3000, 0x3fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x4000, 0x51ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x5200, 0x7fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8000, 0x812f, FORCEWAKE_GT),
	GEN_FW_RANGE(0x8130, 0x813f, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x8140, 0x815f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8160, 0x82ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x8300, 0x84ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8500, 0x87ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x8800, 0x89ff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x8a00, 0x8bff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x8c00, 0x8cff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8d00, 0x93ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x9400, 0x97ff, FORCEWAKE_RENDER | FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x9800, 0xafff, FORCEWAKE_GT),
	GEN_FW_RANGE(0xb000, 0xb47f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xb480, 0xcfff, FORCEWAKE_GT),
	GEN_FW_RANGE(0xd000, 0xd7ff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0xd800, 0xdfff, FORCEWAKE_GT),
	GEN_FW_RANGE(0xe000, 0xe8ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xe900, 0x11fff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x12000, 0x13fff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x14000, 0x19fff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x1a000, 0x1e9ff, FORCEWAKE_MEDIA),
	GEN_FW_RANGE(0x1ea00, 0x243ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x24400, 0x247ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x24800, 0x2ffff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x30000, 0x3ffff, FORCEWAKE_MEDIA),
};

/* *Must* be sorted by offset ranges! See intel_fw_table_check(). */
static const struct intel_forcewake_range __gen11_fw_ranges[] = {
	GEN_FW_RANGE(0x0, 0x1fff, 0), /* uncore range */
	GEN_FW_RANGE(0x2000, 0x26ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x2700, 0x2fff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x3000, 0x3fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x4000, 0x51ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x5200, 0x7fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8000, 0x813f, FORCEWAKE_GT),
	GEN_FW_RANGE(0x8140, 0x815f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8160, 0x82ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x8300, 0x84ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8500, 0x87ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x8800, 0x8bff, 0),
	GEN_FW_RANGE(0x8c00, 0x8cff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8d00, 0x94cf, FORCEWAKE_GT),
	GEN_FW_RANGE(0x94d0, 0x955f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x9560, 0x95ff, 0),
	GEN_FW_RANGE(0x9600, 0xafff, FORCEWAKE_GT),
	GEN_FW_RANGE(0xb000, 0xb47f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xb480, 0xdeff, FORCEWAKE_GT),
	GEN_FW_RANGE(0xdf00, 0xe8ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xe900, 0x16dff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x16e00, 0x19fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x1a000, 0x23fff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x24000, 0x2407f, 0),
	GEN_FW_RANGE(0x24080, 0x2417f, FORCEWAKE_GT),
	GEN_FW_RANGE(0x24180, 0x242ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x24300, 0x243ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x24400, 0x24fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x25000, 0x3ffff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x40000, 0x1bffff, 0),
	GEN_FW_RANGE(0x1c0000, 0x1c3fff, FORCEWAKE_MEDIA_VDBOX0),
	GEN_FW_RANGE(0x1c4000, 0x1c7fff, 0),
	GEN_FW_RANGE(0x1c8000, 0x1cffff, FORCEWAKE_MEDIA_VEBOX0),
	GEN_FW_RANGE(0x1d0000, 0x1d3fff, FORCEWAKE_MEDIA_VDBOX2),
	GEN_FW_RANGE(0x1d4000, 0x1dbfff, 0)
};

/* *Must* be sorted by offset ranges! See intel_fw_table_check(). */
static const struct intel_forcewake_range __gen12_fw_ranges[] = {
	GEN_FW_RANGE(0x0, 0xaff, FORCEWAKE_GT),
	GEN_FW_RANGE(0xb00, 0x1fff, 0), /* uncore range */
	GEN_FW_RANGE(0x2000, 0x26ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x2700, 0x2fff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x3000, 0x3fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x4000, 0x51ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x5200, 0x7fff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8000, 0x813f, FORCEWAKE_GT),
	GEN_FW_RANGE(0x8140, 0x815f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8160, 0x82ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x8300, 0x84ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8500, 0x8bff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x8c00, 0x8cff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x8d00, 0x93ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x9400, 0x97ff, FORCEWAKE_ALL),
	GEN_FW_RANGE(0x9800, 0xafff, FORCEWAKE_GT),
	GEN_FW_RANGE(0xb000, 0xb47f, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xb480, 0xdfff, FORCEWAKE_GT),
	GEN_FW_RANGE(0xe000, 0xe8ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0xe900, 0x147ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x14800, 0x148ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x14900, 0x19fff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x1a000, 0x1a7ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x1a800, 0x1afff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x1b000, 0x1bfff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x1c000, 0x243ff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x24400, 0x247ff, FORCEWAKE_RENDER),
	GEN_FW_RANGE(0x24800, 0x3ffff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x40000, 0x1bffff, 0),
	GEN_FW_RANGE(0x1c0000, 0x1c3fff, FORCEWAKE_MEDIA_VDBOX0),
	GEN_FW_RANGE(0x1c4000, 0x1c7fff, FORCEWAKE_MEDIA_VDBOX1),
	GEN_FW_RANGE(0x1c8000, 0x1cbfff, FORCEWAKE_MEDIA_VEBOX0),
	GEN_FW_RANGE(0x1cc000, 0x1cffff, FORCEWAKE_GT),
	GEN_FW_RANGE(0x1d0000, 0x1d3fff, FORCEWAKE_MEDIA_VDBOX2),
	GEN_FW_RANGE(0x1d4000, 0x1d7fff, FORCEWAKE_MEDIA_VDBOX3),
	GEN_FW_RANGE(0x1d8000, 0x1dbfff, FORCEWAKE_MEDIA_VEBOX1)
};

static void
ilk_dummy_write(struct intel_uncore *uncore)
{
	/* WaIssueDummyWriteToWakeupFromRC6:ilk Issue a dummy write to wake up
	 * the chip from rc6 before touching it for real. MI_MODE is masked,
	 * hence harmless to write 0 into. */
	__raw_uncore_write32(uncore, MI_MODE, 0);
}

static void
__unclaimed_reg_debug(struct intel_uncore *uncore,
		      const i915_reg_t reg,
		      const bool read,
		      const bool before)
{
	if (drm_WARN(&uncore->i915->drm,
		     check_for_unclaimed_mmio(uncore) && !before,
		     "Unclaimed %s register 0x%x\n",
		     read ? "read from" : "write to",
		     i915_mmio_reg_offset(reg)))
		/* Only report the first N failures */
		uncore->i915->params.mmio_debug--;
}

static inline void
unclaimed_reg_debug(struct intel_uncore *uncore,
		    const i915_reg_t reg,
		    const bool read,
		    const bool before)
{
	if (likely(!uncore->i915->params.mmio_debug))
		return;

	/* interrupts are disabled and re-enabled around uncore->lock usage */
	lockdep_assert_held(&uncore->lock);

	if (before)
		spin_lock(&uncore->debug->lock);

	__unclaimed_reg_debug(uncore, reg, read, before);

	if (!before)
		spin_unlock(&uncore->debug->lock);
}

#define GEN2_READ_HEADER(x) \
	u##x val = 0; \
	assert_rpm_wakelock_held(uncore->rpm);

#define GEN2_READ_FOOTER \
	trace_i915_reg_rw(false, reg, val, sizeof(val), trace); \
	return val

#define __gen2_read(x) \
static u##x \
gen2_read##x(struct intel_uncore *uncore, i915_reg_t reg, bool trace) { \
	GEN2_READ_HEADER(x); \
	val = __raw_uncore_read##x(uncore, reg); \
	GEN2_READ_FOOTER; \
}

#define __gen5_read(x) \
static u##x \
gen5_read##x(struct intel_uncore *uncore, i915_reg_t reg, bool trace) { \
	GEN2_READ_HEADER(x); \
	ilk_dummy_write(uncore); \
	val = __raw_uncore_read##x(uncore, reg); \
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
	assert_rpm_wakelock_held(uncore->rpm); \
	spin_lock_irqsave(&uncore->lock, irqflags); \
	unclaimed_reg_debug(uncore, reg, true, true)

#define GEN6_READ_FOOTER \
	unclaimed_reg_debug(uncore, reg, true, false); \
	spin_unlock_irqrestore(&uncore->lock, irqflags); \
	trace_i915_reg_rw(false, reg, val, sizeof(val), trace); \
	return val

static noinline void ___force_wake_auto(struct intel_uncore *uncore,
					enum forcewake_domains fw_domains)
{
	struct intel_uncore_forcewake_domain *domain;
	unsigned int tmp;

	GEM_BUG_ON(fw_domains & ~uncore->fw_domains);

	for_each_fw_domain_masked(domain, fw_domains, uncore, tmp)
		fw_domain_arm_timer(domain);

	uncore->funcs.force_wake_get(uncore, fw_domains);
}

static inline void __force_wake_auto(struct intel_uncore *uncore,
				     enum forcewake_domains fw_domains)
{
	GEM_BUG_ON(!fw_domains);

	/* Turn on all requested but inactive supported forcewake domains. */
	fw_domains &= uncore->fw_domains;
	fw_domains &= ~uncore->fw_domains_active;

	if (fw_domains)
		___force_wake_auto(uncore, fw_domains);
}

#define __gen_read(func, x) \
static u##x \
func##_read##x(struct intel_uncore *uncore, i915_reg_t reg, bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_READ_HEADER(x); \
	fw_engine = __##func##_reg_read_fw_domains(uncore, offset); \
	if (fw_engine) \
		__force_wake_auto(uncore, fw_engine); \
	val = __raw_uncore_read##x(uncore, reg); \
	GEN6_READ_FOOTER; \
}

#define __gen_reg_read_funcs(func) \
static enum forcewake_domains \
func##_reg_read_fw_domains(struct intel_uncore *uncore, i915_reg_t reg) { \
	return __##func##_reg_read_fw_domains(uncore, i915_mmio_reg_offset(reg)); \
} \
\
__gen_read(func, 8) \
__gen_read(func, 16) \
__gen_read(func, 32) \
__gen_read(func, 64)

__gen_reg_read_funcs(gen12_fwtable);
__gen_reg_read_funcs(gen11_fwtable);
__gen_reg_read_funcs(fwtable);
__gen_reg_read_funcs(gen6);

#undef __gen_reg_read_funcs
#undef GEN6_READ_FOOTER
#undef GEN6_READ_HEADER

#define GEN2_WRITE_HEADER \
	trace_i915_reg_rw(true, reg, val, sizeof(val), trace); \
	assert_rpm_wakelock_held(uncore->rpm); \

#define GEN2_WRITE_FOOTER

#define __gen2_write(x) \
static void \
gen2_write##x(struct intel_uncore *uncore, i915_reg_t reg, u##x val, bool trace) { \
	GEN2_WRITE_HEADER; \
	__raw_uncore_write##x(uncore, reg, val); \
	GEN2_WRITE_FOOTER; \
}

#define __gen5_write(x) \
static void \
gen5_write##x(struct intel_uncore *uncore, i915_reg_t reg, u##x val, bool trace) { \
	GEN2_WRITE_HEADER; \
	ilk_dummy_write(uncore); \
	__raw_uncore_write##x(uncore, reg, val); \
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
	assert_rpm_wakelock_held(uncore->rpm); \
	spin_lock_irqsave(&uncore->lock, irqflags); \
	unclaimed_reg_debug(uncore, reg, false, true)

#define GEN6_WRITE_FOOTER \
	unclaimed_reg_debug(uncore, reg, false, false); \
	spin_unlock_irqrestore(&uncore->lock, irqflags)

#define __gen6_write(x) \
static void \
gen6_write##x(struct intel_uncore *uncore, i915_reg_t reg, u##x val, bool trace) { \
	GEN6_WRITE_HEADER; \
	if (NEEDS_FORCE_WAKE(offset)) \
		__gen6_gt_wait_for_fifo(uncore); \
	__raw_uncore_write##x(uncore, reg, val); \
	GEN6_WRITE_FOOTER; \
}
__gen6_write(8)
__gen6_write(16)
__gen6_write(32)

#define __gen_write(func, x) \
static void \
func##_write##x(struct intel_uncore *uncore, i915_reg_t reg, u##x val, bool trace) { \
	enum forcewake_domains fw_engine; \
	GEN6_WRITE_HEADER; \
	fw_engine = __##func##_reg_write_fw_domains(uncore, offset); \
	if (fw_engine) \
		__force_wake_auto(uncore, fw_engine); \
	__raw_uncore_write##x(uncore, reg, val); \
	GEN6_WRITE_FOOTER; \
}

#define __gen_reg_write_funcs(func) \
static enum forcewake_domains \
func##_reg_write_fw_domains(struct intel_uncore *uncore, i915_reg_t reg) { \
	return __##func##_reg_write_fw_domains(uncore, i915_mmio_reg_offset(reg)); \
} \
\
__gen_write(func, 8) \
__gen_write(func, 16) \
__gen_write(func, 32)

__gen_reg_write_funcs(gen12_fwtable);
__gen_reg_write_funcs(gen11_fwtable);
__gen_reg_write_funcs(fwtable);
__gen_reg_write_funcs(gen8);

#undef __gen_reg_write_funcs
#undef GEN6_WRITE_FOOTER
#undef GEN6_WRITE_HEADER

#define ASSIGN_RAW_WRITE_MMIO_VFUNCS(uncore, x) \
do { \
	(uncore)->funcs.mmio_writeb = x##_write8; \
	(uncore)->funcs.mmio_writew = x##_write16; \
	(uncore)->funcs.mmio_writel = x##_write32; \
} while (0)

#define ASSIGN_RAW_READ_MMIO_VFUNCS(uncore, x) \
do { \
	(uncore)->funcs.mmio_readb = x##_read8; \
	(uncore)->funcs.mmio_readw = x##_read16; \
	(uncore)->funcs.mmio_readl = x##_read32; \
	(uncore)->funcs.mmio_readq = x##_read64; \
} while (0)

#define ASSIGN_WRITE_MMIO_VFUNCS(uncore, x) \
do { \
	ASSIGN_RAW_WRITE_MMIO_VFUNCS((uncore), x); \
	(uncore)->funcs.write_fw_domains = x##_reg_write_fw_domains; \
} while (0)

#define ASSIGN_READ_MMIO_VFUNCS(uncore, x) \
do { \
	ASSIGN_RAW_READ_MMIO_VFUNCS(uncore, x); \
	(uncore)->funcs.read_fw_domains = x##_reg_read_fw_domains; \
} while (0)

static int __fw_domain_init(struct intel_uncore *uncore,
			    enum forcewake_domain_id domain_id,
			    i915_reg_t reg_set,
			    i915_reg_t reg_ack)
{
	struct intel_uncore_forcewake_domain *d;

	GEM_BUG_ON(domain_id >= FW_DOMAIN_ID_COUNT);
	GEM_BUG_ON(uncore->fw_domain[domain_id]);

	if (i915_inject_probe_failure(uncore->i915))
		return -ENOMEM;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	drm_WARN_ON(&uncore->i915->drm, !i915_mmio_reg_valid(reg_set));
	drm_WARN_ON(&uncore->i915->drm, !i915_mmio_reg_valid(reg_ack));

	d->uncore = uncore;
	d->wake_count = 0;
	d->reg_set = uncore->regs + i915_mmio_reg_offset(reg_set);
	d->reg_ack = uncore->regs + i915_mmio_reg_offset(reg_ack);

	d->id = domain_id;

	BUILD_BUG_ON(FORCEWAKE_RENDER != (1 << FW_DOMAIN_ID_RENDER));
	BUILD_BUG_ON(FORCEWAKE_GT != (1 << FW_DOMAIN_ID_GT));
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

	uncore->fw_domains |= BIT(domain_id);

	fw_domain_reset(d);

	uncore->fw_domain[domain_id] = d;

	return 0;
}

static void fw_domain_fini(struct intel_uncore *uncore,
			   enum forcewake_domain_id domain_id)
{
	struct intel_uncore_forcewake_domain *d;

	GEM_BUG_ON(domain_id >= FW_DOMAIN_ID_COUNT);

	d = fetch_and_zero(&uncore->fw_domain[domain_id]);
	if (!d)
		return;

	uncore->fw_domains &= ~BIT(domain_id);
	drm_WARN_ON(&uncore->i915->drm, d->wake_count);
	drm_WARN_ON(&uncore->i915->drm, hrtimer_cancel(&d->timer));
	kfree(d);
}

static void intel_uncore_fw_domains_fini(struct intel_uncore *uncore)
{
	struct intel_uncore_forcewake_domain *d;
	int tmp;

	for_each_fw_domain(d, uncore, tmp)
		fw_domain_fini(uncore, d->id);
}

static int intel_uncore_fw_domains_init(struct intel_uncore *uncore)
{
	struct drm_i915_private *i915 = uncore->i915;
	int ret = 0;

	GEM_BUG_ON(!intel_uncore_has_forcewake(uncore));

#define fw_domain_init(uncore__, id__, set__, ack__) \
	(ret ?: (ret = __fw_domain_init((uncore__), (id__), (set__), (ack__))))

	if (INTEL_GEN(i915) >= 11) {
		/* we'll prune the domains of missing engines later */
		intel_engine_mask_t emask = INTEL_INFO(i915)->platform_engine_mask;
		int i;

		uncore->funcs.force_wake_get = fw_domains_get_with_fallback;
		uncore->funcs.force_wake_put = fw_domains_put;
		fw_domain_init(uncore, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE_RENDER_GEN9,
			       FORCEWAKE_ACK_RENDER_GEN9);
		fw_domain_init(uncore, FW_DOMAIN_ID_GT,
			       FORCEWAKE_GT_GEN9,
			       FORCEWAKE_ACK_GT_GEN9);

		for (i = 0; i < I915_MAX_VCS; i++) {
			if (!__HAS_ENGINE(emask, _VCS(i)))
				continue;

			fw_domain_init(uncore, FW_DOMAIN_ID_MEDIA_VDBOX0 + i,
				       FORCEWAKE_MEDIA_VDBOX_GEN11(i),
				       FORCEWAKE_ACK_MEDIA_VDBOX_GEN11(i));
		}
		for (i = 0; i < I915_MAX_VECS; i++) {
			if (!__HAS_ENGINE(emask, _VECS(i)))
				continue;

			fw_domain_init(uncore, FW_DOMAIN_ID_MEDIA_VEBOX0 + i,
				       FORCEWAKE_MEDIA_VEBOX_GEN11(i),
				       FORCEWAKE_ACK_MEDIA_VEBOX_GEN11(i));
		}
	} else if (IS_GEN_RANGE(i915, 9, 10)) {
		uncore->funcs.force_wake_get = fw_domains_get_with_fallback;
		uncore->funcs.force_wake_put = fw_domains_put;
		fw_domain_init(uncore, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE_RENDER_GEN9,
			       FORCEWAKE_ACK_RENDER_GEN9);
		fw_domain_init(uncore, FW_DOMAIN_ID_GT,
			       FORCEWAKE_GT_GEN9,
			       FORCEWAKE_ACK_GT_GEN9);
		fw_domain_init(uncore, FW_DOMAIN_ID_MEDIA,
			       FORCEWAKE_MEDIA_GEN9, FORCEWAKE_ACK_MEDIA_GEN9);
	} else if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915)) {
		uncore->funcs.force_wake_get = fw_domains_get;
		uncore->funcs.force_wake_put = fw_domains_put;
		fw_domain_init(uncore, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE_VLV, FORCEWAKE_ACK_VLV);
		fw_domain_init(uncore, FW_DOMAIN_ID_MEDIA,
			       FORCEWAKE_MEDIA_VLV, FORCEWAKE_ACK_MEDIA_VLV);
	} else if (IS_HASWELL(i915) || IS_BROADWELL(i915)) {
		uncore->funcs.force_wake_get =
			fw_domains_get_with_thread_status;
		uncore->funcs.force_wake_put = fw_domains_put;
		fw_domain_init(uncore, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE_MT, FORCEWAKE_ACK_HSW);
	} else if (IS_IVYBRIDGE(i915)) {
		u32 ecobus;

		/* IVB configs may use multi-threaded forcewake */

		/* A small trick here - if the bios hasn't configured
		 * MT forcewake, and if the device is in RC6, then
		 * force_wake_mt_get will not wake the device and the
		 * ECOBUS read will return zero. Which will be
		 * (correctly) interpreted by the test below as MT
		 * forcewake being disabled.
		 */
		uncore->funcs.force_wake_get =
			fw_domains_get_with_thread_status;
		uncore->funcs.force_wake_put = fw_domains_put;

		/* We need to init first for ECOBUS access and then
		 * determine later if we want to reinit, in case of MT access is
		 * not working. In this stage we don't know which flavour this
		 * ivb is, so it is better to reset also the gen6 fw registers
		 * before the ecobus check.
		 */

		__raw_uncore_write32(uncore, FORCEWAKE, 0);
		__raw_posting_read(uncore, ECOBUS);

		ret = __fw_domain_init(uncore, FW_DOMAIN_ID_RENDER,
				       FORCEWAKE_MT, FORCEWAKE_MT_ACK);
		if (ret)
			goto out;

		spin_lock_irq(&uncore->lock);
		fw_domains_get_with_thread_status(uncore, FORCEWAKE_RENDER);
		ecobus = __raw_uncore_read32(uncore, ECOBUS);
		fw_domains_put(uncore, FORCEWAKE_RENDER);
		spin_unlock_irq(&uncore->lock);

		if (!(ecobus & FORCEWAKE_MT_ENABLE)) {
			drm_info(&i915->drm, "No MT forcewake available on Ivybridge, this can result in issues\n");
			drm_info(&i915->drm, "when using vblank-synced partial screen updates.\n");
			fw_domain_fini(uncore, FW_DOMAIN_ID_RENDER);
			fw_domain_init(uncore, FW_DOMAIN_ID_RENDER,
				       FORCEWAKE, FORCEWAKE_ACK);
		}
	} else if (IS_GEN(i915, 6)) {
		uncore->funcs.force_wake_get =
			fw_domains_get_with_thread_status;
		uncore->funcs.force_wake_put = fw_domains_put;
		fw_domain_init(uncore, FW_DOMAIN_ID_RENDER,
			       FORCEWAKE, FORCEWAKE_ACK);
	}

#undef fw_domain_init

	/* All future platforms are expected to require complex power gating */
	drm_WARN_ON(&i915->drm, !ret && uncore->fw_domains == 0);

out:
	if (ret)
		intel_uncore_fw_domains_fini(uncore);

	return ret;
}

#define ASSIGN_FW_DOMAINS_TABLE(uncore, d) \
{ \
	(uncore)->fw_domains_table = \
			(struct intel_forcewake_range *)(d); \
	(uncore)->fw_domains_table_entries = ARRAY_SIZE((d)); \
}

static int i915_pmic_bus_access_notifier(struct notifier_block *nb,
					 unsigned long action, void *data)
{
	struct intel_uncore *uncore = container_of(nb,
			struct intel_uncore, pmic_bus_access_nb);

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
		disable_rpm_wakeref_asserts(uncore->rpm);
		intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);
		enable_rpm_wakeref_asserts(uncore->rpm);
		break;
	case MBI_PMIC_BUS_ACCESS_END:
		intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);
		break;
	}

	return NOTIFY_OK;
}

static int uncore_mmio_setup(struct intel_uncore *uncore)
{
	struct drm_i915_private *i915 = uncore->i915;
	struct pci_dev *pdev = i915->drm.pdev;
	int mmio_bar;
	int mmio_size;

	mmio_bar = IS_GEN(i915, 2) ? 1 : 0;
	/*
	 * Before gen4, the registers and the GTT are behind different BARs.
	 * However, from gen4 onwards, the registers and the GTT are shared
	 * in the same BAR, so we want to restrict this ioremap from
	 * clobbering the GTT which we want ioremap_wc instead. Fortunately,
	 * the register BAR remains the same size for all the earlier
	 * generations up to Ironlake.
	 * For dgfx chips register range is expanded to 4MB.
	 */
	if (INTEL_GEN(i915) < 5)
		mmio_size = 512 * 1024;
	else if (IS_DGFX(i915))
		mmio_size = 4 * 1024 * 1024;
	else
		mmio_size = 2 * 1024 * 1024;

	uncore->regs = pci_iomap(pdev, mmio_bar, mmio_size);
	if (uncore->regs == NULL) {
		drm_err(&i915->drm, "failed to map registers\n");
		return -EIO;
	}

	return 0;
}

static void uncore_mmio_cleanup(struct intel_uncore *uncore)
{
	struct pci_dev *pdev = uncore->i915->drm.pdev;

	pci_iounmap(pdev, uncore->regs);
}

void intel_uncore_init_early(struct intel_uncore *uncore,
			     struct drm_i915_private *i915)
{
	spin_lock_init(&uncore->lock);
	uncore->i915 = i915;
	uncore->rpm = &i915->runtime_pm;
	uncore->debug = &i915->mmio_debug;
}

static void uncore_raw_init(struct intel_uncore *uncore)
{
	GEM_BUG_ON(intel_uncore_has_forcewake(uncore));

	if (IS_GEN(uncore->i915, 5)) {
		ASSIGN_RAW_WRITE_MMIO_VFUNCS(uncore, gen5);
		ASSIGN_RAW_READ_MMIO_VFUNCS(uncore, gen5);
	} else {
		ASSIGN_RAW_WRITE_MMIO_VFUNCS(uncore, gen2);
		ASSIGN_RAW_READ_MMIO_VFUNCS(uncore, gen2);
	}
}

static int uncore_forcewake_init(struct intel_uncore *uncore)
{
	struct drm_i915_private *i915 = uncore->i915;
	int ret;

	GEM_BUG_ON(!intel_uncore_has_forcewake(uncore));

	ret = intel_uncore_fw_domains_init(uncore);
	if (ret)
		return ret;
	forcewake_early_sanitize(uncore, 0);

	if (IS_GEN_RANGE(i915, 6, 7)) {
		ASSIGN_WRITE_MMIO_VFUNCS(uncore, gen6);

		if (IS_VALLEYVIEW(i915)) {
			ASSIGN_FW_DOMAINS_TABLE(uncore, __vlv_fw_ranges);
			ASSIGN_READ_MMIO_VFUNCS(uncore, fwtable);
		} else {
			ASSIGN_READ_MMIO_VFUNCS(uncore, gen6);
		}
	} else if (IS_GEN(i915, 8)) {
		if (IS_CHERRYVIEW(i915)) {
			ASSIGN_FW_DOMAINS_TABLE(uncore, __chv_fw_ranges);
			ASSIGN_WRITE_MMIO_VFUNCS(uncore, fwtable);
			ASSIGN_READ_MMIO_VFUNCS(uncore, fwtable);
		} else {
			ASSIGN_WRITE_MMIO_VFUNCS(uncore, gen8);
			ASSIGN_READ_MMIO_VFUNCS(uncore, gen6);
		}
	} else if (IS_GEN_RANGE(i915, 9, 10)) {
		ASSIGN_FW_DOMAINS_TABLE(uncore, __gen9_fw_ranges);
		ASSIGN_WRITE_MMIO_VFUNCS(uncore, fwtable);
		ASSIGN_READ_MMIO_VFUNCS(uncore, fwtable);
	} else if (IS_GEN(i915, 11)) {
		ASSIGN_FW_DOMAINS_TABLE(uncore, __gen11_fw_ranges);
		ASSIGN_WRITE_MMIO_VFUNCS(uncore, gen11_fwtable);
		ASSIGN_READ_MMIO_VFUNCS(uncore, gen11_fwtable);
	} else {
		ASSIGN_FW_DOMAINS_TABLE(uncore, __gen12_fw_ranges);
		ASSIGN_WRITE_MMIO_VFUNCS(uncore, gen12_fwtable);
		ASSIGN_READ_MMIO_VFUNCS(uncore, gen12_fwtable);
	}

	uncore->pmic_bus_access_nb.notifier_call = i915_pmic_bus_access_notifier;
	iosf_mbi_register_pmic_bus_access_notifier(&uncore->pmic_bus_access_nb);

	return 0;
}

int intel_uncore_init_mmio(struct intel_uncore *uncore)
{
	struct drm_i915_private *i915 = uncore->i915;
	int ret;

	ret = uncore_mmio_setup(uncore);
	if (ret)
		return ret;

	if (INTEL_GEN(i915) > 5 && !intel_vgpu_active(i915))
		uncore->flags |= UNCORE_HAS_FORCEWAKE;

	if (!intel_uncore_has_forcewake(uncore)) {
		uncore_raw_init(uncore);
	} else {
		ret = uncore_forcewake_init(uncore);
		if (ret)
			goto out_mmio_cleanup;
	}

	/* make sure fw funcs are set if and only if we have fw*/
	GEM_BUG_ON(intel_uncore_has_forcewake(uncore) != !!uncore->funcs.force_wake_get);
	GEM_BUG_ON(intel_uncore_has_forcewake(uncore) != !!uncore->funcs.force_wake_put);
	GEM_BUG_ON(intel_uncore_has_forcewake(uncore) != !!uncore->funcs.read_fw_domains);
	GEM_BUG_ON(intel_uncore_has_forcewake(uncore) != !!uncore->funcs.write_fw_domains);

	if (HAS_FPGA_DBG_UNCLAIMED(i915))
		uncore->flags |= UNCORE_HAS_FPGA_DBG_UNCLAIMED;

	if (IS_VALLEYVIEW(i915) || IS_CHERRYVIEW(i915))
		uncore->flags |= UNCORE_HAS_DBG_UNCLAIMED;

	if (IS_GEN_RANGE(i915, 6, 7))
		uncore->flags |= UNCORE_HAS_FIFO;

	/* clear out unclaimed reg detection bit */
	if (intel_uncore_unclaimed_mmio(uncore))
		drm_dbg(&i915->drm, "unclaimed mmio detected on uncore init, clearing\n");

	return 0;

out_mmio_cleanup:
	uncore_mmio_cleanup(uncore);

	return ret;
}

/*
 * We might have detected that some engines are fused off after we initialized
 * the forcewake domains. Prune them, to make sure they only reference existing
 * engines.
 */
void intel_uncore_prune_engine_fw_domains(struct intel_uncore *uncore,
					  struct intel_gt *gt)
{
	enum forcewake_domains fw_domains = uncore->fw_domains;
	enum forcewake_domain_id domain_id;
	int i;

	if (!intel_uncore_has_forcewake(uncore) || INTEL_GEN(uncore->i915) < 11)
		return;

	for (i = 0; i < I915_MAX_VCS; i++) {
		domain_id = FW_DOMAIN_ID_MEDIA_VDBOX0 + i;

		if (HAS_ENGINE(gt, _VCS(i)))
			continue;

		if (fw_domains & BIT(domain_id))
			fw_domain_fini(uncore, domain_id);
	}

	for (i = 0; i < I915_MAX_VECS; i++) {
		domain_id = FW_DOMAIN_ID_MEDIA_VEBOX0 + i;

		if (HAS_ENGINE(gt, _VECS(i)))
			continue;

		if (fw_domains & BIT(domain_id))
			fw_domain_fini(uncore, domain_id);
	}
}

void intel_uncore_fini_mmio(struct intel_uncore *uncore)
{
	if (intel_uncore_has_forcewake(uncore)) {
		iosf_mbi_punit_acquire();
		iosf_mbi_unregister_pmic_bus_access_notifier_unlocked(
			&uncore->pmic_bus_access_nb);
		intel_uncore_forcewake_reset(uncore);
		intel_uncore_fw_domains_fini(uncore);
		iosf_mbi_punit_release();
	}

	uncore_mmio_cleanup(uncore);
}

static const struct reg_whitelist {
	i915_reg_t offset_ldw;
	i915_reg_t offset_udw;
	u16 gen_mask;
	u8 size;
} reg_read_whitelist[] = { {
	.offset_ldw = RING_TIMESTAMP(RENDER_RING_BASE),
	.offset_udw = RING_TIMESTAMP_UDW(RENDER_RING_BASE),
	.gen_mask = INTEL_GEN_MASK(4, 12),
	.size = 8
} };

int i915_reg_read_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct intel_uncore *uncore = &i915->uncore;
	struct drm_i915_reg_read *reg = data;
	struct reg_whitelist const *entry;
	intel_wakeref_t wakeref;
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

		if (INTEL_INFO(i915)->gen_mask & entry->gen_mask &&
		    entry_offset == (reg->offset & -entry->size))
			break;
		entry++;
		remain--;
	}

	if (!remain)
		return -EINVAL;

	flags = reg->offset & (entry->size - 1);

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		if (entry->size == 8 && flags == I915_REG_READ_8B_WA)
			reg->val = intel_uncore_read64_2x32(uncore,
							    entry->offset_ldw,
							    entry->offset_udw);
		else if (entry->size == 8 && flags == 0)
			reg->val = intel_uncore_read64(uncore,
						       entry->offset_ldw);
		else if (entry->size == 4 && flags == 0)
			reg->val = intel_uncore_read(uncore, entry->offset_ldw);
		else if (entry->size == 2 && flags == 0)
			reg->val = intel_uncore_read16(uncore,
						       entry->offset_ldw);
		else if (entry->size == 1 && flags == 0)
			reg->val = intel_uncore_read8(uncore,
						      entry->offset_ldw);
		else
			ret = -EINVAL;
	}

	return ret;
}

/**
 * __intel_wait_for_register_fw - wait until register matches expected state
 * @uncore: the struct intel_uncore
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
 * Return: 0 if the register matches the desired condition, or -ETIMEDOUT.
 */
int __intel_wait_for_register_fw(struct intel_uncore *uncore,
				 i915_reg_t reg,
				 u32 mask,
				 u32 value,
				 unsigned int fast_timeout_us,
				 unsigned int slow_timeout_ms,
				 u32 *out_value)
{
	u32 reg_value = 0;
#define done (((reg_value = intel_uncore_read_fw(uncore, reg)) & mask) == value)
	int ret;

	/* Catch any overuse of this function */
	might_sleep_if(slow_timeout_ms);
	GEM_BUG_ON(fast_timeout_us > 20000);
	GEM_BUG_ON(!fast_timeout_us && !slow_timeout_ms);

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
 * @uncore: the struct intel_uncore
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
 * Return: 0 if the register matches the desired condition, or -ETIMEDOUT.
 */
int __intel_wait_for_register(struct intel_uncore *uncore,
			      i915_reg_t reg,
			      u32 mask,
			      u32 value,
			      unsigned int fast_timeout_us,
			      unsigned int slow_timeout_ms,
			      u32 *out_value)
{
	unsigned fw =
		intel_uncore_forcewake_for_reg(uncore, reg, FW_REG_READ);
	u32 reg_value;
	int ret;

	might_sleep_if(slow_timeout_ms);

	spin_lock_irq(&uncore->lock);
	intel_uncore_forcewake_get__locked(uncore, fw);

	ret = __intel_wait_for_register_fw(uncore,
					   reg, mask, value,
					   fast_timeout_us, 0, &reg_value);

	intel_uncore_forcewake_put__locked(uncore, fw);
	spin_unlock_irq(&uncore->lock);

	if (ret && slow_timeout_ms)
		ret = __wait_for(reg_value = intel_uncore_read_notrace(uncore,
								       reg),
				 (reg_value & mask) == value,
				 slow_timeout_ms * 1000, 10, 1000);

	/* just trace the final value */
	trace_i915_reg_rw(false, reg, reg_value, sizeof(reg_value), true);

	if (out_value)
		*out_value = reg_value;

	return ret;
}

bool intel_uncore_unclaimed_mmio(struct intel_uncore *uncore)
{
	bool ret;

	spin_lock_irq(&uncore->debug->lock);
	ret = check_for_unclaimed_mmio(uncore);
	spin_unlock_irq(&uncore->debug->lock);

	return ret;
}

bool
intel_uncore_arm_unclaimed_mmio_detection(struct intel_uncore *uncore)
{
	bool ret = false;

	spin_lock_irq(&uncore->debug->lock);

	if (unlikely(uncore->debug->unclaimed_mmio_check <= 0))
		goto out;

	if (unlikely(check_for_unclaimed_mmio(uncore))) {
		if (!uncore->i915->params.mmio_debug) {
			drm_dbg(&uncore->i915->drm,
				"Unclaimed register detected, "
				"enabling oneshot unclaimed register reporting. "
				"Please use i915.mmio_debug=N for more information.\n");
			uncore->i915->params.mmio_debug++;
		}
		uncore->debug->unclaimed_mmio_check--;
		ret = true;
	}

out:
	spin_unlock_irq(&uncore->debug->lock);

	return ret;
}

/**
 * intel_uncore_forcewake_for_reg - which forcewake domains are needed to access
 * 				    a register
 * @uncore: pointer to struct intel_uncore
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
intel_uncore_forcewake_for_reg(struct intel_uncore *uncore,
			       i915_reg_t reg, unsigned int op)
{
	enum forcewake_domains fw_domains = 0;

	drm_WARN_ON(&uncore->i915->drm, !op);

	if (!intel_uncore_has_forcewake(uncore))
		return 0;

	if (op & FW_REG_READ)
		fw_domains = uncore->funcs.read_fw_domains(uncore, reg);

	if (op & FW_REG_WRITE)
		fw_domains |= uncore->funcs.write_fw_domains(uncore, reg);

	drm_WARN_ON(&uncore->i915->drm, fw_domains & ~uncore->fw_domains);

	return fw_domains;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_uncore.c"
#include "selftests/intel_uncore.c"
#endif
