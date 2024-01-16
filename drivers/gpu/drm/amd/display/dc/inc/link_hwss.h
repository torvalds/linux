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

#ifndef __DC_LINK_HWSS_H__
#define __DC_LINK_HWSS_H__

/* include basic type headers only */
#include "dc_dp_types.h"
#include "signal_types.h"
#include "grph_object_id.h"
#include "fixed31_32.h"

/* forward declare dc core types */
struct dc_link;
struct link_resource;
struct pipe_ctx;
struct encoder_set_dp_phy_pattern_param;
struct link_mst_stream_allocation_table;

struct link_hwss_ext {
	/* function pointers below may require to check for NULL if caller
	 * considers missing implementation as expected in some cases or none
	 * critical to be investigated immediately
	 * *********************************************************************
	 */
	void (*set_hblank_min_symbol_width)(struct pipe_ctx *pipe_ctx,
			const struct dc_link_settings *link_settings,
			struct fixed31_32 throttled_vcp_size);
	void (*set_throttled_vcp_size)(struct pipe_ctx *pipe_ctx,
			struct fixed31_32 throttled_vcp_size);
	void (*enable_dp_link_output)(struct dc_link *link,
			const struct link_resource *link_res,
			enum signal_type signal,
			enum clock_source_id clock_source,
			const struct dc_link_settings *link_settings);
	void (*set_dp_link_test_pattern)(struct dc_link *link,
			const struct link_resource *link_res,
			struct encoder_set_dp_phy_pattern_param *tp_params);
	void (*set_dp_lane_settings)(struct dc_link *link,
			const struct link_resource *link_res,
			const struct dc_link_settings *link_settings,
			const struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX]);
	void (*update_stream_allocation_table)(struct dc_link *link,
			const struct link_resource *link_res,
			const struct link_mst_stream_allocation_table *table);
};

struct link_hwss {
	struct link_hwss_ext ext;

	/* function pointers below MUST be assigned to all types of link_hwss
	 * *********************************************************************
	 */
	void (*setup_stream_encoder)(struct pipe_ctx *pipe_ctx);
	void (*reset_stream_encoder)(struct pipe_ctx *pipe_ctx);
	void (*setup_stream_attribute)(struct pipe_ctx *pipe_ctx);
	void (*disable_link_output)(struct dc_link *link,
			const struct link_resource *link_res,
			enum signal_type signal);
};
#endif /* __DC_LINK_HWSS_H__ */

