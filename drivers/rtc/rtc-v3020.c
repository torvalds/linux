// SPDX-License-Identifier: GPL-2.0-only
/* drivers/rtc/rtc-v3020.c
 *
 * Copyright (C) 2006 8D Technologies inc.
 * Copyright (C) 2004 Compulab Ltd.
 *
 * Driver for the V3020 RTC
 *
 * Changelog:
 *
 *  10-May-2006: Raphael Assenat <raph@8d.com>
 *				- Converted to platform driver
 *				- Use the generic rtc class
 *
 *  ??-???-2004: Someone at Compulab
 *			- Initial driver creation.
 */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/types.h>
#include <linux/bcd.h>
#include <linux/platform_data/rtc-v3020.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include <linux/io.h>

#undef DEBUG

struct v3020;

struct v3020_chip_ops {
	int (*map_io)(struct v3020 *chip, struct platform_device *pdev,
		      struct v3020_platform_data *pdata);
	void (*unmap_io)(struct v3020 *chip);
	unsigned char (*read_bit)(struct v3020 *chip);
	void (*write_bit)(struct v3020 *chip, unsigned char bit);
};

#define V3020_CS	0
#define V3020_WR	1
#define V3020_RD	2
#define V3020_IO	3

struct v3020 {
	/* MMIO access */
	void __iomem *ioaddress;
	int leftshift;

	/* GPIO access */
	struct gpio *gpio;

	const struct v3020_chip_ops *ops;

	struct rtc_device *rtc;
};


static int v3020_mmio_map(struct v3020 *chip, struct platform_device *pdev,
			  struct v3020_platform_data *pdata)
{
	if (pdev->num_resources != 1)
		return -EBUSY;

	if (pdev->resource[0].flags != IORESOURCE_MEM)
		return -EBUSY;

	chip->leftshift = pdata->leftshift;
	chip->ioaddress = ioremap(pdev->resource[0].start, 1);
	if (chip->ioaddress == NULL)
		return -EBUSY;

	return 0;
}

static void v3020_mmio_unmap(struct v3020 *chip)
{
	iounmap(chip->ioaddress);
}

static void v3020_mmio_write_bit(struct v3020 *chip, unsigned char bit)
{
	writel(bit << chip->leftshift, chip->ioaddress);
}

static unsigned char v3020_mmio_read_bit(struct v3020 *chip)
{
	return !!(readl(chip->ioaddress) & (1 << chip->leftshift));
}

static const struct v3020_chip_ops v3020_mmio_ops = {
	.map_io		= v3020_mmio_map,
	.unmap_io	= v3020_mmio_unmap,
	.read_bit	= v3020_mmio_read_bit,
	.write_bit	= v3020_mmio_write_bit,
};

static struct gpio v3020_gpio[] = {
	{ 0, GPIOF_OUT_INIT_HIGH, "RTC CS"},
	{ 0, GPIOF_OUT_INIT_HIGH, "RTC WR"},
	{ 0, GPIOF_OUT_INIT_HIGH, "RTC RD"},
	{ 0, GPIOF_OUT_INIT_HIGH, "RTC IO"},
};

static int v3020_gpio_map(struct v3020 *chip, struct platform_device *pdev,
			  struct v3020_platform_data *pdata)
{
	int err;

	v3020_gpio[V3020_CS].gpio = pdata->gpio_cs;
	v3020_gpio[V3020_WR].gpio = pdata->gpio_wr;
	v3020_gpio[V3020_RD].gpio = pdata->gpio_rd;
	v3020_gpio[V3020_IO].gpio = pdata->gpio_io;

	err = gpio_request_array(v3020_gpio, ARRAY_SIZE(v3020_gpio));

	if (!err)
		chip->gpio = v3020_gpio;

	return err;
}

static void v3020_gpio_unmap(struct v3020 *chip)
{
	gpio_free_array(v3020_gpio, ARRAY_SIZE(v3020_gpio));
}

static void v3020_gpio_write_bit(struct v3020 *chip, unsigned char bit)
{
	gpio_direction_output(chip->gpio[V3020_IO].gpio, bit);
	gpio_set_value(chip->gpio[V3020_CS].gpio, 0);
	gpio_set_value(chip->gpio[V3020_WR].gpio, 0);
	udelay(1);
	gpio_set_value(chip->gpio[V3020_WR].gpio, 1);
	gpio_set_value(chip->gpio[V3020_CS].gpio, 1);
}

static unsigned char v3020_gpio_read_bit(struct v3020 *chip)
{
	int bit;

	gpio_direction_input(chip->gpio[V3020_IO].gpio);
	gpio_set_value(chip->gpio[V3020_CS].gpio, 0);
	gpio_set_value(chip->gpio[V3020_RD].gpio, 0);
	udelay(1);
	bit = !!gpio_get_value(chip->gpio[V3020_IO].gpio);
	udelay(1);
	gpio_set_value(chip->gpio[V3020_RD].gpio, 1);
	gpio_set_value(chip->gpio[V3020_CS].gpio, 1);

	return bit;
}

static const struct v3020_chip_ops v3020_gpio_ops = {
	.map_io		= v3020_gpio_map,
	.unmap_io	= v3020_gpio_unmap,
	.read_bit	= v3020_gpio_read_bit,
	.write_bit	= v3020_gpio_write_bit,
};

static void v3020_set_reg(struct v3020 *chip, unsigned char address,
			unsigned char data)
{
	int i;
	unsigned char tmp;

	tmp = address;
	for (i = 0; i < 4; i++) {
		chip->ops->write_bit(chip, (tmp & 1));
		tmp >>= 1;
		udelay(1);
	}

	/* Commands dont have data */
	if (!V3020_IS_COMMAND(address)) {
		for (i = 0; i < 8; i++) {
			chip->ops->write_bit(chip, (data & 1));
			data >>= 1;
			udelay(1);
		}
	}
}

static unsigned char v3020_get_reg(struct v3020 *chip, unsigned char address)
{
	unsigned int data = 0;
	int i;

	for (i = 0; i < 4; i++) {
		chip->ops->write_bit(chip, (address & 1));
		address >>= 1;
		udelay(1);
	}

	for (i = 0; i < 8; i++) {
		data >>= 1;
		if (chip->ops->read_bit(chip))
			data |= 0x80;
		udelay(1);
	}

	return data;
}

static int v3020_read_time(struct device *dev, struct rtc_time *dt)
{
	struct v3020 *chip = dev_get_drvdata(dev);
	int tmp;

	/* Copy the current time to ram... */
	v3020_set_reg(chip, V3020_CMD_CLOCK2RAM, 0);

	/* ...and then read constant values. */
	tmp = v3020_get_reg(chip, V3020_SECONDS);
	dt->tm_sec	= bcd2bin(tmp);
	tmp = v3020_get_reg(chip, V3020_MINUTES);
	dt->tm_min	= bcd2bin(tmp);
	tmp = v3020_get_reg(chip, V3020_HOURS);
	dt->tm_hour	= bcd2bin(tmp);
	tmp = v3020_get_reg(chip, V3020_MONTH_DAY);
	dt->tm_mday	= bcd2bin(tmp);
	tmp = v3020_get_reg(chip, V3020_MONTH);
	dt->tm_mon    = bcd2bin(tmp) - 1;
	tmp = v3020_get_reg(chip, V3020_WEEK_DAY);
	dt->tm_wday	= bcd2bin(tmp);
	tmp = v3020_get_reg(chip, V3020_YEAR);
	dt->tm_year = bcd2bin(tmp)+100;

	dev_dbg(dev, "\n%s : Read RTC values\n", __func__);
	dev_dbg(dev, "tm_hour: %i\n", dt->tm_hour);
	dev_dbg(dev, "tm_min : %i\n", dt->tm_min);
	dev_dbg(dev, "tm_sec : %i\n", dt->tm_sec);
	dev_dbg(dev, "tm_year: %i\n", dt->tm_year);
	dev_dbg(dev, "tm_mon : %i\n", dt->tm_mon);
	dev_dbg(dev, "tm_mday: %i\n", dt->tm_mday);
	dev_dbg(dev, "tm_wday: %i\n", dt->tm_wday);

	return 0;
}


static int v3020_set_time(struct device *dev, struct rtc_time *dt)
{
	struct v3020 *chip = dev_get_drvdata(dev);

	dev_dbg(dev, "\n%s : Setting RTC values\n", __func__);
	dev_dbg(dev, "tm_sec : %i\n", dt->tm_sec);
	dev_dbg(dev, "tm_min : %i\n", dt->tm_min);
	dev_dbg(dev, "tm_hour: %i\n", dt->tm_hour);
	dev_dbg(dev, "tm_mday: %i\n", dt->tm_mday);
	dev_dbg(dev, "tm_wday: %i\n", dt->tm_wday);
	dev_dbg(dev, "tm_year: %i\n", dt->tm_year);

	/* Write all the values to ram... */
	v3020_set_reg(chip, V3020_SECONDS,	bin2bcd(dt->tm_sec));
	v3020_set_reg(chip, V3020_MINUTES,	bin2bcd(dt->tm_min));
	v3020_set_reg(chip, V3020_HOURS,	bin2bcd(dt->tm_hour));
	v3020_set_reg(chip, V3020_MONTH_DAY,	bin2bcd(dt->tm_mday));
	v3020_set_reg(chip, V3020_MONTH,	bin2bcd(dt->tm_mon + 1));
	v3020_set_reg(chip, V3020_WEEK_DAY,	bin2bcd(dt->tm_wday));
	v3020_set_reg(chip, V3020_YEAR,		bin2bcd(dt->tm_year % 100));

	/* ...and set the clock. */
	v3020_set_reg(chip, V3020_CMD_RAM2CLOCK, 0);

	/* Compulab used this delay here. I dont know why,
	 * the datasheet does not specify a delay. */
	/*mdelay(5);*/

	return 0;
}

static const struct rtc_class_ops v3020_rtc_ops = {
	.read_time	= v3020_read_time,
	.set_time	= v3020_set_time,
};

static int rtc_probe(struct platform_device *pdev)
{
	struct v3020_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct v3020 *chip;
	int retval = -EBUSY;
	int i;
	int temp;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (pdata->use_gpio)
		chip->ops = &v3020_gpio_ops;
	else
		chip->ops = &v3020_mmio_ops;

	retval = chip->ops->map_io(chip, pdev, pdata);
	if (retval)
		return retval;

	/* Make sure the v3020 expects a communication cycle
	 * by reading 8 times */
	for (i = 0; i < 8; i++)
		temp = chip->ops->read_bit(chip);

	/* Test chip by doing a write/read sequence
	 * to the chip ram */
	v3020_set_reg(chip, V3020_SECONDS, 0x33);
	if (v3020_get_reg(chip, V3020_SECONDS) != 0x33) {
		retval = -ENODEV;
		goto err_io;
	}

	/* Make sure frequency measurement mode, test modes, and lock
	 * are all disabled */
	v3020_set_reg(chip, V3020_STATUS_0, 0x0);

	if (pdata->use_gpio)
		dev_info(&pdev->dev, "Chip available at GPIOs "
			 "%d, %d, %d, %d\n",
			 chip->gpio[V3020_CS].gpio, chip->gpio[V3020_WR].gpio,
			 chip->gpio[V3020_RD].gpio, chip->gpio[V3020_IO].gpio);
	else
		dev_info(&pdev->dev, "Chip available at "
			 "physical address 0x%llx,"
			 "data connected to D%d\n",
			 (unsigned long long)pdev->resource[0].start,
			 chip->leftshift);

	platform_set_drvdata(pdev, chip);

	chip->rtc = devm_rtc_device_register(&pdev->dev, "v3020",
					&v3020_rtc_ops, THIS_MODULE);
	if (IS_ERR(chip->rtc)) {
		retval = PTR_ERR(chip->rtc);
		goto err_io;
	}

	return 0;

err_io:
	chip->ops->unmap_io(chip);

	return retval;
}

static int rtc_remove(struct platform_device *dev)
{
	struct v3020 *chip = platform_get_drvdata(dev);

	chip->ops->unmap_io(chip);

	return 0;
}

static struct platform_driver rtc_device_driver = {
	.probe	= rtc_probe,
	.remove = rtc_remove,
	.driver = {
		.name	= "v3020",
	},
};

module_platform_driver(rtc_device_driver);

MODULE_DESCRIPTION("V3020 RTC");
MODULE_AUTHOR("Raphael Assenat");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:v3020");
