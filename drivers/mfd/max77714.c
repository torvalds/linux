// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim MAX77714 Core Driver
 *
 * Copyright (C) 2022 Luca Ceresoli
 * Author: Luca Ceresoli <luca@lucaceresoli.net>
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77714.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

static const struct mfd_cell max77714_cells[] = {
	{ .name = "max77714-watchdog" },
	{ .name = "max77714-rtc" },
};

static const struct regmap_range max77714_readable_ranges[] = {
	regmap_reg_range(MAX77714_INT_TOP,     MAX77714_INT_TOP),
	regmap_reg_range(MAX77714_INT_TOPM,    MAX77714_INT_TOPM),
	regmap_reg_range(MAX77714_32K_STATUS,  MAX77714_32K_CONFIG),
	regmap_reg_range(MAX77714_CNFG_GLBL2,  MAX77714_CNFG2_ONOFF),
};

static const struct regmap_range max77714_writable_ranges[] = {
	regmap_reg_range(MAX77714_INT_TOPM,    MAX77714_INT_TOPM),
	regmap_reg_range(MAX77714_32K_CONFIG,  MAX77714_32K_CONFIG),
	regmap_reg_range(MAX77714_CNFG_GLBL2,  MAX77714_CNFG2_ONOFF),
};

static const struct regmap_access_table max77714_readable_table = {
	.yes_ranges = max77714_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max77714_readable_ranges),
};

static const struct regmap_access_table max77714_writable_table = {
	.yes_ranges = max77714_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max77714_writable_ranges),
};

static const struct regmap_config max77714_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77714_CNFG2_ONOFF,
	.rd_table = &max77714_readable_table,
	.wr_table = &max77714_writable_table,
};

static const struct regmap_irq max77714_top_irqs[] = {
	REGMAP_IRQ_REG(MAX77714_IRQ_TOP_ONOFF,   0, MAX77714_INT_TOP_ONOFF),
	REGMAP_IRQ_REG(MAX77714_IRQ_TOP_RTC,     0, MAX77714_INT_TOP_RTC),
	REGMAP_IRQ_REG(MAX77714_IRQ_TOP_GPIO,    0, MAX77714_INT_TOP_GPIO),
	REGMAP_IRQ_REG(MAX77714_IRQ_TOP_LDO,     0, MAX77714_INT_TOP_LDO),
	REGMAP_IRQ_REG(MAX77714_IRQ_TOP_SD,      0, MAX77714_INT_TOP_SD),
	REGMAP_IRQ_REG(MAX77714_IRQ_TOP_GLBL,    0, MAX77714_INT_TOP_GLBL),
};

static const struct regmap_irq_chip max77714_irq_chip = {
	.name			= "max77714-pmic",
	.status_base		= MAX77714_INT_TOP,
	.mask_base		= MAX77714_INT_TOPM,
	.num_regs		= 1,
	.irqs			= max77714_top_irqs,
	.num_irqs		= ARRAY_SIZE(max77714_top_irqs),
};

/*
 * MAX77714 initially uses the internal, low precision oscillator. Enable
 * the external oscillator by setting the XOSC_RETRY bit. If the external
 * oscillator is not OK (probably not installed) this has no effect.
 */
static int max77714_setup_xosc(struct device *dev, struct regmap *regmap)
{
	/* Internal Crystal Load Capacitance, indexed by value of 32KLOAD bits */
	static const unsigned int load_cap[4] = {0, 10, 12, 22}; /* pF */
	unsigned int load_cap_idx;
	unsigned int status;
	int err;

	err = regmap_update_bits(regmap, MAX77714_32K_CONFIG,
				 MAX77714_32K_CONFIG_XOSC_RETRY,
				 MAX77714_32K_CONFIG_XOSC_RETRY);
	if (err)
		return dev_err_probe(dev, err, "Failed to configure the external oscillator\n");

	err = regmap_read(regmap, MAX77714_32K_STATUS, &status);
	if (err)
		return dev_err_probe(dev, err, "Failed to read external oscillator status\n");

	load_cap_idx = (status >> MAX77714_32K_STATUS_32KLOAD_SHF)
		& MAX77714_32K_STATUS_32KLOAD_MSK;

	dev_info(dev, "Using %s oscillator, %d pF load cap\n",
		 status & MAX77714_32K_STATUS_32KSOURCE ? "internal" : "external",
		 load_cap[load_cap_idx]);

	return 0;
}

static int max77714_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regmap *regmap;
	struct regmap_irq_chip_data *irq_data;
	int err;

	regmap = devm_regmap_init_i2c(client, &max77714_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to initialise regmap\n");

	err = max77714_setup_xosc(dev, regmap);
	if (err)
		return err;

	err = devm_regmap_add_irq_chip(dev, regmap, client->irq,
				       IRQF_ONESHOT | IRQF_SHARED, 0,
				       &max77714_irq_chip, &irq_data);
	if (err)
		return dev_err_probe(dev, err, "Failed to add PMIC IRQ chip\n");

	err =  devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
				    max77714_cells, ARRAY_SIZE(max77714_cells),
				    NULL, 0, NULL);
	if (err)
		return dev_err_probe(dev, err, "Failed to register child devices\n");

	return 0;
}

static const struct of_device_id max77714_dt_match[] = {
	{ .compatible = "maxim,max77714" },
	{},
};
MODULE_DEVICE_TABLE(of, max77714_dt_match);

static struct i2c_driver max77714_driver = {
	.driver = {
		.name = "max77714",
		.of_match_table = max77714_dt_match,
	},
	.probe_new = max77714_probe,
};
module_i2c_driver(max77714_driver);

MODULE_DESCRIPTION("Maxim MAX77714 MFD core driver");
MODULE_AUTHOR("Luca Ceresoli <luca@lucaceresoli.net>");
MODULE_LICENSE("GPL");
