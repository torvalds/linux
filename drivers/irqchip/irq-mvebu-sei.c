// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "mvebu-sei: " fmt

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

/* Cause register */
#define GICP_SECR(idx)		(0x0  + ((idx) * 0x4))
/* Mask register */
#define GICP_SEMR(idx)		(0x20 + ((idx) * 0x4))
#define GICP_SET_SEI_OFFSET	0x30

#define SEI_IRQ_COUNT_PER_REG	32
#define SEI_IRQ_REG_COUNT	2
#define SEI_IRQ_COUNT		(SEI_IRQ_COUNT_PER_REG * SEI_IRQ_REG_COUNT)
#define SEI_IRQ_REG_IDX(irq_id)	((irq_id) / SEI_IRQ_COUNT_PER_REG)
#define SEI_IRQ_REG_BIT(irq_id)	((irq_id) % SEI_IRQ_COUNT_PER_REG)

struct mvebu_sei_interrupt_range {
	u32 first;
	u32 size;
};

struct mvebu_sei_caps {
	struct mvebu_sei_interrupt_range ap_range;
	struct mvebu_sei_interrupt_range cp_range;
};

struct mvebu_sei {
	struct device *dev;
	void __iomem *base;
	struct resource *res;
	struct irq_domain *sei_domain;
	struct irq_domain *ap_domain;
	struct irq_domain *cp_domain;
	const struct mvebu_sei_caps *caps;

	/* Lock on MSI allocations/releases */
	struct mutex cp_msi_lock;
	DECLARE_BITMAP(cp_msi_bitmap, SEI_IRQ_COUNT);

	/* Lock on IRQ masking register */
	raw_spinlock_t mask_lock;
};

static void mvebu_sei_ack_irq(struct irq_data *d)
{
	struct mvebu_sei *sei = irq_data_get_irq_chip_data(d);
	u32 reg_idx = SEI_IRQ_REG_IDX(d->hwirq);

	writel_relaxed(BIT(SEI_IRQ_REG_BIT(d->hwirq)),
		       sei->base + GICP_SECR(reg_idx));
}

static void mvebu_sei_mask_irq(struct irq_data *d)
{
	struct mvebu_sei *sei = irq_data_get_irq_chip_data(d);
	u32 reg, reg_idx = SEI_IRQ_REG_IDX(d->hwirq);
	unsigned long flags;

	/* 1 disables the interrupt */
	raw_spin_lock_irqsave(&sei->mask_lock, flags);
	reg = readl_relaxed(sei->base + GICP_SEMR(reg_idx));
	reg |= BIT(SEI_IRQ_REG_BIT(d->hwirq));
	writel_relaxed(reg, sei->base + GICP_SEMR(reg_idx));
	raw_spin_unlock_irqrestore(&sei->mask_lock, flags);
}

static void mvebu_sei_unmask_irq(struct irq_data *d)
{
	struct mvebu_sei *sei = irq_data_get_irq_chip_data(d);
	u32 reg, reg_idx = SEI_IRQ_REG_IDX(d->hwirq);
	unsigned long flags;

	/* 0 enables the interrupt */
	raw_spin_lock_irqsave(&sei->mask_lock, flags);
	reg = readl_relaxed(sei->base + GICP_SEMR(reg_idx));
	reg &= ~BIT(SEI_IRQ_REG_BIT(d->hwirq));
	writel_relaxed(reg, sei->base + GICP_SEMR(reg_idx));
	raw_spin_unlock_irqrestore(&sei->mask_lock, flags);
}

static int mvebu_sei_set_affinity(struct irq_data *d,
				  const struct cpumask *mask_val,
				  bool force)
{
	return -EINVAL;
}

static int mvebu_sei_set_irqchip_state(struct irq_data *d,
				       enum irqchip_irq_state which,
				       bool state)
{
	/* We can only clear the pending state by acking the interrupt */
	if (which != IRQCHIP_STATE_PENDING || state)
		return -EINVAL;

	mvebu_sei_ack_irq(d);
	return 0;
}

static struct irq_chip mvebu_sei_irq_chip = {
	.name			= "SEI",
	.irq_ack		= mvebu_sei_ack_irq,
	.irq_mask		= mvebu_sei_mask_irq,
	.irq_unmask		= mvebu_sei_unmask_irq,
	.irq_set_affinity       = mvebu_sei_set_affinity,
	.irq_set_irqchip_state	= mvebu_sei_set_irqchip_state,
};

static int mvebu_sei_ap_set_type(struct irq_data *data, unsigned int type)
{
	if ((type & IRQ_TYPE_SENSE_MASK) != IRQ_TYPE_LEVEL_HIGH)
		return -EINVAL;

	return 0;
}

static struct irq_chip mvebu_sei_ap_irq_chip = {
	.name			= "AP SEI",
	.irq_ack		= irq_chip_ack_parent,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_set_affinity       = irq_chip_set_affinity_parent,
	.irq_set_type		= mvebu_sei_ap_set_type,
};

static void mvebu_sei_cp_compose_msi_msg(struct irq_data *data,
					 struct msi_msg *msg)
{
	struct mvebu_sei *sei = data->chip_data;
	phys_addr_t set = sei->res->start + GICP_SET_SEI_OFFSET;

	msg->data = data->hwirq + sei->caps->cp_range.first;
	msg->address_lo = lower_32_bits(set);
	msg->address_hi = upper_32_bits(set);
}

static int mvebu_sei_cp_set_type(struct irq_data *data, unsigned int type)
{
	if ((type & IRQ_TYPE_SENSE_MASK) != IRQ_TYPE_EDGE_RISING)
		return -EINVAL;

	return 0;
}

static struct irq_chip mvebu_sei_cp_irq_chip = {
	.name			= "CP SEI",
	.irq_ack		= irq_chip_ack_parent,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_set_affinity       = irq_chip_set_affinity_parent,
	.irq_set_type		= mvebu_sei_cp_set_type,
	.irq_compose_msi_msg	= mvebu_sei_cp_compose_msi_msg,
};

static int mvebu_sei_domain_alloc(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs, void *arg)
{
	struct mvebu_sei *sei = domain->host_data;
	struct irq_fwspec *fwspec = arg;

	/* Not much to do, just setup the irqdata */
	irq_domain_set_hwirq_and_chip(domain, virq, fwspec->param[0],
				      &mvebu_sei_irq_chip, sei);

	return 0;
}

static void mvebu_sei_domain_free(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);
		irq_set_handler(virq + i, NULL);
		irq_domain_reset_irq_data(d);
	}
}

static const struct irq_domain_ops mvebu_sei_domain_ops = {
	.alloc	= mvebu_sei_domain_alloc,
	.free	= mvebu_sei_domain_free,
};

static int mvebu_sei_ap_translate(struct irq_domain *domain,
				  struct irq_fwspec *fwspec,
				  unsigned long *hwirq,
				  unsigned int *type)
{
	*hwirq = fwspec->param[0];
	*type  = IRQ_TYPE_LEVEL_HIGH;

	return 0;
}

static int mvebu_sei_ap_alloc(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs, void *arg)
{
	struct mvebu_sei *sei = domain->host_data;
	struct irq_fwspec fwspec;
	unsigned long hwirq;
	unsigned int type;
	int err;

	mvebu_sei_ap_translate(domain, arg, &hwirq, &type);

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 1;
	fwspec.param[0] = hwirq + sei->caps->ap_range.first;

	err = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (err)
		return err;

	irq_domain_set_info(domain, virq, hwirq,
			    &mvebu_sei_ap_irq_chip, sei,
			    handle_level_irq, NULL, NULL);
	irq_set_probe(virq);

	return 0;
}

static const struct irq_domain_ops mvebu_sei_ap_domain_ops = {
	.translate	= mvebu_sei_ap_translate,
	.alloc		= mvebu_sei_ap_alloc,
	.free		= irq_domain_free_irqs_parent,
};

static void mvebu_sei_cp_release_irq(struct mvebu_sei *sei, unsigned long hwirq)
{
	mutex_lock(&sei->cp_msi_lock);
	clear_bit(hwirq, sei->cp_msi_bitmap);
	mutex_unlock(&sei->cp_msi_lock);
}

static int mvebu_sei_cp_domain_alloc(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs,
				     void *args)
{
	struct mvebu_sei *sei = domain->host_data;
	struct irq_fwspec fwspec;
	unsigned long hwirq;
	int ret;

	/* The software only supports single allocations for now */
	if (nr_irqs != 1)
		return -ENOTSUPP;

	mutex_lock(&sei->cp_msi_lock);
	hwirq = find_first_zero_bit(sei->cp_msi_bitmap,
				    sei->caps->cp_range.size);
	if (hwirq < sei->caps->cp_range.size)
		set_bit(hwirq, sei->cp_msi_bitmap);
	mutex_unlock(&sei->cp_msi_lock);

	if (hwirq == sei->caps->cp_range.size)
		return -ENOSPC;

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 1;
	fwspec.param[0] = hwirq + sei->caps->cp_range.first;

	ret = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (ret)
		goto free_irq;

	irq_domain_set_info(domain, virq, hwirq,
			    &mvebu_sei_cp_irq_chip, sei,
			    handle_edge_irq, NULL, NULL);

	return 0;

free_irq:
	mvebu_sei_cp_release_irq(sei, hwirq);
	return ret;
}

static void mvebu_sei_cp_domain_free(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs)
{
	struct mvebu_sei *sei = domain->host_data;
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);

	if (nr_irqs != 1 || d->hwirq >= sei->caps->cp_range.size) {
		dev_err(sei->dev, "Invalid hwirq %lu\n", d->hwirq);
		return;
	}

	mvebu_sei_cp_release_irq(sei, d->hwirq);
	irq_domain_free_irqs_parent(domain, virq, 1);
}

static const struct irq_domain_ops mvebu_sei_cp_domain_ops = {
	.alloc	= mvebu_sei_cp_domain_alloc,
	.free	= mvebu_sei_cp_domain_free,
};

static struct irq_chip mvebu_sei_msi_irq_chip = {
	.name		= "SEI pMSI",
	.irq_ack	= irq_chip_ack_parent,
	.irq_set_type	= irq_chip_set_type_parent,
};

static struct msi_domain_ops mvebu_sei_msi_ops = {
};

static struct msi_domain_info mvebu_sei_msi_domain_info = {
	.flags	= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS,
	.ops	= &mvebu_sei_msi_ops,
	.chip	= &mvebu_sei_msi_irq_chip,
};

static void mvebu_sei_handle_cascade_irq(struct irq_desc *desc)
{
	struct mvebu_sei *sei = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 idx;

	chained_irq_enter(chip, desc);

	for (idx = 0; idx < SEI_IRQ_REG_COUNT; idx++) {
		unsigned long irqmap;
		int bit;

		irqmap = readl_relaxed(sei->base + GICP_SECR(idx));
		for_each_set_bit(bit, &irqmap, SEI_IRQ_COUNT_PER_REG) {
			unsigned long hwirq;
			int err;

			hwirq = idx * SEI_IRQ_COUNT_PER_REG + bit;
			err = generic_handle_domain_irq(sei->sei_domain, hwirq);
			if (unlikely(err))
				dev_warn(sei->dev, "Spurious IRQ detected (hwirq %lu)\n", hwirq);
		}
	}

	chained_irq_exit(chip, desc);
}

static void mvebu_sei_reset(struct mvebu_sei *sei)
{
	u32 reg_idx;

	/* Clear IRQ cause registers, mask all interrupts */
	for (reg_idx = 0; reg_idx < SEI_IRQ_REG_COUNT; reg_idx++) {
		writel_relaxed(0xFFFFFFFF, sei->base + GICP_SECR(reg_idx));
		writel_relaxed(0xFFFFFFFF, sei->base + GICP_SEMR(reg_idx));
	}
}

static int mvebu_sei_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct irq_domain *plat_domain;
	struct mvebu_sei *sei;
	u32 parent_irq;
	int ret;

	sei = devm_kzalloc(&pdev->dev, sizeof(*sei), GFP_KERNEL);
	if (!sei)
		return -ENOMEM;

	sei->dev = &pdev->dev;

	mutex_init(&sei->cp_msi_lock);
	raw_spin_lock_init(&sei->mask_lock);

	sei->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sei->base = devm_ioremap_resource(sei->dev, sei->res);
	if (IS_ERR(sei->base))
		return PTR_ERR(sei->base);

	/* Retrieve the SEI capabilities with the interrupt ranges */
	sei->caps = of_device_get_match_data(&pdev->dev);
	if (!sei->caps) {
		dev_err(sei->dev,
			"Could not retrieve controller capabilities\n");
		return -EINVAL;
	}

	/*
	 * Reserve the single (top-level) parent SPI IRQ from which all the
	 * interrupts handled by this driver will be signaled.
	 */
	parent_irq = irq_of_parse_and_map(node, 0);
	if (parent_irq <= 0) {
		dev_err(sei->dev, "Failed to retrieve top-level SPI IRQ\n");
		return -ENODEV;
	}

	/* Create the root SEI domain */
	sei->sei_domain = irq_domain_create_linear(of_node_to_fwnode(node),
						   (sei->caps->ap_range.size +
						    sei->caps->cp_range.size),
						   &mvebu_sei_domain_ops,
						   sei);
	if (!sei->sei_domain) {
		dev_err(sei->dev, "Failed to create SEI IRQ domain\n");
		ret = -ENOMEM;
		goto dispose_irq;
	}

	irq_domain_update_bus_token(sei->sei_domain, DOMAIN_BUS_NEXUS);

	/* Create the 'wired' domain */
	sei->ap_domain = irq_domain_create_hierarchy(sei->sei_domain, 0,
						     sei->caps->ap_range.size,
						     of_node_to_fwnode(node),
						     &mvebu_sei_ap_domain_ops,
						     sei);
	if (!sei->ap_domain) {
		dev_err(sei->dev, "Failed to create AP IRQ domain\n");
		ret = -ENOMEM;
		goto remove_sei_domain;
	}

	irq_domain_update_bus_token(sei->ap_domain, DOMAIN_BUS_WIRED);

	/* Create the 'MSI' domain */
	sei->cp_domain = irq_domain_create_hierarchy(sei->sei_domain, 0,
						     sei->caps->cp_range.size,
						     of_node_to_fwnode(node),
						     &mvebu_sei_cp_domain_ops,
						     sei);
	if (!sei->cp_domain) {
		pr_err("Failed to create CPs IRQ domain\n");
		ret = -ENOMEM;
		goto remove_ap_domain;
	}

	irq_domain_update_bus_token(sei->cp_domain, DOMAIN_BUS_GENERIC_MSI);

	plat_domain = platform_msi_create_irq_domain(of_node_to_fwnode(node),
						     &mvebu_sei_msi_domain_info,
						     sei->cp_domain);
	if (!plat_domain) {
		pr_err("Failed to create CPs MSI domain\n");
		ret = -ENOMEM;
		goto remove_cp_domain;
	}

	mvebu_sei_reset(sei);

	irq_set_chained_handler_and_data(parent_irq,
					 mvebu_sei_handle_cascade_irq,
					 sei);

	return 0;

remove_cp_domain:
	irq_domain_remove(sei->cp_domain);
remove_ap_domain:
	irq_domain_remove(sei->ap_domain);
remove_sei_domain:
	irq_domain_remove(sei->sei_domain);
dispose_irq:
	irq_dispose_mapping(parent_irq);

	return ret;
}

static struct mvebu_sei_caps mvebu_sei_ap806_caps = {
	.ap_range = {
		.first = 0,
		.size = 21,
	},
	.cp_range = {
		.first = 21,
		.size = 43,
	},
};

static const struct of_device_id mvebu_sei_of_match[] = {
	{
		.compatible = "marvell,ap806-sei",
		.data = &mvebu_sei_ap806_caps,
	},
	{},
};

static struct platform_driver mvebu_sei_driver = {
	.probe  = mvebu_sei_probe,
	.driver = {
		.name = "mvebu-sei",
		.of_match_table = mvebu_sei_of_match,
	},
};
builtin_platform_driver(mvebu_sei_driver);
