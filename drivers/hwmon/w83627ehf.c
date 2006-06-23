/*
    w83627ehf - Driver for the hardware monitoring functionality of
                the Winbond W83627EHF Super-I/O chip
    Copyright (C) 2005  Jean Delvare <khali@linux-fr.org>

    Shamelessly ripped from the w83627hf driver
    Copyright (C) 2003  Mark Studebaker

    Thanks to Leon Moonen, Steve Cliffe and Grant Coady for their help
    in testing and debugging this driver.

    This driver also supports the W83627EHG, which is the lead-free
    version of the W83627EHF.

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


    Supports the following chips:

    Chip        #vin    #fan    #pwm    #temp   chip_id man_id
    w83627ehf   10      5       -       3       0x88    0x5ca3
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/i2c-isa.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <asm/io.h>
#include "lm75.h"

/* The actual ISA address is read from Super-I/O configuration space */
static unsigned short address;

/*
 * Super-I/O constants and functions
 */

static int REG;		/* The register to read/write */
static int VAL;		/* The value to read/write */

#define W83627EHF_LD_HWM	0x0b

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */

#define SIO_W83627EHF_ID	0x8840
#define SIO_ID_MASK		0xFFC0

static inline void
superio_outb(int reg, int val)
{
	outb(reg, REG);
	outb(val, VAL);
}

static inline int
superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

static inline void
superio_select(int ld)
{
	outb(SIO_REG_LDSEL, REG);
	outb(ld, VAL);
}

static inline void
superio_enter(void)
{
	outb(0x87, REG);
	outb(0x87, REG);
}

static inline void
superio_exit(void)
{
	outb(0x02, REG);
	outb(0x02, VAL);
}

/*
 * ISA constants
 */

#define REGION_ALIGNMENT	~7
#define REGION_OFFSET		5
#define REGION_LENGTH		2
#define ADDR_REG_OFFSET		5
#define DATA_REG_OFFSET		6

#define W83627EHF_REG_BANK		0x4E
#define W83627EHF_REG_CONFIG		0x40
#define W83627EHF_REG_CHIP_ID		0x49
#define W83627EHF_REG_MAN_ID		0x4F

static const u16 W83627EHF_REG_FAN[] = { 0x28, 0x29, 0x2a, 0x3f, 0x553 };
static const u16 W83627EHF_REG_FAN_MIN[] = { 0x3b, 0x3c, 0x3d, 0x3e, 0x55c };

/* The W83627EHF registers for nr=7,8,9 are in bank 5 */
#define W83627EHF_REG_IN_MAX(nr)	((nr < 7) ? (0x2b + (nr) * 2) : \
					 (0x554 + (((nr) - 7) * 2)))
#define W83627EHF_REG_IN_MIN(nr)	((nr < 7) ? (0x2c + (nr) * 2) : \
					 (0x555 + (((nr) - 7) * 2)))
#define W83627EHF_REG_IN(nr)		((nr < 7) ? (0x20 + (nr)) : \
					 (0x550 + (nr) - 7))

#define W83627EHF_REG_TEMP1		0x27
#define W83627EHF_REG_TEMP1_HYST	0x3a
#define W83627EHF_REG_TEMP1_OVER	0x39
static const u16 W83627EHF_REG_TEMP[] = { 0x150, 0x250 };
static const u16 W83627EHF_REG_TEMP_HYST[] = { 0x153, 0x253 };
static const u16 W83627EHF_REG_TEMP_OVER[] = { 0x155, 0x255 };
static const u16 W83627EHF_REG_TEMP_CONFIG[] = { 0x152, 0x252 };

/* Fan clock dividers are spread over the following five registers */
#define W83627EHF_REG_FANDIV1		0x47
#define W83627EHF_REG_FANDIV2		0x4B
#define W83627EHF_REG_VBAT		0x5D
#define W83627EHF_REG_DIODE		0x59
#define W83627EHF_REG_SMI_OVT		0x4C

#define W83627EHF_REG_ALARM1		0x459
#define W83627EHF_REG_ALARM2		0x45A
#define W83627EHF_REG_ALARM3		0x45B

/*
 * Conversions
 */

static inline unsigned int
fan_from_reg(u8 reg, unsigned int div)
{
	if (reg == 0 || reg == 255)
		return 0;
	return 1350000U / (reg * div);
}

static inline unsigned int
div_from_reg(u8 reg)
{
	return 1 << reg;
}

static inline int
temp1_from_reg(s8 reg)
{
	return reg * 1000;
}

static inline s8
temp1_to_reg(int temp)
{
	if (temp <= -128000)
		return -128;
	if (temp >= 127000)
		return 127;
	if (temp < 0)
		return (temp - 500) / 1000;
	return (temp + 500) / 1000;
}

/* Some of analog inputs have internal scaling (2x), 8mV is ADC LSB */

static u8 scale_in[10] = { 8, 8, 16, 16, 8, 8, 8, 16, 16, 8 };

static inline long in_from_reg(u8 reg, u8 nr)
{
	return reg * scale_in[nr];
}

static inline u8 in_to_reg(u32 val, u8 nr)
{
	return SENSORS_LIMIT(((val + (scale_in[nr] / 2)) / scale_in[nr]), 0, 255);
}

/*
 * Data structures and manipulation thereof
 */

struct w83627ehf_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct mutex lock;

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* Register values */
	u8 in[10];		/* Register value */
	u8 in_max[10];		/* Register value */
	u8 in_min[10];		/* Register value */
	u8 fan[5];
	u8 fan_min[5];
	u8 fan_div[5];
	u8 has_fan;		/* some fan inputs can be disabled */
	s8 temp1;
	s8 temp1_max;
	s8 temp1_max_hyst;
	s16 temp[2];
	s16 temp_max[2];
	s16 temp_max_hyst[2];
	u32 alarms;
};

static inline int is_word_sized(u16 reg)
{
	return (((reg & 0xff00) == 0x100
	      || (reg & 0xff00) == 0x200)
	     && ((reg & 0x00ff) == 0x50
	      || (reg & 0x00ff) == 0x53
	      || (reg & 0x00ff) == 0x55));
}

/* We assume that the default bank is 0, thus the following two functions do
   nothing for registers which live in bank 0. For others, they respectively
   set the bank register to the correct value (before the register is
   accessed), and back to 0 (afterwards). */
static inline void w83627ehf_set_bank(struct i2c_client *client, u16 reg)
{
	if (reg & 0xff00) {
		outb_p(W83627EHF_REG_BANK, client->addr + ADDR_REG_OFFSET);
		outb_p(reg >> 8, client->addr + DATA_REG_OFFSET);
	}
}

static inline void w83627ehf_reset_bank(struct i2c_client *client, u16 reg)
{
	if (reg & 0xff00) {
		outb_p(W83627EHF_REG_BANK, client->addr + ADDR_REG_OFFSET);
		outb_p(0, client->addr + DATA_REG_OFFSET);
	}
}

static u16 w83627ehf_read_value(struct i2c_client *client, u16 reg)
{
	struct w83627ehf_data *data = i2c_get_clientdata(client);
	int res, word_sized = is_word_sized(reg);

	mutex_lock(&data->lock);

	w83627ehf_set_bank(client, reg);
	outb_p(reg & 0xff, client->addr + ADDR_REG_OFFSET);
	res = inb_p(client->addr + DATA_REG_OFFSET);
	if (word_sized) {
		outb_p((reg & 0xff) + 1,
		       client->addr + ADDR_REG_OFFSET);
		res = (res << 8) + inb_p(client->addr + DATA_REG_OFFSET);
	}
	w83627ehf_reset_bank(client, reg);

	mutex_unlock(&data->lock);

	return res;
}

static int w83627ehf_write_value(struct i2c_client *client, u16 reg, u16 value)
{
	struct w83627ehf_data *data = i2c_get_clientdata(client);
	int word_sized = is_word_sized(reg);

	mutex_lock(&data->lock);

	w83627ehf_set_bank(client, reg);
	outb_p(reg & 0xff, client->addr + ADDR_REG_OFFSET);
	if (word_sized) {
		outb_p(value >> 8, client->addr + DATA_REG_OFFSET);
		outb_p((reg & 0xff) + 1,
		       client->addr + ADDR_REG_OFFSET);
	}
	outb_p(value & 0xff, client->addr + DATA_REG_OFFSET);
	w83627ehf_reset_bank(client, reg);

	mutex_unlock(&data->lock);
	return 0;
}

/* This function assumes that the caller holds data->update_lock */
static void w83627ehf_write_fan_div(struct i2c_client *client, int nr)
{
	struct w83627ehf_data *data = i2c_get_clientdata(client);
	u8 reg;

	switch (nr) {
	case 0:
		reg = (w83627ehf_read_value(client, W83627EHF_REG_FANDIV1) & 0xcf)
		    | ((data->fan_div[0] & 0x03) << 4);
		w83627ehf_write_value(client, W83627EHF_REG_FANDIV1, reg);
		reg = (w83627ehf_read_value(client, W83627EHF_REG_VBAT) & 0xdf)
		    | ((data->fan_div[0] & 0x04) << 3);
		w83627ehf_write_value(client, W83627EHF_REG_VBAT, reg);
		break;
	case 1:
		reg = (w83627ehf_read_value(client, W83627EHF_REG_FANDIV1) & 0x3f)
		    | ((data->fan_div[1] & 0x03) << 6);
		w83627ehf_write_value(client, W83627EHF_REG_FANDIV1, reg);
		reg = (w83627ehf_read_value(client, W83627EHF_REG_VBAT) & 0xbf)
		    | ((data->fan_div[1] & 0x04) << 4);
		w83627ehf_write_value(client, W83627EHF_REG_VBAT, reg);
		break;
	case 2:
		reg = (w83627ehf_read_value(client, W83627EHF_REG_FANDIV2) & 0x3f)
		    | ((data->fan_div[2] & 0x03) << 6);
		w83627ehf_write_value(client, W83627EHF_REG_FANDIV2, reg);
		reg = (w83627ehf_read_value(client, W83627EHF_REG_VBAT) & 0x7f)
		    | ((data->fan_div[2] & 0x04) << 5);
		w83627ehf_write_value(client, W83627EHF_REG_VBAT, reg);
		break;
	case 3:
		reg = (w83627ehf_read_value(client, W83627EHF_REG_DIODE) & 0xfc)
		    | (data->fan_div[3] & 0x03);
		w83627ehf_write_value(client, W83627EHF_REG_DIODE, reg);
		reg = (w83627ehf_read_value(client, W83627EHF_REG_SMI_OVT) & 0x7f)
		    | ((data->fan_div[3] & 0x04) << 5);
		w83627ehf_write_value(client, W83627EHF_REG_SMI_OVT, reg);
		break;
	case 4:
		reg = (w83627ehf_read_value(client, W83627EHF_REG_DIODE) & 0x73)
		    | ((data->fan_div[4] & 0x03) << 3)
		    | ((data->fan_div[4] & 0x04) << 5);
		w83627ehf_write_value(client, W83627EHF_REG_DIODE, reg);
		break;
	}
}

static struct w83627ehf_data *w83627ehf_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627ehf_data *data = i2c_get_clientdata(client);
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ)
	 || !data->valid) {
		/* Fan clock dividers */
		i = w83627ehf_read_value(client, W83627EHF_REG_FANDIV1);
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = (i >> 6) & 0x03;
		i = w83627ehf_read_value(client, W83627EHF_REG_FANDIV2);
		data->fan_div[2] = (i >> 6) & 0x03;
		i = w83627ehf_read_value(client, W83627EHF_REG_VBAT);
		data->fan_div[0] |= (i >> 3) & 0x04;
		data->fan_div[1] |= (i >> 4) & 0x04;
		data->fan_div[2] |= (i >> 5) & 0x04;
		if (data->has_fan & ((1 << 3) | (1 << 4))) {
			i = w83627ehf_read_value(client, W83627EHF_REG_DIODE);
			data->fan_div[3] = i & 0x03;
			data->fan_div[4] = ((i >> 2) & 0x03)
					 | ((i >> 5) & 0x04);
		}
		if (data->has_fan & (1 << 3)) {
			i = w83627ehf_read_value(client, W83627EHF_REG_SMI_OVT);
			data->fan_div[3] |= (i >> 5) & 0x04;
		}

		/* Measured voltages and limits */
		for (i = 0; i < 10; i++) {
			data->in[i] = w83627ehf_read_value(client,
				      W83627EHF_REG_IN(i));
			data->in_min[i] = w83627ehf_read_value(client,
					  W83627EHF_REG_IN_MIN(i));
			data->in_max[i] = w83627ehf_read_value(client,
					  W83627EHF_REG_IN_MAX(i));
		}

		/* Measured fan speeds and limits */
		for (i = 0; i < 5; i++) {
			if (!(data->has_fan & (1 << i)))
				continue;

			data->fan[i] = w83627ehf_read_value(client,
				       W83627EHF_REG_FAN[i]);
			data->fan_min[i] = w83627ehf_read_value(client,
					   W83627EHF_REG_FAN_MIN[i]);

			/* If we failed to measure the fan speed and clock
			   divider can be increased, let's try that for next
			   time */
			if (data->fan[i] == 0xff
			 && data->fan_div[i] < 0x07) {
			 	dev_dbg(&client->dev, "Increasing fan %d "
					"clock divider from %u to %u\n",
					i, div_from_reg(data->fan_div[i]),
					div_from_reg(data->fan_div[i] + 1));
				data->fan_div[i]++;
				w83627ehf_write_fan_div(client, i);
				/* Preserve min limit if possible */
				if (data->fan_min[i] >= 2
				 && data->fan_min[i] != 255)
					w83627ehf_write_value(client,
						W83627EHF_REG_FAN_MIN[i],
						(data->fan_min[i] /= 2));
			}
		}

		/* Measured temperatures and limits */
		data->temp1 = w83627ehf_read_value(client,
			      W83627EHF_REG_TEMP1);
		data->temp1_max = w83627ehf_read_value(client,
				  W83627EHF_REG_TEMP1_OVER);
		data->temp1_max_hyst = w83627ehf_read_value(client,
				       W83627EHF_REG_TEMP1_HYST);
		for (i = 0; i < 2; i++) {
			data->temp[i] = w83627ehf_read_value(client,
					W83627EHF_REG_TEMP[i]);
			data->temp_max[i] = w83627ehf_read_value(client,
					    W83627EHF_REG_TEMP_OVER[i]);
			data->temp_max_hyst[i] = w83627ehf_read_value(client,
						 W83627EHF_REG_TEMP_HYST[i]);
		}

		data->alarms = w83627ehf_read_value(client,
					W83627EHF_REG_ALARM1) |
			       (w83627ehf_read_value(client,
					W83627EHF_REG_ALARM2) << 8) |
			       (w83627ehf_read_value(client,
					W83627EHF_REG_ALARM3) << 16);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);
	return data;
}

/*
 * Sysfs callback functions
 */
#define show_in_reg(reg) \
static ssize_t \
show_##reg(struct device *dev, struct device_attribute *attr, \
	   char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%ld\n", in_from_reg(data->reg[nr], nr)); \
}
show_in_reg(in)
show_in_reg(in_min)
show_in_reg(in_max)

#define store_in_reg(REG, reg) \
static ssize_t \
store_in_##reg (struct device *dev, struct device_attribute *attr, \
			const char *buf, size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct w83627ehf_data *data = i2c_get_clientdata(client); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	u32 val = simple_strtoul(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->in_##reg[nr] = in_to_reg(val, nr); \
	w83627ehf_write_value(client, W83627EHF_REG_IN_##REG(nr), \
			      data->in_##reg[nr]); \
	mutex_unlock(&data->update_lock); \
	return count; \
}

store_in_reg(MIN, min)
store_in_reg(MAX, max)

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	return sprintf(buf, "%u\n", (data->alarms >> nr) & 0x01);
}

static struct sensor_device_attribute sda_in_input[] = {
	SENSOR_ATTR(in0_input, S_IRUGO, show_in, NULL, 0),
	SENSOR_ATTR(in1_input, S_IRUGO, show_in, NULL, 1),
	SENSOR_ATTR(in2_input, S_IRUGO, show_in, NULL, 2),
	SENSOR_ATTR(in3_input, S_IRUGO, show_in, NULL, 3),
	SENSOR_ATTR(in4_input, S_IRUGO, show_in, NULL, 4),
	SENSOR_ATTR(in5_input, S_IRUGO, show_in, NULL, 5),
	SENSOR_ATTR(in6_input, S_IRUGO, show_in, NULL, 6),
	SENSOR_ATTR(in7_input, S_IRUGO, show_in, NULL, 7),
	SENSOR_ATTR(in8_input, S_IRUGO, show_in, NULL, 8),
	SENSOR_ATTR(in9_input, S_IRUGO, show_in, NULL, 9),
};

static struct sensor_device_attribute sda_in_alarm[] = {
	SENSOR_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 0),
	SENSOR_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 1),
	SENSOR_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 2),
	SENSOR_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 3),
	SENSOR_ATTR(in4_alarm, S_IRUGO, show_alarm, NULL, 8),
	SENSOR_ATTR(in5_alarm, S_IRUGO, show_alarm, NULL, 21),
	SENSOR_ATTR(in6_alarm, S_IRUGO, show_alarm, NULL, 20),
	SENSOR_ATTR(in7_alarm, S_IRUGO, show_alarm, NULL, 16),
	SENSOR_ATTR(in8_alarm, S_IRUGO, show_alarm, NULL, 17),
	SENSOR_ATTR(in9_alarm, S_IRUGO, show_alarm, NULL, 19),
};

static struct sensor_device_attribute sda_in_min[] = {
       SENSOR_ATTR(in0_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 0),
       SENSOR_ATTR(in1_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 1),
       SENSOR_ATTR(in2_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 2),
       SENSOR_ATTR(in3_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 3),
       SENSOR_ATTR(in4_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 4),
       SENSOR_ATTR(in5_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 5),
       SENSOR_ATTR(in6_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 6),
       SENSOR_ATTR(in7_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 7),
       SENSOR_ATTR(in8_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 8),
       SENSOR_ATTR(in9_min, S_IWUSR | S_IRUGO, show_in_min, store_in_min, 9),
};

static struct sensor_device_attribute sda_in_max[] = {
       SENSOR_ATTR(in0_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 0),
       SENSOR_ATTR(in1_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 1),
       SENSOR_ATTR(in2_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 2),
       SENSOR_ATTR(in3_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 3),
       SENSOR_ATTR(in4_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 4),
       SENSOR_ATTR(in5_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 5),
       SENSOR_ATTR(in6_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 6),
       SENSOR_ATTR(in7_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 7),
       SENSOR_ATTR(in8_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 8),
       SENSOR_ATTR(in9_max, S_IWUSR | S_IRUGO, show_in_max, store_in_max, 9),
};

static void device_create_file_in(struct device *dev, int i)
{
	device_create_file(dev, &sda_in_input[i].dev_attr);
	device_create_file(dev, &sda_in_alarm[i].dev_attr);
	device_create_file(dev, &sda_in_min[i].dev_attr);
	device_create_file(dev, &sda_in_max[i].dev_attr);
}

#define show_fan_reg(reg) \
static ssize_t \
show_##reg(struct device *dev, struct device_attribute *attr, \
	   char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%d\n", \
		       fan_from_reg(data->reg[nr], \
				    div_from_reg(data->fan_div[nr]))); \
}
show_fan_reg(fan);
show_fan_reg(fan_min);

static ssize_t
show_fan_div(struct device *dev, struct device_attribute *attr,
	     char *buf)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	return sprintf(buf, "%u\n", div_from_reg(data->fan_div[nr]));
}

static ssize_t
store_fan_min(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627ehf_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	unsigned int val = simple_strtoul(buf, NULL, 10);
	unsigned int reg;
	u8 new_div;

	mutex_lock(&data->update_lock);
	if (!val) {
		/* No min limit, alarm disabled */
		data->fan_min[nr] = 255;
		new_div = data->fan_div[nr]; /* No change */
		dev_info(dev, "fan%u low limit and alarm disabled\n", nr + 1);
	} else if ((reg = 1350000U / val) >= 128 * 255) {
		/* Speed below this value cannot possibly be represented,
		   even with the highest divider (128) */
		data->fan_min[nr] = 254;
		new_div = 7; /* 128 == (1 << 7) */
		dev_warn(dev, "fan%u low limit %u below minimum %u, set to "
			 "minimum\n", nr + 1, val, fan_from_reg(254, 128));
	} else if (!reg) {
		/* Speed above this value cannot possibly be represented,
		   even with the lowest divider (1) */
		data->fan_min[nr] = 1;
		new_div = 0; /* 1 == (1 << 0) */
		dev_warn(dev, "fan%u low limit %u above maximum %u, set to "
			 "maximum\n", nr + 1, val, fan_from_reg(1, 1));
	} else {
		/* Automatically pick the best divider, i.e. the one such
		   that the min limit will correspond to a register value
		   in the 96..192 range */
		new_div = 0;
		while (reg > 192 && new_div < 7) {
			reg >>= 1;
			new_div++;
		}
		data->fan_min[nr] = reg;
	}

	/* Write both the fan clock divider (if it changed) and the new
	   fan min (unconditionally) */
	if (new_div != data->fan_div[nr]) {
		if (new_div > data->fan_div[nr])
			data->fan[nr] >>= (data->fan_div[nr] - new_div);
		else
			data->fan[nr] <<= (new_div - data->fan_div[nr]);

		dev_dbg(dev, "fan%u clock divider changed from %u to %u\n",
			nr + 1, div_from_reg(data->fan_div[nr]),
			div_from_reg(new_div));
		data->fan_div[nr] = new_div;
		w83627ehf_write_fan_div(client, nr);
	}
	w83627ehf_write_value(client, W83627EHF_REG_FAN_MIN[nr],
			      data->fan_min[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static struct sensor_device_attribute sda_fan_input[] = {
	SENSOR_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0),
	SENSOR_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1),
	SENSOR_ATTR(fan3_input, S_IRUGO, show_fan, NULL, 2),
	SENSOR_ATTR(fan4_input, S_IRUGO, show_fan, NULL, 3),
	SENSOR_ATTR(fan5_input, S_IRUGO, show_fan, NULL, 4),
};

static struct sensor_device_attribute sda_fan_alarm[] = {
	SENSOR_ATTR(fan1_alarm, S_IRUGO, show_alarm, NULL, 6),
	SENSOR_ATTR(fan2_alarm, S_IRUGO, show_alarm, NULL, 7),
	SENSOR_ATTR(fan3_alarm, S_IRUGO, show_alarm, NULL, 11),
	SENSOR_ATTR(fan4_alarm, S_IRUGO, show_alarm, NULL, 10),
	SENSOR_ATTR(fan5_alarm, S_IRUGO, show_alarm, NULL, 23),
};

static struct sensor_device_attribute sda_fan_min[] = {
	SENSOR_ATTR(fan1_min, S_IWUSR | S_IRUGO, show_fan_min,
		    store_fan_min, 0),
	SENSOR_ATTR(fan2_min, S_IWUSR | S_IRUGO, show_fan_min,
		    store_fan_min, 1),
	SENSOR_ATTR(fan3_min, S_IWUSR | S_IRUGO, show_fan_min,
		    store_fan_min, 2),
	SENSOR_ATTR(fan4_min, S_IWUSR | S_IRUGO, show_fan_min,
		    store_fan_min, 3),
	SENSOR_ATTR(fan5_min, S_IWUSR | S_IRUGO, show_fan_min,
		    store_fan_min, 4),
};

static struct sensor_device_attribute sda_fan_div[] = {
	SENSOR_ATTR(fan1_div, S_IRUGO, show_fan_div, NULL, 0),
	SENSOR_ATTR(fan2_div, S_IRUGO, show_fan_div, NULL, 1),
	SENSOR_ATTR(fan3_div, S_IRUGO, show_fan_div, NULL, 2),
	SENSOR_ATTR(fan4_div, S_IRUGO, show_fan_div, NULL, 3),
	SENSOR_ATTR(fan5_div, S_IRUGO, show_fan_div, NULL, 4),
};

static void device_create_file_fan(struct device *dev, int i)
{
	device_create_file(dev, &sda_fan_input[i].dev_attr);
	device_create_file(dev, &sda_fan_alarm[i].dev_attr);
	device_create_file(dev, &sda_fan_div[i].dev_attr);
	device_create_file(dev, &sda_fan_min[i].dev_attr);
}

#define show_temp1_reg(reg) \
static ssize_t \
show_##reg(struct device *dev, struct device_attribute *attr, \
	   char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	return sprintf(buf, "%d\n", temp1_from_reg(data->reg)); \
}
show_temp1_reg(temp1);
show_temp1_reg(temp1_max);
show_temp1_reg(temp1_max_hyst);

#define store_temp1_reg(REG, reg) \
static ssize_t \
store_temp1_##reg(struct device *dev, struct device_attribute *attr, \
		  const char *buf, size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct w83627ehf_data *data = i2c_get_clientdata(client); \
	u32 val = simple_strtoul(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->temp1_##reg = temp1_to_reg(val); \
	w83627ehf_write_value(client, W83627EHF_REG_TEMP1_##REG, \
			      data->temp1_##reg); \
	mutex_unlock(&data->update_lock); \
	return count; \
}
store_temp1_reg(OVER, max);
store_temp1_reg(HYST, max_hyst);

#define show_temp_reg(reg) \
static ssize_t \
show_##reg(struct device *dev, struct device_attribute *attr, \
	   char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%d\n", \
		       LM75_TEMP_FROM_REG(data->reg[nr])); \
}
show_temp_reg(temp);
show_temp_reg(temp_max);
show_temp_reg(temp_max_hyst);

#define store_temp_reg(REG, reg) \
static ssize_t \
store_##reg(struct device *dev, struct device_attribute *attr, \
	    const char *buf, size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct w83627ehf_data *data = i2c_get_clientdata(client); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	u32 val = simple_strtoul(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->reg[nr] = LM75_TEMP_TO_REG(val); \
	w83627ehf_write_value(client, W83627EHF_REG_TEMP_##REG[nr], \
			      data->reg[nr]); \
	mutex_unlock(&data->update_lock); \
	return count; \
}
store_temp_reg(OVER, temp_max);
store_temp_reg(HYST, temp_max_hyst);

static struct sensor_device_attribute sda_temp[] = {
	SENSOR_ATTR(temp1_input, S_IRUGO, show_temp1, NULL, 0),
	SENSOR_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 0),
	SENSOR_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 1),
	SENSOR_ATTR(temp1_max, S_IRUGO | S_IWUSR, show_temp1_max,
		    store_temp1_max, 0),
	SENSOR_ATTR(temp2_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 0),
	SENSOR_ATTR(temp3_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 1),
	SENSOR_ATTR(temp1_max_hyst, S_IRUGO | S_IWUSR, show_temp1_max_hyst,
		    store_temp1_max_hyst, 0),
	SENSOR_ATTR(temp2_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 0),
	SENSOR_ATTR(temp3_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 1),
	SENSOR_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 4),
	SENSOR_ATTR(temp2_alarm, S_IRUGO, show_alarm, NULL, 5),
	SENSOR_ATTR(temp3_alarm, S_IRUGO, show_alarm, NULL, 13),
};

/*
 * Driver and client management
 */

static struct i2c_driver w83627ehf_driver;

static void w83627ehf_init_client(struct i2c_client *client)
{
	int i;
	u8 tmp;

	/* Start monitoring is needed */
	tmp = w83627ehf_read_value(client, W83627EHF_REG_CONFIG);
	if (!(tmp & 0x01))
		w83627ehf_write_value(client, W83627EHF_REG_CONFIG,
				      tmp | 0x01);

	/* Enable temp2 and temp3 if needed */
	for (i = 0; i < 2; i++) {
		tmp = w83627ehf_read_value(client,
					   W83627EHF_REG_TEMP_CONFIG[i]);
		if (tmp & 0x01)
			w83627ehf_write_value(client,
					      W83627EHF_REG_TEMP_CONFIG[i],
					      tmp & 0xfe);
	}
}

static int w83627ehf_detect(struct i2c_adapter *adapter)
{
	struct i2c_client *client;
	struct w83627ehf_data *data;
	struct device *dev;
	int i, err = 0;

	if (!request_region(address + REGION_OFFSET, REGION_LENGTH,
	                    w83627ehf_driver.driver.name)) {
		err = -EBUSY;
		goto exit;
	}

	if (!(data = kzalloc(sizeof(struct w83627ehf_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit_release;
	}

	client = &data->client;
	i2c_set_clientdata(client, data);
	client->addr = address;
	mutex_init(&data->lock);
	client->adapter = adapter;
	client->driver = &w83627ehf_driver;
	client->flags = 0;
	dev = &client->dev;

	strlcpy(client->name, "w83627ehf", I2C_NAME_SIZE);
	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Tell the i2c layer a new client has arrived */
	if ((err = i2c_attach_client(client)))
		goto exit_free;

	/* Initialize the chip */
	w83627ehf_init_client(client);

	/* A few vars need to be filled upon startup */
	for (i = 0; i < 5; i++)
		data->fan_min[i] = w83627ehf_read_value(client,
				   W83627EHF_REG_FAN_MIN[i]);

	/* It looks like fan4 and fan5 pins can be alternatively used
	   as fan on/off switches */
	data->has_fan = 0x07; /* fan1, fan2 and fan3 */
	i = w83627ehf_read_value(client, W83627EHF_REG_FANDIV1);
	if (i & (1 << 2))
		data->has_fan |= (1 << 3);
	if (i & (1 << 0))
		data->has_fan |= (1 << 4);

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_detach;
	}

	for (i = 0; i < 10; i++)
		device_create_file_in(dev, i);

	for (i = 0; i < 5; i++) {
		if (data->has_fan & (1 << i))
			device_create_file_fan(dev, i);
	}
	for (i = 0; i < ARRAY_SIZE(sda_temp); i++)
		device_create_file(dev, &sda_temp[i].dev_attr);

	return 0;

exit_detach:
	i2c_detach_client(client);
exit_free:
	kfree(data);
exit_release:
	release_region(address + REGION_OFFSET, REGION_LENGTH);
exit:
	return err;
}

static int w83627ehf_detach_client(struct i2c_client *client)
{
	struct w83627ehf_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;
	release_region(client->addr + REGION_OFFSET, REGION_LENGTH);
	kfree(data);

	return 0;
}

static struct i2c_driver w83627ehf_driver = {
	.driver = {
		.name	= "w83627ehf",
	},
	.attach_adapter	= w83627ehf_detect,
	.detach_client	= w83627ehf_detach_client,
};

static int __init w83627ehf_find(int sioaddr, unsigned short *addr)
{
	u16 val;

	REG = sioaddr;
	VAL = sioaddr + 1;
	superio_enter();

	val = (superio_inb(SIO_REG_DEVID) << 8)
	    | superio_inb(SIO_REG_DEVID + 1);
	if ((val & SIO_ID_MASK) != SIO_W83627EHF_ID) {
		superio_exit();
		return -ENODEV;
	}

	superio_select(W83627EHF_LD_HWM);
	val = (superio_inb(SIO_REG_ADDR) << 8)
	    | superio_inb(SIO_REG_ADDR + 1);
	*addr = val & REGION_ALIGNMENT;
	if (*addr == 0) {
		superio_exit();
		return -ENODEV;
	}

	/* Activate logical device if needed */
	val = superio_inb(SIO_REG_ENABLE);
	if (!(val & 0x01))
		superio_outb(SIO_REG_ENABLE, val | 0x01);

	superio_exit();
	return 0;
}

static int __init sensors_w83627ehf_init(void)
{
	if (w83627ehf_find(0x2e, &address)
	 && w83627ehf_find(0x4e, &address))
		return -ENODEV;

	return i2c_isa_add_driver(&w83627ehf_driver);
}

static void __exit sensors_w83627ehf_exit(void)
{
	i2c_isa_del_driver(&w83627ehf_driver);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("W83627EHF driver");
MODULE_LICENSE("GPL");

module_init(sensors_w83627ehf_init);
module_exit(sensors_w83627ehf_exit);
