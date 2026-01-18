/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#ifndef __DC_LINK_DP_PANEL_REPLAY_H__
#define __DC_LINK_DP_PANEL_REPLAY_H__

#include "link_service.h"

bool dp_setup_replay(struct dc_link *link, const struct dc_stream_state *stream);
bool dp_pr_get_panel_inst(const struct dc *dc, const struct dc_link *link, unsigned int *inst_out);
bool dp_pr_enable(struct dc_link *link, bool enable);
bool dp_pr_copy_settings(struct dc_link *link, struct replay_context *replay_context);
bool dp_pr_update_state(struct dc_link *link, struct dmub_cmd_pr_update_state_data *update_state_data);
bool dp_pr_set_general_cmd(struct dc_link *link, struct dmub_cmd_pr_general_cmd_data *general_cmd_data);
bool dp_pr_get_state(const struct dc_link *link, uint64_t *state);

#endif /* __DC_LINK_DP_PANEL_REPLAY_H__ */