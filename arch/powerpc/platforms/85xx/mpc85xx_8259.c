// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MPC85xx 8259 functions for DS Board Setup
 *
 * Author Xianghua Xiao (x.xiao@freescale.com)
 * Roy Zang <tie-fei.zang@freescale.com>
 *      - Add PCI/PCI Express support
 * Copyright 2007 Freescale Semiconductor Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/mpic.h>
#include <asm/i8259.h>

#include "mpc85xx.h"

static void mpc85xx_8259_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int cascade_irq = i8259_irq();

	if (cascade_irq)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}

void __init mpc85xx_8259_init(void)
{
	struct device_node *np;
	struct device_node *cascade_node = NULL;
	int cascade_irq;

	/* Initialize the i8259 controller */
	for_each_node_by_type(np, "interrupt-controller") {
		if (of_device_is_compatible(np, "chrp,iic")) {
			cascade_node = np;
			break;
		}
	}

	if (cascade_node == NULL) {
		pr_debug("i8259: Could not find i8259 PIC\n");
		return;
	}

	cascade_irq = irq_of_parse_and_map(cascade_node, 0);
	if (!cascade_irq) {
		pr_err("i8259: Failed to map cascade interrupt\n");
		return;
	}

	pr_debug("i8259: cascade mapped to irq %d\n", cascade_irq);

	i8259_init(cascade_node, 0);
	of_node_put(cascade_node);

	irq_set_chained_handler(cascade_irq, mpc85xx_8259_cascade);
}
