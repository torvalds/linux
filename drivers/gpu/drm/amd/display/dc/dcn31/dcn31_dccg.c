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

#include "reg_helper.h"
#include "core_types.h"
#include "dcn31_dccg.h"
#include "dal_asic_id.h"

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

void dccg31_update_dpp_dto(struct dccg *dccg, int dpp_inst, int req_dppclk)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (dccg->dpp_clock_gated[dpp_inst]) {
		/*
		 * Do not update the DPPCLK DTO if the clock is stopped.
		 * It is treated the same as if the pipe itself were in PG.
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
		REG_UPDATE(DPPCLK_DTO_CTRL,
				DPPCLK_DTO_ENABLE[dpp_inst], 1);
	} else {
		REG_UPDATE(DPPCLK_DTO_CTRL,
				DPPCLK_DTO_ENABLE[dpp_inst], 0);
	}
	dccg->pipe_dppclk_khz[dpp_inst] = req_dppclk;
}

static enum phyd32clk_clock_source get_phy_mux_symclk(
		struct dcn_dccg *dccg_dcn,
		enum phyd32clk_clock_source src)
{
	if (dccg_dcn->base.ctx->asic_id.chip_family == FAMILY_YELLOW_CARP &&
			dccg_dcn->base.ctx->asic_id.hw_internal_rev == YELLOW_CARP_B0) {
		if (src == PHYD32CLKC)
			src = PHYD32CLKF;
		if (src == PHYD32CLKD)
			src = PHYD32CLKG;
	}
	return src;
}

static void dccg31_enable_dpstreamclk(struct dccg *dccg, int otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	/* enabled to select one of the DTBCLKs for pipe */
	switch (otg_inst) {
	case 0:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK_PIPE0_EN, 1);
		break;
	case 1:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK_PIPE1_EN, 1);
		break;
	case 2:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK_PIPE2_EN, 1);
		break;
	case 3:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK_PIPE3_EN, 1);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
	if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
		REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
			DPSTREAMCLK_GATE_DISABLE, 1,
			DPSTREAMCLK_ROOT_GATE_DISABLE, 1);
}

static void dccg31_disable_dpstreamclk(struct dccg *dccg, int otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
		REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
				DPSTREAMCLK_ROOT_GATE_DISABLE, 0,
				DPSTREAMCLK_GATE_DISABLE, 0);

	switch (otg_inst) {
	case 0:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK_PIPE0_EN, 0);
		break;
	case 1:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK_PIPE1_EN, 0);
		break;
	case 2:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK_PIPE2_EN, 0);
		break;
	case 3:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK_PIPE3_EN, 0);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg31_set_dpstreamclk(
		struct dccg *dccg,
		enum streamclk_source src,
		int otg_inst,
		int dp_hpo_inst)
{
	if (src == REFCLK)
		dccg31_disable_dpstreamclk(dccg, otg_inst);
	else
		dccg31_enable_dpstreamclk(dccg, otg_inst);
}

void dccg31_enable_symclk32_se(
		struct dccg *dccg,
		int hpo_se_inst,
		enum phyd32clk_clock_source phyd32clk)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	phyd32clk = get_phy_mux_symclk(dccg_dcn, phyd32clk);

	/* select one of the PHYD32CLKs as the source for symclk32_se */
	switch (hpo_se_inst) {
	case 0:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE0_GATE_DISABLE, 1,
					SYMCLK32_ROOT_SE0_GATE_DISABLE, 1);
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE0_SRC_SEL, phyd32clk,
				SYMCLK32_SE0_EN, 1);
		break;
	case 1:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE1_GATE_DISABLE, 1,
					SYMCLK32_ROOT_SE1_GATE_DISABLE, 1);
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE1_SRC_SEL, phyd32clk,
				SYMCLK32_SE1_EN, 1);
		break;
	case 2:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE2_GATE_DISABLE, 1,
					SYMCLK32_ROOT_SE2_GATE_DISABLE, 1);
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE2_SRC_SEL, phyd32clk,
				SYMCLK32_SE2_EN, 1);
		break;
	case 3:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE3_GATE_DISABLE, 1,
					SYMCLK32_ROOT_SE3_GATE_DISABLE, 1);
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE3_SRC_SEL, phyd32clk,
				SYMCLK32_SE3_EN, 1);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg31_disable_symclk32_se(
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
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE0_GATE_DISABLE, 0,
					SYMCLK32_ROOT_SE0_GATE_DISABLE, 0);
		break;
	case 1:
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE1_SRC_SEL, 0,
				SYMCLK32_SE1_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE1_GATE_DISABLE, 0,
					SYMCLK32_ROOT_SE1_GATE_DISABLE, 0);
		break;
	case 2:
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE2_SRC_SEL, 0,
				SYMCLK32_SE2_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE2_GATE_DISABLE, 0,
					SYMCLK32_ROOT_SE2_GATE_DISABLE, 0);
		break;
	case 3:
		REG_UPDATE_2(SYMCLK32_SE_CNTL,
				SYMCLK32_SE3_SRC_SEL, 0,
				SYMCLK32_SE3_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_se)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE3_GATE_DISABLE, 0,
					SYMCLK32_ROOT_SE3_GATE_DISABLE, 0);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg31_enable_symclk32_le(
		struct dccg *dccg,
		int hpo_le_inst,
		enum phyd32clk_clock_source phyd32clk)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	phyd32clk = get_phy_mux_symclk(dccg_dcn, phyd32clk);

	/* select one of the PHYD32CLKs as the source for symclk32_le */
	switch (hpo_le_inst) {
	case 0:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_LE0_GATE_DISABLE, 1,
					SYMCLK32_ROOT_LE0_GATE_DISABLE, 1);
		REG_UPDATE_2(SYMCLK32_LE_CNTL,
				SYMCLK32_LE0_SRC_SEL, phyd32clk,
				SYMCLK32_LE0_EN, 1);
		break;
	case 1:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_LE1_GATE_DISABLE, 1,
					SYMCLK32_ROOT_LE1_GATE_DISABLE, 1);
		REG_UPDATE_2(SYMCLK32_LE_CNTL,
				SYMCLK32_LE1_SRC_SEL, phyd32clk,
				SYMCLK32_LE1_EN, 1);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg31_disable_symclk32_le(
		struct dccg *dccg,
		int hpo_le_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	/* set refclk as the source for symclk32_le */
	switch (hpo_le_inst) {
	case 0:
		REG_UPDATE_2(SYMCLK32_LE_CNTL,
				SYMCLK32_LE0_SRC_SEL, 0,
				SYMCLK32_LE0_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_LE0_GATE_DISABLE, 0,
					SYMCLK32_ROOT_LE0_GATE_DISABLE, 0);
		break;
	case 1:
		REG_UPDATE_2(SYMCLK32_LE_CNTL,
				SYMCLK32_LE1_SRC_SEL, 0,
				SYMCLK32_LE1_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_LE1_GATE_DISABLE, 0,
					SYMCLK32_ROOT_LE1_GATE_DISABLE, 0);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg31_disable_dscclk(struct dccg *dccg, int inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (!dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
		return;
	//DTO must be enabled to generate a 0 Hz clock output
	switch (inst) {
	case 0:
		REG_UPDATE(DSCCLK_DTO_CTRL,
				DSCCLK0_DTO_ENABLE, 1);
		REG_UPDATE_2(DSCCLK0_DTO_PARAM,
				DSCCLK0_DTO_PHASE, 0,
				DSCCLK0_DTO_MODULO, 1);
		break;
	case 1:
		REG_UPDATE(DSCCLK_DTO_CTRL,
				DSCCLK1_DTO_ENABLE, 1);
		REG_UPDATE_2(DSCCLK1_DTO_PARAM,
				DSCCLK1_DTO_PHASE, 0,
				DSCCLK1_DTO_MODULO, 1);
		break;
	case 2:
		REG_UPDATE(DSCCLK_DTO_CTRL,
				DSCCLK2_DTO_ENABLE, 1);
		REG_UPDATE_2(DSCCLK2_DTO_PARAM,
				DSCCLK2_DTO_PHASE, 0,
				DSCCLK2_DTO_MODULO, 1);
		break;
	case 3:
		if (REG(DSCCLK3_DTO_PARAM)) {
			REG_UPDATE(DSCCLK_DTO_CTRL,
					DSCCLK3_DTO_ENABLE, 1);
			REG_UPDATE_2(DSCCLK3_DTO_PARAM,
					DSCCLK3_DTO_PHASE, 0,
					DSCCLK3_DTO_MODULO, 1);
		}
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg31_enable_dscclk(struct dccg *dccg, int inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (!dccg->ctx->dc->debug.root_clock_optimization.bits.dsc)
		return;
	//Disable DTO
	switch (inst) {
	case 0:
		REG_UPDATE_2(DSCCLK0_DTO_PARAM,
				DSCCLK0_DTO_PHASE, 0,
				DSCCLK0_DTO_MODULO, 0);
		REG_UPDATE(DSCCLK_DTO_CTRL,
				DSCCLK0_DTO_ENABLE, 0);
		break;
	case 1:
		REG_UPDATE_2(DSCCLK1_DTO_PARAM,
				DSCCLK1_DTO_PHASE, 0,
				DSCCLK1_DTO_MODULO, 0);
		REG_UPDATE(DSCCLK_DTO_CTRL,
				DSCCLK1_DTO_ENABLE, 0);
		break;
	case 2:
		REG_UPDATE_2(DSCCLK2_DTO_PARAM,
				DSCCLK2_DTO_PHASE, 0,
				DSCCLK2_DTO_MODULO, 0);
		REG_UPDATE(DSCCLK_DTO_CTRL,
				DSCCLK2_DTO_ENABLE, 0);
		break;
	case 3:
		if (REG(DSCCLK3_DTO_PARAM)) {
			REG_UPDATE(DSCCLK_DTO_CTRL,
					DSCCLK3_DTO_ENABLE, 0);
			REG_UPDATE_2(DSCCLK3_DTO_PARAM,
					DSCCLK3_DTO_PHASE, 0,
					DSCCLK3_DTO_MODULO, 0);
		}
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg31_set_physymclk(
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
					PHYASYMCLK_FORCE_EN, 1,
					PHYASYMCLK_FORCE_SRC_SEL, clk_src);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYASYMCLK_GATE_DISABLE, 1);
		} else {
			REG_UPDATE_2(PHYASYMCLK_CLOCK_CNTL,
					PHYASYMCLK_FORCE_EN, 0,
					PHYASYMCLK_FORCE_SRC_SEL, 0);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYASYMCLK_GATE_DISABLE, 0);
		}
		break;
	case 1:
		if (force_enable) {
			REG_UPDATE_2(PHYBSYMCLK_CLOCK_CNTL,
					PHYBSYMCLK_FORCE_EN, 1,
					PHYBSYMCLK_FORCE_SRC_SEL, clk_src);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYBSYMCLK_GATE_DISABLE, 1);
		} else {
			REG_UPDATE_2(PHYBSYMCLK_CLOCK_CNTL,
					PHYBSYMCLK_FORCE_EN, 0,
					PHYBSYMCLK_FORCE_SRC_SEL, 0);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYBSYMCLK_GATE_DISABLE, 0);
		}
		break;
	case 2:
		if (force_enable) {
			REG_UPDATE_2(PHYCSYMCLK_CLOCK_CNTL,
					PHYCSYMCLK_FORCE_EN, 1,
					PHYCSYMCLK_FORCE_SRC_SEL, clk_src);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYCSYMCLK_GATE_DISABLE, 1);
		} else {
			REG_UPDATE_2(PHYCSYMCLK_CLOCK_CNTL,
					PHYCSYMCLK_FORCE_EN, 0,
					PHYCSYMCLK_FORCE_SRC_SEL, 0);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYCSYMCLK_GATE_DISABLE, 0);
		}
		break;
	case 3:
		if (force_enable) {
			REG_UPDATE_2(PHYDSYMCLK_CLOCK_CNTL,
					PHYDSYMCLK_FORCE_EN, 1,
					PHYDSYMCLK_FORCE_SRC_SEL, clk_src);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYDSYMCLK_GATE_DISABLE, 1);
		} else {
			REG_UPDATE_2(PHYDSYMCLK_CLOCK_CNTL,
					PHYDSYMCLK_FORCE_EN, 0,
					PHYDSYMCLK_FORCE_SRC_SEL, 0);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYDSYMCLK_GATE_DISABLE, 0);
		}
		break;
	case 4:
		if (force_enable) {
			REG_UPDATE_2(PHYESYMCLK_CLOCK_CNTL,
					PHYESYMCLK_FORCE_EN, 1,
					PHYESYMCLK_FORCE_SRC_SEL, clk_src);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYESYMCLK_GATE_DISABLE, 1);
		} else {
			REG_UPDATE_2(PHYESYMCLK_CLOCK_CNTL,
					PHYESYMCLK_FORCE_EN, 0,
					PHYESYMCLK_FORCE_SRC_SEL, 0);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYESYMCLK_GATE_DISABLE, 0);
		}
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

/* Controls the generation of pixel valid for OTG in (OTG -> HPO case) */
void dccg31_set_dtbclk_dto(
		struct dccg *dccg,
		const struct dtbclk_dto_params *params)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	int req_dtbclk_khz = params->pixclk_khz;
	uint32_t dtbdto_div;

	/* Mode	                DTBDTO Rate       DTBCLK_DTO<x>_DIV Register
	 * ODM 4:1 combine      pixel rate/4      2
	 * ODM 2:1 combine      pixel rate/2      4
	 * non-DSC 4:2:0 mode   pixel rate/2      4
	 * DSC native 4:2:0     pixel rate/2      4
	 * DSC native 4:2:2     pixel rate/2      4
	 * Other modes          pixel rate        8
	 */
	if (params->num_odm_segments == 4) {
		dtbdto_div = 2;
		req_dtbclk_khz = params->pixclk_khz / 4;
	} else if ((params->num_odm_segments == 2) ||
			(params->timing->pixel_encoding == PIXEL_ENCODING_YCBCR420) ||
			(params->timing->flags.DSC && params->timing->pixel_encoding == PIXEL_ENCODING_YCBCR422
					&& !params->timing->dsc_cfg.ycbcr422_simple)) {
		dtbdto_div = 4;
		req_dtbclk_khz = params->pixclk_khz / 2;
	} else
		dtbdto_div = 8;

	if (params->ref_dtbclk_khz && req_dtbclk_khz) {
		uint32_t modulo, phase;

		// phase / modulo = dtbclk / dtbclk ref
		modulo = params->ref_dtbclk_khz * 1000;
		phase = div_u64((((unsigned long long)modulo * req_dtbclk_khz) + params->ref_dtbclk_khz - 1),
				params->ref_dtbclk_khz);

		REG_UPDATE(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLK_DTO_DIV[params->otg_inst], dtbdto_div);

		REG_WRITE(DTBCLK_DTO_MODULO[params->otg_inst], modulo);
		REG_WRITE(DTBCLK_DTO_PHASE[params->otg_inst], phase);

		REG_UPDATE(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLK_DTO_ENABLE[params->otg_inst], 1);

		REG_WAIT(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLKDTO_ENABLE_STATUS[params->otg_inst], 1,
				1, 100);

		/* The recommended programming sequence to enable DTBCLK DTO to generate
		 * valid pixel HPO DPSTREAM ENCODER, specifies that DTO source select should
		 * be set only after DTO is enabled
		 */
		REG_UPDATE(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				PIPE_DTO_SRC_SEL[params->otg_inst], 1);
	} else {
		REG_UPDATE_3(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLK_DTO_ENABLE[params->otg_inst], 0,
				PIPE_DTO_SRC_SEL[params->otg_inst], 0,
				DTBCLK_DTO_DIV[params->otg_inst], dtbdto_div);

		REG_WRITE(DTBCLK_DTO_MODULO[params->otg_inst], 0);
		REG_WRITE(DTBCLK_DTO_PHASE[params->otg_inst], 0);
	}
}

void dccg31_set_audio_dtbclk_dto(
		struct dccg *dccg,
		const struct dtbclk_dto_params *params)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (params->ref_dtbclk_khz && params->req_audio_dtbclk_khz) {
		uint32_t modulo, phase;

		// phase / modulo = dtbclk / dtbclk ref
		modulo = params->ref_dtbclk_khz * 1000;
		phase = div_u64((((unsigned long long)modulo * params->req_audio_dtbclk_khz) + params->ref_dtbclk_khz - 1),
			params->ref_dtbclk_khz);


		REG_WRITE(DCCG_AUDIO_DTBCLK_DTO_MODULO, modulo);
		REG_WRITE(DCCG_AUDIO_DTBCLK_DTO_PHASE, phase);

		//REG_UPDATE(DCCG_AUDIO_DTO_SOURCE,
		//		DCCG_AUDIO_DTBCLK_DTO_USE_512FBR_DTO, 1);

		REG_UPDATE(DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO_SEL, 4);  //  04 - DCCG_AUDIO_DTO_SEL_AUDIO_DTO_DTBCLK
	} else {
		REG_WRITE(DCCG_AUDIO_DTBCLK_DTO_PHASE, 0);
		REG_WRITE(DCCG_AUDIO_DTBCLK_DTO_MODULO, 0);

		REG_UPDATE(DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO_SEL, 3);  //  03 - DCCG_AUDIO_DTO_SEL_NO_AUDIO_DTO
	}
}

void dccg31_get_dccg_ref_freq(struct dccg *dccg,
		unsigned int xtalin_freq_inKhz,
		unsigned int *dccg_ref_freq_inKhz)
{
	/*
	 * Assume refclk is sourced from xtalin
	 * expect 24MHz
	 */
	*dccg_ref_freq_inKhz = xtalin_freq_inKhz;
	return;
}

void dccg31_set_dispclk_change_mode(
	struct dccg *dccg,
	enum dentist_dispclk_change_mode change_mode)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_CHG_MODE,
		   change_mode == DISPCLK_CHANGE_MODE_RAMPING ? 2 : 0);
}

void dccg31_init(struct dccg *dccg)
{
	/* Set HPO stream encoder to use refclk to avoid case where PHY is
	 * disabled and SYMCLK32 for HPO SE is sourced from PHYD32CLK which
	 * will cause DCN to hang.
	 */
	dccg31_disable_symclk32_se(dccg, 0);
	dccg31_disable_symclk32_se(dccg, 1);
	dccg31_disable_symclk32_se(dccg, 2);
	dccg31_disable_symclk32_se(dccg, 3);

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le) {
		dccg31_disable_symclk32_le(dccg, 0);
		dccg31_disable_symclk32_le(dccg, 1);
	}

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream) {
		dccg31_disable_dpstreamclk(dccg, 0);
		dccg31_disable_dpstreamclk(dccg, 1);
		dccg31_disable_dpstreamclk(dccg, 2);
		dccg31_disable_dpstreamclk(dccg, 3);
	}

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk) {
		dccg31_set_physymclk(dccg, 0, PHYSYMCLK_FORCE_SRC_SYMCLK, false);
		dccg31_set_physymclk(dccg, 1, PHYSYMCLK_FORCE_SRC_SYMCLK, false);
		dccg31_set_physymclk(dccg, 2, PHYSYMCLK_FORCE_SRC_SYMCLK, false);
		dccg31_set_physymclk(dccg, 3, PHYSYMCLK_FORCE_SRC_SYMCLK, false);
		dccg31_set_physymclk(dccg, 4, PHYSYMCLK_FORCE_SRC_SYMCLK, false);
	}
}

void dccg31_otg_add_pixel(struct dccg *dccg,
				 uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE(OTG_PIXEL_RATE_CNTL[otg_inst],
			OTG_ADD_PIXEL[otg_inst], 1);
}

void dccg31_otg_drop_pixel(struct dccg *dccg,
				  uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE(OTG_PIXEL_RATE_CNTL[otg_inst],
			OTG_DROP_PIXEL[otg_inst], 1);
}

static const struct dccg_funcs dccg31_funcs = {
	.update_dpp_dto = dccg31_update_dpp_dto,
	.get_dccg_ref_freq = dccg31_get_dccg_ref_freq,
	.dccg_init = dccg31_init,
	.set_dpstreamclk = dccg31_set_dpstreamclk,
	.enable_symclk32_se = dccg31_enable_symclk32_se,
	.disable_symclk32_se = dccg31_disable_symclk32_se,
	.enable_symclk32_le = dccg31_enable_symclk32_le,
	.disable_symclk32_le = dccg31_disable_symclk32_le,
	.set_physymclk = dccg31_set_physymclk,
	.set_dtbclk_dto = dccg31_set_dtbclk_dto,
	.set_audio_dtbclk_dto = dccg31_set_audio_dtbclk_dto,
	.set_fifo_errdet_ovr_en = dccg2_set_fifo_errdet_ovr_en,
	.otg_add_pixel = dccg31_otg_add_pixel,
	.otg_drop_pixel = dccg31_otg_drop_pixel,
	.set_dispclk_change_mode = dccg31_set_dispclk_change_mode,
	.disable_dsc = dccg31_disable_dscclk,
	.enable_dsc = dccg31_enable_dscclk,
};

struct dccg *dccg31_create(
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
	base->funcs = &dccg31_funcs;

	dccg_dcn->regs = regs;
	dccg_dcn->dccg_shift = dccg_shift;
	dccg_dcn->dccg_mask = dccg_mask;

	return &dccg_dcn->base;
}
