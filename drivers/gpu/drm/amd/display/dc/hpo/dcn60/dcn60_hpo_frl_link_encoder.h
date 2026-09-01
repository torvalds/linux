// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DAL_DCN60_HPO_FRL_LINK_ENCODER_H__
#define __DAL_DCN60_HPO_FRL_LINK_ENCODER_H__

#include "link_encoder.h"
#include "dcn30/dcn30_hpo_frl_link_encoder.h"

void hpo_frl_link_encoder60_construct(struct dcn30_hpo_frl_link_encoder *enc3,
				     struct dc_context *ctx,
				     uint32_t inst,
				     const struct dcn30_hpo_frl_link_encoder_registers *hpo_le_regs,
				     const struct dcn30_hpo_frl_link_encoder_shift *hpo_le_shift,
				     const struct dcn30_hpo_frl_link_encoder_mask *hpo_le_mask);

#endif
