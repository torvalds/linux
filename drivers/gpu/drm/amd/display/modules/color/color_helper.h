/**
 *  @file
 *  @date Copyright (c) 2008 Advanced Micro Devices, Inc. (unpublished)
 *
 *  @brief Helper functions for color gamut calculation
 *
 *  @internal
 *  All rights reserved.  This notice is intended as a precaution against
 *  inadvertent publication and does not imply publication or any waiver
 *  of confidentiality.  The year included in the foregoing notice is the
 *  year of creation of the work.
 */

#ifndef COLOR_MOD_COLOR_HELPER_H_
#define COLOR_MOD_COLOR_HELPER_H_

enum predefined_gamut_type {
	gamut_type_bt709,
	gamut_type_bt601,
	gamut_type_adobe_rgb,
	gamut_type_srgb,
	gamut_type_bt2020,
	gamut_type_unknown,
};

enum predefined_white_point_type {
	white_point_type_5000k_horizon,
	white_point_type_6500k_noon,
	white_point_type_7500k_north_sky,
	white_point_type_9300k,
	white_point_type_unknown,
};

bool mod_color_find_predefined_gamut(
		struct gamut_space_coordinates *out_gamut,
		enum predefined_gamut_type type);

bool mod_color_find_predefined_white_point(
		struct white_point_coodinates *out_white_point,
		unsigned int index);

bool mod_color_find_white_point_from_temperature(
		struct white_point_coodinates *out_white_point,
		unsigned int temperature);

bool mod_color_find_temperature_from_white_point(
		struct white_point_coodinates *in_white_point,
		unsigned int *out_temperature);

#endif /* COLOR_MOD_COLOR_HELPER_H_ */
