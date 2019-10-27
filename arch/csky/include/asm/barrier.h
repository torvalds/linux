/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_BARRIER_H
#define __ASM_CSKY_BARRIER_H

#ifndef __ASSEMBLY__

#define nop()	asm volatile ("nop\n":::"memory")

/*
 * sync:        completion barrier, all sync.xx instructions
 *              guarantee the last response recieved by bus transaction
 *              made by ld/st instructions before sync.s
 * sync.s:      inherit from sync, but also shareable to other cores
 * sync.i:      inherit from sync, but also flush cpu pipeline
 * sync.is:     the same with sync.i + sync.s
 *
 * bar.brwarw:  ordering barrier for all load/store instructions before it
 * bar.brwarws: ordering barrier for all load/store instructions before it
 *						and shareable to other cores
 * bar.brar:    ordering barrier for all load       instructions before it
 * bar.brars:   ordering barrier for all load       instructions before it
 *						and shareable to other cores
 * bar.bwaw:    ordering barrier for all store      instructions before it
 * bar.bwaws:   ordering barrier for all store      instructions before it
 *						and shareable to other cores
 */

#ifdef CONFIG_CPU_HAS_CACHEV2
#define mb()		asm volatile ("sync.s\n":::"memory")

#ifdef CONFIG_SMP
#define __smp_mb()	asm volatile ("bar.brwarws\n":::"memory")
#define __smp_rmb()	asm volatile ("bar.brars\n":::"memory")
#define __smp_wmb()	asm volatile ("bar.bwaws\n":::"memory")
#endif /* CONFIG_SMP */

#define sync_is()	asm volatile ("sync.is\n":::"memory")

#else /* !CONFIG_CPU_HAS_CACHEV2 */
#define mb()		asm volatile ("sync\n":::"memory")
#endif

#include <asm-generic/barrier.h>

#endif /* __ASSEMBLY__ */
#endif /* __ASM_CSKY_BARRIER_H */
