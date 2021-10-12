// SPDX-License-Identifier: MIT
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "resource.h"

#include "dcn20_fpu.h"

/**
 * DOC: DCN2x FPU manipulation Overview
 *
 * The DCN architecture relies on FPU operations, which require special
 * compilation flags and the use of kernel_fpu_begin/end functions; ideally, we
 * want to avoid spreading FPU access across multiple files. With this idea in
 * mind, this file aims to centralize all DCN20 and DCN2.1 (DCN2x) functions
 * that require FPU access in a single place. Code in this file follows the
 * following code pattern:
 *
 * 1. Functions that use FPU operations should be isolated in static functions.
 * 2. The FPU functions should have the noinline attribute to ensure anything
 *    that deals with FP register is contained within this call.
 * 3. All function that needs to be accessed outside this file requires a
 *    public interface that not uses any FPU reference.
 * 4. Developers **must not** use DC_FP_START/END in this file, but they need
 *    to ensure that the caller invokes it before access any function available
 *    in this file. For this reason, public functions in this file must invoke
 *    dc_assert_fp_enabled();
 *
 * Let's expand a little bit more the idea in the code pattern. To fully
 * isolate FPU operations in a single place, we must avoid situations where
 * compilers spill FP values to registers due to FP enable in a specific C
 * file. Note that even if we isolate all FPU functions in a single file and
 * call its interface from other files, the compiler might enable the use of
 * FPU before we call DC_FP_START. Nevertheless, it is the programmer's
 * responsibility to invoke DC_FP_START/END in the correct place. To highlight
 * situations where developers forgot to use the FP protection before calling
 * the DC FPU interface functions, we introduce a helper that checks if the
 * function is invoked under FP protection. If not, it will trigger a kernel
 * warning.
 */

void dcn20_populate_dml_writeback_from_context(struct dc *dc,
					       struct resource_context *res_ctx,
					       display_e2e_pipe_params_st *pipes)
{
	int pipe_cnt, i;

	dc_assert_fp_enabled();

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_writeback_info *wb_info = &res_ctx->pipe_ctx[i].stream->writeback_info[0];

		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		/* Set writeback information */
		pipes[pipe_cnt].dout.wb_enable = (wb_info->wb_enabled == true) ? 1 : 0;
		pipes[pipe_cnt].dout.num_active_wb++;
		pipes[pipe_cnt].dout.wb.wb_src_height = wb_info->dwb_params.cnv_params.crop_height;
		pipes[pipe_cnt].dout.wb.wb_src_width = wb_info->dwb_params.cnv_params.crop_width;
		pipes[pipe_cnt].dout.wb.wb_dst_width = wb_info->dwb_params.dest_width;
		pipes[pipe_cnt].dout.wb.wb_dst_height = wb_info->dwb_params.dest_height;
		pipes[pipe_cnt].dout.wb.wb_htaps_luma = 1;
		pipes[pipe_cnt].dout.wb.wb_vtaps_luma = 1;
		pipes[pipe_cnt].dout.wb.wb_htaps_chroma = wb_info->dwb_params.scaler_taps.h_taps_c;
		pipes[pipe_cnt].dout.wb.wb_vtaps_chroma = wb_info->dwb_params.scaler_taps.v_taps_c;
		pipes[pipe_cnt].dout.wb.wb_hratio = 1.0;
		pipes[pipe_cnt].dout.wb.wb_vratio = 1.0;
		if (wb_info->dwb_params.out_format == dwb_scaler_mode_yuv420) {
			if (wb_info->dwb_params.output_depth == DWB_OUTPUT_PIXEL_DEPTH_8BPC)
				pipes[pipe_cnt].dout.wb.wb_pixel_format = dm_420_8;
			else
				pipes[pipe_cnt].dout.wb.wb_pixel_format = dm_420_10;
		} else {
			pipes[pipe_cnt].dout.wb.wb_pixel_format = dm_444_32;
		}

		pipe_cnt++;
	}
}
