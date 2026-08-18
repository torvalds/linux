// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "dml_logger.h"
#include "dml1_frl_cap_chk.h"
#include "dml_inline_defs.h"

static const double __maybe_unused EPSILON			= 0.01;
static const double __maybe_unused DBL_EPSILON			= 2.2204460492503131e-16;
static const double __maybe_unused OVERHEAD_M			= 0.003;  /* %   */
static const double __maybe_unused TOLERANCE_PIXEL_CLOCK	= 0.005;  /* %   */
static const double __maybe_unused DML_TOLERANCE_AUDIO_CLOCK	= 1000;   /* ppm */

#define frl_dump_var(fmt, var) {}
#define frl_print(fmt, ...) {}

const struct frl_primary_format prim_format_444[] = {
/* VIC/Rate/Lanes/HCactive/HCBlank */
	{64,  3, 3, 960,  360}, /* 1920x1080 @ 100 */
	{77,  3, 3, 960,  360}, /* 1920x1080 @ 100 */
	{63,  3, 3, 960,  140}, /* 1920x1080 @ 120 */
	{78,  3, 3, 960,  140}, /* 1920x1080 @ 120 */
	{93,  3, 3, 1920, 828}, /* 3840x2160 @ 24 */
	{103, 3, 3, 1920, 828}, /* 3840x2160 @ 24 */
	{94,  3, 3, 1920, 720}, /* 3840x2160 @ 25 */
	{104, 3, 3, 1920, 720}, /* 3840x2160 @ 25 */
	{95,  3, 3, 1920, 280}, /* 3840x2160 @ 30 */
	{105, 3, 3, 1920, 280}, /* 3840x2160 @ 30 */
	{114, 3, 3, 1920, 828}, /* 3840x2160 @ 48 */
	{116, 3, 3, 1920, 828}, /* 3840x2160 @ 48 */
	{96,  3, 3, 1920, 720}, /* 3840x2160 @ 50 */
	{106, 3, 3, 1920, 720}, /* 3840x2160 @ 50 */
	{97,  3, 3, 1920, 280}, /* 3840x2160 @ 60 */
	{107, 3, 3, 1920, 280}, /* 3840x2160 @ 60 */
	{117, 6, 3, 1920, 720}, /* 3840x2160 @ 100 */
	{119, 6, 3, 1920, 720}, /* 3840x2160 @ 100 */
	{118, 6, 3, 1920, 280}, /* 3840x2160 @ 120 */
	{120, 6, 3, 1920, 280}, /* 3840x2160 @ 120 */
	{98,  3, 3, 2048, 700}, /* 4096x2160 @ 24 */
	{99,  3, 3, 2048, 592}, /* 4096x2160 @ 25 */
	{100, 3, 3, 2048, 152}, /* 4096x2160 @ 30 */
	{115, 3, 3, 2048, 700}, /* 4096x2160 @ 48 */
	{101, 3, 3, 2048, 592}, /* 4096x2160 @ 50 */
	{102, 3, 3, 2048, 152}, /* 4096x2160 @ 60 */
	{218, 6, 3, 2048, 592}, /* 4096x2160 @ 100 */
	{219, 6, 3, 2048, 152}, /* 4096x2160 @ 120 */
	{121, 3, 3, 2560, 1188}, /* 5120x2160 @ 24 */
	{122, 3, 3, 2560, 1040}, /* 5120x2160 @ 25 */
	{123, 3, 3, 2560, 440}, /* 5120x2160 @ 30 */
	{124, 3, 3, 2560, 256}, /* 5120x2160 @ 48 */
	{125, 3, 3, 2560, 484}, /* 5120x2160 @ 50 */
	{126, 3, 3, 2307, 144}, /* 5120x2160 @ 60 */
	{127, 6, 3, 2560, 484}, /* 5120x2160 @ 100 */
	{193, 6, 3, 2334, 104}, /* 5120x2160 @ 120 */
	{194, 6, 3, 3840, 1660}, /* 7680x2160 @ 24 */
	{202, 6, 3, 3840, 1660}, /* 7680x2160 @ 24 */
	{195, 6, 3, 3840, 1560}, /* 7680x2160 @ 25 */
	{203, 6, 3, 3840, 1560}, /* 7680x2160 @ 25 */
	{196, 6, 3, 3840, 660}, /* 7680x2160 @ 30 */
	{204, 6, 3, 3840, 660}, /* 7680x2160 @ 30 */
	{197, 6, 4, 3142, 1292}, /* 7680x2160 @ 48 */
	{205, 6, 4, 3142, 1292}, /* 7680x2160 @ 48 */
	{198, 6, 4, 3142, 1180}, /* 7680x2160 @ 50 */
	{206, 6, 4, 3142, 1180}, /* 7680x2160 @ 50 */
	{199, 6, 4, 3182, 140}, /* 7680x2160 @ 60 */
	{207, 6, 4, 3182, 140}, /* 7680x2160 @ 60 */
	{200, 10, 4, 2680, 784}, /* 7680x2160 @ 100 */
	{208, 10, 4, 2680, 784}, /* 7680x2160 @ 100 */
	{201, 10, 4, 2600, 100}, /* 7680x2160 @ 120 */
	{209, 10, 4, 2600, 100}, /* 7680x2160 @ 120 */
	{210, 6, 3, 4854, 912}, /* 10240x4320 @ 24 */
	{211, 6, 3, 4827, 1536}, /* 10240x4320 @ 25 */
	{212, 6, 3, 4720, 128}, /* 10240x4320 @ 30 */
	{213, 8, 4, 4347, 756}, /* 10240x4320 @ 48 */
	{214, 8, 4, 4320, 1376}, /* 10240x4320 @ 50 */
	{215, 8, 4, 4187, 124}, /* 10240x4320 @ 60 */
};

const struct frl_primary_format prim_format_422[] = {
/* VIC/Rate/Lanes/HCactive/HCBlank */
	{64,  3, 3, 960,  360}, /* 1920x1080 @ 100 */
	{77,  3, 3, 960,  360}, /* 1920x1080 @ 100 */
	{63,  3, 3, 960,  140}, /* 1920x1080 @ 120 */
	{78,  3, 3, 960,  140}, /* 1920x1080 @ 120 */
	{93,  3, 3, 1920, 828}, /* 3840x2160 @ 24 */
	{103, 3, 3, 1920, 828}, /* 3840x2160 @ 24 */
	{94,  3, 3, 1920, 720}, /* 3840x2160 @ 25 */
	{104, 3, 3, 1920, 720}, /* 3840x2160 @ 25 */
	{95,  3, 3, 1920, 280}, /* 3840x2160 @ 30 */
	{105, 3, 3, 1920, 280}, /* 3840x2160 @ 30 */
	{114, 3, 3, 1920, 828}, /* 3840x2160 @ 48 */
	{116, 3, 3, 1920, 828}, /* 3840x2160 @ 48 */
	{96,  3, 3, 1920, 720}, /* 3840x2160 @ 50 */
	{106, 3, 3, 1920, 720}, /* 3840x2160 @ 50 */
	{97,  3, 3, 1920, 280}, /* 3840x2160 @ 60 */
	{107, 3, 3, 1920, 280}, /* 3840x2160 @ 60 */
	{117, 3, 3, 1370, 104}, /* 3840x2160 @ 100 */
	{119, 3, 3, 1370, 104}, /* 3840x2160 @ 100 */
	{118, 3, 3, 1130, 104}, /* 3840x2160 @ 120 */
	{120, 3, 3, 1130, 104}, /* 3840x2160 @ 120 */
	{98,  3, 3, 2048, 700}, /* 4096x2160 @ 24 */
	{99,  3, 3, 2048, 592}, /* 4096x2160 @ 25 */
	{100, 3, 3, 2048, 152}, /* 4096x2160 @ 30 */
	{115, 3, 3, 2048, 700}, /* 4096x2160 @ 48 */
	{101, 3, 3, 2048, 592}, /* 4096x2160 @ 50 */
	{102, 3, 3, 2048, 152}, /* 4096x2160 @ 60 */
	{218, 6, 3, 2048, 592}, /* 4096x2160 @ 100 */
	{219, 6, 3, 2048, 152}, /* 4096x2160 @ 120 */
	{121, 3, 3, 2560, 1188}, /* 5120x2160 @ 24 */
	{122, 3, 3, 2560, 1040}, /* 5120x2160 @ 25 */
	{123, 3, 3, 2560, 440}, /* 5120x2160 @ 30 */
	{124, 3, 3, 2560, 256}, /* 5120x2160 @ 48 */
	{125, 3, 3, 2560, 484}, /* 5120x2160 @ 50 */
	{126, 3, 3, 2307, 144}, /* 5120x2160 @ 60 */
	{127, 6, 3, 2560, 484}, /* 5120x2160 @ 100 */
	{193, 6, 3, 2334, 104}, /* 5120x2160 @ 120 */
	{194, 3, 3, 2460, 816}, /* 7680x2160 @ 24 */
	{202, 3, 3, 2460, 816}, /* 7680x2160 @ 24 */
	{195, 3, 3, 2460, 732}, /* 7680x2160 @ 25 */
	{203, 3, 3, 2460, 732}, /* 7680x2160 @ 25 */
	{196, 3, 3, 2360, 144}, /* 7680x2160 @ 30 */
	{204, 3, 3, 2360, 144}, /* 7680x2160 @ 30 */
	{197, 6, 3, 2460, 816}, /* 7680x2160 @ 48 */
	{205, 6, 3, 2460, 816}, /* 7680x2160 @ 48 */
	{198, 6, 3, 2460, 732}, /* 7680x2160 @ 50 */
	{206, 6, 3, 2460, 732}, /* 7680x2160 @ 50 */
	{199, 6, 3, 2380, 116}, /* 7680x2160 @ 60 */
	{207, 6, 3, 2380, 116}, /* 7680x2160 @ 60 */
	{200, 10, 4, 2680, 784}, /* 7680x2160 @ 100 */
	{208, 10, 4, 2680, 784}, /* 7680x2160 @ 100 */
	{201, 10, 4, 2600, 100}, /* 7680x2160 @ 120 */
	{209, 10, 4, 2600, 100}, /* 7680x2160 @ 120 */
	{210, 6, 3, 4854, 912}, /* 10240x4320 @ 24 */
	{211, 6, 3, 4827, 1536}, /* 10240x4320 @ 25 */
	{212, 6, 3, 4720, 128}, /* 10240x4320 @ 30 */
	{213, 6, 4, 3360, 420}, /* 10240x4320 @ 48 */
	{214, 6, 4, 3334, 892}, /* 10240x4320 @ 50 */
	{215, 6, 4, 3120, 124}, /* 10240x4320 @ 60 */
	{216, 12, 4, 3334, 764}, /* 10240x4320 @ 100 */
	{217, 12, 4, 3120, 124}, /* 10240x4320 @ 120 */
};

const struct frl_primary_format prim_format_420[] = {
/* VIC/Rate/Lanes/HCactive/HCBlank */
	{114, 3, 3, 1920, 828}, /* 3840x2160 @ 48 */
	{116, 3, 3, 1920, 828}, /* 3840x2160 @ 48 */
	{96,  3, 3, 1920, 720}, /* 3840x2160 @ 50 */
	{106, 3, 3, 1920, 720}, /* 3840x2160 @ 50 */
	{97,  3, 3, 1920, 280}, /* 3840x2160 @ 60 */
	{107, 3, 3, 1920, 280}, /* 3840x2160 @ 60 */
	{117, 3, 3, 1370, 104}, /* 3840x2160 @ 100 */
	{119, 3, 3, 1370, 104}, /* 3840x2160 @ 100 */
	{118, 3, 3, 1130, 104}, /* 3840x2160 @ 120 */
	{120, 3, 3, 1130, 104}, /* 3840x2160 @ 120 */
	{115, 3, 3, 2048, 700}, /* 4096x2160 @ 48 */
	{101, 3, 3, 2048, 592}, /* 4096x2160 @ 50 */
	{102, 3, 3, 2048, 152}, /* 4096x2160 @ 60 */
	{218, 3, 3, 1376, 96}, /* 4096x2160 @ 100 */
	{219, 3, 3, 1131, 84}, /* 4096x2160 @ 120 */
	{124, 3, 3, 2560, 256}, /* 5120x2160 @ 48 */
	{125, 3, 3, 2560, 484}, /* 5120x2160 @ 50 */
	{126, 3, 3, 2307, 144}, /* 5120x2160 @ 60 */
	{127, 6, 3, 2560, 484}, /* 5120x2160 @ 100 */
	{193, 6, 3, 2334, 104}, /* 5120x2160 @ 120 */
	{194, 3, 3, 2460, 816}, /* 7680x2160 @ 24 */
	{202, 3, 3, 2460, 816}, /* 7680x2160 @ 24 */
	{195, 3, 3, 2460, 732}, /* 7680x2160 @ 25 */
	{203, 3, 3, 2460, 732}, /* 7680x2160 @ 25 */
	{196, 3, 3, 2360, 144}, /* 7680x2160 @ 30 */
	{204, 3, 3, 2360, 144}, /* 7680x2160 @ 30 */
	{197, 6, 3, 2460, 816}, /* 7680x2160 @ 48 */
	{205, 6, 3, 2460, 816}, /* 7680x2160 @ 48 */
	{198, 6, 3, 2460, 732}, /* 7680x2160 @ 50 */
	{206, 6, 3, 2460, 732}, /* 7680x2160 @ 50 */
	{199, 6, 3, 2380, 116}, /* 7680x2160 @ 60 */
	{207, 6, 3, 2380, 116}, /* 7680x2160 @ 60 */
	{200, 8, 4, 2240, 480}, /* 7680x2160 @ 100 */
	{208, 8, 4, 2240, 480}, /* 7680x2160 @ 100 */
	{201, 8, 4, 2062, 108}, /* 7680x2160 @ 120 */
	{209, 8, 4, 2062, 108}, /* 7680x2160 @ 120 */
	{210, 3, 3, 2614, 172}, /* 10240x4320 @ 24 */
	{211, 3, 3, 2614, 500}, /* 10240x4320 @ 25 */
	{212, 6, 3, 4720, 128}, /* 10240x4320 @ 30 */
	{213, 6, 3, 2614, 172}, /* 10240x4320 @ 48 */
	{214, 6, 4, 3334, 892}, /* 10240x4320 @ 50 */
	{215, 6, 4, 3120, 124}, /* 10240x4320 @ 60 */
	{216, 10, 4, 2854, 520}, /* 10240x4320 @ 100 */
	{217, 10, 4, 2587, 120}, /* 10240x4320 @ 120 */
};

enum frl_cap_chk_result dml1_frl_cap_chk_common(struct frl_cap_chk_intermediates *inter,
						struct frl_cap_chk_params *params)
{
	double audio_bw_reserve = (params->compressed ? 192000.0 : 0.0);

	dc_assert_fp_enabled();

#ifdef DEBUG_FRL_CAP_CHK
	{
		printf("frl_cap_chk inputs:\n");
		printf("-------------------\n");
		frl_dump_var("%i",  params->lanes);
		frl_dump_var("%le", params->f_pixel_clock_nominal);
		frl_dump_var("%le", params->r_bit_nominal);
		frl_dump_var("%i",  params->audio_packet_type);
		frl_dump_var("%le", params->f_audio);
		frl_dump_var("%i",  params->h_active);
		frl_dump_var("%i",  params->h_blank);
		frl_dump_var("%i",  params->bpc);
		frl_dump_var("%i",  params->pixel_encoding);
		frl_dump_var("%i",  params->compressed);
		frl_dump_var("%i",  params->slices);
		frl_dump_var("%i",  params->slice_width);
		frl_dump_var("%le", params->bpp_target);
		frl_dump_var("%i",  params->layout);
		frl_dump_var("%i",  params->acat);
		printf("frl_cap_chk outputs:\n");
		printf("---------------------\n");
	}
#endif

	inter->c_frl_sb          = 4 * C_FRL_CB + params->lanes;
	inter->overhead_sb       = (double)params->lanes / inter->c_frl_sb;
	inter->overhead_rs       = 8.0 * 4.0 / inter->c_frl_sb;
	inter->overhead_map      = 2.5 / inter->c_frl_sb;
	inter->overhead_min      = inter->overhead_sb + inter->overhead_rs + inter->overhead_map;
	inter->overhead_max      = inter->overhead_min + OVERHEAD_M;
	inter->f_pixel_clock_max = params->f_pixel_clock_nominal * (1.0 + TOLERANCE_PIXEL_CLOCK);
	inter->t_line            = (params->h_active + params->h_blank) / inter->f_pixel_clock_max;
	inter->r_bit_min         = params->r_bit_nominal * (1.0 - TOLERANCE_FRL_BIT / 1000000.0);
	inter->r_frl_char_min    = inter->r_bit_min / 18.0;
	inter->c_frl_line        = dml_floor(inter->t_line * inter->r_frl_char_min * params->lanes, 1);

#ifdef DEBUG_FRL_CAP_CHK
	{
		frl_dump_var("%i",  inter->c_frl_sb);
		frl_dump_var("%le", inter->overhead_sb);
		frl_dump_var("%le", inter->overhead_rs);
		frl_dump_var("%le", inter->overhead_map);
		frl_dump_var("%le", inter->overhead_min);
		frl_dump_var("%le", inter->overhead_max);
		frl_dump_var("%le", inter->f_pixel_clock_max);
		frl_dump_var("%le", inter->t_line);
		frl_dump_var("%le", inter->r_bit_min);
		frl_dump_var("%le", inter->r_frl_char_min);
		frl_dump_var("%le", inter->c_frl_line);
	}
#endif

	switch (params->audio_packet_type) {
	case 0x02:
		if (params->layout == 0)
			inter->ap = 0.25;
		else if (params->layout == 1)
			inter->ap = 1.0;
		break;
	case 0x08:
		inter->ap = 0.25;
		break;
	case 0x09:
		inter->ap = 1.0;
		break;
	case 0x07:
	case 0x0e:
	case 0x0f:
	case 0x0b:
	case 0x0c:
		/* Unsupported audio format */
		return FRL_CAP_CHK_ERROR_UNSUPPORTED_AUDIO;
	default:
		inter->ap = 0.0;
	}

	inter->r_ap                   = (dml_max(audio_bw_reserve, params->f_audio * inter->ap) + 2 * ACR_RATE_MAX) * (1 + DML_TOLERANCE_AUDIO_CLOCK / 1000000.0);
	inter->avg_audio_packets_line = inter->r_ap * inter->t_line;
	inter->audio_packets_line     = (int)dml_ceil(inter->avg_audio_packets_line, 1);
	inter->blank_audio_min        = 32 + 32 * inter->audio_packets_line; // h_blank_audio_min or hc_blank_audio_min

	params->borrow_params.audio_packets_line = inter->audio_packets_line;

#ifdef DEBUG_FRL_CAP_CHK
	{
		frl_dump_var("%le", inter->ap);
		frl_dump_var("%le", inter->r_ap);
		frl_dump_var("%le", inter->avg_audio_packets_line);
		frl_dump_var("%i",  inter->audio_packets_line);
		frl_dump_var("%i",  inter->blank_audio_min);
	}
#endif

	return FRL_CAP_CHK_OK;
}

enum frl_cap_chk_result dml1_frl_cap_chk_uncompressed(struct frl_cap_chk_params *params,
						      struct frl_cap_chk_intermediates *inter)
{
	enum frl_cap_chk_result res;
	int      k_420;
	double   k_cd;
	int      c_frl_free;
	int      c_frl_rc_margin;
	int      c_frl_rc_savings;
	int      bpp;
	double   bytes_line;
	int      tb_active;
	int      tb_blank;
	double   f_tb_average;
	double   t_active_ref;
	double   t_blank_ref;
	double   t_active_min;
	double   t_blank_min;
	double   t_borrowed;
	double   tb_borrowed;
	int      c_frl_actual_payload;
	double   utilization;
	double   margin;

	dc_assert_fp_enabled();

	res = dml1_frl_cap_chk_common(inter, params);
	if (res != FRL_CAP_CHK_OK)
		return res;

	k_420            = params->pixel_encoding == HDMI_FRL_PIXEL_ENCODING_420 ? 2 : 1;
	k_cd             = params->pixel_encoding == HDMI_FRL_PIXEL_ENCODING_422 ? 1.0 : params->bpc / 8.0;
	c_frl_free       = (int)dml_max(params->h_blank * k_cd / k_420 - 32 * (1 + inter->audio_packets_line) - 7, 0);
	c_frl_rc_margin  = 4;
	c_frl_rc_savings = (int)dml_floor(dml_max(((7.0 / 8.0) * c_frl_free) - c_frl_rc_margin, 0.0), 1);
	bpp              = (int)(24 * k_cd / k_420);
	bytes_line       = bpp * params->h_active / 8.0;
	tb_active        = (int)dml_ceil(bytes_line / 3, 1);
	tb_blank         = (int)dml_ceil(params->h_blank * k_cd / k_420, 1);

#ifdef DEBUG_FRL_CAP_CHK
	{
		frl_dump_var("%i", k_420);
		frl_dump_var("%le", k_cd);
		frl_dump_var("%i", c_frl_free);
		frl_dump_var("%i", c_frl_rc_margin);
		frl_dump_var("%i", c_frl_rc_savings);
		frl_dump_var("%i", bpp);
		frl_dump_var("%le", bytes_line);
		frl_dump_var("%i", tb_active);
		frl_dump_var("%i", tb_blank);
	}
#endif

	if (!(inter->blank_audio_min <= tb_blank)) {
		frl_dump_var("%i", inter->blank_audio_min);
		frl_dump_var("%i", tb_blank);
		return FRL_CAP_CHK_ERROR_AUDIO_BW;
	}

	f_tb_average = (inter->f_pixel_clock_max / (params->h_active + params->h_blank)) * (tb_active + tb_blank);
	t_active_ref = inter->t_line * ((double)params->h_active / (params->h_active + params->h_blank));
	t_blank_ref  = inter->t_line * ((double)params->h_blank / (params->h_active + params->h_blank));
	t_active_min = (3.0 / 2.0) * tb_active / (params->lanes * inter->r_frl_char_min * (1.0 - inter->overhead_max));
	t_blank_min  = tb_blank / (params->lanes * inter->r_frl_char_min * (1.0 - inter->overhead_max));

#ifdef DEBUG_FRL_CAP_CHK
	{
		frl_dump_var("%le", f_tb_average);
		frl_dump_var("%le", t_active_ref);
		frl_dump_var("%le", t_blank_ref);
		frl_dump_var("%le", t_active_min);
		frl_dump_var("%le", t_blank_min);
	}
#endif

	if (t_active_ref >= t_active_min && t_blank_ref >= t_blank_min) {
		t_borrowed = 0;
		params->borrow_params.borrow_mode = FRL_BORROW_MODE_NONE;
	} else if ((t_active_ref < t_active_min) && (t_blank_ref >= t_blank_min)) {
		t_borrowed = t_active_min - t_active_ref;
		params->borrow_params.borrow_mode = FRL_BORROW_MODE_FROM_BLANK;
	} else {
		return FRL_CAP_CHK_ERROR_BORROW;
	}

	tb_borrowed = dml_ceil(t_borrowed * f_tb_average, 1);

#ifdef DEBUG_FRL_CAP_CHK
	{
		frl_dump_var("%le", tb_borrowed);
		frl_dump_var("%i", params->borrow_params.borrow_mode);
	}
#endif

	if (!(tb_borrowed <= TB_BORROWED_MAX))
		return FRL_CAP_CHK_ERROR_MAX_BORROW;

	c_frl_actual_payload = (int)(dml_ceil((3.0 / 2.0) * tb_active, 1) + tb_blank - c_frl_rc_savings);
	utilization          = c_frl_actual_payload / inter->c_frl_line;
	margin               = 1.0 - (utilization + inter->overhead_max);

#ifdef DEBUG_FRL_CAP_CHK
	{
		frl_dump_var("%i",  c_frl_actual_payload);
		frl_dump_var("%le", utilization);
		frl_dump_var("%le", margin);
	}
#endif

	if (margin < 0 && dcn_bw_fabs(margin) > EPSILON)
		return FRL_CAP_CHK_ERROR_MARGIN;

	return FRL_CAP_CHK_OK;
}

#if defined (CONFIG_DRM_AMD_DC_FP)
enum frl_cap_chk_result dml1_frl_cap_chk_compressed(struct frl_cap_chk_params *params,
						    struct frl_cap_chk_intermediates *inter)
{
	enum frl_cap_chk_result res;
	int      c_frl_available;
#if defined(DEBUG_FRL_CAP_CHK)
	int      c_frl_active_available;
	int      c_frl_blank_available;
#endif
	int      bytes_target = 0;
	int      hc_active_target;
	int      hc_blank_target_est1;
	int      hc_blank_target_est2;
	int      hc_blank_target = 0;
	int      c_frl_actual_target_payload;
	double   utilization_targeted;
	double   margin_target;
	double   f_tb_average;
	double   t_active_ref;
	double   t_blank_ref;
	double   t_active_target;
	double   t_blank_target;
	double   tb_borrowed;
#ifdef DEBUG_FRL_CAP_CHK
	double   tb_delta;
	double   tb_delta_limit;
	int      tb_worst;
#endif
	int table_size_444 = ARRAY_SIZE(prim_format_444);
	int table_size_422 = ARRAY_SIZE(prim_format_422);
	int table_size_420 = ARRAY_SIZE(prim_format_420);
	int i;
	bool hc_active_blank_predefined = false;

	dc_assert_fp_enabled();

	res = dml1_frl_cap_chk_common(inter, params);

	if (res != FRL_CAP_CHK_OK)
		return res;

	c_frl_available        = (int)dml_floor((1 - inter->overhead_max) * inter->c_frl_line, 1);
#if defined(DEBUG_FRL_CAP_CHK)
	c_frl_active_available = dml_floor(c_frl_available * ((double)params->h_active / (params->h_active + params->h_blank)), 1);
	c_frl_blank_available  = dml_floor(c_frl_available * ((double)params->h_blank / (params->h_active + params->h_blank)), 1);
#endif
	bytes_target           = (int)(params->slices * dml_ceil(params->bpp_target * params->slice_width / 8.0, 1));

	if (!params->bypass_hc_target_calc)
			hc_active_target = (int)dml_ceil(bytes_target / 3.0, 1);
	else
		hc_active_target = params->borrow_params.hc_active_target;

	if (!params->allow_all_bpp && params->vic != 0) {
		if (params->pixel_encoding == HDMI_FRL_PIXEL_ENCODING_444) {
			for (i = 0; i < table_size_444 ; i++) {
				if (prim_format_444[i].vic == params->vic) {
					params->borrow_params.hc_active_target = prim_format_444[i].hc_active;
					params->borrow_params.hc_blank_target  = prim_format_444[i].hc_blank;
					hc_active_blank_predefined = true;
					break;
				}
			}
		} else if (params->pixel_encoding == HDMI_FRL_PIXEL_ENCODING_422) {
			for (i = 0; i < table_size_422 ; i++) {
				if (prim_format_422[i].vic == params->vic) {
					params->borrow_params.hc_active_target = prim_format_422[i].hc_active;
					params->borrow_params.hc_blank_target  = prim_format_422[i].hc_blank;
					hc_active_blank_predefined = true;
					break;
				}
			}
		} else if (params->pixel_encoding == HDMI_FRL_PIXEL_ENCODING_420) {
			for (i = 0; i < table_size_420 ; i++) {
				if (prim_format_420[i].vic == params->vic) {
					params->borrow_params.hc_active_target = prim_format_420[i].hc_active;
					params->borrow_params.hc_blank_target  = prim_format_420[i].hc_blank;
					hc_active_blank_predefined = true;
					break;
				}
			}
		}

		if (hc_active_blank_predefined) {
			hc_active_target = params->borrow_params.hc_active_target;
			hc_blank_target = params->borrow_params.hc_blank_target;
		}
	}

	hc_blank_target_est1 = (int)dml_ceil(hc_active_target * ((double)params->h_blank / params->h_active), 1);
	hc_blank_target_est2 = (int)dml_max(hc_blank_target_est1, inter->blank_audio_min);

	if (!hc_active_blank_predefined) {
		if (!params->bypass_hc_target_calc) {
			hc_blank_target = (int)(4 * dml_floor(dml_min(hc_blank_target_est2, c_frl_available - 3.0 / 2.0 * hc_active_target) / 4.0, 1));

			params->borrow_params.hc_active_target = hc_active_target;
			params->borrow_params.hc_blank_target  = hc_blank_target;
		} else {
			hc_blank_target  = params->borrow_params.hc_blank_target;
		}
	}

#ifdef DEBUG_FRL_CAP_CHK
	{
		frl_dump_var("%i", c_frl_available);
		frl_dump_var("%i", c_frl_active_available);
		frl_dump_var("%i", c_frl_blank_available);
		frl_dump_var("%i", bytes_target);
		frl_dump_var("%i", hc_active_target);
		frl_dump_var("%i", hc_blank_target_est1);
		frl_dump_var("%i", hc_blank_target_est2);
		frl_dump_var("%i", hc_blank_target);
	}
#endif

	if (!(inter->blank_audio_min <= hc_blank_target)) {
		frl_dump_var("%i", inter->blank_audio_min);
		frl_dump_var("%i", hc_blank_target);
		return FRL_CAP_CHK_ERROR_AUDIO_BW;
	}

	f_tb_average    = inter->f_pixel_clock_max / (params->h_active + params->h_blank) * (hc_active_target + hc_blank_target);
	t_active_ref    = inter->t_line * ((double)params->h_active / (params->h_active + params->h_blank));
	t_blank_ref     = inter->t_line - t_active_ref; // * ((double) params->h_blank / (params->h_active + params->h_blank));
	t_active_target = dml_max((hc_active_target / f_tb_average),
				  (3.0 / 2.0 * hc_active_target) /
				  (params->lanes * inter->r_frl_char_min * (1.0 - inter->overhead_max)));
	t_blank_target  = inter->t_line - t_active_target;

	tb_borrowed     = t_active_target * f_tb_average - hc_active_target;
#ifdef DEBUG_FRL_CAP_CHK
	tb_delta        = dcn_bw_fabs(t_active_target - t_active_ref) * (hc_active_target + hc_blank_target_est1) / inter->t_line;

	{
		frl_dump_var("%le", f_tb_average);
		frl_dump_var("%le", t_active_ref);
		frl_dump_var("%le", t_blank_ref);
		frl_dump_var("%le", t_active_target);
		frl_dump_var("%le", t_blank_target);
		frl_dump_var("%le", tb_delta);
	}
#endif

	if (t_blank_target - t_blank_ref > DBL_EPSILON) {
#ifdef DEBUG_FRL_CAP_CHK
		tb_delta_limit = (t_active_ref - hc_active_target / f_tb_average) * (hc_active_target + hc_blank_target_est1) / inter->t_line;
#endif
		params->borrow_params.borrow_mode = FRL_BORROW_MODE_FROM_ACTIVE;
	} else if (t_active_target - t_active_ref > DBL_EPSILON) {
#ifdef DEBUG_FRL_CAP_CHK
		tb_delta_limit = tb_delta;
#endif
		params->borrow_params.borrow_mode = FRL_BORROW_MODE_FROM_BLANK;
	} else {
#ifdef DEBUG_FRL_CAP_CHK
		tb_delta_limit = 0;
#endif
		params->borrow_params.borrow_mode = FRL_BORROW_MODE_NONE;
	}

#ifdef DEBUG_FRL_CAP_CHK
	tb_worst = dml_ceil(dml_max(tb_borrowed, tb_delta_limit), 1);

	{
		frl_dump_var("%le", tb_delta_limit);
		frl_dump_var("%le", tb_borrowed);
		frl_dump_var("%i", params->borrow_params.borrow_mode);
		frl_dump_var("%i", tb_worst);
	}
#endif

	if (!(tb_borrowed <= TB_BORROWED_MAX))
		return FRL_CAP_CHK_ERROR_MAX_BORROW;

	c_frl_actual_target_payload = (int)(dml_ceil(3.0 / 2.0 * hc_active_target, 1) + hc_blank_target);
	utilization_targeted        = c_frl_actual_target_payload / inter->c_frl_line;
	margin_target               = 1.0 - (utilization_targeted + inter->overhead_max);

#ifdef DEBUG_FRL_CAP_CHK
	{
		frl_dump_var("%i",  c_frl_actual_target_payload);
		frl_dump_var("%le", utilization_targeted);
		frl_dump_var("%le", margin_target);
	}
#endif

	// oversubscribed bandwidth relative to margin
	if (margin_target < 0 && dcn_bw_fabs(margin_target) > EPSILON)
		return FRL_CAP_CHK_ERROR_MARGIN;

	return FRL_CAP_CHK_OK;
}
#endif

enum frl_cap_chk_result dml1_frl_cap_chk(struct frl_cap_chk_params *params)
{
	struct frl_cap_chk_intermediates inter;

#if defined (CONFIG_DRM_AMD_DC_FP)
	if (params->compressed)
		return dml1_frl_cap_chk_compressed(params, &inter);
#endif

	return dml1_frl_cap_chk_inter(params, &inter);
}

enum frl_cap_chk_result dml1_frl_cap_chk_inter(struct frl_cap_chk_params *params,
					       struct frl_cap_chk_intermediates *inter)
{
	return dml1_frl_cap_chk_uncompressed(params, inter);
}

static double calculate_compressed_active_time(uint32_t h_active,
	const uint32_t h_blank,
	const int hc_active,
	const int hc_blank,
	const uint32_t frl_num_lanes,
	const double pix_clk,
	const int frl_link_rate)
{
	double f_tb_average;
	double r_bit_nominal;
	double r_bit_min;
	double r_frl_char_min;
	double t_active_est_1;
	double t_active_est_2;
	double t_active_target;
	int c_frl_sb = 510;
	int frl_bit_tolerance = 300;
	double overhead_m = 0.003;
	double overhead_sb;
	double overhead_rs;
	double overhead_map;
	double overhead_min;
	double overhead_max;

	switch (frl_link_rate) {
	case FRL_LINK_RATE_3GBPS:
		r_bit_nominal = 3.0e9;
		break;
	case FRL_LINK_RATE_6GBPS:
	case FRL_LINK_RATE_6GBPS_4LANE:
		r_bit_nominal = 6.0e9;
		break;
	case FRL_LINK_RATE_8GBPS:
		r_bit_nominal = 8.0e9;
		break;
	case FRL_LINK_RATE_10GBPS:
	default:
		r_bit_nominal = 10.0e9;
		break;
	case FRL_LINK_RATE_12GBPS:
		r_bit_nominal = 12.0e9;
		break;
	}

	f_tb_average = pix_clk / (h_active + h_blank)
					* (hc_active + hc_blank);

	c_frl_sb = 4 * c_frl_sb + frl_num_lanes;
	overhead_sb = (double)frl_num_lanes / c_frl_sb;
	overhead_rs = 8.0 * 4.0 / c_frl_sb;
	overhead_map = 2.5 / c_frl_sb;
	overhead_min = overhead_sb + overhead_rs + overhead_map;
	overhead_max = overhead_min + overhead_m;

	r_bit_min = r_bit_nominal * (1.0 - frl_bit_tolerance / 1000000.0);
	r_frl_char_min = r_bit_min / 18.0;
	t_active_est_1 = hc_active / f_tb_average;
	t_active_est_2 = (3.0 / 2.0 * hc_active) /
				  (frl_num_lanes * r_frl_char_min * (1.0 - overhead_max));

	if (t_active_est_1 > t_active_est_2) {
		t_active_target = t_active_est_1;
	} else {
		t_active_target = t_active_est_2;
	}

	return t_active_target;
}

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
	uint32_t *h_total_otg)
{
	double pix_clk;
	int h_active;
	int h_blank;
	double t_active_target;
	double hw_pix_clk;
	double h_total_otg_temp;

	pix_clk = (double)pix_clk_100hz * 100;

	h_active = h_addressable + h_border_left + h_border_right;
	h_blank = h_total - h_active;

	t_active_target = calculate_compressed_active_time(h_active, h_blank, hc_active_target, hc_blank_target, frl_num_lanes, pix_clk, frl_link_rate);

	h_total_otg_temp = ((double)h_addressable_otg * (double)h_total) / ((double)pix_clk_100hz * 100.0 * t_active_target);
	/* Htotal must be a multiple of 4, also take the ceiling */
	*h_total_otg = (uint32_t)dml_ceil(h_total_otg_temp, 4.0);

	hw_pix_clk = (double)(pix_clk_100hz * 100.0 * (double)*h_total_otg) / (double)h_total;
	*pix_clk_100hz_otg = (uint32_t)(hw_pix_clk / 100.0);
}

int frl_modify_borrow_mode_for_dsc_padding(const uint32_t pix_clk_100hz,
	const uint32_t h_active,
	const uint32_t h_active_padded,
	const uint32_t h_blank,
	const uint32_t h_blank_padded,
	const int hc_active,
	const int hc_blank,
	const uint8_t frl_num_lanes,
	const int frl_link_rate)
{
	double f_pixel_clock_max;
	double t_line;
	double t_active;
	double t_blank;
	double t_active_target;
	double t_blank_target;
	double pix_clk_tolerance = 0.005;

	enum frl_borrow_mode borrow_mode;

	f_pixel_clock_max = (double)pix_clk_100hz * (1.0 + pix_clk_tolerance);
	t_line = (double)(h_active + h_blank) / f_pixel_clock_max;

	t_active_target = calculate_compressed_active_time(h_active, h_blank, hc_active, hc_blank, frl_num_lanes, f_pixel_clock_max, frl_link_rate);

	t_active = t_line * ((double)h_active_padded / (h_active_padded + h_blank_padded));
	t_blank = t_line - t_active;

	t_blank_target = t_line - t_active_target;

	if (t_blank_target - t_blank > DBL_EPSILON) {
		borrow_mode = FRL_BORROW_MODE_FROM_ACTIVE;
	} else if (t_active_target - t_active > DBL_EPSILON) {
		borrow_mode = FRL_BORROW_MODE_FROM_BLANK;
	} else {
		borrow_mode = FRL_BORROW_MODE_NONE;
	}

	return borrow_mode;
}
