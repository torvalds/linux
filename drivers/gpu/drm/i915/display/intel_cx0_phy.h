// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_CX0_PHY_H__
#define __INTEL_CX0_PHY_H__

#include <linux/types.h>

#define MB_WRITE_COMMITTED      true
#define MB_WRITE_UNCOMMITTED    false

struct drm_printer;
enum icl_port_dpll_id;
struct intel_atomic_state;
struct intel_c10pll_state;
struct intel_c20pll_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_cx0pll_state;
struct intel_display;
struct intel_dpll;
struct intel_dpll_hw_state;
struct intel_encoder;
struct intel_hdmi;

void intel_clear_response_ready_flag(struct intel_encoder *encoder,
				     int lane);
bool intel_encoder_is_c10phy(struct intel_encoder *encoder);
void intel_mtl_pll_enable(struct intel_encoder *encoder,
			  struct intel_dpll *pll,
			  const struct intel_dpll_hw_state *dpll_hw_state);
void intel_mtl_pll_disable(struct intel_encoder *encoder);
enum icl_port_dpll_id
intel_mtl_port_pll_type(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state);
void intel_mtl_pll_enable_clock(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state);
void intel_mtl_pll_disable_clock(struct intel_encoder *encoder);
void intel_mtl_pll_disable_clock(struct intel_encoder *encoder);
void intel_mtl_tbt_pll_enable_clock(struct intel_encoder *encoder,
				    int port_clock);
void intel_mtl_tbt_pll_disable_clock(struct intel_encoder *encoder);

int intel_cx0pll_calc_state(const struct intel_crtc_state *crtc_state,
			    struct intel_encoder *encoder,
			    struct intel_dpll_hw_state *hw_state);
bool intel_cx0pll_readout_hw_state(struct intel_encoder *encoder,
				   struct intel_cx0pll_state *pll_state);
int intel_cx0pll_calc_port_clock(struct intel_encoder *encoder,
				 const struct intel_cx0pll_state *pll_state);

void intel_cx0pll_dump_hw_state(struct drm_printer *p,
				const struct intel_cx0pll_state *hw_state);
bool intel_cx0pll_compare_hw_state(const struct intel_cx0pll_state *a,
				   const struct intel_cx0pll_state *b);
void intel_cx0_phy_set_signal_levels(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state);
void intel_cx0_powerdown_change_sequence(struct intel_encoder *encoder,
					 u8 lane_mask, u8 state);
int intel_cx0_phy_check_hdmi_link_rate(struct intel_hdmi *hdmi, int clock);
void intel_cx0_setup_powerdown(struct intel_encoder *encoder);
bool intel_cx0_is_hdmi_frl(u32 clock);
u8 intel_cx0_read(struct intel_encoder *encoder, u8 lane_mask, u16 addr);
void intel_cx0_rmw(struct intel_encoder *encoder,
		   u8 lane_mask, u16 addr, u8 clear, u8 set, bool committed);
void intel_cx0_write(struct intel_encoder *encoder,
		     u8 lane_mask, u16 addr, u8 data, bool committed);
int intel_cx0_wait_for_ack(struct intel_encoder *encoder,
			   int command, int lane, u32 *val);
void intel_cx0_bus_reset(struct intel_encoder *encoder, int lane);

void intel_mtl_tbt_pll_calc_state(struct intel_dpll_hw_state *hw_state);
bool intel_mtl_tbt_pll_readout_hw_state(struct intel_display *display,
					struct intel_dpll *pll,
					struct intel_dpll_hw_state *hw_state);
int intel_mtl_tbt_calc_port_clock(struct intel_encoder *encoder);

void intel_cx0_pll_power_save_wa(struct intel_display *display);
void intel_lnl_mac_transmit_lfps(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state);
void intel_mtl_tbt_pll_enable(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state);
void intel_mtl_tbt_pll_disable(struct intel_encoder *encoder);

#endif /* __INTEL_CX0_PHY_H__ */
