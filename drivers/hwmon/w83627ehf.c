/*
 *  w83627ehf - Driver for the hardware monitoring functionality of
 *		the Winbond W83627EHF Super-I/O chip
 *  Copyright (C) 2005-2012  Jean Delvare <khali@linux-fr.org>
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
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
 *  nct6775f     9      4       3       9      0xb470 0xc1    0x5ca3
 *  nct6776f     9      5       3       9      0xC330 0xc1    0x5ca3
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
	w83667hg, w83667hg_b, nct6775, nct6776,
};

/* used to set data->name = w83627ehf_device_names[data->sio_kind] */
static const char * const w83627ehf_device_names[] = {
	"w83627ehf",
	"w83627dhg",
	"w83627dhg",
	"w83627uhg",
	"w83667hg",
	"w83667hg",
	"nct6775",
	"nct6776",
};

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

static unsigned short fan_debounce;
module_param(fan_debounce, ushort, 0);
MODULE_PARM_DESC(fan_debounce, "Enable debouncing for fan RPM signal");

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
#define SIO_NCT6775_ID		0xb470
#define SIO_NCT6776_ID		0xc330
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

/* NCT6775F has its own fan divider registers */
#define NCT6775_REG_FANDIV1		0x506
#define NCT6775_REG_FANDIV2		0x507
#define NCT6775_REG_FAN_DEBOUNCE	0xf0

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

static const u16 NCT6775_REG_TARGET[] = { 0x101, 0x201, 0x301 };
static const u16 NCT6775_REG_FAN_MODE[] = { 0x102, 0x202, 0x302 };
static const u16 NCT6775_REG_FAN_STOP_OUTPUT[] = { 0x105, 0x205, 0x305 };
static const u16 NCT6775_REG_FAN_START_OUTPUT[] = { 0x106, 0x206, 0x306 };
static const u16 NCT6775_REG_FAN_STOP_TIME[] = { 0x107, 0x207, 0x307 };
static const u16 NCT6775_REG_PWM[] = { 0x109, 0x209, 0x309 };
static const u16 NCT6775_REG_FAN_MAX_OUTPUT[] = { 0x10a, 0x20a, 0x30a };
static const u16 NCT6775_REG_FAN_STEP_OUTPUT[] = { 0x10b, 0x20b, 0x30b };
static const u16 NCT6775_REG_FAN[] = { 0x630, 0x632, 0x634, 0x636, 0x638 };
static const u16 NCT6776_REG_FAN_MIN[] = { 0x63a, 0x63c, 0x63e, 0x640, 0x642};

static const u16 NCT6775_REG_TEMP[]
	= { 0x27, 0x150, 0x250, 0x73, 0x75, 0x77, 0x62b, 0x62c, 0x62d };
static const u16 NCT6775_REG_TEMP_CONFIG[]
	= { 0, 0x152, 0x252, 0, 0, 0, 0x628, 0x629, 0x62A };
static const u16 NCT6775_REG_TEMP_HYST[]
	= { 0x3a, 0x153, 0x253, 0, 0, 0, 0x673, 0x678, 0x67D };
static const u16 NCT6775_REG_TEMP_OVER[]
	= { 0x39, 0x155, 0x255, 0, 0, 0, 0x672, 0x677, 0x67C };
static const u16 NCT6775_REG_TEMP_SOURCE[]
	= { 0x621, 0x622, 0x623, 0x100, 0x200, 0x300, 0x624, 0x625, 0x626 };

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

static const char *const nct6775_temp_label[] = {
	"",
	"SYSTIN",
	"CPUTIN",
	"AUXTIN",
	"AMD SB-TSI",
	"PECI Agent 0",
	"PECI Agent 1",
	"PECI Agent 2",
	"PECI Agent 3",
	"PECI Agent 4",
	"PECI Agent 5",
	"PECI Agent 6",
	"PECI Agent 7",
	"PCH_CHIP_CPU_MAX_TEMP",
	"PCH_CHIP_TEMP",
	"PCH_CPU_TEMP",
	"PCH_MCH_TEMP",
	"PCH_DIM0_TEMP",
	"PCH_DIM1_TEMP",
	"PCH_DIM2_TEMP",
	"PCH_DIM3_TEMP"
};

static const char *const nct6776_temp_label[] = {
	"",
	"SYSTIN",
	"CPUTIN",
	"AUXTIN",
	"SMBUSMASTER 0",
	"SMBUSMASTER 1",
	"SMBUSMASTER 2",
	"SMBUSMASTER 3",
	"SMBUSMASTER 4",
	"SMBUSMASTER 5",
	"SMBUSMASTER 6",
	"SMBUSMASTER 7",
	"PECI Agent 0",
	"PECI Agent 1",
	"PCH_CHIP_CPU_MAX_TEMP",
	"PCH_CHIP_TEMP",
	"PCH_CPU_TEMP",
	"PCH_MCH_TEMP",
	"PCH_DIM0_TEMP",
	"PCH_DIM1_TEMP",
	"PCH_DIM2_TEMP",
	"PCH_DIM3_TEMP",
	"BYTE_TEMP"
};

#define NUM_REG_TEMP	ARRAY_SIZE(NCT6775_REG_TEMP)

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

static unsigned int fan_from_reg13(u16 reg, unsigned int divreg)
{
	if ((reg & 0xff1f) == 0xff1f)
		return 0;

	reg = (reg & 0x1f) | ((reg & 0xff00) >> 3);

	if (reg == 0)
		return 0;

	return 1350000U / reg;
}

static unsigned int fan_from_reg16(u16 reg, unsigned int divreg)
{
	if (reg == 0 || reg == 0xffff)
		return 0;

	/*
	 * Even though the registers are 16 bit wide, the fan divisor
	 * still applies.
	 */
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

	struct device *hwmon_dev;
	struct mutex lock;

	u16 reg_temp[NUM_REG_TEMP];
	u16 reg_temp_over[NUM_REG_TEMP];
	u16 reg_temp_hyst[NUM_REG_TEMP];
	u16 reg_temp_config[NUM_REG_TEMP];
	u8 temp_src[NUM_REG_TEMP];
	const char * const *temp_label;

	const u16 *REG_PWM;
	const u16 *REG_TARGET;
	const u16 *REG_FAN;
	const u16 *REG_FAN_MIN;
	const u16 *REG_FAN_START_OUTPUT;
	const u16 *REG_FAN_STOP_OUTPUT;
	const u16 *REG_FAN_STOP_TIME;
	const u16 *REG_FAN_MAX_OUTPUT;
	const u16 *REG_FAN_STEP_OUTPUT;
	const u16 *scale_in;

	unsigned int (*fan_from_reg)(u16 reg, unsigned int divreg);
	unsigned int (*fan_from_reg_min)(u16 reg, unsigned int divreg);

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
	bool has_fan_div;
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

#ifdef CONFIG_PM
	/* Remember extra register values over suspend/resume */
	u8 vbat;
	u8 fandiv1;
	u8 fandiv2;
#endif
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
static void nct6775_write_fan_div(struct w83627ehf_data *data, int nr)
{
	u8 reg;

	switch (nr) {
	case 0:
		reg = (w83627ehf_read_value(data, NCT6775_REG_FANDIV1) & 0x70)
		    | (data->fan_div[0] & 0x7);
		w83627ehf_write_value(data, NCT6775_REG_FANDIV1, reg);
		break;
	case 1:
		reg = (w83627ehf_read_value(data, NCT6775_REG_FANDIV1) & 0x7)
		    | ((data->fan_div[1] << 4) & 0x70);
		w83627ehf_write_value(data, NCT6775_REG_FANDIV1, reg);
		break;
	case 2:
		reg = (w83627ehf_read_value(data, NCT6775_REG_FANDIV2) & 0x70)
		    | (data->fan_div[2] & 0x7);
		w83627ehf_write_value(data, NCT6775_REG_FANDIV2, reg);
		break;
	case 3:
		reg = (w83627ehf_read_value(data, NCT6775_REG_FANDIV2) & 0x7)
		    | ((data->fan_div[3] << 4) & 0x70);
		w83627ehf_write_value(data, NCT6775_REG_FANDIV2, reg);
		break;
	}
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

static void w83627ehf_write_fan_div_common(struct device *dev,
					   struct w83627ehf_data *data, int nr)
{
	struct w83627ehf_sio_data *sio_data = dev->platform_data;

	if (sio_data->kind == nct6776)
		; /* no dividers, do nothing */
	else if (sio_data->kind == nct6775)
		nct6775_write_fan_div(data, nr);
	else
		w83627ehf_write_fan_div(data, nr);
}

static void nct6775_update_fan_div(struct w83627ehf_data *data)
{
	u8 i;

	i = w83627ehf_read_value(data, NCT6775_REG_FANDIV1);
	data->fan_div[0] = i & 0x7;
	data->fan_div[1] = (i & 0x70) >> 4;
	i = w83627ehf_read_value(data, NCT6775_REG_FANDIV2);
	data->fan_div[2] = i & 0x7;
	if (data->has_fan & (1<<3))
		data->fan_div[3] = (i & 0x70) >> 4;
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

static void w83627ehf_update_fan_div_common(struct device *dev,
					    struct w83627ehf_data *data)
{
	struct w83627ehf_sio_data *sio_data = dev->platform_data;

	if (sio_data->kind == nct6776)
		; /* no dividers, do nothing */
	else if (sio_data->kind == nct6775)
		nct6775_update_fan_div(data);
	else
		w83627ehf_update_fan_div(data);
}

static void nct6775_update_pwm(struct w83627ehf_data *data)
{
	int i;
	int pwmcfg, fanmodecfg;

	for (i = 0; i < data->pwm_num; i++) {
		pwmcfg = w83627ehf_read_value(data,
					      W83627EHF_REG_PWM_ENABLE[i]);
		fanmodecfg = w83627ehf_read_value(data,
						  NCT6775_REG_FAN_MODE[i]);
		data->pwm_mode[i] =
		  ((pwmcfg >> W83627EHF_PWM_MODE_SHIFT[i]) & 1) ? 0 : 1;
		data->pwm_enable[i] = ((fanmodecfg >> 4) & 7) + 1;
		data->tolerance[i] = fanmodecfg & 0x0f;
		data->pwm[i] = w83627ehf_read_value(data, data->REG_PWM[i]);
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
		data->pwm[i] = w83627ehf_read_value(data, data->REG_PWM[i]);

		data->tolerance[i] = (tolerance >> (i == 1 ? 4 : 0)) & 0x0f;
	}
}

static void w83627ehf_update_pwm_common(struct device *dev,
					struct w83627ehf_data *data)
{
	struct w83627ehf_sio_data *sio_data = dev->platform_data;

	if (sio_data->kind == nct6775 || sio_data->kind == nct6776)
		nct6775_update_pwm(data);
	else
		w83627ehf_update_pwm(data);
}

static struct w83627ehf_data *w83627ehf_update_device(struct device *dev)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	struct w83627ehf_sio_data *sio_data = dev->platform_data;

	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ/2)
	 || !data->valid) {
		/* Fan clock dividers */
		w83627ehf_update_fan_div_common(dev, data);

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

			reg = w83627ehf_read_value(data, data->REG_FAN[i]);
			data->rpm[i] = data->fan_from_reg(reg,
							  data->fan_div[i]);

			if (data->has_fan_min & (1 << i))
				data->fan_min[i] = w83627ehf_read_value(data,
					   data->REG_FAN_MIN[i]);

			/*
			 * If we failed to measure the fan speed and clock
			 * divider can be increased, let's try that for next
			 * time
			 */
			if (data->has_fan_div
			    && (reg >= 0xff || (sio_data->kind == nct6775
						&& reg == 0x00))
			    && data->fan_div[i] < 0x07) {
				dev_dbg(dev,
					"Increasing fan%d clock divider from %u to %u\n",
					i + 1, div_from_reg(data->fan_div[i]),
					div_from_reg(data->fan_div[i] + 1));
				data->fan_div[i]++;
				w83627ehf_write_fan_div_common(dev, data, i);
				/* Preserve min limit if possible */
				if ((data->has_fan_min & (1 << i))
				 && data->fan_min[i] >= 2
				 && data->fan_min[i] != 255)
					w83627ehf_write_value(data,
						data->REG_FAN_MIN[i],
						(data->fan_min[i] /= 2));
			}
		}

		w83627ehf_update_pwm_common(dev, data);

		for (i = 0; i < data->pwm_num; i++) {
			if (!(data->has_fan & (1 << i)))
				continue;

			data->fan_start_output[i] =
			  w83627ehf_read_value(data,
					       data->REG_FAN_START_OUTPUT[i]);
			data->fan_stop_output[i] =
			  w83627ehf_read_value(data,
					       data->REG_FAN_STOP_OUTPUT[i]);
			data->fan_stop_time[i] =
			  w83627ehf_read_value(data,
					       data->REG_FAN_STOP_TIME[i]);

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
					data->REG_TARGET[i]) &
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

/*
 * Sysfs callback functions
 */
#define show_in_reg(reg) \
static ssize_t \
show_##reg(struct device *dev, struct device_attribute *attr, \
	   char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	struct sensor_device_attribute *sensor_attr = \
		to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%ld\n", in_from_reg(data->reg[nr], nr, \
		       data->scale_in)); \
}
show_in_reg(in)
show_in_reg(in_min)
show_in_reg(in_max)

#define store_in_reg(REG, reg) \
static ssize_t \
store_in_##reg(struct device *dev, struct device_attribute *attr, \
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
	mutex_lock(&data->update_lock); \
	data->in_##reg[nr] = in_to_reg(val, nr, data->scale_in); \
	w83627ehf_write_value(data, W83627EHF_REG_IN_##REG(nr), \
			      data->in_##reg[nr]); \
	mutex_unlock(&data->update_lock); \
	return count; \
}

store_in_reg(MIN, min)
store_in_reg(MAX, max)

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
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

static ssize_t
show_fan(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	return sprintf(buf, "%d\n", data->rpm[nr]);
}

static ssize_t
show_fan_min(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	return sprintf(buf, "%d\n",
		       data->fan_from_reg_min(data->fan_min[nr],
					      data->fan_div[nr]));
}

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
	unsigned long val;
	int err;
	unsigned int reg;
	u8 new_div;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	if (!data->has_fan_div) {
		/*
		 * Only NCT6776F for now, so we know that this is a 13 bit
		 * register
		 */
		if (!val) {
			val = 0xff1f;
		} else {
			if (val > 1350000U)
				val = 135000U;
			val = 1350000U / val;
			val = (val & 0x1f) | ((val << 3) & 0xff00);
		}
		data->fan_min[nr] = val;
		goto done;	/* Leave fan divider alone */
	}
	if (!val) {
		/* No min limit, alarm disabled */
		data->fan_min[nr] = 255;
		new_div = data->fan_div[nr]; /* No change */
		dev_info(dev, "fan%u low limit and alarm disabled\n", nr + 1);
	} else if ((reg = 1350000U / val) >= 128 * 255) {
		/*
		 * Speed below this value cannot possibly be represented,
		 * even with the highest divider (128)
		 */
		data->fan_min[nr] = 254;
		new_div = 7; /* 128 == (1 << 7) */
		dev_warn(dev,
			 "fan%u low limit %lu below minimum %u, set to minimum\n",
			 nr + 1, val, data->fan_from_reg_min(254, 7));
	} else if (!reg) {
		/*
		 * Speed above this value cannot possibly be represented,
		 * even with the lowest divider (1)
		 */
		data->fan_min[nr] = 1;
		new_div = 0; /* 1 == (1 << 0) */
		dev_warn(dev,
			 "fan%u low limit %lu above maximum %u, set to maximum\n",
			 nr + 1, val, data->fan_from_reg_min(1, 0));
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
		data->fan_min[nr] = reg;
	}

	/*
	 * Write both the fan clock divider (if it changed) and the new
	 * fan min (unconditionally)
	 */
	if (new_div != data->fan_div[nr]) {
		dev_dbg(dev, "fan%u clock divider changed from %u to %u\n",
			nr + 1, div_from_reg(data->fan_div[nr]),
			div_from_reg(new_div));
		data->fan_div[nr] = new_div;
		w83627ehf_write_fan_div_common(dev, data, nr);
		/* Give the chip time to sample a new speed value */
		data->last_updated = jiffies;
	}
done:
	w83627ehf_write_value(data, data->REG_FAN_MIN[nr],
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

static ssize_t
show_temp_label(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	return sprintf(buf, "%s\n", data->temp_label[data->temp_src[nr]]);
}

#define show_temp_reg(addr, reg) \
static ssize_t \
show_##reg(struct device *dev, struct device_attribute *attr, \
	   char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	struct sensor_device_attribute *sensor_attr = \
		to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	return sprintf(buf, "%d\n", LM75_TEMP_FROM_REG(data->reg[nr])); \
}
show_temp_reg(reg_temp, temp);
show_temp_reg(reg_temp_over, temp_max);
show_temp_reg(reg_temp_hyst, temp_max_hyst);

#define store_temp_reg(addr, reg) \
static ssize_t \
store_##reg(struct device *dev, struct device_attribute *attr, \
	    const char *buf, size_t count) \
{ \
	struct w83627ehf_data *data = dev_get_drvdata(dev); \
	struct sensor_device_attribute *sensor_attr = \
		to_sensor_dev_attr(attr); \
	int nr = sensor_attr->index; \
	int err; \
	long val; \
	err = kstrtol(buf, 10, &val); \
	if (err < 0) \
		return err; \
	mutex_lock(&data->update_lock); \
	data->reg[nr] = LM75_TEMP_TO_REG(val); \
	w83627ehf_write_temp(data, data->addr[nr], data->reg[nr]); \
	mutex_unlock(&data->update_lock); \
	return count; \
}
store_temp_reg(reg_temp_over, temp_max);
store_temp_reg(reg_temp_hyst, temp_max_hyst);

static ssize_t
show_temp_offset(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);

	return sprintf(buf, "%d\n",
		       data->temp_offset[sensor_attr->index] * 1000);
}

static ssize_t
store_temp_offset(struct device *dev, struct device_attribute *attr,
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

	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), -128, 127);

	mutex_lock(&data->update_lock);
	data->temp_offset[nr] = val;
	w83627ehf_write_value(data, W83627EHF_REG_TEMP_OFFSET[nr], val);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_temp_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	return sprintf(buf, "%d\n", (int)data->temp_type[nr]);
}

static struct sensor_device_attribute sda_temp_input[] = {
	SENSOR_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0),
	SENSOR_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1),
	SENSOR_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2),
	SENSOR_ATTR(temp4_input, S_IRUGO, show_temp, NULL, 3),
	SENSOR_ATTR(temp5_input, S_IRUGO, show_temp, NULL, 4),
	SENSOR_ATTR(temp6_input, S_IRUGO, show_temp, NULL, 5),
	SENSOR_ATTR(temp7_input, S_IRUGO, show_temp, NULL, 6),
	SENSOR_ATTR(temp8_input, S_IRUGO, show_temp, NULL, 7),
	SENSOR_ATTR(temp9_input, S_IRUGO, show_temp, NULL, 8),
};

static struct sensor_device_attribute sda_temp_label[] = {
	SENSOR_ATTR(temp1_label, S_IRUGO, show_temp_label, NULL, 0),
	SENSOR_ATTR(temp2_label, S_IRUGO, show_temp_label, NULL, 1),
	SENSOR_ATTR(temp3_label, S_IRUGO, show_temp_label, NULL, 2),
	SENSOR_ATTR(temp4_label, S_IRUGO, show_temp_label, NULL, 3),
	SENSOR_ATTR(temp5_label, S_IRUGO, show_temp_label, NULL, 4),
	SENSOR_ATTR(temp6_label, S_IRUGO, show_temp_label, NULL, 5),
	SENSOR_ATTR(temp7_label, S_IRUGO, show_temp_label, NULL, 6),
	SENSOR_ATTR(temp8_label, S_IRUGO, show_temp_label, NULL, 7),
	SENSOR_ATTR(temp9_label, S_IRUGO, show_temp_label, NULL, 8),
};

static struct sensor_device_attribute sda_temp_max[] = {
	SENSOR_ATTR(temp1_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 0),
	SENSOR_ATTR(temp2_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 1),
	SENSOR_ATTR(temp3_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 2),
	SENSOR_ATTR(temp4_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 3),
	SENSOR_ATTR(temp5_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 4),
	SENSOR_ATTR(temp6_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 5),
	SENSOR_ATTR(temp7_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 6),
	SENSOR_ATTR(temp8_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 7),
	SENSOR_ATTR(temp9_max, S_IRUGO | S_IWUSR, show_temp_max,
		    store_temp_max, 8),
};

static struct sensor_device_attribute sda_temp_max_hyst[] = {
	SENSOR_ATTR(temp1_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 0),
	SENSOR_ATTR(temp2_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 1),
	SENSOR_ATTR(temp3_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 2),
	SENSOR_ATTR(temp4_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 3),
	SENSOR_ATTR(temp5_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 4),
	SENSOR_ATTR(temp6_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 5),
	SENSOR_ATTR(temp7_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 6),
	SENSOR_ATTR(temp8_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 7),
	SENSOR_ATTR(temp9_max_hyst, S_IRUGO | S_IWUSR, show_temp_max_hyst,
		    store_temp_max_hyst, 8),
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

static struct sensor_device_attribute sda_temp_offset[] = {
	SENSOR_ATTR(temp1_offset, S_IRUGO | S_IWUSR, show_temp_offset,
		    store_temp_offset, 0),
	SENSOR_ATTR(temp2_offset, S_IRUGO | S_IWUSR, show_temp_offset,
		    store_temp_offset, 1),
	SENSOR_ATTR(temp3_offset, S_IRUGO | S_IWUSR, show_temp_offset,
		    store_temp_offset, 2),
};

#define show_pwm_reg(reg) \
static ssize_t show_##reg(struct device *dev, struct device_attribute *attr, \
			  char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
	struct sensor_device_attribute *sensor_attr = \
		to_sensor_dev_attr(attr); \
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
	struct w83627ehf_sio_data *sio_data = dev->platform_data;
	int nr = sensor_attr->index;
	unsigned long val;
	int err;
	u16 reg;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (val > 1)
		return -EINVAL;

	/* On NCT67766F, DC mode is only supported for pwm1 */
	if (sio_data->kind == nct6776 && nr && val != 1)
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
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	val = clamp_val(val, 0, 255);

	mutex_lock(&data->update_lock);
	data->pwm[nr] = val;
	w83627ehf_write_value(data, data->REG_PWM[nr], val);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
store_pwm_enable(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	struct w83627ehf_sio_data *sio_data = dev->platform_data;
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	unsigned long val;
	int err;
	u16 reg;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (!val || (val > 4 && val != data->pwm_enable_orig[nr]))
		return -EINVAL;
	/* SmartFan III mode is not supported on NCT6776F */
	if (sio_data->kind == nct6776 && val == 4)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->pwm_enable[nr] = val;
	if (sio_data->kind == nct6775 || sio_data->kind == nct6776) {
		reg = w83627ehf_read_value(data,
					   NCT6775_REG_FAN_MODE[nr]);
		reg &= 0x0f;
		reg |= (val - 1) << 4;
		w83627ehf_write_value(data,
				      NCT6775_REG_FAN_MODE[nr], reg);
	} else {
		reg = w83627ehf_read_value(data, W83627EHF_REG_PWM_ENABLE[nr]);
		reg &= ~(0x03 << W83627EHF_PWM_ENABLE_SHIFT[nr]);
		reg |= (val - 1) << W83627EHF_PWM_ENABLE_SHIFT[nr];
		w83627ehf_write_value(data, W83627EHF_REG_PWM_ENABLE[nr], reg);
	}
	mutex_unlock(&data->update_lock);
	return count;
}


#define show_tol_temp(reg) \
static ssize_t show_##reg(struct device *dev, struct device_attribute *attr, \
				char *buf) \
{ \
	struct w83627ehf_data *data = w83627ehf_update_device(dev); \
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
	w83627ehf_write_value(data, data->REG_TARGET[nr], val);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
store_tolerance(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	struct w83627ehf_sio_data *sio_data = dev->platform_data;
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
	if (sio_data->kind == nct6775 || sio_data->kind == nct6776) {
		/* Limit tolerance further for NCT6776F */
		if (sio_data->kind == nct6776 && val > 7)
			val = 7;
		reg = w83627ehf_read_value(data, NCT6775_REG_FAN_MODE[nr]);
		reg = (reg & 0xf0) | val;
		w83627ehf_write_value(data, NCT6775_REG_FAN_MODE[nr], reg);
	} else {
		reg = w83627ehf_read_value(data, W83627EHF_REG_TOLERANCE[nr]);
		if (nr == 1)
			reg = (reg & 0x0f) | (val << 4);
		else
			reg = (reg & 0xf0) | val;
		w83627ehf_write_value(data, W83627EHF_REG_TOLERANCE[nr], reg);
	}
	data->tolerance[nr] = val;
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
	w83627ehf_write_value(data, data->REG_##REG[nr], val); \
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

static struct sensor_device_attribute sda_sf3_arrays_fan3[] = {
	SENSOR_ATTR(pwm3_stop_time, S_IWUSR | S_IRUGO, show_fan_stop_time,
		    store_fan_stop_time, 2),
	SENSOR_ATTR(pwm3_start_output, S_IWUSR | S_IRUGO, show_fan_start_output,
		    store_fan_start_output, 2),
	SENSOR_ATTR(pwm3_stop_output, S_IWUSR | S_IRUGO, show_fan_stop_output,
		    store_fan_stop_output, 2),
};

static struct sensor_device_attribute sda_sf3_arrays[] = {
	SENSOR_ATTR(pwm1_stop_time, S_IWUSR | S_IRUGO, show_fan_stop_time,
		    store_fan_stop_time, 0),
	SENSOR_ATTR(pwm2_stop_time, S_IWUSR | S_IRUGO, show_fan_stop_time,
		    store_fan_stop_time, 1),
	SENSOR_ATTR(pwm1_start_output, S_IWUSR | S_IRUGO, show_fan_start_output,
		    store_fan_start_output, 0),
	SENSOR_ATTR(pwm2_start_output, S_IWUSR | S_IRUGO, show_fan_start_output,
		    store_fan_start_output, 1),
	SENSOR_ATTR(pwm1_stop_output, S_IWUSR | S_IRUGO, show_fan_stop_output,
		    store_fan_stop_output, 0),
	SENSOR_ATTR(pwm2_stop_output, S_IWUSR | S_IRUGO, show_fan_stop_output,
		    store_fan_stop_output, 1),
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


/* Case open detection */

static ssize_t
show_caseopen(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);

	return sprintf(buf, "%d\n",
		!!(data->caseopen & to_sensor_dev_attr_2(attr)->index));
}

static ssize_t
clear_caseopen(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	unsigned long val;
	u16 reg, mask;

	if (kstrtoul(buf, 10, &val) || val != 0)
		return -EINVAL;

	mask = to_sensor_dev_attr_2(attr)->nr;

	mutex_lock(&data->update_lock);
	reg = w83627ehf_read_value(data, W83627EHF_REG_CASEOPEN_CLR);
	w83627ehf_write_value(data, W83627EHF_REG_CASEOPEN_CLR, reg | mask);
	w83627ehf_write_value(data, W83627EHF_REG_CASEOPEN_CLR, reg & ~mask);
	data->valid = 0;	/* Force cache refresh */
	mutex_unlock(&data->update_lock);

	return count;
}

static struct sensor_device_attribute_2 sda_caseopen[] = {
	SENSOR_ATTR_2(intrusion0_alarm, S_IWUSR | S_IRUGO, show_caseopen,
			clear_caseopen, 0x80, 0x10),
	SENSOR_ATTR_2(intrusion1_alarm, S_IWUSR | S_IRUGO, show_caseopen,
			clear_caseopen, 0x40, 0x40),
};

/*
 * Driver and device management
 */

static void w83627ehf_device_remove_files(struct device *dev)
{
	/*
	 * some entries in the following arrays may not have been used in
	 * device_create_file(), but device_remove_file() will ignore them
	 */
	int i;
	struct w83627ehf_data *data = dev_get_drvdata(dev);

	for (i = 0; i < ARRAY_SIZE(sda_sf3_arrays); i++)
		device_remove_file(dev, &sda_sf3_arrays[i].dev_attr);
	for (i = 0; i < ARRAY_SIZE(sda_sf3_max_step_arrays); i++) {
		struct sensor_device_attribute *attr =
		  &sda_sf3_max_step_arrays[i];
		if (data->REG_FAN_STEP_OUTPUT &&
		    data->REG_FAN_STEP_OUTPUT[attr->index] != 0xff)
			device_remove_file(dev, &attr->dev_attr);
	}
	for (i = 0; i < ARRAY_SIZE(sda_sf3_arrays_fan3); i++)
		device_remove_file(dev, &sda_sf3_arrays_fan3[i].dev_attr);
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
	for (i = 0; i < NUM_REG_TEMP; i++) {
		if (!(data->have_temp & (1 << i)))
			continue;
		device_remove_file(dev, &sda_temp_input[i].dev_attr);
		device_remove_file(dev, &sda_temp_label[i].dev_attr);
		if (i == 2 && data->temp3_val_only)
			continue;
		device_remove_file(dev, &sda_temp_max[i].dev_attr);
		device_remove_file(dev, &sda_temp_max_hyst[i].dev_attr);
		if (i > 2)
			continue;
		device_remove_file(dev, &sda_temp_alarm[i].dev_attr);
		device_remove_file(dev, &sda_temp_type[i].dev_attr);
		device_remove_file(dev, &sda_temp_offset[i].dev_attr);
	}

	device_remove_file(dev, &sda_caseopen[0].dev_attr);
	device_remove_file(dev, &sda_caseopen[1].dev_attr);

	device_remove_file(dev, &dev_attr_name);
	device_remove_file(dev, &dev_attr_cpu0_vid);
}

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

static void w82627ehf_swap_tempreg(struct w83627ehf_data *data,
				   int r1, int r2)
{
	u16 tmp;

	tmp = data->temp_src[r1];
	data->temp_src[r1] = data->temp_src[r2];
	data->temp_src[r2] = tmp;

	tmp = data->reg_temp[r1];
	data->reg_temp[r1] = data->reg_temp[r2];
	data->reg_temp[r2] = tmp;

	tmp = data->reg_temp_over[r1];
	data->reg_temp_over[r1] = data->reg_temp_over[r2];
	data->reg_temp_over[r2] = tmp;

	tmp = data->reg_temp_hyst[r1];
	data->reg_temp_hyst[r1] = data->reg_temp_hyst[r2];
	data->reg_temp_hyst[r2] = tmp;

	tmp = data->reg_temp_config[r1];
	data->reg_temp_config[r1] = data->reg_temp_config[r2];
	data->reg_temp_config[r2] = tmp;
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
	int fan3pin, fan4pin, fan4min, fan5pin, regval;

	/* The W83627UHG is simple, only two fan inputs, no config */
	if (sio_data->kind == w83627uhg) {
		data->has_fan = 0x03; /* fan1 and fan2 */
		data->has_fan_min = 0x03;
		return;
	}

	superio_enter(sio_data->sioreg);

	/* fan4 and fan5 share some pins with the GPIO and serial flash */
	if (sio_data->kind == nct6775) {
		/* On NCT6775, fan4 shares pins with the fdc interface */
		fan3pin = 1;
		fan4pin = !(superio_inb(sio_data->sioreg, 0x2A) & 0x80);
		fan4min = 0;
		fan5pin = 0;
	} else if (sio_data->kind == nct6776) {
		bool gpok = superio_inb(sio_data->sioreg, 0x27) & 0x80;

		superio_select(sio_data->sioreg, W83627EHF_LD_HWM);
		regval = superio_inb(sio_data->sioreg, SIO_REG_ENABLE);

		if (regval & 0x80)
			fan3pin = gpok;
		else
			fan3pin = !(superio_inb(sio_data->sioreg, 0x24) & 0x40);

		if (regval & 0x40)
			fan4pin = gpok;
		else
			fan4pin = !!(superio_inb(sio_data->sioreg, 0x1C) & 0x01);

		if (regval & 0x20)
			fan5pin = gpok;
		else
			fan5pin = !!(superio_inb(sio_data->sioreg, 0x1C) & 0x02);

		fan4min = fan4pin;
	} else if (sio_data->kind == w83667hg || sio_data->kind == w83667hg_b) {
		fan3pin = 1;
		fan4pin = superio_inb(sio_data->sioreg, 0x27) & 0x40;
		fan5pin = superio_inb(sio_data->sioreg, 0x27) & 0x20;
		fan4min = fan4pin;
	} else {
		fan3pin = 1;
		fan4pin = !(superio_inb(sio_data->sioreg, 0x29) & 0x06);
		fan5pin = !(superio_inb(sio_data->sioreg, 0x24) & 0x02);
		fan4min = fan4pin;
	}

	superio_exit(sio_data->sioreg);

	data->has_fan = data->has_fan_min = 0x03; /* fan1 and fan2 */
	data->has_fan |= (fan3pin << 2);
	data->has_fan_min |= (fan3pin << 2);

	if (sio_data->kind == nct6775 || sio_data->kind == nct6776) {
		/*
		 * NCT6775F and NCT6776F don't have the W83627EHF_REG_FANDIV1
		 * register
		 */
		data->has_fan |= (fan4pin << 3) | (fan5pin << 4);
		data->has_fan_min |= (fan4min << 3) | (fan5pin << 4);
	} else {
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
}

static int w83627ehf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct w83627ehf_sio_data *sio_data = dev->platform_data;
	struct w83627ehf_data *data;
	struct resource *res;
	u8 en_vrm10;
	int i, err = 0;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!request_region(res->start, IOREGION_LENGTH, DRVNAME)) {
		err = -EBUSY;
		dev_err(dev, "Failed to request region 0x%lx-0x%lx\n",
			(unsigned long)res->start,
			(unsigned long)res->start + IOREGION_LENGTH - 1);
		goto exit;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(struct w83627ehf_data),
			    GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit_release;
	}

	data->addr = res->start;
	mutex_init(&data->lock);
	mutex_init(&data->update_lock);
	data->name = w83627ehf_device_names[sio_data->kind];
	data->bank = 0xff;		/* Force initial bank selection */
	platform_set_drvdata(pdev, data);

	/* 627EHG and 627EHF have 10 voltage inputs; 627DHG and 667HG have 9 */
	data->in_num = (sio_data->kind == w83627ehf) ? 10 : 9;
	/* 667HG, NCT6775F, and NCT6776F have 3 pwms, and 627UHG has only 2 */
	switch (sio_data->kind) {
	default:
		data->pwm_num = 4;
		break;
	case w83667hg:
	case w83667hg_b:
	case nct6775:
	case nct6776:
		data->pwm_num = 3;
		break;
	case w83627uhg:
		data->pwm_num = 2;
		break;
	}

	/* Default to 3 temperature inputs, code below will adjust as needed */
	data->have_temp = 0x07;

	/* Deal with temperature register setup first. */
	if (sio_data->kind == nct6775 || sio_data->kind == nct6776) {
		int mask = 0;

		/*
		 * Display temperature sensor output only if it monitors
		 * a source other than one already reported. Always display
		 * first three temperature registers, though.
		 */
		for (i = 0; i < NUM_REG_TEMP; i++) {
			u8 src;

			data->reg_temp[i] = NCT6775_REG_TEMP[i];
			data->reg_temp_over[i] = NCT6775_REG_TEMP_OVER[i];
			data->reg_temp_hyst[i] = NCT6775_REG_TEMP_HYST[i];
			data->reg_temp_config[i] = NCT6775_REG_TEMP_CONFIG[i];

			src = w83627ehf_read_value(data,
						   NCT6775_REG_TEMP_SOURCE[i]);
			src &= 0x1f;
			if (src && !(mask & (1 << src))) {
				data->have_temp |= 1 << i;
				mask |= 1 << src;
			}

			data->temp_src[i] = src;

			/*
			 * Now do some register swapping if index 0..2 don't
			 * point to SYSTIN(1), CPUIN(2), and AUXIN(3).
			 * Idea is to have the first three attributes
			 * report SYSTIN, CPUIN, and AUXIN if possible
			 * without overriding the basic system configuration.
			 */
			if (i > 0 && data->temp_src[0] != 1
			    && data->temp_src[i] == 1)
				w82627ehf_swap_tempreg(data, 0, i);
			if (i > 1 && data->temp_src[1] != 2
			    && data->temp_src[i] == 2)
				w82627ehf_swap_tempreg(data, 1, i);
			if (i > 2 && data->temp_src[2] != 3
			    && data->temp_src[i] == 3)
				w82627ehf_swap_tempreg(data, 2, i);
		}
		if (sio_data->kind == nct6776) {
			/*
			 * On NCT6776, AUXTIN and VIN3 pins are shared.
			 * Only way to detect it is to check if AUXTIN is used
			 * as a temperature source, and if that source is
			 * enabled.
			 *
			 * If that is the case, disable in6, which reports VIN3.
			 * Otherwise disable temp3.
			 */
			if (data->temp_src[2] == 3) {
				u8 reg;

				if (data->reg_temp_config[2])
					reg = w83627ehf_read_value(data,
						data->reg_temp_config[2]);
				else
					reg = 0; /* Assume AUXTIN is used */

				if (reg & 0x01)
					data->have_temp &= ~(1 << 2);
				else
					data->in6_skip = 1;
			}
			data->temp_label = nct6776_temp_label;
		} else {
			data->temp_label = nct6775_temp_label;
		}
		data->have_temp_offset = data->have_temp & 0x07;
		for (i = 0; i < 3; i++) {
			if (data->temp_src[i] > 3)
				data->have_temp_offset &= ~(1 << i);
		}
	} else if (sio_data->kind == w83667hg_b) {
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

	if (sio_data->kind == nct6775) {
		data->has_fan_div = true;
		data->fan_from_reg = fan_from_reg16;
		data->fan_from_reg_min = fan_from_reg8;
		data->REG_PWM = NCT6775_REG_PWM;
		data->REG_TARGET = NCT6775_REG_TARGET;
		data->REG_FAN = NCT6775_REG_FAN;
		data->REG_FAN_MIN = W83627EHF_REG_FAN_MIN;
		data->REG_FAN_START_OUTPUT = NCT6775_REG_FAN_START_OUTPUT;
		data->REG_FAN_STOP_OUTPUT = NCT6775_REG_FAN_STOP_OUTPUT;
		data->REG_FAN_STOP_TIME = NCT6775_REG_FAN_STOP_TIME;
		data->REG_FAN_MAX_OUTPUT = NCT6775_REG_FAN_MAX_OUTPUT;
		data->REG_FAN_STEP_OUTPUT = NCT6775_REG_FAN_STEP_OUTPUT;
	} else if (sio_data->kind == nct6776) {
		data->has_fan_div = false;
		data->fan_from_reg = fan_from_reg13;
		data->fan_from_reg_min = fan_from_reg13;
		data->REG_PWM = NCT6775_REG_PWM;
		data->REG_TARGET = NCT6775_REG_TARGET;
		data->REG_FAN = NCT6775_REG_FAN;
		data->REG_FAN_MIN = NCT6776_REG_FAN_MIN;
		data->REG_FAN_START_OUTPUT = NCT6775_REG_FAN_START_OUTPUT;
		data->REG_FAN_STOP_OUTPUT = NCT6775_REG_FAN_STOP_OUTPUT;
		data->REG_FAN_STOP_TIME = NCT6775_REG_FAN_STOP_TIME;
	} else if (sio_data->kind == w83667hg_b) {
		data->has_fan_div = true;
		data->fan_from_reg = fan_from_reg8;
		data->fan_from_reg_min = fan_from_reg8;
		data->REG_PWM = W83627EHF_REG_PWM;
		data->REG_TARGET = W83627EHF_REG_TARGET;
		data->REG_FAN = W83627EHF_REG_FAN;
		data->REG_FAN_MIN = W83627EHF_REG_FAN_MIN;
		data->REG_FAN_START_OUTPUT = W83627EHF_REG_FAN_START_OUTPUT;
		data->REG_FAN_STOP_OUTPUT = W83627EHF_REG_FAN_STOP_OUTPUT;
		data->REG_FAN_STOP_TIME = W83627EHF_REG_FAN_STOP_TIME;
		data->REG_FAN_MAX_OUTPUT =
		  W83627EHF_REG_FAN_MAX_OUTPUT_W83667_B;
		data->REG_FAN_STEP_OUTPUT =
		  W83627EHF_REG_FAN_STEP_OUTPUT_W83667_B;
	} else {
		data->has_fan_div = true;
		data->fan_from_reg = fan_from_reg8;
		data->fan_from_reg_min = fan_from_reg8;
		data->REG_PWM = W83627EHF_REG_PWM;
		data->REG_TARGET = W83627EHF_REG_TARGET;
		data->REG_FAN = W83627EHF_REG_FAN;
		data->REG_FAN_MIN = W83627EHF_REG_FAN_MIN;
		data->REG_FAN_START_OUTPUT = W83627EHF_REG_FAN_START_OUTPUT;
		data->REG_FAN_STOP_OUTPUT = W83627EHF_REG_FAN_STOP_OUTPUT;
		data->REG_FAN_STOP_TIME = W83627EHF_REG_FAN_STOP_TIME;
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
	superio_enter(sio_data->sioreg);
	/* Read VID value */
	if (sio_data->kind == w83667hg || sio_data->kind == w83667hg_b ||
	    sio_data->kind == nct6775 || sio_data->kind == nct6776) {
		/*
		 * W83667HG has different pins for VID input and output, so
		 * we can get the VID input values directly at logical device D
		 * 0xe3.
		 */
		superio_select(sio_data->sioreg, W83667HG_LD_VID);
		data->vid = superio_inb(sio_data->sioreg, 0xe3);
		err = device_create_file(dev, &dev_attr_cpu0_vid);
		if (err)
			goto exit_release;
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

			err = device_create_file(dev, &dev_attr_cpu0_vid);
			if (err)
				goto exit_release;
		} else {
			dev_info(dev,
				 "VID pins in output mode, CPU VID not available\n");
		}
	}

	if (fan_debounce &&
	    (sio_data->kind == nct6775 || sio_data->kind == nct6776)) {
		u8 tmp;

		superio_select(sio_data->sioreg, W83627EHF_LD_HWM);
		tmp = superio_inb(sio_data->sioreg, NCT6775_REG_FAN_DEBOUNCE);
		if (sio_data->kind == nct6776)
			superio_outb(sio_data->sioreg, NCT6775_REG_FAN_DEBOUNCE,
				     0x3e | tmp);
		else
			superio_outb(sio_data->sioreg, NCT6775_REG_FAN_DEBOUNCE,
				     0x1e | tmp);
		pr_info("Enabled fan debounce for chip %s\n", data->name);
	}

	superio_exit(sio_data->sioreg);

	w83627ehf_check_fan_inputs(sio_data, data);

	/* Read fan clock dividers immediately */
	w83627ehf_update_fan_div_common(dev, data);

	/* Read pwm data to save original values */
	w83627ehf_update_pwm_common(dev, data);
	for (i = 0; i < data->pwm_num; i++)
		data->pwm_enable_orig[i] = data->pwm_enable[i];

	/* Register sysfs hooks */
	for (i = 0; i < ARRAY_SIZE(sda_sf3_arrays); i++) {
		err = device_create_file(dev, &sda_sf3_arrays[i].dev_attr);
		if (err)
			goto exit_remove;
	}

	for (i = 0; i < ARRAY_SIZE(sda_sf3_max_step_arrays); i++) {
		struct sensor_device_attribute *attr =
		  &sda_sf3_max_step_arrays[i];
		if (data->REG_FAN_STEP_OUTPUT &&
		    data->REG_FAN_STEP_OUTPUT[attr->index] != 0xff) {
			err = device_create_file(dev, &attr->dev_attr);
			if (err)
				goto exit_remove;
		}
	}
	/* if fan3 and fan4 are enabled create the sf3 files for them */
	if ((data->has_fan & (1 << 2)) && data->pwm_num >= 3)
		for (i = 0; i < ARRAY_SIZE(sda_sf3_arrays_fan3); i++) {
			err = device_create_file(dev,
					&sda_sf3_arrays_fan3[i].dev_attr);
			if (err)
				goto exit_remove;
		}
	if ((data->has_fan & (1 << 3)) && data->pwm_num >= 4)
		for (i = 0; i < ARRAY_SIZE(sda_sf3_arrays_fan4); i++) {
			err = device_create_file(dev,
					&sda_sf3_arrays_fan4[i].dev_attr);
			if (err)
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
					&sda_fan_alarm[i].dev_attr)))
				goto exit_remove;
			if (sio_data->kind != nct6776) {
				err = device_create_file(dev,
						&sda_fan_div[i].dev_attr);
				if (err)
					goto exit_remove;
			}
			if (data->has_fan_min & (1 << i)) {
				err = device_create_file(dev,
						&sda_fan_min[i].dev_attr);
				if (err)
					goto exit_remove;
			}
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

	for (i = 0; i < NUM_REG_TEMP; i++) {
		if (!(data->have_temp & (1 << i)))
			continue;
		err = device_create_file(dev, &sda_temp_input[i].dev_attr);
		if (err)
			goto exit_remove;
		if (data->temp_label) {
			err = device_create_file(dev,
						 &sda_temp_label[i].dev_attr);
			if (err)
				goto exit_remove;
		}
		if (i == 2 && data->temp3_val_only)
			continue;
		if (data->reg_temp_over[i]) {
			err = device_create_file(dev,
				&sda_temp_max[i].dev_attr);
			if (err)
				goto exit_remove;
		}
		if (data->reg_temp_hyst[i]) {
			err = device_create_file(dev,
				&sda_temp_max_hyst[i].dev_attr);
			if (err)
				goto exit_remove;
		}
		if (i > 2)
			continue;
		if ((err = device_create_file(dev,
				&sda_temp_alarm[i].dev_attr))
			|| (err = device_create_file(dev,
				&sda_temp_type[i].dev_attr)))
			goto exit_remove;
		if (data->have_temp_offset & (1 << i)) {
			err = device_create_file(dev,
						 &sda_temp_offset[i].dev_attr);
			if (err)
				goto exit_remove;
		}
	}

	err = device_create_file(dev, &sda_caseopen[0].dev_attr);
	if (err)
		goto exit_remove;

	if (sio_data->kind == nct6776) {
		err = device_create_file(dev, &sda_caseopen[1].dev_attr);
		if (err)
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
exit_release:
	platform_set_drvdata(pdev, NULL);
	release_region(res->start, IOREGION_LENGTH);
exit:
	return err;
}

static int w83627ehf_remove(struct platform_device *pdev)
{
	struct w83627ehf_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	w83627ehf_device_remove_files(&pdev->dev);
	release_region(data->addr, IOREGION_LENGTH);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int w83627ehf_suspend(struct device *dev)
{
	struct w83627ehf_data *data = w83627ehf_update_device(dev);
	struct w83627ehf_sio_data *sio_data = dev->platform_data;

	mutex_lock(&data->update_lock);
	data->vbat = w83627ehf_read_value(data, W83627EHF_REG_VBAT);
	if (sio_data->kind == nct6775) {
		data->fandiv1 = w83627ehf_read_value(data, NCT6775_REG_FANDIV1);
		data->fandiv2 = w83627ehf_read_value(data, NCT6775_REG_FANDIV2);
	}
	mutex_unlock(&data->update_lock);

	return 0;
}

static int w83627ehf_resume(struct device *dev)
{
	struct w83627ehf_data *data = dev_get_drvdata(dev);
	struct w83627ehf_sio_data *sio_data = dev->platform_data;
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

		w83627ehf_write_value(data, data->REG_FAN_MIN[i],
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
	if (sio_data->kind == nct6775) {
		w83627ehf_write_value(data, NCT6775_REG_FANDIV1, data->fandiv1);
		w83627ehf_write_value(data, NCT6775_REG_FANDIV2, data->fandiv2);
	}

	/* Force re-reading all values */
	data->valid = 0;
	mutex_unlock(&data->update_lock);

	return 0;
}

static const struct dev_pm_ops w83627ehf_dev_pm_ops = {
	.suspend = w83627ehf_suspend,
	.resume = w83627ehf_resume,
};

#define W83627EHF_DEV_PM_OPS	(&w83627ehf_dev_pm_ops)
#else
#define W83627EHF_DEV_PM_OPS	NULL
#endif /* CONFIG_PM */

static struct platform_driver w83627ehf_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
		.pm	= W83627EHF_DEV_PM_OPS,
	},
	.probe		= w83627ehf_probe,
	.remove		= w83627ehf_remove,
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
	static const char sio_name_NCT6775[] __initconst = "NCT6775F";
	static const char sio_name_NCT6776[] __initconst = "NCT6776F";

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
	case SIO_NCT6775_ID:
		sio_data->kind = nct6775;
		sio_name = sio_name_NCT6775;
		break;
	case SIO_NCT6776_ID:
		sio_data->kind = nct6776;
		sio_name = sio_name_NCT6776;
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
 * track of the w83627ehf driver. But since we platform_device_alloc(), we
 * must keep track of the device
 */
static struct platform_device *pdev;

static int __init sensors_w83627ehf_init(void)
{
	int err;
	unsigned short address;
	struct resource res;
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

	err = platform_driver_register(&w83627ehf_driver);
	if (err)
		goto exit;

	pdev = platform_device_alloc(DRVNAME, address);
	if (!pdev) {
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
