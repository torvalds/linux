// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * portwell-ec.c: Portwell embedded controller driver.
 *
 * Tested on:
 *  - Portwell NANO-6064
 *
 * This driver supports Portwell boards with an ITE embedded controller (EC).
 * The EC is accessed through I/O ports and provides:
 *  - Temperature and voltage readings (hwmon)
 *  - 8 GPIO pins for control and monitoring
 *  - Hardware watchdog with 1-15300 second timeout range
 *
 * It integrates with the Linux hwmon, GPIO and Watchdog subsystems.
 *
 * (C) Copyright 2025 Portwell, Inc.
 * Author: Yen-Chi Huang (jesse.huang@portwell.com.tw)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/dmi.h>
#include <linux/gpio/driver.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/units.h>
#include <linux/watchdog.h>

#define PORTWELL_EC_IOSPACE              0xe300
#define PORTWELL_EC_IOSPACE_LEN          SZ_256

#define PORTWELL_GPIO_PINS               8
#define PORTWELL_GPIO_DIR_REG            0x2b
#define PORTWELL_GPIO_VAL_REG            0x2c

#define PORTWELL_HWMON_TEMP_NUM          3
#define PORTWELL_HWMON_VOLT_NUM          5

#define PORTWELL_WDT_EC_CONFIG_ADDR      0x06
#define PORTWELL_WDT_CONFIG_ENABLE       0x1
#define PORTWELL_WDT_CONFIG_DISABLE      0x0
#define PORTWELL_WDT_EC_COUNT_MIN_ADDR   0x07
#define PORTWELL_WDT_EC_COUNT_SEC_ADDR   0x08
#define PORTWELL_WDT_EC_MAX_COUNT_SECOND (255 * 60)

#define PORTWELL_EC_FW_VENDOR_ADDRESS    0x4d
#define PORTWELL_EC_FW_VENDOR_LENGTH     3
#define PORTWELL_EC_FW_VENDOR_NAME       "PWG"

#define PORTWELL_EC_ADC_MAX              1023

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Force loading EC driver without checking DMI boardname");

/* A sensor's metadata (label, scale, and register) */
struct pwec_sensor_prop {
	const char *label;
	u8 reg;
	u32 scale;
};

/* Master configuration with properties for all possible sensors */
static const struct {
	const struct pwec_sensor_prop temp_props[PORTWELL_HWMON_TEMP_NUM];
	const struct pwec_sensor_prop in_props[PORTWELL_HWMON_VOLT_NUM];
} pwec_master_data = {
	.temp_props = {
		{ "CPU Temperature",    0x00, 0 },
		{ "System Temperature", 0x02, 0 },
		{ "Aux Temperature",    0x04, 0 },
	},
	.in_props = {
		{ "Vcore", 0x20, 3000 },
		{ "3.3V",  0x22, 6000 },
		{ "5V",    0x24, 9600 },
		{ "12V",   0x30, 19800 },
		{ "VDIMM", 0x32, 3000 },
	},
};

struct pwec_board_info {
	u32 temp_mask;	/* bit N = temperature channel N */
	u32 in_mask;	/* bit N = voltage channel N */
};

static const struct pwec_board_info pwec_board_info_default = {
	.temp_mask = GENMASK(PORTWELL_HWMON_TEMP_NUM - 1, 0),
	.in_mask   = GENMASK(PORTWELL_HWMON_VOLT_NUM - 1, 0),
};

static const struct pwec_board_info pwec_board_info_nano = {
	.temp_mask = BIT(0) | BIT(1),
	.in_mask = GENMASK(4, 0),
};

static const struct dmi_system_id pwec_dmi_table[] = {
	{
		.ident = "NANO-6064 series",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "NANO-6064"),
		},
		.driver_data = (void *)&pwec_board_info_nano,
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

/* Ensure consistent 16-bit read across potential MSB rollover. */
static u16 pwec_read16_stable(u8 lsb_reg)
{
	u8 lsb, msb, old_msb;

	do {
		old_msb = pwec_read(lsb_reg + 1);
		lsb = pwec_read(lsb_reg);
		msb = pwec_read(lsb_reg + 1);
	} while (msb != old_msb);

	return (msb << 8) | lsb;
}

/* GPIO functions */

static int pwec_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	return pwec_read(PORTWELL_GPIO_VAL_REG) & BIT(offset) ? 1 : 0;
}

static int pwec_gpio_set(struct gpio_chip *chip, unsigned int offset, int val)
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
	.set = pwec_gpio_set,
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

/* HWMON functions */

static umode_t pwec_hwmon_is_visible(const void *drvdata, enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	const struct pwec_board_info *info = drvdata;

	switch (type) {
	case hwmon_temp:
		return (info->temp_mask & BIT(channel)) ? 0444 : 0;
	case hwmon_in:
		return (info->in_mask & BIT(channel)) ? 0444 : 0;
	default:
		return 0;
	}
}

static int pwec_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	u16 tmp16;

	switch (type) {
	case hwmon_temp:
		*val = pwec_read(pwec_master_data.temp_props[channel].reg) * MILLIDEGREE_PER_DEGREE;
		return 0;
	case hwmon_in:
		tmp16 = pwec_read16_stable(pwec_master_data.in_props[channel].reg);
		*val = (tmp16 * pwec_master_data.in_props[channel].scale) / PORTWELL_EC_ADC_MAX;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int pwec_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type,
				  u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		*str = pwec_master_data.temp_props[channel].label;
		return 0;
	case hwmon_in:
		*str = pwec_master_data.in_props[channel].label;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info *pwec_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
		HWMON_I_INPUT | HWMON_I_LABEL,
		HWMON_I_INPUT | HWMON_I_LABEL,
		HWMON_I_INPUT | HWMON_I_LABEL,
		HWMON_I_INPUT | HWMON_I_LABEL,
		HWMON_I_INPUT | HWMON_I_LABEL),
	NULL
};

static const struct hwmon_ops pwec_hwmon_ops = {
	.is_visible = pwec_hwmon_is_visible,
	.read = pwec_hwmon_read,
	.read_string = pwec_hwmon_read_string,
};

static const struct hwmon_chip_info pwec_chip_info = {
	.ops = &pwec_hwmon_ops,
	.info = pwec_hwmon_info,
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
	struct device *hwmon_dev;
	void *drvdata = dev_get_platdata(&pdev->dev);
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

	if (IS_REACHABLE(CONFIG_HWMON)) {
		hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
				"portwell_ec", drvdata, &pwec_chip_info, NULL);
		ret = PTR_ERR_OR_ZERO(hwmon_dev);
		if (ret)
			return ret;
	}

	ec_wdt_dev.parent = &pdev->dev;
	return devm_watchdog_register_device(&pdev->dev, &ec_wdt_dev);
}

static int pwec_suspend(struct device *dev)
{
	if (watchdog_active(&ec_wdt_dev))
		return pwec_wdt_stop(&ec_wdt_dev);

	return 0;
}

static int pwec_resume(struct device *dev)
{
	if (watchdog_active(&ec_wdt_dev))
		return pwec_wdt_start(&ec_wdt_dev);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(pwec_dev_pm_ops, pwec_suspend, pwec_resume);

static struct platform_driver pwec_driver = {
	.driver = {
		.name = "portwell-ec",
		.pm = pm_sleep_ptr(&pwec_dev_pm_ops),
	},
	.probe = pwec_probe,
};

static struct platform_device *pwec_dev;

static int __init pwec_init(void)
{
	const struct dmi_system_id *match;
	const struct pwec_board_info *hwmon_data;
	int ret;

	match = dmi_first_match(pwec_dmi_table);
	if (!match) {
		if (!force)
			return -ENODEV;
		hwmon_data = &pwec_board_info_default;
		pr_warn("force load portwell-ec without DMI check, using full display config\n");
	} else {
		hwmon_data = match->driver_data;
	}

	ret = platform_driver_register(&pwec_driver);
	if (ret)
		return ret;

	pwec_dev = platform_device_register_data(NULL, "portwell-ec", PLATFORM_DEVID_NONE,
						hwmon_data, sizeof(*hwmon_data));
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
