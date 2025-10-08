// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2025 ARM Limited, All Rights Reserved.
 */
#define pr_fmt(fmt)	"GICv5 IWB: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-v5.h>

struct gicv5_iwb_chip_data {
	void __iomem	*iwb_base;
	u16		nr_regs;
};

static u32 iwb_readl_relaxed(struct gicv5_iwb_chip_data *iwb_node, const u32 reg_offset)
{
	return readl_relaxed(iwb_node->iwb_base + reg_offset);
}

static void iwb_writel_relaxed(struct gicv5_iwb_chip_data *iwb_node, const u32 val,
			       const u32 reg_offset)
{
	writel_relaxed(val, iwb_node->iwb_base + reg_offset);
}

static int gicv5_iwb_wait_for_wenabler(struct gicv5_iwb_chip_data *iwb_node)
{
	return gicv5_wait_for_op_atomic(iwb_node->iwb_base, GICV5_IWB_WENABLE_STATUSR,
					GICV5_IWB_WENABLE_STATUSR_IDLE, NULL);
}

static int __gicv5_iwb_set_wire_enable(struct gicv5_iwb_chip_data *iwb_node,
				       u32 iwb_wire, bool enable)
{
	u32 n = iwb_wire / 32;
	u8 i = iwb_wire % 32;
	u32 val;

	if (n >= iwb_node->nr_regs) {
		pr_err("IWB_WENABLER<n> is invalid for n=%u\n", n);
		return -EINVAL;
	}

	/*
	 * Enable IWB wire/pin at this point
	 * Note: This is not the same as enabling the interrupt
	 */
	val = iwb_readl_relaxed(iwb_node, GICV5_IWB_WENABLER + (4 * n));
	if (enable)
		val |= BIT(i);
	else
		val &= ~BIT(i);
	iwb_writel_relaxed(iwb_node, val, GICV5_IWB_WENABLER + (4 * n));

	return gicv5_iwb_wait_for_wenabler(iwb_node);
}

static int gicv5_iwb_enable_wire(struct gicv5_iwb_chip_data *iwb_node,
				 u32 iwb_wire)
{
	return __gicv5_iwb_set_wire_enable(iwb_node, iwb_wire, true);
}

static int gicv5_iwb_disable_wire(struct gicv5_iwb_chip_data *iwb_node,
				  u32 iwb_wire)
{
	return __gicv5_iwb_set_wire_enable(iwb_node, iwb_wire, false);
}

static void gicv5_iwb_irq_disable(struct irq_data *d)
{
	struct gicv5_iwb_chip_data *iwb_node = irq_data_get_irq_chip_data(d);

	gicv5_iwb_disable_wire(iwb_node, d->hwirq);
	irq_chip_disable_parent(d);
}

static void gicv5_iwb_irq_enable(struct irq_data *d)
{
	struct gicv5_iwb_chip_data *iwb_node = irq_data_get_irq_chip_data(d);

	gicv5_iwb_enable_wire(iwb_node, d->hwirq);
	irq_chip_enable_parent(d);
}

static int gicv5_iwb_set_type(struct irq_data *d, unsigned int type)
{
	struct gicv5_iwb_chip_data *iwb_node = irq_data_get_irq_chip_data(d);
	u32 iwb_wire, n, wtmr;
	u8 i;

	iwb_wire = d->hwirq;
	i = iwb_wire % 32;
	n = iwb_wire / 32;

	if (n >= iwb_node->nr_regs) {
		pr_err_once("reg %u out of range\n", n);
		return -EINVAL;
	}

	wtmr = iwb_readl_relaxed(iwb_node, GICV5_IWB_WTMR + (4 * n));

	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
	case IRQ_TYPE_LEVEL_LOW:
		wtmr |= BIT(i);
		break;
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
		wtmr &= ~BIT(i);
		break;
	default:
		pr_debug("unexpected wire trigger mode");
		return -EINVAL;
	}

	iwb_writel_relaxed(iwb_node, wtmr, GICV5_IWB_WTMR + (4 * n));

	return 0;
}

static void gicv5_iwb_domain_set_desc(msi_alloc_info_t *alloc_info, struct msi_desc *desc)
{
	alloc_info->desc = desc;
	alloc_info->hwirq = (u32)desc->data.icookie.value;
}

static int gicv5_iwb_irq_domain_translate(struct irq_domain *d, struct irq_fwspec *fwspec,
					  irq_hw_number_t *hwirq,
					  unsigned int *type)
{
	if (!is_of_node(fwspec->fwnode))
		return -EINVAL;

	if (fwspec->param_count < 2)
		return -EINVAL;

	/*
	 * param[0] is be the wire
	 * param[1] is the interrupt type
	 */
	*hwirq = fwspec->param[0];
	*type = fwspec->param[1] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static void gicv5_iwb_write_msi_msg(struct irq_data *d, struct msi_msg *msg) {}

static const struct msi_domain_template iwb_msi_template = {
	.chip = {
		.name			= "GICv5-IWB",
		.irq_mask		= irq_chip_mask_parent,
		.irq_unmask		= irq_chip_unmask_parent,
		.irq_enable		= gicv5_iwb_irq_enable,
		.irq_disable		= gicv5_iwb_irq_disable,
		.irq_eoi		= irq_chip_eoi_parent,
		.irq_set_type		= gicv5_iwb_set_type,
		.irq_write_msi_msg	= gicv5_iwb_write_msi_msg,
		.irq_set_affinity	= irq_chip_set_affinity_parent,
		.irq_get_irqchip_state	= irq_chip_get_parent_state,
		.irq_set_irqchip_state	= irq_chip_set_parent_state,
		.flags			= IRQCHIP_SET_TYPE_MASKED |
					  IRQCHIP_SKIP_SET_WAKE |
					  IRQCHIP_MASK_ON_SUSPEND,
	},

	.ops = {
		.set_desc		= gicv5_iwb_domain_set_desc,
		.msi_translate		= gicv5_iwb_irq_domain_translate,
	},

	.info = {
		.bus_token		= DOMAIN_BUS_WIRED_TO_MSI,
		.flags			= MSI_FLAG_USE_DEV_FWNODE,
	},

	.alloc_info = {
		.flags			= MSI_ALLOC_FLAGS_FIXED_MSG_DATA,
	},
};

static bool gicv5_iwb_create_device_domain(struct device *dev, unsigned int size,
				     struct gicv5_iwb_chip_data *iwb_node)
{
	if (WARN_ON_ONCE(!dev->msi.domain))
		return false;

	return msi_create_device_irq_domain(dev, MSI_DEFAULT_DOMAIN,
					    &iwb_msi_template, size,
					    NULL, iwb_node);
}

static struct gicv5_iwb_chip_data *
gicv5_iwb_init_bases(void __iomem *iwb_base, struct platform_device *pdev)
{
	u32 nr_wires, idr0, cr0;
	unsigned int n;
	int ret;

	struct gicv5_iwb_chip_data *iwb_node __free(kfree) = kzalloc(sizeof(*iwb_node),
								     GFP_KERNEL);
	if (!iwb_node)
		return ERR_PTR(-ENOMEM);

	iwb_node->iwb_base = iwb_base;

	idr0 = iwb_readl_relaxed(iwb_node, GICV5_IWB_IDR0);
	nr_wires = (FIELD_GET(GICV5_IWB_IDR0_IW_RANGE, idr0) + 1) * 32;

	cr0 = iwb_readl_relaxed(iwb_node, GICV5_IWB_CR0);
	if (!FIELD_GET(GICV5_IWB_CR0_IWBEN, cr0)) {
		dev_err(&pdev->dev, "IWB must be enabled in firmware\n");
		return ERR_PTR(-EINVAL);
	}

	iwb_node->nr_regs = FIELD_GET(GICV5_IWB_IDR0_IW_RANGE, idr0) + 1;

	for (n = 0; n < iwb_node->nr_regs; n++)
		iwb_writel_relaxed(iwb_node, 0, GICV5_IWB_WENABLER + (sizeof(u32) * n));

	ret = gicv5_iwb_wait_for_wenabler(iwb_node);
	if (ret)
		return ERR_PTR(ret);

	if (!gicv5_iwb_create_device_domain(&pdev->dev, nr_wires, iwb_node))
		return ERR_PTR(-ENOMEM);

	return_ptr(iwb_node);
}

static int gicv5_iwb_device_probe(struct platform_device *pdev)
{
	struct gicv5_iwb_chip_data *iwb_node;
	void __iomem *iwb_base;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	iwb_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!iwb_base) {
		dev_err(&pdev->dev, "failed to ioremap %pR\n", res);
		return -ENOMEM;
	}

	iwb_node = gicv5_iwb_init_bases(iwb_base, pdev);
	if (IS_ERR(iwb_node))
		return PTR_ERR(iwb_node);

	return 0;
}

static const struct of_device_id gicv5_iwb_of_match[] = {
	{ .compatible = "arm,gic-v5-iwb" },
	{ /* END */ }
};
MODULE_DEVICE_TABLE(of, gicv5_iwb_of_match);

static struct platform_driver gicv5_iwb_platform_driver = {
	.driver = {
		.name			= "GICv5 IWB",
		.of_match_table		= gicv5_iwb_of_match,
		.suppress_bind_attrs	= true,
	},
	.probe				= gicv5_iwb_device_probe,
};

module_platform_driver(gicv5_iwb_platform_driver);
