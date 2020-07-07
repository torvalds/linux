// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2020, Jiaxun Yang <jiaxun.yang@flygoat.com>
 *  Loongson HyperTransport Interrupt Vector support
 */

#define pr_fmt(fmt) "htvec: " fmt

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

/* Registers */
#define HTVEC_EN_OFF		0x20
#define HTVEC_MAX_PARENT_IRQ	4

#define VEC_COUNT_PER_REG	32
#define VEC_REG_COUNT		4
#define VEC_COUNT		(VEC_COUNT_PER_REG * VEC_REG_COUNT)
#define VEC_REG_IDX(irq_id)	((irq_id) / VEC_COUNT_PER_REG)
#define VEC_REG_BIT(irq_id)	((irq_id) % VEC_COUNT_PER_REG)

struct htvec {
	void __iomem		*base;
	struct irq_domain	*htvec_domain;
	raw_spinlock_t		htvec_lock;
};

static void htvec_irq_dispatch(struct irq_desc *desc)
{
	int i;
	u32 pending;
	bool handled = false;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct htvec *priv = irq_desc_get_handler_data(desc);

	chained_irq_enter(chip, desc);

	for (i = 0; i < VEC_REG_COUNT; i++) {
		pending = readl(priv->base + 4 * i);
		while (pending) {
			int bit = __ffs(pending);

			generic_handle_irq(irq_linear_revmap(priv->htvec_domain, bit +
							     VEC_COUNT_PER_REG * i));
			pending &= ~BIT(bit);
			handled = true;
		}
	}

	if (!handled)
		spurious_interrupt();

	chained_irq_exit(chip, desc);
}

static void htvec_ack_irq(struct irq_data *d)
{
	struct htvec *priv = irq_data_get_irq_chip_data(d);

	writel(BIT(VEC_REG_BIT(d->hwirq)),
	       priv->base + VEC_REG_IDX(d->hwirq) * 4);
}

static void htvec_mask_irq(struct irq_data *d)
{
	u32 reg;
	void __iomem *addr;
	struct htvec *priv = irq_data_get_irq_chip_data(d);

	raw_spin_lock(&priv->htvec_lock);
	addr = priv->base + HTVEC_EN_OFF;
	addr += VEC_REG_IDX(d->hwirq) * 4;
	reg = readl(addr);
	reg &= ~BIT(VEC_REG_BIT(d->hwirq));
	writel(reg, addr);
	raw_spin_unlock(&priv->htvec_lock);
}

static void htvec_unmask_irq(struct irq_data *d)
{
	u32 reg;
	void __iomem *addr;
	struct htvec *priv = irq_data_get_irq_chip_data(d);

	raw_spin_lock(&priv->htvec_lock);
	addr = priv->base + HTVEC_EN_OFF;
	addr += VEC_REG_IDX(d->hwirq) * 4;
	reg = readl(addr);
	reg |= BIT(VEC_REG_BIT(d->hwirq));
	writel(reg, addr);
	raw_spin_unlock(&priv->htvec_lock);
}

static struct irq_chip htvec_irq_chip = {
	.name			= "LOONGSON_HTVEC",
	.irq_mask		= htvec_mask_irq,
	.irq_unmask		= htvec_unmask_irq,
	.irq_ack		= htvec_ack_irq,
};

static int htvec_domain_alloc(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs, void *arg)
{
	int ret;
	unsigned long hwirq;
	unsigned int type, i;
	struct htvec *priv = domain->host_data;

	ret = irq_domain_translate_onecell(domain, arg, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, hwirq + i, &htvec_irq_chip,
				    priv, handle_edge_irq, NULL, NULL);
	}

	return 0;
}

static void htvec_domain_free(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);

		irq_set_handler(virq + i, NULL);
		irq_domain_reset_irq_data(d);
	}
}

static const struct irq_domain_ops htvec_domain_ops = {
	.translate	= irq_domain_translate_onecell,
	.alloc		= htvec_domain_alloc,
	.free		= htvec_domain_free,
};

static void htvec_reset(struct htvec *priv)
{
	u32 idx;

	/* Clear IRQ cause registers, mask all interrupts */
	for (idx = 0; idx < VEC_REG_COUNT; idx++) {
		writel_relaxed(0x0, priv->base + HTVEC_EN_OFF + 4 * idx);
		writel_relaxed(0xFFFFFFFF, priv->base);
	}
}

static int htvec_of_init(struct device_node *node,
				struct device_node *parent)
{
	struct htvec *priv;
	int err, parent_irq[4], num_parents = 0, i;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	raw_spin_lock_init(&priv->htvec_lock);
	priv->base = of_iomap(node, 0);
	if (!priv->base) {
		err = -ENOMEM;
		goto free_priv;
	}

	/* Interrupt may come from any of the 4 interrupt line */
	for (i = 0; i < HTVEC_MAX_PARENT_IRQ; i++) {
		parent_irq[i] = irq_of_parse_and_map(node, i);
		if (parent_irq[i] <= 0)
			break;

		num_parents++;
	}

	if (!num_parents) {
		pr_err("Failed to get parent irqs\n");
		err = -ENODEV;
		goto iounmap_base;
	}

	priv->htvec_domain = irq_domain_create_linear(of_node_to_fwnode(node),
						      VEC_COUNT,
						      &htvec_domain_ops,
						      priv);
	if (!priv->htvec_domain) {
		pr_err("Failed to create IRQ domain\n");
		err = -ENOMEM;
		goto irq_dispose;
	}

	htvec_reset(priv);

	for (i = 0; i < num_parents; i++)
		irq_set_chained_handler_and_data(parent_irq[i],
						 htvec_irq_dispatch, priv);

	return 0;

irq_dispose:
	for (; i > 0; i--)
		irq_dispose_mapping(parent_irq[i - 1]);
iounmap_base:
	iounmap(priv->base);
free_priv:
	kfree(priv);

	return err;
}

IRQCHIP_DECLARE(htvec, "loongson,htvec-1.0", htvec_of_init);
