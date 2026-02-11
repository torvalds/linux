/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include "dmub_hw_lock_mgr.h"
#include "dc_dmub_srv.h"
#include "dc_types.h"
#include "core_types.h"

void dmub_hw_lock_mgr_cmd(struct dc_dmub_srv *dmub_srv,
				bool lock,
				union dmub_hw_lock_flags *hw_locks,
				struct dmub_hw_lock_inst_flags *inst_flags)
{
	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.lock_hw.header.type = DMUB_CMD__HW_LOCK;
	cmd.lock_hw.header.sub_type = 0;
	cmd.lock_hw.header.payload_bytes = sizeof(struct dmub_cmd_lock_hw_data);
	cmd.lock_hw.lock_hw_data.client = HW_LOCK_CLIENT_DRIVER;
	cmd.lock_hw.lock_hw_data.lock = lock;
	cmd.lock_hw.lock_hw_data.hw_locks.u8All = hw_locks->u8All;
	memcpy(&cmd.lock_hw.lock_hw_data.inst_flags, inst_flags, sizeof(struct dmub_hw_lock_inst_flags));

	if (!lock)
		cmd.lock_hw.lock_hw_data.should_release = 1;

	dc_wake_and_execute_dmub_cmd(dmub_srv->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

void dmub_hw_lock_mgr_inbox0_cmd(struct dc_dmub_srv *dmub_srv,
		union dmub_inbox0_cmd_lock_hw hw_lock_cmd)
{
	union dmub_inbox0_data_register data = { 0 };

	data.inbox0_cmd_lock_hw = hw_lock_cmd;
	dc_dmub_srv_clear_inbox0_ack(dmub_srv);
	dc_dmub_srv_send_inbox0_cmd(dmub_srv, data);
	dc_dmub_srv_wait_for_inbox0_ack(dmub_srv);
}

bool dmub_hw_lock_mgr_does_link_require_lock(const struct dc *dc, const struct dc_link *link)
{
	if (!link)
		return false;

	if (link->psr_settings.psr_version == DC_PSR_VERSION_SU_1)
		return true;

	if (link->replay_settings.replay_feature_enabled && dc_is_embedded_signal(link->connector_signal))
		return true;

	if (link->psr_settings.psr_version == DC_PSR_VERSION_1) {
		struct dc_link *edp_links[MAX_NUM_EDP];
		int edp_num;

		dc_get_edp_links(dc, edp_links, &edp_num);
		if (edp_num == 1)
			return true;
	}
	return false;
}

bool dmub_hw_lock_mgr_does_context_require_lock(const struct dc *dc, const struct dc_state *context)
{
	if (!context)
		return false;
	for (int i = 0; i < context->stream_count; i++) {
		const struct dc_link *link = context->streams[i]->link;

		if (dmub_hw_lock_mgr_does_link_require_lock(dc, link))
			return true;
	}
	return false;
}

bool should_use_dmub_inbox1_lock(const struct dc *dc, const struct dc_link *link)
{
	/* ASIC doesn't support DMUB */
	if (!dc->ctx->dmub_srv)
		return false;

	if (dc->ctx->dce_version >= DCN_VERSION_4_01)
		return false;

	return dmub_hw_lock_mgr_does_link_require_lock(dc, link);
}
