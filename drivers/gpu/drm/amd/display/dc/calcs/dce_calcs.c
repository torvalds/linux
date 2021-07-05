/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include <linux/slab.h>

#include "resource.h"
#include "dm_services.h"
#include "dce_calcs.h"
#include "dc.h"
#include "core_types.h"
#include "dal_asic_id.h"
#include "calcs_logger.h"

/*
 * NOTE:
 *   This file is gcc-parseable HW gospel, coming straight from HW engineers.
 *
 * It doesn't adhere to Linux kernel style and sometimes will do things in odd
 * ways. Unless there is something clearly wrong with it the code should
 * remain as-is as it provides us with a guarantee from HW that it is correct.
 */

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

static enum bw_calcs_version bw_calcs_version_from_asic_id(struct hw_asic_id asic_id)
{
	switch (asic_id.chip_family) {

	case FAMILY_CZ:
		if (ASIC_REV_IS_STONEY(asic_id.hw_internal_rev))
			return BW_CALCS_VERSION_STONEY;
		return BW_CALCS_VERSION_CARRIZO;

	case FAMILY_VI:
		if (ASIC_REV_IS_POLARIS12_V(asic_id.hw_internal_rev))
			return BW_CALCS_VERSION_POLARIS12;
		if (ASIC_REV_IS_POLARIS10_P(asic_id.hw_internal_rev))
			return BW_CALCS_VERSION_POLARIS10;
		if (ASIC_REV_IS_POLARIS11_M(asic_id.hw_internal_rev))
			return BW_CALCS_VERSION_POLARIS11;
		if (ASIC_REV_IS_VEGAM(asic_id.hw_internal_rev))
			return BW_CALCS_VERSION_VEGAM;
		return BW_CALCS_VERSION_INVALID;

	case FAMILY_AI:
		return BW_CALCS_VERSION_VEGA10;

	default:
		return BW_CALCS_VERSION_INVALID;
	}
}

static void calculate_bandwidth(
	const struct bw_calcs_dceip *dceip,
	const struct bw_calcs_vbios *vbios,
	struct bw_calcs_data *data)

{
	const int32_t pixels_per_chunk = 512;
	const int32_t high = 2;
	const int32_t mid = 1;
	const int32_t low = 0;
	const uint32_t s_low = 0;
	const uint32_t s_mid1 = 1;
	const uint32_t s_mid2 = 2;
	const uint32_t s_mid3 = 3;
	const uint32_t s_mid4 = 4;
	const uint32_t s_mid5 = 5;
	const uint32_t s_mid6 = 6;
	const uint32_t s_high = 7;
	const uint32_t dmif_chunk_buff_margin = 1;

	uint32_t max_chunks_fbc_mode;
	int32_t num_cursor_lines;

	int32_t i, j, k;
	struct bw_fixed *yclk;
	struct bw_fixed *sclk;
	bool d0_underlay_enable;
	bool d1_underlay_enable;
	bool fbc_enabled;
	bool lpt_enabled;
	enum bw_defines sclk_message;
	enum bw_defines yclk_message;
	enum bw_defines *tiling_mode;
	enum bw_defines *surface_type;
	enum bw_defines voltage;
	enum bw_defines pipe_check;
	enum bw_defines hsr_check;
	enum bw_defines vsr_check;
	enum bw_defines lb_size_check;
	enum bw_defines fbc_check;
	enum bw_defines rotation_check;
	enum bw_defines mode_check;
	enum bw_defines nbp_state_change_enable_blank;
	/*initialize variables*/
	int32_t number_of_displays_enabled = 0;
	int32_t number_of_displays_enabled_with_margin = 0;
	int32_t number_of_aligned_displays_with_no_margin = 0;

	yclk = kcalloc(3, sizeof(*yclk), GFP_KERNEL);
	if (!yclk)
		return;

	sclk = kcalloc(8, sizeof(*sclk), GFP_KERNEL);
	if (!sclk)
		goto free_yclk;

	tiling_mode = kcalloc(maximum_number_of_surfaces, sizeof(*tiling_mode), GFP_KERNEL);
	if (!tiling_mode)
		goto free_sclk;

	surface_type = kcalloc(maximum_number_of_surfaces, sizeof(*surface_type), GFP_KERNEL);
	if (!surface_type)
		goto free_tiling_mode;

	yclk[low] = vbios->low_yclk;
	yclk[mid] = vbios->mid_yclk;
	yclk[high] = vbios->high_yclk;
	sclk[s_low] = vbios->low_sclk;
	sclk[s_mid1] = vbios->mid1_sclk;
	sclk[s_mid2] = vbios->mid2_sclk;
	sclk[s_mid3] = vbios->mid3_sclk;
	sclk[s_mid4] = vbios->mid4_sclk;
	sclk[s_mid5] = vbios->mid5_sclk;
	sclk[s_mid6] = vbios->mid6_sclk;
	sclk[s_high] = vbios->high_sclk;
	/*''''''''''''''''''*/
	/* surface assignment:*/
	/* 0: d0 underlay or underlay luma*/
	/* 1: d0 underlay chroma*/
	/* 2: d1 underlay or underlay luma*/
	/* 3: d1 underlay chroma*/
	/* 4: d0 graphics*/
	/* 5: d1 graphics*/
	/* 6: d2 graphics*/
	/* 7: d3 graphics, same mode as d2*/
	/* 8: d4 graphics, same mode as d2*/
	/* 9: d5 graphics, same mode as d2*/
	/* ...*/
	/* maximum_number_of_surfaces-2: d1 display_write_back420 luma*/
	/* maximum_number_of_surfaces-1: d1 display_write_back420 chroma*/
	/* underlay luma and chroma surface parameters from spreadsheet*/




	if (data->d0_underlay_mode == bw_def_none)
		d0_underlay_enable = false;
	else
		d0_underlay_enable = true;
	if (data->d1_underlay_mode == bw_def_none)
		d1_underlay_enable = false;
	else
		d1_underlay_enable = true;
	data->number_of_underlay_surfaces = d0_underlay_enable + d1_underlay_enable;
	switch (data->underlay_surface_type) {
	case bw_def_420:
		surface_type[0] = bw_def_underlay420_luma;
		surface_type[2] = bw_def_underlay420_luma;
		data->bytes_per_pixel[0] = 1;
		data->bytes_per_pixel[2] = 1;
		surface_type[1] = bw_def_underlay420_chroma;
		surface_type[3] = bw_def_underlay420_chroma;
		data->bytes_per_pixel[1] = 2;
		data->bytes_per_pixel[3] = 2;
		data->lb_size_per_component[0] = dceip->underlay420_luma_lb_size_per_component;
		data->lb_size_per_component[1] = dceip->underlay420_chroma_lb_size_per_component;
		data->lb_size_per_component[2] = dceip->underlay420_luma_lb_size_per_component;
		data->lb_size_per_component[3] = dceip->underlay420_chroma_lb_size_per_component;
		break;
	case bw_def_422:
		surface_type[0] = bw_def_underlay422;
		surface_type[2] = bw_def_underlay422;
		data->bytes_per_pixel[0] = 2;
		data->bytes_per_pixel[2] = 2;
		data->lb_size_per_component[0] = dceip->underlay422_lb_size_per_component;
		data->lb_size_per_component[2] = dceip->underlay422_lb_size_per_component;
		break;
	default:
		surface_type[0] = bw_def_underlay444;
		surface_type[2] = bw_def_underlay444;
		data->bytes_per_pixel[0] = 4;
		data->bytes_per_pixel[2] = 4;
		data->lb_size_per_component[0] = dceip->lb_size_per_component444;
		data->lb_size_per_component[2] = dceip->lb_size_per_component444;
		break;
	}
	if (d0_underlay_enable) {
		switch (data->underlay_surface_type) {
		case bw_def_420:
			data->enable[0] = 1;
			data->enable[1] = 1;
			break;
		default:
			data->enable[0] = 1;
			data->enable[1] = 0;
			break;
		}
	}
	else {
		data->enable[0] = 0;
		data->enable[1] = 0;
	}
	if (d1_underlay_enable) {
		switch (data->underlay_surface_type) {
		case bw_def_420:
			data->enable[2] = 1;
			data->enable[3] = 1;
			break;
		default:
			data->enable[2] = 1;
			data->enable[3] = 0;
			break;
		}
	}
	else {
		data->enable[2] = 0;
		data->enable[3] = 0;
	}
	data->use_alpha[0] = 0;
	data->use_alpha[1] = 0;
	data->use_alpha[2] = 0;
	data->use_alpha[3] = 0;
	data->scatter_gather_enable_for_pipe[0] = vbios->scatter_gather_enable;
	data->scatter_gather_enable_for_pipe[1] = vbios->scatter_gather_enable;
	data->scatter_gather_enable_for_pipe[2] = vbios->scatter_gather_enable;
	data->scatter_gather_enable_for_pipe[3] = vbios->scatter_gather_enable;
	/*underlay0 same and graphics display pipe0*/
	data->interlace_mode[0] = data->interlace_mode[4];
	data->interlace_mode[1] = data->interlace_mode[4];
	/*underlay1 same and graphics display pipe1*/
	data->interlace_mode[2] = data->interlace_mode[5];
	data->interlace_mode[3] = data->interlace_mode[5];
	/*underlay0 same and graphics display pipe0*/
	data->h_total[0] = data->h_total[4];
	data->v_total[0] = data->v_total[4];
	data->h_total[1] = data->h_total[4];
	data->v_total[1] = data->v_total[4];
	/*underlay1 same and graphics display pipe1*/
	data->h_total[2] = data->h_total[5];
	data->v_total[2] = data->v_total[5];
	data->h_total[3] = data->h_total[5];
	data->v_total[3] = data->v_total[5];
	/*underlay0 same and graphics display pipe0*/
	data->pixel_rate[0] = data->pixel_rate[4];
	data->pixel_rate[1] = data->pixel_rate[4];
	/*underlay1 same and graphics display pipe1*/
	data->pixel_rate[2] = data->pixel_rate[5];
	data->pixel_rate[3] = data->pixel_rate[5];
	if ((data->underlay_tiling_mode == bw_def_array_linear_general || data->underlay_tiling_mode == bw_def_array_linear_aligned)) {
		tiling_mode[0] = bw_def_linear;
		tiling_mode[1] = bw_def_linear;
		tiling_mode[2] = bw_def_linear;
		tiling_mode[3] = bw_def_linear;
	}
	else {
		tiling_mode[0] = bw_def_landscape;
		tiling_mode[1] = bw_def_landscape;
		tiling_mode[2] = bw_def_landscape;
		tiling_mode[3] = bw_def_landscape;
	}
	data->lb_bpc[0] = data->underlay_lb_bpc;
	data->lb_bpc[1] = data->underlay_lb_bpc;
	data->lb_bpc[2] = data->underlay_lb_bpc;
	data->lb_bpc[3] = data->underlay_lb_bpc;
	data->compression_rate[0] = bw_int_to_fixed(1);
	data->compression_rate[1] = bw_int_to_fixed(1);
	data->compression_rate[2] = bw_int_to_fixed(1);
	data->compression_rate[3] = bw_int_to_fixed(1);
	data->access_one_channel_only[0] = 0;
	data->access_one_channel_only[1] = 0;
	data->access_one_channel_only[2] = 0;
	data->access_one_channel_only[3] = 0;
	data->cursor_width_pixels[0] = bw_int_to_fixed(0);
	data->cursor_width_pixels[1] = bw_int_to_fixed(0);
	data->cursor_width_pixels[2] = bw_int_to_fixed(0);
	data->cursor_width_pixels[3] = bw_int_to_fixed(0);
	/* graphics surface parameters from spreadsheet*/
	fbc_enabled = false;
	lpt_enabled = false;
	for (i = 4; i <= maximum_number_of_surfaces - 3; i++) {
		if (i < data->number_of_displays + 4) {
			if (i == 4 && data->d0_underlay_mode == bw_def_underlay_only) {
				data->enable[i] = 0;
				data->use_alpha[i] = 0;
			}
			else if (i == 4 && data->d0_underlay_mode == bw_def_blend) {
				data->enable[i] = 1;
				data->use_alpha[i] = 1;
			}
			else if (i == 4) {
				data->enable[i] = 1;
				data->use_alpha[i] = 0;
			}
			else if (i == 5 && data->d1_underlay_mode == bw_def_underlay_only) {
				data->enable[i] = 0;
				data->use_alpha[i] = 0;
			}
			else if (i == 5 && data->d1_underlay_mode == bw_def_blend) {
				data->enable[i] = 1;
				data->use_alpha[i] = 1;
			}
			else {
				data->enable[i] = 1;
				data->use_alpha[i] = 0;
			}
		}
		else {
			data->enable[i] = 0;
			data->use_alpha[i] = 0;
		}
		data->scatter_gather_enable_for_pipe[i] = vbios->scatter_gather_enable;
		surface_type[i] = bw_def_graphics;
		data->lb_size_per_component[i] = dceip->lb_size_per_component444;
		if (data->graphics_tiling_mode == bw_def_array_linear_general || data->graphics_tiling_mode == bw_def_array_linear_aligned) {
			tiling_mode[i] = bw_def_linear;
		}
		else {
			tiling_mode[i] = bw_def_tiled;
		}
		data->lb_bpc[i] = data->graphics_lb_bpc;
		if ((data->fbc_en[i] == 1 && (dceip->argb_compression_support || data->d0_underlay_mode != bw_def_blended))) {
			data->compression_rate[i] = bw_int_to_fixed(vbios->average_compression_rate);
			data->access_one_channel_only[i] = data->lpt_en[i];
		}
		else {
			data->compression_rate[i] = bw_int_to_fixed(1);
			data->access_one_channel_only[i] = 0;
		}
		if (data->fbc_en[i] == 1) {
			fbc_enabled = true;
			if (data->lpt_en[i] == 1) {
				lpt_enabled = true;
			}
		}
		data->cursor_width_pixels[i] = bw_int_to_fixed(vbios->cursor_width);
	}
	/* display_write_back420*/
	data->scatter_gather_enable_for_pipe[maximum_number_of_surfaces - 2] = 0;
	data->scatter_gather_enable_for_pipe[maximum_number_of_surfaces - 1] = 0;
	if (data->d1_display_write_back_dwb_enable == 1) {
		data->enable[maximum_number_of_surfaces - 2] = 1;
		data->enable[maximum_number_of_surfaces - 1] = 1;
	}
	else {
		data->enable[maximum_number_of_surfaces - 2] = 0;
		data->enable[maximum_number_of_surfaces - 1] = 0;
	}
	surface_type[maximum_number_of_surfaces - 2] = bw_def_display_write_back420_luma;
	surface_type[maximum_number_of_surfaces - 1] = bw_def_display_write_back420_chroma;
	data->lb_size_per_component[maximum_number_of_surfaces - 2] = dceip->underlay420_luma_lb_size_per_component;
	data->lb_size_per_component[maximum_number_of_surfaces - 1] = dceip->underlay420_chroma_lb_size_per_component;
	data->bytes_per_pixel[maximum_number_of_surfaces - 2] = 1;
	data->bytes_per_pixel[maximum_number_of_surfaces - 1] = 2;
	data->interlace_mode[maximum_number_of_surfaces - 2] = data->interlace_mode[5];
	data->interlace_mode[maximum_number_of_surfaces - 1] = data->interlace_mode[5];
	data->h_taps[maximum_number_of_surfaces - 2] = bw_int_to_fixed(1);
	data->h_taps[maximum_number_of_surfaces - 1] = bw_int_to_fixed(1);
	data->v_taps[maximum_number_of_surfaces - 2] = bw_int_to_fixed(1);
	data->v_taps[maximum_number_of_surfaces - 1] = bw_int_to_fixed(1);
	data->rotation_angle[maximum_number_of_surfaces - 2] = bw_int_to_fixed(0);
	data->rotation_angle[maximum_number_of_surfaces - 1] = bw_int_to_fixed(0);
	tiling_mode[maximum_number_of_surfaces - 2] = bw_def_linear;
	tiling_mode[maximum_number_of_surfaces - 1] = bw_def_linear;
	data->lb_bpc[maximum_number_of_surfaces - 2] = 8;
	data->lb_bpc[maximum_number_of_surfaces - 1] = 8;
	data->compression_rate[maximum_number_of_surfaces - 2] = bw_int_to_fixed(1);
	data->compression_rate[maximum_number_of_surfaces - 1] = bw_int_to_fixed(1);
	data->access_one_channel_only[maximum_number_of_surfaces - 2] = 0;
	data->access_one_channel_only[maximum_number_of_surfaces - 1] = 0;
	/*assume display pipe1 has dwb enabled*/
	data->h_total[maximum_number_of_surfaces - 2] = data->h_total[5];
	data->h_total[maximum_number_of_surfaces - 1] = data->h_total[5];
	data->v_total[maximum_number_of_surfaces - 2] = data->v_total[5];
	data->v_total[maximum_number_of_surfaces - 1] = data->v_total[5];
	data->pixel_rate[maximum_number_of_surfaces - 2] = data->pixel_rate[5];
	data->pixel_rate[maximum_number_of_surfaces - 1] = data->pixel_rate[5];
	data->src_width[maximum_number_of_surfaces - 2] = data->src_width[5];
	data->src_width[maximum_number_of_surfaces - 1] = data->src_width[5];
	data->src_height[maximum_number_of_surfaces - 2] = data->src_height[5];
	data->src_height[maximum_number_of_surfaces - 1] = data->src_height[5];
	data->pitch_in_pixels[maximum_number_of_surfaces - 2] = data->src_width[5];
	data->pitch_in_pixels[maximum_number_of_surfaces - 1] = data->src_width[5];
	data->h_scale_ratio[maximum_number_of_surfaces - 2] = bw_int_to_fixed(1);
	data->h_scale_ratio[maximum_number_of_surfaces - 1] = bw_int_to_fixed(1);
	data->v_scale_ratio[maximum_number_of_surfaces - 2] = bw_int_to_fixed(1);
	data->v_scale_ratio[maximum_number_of_surfaces - 1] = bw_int_to_fixed(1);
	data->stereo_mode[maximum_number_of_surfaces - 2] = bw_def_mono;
	data->stereo_mode[maximum_number_of_surfaces - 1] = bw_def_mono;
	data->cursor_width_pixels[maximum_number_of_surfaces - 2] = bw_int_to_fixed(0);
	data->cursor_width_pixels[maximum_number_of_surfaces - 1] = bw_int_to_fixed(0);
	data->use_alpha[maximum_number_of_surfaces - 2] = 0;
	data->use_alpha[maximum_number_of_surfaces - 1] = 0;
	/*mode check calculations:*/
	/* mode within dce ip capabilities*/
	/* fbc*/
	/* hsr*/
	/* vsr*/
	/* lb size*/
	/*effective scaling source and ratios:*/
	/*for graphics, non-stereo, non-interlace surfaces when the size of the source and destination are the same, only one tap is used*/
	/*420 chroma has half the width, height, horizontal and vertical scaling ratios than luma*/
	/*rotating a graphic or underlay surface swaps the width, height, horizontal and vertical scaling ratios*/
	/*in top-bottom stereo mode there is 2:1 vertical downscaling for each eye*/
	/*in side-by-side stereo mode there is 2:1 horizontal downscaling for each eye*/
	/*in interlace mode there is 2:1 vertical downscaling for each field*/
	/*in panning or bezel adjustment mode the source width has an extra 128 pixels*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (bw_equ(data->h_scale_ratio[i], bw_int_to_fixed(1)) && bw_equ(data->v_scale_ratio[i], bw_int_to_fixed(1)) && surface_type[i] == bw_def_graphics && data->stereo_mode[i] == bw_def_mono && data->interlace_mode[i] == 0) {
				data->h_taps[i] = bw_int_to_fixed(1);
				data->v_taps[i] = bw_int_to_fixed(1);
			}
			if (surface_type[i] == bw_def_display_write_back420_chroma || surface_type[i] == bw_def_underlay420_chroma) {
				data->pitch_in_pixels_after_surface_type[i] = bw_div(data->pitch_in_pixels[i], bw_int_to_fixed(2));
				data->src_width_after_surface_type = bw_div(data->src_width[i], bw_int_to_fixed(2));
				data->src_height_after_surface_type = bw_div(data->src_height[i], bw_int_to_fixed(2));
				data->hsr_after_surface_type = bw_div(data->h_scale_ratio[i], bw_int_to_fixed(2));
				data->vsr_after_surface_type = bw_div(data->v_scale_ratio[i], bw_int_to_fixed(2));
			}
			else {
				data->pitch_in_pixels_after_surface_type[i] = data->pitch_in_pixels[i];
				data->src_width_after_surface_type = data->src_width[i];
				data->src_height_after_surface_type = data->src_height[i];
				data->hsr_after_surface_type = data->h_scale_ratio[i];
				data->vsr_after_surface_type = data->v_scale_ratio[i];
			}
			if ((bw_equ(data->rotation_angle[i], bw_int_to_fixed(90)) || bw_equ(data->rotation_angle[i], bw_int_to_fixed(270))) && surface_type[i] != bw_def_display_write_back420_luma && surface_type[i] != bw_def_display_write_back420_chroma) {
				data->src_width_after_rotation = data->src_height_after_surface_type;
				data->src_height_after_rotation = data->src_width_after_surface_type;
				data->hsr_after_rotation = data->vsr_after_surface_type;
				data->vsr_after_rotation = data->hsr_after_surface_type;
			}
			else {
				data->src_width_after_rotation = data->src_width_after_surface_type;
				data->src_height_after_rotation = data->src_height_after_surface_type;
				data->hsr_after_rotation = data->hsr_after_surface_type;
				data->vsr_after_rotation = data->vsr_after_surface_type;
			}
			switch (data->stereo_mode[i]) {
			case bw_def_top_bottom:
				data->source_width_pixels[i] = data->src_width_after_rotation;
				data->source_height_pixels = bw_mul(bw_int_to_fixed(2), data->src_height_after_rotation);
				data->hsr_after_stereo = data->hsr_after_rotation;
				data->vsr_after_stereo = bw_mul(bw_int_to_fixed(1), data->vsr_after_rotation);
				break;
			case bw_def_side_by_side:
				data->source_width_pixels[i] = bw_mul(bw_int_to_fixed(2), data->src_width_after_rotation);
				data->source_height_pixels = data->src_height_after_rotation;
				data->hsr_after_stereo = bw_mul(bw_int_to_fixed(1), data->hsr_after_rotation);
				data->vsr_after_stereo = data->vsr_after_rotation;
				break;
			default:
				data->source_width_pixels[i] = data->src_width_after_rotation;
				data->source_height_pixels = data->src_height_after_rotation;
				data->hsr_after_stereo = data->hsr_after_rotation;
				data->vsr_after_stereo = data->vsr_after_rotation;
				break;
			}
			data->hsr[i] = data->hsr_after_stereo;
			if (data->interlace_mode[i]) {
				data->vsr[i] = bw_mul(data->vsr_after_stereo, bw_int_to_fixed(2));
			}
			else {
				data->vsr[i] = data->vsr_after_stereo;
			}
			if (data->panning_and_bezel_adjustment != bw_def_none) {
				data->source_width_rounded_up_to_chunks[i] = bw_add(bw_floor2(bw_sub(data->source_width_pixels[i], bw_int_to_fixed(1)), bw_int_to_fixed(128)), bw_int_to_fixed(256));
			}
			else {
				data->source_width_rounded_up_to_chunks[i] = bw_ceil2(data->source_width_pixels[i], bw_int_to_fixed(128));
			}
			data->source_height_rounded_up_to_chunks[i] = data->source_height_pixels;
		}
	}
	/*mode support checks:*/
	/*the number of graphics and underlay pipes is limited by the ip support*/
	/*maximum horizontal and vertical scale ratio is 4, and should not exceed the number of taps*/
	/*for downscaling with the pre-downscaler, the horizontal scale ratio must be more than the ceiling of one quarter of the number of taps*/
	/*the pre-downscaler reduces the line buffer source by the horizontal scale ratio*/
	/*the number of lines in the line buffer has to exceed the number of vertical taps*/
	/*the size of the line in the line buffer is the product of the source width and the bits per component, rounded up to a multiple of 48*/
	/*the size of the line in the line buffer in the case of 10 bit per component is the product of the source width rounded up to multiple of 8 and 30.023438 / 3, rounded up to a multiple of 48*/
	/*the size of the line in the line buffer in the case of 8 bit per component is the product of the source width rounded up to multiple of 8 and 30.023438 / 3, rounded up to a multiple of 48*/
	/*frame buffer compression is not supported with stereo mode, rotation, or non- 888 formats*/
	/*rotation is not supported with linear of stereo modes*/
	if (dceip->number_of_graphics_pipes >= data->number_of_displays && dceip->number_of_underlay_pipes >= data->number_of_underlay_surfaces && !(dceip->display_write_back_supported == 0 && data->d1_display_write_back_dwb_enable == 1)) {
		pipe_check = bw_def_ok;
	}
	else {
		pipe_check = bw_def_notok;
	}
	hsr_check = bw_def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (bw_neq(data->hsr[i], bw_int_to_fixed(1))) {
				if (bw_mtn(data->hsr[i], bw_int_to_fixed(4))) {
					hsr_check = bw_def_hsr_mtn_4;
				}
				else {
					if (bw_mtn(data->hsr[i], data->h_taps[i])) {
						hsr_check = bw_def_hsr_mtn_h_taps;
					}
					else {
						if (dceip->pre_downscaler_enabled == 1 && bw_mtn(data->hsr[i], bw_int_to_fixed(1)) && bw_leq(data->hsr[i], bw_ceil2(bw_div(data->h_taps[i], bw_int_to_fixed(4)), bw_int_to_fixed(1)))) {
							hsr_check = bw_def_ceiling__h_taps_div_4___meq_hsr;
						}
					}
				}
			}
		}
	}
	vsr_check = bw_def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (bw_neq(data->vsr[i], bw_int_to_fixed(1))) {
				if (bw_mtn(data->vsr[i], bw_int_to_fixed(4))) {
					vsr_check = bw_def_vsr_mtn_4;
				}
				else {
					if (bw_mtn(data->vsr[i], data->v_taps[i])) {
						vsr_check = bw_def_vsr_mtn_v_taps;
					}
				}
			}
		}
	}
	lb_size_check = bw_def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if ((dceip->pre_downscaler_enabled && bw_mtn(data->hsr[i], bw_int_to_fixed(1)))) {
				data->source_width_in_lb = bw_div(data->source_width_pixels[i], data->hsr[i]);
			}
			else {
				data->source_width_in_lb = data->source_width_pixels[i];
			}
			switch (data->lb_bpc[i]) {
			case 8:
				data->lb_line_pitch = bw_ceil2(bw_mul(bw_div(bw_frc_to_fixed(2401171875ul, 100000000), bw_int_to_fixed(3)), bw_ceil2(data->source_width_in_lb, bw_int_to_fixed(8))), bw_int_to_fixed(48));
				break;
			case 10:
				data->lb_line_pitch = bw_ceil2(bw_mul(bw_div(bw_frc_to_fixed(300234375, 10000000), bw_int_to_fixed(3)), bw_ceil2(data->source_width_in_lb, bw_int_to_fixed(8))), bw_int_to_fixed(48));
				break;
			default:
				data->lb_line_pitch = bw_ceil2(bw_mul(bw_int_to_fixed(data->lb_bpc[i]), data->source_width_in_lb), bw_int_to_fixed(48));
				break;
			}
			data->lb_partitions[i] = bw_floor2(bw_div(data->lb_size_per_component[i], data->lb_line_pitch), bw_int_to_fixed(1));
			/*clamp the partitions to the maxium number supported by the lb*/
			if ((surface_type[i] != bw_def_graphics || dceip->graphics_lb_nodownscaling_multi_line_prefetching == 1)) {
				data->lb_partitions_max[i] = bw_int_to_fixed(10);
			}
			else {
				data->lb_partitions_max[i] = bw_int_to_fixed(7);
			}
			data->lb_partitions[i] = bw_min2(data->lb_partitions_max[i], data->lb_partitions[i]);
			if (bw_mtn(bw_add(data->v_taps[i], bw_int_to_fixed(1)), data->lb_partitions[i])) {
				lb_size_check = bw_def_notok;
			}
		}
	}
	fbc_check = bw_def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i] && data->fbc_en[i] == 1 && (bw_equ(data->rotation_angle[i], bw_int_to_fixed(90)) || bw_equ(data->rotation_angle[i], bw_int_to_fixed(270)) || data->stereo_mode[i] != bw_def_mono || data->bytes_per_pixel[i] != 4)) {
			fbc_check = bw_def_invalid_rotation_or_bpp_or_stereo;
		}
	}
	rotation_check = bw_def_ok;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if ((bw_equ(data->rotation_angle[i], bw_int_to_fixed(90)) || bw_equ(data->rotation_angle[i], bw_int_to_fixed(270))) && (tiling_mode[i] == bw_def_linear || data->stereo_mode[i] != bw_def_mono)) {
				rotation_check = bw_def_invalid_linear_or_stereo_mode;
			}
		}
	}
	if (pipe_check == bw_def_ok && hsr_check == bw_def_ok && vsr_check == bw_def_ok && lb_size_check == bw_def_ok && fbc_check == bw_def_ok && rotation_check == bw_def_ok) {
		mode_check = bw_def_ok;
	}
	else {
		mode_check = bw_def_notok;
	}
	/*number of memory channels for write-back client*/
	data->number_of_dram_wrchannels = vbios->number_of_dram_channels;
	data->number_of_dram_channels = vbios->number_of_dram_channels;
	/*modify number of memory channels if lpt mode is enabled*/
	/* low power tiling mode register*/
	/* 0 = use channel 0*/
	/* 1 = use channel 0 and 1*/
	/* 2 = use channel 0,1,2,3*/
	if ((fbc_enabled == 1 && lpt_enabled == 1)) {
		if (vbios->memory_type == bw_def_hbm)
			data->dram_efficiency = bw_frc_to_fixed(5, 10);
		else
			data->dram_efficiency = bw_int_to_fixed(1);


		if (dceip->low_power_tiling_mode == 0) {
			data->number_of_dram_channels = 1;
		}
		else if (dceip->low_power_tiling_mode == 1) {
			data->number_of_dram_channels = 2;
		}
		else if (dceip->low_power_tiling_mode == 2) {
			data->number_of_dram_channels = 4;
		}
		else {
			data->number_of_dram_channels = 1;
		}
	}
	else {
		if (vbios->memory_type == bw_def_hbm)
			data->dram_efficiency = bw_frc_to_fixed(5, 10);
		else
			data->dram_efficiency = bw_frc_to_fixed(8, 10);
	}
	/*memory request size and latency hiding:*/
	/*request size is normally 64 byte, 2-line interleaved, with full latency hiding*/
	/*the display write-back requests are single line*/
	/*for tiled graphics surfaces, or undelay surfaces with width higher than the maximum size for full efficiency, request size is 32 byte in 8 and 16 bpp or if the rotation is orthogonal to the tiling grain. only half is useful of the bytes in the request size in 8 bpp or in 32 bpp if the rotation is orthogonal to the tiling grain.*/
	/*for undelay surfaces with width lower than the maximum size for full efficiency, requests are 4-line interleaved in 16bpp if the rotation is parallel to the tiling grain, and 8-line interleaved with 4-line latency hiding in 8bpp or if the rotation is orthogonal to the tiling grain.*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if ((bw_equ(data->rotation_angle[i], bw_int_to_fixed(90)) || bw_equ(data->rotation_angle[i], bw_int_to_fixed(270)))) {
				if ((i < 4)) {
					/*underlay portrait tiling mode is not supported*/
					data->orthogonal_rotation[i] = 1;
				}
				else {
					/*graphics portrait tiling mode*/
					if (data->graphics_micro_tile_mode == bw_def_rotated_micro_tiling) {
						data->orthogonal_rotation[i] = 0;
					}
					else {
						data->orthogonal_rotation[i] = 1;
					}
				}
			}
			else {
				if ((i < 4)) {
					/*underlay landscape tiling mode is only supported*/
					if (data->underlay_micro_tile_mode == bw_def_display_micro_tiling) {
						data->orthogonal_rotation[i] = 0;
					}
					else {
						data->orthogonal_rotation[i] = 1;
					}
				}
				else {
					/*graphics landscape tiling mode*/
					if (data->graphics_micro_tile_mode == bw_def_display_micro_tiling) {
						data->orthogonal_rotation[i] = 0;
					}
					else {
						data->orthogonal_rotation[i] = 1;
					}
				}
			}
			if (bw_equ(data->rotation_angle[i], bw_int_to_fixed(90)) || bw_equ(data->rotation_angle[i], bw_int_to_fixed(270))) {
				data->underlay_maximum_source_efficient_for_tiling = dceip->underlay_maximum_height_efficient_for_tiling;
			}
			else {
				data->underlay_maximum_source_efficient_for_tiling = dceip->underlay_maximum_width_efficient_for_tiling;
			}
			if (surface_type[i] == bw_def_display_write_back420_luma || surface_type[i] == bw_def_display_write_back420_chroma) {
				data->bytes_per_request[i] = bw_int_to_fixed(64);
				data->useful_bytes_per_request[i] = bw_int_to_fixed(64);
				data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(1);
				data->latency_hiding_lines[i] = bw_int_to_fixed(1);
			}
			else if (tiling_mode[i] == bw_def_linear) {
				data->bytes_per_request[i] = bw_int_to_fixed(64);
				data->useful_bytes_per_request[i] = bw_int_to_fixed(64);
				data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(2);
				data->latency_hiding_lines[i] = bw_int_to_fixed(2);
			}
			else {
				if (surface_type[i] == bw_def_graphics || (bw_mtn(data->source_width_rounded_up_to_chunks[i], bw_ceil2(data->underlay_maximum_source_efficient_for_tiling, bw_int_to_fixed(256))))) {
					switch (data->bytes_per_pixel[i]) {
					case 8:
						data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(2);
						data->latency_hiding_lines[i] = bw_int_to_fixed(2);
						if (data->orthogonal_rotation[i]) {
							data->bytes_per_request[i] = bw_int_to_fixed(32);
							data->useful_bytes_per_request[i] = bw_int_to_fixed(32);
						}
						else {
							data->bytes_per_request[i] = bw_int_to_fixed(64);
							data->useful_bytes_per_request[i] = bw_int_to_fixed(64);
						}
						break;
					case 4:
						if (data->orthogonal_rotation[i]) {
							data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(2);
							data->latency_hiding_lines[i] = bw_int_to_fixed(2);
							data->bytes_per_request[i] = bw_int_to_fixed(32);
							data->useful_bytes_per_request[i] = bw_int_to_fixed(16);
						}
						else {
							data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(2);
							data->latency_hiding_lines[i] = bw_int_to_fixed(2);
							data->bytes_per_request[i] = bw_int_to_fixed(64);
							data->useful_bytes_per_request[i] = bw_int_to_fixed(64);
						}
						break;
					case 2:
						data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(2);
						data->latency_hiding_lines[i] = bw_int_to_fixed(2);
						data->bytes_per_request[i] = bw_int_to_fixed(32);
						data->useful_bytes_per_request[i] = bw_int_to_fixed(32);
						break;
					default:
						data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(2);
						data->latency_hiding_lines[i] = bw_int_to_fixed(2);
						data->bytes_per_request[i] = bw_int_to_fixed(32);
						data->useful_bytes_per_request[i] = bw_int_to_fixed(16);
						break;
					}
				}
				else {
					data->bytes_per_request[i] = bw_int_to_fixed(64);
					data->useful_bytes_per_request[i] = bw_int_to_fixed(64);
					if (data->orthogonal_rotation[i]) {
						data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(8);
						data->latency_hiding_lines[i] = bw_int_to_fixed(4);
					}
					else {
						switch (data->bytes_per_pixel[i]) {
						case 4:
							data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(2);
							data->latency_hiding_lines[i] = bw_int_to_fixed(2);
							break;
						case 2:
							data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(4);
							data->latency_hiding_lines[i] = bw_int_to_fixed(4);
							break;
						default:
							data->lines_interleaved_in_mem_access[i] = bw_int_to_fixed(8);
							data->latency_hiding_lines[i] = bw_int_to_fixed(4);
							break;
						}
					}
				}
			}
		}
	}
	/*requested peak bandwidth:*/
	/*the peak request-per-second bandwidth is the product of the maximum source lines in per line out in the beginning*/
	/*and in the middle of the frame, the ratio of the source width to the line time, the ratio of line interleaving*/
	/*in memory to lines of latency hiding, and the ratio of bytes per pixel to useful bytes per request.*/
	/**/
	/*if the dmif data buffer size holds more than vta_ps worth of source lines, then only vsr is used.*/
	/*the peak bandwidth is the peak request-per-second bandwidth times the request size.*/
	/**/
	/*the line buffer lines in per line out in the beginning of the frame is the vertical filter initialization value*/
	/*rounded up to even and divided by the line times for initialization, which is normally three.*/
	/*the line buffer lines in per line out in the middle of the frame is at least one, or the vertical scale ratio,*/
	/*rounded up to line pairs if not doing line buffer prefetching.*/
	/**/
	/*the non-prefetching rounding up of the vertical scale ratio can also be done up to 1 (for a 0,2 pattern), 4/3 (for a 0,2,2 pattern),*/
	/*6/4 (for a 0,2,2,2 pattern), or 3 (for a 2,4 pattern).*/
	/**/
	/*the scaler vertical filter initialization value is calculated by the hardware as the floor of the average of the*/
	/*vertical scale ratio and the number of vertical taps increased by one.  add one more for possible odd line*/
	/*panning/bezel adjustment mode.*/
	/**/
	/*for the bottom interlace field an extra 50% of the vertical scale ratio is considered for this calculation.*/
	/*in top-bottom stereo mode software has to set the filter initialization value manually and explicitly limit it to 4.*/
	/*furthermore, there is only one line time for initialization.*/
	/**/
	/*line buffer prefetching is done when the number of lines in the line buffer exceeds the number of taps plus*/
	/*the ceiling of the vertical scale ratio.*/
	/**/
	/*multi-line buffer prefetching is only done in the graphics pipe when the scaler is disabled or when upscaling and the vsr <= 0.8.'*/
	/**/
	/*the horizontal blank and chunk granularity factor is indirectly used indicate the interval of time required to transfer the source pixels.*/
	/*the denominator of this term represents the total number of destination output pixels required for the input source pixels.*/
	/*it applies when the lines in per line out is not 2 or 4.  it does not apply when there is a line buffer between the scl and blnd.*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			data->v_filter_init[i] = bw_floor2(bw_div((bw_add(bw_add(bw_add(bw_int_to_fixed(1), data->v_taps[i]), data->vsr[i]), bw_mul(bw_mul(bw_int_to_fixed(data->interlace_mode[i]), bw_frc_to_fixed(5, 10)), data->vsr[i]))), bw_int_to_fixed(2)), bw_int_to_fixed(1));
			if (data->panning_and_bezel_adjustment == bw_def_any_lines) {
				data->v_filter_init[i] = bw_add(data->v_filter_init[i], bw_int_to_fixed(1));
			}
			if (data->stereo_mode[i] == bw_def_top_bottom) {
				data->v_filter_init[i] = bw_min2(data->v_filter_init[i], bw_int_to_fixed(4));
			}
			if (data->stereo_mode[i] == bw_def_top_bottom) {
				data->num_lines_at_frame_start = bw_int_to_fixed(1);
			}
			else {
				data->num_lines_at_frame_start = bw_int_to_fixed(3);
			}
			if ((bw_mtn(data->vsr[i], bw_int_to_fixed(1)) && surface_type[i] == bw_def_graphics) || data->panning_and_bezel_adjustment == bw_def_any_lines) {
				data->line_buffer_prefetch[i] = 0;
			}
			else if ((((dceip->underlay_downscale_prefetch_enabled == 1 && surface_type[i] != bw_def_graphics) || surface_type[i] == bw_def_graphics) && (bw_mtn(data->lb_partitions[i], bw_add(data->v_taps[i], bw_ceil2(data->vsr[i], bw_int_to_fixed(1))))))) {
				data->line_buffer_prefetch[i] = 1;
			}
			else {
				data->line_buffer_prefetch[i] = 0;
			}
			data->lb_lines_in_per_line_out_in_beginning_of_frame[i] = bw_div(bw_ceil2(data->v_filter_init[i], bw_int_to_fixed(dceip->lines_interleaved_into_lb)), data->num_lines_at_frame_start);
			if (data->line_buffer_prefetch[i] == 1) {
				data->lb_lines_in_per_line_out_in_middle_of_frame[i] = bw_max2(bw_int_to_fixed(1), data->vsr[i]);
			}
			else if (bw_leq(data->vsr[i], bw_int_to_fixed(1))) {
				data->lb_lines_in_per_line_out_in_middle_of_frame[i] = bw_int_to_fixed(1);
			} else if (bw_leq(data->vsr[i],
					bw_frc_to_fixed(4, 3))) {
				data->lb_lines_in_per_line_out_in_middle_of_frame[i] = bw_div(bw_int_to_fixed(4), bw_int_to_fixed(3));
			} else if (bw_leq(data->vsr[i],
					bw_frc_to_fixed(6, 4))) {
				data->lb_lines_in_per_line_out_in_middle_of_frame[i] = bw_div(bw_int_to_fixed(6), bw_int_to_fixed(4));
			}
			else if (bw_leq(data->vsr[i], bw_int_to_fixed(2))) {
				data->lb_lines_in_per_line_out_in_middle_of_frame[i] = bw_int_to_fixed(2);
			}
			else if (bw_leq(data->vsr[i], bw_int_to_fixed(3))) {
				data->lb_lines_in_per_line_out_in_middle_of_frame[i] = bw_int_to_fixed(3);
			}
			else {
				data->lb_lines_in_per_line_out_in_middle_of_frame[i] = bw_int_to_fixed(4);
			}
			if (data->line_buffer_prefetch[i] == 1 || bw_equ(data->lb_lines_in_per_line_out_in_middle_of_frame[i], bw_int_to_fixed(2)) || bw_equ(data->lb_lines_in_per_line_out_in_middle_of_frame[i], bw_int_to_fixed(4))) {
				data->horizontal_blank_and_chunk_granularity_factor[i] = bw_int_to_fixed(1);
			}
			else {
				data->horizontal_blank_and_chunk_granularity_factor[i] = bw_div(data->h_total[i], (bw_div((bw_add(data->h_total[i], bw_div((bw_sub(data->source_width_pixels[i], bw_int_to_fixed(dceip->chunk_width))), data->hsr[i]))), bw_int_to_fixed(2))));
			}
			data->request_bandwidth[i] = bw_div(bw_mul(bw_div(bw_mul(bw_div(bw_mul(bw_max2(data->lb_lines_in_per_line_out_in_beginning_of_frame[i], data->lb_lines_in_per_line_out_in_middle_of_frame[i]), data->source_width_rounded_up_to_chunks[i]), (bw_div(data->h_total[i], data->pixel_rate[i]))), bw_int_to_fixed(data->bytes_per_pixel[i])), data->useful_bytes_per_request[i]), data->lines_interleaved_in_mem_access[i]), data->latency_hiding_lines[i]);
			data->display_bandwidth[i] = bw_mul(data->request_bandwidth[i], data->bytes_per_request[i]);
		}
	}
	/*outstanding chunk request limit*/
	/*if underlay buffer sharing is enabled, the data buffer size for underlay in 422 or 444 is the sum of the luma and chroma data buffer sizes.*/
	/*underlay buffer sharing mode is only permitted in orthogonal rotation modes.*/
	/**/
	/*if there is only one display enabled, the dmif data buffer size for the graphics surface is increased by concatenating the adjacent buffers.*/
	/**/
	/*the memory chunk size in bytes is 1024 for the writeback, and 256 times the memory line interleaving and the bytes per pixel for graphics*/
	/*and underlay.*/
	/**/
	/*the pipe chunk size uses 2 for line interleaving, except for the write back, in which case it is 1.*/
	/*graphics and underlay data buffer size is adjusted (limited) using the outstanding chunk request limit if there is more than one*/
	/*display enabled or if the dmif request buffer is not large enough for the total data buffer size.*/
	/*the outstanding chunk request limit is the ceiling of the adjusted data buffer size divided by the chunk size in bytes*/
	/*the adjusted data buffer size is the product of the display bandwidth and the minimum effective data buffer size in terms of time,*/
	/*rounded up to the chunk size in bytes, but should not exceed the original data buffer size*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if ((dceip->dmif_pipe_en_fbc_chunk_tracker + 3 == i && fbc_enabled == 0 && tiling_mode[i] != bw_def_linear)) {
				data->max_chunks_non_fbc_mode[i] = 128 - dmif_chunk_buff_margin;
			}
			else {
				data->max_chunks_non_fbc_mode[i] = 16 - dmif_chunk_buff_margin;
			}
		}
		if (data->fbc_en[i] == 1) {
			max_chunks_fbc_mode = 128 - dmif_chunk_buff_margin;
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			switch (surface_type[i]) {
			case bw_def_display_write_back420_luma:
				data->data_buffer_size[i] = bw_int_to_fixed(dceip->display_write_back420_luma_mcifwr_buffer_size);
				break;
			case bw_def_display_write_back420_chroma:
				data->data_buffer_size[i] = bw_int_to_fixed(dceip->display_write_back420_chroma_mcifwr_buffer_size);
				break;
			case bw_def_underlay420_luma:
				data->data_buffer_size[i] = bw_int_to_fixed(dceip->underlay_luma_dmif_size);
				break;
			case bw_def_underlay420_chroma:
				data->data_buffer_size[i] = bw_div(bw_int_to_fixed(dceip->underlay_chroma_dmif_size), bw_int_to_fixed(2));
				break;
			case bw_def_underlay422:case bw_def_underlay444:
				if (data->orthogonal_rotation[i] == 0) {
					data->data_buffer_size[i] = bw_int_to_fixed(dceip->underlay_luma_dmif_size);
				}
				else {
					data->data_buffer_size[i] = bw_add(bw_int_to_fixed(dceip->underlay_luma_dmif_size), bw_int_to_fixed(dceip->underlay_chroma_dmif_size));
				}
				break;
			default:
				if (data->fbc_en[i] == 1) {
					/*data_buffer_size(i) = max_dmif_buffer_allocated * graphics_dmif_size*/
					if (data->number_of_displays == 1) {
						data->data_buffer_size[i] = bw_min2(bw_mul(bw_mul(bw_int_to_fixed(max_chunks_fbc_mode), bw_int_to_fixed(pixels_per_chunk)), bw_int_to_fixed(data->bytes_per_pixel[i])), bw_mul(bw_int_to_fixed(dceip->max_dmif_buffer_allocated), bw_int_to_fixed(dceip->graphics_dmif_size)));
					}
					else {
						data->data_buffer_size[i] = bw_min2(bw_mul(bw_mul(bw_int_to_fixed(max_chunks_fbc_mode), bw_int_to_fixed(pixels_per_chunk)), bw_int_to_fixed(data->bytes_per_pixel[i])), bw_int_to_fixed(dceip->graphics_dmif_size));
					}
				}
				else {
					/*the effective dmif buffer size in non-fbc mode is limited by the 16 entry chunk tracker*/
					if (data->number_of_displays == 1) {
						data->data_buffer_size[i] = bw_min2(bw_mul(bw_mul(bw_int_to_fixed(data->max_chunks_non_fbc_mode[i]), bw_int_to_fixed(pixels_per_chunk)), bw_int_to_fixed(data->bytes_per_pixel[i])), bw_mul(bw_int_to_fixed(dceip->max_dmif_buffer_allocated), bw_int_to_fixed(dceip->graphics_dmif_size)));
					}
					else {
						data->data_buffer_size[i] = bw_min2(bw_mul(bw_mul(bw_int_to_fixed(data->max_chunks_non_fbc_mode[i]), bw_int_to_fixed(pixels_per_chunk)), bw_int_to_fixed(data->bytes_per_pixel[i])), bw_int_to_fixed(dceip->graphics_dmif_size));
					}
				}
				break;
			}
			if (surface_type[i] == bw_def_display_write_back420_luma || surface_type[i] == bw_def_display_write_back420_chroma) {
				data->memory_chunk_size_in_bytes[i] = bw_int_to_fixed(1024);
				data->pipe_chunk_size_in_bytes[i] = bw_int_to_fixed(1024);
			}
			else {
				data->memory_chunk_size_in_bytes[i] = bw_mul(bw_mul(bw_int_to_fixed(dceip->chunk_width), data->lines_interleaved_in_mem_access[i]), bw_int_to_fixed(data->bytes_per_pixel[i]));
				data->pipe_chunk_size_in_bytes[i] = bw_mul(bw_mul(bw_int_to_fixed(dceip->chunk_width), bw_int_to_fixed(dceip->lines_interleaved_into_lb)), bw_int_to_fixed(data->bytes_per_pixel[i]));
			}
		}
	}
	data->min_dmif_size_in_time = bw_int_to_fixed(9999);
	data->min_mcifwr_size_in_time = bw_int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (surface_type[i] != bw_def_display_write_back420_luma && surface_type[i] != bw_def_display_write_back420_chroma) {
				if (bw_ltn(bw_div(bw_div(bw_mul(data->data_buffer_size[i], data->bytes_per_request[i]), data->useful_bytes_per_request[i]), data->display_bandwidth[i]), data->min_dmif_size_in_time)) {
					data->min_dmif_size_in_time = bw_div(bw_div(bw_mul(data->data_buffer_size[i], data->bytes_per_request[i]), data->useful_bytes_per_request[i]), data->display_bandwidth[i]);
				}
			}
			else {
				if (bw_ltn(bw_div(bw_div(bw_mul(data->data_buffer_size[i], data->bytes_per_request[i]), data->useful_bytes_per_request[i]), data->display_bandwidth[i]), data->min_mcifwr_size_in_time)) {
					data->min_mcifwr_size_in_time = bw_div(bw_div(bw_mul(data->data_buffer_size[i], data->bytes_per_request[i]), data->useful_bytes_per_request[i]), data->display_bandwidth[i]);
				}
			}
		}
	}
	data->total_requests_for_dmif_size = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i] && surface_type[i] != bw_def_display_write_back420_luma && surface_type[i] != bw_def_display_write_back420_chroma) {
			data->total_requests_for_dmif_size = bw_add(data->total_requests_for_dmif_size, bw_div(data->data_buffer_size[i], data->useful_bytes_per_request[i]));
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (surface_type[i] != bw_def_display_write_back420_luma && surface_type[i] != bw_def_display_write_back420_chroma && dceip->limit_excessive_outstanding_dmif_requests && (data->number_of_displays > 1 || bw_mtn(data->total_requests_for_dmif_size, dceip->dmif_request_buffer_size))) {
				data->adjusted_data_buffer_size[i] = bw_min2(data->data_buffer_size[i], bw_ceil2(bw_mul(data->min_dmif_size_in_time, data->display_bandwidth[i]), data->memory_chunk_size_in_bytes[i]));
			}
			else {
				data->adjusted_data_buffer_size[i] = data->data_buffer_size[i];
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (data->number_of_displays == 1 && data->number_of_underlay_surfaces == 0) {
				/*set maximum chunk limit if only one graphic pipe is enabled*/
				data->outstanding_chunk_request_limit[i] = bw_int_to_fixed(127);
			}
			else {
				data->outstanding_chunk_request_limit[i] = bw_ceil2(bw_div(data->adjusted_data_buffer_size[i], data->pipe_chunk_size_in_bytes[i]), bw_int_to_fixed(1));
				/*clamp maximum chunk limit in the graphic display pipe*/
				if (i >= 4) {
					data->outstanding_chunk_request_limit[i] = bw_max2(bw_int_to_fixed(127), data->outstanding_chunk_request_limit[i]);
				}
			}
		}
	}
	/*outstanding pte request limit*/
	/*in tiling mode with no rotation the sg pte requests are 8 useful pt_es, the sg row height is the page height and the sg page width x height is 64x64 for 8bpp, 64x32 for 16 bpp, 32x32 for 32 bpp*/
	/*in tiling mode with rotation the sg pte requests are only one useful pte, and the sg row height is also the page height, but the sg page width and height are swapped*/
	/*in linear mode the pte requests are 8 useful pt_es, the sg page width is 4096 divided by the bytes per pixel, the sg page height is 1, but there is just one row whose height is the lines of pte prefetching*/
	/*the outstanding pte request limit is obtained by multiplying the outstanding chunk request limit by the peak pte request to eviction limiting ratio, rounding up to integer, multiplying by the pte requests per chunk, and rounding up to integer again*/
	/*if not using peak pte request to eviction limiting, the outstanding pte request limit is the pte requests in the vblank*/
	/*the pte requests in the vblank is the product of the number of pte request rows times the number of pte requests in a row*/
	/*the number of pte requests in a row is the quotient of the source width divided by 256, multiplied by the pte requests per chunk, rounded up to even, multiplied by the scatter-gather row height and divided by the scatter-gather page height*/
	/*the pte requests per chunk is 256 divided by the scatter-gather page width and the useful pt_es per pte request*/
	if (data->number_of_displays > 1 || (bw_neq(data->rotation_angle[4], bw_int_to_fixed(0)) && bw_neq(data->rotation_angle[4], bw_int_to_fixed(180)))) {
		data->peak_pte_request_to_eviction_ratio_limiting = dceip->peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display;
	}
	else {
		data->peak_pte_request_to_eviction_ratio_limiting = dceip->peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation;
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i] && data->scatter_gather_enable_for_pipe[i] == 1) {
			if (tiling_mode[i] == bw_def_linear) {
				data->useful_pte_per_pte_request = bw_int_to_fixed(8);
				data->scatter_gather_page_width[i] = bw_div(bw_int_to_fixed(4096), bw_int_to_fixed(data->bytes_per_pixel[i]));
				data->scatter_gather_page_height[i] = bw_int_to_fixed(1);
				data->scatter_gather_pte_request_rows = bw_int_to_fixed(1);
				data->scatter_gather_row_height = bw_int_to_fixed(dceip->scatter_gather_lines_of_pte_prefetching_in_linear_mode);
			}
			else if (bw_equ(data->rotation_angle[i], bw_int_to_fixed(0)) || bw_equ(data->rotation_angle[i], bw_int_to_fixed(180))) {
				data->useful_pte_per_pte_request = bw_int_to_fixed(8);
				switch (data->bytes_per_pixel[i]) {
				case 4:
					data->scatter_gather_page_width[i] = bw_int_to_fixed(32);
					data->scatter_gather_page_height[i] = bw_int_to_fixed(32);
					break;
				case 2:
					data->scatter_gather_page_width[i] = bw_int_to_fixed(64);
					data->scatter_gather_page_height[i] = bw_int_to_fixed(32);
					break;
				default:
					data->scatter_gather_page_width[i] = bw_int_to_fixed(64);
					data->scatter_gather_page_height[i] = bw_int_to_fixed(64);
					break;
				}
				data->scatter_gather_pte_request_rows = bw_int_to_fixed(dceip->scatter_gather_pte_request_rows_in_tiling_mode);
				data->scatter_gather_row_height = data->scatter_gather_page_height[i];
			}
			else {
				data->useful_pte_per_pte_request = bw_int_to_fixed(1);
				switch (data->bytes_per_pixel[i]) {
				case 4:
					data->scatter_gather_page_width[i] = bw_int_to_fixed(32);
					data->scatter_gather_page_height[i] = bw_int_to_fixed(32);
					break;
				case 2:
					data->scatter_gather_page_width[i] = bw_int_to_fixed(32);
					data->scatter_gather_page_height[i] = bw_int_to_fixed(64);
					break;
				default:
					data->scatter_gather_page_width[i] = bw_int_to_fixed(64);
					data->scatter_gather_page_height[i] = bw_int_to_fixed(64);
					break;
				}
				data->scatter_gather_pte_request_rows = bw_int_to_fixed(dceip->scatter_gather_pte_request_rows_in_tiling_mode);
				data->scatter_gather_row_height = data->scatter_gather_page_height[i];
			}
			data->pte_request_per_chunk[i] = bw_div(bw_div(bw_int_to_fixed(dceip->chunk_width), data->scatter_gather_page_width[i]), data->useful_pte_per_pte_request);
			data->scatter_gather_pte_requests_in_row[i] = bw_div(bw_mul(bw_ceil2(bw_mul(bw_div(data->source_width_rounded_up_to_chunks[i], bw_int_to_fixed(dceip->chunk_width)), data->pte_request_per_chunk[i]), bw_int_to_fixed(1)), data->scatter_gather_row_height), data->scatter_gather_page_height[i]);
			data->scatter_gather_pte_requests_in_vblank = bw_mul(data->scatter_gather_pte_request_rows, data->scatter_gather_pte_requests_in_row[i]);
			if (bw_equ(data->peak_pte_request_to_eviction_ratio_limiting, bw_int_to_fixed(0))) {
				data->scatter_gather_pte_request_limit[i] = data->scatter_gather_pte_requests_in_vblank;
			}
			else {
				data->scatter_gather_pte_request_limit[i] = bw_max2(dceip->minimum_outstanding_pte_request_limit, bw_min2(data->scatter_gather_pte_requests_in_vblank, bw_ceil2(bw_mul(bw_mul(bw_div(bw_ceil2(data->adjusted_data_buffer_size[i], data->memory_chunk_size_in_bytes[i]), data->memory_chunk_size_in_bytes[i]), data->pte_request_per_chunk[i]), data->peak_pte_request_to_eviction_ratio_limiting), bw_int_to_fixed(1))));
			}
		}
	}
	/*pitch padding recommended for efficiency in linear mode*/
	/*in linear mode graphics or underlay with scatter gather, a pitch that is a multiple of the channel interleave (256 bytes) times the channel-bank rotation is not efficient*/
	/*if that is the case it is recommended to pad the pitch by at least 256 pixels*/
	data->inefficient_linear_pitch_in_bytes = bw_mul(bw_mul(bw_int_to_fixed(256), bw_int_to_fixed(vbios->number_of_dram_banks)), bw_int_to_fixed(data->number_of_dram_channels));

	/*pixel transfer time*/
	/*the dmif and mcifwr yclk(pclk) required is the one that allows the transfer of all pipe's data buffer size in memory in the time for data transfer*/
	/*for dmif, pte and cursor requests have to be included.*/
	/*the dram data requirement is doubled when the data request size in bytes is less than the dram channel width times the burst size (8)*/
	/*the dram data requirement is also multiplied by the number of channels in the case of low power tiling*/
	/*the page close-open time is determined by trc and the number of page close-opens*/
	/*in tiled mode graphics or underlay with scatter-gather enabled the bytes per page close-open is the product of the memory line interleave times the maximum of the scatter-gather page width and the product of the tile width (8 pixels) times the number of channels times the number of banks.*/
	/*in linear mode graphics or underlay with scatter-gather enabled and inefficient pitch, the bytes per page close-open is the line request alternation slice, because different lines are in completely different 4k address bases.*/
	/*otherwise, the bytes page close-open is the chunk size because that is the arbitration slice.*/
	/*pte requests are grouped by pte requests per chunk if that is more than 1. each group costs a page close-open time for dmif reads*/
	/*cursor requests outstanding are limited to a group of two source lines. each group costs a page close-open time for dmif reads*/
	/*the display reads and writes time for data transfer is the minimum data or cursor buffer size in time minus the mc urgent latency*/
	/*the mc urgent latency is experienced more than one time if the number of dmif requests in the data buffer exceeds the request buffer size plus the request slots reserved for dmif in the dram channel arbiter queues*/
	/*the dispclk required is the maximum for all surfaces of the maximum of the source pixels for first output pixel times the throughput factor, divided by the pixels per dispclk, and divided by the minimum latency hiding minus the dram speed/p-state change latency minus the burst time, and the source pixels for last output pixel, times the throughput factor, divided by the pixels per dispclk, and divided by the minimum latency hiding minus the dram speed/p-state change latency minus the burst time, plus the active time.*/
	/*the data burst time is the maximum of the total page close-open time, total dmif/mcifwr buffer size in memory divided by the dram bandwidth, and the total dmif/mcifwr buffer size in memory divided by the 32 byte sclk data bus bandwidth, each multiplied by its efficiency.*/
	/*the source line transfer time is the maximum for all surfaces of the maximum of the burst time plus the urgent latency times the floor of the data required divided by the buffer size for the fist pixel, and the burst time plus the urgent latency times the floor of the data required divided by the buffer size for the last pixel plus the active time.*/
	/*the source pixels for the first output pixel is 512 if the scaler vertical filter initialization value is greater than 2, and it is 4 times the source width if it is greater than 4.*/
	/*the source pixels for the last output pixel is the source width times the scaler vertical filter initialization value rounded up to even*/
	/*the source data for these pixels is the number of pixels times the bytes per pixel times the bytes per request divided by the useful bytes per request.*/
	data->cursor_total_data = bw_int_to_fixed(0);
	data->cursor_total_request_groups = bw_int_to_fixed(0);
	data->scatter_gather_total_pte_requests = bw_int_to_fixed(0);
	data->scatter_gather_total_pte_request_groups = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			data->cursor_total_data = bw_add(data->cursor_total_data, bw_mul(bw_mul(bw_int_to_fixed(2), data->cursor_width_pixels[i]), bw_int_to_fixed(4)));
			if (dceip->large_cursor == 1) {
				data->cursor_total_request_groups = bw_add(data->cursor_total_request_groups, bw_int_to_fixed((dceip->cursor_max_outstanding_group_num + 1)));
			}
			else {
				data->cursor_total_request_groups = bw_add(data->cursor_total_request_groups, bw_ceil2(bw_div(data->cursor_width_pixels[i], dceip->cursor_chunk_width), bw_int_to_fixed(1)));
			}
			if (data->scatter_gather_enable_for_pipe[i]) {
				data->scatter_gather_total_pte_requests = bw_add(data->scatter_gather_total_pte_requests, data->scatter_gather_pte_request_limit[i]);
				data->scatter_gather_total_pte_request_groups = bw_add(data->scatter_gather_total_pte_request_groups, bw_ceil2(bw_div(data->scatter_gather_pte_request_limit[i], bw_ceil2(data->pte_request_per_chunk[i], bw_int_to_fixed(1))), bw_int_to_fixed(1)));
			}
		}
	}
	data->tile_width_in_pixels = bw_int_to_fixed(8);
	data->dmif_total_number_of_data_request_page_close_open = bw_int_to_fixed(0);
	data->mcifwr_total_number_of_data_request_page_close_open = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (data->scatter_gather_enable_for_pipe[i] == 1 && tiling_mode[i] != bw_def_linear) {
				data->bytes_per_page_close_open = bw_mul(data->lines_interleaved_in_mem_access[i], bw_max2(bw_mul(bw_mul(bw_mul(bw_int_to_fixed(data->bytes_per_pixel[i]), data->tile_width_in_pixels), bw_int_to_fixed(vbios->number_of_dram_banks)), bw_int_to_fixed(data->number_of_dram_channels)), bw_mul(bw_int_to_fixed(data->bytes_per_pixel[i]), data->scatter_gather_page_width[i])));
			}
			else if (data->scatter_gather_enable_for_pipe[i] == 1 && tiling_mode[i] == bw_def_linear && bw_equ(bw_mod((bw_mul(data->pitch_in_pixels_after_surface_type[i], bw_int_to_fixed(data->bytes_per_pixel[i]))), data->inefficient_linear_pitch_in_bytes), bw_int_to_fixed(0))) {
				data->bytes_per_page_close_open = dceip->linear_mode_line_request_alternation_slice;
			}
			else {
				data->bytes_per_page_close_open = data->memory_chunk_size_in_bytes[i];
			}
			if (surface_type[i] != bw_def_display_write_back420_luma && surface_type[i] != bw_def_display_write_back420_chroma) {
				data->dmif_total_number_of_data_request_page_close_open = bw_add(data->dmif_total_number_of_data_request_page_close_open, bw_div(bw_ceil2(data->adjusted_data_buffer_size[i], data->memory_chunk_size_in_bytes[i]), data->bytes_per_page_close_open));
			}
			else {
				data->mcifwr_total_number_of_data_request_page_close_open = bw_add(data->mcifwr_total_number_of_data_request_page_close_open, bw_div(bw_ceil2(data->adjusted_data_buffer_size[i], data->memory_chunk_size_in_bytes[i]), data->bytes_per_page_close_open));
			}
		}
	}
	data->dmif_total_page_close_open_time = bw_div(bw_mul((bw_add(bw_add(data->dmif_total_number_of_data_request_page_close_open, data->scatter_gather_total_pte_request_groups), data->cursor_total_request_groups)), vbios->trc), bw_int_to_fixed(1000));
	data->mcifwr_total_page_close_open_time = bw_div(bw_mul(data->mcifwr_total_number_of_data_request_page_close_open, vbios->trc), bw_int_to_fixed(1000));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			data->adjusted_data_buffer_size_in_memory[i] = bw_div(bw_mul(data->adjusted_data_buffer_size[i], data->bytes_per_request[i]), data->useful_bytes_per_request[i]);
		}
	}
	data->total_requests_for_adjusted_dmif_size = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (surface_type[i] != bw_def_display_write_back420_luma && surface_type[i] != bw_def_display_write_back420_chroma) {
				data->total_requests_for_adjusted_dmif_size = bw_add(data->total_requests_for_adjusted_dmif_size, bw_div(data->adjusted_data_buffer_size[i], data->useful_bytes_per_request[i]));
			}
		}
	}
	data->total_dmifmc_urgent_trips = bw_ceil2(bw_div(data->total_requests_for_adjusted_dmif_size, (bw_add(dceip->dmif_request_buffer_size, bw_int_to_fixed(vbios->number_of_request_slots_gmc_reserves_for_dmif_per_channel * data->number_of_dram_channels)))), bw_int_to_fixed(1));
	data->total_dmifmc_urgent_latency = bw_mul(vbios->dmifmc_urgent_latency, data->total_dmifmc_urgent_trips);
	data->total_display_reads_required_data = bw_int_to_fixed(0);
	data->total_display_reads_required_dram_access_data = bw_int_to_fixed(0);
	data->total_display_writes_required_data = bw_int_to_fixed(0);
	data->total_display_writes_required_dram_access_data = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (surface_type[i] != bw_def_display_write_back420_luma && surface_type[i] != bw_def_display_write_back420_chroma) {
				data->display_reads_required_data = data->adjusted_data_buffer_size_in_memory[i];
				/*for hbm memories, each channel is split into 2 pseudo-channels that are each 64 bits in width.  each*/
				/*pseudo-channel may be read independently of one another.*/
				/*the read burst length (bl) for hbm memories is 4, so each read command will access 32 bytes of data.*/
				/*the 64 or 32 byte sized data is stored in one pseudo-channel.*/
				/*it will take 4 memclk cycles or 8 yclk cycles to fetch 64 bytes of data from the hbm memory (2 read commands).*/
				/*it will take 2 memclk cycles or 4 yclk cycles to fetch 32 bytes of data from the hbm memory (1 read command).*/
				/*for gddr5/ddr4 memories, there is additional overhead if the size of the request is smaller than 64 bytes.*/
				/*the read burst length (bl) for gddr5/ddr4 memories is 8, regardless of the size of the data request.*/
				/*therefore it will require 8 cycles to fetch 64 or 32 bytes of data from the memory.*/
				/*the memory efficiency will be 50% for the 32 byte sized data.*/
				if (vbios->memory_type == bw_def_hbm) {
					data->display_reads_required_dram_access_data = data->adjusted_data_buffer_size_in_memory[i];
				}
				else {
					data->display_reads_required_dram_access_data = bw_mul(data->adjusted_data_buffer_size_in_memory[i], bw_ceil2(bw_div(bw_int_to_fixed((8 * vbios->dram_channel_width_in_bits / 8)), data->bytes_per_request[i]), bw_int_to_fixed(1)));
				}
				data->total_display_reads_required_data = bw_add(data->total_display_reads_required_data, data->display_reads_required_data);
				data->total_display_reads_required_dram_access_data = bw_add(data->total_display_reads_required_dram_access_data, data->display_reads_required_dram_access_data);
			}
			else {
				data->total_display_writes_required_data = bw_add(data->total_display_writes_required_data, data->adjusted_data_buffer_size_in_memory[i]);
				data->total_display_writes_required_dram_access_data = bw_add(data->total_display_writes_required_dram_access_data, bw_mul(data->adjusted_data_buffer_size_in_memory[i], bw_ceil2(bw_div(bw_int_to_fixed(vbios->dram_channel_width_in_bits), data->bytes_per_request[i]), bw_int_to_fixed(1))));
			}
		}
	}
	data->total_display_reads_required_data = bw_add(bw_add(data->total_display_reads_required_data, data->cursor_total_data), bw_mul(data->scatter_gather_total_pte_requests, bw_int_to_fixed(64)));
	data->total_display_reads_required_dram_access_data = bw_add(bw_add(data->total_display_reads_required_dram_access_data, data->cursor_total_data), bw_mul(data->scatter_gather_total_pte_requests, bw_int_to_fixed(64)));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (bw_mtn(data->v_filter_init[i], bw_int_to_fixed(4))) {
				data->src_pixels_for_first_output_pixel[i] = bw_mul(bw_int_to_fixed(4), data->source_width_rounded_up_to_chunks[i]);
			}
			else {
				if (bw_mtn(data->v_filter_init[i], bw_int_to_fixed(2))) {
					data->src_pixels_for_first_output_pixel[i] = bw_int_to_fixed(512);
				}
				else {
					data->src_pixels_for_first_output_pixel[i] = bw_int_to_fixed(0);
				}
			}
			data->src_data_for_first_output_pixel[i] = bw_div(bw_mul(bw_mul(data->src_pixels_for_first_output_pixel[i], bw_int_to_fixed(data->bytes_per_pixel[i])), data->bytes_per_request[i]), data->useful_bytes_per_request[i]);
			data->src_pixels_for_last_output_pixel[i] = bw_mul(data->source_width_rounded_up_to_chunks[i], bw_max2(bw_ceil2(data->v_filter_init[i], bw_int_to_fixed(dceip->lines_interleaved_into_lb)), bw_mul(bw_ceil2(data->vsr[i], bw_int_to_fixed(dceip->lines_interleaved_into_lb)), data->horizontal_blank_and_chunk_granularity_factor[i])));
			data->src_data_for_last_output_pixel[i] = bw_div(bw_mul(bw_mul(bw_mul(data->source_width_rounded_up_to_chunks[i], bw_max2(bw_ceil2(data->v_filter_init[i], bw_int_to_fixed(dceip->lines_interleaved_into_lb)), data->lines_interleaved_in_mem_access[i])), bw_int_to_fixed(data->bytes_per_pixel[i])), data->bytes_per_request[i]), data->useful_bytes_per_request[i]);
			data->active_time[i] = bw_div(bw_div(data->source_width_rounded_up_to_chunks[i], data->hsr[i]), data->pixel_rate[i]);
		}
	}
	for (i = 0; i <= 2; i++) {
		for (j = 0; j <= 7; j++) {
			data->dmif_burst_time[i][j] = bw_max3(data->dmif_total_page_close_open_time, bw_div(data->total_display_reads_required_dram_access_data, (bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[i]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_channels)))), bw_div(data->total_display_reads_required_data, (bw_mul(bw_mul(sclk[j], vbios->data_return_bus_width), bw_frc_to_fixed(dceip->percent_of_ideal_port_bw_received_after_urgent_latency, 100)))));
			if (data->d1_display_write_back_dwb_enable == 1) {
				data->mcifwr_burst_time[i][j] = bw_max3(data->mcifwr_total_page_close_open_time, bw_div(data->total_display_writes_required_dram_access_data, (bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[i]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_wrchannels)))), bw_div(data->total_display_writes_required_data, (bw_mul(sclk[j], vbios->data_return_bus_width))));
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		for (j = 0; j <= 2; j++) {
			for (k = 0; k <= 7; k++) {
				if (data->enable[i]) {
					if (surface_type[i] != bw_def_display_write_back420_luma && surface_type[i] != bw_def_display_write_back420_chroma) {
						/*time to transfer data from the dmif buffer to the lb.  since the mc to dmif transfer time overlaps*/
						/*with the dmif to lb transfer time, only time to transfer the last chunk  is considered.*/
						data->dmif_buffer_transfer_time[i] = bw_mul(data->source_width_rounded_up_to_chunks[i], (bw_div(dceip->lb_write_pixels_per_dispclk, (bw_div(vbios->low_voltage_max_dispclk, dceip->display_pipe_throughput_factor)))));
						data->line_source_transfer_time[i][j][k] = bw_max2(bw_mul((bw_add(data->total_dmifmc_urgent_latency, data->dmif_burst_time[j][k])), bw_floor2(bw_div(data->src_data_for_first_output_pixel[i], data->adjusted_data_buffer_size_in_memory[i]), bw_int_to_fixed(1))), bw_sub(bw_add(bw_mul((bw_add(data->total_dmifmc_urgent_latency, data->dmif_burst_time[j][k])), bw_floor2(bw_div(data->src_data_for_last_output_pixel[i], data->adjusted_data_buffer_size_in_memory[i]), bw_int_to_fixed(1))), data->dmif_buffer_transfer_time[i]), data->active_time[i]));
						/*during an mclk switch the requests from the dce ip are stored in the gmc/arb.  these requests should be serviced immediately*/
						/*after the mclk switch sequence and not incur an urgent latency penalty.  it is assumed that the gmc/arb can hold up to 256 requests*/
						/*per memory channel.  if the dce ip is urgent after the mclk switch sequence, all pending requests and subsequent requests should be*/
						/*immediately serviced without a gap in the urgent requests.*/
						/*the latency incurred would be the time to issue the requests and return the data for the first or last output pixel.*/
						if (surface_type[i] == bw_def_graphics) {
							switch (data->lb_bpc[i]) {
							case 6:
								data->v_scaler_efficiency = dceip->graphics_vscaler_efficiency6_bit_per_component;
								break;
							case 8:
								data->v_scaler_efficiency = dceip->graphics_vscaler_efficiency8_bit_per_component;
								break;
							case 10:
								data->v_scaler_efficiency = dceip->graphics_vscaler_efficiency10_bit_per_component;
								break;
							default:
								data->v_scaler_efficiency = dceip->graphics_vscaler_efficiency12_bit_per_component;
								break;
							}
							if (data->use_alpha[i] == 1) {
								data->v_scaler_efficiency = bw_min2(data->v_scaler_efficiency, dceip->alpha_vscaler_efficiency);
							}
						}
						else {
							switch (data->lb_bpc[i]) {
							case 6:
								data->v_scaler_efficiency = dceip->underlay_vscaler_efficiency6_bit_per_component;
								break;
							case 8:
								data->v_scaler_efficiency = dceip->underlay_vscaler_efficiency8_bit_per_component;
								break;
							case 10:
								data->v_scaler_efficiency = dceip->underlay_vscaler_efficiency10_bit_per_component;
								break;
							default:
								data->v_scaler_efficiency = bw_int_to_fixed(3);
								break;
							}
						}
						if (dceip->pre_downscaler_enabled && bw_mtn(data->hsr[i], bw_int_to_fixed(1))) {
							data->scaler_limits_factor = bw_max2(bw_div(data->v_taps[i], data->v_scaler_efficiency), bw_div(data->source_width_rounded_up_to_chunks[i], data->h_total[i]));
						}
						else {
							data->scaler_limits_factor = bw_max3(bw_int_to_fixed(1), bw_ceil2(bw_div(data->h_taps[i], bw_int_to_fixed(4)), bw_int_to_fixed(1)), bw_mul(data->hsr[i], bw_max2(bw_div(data->v_taps[i], data->v_scaler_efficiency), bw_int_to_fixed(1))));
						}
						data->dram_speed_change_line_source_transfer_time[i][j][k] = bw_mul(bw_int_to_fixed(2), bw_max2((bw_add((bw_div(data->src_data_for_first_output_pixel[i], bw_min2(bw_mul(data->bytes_per_request[i], sclk[k]), bw_div(bw_mul(bw_mul(data->bytes_per_request[i], data->pixel_rate[i]), data->scaler_limits_factor), bw_int_to_fixed(2))))), (bw_mul(data->dmif_burst_time[j][k], bw_floor2(bw_div(data->src_data_for_first_output_pixel[i], data->adjusted_data_buffer_size_in_memory[i]), bw_int_to_fixed(1)))))), (bw_add((bw_div(data->src_data_for_last_output_pixel[i], bw_min2(bw_mul(data->bytes_per_request[i], sclk[k]), bw_div(bw_mul(bw_mul(data->bytes_per_request[i], data->pixel_rate[i]), data->scaler_limits_factor), bw_int_to_fixed(2))))), (bw_sub(bw_mul(data->dmif_burst_time[j][k], bw_floor2(bw_div(data->src_data_for_last_output_pixel[i], data->adjusted_data_buffer_size_in_memory[i]), bw_int_to_fixed(1))), data->active_time[i]))))));
					}
					else {
						data->line_source_transfer_time[i][j][k] = bw_max2(bw_mul((bw_add(vbios->mcifwrmc_urgent_latency, data->mcifwr_burst_time[j][k])), bw_floor2(bw_div(data->src_data_for_first_output_pixel[i], data->adjusted_data_buffer_size_in_memory[i]), bw_int_to_fixed(1))), bw_sub(bw_mul((bw_add(vbios->mcifwrmc_urgent_latency, data->mcifwr_burst_time[j][k])), bw_floor2(bw_div(data->src_data_for_last_output_pixel[i], data->adjusted_data_buffer_size_in_memory[i]), bw_int_to_fixed(1))), data->active_time[i]));
						/*during an mclk switch the requests from the dce ip are stored in the gmc/arb.  these requests should be serviced immediately*/
						/*after the mclk switch sequence and not incur an urgent latency penalty.  it is assumed that the gmc/arb can hold up to 256 requests*/
						/*per memory channel.  if the dce ip is urgent after the mclk switch sequence, all pending requests and subsequent requests should be*/
						/*immediately serviced without a gap in the urgent requests.*/
						/*the latency incurred would be the time to issue the requests and return the data for the first or last output pixel.*/
						data->dram_speed_change_line_source_transfer_time[i][j][k] = bw_max2((bw_add((bw_div(data->src_data_for_first_output_pixel[i], bw_min2(bw_mul(data->bytes_per_request[i], sclk[k]), bw_div(bw_mul(data->bytes_per_request[i], vbios->low_voltage_max_dispclk), bw_int_to_fixed(2))))), (bw_mul(data->mcifwr_burst_time[j][k], bw_floor2(bw_div(data->src_data_for_first_output_pixel[i], data->adjusted_data_buffer_size_in_memory[i]), bw_int_to_fixed(1)))))), (bw_add((bw_div(data->src_data_for_last_output_pixel[i], bw_min2(bw_mul(data->bytes_per_request[i], sclk[k]), bw_div(bw_mul(data->bytes_per_request[i], vbios->low_voltage_max_dispclk), bw_int_to_fixed(2))))), (bw_sub(bw_mul(data->mcifwr_burst_time[j][k], bw_floor2(bw_div(data->src_data_for_last_output_pixel[i], data->adjusted_data_buffer_size_in_memory[i]), bw_int_to_fixed(1))), data->active_time[i])))));
					}
				}
			}
		}
	}
	/*cpu c-state and p-state change enable*/
	/*for cpu p-state change to be possible for a yclk(pclk) and sclk level the dispclk required has to be enough for the blackout duration*/
	/*for cpu c-state change to be possible for a yclk(pclk) and sclk level the dispclk required has to be enough for the blackout duration and recovery*/
	/*condition for the blackout duration:*/
	/* minimum latency hiding > blackout duration + dmif burst time + line source transfer time*/
	/*condition for the blackout recovery:*/
	/* recovery time >  dmif burst time + 2 * urgent latency*/
	/* recovery time > (display bw * blackout duration  + (2 * urgent latency + dmif burst time)*dispclk - dmif size )*/
	/*                  / (dispclk - display bw)*/
	/*the minimum latency hiding is the minimum for all pipes of one screen line time, plus one more line time if doing lb prefetch, plus the dmif data buffer size equivalent in time, minus the urgent latency.*/
	/*the minimum latency hiding is  further limited by the cursor.  the cursor latency hiding is the number of lines of the cursor buffer, minus one if the downscaling is less than two, or minus three if it is more*/

	/*initialize variables*/
	number_of_displays_enabled = 0;
	number_of_displays_enabled_with_margin = 0;
	for (k = 0; k <= maximum_number_of_surfaces - 1; k++) {
		if (data->enable[k]) {
			number_of_displays_enabled = number_of_displays_enabled + 1;
		}
		data->display_pstate_change_enable[k] = 0;
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if ((bw_equ(dceip->stutter_and_dram_clock_state_change_gated_before_cursor, bw_int_to_fixed(0)) && bw_mtn(data->cursor_width_pixels[i], bw_int_to_fixed(0)))) {
				if (bw_ltn(data->vsr[i], bw_int_to_fixed(2))) {
					data->cursor_latency_hiding[i] = bw_div(bw_div(bw_mul((bw_sub(dceip->cursor_dcp_buffer_lines, bw_int_to_fixed(1))), data->h_total[i]), data->vsr[i]), data->pixel_rate[i]);
				}
				else {
					data->cursor_latency_hiding[i] = bw_div(bw_div(bw_mul((bw_sub(dceip->cursor_dcp_buffer_lines, bw_int_to_fixed(3))), data->h_total[i]), data->vsr[i]), data->pixel_rate[i]);
				}
			}
			else {
				data->cursor_latency_hiding[i] = bw_int_to_fixed(9999);
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (dceip->graphics_lb_nodownscaling_multi_line_prefetching == 1 && (bw_equ(data->vsr[i], bw_int_to_fixed(1)) || (bw_leq(data->vsr[i], bw_frc_to_fixed(8, 10)) && bw_leq(data->v_taps[i], bw_int_to_fixed(2)) && data->lb_bpc[i] == 8)) && surface_type[i] == bw_def_graphics) {
				if (number_of_displays_enabled > 2)
					data->minimum_latency_hiding[i] = bw_sub(bw_div(bw_mul((bw_div((bw_add(bw_sub(data->lb_partitions[i], bw_int_to_fixed(2)), bw_div(bw_div(data->data_buffer_size[i], bw_int_to_fixed(data->bytes_per_pixel[i])), data->source_width_pixels[i]))), data->vsr[i])), data->h_total[i]), data->pixel_rate[i]), data->total_dmifmc_urgent_latency);
				else
					data->minimum_latency_hiding[i] = bw_sub(bw_div(bw_mul((bw_div((bw_add(bw_sub(data->lb_partitions[i], bw_int_to_fixed(1)), bw_div(bw_div(data->data_buffer_size[i], bw_int_to_fixed(data->bytes_per_pixel[i])), data->source_width_pixels[i]))), data->vsr[i])), data->h_total[i]), data->pixel_rate[i]), data->total_dmifmc_urgent_latency);
			}
			else {
				data->minimum_latency_hiding[i] = bw_sub(bw_div(bw_mul((bw_div((bw_add(bw_int_to_fixed(1 + data->line_buffer_prefetch[i]), bw_div(bw_div(data->data_buffer_size[i], bw_int_to_fixed(data->bytes_per_pixel[i])), data->source_width_pixels[i]))), data->vsr[i])), data->h_total[i]), data->pixel_rate[i]), data->total_dmifmc_urgent_latency);
			}
			data->minimum_latency_hiding_with_cursor[i] = bw_min2(data->minimum_latency_hiding[i], data->cursor_latency_hiding[i]);
		}
	}
	for (i = 0; i <= 2; i++) {
		for (j = 0; j <= 7; j++) {
			data->blackout_duration_margin[i][j] = bw_int_to_fixed(9999);
			data->dispclk_required_for_blackout_duration[i][j] = bw_int_to_fixed(0);
			data->dispclk_required_for_blackout_recovery[i][j] = bw_int_to_fixed(0);
			for (k = 0; k <= maximum_number_of_surfaces - 1; k++) {
				if (data->enable[k] && bw_mtn(vbios->blackout_duration, bw_int_to_fixed(0))) {
					if (surface_type[k] != bw_def_display_write_back420_luma && surface_type[k] != bw_def_display_write_back420_chroma) {
						data->blackout_duration_margin[i][j] = bw_min2(data->blackout_duration_margin[i][j], bw_sub(bw_sub(bw_sub(data->minimum_latency_hiding_with_cursor[k], vbios->blackout_duration), data->dmif_burst_time[i][j]), data->line_source_transfer_time[k][i][j]));
						data->dispclk_required_for_blackout_duration[i][j] = bw_max3(data->dispclk_required_for_blackout_duration[i][j], bw_div(bw_div(bw_mul(data->src_pixels_for_first_output_pixel[k], dceip->display_pipe_throughput_factor), dceip->lb_write_pixels_per_dispclk), (bw_sub(bw_sub(data->minimum_latency_hiding_with_cursor[k], vbios->blackout_duration), data->dmif_burst_time[i][j]))), bw_div(bw_div(bw_mul(data->src_pixels_for_last_output_pixel[k], dceip->display_pipe_throughput_factor), dceip->lb_write_pixels_per_dispclk), (bw_add(bw_sub(bw_sub(data->minimum_latency_hiding_with_cursor[k], vbios->blackout_duration), data->dmif_burst_time[i][j]), data->active_time[k]))));
						if (bw_leq(vbios->maximum_blackout_recovery_time, bw_add(bw_mul(bw_int_to_fixed(2), data->total_dmifmc_urgent_latency), data->dmif_burst_time[i][j]))) {
							data->dispclk_required_for_blackout_recovery[i][j] = bw_int_to_fixed(9999);
						}
						else if (bw_ltn(data->adjusted_data_buffer_size[k], bw_mul(bw_div(bw_mul(data->display_bandwidth[k], data->useful_bytes_per_request[k]), data->bytes_per_request[k]), (bw_add(vbios->blackout_duration, bw_add(bw_mul(bw_int_to_fixed(2), data->total_dmifmc_urgent_latency), data->dmif_burst_time[i][j])))))) {
							data->dispclk_required_for_blackout_recovery[i][j] = bw_max2(data->dispclk_required_for_blackout_recovery[i][j], bw_div(bw_mul(bw_div(bw_div((bw_sub(bw_mul(bw_div(bw_mul(data->display_bandwidth[k], data->useful_bytes_per_request[k]), data->bytes_per_request[k]), (bw_add(vbios->blackout_duration, vbios->maximum_blackout_recovery_time))), data->adjusted_data_buffer_size[k])), bw_int_to_fixed(data->bytes_per_pixel[k])), (bw_sub(vbios->maximum_blackout_recovery_time, bw_sub(bw_mul(bw_int_to_fixed(2), data->total_dmifmc_urgent_latency), data->dmif_burst_time[i][j])))), data->latency_hiding_lines[k]), data->lines_interleaved_in_mem_access[k]));
						}
					}
					else {
						data->blackout_duration_margin[i][j] = bw_min2(data->blackout_duration_margin[i][j], bw_sub(bw_sub(bw_sub(bw_sub(data->minimum_latency_hiding_with_cursor[k], vbios->blackout_duration), data->dmif_burst_time[i][j]), data->mcifwr_burst_time[i][j]), data->line_source_transfer_time[k][i][j]));
						data->dispclk_required_for_blackout_duration[i][j] = bw_max3(data->dispclk_required_for_blackout_duration[i][j], bw_div(bw_div(bw_mul(data->src_pixels_for_first_output_pixel[k], dceip->display_pipe_throughput_factor), dceip->lb_write_pixels_per_dispclk), (bw_sub(bw_sub(bw_sub(data->minimum_latency_hiding_with_cursor[k], vbios->blackout_duration), data->dmif_burst_time[i][j]), data->mcifwr_burst_time[i][j]))), bw_div(bw_div(bw_mul(data->src_pixels_for_last_output_pixel[k], dceip->display_pipe_throughput_factor), dceip->lb_write_pixels_per_dispclk), (bw_add(bw_sub(bw_sub(bw_sub(data->minimum_latency_hiding_with_cursor[k], vbios->blackout_duration), data->dmif_burst_time[i][j]), data->mcifwr_burst_time[i][j]), data->active_time[k]))));
						if (bw_ltn(vbios->maximum_blackout_recovery_time, bw_add(bw_add(bw_mul(bw_int_to_fixed(2), vbios->mcifwrmc_urgent_latency), data->dmif_burst_time[i][j]), data->mcifwr_burst_time[i][j]))) {
							data->dispclk_required_for_blackout_recovery[i][j] = bw_int_to_fixed(9999);
						}
						else if (bw_ltn(data->adjusted_data_buffer_size[k], bw_mul(bw_div(bw_mul(data->display_bandwidth[k], data->useful_bytes_per_request[k]), data->bytes_per_request[k]), (bw_add(vbios->blackout_duration, bw_add(bw_mul(bw_int_to_fixed(2), data->total_dmifmc_urgent_latency), data->dmif_burst_time[i][j])))))) {
							data->dispclk_required_for_blackout_recovery[i][j] = bw_max2(data->dispclk_required_for_blackout_recovery[i][j], bw_div(bw_mul(bw_div(bw_div((bw_sub(bw_mul(bw_div(bw_mul(data->display_bandwidth[k], data->useful_bytes_per_request[k]), data->bytes_per_request[k]), (bw_add(vbios->blackout_duration, vbios->maximum_blackout_recovery_time))), data->adjusted_data_buffer_size[k])), bw_int_to_fixed(data->bytes_per_pixel[k])), (bw_sub(vbios->maximum_blackout_recovery_time, (bw_add(bw_mul(bw_int_to_fixed(2), data->total_dmifmc_urgent_latency), data->dmif_burst_time[i][j]))))), data->latency_hiding_lines[k]), data->lines_interleaved_in_mem_access[k]));
						}
					}
				}
			}
		}
	}
	if (bw_mtn(data->blackout_duration_margin[high][s_high], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[high][s_high], vbios->high_voltage_max_dispclk)) {
		data->cpup_state_change_enable = bw_def_yes;
		if (bw_ltn(data->dispclk_required_for_blackout_recovery[high][s_high], vbios->high_voltage_max_dispclk)) {
			data->cpuc_state_change_enable = bw_def_yes;
		}
		else {
			data->cpuc_state_change_enable = bw_def_no;
		}
	}
	else {
		data->cpup_state_change_enable = bw_def_no;
		data->cpuc_state_change_enable = bw_def_no;
	}
	/*nb p-state change enable*/
	/*for dram speed/p-state change to be possible for a yclk(pclk) and sclk level there has to be positive margin and the dispclk required has to be*/
	/*below the maximum.*/
	/*the dram speed/p-state change margin is the minimum for all surfaces of the maximum latency hiding minus the dram speed/p-state change latency,*/
	/*minus the dmif burst time, minus the source line transfer time*/
	/*the maximum latency hiding is the minimum latency hiding plus one source line used for de-tiling in the line buffer, plus half the urgent latency*/
	/*if stutter and dram clock state change are gated before cursor then the cursor latency hiding does not limit stutter or dram clock state change*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			/*maximum_latency_hiding(i) = minimum_latency_hiding(i) + 1 / vsr(i) **/
			/*      h_total(i) / pixel_rate(i) + 0.5 * total_dmifmc_urgent_latency*/
			data->maximum_latency_hiding[i] = bw_add(data->minimum_latency_hiding[i],
				bw_mul(bw_frc_to_fixed(5, 10), data->total_dmifmc_urgent_latency));
			data->maximum_latency_hiding_with_cursor[i] = bw_min2(data->maximum_latency_hiding[i], data->cursor_latency_hiding[i]);
		}
	}
	for (i = 0; i <= 2; i++) {
		for (j = 0; j <= 7; j++) {
			data->min_dram_speed_change_margin[i][j] = bw_int_to_fixed(9999);
			data->dram_speed_change_margin = bw_int_to_fixed(9999);
			data->dispclk_required_for_dram_speed_change[i][j] = bw_int_to_fixed(0);
			data->num_displays_with_margin[i][j] = 0;
			for (k = 0; k <= maximum_number_of_surfaces - 1; k++) {
				if (data->enable[k]) {
					if (surface_type[k] != bw_def_display_write_back420_luma && surface_type[k] != bw_def_display_write_back420_chroma) {
						data->dram_speed_change_margin = bw_sub(bw_sub(bw_sub(data->maximum_latency_hiding_with_cursor[k], vbios->nbp_state_change_latency), data->dmif_burst_time[i][j]), data->dram_speed_change_line_source_transfer_time[k][i][j]);
						if ((bw_mtn(data->dram_speed_change_margin, bw_int_to_fixed(0)) && bw_ltn(data->dram_speed_change_margin, bw_int_to_fixed(9999)))) {
							/*determine the minimum dram clock change margin for each set of clock frequencies*/
							data->min_dram_speed_change_margin[i][j] = bw_min2(data->min_dram_speed_change_margin[i][j], data->dram_speed_change_margin);
							/*compute the maximum clock frequuency required for the dram clock change at each set of clock frequencies*/
							data->dispclk_required_for_dram_speed_change_pipe[i][j] = bw_max2(bw_div(bw_div(bw_mul(data->src_pixels_for_first_output_pixel[k], dceip->display_pipe_throughput_factor), dceip->lb_write_pixels_per_dispclk), (bw_sub(bw_sub(bw_sub(data->maximum_latency_hiding_with_cursor[k], vbios->nbp_state_change_latency), data->dmif_burst_time[i][j]), data->dram_speed_change_line_source_transfer_time[k][i][j]))), bw_div(bw_div(bw_mul(data->src_pixels_for_last_output_pixel[k], dceip->display_pipe_throughput_factor), dceip->lb_write_pixels_per_dispclk), (bw_add(bw_sub(bw_sub(bw_sub(data->maximum_latency_hiding_with_cursor[k], vbios->nbp_state_change_latency), data->dmif_burst_time[i][j]), data->dram_speed_change_line_source_transfer_time[k][i][j]), data->active_time[k]))));
							if ((bw_ltn(data->dispclk_required_for_dram_speed_change_pipe[i][j], vbios->high_voltage_max_dispclk))) {
								data->display_pstate_change_enable[k] = 1;
								data->num_displays_with_margin[i][j] = data->num_displays_with_margin[i][j] + 1;
								data->dispclk_required_for_dram_speed_change[i][j] = bw_max2(data->dispclk_required_for_dram_speed_change[i][j], data->dispclk_required_for_dram_speed_change_pipe[i][j]);
							}
						}
					}
					else {
						data->dram_speed_change_margin = bw_sub(bw_sub(bw_sub(bw_sub(data->maximum_latency_hiding_with_cursor[k], vbios->nbp_state_change_latency), data->dmif_burst_time[i][j]), data->mcifwr_burst_time[i][j]), data->dram_speed_change_line_source_transfer_time[k][i][j]);
						if ((bw_mtn(data->dram_speed_change_margin, bw_int_to_fixed(0)) && bw_ltn(data->dram_speed_change_margin, bw_int_to_fixed(9999)))) {
							/*determine the minimum dram clock change margin for each display pipe*/
							data->min_dram_speed_change_margin[i][j] = bw_min2(data->min_dram_speed_change_margin[i][j], data->dram_speed_change_margin);
							/*compute the maximum clock frequuency required for the dram clock change at each set of clock frequencies*/
							data->dispclk_required_for_dram_speed_change_pipe[i][j] = bw_max2(bw_div(bw_div(bw_mul(data->src_pixels_for_first_output_pixel[k], dceip->display_pipe_throughput_factor), dceip->lb_write_pixels_per_dispclk), (bw_sub(bw_sub(bw_sub(bw_sub(data->maximum_latency_hiding_with_cursor[k], vbios->nbp_state_change_latency), data->dmif_burst_time[i][j]), data->dram_speed_change_line_source_transfer_time[k][i][j]), data->mcifwr_burst_time[i][j]))), bw_div(bw_div(bw_mul(data->src_pixels_for_last_output_pixel[k], dceip->display_pipe_throughput_factor), dceip->lb_write_pixels_per_dispclk), (bw_add(bw_sub(bw_sub(bw_sub(bw_sub(data->maximum_latency_hiding_with_cursor[k], vbios->nbp_state_change_latency), data->dmif_burst_time[i][j]), data->dram_speed_change_line_source_transfer_time[k][i][j]), data->mcifwr_burst_time[i][j]), data->active_time[k]))));
							if ((bw_ltn(data->dispclk_required_for_dram_speed_change_pipe[i][j], vbios->high_voltage_max_dispclk))) {
								data->display_pstate_change_enable[k] = 1;
								data->num_displays_with_margin[i][j] = data->num_displays_with_margin[i][j] + 1;
								data->dispclk_required_for_dram_speed_change[i][j] = bw_max2(data->dispclk_required_for_dram_speed_change[i][j], data->dispclk_required_for_dram_speed_change_pipe[i][j]);
							}
						}
					}
				}
			}
		}
	}
	/*determine the number of displays with margin to switch in the v_active region*/
	for (k = 0; k <= maximum_number_of_surfaces - 1; k++) {
		if (data->enable[k] == 1 && data->display_pstate_change_enable[k] == 1) {
			number_of_displays_enabled_with_margin = number_of_displays_enabled_with_margin + 1;
		}
	}
	/*determine the number of displays that don't have any dram clock change margin, but*/
	/*have the same resolution.  these displays can switch in a common vblank region if*/
	/*their frames are aligned.*/
	data->min_vblank_dram_speed_change_margin = bw_int_to_fixed(9999);
	for (k = 0; k <= maximum_number_of_surfaces - 1; k++) {
		if (data->enable[k]) {
			if (surface_type[k] != bw_def_display_write_back420_luma && surface_type[k] != bw_def_display_write_back420_chroma) {
				data->v_blank_dram_speed_change_margin[k] = bw_sub(bw_sub(bw_sub(bw_div(bw_mul((bw_sub(data->v_total[k], bw_sub(bw_div(data->src_height[k], data->v_scale_ratio[k]), bw_int_to_fixed(4)))), data->h_total[k]), data->pixel_rate[k]), vbios->nbp_state_change_latency), data->dmif_burst_time[low][s_low]), data->dram_speed_change_line_source_transfer_time[k][low][s_low]);
				data->min_vblank_dram_speed_change_margin = bw_min2(data->min_vblank_dram_speed_change_margin, data->v_blank_dram_speed_change_margin[k]);
			}
			else {
				data->v_blank_dram_speed_change_margin[k] = bw_sub(bw_sub(bw_sub(bw_sub(bw_div(bw_mul((bw_sub(data->v_total[k], bw_sub(bw_div(data->src_height[k], data->v_scale_ratio[k]), bw_int_to_fixed(4)))), data->h_total[k]), data->pixel_rate[k]), vbios->nbp_state_change_latency), data->dmif_burst_time[low][s_low]), data->mcifwr_burst_time[low][s_low]), data->dram_speed_change_line_source_transfer_time[k][low][s_low]);
				data->min_vblank_dram_speed_change_margin = bw_min2(data->min_vblank_dram_speed_change_margin, data->v_blank_dram_speed_change_margin[k]);
			}
		}
	}
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		data->displays_with_same_mode[i] = bw_int_to_fixed(0);
		if (data->enable[i] == 1 && data->display_pstate_change_enable[i] == 0 && bw_mtn(data->v_blank_dram_speed_change_margin[i], bw_int_to_fixed(0))) {
			for (j = 0; j <= maximum_number_of_surfaces - 1; j++) {
				if ((i == j || data->display_synchronization_enabled) && (data->enable[j] == 1 && bw_equ(data->source_width_rounded_up_to_chunks[i], data->source_width_rounded_up_to_chunks[j]) && bw_equ(data->source_height_rounded_up_to_chunks[i], data->source_height_rounded_up_to_chunks[j]) && bw_equ(data->vsr[i], data->vsr[j]) && bw_equ(data->hsr[i], data->hsr[j]) && bw_equ(data->pixel_rate[i], data->pixel_rate[j]))) {
					data->displays_with_same_mode[i] = bw_add(data->displays_with_same_mode[i], bw_int_to_fixed(1));
				}
			}
		}
	}
	/*compute the maximum number of aligned displays with no margin*/
	number_of_aligned_displays_with_no_margin = 0;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		number_of_aligned_displays_with_no_margin = bw_fixed_to_int(bw_max2(bw_int_to_fixed(number_of_aligned_displays_with_no_margin), data->displays_with_same_mode[i]));
	}
	/*dram clock change is possible, if all displays have positive margin except for one display or a group of*/
	/*aligned displays with the same timing.*/
	/*the display(s) with the negative margin can be switched in the v_blank region while the other*/
	/*displays are in v_blank or v_active.*/
	if (number_of_displays_enabled_with_margin > 0 && (number_of_displays_enabled_with_margin + number_of_aligned_displays_with_no_margin) == number_of_displays_enabled && bw_mtn(data->min_dram_speed_change_margin[high][s_high], bw_int_to_fixed(0)) && bw_ltn(data->min_dram_speed_change_margin[high][s_high], bw_int_to_fixed(9999)) && bw_ltn(data->dispclk_required_for_dram_speed_change[high][s_high], vbios->high_voltage_max_dispclk)) {
		data->nbp_state_change_enable = bw_def_yes;
	}
	else {
		data->nbp_state_change_enable = bw_def_no;
	}
	/*dram clock change is possible only in vblank if all displays are aligned and have no margin*/
	if (number_of_aligned_displays_with_no_margin == number_of_displays_enabled) {
		nbp_state_change_enable_blank = bw_def_yes;
	}
	else {
		nbp_state_change_enable_blank = bw_def_no;
	}

	/*average bandwidth*/
	/*the average bandwidth with no compression is the vertical active time is the source width times the bytes per pixel divided by the line time, multiplied by the vertical scale ratio and the ratio of bytes per request divided by the useful bytes per request.*/
	/*the average bandwidth with compression is the same, divided by the compression ratio*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			data->average_bandwidth_no_compression[i] = bw_div(bw_mul(bw_mul(bw_div(bw_mul(data->source_width_rounded_up_to_chunks[i], bw_int_to_fixed(data->bytes_per_pixel[i])), (bw_div(data->h_total[i], data->pixel_rate[i]))), data->vsr[i]), data->bytes_per_request[i]), data->useful_bytes_per_request[i]);
			data->average_bandwidth[i] = bw_div(data->average_bandwidth_no_compression[i], data->compression_rate[i]);
		}
	}
	data->total_average_bandwidth_no_compression = bw_int_to_fixed(0);
	data->total_average_bandwidth = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			data->total_average_bandwidth_no_compression = bw_add(data->total_average_bandwidth_no_compression, data->average_bandwidth_no_compression[i]);
			data->total_average_bandwidth = bw_add(data->total_average_bandwidth, data->average_bandwidth[i]);
		}
	}

	/*required yclk(pclk)*/
	/*yclk requirement only makes sense if the dmif and mcifwr data total page close-open time is less than the time for data transfer and the total pte requests fit in the scatter-gather saw queque size*/
	/*if that is the case, the yclk requirement is the maximum of the ones required by dmif and mcifwr, and the high/low yclk(pclk) is chosen accordingly*/
	/*high yclk(pclk) has to be selected when dram speed/p-state change is not possible.*/
	data->min_cursor_memory_interface_buffer_size_in_time = bw_int_to_fixed(9999);
	/* number of cursor lines stored in the cursor data return buffer*/
	num_cursor_lines = 0;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (bw_mtn(data->cursor_width_pixels[i], bw_int_to_fixed(0))) {
				/*compute number of cursor lines stored in data return buffer*/
				if (bw_leq(data->cursor_width_pixels[i], bw_int_to_fixed(64)) && dceip->large_cursor == 1) {
					num_cursor_lines = 4;
				}
				else {
					num_cursor_lines = 2;
				}
				data->min_cursor_memory_interface_buffer_size_in_time = bw_min2(data->min_cursor_memory_interface_buffer_size_in_time, bw_div(bw_mul(bw_div(bw_int_to_fixed(num_cursor_lines), data->vsr[i]), data->h_total[i]), data->pixel_rate[i]));
			}
		}
	}
	/*compute minimum time to read one chunk from the dmif buffer*/
	if (number_of_displays_enabled > 2) {
		data->chunk_request_delay = 0;
	}
	else {
		data->chunk_request_delay = bw_fixed_to_int(bw_div(bw_int_to_fixed(512), vbios->high_voltage_max_dispclk));
	}
	data->min_read_buffer_size_in_time = bw_min2(data->min_cursor_memory_interface_buffer_size_in_time, data->min_dmif_size_in_time);
	data->display_reads_time_for_data_transfer = bw_sub(bw_sub(data->min_read_buffer_size_in_time, data->total_dmifmc_urgent_latency), bw_int_to_fixed(data->chunk_request_delay));
	data->display_writes_time_for_data_transfer = bw_sub(data->min_mcifwr_size_in_time, vbios->mcifwrmc_urgent_latency);
	data->dmif_required_dram_bandwidth = bw_div(data->total_display_reads_required_dram_access_data, data->display_reads_time_for_data_transfer);
	data->mcifwr_required_dram_bandwidth = bw_div(data->total_display_writes_required_dram_access_data, data->display_writes_time_for_data_transfer);
	data->required_dmifmc_urgent_latency_for_page_close_open = bw_div((bw_sub(data->min_read_buffer_size_in_time, data->dmif_total_page_close_open_time)), data->total_dmifmc_urgent_trips);
	data->required_mcifmcwr_urgent_latency = bw_sub(data->min_mcifwr_size_in_time, data->mcifwr_total_page_close_open_time);
	if (bw_mtn(data->scatter_gather_total_pte_requests, dceip->maximum_total_outstanding_pte_requests_allowed_by_saw)) {
		data->required_dram_bandwidth_gbyte_per_second = bw_int_to_fixed(9999);
		yclk_message = bw_def_exceeded_allowed_outstanding_pte_req_queue_size;
		data->y_clk_level = high;
		data->dram_bandwidth = bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[high]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_channels));
	}
	else if (bw_mtn(vbios->dmifmc_urgent_latency, data->required_dmifmc_urgent_latency_for_page_close_open) || bw_mtn(vbios->mcifwrmc_urgent_latency, data->required_mcifmcwr_urgent_latency)) {
		data->required_dram_bandwidth_gbyte_per_second = bw_int_to_fixed(9999);
		yclk_message = bw_def_exceeded_allowed_page_close_open;
		data->y_clk_level = high;
		data->dram_bandwidth = bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[high]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_channels));
	}
	else {
		data->required_dram_bandwidth_gbyte_per_second = bw_div(bw_max2(data->dmif_required_dram_bandwidth, data->mcifwr_required_dram_bandwidth), bw_int_to_fixed(1000));
		if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_drambw_display_can_use_in_normal_system_operation, 100),yclk[low]),bw_div(bw_int_to_fixed(vbios->dram_channel_width_in_bits),bw_int_to_fixed(8))),bw_int_to_fixed(vbios->number_of_dram_channels)))
				&& bw_ltn(bw_mul(data->required_dram_bandwidth_gbyte_per_second, bw_int_to_fixed(1000)), bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[low]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_channels))) && (data->cpup_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[low][s_high], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[low][s_high], vbios->high_voltage_max_dispclk))) && (data->cpuc_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[low][s_high], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[low][s_high], vbios->high_voltage_max_dispclk) && bw_ltn(data->dispclk_required_for_blackout_recovery[low][s_high], vbios->high_voltage_max_dispclk))) && (!data->increase_voltage_to_support_mclk_switch || data->nbp_state_change_enable == bw_def_no || (bw_mtn(data->min_dram_speed_change_margin[low][s_high], bw_int_to_fixed(0)) && bw_ltn(data->min_dram_speed_change_margin[low][s_high], bw_int_to_fixed(9999)) && bw_leq(data->dispclk_required_for_dram_speed_change[low][s_high], vbios->high_voltage_max_dispclk) && data->num_displays_with_margin[low][s_high] == number_of_displays_enabled_with_margin))) {
			yclk_message = bw_fixed_to_int(vbios->low_yclk);
			data->y_clk_level = low;
			data->dram_bandwidth = bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[low]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_channels));
		}
		else if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_drambw_display_can_use_in_normal_system_operation, 100),yclk[mid]),bw_div(bw_int_to_fixed(vbios->dram_channel_width_in_bits),bw_int_to_fixed(8))),bw_int_to_fixed(vbios->number_of_dram_channels)))
				&& bw_ltn(bw_mul(data->required_dram_bandwidth_gbyte_per_second, bw_int_to_fixed(1000)), bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[mid]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_channels))) && (data->cpup_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[mid][s_high], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[mid][s_high], vbios->high_voltage_max_dispclk))) && (data->cpuc_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[mid][s_high], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[mid][s_high], vbios->high_voltage_max_dispclk) && bw_ltn(data->dispclk_required_for_blackout_recovery[mid][s_high], vbios->high_voltage_max_dispclk))) && (!data->increase_voltage_to_support_mclk_switch || data->nbp_state_change_enable == bw_def_no || (bw_mtn(data->min_dram_speed_change_margin[mid][s_high], bw_int_to_fixed(0)) && bw_ltn(data->min_dram_speed_change_margin[mid][s_high], bw_int_to_fixed(9999)) && bw_leq(data->dispclk_required_for_dram_speed_change[mid][s_high], vbios->high_voltage_max_dispclk) && data->num_displays_with_margin[mid][s_high] == number_of_displays_enabled_with_margin))) {
			yclk_message = bw_fixed_to_int(vbios->mid_yclk);
			data->y_clk_level = mid;
			data->dram_bandwidth = bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[mid]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_channels));
		}
		else if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_drambw_display_can_use_in_normal_system_operation, 100),yclk[high]),bw_div(bw_int_to_fixed(vbios->dram_channel_width_in_bits),bw_int_to_fixed(8))),bw_int_to_fixed(vbios->number_of_dram_channels)))
				&& bw_ltn(bw_mul(data->required_dram_bandwidth_gbyte_per_second, bw_int_to_fixed(1000)), bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[high]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_channels)))) {
			yclk_message = bw_fixed_to_int(vbios->high_yclk);
			data->y_clk_level = high;
			data->dram_bandwidth = bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[high]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_channels));
		}
		else {
			yclk_message = bw_def_exceeded_allowed_maximum_bw;
			data->y_clk_level = high;
			data->dram_bandwidth = bw_mul(bw_div(bw_mul(bw_mul(data->dram_efficiency, yclk[high]), bw_int_to_fixed(vbios->dram_channel_width_in_bits)), bw_int_to_fixed(8)), bw_int_to_fixed(data->number_of_dram_channels));
		}
	}
	/*required sclk*/
	/*sclk requirement only makes sense if the total pte requests fit in the scatter-gather saw queque size*/
	/*if that is the case, the sclk requirement is the maximum of the ones required by dmif and mcifwr, and the high/mid/low sclk is chosen accordingly, unless that choice results in foresaking dram speed/nb p-state change.*/
	/*the dmif and mcifwr sclk required is the one that allows the transfer of all pipe's data buffer size through the sclk bus in the time for data transfer*/
	/*for dmif, pte and cursor requests have to be included.*/
	data->dmif_required_sclk = bw_div(bw_div(data->total_display_reads_required_data, data->display_reads_time_for_data_transfer), (bw_mul(vbios->data_return_bus_width, bw_frc_to_fixed(dceip->percent_of_ideal_port_bw_received_after_urgent_latency, 100))));
	data->mcifwr_required_sclk = bw_div(bw_div(data->total_display_writes_required_data, data->display_writes_time_for_data_transfer), vbios->data_return_bus_width);
	if (bw_mtn(data->scatter_gather_total_pte_requests, dceip->maximum_total_outstanding_pte_requests_allowed_by_saw)) {
		data->required_sclk = bw_int_to_fixed(9999);
		sclk_message = bw_def_exceeded_allowed_outstanding_pte_req_queue_size;
		data->sclk_level = s_high;
	}
	else if (bw_mtn(vbios->dmifmc_urgent_latency, data->required_dmifmc_urgent_latency_for_page_close_open) || bw_mtn(vbios->mcifwrmc_urgent_latency, data->required_mcifmcwr_urgent_latency)) {
		data->required_sclk = bw_int_to_fixed(9999);
		sclk_message = bw_def_exceeded_allowed_page_close_open;
		data->sclk_level = s_high;
	}
	else {
		data->required_sclk = bw_max2(data->dmif_required_sclk, data->mcifwr_required_sclk);
		if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation, 100),sclk[low]),vbios->data_return_bus_width))
				&& bw_ltn(data->required_sclk, sclk[s_low]) && (data->cpup_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_low], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_low], vbios->high_voltage_max_dispclk))) && (data->cpuc_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_low], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_low], vbios->high_voltage_max_dispclk) && bw_ltn(data->dispclk_required_for_blackout_recovery[data->y_clk_level][s_low], vbios->high_voltage_max_dispclk))) && (!data->increase_voltage_to_support_mclk_switch || data->nbp_state_change_enable == bw_def_no || (bw_mtn(data->min_dram_speed_change_margin[data->y_clk_level][s_low], bw_int_to_fixed(0)) && bw_ltn(data->min_dram_speed_change_margin[data->y_clk_level][s_low], bw_int_to_fixed(9999)) && bw_leq(data->dispclk_required_for_dram_speed_change[data->y_clk_level][s_low], vbios->low_voltage_max_dispclk) && data->num_displays_with_margin[data->y_clk_level][s_low] == number_of_displays_enabled_with_margin))) {
			sclk_message = bw_def_low;
			data->sclk_level = s_low;
			data->required_sclk = vbios->low_sclk;
		}
		else if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation, 100),sclk[mid]),vbios->data_return_bus_width))
				&& bw_ltn(data->required_sclk, sclk[s_mid1]) && (data->cpup_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid1], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid1], vbios->high_voltage_max_dispclk))) && (data->cpuc_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid1], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid1], vbios->high_voltage_max_dispclk) && bw_ltn(data->dispclk_required_for_blackout_recovery[data->y_clk_level][s_mid1], vbios->high_voltage_max_dispclk))) && (!data->increase_voltage_to_support_mclk_switch || data->nbp_state_change_enable == bw_def_no || (bw_mtn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid1], bw_int_to_fixed(0)) && bw_ltn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid1], bw_int_to_fixed(9999)) && bw_leq(data->dispclk_required_for_dram_speed_change[data->y_clk_level][s_mid1], vbios->mid_voltage_max_dispclk) && data->num_displays_with_margin[data->y_clk_level][s_mid1] == number_of_displays_enabled_with_margin))) {
			sclk_message = bw_def_mid;
			data->sclk_level = s_mid1;
			data->required_sclk = vbios->mid1_sclk;
		}
		else if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation, 100),sclk[s_mid2]),vbios->data_return_bus_width))
				&& bw_ltn(data->required_sclk, sclk[s_mid2]) && (data->cpup_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid2], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid2], vbios->high_voltage_max_dispclk))) && (data->cpuc_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid2], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid2], vbios->high_voltage_max_dispclk) && bw_ltn(data->dispclk_required_for_blackout_recovery[data->y_clk_level][s_mid2], vbios->high_voltage_max_dispclk))) && (!data->increase_voltage_to_support_mclk_switch || data->nbp_state_change_enable == bw_def_no || (bw_mtn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid2], bw_int_to_fixed(0)) && bw_ltn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid2], bw_int_to_fixed(9999)) && bw_leq(data->dispclk_required_for_dram_speed_change[data->y_clk_level][s_mid2], vbios->mid_voltage_max_dispclk) && data->num_displays_with_margin[data->y_clk_level][s_mid2] == number_of_displays_enabled_with_margin))) {
			sclk_message = bw_def_mid;
			data->sclk_level = s_mid2;
			data->required_sclk = vbios->mid2_sclk;
		}
		else if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation, 100),sclk[s_mid3]),vbios->data_return_bus_width))
				&& bw_ltn(data->required_sclk, sclk[s_mid3]) && (data->cpup_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid3], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid3], vbios->high_voltage_max_dispclk))) && (data->cpuc_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid3], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid3], vbios->high_voltage_max_dispclk) && bw_ltn(data->dispclk_required_for_blackout_recovery[data->y_clk_level][s_mid3], vbios->high_voltage_max_dispclk))) && (!data->increase_voltage_to_support_mclk_switch || data->nbp_state_change_enable == bw_def_no || (bw_mtn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid3], bw_int_to_fixed(0)) && bw_ltn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid3], bw_int_to_fixed(9999)) && bw_leq(data->dispclk_required_for_dram_speed_change[data->y_clk_level][s_mid3], vbios->mid_voltage_max_dispclk) && data->num_displays_with_margin[data->y_clk_level][s_mid3] == number_of_displays_enabled_with_margin))) {
			sclk_message = bw_def_mid;
			data->sclk_level = s_mid3;
			data->required_sclk = vbios->mid3_sclk;
		}
		else if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation, 100),sclk[s_mid4]),vbios->data_return_bus_width))
				&& bw_ltn(data->required_sclk, sclk[s_mid4]) && (data->cpup_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid4], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid4], vbios->high_voltage_max_dispclk))) && (data->cpuc_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid4], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid4], vbios->high_voltage_max_dispclk) && bw_ltn(data->dispclk_required_for_blackout_recovery[data->y_clk_level][s_mid4], vbios->high_voltage_max_dispclk))) && (!data->increase_voltage_to_support_mclk_switch || data->nbp_state_change_enable == bw_def_no || (bw_mtn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid4], bw_int_to_fixed(0)) && bw_ltn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid4], bw_int_to_fixed(9999)) && bw_leq(data->dispclk_required_for_dram_speed_change[data->y_clk_level][s_mid4], vbios->mid_voltage_max_dispclk) && data->num_displays_with_margin[data->y_clk_level][s_mid4] == number_of_displays_enabled_with_margin))) {
			sclk_message = bw_def_mid;
			data->sclk_level = s_mid4;
			data->required_sclk = vbios->mid4_sclk;
		}
		else if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation, 100),sclk[s_mid5]),vbios->data_return_bus_width))
				&& bw_ltn(data->required_sclk, sclk[s_mid5]) && (data->cpup_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid5], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid5], vbios->high_voltage_max_dispclk))) && (data->cpuc_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid5], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid5], vbios->high_voltage_max_dispclk) && bw_ltn(data->dispclk_required_for_blackout_recovery[data->y_clk_level][s_mid5], vbios->high_voltage_max_dispclk))) && (!data->increase_voltage_to_support_mclk_switch || data->nbp_state_change_enable == bw_def_no || (bw_mtn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid5], bw_int_to_fixed(0)) && bw_ltn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid5], bw_int_to_fixed(9999)) && bw_leq(data->dispclk_required_for_dram_speed_change[data->y_clk_level][s_mid5], vbios->mid_voltage_max_dispclk) && data->num_displays_with_margin[data->y_clk_level][s_mid5] == number_of_displays_enabled_with_margin))) {
			sclk_message = bw_def_mid;
			data->sclk_level = s_mid5;
			data->required_sclk = vbios->mid5_sclk;
		}
		else if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation, 100),sclk[s_mid6]),vbios->data_return_bus_width))
				&& bw_ltn(data->required_sclk, sclk[s_mid6]) && (data->cpup_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid6], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid6], vbios->high_voltage_max_dispclk))) && (data->cpuc_state_change_enable == bw_def_no || (bw_mtn(data->blackout_duration_margin[data->y_clk_level][s_mid6], bw_int_to_fixed(0)) && bw_ltn(data->dispclk_required_for_blackout_duration[data->y_clk_level][s_mid6], vbios->high_voltage_max_dispclk) && bw_ltn(data->dispclk_required_for_blackout_recovery[data->y_clk_level][s_mid6], vbios->high_voltage_max_dispclk))) && (!data->increase_voltage_to_support_mclk_switch || data->nbp_state_change_enable == bw_def_no || (bw_mtn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid6], bw_int_to_fixed(0)) && bw_ltn(data->min_dram_speed_change_margin[data->y_clk_level][s_mid6], bw_int_to_fixed(9999)) && bw_leq(data->dispclk_required_for_dram_speed_change[data->y_clk_level][s_mid6], vbios->high_voltage_max_dispclk) && data->num_displays_with_margin[data->y_clk_level][s_mid6] == number_of_displays_enabled_with_margin))) {
			sclk_message = bw_def_mid;
			data->sclk_level = s_mid6;
			data->required_sclk = vbios->mid6_sclk;
		}
		else if (bw_ltn(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation, 100),sclk[s_high]),vbios->data_return_bus_width))
				&& bw_ltn(data->required_sclk, sclk[s_high])) {
			sclk_message = bw_def_high;
			data->sclk_level = s_high;
			data->required_sclk = vbios->high_sclk;
		}
		else if (bw_meq(data->total_average_bandwidth_no_compression, bw_mul(bw_mul(bw_frc_to_fixed(dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation, 100),sclk[s_high]),vbios->data_return_bus_width))
				&& bw_ltn(data->required_sclk, sclk[s_high])) {
			sclk_message = bw_def_high;
			data->sclk_level = s_high;
			data->required_sclk = vbios->high_sclk;
		}
		else {
			sclk_message = bw_def_exceeded_allowed_maximum_sclk;
			data->sclk_level = s_high;
			/*required_sclk = high_sclk*/
		}
	}
	/*dispclk*/
	/*if dispclk is set to the maximum, ramping is not required.  dispclk required without ramping is less than the dispclk required with ramping.*/
	/*if dispclk required without ramping is more than the maximum dispclk, that is the dispclk required, and the mode is not supported*/
	/*if that does not happen, but dispclk required with ramping is more than the maximum dispclk, dispclk required is just the maximum dispclk*/
	/*if that does not happen either, dispclk required is the dispclk required with ramping.*/
	/*dispclk required without ramping is the maximum of the one required for display pipe pixel throughput, for scaler throughput, for total read request thrrougput and for dram/np p-state change if enabled.*/
	/*the display pipe pixel throughput is the maximum of lines in per line out in the beginning of the frame and lines in per line out in the middle of the frame multiplied by the horizontal blank and chunk granularity factor, altogether multiplied by the ratio of the source width to the line time, divided by the line buffer pixels per dispclk throughput, and multiplied by the display pipe throughput factor.*/
	/*the horizontal blank and chunk granularity factor is the ratio of the line time divided by the line time minus half the horizontal blank and chunk time.  it applies when the lines in per line out is not 2 or 4.*/
	/*the dispclk required for scaler throughput is the product of the pixel rate and the scaling limits factor.*/
	/*the dispclk required for total read request throughput is the product of the peak request-per-second bandwidth and the dispclk cycles per request, divided by the request efficiency.*/
	/*for the dispclk required with ramping, instead of multiplying just the pipe throughput by the display pipe throughput factor, we multiply the scaler and pipe throughput by the ramping factor.*/
	/*the scaling limits factor is the product of the horizontal scale ratio, and the ratio of the vertical taps divided by the scaler efficiency clamped to at least 1.*/
	/*the scaling limits factor itself it also clamped to at least 1*/
	/*if doing downscaling with the pre-downscaler enabled, the horizontal scale ratio should not be considered above (use "1")*/
	data->downspread_factor = bw_add(bw_int_to_fixed(1), bw_div(vbios->down_spread_percentage, bw_int_to_fixed(100)));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (surface_type[i] == bw_def_graphics) {
				switch (data->lb_bpc[i]) {
				case 6:
					data->v_scaler_efficiency = dceip->graphics_vscaler_efficiency6_bit_per_component;
					break;
				case 8:
					data->v_scaler_efficiency = dceip->graphics_vscaler_efficiency8_bit_per_component;
					break;
				case 10:
					data->v_scaler_efficiency = dceip->graphics_vscaler_efficiency10_bit_per_component;
					break;
				default:
					data->v_scaler_efficiency = dceip->graphics_vscaler_efficiency12_bit_per_component;
					break;
				}
				if (data->use_alpha[i] == 1) {
					data->v_scaler_efficiency = bw_min2(data->v_scaler_efficiency, dceip->alpha_vscaler_efficiency);
				}
			}
			else {
				switch (data->lb_bpc[i]) {
				case 6:
					data->v_scaler_efficiency = dceip->underlay_vscaler_efficiency6_bit_per_component;
					break;
				case 8:
					data->v_scaler_efficiency = dceip->underlay_vscaler_efficiency8_bit_per_component;
					break;
				case 10:
					data->v_scaler_efficiency = dceip->underlay_vscaler_efficiency10_bit_per_component;
					break;
				default:
					data->v_scaler_efficiency = dceip->underlay_vscaler_efficiency12_bit_per_component;
					break;
				}
			}
			if (dceip->pre_downscaler_enabled && bw_mtn(data->hsr[i], bw_int_to_fixed(1))) {
				data->scaler_limits_factor = bw_max2(bw_div(data->v_taps[i], data->v_scaler_efficiency), bw_div(data->source_width_rounded_up_to_chunks[i], data->h_total[i]));
			}
			else {
				data->scaler_limits_factor = bw_max3(bw_int_to_fixed(1), bw_ceil2(bw_div(data->h_taps[i], bw_int_to_fixed(4)), bw_int_to_fixed(1)), bw_mul(data->hsr[i], bw_max2(bw_div(data->v_taps[i], data->v_scaler_efficiency), bw_int_to_fixed(1))));
			}
			data->display_pipe_pixel_throughput = bw_div(bw_div(bw_mul(bw_max2(data->lb_lines_in_per_line_out_in_beginning_of_frame[i], bw_mul(data->lb_lines_in_per_line_out_in_middle_of_frame[i], data->horizontal_blank_and_chunk_granularity_factor[i])), data->source_width_rounded_up_to_chunks[i]), (bw_div(data->h_total[i], data->pixel_rate[i]))), dceip->lb_write_pixels_per_dispclk);
			data->dispclk_required_without_ramping[i] = bw_mul(data->downspread_factor, bw_max2(bw_mul(data->pixel_rate[i], data->scaler_limits_factor), bw_mul(dceip->display_pipe_throughput_factor, data->display_pipe_pixel_throughput)));
			data->dispclk_required_with_ramping[i] = bw_mul(dceip->dispclk_ramping_factor, bw_max2(bw_mul(data->pixel_rate[i], data->scaler_limits_factor), data->display_pipe_pixel_throughput));
		}
	}
	data->total_dispclk_required_with_ramping = bw_int_to_fixed(0);
	data->total_dispclk_required_without_ramping = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (bw_ltn(data->total_dispclk_required_with_ramping, data->dispclk_required_with_ramping[i])) {
				data->total_dispclk_required_with_ramping = data->dispclk_required_with_ramping[i];
			}
			if (bw_ltn(data->total_dispclk_required_without_ramping, data->dispclk_required_without_ramping[i])) {
				data->total_dispclk_required_without_ramping = data->dispclk_required_without_ramping[i];
			}
		}
	}
	data->total_read_request_bandwidth = bw_int_to_fixed(0);
	data->total_write_request_bandwidth = bw_int_to_fixed(0);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (surface_type[i] != bw_def_display_write_back420_luma && surface_type[i] != bw_def_display_write_back420_chroma) {
				data->total_read_request_bandwidth = bw_add(data->total_read_request_bandwidth, data->request_bandwidth[i]);
			}
			else {
				data->total_write_request_bandwidth = bw_add(data->total_write_request_bandwidth, data->request_bandwidth[i]);
			}
		}
	}
	data->dispclk_required_for_total_read_request_bandwidth = bw_div(bw_mul(data->total_read_request_bandwidth, dceip->dispclk_per_request), dceip->request_efficiency);
	data->total_dispclk_required_with_ramping_with_request_bandwidth = bw_max2(data->total_dispclk_required_with_ramping, data->dispclk_required_for_total_read_request_bandwidth);
	data->total_dispclk_required_without_ramping_with_request_bandwidth = bw_max2(data->total_dispclk_required_without_ramping, data->dispclk_required_for_total_read_request_bandwidth);
	if (data->cpuc_state_change_enable == bw_def_yes) {
		data->total_dispclk_required_with_ramping_with_request_bandwidth = bw_max3(data->total_dispclk_required_with_ramping_with_request_bandwidth, data->dispclk_required_for_blackout_duration[data->y_clk_level][data->sclk_level], data->dispclk_required_for_blackout_recovery[data->y_clk_level][data->sclk_level]);
		data->total_dispclk_required_without_ramping_with_request_bandwidth = bw_max3(data->total_dispclk_required_without_ramping_with_request_bandwidth, data->dispclk_required_for_blackout_duration[data->y_clk_level][data->sclk_level], data->dispclk_required_for_blackout_recovery[data->y_clk_level][data->sclk_level]);
	}
	if (data->cpup_state_change_enable == bw_def_yes) {
		data->total_dispclk_required_with_ramping_with_request_bandwidth = bw_max2(data->total_dispclk_required_with_ramping_with_request_bandwidth, data->dispclk_required_for_blackout_duration[data->y_clk_level][data->sclk_level]);
		data->total_dispclk_required_without_ramping_with_request_bandwidth = bw_max2(data->total_dispclk_required_without_ramping_with_request_bandwidth, data->dispclk_required_for_blackout_duration[data->y_clk_level][data->sclk_level]);
	}
	if (data->nbp_state_change_enable == bw_def_yes && data->increase_voltage_to_support_mclk_switch) {
		data->total_dispclk_required_with_ramping_with_request_bandwidth = bw_max2(data->total_dispclk_required_with_ramping_with_request_bandwidth, data->dispclk_required_for_dram_speed_change[data->y_clk_level][data->sclk_level]);
		data->total_dispclk_required_without_ramping_with_request_bandwidth = bw_max2(data->total_dispclk_required_without_ramping_with_request_bandwidth, data->dispclk_required_for_dram_speed_change[data->y_clk_level][data->sclk_level]);
	}
	if (bw_ltn(data->total_dispclk_required_with_ramping_with_request_bandwidth, vbios->high_voltage_max_dispclk)) {
		data->dispclk = data->total_dispclk_required_with_ramping_with_request_bandwidth;
	}
	else if (bw_ltn(data->total_dispclk_required_without_ramping_with_request_bandwidth, vbios->high_voltage_max_dispclk)) {
		data->dispclk = vbios->high_voltage_max_dispclk;
	}
	else {
		data->dispclk = data->total_dispclk_required_without_ramping_with_request_bandwidth;
	}
	/* required core voltage*/
	/* the core voltage required is low if sclk, yclk(pclk)and dispclk are within the low limits*/
	/* otherwise, the core voltage required is medium if yclk (pclk) is within the low limit and sclk and dispclk are within the medium limit*/
	/* otherwise, the core voltage required is high if the three clocks are within the high limits*/
	/* otherwise, or if the mode is not supported, core voltage requirement is not applicable*/
	if (pipe_check == bw_def_notok) {
		voltage = bw_def_na;
	}
	else if (mode_check == bw_def_notok) {
		voltage = bw_def_notok;
	}
	else if (bw_equ(bw_int_to_fixed(yclk_message), vbios->low_yclk) && sclk_message == bw_def_low && bw_ltn(data->dispclk, vbios->low_voltage_max_dispclk)) {
		voltage = bw_def_0_72;
	}
	else if ((bw_equ(bw_int_to_fixed(yclk_message), vbios->low_yclk) || bw_equ(bw_int_to_fixed(yclk_message), vbios->mid_yclk)) && (sclk_message == bw_def_low || sclk_message == bw_def_mid) && bw_ltn(data->dispclk, vbios->mid_voltage_max_dispclk)) {
		voltage = bw_def_0_8;
	}
	else if ((bw_equ(bw_int_to_fixed(yclk_message), vbios->low_yclk) || bw_equ(bw_int_to_fixed(yclk_message), vbios->mid_yclk) || bw_equ(bw_int_to_fixed(yclk_message), vbios->high_yclk)) && (sclk_message == bw_def_low || sclk_message == bw_def_mid || sclk_message == bw_def_high) && bw_leq(data->dispclk, vbios->high_voltage_max_dispclk)) {
		if ((data->nbp_state_change_enable == bw_def_no && nbp_state_change_enable_blank == bw_def_no)) {
			voltage = bw_def_high_no_nbp_state_change;
		}
		else {
			voltage = bw_def_0_9;
		}
	}
	else {
		voltage = bw_def_notok;
	}
	if (voltage == bw_def_0_72) {
		data->max_phyclk = vbios->low_voltage_max_phyclk;
	}
	else if (voltage == bw_def_0_8) {
		data->max_phyclk = vbios->mid_voltage_max_phyclk;
	}
	else {
		data->max_phyclk = vbios->high_voltage_max_phyclk;
	}
	/*required blackout recovery time*/
	data->blackout_recovery_time = bw_int_to_fixed(0);
	for (k = 0; k <= maximum_number_of_surfaces - 1; k++) {
		if (data->enable[k] && bw_mtn(vbios->blackout_duration, bw_int_to_fixed(0)) && data->cpup_state_change_enable == bw_def_yes) {
			if (surface_type[k] != bw_def_display_write_back420_luma && surface_type[k] != bw_def_display_write_back420_chroma) {
				data->blackout_recovery_time = bw_max2(data->blackout_recovery_time, bw_add(bw_mul(bw_int_to_fixed(2), data->total_dmifmc_urgent_latency), data->dmif_burst_time[data->y_clk_level][data->sclk_level]));
				if (bw_ltn(data->adjusted_data_buffer_size[k], bw_mul(bw_div(bw_mul(data->display_bandwidth[k], data->useful_bytes_per_request[k]), data->bytes_per_request[k]), (bw_add(vbios->blackout_duration, bw_add(bw_mul(bw_int_to_fixed(2), data->total_dmifmc_urgent_latency), data->dmif_burst_time[data->y_clk_level][data->sclk_level])))))) {
					data->blackout_recovery_time = bw_max2(data->blackout_recovery_time, bw_div((bw_add(bw_mul(bw_div(bw_mul(data->display_bandwidth[k], data->useful_bytes_per_request[k]), data->bytes_per_request[k]), vbios->blackout_duration), bw_sub(bw_div(bw_mul(bw_mul(bw_mul((bw_add(bw_mul(bw_int_to_fixed(2), data->total_dmifmc_urgent_latency), data->dmif_burst_time[data->y_clk_level][data->sclk_level])), data->dispclk), bw_int_to_fixed(data->bytes_per_pixel[k])), data->lines_interleaved_in_mem_access[k]), data->latency_hiding_lines[k]), data->adjusted_data_buffer_size[k]))), (bw_sub(bw_div(bw_mul(bw_mul(data->dispclk, bw_int_to_fixed(data->bytes_per_pixel[k])), data->lines_interleaved_in_mem_access[k]), data->latency_hiding_lines[k]), bw_div(bw_mul(data->display_bandwidth[k], data->useful_bytes_per_request[k]), data->bytes_per_request[k])))));
				}
			}
			else {
				data->blackout_recovery_time = bw_max2(data->blackout_recovery_time, bw_add(bw_mul(bw_int_to_fixed(2), vbios->mcifwrmc_urgent_latency), data->mcifwr_burst_time[data->y_clk_level][data->sclk_level]));
				if (bw_ltn(data->adjusted_data_buffer_size[k], bw_mul(bw_div(bw_mul(data->display_bandwidth[k], data->useful_bytes_per_request[k]), data->bytes_per_request[k]), (bw_add(vbios->blackout_duration, bw_add(bw_mul(bw_int_to_fixed(2), vbios->mcifwrmc_urgent_latency), data->mcifwr_burst_time[data->y_clk_level][data->sclk_level])))))) {
					data->blackout_recovery_time = bw_max2(data->blackout_recovery_time, bw_div((bw_add(bw_mul(bw_div(bw_mul(data->display_bandwidth[k], data->useful_bytes_per_request[k]), data->bytes_per_request[k]), vbios->blackout_duration), bw_sub(bw_div(bw_mul(bw_mul(bw_mul((bw_add(bw_add(bw_mul(bw_int_to_fixed(2), vbios->mcifwrmc_urgent_latency), data->dmif_burst_time[data->y_clk_level][data->sclk_level]), data->mcifwr_burst_time[data->y_clk_level][data->sclk_level])), data->dispclk), bw_int_to_fixed(data->bytes_per_pixel[k])), data->lines_interleaved_in_mem_access[k]), data->latency_hiding_lines[k]), data->adjusted_data_buffer_size[k]))), (bw_sub(bw_div(bw_mul(bw_mul(data->dispclk, bw_int_to_fixed(data->bytes_per_pixel[k])), data->lines_interleaved_in_mem_access[k]), data->latency_hiding_lines[k]), bw_div(bw_mul(data->display_bandwidth[k], data->useful_bytes_per_request[k]), data->bytes_per_request[k])))));
				}
			}
		}
	}
	/*sclk deep sleep*/
	/*during self-refresh, sclk can be reduced to dispclk divided by the minimum pixels in the data fifo entry, with 15% margin, but shoudl not be set to less than the request bandwidth.*/
	/*the data fifo entry is 16 pixels for the writeback, 64 bytes/bytes_per_pixel for the graphics, 16 pixels for the parallel rotation underlay,*/
	/*and 16 bytes/bytes_per_pixel for the orthogonal rotation underlay.*/
	/*in parallel mode (underlay pipe), the data read from the dmifv buffer is variable and based on the pixel depth (8bbp - 16 bytes, 16 bpp - 32 bytes, 32 bpp - 64 bytes)*/
	/*in orthogonal mode (underlay pipe), the data read from the dmifv buffer is fixed at 16 bytes.*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (surface_type[i] == bw_def_display_write_back420_luma || surface_type[i] == bw_def_display_write_back420_chroma) {
				data->pixels_per_data_fifo_entry[i] = bw_int_to_fixed(16);
			}
			else if (surface_type[i] == bw_def_graphics) {
				data->pixels_per_data_fifo_entry[i] = bw_div(bw_int_to_fixed(64), bw_int_to_fixed(data->bytes_per_pixel[i]));
			}
			else if (data->orthogonal_rotation[i] == 0) {
				data->pixels_per_data_fifo_entry[i] = bw_int_to_fixed(16);
			}
			else {
				data->pixels_per_data_fifo_entry[i] = bw_div(bw_int_to_fixed(16), bw_int_to_fixed(data->bytes_per_pixel[i]));
			}
		}
	}
	data->min_pixels_per_data_fifo_entry = bw_int_to_fixed(9999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (bw_mtn(data->min_pixels_per_data_fifo_entry, data->pixels_per_data_fifo_entry[i])) {
				data->min_pixels_per_data_fifo_entry = data->pixels_per_data_fifo_entry[i];
			}
		}
	}
	data->sclk_deep_sleep = bw_max2(bw_div(bw_mul(data->dispclk, bw_frc_to_fixed(115, 100)), data->min_pixels_per_data_fifo_entry), data->total_read_request_bandwidth);
	/*urgent, stutter and nb-p_state watermark*/
	/*the urgent watermark is the maximum of the urgent trip time plus the pixel transfer time, the urgent trip times to get data for the first pixel, and the urgent trip times to get data for the last pixel.*/
	/*the stutter exit watermark is the self refresh exit time plus the maximum of the data burst time plus the pixel transfer time, the data burst times to get data for the first pixel, and the data burst times to get data for the last pixel.  it does not apply to the writeback.*/
	/*the nb p-state change watermark is the dram speed/p-state change time plus the maximum of the data burst time plus the pixel transfer time, the data burst times to get data for the first pixel, and the data burst times to get data for the last pixel.*/
	/*the pixel transfer time is the maximum of the time to transfer the source pixels required for the first output pixel, and the time to transfer the pixels for the last output pixel minus the active line time.*/
	/*blackout_duration is added to the urgent watermark*/
	data->chunk_request_time = bw_int_to_fixed(0);
	data->cursor_request_time = bw_int_to_fixed(0);
	/*compute total time to request one chunk from each active display pipe*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			data->chunk_request_time = bw_add(data->chunk_request_time, (bw_div((bw_div(bw_int_to_fixed(pixels_per_chunk * data->bytes_per_pixel[i]), data->useful_bytes_per_request[i])), bw_min2(sclk[data->sclk_level], bw_div(data->dispclk, bw_int_to_fixed(2))))));
		}
	}
	/*compute total time to request cursor data*/
	data->cursor_request_time = (bw_div(data->cursor_total_data, (bw_mul(bw_int_to_fixed(32), sclk[data->sclk_level]))));
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			data->line_source_pixels_transfer_time = bw_max2(bw_div(bw_div(data->src_pixels_for_first_output_pixel[i], dceip->lb_write_pixels_per_dispclk), (bw_div(data->dispclk, dceip->display_pipe_throughput_factor))), bw_sub(bw_div(bw_div(data->src_pixels_for_last_output_pixel[i], dceip->lb_write_pixels_per_dispclk), (bw_div(data->dispclk, dceip->display_pipe_throughput_factor))), data->active_time[i]));
			if (surface_type[i] != bw_def_display_write_back420_luma && surface_type[i] != bw_def_display_write_back420_chroma) {
				data->urgent_watermark[i] = bw_add(bw_add(bw_add(bw_add(bw_add(data->total_dmifmc_urgent_latency, data->dmif_burst_time[data->y_clk_level][data->sclk_level]), bw_max2(data->line_source_pixels_transfer_time, data->line_source_transfer_time[i][data->y_clk_level][data->sclk_level])), vbios->blackout_duration), data->chunk_request_time), data->cursor_request_time);
				data->stutter_exit_watermark[i] = bw_add(bw_sub(vbios->stutter_self_refresh_exit_latency, data->total_dmifmc_urgent_latency), data->urgent_watermark[i]);
				data->stutter_entry_watermark[i] = bw_add(bw_sub(bw_add(vbios->stutter_self_refresh_exit_latency, vbios->stutter_self_refresh_entry_latency), data->total_dmifmc_urgent_latency), data->urgent_watermark[i]);
				/*unconditionally remove black out time from the nb p_state watermark*/
				if (data->display_pstate_change_enable[i] == 1) {
					data->nbp_state_change_watermark[i] = bw_add(bw_add(vbios->nbp_state_change_latency, data->dmif_burst_time[data->y_clk_level][data->sclk_level]), bw_max2(data->line_source_pixels_transfer_time, data->dram_speed_change_line_source_transfer_time[i][data->y_clk_level][data->sclk_level]));
				}
				else {
					/*maximize the watermark to force the switch in the vb_lank region of the frame*/
					data->nbp_state_change_watermark[i] = bw_int_to_fixed(131000);
				}
			}
			else {
				data->urgent_watermark[i] = bw_add(bw_add(bw_add(bw_add(bw_add(vbios->mcifwrmc_urgent_latency, data->mcifwr_burst_time[data->y_clk_level][data->sclk_level]), bw_max2(data->line_source_pixels_transfer_time, data->line_source_transfer_time[i][data->y_clk_level][data->sclk_level])), vbios->blackout_duration), data->chunk_request_time), data->cursor_request_time);
				data->stutter_exit_watermark[i] = bw_int_to_fixed(0);
				data->stutter_entry_watermark[i] = bw_int_to_fixed(0);
				if (data->display_pstate_change_enable[i] == 1) {
					data->nbp_state_change_watermark[i] = bw_add(bw_add(vbios->nbp_state_change_latency, data->mcifwr_burst_time[data->y_clk_level][data->sclk_level]), bw_max2(data->line_source_pixels_transfer_time, data->dram_speed_change_line_source_transfer_time[i][data->y_clk_level][data->sclk_level]));
				}
				else {
					/*maximize the watermark to force the switch in the vb_lank region of the frame*/
					data->nbp_state_change_watermark[i] = bw_int_to_fixed(131000);
				}
			}
		}
	}
	/*stutter mode enable*/
	/*in the multi-display case the stutter exit or entry watermark cannot exceed the minimum latency hiding capabilities of the*/
	/*display pipe.*/
	data->stutter_mode_enable = data->cpuc_state_change_enable;
	if (data->number_of_displays > 1) {
		for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
			if (data->enable[i]) {
				if ((bw_mtn(data->stutter_exit_watermark[i], data->minimum_latency_hiding[i]) || bw_mtn(data->stutter_entry_watermark[i], data->minimum_latency_hiding[i]))) {
					data->stutter_mode_enable = bw_def_no;
				}
			}
		}
	}
	/*performance metrics*/
	/* display read access efficiency (%)*/
	/* display write back access efficiency (%)*/
	/* stutter efficiency (%)*/
	/* extra underlay pitch recommended for efficiency (pixels)*/
	/* immediate flip time (us)*/
	/* latency for other clients due to urgent display read (us)*/
	/* latency for other clients due to urgent display write (us)*/
	/* average bandwidth consumed by display (no compression) (gb/s)*/
	/* required dram  bandwidth (gb/s)*/
	/* required sclk (m_hz)*/
	/* required rd urgent latency (us)*/
	/* nb p-state change margin (us)*/
	/*dmif and mcifwr dram access efficiency*/
	/*is the ratio between the ideal dram access time (which is the data buffer size in memory divided by the dram bandwidth), and the actual time which is the total page close-open time.  but it cannot exceed the dram efficiency provided by the memory subsystem*/
	data->dmifdram_access_efficiency = bw_min2(bw_div(bw_div(data->total_display_reads_required_dram_access_data, data->dram_bandwidth), data->dmif_total_page_close_open_time), bw_int_to_fixed(1));
	if (bw_mtn(data->total_display_writes_required_dram_access_data, bw_int_to_fixed(0))) {
		data->mcifwrdram_access_efficiency = bw_min2(bw_div(bw_div(data->total_display_writes_required_dram_access_data, data->dram_bandwidth), data->mcifwr_total_page_close_open_time), bw_int_to_fixed(1));
	}
	else {
		data->mcifwrdram_access_efficiency = bw_int_to_fixed(0);
	}
	/*stutter efficiency*/
	/*the stutter efficiency is the frame-average time in self-refresh divided by the frame-average stutter cycle duration.  only applies if the display write-back is not enabled.*/
	/*the frame-average stutter cycle used is the minimum for all pipes of the frame-average data buffer size in time, times the compression rate*/
	/*the frame-average time in self-refresh is the stutter cycle minus the self refresh exit latency and the burst time*/
	/*the stutter cycle is the dmif buffer size reduced by the excess of the stutter exit watermark over the lb size in time.*/
	/*the burst time is the data needed during the stutter cycle divided by the available bandwidth*/
	/*compute the time read all the data from the dmif buffer to the lb (dram refresh period)*/
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			data->stutter_refresh_duration[i] = bw_sub(bw_mul(bw_div(bw_div(bw_mul(bw_div(bw_div(data->adjusted_data_buffer_size[i], bw_int_to_fixed(data->bytes_per_pixel[i])), data->source_width_rounded_up_to_chunks[i]), data->h_total[i]), data->vsr[i]), data->pixel_rate[i]), data->compression_rate[i]), bw_max2(bw_int_to_fixed(0), bw_sub(data->stutter_exit_watermark[i], bw_div(bw_mul((bw_sub(data->lb_partitions[i], bw_int_to_fixed(1))), data->h_total[i]), data->pixel_rate[i]))));
			data->stutter_dmif_buffer_size[i] = bw_div(bw_mul(bw_mul(bw_div(bw_mul(bw_mul(data->stutter_refresh_duration[i], bw_int_to_fixed(data->bytes_per_pixel[i])), data->source_width_rounded_up_to_chunks[i]), data->h_total[i]), data->vsr[i]), data->pixel_rate[i]), data->compression_rate[i]);
		}
	}
	data->min_stutter_refresh_duration = bw_int_to_fixed(9999);
	data->total_stutter_dmif_buffer_size = 0;
	data->total_bytes_requested = 0;
	data->min_stutter_dmif_buffer_size = 9999;
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			if (bw_mtn(data->min_stutter_refresh_duration, data->stutter_refresh_duration[i])) {
				data->min_stutter_refresh_duration = data->stutter_refresh_duration[i];
				data->total_bytes_requested = bw_fixed_to_int(bw_add(bw_int_to_fixed(data->total_bytes_requested), (bw_mul(bw_mul(data->source_height_rounded_up_to_chunks[i], data->source_width_rounded_up_to_chunks[i]), bw_int_to_fixed(data->bytes_per_pixel[i])))));
				data->min_stutter_dmif_buffer_size = bw_fixed_to_int(data->stutter_dmif_buffer_size[i]);
			}
			data->total_stutter_dmif_buffer_size = bw_fixed_to_int(bw_add(data->stutter_dmif_buffer_size[i], bw_int_to_fixed(data->total_stutter_dmif_buffer_size)));
		}
	}
	data->stutter_burst_time = bw_div(bw_int_to_fixed(data->total_stutter_dmif_buffer_size), bw_mul(sclk[data->sclk_level], vbios->data_return_bus_width));
	data->num_stutter_bursts = data->total_bytes_requested / data->min_stutter_dmif_buffer_size;
	data->total_stutter_cycle_duration = bw_add(bw_add(data->min_stutter_refresh_duration, vbios->stutter_self_refresh_exit_latency), data->stutter_burst_time);
	data->time_in_self_refresh = data->min_stutter_refresh_duration;
	if (data->d1_display_write_back_dwb_enable == 1) {
		data->stutter_efficiency = bw_int_to_fixed(0);
	}
	else if (bw_ltn(data->time_in_self_refresh, bw_int_to_fixed(0))) {
		data->stutter_efficiency = bw_int_to_fixed(0);
	}
	else {
		/*compute stutter efficiency assuming 60 hz refresh rate*/
		data->stutter_efficiency = bw_max2(bw_int_to_fixed(0), bw_mul((bw_sub(bw_int_to_fixed(1), (bw_div(bw_mul((bw_add(vbios->stutter_self_refresh_exit_latency, data->stutter_burst_time)), bw_int_to_fixed(data->num_stutter_bursts)), bw_frc_to_fixed(166666667, 10000))))), bw_int_to_fixed(100)));
	}
	/*immediate flip time*/
	/*if scatter gather is enabled, the immediate flip takes a number of urgent memory trips equivalent to the pte requests in a row divided by the pte request limit.*/
	/*otherwise, it may take just one urgenr memory trip*/
	data->worst_number_of_trips_to_memory = bw_int_to_fixed(1);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i] && data->scatter_gather_enable_for_pipe[i] == 1) {
			data->number_of_trips_to_memory_for_getting_apte_row[i] = bw_ceil2(bw_div(data->scatter_gather_pte_requests_in_row[i], data->scatter_gather_pte_request_limit[i]), bw_int_to_fixed(1));
			if (bw_ltn(data->worst_number_of_trips_to_memory, data->number_of_trips_to_memory_for_getting_apte_row[i])) {
				data->worst_number_of_trips_to_memory = data->number_of_trips_to_memory_for_getting_apte_row[i];
			}
		}
	}
	data->immediate_flip_time = bw_mul(data->worst_number_of_trips_to_memory, data->total_dmifmc_urgent_latency);
	/*worst latency for other clients*/
	/*it is the urgent latency plus the urgent burst time*/
	data->latency_for_non_dmif_clients = bw_add(data->total_dmifmc_urgent_latency, data->dmif_burst_time[data->y_clk_level][data->sclk_level]);
	if (data->d1_display_write_back_dwb_enable == 1) {
		data->latency_for_non_mcifwr_clients = bw_add(vbios->mcifwrmc_urgent_latency, dceip->mcifwr_all_surfaces_burst_time);
	}
	else {
		data->latency_for_non_mcifwr_clients = bw_int_to_fixed(0);
	}
	/*dmif mc urgent latency supported in high sclk and yclk*/
	data->dmifmc_urgent_latency_supported_in_high_sclk_and_yclk = bw_div((bw_sub(data->min_read_buffer_size_in_time, data->dmif_burst_time[high][s_high])), data->total_dmifmc_urgent_trips);
	/*dram speed/p-state change margin*/
	/*in the multi-display case the nb p-state change watermark cannot exceed the average lb size plus the dmif size or the cursor dcp buffer size*/
	data->v_blank_nbp_state_dram_speed_change_latency_supported = bw_int_to_fixed(99999);
	data->nbp_state_dram_speed_change_latency_supported = bw_int_to_fixed(99999);
	for (i = 0; i <= maximum_number_of_surfaces - 1; i++) {
		if (data->enable[i]) {
			data->nbp_state_dram_speed_change_latency_supported = bw_min2(data->nbp_state_dram_speed_change_latency_supported, bw_add(bw_sub(data->maximum_latency_hiding_with_cursor[i], data->nbp_state_change_watermark[i]), vbios->nbp_state_change_latency));
			data->v_blank_nbp_state_dram_speed_change_latency_supported = bw_min2(data->v_blank_nbp_state_dram_speed_change_latency_supported, bw_add(bw_sub(bw_div(bw_mul((bw_sub(data->v_total[i], bw_sub(bw_div(data->src_height[i], data->v_scale_ratio[i]), bw_int_to_fixed(4)))), data->h_total[i]), data->pixel_rate[i]), data->nbp_state_change_watermark[i]), vbios->nbp_state_change_latency));
		}
	}
	/*sclk required vs urgent latency*/
	for (i = 1; i <= 5; i++) {
		data->display_reads_time_for_data_transfer_and_urgent_latency = bw_sub(data->min_read_buffer_size_in_time, bw_mul(data->total_dmifmc_urgent_trips, bw_int_to_fixed(i)));
		if (pipe_check == bw_def_ok && (bw_mtn(data->display_reads_time_for_data_transfer_and_urgent_latency, data->dmif_total_page_close_open_time))) {
			data->dmif_required_sclk_for_urgent_latency[i] = bw_div(bw_div(data->total_display_reads_required_data, data->display_reads_time_for_data_transfer_and_urgent_latency), (bw_mul(vbios->data_return_bus_width, bw_frc_to_fixed(dceip->percent_of_ideal_port_bw_received_after_urgent_latency, 100))));
		}
		else {
			data->dmif_required_sclk_for_urgent_latency[i] = bw_int_to_fixed(bw_def_na);
		}
	}
	/*output link bit per pixel supported*/
	for (k = 0; k <= maximum_number_of_surfaces - 1; k++) {
		data->output_bpphdmi[k] = bw_def_na;
		data->output_bppdp4_lane_hbr[k] = bw_def_na;
		data->output_bppdp4_lane_hbr2[k] = bw_def_na;
		data->output_bppdp4_lane_hbr3[k] = bw_def_na;
		if (data->enable[k]) {
			data->output_bpphdmi[k] = bw_fixed_to_int(bw_mul(bw_div(bw_min2(bw_int_to_fixed(600), data->max_phyclk), data->pixel_rate[k]), bw_int_to_fixed(24)));
			if (bw_meq(data->max_phyclk, bw_int_to_fixed(270))) {
				data->output_bppdp4_lane_hbr[k] = bw_fixed_to_int(bw_mul(bw_div(bw_mul(bw_int_to_fixed(270), bw_int_to_fixed(4)), data->pixel_rate[k]), bw_int_to_fixed(8)));
			}
			if (bw_meq(data->max_phyclk, bw_int_to_fixed(540))) {
				data->output_bppdp4_lane_hbr2[k] = bw_fixed_to_int(bw_mul(bw_div(bw_mul(bw_int_to_fixed(540), bw_int_to_fixed(4)), data->pixel_rate[k]), bw_int_to_fixed(8)));
			}
			if (bw_meq(data->max_phyclk, bw_int_to_fixed(810))) {
				data->output_bppdp4_lane_hbr3[k] = bw_fixed_to_int(bw_mul(bw_div(bw_mul(bw_int_to_fixed(810), bw_int_to_fixed(4)), data->pixel_rate[k]), bw_int_to_fixed(8)));
			}
		}
	}

	kfree(surface_type);
free_tiling_mode:
	kfree(tiling_mode);
free_yclk:
	kfree(yclk);
free_sclk:
	kfree(sclk);
}

/*******************************************************************************
 * Public functions
 ******************************************************************************/
void bw_calcs_init(struct bw_calcs_dceip *bw_dceip,
	struct bw_calcs_vbios *bw_vbios,
	struct hw_asic_id asic_id)
{
	struct bw_calcs_dceip *dceip;
	struct bw_calcs_vbios *vbios;

	enum bw_calcs_version version = bw_calcs_version_from_asic_id(asic_id);

	dceip = kzalloc(sizeof(*dceip), GFP_KERNEL);
	if (!dceip)
		return;

	vbios = kzalloc(sizeof(*vbios), GFP_KERNEL);
	if (!vbios) {
		kfree(dceip);
		return;
	}

	dceip->version = version;

	switch (version) {
	case BW_CALCS_VERSION_CARRIZO:
		vbios->memory_type = bw_def_gddr5;
		vbios->dram_channel_width_in_bits = 64;
		vbios->number_of_dram_channels = asic_id.vram_width / vbios->dram_channel_width_in_bits;
		vbios->number_of_dram_banks = 8;
		vbios->high_yclk = bw_int_to_fixed(1600);
		vbios->mid_yclk = bw_int_to_fixed(1600);
		vbios->low_yclk = bw_frc_to_fixed(66666, 100);
		vbios->low_sclk = bw_int_to_fixed(200);
		vbios->mid1_sclk = bw_int_to_fixed(300);
		vbios->mid2_sclk = bw_int_to_fixed(300);
		vbios->mid3_sclk = bw_int_to_fixed(300);
		vbios->mid4_sclk = bw_int_to_fixed(300);
		vbios->mid5_sclk = bw_int_to_fixed(300);
		vbios->mid6_sclk = bw_int_to_fixed(300);
		vbios->high_sclk = bw_frc_to_fixed(62609, 100);
		vbios->low_voltage_max_dispclk = bw_int_to_fixed(352);
		vbios->mid_voltage_max_dispclk = bw_int_to_fixed(467);
		vbios->high_voltage_max_dispclk = bw_int_to_fixed(643);
		vbios->low_voltage_max_phyclk = bw_int_to_fixed(540);
		vbios->mid_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->high_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->data_return_bus_width = bw_int_to_fixed(32);
		vbios->trc = bw_int_to_fixed(50);
		vbios->dmifmc_urgent_latency = bw_int_to_fixed(4);
		vbios->stutter_self_refresh_exit_latency = bw_frc_to_fixed(153, 10);
		vbios->stutter_self_refresh_entry_latency = bw_int_to_fixed(0);
		vbios->nbp_state_change_latency = bw_frc_to_fixed(19649, 1000);
		vbios->mcifwrmc_urgent_latency = bw_int_to_fixed(10);
		vbios->scatter_gather_enable = true;
		vbios->down_spread_percentage = bw_frc_to_fixed(5, 10);
		vbios->cursor_width = 32;
		vbios->average_compression_rate = 4;
		vbios->number_of_request_slots_gmc_reserves_for_dmif_per_channel = 256;
		vbios->blackout_duration = bw_int_to_fixed(0); /* us */
		vbios->maximum_blackout_recovery_time = bw_int_to_fixed(0);

		dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation = 100;
		dceip->max_average_percent_of_ideal_drambw_display_can_use_in_normal_system_operation = 100;
		dceip->percent_of_ideal_port_bw_received_after_urgent_latency = 100;
		dceip->large_cursor = false;
		dceip->dmif_request_buffer_size = bw_int_to_fixed(768);
		dceip->dmif_pipe_en_fbc_chunk_tracker = false;
		dceip->cursor_max_outstanding_group_num = 1;
		dceip->lines_interleaved_into_lb = 2;
		dceip->chunk_width = 256;
		dceip->number_of_graphics_pipes = 3;
		dceip->number_of_underlay_pipes = 1;
		dceip->low_power_tiling_mode = 0;
		dceip->display_write_back_supported = false;
		dceip->argb_compression_support = false;
		dceip->underlay_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35556, 10000);
		dceip->underlay_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->underlay_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->underlay_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->graphics_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35, 10);
		dceip->graphics_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->graphics_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->graphics_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->alpha_vscaler_efficiency = bw_int_to_fixed(3);
		dceip->max_dmif_buffer_allocated = 2;
		dceip->graphics_dmif_size = 12288;
		dceip->underlay_luma_dmif_size = 19456;
		dceip->underlay_chroma_dmif_size = 23552;
		dceip->pre_downscaler_enabled = true;
		dceip->underlay_downscale_prefetch_enabled = true;
		dceip->lb_write_pixels_per_dispclk = bw_int_to_fixed(1);
		dceip->lb_size_per_component444 = bw_int_to_fixed(82176);
		dceip->graphics_lb_nodownscaling_multi_line_prefetching = false;
		dceip->stutter_and_dram_clock_state_change_gated_before_cursor =
			bw_int_to_fixed(0);
		dceip->underlay420_luma_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->underlay420_chroma_lb_size_per_component =
			bw_int_to_fixed(164352);
		dceip->underlay422_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->cursor_chunk_width = bw_int_to_fixed(64);
		dceip->cursor_dcp_buffer_lines = bw_int_to_fixed(4);
		dceip->underlay_maximum_width_efficient_for_tiling =
			bw_int_to_fixed(1920);
		dceip->underlay_maximum_height_efficient_for_tiling =
			bw_int_to_fixed(1080);
		dceip->peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display =
			bw_frc_to_fixed(3, 10);
		dceip->peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation =
			bw_int_to_fixed(25);
		dceip->minimum_outstanding_pte_request_limit = bw_int_to_fixed(
			2);
		dceip->maximum_total_outstanding_pte_requests_allowed_by_saw =
			bw_int_to_fixed(128);
		dceip->limit_excessive_outstanding_dmif_requests = true;
		dceip->linear_mode_line_request_alternation_slice =
			bw_int_to_fixed(64);
		dceip->scatter_gather_lines_of_pte_prefetching_in_linear_mode =
			32;
		dceip->display_write_back420_luma_mcifwr_buffer_size = 12288;
		dceip->display_write_back420_chroma_mcifwr_buffer_size = 8192;
		dceip->request_efficiency = bw_frc_to_fixed(8, 10);
		dceip->dispclk_per_request = bw_int_to_fixed(2);
		dceip->dispclk_ramping_factor = bw_frc_to_fixed(105, 100);
		dceip->display_pipe_throughput_factor = bw_frc_to_fixed(105, 100);
		dceip->scatter_gather_pte_request_rows_in_tiling_mode = 2;
		dceip->mcifwr_all_surfaces_burst_time = bw_int_to_fixed(0); /* todo: this is a bug*/
		break;
	case BW_CALCS_VERSION_POLARIS10:
		/* TODO: Treat VEGAM the same as P10 for now
		 * Need to tune the para for VEGAM if needed */
	case BW_CALCS_VERSION_VEGAM:
		vbios->memory_type = bw_def_gddr5;
		vbios->dram_channel_width_in_bits = 32;
		vbios->number_of_dram_channels = asic_id.vram_width / vbios->dram_channel_width_in_bits;
		vbios->number_of_dram_banks = 8;
		vbios->high_yclk = bw_int_to_fixed(6000);
		vbios->mid_yclk = bw_int_to_fixed(3200);
		vbios->low_yclk = bw_int_to_fixed(1000);
		vbios->low_sclk = bw_int_to_fixed(300);
		vbios->mid1_sclk = bw_int_to_fixed(400);
		vbios->mid2_sclk = bw_int_to_fixed(500);
		vbios->mid3_sclk = bw_int_to_fixed(600);
		vbios->mid4_sclk = bw_int_to_fixed(700);
		vbios->mid5_sclk = bw_int_to_fixed(800);
		vbios->mid6_sclk = bw_int_to_fixed(974);
		vbios->high_sclk = bw_int_to_fixed(1154);
		vbios->low_voltage_max_dispclk = bw_int_to_fixed(459);
		vbios->mid_voltage_max_dispclk = bw_int_to_fixed(654);
		vbios->high_voltage_max_dispclk = bw_int_to_fixed(1108);
		vbios->low_voltage_max_phyclk = bw_int_to_fixed(540);
		vbios->mid_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->high_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->data_return_bus_width = bw_int_to_fixed(32);
		vbios->trc = bw_int_to_fixed(48);
		vbios->dmifmc_urgent_latency = bw_int_to_fixed(3);
		vbios->stutter_self_refresh_exit_latency = bw_int_to_fixed(5);
		vbios->stutter_self_refresh_entry_latency = bw_int_to_fixed(0);
		vbios->nbp_state_change_latency = bw_int_to_fixed(45);
		vbios->mcifwrmc_urgent_latency = bw_int_to_fixed(10);
		vbios->scatter_gather_enable = true;
		vbios->down_spread_percentage = bw_frc_to_fixed(5, 10);
		vbios->cursor_width = 32;
		vbios->average_compression_rate = 4;
		vbios->number_of_request_slots_gmc_reserves_for_dmif_per_channel = 256;
		vbios->blackout_duration = bw_int_to_fixed(0); /* us */
		vbios->maximum_blackout_recovery_time = bw_int_to_fixed(0);

		dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation = 100;
		dceip->max_average_percent_of_ideal_drambw_display_can_use_in_normal_system_operation = 100;
		dceip->percent_of_ideal_port_bw_received_after_urgent_latency = 100;
		dceip->large_cursor = false;
		dceip->dmif_request_buffer_size = bw_int_to_fixed(768);
		dceip->dmif_pipe_en_fbc_chunk_tracker = false;
		dceip->cursor_max_outstanding_group_num = 1;
		dceip->lines_interleaved_into_lb = 2;
		dceip->chunk_width = 256;
		dceip->number_of_graphics_pipes = 6;
		dceip->number_of_underlay_pipes = 0;
		dceip->low_power_tiling_mode = 0;
		dceip->display_write_back_supported = false;
		dceip->argb_compression_support = true;
		dceip->underlay_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35556, 10000);
		dceip->underlay_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->underlay_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->underlay_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->graphics_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35, 10);
		dceip->graphics_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->graphics_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->graphics_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->alpha_vscaler_efficiency = bw_int_to_fixed(3);
		dceip->max_dmif_buffer_allocated = 4;
		dceip->graphics_dmif_size = 12288;
		dceip->underlay_luma_dmif_size = 19456;
		dceip->underlay_chroma_dmif_size = 23552;
		dceip->pre_downscaler_enabled = true;
		dceip->underlay_downscale_prefetch_enabled = true;
		dceip->lb_write_pixels_per_dispclk = bw_int_to_fixed(1);
		dceip->lb_size_per_component444 = bw_int_to_fixed(245952);
		dceip->graphics_lb_nodownscaling_multi_line_prefetching = true;
		dceip->stutter_and_dram_clock_state_change_gated_before_cursor =
			bw_int_to_fixed(1);
		dceip->underlay420_luma_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->underlay420_chroma_lb_size_per_component =
			bw_int_to_fixed(164352);
		dceip->underlay422_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->cursor_chunk_width = bw_int_to_fixed(64);
		dceip->cursor_dcp_buffer_lines = bw_int_to_fixed(4);
		dceip->underlay_maximum_width_efficient_for_tiling =
			bw_int_to_fixed(1920);
		dceip->underlay_maximum_height_efficient_for_tiling =
			bw_int_to_fixed(1080);
		dceip->peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display =
			bw_frc_to_fixed(3, 10);
		dceip->peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation =
			bw_int_to_fixed(25);
		dceip->minimum_outstanding_pte_request_limit = bw_int_to_fixed(
			2);
		dceip->maximum_total_outstanding_pte_requests_allowed_by_saw =
			bw_int_to_fixed(128);
		dceip->limit_excessive_outstanding_dmif_requests = true;
		dceip->linear_mode_line_request_alternation_slice =
			bw_int_to_fixed(64);
		dceip->scatter_gather_lines_of_pte_prefetching_in_linear_mode =
			32;
		dceip->display_write_back420_luma_mcifwr_buffer_size = 12288;
		dceip->display_write_back420_chroma_mcifwr_buffer_size = 8192;
		dceip->request_efficiency = bw_frc_to_fixed(8, 10);
		dceip->dispclk_per_request = bw_int_to_fixed(2);
		dceip->dispclk_ramping_factor = bw_frc_to_fixed(105, 100);
		dceip->display_pipe_throughput_factor = bw_frc_to_fixed(105, 100);
		dceip->scatter_gather_pte_request_rows_in_tiling_mode = 2;
		dceip->mcifwr_all_surfaces_burst_time = bw_int_to_fixed(0);
		break;
	case BW_CALCS_VERSION_POLARIS11:
		vbios->memory_type = bw_def_gddr5;
		vbios->dram_channel_width_in_bits = 32;
		vbios->number_of_dram_channels = asic_id.vram_width / vbios->dram_channel_width_in_bits;
		vbios->number_of_dram_banks = 8;
		vbios->high_yclk = bw_int_to_fixed(6000);
		vbios->mid_yclk = bw_int_to_fixed(3200);
		vbios->low_yclk = bw_int_to_fixed(1000);
		vbios->low_sclk = bw_int_to_fixed(300);
		vbios->mid1_sclk = bw_int_to_fixed(400);
		vbios->mid2_sclk = bw_int_to_fixed(500);
		vbios->mid3_sclk = bw_int_to_fixed(600);
		vbios->mid4_sclk = bw_int_to_fixed(700);
		vbios->mid5_sclk = bw_int_to_fixed(800);
		vbios->mid6_sclk = bw_int_to_fixed(974);
		vbios->high_sclk = bw_int_to_fixed(1154);
		vbios->low_voltage_max_dispclk = bw_int_to_fixed(459);
		vbios->mid_voltage_max_dispclk = bw_int_to_fixed(654);
		vbios->high_voltage_max_dispclk = bw_int_to_fixed(1108);
		vbios->low_voltage_max_phyclk = bw_int_to_fixed(540);
		vbios->mid_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->high_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->data_return_bus_width = bw_int_to_fixed(32);
		vbios->trc = bw_int_to_fixed(48);
		if (vbios->number_of_dram_channels == 2) // 64-bit
			vbios->dmifmc_urgent_latency = bw_int_to_fixed(4);
		else
			vbios->dmifmc_urgent_latency = bw_int_to_fixed(3);
		vbios->stutter_self_refresh_exit_latency = bw_int_to_fixed(5);
		vbios->stutter_self_refresh_entry_latency = bw_int_to_fixed(0);
		vbios->nbp_state_change_latency = bw_int_to_fixed(45);
		vbios->mcifwrmc_urgent_latency = bw_int_to_fixed(10);
		vbios->scatter_gather_enable = true;
		vbios->down_spread_percentage = bw_frc_to_fixed(5, 10);
		vbios->cursor_width = 32;
		vbios->average_compression_rate = 4;
		vbios->number_of_request_slots_gmc_reserves_for_dmif_per_channel = 256;
		vbios->blackout_duration = bw_int_to_fixed(0); /* us */
		vbios->maximum_blackout_recovery_time = bw_int_to_fixed(0);

		dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation = 100;
		dceip->max_average_percent_of_ideal_drambw_display_can_use_in_normal_system_operation = 100;
		dceip->percent_of_ideal_port_bw_received_after_urgent_latency = 100;
		dceip->large_cursor = false;
		dceip->dmif_request_buffer_size = bw_int_to_fixed(768);
		dceip->dmif_pipe_en_fbc_chunk_tracker = false;
		dceip->cursor_max_outstanding_group_num = 1;
		dceip->lines_interleaved_into_lb = 2;
		dceip->chunk_width = 256;
		dceip->number_of_graphics_pipes = 5;
		dceip->number_of_underlay_pipes = 0;
		dceip->low_power_tiling_mode = 0;
		dceip->display_write_back_supported = false;
		dceip->argb_compression_support = true;
		dceip->underlay_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35556, 10000);
		dceip->underlay_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->underlay_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->underlay_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->graphics_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35, 10);
		dceip->graphics_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->graphics_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->graphics_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->alpha_vscaler_efficiency = bw_int_to_fixed(3);
		dceip->max_dmif_buffer_allocated = 4;
		dceip->graphics_dmif_size = 12288;
		dceip->underlay_luma_dmif_size = 19456;
		dceip->underlay_chroma_dmif_size = 23552;
		dceip->pre_downscaler_enabled = true;
		dceip->underlay_downscale_prefetch_enabled = true;
		dceip->lb_write_pixels_per_dispclk = bw_int_to_fixed(1);
		dceip->lb_size_per_component444 = bw_int_to_fixed(245952);
		dceip->graphics_lb_nodownscaling_multi_line_prefetching = true;
		dceip->stutter_and_dram_clock_state_change_gated_before_cursor =
			bw_int_to_fixed(1);
		dceip->underlay420_luma_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->underlay420_chroma_lb_size_per_component =
			bw_int_to_fixed(164352);
		dceip->underlay422_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->cursor_chunk_width = bw_int_to_fixed(64);
		dceip->cursor_dcp_buffer_lines = bw_int_to_fixed(4);
		dceip->underlay_maximum_width_efficient_for_tiling =
			bw_int_to_fixed(1920);
		dceip->underlay_maximum_height_efficient_for_tiling =
			bw_int_to_fixed(1080);
		dceip->peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display =
			bw_frc_to_fixed(3, 10);
		dceip->peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation =
			bw_int_to_fixed(25);
		dceip->minimum_outstanding_pte_request_limit = bw_int_to_fixed(
			2);
		dceip->maximum_total_outstanding_pte_requests_allowed_by_saw =
			bw_int_to_fixed(128);
		dceip->limit_excessive_outstanding_dmif_requests = true;
		dceip->linear_mode_line_request_alternation_slice =
			bw_int_to_fixed(64);
		dceip->scatter_gather_lines_of_pte_prefetching_in_linear_mode =
			32;
		dceip->display_write_back420_luma_mcifwr_buffer_size = 12288;
		dceip->display_write_back420_chroma_mcifwr_buffer_size = 8192;
		dceip->request_efficiency = bw_frc_to_fixed(8, 10);
		dceip->dispclk_per_request = bw_int_to_fixed(2);
		dceip->dispclk_ramping_factor = bw_frc_to_fixed(105, 100);
		dceip->display_pipe_throughput_factor = bw_frc_to_fixed(105, 100);
		dceip->scatter_gather_pte_request_rows_in_tiling_mode = 2;
		dceip->mcifwr_all_surfaces_burst_time = bw_int_to_fixed(0);
		break;
	case BW_CALCS_VERSION_POLARIS12:
		vbios->memory_type = bw_def_gddr5;
		vbios->dram_channel_width_in_bits = 32;
		vbios->number_of_dram_channels = asic_id.vram_width / vbios->dram_channel_width_in_bits;
		vbios->number_of_dram_banks = 8;
		vbios->high_yclk = bw_int_to_fixed(6000);
		vbios->mid_yclk = bw_int_to_fixed(3200);
		vbios->low_yclk = bw_int_to_fixed(1000);
		vbios->low_sclk = bw_int_to_fixed(678);
		vbios->mid1_sclk = bw_int_to_fixed(864);
		vbios->mid2_sclk = bw_int_to_fixed(900);
		vbios->mid3_sclk = bw_int_to_fixed(920);
		vbios->mid4_sclk = bw_int_to_fixed(940);
		vbios->mid5_sclk = bw_int_to_fixed(960);
		vbios->mid6_sclk = bw_int_to_fixed(980);
		vbios->high_sclk = bw_int_to_fixed(1049);
		vbios->low_voltage_max_dispclk = bw_int_to_fixed(459);
		vbios->mid_voltage_max_dispclk = bw_int_to_fixed(654);
		vbios->high_voltage_max_dispclk = bw_int_to_fixed(1108);
		vbios->low_voltage_max_phyclk = bw_int_to_fixed(540);
		vbios->mid_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->high_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->data_return_bus_width = bw_int_to_fixed(32);
		vbios->trc = bw_int_to_fixed(48);
		if (vbios->number_of_dram_channels == 2) // 64-bit
			vbios->dmifmc_urgent_latency = bw_int_to_fixed(4);
		else
			vbios->dmifmc_urgent_latency = bw_int_to_fixed(3);
		vbios->stutter_self_refresh_exit_latency = bw_int_to_fixed(5);
		vbios->stutter_self_refresh_entry_latency = bw_int_to_fixed(0);
		vbios->nbp_state_change_latency = bw_int_to_fixed(250);
		vbios->mcifwrmc_urgent_latency = bw_int_to_fixed(10);
		vbios->scatter_gather_enable = false;
		vbios->down_spread_percentage = bw_frc_to_fixed(5, 10);
		vbios->cursor_width = 32;
		vbios->average_compression_rate = 4;
		vbios->number_of_request_slots_gmc_reserves_for_dmif_per_channel = 256;
		vbios->blackout_duration = bw_int_to_fixed(0); /* us */
		vbios->maximum_blackout_recovery_time = bw_int_to_fixed(0);

		dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation = 100;
		dceip->max_average_percent_of_ideal_drambw_display_can_use_in_normal_system_operation = 100;
		dceip->percent_of_ideal_port_bw_received_after_urgent_latency = 100;
		dceip->large_cursor = false;
		dceip->dmif_request_buffer_size = bw_int_to_fixed(768);
		dceip->dmif_pipe_en_fbc_chunk_tracker = false;
		dceip->cursor_max_outstanding_group_num = 1;
		dceip->lines_interleaved_into_lb = 2;
		dceip->chunk_width = 256;
		dceip->number_of_graphics_pipes = 5;
		dceip->number_of_underlay_pipes = 0;
		dceip->low_power_tiling_mode = 0;
		dceip->display_write_back_supported = true;
		dceip->argb_compression_support = true;
		dceip->underlay_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35556, 10000);
		dceip->underlay_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->underlay_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->underlay_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->graphics_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35, 10);
		dceip->graphics_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->graphics_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->graphics_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->alpha_vscaler_efficiency = bw_int_to_fixed(3);
		dceip->max_dmif_buffer_allocated = 4;
		dceip->graphics_dmif_size = 12288;
		dceip->underlay_luma_dmif_size = 19456;
		dceip->underlay_chroma_dmif_size = 23552;
		dceip->pre_downscaler_enabled = true;
		dceip->underlay_downscale_prefetch_enabled = true;
		dceip->lb_write_pixels_per_dispclk = bw_int_to_fixed(1);
		dceip->lb_size_per_component444 = bw_int_to_fixed(245952);
		dceip->graphics_lb_nodownscaling_multi_line_prefetching = true;
		dceip->stutter_and_dram_clock_state_change_gated_before_cursor =
			bw_int_to_fixed(1);
		dceip->underlay420_luma_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->underlay420_chroma_lb_size_per_component =
			bw_int_to_fixed(164352);
		dceip->underlay422_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->cursor_chunk_width = bw_int_to_fixed(64);
		dceip->cursor_dcp_buffer_lines = bw_int_to_fixed(4);
		dceip->underlay_maximum_width_efficient_for_tiling =
			bw_int_to_fixed(1920);
		dceip->underlay_maximum_height_efficient_for_tiling =
			bw_int_to_fixed(1080);
		dceip->peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display =
			bw_frc_to_fixed(3, 10);
		dceip->peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation =
			bw_int_to_fixed(25);
		dceip->minimum_outstanding_pte_request_limit = bw_int_to_fixed(
			2);
		dceip->maximum_total_outstanding_pte_requests_allowed_by_saw =
			bw_int_to_fixed(128);
		dceip->limit_excessive_outstanding_dmif_requests = true;
		dceip->linear_mode_line_request_alternation_slice =
			bw_int_to_fixed(64);
		dceip->scatter_gather_lines_of_pte_prefetching_in_linear_mode =
			32;
		dceip->display_write_back420_luma_mcifwr_buffer_size = 12288;
		dceip->display_write_back420_chroma_mcifwr_buffer_size = 8192;
		dceip->request_efficiency = bw_frc_to_fixed(8, 10);
		dceip->dispclk_per_request = bw_int_to_fixed(2);
		dceip->dispclk_ramping_factor = bw_frc_to_fixed(105, 100);
		dceip->display_pipe_throughput_factor = bw_frc_to_fixed(105, 100);
		dceip->scatter_gather_pte_request_rows_in_tiling_mode = 2;
		dceip->mcifwr_all_surfaces_burst_time = bw_int_to_fixed(0);
		break;
	case BW_CALCS_VERSION_STONEY:
		vbios->memory_type = bw_def_gddr5;
		vbios->dram_channel_width_in_bits = 64;
		vbios->number_of_dram_channels = asic_id.vram_width / vbios->dram_channel_width_in_bits;
		vbios->number_of_dram_banks = 8;
		vbios->high_yclk = bw_int_to_fixed(1866);
		vbios->mid_yclk = bw_int_to_fixed(1866);
		vbios->low_yclk = bw_int_to_fixed(1333);
		vbios->low_sclk = bw_int_to_fixed(200);
		vbios->mid1_sclk = bw_int_to_fixed(600);
		vbios->mid2_sclk = bw_int_to_fixed(600);
		vbios->mid3_sclk = bw_int_to_fixed(600);
		vbios->mid4_sclk = bw_int_to_fixed(600);
		vbios->mid5_sclk = bw_int_to_fixed(600);
		vbios->mid6_sclk = bw_int_to_fixed(600);
		vbios->high_sclk = bw_int_to_fixed(800);
		vbios->low_voltage_max_dispclk = bw_int_to_fixed(352);
		vbios->mid_voltage_max_dispclk = bw_int_to_fixed(467);
		vbios->high_voltage_max_dispclk = bw_int_to_fixed(643);
		vbios->low_voltage_max_phyclk = bw_int_to_fixed(540);
		vbios->mid_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->high_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->data_return_bus_width = bw_int_to_fixed(32);
		vbios->trc = bw_int_to_fixed(50);
		vbios->dmifmc_urgent_latency = bw_int_to_fixed(4);
		vbios->stutter_self_refresh_exit_latency = bw_frc_to_fixed(158, 10);
		vbios->stutter_self_refresh_entry_latency = bw_int_to_fixed(0);
		vbios->nbp_state_change_latency = bw_frc_to_fixed(2008, 100);
		vbios->mcifwrmc_urgent_latency = bw_int_to_fixed(10);
		vbios->scatter_gather_enable = true;
		vbios->down_spread_percentage = bw_frc_to_fixed(5, 10);
		vbios->cursor_width = 32;
		vbios->average_compression_rate = 4;
		vbios->number_of_request_slots_gmc_reserves_for_dmif_per_channel = 256;
		vbios->blackout_duration = bw_int_to_fixed(0); /* us */
		vbios->maximum_blackout_recovery_time = bw_int_to_fixed(0);

		dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation = 100;
		dceip->max_average_percent_of_ideal_drambw_display_can_use_in_normal_system_operation = 100;
		dceip->percent_of_ideal_port_bw_received_after_urgent_latency = 100;
		dceip->large_cursor = false;
		dceip->dmif_request_buffer_size = bw_int_to_fixed(768);
		dceip->dmif_pipe_en_fbc_chunk_tracker = false;
		dceip->cursor_max_outstanding_group_num = 1;
		dceip->lines_interleaved_into_lb = 2;
		dceip->chunk_width = 256;
		dceip->number_of_graphics_pipes = 2;
		dceip->number_of_underlay_pipes = 1;
		dceip->low_power_tiling_mode = 0;
		dceip->display_write_back_supported = false;
		dceip->argb_compression_support = true;
		dceip->underlay_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35556, 10000);
		dceip->underlay_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->underlay_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->underlay_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->graphics_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35, 10);
		dceip->graphics_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->graphics_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->graphics_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->alpha_vscaler_efficiency = bw_int_to_fixed(3);
		dceip->max_dmif_buffer_allocated = 2;
		dceip->graphics_dmif_size = 12288;
		dceip->underlay_luma_dmif_size = 19456;
		dceip->underlay_chroma_dmif_size = 23552;
		dceip->pre_downscaler_enabled = true;
		dceip->underlay_downscale_prefetch_enabled = true;
		dceip->lb_write_pixels_per_dispclk = bw_int_to_fixed(1);
		dceip->lb_size_per_component444 = bw_int_to_fixed(82176);
		dceip->graphics_lb_nodownscaling_multi_line_prefetching = false;
		dceip->stutter_and_dram_clock_state_change_gated_before_cursor =
			bw_int_to_fixed(0);
		dceip->underlay420_luma_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->underlay420_chroma_lb_size_per_component =
			bw_int_to_fixed(164352);
		dceip->underlay422_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->cursor_chunk_width = bw_int_to_fixed(64);
		dceip->cursor_dcp_buffer_lines = bw_int_to_fixed(4);
		dceip->underlay_maximum_width_efficient_for_tiling =
			bw_int_to_fixed(1920);
		dceip->underlay_maximum_height_efficient_for_tiling =
			bw_int_to_fixed(1080);
		dceip->peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display =
			bw_frc_to_fixed(3, 10);
		dceip->peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation =
			bw_int_to_fixed(25);
		dceip->minimum_outstanding_pte_request_limit = bw_int_to_fixed(
			2);
		dceip->maximum_total_outstanding_pte_requests_allowed_by_saw =
			bw_int_to_fixed(128);
		dceip->limit_excessive_outstanding_dmif_requests = true;
		dceip->linear_mode_line_request_alternation_slice =
			bw_int_to_fixed(64);
		dceip->scatter_gather_lines_of_pte_prefetching_in_linear_mode =
			32;
		dceip->display_write_back420_luma_mcifwr_buffer_size = 12288;
		dceip->display_write_back420_chroma_mcifwr_buffer_size = 8192;
		dceip->request_efficiency = bw_frc_to_fixed(8, 10);
		dceip->dispclk_per_request = bw_int_to_fixed(2);
		dceip->dispclk_ramping_factor = bw_frc_to_fixed(105, 100);
		dceip->display_pipe_throughput_factor = bw_frc_to_fixed(105, 100);
		dceip->scatter_gather_pte_request_rows_in_tiling_mode = 2;
		dceip->mcifwr_all_surfaces_burst_time = bw_int_to_fixed(0);
		break;
	case BW_CALCS_VERSION_VEGA10:
		vbios->memory_type = bw_def_hbm;
		vbios->dram_channel_width_in_bits = 128;
		vbios->number_of_dram_channels = asic_id.vram_width / vbios->dram_channel_width_in_bits;
		vbios->number_of_dram_banks = 16;
		vbios->high_yclk = bw_int_to_fixed(2400);
		vbios->mid_yclk = bw_int_to_fixed(1700);
		vbios->low_yclk = bw_int_to_fixed(1000);
		vbios->low_sclk = bw_int_to_fixed(300);
		vbios->mid1_sclk = bw_int_to_fixed(350);
		vbios->mid2_sclk = bw_int_to_fixed(400);
		vbios->mid3_sclk = bw_int_to_fixed(500);
		vbios->mid4_sclk = bw_int_to_fixed(600);
		vbios->mid5_sclk = bw_int_to_fixed(700);
		vbios->mid6_sclk = bw_int_to_fixed(760);
		vbios->high_sclk = bw_int_to_fixed(776);
		vbios->low_voltage_max_dispclk = bw_int_to_fixed(460);
		vbios->mid_voltage_max_dispclk = bw_int_to_fixed(670);
		vbios->high_voltage_max_dispclk = bw_int_to_fixed(1133);
		vbios->low_voltage_max_phyclk = bw_int_to_fixed(540);
		vbios->mid_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->high_voltage_max_phyclk = bw_int_to_fixed(810);
		vbios->data_return_bus_width = bw_int_to_fixed(32);
		vbios->trc = bw_int_to_fixed(48);
		vbios->dmifmc_urgent_latency = bw_int_to_fixed(3);
		vbios->stutter_self_refresh_exit_latency = bw_frc_to_fixed(75, 10);
		vbios->stutter_self_refresh_entry_latency = bw_frc_to_fixed(19, 10);
		vbios->nbp_state_change_latency = bw_int_to_fixed(39);
		vbios->mcifwrmc_urgent_latency = bw_int_to_fixed(10);
		vbios->scatter_gather_enable = false;
		vbios->down_spread_percentage = bw_frc_to_fixed(5, 10);
		vbios->cursor_width = 32;
		vbios->average_compression_rate = 4;
		vbios->number_of_request_slots_gmc_reserves_for_dmif_per_channel = 8;
		vbios->blackout_duration = bw_int_to_fixed(0); /* us */
		vbios->maximum_blackout_recovery_time = bw_int_to_fixed(0);

		dceip->max_average_percent_of_ideal_port_bw_display_can_use_in_normal_system_operation = 100;
		dceip->max_average_percent_of_ideal_drambw_display_can_use_in_normal_system_operation = 100;
		dceip->percent_of_ideal_port_bw_received_after_urgent_latency = 100;
		dceip->large_cursor = false;
		dceip->dmif_request_buffer_size = bw_int_to_fixed(2304);
		dceip->dmif_pipe_en_fbc_chunk_tracker = true;
		dceip->cursor_max_outstanding_group_num = 1;
		dceip->lines_interleaved_into_lb = 2;
		dceip->chunk_width = 256;
		dceip->number_of_graphics_pipes = 6;
		dceip->number_of_underlay_pipes = 0;
		dceip->low_power_tiling_mode = 0;
		dceip->display_write_back_supported = true;
		dceip->argb_compression_support = true;
		dceip->underlay_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35556, 10000);
		dceip->underlay_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->underlay_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->underlay_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->graphics_vscaler_efficiency6_bit_per_component =
			bw_frc_to_fixed(35, 10);
		dceip->graphics_vscaler_efficiency8_bit_per_component =
			bw_frc_to_fixed(34286, 10000);
		dceip->graphics_vscaler_efficiency10_bit_per_component =
			bw_frc_to_fixed(32, 10);
		dceip->graphics_vscaler_efficiency12_bit_per_component =
			bw_int_to_fixed(3);
		dceip->alpha_vscaler_efficiency = bw_int_to_fixed(3);
		dceip->max_dmif_buffer_allocated = 4;
		dceip->graphics_dmif_size = 24576;
		dceip->underlay_luma_dmif_size = 19456;
		dceip->underlay_chroma_dmif_size = 23552;
		dceip->pre_downscaler_enabled = true;
		dceip->underlay_downscale_prefetch_enabled = false;
		dceip->lb_write_pixels_per_dispclk = bw_int_to_fixed(1);
		dceip->lb_size_per_component444 = bw_int_to_fixed(245952);
		dceip->graphics_lb_nodownscaling_multi_line_prefetching = true;
		dceip->stutter_and_dram_clock_state_change_gated_before_cursor =
			bw_int_to_fixed(1);
		dceip->underlay420_luma_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->underlay420_chroma_lb_size_per_component =
			bw_int_to_fixed(164352);
		dceip->underlay422_lb_size_per_component = bw_int_to_fixed(
			82176);
		dceip->cursor_chunk_width = bw_int_to_fixed(64);
		dceip->cursor_dcp_buffer_lines = bw_int_to_fixed(4);
		dceip->underlay_maximum_width_efficient_for_tiling =
			bw_int_to_fixed(1920);
		dceip->underlay_maximum_height_efficient_for_tiling =
			bw_int_to_fixed(1080);
		dceip->peak_pte_request_to_eviction_ratio_limiting_multiple_displays_or_single_rotated_display =
			bw_frc_to_fixed(3, 10);
		dceip->peak_pte_request_to_eviction_ratio_limiting_single_display_no_rotation =
			bw_int_to_fixed(25);
		dceip->minimum_outstanding_pte_request_limit = bw_int_to_fixed(
			2);
		dceip->maximum_total_outstanding_pte_requests_allowed_by_saw =
			bw_int_to_fixed(128);
		dceip->limit_excessive_outstanding_dmif_requests = true;
		dceip->linear_mode_line_request_alternation_slice =
			bw_int_to_fixed(64);
		dceip->scatter_gather_lines_of_pte_prefetching_in_linear_mode =
			32;
		dceip->display_write_back420_luma_mcifwr_buffer_size = 12288;
		dceip->display_write_back420_chroma_mcifwr_buffer_size = 8192;
		dceip->request_efficiency = bw_frc_to_fixed(8, 10);
		dceip->dispclk_per_request = bw_int_to_fixed(2);
		dceip->dispclk_ramping_factor = bw_frc_to_fixed(105, 100);
		dceip->display_pipe_throughput_factor = bw_frc_to_fixed(105, 100);
		dceip->scatter_gather_pte_request_rows_in_tiling_mode = 2;
		dceip->mcifwr_all_surfaces_burst_time = bw_int_to_fixed(0);
		break;
	default:
		break;
	}
	*bw_dceip = *dceip;
	*bw_vbios = *vbios;

	kfree(dceip);
	kfree(vbios);
}

/*
 * Compare calculated (required) clocks against the clocks available at
 * maximum voltage (max Performance Level).
 */
static bool is_display_configuration_supported(
	const struct bw_calcs_vbios *vbios,
	const struct dce_bw_output *calcs_output)
{
	uint32_t int_max_clk;

	int_max_clk = bw_fixed_to_int(vbios->high_voltage_max_dispclk);
	int_max_clk *= 1000; /* MHz to kHz */
	if (calcs_output->dispclk_khz > int_max_clk)
		return false;

	int_max_clk = bw_fixed_to_int(vbios->high_sclk);
	int_max_clk *= 1000; /* MHz to kHz */
	if (calcs_output->sclk_khz > int_max_clk)
		return false;

	return true;
}

static void populate_initial_data(
	const struct pipe_ctx pipe[], int pipe_count, struct bw_calcs_data *data)
{
	int i, j;
	int num_displays = 0;

	data->underlay_surface_type = bw_def_420;
	data->panning_and_bezel_adjustment = bw_def_none;
	data->graphics_lb_bpc = 10;
	data->underlay_lb_bpc = 8;
	data->underlay_tiling_mode = bw_def_tiled;
	data->graphics_tiling_mode = bw_def_tiled;
	data->underlay_micro_tile_mode = bw_def_display_micro_tiling;
	data->graphics_micro_tile_mode = bw_def_display_micro_tiling;
	data->increase_voltage_to_support_mclk_switch = true;

	/* Pipes with underlay first */
	for (i = 0; i < pipe_count; i++) {
		if (!pipe[i].stream || !pipe[i].bottom_pipe)
			continue;

		ASSERT(pipe[i].plane_state);

		if (num_displays == 0) {
			if (!pipe[i].plane_state->visible)
				data->d0_underlay_mode = bw_def_underlay_only;
			else
				data->d0_underlay_mode = bw_def_blend;
		} else {
			if (!pipe[i].plane_state->visible)
				data->d1_underlay_mode = bw_def_underlay_only;
			else
				data->d1_underlay_mode = bw_def_blend;
		}

		data->fbc_en[num_displays + 4] = false;
		data->lpt_en[num_displays + 4] = false;
		data->h_total[num_displays + 4] = bw_int_to_fixed(pipe[i].stream->timing.h_total);
		data->v_total[num_displays + 4] = bw_int_to_fixed(pipe[i].stream->timing.v_total);
		data->pixel_rate[num_displays + 4] = bw_frc_to_fixed(pipe[i].stream->timing.pix_clk_100hz, 10000);
		data->src_width[num_displays + 4] = bw_int_to_fixed(pipe[i].plane_res.scl_data.viewport.width);
		data->pitch_in_pixels[num_displays + 4] = data->src_width[num_displays + 4];
		data->src_height[num_displays + 4] = bw_int_to_fixed(pipe[i].plane_res.scl_data.viewport.height);
		data->h_taps[num_displays + 4] = bw_int_to_fixed(pipe[i].plane_res.scl_data.taps.h_taps);
		data->v_taps[num_displays + 4] = bw_int_to_fixed(pipe[i].plane_res.scl_data.taps.v_taps);
		data->h_scale_ratio[num_displays + 4] = fixed31_32_to_bw_fixed(pipe[i].plane_res.scl_data.ratios.horz.value);
		data->v_scale_ratio[num_displays + 4] = fixed31_32_to_bw_fixed(pipe[i].plane_res.scl_data.ratios.vert.value);
		switch (pipe[i].plane_state->rotation) {
		case ROTATION_ANGLE_0:
			data->rotation_angle[num_displays + 4] = bw_int_to_fixed(0);
			break;
		case ROTATION_ANGLE_90:
			data->rotation_angle[num_displays + 4] = bw_int_to_fixed(90);
			break;
		case ROTATION_ANGLE_180:
			data->rotation_angle[num_displays + 4] = bw_int_to_fixed(180);
			break;
		case ROTATION_ANGLE_270:
			data->rotation_angle[num_displays + 4] = bw_int_to_fixed(270);
			break;
		default:
			break;
		}
		switch (pipe[i].plane_state->format) {
		case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
		case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
			data->bytes_per_pixel[num_displays + 4] = 2;
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
		case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
		case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
			data->bytes_per_pixel[num_displays + 4] = 4;
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
			data->bytes_per_pixel[num_displays + 4] = 8;
			break;
		default:
			data->bytes_per_pixel[num_displays + 4] = 4;
			break;
		}
		data->interlace_mode[num_displays + 4] = false;
		data->stereo_mode[num_displays + 4] = bw_def_mono;


		for (j = 0; j < 2; j++) {
			data->fbc_en[num_displays * 2 + j] = false;
			data->lpt_en[num_displays * 2 + j] = false;

			data->src_height[num_displays * 2 + j] = bw_int_to_fixed(pipe[i].bottom_pipe->plane_res.scl_data.viewport.height);
			data->src_width[num_displays * 2 + j] = bw_int_to_fixed(pipe[i].bottom_pipe->plane_res.scl_data.viewport.width);
			data->pitch_in_pixels[num_displays * 2 + j] = bw_int_to_fixed(
					pipe[i].bottom_pipe->plane_state->plane_size.surface_pitch);
			data->h_taps[num_displays * 2 + j] = bw_int_to_fixed(pipe[i].bottom_pipe->plane_res.scl_data.taps.h_taps);
			data->v_taps[num_displays * 2 + j] = bw_int_to_fixed(pipe[i].bottom_pipe->plane_res.scl_data.taps.v_taps);
			data->h_scale_ratio[num_displays * 2 + j] = fixed31_32_to_bw_fixed(
					pipe[i].bottom_pipe->plane_res.scl_data.ratios.horz.value);
			data->v_scale_ratio[num_displays * 2 + j] = fixed31_32_to_bw_fixed(
					pipe[i].bottom_pipe->plane_res.scl_data.ratios.vert.value);
			switch (pipe[i].bottom_pipe->plane_state->rotation) {
			case ROTATION_ANGLE_0:
				data->rotation_angle[num_displays * 2 + j] = bw_int_to_fixed(0);
				break;
			case ROTATION_ANGLE_90:
				data->rotation_angle[num_displays * 2 + j] = bw_int_to_fixed(90);
				break;
			case ROTATION_ANGLE_180:
				data->rotation_angle[num_displays * 2 + j] = bw_int_to_fixed(180);
				break;
			case ROTATION_ANGLE_270:
				data->rotation_angle[num_displays * 2 + j] = bw_int_to_fixed(270);
				break;
			default:
				break;
			}
			data->stereo_mode[num_displays * 2 + j] = bw_def_mono;
		}

		num_displays++;
	}

	/* Pipes without underlay after */
	for (i = 0; i < pipe_count; i++) {
		unsigned int pixel_clock_100hz;
		if (!pipe[i].stream || pipe[i].bottom_pipe)
			continue;


		data->fbc_en[num_displays + 4] = false;
		data->lpt_en[num_displays + 4] = false;
		data->h_total[num_displays + 4] = bw_int_to_fixed(pipe[i].stream->timing.h_total);
		data->v_total[num_displays + 4] = bw_int_to_fixed(pipe[i].stream->timing.v_total);
		pixel_clock_100hz = pipe[i].stream->timing.pix_clk_100hz;
		if (pipe[i].stream->timing.timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
			pixel_clock_100hz *= 2;
		data->pixel_rate[num_displays + 4] = bw_frc_to_fixed(pixel_clock_100hz, 10000);
		if (pipe[i].plane_state) {
			data->src_width[num_displays + 4] = bw_int_to_fixed(pipe[i].plane_res.scl_data.viewport.width);
			data->pitch_in_pixels[num_displays + 4] = data->src_width[num_displays + 4];
			data->src_height[num_displays + 4] = bw_int_to_fixed(pipe[i].plane_res.scl_data.viewport.height);
			data->h_taps[num_displays + 4] = bw_int_to_fixed(pipe[i].plane_res.scl_data.taps.h_taps);
			data->v_taps[num_displays + 4] = bw_int_to_fixed(pipe[i].plane_res.scl_data.taps.v_taps);
			data->h_scale_ratio[num_displays + 4] = fixed31_32_to_bw_fixed(pipe[i].plane_res.scl_data.ratios.horz.value);
			data->v_scale_ratio[num_displays + 4] = fixed31_32_to_bw_fixed(pipe[i].plane_res.scl_data.ratios.vert.value);
			switch (pipe[i].plane_state->rotation) {
			case ROTATION_ANGLE_0:
				data->rotation_angle[num_displays + 4] = bw_int_to_fixed(0);
				break;
			case ROTATION_ANGLE_90:
				data->rotation_angle[num_displays + 4] = bw_int_to_fixed(90);
				break;
			case ROTATION_ANGLE_180:
				data->rotation_angle[num_displays + 4] = bw_int_to_fixed(180);
				break;
			case ROTATION_ANGLE_270:
				data->rotation_angle[num_displays + 4] = bw_int_to_fixed(270);
				break;
			default:
				break;
			}
			switch (pipe[i].plane_state->format) {
			case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
			case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
			case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
			case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
				data->bytes_per_pixel[num_displays + 4] = 2;
				break;
			case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
			case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
			case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
			case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
			case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
			case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
			case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
				data->bytes_per_pixel[num_displays + 4] = 4;
				break;
			case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
			case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
			case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
				data->bytes_per_pixel[num_displays + 4] = 8;
				break;
			default:
				data->bytes_per_pixel[num_displays + 4] = 4;
				break;
			}
		} else if (pipe[i].stream->dst.width != 0 &&
					pipe[i].stream->dst.height != 0 &&
					pipe[i].stream->src.width != 0 &&
					pipe[i].stream->src.height != 0) {
			data->src_width[num_displays + 4] = bw_int_to_fixed(pipe[i].stream->src.width);
			data->pitch_in_pixels[num_displays + 4] = data->src_width[num_displays + 4];
			data->src_height[num_displays + 4] = bw_int_to_fixed(pipe[i].stream->src.height);
			data->h_taps[num_displays + 4] = pipe[i].stream->src.width == pipe[i].stream->dst.width ? bw_int_to_fixed(1) : bw_int_to_fixed(2);
			data->v_taps[num_displays + 4] = pipe[i].stream->src.height == pipe[i].stream->dst.height ? bw_int_to_fixed(1) : bw_int_to_fixed(2);
			data->h_scale_ratio[num_displays + 4] = bw_frc_to_fixed(pipe[i].stream->src.width, pipe[i].stream->dst.width);
			data->v_scale_ratio[num_displays + 4] = bw_frc_to_fixed(pipe[i].stream->src.height, pipe[i].stream->dst.height);
			data->rotation_angle[num_displays + 4] = bw_int_to_fixed(0);
			data->bytes_per_pixel[num_displays + 4] = 4;
		} else {
			data->src_width[num_displays + 4] = bw_int_to_fixed(pipe[i].stream->timing.h_addressable);
			data->pitch_in_pixels[num_displays + 4] = data->src_width[num_displays + 4];
			data->src_height[num_displays + 4] = bw_int_to_fixed(pipe[i].stream->timing.v_addressable);
			data->h_taps[num_displays + 4] = bw_int_to_fixed(1);
			data->v_taps[num_displays + 4] = bw_int_to_fixed(1);
			data->h_scale_ratio[num_displays + 4] = bw_int_to_fixed(1);
			data->v_scale_ratio[num_displays + 4] = bw_int_to_fixed(1);
			data->rotation_angle[num_displays + 4] = bw_int_to_fixed(0);
			data->bytes_per_pixel[num_displays + 4] = 4;
		}

		data->interlace_mode[num_displays + 4] = false;
		data->stereo_mode[num_displays + 4] = bw_def_mono;
		num_displays++;
	}

	data->number_of_displays = num_displays;
}

static bool all_displays_in_sync(const struct pipe_ctx pipe[],
				 int pipe_count)
{
	const struct pipe_ctx *active_pipes[MAX_PIPES];
	int i, num_active_pipes = 0;

	for (i = 0; i < pipe_count; i++) {
		if (!pipe[i].stream || pipe[i].top_pipe)
			continue;

		active_pipes[num_active_pipes++] = &pipe[i];
	}

	if (!num_active_pipes)
		return false;

	for (i = 1; i < num_active_pipes; ++i) {
		if (!resource_are_streams_timing_synchronizable(
			    active_pipes[0]->stream, active_pipes[i]->stream)) {
			return false;
		}
	}

	return true;
}

/*
 * Return:
 *	true -	Display(s) configuration supported.
 *		In this case 'calcs_output' contains data for HW programming
 *	false - Display(s) configuration not supported (not enough bandwidth).
 */
bool bw_calcs(struct dc_context *ctx,
	const struct bw_calcs_dceip *dceip,
	const struct bw_calcs_vbios *vbios,
	const struct pipe_ctx pipe[],
	int pipe_count,
	struct dce_bw_output *calcs_output)
{
	struct bw_calcs_data *data = kzalloc(sizeof(struct bw_calcs_data),
					     GFP_KERNEL);
	if (!data)
		return false;

	populate_initial_data(pipe, pipe_count, data);

	if (ctx->dc->config.multi_mon_pp_mclk_switch)
		calcs_output->all_displays_in_sync = all_displays_in_sync(pipe, pipe_count);
	else
		calcs_output->all_displays_in_sync = false;

	if (data->number_of_displays != 0) {
		uint8_t yclk_lvl;
		struct bw_fixed high_sclk = vbios->high_sclk;
		struct bw_fixed mid1_sclk = vbios->mid1_sclk;
		struct bw_fixed mid2_sclk = vbios->mid2_sclk;
		struct bw_fixed mid3_sclk = vbios->mid3_sclk;
		struct bw_fixed mid4_sclk = vbios->mid4_sclk;
		struct bw_fixed mid5_sclk = vbios->mid5_sclk;
		struct bw_fixed mid6_sclk = vbios->mid6_sclk;
		struct bw_fixed low_sclk = vbios->low_sclk;
		struct bw_fixed high_yclk = vbios->high_yclk;
		struct bw_fixed mid_yclk = vbios->mid_yclk;
		struct bw_fixed low_yclk = vbios->low_yclk;

		if (ctx->dc->debug.bandwidth_calcs_trace) {
			print_bw_calcs_dceip(ctx, dceip);
			print_bw_calcs_vbios(ctx, vbios);
			print_bw_calcs_data(ctx, data);
		}
		calculate_bandwidth(dceip, vbios, data);

		yclk_lvl = data->y_clk_level;

		calcs_output->nbp_state_change_enable =
			data->nbp_state_change_enable;
		calcs_output->cpuc_state_change_enable =
				data->cpuc_state_change_enable;
		calcs_output->cpup_state_change_enable =
				data->cpup_state_change_enable;
		calcs_output->stutter_mode_enable =
				data->stutter_mode_enable;
		calcs_output->dispclk_khz =
			bw_fixed_to_int(bw_mul(data->dispclk,
					bw_int_to_fixed(1000)));
		calcs_output->blackout_recovery_time_us =
			bw_fixed_to_int(data->blackout_recovery_time);
		calcs_output->sclk_khz =
			bw_fixed_to_int(bw_mul(data->required_sclk,
					bw_int_to_fixed(1000)));
		calcs_output->sclk_deep_sleep_khz =
			bw_fixed_to_int(bw_mul(data->sclk_deep_sleep,
					bw_int_to_fixed(1000)));
		if (yclk_lvl == 0)
			calcs_output->yclk_khz = bw_fixed_to_int(
				bw_mul(low_yclk, bw_int_to_fixed(1000)));
		else if (yclk_lvl == 1)
			calcs_output->yclk_khz = bw_fixed_to_int(
				bw_mul(mid_yclk, bw_int_to_fixed(1000)));
		else
			calcs_output->yclk_khz = bw_fixed_to_int(
				bw_mul(high_yclk, bw_int_to_fixed(1000)));

		/* units: nanosecond, 16bit storage. */

		calcs_output->nbp_state_change_wm_ns[0].a_mark =
			bw_fixed_to_int(bw_mul(data->
				nbp_state_change_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[1].a_mark =
			bw_fixed_to_int(bw_mul(data->
				nbp_state_change_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[2].a_mark =
			bw_fixed_to_int(bw_mul(data->
				nbp_state_change_watermark[6], bw_int_to_fixed(1000)));

		if (ctx->dc->caps.max_slave_planes) {
			calcs_output->nbp_state_change_wm_ns[3].a_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[0], bw_int_to_fixed(1000)));
			calcs_output->nbp_state_change_wm_ns[4].a_mark =
				bw_fixed_to_int(bw_mul(data->
							nbp_state_change_watermark[1], bw_int_to_fixed(1000)));
		} else {
			calcs_output->nbp_state_change_wm_ns[3].a_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[7], bw_int_to_fixed(1000)));
			calcs_output->nbp_state_change_wm_ns[4].a_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[8], bw_int_to_fixed(1000)));
		}
		calcs_output->nbp_state_change_wm_ns[5].a_mark =
			bw_fixed_to_int(bw_mul(data->
				nbp_state_change_watermark[9], bw_int_to_fixed(1000)));



		calcs_output->stutter_exit_wm_ns[0].a_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_exit_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[1].a_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_exit_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[2].a_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_exit_watermark[6], bw_int_to_fixed(1000)));
		if (ctx->dc->caps.max_slave_planes) {
			calcs_output->stutter_exit_wm_ns[3].a_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[0], bw_int_to_fixed(1000)));
			calcs_output->stutter_exit_wm_ns[4].a_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[1], bw_int_to_fixed(1000)));
		} else {
			calcs_output->stutter_exit_wm_ns[3].a_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[7], bw_int_to_fixed(1000)));
			calcs_output->stutter_exit_wm_ns[4].a_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[8], bw_int_to_fixed(1000)));
		}
		calcs_output->stutter_exit_wm_ns[5].a_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_exit_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->stutter_entry_wm_ns[0].a_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->stutter_entry_wm_ns[1].a_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->stutter_entry_wm_ns[2].a_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[6], bw_int_to_fixed(1000)));
		if (ctx->dc->caps.max_slave_planes) {
			calcs_output->stutter_entry_wm_ns[3].a_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[0], bw_int_to_fixed(1000)));
			calcs_output->stutter_entry_wm_ns[4].a_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[1], bw_int_to_fixed(1000)));
		} else {
			calcs_output->stutter_entry_wm_ns[3].a_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[7], bw_int_to_fixed(1000)));
			calcs_output->stutter_entry_wm_ns[4].a_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[8], bw_int_to_fixed(1000)));
		}
		calcs_output->stutter_entry_wm_ns[5].a_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->urgent_wm_ns[0].a_mark =
			bw_fixed_to_int(bw_mul(data->
				urgent_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[1].a_mark =
			bw_fixed_to_int(bw_mul(data->
				urgent_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[2].a_mark =
			bw_fixed_to_int(bw_mul(data->
				urgent_watermark[6], bw_int_to_fixed(1000)));
		if (ctx->dc->caps.max_slave_planes) {
			calcs_output->urgent_wm_ns[3].a_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[0], bw_int_to_fixed(1000)));
			calcs_output->urgent_wm_ns[4].a_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[1], bw_int_to_fixed(1000)));
		} else {
			calcs_output->urgent_wm_ns[3].a_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[7], bw_int_to_fixed(1000)));
			calcs_output->urgent_wm_ns[4].a_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[8], bw_int_to_fixed(1000)));
		}
		calcs_output->urgent_wm_ns[5].a_mark =
			bw_fixed_to_int(bw_mul(data->
				urgent_watermark[9], bw_int_to_fixed(1000)));

		if (dceip->version != BW_CALCS_VERSION_CARRIZO) {
			((struct bw_calcs_vbios *)vbios)->low_sclk = mid3_sclk;
			((struct bw_calcs_vbios *)vbios)->mid1_sclk = mid3_sclk;
			((struct bw_calcs_vbios *)vbios)->mid2_sclk = mid3_sclk;
			calculate_bandwidth(dceip, vbios, data);

			calcs_output->nbp_state_change_wm_ns[0].b_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[4],bw_int_to_fixed(1000)));
			calcs_output->nbp_state_change_wm_ns[1].b_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[5], bw_int_to_fixed(1000)));
			calcs_output->nbp_state_change_wm_ns[2].b_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[6], bw_int_to_fixed(1000)));

			if (ctx->dc->caps.max_slave_planes) {
				calcs_output->nbp_state_change_wm_ns[3].b_mark =
					bw_fixed_to_int(bw_mul(data->
						nbp_state_change_watermark[0], bw_int_to_fixed(1000)));
				calcs_output->nbp_state_change_wm_ns[4].b_mark =
					bw_fixed_to_int(bw_mul(data->
						nbp_state_change_watermark[1], bw_int_to_fixed(1000)));
			} else {
				calcs_output->nbp_state_change_wm_ns[3].b_mark =
					bw_fixed_to_int(bw_mul(data->
						nbp_state_change_watermark[7], bw_int_to_fixed(1000)));
				calcs_output->nbp_state_change_wm_ns[4].b_mark =
					bw_fixed_to_int(bw_mul(data->
						nbp_state_change_watermark[8], bw_int_to_fixed(1000)));
			}
			calcs_output->nbp_state_change_wm_ns[5].b_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[9], bw_int_to_fixed(1000)));



			calcs_output->stutter_exit_wm_ns[0].b_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[4], bw_int_to_fixed(1000)));
			calcs_output->stutter_exit_wm_ns[1].b_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[5], bw_int_to_fixed(1000)));
			calcs_output->stutter_exit_wm_ns[2].b_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[6], bw_int_to_fixed(1000)));
			if (ctx->dc->caps.max_slave_planes) {
				calcs_output->stutter_exit_wm_ns[3].b_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_exit_watermark[0], bw_int_to_fixed(1000)));
				calcs_output->stutter_exit_wm_ns[4].b_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_exit_watermark[1], bw_int_to_fixed(1000)));
			} else {
				calcs_output->stutter_exit_wm_ns[3].b_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_exit_watermark[7], bw_int_to_fixed(1000)));
				calcs_output->stutter_exit_wm_ns[4].b_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_exit_watermark[8], bw_int_to_fixed(1000)));
			}
			calcs_output->stutter_exit_wm_ns[5].b_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[9], bw_int_to_fixed(1000)));

			calcs_output->stutter_entry_wm_ns[0].b_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[4], bw_int_to_fixed(1000)));
			calcs_output->stutter_entry_wm_ns[1].b_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[5], bw_int_to_fixed(1000)));
			calcs_output->stutter_entry_wm_ns[2].b_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[6], bw_int_to_fixed(1000)));
			if (ctx->dc->caps.max_slave_planes) {
				calcs_output->stutter_entry_wm_ns[3].b_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_entry_watermark[0], bw_int_to_fixed(1000)));
				calcs_output->stutter_entry_wm_ns[4].b_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_entry_watermark[1], bw_int_to_fixed(1000)));
			} else {
				calcs_output->stutter_entry_wm_ns[3].b_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_entry_watermark[7], bw_int_to_fixed(1000)));
				calcs_output->stutter_entry_wm_ns[4].b_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_entry_watermark[8], bw_int_to_fixed(1000)));
			}
			calcs_output->stutter_entry_wm_ns[5].b_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[9], bw_int_to_fixed(1000)));

			calcs_output->urgent_wm_ns[0].b_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[4], bw_int_to_fixed(1000)));
			calcs_output->urgent_wm_ns[1].b_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[5], bw_int_to_fixed(1000)));
			calcs_output->urgent_wm_ns[2].b_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[6], bw_int_to_fixed(1000)));
			if (ctx->dc->caps.max_slave_planes) {
				calcs_output->urgent_wm_ns[3].b_mark =
					bw_fixed_to_int(bw_mul(data->
						urgent_watermark[0], bw_int_to_fixed(1000)));
				calcs_output->urgent_wm_ns[4].b_mark =
					bw_fixed_to_int(bw_mul(data->
						urgent_watermark[1], bw_int_to_fixed(1000)));
			} else {
				calcs_output->urgent_wm_ns[3].b_mark =
					bw_fixed_to_int(bw_mul(data->
						urgent_watermark[7], bw_int_to_fixed(1000)));
				calcs_output->urgent_wm_ns[4].b_mark =
					bw_fixed_to_int(bw_mul(data->
						urgent_watermark[8], bw_int_to_fixed(1000)));
			}
			calcs_output->urgent_wm_ns[5].b_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[9], bw_int_to_fixed(1000)));

			((struct bw_calcs_vbios *)vbios)->low_sclk = low_sclk;
			((struct bw_calcs_vbios *)vbios)->mid1_sclk = mid1_sclk;
			((struct bw_calcs_vbios *)vbios)->mid2_sclk = mid2_sclk;
			((struct bw_calcs_vbios *)vbios)->low_yclk = mid_yclk;
			calculate_bandwidth(dceip, vbios, data);

			calcs_output->nbp_state_change_wm_ns[0].c_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[4], bw_int_to_fixed(1000)));
			calcs_output->nbp_state_change_wm_ns[1].c_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[5], bw_int_to_fixed(1000)));
			calcs_output->nbp_state_change_wm_ns[2].c_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[6], bw_int_to_fixed(1000)));
			if (ctx->dc->caps.max_slave_planes) {
				calcs_output->nbp_state_change_wm_ns[3].c_mark =
					bw_fixed_to_int(bw_mul(data->
						nbp_state_change_watermark[0], bw_int_to_fixed(1000)));
				calcs_output->nbp_state_change_wm_ns[4].c_mark =
					bw_fixed_to_int(bw_mul(data->
						nbp_state_change_watermark[1], bw_int_to_fixed(1000)));
			} else {
				calcs_output->nbp_state_change_wm_ns[3].c_mark =
					bw_fixed_to_int(bw_mul(data->
						nbp_state_change_watermark[7], bw_int_to_fixed(1000)));
				calcs_output->nbp_state_change_wm_ns[4].c_mark =
					bw_fixed_to_int(bw_mul(data->
						nbp_state_change_watermark[8], bw_int_to_fixed(1000)));
			}
			calcs_output->nbp_state_change_wm_ns[5].c_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[9], bw_int_to_fixed(1000)));


			calcs_output->stutter_exit_wm_ns[0].c_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[4], bw_int_to_fixed(1000)));
			calcs_output->stutter_exit_wm_ns[1].c_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[5], bw_int_to_fixed(1000)));
			calcs_output->stutter_exit_wm_ns[2].c_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[6], bw_int_to_fixed(1000)));
			if (ctx->dc->caps.max_slave_planes) {
				calcs_output->stutter_exit_wm_ns[3].c_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_exit_watermark[0], bw_int_to_fixed(1000)));
				calcs_output->stutter_exit_wm_ns[4].c_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_exit_watermark[1], bw_int_to_fixed(1000)));
			} else {
				calcs_output->stutter_exit_wm_ns[3].c_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_exit_watermark[7], bw_int_to_fixed(1000)));
				calcs_output->stutter_exit_wm_ns[4].c_mark =
					bw_fixed_to_int(bw_mul(data->
						stutter_exit_watermark[8], bw_int_to_fixed(1000)));
			}
			calcs_output->stutter_exit_wm_ns[5].c_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->stutter_entry_wm_ns[0].c_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->stutter_entry_wm_ns[1].c_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->stutter_entry_wm_ns[2].c_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[6], bw_int_to_fixed(1000)));
		if (ctx->dc->caps.max_slave_planes) {
			calcs_output->stutter_entry_wm_ns[3].c_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[0], bw_int_to_fixed(1000)));
			calcs_output->stutter_entry_wm_ns[4].c_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[1], bw_int_to_fixed(1000)));
		} else {
			calcs_output->stutter_entry_wm_ns[3].c_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[7], bw_int_to_fixed(1000)));
			calcs_output->stutter_entry_wm_ns[4].c_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[8], bw_int_to_fixed(1000)));
		}
		calcs_output->stutter_entry_wm_ns[5].c_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[9], bw_int_to_fixed(1000)));

			calcs_output->urgent_wm_ns[0].c_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[4], bw_int_to_fixed(1000)));
			calcs_output->urgent_wm_ns[1].c_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[5], bw_int_to_fixed(1000)));
			calcs_output->urgent_wm_ns[2].c_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[6], bw_int_to_fixed(1000)));
			if (ctx->dc->caps.max_slave_planes) {
				calcs_output->urgent_wm_ns[3].c_mark =
					bw_fixed_to_int(bw_mul(data->
						urgent_watermark[0], bw_int_to_fixed(1000)));
				calcs_output->urgent_wm_ns[4].c_mark =
					bw_fixed_to_int(bw_mul(data->
						urgent_watermark[1], bw_int_to_fixed(1000)));
			} else {
				calcs_output->urgent_wm_ns[3].c_mark =
					bw_fixed_to_int(bw_mul(data->
						urgent_watermark[7], bw_int_to_fixed(1000)));
				calcs_output->urgent_wm_ns[4].c_mark =
					bw_fixed_to_int(bw_mul(data->
						urgent_watermark[8], bw_int_to_fixed(1000)));
			}
			calcs_output->urgent_wm_ns[5].c_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[9], bw_int_to_fixed(1000)));
		}

		if (dceip->version == BW_CALCS_VERSION_CARRIZO) {
			((struct bw_calcs_vbios *)vbios)->low_yclk = high_yclk;
			((struct bw_calcs_vbios *)vbios)->mid_yclk = high_yclk;
			((struct bw_calcs_vbios *)vbios)->low_sclk = high_sclk;
			((struct bw_calcs_vbios *)vbios)->mid1_sclk = high_sclk;
			((struct bw_calcs_vbios *)vbios)->mid2_sclk = high_sclk;
			((struct bw_calcs_vbios *)vbios)->mid3_sclk = high_sclk;
			((struct bw_calcs_vbios *)vbios)->mid4_sclk = high_sclk;
			((struct bw_calcs_vbios *)vbios)->mid5_sclk = high_sclk;
			((struct bw_calcs_vbios *)vbios)->mid6_sclk = high_sclk;
		} else {
			((struct bw_calcs_vbios *)vbios)->low_yclk = mid_yclk;
			((struct bw_calcs_vbios *)vbios)->low_sclk = mid3_sclk;
			((struct bw_calcs_vbios *)vbios)->mid1_sclk = mid3_sclk;
			((struct bw_calcs_vbios *)vbios)->mid2_sclk = mid3_sclk;
		}

		calculate_bandwidth(dceip, vbios, data);

		calcs_output->nbp_state_change_wm_ns[0].d_mark =
			bw_fixed_to_int(bw_mul(data->
				nbp_state_change_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[1].d_mark =
			bw_fixed_to_int(bw_mul(data->
				nbp_state_change_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->nbp_state_change_wm_ns[2].d_mark =
			bw_fixed_to_int(bw_mul(data->
				nbp_state_change_watermark[6], bw_int_to_fixed(1000)));
		if (ctx->dc->caps.max_slave_planes) {
			calcs_output->nbp_state_change_wm_ns[3].d_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[0], bw_int_to_fixed(1000)));
			calcs_output->nbp_state_change_wm_ns[4].d_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[1], bw_int_to_fixed(1000)));
		} else {
			calcs_output->nbp_state_change_wm_ns[3].d_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[7], bw_int_to_fixed(1000)));
			calcs_output->nbp_state_change_wm_ns[4].d_mark =
				bw_fixed_to_int(bw_mul(data->
					nbp_state_change_watermark[8], bw_int_to_fixed(1000)));
		}
		calcs_output->nbp_state_change_wm_ns[5].d_mark =
			bw_fixed_to_int(bw_mul(data->
				nbp_state_change_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->stutter_exit_wm_ns[0].d_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_exit_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[1].d_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_exit_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->stutter_exit_wm_ns[2].d_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_exit_watermark[6], bw_int_to_fixed(1000)));
		if (ctx->dc->caps.max_slave_planes) {
			calcs_output->stutter_exit_wm_ns[3].d_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[0], bw_int_to_fixed(1000)));
			calcs_output->stutter_exit_wm_ns[4].d_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[1], bw_int_to_fixed(1000)));
		} else {
			calcs_output->stutter_exit_wm_ns[3].d_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[7], bw_int_to_fixed(1000)));
			calcs_output->stutter_exit_wm_ns[4].d_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_exit_watermark[8], bw_int_to_fixed(1000)));
		}
		calcs_output->stutter_exit_wm_ns[5].d_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_exit_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->stutter_entry_wm_ns[0].d_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->stutter_entry_wm_ns[1].d_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->stutter_entry_wm_ns[2].d_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[6], bw_int_to_fixed(1000)));
		if (ctx->dc->caps.max_slave_planes) {
			calcs_output->stutter_entry_wm_ns[3].d_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[0], bw_int_to_fixed(1000)));
			calcs_output->stutter_entry_wm_ns[4].d_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[1], bw_int_to_fixed(1000)));
		} else {
			calcs_output->stutter_entry_wm_ns[3].d_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[7], bw_int_to_fixed(1000)));
			calcs_output->stutter_entry_wm_ns[4].d_mark =
				bw_fixed_to_int(bw_mul(data->
					stutter_entry_watermark[8], bw_int_to_fixed(1000)));
		}
		calcs_output->stutter_entry_wm_ns[5].d_mark =
			bw_fixed_to_int(bw_mul(data->
				stutter_entry_watermark[9], bw_int_to_fixed(1000)));

		calcs_output->urgent_wm_ns[0].d_mark =
			bw_fixed_to_int(bw_mul(data->
				urgent_watermark[4], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[1].d_mark =
			bw_fixed_to_int(bw_mul(data->
				urgent_watermark[5], bw_int_to_fixed(1000)));
		calcs_output->urgent_wm_ns[2].d_mark =
			bw_fixed_to_int(bw_mul(data->
				urgent_watermark[6], bw_int_to_fixed(1000)));
		if (ctx->dc->caps.max_slave_planes) {
			calcs_output->urgent_wm_ns[3].d_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[0], bw_int_to_fixed(1000)));
			calcs_output->urgent_wm_ns[4].d_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[1], bw_int_to_fixed(1000)));
		} else {
			calcs_output->urgent_wm_ns[3].d_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[7], bw_int_to_fixed(1000)));
			calcs_output->urgent_wm_ns[4].d_mark =
				bw_fixed_to_int(bw_mul(data->
					urgent_watermark[8], bw_int_to_fixed(1000)));
		}
		calcs_output->urgent_wm_ns[5].d_mark =
			bw_fixed_to_int(bw_mul(data->
				urgent_watermark[9], bw_int_to_fixed(1000)));

		((struct bw_calcs_vbios *)vbios)->low_yclk = low_yclk;
		((struct bw_calcs_vbios *)vbios)->mid_yclk = mid_yclk;
		((struct bw_calcs_vbios *)vbios)->low_sclk = low_sclk;
		((struct bw_calcs_vbios *)vbios)->mid1_sclk = mid1_sclk;
		((struct bw_calcs_vbios *)vbios)->mid2_sclk = mid2_sclk;
		((struct bw_calcs_vbios *)vbios)->mid3_sclk = mid3_sclk;
		((struct bw_calcs_vbios *)vbios)->mid4_sclk = mid4_sclk;
		((struct bw_calcs_vbios *)vbios)->mid5_sclk = mid5_sclk;
		((struct bw_calcs_vbios *)vbios)->mid6_sclk = mid6_sclk;
		((struct bw_calcs_vbios *)vbios)->high_sclk = high_sclk;
	} else {
		calcs_output->nbp_state_change_enable = true;
		calcs_output->cpuc_state_change_enable = true;
		calcs_output->cpup_state_change_enable = true;
		calcs_output->stutter_mode_enable = true;
		calcs_output->dispclk_khz = 0;
		calcs_output->sclk_khz = 0;
	}

	kfree(data);

	return is_display_configuration_supported(vbios, calcs_output);
}
