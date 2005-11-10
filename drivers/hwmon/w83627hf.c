/*
    w83627hf.c - Part of lm_sensors, Linux kernel modules for hardware
                monitoring
    Copyright (c) 1998 - 2003  Frodo Looijaard <frodol@dds.nl>,
    Philip Edelbrock <phil@netroedge.com>,
    and Mark Studebaker <mdsxyz123@yahoo.com>
    Ported to 2.6 by Bernhard C. Schrenk <clemy@clemy.org>

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
    w83627hf	9	3	2	3	0x20	0x5ca3	no	yes(LPC)
    w83627thf	7	3	3	3	0x90	0x5ca3	no	yes(LPC)
    w83637hf	7	3	3	3	0x80	0x5ca3	no	yes(LPC)
    w83697hf	8	2	2	2	0x60	0x5ca3	no	yes(LPC)

    For other winbond chips, and for i2c support in the above chips,
    use w83781d.c.

    Note: automatic ("cruise") fan control for 697, 637 & 627thf not
    supported yet.
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

static u16 force_addr;
module_param(force_addr, ushort, 0);
MODULE_PARM_DESC(force_addr,
		 "Initialize the base address of the sensors");
static u8 force_i2c = 0x1f;
module_param(force_i2c, byte, 0);
MODULE_PARM_DESC(force_i2c,
		 "Initialize the i2c address of the sensors");

/* The actual ISA address is read from Super-I/O configuration space */
static unsigned short address;

/* Insmod parameters */
enum chips { any_chip, w83627hf, w83627thf, w83697hf, w83637hf };

static int reset;
module_param(reset, bool, 0);
MODULE_PARM_DESC(reset, "Set to one to reset chip on load");

static int init = 1;
module_param(init, bool, 0);
MODULE_PARM_DESC(init, "Set to zero to bypass chip initialization");

/* modified from kernel/include/traps.c */
static int REG;		/* The register to read/write */
#define	DEV	0x07	/* Register: Logical device select */
static int VAL;		/* The value to read/write */

/* logical device numbers for superio_select (below) */
#define W83627HF_LD_FDC		0x00
#define W83627HF_LD_PRT		0x01
#define W83627HF_LD_UART1	0x02
#define W83627HF_LD_UART2	0x03
#define W83627HF_LD_KBC		0x05
#define W83627HF_LD_CIR		0x06 /* w83627hf only */
#define W83627HF_LD_GAME	0x07
#define W83627HF_LD_MIDI	0x07
#define W83627HF_LD_GPIO1	0x07
#define W83627HF_LD_GPIO5	0x07 /* w83627thf only */
#define W83627HF_LD_GPIO2	0x08
#define W83627HF_LD_GPIO3	0x09
#define W83627HF_LD_GPIO4	0x09 /* w83627thf only */
#define W83627HF_LD_ACPI	0x0a
#define W83627HF_LD_HWM		0x0b

#define	DEVID	0x20	/* Register: Device ID */

#define W83627THF_GPIO5_EN	0x30 /* w83627thf only */
#define W83627THF_GPIO5_IOSR	0xf3 /* w83627thf only */
#define W83627THF_GPIO5_DR	0xf4 /* w83627thf only */

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
	outb(DEV, REG);
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
	outb(0xAA, REG);
}

#define W627_DEVID 0x52
#define W627THF_DEVID 0x82
#define W697_DEVID 0x60
#define W637_DEVID 0x70
#define WINB_ACT_REG 0x30
#define WINB_BASE_REG 0x60
/* Constants specified below */

/* Alignment of the base address */
#define WINB_ALIGNMENT		~7

/* Offset & size of I/O region we are interested in */
#define WINB_REGION_OFFSET	5
#define WINB_REGION_SIZE	2

/* Where are the sensors address/data registers relative to the base address */
#define W83781D_ADDR_REG_OFFSET 5
#define W83781D_DATA_REG_OFFSET 6

/* The W83781D registers */
/* The W83782D registers for nr=7,8 are in bank 5 */
#define W83781D_REG_IN_MAX(nr) ((nr < 7) ? (0x2b + (nr) * 2) : \
					   (0x554 + (((nr) - 7) * 2)))
#define W83781D_REG_IN_MIN(nr) ((nr < 7) ? (0x2c + (nr) * 2) : \
					   (0x555 + (((nr) - 7) * 2)))
#define W83781D_REG_IN(nr)     ((nr < 7) ? (0x20 + (nr)) : \
					   (0x550 + (nr) - 7))

#define W83781D_REG_FAN_MIN(nr) (0x3a + (nr))
#define W83781D_REG_FAN(nr) (0x27 + (nr))

#define W83781D_REG_TEMP2_CONFIG 0x152
#define W83781D_REG_TEMP3_CONFIG 0x252
#define W83781D_REG_TEMP(nr)		((nr == 3) ? (0x0250) : \
					((nr == 2) ? (0x0150) : \
					             (0x27)))
#define W83781D_REG_TEMP_HYST(nr)	((nr == 3) ? (0x253) : \
					((nr == 2) ? (0x153) : \
					             (0x3A)))
#define W83781D_REG_TEMP_OVER(nr)	((nr == 3) ? (0x255) : \
					((nr == 2) ? (0x155) : \
					             (0x39)))

#define W83781D_REG_BANK 0x4E

#define W83781D_REG_CONFIG 0x40
#define W83781D_REG_ALARM1 0x459
#define W83781D_REG_ALARM2 0x45A
#define W83781D_REG_ALARM3 0x45B

#define W83781D_REG_BEEP_CONFIG 0x4D
#define W83781D_REG_BEEP_INTS1 0x56
#define W83781D_REG_BEEP_INTS2 0x57
#define W83781D_REG_BEEP_INTS3 0x453

#define W83781D_REG_VID_FANDIV 0x47

#define W83781D_REG_CHIPID 0x49
#define W83781D_REG_WCHIPID 0x58
#define W83781D_REG_CHIPMAN 0x4F
#define W83781D_REG_PIN 0x4B

#define W83781D_REG_VBAT 0x5D

#define W83627HF_REG_PWM1 0x5A
#define W83627HF_REG_PWM2 0x5B

#define W83627THF_REG_PWM1		0x01	/* 697HF and 637HF too */
#define W83627THF_REG_PWM2		0x03	/* 697HF and 637HF too */
#define W83627THF_REG_PWM3		0x11	/* 637HF too */

#define W83627THF_REG_VRM_OVT_CFG 	0x18	/* 637HF too */

static const u8 regpwm_627hf[] = { W83627HF_REG_PWM1, W83627HF_REG_PWM2 };
static const u8 regpwm[] = { W83627THF_REG_PWM1, W83627THF_REG_PWM2,
                             W83627THF_REG_PWM3 };
#define W836X7HF_REG_PWM(type, nr) (((type) == w83627hf) ? \
                                     regpwm_627hf[(nr) - 1] : regpwm[(nr) - 1])

#define W83781D_REG_I2C_ADDR 0x48
#define W83781D_REG_I2C_SUBADDR 0x4A

/* Sensor selection */
#define W83781D_REG_SCFG1 0x5D
static const u8 BIT_SCFG1[] = { 0x02, 0x04, 0x08 };
#define W83781D_REG_SCFG2 0x59
static const u8 BIT_SCFG2[] = { 0x10, 0x20, 0x40 };
#define W83781D_DEFAULT_BETA 3435

/* Conversions. Limit checking is only done on the TO_REG
   variants. Note that you should be a bit careful with which arguments
   these macros are called: arguments may be evaluated more than once.
   Fixing this is just not worth it. */
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

#define TEMP_MIN (-128000)
#define TEMP_MAX ( 127000)

/* TEMP: 0.001C/bit (-128C to +127C)
   REG: 1C/bit, two's complement */
static u8 TEMP_TO_REG(int temp)
{
        int ntemp = SENSORS_LIMIT(temp, TEMP_MIN, TEMP_MAX);
        ntemp += (ntemp<0 ? -500 : 500);
        return (u8)(ntemp / 1000);
}

static int TEMP_FROM_REG(u8 reg)
{
        return (s8)reg * 1000;
}

#define FAN_FROM_REG(val,div) ((val)==0?-1:(val)==255?0:1350000/((val)*(div)))

#define PWM_TO_REG(val) (SENSORS_LIMIT((val),0,255))

#define BEEP_MASK_FROM_REG(val)		 (val)
#define BEEP_MASK_TO_REG(val)		((val) & 0xffffff)
#define BEEP_ENABLE_TO_REG(val)		((val)?1:0)
#define BEEP_ENABLE_FROM_REG(val)	((val)?1:0)

#define DIV_FROM_REG(val) (1 << (val))

static inline u8 DIV_TO_REG(long val)
{
	int i;
	val = SENSORS_LIMIT(val, 1, 128) >> 1;
	for (i = 0; i < 7; i++) {
		if (val == 0)
			break;
		val >>= 1;
	}
	return ((u8) i);
}

/* For each registered chip, we need to keep some data in memory. That
   data is pointed to by w83627hf_list[NR]->data. The structure itself is
   dynamically allocated, at the same time when a new client is allocated. */
struct w83627hf_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore lock;
	enum chips type;

	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	struct i2c_client *lm75;	/* for secondary I2C addresses */
	/* pointer to array of 2 subclients */

	u8 in[9];		/* Register value */
	u8 in_max[9];		/* Register value */
	u8 in_min[9];		/* Register value */
	u8 fan[3];		/* Register value */
	u8 fan_min[3];		/* Register value */
	u8 temp;
	u8 temp_max;		/* Register value */
	u8 temp_max_hyst;	/* Register value */
	u16 temp_add[2];	/* Register value */
	u16 temp_max_add[2];	/* Register value */
	u16 temp_max_hyst_add[2]; /* Register value */
	u8 fan_div[3];		/* Register encoding, shifted right */
	u8 vid;			/* Register encoding, combined */
	u32 alarms;		/* Register encoding, combined */
	u32 beep_mask;		/* Register encoding, combined */
	u8 beep_enable;		/* Boolean */
	u8 pwm[3];		/* Register value */
	u16 sens[3];		/* 782D/783S only.
				   1 = pentium diode; 2 = 3904 diode;
				   3000-5000 = thermistor beta.
				   Default = 3435.
				   Other Betas unimplemented */
	u8 vrm;
	u8 vrm_ovt;		/* Register value, 627thf & 637hf only */
};


static int w83627hf_detect(struct i2c_adapter *adapter);
static int w83627hf_detach_client(struct i2c_client *client);

static int w83627hf_read_value(struct i2c_client *client, u16 register);
static int w83627hf_write_value(struct i2c_client *client, u16 register,
			       u16 value);
static struct w83627hf_data *w83627hf_update_device(struct device *dev);
static void w83627hf_init_client(struct i2c_client *client);

static struct i2c_driver w83627hf_driver = {
	.owner		= THIS_MODULE,
	.name		= "w83627hf",
	.attach_adapter	= w83627hf_detect,
	.detach_client	= w83627hf_detach_client,
};

/* following are the sysfs callback functions */
#define show_in_reg(reg) \
static ssize_t show_##reg (struct device *dev, char *buf, int nr) \
{ \
	struct w83627hf_data *data = w83627hf_update_device(dev); \
	return sprintf(buf,"%ld\n", (long)IN_FROM_REG(data->reg[nr])); \
}
show_in_reg(in)
show_in_reg(in_min)
show_in_reg(in_max)

#define store_in_reg(REG, reg) \
static ssize_t \
store_in_##reg (struct device *dev, const char *buf, size_t count, int nr) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct w83627hf_data *data = i2c_get_clientdata(client); \
	u32 val; \
	 \
	val = simple_strtoul(buf, NULL, 10); \
	 \
	down(&data->update_lock); \
	data->in_##reg[nr] = IN_TO_REG(val); \
	w83627hf_write_value(client, W83781D_REG_IN_##REG(nr), \
			    data->in_##reg[nr]); \
	 \
	up(&data->update_lock); \
	return count; \
}
store_in_reg(MIN, min)
store_in_reg(MAX, max)

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
static ssize_t \
store_regs_in_##reg##offset (struct device *dev, struct device_attribute *attr, \
			    const char *buf, size_t count) \
{ \
	return store_in_##reg (dev, buf, count, offset); \
} \
static DEVICE_ATTR(in##offset##_##reg, S_IRUGO| S_IWUSR, \
		  show_regs_in_##reg##offset, store_regs_in_##reg##offset);

#define sysfs_in_offsets(offset) \
sysfs_in_offset(offset) \
sysfs_in_reg_offset(min, offset) \
sysfs_in_reg_offset(max, offset)

sysfs_in_offsets(1);
sysfs_in_offsets(2);
sysfs_in_offsets(3);
sysfs_in_offsets(4);
sysfs_in_offsets(5);
sysfs_in_offsets(6);
sysfs_in_offsets(7);
sysfs_in_offsets(8);

/* use a different set of functions for in0 */
static ssize_t show_in_0(struct w83627hf_data *data, char *buf, u8 reg)
{
	long in0;

	if ((data->vrm_ovt & 0x01) &&
		(w83627thf == data->type || w83637hf == data->type))

		/* use VRM9 calculation */
		in0 = (long)((reg * 488 + 70000 + 50) / 100);
	else
		/* use VRM8 (standard) calculation */
		in0 = (long)IN_FROM_REG(reg);

	return sprintf(buf,"%ld\n", in0);
}

static ssize_t show_regs_in_0(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return show_in_0(data, buf, data->in[0]);
}

static ssize_t show_regs_in_min0(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return show_in_0(data, buf, data->in_min[0]);
}

static ssize_t show_regs_in_max0(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return show_in_0(data, buf, data->in_max[0]);
}

static ssize_t store_regs_in_min0(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627hf_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	
	if ((data->vrm_ovt & 0x01) &&
		(w83627thf == data->type || w83637hf == data->type))

		/* use VRM9 calculation */
		data->in_min[0] = (u8)(((val * 100) - 70000 + 244) / 488);
	else
		/* use VRM8 (standard) calculation */
		data->in_min[0] = IN_TO_REG(val);

	w83627hf_write_value(client, W83781D_REG_IN_MIN(0), data->in_min[0]);
	up(&data->update_lock);
	return count;
}

static ssize_t store_regs_in_max0(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627hf_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);

	if ((data->vrm_ovt & 0x01) &&
		(w83627thf == data->type || w83637hf == data->type))
		
		/* use VRM9 calculation */
		data->in_max[0] = (u8)(((val * 100) - 70000 + 244) / 488);
	else
		/* use VRM8 (standard) calculation */
		data->in_max[0] = IN_TO_REG(val);

	w83627hf_write_value(client, W83781D_REG_IN_MAX(0), data->in_max[0]);
	up(&data->update_lock);
	return count;
}

static DEVICE_ATTR(in0_input, S_IRUGO, show_regs_in_0, NULL);
static DEVICE_ATTR(in0_min, S_IRUGO | S_IWUSR,
	show_regs_in_min0, store_regs_in_min0);
static DEVICE_ATTR(in0_max, S_IRUGO | S_IWUSR,
	show_regs_in_max0, store_regs_in_max0);

#define device_create_file_in(client, offset) \
do { \
device_create_file(&client->dev, &dev_attr_in##offset##_input); \
device_create_file(&client->dev, &dev_attr_in##offset##_min); \
device_create_file(&client->dev, &dev_attr_in##offset##_max); \
} while (0)

#define show_fan_reg(reg) \
static ssize_t show_##reg (struct device *dev, char *buf, int nr) \
{ \
	struct w83627hf_data *data = w83627hf_update_device(dev); \
	return sprintf(buf,"%ld\n", \
		FAN_FROM_REG(data->reg[nr-1], \
			    (long)DIV_FROM_REG(data->fan_div[nr-1]))); \
}
show_fan_reg(fan);
show_fan_reg(fan_min);

static ssize_t
store_fan_min(struct device *dev, const char *buf, size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627hf_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	data->fan_min[nr - 1] =
	    FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr - 1]));
	w83627hf_write_value(client, W83781D_REG_FAN_MIN(nr),
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
static ssize_t \
store_regs_fan_min##offset (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
	return store_fan_min(dev, buf, count, offset); \
} \
static DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR, \
		  show_regs_fan_min##offset, store_regs_fan_min##offset);

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
	struct w83627hf_data *data = w83627hf_update_device(dev); \
	if (nr >= 2) {	/* TEMP2 and TEMP3 */ \
		return sprintf(buf,"%ld\n", \
			(long)LM75_TEMP_FROM_REG(data->reg##_add[nr-2])); \
	} else {	/* TEMP1 */ \
		return sprintf(buf,"%ld\n", (long)TEMP_FROM_REG(data->reg)); \
	} \
}
show_temp_reg(temp);
show_temp_reg(temp_max);
show_temp_reg(temp_max_hyst);

#define store_temp_reg(REG, reg) \
static ssize_t \
store_temp_##reg (struct device *dev, const char *buf, size_t count, int nr) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct w83627hf_data *data = i2c_get_clientdata(client); \
	u32 val; \
	 \
	val = simple_strtoul(buf, NULL, 10); \
	 \
	down(&data->update_lock); \
	 \
	if (nr >= 2) {	/* TEMP2 and TEMP3 */ \
		data->temp_##reg##_add[nr-2] = LM75_TEMP_TO_REG(val); \
		w83627hf_write_value(client, W83781D_REG_TEMP_##REG(nr), \
				data->temp_##reg##_add[nr-2]); \
	} else {	/* TEMP1 */ \
		data->temp_##reg = TEMP_TO_REG(val); \
		w83627hf_write_value(client, W83781D_REG_TEMP_##REG(nr), \
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
static ssize_t \
store_regs_temp_##reg##offset (struct device *dev, struct device_attribute *attr, \
			      const char *buf, size_t count) \
{ \
	return store_temp_##reg (dev, buf, count, offset); \
} \
static DEVICE_ATTR(temp##offset##_##reg, S_IRUGO| S_IWUSR, \
		  show_regs_temp_##reg##offset, store_regs_temp_##reg##offset);

#define sysfs_temp_offsets(offset) \
sysfs_temp_offset(offset) \
sysfs_temp_reg_offset(max, offset) \
sysfs_temp_reg_offset(max_hyst, offset)

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
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid_reg, NULL);
#define device_create_file_vid(client) \
device_create_file(&client->dev, &dev_attr_cpu0_vid)

static ssize_t
show_vrm_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->vrm);
}
static ssize_t
store_vrm_reg(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627hf_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);
	data->vrm = val;

	return count;
}
static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm_reg, store_vrm_reg);
#define device_create_file_vrm(client) \
device_create_file(&client->dev, &dev_attr_vrm)

static ssize_t
show_alarms_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->alarms);
}
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms_reg, NULL);
#define device_create_file_alarms(client) \
device_create_file(&client->dev, &dev_attr_alarms)

#define show_beep_reg(REG, reg) \
static ssize_t show_beep_##reg (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct w83627hf_data *data = w83627hf_update_device(dev); \
	return sprintf(buf,"%ld\n", \
		      (long)BEEP_##REG##_FROM_REG(data->beep_##reg)); \
}
show_beep_reg(ENABLE, enable)
show_beep_reg(MASK, mask)

#define BEEP_ENABLE			0	/* Store beep_enable */
#define BEEP_MASK			1	/* Store beep_mask */

static ssize_t
store_beep_reg(struct device *dev, const char *buf, size_t count,
	       int update_mask)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627hf_data *data = i2c_get_clientdata(client);
	u32 val, val2;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);

	if (update_mask == BEEP_MASK) {	/* We are storing beep_mask */
		data->beep_mask = BEEP_MASK_TO_REG(val);
		w83627hf_write_value(client, W83781D_REG_BEEP_INTS1,
				    data->beep_mask & 0xff);
		w83627hf_write_value(client, W83781D_REG_BEEP_INTS3,
				    ((data->beep_mask) >> 16) & 0xff);
		val2 = (data->beep_mask >> 8) & 0x7f;
	} else {		/* We are storing beep_enable */
		val2 =
		    w83627hf_read_value(client, W83781D_REG_BEEP_INTS2) & 0x7f;
		data->beep_enable = BEEP_ENABLE_TO_REG(val);
	}

	w83627hf_write_value(client, W83781D_REG_BEEP_INTS2,
			    val2 | data->beep_enable << 7);

	up(&data->update_lock);
	return count;
}

#define sysfs_beep(REG, reg) \
static ssize_t show_regs_beep_##reg (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_beep_##reg(dev, attr, buf); \
} \
static ssize_t \
store_regs_beep_##reg (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
	return store_beep_reg(dev, buf, count, BEEP_##REG); \
} \
static DEVICE_ATTR(beep_##reg, S_IRUGO | S_IWUSR, \
		  show_regs_beep_##reg, store_regs_beep_##reg);

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
	struct w83627hf_data *data = w83627hf_update_device(dev);
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
	struct w83627hf_data *data = i2c_get_clientdata(client);
	unsigned long min;
	u8 reg;
	unsigned long val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);

	/* Save fan_min */
	min = FAN_FROM_REG(data->fan_min[nr],
			   DIV_FROM_REG(data->fan_div[nr]));

	data->fan_div[nr] = DIV_TO_REG(val);

	reg = (w83627hf_read_value(client, nr==2 ? W83781D_REG_PIN : W83781D_REG_VID_FANDIV)
	       & (nr==0 ? 0xcf : 0x3f))
	    | ((data->fan_div[nr] & 0x03) << (nr==0 ? 4 : 6));
	w83627hf_write_value(client, nr==2 ? W83781D_REG_PIN : W83781D_REG_VID_FANDIV, reg);

	reg = (w83627hf_read_value(client, W83781D_REG_VBAT)
	       & ~(1 << (5 + nr)))
	    | ((data->fan_div[nr] & 0x04) << (3 + nr));
	w83627hf_write_value(client, W83781D_REG_VBAT, reg);

	/* Restore fan_min */
	data->fan_min[nr] = FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	w83627hf_write_value(client, W83781D_REG_FAN_MIN(nr+1), data->fan_min[nr]);

	up(&data->update_lock);
	return count;
}

#define sysfs_fan_div(offset) \
static ssize_t show_regs_fan_div_##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_fan_div_reg(dev, buf, offset); \
} \
static ssize_t \
store_regs_fan_div_##offset (struct device *dev, struct device_attribute *attr, \
			    const char *buf, size_t count) \
{ \
	return store_fan_div_reg(dev, buf, count, offset - 1); \
} \
static DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR, \
		  show_regs_fan_div_##offset, store_regs_fan_div_##offset);

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
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->pwm[nr - 1]);
}

static ssize_t
store_pwm_reg(struct device *dev, const char *buf, size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627hf_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);

	if (data->type == w83627thf) {
		/* bits 0-3 are reserved  in 627THF */
		data->pwm[nr - 1] = PWM_TO_REG(val) & 0xf0;
		w83627hf_write_value(client,
				     W836X7HF_REG_PWM(data->type, nr),
				     data->pwm[nr - 1] |
				     (w83627hf_read_value(client,
				     W836X7HF_REG_PWM(data->type, nr)) & 0x0f));
	} else {
		data->pwm[nr - 1] = PWM_TO_REG(val);
		w83627hf_write_value(client,
				     W836X7HF_REG_PWM(data->type, nr),
				     data->pwm[nr - 1]);
	}

	up(&data->update_lock);
	return count;
}

#define sysfs_pwm(offset) \
static ssize_t show_regs_pwm_##offset (struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return show_pwm_reg(dev, buf, offset); \
} \
static ssize_t \
store_regs_pwm_##offset (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
	return store_pwm_reg(dev, buf, count, offset); \
} \
static DEVICE_ATTR(pwm##offset, S_IRUGO | S_IWUSR, \
		  show_regs_pwm_##offset, store_regs_pwm_##offset);

sysfs_pwm(1);
sysfs_pwm(2);
sysfs_pwm(3);

#define device_create_file_pwm(client, offset) \
do { \
device_create_file(&client->dev, &dev_attr_pwm##offset); \
} while (0)

static ssize_t
show_sensor_reg(struct device *dev, char *buf, int nr)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->sens[nr - 1]);
}

static ssize_t
store_sensor_reg(struct device *dev, const char *buf, size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627hf_data *data = i2c_get_clientdata(client);
	u32 val, tmp;

	val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);

	switch (val) {
	case 1:		/* PII/Celeron diode */
		tmp = w83627hf_read_value(client, W83781D_REG_SCFG1);
		w83627hf_write_value(client, W83781D_REG_SCFG1,
				    tmp | BIT_SCFG1[nr - 1]);
		tmp = w83627hf_read_value(client, W83781D_REG_SCFG2);
		w83627hf_write_value(client, W83781D_REG_SCFG2,
				    tmp | BIT_SCFG2[nr - 1]);
		data->sens[nr - 1] = val;
		break;
	case 2:		/* 3904 */
		tmp = w83627hf_read_value(client, W83781D_REG_SCFG1);
		w83627hf_write_value(client, W83781D_REG_SCFG1,
				    tmp | BIT_SCFG1[nr - 1]);
		tmp = w83627hf_read_value(client, W83781D_REG_SCFG2);
		w83627hf_write_value(client, W83781D_REG_SCFG2,
				    tmp & ~BIT_SCFG2[nr - 1]);
		data->sens[nr - 1] = val;
		break;
	case W83781D_DEFAULT_BETA:	/* thermistor */
		tmp = w83627hf_read_value(client, W83781D_REG_SCFG1);
		w83627hf_write_value(client, W83781D_REG_SCFG1,
				    tmp & ~BIT_SCFG1[nr - 1]);
		data->sens[nr - 1] = val;
		break;
	default:
		dev_err(&client->dev,
		       "Invalid sensor type %ld; must be 1, 2, or %d\n",
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
static ssize_t \
store_regs_sensor_##offset (struct device *dev, struct device_attribute *attr, const char *buf, size_t count) \
{ \
    return store_sensor_reg(dev, buf, count, offset); \
} \
static DEVICE_ATTR(temp##offset##_type, S_IRUGO | S_IWUSR, \
		  show_regs_sensor_##offset, store_regs_sensor_##offset);

sysfs_sensor(1);
sysfs_sensor(2);
sysfs_sensor(3);

#define device_create_file_sensor(client, offset) \
do { \
device_create_file(&client->dev, &dev_attr_temp##offset##_type); \
} while (0)


static int __init w83627hf_find(int sioaddr, unsigned short *addr)
{
	u16 val;

	REG = sioaddr;
	VAL = sioaddr + 1;

	superio_enter();
	val= superio_inb(DEVID);
	if(val != W627_DEVID &&
	   val != W627THF_DEVID &&
	   val != W697_DEVID &&
	   val != W637_DEVID) {
		superio_exit();
		return -ENODEV;
	}

	superio_select(W83627HF_LD_HWM);
	val = (superio_inb(WINB_BASE_REG) << 8) |
	       superio_inb(WINB_BASE_REG + 1);
	*addr = val & WINB_ALIGNMENT;
	if (*addr == 0 && force_addr == 0) {
		superio_exit();
		return -ENODEV;
	}

	superio_exit();
	return 0;
}

static int w83627hf_detect(struct i2c_adapter *adapter)
{
	int val, kind;
	struct i2c_client *new_client;
	struct w83627hf_data *data;
	int err = 0;
	const char *client_name = "";

	if(force_addr)
		address = force_addr & WINB_ALIGNMENT;

	if (!request_region(address + WINB_REGION_OFFSET, WINB_REGION_SIZE,
	                    w83627hf_driver.name)) {
		err = -EBUSY;
		goto ERROR0;
	}

	if(force_addr) {
		printk("w83627hf.o: forcing ISA address 0x%04X\n", address);
		superio_enter();
		superio_select(W83627HF_LD_HWM);
		superio_outb(WINB_BASE_REG, address >> 8);
		superio_outb(WINB_BASE_REG+1, address & 0xff);
		superio_exit();
	}

	superio_enter();
	val= superio_inb(DEVID);
	if(val == W627_DEVID)
		kind = w83627hf;
	else if(val == W697_DEVID)
		kind = w83697hf;
	else if(val == W627THF_DEVID)
		kind = w83627thf;
	else if(val == W637_DEVID)
		kind = w83637hf;
	else {
		dev_info(&adapter->dev,
			 "Unsupported chip (dev_id=0x%02X).\n", val);
		goto ERROR1;
	}

	superio_select(W83627HF_LD_HWM);
	if((val = 0x01 & superio_inb(WINB_ACT_REG)) == 0)
		superio_outb(WINB_ACT_REG, 1);
	superio_exit();

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access w83627hf_{read,write}_value. */

	if (!(data = kzalloc(sizeof(struct w83627hf_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR1;
	}

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	init_MUTEX(&data->lock);
	new_client->adapter = adapter;
	new_client->driver = &w83627hf_driver;
	new_client->flags = 0;


	if (kind == w83627hf) {
		client_name = "w83627hf";
	} else if (kind == w83627thf) {
		client_name = "w83627thf";
	} else if (kind == w83697hf) {
		client_name = "w83697hf";
	} else if (kind == w83637hf) {
		client_name = "w83637hf";
	}

	/* Fill in the remaining client fields and put into the global list */
	strlcpy(new_client->name, client_name, I2C_NAME_SIZE);
	data->type = kind;
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR2;

	data->lm75 = NULL;

	/* Initialize the chip */
	w83627hf_init_client(new_client);

	/* A few vars need to be filled upon startup */
	data->fan_min[0] = w83627hf_read_value(new_client, W83781D_REG_FAN_MIN(1));
	data->fan_min[1] = w83627hf_read_value(new_client, W83781D_REG_FAN_MIN(2));
	data->fan_min[2] = w83627hf_read_value(new_client, W83781D_REG_FAN_MIN(3));

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto ERROR3;
	}

	device_create_file_in(new_client, 0);
	if (kind != w83697hf)
		device_create_file_in(new_client, 1);
	device_create_file_in(new_client, 2);
	device_create_file_in(new_client, 3);
	device_create_file_in(new_client, 4);
	if (kind != w83627thf && kind != w83637hf) {
		device_create_file_in(new_client, 5);
		device_create_file_in(new_client, 6);
	}
	device_create_file_in(new_client, 7);
	device_create_file_in(new_client, 8);

	device_create_file_fan(new_client, 1);
	device_create_file_fan(new_client, 2);
	if (kind != w83697hf)
		device_create_file_fan(new_client, 3);

	device_create_file_temp(new_client, 1);
	device_create_file_temp(new_client, 2);
	if (kind != w83697hf)
		device_create_file_temp(new_client, 3);

	if (kind != w83697hf)
		device_create_file_vid(new_client);

	if (kind != w83697hf)
		device_create_file_vrm(new_client);

	device_create_file_fan_div(new_client, 1);
	device_create_file_fan_div(new_client, 2);
	if (kind != w83697hf)
		device_create_file_fan_div(new_client, 3);

	device_create_file_alarms(new_client);

	device_create_file_beep(new_client);

	device_create_file_pwm(new_client, 1);
	device_create_file_pwm(new_client, 2);
	if (kind == w83627thf || kind == w83637hf)
		device_create_file_pwm(new_client, 3);

	device_create_file_sensor(new_client, 1);
	device_create_file_sensor(new_client, 2);
	if (kind != w83697hf)
		device_create_file_sensor(new_client, 3);

	return 0;

      ERROR3:
	i2c_detach_client(new_client);
      ERROR2:
	kfree(data);
      ERROR1:
	release_region(address + WINB_REGION_OFFSET, WINB_REGION_SIZE);
      ERROR0:
	return err;
}

static int w83627hf_detach_client(struct i2c_client *client)
{
	struct w83627hf_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	release_region(client->addr + WINB_REGION_OFFSET, WINB_REGION_SIZE);
	kfree(data);

	return 0;
}


/*
   ISA access must always be locked explicitly!
   We ignore the W83781D BUSY flag at this moment - it could lead to deadlocks,
   would slow down the W83781D access and should not be necessary.
   There are some ugly typecasts here, but the good news is - they should
   nowhere else be necessary! */
static int w83627hf_read_value(struct i2c_client *client, u16 reg)
{
	struct w83627hf_data *data = i2c_get_clientdata(client);
	int res, word_sized;

	down(&data->lock);
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
	up(&data->lock);
	return res;
}

static int w83627thf_read_gpio5(struct i2c_client *client)
{
	int res = 0xff, sel;

	superio_enter();
	superio_select(W83627HF_LD_GPIO5);

	/* Make sure these GPIO pins are enabled */
	if (!(superio_inb(W83627THF_GPIO5_EN) & (1<<3))) {
		dev_dbg(&client->dev, "GPIO5 disabled, no VID function\n");
		goto exit;
	}

	/* Make sure the pins are configured for input
	   There must be at least five (VRM 9), and possibly 6 (VRM 10) */
	sel = superio_inb(W83627THF_GPIO5_IOSR);
	if ((sel & 0x1f) != 0x1f) {
		dev_dbg(&client->dev, "GPIO5 not configured for VID "
			"function\n");
		goto exit;
	}

	dev_info(&client->dev, "Reading VID from GPIO5\n");
	res = superio_inb(W83627THF_GPIO5_DR) & sel;

exit:
	superio_exit();
	return res;
}

static int w83627hf_write_value(struct i2c_client *client, u16 reg, u16 value)
{
	struct w83627hf_data *data = i2c_get_clientdata(client);
	int word_sized;

	down(&data->lock);
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
	outb_p(value & 0xff,
	       client->addr + W83781D_DATA_REG_OFFSET);
	if (reg & 0xff00) {
		outb_p(W83781D_REG_BANK,
		       client->addr + W83781D_ADDR_REG_OFFSET);
		outb_p(0, client->addr + W83781D_DATA_REG_OFFSET);
	}
	up(&data->lock);
	return 0;
}

static void w83627hf_init_client(struct i2c_client *client)
{
	struct w83627hf_data *data = i2c_get_clientdata(client);
	int i;
	int type = data->type;
	u8 tmp;

	if (reset) {
		/* Resetting the chip has been the default for a long time,
		   but repeatedly caused problems (fans going to full
		   speed...) so it is now optional. It might even go away if
		   nobody reports it as being useful, as I see very little
		   reason why this would be needed at all. */
		dev_info(&client->dev, "If reset=1 solved a problem you were "
			 "having, please report!\n");

		/* save this register */
		i = w83627hf_read_value(client, W83781D_REG_BEEP_CONFIG);
		/* Reset all except Watchdog values and last conversion values
		   This sets fan-divs to 2, among others */
		w83627hf_write_value(client, W83781D_REG_CONFIG, 0x80);
		/* Restore the register and disable power-on abnormal beep.
		   This saves FAN 1/2/3 input/output values set by BIOS. */
		w83627hf_write_value(client, W83781D_REG_BEEP_CONFIG, i | 0x80);
		/* Disable master beep-enable (reset turns it on).
		   Individual beeps should be reset to off but for some reason
		   disabling this bit helps some people not get beeped */
		w83627hf_write_value(client, W83781D_REG_BEEP_INTS2, 0);
	}

	/* Minimize conflicts with other winbond i2c-only clients...  */
	/* disable i2c subclients... how to disable main i2c client?? */
	/* force i2c address to relatively uncommon address */
	w83627hf_write_value(client, W83781D_REG_I2C_SUBADDR, 0x89);
	w83627hf_write_value(client, W83781D_REG_I2C_ADDR, force_i2c);

	/* Read VID only once */
	if (w83627hf == data->type || w83637hf == data->type) {
		int lo = w83627hf_read_value(client, W83781D_REG_VID_FANDIV);
		int hi = w83627hf_read_value(client, W83781D_REG_CHIPID);
		data->vid = (lo & 0x0f) | ((hi & 0x01) << 4);
	} else if (w83627thf == data->type) {
		data->vid = w83627thf_read_gpio5(client) & 0x3f;
	}

	/* Read VRM & OVT Config only once */
	if (w83627thf == data->type || w83637hf == data->type) {
		data->vrm_ovt = 
			w83627hf_read_value(client, W83627THF_REG_VRM_OVT_CFG);
		data->vrm = (data->vrm_ovt & 0x01) ? 90 : 82;
	} else {
		/* Convert VID to voltage based on default VRM */
		data->vrm = vid_which_vrm();
	}

	tmp = w83627hf_read_value(client, W83781D_REG_SCFG1);
	for (i = 1; i <= 3; i++) {
		if (!(tmp & BIT_SCFG1[i - 1])) {
			data->sens[i - 1] = W83781D_DEFAULT_BETA;
		} else {
			if (w83627hf_read_value
			    (client,
			     W83781D_REG_SCFG2) & BIT_SCFG2[i - 1])
				data->sens[i - 1] = 1;
			else
				data->sens[i - 1] = 2;
		}
		if ((type == w83697hf) && (i == 2))
			break;
	}

	if(init) {
		/* Enable temp2 */
		tmp = w83627hf_read_value(client, W83781D_REG_TEMP2_CONFIG);
		if (tmp & 0x01) {
			dev_warn(&client->dev, "Enabling temp2, readings "
				 "might not make sense\n");
			w83627hf_write_value(client, W83781D_REG_TEMP2_CONFIG,
				tmp & 0xfe);
		}

		/* Enable temp3 */
		if (type != w83697hf) {
			tmp = w83627hf_read_value(client,
				W83781D_REG_TEMP3_CONFIG);
			if (tmp & 0x01) {
				dev_warn(&client->dev, "Enabling temp3, "
					 "readings might not make sense\n");
				w83627hf_write_value(client,
					W83781D_REG_TEMP3_CONFIG, tmp & 0xfe);
			}
		}
	}

	/* Start monitoring */
	w83627hf_write_value(client, W83781D_REG_CONFIG,
			    (w83627hf_read_value(client,
						W83781D_REG_CONFIG) & 0xf7)
			    | 0x01);
}

static struct w83627hf_data *w83627hf_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83627hf_data *data = i2c_get_clientdata(client);
	int i;

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		for (i = 0; i <= 8; i++) {
			/* skip missing sensors */
			if (((data->type == w83697hf) && (i == 1)) ||
			    ((data->type == w83627thf || data->type == w83637hf)
			    && (i == 5 || i == 6)))
				continue;
			data->in[i] =
			    w83627hf_read_value(client, W83781D_REG_IN(i));
			data->in_min[i] =
			    w83627hf_read_value(client,
					       W83781D_REG_IN_MIN(i));
			data->in_max[i] =
			    w83627hf_read_value(client,
					       W83781D_REG_IN_MAX(i));
		}
		for (i = 1; i <= 3; i++) {
			data->fan[i - 1] =
			    w83627hf_read_value(client, W83781D_REG_FAN(i));
			data->fan_min[i - 1] =
			    w83627hf_read_value(client,
					       W83781D_REG_FAN_MIN(i));
		}
		for (i = 1; i <= 3; i++) {
			u8 tmp = w83627hf_read_value(client,
				W836X7HF_REG_PWM(data->type, i));
 			/* bits 0-3 are reserved  in 627THF */
 			if (data->type == w83627thf)
				tmp &= 0xf0;
			data->pwm[i - 1] = tmp;
			if(i == 2 &&
			   (data->type == w83627hf || data->type == w83697hf))
				break;
		}

		data->temp = w83627hf_read_value(client, W83781D_REG_TEMP(1));
		data->temp_max =
		    w83627hf_read_value(client, W83781D_REG_TEMP_OVER(1));
		data->temp_max_hyst =
		    w83627hf_read_value(client, W83781D_REG_TEMP_HYST(1));
		data->temp_add[0] =
		    w83627hf_read_value(client, W83781D_REG_TEMP(2));
		data->temp_max_add[0] =
		    w83627hf_read_value(client, W83781D_REG_TEMP_OVER(2));
		data->temp_max_hyst_add[0] =
		    w83627hf_read_value(client, W83781D_REG_TEMP_HYST(2));
		if (data->type != w83697hf) {
			data->temp_add[1] =
			  w83627hf_read_value(client, W83781D_REG_TEMP(3));
			data->temp_max_add[1] =
			  w83627hf_read_value(client, W83781D_REG_TEMP_OVER(3));
			data->temp_max_hyst_add[1] =
			  w83627hf_read_value(client, W83781D_REG_TEMP_HYST(3));
		}

		i = w83627hf_read_value(client, W83781D_REG_VID_FANDIV);
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = (i >> 6) & 0x03;
		if (data->type != w83697hf) {
			data->fan_div[2] = (w83627hf_read_value(client,
					       W83781D_REG_PIN) >> 6) & 0x03;
		}
		i = w83627hf_read_value(client, W83781D_REG_VBAT);
		data->fan_div[0] |= (i >> 3) & 0x04;
		data->fan_div[1] |= (i >> 4) & 0x04;
		if (data->type != w83697hf)
			data->fan_div[2] |= (i >> 5) & 0x04;
		data->alarms =
		    w83627hf_read_value(client, W83781D_REG_ALARM1) |
		    (w83627hf_read_value(client, W83781D_REG_ALARM2) << 8) |
		    (w83627hf_read_value(client, W83781D_REG_ALARM3) << 16);
		i = w83627hf_read_value(client, W83781D_REG_BEEP_INTS2);
		data->beep_enable = i >> 7;
		data->beep_mask = ((i & 0x7f) << 8) |
		    w83627hf_read_value(client, W83781D_REG_BEEP_INTS1) |
		    w83627hf_read_value(client, W83781D_REG_BEEP_INTS3) << 16;
		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __init sensors_w83627hf_init(void)
{
	if (w83627hf_find(0x2e, &address)
	 && w83627hf_find(0x4e, &address)) {
		return -ENODEV;
	}

	return i2c_isa_add_driver(&w83627hf_driver);
}

static void __exit sensors_w83627hf_exit(void)
{
	i2c_isa_del_driver(&w83627hf_driver);
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>, "
	      "Philip Edelbrock <phil@netroedge.com>, "
	      "and Mark Studebaker <mdsxyz123@yahoo.com>");
MODULE_DESCRIPTION("W83627HF driver");
MODULE_LICENSE("GPL");

module_init(sensors_w83627hf_init);
module_exit(sensors_w83627hf_exit);
