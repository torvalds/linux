// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#include "reg_helper.h"
#include "core_types.h"

#include "dcn31/dcn31_dccg.h"
#include "dcn314_dccg.h"

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

static void dccg314_trigger_dio_fifo_resync(
	struct dccg *dccg)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t dispclk_rdivider_value = 0;

	REG_GET(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_RDIVIDER, &dispclk_rdivider_value);
	REG_UPDATE(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_WDIVIDER, dispclk_rdivider_value);
}

static void dccg314_get_pixel_rate_div(
		struct dccg *dccg,
		uint32_t otg_inst,
		enum pixel_rate_div *k1,
		enum pixel_rate_div *k2)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t val_k1 = PIXEL_RATE_DIV_NA, val_k2 = PIXEL_RATE_DIV_NA;

	*k1 = PIXEL_RATE_DIV_NA;
	*k2 = PIXEL_RATE_DIV_NA;

	switch (otg_inst) {
	case 0:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG0_PIXEL_RATE_DIVK1, &val_k1,
			OTG0_PIXEL_RATE_DIVK2, &val_k2);
		break;
	case 1:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG1_PIXEL_RATE_DIVK1, &val_k1,
			OTG1_PIXEL_RATE_DIVK2, &val_k2);
		break;
	case 2:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG2_PIXEL_RATE_DIVK1, &val_k1,
			OTG2_PIXEL_RATE_DIVK2, &val_k2);
		break;
	case 3:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG3_PIXEL_RATE_DIVK1, &val_k1,
			OTG3_PIXEL_RATE_DIVK2, &val_k2);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}

	*k1 = (enum pixel_rate_div)val_k1;
	*k2 = (enum pixel_rate_div)val_k2;
}

static void dccg314_set_pixel_rate_div(
		struct dccg *dccg,
		uint32_t otg_inst,
		enum pixel_rate_div k1,
		enum pixel_rate_div k2)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	enum pixel_rate_div cur_k1 = PIXEL_RATE_DIV_NA, cur_k2 = PIXEL_RATE_DIV_NA;

	// Don't program 0xF into the register field. Not valid since
	// K1 / K2 field is only 1 / 2 bits wide
	if (k1 == PIXEL_RATE_DIV_NA || k2 == PIXEL_RATE_DIV_NA) {
		BREAK_TO_DEBUGGER();
		return;
	}

	dccg314_get_pixel_rate_div(dccg, otg_inst, &cur_k1, &cur_k2);
	if (k1 == cur_k1 && k2 == cur_k2)
		return;

	switch (otg_inst) {
	case 0:
		REG_UPDATE_2(OTG_PIXEL_RATE_DIV,
				OTG0_PIXEL_RATE_DIVK1, k1,
				OTG0_PIXEL_RATE_DIVK2, k2);
		break;
	case 1:
		REG_UPDATE_2(OTG_PIXEL_RATE_DIV,
				OTG1_PIXEL_RATE_DIVK1, k1,
				OTG1_PIXEL_RATE_DIVK2, k2);
		break;
	case 2:
		REG_UPDATE_2(OTG_PIXEL_RATE_DIV,
				OTG2_PIXEL_RATE_DIVK1, k1,
				OTG2_PIXEL_RATE_DIVK2, k2);
		break;
	case 3:
		REG_UPDATE_2(OTG_PIXEL_RATE_DIV,
				OTG3_PIXEL_RATE_DIVK1, k1,
				OTG3_PIXEL_RATE_DIVK2, k2);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static void dccg314_set_dtbclk_p_src(
		struct dccg *dccg,
		enum streamclk_source src,
		uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	uint32_t p_src_sel = 0; /* selects dprefclk */

	if (src == DTBCLK0)
		p_src_sel = 2;  /* selects dtbclk0 */

	switch (otg_inst) {
	case 0:
		if (src == REFCLK)
			REG_UPDATE(DTBCLK_P_CNTL,
					DTBCLK_P0_EN, 0);
		else
			REG_UPDATE_2(DTBCLK_P_CNTL,
					DTBCLK_P0_SRC_SEL, p_src_sel,
					DTBCLK_P0_EN, 1);
		break;
	case 1:
		if (src == REFCLK)
			REG_UPDATE(DTBCLK_P_CNTL,
					DTBCLK_P1_EN, 0);
		else
			REG_UPDATE_2(DTBCLK_P_CNTL,
					DTBCLK_P1_SRC_SEL, p_src_sel,
					DTBCLK_P1_EN, 1);
		break;
	case 2:
		if (src == REFCLK)
			REG_UPDATE(DTBCLK_P_CNTL,
					DTBCLK_P2_EN, 0);
		else
			REG_UPDATE_2(DTBCLK_P_CNTL,
					DTBCLK_P2_SRC_SEL, p_src_sel,
					DTBCLK_P2_EN, 1);
		break;
	case 3:
		if (src == REFCLK)
			REG_UPDATE(DTBCLK_P_CNTL,
					DTBCLK_P3_EN, 0);
		else
			REG_UPDATE_2(DTBCLK_P_CNTL,
					DTBCLK_P3_SRC_SEL, p_src_sel,
					DTBCLK_P3_EN, 1);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}

}

/* Controls the generation of pixel valid for OTG in (OTG -> HPO case) */
static void dccg314_set_dtbclk_dto(
		struct dccg *dccg,
		const struct dtbclk_dto_params *params)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	/* DTO Output Rate / Pixel Rate = 1/4 */
	int req_dtbclk_khz = params->pixclk_khz / 4;

	if (params->ref_dtbclk_khz && req_dtbclk_khz) {
		uint32_t modulo, phase;

		// phase / modulo = dtbclk / dtbclk ref
		modulo = params->ref_dtbclk_khz * 1000;
		phase = req_dtbclk_khz * 1000;

		REG_WRITE(DTBCLK_DTO_MODULO[params->otg_inst], modulo);
		REG_WRITE(DTBCLK_DTO_PHASE[params->otg_inst], phase);

		REG_UPDATE(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLK_DTO_ENABLE[params->otg_inst], 1);

		REG_WAIT(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLKDTO_ENABLE_STATUS[params->otg_inst], 1,
				1, 100);

		/* program OTG_PIXEL_RATE_DIV for DIVK1 and DIVK2 fields */
		dccg314_set_pixel_rate_div(dccg, params->otg_inst, PIXEL_RATE_DIV_BY_1, PIXEL_RATE_DIV_BY_1);

		/* The recommended programming sequence to enable DTBCLK DTO to generate
		 * valid pixel HPO DPSTREAM ENCODER, specifies that DTO source select should
		 * be set only after DTO is enabled
		 */
		REG_UPDATE(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				PIPE_DTO_SRC_SEL[params->otg_inst], 2);
	} else {
		REG_UPDATE_2(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLK_DTO_ENABLE[params->otg_inst], 0,
				PIPE_DTO_SRC_SEL[params->otg_inst], 1);

		REG_WRITE(DTBCLK_DTO_MODULO[params->otg_inst], 0);
		REG_WRITE(DTBCLK_DTO_PHASE[params->otg_inst], 0);
	}
}

static void dccg314_set_dpstreamclk(
		struct dccg *dccg,
		enum streamclk_source src,
		int otg_inst,
		int dp_hpo_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	/* set the dtbclk_p source */
	dccg314_set_dtbclk_p_src(dccg, src, otg_inst);

	/* enabled to select one of the DTBCLKs for pipe */
	switch (dp_hpo_inst) {
	case 0:
		REG_UPDATE_2(DPSTREAMCLK_CNTL,
					DPSTREAMCLK0_EN, (src == REFCLK) ? 0 : 1,
					DPSTREAMCLK0_SRC_SEL, otg_inst);
		break;
	case 1:
		REG_UPDATE_2(DPSTREAMCLK_CNTL,
					DPSTREAMCLK1_EN, (src == REFCLK) ? 0 : 1,
					DPSTREAMCLK1_SRC_SEL, otg_inst);
		break;
	case 2:
		REG_UPDATE_2(DPSTREAMCLK_CNTL,
					DPSTREAMCLK2_EN, (src == REFCLK) ? 0 : 1,
					DPSTREAMCLK2_SRC_SEL, otg_inst);
		break;
	case 3:
		REG_UPDATE_2(DPSTREAMCLK_CNTL,
					DPSTREAMCLK3_EN, (src == REFCLK) ? 0 : 1,
					DPSTREAMCLK3_SRC_SEL, otg_inst);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static void dccg314_init(struct dccg *dccg)
{
	int otg_inst;

	/* Set HPO stream encoder to use refclk to avoid case where PHY is
	 * disabled and SYMCLK32 for HPO SE is sourced from PHYD32CLK which
	 * will cause DCN to hang.
	 */
	for (otg_inst = 0; otg_inst < 4; otg_inst++)
		dccg31_disable_symclk32_se(dccg, otg_inst);

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
		for (otg_inst = 0; otg_inst < 2; otg_inst++)
			dccg31_disable_symclk32_le(dccg, otg_inst);

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
		for (otg_inst = 0; otg_inst < 4; otg_inst++)
			dccg314_set_dpstreamclk(dccg, REFCLK, otg_inst,
						otg_inst);

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
		for (otg_inst = 0; otg_inst < 5; otg_inst++)
			dccg31_set_physymclk(dccg, otg_inst,
					     PHYSYMCLK_FORCE_SRC_SYMCLK, false);
}

static void dccg314_set_valid_pixel_rate(
		struct dccg *dccg,
		int ref_dtbclk_khz,
		int otg_inst,
		int pixclk_khz)
{
	struct dtbclk_dto_params dto_params = {0};

	dto_params.ref_dtbclk_khz = ref_dtbclk_khz;
	dto_params.otg_inst = otg_inst;
	dto_params.pixclk_khz = pixclk_khz;

	dccg314_set_dtbclk_dto(dccg, &dto_params);
}

static void dccg314_dpp_root_clock_control(
		struct dccg *dccg,
		unsigned int dpp_inst,
		bool clock_on)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (dccg->dpp_clock_gated[dpp_inst] != clock_on)
		return;

	if (clock_on) {
		/* turn off the DTO and leave phase/modulo at max */
		REG_UPDATE(DPPCLK_DTO_CTRL, DPPCLK_DTO_ENABLE[dpp_inst], 0);
		REG_SET_2(DPPCLK_DTO_PARAM[dpp_inst], 0,
			  DPPCLK0_DTO_PHASE, 0xFF,
			  DPPCLK0_DTO_MODULO, 0xFF);
	} else {
		/* turn on the DTO to generate a 0hz clock */
		REG_UPDATE(DPPCLK_DTO_CTRL, DPPCLK_DTO_ENABLE[dpp_inst], 1);
		REG_SET_2(DPPCLK_DTO_PARAM[dpp_inst], 0,
			  DPPCLK0_DTO_PHASE, 0,
			  DPPCLK0_DTO_MODULO, 1);
	}

	dccg->dpp_clock_gated[dpp_inst] = !clock_on;
}

static const struct dccg_funcs dccg314_funcs = {
	.update_dpp_dto = dccg31_update_dpp_dto,
	.dpp_root_clock_control = dccg314_dpp_root_clock_control,
	.get_dccg_ref_freq = dccg31_get_dccg_ref_freq,
	.dccg_init = dccg314_init,
	.set_dpstreamclk = dccg314_set_dpstreamclk,
	.enable_symclk32_se = dccg31_enable_symclk32_se,
	.disable_symclk32_se = dccg31_disable_symclk32_se,
	.enable_symclk32_le = dccg31_enable_symclk32_le,
	.disable_symclk32_le = dccg31_disable_symclk32_le,
	.set_symclk32_le_root_clock_gating = dccg31_set_symclk32_le_root_clock_gating,
	.set_physymclk = dccg31_set_physymclk,
	.set_dtbclk_dto = dccg314_set_dtbclk_dto,
	.set_audio_dtbclk_dto = dccg31_set_audio_dtbclk_dto,
	.set_fifo_errdet_ovr_en = dccg2_set_fifo_errdet_ovr_en,
	.otg_add_pixel = dccg31_otg_add_pixel,
	.otg_drop_pixel = dccg31_otg_drop_pixel,
	.set_dispclk_change_mode = dccg31_set_dispclk_change_mode,
	.disable_dsc = dccg31_disable_dscclk,
	.enable_dsc = dccg31_enable_dscclk,
	.set_pixel_rate_div = dccg314_set_pixel_rate_div,
	.trigger_dio_fifo_resync = dccg314_trigger_dio_fifo_resync,
	.set_valid_pixel_rate = dccg314_set_valid_pixel_rate,
};

struct dccg *dccg314_create(
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
	base->funcs = &dccg314_funcs;

	dccg_dcn->regs = regs;
	dccg_dcn->dccg_shift = dccg_shift;
	dccg_dcn->dccg_mask = dccg_mask;

	return &dccg_dcn->base;
}
