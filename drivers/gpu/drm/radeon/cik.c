/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include "drmP.h"
#include "radeon.h"
#include "cikd.h"
#include "atom.h"

/*
 * Core functions
 */
/**
 * cik_tiling_mode_table_init - init the hw tiling table
 *
 * @rdev: radeon_device pointer
 *
 * Starting with SI, the tiling setup is done globally in a
 * set of 32 tiling modes.  Rather than selecting each set of
 * parameters per surface as on older asics, we just select
 * which index in the tiling table we want to use, and the
 * surface uses those parameters (CIK).
 */
static void cik_tiling_mode_table_init(struct radeon_device *rdev)
{
	const u32 num_tile_mode_states = 32;
	const u32 num_secondary_tile_mode_states = 16;
	u32 reg_offset, gb_tile_moden, split_equal_to_row_size;
	u32 num_pipe_configs;
	u32 num_rbs = rdev->config.cik.max_backends_per_se *
		rdev->config.cik.max_shader_engines;

	switch (rdev->config.cik.mem_row_size_in_kb) {
	case 1:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_1KB;
		break;
	case 2:
	default:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_2KB;
		break;
	case 4:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_4KB;
		break;
	}

	num_pipe_configs = rdev->config.cik.max_tile_pipes;
	if (num_pipe_configs > 8)
		num_pipe_configs = 8; /* ??? */

	if (num_pipe_configs == 8) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
				break;
			case 1:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
				break;
			case 2:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 3:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
				break;
			case 4:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 5:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
				break;
			case 6:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 7:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 8:
				gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16));
				break;
			case 9:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
				break;
			case 10:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 11:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 12:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 13:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
				break;
			case 14:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 16:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 17:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 27:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
				break;
			case 28:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 29:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 30:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 1:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 2:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 3:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 4:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 5:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 6:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_2_BANK));
				break;
			case 8:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 9:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 10:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 11:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 12:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 13:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 14:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_2_BANK));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			WREG32(GB_MACROTILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else if (num_pipe_configs == 4) {
		if (num_rbs == 4) {
			for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
				switch (reg_offset) {
				case 0:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
					break;
				case 1:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
					break;
				case 2:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 3:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
					break;
				case 4:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 5:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
					break;
				case 6:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 7:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 8:
					gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16));
					break;
				case 9:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
					break;
				case 10:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 11:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 12:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 13:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
					break;
				case 14:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 16:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 17:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 27:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
					break;
				case 28:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 29:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 30:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				default:
					gb_tile_moden = 0;
					break;
				}
				WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
			}
		} else if (num_rbs < 4) {
			for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
				switch (reg_offset) {
				case 0:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
					break;
				case 1:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
					break;
				case 2:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 3:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
					break;
				case 4:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 5:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
					break;
				case 6:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
					break;
				case 7:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 TILE_SPLIT(split_equal_to_row_size));
					break;
				case 8:
					gb_tile_moden = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
						 PIPE_CONFIG(ADDR_SURF_P4_8x16));
					break;
				case 9:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
					break;
				case 10:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 11:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 12:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 13:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
					break;
				case 14:
					gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 16:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 17:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 27:
					gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
					break;
				case 28:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 29:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				case 30:
					gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
							 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
							 PIPE_CONFIG(ADDR_SURF_P4_8x16) |
							 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
					break;
				default:
					gb_tile_moden = 0;
					break;
				}
				WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
			}
		}
		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 1:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 2:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 3:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 4:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 5:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 6:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			case 8:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 9:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 10:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 11:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 12:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 13:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 14:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
						 NUM_BANKS(ADDR_SURF_4_BANK));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			WREG32(GB_MACROTILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else if (num_pipe_configs == 2) {
		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B));
				break;
			case 1:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B));
				break;
			case 2:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 3:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B));
				break;
			case 4:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 5:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
				break;
			case 6:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B));
				break;
			case 7:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 TILE_SPLIT(split_equal_to_row_size));
				break;
			case 8:
				gb_tile_moden = ARRAY_MODE(ARRAY_LINEAR_ALIGNED);
				break;
			case 9:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING));
				break;
			case 10:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 11:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 12:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 13:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING));
				break;
			case 14:
				gb_tile_moden = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 16:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 17:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 27:
				gb_tile_moden = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING));
				break;
			case 28:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 29:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			case 30:
				gb_tile_moden = (ARRAY_MODE(ARRAY_PRT_2D_TILED_THIN1) |
						 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
						 PIPE_CONFIG(ADDR_SURF_P2) |
						 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++) {
			switch (reg_offset) {
			case 0:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 1:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 2:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 3:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 4:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 5:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 6:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			case 8:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 9:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 10:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 11:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 12:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 13:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
						 NUM_BANKS(ADDR_SURF_16_BANK));
				break;
			case 14:
				gb_tile_moden = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
						 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
						 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
						 NUM_BANKS(ADDR_SURF_8_BANK));
				break;
			default:
				gb_tile_moden = 0;
				break;
			}
			WREG32(GB_MACROTILE_MODE0 + (reg_offset * 4), gb_tile_moden);
		}
	} else
		DRM_ERROR("unknown num pipe config: 0x%x\n", num_pipe_configs);
}

/**
 * cik_select_se_sh - select which SE, SH to address
 *
 * @rdev: radeon_device pointer
 * @se_num: shader engine to address
 * @sh_num: sh block to address
 *
 * Select which SE, SH combinations to address. Certain
 * registers are instanced per SE or SH.  0xffffffff means
 * broadcast to all SEs or SHs (CIK).
 */
static void cik_select_se_sh(struct radeon_device *rdev,
			     u32 se_num, u32 sh_num)
{
	u32 data = INSTANCE_BROADCAST_WRITES;

	if ((se_num == 0xffffffff) && (sh_num == 0xffffffff))
		data = SH_BROADCAST_WRITES | SE_BROADCAST_WRITES;
	else if (se_num == 0xffffffff)
		data |= SE_BROADCAST_WRITES | SH_INDEX(sh_num);
	else if (sh_num == 0xffffffff)
		data |= SH_BROADCAST_WRITES | SE_INDEX(se_num);
	else
		data |= SH_INDEX(sh_num) | SE_INDEX(se_num);
	WREG32(GRBM_GFX_INDEX, data);
}

/**
 * cik_create_bitmask - create a bitmask
 *
 * @bit_width: length of the mask
 *
 * create a variable length bit mask (CIK).
 * Returns the bitmask.
 */
static u32 cik_create_bitmask(u32 bit_width)
{
	u32 i, mask = 0;

	for (i = 0; i < bit_width; i++) {
		mask <<= 1;
		mask |= 1;
	}
	return mask;
}

/**
 * cik_select_se_sh - select which SE, SH to address
 *
 * @rdev: radeon_device pointer
 * @max_rb_num: max RBs (render backends) for the asic
 * @se_num: number of SEs (shader engines) for the asic
 * @sh_per_se: number of SH blocks per SE for the asic
 *
 * Calculates the bitmask of disabled RBs (CIK).
 * Returns the disabled RB bitmask.
 */
static u32 cik_get_rb_disabled(struct radeon_device *rdev,
			      u32 max_rb_num, u32 se_num,
			      u32 sh_per_se)
{
	u32 data, mask;

	data = RREG32(CC_RB_BACKEND_DISABLE);
	if (data & 1)
		data &= BACKEND_DISABLE_MASK;
	else
		data = 0;
	data |= RREG32(GC_USER_RB_BACKEND_DISABLE);

	data >>= BACKEND_DISABLE_SHIFT;

	mask = cik_create_bitmask(max_rb_num / se_num / sh_per_se);

	return data & mask;
}

/**
 * cik_setup_rb - setup the RBs on the asic
 *
 * @rdev: radeon_device pointer
 * @se_num: number of SEs (shader engines) for the asic
 * @sh_per_se: number of SH blocks per SE for the asic
 * @max_rb_num: max RBs (render backends) for the asic
 *
 * Configures per-SE/SH RB registers (CIK).
 */
static void cik_setup_rb(struct radeon_device *rdev,
			 u32 se_num, u32 sh_per_se,
			 u32 max_rb_num)
{
	int i, j;
	u32 data, mask;
	u32 disabled_rbs = 0;
	u32 enabled_rbs = 0;

	for (i = 0; i < se_num; i++) {
		for (j = 0; j < sh_per_se; j++) {
			cik_select_se_sh(rdev, i, j);
			data = cik_get_rb_disabled(rdev, max_rb_num, se_num, sh_per_se);
			disabled_rbs |= data << ((i * sh_per_se + j) * CIK_RB_BITMAP_WIDTH_PER_SH);
		}
	}
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);

	mask = 1;
	for (i = 0; i < max_rb_num; i++) {
		if (!(disabled_rbs & mask))
			enabled_rbs |= mask;
		mask <<= 1;
	}

	for (i = 0; i < se_num; i++) {
		cik_select_se_sh(rdev, i, 0xffffffff);
		data = 0;
		for (j = 0; j < sh_per_se; j++) {
			switch (enabled_rbs & 3) {
			case 1:
				data |= (RASTER_CONFIG_RB_MAP_0 << (i * sh_per_se + j) * 2);
				break;
			case 2:
				data |= (RASTER_CONFIG_RB_MAP_3 << (i * sh_per_se + j) * 2);
				break;
			case 3:
			default:
				data |= (RASTER_CONFIG_RB_MAP_2 << (i * sh_per_se + j) * 2);
				break;
			}
			enabled_rbs >>= 2;
		}
		WREG32(PA_SC_RASTER_CONFIG, data);
	}
	cik_select_se_sh(rdev, 0xffffffff, 0xffffffff);
}

/**
 * cik_gpu_init - setup the 3D engine
 *
 * @rdev: radeon_device pointer
 *
 * Configures the 3D engine and tiling configuration
 * registers so that the 3D engine is usable.
 */
static void cik_gpu_init(struct radeon_device *rdev)
{
	u32 gb_addr_config = RREG32(GB_ADDR_CONFIG);
	u32 mc_shared_chmap, mc_arb_ramcfg;
	u32 hdp_host_path_cntl;
	u32 tmp;
	int i, j;

	switch (rdev->family) {
	case CHIP_BONAIRE:
		rdev->config.cik.max_shader_engines = 2;
		rdev->config.cik.max_tile_pipes = 4;
		rdev->config.cik.max_cu_per_sh = 7;
		rdev->config.cik.max_sh_per_se = 1;
		rdev->config.cik.max_backends_per_se = 2;
		rdev->config.cik.max_texture_channel_caches = 4;
		rdev->config.cik.max_gprs = 256;
		rdev->config.cik.max_gs_threads = 32;
		rdev->config.cik.max_hw_contexts = 8;

		rdev->config.cik.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.cik.sc_prim_fifo_size_backend = 0x100;
		rdev->config.cik.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cik.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = BONAIRE_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_KAVERI:
		/* TODO */
		break;
	case CHIP_KABINI:
	default:
		rdev->config.cik.max_shader_engines = 1;
		rdev->config.cik.max_tile_pipes = 2;
		rdev->config.cik.max_cu_per_sh = 2;
		rdev->config.cik.max_sh_per_se = 1;
		rdev->config.cik.max_backends_per_se = 1;
		rdev->config.cik.max_texture_channel_caches = 2;
		rdev->config.cik.max_gprs = 256;
		rdev->config.cik.max_gs_threads = 16;
		rdev->config.cik.max_hw_contexts = 8;

		rdev->config.cik.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.cik.sc_prim_fifo_size_backend = 0x100;
		rdev->config.cik.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.cik.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = BONAIRE_GB_ADDR_CONFIG_GOLDEN;
		break;
	}

	/* Initialize HDP */
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		WREG32((0x2c14 + j), 0x00000000);
		WREG32((0x2c18 + j), 0x00000000);
		WREG32((0x2c1c + j), 0x00000000);
		WREG32((0x2c20 + j), 0x00000000);
		WREG32((0x2c24 + j), 0x00000000);
	}

	WREG32(GRBM_CNTL, GRBM_READ_TIMEOUT(0xff));

	WREG32(BIF_FB_EN, FB_READ_EN | FB_WRITE_EN);

	mc_shared_chmap = RREG32(MC_SHARED_CHMAP);
	mc_arb_ramcfg = RREG32(MC_ARB_RAMCFG);

	rdev->config.cik.num_tile_pipes = rdev->config.cik.max_tile_pipes;
	rdev->config.cik.mem_max_burst_length_bytes = 256;
	tmp = (mc_arb_ramcfg & NOOFCOLS_MASK) >> NOOFCOLS_SHIFT;
	rdev->config.cik.mem_row_size_in_kb = (4 * (1 << (8 + tmp))) / 1024;
	if (rdev->config.cik.mem_row_size_in_kb > 4)
		rdev->config.cik.mem_row_size_in_kb = 4;
	/* XXX use MC settings? */
	rdev->config.cik.shader_engine_tile_size = 32;
	rdev->config.cik.num_gpus = 1;
	rdev->config.cik.multi_gpu_tile_size = 64;

	/* fix up row size */
	gb_addr_config &= ~ROW_SIZE_MASK;
	switch (rdev->config.cik.mem_row_size_in_kb) {
	case 1:
	default:
		gb_addr_config |= ROW_SIZE(0);
		break;
	case 2:
		gb_addr_config |= ROW_SIZE(1);
		break;
	case 4:
		gb_addr_config |= ROW_SIZE(2);
		break;
	}

	/* setup tiling info dword.  gb_addr_config is not adequate since it does
	 * not have bank info, so create a custom tiling dword.
	 * bits 3:0   num_pipes
	 * bits 7:4   num_banks
	 * bits 11:8  group_size
	 * bits 15:12 row_size
	 */
	rdev->config.cik.tile_config = 0;
	switch (rdev->config.cik.num_tile_pipes) {
	case 1:
		rdev->config.cik.tile_config |= (0 << 0);
		break;
	case 2:
		rdev->config.cik.tile_config |= (1 << 0);
		break;
	case 4:
		rdev->config.cik.tile_config |= (2 << 0);
		break;
	case 8:
	default:
		/* XXX what about 12? */
		rdev->config.cik.tile_config |= (3 << 0);
		break;
	}
	if ((mc_arb_ramcfg & NOOFBANK_MASK) >> NOOFBANK_SHIFT)
		rdev->config.cik.tile_config |= 1 << 4;
	else
		rdev->config.cik.tile_config |= 0 << 4;
	rdev->config.cik.tile_config |=
		((gb_addr_config & PIPE_INTERLEAVE_SIZE_MASK) >> PIPE_INTERLEAVE_SIZE_SHIFT) << 8;
	rdev->config.cik.tile_config |=
		((gb_addr_config & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT) << 12;

	WREG32(GB_ADDR_CONFIG, gb_addr_config);
	WREG32(HDP_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CALC, gb_addr_config);

	cik_tiling_mode_table_init(rdev);

	cik_setup_rb(rdev, rdev->config.cik.max_shader_engines,
		     rdev->config.cik.max_sh_per_se,
		     rdev->config.cik.max_backends_per_se);

	/* set HW defaults for 3D engine */
	WREG32(CP_MEQ_THRESHOLDS, MEQ1_START(0x30) | MEQ2_START(0x60));

	WREG32(SX_DEBUG_1, 0x20);

	WREG32(TA_CNTL_AUX, 0x00010000);

	tmp = RREG32(SPI_CONFIG_CNTL);
	tmp |= 0x03000000;
	WREG32(SPI_CONFIG_CNTL, tmp);

	WREG32(SQ_CONFIG, 1);

	WREG32(DB_DEBUG, 0);

	tmp = RREG32(DB_DEBUG2) & ~0xf00fffff;
	tmp |= 0x00000400;
	WREG32(DB_DEBUG2, tmp);

	tmp = RREG32(DB_DEBUG3) & ~0x0002021c;
	tmp |= 0x00020200;
	WREG32(DB_DEBUG3, tmp);

	tmp = RREG32(CB_HW_CONTROL) & ~0x00010000;
	tmp |= 0x00018208;
	WREG32(CB_HW_CONTROL, tmp);

	WREG32(SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(4));

	WREG32(PA_SC_FIFO_SIZE, (SC_FRONTEND_PRIM_FIFO_SIZE(rdev->config.cik.sc_prim_fifo_size_frontend) |
				 SC_BACKEND_PRIM_FIFO_SIZE(rdev->config.cik.sc_prim_fifo_size_backend) |
				 SC_HIZ_TILE_FIFO_SIZE(rdev->config.cik.sc_hiz_tile_fifo_size) |
				 SC_EARLYZ_TILE_FIFO_SIZE(rdev->config.cik.sc_earlyz_tile_fifo_size)));

	WREG32(VGT_NUM_INSTANCES, 1);

	WREG32(CP_PERFMON_CNTL, 0);

	WREG32(SQ_CONFIG, 0);

	WREG32(PA_SC_FORCE_EOV_MAX_CNTS, (FORCE_EOV_MAX_CLK_CNT(4095) |
					  FORCE_EOV_MAX_REZ_CNT(255)));

	WREG32(VGT_CACHE_INVALIDATION, CACHE_INVALIDATION(VC_AND_TC) |
	       AUTO_INVLD_EN(ES_AND_GS_AUTO));

	WREG32(VGT_GS_VERTEX_REUSE, 16);
	WREG32(PA_SC_LINE_STIPPLE_STATE, 0);

	tmp = RREG32(HDP_MISC_CNTL);
	tmp |= HDP_FLUSH_INVALIDATE_CACHE;
	WREG32(HDP_MISC_CNTL, tmp);

	hdp_host_path_cntl = RREG32(HDP_HOST_PATH_CNTL);
	WREG32(HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	WREG32(PA_CL_ENHANCE, CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3));
	WREG32(PA_SC_ENHANCE, ENABLE_PA_SC_OUT_OF_ORDER);

	udelay(50);
}

