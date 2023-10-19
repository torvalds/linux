// SPDX-License-Identifier: GPL-2.0-or-later
/***************************************************************************
 *   Copyright (C) 2010-2012 Hans de Goede <hdegoede@redhat.com>           *
 *                                                                         *
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include "sch56xx-common.h"

#define DRVNAME "sch5627"
#define DEVNAME DRVNAME /* We only support one model */

#define SCH5627_HWMON_ID		0xa5
#define SCH5627_COMPANY_ID		0x5c
#define SCH5627_PRIMARY_ID		0xa0

#define SCH5627_REG_BUILD_CODE		0x39
#define SCH5627_REG_BUILD_ID		0x3a
#define SCH5627_REG_HWMON_ID		0x3c
#define SCH5627_REG_HWMON_REV		0x3d
#define SCH5627_REG_COMPANY_ID		0x3e
#define SCH5627_REG_PRIMARY_ID		0x3f
#define SCH5627_REG_CTRL		0x40

#define SCH5627_NO_TEMPS		8
#define SCH5627_NO_FANS			4
#define SCH5627_NO_IN			5

static const u16 SCH5627_REG_TEMP_MSB[SCH5627_NO_TEMPS] = {
	0x2B, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x180, 0x181 };
static const u16 SCH5627_REG_TEMP_LSN[SCH5627_NO_TEMPS] = {
	0xE2, 0xE1, 0xE1, 0xE5, 0xE5, 0xE6, 0x182, 0x182 };
static const u16 SCH5627_REG_TEMP_HIGH_NIBBLE[SCH5627_NO_TEMPS] = {
	0, 0, 1, 1, 0, 0, 0, 1 };
static const u16 SCH5627_REG_TEMP_HIGH[SCH5627_NO_TEMPS] = {
	0x61, 0x57, 0x59, 0x5B, 0x5D, 0x5F, 0x184, 0x186 };
static const u16 SCH5627_REG_TEMP_ABS[SCH5627_NO_TEMPS] = {
	0x9B, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x1A8, 0x1A9 };

static const u16 SCH5627_REG_FAN[SCH5627_NO_FANS] = {
	0x2C, 0x2E, 0x30, 0x32 };
static const u16 SCH5627_REG_FAN_MIN[SCH5627_NO_FANS] = {
	0x62, 0x64, 0x66, 0x68 };

static const u16 SCH5627_REG_PWM_MAP[SCH5627_NO_FANS] = {
	0xA0, 0xA1, 0xA2, 0xA3 };

static const u16 SCH5627_REG_IN_MSB[SCH5627_NO_IN] = {
	0x22, 0x23, 0x24, 0x25, 0x189 };
static const u16 SCH5627_REG_IN_LSN[SCH5627_NO_IN] = {
	0xE4, 0xE4, 0xE3, 0xE3, 0x18A };
static const u16 SCH5627_REG_IN_HIGH_NIBBLE[SCH5627_NO_IN] = {
	1, 0, 1, 0, 1 };
static const u16 SCH5627_REG_IN_FACTOR[SCH5627_NO_IN] = {
	10745, 3660, 9765, 10745, 3660 };
static const char * const SCH5627_IN_LABELS[SCH5627_NO_IN] = {
	"VCC", "VTT", "VBAT", "VTR", "V_IN" };

struct sch5627_data {
	unsigned short addr;
	u8 control;
	u8 temp_max[SCH5627_NO_TEMPS];
	u8 temp_crit[SCH5627_NO_TEMPS];
	u16 fan_min[SCH5627_NO_FANS];

	struct mutex update_lock;
	unsigned long last_battery;	/* In jiffies */
	char temp_valid;		/* !=0 if following fields are valid */
	char fan_valid;
	char in_valid;
	unsigned long temp_last_updated;	/* In jiffies */
	unsigned long fan_last_updated;
	unsigned long in_last_updated;
	u16 temp[SCH5627_NO_TEMPS];
	u16 fan[SCH5627_NO_FANS];
	u16 in[SCH5627_NO_IN];
};

static int sch5627_update_temp(struct sch5627_data *data)
{
	int ret = 0;
	int i, val;

	mutex_lock(&data->update_lock);

	/* Cache the values for 1 second */
	if (time_after(jiffies, data->temp_last_updated + HZ) || !data->temp_valid) {
		for (i = 0; i < SCH5627_NO_TEMPS; i++) {
			val = sch56xx_read_virtual_reg12(data->addr, SCH5627_REG_TEMP_MSB[i],
							 SCH5627_REG_TEMP_LSN[i],
							 SCH5627_REG_TEMP_HIGH_NIBBLE[i]);
			if (unlikely(val < 0)) {
				ret = val;
				goto abort;
			}
			data->temp[i] = val;
		}
		data->temp_last_updated = jiffies;
		data->temp_valid = 1;
	}
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int sch5627_update_fan(struct sch5627_data *data)
{
	int ret = 0;
	int i, val;

	mutex_lock(&data->update_lock);

	/* Cache the values for 1 second */
	if (time_after(jiffies, data->fan_last_updated + HZ) || !data->fan_valid) {
		for (i = 0; i < SCH5627_NO_FANS; i++) {
			val = sch56xx_read_virtual_reg16(data->addr, SCH5627_REG_FAN[i]);
			if (unlikely(val < 0)) {
				ret = val;
				goto abort;
			}
			data->fan[i] = val;
		}
		data->fan_last_updated = jiffies;
		data->fan_valid = 1;
	}
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int sch5627_update_in(struct sch5627_data *data)
{
	int ret = 0;
	int i, val;

	mutex_lock(&data->update_lock);

	/* Trigger a Vbat voltage measurement every 5 minutes */
	if (time_after(jiffies, data->last_battery + 300 * HZ)) {
		sch56xx_write_virtual_reg(data->addr, SCH5627_REG_CTRL, data->control | 0x10);
		data->last_battery = jiffies;
	}

	/* Cache the values for 1 second */
	if (time_after(jiffies, data->in_last_updated + HZ) || !data->in_valid) {
		for (i = 0; i < SCH5627_NO_IN; i++) {
			val = sch56xx_read_virtual_reg12(data->addr, SCH5627_REG_IN_MSB[i],
							 SCH5627_REG_IN_LSN[i],
							 SCH5627_REG_IN_HIGH_NIBBLE[i]);
			if (unlikely(val < 0)) {
				ret = val;
				goto abort;
			}
			data->in[i] = val;
		}
		data->in_last_updated = jiffies;
		data->in_valid = 1;
	}
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int sch5627_read_limits(struct sch5627_data *data)
{
	int i, val;

	for (i = 0; i < SCH5627_NO_TEMPS; i++) {
		/*
		 * Note what SMSC calls ABS, is what lm_sensors calls max
		 * (aka high), and HIGH is what lm_sensors calls crit.
		 */
		val = sch56xx_read_virtual_reg(data->addr,
					       SCH5627_REG_TEMP_ABS[i]);
		if (val < 0)
			return val;
		data->temp_max[i] = val;

		val = sch56xx_read_virtual_reg(data->addr,
					       SCH5627_REG_TEMP_HIGH[i]);
		if (val < 0)
			return val;
		data->temp_crit[i] = val;
	}
	for (i = 0; i < SCH5627_NO_FANS; i++) {
		val = sch56xx_read_virtual_reg16(data->addr,
						 SCH5627_REG_FAN_MIN[i]);
		if (val < 0)
			return val;
		data->fan_min[i] = val;
	}

	return 0;
}

static int reg_to_temp(u16 reg)
{
	return (reg * 625) / 10 - 64000;
}

static int reg_to_temp_limit(u8 reg)
{
	return (reg - 64) * 1000;
}

static int reg_to_rpm(u16 reg)
{
	if (reg == 0)
		return -EIO;
	if (reg == 0xffff)
		return 0;

	return 5400540 / reg;
}

static umode_t sch5627_is_visible(const void *drvdata, enum hwmon_sensor_types type, u32 attr,
				  int channel)
{
	if (type == hwmon_pwm && attr == hwmon_pwm_auto_channels_temp)
		return 0644;

	return 0444;
}

static int sch5627_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			long *val)
{
	struct sch5627_data *data = dev_get_drvdata(dev);
	int ret;

	switch (type) {
	case hwmon_temp:
		ret = sch5627_update_temp(data);
		if (ret < 0)
			return ret;
		switch (attr) {
		case hwmon_temp_input:
			*val = reg_to_temp(data->temp[channel]);
			return 0;
		case hwmon_temp_max:
			*val = reg_to_temp_limit(data->temp_max[channel]);
			return 0;
		case hwmon_temp_crit:
			*val = reg_to_temp_limit(data->temp_crit[channel]);
			return 0;
		case hwmon_temp_fault:
			*val = (data->temp[channel] == 0);
			return 0;
		default:
			break;
		}
		break;
	case hwmon_fan:
		ret = sch5627_update_fan(data);
		if (ret < 0)
			return ret;
		switch (attr) {
		case hwmon_fan_input:
			ret = reg_to_rpm(data->fan[channel]);
			if (ret < 0)
				return ret;
			*val = ret;
			return 0;
		case hwmon_fan_min:
			ret = reg_to_rpm(data->fan_min[channel]);
			if (ret < 0)
				return ret;
			*val = ret;
			return 0;
		case hwmon_fan_fault:
			*val = (data->fan[channel] == 0xffff);
			return 0;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_auto_channels_temp:
			mutex_lock(&data->update_lock);
			ret = sch56xx_read_virtual_reg(data->addr, SCH5627_REG_PWM_MAP[channel]);
			mutex_unlock(&data->update_lock);

			if (ret < 0)
				return ret;

			*val = ret;

			return 0;
		default:
			break;
		}
		break;
	case hwmon_in:
		ret = sch5627_update_in(data);
		if (ret < 0)
			return ret;
		switch (attr) {
		case hwmon_in_input:
			*val = DIV_ROUND_CLOSEST(data->in[channel] * SCH5627_REG_IN_FACTOR[channel],
						 10000);
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int sch5627_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			       int channel, const char **str)
{
	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_label:
			*str = SCH5627_IN_LABELS[channel];
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int sch5627_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			 long val)
{
	struct sch5627_data *data = dev_get_drvdata(dev);
	int ret;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_auto_channels_temp:
			/* registers are 8 bit wide */
			if (val > U8_MAX || val < 0)
				return -EINVAL;

			mutex_lock(&data->update_lock);
			ret = sch56xx_write_virtual_reg(data->addr, SCH5627_REG_PWM_MAP[channel],
							val);
			mutex_unlock(&data->update_lock);

			return ret;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops sch5627_ops = {
	.is_visible = sch5627_is_visible,
	.read = sch5627_read,
	.read_string = sch5627_read_string,
	.write = sch5627_write,
};

static const struct hwmon_channel_info *sch5627_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_FAULT,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_FAULT
			   ),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_FAULT
			   ),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_AUTO_CHANNELS_TEMP,
			   HWMON_PWM_AUTO_CHANNELS_TEMP,
			   HWMON_PWM_AUTO_CHANNELS_TEMP,
			   HWMON_PWM_AUTO_CHANNELS_TEMP
			   ),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT
			   ),
	NULL
};

static const struct hwmon_chip_info sch5627_chip_info = {
	.ops = &sch5627_ops,
	.info = sch5627_info,
};

static int sch5627_probe(struct platform_device *pdev)
{
	struct sch5627_data *data;
	struct device *hwmon_dev;
	int err, build_code, build_id, hwmon_rev, val;

	data = devm_kzalloc(&pdev->dev, sizeof(struct sch5627_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->addr = platform_get_resource(pdev, IORESOURCE_IO, 0)->start;
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	val = sch56xx_read_virtual_reg(data->addr, SCH5627_REG_HWMON_ID);
	if (val < 0)
		return val;

	if (val != SCH5627_HWMON_ID) {
		pr_err("invalid %s id: 0x%02X (expected 0x%02X)\n", "hwmon",
		       val, SCH5627_HWMON_ID);
		return -ENODEV;
	}

	val = sch56xx_read_virtual_reg(data->addr, SCH5627_REG_COMPANY_ID);
	if (val < 0)
		return val;

	if (val != SCH5627_COMPANY_ID) {
		pr_err("invalid %s id: 0x%02X (expected 0x%02X)\n", "company",
		       val, SCH5627_COMPANY_ID);
		return -ENODEV;
	}

	val = sch56xx_read_virtual_reg(data->addr, SCH5627_REG_PRIMARY_ID);
	if (val < 0)
		return val;

	if (val != SCH5627_PRIMARY_ID) {
		pr_err("invalid %s id: 0x%02X (expected 0x%02X)\n", "primary",
		       val, SCH5627_PRIMARY_ID);
		return -ENODEV;
	}

	build_code = sch56xx_read_virtual_reg(data->addr,
					      SCH5627_REG_BUILD_CODE);
	if (build_code < 0)
		return build_code;

	build_id = sch56xx_read_virtual_reg16(data->addr,
					      SCH5627_REG_BUILD_ID);
	if (build_id < 0)
		return build_id;

	hwmon_rev = sch56xx_read_virtual_reg(data->addr,
					     SCH5627_REG_HWMON_REV);
	if (hwmon_rev < 0)
		return hwmon_rev;

	val = sch56xx_read_virtual_reg(data->addr, SCH5627_REG_CTRL);
	if (val < 0)
		return val;

	data->control = val;
	if (!(data->control & 0x01)) {
		pr_err("hardware monitoring not enabled\n");
		return -ENODEV;
	}
	/* Trigger a Vbat voltage measurement, so that we get a valid reading
	   the first time we read Vbat */
	sch56xx_write_virtual_reg(data->addr, SCH5627_REG_CTRL,
				  data->control | 0x10);
	data->last_battery = jiffies;

	/*
	 * Read limits, we do this only once as reading a register on
	 * the sch5627 is quite expensive (and they don't change).
	 */
	err = sch5627_read_limits(data);
	if (err)
		return err;

	pr_info("found %s chip at %#hx\n", DEVNAME, data->addr);
	pr_info("firmware build: code 0x%02X, id 0x%04X, hwmon: rev 0x%02X\n",
		build_code, build_id, hwmon_rev);

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev, DEVNAME, data,
							 &sch5627_chip_info, NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	/* Note failing to register the watchdog is not a fatal error */
	sch56xx_watchdog_register(&pdev->dev, data->addr,
				  (build_code << 24) | (build_id << 8) | hwmon_rev,
				  &data->update_lock, 1);

	return 0;
}

static const struct platform_device_id sch5627_device_id[] = {
	{
		.name = "sch5627",
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, sch5627_device_id);

static struct platform_driver sch5627_driver = {
	.driver = {
		.name	= DRVNAME,
	},
	.probe		= sch5627_probe,
	.id_table	= sch5627_device_id,
};

module_platform_driver(sch5627_driver);

MODULE_DESCRIPTION("SMSC SCH5627 Hardware Monitoring Driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
