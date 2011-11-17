/*
 * Routines common to most mpc85xx-based boards.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/of_platform.h>

#include <sysdev/cpm2_pic.h>

#include "mpc85xx.h"

#ifdef CONFIG_CPM2
static void cpm2_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int cascade_irq;

	while ((cascade_irq = cpm2_get_irq()) >= 0)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}


void __init mpc85xx_cpm2_pic_init(void)
{
	struct device_node *np;
	int irq;

	/* Setup CPM2 PIC */
	np = of_find_compatible_node(NULL, NULL, "fsl,cpm2-pic");
	if (np == NULL) {
		printk(KERN_ERR "PIC init: can not find fsl,cpm2-pic node\n");
		return;
	}
	irq = irq_of_parse_and_map(np, 0);
	if (irq == NO_IRQ) {
		of_node_put(np);
		printk(KERN_ERR "PIC init: got no IRQ for cpm cascade\n");
		return;
	}

	cpm2_pic_init(np);
	of_node_put(np);
	irq_set_chained_handler(irq, cpm2_cascade);
}
#endif
