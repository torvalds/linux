// SPDX-License-Identifier: GPL-2.0
/*
 * The Gateworks System Controller (GSC) is a multi-function
 * device designed for use in Gateworks Single Board Computers.
 * The control interface is I2C, with an interrupt. The device supports
 * system functions such as push-button monitoring, multiple ADC's for
 * voltage and temperature monitoring, fan controller and watchdog monitor.
 *
 * Copyright (C) 2020 Gateworks Corporation
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/gsc.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <asm/unaligned.h>

/*
 * The GSC suffers from an errata where occasionally during
 * ADC cycles the chip can NAK I2C transactions. To ensure we have reliable
 * register access we place retries around register access.
 */
#define I2C_RETRIES	3

int gsc_write(void *context, unsigned int reg, unsigned int val)
{
	struct i2c_client *client = context;
	int retry, ret;

	for (retry = 0; retry < I2C_RETRIES; retry++) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		/*
		 * -EAGAIN returned when the i2c host controller is busy
		 * -EIO returned when i2c device is busy
		 */
		if (ret != -EAGAIN && ret != -EIO)
			break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gsc_write);

int gsc_read(void *context, unsigned int reg, unsigned int *val)
{
	struct i2c_client *client = context;
	int retry, ret;

	for (retry = 0; retry < I2C_RETRIES; retry++) {
		ret = i2c_smbus_read_byte_data(client, reg);
		/*
		 * -EAGAIN returned when the i2c host controller is busy
		 * -EIO returned when i2c device is busy
		 */
		if (ret != -EAGAIN && ret != -EIO)
			break;
	}
	*val = ret & 0xff;

	return 0;
}
EXPORT_SYMBOL_GPL(gsc_read);

/*
 * gsc_powerdown - API to use GSC to power down board for a specific time
 *
 * secs - number of seconds to remain powered off
 */
static int gsc_powerdown(struct gsc_dev *gsc, unsigned long secs)
{
	int ret;
	unsigned char regs[4];

	dev_info(&gsc->i2c->dev, "GSC powerdown for %ld seconds\n",
		 secs);

	put_unaligned_le32(secs, regs);
	ret = regmap_bulk_write(gsc->regmap, GSC_TIME_ADD, regs, 4);
	if (ret)
		return ret;

	ret = regmap_update_bits(gsc->regmap, GSC_CTRL_1,
				 BIT(GSC_CTRL_1_SLEEP_ADD),
				 BIT(GSC_CTRL_1_SLEEP_ADD));
	if (ret)
		return ret;

	ret = regmap_update_bits(gsc->regmap, GSC_CTRL_1,
				 BIT(GSC_CTRL_1_SLEEP_ACTIVATE) |
				 BIT(GSC_CTRL_1_SLEEP_ENABLE),
				 BIT(GSC_CTRL_1_SLEEP_ACTIVATE) |
				 BIT(GSC_CTRL_1_SLEEP_ENABLE));


	return ret;
}

static ssize_t gsc_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct gsc_dev *gsc = dev_get_drvdata(dev);
	const char *name = attr->attr.name;
	int rz = 0;

	if (strcasecmp(name, "fw_version") == 0)
		rz = sprintf(buf, "%d\n", gsc->fwver);
	else if (strcasecmp(name, "fw_crc") == 0)
		rz = sprintf(buf, "0x%04x\n", gsc->fwcrc);
	else
		dev_err(dev, "invalid command: '%s'\n", name);

	return rz;
}

static ssize_t gsc_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct gsc_dev *gsc = dev_get_drvdata(dev);
	const char *name = attr->attr.name;
	long value;

	if (strcasecmp(name, "powerdown") == 0) {
		if (kstrtol(buf, 0, &value) == 0)
			gsc_powerdown(gsc, value);
	} else {
		dev_err(dev, "invalid command: '%s\n", name);
	}

	return count;
}

static struct device_attribute attr_fwver =
	__ATTR(fw_version, 0440, gsc_show, NULL);
static struct device_attribute attr_fwcrc =
	__ATTR(fw_crc, 0440, gsc_show, NULL);
static struct device_attribute attr_pwrdown =
	__ATTR(powerdown, 0220, NULL, gsc_store);

static struct attribute *gsc_attrs[] = {
	&attr_fwver.attr,
	&attr_fwcrc.attr,
	&attr_pwrdown.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = gsc_attrs,
};

static const struct of_device_id gsc_of_match[] = {
	{ .compatible = "gw,gsc", },
	{ }
};
MODULE_DEVICE_TABLE(of, gsc_of_match);

static struct regmap_bus gsc_regmap_bus = {
	.reg_read = gsc_read,
	.reg_write = gsc_write,
};

static const struct regmap_config gsc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
	.max_register = GSC_WP,
};

static const struct regmap_irq gsc_irqs[] = {
	REGMAP_IRQ_REG(GSC_IRQ_PB, 0, BIT(GSC_IRQ_PB)),
	REGMAP_IRQ_REG(GSC_IRQ_KEY_ERASED, 0, BIT(GSC_IRQ_KEY_ERASED)),
	REGMAP_IRQ_REG(GSC_IRQ_EEPROM_WP, 0, BIT(GSC_IRQ_EEPROM_WP)),
	REGMAP_IRQ_REG(GSC_IRQ_RESV, 0, BIT(GSC_IRQ_RESV)),
	REGMAP_IRQ_REG(GSC_IRQ_GPIO, 0, BIT(GSC_IRQ_GPIO)),
	REGMAP_IRQ_REG(GSC_IRQ_TAMPER, 0, BIT(GSC_IRQ_TAMPER)),
	REGMAP_IRQ_REG(GSC_IRQ_WDT_TIMEOUT, 0, BIT(GSC_IRQ_WDT_TIMEOUT)),
	REGMAP_IRQ_REG(GSC_IRQ_SWITCH_HOLD, 0, BIT(GSC_IRQ_SWITCH_HOLD)),
};

static const struct regmap_irq_chip gsc_irq_chip = {
	.name = "gateworks-gsc",
	.irqs = gsc_irqs,
	.num_irqs = ARRAY_SIZE(gsc_irqs),
	.num_regs = 1,
	.status_base = GSC_IRQ_STATUS,
	.unmask_base = GSC_IRQ_ENABLE,
	.ack_base = GSC_IRQ_STATUS,
	.ack_invert = true,
};

static int gsc_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct gsc_dev *gsc;
	struct regmap_irq_chip_data *irq_data;
	int ret;
	unsigned int reg;

	gsc = devm_kzalloc(dev, sizeof(*gsc), GFP_KERNEL);
	if (!gsc)
		return -ENOMEM;

	gsc->dev = &client->dev;
	gsc->i2c = client;
	i2c_set_clientdata(client, gsc);

	gsc->regmap = devm_regmap_init(dev, &gsc_regmap_bus, client,
				       &gsc_regmap_config);
	if (IS_ERR(gsc->regmap))
		return PTR_ERR(gsc->regmap);

	if (regmap_read(gsc->regmap, GSC_FW_VER, &reg))
		return -EIO;
	gsc->fwver = reg;

	regmap_read(gsc->regmap, GSC_FW_CRC, &reg);
	gsc->fwcrc = reg;
	regmap_read(gsc->regmap, GSC_FW_CRC + 1, &reg);
	gsc->fwcrc |= reg << 8;

	gsc->i2c_hwmon = devm_i2c_new_dummy_device(dev, client->adapter,
						   GSC_HWMON);
	if (IS_ERR(gsc->i2c_hwmon)) {
		dev_err(dev, "Failed to allocate I2C device for HWMON\n");
		return PTR_ERR(gsc->i2c_hwmon);
	}

	ret = devm_regmap_add_irq_chip(dev, gsc->regmap, client->irq,
				       IRQF_ONESHOT | IRQF_SHARED |
				       IRQF_TRIGGER_LOW, 0,
				       &gsc_irq_chip, &irq_data);
	if (ret)
		return ret;

	dev_info(dev, "Gateworks System Controller v%d: fw 0x%04x\n",
		 gsc->fwver, gsc->fwcrc);

	ret = sysfs_create_group(&dev->kobj, &attr_group);
	if (ret)
		dev_err(dev, "failed to create sysfs attrs\n");

	ret = devm_of_platform_populate(dev);
	if (ret) {
		sysfs_remove_group(&dev->kobj, &attr_group);
		return ret;
	}

	return 0;
}

static void gsc_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &attr_group);
}

static struct i2c_driver gsc_driver = {
	.driver = {
		.name	= "gateworks-gsc",
		.of_match_table = gsc_of_match,
	},
	.probe		= gsc_probe,
	.remove		= gsc_remove,
};
module_i2c_driver(gsc_driver);

MODULE_AUTHOR("Tim Harvey <tharvey@gateworks.com>");
MODULE_DESCRIPTION("I2C Core interface for GSC");
MODULE_LICENSE("GPL v2");
