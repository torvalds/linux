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
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/irq.h>

/*
 * INTC Registers
 */
#define XINTC_ISR	0	/* Interrupt Status */
#define XINTC_IPR	4	/* Interrupt Pending */
#define XINTC_IER	8	/* Interrupt Enable */
#define XINTC_IAR	12	/* Interrupt Acknowledge */
#define XINTC_SIE	16	/* Set Interrupt Enable bits */
#define XINTC_CIE	20	/* Clear Interrupt Enable bits */
#define XINTC_IVR	24	/* Interrupt Vector */
#define XINTC_MER	28	/* Master Enable */

static struct irq_host *master_irqhost;

/*
 * IRQ Chip operations
 */
static void xilinx_intc_mask(unsigned int virq)
{
	int irq = virq_to_hw(virq);
	void * regs = get_irq_chip_data(virq);
	pr_debug("mask: %d\n", irq);
	out_be32(regs + XINTC_CIE, 1 << irq);
}

static void xilinx_intc_unmask(unsigned int virq)
{
	int irq = virq_to_hw(virq);
	void * regs = get_irq_chip_data(virq);
	pr_debug("unmask: %d\n", irq);
	out_be32(regs + XINTC_SIE, 1 << irq);
}

static void xilinx_intc_ack(unsigned int virq)
{
	int irq = virq_to_hw(virq);
	void * regs = get_irq_chip_data(virq);
	pr_debug("ack: %d\n", irq);
	out_be32(regs + XINTC_IAR, 1 << irq);
}

static struct irq_chip xilinx_intc_irqchip = {
	.typename = "Xilinx INTC",
	.mask = xilinx_intc_mask,
	.unmask = xilinx_intc_unmask,
	.ack = xilinx_intc_ack,
};

/*
 * IRQ Host operations
 */
static int xilinx_intc_map(struct irq_host *h, unsigned int virq,
				  irq_hw_number_t irq)
{
	set_irq_chip_data(virq, h->host_data);
	set_irq_chip_and_handler(virq, &xilinx_intc_irqchip, handle_level_irq);
	set_irq_type(virq, IRQ_TYPE_NONE);
	return 0;
}

static struct irq_host_ops xilinx_intc_ops = {
	.map = xilinx_intc_map,
};

struct irq_host * __init
xilinx_intc_init(struct device_node *np)
{
	struct irq_host * irq;
	struct resource res;
	void * regs;
	int rc;

	/* Find and map the intc registers */
	rc = of_address_to_resource(np, 0, &res);
	if (rc) {
		printk(KERN_ERR __FILE__ ": of_address_to_resource() failed\n");
		return NULL;
	}
	regs = ioremap(res.start, 32);

	printk(KERN_INFO "Xilinx intc at 0x%08LX mapped to 0x%p\n",
		res.start, regs);

	/* Setup interrupt controller */
	out_be32(regs + XINTC_IER, 0); /* disable all irqs */
	out_be32(regs + XINTC_IAR, ~(u32) 0); /* Acknowledge pending irqs */
	out_be32(regs + XINTC_MER, 0x3UL); /* Turn on the Master Enable. */

	/* Allocate and initialize an irq_host structure. */
	irq = irq_alloc_host(np, IRQ_HOST_MAP_LINEAR, 32, &xilinx_intc_ops, -1);
	if (!irq)
		panic(__FILE__ ": Cannot allocate IRQ host\n");
	irq->host_data = regs;
	return irq;
}

int xilinx_intc_get_irq(void)
{
	void * regs = master_irqhost->host_data;
	pr_debug("get_irq:\n");
	return irq_linear_revmap(master_irqhost, in_be32(regs + XINTC_IVR));
}

void __init xilinx_intc_init_tree(void)
{
	struct device_node *np;

	/* find top level interrupt controller */
	for_each_compatible_node(np, NULL, "xlnx,opb-intc-1.00.c") {
		if (!of_get_property(np, "interrupts", NULL))
			break;
	}
	if (!np) {
		for_each_compatible_node(np, NULL, "xlnx,xps-intc-1.00.a") {
			if (!of_get_property(np, "interrupts", NULL))
				break;
		}
	}

	/* xilinx interrupt controller needs to be top level */
	BUG_ON(!np);

	master_irqhost = xilinx_intc_init(np);
	BUG_ON(!master_irqhost);

	irq_set_default_host(master_irqhost);
	of_node_put(np);
}
