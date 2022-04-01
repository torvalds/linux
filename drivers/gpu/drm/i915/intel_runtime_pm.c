/*
 * Copyright © 2012-2014 Intel Corporation
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
 *
 * Authors:
 *    Eugeni Dodonov <eugeni.dodonov@intel.com>
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include <linux/pm_runtime.h>

#include <drm/drm_print.h>

#include "i915_drv.h"
#include "i915_trace.h"

/**
 * DOC: runtime pm
 *
 * The i915 driver supports dynamic enabling and disabling of entire hardware
 * blocks at runtime. This is especially important on the display side where
 * software is supposed to control many power gates manually on recent hardware,
 * since on the GT side a lot of the power management is done by the hardware.
 * But even there some manual control at the device level is required.
 *
 * Since i915 supports a diverse set of platforms with a unified codebase and
 * hardware engineers just love to shuffle functionality around between power
 * domains there's a sizeable amount of indirection required. This file provides
 * generic functions to the driver for grabbing and releasing references for
 * abstract power domains. It then maps those to the actual power wells
 * present for a given platform.
 */

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)

#include <linux/sort.h>

#define STACKDEPTH 8

static noinline depot_stack_handle_t __save_depot_stack(void)
{
	unsigned long entries[STACKDEPTH];
	unsigned int n;

	n = stack_trace_save(entries, ARRAY_SIZE(entries), 1);
	return stack_depot_save(entries, n, GFP_NOWAIT | __GFP_NOWARN);
}

static void init_intel_runtime_pm_wakeref(struct intel_runtime_pm *rpm)
{
	spin_lock_init(&rpm->debug.lock);

	if (rpm->available)
		stack_depot_init();
}

static noinline depot_stack_handle_t
track_intel_runtime_pm_wakeref(struct intel_runtime_pm *rpm)
{
	depot_stack_handle_t stack, *stacks;
	unsigned long flags;

	if (!rpm->available)
		return -1;

	stack = __save_depot_stack();
	if (!stack)
		return -1;

	spin_lock_irqsave(&rpm->debug.lock, flags);

	if (!rpm->debug.count)
		rpm->debug.last_acquire = stack;

	stacks = krealloc(rpm->debug.owners,
			  (rpm->debug.count + 1) * sizeof(*stacks),
			  GFP_NOWAIT | __GFP_NOWARN);
	if (stacks) {
		stacks[rpm->debug.count++] = stack;
		rpm->debug.owners = stacks;
	} else {
		stack = -1;
	}

	spin_unlock_irqrestore(&rpm->debug.lock, flags);

	return stack;
}

static void untrack_intel_runtime_pm_wakeref(struct intel_runtime_pm *rpm,
					     depot_stack_handle_t stack)
{
	struct drm_i915_private *i915 = container_of(rpm,
						     struct drm_i915_private,
						     runtime_pm);
	unsigned long flags, n;
	bool found = false;

	if (unlikely(stack == -1))
		return;

	spin_lock_irqsave(&rpm->debug.lock, flags);
	for (n = rpm->debug.count; n--; ) {
		if (rpm->debug.owners[n] == stack) {
			memmove(rpm->debug.owners + n,
				rpm->debug.owners + n + 1,
				(--rpm->debug.count - n) * sizeof(stack));
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&rpm->debug.lock, flags);

	if (drm_WARN(&i915->drm, !found,
		     "Unmatched wakeref (tracking %lu), count %u\n",
		     rpm->debug.count, atomic_read(&rpm->wakeref_count))) {
		char *buf;

		buf = kmalloc(PAGE_SIZE, GFP_NOWAIT | __GFP_NOWARN);
		if (!buf)
			return;

		stack_depot_snprint(stack, buf, PAGE_SIZE, 2);
		DRM_DEBUG_DRIVER("wakeref %x from\n%s", stack, buf);

		stack = READ_ONCE(rpm->debug.last_release);
		if (stack) {
			stack_depot_snprint(stack, buf, PAGE_SIZE, 2);
			DRM_DEBUG_DRIVER("wakeref last released at\n%s", buf);
		}

		kfree(buf);
	}
}

static int cmphandle(const void *_a, const void *_b)
{
	const depot_stack_handle_t * const a = _a, * const b = _b;

	if (*a < *b)
		return -1;
	else if (*a > *b)
		return 1;
	else
		return 0;
}

static void
__print_intel_runtime_pm_wakeref(struct drm_printer *p,
				 const struct intel_runtime_pm_debug *dbg)
{
	unsigned long i;
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_NOWAIT | __GFP_NOWARN);
	if (!buf)
		return;

	if (dbg->last_acquire) {
		stack_depot_snprint(dbg->last_acquire, buf, PAGE_SIZE, 2);
		drm_printf(p, "Wakeref last acquired:\n%s", buf);
	}

	if (dbg->last_release) {
		stack_depot_snprint(dbg->last_release, buf, PAGE_SIZE, 2);
		drm_printf(p, "Wakeref last released:\n%s", buf);
	}

	drm_printf(p, "Wakeref count: %lu\n", dbg->count);

	sort(dbg->owners, dbg->count, sizeof(*dbg->owners), cmphandle, NULL);

	for (i = 0; i < dbg->count; i++) {
		depot_stack_handle_t stack = dbg->owners[i];
		unsigned long rep;

		rep = 1;
		while (i + 1 < dbg->count && dbg->owners[i + 1] == stack)
			rep++, i++;
		stack_depot_snprint(stack, buf, PAGE_SIZE, 2);
		drm_printf(p, "Wakeref x%lu taken at:\n%s", rep, buf);
	}

	kfree(buf);
}

static noinline void
__untrack_all_wakerefs(struct intel_runtime_pm_debug *debug,
		       struct intel_runtime_pm_debug *saved)
{
	*saved = *debug;

	debug->owners = NULL;
	debug->count = 0;
	debug->last_release = __save_depot_stack();
}

static void
dump_and_free_wakeref_tracking(struct intel_runtime_pm_debug *debug)
{
	if (debug->count) {
		struct drm_printer p = drm_debug_printer("i915");

		__print_intel_runtime_pm_wakeref(&p, debug);
	}

	kfree(debug->owners);
}

static noinline void
__intel_wakeref_dec_and_check_tracking(struct intel_runtime_pm *rpm)
{
	struct intel_runtime_pm_debug dbg = {};
	unsigned long flags;

	if (!atomic_dec_and_lock_irqsave(&rpm->wakeref_count,
					 &rpm->debug.lock,
					 flags))
		return;

	__untrack_all_wakerefs(&rpm->debug, &dbg);
	spin_unlock_irqrestore(&rpm->debug.lock, flags);

	dump_and_free_wakeref_tracking(&dbg);
}

static noinline void
untrack_all_intel_runtime_pm_wakerefs(struct intel_runtime_pm *rpm)
{
	struct intel_runtime_pm_debug dbg = {};
	unsigned long flags;

	spin_lock_irqsave(&rpm->debug.lock, flags);
	__untrack_all_wakerefs(&rpm->debug, &dbg);
	spin_unlock_irqrestore(&rpm->debug.lock, flags);

	dump_and_free_wakeref_tracking(&dbg);
}

void print_intel_runtime_pm_wakeref(struct intel_runtime_pm *rpm,
				    struct drm_printer *p)
{
	struct intel_runtime_pm_debug dbg = {};

	do {
		unsigned long alloc = dbg.count;
		depot_stack_handle_t *s;

		spin_lock_irq(&rpm->debug.lock);
		dbg.count = rpm->debug.count;
		if (dbg.count <= alloc) {
			memcpy(dbg.owners,
			       rpm->debug.owners,
			       dbg.count * sizeof(*s));
		}
		dbg.last_acquire = rpm->debug.last_acquire;
		dbg.last_release = rpm->debug.last_release;
		spin_unlock_irq(&rpm->debug.lock);
		if (dbg.count <= alloc)
			break;

		s = krealloc(dbg.owners,
			     dbg.count * sizeof(*s),
			     GFP_NOWAIT | __GFP_NOWARN);
		if (!s)
			goto out;

		dbg.owners = s;
	} while (1);

	__print_intel_runtime_pm_wakeref(p, &dbg);

out:
	kfree(dbg.owners);
}

#else

static void init_intel_runtime_pm_wakeref(struct intel_runtime_pm *rpm)
{
}

static depot_stack_handle_t
track_intel_runtime_pm_wakeref(struct intel_runtime_pm *rpm)
{
	return -1;
}

static void untrack_intel_runtime_pm_wakeref(struct intel_runtime_pm *rpm,
					     intel_wakeref_t wref)
{
}

static void
__intel_wakeref_dec_and_check_tracking(struct intel_runtime_pm *rpm)
{
	atomic_dec(&rpm->wakeref_count);
}

static void
untrack_all_intel_runtime_pm_wakerefs(struct intel_runtime_pm *rpm)
{
}

#endif

static void
intel_runtime_pm_acquire(struct intel_runtime_pm *rpm, bool wakelock)
{
	if (wakelock) {
		atomic_add(1 + INTEL_RPM_WAKELOCK_BIAS, &rpm->wakeref_count);
		assert_rpm_wakelock_held(rpm);
	} else {
		atomic_inc(&rpm->wakeref_count);
		assert_rpm_raw_wakeref_held(rpm);
	}
}

static void
intel_runtime_pm_release(struct intel_runtime_pm *rpm, int wakelock)
{
	if (wakelock) {
		assert_rpm_wakelock_held(rpm);
		atomic_sub(INTEL_RPM_WAKELOCK_BIAS, &rpm->wakeref_count);
	} else {
		assert_rpm_raw_wakeref_held(rpm);
	}

	__intel_wakeref_dec_and_check_tracking(rpm);
}

static intel_wakeref_t __intel_runtime_pm_get(struct intel_runtime_pm *rpm,
					      bool wakelock)
{
	struct drm_i915_private *i915 = container_of(rpm,
						     struct drm_i915_private,
						     runtime_pm);
	int ret;

	ret = pm_runtime_get_sync(rpm->kdev);
	drm_WARN_ONCE(&i915->drm, ret < 0,
		      "pm_runtime_get_sync() failed: %d\n", ret);

	intel_runtime_pm_acquire(rpm, wakelock);

	return track_intel_runtime_pm_wakeref(rpm);
}

/**
 * intel_runtime_pm_get_raw - grab a raw runtime pm reference
 * @rpm: the intel_runtime_pm structure
 *
 * This is the unlocked version of intel_display_power_is_enabled() and should
 * only be used from error capture and recovery code where deadlocks are
 * possible.
 * This function grabs a device-level runtime pm reference (mostly used for
 * asynchronous PM management from display code) and ensures that it is powered
 * up. Raw references are not considered during wakelock assert checks.
 *
 * Any runtime pm reference obtained by this function must have a symmetric
 * call to intel_runtime_pm_put_raw() to release the reference again.
 *
 * Returns: the wakeref cookie to pass to intel_runtime_pm_put_raw(), evaluates
 * as True if the wakeref was acquired, or False otherwise.
 */
intel_wakeref_t intel_runtime_pm_get_raw(struct intel_runtime_pm *rpm)
{
	return __intel_runtime_pm_get(rpm, false);
}

/**
 * intel_runtime_pm_get - grab a runtime pm reference
 * @rpm: the intel_runtime_pm structure
 *
 * This function grabs a device-level runtime pm reference (mostly used for GEM
 * code to ensure the GTT or GT is on) and ensures that it is powered up.
 *
 * Any runtime pm reference obtained by this function must have a symmetric
 * call to intel_runtime_pm_put() to release the reference again.
 *
 * Returns: the wakeref cookie to pass to intel_runtime_pm_put()
 */
intel_wakeref_t intel_runtime_pm_get(struct intel_runtime_pm *rpm)
{
	return __intel_runtime_pm_get(rpm, true);
}

/**
 * __intel_runtime_pm_get_if_active - grab a runtime pm reference if device is active
 * @rpm: the intel_runtime_pm structure
 * @ignore_usecount: get a ref even if dev->power.usage_count is 0
 *
 * This function grabs a device-level runtime pm reference if the device is
 * already active and ensures that it is powered up. It is illegal to try
 * and access the HW should intel_runtime_pm_get_if_active() report failure.
 *
 * If @ignore_usecount is true, a reference will be acquired even if there is no
 * user requiring the device to be powered up (dev->power.usage_count == 0).
 * If the function returns false in this case then it's guaranteed that the
 * device's runtime suspend hook has been called already or that it will be
 * called (and hence it's also guaranteed that the device's runtime resume
 * hook will be called eventually).
 *
 * Any runtime pm reference obtained by this function must have a symmetric
 * call to intel_runtime_pm_put() to release the reference again.
 *
 * Returns: the wakeref cookie to pass to intel_runtime_pm_put(), evaluates
 * as True if the wakeref was acquired, or False otherwise.
 */
static intel_wakeref_t __intel_runtime_pm_get_if_active(struct intel_runtime_pm *rpm,
							bool ignore_usecount)
{
	if (IS_ENABLED(CONFIG_PM)) {
		/*
		 * In cases runtime PM is disabled by the RPM core and we get
		 * an -EINVAL return value we are not supposed to call this
		 * function, since the power state is undefined. This applies
		 * atm to the late/early system suspend/resume handlers.
		 */
		if (pm_runtime_get_if_active(rpm->kdev, ignore_usecount) <= 0)
			return 0;
	}

	intel_runtime_pm_acquire(rpm, true);

	return track_intel_runtime_pm_wakeref(rpm);
}

intel_wakeref_t intel_runtime_pm_get_if_in_use(struct intel_runtime_pm *rpm)
{
	return __intel_runtime_pm_get_if_active(rpm, false);
}

intel_wakeref_t intel_runtime_pm_get_if_active(struct intel_runtime_pm *rpm)
{
	return __intel_runtime_pm_get_if_active(rpm, true);
}

/**
 * intel_runtime_pm_get_noresume - grab a runtime pm reference
 * @rpm: the intel_runtime_pm structure
 *
 * This function grabs a device-level runtime pm reference (mostly used for GEM
 * code to ensure the GTT or GT is on).
 *
 * It will _not_ power up the device but instead only check that it's powered
 * on.  Therefore it is only valid to call this functions from contexts where
 * the device is known to be powered up and where trying to power it up would
 * result in hilarity and deadlocks. That pretty much means only the system
 * suspend/resume code where this is used to grab runtime pm references for
 * delayed setup down in work items.
 *
 * Any runtime pm reference obtained by this function must have a symmetric
 * call to intel_runtime_pm_put() to release the reference again.
 *
 * Returns: the wakeref cookie to pass to intel_runtime_pm_put()
 */
intel_wakeref_t intel_runtime_pm_get_noresume(struct intel_runtime_pm *rpm)
{
	assert_rpm_wakelock_held(rpm);
	pm_runtime_get_noresume(rpm->kdev);

	intel_runtime_pm_acquire(rpm, true);

	return track_intel_runtime_pm_wakeref(rpm);
}

static void __intel_runtime_pm_put(struct intel_runtime_pm *rpm,
				   intel_wakeref_t wref,
				   bool wakelock)
{
	struct device *kdev = rpm->kdev;

	untrack_intel_runtime_pm_wakeref(rpm, wref);

	intel_runtime_pm_release(rpm, wakelock);

	pm_runtime_mark_last_busy(kdev);
	pm_runtime_put_autosuspend(kdev);
}

/**
 * intel_runtime_pm_put_raw - release a raw runtime pm reference
 * @rpm: the intel_runtime_pm structure
 * @wref: wakeref acquired for the reference that is being released
 *
 * This function drops the device-level runtime pm reference obtained by
 * intel_runtime_pm_get_raw() and might power down the corresponding
 * hardware block right away if this is the last reference.
 */
void
intel_runtime_pm_put_raw(struct intel_runtime_pm *rpm, intel_wakeref_t wref)
{
	__intel_runtime_pm_put(rpm, wref, false);
}

/**
 * intel_runtime_pm_put_unchecked - release an unchecked runtime pm reference
 * @rpm: the intel_runtime_pm structure
 *
 * This function drops the device-level runtime pm reference obtained by
 * intel_runtime_pm_get() and might power down the corresponding
 * hardware block right away if this is the last reference.
 *
 * This function exists only for historical reasons and should be avoided in
 * new code, as the correctness of its use cannot be checked. Always use
 * intel_runtime_pm_put() instead.
 */
void intel_runtime_pm_put_unchecked(struct intel_runtime_pm *rpm)
{
	__intel_runtime_pm_put(rpm, -1, true);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
/**
 * intel_runtime_pm_put - release a runtime pm reference
 * @rpm: the intel_runtime_pm structure
 * @wref: wakeref acquired for the reference that is being released
 *
 * This function drops the device-level runtime pm reference obtained by
 * intel_runtime_pm_get() and might power down the corresponding
 * hardware block right away if this is the last reference.
 */
void intel_runtime_pm_put(struct intel_runtime_pm *rpm, intel_wakeref_t wref)
{
	__intel_runtime_pm_put(rpm, wref, true);
}
#endif

/**
 * intel_runtime_pm_enable - enable runtime pm
 * @rpm: the intel_runtime_pm structure
 *
 * This function enables runtime pm at the end of the driver load sequence.
 *
 * Note that this function does currently not enable runtime pm for the
 * subordinate display power domains. That is done by
 * intel_power_domains_enable().
 */
void intel_runtime_pm_enable(struct intel_runtime_pm *rpm)
{
	struct drm_i915_private *i915 = container_of(rpm,
						     struct drm_i915_private,
						     runtime_pm);
	struct device *kdev = rpm->kdev;

	/*
	 * Disable the system suspend direct complete optimization, which can
	 * leave the device suspended skipping the driver's suspend handlers
	 * if the device was already runtime suspended. This is needed due to
	 * the difference in our runtime and system suspend sequence and
	 * becaue the HDA driver may require us to enable the audio power
	 * domain during system suspend.
	 */
	dev_pm_set_driver_flags(kdev, DPM_FLAG_NO_DIRECT_COMPLETE);

	pm_runtime_set_autosuspend_delay(kdev, 10000); /* 10s */
	pm_runtime_mark_last_busy(kdev);

	/*
	 * Take a permanent reference to disable the RPM functionality and drop
	 * it only when unloading the driver. Use the low level get/put helpers,
	 * so the driver's own RPM reference tracking asserts also work on
	 * platforms without RPM support.
	 */
	if (!rpm->available) {
		int ret;

		pm_runtime_dont_use_autosuspend(kdev);
		ret = pm_runtime_get_sync(kdev);
		drm_WARN(&i915->drm, ret < 0,
			 "pm_runtime_get_sync() failed: %d\n", ret);
	} else {
		pm_runtime_use_autosuspend(kdev);
	}

	/* Enable by default */
	pm_runtime_allow(kdev);

	/*
	 * The core calls the driver load handler with an RPM reference held.
	 * We drop that here and will reacquire it during unloading in
	 * intel_power_domains_fini().
	 */
	pm_runtime_put_autosuspend(kdev);
}

void intel_runtime_pm_disable(struct intel_runtime_pm *rpm)
{
	struct drm_i915_private *i915 = container_of(rpm,
						     struct drm_i915_private,
						     runtime_pm);
	struct device *kdev = rpm->kdev;

	/* Transfer rpm ownership back to core */
	drm_WARN(&i915->drm, pm_runtime_get_sync(kdev) < 0,
		 "Failed to pass rpm ownership back to core\n");

	pm_runtime_dont_use_autosuspend(kdev);

	if (!rpm->available)
		pm_runtime_put(kdev);
}

void intel_runtime_pm_driver_release(struct intel_runtime_pm *rpm)
{
	struct drm_i915_private *i915 = container_of(rpm,
						     struct drm_i915_private,
						     runtime_pm);
	int count = atomic_read(&rpm->wakeref_count);

	drm_WARN(&i915->drm, count,
		 "i915 raw-wakerefs=%d wakelocks=%d on cleanup\n",
		 intel_rpm_raw_wakeref_count(count),
		 intel_rpm_wakelock_count(count));

	untrack_all_intel_runtime_pm_wakerefs(rpm);
}

void intel_runtime_pm_init_early(struct intel_runtime_pm *rpm)
{
	struct drm_i915_private *i915 =
			container_of(rpm, struct drm_i915_private, runtime_pm);
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	struct device *kdev = &pdev->dev;

	rpm->kdev = kdev;
	rpm->available = HAS_RUNTIME_PM(i915);

	init_intel_runtime_pm_wakeref(rpm);
}
