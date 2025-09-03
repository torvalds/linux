/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __DC_LINK_DETECTION_H__
#define __DC_LINK_DETECTION_H__
#include "link_service.h"
bool link_detect(struct dc_link *link, enum dc_detect_reason reason);
bool link_detect_connection_type(struct dc_link *link,
		enum dc_connection_type *type);
struct dc_sink *link_add_remote_sink(
		struct dc_link *link,
		const uint8_t *edid,
		int len,
		struct dc_sink_init_data *init_data);
void link_remove_remote_sink(struct dc_link *link, struct dc_sink *sink);
bool link_reset_cur_dp_mst_topology(struct dc_link *link);
const struct dc_link_status *link_get_status(const struct dc_link *link);
bool link_is_hdcp14(struct dc_link *link, enum signal_type signal);
bool link_is_hdcp22(struct dc_link *link, enum signal_type signal);
void link_clear_dprx_states(struct dc_link *link);
#endif /* __DC_LINK_DETECTION_H__ */
