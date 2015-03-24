/*
 * mlx90614.c - Support for Melexis MLX90614 contactless IR temperature sensor
 *
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
 * Copyright (c) 2015 Essensium NV
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Driver for the Melexis MLX90614 I2C 16-bit IR thermopile sensor
 *
 * (7-bit I2C slave address 0x5a, 100KHz bus speed only!)
 *
 * TODO: sleep mode, configuration EEPROM
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>

#include <linux/iio/iio.h>

#define MLX90614_OP_RAM		0x00
#define MLX90614_OP_EEPROM	0x20
#define MLX90614_OP_SLEEP	0xff

/* RAM offsets with 16-bit data, MSB first */
#define MLX90614_RAW1	(MLX90614_OP_RAM | 0x04) /* raw data IR channel 1 */
#define MLX90614_RAW2	(MLX90614_OP_RAM | 0x05) /* raw data IR channel 2 */
#define MLX90614_TA	(MLX90614_OP_RAM | 0x06) /* ambient temperature */
#define MLX90614_TOBJ1	(MLX90614_OP_RAM | 0x07) /* object 1 temperature */
#define MLX90614_TOBJ2	(MLX90614_OP_RAM | 0x08) /* object 2 temperature */

/* EEPROM offsets with 16-bit data, MSB first */
#define MLX90614_EMISSIVITY	(MLX90614_OP_EEPROM | 0x04) /* emissivity correction coefficient */
#define MLX90614_CONFIG		(MLX90614_OP_EEPROM | 0x05) /* configuration register */

/* Control bits in configuration register */
#define MLX90614_CONFIG_IIR_SHIFT 0 /* IIR coefficient */
#define MLX90614_CONFIG_IIR_MASK (0x7 << MLX90614_CONFIG_IIR_SHIFT)
#define MLX90614_CONFIG_DUAL_SHIFT 6 /* single (0) or dual (1) IR sensor */
#define MLX90614_CONFIG_DUAL_MASK (1 << MLX90614_CONFIG_DUAL_SHIFT)
#define MLX90614_CONFIG_FIR_SHIFT 8 /* FIR coefficient */
#define MLX90614_CONFIG_FIR_MASK (0x7 << MLX90614_CONFIG_FIR_SHIFT)
#define MLX90614_CONFIG_GAIN_SHIFT 11 /* gain */
#define MLX90614_CONFIG_GAIN_MASK (0x7 << MLX90614_CONFIG_GAIN_SHIFT)

/* Timings (in ms) */
#define MLX90614_TIMING_EEPROM 20 /* time for EEPROM write/erase to complete */
#define MLX90614_TIMING_WAKEUP 34 /* time to hold SDA low for wake-up */
#define MLX90614_TIMING_STARTUP 250 /* time before first data after wake-up */

struct mlx90614_data {
	struct i2c_client *client;
};

static int mlx90614_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int *val,
			    int *val2, long mask)
{
	struct mlx90614_data *data = iio_priv(indio_dev);
	u8 cmd;
	s32 ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: /* 0.02K / LSB */
		switch (channel->channel2) {
		case IIO_MOD_TEMP_AMBIENT:
			cmd = MLX90614_TA;
			break;
		case IIO_MOD_TEMP_OBJECT:
			switch (channel->channel) {
			case 0:
				cmd = MLX90614_TOBJ1;
				break;
			case 1:
				cmd = MLX90614_TOBJ2;
				break;
			default:
				return -EINVAL;
			}
			break;
		default:
			return -EINVAL;
		}

		ret = i2c_smbus_read_word_data(data->client, cmd);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		*val = 13657;
		*val2 = 500000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		*val = 20;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec mlx90614_channels[] = {
	{
		.type = IIO_TEMP,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_AMBIENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		    BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_TEMP,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_OBJECT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		    BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.modified = 1,
		.channel = 1,
		.channel2 = IIO_MOD_TEMP_OBJECT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		    BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct iio_info mlx90614_info = {
	.read_raw = mlx90614_read_raw,
	.driver_module = THIS_MODULE,
};

/* Return 0 for single sensor, 1 for dual sensor, <0 on error. */
static int mlx90614_probe_num_ir_sensors(struct i2c_client *client)
{
	s32 ret;

	ret = i2c_smbus_read_word_data(client, MLX90614_CONFIG);

	if (ret < 0)
		return ret;

	return (ret & MLX90614_CONFIG_DUAL_MASK) ? 1 : 0;
}

static int mlx90614_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct mlx90614_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mlx90614_info;

	ret = mlx90614_probe_num_ir_sensors(client);
	switch (ret) {
	case 0:
		dev_dbg(&client->dev, "Found single sensor");
		indio_dev->channels = mlx90614_channels;
		indio_dev->num_channels = 2;
		break;
	case 1:
		dev_dbg(&client->dev, "Found dual sensor");
		indio_dev->channels = mlx90614_channels;
		indio_dev->num_channels = 3;
		break;
	default:
		return ret;
	}

	return iio_device_register(indio_dev);
}

static int mlx90614_remove(struct i2c_client *client)
{
	iio_device_unregister(i2c_get_clientdata(client));

	return 0;
}

static const struct i2c_device_id mlx90614_id[] = {
	{ "mlx90614", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mlx90614_id);

static struct i2c_driver mlx90614_driver = {
	.driver = {
		.name	= "mlx90614",
		.owner	= THIS_MODULE,
	},
	.probe = mlx90614_probe,
	.remove = mlx90614_remove,
	.id_table = mlx90614_id,
};
module_i2c_driver(mlx90614_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_AUTHOR("Vianney le Clément de Saint-Marcq <vianney.leclement@essensium.com>");
MODULE_DESCRIPTION("Melexis MLX90614 contactless IR temperature sensor driver");
MODULE_LICENSE("GPL");
