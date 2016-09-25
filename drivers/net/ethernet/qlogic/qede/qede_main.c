/* QLogic qede NIC Driver
* Copyright (c) 2015 QLogic Corporation
*
* This software is available under the terms of the GNU General Public License
* (GPL) Version 2, available from the file COPYING in the main directory of
* this source tree.
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

#include "qede.h"

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

#ifndef PCI_DEVICE_ID_NX2_57980E
#define PCI_DEVICE_ID_57980S_40		CHIP_NUM_57980S_40
#define PCI_DEVICE_ID_57980S_10		CHIP_NUM_57980S_10
#define PCI_DEVICE_ID_57980S_MF		CHIP_NUM_57980S_MF
#define PCI_DEVICE_ID_57980S_100	CHIP_NUM_57980S_100
#define PCI_DEVICE_ID_57980S_50		CHIP_NUM_57980S_50
#define PCI_DEVICE_ID_57980S_25		CHIP_NUM_57980S_25
#define PCI_DEVICE_ID_57980S_IOV	CHIP_NUM_57980S_IOV
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
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, qede_pci_tbl);

static int qede_probe(struct pci_dev *pdev, const struct pci_device_id *id);

#define TX_TIMEOUT		(5 * HZ)

static void qede_remove(struct pci_dev *pdev);
static int qede_alloc_rx_buffer(struct qede_dev *edev,
				struct qede_rx_queue *rxq);
static void qede_link_update(void *dev, struct qed_link_output *link);

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
	int rc;

	DP_VERBOSE(edev, QED_MSG_IOV, "Requested %d VFs\n", num_vfs_param);

	rc = edev->ops->iov->configure(edev->cdev, num_vfs_param);

	/* Enable/Disable Tx switching for PF */
	if ((rc == num_vfs_param) && netif_running(edev->ndev) &&
	    qed_info->mf_mode != QED_MF_NPAR && qed_info->tx_switching) {
		struct qed_update_vport_params params;

		memset(&params, 0, sizeof(params));
		params.vport_id = 0;
		params.update_tx_switching_flg = 1;
		params.tx_switching_flg = num_vfs_param ? 1 : 0;
		edev->ops->vport_update(edev->cdev, &params);
	}

	return rc;
}
#endif

static struct pci_driver qede_pci_driver = {
	.name = "qede",
	.id_table = qede_pci_tbl,
	.probe = qede_probe,
	.remove = qede_remove,
#ifdef CONFIG_QED_SRIOV
	.sriov_configure = qede_sriov_configure,
#endif
};

static void qede_force_mac(void *dev, u8 *mac)
{
	struct qede_dev *edev = dev;

	ether_addr_copy(edev->ndev->dev_addr, mac);
	ether_addr_copy(edev->primary_mac, mac);
}

static struct qed_eth_cb_ops qede_ll_ops = {
	{
		.link_update = qede_link_update,
	},
	.force_mac = qede_force_mac,
};

static int qede_netdev_event(struct notifier_block *this, unsigned long event,
			     void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct ethtool_drvinfo drvinfo;
	struct qede_dev *edev;

	/* Currently only support name change */
	if (event != NETDEV_CHANGENAME)
		goto done;

	/* Check whether this is a qede device */
	if (!ndev || !ndev->ethtool_ops || !ndev->ethtool_ops->get_drvinfo)
		goto done;

	memset(&drvinfo, 0, sizeof(drvinfo));
	ndev->ethtool_ops->get_drvinfo(ndev, &drvinfo);
	if (strcmp(drvinfo.driver, "qede"))
		goto done;
	edev = netdev_priv(ndev);

	/* Notify qed of the name change */
	if (!edev->ops || !edev->ops->common)
		goto done;
	edev->ops->common->set_id(edev->cdev, edev->ndev->name,
				  "qede");

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
	 * interface rename after pci probe and netdev registeration.
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

/* -------------------------------------------------------------------------
 * START OF FAST-PATH
 * -------------------------------------------------------------------------
 */

/* Unmap the data and free skb */
static int qede_free_tx_pkt(struct qede_dev *edev,
			    struct qede_tx_queue *txq, int *len)
{
	u16 idx = txq->sw_tx_cons & NUM_TX_BDS_MAX;
	struct sk_buff *skb = txq->sw_tx_ring[idx].skb;
	struct eth_tx_1st_bd *first_bd;
	struct eth_tx_bd *tx_data_bd;
	int bds_consumed = 0;
	int nbds;
	bool data_split = txq->sw_tx_ring[idx].flags & QEDE_TSO_SPLIT_BD;
	int i, split_bd_len = 0;

	if (unlikely(!skb)) {
		DP_ERR(edev,
		       "skb is null for txq idx=%d txq->sw_tx_cons=%d txq->sw_tx_prod=%d\n",
		       idx, txq->sw_tx_cons, txq->sw_tx_prod);
		return -1;
	}

	*len = skb->len;

	first_bd = (struct eth_tx_1st_bd *)qed_chain_consume(&txq->tx_pbl);

	bds_consumed++;

	nbds = first_bd->data.nbds;

	if (data_split) {
		struct eth_tx_bd *split = (struct eth_tx_bd *)
			qed_chain_consume(&txq->tx_pbl);
		split_bd_len = BD_UNMAP_LEN(split);
		bds_consumed++;
	}
	dma_unmap_page(&edev->pdev->dev, BD_UNMAP_ADDR(first_bd),
		       BD_UNMAP_LEN(first_bd) + split_bd_len, DMA_TO_DEVICE);

	/* Unmap the data of the skb frags */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++, bds_consumed++) {
		tx_data_bd = (struct eth_tx_bd *)
			qed_chain_consume(&txq->tx_pbl);
		dma_unmap_page(&edev->pdev->dev, BD_UNMAP_ADDR(tx_data_bd),
			       BD_UNMAP_LEN(tx_data_bd), DMA_TO_DEVICE);
	}

	while (bds_consumed++ < nbds)
		qed_chain_consume(&txq->tx_pbl);

	/* Free skb */
	dev_kfree_skb_any(skb);
	txq->sw_tx_ring[idx].skb = NULL;
	txq->sw_tx_ring[idx].flags = 0;

	return 0;
}

/* Unmap the data and free skb when mapping failed during start_xmit */
static void qede_free_failed_tx_pkt(struct qede_dev *edev,
				    struct qede_tx_queue *txq,
				    struct eth_tx_1st_bd *first_bd,
				    int nbd, bool data_split)
{
	u16 idx = txq->sw_tx_prod & NUM_TX_BDS_MAX;
	struct sk_buff *skb = txq->sw_tx_ring[idx].skb;
	struct eth_tx_bd *tx_data_bd;
	int i, split_bd_len = 0;

	/* Return prod to its position before this skb was handled */
	qed_chain_set_prod(&txq->tx_pbl,
			   le16_to_cpu(txq->tx_db.data.bd_prod), first_bd);

	first_bd = (struct eth_tx_1st_bd *)qed_chain_produce(&txq->tx_pbl);

	if (data_split) {
		struct eth_tx_bd *split = (struct eth_tx_bd *)
					  qed_chain_produce(&txq->tx_pbl);
		split_bd_len = BD_UNMAP_LEN(split);
		nbd--;
	}

	dma_unmap_page(&edev->pdev->dev, BD_UNMAP_ADDR(first_bd),
		       BD_UNMAP_LEN(first_bd) + split_bd_len, DMA_TO_DEVICE);

	/* Unmap the data of the skb frags */
	for (i = 0; i < nbd; i++) {
		tx_data_bd = (struct eth_tx_bd *)
			qed_chain_produce(&txq->tx_pbl);
		if (tx_data_bd->nbytes)
			dma_unmap_page(&edev->pdev->dev,
				       BD_UNMAP_ADDR(tx_data_bd),
				       BD_UNMAP_LEN(tx_data_bd), DMA_TO_DEVICE);
	}

	/* Return again prod to its position before this skb was handled */
	qed_chain_set_prod(&txq->tx_pbl,
			   le16_to_cpu(txq->tx_db.data.bd_prod), first_bd);

	/* Free skb */
	dev_kfree_skb_any(skb);
	txq->sw_tx_ring[idx].skb = NULL;
	txq->sw_tx_ring[idx].flags = 0;
}

static u32 qede_xmit_type(struct qede_dev *edev,
			  struct sk_buff *skb, int *ipv6_ext)
{
	u32 rc = XMIT_L4_CSUM;
	__be16 l3_proto;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return XMIT_PLAIN;

	l3_proto = vlan_get_protocol(skb);
	if (l3_proto == htons(ETH_P_IPV6) &&
	    (ipv6_hdr(skb)->nexthdr == NEXTHDR_IPV6))
		*ipv6_ext = 1;

	if (skb->encapsulation)
		rc |= XMIT_ENC;

	if (skb_is_gso(skb))
		rc |= XMIT_LSO;

	return rc;
}

static void qede_set_params_for_ipv6_ext(struct sk_buff *skb,
					 struct eth_tx_2nd_bd *second_bd,
					 struct eth_tx_3rd_bd *third_bd)
{
	u8 l4_proto;
	u16 bd2_bits1 = 0, bd2_bits2 = 0;

	bd2_bits1 |= (1 << ETH_TX_DATA_2ND_BD_IPV6_EXT_SHIFT);

	bd2_bits2 |= ((((u8 *)skb_transport_header(skb) - skb->data) >> 1) &
		     ETH_TX_DATA_2ND_BD_L4_HDR_START_OFFSET_W_MASK)
		    << ETH_TX_DATA_2ND_BD_L4_HDR_START_OFFSET_W_SHIFT;

	bd2_bits1 |= (ETH_L4_PSEUDO_CSUM_CORRECT_LENGTH <<
		      ETH_TX_DATA_2ND_BD_L4_PSEUDO_CSUM_MODE_SHIFT);

	if (vlan_get_protocol(skb) == htons(ETH_P_IPV6))
		l4_proto = ipv6_hdr(skb)->nexthdr;
	else
		l4_proto = ip_hdr(skb)->protocol;

	if (l4_proto == IPPROTO_UDP)
		bd2_bits1 |= 1 << ETH_TX_DATA_2ND_BD_L4_UDP_SHIFT;

	if (third_bd)
		third_bd->data.bitfields |=
			cpu_to_le16(((tcp_hdrlen(skb) / 4) &
				ETH_TX_DATA_3RD_BD_TCP_HDR_LEN_DW_MASK) <<
				ETH_TX_DATA_3RD_BD_TCP_HDR_LEN_DW_SHIFT);

	second_bd->data.bitfields1 = cpu_to_le16(bd2_bits1);
	second_bd->data.bitfields2 = cpu_to_le16(bd2_bits2);
}

static int map_frag_to_bd(struct qede_dev *edev,
			  skb_frag_t *frag, struct eth_tx_bd *bd)
{
	dma_addr_t mapping;

	/* Map skb non-linear frag data for DMA */
	mapping = skb_frag_dma_map(&edev->pdev->dev, frag, 0,
				   skb_frag_size(frag), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&edev->pdev->dev, mapping))) {
		DP_NOTICE(edev, "Unable to map frag - dropping packet\n");
		return -ENOMEM;
	}

	/* Setup the data pointer of the frag data */
	BD_SET_UNMAP_ADDR_LEN(bd, mapping, skb_frag_size(frag));

	return 0;
}

static u16 qede_get_skb_hlen(struct sk_buff *skb, bool is_encap_pkt)
{
	if (is_encap_pkt)
		return (skb_inner_transport_header(skb) +
			inner_tcp_hdrlen(skb) - skb->data);
	else
		return (skb_transport_header(skb) +
			tcp_hdrlen(skb) - skb->data);
}

/* +2 for 1st BD for headers and 2nd BD for headlen (if required) */
#if ((MAX_SKB_FRAGS + 2) > ETH_TX_MAX_BDS_PER_NON_LSO_PACKET)
static bool qede_pkt_req_lin(struct qede_dev *edev, struct sk_buff *skb,
			     u8 xmit_type)
{
	int allowed_frags = ETH_TX_MAX_BDS_PER_NON_LSO_PACKET - 1;

	if (xmit_type & XMIT_LSO) {
		int hlen;

		hlen = qede_get_skb_hlen(skb, xmit_type & XMIT_ENC);

		/* linear payload would require its own BD */
		if (skb_headlen(skb) > hlen)
			allowed_frags--;
	}

	return (skb_shinfo(skb)->nr_frags > allowed_frags);
}
#endif

static inline void qede_update_tx_producer(struct qede_tx_queue *txq)
{
	/* wmb makes sure that the BDs data is updated before updating the
	 * producer, otherwise FW may read old data from the BDs.
	 */
	wmb();
	barrier();
	writel(txq->tx_db.raw, txq->doorbell_addr);

	/* mmiowb is needed to synchronize doorbell writes from more than one
	 * processor. It guarantees that the write arrives to the device before
	 * the queue lock is released and another start_xmit is called (possibly
	 * on another CPU). Without this barrier, the next doorbell can bypass
	 * this doorbell. This is applicable to IA64/Altix systems.
	 */
	mmiowb();
}

/* Main transmit function */
static netdev_tx_t qede_start_xmit(struct sk_buff *skb,
				   struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);
	struct netdev_queue *netdev_txq;
	struct qede_tx_queue *txq;
	struct eth_tx_1st_bd *first_bd;
	struct eth_tx_2nd_bd *second_bd = NULL;
	struct eth_tx_3rd_bd *third_bd = NULL;
	struct eth_tx_bd *tx_data_bd = NULL;
	u16 txq_index;
	u8 nbd = 0;
	dma_addr_t mapping;
	int rc, frag_idx = 0, ipv6_ext = 0;
	u8 xmit_type;
	u16 idx;
	u16 hlen;
	bool data_split = false;

	/* Get tx-queue context and netdev index */
	txq_index = skb_get_queue_mapping(skb);
	WARN_ON(txq_index >= QEDE_TSS_COUNT(edev));
	txq = QEDE_TX_QUEUE(edev, txq_index);
	netdev_txq = netdev_get_tx_queue(ndev, txq_index);

	WARN_ON(qed_chain_get_elem_left(&txq->tx_pbl) < (MAX_SKB_FRAGS + 1));

	xmit_type = qede_xmit_type(edev, skb, &ipv6_ext);

#if ((MAX_SKB_FRAGS + 2) > ETH_TX_MAX_BDS_PER_NON_LSO_PACKET)
	if (qede_pkt_req_lin(edev, skb, xmit_type)) {
		if (skb_linearize(skb)) {
			DP_NOTICE(edev,
				  "SKB linearization failed - silently dropping this SKB\n");
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
	}
#endif

	/* Fill the entry in the SW ring and the BDs in the FW ring */
	idx = txq->sw_tx_prod & NUM_TX_BDS_MAX;
	txq->sw_tx_ring[idx].skb = skb;
	first_bd = (struct eth_tx_1st_bd *)
		   qed_chain_produce(&txq->tx_pbl);
	memset(first_bd, 0, sizeof(*first_bd));
	first_bd->data.bd_flags.bitfields =
		1 << ETH_TX_1ST_BD_FLAGS_START_BD_SHIFT;

	/* Map skb linear data for DMA and set in the first BD */
	mapping = dma_map_single(&edev->pdev->dev, skb->data,
				 skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&edev->pdev->dev, mapping))) {
		DP_NOTICE(edev, "SKB mapping failed\n");
		qede_free_failed_tx_pkt(edev, txq, first_bd, 0, false);
		qede_update_tx_producer(txq);
		return NETDEV_TX_OK;
	}
	nbd++;
	BD_SET_UNMAP_ADDR_LEN(first_bd, mapping, skb_headlen(skb));

	/* In case there is IPv6 with extension headers or LSO we need 2nd and
	 * 3rd BDs.
	 */
	if (unlikely((xmit_type & XMIT_LSO) | ipv6_ext)) {
		second_bd = (struct eth_tx_2nd_bd *)
			qed_chain_produce(&txq->tx_pbl);
		memset(second_bd, 0, sizeof(*second_bd));

		nbd++;
		third_bd = (struct eth_tx_3rd_bd *)
			qed_chain_produce(&txq->tx_pbl);
		memset(third_bd, 0, sizeof(*third_bd));

		nbd++;
		/* We need to fill in additional data in second_bd... */
		tx_data_bd = (struct eth_tx_bd *)second_bd;
	}

	if (skb_vlan_tag_present(skb)) {
		first_bd->data.vlan = cpu_to_le16(skb_vlan_tag_get(skb));
		first_bd->data.bd_flags.bitfields |=
			1 << ETH_TX_1ST_BD_FLAGS_VLAN_INSERTION_SHIFT;
	}

	/* Fill the parsing flags & params according to the requested offload */
	if (xmit_type & XMIT_L4_CSUM) {
		/* We don't re-calculate IP checksum as it is already done by
		 * the upper stack
		 */
		first_bd->data.bd_flags.bitfields |=
			1 << ETH_TX_1ST_BD_FLAGS_L4_CSUM_SHIFT;

		if (xmit_type & XMIT_ENC) {
			first_bd->data.bd_flags.bitfields |=
				1 << ETH_TX_1ST_BD_FLAGS_IP_CSUM_SHIFT;
			first_bd->data.bitfields |=
			    1 << ETH_TX_DATA_1ST_BD_TUNN_FLAG_SHIFT;
		}

		/* Legacy FW had flipped behavior in regard to this bit -
		 * I.e., needed to set to prevent FW from touching encapsulated
		 * packets when it didn't need to.
		 */
		if (unlikely(txq->is_legacy))
			first_bd->data.bitfields ^=
			    1 << ETH_TX_DATA_1ST_BD_TUNN_FLAG_SHIFT;

		/* If the packet is IPv6 with extension header, indicate that
		 * to FW and pass few params, since the device cracker doesn't
		 * support parsing IPv6 with extension header/s.
		 */
		if (unlikely(ipv6_ext))
			qede_set_params_for_ipv6_ext(skb, second_bd, third_bd);
	}

	if (xmit_type & XMIT_LSO) {
		first_bd->data.bd_flags.bitfields |=
			(1 << ETH_TX_1ST_BD_FLAGS_LSO_SHIFT);
		third_bd->data.lso_mss =
			cpu_to_le16(skb_shinfo(skb)->gso_size);

		if (unlikely(xmit_type & XMIT_ENC)) {
			first_bd->data.bd_flags.bitfields |=
				1 << ETH_TX_1ST_BD_FLAGS_TUNN_IP_CSUM_SHIFT;
			hlen = qede_get_skb_hlen(skb, true);
		} else {
			first_bd->data.bd_flags.bitfields |=
				1 << ETH_TX_1ST_BD_FLAGS_IP_CSUM_SHIFT;
			hlen = qede_get_skb_hlen(skb, false);
		}

		/* @@@TBD - if will not be removed need to check */
		third_bd->data.bitfields |=
			cpu_to_le16((1 << ETH_TX_DATA_3RD_BD_HDR_NBD_SHIFT));

		/* Make life easier for FW guys who can't deal with header and
		 * data on same BD. If we need to split, use the second bd...
		 */
		if (unlikely(skb_headlen(skb) > hlen)) {
			DP_VERBOSE(edev, NETIF_MSG_TX_QUEUED,
				   "TSO split header size is %d (%x:%x)\n",
				   first_bd->nbytes, first_bd->addr.hi,
				   first_bd->addr.lo);

			mapping = HILO_U64(le32_to_cpu(first_bd->addr.hi),
					   le32_to_cpu(first_bd->addr.lo)) +
					   hlen;

			BD_SET_UNMAP_ADDR_LEN(tx_data_bd, mapping,
					      le16_to_cpu(first_bd->nbytes) -
					      hlen);

			/* this marks the BD as one that has no
			 * individual mapping
			 */
			txq->sw_tx_ring[idx].flags |= QEDE_TSO_SPLIT_BD;

			first_bd->nbytes = cpu_to_le16(hlen);

			tx_data_bd = (struct eth_tx_bd *)third_bd;
			data_split = true;
		}
	} else {
		first_bd->data.bitfields |=
		    (skb->len & ETH_TX_DATA_1ST_BD_PKT_LEN_MASK) <<
		    ETH_TX_DATA_1ST_BD_PKT_LEN_SHIFT;
	}

	/* Handle fragmented skb */
	/* special handle for frags inside 2nd and 3rd bds.. */
	while (tx_data_bd && frag_idx < skb_shinfo(skb)->nr_frags) {
		rc = map_frag_to_bd(edev,
				    &skb_shinfo(skb)->frags[frag_idx],
				    tx_data_bd);
		if (rc) {
			qede_free_failed_tx_pkt(edev, txq, first_bd, nbd,
						data_split);
			qede_update_tx_producer(txq);
			return NETDEV_TX_OK;
		}

		if (tx_data_bd == (struct eth_tx_bd *)second_bd)
			tx_data_bd = (struct eth_tx_bd *)third_bd;
		else
			tx_data_bd = NULL;

		frag_idx++;
	}

	/* map last frags into 4th, 5th .... */
	for (; frag_idx < skb_shinfo(skb)->nr_frags; frag_idx++, nbd++) {
		tx_data_bd = (struct eth_tx_bd *)
			     qed_chain_produce(&txq->tx_pbl);

		memset(tx_data_bd, 0, sizeof(*tx_data_bd));

		rc = map_frag_to_bd(edev,
				    &skb_shinfo(skb)->frags[frag_idx],
				    tx_data_bd);
		if (rc) {
			qede_free_failed_tx_pkt(edev, txq, first_bd, nbd,
						data_split);
			qede_update_tx_producer(txq);
			return NETDEV_TX_OK;
		}
	}

	/* update the first BD with the actual num BDs */
	first_bd->data.nbds = nbd;

	netdev_tx_sent_queue(netdev_txq, skb->len);

	skb_tx_timestamp(skb);

	/* Advance packet producer only before sending the packet since mapping
	 * of pages may fail.
	 */
	txq->sw_tx_prod++;

	/* 'next page' entries are counted in the producer value */
	txq->tx_db.data.bd_prod =
		cpu_to_le16(qed_chain_get_prod_idx(&txq->tx_pbl));

	if (!skb->xmit_more || netif_xmit_stopped(netdev_txq))
		qede_update_tx_producer(txq);

	if (unlikely(qed_chain_get_elem_left(&txq->tx_pbl)
		      < (MAX_SKB_FRAGS + 1))) {
		if (skb->xmit_more)
			qede_update_tx_producer(txq);

		netif_tx_stop_queue(netdev_txq);
		txq->stopped_cnt++;
		DP_VERBOSE(edev, NETIF_MSG_TX_QUEUED,
			   "Stop queue was called\n");
		/* paired memory barrier is in qede_tx_int(), we have to keep
		 * ordering of set_bit() in netif_tx_stop_queue() and read of
		 * fp->bd_tx_cons
		 */
		smp_mb();

		if (qed_chain_get_elem_left(&txq->tx_pbl)
		     >= (MAX_SKB_FRAGS + 1) &&
		    (edev->state == QEDE_STATE_OPEN)) {
			netif_tx_wake_queue(netdev_txq);
			DP_VERBOSE(edev, NETIF_MSG_TX_QUEUED,
				   "Wake queue was called\n");
		}
	}

	return NETDEV_TX_OK;
}

int qede_txq_has_work(struct qede_tx_queue *txq)
{
	u16 hw_bd_cons;

	/* Tell compiler that consumer and producer can change */
	barrier();
	hw_bd_cons = le16_to_cpu(*txq->hw_cons_ptr);
	if (qed_chain_get_cons_idx(&txq->tx_pbl) == hw_bd_cons + 1)
		return 0;

	return hw_bd_cons != qed_chain_get_cons_idx(&txq->tx_pbl);
}

static int qede_tx_int(struct qede_dev *edev, struct qede_tx_queue *txq)
{
	struct netdev_queue *netdev_txq;
	u16 hw_bd_cons;
	unsigned int pkts_compl = 0, bytes_compl = 0;
	int rc;

	netdev_txq = netdev_get_tx_queue(edev->ndev, txq->index);

	hw_bd_cons = le16_to_cpu(*txq->hw_cons_ptr);
	barrier();

	while (hw_bd_cons != qed_chain_get_cons_idx(&txq->tx_pbl)) {
		int len = 0;

		rc = qede_free_tx_pkt(edev, txq, &len);
		if (rc) {
			DP_NOTICE(edev, "hw_bd_cons = %d, chain_cons=%d\n",
				  hw_bd_cons,
				  qed_chain_get_cons_idx(&txq->tx_pbl));
			break;
		}

		bytes_compl += len;
		pkts_compl++;
		txq->sw_tx_cons++;
		txq->xmit_pkts++;
	}

	netdev_tx_completed_queue(netdev_txq, pkts_compl, bytes_compl);

	/* Need to make the tx_bd_cons update visible to start_xmit()
	 * before checking for netif_tx_queue_stopped().  Without the
	 * memory barrier, there is a small possibility that
	 * start_xmit() will miss it and cause the queue to be stopped
	 * forever.
	 * On the other hand we need an rmb() here to ensure the proper
	 * ordering of bit testing in the following
	 * netif_tx_queue_stopped(txq) call.
	 */
	smp_mb();

	if (unlikely(netif_tx_queue_stopped(netdev_txq))) {
		/* Taking tx_lock is needed to prevent reenabling the queue
		 * while it's empty. This could have happen if rx_action() gets
		 * suspended in qede_tx_int() after the condition before
		 * netif_tx_wake_queue(), while tx_action (qede_start_xmit()):
		 *
		 * stops the queue->sees fresh tx_bd_cons->releases the queue->
		 * sends some packets consuming the whole queue again->
		 * stops the queue
		 */

		__netif_tx_lock(netdev_txq, smp_processor_id());

		if ((netif_tx_queue_stopped(netdev_txq)) &&
		    (edev->state == QEDE_STATE_OPEN) &&
		    (qed_chain_get_elem_left(&txq->tx_pbl)
		      >= (MAX_SKB_FRAGS + 1))) {
			netif_tx_wake_queue(netdev_txq);
			DP_VERBOSE(edev, NETIF_MSG_TX_DONE,
				   "Wake queue was called\n");
		}

		__netif_tx_unlock(netdev_txq);
	}

	return 0;
}

bool qede_has_rx_work(struct qede_rx_queue *rxq)
{
	u16 hw_comp_cons, sw_comp_cons;

	/* Tell compiler that status block fields can change */
	barrier();

	hw_comp_cons = le16_to_cpu(*rxq->hw_cons_ptr);
	sw_comp_cons = qed_chain_get_cons_idx(&rxq->rx_comp_ring);

	return hw_comp_cons != sw_comp_cons;
}

static bool qede_has_tx_work(struct qede_fastpath *fp)
{
	u8 tc;

	for (tc = 0; tc < fp->edev->num_tc; tc++)
		if (qede_txq_has_work(&fp->txqs[tc]))
			return true;
	return false;
}

static inline void qede_rx_bd_ring_consume(struct qede_rx_queue *rxq)
{
	qed_chain_consume(&rxq->rx_bd_ring);
	rxq->sw_rx_cons++;
}

/* This function reuses the buffer(from an offset) from
 * consumer index to producer index in the bd ring
 */
static inline void qede_reuse_page(struct qede_dev *edev,
				   struct qede_rx_queue *rxq,
				   struct sw_rx_data *curr_cons)
{
	struct eth_rx_bd *rx_bd_prod = qed_chain_produce(&rxq->rx_bd_ring);
	struct sw_rx_data *curr_prod;
	dma_addr_t new_mapping;

	curr_prod = &rxq->sw_rx_ring[rxq->sw_rx_prod & NUM_RX_BDS_MAX];
	*curr_prod = *curr_cons;

	new_mapping = curr_prod->mapping + curr_prod->page_offset;

	rx_bd_prod->addr.hi = cpu_to_le32(upper_32_bits(new_mapping));
	rx_bd_prod->addr.lo = cpu_to_le32(lower_32_bits(new_mapping));

	rxq->sw_rx_prod++;
	curr_cons->data = NULL;
}

/* In case of allocation failures reuse buffers
 * from consumer index to produce buffers for firmware
 */
void qede_recycle_rx_bd_ring(struct qede_rx_queue *rxq,
			     struct qede_dev *edev, u8 count)
{
	struct sw_rx_data *curr_cons;

	for (; count > 0; count--) {
		curr_cons = &rxq->sw_rx_ring[rxq->sw_rx_cons & NUM_RX_BDS_MAX];
		qede_reuse_page(edev, rxq, curr_cons);
		qede_rx_bd_ring_consume(rxq);
	}
}

static inline int qede_realloc_rx_buffer(struct qede_dev *edev,
					 struct qede_rx_queue *rxq,
					 struct sw_rx_data *curr_cons)
{
	/* Move to the next segment in the page */
	curr_cons->page_offset += rxq->rx_buf_seg_size;

	if (curr_cons->page_offset == PAGE_SIZE) {
		if (unlikely(qede_alloc_rx_buffer(edev, rxq))) {
			/* Since we failed to allocate new buffer
			 * current buffer can be used again.
			 */
			curr_cons->page_offset -= rxq->rx_buf_seg_size;

			return -ENOMEM;
		}

		dma_unmap_page(&edev->pdev->dev, curr_cons->mapping,
			       PAGE_SIZE, DMA_FROM_DEVICE);
	} else {
		/* Increment refcount of the page as we don't want
		 * network stack to take the ownership of the page
		 * which can be recycled multiple times by the driver.
		 */
		page_ref_inc(curr_cons->data);
		qede_reuse_page(edev, rxq, curr_cons);
	}

	return 0;
}

static inline void qede_update_rx_prod(struct qede_dev *edev,
				       struct qede_rx_queue *rxq)
{
	u16 bd_prod = qed_chain_get_prod_idx(&rxq->rx_bd_ring);
	u16 cqe_prod = qed_chain_get_prod_idx(&rxq->rx_comp_ring);
	struct eth_rx_prod_data rx_prods = {0};

	/* Update producers */
	rx_prods.bd_prod = cpu_to_le16(bd_prod);
	rx_prods.cqe_prod = cpu_to_le16(cqe_prod);

	/* Make sure that the BD and SGE data is updated before updating the
	 * producers since FW might read the BD/SGE right after the producer
	 * is updated.
	 */
	wmb();

	internal_ram_wr(rxq->hw_rxq_prod_addr, sizeof(rx_prods),
			(u32 *)&rx_prods);

	/* mmiowb is needed to synchronize doorbell writes from more than one
	 * processor. It guarantees that the write arrives to the device before
	 * the napi lock is released and another qede_poll is called (possibly
	 * on another CPU). Without this barrier, the next doorbell can bypass
	 * this doorbell. This is applicable to IA64/Altix systems.
	 */
	mmiowb();
}

static u32 qede_get_rxhash(struct qede_dev *edev,
			   u8 bitfields,
			   __le32 rss_hash, enum pkt_hash_types *rxhash_type)
{
	enum rss_hash_type htype;

	htype = GET_FIELD(bitfields, ETH_FAST_PATH_RX_REG_CQE_RSS_HASH_TYPE);

	if ((edev->ndev->features & NETIF_F_RXHASH) && htype) {
		*rxhash_type = ((htype == RSS_HASH_TYPE_IPV4) ||
				(htype == RSS_HASH_TYPE_IPV6)) ?
				PKT_HASH_TYPE_L3 : PKT_HASH_TYPE_L4;
		return le32_to_cpu(rss_hash);
	}
	*rxhash_type = PKT_HASH_TYPE_NONE;
	return 0;
}

static void qede_set_skb_csum(struct sk_buff *skb, u8 csum_flag)
{
	skb_checksum_none_assert(skb);

	if (csum_flag & QEDE_CSUM_UNNECESSARY)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (csum_flag & QEDE_TUNN_CSUM_UNNECESSARY)
		skb->csum_level = 1;
}

static inline void qede_skb_receive(struct qede_dev *edev,
				    struct qede_fastpath *fp,
				    struct sk_buff *skb, u16 vlan_tag)
{
	if (vlan_tag)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan_tag);

	napi_gro_receive(&fp->napi, skb);
}

static void qede_set_gro_params(struct qede_dev *edev,
				struct sk_buff *skb,
				struct eth_fast_path_rx_tpa_start_cqe *cqe)
{
	u16 parsing_flags = le16_to_cpu(cqe->pars_flags.flags);

	if (((parsing_flags >> PARSING_AND_ERR_FLAGS_L3TYPE_SHIFT) &
	    PARSING_AND_ERR_FLAGS_L3TYPE_MASK) == 2)
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
	else
		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;

	skb_shinfo(skb)->gso_size = __le16_to_cpu(cqe->len_on_first_bd) -
					cqe->header_len;
}

static int qede_fill_frag_skb(struct qede_dev *edev,
			      struct qede_rx_queue *rxq,
			      u8 tpa_agg_index, u16 len_on_bd)
{
	struct sw_rx_data *current_bd = &rxq->sw_rx_ring[rxq->sw_rx_cons &
							 NUM_RX_BDS_MAX];
	struct qede_agg_info *tpa_info = &rxq->tpa_info[tpa_agg_index];
	struct sk_buff *skb = tpa_info->skb;

	if (unlikely(tpa_info->agg_state != QEDE_AGG_STATE_START))
		goto out;

	/* Add one frag and update the appropriate fields in the skb */
	skb_fill_page_desc(skb, tpa_info->frag_id++,
			   current_bd->data, current_bd->page_offset,
			   len_on_bd);

	if (unlikely(qede_realloc_rx_buffer(edev, rxq, current_bd))) {
		/* Incr page ref count to reuse on allocation failure
		 * so that it doesn't get freed while freeing SKB.
		 */
		page_ref_inc(current_bd->data);
		goto out;
	}

	qed_chain_consume(&rxq->rx_bd_ring);
	rxq->sw_rx_cons++;

	skb->data_len += len_on_bd;
	skb->truesize += rxq->rx_buf_seg_size;
	skb->len += len_on_bd;

	return 0;

out:
	tpa_info->agg_state = QEDE_AGG_STATE_ERROR;
	qede_recycle_rx_bd_ring(rxq, edev, 1);
	return -ENOMEM;
}

static void qede_tpa_start(struct qede_dev *edev,
			   struct qede_rx_queue *rxq,
			   struct eth_fast_path_rx_tpa_start_cqe *cqe)
{
	struct qede_agg_info *tpa_info = &rxq->tpa_info[cqe->tpa_agg_index];
	struct eth_rx_bd *rx_bd_cons = qed_chain_consume(&rxq->rx_bd_ring);
	struct eth_rx_bd *rx_bd_prod = qed_chain_produce(&rxq->rx_bd_ring);
	struct sw_rx_data *replace_buf = &tpa_info->replace_buf;
	dma_addr_t mapping = tpa_info->replace_buf_mapping;
	struct sw_rx_data *sw_rx_data_cons;
	struct sw_rx_data *sw_rx_data_prod;
	enum pkt_hash_types rxhash_type;
	u32 rxhash;

	sw_rx_data_cons = &rxq->sw_rx_ring[rxq->sw_rx_cons & NUM_RX_BDS_MAX];
	sw_rx_data_prod = &rxq->sw_rx_ring[rxq->sw_rx_prod & NUM_RX_BDS_MAX];

	/* Use pre-allocated replacement buffer - we can't release the agg.
	 * start until its over and we don't want to risk allocation failing
	 * here, so re-allocate when aggregation will be over.
	 */
	sw_rx_data_prod->mapping = replace_buf->mapping;

	sw_rx_data_prod->data = replace_buf->data;
	rx_bd_prod->addr.hi = cpu_to_le32(upper_32_bits(mapping));
	rx_bd_prod->addr.lo = cpu_to_le32(lower_32_bits(mapping));
	sw_rx_data_prod->page_offset = replace_buf->page_offset;

	rxq->sw_rx_prod++;

	/* move partial skb from cons to pool (don't unmap yet)
	 * save mapping, incase we drop the packet later on.
	 */
	tpa_info->start_buf = *sw_rx_data_cons;
	mapping = HILO_U64(le32_to_cpu(rx_bd_cons->addr.hi),
			   le32_to_cpu(rx_bd_cons->addr.lo));

	tpa_info->start_buf_mapping = mapping;
	rxq->sw_rx_cons++;

	/* set tpa state to start only if we are able to allocate skb
	 * for this aggregation, otherwise mark as error and aggregation will
	 * be dropped
	 */
	tpa_info->skb = netdev_alloc_skb(edev->ndev,
					 le16_to_cpu(cqe->len_on_first_bd));
	if (unlikely(!tpa_info->skb)) {
		DP_NOTICE(edev, "Failed to allocate SKB for gro\n");
		tpa_info->agg_state = QEDE_AGG_STATE_ERROR;
		goto cons_buf;
	}

	skb_put(tpa_info->skb, le16_to_cpu(cqe->len_on_first_bd));
	memcpy(&tpa_info->start_cqe, cqe, sizeof(tpa_info->start_cqe));

	/* Start filling in the aggregation info */
	tpa_info->frag_id = 0;
	tpa_info->agg_state = QEDE_AGG_STATE_START;

	rxhash = qede_get_rxhash(edev, cqe->bitfields,
				 cqe->rss_hash, &rxhash_type);
	skb_set_hash(tpa_info->skb, rxhash, rxhash_type);
	if ((le16_to_cpu(cqe->pars_flags.flags) >>
	     PARSING_AND_ERR_FLAGS_TAG8021QEXIST_SHIFT) &
		    PARSING_AND_ERR_FLAGS_TAG8021QEXIST_MASK)
		tpa_info->vlan_tag = le16_to_cpu(cqe->vlan_tag);
	else
		tpa_info->vlan_tag = 0;

	/* This is needed in order to enable forwarding support */
	qede_set_gro_params(edev, tpa_info->skb, cqe);

cons_buf: /* We still need to handle bd_len_list to consume buffers */
	if (likely(cqe->ext_bd_len_list[0]))
		qede_fill_frag_skb(edev, rxq, cqe->tpa_agg_index,
				   le16_to_cpu(cqe->ext_bd_len_list[0]));

	if (unlikely(cqe->ext_bd_len_list[1])) {
		DP_ERR(edev,
		       "Unlikely - got a TPA aggregation with more than one ext_bd_len_list entry in the TPA start\n");
		tpa_info->agg_state = QEDE_AGG_STATE_ERROR;
	}
}

#ifdef CONFIG_INET
static void qede_gro_ip_csum(struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct tcphdr *th;

	skb_set_transport_header(skb, sizeof(struct iphdr));
	th = tcp_hdr(skb);

	th->check = ~tcp_v4_check(skb->len - skb_transport_offset(skb),
				  iph->saddr, iph->daddr, 0);

	tcp_gro_complete(skb);
}

static void qede_gro_ipv6_csum(struct sk_buff *skb)
{
	struct ipv6hdr *iph = ipv6_hdr(skb);
	struct tcphdr *th;

	skb_set_transport_header(skb, sizeof(struct ipv6hdr));
	th = tcp_hdr(skb);

	th->check = ~tcp_v6_check(skb->len - skb_transport_offset(skb),
				  &iph->saddr, &iph->daddr, 0);
	tcp_gro_complete(skb);
}
#endif

static void qede_gro_receive(struct qede_dev *edev,
			     struct qede_fastpath *fp,
			     struct sk_buff *skb,
			     u16 vlan_tag)
{
	/* FW can send a single MTU sized packet from gro flow
	 * due to aggregation timeout/last segment etc. which
	 * is not expected to be a gro packet. If a skb has zero
	 * frags then simply push it in the stack as non gso skb.
	 */
	if (unlikely(!skb->data_len)) {
		skb_shinfo(skb)->gso_type = 0;
		skb_shinfo(skb)->gso_size = 0;
		goto send_skb;
	}

#ifdef CONFIG_INET
	if (skb_shinfo(skb)->gso_size) {
		skb_set_network_header(skb, 0);

		switch (skb->protocol) {
		case htons(ETH_P_IP):
			qede_gro_ip_csum(skb);
			break;
		case htons(ETH_P_IPV6):
			qede_gro_ipv6_csum(skb);
			break;
		default:
			DP_ERR(edev,
			       "Error: FW GRO supports only IPv4/IPv6, not 0x%04x\n",
			       ntohs(skb->protocol));
		}
	}
#endif

send_skb:
	skb_record_rx_queue(skb, fp->rxq->rxq_id);
	qede_skb_receive(edev, fp, skb, vlan_tag);
}

static inline void qede_tpa_cont(struct qede_dev *edev,
				 struct qede_rx_queue *rxq,
				 struct eth_fast_path_rx_tpa_cont_cqe *cqe)
{
	int i;

	for (i = 0; cqe->len_list[i]; i++)
		qede_fill_frag_skb(edev, rxq, cqe->tpa_agg_index,
				   le16_to_cpu(cqe->len_list[i]));

	if (unlikely(i > 1))
		DP_ERR(edev,
		       "Strange - TPA cont with more than a single len_list entry\n");
}

static void qede_tpa_end(struct qede_dev *edev,
			 struct qede_fastpath *fp,
			 struct eth_fast_path_rx_tpa_end_cqe *cqe)
{
	struct qede_rx_queue *rxq = fp->rxq;
	struct qede_agg_info *tpa_info;
	struct sk_buff *skb;
	int i;

	tpa_info = &rxq->tpa_info[cqe->tpa_agg_index];
	skb = tpa_info->skb;

	for (i = 0; cqe->len_list[i]; i++)
		qede_fill_frag_skb(edev, rxq, cqe->tpa_agg_index,
				   le16_to_cpu(cqe->len_list[i]));
	if (unlikely(i > 1))
		DP_ERR(edev,
		       "Strange - TPA emd with more than a single len_list entry\n");

	if (unlikely(tpa_info->agg_state != QEDE_AGG_STATE_START))
		goto err;

	/* Sanity */
	if (unlikely(cqe->num_of_bds != tpa_info->frag_id + 1))
		DP_ERR(edev,
		       "Strange - TPA had %02x BDs, but SKB has only %d frags\n",
		       cqe->num_of_bds, tpa_info->frag_id);
	if (unlikely(skb->len != le16_to_cpu(cqe->total_packet_len)))
		DP_ERR(edev,
		       "Strange - total packet len [cqe] is %4x but SKB has len %04x\n",
		       le16_to_cpu(cqe->total_packet_len), skb->len);

	memcpy(skb->data,
	       page_address(tpa_info->start_buf.data) +
		tpa_info->start_cqe.placement_offset +
		tpa_info->start_buf.page_offset,
	       le16_to_cpu(tpa_info->start_cqe.len_on_first_bd));

	/* Recycle [mapped] start buffer for the next replacement */
	tpa_info->replace_buf = tpa_info->start_buf;
	tpa_info->replace_buf_mapping = tpa_info->start_buf_mapping;

	/* Finalize the SKB */
	skb->protocol = eth_type_trans(skb, edev->ndev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	/* tcp_gro_complete() will copy NAPI_GRO_CB(skb)->count
	 * to skb_shinfo(skb)->gso_segs
	 */
	NAPI_GRO_CB(skb)->count = le16_to_cpu(cqe->num_of_coalesced_segs);

	qede_gro_receive(edev, fp, skb, tpa_info->vlan_tag);

	tpa_info->agg_state = QEDE_AGG_STATE_NONE;

	return;
err:
	/* The BD starting the aggregation is still mapped; Re-use it for
	 * future aggregations [as replacement buffer]
	 */
	memcpy(&tpa_info->replace_buf, &tpa_info->start_buf,
	       sizeof(struct sw_rx_data));
	tpa_info->replace_buf_mapping = tpa_info->start_buf_mapping;
	tpa_info->start_buf.data = NULL;
	tpa_info->agg_state = QEDE_AGG_STATE_NONE;
	dev_kfree_skb_any(tpa_info->skb);
	tpa_info->skb = NULL;
}

static bool qede_tunn_exist(u16 flag)
{
	return !!(flag & (PARSING_AND_ERR_FLAGS_TUNNELEXIST_MASK <<
			  PARSING_AND_ERR_FLAGS_TUNNELEXIST_SHIFT));
}

static u8 qede_check_tunn_csum(u16 flag)
{
	u16 csum_flag = 0;
	u8 tcsum = 0;

	if (flag & (PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMWASCALCULATED_MASK <<
		    PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMWASCALCULATED_SHIFT))
		csum_flag |= PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMERROR_MASK <<
			     PARSING_AND_ERR_FLAGS_TUNNELL4CHKSMERROR_SHIFT;

	if (flag & (PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_MASK <<
		    PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_SHIFT)) {
		csum_flag |= PARSING_AND_ERR_FLAGS_L4CHKSMERROR_MASK <<
			     PARSING_AND_ERR_FLAGS_L4CHKSMERROR_SHIFT;
		tcsum = QEDE_TUNN_CSUM_UNNECESSARY;
	}

	csum_flag |= PARSING_AND_ERR_FLAGS_TUNNELIPHDRERROR_MASK <<
		     PARSING_AND_ERR_FLAGS_TUNNELIPHDRERROR_SHIFT |
		     PARSING_AND_ERR_FLAGS_IPHDRERROR_MASK <<
		     PARSING_AND_ERR_FLAGS_IPHDRERROR_SHIFT;

	if (csum_flag & flag)
		return QEDE_CSUM_ERROR;

	return QEDE_CSUM_UNNECESSARY | tcsum;
}

static u8 qede_check_notunn_csum(u16 flag)
{
	u16 csum_flag = 0;
	u8 csum = 0;

	if (flag & (PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_MASK <<
		    PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_SHIFT)) {
		csum_flag |= PARSING_AND_ERR_FLAGS_L4CHKSMERROR_MASK <<
			     PARSING_AND_ERR_FLAGS_L4CHKSMERROR_SHIFT;
		csum = QEDE_CSUM_UNNECESSARY;
	}

	csum_flag |= PARSING_AND_ERR_FLAGS_IPHDRERROR_MASK <<
		     PARSING_AND_ERR_FLAGS_IPHDRERROR_SHIFT;

	if (csum_flag & flag)
		return QEDE_CSUM_ERROR;

	return csum;
}

static u8 qede_check_csum(u16 flag)
{
	if (!qede_tunn_exist(flag))
		return qede_check_notunn_csum(flag);
	else
		return qede_check_tunn_csum(flag);
}

static bool qede_pkt_is_ip_fragmented(struct eth_fast_path_rx_reg_cqe *cqe,
				      u16 flag)
{
	u8 tun_pars_flg = cqe->tunnel_pars_flags.flags;

	if ((tun_pars_flg & (ETH_TUNNEL_PARSING_FLAGS_IPV4_FRAGMENT_MASK <<
			     ETH_TUNNEL_PARSING_FLAGS_IPV4_FRAGMENT_SHIFT)) ||
	    (flag & (PARSING_AND_ERR_FLAGS_IPV4FRAG_MASK <<
		     PARSING_AND_ERR_FLAGS_IPV4FRAG_SHIFT)))
		return true;

	return false;
}

static int qede_rx_int(struct qede_fastpath *fp, int budget)
{
	struct qede_dev *edev = fp->edev;
	struct qede_rx_queue *rxq = fp->rxq;

	u16 hw_comp_cons, sw_comp_cons, sw_rx_index, parse_flag;
	int rx_pkt = 0;
	u8 csum_flag;

	hw_comp_cons = le16_to_cpu(*rxq->hw_cons_ptr);
	sw_comp_cons = qed_chain_get_cons_idx(&rxq->rx_comp_ring);

	/* Memory barrier to prevent the CPU from doing speculative reads of CQE
	 * / BD in the while-loop before reading hw_comp_cons. If the CQE is
	 * read before it is written by FW, then FW writes CQE and SB, and then
	 * the CPU reads the hw_comp_cons, it will use an old CQE.
	 */
	rmb();

	/* Loop to complete all indicated BDs */
	while (sw_comp_cons != hw_comp_cons) {
		struct eth_fast_path_rx_reg_cqe *fp_cqe;
		enum pkt_hash_types rxhash_type;
		enum eth_rx_cqe_type cqe_type;
		struct sw_rx_data *sw_rx_data;
		union eth_rx_cqe *cqe;
		struct sk_buff *skb;
		struct page *data;
		__le16 flags;
		u16 len, pad;
		u32 rx_hash;

		/* Get the CQE from the completion ring */
		cqe = (union eth_rx_cqe *)
			qed_chain_consume(&rxq->rx_comp_ring);
		cqe_type = cqe->fast_path_regular.type;

		if (unlikely(cqe_type == ETH_RX_CQE_TYPE_SLOW_PATH)) {
			edev->ops->eth_cqe_completion(
					edev->cdev, fp->id,
					(struct eth_slow_path_rx_cqe *)cqe);
			goto next_cqe;
		}

		if (cqe_type != ETH_RX_CQE_TYPE_REGULAR) {
			switch (cqe_type) {
			case ETH_RX_CQE_TYPE_TPA_START:
				qede_tpa_start(edev, rxq,
					       &cqe->fast_path_tpa_start);
				goto next_cqe;
			case ETH_RX_CQE_TYPE_TPA_CONT:
				qede_tpa_cont(edev, rxq,
					      &cqe->fast_path_tpa_cont);
				goto next_cqe;
			case ETH_RX_CQE_TYPE_TPA_END:
				qede_tpa_end(edev, fp,
					     &cqe->fast_path_tpa_end);
				goto next_rx_only;
			default:
				break;
			}
		}

		/* Get the data from the SW ring */
		sw_rx_index = rxq->sw_rx_cons & NUM_RX_BDS_MAX;
		sw_rx_data = &rxq->sw_rx_ring[sw_rx_index];
		data = sw_rx_data->data;

		fp_cqe = &cqe->fast_path_regular;
		len =  le16_to_cpu(fp_cqe->len_on_first_bd);
		pad = fp_cqe->placement_offset;
		flags = cqe->fast_path_regular.pars_flags.flags;

		/* If this is an error packet then drop it */
		parse_flag = le16_to_cpu(flags);

		csum_flag = qede_check_csum(parse_flag);
		if (unlikely(csum_flag == QEDE_CSUM_ERROR)) {
			if (qede_pkt_is_ip_fragmented(&cqe->fast_path_regular,
						      parse_flag)) {
				rxq->rx_ip_frags++;
				goto alloc_skb;
			}

			DP_NOTICE(edev,
				  "CQE in CONS = %u has error, flags = %x, dropping incoming packet\n",
				  sw_comp_cons, parse_flag);
			rxq->rx_hw_errors++;
			qede_recycle_rx_bd_ring(rxq, edev, fp_cqe->bd_num);
			goto next_cqe;
		}

alloc_skb:
		skb = netdev_alloc_skb(edev->ndev, QEDE_RX_HDR_SIZE);
		if (unlikely(!skb)) {
			DP_NOTICE(edev,
				  "skb allocation failed, dropping incoming packet\n");
			qede_recycle_rx_bd_ring(rxq, edev, fp_cqe->bd_num);
			rxq->rx_alloc_errors++;
			goto next_cqe;
		}

		/* Copy data into SKB */
		if (len + pad <= edev->rx_copybreak) {
			memcpy(skb_put(skb, len),
			       page_address(data) + pad +
				sw_rx_data->page_offset, len);
			qede_reuse_page(edev, rxq, sw_rx_data);
		} else {
			struct skb_frag_struct *frag;
			unsigned int pull_len;
			unsigned char *va;

			frag = &skb_shinfo(skb)->frags[0];

			skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, data,
					pad + sw_rx_data->page_offset,
					len, rxq->rx_buf_seg_size);

			va = skb_frag_address(frag);
			pull_len = eth_get_headlen(va, QEDE_RX_HDR_SIZE);

			/* Align the pull_len to optimize memcpy */
			memcpy(skb->data, va, ALIGN(pull_len, sizeof(long)));

			skb_frag_size_sub(frag, pull_len);
			frag->page_offset += pull_len;
			skb->data_len -= pull_len;
			skb->tail += pull_len;

			if (unlikely(qede_realloc_rx_buffer(edev, rxq,
							    sw_rx_data))) {
				DP_ERR(edev, "Failed to allocate rx buffer\n");
				/* Incr page ref count to reuse on allocation
				 * failure so that it doesn't get freed while
				 * freeing SKB.
				 */

				page_ref_inc(sw_rx_data->data);
				rxq->rx_alloc_errors++;
				qede_recycle_rx_bd_ring(rxq, edev,
							fp_cqe->bd_num);
				dev_kfree_skb_any(skb);
				goto next_cqe;
			}
		}

		qede_rx_bd_ring_consume(rxq);

		if (fp_cqe->bd_num != 1) {
			u16 pkt_len = le16_to_cpu(fp_cqe->pkt_len);
			u8 num_frags;

			pkt_len -= len;

			for (num_frags = fp_cqe->bd_num - 1; num_frags > 0;
			     num_frags--) {
				u16 cur_size = pkt_len > rxq->rx_buf_size ?
						rxq->rx_buf_size : pkt_len;
				if (unlikely(!cur_size)) {
					DP_ERR(edev,
					       "Still got %d BDs for mapping jumbo, but length became 0\n",
					       num_frags);
					qede_recycle_rx_bd_ring(rxq, edev,
								num_frags);
					dev_kfree_skb_any(skb);
					goto next_cqe;
				}

				if (unlikely(qede_alloc_rx_buffer(edev, rxq))) {
					qede_recycle_rx_bd_ring(rxq, edev,
								num_frags);
					dev_kfree_skb_any(skb);
					goto next_cqe;
				}

				sw_rx_index = rxq->sw_rx_cons & NUM_RX_BDS_MAX;
				sw_rx_data = &rxq->sw_rx_ring[sw_rx_index];
				qede_rx_bd_ring_consume(rxq);

				dma_unmap_page(&edev->pdev->dev,
					       sw_rx_data->mapping,
					       PAGE_SIZE, DMA_FROM_DEVICE);

				skb_fill_page_desc(skb,
						   skb_shinfo(skb)->nr_frags++,
						   sw_rx_data->data, 0,
						   cur_size);

				skb->truesize += PAGE_SIZE;
				skb->data_len += cur_size;
				skb->len += cur_size;
				pkt_len -= cur_size;
			}

			if (unlikely(pkt_len))
				DP_ERR(edev,
				       "Mapped all BDs of jumbo, but still have %d bytes\n",
				       pkt_len);
		}

		skb->protocol = eth_type_trans(skb, edev->ndev);

		rx_hash = qede_get_rxhash(edev, fp_cqe->bitfields,
					  fp_cqe->rss_hash, &rxhash_type);

		skb_set_hash(skb, rx_hash, rxhash_type);

		qede_set_skb_csum(skb, csum_flag);

		skb_record_rx_queue(skb, fp->rxq->rxq_id);

		qede_skb_receive(edev, fp, skb, le16_to_cpu(fp_cqe->vlan_tag));
next_rx_only:
		rx_pkt++;

next_cqe: /* don't consume bd rx buffer */
		qed_chain_recycle_consumed(&rxq->rx_comp_ring);
		sw_comp_cons = qed_chain_get_cons_idx(&rxq->rx_comp_ring);
		/* CR TPA - revisit how to handle budget in TPA perhaps
		 * increase on "end"
		 */
		if (rx_pkt == budget)
			break;
	} /* repeat while sw_comp_cons != hw_comp_cons... */

	/* Update producers */
	qede_update_rx_prod(edev, rxq);

	rxq->rcv_pkts += rx_pkt;

	return rx_pkt;
}

static int qede_poll(struct napi_struct *napi, int budget)
{
	struct qede_fastpath *fp = container_of(napi, struct qede_fastpath,
						napi);
	struct qede_dev *edev = fp->edev;
	int rx_work_done = 0;
	u8 tc;

	for (tc = 0; tc < edev->num_tc; tc++)
		if (likely(fp->type & QEDE_FASTPATH_TX) &&
		    qede_txq_has_work(&fp->txqs[tc]))
			qede_tx_int(edev, &fp->txqs[tc]);

	rx_work_done = (likely(fp->type & QEDE_FASTPATH_RX) &&
			qede_has_rx_work(fp->rxq)) ?
			qede_rx_int(fp, budget) : 0;
	if (rx_work_done < budget) {
		qed_sb_update_sb_idx(fp->sb_info);
		/* *_has_*_work() reads the status block,
		 * thus we need to ensure that status block indices
		 * have been actually read (qed_sb_update_sb_idx)
		 * prior to this check (*_has_*_work) so that
		 * we won't write the "newer" value of the status block
		 * to HW (if there was a DMA right after
		 * qede_has_rx_work and if there is no rmb, the memory
		 * reading (qed_sb_update_sb_idx) may be postponed
		 * to right before *_ack_sb). In this case there
		 * will never be another interrupt until there is
		 * another update of the status block, while there
		 * is still unhandled work.
		 */
		rmb();

		/* Fall out from the NAPI loop if needed */
		if (!((likely(fp->type & QEDE_FASTPATH_RX) &&
		       qede_has_rx_work(fp->rxq)) ||
		      (likely(fp->type & QEDE_FASTPATH_TX) &&
		       qede_has_tx_work(fp)))) {
			napi_complete(napi);

			/* Update and reenable interrupts */
			qed_sb_ack(fp->sb_info, IGU_INT_ENABLE,
				   1 /*update*/);
		} else {
			rx_work_done = budget;
		}
	}

	return rx_work_done;
}

static irqreturn_t qede_msix_fp_int(int irq, void *fp_cookie)
{
	struct qede_fastpath *fp = fp_cookie;

	qed_sb_ack(fp->sb_info, IGU_INT_DISABLE, 0 /*do not update*/);

	napi_schedule_irqoff(&fp->napi);
	return IRQ_HANDLED;
}

/* -------------------------------------------------------------------------
 * END OF FAST-PATH
 * -------------------------------------------------------------------------
 */

static int qede_open(struct net_device *ndev);
static int qede_close(struct net_device *ndev);
static int qede_set_mac_addr(struct net_device *ndev, void *p);
static void qede_set_rx_mode(struct net_device *ndev);
static void qede_config_rx_mode(struct net_device *ndev);

static int qede_set_ucast_rx_mac(struct qede_dev *edev,
				 enum qed_filter_xcast_params_type opcode,
				 unsigned char mac[ETH_ALEN])
{
	struct qed_filter_params filter_cmd;

	memset(&filter_cmd, 0, sizeof(filter_cmd));
	filter_cmd.type = QED_FILTER_TYPE_UCAST;
	filter_cmd.filter.ucast.type = opcode;
	filter_cmd.filter.ucast.mac_valid = 1;
	ether_addr_copy(filter_cmd.filter.ucast.mac, mac);

	return edev->ops->filter_config(edev->cdev, &filter_cmd);
}

static int qede_set_ucast_rx_vlan(struct qede_dev *edev,
				  enum qed_filter_xcast_params_type opcode,
				  u16 vid)
{
	struct qed_filter_params filter_cmd;

	memset(&filter_cmd, 0, sizeof(filter_cmd));
	filter_cmd.type = QED_FILTER_TYPE_UCAST;
	filter_cmd.filter.ucast.type = opcode;
	filter_cmd.filter.ucast.vlan_valid = 1;
	filter_cmd.filter.ucast.vlan = vid;

	return edev->ops->filter_config(edev->cdev, &filter_cmd);
}

void qede_fill_by_demand_stats(struct qede_dev *edev)
{
	struct qed_eth_stats stats;

	edev->ops->get_vport_stats(edev->cdev, &stats);
	edev->stats.no_buff_discards = stats.no_buff_discards;
	edev->stats.packet_too_big_discard = stats.packet_too_big_discard;
	edev->stats.ttl0_discard = stats.ttl0_discard;
	edev->stats.rx_ucast_bytes = stats.rx_ucast_bytes;
	edev->stats.rx_mcast_bytes = stats.rx_mcast_bytes;
	edev->stats.rx_bcast_bytes = stats.rx_bcast_bytes;
	edev->stats.rx_ucast_pkts = stats.rx_ucast_pkts;
	edev->stats.rx_mcast_pkts = stats.rx_mcast_pkts;
	edev->stats.rx_bcast_pkts = stats.rx_bcast_pkts;
	edev->stats.mftag_filter_discards = stats.mftag_filter_discards;
	edev->stats.mac_filter_discards = stats.mac_filter_discards;

	edev->stats.tx_ucast_bytes = stats.tx_ucast_bytes;
	edev->stats.tx_mcast_bytes = stats.tx_mcast_bytes;
	edev->stats.tx_bcast_bytes = stats.tx_bcast_bytes;
	edev->stats.tx_ucast_pkts = stats.tx_ucast_pkts;
	edev->stats.tx_mcast_pkts = stats.tx_mcast_pkts;
	edev->stats.tx_bcast_pkts = stats.tx_bcast_pkts;
	edev->stats.tx_err_drop_pkts = stats.tx_err_drop_pkts;
	edev->stats.coalesced_pkts = stats.tpa_coalesced_pkts;
	edev->stats.coalesced_events = stats.tpa_coalesced_events;
	edev->stats.coalesced_aborts_num = stats.tpa_aborts_num;
	edev->stats.non_coalesced_pkts = stats.tpa_not_coalesced_pkts;
	edev->stats.coalesced_bytes = stats.tpa_coalesced_bytes;

	edev->stats.rx_64_byte_packets = stats.rx_64_byte_packets;
	edev->stats.rx_65_to_127_byte_packets = stats.rx_65_to_127_byte_packets;
	edev->stats.rx_128_to_255_byte_packets =
				stats.rx_128_to_255_byte_packets;
	edev->stats.rx_256_to_511_byte_packets =
				stats.rx_256_to_511_byte_packets;
	edev->stats.rx_512_to_1023_byte_packets =
				stats.rx_512_to_1023_byte_packets;
	edev->stats.rx_1024_to_1518_byte_packets =
				stats.rx_1024_to_1518_byte_packets;
	edev->stats.rx_1519_to_1522_byte_packets =
				stats.rx_1519_to_1522_byte_packets;
	edev->stats.rx_1519_to_2047_byte_packets =
				stats.rx_1519_to_2047_byte_packets;
	edev->stats.rx_2048_to_4095_byte_packets =
				stats.rx_2048_to_4095_byte_packets;
	edev->stats.rx_4096_to_9216_byte_packets =
				stats.rx_4096_to_9216_byte_packets;
	edev->stats.rx_9217_to_16383_byte_packets =
				stats.rx_9217_to_16383_byte_packets;
	edev->stats.rx_crc_errors = stats.rx_crc_errors;
	edev->stats.rx_mac_crtl_frames = stats.rx_mac_crtl_frames;
	edev->stats.rx_pause_frames = stats.rx_pause_frames;
	edev->stats.rx_pfc_frames = stats.rx_pfc_frames;
	edev->stats.rx_align_errors = stats.rx_align_errors;
	edev->stats.rx_carrier_errors = stats.rx_carrier_errors;
	edev->stats.rx_oversize_packets = stats.rx_oversize_packets;
	edev->stats.rx_jabbers = stats.rx_jabbers;
	edev->stats.rx_undersize_packets = stats.rx_undersize_packets;
	edev->stats.rx_fragments = stats.rx_fragments;
	edev->stats.tx_64_byte_packets = stats.tx_64_byte_packets;
	edev->stats.tx_65_to_127_byte_packets = stats.tx_65_to_127_byte_packets;
	edev->stats.tx_128_to_255_byte_packets =
				stats.tx_128_to_255_byte_packets;
	edev->stats.tx_256_to_511_byte_packets =
				stats.tx_256_to_511_byte_packets;
	edev->stats.tx_512_to_1023_byte_packets =
				stats.tx_512_to_1023_byte_packets;
	edev->stats.tx_1024_to_1518_byte_packets =
				stats.tx_1024_to_1518_byte_packets;
	edev->stats.tx_1519_to_2047_byte_packets =
				stats.tx_1519_to_2047_byte_packets;
	edev->stats.tx_2048_to_4095_byte_packets =
				stats.tx_2048_to_4095_byte_packets;
	edev->stats.tx_4096_to_9216_byte_packets =
				stats.tx_4096_to_9216_byte_packets;
	edev->stats.tx_9217_to_16383_byte_packets =
				stats.tx_9217_to_16383_byte_packets;
	edev->stats.tx_pause_frames = stats.tx_pause_frames;
	edev->stats.tx_pfc_frames = stats.tx_pfc_frames;
	edev->stats.tx_lpi_entry_count = stats.tx_lpi_entry_count;
	edev->stats.tx_total_collisions = stats.tx_total_collisions;
	edev->stats.brb_truncates = stats.brb_truncates;
	edev->stats.brb_discards = stats.brb_discards;
	edev->stats.tx_mac_ctrl_frames = stats.tx_mac_ctrl_frames;
}

static
struct rtnl_link_stats64 *qede_get_stats64(struct net_device *dev,
					   struct rtnl_link_stats64 *stats)
{
	struct qede_dev *edev = netdev_priv(dev);

	qede_fill_by_demand_stats(edev);

	stats->rx_packets = edev->stats.rx_ucast_pkts +
			    edev->stats.rx_mcast_pkts +
			    edev->stats.rx_bcast_pkts;
	stats->tx_packets = edev->stats.tx_ucast_pkts +
			    edev->stats.tx_mcast_pkts +
			    edev->stats.tx_bcast_pkts;

	stats->rx_bytes = edev->stats.rx_ucast_bytes +
			  edev->stats.rx_mcast_bytes +
			  edev->stats.rx_bcast_bytes;

	stats->tx_bytes = edev->stats.tx_ucast_bytes +
			  edev->stats.tx_mcast_bytes +
			  edev->stats.tx_bcast_bytes;

	stats->tx_errors = edev->stats.tx_err_drop_pkts;
	stats->multicast = edev->stats.rx_mcast_pkts +
			   edev->stats.rx_bcast_pkts;

	stats->rx_fifo_errors = edev->stats.no_buff_discards;

	stats->collisions = edev->stats.tx_total_collisions;
	stats->rx_crc_errors = edev->stats.rx_crc_errors;
	stats->rx_frame_errors = edev->stats.rx_align_errors;

	return stats;
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
#endif

static void qede_config_accept_any_vlan(struct qede_dev *edev, bool action)
{
	struct qed_update_vport_params params;
	int rc;

	/* Proceed only if action actually needs to be performed */
	if (edev->accept_any_vlan == action)
		return;

	memset(&params, 0, sizeof(params));

	params.vport_id = 0;
	params.accept_any_vlan = action;
	params.update_accept_any_vlan_flg = 1;

	rc = edev->ops->vport_update(edev->cdev, &params);
	if (rc) {
		DP_ERR(edev, "Failed to %s accept-any-vlan\n",
		       action ? "enable" : "disable");
	} else {
		DP_INFO(edev, "%s accept-any-vlan\n",
			action ? "enabled" : "disabled");
		edev->accept_any_vlan = action;
	}
}

static int qede_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_vlan *vlan, *tmp;
	int rc;

	DP_VERBOSE(edev, NETIF_MSG_IFUP, "Adding vlan 0x%04x\n", vid);

	vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan) {
		DP_INFO(edev, "Failed to allocate struct for vlan\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&vlan->list);
	vlan->vid = vid;
	vlan->configured = false;

	/* Verify vlan isn't already configured */
	list_for_each_entry(tmp, &edev->vlan_list, list) {
		if (tmp->vid == vlan->vid) {
			DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
				   "vlan already configured\n");
			kfree(vlan);
			return -EEXIST;
		}
	}

	/* If interface is down, cache this VLAN ID and return */
	if (edev->state != QEDE_STATE_OPEN) {
		DP_VERBOSE(edev, NETIF_MSG_IFDOWN,
			   "Interface is down, VLAN %d will be configured when interface is up\n",
			   vid);
		if (vid != 0)
			edev->non_configured_vlans++;
		list_add(&vlan->list, &edev->vlan_list);

		return 0;
	}

	/* Check for the filter limit.
	 * Note - vlan0 has a reserved filter and can be added without
	 * worrying about quota
	 */
	if ((edev->configured_vlans < edev->dev_info.num_vlan_filters) ||
	    (vlan->vid == 0)) {
		rc = qede_set_ucast_rx_vlan(edev,
					    QED_FILTER_XCAST_TYPE_ADD,
					    vlan->vid);
		if (rc) {
			DP_ERR(edev, "Failed to configure VLAN %d\n",
			       vlan->vid);
			kfree(vlan);
			return -EINVAL;
		}
		vlan->configured = true;

		/* vlan0 filter isn't consuming out of our quota */
		if (vlan->vid != 0)
			edev->configured_vlans++;
	} else {
		/* Out of quota; Activate accept-any-VLAN mode */
		if (!edev->non_configured_vlans)
			qede_config_accept_any_vlan(edev, true);

		edev->non_configured_vlans++;
	}

	list_add(&vlan->list, &edev->vlan_list);

	return 0;
}

static void qede_del_vlan_from_list(struct qede_dev *edev,
				    struct qede_vlan *vlan)
{
	/* vlan0 filter isn't consuming out of our quota */
	if (vlan->vid != 0) {
		if (vlan->configured)
			edev->configured_vlans--;
		else
			edev->non_configured_vlans--;
	}

	list_del(&vlan->list);
	kfree(vlan);
}

static int qede_configure_vlan_filters(struct qede_dev *edev)
{
	int rc = 0, real_rc = 0, accept_any_vlan = 0;
	struct qed_dev_eth_info *dev_info;
	struct qede_vlan *vlan = NULL;

	if (list_empty(&edev->vlan_list))
		return 0;

	dev_info = &edev->dev_info;

	/* Configure non-configured vlans */
	list_for_each_entry(vlan, &edev->vlan_list, list) {
		if (vlan->configured)
			continue;

		/* We have used all our credits, now enable accept_any_vlan */
		if ((vlan->vid != 0) &&
		    (edev->configured_vlans == dev_info->num_vlan_filters)) {
			accept_any_vlan = 1;
			continue;
		}

		DP_VERBOSE(edev, NETIF_MSG_IFUP, "Adding vlan %d\n", vlan->vid);

		rc = qede_set_ucast_rx_vlan(edev, QED_FILTER_XCAST_TYPE_ADD,
					    vlan->vid);
		if (rc) {
			DP_ERR(edev, "Failed to configure VLAN %u\n",
			       vlan->vid);
			real_rc = rc;
			continue;
		}

		vlan->configured = true;
		/* vlan0 filter doesn't consume our VLAN filter's quota */
		if (vlan->vid != 0) {
			edev->non_configured_vlans--;
			edev->configured_vlans++;
		}
	}

	/* enable accept_any_vlan mode if we have more VLANs than credits,
	 * or remove accept_any_vlan mode if we've actually removed
	 * a non-configured vlan, and all remaining vlans are truly configured.
	 */

	if (accept_any_vlan)
		qede_config_accept_any_vlan(edev, true);
	else if (!edev->non_configured_vlans)
		qede_config_accept_any_vlan(edev, false);

	return real_rc;
}

static int qede_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct qede_dev *edev = netdev_priv(dev);
	struct qede_vlan *vlan = NULL;
	int rc;

	DP_VERBOSE(edev, NETIF_MSG_IFDOWN, "Removing vlan 0x%04x\n", vid);

	/* Find whether entry exists */
	list_for_each_entry(vlan, &edev->vlan_list, list)
		if (vlan->vid == vid)
			break;

	if (!vlan || (vlan->vid != vid)) {
		DP_VERBOSE(edev, (NETIF_MSG_IFUP | NETIF_MSG_IFDOWN),
			   "Vlan isn't configured\n");
		return 0;
	}

	if (edev->state != QEDE_STATE_OPEN) {
		/* As interface is already down, we don't have a VPORT
		 * instance to remove vlan filter. So just update vlan list
		 */
		DP_VERBOSE(edev, NETIF_MSG_IFDOWN,
			   "Interface is down, removing VLAN from list only\n");
		qede_del_vlan_from_list(edev, vlan);
		return 0;
	}

	/* Remove vlan */
	if (vlan->configured) {
		rc = qede_set_ucast_rx_vlan(edev, QED_FILTER_XCAST_TYPE_DEL,
					    vid);
		if (rc) {
			DP_ERR(edev, "Failed to remove VLAN %d\n", vid);
			return -EINVAL;
		}
	}

	qede_del_vlan_from_list(edev, vlan);

	/* We have removed a VLAN - try to see if we can
	 * configure non-configured VLAN from the list.
	 */
	rc = qede_configure_vlan_filters(edev);

	return rc;
}

static void qede_vlan_mark_nonconfigured(struct qede_dev *edev)
{
	struct qede_vlan *vlan = NULL;

	if (list_empty(&edev->vlan_list))
		return;

	list_for_each_entry(vlan, &edev->vlan_list, list) {
		if (!vlan->configured)
			continue;

		vlan->configured = false;

		/* vlan0 filter isn't consuming out of our quota */
		if (vlan->vid != 0) {
			edev->non_configured_vlans++;
			edev->configured_vlans--;
		}

		DP_VERBOSE(edev, NETIF_MSG_IFDOWN,
			   "marked vlan %d as non-configured\n", vlan->vid);
	}

	edev->accept_any_vlan = false;
}

static int qede_set_features(struct net_device *dev, netdev_features_t features)
{
	struct qede_dev *edev = netdev_priv(dev);
	netdev_features_t changes = features ^ dev->features;
	bool need_reload = false;

	/* No action needed if hardware GRO is disabled during driver load */
	if (changes & NETIF_F_GRO) {
		if (dev->features & NETIF_F_GRO)
			need_reload = !edev->gro_disable;
		else
			need_reload = edev->gro_disable;
	}

	if (need_reload && netif_running(edev->ndev)) {
		dev->features = features;
		qede_reload(edev, NULL, NULL);
		return 1;
	}

	return 0;
}

static void qede_udp_tunnel_add(struct net_device *dev,
				struct udp_tunnel_info *ti)
{
	struct qede_dev *edev = netdev_priv(dev);
	u16 t_port = ntohs(ti->port);

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		if (edev->vxlan_dst_port)
			return;

		edev->vxlan_dst_port = t_port;

		DP_VERBOSE(edev, QED_MSG_DEBUG, "Added vxlan port=%d\n",
			   t_port);

		set_bit(QEDE_SP_VXLAN_PORT_CONFIG, &edev->sp_flags);
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		if (edev->geneve_dst_port)
			return;

		edev->geneve_dst_port = t_port;

		DP_VERBOSE(edev, QED_MSG_DEBUG, "Added geneve port=%d\n",
			   t_port);
		set_bit(QEDE_SP_GENEVE_PORT_CONFIG, &edev->sp_flags);
		break;
	default:
		return;
	}

	schedule_delayed_work(&edev->sp_task, 0);
}

static void qede_udp_tunnel_del(struct net_device *dev,
				struct udp_tunnel_info *ti)
{
	struct qede_dev *edev = netdev_priv(dev);
	u16 t_port = ntohs(ti->port);

	switch (ti->type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		if (t_port != edev->vxlan_dst_port)
			return;

		edev->vxlan_dst_port = 0;

		DP_VERBOSE(edev, QED_MSG_DEBUG, "Deleted vxlan port=%d\n",
			   t_port);

		set_bit(QEDE_SP_VXLAN_PORT_CONFIG, &edev->sp_flags);
		break;
	case UDP_TUNNEL_TYPE_GENEVE:
		if (t_port != edev->geneve_dst_port)
			return;

		edev->geneve_dst_port = 0;

		DP_VERBOSE(edev, QED_MSG_DEBUG, "Deleted geneve port=%d\n",
			   t_port);
		set_bit(QEDE_SP_GENEVE_PORT_CONFIG, &edev->sp_flags);
		break;
	default:
		return;
	}

	schedule_delayed_work(&edev->sp_task, 0);
}

static const struct net_device_ops qede_netdev_ops = {
	.ndo_open = qede_open,
	.ndo_stop = qede_close,
	.ndo_start_xmit = qede_start_xmit,
	.ndo_set_rx_mode = qede_set_rx_mode,
	.ndo_set_mac_address = qede_set_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = qede_change_mtu,
#ifdef CONFIG_QED_SRIOV
	.ndo_set_vf_mac = qede_set_vf_mac,
	.ndo_set_vf_vlan = qede_set_vf_vlan,
#endif
	.ndo_vlan_rx_add_vid = qede_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = qede_vlan_rx_kill_vid,
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
				  info->num_queues, info->num_queues);
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

	edev->num_tc = edev->dev_info.num_tc;

	INIT_LIST_HEAD(&edev->vlan_list);

	return edev;
}

static void qede_init_ndev(struct qede_dev *edev)
{
	struct net_device *ndev = edev->ndev;
	struct pci_dev *pdev = edev->pdev;
	u32 hw_features;

	pci_set_drvdata(pdev, ndev);

	ndev->mem_start = edev->dev_info.common.pci_mem_start;
	ndev->base_addr = ndev->mem_start;
	ndev->mem_end = edev->dev_info.common.pci_mem_end;
	ndev->irq = edev->dev_info.common.pci_irq;

	ndev->watchdog_timeo = TX_TIMEOUT;

	ndev->netdev_ops = &qede_netdev_ops;

	qede_set_ethtool_ops(ndev);

	/* user-changeble features */
	hw_features = NETIF_F_GRO | NETIF_F_SG |
		      NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		      NETIF_F_TSO | NETIF_F_TSO6;

	/* Encap features*/
	hw_features |= NETIF_F_GSO_GRE | NETIF_F_GSO_UDP_TUNNEL |
		       NETIF_F_TSO_ECN;
	ndev->hw_enc_features = NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
				NETIF_F_SG | NETIF_F_TSO | NETIF_F_TSO_ECN |
				NETIF_F_TSO6 | NETIF_F_GSO_GRE |
				NETIF_F_GSO_UDP_TUNNEL | NETIF_F_RXCSUM;

	ndev->vlan_features = hw_features | NETIF_F_RXHASH | NETIF_F_RXCSUM |
			      NETIF_F_HIGHDMA;
	ndev->features = hw_features | NETIF_F_RXHASH | NETIF_F_RXCSUM |
			 NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HIGHDMA |
			 NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_CTAG_TX;

	ndev->hw_features = hw_features;

	/* Set network device HW mac */
	ether_addr_copy(edev->ndev->dev_addr, edev->dev_info.common.hw_mac);
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
			kfree(fp->rxq);
			kfree(fp->txqs);
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

		fp->sb_info = kcalloc(1, sizeof(*fp->sb_info), GFP_KERNEL);
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
			fp->txqs = kcalloc(edev->num_tc, sizeof(*fp->txqs),
					   GFP_KERNEL);
			if (!fp->txqs) {
				DP_NOTICE(edev,
					  "TXQ array allocation failed\n");
				goto err;
			}
		}

		if (fp->type & QEDE_FASTPATH_RX) {
			fp->rxq = kcalloc(1, sizeof(*fp->rxq), GFP_KERNEL);
			if (!fp->rxq) {
				DP_NOTICE(edev,
					  "RXQ struct allocation failed\n");
				goto err;
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
	struct qed_dev *cdev = edev->cdev;

	mutex_lock(&edev->qede_lock);

	if (edev->state == QEDE_STATE_OPEN) {
		if (test_and_clear_bit(QEDE_SP_RX_MODE, &edev->sp_flags))
			qede_config_rx_mode(edev->ndev);
	}

	if (test_and_clear_bit(QEDE_SP_VXLAN_PORT_CONFIG, &edev->sp_flags)) {
		struct qed_tunn_params tunn_params;

		memset(&tunn_params, 0, sizeof(tunn_params));
		tunn_params.update_vxlan_port = 1;
		tunn_params.vxlan_port = edev->vxlan_dst_port;
		qed_ops->tunn_config(cdev, &tunn_params);
	}

	if (test_and_clear_bit(QEDE_SP_GENEVE_PORT_CONFIG, &edev->sp_flags)) {
		struct qed_tunn_params tunn_params;

		memset(&tunn_params, 0, sizeof(tunn_params));
		tunn_params.update_geneve_port = 1;
		tunn_params.geneve_port = edev->geneve_dst_port;
		qed_ops->tunn_config(cdev, &tunn_params);
	}

	mutex_unlock(&edev->qede_lock);
}

static void qede_update_pf_params(struct qed_dev *cdev)
{
	struct qed_pf_params pf_params;

	/* 64 rx + 64 tx */
	memset(&pf_params, 0, sizeof(struct qed_pf_params));
	pf_params.eth_pf_params.num_cons = 128;
	qed_ops->common->update_pf_params(cdev, &pf_params);
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

	rc = register_netdev(edev->ndev);
	if (rc) {
		DP_NOTICE(edev, "Cannot register net-device\n");
		goto err3;
	}

	edev->ops->common->set_id(cdev, edev->ndev->name, DRV_MODULE_VERSION);

	edev->ops->register_ops(cdev, &qede_ll_ops, edev);

#ifdef CONFIG_DCB
	if (!IS_VF(edev))
		qede_set_dcbnl_ops(edev->ndev);
#endif

	INIT_DELAYED_WORK(&edev->sp_task, qede_sp_task);
	mutex_init(&edev->qede_lock);
	edev->rx_copybreak = QEDE_RX_HDR_SIZE;

	DP_INFO(edev, "Ending successfully qede probe\n");

	return 0;

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
	struct qede_dev *edev = netdev_priv(ndev);
	struct qed_dev *cdev = edev->cdev;

	DP_INFO(edev, "Starting qede_remove\n");

	cancel_delayed_work_sync(&edev->sp_task);
	unregister_netdev(ndev);

	edev->ops->common->set_power_state(cdev, PCI_D0);

	pci_set_drvdata(pdev, NULL);

	free_netdev(ndev);

	/* Use global ops since we've freed edev */
	qed_ops->common->slowpath_stop(cdev);
	qed_ops->common->remove(cdev);

	dev_info(&pdev->dev, "Ending qede_remove successfully\n");
}

static void qede_remove(struct pci_dev *pdev)
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

static void qede_free_mem_sb(struct qede_dev *edev,
			     struct qed_sb_info *sb_info)
{
	if (sb_info->sb_virt)
		dma_free_coherent(&edev->pdev->dev, sizeof(*sb_info->sb_virt),
				  (void *)sb_info->sb_virt, sb_info->sb_phys);
}

/* This function allocates fast-path status block memory */
static int qede_alloc_mem_sb(struct qede_dev *edev,
			     struct qed_sb_info *sb_info, u16 sb_id)
{
	struct status_block *sb_virt;
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
			       rx_buf->mapping, PAGE_SIZE, DMA_FROM_DEVICE);

		rx_buf->data = NULL;
		__free_page(data);
	}
}

static void qede_free_sge_mem(struct qede_dev *edev, struct qede_rx_queue *rxq)
{
	int i;

	if (edev->gro_disable)
		return;

	for (i = 0; i < ETH_TPA_MAX_AGGS_NUM; i++) {
		struct qede_agg_info *tpa_info = &rxq->tpa_info[i];
		struct sw_rx_data *replace_buf = &tpa_info->replace_buf;

		if (replace_buf->data) {
			dma_unmap_page(&edev->pdev->dev,
				       replace_buf->mapping,
				       PAGE_SIZE, DMA_FROM_DEVICE);
			__free_page(replace_buf->data);
		}
	}
}

static void qede_free_mem_rxq(struct qede_dev *edev, struct qede_rx_queue *rxq)
{
	qede_free_sge_mem(edev, rxq);

	/* Free rx buffers */
	qede_free_rx_buffers(edev, rxq);

	/* Free the parallel SW ring */
	kfree(rxq->sw_rx_ring);

	/* Free the real RQ ring used by FW */
	edev->ops->common->chain_free(edev->cdev, &rxq->rx_bd_ring);
	edev->ops->common->chain_free(edev->cdev, &rxq->rx_comp_ring);
}

static int qede_alloc_rx_buffer(struct qede_dev *edev,
				struct qede_rx_queue *rxq)
{
	struct sw_rx_data *sw_rx_data;
	struct eth_rx_bd *rx_bd;
	dma_addr_t mapping;
	struct page *data;

	data = alloc_pages(GFP_ATOMIC, 0);
	if (unlikely(!data)) {
		DP_NOTICE(edev, "Failed to allocate Rx data [page]\n");
		return -ENOMEM;
	}

	/* Map the entire page as it would be used
	 * for multiple RX buffer segment size mapping.
	 */
	mapping = dma_map_page(&edev->pdev->dev, data, 0,
			       PAGE_SIZE, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(&edev->pdev->dev, mapping))) {
		__free_page(data);
		DP_NOTICE(edev, "Failed to map Rx buffer\n");
		return -ENOMEM;
	}

	sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_prod & NUM_RX_BDS_MAX];
	sw_rx_data->page_offset = 0;
	sw_rx_data->data = data;
	sw_rx_data->mapping = mapping;

	/* Advance PROD and get BD pointer */
	rx_bd = (struct eth_rx_bd *)qed_chain_produce(&rxq->rx_bd_ring);
	WARN_ON(!rx_bd);
	rx_bd->addr.hi = cpu_to_le32(upper_32_bits(mapping));
	rx_bd->addr.lo = cpu_to_le32(lower_32_bits(mapping));

	rxq->sw_rx_prod++;

	return 0;
}

static int qede_alloc_sge_mem(struct qede_dev *edev, struct qede_rx_queue *rxq)
{
	dma_addr_t mapping;
	int i;

	if (edev->gro_disable)
		return 0;

	if (edev->ndev->mtu > PAGE_SIZE) {
		edev->gro_disable = 1;
		return 0;
	}

	for (i = 0; i < ETH_TPA_MAX_AGGS_NUM; i++) {
		struct qede_agg_info *tpa_info = &rxq->tpa_info[i];
		struct sw_rx_data *replace_buf = &tpa_info->replace_buf;

		replace_buf->data = alloc_pages(GFP_ATOMIC, 0);
		if (unlikely(!replace_buf->data)) {
			DP_NOTICE(edev,
				  "Failed to allocate TPA skb pool [replacement buffer]\n");
			goto err;
		}

		mapping = dma_map_page(&edev->pdev->dev, replace_buf->data, 0,
				       rxq->rx_buf_size, DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(&edev->pdev->dev, mapping))) {
			DP_NOTICE(edev,
				  "Failed to map TPA replacement buffer\n");
			goto err;
		}

		replace_buf->mapping = mapping;
		tpa_info->replace_buf.page_offset = 0;

		tpa_info->replace_buf_mapping = mapping;
		tpa_info->agg_state = QEDE_AGG_STATE_NONE;
	}

	return 0;
err:
	qede_free_sge_mem(edev, rxq);
	edev->gro_disable = 1;
	return -ENOMEM;
}

/* This function allocates all memory needed per Rx queue */
static int qede_alloc_mem_rxq(struct qede_dev *edev, struct qede_rx_queue *rxq)
{
	int i, rc, size;

	rxq->num_rx_buffers = edev->q_num_rx_buffers;

	rxq->rx_buf_size = NET_IP_ALIGN + ETH_OVERHEAD + edev->ndev->mtu;

	if (rxq->rx_buf_size > PAGE_SIZE)
		rxq->rx_buf_size = PAGE_SIZE;

	/* Segment size to spilt a page in multiple equal parts */
	rxq->rx_buf_seg_size = roundup_pow_of_two(rxq->rx_buf_size);

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
					    &rxq->rx_bd_ring);

	if (rc)
		goto err;

	/* Allocate FW completion ring */
	rc = edev->ops->common->chain_alloc(edev->cdev,
					    QED_CHAIN_USE_TO_CONSUME,
					    QED_CHAIN_MODE_PBL,
					    QED_CHAIN_CNT_TYPE_U16,
					    RX_RING_SIZE,
					    sizeof(union eth_rx_cqe),
					    &rxq->rx_comp_ring);
	if (rc)
		goto err;

	/* Allocate buffers for the Rx ring */
	for (i = 0; i < rxq->num_rx_buffers; i++) {
		rc = qede_alloc_rx_buffer(edev, rxq);
		if (rc) {
			DP_ERR(edev,
			       "Rx buffers allocation failed at index %d\n", i);
			goto err;
		}
	}

	rc = qede_alloc_sge_mem(edev, rxq);
err:
	return rc;
}

static void qede_free_mem_txq(struct qede_dev *edev, struct qede_tx_queue *txq)
{
	/* Free the parallel SW ring */
	kfree(txq->sw_tx_ring);

	/* Free the real RQ ring used by FW */
	edev->ops->common->chain_free(edev->cdev, &txq->tx_pbl);
}

/* This function allocates all memory needed per Tx queue */
static int qede_alloc_mem_txq(struct qede_dev *edev, struct qede_tx_queue *txq)
{
	int size, rc;
	union eth_tx_bd_types *p_virt;

	txq->num_tx_buffers = edev->q_num_tx_buffers;

	/* Allocate the parallel driver ring for Tx buffers */
	size = sizeof(*txq->sw_tx_ring) * NUM_TX_BDS_MAX;
	txq->sw_tx_ring = kzalloc(size, GFP_KERNEL);
	if (!txq->sw_tx_ring) {
		DP_NOTICE(edev, "Tx buffers ring allocation failed\n");
		goto err;
	}

	rc = edev->ops->common->chain_alloc(edev->cdev,
					    QED_CHAIN_USE_TO_CONSUME_PRODUCE,
					    QED_CHAIN_MODE_PBL,
					    QED_CHAIN_CNT_TYPE_U16,
					    NUM_TX_BDS_MAX,
					    sizeof(*p_virt), &txq->tx_pbl);
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
	int tc;

	qede_free_mem_sb(edev, fp->sb_info);

	if (fp->type & QEDE_FASTPATH_RX)
		qede_free_mem_rxq(edev, fp->rxq);

	if (fp->type & QEDE_FASTPATH_TX)
		for (tc = 0; tc < edev->num_tc; tc++)
			qede_free_mem_txq(edev, &fp->txqs[tc]);
}

/* This function allocates all memory needed for a single fp (i.e. an entity
 * which contains status block, one rx queue and/or multiple per-TC tx queues.
 */
static int qede_alloc_mem_fp(struct qede_dev *edev, struct qede_fastpath *fp)
{
	int rc, tc;

	rc = qede_alloc_mem_sb(edev, fp->sb_info, fp->id);
	if (rc)
		goto err;

	if (fp->type & QEDE_FASTPATH_RX) {
		rc = qede_alloc_mem_rxq(edev, fp->rxq);
		if (rc)
			goto err;
	}

	if (fp->type & QEDE_FASTPATH_TX) {
		for (tc = 0; tc < edev->num_tc; tc++) {
			rc = qede_alloc_mem_txq(edev, &fp->txqs[tc]);
			if (rc)
				goto err;
		}
	}

	return 0;
err:
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
	int queue_id, rxq_index = 0, txq_index = 0, tc;
	struct qede_fastpath *fp;

	for_each_queue(queue_id) {
		fp = &edev->fp_array[queue_id];

		fp->edev = edev;
		fp->id = queue_id;

		memset((void *)&fp->napi, 0, sizeof(fp->napi));

		memset((void *)fp->sb_info, 0, sizeof(*fp->sb_info));

		if (fp->type & QEDE_FASTPATH_RX) {
			memset((void *)fp->rxq, 0, sizeof(*fp->rxq));
			fp->rxq->rxq_id = rxq_index++;
		}

		if (fp->type & QEDE_FASTPATH_TX) {
			memset((void *)fp->txqs, 0,
			       (edev->num_tc * sizeof(*fp->txqs)));
			for (tc = 0; tc < edev->num_tc; tc++) {
				fp->txqs[tc].index = txq_index +
				    tc * QEDE_TSS_COUNT(edev);
				if (edev->dev_info.is_legacy)
					fp->txqs[tc].is_legacy = true;
			}
			txq_index++;
		}

		snprintf(fp->name, sizeof(fp->name), "%s-fp-%d",
			 edev->ndev->name, queue_id);
	}

	edev->gro_disable = !(edev->ndev->features & NETIF_F_GRO);
}

static int qede_set_real_num_queues(struct qede_dev *edev)
{
	int rc = 0;

	rc = netif_set_real_num_tx_queues(edev->ndev, QEDE_TSS_COUNT(edev));
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

static int qede_stop_queues(struct qede_dev *edev)
{
	struct qed_update_vport_params vport_update_params;
	struct qed_dev *cdev = edev->cdev;
	int rc, tc, i;

	/* Disable the vport */
	memset(&vport_update_params, 0, sizeof(vport_update_params));
	vport_update_params.vport_id = 0;
	vport_update_params.update_vport_active_flg = 1;
	vport_update_params.vport_active_flg = 0;
	vport_update_params.update_rss_flg = 0;

	rc = edev->ops->vport_update(cdev, &vport_update_params);
	if (rc) {
		DP_ERR(edev, "Failed to update vport\n");
		return rc;
	}

	/* Flush Tx queues. If needed, request drain from MCP */
	for_each_queue(i) {
		struct qede_fastpath *fp = &edev->fp_array[i];

		if (fp->type & QEDE_FASTPATH_TX) {
			for (tc = 0; tc < edev->num_tc; tc++) {
				struct qede_tx_queue *txq = &fp->txqs[tc];

				rc = qede_drain_txq(edev, txq, true);
				if (rc)
					return rc;
			}
		}
	}

	/* Stop all Queues in reverse order */
	for (i = QEDE_QUEUE_CNT(edev) - 1; i >= 0; i--) {
		struct qed_stop_rxq_params rx_params;

		/* Stop the Tx Queue(s) */
		if (edev->fp_array[i].type & QEDE_FASTPATH_TX) {
			for (tc = 0; tc < edev->num_tc; tc++) {
				struct qed_stop_txq_params tx_params;
				u8 val;

				tx_params.rss_id = i;
				val = edev->fp_array[i].txqs[tc].index;
				tx_params.tx_queue_id = val;
				rc = edev->ops->q_tx_stop(cdev, &tx_params);
				if (rc) {
					DP_ERR(edev, "Failed to stop TXQ #%d\n",
					       tx_params.tx_queue_id);
					return rc;
				}
			}
		}

		/* Stop the Rx Queue */
		if (edev->fp_array[i].type & QEDE_FASTPATH_RX) {
			memset(&rx_params, 0, sizeof(rx_params));
			rx_params.rss_id = i;
			rx_params.rx_queue_id = edev->fp_array[i].rxq->rxq_id;

			rc = edev->ops->q_rx_stop(cdev, &rx_params);
			if (rc) {
				DP_ERR(edev, "Failed to stop RXQ #%d\n", i);
				return rc;
			}
		}
	}

	/* Stop the vport */
	rc = edev->ops->vport_stop(cdev, 0);
	if (rc)
		DP_ERR(edev, "Failed to stop VPORT\n");

	return rc;
}

static int qede_start_queues(struct qede_dev *edev, bool clear_stats)
{
	int rc, tc, i;
	int vlan_removal_en = 1;
	struct qed_dev *cdev = edev->cdev;
	struct qed_update_vport_params vport_update_params;
	struct qed_queue_start_common_params q_params;
	struct qed_dev_info *qed_info = &edev->dev_info.common;
	struct qed_start_vport_params start = {0};
	bool reset_rss_indir = false;

	if (!edev->num_queues) {
		DP_ERR(edev,
		       "Cannot update V-VPORT as active as there are no Rx queues\n");
		return -EINVAL;
	}

	start.gro_enable = !edev->gro_disable;
	start.mtu = edev->ndev->mtu;
	start.vport_id = 0;
	start.drop_ttl0 = true;
	start.remove_inner_vlan = vlan_removal_en;
	start.clear_stats = clear_stats;

	rc = edev->ops->vport_start(cdev, &start);

	if (rc) {
		DP_ERR(edev, "Start V-PORT failed %d\n", rc);
		return rc;
	}

	DP_VERBOSE(edev, NETIF_MSG_IFUP,
		   "Start vport ramrod passed, vport_id = %d, MTU = %d, vlan_removal_en = %d\n",
		   start.vport_id, edev->ndev->mtu + 0xe, vlan_removal_en);

	for_each_queue(i) {
		struct qede_fastpath *fp = &edev->fp_array[i];
		dma_addr_t p_phys_table;
		u32 page_cnt;

		if (fp->type & QEDE_FASTPATH_RX) {
			struct qede_rx_queue *rxq = fp->rxq;
			__le16 *val;

			memset(&q_params, 0, sizeof(q_params));
			q_params.rss_id = i;
			q_params.queue_id = rxq->rxq_id;
			q_params.vport_id = 0;
			q_params.sb = fp->sb_info->igu_sb_id;
			q_params.sb_idx = RX_PI;

			p_phys_table =
			    qed_chain_get_pbl_phys(&rxq->rx_comp_ring);
			page_cnt = qed_chain_get_page_cnt(&rxq->rx_comp_ring);

			rc = edev->ops->q_rx_start(cdev, &q_params,
						   rxq->rx_buf_size,
						   rxq->rx_bd_ring.p_phys_addr,
						   p_phys_table,
						   page_cnt,
						   &rxq->hw_rxq_prod_addr);
			if (rc) {
				DP_ERR(edev, "Start RXQ #%d failed %d\n", i,
				       rc);
				return rc;
			}

			val = &fp->sb_info->sb_virt->pi_array[RX_PI];
			rxq->hw_cons_ptr = val;

			qede_update_rx_prod(edev, rxq);
		}

		if (!(fp->type & QEDE_FASTPATH_TX))
			continue;

		for (tc = 0; tc < edev->num_tc; tc++) {
			struct qede_tx_queue *txq = &fp->txqs[tc];

			p_phys_table = qed_chain_get_pbl_phys(&txq->tx_pbl);
			page_cnt = qed_chain_get_page_cnt(&txq->tx_pbl);

			memset(&q_params, 0, sizeof(q_params));
			q_params.rss_id = i;
			q_params.queue_id = txq->index;
			q_params.vport_id = 0;
			q_params.sb = fp->sb_info->igu_sb_id;
			q_params.sb_idx = TX_PI(tc);

			rc = edev->ops->q_tx_start(cdev, &q_params,
						   p_phys_table, page_cnt,
						   &txq->doorbell_addr);
			if (rc) {
				DP_ERR(edev, "Start TXQ #%d failed %d\n",
				       txq->index, rc);
				return rc;
			}

			txq->hw_cons_ptr =
				&fp->sb_info->sb_virt->pi_array[TX_PI(tc)];
			SET_FIELD(txq->tx_db.data.params,
				  ETH_DB_DATA_DEST, DB_DEST_XCM);
			SET_FIELD(txq->tx_db.data.params, ETH_DB_DATA_AGG_CMD,
				  DB_AGG_CMD_SET);
			SET_FIELD(txq->tx_db.data.params,
				  ETH_DB_DATA_AGG_VAL_SEL,
				  DQ_XCM_ETH_TX_BD_PROD_CMD);

			txq->tx_db.data.agg_flags = DQ_XCM_ETH_DQ_CF_CMD;
		}
	}

	/* Prepare and send the vport enable */
	memset(&vport_update_params, 0, sizeof(vport_update_params));
	vport_update_params.vport_id = start.vport_id;
	vport_update_params.update_vport_active_flg = 1;
	vport_update_params.vport_active_flg = 1;

	if ((qed_info->mf_mode == QED_MF_NPAR || pci_num_vf(edev->pdev)) &&
	    qed_info->tx_switching) {
		vport_update_params.update_tx_switching_flg = 1;
		vport_update_params.tx_switching_flg = 1;
	}

	/* Fill struct with RSS params */
	if (QEDE_RSS_COUNT(edev) > 1) {
		vport_update_params.update_rss_flg = 1;

		/* Need to validate current RSS config uses valid entries */
		for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++) {
			if (edev->rss_params.rss_ind_table[i] >=
			    QEDE_RSS_COUNT(edev)) {
				reset_rss_indir = true;
				break;
			}
		}

		if (!(edev->rss_params_inited & QEDE_RSS_INDIR_INITED) ||
		    reset_rss_indir) {
			u16 val;

			for (i = 0; i < QED_RSS_IND_TABLE_SIZE; i++) {
				u16 indir_val;

				val = QEDE_RSS_COUNT(edev);
				indir_val = ethtool_rxfh_indir_default(i, val);
				edev->rss_params.rss_ind_table[i] = indir_val;
			}
			edev->rss_params_inited |= QEDE_RSS_INDIR_INITED;
		}

		if (!(edev->rss_params_inited & QEDE_RSS_KEY_INITED)) {
			netdev_rss_key_fill(edev->rss_params.rss_key,
					    sizeof(edev->rss_params.rss_key));
			edev->rss_params_inited |= QEDE_RSS_KEY_INITED;
		}

		if (!(edev->rss_params_inited & QEDE_RSS_CAPS_INITED)) {
			edev->rss_params.rss_caps = QED_RSS_IPV4 |
						    QED_RSS_IPV6 |
						    QED_RSS_IPV4_TCP |
						    QED_RSS_IPV6_TCP;
			edev->rss_params_inited |= QEDE_RSS_CAPS_INITED;
		}

		memcpy(&vport_update_params.rss_params, &edev->rss_params,
		       sizeof(vport_update_params.rss_params));
	} else {
		memset(&vport_update_params.rss_params, 0,
		       sizeof(vport_update_params.rss_params));
	}

	rc = edev->ops->vport_update(cdev, &vport_update_params);
	if (rc) {
		DP_ERR(edev, "Update V-PORT failed %d\n", rc);
		return rc;
	}

	return 0;
}

static int qede_set_mcast_rx_mac(struct qede_dev *edev,
				 enum qed_filter_xcast_params_type opcode,
				 unsigned char *mac, int num_macs)
{
	struct qed_filter_params filter_cmd;
	int i;

	memset(&filter_cmd, 0, sizeof(filter_cmd));
	filter_cmd.type = QED_FILTER_TYPE_MCAST;
	filter_cmd.filter.mcast.type = opcode;
	filter_cmd.filter.mcast.num = num_macs;

	for (i = 0; i < num_macs; i++, mac += ETH_ALEN)
		ether_addr_copy(filter_cmd.filter.mcast.mac[i], mac);

	return edev->ops->filter_config(edev->cdev, &filter_cmd);
}

enum qede_unload_mode {
	QEDE_UNLOAD_NORMAL,
};

static void qede_unload(struct qede_dev *edev, enum qede_unload_mode mode)
{
	struct qed_link_params link_params;
	int rc;

	DP_INFO(edev, "Starting qede unload\n");

	mutex_lock(&edev->qede_lock);
	edev->state = QEDE_STATE_CLOSED;

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

	/* Release the interrupts */
	qede_sync_free_irqs(edev);
	edev->ops->common->set_fp_int(edev->cdev, 0);

	qede_napi_disable_remove(edev);

	qede_free_mem_load(edev);
	qede_free_fp_array(edev);

out:
	mutex_unlock(&edev->qede_lock);
	DP_INFO(edev, "Ending qede unload\n");
}

enum qede_load_mode {
	QEDE_LOAD_NORMAL,
	QEDE_LOAD_RELOAD,
};

static int qede_load(struct qede_dev *edev, enum qede_load_mode mode)
{
	struct qed_link_params link_params;
	struct qed_link_output link_output;
	int rc;

	DP_INFO(edev, "Starting qede load\n");

	rc = qede_set_num_queues(edev);
	if (rc)
		goto err0;

	rc = qede_alloc_fp_array(edev);
	if (rc)
		goto err0;

	qede_init_fp(edev);

	rc = qede_alloc_mem_load(edev);
	if (rc)
		goto err1;
	DP_INFO(edev, "Allocated %d RSS queues on %d TC/s\n",
		QEDE_QUEUE_CNT(edev), edev->num_tc);

	rc = qede_set_real_num_queues(edev);
	if (rc)
		goto err2;

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

	/* Add primary mac and set Rx filters */
	ether_addr_copy(edev->primary_mac, edev->ndev->dev_addr);

	mutex_lock(&edev->qede_lock);
	edev->state = QEDE_STATE_OPEN;
	mutex_unlock(&edev->qede_lock);

	/* Program un-configured VLANs */
	qede_configure_vlan_filters(edev);

	/* Ask for link-up using current configuration */
	memset(&link_params, 0, sizeof(link_params));
	link_params.link_up = true;
	edev->ops->common->set_link(edev->cdev, &link_params);

	/* Query whether link is already-up */
	memset(&link_output, 0, sizeof(link_output));
	edev->ops->common->get_link(edev->cdev, &link_output);
	qede_link_update(edev, &link_output);

	DP_INFO(edev, "Ending successfully qede load\n");

	return 0;

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
err0:
	return rc;
}

void qede_reload(struct qede_dev *edev,
		 void (*func)(struct qede_dev *, union qede_reload_args *),
		 union qede_reload_args *args)
{
	qede_unload(edev, QEDE_UNLOAD_NORMAL);
	/* Call function handler to update parameters
	 * needed for function load.
	 */
	if (func)
		func(edev, args);

	qede_load(edev, QEDE_LOAD_RELOAD);

	mutex_lock(&edev->qede_lock);
	qede_config_rx_mode(edev->ndev);
	mutex_unlock(&edev->qede_lock);
}

/* called with rtnl_lock */
static int qede_open(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);
	int rc;

	netif_carrier_off(ndev);

	edev->ops->common->set_power_state(edev->cdev, PCI_D0);

	rc = qede_load(edev, QEDE_LOAD_NORMAL);

	if (rc)
		return rc;

	udp_tunnel_get_rx_info(ndev);

	return 0;
}

static int qede_close(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);

	qede_unload(edev, QEDE_UNLOAD_NORMAL);

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
		}
	} else {
		if (netif_carrier_ok(edev->ndev)) {
			DP_NOTICE(edev, "Link is down\n");
			netif_tx_disable(edev->ndev);
			netif_carrier_off(edev->ndev);
		}
	}
}

static int qede_set_mac_addr(struct net_device *ndev, void *p)
{
	struct qede_dev *edev = netdev_priv(ndev);
	struct sockaddr *addr = p;
	int rc;

	ASSERT_RTNL(); /* @@@TBD To be removed */

	DP_INFO(edev, "Set_mac_addr called\n");

	if (!is_valid_ether_addr(addr->sa_data)) {
		DP_NOTICE(edev, "The MAC address is not valid\n");
		return -EFAULT;
	}

	if (!edev->ops->check_mac(edev->cdev, addr->sa_data)) {
		DP_NOTICE(edev, "qed prevents setting MAC\n");
		return -EINVAL;
	}

	ether_addr_copy(ndev->dev_addr, addr->sa_data);

	if (!netif_running(ndev))  {
		DP_NOTICE(edev, "The device is currently down\n");
		return 0;
	}

	/* Remove the previous primary mac */
	rc = qede_set_ucast_rx_mac(edev, QED_FILTER_XCAST_TYPE_DEL,
				   edev->primary_mac);
	if (rc)
		return rc;

	/* Add MAC filter according to the new unicast HW MAC address */
	ether_addr_copy(edev->primary_mac, ndev->dev_addr);
	return qede_set_ucast_rx_mac(edev, QED_FILTER_XCAST_TYPE_ADD,
				      edev->primary_mac);
}

static int
qede_configure_mcast_filtering(struct net_device *ndev,
			       enum qed_filter_rx_mode_type *accept_flags)
{
	struct qede_dev *edev = netdev_priv(ndev);
	unsigned char *mc_macs, *temp;
	struct netdev_hw_addr *ha;
	int rc = 0, mc_count;
	size_t size;

	size = 64 * ETH_ALEN;

	mc_macs = kzalloc(size, GFP_KERNEL);
	if (!mc_macs) {
		DP_NOTICE(edev,
			  "Failed to allocate memory for multicast MACs\n");
		rc = -ENOMEM;
		goto exit;
	}

	temp = mc_macs;

	/* Remove all previously configured MAC filters */
	rc = qede_set_mcast_rx_mac(edev, QED_FILTER_XCAST_TYPE_DEL,
				   mc_macs, 1);
	if (rc)
		goto exit;

	netif_addr_lock_bh(ndev);

	mc_count = netdev_mc_count(ndev);
	if (mc_count < 64) {
		netdev_for_each_mc_addr(ha, ndev) {
			ether_addr_copy(temp, ha->addr);
			temp += ETH_ALEN;
		}
	}

	netif_addr_unlock_bh(ndev);

	/* Check for all multicast @@@TBD resource allocation */
	if ((ndev->flags & IFF_ALLMULTI) ||
	    (mc_count > 64)) {
		if (*accept_flags == QED_FILTER_RX_MODE_TYPE_REGULAR)
			*accept_flags = QED_FILTER_RX_MODE_TYPE_MULTI_PROMISC;
	} else {
		/* Add all multicast MAC filters */
		rc = qede_set_mcast_rx_mac(edev, QED_FILTER_XCAST_TYPE_ADD,
					   mc_macs, mc_count);
	}

exit:
	kfree(mc_macs);
	return rc;
}

static void qede_set_rx_mode(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);

	DP_INFO(edev, "qede_set_rx_mode called\n");

	if (edev->state != QEDE_STATE_OPEN) {
		DP_INFO(edev,
			"qede_set_rx_mode called while interface is down\n");
	} else {
		set_bit(QEDE_SP_RX_MODE, &edev->sp_flags);
		schedule_delayed_work(&edev->sp_task, 0);
	}
}

/* Must be called with qede_lock held */
static void qede_config_rx_mode(struct net_device *ndev)
{
	enum qed_filter_rx_mode_type accept_flags = QED_FILTER_TYPE_UCAST;
	struct qede_dev *edev = netdev_priv(ndev);
	struct qed_filter_params rx_mode;
	unsigned char *uc_macs, *temp;
	struct netdev_hw_addr *ha;
	int rc, uc_count;
	size_t size;

	netif_addr_lock_bh(ndev);

	uc_count = netdev_uc_count(ndev);
	size = uc_count * ETH_ALEN;

	uc_macs = kzalloc(size, GFP_ATOMIC);
	if (!uc_macs) {
		DP_NOTICE(edev, "Failed to allocate memory for unicast MACs\n");
		netif_addr_unlock_bh(ndev);
		return;
	}

	temp = uc_macs;
	netdev_for_each_uc_addr(ha, ndev) {
		ether_addr_copy(temp, ha->addr);
		temp += ETH_ALEN;
	}

	netif_addr_unlock_bh(ndev);

	/* Configure the struct for the Rx mode */
	memset(&rx_mode, 0, sizeof(struct qed_filter_params));
	rx_mode.type = QED_FILTER_TYPE_RX_MODE;

	/* Remove all previous unicast secondary macs and multicast macs
	 * (configrue / leave the primary mac)
	 */
	rc = qede_set_ucast_rx_mac(edev, QED_FILTER_XCAST_TYPE_REPLACE,
				   edev->primary_mac);
	if (rc)
		goto out;

	/* Check for promiscuous */
	if ((ndev->flags & IFF_PROMISC) ||
	    (uc_count > 15)) { /* @@@TBD resource allocation - 1 */
		accept_flags = QED_FILTER_RX_MODE_TYPE_PROMISC;
	} else {
		/* Add MAC filters according to the unicast secondary macs */
		int i;

		temp = uc_macs;
		for (i = 0; i < uc_count; i++) {
			rc = qede_set_ucast_rx_mac(edev,
						   QED_FILTER_XCAST_TYPE_ADD,
						   temp);
			if (rc)
				goto out;

			temp += ETH_ALEN;
		}

		rc = qede_configure_mcast_filtering(ndev, &accept_flags);
		if (rc)
			goto out;
	}

	/* take care of VLAN mode */
	if (ndev->flags & IFF_PROMISC) {
		qede_config_accept_any_vlan(edev, true);
	} else if (!edev->non_configured_vlans) {
		/* It's possible that accept_any_vlan mode is set due to a
		 * previous setting of IFF_PROMISC. If vlan credits are
		 * sufficient, disable accept_any_vlan.
		 */
		qede_config_accept_any_vlan(edev, false);
	}

	rx_mode.filter.accept_flags = accept_flags;
	edev->ops->filter_config(edev->cdev, &rx_mode);
out:
	kfree(uc_macs);
}
