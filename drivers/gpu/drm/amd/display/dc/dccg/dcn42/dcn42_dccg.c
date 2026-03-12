// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "reg_helper.h"
#include "core_types.h"
#include "dcn35/dcn35_dccg.h"
#include "dcn42_dccg.h"

#define TO_DCN_DCCG(dccg)\
	container_of(dccg, struct dcn_dccg, base)

#define REG(reg) \
	(dccg_dcn->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	dccg_dcn->dccg_shift->field_name, dccg_dcn->dccg_mask->field_name

#define CTX \
	dccg_dcn->base.ctx
#define DC_LOGGER \
	dccg->ctx->logger

void dccg42_otg_add_pixel(struct dccg *dccg,
		uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (otg_inst) {
	case 0:
		REG_UPDATE(OTG_ADD_DROP_PIXEL_CNTL,
				OTG0_ADD_PIXEL, 1);
		break;
	case 1:
		REG_UPDATE(OTG_ADD_DROP_PIXEL_CNTL,
				OTG1_ADD_PIXEL, 1);
		break;
	case 2:
		REG_UPDATE(OTG_ADD_DROP_PIXEL_CNTL,
				OTG2_ADD_PIXEL, 1);
		break;
	case 3:
		REG_UPDATE(OTG_ADD_DROP_PIXEL_CNTL,
				OTG3_ADD_PIXEL, 1);
		break;
	default:
		ASSERT(0);
	}
}

void dccg42_otg_drop_pixel(struct dccg *dccg,
		uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (otg_inst) {
	case 0:
		REG_UPDATE(OTG_ADD_DROP_PIXEL_CNTL,
				OTG0_DROP_PIXEL, 1);
		break;
	case 1:
		REG_UPDATE(OTG_ADD_DROP_PIXEL_CNTL,
				OTG1_DROP_PIXEL, 1);
		break;
	case 2:
		REG_UPDATE(OTG_ADD_DROP_PIXEL_CNTL,
				OTG2_DROP_PIXEL, 1);
		break;
	case 3:
		REG_UPDATE(OTG_ADD_DROP_PIXEL_CNTL,
				OTG3_DROP_PIXEL, 1);
		break;
	default:
		ASSERT(0);
	}
}

void dccg42_enable_global_fgcg(struct dccg *dccg, bool value)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (dccg->ctx->dc->debug.disable_clock_gate)
		value = false;
	REG_UPDATE(DCCG_GLOBAL_FGCG_REP_CNTL, DCCG_GLOBAL_FGCG_REP_DIS, !value);
}

void dccg42_set_physymclk(
		struct dccg *dccg,
		int phy_inst,
		enum physymclk_clock_source clk_src,
		bool force_enable)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (phy_inst) {
	case 0:
		if (force_enable) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYASYMCLK_ROOT_GATE_DISABLE, 1);
			REG_UPDATE_2(PHYASYMCLK_CLOCK_CNTL,
					PHYASYMCLK_EN, 1,
					PHYASYMCLK_SRC_SEL, clk_src);
		} else {
			REG_UPDATE_2(PHYASYMCLK_CLOCK_CNTL,
					PHYASYMCLK_EN, 0,
					PHYASYMCLK_SRC_SEL, 0);
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYASYMCLK_ROOT_GATE_DISABLE,
					dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk ? 0 : 1);
		}
		break;
	case 1:
		if (force_enable) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYBSYMCLK_ROOT_GATE_DISABLE, 1);
			REG_UPDATE_2(PHYBSYMCLK_CLOCK_CNTL,
					PHYBSYMCLK_EN, 1,
					PHYBSYMCLK_SRC_SEL, clk_src);
		} else {
			REG_UPDATE_2(PHYBSYMCLK_CLOCK_CNTL,
					PHYBSYMCLK_EN, 0,
					PHYBSYMCLK_SRC_SEL, 0);
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYBSYMCLK_ROOT_GATE_DISABLE,
					dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk ? 0 : 1);
		}
		break;
	case 2:
		if (force_enable) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYCSYMCLK_ROOT_GATE_DISABLE, 1);
			REG_UPDATE_2(PHYCSYMCLK_CLOCK_CNTL,
					PHYCSYMCLK_EN, 1,
					PHYCSYMCLK_SRC_SEL, clk_src);
		} else {
			REG_UPDATE_2(PHYCSYMCLK_CLOCK_CNTL,
					PHYCSYMCLK_EN, 0,
					PHYCSYMCLK_SRC_SEL, 0);
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYCSYMCLK_ROOT_GATE_DISABLE,
					dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk ? 0 : 1);
		}
		break;
	case 3:
		if (force_enable) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYDSYMCLK_ROOT_GATE_DISABLE, 1);
			REG_UPDATE_2(PHYDSYMCLK_CLOCK_CNTL,
					PHYDSYMCLK_EN, 1,
					PHYDSYMCLK_SRC_SEL, clk_src);
		} else {
			REG_UPDATE_2(PHYDSYMCLK_CLOCK_CNTL,
					PHYDSYMCLK_EN, 0,
					PHYDSYMCLK_SRC_SEL, 0);
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYDSYMCLK_ROOT_GATE_DISABLE,
					dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk ? 0 : 1);
		}
		break;
	case 4:
		if (force_enable) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYESYMCLK_ROOT_GATE_DISABLE, 1);
			REG_UPDATE_2(PHYESYMCLK_CLOCK_CNTL,
					PHYESYMCLK_EN, 1,
					PHYESYMCLK_SRC_SEL, clk_src);
		} else {
			REG_UPDATE_2(PHYESYMCLK_CLOCK_CNTL,
					PHYESYMCLK_EN, 0,
					PHYESYMCLK_SRC_SEL, 0);
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYESYMCLK_ROOT_GATE_DISABLE,
					dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk ? 0 : 1);
		}
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static void dccg42_init(struct dccg *dccg)
{
	int otg_inst;
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	/* Set HPO stream encoder to use refclk to avoid case where PHY is
	 * disabled and SYMCLK32 for HPO SE is sourced from PHYD32CLK which
	 * will cause DCN to hang.
	 */
	for (otg_inst = 0; otg_inst < 4; otg_inst++)
		dccg35_disable_symclk32_se(dccg, otg_inst);

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le) {
		dccg401_disable_symclk32_le(dccg, 0);
		dccg401_disable_symclk32_le(dccg, 1);
		dccg401_disable_symclk32_le(dccg, 2);
		dccg401_disable_symclk32_le(dccg, 3);
	}

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream) {
		dccg401_disable_dpstreamclk(dccg, 0);
		dccg401_disable_dpstreamclk(dccg, 1);
		dccg401_disable_dpstreamclk(dccg, 2);
		dccg401_disable_dpstreamclk(dccg, 3);
	}
	if (!dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk) {
		REG_UPDATE_5(DCCG_GATE_DISABLE_CNTL2,
			PHYASYMCLK_ROOT_GATE_DISABLE, 1,
			PHYBSYMCLK_ROOT_GATE_DISABLE, 1,
			PHYCSYMCLK_ROOT_GATE_DISABLE, 1,
			PHYDSYMCLK_ROOT_GATE_DISABLE, 1,
			PHYESYMCLK_ROOT_GATE_DISABLE, 1);
	}
}


static const struct dccg_funcs dccg42_funcs = {
	.update_dpp_dto = dccg35_update_dpp_dto,
	.dpp_root_clock_control = dccg35_dpp_root_clock_control,
	.get_dccg_ref_freq = dccg401_get_dccg_ref_freq,
	.dccg_init = dccg42_init,
	.set_dpstreamclk = dccg401_set_dpstreamclk,
	/* Redundant with above^ */
	/* .set_dpstreamclk_root_clock_gating = dccg35_set_dpstreamclk_root_clock_gating, */
	.enable_symclk32_se = dccg31_enable_symclk32_se,
	.disable_symclk32_se = dccg35_disable_symclk32_se,
	.enable_symclk32_le = dccg401_enable_symclk32_le,
	.disable_symclk32_le = dccg401_disable_symclk32_le,
	.set_symclk32_le_root_clock_gating = dccg31_set_symclk32_le_root_clock_gating,
	.set_physymclk = dccg42_set_physymclk,
	.set_dtbclk_dto = NULL,
	.set_dto_dscclk = dccg401_set_dto_dscclk,
	.set_ref_dscclk = dccg401_set_ref_dscclk,
	.set_valid_pixel_rate = NULL,
	.set_fifo_errdet_ovr_en = dccg2_set_fifo_errdet_ovr_en,
	.set_audio_dtbclk_dto = NULL,
	.otg_add_pixel = dccg42_otg_add_pixel,
	.otg_drop_pixel = dccg42_otg_drop_pixel,
	.disable_dsc = dccg35_disable_dscclk,
	.enable_dsc = dccg35_enable_dscclk,
	.set_pixel_rate_div = dccg401_set_pixel_rate_div,
	.get_pixel_rate_div = dccg401_get_pixel_rate_div,
	.trigger_dio_fifo_resync = dccg35_trigger_dio_fifo_resync,
	.set_dp_dto = dccg401_set_dp_dto,
	.enable_symclk_se = dccg35_enable_symclk_se,
	.disable_symclk_se = dccg35_disable_symclk_se,
	.set_dtbclk_p_src = dccg401_set_dtbclk_p_src,
	.dccg_root_gate_disable_control = dccg35_root_gate_disable_control,
	.dccg_read_reg_state = dccg31_read_reg_state,
	.dccg_enable_global_fgcg = dccg42_enable_global_fgcg,
};

struct dccg *dccg42_create(
	struct dc_context *ctx,
	const struct dccg_registers *regs,
	const struct dccg_shift *dccg_shift,
	const struct dccg_mask *dccg_mask)
{
	struct dcn_dccg *dccg_dcn = kzalloc(sizeof(*dccg_dcn), GFP_KERNEL);
	struct dccg *base;

	if (dccg_dcn == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	base = &dccg_dcn->base;
	base->ctx = ctx;
	base->funcs = &dccg42_funcs;

	dccg_dcn->regs = regs;
	dccg_dcn->dccg_shift = dccg_shift;
	dccg_dcn->dccg_mask = dccg_mask;

	return &dccg_dcn->base;
}
