// SPDX-License-Identifier: MIT
/*
 * Timeout conversion helpers for wait ioctls.
 *
 * Copyright 2017 Red Hat
 * Copyright 2016 Advanced Micro Devices, Inc.
 */

#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/sched.h>

#include <drm/drm_timeout.h>

/**
 * drm_timeout_abs_to_jiffies - calculate jiffies timeout from absolute value
 * @timeout_nsec: timeout nsec component in ns, 0 for poll
 *
 * Calculate the timeout in jiffies from an absolute time in ns. The result is
 * clamped to MAX_SCHEDULE_TIMEOUT - 1 to avoid infinite timeouts.
 *
 * Returns:
 * The number of jiffies until the given timeout, or the upper limit.
 */
signed long drm_timeout_abs_to_jiffies(s64 timeout_nsec)
{
	ktime_t abs_timeout, now;
	u64 timeout_ns, timeout_jiffies64;

	/* make 0 timeout means poll - absolute 0 doesn't seem valid */
	if (timeout_nsec == 0)
		return 0;

	abs_timeout = ns_to_ktime(timeout_nsec);
	now = ktime_get();

	if (!ktime_after(abs_timeout, now))
		return 0;

	timeout_ns = ktime_to_ns(ktime_sub(abs_timeout, now));

	timeout_jiffies64 = nsecs_to_jiffies64(timeout_ns);
	/*  clamp timeout to avoid infinite timeout */
	if (timeout_jiffies64 >= MAX_SCHEDULE_TIMEOUT - 1)
		return MAX_SCHEDULE_TIMEOUT - 1;

	return timeout_jiffies64 + 1;
}
EXPORT_SYMBOL(drm_timeout_abs_to_jiffies);

/**
 * drm_timeout_rel_to_jiffies - calculate jiffies timeout from relative value
 * @timeout_nsec: relative timeout in ns, 0 for poll
 *
 * Calculate the timeout in jiffies from a relative timeout in ns, for drivers
 * whose UAPI expresses a wait as a duration rather than as a deadline.
 *
 * The result is clamped to MAX_JIFFY_OFFSET. That keeps it positive once it is
 * converted to the signed long taken by dma_fence_wait_timeout() and friends,
 * which matters on 32-bit, and keeps it distinct from MAX_SCHEDULE_TIMEOUT so
 * that a finite wait is never understood as an infinite one.
 *
 * It's strongly discouraged to use relative timeouts in uAPIs, as they do not
 * survive a restarted ioctl. A signal-interrupted ioctl is re-entered with the
 * same arguments, so the duration starts counting from zero again. New uAPIs
 * should take an absolute deadline and use drm_timeout_abs_to_jiffies().
 *
 * Returns:
 * 0 if @timeout_nsec is 0. Otherwise the equivalent number of jiffies and
 * clamped to MAX_JIFFY_OFFSET.
 */
unsigned long drm_timeout_rel_to_jiffies(u64 timeout_nsec)
{
	u64 secs;
	u32 rem;

	/* Make 0 timeout means poll, as for the absolute variant. */
	if (timeout_nsec == 0)
		return 0;

	/*
	 * As nsecs_to_jiffies64() does not guard against overflow, split
	 * the timeout into whole seconds and nanoseconds. This way
	 * nsecs_to_jiffies64() is always handed a value below a second.
	 */
	secs = div_u64_rem(timeout_nsec, NSEC_PER_SEC, &rem);
	if (secs >= MAX_JIFFY_OFFSET / HZ)
		return MAX_JIFFY_OFFSET;

	return min_t(u64, MAX_JIFFY_OFFSET,
		     secs_to_jiffies(secs) + nsecs_to_jiffies64(rem) + 1);
}
EXPORT_SYMBOL(drm_timeout_rel_to_jiffies);
