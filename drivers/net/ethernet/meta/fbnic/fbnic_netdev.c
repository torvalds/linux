// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/etherdevice.h>
#include <linux/ipv6.h>
#include <linux/types.h>

#include "fbnic.h"
#include "fbnic_netdev.h"
#include "fbnic_txrx.h"

int __fbnic_open(struct fbnic_net *fbn)
{
	struct fbnic_dev *fbd = fbn->fbd;
	int err;

	err = fbnic_alloc_napi_vectors(fbn);
	if (err)
		return err;

	err = fbnic_alloc_resources(fbn);
	if (err)
		goto free_napi_vectors;

	err = netif_set_real_num_tx_queues(fbn->netdev,
					   fbn->num_tx_queues);
	if (err)
		goto free_resources;

	err = netif_set_real_num_rx_queues(fbn->netdev,
					   fbn->num_rx_queues);
	if (err)
		goto free_resources;

	/* Send ownership message and flush to verify FW has seen it */
	err = fbnic_fw_xmit_ownership_msg(fbd, true);
	if (err) {
		dev_warn(fbd->dev,
			 "Error %d sending host ownership message to the firmware\n",
			 err);
		goto free_resources;
	}

	err = fbnic_fw_init_heartbeat(fbd, false);
	if (err)
		goto release_ownership;

	err = fbnic_pcs_irq_enable(fbd);
	if (err)
		goto release_ownership;
	/* Pull the BMC config and initialize the RPC */
	fbnic_bmc_rpc_init(fbd);
	fbnic_rss_reinit(fbd, fbn);

	return 0;
release_ownership:
	fbnic_fw_xmit_ownership_msg(fbn->fbd, false);
free_resources:
	fbnic_free_resources(fbn);
free_napi_vectors:
	fbnic_free_napi_vectors(fbn);
	return err;
}

static int fbnic_open(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	int err;

	err = __fbnic_open(fbn);
	if (!err)
		fbnic_up(fbn);

	return err;
}

static int fbnic_stop(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	fbnic_down(fbn);
	fbnic_pcs_irq_disable(fbn->fbd);

	fbnic_fw_xmit_ownership_msg(fbn->fbd, false);

	fbnic_free_resources(fbn);
	fbnic_free_napi_vectors(fbn);

	return 0;
}

static int fbnic_uc_sync(struct net_device *netdev, const unsigned char *addr)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_mac_addr *avail_addr;

	if (WARN_ON(!is_valid_ether_addr(addr)))
		return -EADDRNOTAVAIL;

	avail_addr = __fbnic_uc_sync(fbn->fbd, addr);
	if (!avail_addr)
		return -ENOSPC;

	/* Add type flag indicating this address is in use by the host */
	set_bit(FBNIC_MAC_ADDR_T_UNICAST, avail_addr->act_tcam);

	return 0;
}

static int fbnic_uc_unsync(struct net_device *netdev, const unsigned char *addr)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;
	int i, ret;

	/* Scan from middle of list to bottom, filling bottom up.
	 * Skip the first entry which is reserved for dev_addr and
	 * leave the last entry to use for promiscuous filtering.
	 */
	for (i = fbd->mac_addr_boundary, ret = -ENOENT;
	     i < FBNIC_RPC_TCAM_MACDA_HOST_ADDR_IDX && ret; i++) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		if (!ether_addr_equal(mac_addr->value.addr8, addr))
			continue;

		ret = __fbnic_uc_unsync(mac_addr);
	}

	return ret;
}

static int fbnic_mc_sync(struct net_device *netdev, const unsigned char *addr)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_mac_addr *avail_addr;

	if (WARN_ON(!is_multicast_ether_addr(addr)))
		return -EADDRNOTAVAIL;

	avail_addr = __fbnic_mc_sync(fbn->fbd, addr);
	if (!avail_addr)
		return -ENOSPC;

	/* Add type flag indicating this address is in use by the host */
	set_bit(FBNIC_MAC_ADDR_T_MULTICAST, avail_addr->act_tcam);

	return 0;
}

static int fbnic_mc_unsync(struct net_device *netdev, const unsigned char *addr)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;
	int i, ret;

	/* Scan from middle of list to top, filling top down.
	 * Skip over the address reserved for the BMC MAC and
	 * exclude index 0 as that belongs to the broadcast address
	 */
	for (i = fbd->mac_addr_boundary, ret = -ENOENT;
	     --i > FBNIC_RPC_TCAM_MACDA_BROADCAST_IDX && ret;) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		if (!ether_addr_equal(mac_addr->value.addr8, addr))
			continue;

		ret = __fbnic_mc_unsync(mac_addr);
	}

	return ret;
}

void __fbnic_set_rx_mode(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	bool uc_promisc = false, mc_promisc = false;
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_mac_addr *mac_addr;
	int err;

	/* Populate host address from dev_addr */
	mac_addr = &fbd->mac_addr[FBNIC_RPC_TCAM_MACDA_HOST_ADDR_IDX];
	if (!ether_addr_equal(mac_addr->value.addr8, netdev->dev_addr) ||
	    mac_addr->state != FBNIC_TCAM_S_VALID) {
		ether_addr_copy(mac_addr->value.addr8, netdev->dev_addr);
		mac_addr->state = FBNIC_TCAM_S_UPDATE;
		set_bit(FBNIC_MAC_ADDR_T_UNICAST, mac_addr->act_tcam);
	}

	/* Populate broadcast address if broadcast is enabled */
	mac_addr = &fbd->mac_addr[FBNIC_RPC_TCAM_MACDA_BROADCAST_IDX];
	if (netdev->flags & IFF_BROADCAST) {
		if (!is_broadcast_ether_addr(mac_addr->value.addr8) ||
		    mac_addr->state != FBNIC_TCAM_S_VALID) {
			eth_broadcast_addr(mac_addr->value.addr8);
			mac_addr->state = FBNIC_TCAM_S_ADD;
		}
		set_bit(FBNIC_MAC_ADDR_T_BROADCAST, mac_addr->act_tcam);
	} else if (mac_addr->state == FBNIC_TCAM_S_VALID) {
		__fbnic_xc_unsync(mac_addr, FBNIC_MAC_ADDR_T_BROADCAST);
	}

	/* Synchronize unicast and multicast address lists */
	err = __dev_uc_sync(netdev, fbnic_uc_sync, fbnic_uc_unsync);
	if (err == -ENOSPC)
		uc_promisc = true;
	err = __dev_mc_sync(netdev, fbnic_mc_sync, fbnic_mc_unsync);
	if (err == -ENOSPC)
		mc_promisc = true;

	uc_promisc |= !!(netdev->flags & IFF_PROMISC);
	mc_promisc |= !!(netdev->flags & IFF_ALLMULTI) || uc_promisc;

	/* Populate last TCAM entry with promiscuous entry and 0/1 bit mask */
	mac_addr = &fbd->mac_addr[FBNIC_RPC_TCAM_MACDA_PROMISC_IDX];
	if (uc_promisc) {
		if (!is_zero_ether_addr(mac_addr->value.addr8) ||
		    mac_addr->state != FBNIC_TCAM_S_VALID) {
			eth_zero_addr(mac_addr->value.addr8);
			eth_broadcast_addr(mac_addr->mask.addr8);
			clear_bit(FBNIC_MAC_ADDR_T_ALLMULTI,
				  mac_addr->act_tcam);
			set_bit(FBNIC_MAC_ADDR_T_PROMISC,
				mac_addr->act_tcam);
			mac_addr->state = FBNIC_TCAM_S_ADD;
		}
	} else if (mc_promisc &&
		   (!fbnic_bmc_present(fbd) || !fbd->fw_cap.all_multi)) {
		/* We have to add a special handler for multicast as the
		 * BMC may have an all-multi rule already in place. As such
		 * adding a rule ourselves won't do any good so we will have
		 * to modify the rules for the ALL MULTI below if the BMC
		 * already has the rule in place.
		 */
		if (!is_multicast_ether_addr(mac_addr->value.addr8) ||
		    mac_addr->state != FBNIC_TCAM_S_VALID) {
			eth_zero_addr(mac_addr->value.addr8);
			eth_broadcast_addr(mac_addr->mask.addr8);
			mac_addr->value.addr8[0] ^= 1;
			mac_addr->mask.addr8[0] ^= 1;
			set_bit(FBNIC_MAC_ADDR_T_ALLMULTI,
				mac_addr->act_tcam);
			clear_bit(FBNIC_MAC_ADDR_T_PROMISC,
				  mac_addr->act_tcam);
			mac_addr->state = FBNIC_TCAM_S_ADD;
		}
	} else if (mac_addr->state == FBNIC_TCAM_S_VALID) {
		if (test_bit(FBNIC_MAC_ADDR_T_BMC, mac_addr->act_tcam)) {
			clear_bit(FBNIC_MAC_ADDR_T_ALLMULTI,
				  mac_addr->act_tcam);
			clear_bit(FBNIC_MAC_ADDR_T_PROMISC,
				  mac_addr->act_tcam);
		} else {
			mac_addr->state = FBNIC_TCAM_S_DELETE;
		}
	}

	/* Add rules for BMC all multicast if it is enabled */
	fbnic_bmc_rpc_all_multi_config(fbd, mc_promisc);

	/* Sift out any unshared BMC rules and place them in BMC only section */
	fbnic_sift_macda(fbd);

	/* Write updates to hardware */
	fbnic_write_rules(fbd);
	fbnic_write_macda(fbd);
}

static void fbnic_set_rx_mode(struct net_device *netdev)
{
	/* No need to update the hardware if we are not running */
	if (netif_running(netdev))
		__fbnic_set_rx_mode(netdev);
}

static int fbnic_set_mac(struct net_device *netdev, void *p)
{
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(netdev, addr->sa_data);

	fbnic_set_rx_mode(netdev);

	return 0;
}

void fbnic_clear_rx_mode(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;
	int idx;

	for (idx = ARRAY_SIZE(fbd->mac_addr); idx--;) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[idx];

		if (mac_addr->state != FBNIC_TCAM_S_VALID)
			continue;

		bitmap_clear(mac_addr->act_tcam,
			     FBNIC_MAC_ADDR_T_HOST_START,
			     FBNIC_MAC_ADDR_T_HOST_LEN);

		if (bitmap_empty(mac_addr->act_tcam,
				 FBNIC_RPC_TCAM_ACT_NUM_ENTRIES))
			mac_addr->state = FBNIC_TCAM_S_DELETE;
	}

	/* Write updates to hardware */
	fbnic_write_macda(fbd);

	__dev_uc_unsync(netdev, NULL);
	__dev_mc_unsync(netdev, NULL);
}

static const struct net_device_ops fbnic_netdev_ops = {
	.ndo_open		= fbnic_open,
	.ndo_stop		= fbnic_stop,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_start_xmit		= fbnic_xmit_frame,
	.ndo_features_check	= fbnic_features_check,
	.ndo_set_mac_address	= fbnic_set_mac,
	.ndo_set_rx_mode	= fbnic_set_rx_mode,
};

void fbnic_reset_queues(struct fbnic_net *fbn,
			unsigned int tx, unsigned int rx)
{
	struct fbnic_dev *fbd = fbn->fbd;
	unsigned int max_napis;

	max_napis = fbd->num_irqs - FBNIC_NON_NAPI_VECTORS;

	tx = min(tx, max_napis);
	fbn->num_tx_queues = tx;

	rx = min(rx, max_napis);
	fbn->num_rx_queues = rx;

	fbn->num_napi = max(tx, rx);
}

/**
 * fbnic_netdev_free - Free the netdev associate with fbnic
 * @fbd: Driver specific structure to free netdev from
 *
 * Allocate and initialize the netdev and netdev private structure. Bind
 * together the hardware, netdev, and pci data structures.
 **/
void fbnic_netdev_free(struct fbnic_dev *fbd)
{
	struct fbnic_net *fbn = netdev_priv(fbd->netdev);

	if (fbn->phylink)
		phylink_destroy(fbn->phylink);

	free_netdev(fbd->netdev);
	fbd->netdev = NULL;
}

/**
 * fbnic_netdev_alloc - Allocate a netdev and associate with fbnic
 * @fbd: Driver specific structure to associate netdev with
 *
 * Allocate and initialize the netdev and netdev private structure. Bind
 * together the hardware, netdev, and pci data structures.
 *
 *  Return: 0 on success, negative on failure
 **/
struct net_device *fbnic_netdev_alloc(struct fbnic_dev *fbd)
{
	struct net_device *netdev;
	struct fbnic_net *fbn;
	int default_queues;

	netdev = alloc_etherdev_mq(sizeof(*fbn), FBNIC_MAX_RXQS);
	if (!netdev)
		return NULL;

	SET_NETDEV_DEV(netdev, fbd->dev);
	fbd->netdev = netdev;

	netdev->netdev_ops = &fbnic_netdev_ops;

	fbn = netdev_priv(netdev);

	fbn->netdev = netdev;
	fbn->fbd = fbd;
	INIT_LIST_HEAD(&fbn->napis);

	fbn->txq_size = FBNIC_TXQ_SIZE_DEFAULT;
	fbn->hpq_size = FBNIC_HPQ_SIZE_DEFAULT;
	fbn->ppq_size = FBNIC_PPQ_SIZE_DEFAULT;
	fbn->rcq_size = FBNIC_RCQ_SIZE_DEFAULT;

	default_queues = netif_get_num_default_rss_queues();
	if (default_queues > fbd->max_num_queues)
		default_queues = fbd->max_num_queues;

	fbnic_reset_queues(fbn, default_queues, default_queues);

	fbnic_reset_indir_tbl(fbn);
	fbnic_rss_key_fill(fbn->rss_key);
	fbnic_rss_init_en_mask(fbn);

	netdev->features |=
		NETIF_F_RXHASH |
		NETIF_F_SG |
		NETIF_F_HW_CSUM |
		NETIF_F_RXCSUM;

	netdev->hw_features |= netdev->features;
	netdev->vlan_features |= netdev->features;
	netdev->hw_enc_features |= netdev->features;

	netdev->min_mtu = IPV6_MIN_MTU;
	netdev->max_mtu = FBNIC_MAX_JUMBO_FRAME_SIZE - ETH_HLEN;

	/* TBD: This is workaround for BMC as phylink doesn't have support
	 * for leavling the link enabled if a BMC is present.
	 */
	netdev->ethtool->wol_enabled = true;

	fbn->fec = FBNIC_FEC_AUTO | FBNIC_FEC_RS;
	fbn->link_mode = FBNIC_LINK_AUTO | FBNIC_LINK_50R2;
	netif_carrier_off(netdev);

	netif_tx_stop_all_queues(netdev);

	if (fbnic_phylink_init(netdev)) {
		fbnic_netdev_free(fbd);
		return NULL;
	}

	return netdev;
}

static int fbnic_dsn_to_mac_addr(u64 dsn, char *addr)
{
	addr[0] = (dsn >> 56) & 0xFF;
	addr[1] = (dsn >> 48) & 0xFF;
	addr[2] = (dsn >> 40) & 0xFF;
	addr[3] = (dsn >> 16) & 0xFF;
	addr[4] = (dsn >> 8) & 0xFF;
	addr[5] = dsn & 0xFF;

	return is_valid_ether_addr(addr) ? 0 : -EINVAL;
}

/**
 * fbnic_netdev_register - Initialize general software structures
 * @netdev: Netdev containing structure to initialize and register
 *
 * Initialize the MAC address for the netdev and register it.
 *
 *  Return: 0 on success, negative on failure
 **/
int fbnic_netdev_register(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;
	u64 dsn = fbd->dsn;
	u8 addr[ETH_ALEN];
	int err;

	err = fbnic_dsn_to_mac_addr(dsn, addr);
	if (!err) {
		ether_addr_copy(netdev->perm_addr, addr);
		eth_hw_addr_set(netdev, addr);
	} else {
		/* A randomly assigned MAC address will cause provisioning
		 * issues so instead just fail to spawn the netdev and
		 * avoid any confusion.
		 */
		dev_err(fbd->dev, "MAC addr %pM invalid\n", addr);
		return err;
	}

	return register_netdev(netdev);
}

void fbnic_netdev_unregister(struct net_device *netdev)
{
	unregister_netdev(netdev);
}
