/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef __DC_LINK_ENCODER__DCN30_H__
#define __DC_LINK_ENCODER__DCN30_H__

#include "dcn20/dcn20_link_encoder.h"

#define LE_DCN3_REG_LIST(id)\
	SRI(DIG_BE_CNTL, DIG, id), \
	SRI(DIG_BE_EN_CNTL, DIG, id), \
	SRI(TMDS_CTL_BITS, DIG, id), \
	SRI(TMDS_DCBALANCER_CONTROL, DIG, id), \
	SRI(DP_CONFIG, DP, id), \
	SRI(DP_DPHY_CNTL, DP, id), \
	SRI(DP_DPHY_PRBS_CNTL, DP, id), \
	SRI(DP_DPHY_SCRAM_CNTL, DP, id),\
	SRI(DP_DPHY_SYM0, DP, id), \
	SRI(DP_DPHY_SYM1, DP, id), \
	SRI(DP_DPHY_SYM2, DP, id), \
	SRI(DP_DPHY_TRAINING_PATTERN_SEL, DP, id), \
	SRI(DP_LINK_CNTL, DP, id), \
	SRI(DP_LINK_FRAMING_CNTL, DP, id), \
	SRI(DP_MSE_SAT0, DP, id), \
	SRI(DP_MSE_SAT1, DP, id), \
	SRI(DP_MSE_SAT2, DP, id), \
	SRI(DP_MSE_SAT_UPDATE, DP, id), \
	SRI(DP_SEC_CNTL, DP, id), \
	SRI(DP_VID_STREAM_CNTL, DP, id), \
	SRI(DP_DPHY_FAST_TRAINING, DP, id), \
	SRI(DP_SEC_CNTL1, DP, id), \
	SRI(DP_DPHY_BS_SR_SWAP_CNTL, DP, id), \
	SRI(DP_DPHY_HBR2_PATTERN_CONTROL, DP, id)

#define LINK_ENCODER_MASK_SH_LIST_DCN30(mask_sh) \
	LINK_ENCODER_MASK_SH_LIST_DCN20(mask_sh)

#define DPCS_DCN3_MASK_SH_LIST(mask_sh)\
	DPCS_DCN2_MASK_SH_LIST(mask_sh),\
	LE_SF(DPCSTX0_DPCSTX_TX_CNTL, DPCS_TX_DATA_ORDER_INVERT_18_BIT, mask_sh),\
	LE_SF(RDPCSTX0_RDPCSTX_PHY_CNTL0, RDPCS_PHY_TX_VBOOST_LVL, mask_sh),\
	LE_SF(RDPCSTX0_RDPCSTX_CLOCK_CNTL, RDPCS_TX_CLK_EN, mask_sh),\
	LE_SF(RDPCSTX0_RDPCSTX_PHY_CNTL6, RDPCS_PHY_DPALT_DP4, mask_sh),\
	LE_SF(RDPCSTX0_RDPCSTX_PHY_CNTL6, RDPCS_PHY_DPALT_DISABLE, mask_sh)


void dcn30_link_encoder_construct(
	struct dcn20_link_encoder *enc20,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	const struct dcn10_link_enc_aux_registers *aux_regs,
	const struct dcn10_link_enc_hpd_registers *hpd_regs,
	const struct dcn10_link_enc_shift *link_shift,
	const struct dcn10_link_enc_mask *link_mask);

void enc3_hw_init(struct link_encoder *enc);

#endif /* __DC_LINK_ENCODER__DCN30_H__ */
