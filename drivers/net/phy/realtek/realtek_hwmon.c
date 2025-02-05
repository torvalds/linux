// SPDX-License-Identifier: GPL-2.0+
/*
 * HWMON support for Realtek PHY's
 *
 * Author: Heiner Kallweit <hkallweit1@gmail.com>
 */

#include <linux/hwmon.h>
#include <linux/phy.h>

#include "realtek.h"

#define RTL822X_VND2_TSALRM				0xa662
#define RTL822X_VND2_TSRR				0xbd84
#define RTL822X_VND2_TSSR				0xb54c

static int rtl822x_hwmon_get_temp(int raw)
{
	if (raw >= 512)
		raw -= 1024;

	return 1000 * raw / 2;
}

static int rtl822x_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long *val)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int raw;

	switch (attr) {
	case hwmon_temp_input:
		raw = phy_read_mmd(phydev, MDIO_MMD_VEND2, RTL822X_VND2_TSRR) & 0x3ff;
		*val = rtl822x_hwmon_get_temp(raw);
		break;
	case hwmon_temp_max:
		/* Chip reduces speed to 1G if threshold is exceeded */
		raw = phy_read_mmd(phydev, MDIO_MMD_VEND2, RTL822X_VND2_TSSR) >> 6;
		*val = rtl822x_hwmon_get_temp(raw);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct hwmon_ops rtl822x_hwmon_ops = {
	.visible = 0444,
	.read = rtl822x_hwmon_read,
};

static const struct hwmon_channel_info * const rtl822x_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MAX),
	NULL
};

static const struct hwmon_chip_info rtl822x_hwmon_chip_info = {
	.ops = &rtl822x_hwmon_ops,
	.info = rtl822x_hwmon_info,
};

int rtl822x_hwmon_init(struct phy_device *phydev)
{
	struct device *hwdev, *dev = &phydev->mdio.dev;
	const char *name;

	/* Ensure over-temp alarm is reset. */
	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND2, RTL822X_VND2_TSALRM, 3);

	name = devm_hwmon_sanitize_name(dev, dev_name(dev));
	if (IS_ERR(name))
		return PTR_ERR(name);

	hwdev = devm_hwmon_device_register_with_info(dev, name, phydev,
						     &rtl822x_hwmon_chip_info,
						     NULL);
	return PTR_ERR_OR_ZERO(hwdev);
}
