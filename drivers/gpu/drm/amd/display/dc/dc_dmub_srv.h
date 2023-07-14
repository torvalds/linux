/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef _DMUB_DC_SRV_H_
#define _DMUB_DC_SRV_H_

#include "dm_services_types.h"
#include "dmub/dmub_srv.h"

struct dmub_srv;
struct dc;
struct pipe_ctx;
struct dc_crtc_timing_adjust;
struct dc_crtc_timing;
struct dc_state;

struct dc_reg_helper_state {
	bool gather_in_progress;
	uint32_t same_addr_count;
	bool should_burst_write;
	union dmub_rb_cmd cmd_data;
	unsigned int reg_seq_count;
};

struct dc_dmub_srv {
	struct dmub_srv *dmub;
	struct dc_reg_helper_state reg_helper_offload;

	struct dc_context *ctx;
	void *dm;
};

void dc_dmub_srv_wait_idle(struct dc_dmub_srv *dc_dmub_srv);

bool dc_dmub_srv_optimized_init_done(struct dc_dmub_srv *dc_dmub_srv);

bool dc_dmub_srv_cmd_run(struct dc_dmub_srv *dc_dmub_srv, union dmub_rb_cmd *cmd, enum dm_dmub_wait_type wait_type);

bool dc_dmub_srv_cmd_run_list(struct dc_dmub_srv *dc_dmub_srv, unsigned int count, union dmub_rb_cmd *cmd_list, enum dm_dmub_wait_type wait_type);

bool dc_dmub_srv_notify_stream_mask(struct dc_dmub_srv *dc_dmub_srv,
				    unsigned int stream_mask);

bool dc_dmub_srv_is_restore_required(struct dc_dmub_srv *dc_dmub_srv);

bool dc_dmub_srv_get_dmub_outbox0_msg(const struct dc *dc, struct dmcub_trace_buf_entry *entry);

void dc_dmub_trace_event_control(struct dc *dc, bool enable);

void dc_dmub_srv_drr_update_cmd(struct dc *dc, uint32_t tg_inst, uint32_t vtotal_min, uint32_t vtotal_max);

void dc_dmub_srv_set_drr_manual_trigger_cmd(struct dc *dc, uint32_t tg_inst);
bool dc_dmub_srv_p_state_delegate(struct dc *dc, bool enable_pstate, struct dc_state *context);

void dc_dmub_srv_query_caps_cmd(struct dc_dmub_srv *dc_dmub_srv);
void dc_dmub_srv_get_visual_confirm_color_cmd(struct dc *dc, struct pipe_ctx *pipe_ctx);
void dc_dmub_srv_clear_inbox0_ack(struct dc_dmub_srv *dmub_srv);
void dc_dmub_srv_wait_for_inbox0_ack(struct dc_dmub_srv *dmub_srv);
void dc_dmub_srv_send_inbox0_cmd(struct dc_dmub_srv *dmub_srv, union dmub_inbox0_data_register data);

bool dc_dmub_srv_get_diagnostic_data(struct dc_dmub_srv *dc_dmub_srv, struct dmub_diagnostic_data *dmub_oca);

void dc_dmub_setup_subvp_dmub_command(struct dc *dc, struct dc_state *context, bool enable);
void dc_dmub_srv_log_diagnostic_data(struct dc_dmub_srv *dc_dmub_srv);

void dc_send_update_cursor_info_to_dmu(struct pipe_ctx *pCtx, uint8_t pipe_idx);
bool dc_dmub_check_min_version(struct dmub_srv *srv);
#endif /* _DMUB_DC_SRV_H_ */
