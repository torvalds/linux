/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_IRQ_WORK_H
#define _ASM_S390_IRQ_WORK_H

static inline bool arch_irq_work_has_interrupt(void)
{
	return true;
}

#endif /* _ASM_S390_IRQ_WORK_H */
