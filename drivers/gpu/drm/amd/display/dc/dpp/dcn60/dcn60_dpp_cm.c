#include "dm_services.h"
#include "core_types.h"
#include "reg_helper.h"
#include "dcn60/dcn60_dpp.h"
#include "dcn30/dcn30_cm_common.h"
#include "basics/conversion.h"

#define REG(reg) \
	dpp->tf_regs->reg

#define CTX \
	dpp->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	dpp->tf_shift->field_name, dpp->tf_mask->field_name

static void dpp60_power_on_gamcor(
	struct dpp *dpp_base,
	bool power_on)
{
	struct dcn60_dpp *dpp = TO_DCN60_DPP(dpp_base);

	if (power_on) {
		REG_UPDATE(CM_MEM_PWR_CTRL, GAMCOR_MEM_PWR_FORCE, 0);
		REG_UPDATE(CM_MEM_PWR_CTRL, GAMCOR_MEM_PWR_DIS, 1);
		REG_WAIT(CM_MEM_PWR_STATUS, GAMCOR_MEM_PWR_STATE, 0, 1, 100);
	} else if (dpp->base.ctx->dc->debug.enable_mem_low_power.bits.cm) {
		dpp->base.ctx->dc->optimized_required = true;
		dpp->base.deferred_reg_writes.bits.disable_gamcor = true;
	}
}

bool dpp6_program_gamcor_lut(
	struct dpp *dpp_base, const struct pwl_params *params)
{
	enum dc_lut_mode current_mode;
	enum dc_lut_mode next_mode;
	struct dcn3_dpp *dpp = TO_DCN30_DPP(dpp_base);
	struct dcn3_xfer_func_reg gam_regs;

	dpp3_enable_cm_block(dpp_base);

	if (params == NULL) {
		REG_SET(CM_GAMCOR_CONTROL, 0, CM_GAMCOR_MODE, 0);
		if (dpp_base->ctx->dc->debug.enable_mem_low_power.bits.cm)
			dpp60_power_on_gamcor(dpp_base, false);
		return false;
	}

	dpp60_power_on_gamcor(dpp_base, true);
	REG_SET(CM_GAMCOR_CONTROL, 0, CM_GAMCOR_MODE, 2);

	current_mode = dpp30_get_gamcor_current(dpp_base);
	if (current_mode == LUT_BYPASS || current_mode == LUT_RAM_A)
		next_mode = LUT_RAM_B;
	else
		next_mode = LUT_RAM_A;

	dpp60_power_on_gamcor(dpp_base, true);
	dpp3_configure_gamcor_lut(dpp_base, next_mode == LUT_RAM_A);

	if (next_mode == LUT_RAM_B) {
		gam_regs.start_cntl_b = REG(CM_GAMCOR_RAMB_START_CNTL_B);
		gam_regs.start_cntl_g = REG(CM_GAMCOR_RAMB_START_CNTL_G);
		gam_regs.start_cntl_r = REG(CM_GAMCOR_RAMB_START_CNTL_R);
		gam_regs.start_slope_cntl_b = REG(CM_GAMCOR_RAMB_START_SLOPE_CNTL_B);
		gam_regs.start_slope_cntl_g = REG(CM_GAMCOR_RAMB_START_SLOPE_CNTL_G);
		gam_regs.start_slope_cntl_r = REG(CM_GAMCOR_RAMB_START_SLOPE_CNTL_R);
		gam_regs.start_end_cntl1_b = REG(CM_GAMCOR_RAMB_END_CNTL1_B);
		gam_regs.start_end_cntl2_b = REG(CM_GAMCOR_RAMB_END_CNTL2_B);
		gam_regs.start_end_cntl1_g = REG(CM_GAMCOR_RAMB_END_CNTL1_G);
		gam_regs.start_end_cntl2_g = REG(CM_GAMCOR_RAMB_END_CNTL2_G);
		gam_regs.start_end_cntl1_r = REG(CM_GAMCOR_RAMB_END_CNTL1_R);
		gam_regs.start_end_cntl2_r = REG(CM_GAMCOR_RAMB_END_CNTL2_R);
		gam_regs.region_start = REG(CM_GAMCOR_RAMB_REGION_0_1);
		gam_regs.region_end = REG(CM_GAMCOR_RAMB_REGION_32_33);
		gam_regs.offset_b = REG(CM_GAMCOR_RAMB_OFFSET_B);
		gam_regs.offset_g = REG(CM_GAMCOR_RAMB_OFFSET_G);
		gam_regs.offset_r = REG(CM_GAMCOR_RAMB_OFFSET_R);
		gam_regs.start_base_cntl_b = REG(CM_GAMCOR_RAMB_START_BASE_CNTL_B);
		gam_regs.start_base_cntl_g = REG(CM_GAMCOR_RAMB_START_BASE_CNTL_G);
		gam_regs.start_base_cntl_r = REG(CM_GAMCOR_RAMB_START_BASE_CNTL_R);
	} else {
		gam_regs.start_cntl_b = REG(CM_GAMCOR_RAMA_START_CNTL_B);
		gam_regs.start_cntl_g = REG(CM_GAMCOR_RAMA_START_CNTL_G);
		gam_regs.start_cntl_r = REG(CM_GAMCOR_RAMA_START_CNTL_R);
		gam_regs.start_slope_cntl_b = REG(CM_GAMCOR_RAMA_START_SLOPE_CNTL_B);
		gam_regs.start_slope_cntl_g = REG(CM_GAMCOR_RAMA_START_SLOPE_CNTL_G);
		gam_regs.start_slope_cntl_r = REG(CM_GAMCOR_RAMA_START_SLOPE_CNTL_R);
		gam_regs.start_end_cntl1_b = REG(CM_GAMCOR_RAMA_END_CNTL1_B);
		gam_regs.start_end_cntl2_b = REG(CM_GAMCOR_RAMA_END_CNTL2_B);
		gam_regs.start_end_cntl1_g = REG(CM_GAMCOR_RAMA_END_CNTL1_G);
		gam_regs.start_end_cntl2_g = REG(CM_GAMCOR_RAMA_END_CNTL2_G);
		gam_regs.start_end_cntl1_r = REG(CM_GAMCOR_RAMA_END_CNTL1_R);
		gam_regs.start_end_cntl2_r = REG(CM_GAMCOR_RAMA_END_CNTL2_R);
		gam_regs.region_start = REG(CM_GAMCOR_RAMA_REGION_0_1);
		gam_regs.region_end = REG(CM_GAMCOR_RAMA_REGION_32_33);
		gam_regs.offset_b = REG(CM_GAMCOR_RAMA_OFFSET_B);
		gam_regs.offset_g = REG(CM_GAMCOR_RAMA_OFFSET_G);
		gam_regs.offset_r = REG(CM_GAMCOR_RAMA_OFFSET_R);
		gam_regs.start_base_cntl_b = REG(CM_GAMCOR_RAMA_START_BASE_CNTL_B);
		gam_regs.start_base_cntl_g = REG(CM_GAMCOR_RAMA_START_BASE_CNTL_G);
		gam_regs.start_base_cntl_r = REG(CM_GAMCOR_RAMA_START_BASE_CNTL_R);
	}

	dpp3_gamcor_reg_field(dpp, &gam_regs);
	cm_helper_program_gamcor_xfer_func(dpp_base->ctx, params, &gam_regs);
	dpp3_program_gammcor_lut(dpp_base, params->rgb_resulted,
			params->hw_points_num, next_mode == LUT_RAM_A);
	REG_UPDATE(CM_GAMCOR_CONTROL, CM_GAMCOR_SELECT,
		next_mode == LUT_RAM_A ? 0 : 1);

	return true;
}
