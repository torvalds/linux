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

#ifndef __DAL_SET_MODE_TYPES_H__
#define __DAL_SET_MODE_TYPES_H__

#include "dc_types.h"
#include <linux/hdmi.h>

/* Info frame packet status */
enum info_frame_flag {
	INFO_PACKET_PACKET_INVALID = 0,
	INFO_PACKET_PACKET_VALID = 1,
	INFO_PACKET_PACKET_RESET = 2,
	INFO_PACKET_PACKET_UPDATE_SCAN_TYPE = 8
};

struct hdmi_info_frame_header {
	uint8_t info_frame_type;
	uint8_t version;
	uint8_t length;
};

#pragma pack(push)
#pragma pack(1)

struct info_packet_raw_data {
	uint8_t hb0;
	uint8_t hb1;
	uint8_t hb2;
	uint8_t sb[28]; /* sb0~sb27 */
};

union hdmi_info_packet {
	struct avi_info_frame {
		struct hdmi_info_frame_header header;

		uint8_t CHECK_SUM:8;

		uint8_t S0_S1:2;
		uint8_t B0_B1:2;
		uint8_t A0:1;
		uint8_t Y0_Y1_Y2:3;

		uint8_t R0_R3:4;
		uint8_t M0_M1:2;
		uint8_t C0_C1:2;

		uint8_t SC0_SC1:2;
		uint8_t Q0_Q1:2;
		uint8_t EC0_EC2:3;
		uint8_t ITC:1;

		uint8_t VIC0_VIC7:8;

		uint8_t PR0_PR3:4;
		uint8_t CN0_CN1:2;
		uint8_t YQ0_YQ1:2;

		uint16_t bar_top;
		uint16_t bar_bottom;
		uint16_t bar_left;
		uint16_t bar_right;

		uint8_t FR0_FR3:4;
		uint8_t ACE0_ACE3:4;

		uint8_t RID0_RID5:6;
		uint8_t FR4:1;
		uint8_t F157:1;

		uint8_t reserved[12];
	} bits;

	struct info_packet_raw_data packet_raw_data;
};

#pragma pack(pop)

#endif /* __DAL_SET_MODE_TYPES_H__ */
