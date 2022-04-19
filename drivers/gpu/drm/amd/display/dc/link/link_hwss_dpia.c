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
#include "link_hwss_dpia.h"
#include "core_types.h"
#include "link_hwss_dio.h"

static const struct link_hwss dpia_link_hwss = {
	.setup_stream_encoder = setup_dio_stream_encoder,
	.reset_stream_encoder = reset_dio_stream_encoder,
	.ext = {
		.set_throttled_vcp_size = set_dio_throttled_vcp_size,
		.enable_dp_link_output = enable_dio_dp_link_output,
		.disable_dp_link_output = disable_dio_dp_link_output,
		.set_dp_link_test_pattern = set_dio_dp_link_test_pattern,
		.set_dp_lane_settings = set_dio_dp_lane_settings,
	},
};

bool can_use_dpia_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res)
{
	return link->is_dig_mapping_flexible &&
			link->dc->res_pool->funcs->link_encs_assign;
}

const struct link_hwss *get_dpia_link_hwss(void)
{
	return &dpia_link_hwss;
}
