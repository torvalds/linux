/*
    w83627hf.c - Part of lm_sensors, Linux kernel modules for hardware
                monitoring
    Copyright (c) 1998 - 2003  Frodo Looijaard <frodol@dds.nl>,
    Philip Edelbrock <phil@netroedge.com>,
    and Mark Studebaker <mdsxyz123@yahoo.com>
    Ported to 2.6 by Bernhard C. Schrenk <clemy@clemy.org>
    Copyright (c) 2007  Jean Delvare <khali@linux-fr.org>

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
    w83687thf	7	3	3	3	0x90	0x5ca3	no	yes(LPC)
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
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include "lm75.h"

static struct platform_device *pdev;

#define DRVNAME "w83627hf"
enum chips { w83627hf, w83627thf, w83697hf, w83637hf, w83687thf };

static u16 force_addr;
module_param(force_addr, ushort, 0);
MODULE_PARM_DESC(force_addr,
		 "Initialize the base address of the sensors");
static u8 force_i2c = 0x1f;
module_param(force_i2c, byte, 0);
MODULE_PARM_DESC(force_i2c,
		 "Initialize the i2c address of the sensors");

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

#define W83687THF_VID_EN	0x29 /* w83687thf only */
#define W83687THF_VID_CFG	0xF0 /* w83687thf only */
#define W83687THF_VID_DATA	0xF1 /* w83687thf only */

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
#define W687THF_DEVID 0x85
#define WINB_ACT_REG 0x30
#define WINB_BASE_REG 0x60
/* Constants specified below */

/* Alignment of the base address */
#define WINB_ALIGNMENT		~7

/* Offset & size of I/O region we are interested in */
#define WINB_REGION_OFFSET	5
#define WINB_REGION_SIZE	2

/* Where are the sensors address/data registers relative to the region offset */
#define W83781D_ADDR_REG_OFFSET 0
#define W83781D_DATA_REG_OFFSET 1

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

#define W83627THF_REG_PWM1		0x01	/* 697HF/637HF/687THF too */
#define W83627THF_REG_PWM2		0x03	/* 697HF/637HF/687THF too */
#define W83627THF_REG_PWM3		0x11	/* 637HF/687THF too */

#define W83627THF_REG_VRM_OVT_CFG 	0x18	/* 637HF/687THF too */

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

/* For each registered chip, we need to keep some data in memory.
   The structure is dynamically allocated. */
struct w83627hf_data {
	unsigned short addr;
	const char *name;
	struct class_device *class_dev;
	struct mutex lock;
	enum chips type;

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

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
	u8 vrm_ovt;		/* Register value, 627THF/637HF/687THF only */
};

struct w83627hf_sio_data {
	enum chips type;
};


static int w83627hf_probe(struct platform_device *pdev);
static int w83627hf_remove(struct platform_device *pdev);

static int w83627hf_read_value(struct w83627hf_data *data, u16 reg);
static int w83627hf_write_value(struct w83627hf_data *data, u16 reg, u16 value);
static struct w83627hf_data *w83627hf_update_device(struct device *dev);
static void w83627hf_init_device(struct platform_device *pdev);

static struct platform_driver w83627hf_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= w83627hf_probe,
	.remove		= __devexit_p(w83627hf_remove),
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
	struct w83627hf_data *data = dev_get_drvdata(dev); \
	u32 val; \
	 \
	val = simple_strtoul(buf, NULL, 10); \
	 \
	mutex_lock(&data->update_lock); \
	data->in_##reg[nr] = IN_TO_REG(val); \
	w83627hf_write_value(data, W83781D_REG_IN_##REG(nr), \
			    data->in_##reg[nr]); \
	 \
	mutex_unlock(&data->update_lock); \
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
		(w83627thf == data->type || w83637hf == data->type
		 || w83687thf == data->type))

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
	struct w83627hf_data *data = dev_get_drvdata(dev);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	
	if ((data->vrm_ovt & 0x01) &&
		(w83627thf == data->type || w83637hf == data->type
		 || w83687thf == data->type))

		/* use VRM9 calculation */
		data->in_min[0] =
			SENSORS_LIMIT(((val * 100) - 70000 + 244) / 488, 0,
					255);
	else
		/* use VRM8 (standard) calculation */
		data->in_min[0] = IN_TO_REG(val);

	w83627hf_write_value(data, W83781D_REG_IN_MIN(0), data->in_min[0]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t store_regs_in_max0(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);

	if ((data->vrm_ovt & 0x01) &&
		(w83627thf == data->type || w83637hf == data->type
		 || w83687thf == data->type))
		
		/* use VRM9 calculation */
		data->in_max[0] =
			SENSORS_LIMIT(((val * 100) - 70000 + 244) / 488, 0,
					255);
	else
		/* use VRM8 (standard) calculation */
		data->in_max[0] = IN_TO_REG(val);

	w83627hf_write_value(data, W83781D_REG_IN_MAX(0), data->in_max[0]);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR(in0_input, S_IRUGO, show_regs_in_0, NULL);
static DEVICE_ATTR(in0_min, S_IRUGO | S_IWUSR,
	show_regs_in_min0, store_regs_in_min0);
static DEVICE_ATTR(in0_max, S_IRUGO | S_IWUSR,
	show_regs_in_max0, store_regs_in_max0);

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
	struct w83627hf_data *data = dev_get_drvdata(dev);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->fan_min[nr - 1] =
	    FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr - 1]));
	w83627hf_write_value(data, W83781D_REG_FAN_MIN(nr),
			    data->fan_min[nr - 1]);

	mutex_unlock(&data->update_lock);
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
	struct w83627hf_data *data = dev_get_drvdata(dev); \
	u32 val; \
	 \
	val = simple_strtoul(buf, NULL, 10); \
	 \
	mutex_lock(&data->update_lock); \
	 \
	if (nr >= 2) {	/* TEMP2 and TEMP3 */ \
		data->temp_##reg##_add[nr-2] = LM75_TEMP_TO_REG(val); \
		w83627hf_write_value(data, W83781D_REG_TEMP_##REG(nr), \
				data->temp_##reg##_add[nr-2]); \
	} else {	/* TEMP1 */ \
		data->temp_##reg = TEMP_TO_REG(val); \
		w83627hf_write_value(data, W83781D_REG_TEMP_##REG(nr), \
			data->temp_##reg); \
	} \
	 \
	mutex_unlock(&data->update_lock); \
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

static ssize_t
show_vid_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid_reg, NULL);

static ssize_t
show_vrm_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->vrm);
}
static ssize_t
store_vrm_reg(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);
	data->vrm = val;

	return count;
}
static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm_reg, store_vrm_reg);

static ssize_t
show_alarms_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->alarms);
}
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms_reg, NULL);

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
	struct w83627hf_data *data = dev_get_drvdata(dev);
	u32 val, val2;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);

	if (update_mask == BEEP_MASK) {	/* We are storing beep_mask */
		data->beep_mask = BEEP_MASK_TO_REG(val);
		w83627hf_write_value(data, W83781D_REG_BEEP_INTS1,
				    data->beep_mask & 0xff);
		w83627hf_write_value(data, W83781D_REG_BEEP_INTS3,
				    ((data->beep_mask) >> 16) & 0xff);
		val2 = (data->beep_mask >> 8) & 0x7f;
	} else {		/* We are storing beep_enable */
		val2 =
		    w83627hf_read_value(data, W83781D_REG_BEEP_INTS2) & 0x7f;
		data->beep_enable = BEEP_ENABLE_TO_REG(val);
	}

	w83627hf_write_value(data, W83781D_REG_BEEP_INTS2,
			    val2 | data->beep_enable << 7);

	mutex_unlock(&data->update_lock);
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

static ssize_t
show_fan_div_reg(struct device *dev, char *buf, int nr)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n",
		       (long) DIV_FROM_REG(data->fan_div[nr - 1]));
}

/* Note: we save and restore the fan minimum here, because its value is
   determined in part by the fan divisor.  This follows the principle of
   least surprise; the user doesn't expect the fan minimum to change just
   because the divisor changed. */
static ssize_t
store_fan_div_reg(struct device *dev, const char *buf, size_t count, int nr)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	unsigned long min;
	u8 reg;
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);

	/* Save fan_min */
	min = FAN_FROM_REG(data->fan_min[nr],
			   DIV_FROM_REG(data->fan_div[nr]));

	data->fan_div[nr] = DIV_TO_REG(val);

	reg = (w83627hf_read_value(data, nr==2 ? W83781D_REG_PIN : W83781D_REG_VID_FANDIV)
	       & (nr==0 ? 0xcf : 0x3f))
	    | ((data->fan_div[nr] & 0x03) << (nr==0 ? 4 : 6));
	w83627hf_write_value(data, nr==2 ? W83781D_REG_PIN : W83781D_REG_VID_FANDIV, reg);

	reg = (w83627hf_read_value(data, W83781D_REG_VBAT)
	       & ~(1 << (5 + nr)))
	    | ((data->fan_div[nr] & 0x04) << (3 + nr));
	w83627hf_write_value(data, W83781D_REG_VBAT, reg);

	/* Restore fan_min */
	data->fan_min[nr] = FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	w83627hf_write_value(data, W83781D_REG_FAN_MIN(nr+1), data->fan_min[nr]);

	mutex_unlock(&data->update_lock);
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

static ssize_t
show_pwm_reg(struct device *dev, char *buf, int nr)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->pwm[nr - 1]);
}

static ssize_t
store_pwm_reg(struct device *dev, const char *buf, size_t count, int nr)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);

	if (data->type == w83627thf) {
		/* bits 0-3 are reserved  in 627THF */
		data->pwm[nr - 1] = PWM_TO_REG(val) & 0xf0;
		w83627hf_write_value(data,
				     W836X7HF_REG_PWM(data->type, nr),
				     data->pwm[nr - 1] |
				     (w83627hf_read_value(data,
				     W836X7HF_REG_PWM(data->type, nr)) & 0x0f));
	} else {
		data->pwm[nr - 1] = PWM_TO_REG(val);
		w83627hf_write_value(data,
				     W836X7HF_REG_PWM(data->type, nr),
				     data->pwm[nr - 1]);
	}

	mutex_unlock(&data->update_lock);
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

static ssize_t
show_sensor_reg(struct device *dev, char *buf, int nr)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->sens[nr - 1]);
}

static ssize_t
store_sensor_reg(struct device *dev, const char *buf, size_t count, int nr)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	u32 val, tmp;

	val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);

	switch (val) {
	case 1:		/* PII/Celeron diode */
		tmp = w83627hf_read_value(data, W83781D_REG_SCFG1);
		w83627hf_write_value(data, W83781D_REG_SCFG1,
				    tmp | BIT_SCFG1[nr - 1]);
		tmp = w83627hf_read_value(data, W83781D_REG_SCFG2);
		w83627hf_write_value(data, W83781D_REG_SCFG2,
				    tmp | BIT_SCFG2[nr - 1]);
		data->sens[nr - 1] = val;
		break;
	case 2:		/* 3904 */
		tmp = w83627hf_read_value(data, W83781D_REG_SCFG1);
		w83627hf_write_value(data, W83781D_REG_SCFG1,
				    tmp | BIT_SCFG1[nr - 1]);
		tmp = w83627hf_read_value(data, W83781D_REG_SCFG2);
		w83627hf_write_value(data, W83781D_REG_SCFG2,
				    tmp & ~BIT_SCFG2[nr - 1]);
		data->sens[nr - 1] = val;
		break;
	case W83781D_DEFAULT_BETA:	/* thermistor */
		tmp = w83627hf_read_value(data, W83781D_REG_SCFG1);
		w83627hf_write_value(data, W83781D_REG_SCFG1,
				    tmp & ~BIT_SCFG1[nr - 1]);
		data->sens[nr - 1] = val;
		break;
	default:
		dev_err(dev,
		       "Invalid sensor type %ld; must be 1, 2, or %d\n",
		       (long) val, W83781D_DEFAULT_BETA);
		break;
	}

	mutex_unlock(&data->update_lock);
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

static ssize_t show_name(struct device *dev, struct device_attribute
			 *devattr, char *buf)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static int __init w83627hf_find(int sioaddr, unsigned short *addr,
				struct w83627hf_sio_data *sio_data)
{
	int err = -ENODEV;
	u16 val;

	static const __initdata char *names[] = {
		"W83627HF",
		"W83627THF",
		"W83697HF",
		"W83637HF",
		"W83687THF",
	};

	REG = sioaddr;
	VAL = sioaddr + 1;

	superio_enter();
	val= superio_inb(DEVID);
	switch (val) {
	case W627_DEVID:
		sio_data->type = w83627hf;
		break;
	case W627THF_DEVID:
		sio_data->type = w83627thf;
		break;
	case W697_DEVID:
		sio_data->type = w83697hf;
		break;
	case W637_DEVID:
		sio_data->type = w83637hf;
		break;
	case W687THF_DEVID:
		sio_data->type = w83687thf;
		break;
	default:
		pr_debug(DRVNAME ": Unsupported chip (DEVID=0x%x)\n", val);
		goto exit;
	}

	superio_select(W83627HF_LD_HWM);
	force_addr &= WINB_ALIGNMENT;
	if (force_addr) {
		printk(KERN_WARNING DRVNAME ": Forcing address 0x%x\n",
		       force_addr);
		superio_outb(WINB_BASE_REG, force_addr >> 8);
		superio_outb(WINB_BASE_REG + 1, force_addr & 0xff);
	}
	val = (superio_inb(WINB_BASE_REG) << 8) |
	       superio_inb(WINB_BASE_REG + 1);
	*addr = val & WINB_ALIGNMENT;
	if (*addr == 0) {
		printk(KERN_WARNING DRVNAME ": Base address not set, "
		       "skipping\n");
		goto exit;
	}

	val = superio_inb(WINB_ACT_REG);
	if (!(val & 0x01)) {
		printk(KERN_WARNING DRVNAME ": Enabling HWM logical device\n");
		superio_outb(WINB_ACT_REG, val | 0x01);
	}

	err = 0;
	pr_info(DRVNAME ": Found %s chip at %#x\n",
		names[sio_data->type], *addr);

 exit:
	superio_exit();
	return err;
}

static struct attribute *w83627hf_attributes[] = {
	&dev_attr_in0_input.attr,
	&dev_attr_in0_min.attr,
	&dev_attr_in0_max.attr,
	&dev_attr_in2_input.attr,
	&dev_attr_in2_min.attr,
	&dev_attr_in2_max.attr,
	&dev_attr_in3_input.attr,
	&dev_attr_in3_min.attr,
	&dev_attr_in3_max.attr,
	&dev_attr_in4_input.attr,
	&dev_attr_in4_min.attr,
	&dev_attr_in4_max.attr,
	&dev_attr_in7_input.attr,
	&dev_attr_in7_min.attr,
	&dev_attr_in7_max.attr,
	&dev_attr_in8_input.attr,
	&dev_attr_in8_min.attr,
	&dev_attr_in8_max.attr,

	&dev_attr_fan1_input.attr,
	&dev_attr_fan1_min.attr,
	&dev_attr_fan1_div.attr,
	&dev_attr_fan2_input.attr,
	&dev_attr_fan2_min.attr,
	&dev_attr_fan2_div.attr,

	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_max.attr,
	&dev_attr_temp1_max_hyst.attr,
	&dev_attr_temp1_type.attr,
	&dev_attr_temp2_input.attr,
	&dev_attr_temp2_max.attr,
	&dev_attr_temp2_max_hyst.attr,
	&dev_attr_temp2_type.attr,

	&dev_attr_alarms.attr,
	&dev_attr_beep_enable.attr,
	&dev_attr_beep_mask.attr,

	&dev_attr_pwm1.attr,
	&dev_attr_pwm2.attr,

	&dev_attr_name.attr,
	NULL
};

static const struct attribute_group w83627hf_group = {
	.attrs = w83627hf_attributes,
};

static struct attribute *w83627hf_attributes_opt[] = {
	&dev_attr_in1_input.attr,
	&dev_attr_in1_min.attr,
	&dev_attr_in1_max.attr,
	&dev_attr_in5_input.attr,
	&dev_attr_in5_min.attr,
	&dev_attr_in5_max.attr,
	&dev_attr_in6_input.attr,
	&dev_attr_in6_min.attr,
	&dev_attr_in6_max.attr,

	&dev_attr_fan3_input.attr,
	&dev_attr_fan3_min.attr,
	&dev_attr_fan3_div.attr,

	&dev_attr_temp3_input.attr,
	&dev_attr_temp3_max.attr,
	&dev_attr_temp3_max_hyst.attr,
	&dev_attr_temp3_type.attr,

	&dev_attr_pwm3.attr,

	NULL
};

static const struct attribute_group w83627hf_group_opt = {
	.attrs = w83627hf_attributes_opt,
};

static int __devinit w83627hf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct w83627hf_sio_data *sio_data = dev->platform_data;
	struct w83627hf_data *data;
	struct resource *res;
	int err;

	static const char *names[] = {
		"w83627hf",
		"w83627thf",
		"w83697hf",
		"w83637hf",
		"w83687thf",
	};

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!request_region(res->start, WINB_REGION_SIZE, DRVNAME)) {
		dev_err(dev, "Failed to request region 0x%lx-0x%lx\n",
			(unsigned long)res->start,
			(unsigned long)(res->start + WINB_REGION_SIZE - 1));
		err = -EBUSY;
		goto ERROR0;
	}

	if (!(data = kzalloc(sizeof(struct w83627hf_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR1;
	}
	data->addr = res->start;
	data->type = sio_data->type;
	data->name = names[sio_data->type];
	mutex_init(&data->lock);
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	/* Initialize the chip */
	w83627hf_init_device(pdev);

	/* A few vars need to be filled upon startup */
	data->fan_min[0] = w83627hf_read_value(data, W83781D_REG_FAN_MIN(1));
	data->fan_min[1] = w83627hf_read_value(data, W83781D_REG_FAN_MIN(2));
	data->fan_min[2] = w83627hf_read_value(data, W83781D_REG_FAN_MIN(3));

	/* Register common device attributes */
	if ((err = sysfs_create_group(&dev->kobj, &w83627hf_group)))
		goto ERROR3;

	/* Register chip-specific device attributes */
	if (data->type == w83627hf || data->type == w83697hf)
		if ((err = device_create_file(dev, &dev_attr_in5_input))
		 || (err = device_create_file(dev, &dev_attr_in5_min))
		 || (err = device_create_file(dev, &dev_attr_in5_max))
		 || (err = device_create_file(dev, &dev_attr_in6_input))
		 || (err = device_create_file(dev, &dev_attr_in6_min))
		 || (err = device_create_file(dev, &dev_attr_in6_max)))
			goto ERROR4;

	if (data->type != w83697hf)
		if ((err = device_create_file(dev, &dev_attr_in1_input))
		 || (err = device_create_file(dev, &dev_attr_in1_min))
		 || (err = device_create_file(dev, &dev_attr_in1_max))
		 || (err = device_create_file(dev, &dev_attr_fan3_input))
		 || (err = device_create_file(dev, &dev_attr_fan3_min))
		 || (err = device_create_file(dev, &dev_attr_fan3_div))
		 || (err = device_create_file(dev, &dev_attr_temp3_input))
		 || (err = device_create_file(dev, &dev_attr_temp3_max))
		 || (err = device_create_file(dev, &dev_attr_temp3_max_hyst))
		 || (err = device_create_file(dev, &dev_attr_temp3_type)))
			goto ERROR4;

	if (data->type != w83697hf && data->vid != 0xff) {
		/* Convert VID to voltage based on VRM */
		data->vrm = vid_which_vrm();

		if ((err = device_create_file(dev, &dev_attr_cpu0_vid))
		 || (err = device_create_file(dev, &dev_attr_vrm)))
			goto ERROR4;
	}

	if (data->type == w83627thf || data->type == w83637hf
	 || data->type == w83687thf)
		if ((err = device_create_file(dev, &dev_attr_pwm3)))
			goto ERROR4;

	data->class_dev = hwmon_device_register(dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto ERROR4;
	}

	return 0;

      ERROR4:
	sysfs_remove_group(&dev->kobj, &w83627hf_group);
	sysfs_remove_group(&dev->kobj, &w83627hf_group_opt);
      ERROR3:
	kfree(data);
      ERROR1:
	release_region(res->start, WINB_REGION_SIZE);
      ERROR0:
	return err;
}

static int __devexit w83627hf_remove(struct platform_device *pdev)
{
	struct w83627hf_data *data = platform_get_drvdata(pdev);
	struct resource *res;

	platform_set_drvdata(pdev, NULL);
	hwmon_device_unregister(data->class_dev);

	sysfs_remove_group(&pdev->dev.kobj, &w83627hf_group);
	sysfs_remove_group(&pdev->dev.kobj, &w83627hf_group_opt);
	kfree(data);

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	release_region(res->start, WINB_REGION_SIZE);

	return 0;
}


static int w83627hf_read_value(struct w83627hf_data *data, u16 reg)
{
	int res, word_sized;

	mutex_lock(&data->lock);
	word_sized = (((reg & 0xff00) == 0x100)
		   || ((reg & 0xff00) == 0x200))
		  && (((reg & 0x00ff) == 0x50)
		   || ((reg & 0x00ff) == 0x53)
		   || ((reg & 0x00ff) == 0x55));
	if (reg & 0xff00) {
		outb_p(W83781D_REG_BANK,
		       data->addr + W83781D_ADDR_REG_OFFSET);
		outb_p(reg >> 8,
		       data->addr + W83781D_DATA_REG_OFFSET);
	}
	outb_p(reg & 0xff, data->addr + W83781D_ADDR_REG_OFFSET);
	res = inb_p(data->addr + W83781D_DATA_REG_OFFSET);
	if (word_sized) {
		outb_p((reg & 0xff) + 1,
		       data->addr + W83781D_ADDR_REG_OFFSET);
		res =
		    (res << 8) + inb_p(data->addr +
				       W83781D_DATA_REG_OFFSET);
	}
	if (reg & 0xff00) {
		outb_p(W83781D_REG_BANK,
		       data->addr + W83781D_ADDR_REG_OFFSET);
		outb_p(0, data->addr + W83781D_DATA_REG_OFFSET);
	}
	mutex_unlock(&data->lock);
	return res;
}

static int __devinit w83627thf_read_gpio5(struct platform_device *pdev)
{
	int res = 0xff, sel;

	superio_enter();
	superio_select(W83627HF_LD_GPIO5);

	/* Make sure these GPIO pins are enabled */
	if (!(superio_inb(W83627THF_GPIO5_EN) & (1<<3))) {
		dev_dbg(&pdev->dev, "GPIO5 disabled, no VID function\n");
		goto exit;
	}

	/* Make sure the pins are configured for input
	   There must be at least five (VRM 9), and possibly 6 (VRM 10) */
	sel = superio_inb(W83627THF_GPIO5_IOSR) & 0x3f;
	if ((sel & 0x1f) != 0x1f) {
		dev_dbg(&pdev->dev, "GPIO5 not configured for VID "
			"function\n");
		goto exit;
	}

	dev_info(&pdev->dev, "Reading VID from GPIO5\n");
	res = superio_inb(W83627THF_GPIO5_DR) & sel;

exit:
	superio_exit();
	return res;
}

static int __devinit w83687thf_read_vid(struct platform_device *pdev)
{
	int res = 0xff;

	superio_enter();
	superio_select(W83627HF_LD_HWM);

	/* Make sure these GPIO pins are enabled */
	if (!(superio_inb(W83687THF_VID_EN) & (1 << 2))) {
		dev_dbg(&pdev->dev, "VID disabled, no VID function\n");
		goto exit;
	}

	/* Make sure the pins are configured for input */
	if (!(superio_inb(W83687THF_VID_CFG) & (1 << 4))) {
		dev_dbg(&pdev->dev, "VID configured as output, "
			"no VID function\n");
		goto exit;
	}

	res = superio_inb(W83687THF_VID_DATA) & 0x3f;

exit:
	superio_exit();
	return res;
}

static int w83627hf_write_value(struct w83627hf_data *data, u16 reg, u16 value)
{
	int word_sized;

	mutex_lock(&data->lock);
	word_sized = (((reg & 0xff00) == 0x100)
		   || ((reg & 0xff00) == 0x200))
		  && (((reg & 0x00ff) == 0x53)
		   || ((reg & 0x00ff) == 0x55));
	if (reg & 0xff00) {
		outb_p(W83781D_REG_BANK,
		       data->addr + W83781D_ADDR_REG_OFFSET);
		outb_p(reg >> 8,
		       data->addr + W83781D_DATA_REG_OFFSET);
	}
	outb_p(reg & 0xff, data->addr + W83781D_ADDR_REG_OFFSET);
	if (word_sized) {
		outb_p(value >> 8,
		       data->addr + W83781D_DATA_REG_OFFSET);
		outb_p((reg & 0xff) + 1,
		       data->addr + W83781D_ADDR_REG_OFFSET);
	}
	outb_p(value & 0xff,
	       data->addr + W83781D_DATA_REG_OFFSET);
	if (reg & 0xff00) {
		outb_p(W83781D_REG_BANK,
		       data->addr + W83781D_ADDR_REG_OFFSET);
		outb_p(0, data->addr + W83781D_DATA_REG_OFFSET);
	}
	mutex_unlock(&data->lock);
	return 0;
}

static void __devinit w83627hf_init_device(struct platform_device *pdev)
{
	struct w83627hf_data *data = platform_get_drvdata(pdev);
	int i;
	enum chips type = data->type;
	u8 tmp;

	if (reset) {
		/* Resetting the chip has been the default for a long time,
		   but repeatedly caused problems (fans going to full
		   speed...) so it is now optional. It might even go away if
		   nobody reports it as being useful, as I see very little
		   reason why this would be needed at all. */
		dev_info(&pdev->dev, "If reset=1 solved a problem you were "
			 "having, please report!\n");

		/* save this register */
		i = w83627hf_read_value(data, W83781D_REG_BEEP_CONFIG);
		/* Reset all except Watchdog values and last conversion values
		   This sets fan-divs to 2, among others */
		w83627hf_write_value(data, W83781D_REG_CONFIG, 0x80);
		/* Restore the register and disable power-on abnormal beep.
		   This saves FAN 1/2/3 input/output values set by BIOS. */
		w83627hf_write_value(data, W83781D_REG_BEEP_CONFIG, i | 0x80);
		/* Disable master beep-enable (reset turns it on).
		   Individual beeps should be reset to off but for some reason
		   disabling this bit helps some people not get beeped */
		w83627hf_write_value(data, W83781D_REG_BEEP_INTS2, 0);
	}

	/* Minimize conflicts with other winbond i2c-only clients...  */
	/* disable i2c subclients... how to disable main i2c client?? */
	/* force i2c address to relatively uncommon address */
	w83627hf_write_value(data, W83781D_REG_I2C_SUBADDR, 0x89);
	w83627hf_write_value(data, W83781D_REG_I2C_ADDR, force_i2c);

	/* Read VID only once */
	if (type == w83627hf || type == w83637hf) {
		int lo = w83627hf_read_value(data, W83781D_REG_VID_FANDIV);
		int hi = w83627hf_read_value(data, W83781D_REG_CHIPID);
		data->vid = (lo & 0x0f) | ((hi & 0x01) << 4);
	} else if (type == w83627thf) {
		data->vid = w83627thf_read_gpio5(pdev);
	} else if (type == w83687thf) {
		data->vid = w83687thf_read_vid(pdev);
	}

	/* Read VRM & OVT Config only once */
	if (type == w83627thf || type == w83637hf || type == w83687thf) {
		data->vrm_ovt = 
			w83627hf_read_value(data, W83627THF_REG_VRM_OVT_CFG);
	}

	tmp = w83627hf_read_value(data, W83781D_REG_SCFG1);
	for (i = 1; i <= 3; i++) {
		if (!(tmp & BIT_SCFG1[i - 1])) {
			data->sens[i - 1] = W83781D_DEFAULT_BETA;
		} else {
			if (w83627hf_read_value
			    (data,
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
		tmp = w83627hf_read_value(data, W83781D_REG_TEMP2_CONFIG);
		if (tmp & 0x01) {
			dev_warn(&pdev->dev, "Enabling temp2, readings "
				 "might not make sense\n");
			w83627hf_write_value(data, W83781D_REG_TEMP2_CONFIG,
				tmp & 0xfe);
		}

		/* Enable temp3 */
		if (type != w83697hf) {
			tmp = w83627hf_read_value(data,
				W83781D_REG_TEMP3_CONFIG);
			if (tmp & 0x01) {
				dev_warn(&pdev->dev, "Enabling temp3, "
					 "readings might not make sense\n");
				w83627hf_write_value(data,
					W83781D_REG_TEMP3_CONFIG, tmp & 0xfe);
			}
		}
	}

	/* Start monitoring */
	w83627hf_write_value(data, W83781D_REG_CONFIG,
			    (w83627hf_read_value(data,
						W83781D_REG_CONFIG) & 0xf7)
			    | 0x01);
}

static struct w83627hf_data *w83627hf_update_device(struct device *dev)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		for (i = 0; i <= 8; i++) {
			/* skip missing sensors */
			if (((data->type == w83697hf) && (i == 1)) ||
			    ((data->type != w83627hf && data->type != w83697hf)
			    && (i == 5 || i == 6)))
				continue;
			data->in[i] =
			    w83627hf_read_value(data, W83781D_REG_IN(i));
			data->in_min[i] =
			    w83627hf_read_value(data,
					       W83781D_REG_IN_MIN(i));
			data->in_max[i] =
			    w83627hf_read_value(data,
					       W83781D_REG_IN_MAX(i));
		}
		for (i = 1; i <= 3; i++) {
			data->fan[i - 1] =
			    w83627hf_read_value(data, W83781D_REG_FAN(i));
			data->fan_min[i - 1] =
			    w83627hf_read_value(data,
					       W83781D_REG_FAN_MIN(i));
		}
		for (i = 1; i <= 3; i++) {
			u8 tmp = w83627hf_read_value(data,
				W836X7HF_REG_PWM(data->type, i));
 			/* bits 0-3 are reserved  in 627THF */
 			if (data->type == w83627thf)
				tmp &= 0xf0;
			data->pwm[i - 1] = tmp;
			if(i == 2 &&
			   (data->type == w83627hf || data->type == w83697hf))
				break;
		}

		data->temp = w83627hf_read_value(data, W83781D_REG_TEMP(1));
		data->temp_max =
		    w83627hf_read_value(data, W83781D_REG_TEMP_OVER(1));
		data->temp_max_hyst =
		    w83627hf_read_value(data, W83781D_REG_TEMP_HYST(1));
		data->temp_add[0] =
		    w83627hf_read_value(data, W83781D_REG_TEMP(2));
		data->temp_max_add[0] =
		    w83627hf_read_value(data, W83781D_REG_TEMP_OVER(2));
		data->temp_max_hyst_add[0] =
		    w83627hf_read_value(data, W83781D_REG_TEMP_HYST(2));
		if (data->type != w83697hf) {
			data->temp_add[1] =
			  w83627hf_read_value(data, W83781D_REG_TEMP(3));
			data->temp_max_add[1] =
			  w83627hf_read_value(data, W83781D_REG_TEMP_OVER(3));
			data->temp_max_hyst_add[1] =
			  w83627hf_read_value(data, W83781D_REG_TEMP_HYST(3));
		}

		i = w83627hf_read_value(data, W83781D_REG_VID_FANDIV);
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = (i >> 6) & 0x03;
		if (data->type != w83697hf) {
			data->fan_div[2] = (w83627hf_read_value(data,
					       W83781D_REG_PIN) >> 6) & 0x03;
		}
		i = w83627hf_read_value(data, W83781D_REG_VBAT);
		data->fan_div[0] |= (i >> 3) & 0x04;
		data->fan_div[1] |= (i >> 4) & 0x04;
		if (data->type != w83697hf)
			data->fan_div[2] |= (i >> 5) & 0x04;
		data->alarms =
		    w83627hf_read_value(data, W83781D_REG_ALARM1) |
		    (w83627hf_read_value(data, W83781D_REG_ALARM2) << 8) |
		    (w83627hf_read_value(data, W83781D_REG_ALARM3) << 16);
		i = w83627hf_read_value(data, W83781D_REG_BEEP_INTS2);
		data->beep_enable = i >> 7;
		data->beep_mask = ((i & 0x7f) << 8) |
		    w83627hf_read_value(data, W83781D_REG_BEEP_INTS1) |
		    w83627hf_read_value(data, W83781D_REG_BEEP_INTS3) << 16;
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init w83627hf_device_add(unsigned short address,
				      const struct w83627hf_sio_data *sio_data)
{
	struct resource res = {
		.start	= address + WINB_REGION_OFFSET,
		.end	= address + WINB_REGION_OFFSET + WINB_REGION_SIZE - 1,
		.name	= DRVNAME,
		.flags	= IORESOURCE_IO,
	};
	int err;

	pdev = platform_device_alloc(DRVNAME, address);
	if (!pdev) {
		err = -ENOMEM;
		printk(KERN_ERR DRVNAME ": Device allocation failed\n");
		goto exit;
	}

	err = platform_device_add_resources(pdev, &res, 1);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device resource addition failed "
		       "(%d)\n", err);
		goto exit_device_put;
	}

	pdev->dev.platform_data = kmalloc(sizeof(struct w83627hf_sio_data),
					  GFP_KERNEL);
	if (!pdev->dev.platform_data) {
		err = -ENOMEM;
		printk(KERN_ERR DRVNAME ": Platform data allocation failed\n");
		goto exit_device_put;
	}
	memcpy(pdev->dev.platform_data, sio_data,
	       sizeof(struct w83627hf_sio_data));

	err = platform_device_add(pdev);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device addition failed (%d)\n",
		       err);
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(pdev);
exit:
	return err;
}

static int __init sensors_w83627hf_init(void)
{
	int err;
	unsigned short address;
	struct w83627hf_sio_data sio_data;

	if (w83627hf_find(0x2e, &address, &sio_data)
	 && w83627hf_find(0x4e, &address, &sio_data))
		return -ENODEV;

	err = platform_driver_register(&w83627hf_driver);
	if (err)
		goto exit;

	/* Sets global pdev as a side effect */
	err = w83627hf_device_add(address, &sio_data);
	if (err)
		goto exit_driver;

	return 0;

exit_driver:
	platform_driver_unregister(&w83627hf_driver);
exit:
	return err;
}

static void __exit sensors_w83627hf_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&w83627hf_driver);
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>, "
	      "Philip Edelbrock <phil@netroedge.com>, "
	      "and Mark Studebaker <mdsxyz123@yahoo.com>");
MODULE_DESCRIPTION("W83627HF driver");
MODULE_LICENSE("GPL");

module_init(sensors_w83627hf_init);
module_exit(sensors_w83627hf_exit);
