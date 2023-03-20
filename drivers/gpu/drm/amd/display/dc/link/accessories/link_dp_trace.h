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
#ifndef __LINK_DP_TRACE_H__
#define __LINK_DP_TRACE_H__
#include "link.h"

void dp_trace_init(struct dc_link *link);
void dp_trace_reset(struct dc_link *link);
bool dp_trace_is_initialized(struct dc_link *link);
void dp_trace_detect_lt_init(struct dc_link *link);
void dp_trace_commit_lt_init(struct dc_link *link);
void dp_trace_link_loss_increment(struct dc_link *link);
void dp_trace_lt_fail_count_update(struct dc_link *link,
		unsigned int fail_count,
		bool in_detection);
void dp_trace_lt_total_count_increment(struct dc_link *link,
		bool in_detection);
void dp_trace_set_is_logged_flag(struct dc_link *link,
		bool in_detection,
		bool is_logged);
bool dp_trace_is_logged(struct dc_link *link,
		bool in_detection);
void dp_trace_lt_result_update(struct dc_link *link,
		enum link_training_result result,
		bool in_detection);
void dp_trace_set_lt_start_timestamp(struct dc_link *link,
		bool in_detection);
void dp_trace_set_lt_end_timestamp(struct dc_link *link,
		bool in_detection);
unsigned long long dp_trace_get_lt_end_timestamp(struct dc_link *link,
		bool in_detection);
const struct dp_trace_lt_counts *dp_trace_get_lt_counts(struct dc_link *link,
		bool in_detection);
unsigned int dp_trace_get_link_loss_count(struct dc_link *link);
void dp_trace_set_edp_power_timestamp(struct dc_link *link,
		bool power_up);
uint64_t dp_trace_get_edp_poweron_timestamp(struct dc_link *link);
uint64_t dp_trace_get_edp_poweroff_timestamp(struct dc_link *link);
void dp_trace_source_sequence(struct dc_link *link, uint8_t dp_test_mode);

#endif /* __LINK_DP_TRACE_H__ */
