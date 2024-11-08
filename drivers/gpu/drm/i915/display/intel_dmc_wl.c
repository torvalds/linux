// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2024 Intel Corporation
 */

#include <linux/kernel.h>

#include "i915_reg.h"
#include "intel_de.h"
#include "intel_dmc.h"
#include "intel_dmc_regs.h"
#include "intel_dmc_wl.h"

/**
 * DOC: DMC wakelock support
 *
 * Wake lock is the mechanism to cause display engine to exit DC
 * states to allow programming to registers that are powered down in
 * those states. Previous projects exited DC states automatically when
 * detecting programming. Now software controls the exit by
 * programming the wake lock. This improves system performance and
 * system interactions and better fits the flip queue style of
 * programming. Wake lock is only required when DC5, DC6, or DC6v have
 * been enabled in DC_STATE_EN and the wake lock mode of operation has
 * been enabled.
 *
 * The wakelock mechanism in DMC allows the display engine to exit DC
 * states explicitly before programming registers that may be powered
 * down.  In earlier hardware, this was done automatically and
 * implicitly when the display engine accessed a register.  With the
 * wakelock implementation, the driver asserts a wakelock in DMC,
 * which forces it to exit the DC state until the wakelock is
 * deasserted.
 *
 * The mechanism can be enabled and disabled by writing to the
 * DMC_WAKELOCK_CFG register.  There are also 13 control registers
 * that can be used to hold and release different wakelocks.  In the
 * current implementation, we only need one wakelock, so only
 * DMC_WAKELOCK1_CTL is used.  The other definitions are here for
 * potential future use.
 */

/*
 * Define DMC_WAKELOCK_CTL_TIMEOUT_US in microseconds because we use the
 * atomic variant of waiting MMIO.
 */
#define DMC_WAKELOCK_CTL_TIMEOUT_US 5000
#define DMC_WAKELOCK_HOLD_TIME 50

struct intel_dmc_wl_range {
	u32 start;
	u32 end;
};

static struct intel_dmc_wl_range powered_off_ranges[] = {
	{ .start = 0x60000, .end = 0x7ffff },
	{},
};

static struct intel_dmc_wl_range xe3lpd_dc5_dc6_dmc_ranges[] = {
	{ .start = 0x45500 }, /* DC_STATE_SEL */
	{ .start = 0x457a0, .end = 0x457b0 }, /* DC*_RESIDENCY_COUNTER */
	{ .start = 0x45504 }, /* DC_STATE_EN */
	{ .start = 0x45400, .end = 0x4540c }, /* PWR_WELL_CTL_* */
	{ .start = 0x454f0 }, /* RETENTION_CTRL */

	/* DBUF_CTL_* */
	{ .start = 0x44300 },
	{ .start = 0x44304 },
	{ .start = 0x44f00 },
	{ .start = 0x44f04 },
	{ .start = 0x44fe8 },
	{ .start = 0x45008 },

	{ .start = 0x46070 }, /* CDCLK_PLL_ENABLE */
	{ .start = 0x46000 }, /* CDCLK_CTL */
	{ .start = 0x46008 }, /* CDCLK_SQUASH_CTL */

	/* TRANS_CMTG_CTL_* */
	{ .start = 0x6fa88 },
	{ .start = 0x6fb88 },

	{ .start = 0x46430 }, /* CHICKEN_DCPR_1 */
	{ .start = 0x46434 }, /* CHICKEN_DCPR_2 */
	{ .start = 0x454a0 }, /* CHICKEN_DCPR_4 */
	{ .start = 0x42084 }, /* CHICKEN_MISC_2 */
	{ .start = 0x42088 }, /* CHICKEN_MISC_3 */
	{ .start = 0x46160 }, /* CMTG_CLK_SEL */
	{ .start = 0x8f000, .end = 0x8ffff }, /* Main DMC registers */

	{},
};

static struct intel_dmc_wl_range xe3lpd_dc3co_dmc_ranges[] = {
	{ .start = 0x454a0 }, /* CHICKEN_DCPR_4 */

	{ .start = 0x45504 }, /* DC_STATE_EN */

	/* DBUF_CTL_* */
	{ .start = 0x44300 },
	{ .start = 0x44304 },
	{ .start = 0x44f00 },
	{ .start = 0x44f04 },
	{ .start = 0x44fe8 },
	{ .start = 0x45008 },

	{ .start = 0x46070 }, /* CDCLK_PLL_ENABLE */
	{ .start = 0x46000 }, /* CDCLK_CTL */
	{ .start = 0x46008 }, /* CDCLK_SQUASH_CTL */
	{ .start = 0x8f000, .end = 0x8ffff }, /* Main DMC registers */

	/* Scanline registers */
	{ .start = 0x70000 },
	{ .start = 0x70004 },
	{ .start = 0x70014 },
	{ .start = 0x70018 },
	{ .start = 0x71000 },
	{ .start = 0x71004 },
	{ .start = 0x71014 },
	{ .start = 0x71018 },
	{ .start = 0x72000 },
	{ .start = 0x72004 },
	{ .start = 0x72014 },
	{ .start = 0x72018 },
	{ .start = 0x73000 },
	{ .start = 0x73004 },
	{ .start = 0x73014 },
	{ .start = 0x73018 },
	{ .start = 0x7b000 },
	{ .start = 0x7b004 },
	{ .start = 0x7b014 },
	{ .start = 0x7b018 },
	{ .start = 0x7c000 },
	{ .start = 0x7c004 },
	{ .start = 0x7c014 },
	{ .start = 0x7c018 },

	{},
};

static void __intel_dmc_wl_release(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct intel_dmc_wl *wl = &display->wl;

	WARN_ON(refcount_read(&wl->refcount));

	queue_delayed_work(i915->unordered_wq, &wl->work,
			   msecs_to_jiffies(DMC_WAKELOCK_HOLD_TIME));
}

static void intel_dmc_wl_work(struct work_struct *work)
{
	struct intel_dmc_wl *wl =
		container_of(work, struct intel_dmc_wl, work.work);
	struct intel_display *display =
		container_of(wl, struct intel_display, wl);
	unsigned long flags;

	spin_lock_irqsave(&wl->lock, flags);

	/*
	 * Bail out if refcount became non-zero while waiting for the spinlock,
	 * meaning that the lock is now taken again.
	 */
	if (refcount_read(&wl->refcount))
		goto out_unlock;

	__intel_de_rmw_nowl(display, DMC_WAKELOCK1_CTL, DMC_WAKELOCK_CTL_REQ, 0);

	if (__intel_de_wait_for_register_atomic_nowl(display, DMC_WAKELOCK1_CTL,
						     DMC_WAKELOCK_CTL_ACK, 0,
						     DMC_WAKELOCK_CTL_TIMEOUT_US)) {
		WARN_RATELIMIT(1, "DMC wakelock release timed out");
		goto out_unlock;
	}

	wl->taken = false;

out_unlock:
	spin_unlock_irqrestore(&wl->lock, flags);
}

static void __intel_dmc_wl_take(struct intel_display *display)
{
	struct intel_dmc_wl *wl = &display->wl;

	/*
	 * Only try to take the wakelock if it's not marked as taken
	 * yet.  It may be already taken at this point if we have
	 * already released the last reference, but the work has not
	 * run yet.
	 */
	if (wl->taken)
		return;

	__intel_de_rmw_nowl(display, DMC_WAKELOCK1_CTL, 0,
			    DMC_WAKELOCK_CTL_REQ);

	/*
	 * We need to use the atomic variant of the waiting routine
	 * because the DMC wakelock is also taken in atomic context.
	 */
	if (__intel_de_wait_for_register_atomic_nowl(display, DMC_WAKELOCK1_CTL,
						     DMC_WAKELOCK_CTL_ACK,
						     DMC_WAKELOCK_CTL_ACK,
						     DMC_WAKELOCK_CTL_TIMEOUT_US)) {
		WARN_RATELIMIT(1, "DMC wakelock ack timed out");
		return;
	}

	wl->taken = true;
}

static bool intel_dmc_wl_reg_in_range(i915_reg_t reg,
				      const struct intel_dmc_wl_range ranges[])
{
	u32 offset = i915_mmio_reg_offset(reg);

	for (int i = 0; ranges[i].start; i++) {
		u32 end = ranges[i].end ?: ranges[i].start;

		if (ranges[i].start <= offset && offset <= end)
			return true;
	}

	return false;
}

static bool intel_dmc_wl_check_range(i915_reg_t reg, u32 dc_state)
{
	const struct intel_dmc_wl_range *ranges;

	/*
	 * Check that the offset is in one of the ranges for which
	 * registers are powered off during DC states.
	 */
	if (intel_dmc_wl_reg_in_range(reg, powered_off_ranges))
		return true;

	/*
	 * Check that the offset is for a register that is touched by
	 * the DMC and requires a DC exit for proper access.
	 */
	switch (dc_state) {
	case DC_STATE_EN_DC3CO:
		ranges = xe3lpd_dc3co_dmc_ranges;
		break;
	case DC_STATE_EN_UPTO_DC5:
	case DC_STATE_EN_UPTO_DC6:
		ranges = xe3lpd_dc5_dc6_dmc_ranges;
		break;
	default:
		ranges = NULL;
	}

	if (ranges && intel_dmc_wl_reg_in_range(reg, ranges))
		return true;

	return false;
}

static bool __intel_dmc_wl_supported(struct intel_display *display)
{
	if (!HAS_DMC_WAKELOCK(display) ||
	    !intel_dmc_has_payload(display) ||
	    !display->params.enable_dmc_wl)
		return false;

	return true;
}

void intel_dmc_wl_init(struct intel_display *display)
{
	struct intel_dmc_wl *wl = &display->wl;

	/* don't call __intel_dmc_wl_supported(), DMC is not loaded yet */
	if (!HAS_DMC_WAKELOCK(display) || !display->params.enable_dmc_wl)
		return;

	INIT_DELAYED_WORK(&wl->work, intel_dmc_wl_work);
	spin_lock_init(&wl->lock);
	refcount_set(&wl->refcount, 0);
}

/* Must only be called as part of enabling dynamic DC states. */
void intel_dmc_wl_enable(struct intel_display *display, u32 dc_state)
{
	struct intel_dmc_wl *wl = &display->wl;
	unsigned long flags;

	if (!__intel_dmc_wl_supported(display))
		return;

	spin_lock_irqsave(&wl->lock, flags);

	wl->dc_state = dc_state;

	if (drm_WARN_ON(display->drm, wl->enabled))
		goto out_unlock;

	/*
	 * Enable wakelock in DMC.  We shouldn't try to take the
	 * wakelock, because we're just enabling it, so call the
	 * non-locking version directly here.
	 */
	__intel_de_rmw_nowl(display, DMC_WAKELOCK_CFG, 0, DMC_WAKELOCK_CFG_ENABLE);

	wl->enabled = true;

	/*
	 * This would be racy in the following scenario:
	 *
	 *   1. Function A calls intel_dmc_wl_get();
	 *   2. Some function calls intel_dmc_wl_disable();
	 *   3. Some function calls intel_dmc_wl_enable();
	 *   4. Concurrently with (3), function A performs the MMIO in between
	 *      setting DMC_WAKELOCK_CFG_ENABLE and asserting the lock with
	 *      __intel_dmc_wl_take().
	 *
	 * TODO: Check with the hardware team whether it is safe to assert the
	 * hardware lock before enabling to avoid such a scenario. Otherwise, we
	 * would need to deal with it via software synchronization.
	 */
	if (refcount_read(&wl->refcount))
		__intel_dmc_wl_take(display);

out_unlock:
	spin_unlock_irqrestore(&wl->lock, flags);
}

/* Must only be called as part of disabling dynamic DC states. */
void intel_dmc_wl_disable(struct intel_display *display)
{
	struct intel_dmc_wl *wl = &display->wl;
	unsigned long flags;

	if (!__intel_dmc_wl_supported(display))
		return;

	flush_delayed_work(&wl->work);

	spin_lock_irqsave(&wl->lock, flags);

	if (drm_WARN_ON(display->drm, !wl->enabled))
		goto out_unlock;

	/* Disable wakelock in DMC */
	__intel_de_rmw_nowl(display, DMC_WAKELOCK_CFG, DMC_WAKELOCK_CFG_ENABLE, 0);

	wl->enabled = false;

	/*
	 * The spec is not explicit about the expectation of existing
	 * lock users at the moment of disabling, but it does say that we must
	 * clear DMC_WAKELOCK_CTL_REQ, which gives us a clue that it is okay to
	 * disable with existing lock users.
	 *
	 * TODO: Get the correct expectation from the hardware team.
	 */
	__intel_de_rmw_nowl(display, DMC_WAKELOCK1_CTL, DMC_WAKELOCK_CTL_REQ, 0);

	wl->taken = false;

out_unlock:
	spin_unlock_irqrestore(&wl->lock, flags);
}

void intel_dmc_wl_get(struct intel_display *display, i915_reg_t reg)
{
	struct intel_dmc_wl *wl = &display->wl;
	unsigned long flags;

	if (!__intel_dmc_wl_supported(display))
		return;

	spin_lock_irqsave(&wl->lock, flags);

	if (i915_mmio_reg_valid(reg) && !intel_dmc_wl_check_range(reg, wl->dc_state))
		goto out_unlock;

	if (!wl->enabled) {
		if (!refcount_inc_not_zero(&wl->refcount))
			refcount_set(&wl->refcount, 1);
		goto out_unlock;
	}

	cancel_delayed_work(&wl->work);

	if (refcount_inc_not_zero(&wl->refcount))
		goto out_unlock;

	refcount_set(&wl->refcount, 1);

	__intel_dmc_wl_take(display);

out_unlock:
	spin_unlock_irqrestore(&wl->lock, flags);
}

void intel_dmc_wl_put(struct intel_display *display, i915_reg_t reg)
{
	struct intel_dmc_wl *wl = &display->wl;
	unsigned long flags;

	if (!__intel_dmc_wl_supported(display))
		return;

	spin_lock_irqsave(&wl->lock, flags);

	if (i915_mmio_reg_valid(reg) && !intel_dmc_wl_check_range(reg, wl->dc_state))
		goto out_unlock;

	if (WARN_RATELIMIT(!refcount_read(&wl->refcount),
			   "Tried to put wakelock with refcount zero\n"))
		goto out_unlock;

	if (refcount_dec_and_test(&wl->refcount)) {
		if (!wl->enabled)
			goto out_unlock;

		__intel_dmc_wl_release(display);

		goto out_unlock;
	}

out_unlock:
	spin_unlock_irqrestore(&wl->lock, flags);
}

void intel_dmc_wl_get_noreg(struct intel_display *display)
{
	intel_dmc_wl_get(display, INVALID_MMIO_REG);
}

void intel_dmc_wl_put_noreg(struct intel_display *display)
{
	intel_dmc_wl_put(display, INVALID_MMIO_REG);
}
