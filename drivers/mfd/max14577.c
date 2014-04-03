/*
 * max14577.c - mfd core driver for the Maxim 14577
 *
 * Copyright (C) 2013 Samsung Electrnoics
 * Chanwoo Choi <cw00.choi@samsung.com>
 * Krzysztof Kozlowski <k.kozlowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver is based on max8997.c
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max14577.h>
#include <linux/mfd/max14577-private.h>

static struct mfd_cell max14577_devs[] = {
	{ .name = "max14577-muic", },
	{
		.name = "max14577-regulator",
		.of_compatible = "maxim,max14577-regulator",
	},
	{ .name = "max14577-charger", },
};

static bool max14577_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX14577_REG_INT1 ... MAX14577_REG_STATUS3:
		return true;
	default:
		break;
	}
	return false;
}

static const struct regmap_config max14577_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.volatile_reg	= max14577_volatile_reg,
	.max_register	= MAX14577_REG_END,
};

static const struct regmap_irq max14577_irqs[] = {
	/* INT1 interrupts */
	{ .reg_offset = 0, .mask = INT1_ADC_MASK, },
	{ .reg_offset = 0, .mask = INT1_ADCLOW_MASK, },
	{ .reg_offset = 0, .mask = INT1_ADCERR_MASK, },
	/* INT2 interrupts */
	{ .reg_offset = 1, .mask = INT2_CHGTYP_MASK, },
	{ .reg_offset = 1, .mask = INT2_CHGDETRUN_MASK, },
	{ .reg_offset = 1, .mask = INT2_DCDTMR_MASK, },
	{ .reg_offset = 1, .mask = INT2_DBCHG_MASK, },
	{ .reg_offset = 1, .mask = INT2_VBVOLT_MASK, },
	/* INT3 interrupts */
	{ .reg_offset = 2, .mask = INT3_EOC_MASK, },
	{ .reg_offset = 2, .mask = INT3_CGMBC_MASK, },
	{ .reg_offset = 2, .mask = INT3_OVP_MASK, },
	{ .reg_offset = 2, .mask = INT3_MBCCHGERR_MASK, },
};

static const struct regmap_irq_chip max14577_irq_chip = {
	.name			= "max14577",
	.status_base		= MAX14577_REG_INT1,
	.mask_base		= MAX14577_REG_INTMASK1,
	.mask_invert		= 1,
	.num_regs		= 3,
	.irqs			= max14577_irqs,
	.num_irqs		= ARRAY_SIZE(max14577_irqs),
};

static int max14577_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct max14577 *max14577;
	struct max14577_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct device_node *np = i2c->dev.of_node;
	u8 reg_data;
	int ret = 0;

	if (np) {
		pdata = devm_kzalloc(&i2c->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		i2c->dev.platform_data = pdata;
	}

	if (!pdata) {
		dev_err(&i2c->dev, "No platform data found.\n");
		return -EINVAL;
	}

	max14577 = devm_kzalloc(&i2c->dev, sizeof(*max14577), GFP_KERNEL);
	if (!max14577)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max14577);
	max14577->dev = &i2c->dev;
	max14577->i2c = i2c;
	max14577->irq = i2c->irq;

	max14577->regmap = devm_regmap_init_i2c(i2c, &max14577_regmap_config);
	if (IS_ERR(max14577->regmap)) {
		ret = PTR_ERR(max14577->regmap);
		dev_err(max14577->dev, "Failed to allocate register map: %d\n",
				ret);
		return ret;
	}

	ret = max14577_read_reg(max14577->regmap, MAX14577_REG_DEVICEID,
			&reg_data);
	if (ret) {
		dev_err(max14577->dev, "Device not found on this channel: %d\n",
				ret);
		return ret;
	}
	max14577->vendor_id = ((reg_data & DEVID_VENDORID_MASK) >>
				DEVID_VENDORID_SHIFT);
	max14577->device_id = ((reg_data & DEVID_DEVICEID_MASK) >>
				DEVID_DEVICEID_SHIFT);
	dev_info(max14577->dev, "Device ID: 0x%x, vendor: 0x%x\n",
			max14577->device_id, max14577->vendor_id);

	ret = regmap_add_irq_chip(max14577->regmap, max14577->irq,
				  IRQF_TRIGGER_FALLING | IRQF_ONESHOT, 0,
				  &max14577_irq_chip,
				  &max14577->irq_data);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request IRQ %d: %d\n",
				max14577->irq, ret);
		return ret;
	}

	ret = mfd_add_devices(max14577->dev, -1, max14577_devs,
			ARRAY_SIZE(max14577_devs), NULL, 0,
			regmap_irq_get_domain(max14577->irq_data));
	if (ret < 0)
		goto err_mfd;

	device_init_wakeup(max14577->dev, 1);

	return 0;

err_mfd:
	regmap_del_irq_chip(max14577->irq, max14577->irq_data);

	return ret;
}

static int max14577_i2c_remove(struct i2c_client *i2c)
{
	struct max14577 *max14577 = i2c_get_clientdata(i2c);

	mfd_remove_devices(max14577->dev);
	regmap_del_irq_chip(max14577->irq, max14577->irq_data);

	return 0;
}

static const struct i2c_device_id max14577_i2c_id[] = {
	{ "max14577", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max14577_i2c_id);

#ifdef CONFIG_PM_SLEEP
static int max14577_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max14577 *max14577 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev)) {
		enable_irq_wake(max14577->irq);
		/*
		 * MUIC IRQ must be disabled during suspend if this is
		 * a wake up source because it will be handled before
		 * resuming I2C.
		 *
		 * When device is woken up from suspend (e.g. by ADC change),
		 * an interrupt occurs before resuming I2C bus controller.
		 * Interrupt handler tries to read registers but this read
		 * will fail because I2C is still suspended.
		 */
		disable_irq(max14577->irq);
	}

	return 0;
}

static int max14577_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max14577 *max14577 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev)) {
		disable_irq_wake(max14577->irq);
		enable_irq(max14577->irq);
	}

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static struct of_device_id max14577_dt_match[] = {
	{ .compatible = "maxim,max14577", },
	{},
};

static SIMPLE_DEV_PM_OPS(max14577_pm, max14577_suspend, max14577_resume);

static struct i2c_driver max14577_i2c_driver = {
	.driver = {
		.name = "max14577",
		.owner = THIS_MODULE,
		.pm = &max14577_pm,
		.of_match_table = max14577_dt_match,
	},
	.probe = max14577_i2c_probe,
	.remove = max14577_i2c_remove,
	.id_table = max14577_i2c_id,
};

static int __init max14577_i2c_init(void)
{
	return i2c_add_driver(&max14577_i2c_driver);
}
subsys_initcall(max14577_i2c_init);

static void __exit max14577_i2c_exit(void)
{
	i2c_del_driver(&max14577_i2c_driver);
}
module_exit(max14577_i2c_exit);

MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>, Krzysztof Kozlowski <k.kozlowski@samsung.com>");
MODULE_DESCRIPTION("MAXIM 14577 multi-function core driver");
MODULE_LICENSE("GPL");
