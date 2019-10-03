/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 ARM Limited
 */
#ifndef __COMPAT_BARRIER_H
#define __COMPAT_BARRIER_H

#ifndef __ASSEMBLY__
/*
 * Warning: This code is meant to be used with
 * ENABLE_COMPAT_VDSO only.
 */
#ifndef ENABLE_COMPAT_VDSO
#error This header is meant to be used with ENABLE_COMPAT_VDSO only
#endif

#ifdef dmb
#undef dmb
#endif

#if __LINUX_ARM_ARCH__ >= 7
#define dmb(option) __asm__ __volatile__ ("dmb " #option : : : "memory")
#elif __LINUX_ARM_ARCH__ == 6
#define dmb(x) __asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 5" \
				    : : "r" (0) : "memory")
#else
#define dmb(x) __asm__ __volatile__ ("" : : : "memory")
#endif

#if __LINUX_ARM_ARCH__ >= 8 && defined(CONFIG_AS_DMB_ISHLD)
#define aarch32_smp_mb()	dmb(ish)
#define aarch32_smp_rmb()	dmb(ishld)
#define aarch32_smp_wmb()	dmb(ishst)
#else
#define aarch32_smp_mb()	dmb(ish)
#define aarch32_smp_rmb()	aarch32_smp_mb()
#define aarch32_smp_wmb()	dmb(ishst)
#endif


#undef smp_mb
#undef smp_rmb
#undef smp_wmb

#define smp_mb()	aarch32_smp_mb()
#define smp_rmb()	aarch32_smp_rmb()
#define smp_wmb()	aarch32_smp_wmb()

#endif /* !__ASSEMBLY__ */

#endif /* __COMPAT_BARRIER_H */
