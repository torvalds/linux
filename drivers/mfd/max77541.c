// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022 Analog Devices, Inc.
 * Driver for the MAX77540 and MAX77541
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77541.h>
#include <linux/property.h>
#include <linux/regmap.h>

static const struct regmap_config max77541_regmap_config = {
	.reg_bits   = 8,
	.val_bits   = 8,
};

static const struct regmap_irq max77541_src_irqs[] = {
	{ .mask = MAX77541_BIT_INT_SRC_TOPSYS },
	{ .mask = MAX77541_BIT_INT_SRC_BUCK },
};

static const struct regmap_irq_chip max77541_src_irq_chip = {
	.name		= "max77541-src",
	.status_base	= MAX77541_REG_INT_SRC,
	.mask_base	= MAX77541_REG_INT_SRC_M,
	.num_regs	= 1,
	.irqs		= max77541_src_irqs,
	.num_irqs       = ARRAY_SIZE(max77541_src_irqs),
};

static const struct regmap_irq max77541_topsys_irqs[] = {
	{ .mask = MAX77541_BIT_TOPSYS_INT_TJ_120C },
	{ .mask = MAX77541_BIT_TOPSYS_INT_TJ_140C },
	{ .mask = MAX77541_BIT_TOPSYS_INT_TSHDN },
	{ .mask = MAX77541_BIT_TOPSYS_INT_UVLO },
	{ .mask = MAX77541_BIT_TOPSYS_INT_ALT_SWO },
	{ .mask = MAX77541_BIT_TOPSYS_INT_EXT_FREQ_DET },
};

static const struct regmap_irq_chip max77541_topsys_irq_chip = {
	.name		= "max77541-topsys",
	.status_base	= MAX77541_REG_TOPSYS_INT,
	.mask_base	= MAX77541_REG_TOPSYS_INT_M,
	.num_regs	= 1,
	.irqs		= max77541_topsys_irqs,
	.num_irqs	= ARRAY_SIZE(max77541_topsys_irqs),
};

static const struct regmap_irq max77541_buck_irqs[] = {
	{ .mask = MAX77541_BIT_BUCK_INT_M1_POK_FLT },
	{ .mask = MAX77541_BIT_BUCK_INT_M2_POK_FLT },
	{ .mask = MAX77541_BIT_BUCK_INT_M1_SCFLT },
	{ .mask = MAX77541_BIT_BUCK_INT_M2_SCFLT },
};

static const struct regmap_irq_chip max77541_buck_irq_chip = {
	.name		= "max77541-buck",
	.status_base	= MAX77541_REG_BUCK_INT,
	.mask_base	= MAX77541_REG_BUCK_INT_M,
	.num_regs	= 1,
	.irqs		= max77541_buck_irqs,
	.num_irqs	= ARRAY_SIZE(max77541_buck_irqs),
};

static const struct regmap_irq max77541_adc_irqs[] = {
	{ .mask = MAX77541_BIT_ADC_INT_CH1_I },
	{ .mask = MAX77541_BIT_ADC_INT_CH2_I },
	{ .mask = MAX77541_BIT_ADC_INT_CH3_I },
	{ .mask = MAX77541_BIT_ADC_INT_CH6_I },
};

static const struct regmap_irq_chip max77541_adc_irq_chip = {
	.name		= "max77541-adc",
	.status_base	= MAX77541_REG_ADC_INT,
	.mask_base	= MAX77541_REG_ADC_INT_M,
	.num_regs	= 1,
	.irqs		= max77541_adc_irqs,
	.num_irqs	= ARRAY_SIZE(max77541_adc_irqs),
};

static const struct mfd_cell max77540_devs[] = {
	MFD_CELL_OF("max77540-regulator", NULL, NULL, 0, 0, NULL),
};

static const struct mfd_cell max77541_devs[] = {
	MFD_CELL_OF("max77541-regulator", NULL, NULL, 0, 0, NULL),
	MFD_CELL_OF("max77541-adc", NULL, NULL, 0, 0, NULL),
};

static int max77541_pmic_irq_init(struct device *dev)
{
	struct max77541 *max77541 = dev_get_drvdata(dev);
	int irq = max77541->i2c->irq;
	int ret;

	ret = devm_regmap_add_irq_chip(dev, max77541->regmap, irq,
				       IRQF_ONESHOT | IRQF_SHARED, 0,
				       &max77541_src_irq_chip,
				       &max77541->irq_data);
	if (ret)
		return ret;

	ret = devm_regmap_add_irq_chip(dev, max77541->regmap, irq,
				       IRQF_ONESHOT | IRQF_SHARED, 0,
				       &max77541_topsys_irq_chip,
				       &max77541->irq_topsys);
	if (ret)
		return ret;

	ret = devm_regmap_add_irq_chip(dev, max77541->regmap, irq,
				       IRQF_ONESHOT | IRQF_SHARED, 0,
				       &max77541_buck_irq_chip,
				       &max77541->irq_buck);
	if (ret)
		return ret;

	if (max77541->id == MAX77541) {
		ret = devm_regmap_add_irq_chip(dev, max77541->regmap, irq,
					       IRQF_ONESHOT | IRQF_SHARED, 0,
					       &max77541_adc_irq_chip,
					       &max77541->irq_adc);
		if (ret)
			return ret;
	}

	return 0;
}

static int max77541_pmic_setup(struct device *dev)
{
	struct max77541 *max77541 = dev_get_drvdata(dev);
	const struct mfd_cell *cells;
	int n_devs;
	int ret;

	switch (max77541->id) {
	case MAX77540:
		cells =  max77540_devs;
		n_devs = ARRAY_SIZE(max77540_devs);
		break;
	case MAX77541:
		cells =  max77541_devs;
		n_devs = ARRAY_SIZE(max77541_devs);
		break;
	default:
		return -EINVAL;
	}

	ret = max77541_pmic_irq_init(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize IRQ\n");

	ret = device_init_wakeup(dev, true);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to init wakeup\n");

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
				    cells, n_devs, NULL, 0, NULL);
}

static int max77541_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct device *dev = &client->dev;
	struct max77541 *max77541;

	max77541 = devm_kzalloc(dev, sizeof(*max77541), GFP_KERNEL);
	if (!max77541)
		return -ENOMEM;

	i2c_set_clientdata(client, max77541);
	max77541->i2c = client;

	max77541->id = (uintptr_t)device_get_match_data(dev);
	if (!max77541->id)
		max77541->id  = (enum max7754x_ids)id->driver_data;

	if (!max77541->id)
		return -EINVAL;

	max77541->regmap = devm_regmap_init_i2c(client,
						&max77541_regmap_config);
	if (IS_ERR(max77541->regmap))
		return dev_err_probe(dev, PTR_ERR(max77541->regmap),
				     "Failed to allocate register map\n");

	return max77541_pmic_setup(dev);
}

static const struct of_device_id max77541_of_id[] = {
	{
		.compatible = "adi,max77540",
		.data = (void *)MAX77540,
	},
	{
		.compatible = "adi,max77541",
		.data = (void *)MAX77541,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, max77541_of_id);

static const struct i2c_device_id max77541_id[] = {
	{ "max77540", MAX77540 },
	{ "max77541", MAX77541 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max77541_id);

static struct i2c_driver max77541_driver = {
	.driver = {
		.name = "max77541",
		.of_match_table = max77541_of_id,
	},
	.probe = max77541_probe,
	.id_table = max77541_id,
};
module_i2c_driver(max77541_driver);

MODULE_DESCRIPTION("MAX7740/MAX7741 Driver");
MODULE_AUTHOR("Okan Sahin <okan.sahin@analog.com>");
MODULE_LICENSE("GPL");
