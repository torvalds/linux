// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD7293 driver
 *
 * Copyright 2021 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <asm/unaligned.h>

#define AD7293_R1B				BIT(16)
#define AD7293_R2B				BIT(17)
#define AD7293_PAGE_ADDR_MSK			GENMASK(15, 8)
#define AD7293_PAGE(x)				FIELD_PREP(AD7293_PAGE_ADDR_MSK, x)

/* AD7293 Register Map Common */
#define AD7293_REG_NO_OP			(AD7293_R1B | AD7293_PAGE(0x0) | 0x0)
#define AD7293_REG_PAGE_SELECT			(AD7293_R1B | AD7293_PAGE(0x0) | 0x1)
#define AD7293_REG_CONV_CMD			(AD7293_R2B | AD7293_PAGE(0x0) | 0x2)
#define AD7293_REG_RESULT			(AD7293_R1B | AD7293_PAGE(0x0) | 0x3)
#define AD7293_REG_DAC_EN			(AD7293_R1B | AD7293_PAGE(0x0) | 0x4)
#define AD7293_REG_DEVICE_ID			(AD7293_R2B | AD7293_PAGE(0x0) | 0xC)
#define AD7293_REG_SOFT_RESET			(AD7293_R2B | AD7293_PAGE(0x0) | 0xF)

/* AD7293 Register Map Page 0x0 */
#define AD7293_REG_VIN0				(AD7293_R2B | AD7293_PAGE(0x0) | 0x10)
#define AD7293_REG_VIN1				(AD7293_R2B | AD7293_PAGE(0x0) | 0x11)
#define AD7293_REG_VIN2				(AD7293_R2B | AD7293_PAGE(0x0) | 0x12)
#define AD7293_REG_VIN3				(AD7293_R2B | AD7293_PAGE(0x0) | 0x13)
#define AD7293_REG_TSENSE_INT			(AD7293_R2B | AD7293_PAGE(0x0) | 0x20)
#define AD7293_REG_TSENSE_D0			(AD7293_R2B | AD7293_PAGE(0x0) | 0x21)
#define AD7293_REG_TSENSE_D1			(AD7293_R2B | AD7293_PAGE(0x0) | 0x22)
#define AD7293_REG_ISENSE_0			(AD7293_R2B | AD7293_PAGE(0x0) | 0x28)
#define AD7293_REG_ISENSE_1			(AD7293_R2B | AD7293_PAGE(0x0) | 0x29)
#define AD7293_REG_ISENSE_2			(AD7293_R2B | AD7293_PAGE(0x0) | 0x2A)
#define AD7293_REG_ISENSE_3			(AD7293_R2B | AD7293_PAGE(0x0) | 0x2B)
#define AD7293_REG_UNI_VOUT0			(AD7293_R2B | AD7293_PAGE(0x0) | 0x30)
#define AD7293_REG_UNI_VOUT1			(AD7293_R2B | AD7293_PAGE(0x0) | 0x31)
#define AD7293_REG_UNI_VOUT2			(AD7293_R2B | AD7293_PAGE(0x0) | 0x32)
#define AD7293_REG_UNI_VOUT3			(AD7293_R2B | AD7293_PAGE(0x0) | 0x33)
#define AD7293_REG_BI_VOUT0			(AD7293_R2B | AD7293_PAGE(0x0) | 0x34)
#define AD7293_REG_BI_VOUT1			(AD7293_R2B | AD7293_PAGE(0x0) | 0x35)
#define AD7293_REG_BI_VOUT2			(AD7293_R2B | AD7293_PAGE(0x0) | 0x36)
#define AD7293_REG_BI_VOUT3			(AD7293_R2B | AD7293_PAGE(0x0) | 0x37)

/* AD7293 Register Map Page 0x2 */
#define AD7293_REG_DIGITAL_OUT_EN		(AD7293_R2B | AD7293_PAGE(0x2) | 0x11)
#define AD7293_REG_DIGITAL_INOUT_FUNC		(AD7293_R2B | AD7293_PAGE(0x2) | 0x12)
#define AD7293_REG_DIGITAL_FUNC_POL		(AD7293_R2B | AD7293_PAGE(0x2) | 0x13)
#define AD7293_REG_GENERAL			(AD7293_R2B | AD7293_PAGE(0x2) | 0x14)
#define AD7293_REG_VINX_RANGE0			(AD7293_R2B | AD7293_PAGE(0x2) | 0x15)
#define AD7293_REG_VINX_RANGE1			(AD7293_R2B | AD7293_PAGE(0x2) | 0x16)
#define AD7293_REG_VINX_DIFF_SE			(AD7293_R2B | AD7293_PAGE(0x2) | 0x17)
#define AD7293_REG_VINX_FILTER			(AD7293_R2B | AD7293_PAGE(0x2) | 0x18)
#define AD7293_REG_BG_EN			(AD7293_R2B | AD7293_PAGE(0x2) | 0x19)
#define AD7293_REG_CONV_DELAY			(AD7293_R2B | AD7293_PAGE(0x2) | 0x1A)
#define AD7293_REG_TSENSE_BG_EN			(AD7293_R2B | AD7293_PAGE(0x2) | 0x1B)
#define AD7293_REG_ISENSE_BG_EN			(AD7293_R2B | AD7293_PAGE(0x2) | 0x1C)
#define AD7293_REG_ISENSE_GAIN			(AD7293_R2B | AD7293_PAGE(0x2) | 0x1D)
#define AD7293_REG_DAC_SNOOZE_O			(AD7293_R2B | AD7293_PAGE(0x2) | 0x1F)
#define AD7293_REG_DAC_SNOOZE_1			(AD7293_R2B | AD7293_PAGE(0x2) | 0x20)
#define AD7293_REG_RSX_MON_BG_EN		(AD7293_R2B | AD7293_PAGE(0x2) | 0x23)
#define AD7293_REG_INTEGR_CL			(AD7293_R2B | AD7293_PAGE(0x2) | 0x28)
#define AD7293_REG_PA_ON_CTRL			(AD7293_R2B | AD7293_PAGE(0x2) | 0x29)
#define AD7293_REG_RAMP_TIME_0			(AD7293_R2B | AD7293_PAGE(0x2) | 0x2A)
#define AD7293_REG_RAMP_TIME_1			(AD7293_R2B | AD7293_PAGE(0x2) | 0x2B)
#define AD7293_REG_RAMP_TIME_2			(AD7293_R2B | AD7293_PAGE(0x2) | 0x2C)
#define AD7293_REG_RAMP_TIME_3			(AD7293_R2B | AD7293_PAGE(0x2) | 0x2D)
#define AD7293_REG_CL_FR_IT			(AD7293_R2B | AD7293_PAGE(0x2) | 0x2E)
#define AD7293_REG_INTX_AVSS_AVDD		(AD7293_R2B | AD7293_PAGE(0x2) | 0x2F)

/* AD7293 Register Map Page 0x3 */
#define AD7293_REG_VINX_SEQ			(AD7293_R2B | AD7293_PAGE(0x3) | 0x10)
#define AD7293_REG_ISENSEX_TSENSEX_SEQ		(AD7293_R2B | AD7293_PAGE(0x3) | 0x11)
#define AD7293_REG_RSX_MON_BI_VOUTX_SEQ		(AD7293_R2B | AD7293_PAGE(0x3) | 0x12)

/* AD7293 Register Map Page 0xE */
#define AD7293_REG_VIN0_OFFSET			(AD7293_R1B | AD7293_PAGE(0xE) | 0x10)
#define AD7293_REG_VIN1_OFFSET			(AD7293_R1B | AD7293_PAGE(0xE) | 0x11)
#define AD7293_REG_VIN2_OFFSET			(AD7293_R1B | AD7293_PAGE(0xE) | 0x12)
#define AD7293_REG_VIN3_OFFSET			(AD7293_R1B | AD7293_PAGE(0xE) | 0x13)
#define AD7293_REG_TSENSE_INT_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x20)
#define AD7293_REG_TSENSE_D0_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x21)
#define AD7293_REG_TSENSE_D1_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x22)
#define AD7293_REG_ISENSE0_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x28)
#define AD7293_REG_ISENSE1_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x29)
#define AD7293_REG_ISENSE2_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x2A)
#define AD7293_REG_ISENSE3_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x2B)
#define AD7293_REG_UNI_VOUT0_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x30)
#define AD7293_REG_UNI_VOUT1_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x31)
#define AD7293_REG_UNI_VOUT2_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x32)
#define AD7293_REG_UNI_VOUT3_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x33)
#define AD7293_REG_BI_VOUT0_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x34)
#define AD7293_REG_BI_VOUT1_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x35)
#define AD7293_REG_BI_VOUT2_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x36)
#define AD7293_REG_BI_VOUT3_OFFSET		(AD7293_R1B | AD7293_PAGE(0xE) | 0x37)

/* AD7293 Miscellaneous Definitions */
#define AD7293_READ				BIT(7)
#define AD7293_TRANSF_LEN_MSK			GENMASK(17, 16)

#define AD7293_REG_ADDR_MSK			GENMASK(7, 0)
#define AD7293_REG_VOUT_OFFSET_MSK		GENMASK(5, 4)
#define AD7293_REG_DATA_RAW_MSK			GENMASK(15, 4)
#define AD7293_REG_VINX_RANGE_GET_CH_MSK(x, ch)	(((x) >> (ch)) & 0x1)
#define AD7293_REG_VINX_RANGE_SET_CH_MSK(x, ch)	(((x) & 0x1) << (ch))
#define AD7293_CHIP_ID				0x18

enum ad7293_ch_type {
	AD7293_ADC_VINX,
	AD7293_ADC_TSENSE,
	AD7293_ADC_ISENSE,
	AD7293_DAC,
};

enum ad7293_max_offset {
	AD7293_TSENSE_MIN_OFFSET_CH = 4,
	AD7293_ISENSE_MIN_OFFSET_CH = 7,
	AD7293_VOUT_MIN_OFFSET_CH = 11,
	AD7293_VOUT_MAX_OFFSET_CH = 18,
};

static const int dac_offset_table[] = {0, 1, 2};

static const int isense_gain_table[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

static const int adc_range_table[] = {0, 1, 2, 3};

struct ad7293_state {
	struct spi_device *spi;
	/* Protect against concurrent accesses to the device, page selection and data content */
	struct mutex lock;
	struct gpio_desc *gpio_reset;
	struct regulator *reg_avdd;
	struct regulator *reg_vdrive;
	u8 page_select;
	u8 data[3] __aligned(IIO_DMA_MINALIGN);
};

static int ad7293_page_select(struct ad7293_state *st, unsigned int reg)
{
	int ret;

	if (st->page_select != FIELD_GET(AD7293_PAGE_ADDR_MSK, reg)) {
		st->data[0] = FIELD_GET(AD7293_REG_ADDR_MSK, AD7293_REG_PAGE_SELECT);
		st->data[1] = FIELD_GET(AD7293_PAGE_ADDR_MSK, reg);

		ret = spi_write(st->spi, &st->data[0], 2);
		if (ret)
			return ret;

		st->page_select = FIELD_GET(AD7293_PAGE_ADDR_MSK, reg);
	}

	return 0;
}

static int __ad7293_spi_read(struct ad7293_state *st, unsigned int reg,
			     u16 *val)
{
	int ret;
	unsigned int length;
	struct spi_transfer t = {0};

	length = FIELD_GET(AD7293_TRANSF_LEN_MSK, reg);

	ret = ad7293_page_select(st, reg);
	if (ret)
		return ret;

	st->data[0] = AD7293_READ | FIELD_GET(AD7293_REG_ADDR_MSK, reg);
	st->data[1] = 0x0;
	st->data[2] = 0x0;

	t.tx_buf = &st->data[0];
	t.rx_buf = &st->data[0];
	t.len = length + 1;

	ret = spi_sync_transfer(st->spi, &t, 1);
	if (ret)
		return ret;

	if (length == 1)
		*val = st->data[1];
	else
		*val = get_unaligned_be16(&st->data[1]);

	return 0;
}

static int ad7293_spi_read(struct ad7293_state *st, unsigned int reg,
			   u16 *val)
{
	int ret;

	mutex_lock(&st->lock);
	ret = __ad7293_spi_read(st, reg, val);
	mutex_unlock(&st->lock);

	return ret;
}

static int __ad7293_spi_write(struct ad7293_state *st, unsigned int reg,
			      u16 val)
{
	int ret;
	unsigned int length;

	length = FIELD_GET(AD7293_TRANSF_LEN_MSK, reg);

	ret = ad7293_page_select(st, reg);
	if (ret)
		return ret;

	st->data[0] = FIELD_GET(AD7293_REG_ADDR_MSK, reg);

	if (length == 1)
		st->data[1] = val;
	else
		put_unaligned_be16(val, &st->data[1]);

	return spi_write(st->spi, &st->data[0], length + 1);
}

static int ad7293_spi_write(struct ad7293_state *st, unsigned int reg,
			    u16 val)
{
	int ret;

	mutex_lock(&st->lock);
	ret = __ad7293_spi_write(st, reg, val);
	mutex_unlock(&st->lock);

	return ret;
}

static int __ad7293_spi_update_bits(struct ad7293_state *st, unsigned int reg,
				    u16 mask, u16 val)
{
	int ret;
	u16 data, temp;

	ret = __ad7293_spi_read(st, reg, &data);
	if (ret)
		return ret;

	temp = (data & ~mask) | (val & mask);

	return __ad7293_spi_write(st, reg, temp);
}

static int ad7293_spi_update_bits(struct ad7293_state *st, unsigned int reg,
				  u16 mask, u16 val)
{
	int ret;

	mutex_lock(&st->lock);
	ret = __ad7293_spi_update_bits(st, reg, mask, val);
	mutex_unlock(&st->lock);

	return ret;
}

static int ad7293_adc_get_scale(struct ad7293_state *st, unsigned int ch,
				u16 *range)
{
	int ret;
	u16 data;

	mutex_lock(&st->lock);

	ret = __ad7293_spi_read(st, AD7293_REG_VINX_RANGE1, &data);
	if (ret)
		goto exit;

	*range = AD7293_REG_VINX_RANGE_GET_CH_MSK(data, ch);

	ret = __ad7293_spi_read(st, AD7293_REG_VINX_RANGE0, &data);
	if (ret)
		goto exit;

	*range |= AD7293_REG_VINX_RANGE_GET_CH_MSK(data, ch) << 1;

exit:
	mutex_unlock(&st->lock);

	return ret;
}

static int ad7293_adc_set_scale(struct ad7293_state *st, unsigned int ch,
				u16 range)
{
	int ret;
	unsigned int ch_msk = BIT(ch);

	mutex_lock(&st->lock);
	ret = __ad7293_spi_update_bits(st, AD7293_REG_VINX_RANGE1, ch_msk,
				       AD7293_REG_VINX_RANGE_SET_CH_MSK(range, ch));
	if (ret)
		goto exit;

	ret = __ad7293_spi_update_bits(st, AD7293_REG_VINX_RANGE0, ch_msk,
				       AD7293_REG_VINX_RANGE_SET_CH_MSK((range >> 1), ch));

exit:
	mutex_unlock(&st->lock);

	return ret;
}

static int ad7293_get_offset(struct ad7293_state *st, unsigned int ch,
			     u16 *offset)
{
	if (ch < AD7293_TSENSE_MIN_OFFSET_CH)
		return ad7293_spi_read(st, AD7293_REG_VIN0_OFFSET + ch, offset);
	else if (ch < AD7293_ISENSE_MIN_OFFSET_CH)
		return ad7293_spi_read(st, AD7293_REG_TSENSE_INT_OFFSET + (ch - 4), offset);
	else if (ch < AD7293_VOUT_MIN_OFFSET_CH)
		return ad7293_spi_read(st, AD7293_REG_ISENSE0_OFFSET + (ch - 7), offset);
	else if (ch <= AD7293_VOUT_MAX_OFFSET_CH)
		return ad7293_spi_read(st, AD7293_REG_UNI_VOUT0_OFFSET + (ch - 11), offset);

	return -EINVAL;
}

static int ad7293_set_offset(struct ad7293_state *st, unsigned int ch,
			     u16 offset)
{
	if (ch < AD7293_TSENSE_MIN_OFFSET_CH)
		return ad7293_spi_write(st, AD7293_REG_VIN0_OFFSET + ch,
					offset);
	else if (ch < AD7293_ISENSE_MIN_OFFSET_CH)
		return ad7293_spi_write(st,
					AD7293_REG_TSENSE_INT_OFFSET +
					(ch - AD7293_TSENSE_MIN_OFFSET_CH),
					offset);
	else if (ch < AD7293_VOUT_MIN_OFFSET_CH)
		return ad7293_spi_write(st,
					AD7293_REG_ISENSE0_OFFSET +
					(ch - AD7293_ISENSE_MIN_OFFSET_CH),
					offset);
	else if (ch <= AD7293_VOUT_MAX_OFFSET_CH)
		return ad7293_spi_update_bits(st,
					      AD7293_REG_UNI_VOUT0_OFFSET +
					      (ch - AD7293_VOUT_MIN_OFFSET_CH),
					      AD7293_REG_VOUT_OFFSET_MSK,
					      FIELD_PREP(AD7293_REG_VOUT_OFFSET_MSK, offset));

	return -EINVAL;
}

static int ad7293_isense_set_scale(struct ad7293_state *st, unsigned int ch,
				   u16 gain)
{
	unsigned int ch_msk = (0xf << (4 * ch));

	return ad7293_spi_update_bits(st, AD7293_REG_ISENSE_GAIN, ch_msk,
				      gain << (4 * ch));
}

static int ad7293_isense_get_scale(struct ad7293_state *st, unsigned int ch,
				   u16 *gain)
{
	int ret;

	ret = ad7293_spi_read(st, AD7293_REG_ISENSE_GAIN, gain);
	if (ret)
		return ret;

	*gain = (*gain >> (4 * ch)) & 0xf;

	return ret;
}

static int ad7293_dac_write_raw(struct ad7293_state *st, unsigned int ch,
				u16 raw)
{
	int ret;

	mutex_lock(&st->lock);

	ret = __ad7293_spi_update_bits(st, AD7293_REG_DAC_EN, BIT(ch), BIT(ch));
	if (ret)
		goto exit;

	ret =  __ad7293_spi_write(st, AD7293_REG_UNI_VOUT0 + ch,
				  FIELD_PREP(AD7293_REG_DATA_RAW_MSK, raw));

exit:
	mutex_unlock(&st->lock);

	return ret;
}

static int ad7293_ch_read_raw(struct ad7293_state *st, enum ad7293_ch_type type,
			      unsigned int ch, u16 *raw)
{
	int ret;
	unsigned int reg_wr, reg_rd, data_wr;

	switch (type) {
	case AD7293_ADC_VINX:
		reg_wr = AD7293_REG_VINX_SEQ;
		reg_rd = AD7293_REG_VIN0 + ch;
		data_wr = BIT(ch);

		break;
	case AD7293_ADC_TSENSE:
		reg_wr = AD7293_REG_ISENSEX_TSENSEX_SEQ;
		reg_rd = AD7293_REG_TSENSE_INT + ch;
		data_wr = BIT(ch);

		break;
	case AD7293_ADC_ISENSE:
		reg_wr = AD7293_REG_ISENSEX_TSENSEX_SEQ;
		reg_rd = AD7293_REG_ISENSE_0 + ch;
		data_wr = BIT(ch) << 8;

		break;
	case AD7293_DAC:
		reg_rd = AD7293_REG_UNI_VOUT0 + ch;

		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&st->lock);

	if (type != AD7293_DAC) {
		if (type == AD7293_ADC_TSENSE) {
			ret = __ad7293_spi_write(st, AD7293_REG_TSENSE_BG_EN,
						 BIT(ch));
			if (ret)
				goto exit;

			usleep_range(9000, 9900);
		} else if (type == AD7293_ADC_ISENSE) {
			ret = __ad7293_spi_write(st, AD7293_REG_ISENSE_BG_EN,
						 BIT(ch));
			if (ret)
				goto exit;

			usleep_range(2000, 7000);
		}

		ret = __ad7293_spi_write(st, reg_wr, data_wr);
		if (ret)
			goto exit;

		ret = __ad7293_spi_write(st, AD7293_REG_CONV_CMD, 0x82);
		if (ret)
			goto exit;
	}

	ret = __ad7293_spi_read(st, reg_rd, raw);

	*raw = FIELD_GET(AD7293_REG_DATA_RAW_MSK, *raw);

exit:
	mutex_unlock(&st->lock);

	return ret;
}

static int ad7293_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad7293_state *st = iio_priv(indio_dev);
	int ret;
	u16 data;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (chan->output)
				ret =  ad7293_ch_read_raw(st, AD7293_DAC,
							  chan->channel, &data);
			else
				ret =  ad7293_ch_read_raw(st, AD7293_ADC_VINX,
							  chan->channel, &data);

			break;
		case IIO_CURRENT:
			ret =  ad7293_ch_read_raw(st, AD7293_ADC_ISENSE,
						  chan->channel, &data);

			break;
		case IIO_TEMP:
			ret =  ad7293_ch_read_raw(st, AD7293_ADC_TSENSE,
						  chan->channel, &data);

			break;
		default:
			return -EINVAL;
		}

		if (ret)
			return ret;

		*val = data;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (chan->output) {
				ret = ad7293_get_offset(st,
							chan->channel + AD7293_VOUT_MIN_OFFSET_CH,
							&data);

				data = FIELD_GET(AD7293_REG_VOUT_OFFSET_MSK, data);
			} else {
				ret = ad7293_get_offset(st, chan->channel, &data);
			}

			break;
		case IIO_CURRENT:
			ret = ad7293_get_offset(st,
						chan->channel + AD7293_ISENSE_MIN_OFFSET_CH,
						&data);

			break;
		case IIO_TEMP:
			ret = ad7293_get_offset(st,
						chan->channel + AD7293_TSENSE_MIN_OFFSET_CH,
						&data);

			break;
		default:
			return -EINVAL;
		}
		if (ret)
			return ret;

		*val = data;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			ret = ad7293_adc_get_scale(st, chan->channel, &data);
			if (ret)
				return ret;

			*val = data;

			return IIO_VAL_INT;
		case IIO_CURRENT:
			ret = ad7293_isense_get_scale(st, chan->channel, &data);
			if (ret)
				return ret;

			*val = data;

			return IIO_VAL_INT;
		case IIO_TEMP:
			*val = 1;
			*val2 = 8;

			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ad7293_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad7293_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (!chan->output)
				return -EINVAL;

			return ad7293_dac_write_raw(st, chan->channel, val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (chan->output)
				return ad7293_set_offset(st,
							 chan->channel +
							 AD7293_VOUT_MIN_OFFSET_CH,
							 val);
			else
				return ad7293_set_offset(st, chan->channel, val);
		case IIO_CURRENT:
			return ad7293_set_offset(st,
						 chan->channel +
						 AD7293_ISENSE_MIN_OFFSET_CH,
						 val);
		case IIO_TEMP:
			return ad7293_set_offset(st,
						 chan->channel +
						 AD7293_TSENSE_MIN_OFFSET_CH,
						 val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			return ad7293_adc_set_scale(st, chan->channel, val);
		case IIO_CURRENT:
			return ad7293_isense_set_scale(st, chan->channel, val);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ad7293_reg_access(struct iio_dev *indio_dev,
			     unsigned int reg,
			     unsigned int write_val,
			     unsigned int *read_val)
{
	struct ad7293_state *st = iio_priv(indio_dev);
	int ret;

	if (read_val) {
		u16 temp;
		ret = ad7293_spi_read(st, reg, &temp);
		*read_val = temp;
	} else {
		ret = ad7293_spi_write(st, reg, (u16)write_val);
	}

	return ret;
}

static int ad7293_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long info)
{
	switch (info) {
	case IIO_CHAN_INFO_OFFSET:
		*vals = dac_offset_table;
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(dac_offset_table);

		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_INT;

		switch (chan->type) {
		case IIO_VOLTAGE:
			*vals = adc_range_table;
			*length = ARRAY_SIZE(adc_range_table);
			return IIO_AVAIL_LIST;
		case IIO_CURRENT:
			*vals = isense_gain_table;
			*length = ARRAY_SIZE(isense_gain_table);
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

#define AD7293_CHAN_ADC(_channel) {					\
	.type = IIO_VOLTAGE,						\
	.output = 0,							\
	.indexed = 1,							\
	.channel = _channel,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_SCALE) |		\
			      BIT(IIO_CHAN_INFO_OFFSET),		\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE)	\
}

#define AD7293_CHAN_DAC(_channel) {					\
	.type = IIO_VOLTAGE,						\
	.output = 1,							\
	.indexed = 1,							\
	.channel = _channel,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_OFFSET),		\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_OFFSET)	\
}

#define AD7293_CHAN_ISENSE(_channel) {					\
	.type = IIO_CURRENT,						\
	.output = 0,							\
	.indexed = 1,							\
	.channel = _channel,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_OFFSET) |		\
			      BIT(IIO_CHAN_INFO_SCALE),			\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE)	\
}

#define AD7293_CHAN_TEMP(_channel) {					\
	.type = IIO_TEMP,						\
	.output = 0,							\
	.indexed = 1,							\
	.channel = _channel,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_OFFSET),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE)		\
}

static const struct iio_chan_spec ad7293_channels[] = {
	AD7293_CHAN_ADC(0),
	AD7293_CHAN_ADC(1),
	AD7293_CHAN_ADC(2),
	AD7293_CHAN_ADC(3),
	AD7293_CHAN_ISENSE(0),
	AD7293_CHAN_ISENSE(1),
	AD7293_CHAN_ISENSE(2),
	AD7293_CHAN_ISENSE(3),
	AD7293_CHAN_TEMP(0),
	AD7293_CHAN_TEMP(1),
	AD7293_CHAN_TEMP(2),
	AD7293_CHAN_DAC(0),
	AD7293_CHAN_DAC(1),
	AD7293_CHAN_DAC(2),
	AD7293_CHAN_DAC(3),
	AD7293_CHAN_DAC(4),
	AD7293_CHAN_DAC(5),
	AD7293_CHAN_DAC(6),
	AD7293_CHAN_DAC(7)
};

static int ad7293_soft_reset(struct ad7293_state *st)
{
	int ret;

	ret = __ad7293_spi_write(st, AD7293_REG_SOFT_RESET, 0x7293);
	if (ret)
		return ret;

	return __ad7293_spi_write(st, AD7293_REG_SOFT_RESET, 0x0000);
}

static int ad7293_reset(struct ad7293_state *st)
{
	if (st->gpio_reset) {
		gpiod_set_value(st->gpio_reset, 0);
		usleep_range(100, 1000);
		gpiod_set_value(st->gpio_reset, 1);
		usleep_range(100, 1000);

		return 0;
	}

	/* Perform a software reset */
	return ad7293_soft_reset(st);
}

static int ad7293_properties_parse(struct ad7293_state *st)
{
	struct spi_device *spi = st->spi;

	st->gpio_reset = devm_gpiod_get_optional(&st->spi->dev, "reset",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(st->gpio_reset))
		return dev_err_probe(&spi->dev, PTR_ERR(st->gpio_reset),
				     "failed to get the reset GPIO\n");

	st->reg_avdd = devm_regulator_get(&spi->dev, "avdd");
	if (IS_ERR(st->reg_avdd))
		return dev_err_probe(&spi->dev, PTR_ERR(st->reg_avdd),
				     "failed to get the AVDD voltage\n");

	st->reg_vdrive = devm_regulator_get(&spi->dev, "vdrive");
	if (IS_ERR(st->reg_vdrive))
		return dev_err_probe(&spi->dev, PTR_ERR(st->reg_vdrive),
				     "failed to get the VDRIVE voltage\n");

	return 0;
}

static void ad7293_reg_disable(void *data)
{
	regulator_disable(data);
}

static int ad7293_init(struct ad7293_state *st)
{
	int ret;
	u16 chip_id;
	struct spi_device *spi = st->spi;

	ret = ad7293_properties_parse(st);
	if (ret)
		return ret;

	ret = ad7293_reset(st);
	if (ret)
		return ret;

	ret = regulator_enable(st->reg_avdd);
	if (ret) {
		dev_err(&spi->dev,
			"Failed to enable specified AVDD Voltage!\n");
		return ret;
	}

	ret = devm_add_action_or_reset(&spi->dev, ad7293_reg_disable,
				       st->reg_avdd);
	if (ret)
		return ret;

	ret = regulator_enable(st->reg_vdrive);
	if (ret) {
		dev_err(&spi->dev,
			"Failed to enable specified VDRIVE Voltage!\n");
		return ret;
	}

	ret = devm_add_action_or_reset(&spi->dev, ad7293_reg_disable,
				       st->reg_vdrive);
	if (ret)
		return ret;

	ret = regulator_get_voltage(st->reg_avdd);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to read avdd regulator: %d\n", ret);
		return ret;
	}

	if (ret > 5500000 || ret < 4500000)
		return -EINVAL;

	ret = regulator_get_voltage(st->reg_vdrive);
	if (ret < 0) {
		dev_err(&spi->dev,
			"Failed to read vdrive regulator: %d\n", ret);
		return ret;
	}
	if (ret > 5500000 || ret < 1700000)
		return -EINVAL;

	/* Check Chip ID */
	ret = __ad7293_spi_read(st, AD7293_REG_DEVICE_ID, &chip_id);
	if (ret)
		return ret;

	if (chip_id != AD7293_CHIP_ID) {
		dev_err(&spi->dev, "Invalid Chip ID.\n");
		return -EINVAL;
	}

	return 0;
}

static const struct iio_info ad7293_info = {
	.read_raw = ad7293_read_raw,
	.write_raw = ad7293_write_raw,
	.read_avail = &ad7293_read_avail,
	.debugfs_reg_access = &ad7293_reg_access,
};

static int ad7293_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ad7293_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	indio_dev->info = &ad7293_info;
	indio_dev->name = "ad7293";
	indio_dev->channels = ad7293_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad7293_channels);

	st->spi = spi;
	st->page_select = 0;

	mutex_init(&st->lock);

	ret = ad7293_init(st);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ad7293_id[] = {
	{ "ad7293", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, ad7293_id);

static const struct of_device_id ad7293_of_match[] = {
	{ .compatible = "adi,ad7293" },
	{}
};
MODULE_DEVICE_TABLE(of, ad7293_of_match);

static struct spi_driver ad7293_driver = {
	.driver = {
		.name = "ad7293",
		.of_match_table = ad7293_of_match,
	},
	.probe = ad7293_probe,
	.id_table = ad7293_id,
};
module_spi_driver(ad7293_driver);

MODULE_AUTHOR("Antoniu Miclaus <antoniu.miclaus@analog.com");
MODULE_DESCRIPTION("Analog Devices AD7293");
MODULE_LICENSE("GPL v2");
