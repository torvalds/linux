// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2008 Freescale Semiconductor, Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/mpic.h>
#include <asm/i8259.h>

#ifdef CONFIG_PPC_I8259
static void mpc86xx_8259_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int cascade_irq = i8259_irq();

	if (cascade_irq)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}
#endif	/* CONFIG_PPC_I8259 */

void __init mpc86xx_init_irq(void)
{
#ifdef CONFIG_PPC_I8259
	struct device_node *np;
	struct device_node *cascade_node = NULL;
	int cascade_irq;
#endif

	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN |
			MPIC_SINGLE_DEST_CPU,
			0, 256, " MPIC     ");
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
	if (!cascade_irq) {
		printk(KERN_ERR "Failed to map cascade interrupt\n");
		return;
	}

	i8259_init(cascade_node, 0);
	of_node_put(cascade_node);

	irq_set_chained_handler(cascade_irq, mpc86xx_8259_cascade);
#endif
}
