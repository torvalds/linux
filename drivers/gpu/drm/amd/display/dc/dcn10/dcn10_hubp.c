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
#include "reg_helper.h"
#include "basics/conversion.h"
#include "dcn10_hubp.h"

#define REG(reg)\
	hubp1->hubp_regs->reg

#define CTX \
	hubp1->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubp1->hubp_shift->field_name, hubp1->hubp_mask->field_name

void hubp1_set_blank(struct hubp *hubp, bool blank)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	uint32_t blank_en = blank ? 1 : 0;

	REG_UPDATE_2(DCHUBP_CNTL,
			HUBP_BLANK_EN, blank_en,
			HUBP_TTU_DISABLE, blank_en);

	if (blank) {
		uint32_t reg_val = REG_READ(DCHUBP_CNTL);

		if (reg_val) {
			/* init sequence workaround: in case HUBP is
			 * power gated, this wait would timeout.
			 *
			 * we just wrote reg_val to non-0, if it stay 0
			 * it means HUBP is gated
			 */
			REG_WAIT(DCHUBP_CNTL,
					HUBP_NO_OUTSTANDING_REQ, 1,
					1, 200);
		}

		hubp->mpcc_id = 0xf;
		hubp->opp_id = 0xf;
	}
}

static void hubp1_disconnect(struct hubp *hubp)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

	REG_UPDATE(DCHUBP_CNTL,
			HUBP_TTU_DISABLE, 1);

	REG_UPDATE(CURSOR_CONTROL,
			CURSOR_ENABLE, 0);
}

static void hubp1_disable_control(struct hubp *hubp, bool disable_hubp)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	uint32_t disable = disable_hubp ? 1 : 0;

	REG_UPDATE(DCHUBP_CNTL,
			HUBP_DISABLE, disable);
}

static unsigned int hubp1_get_underflow_status(struct hubp *hubp)
{
	uint32_t hubp_underflow = 0;
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

	REG_GET(DCHUBP_CNTL,
		HUBP_UNDERFLOW_STATUS,
		&hubp_underflow);

	return hubp_underflow;
}


void hubp1_clear_underflow(struct hubp *hubp)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

	REG_UPDATE(DCHUBP_CNTL, HUBP_UNDERFLOW_CLEAR, 1);
}

static void hubp1_set_hubp_blank_en(struct hubp *hubp, bool blank)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	uint32_t blank_en = blank ? 1 : 0;

	REG_UPDATE(DCHUBP_CNTL, HUBP_BLANK_EN, blank_en);
}

void hubp1_vready_workaround(struct hubp *hubp,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest)
{
	uint32_t value = 0;
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

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

void hubp1_program_tiling(
	struct hubp *hubp,
	const union dc_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

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

void hubp1_program_size(
	struct hubp *hubp,
	enum surface_pixel_format format,
	const union plane_size *plane_size,
	struct dc_plane_dcc_param *dcc)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	uint32_t pitch, meta_pitch, pitch_c, meta_pitch_c;

	/* Program data and meta surface pitch (calculation from addrlib)
	 * 444 or 420 luma
	 */
	if (format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN && format < SURFACE_PIXEL_FORMAT_SUBSAMPLE_END) {
		ASSERT(plane_size->video.chroma_pitch != 0);
		/* Chroma pitch zero can cause system hang! */

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
}

void hubp1_program_rotation(
	struct hubp *hubp,
	enum dc_rotation_angle rotation,
	bool horizontal_mirror)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	uint32_t mirror;


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

void hubp1_program_pixel_format(
	struct hubp *hubp,
	enum surface_pixel_format format)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
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
	case SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
		REG_UPDATE(DCSURF_SURFACE_CONFIG,
				SURFACE_PIXEL_FORMAT, 12);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	/* don't see the need of program the xbar in DCN 1.0 */
}

bool hubp1_program_surface_flip_and_addr(
	struct hubp *hubp,
	const struct dc_plane_address *address,
	bool flip_immediate,
	uint8_t vmid)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);


	//program flip type
	REG_UPDATE(DCSURF_FLIP_CONTROL,
			SURFACE_FLIP_TYPE, flip_immediate);


	if (address->type == PLN_ADDR_TYPE_GRPH_STEREO) {
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_FLIP_MODE_FOR_STEREOSYNC, 0x1);
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_FLIP_IN_STEREOSYNC, 0x1);

	} else {
		// turn off stereo if not in stereo
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_FLIP_MODE_FOR_STEREOSYNC, 0x0);
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_FLIP_IN_STEREOSYNC, 0x0);
	}



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

		REG_UPDATE_2(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ, address->tmz_surface);

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

		REG_UPDATE_4(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ_C, address->tmz_surface);

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

		REG_UPDATE_8(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ_C, address->tmz_surface,
				SECONDARY_SURFACE_TMZ, address->tmz_surface,
				SECONDARY_SURFACE_TMZ_C, address->tmz_surface,
				SECONDARY_META_SURFACE_TMZ, address->tmz_surface,
				SECONDARY_META_SURFACE_TMZ_C, address->tmz_surface);

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

	hubp->request_address = *address;

	return true;
}

void hubp1_dcc_control(struct hubp *hubp, bool enable,
		bool independent_64b_blks)
{
	uint32_t dcc_en = enable ? 1 : 0;
	uint32_t dcc_ind_64b_blk = independent_64b_blks ? 1 : 0;
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

	REG_UPDATE_4(DCSURF_SURFACE_CONTROL,
			PRIMARY_SURFACE_DCC_EN, dcc_en,
			PRIMARY_SURFACE_DCC_IND_64B_BLK, dcc_ind_64b_blk,
			SECONDARY_SURFACE_DCC_EN, dcc_en,
			SECONDARY_SURFACE_DCC_IND_64B_BLK, dcc_ind_64b_blk);
}

void hubp1_program_surface_config(
	struct hubp *hubp,
	enum surface_pixel_format format,
	union dc_tiling_info *tiling_info,
	union plane_size *plane_size,
	enum dc_rotation_angle rotation,
	struct dc_plane_dcc_param *dcc,
	bool horizontal_mirror,
	unsigned int compat_level)
{
	hubp1_dcc_control(hubp, dcc->enable, dcc->grph.independent_64b_blks);
	hubp1_program_tiling(hubp, tiling_info, format);
	hubp1_program_size(hubp, format, plane_size, dcc);
	hubp1_program_rotation(hubp, rotation, horizontal_mirror);
	hubp1_program_pixel_format(hubp, format);
}

void hubp1_program_requestor(
		struct hubp *hubp,
		struct _vcs_dpi_display_rq_regs_st *rq_regs)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

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


void hubp1_program_deadline(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

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

	REG_SET(REF_FREQ_TO_PIX_FREQ, 0,
		REF_FREQ_TO_PIX_FREQ, dlg_attr->ref_freq_to_pix_freq);

	/* DLG - Per luma/chroma */
	REG_SET(VBLANK_PARAMETERS_1, 0,
		REFCYC_PER_PTE_GROUP_VBLANK_L, dlg_attr->refcyc_per_pte_group_vblank_l);

	if (REG(NOM_PARAMETERS_0))
		REG_SET(NOM_PARAMETERS_0, 0,
			DST_Y_PER_PTE_ROW_NOM_L, dlg_attr->dst_y_per_pte_row_nom_l);

	if (REG(NOM_PARAMETERS_1))
		REG_SET(NOM_PARAMETERS_1, 0,
			REFCYC_PER_PTE_GROUP_NOM_L, dlg_attr->refcyc_per_pte_group_nom_l);

	REG_SET(NOM_PARAMETERS_4, 0,
		DST_Y_PER_META_ROW_NOM_L, dlg_attr->dst_y_per_meta_row_nom_l);

	REG_SET(NOM_PARAMETERS_5, 0,
		REFCYC_PER_META_CHUNK_NOM_L, dlg_attr->refcyc_per_meta_chunk_nom_l);

	REG_SET_2(PER_LINE_DELIVERY, 0,
		REFCYC_PER_LINE_DELIVERY_L, dlg_attr->refcyc_per_line_delivery_l,
		REFCYC_PER_LINE_DELIVERY_C, dlg_attr->refcyc_per_line_delivery_c);

	REG_SET(VBLANK_PARAMETERS_2, 0,
		REFCYC_PER_PTE_GROUP_VBLANK_C, dlg_attr->refcyc_per_pte_group_vblank_c);

	if (REG(NOM_PARAMETERS_2))
		REG_SET(NOM_PARAMETERS_2, 0,
			DST_Y_PER_PTE_ROW_NOM_C, dlg_attr->dst_y_per_pte_row_nom_c);

	if (REG(NOM_PARAMETERS_3))
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

	/* TTU - per luma/chroma */
	/* Assumed surf0 is luma and 1 is chroma */

	REG_SET_3(DCN_SURF0_TTU_CNTL0, 0,
		REFCYC_PER_REQ_DELIVERY, ttu_attr->refcyc_per_req_delivery_l,
		QoS_LEVEL_FIXED, ttu_attr->qos_level_fixed_l,
		QoS_RAMP_DISABLE, ttu_attr->qos_ramp_disable_l);

	REG_SET_3(DCN_SURF1_TTU_CNTL0, 0,
		REFCYC_PER_REQ_DELIVERY, ttu_attr->refcyc_per_req_delivery_c,
		QoS_LEVEL_FIXED, ttu_attr->qos_level_fixed_c,
		QoS_RAMP_DISABLE, ttu_attr->qos_ramp_disable_c);

	REG_SET_3(DCN_CUR0_TTU_CNTL0, 0,
		REFCYC_PER_REQ_DELIVERY, ttu_attr->refcyc_per_req_delivery_cur0,
		QoS_LEVEL_FIXED, ttu_attr->qos_level_fixed_cur0,
		QoS_RAMP_DISABLE, ttu_attr->qos_ramp_disable_cur0);
}

static void hubp1_setup(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr,
		struct _vcs_dpi_display_rq_regs_st *rq_regs,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest)
{
	/* otg is locked when this func is called. Register are double buffered.
	 * disable the requestors is not needed
	 */
	hubp1_program_requestor(hubp, rq_regs);
	hubp1_program_deadline(hubp, dlg_attr, ttu_attr);
	hubp1_vready_workaround(hubp, pipe_dest);
}

static void hubp1_setup_interdependent(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

	REG_SET_2(PREFETCH_SETTINS, 0,
		DST_Y_PREFETCH, dlg_attr->dst_y_prefetch,
		VRATIO_PREFETCH, dlg_attr->vratio_prefetch);

	REG_SET(PREFETCH_SETTINS_C, 0,
		VRATIO_PREFETCH_C, dlg_attr->vratio_prefetch_c);

	REG_SET_2(VBLANK_PARAMETERS_0, 0,
		DST_Y_PER_VM_VBLANK, dlg_attr->dst_y_per_vm_vblank,
		DST_Y_PER_ROW_VBLANK, dlg_attr->dst_y_per_row_vblank);

	REG_SET(VBLANK_PARAMETERS_3, 0,
		REFCYC_PER_META_CHUNK_VBLANK_L, dlg_attr->refcyc_per_meta_chunk_vblank_l);

	REG_SET(VBLANK_PARAMETERS_4, 0,
		REFCYC_PER_META_CHUNK_VBLANK_C, dlg_attr->refcyc_per_meta_chunk_vblank_c);

	REG_SET_2(PER_LINE_DELIVERY_PRE, 0,
		REFCYC_PER_LINE_DELIVERY_PRE_L, dlg_attr->refcyc_per_line_delivery_pre_l,
		REFCYC_PER_LINE_DELIVERY_PRE_C, dlg_attr->refcyc_per_line_delivery_pre_c);

	REG_SET(DCN_SURF0_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE,
		ttu_attr->refcyc_per_req_delivery_pre_l);
	REG_SET(DCN_SURF1_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE,
		ttu_attr->refcyc_per_req_delivery_pre_c);
	REG_SET(DCN_CUR0_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE, ttu_attr->refcyc_per_req_delivery_pre_cur0);

	REG_SET_2(DCN_GLOBAL_TTU_CNTL, 0,
		MIN_TTU_VBLANK, ttu_attr->min_ttu_vblank,
		QoS_LEVEL_FLIP, ttu_attr->qos_level_flip);
}

bool hubp1_is_flip_pending(struct hubp *hubp)
{
	uint32_t flip_pending = 0;
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	struct dc_plane_address earliest_inuse_address;

	REG_GET(DCSURF_FLIP_CONTROL,
			SURFACE_FLIP_PENDING, &flip_pending);

	REG_GET(DCSURF_SURFACE_EARLIEST_INUSE,
			SURFACE_EARLIEST_INUSE_ADDRESS, &earliest_inuse_address.grph.addr.low_part);

	REG_GET(DCSURF_SURFACE_EARLIEST_INUSE_HIGH,
			SURFACE_EARLIEST_INUSE_ADDRESS_HIGH, &earliest_inuse_address.grph.addr.high_part);

	if (flip_pending)
		return true;

	if (earliest_inuse_address.grph.addr.quad_part != hubp->request_address.grph.addr.quad_part)
		return true;

	return false;
}

uint32_t aperture_default_system = 1;
uint32_t context0_default_system; /* = 0;*/

static void hubp1_set_vm_system_aperture_settings(struct hubp *hubp,
		struct vm_system_aperture_param *apt)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	PHYSICAL_ADDRESS_LOC mc_vm_apt_default;
	PHYSICAL_ADDRESS_LOC mc_vm_apt_low;
	PHYSICAL_ADDRESS_LOC mc_vm_apt_high;

	mc_vm_apt_default.quad_part = apt->sys_default.quad_part >> 12;
	mc_vm_apt_low.quad_part = apt->sys_low.quad_part >> 12;
	mc_vm_apt_high.quad_part = apt->sys_high.quad_part >> 12;

	REG_SET_2(DCN_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB, 0,
		MC_VM_SYSTEM_APERTURE_DEFAULT_SYSTEM, aperture_default_system, /* 1 = system physical memory */
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

static void hubp1_set_vm_context0_settings(struct hubp *hubp,
		const struct vm_context0_param *vm0)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
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
	REG_SET_2(DCN_VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR_MSB, 0,
			VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR_MSB, vm0->fault_default.high_part,
			VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_SYSTEM, context0_default_system);
	REG_SET(DCN_VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR_LSB, 0,
			VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR_LSB, vm0->fault_default.low_part);

	/* control: enable VM PTE*/
	REG_SET_2(DCN_VM_MX_L1_TLB_CNTL, 0,
			ENABLE_L1_TLB, 1,
			SYSTEM_ACCESS_MODE, 3);
}

void min_set_viewport(
	struct hubp *hubp,
	const struct rect *viewport,
	const struct rect *viewport_c)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

	REG_SET_2(DCSURF_PRI_VIEWPORT_DIMENSION, 0,
		  PRI_VIEWPORT_WIDTH, viewport->width,
		  PRI_VIEWPORT_HEIGHT, viewport->height);

	REG_SET_2(DCSURF_PRI_VIEWPORT_START, 0,
		  PRI_VIEWPORT_X_START, viewport->x,
		  PRI_VIEWPORT_Y_START, viewport->y);

	/*for stereo*/
	REG_SET_2(DCSURF_SEC_VIEWPORT_DIMENSION, 0,
		  SEC_VIEWPORT_WIDTH, viewport->width,
		  SEC_VIEWPORT_HEIGHT, viewport->height);

	REG_SET_2(DCSURF_SEC_VIEWPORT_START, 0,
		  SEC_VIEWPORT_X_START, viewport->x,
		  SEC_VIEWPORT_Y_START, viewport->y);

	/* DC supports NV12 only at the moment */
	REG_SET_2(DCSURF_PRI_VIEWPORT_DIMENSION_C, 0,
		  PRI_VIEWPORT_WIDTH_C, viewport_c->width,
		  PRI_VIEWPORT_HEIGHT_C, viewport_c->height);

	REG_SET_2(DCSURF_PRI_VIEWPORT_START_C, 0,
		  PRI_VIEWPORT_X_START_C, viewport_c->x,
		  PRI_VIEWPORT_Y_START_C, viewport_c->y);
}

void hubp1_read_state(struct hubp *hubp)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	struct dcn_hubp_state *s = &hubp1->state;
	struct _vcs_dpi_display_dlg_regs_st *dlg_attr = &s->dlg_attr;
	struct _vcs_dpi_display_ttu_regs_st *ttu_attr = &s->ttu_attr;
	struct _vcs_dpi_display_rq_regs_st *rq_regs = &s->rq_regs;

	/* Requester */
	REG_GET(HUBPRET_CONTROL,
			DET_BUF_PLANE1_BASE_ADDRESS, &rq_regs->plane1_base_address);
	REG_GET_4(DCN_EXPANSION_MODE,
			DRQ_EXPANSION_MODE, &rq_regs->drq_expansion_mode,
			PRQ_EXPANSION_MODE, &rq_regs->prq_expansion_mode,
			MRQ_EXPANSION_MODE, &rq_regs->mrq_expansion_mode,
			CRQ_EXPANSION_MODE, &rq_regs->crq_expansion_mode);
	REG_GET_8(DCHUBP_REQ_SIZE_CONFIG,
		CHUNK_SIZE, &rq_regs->rq_regs_l.chunk_size,
		MIN_CHUNK_SIZE, &rq_regs->rq_regs_l.min_chunk_size,
		META_CHUNK_SIZE, &rq_regs->rq_regs_l.meta_chunk_size,
		MIN_META_CHUNK_SIZE, &rq_regs->rq_regs_l.min_meta_chunk_size,
		DPTE_GROUP_SIZE, &rq_regs->rq_regs_l.dpte_group_size,
		MPTE_GROUP_SIZE, &rq_regs->rq_regs_l.mpte_group_size,
		SWATH_HEIGHT, &rq_regs->rq_regs_l.swath_height,
		PTE_ROW_HEIGHT_LINEAR, &rq_regs->rq_regs_l.pte_row_height_linear);
	REG_GET_8(DCHUBP_REQ_SIZE_CONFIG_C,
		CHUNK_SIZE_C, &rq_regs->rq_regs_c.chunk_size,
		MIN_CHUNK_SIZE_C, &rq_regs->rq_regs_c.min_chunk_size,
		META_CHUNK_SIZE_C, &rq_regs->rq_regs_c.meta_chunk_size,
		MIN_META_CHUNK_SIZE_C, &rq_regs->rq_regs_c.min_meta_chunk_size,
		DPTE_GROUP_SIZE_C, &rq_regs->rq_regs_c.dpte_group_size,
		MPTE_GROUP_SIZE_C, &rq_regs->rq_regs_c.mpte_group_size,
		SWATH_HEIGHT_C, &rq_regs->rq_regs_c.swath_height,
		PTE_ROW_HEIGHT_LINEAR_C, &rq_regs->rq_regs_c.pte_row_height_linear);

	/* DLG - Per hubp */
	REG_GET_2(BLANK_OFFSET_0,
		REFCYC_H_BLANK_END, &dlg_attr->refcyc_h_blank_end,
		DLG_V_BLANK_END, &dlg_attr->dlg_vblank_end);

	REG_GET(BLANK_OFFSET_1,
		MIN_DST_Y_NEXT_START, &dlg_attr->min_dst_y_next_start);

	REG_GET(DST_DIMENSIONS,
		REFCYC_PER_HTOTAL, &dlg_attr->refcyc_per_htotal);

	REG_GET_2(DST_AFTER_SCALER,
		REFCYC_X_AFTER_SCALER, &dlg_attr->refcyc_x_after_scaler,
		DST_Y_AFTER_SCALER, &dlg_attr->dst_y_after_scaler);

	if (REG(PREFETCH_SETTINS))
		REG_GET_2(PREFETCH_SETTINS,
			DST_Y_PREFETCH, &dlg_attr->dst_y_prefetch,
			VRATIO_PREFETCH, &dlg_attr->vratio_prefetch);
	else
		REG_GET_2(PREFETCH_SETTINGS,
			DST_Y_PREFETCH, &dlg_attr->dst_y_prefetch,
			VRATIO_PREFETCH, &dlg_attr->vratio_prefetch);

	REG_GET_2(VBLANK_PARAMETERS_0,
		DST_Y_PER_VM_VBLANK, &dlg_attr->dst_y_per_vm_vblank,
		DST_Y_PER_ROW_VBLANK, &dlg_attr->dst_y_per_row_vblank);

	REG_GET(REF_FREQ_TO_PIX_FREQ,
		REF_FREQ_TO_PIX_FREQ, &dlg_attr->ref_freq_to_pix_freq);

	/* DLG - Per luma/chroma */
	REG_GET(VBLANK_PARAMETERS_1,
		REFCYC_PER_PTE_GROUP_VBLANK_L, &dlg_attr->refcyc_per_pte_group_vblank_l);

	REG_GET(VBLANK_PARAMETERS_3,
		REFCYC_PER_META_CHUNK_VBLANK_L, &dlg_attr->refcyc_per_meta_chunk_vblank_l);

	if (REG(NOM_PARAMETERS_0))
		REG_GET(NOM_PARAMETERS_0,
			DST_Y_PER_PTE_ROW_NOM_L, &dlg_attr->dst_y_per_pte_row_nom_l);

	if (REG(NOM_PARAMETERS_1))
		REG_GET(NOM_PARAMETERS_1,
			REFCYC_PER_PTE_GROUP_NOM_L, &dlg_attr->refcyc_per_pte_group_nom_l);

	REG_GET(NOM_PARAMETERS_4,
		DST_Y_PER_META_ROW_NOM_L, &dlg_attr->dst_y_per_meta_row_nom_l);

	REG_GET(NOM_PARAMETERS_5,
		REFCYC_PER_META_CHUNK_NOM_L, &dlg_attr->refcyc_per_meta_chunk_nom_l);

	REG_GET_2(PER_LINE_DELIVERY_PRE,
		REFCYC_PER_LINE_DELIVERY_PRE_L, &dlg_attr->refcyc_per_line_delivery_pre_l,
		REFCYC_PER_LINE_DELIVERY_PRE_C, &dlg_attr->refcyc_per_line_delivery_pre_c);

	REG_GET_2(PER_LINE_DELIVERY,
		REFCYC_PER_LINE_DELIVERY_L, &dlg_attr->refcyc_per_line_delivery_l,
		REFCYC_PER_LINE_DELIVERY_C, &dlg_attr->refcyc_per_line_delivery_c);

	if (REG(PREFETCH_SETTINS_C))
		REG_GET(PREFETCH_SETTINS_C,
			VRATIO_PREFETCH_C, &dlg_attr->vratio_prefetch_c);
	else
		REG_GET(PREFETCH_SETTINGS_C,
			VRATIO_PREFETCH_C, &dlg_attr->vratio_prefetch_c);

	REG_GET(VBLANK_PARAMETERS_2,
		REFCYC_PER_PTE_GROUP_VBLANK_C, &dlg_attr->refcyc_per_pte_group_vblank_c);

	REG_GET(VBLANK_PARAMETERS_4,
		REFCYC_PER_META_CHUNK_VBLANK_C, &dlg_attr->refcyc_per_meta_chunk_vblank_c);

	if (REG(NOM_PARAMETERS_2))
		REG_GET(NOM_PARAMETERS_2,
			DST_Y_PER_PTE_ROW_NOM_C, &dlg_attr->dst_y_per_pte_row_nom_c);

	if (REG(NOM_PARAMETERS_3))
		REG_GET(NOM_PARAMETERS_3,
			REFCYC_PER_PTE_GROUP_NOM_C, &dlg_attr->refcyc_per_pte_group_nom_c);

	REG_GET(NOM_PARAMETERS_6,
		DST_Y_PER_META_ROW_NOM_C, &dlg_attr->dst_y_per_meta_row_nom_c);

	REG_GET(NOM_PARAMETERS_7,
		REFCYC_PER_META_CHUNK_NOM_C, &dlg_attr->refcyc_per_meta_chunk_nom_c);

	/* TTU - per hubp */
	REG_GET_2(DCN_TTU_QOS_WM,
		QoS_LEVEL_LOW_WM, &ttu_attr->qos_level_low_wm,
		QoS_LEVEL_HIGH_WM, &ttu_attr->qos_level_high_wm);

	REG_GET_2(DCN_GLOBAL_TTU_CNTL,
		MIN_TTU_VBLANK, &ttu_attr->min_ttu_vblank,
		QoS_LEVEL_FLIP, &ttu_attr->qos_level_flip);

	/* TTU - per luma/chroma */
	/* Assumed surf0 is luma and 1 is chroma */

	REG_GET_3(DCN_SURF0_TTU_CNTL0,
		REFCYC_PER_REQ_DELIVERY, &ttu_attr->refcyc_per_req_delivery_l,
		QoS_LEVEL_FIXED, &ttu_attr->qos_level_fixed_l,
		QoS_RAMP_DISABLE, &ttu_attr->qos_ramp_disable_l);

	REG_GET(DCN_SURF0_TTU_CNTL1,
		REFCYC_PER_REQ_DELIVERY_PRE,
		&ttu_attr->refcyc_per_req_delivery_pre_l);

	REG_GET_3(DCN_SURF1_TTU_CNTL0,
		REFCYC_PER_REQ_DELIVERY, &ttu_attr->refcyc_per_req_delivery_c,
		QoS_LEVEL_FIXED, &ttu_attr->qos_level_fixed_c,
		QoS_RAMP_DISABLE, &ttu_attr->qos_ramp_disable_c);

	REG_GET(DCN_SURF1_TTU_CNTL1,
		REFCYC_PER_REQ_DELIVERY_PRE,
		&ttu_attr->refcyc_per_req_delivery_pre_c);

	/* Rest of hubp */
	REG_GET(DCSURF_SURFACE_CONFIG,
			SURFACE_PIXEL_FORMAT, &s->pixel_format);

	REG_GET(DCSURF_SURFACE_EARLIEST_INUSE_HIGH,
			SURFACE_EARLIEST_INUSE_ADDRESS_HIGH, &s->inuse_addr_hi);

	REG_GET(DCSURF_SURFACE_EARLIEST_INUSE,
			SURFACE_EARLIEST_INUSE_ADDRESS, &s->inuse_addr_lo);

	REG_GET_2(DCSURF_PRI_VIEWPORT_DIMENSION,
			PRI_VIEWPORT_WIDTH, &s->viewport_width,
			PRI_VIEWPORT_HEIGHT, &s->viewport_height);

	REG_GET_2(DCSURF_SURFACE_CONFIG,
			ROTATION_ANGLE, &s->rotation_angle,
			H_MIRROR_EN, &s->h_mirror_en);

	REG_GET(DCSURF_TILING_CONFIG,
			SW_MODE, &s->sw_mode);

	REG_GET(DCSURF_SURFACE_CONTROL,
			PRIMARY_SURFACE_DCC_EN, &s->dcc_en);

	REG_GET_3(DCHUBP_CNTL,
			HUBP_BLANK_EN, &s->blank_en,
			HUBP_TTU_DISABLE, &s->ttu_disable,
			HUBP_UNDERFLOW_STATUS, &s->underflow_status);

	REG_GET(DCN_GLOBAL_TTU_CNTL,
			MIN_TTU_VBLANK, &s->min_ttu_vblank);

	REG_GET_2(DCN_TTU_QOS_WM,
			QoS_LEVEL_LOW_WM, &s->qos_level_low_wm,
			QoS_LEVEL_HIGH_WM, &s->qos_level_high_wm);
}

enum cursor_pitch hubp1_get_cursor_pitch(unsigned int pitch)
{
	enum cursor_pitch hw_pitch;

	switch (pitch) {
	case 64:
		hw_pitch = CURSOR_PITCH_64_PIXELS;
		break;
	case 128:
		hw_pitch = CURSOR_PITCH_128_PIXELS;
		break;
	case 256:
		hw_pitch = CURSOR_PITCH_256_PIXELS;
		break;
	default:
		DC_ERR("Invalid cursor pitch of %d. "
				"Only 64/128/256 is supported on DCN.\n", pitch);
		hw_pitch = CURSOR_PITCH_64_PIXELS;
		break;
	}
	return hw_pitch;
}

static enum cursor_lines_per_chunk hubp1_get_lines_per_chunk(
		unsigned int cur_width,
		enum dc_cursor_color_format format)
{
	enum cursor_lines_per_chunk line_per_chunk;

	if (format == CURSOR_MODE_MONO)
		/* impl B. expansion in CUR Buffer reader */
		line_per_chunk = CURSOR_LINE_PER_CHUNK_16;
	else if (cur_width <= 32)
		line_per_chunk = CURSOR_LINE_PER_CHUNK_16;
	else if (cur_width <= 64)
		line_per_chunk = CURSOR_LINE_PER_CHUNK_8;
	else if (cur_width <= 128)
		line_per_chunk = CURSOR_LINE_PER_CHUNK_4;
	else
		line_per_chunk = CURSOR_LINE_PER_CHUNK_2;

	return line_per_chunk;
}

void hubp1_cursor_set_attributes(
		struct hubp *hubp,
		const struct dc_cursor_attributes *attr)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	enum cursor_pitch hw_pitch = hubp1_get_cursor_pitch(attr->pitch);
	enum cursor_lines_per_chunk lpc = hubp1_get_lines_per_chunk(
			attr->width, attr->color_format);

	hubp->curs_attr = *attr;

	REG_UPDATE(CURSOR_SURFACE_ADDRESS_HIGH,
			CURSOR_SURFACE_ADDRESS_HIGH, attr->address.high_part);
	REG_UPDATE(CURSOR_SURFACE_ADDRESS,
			CURSOR_SURFACE_ADDRESS, attr->address.low_part);

	REG_UPDATE_2(CURSOR_SIZE,
			CURSOR_WIDTH, attr->width,
			CURSOR_HEIGHT, attr->height);

	REG_UPDATE_3(CURSOR_CONTROL,
			CURSOR_MODE, attr->color_format,
			CURSOR_PITCH, hw_pitch,
			CURSOR_LINES_PER_CHUNK, lpc);

	REG_SET_2(CURSOR_SETTINS, 0,
			/* no shift of the cursor HDL schedule */
			CURSOR0_DST_Y_OFFSET, 0,
			 /* used to shift the cursor chunk request deadline */
			CURSOR0_CHUNK_HDL_ADJUST, 3);
}

void hubp1_cursor_set_position(
		struct hubp *hubp,
		const struct dc_cursor_position *pos,
		const struct dc_cursor_mi_param *param)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	int src_x_offset = pos->x - pos->x_hotspot - param->viewport.x;
	int src_y_offset = pos->y - pos->y_hotspot - param->viewport.y;
	int x_hotspot = pos->x_hotspot;
	int y_hotspot = pos->y_hotspot;
	uint32_t dst_x_offset;
	uint32_t cur_en = pos->enable ? 1 : 0;

	/*
	 * Guard aganst cursor_set_position() from being called with invalid
	 * attributes
	 *
	 * TODO: Look at combining cursor_set_position() and
	 * cursor_set_attributes() into cursor_update()
	 */
	if (hubp->curs_attr.address.quad_part == 0)
		return;

	if (param->rotation == ROTATION_ANGLE_90 || param->rotation == ROTATION_ANGLE_270) {
		src_x_offset = pos->y - pos->y_hotspot - param->viewport.x;
		y_hotspot = pos->x_hotspot;
		x_hotspot = pos->y_hotspot;
	}

	if (param->mirror) {
		x_hotspot = param->viewport.width - x_hotspot;
		src_x_offset = param->viewport.x + param->viewport.width - src_x_offset;
	}

	dst_x_offset = (src_x_offset >= 0) ? src_x_offset : 0;
	dst_x_offset *= param->ref_clk_khz;
	dst_x_offset /= param->pixel_clk_khz;

	ASSERT(param->h_scale_ratio.value);

	if (param->h_scale_ratio.value)
		dst_x_offset = dc_fixpt_floor(dc_fixpt_div(
				dc_fixpt_from_int(dst_x_offset),
				param->h_scale_ratio));

	if (src_x_offset >= (int)param->viewport.width)
		cur_en = 0;  /* not visible beyond right edge*/

	if (src_x_offset + (int)hubp->curs_attr.width <= 0)
		cur_en = 0;  /* not visible beyond left edge*/

	if (src_y_offset >= (int)param->viewport.height)
		cur_en = 0;  /* not visible beyond bottom edge*/

	if (src_y_offset + (int)hubp->curs_attr.height <= 0)
		cur_en = 0;  /* not visible beyond top edge*/

	if (cur_en && REG_READ(CURSOR_SURFACE_ADDRESS) == 0)
		hubp->funcs->set_cursor_attributes(hubp, &hubp->curs_attr);

	REG_UPDATE(CURSOR_CONTROL,
			CURSOR_ENABLE, cur_en);

	//account for cases where we see negative offset relative to overlay plane
	if (src_x_offset < 0 && src_y_offset < 0) {
		REG_SET_2(CURSOR_POSITION, 0,
			CURSOR_X_POSITION, 0,
			CURSOR_Y_POSITION, 0);
		x_hotspot -= src_x_offset;
		y_hotspot -= src_y_offset;
	} else if (src_x_offset < 0) {
		REG_SET_2(CURSOR_POSITION, 0,
			CURSOR_X_POSITION, 0,
			CURSOR_Y_POSITION, pos->y);
		x_hotspot -= src_x_offset;
	} else if (src_y_offset < 0) {
		REG_SET_2(CURSOR_POSITION, 0,
			CURSOR_X_POSITION, pos->x,
			CURSOR_Y_POSITION, 0);
		y_hotspot -= src_y_offset;
	} else {
		REG_SET_2(CURSOR_POSITION, 0,
				CURSOR_X_POSITION, pos->x,
				CURSOR_Y_POSITION, pos->y);
	}

	REG_SET_2(CURSOR_HOT_SPOT, 0,
			CURSOR_HOT_SPOT_X, x_hotspot,
			CURSOR_HOT_SPOT_Y, y_hotspot);

	REG_SET(CURSOR_DST_OFFSET, 0,
			CURSOR_DST_X_OFFSET, dst_x_offset);
	/* TODO Handle surface pixel formats other than 4:4:4 */
}

void hubp1_clk_cntl(struct hubp *hubp, bool enable)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);
	uint32_t clk_enable = enable ? 1 : 0;

	REG_UPDATE(HUBP_CLK_CNTL, HUBP_CLOCK_ENABLE, clk_enable);
}

void hubp1_vtg_sel(struct hubp *hubp, uint32_t otg_inst)
{
	struct dcn10_hubp *hubp1 = TO_DCN10_HUBP(hubp);

	REG_UPDATE(DCHUBP_CNTL, HUBP_VTG_SEL, otg_inst);
}

static const struct hubp_funcs dcn10_hubp_funcs = {
	.hubp_program_surface_flip_and_addr =
			hubp1_program_surface_flip_and_addr,
	.hubp_program_surface_config =
			hubp1_program_surface_config,
	.hubp_is_flip_pending = hubp1_is_flip_pending,
	.hubp_setup = hubp1_setup,
	.hubp_setup_interdependent = hubp1_setup_interdependent,
	.hubp_set_vm_system_aperture_settings = hubp1_set_vm_system_aperture_settings,
	.hubp_set_vm_context0_settings = hubp1_set_vm_context0_settings,
	.set_blank = hubp1_set_blank,
	.dcc_control = hubp1_dcc_control,
	.mem_program_viewport = min_set_viewport,
	.set_hubp_blank_en = hubp1_set_hubp_blank_en,
	.set_cursor_attributes	= hubp1_cursor_set_attributes,
	.set_cursor_position	= hubp1_cursor_set_position,
	.hubp_disconnect = hubp1_disconnect,
	.hubp_clk_cntl = hubp1_clk_cntl,
	.hubp_vtg_sel = hubp1_vtg_sel,
	.hubp_read_state = hubp1_read_state,
	.hubp_clear_underflow = hubp1_clear_underflow,
	.hubp_disable_control =  hubp1_disable_control,
	.hubp_get_underflow_status = hubp1_get_underflow_status,

};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

void dcn10_hubp_construct(
	struct dcn10_hubp *hubp1,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_mi_registers *hubp_regs,
	const struct dcn_mi_shift *hubp_shift,
	const struct dcn_mi_mask *hubp_mask)
{
	hubp1->base.funcs = &dcn10_hubp_funcs;
	hubp1->base.ctx = ctx;
	hubp1->hubp_regs = hubp_regs;
	hubp1->hubp_shift = hubp_shift;
	hubp1->hubp_mask = hubp_mask;
	hubp1->base.inst = inst;
	hubp1->base.opp_id = 0xf;
	hubp1->base.mpcc_id = 0xf;
}


