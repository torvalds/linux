/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_IRQFLAGS_H
#define __ASM_IRQFLAGS_H

#ifdef __KERNEL__

#include <asm/alternative.h>
#include <asm/ptrace.h>
#include <asm/sysreg.h>

/*
 * Aarch64 has flags for masking: Debug, Asynchronous (serror), Interrupts and
 * FIQ exceptions, in the 'daif' register. We mask and unmask them in 'dai'
 * order:
 * Masking debug exceptions causes all other exceptions to be masked too/
 * Masking SError masks irq, but not debug exceptions. Masking irqs has no
 * side effects for other flags. Keeping to this order makes it easier for
 * entry.S to know which exceptions should be unmasked.
 *
 * FIQ is never expected, but we mask it when we disable debug exceptions, and
 * unmask it at all other times.
 */

/*
 * CPU interrupt mask handling.
 */
static inline void arch_local_irq_enable(void)
{
	asm volatile(ALTERNATIVE(
		"msr	daifclr, #2		// arch_local_irq_enable\n"
		"nop",
		"msr_s  " __stringify(SYS_ICC_PMR_EL1) ",%0\n"
		"dsb	sy",
		ARM64_HAS_IRQ_PRIO_MASKING)
		:
		: "r" ((unsigned long) GIC_PRIO_IRQON)
		: "memory");
}

static inline void arch_local_irq_disable(void)
{
	asm volatile(ALTERNATIVE(
		"msr	daifset, #2		// arch_local_irq_disable",
		"msr_s  " __stringify(SYS_ICC_PMR_EL1) ", %0",
		ARM64_HAS_IRQ_PRIO_MASKING)
		:
		: "r" ((unsigned long) GIC_PRIO_IRQOFF)
		: "memory");
}

/*
 * Save the current interrupt enable state.
 */
static inline unsigned long arch_local_save_flags(void)
{
	unsigned long daif_bits;
	unsigned long flags;

	daif_bits = read_sysreg(daif);

	/*
	 * The asm is logically equivalent to:
	 *
	 * if (system_uses_irq_prio_masking())
	 *	flags = (daif_bits & PSR_I_BIT) ?
	 *			GIC_PRIO_IRQOFF :
	 *			read_sysreg_s(SYS_ICC_PMR_EL1);
	 * else
	 *	flags = daif_bits;
	 */
	asm volatile(ALTERNATIVE(
			"mov	%0, %1\n"
			"nop\n"
			"nop",
			"mrs_s	%0, " __stringify(SYS_ICC_PMR_EL1) "\n"
			"ands	%1, %1, " __stringify(PSR_I_BIT) "\n"
			"csel	%0, %0, %2, eq",
			ARM64_HAS_IRQ_PRIO_MASKING)
		: "=&r" (flags), "+r" (daif_bits)
		: "r" ((unsigned long) GIC_PRIO_IRQOFF)
		: "memory");

	return flags;
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

	flags = arch_local_save_flags();

	arch_local_irq_disable();

	return flags;
}

/*
 * restore saved IRQ state
 */
static inline void arch_local_irq_restore(unsigned long flags)
{
	asm volatile(ALTERNATIVE(
			"msr	daif, %0\n"
			"nop",
			"msr_s	" __stringify(SYS_ICC_PMR_EL1) ", %0\n"
			"dsb	sy",
			ARM64_HAS_IRQ_PRIO_MASKING)
		: "+r" (flags)
		:
		: "memory");
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	int res;

	asm volatile(ALTERNATIVE(
			"and	%w0, %w1, #" __stringify(PSR_I_BIT) "\n"
			"nop",
			"cmp	%w1, #" __stringify(GIC_PRIO_IRQOFF) "\n"
			"cset	%w0, ls",
			ARM64_HAS_IRQ_PRIO_MASKING)
		: "=&r" (res)
		: "r" ((int) flags)
		: "memory");

	return res;
}
#endif
#endif
