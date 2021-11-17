/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARM_IRQ_WORK_H
#define __ASM_ARM_IRQ_WORK_H

#include <asm/smp_plat.h>

static inline bool arch_irq_work_has_interrupt(void)
{
	return is_smp();
}

#endif /* _ASM_ARM_IRQ_WORK_H */
