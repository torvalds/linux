/*
* Copyright 2018 Advanced Micro Devices, Inc.
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

#include "dcn10/dcn10_hubp.h"
#include "dcn21_hubp.h"

#include "dm_services.h"
#include "reg_helper.h"

#define REG(reg)\
	hubp21->hubp_regs->reg

#define CTX \
	hubp21->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	hubp21->hubp_shift->field_name, hubp21->hubp_mask->field_name

/*
 * In DCN2.1, the non-double buffered version of the following 4 DLG registers are used in RTL.
 * As a result, if S/W updates any of these registers during a mode change,
 * the current frame before the mode change will use the new value right away
 * and can lead to generating incorrect request deadlines and incorrect TTU/QoS behavior.
 *
 * REFCYC_PER_VM_GROUP_FLIP[22:0]
 * REFCYC_PER_VM_GROUP_VBLANK[22:0]
 * REFCYC_PER_VM_REQ_FLIP[22:0]
 * REFCYC_PER_VM_REQ_VBLANK[22:0]
 *
 * REFCYC_PER_VM_*_FLIP affects the deadline of the VM requests generated
 * when flipping to a new surface
 *
 * REFCYC_PER_VM_*_VBLANK affects the deadline of the VM requests generated
 * during prefetch  period of a frame. The prefetch starts at a pre-determined
 * number of lines before the display active per frame
 *
 * DCN may underflow due to incorrectly programming these registers
 * during VM stage of prefetch/iflip. First lines of display active
 * or a sub-region of active using a new surface will be corrupted
 * until the VM data returns at flip/mode change transitions
 *
 * Work around:
 * workaround is always opt to use the more aggressive settings.
 * On any mode switch, if the new reg values are smaller than the current values,
 * then update the regs with the new values.
 *
 * Link to the ticket: http://ontrack-internal.amd.com/browse/DEDCN21-142
 *
 */
void apply_DEDCN21_142_wa_for_hostvm_deadline(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr)
{
	struct dcn21_hubp *hubp21 = TO_DCN21_HUBP(hubp);
	uint32_t cur_value;

	REG_GET(VBLANK_PARAMETERS_5, REFCYC_PER_VM_GROUP_VBLANK, &cur_value);
	if (cur_value > dlg_attr->refcyc_per_vm_group_vblank)
		REG_SET(VBLANK_PARAMETERS_5, 0,
				REFCYC_PER_VM_GROUP_VBLANK, dlg_attr->refcyc_per_vm_group_vblank);

	REG_GET(VBLANK_PARAMETERS_6,
			REFCYC_PER_VM_REQ_VBLANK,
			&cur_value);
	if (cur_value > dlg_attr->refcyc_per_vm_req_vblank)
		REG_SET(VBLANK_PARAMETERS_6, 0,
				REFCYC_PER_VM_REQ_VBLANK, dlg_attr->refcyc_per_vm_req_vblank);

	REG_GET(FLIP_PARAMETERS_3, REFCYC_PER_VM_GROUP_FLIP, &cur_value);
	if (cur_value > dlg_attr->refcyc_per_vm_group_flip)
		REG_SET(FLIP_PARAMETERS_3, 0,
				REFCYC_PER_VM_GROUP_FLIP, dlg_attr->refcyc_per_vm_group_flip);

	REG_GET(FLIP_PARAMETERS_4, REFCYC_PER_VM_REQ_FLIP, &cur_value);
	if (cur_value > dlg_attr->refcyc_per_vm_req_flip)
		REG_SET(FLIP_PARAMETERS_4, 0,
					REFCYC_PER_VM_REQ_FLIP, dlg_attr->refcyc_per_vm_req_flip);

	REG_SET(FLIP_PARAMETERS_5, 0,
			REFCYC_PER_PTE_GROUP_FLIP_C, dlg_attr->refcyc_per_pte_group_flip_c);
	REG_SET(FLIP_PARAMETERS_6, 0,
			REFCYC_PER_META_CHUNK_FLIP_C, dlg_attr->refcyc_per_meta_chunk_flip_c);
}

void hubp21_program_deadline(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr)
{
	hubp2_program_deadline(hubp, dlg_attr, ttu_attr);

	apply_DEDCN21_142_wa_for_hostvm_deadline(hubp, dlg_attr);
}

void hubp21_program_requestor(
		struct hubp *hubp,
		struct _vcs_dpi_display_rq_regs_st *rq_regs)
{
	struct dcn21_hubp *hubp21 = TO_DCN21_HUBP(hubp);

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
		VM_GROUP_SIZE, rq_regs->rq_regs_l.mpte_group_size,
		SWATH_HEIGHT, rq_regs->rq_regs_l.swath_height,
		PTE_ROW_HEIGHT_LINEAR, rq_regs->rq_regs_l.pte_row_height_linear);
	REG_SET_7(DCHUBP_REQ_SIZE_CONFIG_C, 0,
		CHUNK_SIZE_C, rq_regs->rq_regs_c.chunk_size,
		MIN_CHUNK_SIZE_C, rq_regs->rq_regs_c.min_chunk_size,
		META_CHUNK_SIZE_C, rq_regs->rq_regs_c.meta_chunk_size,
		MIN_META_CHUNK_SIZE_C, rq_regs->rq_regs_c.min_meta_chunk_size,
		DPTE_GROUP_SIZE_C, rq_regs->rq_regs_c.dpte_group_size,
		SWATH_HEIGHT_C, rq_regs->rq_regs_c.swath_height,
		PTE_ROW_HEIGHT_LINEAR_C, rq_regs->rq_regs_c.pte_row_height_linear);
}

static void hubp21_setup(
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
	hubp21_program_deadline(hubp, dlg_attr, ttu_attr);

}

void hubp21_set_viewport(
	struct hubp *hubp,
	const struct rect *viewport,
	const struct rect *viewport_c)
{
	struct dcn21_hubp *hubp21 = TO_DCN21_HUBP(hubp);
	int patched_viewport_height = 0;
	struct dc_debug_options *debug = &hubp->ctx->dc->debug;

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

	/*
	 *	Work around for underflow issue with NV12 + rIOMMU translation
	 *	+ immediate flip. This will cause hubp underflow, but will not
	 *	be user visible since underflow is in blank region
	 */
	patched_viewport_height = viewport_c->height;
	if (viewport_c->height != 0 && debug->nv12_iflip_vm_wa) {
		int pte_row_height = 0;
		int pte_rows = 0;

		REG_GET(DCHUBP_REQ_SIZE_CONFIG,
			PTE_ROW_HEIGHT_LINEAR, &pte_row_height);

		pte_row_height = 1 << (pte_row_height + 3);
		pte_rows = (viewport_c->height + pte_row_height - 1) / pte_row_height;
		patched_viewport_height = pte_rows * pte_row_height + 3;
	}


	/* DC supports NV12 only at the moment */
	REG_SET_2(DCSURF_PRI_VIEWPORT_DIMENSION_C, 0,
		  PRI_VIEWPORT_WIDTH_C, viewport_c->width,
		  PRI_VIEWPORT_HEIGHT_C, patched_viewport_height);

	REG_SET_2(DCSURF_PRI_VIEWPORT_START_C, 0,
		  PRI_VIEWPORT_X_START_C, viewport_c->x,
		  PRI_VIEWPORT_Y_START_C, viewport_c->y);

	REG_SET_2(DCSURF_SEC_VIEWPORT_DIMENSION_C, 0,
		  SEC_VIEWPORT_WIDTH_C, viewport_c->width,
		  SEC_VIEWPORT_HEIGHT_C, patched_viewport_height);

	REG_SET_2(DCSURF_SEC_VIEWPORT_START_C, 0,
		  SEC_VIEWPORT_X_START_C, viewport_c->x,
		  SEC_VIEWPORT_Y_START_C, viewport_c->y);
}

void hubp21_set_vm_system_aperture_settings(struct hubp *hubp,
		struct vm_system_aperture_param *apt)
{
	struct dcn21_hubp *hubp21 = TO_DCN21_HUBP(hubp);

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

void hubp21_init(struct hubp *hubp)
{
	// DEDCN21-133: Inconsistent row starting line for flip between DPTE and Meta
	// This is a chicken bit to enable the ECO fix.

	struct dcn21_hubp *hubp21 = TO_DCN21_HUBP(hubp);
	//hubp[i].HUBPREQ_DEBUG.HUBPREQ_DEBUG[26] = 1;
	REG_WRITE(HUBPREQ_DEBUG, 1 << 26);
}
static struct hubp_funcs dcn21_hubp_funcs = {
	.hubp_enable_tripleBuffer = hubp2_enable_triplebuffer,
	.hubp_is_triplebuffer_enabled = hubp2_is_triplebuffer_enabled,
	.hubp_program_surface_flip_and_addr = hubp2_program_surface_flip_and_addr,
	.hubp_program_surface_config = hubp1_program_surface_config,
	.hubp_is_flip_pending = hubp1_is_flip_pending,
	.hubp_setup = hubp21_setup,
	.hubp_setup_interdependent = hubp2_setup_interdependent,
	.hubp_set_vm_system_aperture_settings = hubp21_set_vm_system_aperture_settings,
	.set_blank = hubp1_set_blank,
	.dcc_control = hubp1_dcc_control,
	.mem_program_viewport = hubp21_set_viewport,
	.set_cursor_attributes	= hubp2_cursor_set_attributes,
	.set_cursor_position	= hubp1_cursor_set_position,
	.hubp_clk_cntl = hubp1_clk_cntl,
	.hubp_vtg_sel = hubp1_vtg_sel,
	.dmdata_set_attributes = hubp2_dmdata_set_attributes,
	.dmdata_load = hubp2_dmdata_load,
	.dmdata_status_done = hubp2_dmdata_status_done,
	.hubp_read_state = hubp1_read_state,
	.hubp_clear_underflow = hubp1_clear_underflow,
	.hubp_set_flip_control_surface_gsl = hubp2_set_flip_control_surface_gsl,
	.hubp_init = hubp21_init,
};

bool hubp21_construct(
	struct dcn21_hubp *hubp21,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask)
{
	hubp21->base.funcs = &dcn21_hubp_funcs;
	hubp21->base.ctx = ctx;
	hubp21->hubp_regs = hubp_regs;
	hubp21->hubp_shift = hubp_shift;
	hubp21->hubp_mask = hubp_mask;
	hubp21->base.inst = inst;
	hubp21->base.opp_id = OPP_ID_INVALID;
	hubp21->base.mpcc_id = 0xf;

	return true;
}
