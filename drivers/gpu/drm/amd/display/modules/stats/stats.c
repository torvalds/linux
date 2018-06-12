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

#define DAL_STATS_EVENT_ENTRIES_DEFAULT		0x00000100

#define MOD_STATS_NUM_VSYNCS			5
#define MOD_STATS_EVENT_STRING_MAX		512

struct stats_time_cache {
	unsigned int entry_id;

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

struct stats_event_cache {
	unsigned int entry_id;
	char event_string[MOD_STATS_EVENT_STRING_MAX];
};

struct core_stats {
	struct mod_stats public;
	struct dc *dc;

	bool enabled;
	unsigned int entries;
	unsigned int event_entries;
	unsigned int entry_id;

	struct stats_time_cache *time;
	unsigned int index;

	struct stats_event_cache *events;
	unsigned int event_index;

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

	if (dc == NULL)
		goto fail_construct;

	core_stats = kzalloc(sizeof(struct core_stats), GFP_KERNEL);

	if (core_stats == NULL)
		goto fail_construct;

	core_stats->dc = dc;

	core_stats->enabled = DAL_STATS_ENABLE_REGKEY_DEFAULT;
	if (dm_read_persistent_data(dc->ctx, NULL, NULL,
			DAL_STATS_ENABLE_REGKEY,
			&reg_data, sizeof(unsigned int), &flag))
		core_stats->enabled = reg_data;

	if (core_stats->enabled) {
		core_stats->entries = DAL_STATS_ENTRIES_REGKEY_DEFAULT;
		if (dm_read_persistent_data(dc->ctx, NULL, NULL,
				DAL_STATS_ENTRIES_REGKEY,
				&reg_data, sizeof(unsigned int), &flag)) {
			if (reg_data > DAL_STATS_ENTRIES_REGKEY_MAX)
				core_stats->entries = DAL_STATS_ENTRIES_REGKEY_MAX;
			else
				core_stats->entries = reg_data;
		}
		core_stats->time = kcalloc(core_stats->entries,
						sizeof(struct stats_time_cache),
						GFP_KERNEL);

		if (core_stats->time == NULL)
			goto fail_construct_time;

		core_stats->event_entries = DAL_STATS_EVENT_ENTRIES_DEFAULT;
		core_stats->events = kcalloc(core_stats->event_entries,
					     sizeof(struct stats_event_cache),
					     GFP_KERNEL);

		if (core_stats->events == NULL)
			goto fail_construct_events;

	} else {
		core_stats->entries = 0;
	}

	/* Purposely leave index 0 unused so we don't need special logic to
	 * handle calculation cases that depend on previous flip data.
	 */
	core_stats->index = 1;
	core_stats->event_index = 0;

	// Keeps track of ordering within the different stats structures
	core_stats->entry_id = 0;

	return &core_stats->public;

fail_construct_events:
	kfree(core_stats->time);

fail_construct_time:
	kfree(core_stats);

fail_construct:
	return NULL;
}

void mod_stats_destroy(struct mod_stats *mod_stats)
{
	if (mod_stats != NULL) {
		struct core_stats *core_stats = MOD_STATS_TO_CORE(mod_stats);

		if (core_stats->time != NULL)
			kfree(core_stats->time);

		if (core_stats->events != NULL)
			kfree(core_stats->events);

		kfree(core_stats);
	}
}

void mod_stats_dump(struct mod_stats *mod_stats)
{
	struct dc  *dc = NULL;
	struct dal_logger *logger = NULL;
	struct core_stats *core_stats = NULL;
	struct stats_time_cache *time = NULL;
	struct stats_event_cache *events = NULL;
	unsigned int time_index = 1;
	unsigned int event_index = 0;
	unsigned int index = 0;
	struct log_entry log_entry;

	if (mod_stats == NULL)
		return;

	core_stats = MOD_STATS_TO_CORE(mod_stats);
	dc = core_stats->dc;
	logger = dc->ctx->logger;
	time = core_stats->time;
	events = core_stats->events;

	DISPLAY_STATS_BEGIN(log_entry);

	DISPLAY_STATS("==Display Caps==\n");

	DISPLAY_STATS("==Display Stats==\n");

	DISPLAY_STATS("%10s %10s %10s %10s %10s"
			" %11s %11s %17s %10s %14s"
			" %10s %10s %10s %10s %10s"
			" %10s %10s %10s %10s\n",
		"render", "avgRender",
		"minWindow", "midPoint", "maxWindow",
		"vsyncToFlip", "flipToVsync", "vsyncsBetweenFlip",
		"numFrame", "insertDuration",
		"vTotalMin", "vTotalMax", "eventTrigs",
		"vSyncTime1", "vSyncTime2", "vSyncTime3",
		"vSyncTime4", "vSyncTime5", "flags");

	for (int i = 0; i < core_stats->entry_id; i++) {
		if (event_index < core_stats->event_index &&
				i == events[event_index].entry_id) {
			DISPLAY_STATS("%s\n", events[event_index].event_string);
			event_index++;
		} else if (time_index < core_stats->index &&
				i == time[time_index].entry_id) {
			DISPLAY_STATS("%10u %10u %10u %10u %10u"
					" %11u %11u %17u %10u %14u"
					" %10u %10u %10u %10u %10u"
					" %10u %10u %10u %10u\n",
				time[time_index].render_time_in_us,
				time[time_index].avg_render_time_in_us_last_ten,
				time[time_index].min_window,
				time[time_index].lfc_mid_point_in_us,
				time[time_index].max_window,
				time[time_index].vsync_to_flip_time_in_us,
				time[time_index].flip_to_vsync_time_in_us,
				time[time_index].num_vsync_between_flips,
				time[time_index].num_frames_inserted,
				time[time_index].inserted_duration_in_us,
				time[time_index].v_total_min,
				time[time_index].v_total_max,
				time[time_index].event_triggers,
				time[time_index].v_sync_time_in_us[0],
				time[time_index].v_sync_time_in_us[1],
				time[time_index].v_sync_time_in_us[2],
				time[time_index].v_sync_time_in_us[3],
				time[time_index].v_sync_time_in_us[4],
				time[time_index].flags);

			time_index++;
		}
	}

	DISPLAY_STATS_END(log_entry);
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

	memset(core_stats->events, 0,
		sizeof(struct stats_event_cache) * core_stats->event_entries);

	core_stats->index = 1;
	core_stats->event_index = 0;

	// Keeps track of ordering within the different stats structures
	core_stats->entry_id = 0;
}

void mod_stats_update_event(struct mod_stats *mod_stats,
		char *event_string,
		unsigned int length)
{
	struct core_stats *core_stats = NULL;
	struct stats_event_cache *events = NULL;
	unsigned int index = 0;
	unsigned int copy_length = 0;

	if (mod_stats == NULL)
		return;

	core_stats = MOD_STATS_TO_CORE(mod_stats);

	if (core_stats->event_index >= core_stats->event_entries)
		return;

	events = core_stats->events;
	index = core_stats->event_index;

	copy_length = length;
	if (length > MOD_STATS_EVENT_STRING_MAX)
		copy_length = MOD_STATS_EVENT_STRING_MAX;

	memcpy(&events[index].event_string, event_string, copy_length);
	events[index].event_string[copy_length - 1] = '\0';

	events[index].entry_id = core_stats->entry_id;
	core_stats->event_index++;
	core_stats->entry_id++;
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
		(timestamp_in_ns - time[index - 1].flip_timestamp_in_ns) / 1000;

	if (index >= 10) {
		for (unsigned int i = 0; i < 10; i++)
			time[index].avg_render_time_in_us_last_ten +=
					time[index - i].render_time_in_us;
		time[index].avg_render_time_in_us_last_ten /= 10;
	}

	if (time[index].num_vsync_between_flips > 0)
		time[index].vsync_to_flip_time_in_us =
			(timestamp_in_ns -
				time[index].vupdate_timestamp_in_ns) / 1000;
	else
		time[index].vsync_to_flip_time_in_us =
			(timestamp_in_ns -
				time[index - 1].vupdate_timestamp_in_ns) / 1000;

	time[index].entry_id = core_stats->entry_id;
	core_stats->index++;
	core_stats->entry_id++;
}

void mod_stats_update_vupdate(struct mod_stats *mod_stats,
		unsigned long timestamp_in_ns)
{
	struct core_stats *core_stats = NULL;
	struct stats_time_cache *time = NULL;
	unsigned int index = 0;
	unsigned int num_vsyncs = 0;
	unsigned int prev_vsync_in_ns = 0;

	if (mod_stats == NULL)
		return;

	core_stats = MOD_STATS_TO_CORE(mod_stats);

	if (core_stats->index >= core_stats->entries)
		return;

	time = core_stats->time;
	index = core_stats->index;
	num_vsyncs = time[index].num_vsync_between_flips;

	if (num_vsyncs < MOD_STATS_NUM_VSYNCS) {
		if (num_vsyncs == 0) {
			prev_vsync_in_ns =
				time[index - 1].vupdate_timestamp_in_ns;

			time[index].flip_to_vsync_time_in_us =
				(timestamp_in_ns -
					time[index - 1].flip_timestamp_in_ns) /
					1000;
		} else {
			prev_vsync_in_ns =
				time[index].vupdate_timestamp_in_ns;
		}

		time[index].v_sync_time_in_us[num_vsyncs] =
			(timestamp_in_ns - prev_vsync_in_ns) / 1000;
	}

	time[index].vupdate_timestamp_in_ns = timestamp_in_ns;
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

