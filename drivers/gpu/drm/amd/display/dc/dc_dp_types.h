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

#ifndef DC_DP_TYPES_H
#define DC_DP_TYPES_H

enum dc_lane_count {
	LANE_COUNT_UNKNOWN = 0,
	LANE_COUNT_ONE = 1,
	LANE_COUNT_TWO = 2,
	LANE_COUNT_FOUR = 4,
	LANE_COUNT_EIGHT = 8,
	LANE_COUNT_DP_MAX = LANE_COUNT_FOUR
};

/* This is actually a reference clock (27MHz) multiplier
 * 162MBps bandwidth for 1.62GHz like rate,
 * 270MBps for 2.70GHz,
 * 324MBps for 3.24Ghz,
 * 540MBps for 5.40GHz
 * 810MBps for 8.10GHz
 */
enum dc_link_rate {
	LINK_RATE_UNKNOWN = 0,
	LINK_RATE_LOW = 0x06,
	LINK_RATE_HIGH = 0x0A,
	LINK_RATE_RBR2 = 0x0C,
	LINK_RATE_HIGH2 = 0x14,
	LINK_RATE_HIGH3 = 0x1E
};

enum dc_link_spread {
	LINK_SPREAD_DISABLED = 0x00,
	/* 0.5 % downspread 30 kHz */
	LINK_SPREAD_05_DOWNSPREAD_30KHZ = 0x10,
	/* 0.5 % downspread 33 kHz */
	LINK_SPREAD_05_DOWNSPREAD_33KHZ = 0x11
};

enum dc_voltage_swing {
	VOLTAGE_SWING_LEVEL0 = 0,	/* direct HW translation! */
	VOLTAGE_SWING_LEVEL1,
	VOLTAGE_SWING_LEVEL2,
	VOLTAGE_SWING_LEVEL3,
	VOLTAGE_SWING_MAX_LEVEL = VOLTAGE_SWING_LEVEL3
};

enum dc_pre_emphasis {
	PRE_EMPHASIS_DISABLED = 0,	/* direct HW translation! */
	PRE_EMPHASIS_LEVEL1,
	PRE_EMPHASIS_LEVEL2,
	PRE_EMPHASIS_LEVEL3,
	PRE_EMPHASIS_MAX_LEVEL = PRE_EMPHASIS_LEVEL3
};
/* Post Cursor 2 is optional for transmitter
 * and it applies only to the main link operating at HBR2
 */
enum dc_post_cursor2 {
	POST_CURSOR2_DISABLED = 0,	/* direct HW translation! */
	POST_CURSOR2_LEVEL1,
	POST_CURSOR2_LEVEL2,
	POST_CURSOR2_LEVEL3,
	POST_CURSOR2_MAX_LEVEL = POST_CURSOR2_LEVEL3,
};

struct dc_link_settings {
	enum dc_lane_count lane_count;
	enum dc_link_rate link_rate;
	enum dc_link_spread link_spread;
};

struct dc_lane_settings {
	enum dc_voltage_swing VOLTAGE_SWING;
	enum dc_pre_emphasis PRE_EMPHASIS;
	enum dc_post_cursor2 POST_CURSOR2;
};

struct dc_link_training_settings {
	struct dc_link_settings link;
	struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX];
};

#endif /* DC_DP_TYPES_H */
