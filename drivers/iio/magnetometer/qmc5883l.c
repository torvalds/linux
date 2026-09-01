// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Support for QST QMC5883L 3-Axis Magnetic Sensor on I2C bus.
 *
 * Copyright (C) 2026 Siratul Islam <siratul.islam@linux.dev>
 *
 * Datasheet available at
 * <https://www.qstcorp.com/upload/pdf/202512/13-52-04%20QMC5883L%20Datasheet%20Rev.%20B.pdf>
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/time.h>
#include <linux/types.h>

#include <asm/byteorder.h>

#include <linux/iio/iio.h>

#define QMC5883L_REG_X_LSB	0x00
#define QMC5883L_REG_STATUS1	0x06
#define QMC5883L_REG_CTRL1	0x09
#define QMC5883L_REG_CTRL2	0x0A
#define QMC5883L_REG_SET_RESET	0x0B
#define QMC5883L_REG_ID		0x0D

#define QMC5883L_CHIP_ID	0xFF

#define QMC5883L_MODE_MASK	GENMASK(1, 0)
#define QMC5883L_ODR_MASK	GENMASK(3, 2)
#define QMC5883L_RNG_MASK	GENMASK(5, 4)
#define QMC5883L_OSR_MASK	GENMASK(7, 6)

#define QMC5883L_MODE_STANDBY	0x00
#define QMC5883L_MODE_CONT	0x01

#define QMC5883L_ODR_10HZ	0x00
#define QMC5883L_ODR_50HZ	0x01
#define QMC5883L_ODR_100HZ	0x02
#define QMC5883L_ODR_200HZ	0x03

#define QMC5883L_RNG_2G		0x00
#define QMC5883L_RNG_8G		0x01

#define QMC5883L_OSR_512	0x00
#define QMC5883L_OSR_256	0x01
#define QMC5883L_OSR_128	0x02
#define QMC5883L_OSR_64		0x03

#define QMC5883L_STATUS_DRDY	BIT(0)
#define QMC5883L_STATUS_OVL	BIT(1)

#define QMC5883L_SET_RESET_VAL	BIT(0)
#define QMC5883L_INT_DISABLE	BIT(0)
#define QMC5883L_SOFT_RESET	BIT(7)

#define QMC5883L_PORT_US	350

struct qmc5883l_data {
	struct regmap *regmap;
	/*
	 * Protect data->range/odr/osr.
	 * Protect poll and read during measurement.
	 */
	struct mutex mutex;
	u8 range;
	u8 odr;
	u8 osr;
};

enum qmc5883l_chan {
	QMC5883L_AXIS_X,
	QMC5883L_AXIS_Y,
	QMC5883L_AXIS_Z,
};

static const int qmc5883l_odr_avail[] = { 10, 50, 100, 200 };

static const int qmc5883l_osr_avail[] = { 512, 256, 128, 64 };

static const int qmc5883l_scales[][2] = {
	[QMC5883L_RNG_2G] = { 0, 83333 },
	[QMC5883L_RNG_8G] = { 0, 333333 },
};

static int qmc5883l_take_measurement(struct iio_dev *indio_dev, int index,
				     int *val)
{
	struct qmc5883l_data *data = iio_priv(indio_dev);
	struct regmap *map = data->regmap;
	unsigned int status;
	__le16 buf[3];
	int ret;

	guard(mutex) (&data->mutex);

	/* 50ms headroom over the slowest ODR (10Hz) */
	ret = regmap_read_poll_timeout(map, QMC5883L_REG_STATUS1,
				       status, (status & QMC5883L_STATUS_DRDY),
				       2 * USEC_PER_MSEC, 150 * USEC_PER_MSEC);
	if (ret)
		return ret;

	ret = regmap_bulk_read(map, QMC5883L_REG_X_LSB, buf, sizeof(buf));
	if (ret)
		return ret;

	if (status & QMC5883L_STATUS_OVL)
		return -ERANGE;

	*val = (s16)le16_to_cpu(buf[index]);

	return 0;
}

static int qmc5883l_read_raw(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan,
			     int *val, int *val2, long mask)
{
	struct qmc5883l_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = qmc5883l_take_measurement(indio_dev, chan->address, val);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE: {
		guard(mutex)(&data->mutex);

		*val = qmc5883l_scales[data->range][0];
		*val2 = qmc5883l_scales[data->range][1];

		return IIO_VAL_INT_PLUS_NANO;
	}
	case IIO_CHAN_INFO_SAMP_FREQ: {
		guard(mutex)(&data->mutex);

		switch (data->odr) {
		case QMC5883L_ODR_200HZ:
			*val = 200;
			break;
		case QMC5883L_ODR_100HZ:
			*val = 100;
			break;
		case QMC5883L_ODR_50HZ:
			*val = 50;
			break;
		case QMC5883L_ODR_10HZ:
			*val = 10;
			break;
		default:
			return -EINVAL;
		}

		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO: {
		guard(mutex)(&data->mutex);

		switch (data->osr) {
		case QMC5883L_OSR_64:
			*val = 64;
			break;
		case QMC5883L_OSR_128:
			*val = 128;
			break;
		case QMC5883L_OSR_256:
			*val = 256;
			break;
		case QMC5883L_OSR_512:
			*val = 512;
			break;
		default:
			return -EINVAL;
		}

		return IIO_VAL_INT;
	}
	default:
		return -EINVAL;
	}
}

static int qmc5883l_write_raw(struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan,
			      int val, int val2, long mask)
{
	struct qmc5883l_data *data = iio_priv(indio_dev);
	u8 rng, osr, odr;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE: {
		if (val != 0)
			return -EINVAL;

		if (val2 == qmc5883l_scales[QMC5883L_RNG_2G][1])
			rng = QMC5883L_RNG_2G;
		else if (val2 == qmc5883l_scales[QMC5883L_RNG_8G][1])
			rng = QMC5883L_RNG_8G;
		else
			return -EINVAL;

		guard(mutex)(&data->mutex);

		ret = regmap_update_bits(data->regmap, QMC5883L_REG_CTRL1,
					 QMC5883L_RNG_MASK,
					 FIELD_PREP(QMC5883L_RNG_MASK, rng));
		if (ret)
			return ret;

		data->range = rng;

		return 0;
	}
	case IIO_CHAN_INFO_SAMP_FREQ: {
		switch (val) {
		case 200:
			odr = QMC5883L_ODR_200HZ;
			break;
		case 100:
			odr = QMC5883L_ODR_100HZ;
			break;
		case 50:
			odr = QMC5883L_ODR_50HZ;
			break;
		case 10:
			odr = QMC5883L_ODR_10HZ;
			break;
		default:
			return -EINVAL;
		}

		guard(mutex)(&data->mutex);

		ret = regmap_update_bits(data->regmap, QMC5883L_REG_CTRL1,
					 QMC5883L_ODR_MASK,
					 FIELD_PREP(QMC5883L_ODR_MASK, odr));
		if (ret)
			return ret;

		data->odr = odr;

		return 0;
	}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO: {
		switch (val) {
		case 64:
			osr = QMC5883L_OSR_64;
			break;
		case 128:
			osr = QMC5883L_OSR_128;
			break;
		case 256:
			osr = QMC5883L_OSR_256;
			break;
		case 512:
			osr = QMC5883L_OSR_512;
			break;
		default:
			return -EINVAL;
		}

		guard(mutex)(&data->mutex);

		ret = regmap_update_bits(data->regmap, QMC5883L_REG_CTRL1,
					 QMC5883L_OSR_MASK,
					 FIELD_PREP(QMC5883L_OSR_MASK, osr));
		if (ret)
			return ret;

		data->osr = osr;

		return 0;
	}
	default:
		return -EINVAL;
	}
}

static int qmc5883l_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = qmc5883l_odr_avail;
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(qmc5883l_odr_avail);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = qmc5883l_osr_avail;
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(qmc5883l_osr_avail);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SCALE:
		*vals = (const int *)qmc5883l_scales;
		*type = IIO_VAL_INT_PLUS_NANO;
		*length = ARRAY_SIZE(qmc5883l_scales) * 2;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int qmc5883l_write_raw_get_fmt(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return IIO_VAL_INT;
	}
}

static const struct iio_info qmc5883l_info = {
	.read_raw = qmc5883l_read_raw,
	.write_raw = qmc5883l_write_raw,
	.read_avail = qmc5883l_read_avail,
	.write_raw_get_fmt = qmc5883l_write_raw_get_fmt,
};

static int qmc5883l_init(struct qmc5883l_data *data)
{
	struct regmap *map = data->regmap;
	unsigned int reg;
	int ret;

	ret = regmap_read(map, QMC5883L_REG_ID, &reg);
	if (ret)
		return ret;

	/* Not failing because rev 1.0 had this register reserved */
	if (reg != QMC5883L_CHIP_ID)
		dev_warn(regmap_get_device(map),
			 "Unknown chip id: 0x%02x, continuing\n", reg);

	ret = regmap_write(map, QMC5883L_REG_CTRL2, QMC5883L_SOFT_RESET);
	if (ret)
		return ret;

	/* Use POR completion time as a conservative bound */
	fsleep(QMC5883L_PORT_US);

	/* DRDY pin not used in this version of the driver */
	ret = regmap_write(map, QMC5883L_REG_CTRL2, QMC5883L_INT_DISABLE);
	if (ret)
		return ret;

	ret = regmap_write(map, QMC5883L_REG_SET_RESET, QMC5883L_SET_RESET_VAL);
	if (ret)
		return ret;

	data->odr = QMC5883L_ODR_50HZ;
	data->range = QMC5883L_RNG_2G;
	data->osr = QMC5883L_OSR_64;

	return regmap_write(map, QMC5883L_REG_CTRL1,
			    FIELD_PREP(QMC5883L_MODE_MASK, QMC5883L_MODE_CONT) |
			    FIELD_PREP(QMC5883L_ODR_MASK, data->odr) |
			    FIELD_PREP(QMC5883L_RNG_MASK, data->range) |
			    FIELD_PREP(QMC5883L_OSR_MASK, data->osr));
}

static void qmc5883l_power_down_action(void *priv)
{
	struct qmc5883l_data *data = priv;

	regmap_update_bits(data->regmap, QMC5883L_REG_CTRL1,
			   QMC5883L_MODE_MASK,
			   FIELD_PREP(QMC5883L_MODE_MASK, QMC5883L_MODE_STANDBY));
}

static bool qmc5883l_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg <= QMC5883L_REG_STATUS1;
}

static bool qmc5883l_writable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case QMC5883L_REG_CTRL1:
	case QMC5883L_REG_CTRL2:
	case QMC5883L_REG_SET_RESET:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config qmc5883l_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = QMC5883L_REG_ID,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = qmc5883l_volatile_reg,
	.writeable_reg = qmc5883l_writable_reg,
};

#define QMC5883L_CHANNEL(_axis)                                \
	{                                                      \
		.type = IIO_MAGN,                              \
		.modified = 1,                                 \
		.channel2 = IIO_MOD_##_axis,                   \
		.address = QMC5883L_AXIS_##_axis,              \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),  \
		.info_mask_shared_by_type =                    \
			BIT(IIO_CHAN_INFO_SCALE) |             \
			BIT(IIO_CHAN_INFO_SAMP_FREQ) |         \
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
		.info_mask_shared_by_type_available =          \
			BIT(IIO_CHAN_INFO_SCALE) |             \
			BIT(IIO_CHAN_INFO_SAMP_FREQ) |         \
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
	}

static const struct iio_chan_spec qmc5883l_channels[] = {
	QMC5883L_CHANNEL(X),
	QMC5883L_CHANNEL(Y),
	QMC5883L_CHANNEL(Z),
};

static int qmc5883l_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct qmc5883l_data *data;
	struct iio_dev *indio_dev;
	struct regmap *map;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	map = devm_regmap_init_i2c(client, &qmc5883l_regmap_config);
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map),
				     "regmap initialization failed\n");

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to enable VDD regulator\n");

	ret = devm_regulator_get_enable(dev, "vddio");
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to enable VDDIO regulator\n");

	/* POR completion time max per datasheet Table 7 */
	fsleep(QMC5883L_PORT_US);

	data = iio_priv(indio_dev);
	data->regmap = map;

	ret = devm_mutex_init(dev, &data->mutex);
	if (ret)
		return ret;

	indio_dev->name = "qmc5883l";
	indio_dev->info = &qmc5883l_info;
	indio_dev->channels = qmc5883l_channels;
	indio_dev->num_channels = ARRAY_SIZE(qmc5883l_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = qmc5883l_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "qmc5883l init failed\n");

	ret = devm_add_action_or_reset(dev, qmc5883l_power_down_action, data);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id qmc5883l_match[] = {
	{ .compatible = "qstcorp,qmc5883l" },
	{ }
};
MODULE_DEVICE_TABLE(of, qmc5883l_match);

static const struct i2c_device_id qmc5883l_id[] = {
	{ .name = "qmc5883l" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, qmc5883l_id);

static struct i2c_driver qmc5883l_driver = {
	.driver = {
		.name = "qmc5883l",
		.of_match_table = qmc5883l_match,
	},
	.id_table = qmc5883l_id,
	.probe = qmc5883l_probe,
};
module_i2c_driver(qmc5883l_driver);

MODULE_DESCRIPTION("QST QMC5883L 3-Axis Magnetic Sensor driver");
MODULE_AUTHOR("Siratul Islam <siratul.islam@linux.dev>");
MODULE_LICENSE("Dual BSD/GPL");
