/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_IRQ_H
#define __ASM_IRQ_H

#ifndef __ASSEMBLER__

#include <asm-generic/irq.h>

struct pt_regs;

int set_handle_irq(void (*handle_irq)(struct pt_regs *));
#define set_handle_irq	set_handle_irq

static inline int nr_legacy_irqs(void)
{
	return 0;
}

#endif /* !__ASSEMBLER__ */
#endif
