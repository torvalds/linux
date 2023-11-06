/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_ATOMIC_H
#define __ASM_SH_ATOMIC_H

#if defined(CONFIG_CPU_J2)

#include <asm-generic/atomic.h>

#else

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 */

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

#define arch_atomic_read(v)		READ_ONCE((v)->counter)
#define arch_atomic_set(v,i)		WRITE_ONCE((v)->counter, (i))

#if defined(CONFIG_GUSA_RB)
#include <asm/atomic-grb.h>
#elif defined(CONFIG_CPU_SH4A)
#include <asm/atomic-llsc.h>
#else
#include <asm/atomic-irq.h>
#endif

#endif /* CONFIG_CPU_J2 */

#endif /* __ASM_SH_ATOMIC_H */
