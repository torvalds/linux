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

#define TEMP_ALARM_BASE		24
#define INTRUSION_ALARM_BASE	30

static const u8 NCT6775_REG_CR_CASEOPEN_CLR[] = { 0xe6, 0xee };
static const u8 NCT6775_CR_CASEOPEN_CLR_MASK[] = { 0x20, 0x01 };

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

static const u16 NCT6775_REG_TEMP_OFFSET[] = { 0x454, 0x455, 0x456 };

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

/*
 * Conversions
 */

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
	struct mutex lock;

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

	const u16 *REG_TEMP_SOURCE;	/* temp register sources */

	const u16 *REG_TEMP_OFFSET;

	const u16 *REG_ALARM;

	struct mutex update_lock;
	bool valid;		/* true if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* Register values */
	u8 bank;		/* current register bank */
	u8 in_num;		/* number of in inputs we have */
	u8 in[15][3];		/* [0]=in, [1]=in_max, [2]=in_min */

	u8 temp_fixed_num;	/* 3 or 6 */
	u8 temp_type[NUM_TEMP_FIXED];
	s8 temp_offset[NUM_TEMP_FIXED];
	s16 temp[4][NUM_TEMP]; /* 0=temp, 1=temp_over, 2=temp_hyst,
				* 3=temp_crit */
	u64 alarms;

	u8 vid;
	u8 vrm;

	u16 have_temp;
	u16 have_temp_fixed;
	u16 have_in;
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

	mutex_lock(&data->lock);

	nct6775_set_bank(data, reg);
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

static int nct6775_write_value(struct nct6775_data *data, u16 reg, u16 value)
{
	int word_sized = is_word_sized(data, reg);

	mutex_lock(&data->lock);

	nct6775_set_bank(data, reg);
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

static struct nct6775_data *nct6775_update_device(struct device *dev)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	int i, j;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ/2)
	    || !data->valid) {
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

		/* Measured temperatures and limits */
		for (i = 0; i < NUM_TEMP; i++) {
			if (!(data->have_temp & (1 << i)))
				continue;
			for (j = 0; j < 4; j++) {
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
	nct6775_write_value(data, data->REG_IN_MINMAX[index-1][nr],
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
show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}

static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

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

	for (i = 0; i < data->in_num; i++)
		sysfs_remove_group(&dev->kobj, &nct6775_group_in[i]);

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
	mutex_init(&data->lock);
	mutex_init(&data->update_lock);
	data->name = nct6775_device_names[data->kind];
	data->bank = 0xff;		/* Force initial bank selection */
	platform_set_drvdata(pdev, data);

	switch (data->kind) {
	case nct6775:
		data->in_num = 9;
		data->temp_fixed_num = 3;

		data->ALARM_BITS = NCT6775_ALARM_BITS;

		data->temp_label = nct6775_temp_label;
		data->temp_label_num = ARRAY_SIZE(nct6775_temp_label);

		data->REG_CONFIG = NCT6775_REG_CONFIG;
		data->REG_VBAT = NCT6775_REG_VBAT;
		data->REG_DIODE = NCT6775_REG_DIODE;
		data->REG_VIN = NCT6775_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6775_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6775_REG_IN_MAX;
		data->REG_TEMP_OFFSET = NCT6775_REG_TEMP_OFFSET;
		data->REG_TEMP_SOURCE = NCT6775_REG_TEMP_SOURCE;
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
		data->temp_fixed_num = 3;

		data->ALARM_BITS = NCT6776_ALARM_BITS;

		data->temp_label = nct6776_temp_label;
		data->temp_label_num = ARRAY_SIZE(nct6776_temp_label);

		data->REG_CONFIG = NCT6775_REG_CONFIG;
		data->REG_VBAT = NCT6775_REG_VBAT;
		data->REG_DIODE = NCT6775_REG_DIODE;
		data->REG_VIN = NCT6775_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6775_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6775_REG_IN_MAX;
		data->REG_TEMP_OFFSET = NCT6775_REG_TEMP_OFFSET;
		data->REG_TEMP_SOURCE = NCT6775_REG_TEMP_SOURCE;
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
		data->temp_fixed_num = 6;

		data->ALARM_BITS = NCT6779_ALARM_BITS;

		data->temp_label = nct6779_temp_label;
		data->temp_label_num = ARRAY_SIZE(nct6779_temp_label);

		data->REG_CONFIG = NCT6775_REG_CONFIG;
		data->REG_VBAT = NCT6775_REG_VBAT;
		data->REG_DIODE = NCT6775_REG_DIODE;
		data->REG_VIN = NCT6779_REG_IN;
		data->REG_IN_MINMAX[0] = NCT6775_REG_IN_MIN;
		data->REG_IN_MINMAX[1] = NCT6775_REG_IN_MAX;
		data->REG_TEMP_OFFSET = NCT6779_REG_TEMP_OFFSET;
		data->REG_TEMP_SOURCE = NCT6775_REG_TEMP_SOURCE;
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
			data->reg_temp[1][i] = reg_temp_over[i];
			data->reg_temp[2][i] = reg_temp_hyst[i];
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

	switch (data->kind) {
	case nct6775:
		break;
	case nct6776:
		/*
		 * On NCT6776, AUXTIN and VIN3 pins are shared.
		 * Only way to detect it is to check if AUXTIN is used
		 * as a temperature source, and if that source is
		 * enabled.
		 *
		 * If that is the case, disable in6, which reports VIN3.
		 * Otherwise disable temp3.
		 */
		if (data->have_temp & (1 << 2)) {
			u8 reg = nct6775_read_value(data,
						    data->reg_temp_config[2]);
			if (reg & 0x01)
				data->have_temp &= ~(1 << 2);
			else
				data->have_in &= ~(1 << 6);
		}
		break;
	case nct6779:
		/*
		 * Shared pins:
		 *	VIN4 / AUXTIN0
		 *	VIN5 / AUXTIN1
		 *	VIN6 / AUXTIN2
		 *	VIN7 / AUXTIN3
		 *
		 * There does not seem to be a clean way to detect if VINx or
		 * AUXTINx is active, so for keep both sensor types enabled
		 * for now.
		 */
		break;
	}

	/* Initialize the chip */
	nct6775_init_device(data);

	data->vrm = vid_which_vrm();
	err = superio_enter(sio_data->sioreg);
	if (err)
		return err;

	/*
	 * Read VID value
	 * We can get the VID input values directly at logical device D 0xe3.
	 */
	superio_select(sio_data->sioreg, NCT6775_LD_VID);
	data->vid = superio_inb(sio_data->sioreg, 0xe3);
	superio_exit(sio_data->sioreg);

	err = device_create_file(dev, &dev_attr_cpu0_vid);
	if (err)
		return err;

	for (i = 0; i < data->in_num; i++) {
		if (!(data->have_in & (1 << i)))
			continue;
		err = sysfs_create_group(&dev->kobj, &nct6775_group_in[i]);
		if (err)
			goto exit_remove;
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

static struct platform_driver nct6775_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= nct6775_probe,
	.remove		= nct6775_remove,
};

/* nct6775_find() looks for a '627 in the Super-I/O config space */
static int __init nct6775_find(int sioaddr, unsigned short *addr,
			       struct nct6775_sio_data *sio_data)
{
	static const char sio_name_NCT6775[] __initconst = "NCT6775F";
	static const char sio_name_NCT6776[] __initconst = "NCT6776F";
	static const char sio_name_NCT6779[] __initconst = "NCT6779D";

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
	case SIO_NCT6775_ID:
		sio_data->kind = nct6775;
		sio_name = sio_name_NCT6775;
		break;
	case SIO_NCT6776_ID:
		sio_data->kind = nct6776;
		sio_name = sio_name_NCT6776;
		break;
	case SIO_NCT6779_ID:
		sio_data->kind = nct6779;
		sio_name = sio_name_NCT6779;
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
	pr_info("Found %s chip at %#x\n", sio_name, *addr);
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
