/*
 * Copyright (C) 2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include "pmbus.h"

enum max31785_regs {
	MFR_REVISION		= 0x9b,
	MFR_FAN_CONFIG		= 0xf1,
};

#define MAX31785			0x3030
#define MAX31785A			0x3040

#define MFR_FAN_CONFIG_DUAL_TACH	BIT(12)

#define MAX31785_NR_PAGES		23
#define MAX31785_NR_FAN_PAGES		6

static int max31785_read_byte_data(struct i2c_client *client, int page,
				   int reg)
{
	if (page < MAX31785_NR_PAGES)
		return -ENODATA;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		return -ENOTSUPP;
	case PMBUS_FAN_CONFIG_12:
		return pmbus_read_byte_data(client, page - MAX31785_NR_PAGES,
					    reg);
	}

	return -ENODATA;
}

static int max31785_write_byte(struct i2c_client *client, int page, u8 value)
{
	if (page < MAX31785_NR_PAGES)
		return -ENODATA;

	return -ENOTSUPP;
}

static int max31785_read_long_data(struct i2c_client *client, int page,
				   int reg, u32 *data)
{
	unsigned char cmdbuf[1];
	unsigned char rspbuf[4];
	int rc;

	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = sizeof(cmdbuf),
			.buf = cmdbuf,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = sizeof(rspbuf),
			.buf = rspbuf,
		},
	};

	cmdbuf[0] = reg;

	rc = pmbus_set_page(client, page);
	if (rc < 0)
		return rc;

	rc = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (rc < 0)
		return rc;

	*data = (rspbuf[0] << (0 * 8)) | (rspbuf[1] << (1 * 8)) |
		(rspbuf[2] << (2 * 8)) | (rspbuf[3] << (3 * 8));

	return rc;
}

static int max31785_get_pwm(struct i2c_client *client, int page)
{
	int rv;

	rv = pmbus_get_fan_rate_device(client, page, 0, percent);
	if (rv < 0)
		return rv;
	else if (rv >= 0x8000)
		return 0;
	else if (rv >= 0x2711)
		return 0x2710;

	return rv;
}

static int max31785_get_pwm_mode(struct i2c_client *client, int page)
{
	int config;
	int command;

	config = pmbus_read_byte_data(client, page, PMBUS_FAN_CONFIG_12);
	if (config < 0)
		return config;

	command = pmbus_read_word_data(client, page, PMBUS_FAN_COMMAND_1);
	if (command < 0)
		return command;

	if (config & PB_FAN_1_RPM)
		return (command >= 0x8000) ? 3 : 2;

	if (command >= 0x8000)
		return 3;
	else if (command >= 0x2711)
		return 0;

	return 1;
}

static int max31785_read_word_data(struct i2c_client *client, int page,
				   int reg)
{
	u32 val;
	int rv;

	switch (reg) {
	case PMBUS_READ_FAN_SPEED_1:
		if (page < MAX31785_NR_PAGES)
			return -ENODATA;

		rv = max31785_read_long_data(client, page - MAX31785_NR_PAGES,
					     reg, &val);
		if (rv < 0)
			return rv;

		rv = (val >> 16) & 0xffff;
		break;
	case PMBUS_FAN_COMMAND_1:
		/*
		 * PMBUS_FAN_COMMAND_x is probed to judge whether or not to
		 * expose fan control registers.
		 *
		 * Don't expose fan_target attribute for virtual pages.
		 */
		rv = (page >= MAX31785_NR_PAGES) ? -ENOTSUPP : -ENODATA;
		break;
	case PMBUS_VIRT_PWM_1:
		rv = max31785_get_pwm(client, page);
		break;
	case PMBUS_VIRT_PWM_ENABLE_1:
		rv = max31785_get_pwm_mode(client, page);
		break;
	default:
		rv = -ENODATA;
		break;
	}

	return rv;
}

static inline u32 max31785_scale_pwm(u32 sensor_val)
{
	/*
	 * The datasheet describes the accepted value range for manual PWM as
	 * [0, 0x2710], while the hwmon pwmX sysfs interface accepts values in
	 * [0, 255]. The MAX31785 uses DIRECT mode to scale the FAN_COMMAND
	 * registers and in PWM mode the coefficients are m=1, b=0, R=2. The
	 * important observation here is that 0x2710 == 10000 == 100 * 100.
	 *
	 * R=2 (== 10^2 == 100) accounts for scaling the value provided at the
	 * sysfs interface into the required hardware resolution, but it does
	 * not yet yield a value that we can write to the device (this initial
	 * scaling is handled by pmbus_data2reg()). Multiplying by 100 below
	 * translates the parameter value into the percentage units required by
	 * PMBus, and then we scale back by 255 as required by the hwmon pwmX
	 * interface to yield the percentage value at the appropriate
	 * resolution for hardware.
	 */
	return (sensor_val * 100) / 255;
}

static int max31785_pwm_enable(struct i2c_client *client, int page,
				    u16 word)
{
	int config = 0;
	int rate;

	switch (word) {
	case 0:
		rate = 0x7fff;
		break;
	case 1:
		rate = pmbus_get_fan_rate_cached(client, page, 0, percent);
		if (rate < 0)
			return rate;
		rate = max31785_scale_pwm(rate);
		break;
	case 2:
		config = PB_FAN_1_RPM;
		rate = pmbus_get_fan_rate_cached(client, page, 0, rpm);
		if (rate < 0)
			return rate;
		break;
	case 3:
		rate = 0xffff;
		break;
	default:
		return -EINVAL;
	}

	return pmbus_update_fan(client, page, 0, config, PB_FAN_1_RPM, rate);
}

static int max31785_write_word_data(struct i2c_client *client, int page,
				    int reg, u16 word)
{
	switch (reg) {
	case PMBUS_VIRT_PWM_1:
		return pmbus_update_fan(client, page, 0, 0, PB_FAN_1_RPM,
					max31785_scale_pwm(word));
	case PMBUS_VIRT_PWM_ENABLE_1:
		return max31785_pwm_enable(client, page, word);
	default:
		break;
	}

	return -ENODATA;
}

#define MAX31785_FAN_FUNCS \
	(PMBUS_HAVE_FAN12 | PMBUS_HAVE_STATUS_FAN12 | PMBUS_HAVE_PWM12)

#define MAX31785_TEMP_FUNCS \
	(PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP)

#define MAX31785_VOUT_FUNCS \
	(PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT)

#define MAX37185_NUM_FAN_PAGES 6

static const struct pmbus_driver_info max31785_info = {
	.pages = MAX31785_NR_PAGES,

	.write_word_data = max31785_write_word_data,
	.read_byte_data = max31785_read_byte_data,
	.read_word_data = max31785_read_word_data,
	.write_byte = max31785_write_byte,

	/* RPM */
	.format[PSC_FAN] = direct,
	.m[PSC_FAN] = 1,
	.b[PSC_FAN] = 0,
	.R[PSC_FAN] = 0,
	/* PWM */
	.format[PSC_PWM] = direct,
	.m[PSC_PWM] = 1,
	.b[PSC_PWM] = 0,
	.R[PSC_PWM] = 2,
	.func[0] = MAX31785_FAN_FUNCS,
	.func[1] = MAX31785_FAN_FUNCS,
	.func[2] = MAX31785_FAN_FUNCS,
	.func[3] = MAX31785_FAN_FUNCS,
	.func[4] = MAX31785_FAN_FUNCS,
	.func[5] = MAX31785_FAN_FUNCS,

	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_TEMPERATURE] = 1,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 2,
	.func[6]  = MAX31785_TEMP_FUNCS,
	.func[7]  = MAX31785_TEMP_FUNCS,
	.func[8]  = MAX31785_TEMP_FUNCS,
	.func[9]  = MAX31785_TEMP_FUNCS,
	.func[10] = MAX31785_TEMP_FUNCS,
	.func[11] = MAX31785_TEMP_FUNCS,
	.func[12] = MAX31785_TEMP_FUNCS,
	.func[13] = MAX31785_TEMP_FUNCS,
	.func[14] = MAX31785_TEMP_FUNCS,
	.func[15] = MAX31785_TEMP_FUNCS,
	.func[16] = MAX31785_TEMP_FUNCS,

	.format[PSC_VOLTAGE_OUT] = direct,
	.m[PSC_VOLTAGE_OUT] = 1,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = 0,
	.func[17] = MAX31785_VOUT_FUNCS,
	.func[18] = MAX31785_VOUT_FUNCS,
	.func[19] = MAX31785_VOUT_FUNCS,
	.func[20] = MAX31785_VOUT_FUNCS,
	.func[21] = MAX31785_VOUT_FUNCS,
	.func[22] = MAX31785_VOUT_FUNCS,
};

static int max31785_configure_dual_tach(struct i2c_client *client,
					struct pmbus_driver_info *info)
{
	int ret;
	int i;

	for (i = 0; i < MAX31785_NR_FAN_PAGES; i++) {
		ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, i);
		if (ret < 0)
			return ret;

		ret = i2c_smbus_read_word_data(client, MFR_FAN_CONFIG);
		if (ret < 0)
			return ret;

		if (ret & MFR_FAN_CONFIG_DUAL_TACH) {
			int virtual = MAX31785_NR_PAGES + i;

			info->pages = virtual + 1;
			info->func[virtual] |= PMBUS_HAVE_FAN12;
			info->func[virtual] |= PMBUS_PAGE_VIRTUAL;
		}
	}

	return 0;
}

static int max31785_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct pmbus_driver_info *info;
	bool dual_tach = false;
	s64 ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	info = devm_kzalloc(dev, sizeof(struct pmbus_driver_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	*info = max31785_info;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 255);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_REVISION);
	if (ret < 0)
		return ret;

	if (ret == MAX31785A) {
		dual_tach = true;
	} else if (ret == MAX31785) {
		if (!strcmp("max31785a", id->name))
			dev_warn(dev, "Expected max3175a, found max31785: cannot provide secondary tachometer readings\n");
	} else {
		return -ENODEV;
	}

	if (dual_tach) {
		ret = max31785_configure_dual_tach(client, info);
		if (ret < 0)
			return ret;
	}

	return pmbus_do_probe(client, id, info);
}

static const struct i2c_device_id max31785_id[] = {
	{ "max31785", 0 },
	{ "max31785a", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, max31785_id);

static struct i2c_driver max31785_driver = {
	.driver = {
		.name = "max31785",
	},
	.probe = max31785_probe,
	.remove = pmbus_do_remove,
	.id_table = max31785_id,
};

module_i2c_driver(max31785_driver);

MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_DESCRIPTION("PMBus driver for the Maxim MAX31785");
MODULE_LICENSE("GPL");
