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

#ifndef __DC_MCIF_WB_H__
#define __DC_MCIF_WB_H__

#include "dc_hw_types.h"


enum mmhubbub_wbif_mode {
	PACKED_444 = 0,
	PACKED_444_FP16 = 1,
	PLANAR_420_8BPC = 2,
	PLANAR_420_10BPC = 3
};

struct mcif_arb_params {

	unsigned int		time_per_pixel;
	unsigned int		cli_watermark[4];
	unsigned int		pstate_watermark[4];
	unsigned int		arbitration_slice;
	unsigned int		slice_lines;
	unsigned int		max_scaled_time;
	unsigned int		dram_speed_change_duration;
};

struct mcif_irq_params {
	unsigned int		sw_int_en;
	unsigned int		sw_slice_int_en;
	unsigned int		sw_overrun_int_en;
	unsigned int		vce_int_en;
	unsigned int		vce_slice_int_en;
};


/* / - mcif_wb_frame_dump_info is the info of the dumping WB data */
struct mcif_wb_frame_dump_info {
	unsigned int		size;
	unsigned int		width;
	unsigned int		height;
	unsigned int		luma_pitch;
	unsigned int		chroma_pitch;
	enum dwb_scaler_mode	format;
};

struct mcif_wb {
	const struct mcif_wb_funcs *funcs;
	struct dc_context *ctx;
	int inst;
};

struct mcif_wb_funcs {

	void (*warmup_mcif)(
		struct mcif_wb *mcif_wb,
		struct mcif_warmup_params *params);
	void (*enable_mcif)(struct mcif_wb *mcif_wb);

	void (*disable_mcif)(struct mcif_wb *mcif_wb);

	void (*config_mcif_buf)(
		struct mcif_wb *mcif_wb,
		struct mcif_buf_params *params,
		unsigned int dest_height);

	 void (*config_mcif_arb)(
		struct mcif_wb *mcif_wb,
		struct mcif_arb_params *params);

	 void (*config_mcif_irq)(
		struct mcif_wb *mcif_wb,
		struct mcif_irq_params *params);

	void (*dump_frame)(
		struct mcif_wb *mcif_wb,
		struct mcif_buf_params *mcif_params,
		enum dwb_scaler_mode out_format,
		unsigned int dest_width,
		unsigned int dest_height,
		struct mcif_wb_frame_dump_info *dump_info,
		unsigned char *luma_buffer,
		unsigned char *chroma_buffer,
		unsigned char *dest_luma_buffer,
		unsigned char *dest_chroma_buffer);
};

#endif
