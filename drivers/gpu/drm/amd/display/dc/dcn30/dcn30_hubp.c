/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#include "dcn30_hubp.h"

#include "dm_services.h"
#include "dce_calcs.h"
#include "reg_helper.h"
#include "basics/conversion.h"
#include "dcn20/dcn20_hubp.h"
#include "dcn21/dcn21_hubp.h"

#define REG(reg)\
	hubp2->hubp_regs->reg

#define CTX \
	hubp2->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubp2->hubp_shift->field_name, hubp2->hubp_mask->field_name

void hubp3_set_vm_system_aperture_settings(struct hubp *hubp,
		struct vm_system_aperture_param *apt)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	PHYSICAL_ADDRESS_LOC mc_vm_apt_default;
	PHYSICAL_ADDRESS_LOC mc_vm_apt_low;
	PHYSICAL_ADDRESS_LOC mc_vm_apt_high;

	// The format of default addr is 48:12 of the 48 bit addr
	mc_vm_apt_default.quad_part = apt->sys_default.quad_part >> 12;

	// The format of high/low are 48:18 of the 48 bit addr
	mc_vm_apt_low.quad_part = apt->sys_low.quad_part >> 18;
	mc_vm_apt_high.quad_part = apt->sys_high.quad_part >> 18;

	REG_SET(DCN_VM_SYSTEM_APERTURE_LOW_ADDR, 0,
			MC_VM_SYSTEM_APERTURE_LOW_ADDR, mc_vm_apt_low.quad_part);

	REG_SET(DCN_VM_SYSTEM_APERTURE_HIGH_ADDR, 0,
			MC_VM_SYSTEM_APERTURE_HIGH_ADDR, mc_vm_apt_high.quad_part);

	REG_SET_2(DCN_VM_MX_L1_TLB_CNTL, 0,
			ENABLE_L1_TLB, 1,
			SYSTEM_ACCESS_MODE, 0x3);
}

bool hubp3_program_surface_flip_and_addr(
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

			REG_SET(DCSURF_SECONDARY_META_SURFACE_ADDRESS_HIGH_C, 0,
				SECONDARY_META_SURFACE_ADDRESS_HIGH_C,
				address->grph_stereo.right_alpha_meta_addr.high_part);

			REG_SET(DCSURF_SECONDARY_META_SURFACE_ADDRESS_C, 0,
				SECONDARY_META_SURFACE_ADDRESS_C,
				address->grph_stereo.right_alpha_meta_addr.low_part);

			REG_SET(DCSURF_SECONDARY_META_SURFACE_ADDRESS_HIGH, 0,
					SECONDARY_META_SURFACE_ADDRESS_HIGH,
					address->grph_stereo.right_meta_addr.high_part);

			REG_SET(DCSURF_SECONDARY_META_SURFACE_ADDRESS, 0,
					SECONDARY_META_SURFACE_ADDRESS,
					address->grph_stereo.right_meta_addr.low_part);
		}
		if (address->grph_stereo.left_meta_addr.quad_part != 0) {

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH_C, 0,
				PRIMARY_META_SURFACE_ADDRESS_HIGH_C,
				address->grph_stereo.left_alpha_meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_C, 0,
				PRIMARY_META_SURFACE_ADDRESS_C,
				address->grph_stereo.left_alpha_meta_addr.low_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH, 0,
					PRIMARY_META_SURFACE_ADDRESS_HIGH,
					address->grph_stereo.left_meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS, 0,
					PRIMARY_META_SURFACE_ADDRESS,
					address->grph_stereo.left_meta_addr.low_part);
		}

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

		REG_UPDATE_4(DCSURF_SURFACE_CONTROL,
				PRIMARY_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_SURFACE_TMZ_C, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ, address->tmz_surface,
				PRIMARY_META_SURFACE_TMZ_C, address->tmz_surface);

		if (address->rgbea.meta_addr.quad_part != 0) {

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH_C, 0,
					PRIMARY_META_SURFACE_ADDRESS_HIGH_C,
					address->rgbea.alpha_meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_C, 0,
					PRIMARY_META_SURFACE_ADDRESS_C,
					address->rgbea.alpha_meta_addr.low_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH, 0,
					PRIMARY_META_SURFACE_ADDRESS_HIGH,
					address->rgbea.meta_addr.high_part);

			REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS, 0,
					PRIMARY_META_SURFACE_ADDRESS,
					address->rgbea.meta_addr.low_part);
		}

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

static void hubp3_program_tiling(
	struct dcn20_hubp *hubp2,
	const union dc_tiling_info *info,
	const enum surface_pixel_format pixel_format)
{
	REG_UPDATE_4(DCSURF_ADDR_CONFIG,
		NUM_PIPES, log_2(info->gfx9.num_pipes),
		PIPE_INTERLEAVE, info->gfx9.pipe_interleave,
		MAX_COMPRESSED_FRAGS, log_2(info->gfx9.max_compressed_frags),
		NUM_PKRS, log_2(info->gfx9.num_pkrs));

	REG_UPDATE_3(DCSURF_TILING_CONFIG,
			SW_MODE, info->gfx9.swizzle,
			META_LINEAR, info->gfx9.meta_linear,
			PIPE_ALIGNED, info->gfx9.pipe_aligned);

}

void hubp3_dcc_control(struct hubp *hubp, bool enable,
		enum hubp_ind_block_size blk_size)
{
	uint32_t dcc_en = enable ? 1 : 0;
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	REG_UPDATE_4(DCSURF_SURFACE_CONTROL,
			PRIMARY_SURFACE_DCC_EN, dcc_en,
			PRIMARY_SURFACE_DCC_IND_BLK, blk_size,
			SECONDARY_SURFACE_DCC_EN, dcc_en,
			SECONDARY_SURFACE_DCC_IND_BLK, blk_size);
}

void hubp3_dcc_control_sienna_cichlid(struct hubp *hubp,
		struct dc_plane_dcc_param *dcc)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	/*Workaround until UMD fix the new dcc_ind_blk interface */
	if (dcc->independent_64b_blks && dcc->dcc_ind_blk == 0)
		dcc->dcc_ind_blk = 1;
	if (dcc->independent_64b_blks_c && dcc->dcc_ind_blk_c == 0)
		dcc->dcc_ind_blk_c = 1;

	REG_UPDATE_6(DCSURF_SURFACE_CONTROL,
		PRIMARY_SURFACE_DCC_EN, dcc->enable,
		PRIMARY_SURFACE_DCC_IND_BLK, dcc->dcc_ind_blk,
		PRIMARY_SURFACE_DCC_IND_BLK_C, dcc->dcc_ind_blk_c,
		SECONDARY_SURFACE_DCC_EN, dcc->enable,
		SECONDARY_SURFACE_DCC_IND_BLK, dcc->dcc_ind_blk,
		SECONDARY_SURFACE_DCC_IND_BLK_C, dcc->dcc_ind_blk_c);
}

void hubp3_dmdata_set_attributes(
		struct hubp *hubp,
		const struct dc_dmdata_attributes *attr)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	/*always HW mode */
	REG_UPDATE(DMDATA_CNTL,
			DMDATA_MODE, 1);

	/* for DMDATA flip, need to use SURFACE_UPDATE_LOCK */
	REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_UPDATE_LOCK, 1);

	/* toggle DMDATA_UPDATED and set repeat and size */
	REG_UPDATE(DMDATA_CNTL,
			DMDATA_UPDATED, 0);
	REG_UPDATE_3(DMDATA_CNTL,
			DMDATA_UPDATED, 1,
			DMDATA_REPEAT, attr->dmdata_repeat,
			DMDATA_SIZE, attr->dmdata_size);

	/* set DMDATA address */
	REG_WRITE(DMDATA_ADDRESS_LOW, attr->address.low_part);
	REG_UPDATE(DMDATA_ADDRESS_HIGH,
			DMDATA_ADDRESS_HIGH, attr->address.high_part);

	REG_UPDATE(DCSURF_FLIP_CONTROL, SURFACE_UPDATE_LOCK, 0);

}


void hubp3_program_surface_config(
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

	hubp3_dcc_control_sienna_cichlid(hubp, dcc);
	hubp3_program_tiling(hubp2, tiling_info, format);
	hubp2_program_size(hubp, format, plane_size, dcc);
	hubp2_program_rotation(hubp, rotation, horizontal_mirror);
	hubp2_program_pixel_format(hubp, format);
}

static void hubp3_program_deadline(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);

	hubp2_program_deadline(hubp, dlg_attr, ttu_attr);
	REG_UPDATE(DCN_DMDATA_VM_CNTL,
			REFCYC_PER_VM_DMDATA, dlg_attr->refcyc_per_vm_dmdata);
}

void hubp3_read_state(struct hubp *hubp)
{
	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	struct dcn_hubp_state *s = &hubp2->state;
	struct _vcs_dpi_display_rq_regs_st *rq_regs = &s->rq_regs;

	hubp2_read_state_common(hubp);

	REG_GET_7(DCHUBP_REQ_SIZE_CONFIG,
		CHUNK_SIZE, &rq_regs->rq_regs_l.chunk_size,
		MIN_CHUNK_SIZE, &rq_regs->rq_regs_l.min_chunk_size,
		META_CHUNK_SIZE, &rq_regs->rq_regs_l.meta_chunk_size,
		MIN_META_CHUNK_SIZE, &rq_regs->rq_regs_l.min_meta_chunk_size,
		DPTE_GROUP_SIZE, &rq_regs->rq_regs_l.dpte_group_size,
		SWATH_HEIGHT, &rq_regs->rq_regs_l.swath_height,
		PTE_ROW_HEIGHT_LINEAR, &rq_regs->rq_regs_l.pte_row_height_linear);

	REG_GET_7(DCHUBP_REQ_SIZE_CONFIG_C,
		CHUNK_SIZE_C, &rq_regs->rq_regs_c.chunk_size,
		MIN_CHUNK_SIZE_C, &rq_regs->rq_regs_c.min_chunk_size,
		META_CHUNK_SIZE_C, &rq_regs->rq_regs_c.meta_chunk_size,
		MIN_META_CHUNK_SIZE_C, &rq_regs->rq_regs_c.min_meta_chunk_size,
		DPTE_GROUP_SIZE_C, &rq_regs->rq_regs_c.dpte_group_size,
		SWATH_HEIGHT_C, &rq_regs->rq_regs_c.swath_height,
		PTE_ROW_HEIGHT_LINEAR_C, &rq_regs->rq_regs_c.pte_row_height_linear);

}

void hubp3_setup(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr,
		struct _vcs_dpi_display_rq_regs_st *rq_regs,
		struct _vcs_dpi_display_pipe_dest_params_st *pipe_dest)
{
	/* otg is locked when this func is called. Register are double buffered.
	 * disable the requestors is not needed
	 */
	hubp2_vready_at_or_After_vsync(hubp, pipe_dest);
	hubp21_program_requestor(hubp, rq_regs);
	hubp3_program_deadline(hubp, dlg_attr, ttu_attr);
}

void hubp3_init(struct hubp *hubp)
{
	// DEDCN21-133: Inconsistent row starting line for flip between DPTE and Meta
	// This is a chicken bit to enable the ECO fix.

	struct dcn20_hubp *hubp2 = TO_DCN20_HUBP(hubp);
	//hubp[i].HUBPREQ_DEBUG.HUBPREQ_DEBUG[26] = 1;
	REG_WRITE(HUBPREQ_DEBUG, 1 << 26);
}

static struct hubp_funcs dcn30_hubp_funcs = {
	.hubp_enable_tripleBuffer = hubp2_enable_triplebuffer,
	.hubp_is_triplebuffer_enabled = hubp2_is_triplebuffer_enabled,
	.hubp_program_surface_flip_and_addr = hubp3_program_surface_flip_and_addr,
	.hubp_program_surface_config = hubp3_program_surface_config,
	.hubp_is_flip_pending = hubp2_is_flip_pending,
	.hubp_setup = hubp3_setup,
	.hubp_setup_interdependent = hubp2_setup_interdependent,
	.hubp_set_vm_system_aperture_settings = hubp3_set_vm_system_aperture_settings,
	.set_blank = hubp2_set_blank,
	.dcc_control = hubp3_dcc_control,
	.mem_program_viewport = min_set_viewport,
	.set_cursor_attributes	= hubp2_cursor_set_attributes,
	.set_cursor_position	= hubp2_cursor_set_position,
	.hubp_clk_cntl = hubp2_clk_cntl,
	.hubp_vtg_sel = hubp2_vtg_sel,
	.dmdata_set_attributes = hubp3_dmdata_set_attributes,
	.dmdata_load = hubp2_dmdata_load,
	.dmdata_status_done = hubp2_dmdata_status_done,
	.hubp_read_state = hubp3_read_state,
	.hubp_clear_underflow = hubp2_clear_underflow,
	.hubp_set_flip_control_surface_gsl = hubp2_set_flip_control_surface_gsl,
	.hubp_init = hubp3_init,
	.hubp_in_blank = hubp1_in_blank,
	.hubp_soft_reset = hubp1_soft_reset,
	.hubp_set_flip_int = hubp1_set_flip_int,
};

bool hubp3_construct(
	struct dcn20_hubp *hubp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask)
{
	hubp2->base.funcs = &dcn30_hubp_funcs;
	hubp2->base.ctx = ctx;
	hubp2->hubp_regs = hubp_regs;
	hubp2->hubp_shift = hubp_shift;
	hubp2->hubp_mask = hubp_mask;
	hubp2->base.inst = inst;
	hubp2->base.opp_id = OPP_ID_INVALID;
	hubp2->base.mpcc_id = 0xf;

	return true;
}
