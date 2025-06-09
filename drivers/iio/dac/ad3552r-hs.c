// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices AD3552R
 * Digital to Analog converter driver, High Speed version
 *
 * Copyright 2024 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/backend.h>
#include <linux/iio/buffer.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/units.h>

#include "ad3552r.h"
#include "ad3552r-hs.h"

/*
 * Important notes for register map access:
 * ========================================
 *
 * Register address space is divided in 2 regions, primary (config) and
 * secondary (DAC). Primary region can only be accessed in simple SPI mode,
 * with exception for ad355x models where setting QSPI pin high allows QSPI
 * access to both the regions.
 *
 * Due to the fact that ad3541/2r do not implement QSPI, for proper device
 * detection, HDL keeps "QSPI" pin level low at boot (see ad3552r manual, rev B
 * table 7, pin 31, digital input). For this reason, actually the working mode
 * between SPI, DSPI and QSPI must be set via software, configuring the target
 * DAC appropriately, together with the backend API to configure the bus mode
 * accordingly.
 *
 * Also, important to note that none of the three modes allow to read in DDR.
 *
 * In non-buffering operations, mode is set to simple SPI SDR for all primary
 * and secondary region r/w accesses, to avoid to switch the mode each time DAC
 * register is accessed (raw accesses, r/w), and to be able to dump registers
 * content (possible as non DDR only).
 * In buffering mode, driver sets best possible mode, D/QSPI and DDR.
 */

struct ad3552r_hs_state {
	const struct ad3552r_model_data *model_data;
	struct gpio_desc *reset_gpio;
	struct device *dev;
	struct iio_backend *back;
	bool single_channel;
	struct ad3552r_ch_data ch_data[AD3552R_MAX_CH];
	struct ad3552r_hs_platform_data *data;
	/* INTERFACE_CONFIG_D register cache, in DDR we cannot read values. */
	u32 config_d;
};

static int ad3552r_hs_reg_read(struct ad3552r_hs_state *st, u32 reg, u32 *val,
			       size_t xfer_size)
{
	/* No chip in the family supports DDR read. Informing of this. */
	WARN_ON_ONCE(st->config_d & AD3552R_MASK_SPI_CONFIG_DDR);

	return st->data->bus_reg_read(st->back, reg, val, xfer_size);
}

static int ad3552r_hs_update_reg_bits(struct ad3552r_hs_state *st, u32 reg,
				      u32 mask, u32 val, size_t xfer_size)
{
	u32 rval;
	int ret;

	ret = ad3552r_hs_reg_read(st, reg, &rval, xfer_size);
	if (ret)
		return ret;

	rval = (rval & ~mask) | val;

	return st->data->bus_reg_write(st->back, reg, rval, xfer_size);
}

static int ad3552r_hs_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask)
{
	struct ad3552r_hs_state *st = iio_priv(indio_dev);
	int ret;
	int ch = chan->channel;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		/*
		 * Using a "num_spi_data_lanes" variable since ad3541/2 have
		 * only DSPI interface, while ad355x is QSPI. Then using 2 as
		 * DDR mode is considered always on (considering buffering
		 * mode always).
		 */
		*val = DIV_ROUND_CLOSEST(st->data->bus_sample_data_clock_hz *
					 st->model_data->num_spi_data_lanes * 2,
					 chan->scan_type.realbits);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_RAW:
		/* For RAW accesses, stay always in simple-spi. */
		ret = ad3552r_hs_reg_read(st,
				AD3552R_REG_ADDR_CH_DAC_16B(chan->channel),
				val, 2);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->ch_data[ch].scale_int;
		*val2 = st->ch_data[ch].scale_dec;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		*val = st->ch_data[ch].offset_int;
		*val2 = st->ch_data[ch].offset_dec;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int ad3552r_hs_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct ad3552r_hs_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		/* For RAW accesses, stay always in simple-spi. */
		ret = st->data->bus_reg_write(st->back,
			AD3552R_REG_ADDR_CH_DAC_16B(chan->channel),
			val, 2);

		iio_device_release_direct(indio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static int ad3552r_hs_set_bus_io_mode_hs(struct ad3552r_hs_state *st)
{
	int bus_mode;

	if (st->model_data->num_spi_data_lanes == 4)
		bus_mode = AD3552R_IO_MODE_QSPI;
	else
		bus_mode = AD3552R_IO_MODE_DSPI;

	return st->data->bus_set_io_mode(st->back, bus_mode);
}

static int ad3552r_hs_set_target_io_mode_hs(struct ad3552r_hs_state *st)
{
	u32 mode_target;

	/*
	 * Best access for secondary reg area, QSPI where possible,
	 * else as DSPI.
	 */
	if (st->model_data->num_spi_data_lanes == 4)
		mode_target = AD3552R_QUAD_SPI;
	else
		mode_target = AD3552R_DUAL_SPI;

	/*
	 * Better to not use update here, since generally it is already
	 * set as DDR mode, and it's not possible to read in DDR mode.
	 */
	return st->data->bus_reg_write(st->back,
				AD3552R_REG_ADDR_TRANSFER_REGISTER,
				FIELD_PREP(AD3552R_MASK_MULTI_IO_MODE,
					   mode_target) |
				AD3552R_MASK_STREAM_LENGTH_KEEP_VALUE, 1);
}

static int ad3552r_hs_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad3552r_hs_state *st = iio_priv(indio_dev);
	struct iio_backend_data_fmt fmt = {
		.type = IIO_BACKEND_DATA_UNSIGNED
	};
	int loop_len, val, ret;

	switch (*indio_dev->active_scan_mask) {
	case AD3552R_CH0_ACTIVE:
		st->single_channel = true;
		loop_len = 2;
		val = AD3552R_REG_ADDR_CH_DAC_16B(0);
		break;
	case AD3552R_CH1_ACTIVE:
		st->single_channel = true;
		loop_len = 2;
		val = AD3552R_REG_ADDR_CH_DAC_16B(1);
		break;
	case AD3552R_CH0_ACTIVE | AD3552R_CH1_ACTIVE:
		st->single_channel = false;
		loop_len = 4;
		val = AD3552R_REG_ADDR_CH_DAC_16B(1);
		break;
	default:
		return -EINVAL;
	}

	/*
	 * With ad3541/2r support, QSPI pin is held low at reset from HDL,
	 * streaming start sequence must respect strictly the order below.
	 */

	/* Primary region access, set streaming mode (now in SPI + SDR). */
	ret = ad3552r_hs_update_reg_bits(st,
					 AD3552R_REG_ADDR_INTERFACE_CONFIG_B,
					 AD3552R_MASK_SINGLE_INST, 0, 1);
	if (ret)
		return ret;

	/*
	 * Set target loop len, keeping the value: streaming writes at address
	 * 0x2c or 0x2a, in descending loop (2 or 4 bytes), keeping loop len
	 * value so that it's not cleared hereafter when _CS is deasserted.
	 */
	ret = ad3552r_hs_update_reg_bits(st, AD3552R_REG_ADDR_TRANSFER_REGISTER,
					 AD3552R_MASK_STREAM_LENGTH_KEEP_VALUE,
					 AD3552R_MASK_STREAM_LENGTH_KEEP_VALUE,
					 1);
	if (ret)
		goto exit_err_streaming;

	ret = st->data->bus_reg_write(st->back,
				      AD3552R_REG_ADDR_STREAM_MODE,
				      loop_len, 1);
	if (ret)
		goto exit_err_streaming;

	st->config_d |= AD3552R_MASK_SPI_CONFIG_DDR;
	ret = st->data->bus_reg_write(st->back,
				      AD3552R_REG_ADDR_INTERFACE_CONFIG_D,
				      st->config_d, 1);
	if (ret)
		goto exit_err_streaming;

	ret = iio_backend_ddr_enable(st->back);
	if (ret)
		goto exit_err_ddr_mode_target;

	/*
	 * From here onward mode is DDR, so reading any register is not possible
	 * anymore, including calling "ad3552r_hs_update_reg_bits" function.
	 */

	/* Set target to best high speed mode (D or QSPI). */
	ret = ad3552r_hs_set_target_io_mode_hs(st);
	if (ret)
		goto exit_err_ddr_mode;

	/* Set bus to best high speed mode (D or QSPI). */
	ret = ad3552r_hs_set_bus_io_mode_hs(st);
	if (ret)
		goto exit_err_bus_mode_target;

	/*
	 * Backend setup must be done now only, or related register values will
	 * be disrupted by previous bus accesses.
	 */
	ret = iio_backend_data_transfer_addr(st->back, val);
	if (ret)
		goto exit_err_bus_mode_target;

	ret = iio_backend_data_format_set(st->back, 0, &fmt);
	if (ret)
		goto exit_err_bus_mode_target;

	ret = iio_backend_data_stream_enable(st->back);
	if (ret)
		goto exit_err_bus_mode_target;

	return 0;

exit_err_bus_mode_target:
	/* Back to simple SPI, not using update to avoid read. */
	st->data->bus_reg_write(st->back, AD3552R_REG_ADDR_TRANSFER_REGISTER,
				FIELD_PREP(AD3552R_MASK_MULTI_IO_MODE,
					   AD3552R_SPI) |
				AD3552R_MASK_STREAM_LENGTH_KEEP_VALUE, 1);

	/*
	 * Back bus to simple SPI, this must be executed together with above
	 * target mode unwind, and can be done only after it.
	 */
	st->data->bus_set_io_mode(st->back, AD3552R_IO_MODE_SPI);

exit_err_ddr_mode:
	iio_backend_ddr_disable(st->back);

exit_err_ddr_mode_target:
	/*
	 * Back to SDR. In DDR we cannot read, whatever the mode is, so not
	 * using update.
	 */
	st->config_d &= ~AD3552R_MASK_SPI_CONFIG_DDR;
	st->data->bus_reg_write(st->back, AD3552R_REG_ADDR_INTERFACE_CONFIG_D,
				st->config_d, 1);

exit_err_streaming:
	/* Back to single instruction mode, disabling loop. */
	st->data->bus_reg_write(st->back, AD3552R_REG_ADDR_INTERFACE_CONFIG_B,
				AD3552R_MASK_SINGLE_INST |
				AD3552R_MASK_SHORT_INSTRUCTION, 1);

	return ret;
}

static int ad3552r_hs_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad3552r_hs_state *st = iio_priv(indio_dev);
	int ret;

	ret = iio_backend_data_stream_disable(st->back);
	if (ret)
		return ret;

	/*
	 * Set us to simple SPI, even if still in ddr, so to be able to write
	 * in primary region.
	 */
	ret = st->data->bus_set_io_mode(st->back, AD3552R_IO_MODE_SPI);
	if (ret)
		return ret;

	/*
	 * Back to SDR (in DDR we cannot read, whatever the mode is, so not
	 * using update).
	 */
	st->config_d &= ~AD3552R_MASK_SPI_CONFIG_DDR;
	ret = st->data->bus_reg_write(st->back,
				      AD3552R_REG_ADDR_INTERFACE_CONFIG_D,
				      st->config_d, 1);
	if (ret)
		return ret;

	ret = iio_backend_ddr_disable(st->back);
	if (ret)
		return ret;

	/*
	 * Back to simple SPI for secondary region too now, so to be able to
	 * dump/read registers there too if needed.
	 */
	ret = ad3552r_hs_update_reg_bits(st, AD3552R_REG_ADDR_TRANSFER_REGISTER,
					 AD3552R_MASK_MULTI_IO_MODE,
					 AD3552R_SPI, 1);
	if (ret)
		return ret;

	/* Back to single instruction mode, disabling loop. */
	ret = ad3552r_hs_update_reg_bits(st,
					 AD3552R_REG_ADDR_INTERFACE_CONFIG_B,
					 AD3552R_MASK_SINGLE_INST,
					 AD3552R_MASK_SINGLE_INST, 1);
	if (ret)
		return ret;

	return 0;
}

static inline int ad3552r_hs_set_output_range(struct ad3552r_hs_state *st,
					      int ch, unsigned int mode)
{
	int val;

	if (ch == 0)
		val = FIELD_PREP(AD3552R_MASK_CH0_RANGE, mode);
	else
		val = FIELD_PREP(AD3552R_MASK_CH1_RANGE, mode);

	return ad3552r_hs_update_reg_bits(st,
					  AD3552R_REG_ADDR_CH0_CH1_OUTPUT_RANGE,
					  AD3552R_MASK_CH_OUTPUT_RANGE_SEL(ch),
					  val, 1);
}

static int ad3552r_hs_reset(struct ad3552r_hs_state *st)
{
	int ret;

	st->reset_gpio = devm_gpiod_get_optional(st->dev,
						 "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(st->reset_gpio))
		return PTR_ERR(st->reset_gpio);

	if (st->reset_gpio) {
		fsleep(10);
		gpiod_set_value_cansleep(st->reset_gpio, 0);
	} else {
		ret = ad3552r_hs_update_reg_bits(st,
			AD3552R_REG_ADDR_INTERFACE_CONFIG_A,
			AD3552R_MASK_SOFTWARE_RESET,
			AD3552R_MASK_SOFTWARE_RESET, 1);
		if (ret)
			return ret;
	}
	msleep(100);

	return 0;
}

static int ad3552r_hs_scratch_pad_test(struct ad3552r_hs_state *st)
{
	int ret, val;

	ret = st->data->bus_reg_write(st->back, AD3552R_REG_ADDR_SCRATCH_PAD,
				      AD3552R_SCRATCH_PAD_TEST_VAL1, 1);
	if (ret)
		return ret;

	ret = st->data->bus_reg_read(st->back, AD3552R_REG_ADDR_SCRATCH_PAD,
				     &val, 1);
	if (ret)
		return ret;

	if (val != AD3552R_SCRATCH_PAD_TEST_VAL1)
		return dev_err_probe(st->dev, -EIO,
			"SCRATCH_PAD_TEST mismatch. Expected 0x%x, Read 0x%x\n",
			AD3552R_SCRATCH_PAD_TEST_VAL1, val);

	ret = st->data->bus_reg_write(st->back, AD3552R_REG_ADDR_SCRATCH_PAD,
				      AD3552R_SCRATCH_PAD_TEST_VAL2, 1);
	if (ret)
		return ret;

	ret = st->data->bus_reg_read(st->back, AD3552R_REG_ADDR_SCRATCH_PAD,
				     &val, 1);
	if (ret)
		return ret;

	if (val != AD3552R_SCRATCH_PAD_TEST_VAL2)
		return dev_err_probe(st->dev, -EIO,
			"SCRATCH_PAD_TEST mismatch. Expected 0x%x, Read 0x%x\n",
			AD3552R_SCRATCH_PAD_TEST_VAL2, val);

	return 0;
}

static int ad3552r_hs_setup_custom_gain(struct ad3552r_hs_state *st,
					int ch, u16 gain, u16 offset)
{
	int ret;

	ret = st->data->bus_reg_write(st->back, AD3552R_REG_ADDR_CH_OFFSET(ch),
				      offset, 1);
	if (ret)
		return ret;

	return st->data->bus_reg_write(st->back, AD3552R_REG_ADDR_CH_GAIN(ch),
				      gain, 1);
}

static int ad3552r_hs_setup(struct ad3552r_hs_state *st)
{
	u16 id;
	u16 gain = 0, offset = 0;
	u32 ch, val, range;
	int ret;

	ret = ad3552r_hs_reset(st);
	if (ret)
		return ret;

	/* HDL starts with DDR enabled, disabling it. */
	ret = iio_backend_ddr_disable(st->back);
	if (ret)
		return ret;

	ret = st->data->bus_reg_write(st->back,
				      AD3552R_REG_ADDR_INTERFACE_CONFIG_B,
				      AD3552R_MASK_SINGLE_INST |
				      AD3552R_MASK_SHORT_INSTRUCTION, 1);
	if (ret)
		return ret;

	ret = ad3552r_hs_scratch_pad_test(st);
	if (ret)
		return ret;

	/*
	 * Caching config_d, needed to restore it after streaming,
	 * and also, to detect possible DDR read, that's not allowed.
	 */
	ret = st->data->bus_reg_read(st->back,
				     AD3552R_REG_ADDR_INTERFACE_CONFIG_D,
				     &st->config_d, 1);
	if (ret)
		return ret;

	ret = ad3552r_hs_reg_read(st, AD3552R_REG_ADDR_PRODUCT_ID_L, &val, 1);
	if (ret)
		return ret;

	id = val;

	ret = ad3552r_hs_reg_read(st, AD3552R_REG_ADDR_PRODUCT_ID_H, &val, 1);
	if (ret)
		return ret;

	id |= val << 8;
	if (id != st->model_data->chip_id)
		dev_warn(st->dev,
			 "chip ID mismatch, detected 0x%x but expected 0x%x\n",
			 id, st->model_data->chip_id);

	dev_dbg(st->dev, "chip id %s detected", st->model_data->model_name);

	/* Clear reset error flag, see ad3552r manual, rev B table 38. */
	ret = st->data->bus_reg_write(st->back, AD3552R_REG_ADDR_ERR_STATUS,
				      AD3552R_MASK_RESET_STATUS, 1);
	if (ret)
		return ret;

	ret = st->data->bus_reg_write(st->back,
				      AD3552R_REG_ADDR_SH_REFERENCE_CONFIG,
				      0, 1);
	if (ret)
		return ret;

	ret = iio_backend_data_source_set(st->back, 0, IIO_BACKEND_EXTERNAL);
	if (ret)
		return ret;

	ret = iio_backend_data_source_set(st->back, 1, IIO_BACKEND_EXTERNAL);
	if (ret)
		return ret;

	ret = ad3552r_get_ref_voltage(st->dev, &val);
	if (ret < 0)
		return ret;

	val = ret;

	ret = ad3552r_hs_update_reg_bits(st,
					 AD3552R_REG_ADDR_SH_REFERENCE_CONFIG,
					 AD3552R_MASK_REFERENCE_VOLTAGE_SEL,
					 val, 1);
	if (ret)
		return ret;

	ret = ad3552r_get_drive_strength(st->dev, &val);
	if (!ret) {
		st->config_d |=
			FIELD_PREP(AD3552R_MASK_SDO_DRIVE_STRENGTH, val);

		ret = st->data->bus_reg_write(st->back,
					AD3552R_REG_ADDR_INTERFACE_CONFIG_D,
					st->config_d, 1);
		if (ret)
			return ret;
	}

	device_for_each_child_node_scoped(st->dev, child) {
		ret = fwnode_property_read_u32(child, "reg", &ch);
		if (ret)
			return dev_err_probe(st->dev, ret,
					     "reg property missing\n");

		ret = ad3552r_get_output_range(st->dev, st->model_data, child,
					       &range);
		if (ret && ret != -ENOENT)
			return ret;
		if (ret == -ENOENT) {
			ret = ad3552r_get_custom_gain(st->dev, child,
						&st->ch_data[ch].p,
						&st->ch_data[ch].n,
						&st->ch_data[ch].rfb,
						&st->ch_data[ch].gain_offset);
			if (ret)
				return ret;

			gain = ad3552r_calc_custom_gain(st->ch_data[ch].p,
						st->ch_data[ch].n,
						st->ch_data[ch].gain_offset);
			offset = abs(st->ch_data[ch].gain_offset);

			st->ch_data[ch].range_override = 1;

			ret = ad3552r_hs_setup_custom_gain(st, ch, gain,
							   offset);
			if (ret)
				return ret;
		} else {
			st->ch_data[ch].range = range;

			ret = ad3552r_hs_set_output_range(st, ch, range);
			if (ret)
				return ret;
		}

		ad3552r_calc_gain_and_offset(&st->ch_data[ch], st->model_data);
	}

	return 0;
}

static const struct iio_buffer_setup_ops ad3552r_hs_buffer_setup_ops = {
	.postenable = ad3552r_hs_buffer_postenable,
	.predisable = ad3552r_hs_buffer_predisable,
};

#define AD3552R_CHANNEL(ch) { \
	.type = IIO_VOLTAGE, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
			      BIT(IIO_CHAN_INFO_SCALE) | \
			      BIT(IIO_CHAN_INFO_OFFSET), \
	.output = 1, \
	.indexed = 1, \
	.channel = (ch), \
	.scan_index = (ch), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_BE, \
	} \
}

static const struct iio_chan_spec ad3552r_hs_channels[] = {
	AD3552R_CHANNEL(0),
	AD3552R_CHANNEL(1),
};

static const struct iio_info ad3552r_hs_info = {
	.read_raw = &ad3552r_hs_read_raw,
	.write_raw = &ad3552r_hs_write_raw,
};

static int ad3552r_hs_probe(struct platform_device *pdev)
{
	struct ad3552r_hs_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->dev = &pdev->dev;

	st->data = dev_get_platdata(st->dev);
	if (!st->data)
		return dev_err_probe(st->dev, -ENODEV, "No platform data !");

	st->back = devm_iio_backend_get(&pdev->dev, NULL);
	if (IS_ERR(st->back))
		return PTR_ERR(st->back);

	ret = devm_iio_backend_enable(&pdev->dev, st->back);
	if (ret)
		return ret;

	st->model_data = device_get_match_data(&pdev->dev);
	if (!st->model_data)
		return -ENODEV;

	indio_dev->name = "ad3552r";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->setup_ops = &ad3552r_hs_buffer_setup_ops;
	indio_dev->channels = ad3552r_hs_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad3552r_hs_channels);
	indio_dev->info = &ad3552r_hs_info;

	ret = devm_iio_backend_request_buffer(&pdev->dev, st->back, indio_dev);
	if (ret)
		return ret;

	ret = ad3552r_hs_setup(st);
	if (ret)
		return ret;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static const struct of_device_id ad3552r_hs_of_id[] = {
	{ .compatible = "adi,ad3541r", .data = &ad3541r_model_data },
	{ .compatible = "adi,ad3542r", .data = &ad3542r_model_data },
	{ .compatible = "adi,ad3551r", .data = &ad3551r_model_data },
	{ .compatible = "adi,ad3552r", .data = &ad3552r_model_data },
	{ }
};
MODULE_DEVICE_TABLE(of, ad3552r_hs_of_id);

static struct platform_driver ad3552r_hs_driver = {
	.driver = {
		.name = "ad3552r-hs",
		.of_match_table = ad3552r_hs_of_id,
	},
	.probe = ad3552r_hs_probe,
};
module_platform_driver(ad3552r_hs_driver);

MODULE_AUTHOR("Dragos Bogdan <dragos.bogdan@analog.com>");
MODULE_AUTHOR("Angelo Dureghello <adueghello@baylibre.com>");
MODULE_DESCRIPTION("AD3552R Driver - High Speed version");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_BACKEND");
MODULE_IMPORT_NS("IIO_AD3552R");
