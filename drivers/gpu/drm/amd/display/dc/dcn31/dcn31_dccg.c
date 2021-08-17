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
		if (force_enable)
			REG_UPDATE_2(PHYASYMCLK_CLOCK_CNTL,
					PHYASYMCLK_FORCE_EN, 1,
					PHYASYMCLK_FORCE_SRC_SEL, clk_src);
		else
			REG_UPDATE_2(PHYASYMCLK_CLOCK_CNTL,
					PHYASYMCLK_FORCE_EN, 0,
					PHYASYMCLK_FORCE_SRC_SEL, 0);
		break;
	case 1:
		if (force_enable)
			REG_UPDATE_2(PHYBSYMCLK_CLOCK_CNTL,
					PHYBSYMCLK_FORCE_EN, 1,
					PHYBSYMCLK_FORCE_SRC_SEL, clk_src);
		else
			REG_UPDATE_2(PHYBSYMCLK_CLOCK_CNTL,
					PHYBSYMCLK_FORCE_EN, 0,
					PHYBSYMCLK_FORCE_SRC_SEL, 0);
		break;
	case 2:
		if (force_enable)
			REG_UPDATE_2(PHYCSYMCLK_CLOCK_CNTL,
					PHYCSYMCLK_FORCE_EN, 1,
					PHYCSYMCLK_FORCE_SRC_SEL, clk_src);
		else
			REG_UPDATE_2(PHYCSYMCLK_CLOCK_CNTL,
					PHYCSYMCLK_FORCE_EN, 0,
					PHYCSYMCLK_FORCE_SRC_SEL, 0);
		break;
	case 3:
		if (force_enable)
			REG_UPDATE_2(PHYDSYMCLK_CLOCK_CNTL,
					PHYDSYMCLK_FORCE_EN, 1,
					PHYDSYMCLK_FORCE_SRC_SEL, clk_src);
		else
			REG_UPDATE_2(PHYDSYMCLK_CLOCK_CNTL,
					PHYDSYMCLK_FORCE_EN, 0,
					PHYDSYMCLK_FORCE_SRC_SEL, 0);
		break;
	case 4:
		if (force_enable)
			REG_UPDATE_2(PHYESYMCLK_CLOCK_CNTL,
					PHYESYMCLK_FORCE_EN, 1,
					PHYESYMCLK_FORCE_SRC_SEL, clk_src);
		else
			REG_UPDATE_2(PHYESYMCLK_CLOCK_CNTL,
					PHYESYMCLK_FORCE_EN, 0,
					PHYESYMCLK_FORCE_SRC_SEL, 0);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

/* Controls the generation of pixel valid for OTG in (OTG -> HPO case) */
void dccg31_set_dtbclk_dto(
		struct dccg *dccg,
		int dtbclk_inst,
		int req_dtbclk_khz,
		int num_odm_segments,
		const struct dc_crtc_timing *timing)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t dtbdto_div;

	/* Mode	                DTBDTO Rate       DTBCLK_DTO<x>_DIV Register
	 * ODM 4:1 combine      pixel rate/4      2
	 * ODM 2:1 combine      pixel rate/2      4
	 * non-DSC 4:2:0 mode   pixel rate/2      4
	 * DSC native 4:2:0     pixel rate/2      4
	 * DSC native 4:2:2     pixel rate/2      4
	 * Other modes          pixel rate        8
	 */
	if (num_odm_segments == 4) {
		dtbdto_div = 2;
		req_dtbclk_khz = req_dtbclk_khz / 4;
	} else if ((num_odm_segments == 2) ||
			(timing->pixel_encoding == PIXEL_ENCODING_YCBCR420) ||
			(timing->flags.DSC && timing->pixel_encoding == PIXEL_ENCODING_YCBCR422
					&& !timing->dsc_cfg.ycbcr422_simple)) {
		dtbdto_div = 4;
		req_dtbclk_khz = req_dtbclk_khz / 2;
	} else
		dtbdto_div = 8;

	if (dccg->ref_dtbclk_khz && req_dtbclk_khz) {
		uint32_t modulo, phase;

		// phase / modulo = dtbclk / dtbclk ref
		modulo = dccg->ref_dtbclk_khz * 1000;
		phase = div_u64((((unsigned long long)modulo * req_dtbclk_khz) + dccg->ref_dtbclk_khz - 1),
			dccg->ref_dtbclk_khz);

		REG_UPDATE(OTG_PIXEL_RATE_CNTL[dtbclk_inst],
				DTBCLK_DTO_DIV[dtbclk_inst], dtbdto_div);

		REG_WRITE(DTBCLK_DTO_MODULO[dtbclk_inst], modulo);
		REG_WRITE(DTBCLK_DTO_PHASE[dtbclk_inst], phase);

		REG_UPDATE(OTG_PIXEL_RATE_CNTL[dtbclk_inst],
				DTBCLK_DTO_ENABLE[dtbclk_inst], 1);

		REG_WAIT(OTG_PIXEL_RATE_CNTL[dtbclk_inst],
				DTBCLKDTO_ENABLE_STATUS[dtbclk_inst], 1,
				1, 100);

		/* The recommended programming sequence to enable DTBCLK DTO to generate
		 * valid pixel HPO DPSTREAM ENCODER, specifies that DTO source select should
		 * be set only after DTO is enabled
		 */
		REG_UPDATE(OTG_PIXEL_RATE_CNTL[dtbclk_inst],
				PIPE_DTO_SRC_SEL[dtbclk_inst], 1);

		dccg->dtbclk_khz[dtbclk_inst] = req_dtbclk_khz;
	} else {
		REG_UPDATE_3(OTG_PIXEL_RATE_CNTL[dtbclk_inst],
				DTBCLK_DTO_ENABLE[dtbclk_inst], 0,
				PIPE_DTO_SRC_SEL[dtbclk_inst], 0,
				DTBCLK_DTO_DIV[dtbclk_inst], dtbdto_div);

		REG_WRITE(DTBCLK_DTO_MODULO[dtbclk_inst], 0);
		REG_WRITE(DTBCLK_DTO_PHASE[dtbclk_inst], 0);

		dccg->dtbclk_khz[dtbclk_inst] = 0;
	}
}

void dccg31_set_audio_dtbclk_dto(
		struct dccg *dccg,
		uint32_t req_audio_dtbclk_khz)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (dccg->ref_dtbclk_khz && req_audio_dtbclk_khz) {
		uint32_t modulo, phase;

		// phase / modulo = dtbclk / dtbclk ref
		modulo = dccg->ref_dtbclk_khz * 1000;
		phase = div_u64((((unsigned long long)modulo * req_audio_dtbclk_khz) + dccg->ref_dtbclk_khz - 1),
			dccg->ref_dtbclk_khz);


		REG_WRITE(DCCG_AUDIO_DTBCLK_DTO_MODULO, modulo);
		REG_WRITE(DCCG_AUDIO_DTBCLK_DTO_PHASE, phase);

		//REG_UPDATE(DCCG_AUDIO_DTO_SOURCE,
		//		DCCG_AUDIO_DTBCLK_DTO_USE_512FBR_DTO, 1);

		REG_UPDATE(DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO_SEL, 4);  //  04 - DCCG_AUDIO_DTO_SEL_AUDIO_DTO_DTBCLK

		dccg->audio_dtbclk_khz = req_audio_dtbclk_khz;
	} else {
		REG_WRITE(DCCG_AUDIO_DTBCLK_DTO_PHASE, 0);
		REG_WRITE(DCCG_AUDIO_DTBCLK_DTO_MODULO, 0);

		REG_UPDATE(DCCG_AUDIO_DTO_SOURCE,
				DCCG_AUDIO_DTO_SEL, 3);  //  03 - DCCG_AUDIO_DTO_SEL_NO_AUDIO_DTO

		dccg->audio_dtbclk_khz = 0;
	}
}

static void dccg31_get_dccg_ref_freq(struct dccg *dccg,
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

static void dccg31_set_dispclk_change_mode(
	struct dccg *dccg,
	enum dentist_dispclk_change_mode change_mode)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_CHG_MODE,
		   change_mode == DISPCLK_CHANGE_MODE_RAMPING ? 2 : 0);
}

void dccg31_init(struct dccg *dccg)
{
}

static const struct dccg_funcs dccg31_funcs = {
	.update_dpp_dto = dccg2_update_dpp_dto,
	.get_dccg_ref_freq = dccg31_get_dccg_ref_freq,
	.dccg_init = dccg31_init,
	.set_physymclk = dccg31_set_physymclk,
	.set_dtbclk_dto = dccg31_set_dtbclk_dto,
	.set_audio_dtbclk_dto = dccg31_set_audio_dtbclk_dto,
	.set_dispclk_change_mode = dccg31_set_dispclk_change_mode,
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
