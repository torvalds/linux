/*
 * OpenRISC irq.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/ftrace.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/export.h>
#include <linux/irqflags.h>

/* read interrupt enabled status */
unsigned long arch_local_save_flags(void)
{
	return mfspr(SPR_SR) & (SPR_SR_IEE|SPR_SR_TEE);
}
EXPORT_SYMBOL(arch_local_save_flags);

/* set interrupt enabled status */
void arch_local_irq_restore(unsigned long flags)
{
	mtspr(SPR_SR, ((mfspr(SPR_SR) & ~(SPR_SR_IEE|SPR_SR_TEE)) | flags));
}
EXPORT_SYMBOL(arch_local_irq_restore);

void __init init_IRQ(void)
{
	irqchip_init();
}

static void (*handle_arch_irq)(struct pt_regs *);

void __init set_handle_irq(void (*handle_irq)(struct pt_regs *))
{
	handle_arch_irq = handle_irq;
}

void handle_IRQ(unsigned int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();

	generic_handle_irq(irq);

	irq_exit();
	set_irq_regs(old_regs);
}

void __irq_entry do_IRQ(struct pt_regs *regs)
{
	handle_arch_irq(regs);
}
