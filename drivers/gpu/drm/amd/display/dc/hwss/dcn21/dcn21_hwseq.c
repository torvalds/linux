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

#include "dm_services.h"
#include "dm_helpers.h"
#include "core_types.h"
#include "resource.h"
#include "dce/dce_hwseq.h"
#include "dce110/dce110_hwseq.h"
#include "dcn21_hwseq.h"
#include "vmid.h"
#include "reg_helper.h"
#include "hw/clk_mgr.h"
#include "dc_dmub_srv.h"
#include "abm.h"
#include "link.h"

#define DC_LOGGER_INIT(logger)

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

/* Temporary read settings, future will get values from kmd directly */
static void mmhub_update_page_table_config(struct dcn_hubbub_phys_addr_config *config,
		struct dce_hwseq *hws)
{
	uint32_t page_table_base_hi;
	uint32_t page_table_base_lo;

	REG_GET(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
			PAGE_DIRECTORY_ENTRY_HI32, &page_table_base_hi);
	REG_GET(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
			PAGE_DIRECTORY_ENTRY_LO32, &page_table_base_lo);

	config->gart_config.page_table_base_addr = ((uint64_t)page_table_base_hi << 32) | page_table_base_lo;

}

int dcn21_init_sys_ctx(struct dce_hwseq *hws, struct dc *dc, struct dc_phy_addr_space_config *pa_config)
{
	struct dcn_hubbub_phys_addr_config config = {0};

	config.system_aperture.fb_top = pa_config->system_aperture.fb_top;
	config.system_aperture.fb_offset = pa_config->system_aperture.fb_offset;
	config.system_aperture.fb_base = pa_config->system_aperture.fb_base;
	config.system_aperture.agp_top = pa_config->system_aperture.agp_top;
	config.system_aperture.agp_bot = pa_config->system_aperture.agp_bot;
	config.system_aperture.agp_base = pa_config->system_aperture.agp_base;
	config.gart_config.page_table_start_addr = pa_config->gart_config.page_table_start_addr;
	config.gart_config.page_table_end_addr = pa_config->gart_config.page_table_end_addr;
	config.gart_config.page_table_base_addr = pa_config->gart_config.page_table_base_addr;

	mmhub_update_page_table_config(&config, hws);

	return dc->res_pool->hubbub->funcs->init_dchub_sys_ctx(dc->res_pool->hubbub, &config);
}

// work around for Renoir s0i3, if register is programmed, bypass golden init.

bool dcn21_s0i3_golden_init_wa(struct dc *dc)
{
	struct dce_hwseq *hws = dc->hwseq;
	uint32_t value = 0;

	value = REG_READ(MICROSECOND_TIME_BASE_DIV);

	return value != 0x00120464;
}

void dcn21_exit_optimized_pwr_state(
		const struct dc *dc,
		struct dc_state *context)
{
	dc->clk_mgr->funcs->update_clocks(
			dc->clk_mgr,
			context,
			false);
}

void dcn21_optimize_pwr_state(
		const struct dc *dc,
		struct dc_state *context)
{
	dc->clk_mgr->funcs->update_clocks(
			dc->clk_mgr,
			context,
			true);
}

/* If user hotplug a HDMI monitor while in monitor off,
 * OS will do a mode set (with output timing) but keep output off.
 * In this case DAL will ask vbios to power up the pll in the PHY.
 * If user unplug the monitor (while we are on monitor off) or
 * system attempt to enter modern standby (which we will disable PLL),
 * PHY will hang on the next mode set attempt.
 * if enable PLL follow by disable PLL (without executing lane enable/disable),
 * RDPCS_PHY_DP_MPLLB_STATE remains 1,
 * which indicate that PLL disable attempt actually didn't go through.
 * As a workaround, insert PHY lane enable/disable before PLL disable.
 */
void dcn21_PLAT_58856_wa(struct dc_state *context, struct pipe_ctx *pipe_ctx)
{
	if (!pipe_ctx->stream->dpms_off)
		return;

	pipe_ctx->stream->dpms_off = false;
	pipe_ctx->stream->ctx->dc->link_srv->set_dpms_on(context, pipe_ctx);
	pipe_ctx->stream->ctx->dc->link_srv->set_dpms_off(pipe_ctx);
	pipe_ctx->stream->dpms_off = true;
}

bool dcn21_dmub_abm_set_pipe(struct abm *abm, uint32_t otg_inst,
		uint32_t option, uint32_t panel_inst, uint32_t pwrseq_inst)
{
	union dmub_rb_cmd cmd;
	struct dc_context *dc = abm->ctx;
	uint8_t ramping_boundary = 0xFF;

	memset(&cmd, 0, sizeof(cmd));
	cmd.abm_set_pipe.header.type = DMUB_CMD__ABM;
	cmd.abm_set_pipe.header.sub_type = DMUB_CMD__ABM_SET_PIPE;
	cmd.abm_set_pipe.abm_set_pipe_data.otg_inst = otg_inst;
	cmd.abm_set_pipe.abm_set_pipe_data.pwrseq_inst = pwrseq_inst;
	cmd.abm_set_pipe.abm_set_pipe_data.set_pipe_option = option;
	cmd.abm_set_pipe.abm_set_pipe_data.panel_inst = panel_inst;
	cmd.abm_set_pipe.abm_set_pipe_data.ramping_boundary = ramping_boundary;
	cmd.abm_set_pipe.header.payload_bytes = sizeof(struct dmub_cmd_abm_set_pipe_data);

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);

	return true;
}

static void dmub_abm_set_backlight(struct dc_context *dc, uint32_t backlight_pwm_u16_16,
									uint32_t frame_ramp, uint32_t panel_inst)
{
	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.abm_set_backlight.header.type = DMUB_CMD__ABM;
	cmd.abm_set_backlight.header.sub_type = DMUB_CMD__ABM_SET_BACKLIGHT;
	cmd.abm_set_backlight.abm_set_backlight_data.frame_ramp = frame_ramp;
	cmd.abm_set_backlight.abm_set_backlight_data.backlight_user_level = backlight_pwm_u16_16;
	cmd.abm_set_backlight.abm_set_backlight_data.version = DMUB_CMD_ABM_CONTROL_VERSION_1;
	cmd.abm_set_backlight.abm_set_backlight_data.panel_mask = (0x01 << panel_inst);
	cmd.abm_set_backlight.header.payload_bytes = sizeof(struct dmub_cmd_abm_set_backlight_data);

	dc_wake_and_execute_dmub_cmd(dc, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

void dcn21_set_abm_immediate_disable(struct pipe_ctx *pipe_ctx)
{
	struct abm *abm = pipe_ctx->stream_res.abm;
	uint32_t otg_inst = pipe_ctx->stream_res.tg->inst;
	struct panel_cntl *panel_cntl = pipe_ctx->stream->link->panel_cntl;
	struct dmcu *dmcu = pipe_ctx->stream->ctx->dc->res_pool->dmcu;

	// make a short term w/a for an issue that backlight ramping unexpectedly paused in the middle,
	// will decouple backlight from ABM and redefine DMUB interface, then this w/a could be removed
	if (pipe_ctx->stream->abm_level == 0 || pipe_ctx->stream->abm_level == ABM_LEVEL_IMMEDIATE_DISABLE) {
		return;
	}

	if (dmcu) {
		dce110_set_abm_immediate_disable(pipe_ctx);
		return;
	}

	if (abm && panel_cntl) {
		if (abm->funcs && abm->funcs->set_pipe_ex) {
			abm->funcs->set_pipe_ex(abm, otg_inst, SET_ABM_PIPE_IMMEDIATELY_DISABLE,
					panel_cntl->inst, panel_cntl->pwrseq_inst);
		} else {
			dcn21_dmub_abm_set_pipe(abm,
						otg_inst,
						SET_ABM_PIPE_IMMEDIATELY_DISABLE,
						panel_cntl->inst,
						panel_cntl->pwrseq_inst);
		}
		panel_cntl->funcs->store_backlight_level(panel_cntl);
	}
}

void dcn21_set_pipe(struct pipe_ctx *pipe_ctx)
{
	struct abm *abm = pipe_ctx->stream_res.abm;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;
	struct panel_cntl *panel_cntl = pipe_ctx->stream->link->panel_cntl;
	struct dmcu *dmcu = pipe_ctx->stream->ctx->dc->res_pool->dmcu;
	uint32_t otg_inst;

	if (!abm || !tg || !panel_cntl)
		return;

	otg_inst = tg->inst;

	if (dmcu) {
		dce110_set_pipe(pipe_ctx);
		return;
	}

	if (abm->funcs && abm->funcs->set_pipe_ex) {
		abm->funcs->set_pipe_ex(abm,
					otg_inst,
					SET_ABM_PIPE_NORMAL,
					panel_cntl->inst,
					panel_cntl->pwrseq_inst);
	} else {
			dcn21_dmub_abm_set_pipe(abm, otg_inst,
				  SET_ABM_PIPE_NORMAL,
				  panel_cntl->inst,
				  panel_cntl->pwrseq_inst);
	}
}

bool dcn21_set_backlight_level(struct pipe_ctx *pipe_ctx,
	struct set_backlight_level_params *backlight_level_params)
{
	struct dc_context *dc = pipe_ctx->stream->ctx;
	struct abm *abm = pipe_ctx->stream_res.abm;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;
	struct panel_cntl *panel_cntl = pipe_ctx->stream->link->panel_cntl;
	uint32_t otg_inst;
	uint32_t backlight_pwm_u16_16 = backlight_level_params->backlight_pwm_u16_16;
	uint32_t frame_ramp = backlight_level_params->frame_ramp;

	if (!abm || !tg || !panel_cntl)
		return false;

	otg_inst = tg->inst;

	if (dc->dc->res_pool->dmcu) {
		dce110_set_backlight_level(pipe_ctx, backlight_level_params);
		return true;
	}

	if (abm->funcs && abm->funcs->set_pipe_ex) {
		abm->funcs->set_pipe_ex(abm,
					otg_inst,
					SET_ABM_PIPE_NORMAL,
					panel_cntl->inst,
					panel_cntl->pwrseq_inst);
	} else {
			dcn21_dmub_abm_set_pipe(abm,
				  otg_inst,
				  SET_ABM_PIPE_NORMAL,
				  panel_cntl->inst,
				  panel_cntl->pwrseq_inst);
	}

	if (abm->funcs && abm->funcs->set_backlight_level_pwm)
		abm->funcs->set_backlight_level_pwm(abm, backlight_pwm_u16_16,
			frame_ramp, 0, panel_cntl->inst);
	else
		dmub_abm_set_backlight(dc, backlight_pwm_u16_16, frame_ramp, panel_cntl->inst);

	return true;
}

bool dcn21_is_abm_supported(struct dc *dc,
		struct dc_state *context, struct dc_stream_state *stream)
{
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == stream &&
				(pipe_ctx->prev_odm_pipe == NULL && pipe_ctx->next_odm_pipe == NULL))
			return true;
	}
	return false;
}

