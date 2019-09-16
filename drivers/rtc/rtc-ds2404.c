// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2012 Sven Schnelle <svens@stackframe.org>

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/types.h>
#include <linux/bcd.h>
#include <linux/platform_data/rtc-ds2404.h>
#include <linux/delay.h>
#include <linux/gpio.h>
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

struct ds2404_gpio {
	const char *name;
	unsigned int gpio;
};

struct ds2404 {
	struct ds2404_gpio *gpio;
	struct rtc_device *rtc;
};

static struct ds2404_gpio ds2404_gpio[] = {
	{ "RTC RST", 0 },
	{ "RTC CLK", 0 },
	{ "RTC DQ", 0 },
};

static int ds2404_gpio_map(struct ds2404 *chip, struct platform_device *pdev,
			  struct ds2404_platform_data *pdata)
{
	int i, err;

	ds2404_gpio[DS2404_RST].gpio = pdata->gpio_rst;
	ds2404_gpio[DS2404_CLK].gpio = pdata->gpio_clk;
	ds2404_gpio[DS2404_DQ].gpio = pdata->gpio_dq;

	for (i = 0; i < ARRAY_SIZE(ds2404_gpio); i++) {
		err = gpio_request(ds2404_gpio[i].gpio, ds2404_gpio[i].name);
		if (err) {
			dev_err(&pdev->dev, "error mapping gpio %s: %d\n",
				ds2404_gpio[i].name, err);
			goto err_request;
		}
		if (i != DS2404_DQ)
			gpio_direction_output(ds2404_gpio[i].gpio, 1);
	}

	chip->gpio = ds2404_gpio;
	return 0;

err_request:
	while (--i >= 0)
		gpio_free(ds2404_gpio[i].gpio);
	return err;
}

static void ds2404_gpio_unmap(void *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ds2404_gpio); i++)
		gpio_free(ds2404_gpio[i].gpio);
}

static void ds2404_reset(struct device *dev)
{
	gpio_set_value(ds2404_gpio[DS2404_RST].gpio, 0);
	udelay(1000);
	gpio_set_value(ds2404_gpio[DS2404_RST].gpio, 1);
	gpio_set_value(ds2404_gpio[DS2404_CLK].gpio, 0);
	gpio_direction_output(ds2404_gpio[DS2404_DQ].gpio, 0);
	udelay(10);
}

static void ds2404_write_byte(struct device *dev, u8 byte)
{
	int i;

	gpio_direction_output(ds2404_gpio[DS2404_DQ].gpio, 1);
	for (i = 0; i < 8; i++) {
		gpio_set_value(ds2404_gpio[DS2404_DQ].gpio, byte & (1 << i));
		udelay(10);
		gpio_set_value(ds2404_gpio[DS2404_CLK].gpio, 1);
		udelay(10);
		gpio_set_value(ds2404_gpio[DS2404_CLK].gpio, 0);
		udelay(10);
	}
}

static u8 ds2404_read_byte(struct device *dev)
{
	int i;
	u8 ret = 0;

	gpio_direction_input(ds2404_gpio[DS2404_DQ].gpio);

	for (i = 0; i < 8; i++) {
		gpio_set_value(ds2404_gpio[DS2404_CLK].gpio, 0);
		udelay(10);
		if (gpio_get_value(ds2404_gpio[DS2404_DQ].gpio))
			ret |= 1 << i;
		gpio_set_value(ds2404_gpio[DS2404_CLK].gpio, 1);
		udelay(10);
	}
	return ret;
}

static void ds2404_read_memory(struct device *dev, u16 offset,
			       int length, u8 *out)
{
	ds2404_reset(dev);
	ds2404_write_byte(dev, DS2404_READ_MEMORY_CMD);
	ds2404_write_byte(dev, offset & 0xff);
	ds2404_write_byte(dev, (offset >> 8) & 0xff);
	while (length--)
		*out++ = ds2404_read_byte(dev);
}

static void ds2404_write_memory(struct device *dev, u16 offset,
				int length, u8 *out)
{
	int i;
	u8 ta01, ta02, es;

	ds2404_reset(dev);
	ds2404_write_byte(dev, DS2404_WRITE_SCRATCHPAD_CMD);
	ds2404_write_byte(dev, offset & 0xff);
	ds2404_write_byte(dev, (offset >> 8) & 0xff);

	for (i = 0; i < length; i++)
		ds2404_write_byte(dev, out[i]);

	ds2404_reset(dev);
	ds2404_write_byte(dev, DS2404_READ_SCRATCHPAD_CMD);

	ta01 = ds2404_read_byte(dev);
	ta02 = ds2404_read_byte(dev);
	es = ds2404_read_byte(dev);

	for (i = 0; i < length; i++) {
		if (out[i] != ds2404_read_byte(dev)) {
			dev_err(dev, "read invalid data\n");
			return;
		}
	}

	ds2404_reset(dev);
	ds2404_write_byte(dev, DS2404_COPY_SCRATCHPAD_CMD);
	ds2404_write_byte(dev, ta01);
	ds2404_write_byte(dev, ta02);
	ds2404_write_byte(dev, es);

	gpio_direction_input(ds2404_gpio[DS2404_DQ].gpio);
	while (gpio_get_value(ds2404_gpio[DS2404_DQ].gpio))
		;
}

static void ds2404_enable_osc(struct device *dev)
{
	u8 in[1] = { 0x10 }; /* enable oscillator */
	ds2404_write_memory(dev, 0x201, 1, in);
}

static int ds2404_read_time(struct device *dev, struct rtc_time *dt)
{
	unsigned long time = 0;
	__le32 hw_time = 0;

	ds2404_read_memory(dev, 0x203, 4, (u8 *)&hw_time);
	time = le32_to_cpu(hw_time);

	rtc_time64_to_tm(time, dt);
	return 0;
}

static int ds2404_set_time(struct device *dev, struct rtc_time *dt)
{
	u32 time = cpu_to_le32(rtc_tm_to_time64(dt));
	ds2404_write_memory(dev, 0x203, 4, (u8 *)&time);
	return 0;
}

static const struct rtc_class_ops ds2404_rtc_ops = {
	.read_time	= ds2404_read_time,
	.set_time	= ds2404_set_time,
};

static int rtc_probe(struct platform_device *pdev)
{
	struct ds2404_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct ds2404 *chip;
	int retval = -EBUSY;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct ds2404), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(chip->rtc))
		return PTR_ERR(chip->rtc);

	retval = ds2404_gpio_map(chip, pdev, pdata);
	if (retval)
		return retval;

	retval = devm_add_action_or_reset(&pdev->dev, ds2404_gpio_unmap, chip);
	if (retval)
		return retval;

	dev_info(&pdev->dev, "using GPIOs RST:%d, CLK:%d, DQ:%d\n",
		 chip->gpio[DS2404_RST].gpio, chip->gpio[DS2404_CLK].gpio,
		 chip->gpio[DS2404_DQ].gpio);

	platform_set_drvdata(pdev, chip);

	chip->rtc->ops = &ds2404_rtc_ops;
	chip->rtc->range_max = U32_MAX;

	retval = rtc_register_device(chip->rtc);
	if (retval)
		return retval;

	ds2404_enable_osc(&pdev->dev);
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
