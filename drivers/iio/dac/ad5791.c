// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD5760, AD5780, AD5781, AD5790, AD5791 Voltage Output Digital to Analog
 * Converter
 *
 * Copyright 2011 Analog Devices Inc.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/units.h>

#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/dac/ad5791.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define AD5791_DAC_MASK			GENMASK(19, 0)

#define AD5791_CMD_READ			BIT(23)
#define AD5791_CMD_WRITE		0
#define AD5791_ADDR(addr)		((addr) << 20)

/* Registers */
#define AD5791_ADDR_NOOP		0
#define AD5791_ADDR_DAC0		1
#define AD5791_ADDR_CTRL		2
#define AD5791_ADDR_CLRCODE		3
#define AD5791_ADDR_SW_CTRL		4

/* Control Register */
#define AD5791_CTRL_RBUF		BIT(1)
#define AD5791_CTRL_OPGND		BIT(2)
#define AD5791_CTRL_DACTRI		BIT(3)
#define AD5791_CTRL_BIN2SC		BIT(4)
#define AD5791_CTRL_SDODIS		BIT(5)
#define AD5761_CTRL_LINCOMP(x)		((x) << 6)

#define AD5791_LINCOMP_0_10		0
#define AD5791_LINCOMP_10_12		1
#define AD5791_LINCOMP_12_16		2
#define AD5791_LINCOMP_16_19		3
#define AD5791_LINCOMP_19_20		12

#define AD5780_LINCOMP_0_10		0
#define AD5780_LINCOMP_10_20		12

/* Software Control Register */
#define AD5791_SWCTRL_LDAC		BIT(0)
#define AD5791_SWCTRL_CLR		BIT(1)
#define AD5791_SWCTRL_RESET		BIT(2)

#define AD5791_DAC_PWRDN_6K		0
#define AD5791_DAC_PWRDN_3STATE		1

/**
 * struct ad5791_chip_info - chip specific information
 * @name:		name of the dac chip
 * @channel:		channel specification
 * @channel_offload:	channel specification for offload
 * @get_lin_comp:	function pointer to the device specific function
 */
struct ad5791_chip_info {
	const char *name;
	const struct iio_chan_spec channel;
	const struct iio_chan_spec channel_offload;
	int (*get_lin_comp)(unsigned int span);
};

/**
 * struct ad5791_state - driver instance specific data
 * @spi:			spi_device
 * @reg_vdd:		positive supply regulator
 * @reg_vss:		negative supply regulator
 * @gpio_reset:		reset gpio
 * @gpio_clear:		clear gpio
 * @gpio_ldac:		load dac gpio
 * @chip_info:		chip model specific constants
 * @offload_msg:	spi message used for offload
 * @offload_xfer:	spi transfer used for offload
 * @offload:		offload device
 * @offload_trigger:	offload trigger
 * @offload_trigger_hz:	offload sample rate
 * @vref_mv:		actual reference voltage used
 * @vref_neg_mv:	voltage of the negative supply
 * @ctrl:		control register cache
 * @pwr_down_mode:	current power down mode
 * @pwr_down:		true if device is powered down
 * @data:		spi transfer buffers
 */
struct ad5791_state {
	struct spi_device		*spi;
	struct regulator		*reg_vdd;
	struct regulator		*reg_vss;
	struct gpio_desc		*gpio_reset;
	struct gpio_desc		*gpio_clear;
	struct gpio_desc		*gpio_ldac;
	const struct ad5791_chip_info	*chip_info;
	struct spi_message		offload_msg;
	struct spi_transfer		offload_xfer;
	struct spi_offload		*offload;
	struct spi_offload_trigger	*offload_trigger;
	unsigned int			offload_trigger_hz;
	unsigned short			vref_mv;
	unsigned int			vref_neg_mv;
	unsigned			ctrl;
	unsigned			pwr_down_mode;
	bool				pwr_down;

	union {
		__be32 d32;
		u8 d8[4];
	} data[3] __aligned(IIO_DMA_MINALIGN);
};

static int ad5791_spi_write(struct ad5791_state *st, u8 addr, u32 val)
{
	st->data[0].d32 = cpu_to_be32(AD5791_CMD_WRITE |
			      AD5791_ADDR(addr) |
			      (val & AD5791_DAC_MASK));

	return spi_write(st->spi, &st->data[0].d8[1], 3);
}

static int ad5791_spi_read(struct ad5791_state *st, u8 addr, u32 *val)
{
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = &st->data[0].d8[1],
			.bits_per_word = 8,
			.len = 3,
			.cs_change = 1,
		}, {
			.tx_buf = &st->data[1].d8[1],
			.rx_buf = &st->data[2].d8[1],
			.bits_per_word = 8,
			.len = 3,
		},
	};

	st->data[0].d32 = cpu_to_be32(AD5791_CMD_READ |
			      AD5791_ADDR(addr));
	st->data[1].d32 = cpu_to_be32(AD5791_ADDR(AD5791_ADDR_NOOP));

	ret = spi_sync_transfer(st->spi, xfers, ARRAY_SIZE(xfers));

	*val = be32_to_cpu(st->data[2].d32);

	return ret;
}

static const char * const ad5791_powerdown_modes[] = {
	"6kohm_to_gnd",
	"three_state",
};

static int ad5791_get_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	struct ad5791_state *st = iio_priv(indio_dev);

	return st->pwr_down_mode;
}

static int ad5791_set_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int mode)
{
	struct ad5791_state *st = iio_priv(indio_dev);

	st->pwr_down_mode = mode;

	return 0;
}

static const struct iio_enum ad5791_powerdown_mode_enum = {
	.items = ad5791_powerdown_modes,
	.num_items = ARRAY_SIZE(ad5791_powerdown_modes),
	.get = ad5791_get_powerdown_mode,
	.set = ad5791_set_powerdown_mode,
};

static ssize_t ad5791_read_dac_powerdown(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, char *buf)
{
	struct ad5791_state *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", st->pwr_down);
}

static ssize_t ad5791_write_dac_powerdown(struct iio_dev *indio_dev,
	 uintptr_t private, const struct iio_chan_spec *chan, const char *buf,
	 size_t len)
{
	bool pwr_down;
	int ret;
	struct ad5791_state *st = iio_priv(indio_dev);

	ret = kstrtobool(buf, &pwr_down);
	if (ret)
		return ret;

	if (!pwr_down) {
		st->ctrl &= ~(AD5791_CTRL_OPGND | AD5791_CTRL_DACTRI);
	} else {
		if (st->pwr_down_mode == AD5791_DAC_PWRDN_6K)
			st->ctrl |= AD5791_CTRL_OPGND;
		else if (st->pwr_down_mode == AD5791_DAC_PWRDN_3STATE)
			st->ctrl |= AD5791_CTRL_DACTRI;
	}
	st->pwr_down = pwr_down;

	ret = ad5791_spi_write(st, AD5791_ADDR_CTRL, st->ctrl);

	return ret ? ret : len;
}

static int ad5791_get_lin_comp(unsigned int span)
{
	if (span <= 10000)
		return AD5791_LINCOMP_0_10;
	else if (span <= 12000)
		return AD5791_LINCOMP_10_12;
	else if (span <= 16000)
		return AD5791_LINCOMP_12_16;
	else if (span <= 19000)
		return AD5791_LINCOMP_16_19;
	else
		return AD5791_LINCOMP_19_20;
}

static int ad5780_get_lin_comp(unsigned int span)
{
	if (span <= 10000)
		return AD5780_LINCOMP_0_10;
	else
		return AD5780_LINCOMP_10_20;
}

static int ad5791_set_sample_freq(struct ad5791_state *st, int val)
{
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_PERIODIC,
		.periodic = {
			.frequency_hz = val,
		},
	};
	int ret;

	ret = spi_offload_trigger_validate(st->offload_trigger, &config);
	if (ret)
		return ret;

	st->offload_trigger_hz = config.periodic.frequency_hz;

	return 0;
}

static int ad5791_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5791_state *st = iio_priv(indio_dev);
	u64 val64;
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = ad5791_spi_read(st, chan->address, val);
		if (ret)
			return ret;
		*val &= AD5791_DAC_MASK;
		*val >>= chan->scan_type.shift;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_mv;
		*val2 = (1 << chan->scan_type.realbits) - 1;
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_OFFSET:
		val64 = (((u64)st->vref_neg_mv) << chan->scan_type.realbits);
		do_div(val64, st->vref_mv);
		*val = -val64;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->offload_trigger_hz;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

};

static const struct iio_chan_spec_ext_info ad5791_ext_info[] = {
	{
		.name = "powerdown",
		.shared = IIO_SHARED_BY_TYPE,
		.read = ad5791_read_dac_powerdown,
		.write = ad5791_write_dac_powerdown,
	},
	IIO_ENUM("powerdown_mode", IIO_SHARED_BY_TYPE,
		 &ad5791_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE, &ad5791_powerdown_mode_enum),
	{ },
};

#define AD5791_DEFINE_CHIP_INFO(_name, bits, _shift, _lin_comp)		\
static const struct ad5791_chip_info _name##_chip_info = {		\
	.name = #_name,							\
	.get_lin_comp = &(_lin_comp),					\
	.channel = {							\
			.type = IIO_VOLTAGE,				\
			.output = 1,					\
			.indexed = 1,					\
			.address = AD5791_ADDR_DAC0,			\
			.channel = 0,					\
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
			.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_OFFSET),		\
			.scan_type = {					\
				.sign = 'u',				\
				.realbits = (bits),			\
				.storagebits = 32,			\
				.shift = (_shift),			\
			},						\
			.ext_info = ad5791_ext_info,			\
	},								\
	.channel_offload = {						\
			.type = IIO_VOLTAGE,				\
			.output = 1,					\
			.indexed = 1,					\
			.address = AD5791_ADDR_DAC0,			\
			.channel = 0,					\
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
			.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_OFFSET),		\
			.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
			.scan_type = {					\
				.sign = 'u',				\
				.realbits = (bits),			\
				.storagebits = 32,			\
				.shift = (_shift),			\
			},						\
			.ext_info = ad5791_ext_info,			\
	},								\
}

AD5791_DEFINE_CHIP_INFO(ad5760, 16, 4, ad5780_get_lin_comp);
AD5791_DEFINE_CHIP_INFO(ad5780, 18, 2, ad5780_get_lin_comp);
AD5791_DEFINE_CHIP_INFO(ad5781, 18, 2, ad5791_get_lin_comp);
AD5791_DEFINE_CHIP_INFO(ad5790, 20, 0, ad5791_get_lin_comp);
AD5791_DEFINE_CHIP_INFO(ad5791, 20, 0, ad5791_get_lin_comp);

static int ad5791_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad5791_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		val &= GENMASK(chan->scan_type.realbits - 1, 0);
		val <<= chan->scan_type.shift;

		return ad5791_spi_write(st, chan->address, val);

	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val < 1)
			return -EINVAL;
		return ad5791_set_sample_freq(st, val);
	default:
		return -EINVAL;
	}
}

static int ad5791_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT;
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static int ad5791_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ad5791_state *st = iio_priv(indio_dev);
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_PERIODIC,
		.periodic = {
			.frequency_hz = st->offload_trigger_hz,
		},
	};

	if (st->pwr_down)
		return -EINVAL;

	return spi_offload_trigger_enable(st->offload, st->offload_trigger,
					 &config);
}

static int ad5791_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ad5791_state *st = iio_priv(indio_dev);

	spi_offload_trigger_disable(st->offload, st->offload_trigger);

	return 0;
}

static const struct iio_buffer_setup_ops ad5791_buffer_setup_ops = {
	.preenable = &ad5791_buffer_preenable,
	.postdisable = &ad5791_buffer_postdisable,
};

static int ad5791_offload_setup(struct iio_dev *indio_dev)
{
	struct ad5791_state *st = iio_priv(indio_dev);
	struct spi_device *spi = st->spi;
	struct dma_chan *tx_dma;
	int ret;

	st->offload_trigger = devm_spi_offload_trigger_get(&spi->dev,
		st->offload, SPI_OFFLOAD_TRIGGER_PERIODIC);
	if (IS_ERR(st->offload_trigger))
		return dev_err_probe(&spi->dev, PTR_ERR(st->offload_trigger),
				     "failed to get offload trigger\n");

	ret = ad5791_set_sample_freq(st, 1 * MEGA);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "failed to init sample rate\n");

	tx_dma = devm_spi_offload_tx_stream_request_dma_chan(&spi->dev,
							     st->offload);
	if (IS_ERR(tx_dma))
		return dev_err_probe(&spi->dev, PTR_ERR(tx_dma),
				     "failed to get offload TX DMA\n");

	ret = devm_iio_dmaengine_buffer_setup_with_handle(&spi->dev,
		indio_dev, tx_dma, IIO_BUFFER_DIRECTION_OUT);
	if (ret)
		return ret;

	st->offload_xfer.len = 4;
	st->offload_xfer.bits_per_word = 24;
	st->offload_xfer.offload_flags = SPI_OFFLOAD_XFER_TX_STREAM;

	spi_message_init_with_transfers(&st->offload_msg, &st->offload_xfer, 1);
	st->offload_msg.offload = st->offload;

	return devm_spi_optimize_message(&spi->dev, st->spi, &st->offload_msg);
}

static const struct iio_info ad5791_info = {
	.read_raw = &ad5791_read_raw,
	.write_raw = &ad5791_write_raw,
	.write_raw_get_fmt = &ad5791_write_raw_get_fmt,
};

static const struct spi_offload_config ad5791_offload_config = {
	.capability_flags = SPI_OFFLOAD_CAP_TRIGGER |
			    SPI_OFFLOAD_CAP_TX_STREAM_DMA,
};

static int ad5791_probe(struct spi_device *spi)
{
	const struct ad5791_platform_data *pdata = dev_get_platdata(&spi->dev);
	struct iio_dev *indio_dev;
	struct ad5791_state *st;
	int ret, pos_voltage_uv = 0, neg_voltage_uv = 0;
	bool use_rbuf_gain2;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);

	st->gpio_reset = devm_gpiod_get_optional(&spi->dev, "reset",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(st->gpio_reset))
		return PTR_ERR(st->gpio_reset);

	st->gpio_clear = devm_gpiod_get_optional(&spi->dev, "clear",
						 GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_clear))
		return PTR_ERR(st->gpio_clear);

	st->gpio_ldac = devm_gpiod_get_optional(&spi->dev, "ldac",
						GPIOD_OUT_HIGH);
	if (IS_ERR(st->gpio_ldac))
		return PTR_ERR(st->gpio_ldac);

	st->pwr_down = true;
	st->spi = spi;

	if (pdata)
		use_rbuf_gain2 = pdata->use_rbuf_gain2;
	else
		use_rbuf_gain2 = device_property_read_bool(&spi->dev,
							   "adi,rbuf-gain2-en");

	pos_voltage_uv = devm_regulator_get_enable_read_voltage(&spi->dev, "vdd");
	if (pos_voltage_uv < 0 && pos_voltage_uv != -ENODEV)
		return dev_err_probe(&spi->dev, pos_voltage_uv,
				     "failed to get vdd voltage\n");

	neg_voltage_uv = devm_regulator_get_enable_read_voltage(&spi->dev, "vss");
	if (neg_voltage_uv < 0 && neg_voltage_uv != -ENODEV)
		return dev_err_probe(&spi->dev, neg_voltage_uv,
				     "failed to get vss voltage\n");

	if (neg_voltage_uv >= 0 && pos_voltage_uv >= 0) {
		st->vref_mv = (pos_voltage_uv + neg_voltage_uv) / 1000;
		st->vref_neg_mv = neg_voltage_uv / 1000;
	} else if (pdata) {
		st->vref_mv = pdata->vref_pos_mv + pdata->vref_neg_mv;
		st->vref_neg_mv = pdata->vref_neg_mv;
	} else {
		dev_warn(&spi->dev, "reference voltage unspecified\n");
	}

	if (st->gpio_reset) {
		fsleep(20);
		gpiod_set_value_cansleep(st->gpio_reset, 0);
	} else {
		ret = ad5791_spi_write(st, AD5791_ADDR_SW_CTRL, AD5791_SWCTRL_RESET);
		if (ret)
			return dev_err_probe(&spi->dev, ret, "fail to reset\n");
	}

	st->chip_info = spi_get_device_match_data(spi);
	if (!st->chip_info)
		return dev_err_probe(&spi->dev, -EINVAL, "no chip info\n");

	st->ctrl = AD5761_CTRL_LINCOMP(st->chip_info->get_lin_comp(st->vref_mv))
		  | (use_rbuf_gain2 ? 0 : AD5791_CTRL_RBUF) |
		  AD5791_CTRL_BIN2SC;

	ret = ad5791_spi_write(st, AD5791_ADDR_CTRL, st->ctrl |
		AD5791_CTRL_OPGND | AD5791_CTRL_DACTRI);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "fail to write ctrl register\n");

	indio_dev->info = &ad5791_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &st->chip_info->channel;
	indio_dev->num_channels = 1;
	indio_dev->name = st->chip_info->name;

	st->offload = devm_spi_offload_get(&spi->dev, spi, &ad5791_offload_config);
	ret = PTR_ERR_OR_ZERO(st->offload);
	if (ret && ret != -ENODEV)
		return dev_err_probe(&spi->dev, ret, "failed to get offload\n");

	if (ret != -ENODEV) {
		indio_dev->channels = &st->chip_info->channel_offload;
		indio_dev->setup_ops = &ad5791_buffer_setup_ops;
		ret = ad5791_offload_setup(indio_dev);
		if (ret)
			return dev_err_probe(&spi->dev, ret,
					     "fail to setup offload\n");
	}

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ad5791_of_match[] = {
	{ .compatible = "adi,ad5760", .data = &ad5760_chip_info },
	{ .compatible = "adi,ad5780", .data = &ad5780_chip_info },
	{ .compatible = "adi,ad5781", .data = &ad5781_chip_info },
	{ .compatible = "adi,ad5790", .data = &ad5790_chip_info },
	{ .compatible = "adi,ad5791", .data = &ad5791_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad5791_of_match);

static const struct spi_device_id ad5791_id[] = {
	{ "ad5760", (kernel_ulong_t)&ad5760_chip_info },
	{ "ad5780", (kernel_ulong_t)&ad5780_chip_info },
	{ "ad5781", (kernel_ulong_t)&ad5781_chip_info },
	{ "ad5790", (kernel_ulong_t)&ad5790_chip_info },
	{ "ad5791", (kernel_ulong_t)&ad5791_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad5791_id);

static struct spi_driver ad5791_driver = {
	.driver = {
		   .name = "ad5791",
		   .of_match_table = ad5791_of_match,
		   },
	.probe = ad5791_probe,
	.id_table = ad5791_id,
};
module_spi_driver(ad5791_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5760/AD5780/AD5781/AD5790/AD5791 DAC");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
