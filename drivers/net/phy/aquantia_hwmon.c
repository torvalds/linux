// SPDX-License-Identifier: GPL-2.0
/* HWMON driver for Aquantia PHY
 *
 * Author: Nikita Yushchenko <nikita.yoush@cogentembedded.com>
 * Author: Andrew Lunn <andrew@lunn.ch>
 * Author: Heiner Kallweit <hkallweit1@gmail.com>
 */

#include <linux/phy.h>
#include <linux/device.h>
#include <linux/ctype.h>
#include <linux/hwmon.h>

#include "aquantia.h"

/* Vendor specific 1, MDIO_MMD_VEND2 */
#define VEND1_THERMAL_PROV_HIGH_TEMP_FAIL	0xc421
#define VEND1_THERMAL_PROV_LOW_TEMP_FAIL	0xc422
#define VEND1_THERMAL_PROV_HIGH_TEMP_WARN	0xc423
#define VEND1_THERMAL_PROV_LOW_TEMP_WARN	0xc424
#define VEND1_THERMAL_STAT1			0xc820
#define VEND1_THERMAL_STAT2			0xc821
#define VEND1_THERMAL_STAT2_VALID		BIT(0)
#define VEND1_GENERAL_STAT1			0xc830
#define VEND1_GENERAL_STAT1_HIGH_TEMP_FAIL	BIT(14)
#define VEND1_GENERAL_STAT1_LOW_TEMP_FAIL	BIT(13)
#define VEND1_GENERAL_STAT1_HIGH_TEMP_WARN	BIT(12)
#define VEND1_GENERAL_STAT1_LOW_TEMP_WARN	BIT(11)

#if IS_REACHABLE(CONFIG_HWMON)

static umode_t aqr_hwmon_is_visible(const void *data,
				    enum hwmon_sensor_types type,
				    u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_lcrit_alarm:
	case hwmon_temp_crit_alarm:
		return 0444;
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_lcrit:
	case hwmon_temp_crit:
		return 0644;
	default:
		return 0;
	}
}

static int aqr_hwmon_get(struct phy_device *phydev, int reg, long *value)
{
	int temp = phy_read_mmd(phydev, MDIO_MMD_VEND1, reg);

	if (temp < 0)
		return temp;

	/* 16 bit value is 2's complement with LSB = 1/256th degree Celsius */
	*value = (s16)temp * 1000 / 256;

	return 0;
}

static int aqr_hwmon_set(struct phy_device *phydev, int reg, long value)
{
	int temp;

	if (value >= 128000 || value < -128000)
		return -ERANGE;

	temp = value * 256 / 1000;

	/* temp is in s16 range and we're interested in lower 16 bits only */
	return phy_write_mmd(phydev, MDIO_MMD_VEND1, reg, (u16)temp);
}

static int aqr_hwmon_test_bit(struct phy_device *phydev, int reg, int bit)
{
	int val = phy_read_mmd(phydev, MDIO_MMD_VEND1, reg);

	if (val < 0)
		return val;

	return !!(val & bit);
}

static int aqr_hwmon_status1(struct phy_device *phydev, int bit, long *value)
{
	int val = aqr_hwmon_test_bit(phydev, VEND1_GENERAL_STAT1, bit);

	if (val < 0)
		return val;

	*value = val;

	return 0;
}

static int aqr_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long *value)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int reg;

	if (type != hwmon_temp)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_input:
		reg = aqr_hwmon_test_bit(phydev, VEND1_THERMAL_STAT2,
					 VEND1_THERMAL_STAT2_VALID);
		if (reg < 0)
			return reg;
		if (!reg)
			return -EBUSY;

		return aqr_hwmon_get(phydev, VEND1_THERMAL_STAT1, value);

	case hwmon_temp_lcrit:
		return aqr_hwmon_get(phydev, VEND1_THERMAL_PROV_LOW_TEMP_FAIL,
				     value);
	case hwmon_temp_min:
		return aqr_hwmon_get(phydev, VEND1_THERMAL_PROV_LOW_TEMP_WARN,
				     value);
	case hwmon_temp_max:
		return aqr_hwmon_get(phydev, VEND1_THERMAL_PROV_HIGH_TEMP_WARN,
				     value);
	case hwmon_temp_crit:
		return aqr_hwmon_get(phydev, VEND1_THERMAL_PROV_HIGH_TEMP_FAIL,
				     value);
	case hwmon_temp_lcrit_alarm:
		return aqr_hwmon_status1(phydev,
					 VEND1_GENERAL_STAT1_LOW_TEMP_FAIL,
					 value);
	case hwmon_temp_min_alarm:
		return aqr_hwmon_status1(phydev,
					 VEND1_GENERAL_STAT1_LOW_TEMP_WARN,
					 value);
	case hwmon_temp_max_alarm:
		return aqr_hwmon_status1(phydev,
					 VEND1_GENERAL_STAT1_HIGH_TEMP_WARN,
					 value);
	case hwmon_temp_crit_alarm:
		return aqr_hwmon_status1(phydev,
					 VEND1_GENERAL_STAT1_HIGH_TEMP_FAIL,
					 value);
	default:
		return -EOPNOTSUPP;
	}
}

static int aqr_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long value)
{
	struct phy_device *phydev = dev_get_drvdata(dev);

	if (type != hwmon_temp)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_lcrit:
		return aqr_hwmon_set(phydev, VEND1_THERMAL_PROV_LOW_TEMP_FAIL,
				     value);
	case hwmon_temp_min:
		return aqr_hwmon_set(phydev, VEND1_THERMAL_PROV_LOW_TEMP_WARN,
				     value);
	case hwmon_temp_max:
		return aqr_hwmon_set(phydev, VEND1_THERMAL_PROV_HIGH_TEMP_WARN,
				     value);
	case hwmon_temp_crit:
		return aqr_hwmon_set(phydev, VEND1_THERMAL_PROV_HIGH_TEMP_FAIL,
				     value);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops aqr_hwmon_ops = {
	.is_visible = aqr_hwmon_is_visible,
	.read = aqr_hwmon_read,
	.write = aqr_hwmon_write,
};

static u32 aqr_hwmon_chip_config[] = {
	HWMON_C_REGISTER_TZ,
	0,
};

static const struct hwmon_channel_info aqr_hwmon_chip = {
	.type = hwmon_chip,
	.config = aqr_hwmon_chip_config,
};

static u32 aqr_hwmon_temp_config[] = {
	HWMON_T_INPUT |
	HWMON_T_MAX | HWMON_T_MIN |
	HWMON_T_MAX_ALARM | HWMON_T_MIN_ALARM |
	HWMON_T_CRIT | HWMON_T_LCRIT |
	HWMON_T_CRIT_ALARM | HWMON_T_LCRIT_ALARM,
	0,
};

static const struct hwmon_channel_info aqr_hwmon_temp = {
	.type = hwmon_temp,
	.config = aqr_hwmon_temp_config,
};

static const struct hwmon_channel_info * const aqr_hwmon_info[] = {
	&aqr_hwmon_chip,
	&aqr_hwmon_temp,
	NULL,
};

static const struct hwmon_chip_info aqr_hwmon_chip_info = {
	.ops = &aqr_hwmon_ops,
	.info = aqr_hwmon_info,
};

int aqr_hwmon_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct device *hwmon_dev;
	char *hwmon_name;
	int i, j;

	hwmon_name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!hwmon_name)
		return -ENOMEM;

	for (i = j = 0; hwmon_name[i]; i++) {
		if (isalnum(hwmon_name[i])) {
			if (i != j)
				hwmon_name[j] = hwmon_name[i];
			j++;
		}
	}
	hwmon_name[j] = '\0';

	hwmon_dev = devm_hwmon_device_register_with_info(dev, hwmon_name,
					phydev, &aqr_hwmon_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

#endif
