/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "dcn401_hubp.h"
#include "dal_asic_id.h"

#define REG(reg)\
	hubp2->hubp_regs->reg

#define CTX \
	hubp2->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubp2->hubp_shift->field_name, hubp2->hubp_mask->field_name

static void hubp401_program_3dlut_fl_addr(struct hubp *hubp,
	const struct dc_plane_address address)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(HUBP_3DLUT_ADDRESS_HIGH, HUBP_3DLUT_ADDRESS_HIGH, address.lut3d.addr.high_part);
	REG_WRITE(HUBP_3DLUT_ADDRESS_LOW, address.lut3d.addr.low_part);
}

static void hubp401_program_3dlut_fl_dlg_param(struct hubp *hubp, int refcyc_per_3dlut_group)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(HUBP_3DLUT_DLG_PARAM, REFCYC_PER_3DLUT_GROUP, refcyc_per_3dlut_group);
}

static void hubp401_enable_3dlut_fl(struct hubp *hubp, bool enable)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(HUBP_3DLUT_CONTROL, HUBP_3DLUT_ENABLE, enable ? 1 : 0);
}

int hubp401_get_3dlut_fl_done(struct hubp *hubp)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	int ret;

	REG_GET(HUBP_3DLUT_CONTROL, HUBP_3DLUT_DONE, &ret);
	return ret;
}

static void hubp401_program_3dlut_fl_addressing_mode(struct hubp *hubp, enum hubp_3dlut_fl_addressing_mode addr_mode)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(HUBP_3DLUT_CONTROL, HUBP_3DLUT_ADDRESSING_MODE, addr_mode);
}

static void hubp401_program_3dlut_fl_width(struct hubp *hubp, enum hubp_3dlut_fl_width width)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(HUBP_3DLUT_CONTROL, HUBP_3DLUT_WIDTH, width);
}

static void hubp401_program_3dlut_fl_tmz_protected(struct hubp *hubp, bool protection_enabled)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(HUBP_3DLUT_CONTROL, HUBP_3DLUT_TMZ, protection_enabled ? 1 : 0);
}

static void hubp401_program_3dlut_fl_crossbar(struct hubp *hubp,
			enum hubp_3dlut_fl_crossbar_bit_slice bit_slice_y_g,
			enum hubp_3dlut_fl_crossbar_bit_slice bit_slice_cb_b,
			enum hubp_3dlut_fl_crossbar_bit_slice bit_slice_cr_r)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE_3(HUBP_3DLUT_CONTROL,
			HUBP_3DLUT_CROSSBAR_SELECT_Y_G, bit_slice_y_g,
			HUBP_3DLUT_CROSSBAR_SELECT_CB_B, bit_slice_cb_b,
			HUBP_3DLUT_CROSSBAR_SELECT_CR_R, bit_slice_cr_r);
}

static void hubp401_update_3dlut_fl_bias_scale(struct hubp *hubp, uint16_t bias, uint16_t scale)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE_2(_3DLUT_FL_BIAS_SCALE, HUBP0_3DLUT_FL_BIAS, bias, HUBP0_3DLUT_FL_SCALE, scale);
}

static void hubp401_program_3dlut_fl_mode(struct hubp *hubp, enum hubp_3dlut_fl_mode mode)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(_3DLUT_FL_CONFIG, HUBP0_3DLUT_FL_MODE, mode);
}

static void hubp401_program_3dlut_fl_format(struct hubp *hubp, enum hubp_3dlut_fl_format format)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(_3DLUT_FL_CONFIG, HUBP0_3DLUT_FL_FORMAT, format);
}

void hubp401_update_mall_sel(struct hubp *hubp, uint32_t mall_sel, bool c_cursor)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	// Also cache cursor in MALL if using MALL for SS
	REG_UPDATE_2(DCHUBP_MALL_CONFIG, USE_MALL_SEL, mall_sel,
			USE_MALL_FOR_CURSOR, c_cursor);

	REG_UPDATE_2(DCHUBP_MALL_CONFIG, MALL_PREF_CMD_TYPE, 1, MALL_PREF_MODE, 0);
}


void hubp401_init(struct hubp *hubp)
{
	//For now nothing to do, HUBPREQ_DEBUG_DB register is removed on DCN4x.
}

void hubp401_vready_at_or_After_vsync(struct hubp *hubp,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest)
{
	uint32_t value = 0;
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	/*
	 * if (VSTARTUP_START - (VREADY_OFFSET+VUPDATE_WIDTH+VUPDATE_OFFSET)/htotal) <= OTG_V_BLANK_END
	 *	Set HUBP_VREADY_AT_OR_AFTER_VSYNC = 1
	 * else
	 *	Set HUBP_VREADY_AT_OR_AFTER_VSYNC = 0
	 */
	if (pipe_dest->htotal != 0) {
		if ((pipe_dest->vstartup_start - (pipe_dest->vready_offset+pipe_dest->vupdate_width
			+ pipe_dest->vupdate_offset) / pipe_dest->htotal) <= pipe_dest->vblank_end) {
			value = 1;
		} else
			value = 0;
	}

	REG_UPDATE(DCHUBP_CNTL, HUBP_VREADY_AT_OR_AFTER_VSYNC, value);
}

void hubp401_program_requestor(
		struct hubp *hubp,
		struct _vcs_dpi_display_rq_regs_st *rq_regs)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(HUBPRET_CONTROL,
			DET_BUF_PLANE1_BASE_ADDRESS, rq_regs->plane1_base_address);
	REG_SET_4(DCN_EXPANSION_MODE, 0,
			DRQ_EXPANSION_MODE, rq_regs->drq_expansion_mode,
			PRQ_EXPANSION_MODE, rq_regs->prq_expansion_mode,
			MRQ_EXPANSION_MODE, rq_regs->mrq_expansion_mode,
			CRQ_EXPANSION_MODE, rq_regs->crq_expansion_mode);
	REG_SET_6(DCHUBP_REQ_SIZE_CONFIG, 0,
		CHUNK_SIZE, rq_regs->rq_regs_l.chunk_size,
		MIN_CHUNK_SIZE, rq_regs->rq_regs_l.min_chunk_size,
		DPTE_GROUP_SIZE, rq_regs->rq_regs_l.dpte_group_size,
		VM_GROUP_SIZE, rq_regs->rq_regs_l.mpte_group_size,
		SWATH_HEIGHT, rq_regs->rq_regs_l.swath_height,
		PTE_ROW_HEIGHT_LINEAR, rq_regs->rq_regs_l.pte_row_height_linear);
	REG_SET_5(DCHUBP_REQ_SIZE_CONFIG_C, 0,
		CHUNK_SIZE_C, rq_regs->rq_regs_c.chunk_size,
		MIN_CHUNK_SIZE_C, rq_regs->rq_regs_c.min_chunk_size,
		DPTE_GROUP_SIZE_C, rq_regs->rq_regs_c.dpte_group_size,
		SWATH_HEIGHT_C, rq_regs->rq_regs_c.swath_height,
		PTE_ROW_HEIGHT_LINEAR_C, rq_regs->rq_regs_c.pte_row_height_linear);
}

void hubp401_program_deadline(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	/* put DLG in mission mode */
	REG_WRITE(HUBPREQ_DEBUG_DB, 1 << 8);

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

	REG_SET(FLIP_PARAMETERS_1, 0,
		REFCYC_PER_PTE_GROUP_FLIP_L, dlg_attr->refcyc_per_pte_group_flip_l);
	REG_SET(HUBP_3DLUT_DLG_PARAM, 0, REFCYC_PER_3DLUT_GROUP, dlg_attr->refcyc_per_tdlut_group);

	REG_UPDATE(DCN_DMDATA_VM_CNTL,
			REFCYC_PER_VM_DMDATA, dlg_attr->refcyc_per_vm_dmdata);
}

void hubp401_setup(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr,
		struct _vcs_dpi_display_rq_regs_st *rq_regs,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest)
{
	/* otg is locked when this func is called. Register are double buffered.
	 * disable the requestors is not needed
	 */
	hubp401_vready_at_or_After_vsync(hubp, pipe_dest);
	hubp401_program_requestor(hubp, rq_regs);
	hubp401_program_deadline(hubp, dlg_attr, ttu_attr);
}

void hubp401_setup_interdependent(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_SET_2(PREFETCH_SETTINGS, 0,
			DST_Y_PREFETCH, dlg_attr->dst_y_prefetch,
			VRATIO_PREFETCH, dlg_attr->vratio_prefetch);

	REG_SET(PREFETCH_SETTINGS_C, 0,
			VRATIO_PREFETCH_C, dlg_attr->vratio_prefetch_c);

	REG_SET_2(VBLANK_PARAMETERS_0, 0,
		DST_Y_PER_VM_VBLANK, dlg_attr->dst_y_per_vm_vblank,
		DST_Y_PER_ROW_VBLANK, dlg_attr->dst_y_per_row_vblank);

	REG_SET_2(FLIP_PARAMETERS_0, 0,
		DST_Y_PER_VM_FLIP, dlg_attr->dst_y_per_vm_flip,
		DST_Y_PER_ROW_FLIP, dlg_attr->dst_y_per_row_flip);

	REG_SET(VBLANK_PARAMETERS_3, 0,
		REFCYC_PER_META_CHUNK_VBLANK_L, dlg_attr->refcyc_per_meta_chunk_vblank_l);

	REG_SET(VBLANK_PARAMETERS_4, 0,
		REFCYC_PER_META_CHUNK_VBLANK_C, dlg_attr->refcyc_per_meta_chunk_vblank_c);

	REG_SET(FLIP_PARAMETERS_2, 0,
		REFCYC_PER_META_CHUNK_FLIP_L, dlg_attr->refcyc_per_meta_chunk_flip_l);

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


bool hubp401_program_surface_flip_and_addr(
	struct hubp *hubp,
	const struct dc_plane_address *address,
	bool flip_immediate)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	//program flip type
	REG_UPDATE(DCSURF_FLIP_CONTROL,
			SURFACE_FLIP_TYPE, flip_immediate);

	// Program VMID reg
	if (flip_immediate == 0)
		REG_UPDATE(VMID_SETTINGS_0,
			VMID, address->vmid);

	if (address->type == PLN_ADDR_TYPE_GRPH_STEREO) {
		REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_FLIP_MODE_FOR_STEREOSYNC, 0);
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
		if (address->grph.addr.quad_part == 0)
			break;

		REG_UPDATE(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface);

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

		REG_UPDATE_2(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface);

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

		REG_UPDATE_4(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface,
				SECONDARY_SURFACE_TMZ, address->tmz_surface,
				SECONDARY_SURFACE_TMZ_C, address->tmz_surface);

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH_C, 0,
				SECONDARY_SURFACE_ADDRESS_HIGH_C,
				address->grph_stereo.right_alpha_addr.high_part);

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS_C, 0,
				SECONDARY_SURFACE_ADDRESS_C,
				address->grph_stereo.right_alpha_addr.low_part);

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH, 0,
				SECONDARY_SURFACE_ADDRESS_HIGH,
				address->grph_stereo.right_addr.high_part);

		REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS, 0,
				SECONDARY_SURFACE_ADDRESS,
				address->grph_stereo.right_addr.low_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH_C,
				address->grph_stereo.left_alpha_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_C, 0,
				PRIMARY_SURFACE_ADDRESS_C,
				address->grph_stereo.left_alpha_addr.low_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH,
				address->grph_stereo.left_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
				PRIMARY_SURFACE_ADDRESS,
				address->grph_stereo.left_addr.low_part);
		break;
	case PLN_ADDR_TYPE_RGBEA:
		if (address->rgbea.addr.quad_part == 0
				|| address->rgbea.alpha_addr.quad_part == 0)
			break;

		REG_UPDATE_2(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH_C,
				address->rgbea.alpha_addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_C, 0,
				PRIMARY_SURFACE_ADDRESS_C,
				address->rgbea.alpha_addr.low_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
				PRIMARY_SURFACE_ADDRESS_HIGH,
				address->rgbea.addr.high_part);

		REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
				PRIMARY_SURFACE_ADDRESS,
				address->rgbea.addr.low_part);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	hubp->request_address = *address;

	return true;
}

void hubp401_dcc_control(struct hubp *hubp,
		struct dc_plane_dcc_param *dcc)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE_2(DCSURF_SURFACE_CONTROL,
		PRIMARY_SURFACE_DCC_EN, dcc->enable,
		SECONDARY_SURFACE_DCC_EN, dcc->enable);
}

void hubp401_program_tiling(
	struct dcn20_hubp *hubp2,
	const union dc_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	/* DCSURF_ADDR_CONFIG still shows up in reg spec, but does not need to be programmed for DCN4x
	 * All 4 fields NUM_PIPES, PIPE_INTERLEAVE, MAX_COMPRESSED_FRAGS and NUM_PKRS are irrelevant.
	 *
	 * DIM_TYPE field in DCSURF_TILING for Display is always 1 (2D dimension) which is HW default.
	 */
	 REG_UPDATE(DCSURF_TILING_CONFIG, SW_MODE, info->gfx_addr3.swizzle);
}

void hubp401_program_size(
	struct hubp *hubp,
	enum surface_pixel_format format,
	const struct plane_size *plane_size,
	struct dc_plane_dcc_param *dcc)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	uint32_t pitch, pitch_c;
	bool use_pitch_c = false;

	/* Program data pitch (calculation from addrlib)
	 * 444 or 420 luma
	 */
	use_pitch_c = format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN
		&& format < SURFACE_PIXEL_FORMAT_SUBSAMPLE_END;
	use_pitch_c = use_pitch_c
		|| (format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA);
	if (use_pitch_c) {
		ASSERT(plane_size->chroma_pitch != 0);
		/* Chroma pitch zero can cause system hang! */

		pitch = plane_size->surface_pitch - 1;
		pitch_c = plane_size->chroma_pitch - 1;
	} else {
		pitch = plane_size->surface_pitch - 1;
		pitch_c = 0;
	}

	REG_UPDATE(DCSURF_SURFACE_PITCH, PITCH, pitch);

	if (use_pitch_c)
		REG_UPDATE(DCSURF_SURFACE_PITCH_C, PITCH_C, pitch_c);
}

void hubp401_program_surface_config(
	struct hubp *hubp,
	enum surface_pixel_format format,
	union dc_tiling_info *tiling_info,
	struct plane_size *plane_size,
	enum dc_rotation_angle rotation,
	struct dc_plane_dcc_param *dcc,
	bool horizontal_mirror,
	unsigned int compat_level)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	hubp401_dcc_control(hubp, dcc);
	hubp401_program_tiling(hubp2, tiling_info, format);
	hubp401_program_size(hubp, format, plane_size, dcc);
	hubp2_program_rotation(hubp, rotation, horizontal_mirror);
	hubp2_program_pixel_format(hubp, format);
}

void hubp401_set_viewport(
	struct hubp *hubp,
	const struct rect *viewport,
	const struct rect *viewport_c)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

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

	REG_SET_2(DCSURF_SEC_VIEWPORT_DIMENSION_C, 0,
		  SEC_VIEWPORT_WIDTH_C, viewport_c->width,
		  SEC_VIEWPORT_HEIGHT_C, viewport_c->height);

	REG_SET_2(DCSURF_SEC_VIEWPORT_START_C, 0,
		  SEC_VIEWPORT_X_START_C, viewport_c->x,
		  SEC_VIEWPORT_Y_START_C, viewport_c->y);
}

void hubp401_set_flip_int(struct hubp *hubp)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(DCSURF_SURFACE_FLIP_INTERRUPT,
		SURFACE_FLIP_INT_MASK, 1);

	return;
}

bool hubp401_in_blank(struct hubp *hubp)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	uint32_t in_blank;

	REG_GET(DCHUBP_CNTL, HUBP_IN_BLANK, &in_blank);
	return in_blank ? true : false;
}


void hubp401_cursor_set_position(
	struct hubp *hubp,
	const struct dc_cursor_position *pos,
	const struct dc_cursor_mi_param *param)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	int x_pos = pos->x - param->recout.x;
	int y_pos = pos->y - param->recout.y;
	int x_hotspot = pos->x_hotspot;
	int y_hotspot = pos->y_hotspot;
	int rec_x_offset = x_pos - pos->x_hotspot;
	int rec_y_offset = y_pos - pos->y_hotspot;
	int cursor_height = (int)hubp->curs_attr.height;
	int cursor_width = (int)hubp->curs_attr.width;
	uint32_t dst_x_offset;
	uint32_t cur_en = pos->enable ? 1 : 0;

	hubp->curs_pos = *pos;

	/*
	 * Guard aganst cursor_set_position() from being called with invalid
	 * attributes
	 */
	if (hubp->curs_attr.address.quad_part == 0)
		return;

	// Transform cursor width / height and hotspots for offset calculations
	if (param->rotation == ROTATION_ANGLE_90 || param->rotation == ROTATION_ANGLE_270) {
		swap(cursor_height, cursor_width);
		swap(x_hotspot, y_hotspot);

		if (param->rotation == ROTATION_ANGLE_90) {
			// hotspot = (-y, x)
			rec_x_offset = x_pos - (cursor_width - x_hotspot);
			rec_y_offset = y_pos - y_hotspot;
		} else if (param->rotation == ROTATION_ANGLE_270) {
			// hotspot = (y, -x)
			rec_x_offset = x_pos - x_hotspot;
			rec_y_offset = y_pos - (cursor_height - y_hotspot);
		}
	} else if (param->rotation == ROTATION_ANGLE_180) {
		// hotspot = (-x, -y)
		if (!param->mirror)
			rec_x_offset = x_pos - (cursor_width - x_hotspot);

		rec_y_offset = y_pos - (cursor_height - y_hotspot);
	}

	dst_x_offset = (rec_x_offset >= 0) ? rec_x_offset : 0;
	dst_x_offset *= param->ref_clk_khz;
	dst_x_offset /= param->pixel_clk_khz;

	ASSERT(param->h_scale_ratio.value);

	if (param->h_scale_ratio.value)
		dst_x_offset = dc_fixpt_floor(dc_fixpt_div(
			dc_fixpt_from_int(dst_x_offset),
			param->h_scale_ratio));

	if (rec_x_offset >= (int)param->recout.width)
		cur_en = 0;  /* not visible beyond right edge*/

	if (rec_x_offset + cursor_width <= 0)
		cur_en = 0;  /* not visible beyond left edge*/

	if (rec_y_offset >= (int)param->recout.height)
		cur_en = 0;  /* not visible beyond bottom edge*/

	if (rec_y_offset + cursor_height <= 0)
		cur_en = 0;  /* not visible beyond top edge*/

	if (cur_en && REG_READ(CURSOR_SURFACE_ADDRESS) == 0)
		hubp->funcs->set_cursor_attributes(hubp, &hubp->curs_attr);

	REG_UPDATE(CURSOR_CONTROL,
		CURSOR_ENABLE, cur_en);

	REG_SET_2(CURSOR_POSITION, 0,
		CURSOR_X_POSITION, pos->x,
		CURSOR_Y_POSITION, pos->y);

	REG_SET_2(CURSOR_HOT_SPOT, 0,
		CURSOR_HOT_SPOT_X, pos->x_hotspot,
		CURSOR_HOT_SPOT_Y, pos->y_hotspot);

	REG_SET(CURSOR_DST_OFFSET, 0,
		CURSOR_DST_X_OFFSET, dst_x_offset);

	/* Cursor Position Register Config */
	hubp->pos.cur_ctl.bits.cur_enable = cur_en;
	hubp->pos.position.bits.x_pos = pos->x;
	hubp->pos.position.bits.y_pos = pos->y;
	hubp->pos.hot_spot.bits.x_hot = pos->x_hotspot;
	hubp->pos.hot_spot.bits.y_hot = pos->y_hotspot;
	hubp->pos.dst_offset.bits.dst_x_offset = dst_x_offset;
	/* Cursor Rectangle Cache
	 * Cursor bitmaps have different hotspot values
	 * There's a possibility that the above logic returns a negative value,
	 * so we clamp them to 0
	 */
	if (rec_x_offset < 0)
		rec_x_offset = 0;
	if (rec_y_offset < 0)
		rec_y_offset = 0;
	/* Save necessary cursor info x, y position. w, h is saved in attribute func. */
	hubp->cur_rect.x = rec_x_offset + param->recout.x;
	hubp->cur_rect.y = rec_y_offset + param->recout.y;
}

void hubp401_read_state(struct hubp *hubp)
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

	REG_GET(HUBP_CLK_CNTL,
			HUBP_CLOCK_ENABLE, &s->clock_en);

	REG_GET(DCN_GLOBAL_TTU_CNTL,
			MIN_TTU_VBLANK, &s->min_ttu_vblank);

	REG_GET_2(DCN_TTU_QOS_WM,
			QoS_LEVEL_LOW_WM, &s->qos_level_low_wm,
			QoS_LEVEL_HIGH_WM, &s->qos_level_high_wm);

	REG_GET(DCSURF_PRIMARY_SURFACE_ADDRESS,
			PRIMARY_SURFACE_ADDRESS, &s->primary_surface_addr_lo);

	REG_GET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH,
			PRIMARY_SURFACE_ADDRESS, &s->primary_surface_addr_hi);

	s->uclk_pstate_force = REG_READ(UCLK_PSTATE_FORCE);

	s->hubp_cntl = REG_READ(DCHUBP_CNTL);
	s->flip_control = REG_READ(DCSURF_FLIP_CONTROL);
}

void hubp401_set_unbounded_requesting(struct hubp *hubp, bool enable)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE(DCHUBP_CNTL, HUBP_UNBOUNDED_REQ_MODE, enable);

	/* To ensure that cursor fetching starts as early as possible in
	 * the display prefetch, set CURSOR_REQ_MODE = 1 always.
	 * The setting of CURSOR_REQ_MODE = 0 is no longer supported in
	 * DCN4x as a fall back to legacy behavior of fetching cursor
	 * just before it appears on the screen.
	 */
	REG_UPDATE(CURSOR_CONTROL, CURSOR_REQ_MODE, 1);
}

static struct hubp_funcs dcn401_hubp_funcs = {
	.hubp_enable_tripleBuffer = hubp2_enable_triplebuffer,
	.hubp_is_triplebuffer_enabled = hubp2_is_triplebuffer_enabled,
	.hubp_program_surface_flip_and_addr = hubp401_program_surface_flip_and_addr,
	.hubp_program_surface_config = hubp401_program_surface_config,
	.hubp_is_flip_pending = hubp2_is_flip_pending,
	.hubp_setup = hubp401_setup,
	.hubp_setup_interdependent = hubp401_setup_interdependent,
	.hubp_set_vm_system_aperture_settings = hubp3_set_vm_system_aperture_settings,
	.set_blank = hubp2_set_blank,
	.set_blank_regs = hubp2_set_blank_regs,
	.mem_program_viewport = hubp401_set_viewport,
	.set_cursor_attributes	= hubp32_cursor_set_attributes,
	.set_cursor_position	= hubp401_cursor_set_position,
	.hubp_clk_cntl = hubp2_clk_cntl,
	.hubp_vtg_sel = hubp2_vtg_sel,
	.dmdata_set_attributes = hubp3_dmdata_set_attributes,
	.dmdata_load = hubp2_dmdata_load,
	.dmdata_status_done = hubp2_dmdata_status_done,
	.hubp_read_state = hubp401_read_state,
	.hubp_clear_underflow = hubp2_clear_underflow,
	.hubp_set_flip_control_surface_gsl = hubp2_set_flip_control_surface_gsl,
	.hubp_init = hubp401_init,
	.set_unbounded_requesting = hubp401_set_unbounded_requesting,
	.hubp_soft_reset = hubp31_soft_reset,
	.hubp_set_flip_int = hubp401_set_flip_int,
	.hubp_in_blank = hubp401_in_blank,
	.hubp_update_force_pstate_disallow = hubp32_update_force_pstate_disallow,
	.phantom_hubp_post_enable = hubp32_phantom_hubp_post_enable,
	.hubp_update_mall_sel = hubp401_update_mall_sel,
	.hubp_prepare_subvp_buffering = hubp32_prepare_subvp_buffering,
	.hubp_update_3dlut_fl_bias_scale = hubp401_update_3dlut_fl_bias_scale,
	.hubp_program_3dlut_fl_mode = hubp401_program_3dlut_fl_mode,
	.hubp_program_3dlut_fl_format = hubp401_program_3dlut_fl_format,
	.hubp_program_3dlut_fl_addr = hubp401_program_3dlut_fl_addr,
	.hubp_program_3dlut_fl_dlg_param = hubp401_program_3dlut_fl_dlg_param,
	.hubp_enable_3dlut_fl = hubp401_enable_3dlut_fl,
	.hubp_program_3dlut_fl_addressing_mode = hubp401_program_3dlut_fl_addressing_mode,
	.hubp_program_3dlut_fl_width = hubp401_program_3dlut_fl_width,
	.hubp_program_3dlut_fl_tmz_protected = hubp401_program_3dlut_fl_tmz_protected,
	.hubp_program_3dlut_fl_crossbar = hubp401_program_3dlut_fl_crossbar,
	.hubp_get_3dlut_fl_done = hubp401_get_3dlut_fl_done
};

bool hubp401_construct(
	struct dcn20_hubp *hubp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask)
{
	hubp2->base.funcs = &dcn401_hubp_funcs;
	hubp2->base.ctx = ctx;
	hubp2->hubp_regs = hubp_regs;
	hubp2->hubp_shift = hubp_shift;
	hubp2->hubp_mask = hubp_mask;
	hubp2->base.inst = inst;
	hubp2->base.opp_id = OPP_ID_INVALID;
	hubp2->base.mpcc_id = 0xf;

	return true;
}
