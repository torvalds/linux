/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AD3552R Digital <-> Analog converters common header
 *
 * Copyright 2021-2024 Analog Devices Inc.
 * Author: Angelo Dureghello <adureghello@baylibre.com>
 */

#ifndef __DRIVERS_IIO_DAC_AD3552R_H__
#define __DRIVERS_IIO_DAC_AD3552R_H__

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
#define   AD3552R_MASK_CRC_ENABLE \
		(GENMASK(7, 6) | GENMASK(1, 0))
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
#define   AD3552R_MASK_SAMPLE_HOLD_DIFF_USER_EN		BIT(5)
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
#define   AD3552R_MASK_REF_RANGE_ERR_STATUS		BIT(6)
#define   AD3552R_MASK_STREAM_EXCEEDS_DAC_ERR_STATUS	BIT(5)
#define   AD3552R_MASK_MEM_CRC_ERR_STATUS		BIT(4)
#define   AD3552R_MASK_RESET_STATUS			BIT(0)
#define AD3552R_REG_ADDR_POWERDOWN_CONFIG		0x18
#define   AD3552R_MASK_CH_DAC_POWERDOWN(ch)		BIT(4 + (ch))
#define   AD3552R_MASK_CH_AMPLIFIER_POWERDOWN(ch)	BIT(ch)
#define AD3552R_REG_ADDR_CH0_CH1_OUTPUT_RANGE		0x19
#define   AD3552R_MASK_CH0_RANGE			GENMASK(2, 0)
#define   AD3552R_MASK_CH1_RANGE			GENMASK(6, 4)
#define   AD3552R_MASK_CH_OUTPUT_RANGE			GENMASK(7, 0)
#define   AD3552R_MASK_CH_OUTPUT_RANGE_SEL(ch) \
		((ch) ? GENMASK(7, 4) : GENMASK(3, 0))
#define AD3552R_REG_ADDR_CH_OFFSET(ch)			(0x1B + (ch) * 2)
#define   AD3552R_MASK_CH_OFFSET_BITS_0_7		GENMASK(7, 0)
#define AD3552R_REG_ADDR_CH_GAIN(ch)			(0x1C + (ch) * 2)
#define   AD3552R_MASK_CH_RANGE_OVERRIDE		BIT(7)
#define   AD3552R_MASK_CH_GAIN_SCALING_N		GENMASK(6, 5)
#define   AD3552R_MASK_CH_GAIN_SCALING_P		GENMASK(4, 3)
#define   AD3552R_MASK_CH_OFFSET_POLARITY		BIT(2)
#define   AD3552R_MASK_CH_OFFSET_BIT_8			BIT(8)
/*
 * Secondary region
 * For multibyte registers specify the highest address because the access is
 * done in descending order
 */
#define AD3552R_SECONDARY_REGION_START			0x28
#define AD3552R_REG_ADDR_HW_LDAC_16B			0x28
#define AD3552R_REG_ADDR_CH_DAC_16B(ch)			(0x2C - (1 - (ch)) * 2)
#define AD3552R_REG_ADDR_DAC_PAGE_MASK_16B		0x2E
#define AD3552R_REG_ADDR_CH_SELECT_16B			0x2F
#define AD3552R_REG_ADDR_INPUT_PAGE_MASK_16B		0x31
#define AD3552R_REG_ADDR_SW_LDAC_16B			0x32
#define AD3552R_REG_ADDR_CH_INPUT_16B(ch)		(0x36 - (1 - (ch)) * 2)
/* 3 bytes registers */
#define AD3552R_REG_START_24B				0x37
#define AD3552R_REG_ADDR_HW_LDAC_24B			0x37
#define AD3552R_REG_ADDR_CH_DAC_24B(ch)			(0x3D - (1 - (ch)) * 3)
#define AD3552R_REG_ADDR_DAC_PAGE_MASK_24B		0x40
#define AD3552R_REG_ADDR_CH_SELECT_24B			0x41
#define AD3552R_REG_ADDR_INPUT_PAGE_MASK_24B		0x44
#define AD3552R_REG_ADDR_SW_LDAC_24B			0x45
#define AD3552R_REG_ADDR_CH_INPUT_24B(ch)		(0x4B - (1 - (ch)) * 3)

#define AD3552R_MAX_CH					2
#define AD3552R_MASK_CH(ch)				BIT(ch)
#define AD3552R_MASK_ALL_CH				GENMASK(1, 0)
#define AD3552R_MAX_REG_SIZE				3
#define AD3552R_READ_BIT				BIT(7)
#define AD3552R_ADDR_MASK				GENMASK(6, 0)
#define AD3552R_MASK_DAC_12B				GENMASK(15, 4)
#define AD3552R_DEFAULT_CONFIG_B_VALUE			0x8
#define AD3552R_SCRATCH_PAD_TEST_VAL1			0x34
#define AD3552R_SCRATCH_PAD_TEST_VAL2			0xB2
#define AD3552R_GAIN_SCALE				1000
#define AD3552R_LDAC_PULSE_US				100

#define AD3552R_CH0_ACTIVE				BIT(0)
#define AD3552R_CH1_ACTIVE				BIT(1)

#define AD3552R_MAX_RANGES	5
#define AD3542R_MAX_RANGES	5
#define AD3552R_SPI		0
#define AD3552R_DUAL_SPI	1
#define AD3552R_QUAD_SPI	2

extern const struct ad3552r_model_data ad3541r_model_data;
extern const struct ad3552r_model_data ad3542r_model_data;
extern const struct ad3552r_model_data ad3551r_model_data;
extern const struct ad3552r_model_data ad3552r_model_data;

enum ad3552r_id {
	AD3541R_ID = 0x400b,
	AD3542R_ID = 0x4009,
	AD3551R_ID = 0x400a,
	AD3552R_ID = 0x4008,
};

struct ad3552r_model_data {
	const char *model_name;
	enum ad3552r_id chip_id;
	unsigned int num_hw_channels;
	const s32 (*ranges_table)[2];
	int num_ranges;
	bool requires_output_range;
	int num_spi_data_lanes;
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

enum ad3552r_ch_vref_select {
	/* Internal source with Vref I/O floating */
	AD3552R_INTERNAL_VREF_PIN_FLOATING,
	/* Internal source with Vref I/O at 2.5V */
	AD3552R_INTERNAL_VREF_PIN_2P5V,
	/* External source with Vref I/O as input */
	AD3552R_EXTERNAL_VREF_PIN_INPUT
};

enum ad3542r_ch_output_range {
	/* Range from 0 V to 2.5 V. Requires Rfb1x connection */
	AD3542R_CH_OUTPUT_RANGE_0__2P5V,
	/* Range from 0 V to 5 V. Requires Rfb1x connection  */
	AD3542R_CH_OUTPUT_RANGE_0__5V,
	/* Range from 0 V to 10 V. Requires Rfb2x connection  */
	AD3542R_CH_OUTPUT_RANGE_0__10V,
	/* Range from -5 V to 5 V. Requires Rfb2x connection  */
	AD3542R_CH_OUTPUT_RANGE_NEG_5__5V,
	/* Range from -2.5 V to 7.5 V. Requires Rfb2x connection  */
	AD3542R_CH_OUTPUT_RANGE_NEG_2P5__7P5V,
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

int ad3552r_get_output_range(struct device *dev,
			     const struct ad3552r_model_data *model_info,
			     struct fwnode_handle *child, u32 *val);
int ad3552r_get_custom_gain(struct device *dev, struct fwnode_handle *child,
			    u8 *gs_p, u8 *gs_n, u16 *rfb, s16 *goffs);
u16 ad3552r_calc_custom_gain(u8 p, u8 n, s16 goffs);
int ad3552r_get_ref_voltage(struct device *dev, u32 *val);
int ad3552r_get_drive_strength(struct device *dev, u32 *val);
void ad3552r_calc_gain_and_offset(struct ad3552r_ch_data *ch_data,
				  const struct ad3552r_model_data *model_data);

#endif /* __DRIVERS_IIO_DAC_AD3552R_H__ */
