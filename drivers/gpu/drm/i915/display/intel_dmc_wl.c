// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2024 Intel Corporation
 */

#include <linux/kernel.h>

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

static struct intel_dmc_wl_range lnl_wl_range[] = {
	{ .start = 0x60000, .end = 0x7ffff },
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

	/* Bail out if refcount reached zero while waiting for the spinlock */
	if (!refcount_read(&wl->refcount))
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

static bool intel_dmc_wl_check_range(i915_reg_t reg)
{
	int i;
	bool wl_needed = false;
	u32 offset = i915_mmio_reg_offset(reg);

	for (i = 0; i < ARRAY_SIZE(lnl_wl_range); i++) {
		if (offset >= lnl_wl_range[i].start &&
		    offset <= lnl_wl_range[i].end) {
			wl_needed = true;
			break;
		}
	}

	return wl_needed;
}

static bool __intel_dmc_wl_supported(struct intel_display *display)
{
	if (DISPLAY_VER(display) < 20 ||
	    !intel_dmc_has_payload(display) ||
	    !display->params.enable_dmc_wl)
		return false;

	return true;
}

void intel_dmc_wl_init(struct intel_display *display)
{
	struct intel_dmc_wl *wl = &display->wl;

	/* don't call __intel_dmc_wl_supported(), DMC is not loaded yet */
	if (DISPLAY_VER(display) < 20 || !display->params.enable_dmc_wl)
		return;

	INIT_DELAYED_WORK(&wl->work, intel_dmc_wl_work);
	spin_lock_init(&wl->lock);
	refcount_set(&wl->refcount, 0);
}

void intel_dmc_wl_enable(struct intel_display *display)
{
	struct intel_dmc_wl *wl = &display->wl;
	unsigned long flags;

	if (!__intel_dmc_wl_supported(display))
		return;

	spin_lock_irqsave(&wl->lock, flags);

	if (wl->enabled)
		goto out_unlock;

	/*
	 * Enable wakelock in DMC.  We shouldn't try to take the
	 * wakelock, because we're just enabling it, so call the
	 * non-locking version directly here.
	 */
	__intel_de_rmw_nowl(display, DMC_WAKELOCK_CFG, 0, DMC_WAKELOCK_CFG_ENABLE);

	wl->enabled = true;
	wl->taken = false;

out_unlock:
	spin_unlock_irqrestore(&wl->lock, flags);
}

void intel_dmc_wl_disable(struct intel_display *display)
{
	struct intel_dmc_wl *wl = &display->wl;
	unsigned long flags;

	if (!__intel_dmc_wl_supported(display))
		return;

	flush_delayed_work(&wl->work);

	spin_lock_irqsave(&wl->lock, flags);

	if (!wl->enabled)
		goto out_unlock;

	/* Disable wakelock in DMC */
	__intel_de_rmw_nowl(display, DMC_WAKELOCK_CFG, DMC_WAKELOCK_CFG_ENABLE, 0);

	refcount_set(&wl->refcount, 0);
	wl->enabled = false;
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

	if (!intel_dmc_wl_check_range(reg))
		return;

	spin_lock_irqsave(&wl->lock, flags);

	if (!wl->enabled)
		goto out_unlock;

	cancel_delayed_work(&wl->work);

	if (refcount_inc_not_zero(&wl->refcount))
		goto out_unlock;

	refcount_set(&wl->refcount, 1);

	/*
	 * Only try to take the wakelock if it's not marked as taken
	 * yet.  It may be already taken at this point if we have
	 * already released the last reference, but the work has not
	 * run yet.
	 */
	if (!wl->taken) {
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
			goto out_unlock;
		}

		wl->taken = true;
	}

out_unlock:
	spin_unlock_irqrestore(&wl->lock, flags);
}

void intel_dmc_wl_put(struct intel_display *display, i915_reg_t reg)
{
	struct intel_dmc_wl *wl = &display->wl;
	unsigned long flags;

	if (!__intel_dmc_wl_supported(display))
		return;

	if (!intel_dmc_wl_check_range(reg))
		return;

	spin_lock_irqsave(&wl->lock, flags);

	if (!wl->enabled)
		goto out_unlock;

	if (WARN_RATELIMIT(!refcount_read(&wl->refcount),
			   "Tried to put wakelock with refcount zero\n"))
		goto out_unlock;

	if (refcount_dec_and_test(&wl->refcount)) {
		__intel_dmc_wl_release(display);

		goto out_unlock;
	}

out_unlock:
	spin_unlock_irqrestore(&wl->lock, flags);
}
