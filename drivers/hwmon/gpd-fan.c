// SPDX-License-Identifier: GPL-2.0+

/* Platform driver for GPD devices that expose fan control via hwmon sysfs.
 *
 * Fan control is provided via pwm interface in the range [0-255].
 * Each model has a different range in the EC, the written value is scaled to
 * accommodate for that.
 *
 * Based on this repo:
 * https://github.com/Cryolitia/gpd-fan-driver
 *
 * Copyright (c) 2024 Cryolitia PukNgae
 */

#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "gpdfan"
#define GPD_PWM_CTR_OFFSET 0x1841

static char *gpd_fan_board = "";
module_param(gpd_fan_board, charp, 0444);

// EC read/write locker, protecting a sequence of EC operations
static DEFINE_MUTEX(gpd_fan_sequence_lock);

enum gpd_board {
	win_mini,
	win4_6800u,
	win_max_2,
	duo,
};

enum FAN_PWM_ENABLE {
	DISABLE		= 0,
	MANUAL		= 1,
	AUTOMATIC	= 2,
};

static struct {
	enum FAN_PWM_ENABLE pwm_enable;
	u8 pwm_value;

	const struct gpd_fan_drvdata *drvdata;
} gpd_driver_priv;

struct gpd_fan_drvdata {
	const char *board_name; // Board name for module param comparison
	const enum gpd_board board;

	const u8 addr_port;
	const u8 data_port;
	const u16 manual_control_enable;
	const u16 rpm_read;
	const u16 pwm_write;
	const u16 pwm_max;
};

static struct gpd_fan_drvdata gpd_win_mini_drvdata = {
	.board_name		= "win_mini",
	.board			= win_mini,

	.addr_port		= 0x4E,
	.data_port		= 0x4F,
	.manual_control_enable	= 0x047A,
	.rpm_read		= 0x0478,
	.pwm_write		= 0x047A,
	.pwm_max		= 244,
};

static struct gpd_fan_drvdata gpd_duo_drvdata = {
	.board_name		= "duo",
	.board			= duo,

	.addr_port		= 0x4E,
	.data_port		= 0x4F,
	.manual_control_enable	= 0x047A,
	.rpm_read		= 0x0478,
	.pwm_write		= 0x047A,
	.pwm_max		= 244,
};

static struct gpd_fan_drvdata gpd_win4_drvdata = {
	.board_name		= "win4",
	.board			= win4_6800u,

	.addr_port		= 0x2E,
	.data_port		= 0x2F,
	.manual_control_enable	= 0xC311,
	.rpm_read		= 0xC880,
	.pwm_write		= 0xC311,
	.pwm_max		= 127,
};

static struct gpd_fan_drvdata gpd_wm2_drvdata = {
	.board_name		= "wm2",
	.board			= win_max_2,

	.addr_port		= 0x4E,
	.data_port		= 0x4F,
	.manual_control_enable	= 0x0275,
	.rpm_read		= 0x0218,
	.pwm_write		= 0x1809,
	.pwm_max		= 184,
};

static const struct dmi_system_id dmi_table[] = {
	{
		// GPD Win Mini
		// GPD Win Mini with AMD Ryzen 8840U
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1617-01")
		},
		.driver_data = &gpd_win_mini_drvdata,
	},
	{
		// GPD Win Mini
		// GPD Win Mini with AMD Ryzen HX370
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1617-02")
		},
		.driver_data = &gpd_win_mini_drvdata,
	},
	{
		// GPD Win Mini
		// GPD Win Mini with AMD Ryzen HX370
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1617-02-L")
		},
		.driver_data = &gpd_win_mini_drvdata,
	},
	{
		// GPD Win 4 with AMD Ryzen 6800U
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1618-04"),
			DMI_MATCH(DMI_BOARD_VERSION, "Default string"),
		},
		.driver_data = &gpd_win4_drvdata,
	},
	{
		// GPD Win 4 with Ryzen 7840U
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1618-04"),
			DMI_MATCH(DMI_BOARD_VERSION, "Ver. 1.0"),
		},
		// Since 7840U, win4 uses the same drvdata as wm2
		.driver_data = &gpd_wm2_drvdata,
	},
	{
		// GPD Win 4 with Ryzen 7840U (another)
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1618-04"),
			DMI_MATCH(DMI_BOARD_VERSION, "Ver.1.0"),
		},
		.driver_data = &gpd_wm2_drvdata,
	},
	{
		// GPD Win Max 2 with Ryzen 6800U
		// GPD Win Max 2 2023 with Ryzen 7840U
		// GPD Win Max 2 2024 with Ryzen 8840U
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1619-04"),
		},
		.driver_data = &gpd_wm2_drvdata,
	},
	{
		// GPD Win Max 2 with AMD Ryzen HX370
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1619-05"),
		},
		.driver_data = &gpd_wm2_drvdata,
	},
	{
		// GPD Duo
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1622-01"),
		},
		.driver_data = &gpd_duo_drvdata,
	},
	{
		// GPD Duo (another)
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1622-01-L"),
		},
		.driver_data = &gpd_duo_drvdata,
	},
	{
		// GPD Pocket 4
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1628-04"),
		},
		.driver_data = &gpd_win_mini_drvdata,
	},
	{
		// GPD Pocket 4 (another)
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "GPD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "G1628-04-L"),
		},
		.driver_data = &gpd_win_mini_drvdata,
	},
	{}
};

static const struct gpd_fan_drvdata *gpd_module_drvdata[] = {
	&gpd_win_mini_drvdata, &gpd_win4_drvdata, &gpd_wm2_drvdata, NULL
};

// Helper functions to handle EC read/write
static void gpd_ecram_read(u16 offset, u8 *val)
{
	u16 addr_port = gpd_driver_priv.drvdata->addr_port;
	u16 data_port = gpd_driver_priv.drvdata->data_port;

	outb(0x2E, addr_port);
	outb(0x11, data_port);
	outb(0x2F, addr_port);
	outb((u8)((offset >> 8) & 0xFF), data_port);

	outb(0x2E, addr_port);
	outb(0x10, data_port);
	outb(0x2F, addr_port);
	outb((u8)(offset & 0xFF), data_port);

	outb(0x2E, addr_port);
	outb(0x12, data_port);
	outb(0x2F, addr_port);
	*val = inb(data_port);
}

static void gpd_ecram_write(u16 offset, u8 value)
{
	u16 addr_port = gpd_driver_priv.drvdata->addr_port;
	u16 data_port = gpd_driver_priv.drvdata->data_port;

	outb(0x2E, addr_port);
	outb(0x11, data_port);
	outb(0x2F, addr_port);
	outb((u8)((offset >> 8) & 0xFF), data_port);

	outb(0x2E, addr_port);
	outb(0x10, data_port);
	outb(0x2F, addr_port);
	outb((u8)(offset & 0xFF), data_port);

	outb(0x2E, addr_port);
	outb(0x12, data_port);
	outb(0x2F, addr_port);
	outb(value, data_port);
}

static int gpd_generic_read_rpm(void)
{
	const struct gpd_fan_drvdata *const drvdata = gpd_driver_priv.drvdata;
	u8 high, low;

	gpd_ecram_read(drvdata->rpm_read, &high);
	gpd_ecram_read(drvdata->rpm_read + 1, &low);

	return (u16)high << 8 | low;
}

static int gpd_wm2_read_rpm(void)
{
	for (u16 pwm_ctr_offset = GPD_PWM_CTR_OFFSET;
	     pwm_ctr_offset <= GPD_PWM_CTR_OFFSET + 2; pwm_ctr_offset++) {
		u8 PWMCTR;

		gpd_ecram_read(pwm_ctr_offset, &PWMCTR);

		if (PWMCTR != 0xB8)
			gpd_ecram_write(pwm_ctr_offset, 0xB8);
	}

	return gpd_generic_read_rpm();
}

// Read value for fan1_input
static int gpd_read_rpm(void)
{
	switch (gpd_driver_priv.drvdata->board) {
	case win4_6800u:
	case win_mini:
	case duo:
		return gpd_generic_read_rpm();
	case win_max_2:
		return gpd_wm2_read_rpm();
	}

	return 0;
}

static int gpd_wm2_read_pwm(void)
{
	const struct gpd_fan_drvdata *const drvdata = gpd_driver_priv.drvdata;
	u8 var;

	gpd_ecram_read(drvdata->pwm_write, &var);

	// Match gpd_generic_write_pwm(u8) below
	return DIV_ROUND_CLOSEST((var - 1) * 255, (drvdata->pwm_max - 1));
}

// Read value for pwm1
static int gpd_read_pwm(void)
{
	switch (gpd_driver_priv.drvdata->board) {
	case win_mini:
	case duo:
	case win4_6800u:
		switch (gpd_driver_priv.pwm_enable) {
		case DISABLE:
			return 255;
		case MANUAL:
			return gpd_driver_priv.pwm_value;
		case AUTOMATIC:
			return -EOPNOTSUPP;
		}
		break;
	case win_max_2:
		return gpd_wm2_read_pwm();
	}
	return 0;
}

// PWM value's range in EC is 1 - pwm_max, cast 0 - 255 to it.
static inline u8 gpd_cast_pwm_range(u8 val)
{
	const struct gpd_fan_drvdata *const drvdata = gpd_driver_priv.drvdata;

	return DIV_ROUND_CLOSEST(val * (drvdata->pwm_max - 1), 255) + 1;
}

static void gpd_generic_write_pwm(u8 val)
{
	const struct gpd_fan_drvdata *const drvdata = gpd_driver_priv.drvdata;
	u8 pwm_reg;

	pwm_reg = gpd_cast_pwm_range(val);
	gpd_ecram_write(drvdata->pwm_write, pwm_reg);
}

static void gpd_duo_write_pwm(u8 val)
{
	const struct gpd_fan_drvdata *const drvdata = gpd_driver_priv.drvdata;
	u8 pwm_reg;

	pwm_reg = gpd_cast_pwm_range(val);
	gpd_ecram_write(drvdata->pwm_write, pwm_reg);
	gpd_ecram_write(drvdata->pwm_write + 1, pwm_reg);
}

// Write value for pwm1
static int gpd_write_pwm(u8 val)
{
	if (gpd_driver_priv.pwm_enable != MANUAL)
		return -EPERM;

	switch (gpd_driver_priv.drvdata->board) {
	case duo:
		gpd_duo_write_pwm(val);
		break;
	case win_mini:
	case win4_6800u:
	case win_max_2:
		gpd_generic_write_pwm(val);
		break;
	}

	return 0;
}

static void gpd_win_mini_set_pwm_enable(enum FAN_PWM_ENABLE pwm_enable)
{
	switch (pwm_enable) {
	case DISABLE:
		gpd_generic_write_pwm(255);
		break;
	case MANUAL:
		gpd_generic_write_pwm(gpd_driver_priv.pwm_value);
		break;
	case AUTOMATIC:
		gpd_ecram_write(gpd_driver_priv.drvdata->pwm_write, 0);
		break;
	}
}

static void gpd_duo_set_pwm_enable(enum FAN_PWM_ENABLE pwm_enable)
{
	switch (pwm_enable) {
	case DISABLE:
		gpd_duo_write_pwm(255);
		break;
	case MANUAL:
		gpd_duo_write_pwm(gpd_driver_priv.pwm_value);
		break;
	case AUTOMATIC:
		gpd_ecram_write(gpd_driver_priv.drvdata->pwm_write, 0);
		break;
	}
}

static void gpd_wm2_set_pwm_enable(enum FAN_PWM_ENABLE enable)
{
	const struct gpd_fan_drvdata *const drvdata = gpd_driver_priv.drvdata;

	switch (enable) {
	case DISABLE:
		gpd_generic_write_pwm(255);
		gpd_ecram_write(drvdata->manual_control_enable, 1);
		break;
	case MANUAL:
		gpd_generic_write_pwm(gpd_driver_priv.pwm_value);
		gpd_ecram_write(drvdata->manual_control_enable, 1);
		break;
	case AUTOMATIC:
		gpd_ecram_write(drvdata->manual_control_enable, 0);
		break;
	}
}

// Write value for pwm1_enable
static void gpd_set_pwm_enable(enum FAN_PWM_ENABLE enable)
{
	if (enable == MANUAL)
		// Set pwm_value to max firstly when switching to manual mode, in
		// consideration of device safety.
		gpd_driver_priv.pwm_value = 255;

	switch (gpd_driver_priv.drvdata->board) {
	case win_mini:
	case win4_6800u:
		gpd_win_mini_set_pwm_enable(enable);
		break;
	case duo:
		gpd_duo_set_pwm_enable(enable);
		break;
	case win_max_2:
		gpd_wm2_set_pwm_enable(enable);
		break;
	}
}

static umode_t gpd_fan_hwmon_is_visible(__always_unused const void *drvdata,
					enum hwmon_sensor_types type, u32 attr,
					__always_unused int channel)
{
	if (type == hwmon_fan && attr == hwmon_fan_input) {
		return 0444;
	} else if (type == hwmon_pwm) {
		switch (attr) {
		case hwmon_pwm_enable:
		case hwmon_pwm_input:
			return 0644;
		default:
			return 0;
		}
	}
	return 0;
}

static int gpd_fan_hwmon_read(__always_unused struct device *dev,
			      enum hwmon_sensor_types type, u32 attr,
			      __always_unused int channel, long *val)
{
	int ret;

	ret = mutex_lock_interruptible(&gpd_fan_sequence_lock);
	if (ret)
		return ret;

	if (type == hwmon_fan) {
		if (attr == hwmon_fan_input) {
			ret = gpd_read_rpm();

			if (ret < 0)
				goto OUT;

			*val = ret;
			ret = 0;
			goto OUT;
		}
	} else if (type == hwmon_pwm) {
		switch (attr) {
		case hwmon_pwm_enable:
			*val = gpd_driver_priv.pwm_enable;
			ret = 0;
			goto OUT;
		case hwmon_pwm_input:
			ret = gpd_read_pwm();

			if (ret < 0)
				goto OUT;

			*val = ret;
			ret = 0;
			goto OUT;
		}
	}

	ret = -EOPNOTSUPP;

OUT:
	mutex_unlock(&gpd_fan_sequence_lock);
	return ret;
}

static int gpd_fan_hwmon_write(__always_unused struct device *dev,
			       enum hwmon_sensor_types type, u32 attr,
			       __always_unused int channel, long val)
{
	int ret;

	ret = mutex_lock_interruptible(&gpd_fan_sequence_lock);
	if (ret)
		return ret;

	if (type == hwmon_pwm) {
		switch (attr) {
		case hwmon_pwm_enable:
			if (!in_range(val, 0, 3)) {
				ret = -EINVAL;
				goto OUT;
			}

			gpd_driver_priv.pwm_enable = val;

			gpd_set_pwm_enable(gpd_driver_priv.pwm_enable);
			ret = 0;
			goto OUT;
		case hwmon_pwm_input:
			if (!in_range(val, 0, 256)) {
				ret = -ERANGE;
				goto OUT;
			}

			gpd_driver_priv.pwm_value = val;

			ret = gpd_write_pwm(val);
			goto OUT;
		}
	}

	ret = -EOPNOTSUPP;

OUT:
	mutex_unlock(&gpd_fan_sequence_lock);
	return ret;
}

static const struct hwmon_ops gpd_fan_ops = {
	.is_visible = gpd_fan_hwmon_is_visible,
	.read = gpd_fan_hwmon_read,
	.write = gpd_fan_hwmon_write,
};

static const struct hwmon_channel_info *gpd_fan_hwmon_channel_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	NULL
};

static struct hwmon_chip_info gpd_fan_chip_info = {
	.ops = &gpd_fan_ops,
	.info = gpd_fan_hwmon_channel_info
};

static void gpd_win4_init_ec(void)
{
	u8 chip_id, chip_ver;

	gpd_ecram_read(0x2000, &chip_id);

	if (chip_id == 0x55) {
		gpd_ecram_read(0x1060, &chip_ver);
		gpd_ecram_write(0x1060, chip_ver | 0x80);
	}
}

static void gpd_init_ec(void)
{
	// The buggy firmware won't initialize EC properly on boot.
	// Before its initialization, reading RPM will always return 0,
	// and writing PWM will have no effect.
	// Initialize it manually on driver load.
	if (gpd_driver_priv.drvdata->board == win4_6800u)
		gpd_win4_init_ec();
}

static int gpd_fan_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct resource *region;
	const struct resource *res;
	const struct device *hwdev;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return dev_err_probe(dev, -EINVAL,
				     "Failed to get platform resource\n");

	region = devm_request_region(dev, res->start,
				     resource_size(res), DRIVER_NAME);
	if (!region)
		return dev_err_probe(dev, -EBUSY,
				     "Failed to request region\n");

	hwdev = devm_hwmon_device_register_with_info(dev,
						     DRIVER_NAME,
						     NULL,
						     &gpd_fan_chip_info,
						     NULL);
	if (IS_ERR(hwdev))
		return dev_err_probe(dev, PTR_ERR(hwdev),
				     "Failed to register hwmon device\n");

	gpd_init_ec();

	return 0;
}

static void gpd_fan_remove(__always_unused struct platform_device *pdev)
{
	gpd_driver_priv.pwm_enable = AUTOMATIC;
	gpd_set_pwm_enable(AUTOMATIC);
}

static struct platform_driver gpd_fan_driver = {
	.probe = gpd_fan_probe,
	.remove = gpd_fan_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};

static struct platform_device *gpd_fan_platform_device;

static int __init gpd_fan_init(void)
{
	const struct gpd_fan_drvdata *match = NULL;

	for (const struct gpd_fan_drvdata **p = gpd_module_drvdata; *p; p++) {
		if (strcmp(gpd_fan_board, (*p)->board_name) == 0) {
			match = *p;
			break;
		}
	}

	if (!match) {
		const struct dmi_system_id *dmi_match =
			dmi_first_match(dmi_table);
		if (dmi_match)
			match = dmi_match->driver_data;
	}

	if (!match)
		return -ENODEV;

	gpd_driver_priv.pwm_enable = AUTOMATIC;
	gpd_driver_priv.pwm_value = 255;
	gpd_driver_priv.drvdata = match;

	struct resource gpd_fan_resources[] = {
		{
			.start = match->addr_port,
			.end = match->data_port,
			.flags = IORESOURCE_IO,
		},
	};

	gpd_fan_platform_device = platform_create_bundle(&gpd_fan_driver,
							 gpd_fan_probe,
							 gpd_fan_resources,
							 1, NULL, 0);

	if (IS_ERR(gpd_fan_platform_device)) {
		pr_warn("Failed to create platform device\n");
		return PTR_ERR(gpd_fan_platform_device);
	}

	return 0;
}

static void __exit gpd_fan_exit(void)
{
	platform_device_unregister(gpd_fan_platform_device);
	platform_driver_unregister(&gpd_fan_driver);
}

MODULE_DEVICE_TABLE(dmi, dmi_table);

module_init(gpd_fan_init);
module_exit(gpd_fan_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cryolitia PukNgae <cryolitia@uniontech.com>");
MODULE_DESCRIPTION("GPD Devices fan control driver");
