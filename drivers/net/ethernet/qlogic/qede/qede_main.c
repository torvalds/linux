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
#include <net/vxlan.h>
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

static const char version[] = "QLogic QL4xxx 40G/100G Ethernet Driver qede "
			      DRV_MODULE_VERSION "\n";

MODULE_DESCRIPTION("QLogic 40G/100G Ethernet Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static uint debug;
module_param(debug, uint, 0);
MODULE_PARM_DESC(debug, " Default debug msglevel");

static const struct qed_eth_ops *qed_ops;

#define CHIP_NUM_57980S_40		0x1634
#define CHIP_NUM_57980S_10		0x1635
#define CHIP_NUM_57980S_MF		0x1636
#define CHIP_NUM_57980S_100		0x1644
#define CHIP_NUM_57980S_50		0x1654
#define CHIP_NUM_57980S_25		0x1656

#ifndef PCI_DEVICE_ID_NX2_57980E
#define PCI_DEVICE_ID_57980S_40		CHIP_NUM_57980S_40
#define PCI_DEVICE_ID_57980S_10		CHIP_NUM_57980S_10
#define PCI_DEVICE_ID_57980S_MF		CHIP_NUM_57980S_MF
#define PCI_DEVICE_ID_57980S_100	CHIP_NUM_57980S_100
#define PCI_DEVICE_ID_57980S_50		CHIP_NUM_57980S_50
#define PCI_DEVICE_ID_57980S_25		CHIP_NUM_57980S_25
#endif

static const struct pci_device_id qede_pci_tbl[] = {
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_40), 0 },
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_10), 0 },
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_MF), 0 },
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_100), 0 },
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_50), 0 },
	{ PCI_VDEVICE(QLOGIC, PCI_DEVICE_ID_57980S_25), 0 },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, qede_pci_tbl);

static int qede_probe(struct pci_dev *pdev, const struct pci_device_id *id);

#define TX_TIMEOUT		(5 * HZ)

static void qede_remove(struct pci_dev *pdev);
static int qede_alloc_rx_buffer(struct qede_dev *edev,
				struct qede_rx_queue *rxq);
static void qede_link_update(void *dev, struct qed_link_output *link);

static struct pci_driver qede_pci_driver = {
	.name = "qede",
	.id_table = qede_pci_tbl,
	.probe = qede_probe,
	.remove = qede_remove,
};

static struct qed_eth_cb_ops qede_ll_ops = {
	{
		.link_update = qede_link_update,
	},
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
	u32 qed_ver;

	pr_notice("qede_init: %s\n", version);

	qed_ver = qed_get_protocol_version(QED_PROTOCOL_ETH);
	if (qed_ver !=  QEDE_ETH_INTERFACE_VERSION) {
		pr_notice("Version mismatch [%08x != %08x]\n",
			  qed_ver,
			  QEDE_ETH_INTERFACE_VERSION);
		return -EINVAL;
	}

	qed_ops = qed_get_eth_ops(QEDE_ETH_INTERFACE_VERSION);
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
	pr_notice("qede_cleanup called\n");

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
			    struct qede_tx_queue *txq,
			    int *len)
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
				    int nbd,
				    bool data_split)
{
	u16 idx = txq->sw_tx_prod & NUM_TX_BDS_MAX;
	struct sk_buff *skb = txq->sw_tx_ring[idx].skb;
	struct eth_tx_bd *tx_data_bd;
	int i, split_bd_len = 0;

	/* Return prod to its position before this skb was handled */
	qed_chain_set_prod(&txq->tx_pbl,
			   le16_to_cpu(txq->tx_db.data.bd_prod),
			   first_bd);

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
			   le16_to_cpu(txq->tx_db.data.bd_prod),
			   first_bd);

	/* Free skb */
	dev_kfree_skb_any(skb);
	txq->sw_tx_ring[idx].skb = NULL;
	txq->sw_tx_ring[idx].flags = 0;
}

static u32 qede_xmit_type(struct qede_dev *edev,
			  struct sk_buff *skb,
			  int *ipv6_ext)
{
	u32 rc = XMIT_L4_CSUM;
	__be16 l3_proto;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return XMIT_PLAIN;

	l3_proto = vlan_get_protocol(skb);
	if (l3_proto == htons(ETH_P_IPV6) &&
	    (ipv6_hdr(skb)->nexthdr == NEXTHDR_IPV6))
		*ipv6_ext = 1;

	if (skb_is_gso(skb))
		rc |= XMIT_LSO;

	return rc;
}

static void qede_set_params_for_ipv6_ext(struct sk_buff *skb,
					 struct eth_tx_2nd_bd *second_bd,
					 struct eth_tx_3rd_bd *third_bd)
{
	u8 l4_proto;
	u16 bd2_bits = 0, bd2_bits2 = 0;

	bd2_bits2 |= (1 << ETH_TX_DATA_2ND_BD_IPV6_EXT_SHIFT);

	bd2_bits |= ((((u8 *)skb_transport_header(skb) - skb->data) >> 1) &
		     ETH_TX_DATA_2ND_BD_L4_HDR_START_OFFSET_W_MASK)
		    << ETH_TX_DATA_2ND_BD_L4_HDR_START_OFFSET_W_SHIFT;

	bd2_bits2 |= (ETH_L4_PSEUDO_CSUM_CORRECT_LENGTH <<
		      ETH_TX_DATA_2ND_BD_L4_PSEUDO_CSUM_MODE_SHIFT);

	if (vlan_get_protocol(skb) == htons(ETH_P_IPV6))
		l4_proto = ipv6_hdr(skb)->nexthdr;
	else
		l4_proto = ip_hdr(skb)->protocol;

	if (l4_proto == IPPROTO_UDP)
		bd2_bits2 |= 1 << ETH_TX_DATA_2ND_BD_L4_UDP_SHIFT;

	if (third_bd) {
		third_bd->data.bitfields |=
			((tcp_hdrlen(skb) / 4) &
			 ETH_TX_DATA_3RD_BD_TCP_HDR_LEN_DW_MASK) <<
			ETH_TX_DATA_3RD_BD_TCP_HDR_LEN_DW_SHIFT;
	}

	second_bd->data.bitfields = cpu_to_le16(bd2_bits);
	second_bd->data.bitfields2 = cpu_to_le16(bd2_bits2);
}

static int map_frag_to_bd(struct qede_dev *edev,
			  skb_frag_t *frag,
			  struct eth_tx_bd *bd)
{
	dma_addr_t mapping;

	/* Map skb non-linear frag data for DMA */
	mapping = skb_frag_dma_map(&edev->pdev->dev, frag, 0,
				   skb_frag_size(frag),
				   DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&edev->pdev->dev, mapping))) {
		DP_NOTICE(edev, "Unable to map frag - dropping packet\n");
		return -ENOMEM;
	}

	/* Setup the data pointer of the frag data */
	BD_SET_UNMAP_ADDR_LEN(bd, mapping, skb_frag_size(frag));

	return 0;
}

/* Main transmit function */
static
netdev_tx_t qede_start_xmit(struct sk_buff *skb,
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
	bool data_split;

	/* Get tx-queue context and netdev index */
	txq_index = skb_get_queue_mapping(skb);
	WARN_ON(txq_index >= QEDE_TSS_CNT(edev));
	txq = QEDE_TX_QUEUE(edev, txq_index);
	netdev_txq = netdev_get_tx_queue(ndev, txq_index);

	/* Current code doesn't support SKB linearization, since the max number
	 * of skb frags can be passed in the FW HSI.
	 */
	BUILD_BUG_ON(MAX_SKB_FRAGS > ETH_TX_MAX_BDS_PER_NON_LSO_PACKET);

	WARN_ON(qed_chain_get_elem_left(&txq->tx_pbl) <
			       (MAX_SKB_FRAGS + 1));

	xmit_type = qede_xmit_type(edev, skb, &ipv6_ext);

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

		first_bd->data.bd_flags.bitfields |=
		1 << ETH_TX_1ST_BD_FLAGS_IP_CSUM_SHIFT;
		hlen = skb_transport_header(skb) +
		       tcp_hdrlen(skb) - skb->data;

		/* @@@TBD - if will not be removed need to check */
		third_bd->data.bitfields |=
			(1 << ETH_TX_DATA_3RD_BD_HDR_NBD_SHIFT);

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

	if (unlikely(qed_chain_get_elem_left(&txq->tx_pbl)
		      < (MAX_SKB_FRAGS + 1))) {
		netif_tx_stop_queue(netdev_txq);
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

static int qede_txq_has_work(struct qede_tx_queue *txq)
{
	u16 hw_bd_cons;

	/* Tell compiler that consumer and producer can change */
	barrier();
	hw_bd_cons = le16_to_cpu(*txq->hw_cons_ptr);
	if (qed_chain_get_cons_idx(&txq->tx_pbl) == hw_bd_cons + 1)
		return 0;

	return hw_bd_cons != qed_chain_get_cons_idx(&txq->tx_pbl);
}

static int qede_tx_int(struct qede_dev *edev,
		       struct qede_tx_queue *txq)
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

static bool qede_has_rx_work(struct qede_rx_queue *rxq)
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

/* This function copies the Rx buffer from the CONS position to the PROD
 * position, since we failed to allocate a new Rx buffer.
 */
static void qede_reuse_rx_data(struct qede_rx_queue *rxq)
{
	struct eth_rx_bd *rx_bd_cons = qed_chain_consume(&rxq->rx_bd_ring);
	struct eth_rx_bd *rx_bd_prod = qed_chain_produce(&rxq->rx_bd_ring);
	struct sw_rx_data *sw_rx_data_cons =
		&rxq->sw_rx_ring[rxq->sw_rx_cons & NUM_RX_BDS_MAX];
	struct sw_rx_data *sw_rx_data_prod =
		&rxq->sw_rx_ring[rxq->sw_rx_prod & NUM_RX_BDS_MAX];

	dma_unmap_addr_set(sw_rx_data_prod, mapping,
			   dma_unmap_addr(sw_rx_data_cons, mapping));

	sw_rx_data_prod->data = sw_rx_data_cons->data;
	memcpy(rx_bd_prod, rx_bd_cons, sizeof(struct eth_rx_bd));

	rxq->sw_rx_cons++;
	rxq->sw_rx_prod++;
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
			   __le32 rss_hash,
			   enum pkt_hash_types *rxhash_type)
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
}

static inline void qede_skb_receive(struct qede_dev *edev,
				    struct qede_fastpath *fp,
				    struct sk_buff *skb,
				    u16 vlan_tag)
{
	if (vlan_tag)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       vlan_tag);

	napi_gro_receive(&fp->napi, skb);
}

static u8 qede_check_csum(u16 flag)
{
	u16 csum_flag = 0;
	u8 csum = 0;

	if ((PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_MASK <<
	     PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED_SHIFT) & flag) {
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
		u16 len, pad;
		u32 rx_hash;
		u8 *data;

		/* Get the CQE from the completion ring */
		cqe = (union eth_rx_cqe *)
			qed_chain_consume(&rxq->rx_comp_ring);
		cqe_type = cqe->fast_path_regular.type;

		if (unlikely(cqe_type == ETH_RX_CQE_TYPE_SLOW_PATH)) {
			edev->ops->eth_cqe_completion(
					edev->cdev, fp->rss_id,
					(struct eth_slow_path_rx_cqe *)cqe);
			goto next_cqe;
		}

		/* Get the data from the SW ring */
		sw_rx_index = rxq->sw_rx_cons & NUM_RX_BDS_MAX;
		sw_rx_data = &rxq->sw_rx_ring[sw_rx_index];
		data = sw_rx_data->data;

		fp_cqe = &cqe->fast_path_regular;
		len =  le16_to_cpu(fp_cqe->pkt_len);
		pad = fp_cqe->placement_offset;

		/* For every Rx BD consumed, we allocate a new BD so the BD ring
		 * is always with a fixed size. If allocation fails, we take the
		 * consumed BD and return it to the ring in the PROD position.
		 * The packet that was received on that BD will be dropped (and
		 * not passed to the upper stack).
		 */
		if (likely(qede_alloc_rx_buffer(edev, rxq) == 0)) {
			dma_unmap_single(&edev->pdev->dev,
					 dma_unmap_addr(sw_rx_data, mapping),
					 rxq->rx_buf_size, DMA_FROM_DEVICE);

			/* If this is an error packet then drop it */
			parse_flag =
			le16_to_cpu(cqe->fast_path_regular.pars_flags.flags);
			csum_flag = qede_check_csum(parse_flag);
			if (csum_flag == QEDE_CSUM_ERROR) {
				DP_NOTICE(edev,
					  "CQE in CONS = %u has error, flags = %x, dropping incoming packet\n",
					  sw_comp_cons, parse_flag);
				rxq->rx_hw_errors++;
				kfree(data);
				goto next_rx;
			}

			skb = build_skb(data, 0);

			if (unlikely(!skb)) {
				DP_NOTICE(edev,
					  "Build_skb failed, dropping incoming packet\n");
				kfree(data);
				rxq->rx_alloc_errors++;
				goto next_rx;
			}

			skb_reserve(skb, pad);

		} else {
			DP_NOTICE(edev,
				  "New buffer allocation failed, dropping incoming packet and reusing its buffer\n");
			qede_reuse_rx_data(rxq);
			rxq->rx_alloc_errors++;
			goto next_cqe;
		}

		sw_rx_data->data = NULL;

		skb_put(skb, len);

		skb->protocol = eth_type_trans(skb, edev->ndev);

		rx_hash = qede_get_rxhash(edev, fp_cqe->bitfields,
					  fp_cqe->rss_hash,
					  &rxhash_type);

		skb_set_hash(skb, rx_hash, rxhash_type);

		qede_set_skb_csum(skb, csum_flag);

		skb_record_rx_queue(skb, fp->rss_id);

		qede_skb_receive(edev, fp, skb, le16_to_cpu(fp_cqe->vlan_tag));

		qed_chain_consume(&rxq->rx_bd_ring);

next_rx:
		rxq->sw_rx_cons++;
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

	return rx_pkt;
}

static int qede_poll(struct napi_struct *napi, int budget)
{
	int work_done = 0;
	struct qede_fastpath *fp = container_of(napi, struct qede_fastpath,
						 napi);
	struct qede_dev *edev = fp->edev;

	while (1) {
		u8 tc;

		for (tc = 0; tc < edev->num_tc; tc++)
			if (qede_txq_has_work(&fp->txqs[tc]))
				qede_tx_int(edev, &fp->txqs[tc]);

		if (qede_has_rx_work(fp->rxq)) {
			work_done += qede_rx_int(fp, budget - work_done);

			/* must not complete if we consumed full budget */
			if (work_done >= budget)
				break;
		}

		/* Fall out from the NAPI loop if needed */
		if (!(qede_has_rx_work(fp->rxq) || qede_has_tx_work(fp))) {
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

			if (!(qede_has_rx_work(fp->rxq) ||
			      qede_has_tx_work(fp))) {
				napi_complete(napi);
				/* Update and reenable interrupts */
				qed_sb_ack(fp->sb_info, IGU_INT_ENABLE,
					   1 /*update*/);
				break;
			}
		}
	}

	return work_done;
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

void qede_fill_by_demand_stats(struct qede_dev *edev)
{
	struct qed_eth_stats stats;

	edev->ops->get_vport_stats(edev->cdev, &stats);
	edev->stats.no_buff_discards = stats.no_buff_discards;
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
	edev->stats.rx_127_byte_packets = stats.rx_127_byte_packets;
	edev->stats.rx_255_byte_packets = stats.rx_255_byte_packets;
	edev->stats.rx_511_byte_packets = stats.rx_511_byte_packets;
	edev->stats.rx_1023_byte_packets = stats.rx_1023_byte_packets;
	edev->stats.rx_1518_byte_packets = stats.rx_1518_byte_packets;
	edev->stats.rx_1522_byte_packets = stats.rx_1522_byte_packets;
	edev->stats.rx_2047_byte_packets = stats.rx_2047_byte_packets;
	edev->stats.rx_4095_byte_packets = stats.rx_4095_byte_packets;
	edev->stats.rx_9216_byte_packets = stats.rx_9216_byte_packets;
	edev->stats.rx_16383_byte_packets = stats.rx_16383_byte_packets;
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

static struct rtnl_link_stats64 *qede_get_stats64(
			    struct net_device *dev,
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

static const struct net_device_ops qede_netdev_ops = {
	.ndo_open = qede_open,
	.ndo_stop = qede_close,
	.ndo_start_xmit = qede_start_xmit,
	.ndo_set_rx_mode = qede_set_rx_mode,
	.ndo_set_mac_address = qede_set_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = qede_change_mtu,
	.ndo_get_stats64 = qede_get_stats64,
};

/* -------------------------------------------------------------------------
 * START OF PROBE / REMOVE
 * -------------------------------------------------------------------------
 */

static struct qede_dev *qede_alloc_etherdev(struct qed_dev *cdev,
					    struct pci_dev *pdev,
					    struct qed_dev_eth_info *info,
					    u32 dp_module,
					    u8 dp_level)
{
	struct net_device *ndev;
	struct qede_dev *edev;

	ndev = alloc_etherdev_mqs(sizeof(*edev),
				  info->num_queues,
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

	DP_INFO(edev, "Allocated netdev with 64 tx queues and 64 rx queues\n");

	SET_NETDEV_DEV(ndev, &pdev->dev);

	memset(&edev->stats, 0, sizeof(edev->stats));
	memcpy(&edev->dev_info, info, sizeof(*info));

	edev->num_tc = edev->dev_info.num_tc;

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

	ndev->vlan_features = hw_features | NETIF_F_RXHASH | NETIF_F_RXCSUM |
			      NETIF_F_HIGHDMA;
	ndev->features = hw_features | NETIF_F_RXHASH | NETIF_F_RXCSUM |
			 NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HIGHDMA |
			 NETIF_F_HW_VLAN_CTAG_TX;

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

		for_each_rss(i) {
			fp = &edev->fp_array[i];

			kfree(fp->sb_info);
			kfree(fp->rxq);
			kfree(fp->txqs);
		}
		kfree(edev->fp_array);
	}
	edev->num_rss = 0;
}

static int qede_alloc_fp_array(struct qede_dev *edev)
{
	struct qede_fastpath *fp;
	int i;

	edev->fp_array = kcalloc(QEDE_RSS_CNT(edev),
				 sizeof(*edev->fp_array), GFP_KERNEL);
	if (!edev->fp_array) {
		DP_NOTICE(edev, "fp array allocation failed\n");
		goto err;
	}

	for_each_rss(i) {
		fp = &edev->fp_array[i];

		fp->sb_info = kcalloc(1, sizeof(*fp->sb_info), GFP_KERNEL);
		if (!fp->sb_info) {
			DP_NOTICE(edev, "sb info struct allocation failed\n");
			goto err;
		}

		fp->rxq = kcalloc(1, sizeof(*fp->rxq), GFP_KERNEL);
		if (!fp->rxq) {
			DP_NOTICE(edev, "RXQ struct allocation failed\n");
			goto err;
		}

		fp->txqs = kcalloc(edev->num_tc, sizeof(*fp->txqs), GFP_KERNEL);
		if (!fp->txqs) {
			DP_NOTICE(edev, "TXQ array allocation failed\n");
			goto err;
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
	mutex_lock(&edev->qede_lock);

	if (edev->state == QEDE_STATE_OPEN) {
		if (test_and_clear_bit(QEDE_SP_RX_MODE, &edev->sp_flags))
			qede_config_rx_mode(edev->ndev);
	}

	mutex_unlock(&edev->qede_lock);
}

static void qede_update_pf_params(struct qed_dev *cdev)
{
	struct qed_pf_params pf_params;

	/* 16 rx + 16 tx */
	memset(&pf_params, 0, sizeof(struct qed_pf_params));
	pf_params.eth_pf_params.num_cons = 32;
	qed_ops->common->update_pf_params(cdev, &pf_params);
}

enum qede_probe_mode {
	QEDE_PROBE_NORMAL,
};

static int __qede_probe(struct pci_dev *pdev, u32 dp_module, u8 dp_level,
			enum qede_probe_mode mode)
{
	struct qed_slowpath_params params;
	struct qed_dev_eth_info dev_info;
	struct qede_dev *edev;
	struct qed_dev *cdev;
	int rc;

	if (unlikely(dp_level & QED_LEVEL_INFO))
		pr_notice("Starting qede probe\n");

	cdev = qed_ops->common->probe(pdev, QED_PROTOCOL_ETH,
				      dp_module, dp_level);
	if (!cdev) {
		rc = -ENODEV;
		goto err0;
	}

	qede_update_pf_params(cdev);

	/* Start the Slowpath-process */
	memset(&params, 0, sizeof(struct qed_slowpath_params));
	params.int_mode = QED_INT_MODE_MSIX;
	params.drv_major = QEDE_MAJOR_VERSION;
	params.drv_minor = QEDE_MINOR_VERSION;
	params.drv_rev = QEDE_REVISION_VERSION;
	params.drv_eng = QEDE_ENGINEERING_VERSION;
	strlcpy(params.name, "qede LAN", QED_DRV_VER_STR_SIZE);
	rc = qed_ops->common->slowpath_start(cdev, &params);
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

	qede_init_ndev(edev);

	rc = register_netdev(edev->ndev);
	if (rc) {
		DP_NOTICE(edev, "Cannot register net-device\n");
		goto err3;
	}

	edev->ops->common->set_id(cdev, edev->ndev->name, DRV_MODULE_VERSION);

	edev->ops->register_ops(cdev, &qede_ll_ops, edev);

	INIT_DELAYED_WORK(&edev->sp_task, qede_sp_task);
	mutex_init(&edev->qede_lock);

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
	u32 dp_module = 0;
	u8 dp_level = 0;

	qede_config_debug(debug, &dp_module, &dp_level);

	return __qede_probe(pdev, dp_module, dp_level,
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

	pr_notice("Ending successfully qede_remove\n");
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
	if (edev->req_rss)
		rss_num = edev->req_rss;
	else
		rss_num = netif_get_num_default_rss_queues() *
			  edev->dev_info.common.num_hwfns;

	rss_num = min_t(u16, QEDE_MAX_RSS_CNT(edev), rss_num);

	rc = edev->ops->common->set_fp_int(edev->cdev, rss_num);
	if (rc > 0) {
		/* Managed to request interrupts for our queues */
		edev->num_rss = rc;
		DP_INFO(edev, "Managed %d [of %d] RSS queues\n",
			QEDE_RSS_CNT(edev), rss_num);
		rc = 0;
	}
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
			     struct qed_sb_info *sb_info,
			     u16 sb_id)
{
	struct status_block *sb_virt;
	dma_addr_t sb_phys;
	int rc;

	sb_virt = dma_alloc_coherent(&edev->pdev->dev,
				     sizeof(*sb_virt),
				     &sb_phys, GFP_KERNEL);
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
		u8 *data;

		rx_buf = &rxq->sw_rx_ring[i & NUM_RX_BDS_MAX];
		data = rx_buf->data;

		dma_unmap_single(&edev->pdev->dev,
				 dma_unmap_addr(rx_buf, mapping),
				 rxq->rx_buf_size, DMA_FROM_DEVICE);

		rx_buf->data = NULL;
		kfree(data);
	}
}

static void qede_free_mem_rxq(struct qede_dev *edev,
			      struct qede_rx_queue *rxq)
{
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
	u16 rx_buf_size;
	u8 *data;

	rx_buf_size = rxq->rx_buf_size;

	data = kmalloc(rx_buf_size, GFP_ATOMIC);
	if (unlikely(!data)) {
		DP_NOTICE(edev, "Failed to allocate Rx data\n");
		return -ENOMEM;
	}

	mapping = dma_map_single(&edev->pdev->dev, data,
				 rx_buf_size, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(&edev->pdev->dev, mapping))) {
		kfree(data);
		DP_NOTICE(edev, "Failed to map Rx buffer\n");
		return -ENOMEM;
	}

	sw_rx_data = &rxq->sw_rx_ring[rxq->sw_rx_prod & NUM_RX_BDS_MAX];
	sw_rx_data->data = data;

	dma_unmap_addr_set(sw_rx_data, mapping, mapping);

	/* Advance PROD and get BD pointer */
	rx_bd = (struct eth_rx_bd *)qed_chain_produce(&rxq->rx_bd_ring);
	WARN_ON(!rx_bd);
	rx_bd->addr.hi = cpu_to_le32(upper_32_bits(mapping));
	rx_bd->addr.lo = cpu_to_le32(lower_32_bits(mapping));

	rxq->sw_rx_prod++;

	return 0;
}

/* This function allocates all memory needed per Rx queue */
static int qede_alloc_mem_rxq(struct qede_dev *edev,
			      struct qede_rx_queue *rxq)
{
	int i, rc, size, num_allocated;

	rxq->num_rx_buffers = edev->q_num_rx_buffers;

	rxq->rx_buf_size = NET_IP_ALIGN +
			   ETH_OVERHEAD +
			   edev->ndev->mtu +
			   QEDE_FW_RX_ALIGN_END;

	/* Allocate the parallel driver ring for Rx buffers */
	size = sizeof(*rxq->sw_rx_ring) * NUM_RX_BDS_MAX;
	rxq->sw_rx_ring = kzalloc(size, GFP_KERNEL);
	if (!rxq->sw_rx_ring) {
		DP_ERR(edev, "Rx buffers ring allocation failed\n");
		goto err;
	}

	/* Allocate FW Rx ring  */
	rc = edev->ops->common->chain_alloc(edev->cdev,
					    QED_CHAIN_USE_TO_CONSUME_PRODUCE,
					    QED_CHAIN_MODE_NEXT_PTR,
					    NUM_RX_BDS_MAX,
					    sizeof(struct eth_rx_bd),
					    &rxq->rx_bd_ring);

	if (rc)
		goto err;

	/* Allocate FW completion ring */
	rc = edev->ops->common->chain_alloc(edev->cdev,
					    QED_CHAIN_USE_TO_CONSUME,
					    QED_CHAIN_MODE_PBL,
					    NUM_RX_BDS_MAX,
					    sizeof(union eth_rx_cqe),
					    &rxq->rx_comp_ring);
	if (rc)
		goto err;

	/* Allocate buffers for the Rx ring */
	for (i = 0; i < rxq->num_rx_buffers; i++) {
		rc = qede_alloc_rx_buffer(edev, rxq);
		if (rc)
			break;
	}
	num_allocated = i;
	if (!num_allocated) {
		DP_ERR(edev, "Rx buffers allocation failed\n");
		goto err;
	} else if (num_allocated < rxq->num_rx_buffers) {
		DP_NOTICE(edev,
			  "Allocated less buffers than desired (%d allocated)\n",
			  num_allocated);
	}

	return 0;

err:
	qede_free_mem_rxq(edev, rxq);
	return -ENOMEM;
}

static void qede_free_mem_txq(struct qede_dev *edev,
			      struct qede_tx_queue *txq)
{
	/* Free the parallel SW ring */
	kfree(txq->sw_tx_ring);

	/* Free the real RQ ring used by FW */
	edev->ops->common->chain_free(edev->cdev, &txq->tx_pbl);
}

/* This function allocates all memory needed per Tx queue */
static int qede_alloc_mem_txq(struct qede_dev *edev,
			      struct qede_tx_queue *txq)
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
					    NUM_TX_BDS_MAX,
					    sizeof(*p_virt),
					    &txq->tx_pbl);
	if (rc)
		goto err;

	return 0;

err:
	qede_free_mem_txq(edev, txq);
	return -ENOMEM;
}

/* This function frees all memory of a single fp */
static void qede_free_mem_fp(struct qede_dev *edev,
			     struct qede_fastpath *fp)
{
	int tc;

	qede_free_mem_sb(edev, fp->sb_info);

	qede_free_mem_rxq(edev, fp->rxq);

	for (tc = 0; tc < edev->num_tc; tc++)
		qede_free_mem_txq(edev, &fp->txqs[tc]);
}

/* This function allocates all memory needed for a single fp (i.e. an entity
 * which contains status block, one rx queue and multiple per-TC tx queues.
 */
static int qede_alloc_mem_fp(struct qede_dev *edev,
			     struct qede_fastpath *fp)
{
	int rc, tc;

	rc = qede_alloc_mem_sb(edev, fp->sb_info, fp->rss_id);
	if (rc)
		goto err;

	rc = qede_alloc_mem_rxq(edev, fp->rxq);
	if (rc)
		goto err;

	for (tc = 0; tc < edev->num_tc; tc++) {
		rc = qede_alloc_mem_txq(edev, &fp->txqs[tc]);
		if (rc)
			goto err;
	}

	return 0;

err:
	qede_free_mem_fp(edev, fp);
	return -ENOMEM;
}

static void qede_free_mem_load(struct qede_dev *edev)
{
	int i;

	for_each_rss(i) {
		struct qede_fastpath *fp = &edev->fp_array[i];

		qede_free_mem_fp(edev, fp);
	}
}

/* This function allocates all qede memory at NIC load. */
static int qede_alloc_mem_load(struct qede_dev *edev)
{
	int rc = 0, rss_id;

	for (rss_id = 0; rss_id < QEDE_RSS_CNT(edev); rss_id++) {
		struct qede_fastpath *fp = &edev->fp_array[rss_id];

		rc = qede_alloc_mem_fp(edev, fp);
		if (rc)
			break;
	}

	if (rss_id != QEDE_RSS_CNT(edev)) {
		/* Failed allocating memory for all the queues */
		if (!rss_id) {
			DP_ERR(edev,
			       "Failed to allocate memory for the leading queue\n");
			rc = -ENOMEM;
		} else {
			DP_NOTICE(edev,
				  "Failed to allocate memory for all of RSS queues\n Desired: %d queues, allocated: %d queues\n",
				  QEDE_RSS_CNT(edev), rss_id);
		}
		edev->num_rss = rss_id;
	}

	return 0;
}

/* This function inits fp content and resets the SB, RXQ and TXQ structures */
static void qede_init_fp(struct qede_dev *edev)
{
	int rss_id, txq_index, tc;
	struct qede_fastpath *fp;

	for_each_rss(rss_id) {
		fp = &edev->fp_array[rss_id];

		fp->edev = edev;
		fp->rss_id = rss_id;

		memset((void *)&fp->napi, 0, sizeof(fp->napi));

		memset((void *)fp->sb_info, 0, sizeof(*fp->sb_info));

		memset((void *)fp->rxq, 0, sizeof(*fp->rxq));
		fp->rxq->rxq_id = rss_id;

		memset((void *)fp->txqs, 0, (edev->num_tc * sizeof(*fp->txqs)));
		for (tc = 0; tc < edev->num_tc; tc++) {
			txq_index = tc * QEDE_RSS_CNT(edev) + rss_id;
			fp->txqs[tc].index = txq_index;
		}

		snprintf(fp->name, sizeof(fp->name), "%s-fp-%d",
			 edev->ndev->name, rss_id);
	}
}

static int qede_set_real_num_queues(struct qede_dev *edev)
{
	int rc = 0;

	rc = netif_set_real_num_tx_queues(edev->ndev, QEDE_TSS_CNT(edev));
	if (rc) {
		DP_NOTICE(edev, "Failed to set real number of Tx queues\n");
		return rc;
	}
	rc = netif_set_real_num_rx_queues(edev->ndev, QEDE_RSS_CNT(edev));
	if (rc) {
		DP_NOTICE(edev, "Failed to set real number of Rx queues\n");
		return rc;
	}

	return 0;
}

static void qede_napi_disable_remove(struct qede_dev *edev)
{
	int i;

	for_each_rss(i) {
		napi_disable(&edev->fp_array[i].napi);

		netif_napi_del(&edev->fp_array[i].napi);
	}
}

static void qede_napi_add_enable(struct qede_dev *edev)
{
	int i;

	/* Add NAPI objects */
	for_each_rss(i) {
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
	if (QEDE_RSS_CNT(edev) > edev->int_info.msix_cnt) {
		DP_ERR(edev,
		       "Interrupt mismatch: %d RSS queues > %d MSI-x vectors\n",
		       QEDE_RSS_CNT(edev), edev->int_info.msix_cnt);
		return -EINVAL;
	}

	for (i = 0; i < QEDE_RSS_CNT(edev); i++) {
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
		for (i = 0; i < QEDE_RSS_CNT(edev); i++)
			ops->simd_handler_config(edev->cdev,
						 &edev->fp_array[i], i,
						 qede_simd_fp_handler);
		edev->int_info.used_cnt = QEDE_RSS_CNT(edev);
	}
	return 0;
}

static int qede_drain_txq(struct qede_dev *edev,
			  struct qede_tx_queue *txq,
			  bool allow_drain)
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
	for_each_rss(i) {
		struct qede_fastpath *fp = &edev->fp_array[i];

		for (tc = 0; tc < edev->num_tc; tc++) {
			struct qede_tx_queue *txq = &fp->txqs[tc];

			rc = qede_drain_txq(edev, txq, true);
			if (rc)
				return rc;
		}
	}

	/* Stop all Queues in reverse order*/
	for (i = QEDE_RSS_CNT(edev) - 1; i >= 0; i--) {
		struct qed_stop_rxq_params rx_params;

		/* Stop the Tx Queue(s)*/
		for (tc = 0; tc < edev->num_tc; tc++) {
			struct qed_stop_txq_params tx_params;

			tx_params.rss_id = i;
			tx_params.tx_queue_id = tc * QEDE_RSS_CNT(edev) + i;
			rc = edev->ops->q_tx_stop(cdev, &tx_params);
			if (rc) {
				DP_ERR(edev, "Failed to stop TXQ #%d\n",
				       tx_params.tx_queue_id);
				return rc;
			}
		}

		/* Stop the Rx Queue*/
		memset(&rx_params, 0, sizeof(rx_params));
		rx_params.rss_id = i;
		rx_params.rx_queue_id = i;

		rc = edev->ops->q_rx_stop(cdev, &rx_params);
		if (rc) {
			DP_ERR(edev, "Failed to stop RXQ #%d\n", i);
			return rc;
		}
	}

	/* Stop the vport */
	rc = edev->ops->vport_stop(cdev, 0);
	if (rc)
		DP_ERR(edev, "Failed to stop VPORT\n");

	return rc;
}

static int qede_start_queues(struct qede_dev *edev)
{
	int rc, tc, i;
	int vport_id = 0, drop_ttl0_flg = 1, vlan_removal_en = 1;
	struct qed_dev *cdev = edev->cdev;
	struct qed_update_vport_rss_params *rss_params = &edev->rss_params;
	struct qed_update_vport_params vport_update_params;
	struct qed_queue_start_common_params q_params;

	if (!edev->num_rss) {
		DP_ERR(edev,
		       "Cannot update V-VPORT as active as there are no Rx queues\n");
		return -EINVAL;
	}

	rc = edev->ops->vport_start(cdev, vport_id,
				    edev->ndev->mtu,
				    drop_ttl0_flg,
				    vlan_removal_en);

	if (rc) {
		DP_ERR(edev, "Start V-PORT failed %d\n", rc);
		return rc;
	}

	DP_VERBOSE(edev, NETIF_MSG_IFUP,
		   "Start vport ramrod passed, vport_id = %d, MTU = %d, vlan_removal_en = %d\n",
		   vport_id, edev->ndev->mtu + 0xe, vlan_removal_en);

	for_each_rss(i) {
		struct qede_fastpath *fp = &edev->fp_array[i];
		dma_addr_t phys_table = fp->rxq->rx_comp_ring.pbl.p_phys_table;

		memset(&q_params, 0, sizeof(q_params));
		q_params.rss_id = i;
		q_params.queue_id = i;
		q_params.vport_id = 0;
		q_params.sb = fp->sb_info->igu_sb_id;
		q_params.sb_idx = RX_PI;

		rc = edev->ops->q_rx_start(cdev, &q_params,
					   fp->rxq->rx_buf_size,
					   fp->rxq->rx_bd_ring.p_phys_addr,
					   phys_table,
					   fp->rxq->rx_comp_ring.page_cnt,
					   &fp->rxq->hw_rxq_prod_addr);
		if (rc) {
			DP_ERR(edev, "Start RXQ #%d failed %d\n", i, rc);
			return rc;
		}

		fp->rxq->hw_cons_ptr = &fp->sb_info->sb_virt->pi_array[RX_PI];

		qede_update_rx_prod(edev, fp->rxq);

		for (tc = 0; tc < edev->num_tc; tc++) {
			struct qede_tx_queue *txq = &fp->txqs[tc];
			int txq_index = tc * QEDE_RSS_CNT(edev) + i;

			memset(&q_params, 0, sizeof(q_params));
			q_params.rss_id = i;
			q_params.queue_id = txq_index;
			q_params.vport_id = 0;
			q_params.sb = fp->sb_info->igu_sb_id;
			q_params.sb_idx = TX_PI(tc);

			rc = edev->ops->q_tx_start(cdev, &q_params,
						   txq->tx_pbl.pbl.p_phys_table,
						   txq->tx_pbl.page_cnt,
						   &txq->doorbell_addr);
			if (rc) {
				DP_ERR(edev, "Start TXQ #%d failed %d\n",
				       txq_index, rc);
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
	vport_update_params.vport_id = vport_id;
	vport_update_params.update_vport_active_flg = 1;
	vport_update_params.vport_active_flg = 1;

	/* Fill struct with RSS params */
	if (QEDE_RSS_CNT(edev) > 1) {
		vport_update_params.update_rss_flg = 1;
		for (i = 0; i < 128; i++)
			rss_params->rss_ind_table[i] =
			ethtool_rxfh_indir_default(i, QEDE_RSS_CNT(edev));
		netdev_rss_key_fill(rss_params->rss_key,
				    sizeof(rss_params->rss_key));
	} else {
		memset(rss_params, 0, sizeof(*rss_params));
	}
	memcpy(&vport_update_params.rss_params, rss_params,
	       sizeof(*rss_params));

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
		QEDE_RSS_CNT(edev), edev->num_tc);

	rc = qede_set_real_num_queues(edev);
	if (rc)
		goto err2;

	qede_napi_add_enable(edev);
	DP_INFO(edev, "Napi added and enabled\n");

	rc = qede_setup_irqs(edev);
	if (rc)
		goto err3;
	DP_INFO(edev, "Setup IRQs succeeded\n");

	rc = qede_start_queues(edev);
	if (rc)
		goto err4;
	DP_INFO(edev, "Start VPORT, RXQ and TXQ succeeded\n");

	/* Add primary mac and set Rx filters */
	ether_addr_copy(edev->primary_mac, edev->ndev->dev_addr);

	mutex_lock(&edev->qede_lock);
	edev->state = QEDE_STATE_OPEN;
	mutex_unlock(&edev->qede_lock);

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
	edev->num_rss = 0;
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

	qede_load(edev, QEDE_LOAD_NORMAL);

	mutex_lock(&edev->qede_lock);
	qede_config_rx_mode(edev->ndev);
	mutex_unlock(&edev->qede_lock);
}

/* called with rtnl_lock */
static int qede_open(struct net_device *ndev)
{
	struct qede_dev *edev = netdev_priv(ndev);

	netif_carrier_off(ndev);

	edev->ops->common->set_power_state(edev->cdev, PCI_D0);

	return qede_load(edev, QEDE_LOAD_NORMAL);
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
		DP_NOTICE(edev, "Link is up\n");
		netif_tx_start_all_queues(edev->ndev);
		netif_carrier_on(edev->ndev);
	} else {
		DP_NOTICE(edev, "Link is down\n");
		netif_tx_disable(edev->ndev);
		netif_carrier_off(edev->ndev);
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

	rx_mode.filter.accept_flags = accept_flags;
	edev->ops->filter_config(edev->cdev, &rx_mode);
out:
	kfree(uc_macs);
}
