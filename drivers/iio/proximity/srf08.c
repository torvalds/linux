/*
 * srf08.c - Support for Devantech SRF08 ultrasonic ranger
 *
 * Copyright (c) 2016 Andreas Klinger <ak@it-klinger.de>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * For details about the device see:
 * http://www.robot-electronics.co.uk/htm/srf08tech.html
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* registers of SRF08 device */
#define SRF08_WRITE_COMMAND	0x00	/* Command Register */
#define SRF08_WRITE_MAX_GAIN	0x01	/* Max Gain Register: 0 .. 31 */
#define SRF08_WRITE_RANGE	0x02	/* Range Register: 0 .. 255 */
#define SRF08_READ_SW_REVISION	0x00	/* Software Revision */
#define SRF08_READ_LIGHT	0x01	/* Light Sensor during last echo */
#define SRF08_READ_ECHO_1_HIGH	0x02	/* Range of first echo received */
#define SRF08_READ_ECHO_1_LOW	0x03	/* Range of first echo received */

#define SRF08_CMD_RANGING_CM	0x51	/* Ranging Mode - Result in cm */

#define SRF08_DEFAULT_GAIN	1025	/* default analogue value of Gain */
#define SRF08_DEFAULT_RANGE	6020	/* default value of Range in mm */

struct srf08_data {
	struct i2c_client	*client;
	int			sensitivity;		/* Gain */
	int			range_mm;		/* max. Range in mm */
	struct mutex		lock;
};

/*
 * in the documentation one can read about the "Gain" of the device
 * which is used here for amplifying the signal and filtering out unwanted
 * ones.
 * But with ADC's this term is already used differently and that's why it
 * is called "Sensitivity" here.
 */
static const int srf08_sensitivity[] = {
	 94,  97, 100, 103, 107, 110, 114, 118,
	123, 128, 133, 139, 145, 152, 159, 168,
	177, 187, 199, 212, 227, 245, 265, 288,
	317, 352, 395, 450, 524, 626, 777, 1025 };

static int srf08_read_ranging(struct srf08_data *data)
{
	struct i2c_client *client = data->client;
	int ret, i;
	int waittime;

	mutex_lock(&data->lock);

	ret = i2c_smbus_write_byte_data(data->client,
			SRF08_WRITE_COMMAND, SRF08_CMD_RANGING_CM);
	if (ret < 0) {
		dev_err(&client->dev, "write command - err: %d\n", ret);
		mutex_unlock(&data->lock);
		return ret;
	}

	/*
	 * we read here until a correct version number shows up as
	 * suggested by the documentation
	 *
	 * with an ultrasonic speed of 343 m/s and a roundtrip of it
	 * sleep the expected duration and try to read from the device
	 * if nothing useful is read try it in a shorter grid
	 *
	 * polling for not more than 20 ms should be enough
	 */
	waittime = 1 + data->range_mm / 172;
	msleep(waittime);
	for (i = 0; i < 4; i++) {
		ret = i2c_smbus_read_byte_data(data->client,
						SRF08_READ_SW_REVISION);

		/* check if a valid version number is read */
		if (ret < 255 && ret > 0)
			break;
		msleep(5);
	}

	if (ret >= 255 || ret <= 0) {
		dev_err(&client->dev, "device not ready\n");
		mutex_unlock(&data->lock);
		return -EIO;
	}

	ret = i2c_smbus_read_word_swapped(data->client,
						SRF08_READ_ECHO_1_HIGH);
	if (ret < 0) {
		dev_err(&client->dev, "cannot read distance: ret=%d\n", ret);
		mutex_unlock(&data->lock);
		return ret;
	}

	mutex_unlock(&data->lock);

	return ret;
}

static int srf08_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int *val,
			    int *val2, long mask)
{
	struct srf08_data *data = iio_priv(indio_dev);
	int ret;

	if (channel->type != IIO_DISTANCE)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = srf08_read_ranging(data);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* 1 LSB is 1 cm */
		*val = 0;
		*val2 = 10000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static ssize_t srf08_show_range_mm_available(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "[0.043 0.043 11.008]\n");
}

static IIO_DEVICE_ATTR(sensor_max_range_available, S_IRUGO,
				srf08_show_range_mm_available, NULL, 0);

static ssize_t srf08_show_range_mm(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct srf08_data *data = iio_priv(indio_dev);

	return sprintf(buf, "%d.%03d\n", data->range_mm / 1000,
						data->range_mm % 1000);
}

/*
 * set the range of the sensor to an even multiple of 43 mm
 * which corresponds to 1 LSB in the register
 *
 * register value    corresponding range
 *         0x00             43 mm
 *         0x01             86 mm
 *         0x02            129 mm
 *         ...
 *         0xFF          11008 mm
 */
static ssize_t srf08_write_range_mm(struct srf08_data *data, unsigned int val)
{
	int ret;
	struct i2c_client *client = data->client;
	unsigned int mod;
	u8 regval;

	ret = val / 43 - 1;
	mod = val % 43;

	if (mod || (ret < 0) || (ret > 255))
		return -EINVAL;

	regval = ret;

	mutex_lock(&data->lock);

	ret = i2c_smbus_write_byte_data(client, SRF08_WRITE_RANGE, regval);
	if (ret < 0) {
		dev_err(&client->dev, "write_range - err: %d\n", ret);
		mutex_unlock(&data->lock);
		return ret;
	}

	data->range_mm = val;

	mutex_unlock(&data->lock);

	return 0;
}

static ssize_t srf08_store_range_mm(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct srf08_data *data = iio_priv(indio_dev);
	int ret;
	int integer, fract;

	ret = iio_str_to_fixpoint(buf, 100, &integer, &fract);
	if (ret)
		return ret;

	ret = srf08_write_range_mm(data, integer * 1000 + fract);
	if (ret < 0)
		return ret;

	return len;
}

static IIO_DEVICE_ATTR(sensor_max_range, S_IRUGO | S_IWUSR,
			srf08_show_range_mm, srf08_store_range_mm, 0);

static ssize_t srf08_show_sensitivity_available(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(srf08_sensitivity); i++)
		len += sprintf(buf + len, "%d ", srf08_sensitivity[i]);

	len += sprintf(buf + len, "\n");

	return len;
}

static IIO_DEVICE_ATTR(sensor_sensitivity_available, S_IRUGO,
				srf08_show_sensitivity_available, NULL, 0);

static ssize_t srf08_show_sensitivity(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct srf08_data *data = iio_priv(indio_dev);
	int len;

	len = sprintf(buf, "%d\n", data->sensitivity);

	return len;
}

static ssize_t srf08_write_sensitivity(struct srf08_data *data,
							unsigned int val)
{
	struct i2c_client *client = data->client;
	int ret, i;
	u8 regval;

	for (i = 0; i < ARRAY_SIZE(srf08_sensitivity); i++)
		if (val == srf08_sensitivity[i]) {
			regval = i;
			break;
		}

	if (i >= ARRAY_SIZE(srf08_sensitivity))
		return -EINVAL;

	mutex_lock(&data->lock);

	ret = i2c_smbus_write_byte_data(client,
						SRF08_WRITE_MAX_GAIN, regval);
	if (ret < 0) {
		dev_err(&client->dev, "write_sensitivity - err: %d\n", ret);
		mutex_unlock(&data->lock);
		return ret;
	}

	data->sensitivity = val;

	mutex_unlock(&data->lock);

	return 0;
}

static ssize_t srf08_store_sensitivity(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct srf08_data *data = iio_priv(indio_dev);
	int ret;
	unsigned int val;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	ret = srf08_write_sensitivity(data, val);
	if (ret < 0)
		return ret;

	return len;
}

static IIO_DEVICE_ATTR(sensor_sensitivity, S_IRUGO | S_IWUSR,
			srf08_show_sensitivity, srf08_store_sensitivity, 0);

static struct attribute *srf08_attributes[] = {
	&iio_dev_attr_sensor_max_range.dev_attr.attr,
	&iio_dev_attr_sensor_max_range_available.dev_attr.attr,
	&iio_dev_attr_sensor_sensitivity.dev_attr.attr,
	&iio_dev_attr_sensor_sensitivity_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group srf08_attribute_group = {
	.attrs = srf08_attributes,
};

static const struct iio_chan_spec srf08_channels[] = {
	{
		.type = IIO_DISTANCE,
		.info_mask_separate =
				BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct iio_info srf08_info = {
	.read_raw = srf08_read_raw,
	.attrs = &srf08_attribute_group,
	.driver_module = THIS_MODULE,
};

static int srf08_probe(struct i2c_client *client,
					 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct srf08_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_READ_BYTE_DATA |
					I2C_FUNC_SMBUS_WRITE_BYTE_DATA |
					I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->name = "srf08";
	indio_dev->dev.parent = &client->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &srf08_info;
	indio_dev->channels = srf08_channels;
	indio_dev->num_channels = ARRAY_SIZE(srf08_channels);

	mutex_init(&data->lock);

	/*
	 * set default values of device here
	 * these register values cannot be read from the hardware
	 * therefore set driver specific default values
	 */
	ret = srf08_write_range_mm(data, SRF08_DEFAULT_RANGE);
	if (ret < 0)
		return ret;

	ret = srf08_write_sensitivity(data, SRF08_DEFAULT_GAIN);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id srf08_id[] = {
	{ "srf08", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, srf08_id);

static struct i2c_driver srf08_driver = {
	.driver = {
		.name	= "srf08",
	},
	.probe = srf08_probe,
	.id_table = srf08_id,
};
module_i2c_driver(srf08_driver);

MODULE_AUTHOR("Andreas Klinger <ak@it-klinger.de>");
MODULE_DESCRIPTION("Devantech SRF08 ultrasonic ranger driver");
MODULE_LICENSE("GPL");
