/* SPDX-License-Identifier: MIT */
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
 */

#include "reg_helper.h"
#include "core_types.h"
#include "dcn35_dccg.h"

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

static void dccg35_trigger_dio_fifo_resync(struct dccg *dccg)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t dispclk_rdivider_value = 0;

	REG_GET(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_RDIVIDER, &dispclk_rdivider_value);
	if (dispclk_rdivider_value != 0)
		REG_UPDATE(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_WDIVIDER, dispclk_rdivider_value);
}

static void dcn35_set_dppclk_enable(struct dccg *dccg,
				 uint32_t dpp_inst, uint32_t enable)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (dpp_inst) {
	case 0:
		REG_UPDATE(DPPCLK_CTRL, DPPCLK0_EN, enable);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpp)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DPPCLK0_ROOT_GATE_DISABLE, enable);
		break;
	case 1:
		REG_UPDATE(DPPCLK_CTRL, DPPCLK1_EN, enable);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpp)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DPPCLK1_ROOT_GATE_DISABLE, enable);
		break;
	case 2:
		REG_UPDATE(DPPCLK_CTRL, DPPCLK2_EN, enable);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpp)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DPPCLK2_ROOT_GATE_DISABLE, enable);
		break;
	case 3:
		REG_UPDATE(DPPCLK_CTRL, DPPCLK3_EN, enable);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpp)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DPPCLK3_ROOT_GATE_DISABLE, enable);
		break;
	default:
		break;
	}

}

static void dccg35_update_dpp_dto(struct dccg *dccg, int dpp_inst,
				  int req_dppclk)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (dccg->dpp_clock_gated[dpp_inst]) {
		/*
		 * Do not update the DPPCLK DTO if the clock is stopped.
		 */
		return;
	}

	if (dccg->ref_dppclk && req_dppclk) {
		int ref_dppclk = dccg->ref_dppclk;
		int modulo, phase;

		// phase / modulo = dpp pipe clk / dpp global clk
		modulo = 0xff;   // use FF at the end
		phase = ((modulo * req_dppclk) + ref_dppclk - 1) / ref_dppclk;

		if (phase > 0xff) {
			ASSERT(false);
			phase = 0xff;
		}

		REG_SET_2(DPPCLK_DTO_PARAM[dpp_inst], 0,
				DPPCLK0_DTO_PHASE, phase,
				DPPCLK0_DTO_MODULO, modulo);

		dcn35_set_dppclk_enable(dccg, dpp_inst, true);
	} else
		dcn35_set_dppclk_enable(dccg, dpp_inst, false);
	dccg->pipe_dppclk_khz[dpp_inst] = req_dppclk;
}

static void dccg35_set_dppclk_root_clock_gating(struct dccg *dccg,
		 uint32_t dpp_inst, uint32_t enable)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (!dccg->ctx->dc->debug.root_clock_optimization.bits.dpp)
		return;

	switch (dpp_inst) {
	case 0:
		REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DPPCLK0_ROOT_GATE_DISABLE, enable);
		break;
	case 1:
		REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DPPCLK1_ROOT_GATE_DISABLE, enable);
		break;
	case 2:
		REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DPPCLK2_ROOT_GATE_DISABLE, enable);
		break;
	case 3:
		REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DPPCLK3_ROOT_GATE_DISABLE, enable);
		break;
	default:
		break;
	}
}

static void dccg35_get_pixel_rate_div(
		struct dccg *dccg,
		uint32_t otg_inst,
		uint32_t *k1,
		uint32_t *k2)
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

	*k1 = val_k1;
	*k2 = val_k2;
}

static void dccg35_set_pixel_rate_div(
		struct dccg *dccg,
		uint32_t otg_inst,
		enum pixel_rate_div k1,
		enum pixel_rate_div k2)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t cur_k1 = PIXEL_RATE_DIV_NA;
	uint32_t cur_k2 = PIXEL_RATE_DIV_NA;


	// Don't program 0xF into the register field. Not valid since
	// K1 / K2 field is only 1 / 2 bits wide
	if (k1 == PIXEL_RATE_DIV_NA || k2 == PIXEL_RATE_DIV_NA) {
		BREAK_TO_DEBUGGER();
		return;
	}

	dccg35_get_pixel_rate_div(dccg, otg_inst, &cur_k1, &cur_k2);
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

static void dccg35_set_dtbclk_p_src(
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
static void dccg35_set_dtbclk_dto(
		struct dccg *dccg,
		const struct dtbclk_dto_params *params)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	/* DTO Output Rate / Pixel Rate = 1/4 */
	int req_dtbclk_khz = params->pixclk_khz / 4;

	if (params->ref_dtbclk_khz && req_dtbclk_khz) {
		uint32_t modulo, phase;

		switch (params->otg_inst) {
		case 0:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P0_GATE_DISABLE, 1);
			break;
		case 1:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P1_GATE_DISABLE, 1);
			break;
		case 2:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P2_GATE_DISABLE, 1);
			break;
		case 3:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P3_GATE_DISABLE, 1);
			break;
		}

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
		dccg35_set_pixel_rate_div(dccg, params->otg_inst, PIXEL_RATE_DIV_BY_1, PIXEL_RATE_DIV_BY_1);

		/* The recommended programming sequence to enable DTBCLK DTO to generate
		 * valid pixel HPO DPSTREAM ENCODER, specifies that DTO source select should
		 * be set only after DTO is enabled
		 */
		REG_UPDATE(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				PIPE_DTO_SRC_SEL[params->otg_inst], 2);
	} else {
		switch (params->otg_inst) {
		case 0:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P0_GATE_DISABLE, 0);
			break;
		case 1:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P1_GATE_DISABLE, 0);
			break;
		case 2:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P2_GATE_DISABLE, 0);
			break;
		case 3:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P3_GATE_DISABLE, 0);
			break;
		}

		REG_UPDATE_2(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLK_DTO_ENABLE[params->otg_inst], 0,
				PIPE_DTO_SRC_SEL[params->otg_inst], params->is_hdmi ? 0 : 1);

		REG_WRITE(DTBCLK_DTO_MODULO[params->otg_inst], 0);
		REG_WRITE(DTBCLK_DTO_PHASE[params->otg_inst], 0);
	}
}

static void dccg35_set_dpstreamclk(
		struct dccg *dccg,
		enum streamclk_source src,
		int otg_inst,
		int dp_hpo_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	/* set the dtbclk_p source */
	dccg35_set_dtbclk_p_src(dccg, src, otg_inst);

	/* enabled to select one of the DTBCLKs for pipe */
	switch (dp_hpo_inst) {
	case 0:
		REG_UPDATE_2(DPSTREAMCLK_CNTL, DPSTREAMCLK0_EN,
				(src == REFCLK) ? 0 : 1, DPSTREAMCLK0_SRC_SEL, otg_inst);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK0_ROOT_GATE_DISABLE, (src == REFCLK) ? 0 : 1);
		break;
	case 1:
		REG_UPDATE_2(DPSTREAMCLK_CNTL, DPSTREAMCLK1_EN,
				(src == REFCLK) ? 0 : 1, DPSTREAMCLK1_SRC_SEL, otg_inst);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK1_ROOT_GATE_DISABLE, (src == REFCLK) ? 0 : 1);
		break;
	case 2:
		REG_UPDATE_2(DPSTREAMCLK_CNTL, DPSTREAMCLK2_EN,
				(src == REFCLK) ? 0 : 1, DPSTREAMCLK2_SRC_SEL, otg_inst);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK2_ROOT_GATE_DISABLE, (src == REFCLK) ? 0 : 1);
		break;
	case 3:
		REG_UPDATE_2(DPSTREAMCLK_CNTL, DPSTREAMCLK3_EN,
				(src == REFCLK) ? 0 : 1, DPSTREAMCLK3_SRC_SEL, otg_inst);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK3_ROOT_GATE_DISABLE, (src == REFCLK) ? 0 : 1);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}


static void dccg35_set_dpstreamclk_root_clock_gating(
		struct dccg *dccg,
		int dp_hpo_inst,
		bool enable)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (dp_hpo_inst) {
	case 0:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK0_ROOT_GATE_DISABLE, enable ? 1 : 0);
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK0_GATE_DISABLE, enable ? 1 : 0);
		}
		break;
	case 1:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK1_ROOT_GATE_DISABLE, enable ? 1 : 0);
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK1_GATE_DISABLE, enable ? 1 : 0);
		}
		break;
	case 2:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK2_ROOT_GATE_DISABLE, enable ? 1 : 0);
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK2_GATE_DISABLE, enable ? 1 : 0);
		}
		break;
	case 3:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK3_ROOT_GATE_DISABLE, enable ? 1 : 0);
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK3_GATE_DISABLE, enable ? 1 : 0);
		}
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}



static void dccg35_set_physymclk_root_clock_gating(
		struct dccg *dccg,
		int phy_inst,
		bool enable)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (!dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
		return;

	switch (phy_inst) {
	case 0:
		REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
				PHYASYMCLK_ROOT_GATE_DISABLE, enable ? 1 : 0);
		break;
	case 1:
		REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
				PHYBSYMCLK_ROOT_GATE_DISABLE, enable ? 1 : 0);
		break;
	case 2:
		REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
				PHYCSYMCLK_ROOT_GATE_DISABLE, enable ? 1 : 0);
		break;
	case 3:
		REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
				PHYDSYMCLK_ROOT_GATE_DISABLE, enable ? 1 : 0);
		break;
	case 4:
		REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
				PHYESYMCLK_ROOT_GATE_DISABLE, enable ? 1 : 0);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static void dccg35_set_physymclk(
		struct dccg *dccg,
		int phy_inst,
		enum physymclk_clock_source clk_src,
		bool force_enable)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	/* Force PHYSYMCLK on and Select phyd32clk as the source of clock which is output to PHY through DCIO */
	switch (phy_inst) {
	case 0:
		if (force_enable) {
			REG_UPDATE_2(PHYASYMCLK_CLOCK_CNTL,
					PHYASYMCLK_EN, 1,
					PHYASYMCLK_SRC_SEL, clk_src);
		} else {
			REG_UPDATE_2(PHYASYMCLK_CLOCK_CNTL,
					PHYASYMCLK_EN, 0,
					PHYASYMCLK_SRC_SEL, 0);
		}
		break;
	case 1:
		if (force_enable) {
			REG_UPDATE_2(PHYBSYMCLK_CLOCK_CNTL,
					PHYBSYMCLK_EN, 1,
					PHYBSYMCLK_SRC_SEL, clk_src);
		} else {
			REG_UPDATE_2(PHYBSYMCLK_CLOCK_CNTL,
					PHYBSYMCLK_EN, 0,
					PHYBSYMCLK_SRC_SEL, 0);
		}
		break;
	case 2:
		if (force_enable) {
			REG_UPDATE_2(PHYCSYMCLK_CLOCK_CNTL,
					PHYCSYMCLK_EN, 1,
					PHYCSYMCLK_SRC_SEL, clk_src);
		} else {
			REG_UPDATE_2(PHYCSYMCLK_CLOCK_CNTL,
					PHYCSYMCLK_EN, 0,
					PHYCSYMCLK_SRC_SEL, 0);
		}
		break;
	case 3:
		if (force_enable) {
			REG_UPDATE_2(PHYDSYMCLK_CLOCK_CNTL,
					PHYDSYMCLK_EN, 1,
					PHYDSYMCLK_SRC_SEL, clk_src);
		} else {
			REG_UPDATE_2(PHYDSYMCLK_CLOCK_CNTL,
					PHYDSYMCLK_EN, 0,
					PHYDSYMCLK_SRC_SEL, 0);
		}
		break;
	case 4:
		if (force_enable) {
			REG_UPDATE_2(PHYESYMCLK_CLOCK_CNTL,
					PHYESYMCLK_EN, 1,
					PHYESYMCLK_SRC_SEL, clk_src);
		} else {
			REG_UPDATE_2(PHYESYMCLK_CLOCK_CNTL,
					PHYESYMCLK_EN, 0,
					PHYESYMCLK_SRC_SEL, 0);
		}
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static void dccg35_set_valid_pixel_rate(
		struct dccg *dccg,
		int ref_dtbclk_khz,
		int otg_inst,
		int pixclk_khz)
{
	struct dtbclk_dto_params dto_params = {0};

	dto_params.ref_dtbclk_khz = ref_dtbclk_khz;
	dto_params.otg_inst = otg_inst;
	dto_params.pixclk_khz = pixclk_khz;
	dto_params.is_hdmi = true;

	dccg35_set_dtbclk_dto(dccg, &dto_params);
}

static void dccg35_dpp_root_clock_control(
		struct dccg *dccg,
		unsigned int dpp_inst,
		bool clock_on)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (dccg->dpp_clock_gated[dpp_inst] == clock_on)
		return;

	if (clock_on) {
		/* turn off the DTO and leave phase/modulo at max */
		dcn35_set_dppclk_enable(dccg, dpp_inst, 1);
		REG_SET_2(DPPCLK_DTO_PARAM[dpp_inst], 0,
			  DPPCLK0_DTO_PHASE, 0xFF,
			  DPPCLK0_DTO_MODULO, 0xFF);
	} else {
		dcn35_set_dppclk_enable(dccg, dpp_inst, 0);
		/* turn on the DTO to generate a 0hz clock */
		REG_SET_2(DPPCLK_DTO_PARAM[dpp_inst], 0,
			  DPPCLK0_DTO_PHASE, 0,
			  DPPCLK0_DTO_MODULO, 1);
	}

	dccg->dpp_clock_gated[dpp_inst] = !clock_on;
}

static void dccg35_disable_symclk32_se(
		struct dccg *dccg,
		int hpo_se_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	/* set refclk as the source for symclk32_se */
	switch (hpo_se_inst) {
	case 0:
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE0_SRC_SEL, 0,
				SYMCLK32_SE0_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE0_GATE_DISABLE, 0);
//			REG_UPDATE(DCCG_GATE_DISABLE_CNTL3,
//					SYMCLK32_ROOT_SE0_GATE_DISABLE, 0);
		}
		break;
	case 1:
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE1_SRC_SEL, 0,
				SYMCLK32_SE1_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE1_GATE_DISABLE, 0);
//			REG_UPDATE(DCCG_GATE_DISABLE_CNTL3,
//					SYMCLK32_ROOT_SE1_GATE_DISABLE, 0);
		}
		break;
	case 2:
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE2_SRC_SEL, 0,
				SYMCLK32_SE2_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE2_GATE_DISABLE, 0);
//			REG_UPDATE(DCCG_GATE_DISABLE_CNTL3,
//					SYMCLK32_ROOT_SE2_GATE_DISABLE, 0);
		}
		break;
	case 3:
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE3_SRC_SEL, 0,
				SYMCLK32_SE3_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se) {
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE3_GATE_DISABLE, 0);
//			REG_UPDATE(DCCG_GATE_DISABLE_CNTL3,
//					SYMCLK32_ROOT_SE3_GATE_DISABLE, 0);
		}
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg35_init(struct dccg *dccg)
{
	int otg_inst;
	/* Set HPO stream encoder to use refclk to avoid case where PHY is
	 * disabled and SYMCLK32 for HPO SE is sourced from PHYD32CLK which
	 * will cause DCN to hang.
	 */
	for (otg_inst = 0; otg_inst < 4; otg_inst++)
		dccg35_disable_symclk32_se(dccg, otg_inst);

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
		for (otg_inst = 0; otg_inst < 2; otg_inst++) {
			dccg31_disable_symclk32_le(dccg, otg_inst);
			dccg31_set_symclk32_le_root_clock_gating(dccg, otg_inst, false);
		}

//	if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
//		for (otg_inst = 0; otg_inst < 4; otg_inst++)
//			dccg35_disable_symclk_se(dccg, otg_inst, otg_inst);


	if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
		for (otg_inst = 0; otg_inst < 4; otg_inst++) {
			dccg35_set_dpstreamclk(dccg, REFCLK, otg_inst,
						otg_inst);
			dccg35_set_dpstreamclk_root_clock_gating(dccg, otg_inst, false);
		}

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpp)
		for (otg_inst = 0; otg_inst < 4; otg_inst++)
			dccg35_set_dppclk_root_clock_gating(dccg, otg_inst, 0);

/*
	dccg35_enable_global_fgcg_rep(
		dccg, dccg->ctx->dc->debug.enable_fine_grain_clock_gating.bits
			      .dccg_global_fgcg_rep);*/
}

void dccg35_enable_global_fgcg_rep(struct dccg *dccg, bool value)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE(DCCG_GLOBAL_FGCG_REP_CNTL, DCCG_GLOBAL_FGCG_REP_DIS, !value);
}

static void dccg35_enable_dscclk(struct dccg *dccg, int inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	//Disable DTO
	switch (inst) {
	case 0:
		REG_UPDATE_2(DSCCLK0_DTO_PARAM,
				DSCCLK0_DTO_PHASE, 0,
				DSCCLK0_DTO_MODULO, 0);
		REG_UPDATE(DSCCLK_DTO_CTRL,	DSCCLK0_EN, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DSCCLK0_ROOT_GATE_DISABLE, 1);
		break;
	case 1:
		REG_UPDATE_2(DSCCLK1_DTO_PARAM,
				DSCCLK1_DTO_PHASE, 0,
				DSCCLK1_DTO_MODULO, 0);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK1_EN, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DSCCLK1_ROOT_GATE_DISABLE, 1);
		break;
	case 2:
		REG_UPDATE_2(DSCCLK2_DTO_PARAM,
				DSCCLK2_DTO_PHASE, 0,
				DSCCLK2_DTO_MODULO, 0);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK2_EN, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DSCCLK2_ROOT_GATE_DISABLE, 1);
		break;
	case 3:
		REG_UPDATE_2(DSCCLK3_DTO_PARAM,
				DSCCLK3_DTO_PHASE, 0,
				DSCCLK3_DTO_MODULO, 0);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK3_EN, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DSCCLK3_ROOT_GATE_DISABLE, 1);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static void dccg35_disable_dscclk(struct dccg *dccg,
				int inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (!dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
		return;

	switch (inst) {
	case 0:
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK0_EN, 0);
		REG_UPDATE_2(DSCCLK0_DTO_PARAM,
				DSCCLK0_DTO_PHASE, 0,
				DSCCLK0_DTO_MODULO, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DSCCLK0_ROOT_GATE_DISABLE, 0);
		break;
	case 1:
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK1_EN, 0);
		REG_UPDATE_2(DSCCLK1_DTO_PARAM,
				DSCCLK1_DTO_PHASE, 0,
				DSCCLK1_DTO_MODULO, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DSCCLK1_ROOT_GATE_DISABLE, 0);
		break;
	case 2:
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK2_EN, 0);
		REG_UPDATE_2(DSCCLK2_DTO_PARAM,
				DSCCLK2_DTO_PHASE, 0,
				DSCCLK2_DTO_MODULO, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DSCCLK2_ROOT_GATE_DISABLE, 0);
		break;
	case 3:
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK3_EN, 0);
		REG_UPDATE_2(DSCCLK3_DTO_PARAM,
				DSCCLK3_DTO_PHASE, 0,
				DSCCLK3_DTO_MODULO, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL6, DSCCLK3_ROOT_GATE_DISABLE, 0);
		break;
	default:
		return;
	}
}

static void dccg35_enable_symclk_se(struct dccg *dccg, uint32_t stream_enc_inst, uint32_t link_enc_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (link_enc_inst) {
	case 0:
		REG_UPDATE(SYMCLKA_CLOCK_ENABLE,
				SYMCLKA_CLOCK_ENABLE, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKA_ROOT_GATE_DISABLE, 1);
		break;
	case 1:
		REG_UPDATE(SYMCLKB_CLOCK_ENABLE,
				SYMCLKB_CLOCK_ENABLE, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKB_ROOT_GATE_DISABLE, 1);
		break;
	case 2:
		REG_UPDATE(SYMCLKC_CLOCK_ENABLE,
				SYMCLKC_CLOCK_ENABLE, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKC_ROOT_GATE_DISABLE, 1);
		break;
	case 3:
		REG_UPDATE(SYMCLKD_CLOCK_ENABLE,
				SYMCLKD_CLOCK_ENABLE, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKD_ROOT_GATE_DISABLE, 1);
		break;
	case 4:
		REG_UPDATE(SYMCLKE_CLOCK_ENABLE,
				SYMCLKE_CLOCK_ENABLE, 1);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKE_ROOT_GATE_DISABLE, 1);
		break;
	}

	switch (stream_enc_inst) {
	case 0:
		REG_UPDATE_2(SYMCLKA_CLOCK_ENABLE,
				SYMCLKA_FE_EN, 1,
				SYMCLKA_FE_SRC_SEL, link_enc_inst);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKA_FE_ROOT_GATE_DISABLE, 1);
		break;
	case 1:
		REG_UPDATE_2(SYMCLKB_CLOCK_ENABLE,
				SYMCLKB_FE_EN, 1,
				SYMCLKB_FE_SRC_SEL, link_enc_inst);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKB_FE_ROOT_GATE_DISABLE, 1);
		break;
	case 2:
		REG_UPDATE_2(SYMCLKC_CLOCK_ENABLE,
				SYMCLKC_FE_EN, 1,
				SYMCLKC_FE_SRC_SEL, link_enc_inst);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKC_FE_ROOT_GATE_DISABLE, 1);
		break;
	case 3:
		REG_UPDATE_2(SYMCLKD_CLOCK_ENABLE,
				SYMCLKD_FE_EN, 1,
				SYMCLKD_FE_SRC_SEL, link_enc_inst);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKD_FE_ROOT_GATE_DISABLE, 1);
		break;
	case 4:
		REG_UPDATE_2(SYMCLKE_CLOCK_ENABLE,
				SYMCLKE_FE_EN, 1,
				SYMCLKE_FE_SRC_SEL, link_enc_inst);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKE_FE_ROOT_GATE_DISABLE, 1);
		break;
	}
}

/*get other front end connected to this backend*/
static uint8_t dccg35_get_other_enabled_symclk_fe(struct dccg *dccg, uint32_t stream_enc_inst, uint32_t link_enc_inst)
{
	uint8_t num_enabled_symclk_fe = 0;
	uint32_t be_clk_en = 0, fe_clk_en[5] = {0}, be_clk_sel[5] = {0};
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (link_enc_inst) {
	case 0:
		REG_GET_3(SYMCLKA_CLOCK_ENABLE, SYMCLKA_CLOCK_ENABLE, &be_clk_en,
				SYMCLKA_FE_EN, &fe_clk_en[0],
				SYMCLKA_FE_SRC_SEL, &be_clk_sel[0]);
				break;
	case 1:
		REG_GET_3(SYMCLKB_CLOCK_ENABLE, SYMCLKB_CLOCK_ENABLE, &be_clk_en,
				SYMCLKB_FE_EN, &fe_clk_en[1],
				SYMCLKB_FE_SRC_SEL, &be_clk_sel[1]);
				break;
	case 2:
			REG_GET_3(SYMCLKC_CLOCK_ENABLE, SYMCLKC_CLOCK_ENABLE, &be_clk_en,
				SYMCLKC_FE_EN, &fe_clk_en[2],
				SYMCLKC_FE_SRC_SEL, &be_clk_sel[2]);
				break;
	case 3:
			REG_GET_3(SYMCLKD_CLOCK_ENABLE, SYMCLKD_CLOCK_ENABLE, &be_clk_en,
				SYMCLKD_FE_EN, &fe_clk_en[3],
				SYMCLKD_FE_SRC_SEL, &be_clk_sel[3]);
				break;
	case 4:
			REG_GET_3(SYMCLKE_CLOCK_ENABLE, SYMCLKE_CLOCK_ENABLE, &be_clk_en,
				SYMCLKE_FE_EN, &fe_clk_en[4],
				SYMCLKE_FE_SRC_SEL, &be_clk_sel[4]);
				break;
	}
	if (be_clk_en) {
	/* for DPMST, this backend could be used by multiple front end.
	only disable the backend if this stream_enc_ins is the last active stream enc connected to this back_end*/
		uint8_t i;
		for (i = 0; i != link_enc_inst && i < ARRAY_SIZE(fe_clk_en); i++) {
			if (fe_clk_en[i] && be_clk_sel[i] == link_enc_inst)
				num_enabled_symclk_fe++;
		}
	}
	return num_enabled_symclk_fe;
}

static void dccg35_disable_symclk_se(struct dccg *dccg, uint32_t stream_enc_inst, uint32_t link_enc_inst)
{
	uint8_t num_enabled_symclk_fe = 0;
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (stream_enc_inst) {
	case 0:
		REG_UPDATE_2(SYMCLKA_CLOCK_ENABLE,
				SYMCLKA_FE_EN, 0,
				SYMCLKA_FE_SRC_SEL, 0);
//		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
//			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKA_FE_ROOT_GATE_DISABLE, 0);
		break;
	case 1:
		REG_UPDATE_2(SYMCLKB_CLOCK_ENABLE,
				SYMCLKB_FE_EN, 0,
				SYMCLKB_FE_SRC_SEL, 0);
//		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
//			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKB_FE_ROOT_GATE_DISABLE, 0);
		break;
	case 2:
		REG_UPDATE_2(SYMCLKC_CLOCK_ENABLE,
				SYMCLKC_FE_EN, 0,
				SYMCLKC_FE_SRC_SEL, 0);
//		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
//			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKC_FE_ROOT_GATE_DISABLE, 0);
		break;
	case 3:
		REG_UPDATE_2(SYMCLKD_CLOCK_ENABLE,
				SYMCLKD_FE_EN, 0,
				SYMCLKD_FE_SRC_SEL, 0);
//		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
//			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKD_FE_ROOT_GATE_DISABLE, 0);
		break;
	case 4:
		REG_UPDATE_2(SYMCLKE_CLOCK_ENABLE,
				SYMCLKE_FE_EN, 0,
				SYMCLKE_FE_SRC_SEL, 0);
//		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
//			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKE_FE_ROOT_GATE_DISABLE, 0);
		break;
	}

	/*check other enabled symclk fe */
	num_enabled_symclk_fe = dccg35_get_other_enabled_symclk_fe(dccg, stream_enc_inst, link_enc_inst);
	/*only turn off backend clk if other front end attachecd to this backend are all off,
	 for mst, only turn off the backend if this is the last front end*/
	if (num_enabled_symclk_fe == 0) {
		switch (link_enc_inst) {
		case 0:
			REG_UPDATE(SYMCLKA_CLOCK_ENABLE,
					SYMCLKA_CLOCK_ENABLE, 0);
//			if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
//				REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKA_ROOT_GATE_DISABLE, 0);
			break;
		case 1:
			REG_UPDATE(SYMCLKB_CLOCK_ENABLE,
					SYMCLKB_CLOCK_ENABLE, 0);
//			if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
//				REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKB_ROOT_GATE_DISABLE, 0);
			break;
		case 2:
			REG_UPDATE(SYMCLKC_CLOCK_ENABLE,
					SYMCLKC_CLOCK_ENABLE, 0);
//			if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
//				REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKC_ROOT_GATE_DISABLE, 0);
			break;
		case 3:
			REG_UPDATE(SYMCLKD_CLOCK_ENABLE,
					SYMCLKD_CLOCK_ENABLE, 0);
//			if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
//				REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKD_ROOT_GATE_DISABLE, 0);
			break;
		case 4:
			REG_UPDATE(SYMCLKE_CLOCK_ENABLE,
					SYMCLKE_CLOCK_ENABLE, 0);
//			if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
//				REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, SYMCLKE_ROOT_GATE_DISABLE, 0);
			break;
		}
	}
}

static const struct dccg_funcs dccg35_funcs = {
	.update_dpp_dto = dccg35_update_dpp_dto,
	.dpp_root_clock_control = dccg35_dpp_root_clock_control,
	.get_dccg_ref_freq = dccg31_get_dccg_ref_freq,
	.dccg_init = dccg35_init,
	.set_dpstreamclk = dccg35_set_dpstreamclk,
	.set_dpstreamclk_root_clock_gating = dccg35_set_dpstreamclk_root_clock_gating,
	.enable_symclk32_se = dccg31_enable_symclk32_se,
	.disable_symclk32_se = dccg35_disable_symclk32_se,
	.enable_symclk32_le = dccg31_enable_symclk32_le,
	.disable_symclk32_le = dccg31_disable_symclk32_le,
	.set_symclk32_le_root_clock_gating = dccg31_set_symclk32_le_root_clock_gating,
	.set_physymclk = dccg35_set_physymclk,
	.set_physymclk_root_clock_gating = dccg35_set_physymclk_root_clock_gating,
	.set_dtbclk_dto = dccg35_set_dtbclk_dto,
	.set_audio_dtbclk_dto = dccg31_set_audio_dtbclk_dto,
	.set_fifo_errdet_ovr_en = dccg2_set_fifo_errdet_ovr_en,
	.otg_add_pixel = dccg31_otg_add_pixel,
	.otg_drop_pixel = dccg31_otg_drop_pixel,
	.set_dispclk_change_mode = dccg31_set_dispclk_change_mode,
	.disable_dsc = dccg35_disable_dscclk,
	.enable_dsc = dccg35_enable_dscclk,
	.set_pixel_rate_div = dccg35_set_pixel_rate_div,
	.get_pixel_rate_div = dccg35_get_pixel_rate_div,
	.trigger_dio_fifo_resync = dccg35_trigger_dio_fifo_resync,
	.set_valid_pixel_rate = dccg35_set_valid_pixel_rate,
	.enable_symclk_se = dccg35_enable_symclk_se,
	.disable_symclk_se = dccg35_disable_symclk_se,
	.set_dtbclk_p_src = dccg35_set_dtbclk_p_src,
};

struct dccg *dccg35_create(
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
	base->funcs = &dccg35_funcs;

	dccg_dcn->regs = regs;
	dccg_dcn->dccg_shift = dccg_shift;
	dccg_dcn->dccg_mask = dccg_mask;

	return &dccg_dcn->base;
}
