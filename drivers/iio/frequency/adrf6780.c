// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADRF6780 driver
 *
 * Copyright 2021 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/spi/spi.h>

#include <linux/unaligned.h>

/* ADRF6780 Register Map */
#define ADRF6780_REG_CONTROL			0x00
#define ADRF6780_REG_ALARM_READBACK		0x01
#define ADRF6780_REG_ALARM_MASKS		0x02
#define ADRF6780_REG_ENABLE			0x03
#define ADRF6780_REG_LINEARIZE			0x04
#define ADRF6780_REG_LO_PATH			0x05
#define ADRF6780_REG_ADC_CONTROL		0x06
#define ADRF6780_REG_ADC_OUTPUT			0x0C

/* ADRF6780_REG_CONTROL Map */
#define ADRF6780_PARITY_EN_MSK			BIT(15)
#define ADRF6780_SOFT_RESET_MSK			BIT(14)
#define ADRF6780_CHIP_ID_MSK			GENMASK(11, 4)
#define ADRF6780_CHIP_ID			0xA
#define ADRF6780_CHIP_REVISION_MSK		GENMASK(3, 0)

/* ADRF6780_REG_ALARM_READBACK Map */
#define ADRF6780_PARITY_ERROR_MSK		BIT(15)
#define ADRF6780_TOO_FEW_ERRORS_MSK		BIT(14)
#define ADRF6780_TOO_MANY_ERRORS_MSK		BIT(13)
#define ADRF6780_ADDRESS_RANGE_ERROR_MSK	BIT(12)

/* ADRF6780_REG_ENABLE Map */
#define ADRF6780_VGA_BUFFER_EN_MSK		BIT(8)
#define ADRF6780_DETECTOR_EN_MSK		BIT(7)
#define ADRF6780_LO_BUFFER_EN_MSK		BIT(6)
#define ADRF6780_IF_MODE_EN_MSK			BIT(5)
#define ADRF6780_IQ_MODE_EN_MSK			BIT(4)
#define ADRF6780_LO_X2_EN_MSK			BIT(3)
#define ADRF6780_LO_PPF_EN_MSK			BIT(2)
#define ADRF6780_LO_EN_MSK			BIT(1)
#define ADRF6780_UC_BIAS_EN_MSK			BIT(0)

/* ADRF6780_REG_LINEARIZE Map */
#define ADRF6780_RDAC_LINEARIZE_MSK		GENMASK(7, 0)

/* ADRF6780_REG_LO_PATH Map */
#define ADRF6780_LO_SIDEBAND_MSK		BIT(10)
#define ADRF6780_Q_PATH_PHASE_ACCURACY_MSK	GENMASK(7, 4)
#define ADRF6780_I_PATH_PHASE_ACCURACY_MSK	GENMASK(3, 0)

/* ADRF6780_REG_ADC_CONTROL Map */
#define ADRF6780_VDET_OUTPUT_SELECT_MSK		BIT(3)
#define ADRF6780_ADC_START_MSK			BIT(2)
#define ADRF6780_ADC_EN_MSK			BIT(1)
#define ADRF6780_ADC_CLOCK_EN_MSK		BIT(0)

/* ADRF6780_REG_ADC_OUTPUT Map */
#define ADRF6780_ADC_STATUS_MSK			BIT(8)
#define ADRF6780_ADC_VALUE_MSK			GENMASK(7, 0)

struct adrf6780_state {
	struct spi_device	*spi;
	struct clk		*clkin;
	/* Protect against concurrent accesses to the device */
	struct mutex		lock;
	bool			vga_buff_en;
	bool			lo_buff_en;
	bool			if_mode_en;
	bool			iq_mode_en;
	bool			lo_x2_en;
	bool			lo_ppf_en;
	bool			lo_en;
	bool			uc_bias_en;
	bool			lo_sideband;
	bool			vdet_out_en;
	u8			data[3] __aligned(IIO_DMA_MINALIGN);
};

static int __adrf6780_spi_read(struct adrf6780_state *st, unsigned int reg,
			       unsigned int *val)
{
	int ret;
	struct spi_transfer t = {0};

	st->data[0] = 0x80 | (reg << 1);
	st->data[1] = 0x0;
	st->data[2] = 0x0;

	t.rx_buf = &st->data[0];
	t.tx_buf = &st->data[0];
	t.len = 3;

	ret = spi_sync_transfer(st->spi, &t, 1);
	if (ret)
		return ret;

	*val = (get_unaligned_be24(&st->data[0]) >> 1) & GENMASK(15, 0);

	return ret;
}

static int adrf6780_spi_read(struct adrf6780_state *st, unsigned int reg,
			     unsigned int *val)
{
	int ret;

	mutex_lock(&st->lock);
	ret = __adrf6780_spi_read(st, reg, val);
	mutex_unlock(&st->lock);

	return ret;
}

static int __adrf6780_spi_write(struct adrf6780_state *st,
				unsigned int reg,
				unsigned int val)
{
	put_unaligned_be24((val << 1) | (reg << 17), &st->data[0]);

	return spi_write(st->spi, &st->data[0], 3);
}

static int adrf6780_spi_write(struct adrf6780_state *st, unsigned int reg,
			      unsigned int val)
{
	int ret;

	mutex_lock(&st->lock);
	ret = __adrf6780_spi_write(st, reg, val);
	mutex_unlock(&st->lock);

	return ret;
}

static int __adrf6780_spi_update_bits(struct adrf6780_state *st,
				      unsigned int reg, unsigned int mask,
				      unsigned int val)
{
	int ret;
	unsigned int data, temp;

	ret = __adrf6780_spi_read(st, reg, &data);
	if (ret)
		return ret;

	temp = (data & ~mask) | (val & mask);

	return __adrf6780_spi_write(st, reg, temp);
}

static int adrf6780_spi_update_bits(struct adrf6780_state *st, unsigned int reg,
				    unsigned int mask, unsigned int val)
{
	int ret;

	mutex_lock(&st->lock);
	ret = __adrf6780_spi_update_bits(st, reg, mask, val);
	mutex_unlock(&st->lock);

	return ret;
}

static int adrf6780_read_adc_raw(struct adrf6780_state *st, unsigned int *read_val)
{
	int ret;

	mutex_lock(&st->lock);

	ret = __adrf6780_spi_update_bits(st, ADRF6780_REG_ADC_CONTROL,
					 ADRF6780_ADC_EN_MSK |
					 ADRF6780_ADC_CLOCK_EN_MSK |
					 ADRF6780_ADC_START_MSK,
					 FIELD_PREP(ADRF6780_ADC_EN_MSK, 1) |
					 FIELD_PREP(ADRF6780_ADC_CLOCK_EN_MSK, 1) |
					 FIELD_PREP(ADRF6780_ADC_START_MSK, 1));
	if (ret)
		goto exit;

	/* Recommended delay for the ADC to be ready*/
	usleep_range(200, 250);

	ret = __adrf6780_spi_read(st, ADRF6780_REG_ADC_OUTPUT, read_val);
	if (ret)
		goto exit;

	if (!(*read_val & ADRF6780_ADC_STATUS_MSK)) {
		ret = -EINVAL;
		goto exit;
	}

	ret = __adrf6780_spi_update_bits(st, ADRF6780_REG_ADC_CONTROL,
					 ADRF6780_ADC_START_MSK,
					 FIELD_PREP(ADRF6780_ADC_START_MSK, 0));
	if (ret)
		goto exit;

	ret = __adrf6780_spi_read(st, ADRF6780_REG_ADC_OUTPUT, read_val);

exit:
	mutex_unlock(&st->lock);
	return ret;
}

static int adrf6780_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long info)
{
	struct adrf6780_state *dev = iio_priv(indio_dev);
	unsigned int data;
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = adrf6780_read_adc_raw(dev, &data);
		if (ret)
			return ret;

		*val = data & ADRF6780_ADC_VALUE_MSK;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		ret = adrf6780_spi_read(dev, ADRF6780_REG_LINEARIZE, &data);
		if (ret)
			return ret;

		*val = data & ADRF6780_RDAC_LINEARIZE_MSK;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PHASE:
		ret = adrf6780_spi_read(dev, ADRF6780_REG_LO_PATH, &data);
		if (ret)
			return ret;

		switch (chan->channel2) {
		case IIO_MOD_I:
			*val = data & ADRF6780_I_PATH_PHASE_ACCURACY_MSK;

			return IIO_VAL_INT;
		case IIO_MOD_Q:
			*val = FIELD_GET(ADRF6780_Q_PATH_PHASE_ACCURACY_MSK,
					 data);

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int adrf6780_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long info)
{
	struct adrf6780_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		return adrf6780_spi_write(st, ADRF6780_REG_LINEARIZE, val);
	case IIO_CHAN_INFO_PHASE:
		switch (chan->channel2) {
		case IIO_MOD_I:
			return adrf6780_spi_update_bits(st,
				ADRF6780_REG_LO_PATH,
				ADRF6780_I_PATH_PHASE_ACCURACY_MSK,
				FIELD_PREP(ADRF6780_I_PATH_PHASE_ACCURACY_MSK, val));
		case IIO_MOD_Q:
			return adrf6780_spi_update_bits(st,
				ADRF6780_REG_LO_PATH,
				ADRF6780_Q_PATH_PHASE_ACCURACY_MSK,
				FIELD_PREP(ADRF6780_Q_PATH_PHASE_ACCURACY_MSK, val));
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int adrf6780_reg_access(struct iio_dev *indio_dev,
			       unsigned int reg,
			       unsigned int write_val,
			       unsigned int *read_val)
{
	struct adrf6780_state *st = iio_priv(indio_dev);

	if (read_val)
		return adrf6780_spi_read(st, reg, read_val);
	else
		return adrf6780_spi_write(st, reg, write_val);
}

static const struct iio_info adrf6780_info = {
	.read_raw = adrf6780_read_raw,
	.write_raw = adrf6780_write_raw,
	.debugfs_reg_access = &adrf6780_reg_access,
};

#define ADRF6780_CHAN_ADC(_channel) {			\
	.type = IIO_ALTVOLTAGE,				\
	.output = 0,					\
	.indexed = 1,					\
	.channel = _channel,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)	\
}

#define ADRF6780_CHAN_RDAC(_channel) {			\
	.type = IIO_ALTVOLTAGE,				\
	.output = 1,					\
	.indexed = 1,					\
	.channel = _channel,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_SCALE)	\
}

#define ADRF6780_CHAN_IQ_PHASE(_channel, rf_comp) {		\
	.type = IIO_ALTVOLTAGE,					\
	.modified = 1,						\
	.output = 1,						\
	.indexed = 1,						\
	.channel2 = IIO_MOD_##rf_comp,				\
	.channel = _channel,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PHASE)		\
}

static const struct iio_chan_spec adrf6780_channels[] = {
	ADRF6780_CHAN_ADC(0),
	ADRF6780_CHAN_RDAC(0),
	ADRF6780_CHAN_IQ_PHASE(0, I),
	ADRF6780_CHAN_IQ_PHASE(0, Q),
};

static int adrf6780_reset(struct adrf6780_state *st)
{
	int ret;
	struct spi_device *spi = st->spi;

	ret = __adrf6780_spi_update_bits(st, ADRF6780_REG_CONTROL,
					 ADRF6780_SOFT_RESET_MSK,
					 FIELD_PREP(ADRF6780_SOFT_RESET_MSK, 1));
	if (ret) {
		dev_err(&spi->dev, "ADRF6780 SPI software reset failed.\n");
		return ret;
	}

	ret = __adrf6780_spi_update_bits(st, ADRF6780_REG_CONTROL,
					 ADRF6780_SOFT_RESET_MSK,
					 FIELD_PREP(ADRF6780_SOFT_RESET_MSK, 0));
	if (ret) {
		dev_err(&spi->dev, "ADRF6780 SPI software reset disable failed.\n");
		return ret;
	}

	return 0;
}

static int adrf6780_init(struct adrf6780_state *st)
{
	int ret;
	unsigned int chip_id, enable_reg, enable_reg_msk;
	struct spi_device *spi = st->spi;

	/* Perform a software reset */
	ret = adrf6780_reset(st);
	if (ret)
		return ret;

	ret = __adrf6780_spi_read(st, ADRF6780_REG_CONTROL, &chip_id);
	if (ret)
		return ret;

	chip_id = FIELD_GET(ADRF6780_CHIP_ID_MSK, chip_id);
	if (chip_id != ADRF6780_CHIP_ID) {
		dev_err(&spi->dev, "ADRF6780 Invalid Chip ID.\n");
		return -EINVAL;
	}

	enable_reg_msk = ADRF6780_VGA_BUFFER_EN_MSK |
			ADRF6780_DETECTOR_EN_MSK |
			ADRF6780_LO_BUFFER_EN_MSK |
			ADRF6780_IF_MODE_EN_MSK |
			ADRF6780_IQ_MODE_EN_MSK |
			ADRF6780_LO_X2_EN_MSK |
			ADRF6780_LO_PPF_EN_MSK |
			ADRF6780_LO_EN_MSK |
			ADRF6780_UC_BIAS_EN_MSK;

	enable_reg = FIELD_PREP(ADRF6780_VGA_BUFFER_EN_MSK, st->vga_buff_en) |
			FIELD_PREP(ADRF6780_DETECTOR_EN_MSK, 1) |
			FIELD_PREP(ADRF6780_LO_BUFFER_EN_MSK, st->lo_buff_en) |
			FIELD_PREP(ADRF6780_IF_MODE_EN_MSK, st->if_mode_en) |
			FIELD_PREP(ADRF6780_IQ_MODE_EN_MSK, st->iq_mode_en) |
			FIELD_PREP(ADRF6780_LO_X2_EN_MSK, st->lo_x2_en) |
			FIELD_PREP(ADRF6780_LO_PPF_EN_MSK, st->lo_ppf_en) |
			FIELD_PREP(ADRF6780_LO_EN_MSK, st->lo_en) |
			FIELD_PREP(ADRF6780_UC_BIAS_EN_MSK, st->uc_bias_en);

	ret = __adrf6780_spi_update_bits(st, ADRF6780_REG_ENABLE,
					 enable_reg_msk, enable_reg);
	if (ret)
		return ret;

	ret = __adrf6780_spi_update_bits(st, ADRF6780_REG_LO_PATH,
					 ADRF6780_LO_SIDEBAND_MSK,
					 FIELD_PREP(ADRF6780_LO_SIDEBAND_MSK, st->lo_sideband));
	if (ret)
		return ret;

	return __adrf6780_spi_update_bits(st, ADRF6780_REG_ADC_CONTROL,
		ADRF6780_VDET_OUTPUT_SELECT_MSK,
		FIELD_PREP(ADRF6780_VDET_OUTPUT_SELECT_MSK, st->vdet_out_en));
}

static void adrf6780_properties_parse(struct adrf6780_state *st)
{
	struct spi_device *spi = st->spi;

	st->vga_buff_en = device_property_read_bool(&spi->dev, "adi,vga-buff-en");
	st->lo_buff_en = device_property_read_bool(&spi->dev, "adi,lo-buff-en");
	st->if_mode_en = device_property_read_bool(&spi->dev, "adi,if-mode-en");
	st->iq_mode_en = device_property_read_bool(&spi->dev, "adi,iq-mode-en");
	st->lo_x2_en = device_property_read_bool(&spi->dev, "adi,lo-x2-en");
	st->lo_ppf_en = device_property_read_bool(&spi->dev, "adi,lo-ppf-en");
	st->lo_en = device_property_read_bool(&spi->dev, "adi,lo-en");
	st->uc_bias_en = device_property_read_bool(&spi->dev, "adi,uc-bias-en");
	st->lo_sideband = device_property_read_bool(&spi->dev, "adi,lo-sideband");
	st->vdet_out_en = device_property_read_bool(&spi->dev, "adi,vdet-out-en");
}

static void adrf6780_powerdown(void *data)
{
	/* Disable all components in the Enable Register */
	adrf6780_spi_write(data, ADRF6780_REG_ENABLE, 0x0);
}

static int adrf6780_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct adrf6780_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	indio_dev->info = &adrf6780_info;
	indio_dev->name = "adrf6780";
	indio_dev->channels = adrf6780_channels;
	indio_dev->num_channels = ARRAY_SIZE(adrf6780_channels);

	st->spi = spi;

	adrf6780_properties_parse(st);

	st->clkin = devm_clk_get_enabled(&spi->dev, "lo_in");
	if (IS_ERR(st->clkin))
		return dev_err_probe(&spi->dev, PTR_ERR(st->clkin),
				     "failed to get the LO input clock\n");

	mutex_init(&st->lock);

	ret = adrf6780_init(st);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, adrf6780_powerdown, st);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id adrf6780_id[] = {
	{ "adrf6780", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, adrf6780_id);

static const struct of_device_id adrf6780_of_match[] = {
	{ .compatible = "adi,adrf6780" },
	{ }
};
MODULE_DEVICE_TABLE(of, adrf6780_of_match);

static struct spi_driver adrf6780_driver = {
	.driver = {
		.name = "adrf6780",
		.of_match_table = adrf6780_of_match,
	},
	.probe = adrf6780_probe,
	.id_table = adrf6780_id,
};
module_spi_driver(adrf6780_driver);

MODULE_AUTHOR("Antoniu Miclaus <antoniu.miclaus@analog.com");
MODULE_DESCRIPTION("Analog Devices ADRF6780");
MODULE_LICENSE("GPL v2");
