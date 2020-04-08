/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
#include "dc_dmub_srv.h"
#include "panel_cntl.h"
#include "dce_panel_cntl.h"

#define TO_DCE_PANEL_CNTL(panel_cntl)\
	container_of(panel_cntl, struct dce_panel_cntl, base)

#define CTX \
	dce_panel_cntl->base.ctx

#define DC_LOGGER \
	dce_panel_cntl->base.ctx->logger

#define REG(reg)\
	dce_panel_cntl->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	dce_panel_cntl->shift->field_name, dce_panel_cntl->mask->field_name

void dce_panel_cntl_hw_init(struct panel_cntl *panel_cntl)
{

}

bool dce_is_panel_backlight_on(struct panel_cntl *panel_cntl)
{
	struct dce_panel_cntl *dce_panel_cntl = TO_DCE_PANEL_CNTL(panel_cntl);
	uint32_t value;

	REG_GET(PWRSEQ_CNTL, BLON, &value);

	return value;
}

bool dce_is_panel_powered_on(struct panel_cntl *panel_cntl)
{
	struct dce_panel_cntl *dce_panel_cntl = TO_DCE_PANEL_CNTL(panel_cntl);
	uint32_t pwr_seq_state, dig_on, dig_on_ovrd;

	REG_GET(PWRSEQ_STATE, PWRSEQ_TARGET_STATE_R, &pwr_seq_state);

	REG_GET_2(PWRSEQ_CNTL, DIGON, &dig_on, DIGON_OVRD, &dig_on_ovrd);

	return (pwr_seq_state == 1) || (dig_on == 1 && dig_on_ovrd == 1);
}

static void dce_panel_cntl_destroy(struct panel_cntl **panel_cntl)
{
	struct dce_panel_cntl *dce_panel_cntl = TO_DCE_PANEL_CNTL(*panel_cntl);

	kfree(dce_panel_cntl);
	*panel_cntl = NULL;
}

static const struct panel_cntl_funcs dce_link_panel_cntl_funcs = {
	.destroy = dce_panel_cntl_destroy,
	.hw_init = dce_panel_cntl_hw_init,
	.is_panel_backlight_on = dce_is_panel_backlight_on,
	.is_panel_powered_on = dce_is_panel_powered_on,

};

void dce_panel_cntl_construct(
	struct dce_panel_cntl *dce_panel_cntl,
	const struct panel_cntl_init_data *init_data,
	const struct dce_panel_cntl_registers *regs,
	const struct dce_panel_cntl_shift *shift,
	const struct dce_panel_cntl_mask *mask)
{
	dce_panel_cntl->regs = regs;
	dce_panel_cntl->shift = shift;
	dce_panel_cntl->mask = mask;

	dce_panel_cntl->base.funcs = &dce_link_panel_cntl_funcs;
	dce_panel_cntl->base.ctx = init_data->ctx;
	dce_panel_cntl->base.inst = init_data->inst;
}
