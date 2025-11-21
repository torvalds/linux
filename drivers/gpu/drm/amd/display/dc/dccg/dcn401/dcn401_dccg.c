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
 * Authors: AMD
 *
 */

#include "reg_helper.h"
#include "core_types.h"
#include "dcn401_dccg.h"
#include "dcn31/dcn31_dccg.h"

/*
#include "dmub_common.h"
#include "dmcub_reg_access_helper.h"

#include "dmub401_common.h"
#include "dmub401_regs.h"
#include "dmub401_dccg.h"
*/

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

static void dcn401_set_dppclk_enable(struct dccg *dccg,
				 uint32_t dpp_inst, uint32_t enable)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (dpp_inst) {
	case 0:
		REG_UPDATE(DPPCLK_CTRL, DPPCLK0_EN, enable);
		break;
	case 1:
		REG_UPDATE(DPPCLK_CTRL, DPPCLK1_EN, enable);
		break;
	case 2:
		REG_UPDATE(DPPCLK_CTRL, DPPCLK2_EN, enable);
		break;
	case 3:
		REG_UPDATE(DPPCLK_CTRL, DPPCLK3_EN, enable);
		break;
	default:
		break;
	}
}
void dccg401_update_dpp_dto(struct dccg *dccg, int dpp_inst, int req_dppclk)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

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
		dcn401_set_dppclk_enable(dccg, dpp_inst, true);
	} else {
		dcn401_set_dppclk_enable(dccg, dpp_inst, false);
	}

	dccg->pipe_dppclk_khz[dpp_inst] = req_dppclk;
}

/* This function is a workaround for writing to OTG_PIXEL_RATE_DIV
 * without the probability of causing a DIG FIFO error.
 */
static void dccg401_wait_for_dentist_change_done(
	struct dccg *dccg)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	uint32_t dentist_dispclk_value = REG_READ(DENTIST_DISPCLK_CNTL);

	REG_WRITE(DENTIST_DISPCLK_CNTL, dentist_dispclk_value);
	REG_WAIT(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_CHG_DONE, 1, 50, 2000);
}

void dccg401_get_pixel_rate_div(
		struct dccg *dccg,
		uint32_t otg_inst,
		uint32_t *tmds_div,
		uint32_t *dp_dto_int)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t val_tmds_div = PIXEL_RATE_DIV_NA;

	switch (otg_inst) {
	case 0:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG0_TMDS_PIXEL_RATE_DIV, &val_tmds_div,
			DPDTO0_INT, dp_dto_int);
		break;
	case 1:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG1_TMDS_PIXEL_RATE_DIV, &val_tmds_div,
			DPDTO1_INT, dp_dto_int);
		break;
	case 2:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG2_TMDS_PIXEL_RATE_DIV, &val_tmds_div,
			DPDTO2_INT, dp_dto_int);
		break;
	case 3:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG3_TMDS_PIXEL_RATE_DIV, &val_tmds_div,
			DPDTO3_INT, dp_dto_int);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}

	*tmds_div = val_tmds_div == 0 ? PIXEL_RATE_DIV_BY_2 : PIXEL_RATE_DIV_BY_4;
}

void dccg401_set_pixel_rate_div(
		struct dccg *dccg,
		uint32_t otg_inst,
		enum pixel_rate_div tmds_div,
		enum pixel_rate_div unused)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t cur_tmds_div = PIXEL_RATE_DIV_NA;
	uint32_t dp_dto_int;
	uint32_t reg_val;

	// only 2 and 4 are valid on dcn401
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

		dccg401_wait_for_dentist_change_done(dccg);
		break;
	case 1:
		REG_UPDATE(OTG_PIXEL_RATE_DIV,
				OTG1_TMDS_PIXEL_RATE_DIV, reg_val);

		dccg401_wait_for_dentist_change_done(dccg);
		break;
	case 2:
		REG_UPDATE(OTG_PIXEL_RATE_DIV,
				OTG2_TMDS_PIXEL_RATE_DIV, reg_val);

		dccg401_wait_for_dentist_change_done(dccg);
		break;
	case 3:
		REG_UPDATE(OTG_PIXEL_RATE_DIV,
				OTG3_TMDS_PIXEL_RATE_DIV, reg_val);

		dccg401_wait_for_dentist_change_done(dccg);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}


void dccg401_set_dtbclk_p_src(
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

void dccg401_set_physymclk(
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
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYASYMCLK_ROOT_GATE_DISABLE, 1);
		} else {
			REG_UPDATE_2(PHYASYMCLK_CLOCK_CNTL,
					PHYASYMCLK_EN, 0,
					PHYASYMCLK_SRC_SEL, 0);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYASYMCLK_ROOT_GATE_DISABLE, 0);
		}
		break;
	case 1:
		if (force_enable) {
			REG_UPDATE_2(PHYBSYMCLK_CLOCK_CNTL,
					PHYBSYMCLK_EN, 1,
					PHYBSYMCLK_SRC_SEL, clk_src);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYBSYMCLK_ROOT_GATE_DISABLE, 1);
		} else {
			REG_UPDATE_2(PHYBSYMCLK_CLOCK_CNTL,
					PHYBSYMCLK_EN, 0,
					PHYBSYMCLK_SRC_SEL, 0);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYBSYMCLK_ROOT_GATE_DISABLE, 0);
		}
		break;
	case 2:
		if (force_enable) {
			REG_UPDATE_2(PHYCSYMCLK_CLOCK_CNTL,
					PHYCSYMCLK_EN, 1,
					PHYCSYMCLK_SRC_SEL, clk_src);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYCSYMCLK_ROOT_GATE_DISABLE, 1);
		} else {
			REG_UPDATE_2(PHYCSYMCLK_CLOCK_CNTL,
					PHYCSYMCLK_EN, 0,
					PHYCSYMCLK_SRC_SEL, 0);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYCSYMCLK_ROOT_GATE_DISABLE, 0);
		}
		break;
	case 3:
		if (force_enable) {
			REG_UPDATE_2(PHYDSYMCLK_CLOCK_CNTL,
					PHYDSYMCLK_EN, 1,
					PHYDSYMCLK_SRC_SEL, clk_src);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYDSYMCLK_ROOT_GATE_DISABLE, 1);
		} else {
			REG_UPDATE_2(PHYDSYMCLK_CLOCK_CNTL,
					PHYDSYMCLK_EN, 0,
					PHYDSYMCLK_SRC_SEL, 0);
			if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk)
				REG_UPDATE(DCCG_GATE_DISABLE_CNTL2,
					PHYDSYMCLK_ROOT_GATE_DISABLE, 0);
		}
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg401_get_dccg_ref_freq(struct dccg *dccg,
		unsigned int xtalin_freq_inKhz,
		unsigned int *dccg_ref_freq_inKhz)
{
	/*
	 * Assume refclk is sourced from xtalin
	 * expect 100MHz
	 */
	*dccg_ref_freq_inKhz = xtalin_freq_inKhz;
	return;
}

static void dccg401_otg_add_pixel(struct dccg *dccg,
		uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE(OTG_PIXEL_RATE_CNTL[otg_inst],
			OTG_ADD_PIXEL[otg_inst], 1);
}

static void dccg401_otg_drop_pixel(struct dccg *dccg,
		uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE(OTG_PIXEL_RATE_CNTL[otg_inst],
			OTG_DROP_PIXEL[otg_inst], 1);
}

void dccg401_enable_symclk32_le(
		struct dccg *dccg,
		int hpo_le_inst,
		enum phyd32clk_clock_source phyd32clk)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

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
	case 2:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_LE2_GATE_DISABLE, 1,
					SYMCLK32_ROOT_LE2_GATE_DISABLE, 1);
		REG_UPDATE_2(SYMCLK32_LE_CNTL,
				SYMCLK32_LE2_SRC_SEL, phyd32clk,
				SYMCLK32_LE2_EN, 1);
		break;
	case 3:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_LE3_GATE_DISABLE, 1,
					SYMCLK32_ROOT_LE3_GATE_DISABLE, 1);
		REG_UPDATE_2(SYMCLK32_LE_CNTL,
				SYMCLK32_LE3_SRC_SEL, phyd32clk,
				SYMCLK32_LE3_EN, 1);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg401_disable_symclk32_le(
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
	case 2:
		REG_UPDATE_2(SYMCLK32_LE_CNTL,
				SYMCLK32_LE2_SRC_SEL, 0,
				SYMCLK32_LE2_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_LE2_GATE_DISABLE, 0,
					SYMCLK32_ROOT_LE2_GATE_DISABLE, 0);
		break;
	case 3:
		REG_UPDATE_2(SYMCLK32_LE_CNTL,
				SYMCLK32_LE3_SRC_SEL, 0,
				SYMCLK32_LE3_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.symclk32_le)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_LE3_GATE_DISABLE, 0,
					SYMCLK32_ROOT_LE3_GATE_DISABLE, 0);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static void dccg401_enable_dpstreamclk(struct dccg *dccg, int otg_inst, int dp_hpo_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	/* enabled to select one of the DTBCLKs for pipe */
	switch (dp_hpo_inst) {
	case 0:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL5,
					DPSTREAMCLK0_ROOT_GATE_DISABLE, 1,
					DPSTREAMCLK0_GATE_DISABLE, 1);
		REG_UPDATE_2(DPSTREAMCLK_CNTL,
				DPSTREAMCLK0_SRC_SEL, otg_inst,
				DPSTREAMCLK0_EN, 1);
		break;
	case 1:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL5,
					DPSTREAMCLK1_ROOT_GATE_DISABLE, 1,
					DPSTREAMCLK1_GATE_DISABLE, 1);
		REG_UPDATE_2(DPSTREAMCLK_CNTL,
				DPSTREAMCLK1_SRC_SEL, otg_inst,
				DPSTREAMCLK1_EN, 1);
		break;
	case 2:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL5,
					DPSTREAMCLK2_ROOT_GATE_DISABLE, 1,
					DPSTREAMCLK2_GATE_DISABLE, 1);
		REG_UPDATE_2(DPSTREAMCLK_CNTL,
				DPSTREAMCLK2_SRC_SEL, otg_inst,
				DPSTREAMCLK2_EN, 1);
		break;
	case 3:
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL5,
					DPSTREAMCLK3_ROOT_GATE_DISABLE, 1,
					DPSTREAMCLK3_GATE_DISABLE, 1);
		REG_UPDATE_2(DPSTREAMCLK_CNTL,
				DPSTREAMCLK3_SRC_SEL, otg_inst,
				DPSTREAMCLK3_EN, 1);
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

void dccg401_disable_dpstreamclk(struct dccg *dccg, int dp_hpo_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (dp_hpo_inst) {
	case 0:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK0_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL5,
					DPSTREAMCLK0_ROOT_GATE_DISABLE, 0,
					DPSTREAMCLK0_GATE_DISABLE, 0);
		break;
	case 1:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK1_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL5,
					DPSTREAMCLK1_ROOT_GATE_DISABLE, 0,
					DPSTREAMCLK1_GATE_DISABLE, 0);
		break;
	case 2:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK2_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL5,
					DPSTREAMCLK2_ROOT_GATE_DISABLE, 0,
					DPSTREAMCLK2_GATE_DISABLE, 0);
		break;
	case 3:
		REG_UPDATE(DPSTREAMCLK_CNTL,
				DPSTREAMCLK3_EN, 0);
		if (dccg->ctx->dc->debug.root_clock_optimization.bits.dpstream)
			REG_UPDATE_2(DCCG_GATE_DISABLE_CNTL5,
					DPSTREAMCLK3_ROOT_GATE_DISABLE, 0,
					DPSTREAMCLK3_GATE_DISABLE, 0);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg401_set_dpstreamclk(
		struct dccg *dccg,
		enum streamclk_source src,
		int otg_inst,
		int dp_hpo_inst)
{
	/* enabled to select one of the DTBCLKs for pipe */
	if (src == REFCLK)
		dccg401_disable_dpstreamclk(dccg, dp_hpo_inst);
	else
		dccg401_enable_dpstreamclk(dccg, otg_inst, dp_hpo_inst);
}

void dccg401_set_dp_dto(
		struct dccg *dccg,
		const struct dp_dto_params *params)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	bool enable = false;

	if (params->otg_inst > 3) {
		/* dcn401 only has 4 instances */
		BREAK_TO_DEBUGGER();
		return;
	}
	if (!params->refclk_hz) {
		BREAK_TO_DEBUGGER();
		return;
	}

	if (!dc_is_tmds_signal(params->signal)) {
		uint64_t dto_integer;
		uint64_t dto_phase_hz;
		uint64_t dto_modulo_hz = params->refclk_hz;

		enable = true;

		/* Set DTO values:
		 * int = target_pix_rate / reference_clock
		 * phase = target_pix_rate - int * reference_clock,
		 * modulo = reference_clock */
		dto_integer = div_u64(params->pixclk_hz, dto_modulo_hz);
		dto_phase_hz = params->pixclk_hz - dto_integer * dto_modulo_hz;

		if (dto_phase_hz <= 0 && dto_integer <= 0) {
			/* negative pixel rate should never happen */
			BREAK_TO_DEBUGGER();
			return;
		}

		switch (params->otg_inst) {
		case 0:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P0_GATE_DISABLE, 1);
			REG_UPDATE_4(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE0_GATE_DISABLE, 1,
					SYMCLK32_ROOT_SE0_GATE_DISABLE, 1,
					SYMCLK32_LE0_GATE_DISABLE, 1,
					SYMCLK32_ROOT_LE0_GATE_DISABLE, 1);
			break;
		case 1:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P1_GATE_DISABLE, 1);
			REG_UPDATE_4(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE1_GATE_DISABLE, 1,
					SYMCLK32_ROOT_SE1_GATE_DISABLE, 1,
					SYMCLK32_LE1_GATE_DISABLE, 1,
					SYMCLK32_ROOT_LE1_GATE_DISABLE, 1);
			break;
		case 2:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P2_GATE_DISABLE, 1);
			REG_UPDATE_4(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE2_GATE_DISABLE, 1,
					SYMCLK32_ROOT_SE2_GATE_DISABLE, 1,
					SYMCLK32_LE2_GATE_DISABLE, 1,
					SYMCLK32_ROOT_LE2_GATE_DISABLE, 1);
			break;
		case 3:
			REG_UPDATE(DCCG_GATE_DISABLE_CNTL5, DTBCLK_P3_GATE_DISABLE, 1);
			REG_UPDATE_4(DCCG_GATE_DISABLE_CNTL3,
					SYMCLK32_SE3_GATE_DISABLE, 1,
					SYMCLK32_ROOT_SE3_GATE_DISABLE, 1,
					SYMCLK32_LE3_GATE_DISABLE, 1,
					SYMCLK32_ROOT_LE3_GATE_DISABLE, 1);
			break;
		}

		dccg401_set_dtbclk_p_src(dccg, params->clk_src, params->otg_inst);

		REG_WRITE(DP_DTO_PHASE[params->otg_inst], dto_phase_hz);
		REG_WRITE(DP_DTO_MODULO[params->otg_inst], dto_modulo_hz);

		switch (params->otg_inst) {
		case 0:
			REG_UPDATE(OTG_PIXEL_RATE_DIV,
					DPDTO0_INT, dto_integer);
			break;
		case 1:
			REG_UPDATE(OTG_PIXEL_RATE_DIV,
					DPDTO1_INT, dto_integer);
			break;
		case 2:
			REG_UPDATE(OTG_PIXEL_RATE_DIV,
					DPDTO2_INT, dto_integer);
			break;
		case 3:
			REG_UPDATE(OTG_PIXEL_RATE_DIV,
					DPDTO3_INT, dto_integer);
			break;
		default:
			BREAK_TO_DEBUGGER();
			return;
		}
	}

	/* Toggle DTO */
	REG_UPDATE_2(OTG_PIXEL_RATE_CNTL[params->otg_inst],
			DP_DTO_ENABLE[params->otg_inst], enable,
			PIPE_DTO_SRC_SEL[params->otg_inst], enable);
}

void dccg401_init(struct dccg *dccg)
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

	if (dccg->ctx->dc->debug.root_clock_optimization.bits.physymclk) {
		dccg401_set_physymclk(dccg, 0, PHYSYMCLK_FORCE_SRC_SYMCLK, false);
		dccg401_set_physymclk(dccg, 1, PHYSYMCLK_FORCE_SRC_SYMCLK, false);
		dccg401_set_physymclk(dccg, 2, PHYSYMCLK_FORCE_SRC_SYMCLK, false);
		dccg401_set_physymclk(dccg, 3, PHYSYMCLK_FORCE_SRC_SYMCLK, false);
	}
}

void dccg401_set_dto_dscclk(struct dccg *dccg, uint32_t inst, uint32_t num_slices_h)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (inst) {
	case 0:
		REG_UPDATE_2(DSCCLK0_DTO_PARAM,
				DSCCLK0_DTO_PHASE, 1,
				DSCCLK0_DTO_MODULO, 1);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK0_EN, 1);

		break;
	case 1:
		REG_UPDATE_2(DSCCLK1_DTO_PARAM,
				DSCCLK1_DTO_PHASE, 1,
				DSCCLK1_DTO_MODULO, 1);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK1_EN, 1);
		break;
	case 2:
		REG_UPDATE_2(DSCCLK2_DTO_PARAM,
				DSCCLK2_DTO_PHASE, 1,
				DSCCLK2_DTO_MODULO, 1);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK2_EN, 1);
		break;
	case 3:
		REG_UPDATE_2(DSCCLK3_DTO_PARAM,
				DSCCLK3_DTO_PHASE, 1,
				DSCCLK3_DTO_MODULO, 1);
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK3_EN, 1);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dccg401_set_ref_dscclk(struct dccg *dccg,
				uint32_t dsc_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (dsc_inst) {
	case 0:
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK0_EN, 0);
		REG_UPDATE_2(DSCCLK0_DTO_PARAM,
				DSCCLK0_DTO_PHASE, 0,
				DSCCLK0_DTO_MODULO, 0);
		break;
	case 1:
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK1_EN, 0);
		REG_UPDATE_2(DSCCLK1_DTO_PARAM,
				DSCCLK1_DTO_PHASE, 0,
				DSCCLK1_DTO_MODULO, 0);
		break;
	case 2:
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK2_EN, 0);
		REG_UPDATE_2(DSCCLK2_DTO_PARAM,
				DSCCLK2_DTO_PHASE, 0,
				DSCCLK2_DTO_MODULO, 0);
		break;
	case 3:
		REG_UPDATE(DSCCLK_DTO_CTRL, DSCCLK3_EN, 0);
		REG_UPDATE_2(DSCCLK3_DTO_PARAM,
				DSCCLK3_DTO_PHASE, 0,
				DSCCLK3_DTO_MODULO, 0);
		break;
	default:
		return;
	}
}

void dccg401_enable_symclk_se(struct dccg *dccg, uint32_t stream_enc_inst, uint32_t link_enc_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

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
	}
}

void dccg401_disable_symclk_se(struct dccg *dccg, uint32_t stream_enc_inst, uint32_t link_enc_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	switch (stream_enc_inst) {
	case 0:
		REG_UPDATE_2(SYMCLKA_CLOCK_ENABLE,
				SYMCLKA_FE_EN, 0,
				SYMCLKA_FE_SRC_SEL, 0);
		break;
	case 1:
		REG_UPDATE_2(SYMCLKB_CLOCK_ENABLE,
				SYMCLKB_FE_EN, 0,
				SYMCLKB_FE_SRC_SEL, 0);
		break;
	case 2:
		REG_UPDATE_2(SYMCLKC_CLOCK_ENABLE,
				SYMCLKC_FE_EN, 0,
				SYMCLKC_FE_SRC_SEL, 0);
		break;
	case 3:
		REG_UPDATE_2(SYMCLKD_CLOCK_ENABLE,
				SYMCLKD_FE_EN, 0,
				SYMCLKD_FE_SRC_SEL, 0);
		break;
	}
}

static const struct dccg_funcs dccg401_funcs = {
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
	.set_dto_dscclk = dccg401_set_dto_dscclk,
	.set_ref_dscclk = dccg401_set_ref_dscclk,
	.set_valid_pixel_rate = NULL,
	.set_fifo_errdet_ovr_en = dccg2_set_fifo_errdet_ovr_en,
	.set_audio_dtbclk_dto = NULL,
	.otg_add_pixel = dccg401_otg_add_pixel,
	.otg_drop_pixel = dccg401_otg_drop_pixel,
	.set_pixel_rate_div = dccg401_set_pixel_rate_div,
	.get_pixel_rate_div = dccg401_get_pixel_rate_div,
	.set_dp_dto = dccg401_set_dp_dto,
	.enable_symclk_se = dccg401_enable_symclk_se,
	.disable_symclk_se = dccg401_disable_symclk_se,
	.set_dtbclk_p_src = dccg401_set_dtbclk_p_src,
};

struct dccg *dccg401_create(
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
	base->funcs = &dccg401_funcs;

	dccg_dcn->regs = regs;
	dccg_dcn->dccg_shift = dccg_shift;
	dccg_dcn->dccg_mask = dccg_mask;

	return &dccg_dcn->base;
}
