/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
 * This file provides single entrance to link functionality declared in dc
 * public headers. The file is intended to be used as a thin translation layer
 * that directly calls link internal functions without adding new functional
 * behavior.
 *
 * When exporting a new link related dc function, add function declaration in
 * dc.h with detail interface documentation, then add function implementation
 * in this file which calls link functions.
 */
#include "link.h"

bool dc_link_detect(struct dc_link *link, enum dc_detect_reason reason)
{
	return link_detect(link, reason);
}

bool dc_link_detect_connection_type(struct dc_link *link,
		enum dc_connection_type *type)
{
	return link_detect_connection_type(link, type);
}

const struct dc_link_status *dc_link_get_status(const struct dc_link *link)
{
	return link_get_status(link);
}
#ifdef CONFIG_DRM_AMD_DC_HDCP

/* return true if the connected receiver supports the hdcp version */
bool dc_link_is_hdcp14(struct dc_link *link, enum signal_type signal)
{
	return link_is_hdcp14(link, signal);
}

bool dc_link_is_hdcp22(struct dc_link *link, enum signal_type signal)
{
	return link_is_hdcp22(link, signal);
}
#endif

void dc_link_clear_dprx_states(struct dc_link *link)
{
	link_clear_dprx_states(link);
}

bool dc_link_reset_cur_dp_mst_topology(struct dc_link *link)
{
	return link_reset_cur_dp_mst_topology(link);
}

uint32_t dc_link_bandwidth_kbps(
	const struct dc_link *link,
	const struct dc_link_settings *link_settings)
{
	return dp_link_bandwidth_kbps(link, link_settings);
}

uint32_t dc_bandwidth_in_kbps_from_timing(
	const struct dc_crtc_timing *timing)
{
	return link_timing_bandwidth_kbps(timing);
}

void dc_get_cur_link_res_map(const struct dc *dc, uint32_t *map)
{
	link_get_cur_res_map(dc, map);
}

void dc_restore_link_res_map(const struct dc *dc, uint32_t *map)
{
	link_restore_res_map(dc, map);
}

bool dc_link_update_dsc_config(struct pipe_ctx *pipe_ctx)
{
	return link_update_dsc_config(pipe_ctx);
}
