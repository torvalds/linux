/*
 * dme1737.c - driver for the SMSC DME1737 and Asus A8000 Super-I/O chips
 *             integrated hardware monitoring features.
 * Copyright (c) 2007 Juerg Haefliger <juergh@gmail.com>
 *
 * This driver is based on the LM85 driver. The hardware monitoring
 * capabilities of the DME1737 are very similar to the LM85 with some
 * additional features. Even though the DME1737 is a Super-I/O chip, the
 * hardware monitoring registers are only accessible via SMBus.
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
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <asm/io.h>

/* Module load parameters */
static int force_start;
module_param(force_start, bool, 0);
MODULE_PARM_DESC(force_start, "Force the chip to start monitoring inputs");

/* Addresses to scan */
static unsigned short normal_i2c[] = {0x2c, 0x2d, 0x2e, I2C_CLIENT_END};

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(dme1737);

/* ---------------------------------------------------------------------
 * Registers
 *
 * The sensors are defined as follows:
 *
 * Voltages                          Temperatures
 * --------                          ------------
 * in0   +5VTR (+5V stdby)           temp1   Remote diode 1
 * in1   Vccp  (proc core)           temp2   Internal temp
 * in2   VCC   (internal +3.3V)      temp3   Remote diode 2
 * in3   +5V
 * in4   +12V
 * in5   VTR   (+3.3V stby)
 * in6   Vbat
 *
 * --------------------------------------------------------------------- */

/* Voltages (in) numbered 0-6 (ix) */
#define	DME1737_REG_IN(ix)		((ix) < 5 ? 0x20 + (ix) \
						  : 0x94 + (ix))
#define	DME1737_REG_IN_MIN(ix)		((ix) < 5 ? 0x44 + (ix) * 2 \
						  : 0x91 + (ix) * 2)
#define	DME1737_REG_IN_MAX(ix)		((ix) < 5 ? 0x45 + (ix) * 2 \
						  : 0x92 + (ix) * 2)

/* Temperatures (temp) numbered 0-2 (ix) */
#define DME1737_REG_TEMP(ix)		(0x25 + (ix))
#define DME1737_REG_TEMP_MIN(ix)	(0x4e + (ix) * 2)
#define DME1737_REG_TEMP_MAX(ix)	(0x4f + (ix) * 2)
#define DME1737_REG_TEMP_OFFSET(ix)	((ix) == 0 ? 0x1f \
						   : 0x1c + (ix))

/* Voltage and temperature LSBs
 * The LSBs (4 bits each) are stored in 5 registers with the following layouts:
 *    IN_TEMP_LSB(0) = [in5, in6]
 *    IN_TEMP_LSB(1) = [temp3, temp1]
 *    IN_TEMP_LSB(2) = [in4, temp2]
 *    IN_TEMP_LSB(3) = [in3, in0]
 *    IN_TEMP_LSB(4) = [in2, in1] */
#define DME1737_REG_IN_TEMP_LSB(ix)	(0x84 + (ix))
static const u8 DME1737_REG_IN_LSB[] = {3, 4, 4, 3, 2, 0, 0};
static const u8 DME1737_REG_IN_LSB_SHL[] = {4, 4, 0, 0, 0, 0, 4};
static const u8 DME1737_REG_TEMP_LSB[] = {1, 2, 1};
static const u8 DME1737_REG_TEMP_LSB_SHL[] = {4, 4, 0};

/* Fans numbered 0-5 (ix) */
#define DME1737_REG_FAN(ix)		((ix) < 4 ? 0x28 + (ix) * 2 \
						  : 0xa1 + (ix) * 2)
#define DME1737_REG_FAN_MIN(ix)		((ix) < 4 ? 0x54 + (ix) * 2 \
						  : 0xa5 + (ix) * 2)
#define DME1737_REG_FAN_OPT(ix)		((ix) < 4 ? 0x90 + (ix) \
						  : 0xb2 + (ix))
#define DME1737_REG_FAN_MAX(ix)		(0xb4 + (ix)) /* only for fan[4-5] */

/* PWMs numbered 0-2, 4-5 (ix) */
#define DME1737_REG_PWM(ix)		((ix) < 3 ? 0x30 + (ix) \
						  : 0xa1 + (ix))
#define DME1737_REG_PWM_CONFIG(ix)	(0x5c + (ix)) /* only for pwm[0-2] */
#define DME1737_REG_PWM_MIN(ix)		(0x64 + (ix)) /* only for pwm[0-2] */
#define DME1737_REG_PWM_FREQ(ix)	((ix) < 3 ? 0x5f + (ix) \
						  : 0xa3 + (ix))
/* The layout of the ramp rate registers is different from the other pwm
 * registers. The bits for the 3 PWMs are stored in 2 registers:
 *    PWM_RR(0) = [OFF3, OFF2,  OFF1,  RES,   RR1E, RR1-2, RR1-1, RR1-0]
 *    PWM_RR(1) = [RR2E, RR2-2, RR2-1, RR2-0, RR3E, RR3-2, RR3-1, RR3-0] */
#define DME1737_REG_PWM_RR(ix)		(0x62 + (ix)) /* only for pwm[0-2] */

/* Thermal zones 0-2 */
#define DME1737_REG_ZONE_LOW(ix)	(0x67 + (ix))
#define DME1737_REG_ZONE_ABS(ix)	(0x6a + (ix))
/* The layout of the hysteresis registers is different from the other zone
 * registers. The bits for the 3 zones are stored in 2 registers:
 *    ZONE_HYST(0) = [H1-3,  H1-2,  H1-1, H1-0, H2-3, H2-2, H2-1, H2-0]
 *    ZONE_HYST(1) = [H3-3,  H3-2,  H3-1, H3-0, RES,  RES,  RES,  RES] */
#define DME1737_REG_ZONE_HYST(ix)	(0x6d + (ix))

/* Alarm registers and bit mapping
 * The 3 8-bit alarm registers will be concatenated to a single 32-bit
 * alarm value [0, ALARM3, ALARM2, ALARM1]. */
#define DME1737_REG_ALARM1		0x41
#define DME1737_REG_ALARM2		0x42
#define DME1737_REG_ALARM3		0x83
static const u8 DME1737_BIT_ALARM_IN[] = {0, 1, 2, 3, 8, 16, 17};
static const u8 DME1737_BIT_ALARM_TEMP[] = {4, 5, 6};
static const u8 DME1737_BIT_ALARM_FAN[] = {10, 11, 12, 13, 22, 23};

/* Miscellaneous registers */
#define DME1737_REG_COMPANY		0x3e
#define DME1737_REG_VERSTEP		0x3f
#define DME1737_REG_CONFIG		0x40
#define DME1737_REG_CONFIG2		0x7f
#define DME1737_REG_VID			0x43
#define DME1737_REG_TACH_PWM		0x81

/* ---------------------------------------------------------------------
 * Misc defines
 * --------------------------------------------------------------------- */

/* Chip identification */
#define DME1737_COMPANY_SMSC	0x5c
#define DME1737_VERSTEP		0x88
#define DME1737_VERSTEP_MASK	0xf8

/* ---------------------------------------------------------------------
 * Data structures and manipulation thereof
 * --------------------------------------------------------------------- */

struct dme1737_data {
	struct i2c_client client;
	struct class_device *class_dev;

	struct mutex update_lock;
	int valid;			/* !=0 if following fields are valid */
	unsigned long last_update;	/* in jiffies */
	unsigned long last_vbat;	/* in jiffies */

	u8 vid;
	u8 pwm_rr_en;
	u8 has_pwm;
	u8 has_fan;

	/* Register values */
	u16 in[7];
	u8  in_min[7];
	u8  in_max[7];
	s16 temp[3];
	s8  temp_min[3];
	s8  temp_max[3];
	s8  temp_offset[3];
	u8  config;
	u8  config2;
	u8  vrm;
	u16 fan[6];
	u16 fan_min[6];
	u8  fan_max[2];
	u8  fan_opt[6];
	u8  pwm[6];
	u8  pwm_min[3];
	u8  pwm_config[3];
	u8  pwm_acz[3];
	u8  pwm_freq[6];
	u8  pwm_rr[2];
	u8  zone_low[3];
	u8  zone_abs[3];
	u8  zone_hyst[2];
	u32 alarms;
};

/* Nominal voltage values */
static const int IN_NOMINAL[] = {5000, 2250, 3300, 5000, 12000, 3300, 3300};

/* Voltage input
 * Voltage inputs have 16 bits resolution, limit values have 8 bits
 * resolution. */
static inline int IN_FROM_REG(int reg, int ix, int res)
{
	return (reg * IN_NOMINAL[ix] + (3 << (res - 3))) / (3 << (res - 2));
}

static inline int IN_TO_REG(int val, int ix)
{
	return SENSORS_LIMIT((val * 192 + IN_NOMINAL[ix] / 2) /
			     IN_NOMINAL[ix], 0, 255);
}

/* Temperature input
 * The register values represent temperatures in 2's complement notation from
 * -127 degrees C to +127 degrees C. Temp inputs have 16 bits resolution, limit
 * values have 8 bits resolution. */
static inline int TEMP_FROM_REG(int reg, int res)
{
	return (reg * 1000) >> (res - 8);
}

static inline int TEMP_TO_REG(int val)
{
	return SENSORS_LIMIT((val < 0 ? val - 500 : val + 500) / 1000,
			     -128, 127);
}

/* Temperature range */
static const int TEMP_RANGE[] = {2000, 2500, 3333, 4000, 5000, 6666, 8000,
				 10000, 13333, 16000, 20000, 26666, 32000,
				 40000, 53333, 80000};

static inline int TEMP_RANGE_FROM_REG(int reg)
{
	return TEMP_RANGE[(reg >> 4) & 0x0f];
}

static int TEMP_RANGE_TO_REG(int val, int reg)
{
	int i;

	for (i = 15; i > 0; i--) {
		if (val > (TEMP_RANGE[i] + TEMP_RANGE[i - 1] + 1) / 2) {
			break;
		}
	}

	return (reg & 0x0f) | (i << 4);
}

/* Temperature hysteresis
 * Register layout:
 *    reg[0] = [H1-3, H1-2, H1-1, H1-0, H2-3, H2-2, H2-1, H2-0]
 *    reg[1] = [H3-3, H3-2, H3-1, H3-0, xxxx, xxxx, xxxx, xxxx] */
static inline int TEMP_HYST_FROM_REG(int reg, int ix)
{
	return (((ix == 1) ? reg : reg >> 4) & 0x0f) * 1000;
}

static inline int TEMP_HYST_TO_REG(int val, int ix, int reg)
{
	int hyst = SENSORS_LIMIT((val + 500) / 1000, 0, 15);

	return (ix == 1) ? (reg & 0xf0) | hyst : (reg & 0x0f) | (hyst << 4);
}

/* Fan input RPM */
static inline int FAN_FROM_REG(int reg, int tpc)
{
	return (reg == 0 || reg == 0xffff) ? 0 :
		(tpc == 0) ? 90000 * 60 / reg : tpc * reg;
}

static inline int FAN_TO_REG(int val, int tpc)
{
	return SENSORS_LIMIT((tpc == 0) ? 90000 * 60 / val : val / tpc,
			     0, 0xffff);
}

/* Fan TPC (tach pulse count)
 * Converts a register value to a TPC multiplier or returns 0 if the tachometer
 * is configured in legacy (non-tpc) mode */
static inline int FAN_TPC_FROM_REG(int reg)
{
	return (reg & 0x20) ? 0 : 60 >> (reg & 0x03);
}

/* Fan type
 * The type of a fan is expressed in number of pulses-per-revolution that it
 * emits */
static inline int FAN_TYPE_FROM_REG(int reg)
{
	int edge = (reg >> 1) & 0x03;

	return (edge > 0) ? 1 << (edge - 1) : 0;
}

static inline int FAN_TYPE_TO_REG(int val, int reg)
{
	int edge = (val == 4) ? 3 : val;

	return (reg & 0xf9) | (edge << 1);
}

/* Fan max RPM */
static const int FAN_MAX[] = {0x54, 0x38, 0x2a, 0x21, 0x1c, 0x18, 0x15, 0x12,
			      0x11, 0x0f, 0x0e};

static int FAN_MAX_FROM_REG(int reg)
{
	int i;

	for (i = 10; i > 0; i--) {
		if (reg == FAN_MAX[i]) {
			break;
		}
	}

	return 1000 + i * 500;
}

static int FAN_MAX_TO_REG(int val)
{
	int i;

	for (i = 10; i > 0; i--) {
		if (val > (1000 + (i - 1) * 500)) {
			break;
		}
	}

	return FAN_MAX[i];
}

/* PWM enable
 * Register to enable mapping:
 * 000:  2  fan on zone 1 auto
 * 001:  2  fan on zone 2 auto
 * 010:  2  fan on zone 3 auto
 * 011:  0  fan full on
 * 100: -1  fan disabled
 * 101:  2  fan on hottest of zones 2,3 auto
 * 110:  2  fan on hottest of zones 1,2,3 auto
 * 111:  1  fan in manual mode */
static inline int PWM_EN_FROM_REG(int reg)
{
	static const int en[] = {2, 2, 2, 0, -1, 2, 2, 1};

	return en[(reg >> 5) & 0x07];
}

static inline int PWM_EN_TO_REG(int val, int reg)
{
	int en = (val == 1) ? 7 : 3;

	return (reg & 0x1f) | ((en & 0x07) << 5);
}

/* PWM auto channels zone
 * Register to auto channels zone mapping (ACZ is a bitfield with bit x
 * corresponding to zone x+1):
 * 000: 001  fan on zone 1 auto
 * 001: 010  fan on zone 2 auto
 * 010: 100  fan on zone 3 auto
 * 011: 000  fan full on
 * 100: 000  fan disabled
 * 101: 110  fan on hottest of zones 2,3 auto
 * 110: 111  fan on hottest of zones 1,2,3 auto
 * 111: 000  fan in manual mode */
static inline int PWM_ACZ_FROM_REG(int reg)
{
	static const int acz[] = {1, 2, 4, 0, 0, 6, 7, 0};

	return acz[(reg >> 5) & 0x07];
}

static inline int PWM_ACZ_TO_REG(int val, int reg)
{
	int acz = (val == 4) ? 2 : val - 1;

	return (reg & 0x1f) | ((acz & 0x07) << 5);
}

/* PWM frequency */
static const int PWM_FREQ[] = {11, 15, 22, 29, 35, 44, 59, 88,
			       15000, 20000, 30000, 25000, 0, 0, 0, 0};

static inline int PWM_FREQ_FROM_REG(int reg)
{
	return PWM_FREQ[reg & 0x0f];
}

static int PWM_FREQ_TO_REG(int val, int reg)
{
	int i;

	/* the first two cases are special - stupid chip design! */
	if (val > 27500) {
		i = 10;
	} else if (val > 22500) {
		i = 11;
	} else {
		for (i = 9; i > 0; i--) {
			if (val > (PWM_FREQ[i] + PWM_FREQ[i - 1] + 1) / 2) {
				break;
			}
		}
	}

	return (reg & 0xf0) | i;
}

/* PWM ramp rate
 * Register layout:
 *    reg[0] = [OFF3,  OFF2,  OFF1,  RES,   RR1-E, RR1-2, RR1-1, RR1-0]
 *    reg[1] = [RR2-E, RR2-2, RR2-1, RR2-0, RR3-E, RR3-2, RR3-1, RR3-0] */
static const u8 PWM_RR[] = {206, 104, 69, 41, 26, 18, 10, 5};

static inline int PWM_RR_FROM_REG(int reg, int ix)
{
	int rr = (ix == 1) ? reg >> 4 : reg;

	return (rr & 0x08) ? PWM_RR[rr & 0x07] : 0;
}

static int PWM_RR_TO_REG(int val, int ix, int reg)
{
	int i;

	for (i = 0; i < 7; i++) {
		if (val > (PWM_RR[i] + PWM_RR[i + 1] + 1) / 2) {
			break;
		}
	}

	return (ix == 1) ? (reg & 0x8f) | (i << 4) : (reg & 0xf8) | i;
}

/* PWM ramp rate enable */
static inline int PWM_RR_EN_FROM_REG(int reg, int ix)
{
	return PWM_RR_FROM_REG(reg, ix) ? 1 : 0;
}

static inline int PWM_RR_EN_TO_REG(int val, int ix, int reg)
{
	int en = (ix == 1) ? 0x80 : 0x08;

	return val ? reg | en : reg & ~en;
}

/* PWM min/off
 * The PWM min/off bits are part of the PMW ramp rate register 0 (see above for
 * the register layout). */
static inline int PWM_OFF_FROM_REG(int reg, int ix)
{
	return (reg >> (ix + 5)) & 0x01;
}

static inline int PWM_OFF_TO_REG(int val, int ix, int reg)
{
	return (reg & ~(1 << (ix + 5))) | ((val & 0x01) << (ix + 5));
}

/* ---------------------------------------------------------------------
 * Device I/O access
 * --------------------------------------------------------------------- */

static u8 dme1737_read(struct i2c_client *client, u8 reg)
{
	s32 val = i2c_smbus_read_byte_data(client, reg);

	if (val < 0) {
		dev_warn(&client->dev, "Read from register 0x%02x failed! "
			 "Please report to the driver maintainer.\n", reg);
	}

	return val;
}

static s32 dme1737_write(struct i2c_client *client, u8 reg, u8 value)
{
	s32 res = i2c_smbus_write_byte_data(client, reg, value);

	if (res < 0) {
		dev_warn(&client->dev, "Write to register 0x%02x failed! "
			 "Please report to the driver maintainer.\n", reg);
	}

	return res;
}

static struct dme1737_data *dme1737_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dme1737_data *data = i2c_get_clientdata(client);
	int ix;
	u8 lsb[5];

	mutex_lock(&data->update_lock);

	/* Enable a Vbat monitoring cycle every 10 mins */
	if (time_after(jiffies, data->last_vbat + 600 * HZ) || !data->valid) {
		dme1737_write(client, DME1737_REG_CONFIG, dme1737_read(client,
						DME1737_REG_CONFIG) | 0x10);
		data->last_vbat = jiffies;
	}

	/* Sample register contents every 1 sec */
	if (time_after(jiffies, data->last_update + HZ) || !data->valid) {
		data->vid = dme1737_read(client, DME1737_REG_VID) & 0x3f;

		/* In (voltage) registers */
		for (ix = 0; ix < ARRAY_SIZE(data->in); ix++) {
			/* Voltage inputs are stored as 16 bit values even
			 * though they have only 12 bits resolution. This is
			 * to make it consistent with the temp inputs. */
			data->in[ix] = dme1737_read(client,
					DME1737_REG_IN(ix)) << 8;
			data->in_min[ix] = dme1737_read(client,
					DME1737_REG_IN_MIN(ix));
			data->in_max[ix] = dme1737_read(client,
					DME1737_REG_IN_MAX(ix));
		}

		/* Temp registers */
		for (ix = 0; ix < ARRAY_SIZE(data->temp); ix++) {
			/* Temp inputs are stored as 16 bit values even
			 * though they have only 12 bits resolution. This is
			 * to take advantage of implicit conversions between
			 * register values (2's complement) and temp values
			 * (signed decimal). */
			data->temp[ix] = dme1737_read(client,
					DME1737_REG_TEMP(ix)) << 8;
			data->temp_min[ix] = dme1737_read(client,
					DME1737_REG_TEMP_MIN(ix));
			data->temp_max[ix] = dme1737_read(client,
					DME1737_REG_TEMP_MAX(ix));
			data->temp_offset[ix] = dme1737_read(client,
					DME1737_REG_TEMP_OFFSET(ix));
		}

		/* In and temp LSB registers
		 * The LSBs are latched when the MSBs are read, so the order in
		 * which the registers are read (MSB first, then LSB) is
		 * important! */
		for (ix = 0; ix < ARRAY_SIZE(lsb); ix++) {
			lsb[ix] = dme1737_read(client,
					DME1737_REG_IN_TEMP_LSB(ix));
		}
		for (ix = 0; ix < ARRAY_SIZE(data->in); ix++) {
			data->in[ix] |= (lsb[DME1737_REG_IN_LSB[ix]] <<
					DME1737_REG_IN_LSB_SHL[ix]) & 0xf0;
		}
		for (ix = 0; ix < ARRAY_SIZE(data->temp); ix++) {
			data->temp[ix] |= (lsb[DME1737_REG_TEMP_LSB[ix]] <<
					DME1737_REG_TEMP_LSB_SHL[ix]) & 0xf0;
		}

		/* Fan registers */
		for (ix = 0; ix < ARRAY_SIZE(data->fan); ix++) {
			/* Skip reading registers if optional fans are not
			 * present */
			if (!(data->has_fan & (1 << ix))) {
				continue;
			}
			data->fan[ix] = dme1737_read(client,
					DME1737_REG_FAN(ix));
			data->fan[ix] |= dme1737_read(client,
					DME1737_REG_FAN(ix) + 1) << 8;
			data->fan_min[ix] = dme1737_read(client,
					DME1737_REG_FAN_MIN(ix));
			data->fan_min[ix] |= dme1737_read(client,
					DME1737_REG_FAN_MIN(ix) + 1) << 8;
			data->fan_opt[ix] = dme1737_read(client,
					DME1737_REG_FAN_OPT(ix));
			/* fan_max exists only for fan[5-6] */
			if (ix > 3) {
				data->fan_max[ix - 4] = dme1737_read(client,
					DME1737_REG_FAN_MAX(ix));
			}
		}

		/* PWM registers */
		for (ix = 0; ix < ARRAY_SIZE(data->pwm); ix++) {
			/* Skip reading registers if optional PWMs are not
			 * present */
			if (!(data->has_pwm & (1 << ix))) {
				continue;
			}
			data->pwm[ix] = dme1737_read(client,
					DME1737_REG_PWM(ix));
			data->pwm_freq[ix] = dme1737_read(client,
					DME1737_REG_PWM_FREQ(ix));
			/* pwm_config and pwm_min exist only for pwm[1-3] */
			if (ix < 3) {
				data->pwm_config[ix] = dme1737_read(client,
						DME1737_REG_PWM_CONFIG(ix));
				data->pwm_min[ix] = dme1737_read(client,
						DME1737_REG_PWM_MIN(ix));
			}
		}
		for (ix = 0; ix < ARRAY_SIZE(data->pwm_rr); ix++) {
			data->pwm_rr[ix] = dme1737_read(client,
						DME1737_REG_PWM_RR(ix));
		}

		/* Thermal zone registers */
		for (ix = 0; ix < ARRAY_SIZE(data->zone_low); ix++) {
			data->zone_low[ix] = dme1737_read(client,
					DME1737_REG_ZONE_LOW(ix));
			data->zone_abs[ix] = dme1737_read(client,
					DME1737_REG_ZONE_ABS(ix));
		}
		for (ix = 0; ix < ARRAY_SIZE(data->zone_hyst); ix++) {
			data->zone_hyst[ix] = dme1737_read(client,
						DME1737_REG_ZONE_HYST(ix));
		}

		/* Alarm registers */
		data->alarms = dme1737_read(client,
						DME1737_REG_ALARM1);
		/* Bit 7 tells us if the other alarm registers are non-zero and
		 * therefore also need to be read */
		if (data->alarms & 0x80) {
			data->alarms |= dme1737_read(client,
						DME1737_REG_ALARM2) << 8;
			data->alarms |= dme1737_read(client,
						DME1737_REG_ALARM3) << 16;
		}

		data->last_update = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* ---------------------------------------------------------------------
 * Voltage sysfs attributes
 * ix = [0-5]
 * --------------------------------------------------------------------- */

#define SYS_IN_INPUT	0
#define SYS_IN_MIN	1
#define SYS_IN_MAX	2
#define SYS_IN_ALARM	3

static ssize_t show_in(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct dme1737_data *data = dme1737_update_device(dev);
	struct sensor_device_attribute_2
		*sensor_attr_2 = to_sensor_dev_attr_2(attr);
	int ix = sensor_attr_2->index;
	int fn = sensor_attr_2->nr;
	int res;

	switch (fn) {
	case SYS_IN_INPUT:
		res = IN_FROM_REG(data->in[ix], ix, 16);
		break;
	case SYS_IN_MIN:
		res = IN_FROM_REG(data->in_min[ix], ix, 8);
		break;
	case SYS_IN_MAX:
		res = IN_FROM_REG(data->in_max[ix], ix, 8);
		break;
	case SYS_IN_ALARM:
		res = (data->alarms >> DME1737_BIT_ALARM_IN[ix]) & 0x01;
		break;
	default:
		res = 0;
		dev_dbg(dev, "Unknown attr fetch (%d)\n", fn);
	}

	return sprintf(buf, "%d\n", res);
}

static ssize_t set_in(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dme1737_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2
		*sensor_attr_2 = to_sensor_dev_attr_2(attr);
	int ix = sensor_attr_2->index;
	int fn = sensor_attr_2->nr;
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	switch (fn) {
	case SYS_IN_MIN:
		data->in_min[ix] = IN_TO_REG(val, ix);
		dme1737_write(client, DME1737_REG_IN_MIN(ix),
			      data->in_min[ix]);
		break;
	case SYS_IN_MAX:
		data->in_max[ix] = IN_TO_REG(val, ix);
		dme1737_write(client, DME1737_REG_IN_MAX(ix),
			      data->in_max[ix]);
		break;
	default:
		dev_dbg(dev, "Unknown attr fetch (%d)\n", fn);
	}
	mutex_unlock(&data->update_lock);

	return count;
}

/* ---------------------------------------------------------------------
 * Temperature sysfs attributes
 * ix = [0-2]
 * --------------------------------------------------------------------- */

#define SYS_TEMP_INPUT			0
#define SYS_TEMP_MIN			1
#define SYS_TEMP_MAX			2
#define SYS_TEMP_OFFSET			3
#define SYS_TEMP_ALARM			4
#define SYS_TEMP_FAULT			5

static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct dme1737_data *data = dme1737_update_device(dev);
	struct sensor_device_attribute_2
		*sensor_attr_2 = to_sensor_dev_attr_2(attr);
	int ix = sensor_attr_2->index;
	int fn = sensor_attr_2->nr;
	int res;

	switch (fn) {
	case SYS_TEMP_INPUT:
		res = TEMP_FROM_REG(data->temp[ix], 16);
		break;
	case SYS_TEMP_MIN:
		res = TEMP_FROM_REG(data->temp_min[ix], 8);
		break;
	case SYS_TEMP_MAX:
		res = TEMP_FROM_REG(data->temp_max[ix], 8);
		break;
	case SYS_TEMP_OFFSET:
		res = TEMP_FROM_REG(data->temp_offset[ix], 8);
		break;
	case SYS_TEMP_ALARM:
		res = (data->alarms >> DME1737_BIT_ALARM_TEMP[ix]) & 0x01;
		break;
	case SYS_TEMP_FAULT:
		res = (((u16)data->temp[ix] & 0xff00) == 0x8000);
		break;
	default:
		res = 0;
		dev_dbg(dev, "Unknown attr fetch (%d)\n", fn);
	}

	return sprintf(buf, "%d\n", res);
}

static ssize_t set_temp(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dme1737_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2
		*sensor_attr_2 = to_sensor_dev_attr_2(attr);
	int ix = sensor_attr_2->index;
	int fn = sensor_attr_2->nr;
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	switch (fn) {
	case SYS_TEMP_MIN:
		data->temp_min[ix] = TEMP_TO_REG(val);
		dme1737_write(client, DME1737_REG_TEMP_MIN(ix),
			      data->temp_min[ix]);
		break;
	case SYS_TEMP_MAX:
		data->temp_max[ix] = TEMP_TO_REG(val);
		dme1737_write(client, DME1737_REG_TEMP_MAX(ix),
			      data->temp_max[ix]);
		break;
	case SYS_TEMP_OFFSET:
		data->temp_offset[ix] = TEMP_TO_REG(val);
		dme1737_write(client, DME1737_REG_TEMP_OFFSET(ix),
			      data->temp_offset[ix]);
		break;
	default:
		dev_dbg(dev, "Unknown attr fetch (%d)\n", fn);
	}
	mutex_unlock(&data->update_lock);

	return count;
}

/* ---------------------------------------------------------------------
 * Zone sysfs attributes
 * ix = [0-2]
 * --------------------------------------------------------------------- */

#define SYS_ZONE_AUTO_CHANNELS_TEMP	0
#define SYS_ZONE_AUTO_POINT1_TEMP_HYST	1
#define SYS_ZONE_AUTO_POINT1_TEMP	2
#define SYS_ZONE_AUTO_POINT2_TEMP	3
#define SYS_ZONE_AUTO_POINT3_TEMP	4

static ssize_t show_zone(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct dme1737_data *data = dme1737_update_device(dev);
	struct sensor_device_attribute_2
		*sensor_attr_2 = to_sensor_dev_attr_2(attr);
	int ix = sensor_attr_2->index;
	int fn = sensor_attr_2->nr;
	int res;

	switch (fn) {
	case SYS_ZONE_AUTO_CHANNELS_TEMP:
		/* check config2 for non-standard temp-to-zone mapping */
		if ((ix == 1) && (data->config2 & 0x02)) {
			res = 4;
		} else {
			res = 1 << ix;
		}
		break;
	case SYS_ZONE_AUTO_POINT1_TEMP_HYST:
		res = TEMP_FROM_REG(data->zone_low[ix], 8) -
		      TEMP_HYST_FROM_REG(data->zone_hyst[ix == 2], ix);
		break;
	case SYS_ZONE_AUTO_POINT1_TEMP:
		res = TEMP_FROM_REG(data->zone_low[ix], 8);
		break;
	case SYS_ZONE_AUTO_POINT2_TEMP:
		/* pwm_freq holds the temp range bits in the upper nibble */
		res = TEMP_FROM_REG(data->zone_low[ix], 8) +
		      TEMP_RANGE_FROM_REG(data->pwm_freq[ix]);
		break;
	case SYS_ZONE_AUTO_POINT3_TEMP:
		res = TEMP_FROM_REG(data->zone_abs[ix], 8);
		break;
	default:
		res = 0;
		dev_dbg(dev, "Unknown attr fetch (%d)\n", fn);
	}

	return sprintf(buf, "%d\n", res);
}

static ssize_t set_zone(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dme1737_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2
		*sensor_attr_2 = to_sensor_dev_attr_2(attr);
	int ix = sensor_attr_2->index;
	int fn = sensor_attr_2->nr;
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	switch (fn) {
	case SYS_ZONE_AUTO_POINT1_TEMP_HYST:
		/* Refresh the cache */
		data->zone_low[ix] = dme1737_read(client,
						  DME1737_REG_ZONE_LOW(ix));
		/* Modify the temp hyst value */
		data->zone_hyst[ix == 2] = TEMP_HYST_TO_REG(
					TEMP_FROM_REG(data->zone_low[ix], 8) -
					val, ix, dme1737_read(client,
					DME1737_REG_ZONE_HYST(ix == 2)));
		dme1737_write(client, DME1737_REG_ZONE_HYST(ix == 2),
			      data->zone_hyst[ix == 2]);
		break;
	case SYS_ZONE_AUTO_POINT1_TEMP:
		data->zone_low[ix] = TEMP_TO_REG(val);
		dme1737_write(client, DME1737_REG_ZONE_LOW(ix),
			      data->zone_low[ix]);
		break;
	case SYS_ZONE_AUTO_POINT2_TEMP:
		/* Refresh the cache */
		data->zone_low[ix] = dme1737_read(client,
						  DME1737_REG_ZONE_LOW(ix));
		/* Modify the temp range value (which is stored in the upper
		 * nibble of the pwm_freq register) */
		data->pwm_freq[ix] = TEMP_RANGE_TO_REG(val -
					TEMP_FROM_REG(data->zone_low[ix], 8),
					dme1737_read(client,
					DME1737_REG_PWM_FREQ(ix)));
		dme1737_write(client, DME1737_REG_PWM_FREQ(ix),
			      data->pwm_freq[ix]);
		break;
	case SYS_ZONE_AUTO_POINT3_TEMP:
		data->zone_abs[ix] = TEMP_TO_REG(val);
		dme1737_write(client, DME1737_REG_ZONE_ABS(ix),
			      data->zone_abs[ix]);
		break;
	default:
		dev_dbg(dev, "Unknown attr fetch (%d)\n", fn);
	}
	mutex_unlock(&data->update_lock);

	return count;
}

/* ---------------------------------------------------------------------
 * Fan sysfs attributes
 * ix = [0-5]
 * --------------------------------------------------------------------- */

#define SYS_FAN_INPUT	0
#define SYS_FAN_MIN	1
#define SYS_FAN_MAX	2
#define SYS_FAN_ALARM	3
#define SYS_FAN_TYPE	4

static ssize_t show_fan(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct dme1737_data *data = dme1737_update_device(dev);
	struct sensor_device_attribute_2
		*sensor_attr_2 = to_sensor_dev_attr_2(attr);
	int ix = sensor_attr_2->index;
	int fn = sensor_attr_2->nr;
	int res;

	switch (fn) {
	case SYS_FAN_INPUT:
		res = FAN_FROM_REG(data->fan[ix],
				   ix < 4 ? 0 :
				   FAN_TPC_FROM_REG(data->fan_opt[ix]));
		break;
	case SYS_FAN_MIN:
		res = FAN_FROM_REG(data->fan_min[ix],
				   ix < 4 ? 0 :
				   FAN_TPC_FROM_REG(data->fan_opt[ix]));
		break;
	case SYS_FAN_MAX:
		/* only valid for fan[5-6] */
		res = FAN_MAX_FROM_REG(data->fan_max[ix - 4]);
		break;
	case SYS_FAN_ALARM:
		res = (data->alarms >> DME1737_BIT_ALARM_FAN[ix]) & 0x01;
		break;
	case SYS_FAN_TYPE:
		/* only valid for fan[1-4] */
		res = FAN_TYPE_FROM_REG(data->fan_opt[ix]);
		break;
	default:
		res = 0;
		dev_dbg(dev, "Unknown attr fetch (%d)\n", fn);
	}

	return sprintf(buf, "%d\n", res);
}

static ssize_t set_fan(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dme1737_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2
		*sensor_attr_2 = to_sensor_dev_attr_2(attr);
	int ix = sensor_attr_2->index;
	int fn = sensor_attr_2->nr;
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	switch (fn) {
	case SYS_FAN_MIN:
		if (ix < 4) {
			data->fan_min[ix] = FAN_TO_REG(val, 0);
		} else {
			/* Refresh the cache */
			data->fan_opt[ix] = dme1737_read(client,
						DME1737_REG_FAN_OPT(ix));
			/* Modify the fan min value */
			data->fan_min[ix] = FAN_TO_REG(val,
					FAN_TPC_FROM_REG(data->fan_opt[ix]));
		}
		dme1737_write(client, DME1737_REG_FAN_MIN(ix),
			      data->fan_min[ix] & 0xff);
		dme1737_write(client, DME1737_REG_FAN_MIN(ix) + 1,
			      data->fan_min[ix] >> 8);
		break;
	case SYS_FAN_MAX:
		/* Only valid for fan[5-6] */
		data->fan_max[ix - 4] = FAN_MAX_TO_REG(val);
		dme1737_write(client, DME1737_REG_FAN_MAX(ix),
			      data->fan_max[ix - 4]);
		break;
	case SYS_FAN_TYPE:
		/* Only valid for fan[1-4] */
		if (!(val == 1 || val == 2 || val == 4)) {
			count = -EINVAL;
			dev_warn(&client->dev, "Fan type value %ld not "
				 "supported. Choose one of 1, 2, or 4.\n",
				 val);
			goto exit;
		}
		data->fan_opt[ix] = FAN_TYPE_TO_REG(val, dme1737_read(client,
					DME1737_REG_FAN_OPT(ix)));
		dme1737_write(client, DME1737_REG_FAN_OPT(ix),
			      data->fan_opt[ix]);
		break;
	default:
		dev_dbg(dev, "Unknown attr fetch (%d)\n", fn);
	}
exit:
	mutex_unlock(&data->update_lock);

	return count;
}

/* ---------------------------------------------------------------------
 * PWM sysfs attributes
 * ix = [0-4]
 * --------------------------------------------------------------------- */

#define SYS_PWM				0
#define SYS_PWM_FREQ			1
#define SYS_PWM_ENABLE			2
#define SYS_PWM_RAMP_RATE		3
#define SYS_PWM_AUTO_CHANNELS_ZONE	4
#define SYS_PWM_AUTO_PWM_MIN		5
#define SYS_PWM_AUTO_POINT1_PWM		6
#define SYS_PWM_AUTO_POINT2_PWM		7

static ssize_t show_pwm(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct dme1737_data *data = dme1737_update_device(dev);
	struct sensor_device_attribute_2
		*sensor_attr_2 = to_sensor_dev_attr_2(attr);
	int ix = sensor_attr_2->index;
	int fn = sensor_attr_2->nr;
	int res;

	switch (fn) {
	case SYS_PWM:
		if (PWM_EN_FROM_REG(data->pwm_config[ix]) == 0) {
			res = 255;
		} else {
			res = data->pwm[ix];
		}
		break;
	case SYS_PWM_FREQ:
		res = PWM_FREQ_FROM_REG(data->pwm_freq[ix]);
		break;
	case SYS_PWM_ENABLE:
		if (ix > 3) {
			res = 1; /* pwm[5-6] hard-wired to manual mode */
		} else {
			res = PWM_EN_FROM_REG(data->pwm_config[ix]);
		}
		break;
	case SYS_PWM_RAMP_RATE:
		/* Only valid for pwm[1-3] */
		res = PWM_RR_FROM_REG(data->pwm_rr[ix > 0], ix);
		break;
	case SYS_PWM_AUTO_CHANNELS_ZONE:
		/* Only valid for pwm[1-3] */
		if (PWM_EN_FROM_REG(data->pwm_config[ix]) == 2) {
			res = PWM_ACZ_FROM_REG(data->pwm_config[ix]);
		} else {
			res = data->pwm_acz[ix];
		}
		break;
	case SYS_PWM_AUTO_PWM_MIN:
		/* Only valid for pwm[1-3] */
		if (PWM_OFF_FROM_REG(data->pwm_rr[0], ix)) {
			res = data->pwm_min[ix];
		} else {
			res = 0;
		}
		break;
	case SYS_PWM_AUTO_POINT1_PWM:
		/* Only valid for pwm[1-3] */
		res = data->pwm_min[ix];
		break;
	case SYS_PWM_AUTO_POINT2_PWM:
		/* Only valid for pwm[1-3] */
		res = 255; /* hard-wired */
		break;
	default:
		res = 0;
		dev_dbg(dev, "Unknown attr fetch (%d)\n", fn);
	}

	return sprintf(buf, "%d\n", res);
}

static struct attribute *dme1737_attr_pwm[];
static void dme1737_chmod_file(struct i2c_client*, struct attribute*, mode_t);

static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dme1737_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2
		*sensor_attr_2 = to_sensor_dev_attr_2(attr);
	int ix = sensor_attr_2->index;
	int fn = sensor_attr_2->nr;
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	switch (fn) {
	case SYS_PWM:
		data->pwm[ix] = SENSORS_LIMIT(val, 0, 255);
		dme1737_write(client, DME1737_REG_PWM(ix), data->pwm[ix]);
		break;
	case SYS_PWM_FREQ:
		data->pwm_freq[ix] = PWM_FREQ_TO_REG(val, dme1737_read(client,
						DME1737_REG_PWM_FREQ(ix)));
		dme1737_write(client, DME1737_REG_PWM_FREQ(ix),
			      data->pwm_freq[ix]);
		break;
	case SYS_PWM_ENABLE:
		/* Only valid for pwm[1-3] */
		if (val < 0 || val > 2) {
			count = -EINVAL;
			dev_warn(&client->dev, "PWM enable %ld not "
				 "supported. Choose one of 0, 1, or 2.\n",
				 val);
			goto exit;
		}
		/* Refresh the cache */
		data->pwm_config[ix] = dme1737_read(client,
						DME1737_REG_PWM_CONFIG(ix));
		if (val == PWM_EN_FROM_REG(data->pwm_config[ix])) {
			/* Bail out if no change */
			goto exit;
		}
		/* Do some housekeeping if we are currently in auto mode */
		if (PWM_EN_FROM_REG(data->pwm_config[ix]) == 2) {
			/* Save the current zone channel assignment */
			data->pwm_acz[ix] = PWM_ACZ_FROM_REG(
							data->pwm_config[ix]);
			/* Save the current ramp rate state and disable it */
			data->pwm_rr[ix > 0] = dme1737_read(client,
						DME1737_REG_PWM_RR(ix > 0));
			data->pwm_rr_en &= ~(1 << ix);
			if (PWM_RR_EN_FROM_REG(data->pwm_rr[ix > 0], ix)) {
				data->pwm_rr_en |= (1 << ix);
				data->pwm_rr[ix > 0] = PWM_RR_EN_TO_REG(0, ix,
							data->pwm_rr[ix > 0]);
				dme1737_write(client,
					      DME1737_REG_PWM_RR(ix > 0),
					      data->pwm_rr[ix > 0]);
			}
		}
		/* Set the new PWM mode */
		switch (val) {
		case 0:
			/* Change permissions of pwm[ix] to read-only */
			dme1737_chmod_file(client, dme1737_attr_pwm[ix],
					   S_IRUGO);
			/* Turn fan fully on */
			data->pwm_config[ix] = PWM_EN_TO_REG(0,
							data->pwm_config[ix]);
			dme1737_write(client, DME1737_REG_PWM_CONFIG(ix),
				      data->pwm_config[ix]);
			break;
		case 1:
			/* Turn on manual mode */
			data->pwm_config[ix] = PWM_EN_TO_REG(1,
							data->pwm_config[ix]);
			dme1737_write(client, DME1737_REG_PWM_CONFIG(ix),
				      data->pwm_config[ix]);
			/* Change permissions of pwm[ix] to read-writeable */
			dme1737_chmod_file(client, dme1737_attr_pwm[ix],
					   S_IRUGO | S_IWUSR);
			break;
		case 2:
			/* Change permissions of pwm[ix] to read-only */
			dme1737_chmod_file(client, dme1737_attr_pwm[ix],
					   S_IRUGO);
			/* Turn on auto mode using the saved zone channel
			 * assignment */
			data->pwm_config[ix] = PWM_ACZ_TO_REG(
							data->pwm_acz[ix],
							data->pwm_config[ix]);
			dme1737_write(client, DME1737_REG_PWM_CONFIG(ix),
				      data->pwm_config[ix]);
			/* Enable PWM ramp rate if previously enabled */
			if (data->pwm_rr_en & (1 << ix)) {
				data->pwm_rr[ix > 0] = PWM_RR_EN_TO_REG(1, ix,
						dme1737_read(client,
						DME1737_REG_PWM_RR(ix > 0)));
				dme1737_write(client,
					      DME1737_REG_PWM_RR(ix > 0),
					      data->pwm_rr[ix > 0]);
			}
			break;
		}
		break;
	case SYS_PWM_RAMP_RATE:
		/* Only valid for pwm[1-3] */
		/* Refresh the cache */
		data->pwm_config[ix] = dme1737_read(client,
						DME1737_REG_PWM_CONFIG(ix));
		data->pwm_rr[ix > 0] = dme1737_read(client,
						DME1737_REG_PWM_RR(ix > 0));
		/* Set the ramp rate value */
		if (val > 0) {
			data->pwm_rr[ix > 0] = PWM_RR_TO_REG(val, ix,
							data->pwm_rr[ix > 0]);
		}
		/* Enable/disable the feature only if the associated PWM
		 * output is in automatic mode. */
		if (PWM_EN_FROM_REG(data->pwm_config[ix]) == 2) {
			data->pwm_rr[ix > 0] = PWM_RR_EN_TO_REG(val > 0, ix,
							data->pwm_rr[ix > 0]);
		}
		dme1737_write(client, DME1737_REG_PWM_RR(ix > 0),
			      data->pwm_rr[ix > 0]);
		break;
	case SYS_PWM_AUTO_CHANNELS_ZONE:
		/* Only valid for pwm[1-3] */
		if (!(val == 1 || val == 2 || val == 4 ||
		      val == 6 || val == 7)) {
			count = -EINVAL;
			dev_warn(&client->dev, "PWM auto channels zone %ld "
				 "not supported. Choose one of 1, 2, 4, 6, "
				 "or 7.\n", val);
			goto exit;
		}
		/* Refresh the cache */
		data->pwm_config[ix] = dme1737_read(client,
						DME1737_REG_PWM_CONFIG(ix));
		if (PWM_EN_FROM_REG(data->pwm_config[ix]) == 2) {
			/* PWM is already in auto mode so update the temp
			 * channel assignment */
			data->pwm_config[ix] = PWM_ACZ_TO_REG(val,
						data->pwm_config[ix]);
			dme1737_write(client, DME1737_REG_PWM_CONFIG(ix),
				      data->pwm_config[ix]);
		} else {
			/* PWM is not in auto mode so we save the temp
			 * channel assignment for later use */
			data->pwm_acz[ix] = val;
		}
		break;
	case SYS_PWM_AUTO_PWM_MIN:
		/* Only valid for pwm[1-3] */
		/* Refresh the cache */
		data->pwm_min[ix] = dme1737_read(client,
						DME1737_REG_PWM_MIN(ix));
		/* There are only 2 values supported for the auto_pwm_min
		 * value: 0 or auto_point1_pwm. So if the temperature drops
		 * below the auto_point1_temp_hyst value, the fan either turns
		 * off or runs at auto_point1_pwm duty-cycle. */
		if (val > ((data->pwm_min[ix] + 1) / 2)) {
			data->pwm_rr[0] = PWM_OFF_TO_REG(1, ix,
						dme1737_read(client,
						DME1737_REG_PWM_RR(0)));

		} else {
			data->pwm_rr[0] = PWM_OFF_TO_REG(0, ix,
						dme1737_read(client,
						DME1737_REG_PWM_RR(0)));

		}
		dme1737_write(client, DME1737_REG_PWM_RR(0),
			      data->pwm_rr[0]);
		break;
	case SYS_PWM_AUTO_POINT1_PWM:
		/* Only valid for pwm[1-3] */
		data->pwm_min[ix] = SENSORS_LIMIT(val, 0, 255);
		dme1737_write(client, DME1737_REG_PWM_MIN(ix),
			      data->pwm_min[ix]);
		break;
	default:
		dev_dbg(dev, "Unknown attr fetch (%d)\n", fn);
	}
exit:
	mutex_unlock(&data->update_lock);

	return count;
}

/* ---------------------------------------------------------------------
 * Miscellaneous sysfs attributes
 * --------------------------------------------------------------------- */

static ssize_t show_vrm(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dme1737_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", data->vrm);
}

static ssize_t set_vrm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dme1737_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	data->vrm = val;
	return count;
}

static ssize_t show_vid(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct dme1737_data *data = dme1737_update_device(dev);

	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}

/* ---------------------------------------------------------------------
 * Sysfs device attribute defines and structs
 * --------------------------------------------------------------------- */

/* Voltages 0-6 */

#define SENSOR_DEVICE_ATTR_IN(ix) \
static SENSOR_DEVICE_ATTR_2(in##ix##_input, S_IRUGO, \
        show_in, NULL, SYS_IN_INPUT, ix); \
static SENSOR_DEVICE_ATTR_2(in##ix##_min, S_IRUGO | S_IWUSR, \
        show_in, set_in, SYS_IN_MIN, ix); \
static SENSOR_DEVICE_ATTR_2(in##ix##_max, S_IRUGO | S_IWUSR, \
        show_in, set_in, SYS_IN_MAX, ix); \
static SENSOR_DEVICE_ATTR_2(in##ix##_alarm, S_IRUGO, \
        show_in, NULL, SYS_IN_ALARM, ix)

SENSOR_DEVICE_ATTR_IN(0);
SENSOR_DEVICE_ATTR_IN(1);
SENSOR_DEVICE_ATTR_IN(2);
SENSOR_DEVICE_ATTR_IN(3);
SENSOR_DEVICE_ATTR_IN(4);
SENSOR_DEVICE_ATTR_IN(5);
SENSOR_DEVICE_ATTR_IN(6);

/* Temperatures 1-3 */

#define SENSOR_DEVICE_ATTR_TEMP(ix) \
static SENSOR_DEVICE_ATTR_2(temp##ix##_input, S_IRUGO, \
        show_temp, NULL, SYS_TEMP_INPUT, ix-1); \
static SENSOR_DEVICE_ATTR_2(temp##ix##_min, S_IRUGO | S_IWUSR, \
        show_temp, set_temp, SYS_TEMP_MIN, ix-1); \
static SENSOR_DEVICE_ATTR_2(temp##ix##_max, S_IRUGO | S_IWUSR, \
        show_temp, set_temp, SYS_TEMP_MAX, ix-1); \
static SENSOR_DEVICE_ATTR_2(temp##ix##_offset, S_IRUGO, \
        show_temp, set_temp, SYS_TEMP_OFFSET, ix-1); \
static SENSOR_DEVICE_ATTR_2(temp##ix##_alarm, S_IRUGO, \
        show_temp, NULL, SYS_TEMP_ALARM, ix-1); \
static SENSOR_DEVICE_ATTR_2(temp##ix##_fault, S_IRUGO, \
        show_temp, NULL, SYS_TEMP_FAULT, ix-1)

SENSOR_DEVICE_ATTR_TEMP(1);
SENSOR_DEVICE_ATTR_TEMP(2);
SENSOR_DEVICE_ATTR_TEMP(3);

/* Zones 1-3 */

#define SENSOR_DEVICE_ATTR_ZONE(ix) \
static SENSOR_DEVICE_ATTR_2(zone##ix##_auto_channels_temp, S_IRUGO, \
        show_zone, NULL, SYS_ZONE_AUTO_CHANNELS_TEMP, ix-1); \
static SENSOR_DEVICE_ATTR_2(zone##ix##_auto_point1_temp_hyst, S_IRUGO, \
        show_zone, set_zone, SYS_ZONE_AUTO_POINT1_TEMP_HYST, ix-1); \
static SENSOR_DEVICE_ATTR_2(zone##ix##_auto_point1_temp, S_IRUGO, \
        show_zone, set_zone, SYS_ZONE_AUTO_POINT1_TEMP, ix-1); \
static SENSOR_DEVICE_ATTR_2(zone##ix##_auto_point2_temp, S_IRUGO, \
        show_zone, set_zone, SYS_ZONE_AUTO_POINT2_TEMP, ix-1); \
static SENSOR_DEVICE_ATTR_2(zone##ix##_auto_point3_temp, S_IRUGO, \
        show_zone, set_zone, SYS_ZONE_AUTO_POINT3_TEMP, ix-1)

SENSOR_DEVICE_ATTR_ZONE(1);
SENSOR_DEVICE_ATTR_ZONE(2);
SENSOR_DEVICE_ATTR_ZONE(3);

/* Fans 1-4 */

#define SENSOR_DEVICE_ATTR_FAN_1TO4(ix) \
static SENSOR_DEVICE_ATTR_2(fan##ix##_input, S_IRUGO, \
        show_fan, NULL, SYS_FAN_INPUT, ix-1); \
static SENSOR_DEVICE_ATTR_2(fan##ix##_min, S_IRUGO | S_IWUSR, \
        show_fan, set_fan, SYS_FAN_MIN, ix-1); \
static SENSOR_DEVICE_ATTR_2(fan##ix##_alarm, S_IRUGO, \
        show_fan, NULL, SYS_FAN_ALARM, ix-1); \
static SENSOR_DEVICE_ATTR_2(fan##ix##_type, S_IRUGO | S_IWUSR, \
        show_fan, set_fan, SYS_FAN_TYPE, ix-1)

SENSOR_DEVICE_ATTR_FAN_1TO4(1);
SENSOR_DEVICE_ATTR_FAN_1TO4(2);
SENSOR_DEVICE_ATTR_FAN_1TO4(3);
SENSOR_DEVICE_ATTR_FAN_1TO4(4);

/* Fans 5-6 */

#define SENSOR_DEVICE_ATTR_FAN_5TO6(ix) \
static SENSOR_DEVICE_ATTR_2(fan##ix##_input, S_IRUGO, \
        show_fan, NULL, SYS_FAN_INPUT, ix-1); \
static SENSOR_DEVICE_ATTR_2(fan##ix##_min, S_IRUGO | S_IWUSR, \
        show_fan, set_fan, SYS_FAN_MIN, ix-1); \
static SENSOR_DEVICE_ATTR_2(fan##ix##_alarm, S_IRUGO, \
        show_fan, NULL, SYS_FAN_ALARM, ix-1); \
static SENSOR_DEVICE_ATTR_2(fan##ix##_max, S_IRUGO | S_IWUSR, \
        show_fan, set_fan, SYS_FAN_MAX, ix-1)

SENSOR_DEVICE_ATTR_FAN_5TO6(5);
SENSOR_DEVICE_ATTR_FAN_5TO6(6);

/* PWMs 1-3 */

#define SENSOR_DEVICE_ATTR_PWM_1TO3(ix) \
static SENSOR_DEVICE_ATTR_2(pwm##ix, S_IRUGO, \
        show_pwm, set_pwm, SYS_PWM, ix-1); \
static SENSOR_DEVICE_ATTR_2(pwm##ix##_freq, S_IRUGO, \
        show_pwm, set_pwm, SYS_PWM_FREQ, ix-1); \
static SENSOR_DEVICE_ATTR_2(pwm##ix##_enable, S_IRUGO, \
        show_pwm, set_pwm, SYS_PWM_ENABLE, ix-1); \
static SENSOR_DEVICE_ATTR_2(pwm##ix##_ramp_rate, S_IRUGO, \
        show_pwm, set_pwm, SYS_PWM_RAMP_RATE, ix-1); \
static SENSOR_DEVICE_ATTR_2(pwm##ix##_auto_channels_zone, S_IRUGO, \
        show_pwm, set_pwm, SYS_PWM_AUTO_CHANNELS_ZONE, ix-1); \
static SENSOR_DEVICE_ATTR_2(pwm##ix##_auto_pwm_min, S_IRUGO, \
        show_pwm, set_pwm, SYS_PWM_AUTO_PWM_MIN, ix-1); \
static SENSOR_DEVICE_ATTR_2(pwm##ix##_auto_point1_pwm, S_IRUGO, \
        show_pwm, set_pwm, SYS_PWM_AUTO_POINT1_PWM, ix-1); \
static SENSOR_DEVICE_ATTR_2(pwm##ix##_auto_point2_pwm, S_IRUGO, \
        show_pwm, NULL, SYS_PWM_AUTO_POINT2_PWM, ix-1)

SENSOR_DEVICE_ATTR_PWM_1TO3(1);
SENSOR_DEVICE_ATTR_PWM_1TO3(2);
SENSOR_DEVICE_ATTR_PWM_1TO3(3);

/* PWMs 5-6 */

#define SENSOR_DEVICE_ATTR_PWM_5TO6(ix) \
static SENSOR_DEVICE_ATTR_2(pwm##ix, S_IRUGO | S_IWUSR, \
        show_pwm, set_pwm, SYS_PWM, ix-1); \
static SENSOR_DEVICE_ATTR_2(pwm##ix##_freq, S_IRUGO | S_IWUSR, \
        show_pwm, set_pwm, SYS_PWM_FREQ, ix-1); \
static SENSOR_DEVICE_ATTR_2(pwm##ix##_enable, S_IRUGO, \
        show_pwm, NULL, SYS_PWM_ENABLE, ix-1)

SENSOR_DEVICE_ATTR_PWM_5TO6(5);
SENSOR_DEVICE_ATTR_PWM_5TO6(6);

/* Misc */

static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm, set_vrm);
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

#define SENSOR_DEV_ATTR_IN(ix) \
&sensor_dev_attr_in##ix##_input.dev_attr.attr, \
&sensor_dev_attr_in##ix##_min.dev_attr.attr, \
&sensor_dev_attr_in##ix##_max.dev_attr.attr, \
&sensor_dev_attr_in##ix##_alarm.dev_attr.attr

/* These attributes are read-writeable only if the chip is *not* locked */
#define SENSOR_DEV_ATTR_TEMP_LOCK(ix) \
&sensor_dev_attr_temp##ix##_offset.dev_attr.attr

#define SENSOR_DEV_ATTR_TEMP(ix) \
SENSOR_DEV_ATTR_TEMP_LOCK(ix), \
&sensor_dev_attr_temp##ix##_input.dev_attr.attr, \
&sensor_dev_attr_temp##ix##_min.dev_attr.attr, \
&sensor_dev_attr_temp##ix##_max.dev_attr.attr, \
&sensor_dev_attr_temp##ix##_alarm.dev_attr.attr, \
&sensor_dev_attr_temp##ix##_fault.dev_attr.attr

/* These attributes are read-writeable only if the chip is *not* locked */
#define SENSOR_DEV_ATTR_ZONE_LOCK(ix) \
&sensor_dev_attr_zone##ix##_auto_point1_temp_hyst.dev_attr.attr, \
&sensor_dev_attr_zone##ix##_auto_point1_temp.dev_attr.attr, \
&sensor_dev_attr_zone##ix##_auto_point2_temp.dev_attr.attr, \
&sensor_dev_attr_zone##ix##_auto_point3_temp.dev_attr.attr

#define SENSOR_DEV_ATTR_ZONE(ix) \
SENSOR_DEV_ATTR_ZONE_LOCK(ix), \
&sensor_dev_attr_zone##ix##_auto_channels_temp.dev_attr.attr

#define SENSOR_DEV_ATTR_FAN_1TO4(ix) \
&sensor_dev_attr_fan##ix##_input.dev_attr.attr, \
&sensor_dev_attr_fan##ix##_min.dev_attr.attr, \
&sensor_dev_attr_fan##ix##_alarm.dev_attr.attr, \
&sensor_dev_attr_fan##ix##_type.dev_attr.attr

#define SENSOR_DEV_ATTR_FAN_5TO6(ix) \
&sensor_dev_attr_fan##ix##_input.dev_attr.attr, \
&sensor_dev_attr_fan##ix##_min.dev_attr.attr, \
&sensor_dev_attr_fan##ix##_alarm.dev_attr.attr, \
&sensor_dev_attr_fan##ix##_max.dev_attr.attr

/* These attributes are read-writeable only if the chip is *not* locked */
#define SENSOR_DEV_ATTR_PWM_1TO3_LOCK(ix) \
&sensor_dev_attr_pwm##ix##_freq.dev_attr.attr, \
&sensor_dev_attr_pwm##ix##_enable.dev_attr.attr, \
&sensor_dev_attr_pwm##ix##_ramp_rate.dev_attr.attr, \
&sensor_dev_attr_pwm##ix##_auto_channels_zone.dev_attr.attr, \
&sensor_dev_attr_pwm##ix##_auto_pwm_min.dev_attr.attr, \
&sensor_dev_attr_pwm##ix##_auto_point1_pwm.dev_attr.attr

#define SENSOR_DEV_ATTR_PWM_1TO3(ix) \
SENSOR_DEV_ATTR_PWM_1TO3_LOCK(ix), \
&sensor_dev_attr_pwm##ix.dev_attr.attr, \
&sensor_dev_attr_pwm##ix##_auto_point2_pwm.dev_attr.attr

/* These attributes are read-writeable only if the chip is *not* locked */
#define SENSOR_DEV_ATTR_PWM_5TO6_LOCK(ix) \
&sensor_dev_attr_pwm##ix.dev_attr.attr, \
&sensor_dev_attr_pwm##ix##_freq.dev_attr.attr

#define SENSOR_DEV_ATTR_PWM_5TO6(ix) \
SENSOR_DEV_ATTR_PWM_5TO6_LOCK(ix), \
&sensor_dev_attr_pwm##ix##_enable.dev_attr.attr

/* This struct holds all the attributes that are always present and need to be
 * created unconditionally. The attributes that need modification of their
 * permissions are created read-only and write permissions are added or removed
 * on the fly when required */
static struct attribute *dme1737_attr[] ={
        /* Voltages */
        SENSOR_DEV_ATTR_IN(0),
        SENSOR_DEV_ATTR_IN(1),
        SENSOR_DEV_ATTR_IN(2),
        SENSOR_DEV_ATTR_IN(3),
        SENSOR_DEV_ATTR_IN(4),
        SENSOR_DEV_ATTR_IN(5),
        SENSOR_DEV_ATTR_IN(6),
        /* Temperatures */
        SENSOR_DEV_ATTR_TEMP(1),
        SENSOR_DEV_ATTR_TEMP(2),
        SENSOR_DEV_ATTR_TEMP(3),
        /* Zones */
        SENSOR_DEV_ATTR_ZONE(1),
        SENSOR_DEV_ATTR_ZONE(2),
        SENSOR_DEV_ATTR_ZONE(3),
        /* Misc */
        &dev_attr_vrm.attr,
        &dev_attr_cpu0_vid.attr,
	NULL
};

static const struct attribute_group dme1737_group = {
        .attrs = dme1737_attr,
};

/* The following structs hold the PWM attributes, some of which are optional.
 * Their creation depends on the chip configuration which is determined during
 * module load. */
static struct attribute *dme1737_attr_pwm1[] = {
        SENSOR_DEV_ATTR_PWM_1TO3(1),
	NULL
};
static struct attribute *dme1737_attr_pwm2[] = {
        SENSOR_DEV_ATTR_PWM_1TO3(2),
	NULL
};
static struct attribute *dme1737_attr_pwm3[] = {
        SENSOR_DEV_ATTR_PWM_1TO3(3),
	NULL
};
static struct attribute *dme1737_attr_pwm5[] = {
        SENSOR_DEV_ATTR_PWM_5TO6(5),
	NULL
};
static struct attribute *dme1737_attr_pwm6[] = {
        SENSOR_DEV_ATTR_PWM_5TO6(6),
	NULL
};

static const struct attribute_group dme1737_pwm_group[] = {
	{ .attrs = dme1737_attr_pwm1 },
	{ .attrs = dme1737_attr_pwm2 },
	{ .attrs = dme1737_attr_pwm3 },
	{ .attrs = NULL },
	{ .attrs = dme1737_attr_pwm5 },
	{ .attrs = dme1737_attr_pwm6 },
};

/* The following structs hold the fan attributes, some of which are optional.
 * Their creation depends on the chip configuration which is determined during
 * module load. */
static struct attribute *dme1737_attr_fan1[] = {
        SENSOR_DEV_ATTR_FAN_1TO4(1),
	NULL
};
static struct attribute *dme1737_attr_fan2[] = {
        SENSOR_DEV_ATTR_FAN_1TO4(2),
	NULL
};
static struct attribute *dme1737_attr_fan3[] = {
        SENSOR_DEV_ATTR_FAN_1TO4(3),
	NULL
};
static struct attribute *dme1737_attr_fan4[] = {
        SENSOR_DEV_ATTR_FAN_1TO4(4),
	NULL
};
static struct attribute *dme1737_attr_fan5[] = {
        SENSOR_DEV_ATTR_FAN_5TO6(5),
	NULL
};
static struct attribute *dme1737_attr_fan6[] = {
        SENSOR_DEV_ATTR_FAN_5TO6(6),
	NULL
};

static const struct attribute_group dme1737_fan_group[] = {
	{ .attrs = dme1737_attr_fan1 },
	{ .attrs = dme1737_attr_fan2 },
	{ .attrs = dme1737_attr_fan3 },
	{ .attrs = dme1737_attr_fan4 },
	{ .attrs = dme1737_attr_fan5 },
	{ .attrs = dme1737_attr_fan6 },
};

/* The permissions of all of the following attributes are changed to read-
 * writeable if the chip is *not* locked. Otherwise they stay read-only. */
static struct attribute *dme1737_attr_lock[] = {
	/* Temperatures */
	SENSOR_DEV_ATTR_TEMP_LOCK(1),
	SENSOR_DEV_ATTR_TEMP_LOCK(2),
	SENSOR_DEV_ATTR_TEMP_LOCK(3),
	/* Zones */
	SENSOR_DEV_ATTR_ZONE_LOCK(1),
	SENSOR_DEV_ATTR_ZONE_LOCK(2),
	SENSOR_DEV_ATTR_ZONE_LOCK(3),
	NULL
};

static const struct attribute_group dme1737_lock_group = {
	.attrs = dme1737_attr_lock,
};

/* The permissions of the following PWM attributes are changed to read-
 * writeable if the chip is *not* locked and the respective PWM is available.
 * Otherwise they stay read-only. */
static struct attribute *dme1737_attr_pwm1_lock[] = {
        SENSOR_DEV_ATTR_PWM_1TO3_LOCK(1),
	NULL
};
static struct attribute *dme1737_attr_pwm2_lock[] = {
        SENSOR_DEV_ATTR_PWM_1TO3_LOCK(2),
	NULL
};
static struct attribute *dme1737_attr_pwm3_lock[] = {
        SENSOR_DEV_ATTR_PWM_1TO3_LOCK(3),
	NULL
};
static struct attribute *dme1737_attr_pwm5_lock[] = {
        SENSOR_DEV_ATTR_PWM_5TO6_LOCK(5),
	NULL
};
static struct attribute *dme1737_attr_pwm6_lock[] = {
        SENSOR_DEV_ATTR_PWM_5TO6_LOCK(6),
	NULL
};

static const struct attribute_group dme1737_pwm_lock_group[] = {
	{ .attrs = dme1737_attr_pwm1_lock },
	{ .attrs = dme1737_attr_pwm2_lock },
	{ .attrs = dme1737_attr_pwm3_lock },
	{ .attrs = NULL },
	{ .attrs = dme1737_attr_pwm5_lock },
	{ .attrs = dme1737_attr_pwm6_lock },
};

/* Pwm[1-3] are read-writeable if the associated pwm is in manual mode and the
 * chip is not locked. Otherwise they are read-only. */
static struct attribute *dme1737_attr_pwm[] = {
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
};

/* ---------------------------------------------------------------------
 * Super-IO functions
 * --------------------------------------------------------------------- */

static inline int dme1737_sio_inb(int sio_cip, int reg)
{
	outb(reg, sio_cip);
	return inb(sio_cip + 1);
}

static inline void dme1737_sio_outb(int sio_cip, int reg, int val)
{
	outb(reg, sio_cip);
	outb(val, sio_cip + 1);
}

static int dme1737_sio_get_features(int sio_cip, struct i2c_client *client)
{
	struct dme1737_data *data = i2c_get_clientdata(client);
	int err = 0, reg;
	u16 addr;

	/* Enter configuration mode */
	outb(0x55, sio_cip);

	/* Check device ID
	 * The DME1737 can return either 0x78 or 0x77 as its device ID. */
	reg = dme1737_sio_inb(sio_cip, 0x20);
	if (!(reg == 0x77 || reg == 0x78)) {
		err = -ENODEV;
		goto exit;
	}

	/* Select logical device A (runtime registers) */
	dme1737_sio_outb(sio_cip, 0x07, 0x0a);

	/* Get the base address of the runtime registers */
	if (!(addr = (dme1737_sio_inb(sio_cip, 0x60) << 8) |
		      dme1737_sio_inb(sio_cip, 0x61))) {
		err = -ENODEV;
		goto exit;
	}

	/* Read the runtime registers to determine which optional features
	 * are enabled and available. Bits [3:2] of registers 0x43-0x46 are set
	 * to '10' if the respective feature is enabled. */
	if ((inb(addr + 0x43) & 0x0c) == 0x08) { /* fan6 */
		data->has_fan |= (1 << 5);
	}
	if ((inb(addr + 0x44) & 0x0c) == 0x08) { /* pwm6 */
		data->has_pwm |= (1 << 5);
	}
	if ((inb(addr + 0x45) & 0x0c) == 0x08) { /* fan5 */
		data->has_fan |= (1 << 4);
	}
	if ((inb(addr + 0x46) & 0x0c) == 0x08) { /* pwm5 */
		data->has_pwm |= (1 << 4);
	}

exit:
	/* Exit configuration mode */
	outb(0xaa, sio_cip);

	return err;
}

/* ---------------------------------------------------------------------
 * Device detection, registration and initialization
 * --------------------------------------------------------------------- */

static struct i2c_driver dme1737_driver;

static void dme1737_chmod_file(struct i2c_client *client,
			       struct attribute *attr, mode_t mode)
{
	if (sysfs_chmod_file(&client->dev.kobj, attr, mode)) {
		dev_warn(&client->dev, "Failed to change permissions of %s.\n",
			 attr->name);
	}
}

static void dme1737_chmod_group(struct i2c_client *client,
				const struct attribute_group *group,
				mode_t mode)
{
	struct attribute **attr;

	for (attr = group->attrs; *attr; attr++) {
		dme1737_chmod_file(client, *attr, mode);
	}
}

static int dme1737_init_client(struct i2c_client *client)
{
	struct dme1737_data *data = i2c_get_clientdata(client);
	int ix;
	u8 reg;

        data->config = dme1737_read(client, DME1737_REG_CONFIG);
        /* Inform if part is not monitoring/started */
        if (!(data->config & 0x01)) {
                if (!force_start) {
                        dev_err(&client->dev, "Device is not monitoring. "
                                "Use the force_start load parameter to "
                                "override.\n");
                        return -EFAULT;
                }

                /* Force monitoring */
                data->config |= 0x01;
                dme1737_write(client, DME1737_REG_CONFIG, data->config);
        }
	/* Inform if part is not ready */
	if (!(data->config & 0x04)) {
		dev_err(&client->dev, "Device is not ready.\n");
		return -EFAULT;
	}

	data->config2 = dme1737_read(client, DME1737_REG_CONFIG2);
	/* Check if optional fan3 input is enabled */
	if (data->config2 & 0x04) {
		data->has_fan |= (1 << 2);
	}

	/* Fan4 and pwm3 are only available if the client's I2C address
	 * is the default 0x2e. Otherwise the I/Os associated with these
	 * functions are used for addr enable/select. */
	if (client->addr == 0x2e) {
		data->has_fan |= (1 << 3);
		data->has_pwm |= (1 << 2);
	}

	/* Determine if the optional fan[5-6] and/or pwm[5-6] are enabled.
	 * For this, we need to query the runtime registers through the
	 * Super-IO LPC interface. Try both config ports 0x2e and 0x4e. */
	if (dme1737_sio_get_features(0x2e, client) &&
	    dme1737_sio_get_features(0x4e, client)) {
		dev_warn(&client->dev, "Failed to query Super-IO for optional "
			 "features.\n");
	}

	/* Fan1, fan2, pwm1, and pwm2 are always present */
	data->has_fan |= 0x03;
	data->has_pwm |= 0x03;

	dev_info(&client->dev, "Optional features: pwm3=%s, pwm5=%s, pwm6=%s, "
		 "fan3=%s, fan4=%s, fan5=%s, fan6=%s.\n",
		 (data->has_pwm & (1 << 2)) ? "yes" : "no",
		 (data->has_pwm & (1 << 4)) ? "yes" : "no",
		 (data->has_pwm & (1 << 5)) ? "yes" : "no",
		 (data->has_fan & (1 << 2)) ? "yes" : "no",
		 (data->has_fan & (1 << 3)) ? "yes" : "no",
		 (data->has_fan & (1 << 4)) ? "yes" : "no",
		 (data->has_fan & (1 << 5)) ? "yes" : "no");

	reg = dme1737_read(client, DME1737_REG_TACH_PWM);
	/* Inform if fan-to-pwm mapping differs from the default */
	if (reg != 0xa4) {
		dev_warn(&client->dev, "Non-standard fan to pwm mapping: "
			 "fan1->pwm%d, fan2->pwm%d, fan3->pwm%d, "
			 "fan4->pwm%d. Please report to the driver "
			 "maintainer.\n",
			 (reg & 0x03) + 1, ((reg >> 2) & 0x03) + 1,
			 ((reg >> 4) & 0x03) + 1, ((reg >> 6) & 0x03) + 1);
	}

	/* Switch pwm[1-3] to manual mode if they are currently disabled and
	 * set the duty-cycles to 0% (which is identical to the PWMs being
	 * disabled). */
	if (!(data->config & 0x02)) {
		for (ix = 0; ix < 3; ix++) {
			data->pwm_config[ix] = dme1737_read(client,
						DME1737_REG_PWM_CONFIG(ix));
			if ((data->has_pwm & (1 << ix)) &&
			    (PWM_EN_FROM_REG(data->pwm_config[ix]) == -1)) {
				dev_info(&client->dev, "Switching pwm%d to "
					 "manual mode.\n", ix + 1);
				data->pwm_config[ix] = PWM_EN_TO_REG(1,
							data->pwm_config[ix]);
				dme1737_write(client, DME1737_REG_PWM(ix), 0);
				dme1737_write(client,
					      DME1737_REG_PWM_CONFIG(ix),
					      data->pwm_config[ix]);
			}
		}
	}

	/* Initialize the default PWM auto channels zone (acz) assignments */
	data->pwm_acz[0] = 1;	/* pwm1 -> zone1 */
	data->pwm_acz[1] = 2;	/* pwm2 -> zone2 */
	data->pwm_acz[2] = 4;	/* pwm3 -> zone3 */

	/* Set VRM */
	data->vrm = vid_which_vrm();

	return 0;
}

static int dme1737_detect(struct i2c_adapter *adapter, int address,
			  int kind)
{
	u8 company, verstep = 0;
	struct i2c_client *client;
	struct dme1737_data *data;
	int ix, err = 0;
	const char *name;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		goto exit;
	}

	if (!(data = kzalloc(sizeof(struct dme1737_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	client = &data->client;
	i2c_set_clientdata(client, data);
	client->addr = address;
	client->adapter = adapter;
	client->driver = &dme1737_driver;

	/* A negative kind means that the driver was loaded with no force
	 * parameter (default), so we must identify the chip. */
	if (kind < 0) {
		company = dme1737_read(client, DME1737_REG_COMPANY);
		verstep = dme1737_read(client, DME1737_REG_VERSTEP);

		if (!((company == DME1737_COMPANY_SMSC) &&
		      ((verstep & DME1737_VERSTEP_MASK) == DME1737_VERSTEP))) {
			err = -ENODEV;
			goto exit_kfree;
		}
	}

	kind = dme1737;
	name = "dme1737";

	/* Fill in the remaining client fields and put it into the global
	 * list */
	strlcpy(client->name, name, I2C_NAME_SIZE);
	mutex_init(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(client))) {
		goto exit_kfree;
	}

	/* Initialize the DME1737 chip */
	if ((err = dme1737_init_client(client))) {
		goto exit_detach;
	}

	/* Create standard sysfs attributes */
	if ((err = sysfs_create_group(&client->dev.kobj, &dme1737_group))) {
                goto exit_detach;
	}

	/* Create fan sysfs attributes */
	for (ix = 0; ix < ARRAY_SIZE(dme1737_fan_group); ix++) {
		if (data->has_fan & (1 << ix)) {
			if ((err = sysfs_create_group(&client->dev.kobj,
						&dme1737_fan_group[ix]))) {
				goto exit_remove;
			}
		}
	}

	/* Create PWM sysfs attributes */
	for (ix = 0; ix < ARRAY_SIZE(dme1737_pwm_group); ix++) {
		if (data->has_pwm & (1 << ix)) {
			if ((err = sysfs_create_group(&client->dev.kobj,
						&dme1737_pwm_group[ix]))) {
				goto exit_remove;
			}
		}
	}

	/* Inform if the device is locked. Otherwise change the permissions of
	 * selected attributes from read-only to read-writeable. */
	if (data->config & 0x02) {
		dev_info(&client->dev, "Device is locked. Some attributes "
			 "will be read-only.\n");
	} else {
		/* Change permissions of standard attributes */
		dme1737_chmod_group(client, &dme1737_lock_group,
				    S_IRUGO | S_IWUSR);

		/* Change permissions of PWM attributes */
		for (ix = 0; ix < ARRAY_SIZE(dme1737_pwm_lock_group); ix++) {
			if (data->has_pwm & (1 << ix)) {
				dme1737_chmod_group(client,
						&dme1737_pwm_lock_group[ix],
						S_IRUGO | S_IWUSR);
			}
		}

		/* Change permissions of pwm[1-3] if in manual mode */
		for (ix = 0; ix < 3; ix++) {
			if ((data->has_pwm & (1 << ix)) &&
			    (PWM_EN_FROM_REG(data->pwm_config[ix]) == 1)) {
				dme1737_chmod_file(client,
						   dme1737_attr_pwm[ix],
						   S_IRUGO | S_IWUSR);
			}
		}
	}

	/* Register device */
	data->class_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_remove;
	}

	dev_info(&adapter->dev, "Found a DME1737 chip at 0x%02x "
		 "(rev 0x%02x)\n", client->addr, verstep);

	return 0;

exit_remove:
	for (ix = 0; ix < ARRAY_SIZE(dme1737_fan_group); ix++) {
		if (data->has_fan & (1 << ix)) {
			sysfs_remove_group(&client->dev.kobj,
					   &dme1737_fan_group[ix]);
		}
	}
	for (ix = 0; ix < ARRAY_SIZE(dme1737_pwm_group); ix++) {
		if (data->has_pwm & (1 << ix)) {
			sysfs_remove_group(&client->dev.kobj,
					   &dme1737_pwm_group[ix]);
		}
	}
	sysfs_remove_group(&client->dev.kobj, &dme1737_group);
exit_detach:
	i2c_detach_client(client);
exit_kfree:
	kfree(data);
exit:
	return err;
}

static int dme1737_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON)) {
		return 0;
	}

	return i2c_probe(adapter, &addr_data, dme1737_detect);
}

static int dme1737_detach_client(struct i2c_client *client)
{
	struct dme1737_data *data = i2c_get_clientdata(client);
	int ix, err;

	hwmon_device_unregister(data->class_dev);

	for (ix = 0; ix < ARRAY_SIZE(dme1737_fan_group); ix++) {
		if (data->has_fan & (1 << ix)) {
			sysfs_remove_group(&client->dev.kobj,
					   &dme1737_fan_group[ix]);
		}
	}
	for (ix = 0; ix < ARRAY_SIZE(dme1737_pwm_group); ix++) {
		if (data->has_pwm & (1 << ix)) {
			sysfs_remove_group(&client->dev.kobj,
					   &dme1737_pwm_group[ix]);
		}
	}
	sysfs_remove_group(&client->dev.kobj, &dme1737_group);

	if ((err = i2c_detach_client(client))) {
		return err;
	}

	kfree(data);
	return 0;
}

static struct i2c_driver dme1737_driver = {
	.driver = {
		.name = "dme1737",
	},
	.attach_adapter	= dme1737_attach_adapter,
	.detach_client = dme1737_detach_client,
};

static int __init dme1737_init(void)
{
	return i2c_add_driver(&dme1737_driver);
}

static void __exit dme1737_exit(void)
{
	i2c_del_driver(&dme1737_driver);
}

MODULE_AUTHOR("Juerg Haefliger <juergh@gmail.com>");
MODULE_DESCRIPTION("DME1737 sensors");
MODULE_LICENSE("GPL");

module_init(dme1737_init);
module_exit(dme1737_exit);
