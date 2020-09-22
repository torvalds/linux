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

#include "os_types.h"
#include "dmub/dmub_srv.h"

struct dmub_srv;

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

void dc_dmub_srv_cmd_queue(struct dc_dmub_srv *dc_dmub_srv,
			   union dmub_rb_cmd *cmd);

void dc_dmub_srv_cmd_execute(struct dc_dmub_srv *dc_dmub_srv);

void dc_dmub_srv_wait_idle(struct dc_dmub_srv *dc_dmub_srv);

void dc_dmub_srv_wait_phy_init(struct dc_dmub_srv *dc_dmub_srv);

bool dc_dmub_srv_notify_stream_mask(struct dc_dmub_srv *dc_dmub_srv,
				    unsigned int stream_mask);
#endif /* _DMUB_DC_SRV_H_ */
