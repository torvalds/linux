/*
    w83627ehf - Driver for the hardware monitoring functionality of
                the Winbond W83627EHF Super-I/O chip
    Copyright (C) 2005  Jean Delvare <khali@linux-fr.org>
    Copyright (C) 2006  Yuan Mu (Winbond),
                        Rudolf Marek <r.marek@assembler.cz>
                        David Hubbard <david.c.hubbard@gmail.com>
			Daniel J Blueman <daniel.blueman@gmail.com>

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

    Chip        #vin    #fan    #pwm    #temp  chip IDs       man ID
    w83627ehf   10      5       4       3      0x8850 0x88    0x5ca3
                                               0x8860 0xa1
    w83627dhg    9      5       4       3      0xa020 0xc1    0x5ca3
    w83627dhg-p  9      5       4       3      0xb070 0xc1    0x5ca3
    w83667hg     9      5       3       3      0xa510 0xc1    0x5ca3
    w83667hg-b   9      5       3       3      0xb350 0xc1    0x5ca3
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
#include <linux/acpi.h>
#include <linux/io.h>
#include "lm75.h"

enum kinds { w83627ehf, w83627dhg, w83627dhg_p, w83667hg, w83667hg_b };

/* used to set data->name = w83627ehf_device_names[data->sio_kind] */
static const char * w83627ehf_device_names[] = {
	"w83627ehf",
	"w83627dhg",
	"w83627dhg",
	"w83667hg",
	"w83667hg",
};

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

#define DRVNAME "w83627ehf"

/*
 * Super-I/O constants and functions
 */

#define W83627EHF_LD_HWM	0x0b
#define W83667HG_LD_VID 	0x0d

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_EN_VRM10	0x2C	/* GPIO3, GPIO4 selection */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */
#define SIO_REG_VID_CTRL	0xF0	/* VID control */
#define SIO_REG_VID_DATA	0xF1	/* VID data */

#define SIO_W83627EHF_ID	0x8850
#define SIO_W83627EHG_ID	0x8860
#define SIO_W83627DHG_ID	0xa020
#define SIO_W83627DHG_P_ID	0xb070
#define SIO_W83667HG_ID 	0xa510
#define SIO_W83667HG_B_ID	0xb350
#define SIO_ID_MASK		0xFFF0

static inline void
superio_outb(int ioreg, int reg, int val)
{
	outb(reg, ioreg);
	outb(val, ioreg + 1);
}

static inline int
superio_inb(int ioreg, int reg)
{
	outb(reg, ioreg);
	return inb(ioreg + 1);
}

static inline void
superio_select(int ioreg, int ld)
{
	outb(SIO_REG_LDSEL, ioreg);
	outb(ld, ioreg + 1);
}

static inline void
superio_enter(int ioreg)
{
	outb(0x87, ioreg);
	outb(0x87, ioreg);
}

static inline void
superio_exit(int ioreg)
{
	outb(0xaa, ioreg);
	outb(0x02, ioreg);
	outb(0x02, ioreg + 1);
}

/*
 * ISA constants
 */

#define IOREGION_ALIGNMENT	~7
#define IOREGION_OFFSET		5
#define IOREGION_LENGTH		2
#define ADDR_REG_OFFSET		0
#define DATA_REG_OFFSET		1

#define W83627EHF_REG_BANK		0x4E
#define W83627EHF_REG_CONFIG		0x40

/* Not currently used:
 * REG_MAN_ID has the value 0x5ca3 for all supported chips.
 * REG_CHIP_ID == 0x88/0xa1/0xc1 depending on chip model.
 * REG_MAN_ID is at port 0x4f
 * REG_CHIP_ID is at port 0x58 */

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

/* SmartFan registers */
#define W83627EHF_REG_FAN_STEPUP_TIME 0x0f
#define W83627EHF_REG_FAN_STEPDOWN_TIME 0x0e

/* DC or PWM output fan configuration */
static const u8 W83627EHF_REG_PWM_ENABLE[] = {
	0x04,			/* SYS FAN0 output mode and PWM mode */
	0x04,			/* CPU FAN0 output mode and PWM mode */
	0x12,			/* AUX FAN mode */
	0x62,			/* CPU FAN1 mode */
};

static const u8 W83627EHF_PWM_MODE_SHIFT[] = { 0, 1, 0, 6 };
static const u8 W83627EHF_PWM_ENABLE_SHIFT[] = { 2, 4, 1, 4 };

/* FAN Duty Cycle, be used to control */
static const u8 W83627EHF_REG_PWM[] = { 0x01, 0x03, 0x11, 0x61 };
static const u8 W83627EHF_REG_TARGET[] = { 0x05, 0x06, 0x13, 0x63 };
static const u8 W83627EHF_REG_TOLERANCE[] = { 0x07, 0x07, 0x14, 0x62 };

/* Advanced Fan control, some values are common for all fans */
static const u8 W83627EHF_REG_FAN_START_OUTPUT[] = { 0x0a, 0x0b, 0x16, 0x65 };
static const u8 W83627EHF_REG_FAN_STOP_OUTPUT[] = { 0x08, 0x09, 0x15, 0x64 };
static const u8 W83627EHF_REG_FAN_STOP_TIME[] = { 0x0c, 0x0d, 0x17, 0x66 };

static const u8 W83627EHF_REG_FAN_MAX_OUTPUT_COMMON[]
						= { 0xff, 0x67, 0xff, 0x69 };
static const u8 W83627EHF_REG_FAN_STEP_OUTPUT_COMMON[]
						= { 0xff, 0x68, 0xff, 0x6a };

static const u8 W83627EHF_REG_FAN_MAX_OUTPUT_W83667_B[] = { 0x67, 0x69, 0x6b };
static const u8 W83627EHF_REG_FAN_STEP_OUTPUT_W83667_B[] = { 0x68, 0x6a, 0x6c };

/*
 * Conversions
 */

/* 1 is PWM mode, output in ms */
static inline unsigned int step_time_from_reg(u8 reg, u8 mode)
{
	return mode ? 100 * reg : 400 * reg;
}

static inline u8 step_time_to_reg(unsigned int msec, u8 mode)
{
	return SENSORS_LIMIT((mode ? (msec + 50) / 100 :
						(msec + 200) / 400), 1, 255);
}

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
temp1_to_reg(long temp, int min, int max)
{
	if (temp <= min)
		return min / 1000;
	if (temp >= max)
		return max / 1000;
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
	int addr;	/* IO base of hw monitor block */
	const char *name;

	struct device *hwmon_dev;
	struct mutex lock;

	const u8 *REG_FAN_START_OUTPUT;
	const u8 *REG_FAN_STOP_OUTPUT;
	const u8 *REG_FAN_MAX_OUTPUT;
	const u8 *REG_FAN_STEP_OUTPUT;

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* Register values */
	u8 in_num;		/* number of in inputs we have */
	u8 in[10];		/* Register value */
	u8 in_max[10];		/* Register value */
	u8 in_min[10];		/* Register value */
	u8 fan[5];
	u8 fan_min[5];
	u8 fan_div[5];
	u8 has_fan;		/* some fan inputs can be disabled */
	u8 temp_type[3];
	s8 temp1;
	s8 temp1_max;
	s8 temp1_max_hyst;
	s16 temp[2];
	s16 temp_max[2];
	s16 temp_max_hyst[2];
	u32 alarms;

	u8 pwm_mode[4]; /* 0->DC variable voltage, 1->PWM variable duty cycle */
	u8 pwm_enable[4]; /* 1->manual
			     2->thermal cruise mode (also called SmartFan I)
			     3->fan speed cruise mode
			     4->variable thermal cruise (also called SmartFan III) */
	u8 pwm_num;		/* number of pwm */
	u8 pwm[4];
	u8 target_temp[4];
	u8 tolerance[4];

	u8 fan_start_output[4]; /* minimum fan speed when spinning up */
	u8 fan_stop_output[4]; /* minimum fan speed when spinning down */
	u8 fan_stop_time[4]; /* time at minimum before disabling fan */
	u8 fan_max_output[4]; /* maximum fan speed */
	u8 fan_step_output[4]; /* rate of change output value */

	u8 vid;
	u8 vrm;

	u8 temp3_disable;
	u8 in6_skip;
};

struct w83627ehf_sio_data {
	int sioreg;
	enum kinds kind;
};

static inline int is_word_sized(u16 reg)
{
	return (((reg & 0xff00) == 0x100
	      || (reg & 0xff00) == 0x200)
	     && ((reg & 0x00ff) == 0x50
	      || (reg & 0x00ff) == 0x53
	      || (reg & 0x00ff) == 0x55));
}

/* Registers 0x50-0x5f are banked */
static inline void w83627ehf_set_bank(struct w83627ehf_data *data, u16 reg)
{
	if ((reg & 0x00f0) == 0x50) {
		outb_p(W83627EHF_REG_BANK, data->addr + ADDR_REG_OFFSET);
		outb_p(reg >> 8, data->addr + DATA_REG_OFFSET);
	}
}

/* Not strictly necessary, but play it safe for now */
static inline void w83627ehf_reset_bank(struct w83627ehf_data *data, u16 reg)
{
	if (reg & 0xff00) {
		outb_p(W83627EHF_REG_BANK, data->addr + ADDR_REG_OFFSET);
		outb_p(0, data->addr + DATA_REG_OFFSET);
	}
}

static u16 w83627ehf_read_value(struct w83627ehf_data *data, u16 reg)
{
	int res, word_sized = is_word_sized(reg);

	mutex_lock(&data->lock);

	w83627ehf_set_bank(data, reg);
	outb_p(reg & 0xff, data->addr + ADDR_REG_OFFSET);
	res = inb_p(data->addr + DATA_REG_OFFSET);
	if (word_sized) {
		outb_p((reg & 0xff) + 1,
		       data->addr + ADDR_REG_OFFSET);
		res = (res << 8) + inb_p(data->addr + DATA_REG_OFFSET);
	}
	w83627ehf_reset_bank(data, reg);

	mutex_unlock(&data->lock);

	return res;
}

static int w83627ehf_write_value(struct w83627ehf_data *data, u16 reg, u16 value)
{
	int word_sized = is_word_sized(reg);

	mutex_lock(&data->lock);

	w83627ehf_set_bank(data, reg);
	outb_p(reg & 0xff, data->addr + ADDR_REG_OFFSET);
	if (word_sized) {
		outb_p(value >> 8, data->addr + DATA_REG_OFFSET);
		outb_p((reg & 0xff) + 1,
		       data->addr + ADDR_REG_OFFSET);
	}
	outb_p(value & 0xff, data->addr + DATA_REG_OFFSET);
	w83627ehf_reset_bank(data, reg);

	mutex_unlock(&data->lock);
	return 0;
}

/* This function assumes that the caller holds data->update_lock */
static void w83627ehf_write_fan_div(struct w83627ehf_data *data, int nr)
{
	u8 reg;

	switch (nr) {
	case 0:
		reg = (w83627ehf_read_value(data, W83627EHF_REG_FANDIV1) & 0xcf)
		    | ((data->fan_div[0] & 0x03) << 4);
		/* fan5 input control bit is write only, compute the value */
		reg |= (data->has_fan & (1 << 4)) ? 1 : 0;
		w83627ehf_write_value(data, W83627EHF_REG_FANDIV1, reg);
		reg = (w83627ehf_read_value(data, W83627EHF_REG_VBAT) & 0xdf)
		    | ((data->fan_div[0] & 0x04) << 3);
		w83627ehf_write_value(data, W83627EHF_REG_VBAT, reg);
		break;
	case 1:
		reg = (w83627ehf_read_value(data, W83627EHF_REG_FANDIV1) & 0x3f)
		    | ((data->fan_div[1] & 0x03) << 6);
		/* fan5 input control bit is write only, compute the value */
		reg |= (data->has_fan & (1 << 4)) ? 1 : 0;
		w83627ehf_write_value(data, W83627EHF_REG_FANDIV1, reg);
		reg = (w83627ehf_read_value(data, W83627EHF_REG_VBAT) & 0xbf)
		    | ((data->fan_div[1] & 0x04) << 4);
		w83627ehf_write_value(data, W83627EHF_REG_VBAT, reg);
		break;
	case 2:
		reg = (w83627ehf_read_value(data, W83627EHF_REG_FANDIV2) & 0x3f)
		    | ((data->fan_div[2] & 0x03) << 6);
		w83627ehf_write_value(data, W83627EHF_REG_FANDIV2, reg);
		reg = (w83627ehf_read_value(data, W83627EHF_REG_VBAT) & 0x7f)
		    | ((data->fan_div[2] & 0x04) << 5);
		w83627ehf_write_value(data, W83627EHF_REG_VBAT, reg);
		break;
	case 3:
		reg = (w83627ehf_read_value(data, W83627EHF_REG_DIODE) & 0xfc)
		    | (data->fan_div[3] & 0x03);
		w83627ehf_write_value(data, W83627EHF_REG_DIODE, reg);
		reg = (w83627ehf_read_value(data, W83627EHF_REG_SMI_OVT) & 0x7f)
		    | ((data->fan_div[3] & 0x04) << 5);
		w83627ehf_write_value(data, W83627EHF_REG_SMI_OVT, reg);
		break;
	case 4:
		reg = (w83627ehf_read_value(data, W83627EHF_REG_DIODE) & 0x73)
		    | ((data->fan_div[4] & 0x03) << 2)
		    | ((data->fan_div[4] & 0x04) << 5);
		w83627ehf_write_value(data, W83627EHF_REG_DIODE, reg);
		break;
	}
}

static void w83627ehf_update_fan_div(struct w83627ehf_data *data)
{
	int i;

	i = w83627ehf_read_value(data, W83627EHF_REG_FANDIV1);
	data->fan_div[0] = (i >> 4) & 0x03;
	data->fan_div[1] = (i >> 6) & 0x03;
	i = w83627ehf_read_value(data, W83627EHF_REG_FANDIV2);
	data->fan_div[2] = (i >> 6) & 0x03;
	i = w83627ehf_read_value(data, W83627EHF_REG_VBAT);
	data->fan_div[0] |= (i >> 3) & 0x04;
	data->fan_div[1] |= (i >> 4) & 0x04;
	data->fan_div[2] |= (i >> 5) & 0x04;
	if (data->has_fan & ((1 << 3) | (1 << 4))) {
		i = w83627ehf_read_value(data, W83627EHF_REG_DIODE);
		data->fan_div[3] = i & 0x03;
		data->fan_div[4] = ((i >> 2) & 0x03)
				 | ((i >> 5) & 0x04);
	}
	if (data->has_fan & (1 << 3)) {
		i = w83627ehf_read_value(data, W83627EHF_REG_SMI_OVT);
		data->fan_div[3] |= (i >> 5) & 0x04;
	}
}

static struct w83627ehf_data *w83627ehf_update_device(struct device *dev)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	int pwmcfg = 0, tolerance = 0; /* shut up the compiler */
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ/2)
	 || !data->valid) {
		/* Fan clock dividers */
		w83627ehf_update_fan_div(data);

		/* Measured voltages and limits */
		for (i = 0; i < data->in_num; i++) {
			data->in[i] = w83627ehf_read_value(data,
				      W83627EHF_REG_IN(i));
			data->in_min[i] = w83627ehf_read_value(data,
					  W83627EHF_REG_IN_MIN(i));
			data->in_max[i] = w83627ehf_read_value(data,
					  W83627EHF_REG_IN_MAX(i));
		}

		/* Measured fan speeds and limits */
		for (i = 0; i < 5; i++) {
			if (!(data->has_fan & (1 << i)))
				continue;

			data->fan[i] = w83627ehf_read_value(data,
				       W83627EHF_REG_FAN[i]);
			data->fan_min[i] = w83627ehf_read_value(data,
					   W83627EHF_REG_FAN_MIN[i]);

			/* If we failed to measure the fan speed and clock
			   divider can be increased, let's try that for next
			   time */
			if (data->fan[i] == 0xff
			 && data->fan_div[i] < 0x07) {
			 	dev_dbg(dev, "Increasing fan%d "
					"clock divider from %u to %u\n",
					i + 1, div_from_reg(data->fan_div[i]),
					div_from_reg(data->fan_div[i] + 1));
				data->fan_div[i]++;
				w83627ehf_write_fan_div(data, i);
				/* Preserve min limit if possible */
				if (data->fan_min[i] >= 2
				 && data->fan_min[i] != 255)
					w83627ehf_write_value(data,
						W83627EHF_REG_FAN_MIN[i],
						(data->fan_min[i] /= 2));
			}
		}

		for (i = 0; i < data->pwm_num; i++) {
			if (!(data->has_fan & (1 << i)))
				continue;

			/* pwmcfg, tolerance mapped for i=0, i=1 to same reg */
			if (i != 1) {
				pwmcfg = w83627ehf_read_value(data,
						W83627EHF_REG_PWM_ENABLE[i]);
				tolerance = w83627ehf_read_value(data,
						W83627EHF_REG_TOLERANCE[i]);
			}
			data->pwm_mode[i] =
				((pwmcfg >> W83627EHF_PWM_MODE_SHIFT[i]) & 1)
				? 0 : 1;
			data->pwm_enable[i] =
					((pwmcfg >> W83627EHF_PWM_ENABLE_SHIFT[i])
						& 3) + 1;
			data->pwm[i] = w83627ehf_read_value(data,
						W83627EHF_REG_PWM[i]);
			data->fan_start_output[i] = w83627ehf_read_value(data,
						W83627EHF_REG_FAN_START_OUTPUT[i]);
			data->fan_stop_output[i] = w83627ehf_read_value(data,
						W83627EHF_REG_FAN_STOP_OUTPUT[i]);
			data->fan_stop_time[i] = w83627ehf_read_value(data,
						W83627EHF_REG_FAN_STOP_TIME[i]);

			if (data->REG_FAN_MAX_OUTPUT[i] != 0xff)
				data->fan_max_output[i] =
				  w83627ehf_read_value(data,
					       data->REG_FAN_MAX_OUTPUT[i]);

			if (data->REG_FAN_STEP_OUTPUT[i] != 0xff)
				data->fan_step_output[i] =
				  w83627ehf_read_value(data,
					       data->REG_FAN_STEP_OUTPUT[i]);

			data->target_temp[i] =
				w83627ehf_read_value(data,
					W83627EHF_REG_TARGET[i]) &
					(data->pwm_mode[i] == 1 ? 0x7f : 0xff);
			data->tolerance[i] = (tolerance >> (i == 1 ? 4 : 0))
									& 0x0f;
		}

		/* Measured temperatures and limits */
		data->temp1 = w83627ehf_read_value(data,
			      W83627EHF_REG_TEMP1);
		data->temp1_max = w83627ehf_read_value(data,
				  W83627EHF_REG_TEMP1_OVER);
		data->temp1_max_hyst = w83627ehf_read_value(data,
				       W83627EHF_REG_TEMP1_HYST);
		for (i = 0; i < 2; i++) {
			data->temp[i] = w83627ehf_read_value(data,
					W83627EHF_REG_TEMP[i]);
			data->temp_max[i] = w83627ehf_read_value(data,
					    W83627EHF_REG_TEMP_OVER[i]);
			data->temp_max_hyst[i] = w83627ehf_read_value(data,
						 W83627EHF_REG_TEMP_HYST[i]);
		}

		data->alarms = w83627ehf_read_value(data,
					W83627EHF_REG_ALARM1) |
			       (w83627ehf_read_value(data,
					W83627EHF_REG_ALARM2) << 8) |
			       (w83627ehf_read_value(data,
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
	struct w83627ehf_data *data = dev_get_drvdata(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	u32 val = simple_strtoul(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->in_##reg[nr] = in_to_reg(val, nr); \
	w83627ehf_write_value(data, W83627EHF_REG_IN_##REG(nr), \
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
	struct w83627ehf_data *data = dev_get_drvdata(dev);
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
		/* Preserve the fan speed reading */
		if (data->fan[nr] != 0xff) {
			if (new_div > data->fan_div[nr])
				data->fan[nr] >>= new_div - data->fan_div[nr];
			else if (data->fan[nr] & 0x80)
				data->fan[nr] = 0xff;
			else
				data->fan[nr] <<= data->fan_div[nr] - new_div;
		}

		dev_dbg(dev, "fan%u clock divider changed from %u to %u\n",
			nr + 1, div_from_reg(data->fan_div[nr]),
			div_from_reg(new_div));
		data->fan_div[nr] = new_div;
		w83627ehf_write_fan_div(data, nr);
		/* Give the chip time to sample a new speed value */
		data->last_updated = jiffies;
	}
	w83627ehf_write_value(data, W83627EHF_REG_FAN_MIN[nr],
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
	struct w83627ehf_data *data = dev_get_drvdata(dev); \
	long val = simple_strtol(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->temp1_##reg = temp1_to_reg(val, -128000, 127000); \
	w83627ehf_write_value(data, W83627EHF_REG_TEMP1_##REG, \
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
	struct w83627ehf_data *data = dev_get_drvdata(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	long val = simple_strtol(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->reg[nr] = LM75_TEMP_TO_REG(val); \
	w83627ehf_write_value(data, W83627EHF_REG_TEMP_##REG[nr], \
			      data->reg[nr]); \
	mutex_unlock(&data->update_lock); \
	return count; \
}
store_temp_reg(OVER, temp_max);
store_temp_reg(HYST, temp_max_hyst);

static ssize_t
show_temp_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	return sprintf(buf, "%d\n", (int)data->temp_type[nr]);
}

static struct sensor_device_attribute sda_temp_input[] = {
	SENSOR_ATTR(temp1_input, S_IRUGO, show_temp1, NULL, 0),
	SENSOR_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 0),
	SENSOR_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 1),
};

static struct sensor_device_attribute sda_temp_max[] = {
	SENSOR_ATTR(temp1_max, S_IRUGO | S_IWUSR, show_temp1_max,
		    store_temp1_max, 0),
	SENSOR_ATTR(temp2_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 0),
	SENSOR_ATTR(temp3_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 1),
};

static struct sensor_device_attribute sda_temp_max_hyst[] = {
	SENSOR_ATTR(temp1_max_hyst, S_IRUGO | S_IWUSR, show_temp1_max_hyst,
		    store_temp1_max_hyst, 0),
	SENSOR_ATTR(temp2_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 0),
	SENSOR_ATTR(temp3_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 1),
};

static struct sensor_device_attribute sda_temp_alarm[] = {
	SENSOR_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 4),
	SENSOR_ATTR(temp2_alarm, S_IRUGO, show_alarm, NULL, 5),
	SENSOR_ATTR(temp3_alarm, S_IRUGO, show_alarm, NULL, 13),
};

static struct sensor_device_attribute sda_temp_type[] = {
	SENSOR_ATTR(temp1_type, S_IRUGO, show_temp_type, NULL, 0),
	SENSOR_ATTR(temp2_type, S_IRUGO, show_temp_type, NULL, 1),
	SENSOR_ATTR(temp3_type, S_IRUGO, show_temp_type, NULL, 2),
};

#define show_pwm_reg(reg) \
static ssize_t show_##reg (struct device *dev, struct device_attribute *attr, \
				char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%d\n", data->reg[nr]); \
}

show_pwm_reg(pwm_mode)
show_pwm_reg(pwm_enable)
show_pwm_reg(pwm)

static ssize_t
store_pwm_mode(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	u32 val = simple_strtoul(buf, NULL, 10);
	u16 reg;

	if (val > 1)
		return -EINVAL;
	mutex_lock(&data->update_lock);
	reg = w83627ehf_read_value(data, W83627EHF_REG_PWM_ENABLE[nr]);
	data->pwm_mode[nr] = val;
	reg &= ~(1 << W83627EHF_PWM_MODE_SHIFT[nr]);
	if (!val)
		reg |= 1 << W83627EHF_PWM_MODE_SHIFT[nr];
	w83627ehf_write_value(data, W83627EHF_REG_PWM_ENABLE[nr], reg);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
store_pwm(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	u32 val = SENSORS_LIMIT(simple_strtoul(buf, NULL, 10), 0, 255);

	mutex_lock(&data->update_lock);
	data->pwm[nr] = val;
	w83627ehf_write_value(data, W83627EHF_REG_PWM[nr], val);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
store_pwm_enable(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	u32 val = simple_strtoul(buf, NULL, 10);
	u16 reg;

	if (!val || (val > 4))
		return -EINVAL;
	mutex_lock(&data->update_lock);
	reg = w83627ehf_read_value(data, W83627EHF_REG_PWM_ENABLE[nr]);
	data->pwm_enable[nr] = val;
	reg &= ~(0x03 << W83627EHF_PWM_ENABLE_SHIFT[nr]);
	reg |= (val - 1) << W83627EHF_PWM_ENABLE_SHIFT[nr];
	w83627ehf_write_value(data, W83627EHF_REG_PWM_ENABLE[nr], reg);
	mutex_unlock(&data->update_lock);
	return count;
}


#define show_tol_temp(reg) \
static ssize_t show_##reg(struct device *dev, struct device_attribute *attr, \
				char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%d\n", temp1_from_reg(data->reg[nr])); \
}

show_tol_temp(tolerance)
show_tol_temp(target_temp)

static ssize_t
store_target_temp(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	u8 val = temp1_to_reg(simple_strtoul(buf, NULL, 10), 0, 127000);

	mutex_lock(&data->update_lock);
	data->target_temp[nr] = val;
	w83627ehf_write_value(data, W83627EHF_REG_TARGET[nr], val);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
store_tolerance(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	u16 reg;
	/* Limit the temp to 0C - 15C */
	u8 val = temp1_to_reg(simple_strtoul(buf, NULL, 10), 0, 15000);

	mutex_lock(&data->update_lock);
	reg = w83627ehf_read_value(data, W83627EHF_REG_TOLERANCE[nr]);
	data->tolerance[nr] = val;
	if (nr == 1)
		reg = (reg & 0x0f) | (val << 4);
	else
		reg = (reg & 0xf0) | val;
	w83627ehf_write_value(data, W83627EHF_REG_TOLERANCE[nr], reg);
	mutex_unlock(&data->update_lock);
	return count;
}

static struct sensor_device_attribute sda_pwm[] = {
	SENSOR_ATTR(pwm1, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 0),
	SENSOR_ATTR(pwm2, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 1),
	SENSOR_ATTR(pwm3, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 2),
	SENSOR_ATTR(pwm4, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 3),
};

static struct sensor_device_attribute sda_pwm_mode[] = {
	SENSOR_ATTR(pwm1_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
		    store_pwm_mode, 0),
	SENSOR_ATTR(pwm2_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
		    store_pwm_mode, 1),
	SENSOR_ATTR(pwm3_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
		    store_pwm_mode, 2),
	SENSOR_ATTR(pwm4_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
		    store_pwm_mode, 3),
};

static struct sensor_device_attribute sda_pwm_enable[] = {
	SENSOR_ATTR(pwm1_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
		    store_pwm_enable, 0),
	SENSOR_ATTR(pwm2_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
		    store_pwm_enable, 1),
	SENSOR_ATTR(pwm3_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
		    store_pwm_enable, 2),
	SENSOR_ATTR(pwm4_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
		    store_pwm_enable, 3),
};

static struct sensor_device_attribute sda_target_temp[] = {
	SENSOR_ATTR(pwm1_target, S_IWUSR | S_IRUGO, show_target_temp,
		    store_target_temp, 0),
	SENSOR_ATTR(pwm2_target, S_IWUSR | S_IRUGO, show_target_temp,
		    store_target_temp, 1),
	SENSOR_ATTR(pwm3_target, S_IWUSR | S_IRUGO, show_target_temp,
		    store_target_temp, 2),
	SENSOR_ATTR(pwm4_target, S_IWUSR | S_IRUGO, show_target_temp,
		    store_target_temp, 3),
};

static struct sensor_device_attribute sda_tolerance[] = {
	SENSOR_ATTR(pwm1_tolerance, S_IWUSR | S_IRUGO, show_tolerance,
		    store_tolerance, 0),
	SENSOR_ATTR(pwm2_tolerance, S_IWUSR | S_IRUGO, show_tolerance,
		    store_tolerance, 1),
	SENSOR_ATTR(pwm3_tolerance, S_IWUSR | S_IRUGO, show_tolerance,
		    store_tolerance, 2),
	SENSOR_ATTR(pwm4_tolerance, S_IWUSR | S_IRUGO, show_tolerance,
		    store_tolerance, 3),
};

/* Smart Fan registers */

#define fan_functions(reg, REG) \
static ssize_t show_##reg(struct device *dev, struct device_attribute *attr, \
		       char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%d\n", data->reg[nr]); \
}\
static ssize_t \
store_##reg(struct device *dev, struct device_attribute *attr, \
			    const char *buf, size_t count) \
{\
	struct w83627ehf_data *data = dev_get_drvdata(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	u32 val = SENSORS_LIMIT(simple_strtoul(buf, NULL, 10), 1, 255); \
	mutex_lock(&data->update_lock); \
	data->reg[nr] = val; \
	w83627ehf_write_value(data, data->REG_##REG[nr], val); \
	mutex_unlock(&data->update_lock); \
	return count; \
}

fan_functions(fan_start_output, FAN_START_OUTPUT)
fan_functions(fan_stop_output, FAN_STOP_OUTPUT)
fan_functions(fan_max_output, FAN_MAX_OUTPUT)
fan_functions(fan_step_output, FAN_STEP_OUTPUT)

#define fan_time_functions(reg, REG) \
static ssize_t show_##reg(struct device *dev, struct device_attribute *attr, \
				char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%d\n", \
			step_time_from_reg(data->reg[nr], data->pwm_mode[nr])); \
} \
\
static ssize_t \
store_##reg(struct device *dev, struct device_attribute *attr, \
			const char *buf, size_t count) \
{ \
	struct w83627ehf_data *data = dev_get_drvdata(dev); \
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	u8 val = step_time_to_reg(simple_strtoul(buf, NULL, 10), \
					data->pwm_mode[nr]); \
	mutex_lock(&data->update_lock); \
	data->reg[nr] = val; \
	w83627ehf_write_value(data, W83627EHF_REG_##REG[nr], val); \
	mutex_unlock(&data->update_lock); \
	return count; \
} \

fan_time_functions(fan_stop_time, FAN_STOP_TIME)

static ssize_t show_name(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static struct sensor_device_attribute sda_sf3_arrays_fan4[] = {
	SENSOR_ATTR(pwm4_stop_time, S_IWUSR | S_IRUGO, show_fan_stop_time,
		    store_fan_stop_time, 3),
	SENSOR_ATTR(pwm4_start_output, S_IWUSR | S_IRUGO, show_fan_start_output,
		    store_fan_start_output, 3),
	SENSOR_ATTR(pwm4_stop_output, S_IWUSR | S_IRUGO, show_fan_stop_output,
		    store_fan_stop_output, 3),
	SENSOR_ATTR(pwm4_max_output, S_IWUSR | S_IRUGO, show_fan_max_output,
		    store_fan_max_output, 3),
	SENSOR_ATTR(pwm4_step_output, S_IWUSR | S_IRUGO, show_fan_step_output,
		    store_fan_step_output, 3),
};

static struct sensor_device_attribute sda_sf3_arrays[] = {
	SENSOR_ATTR(pwm1_stop_time, S_IWUSR | S_IRUGO, show_fan_stop_time,
		    store_fan_stop_time, 0),
	SENSOR_ATTR(pwm2_stop_time, S_IWUSR | S_IRUGO, show_fan_stop_time,
		    store_fan_stop_time, 1),
	SENSOR_ATTR(pwm3_stop_time, S_IWUSR | S_IRUGO, show_fan_stop_time,
		    store_fan_stop_time, 2),
	SENSOR_ATTR(pwm1_start_output, S_IWUSR | S_IRUGO, show_fan_start_output,
		    store_fan_start_output, 0),
	SENSOR_ATTR(pwm2_start_output, S_IWUSR | S_IRUGO, show_fan_start_output,
		    store_fan_start_output, 1),
	SENSOR_ATTR(pwm3_start_output, S_IWUSR | S_IRUGO, show_fan_start_output,
		    store_fan_start_output, 2),
	SENSOR_ATTR(pwm1_stop_output, S_IWUSR | S_IRUGO, show_fan_stop_output,
		    store_fan_stop_output, 0),
	SENSOR_ATTR(pwm2_stop_output, S_IWUSR | S_IRUGO, show_fan_stop_output,
		    store_fan_stop_output, 1),
	SENSOR_ATTR(pwm3_stop_output, S_IWUSR | S_IRUGO, show_fan_stop_output,
		    store_fan_stop_output, 2),
};


/*
 * pwm1 and pwm3 don't support max and step settings on all chips.
 * Need to check support while generating/removing attribute files.
 */
static struct sensor_device_attribute sda_sf3_max_step_arrays[] = {
	SENSOR_ATTR(pwm1_max_output, S_IWUSR | S_IRUGO, show_fan_max_output,
		    store_fan_max_output, 0),
	SENSOR_ATTR(pwm1_step_output, S_IWUSR | S_IRUGO, show_fan_step_output,
		    store_fan_step_output, 0),
	SENSOR_ATTR(pwm2_max_output, S_IWUSR | S_IRUGO, show_fan_max_output,
		    store_fan_max_output, 1),
	SENSOR_ATTR(pwm2_step_output, S_IWUSR | S_IRUGO, show_fan_step_output,
		    store_fan_step_output, 1),
	SENSOR_ATTR(pwm3_max_output, S_IWUSR | S_IRUGO, show_fan_max_output,
		    store_fan_max_output, 2),
	SENSOR_ATTR(pwm3_step_output, S_IWUSR | S_IRUGO, show_fan_step_output,
		    store_fan_step_output, 2),
};

static ssize_t
show_vid(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

/*
 * Driver and device management
 */

static void w83627ehf_device_remove_files(struct device *dev)
{
	/* some entries in the following arrays may not have been used in
	 * device_create_file(), but device_remove_file() will ignore them */
	int i;
	struct w83627ehf_data *data = dev_get_drvdata(dev);

	for (i = 0; i < ARRAY_SIZE(sda_sf3_arrays); i++)
		device_remove_file(dev, &sda_sf3_arrays[i].dev_attr);
	for (i = 0; i < ARRAY_SIZE(sda_sf3_max_step_arrays); i++) {
		struct sensor_device_attribute *attr =
		  &sda_sf3_max_step_arrays[i];
		if (data->REG_FAN_STEP_OUTPUT[attr->index] != 0xff)
			device_remove_file(dev, &attr->dev_attr);
	}
	for (i = 0; i < ARRAY_SIZE(sda_sf3_arrays_fan4); i++)
		device_remove_file(dev, &sda_sf3_arrays_fan4[i].dev_attr);
	for (i = 0; i < data->in_num; i++) {
		if ((i == 6) && data->in6_skip)
			continue;
		device_remove_file(dev, &sda_in_input[i].dev_attr);
		device_remove_file(dev, &sda_in_alarm[i].dev_attr);
		device_remove_file(dev, &sda_in_min[i].dev_attr);
		device_remove_file(dev, &sda_in_max[i].dev_attr);
	}
	for (i = 0; i < 5; i++) {
		device_remove_file(dev, &sda_fan_input[i].dev_attr);
		device_remove_file(dev, &sda_fan_alarm[i].dev_attr);
		device_remove_file(dev, &sda_fan_div[i].dev_attr);
		device_remove_file(dev, &sda_fan_min[i].dev_attr);
	}
	for (i = 0; i < data->pwm_num; i++) {
		device_remove_file(dev, &sda_pwm[i].dev_attr);
		device_remove_file(dev, &sda_pwm_mode[i].dev_attr);
		device_remove_file(dev, &sda_pwm_enable[i].dev_attr);
		device_remove_file(dev, &sda_target_temp[i].dev_attr);
		device_remove_file(dev, &sda_tolerance[i].dev_attr);
	}
	for (i = 0; i < 3; i++) {
		if ((i == 2) && data->temp3_disable)
			continue;
		device_remove_file(dev, &sda_temp_input[i].dev_attr);
		device_remove_file(dev, &sda_temp_max[i].dev_attr);
		device_remove_file(dev, &sda_temp_max_hyst[i].dev_attr);
		device_remove_file(dev, &sda_temp_alarm[i].dev_attr);
		device_remove_file(dev, &sda_temp_type[i].dev_attr);
	}

	device_remove_file(dev, &dev_attr_name);
	device_remove_file(dev, &dev_attr_cpu0_vid);
}

/* Get the monitoring functions started */
static inline void __devinit w83627ehf_init_device(struct w83627ehf_data *data)
{
	int i;
	u8 tmp, diode;

	/* Start monitoring is needed */
	tmp = w83627ehf_read_value(data, W83627EHF_REG_CONFIG);
	if (!(tmp & 0x01))
		w83627ehf_write_value(data, W83627EHF_REG_CONFIG,
				      tmp | 0x01);

	/* Enable temp2 and temp3 if needed */
	for (i = 0; i < 2; i++) {
		tmp = w83627ehf_read_value(data,
					   W83627EHF_REG_TEMP_CONFIG[i]);
		if ((i == 1) && data->temp3_disable)
			continue;
		if (tmp & 0x01)
			w83627ehf_write_value(data,
					      W83627EHF_REG_TEMP_CONFIG[i],
					      tmp & 0xfe);
	}

	/* Enable VBAT monitoring if needed */
	tmp = w83627ehf_read_value(data, W83627EHF_REG_VBAT);
	if (!(tmp & 0x01))
		w83627ehf_write_value(data, W83627EHF_REG_VBAT, tmp | 0x01);

	/* Get thermal sensor types */
	diode = w83627ehf_read_value(data, W83627EHF_REG_DIODE);
	for (i = 0; i < 3; i++) {
		if ((tmp & (0x02 << i)))
			data->temp_type[i] = (diode & (0x10 << i)) ? 1 : 2;
		else
			data->temp_type[i] = 4; /* thermistor */
	}
}

static int __devinit w83627ehf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct w83627ehf_sio_data *sio_data = dev->platform_data;
	struct w83627ehf_data *data;
	struct resource *res;
	u8 fan4pin, fan5pin, en_vrm10;
	int i, err = 0;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!request_region(res->start, IOREGION_LENGTH, DRVNAME)) {
		err = -EBUSY;
		dev_err(dev, "Failed to request region 0x%lx-0x%lx\n",
			(unsigned long)res->start,
			(unsigned long)res->start + IOREGION_LENGTH - 1);
		goto exit;
	}

	if (!(data = kzalloc(sizeof(struct w83627ehf_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit_release;
	}

	data->addr = res->start;
	mutex_init(&data->lock);
	mutex_init(&data->update_lock);
	data->name = w83627ehf_device_names[sio_data->kind];
	platform_set_drvdata(pdev, data);

	/* 627EHG and 627EHF have 10 voltage inputs; 627DHG and 667HG have 9 */
	data->in_num = (sio_data->kind == w83627ehf) ? 10 : 9;
	/* 667HG has 3 pwms */
	data->pwm_num = (sio_data->kind == w83667hg
			 || sio_data->kind == w83667hg_b) ? 3 : 4;

	/* Check temp3 configuration bit for 667HG */
	if (sio_data->kind == w83667hg || sio_data->kind == w83667hg_b) {
		data->temp3_disable = w83627ehf_read_value(data,
					W83627EHF_REG_TEMP_CONFIG[1]) & 0x01;
		data->in6_skip = !data->temp3_disable;
	}

	data->REG_FAN_START_OUTPUT = W83627EHF_REG_FAN_START_OUTPUT;
	data->REG_FAN_STOP_OUTPUT = W83627EHF_REG_FAN_STOP_OUTPUT;
	if (sio_data->kind == w83667hg_b) {
		data->REG_FAN_MAX_OUTPUT =
		  W83627EHF_REG_FAN_MAX_OUTPUT_W83667_B;
		data->REG_FAN_STEP_OUTPUT =
		  W83627EHF_REG_FAN_STEP_OUTPUT_W83667_B;
	} else {
		data->REG_FAN_MAX_OUTPUT =
		  W83627EHF_REG_FAN_MAX_OUTPUT_COMMON;
		data->REG_FAN_STEP_OUTPUT =
		  W83627EHF_REG_FAN_STEP_OUTPUT_COMMON;
	}

	/* Initialize the chip */
	w83627ehf_init_device(data);

	data->vrm = vid_which_vrm();
	superio_enter(sio_data->sioreg);
	/* Read VID value */
	if (sio_data->kind == w83667hg || sio_data->kind == w83667hg_b) {
		/* W83667HG has different pins for VID input and output, so
		we can get the VID input values directly at logical device D
		0xe3. */
		superio_select(sio_data->sioreg, W83667HG_LD_VID);
		data->vid = superio_inb(sio_data->sioreg, 0xe3);
		err = device_create_file(dev, &dev_attr_cpu0_vid);
		if (err)
			goto exit_release;
	} else {
		superio_select(sio_data->sioreg, W83627EHF_LD_HWM);
		if (superio_inb(sio_data->sioreg, SIO_REG_VID_CTRL) & 0x80) {
			/* Set VID input sensibility if needed. In theory the
			   BIOS should have set it, but in practice it's not
			   always the case. We only do it for the W83627EHF/EHG
			   because the W83627DHG is more complex in this
			   respect. */
			if (sio_data->kind == w83627ehf) {
				en_vrm10 = superio_inb(sio_data->sioreg,
						       SIO_REG_EN_VRM10);
				if ((en_vrm10 & 0x08) && data->vrm == 90) {
					dev_warn(dev, "Setting VID input "
						 "voltage to TTL\n");
					superio_outb(sio_data->sioreg,
						     SIO_REG_EN_VRM10,
						     en_vrm10 & ~0x08);
				} else if (!(en_vrm10 & 0x08)
					   && data->vrm == 100) {
					dev_warn(dev, "Setting VID input "
						 "voltage to VRM10\n");
					superio_outb(sio_data->sioreg,
						     SIO_REG_EN_VRM10,
						     en_vrm10 | 0x08);
				}
			}

			data->vid = superio_inb(sio_data->sioreg,
						SIO_REG_VID_DATA);
			if (sio_data->kind == w83627ehf) /* 6 VID pins only */
				data->vid &= 0x3f;

			err = device_create_file(dev, &dev_attr_cpu0_vid);
			if (err)
				goto exit_release;
		} else {
			dev_info(dev, "VID pins in output mode, CPU VID not "
				 "available\n");
		}
	}

	/* fan4 and fan5 share some pins with the GPIO and serial flash */
	if (sio_data->kind == w83667hg || sio_data->kind == w83667hg_b) {
		fan5pin = superio_inb(sio_data->sioreg, 0x27) & 0x20;
		fan4pin = superio_inb(sio_data->sioreg, 0x27) & 0x40;
	} else {
		fan5pin = !(superio_inb(sio_data->sioreg, 0x24) & 0x02);
		fan4pin = !(superio_inb(sio_data->sioreg, 0x29) & 0x06);
	}
	superio_exit(sio_data->sioreg);

	/* It looks like fan4 and fan5 pins can be alternatively used
	   as fan on/off switches, but fan5 control is write only :/
	   We assume that if the serial interface is disabled, designers
	   connected fan5 as input unless they are emitting log 1, which
	   is not the default. */

	data->has_fan = 0x07; /* fan1, fan2 and fan3 */
	i = w83627ehf_read_value(data, W83627EHF_REG_FANDIV1);
	if ((i & (1 << 2)) && fan4pin)
		data->has_fan |= (1 << 3);
	if (!(i & (1 << 1)) && fan5pin)
		data->has_fan |= (1 << 4);

	/* Read fan clock dividers immediately */
	w83627ehf_update_fan_div(data);

	/* Register sysfs hooks */
  	for (i = 0; i < ARRAY_SIZE(sda_sf3_arrays); i++)
		if ((err = device_create_file(dev,
			&sda_sf3_arrays[i].dev_attr)))
			goto exit_remove;

	for (i = 0; i < ARRAY_SIZE(sda_sf3_max_step_arrays); i++) {
		struct sensor_device_attribute *attr =
		  &sda_sf3_max_step_arrays[i];
		if (data->REG_FAN_STEP_OUTPUT[attr->index] != 0xff) {
			err = device_create_file(dev, &attr->dev_attr);
			if (err)
				goto exit_remove;
		}
	}
	/* if fan4 is enabled create the sf3 files for it */
	if ((data->has_fan & (1 << 3)) && data->pwm_num >= 4)
		for (i = 0; i < ARRAY_SIZE(sda_sf3_arrays_fan4); i++) {
			if ((err = device_create_file(dev,
				&sda_sf3_arrays_fan4[i].dev_attr)))
				goto exit_remove;
		}

	for (i = 0; i < data->in_num; i++) {
		if ((i == 6) && data->in6_skip)
			continue;
		if ((err = device_create_file(dev, &sda_in_input[i].dev_attr))
			|| (err = device_create_file(dev,
				&sda_in_alarm[i].dev_attr))
			|| (err = device_create_file(dev,
				&sda_in_min[i].dev_attr))
			|| (err = device_create_file(dev,
				&sda_in_max[i].dev_attr)))
			goto exit_remove;
	}

	for (i = 0; i < 5; i++) {
		if (data->has_fan & (1 << i)) {
			if ((err = device_create_file(dev,
					&sda_fan_input[i].dev_attr))
				|| (err = device_create_file(dev,
					&sda_fan_alarm[i].dev_attr))
				|| (err = device_create_file(dev,
					&sda_fan_div[i].dev_attr))
				|| (err = device_create_file(dev,
					&sda_fan_min[i].dev_attr)))
				goto exit_remove;
			if (i < data->pwm_num &&
				((err = device_create_file(dev,
					&sda_pwm[i].dev_attr))
				|| (err = device_create_file(dev,
					&sda_pwm_mode[i].dev_attr))
				|| (err = device_create_file(dev,
					&sda_pwm_enable[i].dev_attr))
				|| (err = device_create_file(dev,
					&sda_target_temp[i].dev_attr))
				|| (err = device_create_file(dev,
					&sda_tolerance[i].dev_attr))))
				goto exit_remove;
		}
	}

	for (i = 0; i < 3; i++) {
		if ((i == 2) && data->temp3_disable)
			continue;
		if ((err = device_create_file(dev,
				&sda_temp_input[i].dev_attr))
			|| (err = device_create_file(dev,
				&sda_temp_max[i].dev_attr))
			|| (err = device_create_file(dev,
				&sda_temp_max_hyst[i].dev_attr))
			|| (err = device_create_file(dev,
				&sda_temp_alarm[i].dev_attr))
			|| (err = device_create_file(dev,
				&sda_temp_type[i].dev_attr)))
			goto exit_remove;
	}

	err = device_create_file(dev, &dev_attr_name);
	if (err)
		goto exit_remove;

	data->hwmon_dev = hwmon_device_register(dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	w83627ehf_device_remove_files(dev);
	kfree(data);
	platform_set_drvdata(pdev, NULL);
exit_release:
	release_region(res->start, IOREGION_LENGTH);
exit:
	return err;
}

static int __devexit w83627ehf_remove(struct platform_device *pdev)
{
	struct w83627ehf_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	w83627ehf_device_remove_files(&pdev->dev);
	release_region(data->addr, IOREGION_LENGTH);
	platform_set_drvdata(pdev, NULL);
	kfree(data);

	return 0;
}

static struct platform_driver w83627ehf_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= w83627ehf_probe,
	.remove		= __devexit_p(w83627ehf_remove),
};

/* w83627ehf_find() looks for a '627 in the Super-I/O config space */
static int __init w83627ehf_find(int sioaddr, unsigned short *addr,
				 struct w83627ehf_sio_data *sio_data)
{
	static const char __initdata sio_name_W83627EHF[] = "W83627EHF";
	static const char __initdata sio_name_W83627EHG[] = "W83627EHG";
	static const char __initdata sio_name_W83627DHG[] = "W83627DHG";
	static const char __initdata sio_name_W83627DHG_P[] = "W83627DHG-P";
	static const char __initdata sio_name_W83667HG[] = "W83667HG";
	static const char __initdata sio_name_W83667HG_B[] = "W83667HG-B";

	u16 val;
	const char *sio_name;

	superio_enter(sioaddr);

	if (force_id)
		val = force_id;
	else
		val = (superio_inb(sioaddr, SIO_REG_DEVID) << 8)
		    | superio_inb(sioaddr, SIO_REG_DEVID + 1);
	switch (val & SIO_ID_MASK) {
	case SIO_W83627EHF_ID:
		sio_data->kind = w83627ehf;
		sio_name = sio_name_W83627EHF;
		break;
	case SIO_W83627EHG_ID:
		sio_data->kind = w83627ehf;
		sio_name = sio_name_W83627EHG;
		break;
	case SIO_W83627DHG_ID:
		sio_data->kind = w83627dhg;
		sio_name = sio_name_W83627DHG;
		break;
	case SIO_W83627DHG_P_ID:
		sio_data->kind = w83627dhg_p;
		sio_name = sio_name_W83627DHG_P;
		break;
	case SIO_W83667HG_ID:
		sio_data->kind = w83667hg;
		sio_name = sio_name_W83667HG;
		break;
	case SIO_W83667HG_B_ID:
		sio_data->kind = w83667hg_b;
		sio_name = sio_name_W83667HG_B;
		break;
	default:
		if (val != 0xffff)
			pr_debug("unsupported chip ID: 0x%04x\n", val);
		superio_exit(sioaddr);
		return -ENODEV;
	}

	/* We have a known chip, find the HWM I/O address */
	superio_select(sioaddr, W83627EHF_LD_HWM);
	val = (superio_inb(sioaddr, SIO_REG_ADDR) << 8)
	    | superio_inb(sioaddr, SIO_REG_ADDR + 1);
	*addr = val & IOREGION_ALIGNMENT;
	if (*addr == 0) {
		pr_err("Refusing to enable a Super-I/O device with a base I/O port 0\n");
		superio_exit(sioaddr);
		return -ENODEV;
	}

	/* Activate logical device if needed */
	val = superio_inb(sioaddr, SIO_REG_ENABLE);
	if (!(val & 0x01)) {
		pr_warn("Forcibly enabling Super-I/O. Sensor is probably unusable.\n");
		superio_outb(sioaddr, SIO_REG_ENABLE, val | 0x01);
	}

	superio_exit(sioaddr);
	pr_info("Found %s chip at %#x\n", sio_name, *addr);
	sio_data->sioreg = sioaddr;

	return 0;
}

/* when Super-I/O functions move to a separate file, the Super-I/O
 * bus will manage the lifetime of the device and this module will only keep
 * track of the w83627ehf driver. But since we platform_device_alloc(), we
 * must keep track of the device */
static struct platform_device *pdev;

static int __init sensors_w83627ehf_init(void)
{
	int err;
	unsigned short address;
	struct resource res;
	struct w83627ehf_sio_data sio_data;

	/* initialize sio_data->kind and sio_data->sioreg.
	 *
	 * when Super-I/O functions move to a separate file, the Super-I/O
	 * driver will probe 0x2e and 0x4e and auto-detect the presence of a
	 * w83627ehf hardware monitor, and call probe() */
	if (w83627ehf_find(0x2e, &address, &sio_data) &&
	    w83627ehf_find(0x4e, &address, &sio_data))
		return -ENODEV;

	err = platform_driver_register(&w83627ehf_driver);
	if (err)
		goto exit;

	if (!(pdev = platform_device_alloc(DRVNAME, address))) {
		err = -ENOMEM;
		pr_err("Device allocation failed\n");
		goto exit_unregister;
	}

	err = platform_device_add_data(pdev, &sio_data,
				       sizeof(struct w83627ehf_sio_data));
	if (err) {
		pr_err("Platform data allocation failed\n");
		goto exit_device_put;
	}

	memset(&res, 0, sizeof(res));
	res.name = DRVNAME;
	res.start = address + IOREGION_OFFSET;
	res.end = address + IOREGION_OFFSET + IOREGION_LENGTH - 1;
	res.flags = IORESOURCE_IO;

	err = acpi_check_resource_conflict(&res);
	if (err)
		goto exit_device_put;

	err = platform_device_add_resources(pdev, &res, 1);
	if (err) {
		pr_err("Device resource addition failed (%d)\n", err);
		goto exit_device_put;
	}

	/* platform_device_add calls probe() */
	err = platform_device_add(pdev);
	if (err) {
		pr_err("Device addition failed (%d)\n", err);
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(pdev);
exit_unregister:
	platform_driver_unregister(&w83627ehf_driver);
exit:
	return err;
}

static void __exit sensors_w83627ehf_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&w83627ehf_driver);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("W83627EHF driver");
MODULE_LICENSE("GPL");

module_init(sensors_w83627ehf_init);
module_exit(sensors_w83627ehf_exit);
