/*
    w83781d.c - Part of lm_sensors, Linux kernel modules for hardware
                monitoring
    Copyright (c) 1998 - 2001  Frodo Looijaard <frodol@dds.nl>,
    Philip Edelbrock <phil@netroedge.com>,
    and Mark Studebaker <mdsxyz123@yahoo.com>

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
#include <linux/i2c-isa.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <asm/io.h>
#include "lm75.h"

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
					0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
					0x2c, 0x2d, 0x2e, 0x2f, I2C_CLIENT_END };
static unsigned short isa_address = 0x290;

/* Insmod parameters */
I2C_CLIENT_INSMOD_5(w83781d, w83782d, w83783s, w83627hf, as99127f);
I2C_CLIENT_MODULE_PARM(force_subclients, "List of subclient addresses: "
		    "{bus, clientaddr, subclientaddr1, subclientaddr2}");

static int init = 1;
module_param(init, bool, 0);
MODULE_PARM_DESC(init, "Set to zero to bypass chip initialization");

/* Constants specified below */

/* Length of ISA address segment */
#define W83781D_EXTENT			8

/* Where are the ISA address/data registers relative to the base address */
#define W83781D_ADDR_REG_OFFSET		5
#define W83781D_DATA_REG_OFFSET		6

/* The W83781D registers */
/* The W83782D registers for nr=7,8 are in bank 5 */
#define W83781D_REG_IN_MAX(nr)		((nr < 7) ? (0x2b + (nr) * 2) : \
						    (0x554 + (((nr) - 7) * 2)))
#define W83781D_REG_IN_MIN(nr)		((nr < 7) ? (0x2c + (nr) * 2) : \
						    (0x555 + (((nr) - 7) * 2)))
#define W83781D_REG_IN(nr)		((nr < 7) ? (0x20 + (nr)) : \
						    (0x550 + (nr) - 7))

#define W83781D_REG_FAN_MIN(nr)		(0x3a + (nr))
#define W83781D_REG_FAN(nr)		(0x27 + (nr))

#define W83781D_REG_BANK		0x4E
#define W83781D_REG_TEMP2_CONFIG	0x152
#define W83781D_REG_TEMP3_CONFIG	0x252
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
#define W83781D_REG_ALARM1		0x41
#define W83781D_REG_ALARM2		0x42
#define W83781D_REG_ALARM3		0x450	/* not on W83781D */

#define W83781D_REG_IRQ			0x4C
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
#define W83781D_REG_PWM1		0x5B	/* 782d and 783s/627hf datasheets disagree */
						/* on which is which; */
#define W83781D_REG_PWM2		0x5A	/* We follow the 782d convention here, */
						/* However 782d is probably wrong. */
#define W83781D_REG_PWM3		0x5E
#define W83781D_REG_PWM4		0x5F
#define W83781D_REG_PWMCLK12		0x5C
#define W83781D_REG_PWMCLK34		0x45C
static const u8 regpwm[] = { W83781D_REG_PWM1, W83781D_REG_PWM2,
	W83781D_REG_PWM3, W83781D_REG_PWM4
};

#define W83781D_REG_PWM(nr)		(regpwm[(nr) - 1])

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

/* Conversions. Rounding and limit checking is only done on the TO_REG
   variants. Note that you should be a bit careful with which arguments
   these macros are called: arguments may be evaluated more than once.
   Fixing this is just not worth it. */
#define IN_TO_REG(val)			(SENSORS_LIMIT((((val) * 10 + 8)/16),0,255))
#define IN_FROM_REG(val)		(((val) * 16) / 10)

static inline u8
FAN_TO_REG(long rpm, int div)
{
	if (rpm == 0)
		return 255;
	rpm = SENSORS_LIMIT(rpm, 1, 1000000);
	return SENSORS_LIMIT((1350000 + rpm * div / 2) / (rpm * div), 1, 254);
}

#define FAN_FROM_REG(val,div)		((val) == 0   ? -1 : \
					((val) == 255 ? 0 : \
							1350000 / ((val) * (div))))

#define TEMP_TO_REG(val)		(SENSORS_LIMIT(((val) < 0 ? (val)+0x100*1000 \
						: (val)) / 1000, 0, 0xff))
#define TEMP_FROM_REG(val)		(((val) & 0x80 ? (val)-0x100 : (val)) * 1000)

#define PWM_FROM_REG(val)		(val)
#define PWM_TO_REG(val)			(SENSORS_LIMIT((val),0,255))
#define BEEP_MASK_FROM_REG(val,type)	((type) == as99127f ? \
					 (val) ^ 0x7fff : (val))
#define BEEP_MASK_TO_REG(val,type)	((type) == as99127f ? \
					 (~(val)) & 0x7fff : (val) & 0xffffff)

#define BEEP_ENABLE_TO_REG(val)		((val) ? 1 : 0)
#define BEEP_ENABLE_FROM_REG(val)	((val) ? 1 : 0)

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
	return ((u8) i);
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

/* This module may seem overly long and complicated. In fact, it is not so
   bad. Quite a lot of bookkeeping is done. A real driver can often cut
   some corners. */

/* For each registered W83781D, we need to keep some data in memory. That
   data is pointed to by w83781d_list[NR]->data. The structure itself is
   dynamically allocated, at the same time when a new w83781d client is
   allocated. */
struct w83781d_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore lock;
	enum chips type;

	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	struct i2c_client *lm75[2];	/* for secondary I2C addresses */
	/* array of 2 pointers to subclients */

	u8 in[9];		/* Register value - 8 & 9 for 782D only */
	u8 in_max[9];		/* Register value - 8 & 9 for 782D only */
	u8 in_min[9];		/* Register value - 8 & 9 for 782D only */
	u8 fan[3];		/* Register value */
	u8 fan_min[3];		/* Register value */
	u8 temp;
	u8 temp_max;		/* Register value */
	u8 temp_max_hyst;	/* Register value */
	u16 temp_add[2];	/* Register value */
	u16 temp_max_add[2];	/* Register value */
	u16 temp_max_hyst_add[2];	/* Register value */
	u8 fan_div[3];		/* Register encoding, shifted right */
	u8 vid;			/* Register encoding, combined */
	u32 alarms;		/* Register encoding, combined */
	u32 beep_mask;		/* Register encoding, combined */
	u8 beep_enable;		/* Boolean */
	u8 pwm[4];		/* Register value */
	u8 pwmenable[4];	/* Boolean */
	u16 sens[3];		/* 782D/783S only.
				   1 = pentium diode; 2 = 3904 diode;
				   3000-5000 = thermistor beta.
				   Default = 3435. 
				   Other Betas unimplemented */
	u8 vrm;
};

static int w83781d_attach_adapter(struct i2c_adapter *adapter);
static int w83781d_isa_attach_adapter(struct i2c_adapter *adapter);
static int w83781d_detect(struct i2c_adapter *adapter, int address, int kind);
static int w83781d_detach_client(struct i2c_client *client);

static int w83781d_read_value(struct i2c_client *client, u16 register);
static int w83781d_write_value(struct i2c_client *client, u16 register,
			       u16 value);
static struct w83781d_data *w83781d_update_device(struct device *dev);
static void w83781d_init_client(struct i2c_client *client);

static struct i2c_driver w83781d_driver = {
	.owner = THIS_MODULE,
	.name = "w83781d",
	.id = I2C_DRIVERID_W83781D,
	.flags = I2C_DF_NOTIFY,
	.attach_adapter = w83781d_attach_adapter,
	.detach_client = w83781d_detach_client,
};

static struct i2c_driver w83781d_isa_driver = {
	.owner = THIS_MODULE,
	.name = "w83781d-isa",
	.attach_adapter = w83781d_isa_attach_adapter,
	.detach_client = w83781d_detach_client,
};


/* following are the sysfs callback functions */
#define show_in_reg(reg) \
static ssize_t show_##reg (struct device *dev, char *buf, int nr) \
{ \
	struct w83781d_data *data = w83781d_update_device(dev); \
	return sprintf(buf,"%ld\n", (long)IN_FROM_REG(data->reg[nr] * 10)); \
}
show_in_reg(in);
show_in_reg(in_min);
show_in_reg(in_max);

#define store_in_reg(REG, reg) \
static ssize_t store_in_##reg (struct device *dev, const char *buf, size_t count, int nr) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct w83781d_data *data = i2c_get_clientdata(client); \
	u32 val; \
	 \
	val = simple_strtoul(buf, NULL, 10) / 10; \
	 \
	down(&data->update_lock); \
	data->in_##reg[nr] = IN_TO_REG(val); \
	w83781d_write_value(client, W83781D_REG_IN_##REG(nr), data->in_##reg[nr]); \
	 \
	up(&data->update_lock); \
	return count; \
}
store_in_reg(MIN, min);
store_in_reg(MAX, max);

#define sysfs_in_offset(offset) \
static ssize_t \
show_regs_in_##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
        return show_in(dev, buf, offset); \
} \
static DEVICE_ATTR(in##offset##_input, S_IRUGO, show_regs_in_##offset, NULL);

#define sysfs_in_reg_offset(reg, offset) \
static ssize_t show_regs_in_##reg##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_in_##reg (dev, buf, offset); \
} \
static ssize_t store_regs_in_##reg##offset (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
	return store_in_##reg (dev, buf, count, offset); \
} \
static DEVICE_ATTR(in##offset##_##reg, S_IRUGO| S_IWUSR, show_regs_in_##reg##offset, store_regs_in_##reg##offset);

#define sysfs_in_offsets(offset) \
sysfs_in_offset(offset); \
sysfs_in_reg_offset(min, offset); \
sysfs_in_reg_offset(max, offset);

sysfs_in_offsets(0);
sysfs_in_offsets(1);
sysfs_in_offsets(2);
sysfs_in_offsets(3);
sysfs_in_offsets(4);
sysfs_in_offsets(5);
sysfs_in_offsets(6);
sysfs_in_offsets(7);
sysfs_in_offsets(8);

#define device_create_file_in(client, offset) \
do { \
device_create_file(&client->dev, &dev_attr_in##offset##_input); \
device_create_file(&client->dev, &dev_attr_in##offset##_min); \
device_create_file(&client->dev, &dev_attr_in##offset##_max); \
} while (0)

#define show_fan_reg(reg) \
static ssize_t show_##reg (struct device *dev, char *buf, int nr) \
{ \
	struct w83781d_data *data = w83781d_update_device(dev); \
	return sprintf(buf,"%ld\n", \
		FAN_FROM_REG(data->reg[nr-1], (long)DIV_FROM_REG(data->fan_div[nr-1]))); \
}
show_fan_reg(fan);
show_fan_reg(fan_min);

static ssize_t
store_fan_min(struct device *dev, const char *buf, size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83781d_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	data->fan_min[nr - 1] =
	    FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr - 1]));
	w83781d_write_value(client, W83781D_REG_FAN_MIN(nr),
			    data->fan_min[nr - 1]);

	up(&data->update_lock);
	return count;
}

#define sysfs_fan_offset(offset) \
static ssize_t show_regs_fan_##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_fan(dev, buf, offset); \
} \
static DEVICE_ATTR(fan##offset##_input, S_IRUGO, show_regs_fan_##offset, NULL);

#define sysfs_fan_min_offset(offset) \
static ssize_t show_regs_fan_min##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_fan_min(dev, buf, offset); \
} \
static ssize_t store_regs_fan_min##offset (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
	return store_fan_min(dev, buf, count, offset); \
} \
static DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR, show_regs_fan_min##offset, store_regs_fan_min##offset);

sysfs_fan_offset(1);
sysfs_fan_min_offset(1);
sysfs_fan_offset(2);
sysfs_fan_min_offset(2);
sysfs_fan_offset(3);
sysfs_fan_min_offset(3);

#define device_create_file_fan(client, offset) \
do { \
device_create_file(&client->dev, &dev_attr_fan##offset##_input); \
device_create_file(&client->dev, &dev_attr_fan##offset##_min); \
} while (0)

#define show_temp_reg(reg) \
static ssize_t show_##reg (struct device *dev, char *buf, int nr) \
{ \
	struct w83781d_data *data = w83781d_update_device(dev); \
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
static ssize_t store_temp_##reg (struct device *dev, const char *buf, size_t count, int nr) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct w83781d_data *data = i2c_get_clientdata(client); \
	s32 val; \
	 \
	val = simple_strtol(buf, NULL, 10); \
	 \
	down(&data->update_lock); \
	 \
	if (nr >= 2) {	/* TEMP2 and TEMP3 */ \
		data->temp_##reg##_add[nr-2] = LM75_TEMP_TO_REG(val); \
		w83781d_write_value(client, W83781D_REG_TEMP_##REG(nr), \
				data->temp_##reg##_add[nr-2]); \
	} else {	/* TEMP1 */ \
		data->temp_##reg = TEMP_TO_REG(val); \
		w83781d_write_value(client, W83781D_REG_TEMP_##REG(nr), \
			data->temp_##reg); \
	} \
	 \
	up(&data->update_lock); \
	return count; \
}
store_temp_reg(OVER, max);
store_temp_reg(HYST, max_hyst);

#define sysfs_temp_offset(offset) \
static ssize_t \
show_regs_temp_##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_temp(dev, buf, offset); \
} \
static DEVICE_ATTR(temp##offset##_input, S_IRUGO, show_regs_temp_##offset, NULL);

#define sysfs_temp_reg_offset(reg, offset) \
static ssize_t show_regs_temp_##reg##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_temp_##reg (dev, buf, offset); \
} \
static ssize_t store_regs_temp_##reg##offset (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
	return store_temp_##reg (dev, buf, count, offset); \
} \
static DEVICE_ATTR(temp##offset##_##reg, S_IRUGO| S_IWUSR, show_regs_temp_##reg##offset, store_regs_temp_##reg##offset);

#define sysfs_temp_offsets(offset) \
sysfs_temp_offset(offset); \
sysfs_temp_reg_offset(max, offset); \
sysfs_temp_reg_offset(max_hyst, offset);

sysfs_temp_offsets(1);
sysfs_temp_offsets(2);
sysfs_temp_offsets(3);

#define device_create_file_temp(client, offset) \
do { \
device_create_file(&client->dev, &dev_attr_temp##offset##_input); \
device_create_file(&client->dev, &dev_attr_temp##offset##_max); \
device_create_file(&client->dev, &dev_attr_temp##offset##_max_hyst); \
} while (0)

static ssize_t
show_vid_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n", (long) vid_from_reg(data->vid, data->vrm));
}

static
DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid_reg, NULL);
#define device_create_file_vid(client) \
device_create_file(&client->dev, &dev_attr_cpu0_vid);
static ssize_t
show_vrm_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->vrm);
}

static ssize_t
store_vrm_reg(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83781d_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);
	data->vrm = val;

	return count;
}

static
DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm_reg, store_vrm_reg);
#define device_create_file_vrm(client) \
device_create_file(&client->dev, &dev_attr_vrm);
static ssize_t
show_alarms_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}

static
DEVICE_ATTR(alarms, S_IRUGO, show_alarms_reg, NULL);
#define device_create_file_alarms(client) \
device_create_file(&client->dev, &dev_attr_alarms);
static ssize_t show_beep_mask (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n",
		       (long)BEEP_MASK_FROM_REG(data->beep_mask, data->type));
}
static ssize_t show_beep_enable (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n",
		       (long)BEEP_ENABLE_FROM_REG(data->beep_enable));
}

#define BEEP_ENABLE			0	/* Store beep_enable */
#define BEEP_MASK			1	/* Store beep_mask */

static ssize_t
store_beep_reg(struct device *dev, const char *buf, size_t count,
	       int update_mask)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83781d_data *data = i2c_get_clientdata(client);
	u32 val, val2;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);

	if (update_mask == BEEP_MASK) {	/* We are storing beep_mask */
		data->beep_mask = BEEP_MASK_TO_REG(val, data->type);
		w83781d_write_value(client, W83781D_REG_BEEP_INTS1,
				    data->beep_mask & 0xff);

		if ((data->type != w83781d) && (data->type != as99127f)) {
			w83781d_write_value(client, W83781D_REG_BEEP_INTS3,
					    ((data->beep_mask) >> 16) & 0xff);
		}

		val2 = (data->beep_mask >> 8) & 0x7f;
	} else {		/* We are storing beep_enable */
		val2 = w83781d_read_value(client, W83781D_REG_BEEP_INTS2) & 0x7f;
		data->beep_enable = BEEP_ENABLE_TO_REG(val);
	}

	w83781d_write_value(client, W83781D_REG_BEEP_INTS2,
			    val2 | data->beep_enable << 7);

	up(&data->update_lock);
	return count;
}

#define sysfs_beep(REG, reg) \
static ssize_t show_regs_beep_##reg (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_beep_##reg(dev, attr, buf); \
} \
static ssize_t store_regs_beep_##reg (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
	return store_beep_reg(dev, buf, count, BEEP_##REG); \
} \
static DEVICE_ATTR(beep_##reg, S_IRUGO | S_IWUSR, show_regs_beep_##reg, store_regs_beep_##reg);

sysfs_beep(ENABLE, enable);
sysfs_beep(MASK, mask);

#define device_create_file_beep(client) \
do { \
device_create_file(&client->dev, &dev_attr_beep_enable); \
device_create_file(&client->dev, &dev_attr_beep_mask); \
} while (0)

static ssize_t
show_fan_div_reg(struct device *dev, char *buf, int nr)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n",
		       (long) DIV_FROM_REG(data->fan_div[nr - 1]));
}

/* Note: we save and restore the fan minimum here, because its value is
   determined in part by the fan divisor.  This follows the principle of
   least suprise; the user doesn't expect the fan minimum to change just
   because the divisor changed. */
static ssize_t
store_fan_div_reg(struct device *dev, const char *buf, size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83781d_data *data = i2c_get_clientdata(client);
	unsigned long min;
	u8 reg;
	unsigned long val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	
	/* Save fan_min */
	min = FAN_FROM_REG(data->fan_min[nr],
			   DIV_FROM_REG(data->fan_div[nr]));

	data->fan_div[nr] = DIV_TO_REG(val, data->type);

	reg = (w83781d_read_value(client, nr==2 ? W83781D_REG_PIN : W83781D_REG_VID_FANDIV)
	       & (nr==0 ? 0xcf : 0x3f))
	    | ((data->fan_div[nr] & 0x03) << (nr==0 ? 4 : 6));
	w83781d_write_value(client, nr==2 ? W83781D_REG_PIN : W83781D_REG_VID_FANDIV, reg);

	/* w83781d and as99127f don't have extended divisor bits */
	if (data->type != w83781d && data->type != as99127f) {
		reg = (w83781d_read_value(client, W83781D_REG_VBAT)
		       & ~(1 << (5 + nr)))
		    | ((data->fan_div[nr] & 0x04) << (3 + nr));
		w83781d_write_value(client, W83781D_REG_VBAT, reg);
	}

	/* Restore fan_min */
	data->fan_min[nr] = FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	w83781d_write_value(client, W83781D_REG_FAN_MIN(nr+1), data->fan_min[nr]);

	up(&data->update_lock);
	return count;
}

#define sysfs_fan_div(offset) \
static ssize_t show_regs_fan_div_##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_fan_div_reg(dev, buf, offset); \
} \
static ssize_t store_regs_fan_div_##offset (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
	return store_fan_div_reg(dev, buf, count, offset - 1); \
} \
static DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR, show_regs_fan_div_##offset, store_regs_fan_div_##offset);

sysfs_fan_div(1);
sysfs_fan_div(2);
sysfs_fan_div(3);

#define device_create_file_fan_div(client, offset) \
do { \
device_create_file(&client->dev, &dev_attr_fan##offset##_div); \
} while (0)

static ssize_t
show_pwm_reg(struct device *dev, char *buf, int nr)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n", (long) PWM_FROM_REG(data->pwm[nr - 1]));
}

static ssize_t
show_pwmenable_reg(struct device *dev, char *buf, int nr)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->pwmenable[nr - 1]);
}

static ssize_t
store_pwm_reg(struct device *dev, const char *buf, size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83781d_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	data->pwm[nr - 1] = PWM_TO_REG(val);
	w83781d_write_value(client, W83781D_REG_PWM(nr), data->pwm[nr - 1]);
	up(&data->update_lock);
	return count;
}

static ssize_t
store_pwmenable_reg(struct device *dev, const char *buf, size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83781d_data *data = i2c_get_clientdata(client);
	u32 val, reg;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);

	switch (val) {
	case 0:
	case 1:
		reg = w83781d_read_value(client, W83781D_REG_PWMCLK12);
		w83781d_write_value(client, W83781D_REG_PWMCLK12,
				    (reg & 0xf7) | (val << 3));

		reg = w83781d_read_value(client, W83781D_REG_BEEP_CONFIG);
		w83781d_write_value(client, W83781D_REG_BEEP_CONFIG,
				    (reg & 0xef) | (!val << 4));

		data->pwmenable[nr - 1] = val;
		break;

	default:
		up(&data->update_lock);
		return -EINVAL;
	}

	up(&data->update_lock);
	return count;
}

#define sysfs_pwm(offset) \
static ssize_t show_regs_pwm_##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_pwm_reg(dev, buf, offset); \
} \
static ssize_t store_regs_pwm_##offset (struct device *dev, struct device_attribute *attr, \
		const char *buf, size_t count) \
{ \
	return store_pwm_reg(dev, buf, count, offset); \
} \
static DEVICE_ATTR(pwm##offset, S_IRUGO | S_IWUSR, \
		show_regs_pwm_##offset, store_regs_pwm_##offset);

#define sysfs_pwmenable(offset) \
static ssize_t show_regs_pwmenable_##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_pwmenable_reg(dev, buf, offset); \
} \
static ssize_t store_regs_pwmenable_##offset (struct device *dev, struct device_attribute *attr, \
		const char *buf, size_t count) \
{ \
	return store_pwmenable_reg(dev, buf, count, offset); \
} \
static DEVICE_ATTR(pwm##offset##_enable, S_IRUGO | S_IWUSR, \
		show_regs_pwmenable_##offset, store_regs_pwmenable_##offset);

sysfs_pwm(1);
sysfs_pwm(2);
sysfs_pwmenable(2);		/* only PWM2 can be enabled/disabled */
sysfs_pwm(3);
sysfs_pwm(4);

#define device_create_file_pwm(client, offset) \
do { \
device_create_file(&client->dev, &dev_attr_pwm##offset); \
} while (0)

#define device_create_file_pwmenable(client, offset) \
do { \
device_create_file(&client->dev, &dev_attr_pwm##offset##_enable); \
} while (0)

static ssize_t
show_sensor_reg(struct device *dev, char *buf, int nr)
{
	struct w83781d_data *data = w83781d_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->sens[nr - 1]);
}

static ssize_t
store_sensor_reg(struct device *dev, const char *buf, size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83781d_data *data = i2c_get_clientdata(client);
	u32 val, tmp;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);

	switch (val) {
	case 1:		/* PII/Celeron diode */
		tmp = w83781d_read_value(client, W83781D_REG_SCFG1);
		w83781d_write_value(client, W83781D_REG_SCFG1,
				    tmp | BIT_SCFG1[nr - 1]);
		tmp = w83781d_read_value(client, W83781D_REG_SCFG2);
		w83781d_write_value(client, W83781D_REG_SCFG2,
				    tmp | BIT_SCFG2[nr - 1]);
		data->sens[nr - 1] = val;
		break;
	case 2:		/* 3904 */
		tmp = w83781d_read_value(client, W83781D_REG_SCFG1);
		w83781d_write_value(client, W83781D_REG_SCFG1,
				    tmp | BIT_SCFG1[nr - 1]);
		tmp = w83781d_read_value(client, W83781D_REG_SCFG2);
		w83781d_write_value(client, W83781D_REG_SCFG2,
				    tmp & ~BIT_SCFG2[nr - 1]);
		data->sens[nr - 1] = val;
		break;
	case W83781D_DEFAULT_BETA:	/* thermistor */
		tmp = w83781d_read_value(client, W83781D_REG_SCFG1);
		w83781d_write_value(client, W83781D_REG_SCFG1,
				    tmp & ~BIT_SCFG1[nr - 1]);
		data->sens[nr - 1] = val;
		break;
	default:
		dev_err(dev, "Invalid sensor type %ld; must be 1, 2, or %d\n",
		       (long) val, W83781D_DEFAULT_BETA);
		break;
	}

	up(&data->update_lock);
	return count;
}

#define sysfs_sensor(offset) \
static ssize_t show_regs_sensor_##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
    return show_sensor_reg(dev, buf, offset); \
} \
static ssize_t store_regs_sensor_##offset (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
    return store_sensor_reg(dev, buf, count, offset); \
} \
static DEVICE_ATTR(temp##offset##_type, S_IRUGO | S_IWUSR, show_regs_sensor_##offset, store_regs_sensor_##offset);

sysfs_sensor(1);
sysfs_sensor(2);
sysfs_sensor(3);

#define device_create_file_sensor(client, offset) \
do { \
device_create_file(&client->dev, &dev_attr_temp##offset##_type); \
} while (0)

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

static int
w83781d_isa_attach_adapter(struct i2c_adapter *adapter)
{
	return w83781d_detect(adapter, isa_address, -1);
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
		w83781d_write_value(new_client, W83781D_REG_I2C_SUBADDR,
				(force_subclients[2] & 0x07) |
				((force_subclients[3] & 0x07) << 4));
		data->lm75[0]->addr = force_subclients[2];
	} else {
		val1 = w83781d_read_value(new_client, W83781D_REG_I2C_SUBADDR);
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

static int
w83781d_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int i = 0, val1 = 0, val2;
	struct i2c_client *new_client;
	struct w83781d_data *data;
	int err;
	const char *client_name = "";
	int is_isa = i2c_is_isa_adapter(adapter);
	enum vendor { winbond, asus } vendid;

	if (!is_isa
	    && !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		err = -EINVAL;
		goto ERROR0;
	}

	/* Prevent users from forcing a kind for a bus it isn't supposed
	   to possibly be on */
	if (is_isa && (kind == as99127f || kind == w83783s)) {
		dev_err(&adapter->dev,
			"Cannot force I2C-only chip for ISA address 0x%02x.\n",
			address);
		err = -EINVAL;
		goto ERROR0;
	}
	
	if (is_isa)
		if (!request_region(address, W83781D_EXTENT,
				    w83781d_isa_driver.name)) {
			dev_dbg(&adapter->dev, "Request of region "
				"0x%x-0x%x for w83781d failed\n", address,
				address + W83781D_EXTENT - 1);
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
			if (inb_p(address + 2) != i
			 || inb_p(address + 3) != i
			 || inb_p(address + 7) != i) {
				dev_dbg(&adapter->dev, "Detection of w83781d "
					"chip failed at step 1\n");
				err = -ENODEV;
				goto ERROR1;
			}
#undef REALLY_SLOW_IO

			/* Let's just hope nothing breaks here */
			i = inb_p(address + 5) & 0x7f;
			outb_p(~i & 0x7f, address + 5);
			val2 = inb_p(address + 5) & 0x7f;
			if (val2 != (~i & 0x7f)) {
				outb_p(i, address + 5);
				dev_dbg(&adapter->dev, "Detection of w83781d "
					"chip failed at step 2 (0x%x != "
					"0x%x at 0x%x)\n", val2, ~i & 0x7f,
					address + 5);
				err = -ENODEV;
				goto ERROR1;
			}
		}
	}

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access w83781d_{read,write}_value. */

	if (!(data = kzalloc(sizeof(struct w83781d_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR1;
	}

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	init_MUTEX(&data->lock);
	new_client->adapter = adapter;
	new_client->driver = is_isa ? &w83781d_isa_driver : &w83781d_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. */

	/* The w8378?d may be stuck in some other bank than bank 0. This may
	   make reading other information impossible. Specify a force=... or
	   force_*=... parameter, and the Winbond will be reset to the right
	   bank. */
	if (kind < 0) {
		if (w83781d_read_value(new_client, W83781D_REG_CONFIG) & 0x80) {
			dev_dbg(&new_client->dev, "Detection failed at step "
				"3\n");
			err = -ENODEV;
			goto ERROR2;
		}
		val1 = w83781d_read_value(new_client, W83781D_REG_BANK);
		val2 = w83781d_read_value(new_client, W83781D_REG_CHIPMAN);
		/* Check for Winbond or Asus ID if in bank 0 */
		if ((!(val1 & 0x07)) &&
		    (((!(val1 & 0x80)) && (val2 != 0xa3) && (val2 != 0xc3))
		     || ((val1 & 0x80) && (val2 != 0x5c) && (val2 != 0x12)))) {
			dev_dbg(&new_client->dev, "Detection failed at step "
				"4\n");
			err = -ENODEV;
			goto ERROR2;
		}
		/* If Winbond SMBus, check address at 0x48.
		   Asus doesn't support, except for as99127f rev.2 */
		if ((!is_isa) && (((!(val1 & 0x80)) && (val2 == 0xa3)) ||
				  ((val1 & 0x80) && (val2 == 0x5c)))) {
			if (w83781d_read_value
			    (new_client, W83781D_REG_I2C_ADDR) != address) {
				dev_dbg(&new_client->dev, "Detection failed "
					"at step 5\n");
				err = -ENODEV;
				goto ERROR2;
			}
		}
	}

	/* We have either had a force parameter, or we have already detected the
	   Winbond. Put it now into bank 0 and Vendor ID High Byte */
	w83781d_write_value(new_client, W83781D_REG_BANK,
			    (w83781d_read_value(new_client,
						W83781D_REG_BANK) & 0x78) |
			    0x80);

	/* Determine the chip type. */
	if (kind <= 0) {
		/* get vendor ID */
		val2 = w83781d_read_value(new_client, W83781D_REG_CHIPMAN);
		if (val2 == 0x5c)
			vendid = winbond;
		else if (val2 == 0x12)
			vendid = asus;
		else {
			dev_dbg(&new_client->dev, "Chip was made by neither "
				"Winbond nor Asus?\n");
			err = -ENODEV;
			goto ERROR2;
		}

		val1 = w83781d_read_value(new_client, W83781D_REG_WCHIPID);
		if ((val1 == 0x10 || val1 == 0x11) && vendid == winbond)
			kind = w83781d;
		else if (val1 == 0x30 && vendid == winbond)
			kind = w83782d;
		else if (val1 == 0x40 && vendid == winbond && !is_isa
				&& address == 0x2d)
			kind = w83783s;
		else if (val1 == 0x21 && vendid == winbond)
			kind = w83627hf;
		else if (val1 == 0x31 && !is_isa && address >= 0x28)
			kind = as99127f;
		else {
			if (kind == 0)
				dev_warn(&new_client->dev, "Ignoring 'force' "
					 "parameter for unknown chip at "
					 "adapter %d, address 0x%02x\n",
					 i2c_adapter_id(adapter), address);
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
	strlcpy(new_client->name, client_name, I2C_NAME_SIZE);
	data->type = kind;

	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR2;

	/* attach secondary i2c lm75-like clients */
	if (!is_isa) {
		if ((err = w83781d_detect_subclients(adapter, address,
				kind, new_client)))
			goto ERROR3;
	} else {
		data->lm75[0] = NULL;
		data->lm75[1] = NULL;
	}

	/* Initialize the chip */
	w83781d_init_client(new_client);

	/* A few vars need to be filled upon startup */
	for (i = 1; i <= 3; i++) {
		data->fan_min[i - 1] = w83781d_read_value(new_client,
					W83781D_REG_FAN_MIN(i));
	}
	if (kind != w83781d && kind != as99127f)
		for (i = 0; i < 4; i++)
			data->pwmenable[i] = 1;

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto ERROR4;
	}

	device_create_file_in(new_client, 0);
	if (kind != w83783s)
		device_create_file_in(new_client, 1);
	device_create_file_in(new_client, 2);
	device_create_file_in(new_client, 3);
	device_create_file_in(new_client, 4);
	device_create_file_in(new_client, 5);
	device_create_file_in(new_client, 6);
	if (kind != as99127f && kind != w83781d && kind != w83783s) {
		device_create_file_in(new_client, 7);
		device_create_file_in(new_client, 8);
	}

	device_create_file_fan(new_client, 1);
	device_create_file_fan(new_client, 2);
	device_create_file_fan(new_client, 3);

	device_create_file_temp(new_client, 1);
	device_create_file_temp(new_client, 2);
	if (kind != w83783s)
		device_create_file_temp(new_client, 3);

	device_create_file_vid(new_client);
	device_create_file_vrm(new_client);

	device_create_file_fan_div(new_client, 1);
	device_create_file_fan_div(new_client, 2);
	device_create_file_fan_div(new_client, 3);

	device_create_file_alarms(new_client);

	device_create_file_beep(new_client);

	if (kind != w83781d && kind != as99127f) {
		device_create_file_pwm(new_client, 1);
		device_create_file_pwm(new_client, 2);
		device_create_file_pwmenable(new_client, 2);
	}
	if (kind == w83782d && !is_isa) {
		device_create_file_pwm(new_client, 3);
		device_create_file_pwm(new_client, 4);
	}

	if (kind != as99127f && kind != w83781d) {
		device_create_file_sensor(new_client, 1);
		device_create_file_sensor(new_client, 2);
		if (kind != w83783s)
			device_create_file_sensor(new_client, 3);
	}

	return 0;

ERROR4:
	if (data->lm75[1]) {
		i2c_detach_client(data->lm75[1]);
		kfree(data->lm75[1]);
	}
	if (data->lm75[0]) {
		i2c_detach_client(data->lm75[0]);
		kfree(data->lm75[0]);
	}
ERROR3:
	i2c_detach_client(new_client);
ERROR2:
	kfree(data);
ERROR1:
	if (is_isa)
		release_region(address, W83781D_EXTENT);
ERROR0:
	return err;
}

static int
w83781d_detach_client(struct i2c_client *client)
{
	struct w83781d_data *data = i2c_get_clientdata(client);
	int err;

	/* main client */
	if (data)
		hwmon_device_unregister(data->class_dev);

	if (i2c_is_isa_client(client))
		release_region(client->addr, W83781D_EXTENT);

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

/* The SMBus locks itself, usually, but nothing may access the Winbond between
   bank switches. ISA access must always be locked explicitly! 
   We ignore the W83781D BUSY flag at this moment - it could lead to deadlocks,
   would slow down the W83781D access and should not be necessary. 
   There are some ugly typecasts here, but the good news is - they should
   nowhere else be necessary! */
static int
w83781d_read_value(struct i2c_client *client, u16 reg)
{
	struct w83781d_data *data = i2c_get_clientdata(client);
	int res, word_sized, bank;
	struct i2c_client *cl;

	down(&data->lock);
	if (i2c_is_isa_client(client)) {
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
	up(&data->lock);
	return res;
}

static int
w83781d_write_value(struct i2c_client *client, u16 reg, u16 value)
{
	struct w83781d_data *data = i2c_get_clientdata(client);
	int word_sized, bank;
	struct i2c_client *cl;

	down(&data->lock);
	if (i2c_is_isa_client(client)) {
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
	up(&data->lock);
	return 0;
}

static void
w83781d_init_client(struct i2c_client *client)
{
	struct w83781d_data *data = i2c_get_clientdata(client);
	int i, p;
	int type = data->type;
	u8 tmp;

	if (init && type != as99127f) {	/* this resets registers we don't have
					   documentation for on the as99127f */
		/* save these registers */
		i = w83781d_read_value(client, W83781D_REG_BEEP_CONFIG);
		p = w83781d_read_value(client, W83781D_REG_PWMCLK12);
		/* Reset all except Watchdog values and last conversion values
		   This sets fan-divs to 2, among others */
		w83781d_write_value(client, W83781D_REG_CONFIG, 0x80);
		/* Restore the registers and disable power-on abnormal beep.
		   This saves FAN 1/2/3 input/output values set by BIOS. */
		w83781d_write_value(client, W83781D_REG_BEEP_CONFIG, i | 0x80);
		w83781d_write_value(client, W83781D_REG_PWMCLK12, p);
		/* Disable master beep-enable (reset turns it on).
		   Individual beep_mask should be reset to off but for some reason
		   disabling this bit helps some people not get beeped */
		w83781d_write_value(client, W83781D_REG_BEEP_INTS2, 0);
	}

	data->vrm = vid_which_vrm();

	if ((type != w83781d) && (type != as99127f)) {
		tmp = w83781d_read_value(client, W83781D_REG_SCFG1);
		for (i = 1; i <= 3; i++) {
			if (!(tmp & BIT_SCFG1[i - 1])) {
				data->sens[i - 1] = W83781D_DEFAULT_BETA;
			} else {
				if (w83781d_read_value
				    (client,
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
		tmp = w83781d_read_value(client, W83781D_REG_TEMP2_CONFIG);
		if (tmp & 0x01) {
			dev_warn(&client->dev, "Enabling temp2, readings "
				 "might not make sense\n");
			w83781d_write_value(client, W83781D_REG_TEMP2_CONFIG,
				tmp & 0xfe);
		}

		/* Enable temp3 */
		if (type != w83783s) {
			tmp = w83781d_read_value(client,
				W83781D_REG_TEMP3_CONFIG);
			if (tmp & 0x01) {
				dev_warn(&client->dev, "Enabling temp3, "
					 "readings might not make sense\n");
				w83781d_write_value(client,
					W83781D_REG_TEMP3_CONFIG, tmp & 0xfe);
			}
		}

		if (type != w83781d) {
			/* enable comparator mode for temp2 and temp3 so
			   alarm indication will work correctly */
			i = w83781d_read_value(client, W83781D_REG_IRQ);
			if (!(i & 0x40))
				w83781d_write_value(client, W83781D_REG_IRQ,
						    i | 0x40);
		}
	}

	/* Start monitoring */
	w83781d_write_value(client, W83781D_REG_CONFIG,
			    (w83781d_read_value(client,
						W83781D_REG_CONFIG) & 0xf7)
			    | 0x01);
}

static struct w83781d_data *w83781d_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83781d_data *data = i2c_get_clientdata(client);
	int i;

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		dev_dbg(dev, "Starting device update\n");

		for (i = 0; i <= 8; i++) {
			if (data->type == w83783s && i == 1)
				continue;	/* 783S has no in1 */
			data->in[i] =
			    w83781d_read_value(client, W83781D_REG_IN(i));
			data->in_min[i] =
			    w83781d_read_value(client, W83781D_REG_IN_MIN(i));
			data->in_max[i] =
			    w83781d_read_value(client, W83781D_REG_IN_MAX(i));
			if ((data->type != w83782d)
			    && (data->type != w83627hf) && (i == 6))
				break;
		}
		for (i = 1; i <= 3; i++) {
			data->fan[i - 1] =
			    w83781d_read_value(client, W83781D_REG_FAN(i));
			data->fan_min[i - 1] =
			    w83781d_read_value(client, W83781D_REG_FAN_MIN(i));
		}
		if (data->type != w83781d && data->type != as99127f) {
			for (i = 1; i <= 4; i++) {
				data->pwm[i - 1] =
				    w83781d_read_value(client,
						       W83781D_REG_PWM(i));
				if ((data->type != w83782d
				     || i2c_is_isa_client(client))
				    && i == 2)
					break;
			}
			/* Only PWM2 can be disabled */
			data->pwmenable[1] = (w83781d_read_value(client,
					      W83781D_REG_PWMCLK12) & 0x08) >> 3;
		}

		data->temp = w83781d_read_value(client, W83781D_REG_TEMP(1));
		data->temp_max =
		    w83781d_read_value(client, W83781D_REG_TEMP_OVER(1));
		data->temp_max_hyst =
		    w83781d_read_value(client, W83781D_REG_TEMP_HYST(1));
		data->temp_add[0] =
		    w83781d_read_value(client, W83781D_REG_TEMP(2));
		data->temp_max_add[0] =
		    w83781d_read_value(client, W83781D_REG_TEMP_OVER(2));
		data->temp_max_hyst_add[0] =
		    w83781d_read_value(client, W83781D_REG_TEMP_HYST(2));
		if (data->type != w83783s) {
			data->temp_add[1] =
			    w83781d_read_value(client, W83781D_REG_TEMP(3));
			data->temp_max_add[1] =
			    w83781d_read_value(client,
					       W83781D_REG_TEMP_OVER(3));
			data->temp_max_hyst_add[1] =
			    w83781d_read_value(client,
					       W83781D_REG_TEMP_HYST(3));
		}
		i = w83781d_read_value(client, W83781D_REG_VID_FANDIV);
		data->vid = i & 0x0f;
		data->vid |= (w83781d_read_value(client,
					W83781D_REG_CHIPID) & 0x01) << 4;
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = (i >> 6) & 0x03;
		data->fan_div[2] = (w83781d_read_value(client,
					W83781D_REG_PIN) >> 6) & 0x03;
		if ((data->type != w83781d) && (data->type != as99127f)) {
			i = w83781d_read_value(client, W83781D_REG_VBAT);
			data->fan_div[0] |= (i >> 3) & 0x04;
			data->fan_div[1] |= (i >> 4) & 0x04;
			data->fan_div[2] |= (i >> 5) & 0x04;
		}
		data->alarms =
		    w83781d_read_value(client,
				       W83781D_REG_ALARM1) +
		    (w83781d_read_value(client, W83781D_REG_ALARM2) << 8);
		if ((data->type == w83782d) || (data->type == w83627hf)) {
			data->alarms |=
			    w83781d_read_value(client,
					       W83781D_REG_ALARM3) << 16;
		}
		i = w83781d_read_value(client, W83781D_REG_BEEP_INTS2);
		data->beep_enable = i >> 7;
		data->beep_mask = ((i & 0x7f) << 8) +
		    w83781d_read_value(client, W83781D_REG_BEEP_INTS1);
		if ((data->type != w83781d) && (data->type != as99127f)) {
			data->beep_mask |=
			    w83781d_read_value(client,
					       W83781D_REG_BEEP_INTS3) << 16;
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __init
sensors_w83781d_init(void)
{
	int res;

	res = i2c_add_driver(&w83781d_driver);
	if (res)
		return res;

	res = i2c_isa_add_driver(&w83781d_isa_driver);
	if (res) {
		i2c_del_driver(&w83781d_driver);
		return res;
	}

	return 0;
}

static void __exit
sensors_w83781d_exit(void)
{
	i2c_isa_del_driver(&w83781d_isa_driver);
	i2c_del_driver(&w83781d_driver);
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>, "
	      "Philip Edelbrock <phil@netroedge.com>, "
	      "and Mark Studebaker <mdsxyz123@yahoo.com>");
MODULE_DESCRIPTION("W83781D driver");
MODULE_LICENSE("GPL");

module_init(sensors_w83781d_init);
module_exit(sensors_w83781d_exit);
