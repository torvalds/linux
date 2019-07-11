/*
 * lm93.c - Part of lm_sensors, Linux kernel modules for hardware monitoring
 *
 * Author/Maintainer: Mark M. Hoffman <mhoffman@lightlink.com>
 *	Copyright (c) 2004 Utilitek Systems, Inc.
 *
 * derived in part from lm78.c:
 *	Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>
 *
 * derived in part from lm85.c:
 *	Copyright (c) 2002, 2003 Philip Pokorny <ppokorny@penguincomputing.com>
 *	Copyright (c) 2003       Margit Schubert-While <margitsw@t-online.de>
 *
 * derived in part from w83l785ts.c:
 *	Copyright (c) 2003-2004 Jean Delvare <jdelvare@suse.de>
 *
 * Ported to Linux 2.6 by Eric J. Bowersox <ericb@aspsys.com>
 *	Copyright (c) 2005 Aspen Systems, Inc.
 *
 * Adapted to 2.6.20 by Carsten Emde <cbe@osadl.org>
 *	Copyright (c) 2006 Carsten Emde, Open Source Automation Development Lab
 *
 * Modified for mainline integration by Hans J. Koch <hjk@hansjkoch.de>
 *	Copyright (c) 2007 Hans J. Koch, Linutronix GmbH
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
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

/* LM93 REGISTER ADDRESSES */

/* miscellaneous */
#define LM93_REG_MFR_ID			0x3e
#define LM93_REG_VER			0x3f
#define LM93_REG_STATUS_CONTROL		0xe2
#define LM93_REG_CONFIG			0xe3
#define LM93_REG_SLEEP_CONTROL		0xe4

/* alarm values start here */
#define LM93_REG_HOST_ERROR_1		0x48

/* voltage inputs: in1-in16 (nr => 0-15) */
#define LM93_REG_IN(nr)			(0x56 + (nr))
#define LM93_REG_IN_MIN(nr)		(0x90 + (nr) * 2)
#define LM93_REG_IN_MAX(nr)		(0x91 + (nr) * 2)

/* temperature inputs: temp1-temp4 (nr => 0-3) */
#define LM93_REG_TEMP(nr)		(0x50 + (nr))
#define LM93_REG_TEMP_MIN(nr)		(0x78 + (nr) * 2)
#define LM93_REG_TEMP_MAX(nr)		(0x79 + (nr) * 2)

/* temp[1-4]_auto_boost (nr => 0-3) */
#define LM93_REG_BOOST(nr)		(0x80 + (nr))

/* #PROCHOT inputs: prochot1-prochot2 (nr => 0-1) */
#define LM93_REG_PROCHOT_CUR(nr)	(0x67 + (nr) * 2)
#define LM93_REG_PROCHOT_AVG(nr)	(0x68 + (nr) * 2)
#define LM93_REG_PROCHOT_MAX(nr)	(0xb0 + (nr))

/* fan tach inputs: fan1-fan4 (nr => 0-3) */
#define LM93_REG_FAN(nr)		(0x6e + (nr) * 2)
#define LM93_REG_FAN_MIN(nr)		(0xb4 + (nr) * 2)

/* pwm outputs: pwm1-pwm2 (nr => 0-1, reg => 0-3) */
#define LM93_REG_PWM_CTL(nr, reg)	(0xc8 + (reg) + (nr) * 4)
#define LM93_PWM_CTL1	0x0
#define LM93_PWM_CTL2	0x1
#define LM93_PWM_CTL3	0x2
#define LM93_PWM_CTL4	0x3

/* GPIO input state */
#define LM93_REG_GPI			0x6b

/* vid inputs: vid1-vid2 (nr => 0-1) */
#define LM93_REG_VID(nr)		(0x6c + (nr))

/* vccp1 & vccp2: VID relative inputs (nr => 0-1) */
#define LM93_REG_VCCP_LIMIT_OFF(nr)	(0xb2 + (nr))

/* temp[1-4]_auto_boost_hyst */
#define LM93_REG_BOOST_HYST_12		0xc0
#define LM93_REG_BOOST_HYST_34		0xc1
#define LM93_REG_BOOST_HYST(nr)		(0xc0 + (nr)/2)

/* temp[1-4]_auto_pwm_[min|hyst] */
#define LM93_REG_PWM_MIN_HYST_12	0xc3
#define LM93_REG_PWM_MIN_HYST_34	0xc4
#define LM93_REG_PWM_MIN_HYST(nr)	(0xc3 + (nr)/2)

/* prochot_override & prochot_interval */
#define LM93_REG_PROCHOT_OVERRIDE	0xc6
#define LM93_REG_PROCHOT_INTERVAL	0xc7

/* temp[1-4]_auto_base (nr => 0-3) */
#define LM93_REG_TEMP_BASE(nr)		(0xd0 + (nr))

/* temp[1-4]_auto_offsets (step => 0-11) */
#define LM93_REG_TEMP_OFFSET(step)	(0xd4 + (step))

/* #PROCHOT & #VRDHOT PWM ramp control */
#define LM93_REG_PWM_RAMP_CTL		0xbf

/* miscellaneous */
#define LM93_REG_SFC1		0xbc
#define LM93_REG_SFC2		0xbd
#define LM93_REG_GPI_VID_CTL	0xbe
#define LM93_REG_SF_TACH_TO_PWM	0xe0

/* error masks */
#define LM93_REG_GPI_ERR_MASK	0xec
#define LM93_REG_MISC_ERR_MASK	0xed

/* LM93 REGISTER VALUES */
#define LM93_MFR_ID		0x73
#define LM93_MFR_ID_PROTOTYPE	0x72

/* LM94 REGISTER VALUES */
#define LM94_MFR_ID_2		0x7a
#define LM94_MFR_ID		0x79
#define LM94_MFR_ID_PROTOTYPE	0x78

/* SMBus capabilities */
#define LM93_SMBUS_FUNC_FULL (I2C_FUNC_SMBUS_BYTE_DATA | \
		I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_BLOCK_DATA)
#define LM93_SMBUS_FUNC_MIN  (I2C_FUNC_SMBUS_BYTE_DATA | \
		I2C_FUNC_SMBUS_WORD_DATA)

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

/* Insmod parameters */

static bool disable_block;
module_param(disable_block, bool, 0);
MODULE_PARM_DESC(disable_block,
	"Set to non-zero to disable SMBus block data transactions.");

static bool init;
module_param(init, bool, 0);
MODULE_PARM_DESC(init, "Set to non-zero to force chip initialization.");

static int vccp_limit_type[2] = {0, 0};
module_param_array(vccp_limit_type, int, NULL, 0);
MODULE_PARM_DESC(vccp_limit_type, "Configures in7 and in8 limit modes.");

static int vid_agtl;
module_param(vid_agtl, int, 0);
MODULE_PARM_DESC(vid_agtl, "Configures VID pin input thresholds.");

/* Driver data */
static struct i2c_driver lm93_driver;

/* LM93 BLOCK READ COMMANDS */
static const struct { u8 cmd; u8 len; } lm93_block_read_cmds[12] = {
	{ 0xf2,  8 },
	{ 0xf3,  8 },
	{ 0xf4,  6 },
	{ 0xf5, 16 },
	{ 0xf6,  4 },
	{ 0xf7,  8 },
	{ 0xf8, 12 },
	{ 0xf9, 32 },
	{ 0xfa,  8 },
	{ 0xfb,  8 },
	{ 0xfc, 16 },
	{ 0xfd,  9 },
};

/*
 * ALARMS: SYSCTL format described further below
 * REG: 64 bits in 8 registers, as immediately below
 */
struct block1_t {
	u8 host_status_1;
	u8 host_status_2;
	u8 host_status_3;
	u8 host_status_4;
	u8 p1_prochot_status;
	u8 p2_prochot_status;
	u8 gpi_status;
	u8 fan_status;
};

/*
 * Client-specific data
 */
struct lm93_data {
	struct i2c_client *client;

	struct mutex update_lock;
	unsigned long last_updated;	/* In jiffies */

	/* client update function */
	void (*update)(struct lm93_data *, struct i2c_client *);

	char valid; /* !=0 if following fields are valid */

	/* register values, arranged by block read groups */
	struct block1_t block1;

	/*
	 * temp1 - temp4: unfiltered readings
	 * temp1 - temp2: filtered readings
	 */
	u8 block2[6];

	/* vin1 - vin16: readings */
	u8 block3[16];

	/* prochot1 - prochot2: readings */
	struct {
		u8 cur;
		u8 avg;
	} block4[2];

	/* fan counts 1-4 => 14-bits, LE, *left* justified */
	u16 block5[4];

	/* block6 has a lot of data we don't need */
	struct {
		u8 min;
		u8 max;
	} temp_lim[4];

	/* vin1 - vin16: low and high limits */
	struct {
		u8 min;
		u8 max;
	} block7[16];

	/* fan count limits 1-4 => same format as block5 */
	u16 block8[4];

	/* pwm control registers (2 pwms, 4 regs) */
	u8 block9[2][4];

	/* auto/pwm base temp and offset temp registers */
	struct {
		u8 base[4];
		u8 offset[12];
	} block10;

	/* master config register */
	u8 config;

	/* VID1 & VID2 => register format, 6-bits, right justified */
	u8 vid[2];

	/* prochot1 - prochot2: limits */
	u8 prochot_max[2];

	/* vccp1 & vccp2 (in7 & in8): VID relative limits (register format) */
	u8 vccp_limits[2];

	/* GPIO input state (register format, i.e. inverted) */
	u8 gpi;

	/* #PROCHOT override (register format) */
	u8 prochot_override;

	/* #PROCHOT intervals (register format) */
	u8 prochot_interval;

	/* Fan Boost Temperatures (register format) */
	u8 boost[4];

	/* Fan Boost Hysteresis (register format) */
	u8 boost_hyst[2];

	/* Temperature Zone Min. PWM & Hysteresis (register format) */
	u8 auto_pwm_min_hyst[2];

	/* #PROCHOT & #VRDHOT PWM Ramp Control */
	u8 pwm_ramp_ctl;

	/* miscellaneous setup regs */
	u8 sfc1;
	u8 sfc2;
	u8 sf_tach_to_pwm;

	/*
	 * The two PWM CTL2  registers can read something other than what was
	 * last written for the OVR_DC field (duty cycle override).  So, we
	 * save the user-commanded value here.
	 */
	u8 pwm_override[2];
};

/*
 * VID:	mV
 * REG: 6-bits, right justified, *always* using Intel VRM/VRD 10
 */
static int LM93_VID_FROM_REG(u8 reg)
{
	return vid_from_reg((reg & 0x3f), 100);
}

/* min, max, and nominal register values, per channel (u8) */
static const u8 lm93_vin_reg_min[16] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xae,
};
static const u8 lm93_vin_reg_max[16] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xd1,
};
/*
 * Values from the datasheet. They're here for documentation only.
 * static const u8 lm93_vin_reg_nom[16] = {
 * 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
 * 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0x40, 0xc0,
 * };
 */

/* min, max, and nominal voltage readings, per channel (mV)*/
static const unsigned long lm93_vin_val_min[16] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 3000,
};

static const unsigned long lm93_vin_val_max[16] = {
	1236, 1236, 1236, 1600, 2000, 2000, 1600, 1600,
	4400, 6500, 3333, 2625, 1312, 1312, 1236, 3600,
};
/*
 * Values from the datasheet. They're here for documentation only.
 * static const unsigned long lm93_vin_val_nom[16] = {
 * 927,  927,  927, 1200, 1500, 1500, 1200, 1200,
 * 3300, 5000, 2500, 1969,  984,  984,  309, 3300,
 * };
 */

static unsigned LM93_IN_FROM_REG(int nr, u8 reg)
{
	const long uv_max = lm93_vin_val_max[nr] * 1000;
	const long uv_min = lm93_vin_val_min[nr] * 1000;

	const long slope = (uv_max - uv_min) /
		(lm93_vin_reg_max[nr] - lm93_vin_reg_min[nr]);
	const long intercept = uv_min - slope * lm93_vin_reg_min[nr];

	return (slope * reg + intercept + 500) / 1000;
}

/*
 * IN: mV, limits determined by channel nr
 * REG: scaling determined by channel nr
 */
static u8 LM93_IN_TO_REG(int nr, unsigned val)
{
	/* range limit */
	const long mv = clamp_val(val,
				  lm93_vin_val_min[nr], lm93_vin_val_max[nr]);

	/* try not to lose too much precision here */
	const long uv = mv * 1000;
	const long uv_max = lm93_vin_val_max[nr] * 1000;
	const long uv_min = lm93_vin_val_min[nr] * 1000;

	/* convert */
	const long slope = (uv_max - uv_min) /
		(lm93_vin_reg_max[nr] - lm93_vin_reg_min[nr]);
	const long intercept = uv_min - slope * lm93_vin_reg_min[nr];

	u8 result = ((uv - intercept + (slope/2)) / slope);
	result = clamp_val(result,
			   lm93_vin_reg_min[nr], lm93_vin_reg_max[nr]);
	return result;
}

/* vid in mV, upper == 0 indicates low limit, otherwise upper limit */
static unsigned LM93_IN_REL_FROM_REG(u8 reg, int upper, int vid)
{
	const long uv_offset = upper ? (((reg >> 4 & 0x0f) + 1) * 12500) :
				(((reg >> 0 & 0x0f) + 1) * -25000);
	const long uv_vid = vid * 1000;
	return (uv_vid + uv_offset + 5000) / 10000;
}

#define LM93_IN_MIN_FROM_REG(reg, vid)	LM93_IN_REL_FROM_REG((reg), 0, (vid))
#define LM93_IN_MAX_FROM_REG(reg, vid)	LM93_IN_REL_FROM_REG((reg), 1, (vid))

/*
 * vid in mV , upper == 0 indicates low limit, otherwise upper limit
 * upper also determines which nibble of the register is returned
 * (the other nibble will be 0x0)
 */
static u8 LM93_IN_REL_TO_REG(unsigned val, int upper, int vid)
{
	long uv_offset = vid * 1000 - val * 10000;
	if (upper) {
		uv_offset = clamp_val(uv_offset, 12500, 200000);
		return (u8)((uv_offset /  12500 - 1) << 4);
	} else {
		uv_offset = clamp_val(uv_offset, -400000, -25000);
		return (u8)((uv_offset / -25000 - 1) << 0);
	}
}

/*
 * TEMP: 1/1000 degrees C (-128C to +127C)
 * REG: 1C/bit, two's complement
 */
static int LM93_TEMP_FROM_REG(u8 reg)
{
	return (s8)reg * 1000;
}

#define LM93_TEMP_MIN (-128000)
#define LM93_TEMP_MAX (127000)

/*
 * TEMP: 1/1000 degrees C (-128C to +127C)
 * REG: 1C/bit, two's complement
 */
static u8 LM93_TEMP_TO_REG(long temp)
{
	int ntemp = clamp_val(temp, LM93_TEMP_MIN, LM93_TEMP_MAX);
	ntemp += (ntemp < 0 ? -500 : 500);
	return (u8)(ntemp / 1000);
}

/* Determine 4-bit temperature offset resolution */
static int LM93_TEMP_OFFSET_MODE_FROM_REG(u8 sfc2, int nr)
{
	/* mode: 0 => 1C/bit, nonzero => 0.5C/bit */
	return sfc2 & (nr < 2 ? 0x10 : 0x20);
}

/*
 * This function is common to all 4-bit temperature offsets
 * reg is 4 bits right justified
 * mode 0 => 1C/bit, mode !0 => 0.5C/bit
 */
static int LM93_TEMP_OFFSET_FROM_REG(u8 reg, int mode)
{
	return (reg & 0x0f) * (mode ? 5 : 10);
}

#define LM93_TEMP_OFFSET_MIN  (0)
#define LM93_TEMP_OFFSET_MAX0 (150)
#define LM93_TEMP_OFFSET_MAX1 (75)

/*
 * This function is common to all 4-bit temperature offsets
 * returns 4 bits right justified
 * mode 0 => 1C/bit, mode !0 => 0.5C/bit
 */
static u8 LM93_TEMP_OFFSET_TO_REG(int off, int mode)
{
	int factor = mode ? 5 : 10;

	off = clamp_val(off, LM93_TEMP_OFFSET_MIN,
		mode ? LM93_TEMP_OFFSET_MAX1 : LM93_TEMP_OFFSET_MAX0);
	return (u8)((off + factor/2) / factor);
}

/* 0 <= nr <= 3 */
static int LM93_TEMP_AUTO_OFFSET_FROM_REG(u8 reg, int nr, int mode)
{
	/* temp1-temp2 (nr=0,1) use lower nibble */
	if (nr < 2)
		return LM93_TEMP_OFFSET_FROM_REG(reg & 0x0f, mode);

	/* temp3-temp4 (nr=2,3) use upper nibble */
	else
		return LM93_TEMP_OFFSET_FROM_REG(reg >> 4 & 0x0f, mode);
}

/*
 * TEMP: 1/10 degrees C (0C to +15C (mode 0) or +7.5C (mode non-zero))
 * REG: 1.0C/bit (mode 0) or 0.5C/bit (mode non-zero)
 * 0 <= nr <= 3
 */
static u8 LM93_TEMP_AUTO_OFFSET_TO_REG(u8 old, int off, int nr, int mode)
{
	u8 new = LM93_TEMP_OFFSET_TO_REG(off, mode);

	/* temp1-temp2 (nr=0,1) use lower nibble */
	if (nr < 2)
		return (old & 0xf0) | (new & 0x0f);

	/* temp3-temp4 (nr=2,3) use upper nibble */
	else
		return (new << 4 & 0xf0) | (old & 0x0f);
}

static int LM93_AUTO_BOOST_HYST_FROM_REGS(struct lm93_data *data, int nr,
		int mode)
{
	u8 reg;

	switch (nr) {
	case 0:
		reg = data->boost_hyst[0] & 0x0f;
		break;
	case 1:
		reg = data->boost_hyst[0] >> 4 & 0x0f;
		break;
	case 2:
		reg = data->boost_hyst[1] & 0x0f;
		break;
	case 3:
	default:
		reg = data->boost_hyst[1] >> 4 & 0x0f;
		break;
	}

	return LM93_TEMP_FROM_REG(data->boost[nr]) -
			LM93_TEMP_OFFSET_FROM_REG(reg, mode);
}

static u8 LM93_AUTO_BOOST_HYST_TO_REG(struct lm93_data *data, long hyst,
		int nr, int mode)
{
	u8 reg = LM93_TEMP_OFFSET_TO_REG(
			(LM93_TEMP_FROM_REG(data->boost[nr]) - hyst), mode);

	switch (nr) {
	case 0:
		reg = (data->boost_hyst[0] & 0xf0) | (reg & 0x0f);
		break;
	case 1:
		reg = (reg << 4 & 0xf0) | (data->boost_hyst[0] & 0x0f);
		break;
	case 2:
		reg = (data->boost_hyst[1] & 0xf0) | (reg & 0x0f);
		break;
	case 3:
	default:
		reg = (reg << 4 & 0xf0) | (data->boost_hyst[1] & 0x0f);
		break;
	}

	return reg;
}

/*
 * PWM: 0-255 per sensors documentation
 * REG: 0-13 as mapped below... right justified
 */
enum pwm_freq { LM93_PWM_MAP_HI_FREQ, LM93_PWM_MAP_LO_FREQ };

static int lm93_pwm_map[2][16] = {
	{
		0x00, /*   0.00% */ 0x40, /*  25.00% */
		0x50, /*  31.25% */ 0x60, /*  37.50% */
		0x70, /*  43.75% */ 0x80, /*  50.00% */
		0x90, /*  56.25% */ 0xa0, /*  62.50% */
		0xb0, /*  68.75% */ 0xc0, /*  75.00% */
		0xd0, /*  81.25% */ 0xe0, /*  87.50% */
		0xf0, /*  93.75% */ 0xff, /* 100.00% */
		0xff, 0xff, /* 14, 15 are reserved and should never occur */
	},
	{
		0x00, /*   0.00% */ 0x40, /*  25.00% */
		0x49, /*  28.57% */ 0x52, /*  32.14% */
		0x5b, /*  35.71% */ 0x64, /*  39.29% */
		0x6d, /*  42.86% */ 0x76, /*  46.43% */
		0x80, /*  50.00% */ 0x89, /*  53.57% */
		0x92, /*  57.14% */ 0xb6, /*  71.43% */
		0xdb, /*  85.71% */ 0xff, /* 100.00% */
		0xff, 0xff, /* 14, 15 are reserved and should never occur */
	},
};

static int LM93_PWM_FROM_REG(u8 reg, enum pwm_freq freq)
{
	return lm93_pwm_map[freq][reg & 0x0f];
}

/* round up to nearest match */
static u8 LM93_PWM_TO_REG(int pwm, enum pwm_freq freq)
{
	int i;
	for (i = 0; i < 13; i++)
		if (pwm <= lm93_pwm_map[freq][i])
			break;

	/* can fall through with i==13 */
	return (u8)i;
}

static int LM93_FAN_FROM_REG(u16 regs)
{
	const u16 count = le16_to_cpu(regs) >> 2;
	return count == 0 ? -1 : count == 0x3fff ? 0 : 1350000 / count;
}

/*
 * RPM: (82.5 to 1350000)
 * REG: 14-bits, LE, *left* justified
 */
static u16 LM93_FAN_TO_REG(long rpm)
{
	u16 count, regs;

	if (rpm == 0) {
		count = 0x3fff;
	} else {
		rpm = clamp_val(rpm, 1, 1000000);
		count = clamp_val((1350000 + rpm) / rpm, 1, 0x3ffe);
	}

	regs = count << 2;
	return cpu_to_le16(regs);
}

/*
 * PWM FREQ: HZ
 * REG: 0-7 as mapped below
 */
static int lm93_pwm_freq_map[8] = {
	22500, 96, 84, 72, 60, 48, 36, 12
};

static int LM93_PWM_FREQ_FROM_REG(u8 reg)
{
	return lm93_pwm_freq_map[reg & 0x07];
}

/* round up to nearest match */
static u8 LM93_PWM_FREQ_TO_REG(int freq)
{
	int i;
	for (i = 7; i > 0; i--)
		if (freq <= lm93_pwm_freq_map[i])
			break;

	/* can fall through with i==0 */
	return (u8)i;
}

/*
 * TIME: 1/100 seconds
 * REG: 0-7 as mapped below
 */
static int lm93_spinup_time_map[8] = {
	0, 10, 25, 40, 70, 100, 200, 400,
};

static int LM93_SPINUP_TIME_FROM_REG(u8 reg)
{
	return lm93_spinup_time_map[reg >> 5 & 0x07];
}

/* round up to nearest match */
static u8 LM93_SPINUP_TIME_TO_REG(int time)
{
	int i;
	for (i = 0; i < 7; i++)
		if (time <= lm93_spinup_time_map[i])
			break;

	/* can fall through with i==8 */
	return (u8)i;
}

#define LM93_RAMP_MIN 0
#define LM93_RAMP_MAX 75

static int LM93_RAMP_FROM_REG(u8 reg)
{
	return (reg & 0x0f) * 5;
}

/*
 * RAMP: 1/100 seconds
 * REG: 50mS/bit 4-bits right justified
 */
static u8 LM93_RAMP_TO_REG(int ramp)
{
	ramp = clamp_val(ramp, LM93_RAMP_MIN, LM93_RAMP_MAX);
	return (u8)((ramp + 2) / 5);
}

/*
 * PROCHOT: 0-255, 0 => 0%, 255 => > 96.6%
 * REG: (same)
 */
static u8 LM93_PROCHOT_TO_REG(long prochot)
{
	prochot = clamp_val(prochot, 0, 255);
	return (u8)prochot;
}

/*
 * PROCHOT-INTERVAL: 73 - 37200 (1/100 seconds)
 * REG: 0-9 as mapped below
 */
static int lm93_interval_map[10] = {
	73, 146, 290, 580, 1170, 2330, 4660, 9320, 18600, 37200,
};

static int LM93_INTERVAL_FROM_REG(u8 reg)
{
	return lm93_interval_map[reg & 0x0f];
}

/* round up to nearest match */
static u8 LM93_INTERVAL_TO_REG(long interval)
{
	int i;
	for (i = 0; i < 9; i++)
		if (interval <= lm93_interval_map[i])
			break;

	/* can fall through with i==9 */
	return (u8)i;
}

/*
 * GPIO: 0-255, GPIO0 is LSB
 * REG: inverted
 */
static unsigned LM93_GPI_FROM_REG(u8 reg)
{
	return ~reg & 0xff;
}

/*
 * alarm bitmask definitions
 * The LM93 has nearly 64 bits of error status... I've pared that down to
 * what I think is a useful subset in order to fit it into 32 bits.
 *
 * Especially note that the #VRD_HOT alarms are missing because we provide
 * that information as values in another sysfs file.
 *
 * If libsensors is extended to support 64 bit values, this could be revisited.
 */
#define LM93_ALARM_IN1		0x00000001
#define LM93_ALARM_IN2		0x00000002
#define LM93_ALARM_IN3		0x00000004
#define LM93_ALARM_IN4		0x00000008
#define LM93_ALARM_IN5		0x00000010
#define LM93_ALARM_IN6		0x00000020
#define LM93_ALARM_IN7		0x00000040
#define LM93_ALARM_IN8		0x00000080
#define LM93_ALARM_IN9		0x00000100
#define LM93_ALARM_IN10		0x00000200
#define LM93_ALARM_IN11		0x00000400
#define LM93_ALARM_IN12		0x00000800
#define LM93_ALARM_IN13		0x00001000
#define LM93_ALARM_IN14		0x00002000
#define LM93_ALARM_IN15		0x00004000
#define LM93_ALARM_IN16		0x00008000
#define LM93_ALARM_FAN1		0x00010000
#define LM93_ALARM_FAN2		0x00020000
#define LM93_ALARM_FAN3		0x00040000
#define LM93_ALARM_FAN4		0x00080000
#define LM93_ALARM_PH1_ERR	0x00100000
#define LM93_ALARM_PH2_ERR	0x00200000
#define LM93_ALARM_SCSI1_ERR	0x00400000
#define LM93_ALARM_SCSI2_ERR	0x00800000
#define LM93_ALARM_DVDDP1_ERR	0x01000000
#define LM93_ALARM_DVDDP2_ERR	0x02000000
#define LM93_ALARM_D1_ERR	0x04000000
#define LM93_ALARM_D2_ERR	0x08000000
#define LM93_ALARM_TEMP1	0x10000000
#define LM93_ALARM_TEMP2	0x20000000
#define LM93_ALARM_TEMP3	0x40000000

static unsigned LM93_ALARMS_FROM_REG(struct block1_t b1)
{
	unsigned result;
	result  = b1.host_status_2 & 0x3f;

	if (vccp_limit_type[0])
		result |= (b1.host_status_4 & 0x10) << 2;
	else
		result |= b1.host_status_2 & 0x40;

	if (vccp_limit_type[1])
		result |= (b1.host_status_4 & 0x20) << 2;
	else
		result |= b1.host_status_2 & 0x80;

	result |= b1.host_status_3 << 8;
	result |= (b1.fan_status & 0x0f) << 16;
	result |= (b1.p1_prochot_status & 0x80) << 13;
	result |= (b1.p2_prochot_status & 0x80) << 14;
	result |= (b1.host_status_4 & 0xfc) << 20;
	result |= (b1.host_status_1 & 0x07) << 28;
	return result;
}

#define MAX_RETRIES 5

static u8 lm93_read_byte(struct i2c_client *client, u8 reg)
{
	int value, i;

	/* retry in case of read errors */
	for (i = 1; i <= MAX_RETRIES; i++) {
		value = i2c_smbus_read_byte_data(client, reg);
		if (value >= 0) {
			return value;
		} else {
			dev_warn(&client->dev,
				 "lm93: read byte data failed, address 0x%02x.\n",
				 reg);
			mdelay(i + 3);
		}

	}

	/* <TODO> what to return in case of error? */
	dev_err(&client->dev, "lm93: All read byte retries failed!!\n");
	return 0;
}

static int lm93_write_byte(struct i2c_client *client, u8 reg, u8 value)
{
	int result;

	/* <TODO> how to handle write errors? */
	result = i2c_smbus_write_byte_data(client, reg, value);

	if (result < 0)
		dev_warn(&client->dev,
			 "lm93: write byte data failed, 0x%02x at address 0x%02x.\n",
			 value, reg);

	return result;
}

static u16 lm93_read_word(struct i2c_client *client, u8 reg)
{
	int value, i;

	/* retry in case of read errors */
	for (i = 1; i <= MAX_RETRIES; i++) {
		value = i2c_smbus_read_word_data(client, reg);
		if (value >= 0) {
			return value;
		} else {
			dev_warn(&client->dev,
				 "lm93: read word data failed, address 0x%02x.\n",
				 reg);
			mdelay(i + 3);
		}

	}

	/* <TODO> what to return in case of error? */
	dev_err(&client->dev, "lm93: All read word retries failed!!\n");
	return 0;
}

static int lm93_write_word(struct i2c_client *client, u8 reg, u16 value)
{
	int result;

	/* <TODO> how to handle write errors? */
	result = i2c_smbus_write_word_data(client, reg, value);

	if (result < 0)
		dev_warn(&client->dev,
			 "lm93: write word data failed, 0x%04x at address 0x%02x.\n",
			 value, reg);

	return result;
}

static u8 lm93_block_buffer[I2C_SMBUS_BLOCK_MAX];

/*
 * read block data into values, retry if not expected length
 * fbn => index to lm93_block_read_cmds table
 * (Fixed Block Number - section 14.5.2 of LM93 datasheet)
 */
static void lm93_read_block(struct i2c_client *client, u8 fbn, u8 *values)
{
	int i, result = 0;

	for (i = 1; i <= MAX_RETRIES; i++) {
		result = i2c_smbus_read_block_data(client,
			lm93_block_read_cmds[fbn].cmd, lm93_block_buffer);

		if (result == lm93_block_read_cmds[fbn].len) {
			break;
		} else {
			dev_warn(&client->dev,
				 "lm93: block read data failed, command 0x%02x.\n",
				 lm93_block_read_cmds[fbn].cmd);
			mdelay(i + 3);
		}
	}

	if (result == lm93_block_read_cmds[fbn].len) {
		memcpy(values, lm93_block_buffer,
		       lm93_block_read_cmds[fbn].len);
	} else {
		/* <TODO> what to do in case of error? */
	}
}

static struct lm93_data *lm93_update_device(struct device *dev)
{
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	const unsigned long interval = HZ + (HZ / 2);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + interval) ||
		!data->valid) {

		data->update(data, client);
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);
	return data;
}

/* update routine for data that has no corresponding SMBus block command */
static void lm93_update_client_common(struct lm93_data *data,
				      struct i2c_client *client)
{
	int i;
	u8 *ptr;

	/* temp1 - temp4: limits */
	for (i = 0; i < 4; i++) {
		data->temp_lim[i].min =
			lm93_read_byte(client, LM93_REG_TEMP_MIN(i));
		data->temp_lim[i].max =
			lm93_read_byte(client, LM93_REG_TEMP_MAX(i));
	}

	/* config register */
	data->config = lm93_read_byte(client, LM93_REG_CONFIG);

	/* vid1 - vid2: values */
	for (i = 0; i < 2; i++)
		data->vid[i] = lm93_read_byte(client, LM93_REG_VID(i));

	/* prochot1 - prochot2: limits */
	for (i = 0; i < 2; i++)
		data->prochot_max[i] = lm93_read_byte(client,
				LM93_REG_PROCHOT_MAX(i));

	/* vccp1 - vccp2: VID relative limits */
	for (i = 0; i < 2; i++)
		data->vccp_limits[i] = lm93_read_byte(client,
				LM93_REG_VCCP_LIMIT_OFF(i));

	/* GPIO input state */
	data->gpi = lm93_read_byte(client, LM93_REG_GPI);

	/* #PROCHOT override state */
	data->prochot_override = lm93_read_byte(client,
			LM93_REG_PROCHOT_OVERRIDE);

	/* #PROCHOT intervals */
	data->prochot_interval = lm93_read_byte(client,
			LM93_REG_PROCHOT_INTERVAL);

	/* Fan Boost Temperature registers */
	for (i = 0; i < 4; i++)
		data->boost[i] = lm93_read_byte(client, LM93_REG_BOOST(i));

	/* Fan Boost Temperature Hyst. registers */
	data->boost_hyst[0] = lm93_read_byte(client, LM93_REG_BOOST_HYST_12);
	data->boost_hyst[1] = lm93_read_byte(client, LM93_REG_BOOST_HYST_34);

	/* Temperature Zone Min. PWM & Hysteresis registers */
	data->auto_pwm_min_hyst[0] =
			lm93_read_byte(client, LM93_REG_PWM_MIN_HYST_12);
	data->auto_pwm_min_hyst[1] =
			lm93_read_byte(client, LM93_REG_PWM_MIN_HYST_34);

	/* #PROCHOT & #VRDHOT PWM Ramp Control register */
	data->pwm_ramp_ctl = lm93_read_byte(client, LM93_REG_PWM_RAMP_CTL);

	/* misc setup registers */
	data->sfc1 = lm93_read_byte(client, LM93_REG_SFC1);
	data->sfc2 = lm93_read_byte(client, LM93_REG_SFC2);
	data->sf_tach_to_pwm = lm93_read_byte(client,
			LM93_REG_SF_TACH_TO_PWM);

	/* write back alarm values to clear */
	for (i = 0, ptr = (u8 *)(&data->block1); i < 8; i++)
		lm93_write_byte(client, LM93_REG_HOST_ERROR_1 + i, *(ptr + i));
}

/* update routine which uses SMBus block data commands */
static void lm93_update_client_full(struct lm93_data *data,
				    struct i2c_client *client)
{
	dev_dbg(&client->dev, "starting device update (block data enabled)\n");

	/* in1 - in16: values & limits */
	lm93_read_block(client, 3, (u8 *)(data->block3));
	lm93_read_block(client, 7, (u8 *)(data->block7));

	/* temp1 - temp4: values */
	lm93_read_block(client, 2, (u8 *)(data->block2));

	/* prochot1 - prochot2: values */
	lm93_read_block(client, 4, (u8 *)(data->block4));

	/* fan1 - fan4: values & limits */
	lm93_read_block(client, 5, (u8 *)(data->block5));
	lm93_read_block(client, 8, (u8 *)(data->block8));

	/* pmw control registers */
	lm93_read_block(client, 9, (u8 *)(data->block9));

	/* alarm values */
	lm93_read_block(client, 1, (u8 *)(&data->block1));

	/* auto/pwm registers */
	lm93_read_block(client, 10, (u8 *)(&data->block10));

	lm93_update_client_common(data, client);
}

/* update routine which uses SMBus byte/word data commands only */
static void lm93_update_client_min(struct lm93_data *data,
				   struct i2c_client *client)
{
	int i, j;
	u8 *ptr;

	dev_dbg(&client->dev, "starting device update (block data disabled)\n");

	/* in1 - in16: values & limits */
	for (i = 0; i < 16; i++) {
		data->block3[i] =
			lm93_read_byte(client, LM93_REG_IN(i));
		data->block7[i].min =
			lm93_read_byte(client, LM93_REG_IN_MIN(i));
		data->block7[i].max =
			lm93_read_byte(client, LM93_REG_IN_MAX(i));
	}

	/* temp1 - temp4: values */
	for (i = 0; i < 4; i++) {
		data->block2[i] =
			lm93_read_byte(client, LM93_REG_TEMP(i));
	}

	/* prochot1 - prochot2: values */
	for (i = 0; i < 2; i++) {
		data->block4[i].cur =
			lm93_read_byte(client, LM93_REG_PROCHOT_CUR(i));
		data->block4[i].avg =
			lm93_read_byte(client, LM93_REG_PROCHOT_AVG(i));
	}

	/* fan1 - fan4: values & limits */
	for (i = 0; i < 4; i++) {
		data->block5[i] =
			lm93_read_word(client, LM93_REG_FAN(i));
		data->block8[i] =
			lm93_read_word(client, LM93_REG_FAN_MIN(i));
	}

	/* pwm control registers */
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 4; j++) {
			data->block9[i][j] =
				lm93_read_byte(client, LM93_REG_PWM_CTL(i, j));
		}
	}

	/* alarm values */
	for (i = 0, ptr = (u8 *)(&data->block1); i < 8; i++) {
		*(ptr + i) =
			lm93_read_byte(client, LM93_REG_HOST_ERROR_1 + i);
	}

	/* auto/pwm (base temp) registers */
	for (i = 0; i < 4; i++) {
		data->block10.base[i] =
			lm93_read_byte(client, LM93_REG_TEMP_BASE(i));
	}

	/* auto/pwm (offset temp) registers */
	for (i = 0; i < 12; i++) {
		data->block10.offset[i] =
			lm93_read_byte(client, LM93_REG_TEMP_OFFSET(i));
	}

	lm93_update_client_common(data, client);
}

/* following are the sysfs callback functions */
static ssize_t in_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;

	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", LM93_IN_FROM_REG(nr, data->block3[nr]));
}

static SENSOR_DEVICE_ATTR_RO(in1_input, in, 0);
static SENSOR_DEVICE_ATTR_RO(in2_input, in, 1);
static SENSOR_DEVICE_ATTR_RO(in3_input, in, 2);
static SENSOR_DEVICE_ATTR_RO(in4_input, in, 3);
static SENSOR_DEVICE_ATTR_RO(in5_input, in, 4);
static SENSOR_DEVICE_ATTR_RO(in6_input, in, 5);
static SENSOR_DEVICE_ATTR_RO(in7_input, in, 6);
static SENSOR_DEVICE_ATTR_RO(in8_input, in, 7);
static SENSOR_DEVICE_ATTR_RO(in9_input, in, 8);
static SENSOR_DEVICE_ATTR_RO(in10_input, in, 9);
static SENSOR_DEVICE_ATTR_RO(in11_input, in, 10);
static SENSOR_DEVICE_ATTR_RO(in12_input, in, 11);
static SENSOR_DEVICE_ATTR_RO(in13_input, in, 12);
static SENSOR_DEVICE_ATTR_RO(in14_input, in, 13);
static SENSOR_DEVICE_ATTR_RO(in15_input, in, 14);
static SENSOR_DEVICE_ATTR_RO(in16_input, in, 15);

static ssize_t in_min_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	int vccp = nr - 6;
	long rc, vid;

	if ((nr == 6 || nr == 7) && vccp_limit_type[vccp]) {
		vid = LM93_VID_FROM_REG(data->vid[vccp]);
		rc = LM93_IN_MIN_FROM_REG(data->vccp_limits[vccp], vid);
	} else {
		rc = LM93_IN_FROM_REG(nr, data->block7[nr].min);
	}
	return sprintf(buf, "%ld\n", rc);
}

static ssize_t in_min_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int vccp = nr - 6;
	long vid;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	if ((nr == 6 || nr == 7) && vccp_limit_type[vccp]) {
		vid = LM93_VID_FROM_REG(data->vid[vccp]);
		data->vccp_limits[vccp] = (data->vccp_limits[vccp] & 0xf0) |
				LM93_IN_REL_TO_REG(val, 0, vid);
		lm93_write_byte(client, LM93_REG_VCCP_LIMIT_OFF(vccp),
				data->vccp_limits[vccp]);
	} else {
		data->block7[nr].min = LM93_IN_TO_REG(nr, val);
		lm93_write_byte(client, LM93_REG_IN_MIN(nr),
				data->block7[nr].min);
	}
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(in1_min, in_min, 0);
static SENSOR_DEVICE_ATTR_RW(in2_min, in_min, 1);
static SENSOR_DEVICE_ATTR_RW(in3_min, in_min, 2);
static SENSOR_DEVICE_ATTR_RW(in4_min, in_min, 3);
static SENSOR_DEVICE_ATTR_RW(in5_min, in_min, 4);
static SENSOR_DEVICE_ATTR_RW(in6_min, in_min, 5);
static SENSOR_DEVICE_ATTR_RW(in7_min, in_min, 6);
static SENSOR_DEVICE_ATTR_RW(in8_min, in_min, 7);
static SENSOR_DEVICE_ATTR_RW(in9_min, in_min, 8);
static SENSOR_DEVICE_ATTR_RW(in10_min, in_min, 9);
static SENSOR_DEVICE_ATTR_RW(in11_min, in_min, 10);
static SENSOR_DEVICE_ATTR_RW(in12_min, in_min, 11);
static SENSOR_DEVICE_ATTR_RW(in13_min, in_min, 12);
static SENSOR_DEVICE_ATTR_RW(in14_min, in_min, 13);
static SENSOR_DEVICE_ATTR_RW(in15_min, in_min, 14);
static SENSOR_DEVICE_ATTR_RW(in16_min, in_min, 15);

static ssize_t in_max_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	int vccp = nr - 6;
	long rc, vid;

	if ((nr == 6 || nr == 7) && vccp_limit_type[vccp]) {
		vid = LM93_VID_FROM_REG(data->vid[vccp]);
		rc = LM93_IN_MAX_FROM_REG(data->vccp_limits[vccp], vid);
	} else {
		rc = LM93_IN_FROM_REG(nr, data->block7[nr].max);
	}
	return sprintf(buf, "%ld\n", rc);
}

static ssize_t in_max_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int vccp = nr - 6;
	long vid;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	if ((nr == 6 || nr == 7) && vccp_limit_type[vccp]) {
		vid = LM93_VID_FROM_REG(data->vid[vccp]);
		data->vccp_limits[vccp] = (data->vccp_limits[vccp] & 0x0f) |
				LM93_IN_REL_TO_REG(val, 1, vid);
		lm93_write_byte(client, LM93_REG_VCCP_LIMIT_OFF(vccp),
				data->vccp_limits[vccp]);
	} else {
		data->block7[nr].max = LM93_IN_TO_REG(nr, val);
		lm93_write_byte(client, LM93_REG_IN_MAX(nr),
				data->block7[nr].max);
	}
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(in1_max, in_max, 0);
static SENSOR_DEVICE_ATTR_RW(in2_max, in_max, 1);
static SENSOR_DEVICE_ATTR_RW(in3_max, in_max, 2);
static SENSOR_DEVICE_ATTR_RW(in4_max, in_max, 3);
static SENSOR_DEVICE_ATTR_RW(in5_max, in_max, 4);
static SENSOR_DEVICE_ATTR_RW(in6_max, in_max, 5);
static SENSOR_DEVICE_ATTR_RW(in7_max, in_max, 6);
static SENSOR_DEVICE_ATTR_RW(in8_max, in_max, 7);
static SENSOR_DEVICE_ATTR_RW(in9_max, in_max, 8);
static SENSOR_DEVICE_ATTR_RW(in10_max, in_max, 9);
static SENSOR_DEVICE_ATTR_RW(in11_max, in_max, 10);
static SENSOR_DEVICE_ATTR_RW(in12_max, in_max, 11);
static SENSOR_DEVICE_ATTR_RW(in13_max, in_max, 12);
static SENSOR_DEVICE_ATTR_RW(in14_max, in_max, 13);
static SENSOR_DEVICE_ATTR_RW(in15_max, in_max, 14);
static SENSOR_DEVICE_ATTR_RW(in16_max, in_max, 15);

static ssize_t temp_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", LM93_TEMP_FROM_REG(data->block2[nr]));
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp, 2);

static ssize_t temp_min_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", LM93_TEMP_FROM_REG(data->temp_lim[nr].min));
}

static ssize_t temp_min_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_lim[nr].min = LM93_TEMP_TO_REG(val);
	lm93_write_byte(client, LM93_REG_TEMP_MIN(nr), data->temp_lim[nr].min);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_min, temp_min, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_min, temp_min, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_min, temp_min, 2);

static ssize_t temp_max_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", LM93_TEMP_FROM_REG(data->temp_lim[nr].max));
}

static ssize_t temp_max_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_lim[nr].max = LM93_TEMP_TO_REG(val);
	lm93_write_byte(client, LM93_REG_TEMP_MAX(nr), data->temp_lim[nr].max);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_max, temp_max, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp_max, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_max, temp_max, 2);

static ssize_t temp_auto_base_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", LM93_TEMP_FROM_REG(data->block10.base[nr]));
}

static ssize_t temp_auto_base_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->block10.base[nr] = LM93_TEMP_TO_REG(val);
	lm93_write_byte(client, LM93_REG_TEMP_BASE(nr), data->block10.base[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_auto_base, temp_auto_base, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_auto_base, temp_auto_base, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_auto_base, temp_auto_base, 2);

static ssize_t temp_auto_boost_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", LM93_TEMP_FROM_REG(data->boost[nr]));
}

static ssize_t temp_auto_boost_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->boost[nr] = LM93_TEMP_TO_REG(val);
	lm93_write_byte(client, LM93_REG_BOOST(nr), data->boost[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_auto_boost, temp_auto_boost, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_auto_boost, temp_auto_boost, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_auto_boost, temp_auto_boost, 2);

static ssize_t temp_auto_boost_hyst_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	int mode = LM93_TEMP_OFFSET_MODE_FROM_REG(data->sfc2, nr);
	return sprintf(buf, "%d\n",
		       LM93_AUTO_BOOST_HYST_FROM_REGS(data, nr, mode));
}

static ssize_t temp_auto_boost_hyst_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	/* force 0.5C/bit mode */
	data->sfc2 = lm93_read_byte(client, LM93_REG_SFC2);
	data->sfc2 |= ((nr < 2) ? 0x10 : 0x20);
	lm93_write_byte(client, LM93_REG_SFC2, data->sfc2);
	data->boost_hyst[nr/2] = LM93_AUTO_BOOST_HYST_TO_REG(data, val, nr, 1);
	lm93_write_byte(client, LM93_REG_BOOST_HYST(nr),
			data->boost_hyst[nr/2]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_auto_boost_hyst, temp_auto_boost_hyst, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_auto_boost_hyst, temp_auto_boost_hyst, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_auto_boost_hyst, temp_auto_boost_hyst, 2);

static ssize_t temp_auto_offset_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *s_attr = to_sensor_dev_attr_2(attr);
	int nr = s_attr->index;
	int ofs = s_attr->nr;
	struct lm93_data *data = lm93_update_device(dev);
	int mode = LM93_TEMP_OFFSET_MODE_FROM_REG(data->sfc2, nr);
	return sprintf(buf, "%d\n",
	       LM93_TEMP_AUTO_OFFSET_FROM_REG(data->block10.offset[ofs],
					      nr, mode));
}

static ssize_t temp_auto_offset_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *s_attr = to_sensor_dev_attr_2(attr);
	int nr = s_attr->index;
	int ofs = s_attr->nr;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	/* force 0.5C/bit mode */
	data->sfc2 = lm93_read_byte(client, LM93_REG_SFC2);
	data->sfc2 |= ((nr < 2) ? 0x10 : 0x20);
	lm93_write_byte(client, LM93_REG_SFC2, data->sfc2);
	data->block10.offset[ofs] = LM93_TEMP_AUTO_OFFSET_TO_REG(
			data->block10.offset[ofs], val, nr, 1);
	lm93_write_byte(client, LM93_REG_TEMP_OFFSET(ofs),
			data->block10.offset[ofs]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset1, temp_auto_offset, 0, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset2, temp_auto_offset, 1, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset3, temp_auto_offset, 2, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset4, temp_auto_offset, 3, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset5, temp_auto_offset, 4, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset6, temp_auto_offset, 5, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset7, temp_auto_offset, 6, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset8, temp_auto_offset, 7, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset9, temp_auto_offset, 8, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset10, temp_auto_offset, 9, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset11, temp_auto_offset, 10, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_auto_offset12, temp_auto_offset, 11, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset1, temp_auto_offset, 0, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset2, temp_auto_offset, 1, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset3, temp_auto_offset, 2, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset4, temp_auto_offset, 3, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset5, temp_auto_offset, 4, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset6, temp_auto_offset, 5, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset7, temp_auto_offset, 6, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset8, temp_auto_offset, 7, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset9, temp_auto_offset, 8, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset10, temp_auto_offset, 9, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset11, temp_auto_offset, 10, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_auto_offset12, temp_auto_offset, 11, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset1, temp_auto_offset, 0, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset2, temp_auto_offset, 1, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset3, temp_auto_offset, 2, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset4, temp_auto_offset, 3, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset5, temp_auto_offset, 4, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset6, temp_auto_offset, 5, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset7, temp_auto_offset, 6, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset8, temp_auto_offset, 7, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset9, temp_auto_offset, 8, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset10, temp_auto_offset, 9, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset11, temp_auto_offset, 10, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_auto_offset12, temp_auto_offset, 11, 2);

static ssize_t temp_auto_pwm_min_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	u8 reg, ctl4;
	struct lm93_data *data = lm93_update_device(dev);
	reg = data->auto_pwm_min_hyst[nr/2] >> 4 & 0x0f;
	ctl4 = data->block9[nr][LM93_PWM_CTL4];
	return sprintf(buf, "%d\n", LM93_PWM_FROM_REG(reg, (ctl4 & 0x07) ?
				LM93_PWM_MAP_LO_FREQ : LM93_PWM_MAP_HI_FREQ));
}

static ssize_t temp_auto_pwm_min_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 reg, ctl4;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	reg = lm93_read_byte(client, LM93_REG_PWM_MIN_HYST(nr));
	ctl4 = lm93_read_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL4));
	reg = (reg & 0x0f) |
		LM93_PWM_TO_REG(val, (ctl4 & 0x07) ?
				LM93_PWM_MAP_LO_FREQ :
				LM93_PWM_MAP_HI_FREQ) << 4;
	data->auto_pwm_min_hyst[nr/2] = reg;
	lm93_write_byte(client, LM93_REG_PWM_MIN_HYST(nr), reg);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_auto_pwm_min, temp_auto_pwm_min, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_auto_pwm_min, temp_auto_pwm_min, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_auto_pwm_min, temp_auto_pwm_min, 2);

static ssize_t temp_auto_offset_hyst_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	int mode = LM93_TEMP_OFFSET_MODE_FROM_REG(data->sfc2, nr);
	return sprintf(buf, "%d\n", LM93_TEMP_OFFSET_FROM_REG(
					data->auto_pwm_min_hyst[nr / 2], mode));
}

static ssize_t temp_auto_offset_hyst_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 reg;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	/* force 0.5C/bit mode */
	data->sfc2 = lm93_read_byte(client, LM93_REG_SFC2);
	data->sfc2 |= ((nr < 2) ? 0x10 : 0x20);
	lm93_write_byte(client, LM93_REG_SFC2, data->sfc2);
	reg = data->auto_pwm_min_hyst[nr/2];
	reg = (reg & 0xf0) | (LM93_TEMP_OFFSET_TO_REG(val, 1) & 0x0f);
	data->auto_pwm_min_hyst[nr/2] = reg;
	lm93_write_byte(client, LM93_REG_PWM_MIN_HYST(nr), reg);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_auto_offset_hyst, temp_auto_offset_hyst, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_auto_offset_hyst, temp_auto_offset_hyst, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_auto_offset_hyst, temp_auto_offset_hyst, 2);

static ssize_t fan_input_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *s_attr = to_sensor_dev_attr(attr);
	int nr = s_attr->index;
	struct lm93_data *data = lm93_update_device(dev);

	return sprintf(buf, "%d\n", LM93_FAN_FROM_REG(data->block5[nr]));
}

static SENSOR_DEVICE_ATTR_RO(fan1_input, fan_input, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan_input, 1);
static SENSOR_DEVICE_ATTR_RO(fan3_input, fan_input, 2);
static SENSOR_DEVICE_ATTR_RO(fan4_input, fan_input, 3);

static ssize_t fan_min_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);

	return sprintf(buf, "%d\n", LM93_FAN_FROM_REG(data->block8[nr]));
}

static ssize_t fan_min_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->block8[nr] = LM93_FAN_TO_REG(val);
	lm93_write_word(client, LM93_REG_FAN_MIN(nr), data->block8[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(fan1_min, fan_min, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_min, fan_min, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_min, fan_min, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_min, fan_min, 3);

/*
 * some tedious bit-twiddling here to deal with the register format:
 *
 *	data->sf_tach_to_pwm: (tach to pwm mapping bits)
 *
 *		bit |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
 *		     T4:P2 T4:P1 T3:P2 T3:P1 T2:P2 T2:P1 T1:P2 T1:P1
 *
 *	data->sfc2: (enable bits)
 *
 *		bit |  3  |  2  |  1  |  0
 *		       T4    T3    T2    T1
 */

static ssize_t fan_smart_tach_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	long rc = 0;
	int mapping;

	/* extract the relevant mapping */
	mapping = (data->sf_tach_to_pwm >> (nr * 2)) & 0x03;

	/* if there's a mapping and it's enabled */
	if (mapping && ((data->sfc2 >> nr) & 0x01))
		rc = mapping;
	return sprintf(buf, "%ld\n", rc);
}

/*
 * helper function - must grab data->update_lock before calling
 * fan is 0-3, indicating fan1-fan4
 */
static void lm93_write_fan_smart_tach(struct i2c_client *client,
	struct lm93_data *data, int fan, long value)
{
	/* insert the new mapping and write it out */
	data->sf_tach_to_pwm = lm93_read_byte(client, LM93_REG_SF_TACH_TO_PWM);
	data->sf_tach_to_pwm &= ~(0x3 << fan * 2);
	data->sf_tach_to_pwm |= value << fan * 2;
	lm93_write_byte(client, LM93_REG_SF_TACH_TO_PWM, data->sf_tach_to_pwm);

	/* insert the enable bit and write it out */
	data->sfc2 = lm93_read_byte(client, LM93_REG_SFC2);
	if (value)
		data->sfc2 |= 1 << fan;
	else
		data->sfc2 &= ~(1 << fan);
	lm93_write_byte(client, LM93_REG_SFC2, data->sfc2);
}

static ssize_t fan_smart_tach_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	/* sanity test, ignore the write otherwise */
	if (val <= 2) {
		/* can't enable if pwm freq is 22.5KHz */
		if (val) {
			u8 ctl4 = lm93_read_byte(client,
				LM93_REG_PWM_CTL(val - 1, LM93_PWM_CTL4));
			if ((ctl4 & 0x07) == 0)
				val = 0;
		}
		lm93_write_fan_smart_tach(client, data, nr, val);
	}
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(fan1_smart_tach, fan_smart_tach, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_smart_tach, fan_smart_tach, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_smart_tach, fan_smart_tach, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_smart_tach, fan_smart_tach, 3);

static ssize_t pwm_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	u8 ctl2, ctl4;
	long rc;

	ctl2 = data->block9[nr][LM93_PWM_CTL2];
	ctl4 = data->block9[nr][LM93_PWM_CTL4];
	if (ctl2 & 0x01) /* show user commanded value if enabled */
		rc = data->pwm_override[nr];
	else /* show present h/w value if manual pwm disabled */
		rc = LM93_PWM_FROM_REG(ctl2 >> 4, (ctl4 & 0x07) ?
			LM93_PWM_MAP_LO_FREQ : LM93_PWM_MAP_HI_FREQ);
	return sprintf(buf, "%ld\n", rc);
}

static ssize_t pwm_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 ctl2, ctl4;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	ctl2 = lm93_read_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL2));
	ctl4 = lm93_read_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL4));
	ctl2 = (ctl2 & 0x0f) | LM93_PWM_TO_REG(val, (ctl4 & 0x07) ?
			LM93_PWM_MAP_LO_FREQ : LM93_PWM_MAP_HI_FREQ) << 4;
	/* save user commanded value */
	data->pwm_override[nr] = LM93_PWM_FROM_REG(ctl2 >> 4,
			(ctl4 & 0x07) ?  LM93_PWM_MAP_LO_FREQ :
			LM93_PWM_MAP_HI_FREQ);
	lm93_write_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL2), ctl2);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1, pwm, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2, pwm, 1);

static ssize_t pwm_enable_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	u8 ctl2;
	long rc;

	ctl2 = data->block9[nr][LM93_PWM_CTL2];
	if (ctl2 & 0x01) /* manual override enabled ? */
		rc = ((ctl2 & 0xF0) == 0xF0) ? 0 : 1;
	else
		rc = 2;
	return sprintf(buf, "%ld\n", rc);
}

static ssize_t pwm_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 ctl2;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	ctl2 = lm93_read_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL2));

	switch (val) {
	case 0:
		ctl2 |= 0xF1; /* enable manual override, set PWM to max */
		break;
	case 1:
		ctl2 |= 0x01; /* enable manual override */
		break;
	case 2:
		ctl2 &= ~0x01; /* disable manual override */
		break;
	default:
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	lm93_write_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL2), ctl2);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1_enable, pwm_enable, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_enable, pwm_enable, 1);

static ssize_t pwm_freq_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	u8 ctl4;

	ctl4 = data->block9[nr][LM93_PWM_CTL4];
	return sprintf(buf, "%d\n", LM93_PWM_FREQ_FROM_REG(ctl4));
}

/*
 * helper function - must grab data->update_lock before calling
 * pwm is 0-1, indicating pwm1-pwm2
 * this disables smart tach for all tach channels bound to the given pwm
 */
static void lm93_disable_fan_smart_tach(struct i2c_client *client,
	struct lm93_data *data, int pwm)
{
	int mapping = lm93_read_byte(client, LM93_REG_SF_TACH_TO_PWM);
	int mask;

	/* collapse the mapping into a mask of enable bits */
	mapping = (mapping >> pwm) & 0x55;
	mask = mapping & 0x01;
	mask |= (mapping & 0x04) >> 1;
	mask |= (mapping & 0x10) >> 2;
	mask |= (mapping & 0x40) >> 3;

	/* disable smart tach according to the mask */
	data->sfc2 = lm93_read_byte(client, LM93_REG_SFC2);
	data->sfc2 &= ~mask;
	lm93_write_byte(client, LM93_REG_SFC2, data->sfc2);
}

static ssize_t pwm_freq_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 ctl4;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	ctl4 = lm93_read_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL4));
	ctl4 = (ctl4 & 0xf8) | LM93_PWM_FREQ_TO_REG(val);
	data->block9[nr][LM93_PWM_CTL4] = ctl4;
	/* ctl4 == 0 -> 22.5KHz -> disable smart tach */
	if (!ctl4)
		lm93_disable_fan_smart_tach(client, data, nr);
	lm93_write_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL4), ctl4);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1_freq, pwm_freq, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_freq, pwm_freq, 1);

static ssize_t pwm_auto_channels_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", data->block9[nr][LM93_PWM_CTL1]);
}

static ssize_t pwm_auto_channels_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->block9[nr][LM93_PWM_CTL1] = clamp_val(val, 0, 255);
	lm93_write_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL1),
				data->block9[nr][LM93_PWM_CTL1]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1_auto_channels, pwm_auto_channels, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_channels, pwm_auto_channels, 1);

static ssize_t pwm_auto_spinup_min_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	u8 ctl3, ctl4;

	ctl3 = data->block9[nr][LM93_PWM_CTL3];
	ctl4 = data->block9[nr][LM93_PWM_CTL4];
	return sprintf(buf, "%d\n",
		       LM93_PWM_FROM_REG(ctl3 & 0x0f, (ctl4 & 0x07) ?
			LM93_PWM_MAP_LO_FREQ : LM93_PWM_MAP_HI_FREQ));
}

static ssize_t pwm_auto_spinup_min_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 ctl3, ctl4;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	ctl3 = lm93_read_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL3));
	ctl4 = lm93_read_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL4));
	ctl3 = (ctl3 & 0xf0) | LM93_PWM_TO_REG(val, (ctl4 & 0x07) ?
			LM93_PWM_MAP_LO_FREQ :
			LM93_PWM_MAP_HI_FREQ);
	data->block9[nr][LM93_PWM_CTL3] = ctl3;
	lm93_write_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL3), ctl3);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1_auto_spinup_min, pwm_auto_spinup_min, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_spinup_min, pwm_auto_spinup_min, 1);

static ssize_t pwm_auto_spinup_time_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", LM93_SPINUP_TIME_FROM_REG(
				data->block9[nr][LM93_PWM_CTL3]));
}

static ssize_t pwm_auto_spinup_time_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 ctl3;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	ctl3 = lm93_read_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL3));
	ctl3 = (ctl3 & 0x1f) | (LM93_SPINUP_TIME_TO_REG(val) << 5 & 0xe0);
	data->block9[nr][LM93_PWM_CTL3] = ctl3;
	lm93_write_byte(client, LM93_REG_PWM_CTL(nr, LM93_PWM_CTL3), ctl3);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm1_auto_spinup_time, pwm_auto_spinup_time, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_spinup_time, pwm_auto_spinup_time, 1);

static ssize_t pwm_auto_prochot_ramp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n",
		       LM93_RAMP_FROM_REG(data->pwm_ramp_ctl >> 4 & 0x0f));
}

static ssize_t pwm_auto_prochot_ramp_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 ramp;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	ramp = lm93_read_byte(client, LM93_REG_PWM_RAMP_CTL);
	ramp = (ramp & 0x0f) | (LM93_RAMP_TO_REG(val) << 4 & 0xf0);
	lm93_write_byte(client, LM93_REG_PWM_RAMP_CTL, ramp);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR_RW(pwm_auto_prochot_ramp);

static ssize_t pwm_auto_vrdhot_ramp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n",
		       LM93_RAMP_FROM_REG(data->pwm_ramp_ctl & 0x0f));
}

static ssize_t pwm_auto_vrdhot_ramp_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 ramp;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	ramp = lm93_read_byte(client, LM93_REG_PWM_RAMP_CTL);
	ramp = (ramp & 0xf0) | (LM93_RAMP_TO_REG(val) & 0x0f);
	lm93_write_byte(client, LM93_REG_PWM_RAMP_CTL, ramp);
	mutex_unlock(&data->update_lock);
	return 0;
}

static DEVICE_ATTR_RW(pwm_auto_vrdhot_ramp);

static ssize_t vid_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", LM93_VID_FROM_REG(data->vid[nr]));
}

static SENSOR_DEVICE_ATTR_RO(cpu0_vid, vid, 0);
static SENSOR_DEVICE_ATTR_RO(cpu1_vid, vid, 1);

static ssize_t prochot_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", data->block4[nr].cur);
}

static SENSOR_DEVICE_ATTR_RO(prochot1, prochot, 0);
static SENSOR_DEVICE_ATTR_RO(prochot2, prochot, 1);

static ssize_t prochot_avg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", data->block4[nr].avg);
}

static SENSOR_DEVICE_ATTR_RO(prochot1_avg, prochot_avg, 0);
static SENSOR_DEVICE_ATTR_RO(prochot2_avg, prochot_avg, 1);

static ssize_t prochot_max_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", data->prochot_max[nr]);
}

static ssize_t prochot_max_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->prochot_max[nr] = LM93_PROCHOT_TO_REG(val);
	lm93_write_byte(client, LM93_REG_PROCHOT_MAX(nr),
			data->prochot_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(prochot1_max, prochot_max, 0);
static SENSOR_DEVICE_ATTR_RW(prochot2_max, prochot_max, 1);

static const u8 prochot_override_mask[] = { 0x80, 0x40 };

static ssize_t prochot_override_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n",
		(data->prochot_override & prochot_override_mask[nr]) ? 1 : 0);
}

static ssize_t prochot_override_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	if (val)
		data->prochot_override |= prochot_override_mask[nr];
	else
		data->prochot_override &= (~prochot_override_mask[nr]);
	lm93_write_byte(client, LM93_REG_PROCHOT_OVERRIDE,
			data->prochot_override);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(prochot1_override, prochot_override, 0);
static SENSOR_DEVICE_ATTR_RW(prochot2_override, prochot_override, 1);

static ssize_t prochot_interval_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	u8 tmp;
	if (nr == 1)
		tmp = (data->prochot_interval & 0xf0) >> 4;
	else
		tmp = data->prochot_interval & 0x0f;
	return sprintf(buf, "%d\n", LM93_INTERVAL_FROM_REG(tmp));
}

static ssize_t prochot_interval_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 tmp;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	tmp = lm93_read_byte(client, LM93_REG_PROCHOT_INTERVAL);
	if (nr == 1)
		tmp = (tmp & 0x0f) | (LM93_INTERVAL_TO_REG(val) << 4);
	else
		tmp = (tmp & 0xf0) | LM93_INTERVAL_TO_REG(val);
	data->prochot_interval = tmp;
	lm93_write_byte(client, LM93_REG_PROCHOT_INTERVAL, tmp);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(prochot1_interval, prochot_interval, 0);
static SENSOR_DEVICE_ATTR_RW(prochot2_interval, prochot_interval, 1);

static ssize_t prochot_override_duty_cycle_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", data->prochot_override & 0x0f);
}

static ssize_t prochot_override_duty_cycle_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->prochot_override = (data->prochot_override & 0xf0) |
					clamp_val(val, 0, 15);
	lm93_write_byte(client, LM93_REG_PROCHOT_OVERRIDE,
			data->prochot_override);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR_RW(prochot_override_duty_cycle);

static ssize_t prochot_short_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", (data->config & 0x10) ? 1 : 0);
}

static ssize_t prochot_short_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct lm93_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	if (val)
		data->config |= 0x10;
	else
		data->config &= ~0x10;
	lm93_write_byte(client, LM93_REG_CONFIG, data->config);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR_RW(prochot_short);

static ssize_t vrdhot_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int nr = (to_sensor_dev_attr(attr))->index;
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n",
		       data->block1.host_status_1 & (1 << (nr + 4)) ? 1 : 0);
}

static SENSOR_DEVICE_ATTR_RO(vrdhot1, vrdhot, 0);
static SENSOR_DEVICE_ATTR_RO(vrdhot2, vrdhot, 1);

static ssize_t gpio_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", LM93_GPI_FROM_REG(data->gpi));
}

static DEVICE_ATTR_RO(gpio);

static ssize_t alarms_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct lm93_data *data = lm93_update_device(dev);
	return sprintf(buf, "%d\n", LM93_ALARMS_FROM_REG(data->block1));
}

static DEVICE_ATTR_RO(alarms);

static struct attribute *lm93_attrs[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,
	&sensor_dev_attr_in12_input.dev_attr.attr,
	&sensor_dev_attr_in13_input.dev_attr.attr,
	&sensor_dev_attr_in14_input.dev_attr.attr,
	&sensor_dev_attr_in15_input.dev_attr.attr,
	&sensor_dev_attr_in16_input.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_in8_min.dev_attr.attr,
	&sensor_dev_attr_in9_min.dev_attr.attr,
	&sensor_dev_attr_in10_min.dev_attr.attr,
	&sensor_dev_attr_in11_min.dev_attr.attr,
	&sensor_dev_attr_in12_min.dev_attr.attr,
	&sensor_dev_attr_in13_min.dev_attr.attr,
	&sensor_dev_attr_in14_min.dev_attr.attr,
	&sensor_dev_attr_in15_min.dev_attr.attr,
	&sensor_dev_attr_in16_min.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in8_max.dev_attr.attr,
	&sensor_dev_attr_in9_max.dev_attr.attr,
	&sensor_dev_attr_in10_max.dev_attr.attr,
	&sensor_dev_attr_in11_max.dev_attr.attr,
	&sensor_dev_attr_in12_max.dev_attr.attr,
	&sensor_dev_attr_in13_max.dev_attr.attr,
	&sensor_dev_attr_in14_max.dev_attr.attr,
	&sensor_dev_attr_in15_max.dev_attr.attr,
	&sensor_dev_attr_in16_max.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_base.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_base.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_base.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_boost.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_boost.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_boost.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_boost_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_boost_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_boost_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset1.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset2.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset3.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset4.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset5.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset6.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset7.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset8.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset9.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset10.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset11.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset12.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset1.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset2.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset3.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset4.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset5.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset6.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset7.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset8.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset9.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset10.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset11.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset12.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset1.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset2.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset3.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset4.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset5.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset6.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset7.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset8.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset9.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset10.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset11.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset12.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_pwm_min.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_pwm_min.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_pwm_min.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_offset_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_offset_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_offset_hyst.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan4_min.dev_attr.attr,
	&sensor_dev_attr_fan1_smart_tach.dev_attr.attr,
	&sensor_dev_attr_fan2_smart_tach.dev_attr.attr,
	&sensor_dev_attr_fan3_smart_tach.dev_attr.attr,
	&sensor_dev_attr_fan4_smart_tach.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_freq.dev_attr.attr,
	&sensor_dev_attr_pwm2_freq.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_channels.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_channels.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_spinup_min.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_spinup_min.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_spinup_time.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_spinup_time.dev_attr.attr,
	&dev_attr_pwm_auto_prochot_ramp.attr,
	&dev_attr_pwm_auto_vrdhot_ramp.attr,
	&sensor_dev_attr_cpu0_vid.dev_attr.attr,
	&sensor_dev_attr_cpu1_vid.dev_attr.attr,
	&sensor_dev_attr_prochot1.dev_attr.attr,
	&sensor_dev_attr_prochot2.dev_attr.attr,
	&sensor_dev_attr_prochot1_avg.dev_attr.attr,
	&sensor_dev_attr_prochot2_avg.dev_attr.attr,
	&sensor_dev_attr_prochot1_max.dev_attr.attr,
	&sensor_dev_attr_prochot2_max.dev_attr.attr,
	&sensor_dev_attr_prochot1_override.dev_attr.attr,
	&sensor_dev_attr_prochot2_override.dev_attr.attr,
	&sensor_dev_attr_prochot1_interval.dev_attr.attr,
	&sensor_dev_attr_prochot2_interval.dev_attr.attr,
	&dev_attr_prochot_override_duty_cycle.attr,
	&dev_attr_prochot_short.attr,
	&sensor_dev_attr_vrdhot1.dev_attr.attr,
	&sensor_dev_attr_vrdhot2.dev_attr.attr,
	&dev_attr_gpio.attr,
	&dev_attr_alarms.attr,
	NULL
};

ATTRIBUTE_GROUPS(lm93);

static void lm93_init_client(struct i2c_client *client)
{
	int i;
	u8 reg;

	/* configure VID pin input thresholds */
	reg = lm93_read_byte(client, LM93_REG_GPI_VID_CTL);
	lm93_write_byte(client, LM93_REG_GPI_VID_CTL,
			reg | (vid_agtl ? 0x03 : 0x00));

	if (init) {
		/* enable #ALERT pin */
		reg = lm93_read_byte(client, LM93_REG_CONFIG);
		lm93_write_byte(client, LM93_REG_CONFIG, reg | 0x08);

		/* enable ASF mode for BMC status registers */
		reg = lm93_read_byte(client, LM93_REG_STATUS_CONTROL);
		lm93_write_byte(client, LM93_REG_STATUS_CONTROL, reg | 0x02);

		/* set sleep state to S0 */
		lm93_write_byte(client, LM93_REG_SLEEP_CONTROL, 0);

		/* unmask #VRDHOT and dynamic VCCP (if nec) error events */
		reg = lm93_read_byte(client, LM93_REG_MISC_ERR_MASK);
		reg &= ~0x03;
		reg &= ~(vccp_limit_type[0] ? 0x10 : 0);
		reg &= ~(vccp_limit_type[1] ? 0x20 : 0);
		lm93_write_byte(client, LM93_REG_MISC_ERR_MASK, reg);
	}

	/* start monitoring */
	reg = lm93_read_byte(client, LM93_REG_CONFIG);
	lm93_write_byte(client, LM93_REG_CONFIG, reg | 0x01);

	/* spin until ready */
	for (i = 0; i < 20; i++) {
		msleep(10);
		if ((lm93_read_byte(client, LM93_REG_CONFIG) & 0x80) == 0x80)
			return;
	}

	dev_warn(&client->dev,
		 "timed out waiting for sensor chip to signal ready!\n");
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm93_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int mfr, ver;
	const char *name;

	if (!i2c_check_functionality(adapter, LM93_SMBUS_FUNC_MIN))
		return -ENODEV;

	/* detection */
	mfr = lm93_read_byte(client, LM93_REG_MFR_ID);
	if (mfr != 0x01) {
		dev_dbg(&adapter->dev,
			"detect failed, bad manufacturer id 0x%02x!\n", mfr);
		return -ENODEV;
	}

	ver = lm93_read_byte(client, LM93_REG_VER);
	switch (ver) {
	case LM93_MFR_ID:
	case LM93_MFR_ID_PROTOTYPE:
		name = "lm93";
		break;
	case LM94_MFR_ID_2:
	case LM94_MFR_ID:
	case LM94_MFR_ID_PROTOTYPE:
		name = "lm94";
		break;
	default:
		dev_dbg(&adapter->dev,
			"detect failed, bad version id 0x%02x!\n", ver);
		return -ENODEV;
	}

	strlcpy(info->type, name, I2C_NAME_SIZE);
	dev_dbg(&adapter->dev, "loading %s at %d, 0x%02x\n",
		client->name, i2c_adapter_id(client->adapter),
		client->addr);

	return 0;
}

static int lm93_probe(struct i2c_client *client,
		      const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct lm93_data *data;
	struct device *hwmon_dev;
	int func;
	void (*update)(struct lm93_data *, struct i2c_client *);

	/* choose update routine based on bus capabilities */
	func = i2c_get_functionality(client->adapter);
	if (((LM93_SMBUS_FUNC_FULL & func) == LM93_SMBUS_FUNC_FULL) &&
			(!disable_block)) {
		dev_dbg(dev, "using SMBus block data transactions\n");
		update = lm93_update_client_full;
	} else if ((LM93_SMBUS_FUNC_MIN & func) == LM93_SMBUS_FUNC_MIN) {
		dev_dbg(dev, "disabled SMBus block data transactions\n");
		update = lm93_update_client_min;
	} else {
		dev_dbg(dev, "detect failed, smbus byte and/or word data not supported!\n");
		return -ENODEV;
	}

	data = devm_kzalloc(dev, sizeof(struct lm93_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* housekeeping */
	data->client = client;
	data->update = update;
	mutex_init(&data->update_lock);

	/* initialize the chip */
	lm93_init_client(client);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   lm93_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id lm93_id[] = {
	{ "lm93", 0 },
	{ "lm94", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm93_id);

static struct i2c_driver lm93_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm93",
	},
	.probe		= lm93_probe,
	.id_table	= lm93_id,
	.detect		= lm93_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm93_driver);

MODULE_AUTHOR("Mark M. Hoffman <mhoffman@lightlink.com>, "
		"Hans J. Koch <hjk@hansjkoch.de>");
MODULE_DESCRIPTION("LM93 driver");
MODULE_LICENSE("GPL");
