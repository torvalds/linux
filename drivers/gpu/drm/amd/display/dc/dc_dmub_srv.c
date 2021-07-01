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

#include "dc.h"
#include "dc_dmub_srv.h"
#include "../dmub/dmub_srv.h"
#include "dm_helpers.h"

#define CTX dc_dmub_srv->ctx
#define DC_LOGGER CTX->logger

static void dc_dmub_srv_construct(struct dc_dmub_srv *dc_srv, struct dc *dc,
				  struct dmub_srv *dmub)
{
	dc_srv->dmub = dmub;
	dc_srv->ctx = dc->ctx;
}

struct dc_dmub_srv *dc_dmub_srv_create(struct dc *dc, struct dmub_srv *dmub)
{
	struct dc_dmub_srv *dc_srv =
		kzalloc(sizeof(struct dc_dmub_srv), GFP_KERNEL);

	if (dc_srv == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dc_dmub_srv_construct(dc_srv, dc, dmub);

	return dc_srv;
}

void dc_dmub_srv_destroy(struct dc_dmub_srv **dmub_srv)
{
	if (*dmub_srv) {
		kfree(*dmub_srv);
		*dmub_srv = NULL;
	}
}

void dc_dmub_srv_cmd_queue(struct dc_dmub_srv *dc_dmub_srv,
			   union dmub_rb_cmd *cmd)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	status = dmub_srv_cmd_queue(dmub, cmd);
	if (status == DMUB_STATUS_OK)
		return;

	if (status != DMUB_STATUS_QUEUE_FULL)
		goto error;

	/* Execute and wait for queue to become empty again. */
	dc_dmub_srv_cmd_execute(dc_dmub_srv);
	dc_dmub_srv_wait_idle(dc_dmub_srv);

	/* Requeue the command. */
	status = dmub_srv_cmd_queue(dmub, cmd);
	if (status == DMUB_STATUS_OK)
		return;

error:
	DC_ERROR("Error queuing DMUB command: status=%d\n", status);
	dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
}

void dc_dmub_srv_cmd_execute(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	status = dmub_srv_cmd_execute(dmub);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error starting DMUB execution: status=%d\n", status);
		dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
	}
}

void dc_dmub_srv_wait_idle(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	status = dmub_srv_wait_for_idle(dmub, 100000);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error waiting for DMUB idle: status=%d\n", status);
		dc_dmub_srv_log_diagnostic_data(dc_dmub_srv);
	}
}

void dc_dmub_srv_send_inbox0_cmd(struct dc_dmub_srv *dmub_srv,
		union dmub_inbox0_data_register data)
{
	struct dmub_srv *dmub = dmub_srv->dmub;
	if (dmub->hw_funcs.send_inbox0_cmd)
		dmub->hw_funcs.send_inbox0_cmd(dmub, data);
	// TODO: Add wait command -- poll register for ACK
}

bool dc_dmub_srv_cmd_with_reply_data(struct dc_dmub_srv *dc_dmub_srv, union dmub_rb_cmd *cmd)
{
	struct dmub_srv *dmub;
	enum dmub_status status;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dmub = dc_dmub_srv->dmub;

	status = dmub_srv_cmd_with_reply_data(dmub, cmd);
	if (status != DMUB_STATUS_OK) {
		DC_LOG_DEBUG("No reply for DMUB command: status=%d\n", status);
		return false;
	}

	return true;
}

void dc_dmub_srv_wait_phy_init(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	for (;;) {
		/* Wait up to a second for PHY init. */
		status = dmub_srv_wait_for_phy_init(dmub, 1000000);
		if (status == DMUB_STATUS_OK)
			/* Initialization OK */
			break;

		DC_ERROR("DMCUB PHY init failed: status=%d\n", status);
		ASSERT(0);

		if (status != DMUB_STATUS_TIMEOUT)
			/*
			 * Server likely initialized or we don't have
			 * DMCUB HW support - this won't end.
			 */
			break;

		/* Continue spinning so we don't hang the ASIC. */
	}
}

bool dc_dmub_srv_notify_stream_mask(struct dc_dmub_srv *dc_dmub_srv,
				    unsigned int stream_mask)
{
	struct dmub_srv *dmub;
	const uint32_t timeout = 30;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dmub = dc_dmub_srv->dmub;

	return dmub_srv_send_gpint_command(
		       dmub, DMUB_GPINT__IDLE_OPT_NOTIFY_STREAM_MASK,
		       stream_mask, timeout) == DMUB_STATUS_OK;
}

bool dc_dmub_srv_is_restore_required(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub;
	struct dc_context *dc_ctx;
	union dmub_fw_boot_status boot_status;
	enum dmub_status status;

	if (!dc_dmub_srv || !dc_dmub_srv->dmub)
		return false;

	dmub = dc_dmub_srv->dmub;
	dc_ctx = dc_dmub_srv->ctx;

	status = dmub_srv_get_fw_boot_status(dmub, &boot_status);
	if (status != DMUB_STATUS_OK) {
		DC_ERROR("Error querying DMUB boot status: error=%d\n", status);
		return false;
	}

	return boot_status.bits.restore_required;
}

bool dc_dmub_srv_get_dmub_outbox0_msg(const struct dc *dc, struct dmcub_trace_buf_entry *entry)
{
	struct dmub_srv *dmub = dc->ctx->dmub_srv->dmub;
	return dmub_srv_get_outbox0_msg(dmub, entry);
}

void dc_dmub_trace_event_control(struct dc *dc, bool enable)
{
	dm_helpers_dmub_outbox_interrupt_control(dc->ctx, enable);
}

bool dc_dmub_srv_get_diagnostic_data(struct dc_dmub_srv *dc_dmub_srv, struct dmub_diagnostic_data *diag_data)
{
	if (!dc_dmub_srv || !dc_dmub_srv->dmub || !diag_data)
		return false;
	return dmub_srv_get_diagnostic_data(dc_dmub_srv->dmub, diag_data);
}

void dc_dmub_srv_log_diagnostic_data(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_diagnostic_data diag_data = {0};

	if (!dc_dmub_srv || !dc_dmub_srv->dmub) {
		DC_LOG_ERROR("%s: invalid parameters.", __func__);
		return;
	}

	if (!dc_dmub_srv_get_diagnostic_data(dc_dmub_srv, &diag_data)) {
		DC_LOG_ERROR("%s: dc_dmub_srv_get_diagnostic_data failed.", __func__);
		return;
	}

	DC_LOG_DEBUG(
		"DMCUB STATE\n"
		"    dmcub_version      : %08x\n"
		"    scratch  [0]       : %08x\n"
		"    scratch  [1]       : %08x\n"
		"    scratch  [2]       : %08x\n"
		"    scratch  [3]       : %08x\n"
		"    scratch  [4]       : %08x\n"
		"    scratch  [5]       : %08x\n"
		"    scratch  [6]       : %08x\n"
		"    scratch  [7]       : %08x\n"
		"    scratch  [8]       : %08x\n"
		"    scratch  [9]       : %08x\n"
		"    scratch [10]       : %08x\n"
		"    scratch [11]       : %08x\n"
		"    scratch [12]       : %08x\n"
		"    scratch [13]       : %08x\n"
		"    scratch [14]       : %08x\n"
		"    scratch [15]       : %08x\n"
		"    pc                 : %08x\n"
		"    unk_fault_addr     : %08x\n"
		"    inst_fault_addr    : %08x\n"
		"    data_fault_addr    : %08x\n"
		"    inbox1_rptr        : %08x\n"
		"    inbox1_wptr        : %08x\n"
		"    inbox1_size        : %08x\n"
		"    inbox0_rptr        : %08x\n"
		"    inbox0_wptr        : %08x\n"
		"    inbox0_size        : %08x\n"
		"    is_enabled         : %d\n"
		"    is_soft_reset      : %d\n"
		"    is_secure_reset    : %d\n"
		"    is_traceport_en    : %d\n"
		"    is_cw0_en          : %d\n"
		"    is_cw6_en          : %d\n",
		diag_data.dmcub_version,
		diag_data.scratch[0],
		diag_data.scratch[1],
		diag_data.scratch[2],
		diag_data.scratch[3],
		diag_data.scratch[4],
		diag_data.scratch[5],
		diag_data.scratch[6],
		diag_data.scratch[7],
		diag_data.scratch[8],
		diag_data.scratch[9],
		diag_data.scratch[10],
		diag_data.scratch[11],
		diag_data.scratch[12],
		diag_data.scratch[13],
		diag_data.scratch[14],
		diag_data.scratch[15],
		diag_data.pc,
		diag_data.undefined_address_fault_addr,
		diag_data.inst_fetch_fault_addr,
		diag_data.data_write_fault_addr,
		diag_data.inbox1_rptr,
		diag_data.inbox1_wptr,
		diag_data.inbox1_size,
		diag_data.inbox0_rptr,
		diag_data.inbox0_wptr,
		diag_data.inbox0_size,
		diag_data.is_dmcub_enabled,
		diag_data.is_dmcub_soft_reset,
		diag_data.is_dmcub_secure_reset,
		diag_data.is_traceport_en,
		diag_data.is_cw0_enabled,
		diag_data.is_cw6_enabled);
}
