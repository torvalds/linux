/*
 * Copyright (C) 2017 Marvell
 *
 * Hanna Hawa <hannah@marvell.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <dt-bindings/interrupt-controller/mvebu-icu.h>

/* ICU registers */
#define ICU_SETSPI_NSR_AL	0x10
#define ICU_SETSPI_NSR_AH	0x14
#define ICU_CLRSPI_NSR_AL	0x18
#define ICU_CLRSPI_NSR_AH	0x1c
#define ICU_SET_SEI_AL		0x50
#define ICU_SET_SEI_AH		0x54
#define ICU_CLR_SEI_AL		0x58
#define ICU_CLR_SEI_AH		0x5C
#define ICU_INT_CFG(x)          (0x100 + 4 * (x))
#define   ICU_INT_ENABLE	BIT(24)
#define   ICU_IS_EDGE		BIT(28)
#define   ICU_GROUP_SHIFT	29

/* ICU definitions */
#define ICU_MAX_IRQS		207
#define ICU_SATA0_ICU_ID	109
#define ICU_SATA1_ICU_ID	107

struct mvebu_icu_subset_data {
	unsigned int icu_group;
	unsigned int offset_set_ah;
	unsigned int offset_set_al;
	unsigned int offset_clr_ah;
	unsigned int offset_clr_al;
};

struct mvebu_icu {
	void __iomem *base;
	struct device *dev;
};

struct mvebu_icu_msi_data {
	struct mvebu_icu *icu;
	atomic_t initialized;
	const struct mvebu_icu_subset_data *subset_data;
};

struct mvebu_icu_irq_data {
	struct mvebu_icu *icu;
	unsigned int icu_group;
	unsigned int type;
};

DEFINE_STATIC_KEY_FALSE(legacy_bindings);

static void mvebu_icu_init(struct mvebu_icu *icu,
			   struct mvebu_icu_msi_data *msi_data,
			   struct msi_msg *msg)
{
	const struct mvebu_icu_subset_data *subset = msi_data->subset_data;

	if (atomic_cmpxchg(&msi_data->initialized, false, true))
		return;

	/* Set 'SET' ICU SPI message address in AP */
	writel_relaxed(msg[0].address_hi, icu->base + subset->offset_set_ah);
	writel_relaxed(msg[0].address_lo, icu->base + subset->offset_set_al);

	if (subset->icu_group != ICU_GRP_NSR)
		return;

	/* Set 'CLEAR' ICU SPI message address in AP (level-MSI only) */
	writel_relaxed(msg[1].address_hi, icu->base + subset->offset_clr_ah);
	writel_relaxed(msg[1].address_lo, icu->base + subset->offset_clr_al);
}

static void mvebu_icu_write_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	struct irq_data *d = irq_get_irq_data(desc->irq);
	struct mvebu_icu_msi_data *msi_data = platform_msi_get_host_data(d->domain);
	struct mvebu_icu_irq_data *icu_irqd = d->chip_data;
	struct mvebu_icu *icu = icu_irqd->icu;
	unsigned int icu_int;

	if (msg->address_lo || msg->address_hi) {
		/* One off initialization per domain */
		mvebu_icu_init(icu, msi_data, msg);
		/* Configure the ICU with irq number & type */
		icu_int = msg->data | ICU_INT_ENABLE;
		if (icu_irqd->type & IRQ_TYPE_EDGE_RISING)
			icu_int |= ICU_IS_EDGE;
		icu_int |= icu_irqd->icu_group << ICU_GROUP_SHIFT;
	} else {
		/* De-configure the ICU */
		icu_int = 0;
	}

	writel_relaxed(icu_int, icu->base + ICU_INT_CFG(d->hwirq));

	/*
	 * The SATA unit has 2 ports, and a dedicated ICU entry per
	 * port. The ahci sata driver supports only one irq interrupt
	 * per SATA unit. To solve this conflict, we configure the 2
	 * SATA wired interrupts in the south bridge into 1 GIC
	 * interrupt in the north bridge. Even if only a single port
	 * is enabled, if sata node is enabled, both interrupts are
	 * configured (regardless of which port is actually in use).
	 */
	if (d->hwirq == ICU_SATA0_ICU_ID || d->hwirq == ICU_SATA1_ICU_ID) {
		writel_relaxed(icu_int,
			       icu->base + ICU_INT_CFG(ICU_SATA0_ICU_ID));
		writel_relaxed(icu_int,
			       icu->base + ICU_INT_CFG(ICU_SATA1_ICU_ID));
	}
}

static struct irq_chip mvebu_icu_nsr_chip = {
	.name			= "ICU-NSR",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
};

static struct irq_chip mvebu_icu_sei_chip = {
	.name			= "ICU-SEI",
	.irq_ack		= irq_chip_ack_parent,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
};

static int
mvebu_icu_irq_domain_translate(struct irq_domain *d, struct irq_fwspec *fwspec,
			       unsigned long *hwirq, unsigned int *type)
{
	struct mvebu_icu_msi_data *msi_data = platform_msi_get_host_data(d);
	struct mvebu_icu *icu = platform_msi_get_host_data(d);
	unsigned int param_count = static_branch_unlikely(&legacy_bindings) ? 3 : 2;

	/* Check the count of the parameters in dt */
	if (WARN_ON(fwspec->param_count != param_count)) {
		dev_err(icu->dev, "wrong ICU parameter count %d\n",
			fwspec->param_count);
		return -EINVAL;
	}

	if (static_branch_unlikely(&legacy_bindings)) {
		*hwirq = fwspec->param[1];
		*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;
		if (fwspec->param[0] != ICU_GRP_NSR) {
			dev_err(icu->dev, "wrong ICU group type %x\n",
				fwspec->param[0]);
			return -EINVAL;
		}
	} else {
		*hwirq = fwspec->param[0];
		*type = fwspec->param[1] & IRQ_TYPE_SENSE_MASK;

		/*
		 * The ICU receives level interrupts. While the NSR are also
		 * level interrupts, SEI are edge interrupts. Force the type
		 * here in this case. Please note that this makes the interrupt
		 * handling unreliable.
		 */
		if (msi_data->subset_data->icu_group == ICU_GRP_SEI)
			*type = IRQ_TYPE_EDGE_RISING;
	}

	if (*hwirq >= ICU_MAX_IRQS) {
		dev_err(icu->dev, "invalid interrupt number %ld\n", *hwirq);
		return -EINVAL;
	}

	return 0;
}

static int
mvebu_icu_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
			   unsigned int nr_irqs, void *args)
{
	int err;
	unsigned long hwirq;
	struct irq_fwspec *fwspec = args;
	struct mvebu_icu_msi_data *msi_data = platform_msi_get_host_data(domain);
	struct mvebu_icu *icu = msi_data->icu;
	struct mvebu_icu_irq_data *icu_irqd;
	struct irq_chip *chip = &mvebu_icu_nsr_chip;

	icu_irqd = kmalloc(sizeof(*icu_irqd), GFP_KERNEL);
	if (!icu_irqd)
		return -ENOMEM;

	err = mvebu_icu_irq_domain_translate(domain, fwspec, &hwirq,
					     &icu_irqd->type);
	if (err) {
		dev_err(icu->dev, "failed to translate ICU parameters\n");
		goto free_irqd;
	}

	if (static_branch_unlikely(&legacy_bindings))
		icu_irqd->icu_group = fwspec->param[0];
	else
		icu_irqd->icu_group = msi_data->subset_data->icu_group;
	icu_irqd->icu = icu;

	err = platform_msi_domain_alloc(domain, virq, nr_irqs);
	if (err) {
		dev_err(icu->dev, "failed to allocate ICU interrupt in parent domain\n");
		goto free_irqd;
	}

	/* Make sure there is no interrupt left pending by the firmware */
	err = irq_set_irqchip_state(virq, IRQCHIP_STATE_PENDING, false);
	if (err)
		goto free_msi;

	if (icu_irqd->icu_group == ICU_GRP_SEI)
		chip = &mvebu_icu_sei_chip;

	err = irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
					    chip, icu_irqd);
	if (err) {
		dev_err(icu->dev, "failed to set the data to IRQ domain\n");
		goto free_msi;
	}

	return 0;

free_msi:
	platform_msi_domain_free(domain, virq, nr_irqs);
free_irqd:
	kfree(icu_irqd);
	return err;
}

static void
mvebu_icu_irq_domain_free(struct irq_domain *domain, unsigned int virq,
			  unsigned int nr_irqs)
{
	struct irq_data *d = irq_get_irq_data(virq);
	struct mvebu_icu_irq_data *icu_irqd = d->chip_data;

	kfree(icu_irqd);

	platform_msi_domain_free(domain, virq, nr_irqs);
}

static const struct irq_domain_ops mvebu_icu_domain_ops = {
	.translate = mvebu_icu_irq_domain_translate,
	.alloc     = mvebu_icu_irq_domain_alloc,
	.free      = mvebu_icu_irq_domain_free,
};

static const struct mvebu_icu_subset_data mvebu_icu_nsr_subset_data = {
	.icu_group = ICU_GRP_NSR,
	.offset_set_ah = ICU_SETSPI_NSR_AH,
	.offset_set_al = ICU_SETSPI_NSR_AL,
	.offset_clr_ah = ICU_CLRSPI_NSR_AH,
	.offset_clr_al = ICU_CLRSPI_NSR_AL,
};

static const struct mvebu_icu_subset_data mvebu_icu_sei_subset_data = {
	.icu_group = ICU_GRP_SEI,
	.offset_set_ah = ICU_SET_SEI_AH,
	.offset_set_al = ICU_SET_SEI_AL,
};

static const struct of_device_id mvebu_icu_subset_of_match[] = {
	{
		.compatible = "marvell,cp110-icu-nsr",
		.data = &mvebu_icu_nsr_subset_data,
	},
	{
		.compatible = "marvell,cp110-icu-sei",
		.data = &mvebu_icu_sei_subset_data,
	},
	{},
};

static int mvebu_icu_subset_probe(struct platform_device *pdev)
{
	struct mvebu_icu_msi_data *msi_data;
	struct device_node *msi_parent_dn;
	struct device *dev = &pdev->dev;
	struct irq_domain *irq_domain;

	msi_data = devm_kzalloc(dev, sizeof(*msi_data), GFP_KERNEL);
	if (!msi_data)
		return -ENOMEM;

	if (static_branch_unlikely(&legacy_bindings)) {
		msi_data->icu = dev_get_drvdata(dev);
		msi_data->subset_data = &mvebu_icu_nsr_subset_data;
	} else {
		msi_data->icu = dev_get_drvdata(dev->parent);
		msi_data->subset_data = of_device_get_match_data(dev);
	}

	dev->msi_domain = of_msi_get_domain(dev, dev->of_node,
					    DOMAIN_BUS_PLATFORM_MSI);
	if (!dev->msi_domain)
		return -EPROBE_DEFER;

	msi_parent_dn = irq_domain_get_of_node(dev->msi_domain);
	if (!msi_parent_dn)
		return -ENODEV;

	irq_domain = platform_msi_create_device_tree_domain(dev, ICU_MAX_IRQS,
							    mvebu_icu_write_msg,
							    &mvebu_icu_domain_ops,
							    msi_data);
	if (!irq_domain) {
		dev_err(dev, "Failed to create ICU MSI domain\n");
		return -ENOMEM;
	}

	return 0;
}

static struct platform_driver mvebu_icu_subset_driver = {
	.probe  = mvebu_icu_subset_probe,
	.driver = {
		.name = "mvebu-icu-subset",
		.of_match_table = mvebu_icu_subset_of_match,
	},
};
builtin_platform_driver(mvebu_icu_subset_driver);

static int mvebu_icu_probe(struct platform_device *pdev)
{
	struct mvebu_icu *icu;
	struct resource *res;
	int i;

	icu = devm_kzalloc(&pdev->dev, sizeof(struct mvebu_icu),
			   GFP_KERNEL);
	if (!icu)
		return -ENOMEM;

	icu->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	icu->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(icu->base)) {
		dev_err(&pdev->dev, "Failed to map icu base address.\n");
		return PTR_ERR(icu->base);
	}

	/*
	 * Legacy bindings: ICU is one node with one MSI parent: force manually
	 *                  the probe of the NSR interrupts side.
	 * New bindings: ICU node has children, one per interrupt controller
	 *               having its own MSI parent: call platform_populate().
	 * All ICU instances should use the same bindings.
	 */
	if (!of_get_child_count(pdev->dev.of_node))
		static_branch_enable(&legacy_bindings);

	/*
	 * Clean all ICU interrupts of type NSR and SEI, required to
	 * avoid unpredictable SPI assignments done by firmware.
	 */
	for (i = 0 ; i < ICU_MAX_IRQS ; i++) {
		u32 icu_int, icu_grp;

		icu_int = readl_relaxed(icu->base + ICU_INT_CFG(i));
		icu_grp = icu_int >> ICU_GROUP_SHIFT;

		if (icu_grp == ICU_GRP_NSR ||
		    (icu_grp == ICU_GRP_SEI &&
		     !static_branch_unlikely(&legacy_bindings)))
			writel_relaxed(0x0, icu->base + ICU_INT_CFG(i));
	}

	platform_set_drvdata(pdev, icu);

	if (static_branch_unlikely(&legacy_bindings))
		return mvebu_icu_subset_probe(pdev);
	else
		return devm_of_platform_populate(&pdev->dev);
}

static const struct of_device_id mvebu_icu_of_match[] = {
	{ .compatible = "marvell,cp110-icu", },
	{},
};

static struct platform_driver mvebu_icu_driver = {
	.probe  = mvebu_icu_probe,
	.driver = {
		.name = "mvebu-icu",
		.of_match_table = mvebu_icu_of_match,
	},
};
builtin_platform_driver(mvebu_icu_driver);
