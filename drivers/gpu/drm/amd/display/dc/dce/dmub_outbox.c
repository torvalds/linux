/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
 */

#include "dc.h"
#include "dc_dmub_srv.h"
#include "dmub_outbox.h"
#include "dmub/inc/dmub_cmd.h"

/*
 *  Function: dmub_enable_outbox_notification
 *
 *  @brief
 *		Sends inbox cmd to dmub for enabling outbox notifications to x86.
 *
 *  @param
 *		[in] dmub_srv: dmub_srv structure
 */
void dmub_enable_outbox_notification(struct dc_dmub_srv *dmub_srv)
{
	union dmub_rb_cmd cmd;

	memset(&cmd, 0x0, sizeof(cmd));
	cmd.outbox1_enable.header.type = DMUB_CMD__OUTBOX1_ENABLE;
	cmd.outbox1_enable.header.sub_type = 0;
	cmd.outbox1_enable.header.payload_bytes =
		sizeof(cmd.outbox1_enable) -
		sizeof(cmd.outbox1_enable.header);
	cmd.outbox1_enable.enable = true;

	dc_wake_and_execute_dmub_cmd(dmub_srv->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}
