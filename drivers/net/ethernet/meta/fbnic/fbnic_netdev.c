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

static const struct net_device_ops fbnic_netdev_ops = {
	.ndo_open		= fbnic_open,
	.ndo_stop		= fbnic_stop,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_start_xmit		= fbnic_xmit_frame,
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
