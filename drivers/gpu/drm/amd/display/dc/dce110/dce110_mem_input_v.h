/* Copyright 2012-16 Advanced Micro Devices, Inc.
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

#ifndef __DC_MEM_INPUT_V_DCE110_H__
#define __DC_MEM_INPUT_V_DCE110_H__

#include "mem_input.h"
#include "dce110_mem_input.h"

bool dce110_mem_input_v_construct(
	struct dce110_mem_input *mem_input110,
	struct dc_context *ctx);

/*
 * This function will program nbp stutter and urgency watermarks to minimum
 * allowable values
 */
void dce110_mem_input_v_program_display_marks(
	struct mem_input *mem_input,
	struct bw_watermarks nbp,
	struct bw_watermarks stutter,
	struct bw_watermarks urgent,
	uint32_t total_dest_line_time_ns);

/*
 * This function will allocate a dmif buffer and program required
 * pixel duration for pipe
 */
void dce110_allocate_mem_v_input(
	struct mem_input *mem_input,
	uint32_t h_total,/* for current stream */
	uint32_t v_total,/* for current stream */
	uint32_t pix_clk_khz,/* for current stream */
	uint32_t total_stream_num);

/*
 * This function will deallocate a dmif buffer from pipe
 */
void dce110_free_mem_v_input(
	struct mem_input *mem_input,
	uint32_t total_stream_num);

/*
 * This function programs hsync/vsync mode and surface address
 */
bool dce110_mem_input_v_program_surface_flip_and_addr(
	struct mem_input *mem_input,
	const struct dc_plane_address *address,
	bool flip_immediate);

/*
 * dce110_mem_input_v_program_scatter_gather
 *
 * This function will program scatter gather registers.
 */
bool  dce110_mem_input_v_program_pte_vm(
	struct mem_input *mem_input,
	enum surface_pixel_format format,
	union dc_tiling_info *tiling_info,
	enum dc_rotation_angle rotation);

/*
 * This function will program surface tiling, size, rotation and pixel format
 * to corresponding dcp registers.
 */
bool  dce110_mem_input_v_program_surface_config(
	struct mem_input *mem_input,
	enum surface_pixel_format format,
	union dc_tiling_info *tiling_info,
	union plane_size *plane_size,
	enum dc_rotation_angle rotation,
	bool visible);

#endif
