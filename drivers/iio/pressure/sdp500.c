// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Sensirion sdp500 and sdp510 pressure sensors
 *
 * Datasheet: https://sensirion.com/resource/datasheet/sdp600
 */

#include <linux/i2c.h>
#include <linux/crc8.h>
#include <linux/iio/iio.h>
#include <linux/mod_devicetable.h>
#include <linux/regulator/consumer.h>
#include <linux/unaligned.h>

#define SDP500_CRC8_POLYNOMIAL  0x31   /* x8+x5+x4+1 (normalized to 0x31) */
#define SDP500_READ_SIZE        3

#define SDP500_I2C_START_MEAS 0xF1

struct sdp500_data {
	struct device *dev;
};

DECLARE_CRC8_TABLE(sdp500_crc8_table);

static int sdp500_start_measurement(struct sdp500_data *data)
{
	struct i2c_client *client = to_i2c_client(data->dev);

	return i2c_smbus_write_byte(client, SDP500_I2C_START_MEAS);
}

static const struct iio_chan_spec sdp500_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
	},
};

static int sdp500_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	int ret;
	u8 rxbuf[SDP500_READ_SIZE];
	u8 received_crc, calculated_crc;
	struct sdp500_data *data = iio_priv(indio_dev);
	struct i2c_client *client = to_i2c_client(data->dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = i2c_master_recv(client, rxbuf, SDP500_READ_SIZE);
		if (ret < 0) {
			dev_err(data->dev, "Failed to receive data");
			return ret;
		}
		if (ret != SDP500_READ_SIZE) {
			dev_err(data->dev, "Data is received wrongly");
			return -EIO;
		}

		received_crc = rxbuf[2];
		calculated_crc = crc8(sdp500_crc8_table, rxbuf,
				      sizeof(rxbuf) - 1, 0x00);
		if (received_crc != calculated_crc) {
			dev_err(data->dev,
				"calculated crc = 0x%.2X, received 0x%.2X",
				calculated_crc, received_crc);
			return -EIO;
		}

		*val = get_unaligned_be16(rxbuf);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 1;
		*val2 = 60;

		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;
	}
}

static const struct iio_info sdp500_info = {
	.read_raw = &sdp500_read_raw,
};

static int sdp500_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct sdp500_data *data;
	struct device *dev = &client->dev;
	int ret;
	u8 rxbuf[SDP500_READ_SIZE];

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret,
			"Failed to get and enable regulator\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	/* has to be done before the first i2c communication */
	crc8_populate_msb(sdp500_crc8_table, SDP500_CRC8_POLYNOMIAL);

	data = iio_priv(indio_dev);
	data->dev = dev;

	indio_dev->name = "sdp500";
	indio_dev->channels = sdp500_channels;
	indio_dev->info = &sdp500_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->num_channels = ARRAY_SIZE(sdp500_channels);

	ret = sdp500_start_measurement(data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to start measurement");

	/* First measurement is not correct, read it out to get rid of it */
	i2c_master_recv(client, rxbuf, SDP500_READ_SIZE);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register indio_dev");

	return 0;
}

static const struct i2c_device_id sdp500_id[] = {
	{ "sdp500" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sdp500_id);

static const struct of_device_id sdp500_of_match[] = {
	{ .compatible = "sensirion,sdp500" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdp500_of_match);

static struct i2c_driver sdp500_driver = {
	.driver = {
		.name = "sensirion,sdp500",
		.of_match_table = sdp500_of_match,
	},
	.probe = sdp500_probe,
	.id_table = sdp500_id,
};
module_i2c_driver(sdp500_driver);

MODULE_AUTHOR("Thomas Sioutas <thomas.sioutas@prodrive-technologies.com>");
MODULE_DESCRIPTION("Driver for Sensirion SDP500 differential pressure sensor");
MODULE_LICENSE("GPL");
