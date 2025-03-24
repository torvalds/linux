/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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
#include "dm_services.h"

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
/* TODO: this needs to be looked at, used by Stella's workaround*/
#include "gmc/gmc_8_2_d.h"
#include "gmc/gmc_8_2_sh_mask.h"

#include "include/logger_interface.h"
#include "inc/dce_calcs.h"

#include "dce/dce_mem_input.h"
#include "dce110_mem_input_v.h"

static void set_flip_control(
	struct dce_mem_input *mem_input110,
	bool immediate)
{
	uint32_t value = 0;

	value = dm_read_reg(
			mem_input110->base.ctx,
			mmUNP_FLIP_CONTROL);

	set_reg_field_value(value, 1,
			UNP_FLIP_CONTROL,
			GRPH_SURFACE_UPDATE_PENDING_MODE);

	dm_write_reg(
			mem_input110->base.ctx,
			mmUNP_FLIP_CONTROL,
			value);
}

/* chroma part */
static void program_pri_addr_c(
	struct dce_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp = 0;
	/*high register MUST be programmed first*/
	temp = address.high_part &
UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C__GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C_MASK;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C);

	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_C,
		value);

	value = 0;
	temp = address.low_part >>
	UNP_GRPH_PRIMARY_SURFACE_ADDRESS_C__GRPH_PRIMARY_SURFACE_ADDRESS_C__SHIFT;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_C,
		GRPH_PRIMARY_SURFACE_ADDRESS_C);

	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_C,
		value);
}

/* luma part */
static void program_pri_addr_l(
	struct dce_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp = 0;

	/*high register MUST be programmed first*/
	temp = address.high_part &
UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L__GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L_MASK;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L);

	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_L,
		value);

	value = 0;
	temp = address.low_part >>
	UNP_GRPH_PRIMARY_SURFACE_ADDRESS_L__GRPH_PRIMARY_SURFACE_ADDRESS_L__SHIFT;

	set_reg_field_value(value, temp,
		UNP_GRPH_PRIMARY_SURFACE_ADDRESS_L,
		GRPH_PRIMARY_SURFACE_ADDRESS_L);

	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_PRIMARY_SURFACE_ADDRESS_L,
		value);
}

static void program_addr(
	struct dce_mem_input *mem_input110,
	const struct dc_plane_address *addr)
{
	switch (addr->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		program_pri_addr_l(
			mem_input110,
			addr->grph.addr);
		break;
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE:
		program_pri_addr_c(
			mem_input110,
			addr->video_progressive.chroma_addr);
		program_pri_addr_l(
			mem_input110,
			addr->video_progressive.luma_addr);
		break;
	default:
		/* not supported */
		BREAK_TO_DEBUGGER();
	}
}

static void enable(struct dce_mem_input *mem_input110)
{
	uint32_t value = 0;

	value = dm_read_reg(mem_input110->base.ctx, mmUNP_GRPH_ENABLE);
	set_reg_field_value(value, 1, UNP_GRPH_ENABLE, GRPH_ENABLE);
	dm_write_reg(mem_input110->base.ctx,
		mmUNP_GRPH_ENABLE,
		value);
}

static void program_tiling(
	struct dce_mem_input *mem_input110,
	const struct dc_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	uint32_t value = 0;

	set_reg_field_value(value, info->gfx8.num_banks,
		UNP_GRPH_CONTROL, GRPH_NUM_BANKS);

	set_reg_field_value(value, info->gfx8.bank_width,
		UNP_GRPH_CONTROL, GRPH_BANK_WIDTH_L);

	set_reg_field_value(value, info->gfx8.bank_height,
		UNP_GRPH_CONTROL, GRPH_BANK_HEIGHT_L);

	set_reg_field_value(value, info->gfx8.tile_aspect,
		UNP_GRPH_CONTROL, GRPH_MACRO_TILE_ASPECT_L);

	set_reg_field_value(value, info->gfx8.tile_split,
		UNP_GRPH_CONTROL, GRPH_TILE_SPLIT_L);

	set_reg_field_value(value, info->gfx8.tile_mode,
		UNP_GRPH_CONTROL, GRPH_MICRO_TILE_MODE_L);

	set_reg_field_value(value, info->gfx8.pipe_config,
		UNP_GRPH_CONTROL, GRPH_PIPE_CONFIG);

	set_reg_field_value(value, info->gfx8.array_mode,
		UNP_GRPH_CONTROL, GRPH_ARRAY_MODE);

	set_reg_field_value(value, 1,
		UNP_GRPH_CONTROL, GRPH_COLOR_EXPANSION_MODE);

	set_reg_field_value(value, 0,
		UNP_GRPH_CONTROL, GRPH_Z);

	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_CONTROL,
		value);

	value = 0;

	set_reg_field_value(value, info->gfx8.bank_width_c,
		UNP_GRPH_CONTROL_C, GRPH_BANK_WIDTH_C);

	set_reg_field_value(value, info->gfx8.bank_height_c,
		UNP_GRPH_CONTROL_C, GRPH_BANK_HEIGHT_C);

	set_reg_field_value(value, info->gfx8.tile_aspect_c,
		UNP_GRPH_CONTROL_C, GRPH_MACRO_TILE_ASPECT_C);

	set_reg_field_value(value, info->gfx8.tile_split_c,
		UNP_GRPH_CONTROL_C, GRPH_TILE_SPLIT_C);

	set_reg_field_value(value, info->gfx8.tile_mode_c,
		UNP_GRPH_CONTROL_C, GRPH_MICRO_TILE_MODE_C);

	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_CONTROL_C,
		value);
}

static void program_size_and_rotation(
	struct dce_mem_input *mem_input110,
	enum dc_rotation_angle rotation,
	const struct plane_size *plane_size)
{
	uint32_t value = 0;
	struct plane_size local_size = *plane_size;

	if (rotation == ROTATION_ANGLE_90 ||
		rotation == ROTATION_ANGLE_270) {

		swap(local_size.surface_size.x,
		     local_size.surface_size.y);
		swap(local_size.surface_size.width,
		     local_size.surface_size.height);
		swap(local_size.chroma_size.x,
		     local_size.chroma_size.y);
		swap(local_size.chroma_size.width,
		     local_size.chroma_size.height);
	}

	value = 0;
	set_reg_field_value(value, local_size.surface_pitch,
			UNP_GRPH_PITCH_L, GRPH_PITCH_L);

	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_PITCH_L,
		value);

	value = 0;
	set_reg_field_value(value, local_size.chroma_pitch,
			UNP_GRPH_PITCH_C, GRPH_PITCH_C);
	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_PITCH_C,
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_X_START_L, GRPH_X_START_L);
	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_X_START_L,
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_X_START_C, GRPH_X_START_C);
	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_X_START_C,
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_Y_START_L, GRPH_Y_START_L);
	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_Y_START_L,
		value);

	value = 0;
	set_reg_field_value(value, 0,
			UNP_GRPH_Y_START_C, GRPH_Y_START_C);
	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_Y_START_C,
		value);

	value = 0;
	set_reg_field_value(value, local_size.surface_size.x +
			local_size.surface_size.width,
			UNP_GRPH_X_END_L, GRPH_X_END_L);
	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_X_END_L,
		value);

	value = 0;
	set_reg_field_value(value, local_size.chroma_size.x +
			local_size.chroma_size.width,
			UNP_GRPH_X_END_C, GRPH_X_END_C);
	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_X_END_C,
		value);

	value = 0;
	set_reg_field_value(value, local_size.surface_size.y +
			local_size.surface_size.height,
			UNP_GRPH_Y_END_L, GRPH_Y_END_L);
	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_Y_END_L,
		value);

	value = 0;
	set_reg_field_value(value, local_size.chroma_size.y +
			local_size.chroma_size.height,
			UNP_GRPH_Y_END_C, GRPH_Y_END_C);
	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_GRPH_Y_END_C,
		value);

	value = 0;
	switch (rotation) {
	case ROTATION_ANGLE_90:
		set_reg_field_value(value, 3,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	case ROTATION_ANGLE_180:
		set_reg_field_value(value, 2,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	case ROTATION_ANGLE_270:
		set_reg_field_value(value, 1,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	default:
		set_reg_field_value(value, 0,
			UNP_HW_ROTATION, ROTATION_ANGLE);
		break;
	}

	dm_write_reg(
		mem_input110->base.ctx,
		mmUNP_HW_ROTATION,
		value);
}

static void program_pixel_format(
	struct dce_mem_input *mem_input110,
	enum surface_pixel_format format)
{
	if (format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		uint32_t value;
		uint8_t grph_depth;
		uint8_t grph_format;

		value =	dm_read_reg(
				mem_input110->base.ctx,
				mmUNP_GRPH_CONTROL);

		switch (format) {
		case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
			grph_depth = 0;
			grph_format = 0;
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
			grph_depth = 1;
			grph_format = 1;
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
			grph_depth = 2;
			grph_format = 0;
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
			grph_depth = 2;
			grph_format = 1;
			break;
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
		case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
			grph_depth = 3;
			grph_format = 0;
			break;
		default:
			grph_depth = 2;
			grph_format = 0;
			break;
		}

		set_reg_field_value(
				value,
				grph_depth,
				UNP_GRPH_CONTROL,
				GRPH_DEPTH);
		set_reg_field_value(
				value,
				grph_format,
				UNP_GRPH_CONTROL,
				GRPH_FORMAT);

		dm_write_reg(
				mem_input110->base.ctx,
				mmUNP_GRPH_CONTROL,
				value);

		value =	dm_read_reg(
				mem_input110->base.ctx,
				mmUNP_GRPH_CONTROL_EXP);

		/* VIDEO FORMAT 0 */
		set_reg_field_value(
				value,
				0,
				UNP_GRPH_CONTROL_EXP,
				VIDEO_FORMAT);
		dm_write_reg(
				mem_input110->base.ctx,
				mmUNP_GRPH_CONTROL_EXP,
				value);

	} else {
		/* Video 422 and 420 needs UNP_GRPH_CONTROL_EXP programmed */
		uint32_t value;
		uint8_t video_format;

		value =	dm_read_reg(
				mem_input110->base.ctx,
				mmUNP_GRPH_CONTROL_EXP);

		switch (format) {
		case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
			video_format = 2;
			break;
		case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
			video_format = 3;
			break;
		default:
			video_format = 0;
			break;
		}

		set_reg_field_value(
			value,
			video_format,
			UNP_GRPH_CONTROL_EXP,
			VIDEO_FORMAT);

		dm_write_reg(
			mem_input110->base.ctx,
			mmUNP_GRPH_CONTROL_EXP,
			value);
	}
}

static bool dce_mem_input_v_is_surface_pending(struct mem_input *mem_input)
{
	struct dce_mem_input *mem_input110 = TO_DCE_MEM_INPUT(mem_input);
	uint32_t value;

	value = dm_read_reg(mem_input110->base.ctx, mmUNP_GRPH_UPDATE);

	if (get_reg_field_value(value, UNP_GRPH_UPDATE,
			GRPH_SURFACE_UPDATE_PENDING))
		return true;

	mem_input->current_address = mem_input->request_address;
	return false;
}

static bool dce_mem_input_v_program_surface_flip_and_addr(
	struct mem_input *mem_input,
	const struct dc_plane_address *address,
	bool flip_immediate)
{
	struct dce_mem_input *mem_input110 = TO_DCE_MEM_INPUT(mem_input);

	set_flip_control(mem_input110, flip_immediate);
	program_addr(mem_input110,
		address);

	mem_input->request_address = *address;

	return true;
}

/* Scatter Gather param tables */
static const unsigned int dvmm_Hw_Setting_2DTiling[4][9] = {
		{  8, 64, 64,  8,  8, 1, 4, 0, 0},
		{ 16, 64, 32,  8, 16, 1, 8, 0, 0},
		{ 32, 32, 32, 16, 16, 1, 8, 0, 0},
		{ 64,  8, 32, 16, 16, 1, 8, 0, 0}, /* fake */
};

static const unsigned int dvmm_Hw_Setting_1DTiling[4][9] = {
		{  8, 512, 8, 1, 0, 1, 0, 0, 0},  /* 0 for invalid */
		{ 16, 256, 8, 2, 0, 1, 0, 0, 0},
		{ 32, 128, 8, 4, 0, 1, 0, 0, 0},
		{ 64,  64, 8, 4, 0, 1, 0, 0, 0}, /* fake */
};

static const unsigned int dvmm_Hw_Setting_Linear[4][9] = {
		{  8, 4096, 1, 8, 0, 1, 0, 0, 0},
		{ 16, 2048, 1, 8, 0, 1, 0, 0, 0},
		{ 32, 1024, 1, 8, 0, 1, 0, 0, 0},
		{ 64,  512, 1, 8, 0, 1, 0, 0, 0}, /* new for 64bpp from HW */
};

/* Helper to get table entry from surface info */
static const unsigned int *get_dvmm_hw_setting(
		struct dc_tiling_info *tiling_info,
		enum surface_pixel_format format,
		bool chroma)
{
	enum bits_per_pixel {
		bpp_8 = 0,
		bpp_16,
		bpp_32,
		bpp_64
	} bpp;

	if (format >= SURFACE_PIXEL_FORMAT_INVALID)
		bpp = bpp_32;
	else if (format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
		bpp = chroma ? bpp_16 : bpp_8;
	else
		bpp = bpp_8;

	switch (tiling_info->gfx8.array_mode) {
	case DC_ARRAY_1D_TILED_THIN1:
	case DC_ARRAY_1D_TILED_THICK:
	case DC_ARRAY_PRT_TILED_THIN1:
		return dvmm_Hw_Setting_1DTiling[bpp];
	case DC_ARRAY_2D_TILED_THIN1:
	case DC_ARRAY_2D_TILED_THICK:
	case DC_ARRAY_2D_TILED_X_THICK:
	case DC_ARRAY_PRT_2D_TILED_THIN1:
	case DC_ARRAY_PRT_2D_TILED_THICK:
		return dvmm_Hw_Setting_2DTiling[bpp];
	case DC_ARRAY_LINEAR_GENERAL:
	case DC_ARRAY_LINEAR_ALLIGNED:
		return dvmm_Hw_Setting_Linear[bpp];
	default:
		return dvmm_Hw_Setting_2DTiling[bpp];
	}
}

static void dce_mem_input_v_program_pte_vm(
		struct mem_input *mem_input,
		enum surface_pixel_format format,
		struct dc_tiling_info *tiling_info,
		enum dc_rotation_angle rotation)
{
	struct dce_mem_input *mem_input110 = TO_DCE_MEM_INPUT(mem_input);
	const unsigned int *pte = get_dvmm_hw_setting(tiling_info, format, false);
	const unsigned int *pte_chroma = get_dvmm_hw_setting(tiling_info, format, true);

	unsigned int page_width = 0;
	unsigned int page_height = 0;
	unsigned int page_width_chroma = 0;
	unsigned int page_height_chroma = 0;
	unsigned int temp_page_width = pte[1];
	unsigned int temp_page_height = pte[2];
	unsigned int min_pte_before_flip = 0;
	unsigned int min_pte_before_flip_chroma = 0;
	uint32_t value = 0;

	while ((temp_page_width >>= 1) != 0)
		page_width++;
	while ((temp_page_height >>= 1) != 0)
		page_height++;

	temp_page_width = pte_chroma[1];
	temp_page_height = pte_chroma[2];
	while ((temp_page_width >>= 1) != 0)
		page_width_chroma++;
	while ((temp_page_height >>= 1) != 0)
		page_height_chroma++;

	switch (rotation) {
	case ROTATION_ANGLE_90:
	case ROTATION_ANGLE_270:
		min_pte_before_flip = pte[4];
		min_pte_before_flip_chroma = pte_chroma[4];
		break;
	default:
		min_pte_before_flip = pte[3];
		min_pte_before_flip_chroma = pte_chroma[3];
		break;
	}

	value = dm_read_reg(mem_input110->base.ctx, mmUNP_PIPE_OUTSTANDING_REQUEST_LIMIT);
	/* TODO: un-hardcode requestlimit */
	set_reg_field_value(value, 0xff, UNP_PIPE_OUTSTANDING_REQUEST_LIMIT, UNP_PIPE_OUTSTANDING_REQUEST_LIMIT_L);
	set_reg_field_value(value, 0xff, UNP_PIPE_OUTSTANDING_REQUEST_LIMIT, UNP_PIPE_OUTSTANDING_REQUEST_LIMIT_C);
	dm_write_reg(mem_input110->base.ctx, mmUNP_PIPE_OUTSTANDING_REQUEST_LIMIT, value);

	value = dm_read_reg(mem_input110->base.ctx, mmUNP_DVMM_PTE_CONTROL);
	set_reg_field_value(value, page_width, UNP_DVMM_PTE_CONTROL, DVMM_PAGE_WIDTH);
	set_reg_field_value(value, page_height, UNP_DVMM_PTE_CONTROL, DVMM_PAGE_HEIGHT);
	set_reg_field_value(value, min_pte_before_flip, UNP_DVMM_PTE_CONTROL, DVMM_MIN_PTE_BEFORE_FLIP);
	dm_write_reg(mem_input110->base.ctx, mmUNP_DVMM_PTE_CONTROL, value);

	value = dm_read_reg(mem_input110->base.ctx, mmUNP_DVMM_PTE_ARB_CONTROL);
	set_reg_field_value(value, pte[5], UNP_DVMM_PTE_ARB_CONTROL, DVMM_PTE_REQ_PER_CHUNK);
	set_reg_field_value(value, 0xff, UNP_DVMM_PTE_ARB_CONTROL, DVMM_MAX_PTE_REQ_OUTSTANDING);
	dm_write_reg(mem_input110->base.ctx, mmUNP_DVMM_PTE_ARB_CONTROL, value);

	value = dm_read_reg(mem_input110->base.ctx, mmUNP_DVMM_PTE_CONTROL_C);
	set_reg_field_value(value, page_width_chroma, UNP_DVMM_PTE_CONTROL_C, DVMM_PAGE_WIDTH_C);
	set_reg_field_value(value, page_height_chroma, UNP_DVMM_PTE_CONTROL_C, DVMM_PAGE_HEIGHT_C);
	set_reg_field_value(value, min_pte_before_flip_chroma, UNP_DVMM_PTE_CONTROL_C, DVMM_MIN_PTE_BEFORE_FLIP_C);
	dm_write_reg(mem_input110->base.ctx, mmUNP_DVMM_PTE_CONTROL_C, value);

	value = dm_read_reg(mem_input110->base.ctx, mmUNP_DVMM_PTE_ARB_CONTROL_C);
	set_reg_field_value(value, pte_chroma[5], UNP_DVMM_PTE_ARB_CONTROL_C, DVMM_PTE_REQ_PER_CHUNK_C);
	set_reg_field_value(value, 0xff, UNP_DVMM_PTE_ARB_CONTROL_C, DVMM_MAX_PTE_REQ_OUTSTANDING_C);
	dm_write_reg(mem_input110->base.ctx, mmUNP_DVMM_PTE_ARB_CONTROL_C, value);
}

static void dce_mem_input_v_program_surface_config(
	struct mem_input *mem_input,
	enum surface_pixel_format format,
	struct dc_tiling_info *tiling_info,
	struct plane_size *plane_size,
	enum dc_rotation_angle rotation,
	struct dc_plane_dcc_param *dcc,
	bool horizotal_mirror)
{
	struct dce_mem_input *mem_input110 = TO_DCE_MEM_INPUT(mem_input);

	enable(mem_input110);
	program_tiling(mem_input110, tiling_info, format);
	program_size_and_rotation(mem_input110, rotation, plane_size);
	program_pixel_format(mem_input110, format);
}

static void program_urgency_watermark(
	const struct dc_context *ctx,
	const uint32_t urgency_addr,
	const uint32_t wm_addr,
	struct dce_watermarks marks_low,
	uint32_t total_dest_line_time_ns)
{
	/* register value */
	uint32_t urgency_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	/*Write mask to enable reading/writing of watermark set A*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			1,
			DPGV0_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(
		urgency_cntl,
		marks_low.a_mark,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(
		urgency_cntl,
		total_dest_line_time_ns,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dm_write_reg(ctx, urgency_addr, urgency_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			2,
			DPGV0_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(urgency_cntl,
		marks_low.b_mark,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(urgency_cntl,
		total_dest_line_time_ns,
		DPGV0_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);

	dm_write_reg(ctx, urgency_addr, urgency_cntl);
}

static void program_urgency_watermark_l(
	const struct dc_context *ctx,
	struct dce_watermarks marks_low,
	uint32_t total_dest_line_time_ns)
{
	program_urgency_watermark(
		ctx,
		mmDPGV0_PIPE_URGENCY_CONTROL,
		mmDPGV0_WATERMARK_MASK_CONTROL,
		marks_low,
		total_dest_line_time_ns);
}

static void program_urgency_watermark_c(
	const struct dc_context *ctx,
	struct dce_watermarks marks_low,
	uint32_t total_dest_line_time_ns)
{
	program_urgency_watermark(
		ctx,
		mmDPGV1_PIPE_URGENCY_CONTROL,
		mmDPGV1_WATERMARK_MASK_CONTROL,
		marks_low,
		total_dest_line_time_ns);
}

static void program_stutter_watermark(
	const struct dc_context *ctx,
	const uint32_t stutter_addr,
	const uint32_t wm_addr,
	struct dce_watermarks marks)
{
	/* register value */
	uint32_t stutter_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	/*Write mask to enable reading/writing of watermark set A*/

	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		1,
		DPGV0_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);

	if (ctx->dc->debug.disable_stutter) {
		set_reg_field_value(stutter_cntl,
			0,
			DPGV0_PIPE_STUTTER_CONTROL,
			STUTTER_ENABLE);
	} else {
		set_reg_field_value(stutter_cntl,
			1,
			DPGV0_PIPE_STUTTER_CONTROL,
			STUTTER_ENABLE);
	}

	set_reg_field_value(stutter_cntl,
		1,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set A*/
	set_reg_field_value(stutter_cntl,
		marks.a_mark,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		2,
		DPGV0_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);
	/*Write watermark set B*/
	set_reg_field_value(stutter_cntl,
		marks.b_mark,
		DPGV0_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);
}

static void program_stutter_watermark_l(
	const struct dc_context *ctx,
	struct dce_watermarks marks)
{
	program_stutter_watermark(ctx,
			mmDPGV0_PIPE_STUTTER_CONTROL,
			mmDPGV0_WATERMARK_MASK_CONTROL,
			marks);
}

static void program_stutter_watermark_c(
	const struct dc_context *ctx,
	struct dce_watermarks marks)
{
	program_stutter_watermark(ctx,
			mmDPGV1_PIPE_STUTTER_CONTROL,
			mmDPGV1_WATERMARK_MASK_CONTROL,
			marks);
}

static void program_nbp_watermark(
	const struct dc_context *ctx,
	const uint32_t wm_mask_ctrl_addr,
	const uint32_t nbp_pstate_ctrl_addr,
	struct dce_watermarks marks)
{
	uint32_t value;

	/* Write mask to enable reading/writing of watermark set A */

	value = dm_read_reg(ctx, wm_mask_ctrl_addr);

	set_reg_field_value(
		value,
		1,
		DPGV0_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, wm_mask_ctrl_addr, value);

	value = dm_read_reg(ctx, nbp_pstate_ctrl_addr);

	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, nbp_pstate_ctrl_addr, value);

	/* Write watermark set A */
	value = dm_read_reg(ctx, nbp_pstate_ctrl_addr);
	set_reg_field_value(
		value,
		marks.a_mark,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, nbp_pstate_ctrl_addr, value);

	/* Write mask to enable reading/writing of watermark set B */
	value = dm_read_reg(ctx, wm_mask_ctrl_addr);
	set_reg_field_value(
		value,
		2,
		DPGV0_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, wm_mask_ctrl_addr, value);

	value = dm_read_reg(ctx, nbp_pstate_ctrl_addr);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, nbp_pstate_ctrl_addr, value);

	/* Write watermark set B */
	value = dm_read_reg(ctx, nbp_pstate_ctrl_addr);
	set_reg_field_value(
		value,
		marks.b_mark,
		DPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, nbp_pstate_ctrl_addr, value);
}

static void program_nbp_watermark_l(
	const struct dc_context *ctx,
	struct dce_watermarks marks)
{
	program_nbp_watermark(ctx,
			mmDPGV0_WATERMARK_MASK_CONTROL,
			mmDPGV0_PIPE_NB_PSTATE_CHANGE_CONTROL,
			marks);
}

static void program_nbp_watermark_c(
	const struct dc_context *ctx,
	struct dce_watermarks marks)
{
	program_nbp_watermark(ctx,
			mmDPGV1_WATERMARK_MASK_CONTROL,
			mmDPGV1_PIPE_NB_PSTATE_CHANGE_CONTROL,
			marks);
}

static void dce_mem_input_v_program_display_marks(
	struct mem_input *mem_input,
	struct dce_watermarks nbp,
	struct dce_watermarks stutter,
	struct dce_watermarks stutter_enter,
	struct dce_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	program_urgency_watermark_l(
		mem_input->ctx,
		urgent,
		total_dest_line_time_ns);

	program_nbp_watermark_l(
		mem_input->ctx,
		nbp);

	program_stutter_watermark_l(
		mem_input->ctx,
		stutter);

}

static void dce_mem_input_program_chroma_display_marks(
	struct mem_input *mem_input,
	struct dce_watermarks nbp,
	struct dce_watermarks stutter,
	struct dce_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	program_urgency_watermark_c(
		mem_input->ctx,
		urgent,
		total_dest_line_time_ns);

	program_nbp_watermark_c(
		mem_input->ctx,
		nbp);

	program_stutter_watermark_c(
		mem_input->ctx,
		stutter);
}

static void dce110_allocate_mem_input_v(
	struct mem_input *mi,
	uint32_t h_total,/* for current stream */
	uint32_t v_total,/* for current stream */
	uint32_t pix_clk_khz,/* for current stream */
	uint32_t total_stream_num)
{
	uint32_t addr;
	uint32_t value;
	uint32_t pix_dur;
	if (pix_clk_khz != 0) {
		addr = mmDPGV0_PIPE_ARBITRATION_CONTROL1;
		value = dm_read_reg(mi->ctx, addr);
		pix_dur = 1000000000ULL / pix_clk_khz;
		set_reg_field_value(
			value,
			pix_dur,
			DPGV0_PIPE_ARBITRATION_CONTROL1,
			PIXEL_DURATION);
		dm_write_reg(mi->ctx, addr, value);

		addr = mmDPGV1_PIPE_ARBITRATION_CONTROL1;
		value = dm_read_reg(mi->ctx, addr);
		pix_dur = 1000000000ULL / pix_clk_khz;
		set_reg_field_value(
			value,
			pix_dur,
			DPGV1_PIPE_ARBITRATION_CONTROL1,
			PIXEL_DURATION);
		dm_write_reg(mi->ctx, addr, value);

		addr = mmDPGV0_PIPE_ARBITRATION_CONTROL2;
		value = 0x4000800;
		dm_write_reg(mi->ctx, addr, value);

		addr = mmDPGV1_PIPE_ARBITRATION_CONTROL2;
		value = 0x4000800;
		dm_write_reg(mi->ctx, addr, value);
	}

}

static void dce110_free_mem_input_v(
	struct mem_input *mi,
	uint32_t total_stream_num)
{
}

static const struct mem_input_funcs dce110_mem_input_v_funcs = {
	.mem_input_program_display_marks =
			dce_mem_input_v_program_display_marks,
	.mem_input_program_chroma_display_marks =
			dce_mem_input_program_chroma_display_marks,
	.allocate_mem_input = dce110_allocate_mem_input_v,
	.free_mem_input = dce110_free_mem_input_v,
	.mem_input_program_surface_flip_and_addr =
			dce_mem_input_v_program_surface_flip_and_addr,
	.mem_input_program_pte_vm =
			dce_mem_input_v_program_pte_vm,
	.mem_input_program_surface_config =
			dce_mem_input_v_program_surface_config,
	.mem_input_is_flip_pending =
			dce_mem_input_v_is_surface_pending
};
/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

void dce110_mem_input_v_construct(
	struct dce_mem_input *dce_mi,
	struct dc_context *ctx)
{
	dce_mi->base.funcs = &dce110_mem_input_v_funcs;
	dce_mi->base.ctx = ctx;
}

