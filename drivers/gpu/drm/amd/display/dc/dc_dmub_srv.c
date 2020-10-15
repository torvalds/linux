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
}

void dc_dmub_srv_cmd_execute(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	status = dmub_srv_cmd_execute(dmub);
	if (status != DMUB_STATUS_OK)
		DC_ERROR("Error starting DMUB execution: status=%d\n", status);
}

void dc_dmub_srv_wait_idle(struct dc_dmub_srv *dc_dmub_srv)
{
	struct dmub_srv *dmub = dc_dmub_srv->dmub;
	struct dc_context *dc_ctx = dc_dmub_srv->ctx;
	enum dmub_status status;

	status = dmub_srv_wait_for_idle(dmub, 100000);
	if (status != DMUB_STATUS_OK)
		DC_ERROR("Error waiting for DMUB idle: status=%d\n", status);
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
