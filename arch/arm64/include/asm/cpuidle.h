/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_CPUIDLE_H
#define __ASM_CPUIDLE_H

#include <asm/proc-fns.h>

#ifdef CONFIG_ARM64_PSEUDO_NMI
#include <asm/arch_gicv3.h>

struct arm_cpuidle_irq_context {
	unsigned long pmr;
	unsigned long daif_bits;
};

#define arm_cpuidle_save_irq_context(__c)				\
	do {								\
		struct arm_cpuidle_irq_context *c = __c;		\
		if (system_uses_irq_prio_masking()) {			\
			c->daif_bits = read_sysreg(daif);		\
			write_sysreg(c->daif_bits | PSR_I_BIT | PSR_F_BIT, \
				     daif);				\
			c->pmr = gic_read_pmr();			\
			gic_write_pmr(GIC_PRIO_IRQON | GIC_PRIO_PSR_I_SET); \
		}							\
	} while (0)

#define arm_cpuidle_restore_irq_context(__c)				\
	do {								\
		struct arm_cpuidle_irq_context *c = __c;		\
		if (system_uses_irq_prio_masking()) {			\
			gic_write_pmr(c->pmr);				\
			write_sysreg(c->daif_bits, daif);		\
		}							\
	} while (0)
#else
struct arm_cpuidle_irq_context { };

#define arm_cpuidle_save_irq_context(c)		(void)c
#define arm_cpuidle_restore_irq_context(c)	(void)c
#endif
#endif
