// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <dt-bindings/mfd/qcom-pm8008.h>

#define I2C_INTR_STATUS_BASE		0x0550
#define INT_RT_STS_OFFSET		0x10
#define INT_SET_TYPE_OFFSET		0x11
#define INT_POL_HIGH_OFFSET		0x12
#define INT_POL_LOW_OFFSET		0x13
#define INT_LATCHED_CLR_OFFSET		0x14
#define INT_EN_SET_OFFSET		0x15
#define INT_EN_CLR_OFFSET		0x16
#define INT_LATCHED_STS_OFFSET		0x18

enum {
	PM8008_MISC,
	PM8008_TEMP_ALARM,
	PM8008_GPIO1,
	PM8008_GPIO2,
	PM8008_NUM_PERIPHS,
};

#define PM8008_PERIPH_0_BASE	0x900
#define PM8008_PERIPH_1_BASE	0x2400
#define PM8008_PERIPH_2_BASE	0xC000
#define PM8008_PERIPH_3_BASE	0xC100

#define PM8008_TEMP_ALARM_ADDR	PM8008_PERIPH_1_BASE
#define PM8008_GPIO1_ADDR	PM8008_PERIPH_2_BASE
#define PM8008_GPIO2_ADDR	PM8008_PERIPH_3_BASE

enum {
	SET_TYPE_INDEX,
	POLARITY_HI_INDEX,
	POLARITY_LO_INDEX,
};

static unsigned int pm8008_config_regs[] = {
	INT_SET_TYPE_OFFSET,
	INT_POL_HIGH_OFFSET,
	INT_POL_LOW_OFFSET,
};

static struct regmap_irq pm8008_irqs[] = {
	REGMAP_IRQ_REG(PM8008_IRQ_MISC_UVLO,	PM8008_MISC,	BIT(0)),
	REGMAP_IRQ_REG(PM8008_IRQ_MISC_OVLO,	PM8008_MISC,	BIT(1)),
	REGMAP_IRQ_REG(PM8008_IRQ_MISC_OTST2,	PM8008_MISC,	BIT(2)),
	REGMAP_IRQ_REG(PM8008_IRQ_MISC_OTST3,	PM8008_MISC,	BIT(3)),
	REGMAP_IRQ_REG(PM8008_IRQ_MISC_LDO_OCP,	PM8008_MISC,	BIT(4)),
	REGMAP_IRQ_REG(PM8008_IRQ_TEMP_ALARM,	PM8008_TEMP_ALARM, BIT(0)),
	REGMAP_IRQ_REG(PM8008_IRQ_GPIO1,	PM8008_GPIO1,	BIT(0)),
	REGMAP_IRQ_REG(PM8008_IRQ_GPIO2,	PM8008_GPIO2,	BIT(0)),
};

static const unsigned int pm8008_periph_base[] = {
	PM8008_PERIPH_0_BASE,
	PM8008_PERIPH_1_BASE,
	PM8008_PERIPH_2_BASE,
	PM8008_PERIPH_3_BASE,
};

static unsigned int pm8008_get_irq_reg(struct regmap_irq_chip_data *data,
				       unsigned int base, int index)
{
	/* Simple linear addressing for the main status register */
	if (base == I2C_INTR_STATUS_BASE)
		return base + index;

	return pm8008_periph_base[index] + base;
}

static int pm8008_set_type_config(unsigned int **buf, unsigned int type,
				  const struct regmap_irq *irq_data, int idx,
				  void *irq_drv_data)
{
	switch (type) {
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_LEVEL_LOW:
		buf[POLARITY_HI_INDEX][idx] &= ~irq_data->mask;
		buf[POLARITY_LO_INDEX][idx] |= irq_data->mask;
		break;

	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		buf[POLARITY_HI_INDEX][idx] |= irq_data->mask;
		buf[POLARITY_LO_INDEX][idx] &= ~irq_data->mask;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		buf[POLARITY_HI_INDEX][idx] |= irq_data->mask;
		buf[POLARITY_LO_INDEX][idx] |= irq_data->mask;
		break;

	default:
		return -EINVAL;
	}

	if (type & IRQ_TYPE_EDGE_BOTH)
		buf[SET_TYPE_INDEX][idx] |= irq_data->mask;
	else
		buf[SET_TYPE_INDEX][idx] &= ~irq_data->mask;

	return 0;
}

static struct regmap_irq_chip pm8008_irq_chip = {
	.name			= "pm8008_irq",
	.main_status		= I2C_INTR_STATUS_BASE,
	.num_main_regs		= 1,
	.irqs			= pm8008_irqs,
	.num_irqs		= ARRAY_SIZE(pm8008_irqs),
	.num_regs		= PM8008_NUM_PERIPHS,
	.status_base		= INT_LATCHED_STS_OFFSET,
	.mask_base		= INT_EN_CLR_OFFSET,
	.unmask_base		= INT_EN_SET_OFFSET,
	.mask_unmask_non_inverted = true,
	.ack_base		= INT_LATCHED_CLR_OFFSET,
	.config_base		= pm8008_config_regs,
	.num_config_bases	= ARRAY_SIZE(pm8008_config_regs),
	.num_config_regs	= PM8008_NUM_PERIPHS,
	.set_type_config	= pm8008_set_type_config,
	.get_irq_reg		= pm8008_get_irq_reg,
};

static struct regmap_config qcom_mfd_regmap_cfg = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xFFFF,
};

static int pm8008_probe_irq_peripherals(struct device *dev,
					struct regmap *regmap,
					int client_irq)
{
	int rc, i;
	struct regmap_irq_type *type;
	struct regmap_irq_chip_data *irq_data;

	for (i = 0; i < ARRAY_SIZE(pm8008_irqs); i++) {
		type = &pm8008_irqs[i].type;

		type->type_reg_offset = pm8008_irqs[i].reg_offset;

		if (type->type_reg_offset == PM8008_MISC)
			type->types_supported = IRQ_TYPE_EDGE_RISING;
		else
			type->types_supported = (IRQ_TYPE_EDGE_BOTH |
				IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW);
	}

	rc = devm_regmap_add_irq_chip(dev, regmap, client_irq,
			IRQF_SHARED, 0, &pm8008_irq_chip, &irq_data);
	if (rc) {
		dev_err(dev, "Failed to add IRQ chip: %d\n", rc);
		return rc;
	}

	return 0;
}

static int pm8008_probe(struct i2c_client *client)
{
	int rc;
	struct device *dev;
	struct regmap *regmap;

	dev = &client->dev;
	regmap = devm_regmap_init_i2c(client, &qcom_mfd_regmap_cfg);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	i2c_set_clientdata(client, regmap);

	if (of_property_read_bool(dev->of_node, "interrupt-controller")) {
		rc = pm8008_probe_irq_peripherals(dev, regmap, client->irq);
		if (rc)
			dev_err(dev, "Failed to probe irq periphs: %d\n", rc);
	}

	return devm_of_platform_populate(dev);
}

static const struct of_device_id pm8008_match[] = {
	{ .compatible = "qcom,pm8008", },
	{ },
};

static struct i2c_driver pm8008_mfd_driver = {
	.driver = {
		.name = "pm8008",
		.of_match_table = pm8008_match,
	},
	.probe_new = pm8008_probe,
};
module_i2c_driver(pm8008_mfd_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:qcom-pm8008");
