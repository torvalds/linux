/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 ARM Limited
 */
#ifndef __COMPAT_BARRIER_H
#define __COMPAT_BARRIER_H

#ifndef __ASSEMBLY__
/*
 * Warning: This code is meant to be used from the compat vDSO only.
 */
#ifdef __arch64__
#error This header is meant to be used with from the compat vDSO only
#endif

#ifdef dmb
#undef dmb
#endif

#define dmb(option) __asm__ __volatile__ ("dmb " #option : : : "memory")

#define aarch32_smp_mb()	dmb(ish)
#define aarch32_smp_rmb()	dmb(ishld)
#define aarch32_smp_wmb()	dmb(ishst)

#undef smp_mb
#undef smp_rmb
#undef smp_wmb

#define smp_mb()	aarch32_smp_mb()
#define smp_rmb()	aarch32_smp_rmb()
#define smp_wmb()	aarch32_smp_wmb()

#endif /* !__ASSEMBLY__ */

#endif /* __COMPAT_BARRIER_H */
