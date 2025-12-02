// SPDX-License-Identifier: GPL-2.0
/*
 * mlx90635.c - Melexis MLX90635 contactless IR temperature sensor
 *
 * Copyright (c) 2023 Melexis <cmo@melexis.com>
 *
 * Driver for the Melexis MLX90635 I2C 16-bit IR thermopile sensor
 */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>

/* Memory sections addresses */
#define MLX90635_ADDR_RAM	0x0000 /* Start address of ram */
#define MLX90635_ADDR_EEPROM	0x0018 /* Start address of user eeprom */

/* EEPROM addresses - used at startup */
#define MLX90635_EE_I2C_CFG	0x0018 /* I2C address register initial value */
#define MLX90635_EE_CTRL1	0x001A /* Control register1 initial value */
#define MLX90635_EE_CTRL2	0x001C /* Control register2 initial value */

#define MLX90635_EE_Ha		0x001E /* Ha customer calib value reg 16bit */
#define MLX90635_EE_Hb		0x0020 /* Hb customer calib value reg 16bit */
#define MLX90635_EE_Fa		0x0026 /* Fa calibration register 32bit */
#define MLX90635_EE_FASCALE	0x002A /* Scaling coefficient for Fa register 16bit */
#define MLX90635_EE_Ga		0x002C /* Ga calibration register 16bit */
#define MLX90635_EE_Fb		0x002E /* Fb calibration register 16bit */
#define MLX90635_EE_Ea		0x0030 /* Ea calibration register 32bit */
#define MLX90635_EE_Eb		0x0034 /* Eb calibration register 32bit */
#define MLX90635_EE_P_G		0x0038 /* P_G calibration register 16bit */
#define MLX90635_EE_P_O		0x003A /* P_O calibration register 16bit */
#define MLX90635_EE_Aa		0x003C /* Aa calibration register 16bit */
#define MLX90635_EE_VERSION	0x003E /* Version bits 4:7 and 12:15 */
#define MLX90635_EE_Gb		0x0040 /* Gb calibration register 16bit */

/* Device status register - volatile */
#define MLX90635_REG_STATUS	0x0000
#define   MLX90635_STAT_BUSY BIT(6) /* Device busy indicator */
#define   MLX90635_STAT_BRST BIT(5) /* Brown out reset indicator */
#define   MLX90635_STAT_CYCLE_POS GENMASK(4, 2) /* Data position */
#define   MLX90635_STAT_END_CONV BIT(1) /* End of conversion indicator */
#define   MLX90635_STAT_DATA_RDY BIT(0) /* Data ready indicator */

/* EEPROM control register address - volatile */
#define MLX90635_REG_EE		0x000C
#define   MLX90635_EE_ACTIVE BIT(4) /* Power-on EEPROM */
#define   MLX90635_EE_BUSY_MASK	BIT(15)

#define MLX90635_REG_CMD	0x0010 /* Command register address */

/* Control register1 address - volatile */
#define MLX90635_REG_CTRL1	0x0014
#define   MLX90635_CTRL1_REFRESH_RATE_MASK GENMASK(2, 0)
#define   MLX90635_CTRL1_RES_CTRL_MASK GENMASK(4, 3)
#define   MLX90635_CTRL1_TABLE_MASK BIT(15) /* Table select */

/* Control register2 address - volatile */
#define   MLX90635_REG_CTRL2	0x0016
#define   MLX90635_CTRL2_BURST_CNT_MASK GENMASK(10, 6) /* Burst count */
#define   MLX90635_CTRL2_MODE_MASK GENMASK(12, 11) /* Power mode */
#define   MLX90635_CTRL2_SOB_MASK BIT(15)

/* PowerModes statuses */
#define MLX90635_PWR_STATUS_HALT 0
#define MLX90635_PWR_STATUS_SLEEP_STEP 1
#define MLX90635_PWR_STATUS_STEP 2
#define MLX90635_PWR_STATUS_CONTINUOUS 3

/* Measurement data addresses */
#define MLX90635_RESULT_1   0x0002
#define MLX90635_RESULT_2   0x0004
#define MLX90635_RESULT_3   0x0006
#define MLX90635_RESULT_4   0x0008
#define MLX90635_RESULT_5   0x000A

/* Timings (ms) */
#define MLX90635_TIMING_RST_MIN 200 /* Minimum time after addressed reset command */
#define MLX90635_TIMING_RST_MAX 250 /* Maximum time after addressed reset command */
#define MLX90635_TIMING_POLLING 10000 /* Time between bit polling*/
#define MLX90635_TIMING_EE_ACTIVE_MIN 100 /* Minimum time after activating the EEPROM for read */
#define MLX90635_TIMING_EE_ACTIVE_MAX 150 /* Maximum time after activating the EEPROM for read */

/* Magic constants */
#define MLX90635_ID_DSPv1 0x01 /* EEPROM DSP version */
#define MLX90635_RESET_CMD  0x0006 /* Reset sensor (address or global) */
#define MLX90635_MAX_MEAS_NUM   31 /* Maximum number of measurements in list */
#define MLX90635_PTAT_DIV 12   /* Used to divide the PTAT value in pre-processing */
#define MLX90635_IR_DIV 24   /* Used to divide the IR value in pre-processing */
#define MLX90635_SLEEP_DELAY_MS 6000 /* Autosleep delay */
#define MLX90635_MEAS_MAX_TIME 2000 /* Max measurement time in ms for the lowest refresh rate */
#define MLX90635_READ_RETRIES 100 /* Number of read retries before quitting with timeout error */
#define MLX90635_VERSION_MASK (GENMASK(15, 12) | GENMASK(7, 4))
#define MLX90635_DSP_VERSION(reg) (((reg & GENMASK(14, 12)) >> 9) | ((reg & GENMASK(6, 4)) >> 4))
#define MLX90635_DSP_FIXED BIT(15)


/**
 * struct mlx90635_data - private data for the MLX90635 device
 * @client: I2C client of the device
 * @lock: Internal mutex because multiple reads are needed for single triggered
 *	  measurement to ensure data consistency
 * @regmap: Regmap of the device registers
 * @regmap_ee: Regmap of the device EEPROM which can be cached
 * @emissivity: Object emissivity from 0 to 1000 where 1000 = 1
 * @regulator: Regulator of the device
 * @powerstatus: Current POWER status of the device
 * @interaction_ts: Timestamp of the last temperature read that is used
 *		    for power management in jiffies
 */
struct mlx90635_data {
	struct i2c_client *client;
	struct mutex lock;
	struct regmap *regmap;
	struct regmap *regmap_ee;
	u16 emissivity;
	struct regulator *regulator;
	int powerstatus;
	unsigned long interaction_ts;
};

static const struct regmap_range mlx90635_volatile_reg_range[] = {
	regmap_reg_range(MLX90635_REG_STATUS, MLX90635_REG_STATUS),
	regmap_reg_range(MLX90635_RESULT_1, MLX90635_RESULT_5),
	regmap_reg_range(MLX90635_REG_EE, MLX90635_REG_EE),
	regmap_reg_range(MLX90635_REG_CMD, MLX90635_REG_CMD),
	regmap_reg_range(MLX90635_REG_CTRL1, MLX90635_REG_CTRL2),
};

static const struct regmap_access_table mlx90635_volatile_regs_tbl = {
	.yes_ranges = mlx90635_volatile_reg_range,
	.n_yes_ranges = ARRAY_SIZE(mlx90635_volatile_reg_range),
};

static const struct regmap_range mlx90635_read_reg_range[] = {
	regmap_reg_range(MLX90635_REG_STATUS, MLX90635_REG_STATUS),
	regmap_reg_range(MLX90635_RESULT_1, MLX90635_RESULT_5),
	regmap_reg_range(MLX90635_REG_EE, MLX90635_REG_EE),
	regmap_reg_range(MLX90635_REG_CMD, MLX90635_REG_CMD),
	regmap_reg_range(MLX90635_REG_CTRL1, MLX90635_REG_CTRL2),
};

static const struct regmap_access_table mlx90635_readable_regs_tbl = {
	.yes_ranges = mlx90635_read_reg_range,
	.n_yes_ranges = ARRAY_SIZE(mlx90635_read_reg_range),
};

static const struct regmap_range mlx90635_no_write_reg_range[] = {
	regmap_reg_range(MLX90635_RESULT_1, MLX90635_RESULT_5),
};

static const struct regmap_access_table mlx90635_writeable_regs_tbl = {
	.no_ranges = mlx90635_no_write_reg_range,
	.n_no_ranges = ARRAY_SIZE(mlx90635_no_write_reg_range),
};

static const struct regmap_config mlx90635_regmap = {
	.name = "mlx90635-registers",
	.reg_stride = 1,
	.reg_bits = 16,
	.val_bits = 16,

	.volatile_table = &mlx90635_volatile_regs_tbl,
	.rd_table = &mlx90635_readable_regs_tbl,
	.wr_table = &mlx90635_writeable_regs_tbl,

	.use_single_read = true,
	.use_single_write = true,
	.can_multi_write = false,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.cache_type = REGCACHE_RBTREE,
};

static const struct regmap_range mlx90635_read_ee_range[] = {
	regmap_reg_range(MLX90635_EE_I2C_CFG, MLX90635_EE_CTRL2),
	regmap_reg_range(MLX90635_EE_Ha, MLX90635_EE_Gb),
};

static const struct regmap_access_table mlx90635_readable_ees_tbl = {
	.yes_ranges = mlx90635_read_ee_range,
	.n_yes_ranges = ARRAY_SIZE(mlx90635_read_ee_range),
};

static const struct regmap_range mlx90635_no_write_ee_range[] = {
	regmap_reg_range(MLX90635_ADDR_EEPROM, MLX90635_EE_Gb),
};

static const struct regmap_access_table mlx90635_writeable_ees_tbl = {
	.no_ranges = mlx90635_no_write_ee_range,
	.n_no_ranges = ARRAY_SIZE(mlx90635_no_write_ee_range),
};

static const struct regmap_config mlx90635_regmap_ee = {
	.name = "mlx90635-eeprom",
	.reg_stride = 1,
	.reg_bits = 16,
	.val_bits = 16,

	.volatile_table = NULL,
	.rd_table = &mlx90635_readable_ees_tbl,
	.wr_table = &mlx90635_writeable_ees_tbl,

	.use_single_read = true,
	.use_single_write = true,
	.can_multi_write = false,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.cache_type = REGCACHE_RBTREE,
};

/**
 * mlx90635_reset_delay() - Give the mlx90635 some time to reset properly
 * If this is not done, the following I2C command(s) will not be accepted.
 */
static void mlx90635_reset_delay(void)
{
	usleep_range(MLX90635_TIMING_RST_MIN, MLX90635_TIMING_RST_MAX);
}

static int mlx90635_pwr_sleep_step(struct mlx90635_data *data)
{
	int ret;

	if (data->powerstatus == MLX90635_PWR_STATUS_SLEEP_STEP)
		return 0;

	ret = regmap_write_bits(data->regmap, MLX90635_REG_CTRL2, MLX90635_CTRL2_MODE_MASK,
				FIELD_PREP(MLX90635_CTRL2_MODE_MASK, MLX90635_PWR_STATUS_SLEEP_STEP));
	if (ret < 0)
		return ret;

	data->powerstatus = MLX90635_PWR_STATUS_SLEEP_STEP;
	return 0;
}

static int mlx90635_pwr_continuous(struct mlx90635_data *data)
{
	int ret;

	if (data->powerstatus == MLX90635_PWR_STATUS_CONTINUOUS)
		return 0;

	ret = regmap_write_bits(data->regmap, MLX90635_REG_CTRL2, MLX90635_CTRL2_MODE_MASK,
				FIELD_PREP(MLX90635_CTRL2_MODE_MASK, MLX90635_PWR_STATUS_CONTINUOUS));
	if (ret < 0)
		return ret;

	data->powerstatus = MLX90635_PWR_STATUS_CONTINUOUS;
	return 0;
}

static int mlx90635_read_ee_register(struct regmap *regmap, u16 reg_lsb,
				     s32 *reg_value)
{
	unsigned int read;
	u32 value;
	int ret;

	ret = regmap_read(regmap, reg_lsb + 2, &read);
	if (ret < 0)
		return ret;

	value = read;

	ret = regmap_read(regmap, reg_lsb, &read);
	if (ret < 0)
		return ret;

	*reg_value = (read << 16) | (value & 0xffff);

	return 0;
}

static int mlx90635_read_ee_ambient(struct regmap *regmap, s16 *PG, s16 *PO, s16 *Gb)
{
	unsigned int read_tmp;
	int ret;

	ret = regmap_read(regmap, MLX90635_EE_P_O, &read_tmp);
	if (ret < 0)
		return ret;
	*PO = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90635_EE_P_G, &read_tmp);
	if (ret < 0)
		return ret;
	*PG = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90635_EE_Gb, &read_tmp);
	if (ret < 0)
		return ret;
	*Gb = (u16)read_tmp;

	return 0;
}

static int mlx90635_read_ee_object(struct regmap *regmap, u32 *Ea, u32 *Eb, u32 *Fa, s16 *Fb,
				   s16 *Ga, s16 *Gb, s16 *Ha, s16 *Hb, u16 *Fa_scale)
{
	unsigned int read_tmp;
	int ret;

	ret = mlx90635_read_ee_register(regmap, MLX90635_EE_Ea, Ea);
	if (ret < 0)
		return ret;

	ret = mlx90635_read_ee_register(regmap, MLX90635_EE_Eb, Eb);
	if (ret < 0)
		return ret;

	ret = mlx90635_read_ee_register(regmap, MLX90635_EE_Fa, Fa);
	if (ret < 0)
		return ret;

	ret = regmap_read(regmap, MLX90635_EE_Ha, &read_tmp);
	if (ret < 0)
		return ret;
	*Ha = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90635_EE_Hb, &read_tmp);
	if (ret < 0)
		return ret;
	*Hb = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90635_EE_Ga, &read_tmp);
	if (ret < 0)
		return ret;
	*Ga = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90635_EE_Gb, &read_tmp);
	if (ret < 0)
		return ret;
	*Gb = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90635_EE_Fb, &read_tmp);
	if (ret < 0)
		return ret;
	*Fb = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90635_EE_FASCALE, &read_tmp);
	if (ret < 0)
		return ret;
	*Fa_scale = (u16)read_tmp;

	return 0;
}

static int mlx90635_calculate_dataset_ready_time(struct mlx90635_data *data, int *refresh_time)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(data->regmap, MLX90635_REG_CTRL1, &reg);
	if (ret < 0)
		return ret;

	*refresh_time = 2 * (MLX90635_MEAS_MAX_TIME >> FIELD_GET(MLX90635_CTRL1_REFRESH_RATE_MASK, reg)) + 80;

	return 0;
}

static int mlx90635_perform_measurement_burst(struct mlx90635_data *data)
{
	unsigned int reg_status;
	int refresh_time;
	int ret;

	ret = regmap_write_bits(data->regmap, MLX90635_REG_STATUS,
				MLX90635_STAT_END_CONV, MLX90635_STAT_END_CONV);
	if (ret < 0)
		return ret;

	ret = mlx90635_calculate_dataset_ready_time(data, &refresh_time);
	if (ret < 0)
		return ret;

	ret = regmap_write_bits(data->regmap, MLX90635_REG_CTRL2,
				FIELD_PREP(MLX90635_CTRL2_SOB_MASK, 1),
				FIELD_PREP(MLX90635_CTRL2_SOB_MASK, 1));
	if (ret < 0)
		return ret;

	msleep(refresh_time); /* Wait minimum time for dataset to be ready */

	ret = regmap_read_poll_timeout(data->regmap, MLX90635_REG_STATUS, reg_status,
				       (!(reg_status & MLX90635_STAT_END_CONV)) == 0,
				       MLX90635_TIMING_POLLING, MLX90635_READ_RETRIES * 10000);
	if (ret < 0) {
		dev_err(&data->client->dev, "data not ready");
		return -ETIMEDOUT;
	}

	return 0;
}

static int mlx90635_read_ambient_raw(struct regmap *regmap,
				     s16 *ambient_new_raw, s16 *ambient_old_raw)
{
	unsigned int read_tmp;
	int ret;

	ret = regmap_read(regmap, MLX90635_RESULT_2, &read_tmp);
	if (ret < 0)
		return ret;
	*ambient_new_raw = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90635_RESULT_3, &read_tmp);
	if (ret < 0)
		return ret;
	*ambient_old_raw = (s16)read_tmp;

	return 0;
}

static int mlx90635_read_object_raw(struct regmap *regmap, s16 *object_raw)
{
	unsigned int read_tmp;
	s16 read;
	int ret;

	ret = regmap_read(regmap, MLX90635_RESULT_1, &read_tmp);
	if (ret < 0)
		return ret;

	read = (s16)read_tmp;

	ret = regmap_read(regmap, MLX90635_RESULT_4, &read_tmp);
	if (ret < 0)
		return ret;
	*object_raw = (read - (s16)read_tmp) / 2;

	return 0;
}

static int mlx90635_read_all_channel(struct mlx90635_data *data,
				     s16 *ambient_new_raw, s16 *ambient_old_raw,
				     s16 *object_raw)
{
	int ret;

	mutex_lock(&data->lock);
	if (data->powerstatus == MLX90635_PWR_STATUS_SLEEP_STEP) {
		/* Trigger measurement in Sleep Step mode */
		ret = mlx90635_perform_measurement_burst(data);
		if (ret < 0)
			goto read_unlock;
	}

	ret = mlx90635_read_ambient_raw(data->regmap, ambient_new_raw,
					ambient_old_raw);
	if (ret < 0)
		goto read_unlock;

	ret = mlx90635_read_object_raw(data->regmap, object_raw);
read_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static s64 mlx90635_preprocess_temp_amb(s16 ambient_new_raw,
					s16 ambient_old_raw, s16 Gb)
{
	s64 VR_Ta, kGb, tmp;

	kGb = ((s64)Gb * 1000LL) >> 10ULL;
	VR_Ta = (s64)ambient_old_raw * 1000000LL +
		kGb * div64_s64(((s64)ambient_new_raw * 1000LL),
			(MLX90635_PTAT_DIV));
	tmp = div64_s64(
			 div64_s64(((s64)ambient_new_raw * 1000000000000LL),
				   (MLX90635_PTAT_DIV)), VR_Ta);
	return div64_s64(tmp << 19ULL, 1000LL);
}

static s64 mlx90635_preprocess_temp_obj(s16 object_raw,
					s16 ambient_new_raw,
					s16 ambient_old_raw, s16 Gb)
{
	s64 VR_IR, kGb, tmp;

	kGb = ((s64)Gb * 1000LL) >> 10ULL;
	VR_IR = (s64)ambient_old_raw * 1000000LL +
		kGb * (div64_s64((s64)ambient_new_raw * 1000LL,
			MLX90635_PTAT_DIV));
	tmp = div64_s64(
			div64_s64((s64)(object_raw * 1000000LL),
				   MLX90635_IR_DIV) * 1000000LL,
			VR_IR);
	return div64_s64((tmp << 19ULL), 1000LL);
}

static s32 mlx90635_calc_temp_ambient(s16 ambient_new_raw, s16 ambient_old_raw,
				      u16 P_G, u16 P_O, s16 Gb)
{
	s64 kPG, kPO, AMB;

	AMB = mlx90635_preprocess_temp_amb(ambient_new_raw, ambient_old_raw,
					   Gb);
	kPG = ((s64)P_G * 1000000LL) >> 9ULL;
	kPO = AMB - (((s64)P_O * 1000LL) >> 1ULL);

	return 30 * 1000LL + div64_s64(kPO * 1000000LL, kPG);
}

static s32 mlx90635_calc_temp_object_iteration(s32 prev_object_temp, s64 object,
					       s64 TAdut, s64 TAdut4, s16 Ga,
					       u32 Fa, u16 Fa_scale, s16 Fb,
					       s16 Ha, s16 Hb, u16 emissivity)
{
	s64 calcedGa, calcedGb, calcedFa, Alpha_corr;
	s64 Ha_customer, Hb_customer;

	Ha_customer = ((s64)Ha * 1000000LL) >> 14ULL;
	Hb_customer = ((s64)Hb * 100) >> 10ULL;

	calcedGa = ((s64)((s64)Ga * (prev_object_temp - 35 * 1000LL)
			     * 1000LL)) >> 24LL;
	calcedGb = ((s64)(Fb * (TAdut - 30 * 1000000LL))) >> 24LL;

	Alpha_corr = ((s64)((s64)Fa * Ha_customer * 10000LL) >> Fa_scale);
	Alpha_corr *= ((s64)(1 * 1000000LL + calcedGa + calcedGb));

	Alpha_corr = div64_s64(Alpha_corr, 1000LL);
	Alpha_corr *= emissivity;
	Alpha_corr = div64_s64(Alpha_corr, 100LL);
	calcedFa = div64_s64((s64)object * 100000000000LL, Alpha_corr);

	return (int_sqrt64(int_sqrt64(calcedFa * 100000000LL + TAdut4))
		- 27315 - Hb_customer) * 10;
}

static s64 mlx90635_calc_ta4(s64 TAdut, s64 scale)
{
	return (div64_s64(TAdut, scale) + 27315) *
		(div64_s64(TAdut, scale) + 27315) *
		(div64_s64(TAdut, scale) + 27315) *
		(div64_s64(TAdut, scale) + 27315);
}

static s32 mlx90635_calc_temp_object(s64 object, s64 ambient, u32 Ea, u32 Eb,
				     s16 Ga, u32 Fa, u16 Fa_scale, s16 Fb, s16 Ha, s16 Hb,
				     u16 tmp_emi)
{
	s64 kTA, kTA0, TAdut, TAdut4;
	s64 temp = 35000;
	s8 i;

	kTA = (Ea * 1000LL) >> 16LL;
	kTA0 = (Eb * 1000LL) >> 8LL;
	TAdut = div64_s64(((ambient - kTA0) * 1000000LL), kTA) + 30 * 1000000LL;
	TAdut4 = mlx90635_calc_ta4(TAdut, 10000LL);

	/* Iterations of calculation as described in datasheet */
	for (i = 0; i < 5; ++i) {
		temp = mlx90635_calc_temp_object_iteration(temp, object, TAdut, TAdut4,
							   Ga, Fa, Fa_scale, Fb, Ha, Hb,
							   tmp_emi);
	}
	return temp;
}

static int mlx90635_calc_object(struct mlx90635_data *data, int *val)
{
	s16 ambient_new_raw, ambient_old_raw, object_raw;
	s16 Fb, Ga, Gb, Ha, Hb;
	s64 object, ambient;
	u32 Ea, Eb, Fa;
	u16 Fa_scale;
	int ret;

	ret = mlx90635_read_ee_object(data->regmap_ee, &Ea, &Eb, &Fa, &Fb, &Ga, &Gb, &Ha, &Hb, &Fa_scale);
	if (ret < 0)
		return ret;

	ret = mlx90635_read_all_channel(data,
					&ambient_new_raw, &ambient_old_raw,
					&object_raw);
	if (ret < 0)
		return ret;

	ambient = mlx90635_preprocess_temp_amb(ambient_new_raw,
					       ambient_old_raw, Gb);
	object = mlx90635_preprocess_temp_obj(object_raw,
					      ambient_new_raw,
					      ambient_old_raw, Gb);

	*val = mlx90635_calc_temp_object(object, ambient, Ea, Eb, Ga, Fa, Fa_scale, Fb,
					 Ha, Hb, data->emissivity);
	return 0;
}

static int mlx90635_calc_ambient(struct mlx90635_data *data, int *val)
{
	s16 ambient_new_raw, ambient_old_raw;
	s16 PG, PO, Gb;
	int ret;

	ret = mlx90635_read_ee_ambient(data->regmap_ee, &PG, &PO, &Gb);
	if (ret < 0)
		return ret;

	mutex_lock(&data->lock);
	if (data->powerstatus == MLX90635_PWR_STATUS_SLEEP_STEP) {
		ret = mlx90635_perform_measurement_burst(data);
		if (ret < 0)
			goto read_ambient_unlock;
	}

	ret = mlx90635_read_ambient_raw(data->regmap, &ambient_new_raw,
					&ambient_old_raw);
read_ambient_unlock:
	mutex_unlock(&data->lock);
	if (ret < 0)
		return ret;

	*val = mlx90635_calc_temp_ambient(ambient_new_raw, ambient_old_raw,
					  PG, PO, Gb);
	return ret;
}

static int mlx90635_get_refresh_rate(struct mlx90635_data *data,
				     unsigned int *refresh_rate)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(data->regmap, MLX90635_REG_CTRL1, &reg);
	if (ret < 0)
		return ret;

	*refresh_rate = FIELD_GET(MLX90635_CTRL1_REFRESH_RATE_MASK, reg);

	return 0;
}

static const struct {
	int val;
	int val2;
} mlx90635_freqs[] = {
	{ 0, 200000 },
	{ 0, 500000 },
	{ 0, 900000 },
	{ 1, 700000 },
	{ 3, 0 },
	{ 4, 800000 },
	{ 6, 900000 },
	{ 8, 900000 }
};

/**
 * mlx90635_pm_interaction_wakeup() - Measure time between user interactions to change powermode
 * @data: pointer to mlx90635_data object containing interaction_ts information
 *
 * Switch to continuous mode when interaction is faster than MLX90635_MEAS_MAX_TIME. Update the
 * interaction_ts for each function call with the jiffies to enable measurement between function
 * calls. Initial value of the interaction_ts needs to be set before this function call.
 */
static int mlx90635_pm_interaction_wakeup(struct mlx90635_data *data)
{
	unsigned long now;
	int ret;

	now = jiffies;
	if (time_in_range(now, data->interaction_ts,
			  data->interaction_ts +
			  msecs_to_jiffies(MLX90635_MEAS_MAX_TIME + 100))) {
		ret = mlx90635_pwr_continuous(data);
		if (ret < 0)
			return ret;
	}

	data->interaction_ts = now;

	return 0;
}

static int mlx90635_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *channel, int *val,
			     int *val2, long mask)
{
	struct mlx90635_data *data = iio_priv(indio_dev);
	int ret;
	int cr;

	pm_runtime_get_sync(&data->client->dev);
	ret = mlx90635_pm_interaction_wakeup(data);
	if (ret < 0)
		goto mlx90635_read_raw_pm;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (channel->channel2) {
		case IIO_MOD_TEMP_AMBIENT:
			ret = mlx90635_calc_ambient(data, val);
			if (ret < 0)
				goto mlx90635_read_raw_pm;

			ret = IIO_VAL_INT;
			break;
		case IIO_MOD_TEMP_OBJECT:
			ret = mlx90635_calc_object(data, val);
			if (ret < 0)
				goto mlx90635_read_raw_pm;

			ret = IIO_VAL_INT;
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	case IIO_CHAN_INFO_CALIBEMISSIVITY:
		if (data->emissivity == 1000) {
			*val = 1;
			*val2 = 0;
		} else {
			*val = 0;
			*val2 = data->emissivity * 1000;
		}
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = mlx90635_get_refresh_rate(data, &cr);
		if (ret < 0)
			goto mlx90635_read_raw_pm;

		*val = mlx90635_freqs[cr].val;
		*val2 = mlx90635_freqs[cr].val2;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

mlx90635_read_raw_pm:
	pm_runtime_put_autosuspend(&data->client->dev);
	return ret;
}

static int mlx90635_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *channel, int val,
			      int val2, long mask)
{
	struct mlx90635_data *data = iio_priv(indio_dev);
	int ret;
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBEMISSIVITY:
		/* Confirm we are within 0 and 1.0 */
		if (val < 0 || val2 < 0 || val > 1 ||
		    (val == 1 && val2 != 0))
			return -EINVAL;
		data->emissivity = val * 1000 + val2 / 1000;
		return 0;
	case IIO_CHAN_INFO_SAMP_FREQ:
		for (i = 0; i < ARRAY_SIZE(mlx90635_freqs); i++) {
			if (val == mlx90635_freqs[i].val &&
			    val2 == mlx90635_freqs[i].val2)
				break;
		}
		if (i == ARRAY_SIZE(mlx90635_freqs))
			return -EINVAL;

		ret = regmap_write_bits(data->regmap, MLX90635_REG_CTRL1,
					MLX90635_CTRL1_REFRESH_RATE_MASK, i);

		return ret;
	default:
		return -EINVAL;
	}
}

static int mlx90635_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (int *)mlx90635_freqs;
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = 2 * ARRAY_SIZE(mlx90635_freqs);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec mlx90635_channels[] = {
	{
		.type = IIO_TEMP,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_AMBIENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
	{
		.type = IIO_TEMP,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_OBJECT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
			BIT(IIO_CHAN_INFO_CALIBEMISSIVITY),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
};

static const struct iio_info mlx90635_info = {
	.read_raw = mlx90635_read_raw,
	.write_raw = mlx90635_write_raw,
	.read_avail = mlx90635_read_avail,
};

static void mlx90635_sleep(void *_data)
{
	struct mlx90635_data *data = _data;

	mlx90635_pwr_sleep_step(data);
}

static int mlx90635_suspend(struct mlx90635_data *data)
{
	return mlx90635_pwr_sleep_step(data);
}

static int mlx90635_wakeup(struct mlx90635_data *data)
{
	s16 Fb, Ga, Gb, Ha, Hb, PG, PO;
	unsigned int dsp_version;
	u32 Ea, Eb, Fa;
	u16 Fa_scale;
	int ret;

	regcache_cache_bypass(data->regmap_ee, false);
	regcache_cache_only(data->regmap_ee, false);
	regcache_cache_only(data->regmap, false);

	ret = mlx90635_pwr_continuous(data);
	if (ret < 0) {
		dev_err(&data->client->dev, "Switch to continuous mode failed\n");
		return ret;
	}
	ret = regmap_write_bits(data->regmap, MLX90635_REG_EE,
				MLX90635_EE_ACTIVE, MLX90635_EE_ACTIVE);
	if (ret < 0) {
		dev_err(&data->client->dev, "Powering EEPROM failed\n");
		return ret;
	}
	usleep_range(MLX90635_TIMING_EE_ACTIVE_MIN, MLX90635_TIMING_EE_ACTIVE_MAX);

	regcache_mark_dirty(data->regmap_ee);

	ret = regcache_sync(data->regmap_ee);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed to sync cache: %d\n", ret);
		return ret;
	}

	ret = mlx90635_read_ee_ambient(data->regmap_ee, &PG, &PO, &Gb);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed to read to cache Ambient coefficients EEPROM region: %d\n", ret);
		return ret;
	}

	ret = mlx90635_read_ee_object(data->regmap_ee, &Ea, &Eb, &Fa, &Fb, &Ga, &Gb, &Ha, &Hb, &Fa_scale);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed to read to cache Object coefficients EEPROM region: %d\n", ret);
		return ret;
	}

	ret = regmap_read(data->regmap_ee, MLX90635_EE_VERSION, &dsp_version);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed to read to cache of EEPROM version: %d\n", ret);
		return ret;
	}

	regcache_cache_only(data->regmap_ee, true);

	return ret;
}

static void mlx90635_disable_regulator(void *_data)
{
	struct mlx90635_data *data = _data;
	int ret;

	ret = regulator_disable(data->regulator);
	if (ret < 0)
		dev_err(regmap_get_device(data->regmap),
			"Failed to disable power regulator: %d\n", ret);
}

static int mlx90635_enable_regulator(struct mlx90635_data *data)
{
	int ret;

	ret = regulator_enable(data->regulator);
	if (ret < 0) {
		dev_err(regmap_get_device(data->regmap), "Failed to enable power regulator!\n");
		return ret;
	}

	mlx90635_reset_delay();

	return ret;
}

static int mlx90635_probe(struct i2c_client *client)
{
	struct mlx90635_data *mlx90635;
	struct iio_dev *indio_dev;
	unsigned int dsp_version;
	struct regmap *regmap;
	struct regmap *regmap_ee;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*mlx90635));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &mlx90635_regmap);
	if (IS_ERR(regmap))
		return dev_err_probe(&client->dev, PTR_ERR(regmap),
				     "failed to allocate regmap\n");

	regmap_ee = devm_regmap_init_i2c(client, &mlx90635_regmap_ee);
	if (IS_ERR(regmap_ee))
		return dev_err_probe(&client->dev, PTR_ERR(regmap_ee),
				     "failed to allocate EEPROM regmap\n");

	mlx90635 = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	mlx90635->client = client;
	mlx90635->regmap = regmap;
	mlx90635->regmap_ee = regmap_ee;
	mlx90635->powerstatus = MLX90635_PWR_STATUS_SLEEP_STEP;

	mutex_init(&mlx90635->lock);
	indio_dev->name = "mlx90635";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mlx90635_info;
	indio_dev->channels = mlx90635_channels;
	indio_dev->num_channels = ARRAY_SIZE(mlx90635_channels);

	mlx90635->regulator = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(mlx90635->regulator))
		return dev_err_probe(&client->dev, PTR_ERR(mlx90635->regulator),
				     "failed to get vdd regulator");

	ret = mlx90635_enable_regulator(mlx90635);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&client->dev, mlx90635_disable_regulator,
				       mlx90635);
	if (ret < 0)
		return ret;

	ret = mlx90635_wakeup(mlx90635);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret, "wakeup failed\n");

	ret = devm_add_action_or_reset(&client->dev, mlx90635_sleep, mlx90635);
	if (ret < 0)
		return ret;

	ret = regmap_read(mlx90635->regmap_ee, MLX90635_EE_VERSION, &dsp_version);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret, "read of version failed\n");

	dsp_version = dsp_version & MLX90635_VERSION_MASK;

	if (FIELD_GET(MLX90635_DSP_FIXED, dsp_version)) {
		if (MLX90635_DSP_VERSION(dsp_version) == MLX90635_ID_DSPv1) {
			dev_dbg(&client->dev,
				"Detected DSP v1 calibration %x\n", dsp_version);
		} else {
			dev_dbg(&client->dev,
				"Detected Unknown EEPROM calibration %lx\n",
				MLX90635_DSP_VERSION(dsp_version));
		}
	} else {
		return dev_err_probe(&client->dev, -EPROTONOSUPPORT,
			"Wrong fixed top bit %x (expected 0x8X0X)\n",
			dsp_version);
	}

	mlx90635->emissivity = 1000;
	mlx90635->interaction_ts = jiffies; /* Set initial value */

	pm_runtime_get_noresume(&client->dev);
	pm_runtime_set_active(&client->dev);

	ret = devm_pm_runtime_enable(&client->dev);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to enable powermanagement\n");

	pm_runtime_set_autosuspend_delay(&client->dev, MLX90635_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id mlx90635_id[] = {
	{ "mlx90635" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mlx90635_id);

static const struct of_device_id mlx90635_of_match[] = {
	{ .compatible = "melexis,mlx90635" },
	{ }
};
MODULE_DEVICE_TABLE(of, mlx90635_of_match);

static int mlx90635_pm_suspend(struct device *dev)
{
	struct mlx90635_data *data = iio_priv(dev_get_drvdata(dev));
	int ret;

	ret = mlx90635_suspend(data);
	if (ret < 0)
		return ret;

	ret = regulator_disable(data->regulator);
	if (ret < 0)
		dev_err(regmap_get_device(data->regmap),
			"Failed to disable power regulator: %d\n", ret);

	return ret;
}

static int mlx90635_pm_resume(struct device *dev)
{
	struct mlx90635_data *data = iio_priv(dev_get_drvdata(dev));
	int ret;

	ret = mlx90635_enable_regulator(data);
	if (ret < 0)
		return ret;

	return mlx90635_wakeup(data);
}

static int mlx90635_pm_runtime_suspend(struct device *dev)
{
	struct mlx90635_data *data = iio_priv(dev_get_drvdata(dev));

	return mlx90635_pwr_sleep_step(data);
}

static const struct dev_pm_ops mlx90635_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(mlx90635_pm_suspend, mlx90635_pm_resume)
	RUNTIME_PM_OPS(mlx90635_pm_runtime_suspend, NULL, NULL)
};

static struct i2c_driver mlx90635_driver = {
	.driver = {
		.name	= "mlx90635",
		.of_match_table = mlx90635_of_match,
		.pm	= pm_ptr(&mlx90635_pm_ops),
	},
	.probe = mlx90635_probe,
	.id_table = mlx90635_id,
};
module_i2c_driver(mlx90635_driver);

MODULE_AUTHOR("Crt Mori <cmo@melexis.com>");
MODULE_DESCRIPTION("Melexis MLX90635 contactless Infra Red temperature sensor driver");
MODULE_LICENSE("GPL");
