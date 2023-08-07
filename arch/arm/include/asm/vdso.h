/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_H
#define __ASM_VDSO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

struct mm_struct;

#ifdef CONFIG_VDSO

void arm_install_vdso(struct mm_struct *mm, unsigned long addr);

extern unsigned int vdso_total_pages;

#else /* CONFIG_VDSO */

static inline void arm_install_vdso(struct mm_struct *mm, unsigned long addr)
{
}

#define vdso_total_pages 0

#endif /* CONFIG_VDSO */

int __vdso_clock_gettime(clockid_t clock, struct old_timespec32 *ts);
int __vdso_clock_gettime64(clockid_t clock, struct __kernel_timespec *ts);
int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz);
int __vdso_clock_getres(clockid_t clock_id, struct old_timespec32 *res);

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __ASM_VDSO_H */
