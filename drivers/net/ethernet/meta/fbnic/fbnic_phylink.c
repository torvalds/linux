// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/pcs/pcs-xpcs.h>
#include <linux/phy.h>
#include <linux/phylink.h>

#include "fbnic.h"
#include "fbnic_mac.h"
#include "fbnic_netdev.h"

static phy_interface_t fbnic_phylink_select_interface(u8 aui)
{
	switch (aui) {
	case FBNIC_AUI_100GAUI2:
		return PHY_INTERFACE_MODE_100GBASEP;
	case FBNIC_AUI_50GAUI1:
		return PHY_INTERFACE_MODE_50GBASER;
	case FBNIC_AUI_LAUI2:
		return PHY_INTERFACE_MODE_LAUI;
	case FBNIC_AUI_25GAUI:
		return PHY_INTERFACE_MODE_25GBASER;
	}

	return PHY_INTERFACE_MODE_NA;
}

void fbnic_phylink_get_pauseparam(struct net_device *netdev,
				  struct ethtool_pauseparam *pause)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	phylink_ethtool_get_pauseparam(fbn->phylink, pause);
}

int fbnic_phylink_set_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	return phylink_ethtool_set_pauseparam(fbn->phylink, pause);
}

static void
fbnic_phylink_get_supported_fec_modes(unsigned long *supported)
{
	/* The NIC can support up to 8 possible combinations.
	 * Either 50G-CR, or 100G-CR2
	 *   This is with RS FEC mode only
	 * Either 25G-CR, or 50G-CR2
	 *   This is with No FEC, RS, or Base-R
	 */
	if (phylink_test(supported, 100000baseCR2_Full) ||
	    phylink_test(supported, 50000baseCR_Full))
		phylink_set(supported, FEC_RS);
	if (phylink_test(supported, 50000baseCR2_Full) ||
	    phylink_test(supported, 25000baseCR_Full)) {
		phylink_set(supported, FEC_BASER);
		phylink_set(supported, FEC_NONE);
		phylink_set(supported, FEC_RS);
	}
}

int fbnic_phylink_ethtool_ksettings_get(struct net_device *netdev,
					struct ethtool_link_ksettings *cmd)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	int err;

	err = phylink_ethtool_ksettings_get(fbn->phylink, cmd);
	if (!err) {
		unsigned long *supp = cmd->link_modes.supported;

		cmd->base.port = PORT_DA;
		cmd->lanes = (fbn->aui & FBNIC_AUI_MODE_R2) ? 2 : 1;

		fbnic_phylink_get_supported_fec_modes(supp);
	}

	return err;
}

int fbnic_phylink_get_fecparam(struct net_device *netdev,
			       struct ethtool_fecparam *fecparam)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	if (fbn->fec & FBNIC_FEC_RS) {
		fecparam->active_fec = ETHTOOL_FEC_RS;
		fecparam->fec = ETHTOOL_FEC_RS;
	} else if (fbn->fec & FBNIC_FEC_BASER) {
		fecparam->active_fec = ETHTOOL_FEC_BASER;
		fecparam->fec = ETHTOOL_FEC_BASER;
	} else {
		fecparam->active_fec = ETHTOOL_FEC_OFF;
		fecparam->fec = ETHTOOL_FEC_OFF;
	}

	if (fbn->aui & FBNIC_AUI_MODE_PAM4)
		fecparam->fec |= ETHTOOL_FEC_AUTO;

	return 0;
}

static struct phylink_pcs *
fbnic_phylink_mac_select_pcs(struct phylink_config *config,
			     phy_interface_t interface)
{
	struct net_device *netdev = to_net_dev(config->dev);
	struct fbnic_net *fbn = netdev_priv(netdev);

	return fbn->pcs;
}

static int
fbnic_phylink_mac_prepare(struct phylink_config *config, unsigned int mode,
			  phy_interface_t iface)
{
	struct net_device *netdev = to_net_dev(config->dev);
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	fbd->mac->prepare(fbd, fbn->aui, fbn->fec);

	return 0;
}

static void
fbnic_phylink_mac_config(struct phylink_config *config, unsigned int mode,
			 const struct phylink_link_state *state)
{
}

static int
fbnic_phylink_mac_finish(struct phylink_config *config, unsigned int mode,
			 phy_interface_t iface)
{
	struct net_device *netdev = to_net_dev(config->dev);
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	/* Retest the link state and restart interrupts */
	fbd->mac->get_link(fbd, fbn->aui, fbn->fec);

	return 0;
}

static void
fbnic_phylink_mac_link_down(struct phylink_config *config, unsigned int mode,
			    phy_interface_t interface)
{
	struct net_device *netdev = to_net_dev(config->dev);
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	fbd->mac->link_down(fbd);

	fbn->link_down_events++;
}

static void
fbnic_phylink_mac_link_up(struct phylink_config *config,
			  struct phy_device *phy, unsigned int mode,
			  phy_interface_t interface, int speed, int duplex,
			  bool tx_pause, bool rx_pause)
{
	struct net_device *netdev = to_net_dev(config->dev);
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	fbn->tx_pause = tx_pause;
	fbnic_config_drop_mode(fbn, tx_pause);

	fbd->mac->link_up(fbd, tx_pause, rx_pause);
}

static const struct phylink_mac_ops fbnic_phylink_mac_ops = {
	.mac_select_pcs = fbnic_phylink_mac_select_pcs,
	.mac_prepare = fbnic_phylink_mac_prepare,
	.mac_config = fbnic_phylink_mac_config,
	.mac_finish = fbnic_phylink_mac_finish,
	.mac_link_down = fbnic_phylink_mac_link_down,
	.mac_link_up = fbnic_phylink_mac_link_up,
};

/**
 * fbnic_phylink_create - Phylink device creation
 * @netdev: Network Device struct to attach phylink device
 *
 * Initialize and attach a phylink instance to the device. The phylink
 * device will make use of the netdev struct to track carrier and will
 * eventually be used to expose the current state of the MAC and PCS
 * setup.
 *
 * Return: 0 on success, negative on failure
 **/
int fbnic_phylink_create(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;
	struct phylink_pcs *pcs;
	struct phylink *phylink;
	int err;

	pcs = xpcs_create_pcs_mdiodev(fbd->mdio_bus, 0);
	if (IS_ERR(pcs)) {
		err = PTR_ERR(pcs);
		dev_err(fbd->dev, "Failed to create PCS device: %d\n", err);
		return err;
	}

	fbn->pcs = pcs;

	fbn->phylink_config.dev = &netdev->dev;
	fbn->phylink_config.type = PHYLINK_NETDEV;
	fbn->phylink_config.mac_capabilities = MAC_SYM_PAUSE | MAC_ASYM_PAUSE |
					       MAC_25000FD | MAC_50000FD |
					       MAC_100000FD;
	fbn->phylink_config.default_an_inband = true;

	__set_bit(PHY_INTERFACE_MODE_100GBASEP,
		  fbn->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_50GBASER,
		  fbn->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_LAUI,
		  fbn->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_25GBASER,
		  fbn->phylink_config.supported_interfaces);

	fbnic_mac_get_fw_settings(fbd, &fbn->aui, &fbn->fec);

	phylink = phylink_create(&fbn->phylink_config, NULL,
				 fbnic_phylink_select_interface(fbn->aui),
				 &fbnic_phylink_mac_ops);
	if (IS_ERR(phylink)) {
		err = PTR_ERR(phylink);
		dev_err(netdev->dev.parent,
			"Failed to create Phylink interface, err: %d\n", err);
		xpcs_destroy_pcs(pcs);
		return err;
	}

	fbn->phylink = phylink;

	return 0;
}

/**
 * fbnic_phylink_destroy - Teardown phylink related interfaces
 * @netdev: Network Device struct containing phylink device
 *
 * Detach and free resources related to phylink interface.
 **/
void fbnic_phylink_destroy(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	if (fbn->phylink)
		phylink_destroy(fbn->phylink);
	if (fbn->pcs)
		xpcs_destroy_pcs(fbn->pcs);
}

/**
 * fbnic_phylink_pmd_training_complete_notify - PMD training complete notifier
 * @netdev: Netdev struct phylink device attached to
 *
 * When the link first comes up the PMD will have a period of 2 to 3 seconds
 * where the link will flutter due to link training. To avoid spamming the
 * kernel log with messages about this we add a delay of 4 seconds from the
 * time of the last PCS report of link so that we can guarantee we are unlikely
 * to see any further link loss events due to link training.
 **/
void fbnic_phylink_pmd_training_complete_notify(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	if (fbd->pmd_state != FBNIC_PMD_TRAINING)
		return;

	/* Prevent reading end_of_pmd_training until we verified state */
	smp_rmb();

	if (!time_before(READ_ONCE(fbd->end_of_pmd_training), jiffies))
		return;

	/* At this point we have verified that the link has been up for
	 * the full training duration. As a first step we will try
	 * transitioning to link ready.
	 */
	if (cmpxchg(&fbd->pmd_state, FBNIC_PMD_TRAINING,
		    FBNIC_PMD_LINK_READY) != FBNIC_PMD_TRAINING)
		return;

	/* Perform a follow-up check to verify that the link didn't flap
	 * just before our transition by rechecking the training timer.
	 */
	if (!time_before(READ_ONCE(fbd->end_of_pmd_training), jiffies))
		return;

	/* The training timeout has been completed. We are good to swap out
	 * link_ready for send_data assuming no other events have occurred
	 * that would have pulled us back into initialization or training.
	 */
	if (cmpxchg(&fbd->pmd_state, FBNIC_PMD_LINK_READY,
		    FBNIC_PMD_SEND_DATA) != FBNIC_PMD_LINK_READY)
		return;

	phylink_pcs_change(fbn->pcs, false);
}
