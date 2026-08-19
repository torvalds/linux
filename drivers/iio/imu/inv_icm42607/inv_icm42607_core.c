// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 InvenSense, Inc.
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include <asm/byteorder.h>

#include "inv_icm42607.h"

static bool inv_icm42607_is_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case INV_ICM42607_REG_MCLK_RDY ... INV_ICM42607_REG_INT_CONFIG:
	case INV_ICM42607_REG_TEMP_DATA1 ... INV_ICM42607_REG_TMST_FSYNCL:
	case INV_ICM42607_REG_APEX_DATA4 ... INV_ICM42607_REG_INTF_CONFIG1:
	case INV_ICM42607_REG_INT_STATUS_DRDY ... INV_ICM42607_REG_FIFO_DATA:
	case INV_ICM42607_REG_WHOAMI:
		return true;
	}

	return false;
}

static bool inv_icm42607_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case INV_ICM42607_REG_DEVICE_CONFIG ... INV_ICM42607_REG_INT_CONFIG:
	case INV_ICM42607_REG_PWR_MGMT0 ... INV_ICM42607_REG_INT_SOURCE4:
	case INV_ICM42607_REG_INTF_CONFIG0 ... INV_ICM42607_REG_INTF_CONFIG1:
		return true;
	}

	return false;
}

static bool inv_icm42607_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case INV_ICM42607_REG_MCLK_RDY:
	case INV_ICM42607_REG_SIGNAL_PATH_RESET:
	case INV_ICM42607_REG_TEMP_DATA1 ... INV_ICM42607_REG_APEX_DATA5:
	case INV_ICM42607_REG_APEX_CONFIG0:
	case INV_ICM42607_REG_FIFO_LOST_PKT0 ... INV_ICM42607_REG_APEX_DATA3:
	case INV_ICM42607_REG_INT_STATUS_DRDY:
	case INV_ICM42607_REG_INT_STATUS ... INV_ICM42607_REG_FIFO_DATA:
		return true;
	}

	return false;
}

const struct regmap_config inv_icm42607_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = inv_icm42607_is_writeable_reg,
	.readable_reg = inv_icm42607_is_readable_reg,
	.volatile_reg = inv_icm42607_is_volatile_reg,
	.max_register = INV_ICM42607_REG_WHOAMI,
	.cache_type = REGCACHE_MAPLE,
};
EXPORT_SYMBOL_NS_GPL(inv_icm42607_regmap_config, "IIO_ICM42607");

/* chip initial default configuration */
static const struct inv_icm42607_conf inv_icm42607_default_conf = {
	.gyro = {
		.mode = INV_ICM42607_SENSOR_MODE_OFF,
		.fs = INV_ICM42607_GYRO_FS_1000DPS,
		.odr = INV_ICM42607_ODR_100HZ,
		.filter = INV_ICM42607_FILTER_BW_25HZ,
	},
	.accel = {
		.mode = INV_ICM42607_SENSOR_MODE_OFF,
		.fs = INV_ICM42607_ACCEL_FS_4G,
		.odr = INV_ICM42607_ODR_100HZ,
		.filter = INV_ICM42607_FILTER_BW_25HZ,
	},
};

const struct inv_icm42607_hw inv_icm42607_hw_data = {
	.whoami = INV_ICM42607_WHOAMI,
	.name = "icm42607",
	.conf = &inv_icm42607_default_conf,
};
EXPORT_SYMBOL_NS_GPL(inv_icm42607_hw_data, "IIO_ICM42607");

const struct inv_icm42607_hw inv_icm42607p_hw_data = {
	.whoami = INV_ICM42607P_WHOAMI,
	.name = "icm42607p",
	.conf = &inv_icm42607_default_conf,
};
EXPORT_SYMBOL_NS_GPL(inv_icm42607p_hw_data, "IIO_ICM42607");

const struct iio_mount_matrix *
inv_icm42607_get_mount_matrix(struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan)
{
	const struct inv_icm42607_state *st = iio_device_get_drvdata(indio_dev);

	return &st->orientation;
}

static u32 inv_icm42607_odr_to_period_us(enum inv_icm42607_odr odr)
{
	static const u32 odr_periods[INV_ICM42607_ODR_NB] = {
		/* Reserved values */
		0, 0, 0, 0, 0,
		/* 1600Hz */
		625,
		/* 800Hz */
		1250,
		/* 400Hz */
		2500,
		/* 200Hz */
		5000,
		/* 100 Hz */
		10000,
		/* 50Hz */
		20000,
		/* 25Hz */
		40000,
		/* 12.5Hz */
		80000,
		/* 6.25Hz */
		160000,
		/* 3.125Hz */
		320000,
		/* 1.5625Hz */
		640000,
	};

	odr = clamp(odr, INV_ICM42607_ODR_1600HZ, INV_ICM42607_ODR_1_5625HZ_LP);

	return odr_periods[odr];
}

int inv_icm42607_get_pwr_mgmt0(struct inv_icm42607_state *st,
			       enum inv_icm42607_sensor_mode *gyro,
			       enum inv_icm42607_sensor_mode *accel)
{
	unsigned int val;
	int ret;

	ret = regmap_read(st->map, INV_ICM42607_REG_PWR_MGMT0, &val);
	if (ret)
		return ret;

	*gyro = FIELD_GET(INV_ICM42607_PWR_MGMT0_GYRO_MODE_MASK, val);
	*accel = FIELD_GET(INV_ICM42607_PWR_MGMT0_ACCEL_MODE_MASK, val);

	return 0;
}

static int inv_icm42607_set_pwr_mgmt0(struct inv_icm42607_state *st,
				      enum inv_icm42607_sensor_mode gyro,
				      enum inv_icm42607_sensor_mode accel)
{
	enum inv_icm42607_sensor_mode oldaccel, oldgyro;
	unsigned int sleepval_us, odr_us;
	unsigned int val;
	s64 disable_wait;
	int ret;

	ret = inv_icm42607_get_pwr_mgmt0(st, &oldgyro, &oldaccel);
	if (ret)
		return ret;

	if (gyro == oldgyro && accel == oldaccel)
		return 0;

	/*
	 * Datasheet on page 14.26 says we need to ensure the gyro sensor is on
	 * for a minimum of 45ms. So if we transition from an on state to an
	 * off state make sure at least 45ms have passed before power off and
	 * wait if it hasn't. In case some platforms don't respond well to a
	 * sleep of 0, make sure the fsleep duration is > 0.
	 */
	if (!gyro && oldgyro) {
		disable_wait = clamp(ktime_us_delta(st->conf.gyro_stop, ktime_get()),
				     0, INV_ICM42607_GYRO_STOP_TIME_US);

		if (disable_wait > 0)
			fsleep(disable_wait);
	}

	val = FIELD_PREP(INV_ICM42607_PWR_MGMT0_GYRO_MODE_MASK, gyro) |
	      FIELD_PREP(INV_ICM42607_PWR_MGMT0_ACCEL_MODE_MASK, accel);
	ret = regmap_write(st->map, INV_ICM42607_REG_PWR_MGMT0, val);
	if (ret)
		return ret;

	/*
	 * If a state change occurs from off to on, sleep for the startup time
	 * of the sensor. Since more than one sensor can be transitioned from
	 * off to on, select the maximum time from each of the sensors changing
	 * from off to on. Also account for the time it takes for the first
	 * sample to be ready by looking up the odr value and adding it to the
	 * sleep time. The startup time for the temp sensor is considerably
	 * smaller than the startup time for the other sensors and one or more
	 * are required to be on for the temp sensor to function, so any start
	 * delay should be enough.
	 */
	sleepval_us = 0;
	odr_us = 0;
	if (accel && !oldaccel) {
		sleepval_us = max(sleepval_us, INV_ICM42607_ACCEL_STARTUP_TIME_US);
		odr_us = max(odr_us, inv_icm42607_odr_to_period_us(st->conf.accel.odr));
	}

	if (gyro && !oldgyro) {
		sleepval_us = max(sleepval_us, INV_ICM42607_GYRO_STARTUP_TIME_US);
		odr_us = max(odr_us, inv_icm42607_odr_to_period_us(st->conf.gyro.odr));
		/* Track the earliest we can turn off the gyroscope. */
		st->conf.gyro_stop = ktime_add_us(ktime_get(),
						  INV_ICM42607_GYRO_STOP_TIME_US);
	}

	sleepval_us += odr_us;

	/*
	 * Only sleep if sleepval_us is greater than 0 in case some platforms
	 * have issues with a 0 delay. The 0 delay can happen if one or both
	 * sensors is shut down.
	 */
	if (sleepval_us > 0)
		fsleep(sleepval_us);

	return 0;
}

/*
 * Sanity test between old and new config values, and note if we need
 * to update register config0 or config1.
 */
static void inv_icm42607_update_config(struct inv_icm42607_sensor_conf *conf,
				       struct inv_icm42607_sensor_conf *oldconf,
				       bool *config0, bool *config1)
{
	if (conf->mode < 0)
		conf->mode = oldconf->mode;
	if (conf->fs < 0)
		conf->fs = oldconf->fs;
	if (conf->odr < 0)
		conf->odr = oldconf->odr;
	if (conf->filter < 0)
		conf->filter = oldconf->filter;

	*config0 = (conf->fs != oldconf->fs) || (conf->odr != oldconf->odr);

	*config1 = (conf->filter != oldconf->filter);
}

int inv_icm42607_set_sensor_conf(struct inv_icm42607_state *st,
				 struct inv_icm42607_sensor_conf *conf,
				 enum iio_chan_type chan_type)
{
	enum inv_icm42607_sensor_mode accel_mode, gyro_mode;
	struct inv_icm42607_sensor_conf *oldconf;
	bool config0, config1;
	unsigned int val;
	int ret;

	switch (chan_type) {
	case IIO_ACCEL:
		oldconf = &st->conf.accel;
		break;
	case IIO_ANGL_VEL:
		oldconf = &st->conf.gyro;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Since we are only making one-shot calls today, we can avoid needless
	 * on/off cycles if we rely on the runtime power management to handle
	 * shutting off the sensors when not in use. The downside is if we
	 * enable both sensors then only read from one, we have to wait for
	 * runtime PM to turn it off. So just keep the mode of the sensor we
	 * are not actively using to whatever it's already set as.
	 */
	ret = inv_icm42607_get_pwr_mgmt0(st, &gyro_mode, &accel_mode);
	if (ret)
		return ret;

	inv_icm42607_update_config(conf, oldconf, &config0, &config1);

	if (config0) {
		if (chan_type == IIO_ANGL_VEL) {
			val = FIELD_PREP(INV_ICM42607_GYRO_CONFIG0_FS_SEL_MASK, conf->fs);
			val |= FIELD_PREP(INV_ICM42607_GYRO_CONFIG0_ODR_MASK, conf->odr);
			ret = regmap_write(st->map, INV_ICM42607_REG_GYRO_CONFIG0, val);
		} else {
			val = FIELD_PREP(INV_ICM42607_ACCEL_CONFIG0_FS_SEL_MASK, conf->fs);
			val |= FIELD_PREP(INV_ICM42607_ACCEL_CONFIG0_ODR_MASK, conf->odr);
			ret = regmap_write(st->map, INV_ICM42607_REG_ACCEL_CONFIG0, val);
		}
		if (ret)
			return ret;

		oldconf->fs = conf->fs;
		oldconf->odr = conf->odr;
	}

	if (config1) {
		if (chan_type == IIO_ANGL_VEL) {
			val = FIELD_PREP(INV_ICM42607_GYRO_CONFIG1_FILTER_MASK,
					 conf->filter);
			ret = regmap_update_bits(st->map, INV_ICM42607_REG_GYRO_CONFIG1,
						 INV_ICM42607_GYRO_CONFIG1_FILTER_MASK, val);
		} else {
			val = FIELD_PREP(INV_ICM42607_ACCEL_CONFIG1_FILTER_MASK,
					 conf->filter);
			ret = regmap_update_bits(st->map, INV_ICM42607_REG_ACCEL_CONFIG1,
						 INV_ICM42607_ACCEL_CONFIG1_FILTER_MASK, val);
		}
		if (ret)
			return ret;

		oldconf->filter = conf->filter;
	}

	oldconf->mode = conf->mode;

	switch (chan_type) {
	case IIO_ACCEL:
		return inv_icm42607_set_pwr_mgmt0(st, gyro_mode, conf->mode);
	case IIO_ANGL_VEL:
		return inv_icm42607_set_pwr_mgmt0(st, conf->mode, accel_mode);
	default:
		return -EINVAL;
	}
}

int inv_icm42607_read_sensor(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     s16 *val)
{
	struct inv_icm42607_sensor_conf conf = INV_ICM42607_SENSOR_CONF_INIT;
	struct inv_icm42607_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm42607_sensor_state *sensor_st = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(st->map);
	unsigned int reg;
	__be16 data;
	int ret;

	if ((chan->type != IIO_ANGL_VEL) && (chan->type != IIO_ACCEL))
		return -EINVAL;

	switch (chan->channel2) {
	case IIO_MOD_X:
		if (chan->type == IIO_ANGL_VEL)
			reg = INV_ICM42607_REG_GYRO_DATA_X1;
		else
			reg = INV_ICM42607_REG_ACCEL_DATA_X1;
		break;
	case IIO_MOD_Y:
		if (chan->type == IIO_ANGL_VEL)
			reg = INV_ICM42607_REG_GYRO_DATA_Y1;
		else
			reg = INV_ICM42607_REG_ACCEL_DATA_Y1;
		break;
	case IIO_MOD_Z:
		if (chan->type == IIO_ANGL_VEL)
			reg = INV_ICM42607_REG_GYRO_DATA_Z1;
		else
			reg = INV_ICM42607_REG_ACCEL_DATA_Z1;
		break;
	default:
		return -EINVAL;
	}

	PM_RUNTIME_ACQUIRE_AUTOSUSPEND(dev, pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);

	/* enable sensor */
	conf.mode = sensor_st->power_mode;
	conf.filter = sensor_st->filter;
	ret = inv_icm42607_set_sensor_conf(st, &conf, chan->type);
	if (ret)
		return ret;

	/* read sensor register data */
	ret = regmap_bulk_read(st->map, reg, &data, sizeof(data));
	if (ret)
		return ret;

	*val = be16_to_cpu(data);
	if (*val == INV_ICM42607_DATA_INVALID)
		return -EINVAL;

	return 0;
}

static int inv_icm42607_set_init_conf(struct inv_icm42607_state *st,
				      const struct inv_icm42607_conf *conf)
{
	unsigned int val;
	int ret;

	val = FIELD_PREP(INV_ICM42607_PWR_MGMT0_GYRO_MODE_MASK, conf->gyro.mode);
	val |= FIELD_PREP(INV_ICM42607_PWR_MGMT0_ACCEL_MODE_MASK, conf->accel.mode);
	ret = regmap_write(st->map, INV_ICM42607_REG_PWR_MGMT0, val);
	if (ret)
		return ret;

	val = FIELD_PREP(INV_ICM42607_GYRO_CONFIG0_FS_SEL_MASK, conf->gyro.fs);
	val |= FIELD_PREP(INV_ICM42607_GYRO_CONFIG0_ODR_MASK, conf->gyro.odr);
	ret = regmap_write(st->map, INV_ICM42607_REG_GYRO_CONFIG0, val);
	if (ret)
		return ret;

	val = FIELD_PREP(INV_ICM42607_ACCEL_CONFIG0_FS_SEL_MASK, conf->accel.fs);
	val |= FIELD_PREP(INV_ICM42607_ACCEL_CONFIG0_ODR_MASK, conf->accel.odr);
	ret = regmap_write(st->map, INV_ICM42607_REG_ACCEL_CONFIG0, val);
	if (ret)
		return ret;

	val = FIELD_PREP(INV_ICM42607_GYRO_CONFIG1_FILTER_MASK, conf->gyro.filter);
	ret = regmap_update_bits(st->map, INV_ICM42607_REG_GYRO_CONFIG1,
				 INV_ICM42607_GYRO_CONFIG1_FILTER_MASK, val);
	if (ret)
		return ret;

	val = FIELD_PREP(INV_ICM42607_ACCEL_CONFIG1_FILTER_MASK, conf->accel.filter);
	ret = regmap_update_bits(st->map, INV_ICM42607_REG_ACCEL_CONFIG1,
				 INV_ICM42607_ACCEL_CONFIG1_FILTER_MASK, val);
	if (ret)
		return ret;

	val = FIELD_PREP(INV_ICM42607_TEMP_CONFIG0_FILTER_MASK,
			 INV_ICM42607_TEMP_FILTER_BW_34HZ);
	ret = regmap_update_bits(st->map, INV_ICM42607_REG_TEMP_CONFIG0,
				 INV_ICM42607_TEMP_CONFIG0_FILTER_MASK, val);
	if (ret)
		return ret;

	st->conf = *conf;

	return 0;
}

static int inv_icm42607_setup(struct inv_icm42607_state *st,
			      inv_icm42607_bus_setup inv_icm42607_bus_setup)
{
	const struct device *dev = regmap_get_device(st->map);
	unsigned int val;
	int ret;

	ret = regmap_read(st->map, INV_ICM42607_REG_WHOAMI, &val);
	if (ret)
		return ret;

	/* Warn, but don't fail. */
	if (val != st->hw->whoami)
		dev_warn(dev, "Unknown whoami %#02x expected %#02x (%s)\n",
			 val, st->hw->whoami, st->hw->name);

	ret = regmap_write(st->map, INV_ICM42607_REG_SIGNAL_PATH_RESET,
			   INV_ICM42607_SIGNAL_PATH_RESET_SOFT_RESET);
	if (ret)
		return ret;

	fsleep(1 * USEC_PER_MSEC);

	/*
	 * No polling interval specified in datasheet, so use reset time as
	 * polling interval and 10x reset time as timeout period.
	 */
	ret = regmap_read_poll_timeout(st->map, INV_ICM42607_REG_INT_STATUS,
				       val, val & INV_ICM42607_INT_STATUS_RESET_DONE,
				       1 * USEC_PER_MSEC, 10 * USEC_PER_MSEC);
	if (ret)
		return dev_err_probe(dev, ret,
				     "reset error, reset done bit not set\n");

	/* Sync the regcache again after a reset. */
	regcache_mark_dirty(st->map);
	ret = regcache_sync(st->map);
	if (ret)
		return ret;

	ret = inv_icm42607_bus_setup(st);
	if (ret)
		return ret;

	ret = regmap_set_bits(st->map, INV_ICM42607_REG_INTF_CONFIG0,
			      INV_ICM42607_INTF_CONFIG0_SENSOR_DATA_ENDIAN);
	if (ret)
		return ret;

	val = FIELD_PREP(INV_ICM42607_INTF_CONFIG1_CLKSEL_MASK,
			 INV_ICM42607_INTF_CONFIG1_CLKSEL_PLL);
	ret = regmap_update_bits(st->map, INV_ICM42607_REG_INTF_CONFIG1,
				 INV_ICM42607_INTF_CONFIG1_CLKSEL_MASK,
				 val);
	if (ret)
		return ret;

	return inv_icm42607_set_init_conf(st, st->hw->conf);
}

static int inv_icm42607_enable_vddio_reg(struct inv_icm42607_state *st)
{
	int ret;

	if (st->vddio_en)
		return 0;

	ret = regulator_enable(st->vddio_supply);
	if (ret)
		return ret;

	fsleep(INV_ICM42607_POWER_UP_TIME_US);

	st->vddio_en = true;

	return 0;
}

static void inv_icm42607_sensors_off(void *_data)
{
	struct inv_icm42607_state *st = _data;
	const struct device *dev = regmap_get_device(st->map);
	int ret;

	guard(mutex)(&st->lock);

	st->conf.gyro.mode = INV_ICM42607_SENSOR_MODE_OFF;
	st->conf.accel.mode = INV_ICM42607_SENSOR_MODE_OFF;

	ret = inv_icm42607_set_pwr_mgmt0(st, st->conf.gyro.mode,
					 st->conf.accel.mode);
	if (ret)
		dev_err(dev, "Unable to turn off sensors\n");
}

static void inv_icm42607_disable_vddio_reg(void *_data)
{
	struct inv_icm42607_state *st = _data;

	if (!st->vddio_en)
		return;

	regulator_disable(st->vddio_supply);

	st->vddio_en = false;
}

int inv_icm42607_core_probe(struct regmap *regmap,
			    const struct inv_icm42607_hw *hw,
			    inv_icm42607_bus_setup inv_icm42607_bus_setup)
{
	struct device *dev = regmap_get_device(regmap);
	struct inv_icm42607_state *st;
	int ret;

	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	dev_set_drvdata(dev, st);

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	st->hw = hw;
	st->map = regmap;

	ret = iio_read_mount_matrix(dev, &st->orientation);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to retrieve mounting matrix\n");

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get vdd regulator\n");

	st->vddio_supply = devm_regulator_get(dev, "vddio");
	if (IS_ERR(st->vddio_supply))
		return dev_err_probe(dev, PTR_ERR(st->vddio_supply),
				     "Failed to get vddio regulator\n");

	ret = inv_icm42607_enable_vddio_reg(st);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, inv_icm42607_disable_vddio_reg, st);
	if (ret)
		return ret;

	/* Setup chip registers (includes WHOAMI check, reset check, bus setup) */
	ret = inv_icm42607_setup(st, inv_icm42607_bus_setup);
	if (ret)
		return ret;

	/*
	 * Ensure if sensors get turned on at some point, they're turned off
	 * as part of teardown.
	 */
	ret = devm_add_action_or_reset(dev, inv_icm42607_sensors_off, st);
	if (ret)
		return ret;

	ret = devm_pm_runtime_set_active_enabled(dev);
	if (ret)
		return ret;

	pm_runtime_set_autosuspend_delay(dev, INV_ICM42607_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	/* Initialize IIO device for Accel */
	st->indio_accel = inv_icm42607_accel_init(st);
	if (IS_ERR(st->indio_accel))
		return PTR_ERR(st->indio_accel);

	/* Initialize IIO device for Gyro */
	st->indio_gyro = inv_icm42607_gyro_init(st);
	if (IS_ERR(st->indio_gyro))
		return PTR_ERR(st->indio_gyro);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(inv_icm42607_core_probe, "IIO_ICM42607");

static int inv_icm42607_suspend(struct device *dev)
{
	struct inv_icm42607_state *st = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		return ret;

	inv_icm42607_disable_vddio_reg(st);

	return 0;
}

static int inv_icm42607_resume(struct device *dev)
{
	struct inv_icm42607_state *st = dev_get_drvdata(dev);
	int ret;

	ret = inv_icm42607_enable_vddio_reg(st);
	if (ret)
		return ret;

	/* Sync the regcache again after regulator shutdown. */
	regcache_mark_dirty(st->map);
	ret = regcache_sync(st->map);
	if (ret)
		return ret;

	return pm_runtime_force_resume(dev);
}

static int inv_icm42607_runtime_suspend(struct device *dev)
{
	struct inv_icm42607_state *st = dev_get_drvdata(dev);

	/*
	 * Set sensors state to off. Since we only support one-shot
	 * today we can use runtime PM to turn sensors off when not
	 * in use, and then when needed the reads/writes will
	 * re-enable the sensors as needed. This reduces complexity,
	 * however the tradeoff is that an unused sensor won't be
	 * turned off until the entire chip is no longer in use.
	 */
	inv_icm42607_sensors_off(st);
	return 0;
}

EXPORT_NS_GPL_DEV_PM_OPS(inv_icm42607_pm_ops, IIO_ICM42607) = {
	SYSTEM_SLEEP_PM_OPS(inv_icm42607_suspend, inv_icm42607_resume)
	RUNTIME_PM_OPS(inv_icm42607_runtime_suspend, NULL, NULL)
};

MODULE_AUTHOR("InvenSense, Inc.");
MODULE_DESCRIPTION("InvenSense ICM-42607 device driver");
MODULE_LICENSE("GPL");
