/*
    it87.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring.

    Supports: IT8705F  Super I/O chip w/LPC interface
              IT8712F  Super I/O chip w/LPC interface & SMBus
              Sis950   A clone of the IT8705F

    Copyright (C) 2001 Chris Gauthron <chrisg@0-in.com> 
    Largely inspired by lm78.c of the same package

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
    djg@pdp8.net David Gesswein 7/18/01
    Modified to fix bug with not all alarms enabled.
    Added ability to read battery voltage and select temperature sensor
    type at module load time.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-isa.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <asm/io.h>


/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d,
					0x2e, 0x2f, I2C_CLIENT_END };
static unsigned short isa_address;

/* Insmod parameters */
I2C_CLIENT_INSMOD_2(it87, it8712);

#define	REG	0x2e	/* The register to read/write */
#define	DEV	0x07	/* Register: Logical device select */
#define	VAL	0x2f	/* The value to read/write */
#define PME	0x04	/* The device with the fan registers in it */
#define	DEVID	0x20	/* Register: Device ID */
#define	DEVREV	0x22	/* Register: Device Revision */

static inline int
superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

static int superio_inw(int reg)
{
	int val;
	outb(reg++, REG);
	val = inb(VAL) << 8;
	outb(reg, REG);
	val |= inb(VAL);
	return val;
}

static inline void
superio_select(void)
{
	outb(DEV, REG);
	outb(PME, VAL);
}

static inline void
superio_enter(void)
{
	outb(0x87, REG);
	outb(0x01, REG);
	outb(0x55, REG);
	outb(0x55, REG);
}

static inline void
superio_exit(void)
{
	outb(0x02, REG);
	outb(0x02, VAL);
}

#define IT8712F_DEVID 0x8712
#define IT8705F_DEVID 0x8705
#define IT87_ACT_REG  0x30
#define IT87_BASE_REG 0x60

/* Update battery voltage after every reading if true */
static int update_vbat;

/* Not all BIOSes properly configure the PWM registers */
static int fix_pwm_polarity;

/* Chip Type */

static u16 chip_type;

/* Many IT87 constants specified below */

/* Length of ISA address segment */
#define IT87_EXTENT 8

/* Where are the ISA address/data registers relative to the base address */
#define IT87_ADDR_REG_OFFSET 5
#define IT87_DATA_REG_OFFSET 6

/*----- The IT87 registers -----*/

#define IT87_REG_CONFIG        0x00

#define IT87_REG_ALARM1        0x01
#define IT87_REG_ALARM2        0x02
#define IT87_REG_ALARM3        0x03

#define IT87_REG_VID           0x0a
#define IT87_REG_FAN_DIV       0x0b

/* Monitors: 9 voltage (0 to 7, battery), 3 temp (1 to 3), 3 fan (1 to 3) */

#define IT87_REG_FAN(nr)       (0x0d + (nr))
#define IT87_REG_FAN_MIN(nr)   (0x10 + (nr))
#define IT87_REG_FAN_MAIN_CTRL 0x13
#define IT87_REG_FAN_CTL       0x14
#define IT87_REG_PWM(nr)       (0x15 + (nr))

#define IT87_REG_VIN(nr)       (0x20 + (nr))
#define IT87_REG_TEMP(nr)      (0x29 + (nr))

#define IT87_REG_VIN_MAX(nr)   (0x30 + (nr) * 2)
#define IT87_REG_VIN_MIN(nr)   (0x31 + (nr) * 2)
#define IT87_REG_TEMP_HIGH(nr) (0x40 + (nr) * 2)
#define IT87_REG_TEMP_LOW(nr)  (0x41 + (nr) * 2)

#define IT87_REG_I2C_ADDR      0x48

#define IT87_REG_VIN_ENABLE    0x50
#define IT87_REG_TEMP_ENABLE   0x51

#define IT87_REG_CHIPID        0x58

#define IN_TO_REG(val)  (SENSORS_LIMIT((((val) + 8)/16),0,255))
#define IN_FROM_REG(val) ((val) * 16)

static inline u8 FAN_TO_REG(long rpm, int div)
{
	if (rpm == 0)
		return 255;
	rpm = SENSORS_LIMIT(rpm, 1, 1000000);
	return SENSORS_LIMIT((1350000 + rpm * div / 2) / (rpm * div), 1,
			     254);
}

#define FAN_FROM_REG(val,div) ((val)==0?-1:(val)==255?0:1350000/((val)*(div)))

#define TEMP_TO_REG(val) (SENSORS_LIMIT(((val)<0?(((val)-500)/1000):\
					((val)+500)/1000),-128,127))
#define TEMP_FROM_REG(val) (((val)>0x80?(val)-0x100:(val))*1000)

#define PWM_TO_REG(val)   ((val) >> 1)
#define PWM_FROM_REG(val) (((val)&0x7f) << 1)

static int DIV_TO_REG(int val)
{
	int answer = 0;
	while ((val >>= 1) != 0)
		answer++;
	return answer;
}
#define DIV_FROM_REG(val) (1 << (val))


/* For each registered IT87, we need to keep some data in memory. That
   data is pointed to by it87_list[NR]->data. The structure itself is
   dynamically allocated, at the same time when a new it87 client is
   allocated. */
struct it87_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore lock;
	enum chips type;

	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[9];		/* Register value */
	u8 in_max[9];		/* Register value */
	u8 in_min[9];		/* Register value */
	u8 fan[3];		/* Register value */
	u8 fan_min[3];		/* Register value */
	u8 temp[3];		/* Register value */
	u8 temp_high[3];	/* Register value */
	u8 temp_low[3];		/* Register value */
	u8 sensor;		/* Register value */
	u8 fan_div[3];		/* Register encoding, shifted right */
	u8 vid;			/* Register encoding, combined */
	int vrm;
	u32 alarms;		/* Register encoding, combined */
	u8 fan_main_ctrl;	/* Register value */
	u8 manual_pwm_ctl[3];   /* manual PWM value set by user */
};


static int it87_attach_adapter(struct i2c_adapter *adapter);
static int it87_isa_attach_adapter(struct i2c_adapter *adapter);
static int it87_detect(struct i2c_adapter *adapter, int address, int kind);
static int it87_detach_client(struct i2c_client *client);

static int it87_read_value(struct i2c_client *client, u8 register);
static int it87_write_value(struct i2c_client *client, u8 register,
			u8 value);
static struct it87_data *it87_update_device(struct device *dev);
static int it87_check_pwm(struct i2c_client *client);
static void it87_init_client(struct i2c_client *client, struct it87_data *data);


static struct i2c_driver it87_driver = {
	.owner		= THIS_MODULE,
	.name		= "it87",
	.id		= I2C_DRIVERID_IT87,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= it87_attach_adapter,
	.detach_client	= it87_detach_client,
};

static struct i2c_driver it87_isa_driver = {
	.owner		= THIS_MODULE,
	.name		= "it87-isa",
	.attach_adapter	= it87_isa_attach_adapter,
	.detach_client	= it87_detach_client,
};


static ssize_t show_in(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in[nr]));
}

static ssize_t show_in_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_min[nr]));
}

static ssize_t show_in_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_max[nr]));
}

static ssize_t set_in_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	data->in_min[nr] = IN_TO_REG(val);
	it87_write_value(client, IT87_REG_VIN_MIN(nr), 
			data->in_min[nr]);
	up(&data->update_lock);
	return count;
}
static ssize_t set_in_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	data->in_max[nr] = IN_TO_REG(val);
	it87_write_value(client, IT87_REG_VIN_MAX(nr), 
			data->in_max[nr]);
	up(&data->update_lock);
	return count;
}

#define show_in_offset(offset)					\
static SENSOR_DEVICE_ATTR(in##offset##_input, S_IRUGO,		\
		show_in, NULL, offset);

#define limit_in_offset(offset)					\
static SENSOR_DEVICE_ATTR(in##offset##_min, S_IRUGO | S_IWUSR,	\
		show_in_min, set_in_min, offset);		\
static SENSOR_DEVICE_ATTR(in##offset##_max, S_IRUGO | S_IWUSR,	\
		show_in_max, set_in_max, offset);

show_in_offset(0);
limit_in_offset(0);
show_in_offset(1);
limit_in_offset(1);
show_in_offset(2);
limit_in_offset(2);
show_in_offset(3);
limit_in_offset(3);
show_in_offset(4);
limit_in_offset(4);
show_in_offset(5);
limit_in_offset(5);
show_in_offset(6);
limit_in_offset(6);
show_in_offset(7);
limit_in_offset(7);
show_in_offset(8);

/* 3 temperatures */
static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[nr]));
}
static ssize_t show_temp_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_high[nr]));
}
static ssize_t show_temp_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_low[nr]));
}
static ssize_t set_temp_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->temp_high[nr] = TEMP_TO_REG(val);
	it87_write_value(client, IT87_REG_TEMP_HIGH(nr), data->temp_high[nr]);
	up(&data->update_lock);
	return count;
}
static ssize_t set_temp_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->temp_low[nr] = TEMP_TO_REG(val);
	it87_write_value(client, IT87_REG_TEMP_LOW(nr), data->temp_low[nr]);
	up(&data->update_lock);
	return count;
}
#define show_temp_offset(offset)					\
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO,		\
		show_temp, NULL, offset - 1);				\
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IRUGO | S_IWUSR,	\
		show_temp_max, set_temp_max, offset - 1);		\
static SENSOR_DEVICE_ATTR(temp##offset##_min, S_IRUGO | S_IWUSR,	\
		show_temp_min, set_temp_min, offset - 1);

show_temp_offset(1);
show_temp_offset(2);
show_temp_offset(3);

static ssize_t show_sensor(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	u8 reg = data->sensor; /* In case the value is updated while we use it */
	
	if (reg & (1 << nr))
		return sprintf(buf, "3\n");  /* thermal diode */
	if (reg & (8 << nr))
		return sprintf(buf, "2\n");  /* thermistor */
	return sprintf(buf, "0\n");      /* disabled */
}
static ssize_t set_sensor(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);

	data->sensor &= ~(1 << nr);
	data->sensor &= ~(8 << nr);
	/* 3 = thermal diode; 2 = thermistor; 0 = disabled */
	if (val == 3)
	    data->sensor |= 1 << nr;
	else if (val == 2)
	    data->sensor |= 8 << nr;
	else if (val != 0) {
		up(&data->update_lock);
		return -EINVAL;
	}
	it87_write_value(client, IT87_REG_TEMP_ENABLE, data->sensor);
	up(&data->update_lock);
	return count;
}
#define show_sensor_offset(offset)					\
static SENSOR_DEVICE_ATTR(temp##offset##_type, S_IRUGO | S_IWUSR,	\
		show_sensor, set_sensor, offset - 1);

show_sensor_offset(1);
show_sensor_offset(2);
show_sensor_offset(3);

/* 3 Fans */
static ssize_t show_fan(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf,"%d\n", FAN_FROM_REG(data->fan[nr], 
				DIV_FROM_REG(data->fan_div[nr])));
}
static ssize_t show_fan_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf,"%d\n",
		FAN_FROM_REG(data->fan_min[nr], DIV_FROM_REG(data->fan_div[nr])));
}
static ssize_t show_fan_div(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[nr]));
}
static ssize_t show_pwm_enable(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf,"%d\n", (data->fan_main_ctrl & (1 << nr)) ? 1 : 0);
}
static ssize_t show_pwm(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf,"%d\n", data->manual_pwm_ctl[nr]);
}
static ssize_t set_fan_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);
	u8 reg = it87_read_value(client, IT87_REG_FAN_DIV);

	down(&data->update_lock);
	switch (nr) {
	case 0: data->fan_div[nr] = reg & 0x07; break;
	case 1: data->fan_div[nr] = (reg >> 3) & 0x07; break;
	case 2: data->fan_div[nr] = (reg & 0x40) ? 3 : 1; break;
	}

	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	it87_write_value(client, IT87_REG_FAN_MIN(nr), data->fan_min[nr]);
	up(&data->update_lock);
	return count;
}
static ssize_t set_fan_div(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);
	int i, min[3];
	u8 old;

	down(&data->update_lock);
	old = it87_read_value(client, IT87_REG_FAN_DIV);

	for (i = 0; i < 3; i++)
		min[i] = FAN_FROM_REG(data->fan_min[i], DIV_FROM_REG(data->fan_div[i]));

	switch (nr) {
	case 0:
	case 1:
		data->fan_div[nr] = DIV_TO_REG(val);
		break;
	case 2:
		if (val < 8)
			data->fan_div[nr] = 1;
		else
			data->fan_div[nr] = 3;
	}
	val = old & 0x80;
	val |= (data->fan_div[0] & 0x07);
	val |= (data->fan_div[1] & 0x07) << 3;
	if (data->fan_div[2] == 3)
		val |= 0x1 << 6;
	it87_write_value(client, IT87_REG_FAN_DIV, val);

	for (i = 0; i < 3; i++) {
		data->fan_min[i]=FAN_TO_REG(min[i], DIV_FROM_REG(data->fan_div[i]));
		it87_write_value(client, IT87_REG_FAN_MIN(i), data->fan_min[i]);
	}
	up(&data->update_lock);
	return count;
}
static ssize_t set_pwm_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);

	if (val == 0) {
		int tmp;
		/* make sure the fan is on when in on/off mode */
		tmp = it87_read_value(client, IT87_REG_FAN_CTL);
		it87_write_value(client, IT87_REG_FAN_CTL, tmp | (1 << nr));
		/* set on/off mode */
		data->fan_main_ctrl &= ~(1 << nr);
		it87_write_value(client, IT87_REG_FAN_MAIN_CTRL, data->fan_main_ctrl);
	} else if (val == 1) {
		/* set SmartGuardian mode */
		data->fan_main_ctrl |= (1 << nr);
		it87_write_value(client, IT87_REG_FAN_MAIN_CTRL, data->fan_main_ctrl);
		/* set saved pwm value, clear FAN_CTLX PWM mode bit */
		it87_write_value(client, IT87_REG_PWM(nr), PWM_TO_REG(data->manual_pwm_ctl[nr]));
	} else {
		up(&data->update_lock);
		return -EINVAL;
	}

	up(&data->update_lock);
	return count;
}
static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	if (val < 0 || val > 255)
		return -EINVAL;

	down(&data->update_lock);
	data->manual_pwm_ctl[nr] = val;
	if (data->fan_main_ctrl & (1 << nr))
		it87_write_value(client, IT87_REG_PWM(nr), PWM_TO_REG(data->manual_pwm_ctl[nr]));
	up(&data->update_lock);
	return count;
}

#define show_fan_offset(offset)					\
static SENSOR_DEVICE_ATTR(fan##offset##_input, S_IRUGO,		\
		show_fan, NULL, offset - 1);			\
static SENSOR_DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR, \
		show_fan_min, set_fan_min, offset - 1);		\
static SENSOR_DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR, \
		show_fan_div, set_fan_div, offset - 1);

show_fan_offset(1);
show_fan_offset(2);
show_fan_offset(3);

#define show_pwm_offset(offset)						\
static SENSOR_DEVICE_ATTR(pwm##offset##_enable, S_IRUGO | S_IWUSR,	\
		show_pwm_enable, set_pwm_enable, offset - 1);		\
static SENSOR_DEVICE_ATTR(pwm##offset, S_IRUGO | S_IWUSR,		\
		show_pwm, set_pwm, offset - 1);

show_pwm_offset(1);
show_pwm_offset(2);
show_pwm_offset(3);

/* Alarms */
static ssize_t show_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static ssize_t
show_vrm_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->vrm);
}
static ssize_t
store_vrm_reg(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);
	data->vrm = val;

	return count;
}
static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm_reg, store_vrm_reg);
#define device_create_file_vrm(client) \
device_create_file(&client->dev, &dev_attr_vrm)

static ssize_t
show_vid_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%ld\n", (long) vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid_reg, NULL);
#define device_create_file_vid(client) \
device_create_file(&client->dev, &dev_attr_cpu0_vid)

/* This function is called when:
     * it87_driver is inserted (when this module is loaded), for each
       available adapter
     * when a new adapter is inserted (and it87_driver is still present) */
static int it87_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, it87_detect);
}

static int it87_isa_attach_adapter(struct i2c_adapter *adapter)
{
	return it87_detect(adapter, isa_address, -1);
}

/* SuperIO detection - will change isa_address if a chip is found */
static int __init it87_find(unsigned short *address)
{
	int err = -ENODEV;

	superio_enter();
	chip_type = superio_inw(DEVID);
	if (chip_type != IT8712F_DEVID
	 && chip_type != IT8705F_DEVID)
	 	goto exit;

	superio_select();
	if (!(superio_inb(IT87_ACT_REG) & 0x01)) {
		pr_info("it87: Device not activated, skipping\n");
		goto exit;
	}

	*address = superio_inw(IT87_BASE_REG) & ~(IT87_EXTENT - 1);
	if (*address == 0) {
		pr_info("it87: Base address not set, skipping\n");
		goto exit;
	}

	err = 0;
	pr_info("it87: Found IT%04xF chip at 0x%x, revision %d\n",
		chip_type, *address, superio_inb(DEVREV) & 0x0f);

exit:
	superio_exit();
	return err;
}

/* This function is called by i2c_probe */
static int it87_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int i;
	struct i2c_client *new_client;
	struct it87_data *data;
	int err = 0;
	const char *name = "";
	int is_isa = i2c_is_isa_adapter(adapter);
	int enable_pwm_interface;

	if (!is_isa && 
	    !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto ERROR0;

	/* Reserve the ISA region */
	if (is_isa)
		if (!request_region(address, IT87_EXTENT, it87_isa_driver.name))
			goto ERROR0;

	/* For now, we presume we have a valid client. We create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access it87_{read,write}_value. */

	if (!(data = kzalloc(sizeof(struct it87_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR1;
	}

	new_client = &data->client;
	if (is_isa)
		init_MUTEX(&data->lock);
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = is_isa ? &it87_isa_driver : &it87_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. */

	if (kind < 0) {
		if ((it87_read_value(new_client, IT87_REG_CONFIG) & 0x80)
		  || (!is_isa
		   && it87_read_value(new_client, IT87_REG_I2C_ADDR) != address)) {
		   	err = -ENODEV;
			goto ERROR2;
		}
	}

	/* Determine the chip type. */
	if (kind <= 0) {
		i = it87_read_value(new_client, IT87_REG_CHIPID);
		if (i == 0x90) {
			kind = it87;
			if ((is_isa) && (chip_type == IT8712F_DEVID))
				kind = it8712;
		}
		else {
			if (kind == 0)
				dev_info(&adapter->dev, 
					"Ignoring 'force' parameter for unknown chip at "
					"adapter %d, address 0x%02x\n",
					i2c_adapter_id(adapter), address);
			err = -ENODEV;
			goto ERROR2;
		}
	}

	if (kind == it87) {
		name = "it87";
	} else if (kind == it8712) {
		name = "it8712";
	}

	/* Fill in the remaining client fields and put it into the global list */
	strlcpy(new_client->name, name, I2C_NAME_SIZE);
	data->type = kind;
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR2;

	/* Check PWM configuration */
	enable_pwm_interface = it87_check_pwm(new_client);

	/* Initialize the IT87 chip */
	it87_init_client(new_client, data);

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto ERROR3;
	}

	device_create_file(&new_client->dev, &sensor_dev_attr_in0_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in1_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in2_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in3_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in4_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in5_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in6_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in7_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in8_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in0_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in1_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in2_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in3_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in4_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in5_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in6_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in7_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in0_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in1_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in2_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in3_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in4_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in5_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in6_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_in7_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp1_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp2_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp3_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp1_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp2_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp3_max.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp1_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp2_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp3_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp1_type.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp2_type.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_temp3_type.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_fan1_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_fan2_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_fan3_input.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_fan1_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_fan2_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_fan3_min.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_fan1_div.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_fan2_div.dev_attr);
	device_create_file(&new_client->dev, &sensor_dev_attr_fan3_div.dev_attr);
	device_create_file(&new_client->dev, &dev_attr_alarms);
	if (enable_pwm_interface) {
		device_create_file(&new_client->dev, &sensor_dev_attr_pwm1_enable.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_pwm2_enable.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_pwm3_enable.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_pwm1.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_pwm2.dev_attr);
		device_create_file(&new_client->dev, &sensor_dev_attr_pwm3.dev_attr);
	}

	if (data->type == it8712) {
		data->vrm = vid_which_vrm();
		device_create_file_vrm(new_client);
		device_create_file_vid(new_client);
	}

	return 0;

ERROR3:
	i2c_detach_client(new_client);
ERROR2:
	kfree(data);
ERROR1:
	if (is_isa)
		release_region(address, IT87_EXTENT);
ERROR0:
	return err;
}

static int it87_detach_client(struct i2c_client *client)
{
	struct it87_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	if(i2c_is_isa_client(client))
		release_region(client->addr, IT87_EXTENT);
	kfree(data);

	return 0;
}

/* The SMBus locks itself, but ISA access must be locked explicitly! 
   We don't want to lock the whole ISA bus, so we lock each client
   separately.
   We ignore the IT87 BUSY flag at this moment - it could lead to deadlocks,
   would slow down the IT87 access and should not be necessary. */
static int it87_read_value(struct i2c_client *client, u8 reg)
{
	struct it87_data *data = i2c_get_clientdata(client);

	int res;
	if (i2c_is_isa_client(client)) {
		down(&data->lock);
		outb_p(reg, client->addr + IT87_ADDR_REG_OFFSET);
		res = inb_p(client->addr + IT87_DATA_REG_OFFSET);
		up(&data->lock);
		return res;
	} else
		return i2c_smbus_read_byte_data(client, reg);
}

/* The SMBus locks itself, but ISA access muse be locked explicitly! 
   We don't want to lock the whole ISA bus, so we lock each client
   separately.
   We ignore the IT87 BUSY flag at this moment - it could lead to deadlocks,
   would slow down the IT87 access and should not be necessary. */
static int it87_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	struct it87_data *data = i2c_get_clientdata(client);

	if (i2c_is_isa_client(client)) {
		down(&data->lock);
		outb_p(reg, client->addr + IT87_ADDR_REG_OFFSET);
		outb_p(value, client->addr + IT87_DATA_REG_OFFSET);
		up(&data->lock);
		return 0;
	} else
		return i2c_smbus_write_byte_data(client, reg, value);
}

/* Return 1 if and only if the PWM interface is safe to use */
static int it87_check_pwm(struct i2c_client *client)
{
	/* Some BIOSes fail to correctly configure the IT87 fans. All fans off
	 * and polarity set to active low is sign that this is the case so we
	 * disable pwm control to protect the user. */
	int tmp = it87_read_value(client, IT87_REG_FAN_CTL);
	if ((tmp & 0x87) == 0) {
		if (fix_pwm_polarity) {
			/* The user asks us to attempt a chip reconfiguration.
			 * This means switching to active high polarity and
			 * inverting all fan speed values. */
			int i;
			u8 pwm[3];

			for (i = 0; i < 3; i++)
				pwm[i] = it87_read_value(client,
							 IT87_REG_PWM(i));

			/* If any fan is in automatic pwm mode, the polarity
			 * might be correct, as suspicious as it seems, so we
			 * better don't change anything (but still disable the
			 * PWM interface). */
			if (!((pwm[0] | pwm[1] | pwm[2]) & 0x80)) {
				dev_info(&client->dev, "Reconfiguring PWM to "
					 "active high polarity\n");
				it87_write_value(client, IT87_REG_FAN_CTL,
						 tmp | 0x87);
				for (i = 0; i < 3; i++)
					it87_write_value(client,
							 IT87_REG_PWM(i),
							 0x7f & ~pwm[i]);
				return 1;
			}

			dev_info(&client->dev, "PWM configuration is "
				 "too broken to be fixed\n");
		}

		dev_info(&client->dev, "Detected broken BIOS "
			 "defaults, disabling PWM interface\n");
		return 0;
	} else if (fix_pwm_polarity) {
		dev_info(&client->dev, "PWM configuration looks "
			 "sane, won't touch\n");
	}

	return 1;
}

/* Called when we have found a new IT87. */
static void it87_init_client(struct i2c_client *client, struct it87_data *data)
{
	int tmp, i;

	/* initialize to sane defaults:
	 * - if the chip is in manual pwm mode, this will be overwritten with
	 *   the actual settings on the chip (so in this case, initialization
	 *   is not needed)
	 * - if in automatic or on/off mode, we could switch to manual mode,
	 *   read the registers and set manual_pwm_ctl accordingly, but currently
	 *   this is not implemented, so we initialize to something sane */
	for (i = 0; i < 3; i++) {
		data->manual_pwm_ctl[i] = 0xff;
	}

	/* Check if temperature channnels are reset manually or by some reason */
	tmp = it87_read_value(client, IT87_REG_TEMP_ENABLE);
	if ((tmp & 0x3f) == 0) {
		/* Temp1,Temp3=thermistor; Temp2=thermal diode */
		tmp = (tmp & 0xc0) | 0x2a;
		it87_write_value(client, IT87_REG_TEMP_ENABLE, tmp);
	}
	data->sensor = tmp;

	/* Check if voltage monitors are reset manually or by some reason */
	tmp = it87_read_value(client, IT87_REG_VIN_ENABLE);
	if ((tmp & 0xff) == 0) {
		/* Enable all voltage monitors */
		it87_write_value(client, IT87_REG_VIN_ENABLE, 0xff);
	}

	/* Check if tachometers are reset manually or by some reason */
	data->fan_main_ctrl = it87_read_value(client, IT87_REG_FAN_MAIN_CTRL);
	if ((data->fan_main_ctrl & 0x70) == 0) {
		/* Enable all fan tachometers */
		data->fan_main_ctrl |= 0x70;
		it87_write_value(client, IT87_REG_FAN_MAIN_CTRL, data->fan_main_ctrl);
	}

	/* Set current fan mode registers and the default settings for the
	 * other mode registers */
	for (i = 0; i < 3; i++) {
		if (data->fan_main_ctrl & (1 << i)) {
			/* pwm mode */
			tmp = it87_read_value(client, IT87_REG_PWM(i));
			if (tmp & 0x80) {
				/* automatic pwm - not yet implemented, but
				 * leave the settings made by the BIOS alone
				 * until a change is requested via the sysfs
				 * interface */
			} else {
				/* manual pwm */
				data->manual_pwm_ctl[i] = PWM_FROM_REG(tmp);
			}
		}
 	}

	/* Start monitoring */
	it87_write_value(client, IT87_REG_CONFIG,
			 (it87_read_value(client, IT87_REG_CONFIG) & 0x36)
			 | (update_vbat ? 0x41 : 0x01));
}

static struct it87_data *it87_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int i;

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {

		if (update_vbat) {
			/* Cleared after each update, so reenable.  Value
		 	  returned by this read will be previous value */	
			it87_write_value(client, IT87_REG_CONFIG,
			   it87_read_value(client, IT87_REG_CONFIG) | 0x40);
		}
		for (i = 0; i <= 7; i++) {
			data->in[i] =
			    it87_read_value(client, IT87_REG_VIN(i));
			data->in_min[i] =
			    it87_read_value(client, IT87_REG_VIN_MIN(i));
			data->in_max[i] =
			    it87_read_value(client, IT87_REG_VIN_MAX(i));
		}
		data->in[8] =
		    it87_read_value(client, IT87_REG_VIN(8));
		/* Temperature sensor doesn't have limit registers, set
		   to min and max value */
		data->in_min[8] = 0;
		data->in_max[8] = 255;

		for (i = 0; i < 3; i++) {
			data->fan[i] =
			    it87_read_value(client, IT87_REG_FAN(i));
			data->fan_min[i] =
			    it87_read_value(client, IT87_REG_FAN_MIN(i));
		}
		for (i = 0; i < 3; i++) {
			data->temp[i] =
			    it87_read_value(client, IT87_REG_TEMP(i));
			data->temp_high[i] =
			    it87_read_value(client, IT87_REG_TEMP_HIGH(i));
			data->temp_low[i] =
			    it87_read_value(client, IT87_REG_TEMP_LOW(i));
		}

		i = it87_read_value(client, IT87_REG_FAN_DIV);
		data->fan_div[0] = i & 0x07;
		data->fan_div[1] = (i >> 3) & 0x07;
		data->fan_div[2] = (i & 0x40) ? 3 : 1;

		data->alarms =
			it87_read_value(client, IT87_REG_ALARM1) |
			(it87_read_value(client, IT87_REG_ALARM2) << 8) |
			(it87_read_value(client, IT87_REG_ALARM3) << 16);
		data->fan_main_ctrl = it87_read_value(client, IT87_REG_FAN_MAIN_CTRL);

		data->sensor = it87_read_value(client, IT87_REG_TEMP_ENABLE);
		/* The 8705 does not have VID capability */
		if (data->type == it8712) {
			data->vid = it87_read_value(client, IT87_REG_VID);
			data->vid &= 0x1f;
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __init sm_it87_init(void)
{
	int res;

	res = i2c_add_driver(&it87_driver);
	if (res)
		return res;

	if (!it87_find(&isa_address)) {
		res = i2c_isa_add_driver(&it87_isa_driver);
		if (res) {
			i2c_del_driver(&it87_driver);
			return res;
		}
	}

	return 0;
}

static void __exit sm_it87_exit(void)
{
	i2c_isa_del_driver(&it87_isa_driver);
	i2c_del_driver(&it87_driver);
}


MODULE_AUTHOR("Chris Gauthron <chrisg@0-in.com>");
MODULE_DESCRIPTION("IT8705F, IT8712F, Sis950 driver");
module_param(update_vbat, bool, 0);
MODULE_PARM_DESC(update_vbat, "Update vbat if set else return powerup value");
module_param(fix_pwm_polarity, bool, 0);
MODULE_PARM_DESC(fix_pwm_polarity, "Force PWM polarity to active high (DANGEROUS)");
MODULE_LICENSE("GPL");

module_init(sm_it87_init);
module_exit(sm_it87_exit);
