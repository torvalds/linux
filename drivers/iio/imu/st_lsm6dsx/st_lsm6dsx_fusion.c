// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsx IMU sensor fusion
 *
 * Copyright 2026 BayLibre, SAS
 */

#include <linux/cleanup.h>
#include <linux/errno.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/sprintf.h>
#include <linux/types.h>
#include <linux/units.h>

#include "st_lsm6dsx.h"

static int
st_lsm6dsx_fusion_get_odr_val(const struct st_lsm6dsx_fusion_settings *settings,
			      u32 odr_mHz, u8 *val)
{
	int odr_hz = odr_mHz / MILLI;
	int i;

	for (i = 0; i < settings->odr_len; i++) {
		if (settings->odr_hz[i] == odr_hz)
			break;
	}
	if (i == settings->odr_len)
		return -EINVAL;

	*val = i;
	return 0;
}

/**
 * st_lsm6dsx_fusion_page_enable - Enable access to sensor fusion configuration
 * registers.
 * @hw: Sensor hardware instance.
 *
 * Return: 0 on success, negative value on error.
 */
static int st_lsm6dsx_fusion_page_enable(struct st_lsm6dsx_hw *hw)
{
	const struct st_lsm6dsx_reg *mux;

	mux = &hw->settings->fusion_settings.page_mux;

	return regmap_set_bits(hw->regmap, mux->addr, mux->mask);
}

/**
 * st_lsm6dsx_fusion_page_disable - Disable access to sensor fusion
 * configuration registers.
 * @hw: Sensor hardware instance.
 *
 * Return: 0 on success, negative value on error.
 */
static int st_lsm6dsx_fusion_page_disable(struct st_lsm6dsx_hw *hw)
{
	const struct st_lsm6dsx_reg *mux;

	mux = &hw->settings->fusion_settings.page_mux;

	return regmap_clear_bits(hw->regmap, mux->addr, mux->mask);
}

static int st_lsm6dsx_fusion_set_odr_locked(struct st_lsm6dsx_sensor *sensor,
					    bool enable)
{
	const struct st_lsm6dsx_fusion_settings *settings;
	struct st_lsm6dsx_hw *hw = sensor->hw;
	int err;

	settings = &hw->settings->fusion_settings;
	if (enable) {
		const struct st_lsm6dsx_reg *reg = &settings->odr_reg;
		u8 odr_val;
		u8 data;

		st_lsm6dsx_fusion_get_odr_val(settings, sensor->hwfifo_odr_mHz,
					      &odr_val);
		data = ST_LSM6DSX_SHIFT_VAL(odr_val, reg->mask);
		err = regmap_update_bits(hw->regmap, reg->addr, reg->mask,
					 data);
		if (err)
			return err;
	}

	return regmap_assign_bits(hw->regmap, settings->fifo_enable.addr,
				  settings->fifo_enable.mask, enable);
}

int st_lsm6dsx_fusion_set_enable(struct st_lsm6dsx_sensor *sensor, bool enable)
{
	struct st_lsm6dsx_hw *hw = sensor->hw;
	const struct st_lsm6dsx_reg *en_reg;
	int err;

	guard(mutex)(&hw->page_lock);

	en_reg = &hw->settings->fusion_settings.enable;
	err = st_lsm6dsx_fusion_page_enable(hw);
	if (err)
		return err;

	err = regmap_assign_bits(hw->regmap, en_reg->addr, en_reg->mask, enable);
	if (err) {
		st_lsm6dsx_fusion_page_disable(hw);
		return err;
	}

	return st_lsm6dsx_fusion_page_disable(hw);
}

int st_lsm6dsx_fusion_set_odr(struct st_lsm6dsx_sensor *sensor, bool enable)
{
	struct st_lsm6dsx_hw *hw = sensor->hw;
	int err;

	guard(mutex)(&hw->page_lock);

	err = st_lsm6dsx_fusion_page_enable(hw);
	if (err)
		return err;

	err = st_lsm6dsx_fusion_set_odr_locked(sensor, enable);
	if (err) {
		st_lsm6dsx_fusion_page_disable(hw);
		return err;
	}

	return st_lsm6dsx_fusion_page_disable(hw);
}

static int st_lsm6dsx_fusion_read_raw(struct iio_dev *iio_dev,
				      struct iio_chan_spec const *ch,
				      int *val, int *val2, long mask)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = sensor->hwfifo_odr_mHz / MILLI;
		*val2 = (sensor->hwfifo_odr_mHz % MILLI) * (MICRO / MILLI);
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int st_lsm6dsx_fusion_write_raw(struct iio_dev *iio_dev,
				       struct iio_chan_spec const *chan,
				       int val, int val2, long mask)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(iio_dev);
	const struct st_lsm6dsx_fusion_settings *settings;
	int err;

	settings = &sensor->hw->settings->fusion_settings;
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ: {
		u32 odr_mHz = val * MILLI + val2 * (MILLI / MICRO);
		u8 odr_val;

		/* check that the requested frequency is supported */
		err = st_lsm6dsx_fusion_get_odr_val(settings, odr_mHz, &odr_val);
		if (err)
			return err;

		sensor->hwfifo_odr_mHz = odr_mHz;
		return 0;
	}
	default:
		return -EINVAL;
	}
}

static int st_lsm6dsx_fusion_read_avail(struct iio_dev *indio_dev,
					struct iio_chan_spec const *chan,
					const int **vals, int *type,
					int *length, long mask)
{
	struct st_lsm6dsx_sensor *sensor = iio_priv(indio_dev);
	const struct st_lsm6dsx_fusion_settings *settings;

	settings = &sensor->hw->settings->fusion_settings;
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = settings->odr_hz;
		*type = IIO_VAL_INT;
		*length = settings->odr_len;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static const struct iio_info st_lsm6dsx_fusion_info = {
	.read_raw = st_lsm6dsx_fusion_read_raw,
	.read_avail = st_lsm6dsx_fusion_read_avail,
	.write_raw = st_lsm6dsx_fusion_write_raw,
	.hwfifo_set_watermark = st_lsm6dsx_set_watermark,
};

int st_lsm6dsx_fusion_probe(struct st_lsm6dsx_hw *hw, const char *name)
{
	const struct st_lsm6dsx_fusion_settings *settings;
	struct st_lsm6dsx_sensor *sensor;
	struct iio_dev *iio_dev;
	int ret;

	iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	if (!iio_dev)
		return -ENOMEM;

	settings = &hw->settings->fusion_settings;
	sensor = iio_priv(iio_dev);
	sensor->id = ST_LSM6DSX_ID_FUSION;
	sensor->hw = hw;
	sensor->hwfifo_odr_mHz = settings->odr_hz[0] * MILLI;
	sensor->watermark = 1;
	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->info = &st_lsm6dsx_fusion_info;
	iio_dev->channels = settings->chan;
	iio_dev->num_channels = settings->chan_len;
	ret = snprintf(sensor->name, sizeof(sensor->name), "%s_fusion", name);
	if (ret >= sizeof(sensor->name))
		return -E2BIG;
	iio_dev->name = sensor->name;

	/*
	 * Put the IIO device pointer in the iio_devs array so that the caller
	 * can set up a buffer and register this IIO device.
	 */
	hw->iio_devs[ST_LSM6DSX_ID_FUSION] = iio_dev;

	return 0;
}
