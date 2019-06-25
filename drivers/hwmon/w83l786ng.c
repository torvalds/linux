// SPDX-License-Identifier: GPL-2.0-only
/*
 * w83l786ng.c - Linux kernel driver for hardware monitoring
 * Copyright (c) 2007 Kevin Lo <kevlo@kevlo.org>
 */

/*
 * Supports following chips:
 *
 * Chip		#vin	#fanin	#pwm	#temp	wchipid	vendid	i2c	ISA
 * w83l786ng	3	2	2	2	0x7b	0x5ca3	yes	no
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2e, 0x2f, I2C_CLIENT_END };

/* Insmod parameters */

static bool reset;
module_param(reset, bool, 0);
MODULE_PARM_DESC(reset, "Set to 1 to reset chip, not recommended");

#define W83L786NG_REG_IN_MIN(nr)	(0x2C + (nr) * 2)
#define W83L786NG_REG_IN_MAX(nr)	(0x2B + (nr) * 2)
#define W83L786NG_REG_IN(nr)		((nr) + 0x20)

#define W83L786NG_REG_FAN(nr)		((nr) + 0x28)
#define W83L786NG_REG_FAN_MIN(nr)	((nr) + 0x3B)

#define W83L786NG_REG_CONFIG		0x40
#define W83L786NG_REG_ALARM1		0x41
#define W83L786NG_REG_ALARM2		0x42
#define W83L786NG_REG_GPIO_EN		0x47
#define W83L786NG_REG_MAN_ID2		0x4C
#define W83L786NG_REG_MAN_ID1		0x4D
#define W83L786NG_REG_CHIP_ID		0x4E

#define W83L786NG_REG_DIODE		0x53
#define W83L786NG_REG_FAN_DIV		0x54
#define W83L786NG_REG_FAN_CFG		0x80

#define W83L786NG_REG_TOLERANCE		0x8D

static const u8 W83L786NG_REG_TEMP[2][3] = {
	{ 0x25,		/* TEMP 0 in DataSheet */
	  0x35,		/* TEMP 0 Over in DataSheet */
	  0x36 },	/* TEMP 0 Hyst in DataSheet */
	{ 0x26,		/* TEMP 1 in DataSheet */
	  0x37,		/* TEMP 1 Over in DataSheet */
	  0x38 }	/* TEMP 1 Hyst in DataSheet */
};

static const u8 W83L786NG_PWM_MODE_SHIFT[] = {6, 7};
static const u8 W83L786NG_PWM_ENABLE_SHIFT[] = {2, 4};

/* FAN Duty Cycle, be used to control */
static const u8 W83L786NG_REG_PWM[] = {0x81, 0x87};


static inline u8
FAN_TO_REG(long rpm, int div)
{
	if (rpm == 0)
		return 255;
	rpm = clamp_val(rpm, 1, 1000000);
	return clamp_val((1350000 + rpm * div / 2) / (rpm * div), 1, 254);
}

#define FAN_FROM_REG(val, div)	((val) == 0   ? -1 : \
				((val) == 255 ? 0 : \
				1350000 / ((val) * (div))))

/* for temp */
#define TEMP_TO_REG(val)	(clamp_val(((val) < 0 ? (val) + 0x100 * 1000 \
						      : (val)) / 1000, 0, 0xff))
#define TEMP_FROM_REG(val)	(((val) & 0x80 ? \
				  (val) - 0x100 : (val)) * 1000)

/*
 * The analog voltage inputs have 8mV LSB. Since the sysfs output is
 * in mV as would be measured on the chip input pin, need to just
 * multiply/divide by 8 to translate from/to register values.
 */
#define IN_TO_REG(val)		(clamp_val((((val) + 4) / 8), 0, 255))
#define IN_FROM_REG(val)	((val) * 8)

#define DIV_FROM_REG(val)	(1 << (val))

static inline u8
DIV_TO_REG(long val)
{
	int i;
	val = clamp_val(val, 1, 128) >> 1;
	for (i = 0; i < 7; i++) {
		if (val == 0)
			break;
		val >>= 1;
	}
	return (u8)i;
}

struct w83l786ng_data {
	struct i2c_client *client;
	struct mutex update_lock;
	char valid;			/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */
	unsigned long last_nonvolatile;	/* In jiffies, last time we update the
					 * nonvolatile registers */

	u8 in[3];
	u8 in_max[3];
	u8 in_min[3];
	u8 fan[2];
	u8 fan_div[2];
	u8 fan_min[2];
	u8 temp_type[2];
	u8 temp[2][3];
	u8 pwm[2];
	u8 pwm_mode[2];	/* 0->DC variable voltage
			 * 1->PWM variable duty cycle */

	u8 pwm_enable[2]; /* 1->manual
			   * 2->thermal cruise (also called SmartFan I) */
	u8 tolerance[2];
};

static u8
w83l786ng_read_value(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int
w83l786ng_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static struct w83l786ng_data *w83l786ng_update_device(struct device *dev)
{
	struct w83l786ng_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i, j;
	u8 reg_tmp, pwmcfg;

	mutex_lock(&data->update_lock);
	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		dev_dbg(&client->dev, "Updating w83l786ng data.\n");

		/* Update the voltages measured value and limits */
		for (i = 0; i < 3; i++) {
			data->in[i] = w83l786ng_read_value(client,
			    W83L786NG_REG_IN(i));
			data->in_min[i] = w83l786ng_read_value(client,
			    W83L786NG_REG_IN_MIN(i));
			data->in_max[i] = w83l786ng_read_value(client,
			    W83L786NG_REG_IN_MAX(i));
		}

		/* Update the fan counts and limits */
		for (i = 0; i < 2; i++) {
			data->fan[i] = w83l786ng_read_value(client,
			    W83L786NG_REG_FAN(i));
			data->fan_min[i] = w83l786ng_read_value(client,
			    W83L786NG_REG_FAN_MIN(i));
		}

		/* Update the fan divisor */
		reg_tmp = w83l786ng_read_value(client, W83L786NG_REG_FAN_DIV);
		data->fan_div[0] = reg_tmp & 0x07;
		data->fan_div[1] = (reg_tmp >> 4) & 0x07;

		pwmcfg = w83l786ng_read_value(client, W83L786NG_REG_FAN_CFG);
		for (i = 0; i < 2; i++) {
			data->pwm_mode[i] =
			    ((pwmcfg >> W83L786NG_PWM_MODE_SHIFT[i]) & 1)
			    ? 0 : 1;
			data->pwm_enable[i] =
			    ((pwmcfg >> W83L786NG_PWM_ENABLE_SHIFT[i]) & 3) + 1;
			data->pwm[i] =
			    (w83l786ng_read_value(client, W83L786NG_REG_PWM[i])
			     & 0x0f) * 0x11;
		}


		/* Update the temperature sensors */
		for (i = 0; i < 2; i++) {
			for (j = 0; j < 3; j++) {
				data->temp[i][j] = w83l786ng_read_value(client,
				    W83L786NG_REG_TEMP[i][j]);
			}
		}

		/* Update Smart Fan I/II tolerance */
		reg_tmp = w83l786ng_read_value(client, W83L786NG_REG_TOLERANCE);
		data->tolerance[0] = reg_tmp & 0x0f;
		data->tolerance[1] = (reg_tmp >> 4) & 0x0f;

		data->last_updated = jiffies;
		data->valid = 1;

	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* following are the sysfs callback functions */
#define show_in_reg(reg) \
static ssize_t \
show_##reg(struct device *dev, struct device_attribute *attr, \
	   char *buf) \
{ \
	int nr = to_sensor_dev_attr(attr)->index; \
	struct w83l786ng_data *data = w83l786ng_update_device(dev); \
	return sprintf(buf, "%d\n", IN_FROM_REG(data->reg[nr])); \
}

show_in_reg(in)
show_in_reg(in_min)
show_in_reg(in_max)

#define store_in_reg(REG, reg) \
static ssize_t \
store_in_##reg(struct device *dev, struct device_attribute *attr, \
	       const char *buf, size_t count) \
{ \
	int nr = to_sensor_dev_attr(attr)->index; \
	struct w83l786ng_data *data = dev_get_drvdata(dev); \
	struct i2c_client *client = data->client; \
	unsigned long val; \
	int err = kstrtoul(buf, 10, &val); \
	if (err) \
		return err; \
	mutex_lock(&data->update_lock); \
	data->in_##reg[nr] = IN_TO_REG(val); \
	w83l786ng_write_value(client, W83L786NG_REG_IN_##REG(nr), \
			      data->in_##reg[nr]); \
	mutex_unlock(&data->update_lock); \
	return count; \
}

store_in_reg(MIN, min)
store_in_reg(MAX, max)

static struct sensor_device_attribute sda_in_input[] = {
	SENSOR_ATTR(in0_input, S_IRUGO, show_in, NULL, 0),
	SENSOR_ATTR(in1_input, S_IRUGO, show_in, NULL, 1),
	SENSOR_ATTR(in2_input, S_IRUGO, show_in, NULL, 2),
};

static struct sensor_device_attribute sda_in_min[] = {
	SENSOR_ATTR(in0_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 0),
	SENSOR_ATTR(in1_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 1),
	SENSOR_ATTR(in2_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 2),
};

static struct sensor_device_attribute sda_in_max[] = {
	SENSOR_ATTR(in0_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 0),
	SENSOR_ATTR(in1_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 1),
	SENSOR_ATTR(in2_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 2),
};

#define show_fan_reg(reg) \
static ssize_t show_##reg(struct device *dev, struct device_attribute *attr, \
			  char *buf) \
{ \
	int nr = to_sensor_dev_attr(attr)->index; \
	struct w83l786ng_data *data = w83l786ng_update_device(dev); \
	return sprintf(buf, "%d\n", \
		FAN_FROM_REG(data->reg[nr], DIV_FROM_REG(data->fan_div[nr]))); \
}

show_fan_reg(fan);
show_fan_reg(fan_min);

static ssize_t
store_fan_min(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct w83l786ng_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	w83l786ng_write_value(client, W83L786NG_REG_FAN_MIN(nr),
			      data->fan_min[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_fan_div(struct device *dev, struct device_attribute *attr,
	     char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct w83l786ng_data *data = w83l786ng_update_device(dev);
	return sprintf(buf, "%u\n", DIV_FROM_REG(data->fan_div[nr]));
}

/*
 * Note: we save and restore the fan minimum here, because its value is
 * determined in part by the fan divisor.  This follows the principle of
 * least surprise; the user doesn't expect the fan minimum to change just
 * because the divisor changed.
 */
static ssize_t
store_fan_div(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct w83l786ng_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	unsigned long min;
	u8 tmp_fan_div;
	u8 fan_div_reg;
	u8 keep_mask = 0;
	u8 new_shift = 0;

	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	/* Save fan_min */
	mutex_lock(&data->update_lock);
	min = FAN_FROM_REG(data->fan_min[nr], DIV_FROM_REG(data->fan_div[nr]));

	data->fan_div[nr] = DIV_TO_REG(val);

	switch (nr) {
	case 0:
		keep_mask = 0xf8;
		new_shift = 0;
		break;
	case 1:
		keep_mask = 0x8f;
		new_shift = 4;
		break;
	}

	fan_div_reg = w83l786ng_read_value(client, W83L786NG_REG_FAN_DIV)
					   & keep_mask;

	tmp_fan_div = (data->fan_div[nr] << new_shift) & ~keep_mask;

	w83l786ng_write_value(client, W83L786NG_REG_FAN_DIV,
			      fan_div_reg | tmp_fan_div);

	/* Restore fan_min */
	data->fan_min[nr] = FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	w83l786ng_write_value(client, W83L786NG_REG_FAN_MIN(nr),
			      data->fan_min[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static struct sensor_device_attribute sda_fan_input[] = {
	SENSOR_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0),
	SENSOR_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1),
};

static struct sensor_device_attribute sda_fan_min[] = {
	SENSOR_ATTR(fan1_min, S_IWUSR | S_IRUGO, show_fan_min,
		    store_fan_min, 0),
	SENSOR_ATTR(fan2_min, S_IWUSR | S_IRUGO, show_fan_min,
		    store_fan_min, 1),
};

static struct sensor_device_attribute sda_fan_div[] = {
	SENSOR_ATTR(fan1_div, S_IWUSR | S_IRUGO, show_fan_div,
		    store_fan_div, 0),
	SENSOR_ATTR(fan2_div, S_IWUSR | S_IRUGO, show_fan_div,
		    store_fan_div, 1),
};


/* read/write the temperature, includes measured value and limits */

static ssize_t
show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83l786ng_data *data = w83l786ng_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[nr][index]));
}

static ssize_t
store_temp(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83l786ng_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp[nr][index] = TEMP_TO_REG(val);
	w83l786ng_write_value(client, W83L786NG_REG_TEMP[nr][index],
			      data->temp[nr][index]);
	mutex_unlock(&data->update_lock);

	return count;
}

static struct sensor_device_attribute_2 sda_temp_input[] = {
	SENSOR_ATTR_2(temp1_input, S_IRUGO, show_temp, NULL, 0, 0),
	SENSOR_ATTR_2(temp2_input, S_IRUGO, show_temp, NULL, 1, 0),
};

static struct sensor_device_attribute_2 sda_temp_max[] = {
	SENSOR_ATTR_2(temp1_max, S_IRUGO | S_IWUSR,
		      show_temp, store_temp, 0, 1),
	SENSOR_ATTR_2(temp2_max, S_IRUGO | S_IWUSR,
		      show_temp, store_temp, 1, 1),
};

static struct sensor_device_attribute_2 sda_temp_max_hyst[] = {
	SENSOR_ATTR_2(temp1_max_hyst, S_IRUGO | S_IWUSR,
		      show_temp, store_temp, 0, 2),
	SENSOR_ATTR_2(temp2_max_hyst, S_IRUGO | S_IWUSR,
		      show_temp, store_temp, 1, 2),
};

#define show_pwm_reg(reg) \
static ssize_t show_##reg(struct device *dev, struct device_attribute *attr, \
			  char *buf) \
{ \
	struct w83l786ng_data *data = w83l786ng_update_device(dev); \
	int nr = to_sensor_dev_attr(attr)->index; \
	return sprintf(buf, "%d\n", data->reg[nr]); \
}

show_pwm_reg(pwm_mode)
show_pwm_reg(pwm_enable)
show_pwm_reg(pwm)

static ssize_t
store_pwm_mode(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct w83l786ng_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 reg;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val > 1)
		return -EINVAL;
	mutex_lock(&data->update_lock);
	data->pwm_mode[nr] = val;
	reg = w83l786ng_read_value(client, W83L786NG_REG_FAN_CFG);
	reg &= ~(1 << W83L786NG_PWM_MODE_SHIFT[nr]);
	if (!val)
		reg |= 1 << W83L786NG_PWM_MODE_SHIFT[nr];
	w83l786ng_write_value(client, W83L786NG_REG_FAN_CFG, reg);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
store_pwm(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct w83l786ng_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;
	val = clamp_val(val, 0, 255);
	val = DIV_ROUND_CLOSEST(val, 0x11);

	mutex_lock(&data->update_lock);
	data->pwm[nr] = val * 0x11;
	val |= w83l786ng_read_value(client, W83L786NG_REG_PWM[nr]) & 0xf0;
	w83l786ng_write_value(client, W83L786NG_REG_PWM[nr], val);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
store_pwm_enable(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct w83l786ng_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 reg;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (!val || val > 2)  /* only modes 1 and 2 are supported */
		return -EINVAL;

	mutex_lock(&data->update_lock);
	reg = w83l786ng_read_value(client, W83L786NG_REG_FAN_CFG);
	data->pwm_enable[nr] = val;
	reg &= ~(0x03 << W83L786NG_PWM_ENABLE_SHIFT[nr]);
	reg |= (val - 1) << W83L786NG_PWM_ENABLE_SHIFT[nr];
	w83l786ng_write_value(client, W83L786NG_REG_FAN_CFG, reg);
	mutex_unlock(&data->update_lock);
	return count;
}

static struct sensor_device_attribute sda_pwm[] = {
	SENSOR_ATTR(pwm1, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 0),
	SENSOR_ATTR(pwm2, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 1),
};

static struct sensor_device_attribute sda_pwm_mode[] = {
	SENSOR_ATTR(pwm1_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
		    store_pwm_mode, 0),
	SENSOR_ATTR(pwm2_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
		    store_pwm_mode, 1),
};

static struct sensor_device_attribute sda_pwm_enable[] = {
	SENSOR_ATTR(pwm1_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
		    store_pwm_enable, 0),
	SENSOR_ATTR(pwm2_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
		    store_pwm_enable, 1),
};

/* For Smart Fan I/Thermal Cruise and Smart Fan II */
static ssize_t
show_tolerance(struct device *dev, struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct w83l786ng_data *data = w83l786ng_update_device(dev);
	return sprintf(buf, "%ld\n", (long)data->tolerance[nr]);
}

static ssize_t
store_tolerance(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct w83l786ng_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 tol_tmp, tol_mask;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	tol_mask = w83l786ng_read_value(client,
	    W83L786NG_REG_TOLERANCE) & ((nr == 1) ? 0x0f : 0xf0);
	tol_tmp = clamp_val(val, 0, 15);
	tol_tmp &= 0x0f;
	data->tolerance[nr] = tol_tmp;
	if (nr == 1)
		tol_tmp <<= 4;

	w83l786ng_write_value(client, W83L786NG_REG_TOLERANCE,
			      tol_mask | tol_tmp);
	mutex_unlock(&data->update_lock);
	return count;
}

static struct sensor_device_attribute sda_tolerance[] = {
	SENSOR_ATTR(pwm1_tolerance, S_IWUSR | S_IRUGO,
		    show_tolerance, store_tolerance, 0),
	SENSOR_ATTR(pwm2_tolerance, S_IWUSR | S_IRUGO,
		    show_tolerance, store_tolerance, 1),
};


#define IN_UNIT_ATTRS(X)	\
	&sda_in_input[X].dev_attr.attr,		\
	&sda_in_min[X].dev_attr.attr,		\
	&sda_in_max[X].dev_attr.attr

#define FAN_UNIT_ATTRS(X)	\
	&sda_fan_input[X].dev_attr.attr,	\
	&sda_fan_min[X].dev_attr.attr,		\
	&sda_fan_div[X].dev_attr.attr

#define TEMP_UNIT_ATTRS(X)	\
	&sda_temp_input[X].dev_attr.attr,	\
	&sda_temp_max[X].dev_attr.attr,		\
	&sda_temp_max_hyst[X].dev_attr.attr

#define PWM_UNIT_ATTRS(X)	\
	&sda_pwm[X].dev_attr.attr,		\
	&sda_pwm_mode[X].dev_attr.attr,		\
	&sda_pwm_enable[X].dev_attr.attr

#define TOLERANCE_UNIT_ATTRS(X)	\
	&sda_tolerance[X].dev_attr.attr

static struct attribute *w83l786ng_attrs[] = {
	IN_UNIT_ATTRS(0),
	IN_UNIT_ATTRS(1),
	IN_UNIT_ATTRS(2),
	FAN_UNIT_ATTRS(0),
	FAN_UNIT_ATTRS(1),
	TEMP_UNIT_ATTRS(0),
	TEMP_UNIT_ATTRS(1),
	PWM_UNIT_ATTRS(0),
	PWM_UNIT_ATTRS(1),
	TOLERANCE_UNIT_ATTRS(0),
	TOLERANCE_UNIT_ATTRS(1),
	NULL
};

ATTRIBUTE_GROUPS(w83l786ng);

static int
w83l786ng_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	u16 man_id;
	u8 chip_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Detection */
	if ((w83l786ng_read_value(client, W83L786NG_REG_CONFIG) & 0x80)) {
		dev_dbg(&adapter->dev, "W83L786NG detection failed at 0x%02x\n",
			client->addr);
		return -ENODEV;
	}

	/* Identification */
	man_id = (w83l786ng_read_value(client, W83L786NG_REG_MAN_ID1) << 8) +
		 w83l786ng_read_value(client, W83L786NG_REG_MAN_ID2);
	chip_id = w83l786ng_read_value(client, W83L786NG_REG_CHIP_ID);

	if (man_id != 0x5CA3 ||		/* Winbond */
	    chip_id != 0x80) {		/* W83L786NG */
		dev_dbg(&adapter->dev,
			"Unsupported chip (man_id=0x%04X, chip_id=0x%02X)\n",
			man_id, chip_id);
		return -ENODEV;
	}

	strlcpy(info->type, "w83l786ng", I2C_NAME_SIZE);

	return 0;
}

static void w83l786ng_init_client(struct i2c_client *client)
{
	u8 tmp;

	if (reset)
		w83l786ng_write_value(client, W83L786NG_REG_CONFIG, 0x80);

	/* Start monitoring */
	tmp = w83l786ng_read_value(client, W83L786NG_REG_CONFIG);
	if (!(tmp & 0x01))
		w83l786ng_write_value(client, W83L786NG_REG_CONFIG, tmp | 0x01);
}

static int
w83l786ng_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct w83l786ng_data *data;
	struct device *hwmon_dev;
	int i;
	u8 reg_tmp;

	data = devm_kzalloc(dev, sizeof(struct w83l786ng_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	/* Initialize the chip */
	w83l786ng_init_client(client);

	/* A few vars need to be filled upon startup */
	for (i = 0; i < 2; i++) {
		data->fan_min[i] = w83l786ng_read_value(client,
		    W83L786NG_REG_FAN_MIN(i));
	}

	/* Update the fan divisor */
	reg_tmp = w83l786ng_read_value(client, W83L786NG_REG_FAN_DIV);
	data->fan_div[0] = reg_tmp & 0x07;
	data->fan_div[1] = (reg_tmp >> 4) & 0x07;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   w83l786ng_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id w83l786ng_id[] = {
	{ "w83l786ng", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, w83l786ng_id);

static struct i2c_driver w83l786ng_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		   .name = "w83l786ng",
	},
	.probe		= w83l786ng_probe,
	.id_table	= w83l786ng_id,
	.detect		= w83l786ng_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(w83l786ng_driver);

MODULE_AUTHOR("Kevin Lo");
MODULE_DESCRIPTION("w83l786ng driver");
MODULE_LICENSE("GPL");
