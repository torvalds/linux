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

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/ftrace.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/kernel_stat.h>

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


/* OR1K PIC implementation */

/* We're a couple of cycles faster than the generic implementations with
 * these 'fast' versions.
 */

static void or1k_pic_mask(struct irq_data *data)
{
	mtspr(SPR_PICMR, mfspr(SPR_PICMR) & ~(1UL << data->irq));
}

static void or1k_pic_unmask(struct irq_data *data)
{
	mtspr(SPR_PICMR, mfspr(SPR_PICMR) | (1UL << data->irq));
}

static void or1k_pic_ack(struct irq_data *data)
{
	/* EDGE-triggered interrupts need to be ack'ed in order to clear
	 * the latch.
	 * LEVER-triggered interrupts do not need to be ack'ed; however,
	 * ack'ing the interrupt has no ill-effect and is quicker than
	 * trying to figure out what type it is...
	 */

	/* The OpenRISC 1000 spec says to write a 1 to the bit to ack the
	 * interrupt, but the OR1200 does this backwards and requires a 0
	 * to be written...
	 */

#ifdef CONFIG_OR1K_1200
	/* There are two oddities with the OR1200 PIC implementation:
	 * i)  LEVEL-triggered interrupts are latched and need to be cleared
	 * ii) the interrupt latch is cleared by writing a 0 to the bit,
	 *     as opposed to a 1 as mandated by the spec
	 */

	mtspr(SPR_PICSR, mfspr(SPR_PICSR) & ~(1UL << data->irq));
#else
	WARN(1, "Interrupt handling possibily broken\n");
	mtspr(SPR_PICSR, (1UL << irq));
#endif
}

static void or1k_pic_mask_ack(struct irq_data *data)
{
	/* Comments for pic_ack apply here, too */

#ifdef CONFIG_OR1K_1200
	mtspr(SPR_PICSR, mfspr(SPR_PICSR) & ~(1UL << data->irq));
#else
	WARN(1, "Interrupt handling possibily broken\n");
	mtspr(SPR_PICSR, (1UL << irq));
#endif
}

static int or1k_pic_set_type(struct irq_data *data, unsigned int flow_type)
{
	/* There's nothing to do in the PIC configuration when changing
	 * flow type.  Level and edge-triggered interrupts are both
	 * supported, but it's PIC-implementation specific which type
	 * is handled. */

	return irq_setup_alt_chip(data, flow_type);
}

static inline int pic_get_irq(int first)
{
	int irq;

	irq = ffs(mfspr(SPR_PICSR) >> first);

	return irq ? irq + first - 1 : NO_IRQ;
}

static void __init or1k_irq_init(void)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	/* Disable all interrupts until explicitly requested */
	mtspr(SPR_PICMR, (0UL));

	gc = irq_alloc_generic_chip("or1k-PIC", 1, 0, 0, handle_level_irq);
	ct = gc->chip_types;

	ct->chip.irq_unmask = or1k_pic_unmask;
	ct->chip.irq_mask = or1k_pic_mask;
	ct->chip.irq_ack = or1k_pic_ack;
	ct->chip.irq_mask_ack = or1k_pic_mask_ack;
	ct->chip.irq_set_type = or1k_pic_set_type;

	/* The OR1K PIC can handle both level and edge trigged
	 * interrupts in roughly the same manner
	 */
#if 0
	/* FIXME: chip.type??? */
	ct->chip.type = IRQ_TYPE_EDGE_BOTH | IRQ_TYPE_LEVEL_MASK;
#endif

	irq_setup_generic_chip(gc, IRQ_MSK(NR_IRQS), 0,
			       IRQ_NOREQUEST, IRQ_LEVEL | IRQ_NOPROBE);
}

void __init init_IRQ(void)
{
	or1k_irq_init();
}

void __irq_entry do_IRQ(struct pt_regs *regs)
{
	int irq = -1;
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();

	while ((irq = pic_get_irq(irq + 1)) != NO_IRQ)
		generic_handle_irq(irq);

	irq_exit();
	set_irq_regs(old_regs);
}

unsigned int irq_create_of_mapping(struct device_node *controller,
				   const u32 *intspec, unsigned int intsize)
{
	return intspec[0];
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);
