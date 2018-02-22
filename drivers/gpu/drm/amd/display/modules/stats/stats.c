/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include "mod_stats.h"
#include "dm_services.h"
#include "dc.h"
#include "core_types.h"

#define DAL_STATS_ENABLE_REGKEY			"DalStatsEnable"
#define DAL_STATS_ENABLE_REGKEY_DEFAULT		0x00000001
#define DAL_STATS_ENABLE_REGKEY_ENABLED		0x00000001

#define DAL_STATS_ENTRIES_REGKEY		"DalStatsEntries"
#define DAL_STATS_ENTRIES_REGKEY_DEFAULT	0x00350000
#define DAL_STATS_ENTRIES_REGKEY_MAX		0x01000000

#define MOD_STATS_NUM_VSYNCS			5

struct stats_time_cache {
	unsigned long flip_timestamp_in_ns;
	unsigned long vupdate_timestamp_in_ns;

	unsigned int render_time_in_us;
	unsigned int avg_render_time_in_us_last_ten;
	unsigned int v_sync_time_in_us[MOD_STATS_NUM_VSYNCS];
	unsigned int num_vsync_between_flips;

	unsigned int flip_to_vsync_time_in_us;
	unsigned int vsync_to_flip_time_in_us;

	unsigned int min_window;
	unsigned int max_window;
	unsigned int v_total_min;
	unsigned int v_total_max;
	unsigned int event_triggers;

	unsigned int lfc_mid_point_in_us;
	unsigned int num_frames_inserted;
	unsigned int inserted_duration_in_us;

	unsigned int flags;
};

struct core_stats {
	struct mod_stats public;
	struct dc *dc;

	struct stats_time_cache *time;
	unsigned int index;

	bool enabled;
	unsigned int entries;
};

#define MOD_STATS_TO_CORE(mod_stats)\
		container_of(mod_stats, struct core_stats, public)

bool mod_stats_init(struct mod_stats *mod_stats)
{
	bool result = false;
	struct core_stats *core_stats = NULL;
	struct dc *dc = NULL;

	if (mod_stats == NULL)
		return false;

	core_stats = MOD_STATS_TO_CORE(mod_stats);
	dc = core_stats->dc;

	return result;
}

struct mod_stats *mod_stats_create(struct dc *dc)
{
	struct core_stats *core_stats = NULL;
	struct persistent_data_flag flag;
	unsigned int reg_data;
	int i = 0;

	core_stats = kzalloc(sizeof(struct core_stats), GFP_KERNEL);

	if (core_stats == NULL)
		goto fail_alloc_context;

	if (dc == NULL)
		goto fail_construct;

	core_stats->dc = dc;

	core_stats->enabled = DAL_STATS_ENABLE_REGKEY_DEFAULT;
	if (dm_read_persistent_data(dc->ctx, NULL, NULL,
			DAL_STATS_ENABLE_REGKEY,
			&reg_data, sizeof(unsigned int), &flag))
		core_stats->enabled = reg_data;

	core_stats->entries = DAL_STATS_ENTRIES_REGKEY_DEFAULT;
	if (dm_read_persistent_data(dc->ctx, NULL, NULL,
			DAL_STATS_ENTRIES_REGKEY,
			&reg_data, sizeof(unsigned int), &flag)) {
		if (reg_data > DAL_STATS_ENTRIES_REGKEY_MAX)
			core_stats->entries = DAL_STATS_ENTRIES_REGKEY_MAX;
		else
			core_stats->entries = reg_data;
	}

	core_stats->time = kzalloc(sizeof(struct stats_time_cache) * core_stats->entries,
					GFP_KERNEL);

	if (core_stats->time == NULL)
		goto fail_construct;

	/* Purposely leave index 0 unused so we don't need special logic to
	 * handle calculation cases that depend on previous flip data.
	 */
	core_stats->index = 1;

	return &core_stats->public;

fail_construct:
	kfree(core_stats);

fail_alloc_context:
	return NULL;
}

void mod_stats_destroy(struct mod_stats *mod_stats)
{
	if (mod_stats != NULL) {
		struct core_stats *core_stats = MOD_STATS_TO_CORE(mod_stats);

		if (core_stats->time != NULL)
			kfree(core_stats->time);

		kfree(core_stats);
	}
}

void mod_stats_dump(struct mod_stats *mod_stats)
{
	struct dc  *dc = NULL;
	struct dal_logger *logger = NULL;
	struct core_stats *core_stats = NULL;
	struct stats_time_cache *time = NULL;
	unsigned int index = 0;

	if (mod_stats == NULL)
		return;

	core_stats = MOD_STATS_TO_CORE(mod_stats);
	dc = core_stats->dc;
	logger = dc->ctx->logger;
	time = core_stats->time;

	//LogEntry* pLog = GetLog()->Open(LogMajor_ISR, LogMinor_ISR_FreeSyncSW);

	//if (!pLog->IsDummyEntry())
	{
		dm_logger_write(logger, LOG_PROFILING, "==Display Caps==\n");
		dm_logger_write(logger, LOG_PROFILING, "\n");
		dm_logger_write(logger, LOG_PROFILING, "\n");

		dm_logger_write(logger, LOG_PROFILING, "==Stats==\n");
		dm_logger_write(logger, LOG_PROFILING,
			"render avgRender minWindow midPoint maxWindow vsyncToFlip flipToVsync #vsyncBetweenFlip #frame insertDuration vTotalMin vTotalMax eventTrigs vSyncTime1 vSyncTime2 vSyncTime3 vSyncTime4 vSyncTime5 flags\n");

		for (int i = 0; i < core_stats->index && i < core_stats->entries; i++) {
			dm_logger_write(logger, LOG_PROFILING,
					"%u  %u  %u  %u  %u  %u  %u  %u  %u  %u  %u  %u  %u  %u  %u  %u  %u  %u  %u\n",
					time[i].render_time_in_us,
					time[i].avg_render_time_in_us_last_ten,
					time[i].min_window,
					time[i].lfc_mid_point_in_us,
					time[i].max_window,
					time[i].vsync_to_flip_time_in_us,
					time[i].flip_to_vsync_time_in_us,
					time[i].num_vsync_between_flips,
					time[i].num_frames_inserted,
					time[i].inserted_duration_in_us,
					time[i].v_total_min,
					time[i].v_total_max,
					time[i].event_triggers,
					time[i].v_sync_time_in_us[0],
					time[i].v_sync_time_in_us[1],
					time[i].v_sync_time_in_us[2],
					time[i].v_sync_time_in_us[3],
					time[i].v_sync_time_in_us[4],
					time[i].flags);
		}
	}
	//GetLog()->Close(pLog);
	//GetLog()->UnSetLogMask(LogMajor_ISR, LogMinor_ISR_FreeSyncSW);
}

void mod_stats_reset_data(struct mod_stats *mod_stats)
{
	struct core_stats *core_stats = NULL;
	struct stats_time_cache *time = NULL;
	unsigned int index = 0;

	if (mod_stats == NULL)
		return;

	core_stats = MOD_STATS_TO_CORE(mod_stats);

	memset(core_stats->time, 0,
		sizeof(struct stats_time_cache) * core_stats->entries);

	core_stats->index = 0;
}

void mod_stats_update_flip(struct mod_stats *mod_stats,
		unsigned long timestamp_in_ns)
{
	struct core_stats *core_stats = NULL;
	struct stats_time_cache *time = NULL;
	unsigned int index = 0;

	if (mod_stats == NULL)
		return;

	core_stats = MOD_STATS_TO_CORE(mod_stats);

	if (core_stats->index >= core_stats->entries)
		return;

	time = core_stats->time;
	index = core_stats->index;

	time[index].flip_timestamp_in_ns = timestamp_in_ns;
	time[index].render_time_in_us =
		timestamp_in_ns - time[index - 1].flip_timestamp_in_ns;

	if (index >= 10) {
		for (unsigned int i = 0; i < 10; i++)
			time[index].avg_render_time_in_us_last_ten +=
					time[index - i].render_time_in_us;
		time[index].avg_render_time_in_us_last_ten /= 10;
	}

	if (time[index].num_vsync_between_flips > 0)
		time[index].vsync_to_flip_time_in_us =
			timestamp_in_ns - time[index].vupdate_timestamp_in_ns;
	else
		time[index].vsync_to_flip_time_in_us =
			timestamp_in_ns - time[index - 1].vupdate_timestamp_in_ns;

	core_stats->index++;
}

void mod_stats_update_vupdate(struct mod_stats *mod_stats,
		unsigned long timestamp_in_ns)
{
	struct core_stats *core_stats = NULL;
	struct stats_time_cache *time = NULL;
	unsigned int index = 0;

	if (mod_stats == NULL)
		return;

	core_stats = MOD_STATS_TO_CORE(mod_stats);

	if (core_stats->index >= core_stats->entries)
		return;

	time = core_stats->time;
	index = core_stats->index;

	time[index].vupdate_timestamp_in_ns = timestamp_in_ns;
	if (time[index].num_vsync_between_flips < MOD_STATS_NUM_VSYNCS)
		time[index].v_sync_time_in_us[time[index].num_vsync_between_flips] =
			timestamp_in_ns - time[index - 1].vupdate_timestamp_in_ns;
	time[index].flip_to_vsync_time_in_us =
		timestamp_in_ns - time[index - 1].flip_timestamp_in_ns;

	time[index].num_vsync_between_flips++;
}

void mod_stats_update_freesync(struct mod_stats *mod_stats,
		unsigned int v_total_min,
		unsigned int v_total_max,
		unsigned int event_triggers,
		unsigned int window_min,
		unsigned int window_max,
		unsigned int lfc_mid_point_in_us,
		unsigned int inserted_frames,
		unsigned int inserted_duration_in_us)
{
	struct core_stats *core_stats = NULL;
	struct stats_time_cache *time = NULL;
	unsigned int index = 0;

	if (mod_stats == NULL)
		return;

	core_stats = MOD_STATS_TO_CORE(mod_stats);

	if (core_stats->index >= core_stats->entries)
		return;

	time = core_stats->time;
	index = core_stats->index;

	time[index].v_total_min = v_total_min;
	time[index].v_total_max = v_total_max;
	time[index].event_triggers = event_triggers;
	time[index].min_window = window_min;
	time[index].max_window = window_max;
	time[index].lfc_mid_point_in_us = lfc_mid_point_in_us;
	time[index].num_frames_inserted = inserted_frames;
	time[index].inserted_duration_in_us = inserted_duration_in_us;
}

