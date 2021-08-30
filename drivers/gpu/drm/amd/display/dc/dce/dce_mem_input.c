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

#include "dce_mem_input.h"
#include "reg_helper.h"
#include "basics/conversion.h"

#define CTX \
	dce_mi->base.ctx
#define REG(reg)\
	dce_mi->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	dce_mi->shifts->field_name, dce_mi->masks->field_name

struct pte_setting {
	unsigned int bpp;
	unsigned int page_width;
	unsigned int page_height;
	unsigned char min_pte_before_flip_horiz_scan;
	unsigned char min_pte_before_flip_vert_scan;
	unsigned char pte_req_per_chunk;
	unsigned char param_6;
	unsigned char param_7;
	unsigned char param_8;
};

enum mi_bits_per_pixel {
	mi_bpp_8 = 0,
	mi_bpp_16,
	mi_bpp_32,
	mi_bpp_64,
	mi_bpp_count,
};

enum mi_tiling_format {
	mi_tiling_linear = 0,
	mi_tiling_1D,
	mi_tiling_2D,
	mi_tiling_count,
};

static const struct pte_setting pte_settings[mi_tiling_count][mi_bpp_count] = {
	[mi_tiling_linear] = {
		{  8, 4096, 1, 8, 0, 1, 0, 0, 0},
		{ 16, 2048, 1, 8, 0, 1, 0, 0, 0},
		{ 32, 1024, 1, 8, 0, 1, 0, 0, 0},
		{ 64,  512, 1, 8, 0, 1, 0, 0, 0}, /* new for 64bpp from HW */
	},
	[mi_tiling_1D] = {
		{  8, 512, 8, 1, 0, 1, 0, 0, 0},  /* 0 for invalid */
		{ 16, 256, 8, 2, 0, 1, 0, 0, 0},
		{ 32, 128, 8, 4, 0, 1, 0, 0, 0},
		{ 64,  64, 8, 4, 0, 1, 0, 0, 0}, /* fake */
	},
	[mi_tiling_2D] = {
		{  8, 64, 64,  8,  8, 1, 4, 0, 0},
		{ 16, 64, 32,  8, 16, 1, 8, 0, 0},
		{ 32, 32, 32, 16, 16, 1, 8, 0, 0},
		{ 64,  8, 32, 16, 16, 1, 8, 0, 0}, /* fake */
	},
};

static enum mi_bits_per_pixel get_mi_bpp(
		enum surface_pixel_format format)
{
	if (format >= SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616)
		return mi_bpp_64;
	else if (format >= SURFACE_PIXEL_FORMAT_GRPH_ARGB8888)
		return mi_bpp_32;
	else if (format >= SURFACE_PIXEL_FORMAT_GRPH_ARGB1555)
		return mi_bpp_16;
	else
		return mi_bpp_8;
}

static enum mi_tiling_format get_mi_tiling(
		union dc_tiling_info *tiling_info)
{
	switch (tiling_info->gfx8.array_mode) {
	case DC_ARRAY_1D_TILED_THIN1:
	case DC_ARRAY_1D_TILED_THICK:
	case DC_ARRAY_PRT_TILED_THIN1:
		return mi_tiling_1D;
	case DC_ARRAY_2D_TILED_THIN1:
	case DC_ARRAY_2D_TILED_THICK:
	case DC_ARRAY_2D_TILED_X_THICK:
	case DC_ARRAY_PRT_2D_TILED_THIN1:
	case DC_ARRAY_PRT_2D_TILED_THICK:
		return mi_tiling_2D;
	case DC_ARRAY_LINEAR_GENERAL:
	case DC_ARRAY_LINEAR_ALLIGNED:
		return mi_tiling_linear;
	default:
		return mi_tiling_2D;
	}
}

static bool is_vert_scan(enum dc_rotation_angle rotation)
{
	switch (rotation) {
	case ROTATION_ANGLE_90:
	case ROTATION_ANGLE_270:
		return true;
	default:
		return false;
	}
}

static void dce_mi_program_pte_vm(
		struct mem_input *mi,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		enum dc_rotation_angle rotation)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mi);
	enum mi_bits_per_pixel mi_bpp = get_mi_bpp(format);
	enum mi_tiling_format mi_tiling = get_mi_tiling(tiling_info);
	const struct pte_setting *pte = &pte_settings[mi_tiling][mi_bpp];

	unsigned int page_width = log_2(pte->page_width);
	unsigned int page_height = log_2(pte->page_height);
	unsigned int min_pte_before_flip = is_vert_scan(rotation) ?
			pte->min_pte_before_flip_vert_scan :
			pte->min_pte_before_flip_horiz_scan;

	REG_UPDATE(GRPH_PIPE_OUTSTANDING_REQUEST_LIMIT,
			GRPH_PIPE_OUTSTANDING_REQUEST_LIMIT, 0x7f);

	REG_UPDATE_3(DVMM_PTE_CONTROL,
			DVMM_PAGE_WIDTH, page_width,
			DVMM_PAGE_HEIGHT, page_height,
			DVMM_MIN_PTE_BEFORE_FLIP, min_pte_before_flip);

	REG_UPDATE_2(DVMM_PTE_ARB_CONTROL,
			DVMM_PTE_REQ_PER_CHUNK, pte->pte_req_per_chunk,
			DVMM_MAX_PTE_REQ_OUTSTANDING, 0x7f);
}

static void program_urgency_watermark(
	struct dce_mem_input *dce_mi,
	uint32_t wm_select,
	uint32_t urgency_low_wm,
	uint32_t urgency_high_wm)
{
	REG_UPDATE(DPG_WATERMARK_MASK_CONTROL,
		URGENCY_WATERMARK_MASK, wm_select);

	REG_SET_2(DPG_PIPE_URGENCY_CONTROL, 0,
		URGENCY_LOW_WATERMARK, urgency_low_wm,
		URGENCY_HIGH_WATERMARK, urgency_high_wm);
}

#if defined(CONFIG_DRM_AMD_DC_SI)
static void dce60_program_urgency_watermark(
	struct dce_mem_input *dce_mi,
	uint32_t wm_select,
	uint32_t urgency_low_wm,
	uint32_t urgency_high_wm)
{
	REG_UPDATE(DPG_PIPE_ARBITRATION_CONTROL3,
		URGENCY_WATERMARK_MASK, wm_select);

	REG_SET_2(DPG_PIPE_URGENCY_CONTROL, 0,
		URGENCY_LOW_WATERMARK, urgency_low_wm,
		URGENCY_HIGH_WATERMARK, urgency_high_wm);
}
#endif

static void dce120_program_urgency_watermark(
	struct dce_mem_input *dce_mi,
	uint32_t wm_select,
	uint32_t urgency_low_wm,
	uint32_t urgency_high_wm)
{
	REG_UPDATE(DPG_WATERMARK_MASK_CONTROL,
		URGENCY_WATERMARK_MASK, wm_select);

	REG_SET_2(DPG_PIPE_URGENCY_CONTROL, 0,
		URGENCY_LOW_WATERMARK, urgency_low_wm,
		URGENCY_HIGH_WATERMARK, urgency_high_wm);

	REG_SET_2(DPG_PIPE_URGENT_LEVEL_CONTROL, 0,
		URGENT_LEVEL_LOW_WATERMARK, urgency_low_wm,
		URGENT_LEVEL_HIGH_WATERMARK, urgency_high_wm);

}

#if defined(CONFIG_DRM_AMD_DC_SI)
static void dce60_program_nbp_watermark(
	struct dce_mem_input *dce_mi,
	uint32_t wm_select,
	uint32_t nbp_wm)
{
	REG_UPDATE(DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK, wm_select);

	REG_UPDATE_3(DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE, 1,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST, 1,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST, 1);

	REG_UPDATE(DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK, nbp_wm);
}
#endif

static void program_nbp_watermark(
	struct dce_mem_input *dce_mi,
	uint32_t wm_select,
	uint32_t nbp_wm)
{
	if (REG(DPG_PIPE_NB_PSTATE_CHANGE_CONTROL)) {
		REG_UPDATE(DPG_WATERMARK_MASK_CONTROL,
				NB_PSTATE_CHANGE_WATERMARK_MASK, wm_select);

		REG_UPDATE_3(DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
				NB_PSTATE_CHANGE_ENABLE, 1,
				NB_PSTATE_CHANGE_URGENT_DURING_REQUEST, 1,
				NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST, 1);

		REG_UPDATE(DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
				NB_PSTATE_CHANGE_WATERMARK, nbp_wm);
	}

	if (REG(DPG_PIPE_LOW_POWER_CONTROL)) {
		REG_UPDATE(DPG_WATERMARK_MASK_CONTROL,
				PSTATE_CHANGE_WATERMARK_MASK, wm_select);

		REG_UPDATE_3(DPG_PIPE_LOW_POWER_CONTROL,
				PSTATE_CHANGE_ENABLE, 1,
				PSTATE_CHANGE_URGENT_DURING_REQUEST, 1,
				PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST, 1);

		REG_UPDATE(DPG_PIPE_LOW_POWER_CONTROL,
				PSTATE_CHANGE_WATERMARK, nbp_wm);
	}
}

#if defined(CONFIG_DRM_AMD_DC_SI)
static void dce60_program_stutter_watermark(
	struct dce_mem_input *dce_mi,
	uint32_t wm_select,
	uint32_t stutter_mark)
{
	REG_UPDATE(DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK, wm_select);

	REG_UPDATE(DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK, stutter_mark);
}
#endif

static void dce120_program_stutter_watermark(
	struct dce_mem_input *dce_mi,
	uint32_t wm_select,
	uint32_t stutter_mark,
	uint32_t stutter_entry)
{
	REG_UPDATE(DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK, wm_select);

	if (REG(DPG_PIPE_STUTTER_CONTROL2))
		REG_UPDATE_2(DPG_PIPE_STUTTER_CONTROL2,
				STUTTER_EXIT_SELF_REFRESH_WATERMARK, stutter_mark,
				STUTTER_ENTER_SELF_REFRESH_WATERMARK, stutter_entry);
	else
		REG_UPDATE_2(DPG_PIPE_STUTTER_CONTROL,
				STUTTER_EXIT_SELF_REFRESH_WATERMARK, stutter_mark,
				STUTTER_ENTER_SELF_REFRESH_WATERMARK, stutter_entry);
}

static void program_stutter_watermark(
	struct dce_mem_input *dce_mi,
	uint32_t wm_select,
	uint32_t stutter_mark)
{
	REG_UPDATE(DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK, wm_select);

	if (REG(DPG_PIPE_STUTTER_CONTROL2))
		REG_UPDATE(DPG_PIPE_STUTTER_CONTROL2,
				STUTTER_EXIT_SELF_REFRESH_WATERMARK, stutter_mark);
	else
		REG_UPDATE(DPG_PIPE_STUTTER_CONTROL,
				STUTTER_EXIT_SELF_REFRESH_WATERMARK, stutter_mark);
}

static void dce_mi_program_display_marks(
	struct mem_input *mi,
	struct dce_watermarks nbp,
	struct dce_watermarks stutter_exit,
	struct dce_watermarks stutter_enter,
	struct dce_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mi);
	uint32_t stutter_en = mi->ctx->dc->debug.disable_stutter ? 0 : 1;

	program_urgency_watermark(dce_mi, 2, /* set a */
			urgent.a_mark, total_dest_line_time_ns);
	program_urgency_watermark(dce_mi, 1, /* set d */
			urgent.d_mark, total_dest_line_time_ns);

	REG_UPDATE_2(DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE, stutter_en,
		STUTTER_IGNORE_FBC, 1);
	program_nbp_watermark(dce_mi, 2, nbp.a_mark); /* set a */
	program_nbp_watermark(dce_mi, 1, nbp.d_mark); /* set d */

	program_stutter_watermark(dce_mi, 2, stutter_exit.a_mark); /* set a */
	program_stutter_watermark(dce_mi, 1, stutter_exit.d_mark); /* set d */
}

#if defined(CONFIG_DRM_AMD_DC_SI)
static void dce60_mi_program_display_marks(
	struct mem_input *mi,
	struct dce_watermarks nbp,
	struct dce_watermarks stutter_exit,
	struct dce_watermarks stutter_enter,
	struct dce_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mi);
	uint32_t stutter_en = mi->ctx->dc->debug.disable_stutter ? 0 : 1;

	dce60_program_urgency_watermark(dce_mi, 2, /* set a */
			urgent.a_mark, total_dest_line_time_ns);
	dce60_program_urgency_watermark(dce_mi, 1, /* set d */
			urgent.d_mark, total_dest_line_time_ns);

	REG_UPDATE_2(DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE, stutter_en,
		STUTTER_IGNORE_FBC, 1);
	dce60_program_nbp_watermark(dce_mi, 2, nbp.a_mark); /* set a */
	dce60_program_nbp_watermark(dce_mi, 1, nbp.d_mark); /* set d */

	dce60_program_stutter_watermark(dce_mi, 2, stutter_exit.a_mark); /* set a */
	dce60_program_stutter_watermark(dce_mi, 1, stutter_exit.d_mark); /* set d */
}
#endif

static void dce112_mi_program_display_marks(struct mem_input *mi,
	struct dce_watermarks nbp,
	struct dce_watermarks stutter_exit,
	struct dce_watermarks stutter_entry,
	struct dce_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mi);
	uint32_t stutter_en = mi->ctx->dc->debug.disable_stutter ? 0 : 1;

	program_urgency_watermark(dce_mi, 0, /* set a */
			urgent.a_mark, total_dest_line_time_ns);
	program_urgency_watermark(dce_mi, 1, /* set b */
			urgent.b_mark, total_dest_line_time_ns);
	program_urgency_watermark(dce_mi, 2, /* set c */
			urgent.c_mark, total_dest_line_time_ns);
	program_urgency_watermark(dce_mi, 3, /* set d */
			urgent.d_mark, total_dest_line_time_ns);

	REG_UPDATE_2(DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE, stutter_en,
		STUTTER_IGNORE_FBC, 1);
	program_nbp_watermark(dce_mi, 0, nbp.a_mark); /* set a */
	program_nbp_watermark(dce_mi, 1, nbp.b_mark); /* set b */
	program_nbp_watermark(dce_mi, 2, nbp.c_mark); /* set c */
	program_nbp_watermark(dce_mi, 3, nbp.d_mark); /* set d */

	program_stutter_watermark(dce_mi, 0, stutter_exit.a_mark); /* set a */
	program_stutter_watermark(dce_mi, 1, stutter_exit.b_mark); /* set b */
	program_stutter_watermark(dce_mi, 2, stutter_exit.c_mark); /* set c */
	program_stutter_watermark(dce_mi, 3, stutter_exit.d_mark); /* set d */
}

static void dce120_mi_program_display_marks(struct mem_input *mi,
	struct dce_watermarks nbp,
	struct dce_watermarks stutter_exit,
	struct dce_watermarks stutter_entry,
	struct dce_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mi);
	uint32_t stutter_en = mi->ctx->dc->debug.disable_stutter ? 0 : 1;

	dce120_program_urgency_watermark(dce_mi, 0, /* set a */
			urgent.a_mark, total_dest_line_time_ns);
	dce120_program_urgency_watermark(dce_mi, 1, /* set b */
			urgent.b_mark, total_dest_line_time_ns);
	dce120_program_urgency_watermark(dce_mi, 2, /* set c */
			urgent.c_mark, total_dest_line_time_ns);
	dce120_program_urgency_watermark(dce_mi, 3, /* set d */
			urgent.d_mark, total_dest_line_time_ns);

	REG_UPDATE_2(DPG_PIPE_STUTTER_CONTROL,
		STUTTER_ENABLE, stutter_en,
		STUTTER_IGNORE_FBC, 1);
	program_nbp_watermark(dce_mi, 0, nbp.a_mark); /* set a */
	program_nbp_watermark(dce_mi, 1, nbp.b_mark); /* set b */
	program_nbp_watermark(dce_mi, 2, nbp.c_mark); /* set c */
	program_nbp_watermark(dce_mi, 3, nbp.d_mark); /* set d */

	dce120_program_stutter_watermark(dce_mi, 0, stutter_exit.a_mark, stutter_entry.a_mark); /* set a */
	dce120_program_stutter_watermark(dce_mi, 1, stutter_exit.b_mark, stutter_entry.b_mark); /* set b */
	dce120_program_stutter_watermark(dce_mi, 2, stutter_exit.c_mark, stutter_entry.c_mark); /* set c */
	dce120_program_stutter_watermark(dce_mi, 3, stutter_exit.d_mark, stutter_entry.d_mark); /* set d */
}

static void program_tiling(
	struct dce_mem_input *dce_mi, const union dc_tiling_info *info)
{
	if (dce_mi->masks->GRPH_SW_MODE) { /* GFX9 */
		REG_UPDATE_6(GRPH_CONTROL,
				GRPH_SW_MODE, info->gfx9.swizzle,
				GRPH_NUM_BANKS, log_2(info->gfx9.num_banks),
				GRPH_NUM_SHADER_ENGINES, log_2(info->gfx9.num_shader_engines),
				GRPH_NUM_PIPES, log_2(info->gfx9.num_pipes),
				GRPH_COLOR_EXPANSION_MODE, 1,
				GRPH_SE_ENABLE, info->gfx9.shaderEnable);
		/* TODO: DCP0_GRPH_CONTROL__GRPH_SE_ENABLE where to get info
		GRPH_SE_ENABLE, 1,
		GRPH_Z, 0);
		 */
	}

	if (dce_mi->masks->GRPH_MICRO_TILE_MODE) { /* GFX8 */
		REG_UPDATE_9(GRPH_CONTROL,
				GRPH_NUM_BANKS, info->gfx8.num_banks,
				GRPH_BANK_WIDTH, info->gfx8.bank_width,
				GRPH_BANK_HEIGHT, info->gfx8.bank_height,
				GRPH_MACRO_TILE_ASPECT, info->gfx8.tile_aspect,
				GRPH_TILE_SPLIT, info->gfx8.tile_split,
				GRPH_MICRO_TILE_MODE, info->gfx8.tile_mode,
				GRPH_PIPE_CONFIG, info->gfx8.pipe_config,
				GRPH_ARRAY_MODE, info->gfx8.array_mode,
				GRPH_COLOR_EXPANSION_MODE, 1);
		/* 01 - DCP_GRPH_COLOR_EXPANSION_MODE_ZEXP: zero expansion for YCbCr */
		/*
				GRPH_Z, 0);
				*/
	}

	if (dce_mi->masks->GRPH_ARRAY_MODE) { /* GFX6 but reuses gfx8 struct */
		REG_UPDATE_8(GRPH_CONTROL,
				GRPH_NUM_BANKS, info->gfx8.num_banks,
				GRPH_BANK_WIDTH, info->gfx8.bank_width,
				GRPH_BANK_HEIGHT, info->gfx8.bank_height,
				GRPH_MACRO_TILE_ASPECT, info->gfx8.tile_aspect,
				GRPH_TILE_SPLIT, info->gfx8.tile_split,
				/* DCE6 has no GRPH_MICRO_TILE_MODE mask */
				GRPH_PIPE_CONFIG, info->gfx8.pipe_config,
				GRPH_ARRAY_MODE, info->gfx8.array_mode,
				GRPH_COLOR_EXPANSION_MODE, 1);
		/* 01 - DCP_GRPH_COLOR_EXPANSION_MODE_ZEXP: zero expansion for YCbCr */
		/*
				GRPH_Z, 0);
				*/
	}
}


static void program_size_and_rotation(
	struct dce_mem_input *dce_mi,
	enum dc_rotation_angle rotation,
	const struct plane_size *plane_size)
{
	const struct rect *in_rect = &plane_size->surface_size;
	struct rect hw_rect = plane_size->surface_size;
	const uint32_t rotation_angles[ROTATION_ANGLE_COUNT] = {
			[ROTATION_ANGLE_0] = 0,
			[ROTATION_ANGLE_90] = 1,
			[ROTATION_ANGLE_180] = 2,
			[ROTATION_ANGLE_270] = 3,
	};

	if (rotation == ROTATION_ANGLE_90 || rotation == ROTATION_ANGLE_270) {
		hw_rect.x = in_rect->y;
		hw_rect.y = in_rect->x;

		hw_rect.height = in_rect->width;
		hw_rect.width = in_rect->height;
	}

	REG_SET(GRPH_X_START, 0,
			GRPH_X_START, hw_rect.x);

	REG_SET(GRPH_Y_START, 0,
			GRPH_Y_START, hw_rect.y);

	REG_SET(GRPH_X_END, 0,
			GRPH_X_END, hw_rect.width);

	REG_SET(GRPH_Y_END, 0,
			GRPH_Y_END, hw_rect.height);

	REG_SET(GRPH_PITCH, 0,
			GRPH_PITCH, plane_size->surface_pitch);

	REG_SET(HW_ROTATION, 0,
			GRPH_ROTATION_ANGLE, rotation_angles[rotation]);
}

#if defined(CONFIG_DRM_AMD_DC_SI)
static void dce60_program_size(
	struct dce_mem_input *dce_mi,
	enum dc_rotation_angle rotation, /* not used in DCE6 */
	const struct plane_size *plane_size)
{
	struct rect hw_rect = plane_size->surface_size;
	/* DCE6 has no HW rotation, skip rotation_angles declaration */

	/* DCE6 has no HW rotation, skip ROTATION_ANGLE_* processing */

	REG_SET(GRPH_X_START, 0,
			GRPH_X_START, hw_rect.x);

	REG_SET(GRPH_Y_START, 0,
			GRPH_Y_START, hw_rect.y);

	REG_SET(GRPH_X_END, 0,
			GRPH_X_END, hw_rect.width);

	REG_SET(GRPH_Y_END, 0,
			GRPH_Y_END, hw_rect.height);

	REG_SET(GRPH_PITCH, 0,
			GRPH_PITCH, plane_size->surface_pitch);

	/* DCE6 has no HW_ROTATION register, skip setting rotation_angles */
}
#endif

static void program_grph_pixel_format(
	struct dce_mem_input *dce_mi,
	enum surface_pixel_format format)
{
	uint32_t red_xbar = 0, blue_xbar = 0; /* no swap */
	uint32_t grph_depth = 0, grph_format = 0;
	uint32_t sign = 0, floating = 0;

	if (format == SURFACE_PIXEL_FORMAT_GRPH_ABGR8888 ||
			/*todo: doesn't look like we handle BGRA here,
			 *  should problem swap endian*/
		format == SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010 ||
		format == SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS ||
		format == SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616 ||
		format == SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F) {
		/* ABGR formats */
		red_xbar = 2;
		blue_xbar = 2;
	}

	REG_SET_2(GRPH_SWAP_CNTL, 0,
			GRPH_RED_CROSSBAR, red_xbar,
			GRPH_BLUE_CROSSBAR, blue_xbar);

	switch (format) {
	case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
		grph_depth = 0;
		grph_format = 0;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
		grph_depth = 1;
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
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		sign = 1;
		floating = 1;
		fallthrough;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F: /* shouldn't this get float too? */
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
		grph_depth = 3;
		grph_format = 0;
		break;
	default:
		DC_ERR("unsupported grph pixel format");
		break;
	}

	REG_UPDATE_2(GRPH_CONTROL,
			GRPH_DEPTH, grph_depth,
			GRPH_FORMAT, grph_format);

	REG_UPDATE_4(PRESCALE_GRPH_CONTROL,
			GRPH_PRESCALE_SELECT, floating,
			GRPH_PRESCALE_R_SIGN, sign,
			GRPH_PRESCALE_G_SIGN, sign,
			GRPH_PRESCALE_B_SIGN, sign);
}

static void dce_mi_program_surface_config(
	struct mem_input *mi,
	enum surface_pixel_format format,
	union dc_tiling_info *tiling_info,
	struct plane_size *plane_size,
	enum dc_rotation_angle rotation,
	struct dc_plane_dcc_param *dcc,
	bool horizontal_mirror)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mi);
	REG_UPDATE(GRPH_ENABLE, GRPH_ENABLE, 1);

	program_tiling(dce_mi, tiling_info);
	program_size_and_rotation(dce_mi, rotation, plane_size);

	if (format >= SURFACE_PIXEL_FORMAT_GRPH_BEGIN &&
		format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
		program_grph_pixel_format(dce_mi, format);
}

#if defined(CONFIG_DRM_AMD_DC_SI)
static void dce60_mi_program_surface_config(
	struct mem_input *mi,
	enum surface_pixel_format format,
	union dc_tiling_info *tiling_info,
	struct plane_size *plane_size,
	enum dc_rotation_angle rotation, /* not used in DCE6 */
	struct dc_plane_dcc_param *dcc,
	bool horizontal_mirror)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mi);
	REG_UPDATE(GRPH_ENABLE, GRPH_ENABLE, 1);

	program_tiling(dce_mi, tiling_info);
	dce60_program_size(dce_mi, rotation, plane_size);

	if (format >= SURFACE_PIXEL_FORMAT_GRPH_BEGIN &&
		format < SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
		program_grph_pixel_format(dce_mi, format);
}
#endif

static uint32_t get_dmif_switch_time_us(
	uint32_t h_total,
	uint32_t v_total,
	uint32_t pix_clk_khz)
{
	uint32_t frame_time;
	uint32_t pixels_per_second;
	uint32_t pixels_per_frame;
	uint32_t refresh_rate;
	const uint32_t us_in_sec = 1000000;
	const uint32_t min_single_frame_time_us = 30000;
	/*return double of frame time*/
	const uint32_t single_frame_time_multiplier = 2;

	if (!h_total || v_total || !pix_clk_khz)
		return single_frame_time_multiplier * min_single_frame_time_us;

	/*TODO: should we use pixel format normalized pixel clock here?*/
	pixels_per_second = pix_clk_khz * 1000;
	pixels_per_frame = h_total * v_total;

	if (!pixels_per_second || !pixels_per_frame) {
		/* avoid division by zero */
		ASSERT(pixels_per_frame);
		ASSERT(pixels_per_second);
		return single_frame_time_multiplier * min_single_frame_time_us;
	}

	refresh_rate = pixels_per_second / pixels_per_frame;

	if (!refresh_rate) {
		/* avoid division by zero*/
		ASSERT(refresh_rate);
		return single_frame_time_multiplier * min_single_frame_time_us;
	}

	frame_time = us_in_sec / refresh_rate;

	if (frame_time < min_single_frame_time_us)
		frame_time = min_single_frame_time_us;

	frame_time *= single_frame_time_multiplier;

	return frame_time;
}

static void dce_mi_allocate_dmif(
	struct mem_input *mi,
	uint32_t h_total,
	uint32_t v_total,
	uint32_t pix_clk_khz,
	uint32_t total_stream_num)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mi);
	const uint32_t retry_delay = 10;
	uint32_t retry_count = get_dmif_switch_time_us(
			h_total,
			v_total,
			pix_clk_khz) / retry_delay;

	uint32_t pix_dur;
	uint32_t buffers_allocated;
	uint32_t dmif_buffer_control;

	dmif_buffer_control = REG_GET(DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATED, &buffers_allocated);

	if (buffers_allocated == 2)
		return;

	REG_SET(DMIF_BUFFER_CONTROL, dmif_buffer_control,
			DMIF_BUFFERS_ALLOCATED, 2);

	REG_WAIT(DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATION_COMPLETED, 1,
			retry_delay, retry_count);

	if (pix_clk_khz != 0) {
		pix_dur = 1000000000ULL / pix_clk_khz;

		REG_UPDATE(DPG_PIPE_ARBITRATION_CONTROL1,
			PIXEL_DURATION, pix_dur);
	}

	if (dce_mi->wa.single_head_rdreq_dmif_limit) {
		uint32_t enable =  (total_stream_num > 1) ? 0 :
				dce_mi->wa.single_head_rdreq_dmif_limit;

		REG_UPDATE(MC_HUB_RDREQ_DMIF_LIMIT,
				ENABLE, enable);
	}
}

static void dce_mi_free_dmif(
		struct mem_input *mi,
		uint32_t total_stream_num)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mi);
	uint32_t buffers_allocated;
	uint32_t dmif_buffer_control;

	dmif_buffer_control = REG_GET(DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATED, &buffers_allocated);

	if (buffers_allocated == 0)
		return;

	REG_SET(DMIF_BUFFER_CONTROL, dmif_buffer_control,
			DMIF_BUFFERS_ALLOCATED, 0);

	REG_WAIT(DMIF_BUFFER_CONTROL,
			DMIF_BUFFERS_ALLOCATION_COMPLETED, 1,
			10, 3500);

	if (dce_mi->wa.single_head_rdreq_dmif_limit) {
		uint32_t enable =  (total_stream_num > 1) ? 0 :
				dce_mi->wa.single_head_rdreq_dmif_limit;

		REG_UPDATE(MC_HUB_RDREQ_DMIF_LIMIT,
				ENABLE, enable);
	}
}


static void program_sec_addr(
	struct dce_mem_input *dce_mi,
	PHYSICAL_ADDRESS_LOC address)
{
	/*high register MUST be programmed first*/
	REG_SET(GRPH_SECONDARY_SURFACE_ADDRESS_HIGH, 0,
		GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,
		address.high_part);

	REG_SET_2(GRPH_SECONDARY_SURFACE_ADDRESS, 0,
		GRPH_SECONDARY_SURFACE_ADDRESS, address.low_part >> 8,
		GRPH_SECONDARY_DFQ_ENABLE, 0);
}

static void program_pri_addr(
	struct dce_mem_input *dce_mi,
	PHYSICAL_ADDRESS_LOC address)
{
	/*high register MUST be programmed first*/
	REG_SET(GRPH_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH,
		address.high_part);

	REG_SET(GRPH_PRIMARY_SURFACE_ADDRESS, 0,
		GRPH_PRIMARY_SURFACE_ADDRESS,
		address.low_part >> 8);
}


static bool dce_mi_is_flip_pending(struct mem_input *mem_input)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mem_input);
	uint32_t update_pending;

	REG_GET(GRPH_UPDATE, GRPH_SURFACE_UPDATE_PENDING, &update_pending);
	if (update_pending)
		return true;

	mem_input->current_address = mem_input->request_address;
	return false;
}

static bool dce_mi_program_surface_flip_and_addr(
	struct mem_input *mem_input,
	const struct dc_plane_address *address,
	bool flip_immediate)
{
	struct dce_mem_input *dce_mi = TO_DCE_MEM_INPUT(mem_input);

	REG_UPDATE(GRPH_UPDATE, GRPH_UPDATE_LOCK, 1);

	REG_UPDATE(
		GRPH_FLIP_CONTROL,
		GRPH_SURFACE_UPDATE_H_RETRACE_EN, flip_immediate ? 1 : 0);

	switch (address->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		if (address->grph.addr.quad_part == 0)
			break;
		program_pri_addr(dce_mi, address->grph.addr);
		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
		if (address->grph_stereo.left_addr.quad_part == 0 ||
		    address->grph_stereo.right_addr.quad_part == 0)
			break;
		program_pri_addr(dce_mi, address->grph_stereo.left_addr);
		program_sec_addr(dce_mi, address->grph_stereo.right_addr);
		break;
	default:
		/* not supported */
		BREAK_TO_DEBUGGER();
		break;
	}

	mem_input->request_address = *address;

	if (flip_immediate)
		mem_input->current_address = *address;

	REG_UPDATE(GRPH_UPDATE, GRPH_UPDATE_LOCK, 0);

	return true;
}

static const struct mem_input_funcs dce_mi_funcs = {
	.mem_input_program_display_marks = dce_mi_program_display_marks,
	.allocate_mem_input = dce_mi_allocate_dmif,
	.free_mem_input = dce_mi_free_dmif,
	.mem_input_program_surface_flip_and_addr =
			dce_mi_program_surface_flip_and_addr,
	.mem_input_program_pte_vm = dce_mi_program_pte_vm,
	.mem_input_program_surface_config =
			dce_mi_program_surface_config,
	.mem_input_is_flip_pending = dce_mi_is_flip_pending
};

#if defined(CONFIG_DRM_AMD_DC_SI)
static const struct mem_input_funcs dce60_mi_funcs = {
	.mem_input_program_display_marks = dce60_mi_program_display_marks,
	.allocate_mem_input = dce_mi_allocate_dmif,
	.free_mem_input = dce_mi_free_dmif,
	.mem_input_program_surface_flip_and_addr =
			dce_mi_program_surface_flip_and_addr,
	.mem_input_program_pte_vm = dce_mi_program_pte_vm,
	.mem_input_program_surface_config =
			dce60_mi_program_surface_config,
	.mem_input_is_flip_pending = dce_mi_is_flip_pending
};
#endif

static const struct mem_input_funcs dce112_mi_funcs = {
	.mem_input_program_display_marks = dce112_mi_program_display_marks,
	.allocate_mem_input = dce_mi_allocate_dmif,
	.free_mem_input = dce_mi_free_dmif,
	.mem_input_program_surface_flip_and_addr =
			dce_mi_program_surface_flip_and_addr,
	.mem_input_program_pte_vm = dce_mi_program_pte_vm,
	.mem_input_program_surface_config =
			dce_mi_program_surface_config,
	.mem_input_is_flip_pending = dce_mi_is_flip_pending
};

static const struct mem_input_funcs dce120_mi_funcs = {
	.mem_input_program_display_marks = dce120_mi_program_display_marks,
	.allocate_mem_input = dce_mi_allocate_dmif,
	.free_mem_input = dce_mi_free_dmif,
	.mem_input_program_surface_flip_and_addr =
			dce_mi_program_surface_flip_and_addr,
	.mem_input_program_pte_vm = dce_mi_program_pte_vm,
	.mem_input_program_surface_config =
			dce_mi_program_surface_config,
	.mem_input_is_flip_pending = dce_mi_is_flip_pending
};

void dce_mem_input_construct(
	struct dce_mem_input *dce_mi,
	struct dc_context *ctx,
	int inst,
	const struct dce_mem_input_registers *regs,
	const struct dce_mem_input_shift *mi_shift,
	const struct dce_mem_input_mask *mi_mask)
{
	dce_mi->base.ctx = ctx;

	dce_mi->base.inst = inst;
	dce_mi->base.funcs = &dce_mi_funcs;

	dce_mi->regs = regs;
	dce_mi->shifts = mi_shift;
	dce_mi->masks = mi_mask;
}

#if defined(CONFIG_DRM_AMD_DC_SI)
void dce60_mem_input_construct(
	struct dce_mem_input *dce_mi,
	struct dc_context *ctx,
	int inst,
	const struct dce_mem_input_registers *regs,
	const struct dce_mem_input_shift *mi_shift,
	const struct dce_mem_input_mask *mi_mask)
{
	dce_mem_input_construct(dce_mi, ctx, inst, regs, mi_shift, mi_mask);
	dce_mi->base.funcs = &dce60_mi_funcs;
}
#endif

void dce112_mem_input_construct(
	struct dce_mem_input *dce_mi,
	struct dc_context *ctx,
	int inst,
	const struct dce_mem_input_registers *regs,
	const struct dce_mem_input_shift *mi_shift,
	const struct dce_mem_input_mask *mi_mask)
{
	dce_mem_input_construct(dce_mi, ctx, inst, regs, mi_shift, mi_mask);
	dce_mi->base.funcs = &dce112_mi_funcs;
}

void dce120_mem_input_construct(
	struct dce_mem_input *dce_mi,
	struct dc_context *ctx,
	int inst,
	const struct dce_mem_input_registers *regs,
	const struct dce_mem_input_shift *mi_shift,
	const struct dce_mem_input_mask *mi_mask)
{
	dce_mem_input_construct(dce_mi, ctx, inst, regs, mi_shift, mi_mask);
	dce_mi->base.funcs = &dce120_mi_funcs;
}
