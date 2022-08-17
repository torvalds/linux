// SPDX-License-Identifier: GPL-2.0
/*
 * mlx90632.c - Melexis MLX90632 contactless IR temperature sensor
 *
 * Copyright (c) 2017 Melexis <cmo@melexis.com>
 *
 * Driver for the Melexis MLX90632 I2C 16-bit IR thermopile sensor
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* Memory sections addresses */
#define MLX90632_ADDR_RAM	0x4000 /* Start address of ram */
#define MLX90632_ADDR_EEPROM	0x2480 /* Start address of user eeprom */

/* EEPROM addresses - used at startup */
#define MLX90632_EE_CTRL	0x24d4 /* Control register initial value */
#define MLX90632_EE_I2C_ADDR	0x24d5 /* I2C address register initial value */
#define MLX90632_EE_VERSION	0x240b /* EEPROM version reg address */
#define MLX90632_EE_P_R		0x240c /* P_R calibration register 32bit */
#define MLX90632_EE_P_G		0x240e /* P_G calibration register 32bit */
#define MLX90632_EE_P_T		0x2410 /* P_T calibration register 32bit */
#define MLX90632_EE_P_O		0x2412 /* P_O calibration register 32bit */
#define MLX90632_EE_Aa		0x2414 /* Aa calibration register 32bit */
#define MLX90632_EE_Ab		0x2416 /* Ab calibration register 32bit */
#define MLX90632_EE_Ba		0x2418 /* Ba calibration register 32bit */
#define MLX90632_EE_Bb		0x241a /* Bb calibration register 32bit */
#define MLX90632_EE_Ca		0x241c /* Ca calibration register 32bit */
#define MLX90632_EE_Cb		0x241e /* Cb calibration register 32bit */
#define MLX90632_EE_Da		0x2420 /* Da calibration register 32bit */
#define MLX90632_EE_Db		0x2422 /* Db calibration register 32bit */
#define MLX90632_EE_Ea		0x2424 /* Ea calibration register 32bit */
#define MLX90632_EE_Eb		0x2426 /* Eb calibration register 32bit */
#define MLX90632_EE_Fa		0x2428 /* Fa calibration register 32bit */
#define MLX90632_EE_Fb		0x242a /* Fb calibration register 32bit */
#define MLX90632_EE_Ga		0x242c /* Ga calibration register 32bit */

#define MLX90632_EE_Gb		0x242e /* Gb calibration register 16bit */
#define MLX90632_EE_Ka		0x242f /* Ka calibration register 16bit */

#define MLX90632_EE_Ha		0x2481 /* Ha customer calib value reg 16bit */
#define MLX90632_EE_Hb		0x2482 /* Hb customer calib value reg 16bit */

/* Register addresses - volatile */
#define MLX90632_REG_I2C_ADDR	0x3000 /* Chip I2C address register */

/* Control register address - volatile */
#define MLX90632_REG_CONTROL	0x3001 /* Control Register address */
#define   MLX90632_CFG_PWR_MASK		GENMASK(2, 1) /* PowerMode Mask */
#define   MLX90632_CFG_MTYP_MASK		GENMASK(8, 4) /* Meas select Mask */

/* PowerModes statuses */
#define MLX90632_PWR_STATUS(ctrl_val) (ctrl_val << 1)
#define MLX90632_PWR_STATUS_HALT MLX90632_PWR_STATUS(0) /* hold */
#define MLX90632_PWR_STATUS_SLEEP_STEP MLX90632_PWR_STATUS(1) /* sleep step*/
#define MLX90632_PWR_STATUS_STEP MLX90632_PWR_STATUS(2) /* step */
#define MLX90632_PWR_STATUS_CONTINUOUS MLX90632_PWR_STATUS(3) /* continuous*/

/* Measurement types */
#define MLX90632_MTYP_MEDICAL 0
#define MLX90632_MTYP_EXTENDED 17

/* Measurement type select*/
#define MLX90632_MTYP_STATUS(ctrl_val) (ctrl_val << 4)
#define MLX90632_MTYP_STATUS_MEDICAL MLX90632_MTYP_STATUS(MLX90632_MTYP_MEDICAL)
#define MLX90632_MTYP_STATUS_EXTENDED MLX90632_MTYP_STATUS(MLX90632_MTYP_EXTENDED)

/* I2C command register - volatile */
#define MLX90632_REG_I2C_CMD    0x3005 /* I2C command Register address */

/* Device status register - volatile */
#define MLX90632_REG_STATUS	0x3fff /* Device status register */
#define   MLX90632_STAT_BUSY		BIT(10) /* Device busy indicator */
#define   MLX90632_STAT_EE_BUSY		BIT(9) /* EEPROM busy indicator */
#define   MLX90632_STAT_BRST		BIT(8) /* Brown out reset indicator */
#define   MLX90632_STAT_CYCLE_POS	GENMASK(6, 2) /* Data position */
#define   MLX90632_STAT_DATA_RDY	BIT(0) /* Data ready indicator */

/* RAM_MEAS address-es for each channel */
#define MLX90632_RAM_1(meas_num)	(MLX90632_ADDR_RAM + 3 * meas_num)
#define MLX90632_RAM_2(meas_num)	(MLX90632_ADDR_RAM + 3 * meas_num + 1)
#define MLX90632_RAM_3(meas_num)	(MLX90632_ADDR_RAM + 3 * meas_num + 2)

/* Name important RAM_MEAS channels */
#define MLX90632_RAM_DSP5_EXTENDED_AMBIENT_1 MLX90632_RAM_3(17)
#define MLX90632_RAM_DSP5_EXTENDED_AMBIENT_2 MLX90632_RAM_3(18)
#define MLX90632_RAM_DSP5_EXTENDED_OBJECT_1 MLX90632_RAM_1(17)
#define MLX90632_RAM_DSP5_EXTENDED_OBJECT_2 MLX90632_RAM_2(17)
#define MLX90632_RAM_DSP5_EXTENDED_OBJECT_3 MLX90632_RAM_1(18)
#define MLX90632_RAM_DSP5_EXTENDED_OBJECT_4 MLX90632_RAM_2(18)
#define MLX90632_RAM_DSP5_EXTENDED_OBJECT_5 MLX90632_RAM_1(19)
#define MLX90632_RAM_DSP5_EXTENDED_OBJECT_6 MLX90632_RAM_2(19)

/* Magic constants */
#define MLX90632_ID_MEDICAL	0x0105 /* EEPROM DSPv5 Medical device id */
#define MLX90632_ID_CONSUMER	0x0205 /* EEPROM DSPv5 Consumer device id */
#define MLX90632_ID_EXTENDED	0x0505 /* EEPROM DSPv5 Extended range device id */
#define MLX90632_ID_MASK	GENMASK(14, 0) /* DSP version and device ID in EE_VERSION */
#define MLX90632_DSP_VERSION	5 /* DSP version */
#define MLX90632_DSP_MASK	GENMASK(7, 0) /* DSP version in EE_VERSION */
#define MLX90632_RESET_CMD	0x0006 /* Reset sensor (address or global) */
#define MLX90632_REF_12 	12LL /* ResCtrlRef value of Ch 1 or Ch 2 */
#define MLX90632_REF_3		12LL /* ResCtrlRef value of Channel 3 */
#define MLX90632_MAX_MEAS_NUM	31 /* Maximum measurements in list */
#define MLX90632_SLEEP_DELAY_MS 3000 /* Autosleep delay */
#define MLX90632_EXTENDED_LIMIT 27000 /* Extended mode raw value limit */

/**
 * struct mlx90632_data - private data for the MLX90632 device
 * @client: I2C client of the device
 * @lock: Internal mutex for multiple reads for single measurement
 * @regmap: Regmap of the device
 * @emissivity: Object emissivity from 0 to 1000 where 1000 = 1.
 * @mtyp: Measurement type physical sensor configuration for extended range
 *        calculations
 * @object_ambient_temperature: Ambient temperature at object (might differ of
 *                              the ambient temperature of sensor.
 */
struct mlx90632_data {
	struct i2c_client *client;
	struct mutex lock;
	struct regmap *regmap;
	u16 emissivity;
	u8 mtyp;
	u32 object_ambient_temperature;
};

static const struct regmap_range mlx90632_volatile_reg_range[] = {
	regmap_reg_range(MLX90632_REG_I2C_ADDR, MLX90632_REG_CONTROL),
	regmap_reg_range(MLX90632_REG_I2C_CMD, MLX90632_REG_I2C_CMD),
	regmap_reg_range(MLX90632_REG_STATUS, MLX90632_REG_STATUS),
	regmap_reg_range(MLX90632_RAM_1(0),
			 MLX90632_RAM_3(MLX90632_MAX_MEAS_NUM)),
};

static const struct regmap_access_table mlx90632_volatile_regs_tbl = {
	.yes_ranges = mlx90632_volatile_reg_range,
	.n_yes_ranges = ARRAY_SIZE(mlx90632_volatile_reg_range),
};

static const struct regmap_range mlx90632_read_reg_range[] = {
	regmap_reg_range(MLX90632_EE_VERSION, MLX90632_EE_Ka),
	regmap_reg_range(MLX90632_EE_CTRL, MLX90632_EE_I2C_ADDR),
	regmap_reg_range(MLX90632_EE_Ha, MLX90632_EE_Hb),
	regmap_reg_range(MLX90632_REG_I2C_ADDR, MLX90632_REG_CONTROL),
	regmap_reg_range(MLX90632_REG_I2C_CMD, MLX90632_REG_I2C_CMD),
	regmap_reg_range(MLX90632_REG_STATUS, MLX90632_REG_STATUS),
	regmap_reg_range(MLX90632_RAM_1(0),
			 MLX90632_RAM_3(MLX90632_MAX_MEAS_NUM)),
};

static const struct regmap_access_table mlx90632_readable_regs_tbl = {
	.yes_ranges = mlx90632_read_reg_range,
	.n_yes_ranges = ARRAY_SIZE(mlx90632_read_reg_range),
};

static const struct regmap_range mlx90632_no_write_reg_range[] = {
	regmap_reg_range(MLX90632_EE_VERSION, MLX90632_EE_Ka),
	regmap_reg_range(MLX90632_RAM_1(0),
			 MLX90632_RAM_3(MLX90632_MAX_MEAS_NUM)),
};

static const struct regmap_access_table mlx90632_writeable_regs_tbl = {
	.no_ranges = mlx90632_no_write_reg_range,
	.n_no_ranges = ARRAY_SIZE(mlx90632_no_write_reg_range),
};

static const struct regmap_config mlx90632_regmap = {
	.reg_bits = 16,
	.val_bits = 16,

	.volatile_table = &mlx90632_volatile_regs_tbl,
	.rd_table = &mlx90632_readable_regs_tbl,
	.wr_table = &mlx90632_writeable_regs_tbl,

	.use_single_read = true,
	.use_single_write = true,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.cache_type = REGCACHE_RBTREE,
};

static s32 mlx90632_pwr_set_sleep_step(struct regmap *regmap)
{
	return regmap_update_bits(regmap, MLX90632_REG_CONTROL,
				  MLX90632_CFG_PWR_MASK,
				  MLX90632_PWR_STATUS_SLEEP_STEP);
}

static s32 mlx90632_pwr_continuous(struct regmap *regmap)
{
	return regmap_update_bits(regmap, MLX90632_REG_CONTROL,
				  MLX90632_CFG_PWR_MASK,
				  MLX90632_PWR_STATUS_CONTINUOUS);
}

/**
 * mlx90632_perform_measurement() - Trigger and retrieve current measurement cycle
 * @data: pointer to mlx90632_data object containing regmap information
 *
 * Perform a measurement and return latest measurement cycle position reported
 * by sensor. This is a blocking function for 500ms, as that is default sensor
 * refresh rate.
 */
static int mlx90632_perform_measurement(struct mlx90632_data *data)
{
	unsigned int reg_status;
	int ret;

	ret = regmap_update_bits(data->regmap, MLX90632_REG_STATUS,
				 MLX90632_STAT_DATA_RDY, 0);
	if (ret < 0)
		return ret;

	ret = regmap_read_poll_timeout(data->regmap, MLX90632_REG_STATUS, reg_status,
				       !(reg_status & MLX90632_STAT_DATA_RDY), 10000,
				       100 * 10000);

	if (ret < 0) {
		dev_err(&data->client->dev, "data not ready");
		return -ETIMEDOUT;
	}

	return (reg_status & MLX90632_STAT_CYCLE_POS) >> 2;
}

static int mlx90632_set_meas_type(struct regmap *regmap, u8 type)
{
	int ret;

	if ((type != MLX90632_MTYP_MEDICAL) && (type != MLX90632_MTYP_EXTENDED))
		return -EINVAL;

	ret = regmap_write(regmap, MLX90632_REG_I2C_CMD, MLX90632_RESET_CMD);
	if (ret < 0)
		return ret;

	/*
	 * Give the mlx90632 some time to reset properly before sending a new I2C command
	 * if this is not done, the following I2C command(s) will not be accepted.
	 */
	usleep_range(150, 200);

	ret = regmap_write_bits(regmap, MLX90632_REG_CONTROL,
				 (MLX90632_CFG_MTYP_MASK | MLX90632_CFG_PWR_MASK),
				 (MLX90632_MTYP_STATUS(type) | MLX90632_PWR_STATUS_HALT));
	if (ret < 0)
		return ret;

	return mlx90632_pwr_continuous(regmap);
}

static int mlx90632_channel_new_select(int perform_ret, uint8_t *channel_new,
				       uint8_t *channel_old)
{
	switch (perform_ret) {
	case 1:
		*channel_new = 1;
		*channel_old = 2;
		break;
	case 2:
		*channel_new = 2;
		*channel_old = 1;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mlx90632_read_ambient_raw(struct regmap *regmap,
				     s16 *ambient_new_raw, s16 *ambient_old_raw)
{
	int ret;
	unsigned int read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_3(1), &read_tmp);
	if (ret < 0)
		return ret;
	*ambient_new_raw = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_3(2), &read_tmp);
	if (ret < 0)
		return ret;
	*ambient_old_raw = (s16)read_tmp;

	return ret;
}

static int mlx90632_read_object_raw(struct regmap *regmap,
				    int perform_measurement_ret,
				    s16 *object_new_raw, s16 *object_old_raw)
{
	int ret;
	unsigned int read_tmp;
	s16 read;
	u8 channel = 0;
	u8 channel_old = 0;

	ret = mlx90632_channel_new_select(perform_measurement_ret, &channel,
					  &channel_old);
	if (ret != 0)
		return ret;

	ret = regmap_read(regmap, MLX90632_RAM_2(channel), &read_tmp);
	if (ret < 0)
		return ret;

	read = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_1(channel), &read_tmp);
	if (ret < 0)
		return ret;
	*object_new_raw = (read + (s16)read_tmp) / 2;

	ret = regmap_read(regmap, MLX90632_RAM_2(channel_old), &read_tmp);
	if (ret < 0)
		return ret;
	read = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_1(channel_old), &read_tmp);
	if (ret < 0)
		return ret;
	*object_old_raw = (read + (s16)read_tmp) / 2;

	return ret;
}

static int mlx90632_read_all_channel(struct mlx90632_data *data,
				     s16 *ambient_new_raw, s16 *ambient_old_raw,
				     s16 *object_new_raw, s16 *object_old_raw)
{
	s32 ret, measurement;

	mutex_lock(&data->lock);
	measurement = mlx90632_perform_measurement(data);
	if (measurement < 0) {
		ret = measurement;
		goto read_unlock;
	}
	ret = mlx90632_read_ambient_raw(data->regmap, ambient_new_raw,
					ambient_old_raw);
	if (ret < 0)
		goto read_unlock;

	ret = mlx90632_read_object_raw(data->regmap, measurement,
				       object_new_raw, object_old_raw);
read_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static int mlx90632_read_ambient_raw_extended(struct regmap *regmap,
					      s16 *ambient_new_raw, s16 *ambient_old_raw)
{
	unsigned int read_tmp;
	int ret;

	ret = regmap_read(regmap, MLX90632_RAM_DSP5_EXTENDED_AMBIENT_1, &read_tmp);
	if (ret < 0)
		return ret;
	*ambient_new_raw = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_DSP5_EXTENDED_AMBIENT_2, &read_tmp);
	if (ret < 0)
		return ret;
	*ambient_old_raw = (s16)read_tmp;

	return 0;
}

static int mlx90632_read_object_raw_extended(struct regmap *regmap, s16 *object_new_raw)
{
	unsigned int read_tmp;
	s32 read;
	int ret;

	ret = regmap_read(regmap, MLX90632_RAM_DSP5_EXTENDED_OBJECT_1, &read_tmp);
	if (ret < 0)
		return ret;
	read = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_DSP5_EXTENDED_OBJECT_2, &read_tmp);
	if (ret < 0)
		return ret;
	read = read - (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_DSP5_EXTENDED_OBJECT_3, &read_tmp);
	if (ret < 0)
		return ret;
	read = read - (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_DSP5_EXTENDED_OBJECT_4, &read_tmp);
	if (ret < 0)
		return ret;
	read = (read + (s16)read_tmp) / 2;

	ret = regmap_read(regmap, MLX90632_RAM_DSP5_EXTENDED_OBJECT_5, &read_tmp);
	if (ret < 0)
		return ret;
	read = read + (s16)read_tmp;

	ret = regmap_read(regmap, MLX90632_RAM_DSP5_EXTENDED_OBJECT_6, &read_tmp);
	if (ret < 0)
		return ret;
	read = read + (s16)read_tmp;

	if (read > S16_MAX || read < S16_MIN)
		return -ERANGE;

	*object_new_raw = read;

	return 0;
}

static int mlx90632_read_all_channel_extended(struct mlx90632_data *data, s16 *object_new_raw,
					      s16 *ambient_new_raw, s16 *ambient_old_raw)
{
	s32 ret, meas;

	mutex_lock(&data->lock);
	ret = mlx90632_set_meas_type(data->regmap, MLX90632_MTYP_EXTENDED);
	if (ret < 0)
		goto read_unlock;

	ret = read_poll_timeout(mlx90632_perform_measurement, meas, meas == 19,
				50000, 800000, false, data);
	if (ret != 0)
		goto read_unlock;

	ret = mlx90632_read_object_raw_extended(data->regmap, object_new_raw);
	if (ret < 0)
		goto read_unlock;

	ret = mlx90632_read_ambient_raw_extended(data->regmap, ambient_new_raw, ambient_old_raw);

read_unlock:
	(void) mlx90632_set_meas_type(data->regmap, MLX90632_MTYP_MEDICAL);

	mutex_unlock(&data->lock);
	return ret;
}

static int mlx90632_read_ee_register(struct regmap *regmap, u16 reg_lsb,
				     s32 *reg_value)
{
	s32 ret;
	unsigned int read;
	u32 value;

	ret = regmap_read(regmap, reg_lsb, &read);
	if (ret < 0)
		return ret;

	value = read;

	ret = regmap_read(regmap, reg_lsb + 1, &read);
	if (ret < 0)
		return ret;

	*reg_value = (read << 16) | (value & 0xffff);

	return 0;
}

static s64 mlx90632_preprocess_temp_amb(s16 ambient_new_raw,
					s16 ambient_old_raw, s16 Gb)
{
	s64 VR_Ta, kGb, tmp;

	kGb = ((s64)Gb * 1000LL) >> 10ULL;
	VR_Ta = (s64)ambient_old_raw * 1000000LL +
		kGb * div64_s64(((s64)ambient_new_raw * 1000LL),
			(MLX90632_REF_3));
	tmp = div64_s64(
			 div64_s64(((s64)ambient_new_raw * 1000000000000LL),
				   (MLX90632_REF_3)), VR_Ta);
	return div64_s64(tmp << 19ULL, 1000LL);
}

static s64 mlx90632_preprocess_temp_obj(s16 object_new_raw, s16 object_old_raw,
					s16 ambient_new_raw,
					s16 ambient_old_raw, s16 Ka)
{
	s64 VR_IR, kKa, tmp;

	kKa = ((s64)Ka * 1000LL) >> 10ULL;
	VR_IR = (s64)ambient_old_raw * 1000000LL +
		kKa * div64_s64(((s64)ambient_new_raw * 1000LL),
			(MLX90632_REF_3));
	tmp = div64_s64(
			div64_s64(((s64)((object_new_raw + object_old_raw) / 2)
				   * 1000000000000LL), (MLX90632_REF_12)),
			VR_IR);
	return div64_s64((tmp << 19ULL), 1000LL);
}

static s64 mlx90632_preprocess_temp_obj_extended(s16 object_new_raw, s16 ambient_new_raw,
						 s16 ambient_old_raw, s16 Ka)
{
	s64 VR_IR, kKa, tmp;

	kKa = ((s64)Ka * 1000LL) >> 10ULL;
	VR_IR = (s64)ambient_old_raw * 1000000LL +
		kKa * div64_s64((s64)ambient_new_raw * 1000LL,
				MLX90632_REF_3);
	tmp = div64_s64(
			div64_s64((s64) object_new_raw * 1000000000000LL, MLX90632_REF_12),
			VR_IR);
	return div64_s64(tmp << 19ULL, 1000LL);
}

static s32 mlx90632_calc_temp_ambient(s16 ambient_new_raw, s16 ambient_old_raw,
				      s32 P_T, s32 P_R, s32 P_G, s32 P_O, s16 Gb)
{
	s64 Asub, Bsub, Ablock, Bblock, Cblock, AMB, sum;

	AMB = mlx90632_preprocess_temp_amb(ambient_new_raw, ambient_old_raw,
					   Gb);
	Asub = ((s64)P_T * 10000000000LL) >> 44ULL;
	Bsub = AMB - (((s64)P_R * 1000LL) >> 8ULL);
	Ablock = Asub * (Bsub * Bsub);
	Bblock = (div64_s64(Bsub * 10000000LL, P_G)) << 20ULL;
	Cblock = ((s64)P_O * 10000000000LL) >> 8ULL;

	sum = div64_s64(Ablock, 1000000LL) + Bblock + Cblock;

	return div64_s64(sum, 10000000LL);
}

static s32 mlx90632_calc_temp_object_iteration(s32 prev_object_temp, s64 object,
					       s64 TAdut, s64 TAdut4, s32 Fa, s32 Fb,
					       s32 Ga, s16 Ha, s16 Hb,
					       u16 emissivity)
{
	s64 calcedKsTO, calcedKsTA, ir_Alpha, Alpha_corr;
	s64 Ha_customer, Hb_customer;

	Ha_customer = ((s64)Ha * 1000000LL) >> 14ULL;
	Hb_customer = ((s64)Hb * 100) >> 10ULL;

	calcedKsTO = ((s64)((s64)Ga * (prev_object_temp - 25 * 1000LL)
			     * 1000LL)) >> 36LL;
	calcedKsTA = ((s64)(Fb * (TAdut - 25 * 1000000LL))) >> 36LL;
	Alpha_corr = div64_s64((((s64)(Fa * 10000000000LL) >> 46LL)
				* Ha_customer), 1000LL);
	Alpha_corr *= ((s64)(1 * 1000000LL + calcedKsTO + calcedKsTA));
	Alpha_corr = emissivity * div64_s64(Alpha_corr, 100000LL);
	Alpha_corr = div64_s64(Alpha_corr, 1000LL);
	ir_Alpha = div64_s64((s64)object * 10000000LL, Alpha_corr);

	return (int_sqrt64(int_sqrt64(ir_Alpha * 1000000000000LL + TAdut4))
		- 27315 - Hb_customer) * 10;
}

static s64 mlx90632_calc_ta4(s64 TAdut, s64 scale)
{
	return (div64_s64(TAdut, scale) + 27315) *
		(div64_s64(TAdut, scale) + 27315) *
		(div64_s64(TAdut, scale) + 27315) *
		(div64_s64(TAdut, scale) + 27315);
}

static s32 mlx90632_calc_temp_object(s64 object, s64 ambient, s32 Ea, s32 Eb,
				     s32 Fa, s32 Fb, s32 Ga, s16 Ha, s16 Hb,
				     u16 tmp_emi)
{
	s64 kTA, kTA0, TAdut, TAdut4;
	s64 temp = 25000;
	s8 i;

	kTA = (Ea * 1000LL) >> 16LL;
	kTA0 = (Eb * 1000LL) >> 8LL;
	TAdut = div64_s64(((ambient - kTA0) * 1000000LL), kTA) + 25 * 1000000LL;
	TAdut4 = mlx90632_calc_ta4(TAdut, 10000LL);

	/* Iterations of calculation as described in datasheet */
	for (i = 0; i < 5; ++i) {
		temp = mlx90632_calc_temp_object_iteration(temp, object, TAdut, TAdut4,
							   Fa, Fb, Ga, Ha, Hb,
							   tmp_emi);
	}
	return temp;
}

static s32 mlx90632_calc_temp_object_extended(s64 object, s64 ambient, s64 reflected,
					      s32 Ea, s32 Eb, s32 Fa, s32 Fb, s32 Ga,
					      s16 Ha, s16 Hb, u16 tmp_emi)
{
	s64 kTA, kTA0, TAdut, TAdut4, Tr4, TaTr4;
	s64 temp = 25000;
	s8 i;

	kTA = (Ea * 1000LL) >> 16LL;
	kTA0 = (Eb * 1000LL) >> 8LL;
	TAdut = div64_s64((ambient - kTA0) * 1000000LL, kTA) + 25 * 1000000LL;
	Tr4 = mlx90632_calc_ta4(reflected, 10);
	TAdut4 = mlx90632_calc_ta4(TAdut, 10000LL);
	TaTr4 = Tr4 - div64_s64(Tr4 - TAdut4, tmp_emi) * 1000;

	/* Iterations of calculation as described in datasheet */
	for (i = 0; i < 5; ++i) {
		temp = mlx90632_calc_temp_object_iteration(temp, object, TAdut, TaTr4,
							   Fa / 2, Fb, Ga, Ha, Hb,
							   tmp_emi);
	}

	return temp;
}

static int mlx90632_calc_object_dsp105(struct mlx90632_data *data, int *val)
{
	s32 ret;
	s32 Ea, Eb, Fa, Fb, Ga;
	unsigned int read_tmp;
	s16 Ha, Hb, Gb, Ka;
	s16 ambient_new_raw, ambient_old_raw, object_new_raw, object_old_raw;
	s64 object, ambient;

	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_Ea, &Ea);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_Eb, &Eb);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_Fa, &Fa);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_Fb, &Fb);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_Ga, &Ga);
	if (ret < 0)
		return ret;
	ret = regmap_read(data->regmap, MLX90632_EE_Ha, &read_tmp);
	if (ret < 0)
		return ret;
	Ha = (s16)read_tmp;
	ret = regmap_read(data->regmap, MLX90632_EE_Hb, &read_tmp);
	if (ret < 0)
		return ret;
	Hb = (s16)read_tmp;
	ret = regmap_read(data->regmap, MLX90632_EE_Gb, &read_tmp);
	if (ret < 0)
		return ret;
	Gb = (s16)read_tmp;
	ret = regmap_read(data->regmap, MLX90632_EE_Ka, &read_tmp);
	if (ret < 0)
		return ret;
	Ka = (s16)read_tmp;

	ret = mlx90632_read_all_channel(data,
					&ambient_new_raw, &ambient_old_raw,
					&object_new_raw, &object_old_raw);
	if (ret < 0)
		return ret;

	if (object_new_raw > MLX90632_EXTENDED_LIMIT &&
	    data->mtyp == MLX90632_MTYP_EXTENDED) {
		ret = mlx90632_read_all_channel_extended(data, &object_new_raw,
							 &ambient_new_raw, &ambient_old_raw);
		if (ret < 0)
			return ret;

		/* Use extended mode calculations */
		ambient = mlx90632_preprocess_temp_amb(ambient_new_raw,
						       ambient_old_raw, Gb);
		object = mlx90632_preprocess_temp_obj_extended(object_new_raw,
							       ambient_new_raw,
							       ambient_old_raw, Ka);
		*val = mlx90632_calc_temp_object_extended(object, ambient,
							  data->object_ambient_temperature,
							  Ea, Eb, Fa, Fb, Ga,
							  Ha, Hb, data->emissivity);
		return 0;
	}

	ambient = mlx90632_preprocess_temp_amb(ambient_new_raw,
					       ambient_old_raw, Gb);
	object = mlx90632_preprocess_temp_obj(object_new_raw,
					      object_old_raw,
					      ambient_new_raw,
					      ambient_old_raw, Ka);

	*val = mlx90632_calc_temp_object(object, ambient, Ea, Eb, Fa, Fb, Ga,
					 Ha, Hb, data->emissivity);
	return 0;
}

static int mlx90632_calc_ambient_dsp105(struct mlx90632_data *data, int *val)
{
	s32 ret;
	unsigned int read_tmp;
	s32 PT, PR, PG, PO;
	s16 Gb;
	s16 ambient_new_raw, ambient_old_raw;

	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_P_R, &PR);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_P_G, &PG);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_P_T, &PT);
	if (ret < 0)
		return ret;
	ret = mlx90632_read_ee_register(data->regmap, MLX90632_EE_P_O, &PO);
	if (ret < 0)
		return ret;
	ret = regmap_read(data->regmap, MLX90632_EE_Gb, &read_tmp);
	if (ret < 0)
		return ret;
	Gb = (s16)read_tmp;

	ret = mlx90632_read_ambient_raw(data->regmap, &ambient_new_raw,
					&ambient_old_raw);
	if (ret < 0)
		return ret;
	*val = mlx90632_calc_temp_ambient(ambient_new_raw, ambient_old_raw,
					  PT, PR, PG, PO, Gb);
	return ret;
}

static int mlx90632_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *channel, int *val,
			     int *val2, long mask)
{
	struct mlx90632_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (channel->channel2) {
		case IIO_MOD_TEMP_AMBIENT:
			ret = mlx90632_calc_ambient_dsp105(data, val);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT;
		case IIO_MOD_TEMP_OBJECT:
			ret = mlx90632_calc_object_dsp105(data, val);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBEMISSIVITY:
		if (data->emissivity == 1000) {
			*val = 1;
			*val2 = 0;
		} else {
			*val = 0;
			*val2 = data->emissivity * 1000;
		}
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBAMBIENT:
		*val = data->object_ambient_temperature;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int mlx90632_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *channel, int val,
			      int val2, long mask)
{
	struct mlx90632_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBEMISSIVITY:
		/* Confirm we are within 0 and 1.0 */
		if (val < 0 || val2 < 0 || val > 1 ||
		    (val == 1 && val2 != 0))
			return -EINVAL;
		data->emissivity = val * 1000 + val2 / 1000;
		return 0;
	case IIO_CHAN_INFO_CALIBAMBIENT:
		data->object_ambient_temperature = val;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec mlx90632_channels[] = {
	{
		.type = IIO_TEMP,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_AMBIENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
	{
		.type = IIO_TEMP,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_OBJECT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
			BIT(IIO_CHAN_INFO_CALIBEMISSIVITY) | BIT(IIO_CHAN_INFO_CALIBAMBIENT),
	},
};

static const struct iio_info mlx90632_info = {
	.read_raw = mlx90632_read_raw,
	.write_raw = mlx90632_write_raw,
};

static int mlx90632_sleep(struct mlx90632_data *data)
{
	regcache_mark_dirty(data->regmap);

	dev_dbg(&data->client->dev, "Requesting sleep");
	return mlx90632_pwr_set_sleep_step(data->regmap);
}

static int mlx90632_wakeup(struct mlx90632_data *data)
{
	int ret;

	ret = regcache_sync(data->regmap);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed to sync regmap registers: %d\n", ret);
		return ret;
	}

	dev_dbg(&data->client->dev, "Requesting wake-up\n");
	return mlx90632_pwr_continuous(data->regmap);
}

static int mlx90632_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct mlx90632_data *mlx90632;
	struct regmap *regmap;
	int ret;
	unsigned int read;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*mlx90632));
	if (!indio_dev) {
		dev_err(&client->dev, "Failed to allocate device\n");
		return -ENOMEM;
	}

	regmap = devm_regmap_init_i2c(client, &mlx90632_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&client->dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	mlx90632 = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	mlx90632->client = client;
	mlx90632->regmap = regmap;
	mlx90632->mtyp = MLX90632_MTYP_MEDICAL;

	mutex_init(&mlx90632->lock);
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mlx90632_info;
	indio_dev->channels = mlx90632_channels;
	indio_dev->num_channels = ARRAY_SIZE(mlx90632_channels);

	ret = mlx90632_wakeup(mlx90632);
	if (ret < 0) {
		dev_err(&client->dev, "Wakeup failed: %d\n", ret);
		return ret;
	}

	ret = regmap_read(mlx90632->regmap, MLX90632_EE_VERSION, &read);
	if (ret < 0) {
		dev_err(&client->dev, "read of version failed: %d\n", ret);
		return ret;
	}
	read = read & MLX90632_ID_MASK;
	if (read == MLX90632_ID_MEDICAL) {
		dev_dbg(&client->dev,
			"Detected Medical EEPROM calibration %x\n", read);
	} else if (read == MLX90632_ID_CONSUMER) {
		dev_dbg(&client->dev,
			"Detected Consumer EEPROM calibration %x\n", read);
	} else if (read == MLX90632_ID_EXTENDED) {
		dev_dbg(&client->dev,
			"Detected Extended range EEPROM calibration %x\n", read);
		mlx90632->mtyp = MLX90632_MTYP_EXTENDED;
	} else if ((read & MLX90632_DSP_MASK) == MLX90632_DSP_VERSION) {
		dev_dbg(&client->dev,
			"Detected Unknown EEPROM calibration %x\n", read);
	} else {
		dev_err(&client->dev,
			"Wrong DSP version %x (expected %x)\n",
			read, MLX90632_DSP_VERSION);
		return -EPROTONOSUPPORT;
	}

	mlx90632->emissivity = 1000;
	mlx90632->object_ambient_temperature = 25000; /* 25 degrees milliCelsius */

	pm_runtime_disable(&client->dev);
	ret = pm_runtime_set_active(&client->dev);
	if (ret < 0) {
		mlx90632_sleep(mlx90632);
		return ret;
	}
	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, MLX90632_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	return iio_device_register(indio_dev);
}

static void mlx90632_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct mlx90632_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	mlx90632_sleep(data);
}

static const struct i2c_device_id mlx90632_id[] = {
	{ "mlx90632", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mlx90632_id);

static const struct of_device_id mlx90632_of_match[] = {
	{ .compatible = "melexis,mlx90632" },
	{ }
};
MODULE_DEVICE_TABLE(of, mlx90632_of_match);

static int __maybe_unused mlx90632_pm_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mlx90632_data *data = iio_priv(indio_dev);

	return mlx90632_sleep(data);
}

static int __maybe_unused mlx90632_pm_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mlx90632_data *data = iio_priv(indio_dev);

	return mlx90632_wakeup(data);
}

static UNIVERSAL_DEV_PM_OPS(mlx90632_pm_ops, mlx90632_pm_suspend,
			    mlx90632_pm_resume, NULL);

static struct i2c_driver mlx90632_driver = {
	.driver = {
		.name	= "mlx90632",
		.of_match_table = mlx90632_of_match,
		.pm	= &mlx90632_pm_ops,
	},
	.probe = mlx90632_probe,
	.remove = mlx90632_remove,
	.id_table = mlx90632_id,
};
module_i2c_driver(mlx90632_driver);

MODULE_AUTHOR("Crt Mori <cmo@melexis.com>");
MODULE_DESCRIPTION("Melexis MLX90632 contactless Infra Red temperature sensor driver");
MODULE_LICENSE("GPL v2");
