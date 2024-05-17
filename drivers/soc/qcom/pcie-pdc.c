// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <soc/qcom/pcie-pdc.h>

#define IRQ_i_CFG		0x110
#define IRQ_i_CFG_IRQ_ENABLE	BIT(3)
#define IRQ_i_CFG_TYPE_MASK	0x7
#define IRQ_i_CFG_DISTANCE	0x4

#define IRQ_i_GP_IRQ_SELECT	0x4900
#define IRQ_i_GP_IRQ_DISTANCE	0x14

struct irq_map {
	u32 gpio;
	u32 mux;
	u32 irq;
};

struct pdc_match_data {
	const struct irq_map *map;
	u32 size;
};

static const struct pdc_match_data *d;
static DEFINE_RAW_SPINLOCK(pdc_lock);
static void __iomem *pcie_pdc_base;

enum pdc_irq_config_bits {
	PDC_LEVEL_LOW		= 0b000,
	PDC_EDGE_FALLING	= 0b010,
	PDC_LEVEL_HIGH		= 0b100,
	PDC_EDGE_RISING		= 0b110,
	PDC_EDGE_DUAL		= 0b111,
};

static int pdc_cfg_irq(u32 irq, u32 mux, int mux_select, unsigned int type, bool enable)
{
	unsigned long flags;
	u32 value;
	enum pdc_irq_config_bits pdc_type;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		pdc_type = PDC_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		pdc_type = PDC_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		pdc_type = PDC_EDGE_DUAL;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		pdc_type = PDC_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		pdc_type = PDC_LEVEL_LOW;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	if (enable) {
		value = mux;
		pdc_type |= IRQ_i_CFG_IRQ_ENABLE;
	} else {
		value = 0;
		pdc_type &= ~IRQ_i_CFG_IRQ_ENABLE;
	}

	raw_spin_lock_irqsave(&pdc_lock, flags);

	writel_relaxed(value, pcie_pdc_base + IRQ_i_GP_IRQ_SELECT +
		       mux_select * IRQ_i_GP_IRQ_DISTANCE);
	writel_relaxed(pdc_type, pcie_pdc_base + IRQ_i_CFG + irq * IRQ_i_CFG_DISTANCE);

	raw_spin_unlock_irqrestore(&pdc_lock, flags);

	return 0;
}

/**
 * pcie_pdc_cfg_irq() - Configure the GPIO interrupt at PCIe PDC
 * @gpio:      The wake capable GPIO number
 * @type:      The interrupt type for GPIO
 * @enable:    Enable or disable GPIO interrupt
 *
 * Configures the GPIO interrupt at PCIe PDC.
 *
 * Return:
 * * 0			- Success
 * * -Error		- Error code
 */
int pcie_pdc_cfg_irq(u32 gpio, unsigned int type, bool enable)
{
	int i;

	if (!d)
		return -ENODEV;

	for (i = 0; i < d->size; i++) {
		if (gpio == d->map[i].gpio)
			return pdc_cfg_irq(d->map[i].irq, d->map[i].mux, i, type, enable);
	}

	return -EINVAL;
}
EXPORT_SYMBOL(pcie_pdc_cfg_irq);

static int qcom_pcie_pdc_probe(struct platform_device *pdev)
{
	pcie_pdc_base = devm_platform_ioremap_resource(pdev, 0);
	if (!pcie_pdc_base)
		return -ENXIO;

	d = of_device_get_match_data(&pdev->dev);
	if (!d)
		return -EINVAL;

	return 0;
}

static const struct irq_map niobe_irq_map[] = {
	{ 126, 58, 10 },
};

static const struct pdc_match_data niobe_pdc_match_data = {
	.map = niobe_irq_map,
	.size = ARRAY_SIZE(niobe_irq_map),
};

static const struct irq_map pineapple_irq_map[] = {
	{ 95, 67, 10 },
	{ 98, 50, 11 },
};

static const struct pdc_match_data pineapple_pdc_match_data = {
	.map = pineapple_irq_map,
	.size = ARRAY_SIZE(pineapple_irq_map),
};

static const struct irq_map cliffs_irq_map[] = {
	{ 118, 95, 10 },
};

static const struct pdc_match_data cliffs_pdc_match_data = {
	.map = cliffs_irq_map,
	.size = ARRAY_SIZE(cliffs_irq_map),
};

static const struct of_device_id qcom_pcie_pdc_match_table[] = {
	{ .compatible = "qcom,pineapple-pcie-pdc", .data = &pineapple_pdc_match_data },
	{ .compatible = "qcom,cliffs-pcie-pdc", .data = &cliffs_pdc_match_data },
	{ .compatible = "qcom,niobe-pcie-pdc", .data = &niobe_pdc_match_data },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_pcie_pdc_match_table);

static struct platform_driver qcom_pcie_pdc_driver = {
	.probe = qcom_pcie_pdc_probe,
	.driver = {
		.name = "qcom-pcie-pdc",
		.of_match_table = qcom_pcie_pdc_match_table,
	},
};

module_platform_driver(qcom_pcie_pdc_driver);
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Power Domain Controller for PCIe");
MODULE_LICENSE("GPL");
