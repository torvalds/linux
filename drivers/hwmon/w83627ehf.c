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
    w83627ehf   -       5       -       3       0x88    0x5ca3

    This is a preliminary version of the driver, only supporting the
    fan and temperature inputs. The chip does much more than that.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/i2c-isa.h>
#include <linux/hwmon.h>
#include <linux/err.h>
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

#define REGION_LENGTH		8
#define ADDR_REG_OFFSET		5
#define DATA_REG_OFFSET		6

#define W83627EHF_REG_BANK		0x4E
#define W83627EHF_REG_CONFIG		0x40
#define W83627EHF_REG_CHIP_ID		0x49
#define W83627EHF_REG_MAN_ID		0x4F

static const u16 W83627EHF_REG_FAN[] = { 0x28, 0x29, 0x2a, 0x3f, 0x553 };
static const u16 W83627EHF_REG_FAN_MIN[] = { 0x3b, 0x3c, 0x3d, 0x3e, 0x55c };

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

/*
 * Data structures and manipulation thereof
 */

struct w83627ehf_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore lock;

	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* Register values */
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

	down(&data->lock);

	w83627ehf_set_bank(client, reg);
	outb_p(reg & 0xff, client->addr + ADDR_REG_OFFSET);
	res = inb_p(client->addr + DATA_REG_OFFSET);
	if (word_sized) {
		outb_p((reg & 0xff) + 1,
		       client->addr + ADDR_REG_OFFSET);
		res = (res << 8) + inb_p(client->addr + DATA_REG_OFFSET);
	}
	w83627ehf_reset_bank(client, reg);

	up(&data->lock);

	return res;
}

static int w83627ehf_write_value(struct i2c_client *client, u16 reg, u16 value)
{
	struct w83627ehf_data *data = i2c_get_clientdata(client);
	int word_sized = is_word_sized(reg);

	down(&data->lock);

	w83627ehf_set_bank(client, reg);
	outb_p(reg & 0xff, client->addr + ADDR_REG_OFFSET);
	if (word_sized) {
		outb_p(value >> 8, client->addr + DATA_REG_OFFSET);
		outb_p((reg & 0xff) + 1,
		       client->addr + ADDR_REG_OFFSET);
	}
	outb_p(value & 0xff, client->addr + DATA_REG_OFFSET);
	w83627ehf_reset_bank(client, reg);

	up(&data->lock);
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

	down(&data->update_lock);

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

		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);
	return data;
}

/*
 * Sysfs callback functions
 */

#define show_fan_reg(reg) \
static ssize_t \
show_##reg(struct device *dev, char *buf, int nr) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	return sprintf(buf, "%d\n", \
		       fan_from_reg(data->reg[nr], \
				    div_from_reg(data->fan_div[nr]))); \
}
show_fan_reg(fan);
show_fan_reg(fan_min);

static ssize_t
show_fan_div(struct device *dev, char *buf, int nr)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);
	return sprintf(buf, "%u\n",
		       div_from_reg(data->fan_div[nr]));
}

static ssize_t
store_fan_min(struct device *dev, const char *buf, size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627ehf_data *data = i2c_get_clientdata(client);
	unsigned int val = simple_strtoul(buf, NULL, 10);
	unsigned int reg;
	u8 new_div;

	down(&data->update_lock);
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
	up(&data->update_lock);

	return count;
}

#define sysfs_fan_offset(offset) \
static ssize_t \
show_reg_fan_##offset(struct device *dev, struct device_attribute *attr, \
		      char *buf) \
{ \
	return show_fan(dev, buf, offset-1); \
} \
static DEVICE_ATTR(fan##offset##_input, S_IRUGO, \
		   show_reg_fan_##offset, NULL);

#define sysfs_fan_min_offset(offset) \
static ssize_t \
show_reg_fan##offset##_min(struct device *dev, struct device_attribute *attr, \
			   char *buf) \
{ \
	return show_fan_min(dev, buf, offset-1); \
} \
static ssize_t \
store_reg_fan##offset##_min(struct device *dev, struct device_attribute *attr, \
			    const char *buf, size_t count) \
{ \
	return store_fan_min(dev, buf, count, offset-1); \
} \
static DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR, \
		   show_reg_fan##offset##_min, \
		   store_reg_fan##offset##_min);

#define sysfs_fan_div_offset(offset) \
static ssize_t \
show_reg_fan##offset##_div(struct device *dev, struct device_attribute *attr, \
			   char *buf) \
{ \
	return show_fan_div(dev, buf, offset - 1); \
} \
static DEVICE_ATTR(fan##offset##_div, S_IRUGO, \
		   show_reg_fan##offset##_div, NULL);

sysfs_fan_offset(1);
sysfs_fan_min_offset(1);
sysfs_fan_div_offset(1);
sysfs_fan_offset(2);
sysfs_fan_min_offset(2);
sysfs_fan_div_offset(2);
sysfs_fan_offset(3);
sysfs_fan_min_offset(3);
sysfs_fan_div_offset(3);
sysfs_fan_offset(4);
sysfs_fan_min_offset(4);
sysfs_fan_div_offset(4);
sysfs_fan_offset(5);
sysfs_fan_min_offset(5);
sysfs_fan_div_offset(5);

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
	down(&data->update_lock); \
	data->temp1_##reg = temp1_to_reg(val); \
	w83627ehf_write_value(client, W83627EHF_REG_TEMP1_##REG, \
			      data->temp1_##reg); \
	up(&data->update_lock); \
	return count; \
}
store_temp1_reg(OVER, max);
store_temp1_reg(HYST, max_hyst);

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp1, NULL);
static DEVICE_ATTR(temp1_max, S_IRUGO| S_IWUSR,
		   show_temp1_max, store_temp1_max);
static DEVICE_ATTR(temp1_max_hyst, S_IRUGO| S_IWUSR,
		   show_temp1_max_hyst, store_temp1_max_hyst);

#define show_temp_reg(reg) \
static ssize_t \
show_##reg (struct device *dev, char *buf, int nr) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	return sprintf(buf, "%d\n", \
		       LM75_TEMP_FROM_REG(data->reg[nr])); \
}
show_temp_reg(temp);
show_temp_reg(temp_max);
show_temp_reg(temp_max_hyst);

#define store_temp_reg(REG, reg) \
static ssize_t \
store_##reg (struct device *dev, const char *buf, size_t count, int nr) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct w83627ehf_data *data = i2c_get_clientdata(client); \
	u32 val = simple_strtoul(buf, NULL, 10); \
 \
	down(&data->update_lock); \
	data->reg[nr] = LM75_TEMP_TO_REG(val); \
	w83627ehf_write_value(client, W83627EHF_REG_TEMP_##REG[nr], \
			      data->reg[nr]); \
	up(&data->update_lock); \
	return count; \
}
store_temp_reg(OVER, temp_max);
store_temp_reg(HYST, temp_max_hyst);

#define sysfs_temp_offset(offset) \
static ssize_t \
show_reg_temp##offset (struct device *dev, struct device_attribute *attr, \
		       char *buf) \
{ \
	return show_temp(dev, buf, offset - 2); \
} \
static DEVICE_ATTR(temp##offset##_input, S_IRUGO, \
		   show_reg_temp##offset, NULL);

#define sysfs_temp_reg_offset(reg, offset) \
static ssize_t \
show_reg_temp##offset##_##reg(struct device *dev, struct device_attribute *attr, \
			      char *buf) \
{ \
	return show_temp_##reg(dev, buf, offset - 2); \
} \
static ssize_t \
store_reg_temp##offset##_##reg(struct device *dev, struct device_attribute *attr, \
			       const char *buf, size_t count) \
{ \
	return store_temp_##reg(dev, buf, count, offset - 2); \
} \
static DEVICE_ATTR(temp##offset##_##reg, S_IRUGO| S_IWUSR, \
		   show_reg_temp##offset##_##reg, \
		   store_reg_temp##offset##_##reg);

sysfs_temp_offset(2);
sysfs_temp_reg_offset(max, 2);
sysfs_temp_reg_offset(max_hyst, 2);
sysfs_temp_offset(3);
sysfs_temp_reg_offset(max, 3);
sysfs_temp_reg_offset(max_hyst, 3);

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
	int i, err = 0;

	if (!request_region(address, REGION_LENGTH, w83627ehf_driver.name)) {
		err = -EBUSY;
		goto exit;
	}

	if (!(data = kmalloc(sizeof(struct w83627ehf_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit_release;
	}
	memset(data, 0, sizeof(struct w83627ehf_data));

	client = &data->client;
	i2c_set_clientdata(client, data);
	client->addr = address;
	init_MUTEX(&data->lock);
	client->adapter = adapter;
	client->driver = &w83627ehf_driver;
	client->flags = 0;

	strlcpy(client->name, "w83627ehf", I2C_NAME_SIZE);
	data->valid = 0;
	init_MUTEX(&data->update_lock);

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
	data->class_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_detach;
	}

	device_create_file(&client->dev, &dev_attr_fan1_input);
	device_create_file(&client->dev, &dev_attr_fan1_min);
	device_create_file(&client->dev, &dev_attr_fan1_div);
	device_create_file(&client->dev, &dev_attr_fan2_input);
	device_create_file(&client->dev, &dev_attr_fan2_min);
	device_create_file(&client->dev, &dev_attr_fan2_div);
	device_create_file(&client->dev, &dev_attr_fan3_input);
	device_create_file(&client->dev, &dev_attr_fan3_min);
	device_create_file(&client->dev, &dev_attr_fan3_div);

	if (data->has_fan & (1 << 3)) {
		device_create_file(&client->dev, &dev_attr_fan4_input);
		device_create_file(&client->dev, &dev_attr_fan4_min);
		device_create_file(&client->dev, &dev_attr_fan4_div);
	}
	if (data->has_fan & (1 << 4)) {
		device_create_file(&client->dev, &dev_attr_fan5_input);
		device_create_file(&client->dev, &dev_attr_fan5_min);
		device_create_file(&client->dev, &dev_attr_fan5_div);
	}

	device_create_file(&client->dev, &dev_attr_temp1_input);
	device_create_file(&client->dev, &dev_attr_temp1_max);
	device_create_file(&client->dev, &dev_attr_temp1_max_hyst);
	device_create_file(&client->dev, &dev_attr_temp2_input);
	device_create_file(&client->dev, &dev_attr_temp2_max);
	device_create_file(&client->dev, &dev_attr_temp2_max_hyst);
	device_create_file(&client->dev, &dev_attr_temp3_input);
	device_create_file(&client->dev, &dev_attr_temp3_max);
	device_create_file(&client->dev, &dev_attr_temp3_max_hyst);

	return 0;

exit_detach:
	i2c_detach_client(client);
exit_free:
	kfree(data);
exit_release:
	release_region(address, REGION_LENGTH);
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
	release_region(client->addr, REGION_LENGTH);
	kfree(data);

	return 0;
}

static struct i2c_driver w83627ehf_driver = {
	.owner		= THIS_MODULE,
	.name		= "w83627ehf",
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
	*addr = val & ~(REGION_LENGTH - 1);
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
