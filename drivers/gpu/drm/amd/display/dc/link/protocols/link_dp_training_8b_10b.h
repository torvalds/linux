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


#ifndef __DC_LINK_DP_TRAINING_8B_10B_H__
#define __DC_LINK_DP_TRAINING_8B_10B_H__
#include "link_dp_training.h"

/* to avoid infinite loop where-in the receiver
 * switches between different VS
 */
#define LINK_TRAINING_MAX_CR_RETRY 100
#define LINK_TRAINING_MAX_RETRY_COUNT 5

enum link_training_result dp_perform_8b_10b_link_training(
		struct dc_link *link,
		const struct link_resource *link_res,
		struct link_training_settings *lt_settings);

enum link_training_result perform_8b_10b_clock_recovery_sequence(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings,
	uint32_t offset);

enum link_training_result perform_8b_10b_channel_equalization_sequence(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings,
	uint32_t offset);

enum lttpr_mode dp_decide_8b_10b_lttpr_mode(struct dc_link *link);

void decide_8b_10b_training_settings(
	 struct dc_link *link,
	const struct dc_link_settings *link_setting,
	struct link_training_settings *lt_settings);

#endif /* __DC_LINK_DP_TRAINING_8B_10B_H__ */
