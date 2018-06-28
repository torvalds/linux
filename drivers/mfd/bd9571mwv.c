/*
 * ROHM BD9571MWV-M MFD driver
 *
 * Copyright (C) 2017 Marek Vasut <marek.vasut+renesas@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether expressed or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 *
 * Based on the TPS65086 driver
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
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
	.cache_type	= REGCACHE_RBTREE,
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

static int bd9571mwv_identify(struct bd9571mwv *bd)
{
	struct device *dev = bd->dev;
	unsigned int value;
	int ret;

	ret = regmap_read(bd->regmap, BD9571MWV_VENDOR_CODE, &value);
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

	ret = regmap_read(bd->regmap, BD9571MWV_PRODUCT_CODE, &value);
	if (ret) {
		dev_err(dev, "Failed to read product code register (ret=%i)\n",
			ret);
		return ret;
	}

	if (value != BD9571MWV_PRODUCT_CODE_VAL) {
		dev_err(dev, "Invalid product code ID %02x (expected %02x)\n",
			value, BD9571MWV_PRODUCT_CODE_VAL);
		return -EINVAL;
	}

	ret = regmap_read(bd->regmap, BD9571MWV_PRODUCT_REVISION, &value);
	if (ret) {
		dev_err(dev, "Failed to read revision register (ret=%i)\n",
			ret);
		return ret;
	}

	dev_info(dev, "Device: BD9571MWV rev. %d\n", value & 0xff);

	return 0;
}

static int bd9571mwv_probe(struct i2c_client *client,
			  const struct i2c_device_id *ids)
{
	struct bd9571mwv *bd;
	int ret;

	bd = devm_kzalloc(&client->dev, sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	i2c_set_clientdata(client, bd);
	bd->dev = &client->dev;
	bd->irq = client->irq;

	bd->regmap = devm_regmap_init_i2c(client, &bd9571mwv_regmap_config);
	if (IS_ERR(bd->regmap)) {
		dev_err(bd->dev, "Failed to initialize register map\n");
		return PTR_ERR(bd->regmap);
	}

	ret = bd9571mwv_identify(bd);
	if (ret)
		return ret;

	ret = regmap_add_irq_chip(bd->regmap, bd->irq, IRQF_ONESHOT, 0,
				  &bd9571mwv_irq_chip, &bd->irq_data);
	if (ret) {
		dev_err(bd->dev, "Failed to register IRQ chip\n");
		return ret;
	}

	ret = mfd_add_devices(bd->dev, PLATFORM_DEVID_AUTO, bd9571mwv_cells,
			      ARRAY_SIZE(bd9571mwv_cells), NULL, 0,
			      regmap_irq_get_domain(bd->irq_data));
	if (ret) {
		regmap_del_irq_chip(bd->irq, bd->irq_data);
		return ret;
	}

	return 0;
}

static int bd9571mwv_remove(struct i2c_client *client)
{
	struct bd9571mwv *bd = i2c_get_clientdata(client);

	regmap_del_irq_chip(bd->irq, bd->irq_data);

	return 0;
}

static const struct of_device_id bd9571mwv_of_match_table[] = {
	{ .compatible = "rohm,bd9571mwv", },
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
	.remove		= bd9571mwv_remove,
	.id_table       = bd9571mwv_id_table,
};
module_i2c_driver(bd9571mwv_driver);

MODULE_AUTHOR("Marek Vasut <marek.vasut+renesas@gmail.com>");
MODULE_DESCRIPTION("BD9571MWV PMIC Driver");
MODULE_LICENSE("GPL v2");
