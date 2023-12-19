// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#define __INLINE_INPUT_SYSTEM__
#include "input_system.h"
#include "assert_support.h"
#include "ia_css_isys.h"
#include "ia_css_irq.h"
#include "sh_css_internal.h"

void ia_css_isys_rx_enable_all_interrupts(enum mipi_port_id port)
{
	hrt_data bits = receiver_port_reg_load(RX0_ID,
					       port,
					       _HRT_CSS_RECEIVER_IRQ_ENABLE_REG_IDX);

	bits |= (1U << _HRT_CSS_RECEIVER_IRQ_OVERRUN_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_INIT_TIMEOUT_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_SLEEP_MODE_ENTRY_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_SLEEP_MODE_EXIT_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_ERR_SOT_HS_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_ERR_SOT_SYNC_HS_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_ERR_CONTROL_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_ERR_ECC_DOUBLE_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_ERR_ECC_CORRECTED_BIT) |
		/*(1U << _HRT_CSS_RECEIVER_IRQ_ERR_ECC_NO_CORRECTION_BIT) | */
		(1U << _HRT_CSS_RECEIVER_IRQ_ERR_CRC_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_ERR_ID_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_ERR_FRAME_SYNC_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_ERR_FRAME_DATA_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_DATA_TIMEOUT_BIT) |
		(1U << _HRT_CSS_RECEIVER_IRQ_ERR_ESCAPE_BIT);
	/*(1U << _HRT_CSS_RECEIVER_IRQ_ERR_LINE_SYNC_BIT); */

	receiver_port_reg_store(RX0_ID,
				port,
				_HRT_CSS_RECEIVER_IRQ_ENABLE_REG_IDX, bits);

	/*
	 * The CSI is nested into the Iunit IRQ's
	 */
	ia_css_irq_enable(IA_CSS_IRQ_INFO_CSS_RECEIVER_ERROR, true);

	return;
}

/* This function converts between the enum used on the CSS API and the
 * internal DLI enum type.
 * We do not use an array for this since we cannot use named array
 * initializers in Windows. Without that there is no easy way to guarantee
 * that the array values would be in the correct order.
 * */
enum mipi_port_id ia_css_isys_port_to_mipi_port(enum mipi_port_id api_port)
{
	/* In this module the validity of the inptu variable should
	 * have been checked already, so we do not check for erroneous
	 * values. */
	enum mipi_port_id port = MIPI_PORT0_ID;

	if (api_port == MIPI_PORT1_ID)
		port = MIPI_PORT1_ID;
	else if (api_port == MIPI_PORT2_ID)
		port = MIPI_PORT2_ID;

	return port;
}

unsigned int ia_css_isys_rx_get_interrupt_reg(enum mipi_port_id port)
{
	return receiver_port_reg_load(RX0_ID,
				      port,
				      _HRT_CSS_RECEIVER_IRQ_STATUS_REG_IDX);
}

void ia_css_rx_get_irq_info(unsigned int *irq_infos)
{
	ia_css_rx_port_get_irq_info(MIPI_PORT1_ID, irq_infos);
}

void ia_css_rx_port_get_irq_info(enum mipi_port_id api_port,
				 unsigned int *irq_infos)
{
	enum mipi_port_id port = ia_css_isys_port_to_mipi_port(api_port);

	ia_css_isys_rx_get_irq_info(port, irq_infos);
}

void ia_css_isys_rx_get_irq_info(enum mipi_port_id port,
				 unsigned int *irq_infos)
{
	unsigned int bits;

	assert(irq_infos);
	bits = ia_css_isys_rx_get_interrupt_reg(port);
	*irq_infos = ia_css_isys_rx_translate_irq_infos(bits);
}

/* Translate register bits to CSS API enum mask */
unsigned int ia_css_isys_rx_translate_irq_infos(unsigned int bits)
{
	unsigned int infos = 0;

	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_OVERRUN_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_BUFFER_OVERRUN;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_INIT_TIMEOUT_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_INIT_TIMEOUT;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_SLEEP_MODE_ENTRY_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ENTER_SLEEP_MODE;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_SLEEP_MODE_EXIT_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_EXIT_SLEEP_MODE;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_ECC_CORRECTED_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ECC_CORRECTED;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_SOT_HS_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_SOT;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_SOT_SYNC_HS_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_SOT_SYNC;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_CONTROL_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_CONTROL;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_ECC_DOUBLE_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_ECC_DOUBLE;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_CRC_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_CRC;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_ID_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_UNKNOWN_ID;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_FRAME_SYNC_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_FRAME_SYNC;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_FRAME_DATA_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_FRAME_DATA;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_DATA_TIMEOUT_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_DATA_TIMEOUT;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_ESCAPE_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_UNKNOWN_ESC;
	if (bits & (1U << _HRT_CSS_RECEIVER_IRQ_ERR_LINE_SYNC_BIT))
		infos |= IA_CSS_RX_IRQ_INFO_ERR_LINE_SYNC;

	return infos;
}

void ia_css_rx_clear_irq_info(unsigned int irq_infos)
{
	ia_css_rx_port_clear_irq_info(MIPI_PORT1_ID, irq_infos);
}

void ia_css_rx_port_clear_irq_info(enum mipi_port_id api_port,
				   unsigned int irq_infos)
{
	enum mipi_port_id port = ia_css_isys_port_to_mipi_port(api_port);

	ia_css_isys_rx_clear_irq_info(port, irq_infos);
}

void ia_css_isys_rx_clear_irq_info(enum mipi_port_id port,
				   unsigned int irq_infos)
{
	hrt_data bits = receiver_port_reg_load(RX0_ID,
					       port,
					       _HRT_CSS_RECEIVER_IRQ_ENABLE_REG_IDX);

	/* MW: Why do we remap the receiver bitmap */
	if (irq_infos & IA_CSS_RX_IRQ_INFO_BUFFER_OVERRUN)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_OVERRUN_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_INIT_TIMEOUT)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_INIT_TIMEOUT_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ENTER_SLEEP_MODE)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_SLEEP_MODE_ENTRY_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_EXIT_SLEEP_MODE)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_SLEEP_MODE_EXIT_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ECC_CORRECTED)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_ECC_CORRECTED_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_SOT)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_SOT_HS_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_SOT_SYNC)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_SOT_SYNC_HS_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_CONTROL)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_CONTROL_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_ECC_DOUBLE)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_ECC_DOUBLE_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_CRC)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_CRC_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_UNKNOWN_ID)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_ID_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_FRAME_SYNC)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_FRAME_SYNC_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_FRAME_DATA)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_FRAME_DATA_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_DATA_TIMEOUT)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_DATA_TIMEOUT_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_UNKNOWN_ESC)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_ESCAPE_BIT;
	if (irq_infos & IA_CSS_RX_IRQ_INFO_ERR_LINE_SYNC)
		bits |= 1U << _HRT_CSS_RECEIVER_IRQ_ERR_LINE_SYNC_BIT;

	receiver_port_reg_store(RX0_ID,
				port,
				_HRT_CSS_RECEIVER_IRQ_ENABLE_REG_IDX, bits);

	return;
}

static int ia_css_isys_2400_set_fmt_type(enum atomisp_input_format input_format,
					 unsigned int *fmt_type)
{
	switch (input_format) {
	case ATOMISP_INPUT_FORMAT_RGB_888:
		*fmt_type = MIPI_FORMAT_2400_RGB888;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_555:
		*fmt_type = MIPI_FORMAT_2400_RGB555;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_444:
		*fmt_type = MIPI_FORMAT_2400_RGB444;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_565:
		*fmt_type = MIPI_FORMAT_2400_RGB565;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_666:
		*fmt_type = MIPI_FORMAT_2400_RGB666;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_8:
		*fmt_type = MIPI_FORMAT_2400_RAW8;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_10:
		*fmt_type = MIPI_FORMAT_2400_RAW10;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_6:
		*fmt_type = MIPI_FORMAT_2400_RAW6;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_7:
		*fmt_type = MIPI_FORMAT_2400_RAW7;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_12:
		*fmt_type = MIPI_FORMAT_2400_RAW12;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_14:
		*fmt_type = MIPI_FORMAT_2400_RAW14;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_8:
		*fmt_type = MIPI_FORMAT_2400_YUV420_8;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_10:
		*fmt_type = MIPI_FORMAT_2400_YUV420_10;
		break;
	case ATOMISP_INPUT_FORMAT_YUV422_8:
		*fmt_type = MIPI_FORMAT_2400_YUV422_8;
		break;
	case ATOMISP_INPUT_FORMAT_YUV422_10:
		*fmt_type = MIPI_FORMAT_2400_YUV422_10;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY:
		*fmt_type = MIPI_FORMAT_2400_YUV420_8_LEGACY;
		break;
	case ATOMISP_INPUT_FORMAT_EMBEDDED:
		*fmt_type = MIPI_FORMAT_2400_EMBEDDED;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_16:
		/* This is not specified by Arasan, so we use
		 * 17 for now.
		 */
		*fmt_type = MIPI_FORMAT_2400_RAW16;
		break;
	case ATOMISP_INPUT_FORMAT_BINARY_8:
		*fmt_type = MIPI_FORMAT_2400_CUSTOM0;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_16:
	case ATOMISP_INPUT_FORMAT_YUV422_16:
	default:
		return -EINVAL;
	}
	return 0;
}

static int ia_css_isys_2401_set_fmt_type(enum atomisp_input_format input_format,
					 unsigned int *fmt_type)
{
	switch (input_format) {
	case ATOMISP_INPUT_FORMAT_RGB_888:
		*fmt_type = MIPI_FORMAT_2401_RGB888;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_555:
		*fmt_type = MIPI_FORMAT_2401_RGB555;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_444:
		*fmt_type = MIPI_FORMAT_2401_RGB444;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_565:
		*fmt_type = MIPI_FORMAT_2401_RGB565;
		break;
	case ATOMISP_INPUT_FORMAT_RGB_666:
		*fmt_type = MIPI_FORMAT_2401_RGB666;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_8:
		*fmt_type = MIPI_FORMAT_2401_RAW8;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_10:
		*fmt_type = MIPI_FORMAT_2401_RAW10;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_6:
		*fmt_type = MIPI_FORMAT_2401_RAW6;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_7:
		*fmt_type = MIPI_FORMAT_2401_RAW7;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_12:
		*fmt_type = MIPI_FORMAT_2401_RAW12;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_14:
		*fmt_type = MIPI_FORMAT_2401_RAW14;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_8:
		*fmt_type = MIPI_FORMAT_2401_YUV420_8;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_10:
		*fmt_type = MIPI_FORMAT_2401_YUV420_10;
		break;
	case ATOMISP_INPUT_FORMAT_YUV422_8:
		*fmt_type = MIPI_FORMAT_2401_YUV422_8;
		break;
	case ATOMISP_INPUT_FORMAT_YUV422_10:
		*fmt_type = MIPI_FORMAT_2401_YUV422_10;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY:
		*fmt_type = MIPI_FORMAT_2401_YUV420_8_LEGACY;
		break;
	case ATOMISP_INPUT_FORMAT_EMBEDDED:
		*fmt_type = MIPI_FORMAT_2401_EMBEDDED;
		break;
	case ATOMISP_INPUT_FORMAT_USER_DEF1:
		*fmt_type = MIPI_FORMAT_2401_CUSTOM0;
		break;
	case ATOMISP_INPUT_FORMAT_USER_DEF2:
		*fmt_type = MIPI_FORMAT_2401_CUSTOM1;
		break;
	case ATOMISP_INPUT_FORMAT_USER_DEF3:
		*fmt_type = MIPI_FORMAT_2401_CUSTOM2;
		break;
	case ATOMISP_INPUT_FORMAT_USER_DEF4:
		*fmt_type = MIPI_FORMAT_2401_CUSTOM3;
		break;
	case ATOMISP_INPUT_FORMAT_USER_DEF5:
		*fmt_type = MIPI_FORMAT_2401_CUSTOM4;
		break;
	case ATOMISP_INPUT_FORMAT_USER_DEF6:
		*fmt_type = MIPI_FORMAT_2401_CUSTOM5;
		break;
	case ATOMISP_INPUT_FORMAT_USER_DEF7:
		*fmt_type = MIPI_FORMAT_2401_CUSTOM6;
		break;
	case ATOMISP_INPUT_FORMAT_USER_DEF8:
		*fmt_type = MIPI_FORMAT_2401_CUSTOM7;
		break;

	case ATOMISP_INPUT_FORMAT_YUV420_16:
	case ATOMISP_INPUT_FORMAT_YUV422_16:
	default:
		return -EINVAL;
	}
	return 0;
}

int ia_css_isys_convert_stream_format_to_mipi_format(
    enum atomisp_input_format input_format,
    mipi_predictor_t compression,
    unsigned int *fmt_type)
{
	assert(fmt_type);
	/*
	 * Custom (user defined) modes. Used for compressed
	 * MIPI transfers
	 *
	 * Checkpatch thinks the indent before "if" is suspect
	 * I think the only suspect part is the missing "else"
	 * because of the return.
	 */
	if (compression != MIPI_PREDICTOR_NONE) {
		switch (input_format) {
		case ATOMISP_INPUT_FORMAT_RAW_6:
			*fmt_type = 6;
			break;
		case ATOMISP_INPUT_FORMAT_RAW_7:
			*fmt_type = 7;
			break;
		case ATOMISP_INPUT_FORMAT_RAW_8:
			*fmt_type = 8;
			break;
		case ATOMISP_INPUT_FORMAT_RAW_10:
			*fmt_type = 10;
			break;
		case ATOMISP_INPUT_FORMAT_RAW_12:
			*fmt_type = 12;
			break;
		case ATOMISP_INPUT_FORMAT_RAW_14:
			*fmt_type = 14;
			break;
		case ATOMISP_INPUT_FORMAT_RAW_16:
			*fmt_type = 16;
			break;
		default:
			return -EINVAL;
		}
		return 0;
	}
	/*
	 * This mapping comes from the Arasan CSS function spec
	 * (CSS_func_spec1.08_ahb_sep29_08.pdf).
	 *
	 * MW: For some reason the mapping is not 1-to-1
	 */
	if (IS_ISP2401)
		return ia_css_isys_2401_set_fmt_type(input_format, fmt_type);
	else
		return ia_css_isys_2400_set_fmt_type(input_format, fmt_type);
}

static mipi_predictor_t sh_css_csi2_compression_type_2_mipi_predictor(
    enum ia_css_csi2_compression_type type)
{
	mipi_predictor_t predictor = MIPI_PREDICTOR_NONE;

	switch (type) {
	case IA_CSS_CSI2_COMPRESSION_TYPE_1:
		predictor = MIPI_PREDICTOR_TYPE1 - 1;
		break;
	case IA_CSS_CSI2_COMPRESSION_TYPE_2:
		predictor = MIPI_PREDICTOR_TYPE2 - 1;
		break;
	default:
		break;
	}
	return predictor;
}

int ia_css_isys_convert_compressed_format(
    struct ia_css_csi2_compression *comp,
    struct isp2401_input_system_cfg_s *cfg)
{
	int err = 0;

	assert(comp);
	assert(cfg);

	if (comp->type != IA_CSS_CSI2_COMPRESSION_TYPE_NONE) {
		/* compression register bit slicing
		4 bit for each user defined data type
			3 bit indicate compression scheme
				000 No compression
				001 10-6-10
				010 10-7-10
				011 10-8-10
				100 12-6-12
				101 12-6-12
				100 12-7-12
				110 12-8-12
			1 bit indicate predictor
		*/
		if (comp->uncompressed_bits_per_pixel == UNCOMPRESSED_BITS_PER_PIXEL_10) {
			switch (comp->compressed_bits_per_pixel) {
			case COMPRESSED_BITS_PER_PIXEL_6:
				cfg->csi_port_attr.comp_scheme = MIPI_COMPRESSOR_10_6_10;
				break;
			case COMPRESSED_BITS_PER_PIXEL_7:
				cfg->csi_port_attr.comp_scheme = MIPI_COMPRESSOR_10_7_10;
				break;
			case COMPRESSED_BITS_PER_PIXEL_8:
				cfg->csi_port_attr.comp_scheme = MIPI_COMPRESSOR_10_8_10;
				break;
			default:
				err = -EINVAL;
			}
		} else if (comp->uncompressed_bits_per_pixel ==
			   UNCOMPRESSED_BITS_PER_PIXEL_12) {
			switch (comp->compressed_bits_per_pixel) {
			case COMPRESSED_BITS_PER_PIXEL_6:
				cfg->csi_port_attr.comp_scheme = MIPI_COMPRESSOR_12_6_12;
				break;
			case COMPRESSED_BITS_PER_PIXEL_7:
				cfg->csi_port_attr.comp_scheme = MIPI_COMPRESSOR_12_7_12;
				break;
			case COMPRESSED_BITS_PER_PIXEL_8:
				cfg->csi_port_attr.comp_scheme = MIPI_COMPRESSOR_12_8_12;
				break;
			default:
				err = -EINVAL;
			}
		} else
			err = -EINVAL;
		cfg->csi_port_attr.comp_predictor =
		    sh_css_csi2_compression_type_2_mipi_predictor(comp->type);
		cfg->csi_port_attr.comp_enable = true;
	} else /* No compression */
		cfg->csi_port_attr.comp_enable = false;
	return err;
}

unsigned int ia_css_csi2_calculate_input_system_alignment(
    enum atomisp_input_format fmt_type)
{
	unsigned int memory_alignment_in_bytes = HIVE_ISP_DDR_WORD_BYTES;

	switch (fmt_type) {
	case ATOMISP_INPUT_FORMAT_RAW_6:
	case ATOMISP_INPUT_FORMAT_RAW_7:
	case ATOMISP_INPUT_FORMAT_RAW_8:
	case ATOMISP_INPUT_FORMAT_RAW_10:
	case ATOMISP_INPUT_FORMAT_RAW_12:
	case ATOMISP_INPUT_FORMAT_RAW_14:
		memory_alignment_in_bytes = 2 * ISP_VEC_NELEMS;
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_8:
	case ATOMISP_INPUT_FORMAT_YUV422_8:
	case ATOMISP_INPUT_FORMAT_USER_DEF1:
	case ATOMISP_INPUT_FORMAT_USER_DEF2:
	case ATOMISP_INPUT_FORMAT_USER_DEF3:
	case ATOMISP_INPUT_FORMAT_USER_DEF4:
	case ATOMISP_INPUT_FORMAT_USER_DEF5:
	case ATOMISP_INPUT_FORMAT_USER_DEF6:
	case ATOMISP_INPUT_FORMAT_USER_DEF7:
	case ATOMISP_INPUT_FORMAT_USER_DEF8:
		/* Planar YUV formats need to have all planes aligned, this means
		 * double the alignment for the Y plane if the horizontal decimation is 2. */
		memory_alignment_in_bytes = 2 * HIVE_ISP_DDR_WORD_BYTES;
		break;
	case ATOMISP_INPUT_FORMAT_EMBEDDED:
	default:
		memory_alignment_in_bytes = HIVE_ISP_DDR_WORD_BYTES;
		break;
	}
	return memory_alignment_in_bytes;
}


static const mipi_lane_cfg_t MIPI_PORT_LANES[N_RX_MODE][N_MIPI_PORT_ID] = {
	{MIPI_4LANE_CFG, MIPI_1LANE_CFG, MIPI_0LANE_CFG},
	{MIPI_3LANE_CFG, MIPI_1LANE_CFG, MIPI_0LANE_CFG},
	{MIPI_2LANE_CFG, MIPI_1LANE_CFG, MIPI_0LANE_CFG},
	{MIPI_1LANE_CFG, MIPI_1LANE_CFG, MIPI_0LANE_CFG},
	{MIPI_2LANE_CFG, MIPI_1LANE_CFG, MIPI_2LANE_CFG},
	{MIPI_3LANE_CFG, MIPI_1LANE_CFG, MIPI_1LANE_CFG},
	{MIPI_2LANE_CFG, MIPI_1LANE_CFG, MIPI_1LANE_CFG},
	{MIPI_1LANE_CFG, MIPI_1LANE_CFG, MIPI_1LANE_CFG}
};

void ia_css_isys_rx_configure(const rx_cfg_t *config,
			      const enum ia_css_input_mode input_mode)
{
	bool any_port_enabled = false;
	enum mipi_port_id port;

	if ((!config)
	    || (config->mode >= N_RX_MODE)
	    || (config->port >= N_MIPI_PORT_ID)) {
		assert(0);
		return;
	}
	for (port = (enum mipi_port_id)0; port < N_MIPI_PORT_ID; port++) {
		if (is_receiver_port_enabled(RX0_ID, port))
			any_port_enabled = true;
	}
	/* AM: Check whether this is a problem with multiple
	 * streams. MS: This is the case. */

	port = config->port;
	receiver_port_enable(RX0_ID, port, false);

	port = config->port;

	/* AM: Check whether this is a problem with multiple streams. */
	if (MIPI_PORT_LANES[config->mode][port] != MIPI_0LANE_CFG) {
		receiver_port_reg_store(RX0_ID, port,
					_HRT_CSS_RECEIVER_FUNC_PROG_REG_IDX,
					config->timeout);
		receiver_port_reg_store(RX0_ID, port,
					_HRT_CSS_RECEIVER_2400_INIT_COUNT_REG_IDX,
					config->initcount);
		receiver_port_reg_store(RX0_ID, port,
					_HRT_CSS_RECEIVER_2400_SYNC_COUNT_REG_IDX,
					config->synccount);
		receiver_port_reg_store(RX0_ID, port,
					_HRT_CSS_RECEIVER_2400_RX_COUNT_REG_IDX,
					config->rxcount);

		if (input_mode != IA_CSS_INPUT_MODE_BUFFERED_SENSOR) {
			/* MW: A bit of a hack, straight wiring of the capture
			 * units,assuming they are linearly enumerated. */
			input_system_sub_system_reg_store(INPUT_SYSTEM0_ID,
							  GPREGS_UNIT0_ID,
							  HIVE_ISYS_GPREG_MULTICAST_A_IDX
							  + (unsigned int)port,
							  INPUT_SYSTEM_CSI_BACKEND);
			/* MW: Like the integration test example we overwite,
			 * the GPREG_MUX register */
			input_system_sub_system_reg_store(INPUT_SYSTEM0_ID,
							  GPREGS_UNIT0_ID,
							  HIVE_ISYS_GPREG_MUX_IDX,
							  (input_system_multiplex_t)port);
		} else {
			/*
			 * AM: A bit of a hack, wiring the input system.
			 */
			input_system_sub_system_reg_store(INPUT_SYSTEM0_ID,
							  GPREGS_UNIT0_ID,
							  HIVE_ISYS_GPREG_MULTICAST_A_IDX
							  + (unsigned int)port,
							  INPUT_SYSTEM_INPUT_BUFFER);
			input_system_sub_system_reg_store(INPUT_SYSTEM0_ID,
							  GPREGS_UNIT0_ID,
							  HIVE_ISYS_GPREG_MUX_IDX,
							  INPUT_SYSTEM_ACQUISITION_UNIT);
		}
	}
	/*
	 * The 2ppc is shared for all ports, so we cannot
	 * disable->configure->enable individual ports
	 */
	/* AM: Check whether this is a problem with multiple streams. */
	/* MS: 2ppc should be a property per binary and should be
	 * enabled/disabled per binary.
	 * Currently it is implemented as a system wide setting due
	 * to effort and risks. */
	if (!any_port_enabled) {
		receiver_reg_store(RX0_ID,
				   _HRT_CSS_RECEIVER_TWO_PIXEL_EN_REG_IDX,
				   config->is_two_ppc);
		receiver_reg_store(RX0_ID, _HRT_CSS_RECEIVER_BE_TWO_PPC_REG_IDX,
				   config->is_two_ppc);
	}
	receiver_port_enable(RX0_ID, port, true);
	/* TODO: JB: need to add the beneath used define to mizuchi */
	/* sh_css_sw_hive_isp_css_2400_system_20121224_0125\css
	 *                      \hrt\input_system_defs.h
	 * #define INPUT_SYSTEM_CSI_RECEIVER_SELECT_BACKENG 0X207
	 */
	/* TODO: need better name for define
	 * input_system_reg_store(INPUT_SYSTEM0_ID,
	 *                INPUT_SYSTEM_CSI_RECEIVER_SELECT_BACKENG, 1);
	 */
	input_system_reg_store(INPUT_SYSTEM0_ID, 0x207, 1);

	return;
}

void ia_css_isys_rx_disable(void)
{
	enum mipi_port_id port;

	for (port = (enum mipi_port_id)0; port < N_MIPI_PORT_ID; port++) {
		receiver_port_reg_store(RX0_ID, port,
					_HRT_CSS_RECEIVER_DEVICE_READY_REG_IDX,
					false);
	}
	return;
}
