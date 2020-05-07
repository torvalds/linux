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
#include "dcn21_hwseq.h"
#include "vmid.h"
#include "reg_helper.h"
#include "hw/clk_mgr.h"


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
	struct dcn_hubbub_phys_addr_config config;

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
 * which indicate that PLL disable attempt actually didn’t go through.
 * As a workaround, insert PHY lane enable/disable before PLL disable.
 */
void dcn21_PLAT_58856_wa(struct dc_state *context, struct pipe_ctx *pipe_ctx)
{
	if (!pipe_ctx->stream->dpms_off)
		return;

	pipe_ctx->stream->dpms_off = false;
	core_link_enable_stream(context, pipe_ctx);
	core_link_disable_stream(pipe_ctx);
	pipe_ctx->stream->dpms_off = true;
}

