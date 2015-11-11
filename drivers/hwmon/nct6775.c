/*
 * nct6775 - Driver for the hardware monitoring functionality of
 *	       Nuvoton NCT677x Super-I/O chips
 *
 * Copyright (C) 2012  Guenter Roeck <linux@roeck-us.net>
 *
 * Derived from w83627ehf driver
 * Copyright (C) 2005-2012  Jean Delvare <khali@linux-fr.org>
 * Copyright (C) 2006  Yuan Mu (Winbond),
 *		       Rudolf Marek <r.marek@assembler.cz>
 *		       David Hubbard <david.c.hubbard@gmail.com>
 *		       Daniel J Blueman <daniel.blueman@gmail.com>
 * Copyright (C) 2010  Sheng-Yuan Huang (Nuvoton) (PS00)
 *
 * Shamelessly ripped from the w83627hf driver
 * Copyright (C) 2003  Mark Studebaker
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * Supports the following chips:
 *
 * Chip        #vin    #fan    #pwm    #temp  chip IDs       man ID
 * nct6775f     9      4       3       6+3    0xb470 0xc1    0x5ca3
 * nct6776f     9      5       3       6+3    0xc330 0xc1    0x5ca3
 * nct6779d    15      5       5       2+6    0xc560 0xc1    0x5ca3
 *
 * #temp lists the number of monitored temperature sources (first value) plus
 * the number of directly connectable temperature sensors (second value).
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

#define USE_ALTERNATE

enum kinds { nct6775, nct6776, nct6779 };

/* used to set data->name = nct6775_device_names[data->sio_kind] */
static const char * const nct6775_device_names[] = {
	"nct6775",
	"nct6776",
	"nct6779",
};

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

static unsigned short fan_debounce;
module_param(fan_debounce, ushort, 0);
MODULE_PARM_DESC(fan_debounce, "Enable debouncing for fan RPM signal");

#define DRVNAME "nct6775"

/*
 * Super-I/O constants and functions
 */

#define NCT6775_LD_ACPI		0x0a
#define NCT6775_LD_HWM		0x0b
#define NCT6775_LD_VID		0x0d

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */

#define SIO_NCT6775_ID		0xb470
#define SIO_NCT6776_ID		0xc330
#define SIO_NCT6779_ID		0xc560
#define SIO_ID_MASK		0xFFF0

enum pwm_enable { off, manual, thermal_cruise, speed_cruise, sf3, sf4 };

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
	/*
	 * Try to reserve <ioreg> and <ioreg + 1> for exclusive access.
	 */
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

#define NCT6775_REG_BANK	0x4E
#define NCT6775_REG_CONFIG	0x40

/*
 * Not currently used:
 * REG_MAN_ID has the value 0x5ca3 for all supported chips.
 * REG_CHIP_ID == 0x88/0xa1/0xc1 depending on chip model.
 * REG_MAN_ID is at port 0x4f
 * REG_CHIP_ID is at port 0x58
 */

#define NUM_TEMP	10	/* Max number of temp attribute sets w/ limits*/
#define NUM_TEMP_FIXED	6	/* Max number of fixed temp attribute sets */

#define NUM_REG_ALARM	4	/* Max number of alarm registers */

/* Common and NCT6775 specific data */

/* Voltage min/max registers for nr=7..14 are in bank 5 */

static const u16 NCT6775_REG_IN_MAX[] = {
	0x2b, 0x2d, 0x2f, 0x31, 0x33, 0x35, 0x37, 0x554, 0x556, 0x558, 0x55a,
	0x55c, 0x55e, 0x560, 0x562 };
static const u16 NCT6775_REG_IN_MIN[] = {
	0x2c, 0x2e, 0x30, 0x32, 0x34, 0x36, 0x38, 0x555, 0x557, 0x559, 0x55b,
	0x55d, 0x55f, 0x561, 0x563 };
static const u16 NCT6775_REG_IN[] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x550, 0x551, 0x552
};

#define NCT6775_REG_VBAT		0x5D
#define NCT6775_REG_DIODE		0x5E

#define NCT6775_REG_FANDIV1		0x506
#define NCT6775_REG_FANDIV2		0x507

#define NCT6775_REG_CR_FAN_DEBOUNCE	0xf0

static const u16 NCT6775_REG_ALARM[NUM_REG_ALARM] = { 0x459, 0x45A, 0x45B };

/* 0..15 voltages, 16..23 fans, 24..31 temperatures */

static const s8 NCT6775_ALARM_BITS[] = {
	0, 1, 2, 3, 8, 21, 20, 16,	/* in0.. in7 */
	17, -1, -1, -1, -1, -1, -1,	/* in8..in14 */
	-1,				/* unused */
	6, 7, 11, 10, 23,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	4, 5, 13, -1, -1, -1,		/* temp1..temp6 */
	12, -1 };			/* intrusion0, intrusion1 */

#define FAN_ALARM_BASE		16
#define TEMP_ALARM_BASE		24
#define INTRUSION_ALARM_BASE	30

static const u8 NCT6775_REG_CR_CASEOPEN_CLR[] = { 0xe6, 0xee };
static const u8 NCT6775_CR_CASEOPEN_CLR_MASK[] = { 0x20, 0x01 };

/* DC or PWM output fan configuration */
static const u8 NCT6775_REG_PWM_MODE[] = { 0x04, 0x04, 0x12 };
static const u8 NCT6775_PWM_MODE_MASK[] = { 0x01, 0x02, 0x01 };

/* Advanced Fan control, some values are common for all fans */

static const u16 NCT6775_REG_TARGET[] = { 0x101, 0x201, 0x301, 0x801, 0x901 };
static const u16 NCT6775_REG_FAN_MODE[] = { 0x102, 0x202, 0x302, 0x802, 0x902 };
static const u16 NCT6775_REG_FAN_STEP_DOWN_TIME[] = {
	0x103, 0x203, 0x303, 0x803, 0x903 };
static const u16 NCT6775_REG_FAN_STEP_UP_TIME[] = {
	0x104, 0x204, 0x304, 0x804, 0x904 };
static const u16 NCT6775_REG_FAN_STOP_OUTPUT[] = {
	0x105, 0x205, 0x305, 0x805, 0x905 };
static const u16 NCT6775_REG_FAN_START_OUTPUT[]
	= { 0x106, 0x206, 0x306, 0x806, 0x906 };
static const u16 NCT6775_REG_FAN_MAX_OUTPUT[] = { 0x10a, 0x20a, 0x30a };
static const u16 NCT6775_REG_FAN_STEP_OUTPUT[] = { 0x10b, 0x20b, 0x30b };

static const u16 NCT6775_REG_FAN_STOP_TIME[] = {
	0x107, 0x207, 0x307, 0x807, 0x907 };
static const u16 NCT6775_REG_PWM[] = { 0x109, 0x209, 0x309, 0x809, 0x909 };
static const u16 NCT6775_REG_PWM_READ[] = { 0x01, 0x03, 0x11, 0x13, 0x15 };

static const u16 NCT6775_REG_FAN[] = { 0x630, 0x632, 0x634, 0x636, 0x638 };
static const u16 NCT6775_REG_FAN_MIN[] = { 0x3b, 0x3c, 0x3d };
static const u16 NCT6775_REG_FAN_PULSES[] = { 0x641, 0x642, 0x643, 0x644, 0 };

static const u16 NCT6775_REG_TEMP[] = {
	0x27, 0x150, 0x250, 0x62b, 0x62c, 0x62d };

static const u16 NCT6775_REG_TEMP_CONFIG[ARRAY_SIZE(NCT6775_REG_TEMP)] = {
	0, 0x152, 0x252, 0x628, 0x629, 0x62A };
static const u16 NCT6775_REG_TEMP_HYST[ARRAY_SIZE(NCT6775_REG_TEMP)] = {
	0x3a, 0x153, 0x253, 0x673, 0x678, 0x67D };
static const u16 NCT6775_REG_TEMP_OVER[ARRAY_SIZE(NCT6775_REG_TEMP)] = {
	0x39, 0x155, 0x255, 0x672, 0x677, 0x67C };

static const u16 NCT6775_REG_TEMP_SOURCE[ARRAY_SIZE(NCT6775_REG_TEMP)] = {
	0x621, 0x622, 0x623, 0x624, 0x625, 0x626 };

static const u16 NCT6775_REG_TEMP_SEL[] = {
	0x100, 0x200, 0x300, 0x800, 0x900 };

static const u16 NCT6775_REG_WEIGHT_TEMP_SEL[] = {
	0x139, 0x239, 0x339, 0x839, 0x939 };
static const u16 NCT6775_REG_WEIGHT_TEMP_STEP[] = {
	0x13a, 0x23a, 0x33a, 0x83a, 0x93a };
static const u16 NCT6775_REG_WEIGHT_TEMP_STEP_TOL[] = {
	0x13b, 0x23b, 0x33b, 0x83b, 0x93b };
static const u16 NCT6775_REG_WEIGHT_DUTY_STEP[] = {
	0x13c, 0x23c, 0x33c, 0x83c, 0x93c };
static const u16 NCT6775_REG_WEIGHT_TEMP_BASE[] = {
	0x13d, 0x23d, 0x33d, 0x83d, 0x93d };

static const u16 NCT6775_REG_TEMP_OFFSET[] = { 0x454, 0x455, 0x456 };

static const u16 NCT6775_REG_AUTO_TEMP[] = {
	0x121, 0x221, 0x321, 0x821, 0x921 };
static const u16 NCT6775_REG_AUTO_PWM[] = {
	0x127, 0x227, 0x327, 0x827, 0x927 };

#define NCT6775_AUTO_TEMP(data, nr, p)	((data)->REG_AUTO_TEMP[nr] + (p))
#define NCT6775_AUTO_PWM(data, nr, p)	((data)->REG_AUTO_PWM[nr] + (p))

static const u16 NCT6775_REG_CRITICAL_ENAB[] = { 0x134, 0x234, 0x334 };

static const u16 NCT6775_REG_CRITICAL_TEMP[] = {
	0x135, 0x235, 0x335, 0x835, 0x935 };
static const u16 NCT6775_REG_CRITICAL_TEMP_TOLERANCE[] = {
	0x138, 0x238, 0x338, 0x838, 0x938 };

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

static const u16 NCT6775_REG_TEMP_ALTERNATE[ARRAY_SIZE(nct6775_temp_label) - 1]
	= { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x661, 0x662, 0x664 };

static const u16 NCT6775_REG_TEMP_CRIT[ARRAY_SIZE(nct6775_temp_label) - 1]
	= { 0, 0, 0, 0, 0xa00, 0xa01, 0xa02, 0xa03, 0xa04, 0xa05, 0xa06,
	    0xa07 };

/* NCT6776 specific data */

static const s8 NCT6776_ALARM_BITS[] = {
	0, 1, 2, 3, 8, 21, 20, 16,	/* in0.. in7 */
	17, -1, -1, -1, -1, -1, -1,	/* in8..in14 */
	-1,				/* unused */
	6, 7, 11, 10, 23,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	4, 5, 13, -1, -1, -1,		/* temp1..temp6 */
	12, 9 };			/* intrusion0, intrusion1 */

static const u16 NCT6776_REG_TOLERANCE_H[] = {
	0x10c, 0x20c, 0x30c, 0x80c, 0x90c };

static const u8 NCT6776_REG_PWM_MODE[] = { 0x04, 0, 0 };
static const u8 NCT6776_PWM_MODE_MASK[] = { 0x01, 0, 0 };

static const u16 NCT6776_REG_FAN_MIN[] = { 0x63a, 0x63c, 0x63e, 0x640, 0x642 };
static const u16 NCT6776_REG_FAN_PULSES[] = { 0x644, 0x645, 0x646, 0, 0 };

static const u16 NCT6776_REG_WEIGHT_DUTY_BASE[] = {
	0x13e, 0x23e, 0x33e, 0x83e, 0x93e };

static const u16 NCT6776_REG_TEMP_CONFIG[ARRAY_SIZE(NCT6775_REG_TEMP)] = {
	0x18, 0x152, 0x252, 0x628, 0x629, 0x62A };

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

static const u16 NCT6776_REG_TEMP_ALTERNATE[ARRAY_SIZE(nct6776_temp_label) - 1]
	= { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x401, 0x402, 0x404 };

static const u16 NCT6776_REG_TEMP_CRIT[ARRAY_SIZE(nct6776_temp_label) - 1]
	= { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x709, 0x70a };

/* NCT6779 specific data */

static const u16 NCT6779_REG_IN[] = {
	0x480, 0x481, 0x482, 0x483, 0x484, 0x485, 0x486, 0x487,
	0x488, 0x489, 0x48a, 0x48b, 0x48c, 0x48d, 0x48e };

static const u16 NCT6779_REG_ALARM[NUM_REG_ALARM] = {
	0x459, 0x45A, 0x45B, 0x568 };

static const s8 NCT6779_ALARM_BITS[] = {
	0, 1, 2, 3, 8, 21, 20, 16,	/* in0.. in7 */
	17, 24, 25, 26, 27, 28, 29,	/* in8..in14 */
	-1,				/* unused */
	6, 7, 11, 10, 23,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	4, 5, 13, -1, -1, -1,		/* temp1..temp6 */
	12, 9 };			/* intrusion0, intrusion1 */

static const u16 NCT6779_REG_FAN[] = { 0x4b0, 0x4b2, 0x4b4, 0x4b6, 0x4b8 };
static const u16 NCT6779_REG_FAN_PULSES[] = {
	0x644, 0x645, 0x646, 0x647, 0x648 };

static const u16 NCT6779_REG_CRITICAL_PWM_ENABLE[] = {
	0x136, 0x236, 0x336, 0x836, 0x936 };
static const u16 NCT6779_REG_CRITICAL_PWM[] = {
	0x137, 0x237, 0x337, 0x837, 0x937 };

static const u16 NCT6779_REG_TEMP[] = { 0x27, 0x150 };
static const u16 NCT6779_REG_TEMP_CONFIG[ARRAY_SIZE(NCT6779_REG_TEMP)] = {
	0x18, 0x152 };
static const u16 NCT6779_REG_TEMP_HYST[ARRAY_SIZE(NCT6779_REG_TEMP)] = {
	0x3a, 0x153 };
static const u16 NCT6779_REG_TEMP_OVER[ARRAY_SIZE(NCT6779_REG_TEMP)] = {
	0x39, 0x155 };

static const u16 NCT6779_REG_TEMP_OFFSET[] = {
	0x454, 0x455, 0x456, 0x44a, 0x44b, 0x44c };

static const char *const nct6779_temp_label[] = {
	"",
	"SYSTIN",
	"CPUTIN",
	"AUXTIN0",
	"AUXTIN1",
	"AUXTIN2",
	"AUXTIN3",
	"",
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

static const u16 NCT6779_REG_TEMP_ALTERNATE[ARRAY_SIZE(nct6779_temp_label) - 1]
	= { 0x490, 0x491, 0x492, 0x493, 0x494, 0x495, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0x400, 0x401, 0x402, 0x404, 0x405, 0x406, 0x407,
	    0x408, 0 };

static const u16 NCT6779_REG_TEMP_CRIT[ARRAY_SIZE(nct6779_temp_label) - 1]
	= { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x709, 0x70a };

static enum pwm_enable reg_to_pwm_enable(int pwm, int mode)
{
	if (mode == 0 && pwm == 255)
		return off;
	return mode + 1;
}

static int pwm_enable_to_reg(enum pwm_enable mode)
{
	if (mode == off)
		return 0;
	return mode - 1;
}

/*
 * Conversions
 */

/* 1 is DC mode, output in ms */
static unsigned int step_time_from_reg(u8 reg, u8 mode)
{
	return mode ? 400 * reg : 100 * reg;
}

static u8 step_time_to_reg(unsigned int msec, u8 mode)
{
	return clamp_val((mode ? (msec + 200) / 400 :
					(msec + 50) / 100), 1, 255);
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

static u16 fan_to_reg(u32 fan, unsigned int divreg)
{
	if (!fan)
		return 0;

	return (1350000U / fan) >> divreg;
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
static const u16 scale_in[15] = {
	800, 800, 1600, 1600, 800, 800, 800, 1600, 1600, 800, 800, 800, 800,
	800, 800
};

static inline long in_from_reg(u8 reg, u8 nr)
{
	return DIV_ROUND_CLOSEST(reg * scale_in[nr], 100);
}

static inline u8 in_to_reg(u32 val, u8 nr)
{
	return clamp_val(DIV_ROUND_CLOSEST(val * 100, scale_in[nr]), 0, 255);
}

/*
 * Data structures and manipulation thereof
 */

struct nct6775_data {
	int addr;	/* IO base of hw monitor block */
	enum kinds kind;
	const char *name;

	struct device *hwmon_dev;

	u16 reg_temp[4][NUM_TEMP]; /* 0=temp, 1=temp_over, 2=temp_hyst,
				    * 3=temp_crit
				    */
	u8 temp_src[NUM_TEMP];
	u16 reg_temp_config[NUM_TEMP];
	const char * const *temp_label;
	int temp_label_num;

	u16 REG_CONFIG;
	u16 REG_VBAT;
	u16 REG_DIODE;

	const s8 *ALARM_BITS;

	const u16 *REG_VIN;
	const u16 *REG_IN_MINMAX[2];

	const u16 *REG_TARGET;
	const u16 *REG_FAN;
	const u16 *REG_FAN_MODE;
	const u16 *REG_FAN_MIN;
	const u16 *REG_FAN_PULSES;
	const u16 *REG_FAN_TIME[3];

	const u16 *REG_TOLERANCE_H;

	const u8 *REG_PWM_MODE;
	const u8 *PWM_MODE_MASK;

	const u16 *REG_PWM[7];	/* [0]=pwm, [1]=pwm_start, [2]=pwm_floor,
				 * [3]=pwm_max, [4]=pwm_step,
				 * [5]=weight_duty_step, [6]=weight_duty_base
				 */
	const u16 *REG_PWM_READ;

	const u16 *REG_AUTO_TEMP;
	const u16 *REG_AUTO_PWM;

	const u16 *REG_CRITICAL_TEMP;
	const u16 *REG_CRITICAL_TEMP_TOLERANCE;

	const u16 *REG_TEMP_SOURCE;	/* temp register sources */
	const u16 *REG_TEMP_SEL;
	const u16 *REG_WEIGHT_TEMP_SEL;
	const u16 *REG_WEIGHT_TEMP[3];	/* 0=base, 1=tolerance, 2=step */

	const u16 *REG_TEMP_OFFSET;

	const u16 *REG_ALARM;

	unsigned int (*fan_from_reg)(u16 reg, unsigned int divreg);
	unsigned int (*fan_from_reg_min)(u16 reg, unsigned int divreg);

	struct mutex update_lock;
	bool valid;		/* true if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* Register values */
	u8 bank;		/* current register bank */
	u8 in_num;		/* number of in inputs we have */
	u8 in[15][3];		/* [0]=in, [1]=in_max, [2]=in_min */
	unsigned int rpm[5];
	u16 fan_min[5];
	u8 fan_pulses[5];
	u8 fan_div[5];
	u8 has_pwm;
	u8 has_fan;		/* some fan inputs can be disabled */
	u8 has_fan_min;		/* some fans don't have min register */
	bool has_fan_div;

	u8 temp_fixed_num;	/* 3 or 6 */
	u8 temp_type[NUM_TEMP_FIXED];
	s8 temp_offset[NUM_TEMP_FIXED];
	s16 temp[4][NUM_TEMP]; /* 0=temp, 1=temp_over, 2=temp_hyst,
				* 3=temp_crit */
	u64 alarms;

	u8 pwm_num;	/* number of pwm */
	u8 pwm_mode[5]; /* 1->DC variable voltage, 0->PWM variable duty cycle */
	enum pwm_enable pwm_enable[5];
			/* 0->off
			 * 1->manual
			 * 2->thermal cruise mode (also called SmartFan I)
			 * 3->fan speed cruise mode
			 * 4->SmartFan III
			 * 5->enhanced variable thermal cruise (SmartFan IV)
			 */
	u8 pwm[7][5];	/* [0]=pwm, [1]=pwm_start, [2]=pwm_floor,
			 * [3]=pwm_max, [4]=pwm_step,
			 * [5]=weight_duty_step, [6]=weight_duty_base
			 */

	u8 target_temp[5];
	u8 target_temp_mask;
	u32 target_speed[5];
	u32 target_speed_tolerance[5];
	u8 speed_tolerance_limit;

	u8 temp_tolerance[2][5];
	u8 tolerance_mask;

	u8 fan_time[3][5]; /* 0 = stop_time, 1 = step_up, 2 = step_down */

	/* Automatic fan speed control registers */
	int auto_pwm_num;
	u8 auto_pwm[5][7];
	u8 auto_temp[5][7];
	u8 pwm_temp_sel[5];
	u8 pwm_weight_temp_sel[5];
	u8 weight_temp[3][5];	/* 0->temp_step, 1->temp_step_tol,
				 * 2->temp_base
				 */

	u8 vid;
	u8 vrm;

	u16 have_temp;
	u16 have_temp_fixed;
	u16 have_in;
#ifdef CONFIG_PM
	/* Remember extra register values over suspend/resume */
	u8 vbat;
	u8 fandiv1;
	u8 fandiv2;
#endif
};

struct nct6775_sio_data {
	int sioreg;
	enum kinds kind;
};

static bool is_word_sized(struct nct6775_data *data, u16 reg)
{
	switch (data->kind) {
	case nct6775:
		return (((reg & 0xff00) == 0x100 ||
		    (reg & 0xff00) == 0x200) &&
		   ((reg & 0x00ff) == 0x50 ||
		    (reg & 0x00ff) == 0x53 ||
		    (reg & 0x00ff) == 0x55)) ||
		  (reg & 0xfff0) == 0x630 ||
		  reg == 0x640 || reg == 0x642 ||
		  reg == 0x662 ||
		  ((reg & 0xfff0) == 0x650 && (reg & 0x000f) >= 0x06) ||
		  reg == 0x73 || reg == 0x75 || reg == 0x77;
	case nct6776:
		return (((reg & 0xff00) == 0x100 ||
		    (reg & 0xff00) == 0x200) &&
		   ((reg & 0x00ff) == 0x50 ||
		    (reg & 0x00ff) == 0x53 ||
		    (reg & 0x00ff) == 0x55)) ||
		  (reg & 0xfff0) == 0x630 ||
		  reg == 0x402 ||
		  reg == 0x640 || reg == 0x642 ||
		  ((reg & 0xfff0) == 0x650 && (reg & 0x000f) >= 0x06) ||
		  reg == 0x73 || reg == 0x75 || reg == 0x77;
	case nct6779:
		return reg == 0x150 || reg == 0x153 || reg == 0x155 ||
		  ((reg & 0xfff0) == 0x4b0 && (reg & 0x000f) < 0x09) ||
		  reg == 0x402 ||
		  reg == 0x63a || reg == 0x63c || reg == 0x63e ||
		  reg == 0x640 || reg == 0x642 ||
		  reg == 0x73 || reg == 0x75 || reg == 0x77 || reg == 0x79 ||
		  reg == 0x7b;
	}
	return false;
}

/*
 * On older chips, only registers 0x50-0x5f are banked.
 * On more recent chips, all registers are banked.
 * Assume that is the case and set the bank number for each access.
 * Cache the bank number so it only needs to be set if it changes.
 */
static inline void nct6775_set_bank(struct nct6775_data *data, u16 reg)
{
	u8 bank = reg >> 8;
	if (data->bank != bank) {
		outb_p(NCT6775_REG_BANK, data->addr + ADDR_REG_OFFSET);
		outb_p(bank, data->addr + DATA_REG_OFFSET);
		data->bank = bank;
	}
}

static u16 nct6775_read_value(struct nct6775_data *data, u16 reg)
{
	int res, word_sized = is_word_sized(data, reg);

	nct6775_set_bank(data, reg);
	outb_p(reg & 0xff, data->addr + ADDR_REG_OFFSET);
	res = inb_p(data->addr + DATA_REG_OFFSET);
	if (word_sized) {
		outb_p((reg & 0xff) + 1,
		       data->addr + ADDR_REG_OFFSET);
		res = (res << 8) + inb_p(data->addr + DATA_REG_OFFSET);
	}
	return res;
}

static int nct6775_write_value(struct nct6775_data *data, u16 reg, u16 value)
{
	int word_sized = is_word_sized(data, reg);

	nct6775_set_bank(data, reg);
	outb_p(reg & 0xff, data->addr + ADDR_REG_OFFSET);
	if (word_sized) {
		outb_p(value >> 8, data->addr + DATA_REG_OFFSET);
		outb_p((reg & 0xff) + 1,
		       data->addr + ADDR_REG_OFFSET);
	}
	outb_p(value & 0xff, data->addr + DATA_REG_OFFSET);
	return 0;
}

/* We left-align 8-bit temperature values to make the code simpler */
static u16 nct6775_read_temp(struct nct6775_data *data, u16 reg)
{
	u16 res;

	res = nct6775_read_value(data, reg);
	if (!is_word_sized(data, reg))
		res <<= 8;

	return res;
}

static int nct6775_write_temp(struct nct6775_data *data, u16 reg, u16 value)
{
	if (!is_word_sized(data, reg))
		value >>= 8;
	return nct6775_write_value(data, reg, value);
}

/* This function assumes that the caller holds data->update_lock */
static void nct6775_write_fan_div(struct nct6775_data *data, int nr)
{
	u8 reg;

	switch (nr) {
	case 0:
		reg = (nct6775_read_value(data, NCT6775_REG_FANDIV1) & 0x70)
		    | (data->fan_div[0] & 0x7);
		nct6775_write_value(data, NCT6775_REG_FANDIV1, reg);
		break;
	case 1:
		reg = (nct6775_read_value(data, NCT6775_REG_FANDIV1) & 0x7)
		    | ((data->fan_div[1] << 4) & 0x70);
		nct6775_write_value(data, NCT6775_REG_FANDIV1, reg);
		break;
	case 2:
		reg = (nct6775_read_value(data, NCT6775_REG_FANDIV2) & 0x70)
		    | (data->fan_div[2] & 0x7);
		nct6775_write_value(data, NCT6775_REG_FANDIV2, reg);
		break;
	case 3:
		reg = (nct6775_read_value(data, NCT6775_REG_FANDIV2) & 0x7)
		    | ((data->fan_div[3] << 4) & 0x70);
		nct6775_write_value(data, NCT6775_REG_FANDIV2, reg);
		break;
	}
}

static void nct6775_write_fan_div_common(struct nct6775_data *data, int nr)
{
	if (data->kind == nct6775)
		nct6775_write_fan_div(data, nr);
}

static void nct6775_update_fan_div(struct nct6775_data *data)
{
	u8 i;

	i = nct6775_read_value(data, NCT6775_REG_FANDIV1);
	data->fan_div[0] = i & 0x7;
	data->fan_div[1] = (i & 0x70) >> 4;
	i = nct6775_read_value(data, NCT6775_REG_FANDIV2);
	data->fan_div[2] = i & 0x7;
	if (data->has_fan & (1 << 3))
		data->fan_div[3] = (i & 0x70) >> 4;
}

static void nct6775_update_fan_div_common(struct nct6775_data *data)
{
	if (data->kind == nct6775)
		nct6775_update_fan_div(data);
}

static void nct6775_init_fan_div(struct nct6775_data *data)
{
	int i;

	nct6775_update_fan_div_common(data);
	/*
	 * For all fans, start with highest divider value if the divider
	 * register is not initialized. This ensures that we get a
	 * reading from the fan count register, even if it is not optimal.
	 * We'll compute a better divider later on.
	 */
	for (i = 0; i < ARRAY_SIZE(data->fan_div); i++) {
		if (!(data->has_fan & (1 << i)))
			continue;
		if (data->fan_div[i] == 0) {
			data->fan_div[i] = 7;
			nct6775_write_fan_div_common(data, i);
		}
	}
}

static void nct6775_init_fan_common(struct device *dev,
				    struct nct6775_data *data)
{
	int i;
	u8 reg;

	if (data->has_fan_div)
		nct6775_init_fan_div(data);

	/*
	 * If fan_min is not set (0), set it to 0xff to disable it. This
	 * prevents the unnecessary warning when fanX_min is reported as 0.
	 */
	for (i = 0; i < ARRAY_SIZE(data->fan_min); i++) {
		if (data->has_fan_min & (1 << i)) {
			reg = nct6775_read_value(data, data->REG_FAN_MIN[i]);
			if (!reg)
				nct6775_write_value(data, data->REG_FAN_MIN[i],
						    data->has_fan_div ? 0xff
								      : 0xff1f);
		}
	}
}

static void nct6775_select_fan_div(struct device *dev,
				   struct nct6775_data *data, int nr, u16 reg)
{
	u8 fan_div = data->fan_div[nr];
	u16 fan_min;

	if (!data->has_fan_div)
		return;

	/*
	 * If we failed to measure the fan speed, or the reported value is not
	 * in the optimal range, and the clock divider can be modified,
	 * let's try that for next time.
	 */
	if (reg == 0x00 && fan_div < 0x07)
		fan_div++;
	else if (reg != 0x00 && reg < 0x30 && fan_div > 0)
		fan_div--;

	if (fan_div != data->fan_div[nr]) {
		dev_dbg(dev, "Modifying fan%d clock divider from %u to %u\n",
			nr + 1, div_from_reg(data->fan_div[nr]),
			div_from_reg(fan_div));

		/* Preserve min limit if possible */
		if (data->has_fan_min & (1 << nr)) {
			fan_min = data->fan_min[nr];
			if (fan_div > data->fan_div[nr]) {
				if (fan_min != 255 && fan_min > 1)
					fan_min >>= 1;
			} else {
				if (fan_min != 255) {
					fan_min <<= 1;
					if (fan_min > 254)
						fan_min = 254;
				}
			}
			if (fan_min != data->fan_min[nr]) {
				data->fan_min[nr] = fan_min;
				nct6775_write_value(data, data->REG_FAN_MIN[nr],
						    fan_min);
			}
		}
		data->fan_div[nr] = fan_div;
		nct6775_write_fan_div_common(data, nr);
	}
}

static void nct6775_update_pwm(struct device *dev)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	int i, j;
	int fanmodecfg, reg;
	bool duty_is_dc;

	for (i = 0; i < data->pwm_num; i++) {
		if (!(data->has_pwm & (1 << i)))
			continue;

		duty_is_dc = data->REG_PWM_MODE[i] &&
		  (nct6775_read_value(data, data->REG_PWM_MODE[i])
		   & data->PWM_MODE_MASK[i]);
		data->pwm_mode[i] = duty_is_dc;

		fanmodecfg = nct6775_read_value(data, data->REG_FAN_MODE[i]);
		for (j = 0; j < ARRAY_SIZE(data->REG_PWM); j++) {
			if (data->REG_PWM[j] && data->REG_PWM[j][i]) {
				data->pwm[j][i]
				  = nct6775_read_value(data,
						       data->REG_PWM[j][i]);
			}
		}

		data->pwm_enable[i] = reg_to_pwm_enable(data->pwm[0][i],
							(fanmodecfg >> 4) & 7);

		if (!data->temp_tolerance[0][i] ||
		    data->pwm_enable[i] != speed_cruise)
			data->temp_tolerance[0][i] = fanmodecfg & 0x0f;
		if (!data->target_speed_tolerance[i] ||
		    data->pwm_enable[i] == speed_cruise) {
			u8 t = fanmodecfg & 0x0f;
			if (data->REG_TOLERANCE_H) {
				t |= (nct6775_read_value(data,
				      data->REG_TOLERANCE_H[i]) & 0x70) >> 1;
			}
			data->target_speed_tolerance[i] = t;
		}

		data->temp_tolerance[1][i] =
			nct6775_read_value(data,
					data->REG_CRITICAL_TEMP_TOLERANCE[i]);

		reg = nct6775_read_value(data, data->REG_TEMP_SEL[i]);
		data->pwm_temp_sel[i] = reg & 0x1f;
		/* If fan can stop, report floor as 0 */
		if (reg & 0x80)
			data->pwm[2][i] = 0;

		reg = nct6775_read_value(data, data->REG_WEIGHT_TEMP_SEL[i]);
		data->pwm_weight_temp_sel[i] = reg & 0x1f;
		/* If weight is disabled, report weight source as 0 */
		if (j == 1 && !(reg & 0x80))
			data->pwm_weight_temp_sel[i] = 0;

		/* Weight temp data */
		for (j = 0; j < ARRAY_SIZE(data->weight_temp); j++) {
			data->weight_temp[j][i]
			  = nct6775_read_value(data,
					       data->REG_WEIGHT_TEMP[j][i]);
		}
	}
}

static void nct6775_update_pwm_limits(struct device *dev)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	int i, j;
	u8 reg;
	u16 reg_t;

	for (i = 0; i < data->pwm_num; i++) {
		if (!(data->has_pwm & (1 << i)))
			continue;

		for (j = 0; j < ARRAY_SIZE(data->fan_time); j++) {
			data->fan_time[j][i] =
			  nct6775_read_value(data, data->REG_FAN_TIME[j][i]);
		}

		reg_t = nct6775_read_value(data, data->REG_TARGET[i]);
		/* Update only in matching mode or if never updated */
		if (!data->target_temp[i] ||
		    data->pwm_enable[i] == thermal_cruise)
			data->target_temp[i] = reg_t & data->target_temp_mask;
		if (!data->target_speed[i] ||
		    data->pwm_enable[i] == speed_cruise) {
			if (data->REG_TOLERANCE_H) {
				reg_t |= (nct6775_read_value(data,
					data->REG_TOLERANCE_H[i]) & 0x0f) << 8;
			}
			data->target_speed[i] = reg_t;
		}

		for (j = 0; j < data->auto_pwm_num; j++) {
			data->auto_pwm[i][j] =
			  nct6775_read_value(data,
					     NCT6775_AUTO_PWM(data, i, j));
			data->auto_temp[i][j] =
			  nct6775_read_value(data,
					     NCT6775_AUTO_TEMP(data, i, j));
		}

		/* critical auto_pwm temperature data */
		data->auto_temp[i][data->auto_pwm_num] =
			nct6775_read_value(data, data->REG_CRITICAL_TEMP[i]);

		switch (data->kind) {
		case nct6775:
			reg = nct6775_read_value(data,
						 NCT6775_REG_CRITICAL_ENAB[i]);
			data->auto_pwm[i][data->auto_pwm_num] =
						(reg & 0x02) ? 0xff : 0x00;
			break;
		case nct6776:
			data->auto_pwm[i][data->auto_pwm_num] = 0xff;
			break;
		case nct6779:
			reg = nct6775_read_value(data,
					NCT6779_REG_CRITICAL_PWM_ENABLE[i]);
			if (reg & 1)
				data->auto_pwm[i][data->auto_pwm_num] =
				  nct6775_read_value(data,
					NCT6779_REG_CRITICAL_PWM[i]);
			else
				data->auto_pwm[i][data->auto_pwm_num] = 0xff;
			break;
		}
	}
}

static struct nct6775_data *nct6775_update_device(struct device *dev)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	int i, j;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		/* Fan clock dividers */
		nct6775_update_fan_div_common(data);

		/* Measured voltages and limits */
		for (i = 0; i < data->in_num; i++) {
			if (!(data->have_in & (1 << i)))
				continue;

			data->in[i][0] = nct6775_read_value(data,
							    data->REG_VIN[i]);
			data->in[i][1] = nct6775_read_value(data,
					  data->REG_IN_MINMAX[0][i]);
			data->in[i][2] = nct6775_read_value(data,
					  data->REG_IN_MINMAX[1][i]);
		}

		/* Measured fan speeds and limits */
		for (i = 0; i < ARRAY_SIZE(data->rpm); i++) {
			u16 reg;

			if (!(data->has_fan & (1 << i)))
				continue;

			reg = nct6775_read_value(data, data->REG_FAN[i]);
			data->rpm[i] = data->fan_from_reg(reg,
							  data->fan_div[i]);

			if (data->has_fan_min & (1 << i))
				data->fan_min[i] = nct6775_read_value(data,
					   data->REG_FAN_MIN[i]);
			data->fan_pulses[i] =
			  nct6775_read_value(data, data->REG_FAN_PULSES[i]);

			nct6775_select_fan_div(dev, data, i, reg);
		}

		nct6775_update_pwm(dev);
		nct6775_update_pwm_limits(dev);

		/* Measured temperatures and limits */
		for (i = 0; i < NUM_TEMP; i++) {
			if (!(data->have_temp & (1 << i)))
				continue;
			for (j = 0; j < ARRAY_SIZE(data->reg_temp); j++) {
				if (data->reg_temp[j][i])
					data->temp[j][i]
					  = nct6775_read_temp(data,
						data->reg_temp[j][i]);
			}
			if (!(data->have_temp_fixed & (1 << i)))
				continue;
			data->temp_offset[i]
			  = nct6775_read_value(data, data->REG_TEMP_OFFSET[i]);
		}

		data->alarms = 0;
		for (i = 0; i < NUM_REG_ALARM; i++) {
			u8 alarm;
			if (!data->REG_ALARM[i])
				continue;
			alarm = nct6775_read_value(data, data->REG_ALARM[i]);
			data->alarms |= ((u64)alarm) << (i << 3);
		}

		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);
	return data;
}

/*
 * Sysfs callback functions
 */
static ssize_t
show_in_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;
	return sprintf(buf, "%ld\n", in_from_reg(data->in[nr][index], nr));
}

static ssize_t
store_in_reg(struct device *dev, struct device_attribute *attr, const char *buf,
	     size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;
	unsigned long val;
	int err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	mutex_lock(&data->update_lock);
	data->in[nr][index] = in_to_reg(val, nr);
	nct6775_write_value(data, data->REG_IN_MINMAX[index - 1][nr],
			    data->in[nr][index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_alarm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = data->ALARM_BITS[sattr->index];
	return sprintf(buf, "%u\n",
		       (unsigned int)((data->alarms >> nr) & 0x01));
}

static SENSOR_DEVICE_ATTR_2(in0_input, S_IRUGO, show_in_reg, NULL, 0, 0);
static SENSOR_DEVICE_ATTR_2(in1_input, S_IRUGO, show_in_reg, NULL, 1, 0);
static SENSOR_DEVICE_ATTR_2(in2_input, S_IRUGO, show_in_reg, NULL, 2, 0);
static SENSOR_DEVICE_ATTR_2(in3_input, S_IRUGO, show_in_reg, NULL, 3, 0);
static SENSOR_DEVICE_ATTR_2(in4_input, S_IRUGO, show_in_reg, NULL, 4, 0);
static SENSOR_DEVICE_ATTR_2(in5_input, S_IRUGO, show_in_reg, NULL, 5, 0);
static SENSOR_DEVICE_ATTR_2(in6_input, S_IRUGO, show_in_reg, NULL, 6, 0);
static SENSOR_DEVICE_ATTR_2(in7_input, S_IRUGO, show_in_reg, NULL, 7, 0);
static SENSOR_DEVICE_ATTR_2(in8_input, S_IRUGO, show_in_reg, NULL, 8, 0);
static SENSOR_DEVICE_ATTR_2(in9_input, S_IRUGO, show_in_reg, NULL, 9, 0);
static SENSOR_DEVICE_ATTR_2(in10_input, S_IRUGO, show_in_reg, NULL, 10, 0);
static SENSOR_DEVICE_ATTR_2(in11_input, S_IRUGO, show_in_reg, NULL, 11, 0);
static SENSOR_DEVICE_ATTR_2(in12_input, S_IRUGO, show_in_reg, NULL, 12, 0);
static SENSOR_DEVICE_ATTR_2(in13_input, S_IRUGO, show_in_reg, NULL, 13, 0);
static SENSOR_DEVICE_ATTR_2(in14_input, S_IRUGO, show_in_reg, NULL, 14, 0);

static SENSOR_DEVICE_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(in5_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(in6_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(in7_alarm, S_IRUGO, show_alarm, NULL, 7);
static SENSOR_DEVICE_ATTR(in8_alarm, S_IRUGO, show_alarm, NULL, 8);
static SENSOR_DEVICE_ATTR(in9_alarm, S_IRUGO, show_alarm, NULL, 9);
static SENSOR_DEVICE_ATTR(in10_alarm, S_IRUGO, show_alarm, NULL, 10);
static SENSOR_DEVICE_ATTR(in11_alarm, S_IRUGO, show_alarm, NULL, 11);
static SENSOR_DEVICE_ATTR(in12_alarm, S_IRUGO, show_alarm, NULL, 12);
static SENSOR_DEVICE_ATTR(in13_alarm, S_IRUGO, show_alarm, NULL, 13);
static SENSOR_DEVICE_ATTR(in14_alarm, S_IRUGO, show_alarm, NULL, 14);

static SENSOR_DEVICE_ATTR_2(in0_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 0, 1);
static SENSOR_DEVICE_ATTR_2(in1_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 1, 1);
static SENSOR_DEVICE_ATTR_2(in2_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 2, 1);
static SENSOR_DEVICE_ATTR_2(in3_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 3, 1);
static SENSOR_DEVICE_ATTR_2(in4_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 4, 1);
static SENSOR_DEVICE_ATTR_2(in5_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 5, 1);
static SENSOR_DEVICE_ATTR_2(in6_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 6, 1);
static SENSOR_DEVICE_ATTR_2(in7_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 7, 1);
static SENSOR_DEVICE_ATTR_2(in8_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 8, 1);
static SENSOR_DEVICE_ATTR_2(in9_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 9, 1);
static SENSOR_DEVICE_ATTR_2(in10_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 10, 1);
static SENSOR_DEVICE_ATTR_2(in11_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 11, 1);
static SENSOR_DEVICE_ATTR_2(in12_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 12, 1);
static SENSOR_DEVICE_ATTR_2(in13_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 13, 1);
static SENSOR_DEVICE_ATTR_2(in14_min, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 14, 1);

static SENSOR_DEVICE_ATTR_2(in0_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 0, 2);
static SENSOR_DEVICE_ATTR_2(in1_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 1, 2);
static SENSOR_DEVICE_ATTR_2(in2_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 2, 2);
static SENSOR_DEVICE_ATTR_2(in3_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 3, 2);
static SENSOR_DEVICE_ATTR_2(in4_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 4, 2);
static SENSOR_DEVICE_ATTR_2(in5_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 5, 2);
static SENSOR_DEVICE_ATTR_2(in6_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 6, 2);
static SENSOR_DEVICE_ATTR_2(in7_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 7, 2);
static SENSOR_DEVICE_ATTR_2(in8_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 8, 2);
static SENSOR_DEVICE_ATTR_2(in9_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 9, 2);
static SENSOR_DEVICE_ATTR_2(in10_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 10, 2);
static SENSOR_DEVICE_ATTR_2(in11_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 11, 2);
static SENSOR_DEVICE_ATTR_2(in12_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 12, 2);
static SENSOR_DEVICE_ATTR_2(in13_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 13, 2);
static SENSOR_DEVICE_ATTR_2(in14_max, S_IWUSR | S_IRUGO, show_in_reg,
			    store_in_reg, 14, 2);

static struct attribute *nct6775_attributes_in[15][5] = {
	{
		&sensor_dev_attr_in0_input.dev_attr.attr,
		&sensor_dev_attr_in0_min.dev_attr.attr,
		&sensor_dev_attr_in0_max.dev_attr.attr,
		&sensor_dev_attr_in0_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in1_input.dev_attr.attr,
		&sensor_dev_attr_in1_min.dev_attr.attr,
		&sensor_dev_attr_in1_max.dev_attr.attr,
		&sensor_dev_attr_in1_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in2_input.dev_attr.attr,
		&sensor_dev_attr_in2_min.dev_attr.attr,
		&sensor_dev_attr_in2_max.dev_attr.attr,
		&sensor_dev_attr_in2_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in3_input.dev_attr.attr,
		&sensor_dev_attr_in3_min.dev_attr.attr,
		&sensor_dev_attr_in3_max.dev_attr.attr,
		&sensor_dev_attr_in3_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in4_input.dev_attr.attr,
		&sensor_dev_attr_in4_min.dev_attr.attr,
		&sensor_dev_attr_in4_max.dev_attr.attr,
		&sensor_dev_attr_in4_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in5_input.dev_attr.attr,
		&sensor_dev_attr_in5_min.dev_attr.attr,
		&sensor_dev_attr_in5_max.dev_attr.attr,
		&sensor_dev_attr_in5_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in6_input.dev_attr.attr,
		&sensor_dev_attr_in6_min.dev_attr.attr,
		&sensor_dev_attr_in6_max.dev_attr.attr,
		&sensor_dev_attr_in6_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in7_input.dev_attr.attr,
		&sensor_dev_attr_in7_min.dev_attr.attr,
		&sensor_dev_attr_in7_max.dev_attr.attr,
		&sensor_dev_attr_in7_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in8_input.dev_attr.attr,
		&sensor_dev_attr_in8_min.dev_attr.attr,
		&sensor_dev_attr_in8_max.dev_attr.attr,
		&sensor_dev_attr_in8_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in9_input.dev_attr.attr,
		&sensor_dev_attr_in9_min.dev_attr.attr,
		&sensor_dev_attr_in9_max.dev_attr.attr,
		&sensor_dev_attr_in9_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in10_input.dev_attr.attr,
		&sensor_dev_attr_in10_min.dev_attr.attr,
		&sensor_dev_attr_in10_max.dev_attr.attr,
		&sensor_dev_attr_in10_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in11_input.dev_attr.attr,
		&sensor_dev_attr_in11_min.dev_attr.attr,
		&sensor_dev_attr_in11_max.dev_attr.attr,
		&sensor_dev_attr_in11_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in12_input.dev_attr.attr,
		&sensor_dev_attr_in12_min.dev_attr.attr,
		&sensor_dev_attr_in12_max.dev_attr.attr,
		&sensor_dev_attr_in12_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in13_input.dev_attr.attr,
		&sensor_dev_attr_in13_min.dev_attr.attr,
		&sensor_dev_attr_in13_max.dev_attr.attr,
		&sensor_dev_attr_in13_alarm.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_in14_input.dev_attr.attr,
		&sensor_dev_attr_in14_min.dev_attr.attr,
		&sensor_dev_attr_in14_max.dev_attr.attr,
		&sensor_dev_attr_in14_alarm.dev_attr.attr,
		NULL
	},
};

static const struct attribute_group nct6775_group_in[15] = {
	{ .attrs = nct6775_attributes_in[0] },
	{ .attrs = nct6775_attributes_in[1] },
	{ .attrs = nct6775_attributes_in[2] },
	{ .attrs = nct6775_attributes_in[3] },
	{ .attrs = nct6775_attributes_in[4] },
	{ .attrs = nct6775_attributes_in[5] },
	{ .attrs = nct6775_attributes_in[6] },
	{ .attrs = nct6775_attributes_in[7] },
	{ .attrs = nct6775_attributes_in[8] },
	{ .attrs = nct6775_attributes_in[9] },
	{ .attrs = nct6775_attributes_in[10] },
	{ .attrs = nct6775_attributes_in[11] },
	{ .attrs = nct6775_attributes_in[12] },
	{ .attrs = nct6775_attributes_in[13] },
	{ .attrs = nct6775_attributes_in[14] },
};

static ssize_t
show_fan(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	return sprintf(buf, "%d\n", data->rpm[nr]);
}

static ssize_t
show_fan_min(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	return sprintf(buf, "%d\n",
		       data->fan_from_reg_min(data->fan_min[nr],
					      data->fan_div[nr]));
}

static ssize_t
show_fan_div(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	return sprintf(buf, "%u\n", div_from_reg(data->fan_div[nr]));
}

static ssize_t
store_fan_min(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	unsigned long val;
	int err;
	unsigned int reg;
	u8 new_div;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	if (!data->has_fan_div) {
		/* NCT6776F or NCT6779D; we know this is a 13 bit register */
		if (!val) {
			val = 0xff1f;
		} else {
			if (val > 1350000U)
				val = 135000U;
			val = 1350000U / val;
			val = (val & 0x1f) | ((val << 3) & 0xff00);
		}
		data->fan_min[nr] = val;
		goto write_min;	/* Leave fan divider alone */
	}
	if (!val) {
		/* No min limit, alarm disabled */
		data->fan_min[nr] = 255;
		new_div = data->fan_div[nr]; /* No change */
		dev_info(dev, "fan%u low limit and alarm disabled\n", nr + 1);
		goto write_div;
	}
	reg = 1350000U / val;
	if (reg >= 128 * 255) {
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

write_div:
	/*
	 * Write both the fan clock divider (if it changed) and the new
	 * fan min (unconditionally)
	 */
	if (new_div != data->fan_div[nr]) {
		dev_dbg(dev, "fan%u clock divider changed from %u to %u\n",
			nr + 1, div_from_reg(data->fan_div[nr]),
			div_from_reg(new_div));
		data->fan_div[nr] = new_div;
		nct6775_write_fan_div_common(data, nr);
		/* Give the chip time to sample a new speed value */
		data->last_updated = jiffies;
	}

write_min:
	nct6775_write_value(data, data->REG_FAN_MIN[nr], data->fan_min[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_fan_pulses(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int p = data->fan_pulses[sattr->index];

	return sprintf(buf, "%d\n", p ? : 4);
}

static ssize_t
store_fan_pulses(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (val > 4)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->fan_pulses[nr] = val & 3;
	nct6775_write_value(data, data->REG_FAN_PULSES[nr], val & 3);
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
	SENSOR_ATTR(fan1_alarm, S_IRUGO, show_alarm, NULL, FAN_ALARM_BASE),
	SENSOR_ATTR(fan2_alarm, S_IRUGO, show_alarm, NULL, FAN_ALARM_BASE + 1),
	SENSOR_ATTR(fan3_alarm, S_IRUGO, show_alarm, NULL, FAN_ALARM_BASE + 2),
	SENSOR_ATTR(fan4_alarm, S_IRUGO, show_alarm, NULL, FAN_ALARM_BASE + 3),
	SENSOR_ATTR(fan5_alarm, S_IRUGO, show_alarm, NULL, FAN_ALARM_BASE + 4),
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

static struct sensor_device_attribute sda_fan_pulses[] = {
	SENSOR_ATTR(fan1_pulses, S_IWUSR | S_IRUGO, show_fan_pulses,
		    store_fan_pulses, 0),
	SENSOR_ATTR(fan2_pulses, S_IWUSR | S_IRUGO, show_fan_pulses,
		    store_fan_pulses, 1),
	SENSOR_ATTR(fan3_pulses, S_IWUSR | S_IRUGO, show_fan_pulses,
		    store_fan_pulses, 2),
	SENSOR_ATTR(fan4_pulses, S_IWUSR | S_IRUGO, show_fan_pulses,
		    store_fan_pulses, 3),
	SENSOR_ATTR(fan5_pulses, S_IWUSR | S_IRUGO, show_fan_pulses,
		    store_fan_pulses, 4),
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
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	return sprintf(buf, "%s\n", data->temp_label[data->temp_src[nr]]);
}

static ssize_t
show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;

	return sprintf(buf, "%d\n", LM75_TEMP_FROM_REG(data->temp[index][nr]));
}

static ssize_t
store_temp(struct device *dev, struct device_attribute *attr, const char *buf,
	   size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;
	int err;
	long val;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	data->temp[index][nr] = LM75_TEMP_TO_REG(val);
	nct6775_write_temp(data, data->reg_temp[index][nr],
			   data->temp[index][nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_temp_offset(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	return sprintf(buf, "%d\n", data->temp_offset[sattr->index] * 1000);
}

static ssize_t
store_temp_offset(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), -128, 127);

	mutex_lock(&data->update_lock);
	data->temp_offset[nr] = val;
	nct6775_write_value(data, data->REG_TEMP_OFFSET[nr], val);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_temp_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	return sprintf(buf, "%d\n", (int)data->temp_type[nr]);
}

static ssize_t
store_temp_type(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	unsigned long val;
	int err;
	u8 vbat, diode, bit;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (val != 1 && val != 3 && val != 4)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	data->temp_type[nr] = val;
	vbat = nct6775_read_value(data, data->REG_VBAT) & ~(0x02 << nr);
	diode = nct6775_read_value(data, data->REG_DIODE) & ~(0x02 << nr);
	bit = 0x02 << nr;
	switch (val) {
	case 1:	/* CPU diode (diode, current mode) */
		vbat |= bit;
		diode |= bit;
		break;
	case 3: /* diode, voltage mode */
		vbat |= bit;
		break;
	case 4:	/* thermistor */
		break;
	}
	nct6775_write_value(data, data->REG_VBAT, vbat);
	nct6775_write_value(data, data->REG_DIODE, diode);

	mutex_unlock(&data->update_lock);
	return count;
}

static struct sensor_device_attribute_2 sda_temp_input[] = {
	SENSOR_ATTR_2(temp1_input, S_IRUGO, show_temp, NULL, 0, 0),
	SENSOR_ATTR_2(temp2_input, S_IRUGO, show_temp, NULL, 1, 0),
	SENSOR_ATTR_2(temp3_input, S_IRUGO, show_temp, NULL, 2, 0),
	SENSOR_ATTR_2(temp4_input, S_IRUGO, show_temp, NULL, 3, 0),
	SENSOR_ATTR_2(temp5_input, S_IRUGO, show_temp, NULL, 4, 0),
	SENSOR_ATTR_2(temp6_input, S_IRUGO, show_temp, NULL, 5, 0),
	SENSOR_ATTR_2(temp7_input, S_IRUGO, show_temp, NULL, 6, 0),
	SENSOR_ATTR_2(temp8_input, S_IRUGO, show_temp, NULL, 7, 0),
	SENSOR_ATTR_2(temp9_input, S_IRUGO, show_temp, NULL, 8, 0),
	SENSOR_ATTR_2(temp10_input, S_IRUGO, show_temp, NULL, 9, 0),
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
	SENSOR_ATTR(temp10_label, S_IRUGO, show_temp_label, NULL, 9),
};

static struct sensor_device_attribute_2 sda_temp_max[] = {
	SENSOR_ATTR_2(temp1_max, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      0, 1),
	SENSOR_ATTR_2(temp2_max, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      1, 1),
	SENSOR_ATTR_2(temp3_max, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      2, 1),
	SENSOR_ATTR_2(temp4_max, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      3, 1),
	SENSOR_ATTR_2(temp5_max, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      4, 1),
	SENSOR_ATTR_2(temp6_max, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      5, 1),
	SENSOR_ATTR_2(temp7_max, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      6, 1),
	SENSOR_ATTR_2(temp8_max, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      7, 1),
	SENSOR_ATTR_2(temp9_max, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      8, 1),
	SENSOR_ATTR_2(temp10_max, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      9, 1),
};

static struct sensor_device_attribute_2 sda_temp_max_hyst[] = {
	SENSOR_ATTR_2(temp1_max_hyst, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      0, 2),
	SENSOR_ATTR_2(temp2_max_hyst, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      1, 2),
	SENSOR_ATTR_2(temp3_max_hyst, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      2, 2),
	SENSOR_ATTR_2(temp4_max_hyst, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      3, 2),
	SENSOR_ATTR_2(temp5_max_hyst, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      4, 2),
	SENSOR_ATTR_2(temp6_max_hyst, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      5, 2),
	SENSOR_ATTR_2(temp7_max_hyst, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      6, 2),
	SENSOR_ATTR_2(temp8_max_hyst, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      7, 2),
	SENSOR_ATTR_2(temp9_max_hyst, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      8, 2),
	SENSOR_ATTR_2(temp10_max_hyst, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      9, 2),
};

static struct sensor_device_attribute_2 sda_temp_crit[] = {
	SENSOR_ATTR_2(temp1_crit, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      0, 3),
	SENSOR_ATTR_2(temp2_crit, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      1, 3),
	SENSOR_ATTR_2(temp3_crit, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      2, 3),
	SENSOR_ATTR_2(temp4_crit, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      3, 3),
	SENSOR_ATTR_2(temp5_crit, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      4, 3),
	SENSOR_ATTR_2(temp6_crit, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      5, 3),
	SENSOR_ATTR_2(temp7_crit, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      6, 3),
	SENSOR_ATTR_2(temp8_crit, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      7, 3),
	SENSOR_ATTR_2(temp9_crit, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      8, 3),
	SENSOR_ATTR_2(temp10_crit, S_IRUGO | S_IWUSR, show_temp, store_temp,
		      9, 3),
};

static struct sensor_device_attribute sda_temp_offset[] = {
	SENSOR_ATTR(temp1_offset, S_IRUGO | S_IWUSR, show_temp_offset,
		    store_temp_offset, 0),
	SENSOR_ATTR(temp2_offset, S_IRUGO | S_IWUSR, show_temp_offset,
		    store_temp_offset, 1),
	SENSOR_ATTR(temp3_offset, S_IRUGO | S_IWUSR, show_temp_offset,
		    store_temp_offset, 2),
	SENSOR_ATTR(temp4_offset, S_IRUGO | S_IWUSR, show_temp_offset,
		    store_temp_offset, 3),
	SENSOR_ATTR(temp5_offset, S_IRUGO | S_IWUSR, show_temp_offset,
		    store_temp_offset, 4),
	SENSOR_ATTR(temp6_offset, S_IRUGO | S_IWUSR, show_temp_offset,
		    store_temp_offset, 5),
};

static struct sensor_device_attribute sda_temp_type[] = {
	SENSOR_ATTR(temp1_type, S_IRUGO | S_IWUSR, show_temp_type,
		    store_temp_type, 0),
	SENSOR_ATTR(temp2_type, S_IRUGO | S_IWUSR, show_temp_type,
		    store_temp_type, 1),
	SENSOR_ATTR(temp3_type, S_IRUGO | S_IWUSR, show_temp_type,
		    store_temp_type, 2),
	SENSOR_ATTR(temp4_type, S_IRUGO | S_IWUSR, show_temp_type,
		    store_temp_type, 3),
	SENSOR_ATTR(temp5_type, S_IRUGO | S_IWUSR, show_temp_type,
		    store_temp_type, 4),
	SENSOR_ATTR(temp6_type, S_IRUGO | S_IWUSR, show_temp_type,
		    store_temp_type, 5),
};

static struct sensor_device_attribute sda_temp_alarm[] = {
	SENSOR_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL,
		    TEMP_ALARM_BASE),
	SENSOR_ATTR(temp2_alarm, S_IRUGO, show_alarm, NULL,
		    TEMP_ALARM_BASE + 1),
	SENSOR_ATTR(temp3_alarm, S_IRUGO, show_alarm, NULL,
		    TEMP_ALARM_BASE + 2),
	SENSOR_ATTR(temp4_alarm, S_IRUGO, show_alarm, NULL,
		    TEMP_ALARM_BASE + 3),
	SENSOR_ATTR(temp5_alarm, S_IRUGO, show_alarm, NULL,
		    TEMP_ALARM_BASE + 4),
	SENSOR_ATTR(temp6_alarm, S_IRUGO, show_alarm, NULL,
		    TEMP_ALARM_BASE + 5),
};

#define NUM_TEMP_ALARM	ARRAY_SIZE(sda_temp_alarm)

static ssize_t
show_pwm_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	return sprintf(buf, "%d\n", !data->pwm_mode[sattr->index]);
}

static ssize_t
store_pwm_mode(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	unsigned long val;
	int err;
	u8 reg;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (val > 1)
		return -EINVAL;

	/* Setting DC mode is not supported for all chips/channels */
	if (data->REG_PWM_MODE[nr] == 0) {
		if (val)
			return -EINVAL;
		return count;
	}

	mutex_lock(&data->update_lock);
	data->pwm_mode[nr] = val;
	reg = nct6775_read_value(data, data->REG_PWM_MODE[nr]);
	reg &= ~data->PWM_MODE_MASK[nr];
	if (val)
		reg |= data->PWM_MODE_MASK[nr];
	nct6775_write_value(data, data->REG_PWM_MODE[nr], reg);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;
	int pwm;

	/*
	 * For automatic fan control modes, show current pwm readings.
	 * Otherwise, show the configured value.
	 */
	if (index == 0 && data->pwm_enable[nr] > manual)
		pwm = nct6775_read_value(data, data->REG_PWM_READ[nr]);
	else
		pwm = data->pwm[index][nr];

	return sprintf(buf, "%d\n", pwm);
}

static ssize_t
store_pwm(struct device *dev, struct device_attribute *attr, const char *buf,
	  size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;
	unsigned long val;
	int minval[7] = { 0, 1, 1, data->pwm[2][nr], 0, 0, 0 };
	int maxval[7]
	  = { 255, 255, data->pwm[3][nr] ? : 255, 255, 255, 255, 255 };
	int err;
	u8 reg;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	val = clamp_val(val, minval[index], maxval[index]);

	mutex_lock(&data->update_lock);
	data->pwm[index][nr] = val;
	nct6775_write_value(data, data->REG_PWM[index][nr], val);
	if (index == 2)	{ /* floor: disable if val == 0 */
		reg = nct6775_read_value(data, data->REG_TEMP_SEL[nr]);
		reg &= 0x7f;
		if (val)
			reg |= 0x80;
		nct6775_write_value(data, data->REG_TEMP_SEL[nr], reg);
	}
	mutex_unlock(&data->update_lock);
	return count;
}

/* Returns 0 if OK, -EINVAL otherwise */
static int check_trip_points(struct nct6775_data *data, int nr)
{
	int i;

	for (i = 0; i < data->auto_pwm_num - 1; i++) {
		if (data->auto_temp[nr][i] > data->auto_temp[nr][i + 1])
			return -EINVAL;
	}
	for (i = 0; i < data->auto_pwm_num - 1; i++) {
		if (data->auto_pwm[nr][i] > data->auto_pwm[nr][i + 1])
			return -EINVAL;
	}
	/* validate critical temperature and pwm if enabled (pwm > 0) */
	if (data->auto_pwm[nr][data->auto_pwm_num]) {
		if (data->auto_temp[nr][data->auto_pwm_num - 1] >
				data->auto_temp[nr][data->auto_pwm_num] ||
		    data->auto_pwm[nr][data->auto_pwm_num - 1] >
				data->auto_pwm[nr][data->auto_pwm_num])
			return -EINVAL;
	}
	return 0;
}

static void pwm_update_registers(struct nct6775_data *data, int nr)
{
	u8 reg;

	switch (data->pwm_enable[nr]) {
	case off:
	case manual:
		break;
	case speed_cruise:
		reg = nct6775_read_value(data, data->REG_FAN_MODE[nr]);
		reg = (reg & ~data->tolerance_mask) |
		  (data->target_speed_tolerance[nr] & data->tolerance_mask);
		nct6775_write_value(data, data->REG_FAN_MODE[nr], reg);
		nct6775_write_value(data, data->REG_TARGET[nr],
				    data->target_speed[nr] & 0xff);
		if (data->REG_TOLERANCE_H) {
			reg = (data->target_speed[nr] >> 8) & 0x0f;
			reg |= (data->target_speed_tolerance[nr] & 0x38) << 1;
			nct6775_write_value(data,
					    data->REG_TOLERANCE_H[nr],
					    reg);
		}
		break;
	case thermal_cruise:
		nct6775_write_value(data, data->REG_TARGET[nr],
				    data->target_temp[nr]);
		/* intentional */
	default:
		reg = nct6775_read_value(data, data->REG_FAN_MODE[nr]);
		reg = (reg & ~data->tolerance_mask) |
		  data->temp_tolerance[0][nr];
		nct6775_write_value(data, data->REG_FAN_MODE[nr], reg);
		break;
	}
}

static ssize_t
show_pwm_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	return sprintf(buf, "%d\n", data->pwm_enable[sattr->index]);
}

static ssize_t
store_pwm_enable(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	unsigned long val;
	int err;
	u16 reg;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (val > sf4)
		return -EINVAL;

	if (val == sf3 && data->kind != nct6775)
		return -EINVAL;

	if (val == sf4 && check_trip_points(data, nr)) {
		dev_err(dev, "Inconsistent trip points, not switching to SmartFan IV mode\n");
		dev_err(dev, "Adjust trip points and try again\n");
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	data->pwm_enable[nr] = val;
	if (val == off) {
		/*
		 * turn off pwm control: select manual mode, set pwm to maximum
		 */
		data->pwm[0][nr] = 255;
		nct6775_write_value(data, data->REG_PWM[0][nr], 255);
	}
	pwm_update_registers(data, nr);
	reg = nct6775_read_value(data, data->REG_FAN_MODE[nr]);
	reg &= 0x0f;
	reg |= pwm_enable_to_reg(val) << 4;
	nct6775_write_value(data, data->REG_FAN_MODE[nr], reg);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_pwm_temp_sel_common(struct nct6775_data *data, char *buf, int src)
{
	int i, sel = 0;

	for (i = 0; i < NUM_TEMP; i++) {
		if (!(data->have_temp & (1 << i)))
			continue;
		if (src == data->temp_src[i]) {
			sel = i + 1;
			break;
		}
	}

	return sprintf(buf, "%d\n", sel);
}

static ssize_t
show_pwm_temp_sel(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int index = sattr->index;

	return show_pwm_temp_sel_common(data, buf, data->pwm_temp_sel[index]);
}

static ssize_t
store_pwm_temp_sel(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	unsigned long val;
	int err, reg, src;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	if (val == 0 || val > NUM_TEMP)
		return -EINVAL;
	if (!(data->have_temp & (1 << (val - 1))) || !data->temp_src[val - 1])
		return -EINVAL;

	mutex_lock(&data->update_lock);
	src = data->temp_src[val - 1];
	data->pwm_temp_sel[nr] = src;
	reg = nct6775_read_value(data, data->REG_TEMP_SEL[nr]);
	reg &= 0xe0;
	reg |= src;
	nct6775_write_value(data, data->REG_TEMP_SEL[nr], reg);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_pwm_weight_temp_sel(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int index = sattr->index;

	return show_pwm_temp_sel_common(data, buf,
					data->pwm_weight_temp_sel[index]);
}

static ssize_t
store_pwm_weight_temp_sel(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	unsigned long val;
	int err, reg, src;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	if (val > NUM_TEMP)
		return -EINVAL;
	if (val && (!(data->have_temp & (1 << (val - 1))) ||
		    !data->temp_src[val - 1]))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	if (val) {
		src = data->temp_src[val - 1];
		data->pwm_weight_temp_sel[nr] = src;
		reg = nct6775_read_value(data, data->REG_WEIGHT_TEMP_SEL[nr]);
		reg &= 0xe0;
		reg |= (src | 0x80);
		nct6775_write_value(data, data->REG_WEIGHT_TEMP_SEL[nr], reg);
	} else {
		data->pwm_weight_temp_sel[nr] = 0;
		reg = nct6775_read_value(data, data->REG_WEIGHT_TEMP_SEL[nr]);
		reg &= 0x7f;
		nct6775_write_value(data, data->REG_WEIGHT_TEMP_SEL[nr], reg);
	}
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_target_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	return sprintf(buf, "%d\n", data->target_temp[sattr->index] * 1000);
}

static ssize_t
store_target_temp(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), 0,
			data->target_temp_mask);

	mutex_lock(&data->update_lock);
	data->target_temp[nr] = val;
	pwm_update_registers(data, nr);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_target_speed(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;

	return sprintf(buf, "%d\n",
		       fan_from_reg16(data->target_speed[nr],
				      data->fan_div[nr]));
}

static ssize_t
store_target_speed(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	unsigned long val;
	int err;
	u16 speed;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	val = clamp_val(val, 0, 1350000U);
	speed = fan_to_reg(val, data->fan_div[nr]);

	mutex_lock(&data->update_lock);
	data->target_speed[nr] = speed;
	pwm_update_registers(data, nr);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_temp_tolerance(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;

	return sprintf(buf, "%d\n", data->temp_tolerance[index][nr] * 1000);
}

static ssize_t
store_temp_tolerance(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	/* Limit tolerance as needed */
	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), 0, data->tolerance_mask);

	mutex_lock(&data->update_lock);
	data->temp_tolerance[index][nr] = val;
	if (index)
		pwm_update_registers(data, nr);
	else
		nct6775_write_value(data,
				    data->REG_CRITICAL_TEMP_TOLERANCE[nr],
				    val);
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * Fan speed tolerance is a tricky beast, since the associated register is
 * a tick counter, but the value is reported and configured as rpm.
 * Compute resulting low and high rpm values and report the difference.
 */
static ssize_t
show_speed_tolerance(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	int low = data->target_speed[nr] - data->target_speed_tolerance[nr];
	int high = data->target_speed[nr] + data->target_speed_tolerance[nr];
	int tolerance;

	if (low <= 0)
		low = 1;
	if (high > 0xffff)
		high = 0xffff;
	if (high < low)
		high = low;

	tolerance = (fan_from_reg16(low, data->fan_div[nr])
		     - fan_from_reg16(high, data->fan_div[nr])) / 2;

	return sprintf(buf, "%d\n", tolerance);
}

static ssize_t
store_speed_tolerance(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	unsigned long val;
	int err;
	int low, high;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	high = fan_from_reg16(data->target_speed[nr],
			      data->fan_div[nr]) + val;
	low = fan_from_reg16(data->target_speed[nr],
			     data->fan_div[nr]) - val;
	if (low <= 0)
		low = 1;
	if (high < low)
		high = low;

	val = (fan_to_reg(low, data->fan_div[nr]) -
	       fan_to_reg(high, data->fan_div[nr])) / 2;

	/* Limit tolerance as needed */
	val = clamp_val(val, 0, data->speed_tolerance_limit);

	mutex_lock(&data->update_lock);
	data->target_speed_tolerance[nr] = val;
	pwm_update_registers(data, nr);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_2(pwm1, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 0, 0);
static SENSOR_DEVICE_ATTR_2(pwm2, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 1, 0);
static SENSOR_DEVICE_ATTR_2(pwm3, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 2, 0);
static SENSOR_DEVICE_ATTR_2(pwm4, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 3, 0);
static SENSOR_DEVICE_ATTR_2(pwm5, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 4, 0);

static SENSOR_DEVICE_ATTR(pwm1_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
			  store_pwm_mode, 0);
static SENSOR_DEVICE_ATTR(pwm2_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
			  store_pwm_mode, 1);
static SENSOR_DEVICE_ATTR(pwm3_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
			  store_pwm_mode, 2);
static SENSOR_DEVICE_ATTR(pwm4_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
			  store_pwm_mode, 3);
static SENSOR_DEVICE_ATTR(pwm5_mode, S_IWUSR | S_IRUGO, show_pwm_mode,
			  store_pwm_mode, 4);

static SENSOR_DEVICE_ATTR(pwm1_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
			  store_pwm_enable, 0);
static SENSOR_DEVICE_ATTR(pwm2_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
			  store_pwm_enable, 1);
static SENSOR_DEVICE_ATTR(pwm3_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
			  store_pwm_enable, 2);
static SENSOR_DEVICE_ATTR(pwm4_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
			  store_pwm_enable, 3);
static SENSOR_DEVICE_ATTR(pwm5_enable, S_IWUSR | S_IRUGO, show_pwm_enable,
			  store_pwm_enable, 4);

static SENSOR_DEVICE_ATTR(pwm1_temp_sel, S_IWUSR | S_IRUGO,
			    show_pwm_temp_sel, store_pwm_temp_sel, 0);
static SENSOR_DEVICE_ATTR(pwm2_temp_sel, S_IWUSR | S_IRUGO,
			    show_pwm_temp_sel, store_pwm_temp_sel, 1);
static SENSOR_DEVICE_ATTR(pwm3_temp_sel, S_IWUSR | S_IRUGO,
			    show_pwm_temp_sel, store_pwm_temp_sel, 2);
static SENSOR_DEVICE_ATTR(pwm4_temp_sel, S_IWUSR | S_IRUGO,
			    show_pwm_temp_sel, store_pwm_temp_sel, 3);
static SENSOR_DEVICE_ATTR(pwm5_temp_sel, S_IWUSR | S_IRUGO,
			    show_pwm_temp_sel, store_pwm_temp_sel, 4);

static SENSOR_DEVICE_ATTR(pwm1_target_temp, S_IWUSR | S_IRUGO, show_target_temp,
			  store_target_temp, 0);
static SENSOR_DEVICE_ATTR(pwm2_target_temp, S_IWUSR | S_IRUGO, show_target_temp,
			  store_target_temp, 1);
static SENSOR_DEVICE_ATTR(pwm3_target_temp, S_IWUSR | S_IRUGO, show_target_temp,
			  store_target_temp, 2);
static SENSOR_DEVICE_ATTR(pwm4_target_temp, S_IWUSR | S_IRUGO, show_target_temp,
			  store_target_temp, 3);
static SENSOR_DEVICE_ATTR(pwm5_target_temp, S_IWUSR | S_IRUGO, show_target_temp,
			  store_target_temp, 4);

static SENSOR_DEVICE_ATTR(fan1_target, S_IWUSR | S_IRUGO, show_target_speed,
			  store_target_speed, 0);
static SENSOR_DEVICE_ATTR(fan2_target, S_IWUSR | S_IRUGO, show_target_speed,
			  store_target_speed, 1);
static SENSOR_DEVICE_ATTR(fan3_target, S_IWUSR | S_IRUGO, show_target_speed,
			  store_target_speed, 2);
static SENSOR_DEVICE_ATTR(fan4_target, S_IWUSR | S_IRUGO, show_target_speed,
			  store_target_speed, 3);
static SENSOR_DEVICE_ATTR(fan5_target, S_IWUSR | S_IRUGO, show_target_speed,
			  store_target_speed, 4);

static SENSOR_DEVICE_ATTR(fan1_tolerance, S_IWUSR | S_IRUGO,
			    show_speed_tolerance, store_speed_tolerance, 0);
static SENSOR_DEVICE_ATTR(fan2_tolerance, S_IWUSR | S_IRUGO,
			    show_speed_tolerance, store_speed_tolerance, 1);
static SENSOR_DEVICE_ATTR(fan3_tolerance, S_IWUSR | S_IRUGO,
			    show_speed_tolerance, store_speed_tolerance, 2);
static SENSOR_DEVICE_ATTR(fan4_tolerance, S_IWUSR | S_IRUGO,
			    show_speed_tolerance, store_speed_tolerance, 3);
static SENSOR_DEVICE_ATTR(fan5_tolerance, S_IWUSR | S_IRUGO,
			    show_speed_tolerance, store_speed_tolerance, 4);

/* Smart Fan registers */

static ssize_t
show_weight_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;

	return sprintf(buf, "%d\n", data->weight_temp[index][nr] * 1000);
}

static ssize_t
store_weight_temp(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	val = clamp_val(DIV_ROUND_CLOSEST(val, 1000), 0, 255);

	mutex_lock(&data->update_lock);
	data->weight_temp[index][nr] = val;
	nct6775_write_value(data, data->REG_WEIGHT_TEMP[index][nr], val);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(pwm1_weight_temp_sel, S_IWUSR | S_IRUGO,
			    show_pwm_weight_temp_sel, store_pwm_weight_temp_sel,
			    0);
static SENSOR_DEVICE_ATTR(pwm2_weight_temp_sel, S_IWUSR | S_IRUGO,
			    show_pwm_weight_temp_sel, store_pwm_weight_temp_sel,
			    1);
static SENSOR_DEVICE_ATTR(pwm3_weight_temp_sel, S_IWUSR | S_IRUGO,
			    show_pwm_weight_temp_sel, store_pwm_weight_temp_sel,
			    2);
static SENSOR_DEVICE_ATTR(pwm4_weight_temp_sel, S_IWUSR | S_IRUGO,
			    show_pwm_weight_temp_sel, store_pwm_weight_temp_sel,
			    3);
static SENSOR_DEVICE_ATTR(pwm5_weight_temp_sel, S_IWUSR | S_IRUGO,
			    show_pwm_weight_temp_sel, store_pwm_weight_temp_sel,
			    4);

static SENSOR_DEVICE_ATTR_2(pwm1_weight_temp_step, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 0, 0);
static SENSOR_DEVICE_ATTR_2(pwm2_weight_temp_step, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 1, 0);
static SENSOR_DEVICE_ATTR_2(pwm3_weight_temp_step, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 2, 0);
static SENSOR_DEVICE_ATTR_2(pwm4_weight_temp_step, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 3, 0);
static SENSOR_DEVICE_ATTR_2(pwm5_weight_temp_step, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 4, 0);

static SENSOR_DEVICE_ATTR_2(pwm1_weight_temp_step_tol, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 0, 1);
static SENSOR_DEVICE_ATTR_2(pwm2_weight_temp_step_tol, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 1, 1);
static SENSOR_DEVICE_ATTR_2(pwm3_weight_temp_step_tol, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 2, 1);
static SENSOR_DEVICE_ATTR_2(pwm4_weight_temp_step_tol, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 3, 1);
static SENSOR_DEVICE_ATTR_2(pwm5_weight_temp_step_tol, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 4, 1);

static SENSOR_DEVICE_ATTR_2(pwm1_weight_temp_step_base, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 0, 2);
static SENSOR_DEVICE_ATTR_2(pwm2_weight_temp_step_base, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 1, 2);
static SENSOR_DEVICE_ATTR_2(pwm3_weight_temp_step_base, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 2, 2);
static SENSOR_DEVICE_ATTR_2(pwm4_weight_temp_step_base, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 3, 2);
static SENSOR_DEVICE_ATTR_2(pwm5_weight_temp_step_base, S_IWUSR | S_IRUGO,
			    show_weight_temp, store_weight_temp, 4, 2);

static SENSOR_DEVICE_ATTR_2(pwm1_weight_duty_step, S_IWUSR | S_IRUGO,
			    show_pwm, store_pwm, 0, 5);
static SENSOR_DEVICE_ATTR_2(pwm2_weight_duty_step, S_IWUSR | S_IRUGO,
			    show_pwm, store_pwm, 1, 5);
static SENSOR_DEVICE_ATTR_2(pwm3_weight_duty_step, S_IWUSR | S_IRUGO,
			    show_pwm, store_pwm, 2, 5);
static SENSOR_DEVICE_ATTR_2(pwm4_weight_duty_step, S_IWUSR | S_IRUGO,
			    show_pwm, store_pwm, 3, 5);
static SENSOR_DEVICE_ATTR_2(pwm5_weight_duty_step, S_IWUSR | S_IRUGO,
			    show_pwm, store_pwm, 4, 5);

/* duty_base is not supported on all chips */
static struct sensor_device_attribute_2 sda_weight_duty_base[] = {
	SENSOR_ATTR_2(pwm1_weight_duty_base, S_IWUSR | S_IRUGO,
		      show_pwm, store_pwm, 0, 6),
	SENSOR_ATTR_2(pwm2_weight_duty_base, S_IWUSR | S_IRUGO,
		      show_pwm, store_pwm, 1, 6),
	SENSOR_ATTR_2(pwm3_weight_duty_base, S_IWUSR | S_IRUGO,
		      show_pwm, store_pwm, 2, 6),
	SENSOR_ATTR_2(pwm4_weight_duty_base, S_IWUSR | S_IRUGO,
		      show_pwm, store_pwm, 3, 6),
	SENSOR_ATTR_2(pwm5_weight_duty_base, S_IWUSR | S_IRUGO,
		      show_pwm, store_pwm, 4, 6),
};

static ssize_t
show_fan_time(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;

	return sprintf(buf, "%d\n",
		       step_time_from_reg(data->fan_time[index][nr],
					  data->pwm_mode[nr]));
}

static ssize_t
store_fan_time(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	val = step_time_to_reg(val, data->pwm_mode[nr]);
	mutex_lock(&data->update_lock);
	data->fan_time[index][nr] = val;
	nct6775_write_value(data, data->REG_FAN_TIME[index][nr], val);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}

static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static SENSOR_DEVICE_ATTR_2(pwm1_stop_time, S_IWUSR | S_IRUGO, show_fan_time,
			    store_fan_time, 0, 0);
static SENSOR_DEVICE_ATTR_2(pwm2_stop_time, S_IWUSR | S_IRUGO, show_fan_time,
			    store_fan_time, 1, 0);
static SENSOR_DEVICE_ATTR_2(pwm3_stop_time, S_IWUSR | S_IRUGO, show_fan_time,
			    store_fan_time, 2, 0);
static SENSOR_DEVICE_ATTR_2(pwm4_stop_time, S_IWUSR | S_IRUGO, show_fan_time,
			    store_fan_time, 3, 0);
static SENSOR_DEVICE_ATTR_2(pwm5_stop_time, S_IWUSR | S_IRUGO, show_fan_time,
			    store_fan_time, 4, 0);

static SENSOR_DEVICE_ATTR_2(pwm1_step_up_time, S_IWUSR | S_IRUGO, show_fan_time,
			    store_fan_time, 0, 1);
static SENSOR_DEVICE_ATTR_2(pwm2_step_up_time, S_IWUSR | S_IRUGO, show_fan_time,
			    store_fan_time, 1, 1);
static SENSOR_DEVICE_ATTR_2(pwm3_step_up_time, S_IWUSR | S_IRUGO, show_fan_time,
			    store_fan_time, 2, 1);
static SENSOR_DEVICE_ATTR_2(pwm4_step_up_time, S_IWUSR | S_IRUGO, show_fan_time,
			    store_fan_time, 3, 1);
static SENSOR_DEVICE_ATTR_2(pwm5_step_up_time, S_IWUSR | S_IRUGO, show_fan_time,
			    store_fan_time, 4, 1);

static SENSOR_DEVICE_ATTR_2(pwm1_step_down_time, S_IWUSR | S_IRUGO,
			    show_fan_time, store_fan_time, 0, 2);
static SENSOR_DEVICE_ATTR_2(pwm2_step_down_time, S_IWUSR | S_IRUGO,
			    show_fan_time, store_fan_time, 1, 2);
static SENSOR_DEVICE_ATTR_2(pwm3_step_down_time, S_IWUSR | S_IRUGO,
			    show_fan_time, store_fan_time, 2, 2);
static SENSOR_DEVICE_ATTR_2(pwm4_step_down_time, S_IWUSR | S_IRUGO,
			    show_fan_time, store_fan_time, 3, 2);
static SENSOR_DEVICE_ATTR_2(pwm5_step_down_time, S_IWUSR | S_IRUGO,
			    show_fan_time, store_fan_time, 4, 2);

static SENSOR_DEVICE_ATTR_2(pwm1_start, S_IWUSR | S_IRUGO, show_pwm,
			    store_pwm, 0, 1);
static SENSOR_DEVICE_ATTR_2(pwm2_start, S_IWUSR | S_IRUGO, show_pwm,
			    store_pwm, 1, 1);
static SENSOR_DEVICE_ATTR_2(pwm3_start, S_IWUSR | S_IRUGO, show_pwm,
			    store_pwm, 2, 1);
static SENSOR_DEVICE_ATTR_2(pwm4_start, S_IWUSR | S_IRUGO, show_pwm,
			    store_pwm, 3, 1);
static SENSOR_DEVICE_ATTR_2(pwm5_start, S_IWUSR | S_IRUGO, show_pwm,
			    store_pwm, 4, 1);

static SENSOR_DEVICE_ATTR_2(pwm1_floor, S_IWUSR | S_IRUGO, show_pwm,
			    store_pwm, 0, 2);
static SENSOR_DEVICE_ATTR_2(pwm2_floor, S_IWUSR | S_IRUGO, show_pwm,
			    store_pwm, 1, 2);
static SENSOR_DEVICE_ATTR_2(pwm3_floor, S_IWUSR | S_IRUGO, show_pwm,
			    store_pwm, 2, 2);
static SENSOR_DEVICE_ATTR_2(pwm4_floor, S_IWUSR | S_IRUGO, show_pwm,
			    store_pwm, 3, 2);
static SENSOR_DEVICE_ATTR_2(pwm5_floor, S_IWUSR | S_IRUGO, show_pwm,
			    store_pwm, 4, 2);

static SENSOR_DEVICE_ATTR_2(pwm1_temp_tolerance, S_IWUSR | S_IRUGO,
			    show_temp_tolerance, store_temp_tolerance, 0, 0);
static SENSOR_DEVICE_ATTR_2(pwm2_temp_tolerance, S_IWUSR | S_IRUGO,
			    show_temp_tolerance, store_temp_tolerance, 1, 0);
static SENSOR_DEVICE_ATTR_2(pwm3_temp_tolerance, S_IWUSR | S_IRUGO,
			    show_temp_tolerance, store_temp_tolerance, 2, 0);
static SENSOR_DEVICE_ATTR_2(pwm4_temp_tolerance, S_IWUSR | S_IRUGO,
			    show_temp_tolerance, store_temp_tolerance, 3, 0);
static SENSOR_DEVICE_ATTR_2(pwm5_temp_tolerance, S_IWUSR | S_IRUGO,
			    show_temp_tolerance, store_temp_tolerance, 4, 0);

static SENSOR_DEVICE_ATTR_2(pwm1_crit_temp_tolerance, S_IWUSR | S_IRUGO,
			    show_temp_tolerance, store_temp_tolerance, 0, 1);
static SENSOR_DEVICE_ATTR_2(pwm2_crit_temp_tolerance, S_IWUSR | S_IRUGO,
			    show_temp_tolerance, store_temp_tolerance, 1, 1);
static SENSOR_DEVICE_ATTR_2(pwm3_crit_temp_tolerance, S_IWUSR | S_IRUGO,
			    show_temp_tolerance, store_temp_tolerance, 2, 1);
static SENSOR_DEVICE_ATTR_2(pwm4_crit_temp_tolerance, S_IWUSR | S_IRUGO,
			    show_temp_tolerance, store_temp_tolerance, 3, 1);
static SENSOR_DEVICE_ATTR_2(pwm5_crit_temp_tolerance, S_IWUSR | S_IRUGO,
			    show_temp_tolerance, store_temp_tolerance, 4, 1);

/* pwm_max is not supported on all chips */
static struct sensor_device_attribute_2 sda_pwm_max[] = {
	SENSOR_ATTR_2(pwm1_max, S_IWUSR | S_IRUGO, show_pwm, store_pwm,
		      0, 3),
	SENSOR_ATTR_2(pwm2_max, S_IWUSR | S_IRUGO, show_pwm, store_pwm,
		      1, 3),
	SENSOR_ATTR_2(pwm3_max, S_IWUSR | S_IRUGO, show_pwm, store_pwm,
		      2, 3),
	SENSOR_ATTR_2(pwm4_max, S_IWUSR | S_IRUGO, show_pwm, store_pwm,
		      3, 3),
	SENSOR_ATTR_2(pwm5_max, S_IWUSR | S_IRUGO, show_pwm, store_pwm,
		      4, 3),
};

/* pwm_step is not supported on all chips */
static struct sensor_device_attribute_2 sda_pwm_step[] = {
	SENSOR_ATTR_2(pwm1_step, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 0, 4),
	SENSOR_ATTR_2(pwm2_step, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 1, 4),
	SENSOR_ATTR_2(pwm3_step, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 2, 4),
	SENSOR_ATTR_2(pwm4_step, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 3, 4),
	SENSOR_ATTR_2(pwm5_step, S_IWUSR | S_IRUGO, show_pwm, store_pwm, 4, 4),
};

static struct attribute *nct6775_attributes_pwm[5][20] = {
	{
		&sensor_dev_attr_pwm1.dev_attr.attr,
		&sensor_dev_attr_pwm1_mode.dev_attr.attr,
		&sensor_dev_attr_pwm1_enable.dev_attr.attr,
		&sensor_dev_attr_pwm1_temp_sel.dev_attr.attr,
		&sensor_dev_attr_pwm1_temp_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm1_crit_temp_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm1_target_temp.dev_attr.attr,
		&sensor_dev_attr_fan1_target.dev_attr.attr,
		&sensor_dev_attr_fan1_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm1_stop_time.dev_attr.attr,
		&sensor_dev_attr_pwm1_step_up_time.dev_attr.attr,
		&sensor_dev_attr_pwm1_step_down_time.dev_attr.attr,
		&sensor_dev_attr_pwm1_start.dev_attr.attr,
		&sensor_dev_attr_pwm1_floor.dev_attr.attr,
		&sensor_dev_attr_pwm1_weight_temp_sel.dev_attr.attr,
		&sensor_dev_attr_pwm1_weight_temp_step.dev_attr.attr,
		&sensor_dev_attr_pwm1_weight_temp_step_tol.dev_attr.attr,
		&sensor_dev_attr_pwm1_weight_temp_step_base.dev_attr.attr,
		&sensor_dev_attr_pwm1_weight_duty_step.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_pwm2.dev_attr.attr,
		&sensor_dev_attr_pwm2_mode.dev_attr.attr,
		&sensor_dev_attr_pwm2_enable.dev_attr.attr,
		&sensor_dev_attr_pwm2_temp_sel.dev_attr.attr,
		&sensor_dev_attr_pwm2_temp_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm2_crit_temp_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm2_target_temp.dev_attr.attr,
		&sensor_dev_attr_fan2_target.dev_attr.attr,
		&sensor_dev_attr_fan2_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm2_stop_time.dev_attr.attr,
		&sensor_dev_attr_pwm2_step_up_time.dev_attr.attr,
		&sensor_dev_attr_pwm2_step_down_time.dev_attr.attr,
		&sensor_dev_attr_pwm2_start.dev_attr.attr,
		&sensor_dev_attr_pwm2_floor.dev_attr.attr,
		&sensor_dev_attr_pwm2_weight_temp_sel.dev_attr.attr,
		&sensor_dev_attr_pwm2_weight_temp_step.dev_attr.attr,
		&sensor_dev_attr_pwm2_weight_temp_step_tol.dev_attr.attr,
		&sensor_dev_attr_pwm2_weight_temp_step_base.dev_attr.attr,
		&sensor_dev_attr_pwm2_weight_duty_step.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_pwm3.dev_attr.attr,
		&sensor_dev_attr_pwm3_mode.dev_attr.attr,
		&sensor_dev_attr_pwm3_enable.dev_attr.attr,
		&sensor_dev_attr_pwm3_temp_sel.dev_attr.attr,
		&sensor_dev_attr_pwm3_temp_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm3_crit_temp_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm3_target_temp.dev_attr.attr,
		&sensor_dev_attr_fan3_target.dev_attr.attr,
		&sensor_dev_attr_fan3_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm3_stop_time.dev_attr.attr,
		&sensor_dev_attr_pwm3_step_up_time.dev_attr.attr,
		&sensor_dev_attr_pwm3_step_down_time.dev_attr.attr,
		&sensor_dev_attr_pwm3_start.dev_attr.attr,
		&sensor_dev_attr_pwm3_floor.dev_attr.attr,
		&sensor_dev_attr_pwm3_weight_temp_sel.dev_attr.attr,
		&sensor_dev_attr_pwm3_weight_temp_step.dev_attr.attr,
		&sensor_dev_attr_pwm3_weight_temp_step_tol.dev_attr.attr,
		&sensor_dev_attr_pwm3_weight_temp_step_base.dev_attr.attr,
		&sensor_dev_attr_pwm3_weight_duty_step.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_pwm4.dev_attr.attr,
		&sensor_dev_attr_pwm4_mode.dev_attr.attr,
		&sensor_dev_attr_pwm4_enable.dev_attr.attr,
		&sensor_dev_attr_pwm4_temp_sel.dev_attr.attr,
		&sensor_dev_attr_pwm4_temp_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm4_crit_temp_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm4_target_temp.dev_attr.attr,
		&sensor_dev_attr_fan4_target.dev_attr.attr,
		&sensor_dev_attr_fan4_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm4_stop_time.dev_attr.attr,
		&sensor_dev_attr_pwm4_step_up_time.dev_attr.attr,
		&sensor_dev_attr_pwm4_step_down_time.dev_attr.attr,
		&sensor_dev_attr_pwm4_start.dev_attr.attr,
		&sensor_dev_attr_pwm4_floor.dev_attr.attr,
		&sensor_dev_attr_pwm4_weight_temp_sel.dev_attr.attr,
		&sensor_dev_attr_pwm4_weight_temp_step.dev_attr.attr,
		&sensor_dev_attr_pwm4_weight_temp_step_tol.dev_attr.attr,
		&sensor_dev_attr_pwm4_weight_temp_step_base.dev_attr.attr,
		&sensor_dev_attr_pwm4_weight_duty_step.dev_attr.attr,
		NULL
	},
	{
		&sensor_dev_attr_pwm5.dev_attr.attr,
		&sensor_dev_attr_pwm5_mode.dev_attr.attr,
		&sensor_dev_attr_pwm5_enable.dev_attr.attr,
		&sensor_dev_attr_pwm5_temp_sel.dev_attr.attr,
		&sensor_dev_attr_pwm5_temp_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm5_crit_temp_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm5_target_temp.dev_attr.attr,
		&sensor_dev_attr_fan5_target.dev_attr.attr,
		&sensor_dev_attr_fan5_tolerance.dev_attr.attr,
		&sensor_dev_attr_pwm5_stop_time.dev_attr.attr,
		&sensor_dev_attr_pwm5_step_up_time.dev_attr.attr,
		&sensor_dev_attr_pwm5_step_down_time.dev_attr.attr,
		&sensor_dev_attr_pwm5_start.dev_attr.attr,
		&sensor_dev_attr_pwm5_floor.dev_attr.attr,
		&sensor_dev_attr_pwm5_weight_temp_sel.dev_attr.attr,
		&sensor_dev_attr_pwm5_weight_temp_step.dev_attr.attr,
		&sensor_dev_attr_pwm5_weight_temp_step_tol.dev_attr.attr,
		&sensor_dev_attr_pwm5_weight_temp_step_base.dev_attr.attr,
		&sensor_dev_attr_pwm5_weight_duty_step.dev_attr.attr,
		NULL
	},
};

static const struct attribute_group nct6775_group_pwm[5] = {
	{ .attrs = nct6775_attributes_pwm[0] },
	{ .attrs = nct6775_attributes_pwm[1] },
	{ .attrs = nct6775_attributes_pwm[2] },
	{ .attrs = nct6775_attributes_pwm[3] },
	{ .attrs = nct6775_attributes_pwm[4] },
};

static ssize_t
show_auto_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	return sprintf(buf, "%d\n", data->auto_pwm[sattr->nr][sattr->index]);
}

static ssize_t
store_auto_pwm(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int point = sattr->index;
	unsigned long val;
	int err;
	u8 reg;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	if (val > 255)
		return -EINVAL;

	if (point == data->auto_pwm_num) {
		if (data->kind != nct6775 && !val)
			return -EINVAL;
		if (data->kind != nct6779 && val)
			val = 0xff;
	}

	mutex_lock(&data->update_lock);
	data->auto_pwm[nr][point] = val;
	if (point < data->auto_pwm_num) {
		nct6775_write_value(data,
				    NCT6775_AUTO_PWM(data, nr, point),
				    data->auto_pwm[nr][point]);
	} else {
		switch (data->kind) {
		case nct6775:
			/* disable if needed (pwm == 0) */
			reg = nct6775_read_value(data,
						 NCT6775_REG_CRITICAL_ENAB[nr]);
			if (val)
				reg |= 0x02;
			else
				reg &= ~0x02;
			nct6775_write_value(data, NCT6775_REG_CRITICAL_ENAB[nr],
					    reg);
			break;
		case nct6776:
			break; /* always enabled, nothing to do */
		case nct6779:
			nct6775_write_value(data, NCT6779_REG_CRITICAL_PWM[nr],
					    val);
			reg = nct6775_read_value(data,
					NCT6779_REG_CRITICAL_PWM_ENABLE[nr]);
			if (val == 255)
				reg &= ~0x01;
			else
				reg |= 0x01;
			nct6775_write_value(data,
					    NCT6779_REG_CRITICAL_PWM_ENABLE[nr],
					    reg);
			break;
		}
	}
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_auto_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int point = sattr->index;

	/*
	 * We don't know for sure if the temperature is signed or unsigned.
	 * Assume it is unsigned.
	 */
	return sprintf(buf, "%d\n", data->auto_temp[nr][point] * 1000);
}

static ssize_t
store_auto_temp(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int point = sattr->index;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;
	if (val > 255000)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->auto_temp[nr][point] = DIV_ROUND_CLOSEST(val, 1000);
	if (point < data->auto_pwm_num) {
		nct6775_write_value(data,
				    NCT6775_AUTO_TEMP(data, nr, point),
				    data->auto_temp[nr][point]);
	} else {
		nct6775_write_value(data, data->REG_CRITICAL_TEMP[nr],
				    data->auto_temp[nr][point]);
	}
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * The number of auto-point trip points is chip dependent.
 * Need to check support while generating/removing attribute files.
 */
static struct sensor_device_attribute_2 sda_auto_pwm_arrays[] = {
	SENSOR_ATTR_2(pwm1_auto_point1_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 0, 0),
	SENSOR_ATTR_2(pwm1_auto_point1_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 0, 0),
	SENSOR_ATTR_2(pwm1_auto_point2_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 0, 1),
	SENSOR_ATTR_2(pwm1_auto_point2_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 0, 1),
	SENSOR_ATTR_2(pwm1_auto_point3_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 0, 2),
	SENSOR_ATTR_2(pwm1_auto_point3_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 0, 2),
	SENSOR_ATTR_2(pwm1_auto_point4_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 0, 3),
	SENSOR_ATTR_2(pwm1_auto_point4_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 0, 3),
	SENSOR_ATTR_2(pwm1_auto_point5_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 0, 4),
	SENSOR_ATTR_2(pwm1_auto_point5_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 0, 4),
	SENSOR_ATTR_2(pwm1_auto_point6_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 0, 5),
	SENSOR_ATTR_2(pwm1_auto_point6_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 0, 5),
	SENSOR_ATTR_2(pwm1_auto_point7_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 0, 6),
	SENSOR_ATTR_2(pwm1_auto_point7_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 0, 6),

	SENSOR_ATTR_2(pwm2_auto_point1_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 1, 0),
	SENSOR_ATTR_2(pwm2_auto_point1_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 1, 0),
	SENSOR_ATTR_2(pwm2_auto_point2_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 1, 1),
	SENSOR_ATTR_2(pwm2_auto_point2_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 1, 1),
	SENSOR_ATTR_2(pwm2_auto_point3_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 1, 2),
	SENSOR_ATTR_2(pwm2_auto_point3_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 1, 2),
	SENSOR_ATTR_2(pwm2_auto_point4_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 1, 3),
	SENSOR_ATTR_2(pwm2_auto_point4_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 1, 3),
	SENSOR_ATTR_2(pwm2_auto_point5_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 1, 4),
	SENSOR_ATTR_2(pwm2_auto_point5_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 1, 4),
	SENSOR_ATTR_2(pwm2_auto_point6_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 1, 5),
	SENSOR_ATTR_2(pwm2_auto_point6_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 1, 5),
	SENSOR_ATTR_2(pwm2_auto_point7_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 1, 6),
	SENSOR_ATTR_2(pwm2_auto_point7_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 1, 6),

	SENSOR_ATTR_2(pwm3_auto_point1_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 2, 0),
	SENSOR_ATTR_2(pwm3_auto_point1_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 2, 0),
	SENSOR_ATTR_2(pwm3_auto_point2_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 2, 1),
	SENSOR_ATTR_2(pwm3_auto_point2_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 2, 1),
	SENSOR_ATTR_2(pwm3_auto_point3_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 2, 2),
	SENSOR_ATTR_2(pwm3_auto_point3_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 2, 2),
	SENSOR_ATTR_2(pwm3_auto_point4_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 2, 3),
	SENSOR_ATTR_2(pwm3_auto_point4_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 2, 3),
	SENSOR_ATTR_2(pwm3_auto_point5_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 2, 4),
	SENSOR_ATTR_2(pwm3_auto_point5_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 2, 4),
	SENSOR_ATTR_2(pwm3_auto_point6_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 2, 5),
	SENSOR_ATTR_2(pwm3_auto_point6_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 2, 5),
	SENSOR_ATTR_2(pwm3_auto_point7_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 2, 6),
	SENSOR_ATTR_2(pwm3_auto_point7_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 2, 6),

	SENSOR_ATTR_2(pwm4_auto_point1_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 3, 0),
	SENSOR_ATTR_2(pwm4_auto_point1_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 3, 0),
	SENSOR_ATTR_2(pwm4_auto_point2_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 3, 1),
	SENSOR_ATTR_2(pwm4_auto_point2_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 3, 1),
	SENSOR_ATTR_2(pwm4_auto_point3_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 3, 2),
	SENSOR_ATTR_2(pwm4_auto_point3_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 3, 2),
	SENSOR_ATTR_2(pwm4_auto_point4_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 3, 3),
	SENSOR_ATTR_2(pwm4_auto_point4_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 3, 3),
	SENSOR_ATTR_2(pwm4_auto_point5_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 3, 4),
	SENSOR_ATTR_2(pwm4_auto_point5_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 3, 4),
	SENSOR_ATTR_2(pwm4_auto_point6_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 3, 5),
	SENSOR_ATTR_2(pwm4_auto_point6_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 3, 5),
	SENSOR_ATTR_2(pwm4_auto_point7_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 3, 6),
	SENSOR_ATTR_2(pwm4_auto_point7_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 3, 6),

	SENSOR_ATTR_2(pwm5_auto_point1_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 4, 0),
	SENSOR_ATTR_2(pwm5_auto_point1_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 4, 0),
	SENSOR_ATTR_2(pwm5_auto_point2_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 4, 1),
	SENSOR_ATTR_2(pwm5_auto_point2_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 4, 1),
	SENSOR_ATTR_2(pwm5_auto_point3_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 4, 2),
	SENSOR_ATTR_2(pwm5_auto_point3_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 4, 2),
	SENSOR_ATTR_2(pwm5_auto_point4_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 4, 3),
	SENSOR_ATTR_2(pwm5_auto_point4_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 4, 3),
	SENSOR_ATTR_2(pwm5_auto_point5_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 4, 4),
	SENSOR_ATTR_2(pwm5_auto_point5_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 4, 4),
	SENSOR_ATTR_2(pwm5_auto_point6_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 4, 5),
	SENSOR_ATTR_2(pwm5_auto_point6_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 4, 5),
	SENSOR_ATTR_2(pwm5_auto_point7_pwm, S_IWUSR | S_IRUGO,
		      show_auto_pwm, store_auto_pwm, 4, 6),
	SENSOR_ATTR_2(pwm5_auto_point7_temp, S_IWUSR | S_IRUGO,
		      show_auto_temp, store_auto_temp, 4, 6),
};

static ssize_t
show_vid(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}

static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

/* Case open detection */

static ssize_t
clear_caseopen(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct nct6775_sio_data *sio_data = dev->platform_data;
	int nr = to_sensor_dev_attr(attr)->index - INTRUSION_ALARM_BASE;
	unsigned long val;
	u8 reg;
	int ret;

	if (kstrtoul(buf, 10, &val) || val != 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	/*
	 * Use CR registers to clear caseopen status.
	 * The CR registers are the same for all chips, and not all chips
	 * support clearing the caseopen status through "regular" registers.
	 */
	ret = superio_enter(sio_data->sioreg);
	if (ret) {
		count = ret;
		goto error;
	}

	superio_select(sio_data->sioreg, NCT6775_LD_ACPI);
	reg = superio_inb(sio_data->sioreg, NCT6775_REG_CR_CASEOPEN_CLR[nr]);
	reg |= NCT6775_CR_CASEOPEN_CLR_MASK[nr];
	superio_outb(sio_data->sioreg, NCT6775_REG_CR_CASEOPEN_CLR[nr], reg);
	reg &= ~NCT6775_CR_CASEOPEN_CLR_MASK[nr];
	superio_outb(sio_data->sioreg, NCT6775_REG_CR_CASEOPEN_CLR[nr], reg);
	superio_exit(sio_data->sioreg);

	data->valid = false;	/* Force cache refresh */
error:
	mutex_unlock(&data->update_lock);
	return count;
}

static struct sensor_device_attribute sda_caseopen[] = {
	SENSOR_ATTR(intrusion0_alarm, S_IWUSR | S_IRUGO, show_alarm,
		    clear_caseopen, INTRUSION_ALARM_BASE),
	SENSOR_ATTR(intrusion1_alarm, S_IWUSR | S_IRUGO, show_alarm,
		    clear_caseopen, INTRUSION_ALARM_BASE + 1),
};

/*
 * Driver and device management
 */

static void nct6775_device_remove_files(struct device *dev)
{
	/*
	 * some entries in the following arrays may not have been used in
	 * device_create_file(), but device_remove_file() will ignore them
	 */
	int i;
	struct nct6775_data *data = dev_get_drvdata(dev);

	for (i = 0; i < data->pwm_num; i++)
		sysfs_remove_group(&dev->kobj, &nct6775_group_pwm[i]);

	for (i = 0; i < ARRAY_SIZE(sda_pwm_max); i++)
		device_remove_file(dev, &sda_pwm_max[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(sda_pwm_step); i++)
		device_remove_file(dev, &sda_pwm_step[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(sda_weight_duty_base); i++)
		device_remove_file(dev, &sda_weight_duty_base[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(sda_auto_pwm_arrays); i++)
		device_remove_file(dev, &sda_auto_pwm_arrays[i].dev_attr);

	for (i = 0; i < data->in_num; i++)
		sysfs_remove_group(&dev->kobj, &nct6775_group_in[i]);

	for (i = 0; i < 5; i++) {
		device_remove_file(dev, &sda_fan_input[i].dev_attr);
		device_remove_file(dev, &sda_fan_alarm[i].dev_attr);
		device_remove_file(dev, &sda_fan_div[i].dev_attr);
		device_remove_file(dev, &sda_fan_min[i].dev_attr);
		device_remove_file(dev, &sda_fan_pulses[i].dev_attr);
	}
	for (i = 0; i < NUM_TEMP; i++) {
		if (!(data->have_temp & (1 << i)))
			continue;
		device_remove_file(dev, &sda_temp_input[i].dev_attr);
		device_remove_file(dev, &sda_temp_label[i].dev_attr);
		device_remove_file(dev, &sda_temp_max[i].dev_attr);
		device_remove_file(dev, &sda_temp_max_hyst[i].dev_attr);
		device_remove_file(dev, &sda_temp_crit[i].dev_attr);
		if (!(data->have_temp_fixed & (1 << i)))
			continue;
		device_remove_file(dev, &sda_temp_type[i].dev_attr);
		device_remove_file(dev, &sda_temp_offset[i].dev_attr);
		if (i >= NUM_TEMP_ALARM)
			continue;
		device_remove_file(dev, &sda_temp_alarm[i].dev_attr);
	}

	device_remove_file(dev, &sda_caseopen[0].dev_attr);
	device_remove_file(dev, &sda_caseopen[1].dev_attr);

	device_remove_file(dev, &dev_attr_name);
	device_remove_file(dev, &dev_attr_cpu0_vid);
}

/* Get the monitoring functions started */
static inline void nct6775_init_device(struct nct6775_data *data)
{
	int i;
	u8 tmp, diode;

	/* Start monitoring if needed */
	if (data->REG_CONFIG) {
		tmp = nct6775_read_value(data, data->REG_CONFIG);
		if (!(tmp & 0x01))
			nct6775_write_value(data, data->REG_CONFIG, tmp | 0x01);
	}

	/* Enable temperature sensors if needed */
	for (i = 0; i < NUM_TEMP; i++) {
		if (!(data->have_temp & (1 << i)))
			continue;
		if (!data->reg_temp_config[i])
			continue;
		tmp = nct6775_read_value(data, data->reg_temp_config[i]);
		if (tmp & 0x01)
			nct6775_write_value(data, data->reg_temp_config[i],
					    tmp & 0xfe);
	}

	/* Enable VBAT monitoring if needed */
	tmp = nct6775_read_value(data, data->REG_VBAT);
	if (!(tmp & 0x01))
		nct6775_write_value(data, data->REG_VBAT, tmp | 0x01);

	diode = nct6775_read_value(data, data->REG_DIODE);

	for (i = 0; i < data->temp_fixed_num; i++) {
		if (!(data->have_temp_fixed & (1 << i)))
			continue;
		if ((tmp & (0x02 << i)))	/* diode */
			data->temp_type[i] = 3 - ((diode >> i) & 0x02);
		else				/* thermistor */
			data->temp_type[i] = 4;
	}
}

static int
nct6775_check_fan_inputs(const struct nct6775_sio_data *sio_data,
			 struct nct6775_data *data)
{
	int regval;
	bool fan3pin, fan3min, fan4pin, fan4min, fan5pin;
	bool pwm3pin, pwm4pin, pwm5pin;
	int ret;

	ret = superio_enter(sio_data->sioreg);
	if (ret)
		return ret;

	/* fan4 and fan5 share some pins with the GPIO and serial flash */
	if (data->kind == nct6775) {
		regval = superio_inb(sio_data->sioreg, 0x2c);

		fan3pin = regval & (1 << 6);
		fan3min = fan3pin;
		pwm3pin = regval & (1 << 7);

		/* On NCT6775, fan4 shares pins with the fdc interface */
		fan4pin = !(superio_inb(sio_data->sioreg, 0x2A) & 0x80);
		fan4min = 0;
		fan5pin = 0;
		pwm4pin = 0;
		pwm5pin = 0;
	} else if (data->kind == nct6776) {
		bool gpok = superio_inb(sio_data->sioreg, 0x27) & 0x80;

		superio_select(sio_data->sioreg, NCT6775_LD_HWM);
		regval = superio_inb(sio_data->sioreg, SIO_REG_ENABLE);

		if (regval & 0x80)
			fan3pin = gpok;
		else
			fan3pin = !(superio_inb(sio_data->sioreg, 0x24) & 0x40);

		if (regval & 0x40)
			fan4pin = gpok;
		else
			fan4pin = superio_inb(sio_data->sioreg, 0x1C) & 0x01;

		if (regval & 0x20)
			fan5pin = gpok;
		else
			fan5pin = superio_inb(sio_data->sioreg, 0x1C) & 0x02;

		fan4min = fan4pin;
		fan3min = fan3pin;
		pwm3pin = fan3pin;
		pwm4pin = 0;
		pwm5pin = 0;
	} else {	/* NCT6779D */
		regval = superio_inb(sio_data->sioreg, 0x1c);

		fan3pin = !(regval & (1 << 5));
		fan4pin = !(regval & (1 << 6));
		fan5pin = !(regval & (1 << 7));

		pwm3pin = !(regval & (1 << 0));
		pwm4pin = !(regval & (1 << 1));
		pwm5pin = !(regval & (1 << 2));

		fan3min = fan3pin;
		fan4min = fan4pin;
	}

	superio_exit(sio_data->sioreg);

	data->has_fan = data->has_fan_min = 0x03; /* fan1 and fan2 */
	data->has_fan |= fan3pin << 2;
	data->has_fan_min |= fan3min << 2;

	data->has_fan |= (fan4pin << 3) | (fan5pin << 4);
	data->has_fan_min |= (fan4min << 3) | (fan5pin << 4);

	data->has_pwm = 0x03 | (pwm3pin << 2) | (pwm4pin << 3) | (pwm5pin << 4);

	return 0;
}

static void add_temp_sensors(struct nct6775_data *data, const u16 *regp,
			     int *available, int *mask)
{
	int i;
	u8 src;

	for (i = 0; i < data->pwm_num && *available; i++) {
		int index;

		if (!regp[i])
			continue;
		src = nct6775_read_value(data, regp[i]);
		src &= 0x1f;
		if (!src || (*mask & (1 << src)))
			continue;
		if (src >= data->temp_label_num ||
		    !strlen(data->temp_label[src]))
			continue;

		index = __ffs(*available);
		nct6775_write_value(data, data->REG_TEMP_SOURCE[index], src);
		*available &= ~(1 << index);
		*mask |= 1 << src;
	}
}

static int nct6775_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nct6775_sio_data *sio_data = dev->platform_data;
	struct nct6775_data *data;
	struct resource *res;
	int i, s, err = 0;
	int src, mask, available;
	const u16 *reg_temp, *reg_temp_over, *reg_temp_hyst, *reg_temp_config;
	const u16 *reg_temp_alternate, *reg_temp_crit;
	int num_reg_temp;
	bool have_vid = false;
	u8 cr2a;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!devm_request_region(&pdev->dev, res->start, IOREGION_LENGTH,
				 DRVNAME))
		return -EBUSY;

	data = devm_kzalloc(&pdev->dev, sizeof(struct nct6775_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->kind = sio_data->kind;
	data->addr = res->start;
	mutex_init(&data->update_lock);
	data->name = nct6775_device_names[data->kind];
	data->bank = 0xff;		/* Force initial bank selection */
	platform_set_drvdata(pdev, data);

	switch (data->kind) {
	case nct6775:
		data->in_num = 9;
		data->pwm_num = 3;
		data->auto_pwm_num = 6;
		data->has_fan_div = true;
		data->temp_fixed_num = 3;

		data->ALARM_BITS = NCT6775_ALARM_BITS;

		data->fan_from_reg = fan_from_reg16;
		data->fan_from_reg_min = fan_from_reg8;
		data->target_temp_mask = 0x7f;
		data->tolerance_mask = 0x0f;
		data->speed_tolerance_limit = 15;

		data->temp_label = nct6775_temp_label;
		data->temp_label_num = ARRAY_SIZE(nct6775_temp_label);

		data->REG_CONFIG = NCT6775_REG_CONFIG;
		data->REG_VBAT = NCT6775_REG_VBAT;
		data->REG_DIODE = NCT6775_REG_DIODE;
		data->REG_VIN = NCT6775_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6775_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6775_REG_IN_MAX;
		data->REG_TARGET = NCT6775_REG_TARGET;
		data->REG_FAN = NCT6775_REG_FAN;
		data->REG_FAN_MODE = NCT6775_REG_FAN_MODE;
		data->REG_FAN_MIN = NCT6775_REG_FAN_MIN;
		data->REG_FAN_PULSES = NCT6775_REG_FAN_PULSES;
		data->REG_FAN_TIME[0] = NCT6775_REG_FAN_STOP_TIME;
		data->REG_FAN_TIME[1] = NCT6775_REG_FAN_STEP_UP_TIME;
		data->REG_FAN_TIME[2] = NCT6775_REG_FAN_STEP_DOWN_TIME;
		data->REG_PWM[0] = NCT6775_REG_PWM;
		data->REG_PWM[1] = NCT6775_REG_FAN_START_OUTPUT;
		data->REG_PWM[2] = NCT6775_REG_FAN_STOP_OUTPUT;
		data->REG_PWM[3] = NCT6775_REG_FAN_MAX_OUTPUT;
		data->REG_PWM[4] = NCT6775_REG_FAN_STEP_OUTPUT;
		data->REG_PWM[5] = NCT6775_REG_WEIGHT_DUTY_STEP;
		data->REG_PWM_READ = NCT6775_REG_PWM_READ;
		data->REG_PWM_MODE = NCT6775_REG_PWM_MODE;
		data->PWM_MODE_MASK = NCT6775_PWM_MODE_MASK;
		data->REG_AUTO_TEMP = NCT6775_REG_AUTO_TEMP;
		data->REG_AUTO_PWM = NCT6775_REG_AUTO_PWM;
		data->REG_CRITICAL_TEMP = NCT6775_REG_CRITICAL_TEMP;
		data->REG_CRITICAL_TEMP_TOLERANCE
		  = NCT6775_REG_CRITICAL_TEMP_TOLERANCE;
		data->REG_TEMP_OFFSET = NCT6775_REG_TEMP_OFFSET;
		data->REG_TEMP_SOURCE = NCT6775_REG_TEMP_SOURCE;
		data->REG_TEMP_SEL = NCT6775_REG_TEMP_SEL;
		data->REG_WEIGHT_TEMP_SEL = NCT6775_REG_WEIGHT_TEMP_SEL;
		data->REG_WEIGHT_TEMP[0] = NCT6775_REG_WEIGHT_TEMP_STEP;
		data->REG_WEIGHT_TEMP[1] = NCT6775_REG_WEIGHT_TEMP_STEP_TOL;
		data->REG_WEIGHT_TEMP[2] = NCT6775_REG_WEIGHT_TEMP_BASE;
		data->REG_ALARM = NCT6775_REG_ALARM;

		reg_temp = NCT6775_REG_TEMP;
		num_reg_temp = ARRAY_SIZE(NCT6775_REG_TEMP);
		reg_temp_over = NCT6775_REG_TEMP_OVER;
		reg_temp_hyst = NCT6775_REG_TEMP_HYST;
		reg_temp_config = NCT6775_REG_TEMP_CONFIG;
		reg_temp_alternate = NCT6775_REG_TEMP_ALTERNATE;
		reg_temp_crit = NCT6775_REG_TEMP_CRIT;

		break;
	case nct6776:
		data->in_num = 9;
		data->pwm_num = 3;
		data->auto_pwm_num = 4;
		data->has_fan_div = false;
		data->temp_fixed_num = 3;

		data->ALARM_BITS = NCT6776_ALARM_BITS;

		data->fan_from_reg = fan_from_reg13;
		data->fan_from_reg_min = fan_from_reg13;
		data->target_temp_mask = 0xff;
		data->tolerance_mask = 0x07;
		data->speed_tolerance_limit = 63;

		data->temp_label = nct6776_temp_label;
		data->temp_label_num = ARRAY_SIZE(nct6776_temp_label);

		data->REG_CONFIG = NCT6775_REG_CONFIG;
		data->REG_VBAT = NCT6775_REG_VBAT;
		data->REG_DIODE = NCT6775_REG_DIODE;
		data->REG_VIN = NCT6775_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6775_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6775_REG_IN_MAX;
		data->REG_TARGET = NCT6775_REG_TARGET;
		data->REG_FAN = NCT6775_REG_FAN;
		data->REG_FAN_MODE = NCT6775_REG_FAN_MODE;
		data->REG_FAN_MIN = NCT6776_REG_FAN_MIN;
		data->REG_FAN_PULSES = NCT6776_REG_FAN_PULSES;
		data->REG_FAN_TIME[0] = NCT6775_REG_FAN_STOP_TIME;
		data->REG_FAN_TIME[1] = NCT6775_REG_FAN_STEP_UP_TIME;
		data->REG_FAN_TIME[2] = NCT6775_REG_FAN_STEP_DOWN_TIME;
		data->REG_TOLERANCE_H = NCT6776_REG_TOLERANCE_H;
		data->REG_PWM[0] = NCT6775_REG_PWM;
		data->REG_PWM[1] = NCT6775_REG_FAN_START_OUTPUT;
		data->REG_PWM[2] = NCT6775_REG_FAN_STOP_OUTPUT;
		data->REG_PWM[5] = NCT6775_REG_WEIGHT_DUTY_STEP;
		data->REG_PWM[6] = NCT6776_REG_WEIGHT_DUTY_BASE;
		data->REG_PWM_READ = NCT6775_REG_PWM_READ;
		data->REG_PWM_MODE = NCT6776_REG_PWM_MODE;
		data->PWM_MODE_MASK = NCT6776_PWM_MODE_MASK;
		data->REG_AUTO_TEMP = NCT6775_REG_AUTO_TEMP;
		data->REG_AUTO_PWM = NCT6775_REG_AUTO_PWM;
		data->REG_CRITICAL_TEMP = NCT6775_REG_CRITICAL_TEMP;
		data->REG_CRITICAL_TEMP_TOLERANCE
		  = NCT6775_REG_CRITICAL_TEMP_TOLERANCE;
		data->REG_TEMP_OFFSET = NCT6775_REG_TEMP_OFFSET;
		data->REG_TEMP_SOURCE = NCT6775_REG_TEMP_SOURCE;
		data->REG_TEMP_SEL = NCT6775_REG_TEMP_SEL;
		data->REG_WEIGHT_TEMP_SEL = NCT6775_REG_WEIGHT_TEMP_SEL;
		data->REG_WEIGHT_TEMP[0] = NCT6775_REG_WEIGHT_TEMP_STEP;
		data->REG_WEIGHT_TEMP[1] = NCT6775_REG_WEIGHT_TEMP_STEP_TOL;
		data->REG_WEIGHT_TEMP[2] = NCT6775_REG_WEIGHT_TEMP_BASE;
		data->REG_ALARM = NCT6775_REG_ALARM;

		reg_temp = NCT6775_REG_TEMP;
		num_reg_temp = ARRAY_SIZE(NCT6775_REG_TEMP);
		reg_temp_over = NCT6775_REG_TEMP_OVER;
		reg_temp_hyst = NCT6775_REG_TEMP_HYST;
		reg_temp_config = NCT6776_REG_TEMP_CONFIG;
		reg_temp_alternate = NCT6776_REG_TEMP_ALTERNATE;
		reg_temp_crit = NCT6776_REG_TEMP_CRIT;

		break;
	case nct6779:
		data->in_num = 15;
		data->pwm_num = 5;
		data->auto_pwm_num = 4;
		data->has_fan_div = false;
		data->temp_fixed_num = 6;

		data->ALARM_BITS = NCT6779_ALARM_BITS;

		data->fan_from_reg = fan_from_reg13;
		data->fan_from_reg_min = fan_from_reg13;
		data->target_temp_mask = 0xff;
		data->tolerance_mask = 0x07;
		data->speed_tolerance_limit = 63;

		data->temp_label = nct6779_temp_label;
		data->temp_label_num = ARRAY_SIZE(nct6779_temp_label);

		data->REG_CONFIG = NCT6775_REG_CONFIG;
		data->REG_VBAT = NCT6775_REG_VBAT;
		data->REG_DIODE = NCT6775_REG_DIODE;
		data->REG_VIN = NCT6779_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6775_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6775_REG_IN_MAX;
		data->REG_TARGET = NCT6775_REG_TARGET;
		data->REG_FAN = NCT6779_REG_FAN;
		data->REG_FAN_MODE = NCT6775_REG_FAN_MODE;
		data->REG_FAN_MIN = NCT6776_REG_FAN_MIN;
		data->REG_FAN_PULSES = NCT6779_REG_FAN_PULSES;
		data->REG_FAN_TIME[0] = NCT6775_REG_FAN_STOP_TIME;
		data->REG_FAN_TIME[1] = NCT6775_REG_FAN_STEP_UP_TIME;
		data->REG_FAN_TIME[2] = NCT6775_REG_FAN_STEP_DOWN_TIME;
		data->REG_TOLERANCE_H = NCT6776_REG_TOLERANCE_H;
		data->REG_PWM[0] = NCT6775_REG_PWM;
		data->REG_PWM[1] = NCT6775_REG_FAN_START_OUTPUT;
		data->REG_PWM[2] = NCT6775_REG_FAN_STOP_OUTPUT;
		data->REG_PWM[5] = NCT6775_REG_WEIGHT_DUTY_STEP;
		data->REG_PWM[6] = NCT6776_REG_WEIGHT_DUTY_BASE;
		data->REG_PWM_READ = NCT6775_REG_PWM_READ;
		data->REG_PWM_MODE = NCT6776_REG_PWM_MODE;
		data->PWM_MODE_MASK = NCT6776_PWM_MODE_MASK;
		data->REG_AUTO_TEMP = NCT6775_REG_AUTO_TEMP;
		data->REG_AUTO_PWM = NCT6775_REG_AUTO_PWM;
		data->REG_CRITICAL_TEMP = NCT6775_REG_CRITICAL_TEMP;
		data->REG_CRITICAL_TEMP_TOLERANCE
		  = NCT6775_REG_CRITICAL_TEMP_TOLERANCE;
		data->REG_TEMP_OFFSET = NCT6779_REG_TEMP_OFFSET;
		data->REG_TEMP_SOURCE = NCT6775_REG_TEMP_SOURCE;
		data->REG_TEMP_SEL = NCT6775_REG_TEMP_SEL;
		data->REG_WEIGHT_TEMP_SEL = NCT6775_REG_WEIGHT_TEMP_SEL;
		data->REG_WEIGHT_TEMP[0] = NCT6775_REG_WEIGHT_TEMP_STEP;
		data->REG_WEIGHT_TEMP[1] = NCT6775_REG_WEIGHT_TEMP_STEP_TOL;
		data->REG_WEIGHT_TEMP[2] = NCT6775_REG_WEIGHT_TEMP_BASE;
		data->REG_ALARM = NCT6779_REG_ALARM;

		reg_temp = NCT6779_REG_TEMP;
		num_reg_temp = ARRAY_SIZE(NCT6779_REG_TEMP);
		reg_temp_over = NCT6779_REG_TEMP_OVER;
		reg_temp_hyst = NCT6779_REG_TEMP_HYST;
		reg_temp_config = NCT6779_REG_TEMP_CONFIG;
		reg_temp_alternate = NCT6779_REG_TEMP_ALTERNATE;
		reg_temp_crit = NCT6779_REG_TEMP_CRIT;

		break;
	default:
		return -ENODEV;
	}
	data->have_in = (1 << data->in_num) - 1;
	data->have_temp = 0;

	/*
	 * On some boards, not all available temperature sources are monitored,
	 * even though some of the monitoring registers are unused.
	 * Get list of unused monitoring registers, then detect if any fan
	 * controls are configured to use unmonitored temperature sources.
	 * If so, assign the unmonitored temperature sources to available
	 * monitoring registers.
	 */
	mask = 0;
	available = 0;
	for (i = 0; i < num_reg_temp; i++) {
		if (reg_temp[i] == 0)
			continue;

		src = nct6775_read_value(data, data->REG_TEMP_SOURCE[i]) & 0x1f;
		if (!src || (mask & (1 << src)))
			available |= 1 << i;

		mask |= 1 << src;
	}

	/*
	 * Now find unmonitored temperature registers and enable monitoring
	 * if additional monitoring registers are available.
	 */
	add_temp_sensors(data, data->REG_TEMP_SEL, &available, &mask);
	add_temp_sensors(data, data->REG_WEIGHT_TEMP_SEL, &available, &mask);

	mask = 0;
	s = NUM_TEMP_FIXED;	/* First dynamic temperature attribute */
	for (i = 0; i < num_reg_temp; i++) {
		if (reg_temp[i] == 0)
			continue;

		src = nct6775_read_value(data, data->REG_TEMP_SOURCE[i]) & 0x1f;
		if (!src || (mask & (1 << src)))
			continue;

		if (src >= data->temp_label_num ||
		    !strlen(data->temp_label[src])) {
			dev_info(dev,
				 "Invalid temperature source %d at index %d, source register 0x%x, temp register 0x%x\n",
				 src, i, data->REG_TEMP_SOURCE[i], reg_temp[i]);
			continue;
		}

		mask |= 1 << src;

		/* Use fixed index for SYSTIN(1), CPUTIN(2), AUXTIN(3) */
		if (src <= data->temp_fixed_num) {
			data->have_temp |= 1 << (src - 1);
			data->have_temp_fixed |= 1 << (src - 1);
			data->reg_temp[0][src - 1] = reg_temp[i];
			data->reg_temp[1][src - 1] = reg_temp_over[i];
			data->reg_temp[2][src - 1] = reg_temp_hyst[i];
			data->reg_temp_config[src - 1] = reg_temp_config[i];
			data->temp_src[src - 1] = src;
			continue;
		}

		if (s >= NUM_TEMP)
			continue;

		/* Use dynamic index for other sources */
		data->have_temp |= 1 << s;
		data->reg_temp[0][s] = reg_temp[i];
		data->reg_temp[1][s] = reg_temp_over[i];
		data->reg_temp[2][s] = reg_temp_hyst[i];
		data->reg_temp_config[s] = reg_temp_config[i];
		if (reg_temp_crit[src - 1])
			data->reg_temp[3][s] = reg_temp_crit[src - 1];

		data->temp_src[s] = src;
		s++;
	}

#ifdef USE_ALTERNATE
	/*
	 * Go through the list of alternate temp registers and enable
	 * if possible.
	 * The temperature is already monitored if the respective bit in <mask>
	 * is set.
	 */
	for (i = 0; i < data->temp_label_num - 1; i++) {
		if (!reg_temp_alternate[i])
			continue;
		if (mask & (1 << (i + 1)))
			continue;
		if (i < data->temp_fixed_num) {
			if (data->have_temp & (1 << i))
				continue;
			data->have_temp |= 1 << i;
			data->have_temp_fixed |= 1 << i;
			data->reg_temp[0][i] = reg_temp_alternate[i];
			if (i < num_reg_temp) {
				data->reg_temp[1][i] = reg_temp_over[i];
				data->reg_temp[2][i] = reg_temp_hyst[i];
			}
			data->temp_src[i] = i + 1;
			continue;
		}

		if (s >= NUM_TEMP)	/* Abort if no more space */
			break;

		data->have_temp |= 1 << s;
		data->reg_temp[0][s] = reg_temp_alternate[i];
		data->temp_src[s] = i + 1;
		s++;
	}
#endif /* USE_ALTERNATE */

	/* Initialize the chip */
	nct6775_init_device(data);

	err = superio_enter(sio_data->sioreg);
	if (err)
		return err;

	cr2a = superio_inb(sio_data->sioreg, 0x2a);
	switch (data->kind) {
	case nct6775:
		have_vid = (cr2a & 0x40);
		break;
	case nct6776:
		have_vid = (cr2a & 0x60) == 0x40;
		break;
	case nct6779:
		break;
	}

	/*
	 * Read VID value
	 * We can get the VID input values directly at logical device D 0xe3.
	 */
	if (have_vid) {
		superio_select(sio_data->sioreg, NCT6775_LD_VID);
		data->vid = superio_inb(sio_data->sioreg, 0xe3);
		data->vrm = vid_which_vrm();
	}

	if (fan_debounce) {
		u8 tmp;

		superio_select(sio_data->sioreg, NCT6775_LD_HWM);
		tmp = superio_inb(sio_data->sioreg,
				  NCT6775_REG_CR_FAN_DEBOUNCE);
		switch (data->kind) {
		case nct6775:
			tmp |= 0x1e;
			break;
		case nct6776:
		case nct6779:
			tmp |= 0x3e;
			break;
		}
		superio_outb(sio_data->sioreg, NCT6775_REG_CR_FAN_DEBOUNCE,
			     tmp);
		dev_info(&pdev->dev, "Enabled fan debounce for chip %s\n",
			 data->name);
	}

	superio_exit(sio_data->sioreg);

	if (have_vid) {
		err = device_create_file(dev, &dev_attr_cpu0_vid);
		if (err)
			return err;
	}

	err = nct6775_check_fan_inputs(sio_data, data);
	if (err)
		goto exit_remove;

	/* Read fan clock dividers immediately */
	nct6775_init_fan_common(dev, data);

	/* Register sysfs hooks */
	for (i = 0; i < data->pwm_num; i++) {
		if (!(data->has_pwm & (1 << i)))
			continue;

		err = sysfs_create_group(&dev->kobj, &nct6775_group_pwm[i]);
		if (err)
			goto exit_remove;

		if (data->REG_PWM[3]) {
			err = device_create_file(dev,
					&sda_pwm_max[i].dev_attr);
			if (err)
				goto exit_remove;
		}
		if (data->REG_PWM[4]) {
			err = device_create_file(dev,
					&sda_pwm_step[i].dev_attr);
			if (err)
				goto exit_remove;
		}
		if (data->REG_PWM[6]) {
			err = device_create_file(dev,
					&sda_weight_duty_base[i].dev_attr);
			if (err)
				goto exit_remove;
		}
	}
	for (i = 0; i < ARRAY_SIZE(sda_auto_pwm_arrays); i++) {
		struct sensor_device_attribute_2 *attr =
			&sda_auto_pwm_arrays[i];

		if (!(data->has_pwm & (1 << attr->nr)))
			continue;
		if (attr->index > data->auto_pwm_num)
			continue;
		err = device_create_file(dev, &attr->dev_attr);
		if (err)
			goto exit_remove;
	}

	for (i = 0; i < data->in_num; i++) {
		if (!(data->have_in & (1 << i)))
			continue;
		err = sysfs_create_group(&dev->kobj, &nct6775_group_in[i]);
		if (err)
			goto exit_remove;
	}

	for (i = 0; i < 5; i++) {
		if (data->has_fan & (1 << i)) {
			err = device_create_file(dev,
						 &sda_fan_input[i].dev_attr);
			if (err)
				goto exit_remove;
			err = device_create_file(dev,
						 &sda_fan_alarm[i].dev_attr);
			if (err)
				goto exit_remove;
			if (data->kind != nct6776 &&
			    data->kind != nct6779) {
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
			err = device_create_file(dev,
						&sda_fan_pulses[i].dev_attr);
			if (err)
				goto exit_remove;
		}
	}

	for (i = 0; i < NUM_TEMP; i++) {
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
		if (data->reg_temp[1][i]) {
			err = device_create_file(dev,
						 &sda_temp_max[i].dev_attr);
			if (err)
				goto exit_remove;
		}
		if (data->reg_temp[2][i]) {
			err = device_create_file(dev,
					&sda_temp_max_hyst[i].dev_attr);
			if (err)
				goto exit_remove;
		}
		if (data->reg_temp[3][i]) {
			err = device_create_file(dev,
						 &sda_temp_crit[i].dev_attr);
			if (err)
				goto exit_remove;
		}
		if (!(data->have_temp_fixed & (1 << i)))
			continue;
		err = device_create_file(dev, &sda_temp_type[i].dev_attr);
		if (err)
			goto exit_remove;
		err = device_create_file(dev, &sda_temp_offset[i].dev_attr);
		if (err)
			goto exit_remove;
		if (i >= NUM_TEMP_ALARM ||
		    data->ALARM_BITS[TEMP_ALARM_BASE + i] < 0)
			continue;
		err = device_create_file(dev, &sda_temp_alarm[i].dev_attr);
		if (err)
			goto exit_remove;
	}

	for (i = 0; i < ARRAY_SIZE(sda_caseopen); i++) {
		if (data->ALARM_BITS[INTRUSION_ALARM_BASE + i] < 0)
			continue;
		err = device_create_file(dev, &sda_caseopen[i].dev_attr);
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
	nct6775_device_remove_files(dev);
	return err;
}

static int nct6775_remove(struct platform_device *pdev)
{
	struct nct6775_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	nct6775_device_remove_files(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int nct6775_suspend(struct device *dev)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct nct6775_sio_data *sio_data = dev->platform_data;

	mutex_lock(&data->update_lock);
	data->vbat = nct6775_read_value(data, data->REG_VBAT);
	if (sio_data->kind == nct6775) {
		data->fandiv1 = nct6775_read_value(data, NCT6775_REG_FANDIV1);
		data->fandiv2 = nct6775_read_value(data, NCT6775_REG_FANDIV2);
	}
	mutex_unlock(&data->update_lock);

	return 0;
}

static int nct6775_resume(struct device *dev)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct nct6775_sio_data *sio_data = dev->platform_data;
	int i, j;

	mutex_lock(&data->update_lock);
	data->bank = 0xff;		/* Force initial bank selection */

	/* Restore limits */
	for (i = 0; i < data->in_num; i++) {
		if (!(data->have_in & (1 << i)))
			continue;

		nct6775_write_value(data, data->REG_IN_MINMAX[0][i],
				    data->in[i][1]);
		nct6775_write_value(data, data->REG_IN_MINMAX[1][i],
				    data->in[i][2]);
	}

	for (i = 0; i < ARRAY_SIZE(data->fan_min); i++) {
		if (!(data->has_fan_min & (1 << i)))
			continue;

		nct6775_write_value(data, data->REG_FAN_MIN[i],
				    data->fan_min[i]);
	}

	for (i = 0; i < NUM_TEMP; i++) {
		if (!(data->have_temp & (1 << i)))
			continue;

		for (j = 1; j < ARRAY_SIZE(data->reg_temp); j++)
			if (data->reg_temp[j][i])
				nct6775_write_temp(data, data->reg_temp[j][i],
						   data->temp[j][i]);
	}

	/* Restore other settings */
	nct6775_write_value(data, data->REG_VBAT, data->vbat);
	if (sio_data->kind == nct6775) {
		nct6775_write_value(data, NCT6775_REG_FANDIV1, data->fandiv1);
		nct6775_write_value(data, NCT6775_REG_FANDIV2, data->fandiv2);
	}

	/* Force re-reading all values */
	data->valid = false;
	mutex_unlock(&data->update_lock);

	return 0;
}

static const struct dev_pm_ops nct6775_dev_pm_ops = {
	.suspend = nct6775_suspend,
	.resume = nct6775_resume,
};

#define NCT6775_DEV_PM_OPS	(&nct6775_dev_pm_ops)
#else
#define NCT6775_DEV_PM_OPS	NULL
#endif /* CONFIG_PM */

static struct platform_driver nct6775_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
		.pm	= NCT6775_DEV_PM_OPS,
	},
	.probe		= nct6775_probe,
	.remove		= nct6775_remove,
};

static const char * const nct6775_sio_names[] __initconst = {
	"NCT6775F",
	"NCT6776D/F",
	"NCT6779D",
};

/* nct6775_find() looks for a '627 in the Super-I/O config space */
static int __init nct6775_find(int sioaddr, unsigned short *addr,
			       struct nct6775_sio_data *sio_data)
{
	u16 val;
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
	case SIO_NCT6775_ID:
		sio_data->kind = nct6775;
		break;
	case SIO_NCT6776_ID:
		sio_data->kind = nct6776;
		break;
	case SIO_NCT6779_ID:
		sio_data->kind = nct6779;
		break;
	default:
		if (val != 0xffff)
			pr_debug("unsupported chip ID: 0x%04x\n", val);
		superio_exit(sioaddr);
		return -ENODEV;
	}

	/* We have a known chip, find the HWM I/O address */
	superio_select(sioaddr, NCT6775_LD_HWM);
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
	pr_info("Found %s or compatible chip at %#x\n",
		nct6775_sio_names[sio_data->kind], *addr);
	sio_data->sioreg = sioaddr;

	return 0;
}

/*
 * when Super-I/O functions move to a separate file, the Super-I/O
 * bus will manage the lifetime of the device and this module will only keep
 * track of the nct6775 driver. But since we platform_device_alloc(), we
 * must keep track of the device
 */
static struct platform_device *pdev;

static int __init sensors_nct6775_init(void)
{
	int err;
	unsigned short address;
	struct resource res;
	struct nct6775_sio_data sio_data;

	/*
	 * initialize sio_data->kind and sio_data->sioreg.
	 *
	 * when Super-I/O functions move to a separate file, the Super-I/O
	 * driver will probe 0x2e and 0x4e and auto-detect the presence of a
	 * nct6775 hardware monitor, and call probe()
	 */
	if (nct6775_find(0x2e, &address, &sio_data) &&
	    nct6775_find(0x4e, &address, &sio_data))
		return -ENODEV;

	err = platform_driver_register(&nct6775_driver);
	if (err)
		goto exit;

	pdev = platform_device_alloc(DRVNAME, address);
	if (!pdev) {
		err = -ENOMEM;
		pr_err("Device allocation failed\n");
		goto exit_unregister;
	}

	err = platform_device_add_data(pdev, &sio_data,
				       sizeof(struct nct6775_sio_data));
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
	platform_driver_unregister(&nct6775_driver);
exit:
	return err;
}

static void __exit sensors_nct6775_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&nct6775_driver);
}

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("NCT6775F/NCT6776F/NCT6779D driver");
MODULE_LICENSE("GPL");

module_init(sensors_nct6775_init);
module_exit(sensors_nct6775_exit);
