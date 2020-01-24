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

#ifndef __DC_LINK_ENCODER__DCN20_H__
#define __DC_LINK_ENCODER__DCN20_H__

#include "dcn10/dcn10_link_encoder.h"

#define DCN2_AUX_REG_LIST(id)\
	AUX_REG_LIST(id), \
	SRI(AUX_DPHY_TX_CONTROL, DP_AUX, id)

#define UNIPHY_MASK_SH_LIST(mask_sh)\
	LE_SF(UNIPHYA_CHANNEL_XBAR_CNTL, UNIPHY_LINK_ENABLE, mask_sh)

#define LINK_ENCODER_MASK_SH_LIST_DCN20(mask_sh)\
	LINK_ENCODER_MASK_SH_LIST_DCN10(mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_FEC_EN, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_FEC_READY_SHADOW, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_FEC_ACTIVE_STATUS, mask_sh),\
	LE_SF(DIG0_DIG_LANE_ENABLE, DIG_LANE0EN, mask_sh),\
	LE_SF(DIG0_DIG_LANE_ENABLE, DIG_LANE1EN, mask_sh),\
	LE_SF(DIG0_DIG_LANE_ENABLE, DIG_LANE2EN, mask_sh),\
	LE_SF(DIG0_DIG_LANE_ENABLE, DIG_LANE3EN, mask_sh),\
	LE_SF(DIG0_DIG_LANE_ENABLE, DIG_CLK_EN, mask_sh),\
	LE_SF(DIG0_TMDS_CTL_BITS, TMDS_CTL0, mask_sh), \
	UNIPHY_MASK_SH_LIST(mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_START_WINDOW, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_HALF_SYM_DETECT_LEN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_TRANSITION_FILTER_EN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_ALLOW_BELOW_THRESHOLD_PHASE_DETECT, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_ALLOW_BELOW_THRESHOLD_START, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_ALLOW_BELOW_THRESHOLD_STOP, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_PHASE_DETECT_LEN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_DETECTION_THRESHOLD, mask_sh), \
	LE_SF(DP_AUX0_AUX_DPHY_TX_CONTROL, AUX_TX_PRECHARGE_LEN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_TX_CONTROL, AUX_TX_PRECHARGE_SYMBOLS, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_TX_CONTROL, AUX_MODE_DET_CHECK_DELAY, mask_sh)

#define UNIPHY_DCN2_REG_LIST(id) \
	SRI(CLOCK_ENABLE, SYMCLK, id), \
	SRI(CHANNEL_XBAR_CNTL, UNIPHY, id)

struct mpll_cfg {
	uint32_t mpllb_ana_v2i;
	uint32_t mpllb_ana_freq_vco;
	uint32_t mpllb_ana_cp_int;
	uint32_t mpllb_ana_cp_prop;
	uint32_t mpllb_multiplier;
	uint32_t ref_clk_mpllb_div;
	bool mpllb_word_div2_en;
	bool mpllb_ssc_en;
	bool mpllb_div5_clk_en;
	bool mpllb_div_clk_en;
	bool mpllb_fracn_en;
	bool mpllb_pmix_en;
	uint32_t mpllb_div_multiplier;
	uint32_t mpllb_tx_clk_div;
	uint32_t mpllb_fracn_quot;
	uint32_t mpllb_fracn_den;
	uint32_t mpllb_ssc_peak;
	uint32_t mpllb_ssc_stepsize;
	uint32_t mpllb_ssc_up_spread;
	uint32_t mpllb_fracn_rem;
	uint32_t mpllb_hdmi_div;
	// TODO: May not mpll params, need to figure out.
	uint32_t tx_vboost_lvl;
	uint32_t hdmi_pixel_clk_div;
	uint32_t ref_range;
	uint32_t ref_clk;
	bool hdmimode_enable;
	bool sup_pre_hp;
	bool dp_tx0_vergdrv_byp;
	bool dp_tx1_vergdrv_byp;
	bool dp_tx2_vergdrv_byp;
	bool dp_tx3_vergdrv_byp;


};

struct dpcssys_phy_seq_cfg {
	bool program_fuse;
	bool bypass_sram;
	bool lane_en[4];
	bool use_calibration_setting;
	struct mpll_cfg mpll_cfg;
	bool load_sram_fw;
#if 0

	bool hdmimode_enable;
	bool silver2;
	bool ext_refclk_en;
	uint32_t dp_tx0_term_ctrl;
	uint32_t dp_tx1_term_ctrl;
	uint32_t dp_tx2_term_ctrl;
	uint32_t dp_tx3_term_ctrl;
	uint32_t fw_data[0x1000];
	uint32_t dp_tx0_width;
	uint32_t dp_tx1_width;
	uint32_t dp_tx2_width;
	uint32_t dp_tx3_width;
	uint32_t dp_tx0_rate;
	uint32_t dp_tx1_rate;
	uint32_t dp_tx2_rate;
	uint32_t dp_tx3_rate;
	uint32_t dp_tx0_eq_main;
	uint32_t dp_tx0_eq_pre;
	uint32_t dp_tx0_eq_post;
	uint32_t dp_tx1_eq_main;
	uint32_t dp_tx1_eq_pre;
	uint32_t dp_tx1_eq_post;
	uint32_t dp_tx2_eq_main;
	uint32_t dp_tx2_eq_pre;
	uint32_t dp_tx2_eq_post;
	uint32_t dp_tx3_eq_main;
	uint32_t dp_tx3_eq_pre;
	uint32_t dp_tx3_eq_post;
	bool data_swap_en;
	bool data_order_invert_en;
	uint32_t ldpcs_fifo_start_delay;
	uint32_t rdpcs_fifo_start_delay;
	bool rdpcs_reg_fifo_error_mask;
	bool rdpcs_tx_fifo_error_mask;
	bool rdpcs_dpalt_disable_mask;
	bool rdpcs_dpalt_4lane_mask;
#endif
};

struct dcn20_link_encoder {
	struct dcn10_link_encoder enc10;
	struct dpcssys_phy_seq_cfg phy_seq_cfg;
};

void enc2_fec_set_enable(struct link_encoder *enc, bool enable);
void enc2_fec_set_ready(struct link_encoder *enc, bool ready);
bool enc2_fec_is_active(struct link_encoder *enc);
void enc2_hw_init(struct link_encoder *enc);

#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
void link_enc2_read_state(struct link_encoder *enc, struct link_enc_state *s);
#endif

void dcn20_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source);

void dcn20_link_encoder_construct(
	struct dcn20_link_encoder *enc20,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	const struct dcn10_link_enc_aux_registers *aux_regs,
	const struct dcn10_link_enc_hpd_registers *hpd_regs,
	const struct dcn10_link_enc_shift *link_shift,
	const struct dcn10_link_enc_mask *link_mask);

#endif /* __DC_LINK_ENCODER__DCN20_H__ */
