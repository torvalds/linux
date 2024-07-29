// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI ADC108S102 SPI ADC driver
 *
 * Copyright (c) 2013-2015 Intel Corporation.
 * Copyright (c) 2017 Siemens AG
 *
 * This IIO device driver is designed to work with the following
 * analog to digital converters from Texas Instruments:
 *  ADC108S102
 *  ADC128S102
 * The communication with ADC chip is via the SPI bus (mode 3).
 */

#include <linux/acpi.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/types.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

/*
 * In case of ACPI, we use the hard-wired 5000 mV of the Galileo and IOT2000
 * boards as default for the reference pin VA. Device tree users encode that
 * via the vref-supply regulator.
 */
#define ADC108S102_VA_MV_ACPI_DEFAULT	5000

/*
 * Defining the ADC resolution being 12 bits, we can use the same driver for
 * both ADC108S102 (10 bits resolution) and ADC128S102 (12 bits resolution)
 * chips. The ADC108S102 effectively returns a 12-bit result with the 2
 * least-significant bits unset.
 */
#define ADC108S102_BITS		12
#define ADC108S102_MAX_CHANNELS	8

/*
 * 16-bit SPI command format:
 *   [15:14] Ignored
 *   [13:11] 3-bit channel address
 *   [10:0]  Ignored
 */
#define ADC108S102_CMD(ch)		((u16)(ch) << 11)

/*
 * 16-bit SPI response format:
 *   [15:12] Zeros
 *   [11:0]  12-bit ADC sample (for ADC108S102, [1:0] will always be 0).
 */
#define ADC108S102_RES_DATA(res)	((u16)res & GENMASK(11, 0))

struct adc108s102_state {
	struct spi_device		*spi;
	u32				va_millivolt;
	/* SPI transfer used by triggered buffer handler*/
	struct spi_transfer		ring_xfer;
	/* SPI transfer used by direct scan */
	struct spi_transfer		scan_single_xfer;
	/* SPI message used by ring_xfer SPI transfer */
	struct spi_message		ring_msg;
	/* SPI message used by scan_single_xfer SPI transfer */
	struct spi_message		scan_single_msg;

	/*
	 * SPI message buffers:
	 *  tx_buf: |C0|C1|C2|C3|C4|C5|C6|C7|XX|
	 *  rx_buf: |XX|R0|R1|R2|R3|R4|R5|R6|R7|tt|tt|tt|tt|
	 *
	 *  tx_buf: 8 channel read commands, plus 1 dummy command
	 *  rx_buf: 1 dummy response, 8 channel responses
	 */
	__be16				rx_buf[9] __aligned(IIO_DMA_MINALIGN);
	__be16				tx_buf[9] __aligned(IIO_DMA_MINALIGN);
};

#define ADC108S102_V_CHAN(index)					\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = index,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			BIT(IIO_CHAN_INFO_SCALE),			\
		.address = index,					\
		.scan_index = index,					\
		.scan_type = {						\
			.sign = 'u',					\
			.realbits = ADC108S102_BITS,			\
			.storagebits = 16,				\
			.endianness = IIO_BE,				\
		},							\
	}

static const struct iio_chan_spec adc108s102_channels[] = {
	ADC108S102_V_CHAN(0),
	ADC108S102_V_CHAN(1),
	ADC108S102_V_CHAN(2),
	ADC108S102_V_CHAN(3),
	ADC108S102_V_CHAN(4),
	ADC108S102_V_CHAN(5),
	ADC108S102_V_CHAN(6),
	ADC108S102_V_CHAN(7),
	IIO_CHAN_SOFT_TIMESTAMP(8),
};

static int adc108s102_update_scan_mode(struct iio_dev *indio_dev,
		unsigned long const *active_scan_mask)
{
	struct adc108s102_state *st = iio_priv(indio_dev);
	unsigned int bit, cmds;

	/*
	 * Fill in the first x shorts of tx_buf with the number of channels
	 * enabled for sampling by the triggered buffer.
	 */
	cmds = 0;
	for_each_set_bit(bit, active_scan_mask, ADC108S102_MAX_CHANNELS)
		st->tx_buf[cmds++] = cpu_to_be16(ADC108S102_CMD(bit));

	/* One dummy command added, to clock in the last response */
	st->tx_buf[cmds++] = 0x00;

	/* build SPI ring message */
	st->ring_xfer.tx_buf = &st->tx_buf[0];
	st->ring_xfer.rx_buf = &st->rx_buf[0];
	st->ring_xfer.len = cmds * sizeof(st->tx_buf[0]);

	spi_message_init_with_transfers(&st->ring_msg, &st->ring_xfer, 1);

	return 0;
}

static irqreturn_t adc108s102_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adc108s102_state *st = iio_priv(indio_dev);
	int ret;

	ret = spi_sync(st->spi, &st->ring_msg);
	if (ret < 0)
		goto out_notify;

	/* Skip the dummy response in the first slot */
	iio_push_to_buffers_with_ts_unaligned(indio_dev,
					      &st->rx_buf[1],
					      st->ring_xfer.len - sizeof(st->rx_buf[1]),
					      iio_get_time_ns(indio_dev));

out_notify:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int adc108s102_scan_direct(struct adc108s102_state *st, unsigned int ch)
{
	int ret;

	st->tx_buf[0] = cpu_to_be16(ADC108S102_CMD(ch));
	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	/* Skip the dummy response in the first slot */
	return be16_to_cpu(st->rx_buf[1]);
}

static int adc108s102_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long m)
{
	struct adc108s102_state *st = iio_priv(indio_dev);
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = adc108s102_scan_direct(st, chan->address);

		iio_device_release_direct_mode(indio_dev);

		if (ret < 0)
			return ret;

		*val = ADC108S102_RES_DATA(ret);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type != IIO_VOLTAGE)
			break;

		*val = st->va_millivolt;
		*val2 = chan->scan_type.realbits;

		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_info adc108s102_info = {
	.read_raw		= &adc108s102_read_raw,
	.update_scan_mode	= &adc108s102_update_scan_mode,
};

static int adc108s102_probe(struct spi_device *spi)
{
	struct adc108s102_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	if (ACPI_COMPANION(&spi->dev)) {
		st->va_millivolt = ADC108S102_VA_MV_ACPI_DEFAULT;
	} else {
		ret = devm_regulator_get_enable_read_voltage(&spi->dev, "vref");
		if (ret < 0)
			return dev_err_probe(&spi->dev, ret, "failed get vref voltage\n");

		st->va_millivolt = ret / 1000;
	}

	st->spi = spi;

	indio_dev->name = spi->modalias;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adc108s102_channels;
	indio_dev->num_channels = ARRAY_SIZE(adc108s102_channels);
	indio_dev->info = &adc108s102_info;

	/* Setup default message */
	st->scan_single_xfer.tx_buf = st->tx_buf;
	st->scan_single_xfer.rx_buf = st->rx_buf;
	st->scan_single_xfer.len = 2 * sizeof(st->tx_buf[0]);

	spi_message_init_with_transfers(&st->scan_single_msg,
					&st->scan_single_xfer, 1);

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev, NULL,
					      &adc108s102_trigger_handler,
					      NULL);
	if (ret)
		return ret;

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret)
		dev_err(&spi->dev, "Failed to register IIO device\n");
	return ret;
}

static const struct of_device_id adc108s102_of_match[] = {
	{ .compatible = "ti,adc108s102" },
	{ }
};
MODULE_DEVICE_TABLE(of, adc108s102_of_match);

static const struct acpi_device_id adc108s102_acpi_ids[] = {
	{ "INT3495", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, adc108s102_acpi_ids);

static const struct spi_device_id adc108s102_id[] = {
	{ "adc108s102", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, adc108s102_id);

static struct spi_driver adc108s102_driver = {
	.driver = {
		.name   = "adc108s102",
		.of_match_table = adc108s102_of_match,
		.acpi_match_table = adc108s102_acpi_ids,
	},
	.probe		= adc108s102_probe,
	.id_table	= adc108s102_id,
};
module_spi_driver(adc108s102_driver);

MODULE_AUTHOR("Bogdan Pricop <bogdan.pricop@emutex.com>");
MODULE_DESCRIPTION("Texas Instruments ADC108S102 and ADC128S102 driver");
MODULE_LICENSE("GPL v2");
