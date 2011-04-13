/*
 * linux/arch/m32r/kernel/irq.c
 *
 *  Copyright (c) 2003, 2004  Hitoshi Yamamoto
 *  Copyright (c) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

/*
 *	linux/arch/i386/kernel/irq.c
 *
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the lowest level m32r-specific interrupt
 * entry and irq statistics code. All the remaining irq logic is
 * done by the generic kernel/irq/ code and in the
 * m32r-specific irq controller code.
 */

#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/uaccess.h>

/*
 * do_IRQ handles all normal device IRQs (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
asmlinkage unsigned int do_IRQ(int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	old_regs = set_irq_regs(regs);
	irq_enter();

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* FIXME M32R */
#endif
	generic_handle_irq(irq);
	irq_exit();
	set_irq_regs(old_regs);

	return 1;
}
