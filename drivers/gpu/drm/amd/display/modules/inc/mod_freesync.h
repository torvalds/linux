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

#ifndef MOD_FREESYNC_H_
#define MOD_FREESYNC_H_

#include "dm_services.h"

struct mod_freesync *mod_freesync_create(struct dc *dc);
void mod_freesync_destroy(struct mod_freesync *mod_freesync);

struct mod_freesync {
	int dummy;
};

enum mod_freesync_state {
	FREESYNC_STATE_NONE,
	FREESYNC_STATE_FULLSCREEN,
	FREESYNC_STATE_STATIC_SCREEN,
	FREESYNC_STATE_VIDEO
};

enum mod_freesync_user_enable_mask {
	FREESYNC_USER_ENABLE_STATIC = 0x1,
	FREESYNC_USER_ENABLE_VIDEO = 0x2,
	FREESYNC_USER_ENABLE_GAMING = 0x4
};

struct mod_freesync_user_enable {
	bool enable_for_static;
	bool enable_for_video;
	bool enable_for_gaming;
};

struct mod_freesync_caps {
	bool supported;
	unsigned int min_refresh_in_micro_hz;
	unsigned int max_refresh_in_micro_hz;

	bool btr_supported;
};

struct mod_freesync_params {
	enum mod_freesync_state state;
	bool enable;
	unsigned int update_duration_in_ns;
	bool windowed_fullscreen;
};

/*
 * Add stream to be tracked by module
 */
bool mod_freesync_add_stream(struct mod_freesync *mod_freesync,
		const struct dc_stream *stream, struct mod_freesync_caps *caps);

/*
 * Remove stream to be tracked by module
 */
bool mod_freesync_remove_stream(struct mod_freesync *mod_freesync,
		const struct dc_stream *stream);

/*
 * Update the freesync state flags for each display and program
 * freesync accordingly
 */
void mod_freesync_update_state(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams,
		struct mod_freesync_params *freesync_params);

bool mod_freesync_get_state(struct mod_freesync *mod_freesync,
		const struct dc_stream *stream,
		struct mod_freesync_params *freesync_params);

bool mod_freesync_set_user_enable(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams,
		struct mod_freesync_user_enable *user_enable);

bool mod_freesync_get_user_enable(struct mod_freesync *mod_freesync,
		const struct dc_stream *stream,
		struct mod_freesync_user_enable *user_enable);

bool mod_freesync_override_min_max(struct mod_freesync *mod_freesync,
		const struct dc_stream *streams,
		unsigned int min_refresh,
		unsigned int max_refresh);

bool mod_freesync_get_min_max(struct mod_freesync *mod_freesync,
		const struct dc_stream *stream,
		unsigned int *min_refresh,
		unsigned int *max_refresh);

bool mod_freesync_get_vmin_vmax(struct mod_freesync *mod_freesync,
		const struct dc_stream *stream,
		unsigned int *vmin,
		unsigned int *vmax);

bool mod_freesync_get_v_position(struct mod_freesync *mod_freesync,
		const struct dc_stream *stream,
		unsigned int *nom_v_pos,
		unsigned int *v_pos);

void mod_freesync_handle_v_update(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams);

void mod_freesync_notify_mode_change(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams);

void mod_freesync_pre_update_plane_addresses(struct mod_freesync *mod_freesync,
		const struct dc_stream **streams, int num_streams,
		unsigned int curr_time_stamp);

#endif
