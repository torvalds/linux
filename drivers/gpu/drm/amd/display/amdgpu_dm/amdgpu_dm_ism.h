/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_DM_ISM_H__
#define __AMDGPU_DM_ISM_H__

#include <linux/workqueue.h>

struct amdgpu_crtc;
struct amdgpu_display_manager;

#define AMDGPU_DM_IDLE_HIST_LEN 16

enum amdgpu_dm_ism_state {
	DM_ISM_STATE_FULL_POWER_RUNNING,
	DM_ISM_STATE_FULL_POWER_BUSY,
	DM_ISM_STATE_HYSTERESIS_WAITING,
	DM_ISM_STATE_HYSTERESIS_BUSY,
	DM_ISM_STATE_OPTIMIZED_IDLE,
	DM_ISM_STATE_OPTIMIZED_IDLE_SSO,
	DM_ISM_STATE_TIMER_ABORTED,
	DM_ISM_NUM_STATES,
};

enum amdgpu_dm_ism_event {
	DM_ISM_EVENT_IMMEDIATE,
	DM_ISM_EVENT_ENTER_IDLE_REQUESTED,
	DM_ISM_EVENT_EXIT_IDLE_REQUESTED,
	DM_ISM_EVENT_BEGIN_CURSOR_UPDATE,
	DM_ISM_EVENT_END_CURSOR_UPDATE,
	DM_ISM_EVENT_TIMER_ELAPSED,
	DM_ISM_EVENT_SSO_TIMER_ELAPSED,
	DM_ISM_NUM_EVENTS,
};

#define STATE_EVENT(state, event) (((state) << 8) | (event))

struct amdgpu_dm_ism_config {

	/**
	 * @filter_num_frames: Idle periods shorter than this number of frames
	 * will be considered a "short idle period" for filtering.
	 *
	 * 0 indicates no filtering (i.e. no idle allow delay will be applied)
	 */
	unsigned int filter_num_frames;

	/**
	 * @filter_history_size: Number of recent idle periods to consider when
	 * counting the number of short idle periods.
	 */
	unsigned int filter_history_size;

	/**
	 * @filter_entry_count: When the number of short idle periods within
	 * recent &filter_history_size reaches this count, the idle allow delay
	 * will be applied.
	 *
	 * 0 indicates no filtering (i.e. no idle allow delay will be applied)
	 */
	unsigned int filter_entry_count;

	/**
	 * @activation_num_delay_frames: Defines the number of frames to wait
	 * for the idle allow delay.
	 *
	 * 0 indicates no filtering (i.e. no idle allow delay will be applied)
	 */
	unsigned int activation_num_delay_frames;

	/**
	 * @filter_old_history_threshold: A time-based restriction on top of
	 * &filter_history_size. Idle periods older than this threshold (in
	 * number of frames) will be ignored when counting the number of short
	 * idle periods.
	 *
	 * 0 indicates no time-based restriction, i.e. history is limited only
	 * by &filter_history_size.
	 */
	unsigned int filter_old_history_threshold;

	/**
	 * @sso_num_frames: Number of frames to delay before enabling static
	 * screen optimizations, such as PSR1 and Replay low HZ idle mode.
	 *
	 * 0 indicates immediate SSO enable upon allowing idle.
	 */
	unsigned int sso_num_frames;
};

struct amdgpu_dm_ism_record {
	/**
	 * @timestamp_ns: When idle was allowed
	 */
	unsigned long long timestamp_ns;

	/**
	 * @duration_ns: How long idle was allowed
	 */
	unsigned long long duration_ns;
};

struct amdgpu_dm_ism {
	struct amdgpu_dm_ism_config config;
	unsigned long long last_idle_timestamp_ns;

	enum amdgpu_dm_ism_state current_state;
	enum amdgpu_dm_ism_state previous_state;

	struct amdgpu_dm_ism_record records[AMDGPU_DM_IDLE_HIST_LEN];
	int next_record_idx;

	struct delayed_work delayed_work;
	struct delayed_work sso_delayed_work;
};

#define ism_to_amdgpu_crtc(ism_ptr) \
	container_of(ism_ptr, struct amdgpu_crtc, ism)

void amdgpu_dm_ism_init(struct amdgpu_dm_ism *ism,
			struct amdgpu_dm_ism_config *config);
void amdgpu_dm_ism_fini(struct amdgpu_dm_ism *ism);
void amdgpu_dm_ism_commit_event(struct amdgpu_dm_ism *ism,
				enum amdgpu_dm_ism_event event);
void amdgpu_dm_ism_disable(struct amdgpu_display_manager *dm);
void amdgpu_dm_ism_enable(struct amdgpu_display_manager *dm);

#endif
