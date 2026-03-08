/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Advanced Micro Devices, Inc. */


#ifndef __DAL_DCN42_HPO_DP_LINK_ENCODER_H__
#define __DAL_DCN42_HPO_DP_LINK_ENCODER_H__

#include "link_encoder.h"

void hpo_dp_link_encoder42_construct(struct dcn31_hpo_dp_link_encoder *enc31,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn31_hpo_dp_link_encoder_registers *hpo_le_regs,
	const struct dcn31_hpo_dp_link_encoder_shift *hpo_le_shift,
	const struct dcn31_hpo_dp_link_encoder_mask *hpo_le_mask);

#endif   // __DAL_DCN32_HPO_DP_LINK_ENCODER_H__
