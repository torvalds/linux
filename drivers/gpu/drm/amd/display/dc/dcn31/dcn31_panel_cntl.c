/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "dcn31_panel_cntl.h"
#include "atom.h"

#define TO_DCN31_PANEL_CNTL(panel_cntl)\
	container_of(panel_cntl, struct dcn31_panel_cntl, base)

#define CTX \
	dcn31_panel_cntl->base.ctx

#define DC_LOGGER \
	dcn31_panel_cntl->base.ctx->logger

static bool dcn31_query_backlight_info(struct panel_cntl *panel_cntl, union dmub_rb_cmd *cmd)
{
	struct dcn31_panel_cntl *dcn31_panel_cntl = TO_DCN31_PANEL_CNTL(panel_cntl);
	struct dc_dmub_srv *dc_dmub_srv = panel_cntl->ctx->dmub_srv;

	if (!dc_dmub_srv)
		return false;

	memset(cmd, 0, sizeof(*cmd));
	cmd->panel_cntl.header.type = DMUB_CMD__PANEL_CNTL;
	cmd->panel_cntl.header.sub_type = DMUB_CMD__PANEL_CNTL_QUERY_BACKLIGHT_INFO;
	cmd->panel_cntl.header.payload_bytes = sizeof(cmd->panel_cntl.data);
	cmd->panel_cntl.data.pwrseq_inst = dcn31_panel_cntl->base.pwrseq_inst;

	return dc_wake_and_execute_dmub_cmd(dc_dmub_srv->ctx, cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY);
}

static uint32_t dcn31_get_16_bit_backlight_from_pwm(struct panel_cntl *panel_cntl)
{
	union dmub_rb_cmd cmd;

	if (!dcn31_query_backlight_info(panel_cntl, &cmd))
		return 0;

	return cmd.panel_cntl.data.current_backlight;
}

static uint32_t dcn31_panel_cntl_hw_init(struct panel_cntl *panel_cntl)
{
	struct dcn31_panel_cntl *dcn31_panel_cntl = TO_DCN31_PANEL_CNTL(panel_cntl);
	struct dc_dmub_srv *dc_dmub_srv = panel_cntl->ctx->dmub_srv;
	union dmub_rb_cmd cmd;
	uint32_t freq_to_set = panel_cntl->ctx->dc->debug.pwm_freq;

	if (!dc_dmub_srv)
		return 0;

	memset(&cmd, 0, sizeof(cmd));
	cmd.panel_cntl.header.type = DMUB_CMD__PANEL_CNTL;
	cmd.panel_cntl.header.sub_type = DMUB_CMD__PANEL_CNTL_HW_INIT;
	cmd.panel_cntl.header.payload_bytes = sizeof(cmd.panel_cntl.data);
	cmd.panel_cntl.data.pwrseq_inst = dcn31_panel_cntl->base.pwrseq_inst;
	cmd.panel_cntl.data.bl_pwm_cntl = panel_cntl->stored_backlight_registers.BL_PWM_CNTL;
	cmd.panel_cntl.data.bl_pwm_period_cntl = panel_cntl->stored_backlight_registers.BL_PWM_PERIOD_CNTL;
	cmd.panel_cntl.data.bl_pwm_ref_div1 =
		panel_cntl->stored_backlight_registers.LVTMA_PWRSEQ_REF_DIV_BL_PWM_REF_DIV;
	cmd.panel_cntl.data.bl_pwm_ref_div2 =
		panel_cntl->stored_backlight_registers.PANEL_PWRSEQ_REF_DIV2;
	if (!dc_wake_and_execute_dmub_cmd(dc_dmub_srv->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
		return 0;

	panel_cntl->stored_backlight_registers.BL_PWM_CNTL = cmd.panel_cntl.data.bl_pwm_cntl;
	panel_cntl->stored_backlight_registers.BL_PWM_CNTL2 = 0; /* unused */
	panel_cntl->stored_backlight_registers.BL_PWM_PERIOD_CNTL = cmd.panel_cntl.data.bl_pwm_period_cntl;
	panel_cntl->stored_backlight_registers.LVTMA_PWRSEQ_REF_DIV_BL_PWM_REF_DIV =
		cmd.panel_cntl.data.bl_pwm_ref_div1;
	panel_cntl->stored_backlight_registers.PANEL_PWRSEQ_REF_DIV2 =
		cmd.panel_cntl.data.bl_pwm_ref_div2;

	if (freq_to_set >= MIN_DEBUG_FREQ_HZ && freq_to_set <= MAX_DEBUG_FREQ_HZ) {
		uint32_t xtal = panel_cntl->ctx->dc->res_pool->ref_clocks.dccg_ref_clock_inKhz;

		memset(&cmd, 0, sizeof(cmd));
		cmd.panel_cntl.header.type = DMUB_CMD__PANEL_CNTL;
		cmd.panel_cntl.header.sub_type = DMUB_CMD__PANEL_DEBUG_PWM_FREQ;
		cmd.panel_cntl.header.payload_bytes = sizeof(cmd.panel_cntl.data);
		cmd.panel_cntl.data.pwrseq_inst = dcn31_panel_cntl->base.pwrseq_inst;
		cmd.panel_cntl.data.bl_pwm_cntl = xtal;
		cmd.panel_cntl.data.bl_pwm_period_cntl = freq_to_set;
		if (!dc_wake_and_execute_dmub_cmd(dc_dmub_srv->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT_WITH_REPLY))
			return 0;
	}
	return cmd.panel_cntl.data.current_backlight;
}

static void dcn31_panel_cntl_destroy(struct panel_cntl **panel_cntl)
{
	struct dcn31_panel_cntl *dcn31_panel_cntl = TO_DCN31_PANEL_CNTL(*panel_cntl);

	kfree(dcn31_panel_cntl);
	*panel_cntl = NULL;
}

static bool dcn31_is_panel_backlight_on(struct panel_cntl *panel_cntl)
{
	union dmub_rb_cmd cmd;

	if (!dcn31_query_backlight_info(panel_cntl, &cmd))
		return false;

	return cmd.panel_cntl.data.is_backlight_on;
}

static bool dcn31_is_panel_powered_on(struct panel_cntl *panel_cntl)
{
	union dmub_rb_cmd cmd;

	if (!dcn31_query_backlight_info(panel_cntl, &cmd))
		return false;

	return cmd.panel_cntl.data.is_powered_on;
}

static void dcn31_store_backlight_level(struct panel_cntl *panel_cntl)
{
	union dmub_rb_cmd cmd;

	if (!dcn31_query_backlight_info(panel_cntl, &cmd))
		return;

	panel_cntl->stored_backlight_registers.BL_PWM_CNTL = cmd.panel_cntl.data.bl_pwm_cntl;
	panel_cntl->stored_backlight_registers.BL_PWM_CNTL2 = 0; /* unused */
	panel_cntl->stored_backlight_registers.BL_PWM_PERIOD_CNTL = cmd.panel_cntl.data.bl_pwm_period_cntl;
	panel_cntl->stored_backlight_registers.LVTMA_PWRSEQ_REF_DIV_BL_PWM_REF_DIV =
		cmd.panel_cntl.data.bl_pwm_ref_div1;
}

static const struct panel_cntl_funcs dcn31_link_panel_cntl_funcs = {
	.destroy = dcn31_panel_cntl_destroy,
	.hw_init = dcn31_panel_cntl_hw_init,
	.is_panel_backlight_on = dcn31_is_panel_backlight_on,
	.is_panel_powered_on = dcn31_is_panel_powered_on,
	.store_backlight_level = dcn31_store_backlight_level,
	.get_current_backlight = dcn31_get_16_bit_backlight_from_pwm,
};

void dcn31_panel_cntl_construct(
	struct dcn31_panel_cntl *dcn31_panel_cntl,
	const struct panel_cntl_init_data *init_data)
{

	dcn31_panel_cntl->base.funcs = &dcn31_link_panel_cntl_funcs;
	dcn31_panel_cntl->base.ctx = init_data->ctx;
	dcn31_panel_cntl->base.inst = init_data->inst;

	if (dcn31_panel_cntl->base.ctx->dc->config.support_edp0_on_dp1) {
		//If supported, power sequencer mapping shall follow the DIG instance
		uint8_t pwrseq_inst = 0xF;

		switch (init_data->eng_id) {
		case ENGINE_ID_DIGA:
			pwrseq_inst = 0;
			break;
		case ENGINE_ID_DIGB:
			pwrseq_inst = 1;
			break;
		default:
			DC_LOG_WARNING("Unsupported pwrseq engine id: %d!\n", init_data->eng_id);
			ASSERT(false);
			break;
		}

		dcn31_panel_cntl->base.pwrseq_inst = pwrseq_inst;
	} else {
		/* If not supported, pwrseq will be assigned in order,
		 * so first pwrseq will be assigned to first panel instance (legacy behavior)
		 */
		dcn31_panel_cntl->base.pwrseq_inst = dcn31_panel_cntl->base.inst;
	}
}
