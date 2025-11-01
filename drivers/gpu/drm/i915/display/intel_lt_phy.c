// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_print.h>

#include "i915_reg.h"
#include "intel_cx0_phy.h"
#include "intel_cx0_phy_regs.h"
#include "intel_de.h"
#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_hdmi.h"
#include "intel_lt_phy.h"
#include "intel_lt_phy_regs.h"
#include "intel_tc.h"

#define INTEL_LT_PHY_LANE0		BIT(0)
#define INTEL_LT_PHY_LANE1		BIT(1)
#define INTEL_LT_PHY_BOTH_LANES		(INTEL_LT_PHY_LANE1 |\
					 INTEL_LT_PHY_LANE0)

static u8 intel_lt_phy_get_owned_lane_mask(struct intel_encoder *encoder)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	if (!intel_tc_port_in_dp_alt_mode(dig_port))
		return INTEL_LT_PHY_BOTH_LANES;

	return intel_tc_port_max_lane_count(dig_port) > 2
		? INTEL_LT_PHY_BOTH_LANES : INTEL_LT_PHY_LANE0;
}

static void
intel_lt_phy_setup_powerdown(struct intel_encoder *encoder, u8 lane_count)
{
	/*
	 * The new PORT_BUF_CTL6 stuff for dc5 entry and exit needs to be handled
	 * by dmc firmware not explicitly mentioned in Bspec. This leaves this
	 * function as a wrapper only but keeping it expecting future changes.
	 */
	intel_cx0_setup_powerdown(encoder);
}

static void
intel_lt_phy_lane_reset(struct intel_encoder *encoder,
			u8 lane_count)
{
	struct intel_display *display = to_intel_display(encoder);
	enum port port = encoder->port;
	enum phy phy = intel_encoder_to_phy(encoder);
	u8 owned_lane_mask = intel_lt_phy_get_owned_lane_mask(encoder);
	u32 lane_pipe_reset = owned_lane_mask == INTEL_LT_PHY_BOTH_LANES
				? XELPDP_LANE_PIPE_RESET(0) | XELPDP_LANE_PIPE_RESET(1)
				: XELPDP_LANE_PIPE_RESET(0);
	u32 lane_phy_current_status = owned_lane_mask == INTEL_LT_PHY_BOTH_LANES
					? (XELPDP_LANE_PHY_CURRENT_STATUS(0) |
					   XELPDP_LANE_PHY_CURRENT_STATUS(1))
					: XELPDP_LANE_PHY_CURRENT_STATUS(0);
	u32 lane_phy_pulse_status = owned_lane_mask == INTEL_LT_PHY_BOTH_LANES
					? (XE3PLPDP_LANE_PHY_PULSE_STATUS(0) |
					   XE3PLPDP_LANE_PHY_PULSE_STATUS(1))
					: XE3PLPDP_LANE_PHY_PULSE_STATUS(0);

	intel_de_rmw(display, XE3PLPD_PORT_BUF_CTL5(port),
		     XE3PLPD_MACCLK_RATE_MASK, XE3PLPD_MACCLK_RATE_DEF);

	intel_de_rmw(display, XELPDP_PORT_BUF_CTL1(display, port),
		     XE3PLPDP_PHY_MODE_MASK, XE3PLPDP_PHY_MODE_DP);

	intel_lt_phy_setup_powerdown(encoder, lane_count);

	intel_de_rmw(display, XE3PLPD_PORT_BUF_CTL5(port),
		     XE3PLPD_MACCLK_RESET_0, 0);

	intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, port),
		     XELPDP_LANE_PCLK_PLL_REQUEST(0),
		     XELPDP_LANE_PCLK_PLL_REQUEST(0));

	if (intel_de_wait_custom(display, XELPDP_PORT_CLOCK_CTL(display, port),
				 XELPDP_LANE_PCLK_PLL_ACK(0),
				 XELPDP_LANE_PCLK_PLL_ACK(0),
				 XE3PLPD_MACCLK_TURNON_LATENCY_US,
				 XE3PLPD_MACCLK_TURNON_LATENCY_MS, NULL))
		drm_warn(display->drm, "PHY %c PLL MacCLK assertion Ack not done after %dus.\n",
			 phy_name(phy), XE3PLPD_MACCLK_TURNON_LATENCY_MS * 1000);

	intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, port),
		     XELPDP_FORWARD_CLOCK_UNGATE,
		     XELPDP_FORWARD_CLOCK_UNGATE);

	intel_de_rmw(display, XELPDP_PORT_BUF_CTL2(display, port),
		     lane_pipe_reset | lane_phy_pulse_status, 0);

	if (intel_de_wait_custom(display, XELPDP_PORT_BUF_CTL2(display, port),
				 lane_phy_current_status, 0,
				 XE3PLPD_RESET_END_LATENCY_US, 2, NULL))
		drm_warn(display->drm,
			 "PHY %c failed to bring out of Lane reset after %dus.\n",
			 phy_name(phy), XE3PLPD_RESET_END_LATENCY_US);

	if (intel_de_wait_custom(display, XELPDP_PORT_BUF_CTL2(display, port),
				 lane_phy_pulse_status, lane_phy_pulse_status,
				 XE3PLPD_RATE_CALIB_DONE_LATENCY_US, 0, NULL))
		drm_warn(display->drm, "PHY %c PLL rate not changed after %dus.\n",
			 phy_name(phy), XE3PLPD_RATE_CALIB_DONE_LATENCY_US);

	intel_de_rmw(display, XELPDP_PORT_BUF_CTL2(display, port), lane_phy_pulse_status, 0);
}

static void
intel_lt_phy_program_port_clock_ctl(struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state,
				    bool lane_reversal)
{
	struct intel_display *display = to_intel_display(encoder);
	u32 val = 0;

	intel_de_rmw(display, XELPDP_PORT_BUF_CTL1(display, encoder->port),
		     XELPDP_PORT_REVERSAL,
		     lane_reversal ? XELPDP_PORT_REVERSAL : 0);

	val |= XELPDP_FORWARD_CLOCK_UNGATE;

	/*
	 * We actually mean MACCLK here and not MAXPCLK when using LT Phy
	 * but since the register bits still remain the same we use
	 * the same definition
	 */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI) &&
	    intel_hdmi_is_frl(crtc_state->port_clock))
		val |= XELPDP_DDI_CLOCK_SELECT_PREP(display, XELPDP_DDI_CLOCK_SELECT_DIV18CLK);
	else
		val |= XELPDP_DDI_CLOCK_SELECT_PREP(display, XELPDP_DDI_CLOCK_SELECT_MAXPCLK);

	intel_de_rmw(display, XELPDP_PORT_CLOCK_CTL(display, encoder->port),
		     XELPDP_LANE1_PHY_CLOCK_SELECT | XELPDP_FORWARD_CLOCK_UNGATE |
		     XELPDP_DDI_CLOCK_SELECT_MASK(display) | XELPDP_SSC_ENABLE_PLLA |
		     XELPDP_SSC_ENABLE_PLLB, val);
}

void intel_lt_phy_pll_enable(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool lane_reversal = dig_port->lane_reversal;

	/* 1. Enable MacCLK at default 162 MHz frequency. */
	intel_lt_phy_lane_reset(encoder, crtc_state->lane_count);

	/* 2. Program PORT_CLOCK_CTL register to configure clock muxes, gating, and SSC. */
	intel_lt_phy_program_port_clock_ctl(encoder, crtc_state, lane_reversal);

	/* 3. Change owned PHY lanes power to Ready state. */
	/*
	 * 4. Read the PHY message bus VDR register PHY_VDR_0_Config check enabled PLL type,
	 * encoded rate and encoded mode.
	 */
	/*
	 * 5. Program the PHY internal PLL registers over PHY message bus for the desired
	 * frequency and protocol type
	 */
	/* 6. Use the P2P transaction flow */
	/*
	 * 6.1. Set the PHY VDR register 0xCC4[Rate Control VDR Update] = 1 over PHY message
	 * bus for Owned PHY Lanes.
	 */
	/*
	 * 6.2. Poll for P2P Transaction Ready = "1" and read the MAC message bus VDR register
	 * at offset 0xC00 for Owned PHY Lanes.
	 */
	/* 6.3. Clear P2P transaction Ready bit. */
	/* 7. Program PORT_CLOCK_CTL[PCLK PLL Request LN0] = 0. */
	/* 8. Poll for PORT_CLOCK_CTL[PCLK PLL Ack LN0]= 0. */
	/*
	 * 9. Follow the Display Voltage Frequency Switching - Sequence Before Frequency Change.
	 * We handle this step in bxt_set_cdclk()
	 */
	/* 10. Program DDI_CLK_VALFREQ to match intended DDI clock frequency. */
	/* 11. Program PORT_CLOCK_CTL[PCLK PLL Request LN0] = 1. */
	/* 12. Poll for PORT_CLOCK_CTL[PCLK PLL Ack LN0]= 1. */
	/* 13. Ungate the forward clock by setting PORT_CLOCK_CTL[Forward Clock Ungate] = 1. */
	/* 14. SW clears PORT_BUF_CTL2 [PHY Pulse Status]. */
	/*
	 * 15. Clear the PHY VDR register 0xCC4[Rate Control VDR Update] over PHY message bus for
	 * Owned PHY Lanes.
	 */
	/* 16. Poll for PORT_BUF_CTL2 register PHY Pulse Status = 1 for Owned PHY Lanes. */
	/* 17. SW clears PORT_BUF_CTL2 [PHY Pulse Status]. */
	/*
	 * 18. Follow the Display Voltage Frequency Switching - Sequence After Frequency Change.
	 * We handle this step in bxt_set_cdclk()
	 */
	/* 19. Move the PHY powerdown state to Active and program to enable/disable transmitters */
}
