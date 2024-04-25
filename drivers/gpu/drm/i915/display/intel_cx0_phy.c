// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/log2.h>
#include <linux/math64.h>
#include "i915_reg.h"
#include "intel_cx0_phy.h"
#include "intel_cx0_phy_regs.h"
#include "intel_ddi.h"
#include "intel_ddi_buf_trans.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_hdmi.h"
#include "intel_panel.h"
#include "intel_psr.h"
#include "intel_tc.h"

#define MB_WRITE_COMMITTED      true
#define MB_WRITE_UNCOMMITTED    false

#define for_each_cx0_lane_in_mask(__lane_mask, __lane) \
	for ((__lane) = 0; (__lane) < 2; (__lane)++) \
		for_each_if((__lane_mask) & BIT(__lane))

#define INTEL_CX0_LANE0		BIT(0)
#define INTEL_CX0_LANE1		BIT(1)
#define INTEL_CX0_BOTH_LANES	(INTEL_CX0_LANE1 | INTEL_CX0_LANE0)

bool intel_encoder_is_c10phy(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_encoder_to_phy(encoder);

	if ((IS_LUNARLAKE(i915) || IS_METEORLAKE(i915)) && phy < PHY_C)
		return true;

	return false;
}

static int lane_mask_to_lane(u8 lane_mask)
{
	if (WARN_ON((lane_mask & ~INTEL_CX0_BOTH_LANES) ||
		    hweight8(lane_mask) != 1))
		return 0;

	return ilog2(lane_mask);
}

static u8 intel_cx0_get_owned_lane_mask(struct intel_encoder *encoder)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	if (!intel_tc_port_in_dp_alt_mode(dig_port))
		return INTEL_CX0_BOTH_LANES;

	/*
	 * In DP-alt with pin assignment D, only PHY lane 0 is owned
	 * by display and lane 1 is owned by USB.
	 */
	return intel_tc_port_max_lane_count(dig_port) > 2
		? INTEL_CX0_BOTH_LANES : INTEL_CX0_LANE0;
}

static void
assert_dc_off(struct drm_i915_private *i915)
{
	bool enabled;

	enabled = intel_display_power_is_enabled(i915, POWER_DOMAIN_DC_OFF);
	drm_WARN_ON(&i915->drm, !enabled);
}

static void intel_cx0_program_msgbus_timer(struct intel_encoder *encoder)
{
	int lane;
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	for_each_cx0_lane_in_mask(INTEL_CX0_BOTH_LANES, lane)
		intel_de_rmw(i915,
			     XELPDP_PORT_MSGBUS_TIMER(i915, encoder->port, lane),
			     XELPDP_PORT_MSGBUS_TIMER_VAL_MASK,
			     XELPDP_PORT_MSGBUS_TIMER_VAL);
}

/*
 * Prepare HW for CX0 phy transactions.
 *
 * It is required that PSR and DC5/6 are disabled before any CX0 message
 * bus transaction is executed.
 *
 * We also do the msgbus timer programming here to ensure that the timer
 * is already programmed before any access to the msgbus.
 */
static intel_wakeref_t intel_cx0_phy_transaction_begin(struct intel_encoder *encoder)
{
	intel_wakeref_t wakeref;
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_psr_pause(intel_dp);
	wakeref = intel_display_power_get(i915, POWER_DOMAIN_DC_OFF);
	intel_cx0_program_msgbus_timer(encoder);

	return wakeref;
}

static void intel_cx0_phy_transaction_end(struct intel_encoder *encoder, intel_wakeref_t wakeref)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_psr_resume(intel_dp);
	intel_display_power_put(i915, POWER_DOMAIN_DC_OFF, wakeref);
}

static void intel_clear_response_ready_flag(struct intel_encoder *encoder,
					    int lane)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_de_rmw(i915, XELPDP_PORT_P2M_MSGBUS_STATUS(i915, encoder->port, lane),
		     0, XELPDP_PORT_P2M_RESPONSE_READY | XELPDP_PORT_P2M_ERROR_SET);
}

static void intel_cx0_bus_reset(struct intel_encoder *encoder, int lane)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum phy phy = intel_encoder_to_phy(encoder);

	intel_de_write(i915, XELPDP_PORT_M2P_MSGBUS_CTL(i915, port, lane),
		       XELPDP_PORT_M2P_TRANSACTION_RESET);

	if (intel_de_wait_for_clear(i915, XELPDP_PORT_M2P_MSGBUS_CTL(i915, port, lane),
				    XELPDP_PORT_M2P_TRANSACTION_RESET,
				    XELPDP_MSGBUS_TIMEOUT_SLOW)) {
		drm_err_once(&i915->drm, "Failed to bring PHY %c to idle.\n", phy_name(phy));
		return;
	}

	intel_clear_response_ready_flag(encoder, lane);
}

static int intel_cx0_wait_for_ack(struct intel_encoder *encoder,
				  int command, int lane, u32 *val)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum phy phy = intel_encoder_to_phy(encoder);

	if (intel_de_wait_custom(i915,
				 XELPDP_PORT_P2M_MSGBUS_STATUS(i915, port, lane),
				 XELPDP_PORT_P2M_RESPONSE_READY,
				 XELPDP_PORT_P2M_RESPONSE_READY,
				 XELPDP_MSGBUS_TIMEOUT_FAST_US,
				 XELPDP_MSGBUS_TIMEOUT_SLOW, val)) {
		drm_dbg_kms(&i915->drm, "PHY %c Timeout waiting for message ACK. Status: 0x%x\n",
			    phy_name(phy), *val);

		if (!(intel_de_read(i915, XELPDP_PORT_MSGBUS_TIMER(i915, port, lane)) &
		      XELPDP_PORT_MSGBUS_TIMER_TIMED_OUT))
			drm_dbg_kms(&i915->drm,
				    "PHY %c Hardware did not detect a timeout\n",
				    phy_name(phy));

		intel_cx0_bus_reset(encoder, lane);
		return -ETIMEDOUT;
	}

	if (*val & XELPDP_PORT_P2M_ERROR_SET) {
		drm_dbg_kms(&i915->drm, "PHY %c Error occurred during %s command. Status: 0x%x\n", phy_name(phy),
			    command == XELPDP_PORT_P2M_COMMAND_READ_ACK ? "read" : "write", *val);
		intel_cx0_bus_reset(encoder, lane);
		return -EINVAL;
	}

	if (REG_FIELD_GET(XELPDP_PORT_P2M_COMMAND_TYPE_MASK, *val) != command) {
		drm_dbg_kms(&i915->drm, "PHY %c Not a %s response. MSGBUS Status: 0x%x.\n", phy_name(phy),
			    command == XELPDP_PORT_P2M_COMMAND_READ_ACK ? "read" : "write", *val);
		intel_cx0_bus_reset(encoder, lane);
		return -EINVAL;
	}

	return 0;
}

static int __intel_cx0_read_once(struct intel_encoder *encoder,
				 int lane, u16 addr)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum phy phy = intel_encoder_to_phy(encoder);
	int ack;
	u32 val;

	if (intel_de_wait_for_clear(i915, XELPDP_PORT_M2P_MSGBUS_CTL(i915, port, lane),
				    XELPDP_PORT_M2P_TRANSACTION_PENDING,
				    XELPDP_MSGBUS_TIMEOUT_SLOW)) {
		drm_dbg_kms(&i915->drm,
			    "PHY %c Timeout waiting for previous transaction to complete. Reset the bus and retry.\n", phy_name(phy));
		intel_cx0_bus_reset(encoder, lane);
		return -ETIMEDOUT;
	}

	intel_de_write(i915, XELPDP_PORT_M2P_MSGBUS_CTL(i915, port, lane),
		       XELPDP_PORT_M2P_TRANSACTION_PENDING |
		       XELPDP_PORT_M2P_COMMAND_READ |
		       XELPDP_PORT_M2P_ADDRESS(addr));

	ack = intel_cx0_wait_for_ack(encoder, XELPDP_PORT_P2M_COMMAND_READ_ACK, lane, &val);
	if (ack < 0)
		return ack;

	intel_clear_response_ready_flag(encoder, lane);

	/*
	 * FIXME: Workaround to let HW to settle
	 * down and let the message bus to end up
	 * in a known state
	 */
	intel_cx0_bus_reset(encoder, lane);

	return REG_FIELD_GET(XELPDP_PORT_P2M_DATA_MASK, val);
}

static u8 __intel_cx0_read(struct intel_encoder *encoder,
			   int lane, u16 addr)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_encoder_to_phy(encoder);
	int i, status;

	assert_dc_off(i915);

	/* 3 tries is assumed to be enough to read successfully */
	for (i = 0; i < 3; i++) {
		status = __intel_cx0_read_once(encoder, lane, addr);

		if (status >= 0)
			return status;
	}

	drm_err_once(&i915->drm, "PHY %c Read %04x failed after %d retries.\n",
		     phy_name(phy), addr, i);

	return 0;
}

static u8 intel_cx0_read(struct intel_encoder *encoder,
			 u8 lane_mask, u16 addr)
{
	int lane = lane_mask_to_lane(lane_mask);

	return __intel_cx0_read(encoder, lane, addr);
}

static int __intel_cx0_write_once(struct intel_encoder *encoder,
				  int lane, u16 addr, u8 data, bool committed)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum phy phy = intel_encoder_to_phy(encoder);
	int ack;
	u32 val;

	if (intel_de_wait_for_clear(i915, XELPDP_PORT_M2P_MSGBUS_CTL(i915, port, lane),
				    XELPDP_PORT_M2P_TRANSACTION_PENDING,
				    XELPDP_MSGBUS_TIMEOUT_SLOW)) {
		drm_dbg_kms(&i915->drm,
			    "PHY %c Timeout waiting for previous transaction to complete. Resetting the bus.\n", phy_name(phy));
		intel_cx0_bus_reset(encoder, lane);
		return -ETIMEDOUT;
	}

	intel_de_write(i915, XELPDP_PORT_M2P_MSGBUS_CTL(i915, port, lane),
		       XELPDP_PORT_M2P_TRANSACTION_PENDING |
		       (committed ? XELPDP_PORT_M2P_COMMAND_WRITE_COMMITTED :
				    XELPDP_PORT_M2P_COMMAND_WRITE_UNCOMMITTED) |
		       XELPDP_PORT_M2P_DATA(data) |
		       XELPDP_PORT_M2P_ADDRESS(addr));

	if (intel_de_wait_for_clear(i915, XELPDP_PORT_M2P_MSGBUS_CTL(i915, port, lane),
				    XELPDP_PORT_M2P_TRANSACTION_PENDING,
				    XELPDP_MSGBUS_TIMEOUT_SLOW)) {
		drm_dbg_kms(&i915->drm,
			    "PHY %c Timeout waiting for write to complete. Resetting the bus.\n", phy_name(phy));
		intel_cx0_bus_reset(encoder, lane);
		return -ETIMEDOUT;
	}

	if (committed) {
		ack = intel_cx0_wait_for_ack(encoder, XELPDP_PORT_P2M_COMMAND_WRITE_ACK, lane, &val);
		if (ack < 0)
			return ack;
	} else if ((intel_de_read(i915, XELPDP_PORT_P2M_MSGBUS_STATUS(i915, port, lane)) &
		    XELPDP_PORT_P2M_ERROR_SET)) {
		drm_dbg_kms(&i915->drm,
			    "PHY %c Error occurred during write command.\n", phy_name(phy));
		intel_cx0_bus_reset(encoder, lane);
		return -EINVAL;
	}

	intel_clear_response_ready_flag(encoder, lane);

	/*
	 * FIXME: Workaround to let HW to settle
	 * down and let the message bus to end up
	 * in a known state
	 */
	intel_cx0_bus_reset(encoder, lane);

	return 0;
}

static void __intel_cx0_write(struct intel_encoder *encoder,
			      int lane, u16 addr, u8 data, bool committed)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_encoder_to_phy(encoder);
	int i, status;

	assert_dc_off(i915);

	/* 3 tries is assumed to be enough to write successfully */
	for (i = 0; i < 3; i++) {
		status = __intel_cx0_write_once(encoder, lane, addr, data, committed);

		if (status == 0)
			return;
	}

	drm_err_once(&i915->drm,
		     "PHY %c Write %04x failed after %d retries.\n", phy_name(phy), addr, i);
}

static void intel_cx0_write(struct intel_encoder *encoder,
			    u8 lane_mask, u16 addr, u8 data, bool committed)
{
	int lane;

	for_each_cx0_lane_in_mask(lane_mask, lane)
		__intel_cx0_write(encoder, lane, addr, data, committed);
}

static void intel_c20_sram_write(struct intel_encoder *encoder,
				 int lane, u16 addr, u16 data)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	assert_dc_off(i915);

	intel_cx0_write(encoder, lane, PHY_C20_WR_ADDRESS_H, addr >> 8, 0);
	intel_cx0_write(encoder, lane, PHY_C20_WR_ADDRESS_L, addr & 0xff, 0);

	intel_cx0_write(encoder, lane, PHY_C20_WR_DATA_H, data >> 8, 0);
	intel_cx0_write(encoder, lane, PHY_C20_WR_DATA_L, data & 0xff, 1);
}

static u16 intel_c20_sram_read(struct intel_encoder *encoder,
			       int lane, u16 addr)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	u16 val;

	assert_dc_off(i915);

	intel_cx0_write(encoder, lane, PHY_C20_RD_ADDRESS_H, addr >> 8, 0);
	intel_cx0_write(encoder, lane, PHY_C20_RD_ADDRESS_L, addr & 0xff, 1);

	val = intel_cx0_read(encoder, lane, PHY_C20_RD_DATA_H);
	val <<= 8;
	val |= intel_cx0_read(encoder, lane, PHY_C20_RD_DATA_L);

	return val;
}

static void __intel_cx0_rmw(struct intel_encoder *encoder,
			    int lane, u16 addr, u8 clear, u8 set, bool committed)
{
	u8 old, val;

	old = __intel_cx0_read(encoder, lane, addr);
	val = (old & ~clear) | set;

	if (val != old)
		__intel_cx0_write(encoder, lane, addr, val, committed);
}

static void intel_cx0_rmw(struct intel_encoder *encoder,
			  u8 lane_mask, u16 addr, u8 clear, u8 set, bool committed)
{
	u8 lane;

	for_each_cx0_lane_in_mask(lane_mask, lane)
		__intel_cx0_rmw(encoder, lane, addr, clear, set, committed);
}

static u8 intel_c10_get_tx_vboost_lvl(const struct intel_crtc_state *crtc_state)
{
	if (intel_crtc_has_dp_encoder(crtc_state)) {
		if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP) &&
		    (crtc_state->port_clock == 540000 ||
		     crtc_state->port_clock == 810000))
			return 5;
		else
			return 4;
	} else {
		return 5;
	}
}

static u8 intel_c10_get_tx_term_ctl(const struct intel_crtc_state *crtc_state)
{
	if (intel_crtc_has_dp_encoder(crtc_state)) {
		if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP) &&
		    (crtc_state->port_clock == 540000 ||
		     crtc_state->port_clock == 810000))
			return 5;
		else
			return 2;
	} else {
		return 6;
	}
}

void intel_cx0_phy_set_signal_levels(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_ddi_buf_trans *trans;
	u8 owned_lane_mask;
	intel_wakeref_t wakeref;
	int n_entries, ln;
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		return;

	owned_lane_mask = intel_cx0_get_owned_lane_mask(encoder);

	wakeref = intel_cx0_phy_transaction_begin(encoder);

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(&i915->drm, !trans)) {
		intel_cx0_phy_transaction_end(encoder, wakeref);
		return;
	}

	if (intel_encoder_is_c10phy(encoder)) {
		intel_cx0_rmw(encoder, owned_lane_mask, PHY_C10_VDR_CONTROL(1),
			      0, C10_VDR_CTRL_MSGBUS_ACCESS, MB_WRITE_COMMITTED);
		intel_cx0_rmw(encoder, owned_lane_mask, PHY_C10_VDR_CMN(3),
			      C10_CMN3_TXVBOOST_MASK,
			      C10_CMN3_TXVBOOST(intel_c10_get_tx_vboost_lvl(crtc_state)),
			      MB_WRITE_UNCOMMITTED);
		intel_cx0_rmw(encoder, owned_lane_mask, PHY_C10_VDR_TX(1),
			      C10_TX1_TERMCTL_MASK,
			      C10_TX1_TERMCTL(intel_c10_get_tx_term_ctl(crtc_state)),
			      MB_WRITE_COMMITTED);
	}

	for (ln = 0; ln < crtc_state->lane_count; ln++) {
		int level = intel_ddi_level(encoder, crtc_state, ln);
		int lane = ln / 2;
		int tx = ln % 2;
		u8 lane_mask = lane == 0 ? INTEL_CX0_LANE0 : INTEL_CX0_LANE1;

		if (!(lane_mask & owned_lane_mask))
			continue;

		intel_cx0_rmw(encoder, lane_mask, PHY_CX0_VDROVRD_CTL(lane, tx, 0),
			      C10_PHY_OVRD_LEVEL_MASK,
			      C10_PHY_OVRD_LEVEL(trans->entries[level].snps.pre_cursor),
			      MB_WRITE_COMMITTED);
		intel_cx0_rmw(encoder, lane_mask, PHY_CX0_VDROVRD_CTL(lane, tx, 1),
			      C10_PHY_OVRD_LEVEL_MASK,
			      C10_PHY_OVRD_LEVEL(trans->entries[level].snps.vswing),
			      MB_WRITE_COMMITTED);
		intel_cx0_rmw(encoder, lane_mask, PHY_CX0_VDROVRD_CTL(lane, tx, 2),
			      C10_PHY_OVRD_LEVEL_MASK,
			      C10_PHY_OVRD_LEVEL(trans->entries[level].snps.post_cursor),
			      MB_WRITE_COMMITTED);
	}

	/* Write Override enables in 0xD71 */
	intel_cx0_rmw(encoder, owned_lane_mask, PHY_C10_VDR_OVRD,
		      0, PHY_C10_VDR_OVRD_TX1 | PHY_C10_VDR_OVRD_TX2,
		      MB_WRITE_COMMITTED);

	if (intel_encoder_is_c10phy(encoder))
		intel_cx0_rmw(encoder, owned_lane_mask, PHY_C10_VDR_CONTROL(1),
			      0, C10_VDR_CTRL_UPDATE_CFG, MB_WRITE_COMMITTED);

	intel_cx0_phy_transaction_end(encoder, wakeref);
}

/*
 * Basic DP link rates with 38.4 MHz reference clock.
 * Note: The tables below are with SSC. In non-ssc
 * registers 0xC04 to 0xC08(pll[4] to pll[8]) will be
 * programmed 0.
 */

static const struct intel_c10pll_state mtl_c10_dp_rbr = {
	.clock = 162000,
	.tx = 0x10,
	.cmn = 0x21,
	.pll[0] = 0xB4,
	.pll[1] = 0,
	.pll[2] = 0x30,
	.pll[3] = 0x1,
	.pll[4] = 0x26,
	.pll[5] = 0x0C,
	.pll[6] = 0x98,
	.pll[7] = 0x46,
	.pll[8] = 0x1,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0xC0,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0x2,
	.pll[16] = 0x84,
	.pll[17] = 0x4F,
	.pll[18] = 0xE5,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_edp_r216 = {
	.clock = 216000,
	.tx = 0x10,
	.cmn = 0x21,
	.pll[0] = 0x4,
	.pll[1] = 0,
	.pll[2] = 0xA2,
	.pll[3] = 0x1,
	.pll[4] = 0x33,
	.pll[5] = 0x10,
	.pll[6] = 0x75,
	.pll[7] = 0xB3,
	.pll[8] = 0x1,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0x2,
	.pll[16] = 0x85,
	.pll[17] = 0x0F,
	.pll[18] = 0xE6,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_edp_r243 = {
	.clock = 243000,
	.tx = 0x10,
	.cmn = 0x21,
	.pll[0] = 0x34,
	.pll[1] = 0,
	.pll[2] = 0xDA,
	.pll[3] = 0x1,
	.pll[4] = 0x39,
	.pll[5] = 0x12,
	.pll[6] = 0xE3,
	.pll[7] = 0xE9,
	.pll[8] = 0x1,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0x20,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0x2,
	.pll[16] = 0x85,
	.pll[17] = 0x8F,
	.pll[18] = 0xE6,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_dp_hbr1 = {
	.clock = 270000,
	.tx = 0x10,
	.cmn = 0x21,
	.pll[0] = 0xF4,
	.pll[1] = 0,
	.pll[2] = 0xF8,
	.pll[3] = 0x0,
	.pll[4] = 0x20,
	.pll[5] = 0x0A,
	.pll[6] = 0x29,
	.pll[7] = 0x10,
	.pll[8] = 0x1,   /* Verify */
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0xA0,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0x1,
	.pll[16] = 0x84,
	.pll[17] = 0x4F,
	.pll[18] = 0xE5,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_edp_r324 = {
	.clock = 324000,
	.tx = 0x10,
	.cmn = 0x21,
	.pll[0] = 0xB4,
	.pll[1] = 0,
	.pll[2] = 0x30,
	.pll[3] = 0x1,
	.pll[4] = 0x26,
	.pll[5] = 0x0C,
	.pll[6] = 0x98,
	.pll[7] = 0x46,
	.pll[8] = 0x1,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0xC0,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0x1,
	.pll[16] = 0x85,
	.pll[17] = 0x4F,
	.pll[18] = 0xE6,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_edp_r432 = {
	.clock = 432000,
	.tx = 0x10,
	.cmn = 0x21,
	.pll[0] = 0x4,
	.pll[1] = 0,
	.pll[2] = 0xA2,
	.pll[3] = 0x1,
	.pll[4] = 0x33,
	.pll[5] = 0x10,
	.pll[6] = 0x75,
	.pll[7] = 0xB3,
	.pll[8] = 0x1,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0x1,
	.pll[16] = 0x85,
	.pll[17] = 0x0F,
	.pll[18] = 0xE6,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_dp_hbr2 = {
	.clock = 540000,
	.tx = 0x10,
	.cmn = 0x21,
	.pll[0] = 0xF4,
	.pll[1] = 0,
	.pll[2] = 0xF8,
	.pll[3] = 0,
	.pll[4] = 0x20,
	.pll[5] = 0x0A,
	.pll[6] = 0x29,
	.pll[7] = 0x10,
	.pll[8] = 0x1,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0xA0,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0,
	.pll[16] = 0x84,
	.pll[17] = 0x4F,
	.pll[18] = 0xE5,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_edp_r675 = {
	.clock = 675000,
	.tx = 0x10,
	.cmn = 0x21,
	.pll[0] = 0xB4,
	.pll[1] = 0,
	.pll[2] = 0x3E,
	.pll[3] = 0x1,
	.pll[4] = 0xA8,
	.pll[5] = 0x0C,
	.pll[6] = 0x33,
	.pll[7] = 0x54,
	.pll[8] = 0x1,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0xC8,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0,
	.pll[16] = 0x85,
	.pll[17] = 0x8F,
	.pll[18] = 0xE6,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_dp_hbr3 = {
	.clock = 810000,
	.tx = 0x10,
	.cmn = 0x21,
	.pll[0] = 0x34,
	.pll[1] = 0,
	.pll[2] = 0x84,
	.pll[3] = 0x1,
	.pll[4] = 0x30,
	.pll[5] = 0x0F,
	.pll[6] = 0x3D,
	.pll[7] = 0x98,
	.pll[8] = 0x1,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0xF0,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0,
	.pll[16] = 0x84,
	.pll[17] = 0x0F,
	.pll[18] = 0xE5,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state * const mtl_c10_dp_tables[] = {
	&mtl_c10_dp_rbr,
	&mtl_c10_dp_hbr1,
	&mtl_c10_dp_hbr2,
	&mtl_c10_dp_hbr3,
	NULL,
};

static const struct intel_c10pll_state * const mtl_c10_edp_tables[] = {
	&mtl_c10_dp_rbr,
	&mtl_c10_edp_r216,
	&mtl_c10_edp_r243,
	&mtl_c10_dp_hbr1,
	&mtl_c10_edp_r324,
	&mtl_c10_edp_r432,
	&mtl_c10_dp_hbr2,
	&mtl_c10_edp_r675,
	&mtl_c10_dp_hbr3,
	NULL,
};

/* C20 basic DP 1.4 tables */
static const struct intel_c20pll_state mtl_c20_dp_rbr = {
	.clock = 162000,
	.tx = {	0xbe88, /* tx cfg0 */
		0x5800, /* tx cfg1 */
		0x0000, /* tx cfg2 */
		},
	.cmn = {0x0500, /* cmn cfg0*/
		0x0005, /* cmn cfg1 */
		0x0000, /* cmn cfg2 */
		0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x50a8,	/* mpllb cfg0 */
		0x2120,		/* mpllb cfg1 */
		0xcd9a,		/* mpllb cfg2 */
		0xbfc1,		/* mpllb cfg3 */
		0x5ab8,         /* mpllb cfg4 */
		0x4c34,         /* mpllb cfg5 */
		0x2000,		/* mpllb cfg6 */
		0x0001,		/* mpllb cfg7 */
		0x6000,		/* mpllb cfg8 */
		0x0000,		/* mpllb cfg9 */
		0x0000,		/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_dp_hbr1 = {
	.clock = 270000,
	.tx = {	0xbe88, /* tx cfg0 */
		0x4800, /* tx cfg1 */
		0x0000, /* tx cfg2 */
		},
	.cmn = {0x0500, /* cmn cfg0*/
		0x0005, /* cmn cfg1 */
		0x0000, /* cmn cfg2 */
		0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x308c,	/* mpllb cfg0 */
		0x2110,		/* mpllb cfg1 */
		0xcc9c,		/* mpllb cfg2 */
		0xbfc1,		/* mpllb cfg3 */
		0x4b9a,         /* mpllb cfg4 */
		0x3f81,         /* mpllb cfg5 */
		0x2000,		/* mpllb cfg6 */
		0x0001,		/* mpllb cfg7 */
		0x5000,		/* mpllb cfg8 */
		0x0000,		/* mpllb cfg9 */
		0x0000,		/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_dp_hbr2 = {
	.clock = 540000,
	.tx = {	0xbe88, /* tx cfg0 */
		0x4800, /* tx cfg1 */
		0x0000, /* tx cfg2 */
		},
	.cmn = {0x0500, /* cmn cfg0*/
		0x0005, /* cmn cfg1 */
		0x0000, /* cmn cfg2 */
		0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x108c,	/* mpllb cfg0 */
		0x2108,		/* mpllb cfg1 */
		0xcc9c,		/* mpllb cfg2 */
		0xbfc1,		/* mpllb cfg3 */
		0x4b9a,         /* mpllb cfg4 */
		0x3f81,         /* mpllb cfg5 */
		0x2000,		/* mpllb cfg6 */
		0x0001,		/* mpllb cfg7 */
		0x5000,		/* mpllb cfg8 */
		0x0000,		/* mpllb cfg9 */
		0x0000,		/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_dp_hbr3 = {
	.clock = 810000,
	.tx = {	0xbe88, /* tx cfg0 */
		0x4800, /* tx cfg1 */
		0x0000, /* tx cfg2 */
		},
	.cmn = {0x0500, /* cmn cfg0*/
		0x0005, /* cmn cfg1 */
		0x0000, /* cmn cfg2 */
		0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x10d2,	/* mpllb cfg0 */
		0x2108,		/* mpllb cfg1 */
		0x8d98,		/* mpllb cfg2 */
		0xbfc1,		/* mpllb cfg3 */
		0x7166,         /* mpllb cfg4 */
		0x5f42,         /* mpllb cfg5 */
		0x2000,		/* mpllb cfg6 */
		0x0001,		/* mpllb cfg7 */
		0x7800,		/* mpllb cfg8 */
		0x0000,		/* mpllb cfg9 */
		0x0000,		/* mpllb cfg10 */
		},
};

/* C20 basic DP 2.0 tables */
static const struct intel_c20pll_state mtl_c20_dp_uhbr10 = {
	.clock = 1000000, /* 10 Gbps */
	.tx = {	0xbe21, /* tx cfg0 */
		0xe800, /* tx cfg1 */
		0x0000, /* tx cfg2 */
		},
	.cmn = {0x0700, /* cmn cfg0*/
		0x0005, /* cmn cfg1 */
		0x0000, /* cmn cfg2 */
		0x0000, /* cmn cfg3 */
		},
	.mplla = { 0x3104,	/* mplla cfg0 */
		0xd105,		/* mplla cfg1 */
		0xc025,		/* mplla cfg2 */
		0xc025,		/* mplla cfg3 */
		0x8c00,		/* mplla cfg4 */
		0x759a,		/* mplla cfg5 */
		0x4000,		/* mplla cfg6 */
		0x0003,		/* mplla cfg7 */
		0x3555,		/* mplla cfg8 */
		0x0001,		/* mplla cfg9 */
		},
};

static const struct intel_c20pll_state mtl_c20_dp_uhbr13_5 = {
	.clock = 1350000, /* 13.5 Gbps */
	.tx = {	0xbea0, /* tx cfg0 */
		0x4800, /* tx cfg1 */
		0x0000, /* tx cfg2 */
		},
	.cmn = {0x0500, /* cmn cfg0*/
		0x0005, /* cmn cfg1 */
		0x0000, /* cmn cfg2 */
		0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x015f,	/* mpllb cfg0 */
		0x2205,		/* mpllb cfg1 */
		0x1b17,		/* mpllb cfg2 */
		0xffc1,		/* mpllb cfg3 */
		0xe100,		/* mpllb cfg4 */
		0xbd00,		/* mpllb cfg5 */
		0x2000,		/* mpllb cfg6 */
		0x0001,		/* mpllb cfg7 */
		0x4800,		/* mpllb cfg8 */
		0x0000,		/* mpllb cfg9 */
		0x0000,		/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_dp_uhbr20 = {
	.clock = 2000000, /* 20 Gbps */
	.tx = {	0xbe20, /* tx cfg0 */
		0x4800, /* tx cfg1 */
		0x0000, /* tx cfg2 */
		},
	.cmn = {0x0500, /* cmn cfg0*/
		0x0005, /* cmn cfg1 */
		0x0000, /* cmn cfg2 */
		0x0000, /* cmn cfg3 */
		},
	.mplla = { 0x3104,	/* mplla cfg0 */
		0xd105,		/* mplla cfg1 */
		0xc025,		/* mplla cfg2 */
		0xc025,		/* mplla cfg3 */
		0xa6ab,		/* mplla cfg4 */
		0x8c00,		/* mplla cfg5 */
		0x4000,		/* mplla cfg6 */
		0x0003,		/* mplla cfg7 */
		0x3555,		/* mplla cfg8 */
		0x0001,		/* mplla cfg9 */
		},
};

static const struct intel_c20pll_state * const mtl_c20_dp_tables[] = {
	&mtl_c20_dp_rbr,
	&mtl_c20_dp_hbr1,
	&mtl_c20_dp_hbr2,
	&mtl_c20_dp_hbr3,
	&mtl_c20_dp_uhbr10,
	&mtl_c20_dp_uhbr13_5,
	&mtl_c20_dp_uhbr20,
	NULL,
};

/*
 * HDMI link rates with 38.4 MHz reference clock.
 */

static const struct intel_c10pll_state mtl_c10_hdmi_25_2 = {
	.clock = 25200,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x4,
	.pll[1] = 0,
	.pll[2] = 0xB2,
	.pll[3] = 0,
	.pll[4] = 0,
	.pll[5] = 0,
	.pll[6] = 0,
	.pll[7] = 0,
	.pll[8] = 0x20,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0xD,
	.pll[16] = 0x6,
	.pll[17] = 0x8F,
	.pll[18] = 0x84,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_27_0 = {
	.clock = 27000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34,
	.pll[1] = 0,
	.pll[2] = 0xC0,
	.pll[3] = 0,
	.pll[4] = 0,
	.pll[5] = 0,
	.pll[6] = 0,
	.pll[7] = 0,
	.pll[8] = 0x20,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0x80,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0xD,
	.pll[16] = 0x6,
	.pll[17] = 0xCF,
	.pll[18] = 0x84,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_74_25 = {
	.clock = 74250,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4,
	.pll[1] = 0,
	.pll[2] = 0x7A,
	.pll[3] = 0,
	.pll[4] = 0,
	.pll[5] = 0,
	.pll[6] = 0,
	.pll[7] = 0,
	.pll[8] = 0x20,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0x58,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0xB,
	.pll[16] = 0x6,
	.pll[17] = 0xF,
	.pll[18] = 0x85,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_148_5 = {
	.clock = 148500,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4,
	.pll[1] = 0,
	.pll[2] = 0x7A,
	.pll[3] = 0,
	.pll[4] = 0,
	.pll[5] = 0,
	.pll[6] = 0,
	.pll[7] = 0,
	.pll[8] = 0x20,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0x58,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0xA,
	.pll[16] = 0x6,
	.pll[17] = 0xF,
	.pll[18] = 0x85,
	.pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_594 = {
	.clock = 594000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4,
	.pll[1] = 0,
	.pll[2] = 0x7A,
	.pll[3] = 0,
	.pll[4] = 0,
	.pll[5] = 0,
	.pll[6] = 0,
	.pll[7] = 0,
	.pll[8] = 0x20,
	.pll[9] = 0x1,
	.pll[10] = 0,
	.pll[11] = 0,
	.pll[12] = 0x58,
	.pll[13] = 0,
	.pll[14] = 0,
	.pll[15] = 0x8,
	.pll[16] = 0x6,
	.pll[17] = 0xF,
	.pll[18] = 0x85,
	.pll[19] = 0x23,
};

/* Precomputed C10 HDMI PLL tables */
static const struct intel_c10pll_state mtl_c10_hdmi_27027 = {
	.clock = 27027,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34, .pll[1] = 0x00, .pll[2] = 0xC0, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0xCC, .pll[12] = 0x9C, .pll[13] = 0xCB, .pll[14] = 0xCC,
	.pll[15] = 0x0D, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_28320 = {
	.clock = 28320,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x04, .pll[1] = 0x00, .pll[2] = 0xCC, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x00, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0D, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_30240 = {
	.clock = 30240,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x04, .pll[1] = 0x00, .pll[2] = 0xDC, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x00, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0D, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_31500 = {
	.clock = 31500,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x62, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0xA0, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0C, .pll[16] = 0x09, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_36000 = {
	.clock = 36000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xC4, .pll[1] = 0x00, .pll[2] = 0x76, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x00, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0C, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_40000 = {
	.clock = 40000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xB4, .pll[1] = 0x00, .pll[2] = 0x86, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x55, .pll[12] = 0x55, .pll[13] = 0x55, .pll[14] = 0x55,
	.pll[15] = 0x0C, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_49500 = {
	.clock = 49500,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x74, .pll[1] = 0x00, .pll[2] = 0xAE, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x20, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0C, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_50000 = {
	.clock = 50000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x74, .pll[1] = 0x00, .pll[2] = 0xB0, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0xAA, .pll[12] = 0x2A, .pll[13] = 0xA9, .pll[14] = 0xAA,
	.pll[15] = 0x0C, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_57284 = {
	.clock = 57284,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34, .pll[1] = 0x00, .pll[2] = 0xCE, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x77, .pll[12] = 0x57, .pll[13] = 0x77, .pll[14] = 0x77,
	.pll[15] = 0x0C, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_58000 = {
	.clock = 58000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34, .pll[1] = 0x00, .pll[2] = 0xD0, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x55, .pll[12] = 0xD5, .pll[13] = 0x55, .pll[14] = 0x55,
	.pll[15] = 0x0C, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_65000 = {
	.clock = 65000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x66, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x55, .pll[12] = 0xB5, .pll[13] = 0x55, .pll[14] = 0x55,
	.pll[15] = 0x0B, .pll[16] = 0x09, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_71000 = {
	.clock = 71000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x72, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x55, .pll[12] = 0xF5, .pll[13] = 0x55, .pll[14] = 0x55,
	.pll[15] = 0x0B, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_74176 = {
	.clock = 74176,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x7A, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x44, .pll[12] = 0x44, .pll[13] = 0x44, .pll[14] = 0x44,
	.pll[15] = 0x0B, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_75000 = {
	.clock = 75000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x7C, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x20, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0B, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_78750 = {
	.clock = 78750,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xB4, .pll[1] = 0x00, .pll[2] = 0x84, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x08, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0B, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_85500 = {
	.clock = 85500,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xB4, .pll[1] = 0x00, .pll[2] = 0x92, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x10, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0B, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_88750 = {
	.clock = 88750,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x74, .pll[1] = 0x00, .pll[2] = 0x98, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0xAA, .pll[12] = 0x72, .pll[13] = 0xA9, .pll[14] = 0xAA,
	.pll[15] = 0x0B, .pll[16] = 0x09, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_106500 = {
	.clock = 106500,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34, .pll[1] = 0x00, .pll[2] = 0xBC, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0xF0, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0B, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_108000 = {
	.clock = 108000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34, .pll[1] = 0x00, .pll[2] = 0xC0, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x80, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0B, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_115500 = {
	.clock = 115500,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34, .pll[1] = 0x00, .pll[2] = 0xD0, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x50, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0B, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_119000 = {
	.clock = 119000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34, .pll[1] = 0x00, .pll[2] = 0xD6, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x55, .pll[12] = 0xF5, .pll[13] = 0x55, .pll[14] = 0x55,
	.pll[15] = 0x0B, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_135000 = {
	.clock = 135000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x6C, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x50, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0A, .pll[16] = 0x09, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_138500 = {
	.clock = 138500,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x70, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0xAA, .pll[12] = 0x22, .pll[13] = 0xA9, .pll[14] = 0xAA,
	.pll[15] = 0x0A, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_147160 = {
	.clock = 147160,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x78, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x55, .pll[12] = 0xA5, .pll[13] = 0x55, .pll[14] = 0x55,
	.pll[15] = 0x0A, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_148352 = {
	.clock = 148352,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x7A, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x44, .pll[12] = 0x44, .pll[13] = 0x44, .pll[14] = 0x44,
	.pll[15] = 0x0A, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_154000 = {
	.clock = 154000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xB4, .pll[1] = 0x00, .pll[2] = 0x80, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x55, .pll[12] = 0x35, .pll[13] = 0x55, .pll[14] = 0x55,
	.pll[15] = 0x0A, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_162000 = {
	.clock = 162000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xB4, .pll[1] = 0x00, .pll[2] = 0x88, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x60, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0A, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_167000 = {
	.clock = 167000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xB4, .pll[1] = 0x00, .pll[2] = 0x8C, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0xAA, .pll[12] = 0xFA, .pll[13] = 0xA9, .pll[14] = 0xAA,
	.pll[15] = 0x0A, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_197802 = {
	.clock = 197802,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x74, .pll[1] = 0x00, .pll[2] = 0xAE, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x99, .pll[12] = 0x05, .pll[13] = 0x98, .pll[14] = 0x99,
	.pll[15] = 0x0A, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_198000 = {
	.clock = 198000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x74, .pll[1] = 0x00, .pll[2] = 0xAE, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x20, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0A, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_209800 = {
	.clock = 209800,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34, .pll[1] = 0x00, .pll[2] = 0xBA, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x55, .pll[12] = 0x45, .pll[13] = 0x55, .pll[14] = 0x55,
	.pll[15] = 0x0A, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_241500 = {
	.clock = 241500,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34, .pll[1] = 0x00, .pll[2] = 0xDA, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0xC8, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x0A, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_262750 = {
	.clock = 262750,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x68, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0xAA, .pll[12] = 0x6C, .pll[13] = 0xA9, .pll[14] = 0xAA,
	.pll[15] = 0x09, .pll[16] = 0x09, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_268500 = {
	.clock = 268500,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x6A, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0xEC, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x09, .pll[16] = 0x09, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_296703 = {
	.clock = 296703,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x7A, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x33, .pll[12] = 0x44, .pll[13] = 0x33, .pll[14] = 0x33,
	.pll[15] = 0x09, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_297000 = {
	.clock = 297000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x7A, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x00, .pll[12] = 0x58, .pll[13] = 0x00, .pll[14] = 0x00,
	.pll[15] = 0x09, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_319750 = {
	.clock = 319750,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xB4, .pll[1] = 0x00, .pll[2] = 0x86, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0xAA, .pll[12] = 0x44, .pll[13] = 0xA9, .pll[14] = 0xAA,
	.pll[15] = 0x09, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_497750 = {
	.clock = 497750,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0x34, .pll[1] = 0x00, .pll[2] = 0xE2, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x55, .pll[12] = 0x9F, .pll[13] = 0x55, .pll[14] = 0x55,
	.pll[15] = 0x09, .pll[16] = 0x08, .pll[17] = 0xCF, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_592000 = {
	.clock = 592000,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x7A, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x55, .pll[12] = 0x15, .pll[13] = 0x55, .pll[14] = 0x55,
	.pll[15] = 0x08, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state mtl_c10_hdmi_593407 = {
	.clock = 593407,
	.tx = 0x10,
	.cmn = 0x1,
	.pll[0] = 0xF4, .pll[1] = 0x00, .pll[2] = 0x7A, .pll[3] = 0x00, .pll[4] = 0x00,
	.pll[5] = 0x00, .pll[6] = 0x00, .pll[7] = 0x00, .pll[8] = 0x20, .pll[9] = 0xFF,
	.pll[10] = 0xFF, .pll[11] = 0x3B, .pll[12] = 0x44, .pll[13] = 0xBA, .pll[14] = 0xBB,
	.pll[15] = 0x08, .pll[16] = 0x08, .pll[17] = 0x8F, .pll[18] = 0x84, .pll[19] = 0x23,
};

static const struct intel_c10pll_state * const mtl_c10_hdmi_tables[] = {
	&mtl_c10_hdmi_25_2, /* Consolidated Table */
	&mtl_c10_hdmi_27_0, /* Consolidated Table */
	&mtl_c10_hdmi_27027,
	&mtl_c10_hdmi_28320,
	&mtl_c10_hdmi_30240,
	&mtl_c10_hdmi_31500,
	&mtl_c10_hdmi_36000,
	&mtl_c10_hdmi_40000,
	&mtl_c10_hdmi_49500,
	&mtl_c10_hdmi_50000,
	&mtl_c10_hdmi_57284,
	&mtl_c10_hdmi_58000,
	&mtl_c10_hdmi_65000,
	&mtl_c10_hdmi_71000,
	&mtl_c10_hdmi_74176,
	&mtl_c10_hdmi_74_25, /* Consolidated Table */
	&mtl_c10_hdmi_75000,
	&mtl_c10_hdmi_78750,
	&mtl_c10_hdmi_85500,
	&mtl_c10_hdmi_88750,
	&mtl_c10_hdmi_106500,
	&mtl_c10_hdmi_108000,
	&mtl_c10_hdmi_115500,
	&mtl_c10_hdmi_119000,
	&mtl_c10_hdmi_135000,
	&mtl_c10_hdmi_138500,
	&mtl_c10_hdmi_147160,
	&mtl_c10_hdmi_148352,
	&mtl_c10_hdmi_148_5, /* Consolidated Table */
	&mtl_c10_hdmi_154000,
	&mtl_c10_hdmi_162000,
	&mtl_c10_hdmi_167000,
	&mtl_c10_hdmi_197802,
	&mtl_c10_hdmi_198000,
	&mtl_c10_hdmi_209800,
	&mtl_c10_hdmi_241500,
	&mtl_c10_hdmi_262750,
	&mtl_c10_hdmi_268500,
	&mtl_c10_hdmi_296703,
	&mtl_c10_hdmi_297000,
	&mtl_c10_hdmi_319750,
	&mtl_c10_hdmi_497750,
	&mtl_c10_hdmi_592000,
	&mtl_c10_hdmi_593407,
	&mtl_c10_hdmi_594, /* Consolidated Table */
	NULL,
};

static const struct intel_c20pll_state mtl_c20_hdmi_25_175 = {
	.clock = 25175,
	.tx = {  0xbe88, /* tx cfg0 */
		  0x9800, /* tx cfg1 */
		  0x0000, /* tx cfg2 */
		},
	.cmn = { 0x0500, /* cmn cfg0*/
		  0x0005, /* cmn cfg1 */
		  0x0000, /* cmn cfg2 */
		  0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0xa0d2,	/* mpllb cfg0 */
		   0x7d80,	/* mpllb cfg1 */
		   0x0906,	/* mpllb cfg2 */
		   0xbe40,	/* mpllb cfg3 */
		   0x0000,	/* mpllb cfg4 */
		   0x0000,	/* mpllb cfg5 */
		   0x0200,	/* mpllb cfg6 */
		   0x0001,	/* mpllb cfg7 */
		   0x0000,	/* mpllb cfg8 */
		   0x0000,	/* mpllb cfg9 */
		   0x0001,	/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_hdmi_27_0 = {
	.clock = 27000,
	.tx = {  0xbe88, /* tx cfg0 */
		  0x9800, /* tx cfg1 */
		  0x0000, /* tx cfg2 */
		},
	.cmn = { 0x0500, /* cmn cfg0*/
		  0x0005, /* cmn cfg1 */
		  0x0000, /* cmn cfg2 */
		  0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0xa0e0,	/* mpllb cfg0 */
		   0x7d80,	/* mpllb cfg1 */
		   0x0906,	/* mpllb cfg2 */
		   0xbe40,	/* mpllb cfg3 */
		   0x0000,	/* mpllb cfg4 */
		   0x0000,	/* mpllb cfg5 */
		   0x2200,	/* mpllb cfg6 */
		   0x0001,	/* mpllb cfg7 */
		   0x8000,	/* mpllb cfg8 */
		   0x0000,	/* mpllb cfg9 */
		   0x0001,	/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_hdmi_74_25 = {
	.clock = 74250,
	.tx = {  0xbe88, /* tx cfg0 */
		  0x9800, /* tx cfg1 */
		  0x0000, /* tx cfg2 */
		},
	.cmn = { 0x0500, /* cmn cfg0*/
		  0x0005, /* cmn cfg1 */
		  0x0000, /* cmn cfg2 */
		  0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x609a,	/* mpllb cfg0 */
		   0x7d40,	/* mpllb cfg1 */
		   0xca06,	/* mpllb cfg2 */
		   0xbe40,	/* mpllb cfg3 */
		   0x0000,	/* mpllb cfg4 */
		   0x0000,	/* mpllb cfg5 */
		   0x2200,	/* mpllb cfg6 */
		   0x0001,	/* mpllb cfg7 */
		   0x5800,	/* mpllb cfg8 */
		   0x0000,	/* mpllb cfg9 */
		   0x0001,	/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_hdmi_148_5 = {
	.clock = 148500,
	.tx = {  0xbe88, /* tx cfg0 */
		  0x9800, /* tx cfg1 */
		  0x0000, /* tx cfg2 */
		},
	.cmn = { 0x0500, /* cmn cfg0*/
		  0x0005, /* cmn cfg1 */
		  0x0000, /* cmn cfg2 */
		  0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x409a,	/* mpllb cfg0 */
		   0x7d20,	/* mpllb cfg1 */
		   0xca06,	/* mpllb cfg2 */
		   0xbe40,	/* mpllb cfg3 */
		   0x0000,	/* mpllb cfg4 */
		   0x0000,	/* mpllb cfg5 */
		   0x2200,	/* mpllb cfg6 */
		   0x0001,	/* mpllb cfg7 */
		   0x5800,	/* mpllb cfg8 */
		   0x0000,	/* mpllb cfg9 */
		   0x0001,	/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_hdmi_594 = {
	.clock = 594000,
	.tx = {  0xbe88, /* tx cfg0 */
		  0x9800, /* tx cfg1 */
		  0x0000, /* tx cfg2 */
		},
	.cmn = { 0x0500, /* cmn cfg0*/
		  0x0005, /* cmn cfg1 */
		  0x0000, /* cmn cfg2 */
		  0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x009a,	/* mpllb cfg0 */
		   0x7d08,	/* mpllb cfg1 */
		   0xca06,	/* mpllb cfg2 */
		   0xbe40,	/* mpllb cfg3 */
		   0x0000,	/* mpllb cfg4 */
		   0x0000,	/* mpllb cfg5 */
		   0x2200,	/* mpllb cfg6 */
		   0x0001,	/* mpllb cfg7 */
		   0x5800,	/* mpllb cfg8 */
		   0x0000,	/* mpllb cfg9 */
		   0x0001,	/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_hdmi_300 = {
	.clock = 3000000,
	.tx = {  0xbe98, /* tx cfg0 */
		  0x8800, /* tx cfg1 */
		  0x0000, /* tx cfg2 */
		},
	.cmn = { 0x0500, /* cmn cfg0*/
		  0x0005, /* cmn cfg1 */
		  0x0000, /* cmn cfg2 */
		  0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x309c,	/* mpllb cfg0 */
		   0x2110,	/* mpllb cfg1 */
		   0xca06,	/* mpllb cfg2 */
		   0xbe40,	/* mpllb cfg3 */
		   0x0000,	/* mpllb cfg4 */
		   0x0000,	/* mpllb cfg5 */
		   0x2200,	/* mpllb cfg6 */
		   0x0001,	/* mpllb cfg7 */
		   0x2000,	/* mpllb cfg8 */
		   0x0000,	/* mpllb cfg9 */
		   0x0004,	/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_hdmi_600 = {
	.clock = 6000000,
	.tx = {  0xbe98, /* tx cfg0 */
		  0x8800, /* tx cfg1 */
		  0x0000, /* tx cfg2 */
		},
	.cmn = { 0x0500, /* cmn cfg0*/
		  0x0005, /* cmn cfg1 */
		  0x0000, /* cmn cfg2 */
		  0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x109c,	/* mpllb cfg0 */
		   0x2108,	/* mpllb cfg1 */
		   0xca06,	/* mpllb cfg2 */
		   0xbe40,	/* mpllb cfg3 */
		   0x0000,	/* mpllb cfg4 */
		   0x0000,	/* mpllb cfg5 */
		   0x2200,	/* mpllb cfg6 */
		   0x0001,	/* mpllb cfg7 */
		   0x2000,	/* mpllb cfg8 */
		   0x0000,	/* mpllb cfg9 */
		   0x0004,	/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_hdmi_800 = {
	.clock = 8000000,
	.tx = {  0xbe98, /* tx cfg0 */
		  0x8800, /* tx cfg1 */
		  0x0000, /* tx cfg2 */
		},
	.cmn = { 0x0500, /* cmn cfg0*/
		  0x0005, /* cmn cfg1 */
		  0x0000, /* cmn cfg2 */
		  0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x10d0,	/* mpllb cfg0 */
		   0x2108,	/* mpllb cfg1 */
		   0x4a06,	/* mpllb cfg2 */
		   0xbe40,	/* mpllb cfg3 */
		   0x0000,	/* mpllb cfg4 */
		   0x0000,	/* mpllb cfg5 */
		   0x2200,	/* mpllb cfg6 */
		   0x0003,	/* mpllb cfg7 */
		   0x2aaa,	/* mpllb cfg8 */
		   0x0002,	/* mpllb cfg9 */
		   0x0004,	/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_hdmi_1000 = {
	.clock = 10000000,
	.tx = {  0xbe98, /* tx cfg0 */
		  0x8800, /* tx cfg1 */
		  0x0000, /* tx cfg2 */
		},
	.cmn = { 0x0500, /* cmn cfg0*/
		  0x0005, /* cmn cfg1 */
		  0x0000, /* cmn cfg2 */
		  0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x1104,	/* mpllb cfg0 */
		   0x2108,	/* mpllb cfg1 */
		   0x0a06,	/* mpllb cfg2 */
		   0xbe40,	/* mpllb cfg3 */
		   0x0000,	/* mpllb cfg4 */
		   0x0000,	/* mpllb cfg5 */
		   0x2200,	/* mpllb cfg6 */
		   0x0003,	/* mpllb cfg7 */
		   0x3555,	/* mpllb cfg8 */
		   0x0001,	/* mpllb cfg9 */
		   0x0004,	/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state mtl_c20_hdmi_1200 = {
	.clock = 12000000,
	.tx = {  0xbe98, /* tx cfg0 */
		  0x8800, /* tx cfg1 */
		  0x0000, /* tx cfg2 */
		},
	.cmn = { 0x0500, /* cmn cfg0*/
		  0x0005, /* cmn cfg1 */
		  0x0000, /* cmn cfg2 */
		  0x0000, /* cmn cfg3 */
		},
	.mpllb = { 0x1138,	/* mpllb cfg0 */
		   0x2108,	/* mpllb cfg1 */
		   0x5486,	/* mpllb cfg2 */
		   0xfe40,	/* mpllb cfg3 */
		   0x0000,	/* mpllb cfg4 */
		   0x0000,	/* mpllb cfg5 */
		   0x2200,	/* mpllb cfg6 */
		   0x0001,	/* mpllb cfg7 */
		   0x4000,	/* mpllb cfg8 */
		   0x0000,	/* mpllb cfg9 */
		   0x0004,	/* mpllb cfg10 */
		},
};

static const struct intel_c20pll_state * const mtl_c20_hdmi_tables[] = {
	&mtl_c20_hdmi_25_175,
	&mtl_c20_hdmi_27_0,
	&mtl_c20_hdmi_74_25,
	&mtl_c20_hdmi_148_5,
	&mtl_c20_hdmi_594,
	&mtl_c20_hdmi_300,
	&mtl_c20_hdmi_600,
	&mtl_c20_hdmi_800,
	&mtl_c20_hdmi_1000,
	&mtl_c20_hdmi_1200,
	NULL,
};

static int intel_c10_phy_check_hdmi_link_rate(int clock)
{
	const struct intel_c10pll_state * const *tables = mtl_c10_hdmi_tables;
	int i;

	for (i = 0; tables[i]; i++) {
		if (clock == tables[i]->clock)
			return MODE_OK;
	}

	return MODE_CLOCK_RANGE;
}

static const struct intel_c10pll_state * const *
intel_c10pll_tables_get(struct intel_crtc_state *crtc_state,
			struct intel_encoder *encoder)
{
	if (intel_crtc_has_dp_encoder(crtc_state)) {
		if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
			return mtl_c10_edp_tables;
		else
			return mtl_c10_dp_tables;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		return mtl_c10_hdmi_tables;
	}

	MISSING_CASE(encoder->type);
	return NULL;
}

static void intel_c10pll_update_pll(struct intel_crtc_state *crtc_state,
				    struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_cx0pll_state *pll_state = &crtc_state->dpll_hw_state.cx0pll;
	int i;

	if (intel_crtc_has_dp_encoder(crtc_state)) {
		if (intel_panel_use_ssc(i915)) {
			struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

			pll_state->ssc_enabled =
				(intel_dp->dpcd[DP_MAX_DOWNSPREAD] & DP_MAX_DOWNSPREAD_0_5);
		}
	}

	if (pll_state->ssc_enabled)
		return;

	drm_WARN_ON(&i915->drm, ARRAY_SIZE(pll_state->c10.pll) < 9);
	for (i = 4; i < 9; i++)
		pll_state->c10.pll[i] = 0;
}

static int intel_c10pll_calc_state(struct intel_crtc_state *crtc_state,
				   struct intel_encoder *encoder)
{
	const struct intel_c10pll_state * const *tables;
	int i;

	tables = intel_c10pll_tables_get(crtc_state, encoder);
	if (!tables)
		return -EINVAL;

	for (i = 0; tables[i]; i++) {
		if (crtc_state->port_clock == tables[i]->clock) {
			crtc_state->dpll_hw_state.cx0pll.c10 = *tables[i];
			intel_c10pll_update_pll(crtc_state, encoder);

			return 0;
		}
	}

	return -EINVAL;
}

static void intel_c10pll_readout_hw_state(struct intel_encoder *encoder,
					  struct intel_c10pll_state *pll_state)
{
	u8 lane = INTEL_CX0_LANE0;
	intel_wakeref_t wakeref;
	int i;

	wakeref = intel_cx0_phy_transaction_begin(encoder);

	/*
	 * According to C10 VDR Register programming Sequence we need
	 * to do this to read PHY internal registers from MsgBus.
	 */
	intel_cx0_rmw(encoder, lane, PHY_C10_VDR_CONTROL(1),
		      0, C10_VDR_CTRL_MSGBUS_ACCESS,
		      MB_WRITE_COMMITTED);

	for (i = 0; i < ARRAY_SIZE(pll_state->pll); i++)
		pll_state->pll[i] = intel_cx0_read(encoder, lane, PHY_C10_VDR_PLL(i));

	pll_state->cmn = intel_cx0_read(encoder, lane, PHY_C10_VDR_CMN(0));
	pll_state->tx = intel_cx0_read(encoder, lane, PHY_C10_VDR_TX(0));

	intel_cx0_phy_transaction_end(encoder, wakeref);
}

static void intel_c10_pll_program(struct drm_i915_private *i915,
				  const struct intel_crtc_state *crtc_state,
				  struct intel_encoder *encoder)
{
	const struct intel_c10pll_state *pll_state = &crtc_state->dpll_hw_state.cx0pll.c10;
	int i;

	intel_cx0_rmw(encoder, INTEL_CX0_BOTH_LANES, PHY_C10_VDR_CONTROL(1),
		      0, C10_VDR_CTRL_MSGBUS_ACCESS,
		      MB_WRITE_COMMITTED);

	/* Custom width needs to be programmed to 0 for both the phy lanes */
	intel_cx0_rmw(encoder, INTEL_CX0_BOTH_LANES, PHY_C10_VDR_CUSTOM_WIDTH,
		      C10_VDR_CUSTOM_WIDTH_MASK, C10_VDR_CUSTOM_WIDTH_8_10,
		      MB_WRITE_COMMITTED);
	intel_cx0_rmw(encoder, INTEL_CX0_BOTH_LANES, PHY_C10_VDR_CONTROL(1),
		      0, C10_VDR_CTRL_UPDATE_CFG,
		      MB_WRITE_COMMITTED);

	/* Program the pll values only for the master lane */
	for (i = 0; i < ARRAY_SIZE(pll_state->pll); i++)
		intel_cx0_write(encoder, INTEL_CX0_LANE0, PHY_C10_VDR_PLL(i),
				pll_state->pll[i],
				(i % 4) ? MB_WRITE_UNCOMMITTED : MB_WRITE_COMMITTED);

	intel_cx0_write(encoder, INTEL_CX0_LANE0, PHY_C10_VDR_CMN(0), pll_state->cmn, MB_WRITE_COMMITTED);
	intel_cx0_write(encoder, INTEL_CX0_LANE0, PHY_C10_VDR_TX(0), pll_state->tx, MB_WRITE_COMMITTED);

	intel_cx0_rmw(encoder, INTEL_CX0_LANE0, PHY_C10_VDR_CONTROL(1),
		      0, C10_VDR_CTRL_MASTER_LANE | C10_VDR_CTRL_UPDATE_CFG,
		      MB_WRITE_COMMITTED);
}

void intel_c10pll_dump_hw_state(struct drm_i915_private *i915,
				const struct intel_c10pll_state *hw_state)
{
	bool fracen;
	int i;
	unsigned int frac_quot = 0, frac_rem = 0, frac_den = 1;
	unsigned int multiplier, tx_clk_div;

	fracen = hw_state->pll[0] & C10_PLL0_FRACEN;
	drm_dbg_kms(&i915->drm, "c10pll_hw_state: fracen: %s, ",
		    str_yes_no(fracen));

	if (fracen) {
		frac_quot = hw_state->pll[12] << 8 | hw_state->pll[11];
		frac_rem =  hw_state->pll[14] << 8 | hw_state->pll[13];
		frac_den =  hw_state->pll[10] << 8 | hw_state->pll[9];
		drm_dbg_kms(&i915->drm, "quot: %u, rem: %u, den: %u,\n",
			    frac_quot, frac_rem, frac_den);
	}

	multiplier = (REG_FIELD_GET8(C10_PLL3_MULTIPLIERH_MASK, hw_state->pll[3]) << 8 |
		      hw_state->pll[2]) / 2 + 16;
	tx_clk_div = REG_FIELD_GET8(C10_PLL15_TXCLKDIV_MASK, hw_state->pll[15]);
	drm_dbg_kms(&i915->drm,
		    "multiplier: %u, tx_clk_div: %u.\n", multiplier, tx_clk_div);

	drm_dbg_kms(&i915->drm, "c10pll_rawhw_state:");
	drm_dbg_kms(&i915->drm, "tx: 0x%x, cmn: 0x%x\n", hw_state->tx, hw_state->cmn);

	BUILD_BUG_ON(ARRAY_SIZE(hw_state->pll) % 4);
	for (i = 0; i < ARRAY_SIZE(hw_state->pll); i = i + 4)
		drm_dbg_kms(&i915->drm, "pll[%d] = 0x%x, pll[%d] = 0x%x, pll[%d] = 0x%x, pll[%d] = 0x%x\n",
			    i, hw_state->pll[i], i + 1, hw_state->pll[i + 1],
			    i + 2, hw_state->pll[i + 2], i + 3, hw_state->pll[i + 3]);
}

static int intel_c20_compute_hdmi_tmds_pll(u64 pixel_clock, struct intel_c20pll_state *pll_state)
{
	u64 datarate;
	u64 mpll_tx_clk_div;
	u64 vco_freq_shift;
	u64 vco_freq;
	u64 multiplier;
	u64 mpll_multiplier;
	u64 mpll_fracn_quot;
	u64 mpll_fracn_rem;
	u8  mpllb_ana_freq_vco;
	u8  mpll_div_multiplier;

	if (pixel_clock < 25175 || pixel_clock > 600000)
		return -EINVAL;

	datarate = ((u64)pixel_clock * 1000) * 10;
	mpll_tx_clk_div = ilog2(div64_u64((u64)CLOCK_9999MHZ, (u64)datarate));
	vco_freq_shift = ilog2(div64_u64((u64)CLOCK_4999MHZ * (u64)256, (u64)datarate));
	vco_freq = (datarate << vco_freq_shift) >> 8;
	multiplier = div64_u64((vco_freq << 28), (REFCLK_38_4_MHZ >> 4));
	mpll_multiplier = 2 * (multiplier >> 32);

	mpll_fracn_quot = (multiplier >> 16) & 0xFFFF;
	mpll_fracn_rem  = multiplier & 0xFFFF;

	mpll_div_multiplier = min_t(u8, div64_u64((vco_freq * 16 + (datarate >> 1)),
						  datarate), 255);

	if (vco_freq <= DATARATE_3000000000)
		mpllb_ana_freq_vco = MPLLB_ANA_FREQ_VCO_3;
	else if (vco_freq <= DATARATE_3500000000)
		mpllb_ana_freq_vco = MPLLB_ANA_FREQ_VCO_2;
	else if (vco_freq <= DATARATE_4000000000)
		mpllb_ana_freq_vco = MPLLB_ANA_FREQ_VCO_1;
	else
		mpllb_ana_freq_vco = MPLLB_ANA_FREQ_VCO_0;

	pll_state->clock	= pixel_clock;
	pll_state->tx[0]	= 0xbe88;
	pll_state->tx[1]	= 0x9800;
	pll_state->tx[2]	= 0x0000;
	pll_state->cmn[0]	= 0x0500;
	pll_state->cmn[1]	= 0x0005;
	pll_state->cmn[2]	= 0x0000;
	pll_state->cmn[3]	= 0x0000;
	pll_state->mpllb[0]	= (MPLL_TX_CLK_DIV(mpll_tx_clk_div) |
				   MPLL_MULTIPLIER(mpll_multiplier));
	pll_state->mpllb[1]	= (CAL_DAC_CODE(CAL_DAC_CODE_31) |
				   WORD_CLK_DIV |
				   MPLL_DIV_MULTIPLIER(mpll_div_multiplier));
	pll_state->mpllb[2]	= (MPLLB_ANA_FREQ_VCO(mpllb_ana_freq_vco) |
				   CP_PROP(CP_PROP_20) |
				   CP_INT(CP_INT_6));
	pll_state->mpllb[3]	= (V2I(V2I_2) |
				   CP_PROP_GS(CP_PROP_GS_30) |
				   CP_INT_GS(CP_INT_GS_28));
	pll_state->mpllb[4]	= 0x0000;
	pll_state->mpllb[5]	= 0x0000;
	pll_state->mpllb[6]	= (C20_MPLLB_FRACEN | SSC_UP_SPREAD);
	pll_state->mpllb[7]	= MPLL_FRACN_DEN;
	pll_state->mpllb[8]	= mpll_fracn_quot;
	pll_state->mpllb[9]	= mpll_fracn_rem;
	pll_state->mpllb[10]	= HDMI_DIV(HDMI_DIV_1);

	return 0;
}

static int intel_c20_phy_check_hdmi_link_rate(int clock)
{
	const struct intel_c20pll_state * const *tables = mtl_c20_hdmi_tables;
	int i;

	for (i = 0; tables[i]; i++) {
		if (clock == tables[i]->clock)
			return MODE_OK;
	}

	if (clock >= 25175 && clock <= 594000)
		return MODE_OK;

	return MODE_CLOCK_RANGE;
}

int intel_cx0_phy_check_hdmi_link_rate(struct intel_hdmi *hdmi, int clock)
{
	struct intel_digital_port *dig_port = hdmi_to_dig_port(hdmi);

	if (intel_encoder_is_c10phy(&dig_port->base))
		return intel_c10_phy_check_hdmi_link_rate(clock);
	return intel_c20_phy_check_hdmi_link_rate(clock);
}

static const struct intel_c20pll_state * const *
intel_c20_pll_tables_get(struct intel_crtc_state *crtc_state,
			 struct intel_encoder *encoder)
{
	if (intel_crtc_has_dp_encoder(crtc_state))
		return mtl_c20_dp_tables;
	else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return mtl_c20_hdmi_tables;

	MISSING_CASE(encoder->type);
	return NULL;
}

static int intel_c20pll_calc_state(struct intel_crtc_state *crtc_state,
				   struct intel_encoder *encoder)
{
	const struct intel_c20pll_state * const *tables;
	int i;

	/* try computed C20 HDMI tables before using consolidated tables */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		if (intel_c20_compute_hdmi_tmds_pll(crtc_state->port_clock,
						    &crtc_state->dpll_hw_state.cx0pll.c20) == 0)
			return 0;
	}

	tables = intel_c20_pll_tables_get(crtc_state, encoder);
	if (!tables)
		return -EINVAL;

	for (i = 0; tables[i]; i++) {
		if (crtc_state->port_clock == tables[i]->clock) {
			crtc_state->dpll_hw_state.cx0pll.c20 = *tables[i];
			return 0;
		}
	}

	return -EINVAL;
}

int intel_cx0pll_calc_state(struct intel_crtc_state *crtc_state,
			    struct intel_encoder *encoder)
{
	if (intel_encoder_is_c10phy(encoder))
		return intel_c10pll_calc_state(crtc_state, encoder);
	return intel_c20pll_calc_state(crtc_state, encoder);
}

static bool intel_c20phy_use_mpllb(const struct intel_c20pll_state *state)
{
	return state->tx[0] & C20_PHY_USE_MPLLB;
}

static int intel_c20pll_calc_port_clock(struct intel_encoder *encoder,
					const struct intel_c20pll_state *pll_state)
{
	unsigned int frac, frac_en, frac_quot, frac_rem, frac_den;
	unsigned int multiplier, refclk = 38400;
	unsigned int tx_clk_div;
	unsigned int ref_clk_mpllb_div;
	unsigned int fb_clk_div4_en;
	unsigned int ref, vco;
	unsigned int tx_rate_mult;
	unsigned int tx_rate = REG_FIELD_GET(C20_PHY_TX_RATE, pll_state->tx[0]);

	if (intel_c20phy_use_mpllb(pll_state)) {
		tx_rate_mult = 1;
		frac_en = REG_FIELD_GET(C20_MPLLB_FRACEN, pll_state->mpllb[6]);
		frac_quot = pll_state->mpllb[8];
		frac_rem =  pll_state->mpllb[9];
		frac_den =  pll_state->mpllb[7];
		multiplier = REG_FIELD_GET(C20_MULTIPLIER_MASK, pll_state->mpllb[0]);
		tx_clk_div = REG_FIELD_GET(C20_MPLLB_TX_CLK_DIV_MASK, pll_state->mpllb[0]);
		ref_clk_mpllb_div = REG_FIELD_GET(C20_REF_CLK_MPLLB_DIV_MASK, pll_state->mpllb[6]);
		fb_clk_div4_en = 0;
	} else {
		tx_rate_mult = 2;
		frac_en = REG_FIELD_GET(C20_MPLLA_FRACEN, pll_state->mplla[6]);
		frac_quot = pll_state->mplla[8];
		frac_rem =  pll_state->mplla[9];
		frac_den =  pll_state->mplla[7];
		multiplier = REG_FIELD_GET(C20_MULTIPLIER_MASK, pll_state->mplla[0]);
		tx_clk_div = REG_FIELD_GET(C20_MPLLA_TX_CLK_DIV_MASK, pll_state->mplla[1]);
		ref_clk_mpllb_div = REG_FIELD_GET(C20_REF_CLK_MPLLB_DIV_MASK, pll_state->mplla[6]);
		fb_clk_div4_en = REG_FIELD_GET(C20_FB_CLK_DIV4_EN, pll_state->mplla[0]);
	}

	if (frac_en)
		frac = frac_quot + DIV_ROUND_CLOSEST(frac_rem, frac_den);
	else
		frac = 0;

	ref = DIV_ROUND_CLOSEST(refclk * (1 << (1 + fb_clk_div4_en)), 1 << ref_clk_mpllb_div);
	vco = DIV_ROUND_CLOSEST_ULL(mul_u32_u32(ref, (multiplier << (17 - 2)) + frac) >> 17, 10);

	return vco << tx_rate_mult >> tx_clk_div >> tx_rate;
}

static void intel_c20pll_readout_hw_state(struct intel_encoder *encoder,
					  struct intel_c20pll_state *pll_state)
{
	bool cntx;
	intel_wakeref_t wakeref;
	int i;

	wakeref = intel_cx0_phy_transaction_begin(encoder);

	/* 1. Read current context selection */
	cntx = intel_cx0_read(encoder, INTEL_CX0_LANE0, PHY_C20_VDR_CUSTOM_SERDES_RATE) & PHY_C20_CONTEXT_TOGGLE;

	/* Read Tx configuration */
	for (i = 0; i < ARRAY_SIZE(pll_state->tx); i++) {
		if (cntx)
			pll_state->tx[i] = intel_c20_sram_read(encoder, INTEL_CX0_LANE0,
							       PHY_C20_B_TX_CNTX_CFG(i));
		else
			pll_state->tx[i] = intel_c20_sram_read(encoder, INTEL_CX0_LANE0,
							       PHY_C20_A_TX_CNTX_CFG(i));
	}

	/* Read common configuration */
	for (i = 0; i < ARRAY_SIZE(pll_state->cmn); i++) {
		if (cntx)
			pll_state->cmn[i] = intel_c20_sram_read(encoder, INTEL_CX0_LANE0,
								PHY_C20_B_CMN_CNTX_CFG(i));
		else
			pll_state->cmn[i] = intel_c20_sram_read(encoder, INTEL_CX0_LANE0,
								PHY_C20_A_CMN_CNTX_CFG(i));
	}

	if (intel_c20phy_use_mpllb(pll_state)) {
		/* MPLLB configuration */
		for (i = 0; i < ARRAY_SIZE(pll_state->mpllb); i++) {
			if (cntx)
				pll_state->mpllb[i] = intel_c20_sram_read(encoder, INTEL_CX0_LANE0,
									  PHY_C20_B_MPLLB_CNTX_CFG(i));
			else
				pll_state->mpllb[i] = intel_c20_sram_read(encoder, INTEL_CX0_LANE0,
									  PHY_C20_A_MPLLB_CNTX_CFG(i));
		}
	} else {
		/* MPLLA configuration */
		for (i = 0; i < ARRAY_SIZE(pll_state->mplla); i++) {
			if (cntx)
				pll_state->mplla[i] = intel_c20_sram_read(encoder, INTEL_CX0_LANE0,
									  PHY_C20_B_MPLLA_CNTX_CFG(i));
			else
				pll_state->mplla[i] = intel_c20_sram_read(encoder, INTEL_CX0_LANE0,
									  PHY_C20_A_MPLLA_CNTX_CFG(i));
		}
	}

	pll_state->clock = intel_c20pll_calc_port_clock(encoder, pll_state);

	intel_cx0_phy_transaction_end(encoder, wakeref);
}

void intel_c20pll_dump_hw_state(struct drm_i915_private *i915,
				const struct intel_c20pll_state *hw_state)
{
	int i;

	drm_dbg_kms(&i915->drm, "c20pll_hw_state:\n");
	drm_dbg_kms(&i915->drm, "tx[0] = 0x%.4x, tx[1] = 0x%.4x, tx[2] = 0x%.4x\n",
		    hw_state->tx[0], hw_state->tx[1], hw_state->tx[2]);
	drm_dbg_kms(&i915->drm, "cmn[0] = 0x%.4x, cmn[1] = 0x%.4x, cmn[2] = 0x%.4x, cmn[3] = 0x%.4x\n",
		    hw_state->cmn[0], hw_state->cmn[1], hw_state->cmn[2], hw_state->cmn[3]);

	if (intel_c20phy_use_mpllb(hw_state)) {
		for (i = 0; i < ARRAY_SIZE(hw_state->mpllb); i++)
			drm_dbg_kms(&i915->drm, "mpllb[%d] = 0x%.4x\n", i, hw_state->mpllb[i]);
	} else {
		for (i = 0; i < ARRAY_SIZE(hw_state->mplla); i++)
			drm_dbg_kms(&i915->drm, "mplla[%d] = 0x%.4x\n", i, hw_state->mplla[i]);
	}
}

static u8 intel_c20_get_dp_rate(u32 clock)
{
	switch (clock) {
	case 162000: /* 1.62 Gbps DP1.4 */
		return 0;
	case 270000: /* 2.7 Gbps DP1.4 */
		return 1;
	case 540000: /* 5.4 Gbps DP 1.4 */
		return 2;
	case 810000: /* 8.1 Gbps DP1.4 */
		return 3;
	case 216000: /* 2.16 Gbps eDP */
		return 4;
	case 243000: /* 2.43 Gbps eDP */
		return 5;
	case 324000: /* 3.24 Gbps eDP */
		return 6;
	case 432000: /* 4.32 Gbps eDP */
		return 7;
	case 1000000: /* 10 Gbps DP2.0 */
		return 8;
	case 1350000: /* 13.5 Gbps DP2.0 */
		return 9;
	case 2000000: /* 20 Gbps DP2.0 */
		return 10;
	case 648000: /* 6.48 Gbps eDP*/
		return 11;
	case 675000: /* 6.75 Gbps eDP*/
		return 12;
	default:
		MISSING_CASE(clock);
		return 0;
	}
}

static u8 intel_c20_get_hdmi_rate(u32 clock)
{
	if (clock >= 25175 && clock <= 600000)
		return 0;

	switch (clock) {
	case 300000: /* 3 Gbps */
	case 600000: /* 6 Gbps */
	case 1200000: /* 12 Gbps */
		return 1;
	case 800000: /* 8 Gbps */
		return 2;
	case 1000000: /* 10 Gbps */
		return 3;
	default:
		MISSING_CASE(clock);
		return 0;
	}
}

static bool is_dp2(u32 clock)
{
	/* DP2.0 clock rates */
	if (clock == 1000000 || clock == 1350000 || clock  == 2000000)
		return true;

	return false;
}

static bool is_hdmi_frl(u32 clock)
{
	switch (clock) {
	case 300000: /* 3 Gbps */
	case 600000: /* 6 Gbps */
	case 800000: /* 8 Gbps */
	case 1000000: /* 10 Gbps */
	case 1200000: /* 12 Gbps */
		return true;
	default:
		return false;
	}
}

static bool intel_c20_protocol_switch_valid(struct intel_encoder *encoder)
{
	struct intel_digital_port *intel_dig_port = enc_to_dig_port(encoder);

	/* banks should not be cleared for DPALT/USB4/TBT modes */
	/* TODO: optimize re-calibration in legacy mode */
	return intel_tc_port_in_legacy_mode(intel_dig_port);
}

static int intel_get_c20_custom_width(u32 clock, bool dp)
{
	if (dp && is_dp2(clock))
		return 2;
	else if (is_hdmi_frl(clock))
		return 1;
	else
		return 0;
}

static void intel_c20_pll_program(struct drm_i915_private *i915,
				  const struct intel_crtc_state *crtc_state,
				  struct intel_encoder *encoder)
{
	const struct intel_c20pll_state *pll_state = &crtc_state->dpll_hw_state.cx0pll.c20;
	bool dp = false;
	int lane = crtc_state->lane_count > 2 ? INTEL_CX0_BOTH_LANES : INTEL_CX0_LANE0;
	u32 clock = crtc_state->port_clock;
	bool cntx;
	int i;

	if (intel_crtc_has_dp_encoder(crtc_state))
		dp = true;

	/* 1. Read current context selection */
	cntx = intel_cx0_read(encoder, INTEL_CX0_LANE0, PHY_C20_VDR_CUSTOM_SERDES_RATE) & BIT(0);

	/*
	 * 2. If there is a protocol switch from HDMI to DP or vice versa, clear
	 * the lane #0 MPLLB CAL_DONE_BANK DP2.0 10G and 20G rates enable MPLLA.
	 * Protocol switch is only applicable for MPLLA
	 */
	if (intel_c20_protocol_switch_valid(encoder)) {
		for (i = 0; i < 4; i++)
			intel_c20_sram_write(encoder, INTEL_CX0_LANE0, RAWLANEAONX_DIG_TX_MPLLB_CAL_DONE_BANK(i), 0);
		usleep_range(4000, 4100);
	}

	/* 3. Write SRAM configuration context. If A in use, write configuration to B context */
	/* 3.1 Tx configuration */
	for (i = 0; i < ARRAY_SIZE(pll_state->tx); i++) {
		if (cntx)
			intel_c20_sram_write(encoder, INTEL_CX0_LANE0, PHY_C20_A_TX_CNTX_CFG(i), pll_state->tx[i]);
		else
			intel_c20_sram_write(encoder, INTEL_CX0_LANE0, PHY_C20_B_TX_CNTX_CFG(i), pll_state->tx[i]);
	}

	/* 3.2 common configuration */
	for (i = 0; i < ARRAY_SIZE(pll_state->cmn); i++) {
		if (cntx)
			intel_c20_sram_write(encoder, INTEL_CX0_LANE0, PHY_C20_A_CMN_CNTX_CFG(i), pll_state->cmn[i]);
		else
			intel_c20_sram_write(encoder, INTEL_CX0_LANE0, PHY_C20_B_CMN_CNTX_CFG(i), pll_state->cmn[i]);
	}

	/* 3.3 mpllb or mplla configuration */
	if (intel_c20phy_use_mpllb(pll_state)) {
		for (i = 0; i < ARRAY_SIZE(pll_state->mpllb); i++) {
			if (cntx)
				intel_c20_sram_write(encoder, INTEL_CX0_LANE0,
						     PHY_C20_A_MPLLB_CNTX_CFG(i),
						     pll_state->mpllb[i]);
			else
				intel_c20_sram_write(encoder, INTEL_CX0_LANE0,
						     PHY_C20_B_MPLLB_CNTX_CFG(i),
						     pll_state->mpllb[i]);
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(pll_state->mplla); i++) {
			if (cntx)
				intel_c20_sram_write(encoder, INTEL_CX0_LANE0,
						     PHY_C20_A_MPLLA_CNTX_CFG(i),
						     pll_state->mplla[i]);
			else
				intel_c20_sram_write(encoder, INTEL_CX0_LANE0,
						     PHY_C20_B_MPLLA_CNTX_CFG(i),
						     pll_state->mplla[i]);
		}
	}

	/* 4. Program custom width to match the link protocol */
	intel_cx0_rmw(encoder, lane, PHY_C20_VDR_CUSTOM_WIDTH,
		      PHY_C20_CUSTOM_WIDTH_MASK,
		      PHY_C20_CUSTOM_WIDTH(intel_get_c20_custom_width(clock, dp)),
		      MB_WRITE_COMMITTED);

	/* 5. For DP or 6. For HDMI */
	if (dp) {
		intel_cx0_rmw(encoder, lane, PHY_C20_VDR_CUSTOM_SERDES_RATE,
			      BIT(6) | PHY_C20_CUSTOM_SERDES_MASK,
			      BIT(6) | PHY_C20_CUSTOM_SERDES(intel_c20_get_dp_rate(clock)),
			      MB_WRITE_COMMITTED);
	} else {
		intel_cx0_rmw(encoder, lane, PHY_C20_VDR_CUSTOM_SERDES_RATE,
			      BIT(7) | PHY_C20_CUSTOM_SERDES_MASK,
			      is_hdmi_frl(clock) ? BIT(7) : 0,
			      MB_WRITE_COMMITTED);

		intel_cx0_write(encoder, INTEL_CX0_BOTH_LANES, PHY_C20_VDR_HDMI_RATE,
				intel_c20_get_hdmi_rate(clock),
				MB_WRITE_COMMITTED);
	}

	/*
	 * 7. Write Vendor specific registers to toggle context setting to load
	 * the updated programming toggle context bit
	 */
	intel_cx0_rmw(encoder, lane, PHY_C20_VDR_CUSTOM_SERDES_RATE,
		      BIT(0), cntx ? 0 : 1, MB_WRITE_COMMITTED);
}

static int intel_c10pll_calc_port_clock(struct intel_encoder *encoder,
					const struct intel_c10pll_state *pll_state)
{
	unsigned int frac_quot = 0, frac_rem = 0, frac_den = 1;
	unsigned int multiplier, tx_clk_div, hdmi_div, refclk = 38400;
	int tmpclk = 0;

	if (pll_state->pll[0] & C10_PLL0_FRACEN) {
		frac_quot = pll_state->pll[12] << 8 | pll_state->pll[11];
		frac_rem =  pll_state->pll[14] << 8 | pll_state->pll[13];
		frac_den =  pll_state->pll[10] << 8 | pll_state->pll[9];
	}

	multiplier = (REG_FIELD_GET8(C10_PLL3_MULTIPLIERH_MASK, pll_state->pll[3]) << 8 |
		      pll_state->pll[2]) / 2 + 16;

	tx_clk_div = REG_FIELD_GET8(C10_PLL15_TXCLKDIV_MASK, pll_state->pll[15]);
	hdmi_div = REG_FIELD_GET8(C10_PLL15_HDMIDIV_MASK, pll_state->pll[15]);

	tmpclk = DIV_ROUND_CLOSEST_ULL(mul_u32_u32(refclk, (multiplier << 16) + frac_quot) +
				     DIV_ROUND_CLOSEST(refclk * frac_rem, frac_den),
				     10 << (tx_clk_div + 16));
	tmpclk *= (hdmi_div ? 2 : 1);

	return tmpclk;
}

static void intel_program_port_clock_ctl(struct intel_encoder *encoder,
					 const struct intel_crtc_state *crtc_state,
					 bool lane_reversal)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	u32 val = 0;

	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL1(i915, encoder->port),
		     XELPDP_PORT_REVERSAL,
		     lane_reversal ? XELPDP_PORT_REVERSAL : 0);

	if (lane_reversal)
		val |= XELPDP_LANE1_PHY_CLOCK_SELECT;

	val |= XELPDP_FORWARD_CLOCK_UNGATE;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI) &&
	    is_hdmi_frl(crtc_state->port_clock))
		val |= XELPDP_DDI_CLOCK_SELECT(XELPDP_DDI_CLOCK_SELECT_DIV18CLK);
	else
		val |= XELPDP_DDI_CLOCK_SELECT(XELPDP_DDI_CLOCK_SELECT_MAXPCLK);

	/* TODO: HDMI FRL */
	/* DP2.0 10G and 20G rates enable MPLLA*/
	if (crtc_state->port_clock == 1000000 || crtc_state->port_clock == 2000000)
		val |= crtc_state->dpll_hw_state.cx0pll.ssc_enabled ? XELPDP_SSC_ENABLE_PLLA : 0;
	else
		val |= crtc_state->dpll_hw_state.cx0pll.ssc_enabled ? XELPDP_SSC_ENABLE_PLLB : 0;

	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
		     XELPDP_LANE1_PHY_CLOCK_SELECT | XELPDP_FORWARD_CLOCK_UNGATE |
		     XELPDP_DDI_CLOCK_SELECT_MASK | XELPDP_SSC_ENABLE_PLLA |
		     XELPDP_SSC_ENABLE_PLLB, val);
}

static u32 intel_cx0_get_powerdown_update(u8 lane_mask)
{
	u32 val = 0;
	int lane = 0;

	for_each_cx0_lane_in_mask(lane_mask, lane)
		val |= XELPDP_LANE_POWERDOWN_UPDATE(lane);

	return val;
}

static u32 intel_cx0_get_powerdown_state(u8 lane_mask, u8 state)
{
	u32 val = 0;
	int lane = 0;

	for_each_cx0_lane_in_mask(lane_mask, lane)
		val |= XELPDP_LANE_POWERDOWN_NEW_STATE(lane, state);

	return val;
}

static void intel_cx0_powerdown_change_sequence(struct intel_encoder *encoder,
						u8 lane_mask, u8 state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum phy phy = intel_encoder_to_phy(encoder);
	i915_reg_t buf_ctl2_reg = XELPDP_PORT_BUF_CTL2(i915, port);
	int lane;

	intel_de_rmw(i915, buf_ctl2_reg,
		     intel_cx0_get_powerdown_state(INTEL_CX0_BOTH_LANES, XELPDP_LANE_POWERDOWN_NEW_STATE_MASK),
		     intel_cx0_get_powerdown_state(lane_mask, state));

	/* Wait for pending transactions.*/
	for_each_cx0_lane_in_mask(lane_mask, lane)
		if (intel_de_wait_for_clear(i915, XELPDP_PORT_M2P_MSGBUS_CTL(i915, port, lane),
					    XELPDP_PORT_M2P_TRANSACTION_PENDING,
					    XELPDP_MSGBUS_TIMEOUT_SLOW)) {
			drm_dbg_kms(&i915->drm,
				    "PHY %c Timeout waiting for previous transaction to complete. Reset the bus.\n",
				    phy_name(phy));
			intel_cx0_bus_reset(encoder, lane);
		}

	intel_de_rmw(i915, buf_ctl2_reg,
		     intel_cx0_get_powerdown_update(INTEL_CX0_BOTH_LANES),
		     intel_cx0_get_powerdown_update(lane_mask));

	/* Update Timeout Value */
	if (intel_de_wait_custom(i915, buf_ctl2_reg,
				 intel_cx0_get_powerdown_update(lane_mask), 0,
				 XELPDP_PORT_POWERDOWN_UPDATE_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "PHY %c failed to bring out of Lane reset after %dus.\n",
			 phy_name(phy), XELPDP_PORT_RESET_START_TIMEOUT_US);
}

static void intel_cx0_setup_powerdown(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;

	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL2(i915, port),
		     XELPDP_POWER_STATE_READY_MASK,
		     XELPDP_POWER_STATE_READY(CX0_P2_STATE_READY));
	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL3(i915, port),
		     XELPDP_POWER_STATE_ACTIVE_MASK |
		     XELPDP_PLL_LANE_STAGGERING_DELAY_MASK,
		     XELPDP_POWER_STATE_ACTIVE(CX0_P0_STATE_ACTIVE) |
		     XELPDP_PLL_LANE_STAGGERING_DELAY(0));
}

static u32 intel_cx0_get_pclk_refclk_request(u8 lane_mask)
{
	u32 val = 0;
	int lane = 0;

	for_each_cx0_lane_in_mask(lane_mask, lane)
		val |= XELPDP_LANE_PCLK_REFCLK_REQUEST(lane);

	return val;
}

static u32 intel_cx0_get_pclk_refclk_ack(u8 lane_mask)
{
	u32 val = 0;
	int lane = 0;

	for_each_cx0_lane_in_mask(lane_mask, lane)
		val |= XELPDP_LANE_PCLK_REFCLK_ACK(lane);

	return val;
}

static void intel_cx0_phy_lane_reset(struct intel_encoder *encoder,
				     bool lane_reversal)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum phy phy = intel_encoder_to_phy(encoder);
	u8 owned_lane_mask = intel_cx0_get_owned_lane_mask(encoder);
	u8 lane_mask = lane_reversal ? INTEL_CX0_LANE1 : INTEL_CX0_LANE0;
	u32 lane_pipe_reset = owned_lane_mask == INTEL_CX0_BOTH_LANES
				? XELPDP_LANE_PIPE_RESET(0) | XELPDP_LANE_PIPE_RESET(1)
				: XELPDP_LANE_PIPE_RESET(0);
	u32 lane_phy_current_status = owned_lane_mask == INTEL_CX0_BOTH_LANES
					? (XELPDP_LANE_PHY_CURRENT_STATUS(0) |
					   XELPDP_LANE_PHY_CURRENT_STATUS(1))
					: XELPDP_LANE_PHY_CURRENT_STATUS(0);

	if (intel_de_wait_custom(i915, XELPDP_PORT_BUF_CTL1(i915, port),
				 XELPDP_PORT_BUF_SOC_PHY_READY,
				 XELPDP_PORT_BUF_SOC_PHY_READY,
				 XELPDP_PORT_BUF_SOC_READY_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "PHY %c failed to bring out of SOC reset after %dus.\n",
			 phy_name(phy), XELPDP_PORT_BUF_SOC_READY_TIMEOUT_US);

	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL2(i915, port), lane_pipe_reset,
		     lane_pipe_reset);

	if (intel_de_wait_custom(i915, XELPDP_PORT_BUF_CTL2(i915, port),
				 lane_phy_current_status, lane_phy_current_status,
				 XELPDP_PORT_RESET_START_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "PHY %c failed to bring out of Lane reset after %dus.\n",
			 phy_name(phy), XELPDP_PORT_RESET_START_TIMEOUT_US);

	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(i915, port),
		     intel_cx0_get_pclk_refclk_request(owned_lane_mask),
		     intel_cx0_get_pclk_refclk_request(lane_mask));

	if (intel_de_wait_custom(i915, XELPDP_PORT_CLOCK_CTL(i915, port),
				 intel_cx0_get_pclk_refclk_ack(owned_lane_mask),
				 intel_cx0_get_pclk_refclk_ack(lane_mask),
				 XELPDP_REFCLK_ENABLE_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "PHY %c failed to request refclk after %dus.\n",
			 phy_name(phy), XELPDP_REFCLK_ENABLE_TIMEOUT_US);

	intel_cx0_powerdown_change_sequence(encoder, INTEL_CX0_BOTH_LANES,
					    CX0_P2_STATE_RESET);
	intel_cx0_setup_powerdown(encoder);

	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL2(i915, port), lane_pipe_reset, 0);

	if (intel_de_wait_for_clear(i915, XELPDP_PORT_BUF_CTL2(i915, port),
				    lane_phy_current_status,
				    XELPDP_PORT_RESET_END_TIMEOUT))
		drm_warn(&i915->drm, "PHY %c failed to bring out of Lane reset after %dms.\n",
			 phy_name(phy), XELPDP_PORT_RESET_END_TIMEOUT);
}

static void intel_cx0_program_phy_lane(struct drm_i915_private *i915,
				       struct intel_encoder *encoder, int lane_count,
				       bool lane_reversal)
{
	int i;
	u8 disables;
	bool dp_alt_mode = intel_tc_port_in_dp_alt_mode(enc_to_dig_port(encoder));
	u8 owned_lane_mask = intel_cx0_get_owned_lane_mask(encoder);

	if (intel_encoder_is_c10phy(encoder))
		intel_cx0_rmw(encoder, owned_lane_mask,
			      PHY_C10_VDR_CONTROL(1), 0,
			      C10_VDR_CTRL_MSGBUS_ACCESS,
			      MB_WRITE_COMMITTED);

	if (lane_reversal)
		disables = REG_GENMASK8(3, 0) >> lane_count;
	else
		disables = REG_GENMASK8(3, 0) << lane_count;

	if (dp_alt_mode && lane_count == 1) {
		disables &= ~REG_GENMASK8(1, 0);
		disables |= REG_FIELD_PREP8(REG_GENMASK8(1, 0), 0x1);
	}

	for (i = 0; i < 4; i++) {
		int tx = i % 2 + 1;
		u8 lane_mask = i < 2 ? INTEL_CX0_LANE0 : INTEL_CX0_LANE1;

		if (!(owned_lane_mask & lane_mask))
			continue;

		intel_cx0_rmw(encoder, lane_mask, PHY_CX0_TX_CONTROL(tx, 2),
			      CONTROL2_DISABLE_SINGLE_TX,
			      disables & BIT(i) ? CONTROL2_DISABLE_SINGLE_TX : 0,
			      MB_WRITE_COMMITTED);
	}

	if (intel_encoder_is_c10phy(encoder))
		intel_cx0_rmw(encoder, owned_lane_mask,
			      PHY_C10_VDR_CONTROL(1), 0,
			      C10_VDR_CTRL_UPDATE_CFG,
			      MB_WRITE_COMMITTED);
}

static u32 intel_cx0_get_pclk_pll_request(u8 lane_mask)
{
	u32 val = 0;
	int lane = 0;

	for_each_cx0_lane_in_mask(lane_mask, lane)
		val |= XELPDP_LANE_PCLK_PLL_REQUEST(lane);

	return val;
}

static u32 intel_cx0_get_pclk_pll_ack(u8 lane_mask)
{
	u32 val = 0;
	int lane = 0;

	for_each_cx0_lane_in_mask(lane_mask, lane)
		val |= XELPDP_LANE_PCLK_PLL_ACK(lane);

	return val;
}

static void intel_cx0pll_enable(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_encoder_to_phy(encoder);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool lane_reversal = dig_port->saved_port_bits & DDI_BUF_PORT_REVERSAL;
	u8 maxpclk_lane = lane_reversal ? INTEL_CX0_LANE1 :
					  INTEL_CX0_LANE0;
	intel_wakeref_t wakeref = intel_cx0_phy_transaction_begin(encoder);

	/*
	 * 1. Program PORT_CLOCK_CTL REGISTER to configure
	 * clock muxes, gating and SSC
	 */
	intel_program_port_clock_ctl(encoder, crtc_state, lane_reversal);

	/* 2. Bring PHY out of reset. */
	intel_cx0_phy_lane_reset(encoder, lane_reversal);

	/*
	 * 3. Change Phy power state to Ready.
	 * TODO: For DP alt mode use only one lane.
	 */
	intel_cx0_powerdown_change_sequence(encoder, INTEL_CX0_BOTH_LANES,
					    CX0_P2_STATE_READY);

	/*
	 * 4. Program PORT_MSGBUS_TIMER register's Message Bus Timer field to 0xA000.
	 *    (This is done inside intel_cx0_phy_transaction_begin(), since we would need
	 *    the right timer thresholds for readouts too.)
	 */

	/* 5. Program PHY internal PLL internal registers. */
	if (intel_encoder_is_c10phy(encoder))
		intel_c10_pll_program(i915, crtc_state, encoder);
	else
		intel_c20_pll_program(i915, crtc_state, encoder);

	/*
	 * 6. Program the enabled and disabled owned PHY lane
	 * transmitters over message bus
	 */
	intel_cx0_program_phy_lane(i915, encoder, crtc_state->lane_count, lane_reversal);

	/*
	 * 7. Follow the Display Voltage Frequency Switching - Sequence
	 * Before Frequency Change. We handle this step in bxt_set_cdclk().
	 */

	/*
	 * 8. Program DDI_CLK_VALFREQ to match intended DDI
	 * clock frequency.
	 */
	intel_de_write(i915, DDI_CLK_VALFREQ(encoder->port),
		       crtc_state->port_clock);

	/*
	 * 9. Set PORT_CLOCK_CTL register PCLK PLL Request
	 * LN<Lane for maxPCLK> to "1" to enable PLL.
	 */
	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
		     intel_cx0_get_pclk_pll_request(INTEL_CX0_BOTH_LANES),
		     intel_cx0_get_pclk_pll_request(maxpclk_lane));

	/* 10. Poll on PORT_CLOCK_CTL PCLK PLL Ack LN<Lane for maxPCLK> == "1". */
	if (intel_de_wait_custom(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
				 intel_cx0_get_pclk_pll_ack(INTEL_CX0_BOTH_LANES),
				 intel_cx0_get_pclk_pll_ack(maxpclk_lane),
				 XELPDP_PCLK_PLL_ENABLE_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "Port %c PLL not locked after %dus.\n",
			 phy_name(phy), XELPDP_PCLK_PLL_ENABLE_TIMEOUT_US);

	/*
	 * 11. Follow the Display Voltage Frequency Switching Sequence After
	 * Frequency Change. We handle this step in bxt_set_cdclk().
	 */

	/* TODO: enable TBT-ALT mode */
	intel_cx0_phy_transaction_end(encoder, wakeref);
}

int intel_mtl_tbt_calc_port_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	u32 clock;
	u32 val = intel_de_read(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port));

	clock = REG_FIELD_GET(XELPDP_DDI_CLOCK_SELECT_MASK, val);

	drm_WARN_ON(&i915->drm, !(val & XELPDP_FORWARD_CLOCK_UNGATE));
	drm_WARN_ON(&i915->drm, !(val & XELPDP_TBT_CLOCK_REQUEST));
	drm_WARN_ON(&i915->drm, !(val & XELPDP_TBT_CLOCK_ACK));

	switch (clock) {
	case XELPDP_DDI_CLOCK_SELECT_TBT_162:
		return 162000;
	case XELPDP_DDI_CLOCK_SELECT_TBT_270:
		return 270000;
	case XELPDP_DDI_CLOCK_SELECT_TBT_540:
		return 540000;
	case XELPDP_DDI_CLOCK_SELECT_TBT_810:
		return 810000;
	default:
		MISSING_CASE(clock);
		return 162000;
	}
}

static int intel_mtl_tbt_clock_select(struct drm_i915_private *i915, int clock)
{
	switch (clock) {
	case 162000:
		return XELPDP_DDI_CLOCK_SELECT_TBT_162;
	case 270000:
		return XELPDP_DDI_CLOCK_SELECT_TBT_270;
	case 540000:
		return XELPDP_DDI_CLOCK_SELECT_TBT_540;
	case 810000:
		return XELPDP_DDI_CLOCK_SELECT_TBT_810;
	default:
		MISSING_CASE(clock);
		return XELPDP_DDI_CLOCK_SELECT_TBT_162;
	}
}

static void intel_mtl_tbt_pll_enable(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_encoder_to_phy(encoder);
	u32 val = 0;

	/*
	 * 1. Program PORT_CLOCK_CTL REGISTER to configure
	 * clock muxes, gating and SSC
	 */
	val |= XELPDP_DDI_CLOCK_SELECT(intel_mtl_tbt_clock_select(i915, crtc_state->port_clock));
	val |= XELPDP_FORWARD_CLOCK_UNGATE;
	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
		     XELPDP_DDI_CLOCK_SELECT_MASK | XELPDP_FORWARD_CLOCK_UNGATE, val);

	/* 2. Read back PORT_CLOCK_CTL REGISTER */
	val = intel_de_read(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port));

	/*
	 * 3. Follow the Display Voltage Frequency Switching - Sequence
	 * Before Frequency Change. We handle this step in bxt_set_cdclk().
	 */

	/*
	 * 4. Set PORT_CLOCK_CTL register TBT CLOCK Request to "1" to enable PLL.
	 */
	val |= XELPDP_TBT_CLOCK_REQUEST;
	intel_de_write(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port), val);

	/* 5. Poll on PORT_CLOCK_CTL TBT CLOCK Ack == "1". */
	if (intel_de_wait_custom(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
				 XELPDP_TBT_CLOCK_ACK,
				 XELPDP_TBT_CLOCK_ACK,
				 100, 0, NULL))
		drm_warn(&i915->drm, "[ENCODER:%d:%s][%c] PHY PLL not locked after 100us.\n",
			 encoder->base.base.id, encoder->base.name, phy_name(phy));

	/*
	 * 6. Follow the Display Voltage Frequency Switching Sequence After
	 * Frequency Change. We handle this step in bxt_set_cdclk().
	 */

	/*
	 * 7. Program DDI_CLK_VALFREQ to match intended DDI
	 * clock frequency.
	 */
	intel_de_write(i915, DDI_CLK_VALFREQ(encoder->port),
		       crtc_state->port_clock);
}

void intel_mtl_pll_enable(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		intel_mtl_tbt_pll_enable(encoder, crtc_state);
	else
		intel_cx0pll_enable(encoder, crtc_state);
}

static void intel_cx0pll_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_encoder_to_phy(encoder);
	bool is_c10 = intel_encoder_is_c10phy(encoder);
	intel_wakeref_t wakeref = intel_cx0_phy_transaction_begin(encoder);

	/* 1. Change owned PHY lane power to Disable state. */
	intel_cx0_powerdown_change_sequence(encoder, INTEL_CX0_BOTH_LANES,
					    is_c10 ? CX0_P2PG_STATE_DISABLE :
					    CX0_P4PG_STATE_DISABLE);

	/*
	 * 2. Follow the Display Voltage Frequency Switching Sequence Before
	 * Frequency Change. We handle this step in bxt_set_cdclk().
	 */

	/*
	 * 3. Set PORT_CLOCK_CTL register PCLK PLL Request LN<Lane for maxPCLK>
	 * to "0" to disable PLL.
	 */
	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
		     intel_cx0_get_pclk_pll_request(INTEL_CX0_BOTH_LANES) |
		     intel_cx0_get_pclk_refclk_request(INTEL_CX0_BOTH_LANES), 0);

	/* 4. Program DDI_CLK_VALFREQ to 0. */
	intel_de_write(i915, DDI_CLK_VALFREQ(encoder->port), 0);

	/*
	 * 5. Poll on PORT_CLOCK_CTL PCLK PLL Ack LN<Lane for maxPCLK**> == "0".
	 */
	if (intel_de_wait_custom(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
				 intel_cx0_get_pclk_pll_ack(INTEL_CX0_BOTH_LANES) |
				 intel_cx0_get_pclk_refclk_ack(INTEL_CX0_BOTH_LANES), 0,
				 XELPDP_PCLK_PLL_DISABLE_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "Port %c PLL not unlocked after %dus.\n",
			 phy_name(phy), XELPDP_PCLK_PLL_DISABLE_TIMEOUT_US);

	/*
	 * 6. Follow the Display Voltage Frequency Switching Sequence After
	 * Frequency Change. We handle this step in bxt_set_cdclk().
	 */

	/* 7. Program PORT_CLOCK_CTL register to disable and gate clocks. */
	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
		     XELPDP_DDI_CLOCK_SELECT_MASK, 0);
	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
		     XELPDP_FORWARD_CLOCK_UNGATE, 0);

	intel_cx0_phy_transaction_end(encoder, wakeref);
}

static void intel_mtl_tbt_pll_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_encoder_to_phy(encoder);

	/*
	 * 1. Follow the Display Voltage Frequency Switching Sequence Before
	 * Frequency Change. We handle this step in bxt_set_cdclk().
	 */

	/*
	 * 2. Set PORT_CLOCK_CTL register TBT CLOCK Request to "0" to disable PLL.
	 */
	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
		     XELPDP_TBT_CLOCK_REQUEST, 0);

	/* 3. Poll on PORT_CLOCK_CTL TBT CLOCK Ack == "0". */
	if (intel_de_wait_custom(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
				 XELPDP_TBT_CLOCK_ACK, 0, 10, 0, NULL))
		drm_warn(&i915->drm, "[ENCODER:%d:%s][%c] PHY PLL not unlocked after 10us.\n",
			 encoder->base.base.id, encoder->base.name, phy_name(phy));

	/*
	 * 4. Follow the Display Voltage Frequency Switching Sequence After
	 * Frequency Change. We handle this step in bxt_set_cdclk().
	 */

	/*
	 * 5. Program PORT CLOCK CTRL register to disable and gate clocks
	 */
	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port),
		     XELPDP_DDI_CLOCK_SELECT_MASK |
		     XELPDP_FORWARD_CLOCK_UNGATE, 0);

	/* 6. Program DDI_CLK_VALFREQ to 0. */
	intel_de_write(i915, DDI_CLK_VALFREQ(encoder->port), 0);
}

void intel_mtl_pll_disable(struct intel_encoder *encoder)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

	if (intel_tc_port_in_tbt_alt_mode(dig_port))
		intel_mtl_tbt_pll_disable(encoder);
	else
		intel_cx0pll_disable(encoder);
}

enum icl_port_dpll_id
intel_mtl_port_pll_type(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	/*
	 * TODO: Determine the PLL type from the SW state, once MTL PLL
	 * handling is done via the standard shared DPLL framework.
	 */
	u32 val = intel_de_read(i915, XELPDP_PORT_CLOCK_CTL(i915, encoder->port));
	u32 clock = REG_FIELD_GET(XELPDP_DDI_CLOCK_SELECT_MASK, val);

	if (clock == XELPDP_DDI_CLOCK_SELECT_MAXPCLK ||
	    clock == XELPDP_DDI_CLOCK_SELECT_DIV18CLK)
		return ICL_PORT_DPLL_MG_PHY;
	else
		return ICL_PORT_DPLL_DEFAULT;
}

static void intel_c10pll_state_verify(const struct intel_crtc_state *state,
				      struct intel_crtc *crtc,
				      struct intel_encoder *encoder,
				      struct intel_c10pll_state *mpllb_hw_state)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	const struct intel_c10pll_state *mpllb_sw_state = &state->dpll_hw_state.cx0pll.c10;
	int i;

	if (intel_crtc_needs_fastset(state))
		return;

	for (i = 0; i < ARRAY_SIZE(mpllb_sw_state->pll); i++) {
		u8 expected = mpllb_sw_state->pll[i];

		I915_STATE_WARN(i915, mpllb_hw_state->pll[i] != expected,
				"[CRTC:%d:%s] mismatch in C10MPLLB: Register[%d] (expected 0x%02x, found 0x%02x)",
				crtc->base.base.id, crtc->base.name, i,
				expected, mpllb_hw_state->pll[i]);
	}

	I915_STATE_WARN(i915, mpllb_hw_state->tx != mpllb_sw_state->tx,
			"[CRTC:%d:%s] mismatch in C10MPLLB: Register TX0 (expected 0x%02x, found 0x%02x)",
			crtc->base.base.id, crtc->base.name,
			mpllb_sw_state->tx, mpllb_hw_state->tx);

	I915_STATE_WARN(i915, mpllb_hw_state->cmn != mpllb_sw_state->cmn,
			"[CRTC:%d:%s] mismatch in C10MPLLB: Register CMN0 (expected 0x%02x, found 0x%02x)",
			crtc->base.base.id, crtc->base.name,
			mpllb_sw_state->cmn, mpllb_hw_state->cmn);
}

void intel_cx0pll_readout_hw_state(struct intel_encoder *encoder,
				   struct intel_cx0pll_state *pll_state)
{
	if (intel_encoder_is_c10phy(encoder))
		intel_c10pll_readout_hw_state(encoder, &pll_state->c10);
	else
		intel_c20pll_readout_hw_state(encoder, &pll_state->c20);
}

int intel_cx0pll_calc_port_clock(struct intel_encoder *encoder,
				 const struct intel_cx0pll_state *pll_state)
{
	if (intel_encoder_is_c10phy(encoder))
		return intel_c10pll_calc_port_clock(encoder, &pll_state->c10);

	return intel_c20pll_calc_port_clock(encoder, &pll_state->c20);
}

static void intel_c20pll_state_verify(const struct intel_crtc_state *state,
				      struct intel_crtc *crtc,
				      struct intel_encoder *encoder,
				      struct intel_c20pll_state *mpll_hw_state)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	const struct intel_c20pll_state *mpll_sw_state = &state->dpll_hw_state.cx0pll.c20;
	bool sw_use_mpllb = intel_c20phy_use_mpllb(mpll_sw_state);
	bool hw_use_mpllb = intel_c20phy_use_mpllb(mpll_hw_state);
	int i;

	I915_STATE_WARN(i915, mpll_hw_state->clock != mpll_sw_state->clock,
			"[CRTC:%d:%s] mismatch in C20: Register CLOCK (expected %d, found %d)",
			crtc->base.base.id, crtc->base.name,
			mpll_sw_state->clock, mpll_hw_state->clock);

	I915_STATE_WARN(i915, sw_use_mpllb != hw_use_mpllb,
			"[CRTC:%d:%s] mismatch in C20: Register MPLLB selection (expected %d, found %d)",
			crtc->base.base.id, crtc->base.name,
			sw_use_mpllb, hw_use_mpllb);

	if (hw_use_mpllb) {
		for (i = 0; i < ARRAY_SIZE(mpll_sw_state->mpllb); i++) {
			I915_STATE_WARN(i915, mpll_hw_state->mpllb[i] != mpll_sw_state->mpllb[i],
					"[CRTC:%d:%s] mismatch in C20MPLLB: Register[%d] (expected 0x%04x, found 0x%04x)",
					crtc->base.base.id, crtc->base.name, i,
					mpll_sw_state->mpllb[i], mpll_hw_state->mpllb[i]);
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(mpll_sw_state->mplla); i++) {
			I915_STATE_WARN(i915, mpll_hw_state->mplla[i] != mpll_sw_state->mplla[i],
					"[CRTC:%d:%s] mismatch in C20MPLLA: Register[%d] (expected 0x%04x, found 0x%04x)",
					crtc->base.base.id, crtc->base.name, i,
					mpll_sw_state->mplla[i], mpll_hw_state->mplla[i]);
		}
	}

	for (i = 0; i < ARRAY_SIZE(mpll_sw_state->tx); i++) {
		I915_STATE_WARN(i915, mpll_hw_state->tx[i] != mpll_sw_state->tx[i],
				"[CRTC:%d:%s] mismatch in C20: Register TX[%i] (expected 0x%04x, found 0x%04x)",
				crtc->base.base.id, crtc->base.name, i,
				mpll_sw_state->tx[i], mpll_hw_state->tx[i]);
	}

	for (i = 0; i < ARRAY_SIZE(mpll_sw_state->cmn); i++) {
		I915_STATE_WARN(i915, mpll_hw_state->cmn[i] != mpll_sw_state->cmn[i],
				"[CRTC:%d:%s] mismatch in C20: Register CMN[%i] (expected 0x%04x, found 0x%04x)",
				crtc->base.base.id, crtc->base.name, i,
				mpll_sw_state->cmn[i], mpll_hw_state->cmn[i]);
	}
}

void intel_cx0pll_state_verify(struct intel_atomic_state *state,
			       struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_encoder *encoder;
	struct intel_cx0pll_state mpll_hw_state = {};

	if (DISPLAY_VER(i915) < 14)
		return;

	if (!new_crtc_state->hw.active)
		return;

	/* intel_get_crtc_new_encoder() only works for modeset/fastset commits */
	if (!intel_crtc_needs_modeset(new_crtc_state) &&
	    !intel_crtc_needs_fastset(new_crtc_state))
		return;

	encoder = intel_get_crtc_new_encoder(state, new_crtc_state);

	if (intel_tc_port_in_tbt_alt_mode(enc_to_dig_port(encoder)))
		return;

	intel_cx0pll_readout_hw_state(encoder, &mpll_hw_state);

	if (intel_encoder_is_c10phy(encoder))
		intel_c10pll_state_verify(new_crtc_state, crtc, encoder, &mpll_hw_state.c10);
	else
		intel_c20pll_state_verify(new_crtc_state, crtc, encoder, &mpll_hw_state.c20);
}
