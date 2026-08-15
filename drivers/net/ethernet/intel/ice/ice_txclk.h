/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2026 Intel Corporation */

#ifndef _ICE_TXCLK_H_
#define _ICE_TXCLK_H_

/**
 * ice_txclk_any_port_uses - check if any port on a PHY uses this TX refclk
 * @ctrl_pf: control PF (owner of the shared tx_refclks map)
 * @phy: PHY index
 * @clk: TX reference clock
 *
 * Return: true if any bit (port) is set for this clock on this PHY
 */
static inline bool
ice_txclk_any_port_uses(const struct ice_pf *ctrl_pf, u8 phy,
			enum ice_e825c_ref_clk clk)
{
	return find_first_bit(&ctrl_pf->ptp.tx_refclks[phy][clk],
			BITS_PER_LONG) < BITS_PER_LONG;
}

static inline enum dpll_lock_status
ice_txclk_lock_status(enum ice_e825c_ref_clk clk)
{
	switch (clk) {
	case ICE_REF_CLK_SYNCE:
	case ICE_REF_CLK_EREF0:
		return DPLL_LOCK_STATUS_LOCKED;
	case ICE_REF_CLK_ENET:
	default:
		return DPLL_LOCK_STATUS_UNLOCKED;
	}
}

int ice_txclk_set_clk(struct ice_pf *pf, enum ice_e825c_ref_clk clk);
void ice_txclk_update_and_notify(struct ice_pf *pf);
struct dpll_pin *ice_txclk_get_pin(struct ice_pf *pf,
				   enum ice_e825c_ref_clk ref_clk);
#endif /* _ICE_TXCLK_H_ */
