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

#ifndef MOD_SHARED_H_
#define MOD_SHARED_H_

enum color_transfer_func {
	TRANSFER_FUNC_UNKNOWN,
	TRANSFER_FUNC_SRGB,
	TRANSFER_FUNC_BT709,
	TRANSFER_FUNC_PQ2084,
	TRANSFER_FUNC_PQ2084_INTERIM,
	TRANSFER_FUNC_LINEAR_0_1,
	TRANSFER_FUNC_LINEAR_0_125,
	TRANSFER_FUNC_GAMMA_22,
	TRANSFER_FUNC_GAMMA_26
};

enum vrr_packet_type {
	PACKET_TYPE_VRR,
	PACKET_TYPE_FS1,
	PACKET_TYPE_FS2,
	PACKET_TYPE_VTEM
};

union lut3d_control_flags {
	unsigned int raw;
	struct {
		unsigned int do_chroma_scale				:1;
		unsigned int spec_version				:3;
		unsigned int use_zero_display_black			:1;
		unsigned int use_zero_source_black			:1;
		unsigned int force_display_black			:6;
		unsigned int apply_display_gamma			:1;
		unsigned int exp_shaper_max				:6;
		unsigned int unity_3dlut				:1;
		unsigned int bypass_3dlut				:1;
		unsigned int use_3dlut					:1;
		unsigned int less_than_dcip3				:1;
		unsigned int override_lum				:1;
		unsigned int use_gamut_map_lib					:1;
		unsigned int chromatic_adaptation_src				:1;
		unsigned int chromatic_adaptation_dst				:1;
		unsigned int do_blender_lut_degamma		:1;
		unsigned int reseved					:4;
	} bits;
};

enum tm_show_option_internal {
	tm_show_option_internal_single_file		= 0,/*flags2 not in use*/
	tm_show_option_internal_duplicate_file,		/*use flags2*/
	tm_show_option_internal_duplicate_sidebyside/*use flags2*/
};

enum lut3d_control_gamut_map {
	lut3d_control_gamut_map_none = 0,
	lut3d_control_gamut_map_tonemap,
	lut3d_control_gamut_map_chto,
	lut3d_control_gamut_map_chso,
	lut3d_control_gamut_map_chci
};

enum lut3d_control_rotation_mode {
	lut3d_control_rotation_mode_none = 0,
	lut3d_control_rotation_mode_hue,
	lut3d_control_rotation_mode_cc,
	lut3d_control_rotation_mode_hue_cc
};

struct lut3d_settings {
	unsigned char version;
	union lut3d_control_flags flags;
	union lut3d_control_flags flags2;
	enum tm_show_option_internal option;
	unsigned int min_lum;/*multiplied by 100*/
	unsigned int max_lum;
	unsigned int min_lum2;
	unsigned int max_lum2;
	enum lut3d_control_gamut_map map;
	enum lut3d_control_rotation_mode rotation;
	enum lut3d_control_gamut_map map2;
	enum lut3d_control_rotation_mode rotation2;
};

#endif /* MOD_SHARED_H_ */
