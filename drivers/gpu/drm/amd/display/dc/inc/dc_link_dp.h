/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef __DC_LINK_DP_H__
#define __DC_LINK_DP_H__

#define LINK_TRAINING_ATTEMPTS 4
#define LINK_TRAINING_RETRY_DELAY 50 /* ms */

struct core_link;
struct core_stream;
struct dc_link_settings;

bool dp_hbr_verify_link_cap(
	struct core_link *link,
	struct dc_link_settings *known_limit_link_setting);

bool dp_validate_mode_timing(
	struct core_link *link,
	const struct dc_crtc_timing *timing);

void decide_link_settings(
	struct core_stream *stream,
	struct dc_link_settings *link_setting);

bool perform_link_training_with_retries(
	struct core_link *link,
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern,
	int attempts);

bool is_mst_supported(struct core_link *link);

void detect_dp_sink_caps(struct core_link *link);

bool is_dp_active_dongle(const struct core_link *link);

#endif /* __DC_LINK_DP_H__ */
