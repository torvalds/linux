// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/powerpc/platforms/embedded6xx/flipper-pic.c
 *
 * Nintendo GameCube/Wii "Flipper" interrupt controller support.
 * Copyright (C) 2004-2009 The GameCube Linux Team
 * Copyright (C) 2007,2008,2009 Albert Herranz
 */
#define DRV_MODULE_NAME "flipper-pic"
#define pr_fmt(fmt) DRV_MODULE_NAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/io.h>

#include "flipper-pic.h"

#define FLIPPER_NR_IRQS		32

/*
 * Each interrupt has a corresponding bit in both
 * the Interrupt Cause (ICR) and Interrupt Mask (IMR) registers.
 *
 * Enabling/disabling an interrupt line involves setting/clearing
 * the corresponding bit in IMR.
 * Except for the RSW interrupt, all interrupts get deasserted automatically
 * when the source deasserts the interrupt.
 */
#define FLIPPER_ICR		0x00
#define FLIPPER_ICR_RSS		(1<<16) /* reset switch state */

#define FLIPPER_IMR		0x04

#define FLIPPER_RESET		0x24


/*
 * IRQ chip hooks.
 *
 */

static void flipper_pic_mask_and_ack(struct irq_data *d)
{
	int irq = irqd_to_hwirq(d);
	void __iomem *io_base = irq_data_get_irq_chip_data(d);
	u32 mask = 1 << irq;

	clrbits32(io_base + FLIPPER_IMR, mask);
	/* this is at least needed for RSW */
	out_be32(io_base + FLIPPER_ICR, mask);
}

static void flipper_pic_ack(struct irq_data *d)
{
	int irq = irqd_to_hwirq(d);
	void __iomem *io_base = irq_data_get_irq_chip_data(d);

	/* this is at least needed for RSW */
	out_be32(io_base + FLIPPER_ICR, 1 << irq);
}

static void flipper_pic_mask(struct irq_data *d)
{
	int irq = irqd_to_hwirq(d);
	void __iomem *io_base = irq_data_get_irq_chip_data(d);

	clrbits32(io_base + FLIPPER_IMR, 1 << irq);
}

static void flipper_pic_unmask(struct irq_data *d)
{
	int irq = irqd_to_hwirq(d);
	void __iomem *io_base = irq_data_get_irq_chip_data(d);

	setbits32(io_base + FLIPPER_IMR, 1 << irq);
}


static struct irq_chip flipper_pic = {
	.name		= "flipper-pic",
	.irq_ack	= flipper_pic_ack,
	.irq_mask_ack	= flipper_pic_mask_and_ack,
	.irq_mask	= flipper_pic_mask,
	.irq_unmask	= flipper_pic_unmask,
};

/*
 * IRQ host hooks.
 *
 */

static struct irq_domain *flipper_irq_host;

static int flipper_pic_map(struct irq_domain *h, unsigned int virq,
			   irq_hw_number_t hwirq)
{
	irq_set_chip_data(virq, h->host_data);
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &flipper_pic, handle_level_irq);
	return 0;
}

static const struct irq_domain_ops flipper_irq_domain_ops = {
	.map = flipper_pic_map,
};

/*
 * Platform hooks.
 *
 */

static void __flipper_quiesce(void __iomem *io_base)
{
	/* mask and ack all IRQs */
	out_be32(io_base + FLIPPER_IMR, 0x00000000);
	out_be32(io_base + FLIPPER_ICR, 0xffffffff);
}

static struct irq_domain * __init flipper_pic_init(struct device_node *np)
{
	struct device_node *pi;
	struct irq_domain *irq_domain = NULL;
	struct resource res;
	void __iomem *io_base;
	int retval;

	pi = of_get_parent(np);
	if (!pi) {
		pr_err("no parent found\n");
		goto out;
	}
	if (!of_device_is_compatible(pi, "nintendo,flipper-pi")) {
		pr_err("unexpected parent compatible\n");
		goto out;
	}

	retval = of_address_to_resource(pi, 0, &res);
	if (retval) {
		pr_err("no io memory range found\n");
		goto out;
	}
	io_base = ioremap(res.start, resource_size(&res));

	pr_info("controller at 0x%pa mapped to 0x%p\n", &res.start, io_base);

	__flipper_quiesce(io_base);

	irq_domain = irq_domain_create_linear(of_fwnode_handle(np),
					      FLIPPER_NR_IRQS,
					      &flipper_irq_domain_ops, io_base);
	if (!irq_domain) {
		pr_err("failed to allocate irq_domain\n");
		return NULL;
	}

out:
	return irq_domain;
}

unsigned int flipper_pic_get_irq(void)
{
	void __iomem *io_base = flipper_irq_host->host_data;
	int irq;
	u32 irq_status;

	irq_status = in_be32(io_base + FLIPPER_ICR) &
		     in_be32(io_base + FLIPPER_IMR);
	if (irq_status == 0)
		return 0;	/* no more IRQs pending */

	irq = __ffs(irq_status);
	return irq_find_mapping(flipper_irq_host, irq);
}

/*
 * Probe function.
 *
 */

void __init flipper_pic_probe(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "nintendo,flipper-pic");
	BUG_ON(!np);

	flipper_irq_host = flipper_pic_init(np);
	BUG_ON(!flipper_irq_host);

	irq_set_default_domain(flipper_irq_host);

	of_node_put(np);
}

/*
 * Misc functions related to the flipper chipset.
 *
 */

/**
 * flipper_quiesce() - quiesce flipper irq controller
 *
 * Mask and ack all interrupt sources.
 *
 */
void flipper_quiesce(void)
{
	void __iomem *io_base = flipper_irq_host->host_data;

	__flipper_quiesce(io_base);
}

/*
 * Resets the platform.
 */
void flipper_platform_reset(void)
{
	void __iomem *io_base;

	if (flipper_irq_host && flipper_irq_host->host_data) {
		io_base = flipper_irq_host->host_data;
		out_8(io_base + FLIPPER_RESET, 0x00);
	}
}

/*
 * Returns non-zero if the reset button is pressed.
 */
int flipper_is_reset_button_pressed(void)
{
	void __iomem *io_base;
	u32 icr;

	if (flipper_irq_host && flipper_irq_host->host_data) {
		io_base = flipper_irq_host->host_data;
		icr = in_be32(io_base + FLIPPER_ICR);
		return !(icr & FLIPPER_ICR_RSS);
	}
	return 0;
}

