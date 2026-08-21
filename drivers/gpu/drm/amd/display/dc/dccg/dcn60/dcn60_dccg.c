// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "reg_helper.h"
#include "core_types.h"
#include "dcn42/dcn42_dccg.h"
#include "dcn60_dccg.h"

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

static void dccg60_set_pixel_rate_div(
		struct dccg *dccg,
		uint32_t otg_inst,
		enum pixel_rate_div tmds_div,
		enum pixel_rate_div unused)
{
	(void)unused;
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t cur_tmds_div = PIXEL_RATE_DIV_NA;
	uint32_t dp_dto_int;
	uint32_t reg_val;

	// only 2 is valid on dcn401
	if (tmds_div != PIXEL_RATE_DIV_BY_2 && tmds_div != PIXEL_RATE_DIV_BY_4) {
		return;
	}

	dccg401_get_pixel_rate_div(dccg, otg_inst, &cur_tmds_div, &dp_dto_int);
	if (tmds_div == cur_tmds_div)
		return;

	// encode enum to register value
	reg_val = tmds_div == PIXEL_RATE_DIV_BY_4 ? 1 : 0;

	switch (otg_inst) {
	case 0:
		REG_UPDATE(OTG_PIXEL_RATE_DIV,
				OTG0_TMDS_PIXEL_RATE_DIV, reg_val);
		break;
	case 1:
		REG_UPDATE(OTG_PIXEL_RATE_DIV,
				OTG1_TMDS_PIXEL_RATE_DIV, reg_val);
		break;
	case 2:
		REG_UPDATE(OTG_PIXEL_RATE_DIV,
				OTG2_TMDS_PIXEL_RATE_DIV, reg_val);
		break;
	case 3:
		REG_UPDATE(OTG_PIXEL_RATE_DIV,
				OTG3_TMDS_PIXEL_RATE_DIV, reg_val);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static void dccg60_set_dto_dscclk(struct dccg *dccg, uint32_t inst,
		uint32_t num_slices_h)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (inst) {
	case 0:
		REG_UPDATE_2(DSCCLK0_DTO_PARAM, DSCCLK0_DTO_PHASE, 1,
				DSCCLK0_DTO_MODULO, 1);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK0_EN, 1);

		/*
		 * Source for dscclk should be set when dto tuned clock is used.
		 * For 1 slice config, set src to dprefclk.
		 * For 2 or more slice config, set src to dispclk.
		 */
		if (num_slices_h == 1)
			REG_UPDATE(DSCCLK_SRC_SEL, DSCCLK0_SRC_SEL, 1);
		else
			REG_UPDATE(DSCCLK_SRC_SEL, DSCCLK0_SRC_SEL, 0);

		break;
	case 1:
		REG_UPDATE_2(DSCCLK1_DTO_PARAM, DSCCLK1_DTO_PHASE, 1,
				DSCCLK1_DTO_MODULO, 1);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK1_EN, 1);

		if (num_slices_h == 1)
			REG_UPDATE(DSCCLK_SRC_SEL, DSCCLK1_SRC_SEL, 1);
		else
			REG_UPDATE(DSCCLK_SRC_SEL, DSCCLK1_SRC_SEL, 0);

		break;
	case 2:
		REG_UPDATE_2(DSCCLK2_DTO_PARAM, DSCCLK2_DTO_PHASE, 1,
				DSCCLK2_DTO_MODULO, 1);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK2_EN, 1);

		if (num_slices_h == 1)
			REG_UPDATE(DSCCLK_SRC_SEL, DSCCLK2_SRC_SEL, 1);
		else
			REG_UPDATE(DSCCLK_SRC_SEL, DSCCLK2_SRC_SEL, 0);

		break;
	case 3:
		REG_UPDATE_2(DSCCLK3_DTO_PARAM, DSCCLK3_DTO_PHASE, 1,
				DSCCLK3_DTO_MODULO, 1);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK3_EN, 1);

		if (num_slices_h == 1)
			REG_UPDATE(DSCCLK_SRC_SEL, DSCCLK3_SRC_SEL, 1);
		else
			REG_UPDATE(DSCCLK_SRC_SEL, DSCCLK3_SRC_SEL, 0);

		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static const struct dccg_funcs dccg60_funcs = {
	.enable_hdmicharclk = dccg401_enable_hdmicharclk,
	.disable_hdmicharclk = dccg401_disable_hdmicharclk,
	.set_hdmistreamclk = dccg401_set_hdmistreamclk,
	.update_dpp_dto = dccg401_update_dpp_dto,
	.get_dccg_ref_freq = dccg401_get_dccg_ref_freq,
	.dccg_init = dccg401_init,
	.set_dpstreamclk = dccg401_set_dpstreamclk,
	.enable_symclk32_se = dccg31_enable_symclk32_se,
	.disable_symclk32_se = dccg31_disable_symclk32_se,
	.enable_symclk32_le = dccg401_enable_symclk32_le,
	.disable_symclk32_le = dccg401_disable_symclk32_le,
	.set_physymclk = dccg401_set_physymclk,
	.set_dtbclk_dto = NULL,
	.set_dto_dscclk = dccg60_set_dto_dscclk,
	.set_ref_dscclk = dccg401_set_ref_dscclk,
	.set_valid_pixel_rate = NULL,
	.set_fifo_errdet_ovr_en = dccg2_set_fifo_errdet_ovr_en,
	.set_audio_dtbclk_dto = NULL,
	.otg_add_pixel = dccg42_otg_add_pixel,
	.otg_drop_pixel = dccg42_otg_drop_pixel,
	.set_pixel_rate_div = dccg60_set_pixel_rate_div,
	.get_pixel_rate_div = dccg401_get_pixel_rate_div,
	.set_dp_dto = dccg401_set_dp_dto,
	.enable_symclk_se = dccg401_enable_symclk_se,
	.disable_symclk_se = dccg401_disable_symclk_se,
	.set_dtbclk_p_src = dccg401_set_dtbclk_p_src,
	.dccg_read_reg_state = dccg31_read_reg_state,
	.allow_clock_gating = dccg2_allow_clock_gating
};

struct dccg *dccg60_create(
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
	base->funcs = &dccg60_funcs;

	dccg_dcn->regs = regs;
	dccg_dcn->dccg_shift = dccg_shift;
	dccg_dcn->dccg_mask = dccg_mask;

	return &dccg_dcn->base;
}
