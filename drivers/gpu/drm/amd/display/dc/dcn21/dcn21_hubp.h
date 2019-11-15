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

#ifndef DAL_DC_DCN21_DCN21_HUBP_H_
#define DAL_DC_DCN21_DCN21_HUBP_H_

#include "../dcn20/dcn20_hubp.h"
#include "../dcn10/dcn10_hubp.h"

#define TO_DCN21_HUBP(hubp)\
	container_of(hubp, struct dcn21_hubp, base)

#define HUBP_REG_LIST_DCN21(id)\
	HUBP_REG_LIST_DCN2_COMMON(id),\
	SRI(FLIP_PARAMETERS_3, HUBPREQ, id),\
	SRI(FLIP_PARAMETERS_4, HUBPREQ, id),\
	SRI(FLIP_PARAMETERS_5, HUBPREQ, id),\
	SRI(FLIP_PARAMETERS_6, HUBPREQ, id),\
	SRI(VBLANK_PARAMETERS_5, HUBPREQ, id),\
	SRI(VBLANK_PARAMETERS_6, HUBPREQ, id)

#define HUBP_MASK_SH_LIST_DCN21_COMMON(mask_sh)\
	HUBP_MASK_SH_LIST_DCN_SHARE_COMMON(mask_sh),\
	HUBP_MASK_SH_LIST_DCN_VM(mask_sh),\
	HUBP_SF(HUBP0_DCSURF_SURFACE_CONFIG, ROTATION_ANGLE, mask_sh),\
	HUBP_SF(HUBP0_DCSURF_SURFACE_CONFIG, H_MIRROR_EN, mask_sh),\
	HUBP_SF(HUBPREQ0_PREFETCH_SETTINGS, DST_Y_PREFETCH, mask_sh),\
	HUBP_SF(HUBPREQ0_PREFETCH_SETTINGS, VRATIO_PREFETCH, mask_sh),\
	HUBP_SF(HUBPREQ0_PREFETCH_SETTINGS_C, VRATIO_PREFETCH_C, mask_sh),\
	HUBP_SF(HUBPREQ0_DCN_VM_SYSTEM_APERTURE_LOW_ADDR, MC_VM_SYSTEM_APERTURE_LOW_ADDR, mask_sh),\
	HUBP_SF(HUBPREQ0_DCN_VM_SYSTEM_APERTURE_HIGH_ADDR, MC_VM_SYSTEM_APERTURE_HIGH_ADDR, mask_sh),\
	HUBP_SF(HUBPREQ0_CURSOR_SETTINGS, CURSOR0_DST_Y_OFFSET, mask_sh), \
	HUBP_SF(HUBPREQ0_CURSOR_SETTINGS, CURSOR0_CHUNK_HDL_ADJUST, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_SURFACE_ADDRESS_HIGH, CURSOR_SURFACE_ADDRESS_HIGH, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_SURFACE_ADDRESS, CURSOR_SURFACE_ADDRESS, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_SIZE, CURSOR_WIDTH, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_SIZE, CURSOR_HEIGHT, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_CONTROL, CURSOR_MODE, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_CONTROL, CURSOR_2X_MAGNIFY, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_CONTROL, CURSOR_PITCH, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_CONTROL, CURSOR_LINES_PER_CHUNK, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_CONTROL, CURSOR_ENABLE, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_POSITION, CURSOR_X_POSITION, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_POSITION, CURSOR_Y_POSITION, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_HOT_SPOT, CURSOR_HOT_SPOT_X, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_HOT_SPOT, CURSOR_HOT_SPOT_Y, mask_sh), \
	HUBP_SF(CURSOR0_0_CURSOR_DST_OFFSET, CURSOR_DST_X_OFFSET, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_ADDRESS_HIGH, DMDATA_ADDRESS_HIGH, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_CNTL, DMDATA_MODE, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_CNTL, DMDATA_UPDATED, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_CNTL, DMDATA_REPEAT, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_CNTL, DMDATA_SIZE, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_SW_CNTL, DMDATA_SW_UPDATED, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_SW_CNTL, DMDATA_SW_REPEAT, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_SW_CNTL, DMDATA_SW_SIZE, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_QOS_CNTL, DMDATA_QOS_MODE, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_QOS_CNTL, DMDATA_QOS_LEVEL, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_QOS_CNTL, DMDATA_DL_DELTA, mask_sh), \
	HUBP_SF(CURSOR0_0_DMDATA_STATUS, DMDATA_DONE, mask_sh),\
	HUBP_SF(HUBPREQ0_FLIP_PARAMETERS_0, DST_Y_PER_VM_FLIP, mask_sh),\
	HUBP_SF(HUBPREQ0_FLIP_PARAMETERS_0, DST_Y_PER_ROW_FLIP, mask_sh),\
	HUBP_SF(HUBPREQ0_FLIP_PARAMETERS_1, REFCYC_PER_PTE_GROUP_FLIP_L, mask_sh),\
	HUBP_SF(HUBPREQ0_FLIP_PARAMETERS_2, REFCYC_PER_META_CHUNK_FLIP_L, mask_sh),\
	HUBP_SF(HUBP0_DCHUBP_CNTL, HUBP_VREADY_AT_OR_AFTER_VSYNC, mask_sh),\
	HUBP_SF(HUBP0_DCHUBP_CNTL, HUBP_DISABLE_STOP_DATA_DURING_VM, mask_sh),\
	HUBP_SF(HUBPREQ0_DCSURF_FLIP_CONTROL, HUBPREQ_MASTER_UPDATE_LOCK_STATUS, mask_sh),\
	HUBP_SF(HUBPREQ0_DCSURF_FLIP_CONTROL2, SURFACE_GSL_ENABLE, mask_sh),\
	HUBP_SF(HUBPREQ0_DCSURF_FLIP_CONTROL2, SURFACE_TRIPLE_BUFFER_ENABLE, mask_sh),\
	HUBP_SF(HUBPREQ0_VMID_SETTINGS_0, VMID, mask_sh),\
	HUBP_SF(HUBPREQ0_FLIP_PARAMETERS_3, REFCYC_PER_VM_GROUP_FLIP, mask_sh),\
	HUBP_SF(HUBPREQ0_FLIP_PARAMETERS_4, REFCYC_PER_VM_REQ_FLIP, mask_sh),\
	HUBP_SF(HUBPREQ0_FLIP_PARAMETERS_5, REFCYC_PER_PTE_GROUP_FLIP_C, mask_sh),\
	HUBP_SF(HUBPREQ0_FLIP_PARAMETERS_6, REFCYC_PER_META_CHUNK_FLIP_C, mask_sh),\
	HUBP_SF(HUBPREQ0_VBLANK_PARAMETERS_5, REFCYC_PER_VM_GROUP_VBLANK, mask_sh),\
	HUBP_SF(HUBPREQ0_VBLANK_PARAMETERS_6, REFCYC_PER_VM_REQ_VBLANK, mask_sh),\
	HUBP_SF(HUBP0_DCHUBP_REQ_SIZE_CONFIG, VM_GROUP_SIZE, mask_sh)

#define HUBP_MASK_SH_LIST_DCN21(mask_sh)\
	HUBP_MASK_SH_LIST_DCN21_COMMON(mask_sh),\
	HUBP_SF(HUBP0_DCSURF_TILING_CONFIG, RB_ALIGNED, mask_sh)


struct dcn21_hubp {
	struct hubp base;
	struct dcn_hubp_state state;
	const struct dcn_hubp2_registers *hubp_regs;
	const struct dcn_hubp2_shift *hubp_shift;
	const struct dcn_hubp2_mask *hubp_mask;
};

bool hubp21_construct(
	struct dcn21_hubp *hubp21,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask);

void apply_DEDCN21_142_wa_for_hostvm_deadline(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr);

void hubp21_program_deadline(
		struct hubp *hubp,
		struct _vcs_dpi_display_dlg_regs_st *dlg_attr,
		struct _vcs_dpi_display_ttu_regs_st *ttu_attr);

void hubp21_program_requestor(
		struct hubp *hubp,
		struct _vcs_dpi_display_rq_regs_st *rq_regs);
#endif /* DAL_DC_DCN21_DCN21_HUBP_H_ */
