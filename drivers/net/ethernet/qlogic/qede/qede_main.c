/* QLogic qede NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <asm/byteorder.h>
#include <asm/param.h>
#include <linux/io.h>
#include <linux/netdev_features.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <net/udp_tunnel.h>
#include <linux/ip.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/pkt_sched.h>
#include <linux/ethtool.h>
#include <linux/in.h>
#include <linux/random.h>
#include <net/ip6_checksum.h>
#include <linux/bitops.h>
#include <linux/vmalloc.h>
#include "qede.h"
#include "qede_ptp.h"

static char version[] =
	"QLogic FastLinQ 4xxxx Ethernet Driver qede " DRV_MODULE_VERSION "\n";

MODULE_DESCRIPTION("QLogic FastLinQ 4xxxx Ethernet Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static uint debug;
module_param(debug, uint, 0);
MODULE_PARM_DESC(debug, " Default debug msglevel");

static const struct qed_eth_ops *qed_ops;

#define CHIP_NUM_57980S_40		0x1634
#define CHIP_NUM_57980S_10		0x1666
#define CHIP_NUM_57980S_MF		0x1636
#define CHIP_NUM_57980S_100		0x1644
#define CHIP_NUM_57980S_50		0x1654
#define CHIP_NUM_57980S_25		0x1656
#define CHIP_NUM_57980S_IOV		0x1664
#define CHIP_NUM_AH			0x8070
#define CHIP_NUM_AH_IOV			0x8090

#ifndef PCI_DEVICE_ID_NX2_57980E
#define PCI_DEVICE_ID_57980S_40		CHIP_NUM_57980S_40
#define PCI_DEVICE_ID_57980S_10		CHIP_NUM_57980S_10
#define PCI_DEVICE_ID_57980S_MF		CHIP_NUM_57980S_MF
#define PCI_DEVICE_ID_57980S_100	CHIP_NUM_57980S_100
#define PCI_DEVICE_ID_57980S_50		CHIP_NUM_57980S_50
#define PCI_DEVICE_ID_57980S_25		CHIP_NUM_57980S_25
#define PCI_DEVICE_ID_57980S_IOV	CHIP_NUM_57980S_IOV
#define PCI_DEVICE_ID_AH		CHIP_NUM_AH
#define PCI_DEVICE_ID_AH_IOV		CHIP_NUM_AH_IOV

#endif

enum qede_pci_private {
	QEDE_PRIVATE_PF,
	QEDE_PRIVATE_VF
};

static const struct pci_device_id qede_pci_tbl[] = {
	{PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_40), QEDE_PRIVATE_PF},
	{PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_10), QEDE_PRIVATE_PF},
	{PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_MF), QEDE_PRIVATE_PF},
	{PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_100), QEDE_PRIVATE_PF},
	{PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_50), QEDE_PRIVATE_PF},
	{PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_25), QEDE_PRIVATE_PF},
#ifdef CONFIG_QED_SRIOV
	{PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_IOV), QEDE_PRIVATE_VF},
#endif
	{PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_AH), QEDE_PRIVATE_PF},
#ifdef CONFIG_QED_SRIOV
	{PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_AH_IOV), QEDE_PRIVATE_VF},
#endif
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, qede_pci_tbl);

static int qede_probe(struct pci_dev *pdev, const struct pci_device_id *id);

#define TX_TIMEOUT		(5 * HZ)

/* Utilize last protocol index for XDP */
#define XDP_PI	11

static void qede_remove(struct pci_dev *pdev);
static void qede_shutdown(struct pci_dev *pdev);
static void qede_link_update(void *dev, struct qed_link_output *link);
static void qede_get_eth_tlv_data(void *edev, void *data);
static void qede_get_generic_tlv_data(void *edev,
				      struct qed_generic_tlvs *data);

/* The qede lock is used to protect driver state change and driver flows that
 * are not reentrant.
 */
void __qede_lock(struct qede_dev *edev)
{
	mutex_lock(&edev->qede_lock);
}

void __qede_unlock(struct qede_dev *edev)
{
	mutex_unlock(&edev->qede_lock);
}

#ifdef CONFIG_QED_SRIOV
static int qede_set_vf_vlan(struct net_device *ndev, int vf, u16 vlan, u8 qos,
			    __be16 vlan_proto)
{
	struct qede_dev *edev = netdev_priv(ndev);

	if (vlan > 4095) {
		DP_NOTICE(edev, "Illegal vlan value %d\n", vlan);
		return -EINVAL;
	}

	if (vlan_proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;

	DP_VERBOSE(edev, QED_MSG_IOV, "Setting Vlan 0x%04x to VF [%d]\n",
		   vlan, vf);

	return edev->ops->iov->set_vlan(edev->cdev, vlan, vf);
}

static int qede_set_vf_mac(struct net_device *ndev, int vfidx, u8 *mac)
{
	struct qede_dev *edev = netdev_priv(ndev);

	DP_VERBOSE(edev, QED_MSG_IOV,
		   "Setting MAC %02x:%02x:%02x:%02x:%02x:%02x to VF [%d]\n",
		   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], vfidx);

	if (!is_valid_ether_addr(mac)) {
		DP_VERBOSE(edev, QED_MSG_IOV, "MAC address isn't valid\n");
		return -EINVAL;
	}

	return edev->ops->iov->set_mac(edev->cdev, mac, vfidx);
}

static int qede_sriov_configure(struct pci_dev *pdev, int num_vfs_param)
{
	struct qede_dev *edev = netdev_priv(pci_get_drvdata(pdev));
	struct qed_dev_info *qed_info = &edev->dev_info.common;
	struct qed_update_vport_params *vport_params;
	int rc;

	vport_params = vzalloc(sizeof(*vport_params));
	if (!vport_params)
		return -ENOMEM;
	DP_VERBOSE(edev, QED_MSG_IOV, "Requested %d VFs\n", num_vfs_param);

	rc = edev->ops->iov->configure(edev->cdev, num_vfs_param);

	/* Enable/Disable Tx switching for PF */
	if ((rc == num_vfs_param) && netif_running(edev->ndev) &&
	    !qed_info->b_inter_pf_switch && qed_info->tx_switching) {
		vport_params->vport_id = 0;
		vport_params->update_tx_switching_flg = 1;
		vport_params->tx_switching_flg = num_vfs_param ? 1 : 0;
		edev->ops->vport_update(edev->cdev, vport_params);
	}

	vfree(vport_params);
	return rc;
}
#endif

static struct pci_driver qede_pci_driver = {
	.name = "qede",
	.id_table = qede_pci_tbl,
	.probe = qede_probe,
	.remove = qede_remove,
	.shutdown = qede_shutdown,
#ifdef CONFIG_QED_SRIOV
	.sriov_configure = qede_sriov_configure,
#endif
};

static struct qed_eth_cb_ops qede_ll_ops = {
	{
#ifdef CONFIG_RFS_ACCEL
		.arfs_filter_op = qede_arfs_filter_op,
#endif
		.link_update = qede_link_update,
		.get_generic_tlv_data = qede_get_generic_tlv_data,
		.get_protocol_tlv_data = qede_get_eth_tlv_data,
	},
	.force_mac = qede_force_mac,
	.ports_update = qede_udp_ports_update,
};

static int qede_netdev_event(struct notifier_block *this, unsigned long event,
			     void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct ethtool_drvinfo drvinfo;
	struct qede_dev *edev;

	if (event != NETDEV_CHANGENAME && event != NETDEV_CHANGEADDR)
		goto done;

	/* Check whether this is a qede device */
	if (!ndev || !ndev->ethtool_ops || !ndev->ethtool_ops->get_drvinfo)
		goto done;

	memset(&drvinfo, 0, sizeof(drvinfo));
	ndev->ethtool_ops->get_drvinfo(ndev, &drvinfo);
	if (strcmp(drvinfo.driver, "qede"))
		goto done;
	edev = netdev_priv(ndev);

	switch (event) {
	case NETDEV_CHANGENAME:
		/* Notify qed of the name change */
		if (!edev->ops || !edev->ops->common)
			goto done;
		edev->ops->common->set_name(edev->cdev, edev->ndev->name);
		break;
	case NETDEV_CHANGEADDR:
		edev = netdev_priv(ndev);
		qede_rdma_event_changeaddr(edev);
		break;
	}

done:
	return NOTIFY_DONE;
}

static struct notifier_block qede_netdev_notifier = {
	.notifier_call = qede_netdev_event,
};

static
int __init qede_init(void)
{
	int ret;

	pr_info("qede_init: %s\n", version);

	qed_ops = qed_get_eth_ops();
	if (!qed_ops) {
		pr_notice("Failed to get qed ethtool operations\n");
		return -EINVAL;
	}

	/* Must register notifier before pci ops, since we might miss
	 * interface rename after pci probe and netdev registration.
	 */
	ret = register_netdevice_notifier(&qede_netdev_notifier);
	if (ret) {
		pr_notice("Failed to register netdevice_notifier\n");
		qed_put_eth_ops();
		return -EINVAL;
	}

	ret = pci_register_driver(&qede_pci_driver);
	if (ret) {
		pr_notice("Failed to register driver\n");
		unregister_netdevice_notifier(&qede_netdev_notifier);
		qed_put_eth_ops();
		return -EINVAL;
	}

	return 0;
}

static void __exit qede_cleanup(void)
{
	if (debug & QED_LOG_INFO_MASK)
		pr_info("qede_cleanup called\n");

	unregister_netdevice_notifier(&qede_netdev_notifier);
	pci_unregister_driver(&qede_pci_driver);
	qed_put_eth_ops();
}

module_init(qede_init);
module_exit(qede_cleanup);

static int qede_open(struct net_device *ndev);
static int qede_close(struct net_device *ndev);

void qede_fill_by_demand_stats(struct qede_dev *edev)
{
	struct qede_stats_common *p_common = &edev->stats.common;
	struct qed_eth_stats stats;

	edev->ops->get_vport_stats(edev->cdev, &stats);

	p_common->no_buff_discards = stats.common.no_buff_discards;
	p_common->packet_too_big_discard = stats.common.packet_too_big_discard;
	p_common->ttl0_discard = stats.common.ttl0_discard;
	p_common->rx_ucast_bytes = stats.common.rx_ucast_bytes;
	p_common->rx_mcast_bytes = stats.common.rx_mcast_bytes;
	p_common->rx_bcast_bytes = stats.common.rx_bcast_bytes;
	p_common->rx_ucast_pkts = stats.common.rx_ucast_pkts;
	p_common->rx_mcast_pkts = stats.common.rx_mcast_pkts;
	p_common->rx_bcast_pkts = stats.common.rx_bcast_pkts;
	p_common->mftag_filter_discards = stats.common.mftag_filter_discards;
	p_common->mac_filter_discards = stats.common.mac_filter_discards;
	p_common->gft_filter_drop = stats.common.gft_filter_drop;

	p_common->tx_ucast_bytes = stats.common.tx_ucast_bytes;
	p_common->tx_mcast_bytes = stats.common.tx_mcast_bytes;
	p_common->tx_bcast_bytes = stats.common.tx_bcast_bytes;
	p_common->tx_ucast_pkts = stats.common.tx_ucast_pkts;
	p_common->tx_mcast_pkts = stats.common.tx_mcast_pkts;
	p_common->tx_bcast_pkts = stats.common.tx_bcast_pkts;
	p_common->tx_err_drop_pkts = stats.common.tx_err_drop_pkts;
	p_common->coalesced_pkts = stats.common.tpa_coalesced_pkts;
	p_common->coalesced_events = stats.common.tpa_coalesced_events;
	p_common->coalesced_aborts_num = stats.common.tpa_aborts_num;
	p_common->non_coalesced_pkts = stats.common.tpa_not_coalesced_pkts;
	p_common->coalesced_bytes = stats.common.tpa_coalesced_bytes;

	p_common->rx_64_byte_packets = stats.common.rx_64_byte_packets;
	p_common->rx_65_to_127_byte_packets =
	    stats.common.rx_65_to_127_byte_packets;
	p_common->rx_128_to_255_byte_packets =
	    stats.common.rx_128_to_255_byte_packets;
	p_common->rx_256_to_511_byte_packets =
	    stats.common.rx_256_to_511_byte_packets;
	p_common->rx_512_to_1023_byte_packets =
	    stats.common.rx_512_to_1023_byte_packets;
	p_common->rx_1024_to_1518_byte_packets =
	    stats.common.rx_1024_to_1518_byte_packets;
	p_common->rx_crc_errors = stats.common.rx_crc_errors;
	p_common->rx_mac_crtl_frames = stats.common.rx_mac_crtl_frames;
	p_common->rx_pause_frames = stats.common.rx_pause_frames;
	p_common->rx_pfc_frames = stats.common.rx_pfc_frames;
	p_common->rx_align_errors = stats.common.rx_align_errors;
	p_common->rx_carrier_errors = stats.common.rx_carrier_errors;
	p_common->rx_oversize_packets = stats.common.rx_oversize_packets;
	p_common->rx_jabbers = stats.common.rx_jabbers;
	p_common->rx_undersize_packets = stats.common.rx_undersize_packets;
	p_common->rx_fragments = stats.common.rx_fragments;
	p_common->tx_64_byte_packets = stats.common.tx_64_byte_packets;
	p_common->tx_65_to_127_byte_packets =
	    stats.common.tx_65_to_127_byte_packets;
	p_common->tx_128_to_255_byte_packets =
	    stats.common.tx_128_to_255_byte_packets;
	p_common->tx_256_to_511_byte_packets =
	    stats.common.tx_256_to_511_byte_packets;
	p_common->tx_512_to_1023_byte_packets =
	    stats.common.tx_512_to_1023_byte_packets;
	p_common->tx_1024_to_1518_byte_packets =
	    stats.common.tx_1024_to_1518_byte_packets;
	p_common->tx_pause_frames = stats.common.tx_pause_frames;
	p_common->tx_pfc_frames = stats.common.tx_pfc_frames;
	p_common->brb_truncates = stats.common.brb_truncates;
	p_common->brb_discards = stats.common.brb_discards;
	p_common->tx_mac_ctrl_frames = stats.common.tx_mac_ctrl_frames;
	p_common->link_change_count = stats.common.link_change_count;

	if (QEDE_IS_BB(edev)) {
		struct qede_stats_bb *p_bb = &edev->stats.bb;

		p_bb->rx_1519_to_1522_byte_packets =
		    stats.bb.rx_1519_to_1522_byte_packets;
		p_bb->rx_1519_to_2047_byte_packets =
		    stats.bb.rx_1519_to_2047_byte_packets;
		p_bb->rx_2048_to_4095_byte_packets =
		    stats.bb.rx_2048_to_4095_byte_packets;
		p_bb->rx_4096_to_9216_byte_packets =
		    stats.bb.rx_4096_to_9216_byte_packets;
		p_bb->rx_9217_to_16383_byte_packets =
		    stats.bb.rx_9217_to_16383_byte_packets;
		p_bb->tx_1519_to_2047_byte_packets =
		    stats.bb.tx_1519_to_2047_byte_packets;
		p_bb->tx_2048_to_4095_byte_packets =
		    stats.bb.tx_2048_to_4095_byte_packets;
		p_bb->tx_4096_to_9216_byte_packets =
		    stats.bb.tx_4096_to_9216_byte_packets;
		p_bb->tx_9217_to_16383_byte_packets =
		    stats.bb.tx_9217_to_16383_byte_packets;
		p_bb->tx_lpi_entry_count = stats.bb.tx_lpi_entry_count;
		p_bb->tx_total_collisions = stats.bb.tx_total_collisions;
	} else {
		struct qede_stats_ah *p_ah = &edev->stats.ah;

		p_ah->rx_1519_to_max_byte_packets =
		    stats.ah.rx_1519_to_max_byte_packets;
		p_ah->tx_1519_to_max_byte_packets =
		    stats.ah.tx_1519_to_max_byte_packets;
	}
}

static void qede_get_stats64(struct net_device *dev,
			     struct rtnl_link_stats64 *stats)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_stats_common *p_common;

	qede_fill_by_demand_stats(edev);
	p_common = &edev->stats.common;

	stats->rx_packets = p_common->rx_ucast_pkts + p_common->rx_mcast_pkts +
			    p_common->rx_bcast_pkts;
	stats->tx_packets = p_common->tx_ucast_pkts + p_common->tx_mcast_pkts +
			    p_common->tx_bcast_pkts;

	stats->rx_bytes = p_common->rx_ucast_bytes + p_common->rx_mcast_bytes +
			  p_common->rx_bcast_bytes;
	stats->tx_bytes = p_common->tx_ucast_bytes + p_common->tx_mcast_bytes +
			  p_common->tx_bcast_bytes;

	stats->tx_errors = p_common->tx_err_drop_pkts;
	stats->multicast = p_common->rx_mcast_pkts + p_common->rx_bcast_pkts;

	stats->rx_fifo_errors = p_common->no_buff_discards;

	if (QEDE_IS_BB(edev))
		stats->collisions = edev->stats.bb.tx_total_collisions;
	stats->rx_crc_errors = p_common->rx_crc_errors;
	stats->rx_frame_errors = p_common->rx_align_errors;
}

#ifdef CONFIG_QED_SRIOV
static int qede_get_vf_config(struct net_device *dev, int vfidx,
			      struct ifla_vf_info *ivi)
{
	struct qede_dev *edev = netdev_priv(dev);

	if (!edev->ops)
		return -EINVAL;

	return edev->ops->iov->get_config(edev->cdev, vfidx, ivi);
}

static int qede_set_vf_rate(struct net_device *dev, int vfidx,
			    int min_tx_rate, int max_tx_rate)
{
	struct qede_dev *edev = netdev_priv(dev);

	return edev->ops->iov->set_rate(edev->cdev, vfidx, min_tx_rate,
					max_tx_rate);
}

static int qede_set_vf_spoofchk(struct net_device *dev, int vfidx, bool val)
{
	struct qede_dev *edev = netdev_priv(dev);

	if (!edev->ops)
		return -EINVAL;

	return edev->ops->iov->set_spoof(edev->cdev, vfidx, val);
}

static int qede_set_vf_link_state(struct net_device *dev, int vfidx,
				  int link_state)
{
	struct qede_dev *edev = netdev_priv(dev);

	if (!edev->ops)
		return -EINVAL;

	return edev->ops->iov->set_link_state(edev->cdev, vfidx, link_state);
}

static int qede_set_vf_trust(struct net_device *dev, int vfidx, bool setting)
{
	struct qede_dev *edev = netdev_priv(dev);

	if (!edev->ops)
		return -EINVAL;

	return edev->ops->iov->set_trust(edev->cdev, vfidx, setting);
}
#endif

static int qede_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct qede_dev *edev = netdev_priv(dev);

	if (!netif_running(dev))
		return -EAGAIN;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return qede_ptp_hw_ts(edev, ifr);
	default:
		DP_VERBOSE(edev, QED_MSG_DEBUG,
			   "default IOCTL cmd 0x%x\n", cmd);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int qede_setup_tc(struct net_device *ndev, u8 num_tc)
{
	struct qede_dev *edev = netdev_priv(ndev);
	int cos, count, offset;

	if (num_tc > edev->dev_info.num_tc)
		return -EINVAL;

	netdev_reset_tc(ndev);
	netdev_set_num_tc(ndev, num_tc);

	for_each_cos_in_txq(edev, cos) {
		count = QEDE_TSS_COUNT(edev);
		offset = cos * QEDE_TSS_COUNT(edev);
		netdev_set_tc_queue(ndev, cos, count, offset);
	}

	return 0;
}

static int
qede_set_flower(struct qede_dev *edev, struct tc_cls_flower_offload *f,
		__be16 proto)
{
	switch (f->command) {
	case TC_CLSFLOWER_REPLACE:
		return qede_add_tc_flower_fltr(edev, proto, f);
	case TC_CLSFLOWER_DESTROY:
		return qede_delete_flow_filter(edev, f->cookie);
	default:
		return -EOPNOTSUPP;
	}
}

static int qede_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				  void *cb_priv)
{
	struct tc_cls_flower_offload *f;
	struct qede_dev *edev = cb_priv;

	if (!tc_cls_can_offload_and_chain0(edev->ndev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		f = type_data;
		return qede_set_flower(edev, f, f->common.protocol);
	default:
		return -EOPNOTSUPP;
	}
}

static int qede_setup_tc_block(struct qede_dev *edev,
			       struct tc_block_offload *f)
{
	if (f->binder_type != TCF_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	switch (f->command) {
	case TC_BLOCK_BIND:
		return tcf_block_cb_register(f->block,
					     qede_setup_tc_block_cb,
					     edev, edev, f->extack);
	case TC_BLOCK_UNBIND:
		tcf_block_cb_unregister(f->block, qede_setup_tc_block_cb, edev);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int
qede_setup_tc_offload(struct net_device *dev, enum tc_setup_type type,
		      void *type_data)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct tc_mqprio_qopt *mqprio;

	switch (type) {
	case TC_SETUP_BLOCK:
		return qede_setup_tc_block(edev, type_data);
	case TC_SETUP_QDISC_MQPRIO:
		mqprio = type_data;

		mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;
		return qede_setup_tc(dev, mqprio->num_tc);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops qede_netdev_ops = {
	.ndo_open = qede_open,
	.ndo_stop = qede_close,
	.ndo_start_xmit = qede_start_xmit,
	.ndo_select_queue = qede_select_queue,
	.ndo_set_rx_mode = qede_set_rx_mode,
	.ndo_set_mac_address = qede_set_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = qede_change_mtu,
	.ndo_do_ioctl = qede_ioctl,
#ifdef CONFIG_QED_SRIOV
	.ndo_set_vf_mac = qede_set_vf_mac,
	.ndo_set_vf_vlan = qede_set_vf_vlan,
	.ndo_set_vf_trust = qede_set_vf_trust,
#endif
	.ndo_vlan_rx_add_vid = qede_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = qede_vlan_rx_kill_vid,
	.ndo_fix_features = qede_fix_features,
	.ndo_set_features = qede_set_features,
	.ndo_get_stats64 = qede_get_stats64,
#ifdef CONFIG_QED_SRIOV
	.ndo_set_vf_link_state = qede_set_vf_link_state,
	.ndo_set_vf_spoofchk = qede_set_vf_spoofchk,
	.ndo_get_vf_config = qede_get_vf_config,
	.ndo_set_vf_rate = qede_set_vf_rate,
#endif
	.ndo_udp_tunnel_add = qede_udp_tunnel_add,
	.ndo_udp_tunnel_del = qede_udp_tunnel_del,
	.ndo_features_check = qede_features_check,
	.ndo_bpf = qede_xdp,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer = qede_rx_flow_steer,
#endif
	.ndo_setup_tc = qede_setup_tc_offload,
};

static const struct net_device_ops qede_netdev_vf_ops = {
	.ndo_open = qede_open,
	.ndo_stop = qede_close,
	.ndo_start_xmit = qede_start_xmit,
	.ndo_select_queue = qede_select_queue,
	.ndo_set_rx_mode = qede_set_rx_mode,
	.ndo_set_mac_address = qede_set_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = qede_change_mtu,
	.ndo_vlan_rx_add_vid = qede_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = qede_vlan_rx_kill_vid,
	.ndo_fix_features = qede_fix_features,
	.ndo_set_features = qede_set_features,
	.ndo_get_stats64 = qede_get_stats64,
	.ndo_udp_tunnel_add = qede_udp_tunnel_add,
	.ndo_udp_tunnel_del = qede_udp_tunnel_del,
	.ndo_features_check = qede_features_check,
};

static const struct net_device_ops qede_netdev_vf_xdp_ops = {
	.ndo_open = qede_open,
	.ndo_stop = qede_close,
	.ndo_start_xmit = qede_start_xmit,
	.ndo_select_queue = qede_select_queue,
	.ndo_set_rx_mode = qede_set_rx_mode,
	.ndo_set_mac_address = qede_set_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = qede_change_mtu,
	.ndo_vlan_rx_add_vid = qede_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = qede_vlan_rx_kill_vid,
	.ndo_fix_features = qede_fix_features,
	.ndo_set_features = qede_set_features,
	.ndo_get_stats64 = qede_get_stats64,
	.ndo_udp_tunnel_add = qede_udp_tunnel_add,
	.ndo_udp_tunnel_del = qede_udp_tunnel_del,
	.ndo_features_check = qede_features_check,
	.ndo_bpf = qede_xdp,
};

/* -------------------------------------------------------------------------
 * START OF PROBE / REMOVE
 * -------------------------------------------------------------------------
 */

static struct qede_dev *qede_alloc_etherdev(struct qed_dev *cdev,
					    struct pci_dev *pdev,
					    struct qed_dev_eth_info *info,
					    u32 dp_module, u8 dp_level)
{
	struct net_device *ndev;
	struct qede_dev *edev;

	ndev = alloc_etherdev_mqs(sizeof(*edev),
				  info->num_queues * info->num_tc,
				  info->num_queues);
	if (!ndev) {
		pr_err("etherdev allocation failed\n");
		return NULL;
	}

	edev = netdev_priv(ndev);
	edev->ndev = ndev;
	edev->cdev = cdev;
	edev->pdev = pdev;
	edev->dp_module = dp_module;
	edev->dp_level = dp_level;
	edev->ops = qed_ops;
	edev->q_num_rx_buffers = NUM_RX_BDS_DEF;
	edev->q_num_tx_buffers = NUM_TX_BDS_DEF;

	DP_INFO(edev, "Allocated netdev with %d tx queues and %d rx queues\n",
		info->num_queues, info->num_queues);

	SET_NETDEV_DEV(ndev, &pdev->dev);

	memset(&edev->stats, 0, sizeof(edev->stats));
	memcpy(&edev->dev_info, info, sizeof(*info));

	/* As ethtool doesn't have the ability to show WoL behavior as
	 * 'default', if device supports it declare it's enabled.
	 */
	if (edev->dev_info.common.wol_support)
		edev->wol_enabled = true;

	INIT_LIST_HEAD(&edev->vlan_list);

	return edev;
}

static void qede_init_ndev(struct qede_dev *edev)
{
	struct net_device *ndev = edev->ndev;
	struct pci_dev *pdev = edev->pdev;
	bool udp_tunnel_enable = false;
	netdev_features_t hw_features;

	pci_set_drvdata(pdev, ndev);

	ndev->mem_start = edev->dev_info.common.pci_mem_start;
	ndev->base_addr = ndev->mem_start;
	ndev->mem_end = edev->dev_info.common.pci_mem_end;
	ndev->irq = edev->dev_info.common.pci_irq;

	ndev->watchdog_timeo = TX_TIMEOUT;

	if (IS_VF(edev)) {
		if (edev->dev_info.xdp_supported)
			ndev->netdev_ops = &qede_netdev_vf_xdp_ops;
		else
			ndev->netdev_ops = &qede_netdev_vf_ops;
	} else {
		ndev->netdev_ops = &qede_netdev_ops;
	}

	qede_set_ethtool_ops(ndev);

	ndev->priv_flags |= IFF_UNICAST_FLT;

	/* user-changeble features */
	hw_features = NETIF_F_GRO | NETIF_F_GRO_HW | NETIF_F_SG |
		      NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		      NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_HW_TC;

	if (!IS_VF(edev) && edev->dev_info.common.num_hwfns == 1)
		hw_features |= NETIF_F_NTUPLE;

	if (edev->dev_info.common.vxlan_enable ||
	    edev->dev_info.common.geneve_enable)
		udp_tunnel_enable = true;

	if (udp_tunnel_enable || edev->dev_info.common.gre_enable) {
		hw_features |= NETIF_F_TSO_ECN;
		ndev->hw_enc_features = NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
					NETIF_F_SG | NETIF_F_TSO |
					NETIF_F_TSO_ECN | NETIF_F_TSO6 |
					NETIF_F_RXCSUM;
	}

	if (udp_tunnel_enable) {
		hw_features |= (NETIF_F_GSO_UDP_TUNNEL |
				NETIF_F_GSO_UDP_TUNNEL_CSUM);
		ndev->hw_enc_features |= (NETIF_F_GSO_UDP_TUNNEL |
					  NETIF_F_GSO_UDP_TUNNEL_CSUM);
	}

	if (edev->dev_info.common.gre_enable) {
		hw_features |= (NETIF_F_GSO_GRE | NETIF_F_GSO_GRE_CSUM);
		ndev->hw_enc_features |= (NETIF_F_GSO_GRE |
					  NETIF_F_GSO_GRE_CSUM);
	}

	ndev->vlan_features = hw_features | NETIF_F_RXHASH | NETIF_F_RXCSUM |
			      NETIF_F_HIGHDMA;
	ndev->features = hw_features | NETIF_F_RXHASH | NETIF_F_RXCSUM |
			 NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HIGHDMA |
			 NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_CTAG_TX;

	ndev->hw_features = hw_features;

	/* MTU range: 46 - 9600 */
	ndev->min_mtu = ETH_ZLEN - ETH_HLEN;
	ndev->max_mtu = QEDE_MAX_JUMBO_PACKET_SIZE;

	/* Set network device HW mac */
	ether_addr_copy(edev->ndev->dev_addr, edev->dev_info.common.hw_mac);

	ndev->mtu = edev->dev_info.common.mtu;
}

/* This function converts from 32b param to two params of level and module
 * Input 32b decoding:
 * b31 - enable all NOTICE prints. NOTICE prints are for deviation from the
 * 'happy' flow, e.g. memory allocation failed.
 * b30 - enable all INFO prints. INFO prints are for major steps in the flow
 * and provide important parameters.
 * b29-b0 - per-module bitmap, where each bit enables VERBOSE prints of that
 * module. VERBOSE prints are for tracking the specific flow in low level.
 *
 * Notice that the level should be that of the lowest required logs.
 */
void qede_config_debug(uint debug, u32 *p_dp_module, u8 *p_dp_level)
{
	*p_dp_level = QED_LEVEL_NOTICE;
	*p_dp_module = 0;

	if (debug & QED_LOG_VERBOSE_MASK) {
		*p_dp_level = QED_LEVEL_VERBOSE;
		*p_dp_module = (debug & 0x3FFFFFFF);
	} else if (debug & QED_LOG_INFO_MASK) {
		*p_dp_level = QED_LEVEL_INFO;
	} else if (debug & QED_LOG_NOTICE_MASK) {
		*p_dp_level = QED_LEVEL_NOTICE;
	}
}

static void qede_free_fp_array(struct qede_dev *edev)
{
	if (edev->fp_array) {
		struct qede_fastpath *fp;
		int i;

		for_each_queue(i) {
			fp = &edev->fp_array[i];

			kfree(fp->sb_info);
			/* Handle mem alloc failure case where qede_init_fp
			 * didn't register xdp_rxq_info yet.
			 * Implicit only (fp->type & QEDE_FASTPATH_RX)
			 */
			if (fp->rxq && xdp_rxq_info_is_reg(&fp->rxq->xdp_rxq))
				xdp_rxq_info_unreg(&fp->rxq->xdp_rxq);
			kfree(fp->rxq);
			kfree(fp->xdp_tx);
			kfree(fp->txq);
		}
		kfree(edev->fp_array);
	}

	edev->num_queues = 0;
	edev->fp_num_tx = 0;
	edev->fp_num_rx = 0;
}

static int qede_alloc_fp_array(struct qede_dev *edev)
{
	u8 fp_combined, fp_rx = edev->fp_num_rx;
	struct qede_fastpath *fp;
	int i;

	edev->fp_array = kcalloc(QEDE_QUEUE_CNT(edev),
				 sizeof(*edev->fp_array), GFP_KERNEL);
	if (!edev->fp_array) {
		DP_NOTICE(edev, "fp array allocation failed\n");
		goto err;
	}

	fp_combined = QEDE_QUEUE_CNT(edev) - fp_rx - edev->fp_num_tx;

	/* Allocate the FP elements for Rx queues followed by combined and then
	 * the Tx. This ordering should be maintained so that the respective
	 * queues (Rx or Tx) will be together in the fastpath array and the
	 * associated ids will be sequential.
	 */
	for_each_queue(i) {
		fp = &edev->fp_array[i];

		fp->sb_info = kzalloc(sizeof(*fp->sb_info), GFP_KERNEL);
		if (!fp->sb_info) {
			DP_NOTICE(edev, "sb info struct allocation failed\n");
			goto err;
		}

		if (fp_rx) {
			fp->type = QEDE_FASTPATH_RX;
			fp_rx--;
		} else if (fp_combined) {
			fp->type = QEDE_FASTPATH_COMBINED;
			fp_combined--;
		} else {
			fp->type = QEDE_FASTPATH_TX;
		}

		if (fp->type & QEDE_FASTPATH_TX) {
			fp->txq = kcalloc(edev->dev_info.num_tc,
					  sizeof(*fp->txq), GFP_KERNEL);
			if (!fp->txq)
				goto err;
		}

		if (fp->type & QEDE_FASTPATH_RX) {
			fp->rxq = kzalloc(sizeof(*fp->rxq), GFP_KERNEL);
			if (!fp->rxq)
				goto err;

			if (edev->xdp_prog) {
				fp->xdp_tx = kzalloc(sizeof(*fp->xdp_tx),
						     GFP_KERNEL);
				if (!fp->xdp_tx)
					goto err;
				fp->type |= QEDE_FASTPATH_XDP;
			}
		}
	}

	return 0;
err:
	qede_free_fp_array(edev);
	return -ENOMEM;
}

static void qede_sp_task(struct work_struct *work)
{
	struct qede_dev *edev = container_of(work, struct qede_dev,
					     sp_task.work);

	__qede_lock(edev);

	if (test_and_clear_bit(QEDE_SP_RX_MODE, &edev->sp_flags))
		if (edev->state == QEDE_STATE_OPEN)
			qede_config_rx_mode(edev->ndev);

#ifdef CONFIG_RFS_ACCEL
	if (test_and_clear_bit(QEDE_SP_ARFS_CONFIG, &edev->sp_flags)) {
		if (edev->state == QEDE_STATE_OPEN)
			qede_process_arfs_filters(edev, false);
	}
#endif
	__qede_unlock(edev);
}

static void qede_update_pf_params(struct qed_dev *cdev)
{
	struct qed_pf_params pf_params;
	u16 num_cons;

	/* 64 rx + 64 tx + 64 XDP */
	memset(&pf_params, 0, sizeof(struct qed_pf_params));

	/* 1 rx + 1 xdp + max tx cos */
	num_cons = QED_MIN_L2_CONS;

	pf_params.eth_pf_params.num_cons = (MAX_SB_PER_PF_MIMD - 1) * num_cons;

	/* Same for VFs - make sure they'll have sufficient connections
	 * to support XDP Tx queues.
	 */
	pf_params.eth_pf_params.num_vf_cons = 48;

	pf_params.eth_pf_params.num_arfs_filters = QEDE_RFS_MAX_FLTR;
	qed_ops->common->update_pf_params(cdev, &pf_params);
}

#define QEDE_FW_VER_STR_SIZE	80

static void qede_log_probe(struct qede_dev *edev)
{
	struct qed_dev_info *p_dev_info = &edev->dev_info.common;
	u8 buf[QEDE_FW_VER_STR_SIZE];
	size_t left_size;

	snprintf(buf, QEDE_FW_VER_STR_SIZE,
		 "Storm FW %d.%d.%d.%d, Management FW %d.%d.%d.%d",
		 p_dev_info->fw_major, p_dev_info->fw_minor, p_dev_info->fw_rev,
		 p_dev_info->fw_eng,
		 (p_dev_info->mfw_rev & QED_MFW_VERSION_3_MASK) >>
		 QED_MFW_VERSION_3_OFFSET,
		 (p_dev_info->mfw_rev & QED_MFW_VERSION_2_MASK) >>
		 QED_MFW_VERSION_2_OFFSET,
		 (p_dev_info->mfw_rev & QED_MFW_VERSION_1_MASK) >>
		 QED_MFW_VERSION_1_OFFSET,
		 (p_dev_info->mfw_rev & QED_MFW_VERSION_0_MASK) >>
		 QED_MFW_VERSION_0_OFFSET);

	left_size = QEDE_FW_VER_STR_SIZE - strlen(buf);
	if (p_dev_info->mbi_version && left_size)
		snprintf(buf + strlen(buf), left_size,
			 " [MBI %d.%d.%d]",
			 (p_dev_info->mbi_version & QED_MBI_VERSION_2_MASK) >>
			 QED_MBI_VERSION_2_OFFSET,
			 (p_dev_info->mbi_version & QED_MBI_VERSION_1_MASK) >>
			 QED_MBI_VERSION_1_OFFSET,
			 (p_dev_info->mbi_version & QED_MBI_VERSION_0_MASK) >>
			 QED_MBI_VERSION_0_OFFSET);

	pr_info("qede %02x:%02x.%02x: %s [%s]\n", edev->pdev->bus->number,
		PCI_SLOT(edev->pdev->devfn), PCI_FUNC(edev->pdev->devfn),
		buf, edev->ndev->name);
}

enum qede_probe_mode {
	QEDE_PROBE_NORMAL,
};

static int __qede_probe(struct pci_dev *pdev, u32 dp_module, u8 dp_level,
			bool is_vf, enum qede_probe_mode mode)
{
	struct qed_probe_params probe_params;
	struct qed_slowpath_params sp_params;
	struct qed_dev_eth_info dev_info;
	struct qede_dev *edev;
	struct qed_dev *cdev;
	int rc;

	if (unlikely(dp_level & QED_LEVEL_INFO))
		pr_notice("Starting qede probe\n");

	memset(&probe_params, 0, sizeof(probe_params));
	probe_params.protocol = QED_PROTOCOL_ETH;
	probe_params.dp_module = dp_module;
	probe_params.dp_level = dp_level;
	probe_params.is_vf = is_vf;
	cdev = qed_ops->common->probe(pdev, &probe_params);
	if (!cdev) {
		rc = -ENODEV;
		goto err0;
	}

	qede_update_pf_params(cdev);

	/* Start the Slowpath-process */
	memset(&sp_params, 0, sizeof(sp_params));
	sp_params.int_mode = QED_INT_MODE_MSIX;
	sp_params.drv_major = QEDE_MAJOR_VERSION;
	sp_params.drv_minor = QEDE_MINOR_VERSION;
	sp_params.drv_rev = QEDE_REVISION_VERSION;
	sp_params.drv_eng = QEDE_ENGINEERING_VERSION;
	strlcpy(sp_params.name, "qede LAN", QED_DRV_VER_STR_SIZE);
	rc = qed_ops->common->slowpath_start(cdev, &sp_params);
	if (rc) {
		pr_notice("Cannot start slowpath\n");
		goto err1;
	}

	/* Learn information crucial for qede to progress */
	rc = qed_ops->fill_dev_info(cdev, &dev_info);
	if (rc)
		goto err2;

	edev = qede_alloc_etherdev(cdev, pdev, &dev_info, dp_module,
				   dp_level);
	if (!edev) {
		rc = -ENOMEM;
		goto err2;
	}

	if (is_vf)
		edev->flags |= QEDE_FLAG_IS_VF;

	qede_init_ndev(edev);

	rc = qede_rdma_dev_add(edev);
	if (rc)
		goto err3;

	/* Prepare the lock prior to the registration of the netdev,
	 * as once it's registered we might reach flows requiring it
	 * [it's even possible to reach a flow needing it directly
	 * from there, although it's unlikely].
	 */
	INIT_DELAYED_WORK(&edev->sp_task, qede_sp_task);
	mutex_init(&edev->qede_lock);
	rc = register_netdev(edev->ndev);
	if (rc) {
		DP_NOTICE(edev, "Cannot register net-device\n");
		goto err4;
	}

	edev->ops->common->set_name(cdev, edev->ndev->name);

	/* PTP not supported on VFs */
	if (!is_vf)
		qede_ptp_enable(edev, true);

	edev->ops->register_ops(cdev, &qede_ll_ops, edev);

#ifdef CONFIG_DCB
	if (!IS_VF(edev))
		qede_set_dcbnl_ops(edev->ndev);
#endif

	edev->rx_copybreak = QEDE_RX_HDR_SIZE;

	qede_log_probe(edev);
	return 0;

err4:
	qede_rdma_dev_remove(edev);
err3:
	free_netdev(edev->ndev);
err2:
	qed_ops->common->slowpath_stop(cdev);
err1:
	qed_ops->common->remove(cdev);
err0:
	return rc;
}

static int qede_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	bool is_vf = false;
	u32 dp_module = 0;
	u8 dp_level = 0;

	switch ((enum qede_pci_private)id->driver_data) {
	case QEDE_PRIVATE_VF:
		if (debug & QED_LOG_VERBOSE_MASK)
			dev_err(&pdev->dev, "Probing a VF\n");
		is_vf = true;
		break;
	default:
		if (debug & QED_LOG_VERBOSE_MASK)
			dev_err(&pdev->dev, "Probing a PF\n");
	}

	qede_config_debug(debug, &dp_module, &dp_level);

	return __qede_probe(pdev, dp_module, dp_level, is_vf,
			    QEDE_PROBE_NORMAL);
}

enum qede_remove_mode {
	QEDE_REMOVE_NORMAL,
};

static void __qede_remove(struct pci_dev *pdev, enum qede_remove_mode mode)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	struct qede_dev *edev;
	struct qed_dev *cdev;

	if (!ndev) {
		dev_info(&pdev->dev, "Device has already been removed\n");
		return;
	}

	edev = netdev_priv(ndev);
	cdev = edev->cdev;

	DP_INFO(edev, "Starting qede_remove\n");

	qede_rdma_dev_remove(edev);
	unregister_netdev(ndev);
	cancel_delayed_work_sync(&edev->sp_task);

	qede_ptp_disable(edev);

	edev->ops->common->set_power_state(cdev, PCI_D0);

	pci_set_drvdata(pdev, NULL);

	/* Use global ops since we've freed edev */
	qed_ops->common->slowpath_stop(cdev);
	if (system_state == SYSTEM_POWER_OFF)
		return;
	qed_ops->common->remove(cdev);

	/* Since this can happen out-of-sync with other flows,
	 * don't release the netdevice until after slowpath stop
	 * has been called to guarantee various other contexts
	 * [e.g., QED register callbacks] won't break anything when
	 * accessing the netdevice.
	 */
	 free_netdev(ndev);

	dev_info(&pdev->dev, "Ending qede_remove successfully\n");
}

static void qede_remove(struct pci_dev *pdev)
{
	__qede_remove(pdev, QEDE_REMOVE_NORMAL);
}

static void qede_shutdown(struct pci_dev *pdev)
{
	__qede_remove(pdev, QEDE_REMOVE_NORMAL);
}

/* -------------------------------------------------------------------------
 * START OF LOAD / UNLOAD
 * -------------------------------------------------------------------------
 */

static int qede_set_num_queues(struct qede_dev *edev)
{
	int rc;
	u16 rss_num;

	/* Setup queues according to possible resources*/
	if (edev->req_queues)
		rss_num = edev->req_queues;
	else
		rss_num = netif_get_num_default_rss_queues() *
			  edev->dev_info.common.num_hwfns;

	rss_num = min_t(u16, QEDE_MAX_RSS_CNT(edev), rss_num);

	rc = edev->ops->common->set_fp_int(edev->cdev, rss_num);
	if (rc > 0) {
		/* Managed to request interrupts for our queues */
		edev->num_queues = rc;
		DP_INFO(edev, "Managed %d [of %d] RSS queues\n",
			QEDE_QUEUE_CNT(edev), rss_num);
		rc = 0;
	}

	edev->fp_num_tx = edev->req_num_tx;
	edev->fp_num_rx = edev->req_num_rx;

	return rc;
}

static void qede_free_mem_sb(struct qede_dev *edev, struct qed_sb_info *sb_info,
			     u16 sb_id)
{
	if (sb_info->sb_virt) {
		edev->ops->common->sb_release(edev->cdev, sb_info, sb_id);
		dma_free_coherent(&edev->pdev->dev, sizeof(*sb_info->sb_virt),
				  (void *)sb_info->sb_virt, sb_info->sb_phys);
		memset(sb_info, 0, sizeof(*sb_info));
	}
}

/* This function allocates fast-path status block memory */
static int qede_alloc_mem_sb(struct qede_dev *edev,
			     struct qed_sb_info *sb_info, u16 sb_id)
{
	struct status_block_e4 *sb_virt;
	dma_addr_t sb_phys;
	int rc;

	sb_virt = dma_alloc_coherent(&edev->pdev->dev,
				     sizeof(*sb_virt), &sb_phys, GFP_KERNEL);
	if (!sb_virt) {
		DP_ERR(edev, "Status block allocation failed\n");
		return -ENOMEM;
	}

	rc = edev->ops->common->sb_init(edev->cdev, sb_info,
					sb_virt, sb_phys, sb_id,
					QED_SB_TYPE_L2_QUEUE);
	if (rc) {
		DP_ERR(edev, "Status block initialization failed\n");
		dma_free_coherent(&edev->pdev->dev, sizeof(*sb_virt),
				  sb_virt, sb_phys);
		return rc;
	}

	return 0;
}

static void qede_free_rx_buffers(struct qede_dev *edev,
				 struct qede_rx_queue *rxq)
{
	u16 i;

	for (i = rxq->sw_rx_cons; i != rxq->sw_rx_prod; i++) {
		struct sw_rx_data *rx_buf;
		struct page *data;

		rx_buf = &rxq->sw_rx_ring[i & NUM_RX_BDS_MAX];
		data = rx_buf->data;

		dma_unmap_page(&edev->pdev->dev,
			       rx_buf->mapping, PAGE_SIZE, rxq->data_direction);

		rx_buf->data = NULL;
		__free_page(data);
	}
}

static void qede_free_mem_rxq(struct qede_dev *edev, struct qede_rx_queue *rxq)
{
	/* Free rx buffers */
	qede_free_rx_buffers(edev, rxq);

	/* Free the parallel SW ring */
	kfree(rxq->sw_rx_ring);

	/* Free the real RQ ring used by FW */
	edev->ops->common->chain_free(edev->cdev, &rxq->rx_bd_ring);
	edev->ops->common->chain_free(edev->cdev, &rxq->rx_comp_ring);
}

static void qede_set_tpa_param(struct qede_rx_queue *rxq)
{
	int i;

	for (i = 0; i < ETH_TPA_MAX_AGGS_NUM; i++) {
		struct qede_agg_info *tpa_info = &rxq->tpa_info[i];

		tpa_info->state = QEDE_AGG_STATE_NONE;
	}
}

/* This function allocates all memory needed per Rx queue */
static int qede_alloc_mem_rxq(struct qede_dev *edev, struct qede_rx_queue *rxq)
{
	int i, rc, size;

	rxq->num_rx_buffers = edev->q_num_rx_buffers;

	rxq->rx_buf_size = NET_IP_ALIGN + ETH_OVERHEAD + edev->ndev->mtu;

	rxq->rx_headroom = edev->xdp_prog ? XDP_PACKET_HEADROOM : NET_SKB_PAD;
	size = rxq->rx_headroom +
	       SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	/* Make sure that the headroom and  payload fit in a single page */
	if (rxq->rx_buf_size + size > PAGE_SIZE)
		rxq->rx_buf_size = PAGE_SIZE - size;

	/* Segment size to spilt a page in multiple equal parts ,
	 * unless XDP is used in which case we'd use the entire page.
	 */
	if (!edev->xdp_prog) {
		size = size + rxq->rx_buf_size;
		rxq->rx_buf_seg_size = roundup_pow_of_two(size);
	} else {
		rxq->rx_buf_seg_size = PAGE_SIZE;
	}

	/* Allocate the parallel driver ring for Rx buffers */
	size = sizeof(*rxq->sw_rx_ring) * RX_RING_SIZE;
	rxq->sw_rx_ring = kzalloc(size, GFP_KERNEL);
	if (!rxq->sw_rx_ring) {
		DP_ERR(edev, "Rx buffers ring allocation failed\n");
		rc = -ENOMEM;
		goto err;
	}

	/* Allocate FW Rx ring  */
	rc = edev->ops->common->chain_alloc(edev->cdev,
					    QED_CHAIN_USE_TO_CONSUME_PRODUCE,
					    QED_CHAIN_MODE_NEXT_PTR,
					    QED_CHAIN_CNT_TYPE_U16,
					    RX_RING_SIZE,
					    sizeof(struct eth_rx_bd),
					    &rxq->rx_bd_ring, NULL);
	if (rc)
		goto err;

	/* Allocate FW completion ring */
	rc = edev->ops->common->chain_alloc(edev->cdev,
					    QED_CHAIN_USE_TO_CONSUME,
					    QED_CHAIN_MODE_PBL,
					    QED_CHAIN_CNT_TYPE_U16,
					    RX_RING_SIZE,
					    sizeof(union eth_rx_cqe),
					    &rxq->rx_comp_ring, NULL);
	if (rc)
		goto err;

	/* Allocate buffers for the Rx ring */
	rxq->filled_buffers = 0;
	for (i = 0; i < rxq->num_rx_buffers; i++) {
		rc = qede_alloc_rx_buffer(rxq, false);
		if (rc) {
			DP_ERR(edev,
			       "Rx buffers allocation failed at index %d\n", i);
			goto err;
		}
	}

	if (!edev->gro_disable)
		qede_set_tpa_param(rxq);
err:
	return rc;
}

static void qede_free_mem_txq(struct qede_dev *edev, struct qede_tx_queue *txq)
{
	/* Free the parallel SW ring */
	if (txq->is_xdp)
		kfree(txq->sw_tx_ring.xdp);
	else
		kfree(txq->sw_tx_ring.skbs);

	/* Free the real RQ ring used by FW */
	edev->ops->common->chain_free(edev->cdev, &txq->tx_pbl);
}

/* This function allocates all memory needed per Tx queue */
static int qede_alloc_mem_txq(struct qede_dev *edev, struct qede_tx_queue *txq)
{
	union eth_tx_bd_types *p_virt;
	int size, rc;

	txq->num_tx_buffers = edev->q_num_tx_buffers;

	/* Allocate the parallel driver ring for Tx buffers */
	if (txq->is_xdp) {
		size = sizeof(*txq->sw_tx_ring.xdp) * txq->num_tx_buffers;
		txq->sw_tx_ring.xdp = kzalloc(size, GFP_KERNEL);
		if (!txq->sw_tx_ring.xdp)
			goto err;
	} else {
		size = sizeof(*txq->sw_tx_ring.skbs) * txq->num_tx_buffers;
		txq->sw_tx_ring.skbs = kzalloc(size, GFP_KERNEL);
		if (!txq->sw_tx_ring.skbs)
			goto err;
	}

	rc = edev->ops->common->chain_alloc(edev->cdev,
					    QED_CHAIN_USE_TO_CONSUME_PRODUCE,
					    QED_CHAIN_MODE_PBL,
					    QED_CHAIN_CNT_TYPE_U16,
					    txq->num_tx_buffers,
					    sizeof(*p_virt),
					    &txq->tx_pbl, NULL);
	if (rc)
		goto err;

	return 0;

err:
	qede_free_mem_txq(edev, txq);
	return -ENOMEM;
}

/* This function frees all memory of a single fp */
static void qede_free_mem_fp(struct qede_dev *edev, struct qede_fastpath *fp)
{
	qede_free_mem_sb(edev, fp->sb_info, fp->id);

	if (fp->type & QEDE_FASTPATH_RX)
		qede_free_mem_rxq(edev, fp->rxq);

	if (fp->type & QEDE_FASTPATH_XDP)
		qede_free_mem_txq(edev, fp->xdp_tx);

	if (fp->type & QEDE_FASTPATH_TX) {
		int cos;

		for_each_cos_in_txq(edev, cos)
			qede_free_mem_txq(edev, &fp->txq[cos]);
	}
}

/* This function allocates all memory needed for a single fp (i.e. an entity
 * which contains status block, one rx queue and/or multiple per-TC tx queues.
 */
static int qede_alloc_mem_fp(struct qede_dev *edev, struct qede_fastpath *fp)
{
	int rc = 0;

	rc = qede_alloc_mem_sb(edev, fp->sb_info, fp->id);
	if (rc)
		goto out;

	if (fp->type & QEDE_FASTPATH_RX) {
		rc = qede_alloc_mem_rxq(edev, fp->rxq);
		if (rc)
			goto out;
	}

	if (fp->type & QEDE_FASTPATH_XDP) {
		rc = qede_alloc_mem_txq(edev, fp->xdp_tx);
		if (rc)
			goto out;
	}

	if (fp->type & QEDE_FASTPATH_TX) {
		int cos;

		for_each_cos_in_txq(edev, cos) {
			rc = qede_alloc_mem_txq(edev, &fp->txq[cos]);
			if (rc)
				goto out;
		}
	}

out:
	return rc;
}

static void qede_free_mem_load(struct qede_dev *edev)
{
	int i;

	for_each_queue(i) {
		struct qede_fastpath *fp = &edev->fp_array[i];

		qede_free_mem_fp(edev, fp);
	}
}

/* This function allocates all qede memory at NIC load. */
static int qede_alloc_mem_load(struct qede_dev *edev)
{
	int rc = 0, queue_id;

	for (queue_id = 0; queue_id < QEDE_QUEUE_CNT(edev); queue_id++) {
		struct qede_fastpath *fp = &edev->fp_array[queue_id];

		rc = qede_alloc_mem_fp(edev, fp);
		if (rc) {
			DP_ERR(edev,
			       "Failed to allocate memory for fastpath - rss id = %d\n",
			       queue_id);
			qede_free_mem_load(edev);
			return rc;
		}
	}

	return 0;
}

/* This function inits fp content and resets the SB, RXQ and TXQ structures */
static void qede_init_fp(struct qede_dev *edev)
{
	int queue_id, rxq_index = 0, txq_index = 0;
	struct qede_fastpath *fp;

	for_each_queue(queue_id) {
		fp = &edev->fp_array[queue_id];

		fp->edev = edev;
		fp->id = queue_id;

		if (fp->type & QEDE_FASTPATH_XDP) {
			fp->xdp_tx->index = QEDE_TXQ_IDX_TO_XDP(edev,
								rxq_index);
			fp->xdp_tx->is_xdp = 1;
		}

		if (fp->type & QEDE_FASTPATH_RX) {
			fp->rxq->rxq_id = rxq_index++;

			/* Determine how to map buffers for this queue */
			if (fp->type & QEDE_FASTPATH_XDP)
				fp->rxq->data_direction = DMA_BIDIRECTIONAL;
			else
				fp->rxq->data_direction = DMA_FROM_DEVICE;
			fp->rxq->dev = &edev->pdev->dev;

			/* Driver have no error path from here */
			WARN_ON(xdp_rxq_info_reg(&fp->rxq->xdp_rxq, edev->ndev,
						 fp->rxq->rxq_id) < 0);
		}

		if (fp->type & QEDE_FASTPATH_TX) {
			int cos;

			for_each_cos_in_txq(edev, cos) {
				struct qede_tx_queue *txq = &fp->txq[cos];
				u16 ndev_tx_id;

				txq->cos = cos;
				txq->index = txq_index;
				ndev_tx_id = QEDE_TXQ_TO_NDEV_TXQ_ID(edev, txq);
				txq->ndev_txq_id = ndev_tx_id;

				if (edev->dev_info.is_legacy)
					txq->is_legacy = 1;
				txq->dev = &edev->pdev->dev;
			}

			txq_index++;
		}

		snprintf(fp->name, sizeof(fp->name), "%s-fp-%d",
			 edev->ndev->name, queue_id);
	}

	edev->gro_disable = !(edev->ndev->features & NETIF_F_GRO_HW);
}

static int qede_set_real_num_queues(struct qede_dev *edev)
{
	int rc = 0;

	rc = netif_set_real_num_tx_queues(edev->ndev,
					  QEDE_TSS_COUNT(edev) *
					  edev->dev_info.num_tc);
	if (rc) {
		DP_NOTICE(edev, "Failed to set real number of Tx queues\n");
		return rc;
	}

	rc = netif_set_real_num_rx_queues(edev->ndev, QEDE_RSS_COUNT(edev));
	if (rc) {
		DP_NOTICE(edev, "Failed to set real number of Rx queues\n");
		return rc;
	}

	return 0;
}

static void qede_napi_disable_remove(struct qede_dev *edev)
{
	int i;

	for_each_queue(i) {
		napi_disable(&edev->fp_array[i].napi);

		netif_napi_del(&edev->fp_array[i].napi);
	}
}

static void qede_napi_add_enable(struct qede_dev *edev)
{
	int i;

	/* Add NAPI objects */
	for_each_queue(i) {
		netif_napi_add(edev->ndev, &edev->fp_array[i].napi,
			       qede_poll, NAPI_POLL_WEIGHT);
		napi_enable(&edev->fp_array[i].napi);
	}
}

static void qede_sync_free_irqs(struct qede_dev *edev)
{
	int i;

	for (i = 0; i < edev->int_info.used_cnt; i++) {
		if (edev->int_info.msix_cnt) {
			synchronize_irq(edev->int_info.msix[i].vector);
			free_irq(edev->int_info.msix[i].vector,
				 &edev->fp_array[i]);
		} else {
			edev->ops->common->simd_handler_clean(edev->cdev, i);
		}
	}

	edev->int_info.used_cnt = 0;
}

static int qede_req_msix_irqs(struct qede_dev *edev)
{
	int i, rc;

	/* Sanitize number of interrupts == number of prepared RSS queues */
	if (QEDE_QUEUE_CNT(edev) > edev->int_info.msix_cnt) {
		DP_ERR(edev,
		       "Interrupt mismatch: %d RSS queues > %d MSI-x vectors\n",
		       QEDE_QUEUE_CNT(edev), edev->int_info.msix_cnt);
		return -EINVAL;
	}

	for (i = 0; i < QEDE_QUEUE_CNT(edev); i++) {
#ifdef CONFIG_RFS_ACCEL
		struct qede_fastpath *fp = &edev->fp_array[i];

		if (edev->ndev->rx_cpu_rmap && (fp->type & QEDE_FASTPATH_RX)) {
			rc = irq_cpu_rmap_add(edev->ndev->rx_cpu_rmap,
					      edev->int_info.msix[i].vector);
			if (rc) {
				DP_ERR(edev, "Failed to add CPU rmap\n");
				qede_free_arfs(edev);
			}
		}
#endif
		rc = request_irq(edev->int_info.msix[i].vector,
				 qede_msix_fp_int, 0, edev->fp_array[i].name,
				 &edev->fp_array[i]);
		if (rc) {
			DP_ERR(edev, "Request fp %d irq failed\n", i);
			qede_sync_free_irqs(edev);
			return rc;
		}
		DP_VERBOSE(edev, NETIF_MSG_INTR,
			   "Requested fp irq for %s [entry %d]. Cookie is at %p\n",
			   edev->fp_array[i].name, i,
			   &edev->fp_array[i]);
		edev->int_info.used_cnt++;
	}

	return 0;
}

static void qede_simd_fp_handler(void *cookie)
{
	struct qede_fastpath *fp = (struct qede_fastpath *)cookie;

	napi_schedule_irqoff(&fp->napi);
}

static int qede_setup_irqs(struct qede_dev *edev)
{
	int i, rc = 0;

	/* Learn Interrupt configuration */
	rc = edev->ops->common->get_fp_int(edev->cdev, &edev->int_info);
	if (rc)
		return rc;

	if (edev->int_info.msix_cnt) {
		rc = qede_req_msix_irqs(edev);
		if (rc)
			return rc;
		edev->ndev->irq = edev->int_info.msix[0].vector;
	} else {
		const struct qed_common_ops *ops;

		/* qed should learn receive the RSS ids and callbacks */
		ops = edev->ops->common;
		for (i = 0; i < QEDE_QUEUE_CNT(edev); i++)
			ops->simd_handler_config(edev->cdev,
						 &edev->fp_array[i], i,
						 qede_simd_fp_handler);
		edev->int_info.used_cnt = QEDE_QUEUE_CNT(edev);
	}
	return 0;
}

static int qede_drain_txq(struct qede_dev *edev,
			  struct qede_tx_queue *txq, bool allow_drain)
{
	int rc, cnt = 1000;

	while (txq->sw_tx_cons != txq->sw_tx_prod) {
		if (!cnt) {
			if (allow_drain) {
				DP_NOTICE(edev,
					  "Tx queue[%d] is stuck, requesting MCP to drain\n",
					  txq->index);
				rc = edev->ops->common->drain(edev->cdev);
				if (rc)
					return rc;
				return qede_drain_txq(edev, txq, false);
			}
			DP_NOTICE(edev,
				  "Timeout waiting for tx queue[%d]: PROD=%d, CONS=%d\n",
				  txq->index, txq->sw_tx_prod,
				  txq->sw_tx_cons);
			return -ENODEV;
		}
		cnt--;
		usleep_range(1000, 2000);
		barrier();
	}

	/* FW finished processing, wait for HW to transmit all tx packets */
	usleep_range(1000, 2000);

	return 0;
}

static int qede_stop_txq(struct qede_dev *edev,
			 struct qede_tx_queue *txq, int rss_id)
{
	return edev->ops->q_tx_stop(edev->cdev, rss_id, txq->handle);
}

static int qede_stop_queues(struct qede_dev *edev)
{
	struct qed_update_vport_params *vport_update_params;
	struct qed_dev *cdev = edev->cdev;
	struct qede_fastpath *fp;
	int rc, i;

	/* Disable the vport */
	vport_update_params = vzalloc(sizeof(*vport_update_params));
	if (!vport_update_params)
		return -ENOMEM;

	vport_update_params->vport_id = 0;
	vport_update_params->update_vport_active_flg = 1;
	vport_update_params->vport_active_flg = 0;
	vport_update_params->update_rss_flg = 0;

	rc = edev->ops->vport_update(cdev, vport_update_params);
	vfree(vport_update_params);

	if (rc) {
		DP_ERR(edev, "Failed to update vport\n");
		return rc;
	}

	/* Flush Tx queues. If needed, request drain from MCP */
	for_each_queue(i) {
		fp = &edev->fp_array[i];

		if (fp->type & QEDE_FASTPATH_TX) {
			int cos;

			for_each_cos_in_txq(edev, cos) {
				rc = qede_drain_txq(edev, &fp->txq[cos], true);
				if (rc)
					return rc;
			}
		}

		if (fp->type & QEDE_FASTPATH_XDP) {
			rc = qede_drain_txq(edev, fp->xdp_tx, true);
			if (rc)
				return rc;
		}
	}

	/* Stop all Queues in reverse order */
	for (i = QEDE_QUEUE_CNT(edev) - 1; i >= 0; i--) {
		fp = &edev->fp_array[i];

		/* Stop the Tx Queue(s) */
		if (fp->type & QEDE_FASTPATH_TX) {
			int cos;

			for_each_cos_in_txq(edev, cos) {
				rc = qede_stop_txq(edev, &fp->txq[cos], i);
				if (rc)
					return rc;
			}
		}

		/* Stop the Rx Queue */
		if (fp->type & QEDE_FASTPATH_RX) {
			rc = edev->ops->q_rx_stop(cdev, i, fp->rxq->handle);
			if (rc) {
				DP_ERR(edev, "Failed to stop RXQ #%d\n", i);
				return rc;
			}
		}

		/* Stop the XDP forwarding queue */
		if (fp->type & QEDE_FASTPATH_XDP) {
			rc = qede_stop_txq(edev, fp->xdp_tx, i);
			if (rc)
				return rc;

			bpf_prog_put(fp->rxq->xdp_prog);
		}
	}

	/* Stop the vport */
	rc = edev->ops->vport_stop(cdev, 0);
	if (rc)
		DP_ERR(edev, "Failed to stop VPORT\n");

	return rc;
}

static int qede_start_txq(struct qede_dev *edev,
			  struct qede_fastpath *fp,
			  struct qede_tx_queue *txq, u8 rss_id, u16 sb_idx)
{
	dma_addr_t phys_table = qed_chain_get_pbl_phys(&txq->tx_pbl);
	u32 page_cnt = qed_chain_get_page_cnt(&txq->tx_pbl);
	struct qed_queue_start_common_params params;
	struct qed_txq_start_ret_params ret_params;
	int rc;

	memset(&params, 0, sizeof(params));
	memset(&ret_params, 0, sizeof(ret_params));

	/* Let the XDP queue share the queue-zone with one of the regular txq.
	 * We don't really care about its coalescing.
	 */
	if (txq->is_xdp)
		params.queue_id = QEDE_TXQ_XDP_TO_IDX(edev, txq);
	else
		params.queue_id = txq->index;

	params.p_sb = fp->sb_info;
	params.sb_idx = sb_idx;
	params.tc = txq->cos;

	rc = edev->ops->q_tx_start(edev->cdev, rss_id, &params, phys_table,
				   page_cnt, &ret_params);
	if (rc) {
		DP_ERR(edev, "Start TXQ #%d failed %d\n", txq->index, rc);
		return rc;
	}

	txq->doorbell_addr = ret_params.p_doorbell;
	txq->handle = ret_params.p_handle;

	/* Determine the FW consumer address associated */
	txq->hw_cons_ptr = &fp->sb_info->sb_virt->pi_array[sb_idx];

	/* Prepare the doorbell parameters */
	SET_FIELD(txq->tx_db.data.params, ETH_DB_DATA_DEST, DB_DEST_XCM);
	SET_FIELD(txq->tx_db.data.params, ETH_DB_DATA_AGG_CMD, DB_AGG_CMD_SET);
	SET_FIELD(txq->tx_db.data.params, ETH_DB_DATA_AGG_VAL_SEL,
		  DQ_XCM_ETH_TX_BD_PROD_CMD);
	txq->tx_db.data.agg_flags = DQ_XCM_ETH_DQ_CF_CMD;

	return rc;
}

static int qede_start_queues(struct qede_dev *edev, bool clear_stats)
{
	int vlan_removal_en = 1;
	struct qed_dev *cdev = edev->cdev;
	struct qed_dev_info *qed_info = &edev->dev_info.common;
	struct qed_update_vport_params *vport_update_params;
	struct qed_queue_start_common_params q_params;
	struct qed_start_vport_params start = {0};
	int rc, i;

	if (!edev->num_queues) {
		DP_ERR(edev,
		       "Cannot update V-VPORT as active as there are no Rx queues\n");
		return -EINVAL;
	}

	vport_update_params = vzalloc(sizeof(*vport_update_params));
	if (!vport_update_params)
		return -ENOMEM;

	start.handle_ptp_pkts = !!(edev->ptp);
	start.gro_enable = !edev->gro_disable;
	start.mtu = edev->ndev->mtu;
	start.vport_id = 0;
	start.drop_ttl0 = true;
	start.remove_inner_vlan = vlan_removal_en;
	start.clear_stats = clear_stats;

	rc = edev->ops->vport_start(cdev, &start);

	if (rc) {
		DP_ERR(edev, "Start V-PORT failed %d\n", rc);
		goto out;
	}

	DP_VERBOSE(edev, NETIF_MSG_IFUP,
		   "Start vport ramrod passed, vport_id = %d, MTU = %d, vlan_removal_en = %d\n",
		   start.vport_id, edev->ndev->mtu + 0xe, vlan_removal_en);

	for_each_queue(i) {
		struct qede_fastpath *fp = &edev->fp_array[i];
		dma_addr_t p_phys_table;
		u32 page_cnt;

		if (fp->type & QEDE_FASTPATH_RX) {
			struct qed_rxq_start_ret_params ret_params;
			struct qede_rx_queue *rxq = fp->rxq;
			__le16 *val;

			memset(&ret_params, 0, sizeof(ret_params));
			memset(&q_params, 0, sizeof(q_params));
			q_params.queue_id = rxq->rxq_id;
			q_params.vport_id = 0;
			q_params.p_sb = fp->sb_info;
			q_params.sb_idx = RX_PI;

			p_phys_table =
			    qed_chain_get_pbl_phys(&rxq->rx_comp_ring);
			page_cnt = qed_chain_get_page_cnt(&rxq->rx_comp_ring);

			rc = edev->ops->q_rx_start(cdev, i, &q_params,
						   rxq->rx_buf_size,
						   rxq->rx_bd_ring.p_phys_addr,
						   p_phys_table,
						   page_cnt, &ret_params);
			if (rc) {
				DP_ERR(edev, "Start RXQ #%d failed %d\n", i,
				       rc);
				goto out;
			}

			/* Use the return parameters */
			rxq->hw_rxq_prod_addr = ret_params.p_prod;
			rxq->handle = ret_params.p_handle;

			val = &fp->sb_info->sb_virt->pi_array[RX_PI];
			rxq->hw_cons_ptr = val;

			qede_update_rx_prod(edev, rxq);
		}

		if (fp->type & QEDE_FASTPATH_XDP) {
			rc = qede_start_txq(edev, fp, fp->xdp_tx, i, XDP_PI);
			if (rc)
				goto out;

			fp->rxq->xdp_prog = bpf_prog_add(edev->xdp_prog, 1);
			if (IS_ERR(fp->rxq->xdp_prog)) {
				rc = PTR_ERR(fp->rxq->xdp_prog);
				fp->rxq->xdp_prog = NULL;
				goto out;
			}
		}

		if (fp->type & QEDE_FASTPATH_TX) {
			int cos;

			for_each_cos_in_txq(edev, cos) {
				rc = qede_start_txq(edev, fp, &fp->txq[cos], i,
						    TX_PI(cos));
				if (rc)
					goto out;
			}
		}
	}

	/* Prepare and send the vport enable */
	vport_update_params->vport_id = start.vport_id;
	vport_update_params->update_vport_active_flg = 1;
	vport_update_params->vport_active_flg = 1;

	if ((qed_info->b_inter_pf_switch || pci_num_vf(edev->pdev)) &&
	    qed_info->tx_switching) {
		vport_update_params->update_tx_switching_flg = 1;
		vport_update_params->tx_switching_flg = 1;
	}

	qede_fill_rss_params(edev, &vport_update_params->rss_params,
			     &vport_update_params->update_rss_flg);

	rc = edev->ops->vport_update(cdev, vport_update_params);
	if (rc)
		DP_ERR(edev, "Update V-PORT failed %d\n", rc);

out:
	vfree(vport_update_params);
	return rc;
}

enum qede_unload_mode {
	QEDE_UNLOAD_NORMAL,
};

static void qede_unload(struct qede_dev *edev, enum qede_unload_mode mode,
			bool is_locked)
{
	struct qed_link_params link_params;
	int rc;

	DP_INFO(edev, "Starting qede unload\n");

	if (!is_locked)
		__qede_lock(edev);

	edev->state = QEDE_STATE_CLOSED;

	qede_rdma_dev_event_close(edev);

	/* Close OS Tx */
	netif_tx_disable(edev->ndev);
	netif_carrier_off(edev->ndev);

	/* Reset the link */
	memset(&link_params, 0, sizeof(link_params));
	link_params.link_up = false;
	edev->ops->common->set_link(edev->cdev, &link_params);
	rc = qede_stop_queues(edev);
	if (rc) {
		qede_sync_free_irqs(edev);
		goto out;
	}

	DP_INFO(edev, "Stopped Queues\n");

	qede_vlan_mark_nonconfigured(edev);
	edev->ops->fastpath_stop(edev->cdev);

	if (!IS_VF(edev) && edev->dev_info.common.num_hwfns == 1) {
		qede_poll_for_freeing_arfs_filters(edev);
		qede_free_arfs(edev);
	}

	/* Release the interrupts */
	qede_sync_free_irqs(edev);
	edev->ops->common->set_fp_int(edev->cdev, 0);

	qede_napi_disable_remove(edev);

	qede_free_mem_load(edev);
	qede_free_fp_array(edev);

out:
	if (!is_locked)
		__qede_unlock(edev);
	DP_INFO(edev, "Ending qede unload\n");
}

enum qede_load_mode {
	QEDE_LOAD_NORMAL,
	QEDE_LOAD_RELOAD,
};

static int qede_load(struct qede_dev *edev, enum qede_load_mode mode,
		     bool is_locked)
{
	struct qed_link_params link_params;
	u8 num_tc;
	int rc;

	DP_INFO(edev, "Starting qede load\n");

	if (!is_locked)
		__qede_lock(edev);

	rc = qede_set_num_queues(edev);
	if (rc)
		goto out;

	rc = qede_alloc_fp_array(edev);
	if (rc)
		goto out;

	qede_init_fp(edev);

	rc = qede_alloc_mem_load(edev);
	if (rc)
		goto err1;
	DP_INFO(edev, "Allocated %d Rx, %d Tx queues\n",
		QEDE_RSS_COUNT(edev), QEDE_TSS_COUNT(edev));

	rc = qede_set_real_num_queues(edev);
	if (rc)
		goto err2;

	if (!IS_VF(edev) && edev->dev_info.common.num_hwfns == 1) {
		rc = qede_alloc_arfs(edev);
		if (rc)
			DP_NOTICE(edev, "aRFS memory allocation failed\n");
	}

	qede_napi_add_enable(edev);
	DP_INFO(edev, "Napi added and enabled\n");

	rc = qede_setup_irqs(edev);
	if (rc)
		goto err3;
	DP_INFO(edev, "Setup IRQs succeeded\n");

	rc = qede_start_queues(edev, mode != QEDE_LOAD_RELOAD);
	if (rc)
		goto err4;
	DP_INFO(edev, "Start VPORT, RXQ and TXQ succeeded\n");

	num_tc = netdev_get_num_tc(edev->ndev);
	num_tc = num_tc ? num_tc : edev->dev_info.num_tc;
	qede_setup_tc(edev->ndev, num_tc);

	/* Program un-configured VLANs */
	qede_configure_vlan_filters(edev);

	/* Ask for link-up using current configuration */
	memset(&link_params, 0, sizeof(link_params));
	link_params.link_up = true;
	edev->ops->common->set_link(edev->cdev, &link_params);

	edev->state = QEDE_STATE_OPEN;

	DP_INFO(edev, "Ending successfully qede load\n");

	goto out;
err4:
	qede_sync_free_irqs(edev);
	memset(&edev->int_info.msix_cnt, 0, sizeof(struct qed_int_info));
err3:
	qede_napi_disable_remove(edev);
err2:
	qede_free_mem_load(edev);
err1:
	edev->ops->common->set_fp_int(edev->cdev, 0);
	qede_free_fp_array(edev);
	edev->num_queues = 0;
	edev->fp_num_tx = 0;
	edev->fp_num_rx = 0;
out:
	if (!is_locked)
		__qede_unlock(edev);

	return rc;
}

/* 'func' should be able to run between unload and reload assuming interface
 * is actually running, or afterwards in case it's currently DOWN.
 */
void qede_reload(struct qede_dev *edev,
		 struct qede_reload_args *args, bool is_locked)
{
	if (!is_locked)
		__qede_lock(edev);

	/* Since qede_lock is held, internal state wouldn't change even
	 * if netdev state would start transitioning. Check whether current
	 * internal configuration indicates device is up, then reload.
	 */
	if (edev->state == QEDE_STATE_OPEN) {
		qede_unload(edev, QEDE_UNLOAD_NORMAL, true);
		if (args)
			args->func(edev, args);
		qede_load(edev, QEDE_LOAD_RELOAD, true);

		/* Since no one is going to do it for us, re-configure */
		qede_config_rx_mode(edev->ndev);
	} else if (args) {
		args->func(edev, args);
	}

	if (!is_locked)
		__qede_unlock(edev);
}

/* called with rtnl_lock */
static int qede_open(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);
	int rc;

	netif_carrier_off(ndev);

	edev->ops->common->set_power_state(edev->cdev, PCI_D0);

	rc = qede_load(edev, QEDE_LOAD_NORMAL, false);
	if (rc)
		return rc;

	udp_tunnel_get_rx_info(ndev);

	edev->ops->common->update_drv_state(edev->cdev, true);

	return 0;
}

static int qede_close(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);

	qede_unload(edev, QEDE_UNLOAD_NORMAL, false);

	edev->ops->common->update_drv_state(edev->cdev, false);

	return 0;
}

static void qede_link_update(void *dev, struct qed_link_output *link)
{
	struct qede_dev *edev = dev;

	if (!netif_running(edev->ndev)) {
		DP_VERBOSE(edev, NETIF_MSG_LINK, "Interface is not running\n");
		return;
	}

	if (link->link_up) {
		if (!netif_carrier_ok(edev->ndev)) {
			DP_NOTICE(edev, "Link is up\n");
			netif_tx_start_all_queues(edev->ndev);
			netif_carrier_on(edev->ndev);
			qede_rdma_dev_event_open(edev);
		}
	} else {
		if (netif_carrier_ok(edev->ndev)) {
			DP_NOTICE(edev, "Link is down\n");
			netif_tx_disable(edev->ndev);
			netif_carrier_off(edev->ndev);
			qede_rdma_dev_event_close(edev);
		}
	}
}

static bool qede_is_txq_full(struct qede_dev *edev, struct qede_tx_queue *txq)
{
	struct netdev_queue *netdev_txq;

	netdev_txq = netdev_get_tx_queue(edev->ndev, txq->ndev_txq_id);
	if (netif_xmit_stopped(netdev_txq))
		return true;

	return false;
}

static void qede_get_generic_tlv_data(void *dev, struct qed_generic_tlvs *data)
{
	struct qede_dev *edev = dev;
	struct netdev_hw_addr *ha;
	int i;

	if (edev->ndev->features & NETIF_F_IP_CSUM)
		data->feat_flags |= QED_TLV_IP_CSUM;
	if (edev->ndev->features & NETIF_F_TSO)
		data->feat_flags |= QED_TLV_LSO;

	ether_addr_copy(data->mac[0], edev->ndev->dev_addr);
	memset(data->mac[1], 0, ETH_ALEN);
	memset(data->mac[2], 0, ETH_ALEN);
	/* Copy the first two UC macs */
	netif_addr_lock_bh(edev->ndev);
	i = 1;
	netdev_for_each_uc_addr(ha, edev->ndev) {
		ether_addr_copy(data->mac[i++], ha->addr);
		if (i == QED_TLV_MAC_COUNT)
			break;
	}

	netif_addr_unlock_bh(edev->ndev);
}

static void qede_get_eth_tlv_data(void *dev, void *data)
{
	struct qed_mfw_tlv_eth *etlv = data;
	struct qede_dev *edev = dev;
	struct qede_fastpath *fp;
	int i;

	etlv->lso_maxoff_size = 0XFFFF;
	etlv->lso_maxoff_size_set = true;
	etlv->lso_minseg_size = (u16)ETH_TX_LSO_WINDOW_MIN_LEN;
	etlv->lso_minseg_size_set = true;
	etlv->prom_mode = !!(edev->ndev->flags & IFF_PROMISC);
	etlv->prom_mode_set = true;
	etlv->tx_descr_size = QEDE_TSS_COUNT(edev);
	etlv->tx_descr_size_set = true;
	etlv->rx_descr_size = QEDE_RSS_COUNT(edev);
	etlv->rx_descr_size_set = true;
	etlv->iov_offload = QED_MFW_TLV_IOV_OFFLOAD_VEB;
	etlv->iov_offload_set = true;

	/* Fill information regarding queues; Should be done under the qede
	 * lock to guarantee those don't change beneath our feet.
	 */
	etlv->txqs_empty = true;
	etlv->rxqs_empty = true;
	etlv->num_txqs_full = 0;
	etlv->num_rxqs_full = 0;

	__qede_lock(edev);
	for_each_queue(i) {
		fp = &edev->fp_array[i];
		if (fp->type & QEDE_FASTPATH_TX) {
			struct qede_tx_queue *txq = QEDE_FP_TC0_TXQ(fp);

			if (txq->sw_tx_cons != txq->sw_tx_prod)
				etlv->txqs_empty = false;
			if (qede_is_txq_full(edev, txq))
				etlv->num_txqs_full++;
		}
		if (fp->type & QEDE_FASTPATH_RX) {
			if (qede_has_rx_work(fp->rxq))
				etlv->rxqs_empty = false;

			/* This one is a bit tricky; Firmware might stop
			 * placing packets if ring is not yet full.
			 * Give an approximation.
			 */
			if (le16_to_cpu(*fp->rxq->hw_cons_ptr) -
			    qed_chain_get_cons_idx(&fp->rxq->rx_comp_ring) >
			    RX_RING_SIZE - 100)
				etlv->num_rxqs_full++;
		}
	}
	__qede_unlock(edev);

	etlv->txqs_empty_set = true;
	etlv->rxqs_empty_set = true;
	etlv->num_txqs_full_set = true;
	etlv->num_rxqs_full_set = true;
}
