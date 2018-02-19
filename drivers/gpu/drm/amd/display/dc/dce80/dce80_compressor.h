/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_COMPRESSOR_DCE80_H__
#define __DC_COMPRESSOR_DCE80_H__

#include "../inc/compressor.h"

#define TO_DCE80_COMPRESSOR(compressor)\
	container_of(compressor, struct dce80_compressor, base)

struct dce80_compressor_reg_offsets {
	uint32_t dcp_offset;
	uint32_t dmif_offset;
};

struct dce80_compressor {
	struct compressor base;
	struct dce80_compressor_reg_offsets offsets;
};

struct compressor *dce80_compressor_create(struct dc_context *ctx);

void dce80_compressor_construct(struct dce80_compressor *cp80,
		struct dc_context *ctx);

void dce80_compressor_destroy(struct compressor **cp);

/* FBC RELATED */
void dce80_compressor_power_up_fbc(struct compressor *cp);

void dce80_compressor_enable_fbc(struct compressor *cp, uint32_t paths_num,
	struct compr_addr_and_pitch_params *params);

void dce80_compressor_disable_fbc(struct compressor *cp);

void dce80_compressor_set_fbc_invalidation_triggers(struct compressor *cp,
	uint32_t fbc_trigger);

void dce80_compressor_program_compressed_surface_address_and_pitch(
	struct compressor *cp,
	struct compr_addr_and_pitch_params *params);

bool dce80_compressor_is_fbc_enabled_in_hw(struct compressor *cp,
	uint32_t *fbc_mapped_crtc_id);

/* LPT RELATED */
void dce80_compressor_enable_lpt(struct compressor *cp);

void dce80_compressor_disable_lpt(struct compressor *cp);

void dce80_compressor_program_lpt_control(struct compressor *cp,
	struct compr_addr_and_pitch_params *params);

bool dce80_compressor_is_lpt_enabled_in_hw(struct compressor *cp);

#endif
