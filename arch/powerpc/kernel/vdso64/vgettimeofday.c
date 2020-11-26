// SPDX-License-Identifier: GPL-2.0
/*
 * Powerpc userspace implementations of gettimeofday() and similar.
 */
#include <linux/time.h>
#include <linux/types.h>

int __c_kernel_clock_gettime(clockid_t clock, struct __kernel_timespec *ts,
			     const struct vdso_data *vd)
{
	return __cvdso_clock_gettime_data(vd, clock, ts);
}

int __c_kernel_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz,
			    const struct vdso_data *vd)
{
	return __cvdso_gettimeofday_data(vd, tv, tz);
}

int __c_kernel_clock_getres(clockid_t clock_id, struct __kernel_timespec *res,
			    const struct vdso_data *vd)
{
	return __cvdso_clock_getres_data(vd, clock_id, res);
}

__kernel_old_time_t __c_kernel_time(__kernel_old_time_t *time, const struct vdso_data *vd)
{
	return __cvdso_time_data(vd, time);
}
