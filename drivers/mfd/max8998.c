/*
 * max8998.c - mfd core driver for the Maxim 8998
 *
 *  Copyright (C) 2009-2010 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *  Marek Szyprowski <m.szyprowski@samsung.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max8998.h>
#include <linux/mfd/max8998-private.h>

#define RTC_I2C_ADDR		(0x0c >> 1)

static const struct mfd_cell max8998_devs[] = {
	{
		.name = "max8998-pmic",
	}, {
		.name = "max8998-rtc",
	}, {
		.name = "max8998-battery",
	},
};

static const struct mfd_cell lp3974_devs[] = {
	{
		.name = "lp3974-pmic",
	}, {
		.name = "lp3974-rtc",
	},
};

int max8998_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest)
{
	struct max8998_dev *max8998 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&max8998->iolock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	mutex_unlock(&max8998->iolock);
	if (ret < 0)
		return ret;

	ret &= 0xff;
	*dest = ret;
	return 0;
}
EXPORT_SYMBOL(max8998_read_reg);

int max8998_bulk_read(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	struct max8998_dev *max8998 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&max8998->iolock);
	ret = i2c_smbus_read_i2c_block_data(i2c, reg, count, buf);
	mutex_unlock(&max8998->iolock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(max8998_bulk_read);

int max8998_write_reg(struct i2c_client *i2c, u8 reg, u8 value)
{
	struct max8998_dev *max8998 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&max8998->iolock);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	mutex_unlock(&max8998->iolock);
	return ret;
}
EXPORT_SYMBOL(max8998_write_reg);

int max8998_bulk_write(struct i2c_client *i2c, u8 reg, int count, u8 *buf)
{
	struct max8998_dev *max8998 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&max8998->iolock);
	ret = i2c_smbus_write_i2c_block_data(i2c, reg, count, buf);
	mutex_unlock(&max8998->iolock);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(max8998_bulk_write);

int max8998_update_reg(struct i2c_client *i2c, u8 reg, u8 val, u8 mask)
{
	struct max8998_dev *max8998 = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&max8998->iolock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret >= 0) {
		u8 old_val = ret & 0xff;
		u8 new_val = (val & mask) | (old_val & (~mask));
		ret = i2c_smbus_write_byte_data(i2c, reg, new_val);
	}
	mutex_unlock(&max8998->iolock);
	return ret;
}
EXPORT_SYMBOL(max8998_update_reg);

#ifdef CONFIG_OF
static struct of_device_id max8998_dt_match[] = {
	{ .compatible = "maxim,max8998", .data = (void *)TYPE_MAX8998 },
	{ .compatible = "national,lp3974", .data = (void *)TYPE_LP3974 },
	{ .compatible = "ti,lp3974", .data = (void *)TYPE_LP3974 },
	{},
};
MODULE_DEVICE_TABLE(of, max8998_dt_match);
#endif

/*
 * Only the common platform data elements for max8998 are parsed here from the
 * device tree. Other sub-modules of max8998 such as pmic, rtc and others have
 * to parse their own platform data elements from device tree.
 *
 * The max8998 platform data structure is instantiated here and the drivers for
 * the sub-modules need not instantiate another instance while parsing their
 * platform data.
 */
static struct max8998_platform_data *max8998_i2c_parse_dt_pdata(
							struct device *dev)
{
	struct max8998_platform_data *pd;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	pd->ono = irq_of_parse_and_map(dev->of_node, 1);

	/*
	 * ToDo: the 'wakeup' member in the platform data is more of a linux
	 * specfic information. Hence, there is no binding for that yet and
	 * not parsed here.
	 */
	return pd;
}

static inline unsigned long max8998_i2c_get_driver_data(struct i2c_client *i2c,
						const struct i2c_device_id *id)
{
	if (IS_ENABLED(CONFIG_OF) && i2c->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(max8998_dt_match, i2c->dev.of_node);
		return (unsigned long)match->data;
	}

	return id->driver_data;
}

static int max8998_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct max8998_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct max8998_dev *max8998;
	int ret = 0;

	max8998 = devm_kzalloc(&i2c->dev, sizeof(struct max8998_dev),
				GFP_KERNEL);
	if (max8998 == NULL)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_OF) && i2c->dev.of_node) {
		pdata = max8998_i2c_parse_dt_pdata(&i2c->dev);
		if (IS_ERR(pdata)) {
			ret = PTR_ERR(pdata);
			goto err;
		}
	}

	i2c_set_clientdata(i2c, max8998);
	max8998->dev = &i2c->dev;
	max8998->i2c = i2c;
	max8998->irq = i2c->irq;
	max8998->type = max8998_i2c_get_driver_data(i2c, id);
	max8998->pdata = pdata;
	if (pdata) {
		max8998->ono = pdata->ono;
		max8998->irq_base = pdata->irq_base;
		max8998->wakeup = pdata->wakeup;
	}
	mutex_init(&max8998->iolock);

	max8998->rtc = i2c_new_dummy(i2c->adapter, RTC_I2C_ADDR);
	if (!max8998->rtc) {
		dev_err(&i2c->dev, "Failed to allocate I2C device for RTC\n");
		return -ENODEV;
	}
	i2c_set_clientdata(max8998->rtc, max8998);

	max8998_irq_init(max8998);

	pm_runtime_set_active(max8998->dev);

	switch (max8998->type) {
	case TYPE_LP3974:
		ret = mfd_add_devices(max8998->dev, -1,
				      lp3974_devs, ARRAY_SIZE(lp3974_devs),
				      NULL, 0, NULL);
		break;
	case TYPE_MAX8998:
		ret = mfd_add_devices(max8998->dev, -1,
				      max8998_devs, ARRAY_SIZE(max8998_devs),
				      NULL, 0, NULL);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret < 0)
		goto err;

	device_init_wakeup(max8998->dev, max8998->wakeup);

	return ret;

err:
	mfd_remove_devices(max8998->dev);
	max8998_irq_exit(max8998);
	i2c_unregister_device(max8998->rtc);
	return ret;
}

static int max8998_i2c_remove(struct i2c_client *i2c)
{
	struct max8998_dev *max8998 = i2c_get_clientdata(i2c);

	mfd_remove_devices(max8998->dev);
	max8998_irq_exit(max8998);
	i2c_unregister_device(max8998->rtc);

	return 0;
}

static const struct i2c_device_id max8998_i2c_id[] = {
	{ "max8998", TYPE_MAX8998 },
	{ "lp3974", TYPE_LP3974},
	{ }
};
MODULE_DEVICE_TABLE(i2c, max8998_i2c_id);

static int max8998_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max8998_dev *max8998 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		irq_set_irq_wake(max8998->irq, 1);
	return 0;
}

static int max8998_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max8998_dev *max8998 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		irq_set_irq_wake(max8998->irq, 0);
	/*
	 * In LP3974, if IRQ registers are not "read & clear"
	 * when it's set during sleep, the interrupt becomes
	 * disabled.
	 */
	return max8998_irq_resume(i2c_get_clientdata(i2c));
}

struct max8998_reg_dump {
	u8	addr;
	u8	val;
};
#define SAVE_ITEM(x)	{ .addr = (x), .val = 0x0, }
static struct max8998_reg_dump max8998_dump[] = {
	SAVE_ITEM(MAX8998_REG_IRQM1),
	SAVE_ITEM(MAX8998_REG_IRQM2),
	SAVE_ITEM(MAX8998_REG_IRQM3),
	SAVE_ITEM(MAX8998_REG_IRQM4),
	SAVE_ITEM(MAX8998_REG_STATUSM1),
	SAVE_ITEM(MAX8998_REG_STATUSM2),
	SAVE_ITEM(MAX8998_REG_CHGR1),
	SAVE_ITEM(MAX8998_REG_CHGR2),
	SAVE_ITEM(MAX8998_REG_LDO_ACTIVE_DISCHARGE1),
	SAVE_ITEM(MAX8998_REG_LDO_ACTIVE_DISCHARGE1),
	SAVE_ITEM(MAX8998_REG_BUCK_ACTIVE_DISCHARGE3),
	SAVE_ITEM(MAX8998_REG_ONOFF1),
	SAVE_ITEM(MAX8998_REG_ONOFF2),
	SAVE_ITEM(MAX8998_REG_ONOFF3),
	SAVE_ITEM(MAX8998_REG_ONOFF4),
	SAVE_ITEM(MAX8998_REG_BUCK1_VOLTAGE1),
	SAVE_ITEM(MAX8998_REG_BUCK1_VOLTAGE2),
	SAVE_ITEM(MAX8998_REG_BUCK1_VOLTAGE3),
	SAVE_ITEM(MAX8998_REG_BUCK1_VOLTAGE4),
	SAVE_ITEM(MAX8998_REG_BUCK2_VOLTAGE1),
	SAVE_ITEM(MAX8998_REG_BUCK2_VOLTAGE2),
	SAVE_ITEM(MAX8998_REG_LDO2_LDO3),
	SAVE_ITEM(MAX8998_REG_LDO4),
	SAVE_ITEM(MAX8998_REG_LDO5),
	SAVE_ITEM(MAX8998_REG_LDO6),
	SAVE_ITEM(MAX8998_REG_LDO7),
	SAVE_ITEM(MAX8998_REG_LDO8_LDO9),
	SAVE_ITEM(MAX8998_REG_LDO10_LDO11),
	SAVE_ITEM(MAX8998_REG_LDO12),
	SAVE_ITEM(MAX8998_REG_LDO13),
	SAVE_ITEM(MAX8998_REG_LDO14),
	SAVE_ITEM(MAX8998_REG_LDO15),
	SAVE_ITEM(MAX8998_REG_LDO16),
	SAVE_ITEM(MAX8998_REG_LDO17),
	SAVE_ITEM(MAX8998_REG_BKCHR),
	SAVE_ITEM(MAX8998_REG_LBCNFG1),
	SAVE_ITEM(MAX8998_REG_LBCNFG2),
};
/* Save registers before hibernation */
static int max8998_freeze(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(max8998_dump); i++)
		max8998_read_reg(i2c, max8998_dump[i].addr,
				&max8998_dump[i].val);

	return 0;
}

/* Restore registers after hibernation */
static int max8998_restore(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(max8998_dump); i++)
		max8998_write_reg(i2c, max8998_dump[i].addr,
				max8998_dump[i].val);

	return 0;
}

static const struct dev_pm_ops max8998_pm = {
	.suspend = max8998_suspend,
	.resume = max8998_resume,
	.freeze = max8998_freeze,
	.restore = max8998_restore,
};

static struct i2c_driver max8998_i2c_driver = {
	.driver = {
		   .name = "max8998",
		   .owner = THIS_MODULE,
		   .pm = &max8998_pm,
		   .of_match_table = of_match_ptr(max8998_dt_match),
	},
	.probe = max8998_i2c_probe,
	.remove = max8998_i2c_remove,
	.id_table = max8998_i2c_id,
};

static int __init max8998_i2c_init(void)
{
	return i2c_add_driver(&max8998_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(max8998_i2c_init);

static void __exit max8998_i2c_exit(void)
{
	i2c_del_driver(&max8998_i2c_driver);
}
module_exit(max8998_i2c_exit);

MODULE_DESCRIPTION("MAXIM 8998 multi-function core driver");
MODULE_AUTHOR("Kyungmin Park <kyungmin.park@samsung.com>");
MODULE_LICENSE("GPL");
