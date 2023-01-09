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
#define MAX_MTP_SLOT_COUNT 64
#define TRAINING_AUX_RD_INTERVAL 100 //us
#define LINK_AUX_WAKE_TIMEOUT_MS 1500 // Timeout when trying to wake unresponsive DPRX.

struct dc_link;
struct dc_stream_state;
struct dc_link_settings;

enum {
	/*
	 * Some receivers fail to train on first try and are good
	 * on subsequent tries. 2 retries should be plenty. If we
	 * don't have a successful training then we don't expect to
	 * ever get one.
	 */
	LINK_TRAINING_MAX_VERIFY_RETRY = 2,
	PEAK_FACTOR_X1000 = 1006,
};

bool dp_validate_mode_timing(
	struct dc_link *link,
	const struct dc_crtc_timing *timing);

void dp_enable_mst_on_sink(struct dc_link *link, bool enable);

enum dc_status dp_set_fec_ready(struct dc_link *link, const struct link_resource *link_res, bool ready);
void dp_set_fec_enable(struct dc_link *link, bool enable);
bool dp_set_dsc_enable(struct pipe_ctx *pipe_ctx, bool enable);
bool dp_set_dsc_pps_sdp(struct pipe_ctx *pipe_ctx, bool enable, bool immediate_update);
void dp_set_dsc_on_stream(struct pipe_ctx *pipe_ctx, bool enable);
bool dp_update_dsc_config(struct pipe_ctx *pipe_ctx);
bool dp_set_dsc_on_rx(struct pipe_ctx *pipe_ctx, bool enable);

bool dpcd_write_128b_132b_sst_payload_allocation_table(
		const struct dc_stream_state *stream,
		struct dc_link *link,
		struct link_mst_stream_allocation_table *proposed_table,
		bool allocate);

bool dpcd_poll_for_allocation_change_trigger(struct dc_link *link);

struct fixed31_32 calculate_sst_avg_time_slots_per_mtp(
		const struct dc_stream_state *stream,
		const struct dc_link *link);
void setup_dp_hpo_stream(struct pipe_ctx *pipe_ctx, bool enable);
void dp_source_sequence_trace(struct dc_link *link, uint8_t dp_test_mode);


#endif /* __DC_LINK_DP_H__ */
