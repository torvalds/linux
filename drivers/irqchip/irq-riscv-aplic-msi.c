// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqchip/riscv-aplic.h>
#include <linux/irqchip/riscv-imsic.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/smp.h>

#include "irq-riscv-aplic-main.h"

static void aplic_msi_irq_mask(struct irq_data *d)
{
	aplic_irq_mask(d);
	irq_chip_mask_parent(d);
}

static void aplic_msi_irq_unmask(struct irq_data *d)
{
	irq_chip_unmask_parent(d);
	aplic_irq_unmask(d);
}

static void aplic_msi_irq_eoi(struct irq_data *d)
{
	struct aplic_priv *priv = irq_data_get_irq_chip_data(d);

	/*
	 * EOI handling is required only for level-triggered interrupts
	 * when APLIC is in MSI mode.
	 */

	switch (irqd_get_trigger_type(d)) {
	case IRQ_TYPE_LEVEL_LOW:
	case IRQ_TYPE_LEVEL_HIGH:
		/*
		 * The section "4.9.2 Special consideration for level-sensitive interrupt
		 * sources" of the RISC-V AIA specification says:
		 *
		 * A second option is for the interrupt service routine to write the
		 * APLIC’s source identity number for the interrupt to the domain’s
		 * setipnum register just before exiting. This will cause the interrupt’s
		 * pending bit to be set to one again if the source is still asserting
		 * an interrupt, but not if the source is not asserting an interrupt.
		 */
		writel(d->hwirq, priv->regs + APLIC_SETIPNUM_LE);
		break;
	}
}

static void aplic_msi_write_msg(struct irq_data *d, struct msi_msg *msg)
{
	unsigned int group_index, hart_index, guest_index, val;
	struct aplic_priv *priv = irq_data_get_irq_chip_data(d);
	struct aplic_msicfg *mc = &priv->msicfg;
	phys_addr_t tppn, tbppn, msg_addr;
	void __iomem *target;

	/* For zeroed MSI, simply write zero into the target register */
	if (!msg->address_hi && !msg->address_lo && !msg->data) {
		target = priv->regs + APLIC_TARGET_BASE;
		target += (d->hwirq - 1) * sizeof(u32);
		writel(0, target);
		return;
	}

	/* Sanity check on message data */
	WARN_ON(msg->data > APLIC_TARGET_EIID_MASK);

	/* Compute target MSI address */
	msg_addr = (((u64)msg->address_hi) << 32) | msg->address_lo;
	tppn = msg_addr >> APLIC_xMSICFGADDR_PPN_SHIFT;

	/* Compute target HART Base PPN */
	tbppn = tppn;
	tbppn &= ~APLIC_xMSICFGADDR_PPN_HART(mc->lhxs);
	tbppn &= ~APLIC_xMSICFGADDR_PPN_LHX(mc->lhxw, mc->lhxs);
	tbppn &= ~APLIC_xMSICFGADDR_PPN_HHX(mc->hhxw, mc->hhxs);
	WARN_ON(tbppn != mc->base_ppn);

	/* Compute target group and hart indexes */
	group_index = (tppn >> APLIC_xMSICFGADDR_PPN_HHX_SHIFT(mc->hhxs)) &
		     APLIC_xMSICFGADDR_PPN_HHX_MASK(mc->hhxw);
	hart_index = (tppn >> APLIC_xMSICFGADDR_PPN_LHX_SHIFT(mc->lhxs)) &
		     APLIC_xMSICFGADDR_PPN_LHX_MASK(mc->lhxw);
	hart_index |= (group_index << mc->lhxw);
	WARN_ON(hart_index > APLIC_TARGET_HART_IDX_MASK);

	/* Compute target guest index */
	guest_index = tppn & APLIC_xMSICFGADDR_PPN_HART(mc->lhxs);
	WARN_ON(guest_index > APLIC_TARGET_GUEST_IDX_MASK);

	/* Update IRQ TARGET register */
	target = priv->regs + APLIC_TARGET_BASE;
	target += (d->hwirq - 1) * sizeof(u32);
	val = FIELD_PREP(APLIC_TARGET_HART_IDX, hart_index);
	val |= FIELD_PREP(APLIC_TARGET_GUEST_IDX, guest_index);
	val |= FIELD_PREP(APLIC_TARGET_EIID, msg->data);
	writel(val, target);
}

static void aplic_msi_set_desc(msi_alloc_info_t *arg, struct msi_desc *desc)
{
	arg->desc = desc;
	arg->hwirq = (u32)desc->data.icookie.value;
}

static int aplic_msi_translate(struct irq_domain *d, struct irq_fwspec *fwspec,
			       unsigned long *hwirq, unsigned int *type)
{
	struct msi_domain_info *info = d->host_data;
	struct aplic_priv *priv = info->data;

	return aplic_irqdomain_translate(fwspec, priv->gsi_base, hwirq, type);
}

static const struct msi_domain_template aplic_msi_template = {
	.chip = {
		.name			= "APLIC-MSI",
		.irq_mask		= aplic_msi_irq_mask,
		.irq_unmask		= aplic_msi_irq_unmask,
		.irq_set_type		= aplic_irq_set_type,
		.irq_eoi		= aplic_msi_irq_eoi,
#ifdef CONFIG_SMP
		.irq_set_affinity	= irq_chip_set_affinity_parent,
#endif
		.irq_write_msi_msg	= aplic_msi_write_msg,
		.flags			= IRQCHIP_SET_TYPE_MASKED |
					  IRQCHIP_SKIP_SET_WAKE |
					  IRQCHIP_MASK_ON_SUSPEND,
	},

	.ops = {
		.set_desc		= aplic_msi_set_desc,
		.msi_translate		= aplic_msi_translate,
	},

	.info = {
		.bus_token		= DOMAIN_BUS_WIRED_TO_MSI,
		.flags			= MSI_FLAG_USE_DEV_FWNODE,
		.handler		= handle_fasteoi_irq,
		.handler_name		= "fasteoi",
	},
};

int aplic_msi_setup(struct device *dev, void __iomem *regs)
{
	const struct imsic_global_config *imsic_global;
	struct aplic_priv *priv;
	struct aplic_msicfg *mc;
	phys_addr_t pa;
	int rc;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	rc = aplic_setup_priv(priv, dev, regs);
	if (rc) {
		dev_err(dev, "failed to create APLIC context\n");
		return rc;
	}
	mc = &priv->msicfg;

	/*
	 * The APLIC outgoing MSI config registers assume target MSI
	 * controller to be RISC-V AIA IMSIC controller.
	 */
	imsic_global = imsic_get_global_config();
	if (!imsic_global) {
		dev_err(dev, "IMSIC global config not found\n");
		return -ENODEV;
	}

	/* Find number of guest index bits (LHXS) */
	mc->lhxs = imsic_global->guest_index_bits;
	if (APLIC_xMSICFGADDRH_LHXS_MASK < mc->lhxs) {
		dev_err(dev, "IMSIC guest index bits big for APLIC LHXS\n");
		return -EINVAL;
	}

	/* Find number of HART index bits (LHXW) */
	mc->lhxw = imsic_global->hart_index_bits;
	if (APLIC_xMSICFGADDRH_LHXW_MASK < mc->lhxw) {
		dev_err(dev, "IMSIC hart index bits big for APLIC LHXW\n");
		return -EINVAL;
	}

	/* Find number of group index bits (HHXW) */
	mc->hhxw = imsic_global->group_index_bits;
	if (APLIC_xMSICFGADDRH_HHXW_MASK < mc->hhxw) {
		dev_err(dev, "IMSIC group index bits big for APLIC HHXW\n");
		return -EINVAL;
	}

	/* Find first bit position of group index (HHXS) */
	mc->hhxs = imsic_global->group_index_shift;
	if (mc->hhxs < (2 * APLIC_xMSICFGADDR_PPN_SHIFT)) {
		dev_err(dev, "IMSIC group index shift should be >= %d\n",
			(2 * APLIC_xMSICFGADDR_PPN_SHIFT));
		return -EINVAL;
	}
	mc->hhxs -= (2 * APLIC_xMSICFGADDR_PPN_SHIFT);
	if (APLIC_xMSICFGADDRH_HHXS_MASK < mc->hhxs) {
		dev_err(dev, "IMSIC group index shift big for APLIC HHXS\n");
		return -EINVAL;
	}

	/* Compute PPN base */
	mc->base_ppn = imsic_global->base_addr >> APLIC_xMSICFGADDR_PPN_SHIFT;
	mc->base_ppn &= ~APLIC_xMSICFGADDR_PPN_HART(mc->lhxs);
	mc->base_ppn &= ~APLIC_xMSICFGADDR_PPN_LHX(mc->lhxw, mc->lhxs);
	mc->base_ppn &= ~APLIC_xMSICFGADDR_PPN_HHX(mc->hhxw, mc->hhxs);

	/* Setup global config and interrupt delivery */
	aplic_init_hw_global(priv, true);

	/* Set the APLIC device MSI domain if not available */
	if (!dev_get_msi_domain(dev)) {
		/*
		 * The device MSI domain for OF devices is only set at the
		 * time of populating/creating OF device. If the device MSI
		 * domain is discovered later after the OF device is created
		 * then we need to set it explicitly before using any platform
		 * MSI functions.
		 *
		 * In case of APLIC device, the parent MSI domain is always
		 * IMSIC and the IMSIC MSI domains are created later through
		 * the platform driver probing so we set it explicitly here.
		 */
		if (is_of_node(dev->fwnode))
			of_msi_configure(dev, to_of_node(dev->fwnode));
	}

	if (!msi_create_device_irq_domain(dev, MSI_DEFAULT_DOMAIN, &aplic_msi_template,
					  priv->nr_irqs + 1, priv, priv)) {
		dev_err(dev, "failed to create MSI irq domain\n");
		return -ENOMEM;
	}

	/* Advertise the interrupt controller */
	pa = priv->msicfg.base_ppn << APLIC_xMSICFGADDR_PPN_SHIFT;
	dev_info(dev, "%d interrupts forwarded to MSI base %pa\n", priv->nr_irqs, &pa);

	return 0;
}
