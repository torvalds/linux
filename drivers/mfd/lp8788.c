/*
 * TI LP8788 MFD - core interface
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/lp8788.h>
#include <linux/module.h>
#include <linux/slab.h>

#define MAX_LP8788_REGISTERS		0xA2

#define MFD_DEV_SIMPLE(_name)					\
{								\
	.name = LP8788_DEV_##_name,				\
}

#define MFD_DEV_WITH_ID(_name, _id)				\
{								\
	.name = LP8788_DEV_##_name,				\
	.id = _id,						\
}

#define MFD_DEV_WITH_RESOURCE(_name, _resource, num_resource)	\
{								\
	.name = LP8788_DEV_##_name,				\
	.resources = _resource,					\
	.num_resources = num_resource,				\
}

static struct resource chg_irqs[] = {
	/* Charger Interrupts */
	{
		.start = LP8788_INT_CHG_INPUT_STATE,
		.end   = LP8788_INT_PRECHG_TIMEOUT,
		.name  = LP8788_CHG_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	/* Power Routing Switch Interrupts */
	{
		.start = LP8788_INT_ENTER_SYS_SUPPORT,
		.end   = LP8788_INT_EXIT_SYS_SUPPORT,
		.name  = LP8788_PRSW_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	/* Battery Interrupts */
	{
		.start = LP8788_INT_BATT_LOW,
		.end   = LP8788_INT_NO_BATT,
		.name  = LP8788_BATT_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource rtc_irqs[] = {
	{
		.start = LP8788_INT_RTC_ALARM1,
		.end   = LP8788_INT_RTC_ALARM2,
		.name  = LP8788_ALM_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell lp8788_devs[] = {
	/* 4 bucks */
	MFD_DEV_WITH_ID(BUCK, 1),
	MFD_DEV_WITH_ID(BUCK, 2),
	MFD_DEV_WITH_ID(BUCK, 3),
	MFD_DEV_WITH_ID(BUCK, 4),

	/* 12 digital ldos */
	MFD_DEV_WITH_ID(DLDO, 1),
	MFD_DEV_WITH_ID(DLDO, 2),
	MFD_DEV_WITH_ID(DLDO, 3),
	MFD_DEV_WITH_ID(DLDO, 4),
	MFD_DEV_WITH_ID(DLDO, 5),
	MFD_DEV_WITH_ID(DLDO, 6),
	MFD_DEV_WITH_ID(DLDO, 7),
	MFD_DEV_WITH_ID(DLDO, 8),
	MFD_DEV_WITH_ID(DLDO, 9),
	MFD_DEV_WITH_ID(DLDO, 10),
	MFD_DEV_WITH_ID(DLDO, 11),
	MFD_DEV_WITH_ID(DLDO, 12),

	/* 10 analog ldos */
	MFD_DEV_WITH_ID(ALDO, 1),
	MFD_DEV_WITH_ID(ALDO, 2),
	MFD_DEV_WITH_ID(ALDO, 3),
	MFD_DEV_WITH_ID(ALDO, 4),
	MFD_DEV_WITH_ID(ALDO, 5),
	MFD_DEV_WITH_ID(ALDO, 6),
	MFD_DEV_WITH_ID(ALDO, 7),
	MFD_DEV_WITH_ID(ALDO, 8),
	MFD_DEV_WITH_ID(ALDO, 9),
	MFD_DEV_WITH_ID(ALDO, 10),

	/* ADC */
	MFD_DEV_SIMPLE(ADC),

	/* battery charger */
	MFD_DEV_WITH_RESOURCE(CHARGER, chg_irqs, ARRAY_SIZE(chg_irqs)),

	/* rtc */
	MFD_DEV_WITH_RESOURCE(RTC, rtc_irqs, ARRAY_SIZE(rtc_irqs)),

	/* backlight */
	MFD_DEV_SIMPLE(BACKLIGHT),

	/* current sink for vibrator */
	MFD_DEV_SIMPLE(VIBRATOR),

	/* current sink for keypad LED */
	MFD_DEV_SIMPLE(KEYLED),
};

int lp8788_read_byte(struct lp8788 *lp, u8 reg, u8 *data)
{
	int ret;
	unsigned int val;

	ret = regmap_read(lp->regmap, reg, &val);
	if (ret < 0) {
		dev_err(lp->dev, "failed to read 0x%.2x\n", reg);
		return ret;
	}

	*data = (u8)val;
	return 0;
}
EXPORT_SYMBOL_GPL(lp8788_read_byte);

int lp8788_read_multi_bytes(struct lp8788 *lp, u8 reg, u8 *data, size_t count)
{
	return regmap_bulk_read(lp->regmap, reg, data, count);
}
EXPORT_SYMBOL_GPL(lp8788_read_multi_bytes);

int lp8788_write_byte(struct lp8788 *lp, u8 reg, u8 data)
{
	return regmap_write(lp->regmap, reg, data);
}
EXPORT_SYMBOL_GPL(lp8788_write_byte);

int lp8788_update_bits(struct lp8788 *lp, u8 reg, u8 mask, u8 data)
{
	return regmap_update_bits(lp->regmap, reg, mask, data);
}
EXPORT_SYMBOL_GPL(lp8788_update_bits);

static int lp8788_platform_init(struct lp8788 *lp)
{
	struct lp8788_platform_data *pdata = lp->pdata;

	return (pdata && pdata->init_func) ? pdata->init_func(lp) : 0;
}

static const struct regmap_config lp8788_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX_LP8788_REGISTERS,
};

static int lp8788_probe(struct i2c_client *cl, const struct i2c_device_id *id)
{
	struct lp8788 *lp;
	struct lp8788_platform_data *pdata = cl->dev.platform_data;
	int ret;

	lp = devm_kzalloc(&cl->dev, sizeof(struct lp8788), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	lp->regmap = devm_regmap_init_i2c(cl, &lp8788_regmap_config);
	if (IS_ERR(lp->regmap)) {
		ret = PTR_ERR(lp->regmap);
		dev_err(&cl->dev, "regmap init i2c err: %d\n", ret);
		return ret;
	}

	lp->pdata = pdata;
	lp->dev = &cl->dev;
	i2c_set_clientdata(cl, lp);

	ret = lp8788_platform_init(lp);
	if (ret)
		return ret;

	ret = lp8788_irq_init(lp, cl->irq);
	if (ret)
		return ret;

	return mfd_add_devices(lp->dev, -1, lp8788_devs,
			       ARRAY_SIZE(lp8788_devs), NULL, 0, NULL);
}

static int lp8788_remove(struct i2c_client *cl)
{
	struct lp8788 *lp = i2c_get_clientdata(cl);

	mfd_remove_devices(lp->dev);
	lp8788_irq_exit(lp);
	return 0;
}

static const struct i2c_device_id lp8788_ids[] = {
	{"lp8788", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp8788_ids);

static struct i2c_driver lp8788_driver = {
	.driver = {
		.name = "lp8788",
		.owner = THIS_MODULE,
	},
	.probe = lp8788_probe,
	.remove = lp8788_remove,
	.id_table = lp8788_ids,
};

static int __init lp8788_init(void)
{
	return i2c_add_driver(&lp8788_driver);
}
subsys_initcall(lp8788_init);

static void __exit lp8788_exit(void)
{
	i2c_del_driver(&lp8788_driver);
}
module_exit(lp8788_exit);

MODULE_DESCRIPTION("TI LP8788 MFD Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
