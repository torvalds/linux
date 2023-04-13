// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_reg.h"
#include "intel_cx0_phy.h"
#include "intel_cx0_phy_regs.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
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

bool intel_is_c10phy(struct drm_i915_private *i915, enum phy phy)
{
	if (IS_METEORLAKE(i915) && (phy < PHY_C))
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

static void
assert_dc_off(struct drm_i915_private *i915)
{
	bool enabled;

	enabled = intel_display_power_is_enabled(i915, POWER_DOMAIN_DC_OFF);
	drm_WARN_ON(&i915->drm, !enabled);
}

/*
 * Prepare HW for CX0 phy transactions.
 *
 * It is required that PSR and DC5/6 are disabled before any CX0 message
 * bus transaction is executed.
 */
static intel_wakeref_t intel_cx0_phy_transaction_begin(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_psr_pause(intel_dp);
	return intel_display_power_get(i915, POWER_DOMAIN_DC_OFF);
}

static void intel_cx0_phy_transaction_end(struct intel_encoder *encoder, intel_wakeref_t wakeref)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_psr_resume(intel_dp);
	intel_display_power_put(i915, POWER_DOMAIN_DC_OFF, wakeref);
}

static void intel_clear_response_ready_flag(struct drm_i915_private *i915,
					    enum port port, int lane)
{
	intel_de_rmw(i915, XELPDP_PORT_P2M_MSGBUS_STATUS(port, lane),
		     0, XELPDP_PORT_P2M_RESPONSE_READY | XELPDP_PORT_P2M_ERROR_SET);
}

static void intel_cx0_bus_reset(struct drm_i915_private *i915, enum port port, int lane)
{
	enum phy phy = intel_port_to_phy(i915, port);

	intel_de_write(i915, XELPDP_PORT_M2P_MSGBUS_CTL(port, lane),
		       XELPDP_PORT_M2P_TRANSACTION_RESET);

	if (intel_de_wait_for_clear(i915, XELPDP_PORT_M2P_MSGBUS_CTL(port, lane),
				    XELPDP_PORT_M2P_TRANSACTION_RESET,
				    XELPDP_MSGBUS_TIMEOUT_SLOW)) {
		drm_err_once(&i915->drm, "Failed to bring PHY %c to idle.\n", phy_name(phy));
		return;
	}

	intel_clear_response_ready_flag(i915, port, lane);
}

static int intel_cx0_wait_for_ack(struct drm_i915_private *i915, enum port port,
				  int command, int lane, u32 *val)
{
	enum phy phy = intel_port_to_phy(i915, port);

	if (__intel_de_wait_for_register(i915,
					 XELPDP_PORT_P2M_MSGBUS_STATUS(port, lane),
					 XELPDP_PORT_P2M_RESPONSE_READY,
					 XELPDP_PORT_P2M_RESPONSE_READY,
					 XELPDP_MSGBUS_TIMEOUT_FAST_US,
					 XELPDP_MSGBUS_TIMEOUT_SLOW, val)) {
		drm_dbg_kms(&i915->drm, "PHY %c Timeout waiting for message ACK. Status: 0x%x\n",
			    phy_name(phy), *val);
		return -ETIMEDOUT;
	}

	if (*val & XELPDP_PORT_P2M_ERROR_SET) {
		drm_dbg_kms(&i915->drm, "PHY %c Error occurred during %s command. Status: 0x%x\n", phy_name(phy),
			    command == XELPDP_PORT_P2M_COMMAND_READ_ACK ? "read" : "write", *val);
		intel_cx0_bus_reset(i915, port, lane);
		return -EINVAL;
	}

	if (REG_FIELD_GET(XELPDP_PORT_P2M_COMMAND_TYPE_MASK, *val) != command) {
		drm_dbg_kms(&i915->drm, "PHY %c Not a %s response. MSGBUS Status: 0x%x.\n", phy_name(phy),
			    command == XELPDP_PORT_P2M_COMMAND_READ_ACK ? "read" : "write", *val);
		intel_cx0_bus_reset(i915, port, lane);
		return -EINVAL;
	}

	return 0;
}

static int __intel_cx0_read_once(struct drm_i915_private *i915, enum port port,
				 int lane, u16 addr)
{
	enum phy phy = intel_port_to_phy(i915, port);
	int ack;
	u32 val;

	if (intel_de_wait_for_clear(i915, XELPDP_PORT_M2P_MSGBUS_CTL(port, lane),
				    XELPDP_PORT_M2P_TRANSACTION_PENDING,
				    XELPDP_MSGBUS_TIMEOUT_SLOW)) {
		drm_dbg_kms(&i915->drm,
			    "PHY %c Timeout waiting for previous transaction to complete. Reset the bus and retry.\n", phy_name(phy));
		intel_cx0_bus_reset(i915, port, lane);
		return -ETIMEDOUT;
	}

	intel_de_write(i915, XELPDP_PORT_M2P_MSGBUS_CTL(port, lane),
		       XELPDP_PORT_M2P_TRANSACTION_PENDING |
		       XELPDP_PORT_M2P_COMMAND_READ |
		       XELPDP_PORT_M2P_ADDRESS(addr));

	ack = intel_cx0_wait_for_ack(i915, port, XELPDP_PORT_P2M_COMMAND_READ_ACK, lane, &val);
	if (ack < 0) {
		intel_cx0_bus_reset(i915, port, lane);
		return ack;
	}

	intel_clear_response_ready_flag(i915, port, lane);

	return REG_FIELD_GET(XELPDP_PORT_P2M_DATA_MASK, val);
}

static u8 __intel_cx0_read(struct drm_i915_private *i915, enum port port,
			   int lane, u16 addr)
{
	enum phy phy = intel_port_to_phy(i915, port);
	int i, status;

	assert_dc_off(i915);

	/* 3 tries is assumed to be enough to read successfully */
	for (i = 0; i < 3; i++) {
		status = __intel_cx0_read_once(i915, port, lane, addr);

		if (status >= 0)
			return status;
	}

	drm_err_once(&i915->drm, "PHY %c Read %04x failed after %d retries.\n",
		     phy_name(phy), addr, i);

	return 0;
}

static u8 intel_cx0_read(struct drm_i915_private *i915, enum port port,
			 u8 lane_mask, u16 addr)
{
	int lane = lane_mask_to_lane(lane_mask);

	return __intel_cx0_read(i915, port, lane, addr);
}

static int __intel_cx0_write_once(struct drm_i915_private *i915, enum port port,
				  int lane, u16 addr, u8 data, bool committed)
{
	enum phy phy = intel_port_to_phy(i915, port);
	u32 val;

	if (intel_de_wait_for_clear(i915, XELPDP_PORT_M2P_MSGBUS_CTL(port, lane),
				    XELPDP_PORT_M2P_TRANSACTION_PENDING,
				    XELPDP_MSGBUS_TIMEOUT_SLOW)) {
		drm_dbg_kms(&i915->drm,
			    "PHY %c Timeout waiting for previous transaction to complete. Resetting the bus.\n", phy_name(phy));
		intel_cx0_bus_reset(i915, port, lane);
		return -ETIMEDOUT;
	}

	intel_de_write(i915, XELPDP_PORT_M2P_MSGBUS_CTL(port, lane),
		       XELPDP_PORT_M2P_TRANSACTION_PENDING |
		       (committed ? XELPDP_PORT_M2P_COMMAND_WRITE_COMMITTED :
				    XELPDP_PORT_M2P_COMMAND_WRITE_UNCOMMITTED) |
		       XELPDP_PORT_M2P_DATA(data) |
		       XELPDP_PORT_M2P_ADDRESS(addr));

	if (intel_de_wait_for_clear(i915, XELPDP_PORT_M2P_MSGBUS_CTL(port, lane),
				    XELPDP_PORT_M2P_TRANSACTION_PENDING,
				    XELPDP_MSGBUS_TIMEOUT_SLOW)) {
		drm_dbg_kms(&i915->drm,
			    "PHY %c Timeout waiting for write to complete. Resetting the bus.\n", phy_name(phy));
		intel_cx0_bus_reset(i915, port, lane);
		return -ETIMEDOUT;
	}

	if (committed) {
		if (intel_cx0_wait_for_ack(i915, port, XELPDP_PORT_P2M_COMMAND_WRITE_ACK, lane, &val) < 0) {
			intel_cx0_bus_reset(i915, port, lane);
			return -EINVAL;
		}
	} else if ((intel_de_read(i915, XELPDP_PORT_P2M_MSGBUS_STATUS(port, lane)) &
		    XELPDP_PORT_P2M_ERROR_SET)) {
		drm_dbg_kms(&i915->drm,
			    "PHY %c Error occurred during write command.\n", phy_name(phy));
		intel_cx0_bus_reset(i915, port, lane);
		return -EINVAL;
	}

	intel_clear_response_ready_flag(i915, port, lane);

	return 0;
}

static void __intel_cx0_write(struct drm_i915_private *i915, enum port port,
			      int lane, u16 addr, u8 data, bool committed)
{
	enum phy phy = intel_port_to_phy(i915, port);
	int i, status;

	assert_dc_off(i915);

	/* 3 tries is assumed to be enough to write successfully */
	for (i = 0; i < 3; i++) {
		status = __intel_cx0_write_once(i915, port, lane, addr, data, committed);

		if (status == 0)
			return;
	}

	drm_err_once(&i915->drm,
		     "PHY %c Write %04x failed after %d retries.\n", phy_name(phy), addr, i);
}

static void intel_cx0_write(struct drm_i915_private *i915, enum port port,
			    u8 lane_mask, u16 addr, u8 data, bool committed)
{
	int lane;

	for_each_cx0_lane_in_mask(lane_mask, lane)
		__intel_cx0_write(i915, port, lane, addr, data, committed);
}

static void __intel_cx0_rmw(struct drm_i915_private *i915, enum port port,
			    int lane, u16 addr, u8 clear, u8 set, bool committed)
{
	u8 old, val;

	old = __intel_cx0_read(i915, port, lane, addr);
	val = (old & ~clear) | set;

	if (val != old)
		__intel_cx0_write(i915, port, lane, addr, val, committed);
}

static void intel_cx0_rmw(struct drm_i915_private *i915, enum port port,
			  u8 lane_mask, u16 addr, u8 clear, u8 set, bool committed)
{
	u8 lane;

	for_each_cx0_lane_in_mask(lane_mask, lane)
		__intel_cx0_rmw(i915, port, lane, addr, clear, set, committed);
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

static const struct intel_c10pll_state * const *
intel_c10pll_tables_get(struct intel_crtc_state *crtc_state,
			struct intel_encoder *encoder)
{
	if (intel_crtc_has_dp_encoder(crtc_state)) {
		if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
			return mtl_c10_edp_tables;
		else
			return mtl_c10_dp_tables;
	}

	/* TODO: Add HDMI Support */
	MISSING_CASE(encoder->type);
	return NULL;
}

static void intel_c10pll_update_pll(struct intel_crtc_state *crtc_state,
				    struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_cx0pll_state *pll_state = &crtc_state->cx0pll_state;
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
			crtc_state->cx0pll_state.c10 = *tables[i];
			intel_c10pll_update_pll(crtc_state, encoder);

			return 0;
		}
	}

	return -EINVAL;
}

int intel_cx0pll_calc_state(struct intel_crtc_state *crtc_state,
			    struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	drm_WARN_ON(&i915->drm, !intel_is_c10phy(i915, phy));

	return intel_c10pll_calc_state(crtc_state, encoder);
}

void intel_c10pll_readout_hw_state(struct intel_encoder *encoder,
				   struct intel_c10pll_state *pll_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	u8 lane = INTEL_CX0_LANE0;
	intel_wakeref_t wakeref;
	int i;

	wakeref = intel_cx0_phy_transaction_begin(encoder);

	/*
	 * According to C10 VDR Register programming Sequence we need
	 * to do this to read PHY internal registers from MsgBus.
	 */
	intel_cx0_rmw(i915, encoder->port, lane, PHY_C10_VDR_CONTROL(1),
		      0, C10_VDR_CTRL_MSGBUS_ACCESS,
		      MB_WRITE_COMMITTED);

	for (i = 0; i < ARRAY_SIZE(pll_state->pll); i++)
		pll_state->pll[i] = intel_cx0_read(i915, encoder->port, lane,
						   PHY_C10_VDR_PLL(i));

	pll_state->cmn = intel_cx0_read(i915, encoder->port, lane, PHY_C10_VDR_CMN(0));
	pll_state->tx = intel_cx0_read(i915, encoder->port, lane, PHY_C10_VDR_TX(0));

	intel_cx0_phy_transaction_end(encoder, wakeref);
}

static void intel_c10_pll_program(struct drm_i915_private *i915,
				  const struct intel_crtc_state *crtc_state,
				  struct intel_encoder *encoder)
{
	const struct intel_c10pll_state *pll_state = &crtc_state->cx0pll_state.c10;
	int i;

	intel_cx0_rmw(i915, encoder->port, INTEL_CX0_BOTH_LANES, PHY_C10_VDR_CONTROL(1),
		      0, C10_VDR_CTRL_MSGBUS_ACCESS,
		      MB_WRITE_COMMITTED);
	/* Custom width needs to be programmed to 0 for both the phy lanes */
	intel_cx0_rmw(i915, encoder->port, INTEL_CX0_BOTH_LANES, PHY_C10_VDR_CUSTOM_WIDTH,
		      C10_VDR_CUSTOM_WIDTH_MASK, C10_VDR_CUSTOM_WIDTH_8_10,
		      MB_WRITE_COMMITTED);
	intel_cx0_rmw(i915, encoder->port, INTEL_CX0_BOTH_LANES, PHY_C10_VDR_CONTROL(1),
		      0, C10_VDR_CTRL_UPDATE_CFG,
		      MB_WRITE_COMMITTED);

	/* Program the pll values only for the master lane */
	for (i = 0; i < ARRAY_SIZE(pll_state->pll); i++)
		intel_cx0_write(i915, encoder->port, INTEL_CX0_LANE0, PHY_C10_VDR_PLL(i),
				pll_state->pll[i],
				(i % 4) ? MB_WRITE_UNCOMMITTED : MB_WRITE_COMMITTED);

	intel_cx0_write(i915, encoder->port, INTEL_CX0_LANE0, PHY_C10_VDR_CMN(0), pll_state->cmn, MB_WRITE_COMMITTED);
	intel_cx0_write(i915, encoder->port, INTEL_CX0_LANE0, PHY_C10_VDR_TX(0), pll_state->tx, MB_WRITE_COMMITTED);

	intel_cx0_rmw(i915, encoder->port, INTEL_CX0_LANE0, PHY_C10_VDR_CONTROL(1),
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

int intel_c10pll_calc_port_clock(struct intel_encoder *encoder,
				 const struct intel_c10pll_state *pll_state)
{
	unsigned int frac_quot = 0, frac_rem = 0, frac_den = 1;
	unsigned int multiplier, tx_clk_div, refclk = 38400;

	if (pll_state->pll[0] & C10_PLL0_FRACEN) {
		frac_quot = pll_state->pll[12] << 8 | pll_state->pll[11];
		frac_rem =  pll_state->pll[14] << 8 | pll_state->pll[13];
		frac_den =  pll_state->pll[10] << 8 | pll_state->pll[9];
	}

	multiplier = (REG_FIELD_GET8(C10_PLL3_MULTIPLIERH_MASK, pll_state->pll[3]) << 8 |
		      pll_state->pll[2]) / 2 + 16;

	tx_clk_div = REG_FIELD_GET8(C10_PLL15_TXCLKDIV_MASK, pll_state->pll[15]);

	return DIV_ROUND_CLOSEST_ULL(mul_u32_u32(refclk, (multiplier << 16) + frac_quot) +
				     DIV_ROUND_CLOSEST(refclk * frac_rem, frac_den),
				     10 << (tx_clk_div + 16));
}

static void intel_program_port_clock_ctl(struct intel_encoder *encoder,
					 const struct intel_crtc_state *crtc_state,
					 bool lane_reversal)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	u32 val = 0;

	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL1(encoder->port), XELPDP_PORT_REVERSAL,
		     lane_reversal ? XELPDP_PORT_REVERSAL : 0);

	if (lane_reversal)
		val |= XELPDP_LANE1_PHY_CLOCK_SELECT;

	val |= XELPDP_FORWARD_CLOCK_UNGATE;
	val |= XELPDP_DDI_CLOCK_SELECT(XELPDP_DDI_CLOCK_SELECT_MAXPCLK);

	/* TODO: HDMI FRL */
	/* TODO: DP2.0 10G and 20G rates enable MPLLA*/
	val |= crtc_state->cx0pll_state.ssc_enabled ? XELPDP_SSC_ENABLE_PLLB : 0;

	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(encoder->port),
		     XELPDP_LANE1_PHY_CLOCK_SELECT |
		     XELPDP_FORWARD_CLOCK_UNGATE |
		     XELPDP_DDI_CLOCK_SELECT_MASK |
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

static void intel_cx0_powerdown_change_sequence(struct drm_i915_private *i915,
						enum port port,
						u8 lane_mask, u8 state)
{
	enum phy phy = intel_port_to_phy(i915, port);
	int lane;

	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL2(port),
		     intel_cx0_get_powerdown_state(INTEL_CX0_BOTH_LANES, XELPDP_LANE_POWERDOWN_NEW_STATE_MASK),
		     intel_cx0_get_powerdown_state(lane_mask, state));

	/* Wait for pending transactions.*/
	for_each_cx0_lane_in_mask(lane_mask, lane)
		if (intel_de_wait_for_clear(i915, XELPDP_PORT_M2P_MSGBUS_CTL(port, lane),
					    XELPDP_PORT_M2P_TRANSACTION_PENDING,
					    XELPDP_MSGBUS_TIMEOUT_SLOW)) {
			drm_dbg_kms(&i915->drm,
				    "PHY %c Timeout waiting for previous transaction to complete. Reset the bus.\n",
				    phy_name(phy));
			intel_cx0_bus_reset(i915, port, lane);
		}

	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL2(port),
		     intel_cx0_get_powerdown_update(INTEL_CX0_BOTH_LANES),
		     intel_cx0_get_powerdown_update(lane_mask));

	/* Update Timeout Value */
	if (__intel_de_wait_for_register(i915, XELPDP_PORT_BUF_CTL2(port),
					 intel_cx0_get_powerdown_update(lane_mask), 0,
					 XELPDP_PORT_POWERDOWN_UPDATE_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "PHY %c failed to bring out of Lane reset after %dus.\n",
			 phy_name(phy), XELPDP_PORT_RESET_START_TIMEOUT_US);
}

static void intel_cx0_setup_powerdown(struct drm_i915_private *i915, enum port port)
{
	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL2(port),
		     XELPDP_POWER_STATE_READY_MASK,
		     XELPDP_POWER_STATE_READY(CX0_P2_STATE_READY));
	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL3(port),
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

/* FIXME: Some Type-C cases need not reset both the lanes. Handle those cases. */
static void intel_cx0_phy_lane_reset(struct drm_i915_private *i915, enum port port,
				     bool lane_reversal)
{
	enum phy phy = intel_port_to_phy(i915, port);
	u8 lane_mask = lane_reversal ? INTEL_CX0_LANE1 :
				  INTEL_CX0_LANE0;

	if (__intel_de_wait_for_register(i915, XELPDP_PORT_BUF_CTL1(port),
					 XELPDP_PORT_BUF_SOC_PHY_READY,
					 XELPDP_PORT_BUF_SOC_PHY_READY,
					 XELPDP_PORT_BUF_SOC_READY_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "PHY %c failed to bring out of SOC reset after %dus.\n",
			 phy_name(phy), XELPDP_PORT_BUF_SOC_READY_TIMEOUT_US);

	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL2(port),
		     XELPDP_LANE_PIPE_RESET(0) | XELPDP_LANE_PIPE_RESET(1),
		     XELPDP_LANE_PIPE_RESET(0) | XELPDP_LANE_PIPE_RESET(1));

	if (__intel_de_wait_for_register(i915, XELPDP_PORT_BUF_CTL2(port),
					 XELPDP_LANE_PHY_CURRENT_STATUS(0) |
					 XELPDP_LANE_PHY_CURRENT_STATUS(1),
					 XELPDP_LANE_PHY_CURRENT_STATUS(0) |
					 XELPDP_LANE_PHY_CURRENT_STATUS(1),
					 XELPDP_PORT_RESET_START_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "PHY %c failed to bring out of Lane reset after %dus.\n",
			 phy_name(phy), XELPDP_PORT_RESET_START_TIMEOUT_US);

	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(port),
		     intel_cx0_get_pclk_refclk_request(INTEL_CX0_BOTH_LANES),
		     intel_cx0_get_pclk_refclk_request(lane_mask));

	if (__intel_de_wait_for_register(i915, XELPDP_PORT_CLOCK_CTL(port),
					 intel_cx0_get_pclk_refclk_ack(INTEL_CX0_BOTH_LANES),
					 intel_cx0_get_pclk_refclk_ack(lane_mask),
					 XELPDP_REFCLK_ENABLE_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "PHY %c failed to request refclk after %dus.\n",
			 phy_name(phy), XELPDP_REFCLK_ENABLE_TIMEOUT_US);

	intel_cx0_powerdown_change_sequence(i915, port, INTEL_CX0_BOTH_LANES,
					    CX0_P2_STATE_RESET);
	intel_cx0_setup_powerdown(i915, port);

	intel_de_rmw(i915, XELPDP_PORT_BUF_CTL2(port),
		     XELPDP_LANE_PIPE_RESET(0) | XELPDP_LANE_PIPE_RESET(1),
		     0);

	if (intel_de_wait_for_clear(i915, XELPDP_PORT_BUF_CTL2(port),
				    XELPDP_LANE_PHY_CURRENT_STATUS(0) |
				    XELPDP_LANE_PHY_CURRENT_STATUS(1),
				    XELPDP_PORT_RESET_END_TIMEOUT))
		drm_warn(&i915->drm, "PHY %c failed to bring out of Lane reset after %dms.\n",
			 phy_name(phy), XELPDP_PORT_RESET_END_TIMEOUT);
}

static void intel_c10_program_phy_lane(struct drm_i915_private *i915,
				       struct intel_encoder *encoder, int lane_count,
				       bool lane_reversal)
{
	u8 l0t1, l0t2, l1t1, l1t2;
	bool dp_alt_mode = intel_tc_port_in_dp_alt_mode(enc_to_dig_port(encoder));
	enum port port = encoder->port;

	intel_cx0_rmw(i915, port, INTEL_CX0_BOTH_LANES, PHY_C10_VDR_CONTROL(1),
		      0, C10_VDR_CTRL_MSGBUS_ACCESS,
		      MB_WRITE_COMMITTED);

	/* TODO: DP-alt MFD case where only one PHY lane should be programmed. */
	l0t1 = intel_cx0_read(i915, port, INTEL_CX0_LANE0, PHY_CX0_TX_CONTROL(1, 2));
	l0t2 = intel_cx0_read(i915, port, INTEL_CX0_LANE0, PHY_CX0_TX_CONTROL(2, 2));
	l1t1 = intel_cx0_read(i915, port, INTEL_CX0_LANE1, PHY_CX0_TX_CONTROL(1, 2));
	l1t2 = intel_cx0_read(i915, port, INTEL_CX0_LANE1, PHY_CX0_TX_CONTROL(2, 2));

	l0t1 |= CONTROL2_DISABLE_SINGLE_TX;
	l0t2 |= CONTROL2_DISABLE_SINGLE_TX;
	l1t1 |= CONTROL2_DISABLE_SINGLE_TX;
	l1t2 |= CONTROL2_DISABLE_SINGLE_TX;

	if (lane_reversal) {
		switch (lane_count) {
		case 4:
			l0t1 &= ~CONTROL2_DISABLE_SINGLE_TX;
			fallthrough;
		case 3:
			l0t2 &= ~CONTROL2_DISABLE_SINGLE_TX;
			fallthrough;
		case 2:
			l1t1 &= ~CONTROL2_DISABLE_SINGLE_TX;
			fallthrough;
		case 1:
			l1t2 &= ~CONTROL2_DISABLE_SINGLE_TX;
			break;
		default:
			MISSING_CASE(lane_count);
		}
	} else {
		switch (lane_count) {
		case 4:
			l1t2 &= ~CONTROL2_DISABLE_SINGLE_TX;
			fallthrough;
		case 3:
			l1t1 &= ~CONTROL2_DISABLE_SINGLE_TX;
			fallthrough;
		case 2:
			l0t2 &= ~CONTROL2_DISABLE_SINGLE_TX;
			l0t1 &= ~CONTROL2_DISABLE_SINGLE_TX;
			break;
		case 1:
			if (dp_alt_mode)
				l0t2 &= ~CONTROL2_DISABLE_SINGLE_TX;
			else
				l0t1 &= ~CONTROL2_DISABLE_SINGLE_TX;
			break;
		default:
			MISSING_CASE(lane_count);
		}
	}

	/* disable MLs */
	intel_cx0_write(i915, port, INTEL_CX0_LANE0, PHY_CX0_TX_CONTROL(1, 2),
			l0t1, MB_WRITE_COMMITTED);
	intel_cx0_write(i915, port, INTEL_CX0_LANE0, PHY_CX0_TX_CONTROL(2, 2),
			l0t2, MB_WRITE_COMMITTED);
	intel_cx0_write(i915, port, INTEL_CX0_LANE1, PHY_CX0_TX_CONTROL(1, 2),
			l1t1, MB_WRITE_COMMITTED);
	intel_cx0_write(i915, port, INTEL_CX0_LANE1, PHY_CX0_TX_CONTROL(2, 2),
			l1t2, MB_WRITE_COMMITTED);

	intel_cx0_rmw(i915, port, INTEL_CX0_BOTH_LANES, PHY_C10_VDR_CONTROL(1),
		      0, C10_VDR_CTRL_UPDATE_CFG,
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

static void intel_c10pll_enable(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool lane_reversal = dig_port->saved_port_bits & DDI_BUF_PORT_REVERSAL;
	u8 maxpclk_lane = lane_reversal ? INTEL_CX0_LANE1 :
					  INTEL_CX0_LANE0;

	/*
	 * 1. Program PORT_CLOCK_CTL REGISTER to configure
	 * clock muxes, gating and SSC
	 */
	intel_program_port_clock_ctl(encoder, crtc_state, lane_reversal);

	/* 2. Bring PHY out of reset. */
	intel_cx0_phy_lane_reset(i915, encoder->port, lane_reversal);

	/*
	 * 3. Change Phy power state to Ready.
	 * TODO: For DP alt mode use only one lane.
	 */
	intel_cx0_powerdown_change_sequence(i915, encoder->port, INTEL_CX0_BOTH_LANES,
					    CX0_P2_STATE_READY);

	/* 4. Program PHY internal PLL internal registers. */
	intel_c10_pll_program(i915, crtc_state, encoder);

	/*
	 * 5. Program the enabled and disabled owned PHY lane
	 * transmitters over message bus
	 */
	intel_c10_program_phy_lane(i915, encoder, crtc_state->lane_count, lane_reversal);

	/*
	 * 6. Follow the Display Voltage Frequency Switching - Sequence
	 * Before Frequency Change. We handle this step in bxt_set_cdclk().
	 */

	/*
	 * 7. Program DDI_CLK_VALFREQ to match intended DDI
	 * clock frequency.
	 */
	intel_de_write(i915, DDI_CLK_VALFREQ(encoder->port),
		       crtc_state->port_clock);

	/*
	 * 8. Set PORT_CLOCK_CTL register PCLK PLL Request
	 * LN<Lane for maxPCLK> to "1" to enable PLL.
	 */
	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(encoder->port),
		     intel_cx0_get_pclk_pll_request(INTEL_CX0_BOTH_LANES),
		     intel_cx0_get_pclk_pll_request(maxpclk_lane));

	/* 9. Poll on PORT_CLOCK_CTL PCLK PLL Ack LN<Lane for maxPCLK> == "1". */
	if (__intel_de_wait_for_register(i915, XELPDP_PORT_CLOCK_CTL(encoder->port),
					 intel_cx0_get_pclk_pll_ack(INTEL_CX0_BOTH_LANES),
					 intel_cx0_get_pclk_pll_ack(maxpclk_lane),
					 XELPDP_PCLK_PLL_ENABLE_TIMEOUT_US, 0, NULL))
		drm_warn(&i915->drm, "Port %c PLL not locked after %dus.\n",
			 phy_name(phy), XELPDP_PCLK_PLL_ENABLE_TIMEOUT_US);

	/*
	 * 10. Follow the Display Voltage Frequency Switching Sequence After
	 * Frequency Change. We handle this step in bxt_set_cdclk().
	 */
}

void intel_cx0pll_enable(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);
	intel_wakeref_t wakeref;

	wakeref = intel_cx0_phy_transaction_begin(encoder);

	drm_WARN_ON(&i915->drm, !intel_is_c10phy(i915, phy));
	intel_c10pll_enable(encoder, crtc_state);

	/* TODO: enable TBT-ALT mode */
	intel_cx0_phy_transaction_end(encoder, wakeref);
}

static void intel_c10pll_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	/* 1. Change owned PHY lane power to Disable state. */
	intel_cx0_powerdown_change_sequence(i915, encoder->port, INTEL_CX0_BOTH_LANES,
					    CX0_P2PG_STATE_DISABLE);

	/*
	 * 2. Follow the Display Voltage Frequency Switching Sequence Before
	 * Frequency Change. We handle this step in bxt_set_cdclk().
	 */

	/*
	 * 3. Set PORT_CLOCK_CTL register PCLK PLL Request LN<Lane for maxPCLK>
	 * to "0" to disable PLL.
	 */
	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(encoder->port),
		     intel_cx0_get_pclk_pll_request(INTEL_CX0_BOTH_LANES) |
		     intel_cx0_get_pclk_refclk_request(INTEL_CX0_BOTH_LANES), 0);

	/* 4. Program DDI_CLK_VALFREQ to 0. */
	intel_de_write(i915, DDI_CLK_VALFREQ(encoder->port), 0);

	/*
	 * 5. Poll on PORT_CLOCK_CTL PCLK PLL Ack LN<Lane for maxPCLK**> == "0".
	 */
	if (__intel_de_wait_for_register(i915, XELPDP_PORT_CLOCK_CTL(encoder->port),
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
	intel_de_rmw(i915, XELPDP_PORT_CLOCK_CTL(encoder->port),
		     XELPDP_DDI_CLOCK_SELECT_MASK |
		     XELPDP_FORWARD_CLOCK_UNGATE, 0);
}

void intel_cx0pll_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);
	intel_wakeref_t wakeref;

	wakeref = intel_cx0_phy_transaction_begin(encoder);

	drm_WARN_ON(&i915->drm, !intel_is_c10phy(i915, phy));
	intel_c10pll_disable(encoder);
	intel_cx0_phy_transaction_end(encoder, wakeref);
}

void intel_c10pll_state_verify(struct intel_atomic_state *state,
			       struct intel_crtc_state *new_crtc_state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_c10pll_state mpllb_hw_state = { 0 };
	struct intel_c10pll_state *mpllb_sw_state = &new_crtc_state->cx0pll_state.c10;
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->uapi.crtc);
	struct intel_encoder *encoder;
	enum phy phy;
	int i;

	if (DISPLAY_VER(i915) < 14)
		return;

	if (!new_crtc_state->hw.active)
		return;

	encoder = intel_get_crtc_new_encoder(state, new_crtc_state);
	phy = intel_port_to_phy(i915, encoder->port);

	if (!intel_is_c10phy(i915, phy))
		return;

	intel_c10pll_readout_hw_state(encoder, &mpllb_hw_state);

	for (i = 0; i < ARRAY_SIZE(mpllb_sw_state->pll); i++) {
		u8 expected = mpllb_sw_state->pll[i];

		I915_STATE_WARN(mpllb_hw_state.pll[i] != expected,
				"[CRTC:%d:%s] mismatch in C10MPLLB: Register[%d] (expected 0x%02x, found 0x%02x)",
				crtc->base.base.id, crtc->base.name,
				i, expected, mpllb_hw_state.pll[i]);
	}

	I915_STATE_WARN(mpllb_hw_state.tx != mpllb_sw_state->tx,
			"[CRTC:%d:%s] mismatch in C10MPLLB: Register TX0 (expected 0x%02x, found 0x%02x)",
			crtc->base.base.id, crtc->base.name,
			mpllb_sw_state->tx, mpllb_hw_state.tx);

	I915_STATE_WARN(mpllb_hw_state.cmn != mpllb_sw_state->cmn,
			"[CRTC:%d:%s] mismatch in C10MPLLB: Register CMN0 (expected 0x%02x, found 0x%02x)",
			crtc->base.base.id, crtc->base.name,
			mpllb_sw_state->cmn, mpllb_hw_state.cmn);
}
