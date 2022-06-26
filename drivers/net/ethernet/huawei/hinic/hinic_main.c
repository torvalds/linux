// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/if_vlan.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <net/ip.h>
#include <net/devlink.h>
#include <linux/bitops.h>
#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/err.h>

#include "hinic_debugfs.h"
#include "hinic_hw_qp.h"
#include "hinic_hw_dev.h"
#include "hinic_devlink.h"
#include "hinic_port.h"
#include "hinic_tx.h"
#include "hinic_rx.h"
#include "hinic_dev.h"
#include "hinic_sriov.h"

MODULE_AUTHOR("Huawei Technologies CO., Ltd");
MODULE_DESCRIPTION("Huawei Intelligent NIC driver");
MODULE_LICENSE("GPL");

static unsigned int tx_weight = 64;
module_param(tx_weight, uint, 0644);
MODULE_PARM_DESC(tx_weight, "Number Tx packets for NAPI budget (default=64)");

static unsigned int rx_weight = 64;
module_param(rx_weight, uint, 0644);
MODULE_PARM_DESC(rx_weight, "Number Rx packets for NAPI budget (default=64)");

#define HINIC_DEV_ID_QUAD_PORT_25GE         0x1822
#define HINIC_DEV_ID_DUAL_PORT_100GE        0x0200
#define HINIC_DEV_ID_DUAL_PORT_100GE_MEZZ   0x0205
#define HINIC_DEV_ID_QUAD_PORT_25GE_MEZZ    0x0210
#define HINIC_DEV_ID_VF    0x375e

#define HINIC_WQ_NAME                   "hinic_dev"

#define MSG_ENABLE_DEFAULT              (NETIF_MSG_DRV | NETIF_MSG_PROBE | \
					 NETIF_MSG_IFUP |                  \
					 NETIF_MSG_TX_ERR | NETIF_MSG_RX_ERR)

#define HINIC_LRO_MAX_WQE_NUM_DEFAULT	8

#define HINIC_LRO_RX_TIMER_DEFAULT	16

#define work_to_rx_mode_work(work)      \
		container_of(work, struct hinic_rx_mode_work, work)

#define rx_mode_work_to_nic_dev(rx_mode_work) \
		container_of(rx_mode_work, struct hinic_dev, rx_mode_work)

#define HINIC_WAIT_SRIOV_CFG_TIMEOUT	15000

#define HINIC_DEAULT_TXRX_MSIX_PENDING_LIMIT		2
#define HINIC_DEAULT_TXRX_MSIX_COALESC_TIMER_CFG	32
#define HINIC_DEAULT_TXRX_MSIX_RESEND_TIMER_CFG		7

static int change_mac_addr(struct net_device *netdev, const u8 *addr);

static int set_features(struct hinic_dev *nic_dev,
			netdev_features_t pre_features,
			netdev_features_t features, bool force_change);

static void update_rx_stats(struct hinic_dev *nic_dev, struct hinic_rxq *rxq)
{
	struct hinic_rxq_stats *nic_rx_stats = &nic_dev->rx_stats;
	struct hinic_rxq_stats rx_stats;

	u64_stats_init(&rx_stats.syncp);

	hinic_rxq_get_stats(rxq, &rx_stats);

	u64_stats_update_begin(&nic_rx_stats->syncp);
	nic_rx_stats->bytes += rx_stats.bytes;
	nic_rx_stats->pkts  += rx_stats.pkts;
	nic_rx_stats->errors += rx_stats.errors;
	nic_rx_stats->csum_errors += rx_stats.csum_errors;
	nic_rx_stats->other_errors += rx_stats.other_errors;
	u64_stats_update_end(&nic_rx_stats->syncp);

	hinic_rxq_clean_stats(rxq);
}

static void update_tx_stats(struct hinic_dev *nic_dev, struct hinic_txq *txq)
{
	struct hinic_txq_stats *nic_tx_stats = &nic_dev->tx_stats;
	struct hinic_txq_stats tx_stats;

	u64_stats_init(&tx_stats.syncp);

	hinic_txq_get_stats(txq, &tx_stats);

	u64_stats_update_begin(&nic_tx_stats->syncp);
	nic_tx_stats->bytes += tx_stats.bytes;
	nic_tx_stats->pkts += tx_stats.pkts;
	nic_tx_stats->tx_busy += tx_stats.tx_busy;
	nic_tx_stats->tx_wake += tx_stats.tx_wake;
	nic_tx_stats->tx_dropped += tx_stats.tx_dropped;
	nic_tx_stats->big_frags_pkts += tx_stats.big_frags_pkts;
	u64_stats_update_end(&nic_tx_stats->syncp);

	hinic_txq_clean_stats(txq);
}

static void update_nic_stats(struct hinic_dev *nic_dev)
{
	int i, num_qps = hinic_hwdev_num_qps(nic_dev->hwdev);

	for (i = 0; i < num_qps; i++)
		update_rx_stats(nic_dev, &nic_dev->rxqs[i]);

	for (i = 0; i < num_qps; i++)
		update_tx_stats(nic_dev, &nic_dev->txqs[i]);
}

/**
 * create_txqs - Create the Logical Tx Queues of specific NIC device
 * @nic_dev: the specific NIC device
 *
 * Return 0 - Success, negative - Failure
 **/
static int create_txqs(struct hinic_dev *nic_dev)
{
	int err, i, j, num_txqs = hinic_hwdev_num_qps(nic_dev->hwdev);
	struct net_device *netdev = nic_dev->netdev;

	if (nic_dev->txqs)
		return -EINVAL;

	nic_dev->txqs = devm_kcalloc(&netdev->dev, num_txqs,
				     sizeof(*nic_dev->txqs), GFP_KERNEL);
	if (!nic_dev->txqs)
		return -ENOMEM;

	hinic_sq_dbgfs_init(nic_dev);

	for (i = 0; i < num_txqs; i++) {
		struct hinic_sq *sq = hinic_hwdev_get_sq(nic_dev->hwdev, i);

		err = hinic_init_txq(&nic_dev->txqs[i], sq, netdev);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to init Txq\n");
			goto err_init_txq;
		}

		err = hinic_sq_debug_add(nic_dev, i);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to add SQ%d debug\n", i);
			goto err_add_sq_dbg;
		}

	}

	return 0;

err_add_sq_dbg:
	hinic_clean_txq(&nic_dev->txqs[i]);
err_init_txq:
	for (j = 0; j < i; j++) {
		hinic_sq_debug_rem(nic_dev->txqs[j].sq);
		hinic_clean_txq(&nic_dev->txqs[j]);
	}

	hinic_sq_dbgfs_uninit(nic_dev);

	devm_kfree(&netdev->dev, nic_dev->txqs);
	return err;
}

static void enable_txqs_napi(struct hinic_dev *nic_dev)
{
	int num_txqs = hinic_hwdev_num_qps(nic_dev->hwdev);
	int i;

	for (i = 0; i < num_txqs; i++)
		napi_enable(&nic_dev->txqs[i].napi);
}

static void disable_txqs_napi(struct hinic_dev *nic_dev)
{
	int num_txqs = hinic_hwdev_num_qps(nic_dev->hwdev);
	int i;

	for (i = 0; i < num_txqs; i++)
		napi_disable(&nic_dev->txqs[i].napi);
}

/**
 * free_txqs - Free the Logical Tx Queues of specific NIC device
 * @nic_dev: the specific NIC device
 **/
static void free_txqs(struct hinic_dev *nic_dev)
{
	int i, num_txqs = hinic_hwdev_num_qps(nic_dev->hwdev);
	struct net_device *netdev = nic_dev->netdev;

	if (!nic_dev->txqs)
		return;

	for (i = 0; i < num_txqs; i++) {
		hinic_sq_debug_rem(nic_dev->txqs[i].sq);
		hinic_clean_txq(&nic_dev->txqs[i]);
	}

	hinic_sq_dbgfs_uninit(nic_dev);

	devm_kfree(&netdev->dev, nic_dev->txqs);
	nic_dev->txqs = NULL;
}

/**
 * create_txqs - Create the Logical Rx Queues of specific NIC device
 * @nic_dev: the specific NIC device
 *
 * Return 0 - Success, negative - Failure
 **/
static int create_rxqs(struct hinic_dev *nic_dev)
{
	int err, i, j, num_rxqs = hinic_hwdev_num_qps(nic_dev->hwdev);
	struct net_device *netdev = nic_dev->netdev;

	if (nic_dev->rxqs)
		return -EINVAL;

	nic_dev->rxqs = devm_kcalloc(&netdev->dev, num_rxqs,
				     sizeof(*nic_dev->rxqs), GFP_KERNEL);
	if (!nic_dev->rxqs)
		return -ENOMEM;

	hinic_rq_dbgfs_init(nic_dev);

	for (i = 0; i < num_rxqs; i++) {
		struct hinic_rq *rq = hinic_hwdev_get_rq(nic_dev->hwdev, i);

		err = hinic_init_rxq(&nic_dev->rxqs[i], rq, netdev);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to init rxq\n");
			goto err_init_rxq;
		}

		err = hinic_rq_debug_add(nic_dev, i);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to add RQ%d debug\n", i);
			goto err_add_rq_dbg;
		}
	}

	return 0;

err_add_rq_dbg:
	hinic_clean_rxq(&nic_dev->rxqs[i]);
err_init_rxq:
	for (j = 0; j < i; j++) {
		hinic_rq_debug_rem(nic_dev->rxqs[j].rq);
		hinic_clean_rxq(&nic_dev->rxqs[j]);
	}

	hinic_rq_dbgfs_uninit(nic_dev);

	devm_kfree(&netdev->dev, nic_dev->rxqs);
	return err;
}

/**
 * free_txqs - Free the Logical Rx Queues of specific NIC device
 * @nic_dev: the specific NIC device
 **/
static void free_rxqs(struct hinic_dev *nic_dev)
{
	int i, num_rxqs = hinic_hwdev_num_qps(nic_dev->hwdev);
	struct net_device *netdev = nic_dev->netdev;

	if (!nic_dev->rxqs)
		return;

	for (i = 0; i < num_rxqs; i++) {
		hinic_rq_debug_rem(nic_dev->rxqs[i].rq);
		hinic_clean_rxq(&nic_dev->rxqs[i]);
	}

	hinic_rq_dbgfs_uninit(nic_dev);

	devm_kfree(&netdev->dev, nic_dev->rxqs);
	nic_dev->rxqs = NULL;
}

static int hinic_configure_max_qnum(struct hinic_dev *nic_dev)
{
	int err;

	err = hinic_set_max_qnum(nic_dev, nic_dev->hwdev->nic_cap.max_qps);
	if (err)
		return err;

	return 0;
}

static int hinic_rss_init(struct hinic_dev *nic_dev)
{
	u8 default_rss_key[HINIC_RSS_KEY_SIZE];
	u8 tmpl_idx = nic_dev->rss_tmpl_idx;
	u32 *indir_tbl;
	int err, i;

	indir_tbl = kcalloc(HINIC_RSS_INDIR_SIZE, sizeof(u32), GFP_KERNEL);
	if (!indir_tbl)
		return -ENOMEM;

	netdev_rss_key_fill(default_rss_key, sizeof(default_rss_key));
	for (i = 0; i < HINIC_RSS_INDIR_SIZE; i++)
		indir_tbl[i] = ethtool_rxfh_indir_default(i, nic_dev->num_rss);

	err = hinic_rss_set_template_tbl(nic_dev, tmpl_idx, default_rss_key);
	if (err)
		goto out;

	err = hinic_rss_set_indir_tbl(nic_dev, tmpl_idx, indir_tbl);
	if (err)
		goto out;

	err = hinic_set_rss_type(nic_dev, tmpl_idx, nic_dev->rss_type);
	if (err)
		goto out;

	err = hinic_rss_set_hash_engine(nic_dev, tmpl_idx,
					nic_dev->rss_hash_engine);
	if (err)
		goto out;

	err = hinic_rss_cfg(nic_dev, 1, tmpl_idx);
	if (err)
		goto out;

out:
	kfree(indir_tbl);
	return err;
}

static void hinic_rss_deinit(struct hinic_dev *nic_dev)
{
	hinic_rss_cfg(nic_dev, 0, nic_dev->rss_tmpl_idx);
}

static void hinic_init_rss_parameters(struct hinic_dev *nic_dev)
{
	nic_dev->rss_hash_engine = HINIC_RSS_HASH_ENGINE_TYPE_XOR;
	nic_dev->rss_type.tcp_ipv6_ext = 1;
	nic_dev->rss_type.ipv6_ext = 1;
	nic_dev->rss_type.tcp_ipv6 = 1;
	nic_dev->rss_type.ipv6 = 1;
	nic_dev->rss_type.tcp_ipv4 = 1;
	nic_dev->rss_type.ipv4 = 1;
	nic_dev->rss_type.udp_ipv6 = 1;
	nic_dev->rss_type.udp_ipv4 = 1;
}

static void hinic_enable_rss(struct hinic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int i, node, err = 0;
	u16 num_cpus = 0;

	if (nic_dev->max_qps <= 1) {
		nic_dev->flags &= ~HINIC_RSS_ENABLE;
		nic_dev->rss_limit = nic_dev->max_qps;
		nic_dev->num_qps = nic_dev->max_qps;
		nic_dev->num_rss = nic_dev->max_qps;

		return;
	}

	err = hinic_rss_template_alloc(nic_dev, &nic_dev->rss_tmpl_idx);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to alloc tmpl_idx for rss, can't enable rss for this function\n");
		nic_dev->flags &= ~HINIC_RSS_ENABLE;
		nic_dev->max_qps = 1;
		nic_dev->rss_limit = nic_dev->max_qps;
		nic_dev->num_qps = nic_dev->max_qps;
		nic_dev->num_rss = nic_dev->max_qps;

		return;
	}

	nic_dev->flags |= HINIC_RSS_ENABLE;

	for (i = 0; i < num_online_cpus(); i++) {
		node = cpu_to_node(i);
		if (node == dev_to_node(&pdev->dev))
			num_cpus++;
	}

	if (!num_cpus)
		num_cpus = num_online_cpus();

	nic_dev->num_qps = hinic_hwdev_num_qps(hwdev);
	nic_dev->num_qps = min_t(u16, nic_dev->num_qps, num_cpus);

	nic_dev->rss_limit = nic_dev->num_qps;
	nic_dev->num_rss = nic_dev->num_qps;

	hinic_init_rss_parameters(nic_dev);
	err = hinic_rss_init(nic_dev);
	if (err)
		netif_err(nic_dev, drv, netdev, "Failed to init rss\n");
}

int hinic_open(struct net_device *netdev)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	enum hinic_port_link_state link_state;
	int err, ret;

	if (!(nic_dev->flags & HINIC_INTF_UP)) {
		err = hinic_hwdev_ifup(nic_dev->hwdev, nic_dev->sq_depth,
				       nic_dev->rq_depth);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed - HW interface up\n");
			return err;
		}
	}

	err = create_txqs(nic_dev);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to create Tx queues\n");
		goto err_create_txqs;
	}

	enable_txqs_napi(nic_dev);

	err = create_rxqs(nic_dev);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to create Rx queues\n");
		goto err_create_rxqs;
	}

	hinic_enable_rss(nic_dev);

	err = hinic_configure_max_qnum(nic_dev);
	if (err) {
		netif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to configure the maximum number of queues\n");
		goto err_port_state;
	}

	netif_set_real_num_tx_queues(netdev, nic_dev->num_qps);
	netif_set_real_num_rx_queues(netdev, nic_dev->num_qps);

	err = hinic_port_set_state(nic_dev, HINIC_PORT_ENABLE);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to set port state\n");
		goto err_port_state;
	}

	err = hinic_port_set_func_state(nic_dev, HINIC_FUNC_PORT_ENABLE);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to set func port state\n");
		goto err_func_port_state;
	}

	down(&nic_dev->mgmt_lock);

	err = hinic_port_link_state(nic_dev, &link_state);
	if (err) {
		netif_err(nic_dev, drv, netdev, "Failed to get link state\n");
		goto err_port_link;
	}

	if (!HINIC_IS_VF(nic_dev->hwdev->hwif))
		hinic_notify_all_vfs_link_changed(nic_dev->hwdev, link_state);

	if (link_state == HINIC_LINK_STATE_UP) {
		nic_dev->flags |= HINIC_LINK_UP;
		nic_dev->cable_unplugged = false;
		nic_dev->module_unrecognized = false;
	}

	nic_dev->flags |= HINIC_INTF_UP;

	if ((nic_dev->flags & (HINIC_LINK_UP | HINIC_INTF_UP)) ==
	    (HINIC_LINK_UP | HINIC_INTF_UP)) {
		netif_info(nic_dev, drv, netdev, "link + intf UP\n");
		netif_carrier_on(netdev);
		netif_tx_wake_all_queues(netdev);
	}

	up(&nic_dev->mgmt_lock);

	netif_info(nic_dev, drv, netdev, "HINIC_INTF is UP\n");
	return 0;

err_port_link:
	up(&nic_dev->mgmt_lock);
	ret = hinic_port_set_func_state(nic_dev, HINIC_FUNC_PORT_DISABLE);
	if (ret)
		netif_warn(nic_dev, drv, netdev,
			   "Failed to revert func port state\n");

err_func_port_state:
	ret = hinic_port_set_state(nic_dev, HINIC_PORT_DISABLE);
	if (ret)
		netif_warn(nic_dev, drv, netdev,
			   "Failed to revert port state\n");
err_port_state:
	free_rxqs(nic_dev);
	if (nic_dev->flags & HINIC_RSS_ENABLE) {
		hinic_rss_deinit(nic_dev);
		hinic_rss_template_free(nic_dev, nic_dev->rss_tmpl_idx);
	}

err_create_rxqs:
	disable_txqs_napi(nic_dev);
	free_txqs(nic_dev);

err_create_txqs:
	if (!(nic_dev->flags & HINIC_INTF_UP))
		hinic_hwdev_ifdown(nic_dev->hwdev);
	return err;
}

int hinic_close(struct net_device *netdev)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	unsigned int flags;

	/* Disable txq napi firstly to aviod rewaking txq in free_tx_poll */
	disable_txqs_napi(nic_dev);

	down(&nic_dev->mgmt_lock);

	flags = nic_dev->flags;
	nic_dev->flags &= ~HINIC_INTF_UP;

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	update_nic_stats(nic_dev);

	up(&nic_dev->mgmt_lock);

	if (!HINIC_IS_VF(nic_dev->hwdev->hwif))
		hinic_notify_all_vfs_link_changed(nic_dev->hwdev, 0);

	hinic_port_set_state(nic_dev, HINIC_PORT_DISABLE);

	hinic_port_set_func_state(nic_dev, HINIC_FUNC_PORT_DISABLE);

	if (nic_dev->flags & HINIC_RSS_ENABLE) {
		hinic_rss_deinit(nic_dev);
		hinic_rss_template_free(nic_dev, nic_dev->rss_tmpl_idx);
	}

	free_rxqs(nic_dev);
	free_txqs(nic_dev);

	if (flags & HINIC_INTF_UP)
		hinic_hwdev_ifdown(nic_dev->hwdev);

	netif_info(nic_dev, drv, netdev, "HINIC_INTF is DOWN\n");
	return 0;
}

static int hinic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err;

	netif_info(nic_dev, drv, netdev, "set_mtu = %d\n", new_mtu);

	err = hinic_port_set_mtu(nic_dev, new_mtu);
	if (err)
		netif_err(nic_dev, drv, netdev, "Failed to set port mtu\n");
	else
		netdev->mtu = new_mtu;

	return err;
}

/**
 * change_mac_addr - change the main mac address of network device
 * @netdev: network device
 * @addr: mac address to set
 *
 * Return 0 - Success, negative - Failure
 **/
static int change_mac_addr(struct net_device *netdev, const u8 *addr)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u16 vid = 0;
	int err;

	if (!is_valid_ether_addr(addr))
		return -EADDRNOTAVAIL;

	netif_info(nic_dev, drv, netdev, "change mac addr = %02x %02x %02x %02x %02x %02x\n",
		   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	down(&nic_dev->mgmt_lock);

	do {
		err = hinic_port_del_mac(nic_dev, netdev->dev_addr, vid);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to delete mac\n");
			break;
		}

		err = hinic_port_add_mac(nic_dev, addr, vid);
		if (err) {
			netif_err(nic_dev, drv, netdev, "Failed to add mac\n");
			break;
		}

		vid = find_next_bit(nic_dev->vlan_bitmap, VLAN_N_VID, vid + 1);
	} while (vid != VLAN_N_VID);

	up(&nic_dev->mgmt_lock);
	return err;
}

static int hinic_set_mac_addr(struct net_device *netdev, void *addr)
{
	unsigned char new_mac[ETH_ALEN];
	struct sockaddr *saddr = addr;
	int err;

	memcpy(new_mac, saddr->sa_data, ETH_ALEN);

	err = change_mac_addr(netdev, new_mac);
	if (!err)
		memcpy(netdev->dev_addr, new_mac, ETH_ALEN);

	return err;
}

/**
 * add_mac_addr - add mac address to network device
 * @netdev: network device
 * @addr: mac address to add
 *
 * Return 0 - Success, negative - Failure
 **/
static int add_mac_addr(struct net_device *netdev, const u8 *addr)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u16 vid = 0;
	int err;

	netif_info(nic_dev, drv, netdev, "set mac addr = %02x %02x %02x %02x %02x %02x\n",
		   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	down(&nic_dev->mgmt_lock);

	do {
		err = hinic_port_add_mac(nic_dev, addr, vid);
		if (err) {
			netif_err(nic_dev, drv, netdev, "Failed to add mac\n");
			break;
		}

		vid = find_next_bit(nic_dev->vlan_bitmap, VLAN_N_VID, vid + 1);
	} while (vid != VLAN_N_VID);

	up(&nic_dev->mgmt_lock);
	return err;
}

/**
 * remove_mac_addr - remove mac address from network device
 * @netdev: network device
 * @addr: mac address to remove
 *
 * Return 0 - Success, negative - Failure
 **/
static int remove_mac_addr(struct net_device *netdev, const u8 *addr)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u16 vid = 0;
	int err;

	if (!is_valid_ether_addr(addr))
		return -EADDRNOTAVAIL;

	netif_info(nic_dev, drv, netdev, "remove mac addr = %02x %02x %02x %02x %02x %02x\n",
		   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	down(&nic_dev->mgmt_lock);

	do {
		err = hinic_port_del_mac(nic_dev, addr, vid);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to delete mac\n");
			break;
		}

		vid = find_next_bit(nic_dev->vlan_bitmap, VLAN_N_VID, vid + 1);
	} while (vid != VLAN_N_VID);

	up(&nic_dev->mgmt_lock);
	return err;
}

static int hinic_vlan_rx_add_vid(struct net_device *netdev,
				 __always_unused __be16 proto, u16 vid)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int ret, err;

	netif_info(nic_dev, drv, netdev, "add vid = %d\n", vid);

	down(&nic_dev->mgmt_lock);

	err = hinic_port_add_vlan(nic_dev, vid);
	if (err) {
		netif_err(nic_dev, drv, netdev, "Failed to add vlan\n");
		goto err_vlan_add;
	}

	err = hinic_port_add_mac(nic_dev, netdev->dev_addr, vid);
	if (err && err != HINIC_PF_SET_VF_ALREADY) {
		netif_err(nic_dev, drv, netdev, "Failed to set mac\n");
		goto err_add_mac;
	}

	bitmap_set(nic_dev->vlan_bitmap, vid, 1);

	up(&nic_dev->mgmt_lock);
	return 0;

err_add_mac:
	ret = hinic_port_del_vlan(nic_dev, vid);
	if (ret)
		netif_err(nic_dev, drv, netdev,
			  "Failed to revert by removing vlan\n");

err_vlan_add:
	up(&nic_dev->mgmt_lock);
	return err;
}

static int hinic_vlan_rx_kill_vid(struct net_device *netdev,
				  __always_unused __be16 proto, u16 vid)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err;

	netif_info(nic_dev, drv, netdev, "remove vid = %d\n", vid);

	down(&nic_dev->mgmt_lock);

	err = hinic_port_del_vlan(nic_dev, vid);
	if (err) {
		netif_err(nic_dev, drv, netdev, "Failed to delete vlan\n");
		goto err_del_vlan;
	}

	bitmap_clear(nic_dev->vlan_bitmap, vid, 1);

	up(&nic_dev->mgmt_lock);
	return 0;

err_del_vlan:
	up(&nic_dev->mgmt_lock);
	return err;
}

static void set_rx_mode(struct work_struct *work)
{
	struct hinic_rx_mode_work *rx_mode_work = work_to_rx_mode_work(work);
	struct hinic_dev *nic_dev = rx_mode_work_to_nic_dev(rx_mode_work);

	hinic_port_set_rx_mode(nic_dev, rx_mode_work->rx_mode);

	__dev_uc_sync(nic_dev->netdev, add_mac_addr, remove_mac_addr);
	__dev_mc_sync(nic_dev->netdev, add_mac_addr, remove_mac_addr);
}

static void hinic_set_rx_mode(struct net_device *netdev)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_rx_mode_work *rx_mode_work;
	u32 rx_mode;

	rx_mode_work = &nic_dev->rx_mode_work;

	rx_mode = HINIC_RX_MODE_UC |
		  HINIC_RX_MODE_MC |
		  HINIC_RX_MODE_BC;

	if (netdev->flags & IFF_PROMISC) {
		if (!HINIC_IS_VF(nic_dev->hwdev->hwif))
			rx_mode |= HINIC_RX_MODE_PROMISC;
	} else if (netdev->flags & IFF_ALLMULTI) {
		rx_mode |= HINIC_RX_MODE_MC_ALL;
	}

	rx_mode_work->rx_mode = rx_mode;

	queue_work(nic_dev->workq, &rx_mode_work->work);
}

static void hinic_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u16 sw_pi, hw_ci, sw_ci;
	struct hinic_sq *sq;
	u16 num_sqs, q_id;

	num_sqs = hinic_hwdev_num_qps(nic_dev->hwdev);

	netif_err(nic_dev, drv, netdev, "Tx timeout\n");

	for (q_id = 0; q_id < num_sqs; q_id++) {
		if (!netif_xmit_stopped(netdev_get_tx_queue(netdev, q_id)))
			continue;

		sq = hinic_hwdev_get_sq(nic_dev->hwdev, q_id);
		sw_pi = atomic_read(&sq->wq->prod_idx) & sq->wq->mask;
		hw_ci = be16_to_cpu(*(u16 *)(sq->hw_ci_addr)) & sq->wq->mask;
		sw_ci = atomic_read(&sq->wq->cons_idx) & sq->wq->mask;
		netif_err(nic_dev, drv, netdev, "Txq%d: sw_pi: %d, hw_ci: %d, sw_ci: %d, napi->state: 0x%lx\n",
			  q_id, sw_pi, hw_ci, sw_ci,
			  nic_dev->txqs[q_id].napi.state);
	}
}

static void hinic_get_stats64(struct net_device *netdev,
			      struct rtnl_link_stats64 *stats)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_rxq_stats *nic_rx_stats;
	struct hinic_txq_stats *nic_tx_stats;

	nic_rx_stats = &nic_dev->rx_stats;
	nic_tx_stats = &nic_dev->tx_stats;

	down(&nic_dev->mgmt_lock);

	if (nic_dev->flags & HINIC_INTF_UP)
		update_nic_stats(nic_dev);

	up(&nic_dev->mgmt_lock);

	stats->rx_bytes   = nic_rx_stats->bytes;
	stats->rx_packets = nic_rx_stats->pkts;
	stats->rx_errors  = nic_rx_stats->errors;

	stats->tx_bytes   = nic_tx_stats->bytes;
	stats->tx_packets = nic_tx_stats->pkts;
	stats->tx_errors  = nic_tx_stats->tx_dropped;
}

static int hinic_set_features(struct net_device *netdev,
			      netdev_features_t features)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);

	return set_features(nic_dev, nic_dev->netdev->features,
			    features, false);
}

static netdev_features_t hinic_fix_features(struct net_device *netdev,
					    netdev_features_t features)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);

	/* If Rx checksum is disabled, then LRO should also be disabled */
	if (!(features & NETIF_F_RXCSUM)) {
		netif_info(nic_dev, drv, netdev, "disabling LRO as RXCSUM is off\n");
		features &= ~NETIF_F_LRO;
	}

	return features;
}

static const struct net_device_ops hinic_netdev_ops = {
	.ndo_open = hinic_open,
	.ndo_stop = hinic_close,
	.ndo_change_mtu = hinic_change_mtu,
	.ndo_set_mac_address = hinic_set_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_vlan_rx_add_vid = hinic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = hinic_vlan_rx_kill_vid,
	.ndo_set_rx_mode = hinic_set_rx_mode,
	.ndo_start_xmit = hinic_xmit_frame,
	.ndo_tx_timeout = hinic_tx_timeout,
	.ndo_get_stats64 = hinic_get_stats64,
	.ndo_fix_features = hinic_fix_features,
	.ndo_set_features = hinic_set_features,
	.ndo_set_vf_mac	= hinic_ndo_set_vf_mac,
	.ndo_set_vf_vlan = hinic_ndo_set_vf_vlan,
	.ndo_get_vf_config = hinic_ndo_get_vf_config,
	.ndo_set_vf_trust = hinic_ndo_set_vf_trust,
	.ndo_set_vf_rate = hinic_ndo_set_vf_bw,
	.ndo_set_vf_spoofchk = hinic_ndo_set_vf_spoofchk,
	.ndo_set_vf_link_state = hinic_ndo_set_vf_link_state,
};

static const struct net_device_ops hinicvf_netdev_ops = {
	.ndo_open = hinic_open,
	.ndo_stop = hinic_close,
	.ndo_change_mtu = hinic_change_mtu,
	.ndo_set_mac_address = hinic_set_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_vlan_rx_add_vid = hinic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = hinic_vlan_rx_kill_vid,
	.ndo_set_rx_mode = hinic_set_rx_mode,
	.ndo_start_xmit = hinic_xmit_frame,
	.ndo_tx_timeout = hinic_tx_timeout,
	.ndo_get_stats64 = hinic_get_stats64,
	.ndo_fix_features = hinic_fix_features,
	.ndo_set_features = hinic_set_features,
};

static void netdev_features_init(struct net_device *netdev)
{
	netdev->hw_features = NETIF_F_SG | NETIF_F_HIGHDMA | NETIF_F_IP_CSUM |
			      NETIF_F_IPV6_CSUM | NETIF_F_TSO | NETIF_F_TSO6 |
			      NETIF_F_RXCSUM | NETIF_F_LRO |
			      NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
			      NETIF_F_GSO_UDP_TUNNEL | NETIF_F_GSO_UDP_TUNNEL_CSUM;

	netdev->vlan_features = netdev->hw_features;

	netdev->features = netdev->hw_features | NETIF_F_HW_VLAN_CTAG_FILTER;

	netdev->hw_enc_features = NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_SCTP_CRC |
				  NETIF_F_SG | NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_TSO_ECN |
				  NETIF_F_GSO_UDP_TUNNEL_CSUM | NETIF_F_GSO_UDP_TUNNEL;
}

static void hinic_refresh_nic_cfg(struct hinic_dev *nic_dev)
{
	struct hinic_nic_cfg *nic_cfg = &nic_dev->hwdev->func_to_io.nic_cfg;
	struct hinic_pause_config pause_info = {0};
	struct hinic_port_cap port_cap = {0};

	if (hinic_port_get_cap(nic_dev, &port_cap))
		return;

	mutex_lock(&nic_cfg->cfg_mutex);
	if (nic_cfg->pause_set || !port_cap.autoneg_state) {
		nic_cfg->auto_neg = port_cap.autoneg_state;
		pause_info.auto_neg = nic_cfg->auto_neg;
		pause_info.rx_pause = nic_cfg->rx_pause;
		pause_info.tx_pause = nic_cfg->tx_pause;
		hinic_set_hw_pause_info(nic_dev->hwdev, &pause_info);
	}
	mutex_unlock(&nic_cfg->cfg_mutex);
}

/**
 * link_status_event_handler - link event handler
 * @handle: nic device for the handler
 * @buf_in: input buffer
 * @in_size: input size
 * @buf_out: output buffer
 * @out_size: returned output size
 *
 * Return 0 - Success, negative - Failure
 **/
static void link_status_event_handler(void *handle, void *buf_in, u16 in_size,
				      void *buf_out, u16 *out_size)
{
	struct hinic_port_link_status *link_status, *ret_link_status;
	struct hinic_dev *nic_dev = handle;

	link_status = buf_in;

	if (link_status->link == HINIC_LINK_STATE_UP) {
		down(&nic_dev->mgmt_lock);

		nic_dev->flags |= HINIC_LINK_UP;
		nic_dev->cable_unplugged = false;
		nic_dev->module_unrecognized = false;

		if ((nic_dev->flags & (HINIC_LINK_UP | HINIC_INTF_UP)) ==
		    (HINIC_LINK_UP | HINIC_INTF_UP)) {
			netif_carrier_on(nic_dev->netdev);
			netif_tx_wake_all_queues(nic_dev->netdev);
		}

		up(&nic_dev->mgmt_lock);

		if (!HINIC_IS_VF(nic_dev->hwdev->hwif))
			hinic_refresh_nic_cfg(nic_dev);

		netif_info(nic_dev, drv, nic_dev->netdev, "HINIC_Link is UP\n");
	} else {
		down(&nic_dev->mgmt_lock);

		nic_dev->flags &= ~HINIC_LINK_UP;

		netif_carrier_off(nic_dev->netdev);
		netif_tx_disable(nic_dev->netdev);

		up(&nic_dev->mgmt_lock);

		netif_info(nic_dev, drv, nic_dev->netdev, "HINIC_Link is DOWN\n");
	}

	if (!HINIC_IS_VF(nic_dev->hwdev->hwif))
		hinic_notify_all_vfs_link_changed(nic_dev->hwdev,
						  link_status->link);

	ret_link_status = buf_out;
	ret_link_status->status = 0;

	*out_size = sizeof(*ret_link_status);
}

static void cable_plug_event(void *handle,
			     void *buf_in, u16 in_size,
			     void *buf_out, u16 *out_size)
{
	struct hinic_cable_plug_event *plug_event = buf_in;
	struct hinic_dev *nic_dev = handle;

	nic_dev->cable_unplugged = plug_event->plugged ? false : true;

	*out_size = sizeof(*plug_event);
	plug_event = buf_out;
	plug_event->status = 0;
}

static void link_err_event(void *handle,
			   void *buf_in, u16 in_size,
			   void *buf_out, u16 *out_size)
{
	struct hinic_link_err_event *link_err = buf_in;
	struct hinic_dev *nic_dev = handle;

	if (link_err->err_type >= LINK_ERR_NUM)
		netif_info(nic_dev, link, nic_dev->netdev,
			   "Link failed, Unknown error type: 0x%x\n",
			   link_err->err_type);
	else
		nic_dev->module_unrecognized = true;

	*out_size = sizeof(*link_err);
	link_err = buf_out;
	link_err->status = 0;
}

static int set_features(struct hinic_dev *nic_dev,
			netdev_features_t pre_features,
			netdev_features_t features, bool force_change)
{
	netdev_features_t changed = force_change ? ~0 : pre_features ^ features;
	u32 csum_en = HINIC_RX_CSUM_OFFLOAD_EN;
	netdev_features_t failed_features = 0;
	int ret = 0;
	int err = 0;

	if (changed & NETIF_F_TSO) {
		ret = hinic_port_set_tso(nic_dev, (features & NETIF_F_TSO) ?
					 HINIC_TSO_ENABLE : HINIC_TSO_DISABLE);
		if (ret) {
			err = ret;
			failed_features |= NETIF_F_TSO;
		}
	}

	if (changed & NETIF_F_RXCSUM) {
		ret = hinic_set_rx_csum_offload(nic_dev, csum_en);
		if (ret) {
			err = ret;
			failed_features |= NETIF_F_RXCSUM;
		}
	}

	if (changed & NETIF_F_LRO) {
		ret = hinic_set_rx_lro_state(nic_dev,
					     !!(features & NETIF_F_LRO),
					     HINIC_LRO_RX_TIMER_DEFAULT,
					     HINIC_LRO_MAX_WQE_NUM_DEFAULT);
		if (ret) {
			err = ret;
			failed_features |= NETIF_F_LRO;
		}
	}

	if (changed & NETIF_F_HW_VLAN_CTAG_RX) {
		ret = hinic_set_rx_vlan_offload(nic_dev,
						!!(features &
						   NETIF_F_HW_VLAN_CTAG_RX));
		if (ret) {
			err = ret;
			failed_features |= NETIF_F_HW_VLAN_CTAG_RX;
		}
	}

	if (err) {
		nic_dev->netdev->features = features ^ failed_features;
		return -EIO;
	}

	return 0;
}

static int hinic_init_intr_coalesce(struct hinic_dev *nic_dev)
{
	u64 size;
	u16 i;

	size = sizeof(struct hinic_intr_coal_info) * nic_dev->max_qps;
	nic_dev->rx_intr_coalesce = kzalloc(size, GFP_KERNEL);
	if (!nic_dev->rx_intr_coalesce)
		return -ENOMEM;
	nic_dev->tx_intr_coalesce = kzalloc(size, GFP_KERNEL);
	if (!nic_dev->tx_intr_coalesce) {
		kfree(nic_dev->rx_intr_coalesce);
		return -ENOMEM;
	}

	for (i = 0; i < nic_dev->max_qps; i++) {
		nic_dev->rx_intr_coalesce[i].pending_limt =
			HINIC_DEAULT_TXRX_MSIX_PENDING_LIMIT;
		nic_dev->rx_intr_coalesce[i].coalesce_timer_cfg =
			HINIC_DEAULT_TXRX_MSIX_COALESC_TIMER_CFG;
		nic_dev->rx_intr_coalesce[i].resend_timer_cfg =
			HINIC_DEAULT_TXRX_MSIX_RESEND_TIMER_CFG;
		nic_dev->tx_intr_coalesce[i].pending_limt =
			HINIC_DEAULT_TXRX_MSIX_PENDING_LIMIT;
		nic_dev->tx_intr_coalesce[i].coalesce_timer_cfg =
			HINIC_DEAULT_TXRX_MSIX_COALESC_TIMER_CFG;
		nic_dev->tx_intr_coalesce[i].resend_timer_cfg =
			HINIC_DEAULT_TXRX_MSIX_RESEND_TIMER_CFG;
	}

	return 0;
}

static void hinic_free_intr_coalesce(struct hinic_dev *nic_dev)
{
	kfree(nic_dev->tx_intr_coalesce);
	kfree(nic_dev->rx_intr_coalesce);
}

/**
 * nic_dev_init - Initialize the NIC device
 * @pdev: the NIC pci device
 *
 * Return 0 - Success, negative - Failure
 **/
static int nic_dev_init(struct pci_dev *pdev)
{
	struct hinic_rx_mode_work *rx_mode_work;
	struct hinic_txq_stats *tx_stats;
	struct hinic_rxq_stats *rx_stats;
	struct hinic_dev *nic_dev;
	struct net_device *netdev;
	struct hinic_hwdev *hwdev;
	struct devlink *devlink;
	int err, num_qps;

	devlink = hinic_devlink_alloc();
	if (!devlink) {
		dev_err(&pdev->dev, "Hinic devlink alloc failed\n");
		return -ENOMEM;
	}

	hwdev = hinic_init_hwdev(pdev, devlink);
	if (IS_ERR(hwdev)) {
		dev_err(&pdev->dev, "Failed to initialize HW device\n");
		hinic_devlink_free(devlink);
		return PTR_ERR(hwdev);
	}

	num_qps = hinic_hwdev_num_qps(hwdev);
	if (num_qps <= 0) {
		dev_err(&pdev->dev, "Invalid number of QPS\n");
		err = -EINVAL;
		goto err_num_qps;
	}

	netdev = alloc_etherdev_mq(sizeof(*nic_dev), num_qps);
	if (!netdev) {
		dev_err(&pdev->dev, "Failed to allocate Ethernet device\n");
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}

	if (!HINIC_IS_VF(hwdev->hwif))
		netdev->netdev_ops = &hinic_netdev_ops;
	else
		netdev->netdev_ops = &hinicvf_netdev_ops;

	netdev->max_mtu = ETH_MAX_MTU;

	nic_dev = netdev_priv(netdev);
	nic_dev->netdev = netdev;
	nic_dev->hwdev  = hwdev;
	nic_dev->msg_enable = MSG_ENABLE_DEFAULT;
	nic_dev->flags = 0;
	nic_dev->txqs = NULL;
	nic_dev->rxqs = NULL;
	nic_dev->tx_weight = tx_weight;
	nic_dev->rx_weight = rx_weight;
	nic_dev->sq_depth = HINIC_SQ_DEPTH;
	nic_dev->rq_depth = HINIC_RQ_DEPTH;
	nic_dev->sriov_info.hwdev = hwdev;
	nic_dev->sriov_info.pdev = pdev;
	nic_dev->max_qps = num_qps;
	nic_dev->devlink = devlink;

	hinic_set_ethtool_ops(netdev);

	sema_init(&nic_dev->mgmt_lock, 1);

	tx_stats = &nic_dev->tx_stats;
	rx_stats = &nic_dev->rx_stats;

	u64_stats_init(&tx_stats->syncp);
	u64_stats_init(&rx_stats->syncp);

	nic_dev->vlan_bitmap = devm_bitmap_zalloc(&pdev->dev, VLAN_N_VID,
						  GFP_KERNEL);
	if (!nic_dev->vlan_bitmap) {
		err = -ENOMEM;
		goto err_vlan_bitmap;
	}

	nic_dev->workq = create_singlethread_workqueue(HINIC_WQ_NAME);
	if (!nic_dev->workq) {
		err = -ENOMEM;
		goto err_workq;
	}

	pci_set_drvdata(pdev, netdev);

	err = hinic_port_get_mac(nic_dev, netdev->dev_addr);
	if (err) {
		dev_err(&pdev->dev, "Failed to get mac address\n");
		goto err_get_mac;
	}

	if (!is_valid_ether_addr(netdev->dev_addr)) {
		if (!HINIC_IS_VF(nic_dev->hwdev->hwif)) {
			dev_err(&pdev->dev, "Invalid MAC address\n");
			err = -EIO;
			goto err_add_mac;
		}

		dev_info(&pdev->dev, "Invalid MAC address %pM, using random\n",
			 netdev->dev_addr);
		eth_hw_addr_random(netdev);
	}

	err = hinic_port_add_mac(nic_dev, netdev->dev_addr, 0);
	if (err && err != HINIC_PF_SET_VF_ALREADY) {
		dev_err(&pdev->dev, "Failed to add mac\n");
		goto err_add_mac;
	}

	err = hinic_port_set_mtu(nic_dev, netdev->mtu);
	if (err) {
		dev_err(&pdev->dev, "Failed to set mtu\n");
		goto err_set_mtu;
	}

	rx_mode_work = &nic_dev->rx_mode_work;
	INIT_WORK(&rx_mode_work->work, set_rx_mode);

	netdev_features_init(netdev);

	netif_carrier_off(netdev);

	hinic_hwdev_cb_register(nic_dev->hwdev, HINIC_MGMT_MSG_CMD_LINK_STATUS,
				nic_dev, link_status_event_handler);
	hinic_hwdev_cb_register(nic_dev->hwdev,
				HINIC_MGMT_MSG_CMD_CABLE_PLUG_EVENT,
				nic_dev, cable_plug_event);
	hinic_hwdev_cb_register(nic_dev->hwdev,
				HINIC_MGMT_MSG_CMD_LINK_ERR_EVENT,
				nic_dev, link_err_event);

	err = set_features(nic_dev, 0, nic_dev->netdev->features, true);
	if (err)
		goto err_set_features;

	/* enable pause and disable pfc by default */
	err = hinic_dcb_set_pfc(nic_dev->hwdev, 0, 0);
	if (err)
		goto err_set_pfc;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	err = hinic_init_intr_coalesce(nic_dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init_intr_coalesce\n");
		goto err_init_intr;
	}

	hinic_dbg_init(nic_dev);

	hinic_func_tbl_dbgfs_init(nic_dev);

	err = hinic_func_table_debug_add(nic_dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to add func_table debug\n");
		goto err_add_func_table_dbg;
	}

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto err_reg_netdev;
	}

	return 0;

err_reg_netdev:
	hinic_func_table_debug_rem(nic_dev);
err_add_func_table_dbg:
	hinic_func_tbl_dbgfs_uninit(nic_dev);
	hinic_dbg_uninit(nic_dev);
	hinic_free_intr_coalesce(nic_dev);
err_init_intr:
err_set_pfc:
err_set_features:
	hinic_hwdev_cb_unregister(nic_dev->hwdev,
				  HINIC_MGMT_MSG_CMD_LINK_ERR_EVENT);
	hinic_hwdev_cb_unregister(nic_dev->hwdev,
				  HINIC_MGMT_MSG_CMD_CABLE_PLUG_EVENT);
	hinic_hwdev_cb_unregister(nic_dev->hwdev,
				  HINIC_MGMT_MSG_CMD_LINK_STATUS);
	cancel_work_sync(&rx_mode_work->work);

err_set_mtu:
	hinic_port_del_mac(nic_dev, netdev->dev_addr, 0);
err_add_mac:
err_get_mac:
	pci_set_drvdata(pdev, NULL);
	destroy_workqueue(nic_dev->workq);
err_workq:
err_vlan_bitmap:
	free_netdev(netdev);

err_alloc_etherdev:
err_num_qps:
	hinic_free_hwdev(hwdev);
	hinic_devlink_free(devlink);
	return err;
}

static int hinic_probe(struct pci_dev *pdev,
		       const struct pci_device_id *id)
{
	int err = pci_enable_device(pdev);

	if (err) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, HINIC_DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Failed to request PCI regions\n");
		goto err_pci_regions;
	}

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev, "Couldn't set 64-bit DMA mask\n");
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "Failed to set DMA mask\n");
			goto err_dma_mask;
		}
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev,
			 "Couldn't set 64-bit consistent DMA mask\n");
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev,
				"Failed to set consistent DMA mask\n");
			goto err_dma_consistent_mask;
		}
	}

	err = nic_dev_init(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize NIC device\n");
		goto err_nic_dev_init;
	}

	dev_info(&pdev->dev, "HiNIC driver - probed\n");
	return 0;

err_nic_dev_init:
err_dma_consistent_mask:
err_dma_mask:
	pci_release_regions(pdev);

err_pci_regions:
	pci_disable_device(pdev);
	return err;
}

#define HINIC_WAIT_SRIOV_CFG_TIMEOUT	15000

static void wait_sriov_cfg_complete(struct hinic_dev *nic_dev)
{
	struct hinic_sriov_info *sriov_info = &nic_dev->sriov_info;
	u32 loop_cnt = 0;

	set_bit(HINIC_FUNC_REMOVE, &sriov_info->state);
	usleep_range(9900, 10000);

	while (loop_cnt < HINIC_WAIT_SRIOV_CFG_TIMEOUT) {
		if (!test_bit(HINIC_SRIOV_ENABLE, &sriov_info->state) &&
		    !test_bit(HINIC_SRIOV_DISABLE, &sriov_info->state))
			return;

		usleep_range(9900, 10000);
		loop_cnt++;
	}
}

static void hinic_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct devlink *devlink = nic_dev->devlink;
	struct hinic_rx_mode_work *rx_mode_work;

	if (!HINIC_IS_VF(nic_dev->hwdev->hwif)) {
		wait_sriov_cfg_complete(nic_dev);
		hinic_pci_sriov_disable(pdev);
	}

	unregister_netdev(netdev);

	hinic_func_table_debug_rem(nic_dev);

	hinic_func_tbl_dbgfs_uninit(nic_dev);

	hinic_dbg_uninit(nic_dev);

	hinic_free_intr_coalesce(nic_dev);

	hinic_port_del_mac(nic_dev, netdev->dev_addr, 0);

	hinic_hwdev_cb_unregister(nic_dev->hwdev,
				  HINIC_MGMT_MSG_CMD_LINK_ERR_EVENT);
	hinic_hwdev_cb_unregister(nic_dev->hwdev,
				  HINIC_MGMT_MSG_CMD_CABLE_PLUG_EVENT);
	hinic_hwdev_cb_unregister(nic_dev->hwdev,
				  HINIC_MGMT_MSG_CMD_LINK_STATUS);

	rx_mode_work = &nic_dev->rx_mode_work;
	cancel_work_sync(&rx_mode_work->work);

	pci_set_drvdata(pdev, NULL);

	destroy_workqueue(nic_dev->workq);

	hinic_free_hwdev(nic_dev->hwdev);

	free_netdev(netdev);

	hinic_devlink_free(devlink);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	dev_info(&pdev->dev, "HiNIC driver - removed\n");
}

static void hinic_shutdown(struct pci_dev *pdev)
{
	pci_disable_device(pdev);
}

static const struct pci_device_id hinic_pci_table[] = {
	{ PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_QUAD_PORT_25GE), 0},
	{ PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_DUAL_PORT_100GE), 0},
	{ PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_DUAL_PORT_100GE_MEZZ), 0},
	{ PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_QUAD_PORT_25GE_MEZZ), 0},
	{ PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_VF), 0},
	{ 0, 0}
};
MODULE_DEVICE_TABLE(pci, hinic_pci_table);

static struct pci_driver hinic_driver = {
	.name           = HINIC_DRV_NAME,
	.id_table       = hinic_pci_table,
	.probe          = hinic_probe,
	.remove         = hinic_remove,
	.shutdown       = hinic_shutdown,
	.sriov_configure = hinic_pci_sriov_configure,
};

static int __init hinic_module_init(void)
{
	hinic_dbg_register_debugfs(HINIC_DRV_NAME);
	return pci_register_driver(&hinic_driver);
}

static void __exit hinic_module_exit(void)
{
	pci_unregister_driver(&hinic_driver);
	hinic_dbg_unregister_debugfs();
}

module_init(hinic_module_init);
module_exit(hinic_module_exit);
