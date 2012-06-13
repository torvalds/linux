/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_IRQFLAGS_H
#define _ASM_TILE_IRQFLAGS_H

#include <arch/interrupts.h>
#include <arch/chip.h>

#if !defined(__tilegx__) && defined(__ASSEMBLY__)

/*
 * The set of interrupts we want to allow when interrupts are nominally
 * disabled.  The remainder are effectively "NMI" interrupts from
 * the point of view of the generic Linux code.  Note that synchronous
 * interrupts (aka "non-queued") are not blocked by the mask in any case.
 */
#if CHIP_HAS_AUX_PERF_COUNTERS()
#define LINUX_MASKABLE_INTERRUPTS_HI \
	(~(INT_MASK_HI(INT_PERF_COUNT) | INT_MASK_HI(INT_AUX_PERF_COUNT)))
#else
#define LINUX_MASKABLE_INTERRUPTS_HI \
	(~(INT_MASK_HI(INT_PERF_COUNT)))
#endif

#else

#if CHIP_HAS_AUX_PERF_COUNTERS()
#define LINUX_MASKABLE_INTERRUPTS \
	(~(INT_MASK(INT_PERF_COUNT) | INT_MASK(INT_AUX_PERF_COUNT)))
#else
#define LINUX_MASKABLE_INTERRUPTS \
	(~(INT_MASK(INT_PERF_COUNT)))
#endif

#endif

#ifndef __ASSEMBLY__

/* NOTE: we can't include <linux/percpu.h> due to #include dependencies. */
#include <asm/percpu.h>
#include <arch/spr_def.h>

/* Set and clear kernel interrupt masks. */
#if CHIP_HAS_SPLIT_INTR_MASK()
#if INT_PERF_COUNT < 32 || INT_AUX_PERF_COUNT < 32 || INT_MEM_ERROR >= 32
# error Fix assumptions about which word various interrupts are in
#endif
#define interrupt_mask_set(n) do { \
	int __n = (n); \
	int __mask = 1 << (__n & 0x1f); \
	if (__n < 32) \
		__insn_mtspr(SPR_INTERRUPT_MASK_SET_K_0, __mask); \
	else \
		__insn_mtspr(SPR_INTERRUPT_MASK_SET_K_1, __mask); \
} while (0)
#define interrupt_mask_reset(n) do { \
	int __n = (n); \
	int __mask = 1 << (__n & 0x1f); \
	if (__n < 32) \
		__insn_mtspr(SPR_INTERRUPT_MASK_RESET_K_0, __mask); \
	else \
		__insn_mtspr(SPR_INTERRUPT_MASK_RESET_K_1, __mask); \
} while (0)
#define interrupt_mask_check(n) ({ \
	int __n = (n); \
	(((__n < 32) ? \
	 __insn_mfspr(SPR_INTERRUPT_MASK_K_0) : \
	 __insn_mfspr(SPR_INTERRUPT_MASK_K_1)) \
	  >> (__n & 0x1f)) & 1; \
})
#define interrupt_mask_set_mask(mask) do { \
	unsigned long long __m = (mask); \
	__insn_mtspr(SPR_INTERRUPT_MASK_SET_K_0, (unsigned long)(__m)); \
	__insn_mtspr(SPR_INTERRUPT_MASK_SET_K_1, (unsigned long)(__m>>32)); \
} while (0)
#define interrupt_mask_reset_mask(mask) do { \
	unsigned long long __m = (mask); \
	__insn_mtspr(SPR_INTERRUPT_MASK_RESET_K_0, (unsigned long)(__m)); \
	__insn_mtspr(SPR_INTERRUPT_MASK_RESET_K_1, (unsigned long)(__m>>32)); \
} while (0)
#define interrupt_mask_save_mask() \
	(__insn_mfspr(SPR_INTERRUPT_MASK_SET_K_0) | \
	 (((unsigned long long)__insn_mfspr(SPR_INTERRUPT_MASK_SET_K_1))<<32))
#define interrupt_mask_restore_mask(mask) do { \
	unsigned long long __m = (mask); \
	__insn_mtspr(SPR_INTERRUPT_MASK_K_0, (unsigned long)(__m)); \
	__insn_mtspr(SPR_INTERRUPT_MASK_K_1, (unsigned long)(__m>>32)); \
} while (0)
#else
#define interrupt_mask_set(n) \
	__insn_mtspr(SPR_INTERRUPT_MASK_SET_K, (1UL << (n)))
#define interrupt_mask_reset(n) \
	__insn_mtspr(SPR_INTERRUPT_MASK_RESET_K, (1UL << (n)))
#define interrupt_mask_check(n) \
	((__insn_mfspr(SPR_INTERRUPT_MASK_K) >> (n)) & 1)
#define interrupt_mask_set_mask(mask) \
	__insn_mtspr(SPR_INTERRUPT_MASK_SET_K, (mask))
#define interrupt_mask_reset_mask(mask) \
	__insn_mtspr(SPR_INTERRUPT_MASK_RESET_K, (mask))
#define interrupt_mask_save_mask() \
	__insn_mfspr(SPR_INTERRUPT_MASK_K)
#define interrupt_mask_restore_mask(mask) \
	__insn_mtspr(SPR_INTERRUPT_MASK_K, (mask))
#endif

/*
 * The set of interrupts we want active if irqs are enabled.
 * Note that in particular, the tile timer interrupt comes and goes
 * from this set, since we have no other way to turn off the timer.
 * Likewise, INTCTRL_K is removed and re-added during device
 * interrupts, as is the the hardwall UDN_FIREWALL interrupt.
 * We use a low bit (MEM_ERROR) as our sentinel value and make sure it
 * is always claimed as an "active interrupt" so we can query that bit
 * to know our current state.
 */
DECLARE_PER_CPU(unsigned long long, interrupts_enabled_mask);
#define INITIAL_INTERRUPTS_ENABLED INT_MASK(INT_MEM_ERROR)

/* Disable interrupts. */
#define arch_local_irq_disable() \
	interrupt_mask_set_mask(LINUX_MASKABLE_INTERRUPTS)

/* Disable all interrupts, including NMIs. */
#define arch_local_irq_disable_all() \
	interrupt_mask_set_mask(-1ULL)

/* Re-enable all maskable interrupts. */
#define arch_local_irq_enable() \
	interrupt_mask_reset_mask(__get_cpu_var(interrupts_enabled_mask))

/* Disable or enable interrupts based on flag argument. */
#define arch_local_irq_restore(disabled) do { \
	if (disabled) \
		arch_local_irq_disable(); \
	else \
		arch_local_irq_enable(); \
} while (0)

/* Return true if "flags" argument means interrupts are disabled. */
#define arch_irqs_disabled_flags(flags) ((flags) != 0)

/* Return true if interrupts are currently disabled. */
#define arch_irqs_disabled() interrupt_mask_check(INT_MEM_ERROR)

/* Save whether interrupts are currently disabled. */
#define arch_local_save_flags() arch_irqs_disabled()

/* Save whether interrupts are currently disabled, then disable them. */
#define arch_local_irq_save() ({ \
	unsigned long __flags = arch_local_save_flags(); \
	arch_local_irq_disable(); \
	__flags; })

/* Prevent the given interrupt from being enabled next time we enable irqs. */
#define arch_local_irq_mask(interrupt) \
	(__get_cpu_var(interrupts_enabled_mask) &= ~INT_MASK(interrupt))

/* Prevent the given interrupt from being enabled immediately. */
#define arch_local_irq_mask_now(interrupt) do { \
	arch_local_irq_mask(interrupt); \
	interrupt_mask_set(interrupt); \
} while (0)

/* Allow the given interrupt to be enabled next time we enable irqs. */
#define arch_local_irq_unmask(interrupt) \
	(__get_cpu_var(interrupts_enabled_mask) |= INT_MASK(interrupt))

/* Allow the given interrupt to be enabled immediately, if !irqs_disabled. */
#define arch_local_irq_unmask_now(interrupt) do { \
	arch_local_irq_unmask(interrupt); \
	if (!irqs_disabled()) \
		interrupt_mask_reset(interrupt); \
} while (0)

#else /* __ASSEMBLY__ */

/* We provide a somewhat more restricted set for assembly. */

#ifdef __tilegx__

#if INT_MEM_ERROR != 0
# error Fix IRQS_DISABLED() macro
#endif

/* Return 0 or 1 to indicate whether interrupts are currently disabled. */
#define IRQS_DISABLED(tmp)					\
	mfspr   tmp, SPR_INTERRUPT_MASK_K;			\
	andi    tmp, tmp, 1

/* Load up a pointer to &interrupts_enabled_mask. */
#define GET_INTERRUPTS_ENABLED_MASK_PTR(reg)			\
	moveli reg, hw2_last(interrupts_enabled_mask);		\
	shl16insli reg, reg, hw1(interrupts_enabled_mask);	\
	shl16insli reg, reg, hw0(interrupts_enabled_mask);	\
	add     reg, reg, tp

/* Disable interrupts. */
#define IRQ_DISABLE(tmp0, tmp1)					\
	moveli  tmp0, hw2_last(LINUX_MASKABLE_INTERRUPTS);	\
	shl16insli tmp0, tmp0, hw1(LINUX_MASKABLE_INTERRUPTS);	\
	shl16insli tmp0, tmp0, hw0(LINUX_MASKABLE_INTERRUPTS);	\
	mtspr   SPR_INTERRUPT_MASK_SET_K, tmp0

/* Disable ALL synchronous interrupts (used by NMI entry). */
#define IRQ_DISABLE_ALL(tmp)					\
	movei   tmp, -1;					\
	mtspr   SPR_INTERRUPT_MASK_SET_K, tmp

/* Enable interrupts. */
#define IRQ_ENABLE_LOAD(tmp0, tmp1)				\
	GET_INTERRUPTS_ENABLED_MASK_PTR(tmp0);			\
	ld      tmp0, tmp0
#define IRQ_ENABLE_APPLY(tmp0, tmp1)				\
	mtspr   SPR_INTERRUPT_MASK_RESET_K, tmp0

#else /* !__tilegx__ */

/*
 * Return 0 or 1 to indicate whether interrupts are currently disabled.
 * Note that it's important that we use a bit from the "low" mask word,
 * since when we are enabling, that is the word we write first, so if we
 * are interrupted after only writing half of the mask, the interrupt
 * handler will correctly observe that we have interrupts enabled, and
 * will enable interrupts itself on return from the interrupt handler
 * (making the original code's write of the "high" mask word idempotent).
 */
#define IRQS_DISABLED(tmp)					\
	mfspr   tmp, SPR_INTERRUPT_MASK_K_0;			\
	shri    tmp, tmp, INT_MEM_ERROR;			\
	andi    tmp, tmp, 1

/* Load up a pointer to &interrupts_enabled_mask. */
#define GET_INTERRUPTS_ENABLED_MASK_PTR(reg)			\
	moveli  reg, lo16(interrupts_enabled_mask);		\
	auli    reg, reg, ha16(interrupts_enabled_mask);	\
	add     reg, reg, tp

/* Disable interrupts. */
#define IRQ_DISABLE(tmp0, tmp1)					\
	{							\
	 movei  tmp0, -1;					\
	 moveli tmp1, lo16(LINUX_MASKABLE_INTERRUPTS_HI)	\
	};							\
	{							\
	 mtspr  SPR_INTERRUPT_MASK_SET_K_0, tmp0;		\
	 auli   tmp1, tmp1, ha16(LINUX_MASKABLE_INTERRUPTS_HI)	\
	};							\
	mtspr   SPR_INTERRUPT_MASK_SET_K_1, tmp1

/* Disable ALL synchronous interrupts (used by NMI entry). */
#define IRQ_DISABLE_ALL(tmp)					\
	movei   tmp, -1;					\
	mtspr   SPR_INTERRUPT_MASK_SET_K_0, tmp;		\
	mtspr   SPR_INTERRUPT_MASK_SET_K_1, tmp

/* Enable interrupts. */
#define IRQ_ENABLE_LOAD(tmp0, tmp1)				\
	GET_INTERRUPTS_ENABLED_MASK_PTR(tmp0);			\
	{							\
	 lw     tmp0, tmp0;					\
	 addi   tmp1, tmp0, 4					\
	};							\
	lw      tmp1, tmp1
#define IRQ_ENABLE_APPLY(tmp0, tmp1)				\
	mtspr   SPR_INTERRUPT_MASK_RESET_K_0, tmp0;		\
	mtspr   SPR_INTERRUPT_MASK_RESET_K_1, tmp1
#endif

#define IRQ_ENABLE(tmp0, tmp1)					\
	IRQ_ENABLE_LOAD(tmp0, tmp1);				\
	IRQ_ENABLE_APPLY(tmp0, tmp1)

/*
 * Do the CPU's IRQ-state tracing from assembly code. We call a
 * C function, but almost everywhere we do, we don't mind clobbering
 * all the caller-saved registers.
 */
#ifdef CONFIG_TRACE_IRQFLAGS
# define TRACE_IRQS_ON  jal trace_hardirqs_on
# define TRACE_IRQS_OFF jal trace_hardirqs_off
#else
# define TRACE_IRQS_ON
# define TRACE_IRQS_OFF
#endif

#endif /* __ASSEMBLY__ */

#endif /* _ASM_TILE_IRQFLAGS_H */
