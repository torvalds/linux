// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * nct6775 - Driver for the hardware monitoring functionality of
 *	       Nuvoton NCT677x Super-I/O chips
 *
 * Copyright (C) 2012  Guenter Roeck <linux@roeck-us.net>
 *
 * Derived from w83627ehf driver
 * Copyright (C) 2005-2012  Jean Delvare <jdelvare@suse.de>
 * Copyright (C) 2006  Yuan Mu (Winbond),
 *		       Rudolf Marek <r.marek@assembler.cz>
 *		       David Hubbard <david.c.hubbard@gmail.com>
 *		       Daniel J Blueman <daniel.blueman@gmail.com>
 * Copyright (C) 2010  Sheng-Yuan Huang (Nuvoton) (PS00)
 *
 * Shamelessly ripped from the w83627hf driver
 * Copyright (C) 2003  Mark Studebaker
 *
 * Supports the following chips:
 *
 * Chip        #vin    #fan    #pwm    #temp  chip IDs       man ID
 * nct6106d     9      3       3       6+3    0xc450 0xc1    0x5ca3
 * nct6116d     9      5       5       3+3    0xd280 0xc1    0x5ca3
 * nct6775f     9      4       3       6+3    0xb470 0xc1    0x5ca3
 * nct6776f     9      5       3       6+3    0xc330 0xc1    0x5ca3
 * nct6779d    15      5       5       2+6    0xc560 0xc1    0x5ca3
 * nct6791d    15      6       6       2+6    0xc800 0xc1    0x5ca3
 * nct6792d    15      6       6       2+6    0xc910 0xc1    0x5ca3
 * nct6793d    15      6       6       2+6    0xd120 0xc1    0x5ca3
 * nct6795d    14      6       6       2+6    0xd350 0xc1    0x5ca3
 * nct6796d    14      7       7       2+6    0xd420 0xc1    0x5ca3
 * nct6797d    14      7       7       2+6    0xd450 0xc1    0x5ca3
 *                                           (0xd451)
 * nct6798d    14      7       7       2+6    0xd428 0xc1    0x5ca3
 *                                           (0xd429)
 *
 * #temp lists the number of monitored temperature sources (first value) plus
 * the number of directly connectable temperature sensors (second value).
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/nospec.h>
#include <linux/regmap.h>
#include "lm75.h"
#include "nct6775.h"

#undef DEFAULT_SYMBOL_NAMESPACE
#define DEFAULT_SYMBOL_NAMESPACE HWMON_NCT6775

#define USE_ALTERNATE

/* used to set data->name = nct6775_device_names[data->sio_kind] */
static const char * const nct6775_device_names[] = {
	"nct6106",
	"nct6116",
	"nct6775",
	"nct6776",
	"nct6779",
	"nct6791",
	"nct6792",
	"nct6793",
	"nct6795",
	"nct6796",
	"nct6797",
	"nct6798",
};

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
#define NCT6775_DIODE_MASK		0x02

static const u16 NCT6775_REG_ALARM[NUM_REG_ALARM] = { 0x459, 0x45A, 0x45B };

/* 0..15 voltages, 16..23 fans, 24..29 temperatures, 30..31 intrusion */

static const s8 NCT6775_ALARM_BITS[] = {
	0, 1, 2, 3, 8, 21, 20, 16,	/* in0.. in7 */
	17, -1, -1, -1, -1, -1, -1,	/* in8..in14 */
	-1,				/* unused */
	6, 7, 11, -1, -1,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	4, 5, 13, -1, -1, -1,		/* temp1..temp6 */
	12, -1 };			/* intrusion0, intrusion1 */

static const u16 NCT6775_REG_BEEP[NUM_REG_BEEP] = { 0x56, 0x57, 0x453, 0x4e };

/*
 * 0..14 voltages, 15 global beep enable, 16..23 fans, 24..29 temperatures,
 * 30..31 intrusion
 */
static const s8 NCT6775_BEEP_BITS[] = {
	0, 1, 2, 3, 8, 9, 10, 16,	/* in0.. in7 */
	17, -1, -1, -1, -1, -1, -1,	/* in8..in14 */
	21,				/* global beep enable */
	6, 7, 11, 28, -1,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	4, 5, 13, -1, -1, -1,		/* temp1..temp6 */
	12, -1 };			/* intrusion0, intrusion1 */

/* DC or PWM output fan configuration */
static const u8 NCT6775_REG_PWM_MODE[] = { 0x04, 0x04, 0x12 };
static const u8 NCT6775_PWM_MODE_MASK[] = { 0x01, 0x02, 0x01 };

/* Advanced Fan control, some values are common for all fans */

static const u16 NCT6775_REG_TARGET[] = {
	0x101, 0x201, 0x301, 0x801, 0x901, 0xa01, 0xb01 };
static const u16 NCT6775_REG_FAN_MODE[] = {
	0x102, 0x202, 0x302, 0x802, 0x902, 0xa02, 0xb02 };
static const u16 NCT6775_REG_FAN_STEP_DOWN_TIME[] = {
	0x103, 0x203, 0x303, 0x803, 0x903, 0xa03, 0xb03 };
static const u16 NCT6775_REG_FAN_STEP_UP_TIME[] = {
	0x104, 0x204, 0x304, 0x804, 0x904, 0xa04, 0xb04 };
static const u16 NCT6775_REG_FAN_STOP_OUTPUT[] = {
	0x105, 0x205, 0x305, 0x805, 0x905, 0xa05, 0xb05 };
static const u16 NCT6775_REG_FAN_START_OUTPUT[] = {
	0x106, 0x206, 0x306, 0x806, 0x906, 0xa06, 0xb06 };
static const u16 NCT6775_REG_FAN_MAX_OUTPUT[] = { 0x10a, 0x20a, 0x30a };
static const u16 NCT6775_REG_FAN_STEP_OUTPUT[] = { 0x10b, 0x20b, 0x30b };

static const u16 NCT6775_REG_FAN_STOP_TIME[] = {
	0x107, 0x207, 0x307, 0x807, 0x907, 0xa07, 0xb07 };
static const u16 NCT6775_REG_PWM[] = {
	0x109, 0x209, 0x309, 0x809, 0x909, 0xa09, 0xb09 };
static const u16 NCT6775_REG_PWM_READ[] = {
	0x01, 0x03, 0x11, 0x13, 0x15, 0xa09, 0xb09 };

static const u16 NCT6775_REG_FAN[] = { 0x630, 0x632, 0x634, 0x636, 0x638 };
static const u16 NCT6775_REG_FAN_MIN[] = { 0x3b, 0x3c, 0x3d };
static const u16 NCT6775_REG_FAN_PULSES[NUM_FAN] = {
	0x641, 0x642, 0x643, 0x644 };
static const u16 NCT6775_FAN_PULSE_SHIFT[NUM_FAN] = { };

static const u16 NCT6775_REG_TEMP[] = {
	0x27, 0x150, 0x250, 0x62b, 0x62c, 0x62d };

static const u16 NCT6775_REG_TEMP_MON[] = { 0x73, 0x75, 0x77 };

static const u16 NCT6775_REG_TEMP_CONFIG[ARRAY_SIZE(NCT6775_REG_TEMP)] = {
	0, 0x152, 0x252, 0x628, 0x629, 0x62A };
static const u16 NCT6775_REG_TEMP_HYST[ARRAY_SIZE(NCT6775_REG_TEMP)] = {
	0x3a, 0x153, 0x253, 0x673, 0x678, 0x67D };
static const u16 NCT6775_REG_TEMP_OVER[ARRAY_SIZE(NCT6775_REG_TEMP)] = {
	0x39, 0x155, 0x255, 0x672, 0x677, 0x67C };

static const u16 NCT6775_REG_TEMP_SOURCE[ARRAY_SIZE(NCT6775_REG_TEMP)] = {
	0x621, 0x622, 0x623, 0x624, 0x625, 0x626 };

static const u16 NCT6775_REG_TEMP_SEL[] = {
	0x100, 0x200, 0x300, 0x800, 0x900, 0xa00, 0xb00 };

static const u16 NCT6775_REG_WEIGHT_TEMP_SEL[] = {
	0x139, 0x239, 0x339, 0x839, 0x939, 0xa39 };
static const u16 NCT6775_REG_WEIGHT_TEMP_STEP[] = {
	0x13a, 0x23a, 0x33a, 0x83a, 0x93a, 0xa3a };
static const u16 NCT6775_REG_WEIGHT_TEMP_STEP_TOL[] = {
	0x13b, 0x23b, 0x33b, 0x83b, 0x93b, 0xa3b };
static const u16 NCT6775_REG_WEIGHT_DUTY_STEP[] = {
	0x13c, 0x23c, 0x33c, 0x83c, 0x93c, 0xa3c };
static const u16 NCT6775_REG_WEIGHT_TEMP_BASE[] = {
	0x13d, 0x23d, 0x33d, 0x83d, 0x93d, 0xa3d };

static const u16 NCT6775_REG_TEMP_OFFSET[] = { 0x454, 0x455, 0x456 };

static const u16 NCT6775_REG_AUTO_TEMP[] = {
	0x121, 0x221, 0x321, 0x821, 0x921, 0xa21, 0xb21 };
static const u16 NCT6775_REG_AUTO_PWM[] = {
	0x127, 0x227, 0x327, 0x827, 0x927, 0xa27, 0xb27 };

#define NCT6775_AUTO_TEMP(data, nr, p)	((data)->REG_AUTO_TEMP[nr] + (p))
#define NCT6775_AUTO_PWM(data, nr, p)	((data)->REG_AUTO_PWM[nr] + (p))

static const u16 NCT6775_REG_CRITICAL_ENAB[] = { 0x134, 0x234, 0x334 };

static const u16 NCT6775_REG_CRITICAL_TEMP[] = {
	0x135, 0x235, 0x335, 0x835, 0x935, 0xa35, 0xb35 };
static const u16 NCT6775_REG_CRITICAL_TEMP_TOLERANCE[] = {
	0x138, 0x238, 0x338, 0x838, 0x938, 0xa38, 0xb38 };

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

#define NCT6775_TEMP_MASK	0x001ffffe
#define NCT6775_VIRT_TEMP_MASK	0x00000000

static const u16 NCT6775_REG_TEMP_ALTERNATE[32] = {
	[13] = 0x661,
	[14] = 0x662,
	[15] = 0x664,
};

static const u16 NCT6775_REG_TEMP_CRIT[32] = {
	[4] = 0xa00,
	[5] = 0xa01,
	[6] = 0xa02,
	[7] = 0xa03,
	[8] = 0xa04,
	[9] = 0xa05,
	[10] = 0xa06,
	[11] = 0xa07
};

static const u16 NCT6775_REG_TSI_TEMP[] = { 0x669 };

/* NCT6776 specific data */

/* STEP_UP_TIME and STEP_DOWN_TIME regs are swapped for all chips but NCT6775 */
#define NCT6776_REG_FAN_STEP_UP_TIME NCT6775_REG_FAN_STEP_DOWN_TIME
#define NCT6776_REG_FAN_STEP_DOWN_TIME NCT6775_REG_FAN_STEP_UP_TIME

static const s8 NCT6776_ALARM_BITS[] = {
	0, 1, 2, 3, 8, 21, 20, 16,	/* in0.. in7 */
	17, -1, -1, -1, -1, -1, -1,	/* in8..in14 */
	-1,				/* unused */
	6, 7, 11, 10, 23,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	4, 5, 13, -1, -1, -1,		/* temp1..temp6 */
	12, 9 };			/* intrusion0, intrusion1 */

static const u16 NCT6776_REG_BEEP[NUM_REG_BEEP] = { 0xb2, 0xb3, 0xb4, 0xb5 };

static const s8 NCT6776_BEEP_BITS[] = {
	0, 1, 2, 3, 4, 5, 6, 7,		/* in0.. in7 */
	8, -1, -1, -1, -1, -1, -1,	/* in8..in14 */
	24,				/* global beep enable */
	25, 26, 27, 28, 29,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	16, 17, 18, 19, 20, 21,		/* temp1..temp6 */
	30, 31 };			/* intrusion0, intrusion1 */

static const u16 NCT6776_REG_TOLERANCE_H[] = {
	0x10c, 0x20c, 0x30c, 0x80c, 0x90c, 0xa0c, 0xb0c };

static const u8 NCT6776_REG_PWM_MODE[] = { 0x04, 0, 0, 0, 0, 0 };
static const u8 NCT6776_PWM_MODE_MASK[] = { 0x01, 0, 0, 0, 0, 0 };

static const u16 NCT6776_REG_FAN_MIN[] = {
	0x63a, 0x63c, 0x63e, 0x640, 0x642, 0x64a, 0x64c };
static const u16 NCT6776_REG_FAN_PULSES[NUM_FAN] = {
	0x644, 0x645, 0x646, 0x647, 0x648, 0x649 };

static const u16 NCT6776_REG_WEIGHT_DUTY_BASE[] = {
	0x13e, 0x23e, 0x33e, 0x83e, 0x93e, 0xa3e };

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

#define NCT6776_TEMP_MASK	0x007ffffe
#define NCT6776_VIRT_TEMP_MASK	0x00000000

static const u16 NCT6776_REG_TEMP_ALTERNATE[32] = {
	[14] = 0x401,
	[15] = 0x402,
	[16] = 0x404,
};

static const u16 NCT6776_REG_TEMP_CRIT[32] = {
	[11] = 0x709,
	[12] = 0x70a,
};

static const u16 NCT6776_REG_TSI_TEMP[] = {
	0x409, 0x40b, 0x40d, 0x40f, 0x411, 0x413, 0x415, 0x417 };

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

static const s8 NCT6779_BEEP_BITS[] = {
	0, 1, 2, 3, 4, 5, 6, 7,		/* in0.. in7 */
	8, 9, 10, 11, 12, 13, 14,	/* in8..in14 */
	24,				/* global beep enable */
	25, 26, 27, 28, 29,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	16, 17, -1, -1, -1, -1,		/* temp1..temp6 */
	30, 31 };			/* intrusion0, intrusion1 */

static const u16 NCT6779_REG_FAN[] = {
	0x4c0, 0x4c2, 0x4c4, 0x4c6, 0x4c8, 0x4ca, 0x4ce };
static const u16 NCT6779_REG_FAN_PULSES[NUM_FAN] = {
	0x644, 0x645, 0x646, 0x647, 0x648, 0x649, 0x64f };

static const u16 NCT6779_REG_CRITICAL_PWM_ENABLE[] = {
	0x136, 0x236, 0x336, 0x836, 0x936, 0xa36, 0xb36 };
#define NCT6779_CRITICAL_PWM_ENABLE_MASK	0x01
static const u16 NCT6779_REG_CRITICAL_PWM[] = {
	0x137, 0x237, 0x337, 0x837, 0x937, 0xa37, 0xb37 };

static const u16 NCT6779_REG_TEMP[] = { 0x27, 0x150 };
static const u16 NCT6779_REG_TEMP_MON[] = { 0x73, 0x75, 0x77, 0x79, 0x7b };
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
	"BYTE_TEMP",
	"",
	"",
	"",
	"",
	"Virtual_TEMP"
};

#define NCT6779_TEMP_MASK	0x07ffff7e
#define NCT6779_VIRT_TEMP_MASK	0x00000000
#define NCT6791_TEMP_MASK	0x87ffff7e
#define NCT6791_VIRT_TEMP_MASK	0x80000000

static const u16 NCT6779_REG_TEMP_ALTERNATE[32]
	= { 0x490, 0x491, 0x492, 0x493, 0x494, 0x495, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0x400, 0x401, 0x402, 0x404, 0x405, 0x406, 0x407,
	    0x408, 0 };

static const u16 NCT6779_REG_TEMP_CRIT[32] = {
	[15] = 0x709,
	[16] = 0x70a,
};

/* NCT6791 specific data */

static const u16 NCT6791_REG_WEIGHT_TEMP_SEL[NUM_FAN] = { 0, 0x239 };
static const u16 NCT6791_REG_WEIGHT_TEMP_STEP[NUM_FAN] = { 0, 0x23a };
static const u16 NCT6791_REG_WEIGHT_TEMP_STEP_TOL[NUM_FAN] = { 0, 0x23b };
static const u16 NCT6791_REG_WEIGHT_DUTY_STEP[NUM_FAN] = { 0, 0x23c };
static const u16 NCT6791_REG_WEIGHT_TEMP_BASE[NUM_FAN] = { 0, 0x23d };
static const u16 NCT6791_REG_WEIGHT_DUTY_BASE[NUM_FAN] = { 0, 0x23e };

static const u16 NCT6791_REG_ALARM[NUM_REG_ALARM] = {
	0x459, 0x45A, 0x45B, 0x568, 0x45D };

static const s8 NCT6791_ALARM_BITS[] = {
	0, 1, 2, 3, 8, 21, 20, 16,	/* in0.. in7 */
	17, 24, 25, 26, 27, 28, 29,	/* in8..in14 */
	-1,				/* unused */
	6, 7, 11, 10, 23, 33,		/* fan1..fan6 */
	-1, -1,				/* unused */
	4, 5, 13, -1, -1, -1,		/* temp1..temp6 */
	12, 9 };			/* intrusion0, intrusion1 */

/* NCT6792/NCT6793 specific data */

static const u16 NCT6792_REG_TEMP_MON[] = {
	0x73, 0x75, 0x77, 0x79, 0x7b, 0x7d };
static const u16 NCT6792_REG_BEEP[NUM_REG_BEEP] = {
	0xb2, 0xb3, 0xb4, 0xb5, 0xbf };

static const char *const nct6792_temp_label[] = {
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
	"BYTE_TEMP",
	"PECI Agent 0 Calibration",
	"PECI Agent 1 Calibration",
	"",
	"",
	"Virtual_TEMP"
};

#define NCT6792_TEMP_MASK	0x9fffff7e
#define NCT6792_VIRT_TEMP_MASK	0x80000000

static const char *const nct6793_temp_label[] = {
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
	"",
	"",
	"",
	"",
	"",
	"",
	"PECI Agent 0",
	"PECI Agent 1",
	"PCH_CHIP_CPU_MAX_TEMP",
	"PCH_CHIP_TEMP",
	"PCH_CPU_TEMP",
	"PCH_MCH_TEMP",
	"Agent0 Dimm0 ",
	"Agent0 Dimm1",
	"Agent1 Dimm0",
	"Agent1 Dimm1",
	"BYTE_TEMP0",
	"BYTE_TEMP1",
	"PECI Agent 0 Calibration",
	"PECI Agent 1 Calibration",
	"",
	"Virtual_TEMP"
};

#define NCT6793_TEMP_MASK	0xbfff037e
#define NCT6793_VIRT_TEMP_MASK	0x80000000

static const char *const nct6795_temp_label[] = {
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
	"Agent0 Dimm0",
	"Agent0 Dimm1",
	"Agent1 Dimm0",
	"Agent1 Dimm1",
	"BYTE_TEMP0",
	"BYTE_TEMP1",
	"PECI Agent 0 Calibration",
	"PECI Agent 1 Calibration",
	"",
	"Virtual_TEMP"
};

#define NCT6795_TEMP_MASK	0xbfffff7e
#define NCT6795_VIRT_TEMP_MASK	0x80000000

static const char *const nct6796_temp_label[] = {
	"",
	"SYSTIN",
	"CPUTIN",
	"AUXTIN0",
	"AUXTIN1",
	"AUXTIN2",
	"AUXTIN3",
	"AUXTIN4",
	"SMBUSMASTER 0",
	"SMBUSMASTER 1",
	"Virtual_TEMP",
	"Virtual_TEMP",
	"",
	"",
	"",
	"",
	"PECI Agent 0",
	"PECI Agent 1",
	"PCH_CHIP_CPU_MAX_TEMP",
	"PCH_CHIP_TEMP",
	"PCH_CPU_TEMP",
	"PCH_MCH_TEMP",
	"Agent0 Dimm0",
	"Agent0 Dimm1",
	"Agent1 Dimm0",
	"Agent1 Dimm1",
	"BYTE_TEMP0",
	"BYTE_TEMP1",
	"PECI Agent 0 Calibration",
	"PECI Agent 1 Calibration",
	"",
	"Virtual_TEMP"
};

#define NCT6796_TEMP_MASK	0xbfff0ffe
#define NCT6796_VIRT_TEMP_MASK	0x80000c00

static const u16 NCT6796_REG_TSI_TEMP[] = { 0x409, 0x40b };

static const char *const nct6798_temp_label[] = {
	"",
	"SYSTIN",
	"CPUTIN",
	"AUXTIN0",
	"AUXTIN1",
	"AUXTIN2",
	"AUXTIN3",
	"AUXTIN4",
	"SMBUSMASTER 0",
	"SMBUSMASTER 1",
	"Virtual_TEMP",
	"Virtual_TEMP",
	"",
	"",
	"",
	"",
	"PECI Agent 0",
	"PECI Agent 1",
	"PCH_CHIP_CPU_MAX_TEMP",
	"PCH_CHIP_TEMP",
	"PCH_CPU_TEMP",
	"PCH_MCH_TEMP",
	"Agent0 Dimm0",
	"Agent0 Dimm1",
	"Agent1 Dimm0",
	"Agent1 Dimm1",
	"BYTE_TEMP0",
	"BYTE_TEMP1",
	"PECI Agent 0 Calibration",	/* undocumented */
	"PECI Agent 1 Calibration",	/* undocumented */
	"",
	"Virtual_TEMP"
};

#define NCT6798_TEMP_MASK	0xbfff0ffe
#define NCT6798_VIRT_TEMP_MASK	0x80000c00

/* NCT6102D/NCT6106D specific data */

#define NCT6106_REG_VBAT	0x318
#define NCT6106_REG_DIODE	0x319
#define NCT6106_DIODE_MASK	0x01

static const u16 NCT6106_REG_IN_MAX[] = {
	0x90, 0x92, 0x94, 0x96, 0x98, 0x9a, 0x9e, 0xa0, 0xa2 };
static const u16 NCT6106_REG_IN_MIN[] = {
	0x91, 0x93, 0x95, 0x97, 0x99, 0x9b, 0x9f, 0xa1, 0xa3 };
static const u16 NCT6106_REG_IN[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08, 0x09 };

static const u16 NCT6106_REG_TEMP[] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15 };
static const u16 NCT6106_REG_TEMP_MON[] = { 0x18, 0x19, 0x1a };
static const u16 NCT6106_REG_TEMP_HYST[] = {
	0xc3, 0xc7, 0xcb, 0xcf, 0xd3, 0xd7 };
static const u16 NCT6106_REG_TEMP_OVER[] = {
	0xc2, 0xc6, 0xca, 0xce, 0xd2, 0xd6 };
static const u16 NCT6106_REG_TEMP_CRIT_L[] = {
	0xc0, 0xc4, 0xc8, 0xcc, 0xd0, 0xd4 };
static const u16 NCT6106_REG_TEMP_CRIT_H[] = {
	0xc1, 0xc5, 0xc9, 0xcf, 0xd1, 0xd5 };
static const u16 NCT6106_REG_TEMP_OFFSET[] = { 0x311, 0x312, 0x313 };
static const u16 NCT6106_REG_TEMP_CONFIG[] = {
	0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc };

static const u16 NCT6106_REG_FAN[] = { 0x20, 0x22, 0x24 };
static const u16 NCT6106_REG_FAN_MIN[] = { 0xe0, 0xe2, 0xe4 };
static const u16 NCT6106_REG_FAN_PULSES[] = { 0xf6, 0xf6, 0xf6 };
static const u16 NCT6106_FAN_PULSE_SHIFT[] = { 0, 2, 4 };

static const u8 NCT6106_REG_PWM_MODE[] = { 0xf3, 0xf3, 0xf3 };
static const u8 NCT6106_PWM_MODE_MASK[] = { 0x01, 0x02, 0x04 };
static const u16 NCT6106_REG_PWM_READ[] = { 0x4a, 0x4b, 0x4c };
static const u16 NCT6106_REG_FAN_MODE[] = { 0x113, 0x123, 0x133 };
static const u16 NCT6106_REG_TEMP_SOURCE[] = {
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5 };

static const u16 NCT6106_REG_CRITICAL_TEMP[] = { 0x11a, 0x12a, 0x13a };
static const u16 NCT6106_REG_CRITICAL_TEMP_TOLERANCE[] = {
	0x11b, 0x12b, 0x13b };

static const u16 NCT6106_REG_CRITICAL_PWM_ENABLE[] = { 0x11c, 0x12c, 0x13c };
#define NCT6106_CRITICAL_PWM_ENABLE_MASK	0x10
static const u16 NCT6106_REG_CRITICAL_PWM[] = { 0x11d, 0x12d, 0x13d };

static const u16 NCT6106_REG_FAN_STEP_UP_TIME[] = { 0x114, 0x124, 0x134 };
static const u16 NCT6106_REG_FAN_STEP_DOWN_TIME[] = { 0x115, 0x125, 0x135 };
static const u16 NCT6106_REG_FAN_STOP_OUTPUT[] = { 0x116, 0x126, 0x136 };
static const u16 NCT6106_REG_FAN_START_OUTPUT[] = { 0x117, 0x127, 0x137 };
static const u16 NCT6106_REG_FAN_STOP_TIME[] = { 0x118, 0x128, 0x138 };
static const u16 NCT6106_REG_TOLERANCE_H[] = { 0x112, 0x122, 0x132 };

static const u16 NCT6106_REG_TARGET[] = { 0x111, 0x121, 0x131 };

static const u16 NCT6106_REG_WEIGHT_TEMP_SEL[] = { 0x168, 0x178, 0x188 };
static const u16 NCT6106_REG_WEIGHT_TEMP_STEP[] = { 0x169, 0x179, 0x189 };
static const u16 NCT6106_REG_WEIGHT_TEMP_STEP_TOL[] = { 0x16a, 0x17a, 0x18a };
static const u16 NCT6106_REG_WEIGHT_DUTY_STEP[] = { 0x16b, 0x17b, 0x18b };
static const u16 NCT6106_REG_WEIGHT_TEMP_BASE[] = { 0x16c, 0x17c, 0x18c };
static const u16 NCT6106_REG_WEIGHT_DUTY_BASE[] = { 0x16d, 0x17d, 0x18d };

static const u16 NCT6106_REG_AUTO_TEMP[] = { 0x160, 0x170, 0x180 };
static const u16 NCT6106_REG_AUTO_PWM[] = { 0x164, 0x174, 0x184 };

static const u16 NCT6106_REG_ALARM[NUM_REG_ALARM] = {
	0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d };

static const s8 NCT6106_ALARM_BITS[] = {
	0, 1, 2, 3, 4, 5, 7, 8,		/* in0.. in7 */
	9, -1, -1, -1, -1, -1, -1,	/* in8..in14 */
	-1,				/* unused */
	32, 33, 34, -1, -1,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	16, 17, 18, 19, 20, 21,		/* temp1..temp6 */
	48, -1				/* intrusion0, intrusion1 */
};

static const u16 NCT6106_REG_BEEP[NUM_REG_BEEP] = {
	0x3c0, 0x3c1, 0x3c2, 0x3c3, 0x3c4 };

static const s8 NCT6106_BEEP_BITS[] = {
	0, 1, 2, 3, 4, 5, 7, 8,		/* in0.. in7 */
	9, 10, 11, 12, -1, -1, -1,	/* in8..in14 */
	32,				/* global beep enable */
	24, 25, 26, 27, 28,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	16, 17, 18, 19, 20, 21,		/* temp1..temp6 */
	34, -1				/* intrusion0, intrusion1 */
};

static const u16 NCT6106_REG_TEMP_ALTERNATE[32] = {
	[14] = 0x51,
	[15] = 0x52,
	[16] = 0x54,
};

static const u16 NCT6106_REG_TEMP_CRIT[32] = {
	[11] = 0x204,
	[12] = 0x205,
};

static const u16 NCT6106_REG_TSI_TEMP[] = { 0x59, 0x5b, 0x5d, 0x5f, 0x61, 0x63, 0x65, 0x67 };

/* NCT6112D/NCT6114D/NCT6116D specific data */

static const u16 NCT6116_REG_FAN[] = { 0x20, 0x22, 0x24, 0x26, 0x28 };
static const u16 NCT6116_REG_FAN_MIN[] = { 0xe0, 0xe2, 0xe4, 0xe6, 0xe8 };
static const u16 NCT6116_REG_FAN_PULSES[] = { 0xf6, 0xf6, 0xf6, 0xf6, 0xf5 };
static const u16 NCT6116_FAN_PULSE_SHIFT[] = { 0, 2, 4, 6, 6 };

static const u16 NCT6116_REG_PWM[] = { 0x119, 0x129, 0x139, 0x199, 0x1a9 };
static const u16 NCT6116_REG_FAN_MODE[] = { 0x113, 0x123, 0x133, 0x193, 0x1a3 };
static const u16 NCT6116_REG_TEMP_SEL[] = { 0x110, 0x120, 0x130, 0x190, 0x1a0 };
static const u16 NCT6116_REG_TEMP_SOURCE[] = {
	0xb0, 0xb1, 0xb2 };

static const u16 NCT6116_REG_CRITICAL_TEMP[] = {
	0x11a, 0x12a, 0x13a, 0x19a, 0x1aa };
static const u16 NCT6116_REG_CRITICAL_TEMP_TOLERANCE[] = {
	0x11b, 0x12b, 0x13b, 0x19b, 0x1ab };

static const u16 NCT6116_REG_CRITICAL_PWM_ENABLE[] = {
	0x11c, 0x12c, 0x13c, 0x19c, 0x1ac };
static const u16 NCT6116_REG_CRITICAL_PWM[] = {
	0x11d, 0x12d, 0x13d, 0x19d, 0x1ad };

static const u16 NCT6116_REG_FAN_STEP_UP_TIME[] = {
	0x114, 0x124, 0x134, 0x194, 0x1a4 };
static const u16 NCT6116_REG_FAN_STEP_DOWN_TIME[] = {
	0x115, 0x125, 0x135, 0x195, 0x1a5 };
static const u16 NCT6116_REG_FAN_STOP_OUTPUT[] = {
	0x116, 0x126, 0x136, 0x196, 0x1a6 };
static const u16 NCT6116_REG_FAN_START_OUTPUT[] = {
	0x117, 0x127, 0x137, 0x197, 0x1a7 };
static const u16 NCT6116_REG_FAN_STOP_TIME[] = {
	0x118, 0x128, 0x138, 0x198, 0x1a8 };
static const u16 NCT6116_REG_TOLERANCE_H[] = {
	0x112, 0x122, 0x132, 0x192, 0x1a2 };

static const u16 NCT6116_REG_TARGET[] = {
	0x111, 0x121, 0x131, 0x191, 0x1a1 };

static const u16 NCT6116_REG_AUTO_TEMP[] = {
	0x160, 0x170, 0x180, 0x1d0, 0x1e0 };
static const u16 NCT6116_REG_AUTO_PWM[] = {
	0x164, 0x174, 0x184, 0x1d4, 0x1e4 };

static const s8 NCT6116_ALARM_BITS[] = {
	0, 1, 2, 3, 4, 5, 7, 8,		/* in0.. in7 */
	9, -1, -1, -1, -1, -1, -1,	/* in8..in9 */
	-1,				/* unused */
	32, 33, 34, 35, 36,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	16, 17, 18, -1, -1, -1,		/* temp1..temp6 */
	48, -1				/* intrusion0, intrusion1 */
};

static const s8 NCT6116_BEEP_BITS[] = {
	0, 1, 2, 3, 4, 5, 7, 8,		/* in0.. in7 */
	9, 10, 11, 12, -1, -1, -1,	/* in8..in14 */
	32,				/* global beep enable */
	24, 25, 26, 27, 28,		/* fan1..fan5 */
	-1, -1, -1,			/* unused */
	16, 17, 18, -1, -1, -1,		/* temp1..temp6 */
	34, -1				/* intrusion0, intrusion1 */
};

static const u16 NCT6116_REG_TSI_TEMP[] = { 0x59, 0x5b };

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

static unsigned int fan_from_reg_rpm(u16 reg, unsigned int divreg)
{
	return reg;
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
	return BIT(reg);
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

/* TSI temperatures are in 8.3 format */
static inline unsigned int tsi_temp_from_reg(unsigned int reg)
{
	return (reg >> 5) * 125;
}

/*
 * Data structures and manipulation thereof
 */

struct sensor_device_template {
	struct device_attribute dev_attr;
	union {
		struct {
			u8 nr;
			u8 index;
		} s;
		int index;
	} u;
	bool s2;	/* true if both index and nr are used */
};

struct sensor_device_attr_u {
	union {
		struct sensor_device_attribute a1;
		struct sensor_device_attribute_2 a2;
	} u;
	char name[32];
};

#define __TEMPLATE_ATTR(_template, _mode, _show, _store) {	\
	.attr = {.name = _template, .mode = _mode },		\
	.show	= _show,					\
	.store	= _store,					\
}

#define SENSOR_DEVICE_TEMPLATE(_template, _mode, _show, _store, _index)	\
	{ .dev_attr = __TEMPLATE_ATTR(_template, _mode, _show, _store),	\
	  .u.index = _index,						\
	  .s2 = false }

#define SENSOR_DEVICE_TEMPLATE_2(_template, _mode, _show, _store,	\
				 _nr, _index)				\
	{ .dev_attr = __TEMPLATE_ATTR(_template, _mode, _show, _store),	\
	  .u.s.index = _index,						\
	  .u.s.nr = _nr,						\
	  .s2 = true }

#define SENSOR_TEMPLATE(_name, _template, _mode, _show, _store, _index)	\
static struct sensor_device_template sensor_dev_template_##_name	\
	= SENSOR_DEVICE_TEMPLATE(_template, _mode, _show, _store,	\
				 _index)

#define SENSOR_TEMPLATE_2(_name, _template, _mode, _show, _store,	\
			  _nr, _index)					\
static struct sensor_device_template sensor_dev_template_##_name	\
	= SENSOR_DEVICE_TEMPLATE_2(_template, _mode, _show, _store,	\
				 _nr, _index)

struct sensor_template_group {
	struct sensor_device_template **templates;
	umode_t (*is_visible)(struct kobject *, struct attribute *, int);
	int base;
};

static int nct6775_add_template_attr_group(struct device *dev, struct nct6775_data *data,
					   const struct sensor_template_group *tg, int repeat)
{
	struct attribute_group *group;
	struct sensor_device_attr_u *su;
	struct sensor_device_attribute *a;
	struct sensor_device_attribute_2 *a2;
	struct attribute **attrs;
	struct sensor_device_template **t;
	int i, count;

	if (repeat <= 0)
		return -EINVAL;

	t = tg->templates;
	for (count = 0; *t; t++, count++)
		;

	if (count == 0)
		return -EINVAL;

	group = devm_kzalloc(dev, sizeof(*group), GFP_KERNEL);
	if (group == NULL)
		return -ENOMEM;

	attrs = devm_kcalloc(dev, repeat * count + 1, sizeof(*attrs),
			     GFP_KERNEL);
	if (attrs == NULL)
		return -ENOMEM;

	su = devm_kzalloc(dev, array3_size(repeat, count, sizeof(*su)),
			       GFP_KERNEL);
	if (su == NULL)
		return -ENOMEM;

	group->attrs = attrs;
	group->is_visible = tg->is_visible;

	for (i = 0; i < repeat; i++) {
		t = tg->templates;
		while (*t != NULL) {
			snprintf(su->name, sizeof(su->name),
				 (*t)->dev_attr.attr.name, tg->base + i);
			if ((*t)->s2) {
				a2 = &su->u.a2;
				sysfs_attr_init(&a2->dev_attr.attr);
				a2->dev_attr.attr.name = su->name;
				a2->nr = (*t)->u.s.nr + i;
				a2->index = (*t)->u.s.index;
				a2->dev_attr.attr.mode =
				  (*t)->dev_attr.attr.mode;
				a2->dev_attr.show = (*t)->dev_attr.show;
				a2->dev_attr.store = (*t)->dev_attr.store;
				*attrs = &a2->dev_attr.attr;
			} else {
				a = &su->u.a1;
				sysfs_attr_init(&a->dev_attr.attr);
				a->dev_attr.attr.name = su->name;
				a->index = (*t)->u.index + i;
				a->dev_attr.attr.mode =
				  (*t)->dev_attr.attr.mode;
				a->dev_attr.show = (*t)->dev_attr.show;
				a->dev_attr.store = (*t)->dev_attr.store;
				*attrs = &a->dev_attr.attr;
			}
			attrs++;
			su++;
			t++;
		}
	}

	return nct6775_add_attr_group(data, group);
}

bool nct6775_reg_is_word_sized(struct nct6775_data *data, u16 reg)
{
	switch (data->kind) {
	case nct6106:
		return reg == 0x20 || reg == 0x22 || reg == 0x24 ||
		  (reg >= 0x59 && reg < 0x69 && (reg & 1)) ||
		  reg == 0xe0 || reg == 0xe2 || reg == 0xe4 ||
		  reg == 0x111 || reg == 0x121 || reg == 0x131;
	case nct6116:
		return reg == 0x20 || reg == 0x22 || reg == 0x24 ||
		  reg == 0x26 || reg == 0x28 || reg == 0x59 || reg == 0x5b ||
		  reg == 0xe0 || reg == 0xe2 || reg == 0xe4 || reg == 0xe6 ||
		  reg == 0xe8 || reg == 0x111 || reg == 0x121 || reg == 0x131 ||
		  reg == 0x191 || reg == 0x1a1;
	case nct6775:
		return (((reg & 0xff00) == 0x100 ||
		    (reg & 0xff00) == 0x200) &&
		   ((reg & 0x00ff) == 0x50 ||
		    (reg & 0x00ff) == 0x53 ||
		    (reg & 0x00ff) == 0x55)) ||
		  (reg & 0xfff0) == 0x630 ||
		  reg == 0x640 || reg == 0x642 ||
		  reg == 0x662 || reg == 0x669 ||
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
		  (reg >= 0x409 && reg < 0x419 && (reg & 1)) ||
		  reg == 0x640 || reg == 0x642 ||
		  ((reg & 0xfff0) == 0x650 && (reg & 0x000f) >= 0x06) ||
		  reg == 0x73 || reg == 0x75 || reg == 0x77;
	case nct6779:
	case nct6791:
	case nct6792:
	case nct6793:
	case nct6795:
	case nct6796:
	case nct6797:
	case nct6798:
		return reg == 0x150 || reg == 0x153 || reg == 0x155 ||
		  (reg & 0xfff0) == 0x4c0 ||
		  reg == 0x402 ||
		  (reg >= 0x409 && reg < 0x419 && (reg & 1)) ||
		  reg == 0x63a || reg == 0x63c || reg == 0x63e ||
		  reg == 0x640 || reg == 0x642 || reg == 0x64a ||
		  reg == 0x64c ||
		  reg == 0x73 || reg == 0x75 || reg == 0x77 || reg == 0x79 ||
		  reg == 0x7b || reg == 0x7d;
	}
	return false;
}
EXPORT_SYMBOL_GPL(nct6775_reg_is_word_sized);

/* We left-align 8-bit temperature values to make the code simpler */
static int nct6775_read_temp(struct nct6775_data *data, u16 reg, u16 *val)
{
	int err;

	err = nct6775_read_value(data, reg, val);
	if (err)
		return err;

	if (!nct6775_reg_is_word_sized(data, reg))
		*val <<= 8;

	return 0;
}

/* This function assumes that the caller holds data->update_lock */
static int nct6775_write_fan_div(struct nct6775_data *data, int nr)
{
	u16 reg;
	int err;
	u16 fandiv_reg = nr < 2 ? NCT6775_REG_FANDIV1 : NCT6775_REG_FANDIV2;
	unsigned int oddshift = (nr & 1) * 4; /* masks shift by four if nr is odd */

	err = nct6775_read_value(data, fandiv_reg, &reg);
	if (err)
		return err;
	reg &= 0x70 >> oddshift;
	reg |= data->fan_div[nr] & (0x7 << oddshift);
	return nct6775_write_value(data, fandiv_reg, reg);
}

static int nct6775_write_fan_div_common(struct nct6775_data *data, int nr)
{
	if (data->kind == nct6775)
		return nct6775_write_fan_div(data, nr);
	return 0;
}

static int nct6775_update_fan_div(struct nct6775_data *data)
{
	int err;
	u16 i;

	err = nct6775_read_value(data, NCT6775_REG_FANDIV1, &i);
	if (err)
		return err;
	data->fan_div[0] = i & 0x7;
	data->fan_div[1] = (i & 0x70) >> 4;
	err = nct6775_read_value(data, NCT6775_REG_FANDIV2, &i);
	if (err)
		return err;
	data->fan_div[2] = i & 0x7;
	if (data->has_fan & BIT(3))
		data->fan_div[3] = (i & 0x70) >> 4;

	return 0;
}

static int nct6775_update_fan_div_common(struct nct6775_data *data)
{
	if (data->kind == nct6775)
		return nct6775_update_fan_div(data);
	return 0;
}

static int nct6775_init_fan_div(struct nct6775_data *data)
{
	int i, err;

	err = nct6775_update_fan_div_common(data);
	if (err)
		return err;

	/*
	 * For all fans, start with highest divider value if the divider
	 * register is not initialized. This ensures that we get a
	 * reading from the fan count register, even if it is not optimal.
	 * We'll compute a better divider later on.
	 */
	for (i = 0; i < ARRAY_SIZE(data->fan_div); i++) {
		if (!(data->has_fan & BIT(i)))
			continue;
		if (data->fan_div[i] == 0) {
			data->fan_div[i] = 7;
			err = nct6775_write_fan_div_common(data, i);
			if (err)
				return err;
		}
	}

	return 0;
}

static int nct6775_init_fan_common(struct device *dev,
				   struct nct6775_data *data)
{
	int i, err;
	u16 reg;

	if (data->has_fan_div) {
		err = nct6775_init_fan_div(data);
		if (err)
			return err;
	}

	/*
	 * If fan_min is not set (0), set it to 0xff to disable it. This
	 * prevents the unnecessary warning when fanX_min is reported as 0.
	 */
	for (i = 0; i < ARRAY_SIZE(data->fan_min); i++) {
		if (data->has_fan_min & BIT(i)) {
			err = nct6775_read_value(data, data->REG_FAN_MIN[i], &reg);
			if (err)
				return err;
			if (!reg) {
				err = nct6775_write_value(data, data->REG_FAN_MIN[i],
							  data->has_fan_div ? 0xff : 0xff1f);
				if (err)
					return err;
			}
		}
	}

	return 0;
}

static int nct6775_select_fan_div(struct device *dev,
				  struct nct6775_data *data, int nr, u16 reg)
{
	int err;
	u8 fan_div = data->fan_div[nr];
	u16 fan_min;

	if (!data->has_fan_div)
		return 0;

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
		if (data->has_fan_min & BIT(nr)) {
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
				err = nct6775_write_value(data, data->REG_FAN_MIN[nr], fan_min);
				if (err)
					return err;
			}
		}
		data->fan_div[nr] = fan_div;
		err = nct6775_write_fan_div_common(data, nr);
		if (err)
			return err;
	}

	return 0;
}

static int nct6775_update_pwm(struct device *dev)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	int i, j, err;
	u16 fanmodecfg, reg;
	bool duty_is_dc;

	for (i = 0; i < data->pwm_num; i++) {
		if (!(data->has_pwm & BIT(i)))
			continue;

		err = nct6775_read_value(data, data->REG_PWM_MODE[i], &reg);
		if (err)
			return err;
		duty_is_dc = data->REG_PWM_MODE[i] && (reg & data->PWM_MODE_MASK[i]);
		data->pwm_mode[i] = !duty_is_dc;

		err = nct6775_read_value(data, data->REG_FAN_MODE[i], &fanmodecfg);
		if (err)
			return err;
		for (j = 0; j < ARRAY_SIZE(data->REG_PWM); j++) {
			if (data->REG_PWM[j] && data->REG_PWM[j][i]) {
				err = nct6775_read_value(data, data->REG_PWM[j][i], &reg);
				if (err)
					return err;
				data->pwm[j][i] = reg;
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
				err = nct6775_read_value(data, data->REG_TOLERANCE_H[i], &reg);
				if (err)
					return err;
				t |= (reg & 0x70) >> 1;
			}
			data->target_speed_tolerance[i] = t;
		}

		err = nct6775_read_value(data, data->REG_CRITICAL_TEMP_TOLERANCE[i], &reg);
		if (err)
			return err;
		data->temp_tolerance[1][i] = reg;

		err = nct6775_read_value(data, data->REG_TEMP_SEL[i], &reg);
		if (err)
			return err;
		data->pwm_temp_sel[i] = reg & 0x1f;
		/* If fan can stop, report floor as 0 */
		if (reg & 0x80)
			data->pwm[2][i] = 0;

		if (!data->REG_WEIGHT_TEMP_SEL[i])
			continue;

		err = nct6775_read_value(data, data->REG_WEIGHT_TEMP_SEL[i], &reg);
		if (err)
			return err;
		data->pwm_weight_temp_sel[i] = reg & 0x1f;
		/* If weight is disabled, report weight source as 0 */
		if (!(reg & 0x80))
			data->pwm_weight_temp_sel[i] = 0;

		/* Weight temp data */
		for (j = 0; j < ARRAY_SIZE(data->weight_temp); j++) {
			err = nct6775_read_value(data, data->REG_WEIGHT_TEMP[j][i], &reg);
			if (err)
				return err;
			data->weight_temp[j][i] = reg;
		}
	}

	return 0;
}

static int nct6775_update_pwm_limits(struct device *dev)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	int i, j, err;
	u16 reg, reg_t;

	for (i = 0; i < data->pwm_num; i++) {
		if (!(data->has_pwm & BIT(i)))
			continue;

		for (j = 0; j < ARRAY_SIZE(data->fan_time); j++) {
			err = nct6775_read_value(data, data->REG_FAN_TIME[j][i], &reg);
			if (err)
				return err;
			data->fan_time[j][i] = reg;
		}

		err = nct6775_read_value(data, data->REG_TARGET[i], &reg_t);
		if (err)
			return err;

		/* Update only in matching mode or if never updated */
		if (!data->target_temp[i] ||
		    data->pwm_enable[i] == thermal_cruise)
			data->target_temp[i] = reg_t & data->target_temp_mask;
		if (!data->target_speed[i] ||
		    data->pwm_enable[i] == speed_cruise) {
			if (data->REG_TOLERANCE_H) {
				err = nct6775_read_value(data, data->REG_TOLERANCE_H[i], &reg);
				if (err)
					return err;
				reg_t |= (reg & 0x0f) << 8;
			}
			data->target_speed[i] = reg_t;
		}

		for (j = 0; j < data->auto_pwm_num; j++) {
			err = nct6775_read_value(data, NCT6775_AUTO_PWM(data, i, j), &reg);
			if (err)
				return err;
			data->auto_pwm[i][j] = reg;

			err = nct6775_read_value(data, NCT6775_AUTO_TEMP(data, i, j), &reg);
			if (err)
				return err;
			data->auto_temp[i][j] = reg;
		}

		/* critical auto_pwm temperature data */
		err = nct6775_read_value(data, data->REG_CRITICAL_TEMP[i], &reg);
		if (err)
			return err;
		data->auto_temp[i][data->auto_pwm_num] = reg;

		switch (data->kind) {
		case nct6775:
			err = nct6775_read_value(data, NCT6775_REG_CRITICAL_ENAB[i], &reg);
			if (err)
				return err;
			data->auto_pwm[i][data->auto_pwm_num] =
						(reg & 0x02) ? 0xff : 0x00;
			break;
		case nct6776:
			data->auto_pwm[i][data->auto_pwm_num] = 0xff;
			break;
		case nct6106:
		case nct6116:
		case nct6779:
		case nct6791:
		case nct6792:
		case nct6793:
		case nct6795:
		case nct6796:
		case nct6797:
		case nct6798:
			err = nct6775_read_value(data, data->REG_CRITICAL_PWM_ENABLE[i], &reg);
			if (err)
				return err;
			if (reg & data->CRITICAL_PWM_ENABLE_MASK) {
				err = nct6775_read_value(data, data->REG_CRITICAL_PWM[i], &reg);
				if (err)
					return err;
			} else {
				reg = 0xff;
			}
			data->auto_pwm[i][data->auto_pwm_num] = reg;
			break;
		}
	}

	return 0;
}

struct nct6775_data *nct6775_update_device(struct device *dev)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	int i, j, err = 0;
	u16 reg;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		/* Fan clock dividers */
		err = nct6775_update_fan_div_common(data);
		if (err)
			goto out;

		/* Measured voltages and limits */
		for (i = 0; i < data->in_num; i++) {
			if (!(data->have_in & BIT(i)))
				continue;

			err = nct6775_read_value(data, data->REG_VIN[i], &reg);
			if (err)
				goto out;
			data->in[i][0] = reg;

			err = nct6775_read_value(data, data->REG_IN_MINMAX[0][i], &reg);
			if (err)
				goto out;
			data->in[i][1] = reg;

			err = nct6775_read_value(data, data->REG_IN_MINMAX[1][i], &reg);
			if (err)
				goto out;
			data->in[i][2] = reg;
		}

		/* Measured fan speeds and limits */
		for (i = 0; i < ARRAY_SIZE(data->rpm); i++) {
			if (!(data->has_fan & BIT(i)))
				continue;

			err = nct6775_read_value(data, data->REG_FAN[i], &reg);
			if (err)
				goto out;
			data->rpm[i] = data->fan_from_reg(reg,
							  data->fan_div[i]);

			if (data->has_fan_min & BIT(i)) {
				err = nct6775_read_value(data, data->REG_FAN_MIN[i], &reg);
				if (err)
					goto out;
				data->fan_min[i] = reg;
			}

			if (data->REG_FAN_PULSES[i]) {
				err = nct6775_read_value(data, data->REG_FAN_PULSES[i], &reg);
				if (err)
					goto out;
				data->fan_pulses[i] = (reg >> data->FAN_PULSE_SHIFT[i]) & 0x03;
			}

			err = nct6775_select_fan_div(dev, data, i, reg);
			if (err)
				goto out;
		}

		err = nct6775_update_pwm(dev);
		if (err)
			goto out;

		err = nct6775_update_pwm_limits(dev);
		if (err)
			goto out;

		/* Measured temperatures and limits */
		for (i = 0; i < NUM_TEMP; i++) {
			if (!(data->have_temp & BIT(i)))
				continue;
			for (j = 0; j < ARRAY_SIZE(data->reg_temp); j++) {
				if (data->reg_temp[j][i]) {
					err = nct6775_read_temp(data, data->reg_temp[j][i], &reg);
					if (err)
						goto out;
					data->temp[j][i] = reg;
				}
			}
			if (i >= NUM_TEMP_FIXED ||
			    !(data->have_temp_fixed & BIT(i)))
				continue;
			err = nct6775_read_value(data, data->REG_TEMP_OFFSET[i], &reg);
			if (err)
				goto out;
			data->temp_offset[i] = reg;
		}

		for (i = 0; i < NUM_TSI_TEMP; i++) {
			if (!(data->have_tsi_temp & BIT(i)))
				continue;
			err = nct6775_read_value(data, data->REG_TSI_TEMP[i], &reg);
			if (err)
				goto out;
			data->tsi_temp[i] = reg;
		}

		data->alarms = 0;
		for (i = 0; i < NUM_REG_ALARM; i++) {
			u16 alarm;

			if (!data->REG_ALARM[i])
				continue;
			err = nct6775_read_value(data, data->REG_ALARM[i], &alarm);
			if (err)
				goto out;
			data->alarms |= ((u64)alarm) << (i << 3);
		}

		data->beeps = 0;
		for (i = 0; i < NUM_REG_BEEP; i++) {
			u16 beep;

			if (!data->REG_BEEP[i])
				continue;
			err = nct6775_read_value(data, data->REG_BEEP[i], &beep);
			if (err)
				goto out;
			data->beeps |= ((u64)beep) << (i << 3);
		}

		data->last_updated = jiffies;
		data->valid = true;
	}
out:
	mutex_unlock(&data->update_lock);
	return err ? ERR_PTR(err) : data;
}
EXPORT_SYMBOL_GPL(nct6775_update_device);

/*
 * Sysfs callback functions
 */
static ssize_t
show_in_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int index = sattr->index;
	int nr = sattr->nr;

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%ld\n", in_from_reg(data->in[nr][index], nr));
}

static ssize_t
store_in_reg(struct device *dev, struct device_attribute *attr, const char *buf,
	     size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int index = sattr->index;
	int nr = sattr->nr;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	mutex_lock(&data->update_lock);
	data->in[nr][index] = in_to_reg(val, nr);
	err = nct6775_write_value(data, data->REG_IN_MINMAX[index - 1][nr], data->in[nr][index]);
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

ssize_t
nct6775_show_alarm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr;

	if (IS_ERR(data))
		return PTR_ERR(data);

	nr = data->ALARM_BITS[sattr->index];
	return sprintf(buf, "%u\n",
		       (unsigned int)((data->alarms >> nr) & 0x01));
}
EXPORT_SYMBOL_GPL(nct6775_show_alarm);

static int find_temp_source(struct nct6775_data *data, int index, int count)
{
	int source = data->temp_src[index];
	int nr, err;

	for (nr = 0; nr < count; nr++) {
		u16 src;

		err = nct6775_read_value(data, data->REG_TEMP_SOURCE[nr], &src);
		if (err)
			return err;
		if ((src & 0x1f) == source)
			return nr;
	}
	return -ENODEV;
}

static ssize_t
show_temp_alarm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6775_data *data = nct6775_update_device(dev);
	unsigned int alarm = 0;
	int nr;

	if (IS_ERR(data))
		return PTR_ERR(data);

	/*
	 * For temperatures, there is no fixed mapping from registers to alarm
	 * bits. Alarm bits are determined by the temperature source mapping.
	 */
	nr = find_temp_source(data, sattr->index, data->num_temp_alarms);
	if (nr >= 0) {
		int bit = data->ALARM_BITS[nr + TEMP_ALARM_BASE];

		alarm = (data->alarms >> bit) & 0x01;
	}
	return sprintf(buf, "%u\n", alarm);
}

ssize_t
nct6775_show_beep(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6775_data *data = nct6775_update_device(dev);
	int nr;

	if (IS_ERR(data))
		return PTR_ERR(data);

	nr = data->BEEP_BITS[sattr->index];

	return sprintf(buf, "%u\n",
		       (unsigned int)((data->beeps >> nr) & 0x01));
}
EXPORT_SYMBOL_GPL(nct6775_show_beep);

ssize_t
nct6775_store_beep(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct6775_data *data = dev_get_drvdata(dev);
	int nr = data->BEEP_BITS[sattr->index];
	int regindex = nr >> 3;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	if (val > 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	if (val)
		data->beeps |= (1ULL << nr);
	else
		data->beeps &= ~(1ULL << nr);
	err = nct6775_write_value(data, data->REG_BEEP[regindex],
				  (data->beeps >> (regindex << 3)) & 0xff);
	mutex_unlock(&data->update_lock);
	return err ? : count;
}
EXPORT_SYMBOL_GPL(nct6775_store_beep);

static ssize_t
show_temp_beep(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	struct nct6775_data *data = nct6775_update_device(dev);
	unsigned int beep = 0;
	int nr;

	if (IS_ERR(data))
		return PTR_ERR(data);

	/*
	 * For temperatures, there is no fixed mapping from registers to beep
	 * enable bits. Beep enable bits are determined by the temperature
	 * source mapping.
	 */
	nr = find_temp_source(data, sattr->index, data->num_temp_beeps);
	if (nr >= 0) {
		int bit = data->BEEP_BITS[nr + TEMP_ALARM_BASE];

		beep = (data->beeps >> bit) & 0x01;
	}
	return sprintf(buf, "%u\n", beep);
}

static ssize_t
store_temp_beep(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct nct6775_data *data = dev_get_drvdata(dev);
	int nr, bit, regindex;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	if (val > 1)
		return -EINVAL;

	nr = find_temp_source(data, sattr->index, data->num_temp_beeps);
	if (nr < 0)
		return nr;

	bit = data->BEEP_BITS[nr + TEMP_ALARM_BASE];
	regindex = bit >> 3;

	mutex_lock(&data->update_lock);
	if (val)
		data->beeps |= (1ULL << bit);
	else
		data->beeps &= ~(1ULL << bit);
	err = nct6775_write_value(data, data->REG_BEEP[regindex],
				  (data->beeps >> (regindex << 3)) & 0xff);
	mutex_unlock(&data->update_lock);

	return err ? : count;
}

static umode_t nct6775_in_is_visible(struct kobject *kobj,
				     struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct6775_data *data = dev_get_drvdata(dev);
	int in = index / 5;	/* voltage index */

	if (!(data->have_in & BIT(in)))
		return 0;

	return nct6775_attr_mode(data, attr);
}

SENSOR_TEMPLATE_2(in_input, "in%d_input", 0444, show_in_reg, NULL, 0, 0);
SENSOR_TEMPLATE(in_alarm, "in%d_alarm", 0444, nct6775_show_alarm, NULL, 0);
SENSOR_TEMPLATE(in_beep, "in%d_beep", 0644, nct6775_show_beep, nct6775_store_beep, 0);
SENSOR_TEMPLATE_2(in_min, "in%d_min", 0644, show_in_reg, store_in_reg, 0, 1);
SENSOR_TEMPLATE_2(in_max, "in%d_max", 0644, show_in_reg, store_in_reg, 0, 2);

/*
 * nct6775_in_is_visible uses the index into the following array
 * to determine if attributes should be created or not.
 * Any change in order or content must be matched.
 */
static struct sensor_device_template *nct6775_attributes_in_template[] = {
	&sensor_dev_template_in_input,
	&sensor_dev_template_in_alarm,
	&sensor_dev_template_in_beep,
	&sensor_dev_template_in_min,
	&sensor_dev_template_in_max,
	NULL
};

static const struct sensor_template_group nct6775_in_template_group = {
	.templates = nct6775_attributes_in_template,
	.is_visible = nct6775_in_is_visible,
};

static ssize_t
show_fan(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->rpm[nr]);
}

static ssize_t
show_fan_min(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

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

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	unsigned int reg;
	u8 new_div;
	int err;

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
		new_div = 7; /* 128 == BIT(7) */
		dev_warn(dev,
			 "fan%u low limit %lu below minimum %u, set to minimum\n",
			 nr + 1, val, data->fan_from_reg_min(254, 7));
	} else if (!reg) {
		/*
		 * Speed above this value cannot possibly be represented,
		 * even with the lowest divider (1)
		 */
		data->fan_min[nr] = 1;
		new_div = 0; /* 1 == BIT(0) */
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
		err = nct6775_write_fan_div_common(data, nr);
		if (err)
			goto write_min;
		/* Give the chip time to sample a new speed value */
		data->last_updated = jiffies;
	}

write_min:
	err = nct6775_write_value(data, data->REG_FAN_MIN[nr], data->fan_min[nr]);
	mutex_unlock(&data->update_lock);

	return err ? : count;
}

static ssize_t
show_fan_pulses(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int p;

	if (IS_ERR(data))
		return PTR_ERR(data);

	p = data->fan_pulses[sattr->index];
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
	u16 reg;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (val > 4)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->fan_pulses[nr] = val & 3;
	err = nct6775_read_value(data, data->REG_FAN_PULSES[nr], &reg);
	if (err)
		goto out;
	reg &= ~(0x03 << data->FAN_PULSE_SHIFT[nr]);
	reg |= (val & 3) << data->FAN_PULSE_SHIFT[nr];
	err = nct6775_write_value(data, data->REG_FAN_PULSES[nr], reg);
out:
	mutex_unlock(&data->update_lock);

	return err ? : count;
}

static umode_t nct6775_fan_is_visible(struct kobject *kobj,
				      struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct6775_data *data = dev_get_drvdata(dev);
	int fan = index / 6;	/* fan index */
	int nr = index % 6;	/* attribute index */

	if (!(data->has_fan & BIT(fan)))
		return 0;

	if (nr == 1 && data->ALARM_BITS[FAN_ALARM_BASE + fan] == -1)
		return 0;
	if (nr == 2 && data->BEEP_BITS[FAN_ALARM_BASE + fan] == -1)
		return 0;
	if (nr == 3 && !data->REG_FAN_PULSES[fan])
		return 0;
	if (nr == 4 && !(data->has_fan_min & BIT(fan)))
		return 0;
	if (nr == 5 && data->kind != nct6775)
		return 0;

	return nct6775_attr_mode(data, attr);
}

SENSOR_TEMPLATE(fan_input, "fan%d_input", 0444, show_fan, NULL, 0);
SENSOR_TEMPLATE(fan_alarm, "fan%d_alarm", 0444, nct6775_show_alarm, NULL, FAN_ALARM_BASE);
SENSOR_TEMPLATE(fan_beep, "fan%d_beep", 0644, nct6775_show_beep,
		nct6775_store_beep, FAN_ALARM_BASE);
SENSOR_TEMPLATE(fan_pulses, "fan%d_pulses", 0644, show_fan_pulses, store_fan_pulses, 0);
SENSOR_TEMPLATE(fan_min, "fan%d_min", 0644, show_fan_min, store_fan_min, 0);
SENSOR_TEMPLATE(fan_div, "fan%d_div", 0444, show_fan_div, NULL, 0);

/*
 * nct6775_fan_is_visible uses the index into the following array
 * to determine if attributes should be created or not.
 * Any change in order or content must be matched.
 */
static struct sensor_device_template *nct6775_attributes_fan_template[] = {
	&sensor_dev_template_fan_input,
	&sensor_dev_template_fan_alarm,	/* 1 */
	&sensor_dev_template_fan_beep,	/* 2 */
	&sensor_dev_template_fan_pulses,
	&sensor_dev_template_fan_min,	/* 4 */
	&sensor_dev_template_fan_div,	/* 5 */
	NULL
};

static const struct sensor_template_group nct6775_fan_template_group = {
	.templates = nct6775_attributes_fan_template,
	.is_visible = nct6775_fan_is_visible,
	.base = 1,
};

static ssize_t
show_temp_label(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%s\n", data->temp_label[data->temp_src[nr]]);
}

static ssize_t
show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	err = nct6775_write_temp(data, data->reg_temp[index][nr], data->temp[index][nr]);
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

static ssize_t
show_temp_offset(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	err = nct6775_write_value(data, data->REG_TEMP_OFFSET[nr], val);
	mutex_unlock(&data->update_lock);

	return err ? : count;
}

static ssize_t
show_temp_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	u8 vbit, dbit;
	u16 vbat, diode;

	if (IS_ERR(data))
		return PTR_ERR(data);

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (val != 1 && val != 3 && val != 4)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	data->temp_type[nr] = val;
	vbit = 0x02 << nr;
	dbit = data->DIODE_MASK << nr;

	err = nct6775_read_value(data, data->REG_VBAT, &vbat);
	if (err)
		goto out;
	vbat &= ~vbit;

	err = nct6775_read_value(data, data->REG_DIODE, &diode);
	if (err)
		goto out;
	diode &= ~dbit;

	switch (val) {
	case 1:	/* CPU diode (diode, current mode) */
		vbat |= vbit;
		diode |= dbit;
		break;
	case 3: /* diode, voltage mode */
		vbat |= dbit;
		break;
	case 4:	/* thermistor */
		break;
	}
	err = nct6775_write_value(data, data->REG_VBAT, vbat);
	if (err)
		goto out;
	err = nct6775_write_value(data, data->REG_DIODE, diode);
out:
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

static umode_t nct6775_temp_is_visible(struct kobject *kobj,
				       struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct6775_data *data = dev_get_drvdata(dev);
	int temp = index / 10;	/* temp index */
	int nr = index % 10;	/* attribute index */

	if (!(data->have_temp & BIT(temp)))
		return 0;

	if (nr == 1 && !data->temp_label)
		return 0;

	if (nr == 2 && find_temp_source(data, temp, data->num_temp_alarms) < 0)
		return 0;				/* alarm */

	if (nr == 3 && find_temp_source(data, temp, data->num_temp_beeps) < 0)
		return 0;				/* beep */

	if (nr == 4 && !data->reg_temp[1][temp])	/* max */
		return 0;

	if (nr == 5 && !data->reg_temp[2][temp])	/* max_hyst */
		return 0;

	if (nr == 6 && !data->reg_temp[3][temp])	/* crit */
		return 0;

	if (nr == 7 && !data->reg_temp[4][temp])	/* lcrit */
		return 0;

	/* offset and type only apply to fixed sensors */
	if (nr > 7 && !(data->have_temp_fixed & BIT(temp)))
		return 0;

	return nct6775_attr_mode(data, attr);
}

SENSOR_TEMPLATE_2(temp_input, "temp%d_input", 0444, show_temp, NULL, 0, 0);
SENSOR_TEMPLATE(temp_label, "temp%d_label", 0444, show_temp_label, NULL, 0);
SENSOR_TEMPLATE_2(temp_max, "temp%d_max", 0644, show_temp, store_temp, 0, 1);
SENSOR_TEMPLATE_2(temp_max_hyst, "temp%d_max_hyst", 0644, show_temp, store_temp, 0, 2);
SENSOR_TEMPLATE_2(temp_crit, "temp%d_crit", 0644, show_temp, store_temp, 0, 3);
SENSOR_TEMPLATE_2(temp_lcrit, "temp%d_lcrit", 0644, show_temp, store_temp, 0, 4);
SENSOR_TEMPLATE(temp_offset, "temp%d_offset", 0644, show_temp_offset, store_temp_offset, 0);
SENSOR_TEMPLATE(temp_type, "temp%d_type", 0644, show_temp_type, store_temp_type, 0);
SENSOR_TEMPLATE(temp_alarm, "temp%d_alarm", 0444, show_temp_alarm, NULL, 0);
SENSOR_TEMPLATE(temp_beep, "temp%d_beep", 0644, show_temp_beep, store_temp_beep, 0);

/*
 * nct6775_temp_is_visible uses the index into the following array
 * to determine if attributes should be created or not.
 * Any change in order or content must be matched.
 */
static struct sensor_device_template *nct6775_attributes_temp_template[] = {
	&sensor_dev_template_temp_input,
	&sensor_dev_template_temp_label,
	&sensor_dev_template_temp_alarm,	/* 2 */
	&sensor_dev_template_temp_beep,		/* 3 */
	&sensor_dev_template_temp_max,		/* 4 */
	&sensor_dev_template_temp_max_hyst,	/* 5 */
	&sensor_dev_template_temp_crit,		/* 6 */
	&sensor_dev_template_temp_lcrit,	/* 7 */
	&sensor_dev_template_temp_offset,	/* 8 */
	&sensor_dev_template_temp_type,		/* 9 */
	NULL
};

static const struct sensor_template_group nct6775_temp_template_group = {
	.templates = nct6775_attributes_temp_template,
	.is_visible = nct6775_temp_is_visible,
	.base = 1,
};

static ssize_t show_tsi_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sysfs_emit(buf, "%u\n", tsi_temp_from_reg(data->tsi_temp[sattr->index]));
}

static ssize_t show_tsi_temp_label(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	return sysfs_emit(buf, "TSI%d_TEMP\n", sattr->index);
}

SENSOR_TEMPLATE(tsi_temp_input, "temp%d_input", 0444, show_tsi_temp, NULL, 0);
SENSOR_TEMPLATE(tsi_temp_label, "temp%d_label", 0444, show_tsi_temp_label, NULL, 0);

static umode_t nct6775_tsi_temp_is_visible(struct kobject *kobj, struct attribute *attr,
					       int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct6775_data *data = dev_get_drvdata(dev);
	int temp = index / 2;

	return (data->have_tsi_temp & BIT(temp)) ? nct6775_attr_mode(data, attr) : 0;
}

/*
 * The index calculation in nct6775_tsi_temp_is_visible() must be kept in
 * sync with the size of this array.
 */
static struct sensor_device_template *nct6775_tsi_temp_template[] = {
	&sensor_dev_template_tsi_temp_input,
	&sensor_dev_template_tsi_temp_label,
	NULL
};

static ssize_t
show_pwm_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->pwm_mode[sattr->index]);
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
	u16 reg;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (val > 1)
		return -EINVAL;

	/* Setting DC mode (0) is not supported for all chips/channels */
	if (data->REG_PWM_MODE[nr] == 0) {
		if (!val)
			return -EINVAL;
		return count;
	}

	mutex_lock(&data->update_lock);
	data->pwm_mode[nr] = val;
	err = nct6775_read_value(data, data->REG_PWM_MODE[nr], &reg);
	if (err)
		goto out;
	reg &= ~data->PWM_MODE_MASK[nr];
	if (!val)
		reg |= data->PWM_MODE_MASK[nr];
	err = nct6775_write_value(data, data->REG_PWM_MODE[nr], reg);
out:
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

static ssize_t
show_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;
	int err;
	u16 pwm;

	if (IS_ERR(data))
		return PTR_ERR(data);

	/*
	 * For automatic fan control modes, show current pwm readings.
	 * Otherwise, show the configured value.
	 */
	if (index == 0 && data->pwm_enable[nr] > manual) {
		err = nct6775_read_value(data, data->REG_PWM_READ[nr], &pwm);
		if (err)
			return err;
	} else {
		pwm = data->pwm[index][nr];
	}

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
	u16 reg;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	val = clamp_val(val, minval[index], maxval[index]);

	mutex_lock(&data->update_lock);
	data->pwm[index][nr] = val;
	err = nct6775_write_value(data, data->REG_PWM[index][nr], val);
	if (err)
		goto out;
	if (index == 2)	{ /* floor: disable if val == 0 */
		err = nct6775_read_value(data, data->REG_TEMP_SEL[nr], &reg);
		if (err)
			goto out;
		reg &= 0x7f;
		if (val)
			reg |= 0x80;
		err = nct6775_write_value(data, data->REG_TEMP_SEL[nr], reg);
	}
out:
	mutex_unlock(&data->update_lock);
	return err ? : count;
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

static int pwm_update_registers(struct nct6775_data *data, int nr)
{
	u16 reg;
	int err;

	switch (data->pwm_enable[nr]) {
	case off:
	case manual:
		break;
	case speed_cruise:
		err = nct6775_read_value(data, data->REG_FAN_MODE[nr], &reg);
		if (err)
			return err;
		reg = (reg & ~data->tolerance_mask) |
		  (data->target_speed_tolerance[nr] & data->tolerance_mask);
		err = nct6775_write_value(data, data->REG_FAN_MODE[nr], reg);
		if (err)
			return err;
		err = nct6775_write_value(data, data->REG_TARGET[nr],
					  data->target_speed[nr] & 0xff);
		if (err)
			return err;
		if (data->REG_TOLERANCE_H) {
			reg = (data->target_speed[nr] >> 8) & 0x0f;
			reg |= (data->target_speed_tolerance[nr] & 0x38) << 1;
			err = nct6775_write_value(data, data->REG_TOLERANCE_H[nr], reg);
			if (err)
				return err;
		}
		break;
	case thermal_cruise:
		err = nct6775_write_value(data, data->REG_TARGET[nr], data->target_temp[nr]);
		if (err)
			return err;
		fallthrough;
	default:
		err = nct6775_read_value(data, data->REG_FAN_MODE[nr], &reg);
		if (err)
			return err;
		reg = (reg & ~data->tolerance_mask) |
		  data->temp_tolerance[0][nr];
		err = nct6775_write_value(data, data->REG_FAN_MODE[nr], reg);
		if (err)
			return err;
		break;
	}

	return 0;
}

static ssize_t
show_pwm_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

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
		err = nct6775_write_value(data, data->REG_PWM[0][nr], 255);
		if (err)
			goto out;
	}
	err = pwm_update_registers(data, nr);
	if (err)
		goto out;
	err = nct6775_read_value(data, data->REG_FAN_MODE[nr], &reg);
	if (err)
		goto out;
	reg &= 0x0f;
	reg |= pwm_enable_to_reg(val) << 4;
	err = nct6775_write_value(data, data->REG_FAN_MODE[nr], reg);
out:
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

static ssize_t
show_pwm_temp_sel_common(struct nct6775_data *data, char *buf, int src)
{
	int i, sel = 0;

	for (i = 0; i < NUM_TEMP; i++) {
		if (!(data->have_temp & BIT(i)))
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

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	int err, src;
	u16 reg;

	if (IS_ERR(data))
		return PTR_ERR(data);

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	if (val == 0 || val > NUM_TEMP)
		return -EINVAL;
	if (!(data->have_temp & BIT(val - 1)) || !data->temp_src[val - 1])
		return -EINVAL;

	mutex_lock(&data->update_lock);
	src = data->temp_src[val - 1];
	data->pwm_temp_sel[nr] = src;
	err = nct6775_read_value(data, data->REG_TEMP_SEL[nr], &reg);
	if (err)
		goto out;
	reg &= 0xe0;
	reg |= src;
	err = nct6775_write_value(data, data->REG_TEMP_SEL[nr], reg);
out:
	mutex_unlock(&data->update_lock);

	return err ? : count;
}

static ssize_t
show_pwm_weight_temp_sel(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int index = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	int err, src;
	u16 reg;

	if (IS_ERR(data))
		return PTR_ERR(data);

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;
	if (val > NUM_TEMP)
		return -EINVAL;
	val = array_index_nospec(val, NUM_TEMP + 1);
	if (val && (!(data->have_temp & BIT(val - 1)) ||
		    !data->temp_src[val - 1]))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	if (val) {
		src = data->temp_src[val - 1];
		data->pwm_weight_temp_sel[nr] = src;
		err = nct6775_read_value(data, data->REG_WEIGHT_TEMP_SEL[nr], &reg);
		if (err)
			goto out;
		reg &= 0xe0;
		reg |= (src | 0x80);
		err = nct6775_write_value(data, data->REG_WEIGHT_TEMP_SEL[nr], reg);
	} else {
		data->pwm_weight_temp_sel[nr] = 0;
		err = nct6775_read_value(data, data->REG_WEIGHT_TEMP_SEL[nr], &reg);
		if (err)
			goto out;
		reg &= 0x7f;
		err = nct6775_write_value(data, data->REG_WEIGHT_TEMP_SEL[nr], reg);
	}
out:
	mutex_unlock(&data->update_lock);

	return err ? : count;
}

static ssize_t
show_target_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	err = pwm_update_registers(data, nr);
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

static ssize_t
show_target_speed(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	err = pwm_update_registers(data, nr);
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

static ssize_t
show_temp_tolerance(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

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
		err = pwm_update_registers(data, nr);
	else
		err = nct6775_write_value(data, data->REG_CRITICAL_TEMP_TOLERANCE[nr], val);
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

/*
 * Fan speed tolerance is a tricky beast, since the associated register is
 * a tick counter, but the value is reported and configured as rpm.
 * Compute resulting low and high rpm values and report the difference.
 * A fan speed tolerance only makes sense if a fan target speed has been
 * configured, so only display values other than 0 if that is the case.
 */
static ssize_t
show_speed_tolerance(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	int nr = sattr->index;
	int target, tolerance = 0;

	if (IS_ERR(data))
		return PTR_ERR(data);

	target = data->target_speed[nr];

	if (target) {
		int low = target - data->target_speed_tolerance[nr];
		int high = target + data->target_speed_tolerance[nr];

		if (low <= 0)
			low = 1;
		if (high > 0xffff)
			high = 0xffff;
		if (high < low)
			high = low;

		tolerance = (fan_from_reg16(low, data->fan_div[nr])
			     - fan_from_reg16(high, data->fan_div[nr])) / 2;
	}

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

	high = fan_from_reg16(data->target_speed[nr], data->fan_div[nr]) + val;
	low = fan_from_reg16(data->target_speed[nr], data->fan_div[nr]) - val;
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
	err = pwm_update_registers(data, nr);
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

SENSOR_TEMPLATE_2(pwm, "pwm%d", 0644, show_pwm, store_pwm, 0, 0);
SENSOR_TEMPLATE(pwm_mode, "pwm%d_mode", 0644, show_pwm_mode, store_pwm_mode, 0);
SENSOR_TEMPLATE(pwm_enable, "pwm%d_enable", 0644, show_pwm_enable, store_pwm_enable, 0);
SENSOR_TEMPLATE(pwm_temp_sel, "pwm%d_temp_sel", 0644, show_pwm_temp_sel, store_pwm_temp_sel, 0);
SENSOR_TEMPLATE(pwm_target_temp, "pwm%d_target_temp", 0644, show_target_temp, store_target_temp, 0);
SENSOR_TEMPLATE(fan_target, "fan%d_target", 0644, show_target_speed, store_target_speed, 0);
SENSOR_TEMPLATE(fan_tolerance, "fan%d_tolerance", 0644, show_speed_tolerance,
		store_speed_tolerance, 0);

/* Smart Fan registers */

static ssize_t
show_weight_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	err = nct6775_write_value(data, data->REG_WEIGHT_TEMP[index][nr], val);
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

SENSOR_TEMPLATE(pwm_weight_temp_sel, "pwm%d_weight_temp_sel", 0644,
		show_pwm_weight_temp_sel, store_pwm_weight_temp_sel, 0);
SENSOR_TEMPLATE_2(pwm_weight_temp_step, "pwm%d_weight_temp_step",
		  0644, show_weight_temp, store_weight_temp, 0, 0);
SENSOR_TEMPLATE_2(pwm_weight_temp_step_tol, "pwm%d_weight_temp_step_tol",
		  0644, show_weight_temp, store_weight_temp, 0, 1);
SENSOR_TEMPLATE_2(pwm_weight_temp_step_base, "pwm%d_weight_temp_step_base",
		  0644, show_weight_temp, store_weight_temp, 0, 2);
SENSOR_TEMPLATE_2(pwm_weight_duty_step, "pwm%d_weight_duty_step", 0644, show_pwm, store_pwm, 0, 5);
SENSOR_TEMPLATE_2(pwm_weight_duty_base, "pwm%d_weight_duty_base", 0644, show_pwm, store_pwm, 0, 6);

static ssize_t
show_fan_time(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int index = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	err = nct6775_write_value(data, data->REG_FAN_TIME[index][nr], val);
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

static ssize_t
show_auto_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

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
	u16 reg;

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
		err = nct6775_write_value(data, NCT6775_AUTO_PWM(data, nr, point),
					  data->auto_pwm[nr][point]);
	} else {
		switch (data->kind) {
		case nct6775:
			/* disable if needed (pwm == 0) */
			err = nct6775_read_value(data, NCT6775_REG_CRITICAL_ENAB[nr], &reg);
			if (err)
				break;
			if (val)
				reg |= 0x02;
			else
				reg &= ~0x02;
			err = nct6775_write_value(data, NCT6775_REG_CRITICAL_ENAB[nr], reg);
			break;
		case nct6776:
			break; /* always enabled, nothing to do */
		case nct6106:
		case nct6116:
		case nct6779:
		case nct6791:
		case nct6792:
		case nct6793:
		case nct6795:
		case nct6796:
		case nct6797:
		case nct6798:
			err = nct6775_write_value(data, data->REG_CRITICAL_PWM[nr], val);
			if (err)
				break;
			err = nct6775_read_value(data, data->REG_CRITICAL_PWM_ENABLE[nr], &reg);
			if (err)
				break;
			if (val == 255)
				reg &= ~data->CRITICAL_PWM_ENABLE_MASK;
			else
				reg |= data->CRITICAL_PWM_ENABLE_MASK;
			err = nct6775_write_value(data, data->REG_CRITICAL_PWM_ENABLE[nr], reg);
			break;
		}
	}
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

static ssize_t
show_auto_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = nct6775_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int nr = sattr->nr;
	int point = sattr->index;

	if (IS_ERR(data))
		return PTR_ERR(data);

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
		err = nct6775_write_value(data, NCT6775_AUTO_TEMP(data, nr, point),
					  data->auto_temp[nr][point]);
	} else {
		err = nct6775_write_value(data, data->REG_CRITICAL_TEMP[nr],
					  data->auto_temp[nr][point]);
	}
	mutex_unlock(&data->update_lock);
	return err ? : count;
}

static umode_t nct6775_pwm_is_visible(struct kobject *kobj,
				      struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct6775_data *data = dev_get_drvdata(dev);
	int pwm = index / 36;	/* pwm index */
	int nr = index % 36;	/* attribute index */

	if (!(data->has_pwm & BIT(pwm)))
		return 0;

	if ((nr >= 14 && nr <= 18) || nr == 21)   /* weight */
		if (!data->REG_WEIGHT_TEMP_SEL[pwm])
			return 0;
	if (nr == 19 && data->REG_PWM[3] == NULL) /* pwm_max */
		return 0;
	if (nr == 20 && data->REG_PWM[4] == NULL) /* pwm_step */
		return 0;
	if (nr == 21 && data->REG_PWM[6] == NULL) /* weight_duty_base */
		return 0;

	if (nr >= 22 && nr <= 35) {		/* auto point */
		int api = (nr - 22) / 2;	/* auto point index */

		if (api > data->auto_pwm_num)
			return 0;
	}
	return nct6775_attr_mode(data, attr);
}

SENSOR_TEMPLATE_2(pwm_stop_time, "pwm%d_stop_time", 0644, show_fan_time, store_fan_time, 0, 0);
SENSOR_TEMPLATE_2(pwm_step_up_time, "pwm%d_step_up_time", 0644,
		  show_fan_time, store_fan_time, 0, 1);
SENSOR_TEMPLATE_2(pwm_step_down_time, "pwm%d_step_down_time", 0644,
		  show_fan_time, store_fan_time, 0, 2);
SENSOR_TEMPLATE_2(pwm_start, "pwm%d_start", 0644, show_pwm, store_pwm, 0, 1);
SENSOR_TEMPLATE_2(pwm_floor, "pwm%d_floor", 0644, show_pwm, store_pwm, 0, 2);
SENSOR_TEMPLATE_2(pwm_temp_tolerance, "pwm%d_temp_tolerance", 0644,
		  show_temp_tolerance, store_temp_tolerance, 0, 0);
SENSOR_TEMPLATE_2(pwm_crit_temp_tolerance, "pwm%d_crit_temp_tolerance",
		  0644, show_temp_tolerance, store_temp_tolerance, 0, 1);

SENSOR_TEMPLATE_2(pwm_max, "pwm%d_max", 0644, show_pwm, store_pwm, 0, 3);

SENSOR_TEMPLATE_2(pwm_step, "pwm%d_step", 0644, show_pwm, store_pwm, 0, 4);

SENSOR_TEMPLATE_2(pwm_auto_point1_pwm, "pwm%d_auto_point1_pwm",
		  0644, show_auto_pwm, store_auto_pwm, 0, 0);
SENSOR_TEMPLATE_2(pwm_auto_point1_temp, "pwm%d_auto_point1_temp",
		  0644, show_auto_temp, store_auto_temp, 0, 0);

SENSOR_TEMPLATE_2(pwm_auto_point2_pwm, "pwm%d_auto_point2_pwm",
		  0644, show_auto_pwm, store_auto_pwm, 0, 1);
SENSOR_TEMPLATE_2(pwm_auto_point2_temp, "pwm%d_auto_point2_temp",
		  0644, show_auto_temp, store_auto_temp, 0, 1);

SENSOR_TEMPLATE_2(pwm_auto_point3_pwm, "pwm%d_auto_point3_pwm",
		  0644, show_auto_pwm, store_auto_pwm, 0, 2);
SENSOR_TEMPLATE_2(pwm_auto_point3_temp, "pwm%d_auto_point3_temp",
		  0644, show_auto_temp, store_auto_temp, 0, 2);

SENSOR_TEMPLATE_2(pwm_auto_point4_pwm, "pwm%d_auto_point4_pwm",
		  0644, show_auto_pwm, store_auto_pwm, 0, 3);
SENSOR_TEMPLATE_2(pwm_auto_point4_temp, "pwm%d_auto_point4_temp",
		  0644, show_auto_temp, store_auto_temp, 0, 3);

SENSOR_TEMPLATE_2(pwm_auto_point5_pwm, "pwm%d_auto_point5_pwm",
		  0644, show_auto_pwm, store_auto_pwm, 0, 4);
SENSOR_TEMPLATE_2(pwm_auto_point5_temp, "pwm%d_auto_point5_temp",
		  0644, show_auto_temp, store_auto_temp, 0, 4);

SENSOR_TEMPLATE_2(pwm_auto_point6_pwm, "pwm%d_auto_point6_pwm",
		  0644, show_auto_pwm, store_auto_pwm, 0, 5);
SENSOR_TEMPLATE_2(pwm_auto_point6_temp, "pwm%d_auto_point6_temp",
		  0644, show_auto_temp, store_auto_temp, 0, 5);

SENSOR_TEMPLATE_2(pwm_auto_point7_pwm, "pwm%d_auto_point7_pwm",
		  0644, show_auto_pwm, store_auto_pwm, 0, 6);
SENSOR_TEMPLATE_2(pwm_auto_point7_temp, "pwm%d_auto_point7_temp",
		  0644, show_auto_temp, store_auto_temp, 0, 6);

/*
 * nct6775_pwm_is_visible uses the index into the following array
 * to determine if attributes should be created or not.
 * Any change in order or content must be matched.
 */
static struct sensor_device_template *nct6775_attributes_pwm_template[] = {
	&sensor_dev_template_pwm,
	&sensor_dev_template_pwm_mode,
	&sensor_dev_template_pwm_enable,
	&sensor_dev_template_pwm_temp_sel,
	&sensor_dev_template_pwm_temp_tolerance,
	&sensor_dev_template_pwm_crit_temp_tolerance,
	&sensor_dev_template_pwm_target_temp,
	&sensor_dev_template_fan_target,
	&sensor_dev_template_fan_tolerance,
	&sensor_dev_template_pwm_stop_time,
	&sensor_dev_template_pwm_step_up_time,
	&sensor_dev_template_pwm_step_down_time,
	&sensor_dev_template_pwm_start,
	&sensor_dev_template_pwm_floor,
	&sensor_dev_template_pwm_weight_temp_sel,	/* 14 */
	&sensor_dev_template_pwm_weight_temp_step,
	&sensor_dev_template_pwm_weight_temp_step_tol,
	&sensor_dev_template_pwm_weight_temp_step_base,
	&sensor_dev_template_pwm_weight_duty_step,	/* 18 */
	&sensor_dev_template_pwm_max,			/* 19 */
	&sensor_dev_template_pwm_step,			/* 20 */
	&sensor_dev_template_pwm_weight_duty_base,	/* 21 */
	&sensor_dev_template_pwm_auto_point1_pwm,	/* 22 */
	&sensor_dev_template_pwm_auto_point1_temp,
	&sensor_dev_template_pwm_auto_point2_pwm,
	&sensor_dev_template_pwm_auto_point2_temp,
	&sensor_dev_template_pwm_auto_point3_pwm,
	&sensor_dev_template_pwm_auto_point3_temp,
	&sensor_dev_template_pwm_auto_point4_pwm,
	&sensor_dev_template_pwm_auto_point4_temp,
	&sensor_dev_template_pwm_auto_point5_pwm,
	&sensor_dev_template_pwm_auto_point5_temp,
	&sensor_dev_template_pwm_auto_point6_pwm,
	&sensor_dev_template_pwm_auto_point6_temp,
	&sensor_dev_template_pwm_auto_point7_pwm,
	&sensor_dev_template_pwm_auto_point7_temp,	/* 35 */

	NULL
};

static const struct sensor_template_group nct6775_pwm_template_group = {
	.templates = nct6775_attributes_pwm_template,
	.is_visible = nct6775_pwm_is_visible,
	.base = 1,
};

static inline int nct6775_init_device(struct nct6775_data *data)
{
	int i, err;
	u16 tmp, diode;

	/* Start monitoring if needed */
	if (data->REG_CONFIG) {
		err = nct6775_read_value(data, data->REG_CONFIG, &tmp);
		if (err)
			return err;
		if (!(tmp & 0x01)) {
			err = nct6775_write_value(data, data->REG_CONFIG, tmp | 0x01);
			if (err)
				return err;
		}
	}

	/* Enable temperature sensors if needed */
	for (i = 0; i < NUM_TEMP; i++) {
		if (!(data->have_temp & BIT(i)))
			continue;
		if (!data->reg_temp_config[i])
			continue;
		err = nct6775_read_value(data, data->reg_temp_config[i], &tmp);
		if (err)
			return err;
		if (tmp & 0x01) {
			err = nct6775_write_value(data, data->reg_temp_config[i], tmp & 0xfe);
			if (err)
				return err;
		}
	}

	/* Enable VBAT monitoring if needed */
	err = nct6775_read_value(data, data->REG_VBAT, &tmp);
	if (err)
		return err;
	if (!(tmp & 0x01)) {
		err = nct6775_write_value(data, data->REG_VBAT, tmp | 0x01);
		if (err)
			return err;
	}

	err = nct6775_read_value(data, data->REG_DIODE, &diode);
	if (err)
		return err;

	for (i = 0; i < data->temp_fixed_num; i++) {
		if (!(data->have_temp_fixed & BIT(i)))
			continue;
		if ((tmp & (data->DIODE_MASK << i)))	/* diode */
			data->temp_type[i]
			  = 3 - ((diode >> i) & data->DIODE_MASK);
		else				/* thermistor */
			data->temp_type[i] = 4;
	}

	return 0;
}

static int add_temp_sensors(struct nct6775_data *data, const u16 *regp,
			    int *available, int *mask)
{
	int i, err;
	u16 src;

	for (i = 0; i < data->pwm_num && *available; i++) {
		int index;

		if (!regp[i])
			continue;
		err = nct6775_read_value(data, regp[i], &src);
		if (err)
			return err;
		src &= 0x1f;
		if (!src || (*mask & BIT(src)))
			continue;
		if (!(data->temp_mask & BIT(src)))
			continue;

		index = __ffs(*available);
		err = nct6775_write_value(data, data->REG_TEMP_SOURCE[index], src);
		if (err)
			return err;
		*available &= ~BIT(index);
		*mask |= BIT(src);
	}

	return 0;
}

int nct6775_probe(struct device *dev, struct nct6775_data *data,
		  const struct regmap_config *regmapcfg)
{
	int i, s, err = 0;
	int mask, available;
	u16 src;
	const u16 *reg_temp, *reg_temp_over, *reg_temp_hyst, *reg_temp_config;
	const u16 *reg_temp_mon, *reg_temp_alternate, *reg_temp_crit;
	const u16 *reg_temp_crit_l = NULL, *reg_temp_crit_h = NULL;
	int num_reg_temp, num_reg_temp_mon, num_reg_tsi_temp;
	struct device *hwmon_dev;
	struct sensor_template_group tsi_temp_tg;

	data->regmap = devm_regmap_init(dev, NULL, data, regmapcfg);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	mutex_init(&data->update_lock);
	data->name = nct6775_device_names[data->kind];
	data->bank = 0xff;		/* Force initial bank selection */

	switch (data->kind) {
	case nct6106:
		data->in_num = 9;
		data->pwm_num = 3;
		data->auto_pwm_num = 4;
		data->temp_fixed_num = 3;
		data->num_temp_alarms = 6;
		data->num_temp_beeps = 6;

		data->fan_from_reg = fan_from_reg13;
		data->fan_from_reg_min = fan_from_reg13;

		data->temp_label = nct6776_temp_label;
		data->temp_mask = NCT6776_TEMP_MASK;
		data->virt_temp_mask = NCT6776_VIRT_TEMP_MASK;

		data->REG_VBAT = NCT6106_REG_VBAT;
		data->REG_DIODE = NCT6106_REG_DIODE;
		data->DIODE_MASK = NCT6106_DIODE_MASK;
		data->REG_VIN = NCT6106_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6106_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6106_REG_IN_MAX;
		data->REG_TARGET = NCT6106_REG_TARGET;
		data->REG_FAN = NCT6106_REG_FAN;
		data->REG_FAN_MODE = NCT6106_REG_FAN_MODE;
		data->REG_FAN_MIN = NCT6106_REG_FAN_MIN;
		data->REG_FAN_PULSES = NCT6106_REG_FAN_PULSES;
		data->FAN_PULSE_SHIFT = NCT6106_FAN_PULSE_SHIFT;
		data->REG_FAN_TIME[0] = NCT6106_REG_FAN_STOP_TIME;
		data->REG_FAN_TIME[1] = NCT6106_REG_FAN_STEP_UP_TIME;
		data->REG_FAN_TIME[2] = NCT6106_REG_FAN_STEP_DOWN_TIME;
		data->REG_TOLERANCE_H = NCT6106_REG_TOLERANCE_H;
		data->REG_PWM[0] = NCT6116_REG_PWM;
		data->REG_PWM[1] = NCT6106_REG_FAN_START_OUTPUT;
		data->REG_PWM[2] = NCT6106_REG_FAN_STOP_OUTPUT;
		data->REG_PWM[5] = NCT6106_REG_WEIGHT_DUTY_STEP;
		data->REG_PWM[6] = NCT6106_REG_WEIGHT_DUTY_BASE;
		data->REG_PWM_READ = NCT6106_REG_PWM_READ;
		data->REG_PWM_MODE = NCT6106_REG_PWM_MODE;
		data->PWM_MODE_MASK = NCT6106_PWM_MODE_MASK;
		data->REG_AUTO_TEMP = NCT6106_REG_AUTO_TEMP;
		data->REG_AUTO_PWM = NCT6106_REG_AUTO_PWM;
		data->REG_CRITICAL_TEMP = NCT6106_REG_CRITICAL_TEMP;
		data->REG_CRITICAL_TEMP_TOLERANCE
		  = NCT6106_REG_CRITICAL_TEMP_TOLERANCE;
		data->REG_CRITICAL_PWM_ENABLE = NCT6106_REG_CRITICAL_PWM_ENABLE;
		data->CRITICAL_PWM_ENABLE_MASK
		  = NCT6106_CRITICAL_PWM_ENABLE_MASK;
		data->REG_CRITICAL_PWM = NCT6106_REG_CRITICAL_PWM;
		data->REG_TEMP_OFFSET = NCT6106_REG_TEMP_OFFSET;
		data->REG_TEMP_SOURCE = NCT6106_REG_TEMP_SOURCE;
		data->REG_TEMP_SEL = NCT6116_REG_TEMP_SEL;
		data->REG_WEIGHT_TEMP_SEL = NCT6106_REG_WEIGHT_TEMP_SEL;
		data->REG_WEIGHT_TEMP[0] = NCT6106_REG_WEIGHT_TEMP_STEP;
		data->REG_WEIGHT_TEMP[1] = NCT6106_REG_WEIGHT_TEMP_STEP_TOL;
		data->REG_WEIGHT_TEMP[2] = NCT6106_REG_WEIGHT_TEMP_BASE;
		data->REG_ALARM = NCT6106_REG_ALARM;
		data->ALARM_BITS = NCT6106_ALARM_BITS;
		data->REG_BEEP = NCT6106_REG_BEEP;
		data->BEEP_BITS = NCT6106_BEEP_BITS;
		data->REG_TSI_TEMP = NCT6106_REG_TSI_TEMP;

		reg_temp = NCT6106_REG_TEMP;
		reg_temp_mon = NCT6106_REG_TEMP_MON;
		num_reg_temp = ARRAY_SIZE(NCT6106_REG_TEMP);
		num_reg_temp_mon = ARRAY_SIZE(NCT6106_REG_TEMP_MON);
		num_reg_tsi_temp = ARRAY_SIZE(NCT6106_REG_TSI_TEMP);
		reg_temp_over = NCT6106_REG_TEMP_OVER;
		reg_temp_hyst = NCT6106_REG_TEMP_HYST;
		reg_temp_config = NCT6106_REG_TEMP_CONFIG;
		reg_temp_alternate = NCT6106_REG_TEMP_ALTERNATE;
		reg_temp_crit = NCT6106_REG_TEMP_CRIT;
		reg_temp_crit_l = NCT6106_REG_TEMP_CRIT_L;
		reg_temp_crit_h = NCT6106_REG_TEMP_CRIT_H;

		break;
	case nct6116:
		data->in_num = 9;
		data->pwm_num = 3;
		data->auto_pwm_num = 4;
		data->temp_fixed_num = 3;
		data->num_temp_alarms = 3;
		data->num_temp_beeps = 3;

		data->fan_from_reg = fan_from_reg13;
		data->fan_from_reg_min = fan_from_reg13;

		data->temp_label = nct6776_temp_label;
		data->temp_mask = NCT6776_TEMP_MASK;
		data->virt_temp_mask = NCT6776_VIRT_TEMP_MASK;

		data->REG_VBAT = NCT6106_REG_VBAT;
		data->REG_DIODE = NCT6106_REG_DIODE;
		data->DIODE_MASK = NCT6106_DIODE_MASK;
		data->REG_VIN = NCT6106_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6106_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6106_REG_IN_MAX;
		data->REG_TARGET = NCT6116_REG_TARGET;
		data->REG_FAN = NCT6116_REG_FAN;
		data->REG_FAN_MODE = NCT6116_REG_FAN_MODE;
		data->REG_FAN_MIN = NCT6116_REG_FAN_MIN;
		data->REG_FAN_PULSES = NCT6116_REG_FAN_PULSES;
		data->FAN_PULSE_SHIFT = NCT6116_FAN_PULSE_SHIFT;
		data->REG_FAN_TIME[0] = NCT6116_REG_FAN_STOP_TIME;
		data->REG_FAN_TIME[1] = NCT6116_REG_FAN_STEP_UP_TIME;
		data->REG_FAN_TIME[2] = NCT6116_REG_FAN_STEP_DOWN_TIME;
		data->REG_TOLERANCE_H = NCT6116_REG_TOLERANCE_H;
		data->REG_PWM[0] = NCT6116_REG_PWM;
		data->REG_PWM[1] = NCT6116_REG_FAN_START_OUTPUT;
		data->REG_PWM[2] = NCT6116_REG_FAN_STOP_OUTPUT;
		data->REG_PWM[5] = NCT6106_REG_WEIGHT_DUTY_STEP;
		data->REG_PWM[6] = NCT6106_REG_WEIGHT_DUTY_BASE;
		data->REG_PWM_READ = NCT6106_REG_PWM_READ;
		data->REG_PWM_MODE = NCT6106_REG_PWM_MODE;
		data->PWM_MODE_MASK = NCT6106_PWM_MODE_MASK;
		data->REG_AUTO_TEMP = NCT6116_REG_AUTO_TEMP;
		data->REG_AUTO_PWM = NCT6116_REG_AUTO_PWM;
		data->REG_CRITICAL_TEMP = NCT6116_REG_CRITICAL_TEMP;
		data->REG_CRITICAL_TEMP_TOLERANCE
		  = NCT6116_REG_CRITICAL_TEMP_TOLERANCE;
		data->REG_CRITICAL_PWM_ENABLE = NCT6116_REG_CRITICAL_PWM_ENABLE;
		data->CRITICAL_PWM_ENABLE_MASK
		  = NCT6106_CRITICAL_PWM_ENABLE_MASK;
		data->REG_CRITICAL_PWM = NCT6116_REG_CRITICAL_PWM;
		data->REG_TEMP_OFFSET = NCT6106_REG_TEMP_OFFSET;
		data->REG_TEMP_SOURCE = NCT6116_REG_TEMP_SOURCE;
		data->REG_TEMP_SEL = NCT6116_REG_TEMP_SEL;
		data->REG_WEIGHT_TEMP_SEL = NCT6106_REG_WEIGHT_TEMP_SEL;
		data->REG_WEIGHT_TEMP[0] = NCT6106_REG_WEIGHT_TEMP_STEP;
		data->REG_WEIGHT_TEMP[1] = NCT6106_REG_WEIGHT_TEMP_STEP_TOL;
		data->REG_WEIGHT_TEMP[2] = NCT6106_REG_WEIGHT_TEMP_BASE;
		data->REG_ALARM = NCT6106_REG_ALARM;
		data->ALARM_BITS = NCT6116_ALARM_BITS;
		data->REG_BEEP = NCT6106_REG_BEEP;
		data->BEEP_BITS = NCT6116_BEEP_BITS;
		data->REG_TSI_TEMP = NCT6116_REG_TSI_TEMP;

		reg_temp = NCT6106_REG_TEMP;
		reg_temp_mon = NCT6106_REG_TEMP_MON;
		num_reg_temp = ARRAY_SIZE(NCT6106_REG_TEMP);
		num_reg_temp_mon = ARRAY_SIZE(NCT6106_REG_TEMP_MON);
		num_reg_tsi_temp = ARRAY_SIZE(NCT6116_REG_TSI_TEMP);
		reg_temp_over = NCT6106_REG_TEMP_OVER;
		reg_temp_hyst = NCT6106_REG_TEMP_HYST;
		reg_temp_config = NCT6106_REG_TEMP_CONFIG;
		reg_temp_alternate = NCT6106_REG_TEMP_ALTERNATE;
		reg_temp_crit = NCT6106_REG_TEMP_CRIT;
		reg_temp_crit_l = NCT6106_REG_TEMP_CRIT_L;
		reg_temp_crit_h = NCT6106_REG_TEMP_CRIT_H;

		break;
	case nct6775:
		data->in_num = 9;
		data->pwm_num = 3;
		data->auto_pwm_num = 6;
		data->has_fan_div = true;
		data->temp_fixed_num = 3;
		data->num_temp_alarms = 3;
		data->num_temp_beeps = 3;

		data->ALARM_BITS = NCT6775_ALARM_BITS;
		data->BEEP_BITS = NCT6775_BEEP_BITS;

		data->fan_from_reg = fan_from_reg16;
		data->fan_from_reg_min = fan_from_reg8;
		data->target_temp_mask = 0x7f;
		data->tolerance_mask = 0x0f;
		data->speed_tolerance_limit = 15;

		data->temp_label = nct6775_temp_label;
		data->temp_mask = NCT6775_TEMP_MASK;
		data->virt_temp_mask = NCT6775_VIRT_TEMP_MASK;

		data->REG_CONFIG = NCT6775_REG_CONFIG;
		data->REG_VBAT = NCT6775_REG_VBAT;
		data->REG_DIODE = NCT6775_REG_DIODE;
		data->DIODE_MASK = NCT6775_DIODE_MASK;
		data->REG_VIN = NCT6775_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6775_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6775_REG_IN_MAX;
		data->REG_TARGET = NCT6775_REG_TARGET;
		data->REG_FAN = NCT6775_REG_FAN;
		data->REG_FAN_MODE = NCT6775_REG_FAN_MODE;
		data->REG_FAN_MIN = NCT6775_REG_FAN_MIN;
		data->REG_FAN_PULSES = NCT6775_REG_FAN_PULSES;
		data->FAN_PULSE_SHIFT = NCT6775_FAN_PULSE_SHIFT;
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
		data->REG_BEEP = NCT6775_REG_BEEP;
		data->REG_TSI_TEMP = NCT6775_REG_TSI_TEMP;

		reg_temp = NCT6775_REG_TEMP;
		reg_temp_mon = NCT6775_REG_TEMP_MON;
		num_reg_temp = ARRAY_SIZE(NCT6775_REG_TEMP);
		num_reg_temp_mon = ARRAY_SIZE(NCT6775_REG_TEMP_MON);
		num_reg_tsi_temp = ARRAY_SIZE(NCT6775_REG_TSI_TEMP);
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
		data->num_temp_alarms = 3;
		data->num_temp_beeps = 6;

		data->ALARM_BITS = NCT6776_ALARM_BITS;
		data->BEEP_BITS = NCT6776_BEEP_BITS;

		data->fan_from_reg = fan_from_reg13;
		data->fan_from_reg_min = fan_from_reg13;
		data->target_temp_mask = 0xff;
		data->tolerance_mask = 0x07;
		data->speed_tolerance_limit = 63;

		data->temp_label = nct6776_temp_label;
		data->temp_mask = NCT6776_TEMP_MASK;
		data->virt_temp_mask = NCT6776_VIRT_TEMP_MASK;

		data->REG_CONFIG = NCT6775_REG_CONFIG;
		data->REG_VBAT = NCT6775_REG_VBAT;
		data->REG_DIODE = NCT6775_REG_DIODE;
		data->DIODE_MASK = NCT6775_DIODE_MASK;
		data->REG_VIN = NCT6775_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6775_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6775_REG_IN_MAX;
		data->REG_TARGET = NCT6775_REG_TARGET;
		data->REG_FAN = NCT6775_REG_FAN;
		data->REG_FAN_MODE = NCT6775_REG_FAN_MODE;
		data->REG_FAN_MIN = NCT6776_REG_FAN_MIN;
		data->REG_FAN_PULSES = NCT6776_REG_FAN_PULSES;
		data->FAN_PULSE_SHIFT = NCT6775_FAN_PULSE_SHIFT;
		data->REG_FAN_TIME[0] = NCT6775_REG_FAN_STOP_TIME;
		data->REG_FAN_TIME[1] = NCT6776_REG_FAN_STEP_UP_TIME;
		data->REG_FAN_TIME[2] = NCT6776_REG_FAN_STEP_DOWN_TIME;
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
		data->REG_BEEP = NCT6776_REG_BEEP;
		data->REG_TSI_TEMP = NCT6776_REG_TSI_TEMP;

		reg_temp = NCT6775_REG_TEMP;
		reg_temp_mon = NCT6775_REG_TEMP_MON;
		num_reg_temp = ARRAY_SIZE(NCT6775_REG_TEMP);
		num_reg_temp_mon = ARRAY_SIZE(NCT6775_REG_TEMP_MON);
		num_reg_tsi_temp = ARRAY_SIZE(NCT6776_REG_TSI_TEMP);
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
		data->num_temp_alarms = 2;
		data->num_temp_beeps = 2;

		data->ALARM_BITS = NCT6779_ALARM_BITS;
		data->BEEP_BITS = NCT6779_BEEP_BITS;

		data->fan_from_reg = fan_from_reg_rpm;
		data->fan_from_reg_min = fan_from_reg13;
		data->target_temp_mask = 0xff;
		data->tolerance_mask = 0x07;
		data->speed_tolerance_limit = 63;

		data->temp_label = nct6779_temp_label;
		data->temp_mask = NCT6779_TEMP_MASK;
		data->virt_temp_mask = NCT6779_VIRT_TEMP_MASK;

		data->REG_CONFIG = NCT6775_REG_CONFIG;
		data->REG_VBAT = NCT6775_REG_VBAT;
		data->REG_DIODE = NCT6775_REG_DIODE;
		data->DIODE_MASK = NCT6775_DIODE_MASK;
		data->REG_VIN = NCT6779_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6775_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6775_REG_IN_MAX;
		data->REG_TARGET = NCT6775_REG_TARGET;
		data->REG_FAN = NCT6779_REG_FAN;
		data->REG_FAN_MODE = NCT6775_REG_FAN_MODE;
		data->REG_FAN_MIN = NCT6776_REG_FAN_MIN;
		data->REG_FAN_PULSES = NCT6779_REG_FAN_PULSES;
		data->FAN_PULSE_SHIFT = NCT6775_FAN_PULSE_SHIFT;
		data->REG_FAN_TIME[0] = NCT6775_REG_FAN_STOP_TIME;
		data->REG_FAN_TIME[1] = NCT6776_REG_FAN_STEP_UP_TIME;
		data->REG_FAN_TIME[2] = NCT6776_REG_FAN_STEP_DOWN_TIME;
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
		data->REG_CRITICAL_PWM_ENABLE = NCT6779_REG_CRITICAL_PWM_ENABLE;
		data->CRITICAL_PWM_ENABLE_MASK
		  = NCT6779_CRITICAL_PWM_ENABLE_MASK;
		data->REG_CRITICAL_PWM = NCT6779_REG_CRITICAL_PWM;
		data->REG_TEMP_OFFSET = NCT6779_REG_TEMP_OFFSET;
		data->REG_TEMP_SOURCE = NCT6775_REG_TEMP_SOURCE;
		data->REG_TEMP_SEL = NCT6775_REG_TEMP_SEL;
		data->REG_WEIGHT_TEMP_SEL = NCT6775_REG_WEIGHT_TEMP_SEL;
		data->REG_WEIGHT_TEMP[0] = NCT6775_REG_WEIGHT_TEMP_STEP;
		data->REG_WEIGHT_TEMP[1] = NCT6775_REG_WEIGHT_TEMP_STEP_TOL;
		data->REG_WEIGHT_TEMP[2] = NCT6775_REG_WEIGHT_TEMP_BASE;
		data->REG_ALARM = NCT6779_REG_ALARM;
		data->REG_BEEP = NCT6776_REG_BEEP;
		data->REG_TSI_TEMP = NCT6776_REG_TSI_TEMP;

		reg_temp = NCT6779_REG_TEMP;
		reg_temp_mon = NCT6779_REG_TEMP_MON;
		num_reg_temp = ARRAY_SIZE(NCT6779_REG_TEMP);
		num_reg_temp_mon = ARRAY_SIZE(NCT6779_REG_TEMP_MON);
		num_reg_tsi_temp = ARRAY_SIZE(NCT6776_REG_TSI_TEMP);
		reg_temp_over = NCT6779_REG_TEMP_OVER;
		reg_temp_hyst = NCT6779_REG_TEMP_HYST;
		reg_temp_config = NCT6779_REG_TEMP_CONFIG;
		reg_temp_alternate = NCT6779_REG_TEMP_ALTERNATE;
		reg_temp_crit = NCT6779_REG_TEMP_CRIT;

		break;
	case nct6791:
	case nct6792:
	case nct6793:
	case nct6795:
	case nct6796:
	case nct6797:
	case nct6798:
		data->in_num = 15;
		data->pwm_num = (data->kind == nct6796 ||
				 data->kind == nct6797 ||
				 data->kind == nct6798) ? 7 : 6;
		data->auto_pwm_num = 4;
		data->has_fan_div = false;
		data->temp_fixed_num = 6;
		data->num_temp_alarms = 2;
		data->num_temp_beeps = 2;

		data->ALARM_BITS = NCT6791_ALARM_BITS;
		data->BEEP_BITS = NCT6779_BEEP_BITS;

		data->fan_from_reg = fan_from_reg_rpm;
		data->fan_from_reg_min = fan_from_reg13;
		data->target_temp_mask = 0xff;
		data->tolerance_mask = 0x07;
		data->speed_tolerance_limit = 63;

		switch (data->kind) {
		default:
		case nct6791:
			data->temp_label = nct6779_temp_label;
			data->temp_mask = NCT6791_TEMP_MASK;
			data->virt_temp_mask = NCT6791_VIRT_TEMP_MASK;
			break;
		case nct6792:
			data->temp_label = nct6792_temp_label;
			data->temp_mask = NCT6792_TEMP_MASK;
			data->virt_temp_mask = NCT6792_VIRT_TEMP_MASK;
			break;
		case nct6793:
			data->temp_label = nct6793_temp_label;
			data->temp_mask = NCT6793_TEMP_MASK;
			data->virt_temp_mask = NCT6793_VIRT_TEMP_MASK;
			break;
		case nct6795:
		case nct6797:
			data->temp_label = nct6795_temp_label;
			data->temp_mask = NCT6795_TEMP_MASK;
			data->virt_temp_mask = NCT6795_VIRT_TEMP_MASK;
			break;
		case nct6796:
			data->temp_label = nct6796_temp_label;
			data->temp_mask = NCT6796_TEMP_MASK;
			data->virt_temp_mask = NCT6796_VIRT_TEMP_MASK;
			break;
		case nct6798:
			data->temp_label = nct6798_temp_label;
			data->temp_mask = NCT6798_TEMP_MASK;
			data->virt_temp_mask = NCT6798_VIRT_TEMP_MASK;
			break;
		}

		data->REG_CONFIG = NCT6775_REG_CONFIG;
		data->REG_VBAT = NCT6775_REG_VBAT;
		data->REG_DIODE = NCT6775_REG_DIODE;
		data->DIODE_MASK = NCT6775_DIODE_MASK;
		data->REG_VIN = NCT6779_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6775_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6775_REG_IN_MAX;
		data->REG_TARGET = NCT6775_REG_TARGET;
		data->REG_FAN = NCT6779_REG_FAN;
		data->REG_FAN_MODE = NCT6775_REG_FAN_MODE;
		data->REG_FAN_MIN = NCT6776_REG_FAN_MIN;
		data->REG_FAN_PULSES = NCT6779_REG_FAN_PULSES;
		data->FAN_PULSE_SHIFT = NCT6775_FAN_PULSE_SHIFT;
		data->REG_FAN_TIME[0] = NCT6775_REG_FAN_STOP_TIME;
		data->REG_FAN_TIME[1] = NCT6776_REG_FAN_STEP_UP_TIME;
		data->REG_FAN_TIME[2] = NCT6776_REG_FAN_STEP_DOWN_TIME;
		data->REG_TOLERANCE_H = NCT6776_REG_TOLERANCE_H;
		data->REG_PWM[0] = NCT6775_REG_PWM;
		data->REG_PWM[1] = NCT6775_REG_FAN_START_OUTPUT;
		data->REG_PWM[2] = NCT6775_REG_FAN_STOP_OUTPUT;
		data->REG_PWM[5] = NCT6791_REG_WEIGHT_DUTY_STEP;
		data->REG_PWM[6] = NCT6791_REG_WEIGHT_DUTY_BASE;
		data->REG_PWM_READ = NCT6775_REG_PWM_READ;
		data->REG_PWM_MODE = NCT6776_REG_PWM_MODE;
		data->PWM_MODE_MASK = NCT6776_PWM_MODE_MASK;
		data->REG_AUTO_TEMP = NCT6775_REG_AUTO_TEMP;
		data->REG_AUTO_PWM = NCT6775_REG_AUTO_PWM;
		data->REG_CRITICAL_TEMP = NCT6775_REG_CRITICAL_TEMP;
		data->REG_CRITICAL_TEMP_TOLERANCE
		  = NCT6775_REG_CRITICAL_TEMP_TOLERANCE;
		data->REG_CRITICAL_PWM_ENABLE = NCT6779_REG_CRITICAL_PWM_ENABLE;
		data->CRITICAL_PWM_ENABLE_MASK
		  = NCT6779_CRITICAL_PWM_ENABLE_MASK;
		data->REG_CRITICAL_PWM = NCT6779_REG_CRITICAL_PWM;
		data->REG_TEMP_OFFSET = NCT6779_REG_TEMP_OFFSET;
		data->REG_TEMP_SOURCE = NCT6775_REG_TEMP_SOURCE;
		data->REG_TEMP_SEL = NCT6775_REG_TEMP_SEL;
		data->REG_WEIGHT_TEMP_SEL = NCT6791_REG_WEIGHT_TEMP_SEL;
		data->REG_WEIGHT_TEMP[0] = NCT6791_REG_WEIGHT_TEMP_STEP;
		data->REG_WEIGHT_TEMP[1] = NCT6791_REG_WEIGHT_TEMP_STEP_TOL;
		data->REG_WEIGHT_TEMP[2] = NCT6791_REG_WEIGHT_TEMP_BASE;
		data->REG_ALARM = NCT6791_REG_ALARM;
		if (data->kind == nct6791)
			data->REG_BEEP = NCT6776_REG_BEEP;
		else
			data->REG_BEEP = NCT6792_REG_BEEP;
		switch (data->kind) {
		case nct6791:
		case nct6792:
		case nct6793:
			data->REG_TSI_TEMP = NCT6776_REG_TSI_TEMP;
			num_reg_tsi_temp = ARRAY_SIZE(NCT6776_REG_TSI_TEMP);
			break;
		case nct6795:
		case nct6796:
		case nct6797:
		case nct6798:
			data->REG_TSI_TEMP = NCT6796_REG_TSI_TEMP;
			num_reg_tsi_temp = ARRAY_SIZE(NCT6796_REG_TSI_TEMP);
			break;
		default:
			num_reg_tsi_temp = 0;
			break;
		}

		reg_temp = NCT6779_REG_TEMP;
		num_reg_temp = ARRAY_SIZE(NCT6779_REG_TEMP);
		if (data->kind == nct6791) {
			reg_temp_mon = NCT6779_REG_TEMP_MON;
			num_reg_temp_mon = ARRAY_SIZE(NCT6779_REG_TEMP_MON);
		} else {
			reg_temp_mon = NCT6792_REG_TEMP_MON;
			num_reg_temp_mon = ARRAY_SIZE(NCT6792_REG_TEMP_MON);
		}
		reg_temp_over = NCT6779_REG_TEMP_OVER;
		reg_temp_hyst = NCT6779_REG_TEMP_HYST;
		reg_temp_config = NCT6779_REG_TEMP_CONFIG;
		reg_temp_alternate = NCT6779_REG_TEMP_ALTERNATE;
		reg_temp_crit = NCT6779_REG_TEMP_CRIT;

		break;
	default:
		return -ENODEV;
	}
	data->have_in = BIT(data->in_num) - 1;
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

		err = nct6775_read_value(data, data->REG_TEMP_SOURCE[i], &src);
		if (err)
			return err;
		src &= 0x1f;
		if (!src || (mask & BIT(src)))
			available |= BIT(i);

		mask |= BIT(src);
	}

	/*
	 * Now find unmonitored temperature registers and enable monitoring
	 * if additional monitoring registers are available.
	 */
	err = add_temp_sensors(data, data->REG_TEMP_SEL, &available, &mask);
	if (err)
		return err;
	err = add_temp_sensors(data, data->REG_WEIGHT_TEMP_SEL, &available, &mask);
	if (err)
		return err;

	mask = 0;
	s = NUM_TEMP_FIXED;	/* First dynamic temperature attribute */
	for (i = 0; i < num_reg_temp; i++) {
		if (reg_temp[i] == 0)
			continue;

		err = nct6775_read_value(data, data->REG_TEMP_SOURCE[i], &src);
		if (err)
			return err;
		src &= 0x1f;
		if (!src || (mask & BIT(src)))
			continue;

		if (!(data->temp_mask & BIT(src))) {
			dev_info(dev,
				 "Invalid temperature source %d at index %d, source register 0x%x, temp register 0x%x\n",
				 src, i, data->REG_TEMP_SOURCE[i], reg_temp[i]);
			continue;
		}

		mask |= BIT(src);

		/* Use fixed index for SYSTIN(1), CPUTIN(2), AUXTIN(3) */
		if (src <= data->temp_fixed_num) {
			data->have_temp |= BIT(src - 1);
			data->have_temp_fixed |= BIT(src - 1);
			data->reg_temp[0][src - 1] = reg_temp[i];
			data->reg_temp[1][src - 1] = reg_temp_over[i];
			data->reg_temp[2][src - 1] = reg_temp_hyst[i];
			if (reg_temp_crit_h && reg_temp_crit_h[i])
				data->reg_temp[3][src - 1] = reg_temp_crit_h[i];
			else if (reg_temp_crit[src - 1])
				data->reg_temp[3][src - 1]
				  = reg_temp_crit[src - 1];
			if (reg_temp_crit_l && reg_temp_crit_l[i])
				data->reg_temp[4][src - 1] = reg_temp_crit_l[i];
			data->reg_temp_config[src - 1] = reg_temp_config[i];
			data->temp_src[src - 1] = src;
			continue;
		}

		if (s >= NUM_TEMP)
			continue;

		/* Use dynamic index for other sources */
		data->have_temp |= BIT(s);
		data->reg_temp[0][s] = reg_temp[i];
		data->reg_temp[1][s] = reg_temp_over[i];
		data->reg_temp[2][s] = reg_temp_hyst[i];
		data->reg_temp_config[s] = reg_temp_config[i];
		if (reg_temp_crit_h && reg_temp_crit_h[i])
			data->reg_temp[3][s] = reg_temp_crit_h[i];
		else if (reg_temp_crit[src - 1])
			data->reg_temp[3][s] = reg_temp_crit[src - 1];
		if (reg_temp_crit_l && reg_temp_crit_l[i])
			data->reg_temp[4][s] = reg_temp_crit_l[i];

		data->temp_src[s] = src;
		s++;
	}

	/*
	 * Repeat with temperatures used for fan control.
	 * This set of registers does not support limits.
	 */
	for (i = 0; i < num_reg_temp_mon; i++) {
		if (reg_temp_mon[i] == 0)
			continue;

		err = nct6775_read_value(data, data->REG_TEMP_SEL[i], &src);
		if (err)
			return err;
		src &= 0x1f;
		if (!src)
			continue;

		if (!(data->temp_mask & BIT(src))) {
			dev_info(dev,
				 "Invalid temperature source %d at index %d, source register 0x%x, temp register 0x%x\n",
				 src, i, data->REG_TEMP_SEL[i],
				 reg_temp_mon[i]);
			continue;
		}

		/*
		 * For virtual temperature sources, the 'virtual' temperature
		 * for each fan reflects a different temperature, and there
		 * are no duplicates.
		 */
		if (!(data->virt_temp_mask & BIT(src))) {
			if (mask & BIT(src))
				continue;
			mask |= BIT(src);
		}

		/* Use fixed index for SYSTIN(1), CPUTIN(2), AUXTIN(3) */
		if (src <= data->temp_fixed_num) {
			if (data->have_temp & BIT(src - 1))
				continue;
			data->have_temp |= BIT(src - 1);
			data->have_temp_fixed |= BIT(src - 1);
			data->reg_temp[0][src - 1] = reg_temp_mon[i];
			data->temp_src[src - 1] = src;
			continue;
		}

		if (s >= NUM_TEMP)
			continue;

		/* Use dynamic index for other sources */
		data->have_temp |= BIT(s);
		data->reg_temp[0][s] = reg_temp_mon[i];
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
	for (i = 0; i < 31; i++) {
		if (!(data->temp_mask & BIT(i + 1)))
			continue;
		if (!reg_temp_alternate[i])
			continue;
		if (mask & BIT(i + 1))
			continue;
		if (i < data->temp_fixed_num) {
			if (data->have_temp & BIT(i))
				continue;
			data->have_temp |= BIT(i);
			data->have_temp_fixed |= BIT(i);
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

		data->have_temp |= BIT(s);
		data->reg_temp[0][s] = reg_temp_alternate[i];
		data->temp_src[s] = i + 1;
		s++;
	}
#endif /* USE_ALTERNATE */

	/* Check which TSIx_TEMP registers are active */
	for (i = 0; i < num_reg_tsi_temp; i++) {
		u16 tmp;

		err = nct6775_read_value(data, data->REG_TSI_TEMP[i], &tmp);
		if (err)
			return err;
		if (tmp)
			data->have_tsi_temp |= BIT(i);
	}

	/* Initialize the chip */
	err = nct6775_init_device(data);
	if (err)
		return err;

	if (data->driver_init) {
		err = data->driver_init(data);
		if (err)
			return err;
	}

	/* Read fan clock dividers immediately */
	err = nct6775_init_fan_common(dev, data);
	if (err)
		return err;

	/* Register sysfs hooks */
	err = nct6775_add_template_attr_group(dev, data, &nct6775_pwm_template_group,
					      data->pwm_num);
	if (err)
		return err;

	err = nct6775_add_template_attr_group(dev, data, &nct6775_in_template_group,
					      fls(data->have_in));
	if (err)
		return err;

	err = nct6775_add_template_attr_group(dev, data, &nct6775_fan_template_group,
					      fls(data->has_fan));
	if (err)
		return err;

	err = nct6775_add_template_attr_group(dev, data, &nct6775_temp_template_group,
					      fls(data->have_temp));
	if (err)
		return err;

	if (data->have_tsi_temp) {
		tsi_temp_tg.templates = nct6775_tsi_temp_template;
		tsi_temp_tg.is_visible = nct6775_tsi_temp_is_visible;
		tsi_temp_tg.base = fls(data->have_temp) + 1;
		err = nct6775_add_template_attr_group(dev, data, &tsi_temp_tg,
						      fls(data->have_tsi_temp));
		if (err)
			return err;
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, data->name,
							   data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}
EXPORT_SYMBOL_GPL(nct6775_probe);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("Core driver for NCT6775F and compatible chips");
MODULE_LICENSE("GPL");
