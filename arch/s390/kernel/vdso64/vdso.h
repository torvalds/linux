/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_S390_KERNEL_VDSO64_VDSO_H
#define __ARCH_S390_KERNEL_VDSO64_VDSO_H

#include <vdso/datapage.h>

struct getcpu_cache;

int __s390_vdso_getcpu(unsigned *cpu, unsigned *node, struct getcpu_cache *unused);
int __s390_vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz);
int __s390_vdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts);
int __s390_vdso_clock_getres(clockid_t clock, struct __kernel_timespec *ts);

#endif /* __ARCH_S390_KERNEL_VDSO64_VDSO_H */
