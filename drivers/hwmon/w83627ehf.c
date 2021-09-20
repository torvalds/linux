// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  w83627ehf - Driver for the hardware monitoring functionality of
 *		the Winbond W83627EHF Super-I/O chip
 *  Copyright (C) 2005-2012  Jean Delvare <jdelvare@suse.de>
 *  Copyright (C) 2006  Yuan Mu (Winbond),
 *			Rudolf Marek <r.marek@assembler.cz>
 *			David Hubbard <david.c.hubbard@gmail.com>
 *			Daniel J Blueman <daniel.blueman@gmail.com>
 *  Copyright (C) 2010  Sheng-Yuan Huang (Nuvoton) (PS00)
 *
 *  Shamelessly ripped from the w83627hf driver
 *  Copyright (C) 2003  Mark Studebaker
 *
 *  Thanks to Leon Moonen, Steve Cliffe and Grant Coady for their help
 *  in testing and debugging this driver.
 *
 *  This driver also supports the W83627EHG, which is the lead-free
 *  version of the W83627EHF.
 *
 *  Supports the following chips:
 *
 *  Chip        #vin    #fan    #pwm    #temp  chip IDs       man ID
 *  w83627ehf   10      5       4       3      0x8850 0x88    0x5ca3
 *					       0x8860 0xa1
 *  w83627dhg    9      5       4       3      0xa020 0xc1    0x5ca3
 *  w83627dhg-p  9      5       4       3      0xb070 0xc1    0x5ca3
 *  w83627uhg    8      2       2       3      0xa230 0xc1    0x5ca3
 *  w83667hg     9      5       3       3      0xa510 0xc1    0x5ca3
 *  w83667hg-b   9      5       3       4      0xb350 0xc1    0x5ca3
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

enum kinds {
	w83627ehf, w83627dhg, w83627dhg_p, w83627uhg,
	w83667hg, w83667hg_b,
};

/* used to set data->name = w83627ehf_device_names[data->sio_kind] */
static const char * const w83627ehf_device_names[] = {
	"w83627ehf",
	"w83627dhg",
	"w83627dhg",
	"w83627uhg",
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
#define W83667HG_LD_VID		0x0d

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
#define SIO_W83627UHG_ID	0xa230
#define SIO_W83667HG_ID		0xa510
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

static inline int
superio_enter(int ioreg)
{
	if (!request_muxed_region(ioreg, 2, DRVNAME))
		return -EBUSY;

	outb(0x87, ioreg);
	outb(0x87, ioreg);

	return 0;
}

static inline void
superio_exit(int ioreg)
{
	outb(0xaa, ioreg);
	outb(0x02, ioreg);
	outb(0x02, ioreg + 1);
	release_region(ioreg, 2);
}

/*
 * ISA constants
 */

#define IOREGION_ALIGNMENT	(~7)
#define IOREGION_OFFSET		5
#define IOREGION_LENGTH		2
#define ADDR_REG_OFFSET		0
#define DATA_REG_OFFSET		1

#define W83627EHF_REG_BANK		0x4E
#define W83627EHF_REG_CONFIG		0x40

/*
 * Not currently used:
 * REG_MAN_ID has the value 0x5ca3 for all supported chips.
 * REG_CHIP_ID == 0x88/0xa1/0xc1 depending on chip model.
 * REG_MAN_ID is at port 0x4f
 * REG_CHIP_ID is at port 0x58
 */

static const u16 W83627EHF_REG_FAN[] = { 0x28, 0x29, 0x2a, 0x3f, 0x553 };
static const u16 W83627EHF_REG_FAN_MIN[] = { 0x3b, 0x3c, 0x3d, 0x3e, 0x55c };

/* The W83627EHF registers for nr=7,8,9 are in bank 5 */
#define W83627EHF_REG_IN_MAX(nr)	((nr < 7) ? (0x2b + (nr) * 2) : \
					 (0x554 + (((nr) - 7) * 2)))
#define W83627EHF_REG_IN_MIN(nr)	((nr < 7) ? (0x2c + (nr) * 2) : \
					 (0x555 + (((nr) - 7) * 2)))
#define W83627EHF_REG_IN(nr)		((nr < 7) ? (0x20 + (nr)) : \
					 (0x550 + (nr) - 7))

static const u16 W83627EHF_REG_TEMP[] = { 0x27, 0x150, 0x250, 0x7e };
static const u16 W83627EHF_REG_TEMP_HYST[] = { 0x3a, 0x153, 0x253, 0 };
static const u16 W83627EHF_REG_TEMP_OVER[] = { 0x39, 0x155, 0x255, 0 };
static const u16 W83627EHF_REG_TEMP_CONFIG[] = { 0, 0x152, 0x252, 0 };

/* Fan clock dividers are spread over the following five registers */
#define W83627EHF_REG_FANDIV1		0x47
#define W83627EHF_REG_FANDIV2		0x4B
#define W83627EHF_REG_VBAT		0x5D
#define W83627EHF_REG_DIODE		0x59
#define W83627EHF_REG_SMI_OVT		0x4C

#define W83627EHF_REG_ALARM1		0x459
#define W83627EHF_REG_ALARM2		0x45A
#define W83627EHF_REG_ALARM3		0x45B

#define W83627EHF_REG_CASEOPEN_DET	0x42 /* SMI STATUS #2 */
#define W83627EHF_REG_CASEOPEN_CLR	0x46 /* SMI MASK #3 */

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
static const u16 W83627EHF_REG_PWM[] = { 0x01, 0x03, 0x11, 0x61 };
static const u16 W83627EHF_REG_TARGET[] = { 0x05, 0x06, 0x13, 0x63 };
static const u8 W83627EHF_REG_TOLERANCE[] = { 0x07, 0x07, 0x14, 0x62 };

/* Advanced Fan control, some values are common for all fans */
static const u16 W83627EHF_REG_FAN_START_OUTPUT[] = { 0x0a, 0x0b, 0x16, 0x65 };
static const u16 W83627EHF_REG_FAN_STOP_OUTPUT[] = { 0x08, 0x09, 0x15, 0x64 };
static const u16 W83627EHF_REG_FAN_STOP_TIME[] = { 0x0c, 0x0d, 0x17, 0x66 };

static const u16 W83627EHF_REG_FAN_MAX_OUTPUT_COMMON[]
						= { 0xff, 0x67, 0xff, 0x69 };
static const u16 W83627EHF_REG_FAN_STEP_OUTPUT_COMMON[]
						= { 0xff, 0x68, 0xff, 0x6a };

static const u16 W83627EHF_REG_FAN_MAX_OUTPUT_W83667_B[] = { 0x67, 0x69, 0x6b };
static const u16 W83627EHF_REG_FAN_STEP_OUTPUT_W83667_B[]
						= { 0x68, 0x6a, 0x6c };

static const u16 W83627EHF_REG_TEMP_OFFSET[] = { 0x454, 0x455, 0x456 };

static const char *const w83667hg_b_temp_label[] = {
	"SYSTIN",
	"CPUTIN",
	"AUXTIN",
	"AMDTSI",
	"PECI Agent 1",
	"PECI Agent 2",
	"PECI Agent 3",
	"PECI Agent 4"
};

#define NUM_REG_TEMP	ARRAY_SIZE(W83627EHF_REG_TEMP)

static int is_word_sized(u16 reg)
{
	return ((((reg & 0xff00) == 0x100
	      || (reg & 0xff00) == 0x200)
	     && ((reg & 0x00ff) == 0x50
	      || (reg & 0x00ff) == 0x53
	      || (reg & 0x00ff) == 0x55))
	     || (reg & 0xfff0) == 0x630
	     || reg == 0x640 || reg == 0x642
	     || ((reg & 0xfff0) == 0x650
		 && (reg & 0x000f) >= 0x06)
	     || reg == 0x73 || reg == 0x75 || reg == 0x77
		);
}

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
	return clamp_val((mode ? (msec + 50) / 100 : (msec + 200) / 400),
			 1, 255);
}

static unsigned int fan_from_reg8(u16 reg, unsigned int divreg)
{
	if (reg == 0 || reg == 255)
		return 0;
	return 1350000U / (reg << divreg);
}

static inline unsigned int
div_from_reg(u8 reg)
{
	return 1 << reg;
}

/*
 * Some of the voltage inputs have internal scaling, the tables below
 * contain 8 (the ADC LSB in mV) * scaling factor * 100
 */
static const u16 scale_in_common[10] = {
	800, 800, 1600, 1600, 800, 800, 800, 1600, 1600, 800
};
static const u16 scale_in_w83627uhg[9] = {
	800, 800, 3328, 3424, 800, 800, 0, 3328, 3400
};

static inline long in_from_reg(u8 reg, u8 nr, const u16 *scale_in)
{
	return DIV_ROUND_CLOSEST(reg * scale_in[nr], 100);
}

static inline u8 in_to_reg(u32 val, u8 nr, const u16 *scale_in)
{
	return clamp_val(DIV_ROUND_CLOSEST(val * 100, scale_in[nr]), 0, 255);
}

/*
 * Data structures and manipulation thereof
 */

struct w83627ehf_data {
	int addr;	/* IO base of hw monitor block */
	const char *name;

	struct mutex lock;

	u16 reg_temp[NUM_REG_TEMP];
	u16 reg_temp_over[NUM_REG_TEMP];
	u16 reg_temp_hyst[NUM_REG_TEMP];
	u16 reg_temp_config[NUM_REG_TEMP];
	u8 temp_src[NUM_REG_TEMP];
	const char * const *temp_label;

	const u16 *REG_FAN_MAX_OUTPUT;
	const u16 *REG_FAN_STEP_OUTPUT;
	const u16 *scale_in;

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* Register values */
	u8 bank;		/* current register bank */
	u8 in_num;		/* number of in inputs we have */
	u8 in[10];		/* Register value */
	u8 in_max[10];		/* Register value */
	u8 in_min[10];		/* Register value */
	unsigned int rpm[5];
	u16 fan_min[5];
	u8 fan_div[5];
	u8 has_fan;		/* some fan inputs can be disabled */
	u8 has_fan_min;		/* some fans don't have min register */
	u8 temp_type[3];
	s8 temp_offset[3];
	s16 temp[9];
	s16 temp_max[9];
	s16 temp_max_hyst[9];
	u32 alarms;
	u8 caseopen;

	u8 pwm_mode[4]; /* 0->DC variable voltage, 1->PWM variable duty cycle */
	u8 pwm_enable[4]; /* 1->manual
			   * 2->thermal cruise mode (also called SmartFan I)
			   * 3->fan speed cruise mode
			   * 4->variable thermal cruise (also called
			   * SmartFan III)
			   * 5->enhanced variable thermal cruise (also called
			   * SmartFan IV)
			   */
	u8 pwm_enable_orig[4];	/* original value of pwm_enable */
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

	u16 have_temp;
	u16 have_temp_offset;
	u8 in6_skip:1;
	u8 temp3_val_only:1;
	u8 have_vid:1;

	/* Remember extra register values over suspend/resume */
	u8 vbat;
	u8 fandiv1;
	u8 fandiv2;
};

struct w83627ehf_sio_data {
	int sioreg;
	enum kinds kind;
};

/*
 * On older chips, only registers 0x50-0x5f are banked.
 * On more recent chips, all registers are banked.
 * Assume that is the case and set the bank number for each access.
 * Cache the bank number so it only needs to be set if it changes.
 */
static inline void w83627ehf_set_bank(struct w83627ehf_data *data, u16 reg)
{
	u8 bank = reg >> 8;
	if (data->bank != bank) {
		outb_p(W83627EHF_REG_BANK, data->addr + ADDR_REG_OFFSET);
		outb_p(bank, data->addr + DATA_REG_OFFSET);
		data->bank = bank;
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

	mutex_unlock(&data->lock);
	return res;
}

static int w83627ehf_write_value(struct w83627ehf_data *data, u16 reg,
				 u16 value)
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

	mutex_unlock(&data->lock);
	return 0;
}

/* We left-align 8-bit temperature values to make the code simpler */
static u16 w83627ehf_read_temp(struct w83627ehf_data *data, u16 reg)
{
	u16 res;

	res = w83627ehf_read_value(data, reg);
	if (!is_word_sized(reg))
		res <<= 8;

	return res;
}

static int w83627ehf_write_temp(struct w83627ehf_data *data, u16 reg,
				       u16 value)
{
	if (!is_word_sized(reg))
		value >>= 8;
	return w83627ehf_write_value(data, reg, value);
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

static void w83627ehf_update_pwm(struct w83627ehf_data *data)
{
	int i;
	int pwmcfg = 0, tolerance = 0; /* shut up the compiler */

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
			((pwmcfg >> W83627EHF_PWM_MODE_SHIFT[i]) & 1) ? 0 : 1;
		data->pwm_enable[i] = ((pwmcfg >> W83627EHF_PWM_ENABLE_SHIFT[i])
				       & 3) + 1;
		data->pwm[i] = w83627ehf_read_value(data, W83627EHF_REG_PWM[i]);

		data->tolerance[i] = (tolerance >> (i == 1 ? 4 : 0)) & 0x0f;
	}
}

static struct w83627ehf_data *w83627ehf_update_device(struct device *dev)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ/2)
	 || !data->valid) {
		/* Fan clock dividers */
		w83627ehf_update_fan_div(data);

		/* Measured voltages and limits */
		for (i = 0; i < data->in_num; i++) {
			if ((i == 6) && data->in6_skip)
				continue;

			data->in[i] = w83627ehf_read_value(data,
				      W83627EHF_REG_IN(i));
			data->in_min[i] = w83627ehf_read_value(data,
					  W83627EHF_REG_IN_MIN(i));
			data->in_max[i] = w83627ehf_read_value(data,
					  W83627EHF_REG_IN_MAX(i));
		}

		/* Measured fan speeds and limits */
		for (i = 0; i < 5; i++) {
			u16 reg;

			if (!(data->has_fan & (1 << i)))
				continue;

			reg = w83627ehf_read_value(data, W83627EHF_REG_FAN[i]);
			data->rpm[i] = fan_from_reg8(reg, data->fan_div[i]);

			if (data->has_fan_min & (1 << i))
				data->fan_min[i] = w83627ehf_read_value(data,
					   W83627EHF_REG_FAN_MIN[i]);

			/*
			 * If we failed to measure the fan speed and clock
			 * divider can be increased, let's try that for next
			 * time
			 */
			if (reg >= 0xff && data->fan_div[i] < 0x07) {
				dev_dbg(dev,
					"Increasing fan%d clock divider from %u to %u\n",
					i + 1, div_from_reg(data->fan_div[i]),
					div_from_reg(data->fan_div[i] + 1));
				data->fan_div[i]++;
				w83627ehf_write_fan_div(data, i);
				/* Preserve min limit if possible */
				if ((data->has_fan_min & (1 << i))
				 && data->fan_min[i] >= 2
				 && data->fan_min[i] != 255)
					w83627ehf_write_value(data,
						W83627EHF_REG_FAN_MIN[i],
						(data->fan_min[i] /= 2));
			}
		}

		w83627ehf_update_pwm(data);

		for (i = 0; i < data->pwm_num; i++) {
			if (!(data->has_fan & (1 << i)))
				continue;

			data->fan_start_output[i] =
			  w83627ehf_read_value(data,
					     W83627EHF_REG_FAN_START_OUTPUT[i]);
			data->fan_stop_output[i] =
			  w83627ehf_read_value(data,
					     W83627EHF_REG_FAN_STOP_OUTPUT[i]);
			data->fan_stop_time[i] =
			  w83627ehf_read_value(data,
					       W83627EHF_REG_FAN_STOP_TIME[i]);

			if (data->REG_FAN_MAX_OUTPUT &&
			    data->REG_FAN_MAX_OUTPUT[i] != 0xff)
				data->fan_max_output[i] =
				  w83627ehf_read_value(data,
						data->REG_FAN_MAX_OUTPUT[i]);

			if (data->REG_FAN_STEP_OUTPUT &&
			    data->REG_FAN_STEP_OUTPUT[i] != 0xff)
				data->fan_step_output[i] =
				  w83627ehf_read_value(data,
						data->REG_FAN_STEP_OUTPUT[i]);

			data->target_temp[i] =
				w83627ehf_read_value(data,
					W83627EHF_REG_TARGET[i]) &
					(data->pwm_mode[i] == 1 ? 0x7f : 0xff);
		}

		/* Measured temperatures and limits */
		for (i = 0; i < NUM_REG_TEMP; i++) {
			if (!(data->have_temp & (1 << i)))
				continue;
			data->temp[i] = w83627ehf_read_temp(data,
						data->reg_temp[i]);
			if (data->reg_temp_over[i])
				data->temp_max[i]
				  = w83627ehf_read_temp(data,
						data->reg_temp_over[i]);
			if (data->reg_temp_hyst[i])
				data->temp_max_hyst[i]
				  = w83627ehf_read_temp(data,
						data->reg_temp_hyst[i]);
			if (i > 2)
				continue;
			if (data->have_temp_offset & (1 << i))
				data->temp_offset[i]
				  = w83627ehf_read_value(data,
						W83627EHF_REG_TEMP_OFFSET[i]);
		}

		data->alarms = w83627ehf_read_value(data,
					W83627EHF_REG_ALARM1) |
			       (w83627ehf_read_value(data,
					W83627EHF_REG_ALARM2) << 8) |
			       (w83627ehf_read_value(data,
					W83627EHF_REG_ALARM3) << 16);

		data->caseopen = w83627ehf_read_value(data,
						W83627EHF_REG_CASEOPEN_DET);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);
	return data;
}

#define store_in_reg(REG, reg) \
static int \
store_in_##reg(struct device *dev, struct w83627ehf_data *data, int channel, \
	       long val) \
{ \
	if (val < 0) \
		return -EINVAL; \
	mutex_lock(&data->update_lock); \
	data->in_##reg[channel] = in_to_reg(val, channel, data->scale_in); \
	w83627ehf_write_value(data, W83627EHF_REG_IN_##REG(channel), \
			      data->in_##reg[channel]); \
	mutex_unlock(&data->update_lock); \
	return 0; \
}

store_in_reg(MIN, min)
store_in_reg(MAX, max)

static int
store_fan_min(struct device *dev, struct w83627ehf_data *data, int channel,
	      long val)
{
	unsigned int reg;
	u8 new_div;

	if (val < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	if (!val) {
		/* No min limit, alarm disabled */
		data->fan_min[channel] = 255;
		new_div = data->fan_div[channel]; /* No change */
		dev_info(dev, "fan%u low limit and alarm disabled\n",
			 channel + 1);
	} else if ((reg = 1350000U / val) >= 128 * 255) {
		/*
		 * Speed below this value cannot possibly be represented,
		 * even with the highest divider (128)
		 */
		data->fan_min[channel] = 254;
		new_div = 7; /* 128 == (1 << 7) */
		dev_warn(dev,
			 "fan%u low limit %lu below minimum %u, set to minimum\n",
			 channel + 1, val, fan_from_reg8(254, 7));
	} else if (!reg) {
		/*
		 * Speed above this value cannot possibly be represented,
		 * even with the lowest divider (1)
		 */
		data->fan_min[channel] = 1;
		new_div = 0; /* 1 == (1 << 0) */
		dev_warn(dev,
			 "fan%u low limit %lu above maximum %u, set to maximum\n",
			 channel + 1, val, fan_from_reg8(1, 0));
	} else {
		/*
		 * Automatically pick the best divider, i.e. the one such
		 * that the min limit will correspond to a register value
		 * in the 96..192 range
		 */
		new_div = 0;
		while (reg > 192 && new_div < 7) {
			reg >>= 1;
			new_div++;
		}
		data->fan_min[channel] = reg;
	}

	/*
	 * Write both the fan clock divider (if it changed) and the new
	 * fan min (unconditionally)
	 */
	if (new_div != data->fan_div[channel]) {
		dev_dbg(dev, "fan%u clock divider changed from %u to %u\n",
			channel + 1, div_from_reg(data->fan_div[channel]),
			div_from_reg(new_div));
		data->fan_div[channel] = new_div;
		w83627ehf_write_fan_div(data, channel);
		/* Give the chip time to sample a new speed value */
		data->last_updated = jiffies;
	}

	w83627ehf_write_value(data, W83627EHF_REG_FAN_MIN[channel],
			      data->fan_min[channel]);
	mutex_unlock(&data->update_lock);

	return 0;
}

#define store_temp_reg(addr, reg) \
static int \
store_##reg(struct device *dev, struct w83627ehf_data *data, int channel, \
	    long val) \
{ \
	mutex_lock(&data->update_lock); \
	data->reg[channel] = LM75_TEMP_TO_REG(val); \
	w83627ehf_write_temp(data, data->addr[channel], data->reg[channel]); \
	mutex_unlock(&data->update_lock); \
	return 0; \
}
store_temp_reg(reg_temp_over, temp_max);
store_temp_reg(reg_temp_hyst, temp_max_hyst);

static int
store_temp_offset(struct device *dev, struct w83627ehf_data *data, int channel,
		  long val)
{
	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), -128, 127);

	mutex_lock(&data->update_lock);
	data->temp_offset[channel] = val;
	w83627ehf_write_value(data, W83627EHF_REG_TEMP_OFFSET[channel], val);
	mutex_unlock(&data->update_lock);
	return 0;
}

static int
store_pwm_mode(struct device *dev, struct w83627ehf_data *data, int channel,
	       long val)
{
	u16 reg;

	if (val < 0 || val > 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	reg = w83627ehf_read_value(data, W83627EHF_REG_PWM_ENABLE[channel]);
	data->pwm_mode[channel] = val;
	reg &= ~(1 << W83627EHF_PWM_MODE_SHIFT[channel]);
	if (!val)
		reg |= 1 << W83627EHF_PWM_MODE_SHIFT[channel];
	w83627ehf_write_value(data, W83627EHF_REG_PWM_ENABLE[channel], reg);
	mutex_unlock(&data->update_lock);
	return 0;
}

static int
store_pwm(struct device *dev, struct w83627ehf_data *data, int channel,
	  long val)
{
	val = clamp_val(val, 0, 255);

	mutex_lock(&data->update_lock);
	data->pwm[channel] = val;
	w83627ehf_write_value(data, W83627EHF_REG_PWM[channel], val);
	mutex_unlock(&data->update_lock);
	return 0;
}

static int
store_pwm_enable(struct device *dev, struct w83627ehf_data *data, int channel,
		 long val)
{
	u16 reg;

	if (!val || val < 0 ||
	    (val > 4 && val != data->pwm_enable_orig[channel]))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->pwm_enable[channel] = val;
	reg = w83627ehf_read_value(data,
				   W83627EHF_REG_PWM_ENABLE[channel]);
	reg &= ~(0x03 << W83627EHF_PWM_ENABLE_SHIFT[channel]);
	reg |= (val - 1) << W83627EHF_PWM_ENABLE_SHIFT[channel];
	w83627ehf_write_value(data, W83627EHF_REG_PWM_ENABLE[channel],
			      reg);
	mutex_unlock(&data->update_lock);
	return 0;
}

#define show_tol_temp(reg) \
static ssize_t show_##reg(struct device *dev, struct device_attribute *attr, \
				char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev->parent); \
	struct sensor_device_attribute *sensor_attr = \
		to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%d\n", data->reg[nr] * 1000); \
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
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), 0, 127);

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
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	/* Limit the temp to 0C - 15C */
	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), 0, 15);

	mutex_lock(&data->update_lock);
	reg = w83627ehf_read_value(data, W83627EHF_REG_TOLERANCE[nr]);
	if (nr == 1)
		reg = (reg & 0x0f) | (val << 4);
	else
		reg = (reg & 0xf0) | val;
	w83627ehf_write_value(data, W83627EHF_REG_TOLERANCE[nr], reg);
	data->tolerance[nr] = val;
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(pwm1_target, 0644, show_target_temp,
	    store_target_temp, 0);
static SENSOR_DEVICE_ATTR(pwm2_target, 0644, show_target_temp,
	    store_target_temp, 1);
static SENSOR_DEVICE_ATTR(pwm3_target, 0644, show_target_temp,
	    store_target_temp, 2);
static SENSOR_DEVICE_ATTR(pwm4_target, 0644, show_target_temp,
	    store_target_temp, 3);

static SENSOR_DEVICE_ATTR(pwm1_tolerance, 0644, show_tolerance,
	    store_tolerance, 0);
static SENSOR_DEVICE_ATTR(pwm2_tolerance, 0644, show_tolerance,
	    store_tolerance, 1);
static SENSOR_DEVICE_ATTR(pwm3_tolerance, 0644, show_tolerance,
	    store_tolerance, 2);
static SENSOR_DEVICE_ATTR(pwm4_tolerance, 0644, show_tolerance,
	    store_tolerance, 3);

/* Smart Fan registers */

#define fan_functions(reg, REG) \
static ssize_t show_##reg(struct device *dev, struct device_attribute *attr, \
		       char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev->parent); \
	struct sensor_device_attribute *sensor_attr = \
		to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%d\n", data->reg[nr]); \
} \
static ssize_t \
store_##reg(struct device *dev, struct device_attribute *attr, \
			    const char *buf, size_t count) \
{ \
	struct w83627ehf_data *data = dev_get_drvdata(dev); \
	struct sensor_device_attribute *sensor_attr = \
		to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	unsigned long val; \
	int err; \
	err = kstrtoul(buf, 10, &val); \
	if (err < 0) \
		return err; \
	val = clamp_val(val, 1, 255); \
	mutex_lock(&data->update_lock); \
	data->reg[nr] = val; \
	w83627ehf_write_value(data, REG[nr], val); \
	mutex_unlock(&data->update_lock); \
	return count; \
}

fan_functions(fan_start_output, W83627EHF_REG_FAN_START_OUTPUT)
fan_functions(fan_stop_output, W83627EHF_REG_FAN_STOP_OUTPUT)
fan_functions(fan_max_output, data->REG_FAN_MAX_OUTPUT)
fan_functions(fan_step_output, data->REG_FAN_STEP_OUTPUT)

#define fan_time_functions(reg, REG) \
static ssize_t show_##reg(struct device *dev, struct device_attribute *attr, \
				char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev->parent); \
	struct sensor_device_attribute *sensor_attr = \
		to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%d\n", \
			step_time_from_reg(data->reg[nr], \
					   data->pwm_mode[nr])); \
} \
\
static ssize_t \
store_##reg(struct device *dev, struct device_attribute *attr, \
			const char *buf, size_t count) \
{ \
	struct w83627ehf_data *data = dev_get_drvdata(dev); \
	struct sensor_device_attribute *sensor_attr = \
		to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	unsigned long val; \
	int err; \
	err = kstrtoul(buf, 10, &val); \
	if (err < 0) \
		return err; \
	val = step_time_to_reg(val, data->pwm_mode[nr]); \
	mutex_lock(&data->update_lock); \
	data->reg[nr] = val; \
	w83627ehf_write_value(data, REG[nr], val); \
	mutex_unlock(&data->update_lock); \
	return count; \
} \

fan_time_functions(fan_stop_time, W83627EHF_REG_FAN_STOP_TIME)

static SENSOR_DEVICE_ATTR(pwm4_stop_time, 0644, show_fan_stop_time,
	    store_fan_stop_time, 3);
static SENSOR_DEVICE_ATTR(pwm4_start_output, 0644, show_fan_start_output,
	    store_fan_start_output, 3);
static SENSOR_DEVICE_ATTR(pwm4_stop_output, 0644, show_fan_stop_output,
	    store_fan_stop_output, 3);
static SENSOR_DEVICE_ATTR(pwm4_max_output, 0644, show_fan_max_output,
	    store_fan_max_output, 3);
static SENSOR_DEVICE_ATTR(pwm4_step_output, 0644, show_fan_step_output,
	    store_fan_step_output, 3);

static SENSOR_DEVICE_ATTR(pwm3_stop_time, 0644, show_fan_stop_time,
	    store_fan_stop_time, 2);
static SENSOR_DEVICE_ATTR(pwm3_start_output, 0644, show_fan_start_output,
	    store_fan_start_output, 2);
static SENSOR_DEVICE_ATTR(pwm3_stop_output, 0644, show_fan_stop_output,
		    store_fan_stop_output, 2);

static SENSOR_DEVICE_ATTR(pwm1_stop_time, 0644, show_fan_stop_time,
	    store_fan_stop_time, 0);
static SENSOR_DEVICE_ATTR(pwm2_stop_time, 0644, show_fan_stop_time,
	    store_fan_stop_time, 1);
static SENSOR_DEVICE_ATTR(pwm1_start_output, 0644, show_fan_start_output,
	    store_fan_start_output, 0);
static SENSOR_DEVICE_ATTR(pwm2_start_output, 0644, show_fan_start_output,
	    store_fan_start_output, 1);
static SENSOR_DEVICE_ATTR(pwm1_stop_output, 0644, show_fan_stop_output,
	    store_fan_stop_output, 0);
static SENSOR_DEVICE_ATTR(pwm2_stop_output, 0644, show_fan_stop_output,
	    store_fan_stop_output, 1);


/*
 * pwm1 and pwm3 don't support max and step settings on all chips.
 * Need to check support while generating/removing attribute files.
 */
static SENSOR_DEVICE_ATTR(pwm1_max_output, 0644, show_fan_max_output,
	    store_fan_max_output, 0);
static SENSOR_DEVICE_ATTR(pwm1_step_output, 0644, show_fan_step_output,
	    store_fan_step_output, 0);
static SENSOR_DEVICE_ATTR(pwm2_max_output, 0644, show_fan_max_output,
	    store_fan_max_output, 1);
static SENSOR_DEVICE_ATTR(pwm2_step_output, 0644, show_fan_step_output,
	    store_fan_step_output, 1);
static SENSOR_DEVICE_ATTR(pwm3_max_output, 0644, show_fan_max_output,
	    store_fan_max_output, 2);
static SENSOR_DEVICE_ATTR(pwm3_step_output, 0644, show_fan_step_output,
	    store_fan_step_output, 2);

static ssize_t
cpu0_vid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR_RO(cpu0_vid);


/* Case open detection */
static int
clear_caseopen(struct device *dev, struct w83627ehf_data *data, int channel,
	       long val)
{
	const u16 mask = 0x80;
	u16 reg;

	if (val != 0 || channel != 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	reg = w83627ehf_read_value(data, W83627EHF_REG_CASEOPEN_CLR);
	w83627ehf_write_value(data, W83627EHF_REG_CASEOPEN_CLR, reg | mask);
	w83627ehf_write_value(data, W83627EHF_REG_CASEOPEN_CLR, reg & ~mask);
	data->valid = 0;	/* Force cache refresh */
	mutex_unlock(&data->update_lock);

	return 0;
}

static umode_t w83627ehf_attrs_visible(struct kobject *kobj,
				       struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	struct device_attribute *devattr;
	struct sensor_device_attribute *sda;

	devattr = container_of(a, struct device_attribute, attr);

	/* Not sensor */
	if (devattr->show == cpu0_vid_show && data->have_vid)
		return a->mode;

	sda = (struct sensor_device_attribute *)devattr;

	if (sda->index < 2 &&
		(devattr->show == show_fan_stop_time ||
		 devattr->show == show_fan_start_output ||
		 devattr->show == show_fan_stop_output))
		return a->mode;

	if (sda->index < 3 &&
		(devattr->show == show_fan_max_output ||
		 devattr->show == show_fan_step_output) &&
		data->REG_FAN_STEP_OUTPUT &&
		data->REG_FAN_STEP_OUTPUT[sda->index] != 0xff)
		return a->mode;

	/* if fan3 and fan4 are enabled create the files for them */
	if (sda->index == 2 &&
		(data->has_fan & (1 << 2)) && data->pwm_num >= 3 &&
		(devattr->show == show_fan_stop_time ||
		 devattr->show == show_fan_start_output ||
		 devattr->show == show_fan_stop_output))
		return a->mode;

	if (sda->index == 3 &&
		(data->has_fan & (1 << 3)) && data->pwm_num >= 4 &&
		(devattr->show == show_fan_stop_time ||
		 devattr->show == show_fan_start_output ||
		 devattr->show == show_fan_stop_output ||
		 devattr->show == show_fan_max_output ||
		 devattr->show == show_fan_step_output))
		return a->mode;

	if ((devattr->show == show_target_temp ||
	    devattr->show == show_tolerance) &&
	    (data->has_fan & (1 << sda->index)) &&
	    sda->index < data->pwm_num)
		return a->mode;

	return 0;
}

/* These groups handle non-standard attributes used in this device */
static struct attribute *w83627ehf_attrs[] = {

	&sensor_dev_attr_pwm1_stop_time.dev_attr.attr,
	&sensor_dev_attr_pwm1_start_output.dev_attr.attr,
	&sensor_dev_attr_pwm1_stop_output.dev_attr.attr,
	&sensor_dev_attr_pwm1_max_output.dev_attr.attr,
	&sensor_dev_attr_pwm1_step_output.dev_attr.attr,
	&sensor_dev_attr_pwm1_target.dev_attr.attr,
	&sensor_dev_attr_pwm1_tolerance.dev_attr.attr,

	&sensor_dev_attr_pwm2_stop_time.dev_attr.attr,
	&sensor_dev_attr_pwm2_start_output.dev_attr.attr,
	&sensor_dev_attr_pwm2_stop_output.dev_attr.attr,
	&sensor_dev_attr_pwm2_max_output.dev_attr.attr,
	&sensor_dev_attr_pwm2_step_output.dev_attr.attr,
	&sensor_dev_attr_pwm2_target.dev_attr.attr,
	&sensor_dev_attr_pwm2_tolerance.dev_attr.attr,

	&sensor_dev_attr_pwm3_stop_time.dev_attr.attr,
	&sensor_dev_attr_pwm3_start_output.dev_attr.attr,
	&sensor_dev_attr_pwm3_stop_output.dev_attr.attr,
	&sensor_dev_attr_pwm3_max_output.dev_attr.attr,
	&sensor_dev_attr_pwm3_step_output.dev_attr.attr,
	&sensor_dev_attr_pwm3_target.dev_attr.attr,
	&sensor_dev_attr_pwm3_tolerance.dev_attr.attr,

	&sensor_dev_attr_pwm4_stop_time.dev_attr.attr,
	&sensor_dev_attr_pwm4_start_output.dev_attr.attr,
	&sensor_dev_attr_pwm4_stop_output.dev_attr.attr,
	&sensor_dev_attr_pwm4_max_output.dev_attr.attr,
	&sensor_dev_attr_pwm4_step_output.dev_attr.attr,
	&sensor_dev_attr_pwm4_target.dev_attr.attr,
	&sensor_dev_attr_pwm4_tolerance.dev_attr.attr,

	&dev_attr_cpu0_vid.attr,
	NULL
};

static const struct attribute_group w83627ehf_group = {
	.attrs = w83627ehf_attrs,
	.is_visible = w83627ehf_attrs_visible,
};

static const struct attribute_group *w83627ehf_groups[] = {
	&w83627ehf_group,
	NULL
};

/*
 * Driver and device management
 */

/* Get the monitoring functions started */
static inline void w83627ehf_init_device(struct w83627ehf_data *data,
						   enum kinds kind)
{
	int i;
	u8 tmp, diode;

	/* Start monitoring is needed */
	tmp = w83627ehf_read_value(data, W83627EHF_REG_CONFIG);
	if (!(tmp & 0x01))
		w83627ehf_write_value(data, W83627EHF_REG_CONFIG,
				      tmp | 0x01);

	/* Enable temperature sensors if needed */
	for (i = 0; i < NUM_REG_TEMP; i++) {
		if (!(data->have_temp & (1 << i)))
			continue;
		if (!data->reg_temp_config[i])
			continue;
		tmp = w83627ehf_read_value(data,
					   data->reg_temp_config[i]);
		if (tmp & 0x01)
			w83627ehf_write_value(data,
					      data->reg_temp_config[i],
					      tmp & 0xfe);
	}

	/* Enable VBAT monitoring if needed */
	tmp = w83627ehf_read_value(data, W83627EHF_REG_VBAT);
	if (!(tmp & 0x01))
		w83627ehf_write_value(data, W83627EHF_REG_VBAT, tmp | 0x01);

	/* Get thermal sensor types */
	switch (kind) {
	case w83627ehf:
		diode = w83627ehf_read_value(data, W83627EHF_REG_DIODE);
		break;
	case w83627uhg:
		diode = 0x00;
		break;
	default:
		diode = 0x70;
	}
	for (i = 0; i < 3; i++) {
		const char *label = NULL;

		if (data->temp_label)
			label = data->temp_label[data->temp_src[i]];

		/* Digital source overrides analog type */
		if (label && strncmp(label, "PECI", 4) == 0)
			data->temp_type[i] = 6;
		else if (label && strncmp(label, "AMD", 3) == 0)
			data->temp_type[i] = 5;
		else if ((tmp & (0x02 << i)))
			data->temp_type[i] = (diode & (0x10 << i)) ? 1 : 3;
		else
			data->temp_type[i] = 4; /* thermistor */
	}
}

static void
w83627ehf_set_temp_reg_ehf(struct w83627ehf_data *data, int n_temp)
{
	int i;

	for (i = 0; i < n_temp; i++) {
		data->reg_temp[i] = W83627EHF_REG_TEMP[i];
		data->reg_temp_over[i] = W83627EHF_REG_TEMP_OVER[i];
		data->reg_temp_hyst[i] = W83627EHF_REG_TEMP_HYST[i];
		data->reg_temp_config[i] = W83627EHF_REG_TEMP_CONFIG[i];
	}
}

static void
w83627ehf_check_fan_inputs(const struct w83627ehf_sio_data *sio_data,
			   struct w83627ehf_data *data)
{
	int fan3pin, fan4pin, fan5pin, regval;

	/* The W83627UHG is simple, only two fan inputs, no config */
	if (sio_data->kind == w83627uhg) {
		data->has_fan = 0x03; /* fan1 and fan2 */
		data->has_fan_min = 0x03;
		return;
	}

	/* fan4 and fan5 share some pins with the GPIO and serial flash */
	if (sio_data->kind == w83667hg || sio_data->kind == w83667hg_b) {
		fan3pin = 1;
		fan4pin = superio_inb(sio_data->sioreg, 0x27) & 0x40;
		fan5pin = superio_inb(sio_data->sioreg, 0x27) & 0x20;
	} else {
		fan3pin = 1;
		fan4pin = !(superio_inb(sio_data->sioreg, 0x29) & 0x06);
		fan5pin = !(superio_inb(sio_data->sioreg, 0x24) & 0x02);
	}

	data->has_fan = data->has_fan_min = 0x03; /* fan1 and fan2 */
	data->has_fan |= (fan3pin << 2);
	data->has_fan_min |= (fan3pin << 2);

	/*
	 * It looks like fan4 and fan5 pins can be alternatively used
	 * as fan on/off switches, but fan5 control is write only :/
	 * We assume that if the serial interface is disabled, designers
	 * connected fan5 as input unless they are emitting log 1, which
	 * is not the default.
	 */
	regval = w83627ehf_read_value(data, W83627EHF_REG_FANDIV1);
	if ((regval & (1 << 2)) && fan4pin) {
		data->has_fan |= (1 << 3);
		data->has_fan_min |= (1 << 3);
	}
	if (!(regval & (1 << 1)) && fan5pin) {
		data->has_fan |= (1 << 4);
		data->has_fan_min |= (1 << 4);
	}
}

static umode_t
w83627ehf_is_visible(const void *drvdata, enum hwmon_sensor_types type,
		     u32 attr, int channel)
{
	const struct w83627ehf_data *data = drvdata;

	switch (type) {
	case hwmon_temp:
		/* channel 0.., name 1.. */
		if (!(data->have_temp & (1 << channel)))
			return 0;
		if (attr == hwmon_temp_input)
			return 0444;
		if (attr == hwmon_temp_label) {
			if (data->temp_label)
				return 0444;
			return 0;
		}
		if (channel == 2 && data->temp3_val_only)
			return 0;
		if (attr == hwmon_temp_max) {
			if (data->reg_temp_over[channel])
				return 0644;
			else
				return 0;
		}
		if (attr == hwmon_temp_max_hyst) {
			if (data->reg_temp_hyst[channel])
				return 0644;
			else
				return 0;
		}
		if (channel > 2)
			return 0;
		if (attr == hwmon_temp_alarm || attr == hwmon_temp_type)
			return 0444;
		if (attr == hwmon_temp_offset) {
			if (data->have_temp_offset & (1 << channel))
				return 0644;
			else
				return 0;
		}
		break;

	case hwmon_fan:
		/* channel 0.., name 1.. */
		if (!(data->has_fan & (1 << channel)))
			return 0;
		if (attr == hwmon_fan_input || attr == hwmon_fan_alarm)
			return 0444;
		if (attr == hwmon_fan_div) {
			return 0444;
		}
		if (attr == hwmon_fan_min) {
			if (data->has_fan_min & (1 << channel))
				return 0644;
			else
				return 0;
		}
		break;

	case hwmon_in:
		/* channel 0.., name 0.. */
		if (channel >= data->in_num)
			return 0;
		if (channel == 6 && data->in6_skip)
			return 0;
		if (attr == hwmon_in_alarm || attr == hwmon_in_input)
			return 0444;
		if (attr == hwmon_in_min || attr == hwmon_in_max)
			return 0644;
		break;

	case hwmon_pwm:
		/* channel 0.., name 1.. */
		if (!(data->has_fan & (1 << channel)) ||
		    channel >= data->pwm_num)
			return 0;
		if (attr == hwmon_pwm_mode || attr == hwmon_pwm_enable ||
		    attr == hwmon_pwm_input)
			return 0644;
		break;

	case hwmon_intrusion:
		return 0644;

	default: /* Shouldn't happen */
		return 0;
	}

	return 0; /* Shouldn't happen */
}

static int
w83627ehf_do_read_temp(struct w83627ehf_data *data, u32 attr,
		       int channel, long *val)
{
	switch (attr) {
	case hwmon_temp_input:
		*val = LM75_TEMP_FROM_REG(data->temp[channel]);
		return 0;
	case hwmon_temp_max:
		*val = LM75_TEMP_FROM_REG(data->temp_max[channel]);
		return 0;
	case hwmon_temp_max_hyst:
		*val = LM75_TEMP_FROM_REG(data->temp_max_hyst[channel]);
		return 0;
	case hwmon_temp_offset:
		*val = data->temp_offset[channel] * 1000;
		return 0;
	case hwmon_temp_type:
		*val = (int)data->temp_type[channel];
		return 0;
	case hwmon_temp_alarm:
		if (channel < 3) {
			int bit[] = { 4, 5, 13 };
			*val = (data->alarms >> bit[channel]) & 1;
			return 0;
		}
		break;

	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int
w83627ehf_do_read_in(struct w83627ehf_data *data, u32 attr,
		     int channel, long *val)
{
	switch (attr) {
	case hwmon_in_input:
		*val = in_from_reg(data->in[channel], channel, data->scale_in);
		return 0;
	case hwmon_in_min:
		*val = in_from_reg(data->in_min[channel], channel,
				   data->scale_in);
		return 0;
	case hwmon_in_max:
		*val = in_from_reg(data->in_max[channel], channel,
				   data->scale_in);
		return 0;
	case hwmon_in_alarm:
		if (channel < 10) {
			int bit[] = { 0, 1, 2, 3, 8, 21, 20, 16, 17, 19 };
			*val = (data->alarms >> bit[channel]) & 1;
			return 0;
		}
		break;
	default:
		break;
	}
	return -EOPNOTSUPP;
}

static int
w83627ehf_do_read_fan(struct w83627ehf_data *data, u32 attr,
		      int channel, long *val)
{
	switch (attr) {
	case hwmon_fan_input:
		*val = data->rpm[channel];
		return 0;
	case hwmon_fan_min:
		*val = fan_from_reg8(data->fan_min[channel],
				     data->fan_div[channel]);
		return 0;
	case hwmon_fan_div:
		*val = div_from_reg(data->fan_div[channel]);
		return 0;
	case hwmon_fan_alarm:
		if (channel < 5) {
			int bit[] = { 6, 7, 11, 10, 23 };
			*val = (data->alarms >> bit[channel]) & 1;
			return 0;
		}
		break;
	default:
		break;
	}
	return -EOPNOTSUPP;
}

static int
w83627ehf_do_read_pwm(struct w83627ehf_data *data, u32 attr,
		      int channel, long *val)
{
	switch (attr) {
	case hwmon_pwm_input:
		*val = data->pwm[channel];
		return 0;
	case hwmon_pwm_enable:
		*val = data->pwm_enable[channel];
		return 0;
	case hwmon_pwm_mode:
		*val = data->pwm_enable[channel];
		return 0;
	default:
		break;
	}
	return -EOPNOTSUPP;
}

static int
w83627ehf_do_read_intrusion(struct w83627ehf_data *data, u32 attr,
			    int channel, long *val)
{
	if (attr != hwmon_intrusion_alarm || channel != 0)
		return -EOPNOTSUPP; /* shouldn't happen */

	*val = !!(data->caseopen & 0x10);
	return 0;
}

static int
w83627ehf_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev->parent);

	switch (type) {
	case hwmon_fan:
		return w83627ehf_do_read_fan(data, attr, channel, val);

	case hwmon_in:
		return w83627ehf_do_read_in(data, attr, channel, val);

	case hwmon_pwm:
		return w83627ehf_do_read_pwm(data, attr, channel, val);

	case hwmon_temp:
		return w83627ehf_do_read_temp(data, attr, channel, val);

	case hwmon_intrusion:
		return w83627ehf_do_read_intrusion(data, attr, channel, val);

	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int
w83627ehf_read_string(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, const char **str)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		if (attr == hwmon_temp_label) {
			*str = data->temp_label[data->temp_src[channel]];
			return 0;
		}
		break;

	default:
		break;
	}
	/* Nothing else should be read as a string */
	return -EOPNOTSUPP;
}

static int
w83627ehf_write(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long val)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);

	if (type == hwmon_in && attr == hwmon_in_min)
		return store_in_min(dev, data, channel, val);
	if (type == hwmon_in && attr == hwmon_in_max)
		return store_in_max(dev, data, channel, val);

	if (type == hwmon_fan && attr == hwmon_fan_min)
		return store_fan_min(dev, data, channel, val);

	if (type == hwmon_temp && attr == hwmon_temp_max)
		return store_temp_max(dev, data, channel, val);
	if (type == hwmon_temp && attr == hwmon_temp_max_hyst)
		return store_temp_max_hyst(dev, data, channel, val);
	if (type == hwmon_temp && attr == hwmon_temp_offset)
		return store_temp_offset(dev, data, channel, val);

	if (type == hwmon_pwm && attr == hwmon_pwm_mode)
		return store_pwm_mode(dev, data, channel, val);
	if (type == hwmon_pwm && attr == hwmon_pwm_enable)
		return store_pwm_enable(dev, data, channel, val);
	if (type == hwmon_pwm && attr == hwmon_pwm_input)
		return store_pwm(dev, data, channel, val);

	if (type == hwmon_intrusion && attr == hwmon_intrusion_alarm)
		return clear_caseopen(dev, data, channel, val);

	return -EOPNOTSUPP;
}

static const struct hwmon_ops w83627ehf_ops = {
	.is_visible = w83627ehf_is_visible,
	.read = w83627ehf_read,
	.read_string = w83627ehf_read_string,
	.write = w83627ehf_write,
};

static const struct hwmon_channel_info *w83627ehf_info[] = {
	HWMON_CHANNEL_INFO(fan,
		HWMON_F_ALARM | HWMON_F_DIV | HWMON_F_INPUT | HWMON_F_MIN,
		HWMON_F_ALARM | HWMON_F_DIV | HWMON_F_INPUT | HWMON_F_MIN,
		HWMON_F_ALARM | HWMON_F_DIV | HWMON_F_INPUT | HWMON_F_MIN,
		HWMON_F_ALARM | HWMON_F_DIV | HWMON_F_INPUT | HWMON_F_MIN,
		HWMON_F_ALARM | HWMON_F_DIV | HWMON_F_INPUT | HWMON_F_MIN),
	HWMON_CHANNEL_INFO(in,
		HWMON_I_ALARM | HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MIN,
		HWMON_I_ALARM | HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MIN,
		HWMON_I_ALARM | HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MIN,
		HWMON_I_ALARM | HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MIN,
		HWMON_I_ALARM | HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MIN,
		HWMON_I_ALARM | HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MIN,
		HWMON_I_ALARM | HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MIN,
		HWMON_I_ALARM | HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MIN,
		HWMON_I_ALARM | HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MIN,
		HWMON_I_ALARM | HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MIN),
	HWMON_CHANNEL_INFO(pwm,
		HWMON_PWM_ENABLE | HWMON_PWM_INPUT | HWMON_PWM_MODE,
		HWMON_PWM_ENABLE | HWMON_PWM_INPUT | HWMON_PWM_MODE,
		HWMON_PWM_ENABLE | HWMON_PWM_INPUT | HWMON_PWM_MODE,
		HWMON_PWM_ENABLE | HWMON_PWM_INPUT | HWMON_PWM_MODE),
	HWMON_CHANNEL_INFO(temp,
		HWMON_T_ALARM | HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX |
			HWMON_T_MAX_HYST | HWMON_T_OFFSET | HWMON_T_TYPE,
		HWMON_T_ALARM | HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX |
			HWMON_T_MAX_HYST | HWMON_T_OFFSET | HWMON_T_TYPE,
		HWMON_T_ALARM | HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX |
			HWMON_T_MAX_HYST | HWMON_T_OFFSET | HWMON_T_TYPE,
		HWMON_T_ALARM | HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX |
			HWMON_T_MAX_HYST | HWMON_T_OFFSET | HWMON_T_TYPE,
		HWMON_T_ALARM | HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX |
			HWMON_T_MAX_HYST | HWMON_T_OFFSET | HWMON_T_TYPE,
		HWMON_T_ALARM | HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX |
			HWMON_T_MAX_HYST | HWMON_T_OFFSET | HWMON_T_TYPE,
		HWMON_T_ALARM | HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX |
			HWMON_T_MAX_HYST | HWMON_T_OFFSET | HWMON_T_TYPE,
		HWMON_T_ALARM | HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX |
			HWMON_T_MAX_HYST | HWMON_T_OFFSET | HWMON_T_TYPE,
		HWMON_T_ALARM | HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX |
			HWMON_T_MAX_HYST | HWMON_T_OFFSET | HWMON_T_TYPE),
	HWMON_CHANNEL_INFO(intrusion,
		HWMON_INTRUSION_ALARM),
	NULL
};

static const struct hwmon_chip_info w83627ehf_chip_info = {
	.ops = &w83627ehf_ops,
	.info = w83627ehf_info,
};

static int __init w83627ehf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct w83627ehf_sio_data *sio_data = dev_get_platdata(dev);
	struct w83627ehf_data *data;
	struct resource *res;
	u8 en_vrm10;
	int i, err = 0;
	struct device *hwmon_dev;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!devm_request_region(dev, res->start, IOREGION_LENGTH, DRVNAME))
		return -EBUSY;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->addr = res->start;
	mutex_init(&data->lock);
	mutex_init(&data->update_lock);
	data->name = w83627ehf_device_names[sio_data->kind];
	data->bank = 0xff;		/* Force initial bank selection */
	platform_set_drvdata(pdev, data);

	/* 627EHG and 627EHF have 10 voltage inputs; 627DHG and 667HG have 9 */
	data->in_num = (sio_data->kind == w83627ehf) ? 10 : 9;
	/* 667HG has 3 pwms, and 627UHG has only 2 */
	switch (sio_data->kind) {
	default:
		data->pwm_num = 4;
		break;
	case w83667hg:
	case w83667hg_b:
		data->pwm_num = 3;
		break;
	case w83627uhg:
		data->pwm_num = 2;
		break;
	}

	/* Default to 3 temperature inputs, code below will adjust as needed */
	data->have_temp = 0x07;

	/* Deal with temperature register setup first. */
	if (sio_data->kind == w83667hg_b) {
		u8 reg;

		w83627ehf_set_temp_reg_ehf(data, 4);

		/*
		 * Temperature sources are selected with bank 0, registers 0x49
		 * and 0x4a.
		 */
		reg = w83627ehf_read_value(data, 0x4a);
		data->temp_src[0] = reg >> 5;
		reg = w83627ehf_read_value(data, 0x49);
		data->temp_src[1] = reg & 0x07;
		data->temp_src[2] = (reg >> 4) & 0x07;

		/*
		 * W83667HG-B has another temperature register at 0x7e.
		 * The temperature source is selected with register 0x7d.
		 * Support it if the source differs from already reported
		 * sources.
		 */
		reg = w83627ehf_read_value(data, 0x7d);
		reg &= 0x07;
		if (reg != data->temp_src[0] && reg != data->temp_src[1]
		    && reg != data->temp_src[2]) {
			data->temp_src[3] = reg;
			data->have_temp |= 1 << 3;
		}

		/*
		 * Chip supports either AUXTIN or VIN3. Try to find out which
		 * one.
		 */
		reg = w83627ehf_read_value(data, W83627EHF_REG_TEMP_CONFIG[2]);
		if (data->temp_src[2] == 2 && (reg & 0x01))
			data->have_temp &= ~(1 << 2);

		if ((data->temp_src[2] == 2 && (data->have_temp & (1 << 2)))
		    || (data->temp_src[3] == 2 && (data->have_temp & (1 << 3))))
			data->in6_skip = 1;

		data->temp_label = w83667hg_b_temp_label;
		data->have_temp_offset = data->have_temp & 0x07;
		for (i = 0; i < 3; i++) {
			if (data->temp_src[i] > 2)
				data->have_temp_offset &= ~(1 << i);
		}
	} else if (sio_data->kind == w83627uhg) {
		u8 reg;

		w83627ehf_set_temp_reg_ehf(data, 3);

		/*
		 * Temperature sources for temp2 and temp3 are selected with
		 * bank 0, registers 0x49 and 0x4a.
		 */
		data->temp_src[0] = 0;	/* SYSTIN */
		reg = w83627ehf_read_value(data, 0x49) & 0x07;
		/* Adjust to have the same mapping as other source registers */
		if (reg == 0)
			data->temp_src[1] = 1;
		else if (reg >= 2 && reg <= 5)
			data->temp_src[1] = reg + 2;
		else	/* should never happen */
			data->have_temp &= ~(1 << 1);
		reg = w83627ehf_read_value(data, 0x4a);
		data->temp_src[2] = reg >> 5;

		/*
		 * Skip temp3 if source is invalid or the same as temp1
		 * or temp2.
		 */
		if (data->temp_src[2] == 2 || data->temp_src[2] == 3 ||
		    data->temp_src[2] == data->temp_src[0] ||
		    ((data->have_temp & (1 << 1)) &&
		     data->temp_src[2] == data->temp_src[1]))
			data->have_temp &= ~(1 << 2);
		else
			data->temp3_val_only = 1;	/* No limit regs */

		data->in6_skip = 1;			/* No VIN3 */

		data->temp_label = w83667hg_b_temp_label;
		data->have_temp_offset = data->have_temp & 0x03;
		for (i = 0; i < 3; i++) {
			if (data->temp_src[i] > 1)
				data->have_temp_offset &= ~(1 << i);
		}
	} else {
		w83627ehf_set_temp_reg_ehf(data, 3);

		/* Temperature sources are fixed */

		if (sio_data->kind == w83667hg) {
			u8 reg;

			/*
			 * Chip supports either AUXTIN or VIN3. Try to find
			 * out which one.
			 */
			reg = w83627ehf_read_value(data,
						W83627EHF_REG_TEMP_CONFIG[2]);
			if (reg & 0x01)
				data->have_temp &= ~(1 << 2);
			else
				data->in6_skip = 1;
		}
		data->have_temp_offset = data->have_temp & 0x07;
	}

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

	/* Setup input voltage scaling factors */
	if (sio_data->kind == w83627uhg)
		data->scale_in = scale_in_w83627uhg;
	else
		data->scale_in = scale_in_common;

	/* Initialize the chip */
	w83627ehf_init_device(data, sio_data->kind);

	data->vrm = vid_which_vrm();

	err = superio_enter(sio_data->sioreg);
	if (err)
		return err;

	/* Read VID value */
	if (sio_data->kind == w83667hg || sio_data->kind == w83667hg_b) {
		/*
		 * W83667HG has different pins for VID input and output, so
		 * we can get the VID input values directly at logical device D
		 * 0xe3.
		 */
		superio_select(sio_data->sioreg, W83667HG_LD_VID);
		data->vid = superio_inb(sio_data->sioreg, 0xe3);
		data->have_vid = true;
	} else if (sio_data->kind != w83627uhg) {
		superio_select(sio_data->sioreg, W83627EHF_LD_HWM);
		if (superio_inb(sio_data->sioreg, SIO_REG_VID_CTRL) & 0x80) {
			/*
			 * Set VID input sensibility if needed. In theory the
			 * BIOS should have set it, but in practice it's not
			 * always the case. We only do it for the W83627EHF/EHG
			 * because the W83627DHG is more complex in this
			 * respect.
			 */
			if (sio_data->kind == w83627ehf) {
				en_vrm10 = superio_inb(sio_data->sioreg,
						       SIO_REG_EN_VRM10);
				if ((en_vrm10 & 0x08) && data->vrm == 90) {
					dev_warn(dev,
						 "Setting VID input voltage to TTL\n");
					superio_outb(sio_data->sioreg,
						     SIO_REG_EN_VRM10,
						     en_vrm10 & ~0x08);
				} else if (!(en_vrm10 & 0x08)
					   && data->vrm == 100) {
					dev_warn(dev,
						 "Setting VID input voltage to VRM10\n");
					superio_outb(sio_data->sioreg,
						     SIO_REG_EN_VRM10,
						     en_vrm10 | 0x08);
				}
			}

			data->vid = superio_inb(sio_data->sioreg,
						SIO_REG_VID_DATA);
			if (sio_data->kind == w83627ehf) /* 6 VID pins only */
				data->vid &= 0x3f;
			data->have_vid = true;
		} else {
			dev_info(dev,
				 "VID pins in output mode, CPU VID not available\n");
		}
	}

	w83627ehf_check_fan_inputs(sio_data, data);

	superio_exit(sio_data->sioreg);

	/* Read fan clock dividers immediately */
	w83627ehf_update_fan_div(data);

	/* Read pwm data to save original values */
	w83627ehf_update_pwm(data);
	for (i = 0; i < data->pwm_num; i++)
		data->pwm_enable_orig[i] = data->pwm_enable[i];

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							 data->name,
							 data,
							 &w83627ehf_chip_info,
							 w83627ehf_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static int __maybe_unused w83627ehf_suspend(struct device *dev)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);

	mutex_lock(&data->update_lock);
	data->vbat = w83627ehf_read_value(data, W83627EHF_REG_VBAT);
	mutex_unlock(&data->update_lock);

	return 0;
}

static int __maybe_unused w83627ehf_resume(struct device *dev)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	int i;

	mutex_lock(&data->update_lock);
	data->bank = 0xff;		/* Force initial bank selection */

	/* Restore limits */
	for (i = 0; i < data->in_num; i++) {
		if ((i == 6) && data->in6_skip)
			continue;

		w83627ehf_write_value(data, W83627EHF_REG_IN_MIN(i),
				      data->in_min[i]);
		w83627ehf_write_value(data, W83627EHF_REG_IN_MAX(i),
				      data->in_max[i]);
	}

	for (i = 0; i < 5; i++) {
		if (!(data->has_fan_min & (1 << i)))
			continue;

		w83627ehf_write_value(data, W83627EHF_REG_FAN_MIN[i],
				      data->fan_min[i]);
	}

	for (i = 0; i < NUM_REG_TEMP; i++) {
		if (!(data->have_temp & (1 << i)))
			continue;

		if (data->reg_temp_over[i])
			w83627ehf_write_temp(data, data->reg_temp_over[i],
					     data->temp_max[i]);
		if (data->reg_temp_hyst[i])
			w83627ehf_write_temp(data, data->reg_temp_hyst[i],
					     data->temp_max_hyst[i]);
		if (i > 2)
			continue;
		if (data->have_temp_offset & (1 << i))
			w83627ehf_write_value(data,
					      W83627EHF_REG_TEMP_OFFSET[i],
					      data->temp_offset[i]);
	}

	/* Restore other settings */
	w83627ehf_write_value(data, W83627EHF_REG_VBAT, data->vbat);

	/* Force re-reading all values */
	data->valid = 0;
	mutex_unlock(&data->update_lock);

	return 0;
}

static SIMPLE_DEV_PM_OPS(w83627ehf_dev_pm_ops, w83627ehf_suspend, w83627ehf_resume);

static struct platform_driver w83627ehf_driver = {
	.driver = {
		.name	= DRVNAME,
		.pm	= &w83627ehf_dev_pm_ops,
	},
};

/* w83627ehf_find() looks for a '627 in the Super-I/O config space */
static int __init w83627ehf_find(int sioaddr, unsigned short *addr,
				 struct w83627ehf_sio_data *sio_data)
{
	static const char sio_name_W83627EHF[] __initconst = "W83627EHF";
	static const char sio_name_W83627EHG[] __initconst = "W83627EHG";
	static const char sio_name_W83627DHG[] __initconst = "W83627DHG";
	static const char sio_name_W83627DHG_P[] __initconst = "W83627DHG-P";
	static const char sio_name_W83627UHG[] __initconst = "W83627UHG";
	static const char sio_name_W83667HG[] __initconst = "W83667HG";
	static const char sio_name_W83667HG_B[] __initconst = "W83667HG-B";

	u16 val;
	const char *sio_name;
	int err;

	err = superio_enter(sioaddr);
	if (err)
		return err;

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
	case SIO_W83627UHG_ID:
		sio_data->kind = w83627uhg;
		sio_name = sio_name_W83627UHG;
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

/*
 * when Super-I/O functions move to a separate file, the Super-I/O
 * bus will manage the lifetime of the device and this module will only keep
 * track of the w83627ehf driver.
 */
static struct platform_device *pdev;

static int __init sensors_w83627ehf_init(void)
{
	int err;
	unsigned short address;
	struct resource res = {
		.name	= DRVNAME,
		.flags	= IORESOURCE_IO,
	};
	struct w83627ehf_sio_data sio_data;

	/*
	 * initialize sio_data->kind and sio_data->sioreg.
	 *
	 * when Super-I/O functions move to a separate file, the Super-I/O
	 * driver will probe 0x2e and 0x4e and auto-detect the presence of a
	 * w83627ehf hardware monitor, and call probe()
	 */
	if (w83627ehf_find(0x2e, &address, &sio_data) &&
	    w83627ehf_find(0x4e, &address, &sio_data))
		return -ENODEV;

	res.start = address + IOREGION_OFFSET;
	res.end = address + IOREGION_OFFSET + IOREGION_LENGTH - 1;

	err = acpi_check_resource_conflict(&res);
	if (err)
		return err;

	pdev = platform_create_bundle(&w83627ehf_driver, w83627ehf_probe, &res, 1, &sio_data,
				      sizeof(struct w83627ehf_sio_data));

	return PTR_ERR_OR_ZERO(pdev);
}

static void __exit sensors_w83627ehf_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&w83627ehf_driver);
}

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("W83627EHF driver");
MODULE_LICENSE("GPL");

module_init(sensors_w83627ehf_init);
module_exit(sensors_w83627ehf_exit);
