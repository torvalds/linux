/*
    it87.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring.

    Supports: IT8705F  Super I/O chip w/LPC interface
              IT8712F  Super I/O chip w/LPC interface
              IT8716F  Super I/O chip w/LPC interface
              IT8718F  Super I/O chip w/LPC interface
              Sis950   A clone of the IT8705F

    Copyright (C) 2001 Chris Gauthron <chrisg@0-in.com> 
    Copyright (C) 2005-2006 Jean Delvare <khali@linux-fr.org>

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
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <asm/io.h>


static unsigned short isa_address;
enum chips { it87, it8712, it8716, it8718 };

#define	REG	0x2e	/* The register to read/write */
#define	DEV	0x07	/* Register: Logical device select */
#define	VAL	0x2f	/* The value to read/write */
#define PME	0x04	/* The device with the fan registers in it */
#define GPIO	0x07	/* The device with the IT8718F VID value in it */
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
superio_select(int ldn)
{
	outb(DEV, REG);
	outb(ldn, VAL);
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

/* Logical device 4 registers */
#define IT8712F_DEVID 0x8712
#define IT8705F_DEVID 0x8705
#define IT8716F_DEVID 0x8716
#define IT8718F_DEVID 0x8718
#define IT87_ACT_REG  0x30
#define IT87_BASE_REG 0x60

/* Logical device 7 registers (IT8712F and later) */
#define IT87_SIO_PINX2_REG	0x2c	/* Pin selection */
#define IT87_SIO_VID_REG	0xfc	/* VID value */

/* Update battery voltage after every reading if true */
static int update_vbat;

/* Not all BIOSes properly configure the PWM registers */
static int fix_pwm_polarity;

/* Values read from Super-I/O config space */
static u16 chip_type;
static u8 vid_value;

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

/* The IT8718F has the VID value in a different register, in Super-I/O
   configuration space. */
#define IT87_REG_VID           0x0a
/* Warning: register 0x0b is used for something completely different in
   new chips/revisions. I suspect only 16-bit tachometer mode will work
   for these. */
#define IT87_REG_FAN_DIV       0x0b
#define IT87_REG_FAN_16BIT     0x0c

/* Monitors: 9 voltage (0 to 7, battery), 3 temp (1 to 3), 3 fan (1 to 3) */

#define IT87_REG_FAN(nr)       (0x0d + (nr))
#define IT87_REG_FAN_MIN(nr)   (0x10 + (nr))
#define IT87_REG_FANX(nr)      (0x18 + (nr))
#define IT87_REG_FANX_MIN(nr)  (0x1b + (nr))
#define IT87_REG_FAN_MAIN_CTRL 0x13
#define IT87_REG_FAN_CTL       0x14
#define IT87_REG_PWM(nr)       (0x15 + (nr))

#define IT87_REG_VIN(nr)       (0x20 + (nr))
#define IT87_REG_TEMP(nr)      (0x29 + (nr))

#define IT87_REG_VIN_MAX(nr)   (0x30 + (nr) * 2)
#define IT87_REG_VIN_MIN(nr)   (0x31 + (nr) * 2)
#define IT87_REG_TEMP_HIGH(nr) (0x40 + (nr) * 2)
#define IT87_REG_TEMP_LOW(nr)  (0x41 + (nr) * 2)

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

static inline u16 FAN16_TO_REG(long rpm)
{
	if (rpm == 0)
		return 0xffff;
	return SENSORS_LIMIT((1350000 + rpm) / (rpm * 2), 1, 0xfffe);
}

#define FAN_FROM_REG(val,div) ((val)==0?-1:(val)==255?0:1350000/((val)*(div)))
/* The divider is fixed to 2 in 16-bit mode */
#define FAN16_FROM_REG(val) ((val)==0?-1:(val)==0xffff?0:1350000/((val)*2))

#define TEMP_TO_REG(val) (SENSORS_LIMIT(((val)<0?(((val)-500)/1000):\
					((val)+500)/1000),-128,127))
#define TEMP_FROM_REG(val) (((val)>0x80?(val)-0x100:(val))*1000)

#define PWM_TO_REG(val)   ((val) >> 1)
#define PWM_FROM_REG(val) (((val)&0x7f) << 1)

static int DIV_TO_REG(int val)
{
	int answer = 0;
	while (answer < 7 && (val >>= 1))
		answer++;
	return answer;
}
#define DIV_FROM_REG(val) (1 << (val))

static const unsigned int pwm_freq[8] = {
	48000000 / 128,
	24000000 / 128,
	12000000 / 128,
	8000000 / 128,
	6000000 / 128,
	3000000 / 128,
	1500000 / 128,
	750000 / 128,
};


/* For each registered chip, we need to keep some data in memory.
   The structure is dynamically allocated. */
struct it87_data {
	struct i2c_client client;
	struct class_device *class_dev;
	enum chips type;

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[9];		/* Register value */
	u8 in_max[8];		/* Register value */
	u8 in_min[8];		/* Register value */
	u8 has_fan;		/* Bitfield, fans enabled */
	u16 fan[3];		/* Register values, possibly combined */
	u16 fan_min[3];		/* Register values, possibly combined */
	u8 temp[3];		/* Register value */
	u8 temp_high[3];	/* Register value */
	u8 temp_low[3];		/* Register value */
	u8 sensor;		/* Register value */
	u8 fan_div[3];		/* Register encoding, shifted right */
	u8 vid;			/* Register encoding, combined */
	u8 vrm;
	u32 alarms;		/* Register encoding, combined */
	u8 fan_main_ctrl;	/* Register value */
	u8 fan_ctl;		/* Register value */
	u8 manual_pwm_ctl[3];   /* manual PWM value set by user */
};


static int it87_detect(struct i2c_adapter *adapter);
static int it87_detach_client(struct i2c_client *client);

static int it87_read_value(struct i2c_client *client, u8 reg);
static void it87_write_value(struct i2c_client *client, u8 reg, u8 value);
static struct it87_data *it87_update_device(struct device *dev);
static int it87_check_pwm(struct i2c_client *client);
static void it87_init_client(struct i2c_client *client, struct it87_data *data);


static struct i2c_driver it87_isa_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "it87-isa",
	},
	.attach_adapter	= it87_detect,
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

	mutex_lock(&data->update_lock);
	data->in_min[nr] = IN_TO_REG(val);
	it87_write_value(client, IT87_REG_VIN_MIN(nr), 
			data->in_min[nr]);
	mutex_unlock(&data->update_lock);
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

	mutex_lock(&data->update_lock);
	data->in_max[nr] = IN_TO_REG(val);
	it87_write_value(client, IT87_REG_VIN_MAX(nr), 
			data->in_max[nr]);
	mutex_unlock(&data->update_lock);
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

	mutex_lock(&data->update_lock);
	data->temp_high[nr] = TEMP_TO_REG(val);
	it87_write_value(client, IT87_REG_TEMP_HIGH(nr), data->temp_high[nr]);
	mutex_unlock(&data->update_lock);
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

	mutex_lock(&data->update_lock);
	data->temp_low[nr] = TEMP_TO_REG(val);
	it87_write_value(client, IT87_REG_TEMP_LOW(nr), data->temp_low[nr]);
	mutex_unlock(&data->update_lock);
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

	mutex_lock(&data->update_lock);

	data->sensor &= ~(1 << nr);
	data->sensor &= ~(8 << nr);
	/* 3 = thermal diode; 2 = thermistor; 0 = disabled */
	if (val == 3)
	    data->sensor |= 1 << nr;
	else if (val == 2)
	    data->sensor |= 8 << nr;
	else if (val != 0) {
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}
	it87_write_value(client, IT87_REG_TEMP_ENABLE, data->sensor);
	mutex_unlock(&data->update_lock);
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
static ssize_t show_pwm_freq(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct it87_data *data = it87_update_device(dev);
	int index = (data->fan_ctl >> 4) & 0x07;

	return sprintf(buf, "%u\n", pwm_freq[index]);
}
static ssize_t set_fan_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);
	u8 reg;

	mutex_lock(&data->update_lock);
	reg = it87_read_value(client, IT87_REG_FAN_DIV);
	switch (nr) {
	case 0: data->fan_div[nr] = reg & 0x07; break;
	case 1: data->fan_div[nr] = (reg >> 3) & 0x07; break;
	case 2: data->fan_div[nr] = (reg & 0x40) ? 3 : 1; break;
	}

	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	it87_write_value(client, IT87_REG_FAN_MIN(nr), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_fan_div(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);
	int min;
	u8 old;

	mutex_lock(&data->update_lock);
	old = it87_read_value(client, IT87_REG_FAN_DIV);

	/* Save fan min limit */
	min = FAN_FROM_REG(data->fan_min[nr], DIV_FROM_REG(data->fan_div[nr]));

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

	/* Restore fan min limit */
	data->fan_min[nr] = FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	it87_write_value(client, IT87_REG_FAN_MIN(nr), data->fan_min[nr]);

	mutex_unlock(&data->update_lock);
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

	mutex_lock(&data->update_lock);

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
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	mutex_unlock(&data->update_lock);
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

	mutex_lock(&data->update_lock);
	data->manual_pwm_ctl[nr] = val;
	if (data->fan_main_ctrl & (1 << nr))
		it87_write_value(client, IT87_REG_PWM(nr), PWM_TO_REG(data->manual_pwm_ctl[nr]));
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_pwm_freq(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);
	int i;

	/* Search for the nearest available frequency */
	for (i = 0; i < 7; i++) {
		if (val > (pwm_freq[i] + pwm_freq[i+1]) / 2)
			break;
	}

	mutex_lock(&data->update_lock);
	data->fan_ctl = it87_read_value(client, IT87_REG_FAN_CTL) & 0x8f;
	data->fan_ctl |= i << 4;
	it87_write_value(client, IT87_REG_FAN_CTL, data->fan_ctl);
	mutex_unlock(&data->update_lock);

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
		show_pwm, set_pwm, offset - 1);				\
static DEVICE_ATTR(pwm##offset##_freq,					\
		(offset == 1 ? S_IRUGO | S_IWUSR : S_IRUGO),		\
		show_pwm_freq, (offset == 1 ? set_pwm_freq : NULL));

show_pwm_offset(1);
show_pwm_offset(2);
show_pwm_offset(3);

/* A different set of callbacks for 16-bit fans */
static ssize_t show_fan16(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", FAN16_FROM_REG(data->fan[nr]));
}

static ssize_t show_fan16_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", FAN16_FROM_REG(data->fan_min[nr]));
}

static ssize_t set_fan16_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct it87_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN16_TO_REG(val);
	it87_write_value(client, IT87_REG_FAN_MIN(nr),
			 data->fan_min[nr] & 0xff);
	it87_write_value(client, IT87_REG_FANX_MIN(nr),
			 data->fan_min[nr] >> 8);
	mutex_unlock(&data->update_lock);
	return count;
}

/* We want to use the same sysfs file names as 8-bit fans, but we need
   different variable names, so we have to use SENSOR_ATTR instead of
   SENSOR_DEVICE_ATTR. */
#define show_fan16_offset(offset) \
static struct sensor_device_attribute sensor_dev_attr_fan##offset##_input16 \
	= SENSOR_ATTR(fan##offset##_input, S_IRUGO,		\
		show_fan16, NULL, offset - 1);			\
static struct sensor_device_attribute sensor_dev_attr_fan##offset##_min16 \
	= SENSOR_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR,	\
		show_fan16_min, set_fan16_min, offset - 1)

show_fan16_offset(1);
show_fan16_offset(2);
show_fan16_offset(3);

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
	return sprintf(buf, "%u\n", data->vrm);
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

static ssize_t
show_vid_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%ld\n", (long) vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid_reg, NULL);

static struct attribute *it87_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp1_type.dev_attr.attr,
	&sensor_dev_attr_temp2_type.dev_attr.attr,
	&sensor_dev_attr_temp3_type.dev_attr.attr,

	&dev_attr_alarms.attr,
	NULL
};

static const struct attribute_group it87_group = {
	.attrs = it87_attributes,
};

static struct attribute *it87_attributes_opt[] = {
	&sensor_dev_attr_fan1_input16.dev_attr.attr,
	&sensor_dev_attr_fan1_min16.dev_attr.attr,
	&sensor_dev_attr_fan2_input16.dev_attr.attr,
	&sensor_dev_attr_fan2_min16.dev_attr.attr,
	&sensor_dev_attr_fan3_input16.dev_attr.attr,
	&sensor_dev_attr_fan3_min16.dev_attr.attr,

	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan3_div.dev_attr.attr,

	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,

	&dev_attr_vrm.attr,
	&dev_attr_cpu0_vid.attr,
	NULL
};

static const struct attribute_group it87_group_opt = {
	.attrs = it87_attributes_opt,
};

/* SuperIO detection - will change isa_address if a chip is found */
static int __init it87_find(unsigned short *address)
{
	int err = -ENODEV;

	superio_enter();
	chip_type = superio_inw(DEVID);
	if (chip_type != IT8712F_DEVID
	 && chip_type != IT8716F_DEVID
	 && chip_type != IT8718F_DEVID
	 && chip_type != IT8705F_DEVID)
	 	goto exit;

	superio_select(PME);
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

	/* Read GPIO config and VID value from LDN 7 (GPIO) */
	if (chip_type != IT8705F_DEVID) {
		int reg;

		superio_select(GPIO);
		if (chip_type == it8718)
			vid_value = superio_inb(IT87_SIO_VID_REG);

		reg = superio_inb(IT87_SIO_PINX2_REG);
		if (reg & (1 << 0))
			pr_info("it87: in3 is VCC (+5V)\n");
		if (reg & (1 << 1))
			pr_info("it87: in7 is VCCH (+5V Stand-By)\n");
	}

exit:
	superio_exit();
	return err;
}

/* This function is called by i2c_probe */
static int it87_detect(struct i2c_adapter *adapter)
{
	struct i2c_client *new_client;
	struct it87_data *data;
	int err = 0;
	const char *name;
	int enable_pwm_interface;

	/* Reserve the ISA region */
	if (!request_region(isa_address, IT87_EXTENT,
			    it87_isa_driver.driver.name)){
		err = -EBUSY;
		goto ERROR0;
	}

	if (!(data = kzalloc(sizeof(struct it87_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR1;
	}

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = isa_address;
	new_client->adapter = adapter;
	new_client->driver = &it87_isa_driver;

	/* Now, we do the remaining detection. */
	if ((it87_read_value(new_client, IT87_REG_CONFIG) & 0x80)
	 || it87_read_value(new_client, IT87_REG_CHIPID) != 0x90) {
		err = -ENODEV;
		goto ERROR2;
	}

	/* Determine the chip type. */
	switch (chip_type) {
	case IT8712F_DEVID:
		data->type = it8712;
		name = "it8712";
		break;
	case IT8716F_DEVID:
		data->type = it8716;
		name = "it8716";
		break;
	case IT8718F_DEVID:
		data->type = it8718;
		name = "it8718";
		break;
	default:
		data->type = it87;
		name = "it87";
	}

	/* Fill in the remaining client fields and put it into the global list */
	strlcpy(new_client->name, name, I2C_NAME_SIZE);
	mutex_init(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR2;

	/* Check PWM configuration */
	enable_pwm_interface = it87_check_pwm(new_client);

	/* Initialize the IT87 chip */
	it87_init_client(new_client, data);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&new_client->dev.kobj, &it87_group)))
		goto ERROR3;

	/* Do not create fan files for disabled fans */
	if (data->type == it8716 || data->type == it8718) {
		/* 16-bit tachometers */
		if (data->has_fan & (1 << 0)) {
			if ((err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan1_input16.dev_attr))
			 || (err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan1_min16.dev_attr)))
				goto ERROR4;
		}
		if (data->has_fan & (1 << 1)) {
			if ((err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan2_input16.dev_attr))
			 || (err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan2_min16.dev_attr)))
				goto ERROR4;
		}
		if (data->has_fan & (1 << 2)) {
			if ((err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan3_input16.dev_attr))
			 || (err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan3_min16.dev_attr)))
				goto ERROR4;
		}
	} else {
		/* 8-bit tachometers with clock divider */
		if (data->has_fan & (1 << 0)) {
			if ((err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan1_input.dev_attr))
			 || (err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan1_min.dev_attr))
			 || (err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan1_div.dev_attr)))
				goto ERROR4;
		}
		if (data->has_fan & (1 << 1)) {
			if ((err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan2_input.dev_attr))
			 || (err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan2_min.dev_attr))
			 || (err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan2_div.dev_attr)))
				goto ERROR4;
		}
		if (data->has_fan & (1 << 2)) {
			if ((err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan3_input.dev_attr))
			 || (err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan3_min.dev_attr))
			 || (err = device_create_file(&new_client->dev,
			     &sensor_dev_attr_fan3_div.dev_attr)))
				goto ERROR4;
		}
	}

	if (enable_pwm_interface) {
		if ((err = device_create_file(&new_client->dev,
		     &sensor_dev_attr_pwm1_enable.dev_attr))
		 || (err = device_create_file(&new_client->dev,
		     &sensor_dev_attr_pwm2_enable.dev_attr))
		 || (err = device_create_file(&new_client->dev,
		     &sensor_dev_attr_pwm3_enable.dev_attr))
		 || (err = device_create_file(&new_client->dev,
		     &sensor_dev_attr_pwm1.dev_attr))
		 || (err = device_create_file(&new_client->dev,
		     &sensor_dev_attr_pwm2.dev_attr))
		 || (err = device_create_file(&new_client->dev,
		     &sensor_dev_attr_pwm3.dev_attr))
		 || (err = device_create_file(&new_client->dev,
		     &dev_attr_pwm1_freq))
		 || (err = device_create_file(&new_client->dev,
		     &dev_attr_pwm2_freq))
		 || (err = device_create_file(&new_client->dev,
		     &dev_attr_pwm3_freq)))
			goto ERROR4;
	}

	if (data->type == it8712 || data->type == it8716
	 || data->type == it8718) {
		data->vrm = vid_which_vrm();
		/* VID reading from Super-I/O config space if available */
		data->vid = vid_value;
		if ((err = device_create_file(&new_client->dev,
		     &dev_attr_vrm))
		 || (err = device_create_file(&new_client->dev,
		     &dev_attr_cpu0_vid)))
			goto ERROR4;
	}

	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto ERROR4;
	}

	return 0;

ERROR4:
	sysfs_remove_group(&new_client->dev.kobj, &it87_group);
	sysfs_remove_group(&new_client->dev.kobj, &it87_group_opt);
ERROR3:
	i2c_detach_client(new_client);
ERROR2:
	kfree(data);
ERROR1:
	release_region(isa_address, IT87_EXTENT);
ERROR0:
	return err;
}

static int it87_detach_client(struct i2c_client *client)
{
	struct it87_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);
	sysfs_remove_group(&client->dev.kobj, &it87_group);
	sysfs_remove_group(&client->dev.kobj, &it87_group_opt);

	if ((err = i2c_detach_client(client)))
		return err;

	release_region(client->addr, IT87_EXTENT);
	kfree(data);

	return 0;
}

/* Must be called with data->update_lock held, except during initialization.
   We ignore the IT87 BUSY flag at this moment - it could lead to deadlocks,
   would slow down the IT87 access and should not be necessary. */
static int it87_read_value(struct i2c_client *client, u8 reg)
{
	outb_p(reg, client->addr + IT87_ADDR_REG_OFFSET);
	return inb_p(client->addr + IT87_DATA_REG_OFFSET);
}

/* Must be called with data->update_lock held, except during initialization.
   We ignore the IT87 BUSY flag at this moment - it could lead to deadlocks,
   would slow down the IT87 access and should not be necessary. */
static void it87_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	outb_p(reg, client->addr + IT87_ADDR_REG_OFFSET);
	outb_p(value, client->addr + IT87_DATA_REG_OFFSET);
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

	/* Some chips seem to have default value 0xff for all limit
	 * registers. For low voltage limits it makes no sense and triggers
	 * alarms, so change to 0 instead. For high temperature limits, it
	 * means -1 degree C, which surprisingly doesn't trigger an alarm,
	 * but is still confusing, so change to 127 degrees C. */
	for (i = 0; i < 8; i++) {
		tmp = it87_read_value(client, IT87_REG_VIN_MIN(i));
		if (tmp == 0xff)
			it87_write_value(client, IT87_REG_VIN_MIN(i), 0);
	}
	for (i = 0; i < 3; i++) {
		tmp = it87_read_value(client, IT87_REG_TEMP_HIGH(i));
		if (tmp == 0xff)
			it87_write_value(client, IT87_REG_TEMP_HIGH(i), 127);
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
	data->has_fan = (data->fan_main_ctrl >> 4) & 0x07;

	/* Set tachometers to 16-bit mode if needed */
	if (data->type == it8716 || data->type == it8718) {
		tmp = it87_read_value(client, IT87_REG_FAN_16BIT);
		if (~tmp & 0x07 & data->has_fan) {
			dev_dbg(&client->dev,
				"Setting fan1-3 to 16-bit mode\n");
			it87_write_value(client, IT87_REG_FAN_16BIT,
					 tmp | 0x07);
		}
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

	mutex_lock(&data->update_lock);

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
		/* in8 (battery) has no limit registers */
		data->in[8] =
		    it87_read_value(client, IT87_REG_VIN(8));

		for (i = 0; i < 3; i++) {
			/* Skip disabled fans */
			if (!(data->has_fan & (1 << i)))
				continue;

			data->fan_min[i] =
			    it87_read_value(client, IT87_REG_FAN_MIN(i));
			data->fan[i] = it87_read_value(client,
				       IT87_REG_FAN(i));
			/* Add high byte if in 16-bit mode */
			if (data->type == it8716 || data->type == it8718) {
				data->fan[i] |= it87_read_value(client,
						IT87_REG_FANX(i)) << 8;
				data->fan_min[i] |= it87_read_value(client,
						IT87_REG_FANX_MIN(i)) << 8;
			}
		}
		for (i = 0; i < 3; i++) {
			data->temp[i] =
			    it87_read_value(client, IT87_REG_TEMP(i));
			data->temp_high[i] =
			    it87_read_value(client, IT87_REG_TEMP_HIGH(i));
			data->temp_low[i] =
			    it87_read_value(client, IT87_REG_TEMP_LOW(i));
		}

		/* Newer chips don't have clock dividers */
		if ((data->has_fan & 0x07) && data->type != it8716
		 && data->type != it8718) {
			i = it87_read_value(client, IT87_REG_FAN_DIV);
			data->fan_div[0] = i & 0x07;
			data->fan_div[1] = (i >> 3) & 0x07;
			data->fan_div[2] = (i & 0x40) ? 3 : 1;
		}

		data->alarms =
			it87_read_value(client, IT87_REG_ALARM1) |
			(it87_read_value(client, IT87_REG_ALARM2) << 8) |
			(it87_read_value(client, IT87_REG_ALARM3) << 16);
		data->fan_main_ctrl = it87_read_value(client, IT87_REG_FAN_MAIN_CTRL);
		data->fan_ctl = it87_read_value(client, IT87_REG_FAN_CTL);

		data->sensor = it87_read_value(client, IT87_REG_TEMP_ENABLE);
		/* The 8705 does not have VID capability */
		if (data->type == it8712 || data->type == it8716) {
			data->vid = it87_read_value(client, IT87_REG_VID);
			/* The older IT8712F revisions had only 5 VID pins,
			   but we assume it is always safe to read 6 bits. */
			data->vid &= 0x3f;
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init sm_it87_init(void)
{
	int res;

	if ((res = it87_find(&isa_address)))
		return res;
	return i2c_isa_add_driver(&it87_isa_driver);
}

static void __exit sm_it87_exit(void)
{
	i2c_isa_del_driver(&it87_isa_driver);
}


MODULE_AUTHOR("Chris Gauthron <chrisg@0-in.com>, "
	      "Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("IT8705F/8712F/8716F/8718F, SiS950 driver");
module_param(update_vbat, bool, 0);
MODULE_PARM_DESC(update_vbat, "Update vbat if set else return powerup value");
module_param(fix_pwm_polarity, bool, 0);
MODULE_PARM_DESC(fix_pwm_polarity, "Force PWM polarity to active high (DANGEROUS)");
MODULE_LICENSE("GPL");

module_init(sm_it87_init);
module_exit(sm_it87_exit);
