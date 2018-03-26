#ifndef ISP2401
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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
#else
/*
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#include "system_global.h"
#include <linux/kernel.h>

#ifdef USE_INPUT_SYSTEM_VERSION_2

#include "ia_css_ifmtr.h"
#include <math_support.h>
#include "sh_css_internal.h"
#include "input_formatter.h"
#include "assert_support.h"
#include "sh_css_sp.h"
#include "isp/modes/interface/input_buf.isp.h"

/************************************************************
 * Static functions declarations
 ************************************************************/
static enum ia_css_err ifmtr_start_column(
		const struct ia_css_stream_config *config,
		unsigned int bin_in,
		unsigned int *start_column);

static enum ia_css_err ifmtr_input_start_line(
		const struct ia_css_stream_config *config,
		unsigned int bin_in,
		unsigned int *start_line);

static void ifmtr_set_if_blocking_mode(
		const input_formatter_cfg_t * const config_a,
		const input_formatter_cfg_t * const config_b);

/************************************************************
 * Public functions
 ************************************************************/

/* ISP expects GRBG bayer order, we skip one line and/or one row
 * to correct in case the input bayer order is different.
 */
unsigned int ia_css_ifmtr_lines_needed_for_bayer_order(
		const struct ia_css_stream_config *config)
{
	assert(config != NULL);
	if ((IA_CSS_BAYER_ORDER_BGGR == config->input_config.bayer_order)
	    || (IA_CSS_BAYER_ORDER_GBRG == config->input_config.bayer_order))
		return 1;

	return 0;
}

unsigned int ia_css_ifmtr_columns_needed_for_bayer_order(
		const struct ia_css_stream_config *config)
{
	assert(config != NULL);
	if ((IA_CSS_BAYER_ORDER_RGGB == config->input_config.bayer_order)
	    || (IA_CSS_BAYER_ORDER_GBRG == config->input_config.bayer_order))
		return 1;

	return 0;
}

enum ia_css_err ia_css_ifmtr_configure(struct ia_css_stream_config *config,
				       struct ia_css_binary *binary)
{
	unsigned int start_line, start_column = 0,
	    cropped_height,
	    cropped_width,
	    num_vectors,
	    buffer_height = 2,
	    buffer_width,
	    two_ppc,
	    vmem_increment = 0,
	    deinterleaving = 0,
	    deinterleaving_b = 0,
	    width_a = 0,
	    width_b = 0,
	    bits_per_pixel,
	    vectors_per_buffer,
	    vectors_per_line = 0,
	    buffers_per_line = 0,
	    buf_offset_a = 0,
	    buf_offset_b = 0,
	    line_width = 0,
	    width_b_factor = 1, start_column_b,
	    left_padding = 0;
	input_formatter_cfg_t if_a_config, if_b_config;
	enum atomisp_input_format input_format;
	enum ia_css_err err = IA_CSS_SUCCESS;
	uint8_t if_config_index;

	/* Determine which input formatter config set is targeted. */
	/* Index is equal to the CSI-2 port used. */
	enum mipi_port_id port;

	if (binary) {
		cropped_height = binary->in_frame_info.res.height;
		cropped_width = binary->in_frame_info.res.width;
		/* This should correspond to the input buffer definition for
		ISP binaries in input_buf.isp.h */
		if (binary->info->sp.enable.continuous && binary->info->sp.pipeline.mode != IA_CSS_BINARY_MODE_COPY)
			buffer_width = MAX_VECTORS_PER_INPUT_LINE_CONT * ISP_VEC_NELEMS;
		else
			buffer_width = binary->info->sp.input.max_width;
		input_format = binary->input_format;
	} else {
		/* sp raw copy pipe (IA_CSS_PIPE_MODE_COPY): binary is NULL */
		cropped_height = config->input_config.input_res.height;
		cropped_width = config->input_config.input_res.width;
		buffer_width = MAX_VECTORS_PER_INPUT_LINE_CONT * ISP_VEC_NELEMS;
		input_format = config->input_config.format;
	}
	two_ppc = config->pixels_per_clock == 2;
	if (config->mode == IA_CSS_INPUT_MODE_SENSOR
	    || config->mode == IA_CSS_INPUT_MODE_BUFFERED_SENSOR) {
		port = config->source.port.port;
		if_config_index = (uint8_t) (port - MIPI_PORT0_ID);
	} else if (config->mode == IA_CSS_INPUT_MODE_MEMORY) {
		if_config_index = SH_CSS_IF_CONFIG_NOT_NEEDED;
	} else {
		if_config_index = 0;
	}

	assert(if_config_index <= SH_CSS_MAX_IF_CONFIGS
	       || if_config_index == SH_CSS_IF_CONFIG_NOT_NEEDED);

	/* TODO: check to see if input is RAW and if current mode interprets
	 * RAW data in any particular bayer order. copy binary with output
	 * format other than raw should not result in dropping lines and/or
	 * columns.
	 */
	err = ifmtr_input_start_line(config, cropped_height, &start_line);
	if (err != IA_CSS_SUCCESS)
		return err;
	err = ifmtr_start_column(config, cropped_width, &start_column);
	if (err != IA_CSS_SUCCESS)
		return err;

	if (config->left_padding == -1)
		if (!binary)
			/* sp raw copy pipe: set left_padding value */
			left_padding = 0;
		else
			left_padding = binary->left_padding;
	else
		left_padding = 2*ISP_VEC_NELEMS - config->left_padding;


	if (left_padding) {
		num_vectors = CEIL_DIV(cropped_width + left_padding,
				       ISP_VEC_NELEMS);
	} else {
		num_vectors = CEIL_DIV(cropped_width, ISP_VEC_NELEMS);
		num_vectors *= buffer_height;
		/* todo: in case of left padding,
		   num_vectors is vectors per line,
		   otherwise vectors per line * buffer_height. */
	}

	start_column_b = start_column;

	bits_per_pixel = input_formatter_get_alignment(INPUT_FORMATTER0_ID)
	    * 8 / ISP_VEC_NELEMS;
	switch (input_format) {
	case ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY:
		if (two_ppc) {
			vmem_increment = 1;
			deinterleaving = 1;
			deinterleaving_b = 1;
			/* half lines */
			width_a = cropped_width * deinterleaving / 2;
			width_b_factor = 2;
			/* full lines */
			width_b = width_a * width_b_factor;
			buffer_width *= deinterleaving * 2;
			/* Patch from bayer to yuv */
			num_vectors *= deinterleaving;
			buf_offset_b = buffer_width / 2 / ISP_VEC_NELEMS;
			vectors_per_line = num_vectors / buffer_height;
			/* Even lines are half size */
			line_width = vectors_per_line *
			    input_formatter_get_alignment(INPUT_FORMATTER0_ID) /
			    2;
			start_column /= 2;
		} else {
			vmem_increment = 1;
			deinterleaving = 3;
			width_a = cropped_width * deinterleaving / 2;
			buffer_width = buffer_width * deinterleaving / 2;
			/* Patch from bayer to yuv */
			num_vectors = num_vectors / 2 * deinterleaving;
			start_column = start_column * deinterleaving / 2;
		}
		break;
	case ATOMISP_INPUT_FORMAT_YUV420_8:
	case ATOMISP_INPUT_FORMAT_YUV420_10:
	case ATOMISP_INPUT_FORMAT_YUV420_16:
		if (two_ppc) {
			vmem_increment = 1;
			deinterleaving = 1;
			width_a = width_b = cropped_width * deinterleaving / 2;
			buffer_width *= deinterleaving * 2;
			num_vectors *= deinterleaving;
			buf_offset_b = buffer_width / 2 / ISP_VEC_NELEMS;
			vectors_per_line = num_vectors / buffer_height;
			/* Even lines are half size */
			line_width = vectors_per_line *
			    input_formatter_get_alignment(INPUT_FORMATTER0_ID) /
			    2;
			start_column *= deinterleaving;
			start_column /= 2;
			start_column_b = start_column;
		} else {
			vmem_increment = 1;
			deinterleaving = 1;
			width_a = cropped_width * deinterleaving;
			buffer_width *= deinterleaving * 2;
			num_vectors *= deinterleaving;
			start_column *= deinterleaving;
		}
		break;
	case ATOMISP_INPUT_FORMAT_YUV422_8:
	case ATOMISP_INPUT_FORMAT_YUV422_10:
	case ATOMISP_INPUT_FORMAT_YUV422_16:
		if (two_ppc) {
			vmem_increment = 1;
			deinterleaving = 1;
			width_a = width_b = cropped_width * deinterleaving;
			buffer_width *= deinterleaving * 2;
			num_vectors *= deinterleaving;
			start_column *= deinterleaving;
			buf_offset_b = buffer_width / 2 / ISP_VEC_NELEMS;
			start_column_b = start_column;
		} else {
			vmem_increment = 1;
			deinterleaving = 2;
			width_a = cropped_width * deinterleaving;
			buffer_width *= deinterleaving;
			num_vectors *= deinterleaving;
			start_column *= deinterleaving;
		}
		break;
	case ATOMISP_INPUT_FORMAT_RGB_444:
	case ATOMISP_INPUT_FORMAT_RGB_555:
	case ATOMISP_INPUT_FORMAT_RGB_565:
	case ATOMISP_INPUT_FORMAT_RGB_666:
	case ATOMISP_INPUT_FORMAT_RGB_888:
		num_vectors *= 2;
		if (two_ppc) {
			deinterleaving = 2;	/* BR in if_a, G in if_b */
			deinterleaving_b = 1;	/* BR in if_a, G in if_b */
			buffers_per_line = 4;
			start_column_b = start_column;
			start_column *= deinterleaving;
			start_column_b *= deinterleaving_b;
		} else {
			deinterleaving = 3;	/* BGR */
			buffers_per_line = 3;
			start_column *= deinterleaving;
		}
		vmem_increment = 1;
		width_a = cropped_width * deinterleaving;
		width_b = cropped_width * deinterleaving_b;
		buffer_width *= buffers_per_line;
		/* Patch from bayer to rgb */
		num_vectors = num_vectors / 2 * deinterleaving;
		buf_offset_b = buffer_width / 2 / ISP_VEC_NELEMS;
		break;
	case ATOMISP_INPUT_FORMAT_RAW_6:
	case ATOMISP_INPUT_FORMAT_RAW_7:
	case ATOMISP_INPUT_FORMAT_RAW_8:
	case ATOMISP_INPUT_FORMAT_RAW_10:
	case ATOMISP_INPUT_FORMAT_RAW_12:
		if (two_ppc) {
			int crop_col = (start_column % 2) == 1;
			vmem_increment = 2;
			deinterleaving = 1;
			width_a = width_b = cropped_width / 2;

			/* When two_ppc is enabled AND we need to crop one extra
			 * column, if_a crops by one extra and we swap the
			 * output offsets to interleave the bayer pattern in
			 * the correct order.
			 */
			buf_offset_a   = crop_col ? 1 : 0;
			buf_offset_b   = crop_col ? 0 : 1;
			start_column_b = start_column / 2;
			start_column   = start_column / 2 + crop_col;
		} else {
			vmem_increment = 1;
			deinterleaving = 2;
			if ((!binary) || (config->continuous && binary
				&& binary->info->sp.pipeline.mode == IA_CSS_BINARY_MODE_COPY)) {
				/* !binary -> sp raw copy pipe, no deinterleaving */
				deinterleaving = 1;
			}
			width_a = cropped_width;
			/* Must be multiple of deinterleaving */
			num_vectors = CEIL_MUL(num_vectors, deinterleaving);
		}
		buffer_height *= 2;
		if ((!binary) || config->continuous)
			/* !binary -> sp raw copy pipe */
			buffer_height *= 2;
		vectors_per_line = CEIL_DIV(cropped_width, ISP_VEC_NELEMS);
		vectors_per_line = CEIL_MUL(vectors_per_line, deinterleaving);
		break;
	case ATOMISP_INPUT_FORMAT_RAW_14:
	case ATOMISP_INPUT_FORMAT_RAW_16:
		if (two_ppc) {
			num_vectors *= 2;
			vmem_increment = 1;
			deinterleaving = 2;
			width_a = width_b = cropped_width;
			/* B buffer is one line further */
			buf_offset_b = buffer_width / ISP_VEC_NELEMS;
			bits_per_pixel *= 2;
		} else {
			vmem_increment = 1;
			deinterleaving = 2;
			width_a = cropped_width;
			start_column /= deinterleaving;
		}
		buffer_height *= 2;
		break;
	case ATOMISP_INPUT_FORMAT_BINARY_8:
	case ATOMISP_INPUT_FORMAT_GENERIC_SHORT1:
	case ATOMISP_INPUT_FORMAT_GENERIC_SHORT2:
	case ATOMISP_INPUT_FORMAT_GENERIC_SHORT3:
	case ATOMISP_INPUT_FORMAT_GENERIC_SHORT4:
	case ATOMISP_INPUT_FORMAT_GENERIC_SHORT5:
	case ATOMISP_INPUT_FORMAT_GENERIC_SHORT6:
	case ATOMISP_INPUT_FORMAT_GENERIC_SHORT7:
	case ATOMISP_INPUT_FORMAT_GENERIC_SHORT8:
	case ATOMISP_INPUT_FORMAT_YUV420_8_SHIFT:
	case ATOMISP_INPUT_FORMAT_YUV420_10_SHIFT:
	case ATOMISP_INPUT_FORMAT_EMBEDDED:
	case ATOMISP_INPUT_FORMAT_USER_DEF1:
	case ATOMISP_INPUT_FORMAT_USER_DEF2:
	case ATOMISP_INPUT_FORMAT_USER_DEF3:
	case ATOMISP_INPUT_FORMAT_USER_DEF4:
	case ATOMISP_INPUT_FORMAT_USER_DEF5:
	case ATOMISP_INPUT_FORMAT_USER_DEF6:
	case ATOMISP_INPUT_FORMAT_USER_DEF7:
	case ATOMISP_INPUT_FORMAT_USER_DEF8:
		break;
	}
	if (width_a == 0)
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	if (two_ppc)
		left_padding /= 2;

	/* Default values */
	if (left_padding)
		vectors_per_line = num_vectors;
	if (!vectors_per_line) {
		vectors_per_line = CEIL_MUL(num_vectors / buffer_height,
					    deinterleaving);
		line_width = 0;
	}
	if (!line_width)
		line_width = vectors_per_line *
		    input_formatter_get_alignment(INPUT_FORMATTER0_ID);
	if (!buffers_per_line)
		buffers_per_line = deinterleaving;
	line_width = CEIL_MUL(line_width,
			      input_formatter_get_alignment(INPUT_FORMATTER0_ID)
			      * vmem_increment);

	vectors_per_buffer = buffer_height * buffer_width / ISP_VEC_NELEMS;

	if (config->mode == IA_CSS_INPUT_MODE_TPG &&
	    ((binary && binary->info->sp.pipeline.mode == IA_CSS_BINARY_MODE_VIDEO) ||
	    (!binary))) {
		/* !binary -> sp raw copy pipe */
		/* workaround for TPG in video mode */
		start_line = 0;
		start_column = 0;
		cropped_height -= start_line;
		width_a -= start_column;
	}

	if_a_config.start_line = start_line;
	if_a_config.start_column = start_column;
	if_a_config.left_padding = left_padding / deinterleaving;
	if_a_config.cropped_height = cropped_height;
	if_a_config.cropped_width = width_a;
	if_a_config.deinterleaving = deinterleaving;
	if_a_config.buf_vecs = vectors_per_buffer;
	if_a_config.buf_start_index = buf_offset_a;
	if_a_config.buf_increment = vmem_increment;
	if_a_config.buf_eol_offset =
	    buffer_width * bits_per_pixel / 8 - line_width;
	if_a_config.is_yuv420_format =
	    (input_format == ATOMISP_INPUT_FORMAT_YUV420_8)
	    || (input_format == ATOMISP_INPUT_FORMAT_YUV420_10)
	    || (input_format == ATOMISP_INPUT_FORMAT_YUV420_16);
	if_a_config.block_no_reqs = (config->mode != IA_CSS_INPUT_MODE_SENSOR);

	if (two_ppc) {
		if (deinterleaving_b) {
			deinterleaving = deinterleaving_b;
			width_b = cropped_width * deinterleaving;
			buffer_width *= deinterleaving;
			/* Patch from bayer to rgb */
			num_vectors = num_vectors / 2 *
			    deinterleaving * width_b_factor;
			vectors_per_line = num_vectors / buffer_height;
			line_width = vectors_per_line *
			    input_formatter_get_alignment(INPUT_FORMATTER0_ID);
		}
		if_b_config.start_line = start_line;
		if_b_config.start_column = start_column_b;
		if_b_config.left_padding = left_padding / deinterleaving;
		if_b_config.cropped_height = cropped_height;
		if_b_config.cropped_width = width_b;
		if_b_config.deinterleaving = deinterleaving;
		if_b_config.buf_vecs = vectors_per_buffer;
		if_b_config.buf_start_index = buf_offset_b;
		if_b_config.buf_increment = vmem_increment;
		if_b_config.buf_eol_offset =
		    buffer_width * bits_per_pixel / 8 - line_width;
		if_b_config.is_yuv420_format =
		    input_format == ATOMISP_INPUT_FORMAT_YUV420_8
		    || input_format == ATOMISP_INPUT_FORMAT_YUV420_10
		    || input_format == ATOMISP_INPUT_FORMAT_YUV420_16;
		if_b_config.block_no_reqs =
		    (config->mode != IA_CSS_INPUT_MODE_SENSOR);

		if (SH_CSS_IF_CONFIG_NOT_NEEDED != if_config_index) {
			assert(if_config_index <= SH_CSS_MAX_IF_CONFIGS);

			ifmtr_set_if_blocking_mode(&if_a_config, &if_b_config);
			/* Set the ifconfigs to SP group */
			sh_css_sp_set_if_configs(&if_a_config, &if_b_config,
						 if_config_index);
		}
	} else {
		if (SH_CSS_IF_CONFIG_NOT_NEEDED != if_config_index) {
			assert(if_config_index <= SH_CSS_MAX_IF_CONFIGS);

			ifmtr_set_if_blocking_mode(&if_a_config, NULL);
			/* Set the ifconfigs to SP group */
			sh_css_sp_set_if_configs(&if_a_config, NULL,
						 if_config_index);
		}
	}

	return IA_CSS_SUCCESS;
}

bool ifmtr_set_if_blocking_mode_reset = true;

/************************************************************
 * Static functions
 ************************************************************/
static void ifmtr_set_if_blocking_mode(
		const input_formatter_cfg_t * const config_a,
		const input_formatter_cfg_t * const config_b)
{
	int i;
	bool block[] = { false, false, false, false };
	assert(N_INPUT_FORMATTER_ID <= (ARRAY_SIZE(block)));

#if !defined(IS_ISP_2400_SYSTEM)
#error "ifmtr_set_if_blocking_mode: ISP_SYSTEM must be one of {IS_ISP_2400_SYSTEM}"
#endif

	block[INPUT_FORMATTER0_ID] = (bool)config_a->block_no_reqs;
	if (NULL != config_b)
		block[INPUT_FORMATTER1_ID] = (bool)config_b->block_no_reqs;

	/* TODO: next could cause issues when streams are started after
	 * eachother. */
	/*IF should not be reconfigured/reset from host */
	if (ifmtr_set_if_blocking_mode_reset) {
		ifmtr_set_if_blocking_mode_reset = false;
		for (i = 0; i < N_INPUT_FORMATTER_ID; i++) {
			input_formatter_ID_t id = (input_formatter_ID_t) i;
			input_formatter_rst(id);
			input_formatter_set_fifo_blocking_mode(id, block[id]);
		}
	}

	return;
}

static enum ia_css_err ifmtr_start_column(
		const struct ia_css_stream_config *config,
		unsigned int bin_in,
		unsigned int *start_column)
{
	unsigned int in = config->input_config.input_res.width, start,
	    for_bayer = ia_css_ifmtr_columns_needed_for_bayer_order(config);

	if (bin_in + 2 * for_bayer > in)
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	/* On the hardware, we want to use the middle of the input, so we
	 * divide the start column by 2. */
	start = (in - bin_in) / 2;
	/* in case the number of extra columns is 2 or odd, we round the start
	 * column down */
	start &= ~0x1;

	/* now we add the one column (if needed) to correct for the bayer
	 * order).
	 */
	start += for_bayer;
	*start_column = start;
	return IA_CSS_SUCCESS;
}

static enum ia_css_err ifmtr_input_start_line(
		const struct ia_css_stream_config *config,
		unsigned int bin_in,
		unsigned int *start_line)
{
	unsigned int in = config->input_config.input_res.height, start,
	    for_bayer = ia_css_ifmtr_lines_needed_for_bayer_order(config);

	if (bin_in + 2 * for_bayer > in)
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	/* On the hardware, we want to use the middle of the input, so we
	 * divide the start line by 2. On the simulator, we cannot handle extra
	 * lines at the end of the frame.
	 */
	start = (in - bin_in) / 2;
	/* in case the number of extra lines is 2 or odd, we round the start
	 * line down.
	 */
	start &= ~0x1;

	/* now we add the one line (if needed) to correct for the bayer order */
	start += for_bayer;
	*start_line = start;
	return IA_CSS_SUCCESS;
}

#endif
