/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include "fm10k.h"

static netdev_tx_t fm10k_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static int fm10k_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < 68 || new_mtu > FM10K_MAX_JUMBO_FRAME_SIZE)
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}

static int fm10k_set_mac(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	s32 err = 0;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (!err) {
		ether_addr_copy(dev->dev_addr, addr->sa_data);
		dev->addr_assign_type &= ~NET_ADDR_RANDOM;
	}

	return err;
}

static void fm10k_set_rx_mode(struct net_device *dev)
{
}

static const struct net_device_ops fm10k_netdev_ops = {
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_start_xmit		= fm10k_xmit_frame,
	.ndo_set_mac_address	= fm10k_set_mac,
	.ndo_change_mtu		= fm10k_change_mtu,
	.ndo_set_rx_mode	= fm10k_set_rx_mode,
};

#define DEFAULT_DEBUG_LEVEL_SHIFT 3

struct net_device *fm10k_alloc_netdev(void)
{
	struct fm10k_intfc *interface;
	struct net_device *dev;

	dev = alloc_etherdev(sizeof(struct fm10k_intfc));
	if (!dev)
		return NULL;

	/* set net device and ethtool ops */
	dev->netdev_ops = &fm10k_netdev_ops;

	/* configure default debug level */
	interface = netdev_priv(dev);
	interface->msg_enable = (1 << DEFAULT_DEBUG_LEVEL_SHIFT) - 1;

	/* configure default features */
	dev->features |= NETIF_F_SG;

	/* all features defined to this point should be changeable */
	dev->hw_features |= dev->features;

	/* configure VLAN features */
	dev->vlan_features |= dev->features;

	/* configure tunnel offloads */
	dev->hw_enc_features = NETIF_F_SG;

	return dev;
}
