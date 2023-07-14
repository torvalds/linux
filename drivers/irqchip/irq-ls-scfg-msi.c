// SPDX-License-Identifier: GPL-2.0-only
/*
 * Freescale SCFG MSI(-X) support
 *
 * Copyright (C) 2016 Freescale Semiconductor.
 *
 * Author: Minghuan Lian <Minghuan.Lian@nxp.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/spinlock.h>

#define MSI_IRQS_PER_MSIR	32
#define MSI_MSIR_OFFSET		4

#define MSI_LS1043V1_1_IRQS_PER_MSIR	8
#define MSI_LS1043V1_1_MSIR_OFFSET	0x10

struct ls_scfg_msi_cfg {
	u32 ibs_shift; /* Shift of interrupt bit select */
	u32 msir_irqs; /* The irq number per MSIR */
	u32 msir_base; /* The base address of MSIR */
};

struct ls_scfg_msir {
	struct ls_scfg_msi *msi_data;
	unsigned int index;
	unsigned int gic_irq;
	unsigned int bit_start;
	unsigned int bit_end;
	unsigned int srs; /* Shared interrupt register select */
	void __iomem *reg;
};

struct ls_scfg_msi {
	spinlock_t		lock;
	struct platform_device	*pdev;
	struct irq_domain	*parent;
	struct irq_domain	*msi_domain;
	void __iomem		*regs;
	phys_addr_t		msiir_addr;
	struct ls_scfg_msi_cfg	*cfg;
	u32			msir_num;
	struct ls_scfg_msir	*msir;
	u32			irqs_num;
	unsigned long		*used;
};

static struct irq_chip ls_scfg_msi_irq_chip = {
	.name = "MSI",
	.irq_mask	= pci_msi_mask_irq,
	.irq_unmask	= pci_msi_unmask_irq,
};

static struct msi_domain_info ls_scfg_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS |
		   MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_PCI_MSIX),
	.chip	= &ls_scfg_msi_irq_chip,
};

static int msi_affinity_flag = 1;

static int __init early_parse_ls_scfg_msi(char *p)
{
	if (p && strncmp(p, "no-affinity", 11) == 0)
		msi_affinity_flag = 0;
	else
		msi_affinity_flag = 1;

	return 0;
}
early_param("lsmsi", early_parse_ls_scfg_msi);

static void ls_scfg_msi_compose_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct ls_scfg_msi *msi_data = irq_data_get_irq_chip_data(data);

	msg->address_hi = upper_32_bits(msi_data->msiir_addr);
	msg->address_lo = lower_32_bits(msi_data->msiir_addr);
	msg->data = data->hwirq;

	if (msi_affinity_flag) {
		const struct cpumask *mask;

		mask = irq_data_get_effective_affinity_mask(data);
		msg->data |= cpumask_first(mask);
	}

	iommu_dma_compose_msi_msg(irq_data_get_msi_desc(data), msg);
}

static int ls_scfg_msi_set_affinity(struct irq_data *irq_data,
				    const struct cpumask *mask, bool force)
{
	struct ls_scfg_msi *msi_data = irq_data_get_irq_chip_data(irq_data);
	u32 cpu;

	if (!msi_affinity_flag)
		return -EINVAL;

	if (!force)
		cpu = cpumask_any_and(mask, cpu_online_mask);
	else
		cpu = cpumask_first(mask);

	if (cpu >= msi_data->msir_num)
		return -EINVAL;

	if (msi_data->msir[cpu].gic_irq <= 0) {
		pr_warn("cannot bind the irq to cpu%d\n", cpu);
		return -EINVAL;
	}

	irq_data_update_effective_affinity(irq_data, cpumask_of(cpu));

	return IRQ_SET_MASK_OK;
}

static struct irq_chip ls_scfg_msi_parent_chip = {
	.name			= "SCFG",
	.irq_compose_msi_msg	= ls_scfg_msi_compose_msg,
	.irq_set_affinity	= ls_scfg_msi_set_affinity,
};

static int ls_scfg_msi_domain_irq_alloc(struct irq_domain *domain,
					unsigned int virq,
					unsigned int nr_irqs,
					void *args)
{
	msi_alloc_info_t *info = args;
	struct ls_scfg_msi *msi_data = domain->host_data;
	int pos, err = 0;

	WARN_ON(nr_irqs != 1);

	spin_lock(&msi_data->lock);
	pos = find_first_zero_bit(msi_data->used, msi_data->irqs_num);
	if (pos < msi_data->irqs_num)
		__set_bit(pos, msi_data->used);
	else
		err = -ENOSPC;
	spin_unlock(&msi_data->lock);

	if (err)
		return err;

	err = iommu_dma_prepare_msi(info->desc, msi_data->msiir_addr);
	if (err)
		return err;

	irq_domain_set_info(domain, virq, pos,
			    &ls_scfg_msi_parent_chip, msi_data,
			    handle_simple_irq, NULL, NULL);

	return 0;
}

static void ls_scfg_msi_domain_irq_free(struct irq_domain *domain,
				   unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct ls_scfg_msi *msi_data = irq_data_get_irq_chip_data(d);
	int pos;

	pos = d->hwirq;
	if (pos < 0 || pos >= msi_data->irqs_num) {
		pr_err("failed to teardown msi. Invalid hwirq %d\n", pos);
		return;
	}

	spin_lock(&msi_data->lock);
	__clear_bit(pos, msi_data->used);
	spin_unlock(&msi_data->lock);
}

static const struct irq_domain_ops ls_scfg_msi_domain_ops = {
	.alloc	= ls_scfg_msi_domain_irq_alloc,
	.free	= ls_scfg_msi_domain_irq_free,
};

static void ls_scfg_msi_irq_handler(struct irq_desc *desc)
{
	struct ls_scfg_msir *msir = irq_desc_get_handler_data(desc);
	struct ls_scfg_msi *msi_data = msir->msi_data;
	unsigned long val;
	int pos, size, hwirq;

	chained_irq_enter(irq_desc_get_chip(desc), desc);

	val = ioread32be(msir->reg);

	pos = msir->bit_start;
	size = msir->bit_end + 1;

	for_each_set_bit_from(pos, &val, size) {
		hwirq = ((msir->bit_end - pos) << msi_data->cfg->ibs_shift) |
			msir->srs;
		generic_handle_domain_irq(msi_data->parent, hwirq);
	}

	chained_irq_exit(irq_desc_get_chip(desc), desc);
}

static int ls_scfg_msi_domains_init(struct ls_scfg_msi *msi_data)
{
	/* Initialize MSI domain parent */
	msi_data->parent = irq_domain_add_linear(NULL,
						 msi_data->irqs_num,
						 &ls_scfg_msi_domain_ops,
						 msi_data);
	if (!msi_data->parent) {
		dev_err(&msi_data->pdev->dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	msi_data->msi_domain = pci_msi_create_irq_domain(
				of_node_to_fwnode(msi_data->pdev->dev.of_node),
				&ls_scfg_msi_domain_info,
				msi_data->parent);
	if (!msi_data->msi_domain) {
		dev_err(&msi_data->pdev->dev, "failed to create MSI domain\n");
		irq_domain_remove(msi_data->parent);
		return -ENOMEM;
	}

	return 0;
}

static int ls_scfg_msi_setup_hwirq(struct ls_scfg_msi *msi_data, int index)
{
	struct ls_scfg_msir *msir;
	int virq, i, hwirq;

	virq = platform_get_irq(msi_data->pdev, index);
	if (virq <= 0)
		return -ENODEV;

	msir = &msi_data->msir[index];
	msir->index = index;
	msir->msi_data = msi_data;
	msir->gic_irq = virq;
	msir->reg = msi_data->regs + msi_data->cfg->msir_base + 4 * index;

	if (msi_data->cfg->msir_irqs == MSI_LS1043V1_1_IRQS_PER_MSIR) {
		msir->bit_start = 32 - ((msir->index + 1) *
				  MSI_LS1043V1_1_IRQS_PER_MSIR);
		msir->bit_end = msir->bit_start +
				MSI_LS1043V1_1_IRQS_PER_MSIR - 1;
	} else {
		msir->bit_start = 0;
		msir->bit_end = msi_data->cfg->msir_irqs - 1;
	}

	irq_set_chained_handler_and_data(msir->gic_irq,
					 ls_scfg_msi_irq_handler,
					 msir);

	if (msi_affinity_flag) {
		/* Associate MSIR interrupt to the cpu */
		irq_set_affinity(msir->gic_irq, get_cpu_mask(index));
		msir->srs = 0; /* This value is determined by the CPU */
	} else
		msir->srs = index;

	/* Release the hwirqs corresponding to this MSIR */
	if (!msi_affinity_flag || msir->index == 0) {
		for (i = 0; i < msi_data->cfg->msir_irqs; i++) {
			hwirq = i << msi_data->cfg->ibs_shift | msir->index;
			bitmap_clear(msi_data->used, hwirq, 1);
		}
	}

	return 0;
}

static int ls_scfg_msi_teardown_hwirq(struct ls_scfg_msir *msir)
{
	struct ls_scfg_msi *msi_data = msir->msi_data;
	int i, hwirq;

	if (msir->gic_irq > 0)
		irq_set_chained_handler_and_data(msir->gic_irq, NULL, NULL);

	for (i = 0; i < msi_data->cfg->msir_irqs; i++) {
		hwirq = i << msi_data->cfg->ibs_shift | msir->index;
		bitmap_set(msi_data->used, hwirq, 1);
	}

	return 0;
}

static struct ls_scfg_msi_cfg ls1021_msi_cfg = {
	.ibs_shift = 3,
	.msir_irqs = MSI_IRQS_PER_MSIR,
	.msir_base = MSI_MSIR_OFFSET,
};

static struct ls_scfg_msi_cfg ls1046_msi_cfg = {
	.ibs_shift = 2,
	.msir_irqs = MSI_IRQS_PER_MSIR,
	.msir_base = MSI_MSIR_OFFSET,
};

static struct ls_scfg_msi_cfg ls1043_v1_1_msi_cfg = {
	.ibs_shift = 2,
	.msir_irqs = MSI_LS1043V1_1_IRQS_PER_MSIR,
	.msir_base = MSI_LS1043V1_1_MSIR_OFFSET,
};

static const struct of_device_id ls_scfg_msi_id[] = {
	/* The following two misspelled compatibles are obsolete */
	{ .compatible = "fsl,1s1021a-msi", .data = &ls1021_msi_cfg},
	{ .compatible = "fsl,1s1043a-msi", .data = &ls1021_msi_cfg},

	{ .compatible = "fsl,ls1012a-msi", .data = &ls1021_msi_cfg },
	{ .compatible = "fsl,ls1021a-msi", .data = &ls1021_msi_cfg },
	{ .compatible = "fsl,ls1043a-msi", .data = &ls1021_msi_cfg },
	{ .compatible = "fsl,ls1043a-v1.1-msi", .data = &ls1043_v1_1_msi_cfg },
	{ .compatible = "fsl,ls1046a-msi", .data = &ls1046_msi_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, ls_scfg_msi_id);

static int ls_scfg_msi_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct ls_scfg_msi *msi_data;
	struct resource *res;
	int i, ret;

	match = of_match_device(ls_scfg_msi_id, &pdev->dev);
	if (!match)
		return -ENODEV;

	msi_data = devm_kzalloc(&pdev->dev, sizeof(*msi_data), GFP_KERNEL);
	if (!msi_data)
		return -ENOMEM;

	msi_data->cfg = (struct ls_scfg_msi_cfg *) match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	msi_data->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(msi_data->regs)) {
		dev_err(&pdev->dev, "failed to initialize 'regs'\n");
		return PTR_ERR(msi_data->regs);
	}
	msi_data->msiir_addr = res->start;

	msi_data->pdev = pdev;
	spin_lock_init(&msi_data->lock);

	msi_data->irqs_num = MSI_IRQS_PER_MSIR *
			     (1 << msi_data->cfg->ibs_shift);
	msi_data->used = devm_bitmap_zalloc(&pdev->dev, msi_data->irqs_num, GFP_KERNEL);
	if (!msi_data->used)
		return -ENOMEM;
	/*
	 * Reserve all the hwirqs
	 * The available hwirqs will be released in ls1_msi_setup_hwirq()
	 */
	bitmap_set(msi_data->used, 0, msi_data->irqs_num);

	msi_data->msir_num = of_irq_count(pdev->dev.of_node);

	if (msi_affinity_flag) {
		u32 cpu_num;

		cpu_num = num_possible_cpus();
		if (msi_data->msir_num >= cpu_num)
			msi_data->msir_num = cpu_num;
		else
			msi_affinity_flag = 0;
	}

	msi_data->msir = devm_kcalloc(&pdev->dev, msi_data->msir_num,
				      sizeof(*msi_data->msir),
				      GFP_KERNEL);
	if (!msi_data->msir)
		return -ENOMEM;

	for (i = 0; i < msi_data->msir_num; i++)
		ls_scfg_msi_setup_hwirq(msi_data, i);

	ret = ls_scfg_msi_domains_init(msi_data);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, msi_data);

	return 0;
}

static int ls_scfg_msi_remove(struct platform_device *pdev)
{
	struct ls_scfg_msi *msi_data = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < msi_data->msir_num; i++)
		ls_scfg_msi_teardown_hwirq(&msi_data->msir[i]);

	irq_domain_remove(msi_data->msi_domain);
	irq_domain_remove(msi_data->parent);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver ls_scfg_msi_driver = {
	.driver = {
		.name = "ls-scfg-msi",
		.of_match_table = ls_scfg_msi_id,
	},
	.probe = ls_scfg_msi_probe,
	.remove = ls_scfg_msi_remove,
};

module_platform_driver(ls_scfg_msi_driver);

MODULE_AUTHOR("Minghuan Lian <Minghuan.Lian@nxp.com>");
MODULE_DESCRIPTION("Freescale Layerscape SCFG MSI controller driver");
