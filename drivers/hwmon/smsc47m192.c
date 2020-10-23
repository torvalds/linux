// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * smsc47m192.c - Support for hardware monitoring block of
 *		  SMSC LPC47M192 and compatible Super I/O chips
 *
 * Copyright (C) 2006  Hartmut Rick <linux@rick.claranet.de>
 *
 * Derived from lm78.c and other chip drivers.
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
#include <linux/sysfs.h>
#include <linux/mutex.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, I2C_CLIENT_END };

/* SMSC47M192 registers */
#define SMSC47M192_REG_IN(nr)		((nr) < 6 ? (0x20 + (nr)) : \
					(0x50 + (nr) - 6))
#define SMSC47M192_REG_IN_MAX(nr)	((nr) < 6 ? (0x2b + (nr) * 2) : \
					(0x54 + (((nr) - 6) * 2)))
#define SMSC47M192_REG_IN_MIN(nr)	((nr) < 6 ? (0x2c + (nr) * 2) : \
					(0x55 + (((nr) - 6) * 2)))
static u8 SMSC47M192_REG_TEMP[3] =	{ 0x27, 0x26, 0x52 };
static u8 SMSC47M192_REG_TEMP_MAX[3] =	{ 0x39, 0x37, 0x58 };
static u8 SMSC47M192_REG_TEMP_MIN[3] =	{ 0x3A, 0x38, 0x59 };
#define SMSC47M192_REG_TEMP_OFFSET(nr)	((nr) == 2 ? 0x1e : 0x1f)
#define SMSC47M192_REG_ALARM1		0x41
#define SMSC47M192_REG_ALARM2		0x42
#define SMSC47M192_REG_VID		0x47
#define SMSC47M192_REG_VID4		0x49
#define SMSC47M192_REG_CONFIG		0x40
#define SMSC47M192_REG_SFR		0x4f
#define SMSC47M192_REG_COMPANY_ID	0x3e
#define SMSC47M192_REG_VERSION		0x3f

/* generalised scaling with integer rounding */
static inline int SCALE(long val, int mul, int div)
{
	if (val < 0)
		return (val * mul - div / 2) / div;
	else
		return (val * mul + div / 2) / div;
}

/* Conversions */

/* smsc47m192 internally scales voltage measurements */
static const u16 nom_mv[] = { 2500, 2250, 3300, 5000, 12000, 3300, 1500, 1800 };

static inline unsigned int IN_FROM_REG(u8 reg, int n)
{
	return SCALE(reg, nom_mv[n], 192);
}

static inline u8 IN_TO_REG(unsigned long val, int n)
{
	val = clamp_val(val, 0, nom_mv[n] * 255 / 192);
	return SCALE(val, 192, nom_mv[n]);
}

/*
 * TEMP: 0.001 degC units (-128C to +127C)
 * REG: 1C/bit, two's complement
 */
static inline s8 TEMP_TO_REG(long val)
{
	return SCALE(clamp_val(val, -128000, 127000), 1, 1000);
}

static inline int TEMP_FROM_REG(s8 val)
{
	return val * 1000;
}

struct smsc47m192_data {
	struct i2c_client *client;
	const struct attribute_group *groups[3];
	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[8];		/* Register value */
	u8 in_max[8];		/* Register value */
	u8 in_min[8];		/* Register value */
	s8 temp[3];		/* Register value */
	s8 temp_max[3];		/* Register value */
	s8 temp_min[3];		/* Register value */
	s8 temp_offset[3];	/* Register value */
	u16 alarms;		/* Register encoding, combined */
	u8 vid;			/* Register encoding, combined */
	u8 vrm;
};

static struct smsc47m192_data *smsc47m192_update_device(struct device *dev)
{
	struct smsc47m192_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i, config;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	 || !data->valid) {
		u8 sfr = i2c_smbus_read_byte_data(client, SMSC47M192_REG_SFR);

		dev_dbg(&client->dev, "Starting smsc47m192 update\n");

		for (i = 0; i <= 7; i++) {
			data->in[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_IN(i));
			data->in_min[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_IN_MIN(i));
			data->in_max[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_IN_MAX(i));
		}
		for (i = 0; i < 3; i++) {
			data->temp[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_TEMP[i]);
			data->temp_max[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_TEMP_MAX[i]);
			data->temp_min[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_TEMP_MIN[i]);
		}
		for (i = 1; i < 3; i++)
			data->temp_offset[i] = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_TEMP_OFFSET(i));
		/*
		 * first offset is temp_offset[0] if SFR bit 4 is set,
		 * temp_offset[1] otherwise
		 */
		if (sfr & 0x10) {
			data->temp_offset[0] = data->temp_offset[1];
			data->temp_offset[1] = 0;
		} else
			data->temp_offset[0] = 0;

		data->vid = i2c_smbus_read_byte_data(client, SMSC47M192_REG_VID)
			    & 0x0f;
		config = i2c_smbus_read_byte_data(client,
						  SMSC47M192_REG_CONFIG);
		if (config & 0x20)
			data->vid |= (i2c_smbus_read_byte_data(client,
					SMSC47M192_REG_VID4) & 0x01) << 4;
		data->alarms = i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_ALARM1) |
			       (i2c_smbus_read_byte_data(client,
						SMSC47M192_REG_ALARM2) << 8);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* Voltages */
static ssize_t in_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in[nr], nr));
}

static ssize_t in_min_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_min[nr], nr));
}

static ssize_t in_max_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_max[nr], nr));
}

static ssize_t in_min_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_min[nr] = IN_TO_REG(val, nr);
	i2c_smbus_write_byte_data(client, SMSC47M192_REG_IN_MIN(nr),
							data->in_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t in_max_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_max[nr] = IN_TO_REG(val, nr);
	i2c_smbus_write_byte_data(client, SMSC47M192_REG_IN_MAX(nr),
							data->in_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(in0_input, in, 0);
static SENSOR_DEVICE_ATTR_RW(in0_min, in_min, 0);
static SENSOR_DEVICE_ATTR_RW(in0_max, in_max, 0);
static SENSOR_DEVICE_ATTR_RO(in1_input, in, 1);
static SENSOR_DEVICE_ATTR_RW(in1_min, in_min, 1);
static SENSOR_DEVICE_ATTR_RW(in1_max, in_max, 1);
static SENSOR_DEVICE_ATTR_RO(in2_input, in, 2);
static SENSOR_DEVICE_ATTR_RW(in2_min, in_min, 2);
static SENSOR_DEVICE_ATTR_RW(in2_max, in_max, 2);
static SENSOR_DEVICE_ATTR_RO(in3_input, in, 3);
static SENSOR_DEVICE_ATTR_RW(in3_min, in_min, 3);
static SENSOR_DEVICE_ATTR_RW(in3_max, in_max, 3);
static SENSOR_DEVICE_ATTR_RO(in4_input, in, 4);
static SENSOR_DEVICE_ATTR_RW(in4_min, in_min, 4);
static SENSOR_DEVICE_ATTR_RW(in4_max, in_max, 4);
static SENSOR_DEVICE_ATTR_RO(in5_input, in, 5);
static SENSOR_DEVICE_ATTR_RW(in5_min, in_min, 5);
static SENSOR_DEVICE_ATTR_RW(in5_max, in_max, 5);
static SENSOR_DEVICE_ATTR_RO(in6_input, in, 6);
static SENSOR_DEVICE_ATTR_RW(in6_min, in_min, 6);
static SENSOR_DEVICE_ATTR_RW(in6_max, in_max, 6);
static SENSOR_DEVICE_ATTR_RO(in7_input, in, 7);
static SENSOR_DEVICE_ATTR_RW(in7_min, in_min, 7);
static SENSOR_DEVICE_ATTR_RW(in7_max, in_max, 7);

/* Temperatures */
static ssize_t temp_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[nr]));
}

static ssize_t temp_min_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_min[nr]));
}

static ssize_t temp_max_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[nr]));
}

static ssize_t temp_min_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_min[nr] = TEMP_TO_REG(val);
	i2c_smbus_write_byte_data(client, SMSC47M192_REG_TEMP_MIN[nr],
						data->temp_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_max_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_max[nr] = TEMP_TO_REG(val);
	i2c_smbus_write_byte_data(client, SMSC47M192_REG_TEMP_MAX[nr],
						data->temp_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_offset_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_offset[nr]));
}

static ssize_t temp_offset_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 sfr = i2c_smbus_read_byte_data(client, SMSC47M192_REG_SFR);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_offset[nr] = TEMP_TO_REG(val);
	if (nr > 1)
		i2c_smbus_write_byte_data(client,
			SMSC47M192_REG_TEMP_OFFSET(nr), data->temp_offset[nr]);
	else if (data->temp_offset[nr] != 0) {
		/*
		 * offset[0] and offset[1] share the same register,
		 * SFR bit 4 activates offset[0]
		 */
		i2c_smbus_write_byte_data(client, SMSC47M192_REG_SFR,
					(sfr & 0xef) | (nr == 0 ? 0x10 : 0));
		data->temp_offset[1-nr] = 0;
		i2c_smbus_write_byte_data(client,
			SMSC47M192_REG_TEMP_OFFSET(nr), data->temp_offset[nr]);
	} else if ((sfr & 0x10) == (nr == 0 ? 0x10 : 0))
		i2c_smbus_write_byte_data(client,
					SMSC47M192_REG_TEMP_OFFSET(nr), 0);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_min, temp_min, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp_max, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_offset, temp_offset, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_min, temp_min, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp_max, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_offset, temp_offset, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_min, temp_min, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_max, temp_max, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_offset, temp_offset, 2);

/* VID */
static ssize_t cpu0_vid_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR_RO(cpu0_vid);

static ssize_t vrm_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct smsc47m192_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->vrm);
}

static ssize_t vrm_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct smsc47m192_data *data = dev_get_drvdata(dev);
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
static DEVICE_ATTR_RW(vrm);

/* Alarms */
static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct smsc47m192_data *data = smsc47m192_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms & nr) ? 1 : 0);
}

static SENSOR_DEVICE_ATTR_RO(temp1_alarm, alarm, 0x0010);
static SENSOR_DEVICE_ATTR_RO(temp2_alarm, alarm, 0x0020);
static SENSOR_DEVICE_ATTR_RO(temp3_alarm, alarm, 0x0040);
static SENSOR_DEVICE_ATTR_RO(temp2_fault, alarm, 0x4000);
static SENSOR_DEVICE_ATTR_RO(temp3_fault, alarm, 0x8000);
static SENSOR_DEVICE_ATTR_RO(in0_alarm, alarm, 0x0001);
static SENSOR_DEVICE_ATTR_RO(in1_alarm, alarm, 0x0002);
static SENSOR_DEVICE_ATTR_RO(in2_alarm, alarm, 0x0004);
static SENSOR_DEVICE_ATTR_RO(in3_alarm, alarm, 0x0008);
static SENSOR_DEVICE_ATTR_RO(in4_alarm, alarm, 0x0100);
static SENSOR_DEVICE_ATTR_RO(in5_alarm, alarm, 0x0200);
static SENSOR_DEVICE_ATTR_RO(in6_alarm, alarm, 0x0400);
static SENSOR_DEVICE_ATTR_RO(in7_alarm, alarm, 0x0800);

static struct attribute *smsc47m192_attributes[] = {
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
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in6_alarm.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in7_alarm.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_offset.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_offset.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_offset.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,

	&dev_attr_cpu0_vid.attr,
	&dev_attr_vrm.attr,
	NULL
};

static const struct attribute_group smsc47m192_group = {
	.attrs = smsc47m192_attributes,
};

static struct attribute *smsc47m192_attributes_in4[] = {
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group smsc47m192_group_in4 = {
	.attrs = smsc47m192_attributes_in4,
};

static void smsc47m192_init_client(struct i2c_client *client)
{
	int i;
	u8 config = i2c_smbus_read_byte_data(client, SMSC47M192_REG_CONFIG);
	u8 sfr = i2c_smbus_read_byte_data(client, SMSC47M192_REG_SFR);

	/* select cycle mode (pause 1 sec between updates) */
	i2c_smbus_write_byte_data(client, SMSC47M192_REG_SFR,
						(sfr & 0xfd) | 0x02);
	if (!(config & 0x01)) {
		/* initialize alarm limits */
		for (i = 0; i < 8; i++) {
			i2c_smbus_write_byte_data(client,
				SMSC47M192_REG_IN_MIN(i), 0);
			i2c_smbus_write_byte_data(client,
				SMSC47M192_REG_IN_MAX(i), 0xff);
		}
		for (i = 0; i < 3; i++) {
			i2c_smbus_write_byte_data(client,
				SMSC47M192_REG_TEMP_MIN[i], 0x80);
			i2c_smbus_write_byte_data(client,
				SMSC47M192_REG_TEMP_MAX[i], 0x7f);
		}

		/* start monitoring */
		i2c_smbus_write_byte_data(client, SMSC47M192_REG_CONFIG,
						(config & 0xf7) | 0x01);
	}
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int smsc47m192_detect(struct i2c_client *client,
			     struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int version;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Detection criteria from sensors_detect script */
	version = i2c_smbus_read_byte_data(client, SMSC47M192_REG_VERSION);
	if (i2c_smbus_read_byte_data(client,
				SMSC47M192_REG_COMPANY_ID) == 0x55
	 && (version & 0xf0) == 0x20
	 && (i2c_smbus_read_byte_data(client,
				SMSC47M192_REG_VID) & 0x70) == 0x00
	 && (i2c_smbus_read_byte_data(client,
				SMSC47M192_REG_VID4) & 0xfe) == 0x80) {
		dev_info(&adapter->dev,
			 "found SMSC47M192 or compatible, "
			 "version 2, stepping A%d\n", version & 0x0f);
	} else {
		dev_dbg(&adapter->dev,
			"SMSC47M192 detection failed at 0x%02x\n",
			client->addr);
		return -ENODEV;
	}

	strlcpy(info->type, "smsc47m192", I2C_NAME_SIZE);

	return 0;
}

static int smsc47m192_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct smsc47m192_data *data;
	int config;

	data = devm_kzalloc(dev, sizeof(struct smsc47m192_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->vrm = vid_which_vrm();
	mutex_init(&data->update_lock);

	/* Initialize the SMSC47M192 chip */
	smsc47m192_init_client(client);

	/* sysfs hooks */
	data->groups[0] = &smsc47m192_group;
	/* Pin 110 is either in4 (+12V) or VID4 */
	config = i2c_smbus_read_byte_data(client, SMSC47M192_REG_CONFIG);
	if (!(config & 0x20))
		data->groups[1] = &smsc47m192_group_in4;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id smsc47m192_id[] = {
	{ "smsc47m192", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, smsc47m192_id);

static struct i2c_driver smsc47m192_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "smsc47m192",
	},
	.probe_new	= smsc47m192_probe,
	.id_table	= smsc47m192_id,
	.detect		= smsc47m192_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(smsc47m192_driver);

MODULE_AUTHOR("Hartmut Rick <linux@rick.claranet.de>");
MODULE_DESCRIPTION("SMSC47M192 driver");
MODULE_LICENSE("GPL");
