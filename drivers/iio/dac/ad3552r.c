// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices AD3552R
 * Digital to Analog converter driver
 *
 * Copyright 2021 Analog Devices Inc.
 */
#include <asm/unaligned.h>
#include <linux/device.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

/* Register addresses */
/* Primary address space */
#define AD3552R_REG_ADDR_INTERFACE_CONFIG_A		0x00
#define   AD3552R_MASK_SOFTWARE_RESET			(BIT(7) | BIT(0))
#define   AD3552R_MASK_ADDR_ASCENSION			BIT(5)
#define   AD3552R_MASK_SDO_ACTIVE			BIT(4)
#define AD3552R_REG_ADDR_INTERFACE_CONFIG_B		0x01
#define   AD3552R_MASK_SINGLE_INST			BIT(7)
#define   AD3552R_MASK_SHORT_INSTRUCTION		BIT(3)
#define AD3552R_REG_ADDR_DEVICE_CONFIG			0x02
#define   AD3552R_MASK_DEVICE_STATUS(n)			BIT(4 + (n))
#define   AD3552R_MASK_CUSTOM_MODES			GENMASK(3, 2)
#define   AD3552R_MASK_OPERATING_MODES			GENMASK(1, 0)
#define AD3552R_REG_ADDR_CHIP_TYPE			0x03
#define   AD3552R_MASK_CLASS				GENMASK(7, 0)
#define AD3552R_REG_ADDR_PRODUCT_ID_L			0x04
#define AD3552R_REG_ADDR_PRODUCT_ID_H			0x05
#define AD3552R_REG_ADDR_CHIP_GRADE			0x06
#define   AD3552R_MASK_GRADE				GENMASK(7, 4)
#define   AD3552R_MASK_DEVICE_REVISION			GENMASK(3, 0)
#define AD3552R_REG_ADDR_SCRATCH_PAD			0x0A
#define AD3552R_REG_ADDR_SPI_REVISION			0x0B
#define AD3552R_REG_ADDR_VENDOR_L			0x0C
#define AD3552R_REG_ADDR_VENDOR_H			0x0D
#define AD3552R_REG_ADDR_STREAM_MODE			0x0E
#define   AD3552R_MASK_LENGTH				GENMASK(7, 0)
#define AD3552R_REG_ADDR_TRANSFER_REGISTER		0x0F
#define   AD3552R_MASK_MULTI_IO_MODE			GENMASK(7, 6)
#define   AD3552R_MASK_STREAM_LENGTH_KEEP_VALUE		BIT(2)
#define AD3552R_REG_ADDR_INTERFACE_CONFIG_C		0x10
#define   AD3552R_MASK_CRC_ENABLE			(GENMASK(7, 6) |\
							 GENMASK(1, 0))
#define   AD3552R_MASK_STRICT_REGISTER_ACCESS		BIT(5)
#define AD3552R_REG_ADDR_INTERFACE_STATUS_A		0x11
#define   AD3552R_MASK_INTERFACE_NOT_READY		BIT(7)
#define   AD3552R_MASK_CLOCK_COUNTING_ERROR		BIT(5)
#define   AD3552R_MASK_INVALID_OR_NO_CRC		BIT(3)
#define   AD3552R_MASK_WRITE_TO_READ_ONLY_REGISTER	BIT(2)
#define   AD3552R_MASK_PARTIAL_REGISTER_ACCESS		BIT(1)
#define   AD3552R_MASK_REGISTER_ADDRESS_INVALID		BIT(0)
#define AD3552R_REG_ADDR_INTERFACE_CONFIG_D		0x14
#define   AD3552R_MASK_ALERT_ENABLE_PULLUP		BIT(6)
#define   AD3552R_MASK_MEM_CRC_EN			BIT(4)
#define   AD3552R_MASK_SDO_DRIVE_STRENGTH		GENMASK(3, 2)
#define   AD3552R_MASK_DUAL_SPI_SYNCHROUNOUS_EN		BIT(1)
#define   AD3552R_MASK_SPI_CONFIG_DDR			BIT(0)
#define AD3552R_REG_ADDR_SH_REFERENCE_CONFIG		0x15
#define   AD3552R_MASK_IDUMP_FAST_MODE			BIT(6)
#define   AD3552R_MASK_SAMPLE_HOLD_DIFFERENTIAL_USER_EN	BIT(5)
#define   AD3552R_MASK_SAMPLE_HOLD_USER_TRIM		GENMASK(4, 3)
#define   AD3552R_MASK_SAMPLE_HOLD_USER_ENABLE		BIT(2)
#define   AD3552R_MASK_REFERENCE_VOLTAGE_SEL		GENMASK(1, 0)
#define AD3552R_REG_ADDR_ERR_ALARM_MASK			0x16
#define   AD3552R_MASK_REF_RANGE_ALARM			BIT(6)
#define   AD3552R_MASK_CLOCK_COUNT_ERR_ALARM		BIT(5)
#define   AD3552R_MASK_MEM_CRC_ERR_ALARM		BIT(4)
#define   AD3552R_MASK_SPI_CRC_ERR_ALARM		BIT(3)
#define   AD3552R_MASK_WRITE_TO_READ_ONLY_ALARM		BIT(2)
#define   AD3552R_MASK_PARTIAL_REGISTER_ACCESS_ALARM	BIT(1)
#define   AD3552R_MASK_REGISTER_ADDRESS_INVALID_ALARM	BIT(0)
#define AD3552R_REG_ADDR_ERR_STATUS			0x17
#define   AD3552R_MASK_REF_RANGE_ERR_STATUS			BIT(6)
#define   AD3552R_MASK_DUAL_SPI_STREAM_EXCEEDS_DAC_ERR_STATUS	BIT(5)
#define   AD3552R_MASK_MEM_CRC_ERR_STATUS			BIT(4)
#define   AD3552R_MASK_RESET_STATUS				BIT(0)
#define AD3552R_REG_ADDR_POWERDOWN_CONFIG		0x18
#define   AD3552R_MASK_CH_DAC_POWERDOWN(ch)		BIT(4 + (ch))
#define   AD3552R_MASK_CH_AMPLIFIER_POWERDOWN(ch)	BIT(ch)
#define AD3552R_REG_ADDR_CH0_CH1_OUTPUT_RANGE		0x19
#define   AD3552R_MASK_CH_OUTPUT_RANGE_SEL(ch)		((ch) ? GENMASK(7, 4) :\
							 GENMASK(3, 0))
#define AD3552R_REG_ADDR_CH_OFFSET(ch)			(0x1B + (ch) * 2)
#define   AD3552R_MASK_CH_OFFSET_BITS_0_7		GENMASK(7, 0)
#define AD3552R_REG_ADDR_CH_GAIN(ch)			(0x1C + (ch) * 2)
#define   AD3552R_MASK_CH_RANGE_OVERRIDE		BIT(7)
#define   AD3552R_MASK_CH_GAIN_SCALING_N		GENMASK(6, 5)
#define   AD3552R_MASK_CH_GAIN_SCALING_P		GENMASK(4, 3)
#define   AD3552R_MASK_CH_OFFSET_POLARITY		BIT(2)
#define   AD3552R_MASK_CH_OFFSET_BIT_8			BIT(0)
/*
 * Secondary region
 * For multibyte registers specify the highest address because the access is
 * done in descending order
 */
#define AD3552R_SECONDARY_REGION_START			0x28
#define AD3552R_REG_ADDR_HW_LDAC_16B			0x28
#define AD3552R_REG_ADDR_CH_DAC_16B(ch)			(0x2C - (1 - ch) * 2)
#define AD3552R_REG_ADDR_DAC_PAGE_MASK_16B		0x2E
#define AD3552R_REG_ADDR_CH_SELECT_16B			0x2F
#define AD3552R_REG_ADDR_INPUT_PAGE_MASK_16B		0x31
#define AD3552R_REG_ADDR_SW_LDAC_16B			0x32
#define AD3552R_REG_ADDR_CH_INPUT_16B(ch)		(0x36 - (1 - ch) * 2)
/* 3 bytes registers */
#define AD3552R_REG_START_24B				0x37
#define AD3552R_REG_ADDR_HW_LDAC_24B			0x37
#define AD3552R_REG_ADDR_CH_DAC_24B(ch)			(0x3D - (1 - ch) * 3)
#define AD3552R_REG_ADDR_DAC_PAGE_MASK_24B		0x40
#define AD3552R_REG_ADDR_CH_SELECT_24B			0x41
#define AD3552R_REG_ADDR_INPUT_PAGE_MASK_24B		0x44
#define AD3552R_REG_ADDR_SW_LDAC_24B			0x45
#define AD3552R_REG_ADDR_CH_INPUT_24B(ch)		(0x4B - (1 - ch) * 3)

/* Useful defines */
#define AD3552R_NUM_CH					2
#define AD3552R_MASK_CH(ch)				BIT(ch)
#define AD3552R_MASK_ALL_CH				GENMASK(1, 0)
#define AD3552R_MAX_REG_SIZE				3
#define AD3552R_READ_BIT				BIT(7)
#define AD3552R_ADDR_MASK				GENMASK(6, 0)
#define AD3552R_MASK_DAC_12B				0xFFF0
#define AD3552R_DEFAULT_CONFIG_B_VALUE			0x8
#define AD3552R_SCRATCH_PAD_TEST_VAL1			0x34
#define AD3552R_SCRATCH_PAD_TEST_VAL2			0xB2
#define AD3552R_GAIN_SCALE				1000
#define AD3552R_LDAC_PULSE_US				100

enum ad3552r_ch_vref_select {
	/* Internal source with Vref I/O floating */
	AD3552R_INTERNAL_VREF_PIN_FLOATING,
	/* Internal source with Vref I/O at 2.5V */
	AD3552R_INTERNAL_VREF_PIN_2P5V,
	/* External source with Vref I/O as input */
	AD3552R_EXTERNAL_VREF_PIN_INPUT
};

enum ad3542r_id {
	AD3542R_ID = 0x4008,
	AD3552R_ID = 0x4009,
};

enum ad3552r_ch_output_range {
	/* Range from 0 V to 2.5 V. Requires Rfb1x connection */
	AD3552R_CH_OUTPUT_RANGE_0__2P5V,
	/* Range from 0 V to 5 V. Requires Rfb1x connection  */
	AD3552R_CH_OUTPUT_RANGE_0__5V,
	/* Range from 0 V to 10 V. Requires Rfb2x connection  */
	AD3552R_CH_OUTPUT_RANGE_0__10V,
	/* Range from -5 V to 5 V. Requires Rfb2x connection  */
	AD3552R_CH_OUTPUT_RANGE_NEG_5__5V,
	/* Range from -10 V to 10 V. Requires Rfb4x connection  */
	AD3552R_CH_OUTPUT_RANGE_NEG_10__10V,
};

static const s32 ad3552r_ch_ranges[][2] = {
	[AD3552R_CH_OUTPUT_RANGE_0__2P5V]	= {0, 2500},
	[AD3552R_CH_OUTPUT_RANGE_0__5V]		= {0, 5000},
	[AD3552R_CH_OUTPUT_RANGE_0__10V]	= {0, 10000},
	[AD3552R_CH_OUTPUT_RANGE_NEG_5__5V]	= {-5000, 5000},
	[AD3552R_CH_OUTPUT_RANGE_NEG_10__10V]	= {-10000, 10000}
};

enum ad3542r_ch_output_range {
	/* Range from 0 V to 2.5 V. Requires Rfb1x connection */
	AD3542R_CH_OUTPUT_RANGE_0__2P5V,
	/* Range from 0 V to 3 V. Requires Rfb1x connection  */
	AD3542R_CH_OUTPUT_RANGE_0__3V,
	/* Range from 0 V to 5 V. Requires Rfb1x connection  */
	AD3542R_CH_OUTPUT_RANGE_0__5V,
	/* Range from 0 V to 10 V. Requires Rfb2x connection  */
	AD3542R_CH_OUTPUT_RANGE_0__10V,
	/* Range from -2.5 V to 7.5 V. Requires Rfb2x connection  */
	AD3542R_CH_OUTPUT_RANGE_NEG_2P5__7P5V,
	/* Range from -5 V to 5 V. Requires Rfb2x connection  */
	AD3542R_CH_OUTPUT_RANGE_NEG_5__5V,
};

static const s32 ad3542r_ch_ranges[][2] = {
	[AD3542R_CH_OUTPUT_RANGE_0__2P5V]	= {0, 2500},
	[AD3542R_CH_OUTPUT_RANGE_0__3V]		= {0, 3000},
	[AD3542R_CH_OUTPUT_RANGE_0__5V]		= {0, 5000},
	[AD3542R_CH_OUTPUT_RANGE_0__10V]	= {0, 10000},
	[AD3542R_CH_OUTPUT_RANGE_NEG_2P5__7P5V]	= {-2500, 7500},
	[AD3542R_CH_OUTPUT_RANGE_NEG_5__5V]	= {-5000, 5000}
};

enum ad3552r_ch_gain_scaling {
	/* Gain scaling of 1 */
	AD3552R_CH_GAIN_SCALING_1,
	/* Gain scaling of 0.5 */
	AD3552R_CH_GAIN_SCALING_0_5,
	/* Gain scaling of 0.25 */
	AD3552R_CH_GAIN_SCALING_0_25,
	/* Gain scaling of 0.125 */
	AD3552R_CH_GAIN_SCALING_0_125,
};

/* Gain * AD3552R_GAIN_SCALE */
static const s32 gains_scaling_table[] = {
	[AD3552R_CH_GAIN_SCALING_1]		= 1000,
	[AD3552R_CH_GAIN_SCALING_0_5]		= 500,
	[AD3552R_CH_GAIN_SCALING_0_25]		= 250,
	[AD3552R_CH_GAIN_SCALING_0_125]		= 125
};

enum ad3552r_dev_attributes {
	/* - Direct register values */
	/* From 0-3 */
	AD3552R_SDO_DRIVE_STRENGTH,
	/*
	 * 0 -> Internal Vref, vref_io pin floating (default)
	 * 1 -> Internal Vref, vref_io driven by internal vref
	 * 2 or 3 -> External Vref
	 */
	AD3552R_VREF_SELECT,
	/* Read registers in ascending order if set. Else descending */
	AD3552R_ADDR_ASCENSION,
};

enum ad3552r_ch_attributes {
	/* DAC powerdown */
	AD3552R_CH_DAC_POWERDOWN,
	/* DAC amplifier powerdown */
	AD3552R_CH_AMPLIFIER_POWERDOWN,
	/* Select the output range. Select from enum ad3552r_ch_output_range */
	AD3552R_CH_OUTPUT_RANGE_SEL,
	/*
	 * Over-rider the range selector in order to manually set the output
	 * voltage range
	 */
	AD3552R_CH_RANGE_OVERRIDE,
	/* Manually set the offset voltage */
	AD3552R_CH_GAIN_OFFSET,
	/* Sets the polarity of the offset. */
	AD3552R_CH_GAIN_OFFSET_POLARITY,
	/* PDAC gain scaling */
	AD3552R_CH_GAIN_SCALING_P,
	/* NDAC gain scaling */
	AD3552R_CH_GAIN_SCALING_N,
	/* Rfb value */
	AD3552R_CH_RFB,
	/* Channel select. When set allow Input -> DAC and Mask -> DAC */
	AD3552R_CH_SELECT,
};

struct ad3552r_ch_data {
	s32	scale_int;
	s32	scale_dec;
	s32	offset_int;
	s32	offset_dec;
	s16	gain_offset;
	u16	rfb;
	u8	n;
	u8	p;
	u8	range;
	bool	range_override;
};

struct ad3552r_desc {
	/* Used to look the spi bus for atomic operations where needed */
	struct mutex		lock;
	struct gpio_desc	*gpio_reset;
	struct gpio_desc	*gpio_ldac;
	struct spi_device	*spi;
	struct ad3552r_ch_data	ch_data[AD3552R_NUM_CH];
	struct iio_chan_spec	channels[AD3552R_NUM_CH + 1];
	unsigned long		enabled_ch;
	unsigned int		num_ch;
	enum ad3542r_id		chip_id;
};

static const u16 addr_mask_map[][2] = {
	[AD3552R_ADDR_ASCENSION] = {
			AD3552R_REG_ADDR_INTERFACE_CONFIG_A,
			AD3552R_MASK_ADDR_ASCENSION
	},
	[AD3552R_SDO_DRIVE_STRENGTH] = {
			AD3552R_REG_ADDR_INTERFACE_CONFIG_D,
			AD3552R_MASK_SDO_DRIVE_STRENGTH
	},
	[AD3552R_VREF_SELECT] = {
			AD3552R_REG_ADDR_SH_REFERENCE_CONFIG,
			AD3552R_MASK_REFERENCE_VOLTAGE_SEL
	},
};

/* 0 -> reg addr, 1->ch0 mask, 2->ch1 mask */
static const u16 addr_mask_map_ch[][3] = {
	[AD3552R_CH_DAC_POWERDOWN] = {
			AD3552R_REG_ADDR_POWERDOWN_CONFIG,
			AD3552R_MASK_CH_DAC_POWERDOWN(0),
			AD3552R_MASK_CH_DAC_POWERDOWN(1)
	},
	[AD3552R_CH_AMPLIFIER_POWERDOWN] = {
			AD3552R_REG_ADDR_POWERDOWN_CONFIG,
			AD3552R_MASK_CH_AMPLIFIER_POWERDOWN(0),
			AD3552R_MASK_CH_AMPLIFIER_POWERDOWN(1)
	},
	[AD3552R_CH_OUTPUT_RANGE_SEL] = {
			AD3552R_REG_ADDR_CH0_CH1_OUTPUT_RANGE,
			AD3552R_MASK_CH_OUTPUT_RANGE_SEL(0),
			AD3552R_MASK_CH_OUTPUT_RANGE_SEL(1)
	},
	[AD3552R_CH_SELECT] = {
			AD3552R_REG_ADDR_CH_SELECT_16B,
			AD3552R_MASK_CH(0),
			AD3552R_MASK_CH(1)
	}
};

static u8 _ad3552r_reg_len(u8 addr)
{
	switch (addr) {
	case AD3552R_REG_ADDR_HW_LDAC_16B:
	case AD3552R_REG_ADDR_CH_SELECT_16B:
	case AD3552R_REG_ADDR_SW_LDAC_16B:
	case AD3552R_REG_ADDR_HW_LDAC_24B:
	case AD3552R_REG_ADDR_CH_SELECT_24B:
	case AD3552R_REG_ADDR_SW_LDAC_24B:
		return 1;
	default:
		break;
	}

	if (addr > AD3552R_REG_ADDR_HW_LDAC_24B)
		return 3;
	if (addr > AD3552R_REG_ADDR_HW_LDAC_16B)
		return 2;

	return 1;
}

/* SPI transfer to device */
static int ad3552r_transfer(struct ad3552r_desc *dac, u8 addr, u32 len,
			    u8 *data, bool is_read)
{
	/* Maximum transfer: Addr (1B) + 2 * (Data Reg (3B)) + SW LDAC(1B) */
	u8 buf[8];

	buf[0] = addr & AD3552R_ADDR_MASK;
	buf[0] |= is_read ? AD3552R_READ_BIT : 0;
	if (is_read)
		return spi_write_then_read(dac->spi, buf, 1, data, len);

	memcpy(buf + 1, data, len);
	return spi_write_then_read(dac->spi, buf, len + 1, NULL, 0);
}

static int ad3552r_write_reg(struct ad3552r_desc *dac, u8 addr, u16 val)
{
	u8 reg_len;
	u8 buf[AD3552R_MAX_REG_SIZE] = { 0 };

	reg_len = _ad3552r_reg_len(addr);
	if (reg_len == 2)
		/* Only DAC register are 2 bytes wide */
		val &= AD3552R_MASK_DAC_12B;
	if (reg_len == 1)
		buf[0] = val & 0xFF;
	else
		/* reg_len can be 2 or 3, but 3rd bytes needs to be set to 0 */
		put_unaligned_be16(val, buf);

	return ad3552r_transfer(dac, addr, reg_len, buf, false);
}

static int ad3552r_read_reg(struct ad3552r_desc *dac, u8 addr, u16 *val)
{
	int err;
	u8  reg_len, buf[AD3552R_MAX_REG_SIZE] = { 0 };

	reg_len = _ad3552r_reg_len(addr);
	err = ad3552r_transfer(dac, addr, reg_len, buf, true);
	if (err)
		return err;

	if (reg_len == 1)
		*val = buf[0];
	else
		/* reg_len can be 2 or 3, but only first 2 bytes are relevant */
		*val = get_unaligned_be16(buf);

	return 0;
}

static u16 ad3552r_field_prep(u16 val, u16 mask)
{
	return (val << __ffs(mask)) & mask;
}

/* Update field of a register, shift val if needed */
static int ad3552r_update_reg_field(struct ad3552r_desc *dac, u8 addr, u16 mask,
				    u16 val)
{
	int ret;
	u16 reg;

	ret = ad3552r_read_reg(dac, addr, &reg);
	if (ret < 0)
		return ret;

	reg &= ~mask;
	reg |= ad3552r_field_prep(val, mask);

	return ad3552r_write_reg(dac, addr, reg);
}

static int ad3552r_set_ch_value(struct ad3552r_desc *dac,
				enum ad3552r_ch_attributes attr,
				u8 ch,
				u16 val)
{
	/* Update register related to attributes in chip */
	return ad3552r_update_reg_field(dac, addr_mask_map_ch[attr][0],
				       addr_mask_map_ch[attr][ch + 1], val);
}

#define AD3552R_CH_DAC(_idx) ((struct iio_chan_spec) {		\
	.type = IIO_VOLTAGE,					\
	.output = true,						\
	.indexed = true,					\
	.channel = _idx,					\
	.scan_index = _idx,					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_BE,				\
	},							\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_ENABLE) |	\
				BIT(IIO_CHAN_INFO_OFFSET),	\
})

static int ad3552r_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val,
			    int *val2,
			    long mask)
{
	struct ad3552r_desc *dac = iio_priv(indio_dev);
	u16 tmp_val;
	int err;
	u8 ch = chan->channel;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&dac->lock);
		err = ad3552r_read_reg(dac, AD3552R_REG_ADDR_CH_DAC_24B(ch),
				       &tmp_val);
		mutex_unlock(&dac->lock);
		if (err < 0)
			return err;
		*val = tmp_val;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_ENABLE:
		mutex_lock(&dac->lock);
		err = ad3552r_read_reg(dac, AD3552R_REG_ADDR_POWERDOWN_CONFIG,
				       &tmp_val);
		mutex_unlock(&dac->lock);
		if (err < 0)
			return err;
		*val = !((tmp_val & AD3552R_MASK_CH_DAC_POWERDOWN(ch)) >>
			  __ffs(AD3552R_MASK_CH_DAC_POWERDOWN(ch)));
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = dac->ch_data[ch].scale_int;
		*val2 = dac->ch_data[ch].scale_dec;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		*val = dac->ch_data[ch].offset_int;
		*val2 = dac->ch_data[ch].offset_dec;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int ad3552r_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val,
			     int val2,
			     long mask)
{
	struct ad3552r_desc *dac = iio_priv(indio_dev);
	int err;

	mutex_lock(&dac->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = ad3552r_write_reg(dac,
					AD3552R_REG_ADDR_CH_DAC_24B(chan->channel),
					val);
		break;
	case IIO_CHAN_INFO_ENABLE:
		err = ad3552r_set_ch_value(dac, AD3552R_CH_DAC_POWERDOWN,
					   chan->channel, !val);
		break;
	default:
		err = -EINVAL;
		break;
	}
	mutex_unlock(&dac->lock);

	return err;
}

static const struct iio_info ad3552r_iio_info = {
	.read_raw = ad3552r_read_raw,
	.write_raw = ad3552r_write_raw
};

static int32_t ad3552r_trigger_hw_ldac(struct gpio_desc *ldac)
{
	gpiod_set_value_cansleep(ldac, 0);
	usleep_range(AD3552R_LDAC_PULSE_US, AD3552R_LDAC_PULSE_US + 10);
	gpiod_set_value_cansleep(ldac, 1);

	return 0;
}

static int ad3552r_write_all_channels(struct ad3552r_desc *dac, u8 *data)
{
	int err, len;
	u8 addr, buff[AD3552R_NUM_CH * AD3552R_MAX_REG_SIZE + 1];

	addr = AD3552R_REG_ADDR_CH_INPUT_24B(1);
	/* CH1 */
	memcpy(buff, data + 2, 2);
	buff[2] = 0;
	/* CH0 */
	memcpy(buff + 3, data, 2);
	buff[5] = 0;
	len = 6;
	if (!dac->gpio_ldac) {
		/* Software LDAC */
		buff[6] = AD3552R_MASK_ALL_CH;
		++len;
	}
	err = ad3552r_transfer(dac, addr, len, buff, false);
	if (err)
		return err;

	if (dac->gpio_ldac)
		return ad3552r_trigger_hw_ldac(dac->gpio_ldac);

	return 0;
}

static int ad3552r_write_codes(struct ad3552r_desc *dac, u32 mask, u8 *data)
{
	int err;
	u8 addr, buff[AD3552R_MAX_REG_SIZE];

	if (mask == AD3552R_MASK_ALL_CH) {
		if (memcmp(data, data + 2, 2) != 0)
			return ad3552r_write_all_channels(dac, data);

		addr = AD3552R_REG_ADDR_INPUT_PAGE_MASK_24B;
	} else {
		addr = AD3552R_REG_ADDR_CH_INPUT_24B(__ffs(mask));
	}

	memcpy(buff, data, 2);
	buff[2] = 0;
	err = ad3552r_transfer(dac, addr, 3, data, false);
	if (err)
		return err;

	if (dac->gpio_ldac)
		return ad3552r_trigger_hw_ldac(dac->gpio_ldac);

	return ad3552r_write_reg(dac, AD3552R_REG_ADDR_SW_LDAC_24B, mask);
}

static irqreturn_t ad3552r_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct iio_buffer *buf = indio_dev->buffer;
	struct ad3552r_desc *dac = iio_priv(indio_dev);
	/* Maximum size of a scan */
	u8 buff[AD3552R_NUM_CH * AD3552R_MAX_REG_SIZE];
	int err;

	memset(buff, 0, sizeof(buff));
	err = iio_pop_from_buffer(buf, buff);
	if (err)
		goto end;

	mutex_lock(&dac->lock);
	ad3552r_write_codes(dac, *indio_dev->active_scan_mask, buff);
	mutex_unlock(&dac->lock);
end:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ad3552r_check_scratch_pad(struct ad3552r_desc *dac)
{
	const u16 val1 = AD3552R_SCRATCH_PAD_TEST_VAL1;
	const u16 val2 = AD3552R_SCRATCH_PAD_TEST_VAL2;
	u16 val;
	int err;

	err = ad3552r_write_reg(dac, AD3552R_REG_ADDR_SCRATCH_PAD, val1);
	if (err < 0)
		return err;

	err = ad3552r_read_reg(dac, AD3552R_REG_ADDR_SCRATCH_PAD, &val);
	if (err < 0)
		return err;

	if (val1 != val)
		return -ENODEV;

	err = ad3552r_write_reg(dac, AD3552R_REG_ADDR_SCRATCH_PAD, val2);
	if (err < 0)
		return err;

	err = ad3552r_read_reg(dac, AD3552R_REG_ADDR_SCRATCH_PAD, &val);
	if (err < 0)
		return err;

	if (val2 != val)
		return -ENODEV;

	return 0;
}

struct reg_addr_pool {
	struct ad3552r_desc *dac;
	u8		    addr;
};

static int ad3552r_read_reg_wrapper(struct reg_addr_pool *addr)
{
	int err;
	u16 val;

	err = ad3552r_read_reg(addr->dac, addr->addr, &val);
	if (err)
		return err;

	return val;
}

static int ad3552r_reset(struct ad3552r_desc *dac)
{
	struct reg_addr_pool addr;
	int ret;
	int val;

	dac->gpio_reset = devm_gpiod_get_optional(&dac->spi->dev, "reset",
						  GPIOD_OUT_LOW);
	if (IS_ERR(dac->gpio_reset))
		return dev_err_probe(&dac->spi->dev, PTR_ERR(dac->gpio_reset),
				     "Error while getting gpio reset");

	if (dac->gpio_reset) {
		/* Perform hardware reset */
		usleep_range(10, 20);
		gpiod_set_value_cansleep(dac->gpio_reset, 1);
	} else {
		/* Perform software reset if no GPIO provided */
		ret = ad3552r_update_reg_field(dac,
					       AD3552R_REG_ADDR_INTERFACE_CONFIG_A,
					       AD3552R_MASK_SOFTWARE_RESET,
					       AD3552R_MASK_SOFTWARE_RESET);
		if (ret < 0)
			return ret;

	}

	addr.dac = dac;
	addr.addr = AD3552R_REG_ADDR_INTERFACE_CONFIG_B;
	ret = readx_poll_timeout(ad3552r_read_reg_wrapper, &addr, val,
				 val == AD3552R_DEFAULT_CONFIG_B_VALUE ||
				 val < 0,
				 5000, 50000);
	if (val < 0)
		ret = val;
	if (ret) {
		dev_err(&dac->spi->dev, "Error while resetting");
		return ret;
	}

	ret = readx_poll_timeout(ad3552r_read_reg_wrapper, &addr, val,
				 !(val & AD3552R_MASK_INTERFACE_NOT_READY) ||
				 val < 0,
				 5000, 50000);
	if (val < 0)
		ret = val;
	if (ret) {
		dev_err(&dac->spi->dev, "Error while resetting");
		return ret;
	}

	return ad3552r_update_reg_field(dac,
					addr_mask_map[AD3552R_ADDR_ASCENSION][0],
					addr_mask_map[AD3552R_ADDR_ASCENSION][1],
					val);
}

static void ad3552r_get_custom_range(struct ad3552r_desc *dac, s32 i, s32 *v_min,
				     s32 *v_max)
{
	s64 vref, tmp, common, offset, gn, gp;
	/*
	 * From datasheet formula (In Volts):
	 *	Vmin = 2.5 + [(GainN + Offset / 1024) * 2.5 * Rfb * 1.03]
	 *	Vmax = 2.5 - [(GainP + Offset / 1024) * 2.5 * Rfb * 1.03]
	 * Calculus are converted to milivolts
	 */
	vref = 2500;
	/* 2.5 * 1.03 * 1000 (To mV) */
	common = 2575 * dac->ch_data[i].rfb;
	offset = dac->ch_data[i].gain_offset;

	gn = gains_scaling_table[dac->ch_data[i].n];
	tmp = (1024 * gn + AD3552R_GAIN_SCALE * offset) * common;
	tmp = div_s64(tmp, 1024  * AD3552R_GAIN_SCALE);
	*v_max = vref + tmp;

	gp = gains_scaling_table[dac->ch_data[i].p];
	tmp = (1024 * gp - AD3552R_GAIN_SCALE * offset) * common;
	tmp = div_s64(tmp, 1024 * AD3552R_GAIN_SCALE);
	*v_min = vref - tmp;
}

static void ad3552r_calc_gain_and_offset(struct ad3552r_desc *dac, s32 ch)
{
	s32 idx, v_max, v_min, span, rem;
	s64 tmp;

	if (dac->ch_data[ch].range_override) {
		ad3552r_get_custom_range(dac, ch, &v_min, &v_max);
	} else {
		/* Normal range */
		idx = dac->ch_data[ch].range;
		if (dac->chip_id == AD3542R_ID) {
			v_min = ad3542r_ch_ranges[idx][0];
			v_max = ad3542r_ch_ranges[idx][1];
		} else {
			v_min = ad3552r_ch_ranges[idx][0];
			v_max = ad3552r_ch_ranges[idx][1];
		}
	}

	/*
	 * From datasheet formula:
	 *	Vout = Span * (D / 65536) + Vmin
	 * Converted to scale and offset:
	 *	Scale = Span / 65536
	 *	Offset = 65536 * Vmin / Span
	 *
	 * Reminders are in micros in order to be printed as
	 * IIO_VAL_INT_PLUS_MICRO
	 */
	span = v_max - v_min;
	dac->ch_data[ch].scale_int = div_s64_rem(span, 65536, &rem);
	/* Do operations in microvolts */
	dac->ch_data[ch].scale_dec = DIV_ROUND_CLOSEST((s64)rem * 1000000,
							65536);

	dac->ch_data[ch].offset_int = div_s64_rem(v_min * 65536, span, &rem);
	tmp = (s64)rem * 1000000;
	dac->ch_data[ch].offset_dec = div_s64(tmp, span);
}

static int ad3552r_find_range(u16 id, s32 *vals)
{
	int i, len;
	const s32 (*ranges)[2];

	if (id == AD3542R_ID) {
		len = ARRAY_SIZE(ad3542r_ch_ranges);
		ranges = ad3542r_ch_ranges;
	} else {
		len = ARRAY_SIZE(ad3552r_ch_ranges);
		ranges = ad3552r_ch_ranges;
	}

	for (i = 0; i < len; i++)
		if (vals[0] == ranges[i][0] * 1000 &&
		    vals[1] == ranges[i][1] * 1000)
			return i;

	return -EINVAL;
}

static int ad3552r_configure_custom_gain(struct ad3552r_desc *dac,
					 struct fwnode_handle *child,
					 u32 ch)
{
	struct device *dev = &dac->spi->dev;
	struct fwnode_handle *gain_child;
	u32 val;
	int err;
	u8 addr;
	u16 reg = 0, offset;

	gain_child = fwnode_get_named_child_node(child,
						 "custom-output-range-config");
	if (IS_ERR(gain_child)) {
		dev_err(dev,
			"mandatory custom-output-range-config property missing\n");
		return PTR_ERR(gain_child);
	}

	dac->ch_data[ch].range_override = 1;
	reg |= ad3552r_field_prep(1, AD3552R_MASK_CH_RANGE_OVERRIDE);

	err = fwnode_property_read_u32(gain_child, "adi,gain-scaling-p", &val);
	if (err) {
		dev_err(dev, "mandatory adi,gain-scaling-p property missing\n");
		goto put_child;
	}
	reg |= ad3552r_field_prep(val, AD3552R_MASK_CH_GAIN_SCALING_P);
	dac->ch_data[ch].p = val;

	err = fwnode_property_read_u32(gain_child, "adi,gain-scaling-n", &val);
	if (err) {
		dev_err(dev, "mandatory adi,gain-scaling-n property missing\n");
		goto put_child;
	}
	reg |= ad3552r_field_prep(val, AD3552R_MASK_CH_GAIN_SCALING_N);
	dac->ch_data[ch].n = val;

	err = fwnode_property_read_u32(gain_child, "adi,rfb-ohms", &val);
	if (err) {
		dev_err(dev, "mandatory adi,rfb-ohms property missing\n");
		goto put_child;
	}
	dac->ch_data[ch].rfb = val;

	err = fwnode_property_read_u32(gain_child, "adi,gain-offset", &val);
	if (err) {
		dev_err(dev, "mandatory adi,gain-offset property missing\n");
		goto put_child;
	}
	dac->ch_data[ch].gain_offset = val;

	offset = abs((s32)val);
	reg |= ad3552r_field_prep((offset >> 8), AD3552R_MASK_CH_OFFSET_BIT_8);

	reg |= ad3552r_field_prep((s32)val < 0, AD3552R_MASK_CH_OFFSET_POLARITY);
	addr = AD3552R_REG_ADDR_CH_GAIN(ch);
	err = ad3552r_write_reg(dac, addr,
				offset & AD3552R_MASK_CH_OFFSET_BITS_0_7);
	if (err) {
		dev_err(dev, "Error writing register\n");
		goto put_child;
	}

	err = ad3552r_write_reg(dac, addr, reg);
	if (err) {
		dev_err(dev, "Error writing register\n");
		goto put_child;
	}

put_child:
	fwnode_handle_put(gain_child);

	return err;
}

static void ad3552r_reg_disable(void *reg)
{
	regulator_disable(reg);
}

static int ad3552r_configure_device(struct ad3552r_desc *dac)
{
	struct device *dev = &dac->spi->dev;
	struct fwnode_handle *child;
	struct regulator *vref;
	int err, cnt = 0, voltage, delta = 100000;
	u32 vals[2], val, ch;

	dac->gpio_ldac = devm_gpiod_get_optional(dev, "ldac", GPIOD_OUT_HIGH);
	if (IS_ERR(dac->gpio_ldac))
		return dev_err_probe(dev, PTR_ERR(dac->gpio_ldac),
				     "Error getting gpio ldac");

	vref = devm_regulator_get_optional(dev, "vref");
	if (IS_ERR(vref)) {
		if (PTR_ERR(vref) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(vref),
					     "Error getting vref");

		if (device_property_read_bool(dev, "adi,vref-out-en"))
			val = AD3552R_INTERNAL_VREF_PIN_2P5V;
		else
			val = AD3552R_INTERNAL_VREF_PIN_FLOATING;
	} else {
		err = regulator_enable(vref);
		if (err) {
			dev_err(dev, "Failed to enable external vref supply\n");
			return err;
		}

		err = devm_add_action_or_reset(dev, ad3552r_reg_disable, vref);
		if (err) {
			regulator_disable(vref);
			return err;
		}

		voltage = regulator_get_voltage(vref);
		if (voltage > 2500000 + delta || voltage < 2500000 - delta) {
			dev_warn(dev, "vref-supply must be 2.5V");
			return -EINVAL;
		}
		val = AD3552R_EXTERNAL_VREF_PIN_INPUT;
	}

	err = ad3552r_update_reg_field(dac,
				       addr_mask_map[AD3552R_VREF_SELECT][0],
				       addr_mask_map[AD3552R_VREF_SELECT][1],
				       val);
	if (err)
		return err;

	err = device_property_read_u32(dev, "adi,sdo-drive-strength", &val);
	if (!err) {
		if (val > 3) {
			dev_err(dev, "adi,sdo-drive-strength must be less than 4\n");
			return -EINVAL;
		}

		err = ad3552r_update_reg_field(dac,
					       addr_mask_map[AD3552R_SDO_DRIVE_STRENGTH][0],
					       addr_mask_map[AD3552R_SDO_DRIVE_STRENGTH][1],
					       val);
		if (err)
			return err;
	}

	dac->num_ch = device_get_child_node_count(dev);
	if (!dac->num_ch) {
		dev_err(dev, "No channels defined\n");
		return -ENODEV;
	}

	device_for_each_child_node(dev, child) {
		err = fwnode_property_read_u32(child, "reg", &ch);
		if (err) {
			dev_err(dev, "mandatory reg property missing\n");
			goto put_child;
		}
		if (ch >= AD3552R_NUM_CH) {
			dev_err(dev, "reg must be less than %d\n",
				AD3552R_NUM_CH);
			err = -EINVAL;
			goto put_child;
		}

		if (fwnode_property_present(child, "adi,output-range-microvolt")) {
			err = fwnode_property_read_u32_array(child,
							     "adi,output-range-microvolt",
							     vals,
							     2);
			if (err) {
				dev_err(dev,
					"adi,output-range-microvolt property could not be parsed\n");
				goto put_child;
			}

			err = ad3552r_find_range(dac->chip_id, vals);
			if (err < 0) {
				dev_err(dev,
					"Invalid adi,output-range-microvolt value\n");
				goto put_child;
			}
			val = err;
			err = ad3552r_set_ch_value(dac,
						   AD3552R_CH_OUTPUT_RANGE_SEL,
						   ch, val);
			if (err)
				goto put_child;

			dac->ch_data[ch].range = val;
		} else if (dac->chip_id == AD3542R_ID) {
			dev_err(dev,
				"adi,output-range-microvolt is required for ad3542r\n");
			err = -EINVAL;
			goto put_child;
		} else {
			err = ad3552r_configure_custom_gain(dac, child, ch);
			if (err)
				goto put_child;
		}

		ad3552r_calc_gain_and_offset(dac, ch);
		dac->enabled_ch |= BIT(ch);

		err = ad3552r_set_ch_value(dac, AD3552R_CH_SELECT, ch, 1);
		if (err < 0)
			goto put_child;

		dac->channels[cnt] = AD3552R_CH_DAC(ch);
		++cnt;

	}

	/* Disable unused channels */
	for_each_clear_bit(ch, &dac->enabled_ch, AD3552R_NUM_CH) {
		err = ad3552r_set_ch_value(dac, AD3552R_CH_AMPLIFIER_POWERDOWN,
					   ch, 1);
		if (err)
			return err;
	}

	dac->num_ch = cnt;

	return 0;
put_child:
	fwnode_handle_put(child);

	return err;
}

static int ad3552r_init(struct ad3552r_desc *dac)
{
	int err;
	u16 val, id;

	err = ad3552r_reset(dac);
	if (err) {
		dev_err(&dac->spi->dev, "Reset failed\n");
		return err;
	}

	err = ad3552r_check_scratch_pad(dac);
	if (err) {
		dev_err(&dac->spi->dev, "Scratch pad test failed\n");
		return err;
	}

	err = ad3552r_read_reg(dac, AD3552R_REG_ADDR_PRODUCT_ID_L, &val);
	if (err) {
		dev_err(&dac->spi->dev, "Fail read PRODUCT_ID_L\n");
		return err;
	}

	id = val;
	err = ad3552r_read_reg(dac, AD3552R_REG_ADDR_PRODUCT_ID_H, &val);
	if (err) {
		dev_err(&dac->spi->dev, "Fail read PRODUCT_ID_H\n");
		return err;
	}

	id |= val << 8;
	if (id != dac->chip_id) {
		dev_err(&dac->spi->dev, "Product id not matching\n");
		return -ENODEV;
	}

	return ad3552r_configure_device(dac);
}

static int ad3552r_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct ad3552r_desc *dac;
	struct iio_dev *indio_dev;
	int err;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*dac));
	if (!indio_dev)
		return -ENOMEM;

	dac = iio_priv(indio_dev);
	dac->spi = spi;
	dac->chip_id = id->driver_data;

	mutex_init(&dac->lock);

	err = ad3552r_init(dac);
	if (err)
		return err;

	/* Config triggered buffer device */
	if (dac->chip_id == AD3552R_ID)
		indio_dev->name = "ad3552r";
	else
		indio_dev->name = "ad3542r";
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &ad3552r_iio_info;
	indio_dev->num_channels = dac->num_ch;
	indio_dev->channels = dac->channels;
	indio_dev->modes = INDIO_DIRECT_MODE;

	err = devm_iio_triggered_buffer_setup_ext(&indio_dev->dev, indio_dev, NULL,
						  &ad3552r_trigger_handler,
						  IIO_BUFFER_DIRECTION_OUT,
						  NULL,
						  NULL);
	if (err)
		return err;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ad3552r_id[] = {
	{ "ad3542r", AD3542R_ID },
	{ "ad3552r", AD3552R_ID },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad3552r_id);

static const struct of_device_id ad3552r_of_match[] = {
	{ .compatible = "adi,ad3542r"},
	{ .compatible = "adi,ad3552r"},
	{ }
};
MODULE_DEVICE_TABLE(of, ad3552r_of_match);

static struct spi_driver ad3552r_driver = {
	.driver = {
		.name = "ad3552r",
		.of_match_table = ad3552r_of_match,
	},
	.probe = ad3552r_probe,
	.id_table = ad3552r_id
};
module_spi_driver(ad3552r_driver);

MODULE_AUTHOR("Mihail Chindris <mihail.chindris@analog.com>");
MODULE_DESCRIPTION("Analog Device AD3552R DAC");
MODULE_LICENSE("GPL v2");
