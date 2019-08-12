// SPDX-License-Identifier: GPL-2.0+
/*
 * Device driver for monitoring ambient light intensity in (lux) and proximity
 * detection (prox) within the TAOS TSL2571, TSL2671, TMD2671, TSL2771, TMD2771,
 * TSL2572, TSL2672, TMD2672, TSL2772, and TMD2772 devices.
 *
 * Copyright (c) 2012, TAOS Corporation.
 * Copyright (c) 2017-2018 Brian Masney <masneyb@onstation.org>
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/platform_data/tsl2772.h>
#include <linux/regulator/consumer.h>

/* Cal defs */
#define PROX_STAT_CAL			0
#define PROX_STAT_SAMP			1
#define MAX_SAMPLES_CAL			200

/* TSL2772 Device ID */
#define TRITON_ID			0x00
#define SWORDFISH_ID			0x30
#define HALIBUT_ID			0x20

/* Lux calculation constants */
#define TSL2772_LUX_CALC_OVER_FLOW	65535

/*
 * TAOS Register definitions - Note: depending on device, some of these register
 * are not used and the register address is benign.
 */

/* Register offsets */
#define TSL2772_MAX_CONFIG_REG		16

/* Device Registers and Masks */
#define TSL2772_CNTRL			0x00
#define TSL2772_ALS_TIME		0X01
#define TSL2772_PRX_TIME		0x02
#define TSL2772_WAIT_TIME		0x03
#define TSL2772_ALS_MINTHRESHLO		0X04
#define TSL2772_ALS_MINTHRESHHI		0X05
#define TSL2772_ALS_MAXTHRESHLO		0X06
#define TSL2772_ALS_MAXTHRESHHI		0X07
#define TSL2772_PRX_MINTHRESHLO		0X08
#define TSL2772_PRX_MINTHRESHHI		0X09
#define TSL2772_PRX_MAXTHRESHLO		0X0A
#define TSL2772_PRX_MAXTHRESHHI		0X0B
#define TSL2772_PERSISTENCE		0x0C
#define TSL2772_ALS_PRX_CONFIG		0x0D
#define TSL2772_PRX_COUNT		0x0E
#define TSL2772_GAIN			0x0F
#define TSL2772_NOTUSED			0x10
#define TSL2772_REVID			0x11
#define TSL2772_CHIPID			0x12
#define TSL2772_STATUS			0x13
#define TSL2772_ALS_CHAN0LO		0x14
#define TSL2772_ALS_CHAN0HI		0x15
#define TSL2772_ALS_CHAN1LO		0x16
#define TSL2772_ALS_CHAN1HI		0x17
#define TSL2772_PRX_LO			0x18
#define TSL2772_PRX_HI			0x19

/* tsl2772 cmd reg masks */
#define TSL2772_CMD_REG			0x80
#define TSL2772_CMD_SPL_FN		0x60
#define TSL2772_CMD_REPEAT_PROTO	0x00
#define TSL2772_CMD_AUTOINC_PROTO	0x20

#define TSL2772_CMD_PROX_INT_CLR	0X05
#define TSL2772_CMD_ALS_INT_CLR		0x06
#define TSL2772_CMD_PROXALS_INT_CLR	0X07

/* tsl2772 cntrl reg masks */
#define TSL2772_CNTL_ADC_ENBL		0x02
#define TSL2772_CNTL_PWR_ON		0x01

/* tsl2772 status reg masks */
#define TSL2772_STA_ADC_VALID		0x01
#define TSL2772_STA_PRX_VALID		0x02
#define TSL2772_STA_ADC_PRX_VALID	(TSL2772_STA_ADC_VALID | \
					 TSL2772_STA_PRX_VALID)
#define TSL2772_STA_ALS_INTR		0x10
#define TSL2772_STA_PRX_INTR		0x20

/* tsl2772 cntrl reg masks */
#define TSL2772_CNTL_REG_CLEAR		0x00
#define TSL2772_CNTL_PROX_INT_ENBL	0X20
#define TSL2772_CNTL_ALS_INT_ENBL	0X10
#define TSL2772_CNTL_WAIT_TMR_ENBL	0X08
#define TSL2772_CNTL_PROX_DET_ENBL	0X04
#define TSL2772_CNTL_PWRON		0x01
#define TSL2772_CNTL_ALSPON_ENBL	0x03
#define TSL2772_CNTL_INTALSPON_ENBL	0x13
#define TSL2772_CNTL_PROXPON_ENBL	0x0F
#define TSL2772_CNTL_INTPROXPON_ENBL	0x2F

#define TSL2772_ALS_GAIN_TRIM_MIN	250
#define TSL2772_ALS_GAIN_TRIM_MAX	4000

#define TSL2772_MAX_PROX_LEDS		2

#define TSL2772_BOOT_MIN_SLEEP_TIME	10000
#define TSL2772_BOOT_MAX_SLEEP_TIME	28000

/* Device family members */
enum {
	tsl2571,
	tsl2671,
	tmd2671,
	tsl2771,
	tmd2771,
	tsl2572,
	tsl2672,
	tmd2672,
	tsl2772,
	tmd2772,
	apds9930,
};

enum {
	TSL2772_CHIP_UNKNOWN = 0,
	TSL2772_CHIP_WORKING = 1,
	TSL2772_CHIP_SUSPENDED = 2
};

enum {
	TSL2772_SUPPLY_VDD = 0,
	TSL2772_SUPPLY_VDDIO = 1,
	TSL2772_NUM_SUPPLIES = 2
};

/* Per-device data */
struct tsl2772_als_info {
	u16 als_ch0;
	u16 als_ch1;
	u16 lux;
};

struct tsl2772_chip_info {
	int chan_table_elements;
	struct iio_chan_spec channel_with_events[4];
	struct iio_chan_spec channel_without_events[4];
	const struct iio_info *info;
};

static const int tsl2772_led_currents[][2] = {
	{ 100000, TSL2772_100_mA },
	{  50000, TSL2772_50_mA },
	{  25000, TSL2772_25_mA },
	{  13000, TSL2772_13_mA },
	{      0, 0 }
};

struct tsl2772_chip {
	kernel_ulong_t id;
	struct mutex prox_mutex;
	struct mutex als_mutex;
	struct i2c_client *client;
	struct regulator_bulk_data supplies[TSL2772_NUM_SUPPLIES];
	u16 prox_data;
	struct tsl2772_als_info als_cur_info;
	struct tsl2772_settings settings;
	struct tsl2772_platform_data *pdata;
	int als_gain_time_scale;
	int als_saturation;
	int tsl2772_chip_status;
	u8 tsl2772_config[TSL2772_MAX_CONFIG_REG];
	const struct tsl2772_chip_info	*chip_info;
	const struct iio_info *info;
	s64 event_timestamp;
	/*
	 * This structure is intentionally large to accommodate
	 * updates via sysfs.
	 * Sized to 9 = max 8 segments + 1 termination segment
	 */
	struct tsl2772_lux tsl2772_device_lux[TSL2772_MAX_LUX_TABLE_SIZE];
};

/*
 * Different devices require different coefficents, and these numbers were
 * derived from the 'Lux Equation' section of the various device datasheets.
 * All of these coefficients assume a Glass Attenuation (GA) factor of 1.
 * The coefficients are multiplied by 1000 to avoid floating point operations.
 * The two rows in each table correspond to the Lux1 and Lux2 equations from
 * the datasheets.
 */
static const struct tsl2772_lux tsl2x71_lux_table[TSL2772_DEF_LUX_TABLE_SZ] = {
	{ 53000, 106000 },
	{ 31800,  53000 },
	{ 0,          0 },
};

static const struct tsl2772_lux tmd2x71_lux_table[TSL2772_DEF_LUX_TABLE_SZ] = {
	{ 24000,  48000 },
	{ 14400,  24000 },
	{ 0,          0 },
};

static const struct tsl2772_lux tsl2x72_lux_table[TSL2772_DEF_LUX_TABLE_SZ] = {
	{ 60000, 112200 },
	{ 37800,  60000 },
	{     0,      0 },
};

static const struct tsl2772_lux tmd2x72_lux_table[TSL2772_DEF_LUX_TABLE_SZ] = {
	{ 20000,  35000 },
	{ 12600,  20000 },
	{     0,      0 },
};

static const struct tsl2772_lux apds9930_lux_table[TSL2772_DEF_LUX_TABLE_SZ] = {
	{ 52000,  96824 },
	{ 38792,  67132 },
	{     0,      0 },
};

static const struct tsl2772_lux *tsl2772_default_lux_table_group[] = {
	[tsl2571] = tsl2x71_lux_table,
	[tsl2671] = tsl2x71_lux_table,
	[tmd2671] = tmd2x71_lux_table,
	[tsl2771] = tsl2x71_lux_table,
	[tmd2771] = tmd2x71_lux_table,
	[tsl2572] = tsl2x72_lux_table,
	[tsl2672] = tsl2x72_lux_table,
	[tmd2672] = tmd2x72_lux_table,
	[tsl2772] = tsl2x72_lux_table,
	[tmd2772] = tmd2x72_lux_table,
	[apds9930] = apds9930_lux_table,
};

static const struct tsl2772_settings tsl2772_default_settings = {
	.als_time = 255, /* 2.72 / 2.73 ms */
	.als_gain = 0,
	.prox_time = 255, /* 2.72 / 2.73 ms */
	.prox_gain = 0,
	.wait_time = 255,
	.als_prox_config = 0,
	.als_gain_trim = 1000,
	.als_cal_target = 150,
	.als_persistence = 1,
	.als_interrupt_en = false,
	.als_thresh_low = 200,
	.als_thresh_high = 256,
	.prox_persistence = 1,
	.prox_interrupt_en = false,
	.prox_thres_low  = 0,
	.prox_thres_high = 512,
	.prox_max_samples_cal = 30,
	.prox_pulse_count = 8,
	.prox_diode = TSL2772_DIODE1,
	.prox_power = TSL2772_100_mA
};

static const s16 tsl2772_als_gain[] = {
	1,
	8,
	16,
	120
};

static const s16 tsl2772_prox_gain[] = {
	1,
	2,
	4,
	8
};

static const int tsl2772_int_time_avail[][6] = {
	[tsl2571] = { 0, 2720, 0, 2720, 0, 696000 },
	[tsl2671] = { 0, 2720, 0, 2720, 0, 696000 },
	[tmd2671] = { 0, 2720, 0, 2720, 0, 696000 },
	[tsl2771] = { 0, 2720, 0, 2720, 0, 696000 },
	[tmd2771] = { 0, 2720, 0, 2720, 0, 696000 },
	[tsl2572] = { 0, 2730, 0, 2730, 0, 699000 },
	[tsl2672] = { 0, 2730, 0, 2730, 0, 699000 },
	[tmd2672] = { 0, 2730, 0, 2730, 0, 699000 },
	[tsl2772] = { 0, 2730, 0, 2730, 0, 699000 },
	[tmd2772] = { 0, 2730, 0, 2730, 0, 699000 },
	[apds9930] = { 0, 2730, 0, 2730, 0, 699000 },
};

static int tsl2772_int_calibscale_avail[] = { 1, 8, 16, 120 };

static int tsl2772_prox_calibscale_avail[] = { 1, 2, 4, 8 };

/* Channel variations */
enum {
	ALS,
	PRX,
	ALSPRX,
	PRX2,
	ALSPRX2,
};

static const u8 device_channel_config[] = {
	[tsl2571] = ALS,
	[tsl2671] = PRX,
	[tmd2671] = PRX,
	[tsl2771] = ALSPRX,
	[tmd2771] = ALSPRX,
	[tsl2572] = ALS,
	[tsl2672] = PRX2,
	[tmd2672] = PRX2,
	[tsl2772] = ALSPRX2,
	[tmd2772] = ALSPRX2,
	[apds9930] = ALSPRX2,
};

static int tsl2772_read_status(struct tsl2772_chip *chip)
{
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2772_CMD_REG | TSL2772_STATUS);
	if (ret < 0)
		dev_err(&chip->client->dev,
			"%s: failed to read STATUS register: %d\n", __func__,
			ret);

	return ret;
}

static int tsl2772_write_control_reg(struct tsl2772_chip *chip, u8 data)
{
	int ret;

	ret = i2c_smbus_write_byte_data(chip->client,
					TSL2772_CMD_REG | TSL2772_CNTRL, data);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to write to control register %x: %d\n",
			__func__, data, ret);
	}

	return ret;
}

static int tsl2772_read_autoinc_regs(struct tsl2772_chip *chip, int lower_reg,
				     int upper_reg)
{
	u8 buf[2];
	int ret;

	ret = i2c_smbus_write_byte(chip->client,
				   TSL2772_CMD_REG | TSL2772_CMD_AUTOINC_PROTO |
				   lower_reg);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to enable auto increment protocol: %d\n",
			__func__, ret);
		return ret;
	}

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2772_CMD_REG | lower_reg);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to read from register %x: %d\n", __func__,
			lower_reg, ret);
		return ret;
	}
	buf[0] = ret;

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2772_CMD_REG | upper_reg);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to read from register %x: %d\n", __func__,
			upper_reg, ret);
		return ret;
	}
	buf[1] = ret;

	ret = i2c_smbus_write_byte(chip->client,
				   TSL2772_CMD_REG | TSL2772_CMD_REPEAT_PROTO |
				   lower_reg);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to enable repeated byte protocol: %d\n",
			__func__, ret);
		return ret;
	}

	return le16_to_cpup((const __le16 *)&buf[0]);
}

/**
 * tsl2772_get_lux() - Reads and calculates current lux value.
 * @indio_dev:	pointer to IIO device
 *
 * The raw ch0 and ch1 values of the ambient light sensed in the last
 * integration cycle are read from the device. The raw values are multiplied
 * by a device-specific scale factor, and divided by the integration time and
 * device gain. The code supports multiple lux equations through the lux table
 * coefficients. A lux gain trim is applied to each lux equation, and then the
 * maximum lux within the interval 0..65535 is selected.
 */
static int tsl2772_get_lux(struct iio_dev *indio_dev)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	struct tsl2772_lux *p;
	int max_lux, ret;
	bool overflow;

	mutex_lock(&chip->als_mutex);

	if (chip->tsl2772_chip_status != TSL2772_CHIP_WORKING) {
		dev_err(&chip->client->dev, "%s: device is not enabled\n",
			__func__);
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = tsl2772_read_status(chip);
	if (ret < 0)
		goto out_unlock;

	if (!(ret & TSL2772_STA_ADC_VALID)) {
		dev_err(&chip->client->dev,
			"%s: data not valid yet\n", __func__);
		ret = chip->als_cur_info.lux; /* return LAST VALUE */
		goto out_unlock;
	}

	ret = tsl2772_read_autoinc_regs(chip, TSL2772_ALS_CHAN0LO,
					TSL2772_ALS_CHAN0HI);
	if (ret < 0)
		goto out_unlock;
	chip->als_cur_info.als_ch0 = ret;

	ret = tsl2772_read_autoinc_regs(chip, TSL2772_ALS_CHAN1LO,
					TSL2772_ALS_CHAN1HI);
	if (ret < 0)
		goto out_unlock;
	chip->als_cur_info.als_ch1 = ret;

	if (chip->als_cur_info.als_ch0 >= chip->als_saturation) {
		max_lux = TSL2772_LUX_CALC_OVER_FLOW;
		goto update_struct_with_max_lux;
	}

	if (!chip->als_cur_info.als_ch0) {
		/* have no data, so return LAST VALUE */
		ret = chip->als_cur_info.lux;
		goto out_unlock;
	}

	max_lux = 0;
	overflow = false;
	for (p = (struct tsl2772_lux *)chip->tsl2772_device_lux; p->ch0 != 0;
	     p++) {
		int lux;

		lux = ((chip->als_cur_info.als_ch0 * p->ch0) -
		       (chip->als_cur_info.als_ch1 * p->ch1)) /
			chip->als_gain_time_scale;

		/*
		 * The als_gain_trim can have a value within the range 250..4000
		 * and is a multiplier for the lux. A trim of 1000 makes no
		 * changes to the lux, less than 1000 scales it down, and
		 * greater than 1000 scales it up.
		 */
		lux = (lux * chip->settings.als_gain_trim) / 1000;

		if (lux > TSL2772_LUX_CALC_OVER_FLOW) {
			overflow = true;
			continue;
		}

		max_lux = max(max_lux, lux);
	}

	if (overflow && max_lux == 0)
		max_lux = TSL2772_LUX_CALC_OVER_FLOW;

update_struct_with_max_lux:
	chip->als_cur_info.lux = max_lux;
	ret = max_lux;

out_unlock:
	mutex_unlock(&chip->als_mutex);

	return ret;
}

/**
 * tsl2772_get_prox() - Reads proximity data registers and updates
 *                      chip->prox_data.
 *
 * @indio_dev:	pointer to IIO device
 */
static int tsl2772_get_prox(struct iio_dev *indio_dev)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	int ret;

	mutex_lock(&chip->prox_mutex);

	ret = tsl2772_read_status(chip);
	if (ret < 0)
		goto prox_poll_err;

	switch (chip->id) {
	case tsl2571:
	case tsl2671:
	case tmd2671:
	case tsl2771:
	case tmd2771:
		if (!(ret & TSL2772_STA_ADC_VALID)) {
			ret = -EINVAL;
			goto prox_poll_err;
		}
		break;
	case tsl2572:
	case tsl2672:
	case tmd2672:
	case tsl2772:
	case tmd2772:
	case apds9930:
		if (!(ret & TSL2772_STA_PRX_VALID)) {
			ret = -EINVAL;
			goto prox_poll_err;
		}
		break;
	}

	ret = tsl2772_read_autoinc_regs(chip, TSL2772_PRX_LO, TSL2772_PRX_HI);
	if (ret < 0)
		goto prox_poll_err;
	chip->prox_data = ret;

prox_poll_err:
	mutex_unlock(&chip->prox_mutex);

	return ret;
}

static int tsl2772_read_prox_led_current(struct tsl2772_chip *chip)
{
	struct device_node *of_node = chip->client->dev.of_node;
	int ret, tmp, i;

	ret = of_property_read_u32(of_node, "led-max-microamp", &tmp);
	if (ret < 0)
		return ret;

	for (i = 0; tsl2772_led_currents[i][0] != 0; i++) {
		if (tmp == tsl2772_led_currents[i][0]) {
			chip->settings.prox_power = tsl2772_led_currents[i][1];
			return 0;
		}
	}

	dev_err(&chip->client->dev, "Invalid value %d for led-max-microamp\n",
		tmp);

	return -EINVAL;

}

static int tsl2772_read_prox_diodes(struct tsl2772_chip *chip)
{
	struct device_node *of_node = chip->client->dev.of_node;
	int i, ret, num_leds, prox_diode_mask;
	u32 leds[TSL2772_MAX_PROX_LEDS];

	ret = of_property_count_u32_elems(of_node, "amstaos,proximity-diodes");
	if (ret < 0)
		return ret;

	num_leds = ret;
	if (num_leds > TSL2772_MAX_PROX_LEDS)
		num_leds = TSL2772_MAX_PROX_LEDS;

	ret = of_property_read_u32_array(of_node, "amstaos,proximity-diodes",
					 leds, num_leds);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"Invalid value for amstaos,proximity-diodes: %d.\n",
			ret);
		return ret;
	}

	prox_diode_mask = 0;
	for (i = 0; i < num_leds; i++) {
		if (leds[i] == 0)
			prox_diode_mask |= TSL2772_DIODE0;
		else if (leds[i] == 1)
			prox_diode_mask |= TSL2772_DIODE1;
		else {
			dev_err(&chip->client->dev,
				"Invalid value %d in amstaos,proximity-diodes.\n",
				leds[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static void tsl2772_parse_dt(struct tsl2772_chip *chip)
{
	tsl2772_read_prox_led_current(chip);
	tsl2772_read_prox_diodes(chip);
}

/**
 * tsl2772_defaults() - Populates the device nominal operating parameters
 *                      with those provided by a 'platform' data struct or
 *                      with prefined defaults.
 *
 * @chip:               pointer to device structure.
 */
static void tsl2772_defaults(struct tsl2772_chip *chip)
{
	/* If Operational settings defined elsewhere.. */
	if (chip->pdata && chip->pdata->platform_default_settings)
		memcpy(&chip->settings, chip->pdata->platform_default_settings,
		       sizeof(tsl2772_default_settings));
	else
		memcpy(&chip->settings, &tsl2772_default_settings,
		       sizeof(tsl2772_default_settings));

	/* Load up the proper lux table. */
	if (chip->pdata && chip->pdata->platform_lux_table[0].ch0 != 0)
		memcpy(chip->tsl2772_device_lux,
		       chip->pdata->platform_lux_table,
		       sizeof(chip->pdata->platform_lux_table));
	else
		memcpy(chip->tsl2772_device_lux,
		       tsl2772_default_lux_table_group[chip->id],
		       TSL2772_DEFAULT_TABLE_BYTES);

	tsl2772_parse_dt(chip);
}

/**
 * tsl2772_als_calibrate() -	Obtain single reading and calculate
 *                              the als_gain_trim.
 *
 * @indio_dev:	pointer to IIO device
 */
static int tsl2772_als_calibrate(struct iio_dev *indio_dev)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	int ret, lux_val;

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2772_CMD_REG | TSL2772_CNTRL);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to read from the CNTRL register\n",
			__func__);
		return ret;
	}

	if ((ret & (TSL2772_CNTL_ADC_ENBL | TSL2772_CNTL_PWR_ON))
			!= (TSL2772_CNTL_ADC_ENBL | TSL2772_CNTL_PWR_ON)) {
		dev_err(&chip->client->dev,
			"%s: Device is not powered on and/or ADC is not enabled\n",
			__func__);
		return -EINVAL;
	} else if ((ret & TSL2772_STA_ADC_VALID) != TSL2772_STA_ADC_VALID) {
		dev_err(&chip->client->dev,
			"%s: The two ADC channels have not completed an integration cycle\n",
			__func__);
		return -ENODATA;
	}

	lux_val = tsl2772_get_lux(indio_dev);
	if (lux_val < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to get lux\n", __func__);
		return lux_val;
	}
	if (lux_val == 0)
		return -ERANGE;

	ret = (chip->settings.als_cal_target * chip->settings.als_gain_trim) /
			lux_val;
	if (ret < TSL2772_ALS_GAIN_TRIM_MIN || ret > TSL2772_ALS_GAIN_TRIM_MAX)
		return -ERANGE;

	chip->settings.als_gain_trim = ret;

	return ret;
}

static void tsl2772_disable_regulators_action(void *_data)
{
	struct tsl2772_chip *chip = _data;

	regulator_bulk_disable(ARRAY_SIZE(chip->supplies), chip->supplies);
}

static int tsl2772_chip_on(struct iio_dev *indio_dev)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	int ret, i, als_count, als_time_us;
	u8 *dev_reg, reg_val;

	/* Non calculated parameters */
	chip->tsl2772_config[TSL2772_ALS_TIME] = chip->settings.als_time;
	chip->tsl2772_config[TSL2772_PRX_TIME] = chip->settings.prox_time;
	chip->tsl2772_config[TSL2772_WAIT_TIME] = chip->settings.wait_time;
	chip->tsl2772_config[TSL2772_ALS_PRX_CONFIG] =
		chip->settings.als_prox_config;

	chip->tsl2772_config[TSL2772_ALS_MINTHRESHLO] =
		(chip->settings.als_thresh_low) & 0xFF;
	chip->tsl2772_config[TSL2772_ALS_MINTHRESHHI] =
		(chip->settings.als_thresh_low >> 8) & 0xFF;
	chip->tsl2772_config[TSL2772_ALS_MAXTHRESHLO] =
		(chip->settings.als_thresh_high) & 0xFF;
	chip->tsl2772_config[TSL2772_ALS_MAXTHRESHHI] =
		(chip->settings.als_thresh_high >> 8) & 0xFF;
	chip->tsl2772_config[TSL2772_PERSISTENCE] =
		(chip->settings.prox_persistence & 0xFF) << 4 |
		(chip->settings.als_persistence & 0xFF);

	chip->tsl2772_config[TSL2772_PRX_COUNT] =
			chip->settings.prox_pulse_count;
	chip->tsl2772_config[TSL2772_PRX_MINTHRESHLO] =
			(chip->settings.prox_thres_low) & 0xFF;
	chip->tsl2772_config[TSL2772_PRX_MINTHRESHHI] =
			(chip->settings.prox_thres_low >> 8) & 0xFF;
	chip->tsl2772_config[TSL2772_PRX_MAXTHRESHLO] =
			(chip->settings.prox_thres_high) & 0xFF;
	chip->tsl2772_config[TSL2772_PRX_MAXTHRESHHI] =
			(chip->settings.prox_thres_high >> 8) & 0xFF;

	/* and make sure we're not already on */
	if (chip->tsl2772_chip_status == TSL2772_CHIP_WORKING) {
		/* if forcing a register update - turn off, then on */
		dev_info(&chip->client->dev, "device is already enabled\n");
		return -EINVAL;
	}

	/* Set the gain based on tsl2772_settings struct */
	chip->tsl2772_config[TSL2772_GAIN] =
		(chip->settings.als_gain & 0xFF) |
		((chip->settings.prox_gain & 0xFF) << 2) |
		(chip->settings.prox_diode << 4) |
		(chip->settings.prox_power << 6);

	/* set chip time scaling and saturation */
	als_count = 256 - chip->settings.als_time;
	als_time_us = als_count * tsl2772_int_time_avail[chip->id][3];
	chip->als_saturation = als_count * 768; /* 75% of full scale */
	chip->als_gain_time_scale = als_time_us *
		tsl2772_als_gain[chip->settings.als_gain];

	/*
	 * TSL2772 Specific power-on / adc enable sequence
	 * Power on the device 1st.
	 */
	ret = tsl2772_write_control_reg(chip, TSL2772_CNTL_PWR_ON);
	if (ret < 0)
		return ret;

	/*
	 * Use the following shadow copy for our delay before enabling ADC.
	 * Write all the registers.
	 */
	for (i = 0, dev_reg = chip->tsl2772_config;
			i < TSL2772_MAX_CONFIG_REG; i++) {
		int reg = TSL2772_CMD_REG + i;

		ret = i2c_smbus_write_byte_data(chip->client, reg,
						*dev_reg++);
		if (ret < 0) {
			dev_err(&chip->client->dev,
				"%s: failed to write to register %x: %d\n",
				__func__, reg, ret);
			return ret;
		}
	}

	/* Power-on settling time */
	usleep_range(3000, 3500);

	reg_val = TSL2772_CNTL_PWR_ON | TSL2772_CNTL_ADC_ENBL |
		  TSL2772_CNTL_PROX_DET_ENBL;
	if (chip->settings.als_interrupt_en)
		reg_val |= TSL2772_CNTL_ALS_INT_ENBL;
	if (chip->settings.prox_interrupt_en)
		reg_val |= TSL2772_CNTL_PROX_INT_ENBL;

	ret = tsl2772_write_control_reg(chip, reg_val);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte(chip->client,
				   TSL2772_CMD_REG | TSL2772_CMD_SPL_FN |
				   TSL2772_CMD_PROXALS_INT_CLR);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to clear interrupt status: %d\n",
			__func__, ret);
		return ret;
	}

	chip->tsl2772_chip_status = TSL2772_CHIP_WORKING;

	return ret;
}

static int tsl2772_chip_off(struct iio_dev *indio_dev)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);

	/* turn device off */
	chip->tsl2772_chip_status = TSL2772_CHIP_SUSPENDED;
	return tsl2772_write_control_reg(chip, 0x00);
}

static void tsl2772_chip_off_action(void *data)
{
	struct iio_dev *indio_dev = data;

	tsl2772_chip_off(indio_dev);
}

/**
 * tsl2772_invoke_change - power cycle the device to implement the user
 *                         parameters
 * @indio_dev:	pointer to IIO device
 *
 * Obtain and lock both ALS and PROX resources, determine and save device state
 * (On/Off), cycle device to implement updated parameter, put device back into
 * proper state, and unlock resource.
 */
static int tsl2772_invoke_change(struct iio_dev *indio_dev)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	int device_status = chip->tsl2772_chip_status;
	int ret;

	mutex_lock(&chip->als_mutex);
	mutex_lock(&chip->prox_mutex);

	if (device_status == TSL2772_CHIP_WORKING) {
		ret = tsl2772_chip_off(indio_dev);
		if (ret < 0)
			goto unlock;
	}

	ret = tsl2772_chip_on(indio_dev);

unlock:
	mutex_unlock(&chip->prox_mutex);
	mutex_unlock(&chip->als_mutex);

	return ret;
}

static int tsl2772_prox_cal(struct iio_dev *indio_dev)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	int prox_history[MAX_SAMPLES_CAL + 1];
	int i, ret, mean, max, sample_sum;

	if (chip->settings.prox_max_samples_cal < 1 ||
	    chip->settings.prox_max_samples_cal > MAX_SAMPLES_CAL)
		return -EINVAL;

	for (i = 0; i < chip->settings.prox_max_samples_cal; i++) {
		usleep_range(15000, 17500);
		ret = tsl2772_get_prox(indio_dev);
		if (ret < 0)
			return ret;

		prox_history[i] = chip->prox_data;
	}

	sample_sum = 0;
	max = INT_MIN;
	for (i = 0; i < chip->settings.prox_max_samples_cal; i++) {
		sample_sum += prox_history[i];
		max = max(max, prox_history[i]);
	}
	mean = sample_sum / chip->settings.prox_max_samples_cal;

	chip->settings.prox_thres_high = (max << 1) - mean;

	return tsl2772_invoke_change(indio_dev);
}

static int tsl2772_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_INTENSITY) {
			*length = ARRAY_SIZE(tsl2772_int_calibscale_avail);
			*vals = tsl2772_int_calibscale_avail;
		} else {
			*length = ARRAY_SIZE(tsl2772_prox_calibscale_avail);
			*vals = tsl2772_prox_calibscale_avail;
		}
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_INT_TIME:
		*length = ARRAY_SIZE(tsl2772_int_time_avail[chip->id]);
		*vals = tsl2772_int_time_avail[chip->id];
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_RANGE;
	}

	return -EINVAL;
}

static ssize_t in_illuminance0_target_input_show(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct tsl2772_chip *chip = iio_priv(dev_to_iio_dev(dev));

	return snprintf(buf, PAGE_SIZE, "%d\n", chip->settings.als_cal_target);
}

static ssize_t in_illuminance0_target_input_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	u16 value;
	int ret;

	if (kstrtou16(buf, 0, &value))
		return -EINVAL;

	chip->settings.als_cal_target = value;
	ret = tsl2772_invoke_change(indio_dev);
	if (ret < 0)
		return ret;

	return len;
}

static ssize_t in_illuminance0_calibrate_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool value;
	int ret;

	if (kstrtobool(buf, &value) || !value)
		return -EINVAL;

	ret = tsl2772_als_calibrate(indio_dev);
	if (ret < 0)
		return ret;

	ret = tsl2772_invoke_change(indio_dev);
	if (ret < 0)
		return ret;

	return len;
}

static ssize_t in_illuminance0_lux_table_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct tsl2772_chip *chip = iio_priv(dev_to_iio_dev(dev));
	int i = 0;
	int offset = 0;

	while (i < TSL2772_MAX_LUX_TABLE_SIZE) {
		offset += snprintf(buf + offset, PAGE_SIZE, "%u,%u,",
			chip->tsl2772_device_lux[i].ch0,
			chip->tsl2772_device_lux[i].ch1);
		if (chip->tsl2772_device_lux[i].ch0 == 0) {
			/*
			 * We just printed the first "0" entry.
			 * Now get rid of the extra "," and break.
			 */
			offset--;
			break;
		}
		i++;
	}

	offset += snprintf(buf + offset, PAGE_SIZE, "\n");
	return offset;
}

static ssize_t in_illuminance0_lux_table_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	int value[ARRAY_SIZE(chip->tsl2772_device_lux) * 2 + 1];
	int n, ret;

	get_options(buf, ARRAY_SIZE(value), value);

	/*
	 * We now have an array of ints starting at value[1], and
	 * enumerated by value[0].
	 * We expect each group of two ints to be one table entry,
	 * and the last table entry is all 0.
	 */
	n = value[0];
	if ((n % 2) || n < 4 ||
	    n > ((ARRAY_SIZE(chip->tsl2772_device_lux) - 1) * 2))
		return -EINVAL;

	if ((value[(n - 1)] | value[n]) != 0)
		return -EINVAL;

	if (chip->tsl2772_chip_status == TSL2772_CHIP_WORKING) {
		ret = tsl2772_chip_off(indio_dev);
		if (ret < 0)
			return ret;
	}

	/* Zero out the table */
	memset(chip->tsl2772_device_lux, 0, sizeof(chip->tsl2772_device_lux));
	memcpy(chip->tsl2772_device_lux, &value[1], (value[0] * 4));

	ret = tsl2772_invoke_change(indio_dev);
	if (ret < 0)
		return ret;

	return len;
}

static ssize_t in_proximity0_calibrate_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool value;
	int ret;

	if (kstrtobool(buf, &value) || !value)
		return -EINVAL;

	ret = tsl2772_prox_cal(indio_dev);
	if (ret < 0)
		return ret;

	ret = tsl2772_invoke_change(indio_dev);
	if (ret < 0)
		return ret;

	return len;
}

static int tsl2772_read_interrupt_config(struct iio_dev *indio_dev,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);

	if (chan->type == IIO_INTENSITY)
		return chip->settings.als_interrupt_en;
	else
		return chip->settings.prox_interrupt_en;
}

static int tsl2772_write_interrupt_config(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir,
					  int val)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);

	if (chan->type == IIO_INTENSITY)
		chip->settings.als_interrupt_en = val ? true : false;
	else
		chip->settings.prox_interrupt_en = val ? true : false;

	return tsl2772_invoke_change(indio_dev);
}

static int tsl2772_write_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info,
				     int val, int val2)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	int ret = -EINVAL, count, persistence;
	u8 time;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (chan->type == IIO_INTENSITY) {
			switch (dir) {
			case IIO_EV_DIR_RISING:
				chip->settings.als_thresh_high = val;
				ret = 0;
				break;
			case IIO_EV_DIR_FALLING:
				chip->settings.als_thresh_low = val;
				ret = 0;
				break;
			default:
				break;
			}
		} else {
			switch (dir) {
			case IIO_EV_DIR_RISING:
				chip->settings.prox_thres_high = val;
				ret = 0;
				break;
			case IIO_EV_DIR_FALLING:
				chip->settings.prox_thres_low = val;
				ret = 0;
				break;
			default:
				break;
			}
		}
		break;
	case IIO_EV_INFO_PERIOD:
		if (chan->type == IIO_INTENSITY)
			time = chip->settings.als_time;
		else
			time = chip->settings.prox_time;

		count = 256 - time;
		persistence = ((val * 1000000) + val2) /
			(count * tsl2772_int_time_avail[chip->id][3]);

		if (chan->type == IIO_INTENSITY) {
			/* ALS filter values are 1, 2, 3, 5, 10, 15, ..., 60 */
			if (persistence > 3)
				persistence = (persistence / 5) + 3;

			chip->settings.als_persistence = persistence;
		} else {
			chip->settings.prox_persistence = persistence;
		}

		ret = 0;
		break;
	default:
		break;
	}

	if (ret < 0)
		return ret;

	return tsl2772_invoke_change(indio_dev);
}

static int tsl2772_read_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int *val, int *val2)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	int filter_delay, persistence;
	u8 time;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (chan->type == IIO_INTENSITY) {
			switch (dir) {
			case IIO_EV_DIR_RISING:
				*val = chip->settings.als_thresh_high;
				return IIO_VAL_INT;
			case IIO_EV_DIR_FALLING:
				*val = chip->settings.als_thresh_low;
				return IIO_VAL_INT;
			default:
				return -EINVAL;
			}
		} else {
			switch (dir) {
			case IIO_EV_DIR_RISING:
				*val = chip->settings.prox_thres_high;
				return IIO_VAL_INT;
			case IIO_EV_DIR_FALLING:
				*val = chip->settings.prox_thres_low;
				return IIO_VAL_INT;
			default:
				return -EINVAL;
			}
		}
		break;
	case IIO_EV_INFO_PERIOD:
		if (chan->type == IIO_INTENSITY) {
			time = chip->settings.als_time;
			persistence = chip->settings.als_persistence;

			/* ALS filter values are 1, 2, 3, 5, 10, 15, ..., 60 */
			if (persistence > 3)
				persistence = (persistence - 3) * 5;
		} else {
			time = chip->settings.prox_time;
			persistence = chip->settings.prox_persistence;
		}

		filter_delay = persistence * (256 - time) *
			tsl2772_int_time_avail[chip->id][3];

		*val = filter_delay / 1000000;
		*val2 = filter_delay % 1000000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int tsl2772_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val,
			    int *val2,
			    long mask)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_LIGHT:
			tsl2772_get_lux(indio_dev);
			*val = chip->als_cur_info.lux;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_INTENSITY:
			tsl2772_get_lux(indio_dev);
			if (chan->channel == 0)
				*val = chip->als_cur_info.als_ch0;
			else
				*val = chip->als_cur_info.als_ch1;
			return IIO_VAL_INT;
		case IIO_PROXIMITY:
			tsl2772_get_prox(indio_dev);
			*val = chip->prox_data;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_LIGHT)
			*val = tsl2772_als_gain[chip->settings.als_gain];
		else
			*val = tsl2772_prox_gain[chip->settings.prox_gain];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		*val = chip->settings.als_gain_trim;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = (256 - chip->settings.als_time) *
			tsl2772_int_time_avail[chip->id][3];
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int tsl2772_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val,
			     int val2,
			     long mask)
{
	struct tsl2772_chip *chip = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_INTENSITY) {
			switch (val) {
			case 1:
				chip->settings.als_gain = 0;
				break;
			case 8:
				chip->settings.als_gain = 1;
				break;
			case 16:
				chip->settings.als_gain = 2;
				break;
			case 120:
				chip->settings.als_gain = 3;
				break;
			default:
				return -EINVAL;
			}
		} else {
			switch (val) {
			case 1:
				chip->settings.prox_gain = 0;
				break;
			case 2:
				chip->settings.prox_gain = 1;
				break;
			case 4:
				chip->settings.prox_gain = 2;
				break;
			case 8:
				chip->settings.prox_gain = 3;
				break;
			default:
				return -EINVAL;
			}
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		if (val < TSL2772_ALS_GAIN_TRIM_MIN ||
		    val > TSL2772_ALS_GAIN_TRIM_MAX)
			return -EINVAL;

		chip->settings.als_gain_trim = val;
		break;
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0 || val2 < tsl2772_int_time_avail[chip->id][1] ||
		    val2 > tsl2772_int_time_avail[chip->id][5])
			return -EINVAL;

		chip->settings.als_time = 256 -
			(val2 / tsl2772_int_time_avail[chip->id][3]);
		break;
	default:
		return -EINVAL;
	}

	return tsl2772_invoke_change(indio_dev);
}

static DEVICE_ATTR_RW(in_illuminance0_target_input);

static DEVICE_ATTR_WO(in_illuminance0_calibrate);

static DEVICE_ATTR_WO(in_proximity0_calibrate);

static DEVICE_ATTR_RW(in_illuminance0_lux_table);

/* Use the default register values to identify the Taos device */
static int tsl2772_device_id_verif(int id, int target)
{
	switch (target) {
	case tsl2571:
	case tsl2671:
	case tsl2771:
		return (id & 0xf0) == TRITON_ID;
	case tmd2671:
	case tmd2771:
		return (id & 0xf0) == HALIBUT_ID;
	case tsl2572:
	case tsl2672:
	case tmd2672:
	case tsl2772:
	case tmd2772:
	case apds9930:
		return (id & 0xf0) == SWORDFISH_ID;
	}

	return -EINVAL;
}

static irqreturn_t tsl2772_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns(indio_dev);
	int ret;

	ret = tsl2772_read_status(chip);
	if (ret < 0)
		return IRQ_HANDLED;

	/* What type of interrupt do we need to process */
	if (ret & TSL2772_STA_PRX_INTR) {
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       timestamp);
	}

	if (ret & TSL2772_STA_ALS_INTR) {
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_LIGHT,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       timestamp);
	}

	ret = i2c_smbus_write_byte(chip->client,
				   TSL2772_CMD_REG | TSL2772_CMD_SPL_FN |
				   TSL2772_CMD_PROXALS_INT_CLR);
	if (ret < 0)
		dev_err(&chip->client->dev,
			"%s: failed to clear interrupt status: %d\n",
			__func__, ret);

	return IRQ_HANDLED;
}

static struct attribute *tsl2772_ALS_device_attrs[] = {
	&dev_attr_in_illuminance0_target_input.attr,
	&dev_attr_in_illuminance0_calibrate.attr,
	&dev_attr_in_illuminance0_lux_table.attr,
	NULL
};

static struct attribute *tsl2772_PRX_device_attrs[] = {
	&dev_attr_in_proximity0_calibrate.attr,
	NULL
};

static struct attribute *tsl2772_ALSPRX_device_attrs[] = {
	&dev_attr_in_illuminance0_target_input.attr,
	&dev_attr_in_illuminance0_calibrate.attr,
	&dev_attr_in_illuminance0_lux_table.attr,
	NULL
};

static struct attribute *tsl2772_PRX2_device_attrs[] = {
	&dev_attr_in_proximity0_calibrate.attr,
	NULL
};

static struct attribute *tsl2772_ALSPRX2_device_attrs[] = {
	&dev_attr_in_illuminance0_target_input.attr,
	&dev_attr_in_illuminance0_calibrate.attr,
	&dev_attr_in_illuminance0_lux_table.attr,
	&dev_attr_in_proximity0_calibrate.attr,
	NULL
};

static const struct attribute_group tsl2772_device_attr_group_tbl[] = {
	[ALS] = {
		.attrs = tsl2772_ALS_device_attrs,
	},
	[PRX] = {
		.attrs = tsl2772_PRX_device_attrs,
	},
	[ALSPRX] = {
		.attrs = tsl2772_ALSPRX_device_attrs,
	},
	[PRX2] = {
		.attrs = tsl2772_PRX2_device_attrs,
	},
	[ALSPRX2] = {
		.attrs = tsl2772_ALSPRX2_device_attrs,
	},
};

#define TSL2772_DEVICE_INFO(type)[type] = \
	{ \
		.attrs = &tsl2772_device_attr_group_tbl[type], \
		.read_raw = &tsl2772_read_raw, \
		.read_avail = &tsl2772_read_avail, \
		.write_raw = &tsl2772_write_raw, \
		.read_event_value = &tsl2772_read_event_value, \
		.write_event_value = &tsl2772_write_event_value, \
		.read_event_config = &tsl2772_read_interrupt_config, \
		.write_event_config = &tsl2772_write_interrupt_config, \
	}

static const struct iio_info tsl2772_device_info[] = {
	TSL2772_DEVICE_INFO(ALS),
	TSL2772_DEVICE_INFO(PRX),
	TSL2772_DEVICE_INFO(ALSPRX),
	TSL2772_DEVICE_INFO(PRX2),
	TSL2772_DEVICE_INFO(ALSPRX2),
};

static const struct iio_event_spec tsl2772_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_PERIOD) |
			BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct tsl2772_chip_info tsl2772_chip_info_tbl[] = {
	[ALS] = {
		.channel_with_events = {
			{
			.type = IIO_LIGHT,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE) |
				BIT(IIO_CHAN_INFO_CALIBBIAS),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.event_spec = tsl2772_events,
			.num_event_specs = ARRAY_SIZE(tsl2772_events),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 1,
			},
		},
		.channel_without_events = {
			{
			.type = IIO_LIGHT,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE) |
				BIT(IIO_CHAN_INFO_CALIBBIAS),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 1,
			},
		},
		.chan_table_elements = 3,
		.info = &tsl2772_device_info[ALS],
	},
	[PRX] = {
		.channel_with_events = {
			{
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			.event_spec = tsl2772_events,
			.num_event_specs = ARRAY_SIZE(tsl2772_events),
			},
		},
		.channel_without_events = {
			{
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			},
		},
		.chan_table_elements = 1,
		.info = &tsl2772_device_info[PRX],
	},
	[ALSPRX] = {
		.channel_with_events = {
			{
			.type = IIO_LIGHT,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE) |
				BIT(IIO_CHAN_INFO_CALIBBIAS),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.event_spec = tsl2772_events,
			.num_event_specs = ARRAY_SIZE(tsl2772_events),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 1,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			}, {
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			.event_spec = tsl2772_events,
			.num_event_specs = ARRAY_SIZE(tsl2772_events),
			},
		},
		.channel_without_events = {
			{
			.type = IIO_LIGHT,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE) |
				BIT(IIO_CHAN_INFO_CALIBBIAS),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 1,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			}, {
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			},
		},
		.chan_table_elements = 4,
		.info = &tsl2772_device_info[ALSPRX],
	},
	[PRX2] = {
		.channel_with_events = {
			{
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.event_spec = tsl2772_events,
			.num_event_specs = ARRAY_SIZE(tsl2772_events),
			},
		},
		.channel_without_events = {
			{
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			},
		},
		.chan_table_elements = 1,
		.info = &tsl2772_device_info[PRX2],
	},
	[ALSPRX2] = {
		.channel_with_events = {
			{
			.type = IIO_LIGHT,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE) |
				BIT(IIO_CHAN_INFO_CALIBBIAS),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.event_spec = tsl2772_events,
			.num_event_specs = ARRAY_SIZE(tsl2772_events),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 1,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			}, {
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.event_spec = tsl2772_events,
			.num_event_specs = ARRAY_SIZE(tsl2772_events),
			},
		},
		.channel_without_events = {
			{
			.type = IIO_LIGHT,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE) |
				BIT(IIO_CHAN_INFO_CALIBBIAS),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 1,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			}, {
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			},
		},
		.chan_table_elements = 4,
		.info = &tsl2772_device_info[ALSPRX2],
	},
};

static int tsl2772_probe(struct i2c_client *clientp,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct tsl2772_chip *chip;
	int ret;

	indio_dev = devm_iio_device_alloc(&clientp->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	chip->client = clientp;
	i2c_set_clientdata(clientp, indio_dev);

	chip->supplies[TSL2772_SUPPLY_VDD].supply = "vdd";
	chip->supplies[TSL2772_SUPPLY_VDDIO].supply = "vddio";

	ret = devm_regulator_bulk_get(&clientp->dev,
				      ARRAY_SIZE(chip->supplies),
				      chip->supplies);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&clientp->dev,
				"Failed to get regulators: %d\n",
				ret);

		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(chip->supplies), chip->supplies);
	if (ret < 0) {
		dev_err(&clientp->dev, "Failed to enable regulators: %d\n",
			ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&clientp->dev,
					tsl2772_disable_regulators_action,
					chip);
	if (ret < 0) {
		dev_err(&clientp->dev, "Failed to setup regulator cleanup action %d\n",
			ret);
		return ret;
	}

	usleep_range(TSL2772_BOOT_MIN_SLEEP_TIME, TSL2772_BOOT_MAX_SLEEP_TIME);

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2772_CMD_REG | TSL2772_CHIPID);
	if (ret < 0)
		return ret;

	if (tsl2772_device_id_verif(ret, id->driver_data) <= 0) {
		dev_info(&chip->client->dev,
			 "%s: i2c device found does not match expected id\n",
				__func__);
		return -EINVAL;
	}

	ret = i2c_smbus_write_byte(clientp, TSL2772_CMD_REG | TSL2772_CNTRL);
	if (ret < 0) {
		dev_err(&clientp->dev,
			"%s: Failed to write to CMD register: %d\n",
			__func__, ret);
		return ret;
	}

	mutex_init(&chip->als_mutex);
	mutex_init(&chip->prox_mutex);

	chip->tsl2772_chip_status = TSL2772_CHIP_UNKNOWN;
	chip->pdata = dev_get_platdata(&clientp->dev);
	chip->id = id->driver_data;
	chip->chip_info =
		&tsl2772_chip_info_tbl[device_channel_config[id->driver_data]];

	indio_dev->info = chip->chip_info->info;
	indio_dev->dev.parent = &clientp->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = chip->client->name;
	indio_dev->num_channels = chip->chip_info->chan_table_elements;

	if (clientp->irq) {
		indio_dev->channels = chip->chip_info->channel_with_events;

		ret = devm_request_threaded_irq(&clientp->dev, clientp->irq,
						NULL,
						&tsl2772_event_handler,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"TSL2772_event",
						indio_dev);
		if (ret) {
			dev_err(&clientp->dev,
				"%s: irq request failed\n", __func__);
			return ret;
		}
	} else {
		indio_dev->channels = chip->chip_info->channel_without_events;
	}

	tsl2772_defaults(chip);
	ret = tsl2772_chip_on(indio_dev);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&clientp->dev,
					tsl2772_chip_off_action,
					indio_dev);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(&clientp->dev, indio_dev);
}

static int tsl2772_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	int ret;

	ret = tsl2772_chip_off(indio_dev);
	regulator_bulk_disable(ARRAY_SIZE(chip->supplies), chip->supplies);

	return ret;
}

static int tsl2772_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct tsl2772_chip *chip = iio_priv(indio_dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(chip->supplies), chip->supplies);
	if (ret < 0)
		return ret;

	usleep_range(TSL2772_BOOT_MIN_SLEEP_TIME, TSL2772_BOOT_MAX_SLEEP_TIME);

	return tsl2772_chip_on(indio_dev);
}

static const struct i2c_device_id tsl2772_idtable[] = {
	{ "tsl2571", tsl2571 },
	{ "tsl2671", tsl2671 },
	{ "tmd2671", tmd2671 },
	{ "tsl2771", tsl2771 },
	{ "tmd2771", tmd2771 },
	{ "tsl2572", tsl2572 },
	{ "tsl2672", tsl2672 },
	{ "tmd2672", tmd2672 },
	{ "tsl2772", tsl2772 },
	{ "tmd2772", tmd2772 },
	{ "apds9930", apds9930},
	{}
};

MODULE_DEVICE_TABLE(i2c, tsl2772_idtable);

static const struct of_device_id tsl2772_of_match[] = {
	{ .compatible = "amstaos,tsl2571" },
	{ .compatible = "amstaos,tsl2671" },
	{ .compatible = "amstaos,tmd2671" },
	{ .compatible = "amstaos,tsl2771" },
	{ .compatible = "amstaos,tmd2771" },
	{ .compatible = "amstaos,tsl2572" },
	{ .compatible = "amstaos,tsl2672" },
	{ .compatible = "amstaos,tmd2672" },
	{ .compatible = "amstaos,tsl2772" },
	{ .compatible = "amstaos,tmd2772" },
	{ .compatible = "avago,apds9930" },
	{}
};
MODULE_DEVICE_TABLE(of, tsl2772_of_match);

static const struct dev_pm_ops tsl2772_pm_ops = {
	.suspend = tsl2772_suspend,
	.resume  = tsl2772_resume,
};

static struct i2c_driver tsl2772_driver = {
	.driver = {
		.name = "tsl2772",
		.of_match_table = tsl2772_of_match,
		.pm = &tsl2772_pm_ops,
	},
	.id_table = tsl2772_idtable,
	.probe = tsl2772_probe,
};

module_i2c_driver(tsl2772_driver);

MODULE_AUTHOR("J. August Brenner <Jon.Brenner@ams.com>");
MODULE_AUTHOR("Brian Masney <masneyb@onstation.org>");
MODULE_DESCRIPTION("TAOS tsl2772 ambient and proximity light sensor driver");
MODULE_LICENSE("GPL");
