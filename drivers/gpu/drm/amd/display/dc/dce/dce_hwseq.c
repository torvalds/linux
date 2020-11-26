/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include "dce_hwseq.h"
#include "reg_helper.h"
#include "hw_sequencer_private.h"
#include "core_types.h"

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

void dce_enable_fe_clock(struct dce_hwseq *hws,
		unsigned int fe_inst, bool enable)
{
	REG_UPDATE(DCFE_CLOCK_CONTROL[fe_inst],
			DCFE_CLOCK_ENABLE, enable);
}

void dce_pipe_control_lock(struct dc *dc,
		struct pipe_ctx *pipe,
		bool lock)
{
	uint32_t lock_val = lock ? 1 : 0;
	uint32_t dcp_grph, scl, blnd, update_lock_mode, val;
	struct dce_hwseq *hws = dc->hwseq;

	/* Not lock pipe when blank */
	if (lock && pipe->stream_res.tg->funcs->is_blanked &&
	    pipe->stream_res.tg->funcs->is_blanked(pipe->stream_res.tg))
		return;

	val = REG_GET_4(BLND_V_UPDATE_LOCK[pipe->stream_res.tg->inst],
			BLND_DCP_GRPH_V_UPDATE_LOCK, &dcp_grph,
			BLND_SCL_V_UPDATE_LOCK, &scl,
			BLND_BLND_V_UPDATE_LOCK, &blnd,
			BLND_V_UPDATE_LOCK_MODE, &update_lock_mode);

	dcp_grph = lock_val;
	scl = lock_val;
	blnd = lock_val;
	update_lock_mode = lock_val;

	REG_SET_2(BLND_V_UPDATE_LOCK[pipe->stream_res.tg->inst], val,
			BLND_DCP_GRPH_V_UPDATE_LOCK, dcp_grph,
			BLND_SCL_V_UPDATE_LOCK, scl);

	if (hws->masks->BLND_BLND_V_UPDATE_LOCK != 0)
		REG_SET_2(BLND_V_UPDATE_LOCK[pipe->stream_res.tg->inst], val,
				BLND_BLND_V_UPDATE_LOCK, blnd,
				BLND_V_UPDATE_LOCK_MODE, update_lock_mode);

	if (hws->wa.blnd_crtc_trigger) {
		if (!lock) {
			uint32_t value = REG_READ(CRTC_H_BLANK_START_END[pipe->stream_res.tg->inst]);
			REG_WRITE(CRTC_H_BLANK_START_END[pipe->stream_res.tg->inst], value);
		}
	}
}

#if defined(CONFIG_DRM_AMD_DC_SI)
void dce60_pipe_control_lock(struct dc *dc,
		struct pipe_ctx *pipe,
		bool lock)
{
	/* DCE6 has no BLND_V_UPDATE_LOCK register */
}
#endif

void dce_set_blender_mode(struct dce_hwseq *hws,
	unsigned int blnd_inst,
	enum blnd_mode mode)
{
	uint32_t feedthrough = 1;
	uint32_t blnd_mode = 0;
	uint32_t multiplied_mode = 0;
	uint32_t alpha_mode = 2;

	switch (mode) {
	case BLND_MODE_OTHER_PIPE:
		feedthrough = 0;
		blnd_mode = 1;
		alpha_mode = 0;
		break;
	case BLND_MODE_BLENDING:
		feedthrough = 0;
		blnd_mode = 2;
		alpha_mode = 0;
		multiplied_mode = 1;
		break;
	case BLND_MODE_CURRENT_PIPE:
	default:
		if (REG(BLND_CONTROL[blnd_inst]) == REG(BLNDV_CONTROL) ||
				blnd_inst == 0)
			feedthrough = 0;
		break;
	}

	REG_UPDATE(BLND_CONTROL[blnd_inst],
		BLND_MODE, blnd_mode);

	if (hws->masks->BLND_ALPHA_MODE != 0) {
		REG_UPDATE_3(BLND_CONTROL[blnd_inst],
			BLND_FEEDTHROUGH_EN, feedthrough,
			BLND_ALPHA_MODE, alpha_mode,
			BLND_MULTIPLIED_MODE, multiplied_mode);
	}
}


static void dce_disable_sram_shut_down(struct dce_hwseq *hws)
{
	if (REG(DC_MEM_GLOBAL_PWR_REQ_CNTL))
		REG_UPDATE(DC_MEM_GLOBAL_PWR_REQ_CNTL,
				DC_MEM_GLOBAL_PWR_REQ_DIS, 1);
}

static void dce_underlay_clock_enable(struct dce_hwseq *hws)
{
	/* todo: why do we need this at boot? is dce_enable_fe_clock enough? */
	if (REG(DCFEV_CLOCK_CONTROL))
		REG_UPDATE(DCFEV_CLOCK_CONTROL,
				DCFEV_CLOCK_ENABLE, 1);
}

static void enable_hw_base_light_sleep(void)
{
	/* TODO: implement */
}

static void disable_sw_manual_control_light_sleep(void)
{
	/* TODO: implement */
}

void dce_clock_gating_power_up(struct dce_hwseq *hws,
		bool enable)
{
	if (enable) {
		enable_hw_base_light_sleep();
		disable_sw_manual_control_light_sleep();
	} else {
		dce_disable_sram_shut_down(hws);
		dce_underlay_clock_enable(hws);
	}
}

void dce_crtc_switch_to_clk_src(struct dce_hwseq *hws,
		struct clock_source *clk_src,
		unsigned int tg_inst)
{
	if (clk_src->id == CLOCK_SOURCE_ID_DP_DTO || clk_src->dp_clk_src) {
		REG_UPDATE(PIXEL_RATE_CNTL[tg_inst],
				DP_DTO0_ENABLE, 1);

	} else if (clk_src->id >= CLOCK_SOURCE_COMBO_PHY_PLL0) {
		uint32_t rate_source = clk_src->id - CLOCK_SOURCE_COMBO_PHY_PLL0;

		REG_UPDATE_2(PHYPLL_PIXEL_RATE_CNTL[tg_inst],
				PHYPLL_PIXEL_RATE_SOURCE, rate_source,
				PIXEL_RATE_PLL_SOURCE, 0);

		REG_UPDATE(PIXEL_RATE_CNTL[tg_inst],
				DP_DTO0_ENABLE, 0);

	} else if (clk_src->id <= CLOCK_SOURCE_ID_PLL2) {
		uint32_t rate_source = clk_src->id - CLOCK_SOURCE_ID_PLL0;

		REG_UPDATE_2(PIXEL_RATE_CNTL[tg_inst],
				PIXEL_RATE_SOURCE, rate_source,
				DP_DTO0_ENABLE, 0);

		if (REG(PHYPLL_PIXEL_RATE_CNTL[tg_inst]))
			REG_UPDATE(PHYPLL_PIXEL_RATE_CNTL[tg_inst],
					PIXEL_RATE_PLL_SOURCE, 1);
	} else {
		DC_ERR("Unknown clock source. clk_src id: %d, TG_inst: %d",
		       clk_src->id, tg_inst);
	}
}

/* Only use LUT for 8 bit formats */
bool dce_use_lut(enum surface_pixel_format format)
{
	switch (format) {
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
		return true;
	default:
		return false;
	}
}
