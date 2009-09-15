/*
 * drivers/media/radio/si4713-i2c.h
 *
 * Property and commands definitions for Si4713 radio transmitter chip.
 *
 * Copyright (c) 2008 Instituto Nokia de Tecnologia - INdT
 * Contact: Eduardo Valentin <eduardo.valentin@nokia.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#ifndef SI4713_I2C_H
#define SI4713_I2C_H

#include <media/v4l2-subdev.h>
#include <media/si4713.h>

#define SI4713_PRODUCT_NUMBER		0x0D

/* Command Timeouts */
#define DEFAULT_TIMEOUT			500
#define TIMEOUT_SET_PROPERTY		20
#define TIMEOUT_TX_TUNE_POWER		30000
#define TIMEOUT_TX_TUNE			110000
#define TIMEOUT_POWER_UP		200000

/*
 * Command and its arguments definitions
 */
#define SI4713_PWUP_CTSIEN		(1<<7)
#define SI4713_PWUP_GPO2OEN		(1<<6)
#define SI4713_PWUP_PATCH		(1<<5)
#define SI4713_PWUP_XOSCEN		(1<<4)
#define SI4713_PWUP_FUNC_TX		0x02
#define SI4713_PWUP_FUNC_PATCH		0x0F
#define SI4713_PWUP_OPMOD_ANALOG	0x50
#define SI4713_PWUP_OPMOD_DIGITAL	0x0F
#define SI4713_PWUP_NARGS		2
#define SI4713_PWUP_NRESP		1
#define SI4713_CMD_POWER_UP		0x01

#define SI4713_GETREV_NRESP		9
#define SI4713_CMD_GET_REV		0x10

#define SI4713_PWDN_NRESP		1
#define SI4713_CMD_POWER_DOWN		0x11

#define SI4713_SET_PROP_NARGS		5
#define SI4713_SET_PROP_NRESP		1
#define SI4713_CMD_SET_PROPERTY		0x12

#define SI4713_GET_PROP_NARGS		3
#define SI4713_GET_PROP_NRESP		4
#define SI4713_CMD_GET_PROPERTY		0x13

#define SI4713_GET_STATUS_NRESP		1
#define SI4713_CMD_GET_INT_STATUS	0x14

#define SI4713_CMD_PATCH_ARGS		0x15
#define SI4713_CMD_PATCH_DATA		0x16

#define SI4713_MAX_FREQ			10800
#define SI4713_MIN_FREQ			7600
#define SI4713_TXFREQ_NARGS		3
#define SI4713_TXFREQ_NRESP		1
#define SI4713_CMD_TX_TUNE_FREQ		0x30

#define SI4713_MAX_POWER		120
#define SI4713_MIN_POWER		88
#define SI4713_MAX_ANTCAP		191
#define SI4713_MIN_ANTCAP		0
#define SI4713_TXPWR_NARGS		4
#define SI4713_TXPWR_NRESP		1
#define SI4713_CMD_TX_TUNE_POWER	0x31

#define SI4713_TXMEA_NARGS		4
#define SI4713_TXMEA_NRESP		1
#define SI4713_CMD_TX_TUNE_MEASURE	0x32

#define SI4713_INTACK_MASK		0x01
#define SI4713_TXSTATUS_NARGS		1
#define SI4713_TXSTATUS_NRESP		8
#define SI4713_CMD_TX_TUNE_STATUS	0x33

#define SI4713_OVERMOD_BIT		(1 << 2)
#define SI4713_IALH_BIT			(1 << 1)
#define SI4713_IALL_BIT			(1 << 0)
#define SI4713_ASQSTATUS_NARGS		1
#define SI4713_ASQSTATUS_NRESP		5
#define SI4713_CMD_TX_ASQ_STATUS	0x34

#define SI4713_RDSBUFF_MODE_MASK	0x87
#define SI4713_RDSBUFF_NARGS		7
#define SI4713_RDSBUFF_NRESP		6
#define SI4713_CMD_TX_RDS_BUFF		0x35

#define SI4713_RDSPS_PSID_MASK		0x1F
#define SI4713_RDSPS_NARGS		5
#define SI4713_RDSPS_NRESP		1
#define SI4713_CMD_TX_RDS_PS		0x36

#define SI4713_CMD_GPO_CTL		0x80
#define SI4713_CMD_GPO_SET		0x81

/*
 * Bits from status response
 */
#define SI4713_CTS			(1<<7)
#define SI4713_ERR			(1<<6)
#define SI4713_RDS_INT			(1<<2)
#define SI4713_ASQ_INT			(1<<1)
#define SI4713_STC_INT			(1<<0)

/*
 * Property definitions
 */
#define SI4713_GPO_IEN			0x0001
#define SI4713_DIG_INPUT_FORMAT		0x0101
#define SI4713_DIG_INPUT_SAMPLE_RATE	0x0103
#define SI4713_REFCLK_FREQ		0x0201
#define SI4713_REFCLK_PRESCALE		0x0202
#define SI4713_TX_COMPONENT_ENABLE	0x2100
#define SI4713_TX_AUDIO_DEVIATION	0x2101
#define SI4713_TX_PILOT_DEVIATION	0x2102
#define SI4713_TX_RDS_DEVIATION		0x2103
#define SI4713_TX_LINE_INPUT_LEVEL	0x2104
#define SI4713_TX_LINE_INPUT_MUTE	0x2105
#define SI4713_TX_PREEMPHASIS		0x2106
#define SI4713_TX_PILOT_FREQUENCY	0x2107
#define SI4713_TX_ACOMP_ENABLE		0x2200
#define SI4713_TX_ACOMP_THRESHOLD	0x2201
#define SI4713_TX_ACOMP_ATTACK_TIME	0x2202
#define SI4713_TX_ACOMP_RELEASE_TIME	0x2203
#define SI4713_TX_ACOMP_GAIN		0x2204
#define SI4713_TX_LIMITER_RELEASE_TIME	0x2205
#define SI4713_TX_ASQ_INTERRUPT_SOURCE	0x2300
#define SI4713_TX_ASQ_LEVEL_LOW		0x2301
#define SI4713_TX_ASQ_DURATION_LOW	0x2302
#define SI4713_TX_ASQ_LEVEL_HIGH	0x2303
#define SI4713_TX_ASQ_DURATION_HIGH	0x2304
#define SI4713_TX_RDS_INTERRUPT_SOURCE	0x2C00
#define SI4713_TX_RDS_PI		0x2C01
#define SI4713_TX_RDS_PS_MIX		0x2C02
#define SI4713_TX_RDS_PS_MISC		0x2C03
#define SI4713_TX_RDS_PS_REPEAT_COUNT	0x2C04
#define SI4713_TX_RDS_PS_MESSAGE_COUNT	0x2C05
#define SI4713_TX_RDS_PS_AF		0x2C06
#define SI4713_TX_RDS_FIFO_SIZE		0x2C07

#define PREEMPHASIS_USA			75
#define PREEMPHASIS_EU			50
#define PREEMPHASIS_DISABLED		0
#define FMPE_USA			0x00
#define FMPE_EU				0x01
#define FMPE_DISABLED			0x02

#define POWER_UP			0x01
#define POWER_DOWN			0x00

struct rds_info {
	u32 pi;
#define MAX_RDS_PTY			31
	u32 pty;
#define MAX_RDS_DEVIATION		90000
	u32 deviation;
/*
 * PSNAME is known to be defined as 8 character sized (RDS Spec).
 * However, there is receivers which scroll PSNAME 8xN sized.
 */
#define MAX_RDS_PS_NAME			96
	u8 ps_name[MAX_RDS_PS_NAME + 1];
/*
 * MAX_RDS_RADIO_TEXT is known to be defined as 32 (2A group) or 64 (2B group)
 * character sized (RDS Spec).
 * However, there is receivers which scroll them as well.
 */
#define MAX_RDS_RADIO_TEXT		384
	u8 radio_text[MAX_RDS_RADIO_TEXT + 1];
	u32 enabled;
};

struct limiter_info {
#define MAX_LIMITER_RELEASE_TIME	102390
	u32 release_time;
#define MAX_LIMITER_DEVIATION		90000
	u32 deviation;
	u32 enabled;
};

struct pilot_info {
#define MAX_PILOT_DEVIATION		90000
	u32 deviation;
#define MAX_PILOT_FREQUENCY		19000
	u32 frequency;
	u32 enabled;
};

struct acomp_info {
#define MAX_ACOMP_RELEASE_TIME		1000000
	u32 release_time;
#define MAX_ACOMP_ATTACK_TIME		5000
	u32 attack_time;
#define MAX_ACOMP_THRESHOLD		0
#define MIN_ACOMP_THRESHOLD		(-40)
	s32 threshold;
#define MAX_ACOMP_GAIN			20
	u32 gain;
	u32 enabled;
};

/*
 * si4713_device - private data
 */
struct si4713_device {
	/* v4l2_subdev and i2c reference (v4l2_subdev priv data) */
	struct v4l2_subdev sd;
	/* private data structures */
	struct mutex mutex;
	struct completion work;
	struct si4713_platform_data *platform_data;
	struct rds_info rds_info;
	struct limiter_info limiter_info;
	struct pilot_info pilot_info;
	struct acomp_info acomp_info;
	u32 frequency;
	u32 preemphasis;
	u32 mute;
	u32 power_level;
	u32 power_state;
	u32 antenna_capacitor;
	u32 stereo;
	u32 tune_rnl;
};
#endif /* ifndef SI4713_I2C_H */
