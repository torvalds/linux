// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 IBM Corp.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include "pmbus.h"

enum max31785_regs {
	MFR_REVISION		= 0x9b,
	MFR_FAULT_RESPONSE	= 0xd9,
	MFR_TEMP_SENSOR_CONFIG	= 0xf0,
	MFR_FAN_CONFIG		= 0xf1,
	MFR_FAN_FAULT_LIMIT	= 0xf5,
};

#define MAX31785			0x3030
#define MAX31785A			0x3040
#define MAX31785B			0x3061

#define MFR_FAN_CONFIG_DUAL_TACH	BIT(12)
#define MFR_FAN_CONFIG_TSFO		BIT(9)
#define MFR_FAN_CONFIG_TACHO		BIT(8)
#define MFR_FAN_CONFIG_HEALTH		BIT(4)
#define MFR_FAN_CONFIG_ROTOR_HI_LO	BIT(3)
#define MFR_FAN_CONFIG_ROTOR		BIT(2)

#define MFR_FAULT_RESPONSE_MONITOR	BIT(0)

#define MAX31785_NR_PAGES		23
#define MAX31785_NR_FAN_PAGES		6

/*
 * MAX31785 dragons ahead
 *
 * We see weird issues where some transfers fail. There doesn't appear to be
 * any pattern to the problem, so below we wrap all the read/write calls with a
 * retry. The device provides no indication of this besides NACK'ing master
 * Txs; no bits are set in STATUS_BYTE to suggest anything has gone wrong.
 */

#define max31785_retry(_func, ...) ({					\
	/* All relevant functions return int, sue me */			\
	int _ret = _func(__VA_ARGS__);					\
	if (_ret == -EIO)						\
		_ret = _func(__VA_ARGS__);				\
	_ret;								\
})

static int max31785_i2c_smbus_read_byte_data(struct i2c_client *client,
					      int command)
{
	return max31785_retry(i2c_smbus_read_byte_data, client, command);
}


static int max31785_i2c_smbus_write_byte_data(struct i2c_client *client,
					      int command, u16 data)
{
	return max31785_retry(i2c_smbus_write_byte_data, client, command, data);
}

static int max31785_i2c_smbus_read_word_data(struct i2c_client *client,
					     int command)
{
	return max31785_retry(i2c_smbus_read_word_data, client, command);
}

static int max31785_i2c_smbus_write_word_data(struct i2c_client *client,
					      int command, u16 data)
{
	return max31785_retry(i2c_smbus_write_word_data, client, command, data);
}

static int max31785_pmbus_write_byte(struct i2c_client *client, int page,
				     u8 value)
{
	return max31785_retry(pmbus_write_byte, client, page, value);
}

static int max31785_pmbus_read_byte_data(struct i2c_client *client, int page,
					  int command)
{
	return max31785_retry(pmbus_read_byte_data, client, page, command);
}

static int max31785_pmbus_write_byte_data(struct i2c_client *client, int page,
					  int command, u16 data)
{
	return max31785_retry(pmbus_write_byte_data, client, page, command,
			      data);
}

static int max31785_pmbus_read_word_data(struct i2c_client *client, int page,
					 int phase, int command)
{
	return max31785_retry(pmbus_read_word_data, client, page, phase, command);
}

static int max31785_pmbus_write_word_data(struct i2c_client *client, int page,
					  int command, u16 data)
{
	return max31785_retry(pmbus_write_word_data, client, page, command,
			      data);
}

static int max31785_read_byte_data(struct i2c_client *client, int page,
				   int reg)
{
	switch (reg) {
	case PMBUS_VOUT_MODE:
		if (page >= MAX31785_NR_PAGES)
			return -ENOTSUPP;
		break;
	case PMBUS_FAN_CONFIG_12:
		if (page >= MAX31785_NR_PAGES)
			return max31785_pmbus_read_byte_data(client,
					page - MAX31785_NR_PAGES,
					reg);
		break;
	}

	return max31785_pmbus_read_byte_data(client, page, reg);
}

static int max31785_write_byte(struct i2c_client *client, int page, u8 value)
{
	if (page >= MAX31785_NR_PAGES)
		return -ENOTSUPP;

	return max31785_pmbus_write_byte(client, page, value);
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

	rc = pmbus_set_page(client, page, 0xff);
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

	config = max31785_pmbus_read_byte_data(client, page,
					       PMBUS_FAN_CONFIG_12);
	if (config < 0)
		return config;

	command = max31785_pmbus_read_word_data(client, page, 0xff,
						PMBUS_FAN_COMMAND_1);
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
				   int phase, int reg)
{
	u32 val;
	int rv;

	switch (reg) {
	case PMBUS_READ_FAN_SPEED_1:
		if (page < MAX31785_NR_PAGES)
			return max31785_pmbus_read_word_data(client, page, 0xff, reg);

		rv = max31785_read_long_data(client, page - MAX31785_NR_PAGES,
					     reg, &val);
		if (rv < 0)
			return rv;

		return (val >> 16) & 0xffff;
	case PMBUS_FAN_COMMAND_1:
		/*
		 * PMBUS_FAN_COMMAND_x is probed to judge whether or not to
		 * expose fan control registers.
		 *
		 * Don't expose fan_target attribute for virtual pages.
		 */
		if (page >= MAX31785_NR_PAGES)
			return -ENOTSUPP;
		break;
	case PMBUS_VIRT_FAN_TARGET_1:
		if (page >= MAX31785_NR_PAGES)
			return -ENOTSUPP;

		return -ENODATA;
	case PMBUS_VIRT_PWM_1:
		return max31785_get_pwm(client, page);
	case PMBUS_VIRT_PWM_ENABLE_1:
		return max31785_get_pwm_mode(client, page);
	default:
		if (page >= MAX31785_NR_PAGES)
			return -ENXIO;
		break;
	}

	if (reg >= PMBUS_VIRT_BASE)
		return -ENXIO;

	return max31785_pmbus_read_word_data(client, page, 0xff, reg);
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

static int max31785_update_fan(struct i2c_client *client, int page,
			       u8 config, u8 mask, u16 command)
{
	int from, rv;
	u8 to;

	from = max31785_pmbus_read_byte_data(client, page, PMBUS_FAN_CONFIG_12);
	if (from < 0)
		return from;

	to = (from & ~mask) | (config & mask);

	if (to != from) {
		rv = max31785_pmbus_write_byte_data(client, page,
						    PMBUS_FAN_CONFIG_12, to);
		if (rv < 0)
			return rv;
	}

	rv = max31785_pmbus_write_word_data(client, page, PMBUS_FAN_COMMAND_1,
					    command);

	return rv;
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

	return max31785_update_fan(client, page, config, PB_FAN_1_RPM, rate);
}

static int max31785_write_word_data(struct i2c_client *client, int page,
				    int reg, u16 word)
{
	switch (reg) {
	case PMBUS_VIRT_FAN_TARGET_1:
		return max31785_update_fan(client, page, PB_FAN_1_RPM,
					   PB_FAN_1_RPM, word);
	case PMBUS_VIRT_PWM_1:
		return max31785_update_fan(client, page, 0, PB_FAN_1_RPM,
					max31785_scale_pwm(word));
	case PMBUS_VIRT_PWM_ENABLE_1:
		return max31785_pwm_enable(client, page, word);
	default:
		break;
	}

	if (reg < PMBUS_VIRT_BASE)
		return max31785_pmbus_write_word_data(client, page, reg, word);

	return -ENXIO;
}

/*
 * Returns negative error codes if an unrecoverable problem is detected, 0 if a
 * recoverable problem is detected, or a positive value on success.
 */
static int max31785_of_fan_config(struct i2c_client *client,
				  struct pmbus_driver_info *info,
				  struct device_node *child)
{
	int mfr_cfg = 0, mfr_fault_resp = 0, pb_cfg;
	struct device *dev = &client->dev;
	char *lock_polarity = NULL;
	const char *sval;
	u32 page;
	u32 uval;
	int ret;

	if (!of_device_is_compatible(child, "pmbus-fan"))
		return 0;

	ret = of_property_read_u32(child, "reg", &page);
	if (ret < 0) {
		dev_err(&client->dev, "Missing valid reg property\n");
		return ret;
	}

	if (!(info->func[page] & PMBUS_HAVE_FAN12)) {
		dev_err(dev, "Page %d does not have fan capabilities\n", page);
		return -ENXIO;
	}

	ret = max31785_i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	pb_cfg = max31785_i2c_smbus_read_byte_data(client, PMBUS_FAN_CONFIG_12);
	if (pb_cfg < 0)
		return pb_cfg;

	if (of_property_read_bool(child->parent, "use-stored-presence")) {
		if (!(pb_cfg & PB_FAN_1_INSTALLED))
			dev_info(dev, "Fan %d is configured but not installed\n",
				 page);
	} else {
		pb_cfg |= PB_FAN_1_INSTALLED;
	}

	ret = of_property_read_string(child, "maxim,fan-rotor-input", &sval);
	if (ret < 0) {
		dev_err(dev, "Missing valid maxim,fan-rotor-input property for fan %d\n",
				page);
		return ret;
	}

	if (strcmp("tach", sval) && strcmp("lock", sval)) {
		dev_err(dev, "maxim,fan-rotor-input has invalid value for fan %d: %s\n",
				page, sval);
		return -EINVAL;
	} else if (!strcmp("lock", sval)) {
		mfr_cfg |= MFR_FAN_CONFIG_ROTOR;

		ret = max31785_i2c_smbus_write_word_data(client,
							 MFR_FAN_FAULT_LIMIT,
							 1);
		if (ret < 0)
			return ret;

		ret = of_property_read_string(child, "maxim,fan-lock-polarity",
					      &sval);
		if (ret < 0) {
			dev_err(dev, "Missing valid maxim,fan-lock-polarity property for fan %d\n",
					page);
			return ret;
		}

		if (strcmp("low", sval) && strcmp("high", sval)) {
			dev_err(dev, "maxim,fan-lock-polarity has invalid value for fan %d: %s\n",
					page, lock_polarity);
			return -EINVAL;
		} else if (!strcmp("high", sval))
			mfr_cfg |= MFR_FAN_CONFIG_ROTOR_HI_LO;
	}

	if (!of_property_read_string(child, "fan-mode", &sval)) {
		if (!strcmp("rpm", sval))
			pb_cfg |= PB_FAN_1_RPM;
		else if (!strcmp("pwm", sval))
			pb_cfg &= ~PB_FAN_1_RPM;
		else {
			dev_err(dev, "fan-mode has invalid value for fan %d: %s\n",
					page, sval);
			return -EINVAL;
		}
	}

	ret = of_property_read_u32(child, "tach-pulses", &uval);
	if (ret < 0) {
		pb_cfg &= ~PB_FAN_1_PULSE_MASK;
	} else if (uval && (uval - 1) < 4) {
		pb_cfg = ((pb_cfg & ~PB_FAN_1_PULSE_MASK) | ((uval - 1) << 4));
	} else {
		dev_err(dev, "tach-pulses has invalid value for fan %d: %u\n",
				page, uval);
		return -EINVAL;
	}

	if (of_property_read_bool(child, "maxim,fan-health"))
		mfr_cfg |= MFR_FAN_CONFIG_HEALTH;

	if (of_property_read_bool(child, "maxim,fan-no-watchdog") ||
		of_property_read_bool(child, "maxim,tmp-no-fault-ramp"))
		mfr_cfg |= MFR_FAN_CONFIG_TSFO;

	if (of_property_read_bool(child, "maxim,fan-dual-tach"))
		mfr_cfg |= MFR_FAN_CONFIG_DUAL_TACH;

	if (of_property_read_bool(child, "maxim,fan-no-fault-ramp"))
		mfr_cfg |= MFR_FAN_CONFIG_TACHO;

	if (!of_property_read_u32(child, "maxim,fan-startup", &uval)) {
		uval /= 2;
		if (uval < 5) {
			mfr_cfg |= uval;
		} else {
			dev_err(dev, "maxim,fan-startup has invalid value for fan %d: %u\n",
					page, uval);
			return -EINVAL;
		}
	}

	if (!of_property_read_u32(child, "maxim,fan-ramp", &uval)) {
		if (uval < 8) {
			mfr_cfg |= uval << 5;
		} else {
			dev_err(dev, "maxim,fan-ramp has invalid value for fan %d: %u\n",
					page, uval);
			return -EINVAL;
		}
	}

	if (!of_property_read_u32(child, "maxim,tmp-hysteresis", &uval)) {
		uval /= 2;
		uval -= 1;
		if (uval < 4) {
			mfr_cfg |= uval << 10;
		} else {
			dev_err(dev, "maxim,tmp-hysteresis has invalid value for fan %d, %u\n",
					page, uval);
			return -EINVAL;
		}
	}

	if (!of_property_read_u32(child, "maxim,fan-pwm-freq", &uval)) {
		u16 val;

		if (uval == 30) {
			val = 0;
		} else if (uval == 50) {
			val = 1;
		} else if (uval == 100) {
			val = 2;
		} else if (uval == 150) {
			val = 3;
		} else if (uval == 25000) {
			val = 7;
		} else {
			dev_err(dev, "maxim,fan-pwm-freq has invalid value for fan %d: %u\n",
					page, uval);
			return -EINVAL;
		}

		mfr_cfg |= val << 13;
	}

	if (of_property_read_bool(child, "maxim,fan-fault-pin-mon"))
		mfr_fault_resp |= MFR_FAULT_RESPONSE_MONITOR;

	ret = max31785_i2c_smbus_write_byte_data(client, PMBUS_FAN_CONFIG_12,
					pb_cfg & ~PB_FAN_1_INSTALLED);
	if (ret < 0)
		return ret;

	ret = max31785_i2c_smbus_write_word_data(client, MFR_FAN_CONFIG,
						 mfr_cfg);
	if (ret < 0)
		return ret;

	ret = max31785_i2c_smbus_write_byte_data(client, MFR_FAULT_RESPONSE,
						 mfr_fault_resp);
	if (ret < 0)
		return ret;

	ret = max31785_i2c_smbus_write_byte_data(client, PMBUS_FAN_CONFIG_12,
						 pb_cfg);
	if (ret < 0)
		return ret;

	/*
	 * Fans are on pages 0 - 5. If the page property of a fan node is
	 * greater than 5 we will have errored in checks above out above.
	 * Therefore we don't need to cope with values up to 31, and the int
	 * return type is enough.
	 *
	 * The bit mask return value is used to populate a bitfield of fans
	 * who are both configured in the devicetree _and_ reported as
	 * installed by the hardware. Any fans that are not configured in the
	 * devicetree but are reported as installed by the hardware will have
	 * their hardware configuration updated to unset the installed bit.
	 */
	return BIT(page);
}

static int max31785_of_tmp_config(struct i2c_client *client,
				  struct pmbus_driver_info *info,
				  struct device_node *child)
{
	struct device *dev = &client->dev;
	struct device_node *np;
	u16 mfr_tmp_cfg = 0;
	u32 page;
	u32 uval;
	int ret;
	int i;

	if (!of_device_is_compatible(child, "pmbus-temperature"))
		return 0;

	ret = of_property_read_u32(child, "reg", &page);
	if (ret < 0) {
		dev_err(&client->dev, "Missing valid reg property\n");
		return ret;
	}

	if (!(info->func[page] & PMBUS_HAVE_TEMP)) {
		dev_err(dev, "Page %d does not have temp capabilities\n", page);
		return -ENXIO;
	}

	ret = max31785_i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	if (!of_property_read_u32(child, "maxim,tmp-offset", &uval)) {
		if (uval < 32)
			mfr_tmp_cfg |= uval << 10;
	}

	i = 0;
	while ((np = of_parse_phandle(child, "maxim,tmp-fans", i))) {
		if (of_property_read_u32(np, "reg", &uval)) {
			dev_err(&client->dev, "Failed to read fan reg property for phandle index %d\n",
					i);
		} else {
			if (uval < 6)
				mfr_tmp_cfg |= BIT(uval);
			else
				dev_warn(&client->dev, "Invalid fan page: %d\n",
						uval);
		}
		i++;
	}

	ret = max31785_i2c_smbus_write_word_data(client, MFR_TEMP_SENSOR_CONFIG,
					mfr_tmp_cfg);
	if (ret < 0)
		return ret;

	return 0;
}

#define MAX31785_FAN_FUNCS \
	(PMBUS_HAVE_FAN12 | PMBUS_HAVE_STATUS_FAN12 | PMBUS_HAVE_PWM12)

#define MAX31785_TEMP_FUNCS \
	(PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP)

#define MAX31785_VOUT_FUNCS \
	(PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT)

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
		ret = max31785_i2c_smbus_write_byte_data(client, PMBUS_PAGE, i);
		if (ret < 0)
			return ret;

		ret = max31785_i2c_smbus_read_word_data(client, MFR_FAN_CONFIG);
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

static int max31785_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *child;
	struct pmbus_driver_info *info;
	bool dual_tach = false;
	int ret;
	u32 fans;
	int i;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	info = devm_kzalloc(dev, sizeof(struct pmbus_driver_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	*info = max31785_info;

	ret = max31785_i2c_smbus_write_byte_data(client, PMBUS_PAGE, 255);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_REVISION);
	if (ret < 0)
		return ret;

	if (ret == MAX31785A || ret == MAX31785B) {
		dual_tach = true;
	} else if (ret == MAX31785) {
		if (!strcmp("max31785a", client->name) ||
		    !strcmp("max31785b", client->name))
			dev_warn(dev, "Expected max31785a/b, found max31785: cannot provide secondary tachometer readings\n");
	} else {
		dev_err(dev, "Unrecognized MAX31785 revision: %x\n", ret);
		return -ENODEV;
	}

	fans = 0;
	for_each_child_of_node(dev->of_node, child) {
		ret = max31785_of_fan_config(client, info, child);
		if (ret < 0) {
			of_node_put(child);
			return ret;
		}

		if (ret)
			fans |= ret;

		ret = max31785_of_tmp_config(client, info, child);
		if (ret < 0) {
			of_node_put(child);
			return ret;
		}
	}

	for (i = 0; i < MAX31785_NR_PAGES; i++) {
		bool have_fan = !!(info->func[i] & PMBUS_HAVE_FAN12);
		bool fan_configured = !!(fans & BIT(i));

		if (!have_fan || fan_configured)
			continue;

		ret = max31785_i2c_smbus_write_byte_data(client, PMBUS_PAGE,
							 i);
		if (ret < 0)
			return ret;

		ret = max31785_i2c_smbus_read_byte_data(client,
							PMBUS_FAN_CONFIG_12);
		if (ret < 0)
			return ret;

		ret &= ~PB_FAN_1_INSTALLED;
		ret = max31785_i2c_smbus_write_word_data(client,
							 PMBUS_FAN_CONFIG_12,
							 ret);
		if (ret < 0)
			return ret;
	}

	if (dual_tach) {
		ret = max31785_configure_dual_tach(client, info);
		if (ret < 0)
			return ret;
	}

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id max31785_id[] = {
	{ "max31785", 0 },
	{ "max31785a", 0 },
	{ "max31785b", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, max31785_id);

static const struct of_device_id max31785_of_match[] = {
	{ .compatible = "maxim,max31785" },
	{ .compatible = "maxim,max31785a" },
	{ .compatible = "maxim,max31785b" },
	{ },
};

MODULE_DEVICE_TABLE(of, max31785_of_match);

static struct i2c_driver max31785_driver = {
	.driver = {
		.name = "max31785",
		.of_match_table = max31785_of_match,
	},
	.probe_new = max31785_probe,
	.id_table = max31785_id,
};

module_i2c_driver(max31785_driver);

MODULE_AUTHOR("Andrew Jeffery <andrew@aj.id.au>");
MODULE_DESCRIPTION("PMBus driver for the Maxim MAX31785");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
