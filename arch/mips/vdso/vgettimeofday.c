// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MIPS64 and compat userspace implementations of gettimeofday()
 * and similar.
 *
 * Copyright (C) 2015 Imagination Technologies
 * Copyright (C) 2018 ARM Limited
 *
 */
#include <linux/time.h>
#include <linux/types.h>

#if _MIPS_SIM != _MIPS_SIM_ABI64
int __vdso_clock_gettime(clockid_t clock,
			 struct old_timespec32 *ts)
{
	return __cvdso_clock_gettime32(clock, ts);
}

#ifdef CONFIG_MIPS_CLOCK_VSYSCALL

/*
 * This is behind the ifdef so that we don't provide the symbol when there's no
 * possibility of there being a usable clocksource, because there's nothing we
 * can do without it. When libc fails the symbol lookup it should fall back on
 * the standard syscall path.
 */
int __vdso_gettimeofday(struct __kernel_old_timeval *tv,
			struct timezone *tz)
{
	return __cvdso_gettimeofday(tv, tz);
}

#endif /* CONFIG_MIPS_CLOCK_VSYSCALL */

int __vdso_clock_getres(clockid_t clock_id,
			struct old_timespec32 *res)
{
	return __cvdso_clock_getres_time32(clock_id, res);
}

int __vdso_clock_gettime64(clockid_t clock,
			   struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

#else

int __vdso_clock_gettime(clockid_t clock,
			 struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

#ifdef CONFIG_MIPS_CLOCK_VSYSCALL

/*
 * This is behind the ifdef so that we don't provide the symbol when there's no
 * possibility of there being a usable clocksource, because there's nothing we
 * can do without it. When libc fails the symbol lookup it should fall back on
 * the standard syscall path.
 */
int __vdso_gettimeofday(struct __kernel_old_timeval *tv,
			struct timezone *tz)
{
	return __cvdso_gettimeofday(tv, tz);
}

#endif /* CONFIG_MIPS_CLOCK_VSYSCALL */

int __vdso_clock_getres(clockid_t clock_id,
			struct __kernel_timespec *res)
{
	return __cvdso_clock_getres(clock_id, res);
}

#endif
