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

#ifndef __DML_FRL_CAP_CHK_H__
#define __DML_FRL_CAP_CHK_H__

#include "os_types.h"

typedef enum {
	PIXEL_ENCODING_444,
	PIXEL_ENCODING_422,
	PIXEL_ENCODING_420
} enum_pixel_encoding;

typedef enum {
	BORROW_MODE_NONE,
	BORROW_MODE_FROM_ACTIVE,
	BORROW_MODE_FROM_BLANK
} enum_borrow_mode;

typedef enum {
	FRL_CAP_CHK_OK = 0,

	FRL_CAP_CHK_ERROR_AUDIO_BW = -1,
	FRL_CAP_CHK_ERROR_BORROW = -2,
	FRL_CAP_CHK_ERROR_MAX_BORROW = -3,
	FRL_CAP_CHK_ERROR_MARGIN = -4,

	FRL_CAP_CHK_ERROR_UNSUPPORTED_AUDIO = -1000
} frl_cap_chk_result;

typedef struct {
	int c_frl_sb;
	double overhead_sb;
	double overhead_rs;
	double overhead_map;
	double overhead_min;
	double overhead_max;
	double f_pixel_clock_max;
	double t_line;
	double r_bit_min;
	double r_frl_char_min;
	double c_frl_line;
	double ap;
	double r_ap;
	double avg_audio_packets_line;
	int audio_packets_line;
	int blank_audio_min;
} frl_cap_chk_intermediates;

typedef struct {
	int lanes;
	double f_pixel_clock_nominal; /* Pixel Clock rate (Hz) */
	double r_bit_nominal; /* FRL bitrate (bps) */
	int audio_packet_type;
	double f_audio; /* Audio rate (Hz) */
	int h_active; /* Active pixels per line */
	int h_blank; /* Blanking pixels per line */
	int bpc; /* Bits per component */

	enum_pixel_encoding pixel_encoding;

	bool compressed;
	bool bypass_hc_target_calc;

	/* DSC parameters */
	int slices;
	int slice_width;
	double bpp_target;

	int layout; /* not supported */
	int acat; /* not supported */

	/* outputs */
	int audio_packets_line;

	/* inputs or outputs */
	int hc_active_target;
	int hc_blank_target;

	enum_borrow_mode borrow_mode;
} frl_cap_chk_params;

frl_cap_chk_result frl_cap_chk(frl_cap_chk_params *params);
frl_cap_chk_result frl_cap_chk_inter(frl_cap_chk_params *params, frl_cap_chk_intermediates *inter);

#endif
