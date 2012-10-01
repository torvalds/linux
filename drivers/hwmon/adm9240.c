/*
 * adm9240.c	Part of lm_sensors, Linux kernel modules for hardware
 *		monitoring
 *
 * Copyright (C) 1999	Frodo Looijaard <frodol@dds.nl>
 *			Philip Edelbrock <phil@netroedge.com>
 * Copyright (C) 2003	Michiel Rook <michiel@grendelproject.nl>
 * Copyright (C) 2005	Grant Coady <gcoady.lk@gmail.com> with valuable
 *				guidance from Jean Delvare
 *
 * Driver supports	Analog Devices		ADM9240
 *			Dallas Semiconductor	DS1780
 *			National Semiconductor	LM81
 *
 * ADM9240 is the reference, DS1780 and LM81 are register compatibles
 *
 * Voltage	Six inputs are scaled by chip, VID also reported
 * Temperature	Chip temperature to 0.5'C, maximum and max_hysteris
 * Fans		2 fans, low speed alarm, automatic fan clock divider
 * Alarms	16-bit map of active alarms
 * Analog Out	0..1250 mV output
 *
 * Chassis Intrusion: clear CI latch with 'echo 0 > intrusion0_alarm'
 *
 * Test hardware: Intel SE440BX-2 desktop motherboard --Grant
 *
 * LM81 extended temp reading not implemented
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, 0x2f,
					I2C_CLIENT_END };

enum chips { adm9240, ds1780, lm81 };

/* ADM9240 registers */
#define ADM9240_REG_MAN_ID		0x3e
#define ADM9240_REG_DIE_REV		0x3f
#define ADM9240_REG_CONFIG		0x40

#define ADM9240_REG_IN(nr)		(0x20 + (nr))   /* 0..5 */
#define ADM9240_REG_IN_MAX(nr)		(0x2b + (nr) * 2)
#define ADM9240_REG_IN_MIN(nr)		(0x2c + (nr) * 2)
#define ADM9240_REG_FAN(nr)		(0x28 + (nr))   /* 0..1 */
#define ADM9240_REG_FAN_MIN(nr)		(0x3b + (nr))
#define ADM9240_REG_INT(nr)		(0x41 + (nr))
#define ADM9240_REG_INT_MASK(nr)	(0x43 + (nr))
#define ADM9240_REG_TEMP		0x27
#define ADM9240_REG_TEMP_MAX(nr)	(0x39 + (nr)) /* 0, 1 = high, hyst */
#define ADM9240_REG_ANALOG_OUT		0x19
#define ADM9240_REG_CHASSIS_CLEAR	0x46
#define ADM9240_REG_VID_FAN_DIV		0x47
#define ADM9240_REG_I2C_ADDR		0x48
#define ADM9240_REG_VID4		0x49
#define ADM9240_REG_TEMP_CONF		0x4b

/* generalised scaling with integer rounding */
static inline int SCALE(long val, int mul, int div)
{
	if (val < 0)
		return (val * mul - div / 2) / div;
	else
		return (val * mul + div / 2) / div;
}

/* adm9240 internally scales voltage measurements */
static const u16 nom_mv[] = { 2500, 2700, 3300, 5000, 12000, 2700 };

static inline unsigned int IN_FROM_REG(u8 reg, int n)
{
	return SCALE(reg, nom_mv[n], 192);
}

static inline u8 IN_TO_REG(unsigned long val, int n)
{
	return SENSORS_LIMIT(SCALE(val, 192, nom_mv[n]), 0, 255);
}

/* temperature range: -40..125, 127 disables temperature alarm */
static inline s8 TEMP_TO_REG(long val)
{
	return SENSORS_LIMIT(SCALE(val, 1, 1000), -40, 127);
}

/* two fans, each with low fan speed limit */
static inline unsigned int FAN_FROM_REG(u8 reg, u8 div)
{
	if (!reg) /* error */
		return -1;

	if (reg == 255)
		return 0;

	return SCALE(1350000, 1, reg * div);
}

/* analog out 0..1250mV */
static inline u8 AOUT_TO_REG(unsigned long val)
{
	return SENSORS_LIMIT(SCALE(val, 255, 1250), 0, 255);
}

static inline unsigned int AOUT_FROM_REG(u8 reg)
{
	return SCALE(reg, 1250, 255);
}

static int adm9240_probe(struct i2c_client *client,
			 const struct i2c_device_id *id);
static int adm9240_detect(struct i2c_client *client,
			  struct i2c_board_info *info);
static void adm9240_init_client(struct i2c_client *client);
static int adm9240_remove(struct i2c_client *client);
static struct adm9240_data *adm9240_update_device(struct device *dev);

/* driver data */
static const struct i2c_device_id adm9240_id[] = {
	{ "adm9240", adm9240 },
	{ "ds1780", ds1780 },
	{ "lm81", lm81 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adm9240_id);

static struct i2c_driver adm9240_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adm9240",
	},
	.probe		= adm9240_probe,
	.remove		= adm9240_remove,
	.id_table	= adm9240_id,
	.detect		= adm9240_detect,
	.address_list	= normal_i2c,
};

/* per client data */
struct adm9240_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid;
	unsigned long last_updated_measure;
	unsigned long last_updated_config;

	u8 in[6];		/* ro	in0_input */
	u8 in_max[6];		/* rw	in0_max */
	u8 in_min[6];		/* rw	in0_min */
	u8 fan[2];		/* ro	fan1_input */
	u8 fan_min[2];		/* rw	fan1_min */
	u8 fan_div[2];		/* rw	fan1_div, read-only accessor */
	s16 temp;		/* ro	temp1_input, 9-bit sign-extended */
	s8 temp_max[2];		/* rw	0 -> temp_max, 1 -> temp_max_hyst */
	u16 alarms;		/* ro	alarms */
	u8 aout;		/* rw	aout_output */
	u8 vid;			/* ro	vid */
	u8 vrm;			/* --	vrm set on startup, no accessor */
};

/*** sysfs accessors ***/

/* temperature */
static ssize_t show_temp(struct device *dev, struct device_attribute *dummy,
		char *buf)
{
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%d\n", data->temp * 500); /* 9-bit value */
}

static ssize_t show_max(struct device *dev, struct device_attribute *devattr,
		char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_max[attr->index] * 1000);
}

static ssize_t set_max(struct device *dev, struct device_attribute *devattr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adm9240_data *data = i2c_get_clientdata(client);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_max[attr->index] = TEMP_TO_REG(val);
	i2c_smbus_write_byte_data(client, ADM9240_REG_TEMP_MAX(attr->index),
			data->temp_max[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO,
		show_max, set_max, 0);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IWUSR | S_IRUGO,
		show_max, set_max, 1);

/* voltage */
static ssize_t show_in(struct device *dev, struct device_attribute *devattr,
		char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in[attr->index],
				attr->index));
}

static ssize_t show_in_min(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_min[attr->index],
				attr->index));
}

static ssize_t show_in_max(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_max[attr->index],
				attr->index));
}

static ssize_t set_in_min(struct device *dev,
		struct device_attribute *devattr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adm9240_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_min[attr->index] = IN_TO_REG(val, attr->index);
	i2c_smbus_write_byte_data(client, ADM9240_REG_IN_MIN(attr->index),
			data->in_min[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_in_max(struct device *dev,
		struct device_attribute *devattr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adm9240_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_max[attr->index] = IN_TO_REG(val, attr->index);
	i2c_smbus_write_byte_data(client, ADM9240_REG_IN_MAX(attr->index),
			data->in_max[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define vin(nr)							\
static SENSOR_DEVICE_ATTR(in##nr##_input, S_IRUGO,		\
		show_in, NULL, nr);				\
static SENSOR_DEVICE_ATTR(in##nr##_min, S_IRUGO | S_IWUSR,	\
		show_in_min, set_in_min, nr);			\
static SENSOR_DEVICE_ATTR(in##nr##_max, S_IRUGO | S_IWUSR,	\
		show_in_max, set_in_max, nr);

vin(0);
vin(1);
vin(2);
vin(3);
vin(4);
vin(5);

/* fans */
static ssize_t show_fan(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[attr->index],
				1 << data->fan_div[attr->index]));
}

static ssize_t show_fan_min(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[attr->index],
				1 << data->fan_div[attr->index]));
}

static ssize_t show_fan_div(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%d\n", 1 << data->fan_div[attr->index]);
}

/* write new fan div, callers must hold data->update_lock */
static void adm9240_write_fan_div(struct i2c_client *client, int nr,
		u8 fan_div)
{
	u8 reg, old, shift = (nr + 2) * 2;

	reg = i2c_smbus_read_byte_data(client, ADM9240_REG_VID_FAN_DIV);
	old = (reg >> shift) & 3;
	reg &= ~(3 << shift);
	reg |= (fan_div << shift);
	i2c_smbus_write_byte_data(client, ADM9240_REG_VID_FAN_DIV, reg);
	dev_dbg(&client->dev, "fan%d clock divider changed from %u "
			"to %u\n", nr + 1, 1 << old, 1 << fan_div);
}

/*
 * set fan speed low limit:
 *
 * - value is zero: disable fan speed low limit alarm
 *
 * - value is below fan speed measurement range: enable fan speed low
 *   limit alarm to be asserted while fan speed too slow to measure
 *
 * - otherwise: select fan clock divider to suit fan speed low limit,
 *   measurement code may adjust registers to ensure fan speed reading
 */
static ssize_t set_fan_min(struct device *dev,
		struct device_attribute *devattr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adm9240_data *data = i2c_get_clientdata(client);
	int nr = attr->index;
	u8 new_div;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);

	if (!val) {
		data->fan_min[nr] = 255;
		new_div = data->fan_div[nr];

		dev_dbg(&client->dev, "fan%u low limit set disabled\n",
				nr + 1);

	} else if (val < 1350000 / (8 * 254)) {
		new_div = 3;
		data->fan_min[nr] = 254;

		dev_dbg(&client->dev, "fan%u low limit set minimum %u\n",
				nr + 1, FAN_FROM_REG(254, 1 << new_div));

	} else {
		unsigned int new_min = 1350000 / val;

		new_div = 0;
		while (new_min > 192 && new_div < 3) {
			new_div++;
			new_min /= 2;
		}
		if (!new_min) /* keep > 0 */
			new_min++;

		data->fan_min[nr] = new_min;

		dev_dbg(&client->dev, "fan%u low limit set fan speed %u\n",
				nr + 1, FAN_FROM_REG(new_min, 1 << new_div));
	}

	if (new_div != data->fan_div[nr]) {
		data->fan_div[nr] = new_div;
		adm9240_write_fan_div(client, nr, new_div);
	}
	i2c_smbus_write_byte_data(client, ADM9240_REG_FAN_MIN(nr),
			data->fan_min[nr]);

	mutex_unlock(&data->update_lock);
	return count;
}

#define fan(nr)							\
static SENSOR_DEVICE_ATTR(fan##nr##_input, S_IRUGO,		\
		show_fan, NULL, nr - 1);			\
static SENSOR_DEVICE_ATTR(fan##nr##_div, S_IRUGO,		\
		show_fan_div, NULL, nr - 1);			\
static SENSOR_DEVICE_ATTR(fan##nr##_min, S_IRUGO | S_IWUSR,	\
		show_fan_min, set_fan_min, nr - 1);

fan(1);
fan(2);

/* alarms */
static ssize_t show_alarms(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static ssize_t show_alarm(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}
static SENSOR_DEVICE_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_alarm, S_IRUGO, show_alarm, NULL, 8);
static SENSOR_DEVICE_ATTR(in5_alarm, S_IRUGO, show_alarm, NULL, 9);
static SENSOR_DEVICE_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(fan1_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(fan2_alarm, S_IRUGO, show_alarm, NULL, 7);

/* vid */
static ssize_t show_vid(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

/* analog output */
static ssize_t show_aout(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct adm9240_data *data = adm9240_update_device(dev);
	return sprintf(buf, "%d\n", AOUT_FROM_REG(data->aout));
}

static ssize_t set_aout(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm9240_data *data = i2c_get_clientdata(client);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->aout = AOUT_TO_REG(val);
	i2c_smbus_write_byte_data(client, ADM9240_REG_ANALOG_OUT, data->aout);
	mutex_unlock(&data->update_lock);
	return count;
}
static DEVICE_ATTR(aout_output, S_IRUGO | S_IWUSR, show_aout, set_aout);

static ssize_t chassis_clear(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm9240_data *data = i2c_get_clientdata(client);
	unsigned long val;

	if (kstrtoul(buf, 10, &val) || val != 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	i2c_smbus_write_byte_data(client, ADM9240_REG_CHASSIS_CLEAR, 0x80);
	data->valid = 0;		/* Force cache refresh */
	mutex_unlock(&data->update_lock);
	dev_dbg(&client->dev, "chassis intrusion latch cleared\n");

	return count;
}
static SENSOR_DEVICE_ATTR(intrusion0_alarm, S_IRUGO | S_IWUSR, show_alarm,
		chassis_clear, 12);

static struct attribute *adm9240_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	&dev_attr_temp1_input.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&dev_attr_alarms.attr,
	&dev_attr_aout_output.attr,
	&sensor_dev_attr_intrusion0_alarm.dev_attr.attr,
	&dev_attr_cpu0_vid.attr,
	NULL
};

static const struct attribute_group adm9240_group = {
	.attrs = adm9240_attributes,
};


/*** sensor chip detect and driver install ***/

/* Return 0 if detection is successful, -ENODEV otherwise */
static int adm9240_detect(struct i2c_client *new_client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	const char *name = "";
	int address = new_client->addr;
	u8 man_id, die_rev;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* verify chip: reg address should match i2c address */
	if (i2c_smbus_read_byte_data(new_client, ADM9240_REG_I2C_ADDR)
			!= address) {
		dev_err(&adapter->dev, "detect fail: address match, 0x%02x\n",
			address);
		return -ENODEV;
	}

	/* check known chip manufacturer */
	man_id = i2c_smbus_read_byte_data(new_client, ADM9240_REG_MAN_ID);
	if (man_id == 0x23) {
		name = "adm9240";
	} else if (man_id == 0xda) {
		name = "ds1780";
	} else if (man_id == 0x01) {
		name = "lm81";
	} else {
		dev_err(&adapter->dev, "detect fail: unknown manuf, 0x%02x\n",
			man_id);
		return -ENODEV;
	}

	/* successful detect, print chip info */
	die_rev = i2c_smbus_read_byte_data(new_client, ADM9240_REG_DIE_REV);
	dev_info(&adapter->dev, "found %s revision %u\n",
		 man_id == 0x23 ? "ADM9240" :
		 man_id == 0xda ? "DS1780" : "LM81", die_rev);

	strlcpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static int adm9240_probe(struct i2c_client *new_client,
			 const struct i2c_device_id *id)
{
	struct adm9240_data *data;
	int err;

	data = devm_kzalloc(&new_client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(new_client, data);
	mutex_init(&data->update_lock);

	adm9240_init_client(new_client);

	/* populate sysfs filesystem */
	err = sysfs_create_group(&new_client->dev.kobj, &adm9240_group);
	if (err)
		return err;

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	sysfs_remove_group(&new_client->dev.kobj, &adm9240_group);
	return err;
}

static int adm9240_remove(struct i2c_client *client)
{
	struct adm9240_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &adm9240_group);

	return 0;
}

static void adm9240_init_client(struct i2c_client *client)
{
	struct adm9240_data *data = i2c_get_clientdata(client);
	u8 conf = i2c_smbus_read_byte_data(client, ADM9240_REG_CONFIG);
	u8 mode = i2c_smbus_read_byte_data(client, ADM9240_REG_TEMP_CONF) & 3;

	data->vrm = vid_which_vrm(); /* need this to report vid as mV */

	dev_info(&client->dev, "Using VRM: %d.%d\n", data->vrm / 10,
			data->vrm % 10);

	if (conf & 1) { /* measurement cycle running: report state */

		dev_info(&client->dev, "status: config 0x%02x mode %u\n",
				conf, mode);

	} else { /* cold start: open limits before starting chip */
		int i;

		for (i = 0; i < 6; i++) {
			i2c_smbus_write_byte_data(client,
					ADM9240_REG_IN_MIN(i), 0);
			i2c_smbus_write_byte_data(client,
					ADM9240_REG_IN_MAX(i), 255);
		}
		i2c_smbus_write_byte_data(client,
				ADM9240_REG_FAN_MIN(0), 255);
		i2c_smbus_write_byte_data(client,
				ADM9240_REG_FAN_MIN(1), 255);
		i2c_smbus_write_byte_data(client,
				ADM9240_REG_TEMP_MAX(0), 127);
		i2c_smbus_write_byte_data(client,
				ADM9240_REG_TEMP_MAX(1), 127);

		/* start measurement cycle */
		i2c_smbus_write_byte_data(client, ADM9240_REG_CONFIG, 1);

		dev_info(&client->dev, "cold start: config was 0x%02x "
				"mode %u\n", conf, mode);
	}
}

static struct adm9240_data *adm9240_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm9240_data *data = i2c_get_clientdata(client);
	int i;

	mutex_lock(&data->update_lock);

	/* minimum measurement cycle: 1.75 seconds */
	if (time_after(jiffies, data->last_updated_measure + (HZ * 7 / 4))
			|| !data->valid) {

		for (i = 0; i < 6; i++) { /* read voltages */
			data->in[i] = i2c_smbus_read_byte_data(client,
					ADM9240_REG_IN(i));
		}
		data->alarms = i2c_smbus_read_byte_data(client,
					ADM9240_REG_INT(0)) |
					i2c_smbus_read_byte_data(client,
					ADM9240_REG_INT(1)) << 8;

		/*
		 * read temperature: assume temperature changes less than
		 * 0.5'C per two measurement cycles thus ignore possible
		 * but unlikely aliasing error on lsb reading. --Grant
		 */
		data->temp = ((i2c_smbus_read_byte_data(client,
					ADM9240_REG_TEMP) << 8) |
					i2c_smbus_read_byte_data(client,
					ADM9240_REG_TEMP_CONF)) / 128;

		for (i = 0; i < 2; i++) { /* read fans */
			data->fan[i] = i2c_smbus_read_byte_data(client,
					ADM9240_REG_FAN(i));

			/* adjust fan clock divider on overflow */
			if (data->valid && data->fan[i] == 255 &&
					data->fan_div[i] < 3) {

				adm9240_write_fan_div(client, i,
						++data->fan_div[i]);

				/* adjust fan_min if active, but not to 0 */
				if (data->fan_min[i] < 255 &&
						data->fan_min[i] >= 2)
					data->fan_min[i] /= 2;
			}
		}
		data->last_updated_measure = jiffies;
	}

	/* minimum config reading cycle: 300 seconds */
	if (time_after(jiffies, data->last_updated_config + (HZ * 300))
			|| !data->valid) {

		for (i = 0; i < 6; i++) {
			data->in_min[i] = i2c_smbus_read_byte_data(client,
					ADM9240_REG_IN_MIN(i));
			data->in_max[i] = i2c_smbus_read_byte_data(client,
					ADM9240_REG_IN_MAX(i));
		}
		for (i = 0; i < 2; i++) {
			data->fan_min[i] = i2c_smbus_read_byte_data(client,
					ADM9240_REG_FAN_MIN(i));
		}
		data->temp_max[0] = i2c_smbus_read_byte_data(client,
				ADM9240_REG_TEMP_MAX(0));
		data->temp_max[1] = i2c_smbus_read_byte_data(client,
				ADM9240_REG_TEMP_MAX(1));

		/* read fan divs and 5-bit VID */
		i = i2c_smbus_read_byte_data(client, ADM9240_REG_VID_FAN_DIV);
		data->fan_div[0] = (i >> 4) & 3;
		data->fan_div[1] = (i >> 6) & 3;
		data->vid = i & 0x0f;
		data->vid |= (i2c_smbus_read_byte_data(client,
					ADM9240_REG_VID4) & 1) << 4;
		/* read analog out */
		data->aout = i2c_smbus_read_byte_data(client,
				ADM9240_REG_ANALOG_OUT);

		data->last_updated_config = jiffies;
		data->valid = 1;
	}
	mutex_unlock(&data->update_lock);
	return data;
}

module_i2c_driver(adm9240_driver);

MODULE_AUTHOR("Michiel Rook <michiel@grendelproject.nl>, "
		"Grant Coady <gcoady.lk@gmail.com> and others");
MODULE_DESCRIPTION("ADM9240/DS1780/LM81 driver");
MODULE_LICENSE("GPL");
