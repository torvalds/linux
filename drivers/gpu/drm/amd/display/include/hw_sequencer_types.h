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

#ifndef __DAL_HW_SEQUENCER_TYPES_H__
#define __DAL_HW_SEQUENCER_TYPES_H__

#include "signal_types.h"
#include "grph_object_defs.h"
#include "link_service_types.h"

/* define the structure of Dynamic Refresh Mode */
struct drr_params {
	/* defines the minimum possible vertical dimension of display timing
	 * for CRTC as supported by the panel */
	uint32_t vertical_total_min;
	/* defines the maximum possible vertical dimension of display timing
	 * for CRTC as supported by the panel */
	uint32_t vertical_total_max;
};

/* CRTC timing structure */
struct hw_crtc_timing {
	uint32_t h_total;
	uint32_t h_addressable;
	uint32_t h_overscan_left;
	uint32_t h_overscan_right;
	uint32_t h_sync_start;
	uint32_t h_sync_width;

	uint32_t v_total;
	uint32_t v_addressable;
	uint32_t v_overscan_top;
	uint32_t v_overscan_bottom;
	uint32_t v_sync_start;
	uint32_t v_sync_width;

	/* in KHz */
	uint32_t pixel_clock;

	struct {
		uint32_t INTERLACED:1;
		uint32_t DOUBLESCAN:1;
		uint32_t PIXEL_REPETITION:4; /* 1...10 */
		uint32_t HSYNC_POSITIVE_POLARITY:1;
		uint32_t VSYNC_POSITIVE_POLARITY:1;
		/* frame should be packed for 3D
		 * (currently this refers to HDMI 1.4a FramePacking format */
		uint32_t HORZ_COUNT_BY_TWO:1;
		uint32_t PACK_3D_FRAME:1;
		/* 0 - left eye polarity, 1 - right eye polarity */
		uint32_t RIGHT_EYE_3D_POLARITY:1;
		/* DVI-DL High-Color mode */
		uint32_t HIGH_COLOR_DL_MODE:1;
		uint32_t Y_ONLY:1;
		/* HDMI 2.0 - Support scrambling for TMDS character
		 * rates less than or equal to 340Mcsc */
		uint32_t LTE_340MCSC_SCRAMBLE:1;
	} flags;
};

/* TODO hw_info_frame and hw_info_packet structures are same as in encoder
 * merge it*/
struct hw_info_packet {
	bool valid;
	uint8_t hb0;
	uint8_t hb1;
	uint8_t hb2;
	uint8_t hb3;
	uint8_t sb[28];
};

struct hw_info_frame {
	/* Auxiliary Video Information */
	struct hw_info_packet avi_info_packet;
	struct hw_info_packet gamut_packet;
	struct hw_info_packet vendor_info_packet;
	/* Source Product Description */
	struct hw_info_packet spd_packet;
	/* Video Stream Configuration */
	struct hw_info_packet vsc_packet;
};

#endif
