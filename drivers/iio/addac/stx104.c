// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO driver for the Apex Embedded Systems STX104
 * Copyright (C) 2016 William Breathitt Gray
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/regmap.h>
#include <linux/i8254.h>
#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define STX104_OUT_CHAN(chan) {				\
	.type = IIO_VOLTAGE,				\
	.channel = chan,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.indexed = 1,					\
	.output = 1					\
}
#define STX104_IN_CHAN(chan, diff) {					\
	.type = IIO_VOLTAGE,						\
	.channel = chan,						\
	.channel2 = chan,						\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_HARDWAREGAIN) |	\
		BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_SCALE),	\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.indexed = 1,							\
	.differential = diff						\
}

#define STX104_NUM_OUT_CHAN 2

#define STX104_EXTENT 16

static unsigned int base[max_num_isa_dev(STX104_EXTENT)];
static unsigned int num_stx104;
module_param_hw_array(base, uint, ioport, &num_stx104, 0);
MODULE_PARM_DESC(base, "Apex Embedded Systems STX104 base addresses");

#define STX104_AIO_BASE 0x0
#define STX104_SOFTWARE_STROBE STX104_AIO_BASE
#define STX104_ADC_DATA STX104_AIO_BASE
#define STX104_ADC_CHANNEL (STX104_AIO_BASE + 0x2)
#define STX104_DIO_REG (STX104_AIO_BASE + 0x3)
#define STX104_DAC_BASE (STX104_AIO_BASE + 0x4)
#define STX104_ADC_STATUS (STX104_AIO_BASE + 0x8)
#define STX104_ADC_CONTROL (STX104_AIO_BASE + 0x9)
#define STX104_ADC_CONFIGURATION (STX104_AIO_BASE + 0x11)
#define STX104_I8254_BASE (STX104_AIO_BASE + 0x12)

#define STX104_AIO_DATA_STRIDE 2
#define STX104_DAC_OFFSET(_channel) (STX104_DAC_BASE + STX104_AIO_DATA_STRIDE * (_channel))

/* ADC Channel */
#define STX104_FC GENMASK(3, 0)
#define STX104_LC GENMASK(7, 4)
#define STX104_SINGLE_CHANNEL(_channel) \
	(u8_encode_bits(_channel, STX104_FC) | u8_encode_bits(_channel, STX104_LC))

/* ADC Status */
#define STX104_SD BIT(5)
#define STX104_CNV BIT(7)
#define STX104_DIFFERENTIAL 1

/* ADC Control */
#define STX104_ALSS GENMASK(1, 0)
#define STX104_SOFTWARE_TRIGGER u8_encode_bits(0x0, STX104_ALSS)

/* ADC Configuration */
#define STX104_GAIN GENMASK(1, 0)
#define STX104_ADBU BIT(2)
#define STX104_RBK GENMASK(7, 4)
#define STX104_BIPOLAR 0
#define STX104_GAIN_X1 0
#define STX104_GAIN_X2 1
#define STX104_GAIN_X4 2
#define STX104_GAIN_X8 3

/**
 * struct stx104_iio - IIO device private data structure
 * @lock: synchronization lock to prevent I/O race conditions
 * @aio_data_map: Regmap for analog I/O data
 * @aio_ctl_map: Regmap for analog I/O control
 */
struct stx104_iio {
	struct mutex lock;
	struct regmap *aio_data_map;
	struct regmap *aio_ctl_map;
};

static const struct regmap_range aio_ctl_wr_ranges[] = {
	regmap_reg_range(0x0, 0x0), regmap_reg_range(0x2, 0x2), regmap_reg_range(0x9, 0x9),
	regmap_reg_range(0x11, 0x11),
};
static const struct regmap_range aio_ctl_rd_ranges[] = {
	regmap_reg_range(0x2, 0x2), regmap_reg_range(0x8, 0x9), regmap_reg_range(0x11, 0x11),
};
static const struct regmap_range aio_ctl_volatile_ranges[] = {
	regmap_reg_range(0x8, 0x8),
};
static const struct regmap_access_table aio_ctl_wr_table = {
	.yes_ranges = aio_ctl_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(aio_ctl_wr_ranges),
};
static const struct regmap_access_table aio_ctl_rd_table = {
	.yes_ranges = aio_ctl_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(aio_ctl_rd_ranges),
};
static const struct regmap_access_table aio_ctl_volatile_table = {
	.yes_ranges = aio_ctl_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(aio_ctl_volatile_ranges),
};

static const struct regmap_config aio_ctl_regmap_config = {
	.name = "aio_ctl",
	.reg_bits = 8,
	.reg_stride = 1,
	.reg_base = STX104_AIO_BASE,
	.val_bits = 8,
	.io_port = true,
	.wr_table = &aio_ctl_wr_table,
	.rd_table = &aio_ctl_rd_table,
	.volatile_table = &aio_ctl_volatile_table,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_range aio_data_wr_ranges[] = {
	regmap_reg_range(0x4, 0x6),
};
static const struct regmap_range aio_data_rd_ranges[] = {
	regmap_reg_range(0x0, 0x0),
};
static const struct regmap_access_table aio_data_wr_table = {
	.yes_ranges = aio_data_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(aio_data_wr_ranges),
};
static const struct regmap_access_table aio_data_rd_table = {
	.yes_ranges = aio_data_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(aio_data_rd_ranges),
};

static const struct regmap_config aio_data_regmap_config = {
	.name = "aio_data",
	.reg_bits = 16,
	.reg_stride = STX104_AIO_DATA_STRIDE,
	.reg_base = STX104_AIO_BASE,
	.val_bits = 16,
	.io_port = true,
	.wr_table = &aio_data_wr_table,
	.rd_table = &aio_data_rd_table,
	.volatile_table = &aio_data_rd_table,
	.cache_type = REGCACHE_FLAT,
};

static const struct regmap_config dio_regmap_config = {
	.name = "dio",
	.reg_bits = 8,
	.reg_stride = 1,
	.reg_base = STX104_DIO_REG,
	.val_bits = 8,
	.io_port = true,
};

static const struct regmap_range pit_wr_ranges[] = {
	regmap_reg_range(0x0, 0x3),
};
static const struct regmap_range pit_rd_ranges[] = {
	regmap_reg_range(0x0, 0x2),
};
static const struct regmap_access_table pit_wr_table = {
	.yes_ranges = pit_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(pit_wr_ranges),
};
static const struct regmap_access_table pit_rd_table = {
	.yes_ranges = pit_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(pit_rd_ranges),
};

static const struct regmap_config pit_regmap_config = {
	.name = "i8254",
	.reg_bits = 8,
	.reg_stride = 1,
	.reg_base = STX104_I8254_BASE,
	.val_bits = 8,
	.io_port = true,
	.wr_table = &pit_wr_table,
	.rd_table = &pit_rd_table,
};

static int stx104_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
	struct stx104_iio *const priv = iio_priv(indio_dev);
	int err;
	unsigned int adc_config;
	unsigned int value;
	unsigned int adc_status;

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		err = regmap_read(priv->aio_ctl_map, STX104_ADC_CONFIGURATION, &adc_config);
		if (err)
			return err;

		*val = BIT(u8_get_bits(adc_config, STX104_GAIN));
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_RAW:
		if (chan->output) {
			err = regmap_read(priv->aio_data_map, STX104_DAC_OFFSET(chan->channel),
					  &value);
			if (err)
				return err;
			*val = value;
			return IIO_VAL_INT;
		}

		mutex_lock(&priv->lock);

		/* select ADC channel */
		err = regmap_write(priv->aio_ctl_map, STX104_ADC_CHANNEL,
				   STX104_SINGLE_CHANNEL(chan->channel));
		if (err) {
			mutex_unlock(&priv->lock);
			return err;
		}

		/*
		 * Trigger ADC sample capture by writing to the 8-bit Software Strobe Register and
		 * wait for completion; the conversion time range is 5 microseconds to 53.68 seconds
		 * in steps of 25 nanoseconds. The actual Analog Input Frame Timer time interval is
		 * calculated as:
		 * ai_time_frame_ns = ( AIFT + 1 ) * ( 25 nanoseconds ).
		 * Where 0 <= AIFT <= 2147483648.
		 */
		err = regmap_write(priv->aio_ctl_map, STX104_SOFTWARE_STROBE, 0);
		if (err) {
			mutex_unlock(&priv->lock);
			return err;
		}
		err = regmap_read_poll_timeout(priv->aio_ctl_map, STX104_ADC_STATUS, adc_status,
					       !u8_get_bits(adc_status, STX104_CNV), 0, 53687092);
		if (err) {
			mutex_unlock(&priv->lock);
			return err;
		}

		err = regmap_read(priv->aio_data_map, STX104_ADC_DATA, &value);
		if (err) {
			mutex_unlock(&priv->lock);
			return err;
		}
		*val = value;

		mutex_unlock(&priv->lock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		/* get ADC bipolar/unipolar configuration */
		err = regmap_read(priv->aio_ctl_map, STX104_ADC_CONFIGURATION, &adc_config);
		if (err)
			return err;

		*val = (u8_get_bits(adc_config, STX104_ADBU) == STX104_BIPOLAR) ? -32768 : 0;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* get ADC bipolar/unipolar and gain configuration */
		err = regmap_read(priv->aio_ctl_map, STX104_ADC_CONFIGURATION, &adc_config);
		if (err)
			return err;

		*val = 5;
		*val2 = (u8_get_bits(adc_config, STX104_ADBU) == STX104_BIPOLAR) ? 14 : 15;
		*val2 += u8_get_bits(adc_config, STX104_GAIN);
		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static int stx104_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct stx104_iio *const priv = iio_priv(indio_dev);
	u8 gain;

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		/* Only four gain states (x1, x2, x4, x8) */
		switch (val) {
		case 1:
			gain = STX104_GAIN_X1;
			break;
		case 2:
			gain = STX104_GAIN_X2;
			break;
		case 4:
			gain = STX104_GAIN_X4;
			break;
		case 8:
			gain = STX104_GAIN_X8;
			break;
		default:
			return -EINVAL;
		}

		return regmap_write(priv->aio_ctl_map, STX104_ADC_CONFIGURATION, gain);
	case IIO_CHAN_INFO_RAW:
		if (!chan->output)
			return -EINVAL;

		if (val < 0 || val > U16_MAX)
			return -EINVAL;

		return regmap_write(priv->aio_data_map, STX104_DAC_OFFSET(chan->channel), val);
	}

	return -EINVAL;
}

static const struct iio_info stx104_info = {
	.read_raw = stx104_read_raw,
	.write_raw = stx104_write_raw
};

/* single-ended input channels configuration */
static const struct iio_chan_spec stx104_channels_sing[] = {
	STX104_OUT_CHAN(0), STX104_OUT_CHAN(1),
	STX104_IN_CHAN(0, 0), STX104_IN_CHAN(1, 0), STX104_IN_CHAN(2, 0),
	STX104_IN_CHAN(3, 0), STX104_IN_CHAN(4, 0), STX104_IN_CHAN(5, 0),
	STX104_IN_CHAN(6, 0), STX104_IN_CHAN(7, 0), STX104_IN_CHAN(8, 0),
	STX104_IN_CHAN(9, 0), STX104_IN_CHAN(10, 0), STX104_IN_CHAN(11, 0),
	STX104_IN_CHAN(12, 0), STX104_IN_CHAN(13, 0), STX104_IN_CHAN(14, 0),
	STX104_IN_CHAN(15, 0)
};
/* differential input channels configuration */
static const struct iio_chan_spec stx104_channels_diff[] = {
	STX104_OUT_CHAN(0), STX104_OUT_CHAN(1),
	STX104_IN_CHAN(0, 1), STX104_IN_CHAN(1, 1), STX104_IN_CHAN(2, 1),
	STX104_IN_CHAN(3, 1), STX104_IN_CHAN(4, 1), STX104_IN_CHAN(5, 1),
	STX104_IN_CHAN(6, 1), STX104_IN_CHAN(7, 1)
};

static int stx104_reg_mask_xlate(struct gpio_regmap *const gpio, const unsigned int base,
				 unsigned int offset, unsigned int *const reg,
				 unsigned int *const mask)
{
	/* Output lines are located at same register bit offsets as input lines */
	if (offset >= 4)
		offset -= 4;

	*reg = base;
	*mask = BIT(offset);

	return 0;
}

#define STX104_NGPIO 8
static const char *stx104_names[STX104_NGPIO] = {
	"DIN0", "DIN1", "DIN2", "DIN3", "DOUT0", "DOUT1", "DOUT2", "DOUT3"
};

static int bank_select_i8254(struct regmap *map)
{
	const u8 select_i8254[] = { 0x3, 0xB, 0xA };
	size_t i;
	int err;

	for (i = 0; i < ARRAY_SIZE(select_i8254); i++) {
		err = regmap_write_bits(map, STX104_ADC_CONFIGURATION, STX104_RBK, select_i8254[i]);
		if (err)
			return err;
	}

	return 0;
}

static int stx104_init_hw(struct stx104_iio *const priv)
{
	int err;

	/* configure device for software trigger operation */
	err = regmap_write(priv->aio_ctl_map, STX104_ADC_CONTROL, STX104_SOFTWARE_TRIGGER);
	if (err)
		return err;

	/* initialize gain setting to x1 */
	err = regmap_write(priv->aio_ctl_map, STX104_ADC_CONFIGURATION, STX104_GAIN_X1);
	if (err)
		return err;

	/* initialize DAC outputs to 0V */
	err = regmap_write(priv->aio_data_map, STX104_DAC_BASE, 0);
	if (err)
		return err;
	err = regmap_write(priv->aio_data_map, STX104_DAC_BASE + STX104_AIO_DATA_STRIDE, 0);
	if (err)
		return err;

	return bank_select_i8254(priv->aio_ctl_map);
}

static int stx104_probe(struct device *dev, unsigned int id)
{
	struct iio_dev *indio_dev;
	struct stx104_iio *priv;
	struct gpio_regmap_config gpio_config;
	struct i8254_regmap_config pit_config;
	void __iomem *stx104_base;
	struct regmap *aio_ctl_map;
	struct regmap *aio_data_map;
	struct regmap *dio_map;
	int err;
	unsigned int adc_status;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], STX104_EXTENT,
		dev_name(dev))) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + STX104_EXTENT);
		return -EBUSY;
	}

	stx104_base = devm_ioport_map(dev, base[id], STX104_EXTENT);
	if (!stx104_base)
		return -ENOMEM;

	aio_ctl_map = devm_regmap_init_mmio(dev, stx104_base, &aio_ctl_regmap_config);
	if (IS_ERR(aio_ctl_map))
		return dev_err_probe(dev, PTR_ERR(aio_ctl_map),
				     "Unable to initialize aio_ctl register map\n");

	aio_data_map = devm_regmap_init_mmio(dev, stx104_base, &aio_data_regmap_config);
	if (IS_ERR(aio_data_map))
		return dev_err_probe(dev, PTR_ERR(aio_data_map),
				     "Unable to initialize aio_data register map\n");

	dio_map = devm_regmap_init_mmio(dev, stx104_base, &dio_regmap_config);
	if (IS_ERR(dio_map))
		return dev_err_probe(dev, PTR_ERR(dio_map),
				     "Unable to initialize dio register map\n");

	pit_config.map = devm_regmap_init_mmio(dev, stx104_base, &pit_regmap_config);
	if (IS_ERR(pit_config.map))
		return dev_err_probe(dev, PTR_ERR(pit_config.map),
				     "Unable to initialize i8254 register map\n");

	priv = iio_priv(indio_dev);
	priv->aio_ctl_map = aio_ctl_map;
	priv->aio_data_map = aio_data_map;

	indio_dev->info = &stx104_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	err = regmap_read(aio_ctl_map, STX104_ADC_STATUS, &adc_status);
	if (err)
		return err;

	if (u8_get_bits(adc_status, STX104_SD) == STX104_DIFFERENTIAL) {
		indio_dev->num_channels = ARRAY_SIZE(stx104_channels_diff);
		indio_dev->channels = stx104_channels_diff;
	} else {
		indio_dev->num_channels = ARRAY_SIZE(stx104_channels_sing);
		indio_dev->channels = stx104_channels_sing;
	}

	indio_dev->name = dev_name(dev);

	mutex_init(&priv->lock);

	err = stx104_init_hw(priv);
	if (err)
		return err;

	err = devm_iio_device_register(dev, indio_dev);
	if (err)
		return err;

	gpio_config = (struct gpio_regmap_config) {
		.parent = dev,
		.regmap = dio_map,
		.ngpio = STX104_NGPIO,
		.names = stx104_names,
		.reg_dat_base = GPIO_REGMAP_ADDR(STX104_DIO_REG),
		.reg_set_base = GPIO_REGMAP_ADDR(STX104_DIO_REG),
		.ngpio_per_reg = STX104_NGPIO,
		.reg_mask_xlate = stx104_reg_mask_xlate,
		.drvdata = dio_map,
	};

	err = PTR_ERR_OR_ZERO(devm_gpio_regmap_register(dev, &gpio_config));
	if (err)
		return err;

	pit_config.parent = dev;

	return devm_i8254_regmap_register(dev, &pit_config);
}

static struct isa_driver stx104_driver = {
	.probe = stx104_probe,
	.driver = {
		.name = "stx104"
	},
};

module_isa_driver(stx104_driver, num_stx104);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("Apex Embedded Systems STX104 IIO driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(I8254);
