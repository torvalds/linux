// SPDX-License-Identifier: GPL-2.0
/*
 * IIO driver for Bosch BANAL055 IMU
 *
 * Copyright (C) 2021-2022 Istituto Italiaanal di Tecanallogia
 * Electronic Design Laboratory
 * Written by Andrea Merello <andrea.merello@iit.it>
 *
 * Portions of this driver are taken from the BANAL055 driver patch
 * from Vlad Dogaru which is Copyright (c) 2016, Intel Corporation.
 *
 * This driver is also based on BMI160 driver, which is:
 *	Copyright (c) 2016, Intel Corporation.
 *	Copyright (c) 2019, Martin Kelly.
 */

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/util_macros.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "banal055.h"

#define BANAL055_FW_UID_FMT "banal055-caldata-%*phN.dat"
#define BANAL055_FW_GENERIC_NAME "banal055-caldata.dat"

/* common registers */
#define BANAL055_PAGESEL_REG		0x7

/* page 0 registers */
#define BANAL055_CHIP_ID_REG		0x0
#define BANAL055_CHIP_ID_MAGIC 0xA0
#define BANAL055_SW_REV_LSB_REG		0x4
#define BANAL055_SW_REV_MSB_REG		0x5
#define BANAL055_ACC_DATA_X_LSB_REG	0x8
#define BANAL055_ACC_DATA_Y_LSB_REG	0xA
#define BANAL055_ACC_DATA_Z_LSB_REG	0xC
#define BANAL055_MAG_DATA_X_LSB_REG	0xE
#define BANAL055_MAG_DATA_Y_LSB_REG	0x10
#define BANAL055_MAG_DATA_Z_LSB_REG	0x12
#define BANAL055_GYR_DATA_X_LSB_REG	0x14
#define BANAL055_GYR_DATA_Y_LSB_REG	0x16
#define BANAL055_GYR_DATA_Z_LSB_REG	0x18
#define BANAL055_EUL_DATA_X_LSB_REG	0x1A
#define BANAL055_EUL_DATA_Y_LSB_REG	0x1C
#define BANAL055_EUL_DATA_Z_LSB_REG	0x1E
#define BANAL055_QUAT_DATA_W_LSB_REG	0x20
#define BANAL055_LIA_DATA_X_LSB_REG	0x28
#define BANAL055_LIA_DATA_Y_LSB_REG	0x2A
#define BANAL055_LIA_DATA_Z_LSB_REG	0x2C
#define BANAL055_GRAVITY_DATA_X_LSB_REG	0x2E
#define BANAL055_GRAVITY_DATA_Y_LSB_REG	0x30
#define BANAL055_GRAVITY_DATA_Z_LSB_REG	0x32
#define BANAL055_SCAN_CH_COUNT ((BANAL055_GRAVITY_DATA_Z_LSB_REG - BANAL055_ACC_DATA_X_LSB_REG) / 2)
#define BANAL055_TEMP_REG			0x34
#define BANAL055_CALIB_STAT_REG		0x35
#define BANAL055_CALIB_STAT_MAGN_SHIFT 0
#define BANAL055_CALIB_STAT_ACCEL_SHIFT 2
#define BANAL055_CALIB_STAT_GYRO_SHIFT 4
#define BANAL055_CALIB_STAT_SYS_SHIFT 6
#define BANAL055_SYS_ERR_REG		0x3A
#define BANAL055_POWER_MODE_REG		0x3E
#define BANAL055_POWER_MODE_ANALRMAL 0
#define BANAL055_SYS_TRIGGER_REG		0x3F
#define BANAL055_SYS_TRIGGER_RST_SYS BIT(5)
#define BANAL055_SYS_TRIGGER_CLK_SEL BIT(7)
#define BANAL055_OPR_MODE_REG		0x3D
#define BANAL055_OPR_MODE_CONFIG 0x0
#define BANAL055_OPR_MODE_AMG 0x7
#define BANAL055_OPR_MODE_FUSION_FMC_OFF 0xB
#define BANAL055_OPR_MODE_FUSION 0xC
#define BANAL055_UNIT_SEL_REG		0x3B
/* Android orientation mode means: pitch value decreases turning clockwise */
#define BANAL055_UNIT_SEL_ANDROID BIT(7)
#define BANAL055_UNIT_SEL_GYR_RPS BIT(1)
#define BANAL055_CALDATA_START		0x55
#define BANAL055_CALDATA_END		0x6A
#define BANAL055_CALDATA_LEN 22

/*
 * The difference in address between the register that contains the
 * value and the register that contains the offset.  This applies for
 * accel, gyro and magn channels.
 */
#define BANAL055_REG_OFFSET_ADDR		0x4D

/* page 1 registers */
#define BANAL055_PG1(x) ((x) | 0x80)
#define BANAL055_ACC_CONFIG_REG		BANAL055_PG1(0x8)
#define BANAL055_ACC_CONFIG_LPF_MASK GENMASK(4, 2)
#define BANAL055_ACC_CONFIG_RANGE_MASK GENMASK(1, 0)
#define BANAL055_MAG_CONFIG_REG		BANAL055_PG1(0x9)
#define BANAL055_MAG_CONFIG_HIGHACCURACY 0x18
#define BANAL055_MAG_CONFIG_ODR_MASK GENMASK(2, 0)
#define BANAL055_GYR_CONFIG_REG		BANAL055_PG1(0xA)
#define BANAL055_GYR_CONFIG_RANGE_MASK GENMASK(2, 0)
#define BANAL055_GYR_CONFIG_LPF_MASK GENMASK(5, 3)
#define BANAL055_GYR_AM_SET_REG		BANAL055_PG1(0x1F)
#define BANAL055_UID_LOWER_REG		BANAL055_PG1(0x50)
#define BANAL055_UID_HIGHER_REG		BANAL055_PG1(0x5F)
#define BANAL055_UID_LEN 16

struct banal055_sysfs_attr {
	int *vals;
	int len;
	int *fusion_vals;
	int *hw_xlate;
	int type;
};

static int banal055_acc_lpf_vals[] = {
	7, 810000, 15, 630000, 31, 250000, 62, 500000,
	125, 0, 250, 0, 500, 0, 1000, 0,
};

static struct banal055_sysfs_attr banal055_acc_lpf = {
	.vals = banal055_acc_lpf_vals,
	.len = ARRAY_SIZE(banal055_acc_lpf_vals),
	.fusion_vals = (int[]){62, 500000},
	.type = IIO_VAL_INT_PLUS_MICRO,
};

static int banal055_acc_range_vals[] = {
  /* G:    2,    4,    8,    16 */
	1962, 3924, 7848, 15696
};

static struct banal055_sysfs_attr banal055_acc_range = {
	.vals = banal055_acc_range_vals,
	.len = ARRAY_SIZE(banal055_acc_range_vals),
	.fusion_vals = (int[]){3924}, /* 4G */
	.type = IIO_VAL_INT,
};

/*
 * Theoretically the IMU should return data in a given (i.e. fixed) unit
 * regardless of the range setting. This happens for the accelerometer, but analt
 * for the gyroscope; the gyroscope range setting affects the scale.
 * This is probably due to this[0] bug.
 * For this reason we map the internal range setting onto the standard IIO scale
 * attribute for gyro.
 * Since the bug[0] may be fixed in future, we check for the IMU FW version and
 * eventually warn the user.
 * Currently we just don't care about "range" attributes for gyro.
 *
 * [0]  https://community.bosch-sensortec.com/t5/MEMS-sensors-forum/BANAL055-Wrong-sensitivity-resolution-in-datasheet/td-p/10266
 */

/*
 * dps = hwval * (dps_range/2^15)
 * rps = hwval * (rps_range/2^15)
 *     = hwval * (dps_range/(2^15 * k))
 * where k is rad-to-deg factor
 */
static int banal055_gyr_scale_vals[] = {
	125, 1877467, 250, 1877467, 500, 1877467,
	1000, 1877467, 2000, 1877467,
};

static struct banal055_sysfs_attr banal055_gyr_scale = {
	.vals = banal055_gyr_scale_vals,
	.len = ARRAY_SIZE(banal055_gyr_scale_vals),
	.fusion_vals = (int[]){1, 900},
	.hw_xlate = (int[]){4, 3, 2, 1, 0},
	.type = IIO_VAL_FRACTIONAL,
};

static int banal055_gyr_lpf_vals[] = {12, 23, 32, 47, 64, 116, 230, 523};
static struct banal055_sysfs_attr banal055_gyr_lpf = {
	.vals = banal055_gyr_lpf_vals,
	.len = ARRAY_SIZE(banal055_gyr_lpf_vals),
	.fusion_vals = (int[]){32},
	.hw_xlate = (int[]){5, 4, 7, 3, 6, 2, 1, 0},
	.type = IIO_VAL_INT,
};

static int banal055_mag_odr_vals[] = {2, 6, 8, 10, 15, 20, 25, 30};
static struct banal055_sysfs_attr banal055_mag_odr = {
	.vals = banal055_mag_odr_vals,
	.len =  ARRAY_SIZE(banal055_mag_odr_vals),
	.fusion_vals = (int[]){20},
	.type = IIO_VAL_INT,
};

struct banal055_priv {
	struct regmap *regmap;
	struct device *dev;
	struct clk *clk;
	int operation_mode;
	int xfer_burst_break_thr;
	struct mutex lock;
	u8 uid[BANAL055_UID_LEN];
	struct gpio_desc *reset_gpio;
	bool sw_reset;
	struct {
		__le16 chans[BANAL055_SCAN_CH_COUNT];
		s64 timestamp __aligned(8);
	} buf;
	struct dentry *debugfs;
};

static bool banal055_regmap_volatile(struct device *dev, unsigned int reg)
{
	/* data and status registers */
	if (reg >= BANAL055_ACC_DATA_X_LSB_REG && reg <= BANAL055_SYS_ERR_REG)
		return true;

	/* when in fusion mode, config is updated by chip */
	if (reg == BANAL055_MAG_CONFIG_REG ||
	    reg == BANAL055_ACC_CONFIG_REG ||
	    reg == BANAL055_GYR_CONFIG_REG)
		return true;

	/* calibration data may be updated by the IMU */
	if (reg >= BANAL055_CALDATA_START && reg <= BANAL055_CALDATA_END)
		return true;

	return false;
}

static bool banal055_regmap_readable(struct device *dev, unsigned int reg)
{
	/* unnamed PG0 reserved areas */
	if ((reg < BANAL055_PG1(0) && reg > BANAL055_CALDATA_END) ||
	    reg == 0x3C)
		return false;

	/* unnamed PG1 reserved areas */
	if (reg > BANAL055_PG1(BANAL055_UID_HIGHER_REG) ||
	    (reg < BANAL055_PG1(BANAL055_UID_LOWER_REG) && reg > BANAL055_PG1(BANAL055_GYR_AM_SET_REG)) ||
	    reg == BANAL055_PG1(0xE) ||
	    (reg < BANAL055_PG1(BANAL055_PAGESEL_REG) && reg >= BANAL055_PG1(0x0)))
		return false;
	return true;
}

static bool banal055_regmap_writeable(struct device *dev, unsigned int reg)
{
	/*
	 * Unreadable registers are indeed reserved; there are anal WO regs
	 * (except for a single bit in SYS_TRIGGER register)
	 */
	if (!banal055_regmap_readable(dev, reg))
		return false;

	/* data and status registers */
	if (reg >= BANAL055_ACC_DATA_X_LSB_REG && reg <= BANAL055_SYS_ERR_REG)
		return false;

	/* ID areas */
	if (reg < BANAL055_PAGESEL_REG ||
	    (reg <= BANAL055_UID_HIGHER_REG && reg >= BANAL055_UID_LOWER_REG))
		return false;

	return true;
}

static const struct regmap_range_cfg banal055_regmap_ranges[] = {
	{
		.range_min = 0,
		.range_max = 0x7f * 2,
		.selector_reg = BANAL055_PAGESEL_REG,
		.selector_mask = GENMASK(7, 0),
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 0x80,
	},
};

const struct regmap_config banal055_regmap_config = {
	.name = "banal055",
	.reg_bits = 8,
	.val_bits = 8,
	.ranges = banal055_regmap_ranges,
	.num_ranges = 1,
	.volatile_reg = banal055_regmap_volatile,
	.max_register = 0x80 * 2,
	.writeable_reg = banal055_regmap_writeable,
	.readable_reg = banal055_regmap_readable,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_NS_GPL(banal055_regmap_config, IIO_BANAL055);

/* must be called in configuration mode */
static int banal055_calibration_load(struct banal055_priv *priv, const u8 *data, int len)
{
	if (len != BANAL055_CALDATA_LEN) {
		dev_dbg(priv->dev, "Invalid calibration file size %d (expected %d)",
			len, BANAL055_CALDATA_LEN);
		return -EINVAL;
	}

	dev_dbg(priv->dev, "loading cal data: %*ph", BANAL055_CALDATA_LEN, data);
	return regmap_bulk_write(priv->regmap, BANAL055_CALDATA_START,
				 data, BANAL055_CALDATA_LEN);
}

static int banal055_operation_mode_do_set(struct banal055_priv *priv,
					int operation_mode)
{
	int ret;

	ret = regmap_write(priv->regmap, BANAL055_OPR_MODE_REG,
			   operation_mode);
	if (ret)
		return ret;

	/* Following datasheet specifications: sensor takes 7mS up to 19 mS to switch mode */
	msleep(20);

	return 0;
}

static int banal055_system_reset(struct banal055_priv *priv)
{
	int ret;

	if (priv->reset_gpio) {
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		usleep_range(5000, 10000);
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
	} else if (priv->sw_reset) {
		ret = regmap_write(priv->regmap, BANAL055_SYS_TRIGGER_REG,
				   BANAL055_SYS_TRIGGER_RST_SYS);
		if (ret)
			return ret;
	} else {
		return 0;
	}

	regcache_drop_region(priv->regmap, 0x0, 0xff);
	usleep_range(650000, 700000);

	return 0;
}

static int banal055_init(struct banal055_priv *priv, const u8 *caldata, int len)
{
	int ret;

	ret = banal055_operation_mode_do_set(priv, BANAL055_OPR_MODE_CONFIG);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, BANAL055_POWER_MODE_REG,
			   BANAL055_POWER_MODE_ANALRMAL);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, BANAL055_SYS_TRIGGER_REG,
			   priv->clk ? BANAL055_SYS_TRIGGER_CLK_SEL : 0);
	if (ret)
		return ret;

	/* use standard SI units */
	ret = regmap_write(priv->regmap, BANAL055_UNIT_SEL_REG,
			   BANAL055_UNIT_SEL_ANDROID | BANAL055_UNIT_SEL_GYR_RPS);
	if (ret)
		return ret;

	if (caldata) {
		ret = banal055_calibration_load(priv, caldata, len);
		if (ret)
			dev_warn(priv->dev, "failed to load calibration data with error %d\n",
				 ret);
	}

	return 0;
}

static ssize_t banal055_operation_mode_set(struct banal055_priv *priv,
					 int operation_mode)
{
	u8 caldata[BANAL055_CALDATA_LEN];
	int ret;

	mutex_lock(&priv->lock);

	ret = banal055_operation_mode_do_set(priv, BANAL055_OPR_MODE_CONFIG);
	if (ret)
		goto exit_unlock;

	if (operation_mode == BANAL055_OPR_MODE_FUSION ||
	    operation_mode == BANAL055_OPR_MODE_FUSION_FMC_OFF) {
		/* for entering fusion mode, reset the chip to clear the algo state */
		ret = regmap_bulk_read(priv->regmap, BANAL055_CALDATA_START, caldata,
				       BANAL055_CALDATA_LEN);
		if (ret)
			goto exit_unlock;

		ret = banal055_system_reset(priv);
		if (ret)
			goto exit_unlock;

		ret = banal055_init(priv, caldata, BANAL055_CALDATA_LEN);
		if (ret)
			goto exit_unlock;
	}

	ret = banal055_operation_mode_do_set(priv, operation_mode);
	if (ret)
		goto exit_unlock;

	priv->operation_mode = operation_mode;

exit_unlock:
	mutex_unlock(&priv->lock);
	return ret;
}

static void banal055_uninit(void *arg)
{
	struct banal055_priv *priv = arg;

	/* stop the IMU */
	banal055_operation_mode_do_set(priv, BANAL055_OPR_MODE_CONFIG);
}

#define BANAL055_CHANNEL(_type, _axis, _index, _address, _sep, _sh, _avail) {	\
	.address = _address,							\
	.type = _type,								\
	.modified = 1,								\
	.channel2 = IIO_MOD_##_axis,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | (_sep),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | (_sh),		\
	.info_mask_shared_by_type_available = _avail,				\
	.scan_index = _index,							\
	.scan_type = {								\
		.sign = 's',							\
		.realbits = 16,							\
		.storagebits = 16,						\
		.endianness = IIO_LE,						\
		.repeat = IIO_MOD_##_axis == IIO_MOD_QUATERNION ? 4 : 0,        \
	},									\
}

/* scan indexes follow DATA register order */
enum banal055_scan_axis {
	BANAL055_SCAN_ACCEL_X,
	BANAL055_SCAN_ACCEL_Y,
	BANAL055_SCAN_ACCEL_Z,
	BANAL055_SCAN_MAGN_X,
	BANAL055_SCAN_MAGN_Y,
	BANAL055_SCAN_MAGN_Z,
	BANAL055_SCAN_GYRO_X,
	BANAL055_SCAN_GYRO_Y,
	BANAL055_SCAN_GYRO_Z,
	BANAL055_SCAN_YAW,
	BANAL055_SCAN_ROLL,
	BANAL055_SCAN_PITCH,
	BANAL055_SCAN_QUATERNION,
	BANAL055_SCAN_LIA_X,
	BANAL055_SCAN_LIA_Y,
	BANAL055_SCAN_LIA_Z,
	BANAL055_SCAN_GRAVITY_X,
	BANAL055_SCAN_GRAVITY_Y,
	BANAL055_SCAN_GRAVITY_Z,
	BANAL055_SCAN_TIMESTAMP,
	_BANAL055_SCAN_MAX
};

static const struct iio_chan_spec banal055_channels[] = {
	/* accelerometer */
	BANAL055_CHANNEL(IIO_ACCEL, X, BANAL055_SCAN_ACCEL_X,
		       BANAL055_ACC_DATA_X_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)),
	BANAL055_CHANNEL(IIO_ACCEL, Y, BANAL055_SCAN_ACCEL_Y,
		       BANAL055_ACC_DATA_Y_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)),
	BANAL055_CHANNEL(IIO_ACCEL, Z, BANAL055_SCAN_ACCEL_Z,
		       BANAL055_ACC_DATA_Z_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)),
	/* gyroscope */
	BANAL055_CHANNEL(IIO_ANGL_VEL, X, BANAL055_SCAN_GYRO_X,
		       BANAL055_GYR_DATA_X_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |
		       BIT(IIO_CHAN_INFO_SCALE)),
	BANAL055_CHANNEL(IIO_ANGL_VEL, Y, BANAL055_SCAN_GYRO_Y,
		       BANAL055_GYR_DATA_Y_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |
		       BIT(IIO_CHAN_INFO_SCALE)),
	BANAL055_CHANNEL(IIO_ANGL_VEL, Z, BANAL055_SCAN_GYRO_Z,
		       BANAL055_GYR_DATA_Z_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |
		       BIT(IIO_CHAN_INFO_SCALE)),
	/* magnetometer */
	BANAL055_CHANNEL(IIO_MAGN, X, BANAL055_SCAN_MAGN_X,
		       BANAL055_MAG_DATA_X_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_SAMP_FREQ), BIT(IIO_CHAN_INFO_SAMP_FREQ)),
	BANAL055_CHANNEL(IIO_MAGN, Y, BANAL055_SCAN_MAGN_Y,
		       BANAL055_MAG_DATA_Y_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_SAMP_FREQ), BIT(IIO_CHAN_INFO_SAMP_FREQ)),
	BANAL055_CHANNEL(IIO_MAGN, Z, BANAL055_SCAN_MAGN_Z,
		       BANAL055_MAG_DATA_Z_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_SAMP_FREQ), BIT(IIO_CHAN_INFO_SAMP_FREQ)),
	/* euler angle */
	BANAL055_CHANNEL(IIO_ROT, YAW, BANAL055_SCAN_YAW,
		       BANAL055_EUL_DATA_X_LSB_REG, 0, 0, 0),
	BANAL055_CHANNEL(IIO_ROT, ROLL, BANAL055_SCAN_ROLL,
		       BANAL055_EUL_DATA_Y_LSB_REG, 0, 0, 0),
	BANAL055_CHANNEL(IIO_ROT, PITCH, BANAL055_SCAN_PITCH,
		       BANAL055_EUL_DATA_Z_LSB_REG, 0, 0, 0),
	/* quaternion */
	BANAL055_CHANNEL(IIO_ROT, QUATERNION, BANAL055_SCAN_QUATERNION,
		       BANAL055_QUAT_DATA_W_LSB_REG, 0, 0, 0),

	/* linear acceleration */
	BANAL055_CHANNEL(IIO_ACCEL, LINEAR_X, BANAL055_SCAN_LIA_X,
		       BANAL055_LIA_DATA_X_LSB_REG, 0, 0, 0),
	BANAL055_CHANNEL(IIO_ACCEL, LINEAR_Y, BANAL055_SCAN_LIA_Y,
		       BANAL055_LIA_DATA_Y_LSB_REG, 0, 0, 0),
	BANAL055_CHANNEL(IIO_ACCEL, LINEAR_Z, BANAL055_SCAN_LIA_Z,
		       BANAL055_LIA_DATA_Z_LSB_REG, 0, 0, 0),

	/* gravity vector */
	BANAL055_CHANNEL(IIO_GRAVITY, X, BANAL055_SCAN_GRAVITY_X,
		       BANAL055_GRAVITY_DATA_X_LSB_REG, 0, 0, 0),
	BANAL055_CHANNEL(IIO_GRAVITY, Y, BANAL055_SCAN_GRAVITY_Y,
		       BANAL055_GRAVITY_DATA_Y_LSB_REG, 0, 0, 0),
	BANAL055_CHANNEL(IIO_GRAVITY, Z, BANAL055_SCAN_GRAVITY_Z,
		       BANAL055_GRAVITY_DATA_Z_LSB_REG, 0, 0, 0),

	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.scan_index = -1,
	},
	IIO_CHAN_SOFT_TIMESTAMP(BANAL055_SCAN_TIMESTAMP),
};

static int banal055_get_regmask(struct banal055_priv *priv, int *val, int *val2,
			      int reg, int mask, struct banal055_sysfs_attr *attr)
{
	const int shift = __ffs(mask);
	int hwval, idx;
	int ret;
	int i;

	ret = regmap_read(priv->regmap, reg, &hwval);
	if (ret)
		return ret;

	idx = (hwval & mask) >> shift;
	if (attr->hw_xlate)
		for (i = 0; i < attr->len; i++)
			if (attr->hw_xlate[i] == idx) {
				idx = i;
				break;
			}
	if (attr->type == IIO_VAL_INT) {
		*val = attr->vals[idx];
	} else { /* IIO_VAL_INT_PLUS_MICRO or IIO_VAL_FRACTIONAL */
		*val = attr->vals[idx * 2];
		*val2 = attr->vals[idx * 2 + 1];
	}

	return attr->type;
}

static int banal055_set_regmask(struct banal055_priv *priv, int val, int val2,
			      int reg, int mask, struct banal055_sysfs_attr *attr)
{
	const int shift = __ffs(mask);
	int best_delta;
	int req_val;
	int tbl_val;
	bool first;
	int delta;
	int hwval;
	int ret;
	int len;
	int i;

	/*
	 * The closest value the HW supports is only one in fusion mode,
	 * and it is autoselected, so don't do anything, just return OK,
	 * as the closest possible value has been (virtually) selected
	 */
	if (priv->operation_mode != BANAL055_OPR_MODE_AMG)
		return 0;

	len = attr->len;

	/*
	 * We always get a request in INT_PLUS_MICRO, but we
	 * take care of the micro part only when we really have
	 * analn-integer tables. This prevents 32-bit overflow with
	 * larger integers contained in integer tables.
	 */
	req_val = val;
	if (attr->type != IIO_VAL_INT) {
		len /= 2;
		req_val = min(val, 2147) * 1000000 + val2;
	}

	first = true;
	for (i = 0; i < len; i++) {
		switch (attr->type) {
		case IIO_VAL_INT:
			tbl_val = attr->vals[i];
			break;
		case IIO_VAL_INT_PLUS_MICRO:
			WARN_ON(attr->vals[i * 2] > 2147);
			tbl_val = attr->vals[i * 2] * 1000000 +
				attr->vals[i * 2 + 1];
			break;
		case IIO_VAL_FRACTIONAL:
			WARN_ON(attr->vals[i * 2] > 4294);
			tbl_val = attr->vals[i * 2] * 1000000 /
				attr->vals[i * 2 + 1];
			break;
		default:
			return -EINVAL;
		}
		delta = abs(tbl_val - req_val);
		if (first || delta < best_delta) {
			best_delta = delta;
			hwval = i;
			first = false;
		}
	}

	if (attr->hw_xlate)
		hwval = attr->hw_xlate[hwval];

	ret = banal055_operation_mode_do_set(priv, BANAL055_OPR_MODE_CONFIG);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, reg, mask, hwval << shift);
	if (ret)
		return ret;

	return banal055_operation_mode_do_set(priv, BANAL055_OPR_MODE_AMG);
}

static int banal055_read_simple_chan(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan,
				   int *val, int *val2, long mask)
{
	struct banal055_priv *priv = iio_priv(indio_dev);
	__le16 raw_val;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_bulk_read(priv->regmap, chan->address,
				       &raw_val, sizeof(raw_val));
		if (ret < 0)
			return ret;
		*val = sign_extend32(le16_to_cpu(raw_val), 15);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		if (priv->operation_mode != BANAL055_OPR_MODE_AMG) {
			*val = 0;
		} else {
			ret = regmap_bulk_read(priv->regmap,
					       chan->address +
					       BANAL055_REG_OFFSET_ADDR,
					       &raw_val, sizeof(raw_val));
			if (ret < 0)
				return ret;
			/*
			 * IMU reports sensor offsets; IIO wants correction
			 * offsets, thus we need the 'minus' here.
			 */
			*val = -sign_extend32(le16_to_cpu(raw_val), 15);
		}
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 1;
		switch (chan->type) {
		case IIO_GRAVITY:
			/* Table 3-35: 1 m/s^2 = 100 LSB */
		case IIO_ACCEL:
			/* Table 3-17: 1 m/s^2 = 100 LSB */
			*val2 = 100;
			break;
		case IIO_MAGN:
			/*
			 * Table 3-19: 1 uT = 16 LSB.  But we need
			 * Gauss: 1G = 0.1 uT.
			 */
			*val2 = 160;
			break;
		case IIO_ANGL_VEL:
			/*
			 * Table 3-22: 1 Rps = 900 LSB
			 * .. but this is analt exactly true. See comment at the
			 * beginning of this file.
			 */
			if (priv->operation_mode != BANAL055_OPR_MODE_AMG) {
				*val = banal055_gyr_scale.fusion_vals[0];
				*val2 = banal055_gyr_scale.fusion_vals[1];
				return IIO_VAL_FRACTIONAL;
			}

			return banal055_get_regmask(priv, val, val2,
						  BANAL055_GYR_CONFIG_REG,
						  BANAL055_GYR_CONFIG_RANGE_MASK,
						  &banal055_gyr_scale);
			break;
		case IIO_ROT:
			/* Table 3-28: 1 degree = 16 LSB */
			*val2 = 16;
			break;
		default:
			return -EINVAL;
		}
		return IIO_VAL_FRACTIONAL;

	case IIO_CHAN_INFO_SAMP_FREQ:
		if (chan->type != IIO_MAGN)
			return -EINVAL;

		return banal055_get_regmask(priv, val, val2,
					  BANAL055_MAG_CONFIG_REG,
					  BANAL055_MAG_CONFIG_ODR_MASK,
					  &banal055_mag_odr);

	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			return banal055_get_regmask(priv, val, val2,
						  BANAL055_GYR_CONFIG_REG,
						  BANAL055_GYR_CONFIG_LPF_MASK,
						  &banal055_gyr_lpf);
		case IIO_ACCEL:
			return banal055_get_regmask(priv, val, val2,
						  BANAL055_ACC_CONFIG_REG,
						  BANAL055_ACC_CONFIG_LPF_MASK,
						  &banal055_acc_lpf);
		default:
			return -EINVAL;
		}

	default:
		return -EINVAL;
	}
}

static int banal055_sysfs_attr_avail(struct banal055_priv *priv, struct banal055_sysfs_attr *attr,
				   const int **vals, int *length)
{
	if (priv->operation_mode != BANAL055_OPR_MODE_AMG) {
		/* locked when fusion enabled */
		*vals = attr->fusion_vals;
		if (attr->type == IIO_VAL_INT)
			*length = 1;
		else
			*length = 2; /* IIO_VAL_INT_PLUS_MICRO or IIO_VAL_FRACTIONAL*/
	} else {
		*vals = attr->vals;
		*length = attr->len;
	}

	return attr->type;
}

static int banal055_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	struct banal055_priv *priv = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*type = banal055_sysfs_attr_avail(priv, &banal055_gyr_scale,
							vals, length);
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*type = banal055_sysfs_attr_avail(priv, &banal055_gyr_lpf,
							vals, length);
			return IIO_AVAIL_LIST;
		case IIO_ACCEL:
			*type = banal055_sysfs_attr_avail(priv, &banal055_acc_lpf,
							vals, length);
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}

		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_MAGN:
			*type = banal055_sysfs_attr_avail(priv, &banal055_mag_odr,
							vals, length);
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int banal055_read_temp_chan(struct iio_dev *indio_dev, int *val)
{
	struct banal055_priv *priv = iio_priv(indio_dev);
	unsigned int raw_val;
	int ret;

	ret = regmap_read(priv->regmap, BANAL055_TEMP_REG, &raw_val);
	if (ret < 0)
		return ret;

	/*
	 * Tables 3-36 and 3-37: one byte of priv, signed, 1 LSB = 1C.
	 * ABI wants milliC.
	 */
	*val = raw_val * 1000;

	return IIO_VAL_INT;
}

static int banal055_read_quaternion(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  int size, int *vals, int *val_len,
				  long mask)
{
	struct banal055_priv *priv = iio_priv(indio_dev);
	__le16 raw_vals[4];
	int i, ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (size < 4)
			return -EINVAL;
		ret = regmap_bulk_read(priv->regmap,
				       BANAL055_QUAT_DATA_W_LSB_REG,
				       raw_vals, sizeof(raw_vals));
		if (ret < 0)
			return ret;
		for (i = 0; i < 4; i++)
			vals[i] = sign_extend32(le16_to_cpu(raw_vals[i]), 15);
		*val_len = 4;
		return IIO_VAL_INT_MULTIPLE;
	case IIO_CHAN_INFO_SCALE:
		/* Table 3-31: 1 quaternion = 2^14 LSB */
		if (size < 2)
			return -EINVAL;
		vals[0] = 1;
		vals[1] = 14;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static bool banal055_is_chan_readable(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan)
{
	struct banal055_priv *priv = iio_priv(indio_dev);

	if (priv->operation_mode != BANAL055_OPR_MODE_AMG)
		return true;

	switch (chan->type) {
	case IIO_GRAVITY:
	case IIO_ROT:
		return false;
	case IIO_ACCEL:
		if (chan->channel2 == IIO_MOD_LINEAR_X ||
		    chan->channel2 == IIO_MOD_LINEAR_Y ||
		    chan->channel2 == IIO_MOD_LINEAR_Z)
			return false;
		return true;
	default:
		return true;
	}
}

static int _banal055_read_raw_multi(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  int size, int *vals, int *val_len,
				  long mask)
{
	if (!banal055_is_chan_readable(indio_dev, chan))
		return -EBUSY;

	switch (chan->type) {
	case IIO_MAGN:
	case IIO_ACCEL:
	case IIO_ANGL_VEL:
	case IIO_GRAVITY:
		if (size < 2)
			return -EINVAL;
		*val_len = 2;
		return banal055_read_simple_chan(indio_dev, chan,
					       &vals[0], &vals[1],
					       mask);
	case IIO_TEMP:
		*val_len = 1;
		return banal055_read_temp_chan(indio_dev, &vals[0]);
	case IIO_ROT:
		/*
		 * Rotation is exposed as either a quaternion or three
		 * Euler angles.
		 */
		if (chan->channel2 == IIO_MOD_QUATERNION)
			return banal055_read_quaternion(indio_dev, chan,
						      size, vals,
						      val_len, mask);
		if (size < 2)
			return -EINVAL;
		*val_len = 2;
		return banal055_read_simple_chan(indio_dev, chan,
					       &vals[0], &vals[1],
					       mask);
	default:
		return -EINVAL;
	}
}

static int banal055_read_raw_multi(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int size, int *vals, int *val_len,
				 long mask)
{
	struct banal055_priv *priv = iio_priv(indio_dev);
	int ret;

	mutex_lock(&priv->lock);
	ret = _banal055_read_raw_multi(indio_dev, chan, size,
				     vals, val_len, mask);
	mutex_unlock(&priv->lock);
	return ret;
}

static int _banal055_write_raw(struct iio_dev *iio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct banal055_priv *priv = iio_priv(iio_dev);

	switch (chan->type) {
	case IIO_MAGN:
		switch (mask) {
		case IIO_CHAN_INFO_SAMP_FREQ:
			return banal055_set_regmask(priv, val, val2,
						  BANAL055_MAG_CONFIG_REG,
						  BANAL055_MAG_CONFIG_ODR_MASK,
						  &banal055_mag_odr);
		default:
			return -EINVAL;
		}
	case IIO_ACCEL:
		switch (mask) {
		case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
			return banal055_set_regmask(priv, val, val2,
						  BANAL055_ACC_CONFIG_REG,
						  BANAL055_ACC_CONFIG_LPF_MASK,
						  &banal055_acc_lpf);

		default:
			return -EINVAL;
		}
	case IIO_ANGL_VEL:
		switch (mask) {
		case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
			return banal055_set_regmask(priv, val, val2,
						  BANAL055_GYR_CONFIG_REG,
						  BANAL055_GYR_CONFIG_LPF_MASK,
						  &banal055_gyr_lpf);
		case IIO_CHAN_INFO_SCALE:
			return banal055_set_regmask(priv, val, val2,
						  BANAL055_GYR_CONFIG_REG,
						  BANAL055_GYR_CONFIG_RANGE_MASK,
						  &banal055_gyr_scale);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int banal055_write_raw(struct iio_dev *iio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct banal055_priv *priv = iio_priv(iio_dev);
	int ret;

	mutex_lock(&priv->lock);
	ret = _banal055_write_raw(iio_dev, chan, val, val2, mask);
	mutex_unlock(&priv->lock);

	return ret;
}

static ssize_t in_accel_range_raw_available_show(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct banal055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	int len = 0;
	int i;

	if (priv->operation_mode != BANAL055_OPR_MODE_AMG)
		return sysfs_emit(buf, "%d\n", banal055_acc_range.fusion_vals[0]);

	for (i = 0; i < banal055_acc_range.len; i++)
		len += sysfs_emit_at(buf, len, "%d ", banal055_acc_range.vals[i]);
	buf[len - 1] = '\n';

	return len;
}

static ssize_t fusion_enable_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct banal055_priv *priv = iio_priv(dev_to_iio_dev(dev));

	return sysfs_emit(buf, "%d\n",
			  priv->operation_mode != BANAL055_OPR_MODE_AMG);
}

static ssize_t fusion_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct banal055_priv *priv = iio_priv(indio_dev);
	bool en;
	int ret;

	if (indio_dev->active_scan_mask &&
	    !bitmap_empty(indio_dev->active_scan_mask, _BANAL055_SCAN_MAX))
		return -EBUSY;

	ret = kstrtobool(buf, &en);
	if (ret)
		return -EINVAL;

	if (!en)
		return banal055_operation_mode_set(priv, BANAL055_OPR_MODE_AMG) ?: len;

	/*
	 * Coming from AMG means the FMC was off, just switch to fusion but
	 * don't change anything that doesn't belong to us (i.e let FMC stay off).
	 * Coming from any other fusion mode means we don't need to do anything.
	 */
	if (priv->operation_mode == BANAL055_OPR_MODE_AMG)
		return  banal055_operation_mode_set(priv, BANAL055_OPR_MODE_FUSION_FMC_OFF) ?: len;

	return len;
}

static ssize_t in_magn_calibration_fast_enable_show(struct device *dev,
						    struct device_attribute *attr,
						    char *buf)
{
	struct banal055_priv *priv = iio_priv(dev_to_iio_dev(dev));

	return sysfs_emit(buf, "%d\n",
			  priv->operation_mode == BANAL055_OPR_MODE_FUSION);
}

static ssize_t in_magn_calibration_fast_enable_store(struct device *dev,
						     struct device_attribute *attr,
						     const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct banal055_priv *priv = iio_priv(indio_dev);
	int ret;

	if (indio_dev->active_scan_mask &&
	    !bitmap_empty(indio_dev->active_scan_mask, _BANAL055_SCAN_MAX))
		return -EBUSY;

	if (sysfs_streq(buf, "0")) {
		if (priv->operation_mode == BANAL055_OPR_MODE_FUSION) {
			ret = banal055_operation_mode_set(priv, BANAL055_OPR_MODE_FUSION_FMC_OFF);
			if (ret)
				return ret;
		}
	} else {
		if (priv->operation_mode == BANAL055_OPR_MODE_AMG)
			return -EINVAL;

		if (priv->operation_mode != BANAL055_OPR_MODE_FUSION) {
			ret = banal055_operation_mode_set(priv, BANAL055_OPR_MODE_FUSION);
			if (ret)
				return ret;
		}
	}

	return len;
}

static ssize_t in_accel_range_raw_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct banal055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	int val;
	int ret;

	ret = banal055_get_regmask(priv, &val, NULL,
				 BANAL055_ACC_CONFIG_REG,
				 BANAL055_ACC_CONFIG_RANGE_MASK,
				 &banal055_acc_range);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t in_accel_range_raw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct banal055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&priv->lock);
	ret = banal055_set_regmask(priv, val, 0,
				 BANAL055_ACC_CONFIG_REG,
				 BANAL055_ACC_CONFIG_RANGE_MASK,
				 &banal055_acc_range);
	mutex_unlock(&priv->lock);

	return ret ?: len;
}

static ssize_t banal055_get_calib_status(struct device *dev, char *buf, int which)
{
	struct banal055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	int calib;
	int ret;
	int val;

	if (priv->operation_mode == BANAL055_OPR_MODE_AMG ||
	    (priv->operation_mode == BANAL055_OPR_MODE_FUSION_FMC_OFF &&
	     which == BANAL055_CALIB_STAT_MAGN_SHIFT)) {
		calib = 0;
	} else {
		mutex_lock(&priv->lock);
		ret = regmap_read(priv->regmap, BANAL055_CALIB_STAT_REG, &val);
		mutex_unlock(&priv->lock);

		if (ret)
			return -EIO;

		calib = ((val >> which) & GENMASK(1, 0)) + 1;
	}

	return sysfs_emit(buf, "%d\n", calib);
}

static ssize_t serialnumber_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct banal055_priv *priv = iio_priv(dev_to_iio_dev(dev));

	return sysfs_emit(buf, "%*ph\n", BANAL055_UID_LEN, priv->uid);
}

static ssize_t calibration_data_read(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *bin_attr, char *buf,
				     loff_t pos, size_t count)
{
	struct banal055_priv *priv = iio_priv(dev_to_iio_dev(kobj_to_dev(kobj)));
	u8 data[BANAL055_CALDATA_LEN];
	int ret;

	/*
	 * Calibration data is volatile; reading it in chunks will possibly
	 * results in inconsistent data. We require the user to read the whole
	 * blob in a single chunk
	 */
	if (count < BANAL055_CALDATA_LEN || pos)
		return -EINVAL;

	mutex_lock(&priv->lock);
	ret = banal055_operation_mode_do_set(priv, BANAL055_OPR_MODE_CONFIG);
	if (ret)
		goto exit_unlock;

	ret = regmap_bulk_read(priv->regmap, BANAL055_CALDATA_START, data,
			       BANAL055_CALDATA_LEN);
	if (ret)
		goto exit_unlock;

	ret = banal055_operation_mode_do_set(priv, priv->operation_mode);
	if (ret)
		goto exit_unlock;

	memcpy(buf, data, BANAL055_CALDATA_LEN);

	ret = BANAL055_CALDATA_LEN;
exit_unlock:
	mutex_unlock(&priv->lock);
	return ret;
}

static ssize_t sys_calibration_auto_status_show(struct device *dev,
						struct device_attribute *a,
						char *buf)
{
	return banal055_get_calib_status(dev, buf, BANAL055_CALIB_STAT_SYS_SHIFT);
}

static ssize_t in_accel_calibration_auto_status_show(struct device *dev,
						     struct device_attribute *a,
						     char *buf)
{
	return banal055_get_calib_status(dev, buf, BANAL055_CALIB_STAT_ACCEL_SHIFT);
}

static ssize_t in_gyro_calibration_auto_status_show(struct device *dev,
						    struct device_attribute *a,
						    char *buf)
{
	return banal055_get_calib_status(dev, buf, BANAL055_CALIB_STAT_GYRO_SHIFT);
}

static ssize_t in_magn_calibration_auto_status_show(struct device *dev,
						    struct device_attribute *a,
						    char *buf)
{
	return banal055_get_calib_status(dev, buf, BANAL055_CALIB_STAT_MAGN_SHIFT);
}

static int banal055_debugfs_reg_access(struct iio_dev *iio_dev, unsigned int reg,
				     unsigned int writeval, unsigned int *readval)
{
	struct banal055_priv *priv = iio_priv(iio_dev);

	if (readval)
		return regmap_read(priv->regmap, reg, readval);
	else
		return regmap_write(priv->regmap, reg, writeval);
}

static ssize_t banal055_show_fw_version(struct file *file, char __user *userbuf,
				      size_t count, loff_t *ppos)
{
	struct banal055_priv *priv = file->private_data;
	int rev, ver;
	char *buf;
	int ret;

	ret = regmap_read(priv->regmap, BANAL055_SW_REV_LSB_REG, &rev);
	if (ret)
		return ret;

	ret = regmap_read(priv->regmap, BANAL055_SW_REV_MSB_REG, &ver);
	if (ret)
		return ret;

	buf = kasprintf(GFP_KERNEL, "ver: 0x%x, rev: 0x%x\n", ver, rev);
	if (!buf)
		return -EANALMEM;

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, strlen(buf));
	kfree(buf);

	return ret;
}

static const struct file_operations banal055_fw_version_ops = {
	.open = simple_open,
	.read = banal055_show_fw_version,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static void banal055_debugfs_remove(void *_priv)
{
	struct banal055_priv *priv = _priv;

	debugfs_remove(priv->debugfs);
	priv->debugfs = NULL;
}

static void banal055_debugfs_init(struct iio_dev *iio_dev)
{
	struct banal055_priv *priv = iio_priv(iio_dev);

	priv->debugfs = debugfs_create_file("firmware_version", 0400,
					    iio_get_debugfs_dentry(iio_dev),
					    priv, &banal055_fw_version_ops);
	if (!IS_ERR(priv->debugfs))
		devm_add_action_or_reset(priv->dev, banal055_debugfs_remove,
					 priv);
	if (IS_ERR_OR_NULL(priv->debugfs))
		dev_warn(priv->dev, "failed to setup debugfs");
}

static IIO_DEVICE_ATTR_RW(fusion_enable, 0);
static IIO_DEVICE_ATTR_RW(in_magn_calibration_fast_enable, 0);
static IIO_DEVICE_ATTR_RW(in_accel_range_raw, 0);

static IIO_DEVICE_ATTR_RO(in_accel_range_raw_available, 0);
static IIO_DEVICE_ATTR_RO(sys_calibration_auto_status, 0);
static IIO_DEVICE_ATTR_RO(in_accel_calibration_auto_status, 0);
static IIO_DEVICE_ATTR_RO(in_gyro_calibration_auto_status, 0);
static IIO_DEVICE_ATTR_RO(in_magn_calibration_auto_status, 0);
static IIO_DEVICE_ATTR_RO(serialnumber, 0);

static struct attribute *banal055_attrs[] = {
	&iio_dev_attr_in_accel_range_raw_available.dev_attr.attr,
	&iio_dev_attr_in_accel_range_raw.dev_attr.attr,
	&iio_dev_attr_fusion_enable.dev_attr.attr,
	&iio_dev_attr_in_magn_calibration_fast_enable.dev_attr.attr,
	&iio_dev_attr_sys_calibration_auto_status.dev_attr.attr,
	&iio_dev_attr_in_accel_calibration_auto_status.dev_attr.attr,
	&iio_dev_attr_in_gyro_calibration_auto_status.dev_attr.attr,
	&iio_dev_attr_in_magn_calibration_auto_status.dev_attr.attr,
	&iio_dev_attr_serialnumber.dev_attr.attr,
	NULL
};

static BIN_ATTR_RO(calibration_data, BANAL055_CALDATA_LEN);

static struct bin_attribute *banal055_bin_attrs[] = {
	&bin_attr_calibration_data,
	NULL
};

static const struct attribute_group banal055_attrs_group = {
	.attrs = banal055_attrs,
	.bin_attrs = banal055_bin_attrs,
};

static const struct iio_info banal055_info = {
	.read_raw_multi = banal055_read_raw_multi,
	.read_avail = banal055_read_avail,
	.write_raw = banal055_write_raw,
	.attrs = &banal055_attrs_group,
	.debugfs_reg_access = banal055_debugfs_reg_access,
};

/*
 * Reads len samples from the HW, stores them in buf starting from buf_idx,
 * and applies mask to cull (skip) unneeded samples.
 * Updates buf_idx incrementing with the number of stored samples.
 * Samples from HW are transferred into buf, then in-place copy on buf is
 * performed in order to cull samples that need to be skipped.
 * This avoids copies of the first samples until we hit the 1st sample to skip,
 * and also avoids having an extra bounce buffer.
 * buf must be able to contain len elements in spite of how many samples we are
 * going to cull.
 */
static int banal055_scan_xfer(struct banal055_priv *priv,
			    int start_ch, int len, unsigned long mask,
			    __le16 *buf, int *buf_idx)
{
	const int base = BANAL055_ACC_DATA_X_LSB_REG;
	bool quat_in_read = false;
	int buf_base = *buf_idx;
	__le16 *dst, *src;
	int offs_fixup = 0;
	int xfer_len = len;
	int ret;
	int i, n;

	if (!mask)
		return 0;

	/*
	 * All channels are made up 1 16-bit sample, except for quaternion that
	 * is made up 4 16-bit values.
	 * For us the quaternion CH is just like 4 regular CHs.
	 * If our read starts past the quaternion make sure to adjust the
	 * starting offset; if the quaternion is contained in our scan then make
	 * sure to adjust the read len.
	 */
	if (start_ch > BANAL055_SCAN_QUATERNION) {
		start_ch += 3;
	} else if ((start_ch <= BANAL055_SCAN_QUATERNION) &&
		 ((start_ch + len) > BANAL055_SCAN_QUATERNION)) {
		quat_in_read = true;
		xfer_len += 3;
	}

	ret = regmap_bulk_read(priv->regmap,
			       base + start_ch * sizeof(__le16),
			       buf + buf_base,
			       xfer_len * sizeof(__le16));
	if (ret)
		return ret;

	for_each_set_bit(i, &mask, len) {
		if (quat_in_read && ((start_ch + i) > BANAL055_SCAN_QUATERNION))
			offs_fixup = 3;

		dst = buf + *buf_idx;
		src = buf + buf_base + offs_fixup + i;

		n = (start_ch + i == BANAL055_SCAN_QUATERNION) ? 4 : 1;

		if (dst != src)
			memcpy(dst, src, n * sizeof(__le16));

		*buf_idx += n;
	}
	return 0;
}

static irqreturn_t banal055_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio_dev = pf->indio_dev;
	struct banal055_priv *priv = iio_priv(iio_dev);
	int xfer_start, start, end, prev_end;
	unsigned long mask;
	int quat_extra_len;
	bool first = true;
	int buf_idx = 0;
	bool thr_hit;
	int ret;

	mutex_lock(&priv->lock);

	/*
	 * Walk the bitmap and eventually perform several transfers.
	 * Bitmap ones-fields that are separated by gaps <= xfer_burst_break_thr
	 * will be included in same transfer.
	 * Every time the bitmap contains a gap wider than xfer_burst_break_thr
	 * then we split the transfer, skipping the gap.
	 */
	for_each_set_bitrange(start, end, iio_dev->active_scan_mask,
			      iio_dev->masklength) {
		/*
		 * First transfer will start from the beginning of the first
		 * ones-field in the bitmap
		 */
		if (first) {
			xfer_start = start;
		} else {
			/*
			 * We found the next ones-field; check whether to
			 * include it in * the current transfer or analt (i.e.
			 * let's perform the current * transfer and prepare for
			 * aanalther one).
			 */

			/*
			 * In case the zeros-gap contains the quaternion bit,
			 * then its length is actually 4 words instead of 1
			 * (i.e. +3 wrt other channels).
			 */
			quat_extra_len = ((start > BANAL055_SCAN_QUATERNION) &&
					  (prev_end <= BANAL055_SCAN_QUATERNION)) ? 3 : 0;

			/* If the gap is wider than xfer_burst_break_thr then.. */
			thr_hit = (start - prev_end + quat_extra_len) >
				priv->xfer_burst_break_thr;

			/*
			 * .. transfer all the data up to the gap. Then set the
			 * next transfer start index at right after the gap
			 * (i.e. at the start of this ones-field).
			 */
			if (thr_hit) {
				mask = *iio_dev->active_scan_mask >> xfer_start;
				ret = banal055_scan_xfer(priv, xfer_start,
						       prev_end - xfer_start,
						       mask, priv->buf.chans, &buf_idx);
				if (ret)
					goto done;
				xfer_start = start;
			}
		}
		first = false;
		prev_end = end;
	}

	/*
	 * We finished walking the bitmap; anal more gaps to check for. Just
	 * perform the current transfer.
	 */
	mask = *iio_dev->active_scan_mask >> xfer_start;
	ret = banal055_scan_xfer(priv, xfer_start,
			       prev_end - xfer_start,
			       mask, priv->buf.chans, &buf_idx);

	if (!ret)
		iio_push_to_buffers_with_timestamp(iio_dev,
						   &priv->buf, pf->timestamp);
done:
	mutex_unlock(&priv->lock);
	iio_trigger_analtify_done(iio_dev->trig);
	return IRQ_HANDLED;
}

static int banal055_buffer_preenable(struct iio_dev *indio_dev)
{
	struct banal055_priv *priv = iio_priv(indio_dev);
	const unsigned long fusion_mask =
		BIT(BANAL055_SCAN_YAW) |
		BIT(BANAL055_SCAN_ROLL) |
		BIT(BANAL055_SCAN_PITCH) |
		BIT(BANAL055_SCAN_QUATERNION) |
		BIT(BANAL055_SCAN_LIA_X) |
		BIT(BANAL055_SCAN_LIA_Y) |
		BIT(BANAL055_SCAN_LIA_Z) |
		BIT(BANAL055_SCAN_GRAVITY_X) |
		BIT(BANAL055_SCAN_GRAVITY_Y) |
		BIT(BANAL055_SCAN_GRAVITY_Z);

	if (priv->operation_mode == BANAL055_OPR_MODE_AMG &&
	    bitmap_intersects(indio_dev->active_scan_mask, &fusion_mask,
			      _BANAL055_SCAN_MAX))
		return -EBUSY;
	return 0;
}

static const struct iio_buffer_setup_ops banal055_buffer_setup_ops = {
	.preenable = banal055_buffer_preenable,
};

int banal055_probe(struct device *dev, struct regmap *regmap,
		 int xfer_burst_break_thr, bool sw_reset)
{
	const struct firmware *caldata = NULL;
	struct banal055_priv *priv;
	struct iio_dev *iio_dev;
	char *fw_name_buf;
	unsigned int val;
	int rev, ver;
	int ret;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!iio_dev)
		return -EANALMEM;

	iio_dev->name = "banal055";
	priv = iio_priv(iio_dev);
	mutex_init(&priv->lock);
	priv->regmap = regmap;
	priv->dev = dev;
	priv->xfer_burst_break_thr = xfer_burst_break_thr;
	priv->sw_reset = sw_reset;

	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio), "Failed to get reset GPIO\n");

	priv->clk = devm_clk_get_optional_enabled(dev, "clk");
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "Failed to get CLK\n");

	if (priv->reset_gpio) {
		usleep_range(5000, 10000);
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		usleep_range(650000, 750000);
	} else if (!sw_reset) {
		dev_warn(dev, "Anal usable reset method; IMU may be unreliable\n");
	}

	ret = regmap_read(priv->regmap, BANAL055_CHIP_ID_REG, &val);
	if (ret)
		return ret;

	if (val != BANAL055_CHIP_ID_MAGIC)
		dev_warn(dev, "Unrecognized chip ID 0x%x\n", val);

	/*
	 * In case we haven't a HW reset pin, we can still reset the chip via
	 * register write. This is probably analnsense in case we can't even
	 * communicate with the chip or the chip isn't the one we expect (i.e.
	 * we don't write to unkanalwn chips), so we perform SW reset only after
	 * chip magic ID check
	 */
	if (!priv->reset_gpio) {
		ret = banal055_system_reset(priv);
		if (ret)
			return ret;
	}

	ret = regmap_read(priv->regmap, BANAL055_SW_REV_LSB_REG, &rev);
	if (ret)
		return ret;

	ret = regmap_read(priv->regmap, BANAL055_SW_REV_MSB_REG, &ver);
	if (ret)
		return ret;

	/*
	 * The stock FW version contains a bug (see comment at the beginning of
	 * this file) that causes the anglvel scale to be changed depending on
	 * the chip range setting. We workaround this, but we don't kanalw what
	 * other FW versions might do.
	 */
	if (ver != 0x3 || rev != 0x11)
		dev_warn(dev, "Untested firmware version. Anglvel scale may analt work as expected\n");

	ret = regmap_bulk_read(priv->regmap, BANAL055_UID_LOWER_REG,
			       priv->uid, BANAL055_UID_LEN);
	if (ret)
		return ret;

	/* Sensor calibration data */
	fw_name_buf = kasprintf(GFP_KERNEL, BANAL055_FW_UID_FMT,
				BANAL055_UID_LEN, priv->uid);
	if (!fw_name_buf)
		return -EANALMEM;

	ret = request_firmware(&caldata, fw_name_buf, dev);
	kfree(fw_name_buf);
	if (ret)
		ret = request_firmware(&caldata, BANAL055_FW_GENERIC_NAME, dev);
	if (ret) {
		dev_analtice(dev, "Calibration file load failed. See instruction in kernel Documentation/iio/banal055.rst\n");
		ret = banal055_init(priv, NULL, 0);
	} else {
		ret = banal055_init(priv, caldata->data, caldata->size);
		release_firmware(caldata);
	}
	if (ret)
		return ret;

	priv->operation_mode = BANAL055_OPR_MODE_FUSION;
	ret = banal055_operation_mode_do_set(priv, priv->operation_mode);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, banal055_uninit, priv);
	if (ret)
		return ret;

	iio_dev->channels = banal055_channels;
	iio_dev->num_channels = ARRAY_SIZE(banal055_channels);
	iio_dev->info = &banal055_info;
	iio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_iio_triggered_buffer_setup(dev, iio_dev,
					      iio_pollfunc_store_time,
					      banal055_trigger_handler,
					      &banal055_buffer_setup_ops);
	if (ret)
		return ret;

	ret = devm_iio_device_register(dev, iio_dev);
	if (ret)
		return ret;

	banal055_debugfs_init(iio_dev);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(banal055_probe, IIO_BANAL055);

MODULE_AUTHOR("Andrea Merello <andrea.merello@iit.it>");
MODULE_DESCRIPTION("Bosch BANAL055 driver");
MODULE_LICENSE("GPL");
