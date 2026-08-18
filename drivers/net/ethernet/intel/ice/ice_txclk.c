// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 Intel Corporation */

#include "ice.h"
#include "ice_cpi.h"
#include "ice_txclk.h"

#define ICE_PHY0	0
#define ICE_PHY1	1

/**
 * ice_txclk_get_pin - map TX reference clock to its DPLL pin
 * @pf: pointer to the PF structure
 * @ref_clk: TX reference clock selection
 *
 * Return the DPLL pin corresponding to a given external TX reference
 * clock. Only external TX reference clocks (SYNCE and EREF0) are
 * represented as DPLL pins. The internal ENET (TXCO) clock has no
 * associated DPLL pin and therefore yields %NULL.
 *
 * This helper is used when emitting DPLL pin change notifications
 * after TX reference clock transitions have been verified.
 *
 * Return: Pointer to the corresponding struct dpll_pin, or %NULL if
 *         the TX reference clock has no DPLL pin representation.
 */
struct dpll_pin *
ice_txclk_get_pin(struct ice_pf *pf, enum ice_e825c_ref_clk ref_clk)
{
	switch (ref_clk) {
	case ICE_REF_CLK_SYNCE:
		return pf->dplls.txclks[E825_EXT_SYNCE_PIN_IDX].pin;
	case ICE_REF_CLK_EREF0:
		return pf->dplls.txclks[E825_EXT_EREF_PIN_IDX].pin;
	case ICE_REF_CLK_ENET:
	default:
		return NULL;
	}
}

/**
 * ice_txclk_enable_peer - Enable required TX reference clock on peer PHY
 * @pf: pointer to the PF structure
 * @clk: TX reference clock that must be enabled
 *
 * Some TX reference clocks on E825-class devices (SyncE and EREF0) must
 * be enabled on both PHY complexes to allow proper routing:
 *
 *   - SyncE must be enabled on both PHYs when used by PHY0
 *   - EREF0 must be enabled on both PHYs when used by PHY1
 *
 * If the requested clock is not yet enabled on the peer PHY, enable it.
 * ENET does not require duplication and is ignored.
 *
 * Return: 0 on success or negative error code on failure.
 */
static int ice_txclk_enable_peer(struct ice_pf *pf, enum ice_e825c_ref_clk clk)
{
	struct ice_pf *ctrl_pf = ice_get_ctrl_pf(pf);
	bool peer_clk_in_use;
	u8 port_num, phy;
	int err;

	if (clk == ICE_REF_CLK_ENET)
		return 0;

	if (IS_ERR_OR_NULL(ctrl_pf)) {
		dev_err(ice_pf_to_dev(pf),
			"Can't enable tx-clk on peer: no controlling PF\n");
		return -EINVAL;
	}

	port_num = pf->ptp.port.port_num;
	phy = port_num / pf->hw.ptp.ports_per_phy;
	peer_clk_in_use = true;

	/* Hold ctrl_pf->dplls.lock across both the peer-usage check and
	 * the enable AQ command so that two PFs racing to enable the same
	 * peer-PHY clock cannot both observe peer_clk_in_use == false and
	 * issue duplicate enables.
	 */
	mutex_lock(&ctrl_pf->dplls.lock);
	if (clk == ICE_REF_CLK_SYNCE && phy == ICE_PHY0)
		peer_clk_in_use = ice_txclk_any_port_uses(ctrl_pf,
							  ICE_PHY1,
							  clk);
	else if (clk == ICE_REF_CLK_EREF0 && phy == ICE_PHY1)
		peer_clk_in_use = ice_txclk_any_port_uses(ctrl_pf,
							  ICE_PHY0,
							  clk);

	if ((clk == ICE_REF_CLK_SYNCE && phy == ICE_PHY0 && !peer_clk_in_use) ||
	    (clk == ICE_REF_CLK_EREF0 && phy == ICE_PHY1 && !peer_clk_in_use)) {
		u8 peer_phy = phy ? ICE_PHY0 : ICE_PHY1;

		err = ice_cpi_ena_dis_clk_ref(&pf->hw, peer_phy, clk, true);
		if (err) {
			mutex_unlock(&ctrl_pf->dplls.lock);
			dev_err(ice_pf_to_dev(pf),
				"Failed to enable the %u TX clock for the %u PHY\n",
				clk, peer_phy);
			return err;
		}
	}
	mutex_unlock(&ctrl_pf->dplls.lock);

	return 0;
}

#define ICE_REFCLK_USER_TO_AQ_IDX(x) ((x) + 1)

/**
 * ice_txclk_set_clk - Set Tx reference clock
 * @pf: pointer to pf structure
 * @clk: new Tx clock
 *
 * Return: 0 on success, negative value otherwise.
 */
int ice_txclk_set_clk(struct ice_pf *pf, enum ice_e825c_ref_clk clk)
{
	struct ice_pf *ctrl_pf = ice_get_ctrl_pf(pf);
	struct ice_port_info *port_info;
	bool clk_in_use;
	u8 port_num, phy;
	int err;

	if (pf->ptp.port.tx_clk == clk)
		return 0;

	if (IS_ERR_OR_NULL(ctrl_pf)) {
		dev_err(ice_pf_to_dev(pf),
			"Can't set tx-clk: no controlling PF\n");
		return -EINVAL;
	}

	if (!test_bit(ICE_FLAG_DPLL, ctrl_pf->flags)) {
		dev_err(ice_pf_to_dev(pf),
			"Can't set tx-clk: ctrl PF DPLL not available\n");
		return -EOPNOTSUPP;
	}

	port_num = pf->ptp.port.port_num;
	phy = port_num / pf->hw.ptp.ports_per_phy;
	port_info = pf->hw.port_info;

	/* Hold ctrl_pf->dplls.lock across both the usage check and the
	 * enable AQ command so that two PFs racing to switch to the same
	 * (phy, clk) cannot both observe clk_in_use == false and issue
	 * duplicate enables. The tx_refclks bitmap is updated only later
	 * by ice_txclk_update_and_notify() after link-up, so without this
	 * the check-then-act window is wide open.
	 */
	mutex_lock(&ctrl_pf->dplls.lock);
	clk_in_use = ice_txclk_any_port_uses(ctrl_pf, phy, clk);
	if (!clk_in_use) {
		err = ice_cpi_ena_dis_clk_ref(&pf->hw, phy, clk, true);
		if (err) {
			mutex_unlock(&ctrl_pf->dplls.lock);
			dev_err(ice_pf_to_dev(pf), "Failed to enable the %u TX clock for the %u PHY\n",
				clk, phy);
			return err;
		}
	}
	mutex_unlock(&ctrl_pf->dplls.lock);

	if (!clk_in_use) {
		err = ice_txclk_enable_peer(pf, clk);
		if (err)
			return err;
	}

	/* We are ready to switch to the new TX clk. */
	err = ice_aq_set_link_restart_an(port_info, true, NULL,
					 ICE_REFCLK_USER_TO_AQ_IDX(clk));
	if (err) {
		dev_err(ice_pf_to_dev(pf),
			"AN restart AQ command failed with err %d\n",
			err);
		return err;
	}

	/* Clear txclk_switch_requested only after the AN restart AQ has been
	 * accepted by FW. Clearing earlier would race with any asynchronous
	 * link-up event: ice_txclk_update_and_notify() would observe the
	 * cleared flag, read the stale SERDES selector, and misinterpret the
	 * not-yet-applied switch as a HW rejection. Only clear if no newer
	 * request has overwritten tx_clk_req while we were dropping locks.
	 */
	mutex_lock(&pf->dplls.lock);
	if (pf->ptp.port.tx_clk_req == clk)
		pf->dplls.txclk_switch_requested = false;
	mutex_unlock(&pf->dplls.lock);

	return 0;
}

/**
 * ice_txclk_update_and_notify - Validate TX reference clock switching
 * @pf: pointer to PF structure
 *
 * After a link-up event, verify whether the previously requested TX reference
 * clock transition actually succeeded. The SERDES reference selector reflects
 * the effective hardware choice, which may differ from the requested clock
 * when Auto-Negotiation or firmware applies additional policy.
 *
 * If the hardware-selected clock differs from the requested one, update the
 * software state accordingly and stop further processing.
 *
 * When the switch is successful, update the per‑PHY usage bitmaps so that the
 * driver knows which reference clock is currently in use by this port.
 *
 * This function does not initiate a clock switch; it only validates the result
 * of a previously triggered transition and performs cleanup of unused clocks.
 */
void ice_txclk_update_and_notify(struct ice_pf *pf)
{
	struct ice_ptp_port *ptp_port = &pf->ptp.port;
	struct ice_pf *ctrl_pf = ice_get_ctrl_pf(pf);
	struct dpll_pin *old_pin = NULL;
	struct dpll_pin *new_pin = NULL;
	struct ice_hw *hw = &pf->hw;
	enum ice_e825c_ref_clk clk;
	bool notify_dpll = false;
	int err;
	u8 phy;

	phy = ptp_port->port_num / hw->ptp.ports_per_phy;

	/* Hold txclk_notify_rwsem for read across the entire critical
	 * region, including the out-of-lock dpll_*_change_ntf() calls
	 * below. ice_dpll_deinit() takes the write side to wait for all
	 * in-flight notifications to complete before freeing pins and the
	 * TXC DPLL device, preventing a use-after-free on rmmod.
	 */
	down_read(&pf->dplls.txclk_notify_rwsem);
	mutex_lock(&pf->dplls.lock);
	/* Bail out if DPLL subsystem is being torn down. ice_dpll_deinit()
	 * clears ICE_FLAG_DPLL before freeing pins and the dpll device, so a
	 * cleared flag under the lock means those objects can no longer be
	 * safely dereferenced.
	 */
	if (!test_bit(ICE_FLAG_DPLL, pf->flags)) {
		mutex_unlock(&pf->dplls.lock);
		goto out;
	}
	/* If a switch is still pending, the link-up event preceded the
	 * worker's AN restart. Hardware hasn't applied the new clock yet,
	 * so reading the SERDES selector now would produce a false failure.
	 * Let the worker run first; the link-up that follows the AN restart
	 * will trigger the verification.
	 */
	if (pf->dplls.txclk_switch_requested) {
		mutex_unlock(&pf->dplls.lock);
		goto out;
	}
	/* no TX clock change requested */
	if (pf->ptp.port.tx_clk == pf->ptp.port.tx_clk_req) {
		mutex_unlock(&pf->dplls.lock);
		goto out;
	}
	/* verify current Tx reference settings */
	err = ice_get_serdes_ref_sel_e825c(hw,
					   ptp_port->port_num,
					   &clk);
	if (err) {
		mutex_unlock(&pf->dplls.lock);
		goto out;
	}

	if (clk != pf->ptp.port.tx_clk_req) {
		dev_warn(ice_pf_to_dev(pf),
			 "Failed to switch tx-clk for phy %d and clk %u (current: %u)\n",
			 phy, pf->ptp.port.tx_clk_req, clk);
		old_pin = ice_txclk_get_pin(pf, pf->ptp.port.tx_clk_req);
		new_pin = ice_txclk_get_pin(pf, clk);
		pf->ptp.port.tx_clk = clk;
		pf->ptp.port.tx_clk_req = clk;
		/* Update the reference clock bitmap to match the hardware
		 * clock that was actually accepted, so that
		 * ice_txclk_any_port_uses() reflects reality even on failure.
		 * The map is owned by ctrl_pf; take its lock per documented
		 * order (pf->dplls.lock first, then ctrl_pf->dplls.lock) so
		 * readers on other PFs observe a consistent snapshot.
		 */
		if (!IS_ERR_OR_NULL(ctrl_pf)) {
			if (ctrl_pf != pf)
				mutex_lock(&ctrl_pf->dplls.lock);
			for (int i = 0; i < ICE_REF_CLK_MAX; i++) {
				if (clk == i)
					set_bit(ptp_port->port_num,
						&ctrl_pf->ptp.tx_refclks[phy][i]);
				else
					clear_bit(ptp_port->port_num,
						  &ctrl_pf->ptp.tx_refclks[phy][i]);
			}
			if (ctrl_pf != pf)
				mutex_unlock(&ctrl_pf->dplls.lock);
		}
		goto err_notify;
	}

	old_pin = ice_txclk_get_pin(pf, pf->ptp.port.tx_clk);
	pf->ptp.port.tx_clk = clk;
	pf->ptp.port.tx_clk_req = clk;

	if (IS_ERR_OR_NULL(ctrl_pf)) {
		dev_err(ice_pf_to_dev(pf),
			"Can't set tx-clk: no controlling PF\n");
		goto err_notify;
	}

	/* update Tx reference clock usage map; map is owned by ctrl_pf,
	 * take its lock per documented order so readers on other PFs see
	 * a consistent view.
	 */
	if (ctrl_pf != pf)
		mutex_lock(&ctrl_pf->dplls.lock);
	for (int i = 0; i < ICE_REF_CLK_MAX; i++)
		if (clk == i)
			set_bit(ptp_port->port_num,
				&ctrl_pf->ptp.tx_refclks[phy][i]);
		else
			clear_bit(ptp_port->port_num,
				  &ctrl_pf->ptp.tx_refclks[phy][i]);
	if (ctrl_pf != pf)
		mutex_unlock(&ctrl_pf->dplls.lock);

err_notify:
	/* Update TXC DPLL lock status based on effective TX clk, while still
	 * holding the lock to prevent concurrent link-up events from racing
	 * on dpll_state.
	 */
	if (!IS_ERR_OR_NULL(pf->dplls.txc.dpll)) {
		enum dpll_lock_status new_lock = ice_txclk_lock_status(clk);

		if (pf->dplls.txc.dpll_state != new_lock) {
			pf->dplls.txc.dpll_state = new_lock;
			notify_dpll = true;
		}
	}
	mutex_unlock(&pf->dplls.lock);

	/* Notify TX clk pins state transition */
	if (old_pin)
		dpll_pin_change_ntf(old_pin);
	if (new_pin)
		dpll_pin_change_ntf(new_pin);

	if (notify_dpll && !IS_ERR_OR_NULL(pf->dplls.txc.dpll))
		dpll_device_change_ntf(pf->dplls.txc.dpll);

out:
	up_read(&pf->dplls.txclk_notify_rwsem);
}
