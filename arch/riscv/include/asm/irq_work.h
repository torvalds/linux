/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_IRQ_WORK_H
#define _ASM_RISCV_IRQ_WORK_H

static inline bool arch_irq_work_has_interrupt(void)
{
	return IS_ENABLED(CONFIG_SMP);
}
extern void arch_irq_work_raise(void);
#endif /* _ASM_RISCV_IRQ_WORK_H */
