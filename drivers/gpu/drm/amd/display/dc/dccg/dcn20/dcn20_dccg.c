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

#include <linux/slab.h>

#include "reg_helper.h"
#include "core_types.h"
#include "dcn20_dccg.h"

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

void dccg2_update_dpp_dto(struct dccg *dccg, int dpp_inst, int req_dppclk)
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
		REG_UPDATE(DPPCLK_DTO_CTRL,
				DPPCLK_DTO_ENABLE[dpp_inst], 1);
	} else {
		REG_UPDATE(DPPCLK_DTO_CTRL,
				DPPCLK_DTO_ENABLE[dpp_inst], 0);
	}

	dccg->pipe_dppclk_khz[dpp_inst] = req_dppclk;
}

void dccg2_get_dccg_ref_freq(struct dccg *dccg,
		unsigned int xtalin_freq_inKhz,
		unsigned int *dccg_ref_freq_inKhz)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t clk_en = 0;
	uint32_t clk_sel = 0;

	REG_GET_2(REFCLK_CNTL, REFCLK_CLOCK_EN, &clk_en, REFCLK_SRC_SEL, &clk_sel);

	if (clk_en != 0) {
		// DCN20 has never been validated for non-xtalin as reference
		// frequency.  There's actually no way for DC to determine what
		// frequency a non-xtalin source is.
		ASSERT_CRITICAL(false);
	}

	*dccg_ref_freq_inKhz = xtalin_freq_inKhz;

	return;
}

void dccg2_set_fifo_errdet_ovr_en(struct dccg *dccg,
		bool en)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE(DISPCLK_FREQ_CHANGE_CNTL,
			DCCG_FIFO_ERRDET_OVR_EN, en ? 1 : 0);
}

void dccg2_otg_add_pixel(struct dccg *dccg,
		uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE_2(OTG_PIXEL_RATE_CNTL[otg_inst],
			OTG_ADD_PIXEL[otg_inst], 0,
			OTG_DROP_PIXEL[otg_inst], 0);
	REG_UPDATE(OTG_PIXEL_RATE_CNTL[otg_inst],
			OTG_ADD_PIXEL[otg_inst], 1);
}

void dccg2_otg_drop_pixel(struct dccg *dccg,
		uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE_2(OTG_PIXEL_RATE_CNTL[otg_inst],
			OTG_ADD_PIXEL[otg_inst], 0,
			OTG_DROP_PIXEL[otg_inst], 0);
	REG_UPDATE(OTG_PIXEL_RATE_CNTL[otg_inst],
			OTG_DROP_PIXEL[otg_inst], 1);
}

void dccg2_init(struct dccg *dccg)
{
}

static const struct dccg_funcs dccg2_funcs = {
	.update_dpp_dto = dccg2_update_dpp_dto,
	.get_dccg_ref_freq = dccg2_get_dccg_ref_freq,
	.set_fifo_errdet_ovr_en = dccg2_set_fifo_errdet_ovr_en,
	.otg_add_pixel = dccg2_otg_add_pixel,
	.otg_drop_pixel = dccg2_otg_drop_pixel,
	.dccg_init = dccg2_init
};

struct dccg *dccg2_create(
	struct dc_context *ctx,
	const struct dccg_registers *regs,
	const struct dccg_shift *dccg_shift,
	const struct dccg_mask *dccg_mask)
{
	struct dcn_dccg *dccg_dcn = kzalloc(sizeof(*dccg_dcn), GFP_ATOMIC);
	struct dccg *base;

	if (dccg_dcn == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	base = &dccg_dcn->base;
	base->ctx = ctx;
	base->funcs = &dccg2_funcs;

	dccg_dcn->regs = regs;
	dccg_dcn->dccg_shift = dccg_shift;
	dccg_dcn->dccg_mask = dccg_mask;

	return &dccg_dcn->base;
}

void dcn_dccg_destroy(struct dccg **dccg)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(*dccg);

	kfree(dccg_dcn);
	*dccg = NULL;
}
