/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_SMP_H
#define __ASM_ARC_SMP_H

#ifdef CONFIG_SMP

#include <linux/types.h>
#include <linux/init.h>
#include <linux/threads.h>

#define raw_smp_processor_id() (current_thread_info()->cpu)

/* including cpumask.h leads to cyclic deps hence this Forward declaration */
struct cpumask;

/*
 * APIs provided by arch SMP code to generic code
 */
extern void arch_send_call_function_single_ipi(int cpu);
extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);

/*
 * APIs provided by arch SMP code to rest of arch code
 */
extern void __init smp_init_cpus(void);
extern void __init first_lines_of_secondary(void);

/*
 * API expected BY platform smp code (FROM arch smp code)
 *
 * smp_ipi_irq_setup:
 *	Takes @cpu and @irq to which the arch-common ISR is hooked up
 */
extern int smp_ipi_irq_setup(int cpu, int irq);

/*
 * APIs expected FROM platform smp code
 *
 * arc_platform_smp_cpuinfo:
 *	returns a string containing info for /proc/cpuinfo
 *
 * arc_platform_smp_init_cpu:
 *	Called from start_kernel_secondary to do any CPU local setup
 *	such as starting a timer, setting up IPI etc
 *
 * arc_platform_smp_wait_to_boot:
 *	Called from early bootup code for non-Master CPUs to "park" them
 *
 * arc_platform_smp_wakeup_cpu:
 *	Called from __cpu_up (Master CPU) to kick start another one
 *
 * arc_platform_ipi_send:
 *	Takes @cpumask to which IPI(s) would be sent.
 *	The actual msg-id/buffer is manager in arch-common code
 *
 * arc_platform_ipi_clear:
 *	Takes @cpu which got IPI at @irq to do any IPI clearing
 */
extern const char *arc_platform_smp_cpuinfo(void);
extern void arc_platform_smp_init_cpu(void);
extern void arc_platform_smp_wait_to_boot(int cpu);
extern void arc_platform_smp_wakeup_cpu(int cpu, unsigned long pc);
extern void arc_platform_ipi_send(const struct cpumask *callmap);
extern void arc_platform_ipi_clear(int cpu, int irq);

#endif  /* CONFIG_SMP */

/*
 * ARC700 doesn't support atomic Read-Modify-Write ops.
 * Originally Interrupts had to be disabled around code to gaurantee atomicity.
 * The LLOCK/SCOND insns allow writing interrupt-hassle-free based atomic ops
 * based on retry-if-irq-in-atomic (with hardware assist).
 * However despite these, we provide the IRQ disabling variant
 *
 * (1) These insn were introduced only in 4.10 release. So for older released
 *	support needed.
 *
 * (2) In a SMP setup, the LLOCK/SCOND atomiticity across CPUs needs to be
 *	gaurantted by the platform (not something which core handles).
 *	Assuming a platform won't, SMP Linux needs to use spinlocks + local IRQ
 *	disabling for atomicity.
 *
 *	However exported spinlock API is not usable due to cyclic hdr deps
 *	(even after system.h disintegration upstream)
 *	asm/bitops.h -> linux/spinlock.h -> linux/preempt.h
 *		-> linux/thread_info.h -> linux/bitops.h -> asm/bitops.h
 *
 *	So the workaround is to use the lowest level arch spinlock API.
 *	The exported spinlock API is smart enough to be NOP for !CONFIG_SMP,
 *	but same is not true for ARCH backend, hence the need for 2 variants
 */
#ifndef CONFIG_ARC_HAS_LLSC

#include <linux/irqflags.h>
#ifdef CONFIG_SMP

#include <asm/spinlock.h>

extern arch_spinlock_t smp_atomic_ops_lock;
extern arch_spinlock_t smp_bitops_lock;

#define atomic_ops_lock(flags)	do {		\
	local_irq_save(flags);			\
	arch_spin_lock(&smp_atomic_ops_lock);	\
} while (0)

#define atomic_ops_unlock(flags) do {		\
	arch_spin_unlock(&smp_atomic_ops_lock);	\
	local_irq_restore(flags);		\
} while (0)

#define bitops_lock(flags)	do {		\
	local_irq_save(flags);			\
	arch_spin_lock(&smp_bitops_lock);	\
} while (0)

#define bitops_unlock(flags) do {		\
	arch_spin_unlock(&smp_bitops_lock);	\
	local_irq_restore(flags);		\
} while (0)

#else /* !CONFIG_SMP */

#define atomic_ops_lock(flags)		local_irq_save(flags)
#define atomic_ops_unlock(flags)	local_irq_restore(flags)

#define bitops_lock(flags)		local_irq_save(flags)
#define bitops_unlock(flags)		local_irq_restore(flags)

#endif /* !CONFIG_SMP */

#endif	/* !CONFIG_ARC_HAS_LLSC */

#endif
