// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-mmp/irq.c
 *
 *  Generic IRQ handling, GPIO IRQ demultiplexing, etc.
 *  Copyright (C) 2008 - 2012 Marvell Technology Group Ltd.
 *
 *  Author:	Bin Yang <bin.yang@marvell.com>
 *              Haojian Zhuang <haojian.zhuang@gmail.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/exception.h>
#include <asm/hardirq.h>

#define MAX_ICU_NR		16

#define PJ1_INT_SEL		0x10c
#define PJ4_INT_SEL		0x104

/* bit fields in PJ1_INT_SEL and PJ4_INT_SEL */
#define SEL_INT_PENDING		(1 << 6)
#define SEL_INT_NUM_MASK	0x3f

#define MMP2_ICU_INT_ROUTE_PJ4_IRQ	(1 << 5)
#define MMP2_ICU_INT_ROUTE_PJ4_FIQ	(1 << 6)

struct icu_chip_data {
	int			nr_irqs;
	unsigned int		virq_base;
	unsigned int		cascade_irq;
	void __iomem		*reg_status;
	void __iomem		*reg_mask;
	unsigned int		conf_enable;
	unsigned int		conf_disable;
	unsigned int		conf_mask;
	unsigned int		conf2_mask;
	unsigned int		clr_mfp_irq_base;
	unsigned int		clr_mfp_hwirq;
	struct irq_domain	*domain;
};

struct mmp_intc_conf {
	unsigned int	conf_enable;
	unsigned int	conf_disable;
	unsigned int	conf_mask;
	unsigned int	conf2_mask;
};

static void __iomem *mmp_icu_base;
static void __iomem *mmp_icu2_base;
static struct icu_chip_data icu_data[MAX_ICU_NR];
static int max_icu_nr;

extern void mmp2_clear_pmic_int(void);

static void icu_mask_ack_irq(struct irq_data *d)
{
	struct irq_domain *domain = d->domain;
	struct icu_chip_data *data = (struct icu_chip_data *)domain->host_data;
	int hwirq;
	u32 r;

	hwirq = d->irq - data->virq_base;
	if (data == &icu_data[0]) {
		r = readl_relaxed(mmp_icu_base + (hwirq << 2));
		r &= ~data->conf_mask;
		r |= data->conf_disable;
		writel_relaxed(r, mmp_icu_base + (hwirq << 2));
	} else {
#ifdef CONFIG_CPU_MMP2
		if ((data->virq_base == data->clr_mfp_irq_base)
			&& (hwirq == data->clr_mfp_hwirq))
			mmp2_clear_pmic_int();
#endif
		r = readl_relaxed(data->reg_mask) | (1 << hwirq);
		writel_relaxed(r, data->reg_mask);
	}
}

static void icu_mask_irq(struct irq_data *d)
{
	struct irq_domain *domain = d->domain;
	struct icu_chip_data *data = (struct icu_chip_data *)domain->host_data;
	int hwirq;
	u32 r;

	hwirq = d->irq - data->virq_base;
	if (data == &icu_data[0]) {
		r = readl_relaxed(mmp_icu_base + (hwirq << 2));
		r &= ~data->conf_mask;
		r |= data->conf_disable;
		writel_relaxed(r, mmp_icu_base + (hwirq << 2));

		if (data->conf2_mask) {
			/*
			 * ICU1 (above) only controls PJ4 MP1; if using SMP,
			 * we need to also mask the MP2 and MM cores via ICU2.
			 */
			r = readl_relaxed(mmp_icu2_base + (hwirq << 2));
			r &= ~data->conf2_mask;
			writel_relaxed(r, mmp_icu2_base + (hwirq << 2));
		}
	} else {
		r = readl_relaxed(data->reg_mask) | (1 << hwirq);
		writel_relaxed(r, data->reg_mask);
	}
}

static void icu_unmask_irq(struct irq_data *d)
{
	struct irq_domain *domain = d->domain;
	struct icu_chip_data *data = (struct icu_chip_data *)domain->host_data;
	int hwirq;
	u32 r;

	hwirq = d->irq - data->virq_base;
	if (data == &icu_data[0]) {
		r = readl_relaxed(mmp_icu_base + (hwirq << 2));
		r &= ~data->conf_mask;
		r |= data->conf_enable;
		writel_relaxed(r, mmp_icu_base + (hwirq << 2));
	} else {
		r = readl_relaxed(data->reg_mask) & ~(1 << hwirq);
		writel_relaxed(r, data->reg_mask);
	}
}

struct irq_chip icu_irq_chip = {
	.name		= "icu_irq",
	.irq_mask	= icu_mask_irq,
	.irq_mask_ack	= icu_mask_ack_irq,
	.irq_unmask	= icu_unmask_irq,
};

static void icu_mux_irq_demux(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_domain *domain;
	struct icu_chip_data *data;
	int i;
	unsigned long mask, status, n;

	chained_irq_enter(chip, desc);

	for (i = 1; i < max_icu_nr; i++) {
		if (irq == icu_data[i].cascade_irq) {
			domain = icu_data[i].domain;
			data = (struct icu_chip_data *)domain->host_data;
			break;
		}
	}
	if (i >= max_icu_nr) {
		pr_err("Spurious irq %d in MMP INTC\n", irq);
		goto out;
	}

	mask = readl_relaxed(data->reg_mask);
	while (1) {
		status = readl_relaxed(data->reg_status) & ~mask;
		if (status == 0)
			break;
		for_each_set_bit(n, &status, BITS_PER_LONG) {
			generic_handle_irq(icu_data[i].virq_base + n);
		}
	}

out:
	chained_irq_exit(chip, desc);
}

static int mmp_irq_domain_map(struct irq_domain *d, unsigned int irq,
			      irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &icu_irq_chip, handle_level_irq);
	return 0;
}

static int mmp_irq_domain_xlate(struct irq_domain *d, struct device_node *node,
				const u32 *intspec, unsigned int intsize,
				unsigned long *out_hwirq,
				unsigned int *out_type)
{
	*out_hwirq = intspec[0];
	return 0;
}

static const struct irq_domain_ops mmp_irq_domain_ops = {
	.map		= mmp_irq_domain_map,
	.xlate		= mmp_irq_domain_xlate,
};

static const struct mmp_intc_conf mmp_conf = {
	.conf_enable	= 0x51,
	.conf_disable	= 0x0,
	.conf_mask	= 0x7f,
};

static const struct mmp_intc_conf mmp2_conf = {
	.conf_enable	= 0x20,
	.conf_disable	= 0x0,
	.conf_mask	= MMP2_ICU_INT_ROUTE_PJ4_IRQ |
			  MMP2_ICU_INT_ROUTE_PJ4_FIQ,
};

static struct mmp_intc_conf mmp3_conf = {
	.conf_enable	= 0x20,
	.conf_disable	= 0x0,
	.conf_mask	= MMP2_ICU_INT_ROUTE_PJ4_IRQ |
			  MMP2_ICU_INT_ROUTE_PJ4_FIQ,
	.conf2_mask	= 0xf0,
};

static void __exception_irq_entry mmp_handle_irq(struct pt_regs *regs)
{
	int hwirq;

	hwirq = readl_relaxed(mmp_icu_base + PJ1_INT_SEL);
	if (!(hwirq & SEL_INT_PENDING))
		return;
	hwirq &= SEL_INT_NUM_MASK;
	generic_handle_domain_irq(icu_data[0].domain, hwirq);
}

static void __exception_irq_entry mmp2_handle_irq(struct pt_regs *regs)
{
	int hwirq;

	hwirq = readl_relaxed(mmp_icu_base + PJ4_INT_SEL);
	if (!(hwirq & SEL_INT_PENDING))
		return;
	hwirq &= SEL_INT_NUM_MASK;
	generic_handle_domain_irq(icu_data[0].domain, hwirq);
}

static int __init mmp_init_bases(struct device_node *node)
{
	int ret, nr_irqs, irq, i = 0;

	ret = of_property_read_u32(node, "mrvl,intc-nr-irqs", &nr_irqs);
	if (ret) {
		pr_err("Not found mrvl,intc-nr-irqs property\n");
		return ret;
	}

	mmp_icu_base = of_iomap(node, 0);
	if (!mmp_icu_base) {
		pr_err("Failed to get interrupt controller register\n");
		return -ENOMEM;
	}

	icu_data[0].virq_base = 0;
	icu_data[0].domain = irq_domain_create_linear(of_fwnode_handle(node), nr_irqs,
						      &mmp_irq_domain_ops,
						      &icu_data[0]);
	for (irq = 0; irq < nr_irqs; irq++) {
		ret = irq_create_mapping(icu_data[0].domain, irq);
		if (!ret) {
			pr_err("Failed to mapping hwirq\n");
			goto err;
		}
		if (!irq)
			icu_data[0].virq_base = ret;
	}
	icu_data[0].nr_irqs = nr_irqs;
	return 0;
err:
	if (icu_data[0].virq_base) {
		for (i = 0; i < irq; i++)
			irq_dispose_mapping(icu_data[0].virq_base + i);
	}
	irq_domain_remove(icu_data[0].domain);
	iounmap(mmp_icu_base);
	return -EINVAL;
}

static int __init mmp_of_init(struct device_node *node,
			      struct device_node *parent)
{
	int ret;

	ret = mmp_init_bases(node);
	if (ret < 0)
		return ret;

	icu_data[0].conf_enable = mmp_conf.conf_enable;
	icu_data[0].conf_disable = mmp_conf.conf_disable;
	icu_data[0].conf_mask = mmp_conf.conf_mask;
	set_handle_irq(mmp_handle_irq);
	max_icu_nr = 1;
	return 0;
}
IRQCHIP_DECLARE(mmp_intc, "mrvl,mmp-intc", mmp_of_init);

static int __init mmp2_of_init(struct device_node *node,
			       struct device_node *parent)
{
	int ret;

	ret = mmp_init_bases(node);
	if (ret < 0)
		return ret;

	icu_data[0].conf_enable = mmp2_conf.conf_enable;
	icu_data[0].conf_disable = mmp2_conf.conf_disable;
	icu_data[0].conf_mask = mmp2_conf.conf_mask;
	set_handle_irq(mmp2_handle_irq);
	max_icu_nr = 1;
	return 0;
}
IRQCHIP_DECLARE(mmp2_intc, "mrvl,mmp2-intc", mmp2_of_init);

static int __init mmp3_of_init(struct device_node *node,
			       struct device_node *parent)
{
	int ret;

	mmp_icu2_base = of_iomap(node, 1);
	if (!mmp_icu2_base) {
		pr_err("Failed to get interrupt controller register #2\n");
		return -ENODEV;
	}

	ret = mmp_init_bases(node);
	if (ret < 0) {
		iounmap(mmp_icu2_base);
		return ret;
	}

	icu_data[0].conf_enable = mmp3_conf.conf_enable;
	icu_data[0].conf_disable = mmp3_conf.conf_disable;
	icu_data[0].conf_mask = mmp3_conf.conf_mask;
	icu_data[0].conf2_mask = mmp3_conf.conf2_mask;

	if (!parent) {
		/* This is the main interrupt controller. */
		set_handle_irq(mmp2_handle_irq);
	}

	max_icu_nr = 1;
	return 0;
}
IRQCHIP_DECLARE(mmp3_intc, "marvell,mmp3-intc", mmp3_of_init);

static int __init mmp2_mux_of_init(struct device_node *node,
				   struct device_node *parent)
{
	int i, ret, irq, j = 0;
	u32 nr_irqs, mfp_irq;
	u32 reg[4];

	if (!parent)
		return -ENODEV;

	i = max_icu_nr;
	ret = of_property_read_u32(node, "mrvl,intc-nr-irqs",
				   &nr_irqs);
	if (ret) {
		pr_err("Not found mrvl,intc-nr-irqs property\n");
		return -EINVAL;
	}

	/*
	 * For historical reasons, the "regs" property of the
	 * mrvl,mmp2-mux-intc is not a regular "regs" property containing
	 * addresses on the parent bus, but offsets from the intc's base.
	 * That is why we can't use of_address_to_resource() here.
	 */
	ret = of_property_read_variable_u32_array(node, "reg", reg,
						  ARRAY_SIZE(reg),
						  ARRAY_SIZE(reg));
	if (ret < 0) {
		pr_err("Not found reg property\n");
		return -EINVAL;
	}
	icu_data[i].reg_status = mmp_icu_base + reg[0];
	icu_data[i].reg_mask = mmp_icu_base + reg[2];
	icu_data[i].cascade_irq = irq_of_parse_and_map(node, 0);
	if (!icu_data[i].cascade_irq)
		return -EINVAL;

	icu_data[i].virq_base = 0;
	icu_data[i].domain = irq_domain_create_linear(of_fwnode_handle(node), nr_irqs,
						      &mmp_irq_domain_ops,
						      &icu_data[i]);
	for (irq = 0; irq < nr_irqs; irq++) {
		ret = irq_create_mapping(icu_data[i].domain, irq);
		if (!ret) {
			pr_err("Failed to mapping hwirq\n");
			goto err;
		}
		if (!irq)
			icu_data[i].virq_base = ret;
	}
	icu_data[i].nr_irqs = nr_irqs;
	if (!of_property_read_u32(node, "mrvl,clr-mfp-irq",
				  &mfp_irq)) {
		icu_data[i].clr_mfp_irq_base = icu_data[i].virq_base;
		icu_data[i].clr_mfp_hwirq = mfp_irq;
	}
	irq_set_chained_handler(icu_data[i].cascade_irq,
				icu_mux_irq_demux);
	max_icu_nr++;
	return 0;
err:
	if (icu_data[i].virq_base) {
		for (j = 0; j < irq; j++)
			irq_dispose_mapping(icu_data[i].virq_base + j);
	}
	irq_domain_remove(icu_data[i].domain);
	return -EINVAL;
}
IRQCHIP_DECLARE(mmp2_mux_intc, "mrvl,mmp2-mux-intc", mmp2_mux_of_init);
