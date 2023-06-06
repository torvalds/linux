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


#ifndef __DC_LINK_DP_TRAINING_H__
#define __DC_LINK_DP_TRAINING_H__
#include "link.h"

bool perform_link_training_with_retries(
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern,
	int attempts,
	struct pipe_ctx *pipe_ctx,
	enum signal_type signal,
	bool do_fallback);

enum link_training_result dp_perform_link_training(
		struct dc_link *link,
		const struct link_resource *link_res,
		const struct dc_link_settings *link_settings,
		bool skip_video_pattern);

bool dp_set_hw_training_pattern(
		struct dc_link *link,
		const struct link_resource *link_res,
		enum dc_dp_training_pattern pattern,
		uint32_t offset);

void dp_set_hw_test_pattern(
		struct dc_link *link,
		const struct link_resource *link_res,
		enum dp_test_pattern test_pattern,
		uint8_t *custom_pattern,
		uint32_t custom_pattern_size);

void dpcd_set_training_pattern(
	struct dc_link *link,
	enum dc_dp_training_pattern training_pattern);

/* Write DPCD drive settings. */
enum dc_status dpcd_set_lane_settings(
	struct dc_link *link,
	const struct link_training_settings *link_training_setting,
	uint32_t offset);

/* Write DPCD link configuration data. */
enum dc_status dpcd_set_link_settings(
	struct dc_link *link,
	const struct link_training_settings *lt_settings);

void dpcd_set_lt_pattern_and_lane_settings(
	struct dc_link *link,
	const struct link_training_settings *lt_settings,
	enum dc_dp_training_pattern pattern,
	uint32_t offset);

/* Read training status and adjustment requests from DPCD. */
enum dc_status dp_get_lane_status_and_lane_adjust(
	struct dc_link *link,
	const struct link_training_settings *link_training_setting,
	union lane_status ln_status[LANE_COUNT_DP_MAX],
	union lane_align_status_updated *ln_align,
	union lane_adjust ln_adjust[LANE_COUNT_DP_MAX],
	uint32_t offset);

enum dc_status dpcd_configure_lttpr_mode(
		struct dc_link *link,
		struct link_training_settings *lt_settings);

enum dc_status configure_lttpr_mode_transparent(struct dc_link *link);

enum dc_status dpcd_configure_channel_coding(
		struct dc_link *link,
		struct link_training_settings *lt_settings);

void repeater_training_done(struct dc_link *link, uint32_t offset);

void start_clock_recovery_pattern_early(struct dc_link *link,
		const struct link_resource *link_res,
		struct link_training_settings *lt_settings,
		uint32_t offset);

void dp_decide_training_settings(
		struct dc_link *link,
		const struct dc_link_settings *link_settings,
		struct link_training_settings *lt_settings);

void dp_decide_lane_settings(
	const struct link_training_settings *lt_settings,
	const union lane_adjust ln_adjust[LANE_COUNT_DP_MAX],
	struct dc_lane_settings hw_lane_settings[LANE_COUNT_DP_MAX],
	union dpcd_training_lane dpcd_lane_settings[LANE_COUNT_DP_MAX]);

enum dc_dp_training_pattern decide_cr_training_pattern(
		const struct dc_link_settings *link_settings);

enum dc_dp_training_pattern decide_eq_training_pattern(struct dc_link *link,
		const struct dc_link_settings *link_settings);

enum lttpr_mode dp_decide_lttpr_mode(struct dc_link *link,
		struct dc_link_settings *link_setting);

void dp_get_lttpr_mode_override(struct dc_link *link,
		enum lttpr_mode *override);

void override_training_settings(
		struct dc_link *link,
		const struct dc_link_training_overrides *overrides,
		struct link_training_settings *lt_settings);

/* Check DPCD training status registers to detect link loss. */
enum link_training_result dp_check_link_loss_status(
		struct dc_link *link,
		const struct link_training_settings *link_training_setting);

bool dp_is_cr_done(enum dc_lane_count ln_count,
	union lane_status *dpcd_lane_status);

bool dp_is_ch_eq_done(enum dc_lane_count ln_count,
	union lane_status *dpcd_lane_status);
bool dp_is_symbol_locked(enum dc_lane_count ln_count,
	union lane_status *dpcd_lane_status);
bool dp_is_interlane_aligned(union lane_align_status_updated align_status);

bool is_repeater(const struct link_training_settings *lt_settings, uint32_t offset);

bool dp_is_max_vs_reached(
	const struct link_training_settings *lt_settings);

uint8_t get_dpcd_link_rate(const struct dc_link_settings *link_settings);

enum link_training_result dp_get_cr_failure(enum dc_lane_count ln_count,
	union lane_status *dpcd_lane_status);

void dp_hw_to_dpcd_lane_settings(
	const struct link_training_settings *lt_settings,
	const struct dc_lane_settings hw_lane_settings[LANE_COUNT_DP_MAX],
	union dpcd_training_lane dpcd_lane_settings[LANE_COUNT_DP_MAX]);

void dp_wait_for_training_aux_rd_interval(
	struct dc_link *link,
	uint32_t wait_in_micro_secs);

enum dpcd_training_patterns
	dp_training_pattern_to_dpcd_training_pattern(
	struct dc_link *link,
	enum dc_dp_training_pattern pattern);

uint8_t dp_initialize_scrambling_data_symbols(
	struct dc_link *link,
	enum dc_dp_training_pattern pattern);

void dp_log_training_result(
	struct dc_link *link,
	const struct link_training_settings *lt_settings,
	enum link_training_result status);

uint32_t dp_translate_training_aux_read_interval(
		uint32_t dpcd_aux_read_interval);

uint8_t dp_get_nibble_at_index(const uint8_t *buf,
	uint32_t index);
#endif /* __DC_LINK_DP_TRAINING_H__ */
