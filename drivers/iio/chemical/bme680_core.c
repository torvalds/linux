// SPDX-License-Identifier: GPL-2.0
/*
 * Bosch BME680 - Temperature, Pressure, Humidity & Gas Sensor
 *
 * Copyright (C) 2017 - 2018 Bosch Sensortec GmbH
 * Copyright (C) 2018 Himanshu Jha <himanshujha199640@gmail.com>
 *
 * Datasheet:
 * https://ae-bst.resource.bosch.com/media/_tech/media/datasheets/BST-BME680-DS001-00.pdf
 */
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include <asm/unaligned.h>

#include "bme680.h"

/* 1st set of calibration data */
enum {
	/* Temperature calib indexes */
	T2_LSB = 0,
	T3 = 2,
	/* Pressure calib indexes */
	P1_LSB = 4,
	P2_LSB = 6,
	P3 = 8,
	P4_LSB = 10,
	P5_LSB = 12,
	P7 = 14,
	P6 = 15,
	P8_LSB = 18,
	P9_LSB = 20,
	P10 = 22,
};

/* 2nd set of calibration data */
enum {
	/* Humidity calib indexes */
	H2_MSB = 0,
	H1_LSB = 1,
	H3 = 3,
	H4 = 4,
	H5 = 5,
	H6 = 6,
	H7 = 7,
	/* Stray T1 calib index */
	T1_LSB = 8,
	/* Gas heater calib indexes */
	GH2_LSB = 10,
	GH1 = 12,
	GH3 = 13,
};

/* 3rd set of calibration data */
enum {
	RES_HEAT_VAL = 0,
	RES_HEAT_RANGE = 2,
	RANGE_SW_ERR = 4,
};

struct bme680_calib {
	u16 par_t1;
	s16 par_t2;
	s8  par_t3;
	u16 par_p1;
	s16 par_p2;
	s8  par_p3;
	s16 par_p4;
	s16 par_p5;
	s8  par_p6;
	s8  par_p7;
	s16 par_p8;
	s16 par_p9;
	u8  par_p10;
	u16 par_h1;
	u16 par_h2;
	s8  par_h3;
	s8  par_h4;
	s8  par_h5;
	u8  par_h6;
	s8  par_h7;
	s8  par_gh1;
	s16 par_gh2;
	s8  par_gh3;
	u8  res_heat_range;
	s8  res_heat_val;
	s8  range_sw_err;
};

struct bme680_data {
	struct regmap *regmap;
	struct bme680_calib bme680;
	struct mutex lock; /* Protect multiple serial R/W ops to device. */
	u8 oversampling_temp;
	u8 oversampling_press;
	u8 oversampling_humid;
	u16 heater_dur;
	u16 heater_temp;

	union {
		u8 buf[3];
		unsigned int check;
		__be16 be16;
		u8 bme680_cal_buf_1[BME680_CALIB_RANGE_1_LEN];
		u8 bme680_cal_buf_2[BME680_CALIB_RANGE_2_LEN];
		u8 bme680_cal_buf_3[BME680_CALIB_RANGE_3_LEN];
	};
};

static const struct regmap_range bme680_volatile_ranges[] = {
	regmap_reg_range(BME680_REG_MEAS_STAT_0, BME680_REG_GAS_R_LSB),
	regmap_reg_range(BME680_REG_STATUS, BME680_REG_STATUS),
	regmap_reg_range(BME680_T2_LSB_REG, BME680_GH3_REG),
};

static const struct regmap_access_table bme680_volatile_table = {
	.yes_ranges	= bme680_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(bme680_volatile_ranges),
};

const struct regmap_config bme680_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xef,
	.volatile_table = &bme680_volatile_table,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_NS(bme680_regmap_config, IIO_BME680);

static const struct iio_chan_spec bme680_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	},
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	},
	{
		.type = IIO_RESISTANCE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
};

static int bme680_read_calib(struct bme680_data *data,
			     struct bme680_calib *calib)
{
	struct device *dev = regmap_get_device(data->regmap);
	unsigned int tmp_msb, tmp_lsb;
	int ret;

	ret = regmap_bulk_read(data->regmap, BME680_T2_LSB_REG,
			       data->bme680_cal_buf_1,
			       sizeof(data->bme680_cal_buf_1));
	if (ret < 0) {
		dev_err(dev, "failed to read 1st set of calib data;\n");
		return ret;
	}

	calib->par_t2 = get_unaligned_le16(&data->bme680_cal_buf_1[T2_LSB]);
	calib->par_t3 = data->bme680_cal_buf_1[T3];
	calib->par_p1 = get_unaligned_le16(&data->bme680_cal_buf_1[P1_LSB]);
	calib->par_p2 = get_unaligned_le16(&data->bme680_cal_buf_1[P2_LSB]);
	calib->par_p3 = data->bme680_cal_buf_1[P3];
	calib->par_p4 = get_unaligned_le16(&data->bme680_cal_buf_1[P4_LSB]);
	calib->par_p5 = get_unaligned_le16(&data->bme680_cal_buf_1[P5_LSB]);
	calib->par_p7 = data->bme680_cal_buf_1[P7];
	calib->par_p6 = data->bme680_cal_buf_1[P6];
	calib->par_p8 = get_unaligned_le16(&data->bme680_cal_buf_1[P8_LSB]);
	calib->par_p9 = get_unaligned_le16(&data->bme680_cal_buf_1[P9_LSB]);
	calib->par_p10 = data->bme680_cal_buf_1[P10];

	ret = regmap_bulk_read(data->regmap, BME680_H2_MSB_REG,
			       data->bme680_cal_buf_2,
			       sizeof(data->bme680_cal_buf_2));
	if (ret < 0) {
		dev_err(dev, "failed to read 2nd set of calib data;\n");
		return ret;
	}

	tmp_lsb = data->bme680_cal_buf_2[H1_LSB];
	tmp_msb = data->bme680_cal_buf_2[H1_LSB + 1];
	calib->par_h1 = (tmp_msb << BME680_HUM_REG_SHIFT_VAL) |
			(tmp_lsb & BME680_BIT_H1_DATA_MASK);

	tmp_msb = data->bme680_cal_buf_2[H2_MSB];
	tmp_lsb = data->bme680_cal_buf_2[H2_MSB + 1];
	calib->par_h2 = (tmp_msb << BME680_HUM_REG_SHIFT_VAL) |
			(tmp_lsb >> BME680_HUM_REG_SHIFT_VAL);

	calib->par_h3 = data->bme680_cal_buf_2[H3];
	calib->par_h4 = data->bme680_cal_buf_2[H4];
	calib->par_h5 = data->bme680_cal_buf_2[H5];
	calib->par_h6 = data->bme680_cal_buf_2[H6];
	calib->par_h7 = data->bme680_cal_buf_2[H7];
	calib->par_t1 = get_unaligned_le16(&data->bme680_cal_buf_2[T1_LSB]);
	calib->par_gh2 = get_unaligned_le16(&data->bme680_cal_buf_2[GH2_LSB]);
	calib->par_gh1 = data->bme680_cal_buf_2[GH1];
	calib->par_gh3 = data->bme680_cal_buf_2[GH3];

	ret = regmap_bulk_read(data->regmap, BME680_REG_RES_HEAT_VAL,
			       data->bme680_cal_buf_3,
			       sizeof(data->bme680_cal_buf_3));
	if (ret < 0) {
		dev_err(dev, "failed to read 3rd set of calib data;\n");
		return ret;
	}

	calib->res_heat_val = data->bme680_cal_buf_3[RES_HEAT_VAL];

	calib->res_heat_range = FIELD_GET(BME680_RHRANGE_MASK,
					data->bme680_cal_buf_3[RES_HEAT_RANGE]);

	calib->range_sw_err = FIELD_GET(BME680_RSERROR_MASK,
					data->bme680_cal_buf_3[RANGE_SW_ERR]);

	return 0;
}

static int bme680_read_temp_adc(struct bme680_data *data, u32 *adc_temp)
{
	struct device *dev = regmap_get_device(data->regmap);
	u32 value_temp;
	int ret;

	ret = regmap_bulk_read(data->regmap, BME680_REG_TEMP_MSB,
			       data->buf, BME680_TEMP_NUM_BYTES);
	if (ret < 0) {
		dev_err(dev, "failed to read temperature\n");
		return ret;
	}

	value_temp = FIELD_GET(BME680_MEAS_TRIM_MASK,
			       get_unaligned_be24(data->buf));
	if (value_temp == BME680_MEAS_SKIPPED) {
		/* reading was skipped */
		dev_err(dev, "reading temperature skipped\n");
		return -EINVAL;
	}
	*adc_temp = value_temp;

	return 0;
}

/*
 * Taken from Bosch BME680 API:
 * https://github.com/BoschSensortec/BME680_driver/blob/63bb5336/bme680.c#L876
 *
 * Returns temperature measurement in DegC, resolutions is 0.01 DegC. Therefore,
 * output value of "3233" represents 32.33 DegC.
 */
static s32 bme680_calc_t_fine(struct bme680_data *data, u32 adc_temp)
{
	struct bme680_calib *calib = &data->bme680;
	s64 var1, var2, var3;

	/* If the calibration is invalid, attempt to reload it */
	if (!calib->par_t2)
		bme680_read_calib(data, calib);

	var1 = ((s32)adc_temp >> 3) - ((s32)calib->par_t1 << 1);
	var2 = (var1 * calib->par_t2) >> 11;
	var3 = ((var1 >> 1) * (var1 >> 1)) >> 12;
	var3 = (var3 * ((s32)calib->par_t3 << 4)) >> 14;
	return var2 + var3; /* t_fine = var2 + var3 */
}

static int bme680_get_t_fine(struct bme680_data *data, s32 *t_fine)
{
	u32 adc_temp;
	int ret;

	ret = bme680_read_temp_adc(data, &adc_temp);
	if (ret)
		return ret;

	*t_fine = bme680_calc_t_fine(data, adc_temp);

	return 0;
}

static s16 bme680_compensate_temp(struct bme680_data *data,
				  u32 adc_temp)
{
	return (bme680_calc_t_fine(data, adc_temp) * 5 + 128) / 256;
}

static int bme680_read_press_adc(struct bme680_data *data, u32 *adc_press)
{
	struct device *dev = regmap_get_device(data->regmap);
	u32 value_press;
	int ret;

	ret = regmap_bulk_read(data->regmap, BME680_REG_PRESS_MSB,
			       data->buf, BME680_PRESS_NUM_BYTES);
	if (ret < 0) {
		dev_err(dev, "failed to read pressure\n");
		return ret;
	}

	value_press = FIELD_GET(BME680_MEAS_TRIM_MASK,
				get_unaligned_be24(data->buf));
	if (value_press == BME680_MEAS_SKIPPED) {
		/* reading was skipped */
		dev_err(dev, "reading pressure skipped\n");
		return -EINVAL;
	}
	*adc_press = value_press;

	return 0;
}

/*
 * Taken from Bosch BME680 API:
 * https://github.com/BoschSensortec/BME680_driver/blob/63bb5336/bme680.c#L896
 *
 * Returns pressure measurement in Pa. Output value of "97356" represents
 * 97356 Pa = 973.56 hPa.
 */
static u32 bme680_compensate_press(struct bme680_data *data,
				   u32 adc_press, s32 t_fine)
{
	struct bme680_calib *calib = &data->bme680;
	s32 var1, var2, var3, press_comp;

	var1 = (t_fine >> 1) - 64000;
	var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * calib->par_p6) >> 2;
	var2 = var2 + (var1 * calib->par_p5 << 1);
	var2 = (var2 >> 2) + ((s32)calib->par_p4 << 16);
	var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) *
			((s32)calib->par_p3 << 5)) >> 3) +
			((calib->par_p2 * var1) >> 1);
	var1 = var1 >> 18;
	var1 = ((32768 + var1) * calib->par_p1) >> 15;
	press_comp = 1048576 - adc_press;
	press_comp = ((press_comp - (var2 >> 12)) * 3125);

	if (press_comp >= BME680_MAX_OVERFLOW_VAL)
		press_comp = ((press_comp / (u32)var1) << 1);
	else
		press_comp = ((press_comp << 1) / (u32)var1);

	var1 = (calib->par_p9 * (((press_comp >> 3) *
			(press_comp >> 3)) >> 13)) >> 12;
	var2 = ((press_comp >> 2) * calib->par_p8) >> 13;
	var3 = ((press_comp >> 8) * (press_comp >> 8) *
			(press_comp >> 8) * calib->par_p10) >> 17;

	press_comp += (var1 + var2 + var3 + ((s32)calib->par_p7 << 7)) >> 4;

	return press_comp;
}

static int bme680_read_humid_adc(struct bme680_data *data, u32 *adc_humidity)
{
	struct device *dev = regmap_get_device(data->regmap);
	u32 value_humidity;
	int ret;

	ret = regmap_bulk_read(data->regmap, BME680_REG_HUMIDITY_MSB,
			       &data->be16, BME680_HUMID_NUM_BYTES);
	if (ret < 0) {
		dev_err(dev, "failed to read humidity\n");
		return ret;
	}

	value_humidity = be16_to_cpu(data->be16);
	if (value_humidity == BME680_MEAS_SKIPPED) {
		/* reading was skipped */
		dev_err(dev, "reading humidity skipped\n");
		return -EINVAL;
	}
	*adc_humidity = value_humidity;

	return 0;
}

/*
 * Taken from Bosch BME680 API:
 * https://github.com/BoschSensortec/BME680_driver/blob/63bb5336/bme680.c#L937
 *
 * Returns humidity measurement in percent, resolution is 0.001 percent. Output
 * value of "43215" represents 43.215 %rH.
 */
static u32 bme680_compensate_humid(struct bme680_data *data,
				   u16 adc_humid, s32 t_fine)
{
	struct bme680_calib *calib = &data->bme680;
	s32 var1, var2, var3, var4, var5, var6, temp_scaled, calc_hum;

	temp_scaled = (t_fine * 5 + 128) >> 8;
	var1 = (adc_humid - (((s32)calib->par_h1 * 16))) -
		(((temp_scaled * calib->par_h3) / 100) >> 1);
	var2 = (calib->par_h2 *
		(((temp_scaled * calib->par_h4) / 100) +
		 (((temp_scaled * ((temp_scaled * calib->par_h5) / 100))
		   >> 6) / 100) + (1 << 14))) >> 10;
	var3 = var1 * var2;
	var4 = (s32)calib->par_h6 << 7;
	var4 = (var4 + ((temp_scaled * calib->par_h7) / 100)) >> 4;
	var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;
	var6 = (var4 * var5) >> 1;
	calc_hum = (((var3 + var6) >> 10) * 1000) >> 12;

	calc_hum = clamp(calc_hum, 0, 100000); /* clamp between 0-100 %rH */

	return calc_hum;
}

/*
 * Taken from Bosch BME680 API:
 * https://github.com/BoschSensortec/BME680_driver/blob/63bb5336/bme680.c#L973
 *
 * Returns gas measurement in Ohm. Output value of "82986" represent 82986 ohms.
 */
static u32 bme680_compensate_gas(struct bme680_data *data, u16 gas_res_adc,
				 u8 gas_range)
{
	struct bme680_calib *calib = &data->bme680;
	s64 var1;
	u64 var2;
	s64 var3;
	u32 calc_gas_res;

	/* Look up table for the possible gas range values */
	static const u32 lookupTable[16] = {2147483647u, 2147483647u,
				2147483647u, 2147483647u, 2147483647u,
				2126008810u, 2147483647u, 2130303777u,
				2147483647u, 2147483647u, 2143188679u,
				2136746228u, 2147483647u, 2126008810u,
				2147483647u, 2147483647u};

	var1 = ((1340 + (5 * (s64) calib->range_sw_err)) *
			((s64) lookupTable[gas_range])) >> 16;
	var2 = ((gas_res_adc << 15) - 16777216) + var1;
	var3 = ((125000 << (15 - gas_range)) * var1) >> 9;
	var3 += (var2 >> 1);
	calc_gas_res = div64_s64(var3, (s64) var2);

	return calc_gas_res;
}

/*
 * Taken from Bosch BME680 API:
 * https://github.com/BoschSensortec/BME680_driver/blob/63bb5336/bme680.c#L1002
 */
static u8 bme680_calc_heater_res(struct bme680_data *data, u16 temp)
{
	struct bme680_calib *calib = &data->bme680;
	s32 var1, var2, var3, var4, var5, heatr_res_x100;
	u8 heatr_res;

	if (temp > 400) /* Cap temperature */
		temp = 400;

	var1 = (((s32) BME680_AMB_TEMP * calib->par_gh3) / 1000) * 256;
	var2 = (calib->par_gh1 + 784) * (((((calib->par_gh2 + 154009) *
						temp * 5) / 100)
						+ 3276800) / 10);
	var3 = var1 + (var2 / 2);
	var4 = (var3 / (calib->res_heat_range + 4));
	var5 = 131 * calib->res_heat_val + 65536;
	heatr_res_x100 = ((var4 / var5) - 250) * 34;
	heatr_res = DIV_ROUND_CLOSEST(heatr_res_x100, 100);

	return heatr_res;
}

/*
 * Taken from Bosch BME680 API:
 * https://github.com/BoschSensortec/BME680_driver/blob/63bb5336/bme680.c#L1188
 */
static u8 bme680_calc_heater_dur(u16 dur)
{
	u8 durval, factor = 0;

	if (dur >= 0xfc0) {
		durval = 0xff; /* Max duration */
	} else {
		while (dur > 0x3F) {
			dur = dur / 4;
			factor += 1;
		}
		durval = dur + (factor * 64);
	}

	return durval;
}

static int bme680_set_mode(struct bme680_data *data, bool mode)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	if (mode) {
		ret = regmap_write_bits(data->regmap, BME680_REG_CTRL_MEAS,
					BME680_MODE_MASK, BME680_MODE_FORCED);
		if (ret < 0)
			dev_err(dev, "failed to set forced mode\n");

	} else {
		ret = regmap_write_bits(data->regmap, BME680_REG_CTRL_MEAS,
					BME680_MODE_MASK, BME680_MODE_SLEEP);
		if (ret < 0)
			dev_err(dev, "failed to set sleep mode\n");

	}

	return ret;
}

static u8 bme680_oversampling_to_reg(u8 val)
{
	return ilog2(val) + 1;
}

/*
 * Taken from Bosch BME680 API:
 * https://github.com/boschsensortec/BME68x_SensorAPI/blob/v4.4.8/bme68x.c#L490
 */
static int bme680_wait_for_eoc(struct bme680_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	/*
	 * (Sum of oversampling ratios * time per oversampling) +
	 * TPH measurement + gas measurement + wait transition from forced mode
	 * + heater duration
	 */
	int wait_eoc_us = ((data->oversampling_temp + data->oversampling_press +
			   data->oversampling_humid) * 1936) + (477 * 4) +
			   (477 * 5) + 1000 + (data->heater_dur * 1000);

	usleep_range(wait_eoc_us, wait_eoc_us + 100);

	ret = regmap_read(data->regmap, BME680_REG_MEAS_STAT_0, &data->check);
	if (ret) {
		dev_err(dev, "failed to read measurement status register.\n");
		return ret;
	}
	if (data->check & BME680_MEAS_BIT) {
		dev_err(dev, "Device measurement cycle incomplete.\n");
		return -EBUSY;
	}
	if (!(data->check & BME680_NEW_DATA_BIT)) {
		dev_err(dev, "No new data available from the device.\n");
		return -ENODATA;
	}

	return 0;
}

static int bme680_chip_config(struct bme680_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	u8 osrs;

	osrs = FIELD_PREP(
		BME680_OSRS_HUMIDITY_MASK,
		bme680_oversampling_to_reg(data->oversampling_humid));
	/*
	 * Highly recommended to set oversampling of humidity before
	 * temperature/pressure oversampling.
	 */
	ret = regmap_update_bits(data->regmap, BME680_REG_CTRL_HUMIDITY,
				 BME680_OSRS_HUMIDITY_MASK, osrs);
	if (ret < 0) {
		dev_err(dev, "failed to write ctrl_hum register\n");
		return ret;
	}

	/* IIR filter settings */
	ret = regmap_update_bits(data->regmap, BME680_REG_CONFIG,
				 BME680_FILTER_MASK,
				 BME680_FILTER_COEFF_VAL);
	if (ret < 0) {
		dev_err(dev, "failed to write config register\n");
		return ret;
	}

	osrs = FIELD_PREP(BME680_OSRS_TEMP_MASK,
			  bme680_oversampling_to_reg(data->oversampling_temp)) |
	       FIELD_PREP(BME680_OSRS_PRESS_MASK,
			  bme680_oversampling_to_reg(data->oversampling_press));
	ret = regmap_write_bits(data->regmap, BME680_REG_CTRL_MEAS,
				BME680_OSRS_TEMP_MASK | BME680_OSRS_PRESS_MASK,
				osrs);
	if (ret < 0) {
		dev_err(dev, "failed to write ctrl_meas register\n");
		return ret;
	}

	return 0;
}

static int bme680_gas_config(struct bme680_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	u8 heatr_res, heatr_dur;

	/* Go to sleep */
	ret = bme680_set_mode(data, false);
	if (ret < 0)
		return ret;

	heatr_res = bme680_calc_heater_res(data, data->heater_temp);

	/* set target heater temperature */
	ret = regmap_write(data->regmap, BME680_REG_RES_HEAT_0, heatr_res);
	if (ret < 0) {
		dev_err(dev, "failed to write res_heat_0 register\n");
		return ret;
	}

	heatr_dur = bme680_calc_heater_dur(data->heater_dur);

	/* set target heating duration */
	ret = regmap_write(data->regmap, BME680_REG_GAS_WAIT_0, heatr_dur);
	if (ret < 0) {
		dev_err(dev, "failed to write gas_wait_0 register\n");
		return ret;
	}

	/* Enable the gas sensor and select heater profile set-point 0 */
	ret = regmap_update_bits(data->regmap, BME680_REG_CTRL_GAS_1,
				 BME680_RUN_GAS_MASK | BME680_NB_CONV_MASK,
				 FIELD_PREP(BME680_RUN_GAS_MASK, 1) |
				 FIELD_PREP(BME680_NB_CONV_MASK, 0));
	if (ret < 0)
		dev_err(dev, "failed to write ctrl_gas_1 register\n");

	return ret;
}

static int bme680_read_temp(struct bme680_data *data, int *val)
{
	int ret;
	u32 adc_temp;
	s16 comp_temp;

	ret = bme680_read_temp_adc(data, &adc_temp);
	if (ret)
		return ret;

	comp_temp = bme680_compensate_temp(data, adc_temp);
	*val = comp_temp * 10; /* Centidegrees to millidegrees */
	return IIO_VAL_INT;
}

static int bme680_read_press(struct bme680_data *data,
			     int *val, int *val2)
{
	int ret;
	u32 adc_press;
	s32 t_fine;

	ret = bme680_get_t_fine(data, &t_fine);
	if (ret)
		return ret;

	ret = bme680_read_press_adc(data, &adc_press);
	if (ret)
		return ret;

	*val = bme680_compensate_press(data, adc_press, t_fine);
	*val2 = 1000;
	return IIO_VAL_FRACTIONAL;
}

static int bme680_read_humid(struct bme680_data *data,
			     int *val, int *val2)
{
	int ret;
	u32 adc_humidity, comp_humidity;
	s32 t_fine;

	ret = bme680_get_t_fine(data, &t_fine);
	if (ret)
		return ret;

	ret = bme680_read_humid_adc(data, &adc_humidity);
	if (ret)
		return ret;

	comp_humidity = bme680_compensate_humid(data, adc_humidity, t_fine);

	*val = comp_humidity;
	*val2 = 1000;
	return IIO_VAL_FRACTIONAL;
}

static int bme680_read_gas(struct bme680_data *data,
			   int *val)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	u16 adc_gas_res, gas_regs_val;
	u8 gas_range;

	ret = regmap_read(data->regmap, BME680_REG_MEAS_STAT_0, &data->check);
	if (data->check & BME680_GAS_MEAS_BIT) {
		dev_err(dev, "gas measurement incomplete\n");
		return -EBUSY;
	}

	ret = regmap_bulk_read(data->regmap, BME680_REG_GAS_MSB,
			       &data->be16, BME680_GAS_NUM_BYTES);
	if (ret < 0) {
		dev_err(dev, "failed to read gas resistance\n");
		return ret;
	}

	gas_regs_val = be16_to_cpu(data->be16);
	adc_gas_res = FIELD_GET(BME680_ADC_GAS_RES, gas_regs_val);

	/*
	 * occurs if either the gas heating duration was insuffient
	 * to reach the target heater temperature or the target
	 * heater temperature was too high for the heater sink to
	 * reach.
	 */
	if ((gas_regs_val & BME680_GAS_STAB_BIT) == 0) {
		dev_err(dev, "heater failed to reach the target temperature\n");
		return -EINVAL;
	}

	gas_range = FIELD_GET(BME680_GAS_RANGE_MASK, gas_regs_val);

	*val = bme680_compensate_gas(data, adc_gas_res, gas_range);
	return IIO_VAL_INT;
}

static int bme680_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct bme680_data *data = iio_priv(indio_dev);
	int ret;

	guard(mutex)(&data->lock);

	/* set forced mode to trigger measurement */
	ret = bme680_set_mode(data, true);
	if (ret < 0)
		return ret;

	ret = bme680_wait_for_eoc(data);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_TEMP:
			return bme680_read_temp(data, val);
		case IIO_PRESSURE:
			return bme680_read_press(data, val, val2);
		case IIO_HUMIDITYRELATIVE:
			return bme680_read_humid(data, val, val2);
		case IIO_RESISTANCE:
			return bme680_read_gas(data, val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		switch (chan->type) {
		case IIO_TEMP:
			*val = data->oversampling_temp;
			return IIO_VAL_INT;
		case IIO_PRESSURE:
			*val = data->oversampling_press;
			return IIO_VAL_INT;
		case IIO_HUMIDITYRELATIVE:
			*val = data->oversampling_humid;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static bool bme680_is_valid_oversampling(int rate)
{
	return (rate > 0 && rate <= 16 && is_power_of_2(rate));
}

static int bme680_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct bme680_data *data = iio_priv(indio_dev);

	guard(mutex)(&data->lock);

	if (val2 != 0)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
	{
		if (!bme680_is_valid_oversampling(val))
			return -EINVAL;

		switch (chan->type) {
		case IIO_TEMP:
			data->oversampling_temp = val;
			break;
		case IIO_PRESSURE:
			data->oversampling_press = val;
			break;
		case IIO_HUMIDITYRELATIVE:
			data->oversampling_humid = val;
			break;
		default:
			return -EINVAL;
		}

		return bme680_chip_config(data);
	}
	default:
		return -EINVAL;
	}
}

static const char bme680_oversampling_ratio_show[] = "1 2 4 8 16";

static IIO_CONST_ATTR(oversampling_ratio_available,
		      bme680_oversampling_ratio_show);

static struct attribute *bme680_attributes[] = {
	&iio_const_attr_oversampling_ratio_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group bme680_attribute_group = {
	.attrs = bme680_attributes,
};

static const struct iio_info bme680_info = {
	.read_raw = &bme680_read_raw,
	.write_raw = &bme680_write_raw,
	.attrs = &bme680_attribute_group,
};

int bme680_core_probe(struct device *dev, struct regmap *regmap,
		      const char *name)
{
	struct iio_dev *indio_dev;
	struct bme680_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	mutex_init(&data->lock);
	dev_set_drvdata(dev, indio_dev);
	data->regmap = regmap;
	indio_dev->name = name;
	indio_dev->channels = bme680_channels;
	indio_dev->num_channels = ARRAY_SIZE(bme680_channels);
	indio_dev->info = &bme680_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* default values for the sensor */
	data->oversampling_humid = 2; /* 2X oversampling rate */
	data->oversampling_press = 4; /* 4X oversampling rate */
	data->oversampling_temp = 8;  /* 8X oversampling rate */
	data->heater_temp = 320; /* degree Celsius */
	data->heater_dur = 150;  /* milliseconds */

	ret = regmap_write(regmap, BME680_REG_SOFT_RESET,
			   BME680_CMD_SOFTRESET);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to reset chip\n");

	usleep_range(BME680_STARTUP_TIME_US, BME680_STARTUP_TIME_US + 1000);

	ret = regmap_read(regmap, BME680_REG_CHIP_ID, &data->check);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Error reading chip ID\n");

	if (data->check != BME680_CHIP_ID_VAL) {
		dev_err(dev, "Wrong chip ID, got %x expected %x\n",
			data->check, BME680_CHIP_ID_VAL);
		return -ENODEV;
	}

	ret = bme680_read_calib(data, &data->bme680);
	if (ret < 0) {
		return dev_err_probe(dev, ret,
			"failed to read calibration coefficients at probe\n");
	}

	ret = bme680_chip_config(data);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to set chip_config data\n");

	ret = bme680_gas_config(data);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to set gas config data\n");

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(bme680_core_probe, IIO_BME680);

MODULE_AUTHOR("Himanshu Jha <himanshujha199640@gmail.com>");
MODULE_DESCRIPTION("Bosch BME680 Driver");
MODULE_LICENSE("GPL v2");
