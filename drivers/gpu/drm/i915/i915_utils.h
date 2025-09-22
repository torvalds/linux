/*
 * Copyright © 2016 Intel Corporation
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
 */

#ifndef __I915_UTILS_H
#define __I915_UTILS_H

#include <linux/overflow.h>
#include <linux/sched.h>
#include <linux/string_helpers.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/sched/clock.h>

#ifdef CONFIG_X86
#include <asm/hypervisor.h>
#endif

struct drm_i915_private;

#define MISSING_CASE(x) WARN(1, "Missing case (%s == %ld)\n", \
			     __stringify(x), (long)(x))

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)

int __i915_inject_probe_error(struct drm_i915_private *i915, int err,
			      const char *func, int line);
#define i915_inject_probe_error(_i915, _err) \
	__i915_inject_probe_error((_i915), (_err), __func__, __LINE__)
bool i915_error_injected(void);

#else

#define i915_inject_probe_error(i915, e) ({ BUILD_BUG_ON_INVALID(i915); 0; })
#define i915_error_injected() false

#endif

#define i915_inject_probe_failure(i915) i915_inject_probe_error((i915), -ENODEV)

#define i915_probe_error(i915, fmt, ...) ({ \
	if (i915_error_injected()) \
		drm_dbg(&(i915)->drm, fmt, ##__VA_ARGS__); \
	else \
		drm_err(&(i915)->drm, fmt, ##__VA_ARGS__); \
})

#define fetch_and_zero(ptr) ({						\
	typeof(*ptr) __T = *(ptr);					\
	*(ptr) = (typeof(*ptr))0;					\
	__T;								\
})

/*
 * check_user_mbz: Check that a user value exists and is zero
 *
 * Frequently in our uABI we reserve space for future extensions, and
 * two ensure that userspace is prepared we enforce that space must
 * be zero. (Then any future extension can safely assume a default value
 * of 0.)
 *
 * check_user_mbz() combines checking that the user pointer is accessible
 * and that the contained value is zero.
 *
 * Returns: -EFAULT if not accessible, -EINVAL if !zero, or 0 on success.
 */
#define check_user_mbz(U) ({						\
	typeof(*(U)) mbz__;						\
	get_user(mbz__, (U)) ? -EFAULT : mbz__ ? -EINVAL : 0;		\
})

#define __mask_next_bit(mask) ({					\
	int __idx = ffs(mask) - 1;					\
	mask &= ~BIT(__idx);						\
	__idx;								\
})

static inline bool is_power_of_2_u64(u64 n)
{
	return (n != 0 && ((n & (n - 1)) == 0));
}

static inline unsigned long msecs_to_jiffies_timeout(const unsigned int m)
{
	unsigned long j = msecs_to_jiffies(m);

	return min_t(unsigned long, MAX_JIFFY_OFFSET, j + 1);
}

/*
 * If you need to wait X milliseconds between events A and B, but event B
 * doesn't happen exactly after event A, you record the timestamp (jiffies) of
 * when event A happened, then just before event B you call this function and
 * pass the timestamp as the first argument, and X as the second argument.
 */
static inline void
wait_remaining_ms_from_jiffies(unsigned long timestamp_jiffies, int to_wait_ms)
{
	unsigned long target_jiffies, tmp_jiffies, remaining_jiffies;

	/*
	 * Don't re-read the value of "jiffies" every time since it may change
	 * behind our back and break the math.
	 */
	tmp_jiffies = jiffies;
	target_jiffies = timestamp_jiffies +
			 msecs_to_jiffies_timeout(to_wait_ms);

	if (time_after(target_jiffies, tmp_jiffies)) {
		remaining_jiffies = target_jiffies - tmp_jiffies;
		while (remaining_jiffies)
			remaining_jiffies =
			    schedule_timeout_uninterruptible(remaining_jiffies);
	}
}

#define KHz(x) (1000 * (x))
#define MHz(x) KHz(1000 * (x))

void add_taint_for_CI(struct drm_i915_private *i915, unsigned int taint);
static inline void __add_taint_for_CI(unsigned int taint)
{
	/*
	 * The system is "ok", just about surviving for the user, but
	 * CI results are now unreliable as the HW is very suspect.
	 * CI checks the taint state after every test and will reboot
	 * the machine if the kernel is tainted.
	 */
	add_taint(taint, LOCKDEP_STILL_OK);
}

static inline bool i915_run_as_guest(void)
{
#if IS_ENABLED(CONFIG_X86)
	return !hypervisor_is_type(X86_HYPER_NATIVE);
#else
	/* Not supported yet */
	return false;
#endif
}

bool i915_vtd_active(struct drm_i915_private *i915);

bool i915_direct_stolen_access(struct drm_i915_private *i915);

#endif /* !__I915_UTILS_H */
