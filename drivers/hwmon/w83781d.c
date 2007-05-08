/*
    w83781d.c - Part of lm_sensors, Linux kernel modules for hardware
                monitoring
    Copyright (c) 1998 - 2001  Frodo Looijaard <frodol@dds.nl>,
                               Philip Edelbrock <phil@netroedge.com>,
                               and Mark Studebaker <mdsxyz123@yahoo.com>
    Copyright (c) 2007         Jean Delvare <khali@linux-fr.org>

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
    Supports following chips:

    Chip	#vin	#fanin	#pwm	#temp	wchipid	vendid	i2c	ISA
    as99127f	7	3	0	3	0x31	0x12c3	yes	no
    as99127f rev.2 (type_name = as99127f)	0x31	0x5ca3	yes	no
    w83781d	7	3	0	3	0x10-1	0x5ca3	yes	yes
    w83627hf	9	3	2	3	0x21	0x5ca3	yes	yes(LPC)
    w83782d	9	3	2-4	3	0x30	0x5ca3	yes	yes
    w83783s	5-6	3	2	1-2	0x40	0x5ca3	yes	no

*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/hwmon-sysfs.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <asm/io.h>
#include "lm75.h"

/* ISA device, if found */
static struct platform_device *pdev;

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
					0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
					0x2c, 0x2d, 0x2e, 0x2f, I2C_CLIENT_END };
static unsigned short isa_address = 0x290;

/* Insmod parameters */
I2C_CLIENT_INSMOD_5(w83781d, w83782d, w83783s, w83627hf, as99127f);
I2C_CLIENT_MODULE_PARM(force_subclients, "List of subclient addresses: "
		    "{bus, clientaddr, subclientaddr1, subclientaddr2}");

static int reset;
module_param(reset, bool, 0);
MODULE_PARM_DESC(reset, "Set to one to reset chip on load");

static int init = 1;
module_param(init, bool, 0);
MODULE_PARM_DESC(init, "Set to zero to bypass chip initialization");

/* Constants specified below */

/* Length of ISA address segment */
#define W83781D_EXTENT			8

/* Where are the ISA address/data registers relative to the base address */
#define W83781D_ADDR_REG_OFFSET		5
#define W83781D_DATA_REG_OFFSET		6

/* The device registers */
/* in nr from 0 to 8 */
#define W83781D_REG_IN_MAX(nr)		((nr < 7) ? (0x2b + (nr) * 2) : \
						    (0x554 + (((nr) - 7) * 2)))
#define W83781D_REG_IN_MIN(nr)		((nr < 7) ? (0x2c + (nr) * 2) : \
						    (0x555 + (((nr) - 7) * 2)))
#define W83781D_REG_IN(nr)		((nr < 7) ? (0x20 + (nr)) : \
						    (0x550 + (nr) - 7))

/* fan nr from 0 to 2 */
#define W83781D_REG_FAN_MIN(nr)		(0x3b + (nr))
#define W83781D_REG_FAN(nr)		(0x28 + (nr))

#define W83781D_REG_BANK		0x4E
#define W83781D_REG_TEMP2_CONFIG	0x152
#define W83781D_REG_TEMP3_CONFIG	0x252
/* temp nr from 1 to 3 */
#define W83781D_REG_TEMP(nr)		((nr == 3) ? (0x0250) : \
					((nr == 2) ? (0x0150) : \
						     (0x27)))
#define W83781D_REG_TEMP_HYST(nr)	((nr == 3) ? (0x253) : \
					((nr == 2) ? (0x153) : \
						     (0x3A)))
#define W83781D_REG_TEMP_OVER(nr)	((nr == 3) ? (0x255) : \
					((nr == 2) ? (0x155) : \
						     (0x39)))

#define W83781D_REG_CONFIG		0x40

/* Interrupt status (W83781D, AS99127F) */
#define W83781D_REG_ALARM1		0x41
#define W83781D_REG_ALARM2		0x42

/* Real-time status (W83782D, W83783S, W83627HF) */
#define W83782D_REG_ALARM1		0x459
#define W83782D_REG_ALARM2		0x45A
#define W83782D_REG_ALARM3		0x45B

#define W83781D_REG_BEEP_CONFIG		0x4D
#define W83781D_REG_BEEP_INTS1		0x56
#define W83781D_REG_BEEP_INTS2		0x57
#define W83781D_REG_BEEP_INTS3		0x453	/* not on W83781D */

#define W83781D_REG_VID_FANDIV		0x47

#define W83781D_REG_CHIPID		0x49
#define W83781D_REG_WCHIPID		0x58
#define W83781D_REG_CHIPMAN		0x4F
#define W83781D_REG_PIN			0x4B

/* 782D/783S only */
#define W83781D_REG_VBAT		0x5D

/* PWM 782D (1-4) and 783S (1-2) only */
static const u8 W83781D_REG_PWM[] = { 0x5B, 0x5A, 0x5E, 0x5F };
#define W83781D_REG_PWMCLK12		0x5C
#define W83781D_REG_PWMCLK34		0x45C

#define W83781D_REG_I2C_ADDR		0x48
#define W83781D_REG_I2C_SUBADDR		0x4A

/* The following are undocumented in the data sheets however we
   received the information in an email from Winbond tech support */
/* Sensor selection - not on 781d */
#define W83781D_REG_SCFG1		0x5D
static const u8 BIT_SCFG1[] = { 0x02, 0x04, 0x08 };

#define W83781D_REG_SCFG2		0x59
static const u8 BIT_SCFG2[] = { 0x10, 0x20, 0x40 };

#define W83781D_DEFAULT_BETA		3435

/* RT Table registers */
#define W83781D_REG_RT_IDX		0x50
#define W83781D_REG_RT_VAL		0x51

/* Conversions */
#define IN_TO_REG(val)			SENSORS_LIMIT(((val) + 8) / 16, 0, 255)
#define IN_FROM_REG(val)		((val) * 16)

static inline u8
FAN_TO_REG(long rpm, int div)
{
	if (rpm == 0)
		return 255;
	rpm = SENSORS_LIMIT(rpm, 1, 1000000);
	return SENSORS_LIMIT((1350000 + rpm * div / 2) / (rpm * div), 1, 254);
}

static inline long
FAN_FROM_REG(u8 val, int div)
{
	if (val == 0)
		return -1;
	if (val == 255)
		return 0;
	return 1350000 / (val * div);
}

#define TEMP_TO_REG(val)		SENSORS_LIMIT((val) / 1000, -127, 128)
#define TEMP_FROM_REG(val)		((val) * 1000)

#define BEEP_MASK_FROM_REG(val,type)	((type) == as99127f ? \
					 (val) ^ 0x7fff : (val))
#define BEEP_MASK_TO_REG(val,type)	((type) == as99127f ? \
					 (~(val)) & 0x7fff : (val) & 0xffffff)

#define DIV_FROM_REG(val)		(1 << (val))

static inline u8
DIV_TO_REG(long val, enum chips type)
{
	int i;
	val = SENSORS_LIMIT(val, 1,
			    ((type == w83781d
			      || type == as99127f) ? 8 : 128)) >> 1;
	for (i = 0; i < 7; i++) {
		if (val == 0)
			break;
		val >>= 1;
	}
	return i;
}

/* There are some complications in a module like this. First off, W83781D chips
   may be both present on the SMBus and the ISA bus, and we have to handle
   those cases separately at some places. Second, there might be several
   W83781D chips available (well, actually, that is probably never done; but
   it is a clean illustration of how to handle a case like that). Finally,
   a specific chip may be attached to *both* ISA and SMBus, and we would
   not like to detect it double. Fortunately, in the case of the W83781D at
   least, a register tells us what SMBus address we are on, so that helps
   a bit - except if there could be more than one SMBus. Groan. No solution
   for this yet. */

/* For ISA chips, we abuse the i2c_client addr and name fields. We also use
   the driver field to differentiate between I2C and ISA chips. */
struct w83781d_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct mutex lock;
	enum chips type;

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	struct i2c_client *lm75[2];	/* for secondary I2C addresses */
	/* array of 2 pointers to subclients */

	u8 in[9];		/* Register value - 8 & 9 for 782D only */
	u8 in_max[9];		/* Register value - 8 & 9 for 782D only */
	u8 in_min[9];		/* Register value - 8 & 9 for 782D only */
	u8 fan[3];		/* Register value */
	u8 fan_min[3];		/* Register value */
	s8 temp;		/* Register value */
	s8 temp_max;		/* Register value */
	s8 temp_max_hyst;	/* Register value */
	u16 temp_add[2];	/* Register value */
	u16 temp_max_add[2];	/* Register value */
	u16 temp_max_hyst_add[2];	/* Register value */
	u8 fan_div[3];		/* Register encoding, shifted right */
	u8 vid;			/* Register encoding, combined */
	u32 alarms;		/* Register encoding, combined */
	u32 beep_mask;		/* Register encoding, combined */
	u8 beep_enable;		/* Boolean */
	u8 pwm[4];		/* Register value */
	u8 pwm2_enable;		/* Boolean */
	u16 sens[3];		/* 782D/783S only.
				   1 = pentium diode; 2 = 3904 diode;
				   3000-5000 = thermistor beta.
				   Default = 3435. 
				   Other Betas unimplemented */
	u8 vrm;
};

static int w83781d_attach_adapter(struct i2c_adapter *adapter);
static int w83781d_detect(struct i2c_adapter *adapter, int address, int kind);
static int w83781d_detach_client(struct i2c_client *client);

static int __devinit w83781d_isa_probe(struct platform_device *pdev);
static int __devexit w83781d_isa_remove(struct platform_device *pdev);

static int w83781d_read_value(struct w83781d_data *data, u16 reg);
static int w83781d_write_value(struct w83781d_data *data, u16 reg, u16 value);
static struct w83781d_data *w83781d_update_device(struct device *dev);
static void w83781d_init_device(struct device *dev);

static struct i2c_driver w83781d_driver = {
	.driver = {
		.name = "w83781d",
	},
	.id = I2C_DRIVERID_W83781D,
	.attach_adapter = w83781d_attach_adapter,
	.detach_client = w83781d_detach_client,
};

static struct platform_driver w83781d_isa_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "w83781d",
	},
	.probe = w83781d_isa_probe,
	.remove = w83781d_isa_remove,
};


/* following are the sysfs callback functions */
#define show_in_reg(reg) \
static ssize_t show_##reg (struct device *dev, struct device_attribute *da, \
		char *buf) \
{ \
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da); \
	struct w83781d_data *data = w83781d_update_device(dev); \
	return sprintf(buf, "%ld\n", \
		       (long)IN_FROM_REG(data->reg[attr->index])); \
}
show_in_reg(in);
show_in_reg(in_min);
show_in_reg(in_max);

#define store_in_reg(REG, reg) \
static ssize_t store_in_##reg (struct device *dev, struct device_attribute \
		*da, const char *buf, size_t count) \
{ \
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da); \
	struct w83781d_data *data = dev_get_drvdata(dev); \
	int nr = attr->index; \
	u32 val; \
	 \
	val = simple_strtoul(buf, NULL, 10); \
	 \
	mutex_lock(&data->update_lock); \
	data->in_##reg[nr] = IN_TO_REG(val); \
	w83781d_write_value(data, W83781D_REG_IN_##REG(nr), data->in_##reg[nr]); \
	 \
	mutex_unlock(&data->update_lock); \
	return count; \
}
store_in_reg(MIN, min);
store_in_reg(MAX, max);

#define sysfs_in_offsets(offset) \
static SENSOR_DEVICE_ATTR(in##offset##_input, S_IRUGO, \
		show_in, NULL, offset); \
static SENSOR_DEVICE_ATTR(in##offset##_min, S_IRUGO | S_IWUSR, \
		show_in_min, store_in_min, offset); \
static SENSOR_DEVICE_ATTR(in##offset##_max, S_IRUGO | S_IWUSR, \
		show_in_max, store_in_max, offset)

sysfs_in_offsets(0);
sysfs_in_offsets(1);
sysfs_in_offsets(2);
sysfs_in_offsets(3);
sysfs_in_offsets(4);
sysfs_in_offsets(5);
sysfs_in_offsets(6);
sysfs_in_offsets(7);
sysfs_in_offsets(8);

#define show_fan_reg(reg) \
static ssize_t show_##reg (struct device *dev, struct device_attribute *da, \
		char *buf) \
{ \
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da); \
	struct w83781d_data *data = w83781d_update_device(dev); \
	return sprintf(buf,"%ld\n", \
		FAN_FROM_REG(data->reg[attr->index], \
			DIV_FROM_REG(data->fan_div[attr->index]))); \
}
show_fan_reg(fan);
show_fan_reg(fan_min);

static ssize_t
store_fan_min(struct device *dev, struct device_attribute *da,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct w83781d_data *data = dev_get_drvdata(dev);
	int nr = attr->index;
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->fan_min[nr] =
	    FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	w83781d_write_value(data, W83781D_REG_FAN_MIN(nr),
			    data->fan_min[nr]);

	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_min, S_IRUGO | S_IWUSR,
		show_fan_min, store_fan_min, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1);
static SENSOR_DEVICE_ATTR(fan2_min, S_IRUGO | S_IWUSR,
		show_fan_min, store_fan_min, 1);
static SENSOR_DEVICE_ATTR(fan3_input, S_IRUGO, show_fan, NULL, 2);
static SENSOR_DEVICE_ATTR(fan3_min, S_IRUGO | S_IWUSR,
		show_fan_min, store_fan_min, 2);

#define show_temp_reg(reg) \
static ssize_t show_##reg (struct device *dev, struct device_attribute *da, \
		char *buf) \
{ \
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da); \
	struct w83781d_data *data = w83781d_update_device(dev); \
	int nr = attr->index; \
	if (nr >= 2) {	/* TEMP2 and TEMP3 */ \
		return sprintf(buf,"%d\n", \
			LM75_TEMP_FROM_REG(data->reg##_add[nr-2])); \
	} else {	/* TEMP1 */ \
		return sprintf(buf,"%ld\n", (long)TEMP_FROM_REG(data->reg)); \
	} \
}
show_temp_reg(temp);
show_temp_reg(temp_max);
show_temp_reg(temp_max_hyst);

#define store_temp_reg(REG, reg) \
static ssize_t store_temp_##reg (struct device *dev, \
		struct device_attribute *da, const char *buf, size_t count) \
{ \
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da); \
	struct w83781d_data *data = dev_get_drvdata(dev); \
	int nr = attr->index; \
	s32 val; \
	 \
	val = simple_strtol(buf, NULL, 10); \
	 \
	mutex_lock(&data->update_lock); \
	 \
	if (nr >= 2) {	/* TEMP2 and TEMP3 */ \
		data->temp_##reg##_add[nr-2] = LM75_TEMP_TO_REG(val); \
		w83781d_write_value(data, W83781D_REG_TEMP_##REG(nr), \
				data->temp_##reg##_add[nr-2]); \
	} else {	/* TEMP1 */ \
		data->temp_##reg = TEMP_TO_REG(val); \
		w83781d_write_value(data, W83781D_REG_TEMP_##REG(nr), \
			data->temp_##reg); \
	} \
	 \
	mutex_unlock(&data->update_lock); \
	return count; \
}
store_temp_reg(OVER, max);
store_temp_reg(HYST, max_hyst);

#define sysfs_temp_offsets(offset) \
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO, \
		show_temp, NULL, offset); \
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IRUGO | S_IWUSR, \
		show_temp_max, store_temp_max, offset); \
static SENSOR_DEVICE_ATTR(temp##offset##_max_hyst, S_IRUGO | S_IWUSR, \
		show_temp_max_hyst, store_temp_max_hyst, offset);

sysfs_temp_offsets(1);
sysfs_temp_offsets(2);
sysfs_temp_offsets(3);

static ssize_t
show_vid_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n", (long) vid_from_reg(data->vid, data->vrm));
}

static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid_reg, NULL);

static ssize_t
show_vrm_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->vrm);
}

static ssize_t
store_vrm_reg(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct w83781d_data *data = dev_get_drvdata(dev);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);
	data->vrm = val;

	return count;
}

static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm_reg, store_vrm_reg);

static ssize_t
show_alarms_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms_reg, NULL);

static ssize_t show_beep_mask (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n",
		       (long)BEEP_MASK_FROM_REG(data->beep_mask, data->type));
}
static ssize_t show_beep_enable (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n", (long)data->beep_enable);
}

static ssize_t
store_beep_mask(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct w83781d_data *data = dev_get_drvdata(dev);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->beep_mask = BEEP_MASK_TO_REG(val, data->type);
	w83781d_write_value(data, W83781D_REG_BEEP_INTS1,
			    data->beep_mask & 0xff);
	w83781d_write_value(data, W83781D_REG_BEEP_INTS2,
			    ((data->beep_mask >> 8) & 0x7f)
			    | data->beep_enable << 7);
	if (data->type != w83781d && data->type != as99127f) {
		w83781d_write_value(data, W83781D_REG_BEEP_INTS3,
				    ((data->beep_mask) >> 16) & 0xff);
	}
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
store_beep_enable(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct w83781d_data *data = dev_get_drvdata(dev);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);
	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->beep_enable = val;
	val = w83781d_read_value(data, W83781D_REG_BEEP_INTS2) & 0x7f;
	val |= data->beep_enable << 7;
	w83781d_write_value(data, W83781D_REG_BEEP_INTS2, val);
	mutex_unlock(&data->update_lock);

	return count;
}

static DEVICE_ATTR(beep_mask, S_IRUGO | S_IWUSR,
		show_beep_mask, store_beep_mask);
static DEVICE_ATTR(beep_enable, S_IRUGO | S_IWUSR,
		show_beep_enable, store_beep_enable);

static ssize_t
show_fan_div(struct device *dev, struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n",
		       (long) DIV_FROM_REG(data->fan_div[attr->index]));
}

/* Note: we save and restore the fan minimum here, because its value is
   determined in part by the fan divisor.  This follows the principle of
   least surprise; the user doesn't expect the fan minimum to change just
   because the divisor changed. */
static ssize_t
store_fan_div(struct device *dev, struct device_attribute *da,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct w83781d_data *data = dev_get_drvdata(dev);
	unsigned long min;
	int nr = attr->index;
	u8 reg;
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	
	/* Save fan_min */
	min = FAN_FROM_REG(data->fan_min[nr],
			   DIV_FROM_REG(data->fan_div[nr]));

	data->fan_div[nr] = DIV_TO_REG(val, data->type);

	reg = (w83781d_read_value(data, nr==2 ? W83781D_REG_PIN : W83781D_REG_VID_FANDIV)
	       & (nr==0 ? 0xcf : 0x3f))
	    | ((data->fan_div[nr] & 0x03) << (nr==0 ? 4 : 6));
	w83781d_write_value(data, nr==2 ? W83781D_REG_PIN : W83781D_REG_VID_FANDIV, reg);

	/* w83781d and as99127f don't have extended divisor bits */
	if (data->type != w83781d && data->type != as99127f) {
		reg = (w83781d_read_value(data, W83781D_REG_VBAT)
		       & ~(1 << (5 + nr)))
		    | ((data->fan_div[nr] & 0x04) << (3 + nr));
		w83781d_write_value(data, W83781D_REG_VBAT, reg);
	}

	/* Restore fan_min */
	data->fan_min[nr] = FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	w83781d_write_value(data, W83781D_REG_FAN_MIN(nr), data->fan_min[nr]);

	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(fan1_div, S_IRUGO | S_IWUSR,
		show_fan_div, store_fan_div, 0);
static SENSOR_DEVICE_ATTR(fan2_div, S_IRUGO | S_IWUSR,
		show_fan_div, store_fan_div, 1);
static SENSOR_DEVICE_ATTR(fan3_div, S_IRUGO | S_IWUSR,
		show_fan_div, store_fan_div, 2);

static ssize_t
show_pwm(struct device *dev, struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%d\n", (int)data->pwm[attr->index]);
}

static ssize_t
show_pwm2_enable(struct device *dev, struct device_attribute *da, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%d\n", (int)data->pwm2_enable);
}

static ssize_t
store_pwm(struct device *dev, struct device_attribute *da, const char *buf,
		size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct w83781d_data *data = dev_get_drvdata(dev);
	int nr = attr->index;
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->pwm[nr] = SENSORS_LIMIT(val, 0, 255);
	w83781d_write_value(data, W83781D_REG_PWM[nr], data->pwm[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
store_pwm2_enable(struct device *dev, struct device_attribute *da,
		const char *buf, size_t count)
{
	struct w83781d_data *data = dev_get_drvdata(dev);
	u32 val, reg;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);

	switch (val) {
	case 0:
	case 1:
		reg = w83781d_read_value(data, W83781D_REG_PWMCLK12);
		w83781d_write_value(data, W83781D_REG_PWMCLK12,
				    (reg & 0xf7) | (val << 3));

		reg = w83781d_read_value(data, W83781D_REG_BEEP_CONFIG);
		w83781d_write_value(data, W83781D_REG_BEEP_CONFIG,
				    (reg & 0xef) | (!val << 4));

		data->pwm2_enable = val;
		break;

	default:
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, show_pwm, store_pwm, 0);
static SENSOR_DEVICE_ATTR(pwm2, S_IRUGO | S_IWUSR, show_pwm, store_pwm, 1);
static SENSOR_DEVICE_ATTR(pwm3, S_IRUGO | S_IWUSR, show_pwm, store_pwm, 2);
static SENSOR_DEVICE_ATTR(pwm4, S_IRUGO | S_IWUSR, show_pwm, store_pwm, 3);
/* only PWM2 can be enabled/disabled */
static DEVICE_ATTR(pwm2_enable, S_IRUGO | S_IWUSR,
		show_pwm2_enable, store_pwm2_enable);

static ssize_t
show_sensor(struct device *dev, struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%d\n", (int)data->sens[attr->index]);
}

static ssize_t
store_sensor(struct device *dev, struct device_attribute *da,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct w83781d_data *data = dev_get_drvdata(dev);
	int nr = attr->index;
	u32 val, tmp;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);

	switch (val) {
	case 1:		/* PII/Celeron diode */
		tmp = w83781d_read_value(data, W83781D_REG_SCFG1);
		w83781d_write_value(data, W83781D_REG_SCFG1,
				    tmp | BIT_SCFG1[nr]);
		tmp = w83781d_read_value(data, W83781D_REG_SCFG2);
		w83781d_write_value(data, W83781D_REG_SCFG2,
				    tmp | BIT_SCFG2[nr]);
		data->sens[nr] = val;
		break;
	case 2:		/* 3904 */
		tmp = w83781d_read_value(data, W83781D_REG_SCFG1);
		w83781d_write_value(data, W83781D_REG_SCFG1,
				    tmp | BIT_SCFG1[nr]);
		tmp = w83781d_read_value(data, W83781D_REG_SCFG2);
		w83781d_write_value(data, W83781D_REG_SCFG2,
				    tmp & ~BIT_SCFG2[nr]);
		data->sens[nr] = val;
		break;
	case W83781D_DEFAULT_BETA:	/* thermistor */
		tmp = w83781d_read_value(data, W83781D_REG_SCFG1);
		w83781d_write_value(data, W83781D_REG_SCFG1,
				    tmp & ~BIT_SCFG1[nr]);
		data->sens[nr] = val;
		break;
	default:
		dev_err(dev, "Invalid sensor type %ld; must be 1, 2, or %d\n",
		       (long) val, W83781D_DEFAULT_BETA);
		break;
	}

	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(temp1_type, S_IRUGO | S_IWUSR,
	show_sensor, store_sensor, 0);
static SENSOR_DEVICE_ATTR(temp2_type, S_IRUGO | S_IWUSR,
	show_sensor, store_sensor, 0);
static SENSOR_DEVICE_ATTR(temp3_type, S_IRUGO | S_IWUSR,
	show_sensor, store_sensor, 0);

/* I2C devices get this name attribute automatically, but for ISA devices
   we must create it by ourselves. */
static ssize_t
show_name(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct w83781d_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", data->client.name);
}
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

/* This function is called when:
     * w83781d_driver is inserted (when this module is loaded), for each
       available adapter
     * when a new adapter is inserted (and w83781d_driver is still present) */
static int
w83781d_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, w83781d_detect);
}

/* Assumes that adapter is of I2C, not ISA variety.
 * OTHERWISE DON'T CALL THIS
 */
static int
w83781d_detect_subclients(struct i2c_adapter *adapter, int address, int kind,
		struct i2c_client *new_client)
{
	int i, val1 = 0, id;
	int err;
	const char *client_name = "";
	struct w83781d_data *data = i2c_get_clientdata(new_client);

	data->lm75[0] = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!(data->lm75[0])) {
		err = -ENOMEM;
		goto ERROR_SC_0;
	}

	id = i2c_adapter_id(adapter);

	if (force_subclients[0] == id && force_subclients[1] == address) {
		for (i = 2; i <= 3; i++) {
			if (force_subclients[i] < 0x48 ||
			    force_subclients[i] > 0x4f) {
				dev_err(&new_client->dev, "Invalid subclient "
					"address %d; must be 0x48-0x4f\n",
					force_subclients[i]);
				err = -EINVAL;
				goto ERROR_SC_1;
			}
		}
		w83781d_write_value(data, W83781D_REG_I2C_SUBADDR,
				(force_subclients[2] & 0x07) |
				((force_subclients[3] & 0x07) << 4));
		data->lm75[0]->addr = force_subclients[2];
	} else {
		val1 = w83781d_read_value(data, W83781D_REG_I2C_SUBADDR);
		data->lm75[0]->addr = 0x48 + (val1 & 0x07);
	}

	if (kind != w83783s) {
		data->lm75[1] = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
		if (!(data->lm75[1])) {
			err = -ENOMEM;
			goto ERROR_SC_1;
		}

		if (force_subclients[0] == id &&
		    force_subclients[1] == address) {
			data->lm75[1]->addr = force_subclients[3];
		} else {
			data->lm75[1]->addr = 0x48 + ((val1 >> 4) & 0x07);
		}
		if (data->lm75[0]->addr == data->lm75[1]->addr) {
			dev_err(&new_client->dev,
			       "Duplicate addresses 0x%x for subclients.\n",
			       data->lm75[0]->addr);
			err = -EBUSY;
			goto ERROR_SC_2;
		}
	}

	if (kind == w83781d)
		client_name = "w83781d subclient";
	else if (kind == w83782d)
		client_name = "w83782d subclient";
	else if (kind == w83783s)
		client_name = "w83783s subclient";
	else if (kind == w83627hf)
		client_name = "w83627hf subclient";
	else if (kind == as99127f)
		client_name = "as99127f subclient";

	for (i = 0; i <= 1; i++) {
		/* store all data in w83781d */
		i2c_set_clientdata(data->lm75[i], NULL);
		data->lm75[i]->adapter = adapter;
		data->lm75[i]->driver = &w83781d_driver;
		data->lm75[i]->flags = 0;
		strlcpy(data->lm75[i]->name, client_name,
			I2C_NAME_SIZE);
		if ((err = i2c_attach_client(data->lm75[i]))) {
			dev_err(&new_client->dev, "Subclient %d "
				"registration at address 0x%x "
				"failed.\n", i, data->lm75[i]->addr);
			if (i == 1)
				goto ERROR_SC_3;
			goto ERROR_SC_2;
		}
		if (kind == w83783s)
			break;
	}

	return 0;

/* Undo inits in case of errors */
ERROR_SC_3:
	i2c_detach_client(data->lm75[0]);
ERROR_SC_2:
	kfree(data->lm75[1]);
ERROR_SC_1:
	kfree(data->lm75[0]);
ERROR_SC_0:
	return err;
}

#define IN_UNIT_ATTRS(X)					\
	&sensor_dev_attr_in##X##_input.dev_attr.attr,		\
	&sensor_dev_attr_in##X##_min.dev_attr.attr,		\
	&sensor_dev_attr_in##X##_max.dev_attr.attr

#define FAN_UNIT_ATTRS(X)					\
	&sensor_dev_attr_fan##X##_input.dev_attr.attr,		\
	&sensor_dev_attr_fan##X##_min.dev_attr.attr,		\
	&sensor_dev_attr_fan##X##_div.dev_attr.attr

#define TEMP_UNIT_ATTRS(X)					\
	&sensor_dev_attr_temp##X##_input.dev_attr.attr,		\
	&sensor_dev_attr_temp##X##_max.dev_attr.attr,		\
	&sensor_dev_attr_temp##X##_max_hyst.dev_attr.attr

static struct attribute* w83781d_attributes[] = {
	IN_UNIT_ATTRS(0),
	IN_UNIT_ATTRS(2),
	IN_UNIT_ATTRS(3),
	IN_UNIT_ATTRS(4),
	IN_UNIT_ATTRS(5),
	IN_UNIT_ATTRS(6),
	FAN_UNIT_ATTRS(1),
	FAN_UNIT_ATTRS(2),
	FAN_UNIT_ATTRS(3),
	TEMP_UNIT_ATTRS(1),
	TEMP_UNIT_ATTRS(2),
	&dev_attr_cpu0_vid.attr,
	&dev_attr_vrm.attr,
	&dev_attr_alarms.attr,
	&dev_attr_beep_mask.attr,
	&dev_attr_beep_enable.attr,
	NULL
};
static const struct attribute_group w83781d_group = {
	.attrs = w83781d_attributes,
};

static struct attribute *w83781d_attributes_opt[] = {
	IN_UNIT_ATTRS(1),
	IN_UNIT_ATTRS(7),
	IN_UNIT_ATTRS(8),
	TEMP_UNIT_ATTRS(3),
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm4.dev_attr.attr,
	&dev_attr_pwm2_enable.attr,
	&sensor_dev_attr_temp1_type.dev_attr.attr,
	&sensor_dev_attr_temp2_type.dev_attr.attr,
	&sensor_dev_attr_temp3_type.dev_attr.attr,
	NULL
};
static const struct attribute_group w83781d_group_opt = {
	.attrs = w83781d_attributes_opt,
};

/* No clean up is done on error, it's up to the caller */
static int
w83781d_create_files(struct device *dev, int kind, int is_isa)
{
	int err;

	if ((err = sysfs_create_group(&dev->kobj, &w83781d_group)))
		return err;

	if (kind != w83783s) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_in1_input.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_in1_min.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_in1_max.dev_attr)))
			return err;
	}
	if (kind != as99127f && kind != w83781d && kind != w83783s) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_in7_input.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_in7_min.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_in7_max.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_in8_input.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_in8_min.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_in8_max.dev_attr)))
			return err;
	}
	if (kind != w83783s) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_temp3_input.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_temp3_max.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_temp3_max_hyst.dev_attr)))
			return err;
	}

	if (kind != w83781d && kind != as99127f) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_pwm1.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_pwm2.dev_attr))
		    || (err = device_create_file(dev, &dev_attr_pwm2_enable)))
			return err;
	}
	if (kind == w83782d && !is_isa) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_pwm3.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_pwm4.dev_attr)))
			return err;
	}

	if (kind != as99127f && kind != w83781d) {
		if ((err = device_create_file(dev,
				&sensor_dev_attr_temp1_type.dev_attr))
		    || (err = device_create_file(dev,
				&sensor_dev_attr_temp2_type.dev_attr)))
			return err;
		if (kind != w83783s) {
			if ((err = device_create_file(dev,
					&sensor_dev_attr_temp3_type.dev_attr)))
				return err;
		}
	}

	if (is_isa) {
		err = device_create_file(&pdev->dev, &dev_attr_name);
		if (err)
			return err;
	}

	return 0;
}

static int
w83781d_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int val1 = 0, val2;
	struct i2c_client *client;
	struct device *dev;
	struct w83781d_data *data;
	int err;
	const char *client_name = "";
	enum vendor { winbond, asus } vendid;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		err = -EINVAL;
		goto ERROR1;
	}

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access w83781d_{read,write}_value. */

	if (!(data = kzalloc(sizeof(struct w83781d_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR1;
	}

	client = &data->client;
	i2c_set_clientdata(client, data);
	client->addr = address;
	mutex_init(&data->lock);
	client->adapter = adapter;
	client->driver = &w83781d_driver;
	dev = &client->dev;

	/* Now, we do the remaining detection. */

	/* The w8378?d may be stuck in some other bank than bank 0. This may
	   make reading other information impossible. Specify a force=... or
	   force_*=... parameter, and the Winbond will be reset to the right
	   bank. */
	if (kind < 0) {
		if (w83781d_read_value(data, W83781D_REG_CONFIG) & 0x80) {
			dev_dbg(&adapter->dev, "Detection of w83781d chip "
				"failed at step 3\n");
			err = -ENODEV;
			goto ERROR2;
		}
		val1 = w83781d_read_value(data, W83781D_REG_BANK);
		val2 = w83781d_read_value(data, W83781D_REG_CHIPMAN);
		/* Check for Winbond or Asus ID if in bank 0 */
		if ((!(val1 & 0x07)) &&
		    (((!(val1 & 0x80)) && (val2 != 0xa3) && (val2 != 0xc3))
		     || ((val1 & 0x80) && (val2 != 0x5c) && (val2 != 0x12)))) {
			dev_dbg(&adapter->dev, "Detection of w83781d chip "
				"failed at step 4\n");
			err = -ENODEV;
			goto ERROR2;
		}
		/* If Winbond SMBus, check address at 0x48.
		   Asus doesn't support, except for as99127f rev.2 */
		if ((!(val1 & 0x80) && (val2 == 0xa3)) ||
		    ((val1 & 0x80) && (val2 == 0x5c))) {
			if (w83781d_read_value
			    (data, W83781D_REG_I2C_ADDR) != address) {
				dev_dbg(&adapter->dev, "Detection of w83781d "
					"chip failed at step 5\n");
				err = -ENODEV;
				goto ERROR2;
			}
		}
	}

	/* We have either had a force parameter, or we have already detected the
	   Winbond. Put it now into bank 0 and Vendor ID High Byte */
	w83781d_write_value(data, W83781D_REG_BANK,
			    (w83781d_read_value(data, W83781D_REG_BANK)
			     & 0x78) | 0x80);

	/* Determine the chip type. */
	if (kind <= 0) {
		/* get vendor ID */
		val2 = w83781d_read_value(data, W83781D_REG_CHIPMAN);
		if (val2 == 0x5c)
			vendid = winbond;
		else if (val2 == 0x12)
			vendid = asus;
		else {
			dev_dbg(&adapter->dev, "w83781d chip vendor is "
				"neither Winbond nor Asus\n");
			err = -ENODEV;
			goto ERROR2;
		}

		val1 = w83781d_read_value(data, W83781D_REG_WCHIPID);
		if ((val1 == 0x10 || val1 == 0x11) && vendid == winbond)
			kind = w83781d;
		else if (val1 == 0x30 && vendid == winbond)
			kind = w83782d;
		else if (val1 == 0x40 && vendid == winbond && address == 0x2d)
			kind = w83783s;
		else if (val1 == 0x21 && vendid == winbond)
			kind = w83627hf;
		else if (val1 == 0x31 && address >= 0x28)
			kind = as99127f;
		else {
			if (kind == 0)
				dev_warn(&adapter->dev, "Ignoring 'force' "
					 "parameter for unknown chip at "
					 "address 0x%02x\n", address);
			err = -EINVAL;
			goto ERROR2;
		}
	}

	if (kind == w83781d) {
		client_name = "w83781d";
	} else if (kind == w83782d) {
		client_name = "w83782d";
	} else if (kind == w83783s) {
		client_name = "w83783s";
	} else if (kind == w83627hf) {
		client_name = "w83627hf";
	} else if (kind == as99127f) {
		client_name = "as99127f";
	}

	/* Fill in the remaining client fields and put into the global list */
	strlcpy(client->name, client_name, I2C_NAME_SIZE);
	data->type = kind;

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(client)))
		goto ERROR2;

	/* attach secondary i2c lm75-like clients */
	if ((err = w83781d_detect_subclients(adapter, address,
			kind, client)))
		goto ERROR3;

	/* Initialize the chip */
	w83781d_init_device(dev);

	/* Register sysfs hooks */
	err = w83781d_create_files(dev, kind, 0);
	if (err)
		goto ERROR4;

	data->class_dev = hwmon_device_register(dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto ERROR4;
	}

	return 0;

ERROR4:
	sysfs_remove_group(&dev->kobj, &w83781d_group);
	sysfs_remove_group(&dev->kobj, &w83781d_group_opt);

	if (data->lm75[1]) {
		i2c_detach_client(data->lm75[1]);
		kfree(data->lm75[1]);
	}
	if (data->lm75[0]) {
		i2c_detach_client(data->lm75[0]);
		kfree(data->lm75[0]);
	}
ERROR3:
	i2c_detach_client(client);
ERROR2:
	kfree(data);
ERROR1:
	return err;
}

static int
w83781d_detach_client(struct i2c_client *client)
{
	struct w83781d_data *data = i2c_get_clientdata(client);
	int err;

	/* main client */
	if (data) {
		hwmon_device_unregister(data->class_dev);
		sysfs_remove_group(&client->dev.kobj, &w83781d_group);
		sysfs_remove_group(&client->dev.kobj, &w83781d_group_opt);
	}

	if ((err = i2c_detach_client(client)))
		return err;

	/* main client */
	if (data)
		kfree(data);

	/* subclient */
	else
		kfree(client);

	return 0;
}

static int __devinit
w83781d_isa_probe(struct platform_device *pdev)
{
	int err, reg;
	struct w83781d_data *data;
	struct resource *res;
	const char *name;

	/* Reserve the ISA region */
	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!request_region(res->start, W83781D_EXTENT, "w83781d")) {
		err = -EBUSY;
		goto exit;
	}

	if (!(data = kzalloc(sizeof(struct w83781d_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit_release_region;
	}
	mutex_init(&data->lock);
	data->client.addr = res->start;
	i2c_set_clientdata(&data->client, data);
	platform_set_drvdata(pdev, data);

	reg = w83781d_read_value(data, W83781D_REG_WCHIPID);
	switch (reg) {
	case 0x21:
		data->type = w83627hf;
		name = "w83627hf";
		break;
	case 0x30:
		data->type = w83782d;
		name = "w83782d";
		break;
	default:
		data->type = w83781d;
		name = "w83781d";
	}
	strlcpy(data->client.name, name, I2C_NAME_SIZE);

	/* Initialize the W83781D chip */
	w83781d_init_device(&pdev->dev);

	/* Register sysfs hooks */
	err = w83781d_create_files(&pdev->dev, data->type, 1);
	if (err)
		goto exit_remove_files;

	data->class_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_remove_files;
	}

	return 0;

 exit_remove_files:
	sysfs_remove_group(&pdev->dev.kobj, &w83781d_group);
	sysfs_remove_group(&pdev->dev.kobj, &w83781d_group_opt);
	device_remove_file(&pdev->dev, &dev_attr_name);
	kfree(data);
 exit_release_region:
	release_region(res->start, W83781D_EXTENT);
 exit:
	return err;
}

static int __devexit
w83781d_isa_remove(struct platform_device *pdev)
{
	struct w83781d_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->class_dev);
	sysfs_remove_group(&pdev->dev.kobj, &w83781d_group);
	sysfs_remove_group(&pdev->dev.kobj, &w83781d_group_opt);
	device_remove_file(&pdev->dev, &dev_attr_name);
	release_region(data->client.addr, W83781D_EXTENT);
	kfree(data);

	return 0;
}

/* The SMBus locks itself, usually, but nothing may access the Winbond between
   bank switches. ISA access must always be locked explicitly! 
   We ignore the W83781D BUSY flag at this moment - it could lead to deadlocks,
   would slow down the W83781D access and should not be necessary. 
   There are some ugly typecasts here, but the good news is - they should
   nowhere else be necessary! */
static int
w83781d_read_value(struct w83781d_data *data, u16 reg)
{
	struct i2c_client *client = &data->client;
	int res, word_sized, bank;
	struct i2c_client *cl;

	mutex_lock(&data->lock);
	if (!client->driver) { /* ISA device */
		word_sized = (((reg & 0xff00) == 0x100)
			      || ((reg & 0xff00) == 0x200))
		    && (((reg & 0x00ff) == 0x50)
			|| ((reg & 0x00ff) == 0x53)
			|| ((reg & 0x00ff) == 0x55));
		if (reg & 0xff00) {
			outb_p(W83781D_REG_BANK,
			       client->addr + W83781D_ADDR_REG_OFFSET);
			outb_p(reg >> 8,
			       client->addr + W83781D_DATA_REG_OFFSET);
		}
		outb_p(reg & 0xff, client->addr + W83781D_ADDR_REG_OFFSET);
		res = inb_p(client->addr + W83781D_DATA_REG_OFFSET);
		if (word_sized) {
			outb_p((reg & 0xff) + 1,
			       client->addr + W83781D_ADDR_REG_OFFSET);
			res =
			    (res << 8) + inb_p(client->addr +
					       W83781D_DATA_REG_OFFSET);
		}
		if (reg & 0xff00) {
			outb_p(W83781D_REG_BANK,
			       client->addr + W83781D_ADDR_REG_OFFSET);
			outb_p(0, client->addr + W83781D_DATA_REG_OFFSET);
		}
	} else {
		bank = (reg >> 8) & 0x0f;
		if (bank > 2)
			/* switch banks */
			i2c_smbus_write_byte_data(client, W83781D_REG_BANK,
						  bank);
		if (bank == 0 || bank > 2) {
			res = i2c_smbus_read_byte_data(client, reg & 0xff);
		} else {
			/* switch to subclient */
			cl = data->lm75[bank - 1];
			/* convert from ISA to LM75 I2C addresses */
			switch (reg & 0xff) {
			case 0x50:	/* TEMP */
				res = swab16(i2c_smbus_read_word_data(cl, 0));
				break;
			case 0x52:	/* CONFIG */
				res = i2c_smbus_read_byte_data(cl, 1);
				break;
			case 0x53:	/* HYST */
				res = swab16(i2c_smbus_read_word_data(cl, 2));
				break;
			case 0x55:	/* OVER */
			default:
				res = swab16(i2c_smbus_read_word_data(cl, 3));
				break;
			}
		}
		if (bank > 2)
			i2c_smbus_write_byte_data(client, W83781D_REG_BANK, 0);
	}
	mutex_unlock(&data->lock);
	return res;
}

static int
w83781d_write_value(struct w83781d_data *data, u16 reg, u16 value)
{
	struct i2c_client *client = &data->client;
	int word_sized, bank;
	struct i2c_client *cl;

	mutex_lock(&data->lock);
	if (!client->driver) { /* ISA device */
		word_sized = (((reg & 0xff00) == 0x100)
			      || ((reg & 0xff00) == 0x200))
		    && (((reg & 0x00ff) == 0x53)
			|| ((reg & 0x00ff) == 0x55));
		if (reg & 0xff00) {
			outb_p(W83781D_REG_BANK,
			       client->addr + W83781D_ADDR_REG_OFFSET);
			outb_p(reg >> 8,
			       client->addr + W83781D_DATA_REG_OFFSET);
		}
		outb_p(reg & 0xff, client->addr + W83781D_ADDR_REG_OFFSET);
		if (word_sized) {
			outb_p(value >> 8,
			       client->addr + W83781D_DATA_REG_OFFSET);
			outb_p((reg & 0xff) + 1,
			       client->addr + W83781D_ADDR_REG_OFFSET);
		}
		outb_p(value & 0xff, client->addr + W83781D_DATA_REG_OFFSET);
		if (reg & 0xff00) {
			outb_p(W83781D_REG_BANK,
			       client->addr + W83781D_ADDR_REG_OFFSET);
			outb_p(0, client->addr + W83781D_DATA_REG_OFFSET);
		}
	} else {
		bank = (reg >> 8) & 0x0f;
		if (bank > 2)
			/* switch banks */
			i2c_smbus_write_byte_data(client, W83781D_REG_BANK,
						  bank);
		if (bank == 0 || bank > 2) {
			i2c_smbus_write_byte_data(client, reg & 0xff,
						  value & 0xff);
		} else {
			/* switch to subclient */
			cl = data->lm75[bank - 1];
			/* convert from ISA to LM75 I2C addresses */
			switch (reg & 0xff) {
			case 0x52:	/* CONFIG */
				i2c_smbus_write_byte_data(cl, 1, value & 0xff);
				break;
			case 0x53:	/* HYST */
				i2c_smbus_write_word_data(cl, 2, swab16(value));
				break;
			case 0x55:	/* OVER */
				i2c_smbus_write_word_data(cl, 3, swab16(value));
				break;
			}
		}
		if (bank > 2)
			i2c_smbus_write_byte_data(client, W83781D_REG_BANK, 0);
	}
	mutex_unlock(&data->lock);
	return 0;
}

static void
w83781d_init_device(struct device *dev)
{
	struct w83781d_data *data = dev_get_drvdata(dev);
	int i, p;
	int type = data->type;
	u8 tmp;

	if (type == w83627hf)
		dev_info(dev, "The W83627HF chip is better supported by the "
			 "w83627hf driver, support will be dropped from the "
			 "w83781d driver soon\n");

	if (reset && type != as99127f) { /* this resets registers we don't have
					   documentation for on the as99127f */
		/* Resetting the chip has been the default for a long time,
		   but it causes the BIOS initializations (fan clock dividers,
		   thermal sensor types...) to be lost, so it is now optional.
		   It might even go away if nobody reports it as being useful,
		   as I see very little reason why this would be needed at
		   all. */
		dev_info(dev, "If reset=1 solved a problem you were "
			 "having, please report!\n");

		/* save these registers */
		i = w83781d_read_value(data, W83781D_REG_BEEP_CONFIG);
		p = w83781d_read_value(data, W83781D_REG_PWMCLK12);
		/* Reset all except Watchdog values and last conversion values
		   This sets fan-divs to 2, among others */
		w83781d_write_value(data, W83781D_REG_CONFIG, 0x80);
		/* Restore the registers and disable power-on abnormal beep.
		   This saves FAN 1/2/3 input/output values set by BIOS. */
		w83781d_write_value(data, W83781D_REG_BEEP_CONFIG, i | 0x80);
		w83781d_write_value(data, W83781D_REG_PWMCLK12, p);
		/* Disable master beep-enable (reset turns it on).
		   Individual beep_mask should be reset to off but for some reason
		   disabling this bit helps some people not get beeped */
		w83781d_write_value(data, W83781D_REG_BEEP_INTS2, 0);
	}

	/* Disable power-on abnormal beep, as advised by the datasheet.
	   Already done if reset=1. */
	if (init && !reset && type != as99127f) {
		i = w83781d_read_value(data, W83781D_REG_BEEP_CONFIG);
		w83781d_write_value(data, W83781D_REG_BEEP_CONFIG, i | 0x80);
	}

	data->vrm = vid_which_vrm();

	if ((type != w83781d) && (type != as99127f)) {
		tmp = w83781d_read_value(data, W83781D_REG_SCFG1);
		for (i = 1; i <= 3; i++) {
			if (!(tmp & BIT_SCFG1[i - 1])) {
				data->sens[i - 1] = W83781D_DEFAULT_BETA;
			} else {
				if (w83781d_read_value
				    (data,
				     W83781D_REG_SCFG2) & BIT_SCFG2[i - 1])
					data->sens[i - 1] = 1;
				else
					data->sens[i - 1] = 2;
			}
			if (type == w83783s && i == 2)
				break;
		}
	}

	if (init && type != as99127f) {
		/* Enable temp2 */
		tmp = w83781d_read_value(data, W83781D_REG_TEMP2_CONFIG);
		if (tmp & 0x01) {
			dev_warn(dev, "Enabling temp2, readings "
				 "might not make sense\n");
			w83781d_write_value(data, W83781D_REG_TEMP2_CONFIG,
				tmp & 0xfe);
		}

		/* Enable temp3 */
		if (type != w83783s) {
			tmp = w83781d_read_value(data,
				W83781D_REG_TEMP3_CONFIG);
			if (tmp & 0x01) {
				dev_warn(dev, "Enabling temp3, "
					 "readings might not make sense\n");
				w83781d_write_value(data,
					W83781D_REG_TEMP3_CONFIG, tmp & 0xfe);
			}
		}
	}

	/* Start monitoring */
	w83781d_write_value(data, W83781D_REG_CONFIG,
			    (w83781d_read_value(data,
						W83781D_REG_CONFIG) & 0xf7)
			    | 0x01);

	/* A few vars need to be filled upon startup */
	for (i = 0; i < 3; i++) {
		data->fan_min[i] = w83781d_read_value(data,
					W83781D_REG_FAN_MIN(i));
	}

	mutex_init(&data->update_lock);
}

static struct w83781d_data *w83781d_update_device(struct device *dev)
{
	struct w83781d_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = &data->client;
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		dev_dbg(dev, "Starting device update\n");

		for (i = 0; i <= 8; i++) {
			if (data->type == w83783s && i == 1)
				continue;	/* 783S has no in1 */
			data->in[i] =
			    w83781d_read_value(data, W83781D_REG_IN(i));
			data->in_min[i] =
			    w83781d_read_value(data, W83781D_REG_IN_MIN(i));
			data->in_max[i] =
			    w83781d_read_value(data, W83781D_REG_IN_MAX(i));
			if ((data->type != w83782d)
			    && (data->type != w83627hf) && (i == 6))
				break;
		}
		for (i = 0; i < 3; i++) {
			data->fan[i] =
			    w83781d_read_value(data, W83781D_REG_FAN(i));
			data->fan_min[i] =
			    w83781d_read_value(data, W83781D_REG_FAN_MIN(i));
		}
		if (data->type != w83781d && data->type != as99127f) {
			for (i = 0; i < 4; i++) {
				data->pwm[i] =
				    w83781d_read_value(data,
						       W83781D_REG_PWM[i]);
				if ((data->type != w83782d || !client->driver)
				    && i == 1)
					break;
			}
			/* Only PWM2 can be disabled */
			data->pwm2_enable = (w83781d_read_value(data,
					      W83781D_REG_PWMCLK12) & 0x08) >> 3;
		}

		data->temp = w83781d_read_value(data, W83781D_REG_TEMP(1));
		data->temp_max =
		    w83781d_read_value(data, W83781D_REG_TEMP_OVER(1));
		data->temp_max_hyst =
		    w83781d_read_value(data, W83781D_REG_TEMP_HYST(1));
		data->temp_add[0] =
		    w83781d_read_value(data, W83781D_REG_TEMP(2));
		data->temp_max_add[0] =
		    w83781d_read_value(data, W83781D_REG_TEMP_OVER(2));
		data->temp_max_hyst_add[0] =
		    w83781d_read_value(data, W83781D_REG_TEMP_HYST(2));
		if (data->type != w83783s) {
			data->temp_add[1] =
			    w83781d_read_value(data, W83781D_REG_TEMP(3));
			data->temp_max_add[1] =
			    w83781d_read_value(data,
					       W83781D_REG_TEMP_OVER(3));
			data->temp_max_hyst_add[1] =
			    w83781d_read_value(data,
					       W83781D_REG_TEMP_HYST(3));
		}
		i = w83781d_read_value(data, W83781D_REG_VID_FANDIV);
		data->vid = i & 0x0f;
		data->vid |= (w83781d_read_value(data,
					W83781D_REG_CHIPID) & 0x01) << 4;
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = (i >> 6) & 0x03;
		data->fan_div[2] = (w83781d_read_value(data,
					W83781D_REG_PIN) >> 6) & 0x03;
		if ((data->type != w83781d) && (data->type != as99127f)) {
			i = w83781d_read_value(data, W83781D_REG_VBAT);
			data->fan_div[0] |= (i >> 3) & 0x04;
			data->fan_div[1] |= (i >> 4) & 0x04;
			data->fan_div[2] |= (i >> 5) & 0x04;
		}
		if ((data->type == w83782d) || (data->type == w83627hf)) {
			data->alarms = w83781d_read_value(data,
						W83782D_REG_ALARM1)
				     | (w83781d_read_value(data,
						W83782D_REG_ALARM2) << 8)
				     | (w83781d_read_value(data,
						W83782D_REG_ALARM3) << 16);
		} else if (data->type == w83783s) {
			data->alarms = w83781d_read_value(data,
						W83782D_REG_ALARM1)
				     | (w83781d_read_value(data,
						W83782D_REG_ALARM2) << 8);
		} else {
			/* No real-time status registers, fall back to
			   interrupt status registers */
			data->alarms = w83781d_read_value(data,
						W83781D_REG_ALARM1)
				     | (w83781d_read_value(data,
						W83781D_REG_ALARM2) << 8);
		}
		i = w83781d_read_value(data, W83781D_REG_BEEP_INTS2);
		data->beep_enable = i >> 7;
		data->beep_mask = ((i & 0x7f) << 8) +
		    w83781d_read_value(data, W83781D_REG_BEEP_INTS1);
		if ((data->type != w83781d) && (data->type != as99127f)) {
			data->beep_mask |=
			    w83781d_read_value(data,
					       W83781D_REG_BEEP_INTS3) << 16;
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* return 1 if a supported chip is found, 0 otherwise */
static int __init
w83781d_isa_found(unsigned short address)
{
	int val, save, found = 0;

	if (!request_region(address, W83781D_EXTENT, "w83781d"))
		return 0;

#define REALLY_SLOW_IO
	/* We need the timeouts for at least some W83781D-like
	   chips. But only if we read 'undefined' registers. */
	val = inb_p(address + 1);
	if (inb_p(address + 2) != val
	 || inb_p(address + 3) != val
	 || inb_p(address + 7) != val) {
		pr_debug("w83781d: Detection failed at step 1\n");
		goto release;
	}
#undef REALLY_SLOW_IO

	/* We should be able to change the 7 LSB of the address port. The
	   MSB (busy flag) should be clear initially, set after the write. */
	save = inb_p(address + W83781D_ADDR_REG_OFFSET);
	if (save & 0x80) {
		pr_debug("w83781d: Detection failed at step 2\n");
		goto release;
	}
	val = ~save & 0x7f;
	outb_p(val, address + W83781D_ADDR_REG_OFFSET);
	if (inb_p(address + W83781D_ADDR_REG_OFFSET) != (val | 0x80)) {
		outb_p(save, address + W83781D_ADDR_REG_OFFSET);
		pr_debug("w83781d: Detection failed at step 3\n");
		goto release;
	}

	/* We found a device, now see if it could be a W83781D */
	outb_p(W83781D_REG_CONFIG, address + W83781D_ADDR_REG_OFFSET);
	val = inb_p(address + W83781D_DATA_REG_OFFSET);
	if (val & 0x80) {
		pr_debug("w83781d: Detection failed at step 4\n");
		goto release;
	}
	outb_p(W83781D_REG_BANK, address + W83781D_ADDR_REG_OFFSET);
	save = inb_p(address + W83781D_DATA_REG_OFFSET);
	outb_p(W83781D_REG_CHIPMAN, address + W83781D_ADDR_REG_OFFSET);
	val = inb_p(address + W83781D_DATA_REG_OFFSET);
	if ((!(save & 0x80) && (val != 0xa3))
	 || ((save & 0x80) && (val != 0x5c))) {
		pr_debug("w83781d: Detection failed at step 5\n");
		goto release;
	}
	outb_p(W83781D_REG_I2C_ADDR, address + W83781D_ADDR_REG_OFFSET);
	val = inb_p(address + W83781D_DATA_REG_OFFSET);
	if (val < 0x03 || val > 0x77) {	/* Not a valid I2C address */
		pr_debug("w83781d: Detection failed at step 6\n");
		goto release;
	}

	/* The busy flag should be clear again */
	if (inb_p(address + W83781D_ADDR_REG_OFFSET) & 0x80) {
		pr_debug("w83781d: Detection failed at step 7\n");
		goto release;
	}

	/* Determine the chip type */
	outb_p(W83781D_REG_BANK, address + W83781D_ADDR_REG_OFFSET);
	save = inb_p(address + W83781D_DATA_REG_OFFSET);
	outb_p(save & 0xf8, address + W83781D_DATA_REG_OFFSET);
	outb_p(W83781D_REG_WCHIPID, address + W83781D_ADDR_REG_OFFSET);
	val = inb_p(address + W83781D_DATA_REG_OFFSET);
	if ((val & 0xfe) == 0x10	/* W83781D */
	 || val == 0x30			/* W83782D */
	 || val == 0x21)		/* W83627HF */
		found = 1;

	if (found)
		pr_info("w83781d: Found a %s chip at %#x\n",
			val == 0x21 ? "W83627HF" :
			val == 0x30 ? "W83782D" : "W83781D", (int)address);

 release:
	release_region(address, W83781D_EXTENT);
	return found;
}

static int __init
w83781d_isa_device_add(unsigned short address)
{
	struct resource res = {
		.start	= address,
		.end	= address + W83781D_EXTENT,
		.name	= "w83781d",
		.flags	= IORESOURCE_IO,
	};
	int err;

	pdev = platform_device_alloc("w83781d", address);
	if (!pdev) {
		err = -ENOMEM;
		printk(KERN_ERR "w83781d: Device allocation failed\n");
		goto exit;
	}

	err = platform_device_add_resources(pdev, &res, 1);
	if (err) {
		printk(KERN_ERR "w83781d: Device resource addition failed "
		       "(%d)\n", err);
		goto exit_device_put;
	}

	err = platform_device_add(pdev);
	if (err) {
		printk(KERN_ERR "w83781d: Device addition failed (%d)\n",
		       err);
		goto exit_device_put;
	}

	return 0;

 exit_device_put:
	platform_device_put(pdev);
 exit:
	pdev = NULL;
	return err;
}

static int __init
sensors_w83781d_init(void)
{
	int res;

	res = i2c_add_driver(&w83781d_driver);
	if (res)
		goto exit;

	if (w83781d_isa_found(isa_address)) {
		res = platform_driver_register(&w83781d_isa_driver);
		if (res)
			goto exit_unreg_i2c_driver;

		/* Sets global pdev as a side effect */
		res = w83781d_isa_device_add(isa_address);
		if (res)
			goto exit_unreg_isa_driver;
	}

	return 0;

 exit_unreg_isa_driver:
	platform_driver_unregister(&w83781d_isa_driver);
 exit_unreg_i2c_driver:
	i2c_del_driver(&w83781d_driver);
 exit:
	return res;
}

static void __exit
sensors_w83781d_exit(void)
{
	if (pdev) {
		platform_device_unregister(pdev);
		platform_driver_unregister(&w83781d_isa_driver);
	}
	i2c_del_driver(&w83781d_driver);
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>, "
	      "Philip Edelbrock <phil@netroedge.com>, "
	      "and Mark Studebaker <mdsxyz123@yahoo.com>");
MODULE_DESCRIPTION("W83781D driver");
MODULE_LICENSE("GPL");

module_init(sensors_w83781d_init);
module_exit(sensors_w83781d_exit);
