/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

/* FILE POLICY AND INTENDED USAGE:
 * This file implements basic dp phy functionality such as enable/disable phy
 * output and set lane/drive settings. This file is responsible for maintaining
 * and update software state representing current phy status such as current
 * link settings.
 */

#include "link_dp_phy.h"
#include "link_dpcd.h"
#include "link_dp_training.h"
#include "link_dp_capability.h"
#include "clk_mgr.h"
#include "resource.h"

#define DC_LOGGER \
	link->ctx->logger

void dc_link_dp_receiver_power_ctrl(struct dc_link *link, bool on)
{
	uint8_t state;

	state = on ? DP_POWER_STATE_D0 : DP_POWER_STATE_D3;

	if (link->sync_lt_in_progress)
		return;

	core_link_write_dpcd(link, DP_SET_POWER, &state,
						 sizeof(state));

}

void dp_enable_link_phy(
	struct dc_link *link,
	const struct link_resource *link_res,
	enum signal_type signal,
	enum clock_source_id clock_source,
	const struct dc_link_settings *link_settings)
{
	link->cur_link_settings = *link_settings;
	link->dc->hwss.enable_dp_link_output(link, link_res, signal,
			clock_source, link_settings);
	dc_link_dp_receiver_power_ctrl(link, true);
}

void dp_disable_link_phy(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	struct dc  *dc = link->ctx->dc;

	if (!link->wa_flags.dp_keep_receiver_powered)
		dc_link_dp_receiver_power_ctrl(link, false);

	dc->hwss.disable_link_output(link, link_res, signal);
	/* Clear current link setting.*/
	memset(&link->cur_link_settings, 0,
			sizeof(link->cur_link_settings));

	if (dc->clk_mgr->funcs->notify_link_rate_change)
		dc->clk_mgr->funcs->notify_link_rate_change(dc->clk_mgr, link);
}

void dp_disable_link_phy_mst(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	/* MST disable link only when no stream use the link */
	if (link->mst_stream_alloc_table.stream_count > 0)
		return;

	dp_disable_link_phy(link, link_res, signal);

	/* set the sink to SST mode after disabling the link */
	dp_enable_mst_on_sink(link, false);
}

static inline bool is_immediate_downstream(struct dc_link *link, uint32_t offset)
{
	return (dp_parse_lttpr_repeater_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt) ==
			offset);
}

void dp_set_hw_lane_settings(
	struct dc_link *link,
	const struct link_resource *link_res,
	const struct link_training_settings *link_settings,
	uint32_t offset)
{
	const struct link_hwss *link_hwss = get_link_hwss(link, link_res);

	if ((link_settings->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT) &&
			!is_immediate_downstream(link, offset))
		return;

	if (link_hwss->ext.set_dp_lane_settings)
		link_hwss->ext.set_dp_lane_settings(link, link_res,
				&link_settings->link_settings,
				link_settings->hw_lane_settings);

	memmove(link->cur_lane_setting,
			link_settings->hw_lane_settings,
			sizeof(link->cur_lane_setting));
}

void dp_set_drive_settings(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings)
{
	/* program ASIC PHY settings*/
	dp_set_hw_lane_settings(link, link_res, lt_settings, DPRX);

	dp_hw_to_dpcd_lane_settings(lt_settings,
			lt_settings->hw_lane_settings,
			lt_settings->dpcd_lane_settings);

	/* Notify DP sink the PHY settings from source */
	dpcd_set_lane_settings(link, lt_settings, DPRX);
}
