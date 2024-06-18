// SPDX-License-Identifier: GPL-2.0
/*
 * Loongson LPC Interrupt Controller support
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#define pr_fmt(fmt) "lpc: " fmt

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/syscore_ops.h>

/* Registers */
#define LPC_INT_CTL		0x00
#define LPC_INT_ENA		0x04
#define LPC_INT_STS		0x08
#define LPC_INT_CLR		0x0c
#define LPC_INT_POL		0x10
#define LPC_COUNT		16

/* LPC_INT_CTL */
#define LPC_INT_CTL_EN		BIT(31)

struct pch_lpc {
	void __iomem		*base;
	struct irq_domain	*lpc_domain;
	raw_spinlock_t		lpc_lock;
	u32			saved_reg_ctl;
	u32			saved_reg_ena;
	u32			saved_reg_pol;
};

static struct pch_lpc *pch_lpc_priv;
struct fwnode_handle *pch_lpc_handle;

static void lpc_irq_ack(struct irq_data *d)
{
	unsigned long flags;
	struct pch_lpc *priv = d->domain->host_data;

	raw_spin_lock_irqsave(&priv->lpc_lock, flags);
	writel(0x1 << d->hwirq, priv->base + LPC_INT_CLR);
	raw_spin_unlock_irqrestore(&priv->lpc_lock, flags);
}

static void lpc_irq_mask(struct irq_data *d)
{
	unsigned long flags;
	struct pch_lpc *priv = d->domain->host_data;

	raw_spin_lock_irqsave(&priv->lpc_lock, flags);
	writel(readl(priv->base + LPC_INT_ENA) & (~(0x1 << (d->hwirq))),
			priv->base + LPC_INT_ENA);
	raw_spin_unlock_irqrestore(&priv->lpc_lock, flags);
}

static void lpc_irq_unmask(struct irq_data *d)
{
	unsigned long flags;
	struct pch_lpc *priv = d->domain->host_data;

	raw_spin_lock_irqsave(&priv->lpc_lock, flags);
	writel(readl(priv->base + LPC_INT_ENA) | (0x1 << (d->hwirq)),
			priv->base + LPC_INT_ENA);
	raw_spin_unlock_irqrestore(&priv->lpc_lock, flags);
}

static int lpc_irq_set_type(struct irq_data *d, unsigned int type)
{
	u32 val;
	u32 mask = 0x1 << (d->hwirq);
	struct pch_lpc *priv = d->domain->host_data;

	if (!(type & IRQ_TYPE_LEVEL_MASK))
		return 0;

	val = readl(priv->base + LPC_INT_POL);

	if (type == IRQ_TYPE_LEVEL_HIGH)
		val |= mask;
	else
		val &= ~mask;

	writel(val, priv->base + LPC_INT_POL);

	return 0;
}

static const struct irq_chip pch_lpc_irq_chip = {
	.name			= "PCH LPC",
	.irq_mask		= lpc_irq_mask,
	.irq_unmask		= lpc_irq_unmask,
	.irq_ack		= lpc_irq_ack,
	.irq_set_type		= lpc_irq_set_type,
	.flags			= IRQCHIP_SKIP_SET_WAKE,
};

static void lpc_irq_dispatch(struct irq_desc *desc)
{
	u32 pending, bit;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct pch_lpc *priv = irq_desc_get_handler_data(desc);

	chained_irq_enter(chip, desc);

	pending = readl(priv->base + LPC_INT_ENA);
	pending &= readl(priv->base + LPC_INT_STS);
	if (!pending)
		spurious_interrupt();

	while (pending) {
		bit = __ffs(pending);

		generic_handle_domain_irq(priv->lpc_domain, bit);
		pending &= ~BIT(bit);
	}
	chained_irq_exit(chip, desc);
}

static int pch_lpc_map(struct irq_domain *d, unsigned int irq,
			irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &pch_lpc_irq_chip, handle_level_irq);
	return 0;
}

static const struct irq_domain_ops pch_lpc_domain_ops = {
	.map 		= pch_lpc_map,
	.translate	= irq_domain_translate_twocell,
};

static void pch_lpc_reset(struct pch_lpc *priv)
{
	/* Enable the LPC interrupt, bit31: en  bit30: edge */
	writel(LPC_INT_CTL_EN, priv->base + LPC_INT_CTL);
	writel(0, priv->base + LPC_INT_ENA);
	/* Clear all 18-bit interrpt bit */
	writel(GENMASK(17, 0), priv->base + LPC_INT_CLR);
}

static int pch_lpc_disabled(struct pch_lpc *priv)
{
	return (readl(priv->base + LPC_INT_ENA) == 0xffffffff) &&
			(readl(priv->base + LPC_INT_STS) == 0xffffffff);
}

static int pch_lpc_suspend(void)
{
	pch_lpc_priv->saved_reg_ctl = readl(pch_lpc_priv->base + LPC_INT_CTL);
	pch_lpc_priv->saved_reg_ena = readl(pch_lpc_priv->base + LPC_INT_ENA);
	pch_lpc_priv->saved_reg_pol = readl(pch_lpc_priv->base + LPC_INT_POL);
	return 0;
}

static void pch_lpc_resume(void)
{
	writel(pch_lpc_priv->saved_reg_ctl, pch_lpc_priv->base + LPC_INT_CTL);
	writel(pch_lpc_priv->saved_reg_ena, pch_lpc_priv->base + LPC_INT_ENA);
	writel(pch_lpc_priv->saved_reg_pol, pch_lpc_priv->base + LPC_INT_POL);
}

static struct syscore_ops pch_lpc_syscore_ops = {
	.suspend = pch_lpc_suspend,
	.resume = pch_lpc_resume,
};

int __init pch_lpc_acpi_init(struct irq_domain *parent,
					struct acpi_madt_lpc_pic *acpi_pchlpc)
{
	int parent_irq;
	struct pch_lpc *priv;
	struct irq_fwspec fwspec;
	struct fwnode_handle *irq_handle;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	raw_spin_lock_init(&priv->lpc_lock);

	priv->base = ioremap(acpi_pchlpc->address, acpi_pchlpc->size);
	if (!priv->base)
		goto free_priv;

	if (pch_lpc_disabled(priv)) {
		pr_err("Failed to get LPC status\n");
		goto iounmap_base;
	}

	irq_handle = irq_domain_alloc_named_fwnode("lpcintc");
	if (!irq_handle) {
		pr_err("Unable to allocate domain handle\n");
		goto iounmap_base;
	}

	priv->lpc_domain = irq_domain_create_linear(irq_handle, LPC_COUNT,
					&pch_lpc_domain_ops, priv);
	if (!priv->lpc_domain) {
		pr_err("Failed to create IRQ domain\n");
		goto free_irq_handle;
	}
	pch_lpc_reset(priv);

	fwspec.fwnode = parent->fwnode;
	fwspec.param[0] = acpi_pchlpc->cascade + GSI_MIN_PCH_IRQ;
	fwspec.param[1] = IRQ_TYPE_LEVEL_HIGH;
	fwspec.param_count = 2;
	parent_irq = irq_create_fwspec_mapping(&fwspec);
	irq_set_chained_handler_and_data(parent_irq, lpc_irq_dispatch, priv);

	pch_lpc_priv = priv;
	pch_lpc_handle = irq_handle;
	register_syscore_ops(&pch_lpc_syscore_ops);

	return 0;

free_irq_handle:
	irq_domain_free_fwnode(irq_handle);
iounmap_base:
	iounmap(priv->base);
free_priv:
	kfree(priv);

	return -ENOMEM;
}
