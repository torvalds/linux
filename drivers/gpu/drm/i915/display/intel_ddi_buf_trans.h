/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _INTEL_DDI_BUF_TRANS_H_
#define _INTEL_DDI_BUF_TRANS_H_

#include <linux/types.h>

struct drm_i915_private;
struct intel_encoder;
struct intel_crtc_state;

struct ddi_buf_trans {
	u32 trans1;	/* balance leg enable, de-emph level */
	u32 trans2;	/* vref sel, vswing */
	u8 i_boost;	/* SKL: I_boost; valid: 0x0, 0x1, 0x3, 0x7 */
};

struct bxt_ddi_buf_trans {
	u8 margin;	/* swing value */
	u8 scale;	/* scale value */
	u8 enable;	/* scale enable */
	u8 deemphasis;
};

struct cnl_ddi_buf_trans {
	u8 dw2_swing_sel;
	u8 dw7_n_scalar;
	u8 dw4_cursor_coeff;
	u8 dw4_post_cursor_2;
	u8 dw4_post_cursor_1;
};

struct icl_mg_phy_ddi_buf_trans {
	u32 cri_txdeemph_override_11_6;
	u32 cri_txdeemph_override_5_0;
	u32 cri_txdeemph_override_17_12;
};

struct tgl_dkl_phy_ddi_buf_trans {
	u32 dkl_vswing_control;
	u32 dkl_preshoot_control;
	u32 dkl_de_emphasis_control;
};

bool is_hobl_buf_trans(const struct cnl_ddi_buf_trans *table);

int intel_ddi_hdmi_num_entries(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       int *default_entry);

const struct ddi_buf_trans *
intel_ddi_get_buf_trans_edp(struct intel_encoder *encoder, int *n_entries);
const struct ddi_buf_trans *
intel_ddi_get_buf_trans_fdi(struct drm_i915_private *dev_priv,
			    int *n_entries);
const struct ddi_buf_trans *
intel_ddi_get_buf_trans_hdmi(struct intel_encoder *encoder,
			     int *n_entries);
const struct ddi_buf_trans *
intel_ddi_get_buf_trans_dp(struct intel_encoder *encoder, int *n_entries);

const struct bxt_ddi_buf_trans *
bxt_get_buf_trans(struct intel_encoder *encoder,
		  const struct intel_crtc_state *crtc_state,
		  int *n_entries);

const struct cnl_ddi_buf_trans *
tgl_get_combo_buf_trans(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			int *n_entries);
const struct tgl_dkl_phy_ddi_buf_trans *
tgl_get_dkl_buf_trans(struct intel_encoder *encoder,
		      const struct intel_crtc_state *crtc_state,
		      int *n_entries);
const struct cnl_ddi_buf_trans *
jsl_get_combo_buf_trans(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			int *n_entries);
const struct cnl_ddi_buf_trans *
ehl_get_combo_buf_trans(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			int *n_entries);
const struct cnl_ddi_buf_trans *
icl_get_combo_buf_trans(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state,
			int *n_entries);
const struct icl_mg_phy_ddi_buf_trans *
icl_get_mg_buf_trans(struct intel_encoder *encoder,
		     const struct intel_crtc_state *crtc_state,
		     int *n_entries);

const struct cnl_ddi_buf_trans *
cnl_get_buf_trans(struct intel_encoder *encoder,
		  const struct intel_crtc_state *crtc_state,
		  int *n_entries);

#endif
