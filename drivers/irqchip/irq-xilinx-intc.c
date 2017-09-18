/*
 * Copyright (C) 2007-2013 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2012-2013 Xilinx, Inc.
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/jump_label.h>
#include <linux/bug.h>
#include <linux/of_irq.h>

/* No one else should require these constants, so define them locally here. */
#define ISR 0x00			/* Interrupt Status Register */
#define IPR 0x04			/* Interrupt Pending Register */
#define IER 0x08			/* Interrupt Enable Register */
#define IAR 0x0c			/* Interrupt Acknowledge Register */
#define SIE 0x10			/* Set Interrupt Enable bits */
#define CIE 0x14			/* Clear Interrupt Enable bits */
#define IVR 0x18			/* Interrupt Vector Register */
#define MER 0x1c			/* Master Enable Register */

#define MER_ME (1<<0)
#define MER_HIE (1<<1)

static DEFINE_STATIC_KEY_FALSE(xintc_is_be);

struct xintc_irq_chip {
	void		__iomem *base;
	struct		irq_domain *root_domain;
	u32		intr_mask;
};

static struct xintc_irq_chip *xintc_irqc;

static void xintc_write(int reg, u32 data)
{
	if (static_branch_unlikely(&xintc_is_be))
		iowrite32be(data, xintc_irqc->base + reg);
	else
		iowrite32(data, xintc_irqc->base + reg);
}

static unsigned int xintc_read(int reg)
{
	if (static_branch_unlikely(&xintc_is_be))
		return ioread32be(xintc_irqc->base + reg);
	else
		return ioread32(xintc_irqc->base + reg);
}

static void intc_enable_or_unmask(struct irq_data *d)
{
	unsigned long mask = 1 << d->hwirq;

	pr_debug("irq-xilinx: enable_or_unmask: %ld\n", d->hwirq);

	/* ack level irqs because they can't be acked during
	 * ack function since the handle_level_irq function
	 * acks the irq before calling the interrupt handler
	 */
	if (irqd_is_level_type(d))
		xintc_write(IAR, mask);

	xintc_write(SIE, mask);
}

static void intc_disable_or_mask(struct irq_data *d)
{
	pr_debug("irq-xilinx: disable: %ld\n", d->hwirq);
	xintc_write(CIE, 1 << d->hwirq);
}

static void intc_ack(struct irq_data *d)
{
	pr_debug("irq-xilinx: ack: %ld\n", d->hwirq);
	xintc_write(IAR, 1 << d->hwirq);
}

static void intc_mask_ack(struct irq_data *d)
{
	unsigned long mask = 1 << d->hwirq;

	pr_debug("irq-xilinx: disable_and_ack: %ld\n", d->hwirq);
	xintc_write(CIE, mask);
	xintc_write(IAR, mask);
}

static struct irq_chip intc_dev = {
	.name = "Xilinx INTC",
	.irq_unmask = intc_enable_or_unmask,
	.irq_mask = intc_disable_or_mask,
	.irq_ack = intc_ack,
	.irq_mask_ack = intc_mask_ack,
};

unsigned int xintc_get_irq(void)
{
	unsigned int hwirq, irq = -1;

	hwirq = xintc_read(IVR);
	if (hwirq != -1U)
		irq = irq_find_mapping(xintc_irqc->root_domain, hwirq);

	pr_debug("irq-xilinx: hwirq=%d, irq=%d\n", hwirq, irq);

	return irq;
}

static int xintc_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	if (xintc_irqc->intr_mask & (1 << hw)) {
		irq_set_chip_and_handler_name(irq, &intc_dev,
						handle_edge_irq, "edge");
		irq_clear_status_flags(irq, IRQ_LEVEL);
	} else {
		irq_set_chip_and_handler_name(irq, &intc_dev,
						handle_level_irq, "level");
		irq_set_status_flags(irq, IRQ_LEVEL);
	}
	return 0;
}

static const struct irq_domain_ops xintc_irq_domain_ops = {
	.xlate = irq_domain_xlate_onetwocell,
	.map = xintc_map,
};

static void xil_intc_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 pending;

	chained_irq_enter(chip, desc);
	do {
		pending = xintc_get_irq();
		if (pending == -1U)
			break;
		generic_handle_irq(pending);
	} while (true);
	chained_irq_exit(chip, desc);
}

static int __init xilinx_intc_of_init(struct device_node *intc,
					     struct device_node *parent)
{
	u32 nr_irq;
	int ret, irq;
	struct xintc_irq_chip *irqc;

	if (xintc_irqc) {
		pr_err("irq-xilinx: Multiple instances aren't supported\n");
		return -EINVAL;
	}

	irqc = kzalloc(sizeof(*irqc), GFP_KERNEL);
	if (!irqc)
		return -ENOMEM;

	xintc_irqc = irqc;

	irqc->base = of_iomap(intc, 0);
	BUG_ON(!irqc->base);

	ret = of_property_read_u32(intc, "xlnx,num-intr-inputs", &nr_irq);
	if (ret < 0) {
		pr_err("irq-xilinx: unable to read xlnx,num-intr-inputs\n");
		goto err_alloc;
	}

	ret = of_property_read_u32(intc, "xlnx,kind-of-intr", &irqc->intr_mask);
	if (ret < 0) {
		pr_warn("irq-xilinx: unable to read xlnx,kind-of-intr\n");
		irqc->intr_mask = 0;
	}

	if (irqc->intr_mask >> nr_irq)
		pr_warn("irq-xilinx: mismatch in kind-of-intr param\n");

	pr_info("irq-xilinx: %pOF: num_irq=%d, edge=0x%x\n",
		intc, nr_irq, irqc->intr_mask);


	/*
	 * Disable all external interrupts until they are
	 * explicity requested.
	 */
	xintc_write(IER, 0);

	/* Acknowledge any pending interrupts just in case. */
	xintc_write(IAR, 0xffffffff);

	/* Turn on the Master Enable. */
	xintc_write(MER, MER_HIE | MER_ME);
	if (!(xintc_read(MER) & (MER_HIE | MER_ME))) {
		static_branch_enable(&xintc_is_be);
		xintc_write(MER, MER_HIE | MER_ME);
	}

	irqc->root_domain = irq_domain_add_linear(intc, nr_irq,
						  &xintc_irq_domain_ops, irqc);
	if (!irqc->root_domain) {
		pr_err("irq-xilinx: Unable to create IRQ domain\n");
		goto err_alloc;
	}

	if (parent) {
		irq = irq_of_parse_and_map(intc, 0);
		if (irq) {
			irq_set_chained_handler_and_data(irq,
							 xil_intc_irq_handler,
							 irqc);
		} else {
			pr_err("irq-xilinx: interrupts property not in DT\n");
			ret = -EINVAL;
			goto err_alloc;
		}
	} else {
		irq_set_default_host(irqc->root_domain);
	}

	return 0;

err_alloc:
	xintc_irqc = NULL;
	kfree(irqc);
	return ret;

}

IRQCHIP_DECLARE(xilinx_intc_xps, "xlnx,xps-intc-1.00.a", xilinx_intc_of_init);
IRQCHIP_DECLARE(xilinx_intc_opb, "xlnx,opb-intc-1.00.c", xilinx_intc_of_init);
