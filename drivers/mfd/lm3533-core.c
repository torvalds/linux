// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lm3533-core.c -- LM3533 Core
 *
 * Copyright (C) 2011-2012 Texas Instruments
 *
 * Author: Johan Hovold <jhovold@gmail.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/mfd/lm3533.h>


#define LM3533_BOOST_OVP_MASK		0x06
#define LM3533_BOOST_OVP_SHIFT		1

#define LM3533_BOOST_FREQ_MASK		0x01
#define LM3533_BOOST_FREQ_SHIFT		0

#define LM3533_BL_ID_MASK		1
#define LM3533_LED_ID_MASK		3
#define LM3533_BL_ID_MAX		1
#define LM3533_LED_ID_MAX		3

#define LM3533_HVLED_ID_MAX		2
#define LM3533_LVLED_ID_MAX		5

#define LM3533_REG_OUTPUT_CONF1		0x10
#define LM3533_REG_OUTPUT_CONF2		0x11
#define LM3533_REG_BOOST_PWM		0x2c

#define LM3533_REG_MAX			0xb2


static struct mfd_cell lm3533_als_devs[] = {
	{
		.name	= "lm3533-als",
		.id	= -1,
	},
};

static struct mfd_cell lm3533_bl_devs[] = {
	{
		.name	= "lm3533-backlight",
		.id	= 0,
	},
	{
		.name	= "lm3533-backlight",
		.id	= 1,
	},
};

static struct mfd_cell lm3533_led_devs[] = {
	{
		.name	= "lm3533-leds",
		.id	= 0,
	},
	{
		.name	= "lm3533-leds",
		.id	= 1,
	},
	{
		.name	= "lm3533-leds",
		.id	= 2,
	},
	{
		.name	= "lm3533-leds",
		.id	= 3,
	},
};

int lm3533_read(struct lm3533 *lm3533, u8 reg, u8 *val)
{
	int tmp;
	int ret;

	ret = regmap_read(lm3533->regmap, reg, &tmp);
	if (ret < 0) {
		dev_err(lm3533->dev, "failed to read register %02x: %d\n",
								reg, ret);
		return ret;
	}

	*val = tmp;

	dev_dbg(lm3533->dev, "read [%02x]: %02x\n", reg, *val);

	return ret;
}
EXPORT_SYMBOL_GPL(lm3533_read);

int lm3533_write(struct lm3533 *lm3533, u8 reg, u8 val)
{
	int ret;

	dev_dbg(lm3533->dev, "write [%02x]: %02x\n", reg, val);

	ret = regmap_write(lm3533->regmap, reg, val);
	if (ret < 0) {
		dev_err(lm3533->dev, "failed to write register %02x: %d\n",
								reg, ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(lm3533_write);

int lm3533_update(struct lm3533 *lm3533, u8 reg, u8 val, u8 mask)
{
	int ret;

	dev_dbg(lm3533->dev, "update [%02x]: %02x/%02x\n", reg, val, mask);

	ret = regmap_update_bits(lm3533->regmap, reg, mask, val);
	if (ret < 0) {
		dev_err(lm3533->dev, "failed to update register %02x: %d\n",
								reg, ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(lm3533_update);

static int lm3533_set_boost_freq(struct lm3533 *lm3533,
						enum lm3533_boost_freq freq)
{
	int ret;

	ret = lm3533_update(lm3533, LM3533_REG_BOOST_PWM,
					freq << LM3533_BOOST_FREQ_SHIFT,
					LM3533_BOOST_FREQ_MASK);
	if (ret)
		dev_err(lm3533->dev, "failed to set boost frequency\n");

	return ret;
}


static int lm3533_set_boost_ovp(struct lm3533 *lm3533,
						enum lm3533_boost_ovp ovp)
{
	int ret;

	ret = lm3533_update(lm3533, LM3533_REG_BOOST_PWM,
					ovp << LM3533_BOOST_OVP_SHIFT,
					LM3533_BOOST_OVP_MASK);
	if (ret)
		dev_err(lm3533->dev, "failed to set boost ovp\n");

	return ret;
}

/*
 * HVLED output config -- output hvled controlled by backlight bl
 */
static int lm3533_set_hvled_config(struct lm3533 *lm3533, u8 hvled, u8 bl)
{
	u8 val;
	u8 mask;
	int shift;
	int ret;

	if (hvled == 0 || hvled > LM3533_HVLED_ID_MAX)
		return -EINVAL;

	if (bl > LM3533_BL_ID_MAX)
		return -EINVAL;

	shift = hvled - 1;
	mask = LM3533_BL_ID_MASK << shift;
	val = bl << shift;

	ret = lm3533_update(lm3533, LM3533_REG_OUTPUT_CONF1, val, mask);
	if (ret)
		dev_err(lm3533->dev, "failed to set hvled config\n");

	return ret;
}

/*
 * LVLED output config -- output lvled controlled by LED led
 */
static int lm3533_set_lvled_config(struct lm3533 *lm3533, u8 lvled, u8 led)
{
	u8 reg;
	u8 val;
	u8 mask;
	int shift;
	int ret;

	if (lvled == 0 || lvled > LM3533_LVLED_ID_MAX)
		return -EINVAL;

	if (led > LM3533_LED_ID_MAX)
		return -EINVAL;

	if (lvled < 4) {
		reg = LM3533_REG_OUTPUT_CONF1;
		shift = 2 * lvled;
	} else {
		reg = LM3533_REG_OUTPUT_CONF2;
		shift = 2 * (lvled - 4);
	}

	mask = LM3533_LED_ID_MASK << shift;
	val = led << shift;

	ret = lm3533_update(lm3533, reg, val, mask);
	if (ret)
		dev_err(lm3533->dev, "failed to set lvled config\n");

	return ret;
}

static void lm3533_enable(struct lm3533 *lm3533)
{
	if (gpio_is_valid(lm3533->gpio_hwen))
		gpio_set_value(lm3533->gpio_hwen, 1);
}

static void lm3533_disable(struct lm3533 *lm3533)
{
	if (gpio_is_valid(lm3533->gpio_hwen))
		gpio_set_value(lm3533->gpio_hwen, 0);
}

enum lm3533_attribute_type {
	LM3533_ATTR_TYPE_BACKLIGHT,
	LM3533_ATTR_TYPE_LED,
};

struct lm3533_device_attribute {
	struct device_attribute dev_attr;
	enum lm3533_attribute_type type;
	union {
		struct {
			u8 id;
		} output;
	} u;
};

#define to_lm3533_dev_attr(_attr) \
	container_of(_attr, struct lm3533_device_attribute, dev_attr)

static ssize_t show_output(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm3533 *lm3533 = dev_get_drvdata(dev);
	struct lm3533_device_attribute *lattr = to_lm3533_dev_attr(attr);
	int id = lattr->u.output.id;
	u8 reg;
	u8 val;
	u8 mask;
	int shift;
	int ret;

	if (lattr->type == LM3533_ATTR_TYPE_BACKLIGHT) {
		reg = LM3533_REG_OUTPUT_CONF1;
		shift = id - 1;
		mask = LM3533_BL_ID_MASK << shift;
	} else {
		if (id < 4) {
			reg = LM3533_REG_OUTPUT_CONF1;
			shift = 2 * id;
		} else {
			reg = LM3533_REG_OUTPUT_CONF2;
			shift = 2 * (id - 4);
		}
		mask = LM3533_LED_ID_MASK << shift;
	}

	ret = lm3533_read(lm3533, reg, &val);
	if (ret)
		return ret;

	val = (val & mask) >> shift;

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t store_output(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct lm3533 *lm3533 = dev_get_drvdata(dev);
	struct lm3533_device_attribute *lattr = to_lm3533_dev_attr(attr);
	int id = lattr->u.output.id;
	u8 val;
	int ret;

	if (kstrtou8(buf, 0, &val))
		return -EINVAL;

	if (lattr->type == LM3533_ATTR_TYPE_BACKLIGHT)
		ret = lm3533_set_hvled_config(lm3533, id, val);
	else
		ret = lm3533_set_lvled_config(lm3533, id, val);

	if (ret)
		return ret;

	return len;
}

#define LM3533_OUTPUT_ATTR(_name, _mode, _show, _store, _type, _id) \
	struct lm3533_device_attribute lm3533_dev_attr_##_name = \
		{ .dev_attr	= __ATTR(_name, _mode, _show, _store), \
		  .type		= _type, \
		  .u.output	= { .id = _id }, }

#define LM3533_OUTPUT_ATTR_RW(_name, _type, _id) \
	LM3533_OUTPUT_ATTR(output_##_name, S_IRUGO | S_IWUSR, \
					show_output, store_output, _type, _id)

#define LM3533_OUTPUT_HVLED_ATTR_RW(_nr) \
	LM3533_OUTPUT_ATTR_RW(hvled##_nr, LM3533_ATTR_TYPE_BACKLIGHT, _nr)
#define LM3533_OUTPUT_LVLED_ATTR_RW(_nr) \
	LM3533_OUTPUT_ATTR_RW(lvled##_nr, LM3533_ATTR_TYPE_LED, _nr)
/*
 * Output config:
 *
 * output_hvled<nr>	0-1
 * output_lvled<nr>	0-3
 */
static LM3533_OUTPUT_HVLED_ATTR_RW(1);
static LM3533_OUTPUT_HVLED_ATTR_RW(2);
static LM3533_OUTPUT_LVLED_ATTR_RW(1);
static LM3533_OUTPUT_LVLED_ATTR_RW(2);
static LM3533_OUTPUT_LVLED_ATTR_RW(3);
static LM3533_OUTPUT_LVLED_ATTR_RW(4);
static LM3533_OUTPUT_LVLED_ATTR_RW(5);

static struct attribute *lm3533_attributes[] = {
	&lm3533_dev_attr_output_hvled1.dev_attr.attr,
	&lm3533_dev_attr_output_hvled2.dev_attr.attr,
	&lm3533_dev_attr_output_lvled1.dev_attr.attr,
	&lm3533_dev_attr_output_lvled2.dev_attr.attr,
	&lm3533_dev_attr_output_lvled3.dev_attr.attr,
	&lm3533_dev_attr_output_lvled4.dev_attr.attr,
	&lm3533_dev_attr_output_lvled5.dev_attr.attr,
	NULL,
};

#define to_dev_attr(_attr) \
	container_of(_attr, struct device_attribute, attr)

static umode_t lm3533_attr_is_visible(struct kobject *kobj,
					     struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct lm3533 *lm3533 = dev_get_drvdata(dev);
	struct device_attribute *dattr = to_dev_attr(attr);
	struct lm3533_device_attribute *lattr = to_lm3533_dev_attr(dattr);
	enum lm3533_attribute_type type = lattr->type;
	umode_t mode = attr->mode;

	if (!lm3533->have_backlights && type == LM3533_ATTR_TYPE_BACKLIGHT)
		mode = 0;
	else if (!lm3533->have_leds && type == LM3533_ATTR_TYPE_LED)
		mode = 0;

	return mode;
};

static struct attribute_group lm3533_attribute_group = {
	.is_visible	= lm3533_attr_is_visible,
	.attrs		= lm3533_attributes
};

static int lm3533_device_als_init(struct lm3533 *lm3533)
{
	struct lm3533_platform_data *pdata = dev_get_platdata(lm3533->dev);
	int ret;

	if (!pdata->als)
		return 0;

	lm3533_als_devs[0].platform_data = pdata->als;
	lm3533_als_devs[0].pdata_size = sizeof(*pdata->als);

	ret = mfd_add_devices(lm3533->dev, 0, lm3533_als_devs, 1, NULL,
			      0, NULL);
	if (ret) {
		dev_err(lm3533->dev, "failed to add ALS device\n");
		return ret;
	}

	lm3533->have_als = 1;

	return 0;
}

static int lm3533_device_bl_init(struct lm3533 *lm3533)
{
	struct lm3533_platform_data *pdata = dev_get_platdata(lm3533->dev);
	int i;
	int ret;

	if (!pdata->backlights || pdata->num_backlights == 0)
		return 0;

	if (pdata->num_backlights > ARRAY_SIZE(lm3533_bl_devs))
		pdata->num_backlights = ARRAY_SIZE(lm3533_bl_devs);

	for (i = 0; i < pdata->num_backlights; ++i) {
		lm3533_bl_devs[i].platform_data = &pdata->backlights[i];
		lm3533_bl_devs[i].pdata_size = sizeof(pdata->backlights[i]);
	}

	ret = mfd_add_devices(lm3533->dev, 0, lm3533_bl_devs,
			      pdata->num_backlights, NULL, 0, NULL);
	if (ret) {
		dev_err(lm3533->dev, "failed to add backlight devices\n");
		return ret;
	}

	lm3533->have_backlights = 1;

	return 0;
}

static int lm3533_device_led_init(struct lm3533 *lm3533)
{
	struct lm3533_platform_data *pdata = dev_get_platdata(lm3533->dev);
	int i;
	int ret;

	if (!pdata->leds || pdata->num_leds == 0)
		return 0;

	if (pdata->num_leds > ARRAY_SIZE(lm3533_led_devs))
		pdata->num_leds = ARRAY_SIZE(lm3533_led_devs);

	for (i = 0; i < pdata->num_leds; ++i) {
		lm3533_led_devs[i].platform_data = &pdata->leds[i];
		lm3533_led_devs[i].pdata_size = sizeof(pdata->leds[i]);
	}

	ret = mfd_add_devices(lm3533->dev, 0, lm3533_led_devs,
			      pdata->num_leds, NULL, 0, NULL);
	if (ret) {
		dev_err(lm3533->dev, "failed to add LED devices\n");
		return ret;
	}

	lm3533->have_leds = 1;

	return 0;
}

static int lm3533_device_setup(struct lm3533 *lm3533,
					struct lm3533_platform_data *pdata)
{
	int ret;

	ret = lm3533_set_boost_freq(lm3533, pdata->boost_freq);
	if (ret)
		return ret;

	return lm3533_set_boost_ovp(lm3533, pdata->boost_ovp);
}

static int lm3533_device_init(struct lm3533 *lm3533)
{
	struct lm3533_platform_data *pdata = dev_get_platdata(lm3533->dev);
	int ret;

	dev_dbg(lm3533->dev, "%s\n", __func__);

	if (!pdata) {
		dev_err(lm3533->dev, "no platform data\n");
		return -EINVAL;
	}

	lm3533->gpio_hwen = pdata->gpio_hwen;

	dev_set_drvdata(lm3533->dev, lm3533);

	if (gpio_is_valid(lm3533->gpio_hwen)) {
		ret = devm_gpio_request_one(lm3533->dev, lm3533->gpio_hwen,
					GPIOF_OUT_INIT_LOW, "lm3533-hwen");
		if (ret < 0) {
			dev_err(lm3533->dev,
				"failed to request HWEN GPIO %d\n",
				lm3533->gpio_hwen);
			return ret;
		}
	}

	lm3533_enable(lm3533);

	ret = lm3533_device_setup(lm3533, pdata);
	if (ret)
		goto err_disable;

	lm3533_device_als_init(lm3533);
	lm3533_device_bl_init(lm3533);
	lm3533_device_led_init(lm3533);

	ret = sysfs_create_group(&lm3533->dev->kobj, &lm3533_attribute_group);
	if (ret < 0) {
		dev_err(lm3533->dev, "failed to create sysfs attributes\n");
		goto err_unregister;
	}

	return 0;

err_unregister:
	mfd_remove_devices(lm3533->dev);
err_disable:
	lm3533_disable(lm3533);

	return ret;
}

static void lm3533_device_exit(struct lm3533 *lm3533)
{
	dev_dbg(lm3533->dev, "%s\n", __func__);

	sysfs_remove_group(&lm3533->dev->kobj, &lm3533_attribute_group);

	mfd_remove_devices(lm3533->dev);
	lm3533_disable(lm3533);
}

static bool lm3533_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x10 ... 0x2c:
	case 0x30 ... 0x38:
	case 0x40 ... 0x45:
	case 0x50 ... 0x57:
	case 0x60 ... 0x6e:
	case 0x70 ... 0x75:
	case 0x80 ... 0x85:
	case 0x90 ... 0x95:
	case 0xa0 ... 0xa5:
	case 0xb0 ... 0xb2:
		return true;
	default:
		return false;
	}
}

static bool lm3533_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x34 ... 0x36:	/* zone */
	case 0x37 ... 0x38:	/* adc */
	case 0xb0 ... 0xb1:	/* fault */
		return true;
	default:
		return false;
	}
}

static bool lm3533_precious_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x34:		/* zone */
		return true;
	default:
		return false;
	}
}

static const struct regmap_config regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= LM3533_REG_MAX,
	.readable_reg	= lm3533_readable_register,
	.volatile_reg	= lm3533_volatile_register,
	.precious_reg	= lm3533_precious_register,
};

static int lm3533_i2c_probe(struct i2c_client *i2c,
					const struct i2c_device_id *id)
{
	struct lm3533 *lm3533;

	dev_dbg(&i2c->dev, "%s\n", __func__);

	lm3533 = devm_kzalloc(&i2c->dev, sizeof(*lm3533), GFP_KERNEL);
	if (!lm3533)
		return -ENOMEM;

	i2c_set_clientdata(i2c, lm3533);

	lm3533->regmap = devm_regmap_init_i2c(i2c, &regmap_config);
	if (IS_ERR(lm3533->regmap))
		return PTR_ERR(lm3533->regmap);

	lm3533->dev = &i2c->dev;
	lm3533->irq = i2c->irq;

	return lm3533_device_init(lm3533);
}

static int lm3533_i2c_remove(struct i2c_client *i2c)
{
	struct lm3533 *lm3533 = i2c_get_clientdata(i2c);

	dev_dbg(&i2c->dev, "%s\n", __func__);

	lm3533_device_exit(lm3533);

	return 0;
}

static const struct i2c_device_id lm3533_i2c_ids[] = {
	{ "lm3533", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, lm3533_i2c_ids);

static struct i2c_driver lm3533_i2c_driver = {
	.driver = {
		   .name = "lm3533",
	},
	.id_table	= lm3533_i2c_ids,
	.probe		= lm3533_i2c_probe,
	.remove		= lm3533_i2c_remove,
};

static int __init lm3533_i2c_init(void)
{
	return i2c_add_driver(&lm3533_i2c_driver);
}
subsys_initcall(lm3533_i2c_init);

static void __exit lm3533_i2c_exit(void)
{
	i2c_del_driver(&lm3533_i2c_driver);
}
module_exit(lm3533_i2c_exit);

MODULE_AUTHOR("Johan Hovold <jhovold@gmail.com>");
MODULE_DESCRIPTION("LM3533 Core");
MODULE_LICENSE("GPL");
