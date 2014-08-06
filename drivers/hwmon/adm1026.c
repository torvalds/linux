/*
 * adm1026.c - Part of lm_sensors, Linux kernel modules for hardware
 *	       monitoring
 * Copyright (C) 2002, 2003  Philip Pokorny <ppokorny@penguincomputing.com>
 * Copyright (C) 2004 Justin Thiessen <jthiessen@penguincomputing.com>
 *
 * Chip details at:
 *
 * <http://www.onsemi.com/PowerSolutions/product.do?id=ADM1026>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

static int gpio_input[17] = { -1, -1, -1, -1, -1, -1, -1, -1, -1,
				-1, -1, -1, -1, -1, -1, -1, -1 };
static int gpio_output[17] = { -1, -1, -1, -1, -1, -1, -1, -1, -1,
				-1, -1, -1, -1, -1, -1, -1, -1 };
static int gpio_inverted[17] = { -1, -1, -1, -1, -1, -1, -1, -1, -1,
				-1, -1, -1, -1, -1, -1, -1, -1 };
static int gpio_normal[17] = { -1, -1, -1, -1, -1, -1, -1, -1, -1,
				-1, -1, -1, -1, -1, -1, -1, -1 };
static int gpio_fan[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
module_param_array(gpio_input, int, NULL, 0);
MODULE_PARM_DESC(gpio_input, "List of GPIO pins (0-16) to program as inputs");
module_param_array(gpio_output, int, NULL, 0);
MODULE_PARM_DESC(gpio_output,
		 "List of GPIO pins (0-16) to program as outputs");
module_param_array(gpio_inverted, int, NULL, 0);
MODULE_PARM_DESC(gpio_inverted,
		 "List of GPIO pins (0-16) to program as inverted");
module_param_array(gpio_normal, int, NULL, 0);
MODULE_PARM_DESC(gpio_normal,
		 "List of GPIO pins (0-16) to program as normal/non-inverted");
module_param_array(gpio_fan, int, NULL, 0);
MODULE_PARM_DESC(gpio_fan, "List of GPIO pins (0-7) to program as fan tachs");

/* Many ADM1026 constants specified below */

/* The ADM1026 registers */
#define ADM1026_REG_CONFIG1	0x00
#define CFG1_MONITOR		0x01
#define CFG1_INT_ENABLE		0x02
#define CFG1_INT_CLEAR		0x04
#define CFG1_AIN8_9		0x08
#define CFG1_THERM_HOT		0x10
#define CFG1_DAC_AFC		0x20
#define CFG1_PWM_AFC		0x40
#define CFG1_RESET		0x80

#define ADM1026_REG_CONFIG2	0x01
/* CONFIG2 controls FAN0/GPIO0 through FAN7/GPIO7 */

#define ADM1026_REG_CONFIG3	0x07
#define CFG3_GPIO16_ENABLE	0x01
#define CFG3_CI_CLEAR		0x02
#define CFG3_VREF_250		0x04
#define CFG3_GPIO16_DIR		0x40
#define CFG3_GPIO16_POL		0x80

#define ADM1026_REG_E2CONFIG	0x13
#define E2CFG_READ		0x01
#define E2CFG_WRITE		0x02
#define E2CFG_ERASE		0x04
#define E2CFG_ROM		0x08
#define E2CFG_CLK_EXT		0x80

/*
 * There are 10 general analog inputs and 7 dedicated inputs
 * They are:
 *    0 - 9  =  AIN0 - AIN9
 *       10  =  Vbat
 *       11  =  3.3V Standby
 *       12  =  3.3V Main
 *       13  =  +5V
 *       14  =  Vccp (CPU core voltage)
 *       15  =  +12V
 *       16  =  -12V
 */
static u16 ADM1026_REG_IN[] = {
		0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
		0x36, 0x37, 0x27, 0x29, 0x26, 0x2a,
		0x2b, 0x2c, 0x2d, 0x2e, 0x2f
	};
static u16 ADM1026_REG_IN_MIN[] = {
		0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d,
		0x5e, 0x5f, 0x6d, 0x49, 0x6b, 0x4a,
		0x4b, 0x4c, 0x4d, 0x4e, 0x4f
	};
static u16 ADM1026_REG_IN_MAX[] = {
		0x50, 0x51, 0x52, 0x53, 0x54, 0x55,
		0x56, 0x57, 0x6c, 0x41, 0x6a, 0x42,
		0x43, 0x44, 0x45, 0x46, 0x47
	};

/*
 * Temperatures are:
 *    0 - Internal
 *    1 - External 1
 *    2 - External 2
 */
static u16 ADM1026_REG_TEMP[] = { 0x1f, 0x28, 0x29 };
static u16 ADM1026_REG_TEMP_MIN[] = { 0x69, 0x48, 0x49 };
static u16 ADM1026_REG_TEMP_MAX[] = { 0x68, 0x40, 0x41 };
static u16 ADM1026_REG_TEMP_TMIN[] = { 0x10, 0x11, 0x12 };
static u16 ADM1026_REG_TEMP_THERM[] = { 0x0d, 0x0e, 0x0f };
static u16 ADM1026_REG_TEMP_OFFSET[] = { 0x1e, 0x6e, 0x6f };

#define ADM1026_REG_FAN(nr)		(0x38 + (nr))
#define ADM1026_REG_FAN_MIN(nr)		(0x60 + (nr))
#define ADM1026_REG_FAN_DIV_0_3		0x02
#define ADM1026_REG_FAN_DIV_4_7		0x03

#define ADM1026_REG_DAC			0x04
#define ADM1026_REG_PWM			0x05

#define ADM1026_REG_GPIO_CFG_0_3	0x08
#define ADM1026_REG_GPIO_CFG_4_7	0x09
#define ADM1026_REG_GPIO_CFG_8_11	0x0a
#define ADM1026_REG_GPIO_CFG_12_15	0x0b
/* CFG_16 in REG_CFG3 */
#define ADM1026_REG_GPIO_STATUS_0_7	0x24
#define ADM1026_REG_GPIO_STATUS_8_15	0x25
/* STATUS_16 in REG_STATUS4 */
#define ADM1026_REG_GPIO_MASK_0_7	0x1c
#define ADM1026_REG_GPIO_MASK_8_15	0x1d
/* MASK_16 in REG_MASK4 */

#define ADM1026_REG_COMPANY		0x16
#define ADM1026_REG_VERSTEP		0x17
/* These are the recognized values for the above regs */
#define ADM1026_COMPANY_ANALOG_DEV	0x41
#define ADM1026_VERSTEP_GENERIC		0x40
#define ADM1026_VERSTEP_ADM1026		0x44

#define ADM1026_REG_MASK1		0x18
#define ADM1026_REG_MASK2		0x19
#define ADM1026_REG_MASK3		0x1a
#define ADM1026_REG_MASK4		0x1b

#define ADM1026_REG_STATUS1		0x20
#define ADM1026_REG_STATUS2		0x21
#define ADM1026_REG_STATUS3		0x22
#define ADM1026_REG_STATUS4		0x23

#define ADM1026_FAN_ACTIVATION_TEMP_HYST -6
#define ADM1026_FAN_CONTROL_TEMP_RANGE	20
#define ADM1026_PWM_MAX			255

/*
 * Conversions. Rounding and limit checking is only done on the TO_REG
 * variants. Note that you should be a bit careful with which arguments
 * these macros are called: arguments may be evaluated more than once.
 */

/*
 * IN are scaled according to built-in resistors.  These are the
 *   voltages corresponding to 3/4 of full scale (192 or 0xc0)
 *   NOTE: The -12V input needs an additional factor to account
 *      for the Vref pullup resistor.
 *      NEG12_OFFSET = SCALE * Vref / V-192 - Vref
 *                   = 13875 * 2.50 / 1.875 - 2500
 *                   = 16000
 *
 * The values in this table are based on Table II, page 15 of the
 *    datasheet.
 */
static int adm1026_scaling[] = { /* .001 Volts */
		2250, 2250, 2250, 2250, 2250, 2250,
		1875, 1875, 1875, 1875, 3000, 3330,
		3330, 4995, 2250, 12000, 13875
	};
#define NEG12_OFFSET  16000
#define SCALE(val, from, to) (((val)*(to) + ((from)/2))/(from))
#define INS_TO_REG(n, val)  (clamp_val(SCALE(val, adm1026_scaling[n], 192),\
	0, 255))
#define INS_FROM_REG(n, val) (SCALE(val, 192, adm1026_scaling[n]))

/*
 * FAN speed is measured using 22.5kHz clock and counts for 2 pulses
 *   and we assume a 2 pulse-per-rev fan tach signal
 *      22500 kHz * 60 (sec/min) * 2 (pulse) / 2 (pulse/rev) == 1350000
 */
#define FAN_TO_REG(val, div)  ((val) <= 0 ? 0xff : \
				clamp_val(1350000 / ((val) * (div)), \
					      1, 254))
#define FAN_FROM_REG(val, div) ((val) == 0 ? -1 : (val) == 0xff ? 0 : \
				1350000 / ((val) * (div)))
#define DIV_FROM_REG(val) (1 << (val))
#define DIV_TO_REG(val) ((val) >= 8 ? 3 : (val) >= 4 ? 2 : (val) >= 2 ? 1 : 0)

/* Temperature is reported in 1 degC increments */
#define TEMP_TO_REG(val) (clamp_val(((val) + ((val) < 0 ? -500 : 500)) \
					/ 1000, -127, 127))
#define TEMP_FROM_REG(val) ((val) * 1000)
#define OFFSET_TO_REG(val) (clamp_val(((val) + ((val) < 0 ? -500 : 500)) \
					  / 1000, -127, 127))
#define OFFSET_FROM_REG(val) ((val) * 1000)

#define PWM_TO_REG(val) (clamp_val(val, 0, 255))
#define PWM_FROM_REG(val) (val)

#define PWM_MIN_TO_REG(val) ((val) & 0xf0)
#define PWM_MIN_FROM_REG(val) (((val) & 0xf0) + ((val) >> 4))

/*
 * Analog output is a voltage, and scaled to millivolts.  The datasheet
 *   indicates that the DAC could be used to drive the fans, but in our
 *   example board (Arima HDAMA) it isn't connected to the fans at all.
 */
#define DAC_TO_REG(val) (clamp_val(((((val) * 255) + 500) / 2500), 0, 255))
#define DAC_FROM_REG(val) (((val) * 2500) / 255)

/*
 * Chip sampling rates
 *
 * Some sensors are not updated more frequently than once per second
 *    so it doesn't make sense to read them more often than that.
 *    We cache the results and return the saved data if the driver
 *    is called again before a second has elapsed.
 *
 * Also, there is significant configuration data for this chip
 *    So, we keep the config data up to date in the cache
 *    when it is written and only sample it once every 5 *minutes*
 */
#define ADM1026_DATA_INTERVAL		(1 * HZ)
#define ADM1026_CONFIG_INTERVAL		(5 * 60 * HZ)

/*
 * We allow for multiple chips in a single system.
 *
 * For each registered ADM1026, we need to keep state information
 * at client->data. The adm1026_data structure is dynamically
 * allocated, when a new client structure is allocated.
 */

struct pwm_data {
	u8 pwm;
	u8 enable;
	u8 auto_pwm_min;
};

struct adm1026_data {
	struct i2c_client *client;
	const struct attribute_group *groups[3];

	struct mutex update_lock;
	int valid;		/* !=0 if following fields are valid */
	unsigned long last_reading;	/* In jiffies */
	unsigned long last_config;	/* In jiffies */

	u8 in[17];		/* Register value */
	u8 in_max[17];		/* Register value */
	u8 in_min[17];		/* Register value */
	s8 temp[3];		/* Register value */
	s8 temp_min[3];		/* Register value */
	s8 temp_max[3];		/* Register value */
	s8 temp_tmin[3];	/* Register value */
	s8 temp_crit[3];	/* Register value */
	s8 temp_offset[3];	/* Register value */
	u8 fan[8];		/* Register value */
	u8 fan_min[8];		/* Register value */
	u8 fan_div[8];		/* Decoded value */
	struct pwm_data pwm1;	/* Pwm control values */
	u8 vrm;			/* VRM version */
	u8 analog_out;		/* Register value (DAC) */
	long alarms;		/* Register encoding, combined */
	long alarm_mask;	/* Register encoding, combined */
	long gpio;		/* Register encoding, combined */
	long gpio_mask;		/* Register encoding, combined */
	u8 gpio_config[17];	/* Decoded value */
	u8 config1;		/* Register value */
	u8 config2;		/* Register value */
	u8 config3;		/* Register value */
};

static int adm1026_read_value(struct i2c_client *client, u8 reg)
{
	int res;

	if (reg < 0x80) {
		/* "RAM" locations */
		res = i2c_smbus_read_byte_data(client, reg) & 0xff;
	} else {
		/* EEPROM, do nothing */
		res = 0;
	}
	return res;
}

static int adm1026_write_value(struct i2c_client *client, u8 reg, int value)
{
	int res;

	if (reg < 0x80) {
		/* "RAM" locations */
		res = i2c_smbus_write_byte_data(client, reg, value);
	} else {
		/* EEPROM, do nothing */
		res = 0;
	}
	return res;
}

static struct adm1026_data *adm1026_update_device(struct device *dev)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i;
	long value, alarms, gpio;

	mutex_lock(&data->update_lock);
	if (!data->valid
	    || time_after(jiffies,
			  data->last_reading + ADM1026_DATA_INTERVAL)) {
		/* Things that change quickly */
		dev_dbg(&client->dev, "Reading sensor values\n");
		for (i = 0; i <= 16; ++i) {
			data->in[i] =
			    adm1026_read_value(client, ADM1026_REG_IN[i]);
		}

		for (i = 0; i <= 7; ++i) {
			data->fan[i] =
			    adm1026_read_value(client, ADM1026_REG_FAN(i));
		}

		for (i = 0; i <= 2; ++i) {
			/*
			 * NOTE: temp[] is s8 and we assume 2's complement
			 *   "conversion" in the assignment
			 */
			data->temp[i] =
			    adm1026_read_value(client, ADM1026_REG_TEMP[i]);
		}

		data->pwm1.pwm = adm1026_read_value(client,
			ADM1026_REG_PWM);
		data->analog_out = adm1026_read_value(client,
			ADM1026_REG_DAC);
		/* GPIO16 is MSbit of alarms, move it to gpio */
		alarms = adm1026_read_value(client, ADM1026_REG_STATUS4);
		gpio = alarms & 0x80 ? 0x0100 : 0; /* GPIO16 */
		alarms &= 0x7f;
		alarms <<= 8;
		alarms |= adm1026_read_value(client, ADM1026_REG_STATUS3);
		alarms <<= 8;
		alarms |= adm1026_read_value(client, ADM1026_REG_STATUS2);
		alarms <<= 8;
		alarms |= adm1026_read_value(client, ADM1026_REG_STATUS1);
		data->alarms = alarms;

		/* Read the GPIO values */
		gpio |= adm1026_read_value(client,
			ADM1026_REG_GPIO_STATUS_8_15);
		gpio <<= 8;
		gpio |= adm1026_read_value(client,
			ADM1026_REG_GPIO_STATUS_0_7);
		data->gpio = gpio;

		data->last_reading = jiffies;
	}	/* last_reading */

	if (!data->valid ||
	    time_after(jiffies, data->last_config + ADM1026_CONFIG_INTERVAL)) {
		/* Things that don't change often */
		dev_dbg(&client->dev, "Reading config values\n");
		for (i = 0; i <= 16; ++i) {
			data->in_min[i] = adm1026_read_value(client,
				ADM1026_REG_IN_MIN[i]);
			data->in_max[i] = adm1026_read_value(client,
				ADM1026_REG_IN_MAX[i]);
		}

		value = adm1026_read_value(client, ADM1026_REG_FAN_DIV_0_3)
			| (adm1026_read_value(client, ADM1026_REG_FAN_DIV_4_7)
			<< 8);
		for (i = 0; i <= 7; ++i) {
			data->fan_min[i] = adm1026_read_value(client,
				ADM1026_REG_FAN_MIN(i));
			data->fan_div[i] = DIV_FROM_REG(value & 0x03);
			value >>= 2;
		}

		for (i = 0; i <= 2; ++i) {
			/*
			 * NOTE: temp_xxx[] are s8 and we assume 2's
			 *    complement "conversion" in the assignment
			 */
			data->temp_min[i] = adm1026_read_value(client,
				ADM1026_REG_TEMP_MIN[i]);
			data->temp_max[i] = adm1026_read_value(client,
				ADM1026_REG_TEMP_MAX[i]);
			data->temp_tmin[i] = adm1026_read_value(client,
				ADM1026_REG_TEMP_TMIN[i]);
			data->temp_crit[i] = adm1026_read_value(client,
				ADM1026_REG_TEMP_THERM[i]);
			data->temp_offset[i] = adm1026_read_value(client,
				ADM1026_REG_TEMP_OFFSET[i]);
		}

		/* Read the STATUS/alarm masks */
		alarms = adm1026_read_value(client, ADM1026_REG_MASK4);
		gpio = alarms & 0x80 ? 0x0100 : 0; /* GPIO16 */
		alarms = (alarms & 0x7f) << 8;
		alarms |= adm1026_read_value(client, ADM1026_REG_MASK3);
		alarms <<= 8;
		alarms |= adm1026_read_value(client, ADM1026_REG_MASK2);
		alarms <<= 8;
		alarms |= adm1026_read_value(client, ADM1026_REG_MASK1);
		data->alarm_mask = alarms;

		/* Read the GPIO values */
		gpio |= adm1026_read_value(client,
			ADM1026_REG_GPIO_MASK_8_15);
		gpio <<= 8;
		gpio |= adm1026_read_value(client, ADM1026_REG_GPIO_MASK_0_7);
		data->gpio_mask = gpio;

		/* Read various values from CONFIG1 */
		data->config1 = adm1026_read_value(client,
			ADM1026_REG_CONFIG1);
		if (data->config1 & CFG1_PWM_AFC) {
			data->pwm1.enable = 2;
			data->pwm1.auto_pwm_min =
				PWM_MIN_FROM_REG(data->pwm1.pwm);
		}
		/* Read the GPIO config */
		data->config2 = adm1026_read_value(client,
			ADM1026_REG_CONFIG2);
		data->config3 = adm1026_read_value(client,
			ADM1026_REG_CONFIG3);
		data->gpio_config[16] = (data->config3 >> 6) & 0x03;

		value = 0;
		for (i = 0; i <= 15; ++i) {
			if ((i & 0x03) == 0) {
				value = adm1026_read_value(client,
					    ADM1026_REG_GPIO_CFG_0_3 + i/4);
			}
			data->gpio_config[i] = value & 0x03;
			value >>= 2;
		}

		data->last_config = jiffies;
	}	/* last_config */

	data->valid = 1;
	mutex_unlock(&data->update_lock);
	return data;
}

static ssize_t show_in(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", INS_FROM_REG(nr, data->in[nr]));
}
static ssize_t show_in_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", INS_FROM_REG(nr, data->in_min[nr]));
}
static ssize_t set_in_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_min[nr] = INS_TO_REG(nr, val);
	adm1026_write_value(client, ADM1026_REG_IN_MIN[nr], data->in_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_in_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", INS_FROM_REG(nr, data->in_max[nr]));
}
static ssize_t set_in_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_max[nr] = INS_TO_REG(nr, val);
	adm1026_write_value(client, ADM1026_REG_IN_MAX[nr], data->in_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define in_reg(offset)						\
static SENSOR_DEVICE_ATTR(in##offset##_input, S_IRUGO, show_in,	\
		NULL, offset);					\
static SENSOR_DEVICE_ATTR(in##offset##_min, S_IRUGO | S_IWUSR,	\
		show_in_min, set_in_min, offset);		\
static SENSOR_DEVICE_ATTR(in##offset##_max, S_IRUGO | S_IWUSR,	\
		show_in_max, set_in_max, offset);


in_reg(0);
in_reg(1);
in_reg(2);
in_reg(3);
in_reg(4);
in_reg(5);
in_reg(6);
in_reg(7);
in_reg(8);
in_reg(9);
in_reg(10);
in_reg(11);
in_reg(12);
in_reg(13);
in_reg(14);
in_reg(15);

static ssize_t show_in16(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", INS_FROM_REG(16, data->in[16]) -
		NEG12_OFFSET);
}
static ssize_t show_in16_min(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", INS_FROM_REG(16, data->in_min[16])
		- NEG12_OFFSET);
}
static ssize_t set_in16_min(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_min[16] = INS_TO_REG(16, val + NEG12_OFFSET);
	adm1026_write_value(client, ADM1026_REG_IN_MIN[16], data->in_min[16]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_in16_max(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", INS_FROM_REG(16, data->in_max[16])
			- NEG12_OFFSET);
}
static ssize_t set_in16_max(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_max[16] = INS_TO_REG(16, val+NEG12_OFFSET);
	adm1026_write_value(client, ADM1026_REG_IN_MAX[16], data->in_max[16]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(in16_input, S_IRUGO, show_in16, NULL, 16);
static SENSOR_DEVICE_ATTR(in16_min, S_IRUGO | S_IWUSR, show_in16_min,
			  set_in16_min, 16);
static SENSOR_DEVICE_ATTR(in16_max, S_IRUGO | S_IWUSR, show_in16_max,
			  set_in16_max, 16);


/* Now add fan read/write functions */

static ssize_t show_fan(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[nr],
		data->fan_div[nr]));
}
static ssize_t show_fan_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[nr],
		data->fan_div[nr]));
}
static ssize_t set_fan_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN_TO_REG(val, data->fan_div[nr]);
	adm1026_write_value(client, ADM1026_REG_FAN_MIN(nr),
		data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define fan_offset(offset)						\
static SENSOR_DEVICE_ATTR(fan##offset##_input, S_IRUGO, show_fan, NULL,	\
		offset - 1);						\
static SENSOR_DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR,		\
		show_fan_min, set_fan_min, offset - 1);

fan_offset(1);
fan_offset(2);
fan_offset(3);
fan_offset(4);
fan_offset(5);
fan_offset(6);
fan_offset(7);
fan_offset(8);

/* Adjust fan_min to account for new fan divisor */
static void fixup_fan_min(struct device *dev, int fan, int old_div)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int new_min;
	int new_div = data->fan_div[fan];

	/* 0 and 0xff are special.  Don't adjust them */
	if (data->fan_min[fan] == 0 || data->fan_min[fan] == 0xff)
		return;

	new_min = data->fan_min[fan] * old_div / new_div;
	new_min = clamp_val(new_min, 1, 254);
	data->fan_min[fan] = new_min;
	adm1026_write_value(client, ADM1026_REG_FAN_MIN(fan), new_min);
}

/* Now add fan_div read/write functions */
static ssize_t show_fan_div(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", data->fan_div[nr]);
}
static ssize_t set_fan_div(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int orig_div, new_div;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	new_div = DIV_TO_REG(val);

	mutex_lock(&data->update_lock);
	orig_div = data->fan_div[nr];
	data->fan_div[nr] = DIV_FROM_REG(new_div);

	if (nr < 4) { /* 0 <= nr < 4 */
		adm1026_write_value(client, ADM1026_REG_FAN_DIV_0_3,
				    (DIV_TO_REG(data->fan_div[0]) << 0) |
				    (DIV_TO_REG(data->fan_div[1]) << 2) |
				    (DIV_TO_REG(data->fan_div[2]) << 4) |
				    (DIV_TO_REG(data->fan_div[3]) << 6));
	} else { /* 3 < nr < 8 */
		adm1026_write_value(client, ADM1026_REG_FAN_DIV_4_7,
				    (DIV_TO_REG(data->fan_div[4]) << 0) |
				    (DIV_TO_REG(data->fan_div[5]) << 2) |
				    (DIV_TO_REG(data->fan_div[6]) << 4) |
				    (DIV_TO_REG(data->fan_div[7]) << 6));
	}

	if (data->fan_div[nr] != orig_div)
		fixup_fan_min(dev, nr, orig_div);

	mutex_unlock(&data->update_lock);
	return count;
}

#define fan_offset_div(offset)						\
static SENSOR_DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR,		\
		show_fan_div, set_fan_div, offset - 1);

fan_offset_div(1);
fan_offset_div(2);
fan_offset_div(3);
fan_offset_div(4);
fan_offset_div(5);
fan_offset_div(6);
fan_offset_div(7);
fan_offset_div(8);

/* Temps */
static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[nr]));
}
static ssize_t show_temp_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_min[nr]));
}
static ssize_t set_temp_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_min[nr] = TEMP_TO_REG(val);
	adm1026_write_value(client, ADM1026_REG_TEMP_MIN[nr],
		data->temp_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_temp_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[nr]));
}
static ssize_t set_temp_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_max[nr] = TEMP_TO_REG(val);
	adm1026_write_value(client, ADM1026_REG_TEMP_MAX[nr],
		data->temp_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define temp_reg(offset)						\
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO, show_temp,	\
		NULL, offset - 1);					\
static SENSOR_DEVICE_ATTR(temp##offset##_min, S_IRUGO | S_IWUSR,	\
		show_temp_min, set_temp_min, offset - 1);		\
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IRUGO | S_IWUSR,	\
		show_temp_max, set_temp_max, offset - 1);


temp_reg(1);
temp_reg(2);
temp_reg(3);

static ssize_t show_temp_offset(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_offset[nr]));
}
static ssize_t set_temp_offset(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_offset[nr] = TEMP_TO_REG(val);
	adm1026_write_value(client, ADM1026_REG_TEMP_OFFSET[nr],
		data->temp_offset[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define temp_offset_reg(offset)						\
static SENSOR_DEVICE_ATTR(temp##offset##_offset, S_IRUGO | S_IWUSR,	\
		show_temp_offset, set_temp_offset, offset - 1);

temp_offset_reg(1);
temp_offset_reg(2);
temp_offset_reg(3);

static ssize_t show_temp_auto_point1_temp_hyst(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(
		ADM1026_FAN_ACTIVATION_TEMP_HYST + data->temp_tmin[nr]));
}
static ssize_t show_temp_auto_point2_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_tmin[nr] +
		ADM1026_FAN_CONTROL_TEMP_RANGE));
}
static ssize_t show_temp_auto_point1_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_tmin[nr]));
}
static ssize_t set_temp_auto_point1_temp(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_tmin[nr] = TEMP_TO_REG(val);
	adm1026_write_value(client, ADM1026_REG_TEMP_TMIN[nr],
		data->temp_tmin[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define temp_auto_point(offset)						\
static SENSOR_DEVICE_ATTR(temp##offset##_auto_point1_temp,		\
		S_IRUGO | S_IWUSR, show_temp_auto_point1_temp,		\
		set_temp_auto_point1_temp, offset - 1);			\
static SENSOR_DEVICE_ATTR(temp##offset##_auto_point1_temp_hyst, S_IRUGO,\
		show_temp_auto_point1_temp_hyst, NULL, offset - 1);	\
static SENSOR_DEVICE_ATTR(temp##offset##_auto_point2_temp, S_IRUGO,	\
		show_temp_auto_point2_temp, NULL, offset - 1);

temp_auto_point(1);
temp_auto_point(2);
temp_auto_point(3);

static ssize_t show_temp_crit_enable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", (data->config1 & CFG1_THERM_HOT) >> 4);
}
static ssize_t set_temp_crit_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val > 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->config1 = (data->config1 & ~CFG1_THERM_HOT) | (val << 4);
	adm1026_write_value(client, ADM1026_REG_CONFIG1, data->config1);
	mutex_unlock(&data->update_lock);

	return count;
}

#define temp_crit_enable(offset)				\
static DEVICE_ATTR(temp##offset##_crit_enable, S_IRUGO | S_IWUSR, \
	show_temp_crit_enable, set_temp_crit_enable);

temp_crit_enable(1);
temp_crit_enable(2);
temp_crit_enable(3);

static ssize_t show_temp_crit(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_crit[nr]));
}
static ssize_t set_temp_crit(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_crit[nr] = TEMP_TO_REG(val);
	adm1026_write_value(client, ADM1026_REG_TEMP_THERM[nr],
		data->temp_crit[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define temp_crit_reg(offset)						\
static SENSOR_DEVICE_ATTR(temp##offset##_crit, S_IRUGO | S_IWUSR,	\
		show_temp_crit, set_temp_crit, offset - 1);

temp_crit_reg(1);
temp_crit_reg(2);
temp_crit_reg(3);

static ssize_t show_analog_out_reg(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", DAC_FROM_REG(data->analog_out));
}
static ssize_t set_analog_out_reg(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->analog_out = DAC_TO_REG(val);
	adm1026_write_value(client, ADM1026_REG_DAC, data->analog_out);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR(analog_out, S_IRUGO | S_IWUSR, show_analog_out_reg,
	set_analog_out_reg);

static ssize_t show_vid_reg(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	int vid = (data->gpio >> 11) & 0x1f;

	dev_dbg(dev, "Setting VID from GPIO11-15.\n");
	return sprintf(buf, "%d\n", vid_from_reg(vid, data->vrm));
}

static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid_reg, NULL);

static ssize_t show_vrm_reg(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->vrm);
}

static ssize_t store_vrm_reg(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val > 255)
		return -EINVAL;

	data->vrm = val;
	return count;
}

static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm_reg, store_vrm_reg);

static ssize_t show_alarms_reg(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%ld\n", data->alarms);
}

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms_reg, NULL);

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	int bitnr = to_sensor_dev_attr(attr)->index;
	return sprintf(buf, "%ld\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR(temp2_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(temp3_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(in9_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(in11_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(in12_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(in13_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(in14_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(in15_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(in16_alarm, S_IRUGO, show_alarm, NULL, 7);
static SENSOR_DEVICE_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 8);
static SENSOR_DEVICE_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 9);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 10);
static SENSOR_DEVICE_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 11);
static SENSOR_DEVICE_ATTR(in4_alarm, S_IRUGO, show_alarm, NULL, 12);
static SENSOR_DEVICE_ATTR(in5_alarm, S_IRUGO, show_alarm, NULL, 13);
static SENSOR_DEVICE_ATTR(in6_alarm, S_IRUGO, show_alarm, NULL, 14);
static SENSOR_DEVICE_ATTR(in7_alarm, S_IRUGO, show_alarm, NULL, 15);
static SENSOR_DEVICE_ATTR(fan1_alarm, S_IRUGO, show_alarm, NULL, 16);
static SENSOR_DEVICE_ATTR(fan2_alarm, S_IRUGO, show_alarm, NULL, 17);
static SENSOR_DEVICE_ATTR(fan3_alarm, S_IRUGO, show_alarm, NULL, 18);
static SENSOR_DEVICE_ATTR(fan4_alarm, S_IRUGO, show_alarm, NULL, 19);
static SENSOR_DEVICE_ATTR(fan5_alarm, S_IRUGO, show_alarm, NULL, 20);
static SENSOR_DEVICE_ATTR(fan6_alarm, S_IRUGO, show_alarm, NULL, 21);
static SENSOR_DEVICE_ATTR(fan7_alarm, S_IRUGO, show_alarm, NULL, 22);
static SENSOR_DEVICE_ATTR(fan8_alarm, S_IRUGO, show_alarm, NULL, 23);
static SENSOR_DEVICE_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 24);
static SENSOR_DEVICE_ATTR(in10_alarm, S_IRUGO, show_alarm, NULL, 25);
static SENSOR_DEVICE_ATTR(in8_alarm, S_IRUGO, show_alarm, NULL, 26);

static ssize_t show_alarm_mask(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%ld\n", data->alarm_mask);
}
static ssize_t set_alarm_mask(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long mask;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->alarm_mask = val & 0x7fffffff;
	mask = data->alarm_mask
		| (data->gpio_mask & 0x10000 ? 0x80000000 : 0);
	adm1026_write_value(client, ADM1026_REG_MASK1,
		mask & 0xff);
	mask >>= 8;
	adm1026_write_value(client, ADM1026_REG_MASK2,
		mask & 0xff);
	mask >>= 8;
	adm1026_write_value(client, ADM1026_REG_MASK3,
		mask & 0xff);
	mask >>= 8;
	adm1026_write_value(client, ADM1026_REG_MASK4,
		mask & 0xff);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR(alarm_mask, S_IRUGO | S_IWUSR, show_alarm_mask,
	set_alarm_mask);


static ssize_t show_gpio(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%ld\n", data->gpio);
}
static ssize_t set_gpio(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long gpio;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->gpio = val & 0x1ffff;
	gpio = data->gpio;
	adm1026_write_value(client, ADM1026_REG_GPIO_STATUS_0_7, gpio & 0xff);
	gpio >>= 8;
	adm1026_write_value(client, ADM1026_REG_GPIO_STATUS_8_15, gpio & 0xff);
	gpio = ((gpio >> 1) & 0x80) | (data->alarms >> 24 & 0x7f);
	adm1026_write_value(client, ADM1026_REG_STATUS4, gpio & 0xff);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR(gpio, S_IRUGO | S_IWUSR, show_gpio, set_gpio);

static ssize_t show_gpio_mask(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%ld\n", data->gpio_mask);
}
static ssize_t set_gpio_mask(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long mask;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->gpio_mask = val & 0x1ffff;
	mask = data->gpio_mask;
	adm1026_write_value(client, ADM1026_REG_GPIO_MASK_0_7, mask & 0xff);
	mask >>= 8;
	adm1026_write_value(client, ADM1026_REG_GPIO_MASK_8_15, mask & 0xff);
	mask = ((mask >> 1) & 0x80) | (data->alarm_mask >> 24 & 0x7f);
	adm1026_write_value(client, ADM1026_REG_MASK1, mask & 0xff);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR(gpio_mask, S_IRUGO | S_IWUSR, show_gpio_mask, set_gpio_mask);

static ssize_t show_pwm_reg(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", PWM_FROM_REG(data->pwm1.pwm));
}

static ssize_t set_pwm_reg(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	if (data->pwm1.enable == 1) {
		long val;
		int err;

		err = kstrtol(buf, 10, &val);
		if (err)
			return err;

		mutex_lock(&data->update_lock);
		data->pwm1.pwm = PWM_TO_REG(val);
		adm1026_write_value(client, ADM1026_REG_PWM, data->pwm1.pwm);
		mutex_unlock(&data->update_lock);
	}
	return count;
}

static ssize_t show_auto_pwm_min(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm1.auto_pwm_min);
}

static ssize_t set_auto_pwm_min(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->pwm1.auto_pwm_min = clamp_val(val, 0, 255);
	if (data->pwm1.enable == 2) { /* apply immediately */
		data->pwm1.pwm = PWM_TO_REG((data->pwm1.pwm & 0x0f) |
			PWM_MIN_TO_REG(data->pwm1.auto_pwm_min));
		adm1026_write_value(client, ADM1026_REG_PWM, data->pwm1.pwm);
	}
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_auto_pwm_max(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ADM1026_PWM_MAX);
}

static ssize_t show_pwm_enable(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct adm1026_data *data = adm1026_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm1.enable);
}

static ssize_t set_pwm_enable(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct adm1026_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int old_enable;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val >= 3)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	old_enable = data->pwm1.enable;
	data->pwm1.enable = val;
	data->config1 = (data->config1 & ~CFG1_PWM_AFC)
			| ((val == 2) ? CFG1_PWM_AFC : 0);
	adm1026_write_value(client, ADM1026_REG_CONFIG1, data->config1);
	if (val == 2) { /* apply pwm1_auto_pwm_min to pwm1 */
		data->pwm1.pwm = PWM_TO_REG((data->pwm1.pwm & 0x0f) |
			PWM_MIN_TO_REG(data->pwm1.auto_pwm_min));
		adm1026_write_value(client, ADM1026_REG_PWM, data->pwm1.pwm);
	} else if (!((old_enable == 1) && (val == 1))) {
		/* set pwm to safe value */
		data->pwm1.pwm = 255;
		adm1026_write_value(client, ADM1026_REG_PWM, data->pwm1.pwm);
	}
	mutex_unlock(&data->update_lock);

	return count;
}

/* enable PWM fan control */
static DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, show_pwm_reg, set_pwm_reg);
static DEVICE_ATTR(pwm2, S_IRUGO | S_IWUSR, show_pwm_reg, set_pwm_reg);
static DEVICE_ATTR(pwm3, S_IRUGO | S_IWUSR, show_pwm_reg, set_pwm_reg);
static DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR, show_pwm_enable,
	set_pwm_enable);
static DEVICE_ATTR(pwm2_enable, S_IRUGO | S_IWUSR, show_pwm_enable,
	set_pwm_enable);
static DEVICE_ATTR(pwm3_enable, S_IRUGO | S_IWUSR, show_pwm_enable,
	set_pwm_enable);
static DEVICE_ATTR(temp1_auto_point1_pwm, S_IRUGO | S_IWUSR,
	show_auto_pwm_min, set_auto_pwm_min);
static DEVICE_ATTR(temp2_auto_point1_pwm, S_IRUGO | S_IWUSR,
	show_auto_pwm_min, set_auto_pwm_min);
static DEVICE_ATTR(temp3_auto_point1_pwm, S_IRUGO | S_IWUSR,
	show_auto_pwm_min, set_auto_pwm_min);

static DEVICE_ATTR(temp1_auto_point2_pwm, S_IRUGO, show_auto_pwm_max, NULL);
static DEVICE_ATTR(temp2_auto_point2_pwm, S_IRUGO, show_auto_pwm_max, NULL);
static DEVICE_ATTR(temp3_auto_point2_pwm, S_IRUGO, show_auto_pwm_max, NULL);

static struct attribute *adm1026_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in6_alarm.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_in7_alarm.dev_attr.attr,
	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in10_max.dev_attr.attr,
	&sensor_dev_attr_in10_min.dev_attr.attr,
	&sensor_dev_attr_in10_alarm.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,
	&sensor_dev_attr_in11_max.dev_attr.attr,
	&sensor_dev_attr_in11_min.dev_attr.attr,
	&sensor_dev_attr_in11_alarm.dev_attr.attr,
	&sensor_dev_attr_in12_input.dev_attr.attr,
	&sensor_dev_attr_in12_max.dev_attr.attr,
	&sensor_dev_attr_in12_min.dev_attr.attr,
	&sensor_dev_attr_in12_alarm.dev_attr.attr,
	&sensor_dev_attr_in13_input.dev_attr.attr,
	&sensor_dev_attr_in13_max.dev_attr.attr,
	&sensor_dev_attr_in13_min.dev_attr.attr,
	&sensor_dev_attr_in13_alarm.dev_attr.attr,
	&sensor_dev_attr_in14_input.dev_attr.attr,
	&sensor_dev_attr_in14_max.dev_attr.attr,
	&sensor_dev_attr_in14_min.dev_attr.attr,
	&sensor_dev_attr_in14_alarm.dev_attr.attr,
	&sensor_dev_attr_in15_input.dev_attr.attr,
	&sensor_dev_attr_in15_max.dev_attr.attr,
	&sensor_dev_attr_in15_min.dev_attr.attr,
	&sensor_dev_attr_in15_alarm.dev_attr.attr,
	&sensor_dev_attr_in16_input.dev_attr.attr,
	&sensor_dev_attr_in16_max.dev_attr.attr,
	&sensor_dev_attr_in16_min.dev_attr.attr,
	&sensor_dev_attr_in16_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan3_div.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan3_alarm.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan4_div.dev_attr.attr,
	&sensor_dev_attr_fan4_min.dev_attr.attr,
	&sensor_dev_attr_fan4_alarm.dev_attr.attr,
	&sensor_dev_attr_fan5_input.dev_attr.attr,
	&sensor_dev_attr_fan5_div.dev_attr.attr,
	&sensor_dev_attr_fan5_min.dev_attr.attr,
	&sensor_dev_attr_fan5_alarm.dev_attr.attr,
	&sensor_dev_attr_fan6_input.dev_attr.attr,
	&sensor_dev_attr_fan6_div.dev_attr.attr,
	&sensor_dev_attr_fan6_min.dev_attr.attr,
	&sensor_dev_attr_fan6_alarm.dev_attr.attr,
	&sensor_dev_attr_fan7_input.dev_attr.attr,
	&sensor_dev_attr_fan7_div.dev_attr.attr,
	&sensor_dev_attr_fan7_min.dev_attr.attr,
	&sensor_dev_attr_fan7_alarm.dev_attr.attr,
	&sensor_dev_attr_fan8_input.dev_attr.attr,
	&sensor_dev_attr_fan8_div.dev_attr.attr,
	&sensor_dev_attr_fan8_min.dev_attr.attr,
	&sensor_dev_attr_fan8_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_offset.dev_attr.attr,
	&sensor_dev_attr_temp2_offset.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point1_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_point1_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&dev_attr_temp1_crit_enable.attr,
	&dev_attr_temp2_crit_enable.attr,
	&dev_attr_cpu0_vid.attr,
	&dev_attr_vrm.attr,
	&dev_attr_alarms.attr,
	&dev_attr_alarm_mask.attr,
	&dev_attr_gpio.attr,
	&dev_attr_gpio_mask.attr,
	&dev_attr_pwm1.attr,
	&dev_attr_pwm2.attr,
	&dev_attr_pwm3.attr,
	&dev_attr_pwm1_enable.attr,
	&dev_attr_pwm2_enable.attr,
	&dev_attr_pwm3_enable.attr,
	&dev_attr_temp1_auto_point1_pwm.attr,
	&dev_attr_temp2_auto_point1_pwm.attr,
	&dev_attr_temp1_auto_point2_pwm.attr,
	&dev_attr_temp2_auto_point2_pwm.attr,
	&dev_attr_analog_out.attr,
	NULL
};

static const struct attribute_group adm1026_group = {
	.attrs = adm1026_attributes,
};

static struct attribute *adm1026_attributes_temp3[] = {
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_offset.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_point1_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	&dev_attr_temp3_crit_enable.attr,
	&dev_attr_temp3_auto_point1_pwm.attr,
	&dev_attr_temp3_auto_point2_pwm.attr,
	NULL
};

static const struct attribute_group adm1026_group_temp3 = {
	.attrs = adm1026_attributes_temp3,
};

static struct attribute *adm1026_attributes_in8_9[] = {
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in8_max.dev_attr.attr,
	&sensor_dev_attr_in8_min.dev_attr.attr,
	&sensor_dev_attr_in8_alarm.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in9_max.dev_attr.attr,
	&sensor_dev_attr_in9_min.dev_attr.attr,
	&sensor_dev_attr_in9_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group adm1026_group_in8_9 = {
	.attrs = adm1026_attributes_in8_9,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int adm1026_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int address = client->addr;
	int company, verstep;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		/* We need to be able to do byte I/O */
		return -ENODEV;
	}

	/* Now, we do the remaining detection. */

	company = adm1026_read_value(client, ADM1026_REG_COMPANY);
	verstep = adm1026_read_value(client, ADM1026_REG_VERSTEP);

	dev_dbg(&adapter->dev,
		"Detecting device at %d,0x%02x with COMPANY: 0x%02x and VERSTEP: 0x%02x\n",
		i2c_adapter_id(client->adapter), client->addr,
		company, verstep);

	/* Determine the chip type. */
	dev_dbg(&adapter->dev, "Autodetecting device at %d,0x%02x...\n",
		i2c_adapter_id(adapter), address);
	if (company == ADM1026_COMPANY_ANALOG_DEV
	    && verstep == ADM1026_VERSTEP_ADM1026) {
		/* Analog Devices ADM1026 */
	} else if (company == ADM1026_COMPANY_ANALOG_DEV
		&& (verstep & 0xf0) == ADM1026_VERSTEP_GENERIC) {
		dev_err(&adapter->dev,
			"Unrecognized stepping 0x%02x. Defaulting to ADM1026.\n",
			verstep);
	} else if ((verstep & 0xf0) == ADM1026_VERSTEP_GENERIC) {
		dev_err(&adapter->dev,
			"Found version/stepping 0x%02x. Assuming generic ADM1026.\n",
			verstep);
	} else {
		dev_dbg(&adapter->dev, "Autodetection failed\n");
		/* Not an ADM1026... */
		return -ENODEV;
	}

	strlcpy(info->type, "adm1026", I2C_NAME_SIZE);

	return 0;
}

static void adm1026_print_gpio(struct i2c_client *client)
{
	struct adm1026_data *data = i2c_get_clientdata(client);
	int i;

	dev_dbg(&client->dev, "GPIO config is:\n");
	for (i = 0; i <= 7; ++i) {
		if (data->config2 & (1 << i)) {
			dev_dbg(&client->dev, "\t%sGP%s%d\n",
				data->gpio_config[i] & 0x02 ? "" : "!",
				data->gpio_config[i] & 0x01 ? "OUT" : "IN",
				i);
		} else {
			dev_dbg(&client->dev, "\tFAN%d\n", i);
		}
	}
	for (i = 8; i <= 15; ++i) {
		dev_dbg(&client->dev, "\t%sGP%s%d\n",
			data->gpio_config[i] & 0x02 ? "" : "!",
			data->gpio_config[i] & 0x01 ? "OUT" : "IN",
			i);
	}
	if (data->config3 & CFG3_GPIO16_ENABLE) {
		dev_dbg(&client->dev, "\t%sGP%s16\n",
			data->gpio_config[16] & 0x02 ? "" : "!",
			data->gpio_config[16] & 0x01 ? "OUT" : "IN");
	} else {
		/* GPIO16 is THERM */
		dev_dbg(&client->dev, "\tTHERM\n");
	}
}

static void adm1026_fixup_gpio(struct i2c_client *client)
{
	struct adm1026_data *data = i2c_get_clientdata(client);
	int i;
	int value;

	/* Make the changes requested. */
	/*
	 * We may need to unlock/stop monitoring or soft-reset the
	 *    chip before we can make changes.  This hasn't been
	 *    tested much.  FIXME
	 */

	/* Make outputs */
	for (i = 0; i <= 16; ++i) {
		if (gpio_output[i] >= 0 && gpio_output[i] <= 16)
			data->gpio_config[gpio_output[i]] |= 0x01;
		/* if GPIO0-7 is output, it isn't a FAN tach */
		if (gpio_output[i] >= 0 && gpio_output[i] <= 7)
			data->config2 |= 1 << gpio_output[i];
	}

	/* Input overrides output */
	for (i = 0; i <= 16; ++i) {
		if (gpio_input[i] >= 0 && gpio_input[i] <= 16)
			data->gpio_config[gpio_input[i]] &= ~0x01;
		/* if GPIO0-7 is input, it isn't a FAN tach */
		if (gpio_input[i] >= 0 && gpio_input[i] <= 7)
			data->config2 |= 1 << gpio_input[i];
	}

	/* Inverted */
	for (i = 0; i <= 16; ++i) {
		if (gpio_inverted[i] >= 0 && gpio_inverted[i] <= 16)
			data->gpio_config[gpio_inverted[i]] &= ~0x02;
	}

	/* Normal overrides inverted */
	for (i = 0; i <= 16; ++i) {
		if (gpio_normal[i] >= 0 && gpio_normal[i] <= 16)
			data->gpio_config[gpio_normal[i]] |= 0x02;
	}

	/* Fan overrides input and output */
	for (i = 0; i <= 7; ++i) {
		if (gpio_fan[i] >= 0 && gpio_fan[i] <= 7)
			data->config2 &= ~(1 << gpio_fan[i]);
	}

	/* Write new configs to registers */
	adm1026_write_value(client, ADM1026_REG_CONFIG2, data->config2);
	data->config3 = (data->config3 & 0x3f)
			| ((data->gpio_config[16] & 0x03) << 6);
	adm1026_write_value(client, ADM1026_REG_CONFIG3, data->config3);
	for (i = 15, value = 0; i >= 0; --i) {
		value <<= 2;
		value |= data->gpio_config[i] & 0x03;
		if ((i & 0x03) == 0) {
			adm1026_write_value(client,
					ADM1026_REG_GPIO_CFG_0_3 + i/4,
					value);
			value = 0;
		}
	}

	/* Print the new config */
	adm1026_print_gpio(client);
}

static void adm1026_init_client(struct i2c_client *client)
{
	int value, i;
	struct adm1026_data *data = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "Initializing device\n");
	/* Read chip config */
	data->config1 = adm1026_read_value(client, ADM1026_REG_CONFIG1);
	data->config2 = adm1026_read_value(client, ADM1026_REG_CONFIG2);
	data->config3 = adm1026_read_value(client, ADM1026_REG_CONFIG3);

	/* Inform user of chip config */
	dev_dbg(&client->dev, "ADM1026_REG_CONFIG1 is: 0x%02x\n",
		data->config1);
	if ((data->config1 & CFG1_MONITOR) == 0) {
		dev_dbg(&client->dev,
			"Monitoring not currently enabled.\n");
	}
	if (data->config1 & CFG1_INT_ENABLE) {
		dev_dbg(&client->dev,
			"SMBALERT interrupts are enabled.\n");
	}
	if (data->config1 & CFG1_AIN8_9) {
		dev_dbg(&client->dev,
			"in8 and in9 enabled. temp3 disabled.\n");
	} else {
		dev_dbg(&client->dev,
			"temp3 enabled.  in8 and in9 disabled.\n");
	}
	if (data->config1 & CFG1_THERM_HOT) {
		dev_dbg(&client->dev,
			"Automatic THERM, PWM, and temp limits enabled.\n");
	}

	if (data->config3 & CFG3_GPIO16_ENABLE) {
		dev_dbg(&client->dev,
			"GPIO16 enabled.  THERM pin disabled.\n");
	} else {
		dev_dbg(&client->dev,
			"THERM pin enabled.  GPIO16 disabled.\n");
	}
	if (data->config3 & CFG3_VREF_250)
		dev_dbg(&client->dev, "Vref is 2.50 Volts.\n");
	else
		dev_dbg(&client->dev, "Vref is 1.82 Volts.\n");
	/* Read and pick apart the existing GPIO configuration */
	value = 0;
	for (i = 0; i <= 15; ++i) {
		if ((i & 0x03) == 0) {
			value = adm1026_read_value(client,
					ADM1026_REG_GPIO_CFG_0_3 + i / 4);
		}
		data->gpio_config[i] = value & 0x03;
		value >>= 2;
	}
	data->gpio_config[16] = (data->config3 >> 6) & 0x03;

	/* ... and then print it */
	adm1026_print_gpio(client);

	/*
	 * If the user asks us to reprogram the GPIO config, then
	 * do it now.
	 */
	if (gpio_input[0] != -1 || gpio_output[0] != -1
		|| gpio_inverted[0] != -1 || gpio_normal[0] != -1
		|| gpio_fan[0] != -1) {
		adm1026_fixup_gpio(client);
	}

	/*
	 * WE INTENTIONALLY make no changes to the limits,
	 *   offsets, pwms, fans and zones.  If they were
	 *   configured, we don't want to mess with them.
	 *   If they weren't, the default is 100% PWM, no
	 *   control and will suffice until 'sensors -s'
	 *   can be run by the user.  We DO set the default
	 *   value for pwm1.auto_pwm_min to its maximum
	 *   so that enabling automatic pwm fan control
	 *   without first setting a value for pwm1.auto_pwm_min
	 *   will not result in potentially dangerous fan speed decrease.
	 */
	data->pwm1.auto_pwm_min = 255;
	/* Start monitoring */
	value = adm1026_read_value(client, ADM1026_REG_CONFIG1);
	/* Set MONITOR, clear interrupt acknowledge and s/w reset */
	value = (value | CFG1_MONITOR) & (~CFG1_INT_CLEAR & ~CFG1_RESET);
	dev_dbg(&client->dev, "Setting CONFIG to: 0x%02x\n", value);
	data->config1 = value;
	adm1026_write_value(client, ADM1026_REG_CONFIG1, value);

	/* initialize fan_div[] to hardware defaults */
	value = adm1026_read_value(client, ADM1026_REG_FAN_DIV_0_3) |
		(adm1026_read_value(client, ADM1026_REG_FAN_DIV_4_7) << 8);
	for (i = 0; i <= 7; ++i) {
		data->fan_div[i] = DIV_FROM_REG(value & 0x03);
		value >>= 2;
	}
}

static int adm1026_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct adm1026_data *data;

	data = devm_kzalloc(dev, sizeof(struct adm1026_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->client = client;
	mutex_init(&data->update_lock);

	/* Set the VRM version */
	data->vrm = vid_which_vrm();

	/* Initialize the ADM1026 chip */
	adm1026_init_client(client);

	/* sysfs hooks */
	data->groups[0] = &adm1026_group;
	if (data->config1 & CFG1_AIN8_9)
		data->groups[1] = &adm1026_group_in8_9;
	else
		data->groups[1] = &adm1026_group_temp3;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id adm1026_id[] = {
	{ "adm1026", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adm1026_id);

static struct i2c_driver adm1026_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adm1026",
	},
	.probe		= adm1026_probe,
	.id_table	= adm1026_id,
	.detect		= adm1026_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(adm1026_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Philip Pokorny <ppokorny@penguincomputing.com>, "
	      "Justin Thiessen <jthiessen@penguincomputing.com>");
MODULE_DESCRIPTION("ADM1026 driver");
