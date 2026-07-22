/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __DCN50_DPP_H__
#define __DCN50_DPP_H__

#include "dcn20/dcn20_dpp.h"
#include "dcn30/dcn30_dpp.h"
#include "dcn32/dcn32_dpp.h"
#include "dcn401/dcn401_dpp.h"


#define TO_DCN50_DPP(dpp)\
	container_of(dpp, struct dcn50_dpp, base)

#define DPP_REG_LIST_SH_MASK_DCN50_COMMON(mask_sh)\
	DPP_REG_LIST_SH_MASK_DCN401_COMMON(mask_sh), \
	TF_SF(CNVC_CFG0_PRE_GAM, PRE_GAM_MODE, mask_sh), \
	TF_SF(CNVC_CFG0_PRE_GAM, PRE_DEGAM_SELECT, mask_sh), \
	TF_SF(CNVC_CFG0_PRE_GAM, PRE_REGAM_SELECT, mask_sh)

#define DPP_REG_FIELD_LIST_DCN50(type) \
	DPP_REG_FIELD_LIST_DCN401(type); \
	type PRE_GAM_MODE; \
	type PRE_REGAM_SELECT

#define DPP_REG_VARIABLE_LIST_DCN50 \
	DPP_REG_VARIABLE_LIST_DCN401; \
	uint32_t PRE_GAM;

struct dcn50_dpp_registers {
	DPP_REG_VARIABLE_LIST_DCN50
};

struct dcn50_dpp_shift {
	DPP_REG_FIELD_LIST_DCN50(uint8_t);
};

struct dcn50_dpp_mask {
	DPP_REG_FIELD_LIST_DCN50(uint32_t);
};

struct dcn50_dpp {
	struct dpp base;

	const struct dcn50_dpp_registers *tf_regs;
	const struct dcn50_dpp_shift *tf_shift;
	const struct dcn50_dpp_mask *tf_mask;

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

bool dpp50_construct(
	struct dcn50_dpp *dpp50,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn50_dpp_registers *tf_regs,
	const struct dcn50_dpp_shift *tf_shift,
	const struct dcn50_dpp_mask *tf_mask);

void dpp50_dpp_setup(
	struct dpp *dpp_base,
	enum surface_pixel_format format,
	enum expansion_mode mode,
	struct dc_csc_transform input_csc_color_matrix,
	enum dc_color_space input_color_space,
	struct cnv_alpha_2bit_lut *alpha_2bit_lut);

void dpp50_set_pregam_state(
	struct dpp *dpp_base,
	enum dc_transfer_func_predefined tr,
	enum dc_scaling_linearity scaling);

#endif /* __DCN50_DPP_H__ */
