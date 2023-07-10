// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 BayLibre SAS
// Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
//
// Core MFD driver for MAXIM 77650/77651 charger/power-supply.
// Programming manual: https://pdfserv.maximintegrated.com/en/an/AN6428.pdf

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77650.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define MAX77650_INT_GPI_F_MSK		BIT(0)
#define MAX77650_INT_GPI_R_MSK		BIT(1)
#define MAX77650_INT_GPI_MSK \
			(MAX77650_INT_GPI_F_MSK | MAX77650_INT_GPI_R_MSK)
#define MAX77650_INT_nEN_F_MSK		BIT(2)
#define MAX77650_INT_nEN_R_MSK		BIT(3)
#define MAX77650_INT_TJAL1_R_MSK	BIT(4)
#define MAX77650_INT_TJAL2_R_MSK	BIT(5)
#define MAX77650_INT_DOD_R_MSK		BIT(6)

#define MAX77650_INT_THM_MSK		BIT(0)
#define MAX77650_INT_CHG_MSK		BIT(1)
#define MAX77650_INT_CHGIN_MSK		BIT(2)
#define MAX77650_INT_TJ_REG_MSK		BIT(3)
#define MAX77650_INT_CHGIN_CTRL_MSK	BIT(4)
#define MAX77650_INT_SYS_CTRL_MSK	BIT(5)
#define MAX77650_INT_SYS_CNFG_MSK	BIT(6)

#define MAX77650_INT_GLBL_OFFSET	0
#define MAX77650_INT_CHG_OFFSET		1

#define MAX77650_SBIA_LPM_MASK		BIT(5)
#define MAX77650_SBIA_LPM_DISABLED	0x00

enum {
	MAX77650_INT_GPI,
	MAX77650_INT_nEN_F,
	MAX77650_INT_nEN_R,
	MAX77650_INT_TJAL1_R,
	MAX77650_INT_TJAL2_R,
	MAX77650_INT_DOD_R,
	MAX77650_INT_THM,
	MAX77650_INT_CHG,
	MAX77650_INT_CHGIN,
	MAX77650_INT_TJ_REG,
	MAX77650_INT_CHGIN_CTRL,
	MAX77650_INT_SYS_CTRL,
	MAX77650_INT_SYS_CNFG,
};

static const struct resource max77650_charger_resources[] = {
	DEFINE_RES_IRQ_NAMED(MAX77650_INT_CHG, "CHG"),
	DEFINE_RES_IRQ_NAMED(MAX77650_INT_CHGIN, "CHGIN"),
};

static const struct resource max77650_gpio_resources[] = {
	DEFINE_RES_IRQ_NAMED(MAX77650_INT_GPI, "GPI"),
};

static const struct resource max77650_onkey_resources[] = {
	DEFINE_RES_IRQ_NAMED(MAX77650_INT_nEN_F, "nEN_F"),
	DEFINE_RES_IRQ_NAMED(MAX77650_INT_nEN_R, "nEN_R"),
};

static const struct mfd_cell max77650_cells[] = {
	{
		.name		= "max77650-regulator",
		.of_compatible	= "maxim,max77650-regulator",
	}, {
		.name		= "max77650-charger",
		.of_compatible	= "maxim,max77650-charger",
		.resources	= max77650_charger_resources,
		.num_resources	= ARRAY_SIZE(max77650_charger_resources),
	}, {
		.name		= "max77650-gpio",
		.of_compatible	= "maxim,max77650-gpio",
		.resources	= max77650_gpio_resources,
		.num_resources	= ARRAY_SIZE(max77650_gpio_resources),
	}, {
		.name		= "max77650-led",
		.of_compatible	= "maxim,max77650-led",
	}, {
		.name		= "max77650-onkey",
		.of_compatible	= "maxim,max77650-onkey",
		.resources	= max77650_onkey_resources,
		.num_resources	= ARRAY_SIZE(max77650_onkey_resources),
	},
};

static const struct regmap_irq max77650_irqs[] = {
	[MAX77650_INT_GPI] = {
		.reg_offset = MAX77650_INT_GLBL_OFFSET,
		.mask = MAX77650_INT_GPI_MSK,
		.type = {
			.type_falling_val = MAX77650_INT_GPI_F_MSK,
			.type_rising_val = MAX77650_INT_GPI_R_MSK,
			.types_supported = IRQ_TYPE_EDGE_BOTH,
		},
	},
	REGMAP_IRQ_REG(MAX77650_INT_nEN_F,
		       MAX77650_INT_GLBL_OFFSET, MAX77650_INT_nEN_F_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_nEN_R,
		       MAX77650_INT_GLBL_OFFSET, MAX77650_INT_nEN_R_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_TJAL1_R,
		       MAX77650_INT_GLBL_OFFSET, MAX77650_INT_TJAL1_R_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_TJAL2_R,
		       MAX77650_INT_GLBL_OFFSET, MAX77650_INT_TJAL2_R_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_DOD_R,
		       MAX77650_INT_GLBL_OFFSET, MAX77650_INT_DOD_R_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_THM,
		       MAX77650_INT_CHG_OFFSET, MAX77650_INT_THM_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_CHG,
		       MAX77650_INT_CHG_OFFSET, MAX77650_INT_CHG_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_CHGIN,
		       MAX77650_INT_CHG_OFFSET, MAX77650_INT_CHGIN_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_TJ_REG,
		       MAX77650_INT_CHG_OFFSET, MAX77650_INT_TJ_REG_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_CHGIN_CTRL,
		       MAX77650_INT_CHG_OFFSET, MAX77650_INT_CHGIN_CTRL_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_SYS_CTRL,
		       MAX77650_INT_CHG_OFFSET, MAX77650_INT_SYS_CTRL_MSK),
	REGMAP_IRQ_REG(MAX77650_INT_SYS_CNFG,
		       MAX77650_INT_CHG_OFFSET, MAX77650_INT_SYS_CNFG_MSK),
};

static const struct regmap_irq_chip max77650_irq_chip = {
	.name			= "max77650-irq",
	.irqs			= max77650_irqs,
	.num_irqs		= ARRAY_SIZE(max77650_irqs),
	.num_regs		= 2,
	.status_base		= MAX77650_REG_INT_GLBL,
	.mask_base		= MAX77650_REG_INTM_GLBL,
	.type_in_mask		= true,
	.init_ack_masked	= true,
	.clear_on_unmask	= true,
};

static const struct regmap_config max77650_regmap_config = {
	.name		= "max77650",
	.reg_bits	= 8,
	.val_bits	= 8,
};

static int max77650_i2c_probe(struct i2c_client *i2c)
{
	struct regmap_irq_chip_data *irq_data;
	struct device *dev = &i2c->dev;
	struct irq_domain *domain;
	struct regmap *map;
	unsigned int val;
	int rv, id;

	map = devm_regmap_init_i2c(i2c, &max77650_regmap_config);
	if (IS_ERR(map)) {
		dev_err(dev, "Unable to initialise I2C Regmap\n");
		return PTR_ERR(map);
	}

	rv = regmap_read(map, MAX77650_REG_CID, &val);
	if (rv) {
		dev_err(dev, "Unable to read Chip ID\n");
		return rv;
	}

	id = MAX77650_CID_BITS(val);
	switch (id) {
	case MAX77650_CID_77650A:
	case MAX77650_CID_77650C:
	case MAX77650_CID_77651A:
	case MAX77650_CID_77651B:
		break;
	default:
		dev_err(dev, "Chip not supported - ID: 0x%02x\n", id);
		return -ENODEV;
	}

	/*
	 * This IC has a low-power mode which reduces the quiescent current
	 * consumption to ~5.6uA but is only suitable for systems consuming
	 * less than ~2mA. Since this is not likely the case even on
	 * linux-based wearables - keep the chip in normal power mode.
	 */
	rv = regmap_update_bits(map,
				MAX77650_REG_CNFG_GLBL,
				MAX77650_SBIA_LPM_MASK,
				MAX77650_SBIA_LPM_DISABLED);
	if (rv) {
		dev_err(dev, "Unable to change the power mode\n");
		return rv;
	}

	rv = devm_regmap_add_irq_chip(dev, map, i2c->irq,
				      IRQF_ONESHOT | IRQF_SHARED, 0,
				      &max77650_irq_chip, &irq_data);
	if (rv) {
		dev_err(dev, "Unable to add Regmap IRQ chip\n");
		return rv;
	}

	domain = regmap_irq_get_domain(irq_data);

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
				    max77650_cells, ARRAY_SIZE(max77650_cells),
				    NULL, 0, domain);
}

static const struct of_device_id max77650_of_match[] = {
	{ .compatible = "maxim,max77650" },
	{ }
};
MODULE_DEVICE_TABLE(of, max77650_of_match);

static struct i2c_driver max77650_i2c_driver = {
	.driver = {
		.name = "max77650",
		.of_match_table = max77650_of_match,
	},
	.probe = max77650_i2c_probe,
};
module_i2c_driver(max77650_i2c_driver);

MODULE_DESCRIPTION("MAXIM 77650/77651 multi-function core driver");
MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_LICENSE("GPL v2");
