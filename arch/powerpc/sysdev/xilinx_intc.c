/*
 * Interrupt controller driver for Xilinx Virtex FPGAs
 *
 * Copyright (C) 2007 Secret Lab Technologies Ltd.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

/*
 * This is a driver for the interrupt controller typically found in
 * Xilinx Virtex FPGA designs.
 *
 * The interrupt sense levels are hard coded into the FPGA design with
 * typically a 1:1 relationship between irq lines and devices (no shared
 * irq lines).  Therefore, this driver does not attempt to handle edge
 * and level interrupts differently.
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/i8259.h>
#include <asm/irq.h>
#include <linux/irqchip.h>

#if defined(CONFIG_PPC_I8259)
/*
 * Support code for cascading to 8259 interrupt controllers
 */
static void xilinx_i8259_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int cascade_irq = i8259_irq();

	if (cascade_irq)
		generic_handle_irq(cascade_irq);

	/* Let xilinx_intc end the interrupt */
	chip->irq_unmask(&desc->irq_data);
}

static void __init xilinx_i8259_setup_cascade(void)
{
	struct device_node *cascade_node;
	int cascade_irq;

	/* Initialize i8259 controller */
	cascade_node = of_find_compatible_node(NULL, NULL, "chrp,iic");
	if (!cascade_node)
		return;

	cascade_irq = irq_of_parse_and_map(cascade_node, 0);
	if (!cascade_irq) {
		pr_err("virtex_ml510: Failed to map cascade interrupt\n");
		goto out;
	}

	i8259_init(cascade_node, 0);
	irq_set_chained_handler(cascade_irq, xilinx_i8259_cascade);

	/* Program irq 7 (usb/audio), 14/15 (ide) to level sensitive */
	/* This looks like a dirty hack to me --gcl */
	outb(0xc0, 0x4d0);
	outb(0xc0, 0x4d1);

 out:
	of_node_put(cascade_node);
}
#else
static inline void xilinx_i8259_setup_cascade(void) { return; }
#endif /* defined(CONFIG_PPC_I8259) */

/*
 * Initialize master Xilinx interrupt controller
 */
void __init xilinx_intc_init_tree(void)
{
	irqchip_init();
	xilinx_i8259_setup_cascade();
}
