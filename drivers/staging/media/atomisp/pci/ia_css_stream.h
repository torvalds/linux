/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _IA_CSS_STREAM_H_
#define _IA_CSS_STREAM_H_

#include <type_support.h>
#include <system_local.h>
#if !defined(ISP2401)
#include <input_system.h>
#endif
#include "ia_css_types.h"
#include "ia_css_stream_public.h"

/**
 * structure to hold all internal stream related information
 */
struct ia_css_stream {
	struct ia_css_stream_config    config;
	struct ia_css_stream_info      info;
#if !defined(ISP2401)
	rx_cfg_t                       csi_rx_config;
#endif
	bool                           reconfigure_css_rx;
	struct ia_css_pipe            *last_pipe;
	int                            num_pipes;
	struct ia_css_pipe           **pipes;
	struct ia_css_pipe            *continuous_pipe;
	struct ia_css_isp_parameters  *isp_params_configs;
	struct ia_css_isp_parameters  *per_frame_isp_params_configs;

	bool                           cont_capt;
	bool                           disable_cont_vf;

	/* ISP2401 */
	bool                           stop_copy_preview;
	bool                           started;
};

/* @brief Get a binary in the stream, which binary has the shading correction.
 *
 * @param[in] stream: The stream.
 * @return	The binary which has the shading correction.
 *
 */
struct ia_css_binary *
ia_css_stream_get_shading_correction_binary(const struct ia_css_stream *stream);

struct ia_css_binary *
ia_css_stream_get_dvs_binary(const struct ia_css_stream *stream);

struct ia_css_binary *
ia_css_stream_get_3a_binary(const struct ia_css_stream *stream);

unsigned int
ia_css_stream_input_format_bits_per_pixel(struct ia_css_stream *stream);

bool
sh_css_params_set_binning_factor(struct ia_css_stream *stream,
				 unsigned int sensor_binning);

void
sh_css_invalidate_params(struct ia_css_stream *stream);

/* The following functions are used for testing purposes only */
const struct ia_css_fpn_table *
ia_css_get_fpn_table(struct ia_css_stream *stream);

/* @brief Get a pointer to the shading table.
 *
 * @param[in] stream: The stream.
 * @return	The pointer to the shading table.
 *
 */
struct ia_css_shading_table *
ia_css_get_shading_table(struct ia_css_stream *stream);

void
ia_css_get_isp_dis_coefficients(struct ia_css_stream *stream,
				short *horizontal_coefficients,
				short *vertical_coefficients);

void
ia_css_get_isp_dvs2_coefficients(struct ia_css_stream *stream,
				 short *hor_coefs_odd_real,
				 short *hor_coefs_odd_imag,
				 short *hor_coefs_even_real,
				 short *hor_coefs_even_imag,
				 short *ver_coefs_odd_real,
				 short *ver_coefs_odd_imag,
				 short *ver_coefs_even_real,
				 short *ver_coefs_even_imag);

int
ia_css_stream_isp_parameters_init(struct ia_css_stream *stream);

void
ia_css_stream_isp_parameters_uninit(struct ia_css_stream *stream);

#endif /*_IA_CSS_STREAM_H_*/
