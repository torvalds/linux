/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
#include "dce_calcs.h"
#include "dcn10_mem_input.h"
#include "reg_helper.h"
#include "basics/conversion.h"

#define REG(reg)\
	mi->mi_regs->reg

#define CTX \
	mi->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mi->mi_shift->field_name, mi->mi_mask->field_name

static void dcn_mi_set_blank(struct mem_input *mem_input, bool blank)
{
	struct dcn10_mem_input *mi = TO_DCN10_MEM_INPUT(mem_input);
	uint32_t blank_en = blank ? 1 : 0;

	REG_UPDATE_2(DCHUBP_CNTL,
			HUBP_BLANK_EN, blank_en,
			HUBP_TTU_DISABLE, blank_en);
}

static void vready_workaround(struct mem_input *mem_input,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest)
{
	uint32_t value = 0;
	struct dcn10_mem_input *mi = TO_DCN10_MEM_INPUT(mem_input);

	/* set HBUBREQ_DEBUG_DB[12] = 1 */
	value = REG_READ(HUBPREQ_DEBUG_DB);

	/* hack mode disable */
	value |= 0x100;
	value &= ~0x1000;

	if ((pipe_dest->vstartup_start - 2*(pipe_dest->vready_offset+pipe_dest->vupdate_width
		+ pipe_dest->vupdate_offset) / pipe_dest->htotal) <= pipe_dest->vblank_end) {
		/* if (eco_fix_needed(otg_global_sync_timing)
		 * set HBUBREQ_DEBUG_DB[12] = 1 */
		value |= 0x1000;
	}

	REG_WRITE(HUBPREQ_DEBUG_DB, value);
}

static void program_tiling(
	struct dcn10_mem_input *mi,
	const union dc_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	REG_UPDATE_6(DCSURF_ADDR_CONFIG,
			NUM_PIPES, log_2(info->gfx9.num_pipes),
			NUM_BANKS, log_2(info->gfx9.num_banks),
			PIPE_INTERLEAVE, info->gfx9.pipe_interleave,
			NUM_SE, log_2(info->gfx9.num_shader_engines),
			NUM_RB_PER_SE, log_2(info->gfx9.num_rb_per_se),
			MAX_COMPRESSED_FRAGS, log_2(info->gfx9.max_compressed_frags));

	REG_UPDATE_4(DCSURF_TILING_CONFIG,
			SW_MODE, info->gfx9.swizzle,
			META_LINEAR, info->gfx9.meta_linear,
			RB_ALIGNED, info->gfx9.rb_aligned,
			PIPE_ALIGNED, info->gfx9.pipe_aligned);
}

static void program_size_and_rotation(
	struct dcn10_mem_input *mi,
	enum dc_rotation_angle rotation,
	enum surface_pixel_format format,
	const union plane_size *plane_size,
	struct dc_plane_dcc_param *dcc,
	bool horizontal_mirror)
{
	uint32_t pitch, meta_pitch, pitch_c, meta_pitch_c, mirror;

	/* Program data and meta surface pitch (calculation from addrlib)
	 * 444 or 420 luma
	 */
	if (format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN) {
		pitch = plane_size->video.luma_pitch - 1;
		meta_pitch = dcc->video.meta_pitch_l - 1;
		pitch_c = plane_size->video.chroma_pitch - 1;
		meta_pitch_c = dcc->video.meta_pitch_c - 1;
	} else {
		pitch = plane_size->grph.surface_pitch - 1;
		meta_pitch = dcc->grph.meta_pitch - 1;
		pitch_c = 0;
		meta_pitch_c = 0;
	}

	if (!dcc->enable) {
		meta_pitch = 0;
		meta_pitch_c = 0;
	}

	REG_UPDATE_2(DCSURF_SURFACE_PITCH,
			PITCH, pitch, META_PITCH, meta_pitch);

	if (format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN)
		REG_UPDATE_2(DCSURF_SURFACE_PITCH_C,
			PITCH_C, pitch_c, META_PITCH_C, meta_pitch_c);

	if (horizontal_mirror)
		mirror = 1;
	else
		mirror = 0;


	/* Program rotation angle and horz mirror - no mirror */
	if (rotation == ROTATION_ANGLE_0)
		REG_UPDATE_2(DCSURF_SURFACE_CONFIG,
				ROTATION_ANGLE, 0,
				H_MIRROR_EN, mirror);
	else if (rotation == ROTATION_ANGLE_90)
		REG_UPDATE_2(DCSURF_SURFACE_CONFIG,
				ROTATION_ANGLE, 1,
				H_MIRROR_EN, mirror);
	else if (rotation == ROTATION_ANGLE_180)
		REG_UPDATE_2(DCSURF_SURFACE_CONFIG,
				ROTATION_ANGLE, 2,
				H_MIRROR_EN, mirror);
	else if (rotation == ROTATION_ANGLE_270)
		REG_UPDATE_2(DCSURF_SURFACE_CONFIG,
				ROTATION_ANGLE, 3,
				H_MIRROR_EN, mirror);
}

static void program_pixel_format(
	struct dcn10_mem_input *mi,
	enum surface_pixel_format format)
{
	uint32_t red_bar = 3;
	uint32_t blue_bar = 2;

	/* swap for ABGR format */
	if (format == SURFACE_PIXEL_FORMAT_GRPH_ABGR8888
			|| format == SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010
			|| format == SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS
			|| format == SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F) {
		red_bar = 2;
		blue_bar = 3;
	}

	REG_UPDATE_2(HUBPRET_CONTROL,
			CROSSBAR_SRC_CB_B, blue_bar,
			CROSSBAR_SRC_CR_R, red_bar);

	/* Mapping is same as ipp programming (cnvc) */

	switch (format)	{
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 1);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 3);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 8);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 10);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 22);
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:/*we use crossbar already*/
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 24);
		break;

	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 65);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 64);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 67);
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 66);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	/* don't see the need of program the xbar in DCN 1.0 */
}

static bool mem_input_program_surface_flip_and_addr(
	struct mem_input *mem_input,
	const struct dc_plane_address *address,
	bool flip_immediate)
{
	struct dcn10_mem_input *mi = TO_DCN10_MEM_INPUT(mem_input);

	/* program flip type */
	REG_SET(DCSURF_FLIP_CONTROL, 0,
			SURFACE_FLIP_TYPE, flip_immediate);

	/* HW automatically latch rest of address register on write to
	 * DCSURF_PRIMARY_SURFACE_ADDRESS if SURFACE_UPDATE_LOCK is not used
	 *
	 * program high first and then the low addr, order matters!
	 */
	switch (address->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		/* DCN1.0 does not support const color
		 * TODO: program DCHUBBUB_RET_PATH_DCC_CFGx_0/1
		 * base on address->grph.dcc_const_color
		 * x = 0, 2, 4, 6 for pipe 0, 1, 2, 3 for rgb and luma
		 * x = 1, 3, 5, 7 for pipe 0, 1, 2, 3 for chroma
		 */

		if (address->grph.addr.quad_part == 0)
			break;

		if (address->grph.meta_addr.quad_part != 0) {
			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH, 0,
					PRIMARY_META_SURFACE_ADDRESS_HIGH,
					address->grph.meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS, 0,
					PRIMARY_META_SURFACE_ADDRESS,
					address->grph.meta_addr.low_part);
		}

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH,
				address->grph.addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
				PRIMARY_SURFACE_ADDRESS,
				address->grph.addr.low_part);
		break;
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE:
		if (address->video_progressive.luma_addr.quad_part == 0
			|| address->video_progressive.chroma_addr.quad_part == 0)
			break;

		if (address->video_progressive.luma_meta_addr.quad_part != 0) {
			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH_C, 0,
				PRIMARY_META_SURFACE_ADDRESS_HIGH_C,
				address->video_progressive.chroma_meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_C, 0,
				PRIMARY_META_SURFACE_ADDRESS_C,
				address->video_progressive.chroma_meta_addr.low_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH, 0,
				PRIMARY_META_SURFACE_ADDRESS_HIGH,
				address->video_progressive.luma_meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS, 0,
				PRIMARY_META_SURFACE_ADDRESS,
				address->video_progressive.luma_meta_addr.low_part);
		}

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C, 0,
			PRIMARY_SURFACE_ADDRESS_HIGH_C,
			address->video_progressive.chroma_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_C, 0,
			PRIMARY_SURFACE_ADDRESS_C,
			address->video_progressive.chroma_addr.low_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
			PRIMARY_SURFACE_ADDRESS_HIGH,
			address->video_progressive.luma_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
			PRIMARY_SURFACE_ADDRESS,
			address->video_progressive.luma_addr.low_part);
		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
		if (address->grph_stereo.left_addr.quad_part == 0)
			break;
		if (address->grph_stereo.right_addr.quad_part == 0)
			break;
		if (address->grph_stereo.right_meta_addr.quad_part != 0) {

			REG_SET(DCSURF_SECONDARY_META_SURFACE_ADDRESS_HIGH, 0,
					SECONDARY_META_SURFACE_ADDRESS_HIGH,
					address->grph_stereo.right_meta_addr.high_part);

			REG_SET(DCSURF_SECONDARY_META_SURFACE_ADDRESS, 0,
					SECONDARY_META_SURFACE_ADDRESS,
					address->grph_stereo.right_meta_addr.low_part);
		}
		if (address->grph_stereo.left_meta_addr.quad_part != 0) {

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH, 0,
					PRIMARY_META_SURFACE_ADDRESS_HIGH,
					address->grph_stereo.left_meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS, 0,
					PRIMARY_META_SURFACE_ADDRESS,
					address->grph_stereo.left_meta_addr.low_part);
		}

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH, 0,
				SECONDARY_SURFACE_ADDRESS_HIGH,
				address->grph_stereo.right_addr.high_part);

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS, 0,
				SECONDARY_SURFACE_ADDRESS,
				address->grph_stereo.right_addr.low_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH,
				address->grph_stereo.left_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
				PRIMARY_SURFACE_ADDRESS,
				address->grph_stereo.left_addr.low_part);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	mem_input->request_address = *address;

	if (flip_immediate)
		mem_input->current_address = *address;

	return true;
}

static void program_control(struct dcn10_mem_input *mi,
		struct dc_plane_dcc_param *dcc)
{
	uint32_t dcc_en = dcc->enable ? 1 : 0;
	uint32_t dcc_ind_64b_blk = dcc->grph.independent_64b_blks ? 1 : 0;

	REG_UPDATE_2(DCSURF_SURFACE_CONTROL,
			PRIMARY_SURFACE_DCC_EN, dcc_en,
			PRIMARY_SURFACE_DCC_IND_64B_BLK, dcc_ind_64b_blk);

}

static void mem_input_program_surface_config(
	struct mem_input *mem_input,
	enum surface_pixel_format format,
	union dc_tiling_info *tiling_info,
	union plane_size *plane_size,
	enum dc_rotation_angle rotation,
	struct dc_plane_dcc_param *dcc,
	bool horizontal_mirror)
{
	struct dcn10_mem_input *mi = TO_DCN10_MEM_INPUT(mem_input);

	program_control(mi, dcc);
	program_tiling(mi, tiling_info, format);
	program_size_and_rotation(
		mi, rotation, format, plane_size, dcc, horizontal_mirror);
	program_pixel_format(mi, format);
}

static void program_requestor(
		struct mem_input *mem_input,
		struct _vcs_dpi_display_rq_regs_st *rq_regs)
{

	struct dcn10_mem_input *mi = TO_DCN10_MEM_INPUT(mem_input);

	REG_UPDATE(HUBPRET_CONTROL,
			DET_BUF_PLANE1_BASE_ADDRESS, rq_regs->plane1_base_address);
	REG_SET_4(DCN_EXPANSION_MODE, 0,
			DRQ_EXPANSION_MODE, rq_regs->drq_expansion_mode,
			PRQ_EXPANSION_MODE, rq_regs->prq_expansion_mode,
			MRQ_EXPANSION_MODE, rq_regs->mrq_expansion_mode,
			CRQ_EXPANSION_MODE, rq_regs->crq_expansion_mode);
	REG_SET_8(DCHUBP_REQ_SIZE_CONFIG, 0,
		CHUNK_SIZE, rq_regs->rq_regs_l.chunk_size,
		MIN_CHUNK_SIZE, rq_regs->rq_regs_l.min_chunk_size,
		META_CHUNK_SIZE, rq_regs->rq_regs_l.meta_chunk_size,
		MIN_META_CHUNK_SIZE, rq_regs->rq_regs_l.min_meta_chunk_size,
		DPTE_GROUP_SIZE, rq_regs->rq_regs_l.dpte_group_size,
		MPTE_GROUP_SIZE, rq_regs->rq_regs_l.mpte_group_size,
		SWATH_HEIGHT, rq_regs->rq_regs_l.swath_height,
		PTE_ROW_HEIGHT_LINEAR, rq_regs->rq_regs_l.pte_row_height_linear);
	REG_SET_8(DCHUBP_REQ_SIZE_CONFIG_C, 0,
		CHUNK_SIZE_C, rq_regs->rq_regs_c.chunk_size,
		MIN_CHUNK_SIZE_C, rq_regs->rq_regs_c.min_chunk_size,
		META_CHUNK_SIZE_C, rq_regs->rq_regs_c.meta_chunk_size,
		MIN_META_CHUNK_SIZE_C, rq_regs->rq_regs_c.min_meta_chunk_size,
		DPTE_GROUP_SIZE_C, rq_regs->rq_regs_c.dpte_group_size,
		MPTE_GROUP_SIZE_C, rq_regs->rq_regs_c.mpte_group_size,
		SWATH_HEIGHT_C, rq_regs->rq_regs_c.swath_height,
		PTE_ROW_HEIGHT_LINEAR_C, rq_regs->rq_regs_c.pte_row_height_linear);
}


static void program_deadline(
		struct mem_input *mem_input,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr)
{
	struct dcn10_mem_input *mi = TO_DCN10_MEM_INPUT(mem_input);

	/* DLG - Per hubp */
	REG_SET_2(BLANK_OFFSET_0, 0,
		REFCYC_H_BLANK_END, dlg_attr->refcyc_h_blank_end,
		DLG_V_BLANK_END, dlg_attr->dlg_vblank_end);

	REG_SET(BLANK_OFFSET_1, 0,
		MIN_DST_Y_NEXT_START, dlg_attr->min_dst_y_next_start);

	REG_SET(DST_DIMENSIONS, 0,
		REFCYC_PER_HTOTAL, dlg_attr->refcyc_per_htotal);

	REG_SET_2(DST_AFTER_SCALER, 0,
		REFCYC_X_AFTER_SCALER, dlg_attr->refcyc_x_after_scaler,
		DST_Y_AFTER_SCALER, dlg_attr->dst_y_after_scaler);

	REG_SET_2(PREFETCH_SETTINS, 0,
		DST_Y_PREFETCH, dlg_attr->dst_y_prefetch,
		VRATIO_PREFETCH, dlg_attr->vratio_prefetch);

	REG_SET_2(VBLANK_PARAMETERS_0, 0,
		DST_Y_PER_VM_VBLANK, dlg_attr->dst_y_per_vm_vblank,
		DST_Y_PER_ROW_VBLANK, dlg_attr->dst_y_per_row_vblank);

	REG_SET(REF_FREQ_TO_PIX_FREQ, 0,
		REF_FREQ_TO_PIX_FREQ, dlg_attr->ref_freq_to_pix_freq);

	/* DLG - Per luma/chroma */
	REG_SET(VBLANK_PARAMETERS_1, 0,
		REFCYC_PER_PTE_GROUP_VBLANK_L, dlg_attr->refcyc_per_pte_group_vblank_l);

	REG_SET(VBLANK_PARAMETERS_3, 0,
		REFCYC_PER_META_CHUNK_VBLANK_L, dlg_attr->refcyc_per_meta_chunk_vblank_l);

	REG_SET(NOM_PARAMETERS_0, 0,
		DST_Y_PER_PTE_ROW_NOM_L, dlg_attr->dst_y_per_pte_row_nom_l);

	REG_SET(NOM_PARAMETERS_1, 0,
		REFCYC_PER_PTE_GROUP_NOM_L, dlg_attr->refcyc_per_pte_group_nom_l);

	REG_SET(NOM_PARAMETERS_4, 0,
		DST_Y_PER_META_ROW_NOM_L, dlg_attr->dst_y_per_meta_row_nom_l);

	REG_SET(NOM_PARAMETERS_5, 0,
		REFCYC_PER_META_CHUNK_NOM_L, dlg_attr->refcyc_per_meta_chunk_nom_l);

	REG_SET_2(PER_LINE_DELIVERY_PRE, 0,
		REFCYC_PER_LINE_DELIVERY_PRE_L, dlg_attr->refcyc_per_line_delivery_pre_l,
		REFCYC_PER_LINE_DELIVERY_PRE_C, dlg_attr->refcyc_per_line_delivery_pre_c);

	REG_SET_2(PER_LINE_DELIVERY, 0,
		REFCYC_PER_LINE_DELIVERY_L, dlg_attr->refcyc_per_line_delivery_l,
		REFCYC_PER_LINE_DELIVERY_C, dlg_attr->refcyc_per_line_delivery_c);

	REG_SET(PREFETCH_SETTINS_C, 0,
		VRATIO_PREFETCH_C, dlg_attr->vratio_prefetch_c);

	REG_SET(VBLANK_PARAMETERS_2, 0,
		REFCYC_PER_PTE_GROUP_VBLANK_C, dlg_attr->refcyc_per_pte_group_vblank_c);

	REG_SET(VBLANK_PARAMETERS_4, 0,
		REFCYC_PER_META_CHUNK_VBLANK_C, dlg_attr->refcyc_per_meta_chunk_vblank_c);

	REG_SET(NOM_PARAMETERS_2, 0,
		DST_Y_PER_PTE_ROW_NOM_C, dlg_attr->dst_y_per_pte_row_nom_c);

	REG_SET(NOM_PARAMETERS_3, 0,
		REFCYC_PER_PTE_GROUP_NOM_C, dlg_attr->refcyc_per_pte_group_nom_c);

	REG_SET(NOM_PARAMETERS_6, 0,
		DST_Y_PER_META_ROW_NOM_C, dlg_attr->dst_y_per_meta_row_nom_c);

	REG_SET(NOM_PARAMETERS_7, 0,
		REFCYC_PER_META_CHUNK_NOM_C, dlg_attr->refcyc_per_meta_chunk_nom_c);

	/* TTU - per hubp */
	REG_SET_2(DCN_TTU_QOS_WM, 0,
		QoS_LEVEL_LOW_WM, ttu_attr->qos_level_low_wm,
		QoS_LEVEL_HIGH_WM, ttu_attr->qos_level_high_wm);

	REG_SET_2(DCN_GLOBAL_TTU_CNTL, 0,
		MIN_TTU_VBLANK, ttu_attr->min_ttu_vblank,
		QoS_LEVEL_FLIP, ttu_attr->qos_level_flip);

	/* TTU - per luma/chroma */
	/* Assumed surf0 is luma and 1 is chroma */

	REG_SET_3(DCN_SURF0_TTU_CNTL0, 0,
		REFCYC_PER_REQ_DELIVERY, ttu_attr->refcyc_per_req_delivery_l,
		QoS_LEVEL_FIXED, ttu_attr->qos_level_fixed_l,
		QoS_RAMP_DISABLE, ttu_attr->qos_ramp_disable_l);

	REG_SET(DCN_SURF0_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE,
		ttu_attr->refcyc_per_req_delivery_pre_l);

	REG_SET_3(DCN_SURF1_TTU_CNTL0, 0,
		REFCYC_PER_REQ_DELIVERY, ttu_attr->refcyc_per_req_delivery_c,
		QoS_LEVEL_FIXED, ttu_attr->qos_level_fixed_c,
		QoS_RAMP_DISABLE, ttu_attr->qos_ramp_disable_c);

	REG_SET(DCN_SURF1_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE,
		ttu_attr->refcyc_per_req_delivery_pre_c);
}

static void mem_input_setup(
		struct mem_input *mem_input,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr,
		struct _vcs_dpi_display_rq_regs_st *rq_regs,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest)
{
	/* otg is locked when this func is called. Register are double buffered.
	 * disable the requestors is not needed
	 */
	program_requestor(mem_input, rq_regs);
	program_deadline(mem_input, dlg_attr, ttu_attr);
	vready_workaround(mem_input, pipe_dest);
}

static uint32_t convert_and_clamp(
	uint32_t wm_ns,
	uint32_t refclk_mhz,
	uint32_t clamp_value)
{
	uint32_t ret_val = 0;
	ret_val = wm_ns * refclk_mhz;
	ret_val /= 1000;

	if (ret_val > clamp_value)
		ret_val = clamp_value;

	return ret_val;
}

static void program_watermarks(
		struct mem_input *mem_input,
		struct dcn_watermark_set *watermarks,
		unsigned int refclk_mhz)
{
	struct dcn10_mem_input *mi = TO_DCN10_MEM_INPUT(mem_input);
	/*
	 * Need to clamp to max of the register values (i.e. no wrap)
	 * for dcn1, all wm registers are 21-bit wide
	 */
	uint32_t prog_wm_value;

	/* Repeat for water mark set A, B, C and D. */
	/* clock state A */
	prog_wm_value = convert_and_clamp(watermarks->a.urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_A, prog_wm_value);

	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"URGENCY_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->a.urgent_ns, prog_wm_value);

	prog_wm_value = convert_and_clamp(watermarks->a.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_A, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"PTE_META_URGENCY_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->a.pte_meta_urgent_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns,
			refclk_mhz, 0x1fffff);

	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_A, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"SR_ENTER_EXIT_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->a.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->a.cstate_pstate.cstate_exit_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_A, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"SR_EXIT_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->a.cstate_pstate.cstate_exit_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->a.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_A, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"DRAM_CLK_CHANGE_WATERMARK_A calculated =%d\n"
		"HW register value = 0x%x\n\n",
		watermarks->a.cstate_pstate.pstate_change_ns, prog_wm_value);


	/* clock state B */
	prog_wm_value = convert_and_clamp(
			watermarks->b.urgent_ns, refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_B, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"URGENCY_WATERMARK_B calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->b.urgent_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->b.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_B, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"PTE_META_URGENCY_WATERMARK_B calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->b.pte_meta_urgent_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_B, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"SR_ENTER_WATERMARK_B calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->b.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->b.cstate_pstate.cstate_exit_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_B, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"SR_EXIT_WATERMARK_B calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->b.cstate_pstate.cstate_exit_ns, prog_wm_value);

	prog_wm_value = convert_and_clamp(
			watermarks->b.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_B, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"DRAM_CLK_CHANGE_WATERMARK_B calculated =%d\n\n"
		"HW register value = 0x%x\n",
		watermarks->b.cstate_pstate.pstate_change_ns, prog_wm_value);

	/* clock state C */
	prog_wm_value = convert_and_clamp(
			watermarks->c.urgent_ns, refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_C, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"URGENCY_WATERMARK_C calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->c.urgent_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->c.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_C, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"PTE_META_URGENCY_WATERMARK_C calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->c.pte_meta_urgent_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_C, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"SR_ENTER_WATERMARK_C calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->c.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->c.cstate_pstate.cstate_exit_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_C, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"SR_EXIT_WATERMARK_C calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->c.cstate_pstate.cstate_exit_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->c.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_C, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"DRAM_CLK_CHANGE_WATERMARK_C calculated =%d\n\n"
		"HW register value = 0x%x\n",
		watermarks->c.cstate_pstate.pstate_change_ns, prog_wm_value);

	/* clock state D */
	prog_wm_value = convert_and_clamp(
			watermarks->d.urgent_ns, refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_DATA_URGENCY_WATERMARK_D, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"URGENCY_WATERMARK_D calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->d.urgent_ns, prog_wm_value);

	prog_wm_value = convert_and_clamp(
			watermarks->d.pte_meta_urgent_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_PTE_META_URGENCY_WATERMARK_D, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"PTE_META_URGENCY_WATERMARK_D calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->d.pte_meta_urgent_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_ENTER_WATERMARK_D, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"SR_ENTER_WATERMARK_D calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->d.cstate_pstate.cstate_enter_plus_exit_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->d.cstate_pstate.cstate_exit_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_SR_EXIT_WATERMARK_D, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"SR_EXIT_WATERMARK_D calculated =%d\n"
		"HW register value = 0x%x\n",
		watermarks->d.cstate_pstate.cstate_exit_ns, prog_wm_value);


	prog_wm_value = convert_and_clamp(
			watermarks->d.cstate_pstate.pstate_change_ns,
			refclk_mhz, 0x1fffff);
	REG_WRITE(DCHUBBUB_ARB_ALLOW_DRAM_CLK_CHANGE_WATERMARK_D, prog_wm_value);
	dm_logger_write(mem_input->ctx->logger, LOG_HW_MARKS,
		"DRAM_CLK_CHANGE_WATERMARK_D calculated =%d\n"
		"HW register value = 0x%x\n\n",
		watermarks->d.cstate_pstate.pstate_change_ns, prog_wm_value);

	REG_UPDATE(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, 1);
	REG_UPDATE(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, 0);
	REG_UPDATE(DCHUBBUB_ARB_SAT_LEVEL,
			DCHUBBUB_ARB_SAT_LEVEL, 60 * refclk_mhz);
	REG_UPDATE(DCHUBBUB_ARB_DF_REQ_OUTSTAND,
			DCHUBBUB_ARB_MIN_REQ_OUTSTAND, 68);

#if 0
	REG_UPDATE_2(DCHUBBUB_ARB_WATERMARK_CHANGE_CNTL,
			DCHUBBUB_ARB_WATERMARK_CHANGE_DONE_INTERRUPT_DISABLE, 1,
			DCHUBBUB_ARB_WATERMARK_CHANGE_REQUEST, 1);
#endif
}

static void mem_input_program_display_marks(
	struct mem_input *mem_input,
	struct dce_watermarks nbp,
	struct dce_watermarks stutter,
	struct dce_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	/* only for dce
	 * dcn use only program_watermarks
	 */
}

bool mem_input_is_flip_pending(struct mem_input *mem_input)
{
	uint32_t update_pending = 0;
	struct dcn10_mem_input *mi = TO_DCN10_MEM_INPUT(mem_input);

	REG_GET(DCSURF_FLIP_CONTROL,
			SURFACE_UPDATE_PENDING, &update_pending);

	if (update_pending)
		return true;

	mem_input->current_address = mem_input->request_address;
	return false;
}

static void mem_input_update_dchub(
	struct mem_input *mem_input,
	struct dchub_init_data *dh_data)
{
	struct dcn10_mem_input *mi = TO_DCN10_MEM_INPUT(mem_input);
	/* TODO: port code from dal2 */
	switch (dh_data->fb_mode) {
	case FRAME_BUFFER_MODE_ZFB_ONLY:
		/*For ZFB case need to put DCHUB FB BASE and TOP upside down to indicate ZFB mode*/
		REG_UPDATE(DCHUBBUB_SDPIF_FB_TOP,
				SDPIF_FB_TOP, 0);

		REG_UPDATE(DCHUBBUB_SDPIF_FB_BASE,
				SDPIF_FB_BASE, 0x0FFFF);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BASE,
				SDPIF_AGP_BASE, dh_data->zfb_phys_addr_base >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BOT,
				SDPIF_AGP_BOT, dh_data->zfb_mc_base_addr >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_TOP,
				SDPIF_AGP_TOP, (dh_data->zfb_mc_base_addr +
						dh_data->zfb_size_in_byte - 1) >> 22);
		break;
	case FRAME_BUFFER_MODE_MIXED_ZFB_AND_LOCAL:
		/*Should not touch FB LOCATION (done by VBIOS on AsicInit table)*/

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BASE,
				SDPIF_AGP_BASE, dh_data->zfb_phys_addr_base >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BOT,
				SDPIF_AGP_BOT, dh_data->zfb_mc_base_addr >> 22);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_TOP,
				SDPIF_AGP_TOP, (dh_data->zfb_mc_base_addr +
						dh_data->zfb_size_in_byte - 1) >> 22);
		break;
	case FRAME_BUFFER_MODE_LOCAL_ONLY:
		/*Should not touch FB LOCATION (done by VBIOS on AsicInit table)*/
		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BASE,
				SDPIF_AGP_BASE, 0);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_BOT,
				SDPIF_AGP_BOT, 0X03FFFF);

		REG_UPDATE(DCHUBBUB_SDPIF_AGP_TOP,
				SDPIF_AGP_TOP, 0);
		break;
	default:
		break;
	}

	dh_data->dchub_initialzied = true;
	dh_data->dchub_info_valid = false;
}

struct vm_system_aperture_param {
	PHYSICAL_ADDRESS_LOC sys_default;
	PHYSICAL_ADDRESS_LOC sys_low;
	PHYSICAL_ADDRESS_LOC sys_high;
};

static void read_vm_system_aperture_settings(struct dcn10_mem_input *mi,
		struct vm_system_aperture_param *apt)
{
	PHYSICAL_ADDRESS_LOC physical_page_number;
	uint32_t logical_addr_low;
	uint32_t logical_addr_high;

	REG_GET(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
			PHYSICAL_PAGE_NUMBER_MSB, &physical_page_number.high_part);
	REG_GET(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
			PHYSICAL_PAGE_NUMBER_LSB, &physical_page_number.low_part);

	REG_GET(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
			LOGICAL_ADDR, &logical_addr_low);

	REG_GET(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
			LOGICAL_ADDR, &logical_addr_high);

	apt->sys_default.quad_part =  physical_page_number.quad_part << 12;
	apt->sys_low.quad_part =  (int64_t)logical_addr_low << 18;
	apt->sys_high.quad_part =  (int64_t)logical_addr_high << 18;
}

static void set_vm_system_aperture_settings(struct dcn10_mem_input *mi,
		struct vm_system_aperture_param *apt)
{
	PHYSICAL_ADDRESS_LOC mc_vm_apt_default;
	PHYSICAL_ADDRESS_LOC mc_vm_apt_low;
	PHYSICAL_ADDRESS_LOC mc_vm_apt_high;

	mc_vm_apt_default.quad_part = apt->sys_default.quad_part >> 12;
	mc_vm_apt_low.quad_part = apt->sys_low.quad_part >> 12;
	mc_vm_apt_high.quad_part = apt->sys_high.quad_part >> 12;

	REG_SET_2(DCN_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB, 0,
		MC_VM_SYSTEM_APERTURE_DEFAULT_SYSTEM, 1, /* 1 = system physical memory */
		MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB, mc_vm_apt_default.high_part);
	REG_SET(DCN_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB, 0,
		MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB, mc_vm_apt_default.low_part);

	REG_SET(DCN_VM_SYSTEM_APERTURE_LOW_ADDR_MSB, 0,
			MC_VM_SYSTEM_APERTURE_LOW_ADDR_MSB, mc_vm_apt_low.high_part);
	REG_SET(DCN_VM_SYSTEM_APERTURE_LOW_ADDR_LSB, 0,
			MC_VM_SYSTEM_APERTURE_LOW_ADDR_LSB, mc_vm_apt_low.low_part);

	REG_SET(DCN_VM_SYSTEM_APERTURE_HIGH_ADDR_MSB, 0,
			MC_VM_SYSTEM_APERTURE_HIGH_ADDR_MSB, mc_vm_apt_high.high_part);
	REG_SET(DCN_VM_SYSTEM_APERTURE_HIGH_ADDR_LSB, 0,
			MC_VM_SYSTEM_APERTURE_HIGH_ADDR_LSB, mc_vm_apt_high.low_part);
}

struct vm_context0_param {
	PHYSICAL_ADDRESS_LOC pte_base;
	PHYSICAL_ADDRESS_LOC pte_start;
	PHYSICAL_ADDRESS_LOC pte_end;
	PHYSICAL_ADDRESS_LOC fault_default;
};

/* Temporary read settings, future will get values from kmd directly */
static void read_vm_context0_settings(struct dcn10_mem_input *mi,
		struct vm_context0_param *vm0)
{
	PHYSICAL_ADDRESS_LOC fb_base;
	PHYSICAL_ADDRESS_LOC fb_offset;
	uint32_t fb_base_value;
	uint32_t fb_offset_value;

	REG_GET(DCHUBBUB_SDPIF_FB_BASE, SDPIF_FB_BASE, &fb_base_value);
	REG_GET(DCHUBBUB_SDPIF_FB_OFFSET, SDPIF_FB_OFFSET, &fb_offset_value);

	REG_GET(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
			PAGE_DIRECTORY_ENTRY_HI32, &vm0->pte_base.high_part);
	REG_GET(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
			PAGE_DIRECTORY_ENTRY_LO32, &vm0->pte_base.low_part);

	REG_GET(VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
			LOGICAL_PAGE_NUMBER_HI4, &vm0->pte_start.high_part);
	REG_GET(VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
			LOGICAL_PAGE_NUMBER_LO32, &vm0->pte_start.low_part);

	REG_GET(VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
			LOGICAL_PAGE_NUMBER_HI4, &vm0->pte_end.high_part);
	REG_GET(VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
			LOGICAL_PAGE_NUMBER_LO32, &vm0->pte_end.low_part);

	REG_GET(VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
			PHYSICAL_PAGE_ADDR_HI4, &vm0->fault_default.high_part);
	REG_GET(VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
			PHYSICAL_PAGE_ADDR_LO32, &vm0->fault_default.low_part);

	/*
	 * The values in VM_CONTEXT0_PAGE_TABLE_BASE_ADDR is in UMA space.
	 * Therefore we need to do
	 * DCN_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR = VM_CONTEXT0_PAGE_TABLE_BASE_ADDR
	 * - DCHUBBUB_SDPIF_FB_OFFSET + DCHUBBUB_SDPIF_FB_BASE
	 */
	fb_base.quad_part = (uint64_t)fb_base_value << 24;
	fb_offset.quad_part = (uint64_t)fb_offset_value << 24;
	vm0->pte_base.quad_part += fb_base.quad_part;
	vm0->pte_base.quad_part -= fb_offset.quad_part;
}

static void set_vm_context0_settings(struct dcn10_mem_input *mi,
		const struct vm_context0_param *vm0)
{
	/* pte base */
	REG_SET(DCN_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_MSB, 0,
			VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_MSB, vm0->pte_base.high_part);
	REG_SET(DCN_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LSB, 0,
			VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LSB, vm0->pte_base.low_part);

	/* pte start */
	REG_SET(DCN_VM_CONTEXT0_PAGE_TABLE_START_ADDR_MSB, 0,
			VM_CONTEXT0_PAGE_TABLE_START_ADDR_MSB, vm0->pte_start.high_part);
	REG_SET(DCN_VM_CONTEXT0_PAGE_TABLE_START_ADDR_LSB, 0,
			VM_CONTEXT0_PAGE_TABLE_START_ADDR_LSB, vm0->pte_start.low_part);

	/* pte end */
	REG_SET(DCN_VM_CONTEXT0_PAGE_TABLE_END_ADDR_MSB, 0,
			VM_CONTEXT0_PAGE_TABLE_END_ADDR_MSB, vm0->pte_end.high_part);
	REG_SET(DCN_VM_CONTEXT0_PAGE_TABLE_END_ADDR_LSB, 0,
			VM_CONTEXT0_PAGE_TABLE_END_ADDR_LSB, vm0->pte_end.low_part);

	/* fault handling */
	REG_SET(DCN_VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR_MSB, 0,
			VM_CONTEXT0_PAGE_TABLE_END_ADDR_MSB, vm0->fault_default.high_part);
	/* VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_SYSTEM, 0 */
	REG_SET(DCN_VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR_LSB, 0,
			VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR_LSB, vm0->fault_default.low_part);
}

void dcn_mem_input_program_pte_vm(struct mem_input *mem_input,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		enum dc_rotation_angle rotation)
{
	struct dcn10_mem_input *mi = TO_DCN10_MEM_INPUT(mem_input);
	struct vm_system_aperture_param apt = { {{ 0 } } };
	struct vm_context0_param vm0 = { { { 0 } } };


	read_vm_system_aperture_settings(mi, &apt);
	read_vm_context0_settings(mi, &vm0);

	set_vm_system_aperture_settings(mi, &apt);
	set_vm_context0_settings(mi, &vm0);

	/* control: enable VM PTE*/
	REG_SET_2(DCN_VM_MX_L1_TLB_CNTL, 0,
			ENABLE_L1_TLB, 1,
			SYSTEM_ACCESS_MODE, 3);
}

static struct mem_input_funcs dcn10_mem_input_funcs = {
	.mem_input_program_display_marks = mem_input_program_display_marks,
	.allocate_mem_input = NULL,
	.free_mem_input = NULL,
	.mem_input_program_surface_flip_and_addr =
			mem_input_program_surface_flip_and_addr,
	.mem_input_program_surface_config =
			mem_input_program_surface_config,
	.mem_input_is_flip_pending = mem_input_is_flip_pending,
	.mem_input_setup = mem_input_setup,
	.program_watermarks = program_watermarks,
	.mem_input_update_dchub = mem_input_update_dchub,
	.mem_input_program_pte_vm = dcn_mem_input_program_pte_vm,
	.set_blank = dcn_mi_set_blank,
};


/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dcn10_mem_input_construct(
	struct dcn10_mem_input *mi,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_mi_registers *mi_regs,
	const struct dcn_mi_shift *mi_shift,
	const struct dcn_mi_mask *mi_mask)
{
	mi->base.funcs = &dcn10_mem_input_funcs;
	mi->base.ctx = ctx;
	mi->mi_regs = mi_regs;
	mi->mi_shift = mi_shift;
	mi->mi_mask = mi_mask;
	mi->base.inst = inst;

	return true;
}

