// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2012 Invensense, Inc.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/iio/iio.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include "inv_mpu_iio.h"
#include "inv_mpu_magn.h"

/*
 * this is the gyro scale translated from dynamic range plus/minus
 * {250, 500, 1000, 2000} to rad/s
 */
static const int gyro_scale_6050[] = {133090, 266181, 532362, 1064724};

/*
 * this is the accel scale translated from dynamic range plus/minus
 * {2, 4, 8, 16} to m/s^2
 */
static const int accel_scale[] = {598, 1196, 2392, 4785};

static const struct inv_mpu6050_reg_map reg_set_icm20602 = {
	.sample_rate_div	= INV_MPU6050_REG_SAMPLE_RATE_DIV,
	.lpf                    = INV_MPU6050_REG_CONFIG,
	.accel_lpf              = INV_MPU6500_REG_ACCEL_CONFIG_2,
	.user_ctrl              = INV_MPU6050_REG_USER_CTRL,
	.fifo_en                = INV_MPU6050_REG_FIFO_EN,
	.gyro_config            = INV_MPU6050_REG_GYRO_CONFIG,
	.accl_config            = INV_MPU6050_REG_ACCEL_CONFIG,
	.fifo_count_h           = INV_MPU6050_REG_FIFO_COUNT_H,
	.fifo_r_w               = INV_MPU6050_REG_FIFO_R_W,
	.raw_gyro               = INV_MPU6050_REG_RAW_GYRO,
	.raw_accl               = INV_MPU6050_REG_RAW_ACCEL,
	.temperature            = INV_MPU6050_REG_TEMPERATURE,
	.int_enable             = INV_MPU6050_REG_INT_ENABLE,
	.int_status             = INV_MPU6050_REG_INT_STATUS,
	.pwr_mgmt_1             = INV_MPU6050_REG_PWR_MGMT_1,
	.pwr_mgmt_2             = INV_MPU6050_REG_PWR_MGMT_2,
	.int_pin_cfg            = INV_MPU6050_REG_INT_PIN_CFG,
	.accl_offset            = INV_MPU6500_REG_ACCEL_OFFSET,
	.gyro_offset            = INV_MPU6050_REG_GYRO_OFFSET,
	.i2c_if                 = INV_ICM20602_REG_I2C_IF,
};

static const struct inv_mpu6050_reg_map reg_set_6500 = {
	.sample_rate_div	= INV_MPU6050_REG_SAMPLE_RATE_DIV,
	.lpf                    = INV_MPU6050_REG_CONFIG,
	.accel_lpf              = INV_MPU6500_REG_ACCEL_CONFIG_2,
	.user_ctrl              = INV_MPU6050_REG_USER_CTRL,
	.fifo_en                = INV_MPU6050_REG_FIFO_EN,
	.gyro_config            = INV_MPU6050_REG_GYRO_CONFIG,
	.accl_config            = INV_MPU6050_REG_ACCEL_CONFIG,
	.fifo_count_h           = INV_MPU6050_REG_FIFO_COUNT_H,
	.fifo_r_w               = INV_MPU6050_REG_FIFO_R_W,
	.raw_gyro               = INV_MPU6050_REG_RAW_GYRO,
	.raw_accl               = INV_MPU6050_REG_RAW_ACCEL,
	.temperature            = INV_MPU6050_REG_TEMPERATURE,
	.int_enable             = INV_MPU6050_REG_INT_ENABLE,
	.int_status             = INV_MPU6050_REG_INT_STATUS,
	.pwr_mgmt_1             = INV_MPU6050_REG_PWR_MGMT_1,
	.pwr_mgmt_2             = INV_MPU6050_REG_PWR_MGMT_2,
	.int_pin_cfg		= INV_MPU6050_REG_INT_PIN_CFG,
	.accl_offset		= INV_MPU6500_REG_ACCEL_OFFSET,
	.gyro_offset		= INV_MPU6050_REG_GYRO_OFFSET,
	.i2c_if                 = 0,
};

static const struct inv_mpu6050_reg_map reg_set_6050 = {
	.sample_rate_div	= INV_MPU6050_REG_SAMPLE_RATE_DIV,
	.lpf                    = INV_MPU6050_REG_CONFIG,
	.user_ctrl              = INV_MPU6050_REG_USER_CTRL,
	.fifo_en                = INV_MPU6050_REG_FIFO_EN,
	.gyro_config            = INV_MPU6050_REG_GYRO_CONFIG,
	.accl_config            = INV_MPU6050_REG_ACCEL_CONFIG,
	.fifo_count_h           = INV_MPU6050_REG_FIFO_COUNT_H,
	.fifo_r_w               = INV_MPU6050_REG_FIFO_R_W,
	.raw_gyro               = INV_MPU6050_REG_RAW_GYRO,
	.raw_accl               = INV_MPU6050_REG_RAW_ACCEL,
	.temperature            = INV_MPU6050_REG_TEMPERATURE,
	.int_enable             = INV_MPU6050_REG_INT_ENABLE,
	.pwr_mgmt_1             = INV_MPU6050_REG_PWR_MGMT_1,
	.pwr_mgmt_2             = INV_MPU6050_REG_PWR_MGMT_2,
	.int_pin_cfg		= INV_MPU6050_REG_INT_PIN_CFG,
	.accl_offset		= INV_MPU6050_REG_ACCEL_OFFSET,
	.gyro_offset		= INV_MPU6050_REG_GYRO_OFFSET,
	.i2c_if                 = 0,
};

static const struct inv_mpu6050_chip_config chip_config_6050 = {
	.clk = INV_CLK_INTERNAL,
	.fsr = INV_MPU6050_FSR_2000DPS,
	.lpf = INV_MPU6050_FILTER_20HZ,
	.divider = INV_MPU6050_FIFO_RATE_TO_DIVIDER(50),
	.gyro_en = true,
	.accl_en = true,
	.temp_en = true,
	.magn_en = false,
	.gyro_fifo_enable = false,
	.accl_fifo_enable = false,
	.temp_fifo_enable = false,
	.magn_fifo_enable = false,
	.accl_fs = INV_MPU6050_FS_02G,
	.user_ctrl = 0,
};

static const struct inv_mpu6050_chip_config chip_config_6500 = {
	.clk = INV_CLK_PLL,
	.fsr = INV_MPU6050_FSR_2000DPS,
	.lpf = INV_MPU6050_FILTER_20HZ,
	.divider = INV_MPU6050_FIFO_RATE_TO_DIVIDER(50),
	.gyro_en = true,
	.accl_en = true,
	.temp_en = true,
	.magn_en = false,
	.gyro_fifo_enable = false,
	.accl_fifo_enable = false,
	.temp_fifo_enable = false,
	.magn_fifo_enable = false,
	.accl_fs = INV_MPU6050_FS_02G,
	.user_ctrl = 0,
};

/* Indexed by enum inv_devices */
static const struct inv_mpu6050_hw hw_info[] = {
	{
		.whoami = INV_MPU6050_WHOAMI_VALUE,
		.name = "MPU6050",
		.reg = &reg_set_6050,
		.config = &chip_config_6050,
		.fifo_size = 1024,
		.temp = {INV_MPU6050_TEMP_OFFSET, INV_MPU6050_TEMP_SCALE},
		.startup_time = {INV_MPU6050_GYRO_STARTUP_TIME, INV_MPU6050_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_MPU6500_WHOAMI_VALUE,
		.name = "MPU6500",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 512,
		.temp = {INV_MPU6500_TEMP_OFFSET, INV_MPU6500_TEMP_SCALE},
		.startup_time = {INV_MPU6500_GYRO_STARTUP_TIME, INV_MPU6500_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_MPU6515_WHOAMI_VALUE,
		.name = "MPU6515",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 512,
		.temp = {INV_MPU6500_TEMP_OFFSET, INV_MPU6500_TEMP_SCALE},
		.startup_time = {INV_MPU6500_GYRO_STARTUP_TIME, INV_MPU6500_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_MPU6880_WHOAMI_VALUE,
		.name = "MPU6880",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 4096,
		.temp = {INV_MPU6500_TEMP_OFFSET, INV_MPU6500_TEMP_SCALE},
		.startup_time = {INV_MPU6500_GYRO_STARTUP_TIME, INV_MPU6500_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_MPU6000_WHOAMI_VALUE,
		.name = "MPU6000",
		.reg = &reg_set_6050,
		.config = &chip_config_6050,
		.fifo_size = 1024,
		.temp = {INV_MPU6050_TEMP_OFFSET, INV_MPU6050_TEMP_SCALE},
		.startup_time = {INV_MPU6050_GYRO_STARTUP_TIME, INV_MPU6050_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_MPU9150_WHOAMI_VALUE,
		.name = "MPU9150",
		.reg = &reg_set_6050,
		.config = &chip_config_6050,
		.fifo_size = 1024,
		.temp = {INV_MPU6050_TEMP_OFFSET, INV_MPU6050_TEMP_SCALE},
		.startup_time = {INV_MPU6050_GYRO_STARTUP_TIME, INV_MPU6050_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_MPU9250_WHOAMI_VALUE,
		.name = "MPU9250",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 512,
		.temp = {INV_MPU6500_TEMP_OFFSET, INV_MPU6500_TEMP_SCALE},
		.startup_time = {INV_MPU6500_GYRO_STARTUP_TIME, INV_MPU6500_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_MPU9255_WHOAMI_VALUE,
		.name = "MPU9255",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 512,
		.temp = {INV_MPU6500_TEMP_OFFSET, INV_MPU6500_TEMP_SCALE},
		.startup_time = {INV_MPU6500_GYRO_STARTUP_TIME, INV_MPU6500_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_ICM20608_WHOAMI_VALUE,
		.name = "ICM20608",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 512,
		.temp = {INV_ICM20608_TEMP_OFFSET, INV_ICM20608_TEMP_SCALE},
		.startup_time = {INV_MPU6500_GYRO_STARTUP_TIME, INV_MPU6500_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_ICM20608D_WHOAMI_VALUE,
		.name = "ICM20608D",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 512,
		.temp = {INV_ICM20608_TEMP_OFFSET, INV_ICM20608_TEMP_SCALE},
		.startup_time = {INV_MPU6500_GYRO_STARTUP_TIME, INV_MPU6500_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_ICM20609_WHOAMI_VALUE,
		.name = "ICM20609",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 4 * 1024,
		.temp = {INV_ICM20608_TEMP_OFFSET, INV_ICM20608_TEMP_SCALE},
		.startup_time = {INV_MPU6500_GYRO_STARTUP_TIME, INV_MPU6500_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_ICM20689_WHOAMI_VALUE,
		.name = "ICM20689",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 4 * 1024,
		.temp = {INV_ICM20608_TEMP_OFFSET, INV_ICM20608_TEMP_SCALE},
		.startup_time = {INV_MPU6500_GYRO_STARTUP_TIME, INV_MPU6500_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_ICM20600_WHOAMI_VALUE,
		.name = "ICM20600",
		.reg = &reg_set_icm20602,
		.config = &chip_config_6500,
		.fifo_size = 1008,
		.temp = {INV_ICM20608_TEMP_OFFSET, INV_ICM20608_TEMP_SCALE},
		.startup_time = {INV_ICM20602_GYRO_STARTUP_TIME, INV_ICM20602_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_ICM20602_WHOAMI_VALUE,
		.name = "ICM20602",
		.reg = &reg_set_icm20602,
		.config = &chip_config_6500,
		.fifo_size = 1008,
		.temp = {INV_ICM20608_TEMP_OFFSET, INV_ICM20608_TEMP_SCALE},
		.startup_time = {INV_ICM20602_GYRO_STARTUP_TIME, INV_ICM20602_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_ICM20690_WHOAMI_VALUE,
		.name = "ICM20690",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 1024,
		.temp = {INV_ICM20608_TEMP_OFFSET, INV_ICM20608_TEMP_SCALE},
		.startup_time = {INV_ICM20690_GYRO_STARTUP_TIME, INV_ICM20690_ACCEL_STARTUP_TIME},
	},
	{
		.whoami = INV_IAM20680_WHOAMI_VALUE,
		.name = "IAM20680",
		.reg = &reg_set_6500,
		.config = &chip_config_6500,
		.fifo_size = 512,
		.temp = {INV_ICM20608_TEMP_OFFSET, INV_ICM20608_TEMP_SCALE},
		.startup_time = {INV_MPU6500_GYRO_STARTUP_TIME, INV_MPU6500_ACCEL_STARTUP_TIME},
	},
};

static int inv_mpu6050_pwr_mgmt_1_write(struct inv_mpu6050_state *st, bool sleep,
					int clock, int temp_dis)
{
	u8 val;

	if (clock < 0)
		clock = st->chip_config.clk;
	if (temp_dis < 0)
		temp_dis = !st->chip_config.temp_en;

	val = clock & INV_MPU6050_BIT_CLK_MASK;
	if (temp_dis)
		val |= INV_MPU6050_BIT_TEMP_DIS;
	if (sleep)
		val |= INV_MPU6050_BIT_SLEEP;

	dev_dbg(regmap_get_device(st->map), "pwr_mgmt_1: 0x%x\n", val);
	return regmap_write(st->map, st->reg->pwr_mgmt_1, val);
}

static int inv_mpu6050_clock_switch(struct inv_mpu6050_state *st,
				    unsigned int clock)
{
	int ret;

	switch (st->chip_type) {
	case INV_MPU6050:
	case INV_MPU6000:
	case INV_MPU9150:
		/* old chips: switch clock manually */
		ret = inv_mpu6050_pwr_mgmt_1_write(st, false, clock, -1);
		if (ret)
			return ret;
		st->chip_config.clk = clock;
		break;
	default:
		/* automatic clock switching, nothing to do */
		break;
	}

	return 0;
}

int inv_mpu6050_switch_engine(struct inv_mpu6050_state *st, bool en,
			      unsigned int mask)
{
	unsigned int sleep;
	u8 pwr_mgmt2, user_ctrl;
	int ret;

	/* delete useless requests */
	if (mask & INV_MPU6050_SENSOR_ACCL && en == st->chip_config.accl_en)
		mask &= ~INV_MPU6050_SENSOR_ACCL;
	if (mask & INV_MPU6050_SENSOR_GYRO && en == st->chip_config.gyro_en)
		mask &= ~INV_MPU6050_SENSOR_GYRO;
	if (mask & INV_MPU6050_SENSOR_TEMP && en == st->chip_config.temp_en)
		mask &= ~INV_MPU6050_SENSOR_TEMP;
	if (mask & INV_MPU6050_SENSOR_MAGN && en == st->chip_config.magn_en)
		mask &= ~INV_MPU6050_SENSOR_MAGN;
	if (mask == 0)
		return 0;

	/* turn on/off temperature sensor */
	if (mask & INV_MPU6050_SENSOR_TEMP) {
		ret = inv_mpu6050_pwr_mgmt_1_write(st, false, -1, !en);
		if (ret)
			return ret;
		st->chip_config.temp_en = en;
	}

	/* update user_crtl for driving magnetometer */
	if (mask & INV_MPU6050_SENSOR_MAGN) {
		user_ctrl = st->chip_config.user_ctrl;
		if (en)
			user_ctrl |= INV_MPU6050_BIT_I2C_MST_EN;
		else
			user_ctrl &= ~INV_MPU6050_BIT_I2C_MST_EN;
		ret = regmap_write(st->map, st->reg->user_ctrl, user_ctrl);
		if (ret)
			return ret;
		st->chip_config.user_ctrl = user_ctrl;
		st->chip_config.magn_en = en;
	}

	/* manage accel & gyro engines */
	if (mask & (INV_MPU6050_SENSOR_ACCL | INV_MPU6050_SENSOR_GYRO)) {
		/* compute power management 2 current value */
		pwr_mgmt2 = 0;
		if (!st->chip_config.accl_en)
			pwr_mgmt2 |= INV_MPU6050_BIT_PWR_ACCL_STBY;
		if (!st->chip_config.gyro_en)
			pwr_mgmt2 |= INV_MPU6050_BIT_PWR_GYRO_STBY;

		/* update to new requested value */
		if (mask & INV_MPU6050_SENSOR_ACCL) {
			if (en)
				pwr_mgmt2 &= ~INV_MPU6050_BIT_PWR_ACCL_STBY;
			else
				pwr_mgmt2 |= INV_MPU6050_BIT_PWR_ACCL_STBY;
		}
		if (mask & INV_MPU6050_SENSOR_GYRO) {
			if (en)
				pwr_mgmt2 &= ~INV_MPU6050_BIT_PWR_GYRO_STBY;
			else
				pwr_mgmt2 |= INV_MPU6050_BIT_PWR_GYRO_STBY;
		}

		/* switch clock to internal when turning gyro off */
		if (mask & INV_MPU6050_SENSOR_GYRO && !en) {
			ret = inv_mpu6050_clock_switch(st, INV_CLK_INTERNAL);
			if (ret)
				return ret;
		}

		/* update sensors engine */
		dev_dbg(regmap_get_device(st->map), "pwr_mgmt_2: 0x%x\n",
			pwr_mgmt2);
		ret = regmap_write(st->map, st->reg->pwr_mgmt_2, pwr_mgmt2);
		if (ret)
			return ret;
		if (mask & INV_MPU6050_SENSOR_ACCL)
			st->chip_config.accl_en = en;
		if (mask & INV_MPU6050_SENSOR_GYRO)
			st->chip_config.gyro_en = en;

		/* compute required time to have sensors stabilized */
		sleep = 0;
		if (en) {
			if (mask & INV_MPU6050_SENSOR_ACCL) {
				if (sleep < st->hw->startup_time.accel)
					sleep = st->hw->startup_time.accel;
			}
			if (mask & INV_MPU6050_SENSOR_GYRO) {
				if (sleep < st->hw->startup_time.gyro)
					sleep = st->hw->startup_time.gyro;
			}
		} else {
			if (mask & INV_MPU6050_SENSOR_GYRO) {
				if (sleep < INV_MPU6050_GYRO_DOWN_TIME)
					sleep = INV_MPU6050_GYRO_DOWN_TIME;
			}
		}
		if (sleep)
			msleep(sleep);

		/* switch clock to PLL when turning gyro on */
		if (mask & INV_MPU6050_SENSOR_GYRO && en) {
			ret = inv_mpu6050_clock_switch(st, INV_CLK_PLL);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int inv_mpu6050_set_power_itg(struct inv_mpu6050_state *st,
				     bool power_on)
{
	int result;

	result = inv_mpu6050_pwr_mgmt_1_write(st, !power_on, -1, -1);
	if (result)
		return result;

	if (power_on)
		usleep_range(INV_MPU6050_REG_UP_TIME_MIN,
			     INV_MPU6050_REG_UP_TIME_MAX);

	return 0;
}

static int inv_mpu6050_set_gyro_fsr(struct inv_mpu6050_state *st,
				    enum inv_mpu6050_fsr_e val)
{
	unsigned int gyro_shift;
	u8 data;

	switch (st->chip_type) {
	case INV_ICM20690:
		gyro_shift = INV_ICM20690_GYRO_CONFIG_FSR_SHIFT;
		break;
	default:
		gyro_shift = INV_MPU6050_GYRO_CONFIG_FSR_SHIFT;
		break;
	}

	data = val << gyro_shift;
	return regmap_write(st->map, st->reg->gyro_config, data);
}

/*
 *  inv_mpu6050_set_lpf_regs() - set low pass filter registers, chip dependent
 *
 *  MPU60xx/MPU9150 use only 1 register for accelerometer + gyroscope
 *  MPU6500 and above have a dedicated register for accelerometer
 */
static int inv_mpu6050_set_lpf_regs(struct inv_mpu6050_state *st,
				    enum inv_mpu6050_filter_e val)
{
	int result;

	result = regmap_write(st->map, st->reg->lpf, val);
	if (result)
		return result;

	/* set accel lpf */
	switch (st->chip_type) {
	case INV_MPU6050:
	case INV_MPU6000:
	case INV_MPU9150:
		/* old chips, nothing to do */
		return 0;
	case INV_ICM20689:
	case INV_ICM20690:
		/* set FIFO size to maximum value */
		val |= INV_ICM20689_BITS_FIFO_SIZE_MAX;
		break;
	default:
		break;
	}

	return regmap_write(st->map, st->reg->accel_lpf, val);
}

/*
 *  inv_mpu6050_init_config() - Initialize hardware, disable FIFO.
 *
 *  Initial configuration:
 *  FSR: ± 2000DPS
 *  DLPF: 20Hz
 *  FIFO rate: 50Hz
 *  Clock source: Gyro PLL
 */
static int inv_mpu6050_init_config(struct iio_dev *indio_dev)
{
	int result;
	u8 d;
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	result = inv_mpu6050_set_gyro_fsr(st, st->chip_config.fsr);
	if (result)
		return result;

	result = inv_mpu6050_set_lpf_regs(st, st->chip_config.lpf);
	if (result)
		return result;

	d = st->chip_config.divider;
	result = regmap_write(st->map, st->reg->sample_rate_div, d);
	if (result)
		return result;

	d = (st->chip_config.accl_fs << INV_MPU6050_ACCL_CONFIG_FSR_SHIFT);
	result = regmap_write(st->map, st->reg->accl_config, d);
	if (result)
		return result;

	result = regmap_write(st->map, st->reg->int_pin_cfg, st->irq_mask);
	if (result)
		return result;

	/*
	 * Internal chip period is 1ms (1kHz).
	 * Let's use at the beginning the theorical value before measuring
	 * with interrupt timestamps.
	 */
	st->chip_period = NSEC_PER_MSEC;

	/* magn chip init, noop if not present in the chip */
	result = inv_mpu_magn_probe(st);
	if (result)
		return result;

	return 0;
}

static int inv_mpu6050_sensor_set(struct inv_mpu6050_state  *st, int reg,
				int axis, int val)
{
	int ind, result;
	__be16 d = cpu_to_be16(val);

	ind = (axis - IIO_MOD_X) * 2;
	result = regmap_bulk_write(st->map, reg + ind, &d, sizeof(d));
	if (result)
		return -EINVAL;

	return 0;
}

static int inv_mpu6050_sensor_show(struct inv_mpu6050_state  *st, int reg,
				   int axis, int *val)
{
	int ind, result;
	__be16 d;

	ind = (axis - IIO_MOD_X) * 2;
	result = regmap_bulk_read(st->map, reg + ind, &d, sizeof(d));
	if (result)
		return -EINVAL;
	*val = (short)be16_to_cpup(&d);

	return IIO_VAL_INT;
}

static int inv_mpu6050_read_channel_data(struct iio_dev *indio_dev,
					 struct iio_chan_spec const *chan,
					 int *val)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	struct device *pdev = regmap_get_device(st->map);
	unsigned int freq_hz, period_us, min_sleep_us, max_sleep_us;
	int result;
	int ret;

	/* compute sample period */
	freq_hz = INV_MPU6050_DIVIDER_TO_FIFO_RATE(st->chip_config.divider);
	period_us = 1000000 / freq_hz;

	result = pm_runtime_resume_and_get(pdev);
	if (result)
		return result;

	switch (chan->type) {
	case IIO_ANGL_VEL:
		if (!st->chip_config.gyro_en) {
			result = inv_mpu6050_switch_engine(st, true,
					INV_MPU6050_SENSOR_GYRO);
			if (result)
				goto error_power_off;
			/* need to wait 2 periods to have first valid sample */
			min_sleep_us = 2 * period_us;
			max_sleep_us = 2 * (period_us + period_us / 2);
			usleep_range(min_sleep_us, max_sleep_us);
		}
		ret = inv_mpu6050_sensor_show(st, st->reg->raw_gyro,
					      chan->channel2, val);
		break;
	case IIO_ACCEL:
		if (!st->chip_config.accl_en) {
			result = inv_mpu6050_switch_engine(st, true,
					INV_MPU6050_SENSOR_ACCL);
			if (result)
				goto error_power_off;
			/* wait 1 period for first sample availability */
			min_sleep_us = period_us;
			max_sleep_us = period_us + period_us / 2;
			usleep_range(min_sleep_us, max_sleep_us);
		}
		ret = inv_mpu6050_sensor_show(st, st->reg->raw_accl,
					      chan->channel2, val);
		break;
	case IIO_TEMP:
		/* temperature sensor work only with accel and/or gyro */
		if (!st->chip_config.accl_en && !st->chip_config.gyro_en) {
			result = -EBUSY;
			goto error_power_off;
		}
		if (!st->chip_config.temp_en) {
			result = inv_mpu6050_switch_engine(st, true,
					INV_MPU6050_SENSOR_TEMP);
			if (result)
				goto error_power_off;
			/* wait 1 period for first sample availability */
			min_sleep_us = period_us;
			max_sleep_us = period_us + period_us / 2;
			usleep_range(min_sleep_us, max_sleep_us);
		}
		ret = inv_mpu6050_sensor_show(st, st->reg->temperature,
					      IIO_MOD_X, val);
		break;
	case IIO_MAGN:
		if (!st->chip_config.magn_en) {
			result = inv_mpu6050_switch_engine(st, true,
					INV_MPU6050_SENSOR_MAGN);
			if (result)
				goto error_power_off;
			/* frequency is limited for magnetometer */
			if (freq_hz > INV_MPU_MAGN_FREQ_HZ_MAX) {
				freq_hz = INV_MPU_MAGN_FREQ_HZ_MAX;
				period_us = 1000000 / freq_hz;
			}
			/* need to wait 2 periods to have first valid sample */
			min_sleep_us = 2 * period_us;
			max_sleep_us = 2 * (period_us + period_us / 2);
			usleep_range(min_sleep_us, max_sleep_us);
		}
		ret = inv_mpu_magn_read(st, chan->channel2, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(pdev);
	pm_runtime_put_autosuspend(pdev);

	return ret;

error_power_off:
	pm_runtime_put_autosuspend(pdev);
	return result;
}

static int
inv_mpu6050_read_raw(struct iio_dev *indio_dev,
		     struct iio_chan_spec const *chan,
		     int *val, int *val2, long mask)
{
	struct inv_mpu6050_state  *st = iio_priv(indio_dev);
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		mutex_lock(&st->lock);
		ret = inv_mpu6050_read_channel_data(indio_dev, chan, val);
		mutex_unlock(&st->lock);
		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			mutex_lock(&st->lock);
			*val  = 0;
			*val2 = gyro_scale_6050[st->chip_config.fsr];
			mutex_unlock(&st->lock);

			return IIO_VAL_INT_PLUS_NANO;
		case IIO_ACCEL:
			mutex_lock(&st->lock);
			*val = 0;
			*val2 = accel_scale[st->chip_config.accl_fs];
			mutex_unlock(&st->lock);

			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = st->hw->temp.scale / 1000000;
			*val2 = st->hw->temp.scale % 1000000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_MAGN:
			return inv_mpu_magn_get_scale(st, chan, val, val2);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_TEMP:
			*val = st->hw->temp.offset;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			mutex_lock(&st->lock);
			ret = inv_mpu6050_sensor_show(st, st->reg->gyro_offset,
						chan->channel2, val);
			mutex_unlock(&st->lock);
			return IIO_VAL_INT;
		case IIO_ACCEL:
			mutex_lock(&st->lock);
			ret = inv_mpu6050_sensor_show(st, st->reg->accl_offset,
						chan->channel2, val);
			mutex_unlock(&st->lock);
			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int inv_mpu6050_write_gyro_scale(struct inv_mpu6050_state *st, int val,
					int val2)
{
	int result, i;

	if (val != 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(gyro_scale_6050); ++i) {
		if (gyro_scale_6050[i] == val2) {
			result = inv_mpu6050_set_gyro_fsr(st, i);
			if (result)
				return result;

			st->chip_config.fsr = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int inv_write_raw_get_fmt(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			return IIO_VAL_INT_PLUS_NANO;
		default:
			return IIO_VAL_INT_PLUS_MICRO;
		}
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static int inv_mpu6050_write_accel_scale(struct inv_mpu6050_state *st, int val,
					 int val2)
{
	int result, i;
	u8 d;

	if (val != 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(accel_scale); ++i) {
		if (accel_scale[i] == val2) {
			d = (i << INV_MPU6050_ACCL_CONFIG_FSR_SHIFT);
			result = regmap_write(st->map, st->reg->accl_config, d);
			if (result)
				return result;

			st->chip_config.accl_fs = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int inv_mpu6050_write_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	struct inv_mpu6050_state  *st = iio_priv(indio_dev);
	struct device *pdev = regmap_get_device(st->map);
	int result;

	/*
	 * we should only update scale when the chip is disabled, i.e.
	 * not running
	 */
	result = iio_device_claim_direct_mode(indio_dev);
	if (result)
		return result;

	mutex_lock(&st->lock);
	result = pm_runtime_resume_and_get(pdev);
	if (result)
		goto error_write_raw_unlock;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			result = inv_mpu6050_write_gyro_scale(st, val, val2);
			break;
		case IIO_ACCEL:
			result = inv_mpu6050_write_accel_scale(st, val, val2);
			break;
		default:
			result = -EINVAL;
			break;
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			result = inv_mpu6050_sensor_set(st,
							st->reg->gyro_offset,
							chan->channel2, val);
			break;
		case IIO_ACCEL:
			result = inv_mpu6050_sensor_set(st,
							st->reg->accl_offset,
							chan->channel2, val);
			break;
		default:
			result = -EINVAL;
			break;
		}
		break;
	default:
		result = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(pdev);
	pm_runtime_put_autosuspend(pdev);
error_write_raw_unlock:
	mutex_unlock(&st->lock);
	iio_device_release_direct_mode(indio_dev);

	return result;
}

/*
 *  inv_mpu6050_set_lpf() - set low pass filer based on fifo rate.
 *
 *                  Based on the Nyquist principle, the bandwidth of the low
 *                  pass filter must not exceed the signal sampling rate divided
 *                  by 2, or there would be aliasing.
 *                  This function basically search for the correct low pass
 *                  parameters based on the fifo rate, e.g, sampling frequency.
 *
 *  lpf is set automatically when setting sampling rate to avoid any aliases.
 */
static int inv_mpu6050_set_lpf(struct inv_mpu6050_state *st, int rate)
{
	static const int hz[] = {400, 200, 90, 40, 20, 10};
	static const int d[] = {
		INV_MPU6050_FILTER_200HZ, INV_MPU6050_FILTER_100HZ,
		INV_MPU6050_FILTER_45HZ, INV_MPU6050_FILTER_20HZ,
		INV_MPU6050_FILTER_10HZ, INV_MPU6050_FILTER_5HZ
	};
	int i, result;
	u8 data;

	data = INV_MPU6050_FILTER_5HZ;
	for (i = 0; i < ARRAY_SIZE(hz); ++i) {
		if (rate >= hz[i]) {
			data = d[i];
			break;
		}
	}
	result = inv_mpu6050_set_lpf_regs(st, data);
	if (result)
		return result;
	st->chip_config.lpf = data;

	return 0;
}

/*
 * inv_mpu6050_fifo_rate_store() - Set fifo rate.
 */
static ssize_t
inv_mpu6050_fifo_rate_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int fifo_rate;
	u8 d;
	int result;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	struct device *pdev = regmap_get_device(st->map);

	if (kstrtoint(buf, 10, &fifo_rate))
		return -EINVAL;
	if (fifo_rate < INV_MPU6050_MIN_FIFO_RATE ||
	    fifo_rate > INV_MPU6050_MAX_FIFO_RATE)
		return -EINVAL;

	/* compute the chip sample rate divider */
	d = INV_MPU6050_FIFO_RATE_TO_DIVIDER(fifo_rate);
	/* compute back the fifo rate to handle truncation cases */
	fifo_rate = INV_MPU6050_DIVIDER_TO_FIFO_RATE(d);

	mutex_lock(&st->lock);
	if (d == st->chip_config.divider) {
		result = 0;
		goto fifo_rate_fail_unlock;
	}
	result = pm_runtime_resume_and_get(pdev);
	if (result)
		goto fifo_rate_fail_unlock;

	result = regmap_write(st->map, st->reg->sample_rate_div, d);
	if (result)
		goto fifo_rate_fail_power_off;
	st->chip_config.divider = d;

	result = inv_mpu6050_set_lpf(st, fifo_rate);
	if (result)
		goto fifo_rate_fail_power_off;

	/* update rate for magn, noop if not present in chip */
	result = inv_mpu_magn_set_rate(st, fifo_rate);
	if (result)
		goto fifo_rate_fail_power_off;

	pm_runtime_mark_last_busy(pdev);
fifo_rate_fail_power_off:
	pm_runtime_put_autosuspend(pdev);
fifo_rate_fail_unlock:
	mutex_unlock(&st->lock);
	if (result)
		return result;

	return count;
}

/*
 * inv_fifo_rate_show() - Get the current sampling rate.
 */
static ssize_t
inv_fifo_rate_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct inv_mpu6050_state *st = iio_priv(dev_to_iio_dev(dev));
	unsigned fifo_rate;

	mutex_lock(&st->lock);
	fifo_rate = INV_MPU6050_DIVIDER_TO_FIFO_RATE(st->chip_config.divider);
	mutex_unlock(&st->lock);

	return scnprintf(buf, PAGE_SIZE, "%u\n", fifo_rate);
}

/*
 * inv_attr_show() - calling this function will show current
 *                    parameters.
 *
 * Deprecated in favor of IIO mounting matrix API.
 *
 * See inv_get_mount_matrix()
 */
static ssize_t inv_attr_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct inv_mpu6050_state *st = iio_priv(dev_to_iio_dev(dev));
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	s8 *m;

	switch (this_attr->address) {
	/*
	 * In MPU6050, the two matrix are the same because gyro and accel
	 * are integrated in one chip
	 */
	case ATTR_GYRO_MATRIX:
	case ATTR_ACCL_MATRIX:
		m = st->plat_data.orientation;

		return scnprintf(buf, PAGE_SIZE,
			"%d, %d, %d; %d, %d, %d; %d, %d, %d\n",
			m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
	default:
		return -EINVAL;
	}
}

/**
 * inv_mpu6050_validate_trigger() - validate_trigger callback for invensense
 *                                  MPU6050 device.
 * @indio_dev: The IIO device
 * @trig: The new trigger
 *
 * Returns: 0 if the 'trig' matches the trigger registered by the MPU6050
 * device, -EINVAL otherwise.
 */
static int inv_mpu6050_validate_trigger(struct iio_dev *indio_dev,
					struct iio_trigger *trig)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	if (st->trig != trig)
		return -EINVAL;

	return 0;
}

static const struct iio_mount_matrix *
inv_get_mount_matrix(const struct iio_dev *indio_dev,
		     const struct iio_chan_spec *chan)
{
	struct inv_mpu6050_state *data = iio_priv(indio_dev);
	const struct iio_mount_matrix *matrix;

	if (chan->type == IIO_MAGN)
		matrix = &data->magn_orient;
	else
		matrix = &data->orientation;

	return matrix;
}

static const struct iio_chan_spec_ext_info inv_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_TYPE, inv_get_mount_matrix),
	{ }
};

#define INV_MPU6050_CHAN(_type, _channel2, _index)                    \
	{                                                             \
		.type = _type,                                        \
		.modified = 1,                                        \
		.channel2 = _channel2,                                \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	      \
				      BIT(IIO_CHAN_INFO_CALIBBIAS),   \
		.scan_index = _index,                                 \
		.scan_type = {                                        \
				.sign = 's',                          \
				.realbits = 16,                       \
				.storagebits = 16,                    \
				.shift = 0,                           \
				.endianness = IIO_BE,                 \
			     },                                       \
		.ext_info = inv_ext_info,                             \
	}

#define INV_MPU6050_TEMP_CHAN(_index)				\
	{							\
		.type = IIO_TEMP,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)	\
				| BIT(IIO_CHAN_INFO_OFFSET)	\
				| BIT(IIO_CHAN_INFO_SCALE),	\
		.scan_index = _index,				\
		.scan_type = {					\
			.sign = 's',				\
			.realbits = 16,				\
			.storagebits = 16,			\
			.shift = 0,				\
			.endianness = IIO_BE,			\
		},						\
	}

static const struct iio_chan_spec inv_mpu_channels[] = {
	IIO_CHAN_SOFT_TIMESTAMP(INV_MPU6050_SCAN_TIMESTAMP),

	INV_MPU6050_TEMP_CHAN(INV_MPU6050_SCAN_TEMP),

	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_MPU6050_SCAN_GYRO_X),
	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_MPU6050_SCAN_GYRO_Y),
	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_MPU6050_SCAN_GYRO_Z),

	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_X, INV_MPU6050_SCAN_ACCL_X),
	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_MPU6050_SCAN_ACCL_Y),
	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_MPU6050_SCAN_ACCL_Z),
};

#define INV_MPU6050_SCAN_MASK_3AXIS_ACCEL	\
	(BIT(INV_MPU6050_SCAN_ACCL_X)		\
	| BIT(INV_MPU6050_SCAN_ACCL_Y)		\
	| BIT(INV_MPU6050_SCAN_ACCL_Z))

#define INV_MPU6050_SCAN_MASK_3AXIS_GYRO	\
	(BIT(INV_MPU6050_SCAN_GYRO_X)		\
	| BIT(INV_MPU6050_SCAN_GYRO_Y)		\
	| BIT(INV_MPU6050_SCAN_GYRO_Z))

#define INV_MPU6050_SCAN_MASK_TEMP		(BIT(INV_MPU6050_SCAN_TEMP))

static const unsigned long inv_mpu_scan_masks[] = {
	/* 3-axis accel */
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL,
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU6050_SCAN_MASK_TEMP,
	/* 3-axis gyro */
	INV_MPU6050_SCAN_MASK_3AXIS_GYRO,
	INV_MPU6050_SCAN_MASK_3AXIS_GYRO | INV_MPU6050_SCAN_MASK_TEMP,
	/* 6-axis accel + gyro */
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU6050_SCAN_MASK_3AXIS_GYRO,
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU6050_SCAN_MASK_3AXIS_GYRO
		| INV_MPU6050_SCAN_MASK_TEMP,
	0,
};

#define INV_MPU9X50_MAGN_CHAN(_chan2, _bits, _index)			\
	{								\
		.type = IIO_MAGN,					\
		.modified = 1,						\
		.channel2 = _chan2,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_SCALE) |	\
				      BIT(IIO_CHAN_INFO_RAW),		\
		.scan_index = _index,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = _bits,				\
			.storagebits = 16,				\
			.shift = 0,					\
			.endianness = IIO_BE,				\
		},							\
		.ext_info = inv_ext_info,				\
	}

static const struct iio_chan_spec inv_mpu9150_channels[] = {
	IIO_CHAN_SOFT_TIMESTAMP(INV_MPU9X50_SCAN_TIMESTAMP),

	INV_MPU6050_TEMP_CHAN(INV_MPU6050_SCAN_TEMP),

	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_MPU6050_SCAN_GYRO_X),
	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_MPU6050_SCAN_GYRO_Y),
	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_MPU6050_SCAN_GYRO_Z),

	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_X, INV_MPU6050_SCAN_ACCL_X),
	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_MPU6050_SCAN_ACCL_Y),
	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_MPU6050_SCAN_ACCL_Z),

	/* Magnetometer resolution is 13 bits */
	INV_MPU9X50_MAGN_CHAN(IIO_MOD_X, 13, INV_MPU9X50_SCAN_MAGN_X),
	INV_MPU9X50_MAGN_CHAN(IIO_MOD_Y, 13, INV_MPU9X50_SCAN_MAGN_Y),
	INV_MPU9X50_MAGN_CHAN(IIO_MOD_Z, 13, INV_MPU9X50_SCAN_MAGN_Z),
};

static const struct iio_chan_spec inv_mpu9250_channels[] = {
	IIO_CHAN_SOFT_TIMESTAMP(INV_MPU9X50_SCAN_TIMESTAMP),

	INV_MPU6050_TEMP_CHAN(INV_MPU6050_SCAN_TEMP),

	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_MPU6050_SCAN_GYRO_X),
	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_MPU6050_SCAN_GYRO_Y),
	INV_MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_MPU6050_SCAN_GYRO_Z),

	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_X, INV_MPU6050_SCAN_ACCL_X),
	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_MPU6050_SCAN_ACCL_Y),
	INV_MPU6050_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_MPU6050_SCAN_ACCL_Z),

	/* Magnetometer resolution is 16 bits */
	INV_MPU9X50_MAGN_CHAN(IIO_MOD_X, 16, INV_MPU9X50_SCAN_MAGN_X),
	INV_MPU9X50_MAGN_CHAN(IIO_MOD_Y, 16, INV_MPU9X50_SCAN_MAGN_Y),
	INV_MPU9X50_MAGN_CHAN(IIO_MOD_Z, 16, INV_MPU9X50_SCAN_MAGN_Z),
};

#define INV_MPU9X50_SCAN_MASK_3AXIS_MAGN	\
	(BIT(INV_MPU9X50_SCAN_MAGN_X)		\
	| BIT(INV_MPU9X50_SCAN_MAGN_Y)		\
	| BIT(INV_MPU9X50_SCAN_MAGN_Z))

static const unsigned long inv_mpu9x50_scan_masks[] = {
	/* 3-axis accel */
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL,
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU6050_SCAN_MASK_TEMP,
	/* 3-axis gyro */
	INV_MPU6050_SCAN_MASK_3AXIS_GYRO,
	INV_MPU6050_SCAN_MASK_3AXIS_GYRO | INV_MPU6050_SCAN_MASK_TEMP,
	/* 3-axis magn */
	INV_MPU9X50_SCAN_MASK_3AXIS_MAGN,
	INV_MPU9X50_SCAN_MASK_3AXIS_MAGN | INV_MPU6050_SCAN_MASK_TEMP,
	/* 6-axis accel + gyro */
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU6050_SCAN_MASK_3AXIS_GYRO,
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU6050_SCAN_MASK_3AXIS_GYRO
		| INV_MPU6050_SCAN_MASK_TEMP,
	/* 6-axis accel + magn */
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU9X50_SCAN_MASK_3AXIS_MAGN,
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU9X50_SCAN_MASK_3AXIS_MAGN
		| INV_MPU6050_SCAN_MASK_TEMP,
	/* 6-axis gyro + magn */
	INV_MPU6050_SCAN_MASK_3AXIS_GYRO | INV_MPU9X50_SCAN_MASK_3AXIS_MAGN,
	INV_MPU6050_SCAN_MASK_3AXIS_GYRO | INV_MPU9X50_SCAN_MASK_3AXIS_MAGN
		| INV_MPU6050_SCAN_MASK_TEMP,
	/* 9-axis accel + gyro + magn */
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU6050_SCAN_MASK_3AXIS_GYRO
		| INV_MPU9X50_SCAN_MASK_3AXIS_MAGN,
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU6050_SCAN_MASK_3AXIS_GYRO
		| INV_MPU9X50_SCAN_MASK_3AXIS_MAGN
		| INV_MPU6050_SCAN_MASK_TEMP,
	0,
};

static const unsigned long inv_icm20602_scan_masks[] = {
	/* 3-axis accel + temp (mandatory) */
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU6050_SCAN_MASK_TEMP,
	/* 3-axis gyro + temp (mandatory) */
	INV_MPU6050_SCAN_MASK_3AXIS_GYRO | INV_MPU6050_SCAN_MASK_TEMP,
	/* 6-axis accel + gyro + temp (mandatory) */
	INV_MPU6050_SCAN_MASK_3AXIS_ACCEL | INV_MPU6050_SCAN_MASK_3AXIS_GYRO
		| INV_MPU6050_SCAN_MASK_TEMP,
	0,
};

/*
 * The user can choose any frequency between INV_MPU6050_MIN_FIFO_RATE and
 * INV_MPU6050_MAX_FIFO_RATE, but only these frequencies are matched by the
 * low-pass filter. Specifically, each of these sampling rates are about twice
 * the bandwidth of a corresponding low-pass filter, which should eliminate
 * aliasing following the Nyquist principle. By picking a frequency different
 * from these, the user risks aliasing effects.
 */
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("10 20 50 100 200 500");
static IIO_CONST_ATTR(in_anglvel_scale_available,
					  "0.000133090 0.000266181 0.000532362 0.001064724");
static IIO_CONST_ATTR(in_accel_scale_available,
					  "0.000598 0.001196 0.002392 0.004785");
static IIO_DEV_ATTR_SAMP_FREQ(S_IRUGO | S_IWUSR, inv_fifo_rate_show,
	inv_mpu6050_fifo_rate_store);

/* Deprecated: kept for userspace backward compatibility. */
static IIO_DEVICE_ATTR(in_gyro_matrix, S_IRUGO, inv_attr_show, NULL,
	ATTR_GYRO_MATRIX);
static IIO_DEVICE_ATTR(in_accel_matrix, S_IRUGO, inv_attr_show, NULL,
	ATTR_ACCL_MATRIX);

static struct attribute *inv_attributes[] = {
	&iio_dev_attr_in_gyro_matrix.dev_attr.attr,  /* deprecated */
	&iio_dev_attr_in_accel_matrix.dev_attr.attr, /* deprecated */
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	&iio_const_attr_in_anglvel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group inv_attribute_group = {
	.attrs = inv_attributes
};

static int inv_mpu6050_reg_access(struct iio_dev *indio_dev,
				  unsigned int reg,
				  unsigned int writeval,
				  unsigned int *readval)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	if (readval)
		ret = regmap_read(st->map, reg, readval);
	else
		ret = regmap_write(st->map, reg, writeval);
	mutex_unlock(&st->lock);

	return ret;
}

static const struct iio_info mpu_info = {
	.read_raw = &inv_mpu6050_read_raw,
	.write_raw = &inv_mpu6050_write_raw,
	.write_raw_get_fmt = &inv_write_raw_get_fmt,
	.attrs = &inv_attribute_group,
	.validate_trigger = inv_mpu6050_validate_trigger,
	.debugfs_reg_access = &inv_mpu6050_reg_access,
};

/*
 *  inv_check_and_setup_chip() - check and setup chip.
 */
static int inv_check_and_setup_chip(struct inv_mpu6050_state *st)
{
	int result;
	unsigned int regval, mask;
	int i;

	st->hw  = &hw_info[st->chip_type];
	st->reg = hw_info[st->chip_type].reg;
	memcpy(&st->chip_config, hw_info[st->chip_type].config,
	       sizeof(st->chip_config));

	/* check chip self-identification */
	result = regmap_read(st->map, INV_MPU6050_REG_WHOAMI, &regval);
	if (result)
		return result;
	if (regval != st->hw->whoami) {
		/* check whoami against all possible values */
		for (i = 0; i < INV_NUM_PARTS; ++i) {
			if (regval == hw_info[i].whoami) {
				dev_warn(regmap_get_device(st->map),
					"whoami mismatch got 0x%02x (%s) expected 0x%02x (%s)\n",
					regval, hw_info[i].name,
					st->hw->whoami, st->hw->name);
				break;
			}
		}
		if (i >= INV_NUM_PARTS) {
			dev_err(regmap_get_device(st->map),
				"invalid whoami 0x%02x expected 0x%02x (%s)\n",
				regval, st->hw->whoami, st->hw->name);
			return -ENODEV;
		}
	}

	/* reset to make sure previous state are not there */
	result = regmap_write(st->map, st->reg->pwr_mgmt_1,
			      INV_MPU6050_BIT_H_RESET);
	if (result)
		return result;
	msleep(INV_MPU6050_POWER_UP_TIME);
	switch (st->chip_type) {
	case INV_MPU6000:
	case INV_MPU6500:
	case INV_MPU6515:
	case INV_MPU6880:
	case INV_MPU9250:
	case INV_MPU9255:
		/* reset signal path (required for spi connection) */
		regval = INV_MPU6050_BIT_TEMP_RST | INV_MPU6050_BIT_ACCEL_RST |
			 INV_MPU6050_BIT_GYRO_RST;
		result = regmap_write(st->map, INV_MPU6050_REG_SIGNAL_PATH_RESET,
				      regval);
		if (result)
			return result;
		msleep(INV_MPU6050_POWER_UP_TIME);
		break;
	default:
		break;
	}

	/*
	 * Turn power on. After reset, the sleep bit could be on
	 * or off depending on the OTP settings. Turning power on
	 * make it in a definite state as well as making the hardware
	 * state align with the software state
	 */
	result = inv_mpu6050_set_power_itg(st, true);
	if (result)
		return result;
	mask = INV_MPU6050_SENSOR_ACCL | INV_MPU6050_SENSOR_GYRO |
			INV_MPU6050_SENSOR_TEMP | INV_MPU6050_SENSOR_MAGN;
	result = inv_mpu6050_switch_engine(st, false, mask);
	if (result)
		goto error_power_off;

	return 0;

error_power_off:
	inv_mpu6050_set_power_itg(st, false);
	return result;
}

static int inv_mpu_core_enable_regulator_vddio(struct inv_mpu6050_state *st)
{
	int result;

	result = regulator_enable(st->vddio_supply);
	if (result) {
		dev_err(regmap_get_device(st->map),
			"Failed to enable vddio regulator: %d\n", result);
	} else {
		/* Give the device a little bit of time to start up. */
		usleep_range(3000, 5000);
	}

	return result;
}

static int inv_mpu_core_disable_regulator_vddio(struct inv_mpu6050_state *st)
{
	int result;

	result = regulator_disable(st->vddio_supply);
	if (result)
		dev_err(regmap_get_device(st->map),
			"Failed to disable vddio regulator: %d\n", result);

	return result;
}

static void inv_mpu_core_disable_regulator_action(void *_data)
{
	struct inv_mpu6050_state *st = _data;
	int result;

	result = regulator_disable(st->vdd_supply);
	if (result)
		dev_err(regmap_get_device(st->map),
			"Failed to disable vdd regulator: %d\n", result);

	inv_mpu_core_disable_regulator_vddio(st);
}

static void inv_mpu_pm_disable(void *data)
{
	struct device *dev = data;

	pm_runtime_disable(dev);
}

int inv_mpu_core_probe(struct regmap *regmap, int irq, const char *name,
		int (*inv_mpu_bus_setup)(struct iio_dev *), int chip_type)
{
	struct inv_mpu6050_state *st;
	struct iio_dev *indio_dev;
	struct inv_mpu6050_platform_data *pdata;
	struct device *dev = regmap_get_device(regmap);
	int result;
	struct irq_data *desc;
	int irq_type;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	BUILD_BUG_ON(ARRAY_SIZE(hw_info) != INV_NUM_PARTS);
	if (chip_type < 0 || chip_type >= INV_NUM_PARTS) {
		dev_err(dev, "Bad invensense chip_type=%d name=%s\n",
				chip_type, name);
		return -ENODEV;
	}
	st = iio_priv(indio_dev);
	mutex_init(&st->lock);
	st->chip_type = chip_type;
	st->irq = irq;
	st->map = regmap;

	pdata = dev_get_platdata(dev);
	if (!pdata) {
		result = iio_read_mount_matrix(dev, &st->orientation);
		if (result) {
			dev_err(dev, "Failed to retrieve mounting matrix %d\n",
				result);
			return result;
		}
	} else {
		st->plat_data = *pdata;
	}

	if (irq > 0) {
		desc = irq_get_irq_data(irq);
		if (!desc) {
			dev_err(dev, "Could not find IRQ %d\n", irq);
			return -EINVAL;
		}

		irq_type = irqd_get_trigger_type(desc);
		if (!irq_type)
			irq_type = IRQF_TRIGGER_RISING;
	} else {
		/* Doesn't really matter, use the default */
		irq_type = IRQF_TRIGGER_RISING;
	}

	if (irq_type & IRQF_TRIGGER_RISING)	// rising or both-edge
		st->irq_mask = INV_MPU6050_ACTIVE_HIGH;
	else if (irq_type == IRQF_TRIGGER_FALLING)
		st->irq_mask = INV_MPU6050_ACTIVE_LOW;
	else if (irq_type == IRQF_TRIGGER_HIGH)
		st->irq_mask = INV_MPU6050_ACTIVE_HIGH |
			INV_MPU6050_LATCH_INT_EN;
	else if (irq_type == IRQF_TRIGGER_LOW)
		st->irq_mask = INV_MPU6050_ACTIVE_LOW |
			INV_MPU6050_LATCH_INT_EN;
	else {
		dev_err(dev, "Invalid interrupt type 0x%x specified\n",
			irq_type);
		return -EINVAL;
	}

	st->vdd_supply = devm_regulator_get(dev, "vdd");
	if (IS_ERR(st->vdd_supply))
		return dev_err_probe(dev, PTR_ERR(st->vdd_supply),
				     "Failed to get vdd regulator\n");

	st->vddio_supply = devm_regulator_get(dev, "vddio");
	if (IS_ERR(st->vddio_supply))
		return dev_err_probe(dev, PTR_ERR(st->vddio_supply),
				     "Failed to get vddio regulator\n");

	result = regulator_enable(st->vdd_supply);
	if (result) {
		dev_err(dev, "Failed to enable vdd regulator: %d\n", result);
		return result;
	}
	msleep(INV_MPU6050_POWER_UP_TIME);

	result = inv_mpu_core_enable_regulator_vddio(st);
	if (result) {
		regulator_disable(st->vdd_supply);
		return result;
	}

	result = devm_add_action_or_reset(dev, inv_mpu_core_disable_regulator_action,
				 st);
	if (result) {
		dev_err(dev, "Failed to setup regulator cleanup action %d\n",
			result);
		return result;
	}

	/* fill magnetometer orientation */
	result = inv_mpu_magn_set_orient(st);
	if (result)
		return result;

	/* power is turned on inside check chip type*/
	result = inv_check_and_setup_chip(st);
	if (result)
		return result;

	result = inv_mpu6050_init_config(indio_dev);
	if (result) {
		dev_err(dev, "Could not initialize device.\n");
		goto error_power_off;
	}

	dev_set_drvdata(dev, indio_dev);
	/* name will be NULL when enumerated via ACPI */
	if (name)
		indio_dev->name = name;
	else
		indio_dev->name = dev_name(dev);

	/* requires parent device set in indio_dev */
	if (inv_mpu_bus_setup) {
		result = inv_mpu_bus_setup(indio_dev);
		if (result)
			goto error_power_off;
	}

	/* chip init is done, turning on runtime power management */
	result = pm_runtime_set_active(dev);
	if (result)
		goto error_power_off;
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, INV_MPU6050_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_put(dev);
	result = devm_add_action_or_reset(dev, inv_mpu_pm_disable, dev);
	if (result)
		return result;

	switch (chip_type) {
	case INV_MPU9150:
		indio_dev->channels = inv_mpu9150_channels;
		indio_dev->num_channels = ARRAY_SIZE(inv_mpu9150_channels);
		indio_dev->available_scan_masks = inv_mpu9x50_scan_masks;
		break;
	case INV_MPU9250:
	case INV_MPU9255:
		indio_dev->channels = inv_mpu9250_channels;
		indio_dev->num_channels = ARRAY_SIZE(inv_mpu9250_channels);
		indio_dev->available_scan_masks = inv_mpu9x50_scan_masks;
		break;
	case INV_ICM20600:
	case INV_ICM20602:
		indio_dev->channels = inv_mpu_channels;
		indio_dev->num_channels = ARRAY_SIZE(inv_mpu_channels);
		indio_dev->available_scan_masks = inv_icm20602_scan_masks;
		break;
	default:
		indio_dev->channels = inv_mpu_channels;
		indio_dev->num_channels = ARRAY_SIZE(inv_mpu_channels);
		indio_dev->available_scan_masks = inv_mpu_scan_masks;
		break;
	}
	/*
	 * Use magnetometer inside the chip only if there is no i2c
	 * auxiliary device in use. Otherwise Going back to 6-axis only.
	 */
	if (st->magn_disabled) {
		indio_dev->channels = inv_mpu_channels;
		indio_dev->num_channels = ARRAY_SIZE(inv_mpu_channels);
		indio_dev->available_scan_masks = inv_mpu_scan_masks;
	}

	indio_dev->info = &mpu_info;

	if (irq > 0) {
		/*
		 * The driver currently only supports buffered capture with its
		 * own trigger. So no IRQ, no trigger, no buffer
		 */
		result = devm_iio_triggered_buffer_setup(dev, indio_dev,
							 iio_pollfunc_store_time,
							 inv_mpu6050_read_fifo,
							 NULL);
		if (result) {
			dev_err(dev, "configure buffer fail %d\n", result);
			return result;
		}

		result = inv_mpu6050_probe_trigger(indio_dev, irq_type);
		if (result) {
			dev_err(dev, "trigger probe fail %d\n", result);
			return result;
		}
	}

	result = devm_iio_device_register(dev, indio_dev);
	if (result) {
		dev_err(dev, "IIO register fail %d\n", result);
		return result;
	}

	return 0;

error_power_off:
	inv_mpu6050_set_power_itg(st, false);
	return result;
}
EXPORT_SYMBOL_NS_GPL(inv_mpu_core_probe, IIO_MPU6050);

static int inv_mpu_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	int result;

	mutex_lock(&st->lock);
	result = inv_mpu_core_enable_regulator_vddio(st);
	if (result)
		goto out_unlock;

	result = inv_mpu6050_set_power_itg(st, true);
	if (result)
		goto out_unlock;

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	result = inv_mpu6050_switch_engine(st, true, st->suspended_sensors);
	if (result)
		goto out_unlock;

	if (iio_buffer_enabled(indio_dev))
		result = inv_mpu6050_prepare_fifo(st, true);

out_unlock:
	mutex_unlock(&st->lock);

	return result;
}

static int inv_mpu_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	int result;

	mutex_lock(&st->lock);

	st->suspended_sensors = 0;
	if (pm_runtime_suspended(dev)) {
		result = 0;
		goto out_unlock;
	}

	if (iio_buffer_enabled(indio_dev)) {
		result = inv_mpu6050_prepare_fifo(st, false);
		if (result)
			goto out_unlock;
	}

	if (st->chip_config.accl_en)
		st->suspended_sensors |= INV_MPU6050_SENSOR_ACCL;
	if (st->chip_config.gyro_en)
		st->suspended_sensors |= INV_MPU6050_SENSOR_GYRO;
	if (st->chip_config.temp_en)
		st->suspended_sensors |= INV_MPU6050_SENSOR_TEMP;
	if (st->chip_config.magn_en)
		st->suspended_sensors |= INV_MPU6050_SENSOR_MAGN;
	result = inv_mpu6050_switch_engine(st, false, st->suspended_sensors);
	if (result)
		goto out_unlock;

	result = inv_mpu6050_set_power_itg(st, false);
	if (result)
		goto out_unlock;

	inv_mpu_core_disable_regulator_vddio(st);
out_unlock:
	mutex_unlock(&st->lock);

	return result;
}

static int inv_mpu_runtime_suspend(struct device *dev)
{
	struct inv_mpu6050_state *st = iio_priv(dev_get_drvdata(dev));
	unsigned int sensors;
	int ret;

	mutex_lock(&st->lock);

	sensors = INV_MPU6050_SENSOR_ACCL | INV_MPU6050_SENSOR_GYRO |
			INV_MPU6050_SENSOR_TEMP | INV_MPU6050_SENSOR_MAGN;
	ret = inv_mpu6050_switch_engine(st, false, sensors);
	if (ret)
		goto out_unlock;

	ret = inv_mpu6050_set_power_itg(st, false);
	if (ret)
		goto out_unlock;

	inv_mpu_core_disable_regulator_vddio(st);

out_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int inv_mpu_runtime_resume(struct device *dev)
{
	struct inv_mpu6050_state *st = iio_priv(dev_get_drvdata(dev));
	int ret;

	ret = inv_mpu_core_enable_regulator_vddio(st);
	if (ret)
		return ret;

	return inv_mpu6050_set_power_itg(st, true);
}

EXPORT_NS_GPL_DEV_PM_OPS(inv_mpu_pmops, IIO_MPU6050) = {
	SYSTEM_SLEEP_PM_OPS(inv_mpu_suspend, inv_mpu_resume)
	RUNTIME_PM_OPS(inv_mpu_runtime_suspend, inv_mpu_runtime_resume, NULL)
};

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Invensense device MPU6050 driver");
MODULE_LICENSE("GPL");
