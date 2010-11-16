/*
 * asc7621.c - Part of lm_sensors, Linux kernel modules for hardware monitoring
 * Copyright (c) 2007, 2010 George Joseph  <george.joseph@fairview5.com>
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
#include <linux/err.h>
#include <linux/mutex.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = {
	0x2c, 0x2d, 0x2e, I2C_CLIENT_END
};

enum asc7621_type {
	asc7621,
	asc7621a
};

#define INTERVAL_HIGH   (HZ + HZ / 2)
#define INTERVAL_LOW    (1 * 60 * HZ)
#define PRI_NONE        0
#define PRI_LOW         1
#define PRI_HIGH        2
#define FIRST_CHIP      asc7621
#define LAST_CHIP       asc7621a

struct asc7621_chip {
	char *name;
	enum asc7621_type chip_type;
	u8 company_reg;
	u8 company_id;
	u8 verstep_reg;
	u8 verstep_id;
	const unsigned short *addresses;
};

static struct asc7621_chip asc7621_chips[] = {
	{
		.name = "asc7621",
		.chip_type = asc7621,
		.company_reg = 0x3e,
		.company_id = 0x61,
		.verstep_reg = 0x3f,
		.verstep_id = 0x6c,
		.addresses = normal_i2c,
	 },
	{
		.name = "asc7621a",
		.chip_type = asc7621a,
		.company_reg = 0x3e,
		.company_id = 0x61,
		.verstep_reg = 0x3f,
		.verstep_id = 0x6d,
		.addresses = normal_i2c,
	 },
};

/*
 * Defines the highest register to be used, not the count.
 * The actual count will probably be smaller because of gaps
 * in the implementation (unused register locations).
 * This define will safely set the array size of both the parameter
 * and data arrays.
 * This comes from the data sheet register description table.
 */
#define LAST_REGISTER 0xff

struct asc7621_data {
	struct i2c_client client;
	struct device *class_dev;
	struct mutex update_lock;
	int valid;		/* !=0 if following fields are valid */
	unsigned long last_high_reading;	/* In jiffies */
	unsigned long last_low_reading;		/* In jiffies */
	/*
	 * Registers we care about occupy the corresponding index
	 * in the array.  Registers we don't care about are left
	 * at 0.
	 */
	u8 reg[LAST_REGISTER + 1];
};

/*
 * Macro to get the parent asc7621_param structure
 * from a sensor_device_attribute passed into the
 * show/store functions.
 */
#define to_asc7621_param(_sda) \
	container_of(_sda, struct asc7621_param, sda)

/*
 * Each parameter to be retrieved needs an asc7621_param structure
 * allocated.  It contains the sensor_device_attribute structure
 * and the control info needed to retrieve the value from the register map.
 */
struct asc7621_param {
	struct sensor_device_attribute sda;
	u8 priority;
	u8 msb[3];
	u8 lsb[3];
	u8 mask[3];
	u8 shift[3];
};

/*
 * This is the map that ultimately indicates whether we'll be
 * retrieving a register value or not, and at what frequency.
 */
static u8 asc7621_register_priorities[255];

static struct asc7621_data *asc7621_update_device(struct device *dev);

static inline u8 read_byte(struct i2c_client *client, u8 reg)
{
	int res = i2c_smbus_read_byte_data(client, reg);
	if (res < 0) {
		dev_err(&client->dev,
			"Unable to read from register 0x%02x.\n", reg);
		return 0;
	};
	return res & 0xff;
}

static inline int write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int res = i2c_smbus_write_byte_data(client, reg, data);
	if (res < 0) {
		dev_err(&client->dev,
			"Unable to write value 0x%02x to register 0x%02x.\n",
			data, reg);
	};
	return res;
}

/*
 * Data Handlers
 * Each function handles the formatting, storage
 * and retrieval of like parameters.
 */

#define SETUP_SHOW_data_param(d, a) \
	struct sensor_device_attribute *sda = to_sensor_dev_attr(a); \
	struct asc7621_data *data = asc7621_update_device(d); \
	struct asc7621_param *param = to_asc7621_param(sda)

#define SETUP_STORE_data_param(d, a) \
	struct sensor_device_attribute *sda = to_sensor_dev_attr(a); \
	struct i2c_client *client = to_i2c_client(d); \
	struct asc7621_data *data = i2c_get_clientdata(client); \
	struct asc7621_param *param = to_asc7621_param(sda)

/*
 * u8 is just what it sounds like...an unsigned byte with no
 * special formatting.
 */
static ssize_t show_u8(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	SETUP_SHOW_data_param(dev, attr);

	return sprintf(buf, "%u\n", data->reg[param->msb[0]]);
}

static ssize_t store_u8(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	long reqval;

	if (strict_strtol(buf, 10, &reqval))
		return -EINVAL;

	reqval = SENSORS_LIMIT(reqval, 0, 255);

	mutex_lock(&data->update_lock);
	data->reg[param->msb[0]] = reqval;
	write_byte(client, param->msb[0], reqval);
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * Many of the config values occupy only a few bits of a register.
 */
static ssize_t show_bitmask(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);

	return sprintf(buf, "%u\n",
		       (data->reg[param->msb[0]] >> param->
			shift[0]) & param->mask[0]);
}

static ssize_t store_bitmask(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	long reqval;
	u8 currval;

	if (strict_strtol(buf, 10, &reqval))
		return -EINVAL;

	reqval = SENSORS_LIMIT(reqval, 0, param->mask[0]);

	reqval = (reqval & param->mask[0]) << param->shift[0];

	mutex_lock(&data->update_lock);
	currval = read_byte(client, param->msb[0]);
	reqval |= (currval & ~(param->mask[0] << param->shift[0]));
	data->reg[param->msb[0]] = reqval;
	write_byte(client, param->msb[0], reqval);
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * 16 bit fan rpm values
 * reported by the device as the number of 11.111us periods (90khz)
 * between full fan rotations.  Therefore...
 * RPM = (90000 * 60) / register value
 */
static ssize_t show_fan16(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	u16 regval;

	mutex_lock(&data->update_lock);
	regval = (data->reg[param->msb[0]] << 8) | data->reg[param->lsb[0]];
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%u\n",
		       (regval == 0 ? -1 : (regval) ==
			0xffff ? 0 : 5400000 / regval));
}

static ssize_t store_fan16(struct device *dev,
			   struct device_attribute *attr, const char *buf,
			   size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	long reqval;

	if (strict_strtol(buf, 10, &reqval))
		return -EINVAL;

	/* If a minimum RPM of zero is requested, then we set the register to
	   0xffff. This value allows the fan to be stopped completely without
	   generating an alarm. */
	reqval =
	    (reqval <= 0 ? 0xffff : SENSORS_LIMIT(5400000 / reqval, 0, 0xfffe));

	mutex_lock(&data->update_lock);
	data->reg[param->msb[0]] = (reqval >> 8) & 0xff;
	data->reg[param->lsb[0]] = reqval & 0xff;
	write_byte(client, param->msb[0], data->reg[param->msb[0]]);
	write_byte(client, param->lsb[0], data->reg[param->lsb[0]]);
	mutex_unlock(&data->update_lock);

	return count;
}

/*
 * Voltages are scaled in the device so that the nominal voltage
 * is 3/4ths of the 0-255 range (i.e. 192).
 * If all voltages are 'normal' then all voltage registers will
 * read 0xC0.
 *
 * The data sheet provides us with the 3/4 scale value for each voltage
 * which is stored in in_scaling.  The sda->index parameter value provides
 * the index into in_scaling.
 *
 * NOTE: The chip expects the first 2 inputs be 2.5 and 2.25 volts
 * respectively. That doesn't mean that's what the motherboard provides. :)
 */

static int asc7621_in_scaling[] = {
	2500, 2250, 3300, 5000, 12000
};

static ssize_t show_in10(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	u16 regval;
	u8 nr = sda->index;

	mutex_lock(&data->update_lock);
	regval = (data->reg[param->msb[0]] << 8) | (data->reg[param->lsb[0]]);
	mutex_unlock(&data->update_lock);

	/* The LSB value is a 2-bit scaling of the MSB's LSbit value. */
	regval = (regval >> 6) * asc7621_in_scaling[nr] / (0xc0 << 2);

	return sprintf(buf, "%u\n", regval);
}

/* 8 bit voltage values (the mins and maxs) */
static ssize_t show_in8(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	u8 nr = sda->index;

	return sprintf(buf, "%u\n",
		       ((data->reg[param->msb[0]] *
			 asc7621_in_scaling[nr]) / 0xc0));
}

static ssize_t store_in8(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	long reqval;
	u8 nr = sda->index;

	if (strict_strtol(buf, 10, &reqval))
		return -EINVAL;

	reqval = SENSORS_LIMIT(reqval, 0, 0xffff);

	reqval = reqval * 0xc0 / asc7621_in_scaling[nr];

	reqval = SENSORS_LIMIT(reqval, 0, 0xff);

	mutex_lock(&data->update_lock);
	data->reg[param->msb[0]] = reqval;
	write_byte(client, param->msb[0], reqval);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_temp8(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);

	return sprintf(buf, "%d\n", ((s8) data->reg[param->msb[0]]) * 1000);
}

static ssize_t store_temp8(struct device *dev,
			   struct device_attribute *attr, const char *buf,
			   size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	long reqval;
	s8 temp;

	if (strict_strtol(buf, 10, &reqval))
		return -EINVAL;

	reqval = SENSORS_LIMIT(reqval, -127000, 127000);

	temp = reqval / 1000;

	mutex_lock(&data->update_lock);
	data->reg[param->msb[0]] = temp;
	write_byte(client, param->msb[0], temp);
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * Temperatures that occupy 2 bytes always have the whole
 * number of degrees in the MSB with some part of the LSB
 * indicating fractional degrees.
 */

/*   mmmmmmmm.llxxxxxx */
static ssize_t show_temp10(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	u8 msb, lsb;
	int temp;

	mutex_lock(&data->update_lock);
	msb = data->reg[param->msb[0]];
	lsb = (data->reg[param->lsb[0]] >> 6) & 0x03;
	temp = (((s8) msb) * 1000) + (lsb * 250);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", temp);
}

/*   mmmmmm.ll */
static ssize_t show_temp62(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	u8 regval = data->reg[param->msb[0]];
	int temp = ((s8) (regval & 0xfc) * 1000) + ((regval & 0x03) * 250);

	return sprintf(buf, "%d\n", temp);
}

static ssize_t store_temp62(struct device *dev,
			    struct device_attribute *attr, const char *buf,
			    size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	long reqval, i, f;
	s8 temp;

	if (strict_strtol(buf, 10, &reqval))
		return -EINVAL;

	reqval = SENSORS_LIMIT(reqval, -32000, 31750);
	i = reqval / 1000;
	f = reqval - (i * 1000);
	temp = i << 2;
	temp |= f / 250;

	mutex_lock(&data->update_lock);
	data->reg[param->msb[0]] = temp;
	write_byte(client, param->msb[0], temp);
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * The aSC7621 doesn't provide an "auto_point2".  Instead, you
 * specify the auto_point1 and a range.  To keep with the sysfs
 * hwmon specs, we synthesize the auto_point_2 from them.
 */

static u32 asc7621_range_map[] = {
	2000, 2500, 3330, 4000, 5000, 6670, 8000, 10000,
	13330, 16000, 20000, 26670, 32000, 40000, 53330, 80000,
};

static ssize_t show_ap2_temp(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	long auto_point1;
	u8 regval;
	int temp;

	mutex_lock(&data->update_lock);
	auto_point1 = ((s8) data->reg[param->msb[1]]) * 1000;
	regval =
	    ((data->reg[param->msb[0]] >> param->shift[0]) & param->mask[0]);
	temp = auto_point1 + asc7621_range_map[SENSORS_LIMIT(regval, 0, 15)];
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", temp);

}

static ssize_t store_ap2_temp(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	long reqval, auto_point1;
	int i;
	u8 currval, newval = 0;

	if (strict_strtol(buf, 10, &reqval))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	auto_point1 = data->reg[param->msb[1]] * 1000;
	reqval = SENSORS_LIMIT(reqval, auto_point1 + 2000, auto_point1 + 80000);

	for (i = ARRAY_SIZE(asc7621_range_map) - 1; i >= 0; i--) {
		if (reqval >= auto_point1 + asc7621_range_map[i]) {
			newval = i;
			break;
		}
	}

	newval = (newval & param->mask[0]) << param->shift[0];
	currval = read_byte(client, param->msb[0]);
	newval |= (currval & ~(param->mask[0] << param->shift[0]));
	data->reg[param->msb[0]] = newval;
	write_byte(client, param->msb[0], newval);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_pwm_ac(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	u8 config, altbit, regval;
	u8 map[] = {
		0x01, 0x02, 0x04, 0x1f, 0x00, 0x06, 0x07, 0x10,
		0x08, 0x0f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f
	};

	mutex_lock(&data->update_lock);
	config = (data->reg[param->msb[0]] >> param->shift[0]) & param->mask[0];
	altbit = (data->reg[param->msb[1]] >> param->shift[1]) & param->mask[1];
	regval = config | (altbit << 3);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%u\n", map[SENSORS_LIMIT(regval, 0, 15)]);
}

static ssize_t store_pwm_ac(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	unsigned long reqval;
	u8 currval, config, altbit, newval;
	u16 map[] = {
		0x04, 0x00, 0x01, 0xff, 0x02, 0xff, 0x05, 0x06,
		0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f,
		0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03,
	};

	if (strict_strtoul(buf, 10, &reqval))
		return -EINVAL;

	if (reqval > 31)
		return -EINVAL;

	reqval = map[reqval];
	if (reqval == 0xff)
		return -EINVAL;

	config = reqval & 0x07;
	altbit = (reqval >> 3) & 0x01;

	config = (config & param->mask[0]) << param->shift[0];
	altbit = (altbit & param->mask[1]) << param->shift[1];

	mutex_lock(&data->update_lock);
	currval = read_byte(client, param->msb[0]);
	newval = config | (currval & ~(param->mask[0] << param->shift[0]));
	newval = altbit | (newval & ~(param->mask[1] << param->shift[1]));
	data->reg[param->msb[0]] = newval;
	write_byte(client, param->msb[0], newval);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_pwm_enable(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	u8 config, altbit, minoff, val, newval;

	mutex_lock(&data->update_lock);
	config = (data->reg[param->msb[0]] >> param->shift[0]) & param->mask[0];
	altbit = (data->reg[param->msb[1]] >> param->shift[1]) & param->mask[1];
	minoff = (data->reg[param->msb[2]] >> param->shift[2]) & param->mask[2];
	mutex_unlock(&data->update_lock);

	val = config | (altbit << 3);
	newval = 0;

	if (val == 3 || val >= 10)
		newval = 255;
	else if (val == 4)
		newval = 0;
	else if (val == 7)
		newval = 1;
	else if (minoff == 1)
		newval = 2;
	else
		newval = 3;

	return sprintf(buf, "%u\n", newval);
}

static ssize_t store_pwm_enable(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	long reqval;
	u8 currval, config, altbit, newval, minoff = 255;

	if (strict_strtol(buf, 10, &reqval))
		return -EINVAL;

	switch (reqval) {
	case 0:
		newval = 0x04;
		break;
	case 1:
		newval = 0x07;
		break;
	case 2:
		newval = 0x00;
		minoff = 1;
		break;
	case 3:
		newval = 0x00;
		minoff = 0;
		break;
	case 255:
		newval = 0x03;
		break;
	default:
		return -EINVAL;
	}

	config = newval & 0x07;
	altbit = (newval >> 3) & 0x01;

	mutex_lock(&data->update_lock);
	config = (config & param->mask[0]) << param->shift[0];
	altbit = (altbit & param->mask[1]) << param->shift[1];
	currval = read_byte(client, param->msb[0]);
	newval = config | (currval & ~(param->mask[0] << param->shift[0]));
	newval = altbit | (newval & ~(param->mask[1] << param->shift[1]));
	data->reg[param->msb[0]] = newval;
	write_byte(client, param->msb[0], newval);
	if (minoff < 255) {
		minoff = (minoff & param->mask[2]) << param->shift[2];
		currval = read_byte(client, param->msb[2]);
		newval =
		    minoff | (currval & ~(param->mask[2] << param->shift[2]));
		data->reg[param->msb[2]] = newval;
		write_byte(client, param->msb[2], newval);
	}
	mutex_unlock(&data->update_lock);
	return count;
}

static u32 asc7621_pwm_freq_map[] = {
	10, 15, 23, 30, 38, 47, 62, 94,
	23000, 24000, 25000, 26000, 27000, 28000, 29000, 30000
};

static ssize_t show_pwm_freq(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	u8 regval =
	    (data->reg[param->msb[0]] >> param->shift[0]) & param->mask[0];

	regval = SENSORS_LIMIT(regval, 0, 15);

	return sprintf(buf, "%u\n", asc7621_pwm_freq_map[regval]);
}

static ssize_t store_pwm_freq(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	unsigned long reqval;
	u8 currval, newval = 255;
	int i;

	if (strict_strtoul(buf, 10, &reqval))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(asc7621_pwm_freq_map); i++) {
		if (reqval == asc7621_pwm_freq_map[i]) {
			newval = i;
			break;
		}
	}
	if (newval == 255)
		return -EINVAL;

	newval = (newval & param->mask[0]) << param->shift[0];

	mutex_lock(&data->update_lock);
	currval = read_byte(client, param->msb[0]);
	newval |= (currval & ~(param->mask[0] << param->shift[0]));
	data->reg[param->msb[0]] = newval;
	write_byte(client, param->msb[0], newval);
	mutex_unlock(&data->update_lock);
	return count;
}

static u32 asc7621_pwm_auto_spinup_map[] =  {
	0, 100, 250, 400, 700, 1000, 2000, 4000
};

static ssize_t show_pwm_ast(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	u8 regval =
	    (data->reg[param->msb[0]] >> param->shift[0]) & param->mask[0];

	regval = SENSORS_LIMIT(regval, 0, 7);

	return sprintf(buf, "%u\n", asc7621_pwm_auto_spinup_map[regval]);

}

static ssize_t store_pwm_ast(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	long reqval;
	u8 currval, newval = 255;
	u32 i;

	if (strict_strtol(buf, 10, &reqval))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(asc7621_pwm_auto_spinup_map); i++) {
		if (reqval == asc7621_pwm_auto_spinup_map[i]) {
			newval = i;
			break;
		}
	}
	if (newval == 255)
		return -EINVAL;

	newval = (newval & param->mask[0]) << param->shift[0];

	mutex_lock(&data->update_lock);
	currval = read_byte(client, param->msb[0]);
	newval |= (currval & ~(param->mask[0] << param->shift[0]));
	data->reg[param->msb[0]] = newval;
	write_byte(client, param->msb[0], newval);
	mutex_unlock(&data->update_lock);
	return count;
}

static u32 asc7621_temp_smoothing_time_map[] = {
	35000, 17600, 11800, 7000, 4400, 3000, 1600, 800
};

static ssize_t show_temp_st(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	SETUP_SHOW_data_param(dev, attr);
	u8 regval =
	    (data->reg[param->msb[0]] >> param->shift[0]) & param->mask[0];
	regval = SENSORS_LIMIT(regval, 0, 7);

	return sprintf(buf, "%u\n", asc7621_temp_smoothing_time_map[regval]);
}

static ssize_t store_temp_st(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	SETUP_STORE_data_param(dev, attr);
	long reqval;
	u8 currval, newval = 255;
	u32 i;

	if (strict_strtol(buf, 10, &reqval))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(asc7621_temp_smoothing_time_map); i++) {
		if (reqval == asc7621_temp_smoothing_time_map[i]) {
			newval = i;
			break;
		}
	}

	if (newval == 255)
		return -EINVAL;

	newval = (newval & param->mask[0]) << param->shift[0];

	mutex_lock(&data->update_lock);
	currval = read_byte(client, param->msb[0]);
	newval |= (currval & ~(param->mask[0] << param->shift[0]));
	data->reg[param->msb[0]] = newval;
	write_byte(client, param->msb[0], newval);
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * End of data handlers
 *
 * These defines do nothing more than make the table easier
 * to read when wrapped at column 80.
 */

/*
 * Creates a variable length array inititalizer.
 * VAA(1,3,5,7) would produce {1,3,5,7}
 */
#define VAA(args...) {args}

#define PREAD(name, n, pri, rm, rl, m, s, r) \
	{.sda = SENSOR_ATTR(name, S_IRUGO, show_##r, NULL, n), \
	  .priority = pri, .msb[0] = rm, .lsb[0] = rl, .mask[0] = m, \
	  .shift[0] = s,}

#define PWRITE(name, n, pri, rm, rl, m, s, r) \
	{.sda = SENSOR_ATTR(name, S_IRUGO | S_IWUSR, show_##r, store_##r, n), \
	  .priority = pri, .msb[0] = rm, .lsb[0] = rl, .mask[0] = m, \
	  .shift[0] = s,}

/*
 * PWRITEM assumes that the initializers for the .msb, .lsb, .mask and .shift
 * were created using the VAA macro.
 */
#define PWRITEM(name, n, pri, rm, rl, m, s, r) \
	{.sda = SENSOR_ATTR(name, S_IRUGO | S_IWUSR, show_##r, store_##r, n), \
	  .priority = pri, .msb = rm, .lsb = rl, .mask = m, .shift = s,}

static struct asc7621_param asc7621_params[] = {
	PREAD(in0_input, 0, PRI_HIGH, 0x20, 0x13, 0, 0, in10),
	PREAD(in1_input, 1, PRI_HIGH, 0x21, 0x18, 0, 0, in10),
	PREAD(in2_input, 2, PRI_HIGH, 0x22, 0x11, 0, 0, in10),
	PREAD(in3_input, 3, PRI_HIGH, 0x23, 0x12, 0, 0, in10),
	PREAD(in4_input, 4, PRI_HIGH, 0x24, 0x14, 0, 0, in10),

	PWRITE(in0_min, 0, PRI_LOW, 0x44, 0, 0, 0, in8),
	PWRITE(in1_min, 1, PRI_LOW, 0x46, 0, 0, 0, in8),
	PWRITE(in2_min, 2, PRI_LOW, 0x48, 0, 0, 0, in8),
	PWRITE(in3_min, 3, PRI_LOW, 0x4a, 0, 0, 0, in8),
	PWRITE(in4_min, 4, PRI_LOW, 0x4c, 0, 0, 0, in8),

	PWRITE(in0_max, 0, PRI_LOW, 0x45, 0, 0, 0, in8),
	PWRITE(in1_max, 1, PRI_LOW, 0x47, 0, 0, 0, in8),
	PWRITE(in2_max, 2, PRI_LOW, 0x49, 0, 0, 0, in8),
	PWRITE(in3_max, 3, PRI_LOW, 0x4b, 0, 0, 0, in8),
	PWRITE(in4_max, 4, PRI_LOW, 0x4d, 0, 0, 0, in8),

	PREAD(in0_alarm, 0, PRI_HIGH, 0x41, 0, 0x01, 0, bitmask),
	PREAD(in1_alarm, 1, PRI_HIGH, 0x41, 0, 0x01, 1, bitmask),
	PREAD(in2_alarm, 2, PRI_HIGH, 0x41, 0, 0x01, 2, bitmask),
	PREAD(in3_alarm, 3, PRI_HIGH, 0x41, 0, 0x01, 3, bitmask),
	PREAD(in4_alarm, 4, PRI_HIGH, 0x42, 0, 0x01, 0, bitmask),

	PREAD(fan1_input, 0, PRI_HIGH, 0x29, 0x28, 0, 0, fan16),
	PREAD(fan2_input, 1, PRI_HIGH, 0x2b, 0x2a, 0, 0, fan16),
	PREAD(fan3_input, 2, PRI_HIGH, 0x2d, 0x2c, 0, 0, fan16),
	PREAD(fan4_input, 3, PRI_HIGH, 0x2f, 0x2e, 0, 0, fan16),

	PWRITE(fan1_min, 0, PRI_LOW, 0x55, 0x54, 0, 0, fan16),
	PWRITE(fan2_min, 1, PRI_LOW, 0x57, 0x56, 0, 0, fan16),
	PWRITE(fan3_min, 2, PRI_LOW, 0x59, 0x58, 0, 0, fan16),
	PWRITE(fan4_min, 3, PRI_LOW, 0x5b, 0x5a, 0, 0, fan16),

	PREAD(fan1_alarm, 0, PRI_HIGH, 0x42, 0, 0x01, 2, bitmask),
	PREAD(fan2_alarm, 1, PRI_HIGH, 0x42, 0, 0x01, 3, bitmask),
	PREAD(fan3_alarm, 2, PRI_HIGH, 0x42, 0, 0x01, 4, bitmask),
	PREAD(fan4_alarm, 3, PRI_HIGH, 0x42, 0, 0x01, 5, bitmask),

	PREAD(temp1_input, 0, PRI_HIGH, 0x25, 0x10, 0, 0, temp10),
	PREAD(temp2_input, 1, PRI_HIGH, 0x26, 0x15, 0, 0, temp10),
	PREAD(temp3_input, 2, PRI_HIGH, 0x27, 0x16, 0, 0, temp10),
	PREAD(temp4_input, 3, PRI_HIGH, 0x33, 0x17, 0, 0, temp10),
	PREAD(temp5_input, 4, PRI_HIGH, 0xf7, 0xf6, 0, 0, temp10),
	PREAD(temp6_input, 5, PRI_HIGH, 0xf9, 0xf8, 0, 0, temp10),
	PREAD(temp7_input, 6, PRI_HIGH, 0xfb, 0xfa, 0, 0, temp10),
	PREAD(temp8_input, 7, PRI_HIGH, 0xfd, 0xfc, 0, 0, temp10),

	PWRITE(temp1_min, 0, PRI_LOW, 0x4e, 0, 0, 0, temp8),
	PWRITE(temp2_min, 1, PRI_LOW, 0x50, 0, 0, 0, temp8),
	PWRITE(temp3_min, 2, PRI_LOW, 0x52, 0, 0, 0, temp8),
	PWRITE(temp4_min, 3, PRI_LOW, 0x34, 0, 0, 0, temp8),

	PWRITE(temp1_max, 0, PRI_LOW, 0x4f, 0, 0, 0, temp8),
	PWRITE(temp2_max, 1, PRI_LOW, 0x51, 0, 0, 0, temp8),
	PWRITE(temp3_max, 2, PRI_LOW, 0x53, 0, 0, 0, temp8),
	PWRITE(temp4_max, 3, PRI_LOW, 0x35, 0, 0, 0, temp8),

	PREAD(temp1_alarm, 0, PRI_HIGH, 0x41, 0, 0x01, 4, bitmask),
	PREAD(temp2_alarm, 1, PRI_HIGH, 0x41, 0, 0x01, 5, bitmask),
	PREAD(temp3_alarm, 2, PRI_HIGH, 0x41, 0, 0x01, 6, bitmask),
	PREAD(temp4_alarm, 3, PRI_HIGH, 0x43, 0, 0x01, 0, bitmask),

	PWRITE(temp1_source, 0, PRI_LOW, 0x02, 0, 0x07, 4, bitmask),
	PWRITE(temp2_source, 1, PRI_LOW, 0x02, 0, 0x07, 0, bitmask),
	PWRITE(temp3_source, 2, PRI_LOW, 0x03, 0, 0x07, 4, bitmask),
	PWRITE(temp4_source, 3, PRI_LOW, 0x03, 0, 0x07, 0, bitmask),

	PWRITE(temp1_smoothing_enable, 0, PRI_LOW, 0x62, 0, 0x01, 3, bitmask),
	PWRITE(temp2_smoothing_enable, 1, PRI_LOW, 0x63, 0, 0x01, 7, bitmask),
	PWRITE(temp3_smoothing_enable, 2, PRI_LOW, 0x63, 0, 0x01, 3, bitmask),
	PWRITE(temp4_smoothing_enable, 3, PRI_LOW, 0x3c, 0, 0x01, 3, bitmask),

	PWRITE(temp1_smoothing_time, 0, PRI_LOW, 0x62, 0, 0x07, 0, temp_st),
	PWRITE(temp2_smoothing_time, 1, PRI_LOW, 0x63, 0, 0x07, 4, temp_st),
	PWRITE(temp3_smoothing_time, 2, PRI_LOW, 0x63, 0, 0x07, 0, temp_st),
	PWRITE(temp4_smoothing_time, 3, PRI_LOW, 0x3c, 0, 0x07, 0, temp_st),

	PWRITE(temp1_auto_point1_temp_hyst, 0, PRI_LOW, 0x6d, 0, 0x0f, 4,
	       bitmask),
	PWRITE(temp2_auto_point1_temp_hyst, 1, PRI_LOW, 0x6d, 0, 0x0f, 0,
	       bitmask),
	PWRITE(temp3_auto_point1_temp_hyst, 2, PRI_LOW, 0x6e, 0, 0x0f, 4,
	       bitmask),
	PWRITE(temp4_auto_point1_temp_hyst, 3, PRI_LOW, 0x6e, 0, 0x0f, 0,
	       bitmask),

	PREAD(temp1_auto_point2_temp_hyst, 0, PRI_LOW, 0x6d, 0, 0x0f, 4,
	      bitmask),
	PREAD(temp2_auto_point2_temp_hyst, 1, PRI_LOW, 0x6d, 0, 0x0f, 0,
	      bitmask),
	PREAD(temp3_auto_point2_temp_hyst, 2, PRI_LOW, 0x6e, 0, 0x0f, 4,
	      bitmask),
	PREAD(temp4_auto_point2_temp_hyst, 3, PRI_LOW, 0x6e, 0, 0x0f, 0,
	      bitmask),

	PWRITE(temp1_auto_point1_temp, 0, PRI_LOW, 0x67, 0, 0, 0, temp8),
	PWRITE(temp2_auto_point1_temp, 1, PRI_LOW, 0x68, 0, 0, 0, temp8),
	PWRITE(temp3_auto_point1_temp, 2, PRI_LOW, 0x69, 0, 0, 0, temp8),
	PWRITE(temp4_auto_point1_temp, 3, PRI_LOW, 0x3b, 0, 0, 0, temp8),

	PWRITEM(temp1_auto_point2_temp, 0, PRI_LOW, VAA(0x5f, 0x67), VAA(0),
		VAA(0x0f), VAA(4), ap2_temp),
	PWRITEM(temp2_auto_point2_temp, 1, PRI_LOW, VAA(0x60, 0x68), VAA(0),
		VAA(0x0f), VAA(4), ap2_temp),
	PWRITEM(temp3_auto_point2_temp, 2, PRI_LOW, VAA(0x61, 0x69), VAA(0),
		VAA(0x0f), VAA(4), ap2_temp),
	PWRITEM(temp4_auto_point2_temp, 3, PRI_LOW, VAA(0x3c, 0x3b), VAA(0),
		VAA(0x0f), VAA(4), ap2_temp),

	PWRITE(temp1_crit, 0, PRI_LOW, 0x6a, 0, 0, 0, temp8),
	PWRITE(temp2_crit, 1, PRI_LOW, 0x6b, 0, 0, 0, temp8),
	PWRITE(temp3_crit, 2, PRI_LOW, 0x6c, 0, 0, 0, temp8),
	PWRITE(temp4_crit, 3, PRI_LOW, 0x3d, 0, 0, 0, temp8),

	PWRITE(temp5_enable, 4, PRI_LOW, 0x0e, 0, 0x01, 0, bitmask),
	PWRITE(temp6_enable, 5, PRI_LOW, 0x0e, 0, 0x01, 1, bitmask),
	PWRITE(temp7_enable, 6, PRI_LOW, 0x0e, 0, 0x01, 2, bitmask),
	PWRITE(temp8_enable, 7, PRI_LOW, 0x0e, 0, 0x01, 3, bitmask),

	PWRITE(remote1_offset, 0, PRI_LOW, 0x1c, 0, 0, 0, temp62),
	PWRITE(remote2_offset, 1, PRI_LOW, 0x1d, 0, 0, 0, temp62),

	PWRITE(pwm1, 0, PRI_HIGH, 0x30, 0, 0, 0, u8),
	PWRITE(pwm2, 1, PRI_HIGH, 0x31, 0, 0, 0, u8),
	PWRITE(pwm3, 2, PRI_HIGH, 0x32, 0, 0, 0, u8),

	PWRITE(pwm1_invert, 0, PRI_LOW, 0x5c, 0, 0x01, 4, bitmask),
	PWRITE(pwm2_invert, 1, PRI_LOW, 0x5d, 0, 0x01, 4, bitmask),
	PWRITE(pwm3_invert, 2, PRI_LOW, 0x5e, 0, 0x01, 4, bitmask),

	PWRITEM(pwm1_enable, 0, PRI_LOW, VAA(0x5c, 0x5c, 0x62), VAA(0, 0, 0),
		VAA(0x07, 0x01, 0x01), VAA(5, 3, 5), pwm_enable),
	PWRITEM(pwm2_enable, 1, PRI_LOW, VAA(0x5d, 0x5d, 0x62), VAA(0, 0, 0),
		VAA(0x07, 0x01, 0x01), VAA(5, 3, 6), pwm_enable),
	PWRITEM(pwm3_enable, 2, PRI_LOW, VAA(0x5e, 0x5e, 0x62), VAA(0, 0, 0),
		VAA(0x07, 0x01, 0x01), VAA(5, 3, 7), pwm_enable),

	PWRITEM(pwm1_auto_channels, 0, PRI_LOW, VAA(0x5c, 0x5c), VAA(0, 0),
		VAA(0x07, 0x01), VAA(5, 3), pwm_ac),
	PWRITEM(pwm2_auto_channels, 1, PRI_LOW, VAA(0x5d, 0x5d), VAA(0, 0),
		VAA(0x07, 0x01), VAA(5, 3), pwm_ac),
	PWRITEM(pwm3_auto_channels, 2, PRI_LOW, VAA(0x5e, 0x5e), VAA(0, 0),
		VAA(0x07, 0x01), VAA(5, 3), pwm_ac),

	PWRITE(pwm1_auto_point1_pwm, 0, PRI_LOW, 0x64, 0, 0, 0, u8),
	PWRITE(pwm2_auto_point1_pwm, 1, PRI_LOW, 0x65, 0, 0, 0, u8),
	PWRITE(pwm3_auto_point1_pwm, 2, PRI_LOW, 0x66, 0, 0, 0, u8),

	PWRITE(pwm1_auto_point2_pwm, 0, PRI_LOW, 0x38, 0, 0, 0, u8),
	PWRITE(pwm2_auto_point2_pwm, 1, PRI_LOW, 0x39, 0, 0, 0, u8),
	PWRITE(pwm3_auto_point2_pwm, 2, PRI_LOW, 0x3a, 0, 0, 0, u8),

	PWRITE(pwm1_freq, 0, PRI_LOW, 0x5f, 0, 0x0f, 0, pwm_freq),
	PWRITE(pwm2_freq, 1, PRI_LOW, 0x60, 0, 0x0f, 0, pwm_freq),
	PWRITE(pwm3_freq, 2, PRI_LOW, 0x61, 0, 0x0f, 0, pwm_freq),

	PREAD(pwm1_auto_zone_assigned, 0, PRI_LOW, 0, 0, 0x03, 2, bitmask),
	PREAD(pwm2_auto_zone_assigned, 1, PRI_LOW, 0, 0, 0x03, 4, bitmask),
	PREAD(pwm3_auto_zone_assigned, 2, PRI_LOW, 0, 0, 0x03, 6, bitmask),

	PWRITE(pwm1_auto_spinup_time, 0, PRI_LOW, 0x5c, 0, 0x07, 0, pwm_ast),
	PWRITE(pwm2_auto_spinup_time, 1, PRI_LOW, 0x5d, 0, 0x07, 0, pwm_ast),
	PWRITE(pwm3_auto_spinup_time, 2, PRI_LOW, 0x5e, 0, 0x07, 0, pwm_ast),

	PWRITE(peci_enable, 0, PRI_LOW, 0x40, 0, 0x01, 4, bitmask),
	PWRITE(peci_avg, 0, PRI_LOW, 0x36, 0, 0x07, 0, bitmask),
	PWRITE(peci_domain, 0, PRI_LOW, 0x36, 0, 0x01, 3, bitmask),
	PWRITE(peci_legacy, 0, PRI_LOW, 0x36, 0, 0x01, 4, bitmask),
	PWRITE(peci_diode, 0, PRI_LOW, 0x0e, 0, 0x07, 4, bitmask),
	PWRITE(peci_4domain, 0, PRI_LOW, 0x0e, 0, 0x01, 4, bitmask),

};

static struct asc7621_data *asc7621_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct asc7621_data *data = i2c_get_clientdata(client);
	int i;

/*
 * The asc7621 chips guarantee consistent reads of multi-byte values
 * regardless of the order of the reads.  No special logic is needed
 * so we can just read the registers in whatever  order they appear
 * in the asc7621_params array.
 */

	mutex_lock(&data->update_lock);

	/* Read all the high priority registers */

	if (!data->valid ||
	    time_after(jiffies, data->last_high_reading + INTERVAL_HIGH)) {

		for (i = 0; i < ARRAY_SIZE(asc7621_register_priorities); i++) {
			if (asc7621_register_priorities[i] == PRI_HIGH) {
				data->reg[i] =
				    i2c_smbus_read_byte_data(client, i) & 0xff;
			}
		}
		data->last_high_reading = jiffies;
	};			/* last_reading */

	/* Read all the low priority registers. */

	if (!data->valid ||
	    time_after(jiffies, data->last_low_reading + INTERVAL_LOW)) {

		for (i = 0; i < ARRAY_SIZE(asc7621_params); i++) {
			if (asc7621_register_priorities[i] == PRI_LOW) {
				data->reg[i] =
				    i2c_smbus_read_byte_data(client, i) & 0xff;
			}
		}
		data->last_low_reading = jiffies;
	};			/* last_reading */

	data->valid = 1;

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Standard detection and initialization below
 *
 * Helper function that checks if an address is valid
 * for a particular chip.
 */

static inline int valid_address_for_chip(int chip_type, int address)
{
	int i;

	for (i = 0; asc7621_chips[chip_type].addresses[i] != I2C_CLIENT_END;
	     i++) {
		if (asc7621_chips[chip_type].addresses[i] == address)
			return 1;
	}
	return 0;
}

static void asc7621_init_client(struct i2c_client *client)
{
	int value;

	/* Warn if part was not "READY" */

	value = read_byte(client, 0x40);

	if (value & 0x02) {
		dev_err(&client->dev,
			"Client (%d,0x%02x) config is locked.\n",
			i2c_adapter_id(client->adapter), client->addr);
	};
	if (!(value & 0x04)) {
		dev_err(&client->dev, "Client (%d,0x%02x) is not ready.\n",
			i2c_adapter_id(client->adapter), client->addr);
	};

/*
 * Start monitoring
 *
 * Try to clear LOCK, Set START, save everything else
 */
	value = (value & ~0x02) | 0x01;
	write_byte(client, 0x40, value & 0xff);

}

static int
asc7621_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct asc7621_data *data;
	int i, err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	data = kzalloc(sizeof(struct asc7621_data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Initialize the asc7621 chip */
	asc7621_init_client(client);

	/* Create the sysfs entries */
	for (i = 0; i < ARRAY_SIZE(asc7621_params); i++) {
		err =
		    device_create_file(&client->dev,
				       &(asc7621_params[i].sda.dev_attr));
		if (err)
			goto exit_remove;
	}

	data->class_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	for (i = 0; i < ARRAY_SIZE(asc7621_params); i++) {
		device_remove_file(&client->dev,
				   &(asc7621_params[i].sda.dev_attr));
	}

	kfree(data);
	return err;
}

static int asc7621_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int company, verstep, chip_index;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	for (chip_index = FIRST_CHIP; chip_index <= LAST_CHIP; chip_index++) {

		if (!valid_address_for_chip(chip_index, client->addr))
			continue;

		company = read_byte(client,
			asc7621_chips[chip_index].company_reg);
		verstep = read_byte(client,
			asc7621_chips[chip_index].verstep_reg);

		if (company == asc7621_chips[chip_index].company_id &&
		    verstep == asc7621_chips[chip_index].verstep_id) {
			strlcpy(info->type, asc7621_chips[chip_index].name,
				I2C_NAME_SIZE);

			dev_info(&adapter->dev, "Matched %s at 0x%02x\n",
				 asc7621_chips[chip_index].name, client->addr);
			return 0;
		}
	}

	return -ENODEV;
}

static int asc7621_remove(struct i2c_client *client)
{
	struct asc7621_data *data = i2c_get_clientdata(client);
	int i;

	hwmon_device_unregister(data->class_dev);

	for (i = 0; i < ARRAY_SIZE(asc7621_params); i++) {
		device_remove_file(&client->dev,
				   &(asc7621_params[i].sda.dev_attr));
	}

	kfree(data);
	return 0;
}

static const struct i2c_device_id asc7621_id[] = {
	{"asc7621", asc7621},
	{"asc7621a", asc7621a},
	{},
};

MODULE_DEVICE_TABLE(i2c, asc7621_id);

static struct i2c_driver asc7621_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "asc7621",
	},
	.probe = asc7621_probe,
	.remove = asc7621_remove,
	.id_table = asc7621_id,
	.detect = asc7621_detect,
	.address_list = normal_i2c,
};

static int __init sm_asc7621_init(void)
{
	int i, j;
/*
 * Collect all the registers needed into a single array.
 * This way, if a register isn't actually used for anything,
 * we don't retrieve it.
 */

	for (i = 0; i < ARRAY_SIZE(asc7621_params); i++) {
		for (j = 0; j < ARRAY_SIZE(asc7621_params[i].msb); j++)
			asc7621_register_priorities[asc7621_params[i].msb[j]] =
			    asc7621_params[i].priority;
		for (j = 0; j < ARRAY_SIZE(asc7621_params[i].lsb); j++)
			asc7621_register_priorities[asc7621_params[i].lsb[j]] =
			    asc7621_params[i].priority;
	}
	return i2c_add_driver(&asc7621_driver);
}

static void __exit sm_asc7621_exit(void)
{
	i2c_del_driver(&asc7621_driver);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("George Joseph");
MODULE_DESCRIPTION("Andigilog aSC7621 and aSC7621a driver");

module_init(sm_asc7621_init);
module_exit(sm_asc7621_exit);
