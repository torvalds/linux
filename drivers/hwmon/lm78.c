/*
    lm78.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> 

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
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <asm/io.h>

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x20, 0x21, 0x22, 0x23, 0x24,
					0x25, 0x26, 0x27, 0x28, 0x29,
					0x2a, 0x2b, 0x2c, 0x2d, 0x2e,
					0x2f, I2C_CLIENT_END };
static unsigned short isa_address = 0x290;

/* Insmod parameters */
I2C_CLIENT_INSMOD_2(lm78, lm79);

/* Many LM78 constants specified below */

/* Length of ISA address segment */
#define LM78_EXTENT 8

/* Where are the ISA address/data registers relative to the base address */
#define LM78_ADDR_REG_OFFSET 5
#define LM78_DATA_REG_OFFSET 6

/* The LM78 registers */
#define LM78_REG_IN_MAX(nr) (0x2b + (nr) * 2)
#define LM78_REG_IN_MIN(nr) (0x2c + (nr) * 2)
#define LM78_REG_IN(nr) (0x20 + (nr))

#define LM78_REG_FAN_MIN(nr) (0x3b + (nr))
#define LM78_REG_FAN(nr) (0x28 + (nr))

#define LM78_REG_TEMP 0x27
#define LM78_REG_TEMP_OVER 0x39
#define LM78_REG_TEMP_HYST 0x3a

#define LM78_REG_ALARM1 0x41
#define LM78_REG_ALARM2 0x42

#define LM78_REG_VID_FANDIV 0x47

#define LM78_REG_CONFIG 0x40
#define LM78_REG_CHIPID 0x49
#define LM78_REG_I2C_ADDR 0x48


/* Conversions. Rounding and limit checking is only done on the TO_REG 
   variants. */

/* IN: mV, (0V to 4.08V)
   REG: 16mV/bit */
static inline u8 IN_TO_REG(unsigned long val)
{
	unsigned long nval = SENSORS_LIMIT(val, 0, 4080);
	return (nval + 8) / 16;
}
#define IN_FROM_REG(val) ((val) *  16)

static inline u8 FAN_TO_REG(long rpm, int div)
{
	if (rpm <= 0)
		return 255;
	return SENSORS_LIMIT((1350000 + rpm * div / 2) / (rpm * div), 1, 254);
}

static inline int FAN_FROM_REG(u8 val, int div)
{
	return val==0 ? -1 : val==255 ? 0 : 1350000/(val*div);
}

/* TEMP: mC (-128C to +127C)
   REG: 1C/bit, two's complement */
static inline s8 TEMP_TO_REG(int val)
{
	int nval = SENSORS_LIMIT(val, -128000, 127000) ;
	return nval<0 ? (nval-500)/1000 : (nval+500)/1000;
}

static inline int TEMP_FROM_REG(s8 val)
{
	return val * 1000;
}

#define DIV_FROM_REG(val) (1 << (val))

/* There are some complications in a module like this. First off, LM78 chips
   may be both present on the SMBus and the ISA bus, and we have to handle
   those cases separately at some places. Second, there might be several
   LM78 chips available (well, actually, that is probably never done; but
   it is a clean illustration of how to handle a case like that). Finally,
   a specific chip may be attached to *both* ISA and SMBus, and we would
   not like to detect it double. Fortunately, in the case of the LM78 at
   least, a register tells us what SMBus address we are on, so that helps
   a bit - except if there could be more than one SMBus. Groan. No solution
   for this yet. */

/* This module may seem overly long and complicated. In fact, it is not so
   bad. Quite a lot of bookkeeping is done. A real driver can often cut
   some corners. */

/* For each registered LM78, we need to keep some data in memory. That
   data is pointed to by lm78_list[NR]->data. The structure itself is
   dynamically allocated, at the same time when a new lm78 client is
   allocated. */
struct lm78_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct mutex lock;
	enum chips type;

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[7];		/* Register value */
	u8 in_max[7];		/* Register value */
	u8 in_min[7];		/* Register value */
	u8 fan[3];		/* Register value */
	u8 fan_min[3];		/* Register value */
	s8 temp;		/* Register value */
	s8 temp_over;		/* Register value */
	s8 temp_hyst;		/* Register value */
	u8 fan_div[3];		/* Register encoding, shifted right */
	u8 vid;			/* Register encoding, combined */
	u16 alarms;		/* Register encoding, combined */
};


static int lm78_attach_adapter(struct i2c_adapter *adapter);
static int lm78_isa_attach_adapter(struct i2c_adapter *adapter);
static int lm78_detect(struct i2c_adapter *adapter, int address, int kind);
static int lm78_detach_client(struct i2c_client *client);

static int lm78_read_value(struct i2c_client *client, u8 reg);
static int lm78_write_value(struct i2c_client *client, u8 reg, u8 value);
static struct lm78_data *lm78_update_device(struct device *dev);
static void lm78_init_client(struct i2c_client *client);


static struct i2c_driver lm78_driver = {
	.driver = {
		.name	= "lm78",
	},
	.id		= I2C_DRIVERID_LM78,
	.attach_adapter	= lm78_attach_adapter,
	.detach_client	= lm78_detach_client,
};

static struct i2c_driver lm78_isa_driver = {
	.driver = {
		.name	= "lm78-isa",
	},
	.attach_adapter	= lm78_isa_attach_adapter,
	.detach_client	= lm78_detach_client,
};


/* 7 Voltages */
static ssize_t show_in(struct device *dev, char *buf, int nr)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in[nr]));
}

static ssize_t show_in_min(struct device *dev, char *buf, int nr)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_min[nr]));
}

static ssize_t show_in_max(struct device *dev, char *buf, int nr)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in_max[nr]));
}

static ssize_t set_in_min(struct device *dev, const char *buf,
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm78_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->in_min[nr] = IN_TO_REG(val);
	lm78_write_value(client, LM78_REG_IN_MIN(nr), data->in_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_in_max(struct device *dev, const char *buf,
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm78_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->in_max[nr] = IN_TO_REG(val);
	lm78_write_value(client, LM78_REG_IN_MAX(nr), data->in_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
	
#define show_in_offset(offset)					\
static ssize_t							\
	show_in##offset (struct device *dev, struct device_attribute *attr, char *buf)		\
{								\
	return show_in(dev, buf, offset);			\
}								\
static DEVICE_ATTR(in##offset##_input, S_IRUGO, 		\
		show_in##offset, NULL);				\
static ssize_t							\
	show_in##offset##_min (struct device *dev, struct device_attribute *attr, char *buf)   \
{								\
	return show_in_min(dev, buf, offset);			\
}								\
static ssize_t							\
	show_in##offset##_max (struct device *dev, struct device_attribute *attr, char *buf)   \
{								\
	return show_in_max(dev, buf, offset);			\
}								\
static ssize_t set_in##offset##_min (struct device *dev, struct device_attribute *attr,	\
		const char *buf, size_t count)			\
{								\
	return set_in_min(dev, buf, count, offset);		\
}								\
static ssize_t set_in##offset##_max (struct device *dev, struct device_attribute *attr,	\
		const char *buf, size_t count)			\
{								\
	return set_in_max(dev, buf, count, offset);		\
}								\
static DEVICE_ATTR(in##offset##_min, S_IRUGO | S_IWUSR,		\
		show_in##offset##_min, set_in##offset##_min);	\
static DEVICE_ATTR(in##offset##_max, S_IRUGO | S_IWUSR,		\
		show_in##offset##_max, set_in##offset##_max);

show_in_offset(0);
show_in_offset(1);
show_in_offset(2);
show_in_offset(3);
show_in_offset(4);
show_in_offset(5);
show_in_offset(6);

/* Temperature */
static ssize_t show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp));
}

static ssize_t show_temp_over(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_over));
}

static ssize_t set_temp_over(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm78_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_over = TEMP_TO_REG(val);
	lm78_write_value(client, LM78_REG_TEMP_OVER, data->temp_over);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_temp_hyst(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_hyst));
}

static ssize_t set_temp_hyst(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm78_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_hyst = TEMP_TO_REG(val);
	lm78_write_value(client, LM78_REG_TEMP_HYST, data->temp_hyst);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL);
static DEVICE_ATTR(temp1_max, S_IRUGO | S_IWUSR,
		show_temp_over, set_temp_over);
static DEVICE_ATTR(temp1_max_hyst, S_IRUGO | S_IWUSR,
		show_temp_hyst, set_temp_hyst);

/* 3 Fans */
static ssize_t show_fan(struct device *dev, char *buf, int nr)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[nr],
		DIV_FROM_REG(data->fan_div[nr])) );
}

static ssize_t show_fan_min(struct device *dev, char *buf, int nr)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf,"%d\n", FAN_FROM_REG(data->fan_min[nr],
		DIV_FROM_REG(data->fan_div[nr])) );
}

static ssize_t set_fan_min(struct device *dev, const char *buf,
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm78_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	lm78_write_value(client, LM78_REG_FAN_MIN(nr), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_fan_div(struct device *dev, char *buf, int nr)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[nr]) );
}

/* Note: we save and restore the fan minimum here, because its value is
   determined in part by the fan divisor.  This follows the principle of
   least suprise; the user doesn't expect the fan minimum to change just
   because the divisor changed. */
static ssize_t set_fan_div(struct device *dev, const char *buf,
	size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm78_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);
	unsigned long min;
	u8 reg;

	mutex_lock(&data->update_lock);
	min = FAN_FROM_REG(data->fan_min[nr],
			   DIV_FROM_REG(data->fan_div[nr]));

	switch (val) {
	case 1: data->fan_div[nr] = 0; break;
	case 2: data->fan_div[nr] = 1; break;
	case 4: data->fan_div[nr] = 2; break;
	case 8: data->fan_div[nr] = 3; break;
	default:
		dev_err(&client->dev, "fan_div value %ld not "
			"supported. Choose one of 1, 2, 4 or 8!\n", val);
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	reg = lm78_read_value(client, LM78_REG_VID_FANDIV);
	switch (nr) {
	case 0:
		reg = (reg & 0xcf) | (data->fan_div[nr] << 4);
		break;
	case 1:
		reg = (reg & 0x3f) | (data->fan_div[nr] << 6);
		break;
	}
	lm78_write_value(client, LM78_REG_VID_FANDIV, reg);

	data->fan_min[nr] =
		FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	lm78_write_value(client, LM78_REG_FAN_MIN(nr), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

#define show_fan_offset(offset)						\
static ssize_t show_fan_##offset (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return show_fan(dev, buf, offset - 1);				\
}									\
static ssize_t show_fan_##offset##_min (struct device *dev, struct device_attribute *attr, char *buf)  \
{									\
	return show_fan_min(dev, buf, offset - 1);			\
}									\
static ssize_t show_fan_##offset##_div (struct device *dev, struct device_attribute *attr, char *buf)  \
{									\
	return show_fan_div(dev, buf, offset - 1);			\
}									\
static ssize_t set_fan_##offset##_min (struct device *dev, struct device_attribute *attr,		\
		const char *buf, size_t count)				\
{									\
	return set_fan_min(dev, buf, count, offset - 1);		\
}									\
static DEVICE_ATTR(fan##offset##_input, S_IRUGO, show_fan_##offset, NULL);\
static DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR,		\
		show_fan_##offset##_min, set_fan_##offset##_min);

static ssize_t set_fan_1_div(struct device *dev, struct device_attribute *attr, const char *buf,
		size_t count)
{
	return set_fan_div(dev, buf, count, 0) ;
}

static ssize_t set_fan_2_div(struct device *dev, struct device_attribute *attr, const char *buf,
		size_t count)
{
	return set_fan_div(dev, buf, count, 1) ;
}

show_fan_offset(1);
show_fan_offset(2);
show_fan_offset(3);

/* Fan 3 divisor is locked in H/W */
static DEVICE_ATTR(fan1_div, S_IRUGO | S_IWUSR,
		show_fan_1_div, set_fan_1_div);
static DEVICE_ATTR(fan2_div, S_IRUGO | S_IWUSR,
		show_fan_2_div, set_fan_2_div);
static DEVICE_ATTR(fan3_div, S_IRUGO, show_fan_3_div, NULL);

/* VID */
static ssize_t show_vid(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf, "%d\n", vid_from_reg(data->vid, 82));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

/* Alarms */
static ssize_t show_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm78_data *data = lm78_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

/* This function is called when:
     * lm78_driver is inserted (when this module is loaded), for each
       available adapter
     * when a new adapter is inserted (and lm78_driver is still present) */
static int lm78_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, lm78_detect);
}

static int lm78_isa_attach_adapter(struct i2c_adapter *adapter)
{
	return lm78_detect(adapter, isa_address, -1);
}

/* This function is called by i2c_probe */
static int lm78_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int i, err;
	struct i2c_client *new_client;
	struct lm78_data *data;
	const char *client_name = "";
	int is_isa = i2c_is_isa_adapter(adapter);

	if (!is_isa &&
	    !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		err = -ENODEV;
		goto ERROR0;
	}

	/* Reserve the ISA region */
	if (is_isa)
		if (!request_region(address, LM78_EXTENT,
				    lm78_isa_driver.driver.name)) {
			err = -EBUSY;
			goto ERROR0;
		}

	/* Probe whether there is anything available on this address. Already
	   done for SMBus clients */
	if (kind < 0) {
		if (is_isa) {

#define REALLY_SLOW_IO
			/* We need the timeouts for at least some LM78-like
			   chips. But only if we read 'undefined' registers. */
			i = inb_p(address + 1);
			if (inb_p(address + 2) != i) {
				err = -ENODEV;
				goto ERROR1;
			}
			if (inb_p(address + 3) != i) {
				err = -ENODEV;
				goto ERROR1;
			}
			if (inb_p(address + 7) != i) {
				err = -ENODEV;
				goto ERROR1;
			}
#undef REALLY_SLOW_IO

			/* Let's just hope nothing breaks here */
			i = inb_p(address + 5) & 0x7f;
			outb_p(~i & 0x7f, address + 5);
			if ((inb_p(address + 5) & 0x7f) != (~i & 0x7f)) {
				outb_p(i, address + 5);
				err = -ENODEV;
				goto ERROR1;
			}
		}
	}

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access lm78_{read,write}_value. */

	if (!(data = kzalloc(sizeof(struct lm78_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR1;
	}

	new_client = &data->client;
	if (is_isa)
		mutex_init(&data->lock);
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = is_isa ? &lm78_isa_driver : &lm78_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. */
	if (kind < 0) {
		if (lm78_read_value(new_client, LM78_REG_CONFIG) & 0x80) {
			err = -ENODEV;
			goto ERROR2;
		}
		if (!is_isa && (lm78_read_value(
				new_client, LM78_REG_I2C_ADDR) != address)) {
			err = -ENODEV;
			goto ERROR2;
		}
	}

	/* Determine the chip type. */
	if (kind <= 0) {
		i = lm78_read_value(new_client, LM78_REG_CHIPID);
		if (i == 0x00 || i == 0x20	/* LM78 */
		 || i == 0x40)			/* LM78-J */
			kind = lm78;
		else if ((i & 0xfe) == 0xc0)
			kind = lm79;
		else {
			if (kind == 0)
				dev_warn(&adapter->dev, "Ignoring 'force' "
					"parameter for unknown chip at "
					"adapter %d, address 0x%02x\n",
					i2c_adapter_id(adapter), address);
			err = -ENODEV;
			goto ERROR2;
		}
	}

	if (kind == lm78) {
		client_name = "lm78";
	} else if (kind == lm79) {
		client_name = "lm79";
	}

	/* Fill in the remaining client fields and put into the global list */
	strlcpy(new_client->name, client_name, I2C_NAME_SIZE);
	data->type = kind;

	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR2;

	/* Initialize the LM78 chip */
	lm78_init_client(new_client);

	/* A few vars need to be filled upon startup */
	for (i = 0; i < 3; i++) {
		data->fan_min[i] = lm78_read_value(new_client,
					LM78_REG_FAN_MIN(i));
	}

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto ERROR3;
	}

	device_create_file(&new_client->dev, &dev_attr_in0_input);
	device_create_file(&new_client->dev, &dev_attr_in0_min);
	device_create_file(&new_client->dev, &dev_attr_in0_max);
	device_create_file(&new_client->dev, &dev_attr_in1_input);
	device_create_file(&new_client->dev, &dev_attr_in1_min);
	device_create_file(&new_client->dev, &dev_attr_in1_max);
	device_create_file(&new_client->dev, &dev_attr_in2_input);
	device_create_file(&new_client->dev, &dev_attr_in2_min);
	device_create_file(&new_client->dev, &dev_attr_in2_max);
	device_create_file(&new_client->dev, &dev_attr_in3_input);
	device_create_file(&new_client->dev, &dev_attr_in3_min);
	device_create_file(&new_client->dev, &dev_attr_in3_max);
	device_create_file(&new_client->dev, &dev_attr_in4_input);
	device_create_file(&new_client->dev, &dev_attr_in4_min);
	device_create_file(&new_client->dev, &dev_attr_in4_max);
	device_create_file(&new_client->dev, &dev_attr_in5_input);
	device_create_file(&new_client->dev, &dev_attr_in5_min);
	device_create_file(&new_client->dev, &dev_attr_in5_max);
	device_create_file(&new_client->dev, &dev_attr_in6_input);
	device_create_file(&new_client->dev, &dev_attr_in6_min);
	device_create_file(&new_client->dev, &dev_attr_in6_max);
	device_create_file(&new_client->dev, &dev_attr_temp1_input);
	device_create_file(&new_client->dev, &dev_attr_temp1_max);
	device_create_file(&new_client->dev, &dev_attr_temp1_max_hyst);
	device_create_file(&new_client->dev, &dev_attr_fan1_input);
	device_create_file(&new_client->dev, &dev_attr_fan1_min);
	device_create_file(&new_client->dev, &dev_attr_fan1_div);
	device_create_file(&new_client->dev, &dev_attr_fan2_input);
	device_create_file(&new_client->dev, &dev_attr_fan2_min);
	device_create_file(&new_client->dev, &dev_attr_fan2_div);
	device_create_file(&new_client->dev, &dev_attr_fan3_input);
	device_create_file(&new_client->dev, &dev_attr_fan3_min);
	device_create_file(&new_client->dev, &dev_attr_fan3_div);
	device_create_file(&new_client->dev, &dev_attr_alarms);
	device_create_file(&new_client->dev, &dev_attr_cpu0_vid);

	return 0;

ERROR3:
	i2c_detach_client(new_client);
ERROR2:
	kfree(data);
ERROR1:
	if (is_isa)
		release_region(address, LM78_EXTENT);
ERROR0:
	return err;
}

static int lm78_detach_client(struct i2c_client *client)
{
	struct lm78_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	if(i2c_is_isa_client(client))
		release_region(client->addr, LM78_EXTENT);

	kfree(data);

	return 0;
}

/* The SMBus locks itself, but ISA access must be locked explicitly! 
   We don't want to lock the whole ISA bus, so we lock each client
   separately.
   We ignore the LM78 BUSY flag at this moment - it could lead to deadlocks,
   would slow down the LM78 access and should not be necessary.  */
static int lm78_read_value(struct i2c_client *client, u8 reg)
{
	int res;
	if (i2c_is_isa_client(client)) {
		struct lm78_data *data = i2c_get_clientdata(client);
		mutex_lock(&data->lock);
		outb_p(reg, client->addr + LM78_ADDR_REG_OFFSET);
		res = inb_p(client->addr + LM78_DATA_REG_OFFSET);
		mutex_unlock(&data->lock);
		return res;
	} else
		return i2c_smbus_read_byte_data(client, reg);
}

/* The SMBus locks itself, but ISA access muse be locked explicitly! 
   We don't want to lock the whole ISA bus, so we lock each client
   separately.
   We ignore the LM78 BUSY flag at this moment - it could lead to deadlocks,
   would slow down the LM78 access and should not be necessary. 
   There are some ugly typecasts here, but the good new is - they should
   nowhere else be necessary! */
static int lm78_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	if (i2c_is_isa_client(client)) {
		struct lm78_data *data = i2c_get_clientdata(client);
		mutex_lock(&data->lock);
		outb_p(reg, client->addr + LM78_ADDR_REG_OFFSET);
		outb_p(value, client->addr + LM78_DATA_REG_OFFSET);
		mutex_unlock(&data->lock);
		return 0;
	} else
		return i2c_smbus_write_byte_data(client, reg, value);
}

static void lm78_init_client(struct i2c_client *client)
{
	u8 config = lm78_read_value(client, LM78_REG_CONFIG);

	/* Start monitoring */
	if (!(config & 0x01))
		lm78_write_value(client, LM78_REG_CONFIG,
				 (config & 0xf7) | 0x01);
}

static struct lm78_data *lm78_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm78_data *data = i2c_get_clientdata(client);
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {

		dev_dbg(&client->dev, "Starting lm78 update\n");

		for (i = 0; i <= 6; i++) {
			data->in[i] =
			    lm78_read_value(client, LM78_REG_IN(i));
			data->in_min[i] =
			    lm78_read_value(client, LM78_REG_IN_MIN(i));
			data->in_max[i] =
			    lm78_read_value(client, LM78_REG_IN_MAX(i));
		}
		for (i = 0; i < 3; i++) {
			data->fan[i] =
			    lm78_read_value(client, LM78_REG_FAN(i));
			data->fan_min[i] =
			    lm78_read_value(client, LM78_REG_FAN_MIN(i));
		}
		data->temp = lm78_read_value(client, LM78_REG_TEMP);
		data->temp_over =
		    lm78_read_value(client, LM78_REG_TEMP_OVER);
		data->temp_hyst =
		    lm78_read_value(client, LM78_REG_TEMP_HYST);
		i = lm78_read_value(client, LM78_REG_VID_FANDIV);
		data->vid = i & 0x0f;
		if (data->type == lm79)
			data->vid |=
			    (lm78_read_value(client, LM78_REG_CHIPID) &
			     0x01) << 4;
		else
			data->vid |= 0x10;
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = i >> 6;
		data->alarms = lm78_read_value(client, LM78_REG_ALARM1) +
		    (lm78_read_value(client, LM78_REG_ALARM2) << 8);
		data->last_updated = jiffies;
		data->valid = 1;

		data->fan_div[2] = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init sm_lm78_init(void)
{
	int res;

	res = i2c_add_driver(&lm78_driver);
	if (res)
		return res;

	res = i2c_isa_add_driver(&lm78_isa_driver);
	if (res) {
		i2c_del_driver(&lm78_driver);
		return res;
	}

	return 0;
}

static void __exit sm_lm78_exit(void)
{
	i2c_isa_del_driver(&lm78_isa_driver);
	i2c_del_driver(&lm78_driver);
}



MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>");
MODULE_DESCRIPTION("LM78/LM79 driver");
MODULE_LICENSE("GPL");

module_init(sm_lm78_init);
module_exit(sm_lm78_exit);
