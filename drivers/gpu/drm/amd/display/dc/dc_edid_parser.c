/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "dce/dce_dmcu.h"
#include "dc_edid_parser.h"

bool dc_edid_parser_send_cea(struct dc *dc,
		int offset,
		int total_length,
		uint8_t *data,
		int length)
{
	struct dmcu *dmcu = dc->res_pool->dmcu;

	if (dmcu &&
	    dmcu->funcs->is_dmcu_initialized(dmcu) &&
	    dmcu->funcs->send_edid_cea) {
		return dmcu->funcs->send_edid_cea(dmcu,
				offset,
				total_length,
				data,
				length);
	}

	return false;
}

bool dc_edid_parser_recv_cea_ack(struct dc *dc, int *offset)
{
	struct dmcu *dmcu = dc->res_pool->dmcu;

	if (dmcu &&
	    dmcu->funcs->is_dmcu_initialized(dmcu) &&
	    dmcu->funcs->recv_edid_cea_ack) {
		return dmcu->funcs->recv_edid_cea_ack(dmcu, offset);
	}

	return false;
}

bool dc_edid_parser_recv_amd_vsdb(struct dc *dc,
		int *version,
		int *min_frame_rate,
		int *max_frame_rate)
{
	struct dmcu *dmcu = dc->res_pool->dmcu;

	if (dmcu &&
	    dmcu->funcs->is_dmcu_initialized(dmcu) &&
	    dmcu->funcs->recv_amd_vsdb) {
		return dmcu->funcs->recv_amd_vsdb(dmcu,
				version,
				min_frame_rate,
				max_frame_rate);
	}

	return false;
}
