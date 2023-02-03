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
#include <linux/syscore_ops.h>

/* Registers */
#define HTVEC_EN_OFF		0x20
#define HTVEC_MAX_PARENT_IRQ	8
#define VEC_COUNT_PER_REG	32
#define VEC_REG_IDX(irq_id)	((irq_id) / VEC_COUNT_PER_REG)
#define VEC_REG_BIT(irq_id)	((irq_id) % VEC_COUNT_PER_REG)

struct htvec {
	int			num_parents;
	void __iomem		*base;
	struct irq_domain	*htvec_domain;
	raw_spinlock_t		htvec_lock;
	u32			saved_vec_en[HTVEC_MAX_PARENT_IRQ];
};

static struct htvec *htvec_priv;

static void htvec_irq_dispatch(struct irq_desc *desc)
{
	int i;
	u32 pending;
	bool handled = false;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct htvec *priv = irq_desc_get_handler_data(desc);

	chained_irq_enter(chip, desc);

	for (i = 0; i < priv->num_parents; i++) {
		pending = readl(priv->base + 4 * i);
		while (pending) {
			int bit = __ffs(pending);

			generic_handle_domain_irq(priv->htvec_domain,
						  bit + VEC_COUNT_PER_REG * i);
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
	for (idx = 0; idx < priv->num_parents; idx++) {
		writel_relaxed(0x0, priv->base + HTVEC_EN_OFF + 4 * idx);
		writel_relaxed(0xFFFFFFFF, priv->base + 4 * idx);
	}
}

static int htvec_suspend(void)
{
	int i;

	for (i = 0; i < htvec_priv->num_parents; i++)
		htvec_priv->saved_vec_en[i] = readl(htvec_priv->base + HTVEC_EN_OFF + 4 * i);

	return 0;
}

static void htvec_resume(void)
{
	int i;

	for (i = 0; i < htvec_priv->num_parents; i++)
		writel(htvec_priv->saved_vec_en[i], htvec_priv->base + HTVEC_EN_OFF + 4 * i);
}

static struct syscore_ops htvec_syscore_ops = {
	.suspend = htvec_suspend,
	.resume = htvec_resume,
};

static int htvec_init(phys_addr_t addr, unsigned long size,
		int num_parents, int parent_irq[], struct fwnode_handle *domain_handle)
{
	int i;
	struct htvec *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->num_parents = num_parents;
	priv->base = ioremap(addr, size);
	raw_spin_lock_init(&priv->htvec_lock);

	/* Setup IRQ domain */
	priv->htvec_domain = irq_domain_create_linear(domain_handle,
					(VEC_COUNT_PER_REG * priv->num_parents),
					&htvec_domain_ops, priv);
	if (!priv->htvec_domain) {
		pr_err("loongson-htvec: cannot add IRQ domain\n");
		goto iounmap_base;
	}

	htvec_reset(priv);

	for (i = 0; i < priv->num_parents; i++) {
		irq_set_chained_handler_and_data(parent_irq[i],
						 htvec_irq_dispatch, priv);
	}

	htvec_priv = priv;

	register_syscore_ops(&htvec_syscore_ops);

	return 0;

iounmap_base:
	iounmap(priv->base);
	kfree(priv);

	return -EINVAL;
}

#ifdef CONFIG_OF

static int htvec_of_init(struct device_node *node,
				struct device_node *parent)
{
	int i, err;
	int parent_irq[8];
	int num_parents = 0;
	struct resource res;

	if (of_address_to_resource(node, 0, &res))
		return -EINVAL;

	/* Interrupt may come from any of the 8 interrupt lines */
	for (i = 0; i < HTVEC_MAX_PARENT_IRQ; i++) {
		parent_irq[i] = irq_of_parse_and_map(node, i);
		if (parent_irq[i] <= 0)
			break;

		num_parents++;
	}

	err = htvec_init(res.start, resource_size(&res),
			num_parents, parent_irq, of_node_to_fwnode(node));
	if (err < 0)
		return err;

	return 0;
}

IRQCHIP_DECLARE(htvec, "loongson,htvec-1.0", htvec_of_init);

#endif

#ifdef CONFIG_ACPI
static int __init pch_pic_parse_madt(union acpi_subtable_headers *header,
					const unsigned long end)
{
	struct acpi_madt_bio_pic *pchpic_entry = (struct acpi_madt_bio_pic *)header;

	return pch_pic_acpi_init(htvec_priv->htvec_domain, pchpic_entry);
}

static int __init pch_msi_parse_madt(union acpi_subtable_headers *header,
					const unsigned long end)
{
	struct acpi_madt_msi_pic *pchmsi_entry = (struct acpi_madt_msi_pic *)header;

	return pch_msi_acpi_init(htvec_priv->htvec_domain, pchmsi_entry);
}

static int __init acpi_cascade_irqdomain_init(void)
{
	int r;

	r = acpi_table_parse_madt(ACPI_MADT_TYPE_BIO_PIC, pch_pic_parse_madt, 0);
	if (r < 0)
		return r;

	r = acpi_table_parse_madt(ACPI_MADT_TYPE_MSI_PIC, pch_msi_parse_madt, 0);
	if (r < 0)
		return r;

	return 0;
}

int __init htvec_acpi_init(struct irq_domain *parent,
				   struct acpi_madt_ht_pic *acpi_htvec)
{
	int i, ret;
	int num_parents, parent_irq[8];
	struct fwnode_handle *domain_handle;

	if (!acpi_htvec)
		return -EINVAL;

	num_parents = HTVEC_MAX_PARENT_IRQ;

	domain_handle = irq_domain_alloc_fwnode(&acpi_htvec->address);
	if (!domain_handle) {
		pr_err("Unable to allocate domain handle\n");
		return -ENOMEM;
	}

	/* Interrupt may come from any of the 8 interrupt lines */
	for (i = 0; i < HTVEC_MAX_PARENT_IRQ; i++)
		parent_irq[i] = irq_create_mapping(parent, acpi_htvec->cascade[i]);

	ret = htvec_init(acpi_htvec->address, acpi_htvec->size,
			num_parents, parent_irq, domain_handle);

	if (ret == 0)
		ret = acpi_cascade_irqdomain_init();
	else
		irq_domain_free_fwnode(domain_handle);

	return ret;
}

#endif
