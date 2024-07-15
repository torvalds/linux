// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * w83627hf.c - Part of lm_sensors, Linux kernel modules for hardware
 *		monitoring
 * Copyright (c) 1998 - 2003  Frodo Looijaard <frodol@dds.nl>,
 *			      Philip Edelbrock <phil@netroedge.com>,
 *			      and Mark Studebaker <mdsxyz123@yahoo.com>
 * Ported to 2.6 by Bernhard C. Schrenk <clemy@clemy.org>
 * Copyright (c) 2007 - 1012  Jean Delvare <jdelvare@suse.de>
 */

/*
 * Supports following chips:
 *
 * Chip		#vin	#fanin	#pwm	#temp	wchipid	vendid	i2c	ISA
 * w83627hf	9	3	2	3	0x20	0x5ca3	no	yes(LPC)
 * w83627thf	7	3	3	3	0x90	0x5ca3	no	yes(LPC)
 * w83637hf	7	3	3	3	0x80	0x5ca3	no	yes(LPC)
 * w83687thf	7	3	3	3	0x90	0x5ca3	no	yes(LPC)
 * w83697hf	8	2	2	2	0x60	0x5ca3	no	yes(LPC)
 *
 * For other winbond chips, and for i2c support in the above chips,
 * use w83781d.c.
 *
 * Note: automatic ("cruise") fan control for 697, 637 & 627thf not
 * supported yet.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include "lm75.h"

static struct platform_device *pdev;

#define DRVNAME "w83627hf"
enum chips { w83627hf, w83627thf, w83697hf, w83637hf, w83687thf };

struct w83627hf_sio_data {
	enum chips type;
	int sioaddr;
};

static u8 force_i2c = 0x1f;
module_param(force_i2c, byte, 0);
MODULE_PARM_DESC(force_i2c,
		 "Initialize the i2c address of the sensors");

static bool init = 1;
module_param(init, bool, 0);
MODULE_PARM_DESC(init, "Set to zero to bypass chip initialization");

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

/* modified from kernel/include/traps.c */
#define DEV			0x07 /* Register: Logical device select */

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

#define DEVID			0x20 /* Register: Device ID */

#define W83627THF_GPIO5_EN	0x30 /* w83627thf only */
#define W83627THF_GPIO5_IOSR	0xf3 /* w83627thf only */
#define W83627THF_GPIO5_DR	0xf4 /* w83627thf only */

#define W83687THF_VID_EN	0x29 /* w83687thf only */
#define W83687THF_VID_CFG	0xF0 /* w83687thf only */
#define W83687THF_VID_DATA	0xF1 /* w83687thf only */

static inline void
superio_outb(struct w83627hf_sio_data *sio, int reg, int val)
{
	outb(reg, sio->sioaddr);
	outb(val, sio->sioaddr + 1);
}

static inline int
superio_inb(struct w83627hf_sio_data *sio, int reg)
{
	outb(reg, sio->sioaddr);
	return inb(sio->sioaddr + 1);
}

static inline void
superio_select(struct w83627hf_sio_data *sio, int ld)
{
	outb(DEV, sio->sioaddr);
	outb(ld,  sio->sioaddr + 1);
}

static inline int
superio_enter(struct w83627hf_sio_data *sio)
{
	if (!request_muxed_region(sio->sioaddr, 2, DRVNAME))
		return -EBUSY;

	outb(0x87, sio->sioaddr);
	outb(0x87, sio->sioaddr);

	return 0;
}

static inline void
superio_exit(struct w83627hf_sio_data *sio)
{
	outb(0xAA, sio->sioaddr);
	release_region(sio->sioaddr, 2);
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

/* nr:0-2 for fans:1-3 */
#define W83627HF_REG_FAN_MIN(nr)	(0x3b + (nr))
#define W83627HF_REG_FAN(nr)		(0x28 + (nr))

#define W83627HF_REG_TEMP2_CONFIG 0x152
#define W83627HF_REG_TEMP3_CONFIG 0x252
/* these are zero-based, unlike config constants above */
static const u16 w83627hf_reg_temp[]		= { 0x27, 0x150, 0x250 };
static const u16 w83627hf_reg_temp_hyst[]	= { 0x3A, 0x153, 0x253 };
static const u16 w83627hf_reg_temp_over[]	= { 0x39, 0x155, 0x255 };

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

static const u8 W83627THF_REG_PWM_ENABLE[] = {
	0x04,		/* FAN 1 mode */
	0x04,		/* FAN 2 mode */
	0x12,		/* FAN AUX mode */
};
static const u8 W83627THF_PWM_ENABLE_SHIFT[] = { 2, 4, 1 };

#define W83627THF_REG_PWM1		0x01	/* 697HF/637HF/687THF too */
#define W83627THF_REG_PWM2		0x03	/* 697HF/637HF/687THF too */
#define W83627THF_REG_PWM3		0x11	/* 637HF/687THF too */

#define W83627THF_REG_VRM_OVT_CFG 	0x18	/* 637HF/687THF too */

static const u8 regpwm_627hf[] = { W83627HF_REG_PWM1, W83627HF_REG_PWM2 };
static const u8 regpwm[] = { W83627THF_REG_PWM1, W83627THF_REG_PWM2,
                             W83627THF_REG_PWM3 };
#define W836X7HF_REG_PWM(type, nr) (((type) == w83627hf) ? \
				    regpwm_627hf[nr] : regpwm[nr])

#define W83627HF_REG_PWM_FREQ		0x5C	/* Only for the 627HF */

#define W83637HF_REG_PWM_FREQ1		0x00	/* 697HF/687THF too */
#define W83637HF_REG_PWM_FREQ2		0x02	/* 697HF/687THF too */
#define W83637HF_REG_PWM_FREQ3		0x10	/* 687THF too */

static const u8 W83637HF_REG_PWM_FREQ[] = { W83637HF_REG_PWM_FREQ1,
					W83637HF_REG_PWM_FREQ2,
					W83637HF_REG_PWM_FREQ3 };

#define W83627HF_BASE_PWM_FREQ	46870

#define W83781D_REG_I2C_ADDR 0x48
#define W83781D_REG_I2C_SUBADDR 0x4A

/* Sensor selection */
#define W83781D_REG_SCFG1 0x5D
static const u8 BIT_SCFG1[] = { 0x02, 0x04, 0x08 };
#define W83781D_REG_SCFG2 0x59
static const u8 BIT_SCFG2[] = { 0x10, 0x20, 0x40 };
#define W83781D_DEFAULT_BETA 3435

/*
 * Conversions. Limit checking is only done on the TO_REG
 * variants. Note that you should be a bit careful with which arguments
 * these macros are called: arguments may be evaluated more than once.
 * Fixing this is just not worth it.
 */
#define IN_TO_REG(val)  (clamp_val((((val) + 8) / 16), 0, 255))
#define IN_FROM_REG(val) ((val) * 16)

static inline u8 FAN_TO_REG(long rpm, int div)
{
	if (rpm == 0)
		return 255;
	rpm = clamp_val(rpm, 1, 1000000);
	return clamp_val((1350000 + rpm * div / 2) / (rpm * div), 1, 254);
}

#define TEMP_MIN (-128000)
#define TEMP_MAX ( 127000)

/*
 * TEMP: 0.001C/bit (-128C to +127C)
 * REG: 1C/bit, two's complement
 */
static u8 TEMP_TO_REG(long temp)
{
	int ntemp = clamp_val(temp, TEMP_MIN, TEMP_MAX);
	ntemp += (ntemp < 0 ? -500 : 500);
	return (u8)(ntemp / 1000);
}

static int TEMP_FROM_REG(u8 reg)
{
        return (s8)reg * 1000;
}

#define FAN_FROM_REG(val,div) ((val)==0?-1:(val)==255?0:1350000/((val)*(div)))

#define PWM_TO_REG(val) (clamp_val((val), 0, 255))

static inline unsigned long pwm_freq_from_reg_627hf(u8 reg)
{
	unsigned long freq;
	freq = W83627HF_BASE_PWM_FREQ >> reg;
	return freq;
}
static inline u8 pwm_freq_to_reg_627hf(unsigned long val)
{
	u8 i;
	/*
	 * Only 5 dividers (1 2 4 8 16)
	 * Search for the nearest available frequency
	 */
	for (i = 0; i < 4; i++) {
		if (val > (((W83627HF_BASE_PWM_FREQ >> i) +
			    (W83627HF_BASE_PWM_FREQ >> (i+1))) / 2))
			break;
	}
	return i;
}

static inline unsigned long pwm_freq_from_reg(u8 reg)
{
	/* Clock bit 8 -> 180 kHz or 24 MHz */
	unsigned long clock = (reg & 0x80) ? 180000UL : 24000000UL;

	reg &= 0x7f;
	/* This should not happen but anyway... */
	if (reg == 0)
		reg++;
	return clock / (reg << 8);
}
static inline u8 pwm_freq_to_reg(unsigned long val)
{
	/* Minimum divider value is 0x01 and maximum is 0x7F */
	if (val >= 93750)	/* The highest we can do */
		return 0x01;
	if (val >= 720)	/* Use 24 MHz clock */
		return 24000000UL / (val << 8);
	if (val < 6)		/* The lowest we can do */
		return 0xFF;
	else			/* Use 180 kHz clock */
		return 0x80 | (180000UL / (val << 8));
}

#define BEEP_MASK_FROM_REG(val)		((val) & 0xff7fff)
#define BEEP_MASK_TO_REG(val)		((val) & 0xff7fff)

#define DIV_FROM_REG(val) (1 << (val))

static inline u8 DIV_TO_REG(long val)
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

/*
 * For each registered chip, we need to keep some data in memory.
 * The structure is dynamically allocated.
 */
struct w83627hf_data {
	unsigned short addr;
	const char *name;
	struct device *hwmon_dev;
	struct mutex lock;
	enum chips type;

	struct mutex update_lock;
	bool valid;		/* true if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[9];		/* Register value */
	u8 in_max[9];		/* Register value */
	u8 in_min[9];		/* Register value */
	u8 fan[3];		/* Register value */
	u8 fan_min[3];		/* Register value */
	u16 temp[3];		/* Register value */
	u16 temp_max[3];	/* Register value */
	u16 temp_max_hyst[3];	/* Register value */
	u8 fan_div[3];		/* Register encoding, shifted right */
	u8 vid;			/* Register encoding, combined */
	u32 alarms;		/* Register encoding, combined */
	u32 beep_mask;		/* Register encoding, combined */
	u8 pwm[3];		/* Register value */
	u8 pwm_enable[3];	/* 1 = manual
				 * 2 = thermal cruise (also called SmartFan I)
				 * 3 = fan speed cruise
				 */
	u8 pwm_freq[3];		/* Register value */
	u16 sens[3];		/* 1 = pentium diode; 2 = 3904 diode;
				 * 4 = thermistor
				 */
	u8 vrm;
	u8 vrm_ovt;		/* Register value, 627THF/637HF/687THF only */

#ifdef CONFIG_PM
	/* Remember extra register values over suspend/resume */
	u8 scfg1;
	u8 scfg2;
#endif
};

/* Registers 0x50-0x5f are banked */
static inline void w83627hf_set_bank(struct w83627hf_data *data, u16 reg)
{
	if ((reg & 0x00f0) == 0x50) {
		outb_p(W83781D_REG_BANK, data->addr + W83781D_ADDR_REG_OFFSET);
		outb_p(reg >> 8, data->addr + W83781D_DATA_REG_OFFSET);
	}
}

/* Not strictly necessary, but play it safe for now */
static inline void w83627hf_reset_bank(struct w83627hf_data *data, u16 reg)
{
	if (reg & 0xff00) {
		outb_p(W83781D_REG_BANK, data->addr + W83781D_ADDR_REG_OFFSET);
		outb_p(0, data->addr + W83781D_DATA_REG_OFFSET);
	}
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
	w83627hf_set_bank(data, reg);
	outb_p(reg & 0xff, data->addr + W83781D_ADDR_REG_OFFSET);
	res = inb_p(data->addr + W83781D_DATA_REG_OFFSET);
	if (word_sized) {
		outb_p((reg & 0xff) + 1,
		       data->addr + W83781D_ADDR_REG_OFFSET);
		res =
		    (res << 8) + inb_p(data->addr +
				       W83781D_DATA_REG_OFFSET);
	}
	w83627hf_reset_bank(data, reg);
	mutex_unlock(&data->lock);
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
	w83627hf_set_bank(data, reg);
	outb_p(reg & 0xff, data->addr + W83781D_ADDR_REG_OFFSET);
	if (word_sized) {
		outb_p(value >> 8,
		       data->addr + W83781D_DATA_REG_OFFSET);
		outb_p((reg & 0xff) + 1,
		       data->addr + W83781D_ADDR_REG_OFFSET);
	}
	outb_p(value & 0xff,
	       data->addr + W83781D_DATA_REG_OFFSET);
	w83627hf_reset_bank(data, reg);
	mutex_unlock(&data->lock);
	return 0;
}

static void w83627hf_update_fan_div(struct w83627hf_data *data)
{
	int reg;

	reg = w83627hf_read_value(data, W83781D_REG_VID_FANDIV);
	data->fan_div[0] = (reg >> 4) & 0x03;
	data->fan_div[1] = (reg >> 6) & 0x03;
	if (data->type != w83697hf) {
		data->fan_div[2] = (w83627hf_read_value(data,
				       W83781D_REG_PIN) >> 6) & 0x03;
	}
	reg = w83627hf_read_value(data, W83781D_REG_VBAT);
	data->fan_div[0] |= (reg >> 3) & 0x04;
	data->fan_div[1] |= (reg >> 4) & 0x04;
	if (data->type != w83697hf)
		data->fan_div[2] |= (reg >> 5) & 0x04;
}

static struct w83627hf_data *w83627hf_update_device(struct device *dev)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	int i, num_temps = (data->type == w83697hf) ? 2 : 3;
	int num_pwms = (data->type == w83697hf) ? 2 : 3;

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
		for (i = 0; i <= 2; i++) {
			data->fan[i] =
			    w83627hf_read_value(data, W83627HF_REG_FAN(i));
			data->fan_min[i] =
			    w83627hf_read_value(data,
					       W83627HF_REG_FAN_MIN(i));
		}
		for (i = 0; i <= 2; i++) {
			u8 tmp = w83627hf_read_value(data,
				W836X7HF_REG_PWM(data->type, i));
			/* bits 0-3 are reserved  in 627THF */
			if (data->type == w83627thf)
				tmp &= 0xf0;
			data->pwm[i] = tmp;
			if (i == 1 &&
			    (data->type == w83627hf || data->type == w83697hf))
				break;
		}
		if (data->type == w83627hf) {
				u8 tmp = w83627hf_read_value(data,
						W83627HF_REG_PWM_FREQ);
				data->pwm_freq[0] = tmp & 0x07;
				data->pwm_freq[1] = (tmp >> 4) & 0x07;
		} else if (data->type != w83627thf) {
			for (i = 1; i <= 3; i++) {
				data->pwm_freq[i - 1] =
					w83627hf_read_value(data,
						W83637HF_REG_PWM_FREQ[i - 1]);
				if (i == 2 && (data->type == w83697hf))
					break;
			}
		}
		if (data->type != w83627hf) {
			for (i = 0; i < num_pwms; i++) {
				u8 tmp = w83627hf_read_value(data,
					W83627THF_REG_PWM_ENABLE[i]);
				data->pwm_enable[i] =
					((tmp >> W83627THF_PWM_ENABLE_SHIFT[i])
					& 0x03) + 1;
			}
		}
		for (i = 0; i < num_temps; i++) {
			data->temp[i] = w83627hf_read_value(
						data, w83627hf_reg_temp[i]);
			data->temp_max[i] = w83627hf_read_value(
						data, w83627hf_reg_temp_over[i]);
			data->temp_max_hyst[i] = w83627hf_read_value(
						data, w83627hf_reg_temp_hyst[i]);
		}

		w83627hf_update_fan_div(data);

		data->alarms =
		    w83627hf_read_value(data, W83781D_REG_ALARM1) |
		    (w83627hf_read_value(data, W83781D_REG_ALARM2) << 8) |
		    (w83627hf_read_value(data, W83781D_REG_ALARM3) << 16);
		i = w83627hf_read_value(data, W83781D_REG_BEEP_INTS2);
		data->beep_mask = (i << 8) |
		    w83627hf_read_value(data, W83781D_REG_BEEP_INTS1) |
		    w83627hf_read_value(data, W83781D_REG_BEEP_INTS3) << 16;
		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

#ifdef CONFIG_PM
static int w83627hf_suspend(struct device *dev)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);

	mutex_lock(&data->update_lock);
	data->scfg1 = w83627hf_read_value(data, W83781D_REG_SCFG1);
	data->scfg2 = w83627hf_read_value(data, W83781D_REG_SCFG2);
	mutex_unlock(&data->update_lock);

	return 0;
}

static int w83627hf_resume(struct device *dev)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	int i, num_temps = (data->type == w83697hf) ? 2 : 3;

	/* Restore limits */
	mutex_lock(&data->update_lock);
	for (i = 0; i <= 8; i++) {
		/* skip missing sensors */
		if (((data->type == w83697hf) && (i == 1)) ||
		    ((data->type != w83627hf && data->type != w83697hf)
		    && (i == 5 || i == 6)))
			continue;
		w83627hf_write_value(data, W83781D_REG_IN_MAX(i),
				     data->in_max[i]);
		w83627hf_write_value(data, W83781D_REG_IN_MIN(i),
				     data->in_min[i]);
	}
	for (i = 0; i <= 2; i++)
		w83627hf_write_value(data, W83627HF_REG_FAN_MIN(i),
				     data->fan_min[i]);
	for (i = 0; i < num_temps; i++) {
		w83627hf_write_value(data, w83627hf_reg_temp_over[i],
				     data->temp_max[i]);
		w83627hf_write_value(data, w83627hf_reg_temp_hyst[i],
				     data->temp_max_hyst[i]);
	}

	/* Fixup BIOS bugs */
	if (data->type == w83627thf || data->type == w83637hf ||
	    data->type == w83687thf)
		w83627hf_write_value(data, W83627THF_REG_VRM_OVT_CFG,
				     data->vrm_ovt);
	w83627hf_write_value(data, W83781D_REG_SCFG1, data->scfg1);
	w83627hf_write_value(data, W83781D_REG_SCFG2, data->scfg2);

	/* Force re-reading all values */
	data->valid = false;
	mutex_unlock(&data->update_lock);

	return 0;
}

static const struct dev_pm_ops w83627hf_dev_pm_ops = {
	.suspend = w83627hf_suspend,
	.resume = w83627hf_resume,
};

#define W83627HF_DEV_PM_OPS	(&w83627hf_dev_pm_ops)
#else
#define W83627HF_DEV_PM_OPS	NULL
#endif /* CONFIG_PM */

static int w83627thf_read_gpio5(struct platform_device *pdev)
{
	struct w83627hf_sio_data *sio_data = dev_get_platdata(&pdev->dev);
	int res = 0xff, sel;

	if (superio_enter(sio_data)) {
		/*
		 * Some other driver reserved the address space for itself.
		 * We don't want to fail driver instantiation because of that,
		 * so display a warning and keep going.
		 */
		dev_warn(&pdev->dev,
			 "Can not read VID data: Failed to enable SuperIO access\n");
		return res;
	}

	superio_select(sio_data, W83627HF_LD_GPIO5);

	res = 0xff;

	/* Make sure these GPIO pins are enabled */
	if (!(superio_inb(sio_data, W83627THF_GPIO5_EN) & (1<<3))) {
		dev_dbg(&pdev->dev, "GPIO5 disabled, no VID function\n");
		goto exit;
	}

	/*
	 * Make sure the pins are configured for input
	 * There must be at least five (VRM 9), and possibly 6 (VRM 10)
	 */
	sel = superio_inb(sio_data, W83627THF_GPIO5_IOSR) & 0x3f;
	if ((sel & 0x1f) != 0x1f) {
		dev_dbg(&pdev->dev, "GPIO5 not configured for VID "
			"function\n");
		goto exit;
	}

	dev_info(&pdev->dev, "Reading VID from GPIO5\n");
	res = superio_inb(sio_data, W83627THF_GPIO5_DR) & sel;

exit:
	superio_exit(sio_data);
	return res;
}

static int w83687thf_read_vid(struct platform_device *pdev)
{
	struct w83627hf_sio_data *sio_data = dev_get_platdata(&pdev->dev);
	int res = 0xff;

	if (superio_enter(sio_data)) {
		/*
		 * Some other driver reserved the address space for itself.
		 * We don't want to fail driver instantiation because of that,
		 * so display a warning and keep going.
		 */
		dev_warn(&pdev->dev,
			 "Can not read VID data: Failed to enable SuperIO access\n");
		return res;
	}

	superio_select(sio_data, W83627HF_LD_HWM);

	/* Make sure these GPIO pins are enabled */
	if (!(superio_inb(sio_data, W83687THF_VID_EN) & (1 << 2))) {
		dev_dbg(&pdev->dev, "VID disabled, no VID function\n");
		goto exit;
	}

	/* Make sure the pins are configured for input */
	if (!(superio_inb(sio_data, W83687THF_VID_CFG) & (1 << 4))) {
		dev_dbg(&pdev->dev, "VID configured as output, "
			"no VID function\n");
		goto exit;
	}

	res = superio_inb(sio_data, W83687THF_VID_DATA) & 0x3f;

exit:
	superio_exit(sio_data);
	return res;
}

static void w83627hf_init_device(struct platform_device *pdev)
{
	struct w83627hf_data *data = platform_get_drvdata(pdev);
	int i;
	enum chips type = data->type;
	u8 tmp;

	/* Minimize conflicts with other winbond i2c-only clients...  */
	/* disable i2c subclients... how to disable main i2c client?? */
	/* force i2c address to relatively uncommon address */
	if (type == w83627hf) {
		w83627hf_write_value(data, W83781D_REG_I2C_SUBADDR, 0x89);
		w83627hf_write_value(data, W83781D_REG_I2C_ADDR, force_i2c);
	}

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
			data->sens[i - 1] = 4;
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
		tmp = w83627hf_read_value(data, W83627HF_REG_TEMP2_CONFIG);
		if (tmp & 0x01) {
			dev_warn(&pdev->dev, "Enabling temp2, readings "
				 "might not make sense\n");
			w83627hf_write_value(data, W83627HF_REG_TEMP2_CONFIG,
				tmp & 0xfe);
		}

		/* Enable temp3 */
		if (type != w83697hf) {
			tmp = w83627hf_read_value(data,
				W83627HF_REG_TEMP3_CONFIG);
			if (tmp & 0x01) {
				dev_warn(&pdev->dev, "Enabling temp3, "
					 "readings might not make sense\n");
				w83627hf_write_value(data,
					W83627HF_REG_TEMP3_CONFIG, tmp & 0xfe);
			}
		}
	}

	/* Start monitoring */
	w83627hf_write_value(data, W83781D_REG_CONFIG,
			    (w83627hf_read_value(data,
						W83781D_REG_CONFIG) & 0xf7)
			    | 0x01);

	/* Enable VBAT monitoring if needed */
	tmp = w83627hf_read_value(data, W83781D_REG_VBAT);
	if (!(tmp & 0x01))
		w83627hf_write_value(data, W83781D_REG_VBAT, tmp | 0x01);
}

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

static ssize_t in0_input_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return show_in_0(data, buf, data->in[0]);
}
static DEVICE_ATTR_RO(in0_input);

static ssize_t in0_min_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return show_in_0(data, buf, data->in_min[0]);
}

static ssize_t in0_min_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	
	if ((data->vrm_ovt & 0x01) &&
		(w83627thf == data->type || w83637hf == data->type
		 || w83687thf == data->type))

		/* use VRM9 calculation */
		data->in_min[0] =
			clamp_val(((val * 100) - 70000 + 244) / 488, 0, 255);
	else
		/* use VRM8 (standard) calculation */
		data->in_min[0] = IN_TO_REG(val);

	w83627hf_write_value(data, W83781D_REG_IN_MIN(0), data->in_min[0]);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR_RW(in0_min);

static ssize_t in0_max_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return show_in_0(data, buf, data->in_max[0]);
}

static ssize_t in0_max_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);

	if ((data->vrm_ovt & 0x01) &&
		(w83627thf == data->type || w83637hf == data->type
		 || w83687thf == data->type))
		
		/* use VRM9 calculation */
		data->in_max[0] =
			clamp_val(((val * 100) - 70000 + 244) / 488, 0, 255);
	else
		/* use VRM8 (standard) calculation */
		data->in_max[0] = IN_TO_REG(val);

	w83627hf_write_value(data, W83781D_REG_IN_MAX(0), data->in_max[0]);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR_RW(in0_max);

static ssize_t
alarm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	int bitnr = to_sensor_dev_attr(attr)->index;
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR_RO(in0_alarm, alarm, 0);
static SENSOR_DEVICE_ATTR_RO(in1_alarm, alarm, 1);
static SENSOR_DEVICE_ATTR_RO(in2_alarm, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(in3_alarm, alarm, 3);
static SENSOR_DEVICE_ATTR_RO(in4_alarm, alarm, 8);
static SENSOR_DEVICE_ATTR_RO(in5_alarm, alarm, 9);
static SENSOR_DEVICE_ATTR_RO(in6_alarm, alarm, 10);
static SENSOR_DEVICE_ATTR_RO(in7_alarm, alarm, 16);
static SENSOR_DEVICE_ATTR_RO(in8_alarm, alarm, 17);
static SENSOR_DEVICE_ATTR_RO(fan1_alarm, alarm, 6);
static SENSOR_DEVICE_ATTR_RO(fan2_alarm, alarm, 7);
static SENSOR_DEVICE_ATTR_RO(fan3_alarm, alarm, 11);
static SENSOR_DEVICE_ATTR_RO(temp1_alarm, alarm, 4);
static SENSOR_DEVICE_ATTR_RO(temp2_alarm, alarm, 5);
static SENSOR_DEVICE_ATTR_RO(temp3_alarm, alarm, 13);

static ssize_t
beep_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	int bitnr = to_sensor_dev_attr(attr)->index;
	return sprintf(buf, "%u\n", (data->beep_mask >> bitnr) & 1);
}

static ssize_t
beep_store(struct device *dev, struct device_attribute *attr, const char *buf,
	   size_t count)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	int bitnr = to_sensor_dev_attr(attr)->index;
	u8 reg;
	unsigned long bit;
	int err;

	err = kstrtoul(buf, 10, &bit);
	if (err)
		return err;

	if (bit & ~1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	if (bit)
		data->beep_mask |= (1 << bitnr);
	else
		data->beep_mask &= ~(1 << bitnr);

	if (bitnr < 8) {
		reg = w83627hf_read_value(data, W83781D_REG_BEEP_INTS1);
		if (bit)
			reg |= (1 << bitnr);
		else
			reg &= ~(1 << bitnr);
		w83627hf_write_value(data, W83781D_REG_BEEP_INTS1, reg);
	} else if (bitnr < 16) {
		reg = w83627hf_read_value(data, W83781D_REG_BEEP_INTS2);
		if (bit)
			reg |= (1 << (bitnr - 8));
		else
			reg &= ~(1 << (bitnr - 8));
		w83627hf_write_value(data, W83781D_REG_BEEP_INTS2, reg);
	} else {
		reg = w83627hf_read_value(data, W83781D_REG_BEEP_INTS3);
		if (bit)
			reg |= (1 << (bitnr - 16));
		else
			reg &= ~(1 << (bitnr - 16));
		w83627hf_write_value(data, W83781D_REG_BEEP_INTS3, reg);
	}
	mutex_unlock(&data->update_lock);

	return count;
}

static SENSOR_DEVICE_ATTR_RW(in0_beep, beep, 0);
static SENSOR_DEVICE_ATTR_RW(in1_beep, beep, 1);
static SENSOR_DEVICE_ATTR_RW(in2_beep, beep, 2);
static SENSOR_DEVICE_ATTR_RW(in3_beep, beep, 3);
static SENSOR_DEVICE_ATTR_RW(in4_beep, beep, 8);
static SENSOR_DEVICE_ATTR_RW(in5_beep, beep, 9);
static SENSOR_DEVICE_ATTR_RW(in6_beep, beep, 10);
static SENSOR_DEVICE_ATTR_RW(in7_beep, beep, 16);
static SENSOR_DEVICE_ATTR_RW(in8_beep, beep, 17);
static SENSOR_DEVICE_ATTR_RW(fan1_beep, beep, 6);
static SENSOR_DEVICE_ATTR_RW(fan2_beep, beep, 7);
static SENSOR_DEVICE_ATTR_RW(fan3_beep, beep, 11);
static SENSOR_DEVICE_ATTR_RW(temp1_beep, beep, 4);
static SENSOR_DEVICE_ATTR_RW(temp2_beep, beep, 5);
static SENSOR_DEVICE_ATTR_RW(temp3_beep, beep, 13);
static SENSOR_DEVICE_ATTR_RW(beep_enable, beep, 15);

static ssize_t
in_input_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long)IN_FROM_REG(data->in[nr]));
}

static ssize_t
in_min_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long)IN_FROM_REG(data->in_min[nr]));
}

static ssize_t
in_min_store(struct device *dev, struct device_attribute *devattr,
	     const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_min[nr] = IN_TO_REG(val);
	w83627hf_write_value(data, W83781D_REG_IN_MIN(nr), data->in_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
in_max_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long)IN_FROM_REG(data->in_max[nr]));
}

static ssize_t
in_max_store(struct device *dev, struct device_attribute *devattr,
	     const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_max[nr] = IN_TO_REG(val);
	w83627hf_write_value(data, W83781D_REG_IN_MAX(nr), data->in_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(in1_input, in_input, 1);
static SENSOR_DEVICE_ATTR_RW(in1_min, in_min, 1);
static SENSOR_DEVICE_ATTR_RW(in1_max, in_max, 1);
static SENSOR_DEVICE_ATTR_RO(in2_input, in_input, 2);
static SENSOR_DEVICE_ATTR_RW(in2_min, in_min, 2);
static SENSOR_DEVICE_ATTR_RW(in2_max, in_max, 2);
static SENSOR_DEVICE_ATTR_RO(in3_input, in_input, 3);
static SENSOR_DEVICE_ATTR_RW(in3_min, in_min, 3);
static SENSOR_DEVICE_ATTR_RW(in3_max, in_max, 3);
static SENSOR_DEVICE_ATTR_RO(in4_input, in_input, 4);
static SENSOR_DEVICE_ATTR_RW(in4_min, in_min, 4);
static SENSOR_DEVICE_ATTR_RW(in4_max, in_max, 4);
static SENSOR_DEVICE_ATTR_RO(in5_input, in_input, 5);
static SENSOR_DEVICE_ATTR_RW(in5_min, in_min, 5);
static SENSOR_DEVICE_ATTR_RW(in5_max, in_max, 5);
static SENSOR_DEVICE_ATTR_RO(in6_input, in_input, 6);
static SENSOR_DEVICE_ATTR_RW(in6_min, in_min, 6);
static SENSOR_DEVICE_ATTR_RW(in6_max, in_max, 6);
static SENSOR_DEVICE_ATTR_RO(in7_input, in_input, 7);
static SENSOR_DEVICE_ATTR_RW(in7_min, in_min, 7);
static SENSOR_DEVICE_ATTR_RW(in7_max, in_max, 7);
static SENSOR_DEVICE_ATTR_RO(in8_input, in_input, 8);
static SENSOR_DEVICE_ATTR_RW(in8_min, in_min, 8);
static SENSOR_DEVICE_ATTR_RW(in8_max, in_max, 8);

static ssize_t
fan_input_show(struct device *dev, struct device_attribute *devattr,
	       char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", FAN_FROM_REG(data->fan[nr],
				(long)DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t
fan_min_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", FAN_FROM_REG(data->fan_min[nr],
				(long)DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t
fan_min_store(struct device *dev, struct device_attribute *devattr,
	      const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	w83627hf_write_value(data, W83627HF_REG_FAN_MIN(nr),
			     data->fan_min[nr]);

	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(fan1_input, fan_input, 0);
static SENSOR_DEVICE_ATTR_RW(fan1_min, fan_min, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan_input, 1);
static SENSOR_DEVICE_ATTR_RW(fan2_min, fan_min, 1);
static SENSOR_DEVICE_ATTR_RO(fan3_input, fan_input, 2);
static SENSOR_DEVICE_ATTR_RW(fan3_min, fan_min, 2);

static ssize_t
fan_div_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n",
		       (long) DIV_FROM_REG(data->fan_div[nr]));
}

/*
 * Note: we save and restore the fan minimum here, because its value is
 * determined in part by the fan divisor.  This follows the principle of
 * least surprise; the user doesn't expect the fan minimum to change just
 * because the divisor changed.
 */
static ssize_t
fan_div_store(struct device *dev, struct device_attribute *devattr,
	      const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = dev_get_drvdata(dev);
	unsigned long min;
	u8 reg;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

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
	w83627hf_write_value(data, W83627HF_REG_FAN_MIN(nr), data->fan_min[nr]);

	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(fan1_div, fan_div, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_div, fan_div, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_div, fan_div, 2);

static ssize_t
temp_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);

	u16 tmp = data->temp[nr];
	return sprintf(buf, "%ld\n", (nr) ? (long) LM75_TEMP_FROM_REG(tmp)
					  : (long) TEMP_FROM_REG(tmp));
}

static ssize_t
temp_max_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);

	u16 tmp = data->temp_max[nr];
	return sprintf(buf, "%ld\n", (nr) ? (long) LM75_TEMP_FROM_REG(tmp)
					  : (long) TEMP_FROM_REG(tmp));
}

static ssize_t
temp_max_store(struct device *dev, struct device_attribute *devattr,
	       const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = dev_get_drvdata(dev);
	u16 tmp;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	tmp = (nr) ? LM75_TEMP_TO_REG(val) : TEMP_TO_REG(val);
	mutex_lock(&data->update_lock);
	data->temp_max[nr] = tmp;
	w83627hf_write_value(data, w83627hf_reg_temp_over[nr], tmp);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
temp_max_hyst_show(struct device *dev, struct device_attribute *devattr,
		   char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);

	u16 tmp = data->temp_max_hyst[nr];
	return sprintf(buf, "%ld\n", (nr) ? (long) LM75_TEMP_FROM_REG(tmp)
					  : (long) TEMP_FROM_REG(tmp));
}

static ssize_t
temp_max_hyst_store(struct device *dev, struct device_attribute *devattr,
		    const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = dev_get_drvdata(dev);
	u16 tmp;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	tmp = (nr) ? LM75_TEMP_TO_REG(val) : TEMP_TO_REG(val);
	mutex_lock(&data->update_lock);
	data->temp_max_hyst[nr] = tmp;
	w83627hf_write_value(data, w83627hf_reg_temp_hyst[nr], tmp);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp_max, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_max_hyst, temp_max_hyst, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp_max, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_max_hyst, temp_max_hyst, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_max, temp_max, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_max_hyst, temp_max_hyst, 2);

static ssize_t
temp_type_show(struct device *dev, struct device_attribute *devattr,
	       char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->sens[nr]);
}

static ssize_t
temp_type_store(struct device *dev, struct device_attribute *devattr,
		const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = dev_get_drvdata(dev);
	unsigned long val;
	u32 tmp;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);

	switch (val) {
	case 1:		/* PII/Celeron diode */
		tmp = w83627hf_read_value(data, W83781D_REG_SCFG1);
		w83627hf_write_value(data, W83781D_REG_SCFG1,
				    tmp | BIT_SCFG1[nr]);
		tmp = w83627hf_read_value(data, W83781D_REG_SCFG2);
		w83627hf_write_value(data, W83781D_REG_SCFG2,
				    tmp | BIT_SCFG2[nr]);
		data->sens[nr] = val;
		break;
	case 2:		/* 3904 */
		tmp = w83627hf_read_value(data, W83781D_REG_SCFG1);
		w83627hf_write_value(data, W83781D_REG_SCFG1,
				    tmp | BIT_SCFG1[nr]);
		tmp = w83627hf_read_value(data, W83781D_REG_SCFG2);
		w83627hf_write_value(data, W83781D_REG_SCFG2,
				    tmp & ~BIT_SCFG2[nr]);
		data->sens[nr] = val;
		break;
	case W83781D_DEFAULT_BETA:
		dev_warn(dev, "Sensor type %d is deprecated, please use 4 "
			 "instead\n", W83781D_DEFAULT_BETA);
		fallthrough;
	case 4:		/* thermistor */
		tmp = w83627hf_read_value(data, W83781D_REG_SCFG1);
		w83627hf_write_value(data, W83781D_REG_SCFG1,
				    tmp & ~BIT_SCFG1[nr]);
		data->sens[nr] = val;
		break;
	default:
		dev_err(dev,
		       "Invalid sensor type %ld; must be 1, 2, or 4\n",
		       (long) val);
		break;
	}

	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_type, temp_type, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_type, temp_type, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_type, temp_type, 2);

static ssize_t
alarms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->alarms);
}
static DEVICE_ATTR_RO(alarms);

#define VIN_UNIT_ATTRS(_X_)	\
	&sensor_dev_attr_in##_X_##_input.dev_attr.attr,		\
	&sensor_dev_attr_in##_X_##_min.dev_attr.attr,		\
	&sensor_dev_attr_in##_X_##_max.dev_attr.attr,		\
	&sensor_dev_attr_in##_X_##_alarm.dev_attr.attr,		\
	&sensor_dev_attr_in##_X_##_beep.dev_attr.attr

#define FAN_UNIT_ATTRS(_X_)	\
	&sensor_dev_attr_fan##_X_##_input.dev_attr.attr,	\
	&sensor_dev_attr_fan##_X_##_min.dev_attr.attr,		\
	&sensor_dev_attr_fan##_X_##_div.dev_attr.attr,		\
	&sensor_dev_attr_fan##_X_##_alarm.dev_attr.attr,	\
	&sensor_dev_attr_fan##_X_##_beep.dev_attr.attr

#define TEMP_UNIT_ATTRS(_X_)	\
	&sensor_dev_attr_temp##_X_##_input.dev_attr.attr,	\
	&sensor_dev_attr_temp##_X_##_max.dev_attr.attr,		\
	&sensor_dev_attr_temp##_X_##_max_hyst.dev_attr.attr,	\
	&sensor_dev_attr_temp##_X_##_type.dev_attr.attr,	\
	&sensor_dev_attr_temp##_X_##_alarm.dev_attr.attr,	\
	&sensor_dev_attr_temp##_X_##_beep.dev_attr.attr

static ssize_t
beep_mask_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n",
		      (long)BEEP_MASK_FROM_REG(data->beep_mask));
}

static ssize_t
beep_mask_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);

	/* preserve beep enable */
	data->beep_mask = (data->beep_mask & 0x8000)
			| BEEP_MASK_TO_REG(val);
	w83627hf_write_value(data, W83781D_REG_BEEP_INTS1,
			    data->beep_mask & 0xff);
	w83627hf_write_value(data, W83781D_REG_BEEP_INTS3,
			    ((data->beep_mask) >> 16) & 0xff);
	w83627hf_write_value(data, W83781D_REG_BEEP_INTS2,
			    (data->beep_mask >> 8) & 0xff);

	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR_RW(beep_mask);

static ssize_t
pwm_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->pwm[nr]);
}

static ssize_t
pwm_store(struct device *dev, struct device_attribute *devattr,
	  const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);

	if (data->type == w83627thf) {
		/* bits 0-3 are reserved  in 627THF */
		data->pwm[nr] = PWM_TO_REG(val) & 0xf0;
		w83627hf_write_value(data,
				     W836X7HF_REG_PWM(data->type, nr),
				     data->pwm[nr] |
				     (w83627hf_read_value(data,
				     W836X7HF_REG_PWM(data->type, nr)) & 0x0f));
	} else {
		data->pwm[nr] = PWM_TO_REG(val);
		w83627hf_write_value(data,
				     W836X7HF_REG_PWM(data->type, nr),
				     data->pwm[nr]);
	}

	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1, pwm, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2, pwm, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3, pwm, 2);

static ssize_t
name_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}

static DEVICE_ATTR_RO(name);

static struct attribute *w83627hf_attributes[] = {
	&dev_attr_in0_input.attr,
	&dev_attr_in0_min.attr,
	&dev_attr_in0_max.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in0_beep.dev_attr.attr,
	VIN_UNIT_ATTRS(2),
	VIN_UNIT_ATTRS(3),
	VIN_UNIT_ATTRS(4),
	VIN_UNIT_ATTRS(7),
	VIN_UNIT_ATTRS(8),

	FAN_UNIT_ATTRS(1),
	FAN_UNIT_ATTRS(2),

	TEMP_UNIT_ATTRS(1),
	TEMP_UNIT_ATTRS(2),

	&dev_attr_alarms.attr,
	&sensor_dev_attr_beep_enable.dev_attr.attr,
	&dev_attr_beep_mask.attr,

	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&dev_attr_name.attr,
	NULL
};

static const struct attribute_group w83627hf_group = {
	.attrs = w83627hf_attributes,
};

static ssize_t
pwm_freq_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);
	if (data->type == w83627hf)
		return sprintf(buf, "%ld\n",
			pwm_freq_from_reg_627hf(data->pwm_freq[nr]));
	else
		return sprintf(buf, "%ld\n",
			pwm_freq_from_reg(data->pwm_freq[nr]));
}

static ssize_t
pwm_freq_store(struct device *dev, struct device_attribute *devattr,
	       const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = dev_get_drvdata(dev);
	static const u8 mask[]={0xF8, 0x8F};
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);

	if (data->type == w83627hf) {
		data->pwm_freq[nr] = pwm_freq_to_reg_627hf(val);
		w83627hf_write_value(data, W83627HF_REG_PWM_FREQ,
				(data->pwm_freq[nr] << (nr*4)) |
				(w83627hf_read_value(data,
				W83627HF_REG_PWM_FREQ) & mask[nr]));
	} else {
		data->pwm_freq[nr] = pwm_freq_to_reg(val);
		w83627hf_write_value(data, W83637HF_REG_PWM_FREQ[nr],
				data->pwm_freq[nr]);
	}

	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1_freq, pwm_freq, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_freq, pwm_freq, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3_freq, pwm_freq, 2);

static ssize_t
cpu0_vid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%ld\n", (long) vid_from_reg(data->vid, data->vrm));
}

static DEVICE_ATTR_RO(cpu0_vid);

static ssize_t
vrm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%ld\n", (long) data->vrm);
}

static ssize_t
vrm_store(struct device *dev, struct device_attribute *attr, const char *buf,
	  size_t count)
{
	struct w83627hf_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val > 255)
		return -EINVAL;
	data->vrm = val;

	return count;
}

static DEVICE_ATTR_RW(vrm);

static ssize_t
pwm_enable_show(struct device *dev, struct device_attribute *devattr,
		char *buf)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = w83627hf_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm_enable[nr]);
}

static ssize_t
pwm_enable_store(struct device *dev, struct device_attribute *devattr,
		 const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(devattr)->index;
	struct w83627hf_data *data = dev_get_drvdata(dev);
	u8 reg;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (!val || val > 3)	/* modes 1, 2 and 3 are supported */
		return -EINVAL;
	mutex_lock(&data->update_lock);
	data->pwm_enable[nr] = val;
	reg = w83627hf_read_value(data, W83627THF_REG_PWM_ENABLE[nr]);
	reg &= ~(0x03 << W83627THF_PWM_ENABLE_SHIFT[nr]);
	reg |= (val - 1) << W83627THF_PWM_ENABLE_SHIFT[nr];
	w83627hf_write_value(data, W83627THF_REG_PWM_ENABLE[nr], reg);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1_enable, pwm_enable, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_enable, pwm_enable, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3_enable, pwm_enable, 2);

static struct attribute *w83627hf_attributes_opt[] = {
	VIN_UNIT_ATTRS(1),
	VIN_UNIT_ATTRS(5),
	VIN_UNIT_ATTRS(6),

	FAN_UNIT_ATTRS(3),
	TEMP_UNIT_ATTRS(3),
	&sensor_dev_attr_pwm3.dev_attr.attr,

	&sensor_dev_attr_pwm1_freq.dev_attr.attr,
	&sensor_dev_attr_pwm2_freq.dev_attr.attr,
	&sensor_dev_attr_pwm3_freq.dev_attr.attr,

	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,

	NULL
};

static const struct attribute_group w83627hf_group_opt = {
	.attrs = w83627hf_attributes_opt,
};

static int w83627hf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct w83627hf_sio_data *sio_data = dev_get_platdata(dev);
	struct w83627hf_data *data;
	struct resource *res;
	int err, i;

	static const char *names[] = {
		"w83627hf",
		"w83627thf",
		"w83697hf",
		"w83637hf",
		"w83687thf",
	};

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!devm_request_region(dev, res->start, WINB_REGION_SIZE, DRVNAME)) {
		dev_err(dev, "Failed to request region 0x%lx-0x%lx\n",
			(unsigned long)res->start,
			(unsigned long)(res->start + WINB_REGION_SIZE - 1));
		return -EBUSY;
	}

	data = devm_kzalloc(dev, sizeof(struct w83627hf_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->addr = res->start;
	data->type = sio_data->type;
	data->name = names[sio_data->type];
	mutex_init(&data->lock);
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	/* Initialize the chip */
	w83627hf_init_device(pdev);

	/* A few vars need to be filled upon startup */
	for (i = 0; i <= 2; i++)
		data->fan_min[i] = w83627hf_read_value(
					data, W83627HF_REG_FAN_MIN(i));
	w83627hf_update_fan_div(data);

	/* Register common device attributes */
	err = sysfs_create_group(&dev->kobj, &w83627hf_group);
	if (err)
		return err;

	/* Register chip-specific device attributes */
	if (data->type == w83627hf || data->type == w83697hf)
		if ((err = device_create_file(dev,
				&sensor_dev_attr_in5_input.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in5_min.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in5_max.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in5_alarm.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in5_beep.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in6_input.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in6_min.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in6_max.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in6_alarm.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in6_beep.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_pwm1_freq.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_pwm2_freq.dev_attr)))
			goto error;

	if (data->type != w83697hf)
		if ((err = device_create_file(dev,
				&sensor_dev_attr_in1_input.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in1_min.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in1_max.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in1_alarm.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_in1_beep.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan3_input.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan3_min.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan3_div.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan3_alarm.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_fan3_beep.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_temp3_input.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_temp3_max.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_temp3_max_hyst.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_temp3_alarm.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_temp3_beep.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_temp3_type.dev_attr)))
			goto error;

	if (data->type != w83697hf && data->vid != 0xff) {
		/* Convert VID to voltage based on VRM */
		data->vrm = vid_which_vrm();

		if ((err = device_create_file(dev, &dev_attr_cpu0_vid))
		 || (err = device_create_file(dev, &dev_attr_vrm)))
			goto error;
	}

	if (data->type == w83627thf || data->type == w83637hf
	    || data->type == w83687thf) {
		err = device_create_file(dev, &sensor_dev_attr_pwm3.dev_attr);
		if (err)
			goto error;
	}

	if (data->type == w83637hf || data->type == w83687thf)
		if ((err = device_create_file(dev,
				&sensor_dev_attr_pwm1_freq.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_pwm2_freq.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_pwm3_freq.dev_attr)))
			goto error;

	if (data->type != w83627hf)
		if ((err = device_create_file(dev,
				&sensor_dev_attr_pwm1_enable.dev_attr))
		 || (err = device_create_file(dev,
				&sensor_dev_attr_pwm2_enable.dev_attr)))
			goto error;

	if (data->type == w83627thf || data->type == w83637hf
	    || data->type == w83687thf) {
		err = device_create_file(dev,
					 &sensor_dev_attr_pwm3_enable.dev_attr);
		if (err)
			goto error;
	}

	data->hwmon_dev = hwmon_device_register(dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto error;
	}

	return 0;

 error:
	sysfs_remove_group(&dev->kobj, &w83627hf_group);
	sysfs_remove_group(&dev->kobj, &w83627hf_group_opt);
	return err;
}

static void w83627hf_remove(struct platform_device *pdev)
{
	struct w83627hf_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);

	sysfs_remove_group(&pdev->dev.kobj, &w83627hf_group);
	sysfs_remove_group(&pdev->dev.kobj, &w83627hf_group_opt);
}

static struct platform_driver w83627hf_driver = {
	.driver = {
		.name	= DRVNAME,
		.pm	= W83627HF_DEV_PM_OPS,
	},
	.probe		= w83627hf_probe,
	.remove_new	= w83627hf_remove,
};

static int __init w83627hf_find(int sioaddr, unsigned short *addr,
				struct w83627hf_sio_data *sio_data)
{
	int err;
	u16 val;

	static __initconst char *const names[] = {
		"W83627HF",
		"W83627THF",
		"W83697HF",
		"W83637HF",
		"W83687THF",
	};

	sio_data->sioaddr = sioaddr;
	err = superio_enter(sio_data);
	if (err)
		return err;

	err = -ENODEV;
	val = force_id ? force_id : superio_inb(sio_data, DEVID);
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
	case 0xff:	/* No device at all */
		goto exit;
	default:
		pr_debug(DRVNAME ": Unsupported chip (DEVID=0x%02x)\n", val);
		goto exit;
	}

	superio_select(sio_data, W83627HF_LD_HWM);
	val = (superio_inb(sio_data, WINB_BASE_REG) << 8) |
	       superio_inb(sio_data, WINB_BASE_REG + 1);
	*addr = val & WINB_ALIGNMENT;
	if (*addr == 0) {
		pr_warn("Base address not set, skipping\n");
		goto exit;
	}

	val = superio_inb(sio_data, WINB_ACT_REG);
	if (!(val & 0x01)) {
		pr_warn("Enabling HWM logical device\n");
		superio_outb(sio_data, WINB_ACT_REG, val | 0x01);
	}

	err = 0;
	pr_info(DRVNAME ": Found %s chip at %#x\n",
		names[sio_data->type], *addr);

 exit:
	superio_exit(sio_data);
	return err;
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

	err = acpi_check_resource_conflict(&res);
	if (err)
		goto exit;

	pdev = platform_device_alloc(DRVNAME, address);
	if (!pdev) {
		err = -ENOMEM;
		pr_err("Device allocation failed\n");
		goto exit;
	}

	err = platform_device_add_resources(pdev, &res, 1);
	if (err) {
		pr_err("Device resource addition failed (%d)\n", err);
		goto exit_device_put;
	}

	err = platform_device_add_data(pdev, sio_data,
				       sizeof(struct w83627hf_sio_data));
	if (err) {
		pr_err("Platform data allocation failed\n");
		goto exit_device_put;
	}

	err = platform_device_add(pdev);
	if (err) {
		pr_err("Device addition failed (%d)\n", err);
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
