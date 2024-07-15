/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __AUDIO_TYPES_H__
#define __AUDIO_TYPES_H__

#include "signal_types.h"
#include "fixed31_32.h"
#include "dc_dp_types.h"

#define AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS 20
#define MAX_HW_AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS 18
#define MULTI_CHANNEL_SPLIT_NO_ASSO_INFO 0xFFFFFFFF

struct audio_dp_link_info {
	uint32_t link_bandwidth_kbps;
	uint32_t hblank_min_symbol_width;
	enum dp_link_encoding encoding;
	enum dc_link_rate link_rate;
	enum dc_lane_count lane_count;
	bool is_mst;
};

struct audio_crtc_info {
	uint32_t h_total;
	uint32_t h_active;
	uint32_t v_active;
	uint32_t pixel_repetition;
	uint32_t requested_pixel_clock_100Hz; /* in 100Hz */
	uint32_t calculated_pixel_clock_100Hz; /* in 100Hz */
	uint32_t refresh_rate;
	enum dc_color_depth color_depth;
	enum dc_pixel_encoding pixel_encoding;
	bool interlaced;
	uint32_t dsc_bits_per_pixel;
	uint32_t dsc_num_slices;
};
struct azalia_clock_info {
	uint32_t pixel_clock_in_10khz;
	uint32_t audio_dto_phase;
	uint32_t audio_dto_module;
	uint32_t audio_dto_wall_clock_ratio;
};

enum audio_dto_source {
	DTO_SOURCE_UNKNOWN = 0,
	DTO_SOURCE_ID0,
	DTO_SOURCE_ID1,
	DTO_SOURCE_ID2,
	DTO_SOURCE_ID3,
	DTO_SOURCE_ID4,
	DTO_SOURCE_ID5
};

/* PLL information required for AZALIA DTO calculation */

struct audio_pll_info {
	uint32_t audio_dto_source_clock_in_khz;
	uint32_t feed_back_divider;
	enum audio_dto_source dto_source;
	bool ss_enabled;
	uint32_t ss_percentage;
	uint32_t ss_percentage_divider;
};

struct audio_channel_associate_info {
	union {
		struct {
			uint32_t ALL_CHANNEL_FL:4;
			uint32_t ALL_CHANNEL_FR:4;
			uint32_t ALL_CHANNEL_FC:4;
			uint32_t ALL_CHANNEL_Sub:4;
			uint32_t ALL_CHANNEL_SL:4;
			uint32_t ALL_CHANNEL_SR:4;
			uint32_t ALL_CHANNEL_BL:4;
			uint32_t ALL_CHANNEL_BR:4;
		} bits;
		uint32_t u32all;
	};
};

struct audio_output {
	/* Front DIG id. */
	enum engine_id engine_id;
	/* encoder output signal */
	enum signal_type signal;
	/* video timing */
	struct audio_crtc_info crtc_info;
	/* DP link info */
	struct audio_dp_link_info dp_link_info;
	/* PLL for audio */
	struct audio_pll_info pll_info;
};

enum audio_payload {
	CHANNEL_SPLIT_MAPPINGCHANG = 0x9,
};

#endif /* __AUDIO_TYPES_H__ */
