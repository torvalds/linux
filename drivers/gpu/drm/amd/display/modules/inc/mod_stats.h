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

#ifndef MODULES_INC_MOD_STATS_H_
#define MODULES_INC_MOD_STATS_H_

#include "dm_services.h"

struct mod_stats {
	int dummy;
};

struct mod_stats_caps {
	bool dummy;
};

struct mod_stats_init_params {
	unsigned int stats_enable;
	unsigned int stats_entries;
};

struct mod_stats *mod_stats_create(struct dc *dc,
		struct mod_stats_init_params *init_params);

void mod_stats_destroy(struct mod_stats *mod_stats);

bool mod_stats_init(struct mod_stats *mod_stats);

void mod_stats_dump(struct mod_stats *mod_stats);

void mod_stats_reset_data(struct mod_stats *mod_stats);

void mod_stats_update_event(struct mod_stats *mod_stats,
		char *event_string,
		unsigned int length);

void mod_stats_update_flip(struct mod_stats *mod_stats,
		unsigned long long timestamp_in_ns);

void mod_stats_update_vupdate(struct mod_stats *mod_stats,
		unsigned long long timestamp_in_ns);

void mod_stats_update_freesync(struct mod_stats *mod_stats,
		unsigned int v_total_min,
		unsigned int v_total_max,
		unsigned int event_triggers,
		unsigned int window_min,
		unsigned int window_max,
		unsigned int lfc_mid_point_in_us,
		unsigned int inserted_frames,
		unsigned int inserted_frame_duration_in_us);

#endif /* MODULES_INC_MOD_STATS_H_ */
