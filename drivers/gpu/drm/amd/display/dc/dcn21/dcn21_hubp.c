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

#include "dc_dmub_srv.h"

#define DC_LOGGER_INIT(logger)

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
	uint32_t refcyc_per_vm_group_vblank;
	uint32_t refcyc_per_vm_req_vblank;
	uint32_t refcyc_per_vm_group_flip;
	uint32_t refcyc_per_vm_req_flip;
	const uint32_t uninitialized_hw_default = 0;

	REG_GET(VBLANK_PARAMETERS_5,
			REFCYC_PER_VM_GROUP_VBLANK, &refcyc_per_vm_group_vblank);

	if (refcyc_per_vm_group_vblank == uninitialized_hw_default ||
			refcyc_per_vm_group_vblank > dlg_attr->refcyc_per_vm_group_vblank)
		REG_SET(VBLANK_PARAMETERS_5, 0,
				REFCYC_PER_VM_GROUP_VBLANK, dlg_attr->refcyc_per_vm_group_vblank);

	REG_GET(VBLANK_PARAMETERS_6,
			REFCYC_PER_VM_REQ_VBLANK, &refcyc_per_vm_req_vblank);

	if (refcyc_per_vm_req_vblank == uninitialized_hw_default ||
			refcyc_per_vm_req_vblank > dlg_attr->refcyc_per_vm_req_vblank)
		REG_SET(VBLANK_PARAMETERS_6, 0,
				REFCYC_PER_VM_REQ_VBLANK, dlg_attr->refcyc_per_vm_req_vblank);

	REG_GET(FLIP_PARAMETERS_3,
			REFCYC_PER_VM_GROUP_FLIP, &refcyc_per_vm_group_flip);

	if (refcyc_per_vm_group_flip == uninitialized_hw_default ||
			refcyc_per_vm_group_flip > dlg_attr->refcyc_per_vm_group_flip)
		REG_SET(FLIP_PARAMETERS_3, 0,
				REFCYC_PER_VM_GROUP_FLIP, dlg_attr->refcyc_per_vm_group_flip);

	REG_GET(FLIP_PARAMETERS_4,
			REFCYC_PER_VM_REQ_FLIP, &refcyc_per_vm_req_flip);

	if (refcyc_per_vm_req_flip == uninitialized_hw_default ||
			refcyc_per_vm_req_flip > dlg_attr->refcyc_per_vm_req_flip)
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

void hubp21_set_vm_system_aperture_settings(struct hubp *hubp,
		struct vm_system_aperture_param *apt)
{
	struct dcn21_hubp *hubp21 = TO_DCN21_HUBP(hubp);

	PHYSICAL_ADDRESS_LOC mc_vm_apt_low;
	PHYSICAL_ADDRESS_LOC mc_vm_apt_high;

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

void hubp21_validate_dml_output(struct hubp *hubp,
		struct dc_context *ctx,
		struct _vcs_dpi_display_rq_regs_st *dml_rq_regs,
		struct _vcs_dpi_display_dlg_regs_st *dml_dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *dml_ttu_attr)
{
	struct dcn21_hubp *hubp21 = TO_DCN21_HUBP(hubp);
	struct _vcs_dpi_display_rq_regs_st rq_regs = {0};
	struct _vcs_dpi_display_dlg_regs_st dlg_attr = {0};
	struct _vcs_dpi_display_ttu_regs_st ttu_attr = {0};
	DC_LOGGER_INIT(ctx->logger);
	DC_LOG_DEBUG("DML Validation | Running Validation");

	/* Requester - Per hubp */
	REG_GET(HUBPRET_CONTROL,
		DET_BUF_PLANE1_BASE_ADDRESS, &rq_regs.plane1_base_address);
	REG_GET_4(DCN_EXPANSION_MODE,
		DRQ_EXPANSION_MODE, &rq_regs.drq_expansion_mode,
		PRQ_EXPANSION_MODE, &rq_regs.prq_expansion_mode,
		MRQ_EXPANSION_MODE, &rq_regs.mrq_expansion_mode,
		CRQ_EXPANSION_MODE, &rq_regs.crq_expansion_mode);
	REG_GET_8(DCHUBP_REQ_SIZE_CONFIG,
		CHUNK_SIZE, &rq_regs.rq_regs_l.chunk_size,
		MIN_CHUNK_SIZE, &rq_regs.rq_regs_l.min_chunk_size,
		META_CHUNK_SIZE, &rq_regs.rq_regs_l.meta_chunk_size,
		MIN_META_CHUNK_SIZE, &rq_regs.rq_regs_l.min_meta_chunk_size,
		DPTE_GROUP_SIZE, &rq_regs.rq_regs_l.dpte_group_size,
		VM_GROUP_SIZE, &rq_regs.rq_regs_l.mpte_group_size,
		SWATH_HEIGHT, &rq_regs.rq_regs_l.swath_height,
		PTE_ROW_HEIGHT_LINEAR, &rq_regs.rq_regs_l.pte_row_height_linear);
	REG_GET_7(DCHUBP_REQ_SIZE_CONFIG_C,
		CHUNK_SIZE_C, &rq_regs.rq_regs_c.chunk_size,
		MIN_CHUNK_SIZE_C, &rq_regs.rq_regs_c.min_chunk_size,
		META_CHUNK_SIZE_C, &rq_regs.rq_regs_c.meta_chunk_size,
		MIN_META_CHUNK_SIZE_C, &rq_regs.rq_regs_c.min_meta_chunk_size,
		DPTE_GROUP_SIZE_C, &rq_regs.rq_regs_c.dpte_group_size,
		SWATH_HEIGHT_C, &rq_regs.rq_regs_c.swath_height,
		PTE_ROW_HEIGHT_LINEAR_C, &rq_regs.rq_regs_c.pte_row_height_linear);

	if (rq_regs.plane1_base_address != dml_rq_regs->plane1_base_address)
		DC_LOG_DEBUG("DML Validation | HUBPRET_CONTROL:DET_BUF_PLANE1_BASE_ADDRESS - Expected: %u  Actual: %u\n",
				dml_rq_regs->plane1_base_address, rq_regs.plane1_base_address);
	if (rq_regs.drq_expansion_mode != dml_rq_regs->drq_expansion_mode)
		DC_LOG_DEBUG("DML Validation | DCN_EXPANSION_MODE:DRQ_EXPANSION_MODE - Expected: %u  Actual: %u\n",
				dml_rq_regs->drq_expansion_mode, rq_regs.drq_expansion_mode);
	if (rq_regs.prq_expansion_mode != dml_rq_regs->prq_expansion_mode)
		DC_LOG_DEBUG("DML Validation | DCN_EXPANSION_MODE:MRQ_EXPANSION_MODE - Expected: %u  Actual: %u\n",
				dml_rq_regs->prq_expansion_mode, rq_regs.prq_expansion_mode);
	if (rq_regs.mrq_expansion_mode != dml_rq_regs->mrq_expansion_mode)
		DC_LOG_DEBUG("DML Validation | DCN_EXPANSION_MODE:DET_BUF_PLANE1_BASE_ADDRESS - Expected: %u  Actual: %u\n",
				dml_rq_regs->mrq_expansion_mode, rq_regs.mrq_expansion_mode);
	if (rq_regs.crq_expansion_mode != dml_rq_regs->crq_expansion_mode)
		DC_LOG_DEBUG("DML Validation | DCN_EXPANSION_MODE:CRQ_EXPANSION_MODE - Expected: %u  Actual: %u\n",
				dml_rq_regs->crq_expansion_mode, rq_regs.crq_expansion_mode);

	if (rq_regs.rq_regs_l.chunk_size != dml_rq_regs->rq_regs_l.chunk_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG_C:CHUNK_SIZE - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_l.chunk_size, rq_regs.rq_regs_l.chunk_size);
	if (rq_regs.rq_regs_l.min_chunk_size != dml_rq_regs->rq_regs_l.min_chunk_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG_C:MIN_CHUNK_SIZE - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_l.min_chunk_size, rq_regs.rq_regs_l.min_chunk_size);
	if (rq_regs.rq_regs_l.meta_chunk_size != dml_rq_regs->rq_regs_l.meta_chunk_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG_C:META_CHUNK_SIZE - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_l.meta_chunk_size, rq_regs.rq_regs_l.meta_chunk_size);
	if (rq_regs.rq_regs_l.min_meta_chunk_size != dml_rq_regs->rq_regs_l.min_meta_chunk_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG_C:MIN_META_CHUNK_SIZE - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_l.min_meta_chunk_size, rq_regs.rq_regs_l.min_meta_chunk_size);
	if (rq_regs.rq_regs_l.dpte_group_size != dml_rq_regs->rq_regs_l.dpte_group_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG_C:DPTE_GROUP_SIZE - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_l.dpte_group_size, rq_regs.rq_regs_l.dpte_group_size);
	if (rq_regs.rq_regs_l.mpte_group_size != dml_rq_regs->rq_regs_l.mpte_group_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG_C:VM_GROUP_SIZE - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_l.mpte_group_size, rq_regs.rq_regs_l.mpte_group_size);
	if (rq_regs.rq_regs_l.swath_height != dml_rq_regs->rq_regs_l.swath_height)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG_C:SWATH_HEIGHT - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_l.swath_height, rq_regs.rq_regs_l.swath_height);
	if (rq_regs.rq_regs_l.pte_row_height_linear != dml_rq_regs->rq_regs_l.pte_row_height_linear)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG_C:PTE_ROW_HEIGHT_LINEAR - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_l.pte_row_height_linear, rq_regs.rq_regs_l.pte_row_height_linear);

	if (rq_regs.rq_regs_c.chunk_size != dml_rq_regs->rq_regs_c.chunk_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG:CHUNK_SIZE_C - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_c.chunk_size, rq_regs.rq_regs_c.chunk_size);
	if (rq_regs.rq_regs_c.min_chunk_size != dml_rq_regs->rq_regs_c.min_chunk_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG:MIN_CHUNK_SIZE_C - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_c.min_chunk_size, rq_regs.rq_regs_c.min_chunk_size);
	if (rq_regs.rq_regs_c.meta_chunk_size != dml_rq_regs->rq_regs_c.meta_chunk_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG:META_CHUNK_SIZE_C - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_c.meta_chunk_size, rq_regs.rq_regs_c.meta_chunk_size);
	if (rq_regs.rq_regs_c.min_meta_chunk_size != dml_rq_regs->rq_regs_c.min_meta_chunk_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG:MIN_META_CHUNK_SIZE_C - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_c.min_meta_chunk_size, rq_regs.rq_regs_c.min_meta_chunk_size);
	if (rq_regs.rq_regs_c.dpte_group_size != dml_rq_regs->rq_regs_c.dpte_group_size)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG:DPTE_GROUP_SIZE_C - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_c.dpte_group_size, rq_regs.rq_regs_c.dpte_group_size);
	if (rq_regs.rq_regs_c.swath_height != dml_rq_regs->rq_regs_c.swath_height)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG:SWATH_HEIGHT_C - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_c.swath_height, rq_regs.rq_regs_c.swath_height);
	if (rq_regs.rq_regs_c.pte_row_height_linear != dml_rq_regs->rq_regs_c.pte_row_height_linear)
		DC_LOG_DEBUG("DML Validation | DCHUBP_REQ_SIZE_CONFIG:PTE_ROW_HEIGHT_LINEAR_C - Expected: %u  Actual: %u\n",
				dml_rq_regs->rq_regs_c.pte_row_height_linear, rq_regs.rq_regs_c.pte_row_height_linear);


	/* DLG - Per hubp */
	REG_GET_2(BLANK_OFFSET_0,
		REFCYC_H_BLANK_END, &dlg_attr.refcyc_h_blank_end,
		DLG_V_BLANK_END, &dlg_attr.dlg_vblank_end);
	REG_GET(BLANK_OFFSET_1,
		MIN_DST_Y_NEXT_START, &dlg_attr.min_dst_y_next_start);
	REG_GET(DST_DIMENSIONS,
		REFCYC_PER_HTOTAL, &dlg_attr.refcyc_per_htotal);
	REG_GET_2(DST_AFTER_SCALER,
		REFCYC_X_AFTER_SCALER, &dlg_attr.refcyc_x_after_scaler,
		DST_Y_AFTER_SCALER, &dlg_attr.dst_y_after_scaler);
	REG_GET(REF_FREQ_TO_PIX_FREQ,
		REF_FREQ_TO_PIX_FREQ, &dlg_attr.ref_freq_to_pix_freq);

	if (dlg_attr.refcyc_h_blank_end != dml_dlg_attr->refcyc_h_blank_end)
		DC_LOG_DEBUG("DML Validation | BLANK_OFFSET_0:REFCYC_H_BLANK_END - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_h_blank_end, dlg_attr.refcyc_h_blank_end);
	if (dlg_attr.dlg_vblank_end != dml_dlg_attr->dlg_vblank_end)
		DC_LOG_DEBUG("DML Validation | BLANK_OFFSET_0:DLG_V_BLANK_END - Expected: %u  Actual: %u\n",
				dml_dlg_attr->dlg_vblank_end, dlg_attr.dlg_vblank_end);
	if (dlg_attr.min_dst_y_next_start != dml_dlg_attr->min_dst_y_next_start)
		DC_LOG_DEBUG("DML Validation | BLANK_OFFSET_1:MIN_DST_Y_NEXT_START - Expected: %u  Actual: %u\n",
				dml_dlg_attr->min_dst_y_next_start, dlg_attr.min_dst_y_next_start);
	if (dlg_attr.refcyc_per_htotal != dml_dlg_attr->refcyc_per_htotal)
		DC_LOG_DEBUG("DML Validation | DST_DIMENSIONS:REFCYC_PER_HTOTAL - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_htotal, dlg_attr.refcyc_per_htotal);
	if (dlg_attr.refcyc_x_after_scaler != dml_dlg_attr->refcyc_x_after_scaler)
		DC_LOG_DEBUG("DML Validation | DST_AFTER_SCALER:REFCYC_X_AFTER_SCALER - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_x_after_scaler, dlg_attr.refcyc_x_after_scaler);
	if (dlg_attr.dst_y_after_scaler != dml_dlg_attr->dst_y_after_scaler)
		DC_LOG_DEBUG("DML Validation | DST_AFTER_SCALER:DST_Y_AFTER_SCALER - Expected: %u  Actual: %u\n",
				dml_dlg_attr->dst_y_after_scaler, dlg_attr.dst_y_after_scaler);
	if (dlg_attr.ref_freq_to_pix_freq != dml_dlg_attr->ref_freq_to_pix_freq)
		DC_LOG_DEBUG("DML Validation | REF_FREQ_TO_PIX_FREQ:REF_FREQ_TO_PIX_FREQ - Expected: %u  Actual: %u\n",
				dml_dlg_attr->ref_freq_to_pix_freq, dlg_attr.ref_freq_to_pix_freq);

	/* DLG - Per luma/chroma */
	REG_GET(VBLANK_PARAMETERS_1,
		REFCYC_PER_PTE_GROUP_VBLANK_L, &dlg_attr.refcyc_per_pte_group_vblank_l);
	if (REG(NOM_PARAMETERS_0))
		REG_GET(NOM_PARAMETERS_0,
			DST_Y_PER_PTE_ROW_NOM_L, &dlg_attr.dst_y_per_pte_row_nom_l);
	if (REG(NOM_PARAMETERS_1))
		REG_GET(NOM_PARAMETERS_1,
			REFCYC_PER_PTE_GROUP_NOM_L, &dlg_attr.refcyc_per_pte_group_nom_l);
	REG_GET(NOM_PARAMETERS_4,
		DST_Y_PER_META_ROW_NOM_L, &dlg_attr.dst_y_per_meta_row_nom_l);
	REG_GET(NOM_PARAMETERS_5,
		REFCYC_PER_META_CHUNK_NOM_L, &dlg_attr.refcyc_per_meta_chunk_nom_l);
	REG_GET_2(PER_LINE_DELIVERY,
		REFCYC_PER_LINE_DELIVERY_L, &dlg_attr.refcyc_per_line_delivery_l,
		REFCYC_PER_LINE_DELIVERY_C, &dlg_attr.refcyc_per_line_delivery_c);
	REG_GET_2(PER_LINE_DELIVERY_PRE,
		REFCYC_PER_LINE_DELIVERY_PRE_L, &dlg_attr.refcyc_per_line_delivery_pre_l,
		REFCYC_PER_LINE_DELIVERY_PRE_C, &dlg_attr.refcyc_per_line_delivery_pre_c);
	REG_GET(VBLANK_PARAMETERS_2,
		REFCYC_PER_PTE_GROUP_VBLANK_C, &dlg_attr.refcyc_per_pte_group_vblank_c);
	if (REG(NOM_PARAMETERS_2))
		REG_GET(NOM_PARAMETERS_2,
			DST_Y_PER_PTE_ROW_NOM_C, &dlg_attr.dst_y_per_pte_row_nom_c);
	if (REG(NOM_PARAMETERS_3))
		REG_GET(NOM_PARAMETERS_3,
			REFCYC_PER_PTE_GROUP_NOM_C, &dlg_attr.refcyc_per_pte_group_nom_c);
	REG_GET(NOM_PARAMETERS_6,
		DST_Y_PER_META_ROW_NOM_C, &dlg_attr.dst_y_per_meta_row_nom_c);
	REG_GET(NOM_PARAMETERS_7,
		REFCYC_PER_META_CHUNK_NOM_C, &dlg_attr.refcyc_per_meta_chunk_nom_c);
	REG_GET(VBLANK_PARAMETERS_3,
			REFCYC_PER_META_CHUNK_VBLANK_L, &dlg_attr.refcyc_per_meta_chunk_vblank_l);
	REG_GET(VBLANK_PARAMETERS_4,
			REFCYC_PER_META_CHUNK_VBLANK_C, &dlg_attr.refcyc_per_meta_chunk_vblank_c);

	if (dlg_attr.refcyc_per_pte_group_vblank_l != dml_dlg_attr->refcyc_per_pte_group_vblank_l)
		DC_LOG_DEBUG("DML Validation | VBLANK_PARAMETERS_1:REFCYC_PER_PTE_GROUP_VBLANK_L - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_pte_group_vblank_l, dlg_attr.refcyc_per_pte_group_vblank_l);
	if (dlg_attr.dst_y_per_pte_row_nom_l != dml_dlg_attr->dst_y_per_pte_row_nom_l)
		DC_LOG_DEBUG("DML Validation | NOM_PARAMETERS_0:DST_Y_PER_PTE_ROW_NOM_L - Expected: %u  Actual: %u\n",
				dml_dlg_attr->dst_y_per_pte_row_nom_l, dlg_attr.dst_y_per_pte_row_nom_l);
	if (dlg_attr.refcyc_per_pte_group_nom_l != dml_dlg_attr->refcyc_per_pte_group_nom_l)
		DC_LOG_DEBUG("DML Validation | NOM_PARAMETERS_1:REFCYC_PER_PTE_GROUP_NOM_L - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_pte_group_nom_l, dlg_attr.refcyc_per_pte_group_nom_l);
	if (dlg_attr.dst_y_per_meta_row_nom_l != dml_dlg_attr->dst_y_per_meta_row_nom_l)
		DC_LOG_DEBUG("DML Validation | NOM_PARAMETERS_4:DST_Y_PER_META_ROW_NOM_L - Expected: %u  Actual: %u\n",
				dml_dlg_attr->dst_y_per_meta_row_nom_l, dlg_attr.dst_y_per_meta_row_nom_l);
	if (dlg_attr.refcyc_per_meta_chunk_nom_l != dml_dlg_attr->refcyc_per_meta_chunk_nom_l)
		DC_LOG_DEBUG("DML Validation | NOM_PARAMETERS_5:REFCYC_PER_META_CHUNK_NOM_L - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_meta_chunk_nom_l, dlg_attr.refcyc_per_meta_chunk_nom_l);
	if (dlg_attr.refcyc_per_line_delivery_l != dml_dlg_attr->refcyc_per_line_delivery_l)
		DC_LOG_DEBUG("DML Validation | PER_LINE_DELIVERY:REFCYC_PER_LINE_DELIVERY_L - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_line_delivery_l, dlg_attr.refcyc_per_line_delivery_l);
	if (dlg_attr.refcyc_per_line_delivery_c != dml_dlg_attr->refcyc_per_line_delivery_c)
		DC_LOG_DEBUG("DML Validation | PER_LINE_DELIVERY:REFCYC_PER_LINE_DELIVERY_C - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_line_delivery_c, dlg_attr.refcyc_per_line_delivery_c);
	if (dlg_attr.refcyc_per_pte_group_vblank_c != dml_dlg_attr->refcyc_per_pte_group_vblank_c)
		DC_LOG_DEBUG("DML Validation | VBLANK_PARAMETERS_2:REFCYC_PER_PTE_GROUP_VBLANK_C - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_pte_group_vblank_c, dlg_attr.refcyc_per_pte_group_vblank_c);
	if (dlg_attr.dst_y_per_pte_row_nom_c != dml_dlg_attr->dst_y_per_pte_row_nom_c)
		DC_LOG_DEBUG("DML Validation | NOM_PARAMETERS_2:DST_Y_PER_PTE_ROW_NOM_C - Expected: %u  Actual: %u\n",
				dml_dlg_attr->dst_y_per_pte_row_nom_c, dlg_attr.dst_y_per_pte_row_nom_c);
	if (dlg_attr.refcyc_per_pte_group_nom_c != dml_dlg_attr->refcyc_per_pte_group_nom_c)
		DC_LOG_DEBUG("DML Validation | NOM_PARAMETERS_3:REFCYC_PER_PTE_GROUP_NOM_C - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_pte_group_nom_c, dlg_attr.refcyc_per_pte_group_nom_c);
	if (dlg_attr.dst_y_per_meta_row_nom_c != dml_dlg_attr->dst_y_per_meta_row_nom_c)
		DC_LOG_DEBUG("DML Validation | NOM_PARAMETERS_6:DST_Y_PER_META_ROW_NOM_C - Expected: %u  Actual: %u\n",
				dml_dlg_attr->dst_y_per_meta_row_nom_c, dlg_attr.dst_y_per_meta_row_nom_c);
	if (dlg_attr.refcyc_per_meta_chunk_nom_c != dml_dlg_attr->refcyc_per_meta_chunk_nom_c)
		DC_LOG_DEBUG("DML Validation | NOM_PARAMETERS_7:REFCYC_PER_META_CHUNK_NOM_C - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_meta_chunk_nom_c, dlg_attr.refcyc_per_meta_chunk_nom_c);
	if (dlg_attr.refcyc_per_line_delivery_pre_l != dml_dlg_attr->refcyc_per_line_delivery_pre_l)
		DC_LOG_DEBUG("DML Validation | PER_LINE_DELIVERY_PRE:REFCYC_PER_LINE_DELIVERY_PRE_L - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_line_delivery_pre_l, dlg_attr.refcyc_per_line_delivery_pre_l);
	if (dlg_attr.refcyc_per_line_delivery_pre_c != dml_dlg_attr->refcyc_per_line_delivery_pre_c)
		DC_LOG_DEBUG("DML Validation | PER_LINE_DELIVERY_PRE:REFCYC_PER_LINE_DELIVERY_PRE_C - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_line_delivery_pre_c, dlg_attr.refcyc_per_line_delivery_pre_c);
	if (dlg_attr.refcyc_per_meta_chunk_vblank_l != dml_dlg_attr->refcyc_per_meta_chunk_vblank_l)
		DC_LOG_DEBUG("DML Validation | VBLANK_PARAMETERS_3:REFCYC_PER_META_CHUNK_VBLANK_L - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_meta_chunk_vblank_l, dlg_attr.refcyc_per_meta_chunk_vblank_l);
	if (dlg_attr.refcyc_per_meta_chunk_vblank_c != dml_dlg_attr->refcyc_per_meta_chunk_vblank_c)
		DC_LOG_DEBUG("DML Validation | VBLANK_PARAMETERS_4:REFCYC_PER_META_CHUNK_VBLANK_C - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_meta_chunk_vblank_c, dlg_attr.refcyc_per_meta_chunk_vblank_c);

	/* TTU - per hubp */
	REG_GET_2(DCN_TTU_QOS_WM,
		QoS_LEVEL_LOW_WM, &ttu_attr.qos_level_low_wm,
		QoS_LEVEL_HIGH_WM, &ttu_attr.qos_level_high_wm);

	if (ttu_attr.qos_level_low_wm != dml_ttu_attr->qos_level_low_wm)
		DC_LOG_DEBUG("DML Validation | DCN_TTU_QOS_WM:QoS_LEVEL_LOW_WM - Expected: %u  Actual: %u\n",
				dml_ttu_attr->qos_level_low_wm, ttu_attr.qos_level_low_wm);
	if (ttu_attr.qos_level_high_wm != dml_ttu_attr->qos_level_high_wm)
		DC_LOG_DEBUG("DML Validation | DCN_TTU_QOS_WM:QoS_LEVEL_HIGH_WM - Expected: %u  Actual: %u\n",
				dml_ttu_attr->qos_level_high_wm, ttu_attr.qos_level_high_wm);

	/* TTU - per luma/chroma */
	/* Assumed surf0 is luma and 1 is chroma */
	REG_GET_3(DCN_SURF0_TTU_CNTL0,
		REFCYC_PER_REQ_DELIVERY, &ttu_attr.refcyc_per_req_delivery_l,
		QoS_LEVEL_FIXED, &ttu_attr.qos_level_fixed_l,
		QoS_RAMP_DISABLE, &ttu_attr.qos_ramp_disable_l);
	REG_GET_3(DCN_SURF1_TTU_CNTL0,
		REFCYC_PER_REQ_DELIVERY, &ttu_attr.refcyc_per_req_delivery_c,
		QoS_LEVEL_FIXED, &ttu_attr.qos_level_fixed_c,
		QoS_RAMP_DISABLE, &ttu_attr.qos_ramp_disable_c);
	REG_GET_3(DCN_CUR0_TTU_CNTL0,
		REFCYC_PER_REQ_DELIVERY, &ttu_attr.refcyc_per_req_delivery_cur0,
		QoS_LEVEL_FIXED, &ttu_attr.qos_level_fixed_cur0,
		QoS_RAMP_DISABLE, &ttu_attr.qos_ramp_disable_cur0);
	REG_GET(FLIP_PARAMETERS_1,
		REFCYC_PER_PTE_GROUP_FLIP_L, &dlg_attr.refcyc_per_pte_group_flip_l);
	REG_GET(DCN_CUR0_TTU_CNTL1,
			REFCYC_PER_REQ_DELIVERY_PRE, &ttu_attr.refcyc_per_req_delivery_pre_cur0);
	REG_GET(DCN_CUR1_TTU_CNTL1,
			REFCYC_PER_REQ_DELIVERY_PRE, &ttu_attr.refcyc_per_req_delivery_pre_cur1);
	REG_GET(DCN_SURF0_TTU_CNTL1,
			REFCYC_PER_REQ_DELIVERY_PRE, &ttu_attr.refcyc_per_req_delivery_pre_l);
	REG_GET(DCN_SURF1_TTU_CNTL1,
			REFCYC_PER_REQ_DELIVERY_PRE, &ttu_attr.refcyc_per_req_delivery_pre_c);

	if (ttu_attr.refcyc_per_req_delivery_l != dml_ttu_attr->refcyc_per_req_delivery_l)
		DC_LOG_DEBUG("DML Validation | DCN_SURF0_TTU_CNTL0:REFCYC_PER_REQ_DELIVERY - Expected: %u  Actual: %u\n",
				dml_ttu_attr->refcyc_per_req_delivery_l, ttu_attr.refcyc_per_req_delivery_l);
	if (ttu_attr.qos_level_fixed_l != dml_ttu_attr->qos_level_fixed_l)
		DC_LOG_DEBUG("DML Validation | DCN_SURF0_TTU_CNTL0:QoS_LEVEL_FIXED - Expected: %u  Actual: %u\n",
				dml_ttu_attr->qos_level_fixed_l, ttu_attr.qos_level_fixed_l);
	if (ttu_attr.qos_ramp_disable_l != dml_ttu_attr->qos_ramp_disable_l)
		DC_LOG_DEBUG("DML Validation | DCN_SURF0_TTU_CNTL0:QoS_RAMP_DISABLE - Expected: %u  Actual: %u\n",
				dml_ttu_attr->qos_ramp_disable_l, ttu_attr.qos_ramp_disable_l);
	if (ttu_attr.refcyc_per_req_delivery_c != dml_ttu_attr->refcyc_per_req_delivery_c)
		DC_LOG_DEBUG("DML Validation | DCN_SURF1_TTU_CNTL0:REFCYC_PER_REQ_DELIVERY - Expected: %u  Actual: %u\n",
				dml_ttu_attr->refcyc_per_req_delivery_c, ttu_attr.refcyc_per_req_delivery_c);
	if (ttu_attr.qos_level_fixed_c != dml_ttu_attr->qos_level_fixed_c)
		DC_LOG_DEBUG("DML Validation | DCN_SURF1_TTU_CNTL0:QoS_LEVEL_FIXED - Expected: %u  Actual: %u\n",
				dml_ttu_attr->qos_level_fixed_c, ttu_attr.qos_level_fixed_c);
	if (ttu_attr.qos_ramp_disable_c != dml_ttu_attr->qos_ramp_disable_c)
		DC_LOG_DEBUG("DML Validation | DCN_SURF1_TTU_CNTL0:QoS_RAMP_DISABLE - Expected: %u  Actual: %u\n",
				dml_ttu_attr->qos_ramp_disable_c, ttu_attr.qos_ramp_disable_c);
	if (ttu_attr.refcyc_per_req_delivery_cur0 != dml_ttu_attr->refcyc_per_req_delivery_cur0)
		DC_LOG_DEBUG("DML Validation | DCN_CUR0_TTU_CNTL0:REFCYC_PER_REQ_DELIVERY - Expected: %u  Actual: %u\n",
				dml_ttu_attr->refcyc_per_req_delivery_cur0, ttu_attr.refcyc_per_req_delivery_cur0);
	if (ttu_attr.qos_level_fixed_cur0 != dml_ttu_attr->qos_level_fixed_cur0)
		DC_LOG_DEBUG("DML Validation | DCN_CUR0_TTU_CNTL0:QoS_LEVEL_FIXED - Expected: %u  Actual: %u\n",
				dml_ttu_attr->qos_level_fixed_cur0, ttu_attr.qos_level_fixed_cur0);
	if (ttu_attr.qos_ramp_disable_cur0 != dml_ttu_attr->qos_ramp_disable_cur0)
		DC_LOG_DEBUG("DML Validation | DCN_CUR0_TTU_CNTL0:QoS_RAMP_DISABLE - Expected: %u  Actual: %u\n",
				dml_ttu_attr->qos_ramp_disable_cur0, ttu_attr.qos_ramp_disable_cur0);
	if (dlg_attr.refcyc_per_pte_group_flip_l != dml_dlg_attr->refcyc_per_pte_group_flip_l)
		DC_LOG_DEBUG("DML Validation | FLIP_PARAMETERS_1:REFCYC_PER_PTE_GROUP_FLIP_L - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_pte_group_flip_l, dlg_attr.refcyc_per_pte_group_flip_l);
	if (ttu_attr.refcyc_per_req_delivery_pre_cur0 != dml_ttu_attr->refcyc_per_req_delivery_pre_cur0)
		DC_LOG_DEBUG("DML Validation | DCN_CUR0_TTU_CNTL1:REFCYC_PER_REQ_DELIVERY_PRE - Expected: %u  Actual: %u\n",
				dml_ttu_attr->refcyc_per_req_delivery_pre_cur0, ttu_attr.refcyc_per_req_delivery_pre_cur0);
	if (ttu_attr.refcyc_per_req_delivery_pre_cur1 != dml_ttu_attr->refcyc_per_req_delivery_pre_cur1)
		DC_LOG_DEBUG("DML Validation | DCN_CUR1_TTU_CNTL1:REFCYC_PER_REQ_DELIVERY_PRE - Expected: %u  Actual: %u\n",
				dml_ttu_attr->refcyc_per_req_delivery_pre_cur1, ttu_attr.refcyc_per_req_delivery_pre_cur1);
	if (ttu_attr.refcyc_per_req_delivery_pre_l != dml_ttu_attr->refcyc_per_req_delivery_pre_l)
		DC_LOG_DEBUG("DML Validation | DCN_SURF0_TTU_CNTL1:REFCYC_PER_REQ_DELIVERY_PRE - Expected: %u  Actual: %u\n",
				dml_ttu_attr->refcyc_per_req_delivery_pre_l, ttu_attr.refcyc_per_req_delivery_pre_l);
	if (ttu_attr.refcyc_per_req_delivery_pre_c != dml_ttu_attr->refcyc_per_req_delivery_pre_c)
		DC_LOG_DEBUG("DML Validation | DCN_SURF1_TTU_CNTL1:REFCYC_PER_REQ_DELIVERY_PRE - Expected: %u  Actual: %u\n",
				dml_ttu_attr->refcyc_per_req_delivery_pre_c, ttu_attr.refcyc_per_req_delivery_pre_c);

	/* Host VM deadline regs */
	REG_GET(VBLANK_PARAMETERS_5,
		REFCYC_PER_VM_GROUP_VBLANK, &dlg_attr.refcyc_per_vm_group_vblank);
	REG_GET(VBLANK_PARAMETERS_6,
		REFCYC_PER_VM_REQ_VBLANK, &dlg_attr.refcyc_per_vm_req_vblank);
	REG_GET(FLIP_PARAMETERS_3,
		REFCYC_PER_VM_GROUP_FLIP, &dlg_attr.refcyc_per_vm_group_flip);
	REG_GET(FLIP_PARAMETERS_4,
		REFCYC_PER_VM_REQ_FLIP, &dlg_attr.refcyc_per_vm_req_flip);
	REG_GET(FLIP_PARAMETERS_5,
		REFCYC_PER_PTE_GROUP_FLIP_C, &dlg_attr.refcyc_per_pte_group_flip_c);
	REG_GET(FLIP_PARAMETERS_6,
		REFCYC_PER_META_CHUNK_FLIP_C, &dlg_attr.refcyc_per_meta_chunk_flip_c);
	REG_GET(FLIP_PARAMETERS_2,
		REFCYC_PER_META_CHUNK_FLIP_L, &dlg_attr.refcyc_per_meta_chunk_flip_l);

	if (dlg_attr.refcyc_per_vm_group_vblank != dml_dlg_attr->refcyc_per_vm_group_vblank)
		DC_LOG_DEBUG("DML Validation | VBLANK_PARAMETERS_5:REFCYC_PER_VM_GROUP_VBLANK - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_vm_group_vblank, dlg_attr.refcyc_per_vm_group_vblank);
	if (dlg_attr.refcyc_per_vm_req_vblank != dml_dlg_attr->refcyc_per_vm_req_vblank)
		DC_LOG_DEBUG("DML Validation | VBLANK_PARAMETERS_6:REFCYC_PER_VM_REQ_VBLANK - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_vm_req_vblank, dlg_attr.refcyc_per_vm_req_vblank);
	if (dlg_attr.refcyc_per_vm_group_flip != dml_dlg_attr->refcyc_per_vm_group_flip)
		DC_LOG_DEBUG("DML Validation | FLIP_PARAMETERS_3:REFCYC_PER_VM_GROUP_FLIP - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_vm_group_flip, dlg_attr.refcyc_per_vm_group_flip);
	if (dlg_attr.refcyc_per_vm_req_flip != dml_dlg_attr->refcyc_per_vm_req_flip)
		DC_LOG_DEBUG("DML Validation | FLIP_PARAMETERS_4:REFCYC_PER_VM_REQ_FLIP - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_vm_req_flip, dlg_attr.refcyc_per_vm_req_flip);
	if (dlg_attr.refcyc_per_pte_group_flip_c != dml_dlg_attr->refcyc_per_pte_group_flip_c)
		DC_LOG_DEBUG("DML Validation | FLIP_PARAMETERS_5:REFCYC_PER_PTE_GROUP_FLIP_C - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_pte_group_flip_c, dlg_attr.refcyc_per_pte_group_flip_c);
	if (dlg_attr.refcyc_per_meta_chunk_flip_c != dml_dlg_attr->refcyc_per_meta_chunk_flip_c)
		DC_LOG_DEBUG("DML Validation | FLIP_PARAMETERS_6:REFCYC_PER_META_CHUNK_FLIP_C - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_meta_chunk_flip_c, dlg_attr.refcyc_per_meta_chunk_flip_c);
	if (dlg_attr.refcyc_per_meta_chunk_flip_l != dml_dlg_attr->refcyc_per_meta_chunk_flip_l)
		DC_LOG_DEBUG("DML Validation | FLIP_PARAMETERS_2:REFCYC_PER_META_CHUNK_FLIP_L - Expected: %u  Actual: %u\n",
				dml_dlg_attr->refcyc_per_meta_chunk_flip_l, dlg_attr.refcyc_per_meta_chunk_flip_l);
}

static void program_surface_flip_and_addr(struct hubp *hubp, struct surface_flip_registers *flip_regs)
{
	struct dcn21_hubp *hubp21 = TO_DCN21_HUBP(hubp);

	REG_UPDATE_3(DCSURF_FLIP_CONTROL,
					SURFACE_FLIP_TYPE, flip_regs->immediate,
					SURFACE_FLIP_MODE_FOR_STEREOSYNC, flip_regs->grph_stereo,
					SURFACE_FLIP_IN_STEREOSYNC, flip_regs->grph_stereo);

	REG_UPDATE(VMID_SETTINGS_0,
				VMID, flip_regs->vmid);

	REG_UPDATE_8(DCSURF_SURFACE_CONTROL,
			PRIMARY_SURFACE_TMZ, flip_regs->tmz_surface,
			PRIMARY_SURFACE_TMZ_C, flip_regs->tmz_surface,
			PRIMARY_META_SURFACE_TMZ, flip_regs->tmz_surface,
			PRIMARY_META_SURFACE_TMZ_C, flip_regs->tmz_surface,
			SECONDARY_SURFACE_TMZ, flip_regs->tmz_surface,
			SECONDARY_SURFACE_TMZ_C, flip_regs->tmz_surface,
			SECONDARY_META_SURFACE_TMZ, flip_regs->tmz_surface,
			SECONDARY_META_SURFACE_TMZ_C, flip_regs->tmz_surface);

	REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH_C, 0,
			PRIMARY_META_SURFACE_ADDRESS_HIGH_C,
			flip_regs->DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH_C);

	REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_C, 0,
			PRIMARY_META_SURFACE_ADDRESS_C,
			flip_regs->DCSURF_PRIMARY_META_SURFACE_ADDRESS_C);

	REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH, 0,
			PRIMARY_META_SURFACE_ADDRESS_HIGH,
			flip_regs->DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH);

	REG_SET(DCSURF_PRIMARY_META_SURFACE_ADDRESS, 0,
			PRIMARY_META_SURFACE_ADDRESS,
			flip_regs->DCSURF_PRIMARY_META_SURFACE_ADDRESS);

	REG_SET(DCSURF_SECONDARY_META_SURFACE_ADDRESS_HIGH, 0,
			SECONDARY_META_SURFACE_ADDRESS_HIGH,
			flip_regs->DCSURF_SECONDARY_META_SURFACE_ADDRESS_HIGH);

	REG_SET(DCSURF_SECONDARY_META_SURFACE_ADDRESS, 0,
			SECONDARY_META_SURFACE_ADDRESS,
			flip_regs->DCSURF_SECONDARY_META_SURFACE_ADDRESS);


	REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH, 0,
			SECONDARY_SURFACE_ADDRESS_HIGH,
			flip_regs->DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH);

	REG_SET(DCSURF_SECONDARY_SURFACE_ADDRESS, 0,
			SECONDARY_SURFACE_ADDRESS,
			flip_regs->DCSURF_SECONDARY_SURFACE_ADDRESS);


	REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C, 0,
			PRIMARY_SURFACE_ADDRESS_HIGH_C,
			flip_regs->DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C);

	REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_C, 0,
			PRIMARY_SURFACE_ADDRESS_C,
			flip_regs->DCSURF_PRIMARY_SURFACE_ADDRESS_C);

	REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH, 0,
			PRIMARY_SURFACE_ADDRESS_HIGH,
			flip_regs->DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH);

	REG_SET(DCSURF_PRIMARY_SURFACE_ADDRESS, 0,
			PRIMARY_SURFACE_ADDRESS,
			flip_regs->DCSURF_PRIMARY_SURFACE_ADDRESS);
}

void dmcub_PLAT_54186_wa(struct hubp *hubp, struct surface_flip_registers *flip_regs)
{
	struct dc_dmub_srv *dmcub = hubp->ctx->dmub_srv;
	struct dcn21_hubp *hubp21 = TO_DCN21_HUBP(hubp);
	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.PLAT_54186_wa.header.type = DMUB_CMD__PLAT_54186_WA;
	cmd.PLAT_54186_wa.header.payload_bytes = sizeof(cmd.PLAT_54186_wa.flip);
	cmd.PLAT_54186_wa.flip.DCSURF_PRIMARY_SURFACE_ADDRESS =
		flip_regs->DCSURF_PRIMARY_SURFACE_ADDRESS;
	cmd.PLAT_54186_wa.flip.DCSURF_PRIMARY_SURFACE_ADDRESS_C =
		flip_regs->DCSURF_PRIMARY_SURFACE_ADDRESS_C;
	cmd.PLAT_54186_wa.flip.DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH =
		flip_regs->DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH;
	cmd.PLAT_54186_wa.flip.DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C =
		flip_regs->DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C;
	cmd.PLAT_54186_wa.flip.flip_params.grph_stereo = flip_regs->grph_stereo;
	cmd.PLAT_54186_wa.flip.flip_params.hubp_inst = hubp->inst;
	cmd.PLAT_54186_wa.flip.flip_params.immediate = flip_regs->immediate;
	cmd.PLAT_54186_wa.flip.flip_params.tmz_surface = flip_regs->tmz_surface;
	cmd.PLAT_54186_wa.flip.flip_params.vmid = flip_regs->vmid;

	PERF_TRACE();  // TODO: remove after performance is stable.
	dc_dmub_srv_cmd_queue(dmcub, &cmd);
	PERF_TRACE();  // TODO: remove after performance is stable.
	dc_dmub_srv_cmd_execute(dmcub);
	PERF_TRACE();  // TODO: remove after performance is stable.
	dc_dmub_srv_wait_idle(dmcub);
	PERF_TRACE();  // TODO: remove after performance is stable.
}

bool hubp21_program_surface_flip_and_addr(
		struct hubp *hubp,
		const struct dc_plane_address *address,
		bool flip_immediate)
{
	struct surface_flip_registers flip_regs = { 0 };

	flip_regs.vmid = address->vmid;

	switch (address->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		if (address->grph.addr.quad_part == 0) {
			BREAK_TO_DEBUGGER();
			break;
		}

		if (address->grph.meta_addr.quad_part != 0) {
			flip_regs.DCSURF_PRIMARY_META_SURFACE_ADDRESS =
					address->grph.meta_addr.low_part;
			flip_regs.DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH =
					address->grph.meta_addr.high_part;
		}

		flip_regs.DCSURF_PRIMARY_SURFACE_ADDRESS =
				address->grph.addr.low_part;
		flip_regs.DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH =
				address->grph.addr.high_part;
		break;
	case PLN_ADDR_TYPE_VIDEO_PROGRESSIVE:
		if (address->video_progressive.luma_addr.quad_part == 0
				|| address->video_progressive.chroma_addr.quad_part == 0)
			break;

		if (address->video_progressive.luma_meta_addr.quad_part != 0) {
			flip_regs.DCSURF_PRIMARY_META_SURFACE_ADDRESS =
					address->video_progressive.luma_meta_addr.low_part;
			flip_regs.DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH =
					address->video_progressive.luma_meta_addr.high_part;

			flip_regs.DCSURF_PRIMARY_META_SURFACE_ADDRESS_C =
					address->video_progressive.chroma_meta_addr.low_part;
			flip_regs.DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH_C =
					address->video_progressive.chroma_meta_addr.high_part;
		}

		flip_regs.DCSURF_PRIMARY_SURFACE_ADDRESS =
				address->video_progressive.luma_addr.low_part;
		flip_regs.DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH =
				address->video_progressive.luma_addr.high_part;

		flip_regs.DCSURF_PRIMARY_SURFACE_ADDRESS_C =
				address->video_progressive.chroma_addr.low_part;

		flip_regs.DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C =
				address->video_progressive.chroma_addr.high_part;

		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
		if (address->grph_stereo.left_addr.quad_part == 0)
			break;
		if (address->grph_stereo.right_addr.quad_part == 0)
			break;

		flip_regs.grph_stereo = true;

		if (address->grph_stereo.right_meta_addr.quad_part != 0) {
			flip_regs.DCSURF_SECONDARY_META_SURFACE_ADDRESS =
					address->grph_stereo.right_meta_addr.low_part;
			flip_regs.DCSURF_SECONDARY_META_SURFACE_ADDRESS_HIGH =
					address->grph_stereo.right_meta_addr.high_part;
		}

		if (address->grph_stereo.left_meta_addr.quad_part != 0) {
			flip_regs.DCSURF_PRIMARY_META_SURFACE_ADDRESS =
					address->grph_stereo.left_meta_addr.low_part;
			flip_regs.DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH =
					address->grph_stereo.left_meta_addr.high_part;
		}

		flip_regs.DCSURF_PRIMARY_SURFACE_ADDRESS =
				address->grph_stereo.left_addr.low_part;
		flip_regs.DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH =
				address->grph_stereo.left_addr.high_part;

		flip_regs.DCSURF_SECONDARY_SURFACE_ADDRESS =
				address->grph_stereo.right_addr.low_part;
		flip_regs.DCSURF_SECONDARY_SURFACE_ADDRESS_HIGH =
				address->grph_stereo.right_addr.high_part;

		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	flip_regs.tmz_surface = address->tmz_surface;
	flip_regs.immediate = flip_immediate;

	if (hubp->ctx->dc->debug.enable_dmcub_surface_flip && address->type == PLN_ADDR_TYPE_VIDEO_PROGRESSIVE)
		dmcub_PLAT_54186_wa(hubp, &flip_regs);
	else
		program_surface_flip_and_addr(hubp, &flip_regs);

	hubp->request_address = *address;

	return true;
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
	.hubp_program_surface_flip_and_addr = hubp21_program_surface_flip_and_addr,
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
	.validate_dml_output = hubp21_validate_dml_output,
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
