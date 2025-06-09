// SPDX-License-Identifier: GPL-2.0
/*
 * AD7606 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 */

#include <linux/bitmap.h>
#include <linux/err.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/pwm.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/offload/provider.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/units.h>

#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/iio.h>

#include <dt-bindings/iio/adc/adi,ad7606.h>

#include "ad7606.h"

#define MAX_SPI_FREQ_HZ		23500000	/* VDRIVE above 4.75 V */

struct spi_bus_data {
	struct spi_offload *offload;
	struct spi_offload_trigger *offload_trigger;
	struct spi_transfer offload_xfer;
	struct spi_message offload_msg;
};

static u16 ad7616_spi_rd_wr_cmd(int addr, char is_write_op)
{
	/*
	 * The address of register consist of one w/r bit
	 * 6 bits of address followed by one reserved bit.
	 */
	return ((addr & 0x7F) << 1) | ((is_write_op & 0x1) << 7);
}

static u16 ad7606b_spi_rd_wr_cmd(int addr, char is_write_op)
{
	/*
	 * The address of register consists of one bit which
	 * specifies a read command placed in bit 6, followed by
	 * 6 bits of address.
	 */
	return (addr & 0x3F) | (((~is_write_op) & 0x1) << 6);
}

static int ad7606_spi_read_block(struct device *dev,
				 int count, void *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	int i, ret;
	unsigned short *data = buf;
	__be16 *bdata = buf;

	ret = spi_read(spi, buf, count * 2);
	if (ret < 0) {
		dev_err(&spi->dev, "SPI read error\n");
		return ret;
	}

	for (i = 0; i < count; i++)
		data[i] = be16_to_cpu(bdata[i]);

	return 0;
}

static int ad7606_spi_read_block14to16(struct device *dev,
				       int count, void *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct spi_transfer xfer = {
		.bits_per_word = 14,
		.len = count * sizeof(u16),
		.rx_buf = buf,
	};

	return spi_sync_transfer(spi, &xfer, 1);
}

static int ad7606_spi_read_block18to32(struct device *dev,
				       int count, void *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct spi_transfer xfer = {
		.bits_per_word = 18,
		.len = count * sizeof(u32),
		.rx_buf = buf,
	};

	return spi_sync_transfer(spi, &xfer, 1);
}

static int ad7606_spi_reg_read(struct ad7606_state *st, unsigned int addr)
{
	struct spi_device *spi = to_spi_device(st->dev);
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->d16[0],
			.len = 2,
			.cs_change = 1,
		}, {
			.rx_buf = &st->d16[1],
			.len = 2,
		},
	};
	int ret;

	st->d16[0] = cpu_to_be16(st->bops->rd_wr_cmd(addr, 0) << 8);

	ret = spi_sync_transfer(spi, t, ARRAY_SIZE(t));
	if (ret < 0)
		return ret;

	return be16_to_cpu(st->d16[1]);
}

static int ad7606_spi_reg_write(struct ad7606_state *st,
				unsigned int addr,
				unsigned int val)
{
	struct spi_device *spi = to_spi_device(st->dev);

	st->d16[0] = cpu_to_be16((st->bops->rd_wr_cmd(addr, 1) << 8) |
				  (val & 0xFF));

	return spi_write(spi, &st->d16[0], sizeof(st->d16[0]));
}

static int ad7606b_sw_mode_config(struct iio_dev *indio_dev)
{
	struct ad7606_state *st = iio_priv(indio_dev);

	/* Configure device spi to output on a single channel */
	return st->bops->reg_write(st, AD7606_CONFIGURATION_REGISTER,
				   AD7606_SINGLE_DOUT);
}

static const struct spi_offload_config ad7606_spi_offload_config = {
	.capability_flags = SPI_OFFLOAD_CAP_TRIGGER |
			    SPI_OFFLOAD_CAP_RX_STREAM_DMA,
};

static int ad7606_spi_offload_buffer_postenable(struct iio_dev *indio_dev)
{
	const struct iio_scan_type *scan_type;
	struct ad7606_state *st = iio_priv(indio_dev);
	struct spi_bus_data *bus_data = st->bus_data;
	struct spi_transfer *xfer = &bus_data->offload_xfer;
	struct spi_device *spi = to_spi_device(st->dev);
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_DATA_READY,
	};
	int ret;

	scan_type = &indio_dev->channels[0].scan_type;

	xfer->bits_per_word = scan_type->realbits;
	xfer->offload_flags = SPI_OFFLOAD_XFER_RX_STREAM;
	/*
	 * Using SPI offload, storagebits are related to the spi-engine
	 * hw implementation, can be 16 or 32, so can't be used to compute
	 * struct spi_transfer.len. Using realbits instead.
	 */
	xfer->len = (scan_type->realbits > 16 ? 4 : 2) *
		    st->chip_info->num_adc_channels;

	spi_message_init_with_transfers(&bus_data->offload_msg, xfer, 1);
	bus_data->offload_msg.offload = bus_data->offload;

	ret = spi_optimize_message(spi, &bus_data->offload_msg);
	if (ret) {
		dev_err(st->dev, "failed to prepare offload, err: %d\n", ret);
		return ret;
	}

	ret = spi_offload_trigger_enable(bus_data->offload,
					 bus_data->offload_trigger,
					 &config);
	if (ret)
		goto err_unoptimize_message;

	ret = ad7606_pwm_set_swing(st);
	if (ret)
		goto err_offload_exit_conversion_mode;

	return 0;

err_offload_exit_conversion_mode:
	spi_offload_trigger_disable(bus_data->offload,
				    bus_data->offload_trigger);

err_unoptimize_message:
	spi_unoptimize_message(&bus_data->offload_msg);

	return ret;
}

static int ad7606_spi_offload_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	struct spi_bus_data *bus_data = st->bus_data;
	int ret;

	ret = ad7606_pwm_set_low(st);
	if (ret)
		return ret;

	spi_offload_trigger_disable(bus_data->offload,
				    bus_data->offload_trigger);
	spi_unoptimize_message(&bus_data->offload_msg);

	return 0;
}

static const struct iio_buffer_setup_ops ad7606_offload_buffer_setup_ops = {
	.postenable = ad7606_spi_offload_buffer_postenable,
	.predisable = ad7606_spi_offload_buffer_predisable,
};

static bool ad7606_spi_offload_trigger_match(
				struct spi_offload_trigger *trigger,
				enum spi_offload_trigger_type type,
				u64 *args, u32 nargs)
{
	if (type != SPI_OFFLOAD_TRIGGER_DATA_READY)
	       return false;

	/*
	 * Requires 1 arg:
	 * args[0] is the trigger event.
	 */
	if (nargs != 1 || args[0] != AD7606_TRIGGER_EVENT_BUSY)
		return false;

	return true;
}

static int ad7606_spi_offload_trigger_request(
				struct spi_offload_trigger *trigger,
				enum spi_offload_trigger_type type,
				u64 *args, u32 nargs)
{
	/* Should already be validated by match, but just in case. */
	if (nargs != 1)
		return -EINVAL;

	return 0;
}

static int ad7606_spi_offload_trigger_validate(
				struct spi_offload_trigger *trigger,
				struct spi_offload_trigger_config *config)
{
	if (config->type != SPI_OFFLOAD_TRIGGER_DATA_READY)
		return -EINVAL;

	return 0;
}

static const struct spi_offload_trigger_ops ad7606_offload_trigger_ops = {
	.match = ad7606_spi_offload_trigger_match,
	.request = ad7606_spi_offload_trigger_request,
	.validate = ad7606_spi_offload_trigger_validate,
};

static int ad7606_spi_offload_probe(struct device *dev,
				    struct iio_dev *indio_dev)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	struct spi_device *spi = to_spi_device(dev);
	struct spi_bus_data *bus_data;
	struct dma_chan *rx_dma;
	struct spi_offload_trigger_info trigger_info = {
		.fwnode = dev_fwnode(dev),
		.ops = &ad7606_offload_trigger_ops,
		.priv = st,
	};
	int ret;

	bus_data = devm_kzalloc(dev, sizeof(*bus_data), GFP_KERNEL);
	if (!bus_data)
		return -ENOMEM;
	st->bus_data = bus_data;

	bus_data->offload = devm_spi_offload_get(dev, spi,
						 &ad7606_spi_offload_config);
	ret = PTR_ERR_OR_ZERO(bus_data->offload);
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to get SPI offload\n");
	/* Allow main ad7606_probe function to continue. */
	if (ret == -ENODEV)
		return 0;

	ret = devm_spi_offload_trigger_register(dev, &trigger_info);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register offload trigger\n");

	bus_data->offload_trigger = devm_spi_offload_trigger_get(dev,
		bus_data->offload, SPI_OFFLOAD_TRIGGER_DATA_READY);
	if (IS_ERR(bus_data->offload_trigger))
		return dev_err_probe(dev, PTR_ERR(bus_data->offload_trigger),
				     "failed to get offload trigger\n");

	/* TODO: PWM setup should be ok, done for the backend. PWM mutex ? */
	rx_dma = devm_spi_offload_rx_stream_request_dma_chan(dev,
							     bus_data->offload);
	if (IS_ERR(rx_dma))
		return dev_err_probe(dev, PTR_ERR(rx_dma),
				     "failed to get offload RX DMA\n");

	ret = devm_iio_dmaengine_buffer_setup_with_handle(dev, indio_dev,
		rx_dma, IIO_BUFFER_DIRECTION_IN);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to setup offload RX DMA\n");

	/* Use offload ops. */
	indio_dev->setup_ops = &ad7606_offload_buffer_setup_ops;

	st->offload_en = true;

	return 0;
}

static int ad7606_spi_update_scan_mode(struct iio_dev *indio_dev,
				       const unsigned long *scan_mask)
{
	struct ad7606_state *st = iio_priv(indio_dev);

	if (st->offload_en) {
		unsigned int num_adc_ch = st->chip_info->num_adc_channels;

		/*
		 * SPI offload requires that all channels are enabled since
		 * there isn't a way to selectively disable channels that get
		 * read (this is simultaneous sampling ADC) and the DMA buffer
		 * has no way of demuxing the data to filter out unwanted
		 * channels.
		 */
		if (bitmap_weight(scan_mask, num_adc_ch) != num_adc_ch)
			return -EINVAL;
	}

	return 0;
}

static const struct ad7606_bus_ops ad7606_spi_bops = {
	.offload_config = ad7606_spi_offload_probe,
	.read_block = ad7606_spi_read_block,
	.update_scan_mode = ad7606_spi_update_scan_mode,
};

static const struct ad7606_bus_ops ad7607_spi_bops = {
	.offload_config = ad7606_spi_offload_probe,
	.read_block = ad7606_spi_read_block14to16,
	.update_scan_mode = ad7606_spi_update_scan_mode,
};

static const struct ad7606_bus_ops ad7608_spi_bops = {
	.offload_config = ad7606_spi_offload_probe,
	.read_block = ad7606_spi_read_block18to32,
	.update_scan_mode = ad7606_spi_update_scan_mode,
};

static const struct ad7606_bus_ops ad7616_spi_bops = {
	.offload_config = ad7606_spi_offload_probe,
	.read_block = ad7606_spi_read_block,
	.reg_read = ad7606_spi_reg_read,
	.reg_write = ad7606_spi_reg_write,
	.rd_wr_cmd = ad7616_spi_rd_wr_cmd,
	.update_scan_mode = ad7606_spi_update_scan_mode,
};

static const struct ad7606_bus_ops ad7606b_spi_bops = {
	.offload_config = ad7606_spi_offload_probe,
	.read_block = ad7606_spi_read_block,
	.reg_read = ad7606_spi_reg_read,
	.reg_write = ad7606_spi_reg_write,
	.rd_wr_cmd = ad7606b_spi_rd_wr_cmd,
	.sw_mode_config = ad7606b_sw_mode_config,
	.update_scan_mode = ad7606_spi_update_scan_mode,
};

static const struct ad7606_bus_ops ad7606c_18_spi_bops = {
	.offload_config = ad7606_spi_offload_probe,
	.read_block = ad7606_spi_read_block18to32,
	.reg_read = ad7606_spi_reg_read,
	.reg_write = ad7606_spi_reg_write,
	.rd_wr_cmd = ad7606b_spi_rd_wr_cmd,
	.sw_mode_config = ad7606b_sw_mode_config,
	.update_scan_mode = ad7606_spi_update_scan_mode,
};

static const struct ad7606_bus_info ad7605_4_bus_info = {
	.chip_info = &ad7605_4_info,
	.bops = &ad7606_spi_bops,
};

static const struct ad7606_bus_info ad7606_8_bus_info = {
	.chip_info = &ad7606_8_info,
	.bops = &ad7606_spi_bops,
};

static const struct ad7606_bus_info ad7606_6_bus_info = {
	.chip_info = &ad7606_6_info,
	.bops = &ad7606_spi_bops,
};

static const struct ad7606_bus_info ad7606_4_bus_info = {
	.chip_info = &ad7606_4_info,
	.bops = &ad7606_spi_bops,
};

static const struct ad7606_bus_info ad7606b_bus_info = {
	.chip_info = &ad7606b_info,
	.bops = &ad7606b_spi_bops,
};

static const struct ad7606_bus_info ad7606c_16_bus_info = {
	.chip_info = &ad7606c_16_info,
	.bops = &ad7606b_spi_bops,
};

static const struct ad7606_bus_info ad7606c_18_bus_info = {
	.chip_info = &ad7606c_18_info,
	.bops = &ad7606c_18_spi_bops,
};

static const struct ad7606_bus_info ad7607_bus_info = {
	.chip_info = &ad7607_info,
	.bops = &ad7607_spi_bops,
};

static const struct ad7606_bus_info ad7608_bus_info = {
	.chip_info = &ad7608_info,
	.bops = &ad7608_spi_bops,
};

static const struct ad7606_bus_info ad7609_bus_info = {
	.chip_info = &ad7609_info,
	.bops = &ad7608_spi_bops,
};

static const struct ad7606_bus_info ad7616_bus_info = {
	.chip_info = &ad7616_info,
	.bops = &ad7616_spi_bops,
};

static int ad7606_spi_probe(struct spi_device *spi)
{
	const struct ad7606_bus_info *bus_info = spi_get_device_match_data(spi);

	return ad7606_probe(&spi->dev, spi->irq, NULL,
			    bus_info->chip_info, bus_info->bops);
}

static const struct spi_device_id ad7606_id_table[] = {
	{ "ad7605-4", (kernel_ulong_t)&ad7605_4_bus_info },
	{ "ad7606-4", (kernel_ulong_t)&ad7606_4_bus_info },
	{ "ad7606-6", (kernel_ulong_t)&ad7606_6_bus_info },
	{ "ad7606-8", (kernel_ulong_t)&ad7606_8_bus_info },
	{ "ad7606b",  (kernel_ulong_t)&ad7606b_bus_info },
	{ "ad7606c-16", (kernel_ulong_t)&ad7606c_16_bus_info },
	{ "ad7606c-18", (kernel_ulong_t)&ad7606c_18_bus_info },
	{ "ad7607",   (kernel_ulong_t)&ad7607_bus_info },
	{ "ad7608",   (kernel_ulong_t)&ad7608_bus_info },
	{ "ad7609",   (kernel_ulong_t)&ad7609_bus_info },
	{ "ad7616",   (kernel_ulong_t)&ad7616_bus_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7606_id_table);

static const struct of_device_id ad7606_of_match[] = {
	{ .compatible = "adi,ad7605-4", .data = &ad7605_4_bus_info },
	{ .compatible = "adi,ad7606-4", .data = &ad7606_4_bus_info },
	{ .compatible = "adi,ad7606-6", .data = &ad7606_6_bus_info },
	{ .compatible = "adi,ad7606-8", .data = &ad7606_8_bus_info },
	{ .compatible = "adi,ad7606b", .data = &ad7606b_bus_info },
	{ .compatible = "adi,ad7606c-16", .data = &ad7606c_16_bus_info },
	{ .compatible = "adi,ad7606c-18", .data = &ad7606c_18_bus_info },
	{ .compatible = "adi,ad7607", .data = &ad7607_bus_info },
	{ .compatible = "adi,ad7608", .data = &ad7608_bus_info },
	{ .compatible = "adi,ad7609", .data = &ad7609_bus_info },
	{ .compatible = "adi,ad7616", .data = &ad7616_bus_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7606_of_match);

static struct spi_driver ad7606_driver = {
	.driver = {
		.name = "ad7606",
		.of_match_table = ad7606_of_match,
		.pm = AD7606_PM_OPS,
	},
	.probe = ad7606_spi_probe,
	.id_table = ad7606_id_table,
};
module_spi_driver(ad7606_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7606 ADC");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_AD7606");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
