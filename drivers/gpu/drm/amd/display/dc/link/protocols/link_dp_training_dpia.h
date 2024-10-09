/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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


#ifndef __DC_LINK_DP_TRAINING_DPIA_H__
#define __DC_LINK_DP_TRAINING_DPIA_H__
#include "link_dp_training.h"

/* The approximate time (us) it takes to transmit 9 USB4 DP clock sync packets. */
#define DPIA_CLK_SYNC_DELAY 16000

/* Train DP tunneling link for USB4 DPIA display endpoint.
 * DPIA equivalent of dc_link_dp_perfrorm_link_training.
 * Aborts link training upon detection of sink unplug.
 */
enum link_training_result dpia_perform_link_training(
	struct dc_link *link,
	const struct link_resource *link_res,
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern);

void dpia_training_abort(
		struct dc_link *link,
		struct link_training_settings *lt_settings,
		uint32_t hop);

uint32_t dpia_get_eq_aux_rd_interval(
		const struct dc_link *link,
		const struct link_training_settings *lt_settings,
		uint32_t hop);

void dpia_set_tps_notification(
		struct dc_link *link,
		const struct link_training_settings *lt_settings,
		uint8_t pattern,
		uint32_t offset);

#endif /* __DC_LINK_DP_TRAINING_DPIA_H__ */
