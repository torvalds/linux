// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "dce_calcs.h"
#include "reg_helper.h"
#include "basics/conversion.h"
#include "dcn60_hubp.h"

#define REG(reg)\
	hubp2->hubp_regs->reg

#define CTX \
	hubp2->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubp2->hubp_shift->field_name, hubp2->hubp_mask->field_name

static void hubp60_program_deadline(
	struct hubp *hubp,
	struct dml2_display_dlg_regs *dlg_attr,
	struct dml2_display_ttu_regs *ttu_attr)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

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

	/* TTU - per luma/chroma */
	/* TO DO: Set URGENT_FORCE_x fields with values from DML if/when available */
	REG_SET_3(DCN_SURF0_TTU_CNTL0, 0,
		REFCYC_PER_REQ_DELIVERY, ttu_attr->refcyc_per_req_delivery_l,
		URGENT_FORCE_VALUE, 0,
		URGENT_FORCE_EN, 0);

	REG_SET_3(DCN_SURF1_TTU_CNTL0, 0,
		REFCYC_PER_REQ_DELIVERY, ttu_attr->refcyc_per_req_delivery_c,
		URGENT_FORCE_VALUE, 0,
		URGENT_FORCE_EN, 0);

	REG_SET_3(DCN_CUR0_TTU_CNTL0, 0,
		REFCYC_PER_REQ_DELIVERY, ttu_attr->refcyc_per_req_delivery_cur0,
		URGENT_FORCE_VALUE, 0,
		URGENT_FORCE_EN, 0);

	REG_SET(FLIP_PARAMETERS_1, 0,
		REFCYC_PER_PTE_GROUP_FLIP_L, dlg_attr->refcyc_per_pte_group_flip_l);
	REG_SET(HUBP_3DLUT_DLG_PARAM, 0, REFCYC_PER_3DLUT_GROUP, dlg_attr->refcyc_per_tdlut_group);

	REG_UPDATE(DCN_DMDATA_VM_CNTL,
		REFCYC_PER_VM_DMDATA, dlg_attr->refcyc_per_vm_dmdata);
}

void hubp60_setup(
	struct hubp *hubp,
	struct dml2_dchub_per_pipe_register_set *pipe_regs,
	union dml2_global_sync_programming *pipe_global_sync,
	struct dc_crtc_timing *timing)
{
	/* otg is locked when this func is called. Register are double buffered.
	 * disable the requestors is not needed
	 */
	hubp401_vready_at_or_After_vsync(hubp, pipe_global_sync, timing);
	hubp401_program_requestor(hubp, &pipe_regs->rq_regs);
	hubp60_program_deadline(hubp, &pipe_regs->dlg_regs, &pipe_regs->ttu_regs);
}

void hubp60_setup_interdependent(
	struct hubp *hubp,
	struct dml2_dchub_per_pipe_register_set *pipe_regs)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_SET_3(PREFETCH_SETTINGS, 0,
		DST_Y_PREFETCH, pipe_regs->dlg_regs.dst_y_prefetch,
		VRATIO_PREFETCH, pipe_regs->dlg_regs.vratio_prefetch,
		FORCE_DISP_PREF_TO_VBLANK, pipe_regs->dlg_regs.force_prefetch_to_vblank);

	REG_SET(PREFETCH_SETTINGS_C, 0,
		VRATIO_PREFETCH_C, pipe_regs->dlg_regs.vratio_prefetch_c);

	REG_SET_2(VBLANK_PARAMETERS_0, 0,
		DST_Y_PER_VM_VBLANK, pipe_regs->dlg_regs.dst_y_per_vm_vblank,
		DST_Y_PER_ROW_VBLANK, pipe_regs->dlg_regs.dst_y_per_row_vblank);

	REG_SET_2(FLIP_PARAMETERS_0, 0,
		DST_Y_PER_VM_FLIP, pipe_regs->dlg_regs.dst_y_per_vm_flip,
		DST_Y_PER_ROW_FLIP, pipe_regs->dlg_regs.dst_y_per_row_flip);

	REG_SET(VBLANK_PARAMETERS_3, 0,
		REFCYC_PER_META_CHUNK_VBLANK_L, pipe_regs->dlg_regs.refcyc_per_meta_chunk_vblank_l);

	REG_SET(VBLANK_PARAMETERS_4, 0,
		REFCYC_PER_META_CHUNK_VBLANK_C, pipe_regs->dlg_regs.refcyc_per_meta_chunk_vblank_c);

	REG_SET(FLIP_PARAMETERS_2, 0,
		REFCYC_PER_META_CHUNK_FLIP_L, pipe_regs->dlg_regs.refcyc_per_meta_chunk_flip_l);

	REG_SET_2(PER_LINE_DELIVERY_PRE, 0,
		REFCYC_PER_LINE_DELIVERY_PRE_L, pipe_regs->dlg_regs.refcyc_per_line_delivery_pre_l,
		REFCYC_PER_LINE_DELIVERY_PRE_C, pipe_regs->dlg_regs.refcyc_per_line_delivery_pre_c);

	REG_SET(DCN_SURF0_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE,
		pipe_regs->ttu_regs.refcyc_per_req_delivery_pre_l);

	REG_SET(DCN_SURF1_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE,
		pipe_regs->ttu_regs.refcyc_per_req_delivery_pre_c);

	REG_SET(DCN_CUR0_TTU_CNTL1, 0,
		REFCYC_PER_REQ_DELIVERY_PRE, pipe_regs->ttu_regs.refcyc_per_req_delivery_pre_cur0);

	REG_SET(DCN_GLOBAL_TTU_CNTL, 0,
		MIN_TTU_VBLANK, pipe_regs->ttu_regs.min_ttu_vblank);

	REG_SET(DST_Y_DELTA_DRQ_LIMIT, 0,
			DST_Y_DELTA_DRQ_LIMIT, pipe_regs->dlg_regs.dst_y_delta_drq_limit);

	REG_SET(DST_Y_ALT_CH_DRQ_LIMIT, 0,
			DST_Y_ALT_CH_DRQ_LIMIT, pipe_regs->dlg_regs.dst_y_svp_drq_limit);
}

void hubp60_cursor_set_attributes(
	struct hubp *hubp,
	const struct dc_cursor_attributes *attr)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	enum cursor_pitch hw_pitch = hubp1_get_cursor_pitch(attr->pitch);
	enum cursor_lines_per_chunk lpc = hubp2_get_lines_per_chunk(
		attr->width, attr->color_format);

	//Round cursor width up to next multiple of 64
	uint32_t cursor_width = ((attr->width + 63) / 64) * 64;

	hubp->curs_attr = *attr;

	if (!hubp->cursor_offload) {
		REG_UPDATE(CURSOR_SURFACE_ADDRESS_HIGH,
			CURSOR_SURFACE_ADDRESS_HIGH, attr->address.high_part);
		REG_UPDATE(CURSOR_SURFACE_ADDRESS,
			CURSOR_SURFACE_ADDRESS, attr->address.low_part);

		REG_UPDATE_2(CURSOR_SIZE,
			CURSOR_WIDTH, cursor_width,
			CURSOR_HEIGHT, attr->height);

		REG_UPDATE_4(CURSOR_CONTROL,
			CURSOR_MODE, attr->color_format,
			CURSOR_2X_MAGNIFY, attr->attribute_flags.bits.ENABLE_MAGNIFICATION,
			CURSOR_PITCH, hw_pitch,
			CURSOR_LINES_PER_CHUNK, lpc);

		REG_SET_3(CURSOR_SETTINGS, 0,
			/* no shift of the cursor HDL schedule */
			CURSOR0_DST_Y_OFFSET, 0,
			/* used to shift the cursor chunk request deadline */
			CURSOR0_CHUNK_HDL_ADJUST, 3,
			FORCE_CURSOR_TO_DISP_PREF, attr->force_cursor_to_disp_pref);
	}
	hubp->att.SURFACE_ADDR_HIGH  = attr->address.high_part;
	hubp->att.SURFACE_ADDR       = attr->address.low_part;
	hubp->att.size.bits.width    = attr->width;
	hubp->att.size.bits.height   = attr->height;
	hubp->att.cur_ctl.bits.mode  = attr->color_format;

	hubp->cur_rect.w = attr->width;
	hubp->cur_rect.h = attr->height;

	hubp->att.cur_ctl.bits.pitch = hw_pitch;
	hubp->att.cur_ctl.bits.line_per_chunk = lpc;
	hubp->att.cur_ctl.bits.cur_2x_magnify = attr->attribute_flags.bits.ENABLE_MAGNIFICATION;
	hubp->att.settings.bits.dst_y_offset  = 0;
	hubp->att.settings.bits.chunk_hdl_adjust = 3;
	hubp->att.settings.bits.force_cursor_to_disp_pref = attr->force_cursor_to_disp_pref;
}

void hubp60_read_state(struct hubp *hubp)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	struct dcn_hubp_state *s = &hubp2->state;
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

	REG_GET_5(DCHUBP_REQ_SIZE_CONFIG,
		CHUNK_SIZE, &rq_regs->rq_regs_l.chunk_size,
		MIN_CHUNK_SIZE, &rq_regs->rq_regs_l.min_chunk_size,
		DPTE_GROUP_SIZE, &rq_regs->rq_regs_l.dpte_group_size,
		SWATH_HEIGHT, &rq_regs->rq_regs_l.swath_height,
		PTE_ROW_HEIGHT_LINEAR, &rq_regs->rq_regs_l.pte_row_height_linear);

	REG_GET_5(DCHUBP_REQ_SIZE_CONFIG_C,
		CHUNK_SIZE_C, &rq_regs->rq_regs_c.chunk_size,
		MIN_CHUNK_SIZE_C, &rq_regs->rq_regs_c.min_chunk_size,
		DPTE_GROUP_SIZE_C, &rq_regs->rq_regs_c.dpte_group_size,
		SWATH_HEIGHT_C, &rq_regs->rq_regs_c.swath_height,
		PTE_ROW_HEIGHT_LINEAR_C, &rq_regs->rq_regs_c.pte_row_height_linear);

	REG_GET(DCN_VM_SYSTEM_APERTURE_HIGH_ADDR,
		MC_VM_SYSTEM_APERTURE_HIGH_ADDR, &rq_regs->aperture_high_addr);

	REG_GET(DCN_VM_SYSTEM_APERTURE_LOW_ADDR,
		MC_VM_SYSTEM_APERTURE_LOW_ADDR, &rq_regs->aperture_low_addr);

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

	REG_GET(NOM_PARAMETERS_0,
		DST_Y_PER_PTE_ROW_NOM_L, &dlg_attr->dst_y_per_pte_row_nom_l);

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

	REG_GET(PREFETCH_SETTINGS_C,
		VRATIO_PREFETCH_C, &dlg_attr->vratio_prefetch_c);

	REG_GET(VBLANK_PARAMETERS_2,
		REFCYC_PER_PTE_GROUP_VBLANK_C, &dlg_attr->refcyc_per_pte_group_vblank_c);

	REG_GET(VBLANK_PARAMETERS_4,
		REFCYC_PER_META_CHUNK_VBLANK_C, &dlg_attr->refcyc_per_meta_chunk_vblank_c);

	REG_GET(NOM_PARAMETERS_2,
		DST_Y_PER_PTE_ROW_NOM_C, &dlg_attr->dst_y_per_pte_row_nom_c);

	REG_GET(NOM_PARAMETERS_3,
		REFCYC_PER_PTE_GROUP_NOM_C, &dlg_attr->refcyc_per_pte_group_nom_c);

	REG_GET(NOM_PARAMETERS_6,
		DST_Y_PER_META_ROW_NOM_C, &dlg_attr->dst_y_per_meta_row_nom_c);

	REG_GET(NOM_PARAMETERS_7,
		REFCYC_PER_META_CHUNK_NOM_C, &dlg_attr->refcyc_per_meta_chunk_nom_c);

	REG_GET(DCN_GLOBAL_TTU_CNTL,
		MIN_TTU_VBLANK, &ttu_attr->min_ttu_vblank);

	/* TTU - per luma/chroma */
	/* Assumed surf0 is luma and 1 is chroma */

	REG_GET(DCN_SURF0_TTU_CNTL0,
		REFCYC_PER_REQ_DELIVERY, &ttu_attr->refcyc_per_req_delivery_l);

	REG_GET(DCN_SURF0_TTU_CNTL1,
		REFCYC_PER_REQ_DELIVERY_PRE,
		&ttu_attr->refcyc_per_req_delivery_pre_l);

	REG_GET(DCN_SURF1_TTU_CNTL0,
		REFCYC_PER_REQ_DELIVERY, &ttu_attr->refcyc_per_req_delivery_c);

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

	REG_GET(HUBP_CLK_CNTL,
		HUBP_CLOCK_ENABLE, &s->clock_en);

	REG_GET(DCN_GLOBAL_TTU_CNTL,
		MIN_TTU_VBLANK, &s->min_ttu_vblank);

	REG_GET(DCSURF_PRIMARY_SURFACE_ADDRESS,
		PRIMARY_SURFACE_ADDRESS, &s->primary_surface_addr_lo);

	REG_GET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH,
		PRIMARY_SURFACE_ADDRESS, &s->primary_surface_addr_hi);

	s->uclk_pstate_force = REG_READ(UCLK_PSTATE_FORCE);

	s->hubp_cntl = REG_READ(DCHUBP_CNTL);
	s->flip_control = REG_READ(DCSURF_FLIP_CONTROL);
}

static struct hubp_funcs dcn60_hubp_funcs = {
	.hubp_enable_tripleBuffer = hubp2_enable_triplebuffer,
	.hubp_is_triplebuffer_enabled = hubp2_is_triplebuffer_enabled,
	.hubp_program_surface_flip_and_addr = hubp50_program_surface_flip_and_addr,
	.hubp_program_surface_config = hubp50_program_surface_config,
	.hubp_is_flip_pending = hubp2_is_flip_pending,
	.hubp_setup2 = hubp60_setup,
	.hubp_setup_interdependent2 = hubp60_setup_interdependent,
	.hubp_set_vm_system_aperture_settings = hubp3_set_vm_system_aperture_settings,
	.set_blank = hubp2_set_blank,
	.set_blank_regs = hubp2_set_blank_regs,
	.hubp_reset = hubp_reset,
	.mem_program_viewport = hubp401_set_viewport,
	.set_cursor_attributes	= hubp60_cursor_set_attributes,
	.set_cursor_position	= hubp401_cursor_set_position,
	.hubp_clk_cntl = hubp2_clk_cntl,
	.hubp_vtg_sel = hubp2_vtg_sel,
	.dmdata_set_attributes = hubp3_dmdata_set_attributes,
	.dmdata_load = hubp2_dmdata_load,
	.dmdata_status_done = hubp2_dmdata_status_done,
	.hubp_read_state = hubp60_read_state,
	.hubp_clear_underflow = hubp2_clear_underflow,
	.hubp_set_flip_control_surface_gsl = hubp2_set_flip_control_surface_gsl,
	.hubp_init = hubp401_init,
	.set_unbounded_requesting = hubp401_set_unbounded_requesting,
	.hubp_soft_reset = hubp31_soft_reset,
	.hubp_set_flip_int = hubp401_set_flip_int,
	.hubp_in_blank = hubp401_in_blank,
	.phantom_hubp_post_enable = hubp32_phantom_hubp_post_enable,
	.hubp_update_mall_sel = NULL,
	.hubp_prepare_subvp_buffering = hubp32_prepare_subvp_buffering,
	.hubp_program_mcache_id_and_split_coordinate = hubp401_program_mcache_id_and_split_coordinate,
	.hubp_program_3dlut_fl_addr = hubp401_program_3dlut_fl_addr,
	.hubp_program_3dlut_fl_config = hubp42_program_3dlut_fl_config,
	.hubp_program_3dlut_fl_dlg_param = hubp401_program_3dlut_fl_dlg_param,
	.hubp_enable_3dlut_fl = hubp401_enable_3dlut_fl,
	.hubp_program_3dlut_fl_crossbar = hubp42_program_3dlut_fl_crossbar,
	.hubp_get_3dlut_fl_done = hubp401_get_3dlut_fl_done,
	.hubp_clear_tiling = hubp401_clear_tiling,
	.hubp_read_reg_state = hubp3_read_reg_state
};

bool hubp60_construct(
	struct dcn20_hubp *hubp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask)
{
	hubp2->base.funcs = &dcn60_hubp_funcs;
	hubp2->base.ctx = ctx;
	hubp2->hubp_regs = hubp_regs;
	hubp2->hubp_shift = hubp_shift;
	hubp2->hubp_mask = hubp_mask;
	hubp2->base.inst = inst;
	hubp2->base.opp_id = OPP_ID_INVALID;
	hubp2->base.mpcc_id = 0xf;

	return true;
}
