// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include "fm10k.h"

/**
 * fm10k_dcbnl_ieee_getets - get the ETS configuration for the device
 * @dev: netdev interface for the device
 * @ets: ETS structure to push configuration to
 **/
static int fm10k_dcbnl_ieee_getets(struct net_device *dev, struct ieee_ets *ets)
{
	int i;

	/* we support 8 TCs in all modes */
	ets->ets_cap = IEEE_8021QAZ_MAX_TCS;
	ets->cbs = 0;

	/* we only support strict priority and cannot do traffic shaping */
	memset(ets->tc_tx_bw, 0, sizeof(ets->tc_tx_bw));
	memset(ets->tc_rx_bw, 0, sizeof(ets->tc_rx_bw));
	memset(ets->tc_tsa, IEEE_8021QAZ_TSA_STRICT, sizeof(ets->tc_tsa));

	/* populate the prio map based on the netdev */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		ets->prio_tc[i] = netdev_get_prio_tc_map(dev, i);

	return 0;
}

/**
 * fm10k_dcbnl_ieee_setets - set the ETS configuration for the device
 * @dev: netdev interface for the device
 * @ets: ETS structure to pull configuration from
 **/
static int fm10k_dcbnl_ieee_setets(struct net_device *dev, struct ieee_ets *ets)
{
	u8 num_tc = 0;
	int i, err;

	/* verify type and determine num_tcs needed */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		if (ets->tc_tx_bw[i] || ets->tc_rx_bw[i])
			return -EINVAL;
		if (ets->tc_tsa[i] != IEEE_8021QAZ_TSA_STRICT)
			return -EINVAL;
		if (ets->prio_tc[i] > num_tc)
			num_tc = ets->prio_tc[i];
	}

	/* if requested TC is greater than 0 then num_tcs is max + 1 */
	if (num_tc)
		num_tc++;

	if (num_tc > IEEE_8021QAZ_MAX_TCS)
		return -EINVAL;

	/* update TC hardware mapping if necessary */
	if (num_tc != netdev_get_num_tc(dev)) {
		err = fm10k_setup_tc(dev, num_tc);
		if (err)
			return err;
	}

	/* update priority mapping */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		netdev_set_prio_tc_map(dev, i, ets->prio_tc[i]);

	return 0;
}

/**
 * fm10k_dcbnl_ieee_getpfc - get the PFC configuration for the device
 * @dev: netdev interface for the device
 * @pfc: PFC structure to push configuration to
 **/
static int fm10k_dcbnl_ieee_getpfc(struct net_device *dev, struct ieee_pfc *pfc)
{
	struct fm10k_intfc *interface = netdev_priv(dev);

	/* record flow control max count and state of TCs */
	pfc->pfc_cap = IEEE_8021QAZ_MAX_TCS;
	pfc->pfc_en = interface->pfc_en;

	return 0;
}

/**
 * fm10k_dcbnl_ieee_setpfc - set the PFC configuration for the device
 * @dev: netdev interface for the device
 * @pfc: PFC structure to pull configuration from
 **/
static int fm10k_dcbnl_ieee_setpfc(struct net_device *dev, struct ieee_pfc *pfc)
{
	struct fm10k_intfc *interface = netdev_priv(dev);

	/* record PFC configuration to interface */
	interface->pfc_en = pfc->pfc_en;

	/* if we are running update the drop_en state for all queues */
	if (netif_running(dev))
		fm10k_update_rx_drop_en(interface);

	return 0;
}

/**
 * fm10k_dcbnl_ieee_getdcbx - get the DCBX configuration for the device
 * @dev: netdev interface for the device
 *
 * Returns that we support only IEEE DCB for this interface
 **/
static u8 fm10k_dcbnl_getdcbx(struct net_device __always_unused *dev)
{
	return DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_IEEE;
}

/**
 * fm10k_dcbnl_ieee_setdcbx - get the DCBX configuration for the device
 * @dev: netdev interface for the device
 * @mode: new mode for this device
 *
 * Returns error on attempt to enable anything but IEEE DCB for this interface
 **/
static u8 fm10k_dcbnl_setdcbx(struct net_device __always_unused *dev, u8 mode)
{
	return (mode != (DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_IEEE)) ? 1 : 0;
}

static const struct dcbnl_rtnl_ops fm10k_dcbnl_ops = {
	.ieee_getets	= fm10k_dcbnl_ieee_getets,
	.ieee_setets	= fm10k_dcbnl_ieee_setets,
	.ieee_getpfc	= fm10k_dcbnl_ieee_getpfc,
	.ieee_setpfc	= fm10k_dcbnl_ieee_setpfc,

	.getdcbx	= fm10k_dcbnl_getdcbx,
	.setdcbx	= fm10k_dcbnl_setdcbx,
};

/**
 * fm10k_dcbnl_set_ops - Configures dcbnl ops pointer for netdev
 * @dev: netdev interface for the device
 *
 * Enables PF for DCB by assigning DCBNL ops pointer.
 **/
void fm10k_dcbnl_set_ops(struct net_device *dev)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_hw *hw = &interface->hw;

	if (hw->mac.type == fm10k_mac_pf)
		dev->dcbnl_ops = &fm10k_dcbnl_ops;
}
