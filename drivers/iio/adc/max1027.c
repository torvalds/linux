// SPDX-License-Identifier: GPL-2.0-only
 /*
  * iio/adc/max1027.c
  * Copyright (C) 2014 Philippe Reynes
  *
  * based on linux/drivers/iio/ad7923.c
  * Copyright 2011 Analog Devices Inc (from AD7923 Driver)
  * Copyright 2012 CS Systemes d'Information
  *
  * max1027.c
  *
  * Partial support for max1027 and similar chips.
  */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define MAX1027_CONV_REG  BIT(7)
#define MAX1027_SETUP_REG BIT(6)
#define MAX1027_AVG_REG   BIT(5)
#define MAX1027_RST_REG   BIT(4)

/* conversion register */
#define MAX1027_TEMP      BIT(0)
#define MAX1027_SCAN_0_N  (0x00 << 1)
#define MAX1027_SCAN_N_M  (0x01 << 1)
#define MAX1027_SCAN_N    (0x02 << 1)
#define MAX1027_NOSCAN    (0x03 << 1)
#define MAX1027_CHAN(n)   ((n) << 3)

/* setup register */
#define MAX1027_UNIPOLAR  0x02
#define MAX1027_BIPOLAR   0x03
#define MAX1027_REF_MODE0 (0x00 << 2)
#define MAX1027_REF_MODE1 (0x01 << 2)
#define MAX1027_REF_MODE2 (0x02 << 2)
#define MAX1027_REF_MODE3 (0x03 << 2)
#define MAX1027_CKS_MODE0 (0x00 << 4)
#define MAX1027_CKS_MODE1 (0x01 << 4)
#define MAX1027_CKS_MODE2 (0x02 << 4)
#define MAX1027_CKS_MODE3 (0x03 << 4)

/* averaging register */
#define MAX1027_NSCAN_4   0x00
#define MAX1027_NSCAN_8   0x01
#define MAX1027_NSCAN_12  0x02
#define MAX1027_NSCAN_16  0x03
#define MAX1027_NAVG_4    (0x00 << 2)
#define MAX1027_NAVG_8    (0x01 << 2)
#define MAX1027_NAVG_16   (0x02 << 2)
#define MAX1027_NAVG_32   (0x03 << 2)
#define MAX1027_AVG_EN    BIT(4)

enum max1027_id {
	max1027,
	max1029,
	max1031,
};

static const struct spi_device_id max1027_id[] = {
	{"max1027", max1027},
	{"max1029", max1029},
	{"max1031", max1031},
	{}
};
MODULE_DEVICE_TABLE(spi, max1027_id);

#ifdef CONFIG_OF
static const struct of_device_id max1027_adc_dt_ids[] = {
	{ .compatible = "maxim,max1027" },
	{ .compatible = "maxim,max1029" },
	{ .compatible = "maxim,max1031" },
	{},
};
MODULE_DEVICE_TABLE(of, max1027_adc_dt_ids);
#endif

#define MAX1027_V_CHAN(index)						\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = index,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.scan_index = index + 1,				\
		.scan_type = {						\
			.sign = 'u',					\
			.realbits = 10,					\
			.storagebits = 16,				\
			.shift = 2,					\
			.endianness = IIO_BE,				\
		},							\
	}

#define MAX1027_T_CHAN							\
	{								\
		.type = IIO_TEMP,					\
		.channel = 0,						\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.scan_index = 0,					\
		.scan_type = {						\
			.sign = 'u',					\
			.realbits = 12,					\
			.storagebits = 16,				\
			.endianness = IIO_BE,				\
		},							\
	}

static const struct iio_chan_spec max1027_channels[] = {
	MAX1027_T_CHAN,
	MAX1027_V_CHAN(0),
	MAX1027_V_CHAN(1),
	MAX1027_V_CHAN(2),
	MAX1027_V_CHAN(3),
	MAX1027_V_CHAN(4),
	MAX1027_V_CHAN(5),
	MAX1027_V_CHAN(6),
	MAX1027_V_CHAN(7)
};

static const struct iio_chan_spec max1029_channels[] = {
	MAX1027_T_CHAN,
	MAX1027_V_CHAN(0),
	MAX1027_V_CHAN(1),
	MAX1027_V_CHAN(2),
	MAX1027_V_CHAN(3),
	MAX1027_V_CHAN(4),
	MAX1027_V_CHAN(5),
	MAX1027_V_CHAN(6),
	MAX1027_V_CHAN(7),
	MAX1027_V_CHAN(8),
	MAX1027_V_CHAN(9),
	MAX1027_V_CHAN(10),
	MAX1027_V_CHAN(11)
};

static const struct iio_chan_spec max1031_channels[] = {
	MAX1027_T_CHAN,
	MAX1027_V_CHAN(0),
	MAX1027_V_CHAN(1),
	MAX1027_V_CHAN(2),
	MAX1027_V_CHAN(3),
	MAX1027_V_CHAN(4),
	MAX1027_V_CHAN(5),
	MAX1027_V_CHAN(6),
	MAX1027_V_CHAN(7),
	MAX1027_V_CHAN(8),
	MAX1027_V_CHAN(9),
	MAX1027_V_CHAN(10),
	MAX1027_V_CHAN(11),
	MAX1027_V_CHAN(12),
	MAX1027_V_CHAN(13),
	MAX1027_V_CHAN(14),
	MAX1027_V_CHAN(15)
};

static const unsigned long max1027_available_scan_masks[] = {
	0x000001ff,
	0x00000000,
};

static const unsigned long max1029_available_scan_masks[] = {
	0x00001fff,
	0x00000000,
};

static const unsigned long max1031_available_scan_masks[] = {
	0x0001ffff,
	0x00000000,
};

struct max1027_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	const unsigned long *available_scan_masks;
};

static const struct max1027_chip_info max1027_chip_info_tbl[] = {
	[max1027] = {
		.channels = max1027_channels,
		.num_channels = ARRAY_SIZE(max1027_channels),
		.available_scan_masks = max1027_available_scan_masks,
	},
	[max1029] = {
		.channels = max1029_channels,
		.num_channels = ARRAY_SIZE(max1029_channels),
		.available_scan_masks = max1029_available_scan_masks,
	},
	[max1031] = {
		.channels = max1031_channels,
		.num_channels = ARRAY_SIZE(max1031_channels),
		.available_scan_masks = max1031_available_scan_masks,
	},
};

struct max1027_state {
	const struct max1027_chip_info	*info;
	struct spi_device		*spi;
	struct iio_trigger		*trig;
	__be16				*buffer;
	struct mutex			lock;

	u8				reg ____cacheline_aligned;
};

static int max1027_read_single_value(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     int *val)
{
	int ret;
	struct max1027_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev)) {
		dev_warn(&indio_dev->dev, "trigger mode already enabled");
		return -EBUSY;
	}

	/* Start acquisition on conversion register write */
	st->reg = MAX1027_SETUP_REG | MAX1027_REF_MODE2 | MAX1027_CKS_MODE2;
	ret = spi_write(st->spi, &st->reg, 1);
	if (ret < 0) {
		dev_err(&indio_dev->dev,
			"Failed to configure setup register\n");
		return ret;
	}

	/* Configure conversion register with the requested chan */
	st->reg = MAX1027_CONV_REG | MAX1027_CHAN(chan->channel) |
		  MAX1027_NOSCAN;
	if (chan->type == IIO_TEMP)
		st->reg |= MAX1027_TEMP;
	ret = spi_write(st->spi, &st->reg, 1);
	if (ret < 0) {
		dev_err(&indio_dev->dev,
			"Failed to configure conversion register\n");
		return ret;
	}

	/*
	 * For an unknown reason, when we use the mode "10" (write
	 * conversion register), the interrupt doesn't occur every time.
	 * So we just wait 1 ms.
	 */
	mdelay(1);

	/* Read result */
	ret = spi_read(st->spi, st->buffer, (chan->type == IIO_TEMP) ? 4 : 2);
	if (ret < 0)
		return ret;

	*val = be16_to_cpu(st->buffer[0]);

	return IIO_VAL_INT;
}

static int max1027_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	int ret = 0;
	struct max1027_state *st = iio_priv(indio_dev);

	mutex_lock(&st->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = max1027_read_single_value(indio_dev, chan, val);
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_TEMP:
			*val = 1;
			*val2 = 8;
			ret = IIO_VAL_FRACTIONAL;
			break;
		case IIO_VOLTAGE:
			*val = 2500;
			*val2 = 10;
			ret = IIO_VAL_FRACTIONAL_LOG2;
			break;
		default:
			ret = -EINVAL;
			break;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&st->lock);

	return ret;
}

static int max1027_debugfs_reg_access(struct iio_dev *indio_dev,
				      unsigned reg, unsigned writeval,
				      unsigned *readval)
{
	struct max1027_state *st = iio_priv(indio_dev);
	u8 *val = (u8 *)st->buffer;

	if (readval != NULL)
		return -EINVAL;

	*val = (u8)writeval;
	return spi_write(st->spi, val, 1);
}

static int max1027_validate_trigger(struct iio_dev *indio_dev,
				    struct iio_trigger *trig)
{
	struct max1027_state *st = iio_priv(indio_dev);

	if (st->trig != trig)
		return -EINVAL;

	return 0;
}

static int max1027_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct max1027_state *st = iio_priv(indio_dev);
	int ret;

	if (state) {
		/* Start acquisition on cnvst */
		st->reg = MAX1027_SETUP_REG | MAX1027_CKS_MODE0 |
			  MAX1027_REF_MODE2;
		ret = spi_write(st->spi, &st->reg, 1);
		if (ret < 0)
			return ret;

		/* Scan from 0 to max */
		st->reg = MAX1027_CONV_REG | MAX1027_CHAN(0) |
			  MAX1027_SCAN_N_M | MAX1027_TEMP;
		ret = spi_write(st->spi, &st->reg, 1);
		if (ret < 0)
			return ret;
	} else {
		/* Start acquisition on conversion register write */
		st->reg = MAX1027_SETUP_REG | MAX1027_CKS_MODE2	|
			  MAX1027_REF_MODE2;
		ret = spi_write(st->spi, &st->reg, 1);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static irqreturn_t max1027_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct max1027_state *st = iio_priv(indio_dev);

	pr_debug("%s(irq=%d, private=0x%p)\n", __func__, irq, private);

	/* fill buffer with all channel */
	spi_read(st->spi, st->buffer, indio_dev->masklength * 2);

	iio_push_to_buffers(indio_dev, st->buffer);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_trigger_ops max1027_trigger_ops = {
	.validate_device = &iio_trigger_validate_own_device,
	.set_trigger_state = &max1027_set_trigger_state,
};

static const struct iio_info max1027_info = {
	.read_raw = &max1027_read_raw,
	.validate_trigger = &max1027_validate_trigger,
	.debugfs_reg_access = &max1027_debugfs_reg_access,
};

static int max1027_probe(struct spi_device *spi)
{
	int ret;
	struct iio_dev *indio_dev;
	struct max1027_state *st;

	pr_debug("%s: probe(spi = 0x%p)\n", __func__, spi);

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL) {
		pr_err("Can't allocate iio device\n");
		return -ENOMEM;
	}

	spi_set_drvdata(spi, indio_dev);

	st = iio_priv(indio_dev);
	st->spi = spi;
	st->info = &max1027_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	mutex_init(&st->lock);

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->dev.of_node = spi->dev.of_node;
	indio_dev->info = &max1027_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->info->channels;
	indio_dev->num_channels = st->info->num_channels;
	indio_dev->available_scan_masks = st->info->available_scan_masks;

	st->buffer = devm_kmalloc_array(&indio_dev->dev,
				  indio_dev->num_channels, 2,
				  GFP_KERNEL);
	if (st->buffer == NULL) {
		dev_err(&indio_dev->dev, "Can't allocate buffer\n");
		return -ENOMEM;
	}

	ret = iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
					 &max1027_trigger_handler, NULL);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Failed to setup buffer\n");
		return ret;
	}

	st->trig = devm_iio_trigger_alloc(&spi->dev, "%s-trigger",
							indio_dev->name);
	if (st->trig == NULL) {
		ret = -ENOMEM;
		dev_err(&indio_dev->dev, "Failed to allocate iio trigger\n");
		goto fail_trigger_alloc;
	}

	st->trig->ops = &max1027_trigger_ops;
	st->trig->dev.parent = &spi->dev;
	iio_trigger_set_drvdata(st->trig, indio_dev);
	iio_trigger_register(st->trig);

	ret = devm_request_threaded_irq(&spi->dev, spi->irq,
					iio_trigger_generic_data_rdy_poll,
					NULL,
					IRQF_TRIGGER_FALLING,
					spi->dev.driver->name, st->trig);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Failed to allocate IRQ.\n");
		goto fail_dev_register;
	}

	/* Disable averaging */
	st->reg = MAX1027_AVG_REG;
	ret = spi_write(st->spi, &st->reg, 1);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Failed to configure averaging register\n");
		goto fail_dev_register;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Failed to register iio device\n");
		goto fail_dev_register;
	}

	return 0;

fail_dev_register:
fail_trigger_alloc:
	iio_triggered_buffer_cleanup(indio_dev);

	return ret;
}

static int max1027_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	pr_debug("%s: remove(spi = 0x%p)\n", __func__, spi);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);

	return 0;
}

static struct spi_driver max1027_driver = {
	.driver = {
		.name	= "max1027",
		.of_match_table = of_match_ptr(max1027_adc_dt_ids),
	},
	.probe		= max1027_probe,
	.remove		= max1027_remove,
	.id_table	= max1027_id,
};
module_spi_driver(max1027_driver);

MODULE_AUTHOR("Philippe Reynes <tremyfr@yahoo.fr>");
MODULE_DESCRIPTION("MAX1027/MAX1029/MAX1031 ADC");
MODULE_LICENSE("GPL v2");
