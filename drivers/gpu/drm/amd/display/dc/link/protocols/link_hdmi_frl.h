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
#ifndef __LINK_HDMI_FRL_H__
#define __LINK_HDMI_FRL_H__
#include "link_service.h"
enum clock_source_id hdmi_frl_find_matching_phypll(
		struct dc_link *link);
void hdmi_frl_LTS_clear_Update_flag(struct ddc_service *ddc_service);
void hdmi_frl_poll_start(struct ddc_service *ddc_service);
void hdmi_frl_LTS_clear_Link_Setting(struct ddc_service *ddc_service);
void hdmi_frl_retrieve_link_cap(struct dc_link *link, struct dc_sink *sink);
enum link_result hdmi_frl_perform_link_training_with_retries(
	struct dc_link *link);
enum link_result hdmi_frl_perform_link_training_with_fallback(
	struct dc_link *link, struct link_resource *link_res,
	enum clock_source_id frl_phy_clock_source_id);
void hdmi_frl_verify_link_cap(struct dc_link *link,
		struct dc_hdmi_frl_link_settings *known_limit_link_setting);
void hdmi_frl_decide_link_settings(struct dc_stream_state *stream,
	struct dc_hdmi_frl_link_settings *frl_link_settings,
	struct dsc_padding_params *dsc_paddding_params);
bool hdmi_frl_poll_status_flag(struct dc_link *link);
struct dc_hdmi_frl_link_settings *hdmi_frl_get_verified_link_cap(
		struct dc_link *link);
void hdmi_frl_set_preferred_link_settings(struct dc *dc,
		struct dc_hdmi_frl_link_settings *link_setting,
		struct dc_hdmi_frl_link_training_overrides *lt_overrides,
		struct dc_link *link);
void hdmi_frl_write_read_request_enable(
		struct ddc_service *ddc_service);
#endif /* __LINK_HDMI_FRL_H__ */
