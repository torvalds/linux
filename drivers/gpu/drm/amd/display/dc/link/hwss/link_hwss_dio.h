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
#ifndef __LINK_HWSS_DIO_H__
#define __LINK_HWSS_DIO_H__

#include "link_hwss.h"

const struct link_hwss *get_dio_link_hwss(void);
bool can_use_dio_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res);
void set_dio_throttled_vcp_size(struct pipe_ctx *pipe_ctx,
		struct fixed31_32 throttled_vcp_size);
void setup_dio_stream_encoder(struct pipe_ctx *pipe_ctx);
void reset_dio_stream_encoder(struct pipe_ctx *pipe_ctx);
void setup_dio_stream_attribute(struct pipe_ctx *pipe_ctx);
void enable_dio_dp_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		const struct dc_link_settings *link_settings);
void disable_dio_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal);
void set_dio_dp_link_test_pattern(struct dc_link *link,
		const struct link_resource *link_res,
		struct encoder_set_dp_phy_pattern_param *tp_params);
void set_dio_dp_lane_settings(struct dc_link *link,
		const struct link_resource *link_res,
		const struct dc_link_settings *link_settings,
		const struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX]);
void setup_dio_audio_output(struct pipe_ctx *pipe_ctx,
		struct audio_output *audio_output, uint32_t audio_inst);
void enable_dio_audio_packet(struct pipe_ctx *pipe_ctx);
void disable_dio_audio_packet(struct pipe_ctx *pipe_ctx);

#endif /* __LINK_HWSS_DIO_H__ */
