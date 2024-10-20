// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>

#include "bmi270.h"

#define BMI270_CHIP_ID_REG				0x00
#define BMI270_CHIP_ID_VAL				0x24
#define BMI270_CHIP_ID_MSK				GENMASK(7, 0)

#define BMI270_ACCEL_X_REG				0x0c
#define BMI270_ANG_VEL_X_REG				0x12

#define BMI270_INTERNAL_STATUS_REG			0x21
#define BMI270_INTERNAL_STATUS_MSG_MSK			GENMASK(3, 0)
#define BMI270_INTERNAL_STATUS_MSG_INIT_OK		0x01

#define BMI270_INTERNAL_STATUS_AXES_REMAP_ERR_MSK	BIT(5)
#define BMI270_INTERNAL_STATUS_ODR_50HZ_ERR_MSK		BIT(6)

#define BMI270_ACC_CONF_REG				0x40
#define BMI270_ACC_CONF_ODR_MSK				GENMASK(3, 0)
#define BMI270_ACC_CONF_ODR_100HZ			0x08
#define BMI270_ACC_CONF_BWP_MSK				GENMASK(6, 4)
#define BMI270_ACC_CONF_BWP_NORMAL_MODE			0x02
#define BMI270_ACC_CONF_FILTER_PERF_MSK			BIT(7)

#define BMI270_GYR_CONF_REG				0x42
#define BMI270_GYR_CONF_ODR_MSK				GENMASK(3, 0)
#define BMI270_GYR_CONF_ODR_200HZ			0x09
#define BMI270_GYR_CONF_BWP_MSK				GENMASK(5, 4)
#define BMI270_GYR_CONF_BWP_NORMAL_MODE			0x02
#define BMI270_GYR_CONF_NOISE_PERF_MSK			BIT(6)
#define BMI270_GYR_CONF_FILTER_PERF_MSK			BIT(7)

#define BMI270_INIT_CTRL_REG				0x59
#define BMI270_INIT_CTRL_LOAD_DONE_MSK			BIT(0)

#define BMI270_INIT_DATA_REG				0x5e

#define BMI270_PWR_CONF_REG				0x7c
#define BMI270_PWR_CONF_ADV_PWR_SAVE_MSK		BIT(0)
#define BMI270_PWR_CONF_FIFO_WKUP_MSK			BIT(1)
#define BMI270_PWR_CONF_FUP_EN_MSK			BIT(2)

#define BMI270_PWR_CTRL_REG				0x7d
#define BMI270_PWR_CTRL_AUX_EN_MSK			BIT(0)
#define BMI270_PWR_CTRL_GYR_EN_MSK			BIT(1)
#define BMI270_PWR_CTRL_ACCEL_EN_MSK			BIT(2)
#define BMI270_PWR_CTRL_TEMP_EN_MSK			BIT(3)

#define BMI270_INIT_DATA_FILE "bmi270-init-data.fw"

enum bmi270_scan {
	BMI270_SCAN_ACCEL_X,
	BMI270_SCAN_ACCEL_Y,
	BMI270_SCAN_ACCEL_Z,
	BMI270_SCAN_GYRO_X,
	BMI270_SCAN_GYRO_Y,
	BMI270_SCAN_GYRO_Z,
};

const struct bmi270_chip_info bmi270_chip_info = {
	.name = "bmi270",
	.chip_id = BMI270_CHIP_ID_VAL,
	.fw_name = BMI270_INIT_DATA_FILE,
};
EXPORT_SYMBOL_NS_GPL(bmi270_chip_info, IIO_BMI270);

static int bmi270_get_data(struct bmi270_data *bmi270_device,
			   int chan_type, int axis, int *val)
{
	__le16 sample;
	int reg;
	int ret;

	switch (chan_type) {
	case IIO_ACCEL:
		reg = BMI270_ACCEL_X_REG + (axis - IIO_MOD_X) * 2;
		break;
	case IIO_ANGL_VEL:
		reg = BMI270_ANG_VEL_X_REG + (axis - IIO_MOD_X) * 2;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_bulk_read(bmi270_device->regmap, reg, &sample, sizeof(sample));
	if (ret)
		return ret;

	*val = sign_extend32(le16_to_cpu(sample), 15);

	return 0;
}

static int bmi270_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret;
	struct bmi270_data *bmi270_device = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = bmi270_get_data(bmi270_device, chan->type, chan->channel2, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info bmi270_info = {
	.read_raw = bmi270_read_raw,
};

#define BMI270_ACCEL_CHANNEL(_axis) {				\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_##_axis,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
}

#define BMI270_ANG_VEL_CHANNEL(_axis) {				\
	.type = IIO_ANGL_VEL,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_##_axis,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
}

static const struct iio_chan_spec bmi270_channels[] = {
	BMI270_ACCEL_CHANNEL(X),
	BMI270_ACCEL_CHANNEL(Y),
	BMI270_ACCEL_CHANNEL(Z),
	BMI270_ANG_VEL_CHANNEL(X),
	BMI270_ANG_VEL_CHANNEL(Y),
	BMI270_ANG_VEL_CHANNEL(Z),
};

static int bmi270_validate_chip_id(struct bmi270_data *bmi270_device)
{
	int chip_id;
	int ret;
	struct device *dev = bmi270_device->dev;
	struct regmap *regmap = bmi270_device->regmap;

	ret = regmap_read(regmap, BMI270_CHIP_ID_REG, &chip_id);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read chip id");

	if (chip_id != bmi270_device->chip_info->chip_id)
		dev_info(dev, "Unknown chip id 0x%x", chip_id);

	return 0;
}

static int bmi270_write_calibration_data(struct bmi270_data *bmi270_device)
{
	int ret;
	int status = 0;
	const struct firmware *init_data;
	struct device *dev = bmi270_device->dev;
	struct regmap *regmap = bmi270_device->regmap;

	ret = regmap_clear_bits(regmap, BMI270_PWR_CONF_REG,
				BMI270_PWR_CONF_ADV_PWR_SAVE_MSK);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to write power configuration");

	/*
	 * After disabling advanced power save, all registers are accessible
	 * after a 450us delay. This delay is specified in table A of the
	 * datasheet.
	 */
	usleep_range(450, 1000);

	ret = regmap_clear_bits(regmap, BMI270_INIT_CTRL_REG,
				BMI270_INIT_CTRL_LOAD_DONE_MSK);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to prepare device to load init data");

	ret = request_firmware(&init_data,
			       bmi270_device->chip_info->fw_name, dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to load init data file");

	ret = regmap_bulk_write(regmap, BMI270_INIT_DATA_REG,
				init_data->data, init_data->size);
	release_firmware(init_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write init data");

	ret = regmap_set_bits(regmap, BMI270_INIT_CTRL_REG,
			      BMI270_INIT_CTRL_LOAD_DONE_MSK);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to stop device initialization");

	/*
	 * Wait at least 140ms for the device to complete configuration.
	 * This delay is specified in table C of the datasheet.
	 */
	usleep_range(140000, 160000);

	ret = regmap_read(regmap, BMI270_INTERNAL_STATUS_REG, &status);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read internal status");

	if (status != BMI270_INTERNAL_STATUS_MSG_INIT_OK)
		return dev_err_probe(dev, -ENODEV, "Device failed to initialize");

	return 0;
}

static int bmi270_configure_imu(struct bmi270_data *bmi270_device)
{
	int ret;
	struct device *dev = bmi270_device->dev;
	struct regmap *regmap = bmi270_device->regmap;

	ret = regmap_set_bits(regmap, BMI270_PWR_CTRL_REG,
			      BMI270_PWR_CTRL_AUX_EN_MSK |
			      BMI270_PWR_CTRL_GYR_EN_MSK |
			      BMI270_PWR_CTRL_ACCEL_EN_MSK);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable accelerometer and gyroscope");

	ret = regmap_set_bits(regmap, BMI270_ACC_CONF_REG,
			      FIELD_PREP(BMI270_ACC_CONF_ODR_MSK,
					 BMI270_ACC_CONF_ODR_100HZ) |
			      FIELD_PREP(BMI270_ACC_CONF_BWP_MSK,
					 BMI270_ACC_CONF_BWP_NORMAL_MODE) |
			      BMI270_PWR_CONF_ADV_PWR_SAVE_MSK);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to configure accelerometer");

	ret = regmap_set_bits(regmap, BMI270_GYR_CONF_REG,
			      FIELD_PREP(BMI270_GYR_CONF_ODR_MSK,
					 BMI270_GYR_CONF_ODR_200HZ) |
			      FIELD_PREP(BMI270_GYR_CONF_BWP_MSK,
					 BMI270_GYR_CONF_BWP_NORMAL_MODE) |
			      BMI270_PWR_CONF_ADV_PWR_SAVE_MSK);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to configure gyroscope");

	/* Enable FIFO_WKUP, Disable ADV_PWR_SAVE and FUP_EN */
	ret = regmap_write(regmap, BMI270_PWR_CONF_REG,
			   BMI270_PWR_CONF_FIFO_WKUP_MSK);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set power configuration");

	return 0;
}

static int bmi270_chip_init(struct bmi270_data *bmi270_device)
{
	int ret;

	ret = bmi270_validate_chip_id(bmi270_device);
	if (ret)
		return ret;

	ret = bmi270_write_calibration_data(bmi270_device);
	if (ret)
		return ret;

	return bmi270_configure_imu(bmi270_device);
}

int bmi270_core_probe(struct device *dev, struct regmap *regmap,
		      const struct bmi270_chip_info *chip_info)
{
	int ret;
	struct bmi270_data *bmi270_device;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*bmi270_device));
	if (!indio_dev)
		return -ENOMEM;

	bmi270_device = iio_priv(indio_dev);
	bmi270_device->dev = dev;
	bmi270_device->regmap = regmap;
	bmi270_device->chip_info = chip_info;

	ret = bmi270_chip_init(bmi270_device);
	if (ret)
		return ret;

	indio_dev->channels = bmi270_channels;
	indio_dev->num_channels = ARRAY_SIZE(bmi270_channels);
	indio_dev->name = chip_info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &bmi270_info;

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(bmi270_core_probe, IIO_BMI270);

MODULE_AUTHOR("Alex Lanzano");
MODULE_DESCRIPTION("BMI270 driver");
MODULE_LICENSE("GPL");
