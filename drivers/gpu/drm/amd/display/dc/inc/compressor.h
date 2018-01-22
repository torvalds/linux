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

#ifndef __DAL_COMPRESSOR_H__
#define __DAL_COMPRESSOR_H__

#include "include/grph_object_id.h"
#include "bios_parser_interface.h"

enum fbc_compress_ratio {
	FBC_COMPRESS_RATIO_INVALID = 0,
	FBC_COMPRESS_RATIO_1TO1 = 1,
	FBC_COMPRESS_RATIO_2TO1 = 2,
	FBC_COMPRESS_RATIO_4TO1 = 4,
	FBC_COMPRESS_RATIO_8TO1 = 8,
};

union fbc_physical_address {
	struct {
		uint32_t low_part;
		int32_t high_part;
	} addr;
	uint64_t quad_part;
};

struct compr_addr_and_pitch_params {
	/* enum controller_id controller_id; */
	uint32_t inst;
	uint32_t source_view_width;
	uint32_t source_view_height;
};

enum fbc_hw_max_resolution_supported {
	FBC_MAX_X = 3840,
	FBC_MAX_Y = 2400,
	FBC_MAX_X_SG = 1920,
	FBC_MAX_Y_SG = 1080,
};

struct compressor;

struct compressor_funcs {

	void (*power_up_fbc)(struct compressor *cp);
	void (*enable_fbc)(struct compressor *cp,
		struct compr_addr_and_pitch_params *params);
	void (*disable_fbc)(struct compressor *cp);
	void (*set_fbc_invalidation_triggers)(struct compressor *cp,
		uint32_t fbc_trigger);
	void (*surface_address_and_pitch)(
		struct compressor *cp,
		struct compr_addr_and_pitch_params *params);
	bool (*is_fbc_enabled_in_hw)(struct compressor *cp,
		uint32_t *fbc_mapped_crtc_id);
};
struct compressor {
	struct dc_context *ctx;
	uint32_t attached_inst;
	bool is_enabled;
	const struct compressor_funcs *funcs;
	union {
		uint32_t raw;
		struct {
			uint32_t FBC_SUPPORT:1;
			uint32_t FB_POOL:1;
			uint32_t DYNAMIC_ALLOC:1;
			uint32_t LPT_SUPPORT:1;
			uint32_t LPT_MC_CONFIG:1;
			uint32_t DUMMY_BACKEND:1;
			uint32_t CLK_GATING_DISABLED:1;

		} bits;
	} options;

	union fbc_physical_address compr_surface_address;

	uint32_t embedded_panel_h_size;
	uint32_t embedded_panel_v_size;
	uint32_t memory_bus_width;
	uint32_t banks_num;
	uint32_t raw_size;
	uint32_t channel_interleave_size;
	uint32_t dram_channels_num;

	uint32_t allocated_size;
	uint32_t preferred_requested_size;
	uint32_t lpt_channels_num;
	enum fbc_compress_ratio min_compress_ratio;
};

struct fbc_input_info {
	bool           dynamic_fbc_buffer_alloc;
	unsigned int   source_view_width;
	unsigned int   source_view_height;
	unsigned int   num_of_active_targets;
};


struct fbc_requested_compressed_size {
	unsigned int   preferred_size;
	unsigned int   preferred_size_alignment;
	unsigned int   min_size;
	unsigned int   min_size_alignment;
	union {
		struct {
			/* Above preferedSize must be allocated in FB pool */
			unsigned int preferred_must_be_framebuffer_pool : 1;
			/* Above minSize must be allocated in FB pool */
			unsigned int min_must_be_framebuffer_pool : 1;
		} bits;
		unsigned int flags;
	};
};
#endif
