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

static struct irq_domain *master_irqhost;

#define XILINX_INTC_MAXIRQS	(32)

/* The following table allows the interrupt type, edge or level,
 * to be cached after being read from the device tree until the interrupt
 * is mapped
 */
static int xilinx_intc_typetable[XILINX_INTC_MAXIRQS];

/* Map the interrupt type from the device tree to the interrupt types
 * used by the interrupt subsystem
 */
static unsigned char xilinx_intc_map_senses[] = {
	IRQ_TYPE_EDGE_RISING,
	IRQ_TYPE_EDGE_FALLING,
	IRQ_TYPE_LEVEL_HIGH,
	IRQ_TYPE_LEVEL_LOW,
};

/*
 * The interrupt controller is setup such that it doesn't work well with
 * the level interrupt handler in the kernel because the handler acks the
 * interrupt before calling the application interrupt handler. To deal with
 * that, we use 2 different irq chips so that different functions can be
 * used for level and edge type interrupts.
 *
 * IRQ Chip common (across level and edge) operations
 */
static void xilinx_intc_mask(struct irq_data *d)
{
	int irq = irqd_to_hwirq(d);
	void * regs = irq_data_get_irq_chip_data(d);
	pr_debug("mask: %d\n", irq);
	out_be32(regs + XINTC_CIE, 1 << irq);
}

static int xilinx_intc_set_type(struct irq_data *d, unsigned int flow_type)
{
	return 0;
}

/*
 * IRQ Chip level operations
 */
static void xilinx_intc_level_unmask(struct irq_data *d)
{
	int irq = irqd_to_hwirq(d);
	void * regs = irq_data_get_irq_chip_data(d);
	pr_debug("unmask: %d\n", irq);
	out_be32(regs + XINTC_SIE, 1 << irq);

	/* ack level irqs because they can't be acked during
	 * ack function since the handle_level_irq function
	 * acks the irq before calling the inerrupt handler
	 */
	out_be32(regs + XINTC_IAR, 1 << irq);
}

static struct irq_chip xilinx_intc_level_irqchip = {
	.name = "Xilinx Level INTC",
	.irq_mask = xilinx_intc_mask,
	.irq_mask_ack = xilinx_intc_mask,
	.irq_unmask = xilinx_intc_level_unmask,
	.irq_set_type = xilinx_intc_set_type,
};

/*
 * IRQ Chip edge operations
 */
static void xilinx_intc_edge_unmask(struct irq_data *d)
{
	int irq = irqd_to_hwirq(d);
	void *regs = irq_data_get_irq_chip_data(d);
	pr_debug("unmask: %d\n", irq);
	out_be32(regs + XINTC_SIE, 1 << irq);
}

static void xilinx_intc_edge_ack(struct irq_data *d)
{
	int irq = irqd_to_hwirq(d);
	void * regs = irq_data_get_irq_chip_data(d);
	pr_debug("ack: %d\n", irq);
	out_be32(regs + XINTC_IAR, 1 << irq);
}

static struct irq_chip xilinx_intc_edge_irqchip = {
	.name = "Xilinx Edge  INTC",
	.irq_mask = xilinx_intc_mask,
	.irq_unmask = xilinx_intc_edge_unmask,
	.irq_ack = xilinx_intc_edge_ack,
	.irq_set_type = xilinx_intc_set_type,
};

/*
 * IRQ Host operations
 */

/**
 * xilinx_intc_xlate - translate virq# from device tree interrupts property
 */
static int xilinx_intc_xlate(struct irq_domain *h, struct device_node *ct,
				const u32 *intspec, unsigned int intsize,
				irq_hw_number_t *out_hwirq,
				unsigned int *out_flags)
{
	if ((intsize < 2) || (intspec[0] >= XILINX_INTC_MAXIRQS))
		return -EINVAL;

	/* keep a copy of the interrupt type til the interrupt is mapped
	 */
	xilinx_intc_typetable[intspec[0]] = xilinx_intc_map_senses[intspec[1]];

	/* Xilinx uses 2 interrupt entries, the 1st being the h/w
	 * interrupt number, the 2nd being the interrupt type, edge or level
	 */
	*out_hwirq = intspec[0];
	*out_flags = xilinx_intc_map_senses[intspec[1]];

	return 0;
}
static int xilinx_intc_map(struct irq_domain *h, unsigned int virq,
				  irq_hw_number_t irq)
{
	irq_set_chip_data(virq, h->host_data);

	if (xilinx_intc_typetable[irq] == IRQ_TYPE_LEVEL_HIGH ||
	    xilinx_intc_typetable[irq] == IRQ_TYPE_LEVEL_LOW) {
		irq_set_chip_and_handler(virq, &xilinx_intc_level_irqchip,
					 handle_level_irq);
	} else {
		irq_set_chip_and_handler(virq, &xilinx_intc_edge_irqchip,
					 handle_edge_irq);
	}
	return 0;
}

static const struct irq_domain_ops xilinx_intc_ops = {
	.map = xilinx_intc_map,
	.xlate = xilinx_intc_xlate,
};

struct irq_domain * __init
xilinx_intc_init(struct device_node *np)
{
	struct irq_domain * irq;
	void * regs;

	/* Find and map the intc registers */
	regs = of_iomap(np, 0);
	if (!regs) {
		pr_err("xilinx_intc: could not map registers\n");
		return NULL;
	}

	/* Setup interrupt controller */
	out_be32(regs + XINTC_IER, 0); /* disable all irqs */
	out_be32(regs + XINTC_IAR, ~(u32) 0); /* Acknowledge pending irqs */
	out_be32(regs + XINTC_MER, 0x3UL); /* Turn on the Master Enable. */

	/* Allocate and initialize an irq_domain structure. */
	irq = irq_domain_add_linear(np, XILINX_INTC_MAXIRQS, &xilinx_intc_ops,
				    regs);
	if (!irq)
		panic(__FILE__ ": Cannot allocate IRQ host\n");

	return irq;
}

int xilinx_intc_get_irq(void)
{
	void * regs = master_irqhost->host_data;
	pr_debug("get_irq:\n");
	return irq_linear_revmap(master_irqhost, in_be32(regs + XINTC_IVR));
}

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

static const struct of_device_id xilinx_intc_match[] __initconst = {
	{ .compatible = "xlnx,opb-intc-1.00.c", },
	{ .compatible = "xlnx,xps-intc-1.00.a", },
	{}
};

/*
 * Initialize master Xilinx interrupt controller
 */
void __init xilinx_intc_init_tree(void)
{
	struct device_node *np;

	/* find top level interrupt controller */
	for_each_matching_node(np, xilinx_intc_match) {
		if (!of_get_property(np, "interrupts", NULL))
			break;
	}
	BUG_ON(!np);

	master_irqhost = xilinx_intc_init(np);
	BUG_ON(!master_irqhost);

	irq_set_default_host(master_irqhost);
	of_node_put(np);

	xilinx_i8259_setup_cascade();
}
