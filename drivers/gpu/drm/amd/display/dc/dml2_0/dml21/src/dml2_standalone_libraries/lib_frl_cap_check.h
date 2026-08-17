// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __LIB_FRL_CAP_CHECK_H__
#define __LIB_FRL_CAP_CHECK_H__

#include "dml2_external_lib_deps.h"

extern const int DML2_FRL_CHK_TB_BORROWED_MAX;

enum lib_frl_cap_check_pixel_encoding {
	LIB_FRL_CAP_CHECK_PIXEL_ENCODING_444,
	LIB_FRL_CAP_CHECK_PIXEL_ENCODING_422,
	LIB_FRL_CAP_CHECK_PIXEL_ENCODING_420
};

enum lib_frl_cap_check_borrow_mode {
	LIB_FRL_CAP_CHECK_BORROW_MODE_NONE,
	LIB_FRL_CAP_CHECK_BORROW_MODE_FROM_ACTIVE,
	LIB_FRL_CAP_CHECK_BORROW_MODE_FROM_BLANK
};

enum lib_frl_cap_check_status {
	LIB_FRL_CAP_CHECK_OK = 0,

	LIB_FRL_CAP_CHECK_ERROR_AUDIO_BW = -1,
	LIB_FRL_CAP_CHECK_ERROR_BORROW = -2,
	LIB_FRL_CAP_CHECK_ERROR_MAX_BORROW = -3,
	LIB_FRL_CAP_CHECK_ERROR_MARGIN = -4,

	LIB_FRL_CAP_CHECK_ERROR_UNSUPPORTED_AUDIO = -1000
};

struct lib_frl_cap_check_intermediates {
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
};

struct lib_frl_cap_check_params {
	int lanes;
	double f_pixel_clock_nominal; /* Pixel Clock rate (Hz) */
	double r_bit_nominal; /* FRL bitrate (bps) */
	int audio_packet_type;
	double f_audio; /* Audio rate (Hz) */
	int h_active; /* Active pixels per line */
	int h_blank; /* Blanking pixels per line */
	int bpc; /* Bits per component */

	enum lib_frl_cap_check_pixel_encoding pixel_encoding;

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

	enum lib_frl_cap_check_borrow_mode borrow_mode;
};

enum lib_frl_cap_check_status frl_cap_check(struct lib_frl_cap_check_params *params);
enum lib_frl_cap_check_status frl_cap_check_intermediates(struct lib_frl_cap_check_params *params, struct lib_frl_cap_check_intermediates *inter);

#endif
