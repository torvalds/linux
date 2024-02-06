// SPDX-License-Identifier: GPL-2.0-only
/*
 * ROHM BD9571MWV-M and BD9574MVF-M core driver
 *
 * Copyright (C) 2017 Marek Vasut <marek.vasut+renesas@gmail.com>
 * Copyright (C) 2020 Renesas Electronics Corporation
 *
 * Based on the TPS65086 driver
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rohm-generic.h>
#include <linux/module.h>

#include <linux/mfd/bd9571mwv.h>

static const struct mfd_cell bd9571mwv_cells[] = {
	{ .name = "bd9571mwv-regulator", },
	{ .name = "bd9571mwv-gpio", },
};

static const struct regmap_range bd9571mwv_readable_yes_ranges[] = {
	regmap_reg_range(BD9571MWV_VENDOR_CODE, BD9571MWV_PRODUCT_REVISION),
	regmap_reg_range(BD9571MWV_BKUP_MODE_CNT, BD9571MWV_BKUP_MODE_CNT),
	regmap_reg_range(BD9571MWV_AVS_SET_MONI, BD9571MWV_AVS_DVFS_VID(3)),
	regmap_reg_range(BD9571MWV_VD18_VID, BD9571MWV_VD33_VID),
	regmap_reg_range(BD9571MWV_DVFS_VINIT, BD9571MWV_DVFS_VINIT),
	regmap_reg_range(BD9571MWV_DVFS_SETVMAX, BD9571MWV_DVFS_MONIVDAC),
	regmap_reg_range(BD9571MWV_GPIO_IN, BD9571MWV_GPIO_IN),
	regmap_reg_range(BD9571MWV_GPIO_INT, BD9571MWV_GPIO_INTMASK),
	regmap_reg_range(BD9571MWV_INT_INTREQ, BD9571MWV_INT_INTMASK),
};

static const struct regmap_access_table bd9571mwv_readable_table = {
	.yes_ranges	= bd9571mwv_readable_yes_ranges,
	.n_yes_ranges	= ARRAY_SIZE(bd9571mwv_readable_yes_ranges),
};

static const struct regmap_range bd9571mwv_writable_yes_ranges[] = {
	regmap_reg_range(BD9571MWV_BKUP_MODE_CNT, BD9571MWV_BKUP_MODE_CNT),
	regmap_reg_range(BD9571MWV_AVS_VD09_VID(0), BD9571MWV_AVS_VD09_VID(3)),
	regmap_reg_range(BD9571MWV_DVFS_SETVID, BD9571MWV_DVFS_SETVID),
	regmap_reg_range(BD9571MWV_GPIO_DIR, BD9571MWV_GPIO_OUT),
	regmap_reg_range(BD9571MWV_GPIO_INT_SET, BD9571MWV_GPIO_INTMASK),
	regmap_reg_range(BD9571MWV_INT_INTREQ, BD9571MWV_INT_INTMASK),
};

static const struct regmap_access_table bd9571mwv_writable_table = {
	.yes_ranges	= bd9571mwv_writable_yes_ranges,
	.n_yes_ranges	= ARRAY_SIZE(bd9571mwv_writable_yes_ranges),
};

static const struct regmap_range bd9571mwv_volatile_yes_ranges[] = {
	regmap_reg_range(BD9571MWV_DVFS_MONIVDAC, BD9571MWV_DVFS_MONIVDAC),
	regmap_reg_range(BD9571MWV_GPIO_IN, BD9571MWV_GPIO_IN),
	regmap_reg_range(BD9571MWV_GPIO_INT, BD9571MWV_GPIO_INT),
	regmap_reg_range(BD9571MWV_INT_INTREQ, BD9571MWV_INT_INTREQ),
};

static const struct regmap_access_table bd9571mwv_volatile_table = {
	.yes_ranges	= bd9571mwv_volatile_yes_ranges,
	.n_yes_ranges	= ARRAY_SIZE(bd9571mwv_volatile_yes_ranges),
};

static const struct regmap_config bd9571mwv_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.cache_type	= REGCACHE_MAPLE,
	.rd_table	= &bd9571mwv_readable_table,
	.wr_table	= &bd9571mwv_writable_table,
	.volatile_table	= &bd9571mwv_volatile_table,
	.max_register	= 0xff,
};

static const struct regmap_irq bd9571mwv_irqs[] = {
	REGMAP_IRQ_REG(BD9571MWV_IRQ_MD1, 0,
		       BD9571MWV_INT_INTREQ_MD1_INT),
	REGMAP_IRQ_REG(BD9571MWV_IRQ_MD2_E1, 0,
		       BD9571MWV_INT_INTREQ_MD2_E1_INT),
	REGMAP_IRQ_REG(BD9571MWV_IRQ_MD2_E2, 0,
		       BD9571MWV_INT_INTREQ_MD2_E2_INT),
	REGMAP_IRQ_REG(BD9571MWV_IRQ_PROT_ERR, 0,
		       BD9571MWV_INT_INTREQ_PROT_ERR_INT),
	REGMAP_IRQ_REG(BD9571MWV_IRQ_GP, 0,
		       BD9571MWV_INT_INTREQ_GP_INT),
	REGMAP_IRQ_REG(BD9571MWV_IRQ_128H_OF, 0,
		       BD9571MWV_INT_INTREQ_128H_OF_INT),
	REGMAP_IRQ_REG(BD9571MWV_IRQ_WDT_OF, 0,
		       BD9571MWV_INT_INTREQ_WDT_OF_INT),
	REGMAP_IRQ_REG(BD9571MWV_IRQ_BKUP_TRG, 0,
		       BD9571MWV_INT_INTREQ_BKUP_TRG_INT),
};

static struct regmap_irq_chip bd9571mwv_irq_chip = {
	.name		= "bd9571mwv",
	.status_base	= BD9571MWV_INT_INTREQ,
	.mask_base	= BD9571MWV_INT_INTMASK,
	.ack_base	= BD9571MWV_INT_INTREQ,
	.init_ack_masked = true,
	.num_regs	= 1,
	.irqs		= bd9571mwv_irqs,
	.num_irqs	= ARRAY_SIZE(bd9571mwv_irqs),
};

static const struct mfd_cell bd9574mwf_cells[] = {
	{ .name = "bd9574mwf-regulator", },
	{ .name = "bd9574mwf-gpio", },
};

static const struct regmap_range bd9574mwf_readable_yes_ranges[] = {
	regmap_reg_range(BD9571MWV_VENDOR_CODE, BD9571MWV_PRODUCT_REVISION),
	regmap_reg_range(BD9571MWV_BKUP_MODE_CNT, BD9571MWV_BKUP_MODE_CNT),
	regmap_reg_range(BD9571MWV_DVFS_VINIT, BD9571MWV_DVFS_SETVMAX),
	regmap_reg_range(BD9571MWV_DVFS_SETVID, BD9571MWV_DVFS_MONIVDAC),
	regmap_reg_range(BD9571MWV_GPIO_IN, BD9571MWV_GPIO_IN),
	regmap_reg_range(BD9571MWV_GPIO_INT, BD9571MWV_GPIO_INTMASK),
	regmap_reg_range(BD9571MWV_INT_INTREQ, BD9571MWV_INT_INTMASK),
};

static const struct regmap_access_table bd9574mwf_readable_table = {
	.yes_ranges	= bd9574mwf_readable_yes_ranges,
	.n_yes_ranges	= ARRAY_SIZE(bd9574mwf_readable_yes_ranges),
};

static const struct regmap_range bd9574mwf_writable_yes_ranges[] = {
	regmap_reg_range(BD9571MWV_BKUP_MODE_CNT, BD9571MWV_BKUP_MODE_CNT),
	regmap_reg_range(BD9571MWV_DVFS_SETVID, BD9571MWV_DVFS_SETVID),
	regmap_reg_range(BD9571MWV_GPIO_DIR, BD9571MWV_GPIO_OUT),
	regmap_reg_range(BD9571MWV_GPIO_INT_SET, BD9571MWV_GPIO_INTMASK),
	regmap_reg_range(BD9571MWV_INT_INTREQ, BD9571MWV_INT_INTMASK),
};

static const struct regmap_access_table bd9574mwf_writable_table = {
	.yes_ranges	= bd9574mwf_writable_yes_ranges,
	.n_yes_ranges	= ARRAY_SIZE(bd9574mwf_writable_yes_ranges),
};

static const struct regmap_range bd9574mwf_volatile_yes_ranges[] = {
	regmap_reg_range(BD9571MWV_DVFS_MONIVDAC, BD9571MWV_DVFS_MONIVDAC),
	regmap_reg_range(BD9571MWV_GPIO_IN, BD9571MWV_GPIO_IN),
	regmap_reg_range(BD9571MWV_GPIO_INT, BD9571MWV_GPIO_INT),
	regmap_reg_range(BD9571MWV_INT_INTREQ, BD9571MWV_INT_INTREQ),
};

static const struct regmap_access_table bd9574mwf_volatile_table = {
	.yes_ranges	= bd9574mwf_volatile_yes_ranges,
	.n_yes_ranges	= ARRAY_SIZE(bd9574mwf_volatile_yes_ranges),
};

static const struct regmap_config bd9574mwf_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.cache_type	= REGCACHE_MAPLE,
	.rd_table	= &bd9574mwf_readable_table,
	.wr_table	= &bd9574mwf_writable_table,
	.volatile_table	= &bd9574mwf_volatile_table,
	.max_register	= 0xff,
};

static struct regmap_irq_chip bd9574mwf_irq_chip = {
	.name		= "bd9574mwf",
	.status_base	= BD9571MWV_INT_INTREQ,
	.mask_base	= BD9571MWV_INT_INTMASK,
	.ack_base	= BD9571MWV_INT_INTREQ,
	.init_ack_masked = true,
	.num_regs	= 1,
	.irqs		= bd9571mwv_irqs,
	.num_irqs	= ARRAY_SIZE(bd9571mwv_irqs),
};

static int bd957x_identify(struct device *dev, struct regmap *regmap)
{
	unsigned int value;
	int ret;

	ret = regmap_read(regmap, BD9571MWV_VENDOR_CODE, &value);
	if (ret) {
		dev_err(dev, "Failed to read vendor code register (ret=%i)\n",
			ret);
		return ret;
	}

	if (value != BD9571MWV_VENDOR_CODE_VAL) {
		dev_err(dev, "Invalid vendor code ID %02x (expected %02x)\n",
			value, BD9571MWV_VENDOR_CODE_VAL);
		return -EINVAL;
	}

	ret = regmap_read(regmap, BD9571MWV_PRODUCT_CODE, &value);
	if (ret) {
		dev_err(dev, "Failed to read product code register (ret=%i)\n",
			ret);
		return ret;
	}
	ret = regmap_read(regmap, BD9571MWV_PRODUCT_REVISION, &value);
	if (ret) {
		dev_err(dev, "Failed to read revision register (ret=%i)\n",
			ret);
		return ret;
	}

	return 0;
}

static int bd9571mwv_probe(struct i2c_client *client)
{
	const struct regmap_config *regmap_config;
	const struct regmap_irq_chip *irq_chip;
	const struct mfd_cell *cells;
	struct device *dev = &client->dev;
	struct regmap *regmap;
	struct regmap_irq_chip_data *irq_data;
	int ret, num_cells, irq = client->irq;

	/* Read the PMIC product code */
	ret = i2c_smbus_read_byte_data(client, BD9571MWV_PRODUCT_CODE);
	if (ret < 0) {
		dev_err(dev, "Failed to read product code\n");
		return ret;
	}

	switch (ret) {
	case BD9571MWV_PRODUCT_CODE_BD9571MWV:
		regmap_config = &bd9571mwv_regmap_config;
		irq_chip = &bd9571mwv_irq_chip;
		cells = bd9571mwv_cells;
		num_cells = ARRAY_SIZE(bd9571mwv_cells);
		break;
	case BD9571MWV_PRODUCT_CODE_BD9574MWF:
		regmap_config = &bd9574mwf_regmap_config;
		irq_chip = &bd9574mwf_irq_chip;
		cells = bd9574mwf_cells;
		num_cells = ARRAY_SIZE(bd9574mwf_cells);
		break;
	default:
		dev_err(dev, "Unsupported device 0x%x\n", ret);
		return -ENODEV;
	}

	regmap = devm_regmap_init_i2c(client, regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize register map\n");
		return PTR_ERR(regmap);
	}

	ret = bd957x_identify(dev, regmap);
	if (ret)
		return ret;

	ret = devm_regmap_add_irq_chip(dev, regmap, irq, IRQF_ONESHOT, 0,
				       irq_chip, &irq_data);
	if (ret) {
		dev_err(dev, "Failed to register IRQ chip\n");
		return ret;
	}

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, cells, num_cells,
				    NULL, 0, regmap_irq_get_domain(irq_data));
}

static const struct of_device_id bd9571mwv_of_match_table[] = {
	{ .compatible = "rohm,bd9571mwv", },
	{ .compatible = "rohm,bd9574mwf", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bd9571mwv_of_match_table);

static const struct i2c_device_id bd9571mwv_id_table[] = {
	{ "bd9571mwv", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, bd9571mwv_id_table);

static struct i2c_driver bd9571mwv_driver = {
	.driver		= {
		.name	= "bd9571mwv",
		.of_match_table = bd9571mwv_of_match_table,
	},
	.probe		= bd9571mwv_probe,
	.id_table       = bd9571mwv_id_table,
};
module_i2c_driver(bd9571mwv_driver);

MODULE_AUTHOR("Marek Vasut <marek.vasut+renesas@gmail.com>");
MODULE_DESCRIPTION("BD9571MWV PMIC Driver");
MODULE_LICENSE("GPL v2");
