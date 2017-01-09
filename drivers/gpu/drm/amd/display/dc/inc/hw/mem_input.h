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
#ifndef __DAL_MEM_INPUT_H__
#define __DAL_MEM_INPUT_H__

#include "dc.h"
#include "include/grph_object_id.h"
#include "inc/bandwidth_calcs.h"

#include "dce/dce_mem_input.h" /* temporary */

struct stutter_modes {
	bool enhanced;
	bool quad_dmif_buffer;
	bool watermark_nb_pstate;
};

struct mem_input {
	struct mem_input_funcs *funcs;
	struct dc_context *ctx;
	struct dc_plane_address request_address;
	struct dc_plane_address current_address;
	uint32_t inst;
	struct stutter_modes stutter_mode;

	const struct dce_mem_input_registers *regs;
	const struct dce_mem_input_shift *shifts;
	const struct dce_mem_input_mask *masks;
	struct dce_mem_input_wa wa;
};

struct mem_input_funcs {
	void (*mem_input_program_display_marks)(
		struct mem_input *mem_input,
		struct bw_watermarks nbp,
		struct bw_watermarks stutter,
		struct bw_watermarks urgent,
		uint32_t total_dest_line_time_ns);

	void (*mem_input_program_chroma_display_marks)(
			struct mem_input *mem_input,
			struct bw_watermarks nbp,
			struct bw_watermarks stutter,
			struct bw_watermarks urgent,
			uint32_t total_dest_line_time_ns);

	void (*allocate_mem_input)(
		struct mem_input *mem_input,
		uint32_t h_total,/* for current target */
		uint32_t v_total,/* for current target */
		uint32_t pix_clk_khz,/* for current target */
		uint32_t total_streams_num);

	void (*free_mem_input)(
		struct mem_input *mem_input,
		uint32_t paths_num);

	bool (*mem_input_program_surface_flip_and_addr)(
		struct mem_input *mem_input,
		const struct dc_plane_address *address,
		bool flip_immediate);

	void (*mem_input_program_pte_vm)(
		struct mem_input *mem_input,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		enum dc_rotation_angle rotation);

	void (*mem_input_program_surface_config)(
		struct mem_input *mem_input,
		enum surface_pixel_format format,
		union dc_tiling_info *tiling_info,
		union plane_size *plane_size,
		enum dc_rotation_angle rotation,
		struct dc_plane_dcc_param *dcc,
		bool horizontal_mirror,
		bool visible);

	bool (*mem_input_is_flip_pending)(struct mem_input *mem_input);

};

#endif
