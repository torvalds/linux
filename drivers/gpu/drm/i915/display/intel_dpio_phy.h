/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DPIO_PHY_H__
#define __INTEL_DPIO_PHY_H__

#include <linux/types.h>

enum pipe;
enum port;
struct drm_i915_private;
struct intel_crtc_state;
struct intel_digital_port;
struct intel_encoder;

enum dpio_channel {
	DPIO_CH0,
	DPIO_CH1,
};

enum dpio_phy {
	DPIO_PHY0,
	DPIO_PHY1,
	DPIO_PHY2,
};

#ifdef I915
void bxt_port_to_phy_channel(struct drm_i915_private *dev_priv, enum port port,
			     enum dpio_phy *phy, enum dpio_channel *ch);
void bxt_ddi_phy_set_signal_levels(struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state);
void bxt_ddi_phy_init(struct drm_i915_private *dev_priv, enum dpio_phy phy);
void bxt_ddi_phy_uninit(struct drm_i915_private *dev_priv, enum dpio_phy phy);
bool bxt_ddi_phy_is_enabled(struct drm_i915_private *dev_priv,
			    enum dpio_phy phy);
bool bxt_ddi_phy_verify_state(struct drm_i915_private *dev_priv,
			      enum dpio_phy phy);
u8 bxt_ddi_phy_calc_lane_lat_optim_mask(u8 lane_count);
void bxt_ddi_phy_set_lane_optim_mask(struct intel_encoder *encoder,
				     u8 lane_lat_optim_mask);
u8 bxt_ddi_phy_get_lane_lat_optim_mask(struct intel_encoder *encoder);

enum dpio_channel vlv_dig_port_to_channel(struct intel_digital_port *dig_port);
enum dpio_phy vlv_dig_port_to_phy(struct intel_digital_port *dig_port);
enum dpio_phy vlv_pipe_to_phy(enum pipe pipe);
enum dpio_channel vlv_pipe_to_channel(enum pipe pipe);

void chv_set_phy_signal_level(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state,
			      u32 deemph_reg_value, u32 margin_reg_value,
			      bool uniq_trans_scale);
void chv_data_lane_soft_reset(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state,
			      bool reset);
void chv_phy_pre_pll_enable(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state);
void chv_phy_pre_encoder_enable(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state);
void chv_phy_release_cl2_override(struct intel_encoder *encoder);
void chv_phy_post_pll_disable(struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state);

void vlv_set_phy_signal_level(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state,
			      u32 demph_reg_value, u32 preemph_reg_value,
			      u32 uniqtranscale_reg_value, u32 tx3_demph);
void vlv_phy_pre_pll_enable(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state);
void vlv_phy_pre_encoder_enable(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state);
void vlv_phy_reset_lanes(struct intel_encoder *encoder,
			 const struct intel_crtc_state *old_crtc_state);
#else
static inline void bxt_port_to_phy_channel(struct drm_i915_private *dev_priv, enum port port,
					   enum dpio_phy *phy, enum dpio_channel *ch)
{
}
static inline void bxt_ddi_phy_set_signal_levels(struct intel_encoder *encoder,
						 const struct intel_crtc_state *crtc_state)
{
}
static inline void bxt_ddi_phy_init(struct drm_i915_private *dev_priv, enum dpio_phy phy)
{
}
static inline void bxt_ddi_phy_uninit(struct drm_i915_private *dev_priv, enum dpio_phy phy)
{
}
static inline bool bxt_ddi_phy_is_enabled(struct drm_i915_private *dev_priv,
					  enum dpio_phy phy)
{
	return false;
}
static inline bool bxt_ddi_phy_verify_state(struct drm_i915_private *dev_priv,
					    enum dpio_phy phy)
{
	return true;
}
static inline u8 bxt_ddi_phy_calc_lane_lat_optim_mask(u8 lane_count)
{
	return 0;
}
static inline void bxt_ddi_phy_set_lane_optim_mask(struct intel_encoder *encoder,
						   u8 lane_lat_optim_mask)
{
}
static inline u8 bxt_ddi_phy_get_lane_lat_optim_mask(struct intel_encoder *encoder)
{
	return 0;
}
static inline enum dpio_channel vlv_dig_port_to_channel(struct intel_digital_port *dig_port)
{
	return DPIO_CH0;
}
static inline enum dpio_phy vlv_dig_port_to_phy(struct intel_digital_port *dig_port)
{
	return DPIO_PHY0;
}
static inline enum dpio_phy vlv_pipe_to_phy(enum pipe pipe)
{
	return DPIO_PHY0;
}
static inline enum dpio_channel vlv_pipe_to_channel(enum pipe pipe)
{
	return DPIO_CH0;
}
static inline void chv_set_phy_signal_level(struct intel_encoder *encoder,
					    const struct intel_crtc_state *crtc_state,
					    u32 deemph_reg_value, u32 margin_reg_value,
					    bool uniq_trans_scale)
{
}
static inline void chv_data_lane_soft_reset(struct intel_encoder *encoder,
					    const struct intel_crtc_state *crtc_state,
					    bool reset)
{
}
static inline void chv_phy_pre_pll_enable(struct intel_encoder *encoder,
					  const struct intel_crtc_state *crtc_state)
{
}
static inline void chv_phy_pre_encoder_enable(struct intel_encoder *encoder,
					      const struct intel_crtc_state *crtc_state)
{
}
static inline void chv_phy_release_cl2_override(struct intel_encoder *encoder)
{
}
static inline void chv_phy_post_pll_disable(struct intel_encoder *encoder,
					    const struct intel_crtc_state *old_crtc_state)
{
}

static inline void vlv_set_phy_signal_level(struct intel_encoder *encoder,
					    const struct intel_crtc_state *crtc_state,
					    u32 demph_reg_value, u32 preemph_reg_value,
					    u32 uniqtranscale_reg_value, u32 tx3_demph)
{
}
static inline void vlv_phy_pre_pll_enable(struct intel_encoder *encoder,
					  const struct intel_crtc_state *crtc_state)
{
}
static inline void vlv_phy_pre_encoder_enable(struct intel_encoder *encoder,
					      const struct intel_crtc_state *crtc_state)
{
}
static inline void vlv_phy_reset_lanes(struct intel_encoder *encoder,
				       const struct intel_crtc_state *old_crtc_state)
{
}
#endif

#endif /* __INTEL_DPIO_PHY_H__ */
