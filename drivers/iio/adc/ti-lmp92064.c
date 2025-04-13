// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments LMP92064 SPI ADC driver
 *
 * Copyright (c) 2022 Leonard Göhrs <kernel@pengutronix.de>, Pengutronix
 *
 * Based on linux/drivers/iio/adc/ti-tsc2046.c
 * Copyright (c) 2021 Oleksij Rempel <kernel@pengutronix.de>, Pengutronix
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/driver.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define TI_LMP92064_REG_CONFIG_A 0x0000
#define TI_LMP92064_REG_CONFIG_B 0x0001
#define TI_LMP92064_REG_CHIP_REV 0x0006

#define TI_LMP92064_REG_MFR_ID1 0x000C
#define TI_LMP92064_REG_MFR_ID2 0x000D

#define TI_LMP92064_REG_REG_UPDATE 0x000F
#define TI_LMP92064_REG_CONFIG_REG 0x0100
#define TI_LMP92064_REG_STATUS 0x0103

#define TI_LMP92064_REG_DATA_VOUT_LSB 0x0200
#define TI_LMP92064_REG_DATA_VOUT_MSB 0x0201
#define TI_LMP92064_REG_DATA_COUT_LSB 0x0202
#define TI_LMP92064_REG_DATA_COUT_MSB 0x0203

#define TI_LMP92064_VAL_CONFIG_A 0x99
#define TI_LMP92064_VAL_CONFIG_B 0x00
#define TI_LMP92064_VAL_STATUS_OK 0x01

/*
 * Channel number definitions for the two channels of the device
 * - IN Current (INC)
 * - IN Voltage (INV)
 */
#define TI_LMP92064_CHAN_INC 0
#define TI_LMP92064_CHAN_INV 1

static const struct regmap_range lmp92064_readable_reg_ranges[] = {
	regmap_reg_range(TI_LMP92064_REG_CONFIG_A, TI_LMP92064_REG_CHIP_REV),
	regmap_reg_range(TI_LMP92064_REG_MFR_ID1, TI_LMP92064_REG_MFR_ID2),
	regmap_reg_range(TI_LMP92064_REG_REG_UPDATE, TI_LMP92064_REG_REG_UPDATE),
	regmap_reg_range(TI_LMP92064_REG_CONFIG_REG, TI_LMP92064_REG_CONFIG_REG),
	regmap_reg_range(TI_LMP92064_REG_STATUS, TI_LMP92064_REG_STATUS),
	regmap_reg_range(TI_LMP92064_REG_DATA_VOUT_LSB, TI_LMP92064_REG_DATA_COUT_MSB),
};

static const struct regmap_access_table lmp92064_readable_regs = {
	.yes_ranges = lmp92064_readable_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(lmp92064_readable_reg_ranges),
};

static const struct regmap_range lmp92064_writable_reg_ranges[] = {
	regmap_reg_range(TI_LMP92064_REG_CONFIG_A, TI_LMP92064_REG_CONFIG_B),
	regmap_reg_range(TI_LMP92064_REG_REG_UPDATE, TI_LMP92064_REG_REG_UPDATE),
	regmap_reg_range(TI_LMP92064_REG_CONFIG_REG, TI_LMP92064_REG_CONFIG_REG),
};

static const struct regmap_access_table lmp92064_writable_regs = {
	.yes_ranges = lmp92064_writable_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(lmp92064_writable_reg_ranges),
};

static const struct regmap_config lmp92064_spi_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = TI_LMP92064_REG_DATA_COUT_MSB,
	.rd_table = &lmp92064_readable_regs,
	.wr_table = &lmp92064_writable_regs,
};

struct lmp92064_adc_priv {
	int shunt_resistor_uohm;
	struct spi_device *spi;
	struct regmap *regmap;
};

static const struct iio_chan_spec lmp92064_adc_channels[] = {
	{
		.type = IIO_CURRENT,
		.address = TI_LMP92064_CHAN_INC,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = TI_LMP92064_CHAN_INC,
		.scan_type = {
			.sign = 'u',
			.realbits = 12,
			.storagebits = 16,
		},
		.datasheet_name = "INC",
	},
	{
		.type = IIO_VOLTAGE,
		.address = TI_LMP92064_CHAN_INV,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = TI_LMP92064_CHAN_INV,
		.scan_type = {
			.sign = 'u',
			.realbits = 12,
			.storagebits = 16,
		},
		.datasheet_name = "INV",
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static const unsigned long lmp92064_scan_masks[] = {
	BIT(TI_LMP92064_CHAN_INC) | BIT(TI_LMP92064_CHAN_INV),
	0
};

static int lmp92064_read_meas(struct lmp92064_adc_priv *priv, u16 *res)
{
	__be16 raw[2];
	int ret;

	/*
	 * The ADC only latches in new samples if all DATA registers are read
	 * in descending sequential order.
	 * The ADC auto-decrements the register index with each clocked byte.
	 * Read both channels in single SPI transfer by selecting the highest
	 * register using the command below and clocking out all four data
	 * bytes.
	 */

	ret = regmap_bulk_read(priv->regmap, TI_LMP92064_REG_DATA_COUT_MSB,
			 &raw, sizeof(raw));

	if (ret) {
		dev_err(&priv->spi->dev, "regmap_bulk_read failed: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	res[0] = be16_to_cpu(raw[0]);
	res[1] = be16_to_cpu(raw[1]);

	return 0;
}

static int lmp92064_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	struct lmp92064_adc_priv *priv = iio_priv(indio_dev);
	u16 raw[2];
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = lmp92064_read_meas(priv, raw);
		if (ret < 0)
			return ret;

		*val = (chan->address == TI_LMP92064_CHAN_INC) ? raw[0] : raw[1];

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->address == TI_LMP92064_CHAN_INC) {
			/*
			 * processed (mA) = raw * current_lsb (mA)
			 * current_lsb (mA) = shunt_voltage_lsb (nV) / shunt_resistor (uOhm)
			 * shunt_voltage_lsb (nV) = 81920000 / 4096 = 20000
			 */
			*val = 20000;
			*val2 = priv->shunt_resistor_uohm;
		} else {
			/*
			 * processed (mV) = raw * voltage_lsb (mV)
			 * voltage_lsb (mV) = 2048 / 4096
			 */
			*val = 2048;
			*val2 = 4096;
		}
		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;
	}
}

static irqreturn_t lmp92064_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct lmp92064_adc_priv *priv = iio_priv(indio_dev);
	struct {
		u16 values[2];
		aligned_s64 timestamp;
	} data;
	int ret;

	memset(&data, 0, sizeof(data));

	ret = lmp92064_read_meas(priv, data.values);
	if (ret)
		goto err;

	iio_push_to_buffers_with_ts(indio_dev, &data, sizeof(data),
				    iio_get_time_ns(indio_dev));

err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int lmp92064_reset(struct lmp92064_adc_priv *priv,
			  struct gpio_desc *gpio_reset)
{
	unsigned int status;
	int ret, i;

	if (gpio_reset) {
		/*
		 * Perform a hard reset if gpio_reset is available.
		 * The datasheet specifies a very low 3.5ns reset pulse duration and does not
		 * specify how long to wait after a reset to access the device.
		 * Use more conservative pulse lengths to allow analog RC filtering of the
		 * reset line at the board level (as recommended in the datasheet).
		 */
		gpiod_set_value_cansleep(gpio_reset, 1);
		usleep_range(1, 10);
		gpiod_set_value_cansleep(gpio_reset, 0);
		usleep_range(500, 750);
	} else {
		/*
		 * Perform a soft-reset if not.
		 * Also write default values to the config registers that are not
		 * affected by soft reset.
		 */
		ret = regmap_write(priv->regmap, TI_LMP92064_REG_CONFIG_A,
				   TI_LMP92064_VAL_CONFIG_A);
		if (ret < 0)
			return ret;

		ret = regmap_write(priv->regmap, TI_LMP92064_REG_CONFIG_B,
				   TI_LMP92064_VAL_CONFIG_B);
		if (ret < 0)
			return ret;
	}

	/*
	 * Wait for the device to signal readiness to prevent reading bogus data
	 * and make sure device is actually connected.
	 * The datasheet does not specify how long this takes but usually it is
	 * not more than 3-4 iterations of this loop.
	 */
	for (i = 0; i < 10; i++) {
		ret = regmap_read(priv->regmap, TI_LMP92064_REG_STATUS, &status);
		if (ret < 0)
			return ret;

		if (status == TI_LMP92064_VAL_STATUS_OK)
			return 0;

		usleep_range(1000, 2000);
	}

	/*
	 * No (correct) response received.
	 * Device is mostly likely not connected to the bus.
	 */
	return -ENXIO;
}

static const struct iio_info lmp92064_adc_info = {
	.read_raw = lmp92064_read_raw,
};

static int lmp92064_adc_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct lmp92064_adc_priv *priv;
	struct gpio_desc *gpio_reset;
	struct iio_dev *indio_dev;
	u32 shunt_resistor_uohm;
	struct regmap *regmap;
	int ret;

	ret = spi_setup(spi);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Error in SPI setup\n");

	regmap = devm_regmap_init_spi(spi, &lmp92064_spi_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to set up SPI regmap\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);

	priv->spi = spi;
	priv->regmap = regmap;

	ret = device_property_read_u32(dev, "shunt-resistor-micro-ohms",
				       &shunt_resistor_uohm);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to get shunt-resistor value\n");

	/*
	 * The shunt resistance is passed to userspace as the denominator of an iio
	 * fraction. Make sure it is in range for that.
	 */
	if (shunt_resistor_uohm == 0 || shunt_resistor_uohm > INT_MAX) {
		dev_err(dev, "Shunt resistance is out of range\n");
		return -EINVAL;
	}

	priv->shunt_resistor_uohm = shunt_resistor_uohm;

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return ret;

	ret = devm_regulator_get_enable(dev, "vdig");
	if (ret)
		return ret;

	gpio_reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio_reset))
		return dev_err_probe(dev, PTR_ERR(gpio_reset),
				     "Failed to get GPIO reset pin\n");

	ret = lmp92064_reset(priv, gpio_reset);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to reset device\n");

	indio_dev->name = "lmp92064";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = lmp92064_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(lmp92064_adc_channels);
	indio_dev->info = &lmp92064_adc_info;
	indio_dev->available_scan_masks = lmp92064_scan_masks;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      lmp92064_trigger_handler, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to setup buffered read\n");

	return devm_iio_device_register(dev, indio_dev);
}

static const struct spi_device_id lmp92064_id_table[] = {
	{ "lmp92064" },
	{ }
};
MODULE_DEVICE_TABLE(spi, lmp92064_id_table);

static const struct of_device_id lmp92064_of_table[] = {
	{ .compatible = "ti,lmp92064" },
	{ }
};
MODULE_DEVICE_TABLE(of, lmp92064_of_table);

static struct spi_driver lmp92064_adc_driver = {
	.driver = {
		.name = "lmp92064",
		.of_match_table = lmp92064_of_table,
	},
	.probe = lmp92064_adc_probe,
	.id_table = lmp92064_id_table,
};
module_spi_driver(lmp92064_adc_driver);

MODULE_AUTHOR("Leonard Göhrs <kernel@pengutronix.de>");
MODULE_DESCRIPTION("TI LMP92064 ADC");
MODULE_LICENSE("GPL");
