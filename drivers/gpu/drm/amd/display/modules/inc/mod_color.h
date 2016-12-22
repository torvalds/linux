/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */


#ifndef MOD_COLOR_H_
#define MOD_COLOR_H_

#include "dm_services.h"
#include "color_helper.h"

enum color_transfer_func {
	transfer_func_unknown,
	transfer_func_srgb,
	transfer_func_bt709,
	transfer_func_pq2084,
	transfer_func_pq2084_interim,
	transfer_func_linear_0_1,
	transfer_func_linear_0_125,
	transfer_func_dolbyvision,
	transfer_func_gamma_22,
	transfer_func_gamma_26
};

enum color_color_space {
	color_space_unsupported,
	color_space_srgb,
	color_space_bt601,
	color_space_bt709,
	color_space_xv_ycc_bt601,
	color_space_xv_ycc_bt709,
	color_space_xr_rgb,
	color_space_bt2020,
	color_space_adobe,
	color_space_dci_p3,
	color_space_sc_rgb_ms_ref,
	color_space_display_native,
	color_space_app_ctrl,
	color_space_dolby_vision,
	color_space_custom_coordinates
};

enum color_white_point_type {
	color_white_point_type_unknown,
	color_white_point_type_5000k_horizon,
	color_white_point_type_6500k_noon,
	color_white_point_type_7500k_north_sky,
	color_white_point_type_9300k,
	color_white_point_type_custom_coordinates
};

enum colorimetry_support_flag {
	xv_ycc_bt601 = 0x01,
	xv_ycc_bt709 = 0x02,
	s_ycc_601 = 0x04,
	adobe_ycc_601 = 0x08,
	adobe_rgb = 0x10,
	bt_2020_c_ycc = 0x20,
	bt_2020_ycc = 0x40,
	bt_2020_rgb = 0x80
};

enum hdr_tf_support_flag {
	traditional_gamma_sdr = 0x01,
	traditional_gamma_hdr = 0x02,
	smpte_st2084 = 0x04
};

struct mod_color {
	int dummy;
};

struct color_space_coordinates {
	unsigned int redX;
	unsigned int redY;
	unsigned int greenX;
	unsigned int greenY;
	unsigned int blueX;
	unsigned int blueY;
	unsigned int whiteX;
	unsigned int whiteY;
};

struct gamut_space_coordinates {
	unsigned int redX;
	unsigned int redY;
	unsigned int greenX;
	unsigned int greenY;
	unsigned int blueX;
	unsigned int blueY;
};

struct gamut_space_entry {
	unsigned int redX;
	unsigned int redY;
	unsigned int greenX;
	unsigned int greenY;
	unsigned int blueX;
	unsigned int blueY;

	int a0;
	int a1;
	int a2;
	int a3;
	int gamma;
};

struct white_point_coodinates {
	unsigned int whiteX;
	unsigned int whiteY;
};

struct white_point_coodinates_entry {
	unsigned int temperature;
	unsigned int whiteX;
	unsigned int whiteY;
};

struct color_range {
	int current;
	int min;
	int max;
};

struct color_gamut_data {
	enum color_color_space color_space;
	enum color_white_point_type white_point;
	struct color_space_coordinates gamut;
};

struct color_edid_caps {
	unsigned int colorimetry_caps;
	unsigned int hdr_caps;
};

struct mod_color *mod_color_create(struct dc *dc);

void mod_color_destroy(struct mod_color *mod_color);

bool mod_color_add_sink(struct mod_color *mod_color,
		const struct dc_sink *sink, struct color_edid_caps *edid_caps);

bool mod_color_remove_sink(struct mod_color *mod_color,
		const struct dc_sink *sink);

bool mod_color_update_gamut_to_stream(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams);

bool mod_color_set_white_point(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct white_point_coodinates *white_point);

bool mod_color_adjust_source_gamut(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct color_gamut_data *input_gamut_data);

bool mod_color_adjust_destination_gamut(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct color_gamut_data *input_gamut_data);

bool mod_color_adjust_source_gamut_and_tf(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct color_gamut_data *input_gamut_data,
		enum color_transfer_func input_transfer_func);

bool mod_color_get_user_enable(struct mod_color *mod_color,
		const struct dc_sink *sink,
		bool *user_enable);

bool mod_color_set_mastering_info(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		const struct dc_hdr_static_metadata *mastering_info);

bool mod_color_get_mastering_info(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct dc_hdr_static_metadata *mastering_info);

bool mod_color_set_user_enable(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		bool user_enable);

bool mod_color_get_custom_color_temperature(struct mod_color *mod_color,
		const struct dc_sink *sink,
		int *color_temperature);

bool mod_color_set_custom_color_temperature(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int color_temperature);

bool mod_color_get_color_saturation(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct color_range *color_saturation);

bool mod_color_get_color_contrast(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct color_range *color_contrast);

bool mod_color_get_color_brightness(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct color_range *color_brightness);

bool mod_color_get_color_hue(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct color_range *color_hue);

bool mod_color_get_source_gamut(struct mod_color *mod_color,
		const struct dc_sink *sink,
		struct color_space_coordinates *source_gamut);

bool mod_color_notify_mode_change(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams);

bool mod_color_set_brightness(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int brightness_value);

bool mod_color_set_contrast(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int contrast_value);

bool mod_color_set_hue(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int hue_value);

bool mod_color_set_saturation(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		int saturation_value);

bool mod_color_set_input_gamma_correction(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		struct dc_gamma *gamma);

bool mod_color_persist_user_preferred_quantization_range(
		struct mod_color *mod_color,
		const struct dc_sink *sink,
		enum dc_quantization_range quantization_range);

bool mod_color_get_preferred_quantization_range(struct mod_color *mod_color,
		const struct dc_sink *sink,
		const struct dc_crtc_timing *timing,
		enum dc_quantization_range *quantization_range);

bool mod_color_is_rgb_full_range_supported_for_timing(
		const struct dc_sink *sink,
		const struct dc_crtc_timing *timing);

bool mod_color_is_rgb_limited_range_supported_for_timing(
		const struct dc_sink *sink,
		const struct dc_crtc_timing *timing);

bool mod_color_set_regamma(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams);

bool mod_color_set_degamma(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams,
		enum color_transfer_func transfer_function);

bool mod_color_update_gamut_info(struct mod_color *mod_color,
		const struct dc_stream **streams, int num_streams);

#endif /* MOD_COLOR_H_ */
