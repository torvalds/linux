// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAX1241 low-power, 12-bit serial ADC
 *
 * Datasheet: https://datasheets.maximintegrated.com/en/ds/MAX1240-MAX1241.pdf
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#define MAX1241_VAL_MASK GENMASK(11, 0)
#define MAX1241_SHUTDOWN_DELAY_USEC 4

enum max1241_id {
	max1241,
};

struct max1241 {
	struct spi_device *spi;
	struct mutex lock;
	struct regulator *vref;
	struct gpio_desc *shutdown;

	__be16 data __aligned(IIO_DMA_MINALIGN);
};

static const struct iio_chan_spec max1241_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_SCALE),
	},
};

static int max1241_read(struct max1241 *adc)
{
	struct spi_transfer xfers[] = {
		/*
		 * Begin conversion by bringing /CS low for at least
		 * tconv us.
		 */
		{
			.len = 0,
			.delay.value = 8,
			.delay.unit = SPI_DELAY_UNIT_USECS,
		},
		/*
		 * Then read two bytes of data in our RX buffer.
		 */
		{
			.rx_buf = &adc->data,
			.len = 2,
		},
	};

	return spi_sync_transfer(adc->spi, xfers, ARRAY_SIZE(xfers));
}

static int max1241_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	int ret, vref_uV;
	struct max1241 *adc = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&adc->lock);

		if (adc->shutdown) {
			gpiod_set_value(adc->shutdown, 0);
			udelay(MAX1241_SHUTDOWN_DELAY_USEC);
			ret = max1241_read(adc);
			gpiod_set_value(adc->shutdown, 1);
		} else
			ret = max1241_read(adc);

		if (ret) {
			mutex_unlock(&adc->lock);
			return ret;
		}

		*val = (be16_to_cpu(adc->data) >> 3) & MAX1241_VAL_MASK;

		mutex_unlock(&adc->lock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		vref_uV = regulator_get_voltage(adc->vref);

		if (vref_uV < 0)
			return vref_uV;

		*val = vref_uV / 1000;
		*val2 = 12;

		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static const struct iio_info max1241_info = {
	.read_raw = max1241_read_raw,
};

static void max1241_disable_vref_action(void *data)
{
	struct max1241 *adc = data;
	struct device *dev = &adc->spi->dev;
	int err;

	err = regulator_disable(adc->vref);
	if (err)
		dev_err(dev, "could not disable vref regulator.\n");
}

static int max1241_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct max1241 *adc;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;
	mutex_init(&adc->lock);

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get/enable vdd regulator\n");

	adc->vref = devm_regulator_get(dev, "vref");
	if (IS_ERR(adc->vref))
		return dev_err_probe(dev, PTR_ERR(adc->vref),
				     "failed to get vref regulator\n");

	ret = regulator_enable(adc->vref);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, max1241_disable_vref_action, adc);
	if (ret) {
		dev_err(dev, "could not set up vref regulator cleanup action\n");
		return ret;
	}

	adc->shutdown = devm_gpiod_get_optional(dev, "shutdown",
						GPIOD_OUT_HIGH);
	if (IS_ERR(adc->shutdown))
		return dev_err_probe(dev, PTR_ERR(adc->shutdown),
				     "cannot get shutdown gpio\n");

	if (adc->shutdown)
		dev_dbg(dev, "shutdown pin passed, low-power mode enabled");
	else
		dev_dbg(dev, "no shutdown pin passed, low-power mode disabled");

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &max1241_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = max1241_channels;
	indio_dev->num_channels = ARRAY_SIZE(max1241_channels);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct spi_device_id max1241_id[] = {
	{ "max1241", max1241 },
	{}
};

static const struct of_device_id max1241_dt_ids[] = {
	{ .compatible = "maxim,max1241" },
	{}
};
MODULE_DEVICE_TABLE(of, max1241_dt_ids);

static struct spi_driver max1241_spi_driver = {
	.driver = {
		.name = "max1241",
		.of_match_table = max1241_dt_ids,
	},
	.probe = max1241_probe,
	.id_table = max1241_id,
};
module_spi_driver(max1241_spi_driver);

MODULE_AUTHOR("Alexandru Lazar <alazar@startmail.com>");
MODULE_DESCRIPTION("MAX1241 ADC driver");
MODULE_LICENSE("GPL v2");
