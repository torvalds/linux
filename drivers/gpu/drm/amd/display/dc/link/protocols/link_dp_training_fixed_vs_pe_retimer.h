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


#ifndef __DC_LINK_DP_FIXED_VS_PE_RETIMER_H__
#define __DC_LINK_DP_FIXED_VS_PE_RETIMER_H__
#include "link_dp_training.h"

enum link_training_result dp_perform_fixed_vs_pe_training_sequence_legacy(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings);

enum link_training_result dp_perform_fixed_vs_pe_training_sequence(
	struct dc_link *link,
	const struct link_resource *link_res,
	struct link_training_settings *lt_settings);

void dp_fixed_vs_pe_set_retimer_lane_settings(
	struct dc_link *link,
	const union dpcd_training_lane dpcd_lane_adjust[LANE_COUNT_DP_MAX],
	uint8_t lane_count);

void dp_fixed_vs_pe_read_lane_adjust(
	struct dc_link *link,
	union dpcd_training_lane dpcd_lane_adjust[LANE_COUNT_DP_MAX]);

#endif /* __DC_LINK_DP_FIXED_VS_PE_RETIMER_H__ */
