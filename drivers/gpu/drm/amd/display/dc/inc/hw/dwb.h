/* Copyright 2012-17 Advanced Micro Devices, Inc.
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

#ifndef __DC_DWBC_H__
#define __DC_DWBC_H__

#include "dc_hw_types.h"

#define MAX_DWB_PIPES	3

enum dce_version;

enum dwb_sw_version {
	dwb_ver_1_0 = 1,
};

enum dwb_source {
	dwb_src_scl = 0,	/* for DCE7x/9x, DCN won't support. */
	dwb_src_blnd,		/* for DCE7x/9x */
	dwb_src_fmt,		/* for DCE7x/9x */
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	dwb_src_otg0 = 0x100,	/* for DCN1.x, register: mmDWB_SOURCE_SELECT */
	dwb_src_otg1,		/* for DCN1.x */
	dwb_src_otg2,		/* for DCN1.x */
	dwb_src_otg3,		/* for DCN1.x */
#endif
};

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
/* DCN1.x supports 2 pipes */
#endif
enum dwb_pipe {
	dwb_pipe0 = 0,
#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
	dwb_pipe1,
#endif
	dwb_pipe_max_num,
};

enum setting_flags {
	sf_pipe				= 0x00000001,
	sf_output_format	= 0x00000002,
	sf_capture_rate		= 0x00000004,
	sf_all				 = 0xffffffff,
};

enum dwb_capture_rate {

	dwb_capture_rate_0 = 0,	/* Every frame is captured. */
	dwb_capture_rate_1 = 1,	/* Every other frame is captured. */
	dwb_capture_rate_2 = 2,	/* Every 3rd frame is captured. */
	dwb_capture_rate_3 = 3,	/* Every 4th frame is captured. */
};

enum dwb_scaler_mode {
	dwb_scaler_mode_bypass444 = 0,
	dwb_scaler_mode_rgb444 = 1,
	dwb_scaler_mode_yuv444 = 2,
	dwb_scaler_mode_yuv420 = 3
};

struct dwb_caps {
	enum dce_version hw_version;	/* DCN engine version. */
	enum dwb_sw_version sw_version;	/* DWB sw implementation version. */
	unsigned int	reserved[6];	/* Reserved for future use, MUST BE 0. */
	unsigned int	adapter_id;
	unsigned int	num_pipes;		/* number of DWB pipes */
	struct {
		unsigned int support_dwb					 :1;
		unsigned int support_ogam					:1;
		unsigned int support_wbscl					:1;
		unsigned int support_ocsc					:1;
	} caps;
	unsigned int	 reserved2[10];	/* Reserved for future use, MUST BE 0. */
};

struct dwb_status {
	bool			enabled;
	/* Reserved ========================================================================= */
	unsigned int	reserved[8];	/* Reserved fields */
};

struct dwb_basic_settings {
	/* General DWB related input parameters ============================================= */
	enum dwb_source	input_src_select;	 /* Select input source: (DCE) 0: SCL; 1: BLND; 2: FMT; (DCN) OTG* or MPC* */
	enum dwb_pipe	input_pipe_select;	/* Select input pipe: 0: PIPE0; 1: PIPE1; 2: PIPE2 */

	/* CNV: WND Related parameters ====================================================== */
	unsigned int	capture_rate; /* Captures once every (capture_rate+1) frames */

	/* CNV: CSC Related parameters ====================================================== */
	unsigned int	start_x;	/* Horizontal window start position */
	unsigned int	start_y;	/* Vertical window start position */
	unsigned int	src_width;	/* Width of window captured within source window */
	unsigned int	src_height;	/* Height of window captured within source window */

	/* SISCL Related parameters ========================================================= */
	unsigned int	dest_width; /* Destination width */
	unsigned int	dest_height; /* Destination height */

	/* MCIF bufer parameters	========================================================= */
	unsigned long long luma_address[4];
	unsigned long long chroma_address[4];
	unsigned int	luma_pitch;
	unsigned int	chroma_pitch;
	unsigned int	slice_lines;

	/* Reserved ========================================================================= */
	unsigned int	reserved[8];	/* Reserved fields */

};

struct dwb_advanced_settings {
	enum setting_flags		uFlag;
	enum dwb_pipe			pipe;		/* default = DWB_PIPE_ALL */
	enum dwb_scaler_mode	out_format;	/* default = DWBScalerMode_YUV420 */
	enum dwb_capture_rate	capture_rate; /* default = Every frame is captured */
	unsigned int			reserved[64]; /* reserved for future use, must be 0 */
};

/* / - dwb_frame_info is the info of the dumping data */
struct dwb_frame_info {
	unsigned int				 size;
	unsigned int				 width;
	unsigned int				 height;
	unsigned int				 luma_pitch;
	unsigned int				 chroma_pitch;
	enum dwb_scaler_mode		 format;
};

struct dwbc_cfg {
	struct	dwb_basic_settings basic_settings;
	struct	dwb_advanced_settings advanced_settings;
};

struct dwbc {
	const struct dwbc_funcs *funcs;
	struct dc_context *ctx;
	struct dwbc_cfg config;
	struct dwb_status status;
	int inst;
};

struct dwbc_funcs {
	bool (*get_caps)(struct dwbc *dwbc, struct dwb_caps *caps);

	bool (*enable)(struct dwbc *dwbc);

	bool (*disable)(struct dwbc *dwbc);

	bool (*get_status)(struct dwbc *dwbc, struct dwb_status *status);

	bool (*dump_frame)(struct dwbc *dwbc, struct dwb_frame_info *frame_info,
		unsigned char *luma_buffer, unsigned char *chroma_buffer,
		unsigned char *dest_luma_buffer, unsigned char *dest_chroma_buffer);

	bool (*set_basic_settings)(struct dwbc *dwbc,
		const struct dwb_basic_settings *basic_settings);

	bool (*get_basic_settings)(struct dwbc *dwbc,
		struct dwb_basic_settings *basic_settings);

	bool (*set_advanced_settings)(struct dwbc *dwbc,
		const struct dwb_advanced_settings *advanced_settings);

	bool (*get_advanced_settings)(struct dwbc *dwbc,
		struct dwb_advanced_settings *advanced_settings);

	bool (*reset_advanced_settings)(struct dwbc *dwbc);
};

#endif
