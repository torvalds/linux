/* SPDX-License-Identifier: MIT */
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

#ifndef __DML1_FRL_CAP_CHK_H__
#define __DML1_FRL_CAP_CHK_H__

#include "os_types.h"

#define TB_BORROWED_MAX	  400
#define C_FRL_CB	  510
#define TOLERANCE_FRL_BIT 300    /* ppm */
#define ACR_RATE_MAX	  1500

enum hdmi_frl_pixel_encoding {
	HDMI_FRL_PIXEL_ENCODING_444,
	HDMI_FRL_PIXEL_ENCODING_422,
	HDMI_FRL_PIXEL_ENCODING_420
};

enum frl_cap_chk_result {
	FRL_CAP_CHK_OK = 0,

	FRL_CAP_CHK_ERROR_AUDIO_BW   = -1,
	FRL_CAP_CHK_ERROR_BORROW     = -2,
	FRL_CAP_CHK_ERROR_MAX_BORROW = -3,
	FRL_CAP_CHK_ERROR_MARGIN     = -4,

	FRL_CAP_CHK_ERROR_UNSUPPORTED_AUDIO = -1000
};

enum frl_borrow_mode {
	FRL_BORROW_MODE_NONE,
	FRL_BORROW_MODE_FROM_ACTIVE,
	FRL_BORROW_MODE_FROM_BLANK
};

enum frl_link_rate {
	FRL_LINK_RATE_DISABLE = 0,
	FRL_LINK_RATE_3GBPS,
	FRL_LINK_RATE_6GBPS,
	FRL_LINK_RATE_6GBPS_4LANE,
	FRL_LINK_RATE_8GBPS,
	FRL_LINK_RATE_10GBPS,
	FRL_LINK_RATE_12GBPS,
	FRL_LINK_RATE_16GBPS,
	FRL_LINK_RATE_20GBPS,
	FRL_LINK_RATE_24GBPS
};

struct frl_dml_borrow_params {
	int audio_packets_line;
	int hc_active_target;
	int hc_blank_target;
	enum frl_borrow_mode borrow_mode;
};

struct frl_primary_format {
	uint32_t vic;
	uint32_t frl_rate;
	uint32_t frl_lanes;
	uint32_t hc_active;
	uint32_t hc_blank;
};

struct frl_cap_chk_intermediates {
	int      c_frl_sb;
	double   overhead_sb;
	double   overhead_rs;
	double   overhead_map;
	double   overhead_min;
	double   overhead_max;
	double   f_pixel_clock_max;
	double   t_line;
	double   r_bit_min;
	double   r_frl_char_min;
	double   c_frl_line;
	double   ap;
	double   r_ap;
	double   avg_audio_packets_line;
	int      audio_packets_line;
	int      blank_audio_min;
};

struct frl_cap_chk_params {
	int      lanes;
	double   f_pixel_clock_nominal;   /* Pixel Clock rate (Hz)  */
	double   r_bit_nominal;           /* FRL bitrate (bps) */
	int      audio_packet_type;
	double   f_audio;                 /* Audio rate (Hz) */
	int      h_active;                /* Active pixels per line */
	int      h_blank;                 /* Blanking pixels per line */
	int      bpc;                     /* Bits per component */
	int      vic;                     /* Video Identification Code */

	enum hdmi_frl_pixel_encoding    pixel_encoding;

	bool     compressed;              /* set to true if DSC is enabled */
	bool     bypass_hc_target_calc;   /* debug only */
	bool     allow_all_bpp;           /* dsc_all_bpp */

	/* DSC parameters */
	int      slices;
	int      slice_width;
	double   bpp_target;
	bool     is_ovt;
	int      layout;
	int      acat;    /* not supported */

	/* outputs */
	struct frl_dml_borrow_params borrow_params;
	int      average_tribyte_rate;
};

enum frl_cap_chk_result dml1_frl_cap_chk(struct frl_cap_chk_params *params);

enum frl_cap_chk_result dml1_frl_cap_chk_inter(struct frl_cap_chk_params *params,
					       struct frl_cap_chk_intermediates *inter);

enum frl_cap_chk_result dml1_frl_cap_chk_common(struct frl_cap_chk_intermediates *inter,
						struct frl_cap_chk_params *params);

enum frl_cap_chk_result dml1_frl_cap_chk_uncompressed(struct frl_cap_chk_params *params,
						      struct frl_cap_chk_intermediates *inter);

enum frl_cap_chk_result dml1_frl_cap_chk_compressed(struct frl_cap_chk_params *params,
						    struct frl_cap_chk_intermediates *inter);
#endif

void frl_modified_pix_clock_for_dsc_padding(const int hc_active_target,
	const int hc_blank_target,
	const uint8_t frl_num_lanes,
	const uint32_t pix_clk_100hz,
	const int frl_link_rate,
	const uint32_t h_addressable,
	const uint32_t h_border_left,
	const uint32_t h_border_right,
	const uint32_t h_total,
	const uint32_t h_addressable_otg,
	uint32_t *pix_clk_100hz_otg,
	uint32_t *h_total_otg);

int frl_modify_borrow_mode_for_dsc_padding(const uint32_t pix_clk_100hz,
	const uint32_t h_active,
	const uint32_t h_active_padded,
	const uint32_t h_blank,
	const uint32_t h_blank_padded,
	const int hc_active,
	const int hc_blank,
	const uint8_t frl_num_lanes,
	const int frl_link_rate);
