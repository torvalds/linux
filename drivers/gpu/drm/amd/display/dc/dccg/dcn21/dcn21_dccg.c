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
#include "dcn20/dcn20_dccg.h"
#include "dcn21_dccg.h"

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

static void dccg21_update_dpp_dto(struct dccg *dccg, int dpp_inst, int req_dppclk)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	if (dccg->ref_dppclk) {
		int ref_dppclk = dccg->ref_dppclk;
		int modulo = ref_dppclk / 10000;
		int phase;

		if (req_dppclk) {
			/*
			 * program DPP DTO phase and modulo as below
			 * phase = ceiling(dpp_pipe_clk_mhz / 10)
			 * module = trunc(dpp_global_clk_mhz / 10)
			 *
			 * storing frequencies in registers allow dmcub fw
			 * to run time lower clocks when possible for power saving
			 *
			 * ceiling phase and truncate modulo guarentees the divided
			 * down per pipe dpp clock has high enough frequency
			 */
			phase = (req_dppclk + 9999) / 10000;

			if (phase > modulo) {
				/* phase > modulo result in screen corruption
				 * ie phase = 30, mod = 29 for 4k@60 HDMI
				 * in these case we don't want pipe clock to be divided
				 */
				phase = modulo;
			}
		} else {
			/*
			 *  set phase to 10 if dpp isn't used to
			 *  prevent hard hang if access dpp register
			 *  on unused pipe
			 *
			 *  DTO should be on to divide down un-used
			 *  pipe clock for power saving
			 */
			phase = 10;
		}

		REG_SET_2(DPPCLK_DTO_PARAM[dpp_inst], 0,
				DPPCLK0_DTO_PHASE, phase,
				DPPCLK0_DTO_MODULO, modulo);

		REG_UPDATE(DPPCLK_DTO_CTRL,
				DPPCLK_DTO_ENABLE[dpp_inst], 1);
	}

	dccg->pipe_dppclk_khz[dpp_inst] = req_dppclk;
}


static const struct dccg_funcs dccg21_funcs = {
	.update_dpp_dto = dccg21_update_dpp_dto,
	.get_dccg_ref_freq = dccg2_get_dccg_ref_freq,
	.set_fifo_errdet_ovr_en = dccg2_set_fifo_errdet_ovr_en,
	.otg_add_pixel = dccg2_otg_add_pixel,
	.otg_drop_pixel = dccg2_otg_drop_pixel,
	.dccg_init = dccg2_init
};

struct dccg *dccg21_create(
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
	base->funcs = &dccg21_funcs;

	dccg_dcn->regs = regs;
	dccg_dcn->dccg_shift = dccg_shift;
	dccg_dcn->dccg_mask = dccg_mask;

	return &dccg_dcn->base;
}
