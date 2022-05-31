// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lm90.c - Part of lm_sensors, Linux kernel modules for hardware
 *          monitoring
 * Copyright (C) 2003-2010  Jean Delvare <jdelvare@suse.de>
 *
 * Based on the lm83 driver. The LM90 is a sensor chip made by National
 * Semiconductor. It reports up to two temperatures (its own plus up to
 * one external one) with a 0.125 deg resolution (1 deg for local
 * temperature) and a 3-4 deg accuracy.
 *
 * This driver also supports the LM89 and LM99, two other sensor chips
 * made by National Semiconductor. Both have an increased remote
 * temperature measurement accuracy (1 degree), and the LM99
 * additionally shifts remote temperatures (measured and limits) by 16
 * degrees, which allows for higher temperatures measurement.
 * Note that there is no way to differentiate between both chips.
 * When device is auto-detected, the driver will assume an LM99.
 *
 * This driver also supports the LM86, another sensor chip made by
 * National Semiconductor. It is exactly similar to the LM90 except it
 * has a higher accuracy.
 *
 * This driver also supports the ADM1032, a sensor chip made by Analog
 * Devices. That chip is similar to the LM90, with a few differences
 * that are not handled by this driver. Among others, it has a higher
 * accuracy than the LM90, much like the LM86 does.
 *
 * This driver also supports the MAX6657, MAX6658 and MAX6659 sensor
 * chips made by Maxim. These chips are similar to the LM86.
 * Note that there is no easy way to differentiate between the three
 * variants. We use the device address to detect MAX6659, which will result
 * in a detection as max6657 if it is on address 0x4c. The extra address
 * and features of the MAX6659 are only supported if the chip is configured
 * explicitly as max6659, or if its address is not 0x4c.
 * These chips lack the remote temperature offset feature.
 *
 * This driver also supports the MAX6654 chip made by Maxim. This chip can be
 * at 9 different addresses, similar to MAX6680/MAX6681. The MAX6654 is similar
 * to MAX6657/MAX6658/MAX6659, but does not support critical temperature
 * limits. Extended range is available by setting the configuration register
 * accordingly, and is done during initialization. Extended precision is only
 * available at conversion rates of 1 Hz and slower. Note that extended
 * precision is not enabled by default, as this driver initializes all chips
 * to 2 Hz by design.
 *
 * This driver also supports the MAX6646, MAX6647, MAX6648, MAX6649 and
 * MAX6692 chips made by Maxim.  These are again similar to the LM86,
 * but they use unsigned temperature values and can report temperatures
 * from 0 to 145 degrees.
 *
 * This driver also supports the MAX6680 and MAX6681, two other sensor
 * chips made by Maxim. These are quite similar to the other Maxim
 * chips. The MAX6680 and MAX6681 only differ in the pinout so they can
 * be treated identically.
 *
 * This driver also supports the MAX6695 and MAX6696, two other sensor
 * chips made by Maxim. These are also quite similar to other Maxim
 * chips, but support three temperature sensors instead of two. MAX6695
 * and MAX6696 only differ in the pinout so they can be treated identically.
 *
 * This driver also supports ADT7461 and ADT7461A from Analog Devices as well as
 * NCT1008 from ON Semiconductor. The chips are supported in both compatibility
 * and extended mode. They are mostly compatible with LM90 except for a data
 * format difference for the temperature value registers.
 *
 * This driver also supports the SA56004 from Philips. This device is
 * pin-compatible with the LM86, the ED/EDP parts are also address-compatible.
 *
 * This driver also supports the G781 from GMT. This device is compatible
 * with the ADM1032.
 *
 * This driver also supports TMP451 and TMP461 from Texas Instruments.
 * Those devices are supported in both compatibility and extended mode.
 * They are mostly compatible with ADT7461 except for local temperature
 * low byte register and max conversion rate.
 *
 * Since the LM90 was the first chipset supported by this driver, most
 * comments will refer to this chipset, but are actually general and
 * concern all supported chipsets, unless mentioned otherwise.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>

/*
 * Addresses to scan
 * Address is fully defined internally and cannot be changed except for
 * MAX6659, MAX6680 and MAX6681.
 * LM86, LM89, LM90, LM99, ADM1032, ADM1032-1, ADT7461, ADT7461A, MAX6649,
 * MAX6657, MAX6658, NCT1008 and W83L771 have address 0x4c.
 * ADM1032-2, ADT7461-2, ADT7461A-2, LM89-1, LM99-1, MAX6646, and NCT1008D
 * have address 0x4d.
 * MAX6647 has address 0x4e.
 * MAX6659 can have address 0x4c, 0x4d or 0x4e.
 * MAX6654, MAX6680, and MAX6681 can have address 0x18, 0x19, 0x1a, 0x29,
 * 0x2a, 0x2b, 0x4c, 0x4d or 0x4e.
 * SA56004 can have address 0x48 through 0x4F.
 */

static const unsigned short normal_i2c[] = {
	0x18, 0x19, 0x1a, 0x29, 0x2a, 0x2b, 0x48, 0x49, 0x4a, 0x4b, 0x4c,
	0x4d, 0x4e, 0x4f, I2C_CLIENT_END };

enum chips { lm90, adm1032, lm99, lm86, max6657, max6659, adt7461, max6680,
	max6646, w83l771, max6696, sa56004, g781, tmp451, tmp461, max6654 };

/*
 * The LM90 registers
 */

#define LM90_REG_R_MAN_ID		0xFE
#define LM90_REG_R_CHIP_ID		0xFF
#define LM90_REG_R_CONFIG1		0x03
#define LM90_REG_W_CONFIG1		0x09
#define LM90_REG_R_CONFIG2		0xBF
#define LM90_REG_W_CONFIG2		0xBF
#define LM90_REG_R_CONVRATE		0x04
#define LM90_REG_W_CONVRATE		0x0A
#define LM90_REG_R_STATUS		0x02
#define LM90_REG_R_LOCAL_TEMP		0x00
#define LM90_REG_R_LOCAL_HIGH		0x05
#define LM90_REG_W_LOCAL_HIGH		0x0B
#define LM90_REG_R_LOCAL_LOW		0x06
#define LM90_REG_W_LOCAL_LOW		0x0C
#define LM90_REG_R_LOCAL_CRIT		0x20
#define LM90_REG_W_LOCAL_CRIT		0x20
#define LM90_REG_R_REMOTE_TEMPH		0x01
#define LM90_REG_R_REMOTE_TEMPL		0x10
#define LM90_REG_R_REMOTE_OFFSH		0x11
#define LM90_REG_W_REMOTE_OFFSH		0x11
#define LM90_REG_R_REMOTE_OFFSL		0x12
#define LM90_REG_W_REMOTE_OFFSL		0x12
#define LM90_REG_R_REMOTE_HIGHH		0x07
#define LM90_REG_W_REMOTE_HIGHH		0x0D
#define LM90_REG_R_REMOTE_HIGHL		0x13
#define LM90_REG_W_REMOTE_HIGHL		0x13
#define LM90_REG_R_REMOTE_LOWH		0x08
#define LM90_REG_W_REMOTE_LOWH		0x0E
#define LM90_REG_R_REMOTE_LOWL		0x14
#define LM90_REG_W_REMOTE_LOWL		0x14
#define LM90_REG_R_REMOTE_CRIT		0x19
#define LM90_REG_W_REMOTE_CRIT		0x19
#define LM90_REG_R_TCRIT_HYST		0x21
#define LM90_REG_W_TCRIT_HYST		0x21

/* MAX6646/6647/6649/6654/6657/6658/6659/6695/6696 registers */

#define MAX6657_REG_R_LOCAL_TEMPL	0x11
#define MAX6696_REG_R_STATUS2		0x12
#define MAX6659_REG_R_REMOTE_EMERG	0x16
#define MAX6659_REG_W_REMOTE_EMERG	0x16
#define MAX6659_REG_R_LOCAL_EMERG	0x17
#define MAX6659_REG_W_LOCAL_EMERG	0x17

/*  SA56004 registers */

#define SA56004_REG_R_LOCAL_TEMPL 0x22

#define LM90_MAX_CONVRATE_MS	16000	/* Maximum conversion rate in ms */

/* TMP451/TMP461 registers */
#define TMP451_REG_R_LOCAL_TEMPL	0x15
#define TMP451_REG_CONALERT		0x22

#define TMP461_REG_CHEN			0x16
#define TMP461_REG_DFC			0x24

/*
 * Device flags
 */
#define LM90_FLAG_ADT7461_EXT	(1 << 0) /* ADT7461 extended mode	*/
/* Device features */
#define LM90_HAVE_OFFSET	(1 << 1) /* temperature offset register	*/
#define LM90_HAVE_REM_LIMIT_EXT	(1 << 3) /* extended remote limit	*/
#define LM90_HAVE_EMERGENCY	(1 << 4) /* 3rd upper (emergency) limit	*/
#define LM90_HAVE_EMERGENCY_ALARM (1 << 5)/* emergency alarm		*/
#define LM90_HAVE_TEMP3		(1 << 6) /* 3rd temperature sensor	*/
#define LM90_HAVE_BROKEN_ALERT	(1 << 7) /* Broken alert		*/
#define LM90_HAVE_EXTENDED_TEMP	(1 << 8) /* extended temperature support*/
#define LM90_PAUSE_FOR_CONFIG	(1 << 9) /* Pause conversion for config	*/
#define LM90_HAVE_CRIT		(1 << 10)/* Chip supports CRIT/OVERT register	*/
#define LM90_HAVE_CRIT_ALRM_SWP	(1 << 11)/* critical alarm bits swapped	*/

/* LM90 status */
#define LM90_STATUS_LTHRM	(1 << 0) /* local THERM limit tripped */
#define LM90_STATUS_RTHRM	(1 << 1) /* remote THERM limit tripped */
#define LM90_STATUS_ROPEN	(1 << 2) /* remote is an open circuit */
#define LM90_STATUS_RLOW	(1 << 3) /* remote low temp limit tripped */
#define LM90_STATUS_RHIGH	(1 << 4) /* remote high temp limit tripped */
#define LM90_STATUS_LLOW	(1 << 5) /* local low temp limit tripped */
#define LM90_STATUS_LHIGH	(1 << 6) /* local high temp limit tripped */
#define LM90_STATUS_BUSY	(1 << 7) /* conversion is ongoing */

#define MAX6696_STATUS2_R2THRM	(1 << 1) /* remote2 THERM limit tripped */
#define MAX6696_STATUS2_R2OPEN	(1 << 2) /* remote2 is an open circuit */
#define MAX6696_STATUS2_R2LOW	(1 << 3) /* remote2 low temp limit tripped */
#define MAX6696_STATUS2_R2HIGH	(1 << 4) /* remote2 high temp limit tripped */
#define MAX6696_STATUS2_ROT2	(1 << 5) /* remote emergency limit tripped */
#define MAX6696_STATUS2_R2OT2	(1 << 6) /* remote2 emergency limit tripped */
#define MAX6696_STATUS2_LOT2	(1 << 7) /* local emergency limit tripped */

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id lm90_id[] = {
	{ "adm1032", adm1032 },
	{ "adt7461", adt7461 },
	{ "adt7461a", adt7461 },
	{ "g781", g781 },
	{ "lm90", lm90 },
	{ "lm86", lm86 },
	{ "lm89", lm86 },
	{ "lm99", lm99 },
	{ "max6646", max6646 },
	{ "max6647", max6646 },
	{ "max6649", max6646 },
	{ "max6654", max6654 },
	{ "max6657", max6657 },
	{ "max6658", max6657 },
	{ "max6659", max6659 },
	{ "max6680", max6680 },
	{ "max6681", max6680 },
	{ "max6695", max6696 },
	{ "max6696", max6696 },
	{ "nct1008", adt7461 },
	{ "w83l771", w83l771 },
	{ "sa56004", sa56004 },
	{ "tmp451", tmp451 },
	{ "tmp461", tmp461 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm90_id);

static const struct of_device_id __maybe_unused lm90_of_match[] = {
	{
		.compatible = "adi,adm1032",
		.data = (void *)adm1032
	},
	{
		.compatible = "adi,adt7461",
		.data = (void *)adt7461
	},
	{
		.compatible = "adi,adt7461a",
		.data = (void *)adt7461
	},
	{
		.compatible = "gmt,g781",
		.data = (void *)g781
	},
	{
		.compatible = "national,lm90",
		.data = (void *)lm90
	},
	{
		.compatible = "national,lm86",
		.data = (void *)lm86
	},
	{
		.compatible = "national,lm89",
		.data = (void *)lm86
	},
	{
		.compatible = "national,lm99",
		.data = (void *)lm99
	},
	{
		.compatible = "dallas,max6646",
		.data = (void *)max6646
	},
	{
		.compatible = "dallas,max6647",
		.data = (void *)max6646
	},
	{
		.compatible = "dallas,max6649",
		.data = (void *)max6646
	},
	{
		.compatible = "dallas,max6654",
		.data = (void *)max6654
	},
	{
		.compatible = "dallas,max6657",
		.data = (void *)max6657
	},
	{
		.compatible = "dallas,max6658",
		.data = (void *)max6657
	},
	{
		.compatible = "dallas,max6659",
		.data = (void *)max6659
	},
	{
		.compatible = "dallas,max6680",
		.data = (void *)max6680
	},
	{
		.compatible = "dallas,max6681",
		.data = (void *)max6680
	},
	{
		.compatible = "dallas,max6695",
		.data = (void *)max6696
	},
	{
		.compatible = "dallas,max6696",
		.data = (void *)max6696
	},
	{
		.compatible = "onnn,nct1008",
		.data = (void *)adt7461
	},
	{
		.compatible = "winbond,w83l771",
		.data = (void *)w83l771
	},
	{
		.compatible = "nxp,sa56004",
		.data = (void *)sa56004
	},
	{
		.compatible = "ti,tmp451",
		.data = (void *)tmp451
	},
	{
		.compatible = "ti,tmp461",
		.data = (void *)tmp461
	},
	{ },
};
MODULE_DEVICE_TABLE(of, lm90_of_match);

/*
 * chip type specific parameters
 */
struct lm90_params {
	u32 flags;		/* Capabilities */
	u16 alert_alarms;	/* Which alarm bits trigger ALERT# */
				/* Upper 8 bits for max6695/96 */
	u8 max_convrate;	/* Maximum conversion rate register value */
	u8 reg_local_ext;	/* Extended local temp register (optional) */
};

static const struct lm90_params lm90_params[] = {
	[adm1032] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_REM_LIMIT_EXT
		  | LM90_HAVE_BROKEN_ALERT | LM90_HAVE_CRIT,
		.alert_alarms = 0x7c,
		.max_convrate = 10,
	},
	[adt7461] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_REM_LIMIT_EXT
		  | LM90_HAVE_BROKEN_ALERT | LM90_HAVE_EXTENDED_TEMP
		  | LM90_HAVE_CRIT,
		.alert_alarms = 0x7c,
		.max_convrate = 10,
	},
	[g781] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_REM_LIMIT_EXT
		  | LM90_HAVE_BROKEN_ALERT | LM90_HAVE_CRIT,
		.alert_alarms = 0x7c,
		.max_convrate = 7,
	},
	[lm86] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_REM_LIMIT_EXT
		  | LM90_HAVE_CRIT,
		.alert_alarms = 0x7b,
		.max_convrate = 9,
	},
	[lm90] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_REM_LIMIT_EXT
		  | LM90_HAVE_CRIT,
		.alert_alarms = 0x7b,
		.max_convrate = 9,
	},
	[lm99] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_REM_LIMIT_EXT
		  | LM90_HAVE_CRIT,
		.alert_alarms = 0x7b,
		.max_convrate = 9,
	},
	[max6646] = {
		.flags = LM90_HAVE_CRIT | LM90_HAVE_BROKEN_ALERT,
		.alert_alarms = 0x7c,
		.max_convrate = 6,
		.reg_local_ext = MAX6657_REG_R_LOCAL_TEMPL,
	},
	[max6654] = {
		.flags = LM90_HAVE_BROKEN_ALERT,
		.alert_alarms = 0x7c,
		.max_convrate = 7,
		.reg_local_ext = MAX6657_REG_R_LOCAL_TEMPL,
	},
	[max6657] = {
		.flags = LM90_PAUSE_FOR_CONFIG | LM90_HAVE_CRIT,
		.alert_alarms = 0x7c,
		.max_convrate = 8,
		.reg_local_ext = MAX6657_REG_R_LOCAL_TEMPL,
	},
	[max6659] = {
		.flags = LM90_HAVE_EMERGENCY | LM90_HAVE_CRIT,
		.alert_alarms = 0x7c,
		.max_convrate = 8,
		.reg_local_ext = MAX6657_REG_R_LOCAL_TEMPL,
	},
	[max6680] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_CRIT
		  | LM90_HAVE_CRIT_ALRM_SWP | LM90_HAVE_BROKEN_ALERT,
		.alert_alarms = 0x7c,
		.max_convrate = 7,
	},
	[max6696] = {
		.flags = LM90_HAVE_EMERGENCY
		  | LM90_HAVE_EMERGENCY_ALARM | LM90_HAVE_TEMP3 | LM90_HAVE_CRIT,
		.alert_alarms = 0x1c7c,
		.max_convrate = 6,
		.reg_local_ext = MAX6657_REG_R_LOCAL_TEMPL,
	},
	[w83l771] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_REM_LIMIT_EXT | LM90_HAVE_CRIT,
		.alert_alarms = 0x7c,
		.max_convrate = 8,
	},
	[sa56004] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_REM_LIMIT_EXT | LM90_HAVE_CRIT,
		.alert_alarms = 0x7b,
		.max_convrate = 9,
		.reg_local_ext = SA56004_REG_R_LOCAL_TEMPL,
	},
	[tmp451] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_REM_LIMIT_EXT
		  | LM90_HAVE_BROKEN_ALERT | LM90_HAVE_EXTENDED_TEMP | LM90_HAVE_CRIT,
		.alert_alarms = 0x7c,
		.max_convrate = 9,
		.reg_local_ext = TMP451_REG_R_LOCAL_TEMPL,
	},
	[tmp461] = {
		.flags = LM90_HAVE_OFFSET | LM90_HAVE_REM_LIMIT_EXT
		  | LM90_HAVE_BROKEN_ALERT | LM90_HAVE_EXTENDED_TEMP | LM90_HAVE_CRIT,
		.alert_alarms = 0x7c,
		.max_convrate = 9,
		.reg_local_ext = TMP451_REG_R_LOCAL_TEMPL,
	},
};

/*
 * TEMP8 register index
 */
enum lm90_temp8_reg_index {
	LOCAL_LOW = 0,
	LOCAL_HIGH,
	LOCAL_CRIT,
	REMOTE_CRIT,
	LOCAL_EMERG,	/* max6659 and max6695/96 */
	REMOTE_EMERG,	/* max6659 and max6695/96 */
	REMOTE2_CRIT,	/* max6695/96 only */
	REMOTE2_EMERG,	/* max6695/96 only */
	TEMP8_REG_NUM
};

/*
 * TEMP11 register index
 */
enum lm90_temp11_reg_index {
	REMOTE_TEMP = 0,
	REMOTE_LOW,
	REMOTE_HIGH,
	REMOTE_OFFSET,	/* except max6646, max6657/58/59, and max6695/96 */
	LOCAL_TEMP,
	REMOTE2_TEMP,	/* max6695/96 only */
	REMOTE2_LOW,	/* max6695/96 only */
	REMOTE2_HIGH,	/* max6695/96 only */
	TEMP11_REG_NUM
};

/*
 * Client data (each client gets its own)
 */

struct lm90_data {
	struct i2c_client *client;
	struct device *hwmon_dev;
	u32 channel_config[4];
	struct hwmon_channel_info temp_info;
	const struct hwmon_channel_info *info[3];
	struct hwmon_chip_info chip;
	struct mutex update_lock;
	bool valid;		/* true if register values are valid */
	unsigned long last_updated; /* in jiffies */
	int kind;
	u32 flags;

	unsigned int update_interval; /* in milliseconds */

	u8 config;		/* Current configuration register value */
	u8 config_orig;		/* Original configuration register value */
	u8 convrate_orig;	/* Original conversion rate register value */
	u16 alert_alarms;	/* Which alarm bits trigger ALERT# */
				/* Upper 8 bits for max6695/96 */
	u8 max_convrate;	/* Maximum conversion rate */
	u8 reg_local_ext;	/* local extension register offset */

	/* registers values */
	s8 temp8[TEMP8_REG_NUM];
	s16 temp11[TEMP11_REG_NUM];
	u8 temp_hyst;
	u16 alarms; /* bitvector (upper 8 bits for max6695/96) */
};

/*
 * Support functions
 */

/*
 * The ADM1032 supports PEC but not on write byte transactions, so we need
 * to explicitly ask for a transaction without PEC.
 */
static inline s32 adm1032_write_byte(struct i2c_client *client, u8 value)
{
	return i2c_smbus_xfer(client->adapter, client->addr,
			      client->flags & ~I2C_CLIENT_PEC,
			      I2C_SMBUS_WRITE, value, I2C_SMBUS_BYTE, NULL);
}

/*
 * It is assumed that client->update_lock is held (unless we are in
 * detection or initialization steps). This matters when PEC is enabled,
 * because we don't want the address pointer to change between the write
 * byte and the read byte transactions.
 */
static int lm90_read_reg(struct i2c_client *client, u8 reg)
{
	int err;

	if (client->flags & I2C_CLIENT_PEC) {
		err = adm1032_write_byte(client, reg);
		if (err >= 0)
			err = i2c_smbus_read_byte(client);
	} else
		err = i2c_smbus_read_byte_data(client, reg);

	return err;
}

static int lm90_read16(struct i2c_client *client, u8 regh, u8 regl)
{
	int oldh, newh, l;

	/*
	 * There is a trick here. We have to read two registers to have the
	 * sensor temperature, but we have to beware a conversion could occur
	 * between the readings. The datasheet says we should either use
	 * the one-shot conversion register, which we don't want to do
	 * (disables hardware monitoring) or monitor the busy bit, which is
	 * impossible (we can't read the values and monitor that bit at the
	 * exact same time). So the solution used here is to read the high
	 * byte once, then the low byte, then the high byte again. If the new
	 * high byte matches the old one, then we have a valid reading. Else
	 * we have to read the low byte again, and now we believe we have a
	 * correct reading.
	 */
	oldh = lm90_read_reg(client, regh);
	if (oldh < 0)
		return oldh;
	l = lm90_read_reg(client, regl);
	if (l < 0)
		return l;
	newh = lm90_read_reg(client, regh);
	if (newh < 0)
		return newh;
	if (oldh != newh) {
		l = lm90_read_reg(client, regl);
		if (l < 0)
			return l;
	}
	return (newh << 8) | l;
}

static int lm90_update_confreg(struct lm90_data *data, u8 config)
{
	if (data->config != config) {
		int err;

		err = i2c_smbus_write_byte_data(data->client,
						LM90_REG_W_CONFIG1,
						config);
		if (err)
			return err;
		data->config = config;
	}
	return 0;
}

/*
 * client->update_lock must be held when calling this function (unless we are
 * in detection or initialization steps), and while a remote channel other
 * than channel 0 is selected. Also, calling code must make sure to re-select
 * external channel 0 before releasing the lock. This is necessary because
 * various registers have different meanings as a result of selecting a
 * non-default remote channel.
 */
static int lm90_select_remote_channel(struct lm90_data *data, int channel)
{
	int err = 0;

	if (data->kind == max6696) {
		u8 config = data->config & ~0x08;

		if (channel)
			config |= 0x08;
		err = lm90_update_confreg(data, config);
	}
	return err;
}

static int lm90_write_convrate(struct lm90_data *data, int val)
{
	u8 config = data->config;
	int err;

	/* Save config and pause conversion */
	if (data->flags & LM90_PAUSE_FOR_CONFIG) {
		err = lm90_update_confreg(data, config | 0x40);
		if (err < 0)
			return err;
	}

	/* Set conv rate */
	err = i2c_smbus_write_byte_data(data->client, LM90_REG_W_CONVRATE, val);

	/* Revert change to config */
	lm90_update_confreg(data, config);

	return err;
}

/*
 * Set conversion rate.
 * client->update_lock must be held when calling this function (unless we are
 * in detection or initialization steps).
 */
static int lm90_set_convrate(struct i2c_client *client, struct lm90_data *data,
			     unsigned int interval)
{
	unsigned int update_interval;
	int i, err;

	/* Shift calculations to avoid rounding errors */
	interval <<= 6;

	/* find the nearest update rate */
	for (i = 0, update_interval = LM90_MAX_CONVRATE_MS << 6;
	     i < data->max_convrate; i++, update_interval >>= 1)
		if (interval >= update_interval * 3 / 4)
			break;

	err = lm90_write_convrate(data, i);
	data->update_interval = DIV_ROUND_CLOSEST(update_interval, 64);
	return err;
}

static int lm90_update_limits(struct device *dev)
{
	struct lm90_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int val;

	if (data->flags & LM90_HAVE_CRIT) {
		val = lm90_read_reg(client, LM90_REG_R_LOCAL_CRIT);
		if (val < 0)
			return val;
		data->temp8[LOCAL_CRIT] = val;

		val = lm90_read_reg(client, LM90_REG_R_REMOTE_CRIT);
		if (val < 0)
			return val;
		data->temp8[REMOTE_CRIT] = val;

		val = lm90_read_reg(client, LM90_REG_R_TCRIT_HYST);
		if (val < 0)
			return val;
		data->temp_hyst = val;
	}

	val = lm90_read_reg(client, LM90_REG_R_REMOTE_LOWH);
	if (val < 0)
		return val;
	data->temp11[REMOTE_LOW] = val << 8;

	if (data->flags & LM90_HAVE_REM_LIMIT_EXT) {
		val = lm90_read_reg(client, LM90_REG_R_REMOTE_LOWL);
		if (val < 0)
			return val;
		data->temp11[REMOTE_LOW] |= val;
	}

	val = lm90_read_reg(client, LM90_REG_R_REMOTE_HIGHH);
	if (val < 0)
		return val;
	data->temp11[REMOTE_HIGH] = val << 8;

	if (data->flags & LM90_HAVE_REM_LIMIT_EXT) {
		val = lm90_read_reg(client, LM90_REG_R_REMOTE_HIGHL);
		if (val < 0)
			return val;
		data->temp11[REMOTE_HIGH] |= val;
	}

	if (data->flags & LM90_HAVE_OFFSET) {
		val = lm90_read16(client, LM90_REG_R_REMOTE_OFFSH,
				  LM90_REG_R_REMOTE_OFFSL);
		if (val < 0)
			return val;
		data->temp11[REMOTE_OFFSET] = val;
	}

	if (data->flags & LM90_HAVE_EMERGENCY) {
		val = lm90_read_reg(client, MAX6659_REG_R_LOCAL_EMERG);
		if (val < 0)
			return val;
		data->temp8[LOCAL_EMERG] = val;

		val = lm90_read_reg(client, MAX6659_REG_R_REMOTE_EMERG);
		if (val < 0)
			return val;
		data->temp8[REMOTE_EMERG] = val;
	}

	if (data->kind == max6696) {
		val = lm90_select_remote_channel(data, 1);
		if (val < 0)
			return val;

		val = lm90_read_reg(client, LM90_REG_R_REMOTE_CRIT);
		if (val < 0)
			return val;
		data->temp8[REMOTE2_CRIT] = val;

		val = lm90_read_reg(client, MAX6659_REG_R_REMOTE_EMERG);
		if (val < 0)
			return val;
		data->temp8[REMOTE2_EMERG] = val;

		val = lm90_read_reg(client, LM90_REG_R_REMOTE_LOWH);
		if (val < 0)
			return val;
		data->temp11[REMOTE2_LOW] = val << 8;

		val = lm90_read_reg(client, LM90_REG_R_REMOTE_HIGHH);
		if (val < 0)
			return val;
		data->temp11[REMOTE2_HIGH] = val << 8;

		lm90_select_remote_channel(data, 0);
	}

	return 0;
}

static int lm90_update_device(struct device *dev)
{
	struct lm90_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long next_update;
	int val;

	if (!data->valid) {
		val = lm90_update_limits(dev);
		if (val < 0)
			return val;
	}

	next_update = data->last_updated +
		      msecs_to_jiffies(data->update_interval);
	if (time_after(jiffies, next_update) || !data->valid) {
		dev_dbg(&client->dev, "Updating lm90 data.\n");

		data->valid = false;

		val = lm90_read_reg(client, LM90_REG_R_LOCAL_LOW);
		if (val < 0)
			return val;
		data->temp8[LOCAL_LOW] = val;

		val = lm90_read_reg(client, LM90_REG_R_LOCAL_HIGH);
		if (val < 0)
			return val;
		data->temp8[LOCAL_HIGH] = val;

		if (data->reg_local_ext) {
			val = lm90_read16(client, LM90_REG_R_LOCAL_TEMP,
					  data->reg_local_ext);
			if (val < 0)
				return val;
			data->temp11[LOCAL_TEMP] = val;
		} else {
			val = lm90_read_reg(client, LM90_REG_R_LOCAL_TEMP);
			if (val < 0)
				return val;
			data->temp11[LOCAL_TEMP] = val << 8;
		}
		val = lm90_read16(client, LM90_REG_R_REMOTE_TEMPH,
				  LM90_REG_R_REMOTE_TEMPL);
		if (val < 0)
			return val;
		data->temp11[REMOTE_TEMP] = val;

		val = lm90_read_reg(client, LM90_REG_R_STATUS);
		if (val < 0)
			return val;
		data->alarms = val & ~LM90_STATUS_BUSY;

		if (data->kind == max6696) {
			val = lm90_select_remote_channel(data, 1);
			if (val < 0)
				return val;

			val = lm90_read16(client, LM90_REG_R_REMOTE_TEMPH,
					  LM90_REG_R_REMOTE_TEMPL);
			if (val < 0) {
				lm90_select_remote_channel(data, 0);
				return val;
			}
			data->temp11[REMOTE2_TEMP] = val;

			lm90_select_remote_channel(data, 0);

			val = lm90_read_reg(client, MAX6696_REG_R_STATUS2);
			if (val < 0)
				return val;
			data->alarms |= val << 8;
		}

		/*
		 * Re-enable ALERT# output if it was originally enabled and
		 * relevant alarms are all clear
		 */
		if ((client->irq || !(data->config_orig & 0x80)) &&
		    !(data->alarms & data->alert_alarms)) {
			if (data->config & 0x80) {
				dev_dbg(&client->dev, "Re-enabling ALERT#\n");
				lm90_update_confreg(data, data->config & ~0x80);
			}
		}

		data->last_updated = jiffies;
		data->valid = true;
	}

	return 0;
}

/*
 * Conversions
 * For local temperatures and limits, critical limits and the hysteresis
 * value, the LM90 uses signed 8-bit values with LSB = 1 degree Celsius.
 * For remote temperatures and limits, it uses signed 11-bit values with
 * LSB = 0.125 degree Celsius, left-justified in 16-bit registers.  Some
 * Maxim chips use unsigned values.
 */

static inline int temp_from_s8(s8 val)
{
	return val * 1000;
}

static inline int temp_from_u8(u8 val)
{
	return val * 1000;
}

static inline int temp_from_s16(s16 val)
{
	return val / 32 * 125;
}

static inline int temp_from_u16(u16 val)
{
	return val / 32 * 125;
}

static s8 temp_to_s8(long val)
{
	if (val <= -128000)
		return -128;
	if (val >= 127000)
		return 127;
	if (val < 0)
		return (val - 500) / 1000;
	return (val + 500) / 1000;
}

static u8 temp_to_u8(long val)
{
	if (val <= 0)
		return 0;
	if (val >= 255000)
		return 255;
	return (val + 500) / 1000;
}

static s16 temp_to_s16(long val)
{
	if (val <= -128000)
		return 0x8000;
	if (val >= 127875)
		return 0x7FE0;
	if (val < 0)
		return (val - 62) / 125 * 32;
	return (val + 62) / 125 * 32;
}

static u8 hyst_to_reg(long val)
{
	if (val <= 0)
		return 0;
	if (val >= 30500)
		return 31;
	return (val + 500) / 1000;
}

/*
 * ADT7461 in compatibility mode is almost identical to LM90 except that
 * attempts to write values that are outside the range 0 < temp < 127 are
 * treated as the boundary value.
 *
 * ADT7461 in "extended mode" operation uses unsigned integers offset by
 * 64 (e.g., 0 -> -64 degC).  The range is restricted to -64..191 degC.
 */
static inline int temp_from_u8_adt7461(struct lm90_data *data, u8 val)
{
	if (data->flags & LM90_FLAG_ADT7461_EXT)
		return (val - 64) * 1000;
	return temp_from_s8(val);
}

static inline int temp_from_u16_adt7461(struct lm90_data *data, u16 val)
{
	if (data->flags & LM90_FLAG_ADT7461_EXT)
		return (val - 0x4000) / 64 * 250;
	return temp_from_s16(val);
}

static u8 temp_to_u8_adt7461(struct lm90_data *data, long val)
{
	if (data->flags & LM90_FLAG_ADT7461_EXT) {
		if (val <= -64000)
			return 0;
		if (val >= 191000)
			return 0xFF;
		return (val + 500 + 64000) / 1000;
	}
	if (val <= 0)
		return 0;
	if (val >= 127000)
		return 127;
	return (val + 500) / 1000;
}

static u16 temp_to_u16_adt7461(struct lm90_data *data, long val)
{
	if (data->flags & LM90_FLAG_ADT7461_EXT) {
		if (val <= -64000)
			return 0;
		if (val >= 191750)
			return 0xFFC0;
		return (val + 64000 + 125) / 250 * 64;
	}
	if (val <= 0)
		return 0;
	if (val >= 127750)
		return 0x7FC0;
	return (val + 125) / 250 * 64;
}

/* pec used for ADM1032 only */
static ssize_t pec_show(struct device *dev, struct device_attribute *dummy,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);

	return sprintf(buf, "%d\n", !!(client->flags & I2C_CLIENT_PEC));
}

static ssize_t pec_store(struct device *dev, struct device_attribute *dummy,
			 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	switch (val) {
	case 0:
		client->flags &= ~I2C_CLIENT_PEC;
		break;
	case 1:
		client->flags |= I2C_CLIENT_PEC;
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR_RW(pec);

static int lm90_get_temp11(struct lm90_data *data, int index)
{
	s16 temp11 = data->temp11[index];
	int temp;

	if (data->flags & LM90_HAVE_EXTENDED_TEMP)
		temp = temp_from_u16_adt7461(data, temp11);
	else if (data->kind == max6646)
		temp = temp_from_u16(temp11);
	else
		temp = temp_from_s16(temp11);

	/* +16 degrees offset for temp2 for the LM99 */
	if (data->kind == lm99 && index <= 2)
		temp += 16000;

	return temp;
}

static int lm90_set_temp11(struct lm90_data *data, int index, long val)
{
	static struct reg {
		u8 high;
		u8 low;
	} reg[] = {
	[REMOTE_LOW] = { LM90_REG_W_REMOTE_LOWH, LM90_REG_W_REMOTE_LOWL },
	[REMOTE_HIGH] = { LM90_REG_W_REMOTE_HIGHH, LM90_REG_W_REMOTE_HIGHL },
	[REMOTE_OFFSET] = { LM90_REG_W_REMOTE_OFFSH, LM90_REG_W_REMOTE_OFFSL },
	[REMOTE2_LOW] = { LM90_REG_W_REMOTE_LOWH, LM90_REG_W_REMOTE_LOWL },
	[REMOTE2_HIGH] = { LM90_REG_W_REMOTE_HIGHH, LM90_REG_W_REMOTE_HIGHL }
	};
	struct i2c_client *client = data->client;
	struct reg *regp = &reg[index];
	int err;

	/* +16 degrees offset for temp2 for the LM99 */
	if (data->kind == lm99 && index <= 2) {
		/* prevent integer underflow */
		val = max(val, -128000l);
		val -= 16000;
	}

	if (data->flags & LM90_HAVE_EXTENDED_TEMP)
		data->temp11[index] = temp_to_u16_adt7461(data, val);
	else if (data->kind == max6646)
		data->temp11[index] = temp_to_u8(val) << 8;
	else if (data->flags & LM90_HAVE_REM_LIMIT_EXT)
		data->temp11[index] = temp_to_s16(val);
	else
		data->temp11[index] = temp_to_s8(val) << 8;

	lm90_select_remote_channel(data, index >= 3);
	err = i2c_smbus_write_byte_data(client, regp->high,
				  data->temp11[index] >> 8);
	if (err < 0)
		return err;
	if (data->flags & LM90_HAVE_REM_LIMIT_EXT)
		err = i2c_smbus_write_byte_data(client, regp->low,
						data->temp11[index] & 0xff);

	lm90_select_remote_channel(data, 0);
	return err;
}

static int lm90_get_temp8(struct lm90_data *data, int index)
{
	s8 temp8 = data->temp8[index];
	int temp;

	if (data->flags & LM90_HAVE_EXTENDED_TEMP)
		temp = temp_from_u8_adt7461(data, temp8);
	else if (data->kind == max6646)
		temp = temp_from_u8(temp8);
	else
		temp = temp_from_s8(temp8);

	/* +16 degrees offset for temp2 for the LM99 */
	if (data->kind == lm99 && index == 3)
		temp += 16000;

	return temp;
}

static int lm90_set_temp8(struct lm90_data *data, int index, long val)
{
	static const u8 reg[TEMP8_REG_NUM] = {
		LM90_REG_W_LOCAL_LOW,
		LM90_REG_W_LOCAL_HIGH,
		LM90_REG_W_LOCAL_CRIT,
		LM90_REG_W_REMOTE_CRIT,
		MAX6659_REG_W_LOCAL_EMERG,
		MAX6659_REG_W_REMOTE_EMERG,
		LM90_REG_W_REMOTE_CRIT,
		MAX6659_REG_W_REMOTE_EMERG,
	};
	struct i2c_client *client = data->client;
	int err;

	/* +16 degrees offset for temp2 for the LM99 */
	if (data->kind == lm99 && index == 3) {
		/* prevent integer underflow */
		val = max(val, -128000l);
		val -= 16000;
	}

	if (data->flags & LM90_HAVE_EXTENDED_TEMP)
		data->temp8[index] = temp_to_u8_adt7461(data, val);
	else if (data->kind == max6646)
		data->temp8[index] = temp_to_u8(val);
	else
		data->temp8[index] = temp_to_s8(val);

	lm90_select_remote_channel(data, index >= 6);
	err = i2c_smbus_write_byte_data(client, reg[index], data->temp8[index]);
	lm90_select_remote_channel(data, 0);

	return err;
}

static int lm90_get_temphyst(struct lm90_data *data, int index)
{
	int temp;

	if (data->flags & LM90_HAVE_EXTENDED_TEMP)
		temp = temp_from_u8_adt7461(data, data->temp8[index]);
	else if (data->kind == max6646)
		temp = temp_from_u8(data->temp8[index]);
	else
		temp = temp_from_s8(data->temp8[index]);

	/* +16 degrees offset for temp2 for the LM99 */
	if (data->kind == lm99 && index == 3)
		temp += 16000;

	return temp - temp_from_s8(data->temp_hyst);
}

static int lm90_set_temphyst(struct lm90_data *data, long val)
{
	struct i2c_client *client = data->client;
	int temp;
	int err;

	if (data->flags & LM90_HAVE_EXTENDED_TEMP)
		temp = temp_from_u8_adt7461(data, data->temp8[LOCAL_CRIT]);
	else if (data->kind == max6646)
		temp = temp_from_u8(data->temp8[LOCAL_CRIT]);
	else
		temp = temp_from_s8(data->temp8[LOCAL_CRIT]);

	/* prevent integer overflow/underflow */
	val = clamp_val(val, -128000l, 255000l);

	data->temp_hyst = hyst_to_reg(temp - val);
	err = i2c_smbus_write_byte_data(client, LM90_REG_W_TCRIT_HYST,
					data->temp_hyst);
	return err;
}

static const u8 lm90_temp_index[3] = {
	LOCAL_TEMP, REMOTE_TEMP, REMOTE2_TEMP
};

static const u8 lm90_temp_min_index[3] = {
	LOCAL_LOW, REMOTE_LOW, REMOTE2_LOW
};

static const u8 lm90_temp_max_index[3] = {
	LOCAL_HIGH, REMOTE_HIGH, REMOTE2_HIGH
};

static const u8 lm90_temp_crit_index[3] = {
	LOCAL_CRIT, REMOTE_CRIT, REMOTE2_CRIT
};

static const u8 lm90_temp_emerg_index[3] = {
	LOCAL_EMERG, REMOTE_EMERG, REMOTE2_EMERG
};

static const u8 lm90_min_alarm_bits[3] = { 5, 3, 11 };
static const u8 lm90_max_alarm_bits[3] = { 6, 4, 12 };
static const u8 lm90_crit_alarm_bits[3] = { 0, 1, 9 };
static const u8 lm90_crit_alarm_bits_swapped[3] = { 1, 0, 9 };
static const u8 lm90_emergency_alarm_bits[3] = { 15, 13, 14 };
static const u8 lm90_fault_bits[3] = { 0, 2, 10 };

static int lm90_temp_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct lm90_data *data = dev_get_drvdata(dev);
	int err;

	mutex_lock(&data->update_lock);
	err = lm90_update_device(dev);
	mutex_unlock(&data->update_lock);
	if (err)
		return err;

	switch (attr) {
	case hwmon_temp_input:
		*val = lm90_get_temp11(data, lm90_temp_index[channel]);
		break;
	case hwmon_temp_min_alarm:
		*val = (data->alarms >> lm90_min_alarm_bits[channel]) & 1;
		break;
	case hwmon_temp_max_alarm:
		*val = (data->alarms >> lm90_max_alarm_bits[channel]) & 1;
		break;
	case hwmon_temp_crit_alarm:
		if (data->flags & LM90_HAVE_CRIT_ALRM_SWP)
			*val = (data->alarms >> lm90_crit_alarm_bits_swapped[channel]) & 1;
		else
			*val = (data->alarms >> lm90_crit_alarm_bits[channel]) & 1;
		break;
	case hwmon_temp_emergency_alarm:
		*val = (data->alarms >> lm90_emergency_alarm_bits[channel]) & 1;
		break;
	case hwmon_temp_fault:
		*val = (data->alarms >> lm90_fault_bits[channel]) & 1;
		break;
	case hwmon_temp_min:
		if (channel == 0)
			*val = lm90_get_temp8(data,
					      lm90_temp_min_index[channel]);
		else
			*val = lm90_get_temp11(data,
					       lm90_temp_min_index[channel]);
		break;
	case hwmon_temp_max:
		if (channel == 0)
			*val = lm90_get_temp8(data,
					      lm90_temp_max_index[channel]);
		else
			*val = lm90_get_temp11(data,
					       lm90_temp_max_index[channel]);
		break;
	case hwmon_temp_crit:
		*val = lm90_get_temp8(data, lm90_temp_crit_index[channel]);
		break;
	case hwmon_temp_crit_hyst:
		*val = lm90_get_temphyst(data, lm90_temp_crit_index[channel]);
		break;
	case hwmon_temp_emergency:
		*val = lm90_get_temp8(data, lm90_temp_emerg_index[channel]);
		break;
	case hwmon_temp_emergency_hyst:
		*val = lm90_get_temphyst(data, lm90_temp_emerg_index[channel]);
		break;
	case hwmon_temp_offset:
		*val = lm90_get_temp11(data, REMOTE_OFFSET);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int lm90_temp_write(struct device *dev, u32 attr, int channel, long val)
{
	struct lm90_data *data = dev_get_drvdata(dev);
	int err;

	mutex_lock(&data->update_lock);

	err = lm90_update_device(dev);
	if (err)
		goto error;

	switch (attr) {
	case hwmon_temp_min:
		if (channel == 0)
			err = lm90_set_temp8(data,
					      lm90_temp_min_index[channel],
					      val);
		else
			err = lm90_set_temp11(data,
					      lm90_temp_min_index[channel],
					      val);
		break;
	case hwmon_temp_max:
		if (channel == 0)
			err = lm90_set_temp8(data,
					     lm90_temp_max_index[channel],
					     val);
		else
			err = lm90_set_temp11(data,
					      lm90_temp_max_index[channel],
					      val);
		break;
	case hwmon_temp_crit:
		err = lm90_set_temp8(data, lm90_temp_crit_index[channel], val);
		break;
	case hwmon_temp_crit_hyst:
		err = lm90_set_temphyst(data, val);
		break;
	case hwmon_temp_emergency:
		err = lm90_set_temp8(data, lm90_temp_emerg_index[channel], val);
		break;
	case hwmon_temp_offset:
		err = lm90_set_temp11(data, REMOTE_OFFSET, val);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
error:
	mutex_unlock(&data->update_lock);

	return err;
}

static umode_t lm90_temp_is_visible(const void *data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_emergency_alarm:
	case hwmon_temp_emergency_hyst:
	case hwmon_temp_fault:
		return 0444;
	case hwmon_temp_min:
	case hwmon_temp_max:
	case hwmon_temp_crit:
	case hwmon_temp_emergency:
	case hwmon_temp_offset:
		return 0644;
	case hwmon_temp_crit_hyst:
		if (channel == 0)
			return 0644;
		return 0444;
	default:
		return 0;
	}
}

static int lm90_chip_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct lm90_data *data = dev_get_drvdata(dev);
	int err;

	mutex_lock(&data->update_lock);
	err = lm90_update_device(dev);
	mutex_unlock(&data->update_lock);
	if (err)
		return err;

	switch (attr) {
	case hwmon_chip_update_interval:
		*val = data->update_interval;
		break;
	case hwmon_chip_alarms:
		*val = data->alarms;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int lm90_chip_write(struct device *dev, u32 attr, int channel, long val)
{
	struct lm90_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int err;

	mutex_lock(&data->update_lock);

	err = lm90_update_device(dev);
	if (err)
		goto error;

	switch (attr) {
	case hwmon_chip_update_interval:
		err = lm90_set_convrate(client, data,
					clamp_val(val, 0, 100000));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
error:
	mutex_unlock(&data->update_lock);

	return err;
}

static umode_t lm90_chip_is_visible(const void *data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_chip_update_interval:
		return 0644;
	case hwmon_chip_alarms:
		return 0444;
	default:
		return 0;
	}
}

static int lm90_read(struct device *dev, enum hwmon_sensor_types type,
		     u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return lm90_chip_read(dev, attr, channel, val);
	case hwmon_temp:
		return lm90_temp_read(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int lm90_write(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_chip:
		return lm90_chip_write(dev, attr, channel, val);
	case hwmon_temp:
		return lm90_temp_write(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t lm90_is_visible(const void *data, enum hwmon_sensor_types type,
			       u32 attr, int channel)
{
	switch (type) {
	case hwmon_chip:
		return lm90_chip_is_visible(data, attr, channel);
	case hwmon_temp:
		return lm90_temp_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm90_detect(struct i2c_client *client,
		       struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int address = client->addr;
	const char *name = NULL;
	int man_id, chip_id, config1, config2, convrate;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* detection and identification */
	man_id = i2c_smbus_read_byte_data(client, LM90_REG_R_MAN_ID);
	chip_id = i2c_smbus_read_byte_data(client, LM90_REG_R_CHIP_ID);
	config1 = i2c_smbus_read_byte_data(client, LM90_REG_R_CONFIG1);
	convrate = i2c_smbus_read_byte_data(client, LM90_REG_R_CONVRATE);
	if (man_id < 0 || chip_id < 0 || config1 < 0 || convrate < 0)
		return -ENODEV;

	if (man_id == 0x01 || man_id == 0x5C || man_id == 0xA1) {
		config2 = i2c_smbus_read_byte_data(client, LM90_REG_R_CONFIG2);
		if (config2 < 0)
			return -ENODEV;
	}

	if ((address == 0x4C || address == 0x4D)
	 && man_id == 0x01) { /* National Semiconductor */
		if ((config1 & 0x2A) == 0x00
		 && (config2 & 0xF8) == 0x00
		 && convrate <= 0x09) {
			if (address == 0x4C
			 && (chip_id & 0xF0) == 0x20) { /* LM90 */
				name = "lm90";
			} else
			if ((chip_id & 0xF0) == 0x30) { /* LM89/LM99 */
				name = "lm99";
				dev_info(&adapter->dev,
					 "Assuming LM99 chip at 0x%02x\n",
					 address);
				dev_info(&adapter->dev,
					 "If it is an LM89, instantiate it "
					 "with the new_device sysfs "
					 "interface\n");
			} else
			if (address == 0x4C
			 && (chip_id & 0xF0) == 0x10) { /* LM86 */
				name = "lm86";
			}
		}
	} else
	if ((address == 0x4C || address == 0x4D)
	 && man_id == 0x41) { /* Analog Devices */
		if ((chip_id & 0xF0) == 0x40 /* ADM1032 */
		 && (config1 & 0x3F) == 0x00
		 && convrate <= 0x0A) {
			name = "adm1032";
			/*
			 * The ADM1032 supports PEC, but only if combined
			 * transactions are not used.
			 */
			if (i2c_check_functionality(adapter,
						    I2C_FUNC_SMBUS_BYTE))
				info->flags |= I2C_CLIENT_PEC;
		} else
		if (chip_id == 0x51 /* ADT7461 */
		 && (config1 & 0x1B) == 0x00
		 && convrate <= 0x0A) {
			name = "adt7461";
		} else
		if (chip_id == 0x57 /* ADT7461A, NCT1008 */
		 && (config1 & 0x1B) == 0x00
		 && convrate <= 0x0A) {
			name = "adt7461a";
		}
	} else
	if (man_id == 0x4D) { /* Maxim */
		int emerg, emerg2, status2;

		/*
		 * We read MAX6659_REG_R_REMOTE_EMERG twice, and re-read
		 * LM90_REG_R_MAN_ID in between. If MAX6659_REG_R_REMOTE_EMERG
		 * exists, both readings will reflect the same value. Otherwise,
		 * the readings will be different.
		 */
		emerg = i2c_smbus_read_byte_data(client,
						 MAX6659_REG_R_REMOTE_EMERG);
		man_id = i2c_smbus_read_byte_data(client,
						  LM90_REG_R_MAN_ID);
		emerg2 = i2c_smbus_read_byte_data(client,
						  MAX6659_REG_R_REMOTE_EMERG);
		status2 = i2c_smbus_read_byte_data(client,
						   MAX6696_REG_R_STATUS2);
		if (emerg < 0 || man_id < 0 || emerg2 < 0 || status2 < 0)
			return -ENODEV;

		/*
		 * The MAX6657, MAX6658 and MAX6659 do NOT have a chip_id
		 * register. Reading from that address will return the last
		 * read value, which in our case is those of the man_id
		 * register. Likewise, the config1 register seems to lack a
		 * low nibble, so the value will be those of the previous
		 * read, so in our case those of the man_id register.
		 * MAX6659 has a third set of upper temperature limit registers.
		 * Those registers also return values on MAX6657 and MAX6658,
		 * thus the only way to detect MAX6659 is by its address.
		 * For this reason it will be mis-detected as MAX6657 if its
		 * address is 0x4C.
		 */
		if (chip_id == man_id
		 && (address == 0x4C || address == 0x4D || address == 0x4E)
		 && (config1 & 0x1F) == (man_id & 0x0F)
		 && convrate <= 0x09) {
			if (address == 0x4C)
				name = "max6657";
			else
				name = "max6659";
		} else
		/*
		 * Even though MAX6695 and MAX6696 do not have a chip ID
		 * register, reading it returns 0x01. Bit 4 of the config1
		 * register is unused and should return zero when read. Bit 0 of
		 * the status2 register is unused and should return zero when
		 * read.
		 *
		 * MAX6695 and MAX6696 have an additional set of temperature
		 * limit registers. We can detect those chips by checking if
		 * one of those registers exists.
		 */
		if (chip_id == 0x01
		 && (config1 & 0x10) == 0x00
		 && (status2 & 0x01) == 0x00
		 && emerg == emerg2
		 && convrate <= 0x07) {
			name = "max6696";
		} else
		/*
		 * The chip_id register of the MAX6680 and MAX6681 holds the
		 * revision of the chip. The lowest bit of the config1 register
		 * is unused and should return zero when read, so should the
		 * second to last bit of config1 (software reset).
		 */
		if (chip_id == 0x01
		 && (config1 & 0x03) == 0x00
		 && convrate <= 0x07) {
			name = "max6680";
		} else
		/*
		 * The chip_id register of the MAX6646/6647/6649 holds the
		 * revision of the chip. The lowest 6 bits of the config1
		 * register are unused and should return zero when read.
		 */
		if (chip_id == 0x59
		 && (config1 & 0x3f) == 0x00
		 && convrate <= 0x07) {
			name = "max6646";
		} else
		/*
		 * The chip_id of the MAX6654 holds the revision of the chip.
		 * The lowest 3 bits of the config1 register are unused and
		 * should return zero when read.
		 */
		if (chip_id == 0x08
		 && (config1 & 0x07) == 0x00
		 && convrate <= 0x07) {
			name = "max6654";
		}
	} else
	if (address == 0x4C
	 && man_id == 0x5C) { /* Winbond/Nuvoton */
		if ((config1 & 0x2A) == 0x00
		 && (config2 & 0xF8) == 0x00) {
			if (chip_id == 0x01 /* W83L771W/G */
			 && convrate <= 0x09) {
				name = "w83l771";
			} else
			if ((chip_id & 0xFE) == 0x10 /* W83L771AWG/ASG */
			 && convrate <= 0x08) {
				name = "w83l771";
			}
		}
	} else
	if (address >= 0x48 && address <= 0x4F
	 && man_id == 0xA1) { /*  NXP Semiconductor/Philips */
		if (chip_id == 0x00
		 && (config1 & 0x2A) == 0x00
		 && (config2 & 0xFE) == 0x00
		 && convrate <= 0x09) {
			name = "sa56004";
		}
	} else
	if ((address == 0x4C || address == 0x4D)
	 && man_id == 0x47) { /* GMT */
		if (chip_id == 0x01 /* G781 */
		 && (config1 & 0x3F) == 0x00
		 && convrate <= 0x08)
			name = "g781";
	} else
	if (man_id == 0x55 && chip_id == 0x00 &&
	    (config1 & 0x1B) == 0x00 && convrate <= 0x09) {
		int local_ext, conalert, chen, dfc;

		local_ext = i2c_smbus_read_byte_data(client,
						     TMP451_REG_R_LOCAL_TEMPL);
		conalert = i2c_smbus_read_byte_data(client,
						    TMP451_REG_CONALERT);
		chen = i2c_smbus_read_byte_data(client, TMP461_REG_CHEN);
		dfc = i2c_smbus_read_byte_data(client, TMP461_REG_DFC);

		if ((local_ext & 0x0F) == 0x00 &&
		    (conalert & 0xf1) == 0x01 &&
		    (chen & 0xfc) == 0x00 &&
		    (dfc & 0xfc) == 0x00) {
			if (address == 0x4c && !(chen & 0x03))
				name = "tmp451";
			else if (address >= 0x48 && address <= 0x4f)
				name = "tmp461";
		}
	}

	if (!name) { /* identification failed */
		dev_dbg(&adapter->dev,
			"Unsupported chip at 0x%02x (man_id=0x%02X, "
			"chip_id=0x%02X)\n", address, man_id, chip_id);
		return -ENODEV;
	}

	strlcpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static void lm90_restore_conf(void *_data)
{
	struct lm90_data *data = _data;
	struct i2c_client *client = data->client;

	/* Restore initial configuration */
	lm90_write_convrate(data, data->convrate_orig);
	i2c_smbus_write_byte_data(client, LM90_REG_W_CONFIG1,
				  data->config_orig);
}

static int lm90_init_client(struct i2c_client *client, struct lm90_data *data)
{
	int config, convrate;

	convrate = lm90_read_reg(client, LM90_REG_R_CONVRATE);
	if (convrate < 0)
		return convrate;
	data->convrate_orig = convrate;

	/*
	 * Start the conversions.
	 */
	config = lm90_read_reg(client, LM90_REG_R_CONFIG1);
	if (config < 0)
		return config;
	data->config_orig = config;
	data->config = config;

	lm90_set_convrate(client, data, 500); /* 500ms; 2Hz conversion rate */

	/* Check Temperature Range Select */
	if (data->flags & LM90_HAVE_EXTENDED_TEMP) {
		if (config & 0x04)
			data->flags |= LM90_FLAG_ADT7461_EXT;
	}

	/*
	 * Put MAX6680/MAX8881 into extended resolution (bit 0x10,
	 * 0.125 degree resolution) and range (0x08, extend range
	 * to -64 degree) mode for the remote temperature sensor.
	 */
	if (data->kind == max6680)
		config |= 0x18;

	/*
	 * Put MAX6654 into extended range (0x20, extend minimum range from
	 * 0 degrees to -64 degrees). Note that extended resolution is not
	 * possible on the MAX6654 unless conversion rate is set to 1 Hz or
	 * slower, which is intentionally not done by default.
	 */
	if (data->kind == max6654)
		config |= 0x20;

	/*
	 * Select external channel 0 for max6695/96
	 */
	if (data->kind == max6696)
		config &= ~0x08;

	/*
	 * Interrupt is enabled by default on reset, but it may be disabled
	 * by bootloader, unmask it.
	 */
	if (client->irq)
		config &= ~0x80;

	config &= 0xBF;	/* run */
	lm90_update_confreg(data, config);

	return devm_add_action_or_reset(&client->dev, lm90_restore_conf, data);
}

static bool lm90_is_tripped(struct i2c_client *client, u16 *status)
{
	struct lm90_data *data = i2c_get_clientdata(client);
	int st, st2 = 0;

	st = lm90_read_reg(client, LM90_REG_R_STATUS);
	if (st < 0)
		return false;

	if (data->kind == max6696) {
		st2 = lm90_read_reg(client, MAX6696_REG_R_STATUS2);
		if (st2 < 0)
			return false;
	}

	*status = st | (st2 << 8);

	if ((st & 0x7f) == 0 && (st2 & 0xfe) == 0)
		return false;

	if ((st & (LM90_STATUS_LLOW | LM90_STATUS_LHIGH | LM90_STATUS_LTHRM)) ||
	    (st2 & MAX6696_STATUS2_LOT2))
		dev_dbg(&client->dev,
			"temp%d out of range, please check!\n", 1);
	if ((st & (LM90_STATUS_RLOW | LM90_STATUS_RHIGH | LM90_STATUS_RTHRM)) ||
	    (st2 & MAX6696_STATUS2_ROT2))
		dev_dbg(&client->dev,
			"temp%d out of range, please check!\n", 2);
	if (st & LM90_STATUS_ROPEN)
		dev_dbg(&client->dev,
			"temp%d diode open, please check!\n", 2);
	if (st2 & (MAX6696_STATUS2_R2LOW | MAX6696_STATUS2_R2HIGH |
		   MAX6696_STATUS2_R2THRM | MAX6696_STATUS2_R2OT2))
		dev_dbg(&client->dev,
			"temp%d out of range, please check!\n", 3);
	if (st2 & MAX6696_STATUS2_R2OPEN)
		dev_dbg(&client->dev,
			"temp%d diode open, please check!\n", 3);

	if (st & LM90_STATUS_LLOW)
		hwmon_notify_event(data->hwmon_dev, hwmon_temp,
				   hwmon_temp_min_alarm, 0);
	if (st & LM90_STATUS_RLOW)
		hwmon_notify_event(data->hwmon_dev, hwmon_temp,
				   hwmon_temp_min_alarm, 1);
	if (st2 & MAX6696_STATUS2_R2LOW)
		hwmon_notify_event(data->hwmon_dev, hwmon_temp,
				   hwmon_temp_min_alarm, 2);
	if (st & LM90_STATUS_LHIGH)
		hwmon_notify_event(data->hwmon_dev, hwmon_temp,
				   hwmon_temp_max_alarm, 0);
	if (st & LM90_STATUS_RHIGH)
		hwmon_notify_event(data->hwmon_dev, hwmon_temp,
				   hwmon_temp_max_alarm, 1);
	if (st2 & MAX6696_STATUS2_R2HIGH)
		hwmon_notify_event(data->hwmon_dev, hwmon_temp,
				   hwmon_temp_max_alarm, 2);

	return true;
}

static irqreturn_t lm90_irq_thread(int irq, void *dev_id)
{
	struct i2c_client *client = dev_id;
	u16 status;

	if (lm90_is_tripped(client, &status))
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static void lm90_remove_pec(void *dev)
{
	device_remove_file(dev, &dev_attr_pec);
}

static void lm90_regulator_disable(void *regulator)
{
	regulator_disable(regulator);
}


static const struct hwmon_ops lm90_ops = {
	.is_visible = lm90_is_visible,
	.read = lm90_read,
	.write = lm90_write,
};

static int lm90_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct i2c_adapter *adapter = client->adapter;
	struct hwmon_channel_info *info;
	struct regulator *regulator;
	struct device *hwmon_dev;
	struct lm90_data *data;
	int err;

	regulator = devm_regulator_get(dev, "vcc");
	if (IS_ERR(regulator))
		return PTR_ERR(regulator);

	err = regulator_enable(regulator);
	if (err < 0) {
		dev_err(dev, "Failed to enable regulator: %d\n", err);
		return err;
	}

	err = devm_add_action_or_reset(dev, lm90_regulator_disable, regulator);
	if (err)
		return err;

	data = devm_kzalloc(dev, sizeof(struct lm90_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Set the device type */
	if (client->dev.of_node)
		data->kind = (enum chips)of_device_get_match_data(&client->dev);
	else
		data->kind = i2c_match_id(lm90_id, client)->driver_data;
	if (data->kind == adm1032) {
		if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
			client->flags &= ~I2C_CLIENT_PEC;
	}

	/*
	 * Different devices have different alarm bits triggering the
	 * ALERT# output
	 */
	data->alert_alarms = lm90_params[data->kind].alert_alarms;

	/* Set chip capabilities */
	data->flags = lm90_params[data->kind].flags;

	data->chip.ops = &lm90_ops;
	data->chip.info = data->info;

	data->info[0] = HWMON_CHANNEL_INFO(chip,
		HWMON_C_REGISTER_TZ | HWMON_C_UPDATE_INTERVAL | HWMON_C_ALARMS);
	data->info[1] = &data->temp_info;

	info = &data->temp_info;
	info->type = hwmon_temp;
	info->config = data->channel_config;

	data->channel_config[0] = HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
		HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM;
	data->channel_config[1] = HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
		HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM | HWMON_T_FAULT;

	if (data->flags & LM90_HAVE_CRIT) {
		data->channel_config[0] |= HWMON_T_CRIT | HWMON_T_CRIT_ALARM | HWMON_T_CRIT_HYST;
		data->channel_config[1] |= HWMON_T_CRIT | HWMON_T_CRIT_ALARM | HWMON_T_CRIT_HYST;
	}

	if (data->flags & LM90_HAVE_OFFSET)
		data->channel_config[1] |= HWMON_T_OFFSET;

	if (data->flags & LM90_HAVE_EMERGENCY) {
		data->channel_config[0] |= HWMON_T_EMERGENCY |
			HWMON_T_EMERGENCY_HYST;
		data->channel_config[1] |= HWMON_T_EMERGENCY |
			HWMON_T_EMERGENCY_HYST;
	}

	if (data->flags & LM90_HAVE_EMERGENCY_ALARM) {
		data->channel_config[0] |= HWMON_T_EMERGENCY_ALARM;
		data->channel_config[1] |= HWMON_T_EMERGENCY_ALARM;
	}

	if (data->flags & LM90_HAVE_TEMP3) {
		data->channel_config[2] = HWMON_T_INPUT |
			HWMON_T_MIN | HWMON_T_MAX |
			HWMON_T_CRIT | HWMON_T_CRIT_HYST |
			HWMON_T_EMERGENCY | HWMON_T_EMERGENCY_HYST |
			HWMON_T_MIN_ALARM | HWMON_T_MAX_ALARM |
			HWMON_T_CRIT_ALARM | HWMON_T_EMERGENCY_ALARM |
			HWMON_T_FAULT;
	}

	data->reg_local_ext = lm90_params[data->kind].reg_local_ext;

	/* Set maximum conversion rate */
	data->max_convrate = lm90_params[data->kind].max_convrate;

	/* Initialize the LM90 chip */
	err = lm90_init_client(client, data);
	if (err < 0) {
		dev_err(dev, "Failed to initialize device\n");
		return err;
	}

	/*
	 * The 'pec' attribute is attached to the i2c device and thus created
	 * separately.
	 */
	if (client->flags & I2C_CLIENT_PEC) {
		err = device_create_file(dev, &dev_attr_pec);
		if (err)
			return err;
		err = devm_add_action_or_reset(dev, lm90_remove_pec, dev);
		if (err)
			return err;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data, &data->chip,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	data->hwmon_dev = hwmon_dev;

	if (client->irq) {
		dev_dbg(dev, "IRQ: %d\n", client->irq);
		err = devm_request_threaded_irq(dev, client->irq,
						NULL, lm90_irq_thread,
						IRQF_ONESHOT, "lm90", client);
		if (err < 0) {
			dev_err(dev, "cannot request IRQ %d\n", client->irq);
			return err;
		}
	}

	return 0;
}

static void lm90_alert(struct i2c_client *client, enum i2c_alert_protocol type,
		       unsigned int flag)
{
	u16 alarms;

	if (type != I2C_PROTOCOL_SMBUS_ALERT)
		return;

	if (lm90_is_tripped(client, &alarms)) {
		/*
		 * Disable ALERT# output, because these chips don't implement
		 * SMBus alert correctly; they should only hold the alert line
		 * low briefly.
		 */
		struct lm90_data *data = i2c_get_clientdata(client);

		if ((data->flags & LM90_HAVE_BROKEN_ALERT) &&
		    (alarms & data->alert_alarms)) {
			dev_dbg(&client->dev, "Disabling ALERT#\n");
			lm90_update_confreg(data, data->config | 0x80);
		}
	} else {
		dev_dbg(&client->dev, "Everything OK\n");
	}
}

static int __maybe_unused lm90_suspend(struct device *dev)
{
	struct lm90_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	if (client->irq)
		disable_irq(client->irq);

	return 0;
}

static int __maybe_unused lm90_resume(struct device *dev)
{
	struct lm90_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	if (client->irq)
		enable_irq(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(lm90_pm_ops, lm90_suspend, lm90_resume);

static struct i2c_driver lm90_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm90",
		.of_match_table = of_match_ptr(lm90_of_match),
		.pm	= &lm90_pm_ops,
	},
	.probe_new	= lm90_probe,
	.alert		= lm90_alert,
	.id_table	= lm90_id,
	.detect		= lm90_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm90_driver);

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("LM90/ADM1032 driver");
MODULE_LICENSE("GPL");
