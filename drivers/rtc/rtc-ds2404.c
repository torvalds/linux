// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2012 Sven Schnelle <svens@stackframe.org>

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/types.h>
#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>

#include <linux/io.h>

#define DS2404_STATUS_REG 0x200
#define DS2404_CONTROL_REG 0x201
#define DS2404_RTC_REG 0x202

#define DS2404_WRITE_SCRATCHPAD_CMD 0x0f
#define DS2404_READ_SCRATCHPAD_CMD 0xaa
#define DS2404_COPY_SCRATCHPAD_CMD 0x55
#define DS2404_READ_MEMORY_CMD 0xf0

#define DS2404_RST	0
#define DS2404_CLK	1
#define DS2404_DQ	2

struct ds2404 {
	struct device *dev;
	struct gpio_desc *rst_gpiod;
	struct gpio_desc *clk_gpiod;
	struct gpio_desc *dq_gpiod;
};

static int ds2404_gpio_map(struct ds2404 *chip, struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	/* This will de-assert RESET, declare this GPIO as GPIOD_ACTIVE_LOW */
	chip->rst_gpiod = devm_gpiod_get(dev, "rst", GPIOD_OUT_LOW);
	if (IS_ERR(chip->rst_gpiod))
		return PTR_ERR(chip->rst_gpiod);

	chip->clk_gpiod = devm_gpiod_get(dev, "clk", GPIOD_OUT_HIGH);
	if (IS_ERR(chip->clk_gpiod))
		return PTR_ERR(chip->clk_gpiod);

	chip->dq_gpiod = devm_gpiod_get(dev, "dq", GPIOD_ASIS);
	if (IS_ERR(chip->dq_gpiod))
		return PTR_ERR(chip->dq_gpiod);

	return 0;
}

static void ds2404_reset(struct ds2404 *chip)
{
	gpiod_set_value(chip->rst_gpiod, 1);
	udelay(1000);
	gpiod_set_value(chip->rst_gpiod, 0);
	gpiod_set_value(chip->clk_gpiod, 0);
	gpiod_direction_output(chip->dq_gpiod, 0);
	udelay(10);
}

static void ds2404_write_byte(struct ds2404 *chip, u8 byte)
{
	int i;

	gpiod_direction_output(chip->dq_gpiod, 1);
	for (i = 0; i < 8; i++) {
		gpiod_set_value(chip->dq_gpiod, byte & (1 << i));
		udelay(10);
		gpiod_set_value(chip->clk_gpiod, 1);
		udelay(10);
		gpiod_set_value(chip->clk_gpiod, 0);
		udelay(10);
	}
}

static u8 ds2404_read_byte(struct ds2404 *chip)
{
	int i;
	u8 ret = 0;

	gpiod_direction_input(chip->dq_gpiod);

	for (i = 0; i < 8; i++) {
		gpiod_set_value(chip->clk_gpiod, 0);
		udelay(10);
		if (gpiod_get_value(chip->dq_gpiod))
			ret |= 1 << i;
		gpiod_set_value(chip->clk_gpiod, 1);
		udelay(10);
	}
	return ret;
}

static void ds2404_read_memory(struct ds2404 *chip, u16 offset,
			       int length, u8 *out)
{
	ds2404_reset(chip);
	ds2404_write_byte(chip, DS2404_READ_MEMORY_CMD);
	ds2404_write_byte(chip, offset & 0xff);
	ds2404_write_byte(chip, (offset >> 8) & 0xff);
	while (length--)
		*out++ = ds2404_read_byte(chip);
}

static void ds2404_write_memory(struct ds2404 *chip, u16 offset,
				int length, u8 *out)
{
	int i;
	u8 ta01, ta02, es;

	ds2404_reset(chip);
	ds2404_write_byte(chip, DS2404_WRITE_SCRATCHPAD_CMD);
	ds2404_write_byte(chip, offset & 0xff);
	ds2404_write_byte(chip, (offset >> 8) & 0xff);

	for (i = 0; i < length; i++)
		ds2404_write_byte(chip, out[i]);

	ds2404_reset(chip);
	ds2404_write_byte(chip, DS2404_READ_SCRATCHPAD_CMD);

	ta01 = ds2404_read_byte(chip);
	ta02 = ds2404_read_byte(chip);
	es = ds2404_read_byte(chip);

	for (i = 0; i < length; i++) {
		if (out[i] != ds2404_read_byte(chip)) {
			dev_err(chip->dev, "read invalid data\n");
			return;
		}
	}

	ds2404_reset(chip);
	ds2404_write_byte(chip, DS2404_COPY_SCRATCHPAD_CMD);
	ds2404_write_byte(chip, ta01);
	ds2404_write_byte(chip, ta02);
	ds2404_write_byte(chip, es);

	while (gpiod_get_value(chip->dq_gpiod))
		;
}

static void ds2404_enable_osc(struct ds2404 *chip)
{
	u8 in[1] = { 0x10 }; /* enable oscillator */

	ds2404_write_memory(chip, 0x201, 1, in);
}

static int ds2404_read_time(struct device *dev, struct rtc_time *dt)
{
	struct ds2404 *chip = dev_get_drvdata(dev);
	unsigned long time = 0;
	__le32 hw_time = 0;

	ds2404_read_memory(chip, 0x203, 4, (u8 *)&hw_time);
	time = le32_to_cpu(hw_time);

	rtc_time64_to_tm(time, dt);
	return 0;
}

static int ds2404_set_time(struct device *dev, struct rtc_time *dt)
{
	struct ds2404 *chip = dev_get_drvdata(dev);
	u32 time = cpu_to_le32(rtc_tm_to_time64(dt));
	ds2404_write_memory(chip, 0x203, 4, (u8 *)&time);
	return 0;
}

static const struct rtc_class_ops ds2404_rtc_ops = {
	.read_time	= ds2404_read_time,
	.set_time	= ds2404_set_time,
};

static int rtc_probe(struct platform_device *pdev)
{
	struct ds2404 *chip;
	struct rtc_device *rtc;
	int retval = -EBUSY;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct ds2404), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	retval = ds2404_gpio_map(chip, pdev);
	if (retval)
		return retval;

	platform_set_drvdata(pdev, chip);

	rtc->ops = &ds2404_rtc_ops;
	rtc->range_max = U32_MAX;

	retval = devm_rtc_register_device(rtc);
	if (retval)
		return retval;

	ds2404_enable_osc(chip);
	return 0;
}

static struct platform_driver rtc_device_driver = {
	.probe	= rtc_probe,
	.driver = {
		.name	= "ds2404",
	},
};
module_platform_driver(rtc_device_driver);

MODULE_DESCRIPTION("DS2404 RTC");
MODULE_AUTHOR("Sven Schnelle");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ds2404");
