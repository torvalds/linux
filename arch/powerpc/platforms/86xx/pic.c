/*
 * Copyright 2008 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>

#include <asm/system.h>
#include <asm/mpic.h>
#include <asm/i8259.h>

#ifdef CONFIG_PPC_I8259
static void mpc86xx_8259_cascade(unsigned int irq, struct irq_desc *desc)
{
	unsigned int cascade_irq = i8259_irq();
	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);
	desc->chip->eoi(irq);
}
#endif	/* CONFIG_PPC_I8259 */

void __init mpc86xx_init_irq(void)
{
	struct mpic *mpic;
	struct device_node *np;
	struct resource res;
#ifdef CONFIG_PPC_I8259
	struct device_node *cascade_node = NULL;
	int cascade_irq;
#endif

	/* Determine PIC address. */
	np = of_find_node_by_type(NULL, "open-pic");
	if (np == NULL)
		return;
	of_address_to_resource(np, 0, &res);

	mpic = mpic_alloc(np, res.start,
			MPIC_PRIMARY | MPIC_WANTS_RESET |
			MPIC_BIG_ENDIAN | MPIC_BROKEN_FRR_NIRQS |
			MPIC_SINGLE_DEST_CPU,
			0, 256, " MPIC     ");
	of_node_put(np);
	BUG_ON(mpic == NULL);

	mpic_init(mpic);

#ifdef CONFIG_PPC_I8259
	/* Initialize i8259 controller */
	for_each_node_by_type(np, "interrupt-controller")
		if (of_device_is_compatible(np, "chrp,iic")) {
			cascade_node = np;
			break;
		}

	if (cascade_node == NULL) {
		printk(KERN_DEBUG "Could not find i8259 PIC\n");
		return;
	}

	cascade_irq = irq_of_parse_and_map(cascade_node, 0);
	if (cascade_irq == NO_IRQ) {
		printk(KERN_ERR "Failed to map cascade interrupt\n");
		return;
	}

	i8259_init(cascade_node, 0);
	of_node_put(cascade_node);

	set_irq_chained_handler(cascade_irq, mpc86xx_8259_cascade);
#endif
}
