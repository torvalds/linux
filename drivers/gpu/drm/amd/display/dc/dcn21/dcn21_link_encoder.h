/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#ifndef __DC_LINK_ENCODER__DCN21_H__
#define __DC_LINK_ENCODER__DCN21_H__

#include "dcn20/dcn20_link_encoder.h"

struct dcn21_link_encoder {
	struct dcn10_link_encoder enc10;
	struct dpcssys_phy_seq_cfg phy_seq_cfg;
};

#define LINK_ENCODER_MASK_SH_LIST_DCN21(mask_sh)\
	LINK_ENCODER_MASK_SH_LIST_DCN20(mask_sh),\
	LE_SF(UNIPHYA_CHANNEL_XBAR_CNTL, UNIPHY_CHANNEL0_XBAR_SOURCE, mask_sh),\
	LE_SF(UNIPHYA_CHANNEL_XBAR_CNTL, UNIPHY_CHANNEL1_XBAR_SOURCE, mask_sh),\
	LE_SF(UNIPHYA_CHANNEL_XBAR_CNTL, UNIPHY_CHANNEL2_XBAR_SOURCE, mask_sh),\
	LE_SF(UNIPHYA_CHANNEL_XBAR_CNTL, UNIPHY_CHANNEL3_XBAR_SOURCE, mask_sh), \
	SRI(RDPCSTX_PHY_FUSE2, RDPCSTX, id), \
	SRI(RDPCSTX_PHY_FUSE3, RDPCSTX, id), \
	SR(RDPCSTX0_RDPCSTX_SCRATCH)

void dcn21_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source);

void dcn21_link_encoder_construct(
	struct dcn21_link_encoder *enc21,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	const struct dcn10_link_enc_aux_registers *aux_regs,
	const struct dcn10_link_enc_hpd_registers *hpd_regs,
	const struct dcn10_link_enc_shift *link_shift,
	const struct dcn10_link_enc_mask *link_mask);

#endif
