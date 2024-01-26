/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include "dmub_abm.h"
#include "dmub_abm_lcd.h"
#include "dc.h"
#include "core_types.h"
#include "dmub_cmd.h"

#define TO_DMUB_ABM(abm)\
	container_of(abm, struct dce_abm, base)

#define ABM_FEATURE_NO_SUPPORT	0
#define ABM_LCD_SUPPORT			1

static unsigned int abm_feature_support(struct abm *abm, unsigned int panel_inst)
{
	struct dc_context *dc = abm->ctx;
	struct dc_link *edp_links[MAX_NUM_EDP];
	int i;
	int edp_num;
	unsigned int ret = ABM_FEATURE_NO_SUPPORT;

	dc_get_edp_links(dc->dc, edp_links, &edp_num);

	for (i = 0; i < edp_num; i++) {
		if (panel_inst == i)
			break;
	}

	if (i < edp_num) {
		ret = ABM_LCD_SUPPORT;
	}

	return ret;
}

static void dmub_abm_init_ex(struct abm *abm, uint32_t backlight, uint32_t user_level)
{
	dmub_abm_init(abm, backlight, user_level);
}

static unsigned int dmub_abm_get_current_backlight_ex(struct abm *abm)
{
	dc_allow_idle_optimizations(abm->ctx->dc, false);

	return dmub_abm_get_current_backlight(abm);
}

static unsigned int dmub_abm_get_target_backlight_ex(struct abm *abm)
{
	dc_allow_idle_optimizations(abm->ctx->dc, false);

	return dmub_abm_get_target_backlight(abm);
}

static bool dmub_abm_set_level_ex(struct abm *abm, uint32_t level)
{
	bool ret = false;
	unsigned int feature_support, i;
	uint8_t panel_mask0 = 0;

	for (i = 0; i < MAX_NUM_EDP; i++) {
		feature_support = abm_feature_support(abm, i);

		if (feature_support == ABM_LCD_SUPPORT)
			panel_mask0 |= (0x01 << i);
	}

	if (panel_mask0)
		ret = dmub_abm_set_level(abm, level, panel_mask0);

	return ret;
}

static bool dmub_abm_init_config_ex(struct abm *abm,
	const char *src,
	unsigned int bytes,
	unsigned int inst)
{
	unsigned int feature_support;

	feature_support = abm_feature_support(abm, inst);

	if (feature_support == ABM_LCD_SUPPORT)
		dmub_abm_init_config(abm, src, bytes, inst);

	return true;
}

static bool dmub_abm_set_pause_ex(struct abm *abm, bool pause, unsigned int panel_inst, unsigned int stream_inst)
{
	bool ret = false;
	unsigned int feature_support;

	feature_support = abm_feature_support(abm, panel_inst);

	if (feature_support == ABM_LCD_SUPPORT)
		ret = dmub_abm_set_pause(abm, pause, panel_inst, stream_inst);

	return ret;
}

/*****************************************************************************
 *  dmub_abm_save_restore_ex() - calls dmub_abm_save_restore for preserving DMUB's
 *                              Varibright states for LCD only. OLED is TBD
 *  @abm: used to check get dc context
 *  @panel_inst: panel instance index
 *  @pData: contains command to pause/un-pause abm and abm parameters
 *
 *
 ***************************************************************************/
static bool dmub_abm_save_restore_ex(
		struct abm *abm,
		unsigned int panel_inst,
		struct abm_save_restore *pData)
{
	bool ret = false;
	unsigned int feature_support;
	struct dc_context *dc = abm->ctx;

	feature_support = abm_feature_support(abm, panel_inst);

	if (feature_support == ABM_LCD_SUPPORT)
		ret = dmub_abm_save_restore(dc, panel_inst, pData);

	return ret;
}

static bool dmub_abm_set_pipe_ex(struct abm *abm,
		uint32_t otg_inst,
		uint32_t option,
		uint32_t panel_inst,
		uint32_t pwrseq_inst)
{
	bool ret = false;
	unsigned int feature_support;

	feature_support = abm_feature_support(abm, panel_inst);

	if (feature_support == ABM_LCD_SUPPORT)
		ret = dmub_abm_set_pipe(abm, otg_inst, option, panel_inst, pwrseq_inst);

	return ret;
}

static bool dmub_abm_set_backlight_level_pwm_ex(struct abm *abm,
		unsigned int backlight_pwm_u16_16,
		unsigned int frame_ramp,
		unsigned int controller_id,
		unsigned int panel_inst)
{
	bool ret = false;
	unsigned int feature_support;

	feature_support = abm_feature_support(abm, panel_inst);

	if (feature_support == ABM_LCD_SUPPORT)
		ret = dmub_abm_set_backlight_level(abm, backlight_pwm_u16_16, frame_ramp, panel_inst);

	return ret;
}

static const struct abm_funcs abm_funcs = {
	.abm_init = dmub_abm_init_ex,
	.set_abm_level = dmub_abm_set_level_ex,
	.get_current_backlight = dmub_abm_get_current_backlight_ex,
	.get_target_backlight = dmub_abm_get_target_backlight_ex,
	.init_abm_config = dmub_abm_init_config_ex,
	.set_abm_pause = dmub_abm_set_pause_ex,
	.save_restore = dmub_abm_save_restore_ex,
	.set_pipe_ex = dmub_abm_set_pipe_ex,
	.set_backlight_level_pwm = dmub_abm_set_backlight_level_pwm_ex,
};

static void dmub_abm_construct(
	struct dce_abm *abm_dce,
	struct dc_context *ctx,
	const struct dce_abm_registers *regs,
	const struct dce_abm_shift *abm_shift,
	const struct dce_abm_mask *abm_mask)
{
	struct abm *base = &abm_dce->base;

	base->ctx = ctx;
	base->funcs = &abm_funcs;
	base->dmcu_is_running = false;

	abm_dce->regs = regs;
	abm_dce->abm_shift = abm_shift;
	abm_dce->abm_mask = abm_mask;
}

struct abm *dmub_abm_create(
	struct dc_context *ctx,
	const struct dce_abm_registers *regs,
	const struct dce_abm_shift *abm_shift,
	const struct dce_abm_mask *abm_mask)
{
	if (ctx->dc->caps.dmcub_support) {
		struct dce_abm *abm_dce = kzalloc(sizeof(*abm_dce), GFP_KERNEL);

		if (abm_dce == NULL) {
			BREAK_TO_DEBUGGER();
			return NULL;
		}

		dmub_abm_construct(abm_dce, ctx, regs, abm_shift, abm_mask);

		return &abm_dce->base;
	}
	return NULL;
}

void dmub_abm_destroy(struct abm **abm)
{
	struct dce_abm *abm_dce = TO_DMUB_ABM(*abm);

	kfree(abm_dce);
	*abm = NULL;
}
