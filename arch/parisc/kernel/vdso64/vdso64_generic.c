// SPDX-License-Identifier: GPL-2.0

#include "asm/unistd.h"
#include <linux/types.h>

struct timezone;
struct __kernel_timespec;
struct __kernel_old_timeval;

/* forward declarations */
int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz);
int __vdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts);


int __vdso_gettimeofday(struct __kernel_old_timeval *tv,
			struct timezone *tz)
{
	return syscall2(__NR_gettimeofday, (long)tv, (long)tz);
}

int __vdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts)
{
	return syscall2(__NR_clock_gettime, (long)clock, (long)ts);
}
