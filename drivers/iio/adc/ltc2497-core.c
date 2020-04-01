// SPDX-License-Identifier: GPL-2.0-only
/*
 * ltc2497-core.c - Common code for Analog Devices/Linear Technology
 * LTC2496 and LTC2497 ADCs
 *
 * Copyright (C) 2017 Analog Devices Inc.
 */

#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include "ltc2497.h"

#define LTC2497_SGL			BIT(4)
#define LTC2497_DIFF			0
#define LTC2497_SIGN			BIT(3)

static int ltc2497core_wait_conv(struct ltc2497core_driverdata *ddata)
{
	s64 time_elapsed;

	time_elapsed = ktime_ms_delta(ktime_get(), ddata->time_prev);

	if (time_elapsed < LTC2497_CONVERSION_TIME_MS) {
		/* delay if conversion time not passed
		 * since last read or write
		 */
		if (msleep_interruptible(
		    LTC2497_CONVERSION_TIME_MS - time_elapsed))
			return -ERESTARTSYS;

		return 0;
	}

	if (time_elapsed - LTC2497_CONVERSION_TIME_MS <= 0) {
		/* We're in automatic mode -
		 * so the last reading is still not outdated
		 */
		return 0;
	}

	return 1;
}

static int ltc2497core_read(struct ltc2497core_driverdata *ddata, u8 address, int *val)
{
	int ret;

	ret = ltc2497core_wait_conv(ddata);
	if (ret < 0)
		return ret;

	if (ret || ddata->addr_prev != address) {
		ret = ddata->result_and_measure(ddata, address, NULL);
		if (ret < 0)
			return ret;
		ddata->addr_prev = address;

		if (msleep_interruptible(LTC2497_CONVERSION_TIME_MS))
			return -ERESTARTSYS;
	}

	ret = ddata->result_and_measure(ddata, address, val);
	if (ret < 0)
		return ret;

	ddata->time_prev = ktime_get();

	return ret;
}

static int ltc2497core_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct ltc2497core_driverdata *ddata = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		ret = ltc2497core_read(ddata, chan->address, val);
		mutex_unlock(&indio_dev->mlock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(ddata->ref);
		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = 17;

		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

#define LTC2497_CHAN(_chan, _addr, _ds_name) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = (_chan), \
	.address = (_addr | (_chan / 2) | ((_chan & 1) ? LTC2497_SIGN : 0)), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.datasheet_name = (_ds_name), \
}

#define LTC2497_CHAN_DIFF(_chan, _addr) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = (_chan) * 2 + ((_addr) & LTC2497_SIGN ? 1 : 0), \
	.channel2 = (_chan) * 2 + ((_addr) & LTC2497_SIGN ? 0 : 1),\
	.address = (_addr | _chan), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.differential = 1, \
}

static const struct iio_chan_spec ltc2497core_channel[] = {
	LTC2497_CHAN(0, LTC2497_SGL, "CH0"),
	LTC2497_CHAN(1, LTC2497_SGL, "CH1"),
	LTC2497_CHAN(2, LTC2497_SGL, "CH2"),
	LTC2497_CHAN(3, LTC2497_SGL, "CH3"),
	LTC2497_CHAN(4, LTC2497_SGL, "CH4"),
	LTC2497_CHAN(5, LTC2497_SGL, "CH5"),
	LTC2497_CHAN(6, LTC2497_SGL, "CH6"),
	LTC2497_CHAN(7, LTC2497_SGL, "CH7"),
	LTC2497_CHAN(8, LTC2497_SGL, "CH8"),
	LTC2497_CHAN(9, LTC2497_SGL, "CH9"),
	LTC2497_CHAN(10, LTC2497_SGL, "CH10"),
	LTC2497_CHAN(11, LTC2497_SGL, "CH11"),
	LTC2497_CHAN(12, LTC2497_SGL, "CH12"),
	LTC2497_CHAN(13, LTC2497_SGL, "CH13"),
	LTC2497_CHAN(14, LTC2497_SGL, "CH14"),
	LTC2497_CHAN(15, LTC2497_SGL, "CH15"),
	LTC2497_CHAN_DIFF(0, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(1, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(2, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(3, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(4, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(5, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(6, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(7, LTC2497_DIFF),
	LTC2497_CHAN_DIFF(0, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(1, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(2, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(3, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(4, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(5, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(6, LTC2497_DIFF | LTC2497_SIGN),
	LTC2497_CHAN_DIFF(7, LTC2497_DIFF | LTC2497_SIGN),
};

static const struct iio_info ltc2497core_info = {
	.read_raw = ltc2497core_read_raw,
};

int ltc2497core_probe(struct device *dev, struct iio_dev *indio_dev)
{
	struct ltc2497core_driverdata *ddata = iio_priv(indio_dev);
	int ret;

	indio_dev->dev.parent = dev;
	indio_dev->name = dev_name(dev);
	indio_dev->info = &ltc2497core_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ltc2497core_channel;
	indio_dev->num_channels = ARRAY_SIZE(ltc2497core_channel);

	ret = ddata->result_and_measure(ddata, LTC2497_CONFIG_DEFAULT, NULL);
	if (ret < 0)
		return ret;

	ddata->ref = devm_regulator_get(dev, "vref");
	if (IS_ERR(ddata->ref)) {
		if (PTR_ERR(ddata->ref) != -EPROBE_DEFER)
			dev_err(dev, "Failed to get vref regulator: %pe\n",
				ddata->ref);

		return PTR_ERR(ddata->ref);
	}

	ret = regulator_enable(ddata->ref);
	if (ret < 0) {
		dev_err(dev, "Failed to enable vref regulator: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	if (dev->platform_data) {
		struct iio_map *plat_data;

		plat_data = (struct iio_map *)dev->platform_data;

		ret = iio_map_array_register(indio_dev, plat_data);
		if (ret) {
			dev_err(&indio_dev->dev, "iio map err: %d\n", ret);
			goto err_regulator_disable;
		}
	}

	ddata->addr_prev = LTC2497_CONFIG_DEFAULT;
	ddata->time_prev = ktime_get();

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto err_array_unregister;

	return 0;

err_array_unregister:
	iio_map_array_unregister(indio_dev);

err_regulator_disable:
	regulator_disable(ddata->ref);

	return ret;
}
EXPORT_SYMBOL_NS(ltc2497core_probe, LTC2497);

void ltc2497core_remove(struct iio_dev *indio_dev)
{
	struct ltc2497core_driverdata *ddata = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	iio_map_array_unregister(indio_dev);

	regulator_disable(ddata->ref);
}
EXPORT_SYMBOL_NS(ltc2497core_remove, LTC2497);

MODULE_DESCRIPTION("common code for LTC2496/LTC2497 drivers");
MODULE_LICENSE("GPL v2");
