/* Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef __DCN201_DPP_H__
#define __DCN201_DPP_H__

#include "dcn20/dcn20_dpp.h"

#define TO_DCN201_DPP(dpp)\
	container_of(dpp, struct dcn201_dpp, base)

#define TF_REG_LIST_DCN201(id) \
	TF_REG_LIST_DCN20(id)

#define TF_REG_LIST_SH_MASK_DCN201(mask_sh)\
	TF_REG_LIST_SH_MASK_DCN20(mask_sh)

#define TF_REG_FIELD_LIST_DCN201(type) \
	TF_REG_FIELD_LIST_DCN2_0(type)

struct dcn201_dpp_shift {
	TF_REG_FIELD_LIST_DCN201(uint8_t);
};

struct dcn201_dpp_mask {
	TF_REG_FIELD_LIST_DCN201(uint32_t);
};

#define DPP_DCN201_REG_VARIABLE_LIST \
	DPP_DCN2_REG_VARIABLE_LIST

struct dcn201_dpp_registers {
	DPP_DCN201_REG_VARIABLE_LIST;
};

struct dcn201_dpp {
	struct dpp base;

	const struct dcn201_dpp_registers *tf_regs;
	const struct dcn201_dpp_shift *tf_shift;
	const struct dcn201_dpp_mask *tf_mask;

	const uint16_t *filter_v;
	const uint16_t *filter_h;
	const uint16_t *filter_v_c;
	const uint16_t *filter_h_c;
	int lb_pixel_depth_supported;
	int lb_memory_size;
	int lb_bits_per_entry;
	bool is_write_to_ram_a_safe;
	struct scaler_data scl_data;
	struct pwl_params pwl_data;
};

bool dpp201_construct(struct dcn201_dpp *dpp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn201_dpp_registers *tf_regs,
	const struct dcn201_dpp_shift *tf_shift,
	const struct dcn201_dpp_mask *tf_mask);

#endif /* __DC_HWSS_DCN201_H__ */
