// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for 'media5200-platform' compatible boards.
 *
 * Copyright (C) 2008 Secret Lab Technologies Ltd.
 *
 * Description:
 * This code implements support for the Freescape Media5200 platform
 * (built around the MPC5200 SoC).
 *
 * Notable characteristic of the Media5200 is the presence of an FPGA
 * that has all external IRQ lines routed through it.  This file implements
 * a cascaded interrupt controller driver which attaches itself to the
 * Virtual IRQ subsystem after the primary mpc5200 interrupt controller
 * is initialized.
 */

#undef DEBUG

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/mpc52xx.h>

static const struct of_device_id mpc5200_gpio_ids[] __initconst = {
	{ .compatible = "fsl,mpc5200-gpio", },
	{ .compatible = "mpc5200-gpio", },
	{}
};

/* FPGA register set */
#define MEDIA5200_IRQ_ENABLE (0x40c)
#define MEDIA5200_IRQ_STATUS (0x410)
#define MEDIA5200_NUM_IRQS   (6)
#define MEDIA5200_IRQ_SHIFT  (32 - MEDIA5200_NUM_IRQS)

struct media5200_irq {
	void __iomem *regs;
	spinlock_t lock;
	struct irq_domain *irqhost;
};
struct media5200_irq media5200_irq;

static void media5200_irq_unmask(struct irq_data *d)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&media5200_irq.lock, flags);
	val = in_be32(media5200_irq.regs + MEDIA5200_IRQ_ENABLE);
	val |= 1 << (MEDIA5200_IRQ_SHIFT + irqd_to_hwirq(d));
	out_be32(media5200_irq.regs + MEDIA5200_IRQ_ENABLE, val);
	spin_unlock_irqrestore(&media5200_irq.lock, flags);
}

static void media5200_irq_mask(struct irq_data *d)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&media5200_irq.lock, flags);
	val = in_be32(media5200_irq.regs + MEDIA5200_IRQ_ENABLE);
	val &= ~(1 << (MEDIA5200_IRQ_SHIFT + irqd_to_hwirq(d)));
	out_be32(media5200_irq.regs + MEDIA5200_IRQ_ENABLE, val);
	spin_unlock_irqrestore(&media5200_irq.lock, flags);
}

static struct irq_chip media5200_irq_chip = {
	.name = "Media5200 FPGA",
	.irq_unmask = media5200_irq_unmask,
	.irq_mask = media5200_irq_mask,
	.irq_mask_ack = media5200_irq_mask,
};

static void media5200_irq_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int val;
	u32 status, enable;

	/* Mask off the cascaded IRQ */
	raw_spin_lock(&desc->lock);
	chip->irq_mask(&desc->irq_data);
	raw_spin_unlock(&desc->lock);

	/* Ask the FPGA for IRQ status.  If 'val' is 0, then no irqs
	 * are pending.  'ffs()' is 1 based */
	status = in_be32(media5200_irq.regs + MEDIA5200_IRQ_ENABLE);
	enable = in_be32(media5200_irq.regs + MEDIA5200_IRQ_STATUS);
	val = ffs((status & enable) >> MEDIA5200_IRQ_SHIFT);
	if (val) {
		generic_handle_domain_irq(media5200_irq.irqhost, val - 1);
		/* pr_debug("%s: virq=%i s=%.8x e=%.8x hwirq=%i\n",
		 *          __func__, virq, status, enable, val - 1);
		 */
	}

	/* Processing done; can reenable the cascade now */
	raw_spin_lock(&desc->lock);
	chip->irq_ack(&desc->irq_data);
	if (!irqd_irq_disabled(&desc->irq_data))
		chip->irq_unmask(&desc->irq_data);
	raw_spin_unlock(&desc->lock);
}

static int media5200_irq_map(struct irq_domain *h, unsigned int virq,
			     irq_hw_number_t hw)
{
	pr_debug("%s: h=%p, virq=%i, hwirq=%i\n", __func__, h, virq, (int)hw);
	irq_set_chip_data(virq, &media5200_irq);
	irq_set_chip_and_handler(virq, &media5200_irq_chip, handle_level_irq);
	irq_set_status_flags(virq, IRQ_LEVEL);
	return 0;
}

static int media5200_irq_xlate(struct irq_domain *h, struct device_node *ct,
				 const u32 *intspec, unsigned int intsize,
				 irq_hw_number_t *out_hwirq,
				 unsigned int *out_flags)
{
	if (intsize != 2)
		return -1;

	pr_debug("%s: bank=%i, number=%i\n", __func__, intspec[0], intspec[1]);
	*out_hwirq = intspec[1];
	*out_flags = IRQ_TYPE_NONE;
	return 0;
}

static const struct irq_domain_ops media5200_irq_ops = {
	.map = media5200_irq_map,
	.xlate = media5200_irq_xlate,
};

/*
 * Setup Media5200 IRQ mapping
 */
static void __init media5200_init_irq(void)
{
	struct device_node *fpga_np;
	int cascade_virq;

	/* First setup the regular MPC5200 interrupt controller */
	mpc52xx_init_irq();

	/* Now find the FPGA IRQ */
	fpga_np = of_find_compatible_node(NULL, NULL, "fsl,media5200-fpga");
	if (!fpga_np)
		goto out;
	pr_debug("%s: found fpga node: %pOF\n", __func__, fpga_np);

	media5200_irq.regs = of_iomap(fpga_np, 0);
	if (!media5200_irq.regs)
		goto out;
	pr_debug("%s: mapped to %p\n", __func__, media5200_irq.regs);

	cascade_virq = irq_of_parse_and_map(fpga_np, 0);
	if (!cascade_virq)
		goto out;
	pr_debug("%s: cascaded on virq=%i\n", __func__, cascade_virq);

	/* Disable all FPGA IRQs */
	out_be32(media5200_irq.regs + MEDIA5200_IRQ_ENABLE, 0);

	spin_lock_init(&media5200_irq.lock);

	media5200_irq.irqhost = irq_domain_create_linear(of_fwnode_handle(fpga_np),
			MEDIA5200_NUM_IRQS, &media5200_irq_ops, &media5200_irq);
	if (!media5200_irq.irqhost)
		goto out;
	pr_debug("%s: allocated irqhost\n", __func__);

	of_node_put(fpga_np);

	irq_set_handler_data(cascade_virq, &media5200_irq);
	irq_set_chained_handler(cascade_virq, media5200_irq_cascade);

	return;

 out:
	pr_err("Could not find Media5200 FPGA; PCI interrupts will not work\n");
	of_node_put(fpga_np);
}

/*
 * Setup the architecture
 */
static void __init media5200_setup_arch(void)
{

	struct device_node *np;
	struct mpc52xx_gpio __iomem *gpio;
	u32 port_config;

	if (ppc_md.progress)
		ppc_md.progress("media5200_setup_arch()", 0);

	/* Map important registers from the internal memory map */
	mpc52xx_map_common_devices();

	/* Some mpc5200 & mpc5200b related configuration */
	mpc5200_setup_xlb_arbiter();

	np = of_find_matching_node(NULL, mpc5200_gpio_ids);
	gpio = of_iomap(np, 0);
	of_node_put(np);
	if (!gpio) {
		printk(KERN_ERR "%s() failed. expect abnormal behavior\n",
		       __func__);
		return;
	}

	/* Set port config */
	port_config = in_be32(&gpio->port_config);

	port_config &= ~0x03000000;	/* ATA CS is on csb_4/5		*/
	port_config |=  0x01000000;

	out_be32(&gpio->port_config, port_config);

	/* Unmap zone */
	iounmap(gpio);

}

define_machine(media5200_platform) {
	.name		= "media5200-platform",
	.compatible	= "fsl,media5200",
	.setup_arch	= media5200_setup_arch,
	.discover_phbs	= mpc52xx_setup_pci,
	.init		= mpc52xx_declare_of_platform_devices,
	.init_IRQ	= media5200_init_irq,
	.get_irq	= mpc52xx_get_irq,
	.restart	= mpc52xx_restart,
};
