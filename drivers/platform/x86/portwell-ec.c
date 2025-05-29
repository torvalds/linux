// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * portwell-ec.c: Portwell embedded controller driver.
 *
 * Tested on:
 *  - Portwell NANO-6064
 *
 * This driver provides support for GPIO and Watchdog Timer
 * functionalities of the Portwell boards with ITE embedded controller (EC).
 * The EC is accessed through I/O ports and provides:
 *  - 8 GPIO pins for control and monitoring
 *  - Hardware watchdog with 1-15300 second timeout range
 *
 * It integrates with the Linux GPIO and Watchdog subsystems, allowing
 * userspace interaction with EC GPIO pins and watchdog control,
 * ensuring system stability and configurability.
 *
 * (C) Copyright 2025 Portwell, Inc.
 * Author: Yen-Chi Huang (jesse.huang@portwell.com.tw)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/dmi.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/watchdog.h>

#define PORTWELL_EC_IOSPACE              0xe300
#define PORTWELL_EC_IOSPACE_LEN          SZ_256

#define PORTWELL_GPIO_PINS               8
#define PORTWELL_GPIO_DIR_REG            0x2b
#define PORTWELL_GPIO_VAL_REG            0x2c

#define PORTWELL_WDT_EC_CONFIG_ADDR      0x06
#define PORTWELL_WDT_CONFIG_ENABLE       0x1
#define PORTWELL_WDT_CONFIG_DISABLE      0x0
#define PORTWELL_WDT_EC_COUNT_MIN_ADDR   0x07
#define PORTWELL_WDT_EC_COUNT_SEC_ADDR   0x08
#define PORTWELL_WDT_EC_MAX_COUNT_SECOND (255 * 60)

#define PORTWELL_EC_FW_VENDOR_ADDRESS    0x4d
#define PORTWELL_EC_FW_VENDOR_LENGTH     3
#define PORTWELL_EC_FW_VENDOR_NAME       "PWG"

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Force loading EC driver without checking DMI boardname");

static const struct dmi_system_id pwec_dmi_table[] = {
	{
		.ident = "NANO-6064 series",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "NANO-6064"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, pwec_dmi_table);

/* Functions for access EC via IOSPACE */

static void pwec_write(u8 index, u8 data)
{
	outb(data, PORTWELL_EC_IOSPACE + index);
}

static u8 pwec_read(u8 address)
{
	return inb(PORTWELL_EC_IOSPACE + address);
}

/* GPIO functions */

static int pwec_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	return pwec_read(PORTWELL_GPIO_VAL_REG) & BIT(offset) ? 1 : 0;
}

static int pwec_gpio_set_rv(struct gpio_chip *chip, unsigned int offset, int val)
{
	u8 tmp = pwec_read(PORTWELL_GPIO_VAL_REG);

	if (val)
		tmp |= BIT(offset);
	else
		tmp &= ~BIT(offset);
	pwec_write(PORTWELL_GPIO_VAL_REG, tmp);

	return 0;
}

static int pwec_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	u8 direction = pwec_read(PORTWELL_GPIO_DIR_REG) & BIT(offset);

	if (direction)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

/*
 * Changing direction causes issues on some boards,
 * so direction_input and direction_output are disabled for now.
 */

static int pwec_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	return -EOPNOTSUPP;
}

static int pwec_gpio_direction_output(struct gpio_chip *gc, unsigned int offset, int value)
{
	return -EOPNOTSUPP;
}

static struct gpio_chip pwec_gpio_chip = {
	.label = "portwell-ec-gpio",
	.get_direction = pwec_gpio_get_direction,
	.direction_input = pwec_gpio_direction_input,
	.direction_output = pwec_gpio_direction_output,
	.get = pwec_gpio_get,
	.set_rv = pwec_gpio_set_rv,
	.base = -1,
	.ngpio = PORTWELL_GPIO_PINS,
};

/* Watchdog functions */

static void pwec_wdt_write_timeout(unsigned int timeout)
{
	pwec_write(PORTWELL_WDT_EC_COUNT_MIN_ADDR, timeout / 60);
	pwec_write(PORTWELL_WDT_EC_COUNT_SEC_ADDR, timeout % 60);
}

static int pwec_wdt_trigger(struct watchdog_device *wdd)
{
	pwec_wdt_write_timeout(wdd->timeout);
	pwec_write(PORTWELL_WDT_EC_CONFIG_ADDR, PORTWELL_WDT_CONFIG_ENABLE);

	return 0;
}

static int pwec_wdt_start(struct watchdog_device *wdd)
{
	return pwec_wdt_trigger(wdd);
}

static int pwec_wdt_stop(struct watchdog_device *wdd)
{
	pwec_write(PORTWELL_WDT_EC_CONFIG_ADDR, PORTWELL_WDT_CONFIG_DISABLE);
	return 0;
}

static int pwec_wdt_set_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	wdd->timeout = timeout;
	pwec_wdt_write_timeout(wdd->timeout);

	return 0;
}

/* Ensure consistent min/sec read in case of second rollover. */
static unsigned int pwec_wdt_get_timeleft(struct watchdog_device *wdd)
{
	u8 sec, min, old_min;

	do {
		old_min = pwec_read(PORTWELL_WDT_EC_COUNT_MIN_ADDR);
		sec = pwec_read(PORTWELL_WDT_EC_COUNT_SEC_ADDR);
		min = pwec_read(PORTWELL_WDT_EC_COUNT_MIN_ADDR);
	} while (min != old_min);

	return min * 60 + sec;
}

static const struct watchdog_ops pwec_wdt_ops = {
	.owner = THIS_MODULE,
	.start = pwec_wdt_start,
	.stop = pwec_wdt_stop,
	.ping = pwec_wdt_trigger,
	.set_timeout = pwec_wdt_set_timeout,
	.get_timeleft = pwec_wdt_get_timeleft,
};

static struct watchdog_device ec_wdt_dev = {
	.info = &(struct watchdog_info){
		.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
		.identity = "Portwell EC watchdog",
	},
	.ops = &pwec_wdt_ops,
	.timeout = 60,
	.min_timeout = 1,
	.max_timeout = PORTWELL_WDT_EC_MAX_COUNT_SECOND,
};

static int pwec_firmware_vendor_check(void)
{
	u8 buf[PORTWELL_EC_FW_VENDOR_LENGTH + 1];
	u8 i;

	for (i = 0; i < PORTWELL_EC_FW_VENDOR_LENGTH; i++)
		buf[i] = pwec_read(PORTWELL_EC_FW_VENDOR_ADDRESS + i);
	buf[PORTWELL_EC_FW_VENDOR_LENGTH] = '\0';

	return !strcmp(PORTWELL_EC_FW_VENDOR_NAME, buf) ? 0 : -ENODEV;
}

static int pwec_probe(struct platform_device *pdev)
{
	int ret;

	if (!devm_request_region(&pdev->dev, PORTWELL_EC_IOSPACE,
				PORTWELL_EC_IOSPACE_LEN, dev_name(&pdev->dev))) {
		dev_err(&pdev->dev, "failed to get IO region\n");
		return -EBUSY;
	}

	ret = pwec_firmware_vendor_check();
	if (ret < 0)
		return ret;

	ret = devm_gpiochip_add_data(&pdev->dev, &pwec_gpio_chip, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register Portwell EC GPIO\n");
		return ret;
	}

	ret = devm_watchdog_register_device(&pdev->dev, &ec_wdt_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register Portwell EC Watchdog\n");
		return ret;
	}

	return 0;
}

static struct platform_driver pwec_driver = {
	.driver = {
		.name = "portwell-ec",
	},
	.probe = pwec_probe,
};

static struct platform_device *pwec_dev;

static int __init pwec_init(void)
{
	int ret;

	if (!dmi_check_system(pwec_dmi_table)) {
		if (!force)
			return -ENODEV;
		pr_warn("force load portwell-ec without DMI check\n");
	}

	ret = platform_driver_register(&pwec_driver);
	if (ret)
		return ret;

	pwec_dev = platform_device_register_simple("portwell-ec", -1, NULL, 0);
	if (IS_ERR(pwec_dev)) {
		platform_driver_unregister(&pwec_driver);
		return PTR_ERR(pwec_dev);
	}

	return 0;
}

static void __exit pwec_exit(void)
{
	platform_device_unregister(pwec_dev);
	platform_driver_unregister(&pwec_driver);
}

module_init(pwec_init);
module_exit(pwec_exit);

MODULE_AUTHOR("Yen-Chi Huang <jesse.huang@portwell.com.tw>");
MODULE_DESCRIPTION("Portwell EC Driver");
MODULE_LICENSE("GPL");
