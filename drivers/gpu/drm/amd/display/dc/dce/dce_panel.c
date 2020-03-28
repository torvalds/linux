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
#include "panel.h"
#include "dce_panel.h"

#define TO_DCE_PANEL(panel)\
	container_of(panel, struct dce_panel, base)

#define CTX \
	dce_panel->base.ctx

#define DC_LOGGER \
	dce_panel->base.ctx->logger

#define REG(reg)\
	dce_panel->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	dce_panel->shift->field_name, dce_panel->mask->field_name

void dce_panel_hw_init(struct panel *panel)
{

}

bool dce_is_panel_backlight_on(struct panel *panel)
{
	struct dce_panel *dce_panel = TO_DCE_PANEL(panel);
	uint32_t value;

	REG_GET(PWRSEQ_CNTL, BLON, &value);

	return value;
}

bool dce_is_panel_powered_on(struct panel *panel)
{
	struct dce_panel *dce_panel = TO_DCE_PANEL(panel);
	uint32_t pwr_seq_state, dig_on, dig_on_ovrd;

	REG_GET(PWRSEQ_STATE, PWRSEQ_TARGET_STATE_R, &pwr_seq_state);

	REG_GET_2(PWRSEQ_CNTL, DIGON, &dig_on, DIGON_OVRD, &dig_on_ovrd);

	return (pwr_seq_state == 1) || (dig_on == 1 && dig_on_ovrd == 1);
}

static void dce_panel_destroy(struct panel **panel)
{
	struct dce_panel *dce_panel = TO_DCE_PANEL(*panel);

	kfree(dce_panel);
	*panel = NULL;
}

static const struct panel_funcs dce_link_panel_funcs = {
	.destroy = dce_panel_destroy,
	.hw_init = dce_panel_hw_init,
	.is_panel_backlight_on = dce_is_panel_backlight_on,
	.is_panel_powered_on = dce_is_panel_powered_on,

};

void dce_panel_construct(
	struct dce_panel *dce_panel,
	const struct panel_init_data *init_data,
	const struct dce_panel_registers *regs,
	const struct dce_panel_shift *shift,
	const struct dce_panel_mask *mask)
{
	dce_panel->regs = regs;
	dce_panel->shift = shift;
	dce_panel->mask = mask;

	dce_panel->base.funcs = &dce_link_panel_funcs;
	dce_panel->base.ctx = init_data->ctx;
	dce_panel->base.inst = init_data->inst;
}
