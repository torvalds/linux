/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/ethtool.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <net/ip6_checksum.h>

#include "cq_enet_desc.h"
#include "vnic_dev.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "enic_res.h"
#include "enic.h"

#define ENIC_NOTIFY_TIMER_PERIOD	(2 * HZ)

/* Supported devices */
static struct pci_device_id enic_id_table[] = {
	{ PCI_VDEVICE(CISCO, 0x0043) },
	{ 0, }	/* end of table */
};

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Scott Feldman <scofeldm@cisco.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, enic_id_table);

struct enic_stat {
	char name[ETH_GSTRING_LEN];
	unsigned int offset;
};

#define ENIC_TX_STAT(stat)	\
	{ .name = #stat, .offset = offsetof(struct vnic_tx_stats, stat) / 8 }
#define ENIC_RX_STAT(stat)	\
	{ .name = #stat, .offset = offsetof(struct vnic_rx_stats, stat) / 8 }

static const struct enic_stat enic_tx_stats[] = {
	ENIC_TX_STAT(tx_frames_ok),
	ENIC_TX_STAT(tx_unicast_frames_ok),
	ENIC_TX_STAT(tx_multicast_frames_ok),
	ENIC_TX_STAT(tx_broadcast_frames_ok),
	ENIC_TX_STAT(tx_bytes_ok),
	ENIC_TX_STAT(tx_unicast_bytes_ok),
	ENIC_TX_STAT(tx_multicast_bytes_ok),
	ENIC_TX_STAT(tx_broadcast_bytes_ok),
	ENIC_TX_STAT(tx_drops),
	ENIC_TX_STAT(tx_errors),
	ENIC_TX_STAT(tx_tso),
};

static const struct enic_stat enic_rx_stats[] = {
	ENIC_RX_STAT(rx_frames_ok),
	ENIC_RX_STAT(rx_frames_total),
	ENIC_RX_STAT(rx_unicast_frames_ok),
	ENIC_RX_STAT(rx_multicast_frames_ok),
	ENIC_RX_STAT(rx_broadcast_frames_ok),
	ENIC_RX_STAT(rx_bytes_ok),
	ENIC_RX_STAT(rx_unicast_bytes_ok),
	ENIC_RX_STAT(rx_multicast_bytes_ok),
	ENIC_RX_STAT(rx_broadcast_bytes_ok),
	ENIC_RX_STAT(rx_drop),
	ENIC_RX_STAT(rx_no_bufs),
	ENIC_RX_STAT(rx_errors),
	ENIC_RX_STAT(rx_rss),
	ENIC_RX_STAT(rx_crc_errors),
	ENIC_RX_STAT(rx_frames_64),
	ENIC_RX_STAT(rx_frames_127),
	ENIC_RX_STAT(rx_frames_255),
	ENIC_RX_STAT(rx_frames_511),
	ENIC_RX_STAT(rx_frames_1023),
	ENIC_RX_STAT(rx_frames_1518),
	ENIC_RX_STAT(rx_frames_to_max),
};

static const unsigned int enic_n_tx_stats = ARRAY_SIZE(enic_tx_stats);
static const unsigned int enic_n_rx_stats = ARRAY_SIZE(enic_rx_stats);

static int enic_get_settings(struct net_device *netdev,
	struct ethtool_cmd *ecmd)
{
	struct enic *enic = netdev_priv(netdev);

	ecmd->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
	ecmd->advertising = (ADVERTISED_10000baseT_Full | ADVERTISED_FIBRE);
	ecmd->port = PORT_FIBRE;
	ecmd->transceiver = XCVR_EXTERNAL;

	if (netif_carrier_ok(netdev)) {
		ecmd->speed = vnic_dev_port_speed(enic->vdev);
		ecmd->duplex = DUPLEX_FULL;
	} else {
		ecmd->speed = -1;
		ecmd->duplex = -1;
	}

	ecmd->autoneg = AUTONEG_DISABLE;

	return 0;
}

static void enic_get_drvinfo(struct net_device *netdev,
	struct ethtool_drvinfo *drvinfo)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_devcmd_fw_info *fw_info;

	spin_lock(&enic->devcmd_lock);
	vnic_dev_fw_info(enic->vdev, &fw_info);
	spin_unlock(&enic->devcmd_lock);

	strncpy(drvinfo->driver, DRV_NAME, sizeof(drvinfo->driver));
	strncpy(drvinfo->version, DRV_VERSION, sizeof(drvinfo->version));
	strncpy(drvinfo->fw_version, fw_info->fw_version,
		sizeof(drvinfo->fw_version));
	strncpy(drvinfo->bus_info, pci_name(enic->pdev),
		sizeof(drvinfo->bus_info));
}

static void enic_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	unsigned int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < enic_n_tx_stats; i++) {
			memcpy(data, enic_tx_stats[i].name, ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		for (i = 0; i < enic_n_rx_stats; i++) {
			memcpy(data, enic_rx_stats[i].name, ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int enic_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return enic_n_tx_stats + enic_n_rx_stats;
	default:
		return -EOPNOTSUPP;
	}
}

static void enic_get_ethtool_stats(struct net_device *netdev,
	struct ethtool_stats *stats, u64 *data)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_stats *vstats;
	unsigned int i;

	spin_lock(&enic->devcmd_lock);
	vnic_dev_stats_dump(enic->vdev, &vstats);
	spin_unlock(&enic->devcmd_lock);

	for (i = 0; i < enic_n_tx_stats; i++)
		*(data++) = ((u64 *)&vstats->tx)[enic_tx_stats[i].offset];
	for (i = 0; i < enic_n_rx_stats; i++)
		*(data++) = ((u64 *)&vstats->rx)[enic_rx_stats[i].offset];
}

static u32 enic_get_rx_csum(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	return enic->csum_rx_enabled;
}

static int enic_set_rx_csum(struct net_device *netdev, u32 data)
{
	struct enic *enic = netdev_priv(netdev);

	if (data && !ENIC_SETTING(enic, RXCSUM))
		return -EINVAL;

	enic->csum_rx_enabled = !!data;

	return 0;
}

static int enic_set_tx_csum(struct net_device *netdev, u32 data)
{
	struct enic *enic = netdev_priv(netdev);

	if (data && !ENIC_SETTING(enic, TXCSUM))
		return -EINVAL;

	if (data)
		netdev->features |= NETIF_F_HW_CSUM;
	else
		netdev->features &= ~NETIF_F_HW_CSUM;

	return 0;
}

static int enic_set_tso(struct net_device *netdev, u32 data)
{
	struct enic *enic = netdev_priv(netdev);

	if (data && !ENIC_SETTING(enic, TSO))
		return -EINVAL;

	if (data)
		netdev->features |=
			NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_TSO_ECN;
	else
		netdev->features &=
			~(NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_TSO_ECN);

	return 0;
}

static u32 enic_get_msglevel(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	return enic->msg_enable;
}

static void enic_set_msglevel(struct net_device *netdev, u32 value)
{
	struct enic *enic = netdev_priv(netdev);
	enic->msg_enable = value;
}

static struct ethtool_ops enic_ethtool_ops = {
	.get_settings = enic_get_settings,
	.get_drvinfo = enic_get_drvinfo,
	.get_msglevel = enic_get_msglevel,
	.set_msglevel = enic_set_msglevel,
	.get_link = ethtool_op_get_link,
	.get_strings = enic_get_strings,
	.get_sset_count = enic_get_sset_count,
	.get_ethtool_stats = enic_get_ethtool_stats,
	.get_rx_csum = enic_get_rx_csum,
	.set_rx_csum = enic_set_rx_csum,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = enic_set_tx_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
	.get_tso = ethtool_op_get_tso,
	.set_tso = enic_set_tso,
	.get_flags = ethtool_op_get_flags,
	.set_flags = ethtool_op_set_flags,
};

static void enic_free_wq_buf(struct vnic_wq *wq, struct vnic_wq_buf *buf)
{
	struct enic *enic = vnic_dev_priv(wq->vdev);

	if (buf->sop)
		pci_unmap_single(enic->pdev, buf->dma_addr,
			buf->len, PCI_DMA_TODEVICE);
	else
		pci_unmap_page(enic->pdev, buf->dma_addr,
			buf->len, PCI_DMA_TODEVICE);

	if (buf->os_buf)
		dev_kfree_skb_any(buf->os_buf);
}

static void enic_wq_free_buf(struct vnic_wq *wq,
	struct cq_desc *cq_desc, struct vnic_wq_buf *buf, void *opaque)
{
	enic_free_wq_buf(wq, buf);
}

static int enic_wq_service(struct vnic_dev *vdev, struct cq_desc *cq_desc,
	u8 type, u16 q_number, u16 completed_index, void *opaque)
{
	struct enic *enic = vnic_dev_priv(vdev);

	spin_lock(&enic->wq_lock[q_number]);

	vnic_wq_service(&enic->wq[q_number], cq_desc,
		completed_index, enic_wq_free_buf,
		opaque);

	if (netif_queue_stopped(enic->netdev) &&
	    vnic_wq_desc_avail(&enic->wq[q_number]) >= MAX_SKB_FRAGS + 1)
		netif_wake_queue(enic->netdev);

	spin_unlock(&enic->wq_lock[q_number]);

	return 0;
}

static void enic_log_q_error(struct enic *enic)
{
	unsigned int i;
	u32 error_status;

	for (i = 0; i < enic->wq_count; i++) {
		error_status = vnic_wq_error_status(&enic->wq[i]);
		if (error_status)
			printk(KERN_ERR PFX "%s: WQ[%d] error_status %d\n",
				enic->netdev->name, i, error_status);
	}

	for (i = 0; i < enic->rq_count; i++) {
		error_status = vnic_rq_error_status(&enic->rq[i]);
		if (error_status)
			printk(KERN_ERR PFX "%s: RQ[%d] error_status %d\n",
				enic->netdev->name, i, error_status);
	}
}

static void enic_link_check(struct enic *enic)
{
	int link_status = vnic_dev_link_status(enic->vdev);
	int carrier_ok = netif_carrier_ok(enic->netdev);

	if (link_status && !carrier_ok) {
		printk(KERN_INFO PFX "%s: Link UP\n", enic->netdev->name);
		netif_carrier_on(enic->netdev);
	} else if (!link_status && carrier_ok) {
		printk(KERN_INFO PFX "%s: Link DOWN\n", enic->netdev->name);
		netif_carrier_off(enic->netdev);
	}
}

static void enic_mtu_check(struct enic *enic)
{
	u32 mtu = vnic_dev_mtu(enic->vdev);

	if (mtu != enic->port_mtu) {
		if (mtu < enic->netdev->mtu)
			printk(KERN_WARNING PFX
				"%s: interface MTU (%d) set higher "
				"than switch port MTU (%d)\n",
				enic->netdev->name, enic->netdev->mtu, mtu);
		enic->port_mtu = mtu;
	}
}

static void enic_msglvl_check(struct enic *enic)
{
	u32 msg_enable = vnic_dev_msg_lvl(enic->vdev);

	if (msg_enable != enic->msg_enable) {
		printk(KERN_INFO PFX "%s: msg lvl changed from 0x%x to 0x%x\n",
			enic->netdev->name, enic->msg_enable, msg_enable);
		enic->msg_enable = msg_enable;
	}
}

static void enic_notify_check(struct enic *enic)
{
	enic_msglvl_check(enic);
	enic_mtu_check(enic);
	enic_link_check(enic);
}

#define ENIC_TEST_INTR(pba, i) (pba & (1 << i))

static irqreturn_t enic_isr_legacy(int irq, void *data)
{
	struct net_device *netdev = data;
	struct enic *enic = netdev_priv(netdev);
	u32 pba;

	vnic_intr_mask(&enic->intr[ENIC_INTX_WQ_RQ]);

	pba = vnic_intr_legacy_pba(enic->legacy_pba);
	if (!pba) {
		vnic_intr_unmask(&enic->intr[ENIC_INTX_WQ_RQ]);
		return IRQ_NONE;	/* not our interrupt */
	}

	if (ENIC_TEST_INTR(pba, ENIC_INTX_NOTIFY)) {
		vnic_intr_return_all_credits(&enic->intr[ENIC_INTX_NOTIFY]);
		enic_notify_check(enic);
	}

	if (ENIC_TEST_INTR(pba, ENIC_INTX_ERR)) {
		vnic_intr_return_all_credits(&enic->intr[ENIC_INTX_ERR]);
		enic_log_q_error(enic);
		/* schedule recovery from WQ/RQ error */
		schedule_work(&enic->reset);
		return IRQ_HANDLED;
	}

	if (ENIC_TEST_INTR(pba, ENIC_INTX_WQ_RQ)) {
		if (napi_schedule_prep(&enic->napi))
			__napi_schedule(&enic->napi);
	} else {
		vnic_intr_unmask(&enic->intr[ENIC_INTX_WQ_RQ]);
	}

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msi(int irq, void *data)
{
	struct enic *enic = data;

	/* With MSI, there is no sharing of interrupts, so this is
	 * our interrupt and there is no need to ack it.  The device
	 * is not providing per-vector masking, so the OS will not
	 * write to PCI config space to mask/unmask the interrupt.
	 * We're using mask_on_assertion for MSI, so the device
	 * automatically masks the interrupt when the interrupt is
	 * generated.  Later, when exiting polling, the interrupt
	 * will be unmasked (see enic_poll).
	 *
	 * Also, the device uses the same PCIe Traffic Class (TC)
	 * for Memory Write data and MSI, so there are no ordering
	 * issues; the MSI will always arrive at the Root Complex
	 * _after_ corresponding Memory Writes (i.e. descriptor
	 * writes).
	 */

	napi_schedule(&enic->napi);

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msix_rq(int irq, void *data)
{
	struct enic *enic = data;

	/* schedule NAPI polling for RQ cleanup */
	napi_schedule(&enic->napi);

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msix_wq(int irq, void *data)
{
	struct enic *enic = data;
	unsigned int wq_work_to_do = -1; /* no limit */
	unsigned int wq_work_done;

	wq_work_done = vnic_cq_service(&enic->cq[ENIC_CQ_WQ],
		wq_work_to_do, enic_wq_service, NULL);

	vnic_intr_return_credits(&enic->intr[ENIC_MSIX_WQ],
		wq_work_done,
		1 /* unmask intr */,
		1 /* reset intr timer */);

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msix_err(int irq, void *data)
{
	struct enic *enic = data;

	vnic_intr_return_all_credits(&enic->intr[ENIC_MSIX_ERR]);

	enic_log_q_error(enic);

	/* schedule recovery from WQ/RQ error */
	schedule_work(&enic->reset);

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msix_notify(int irq, void *data)
{
	struct enic *enic = data;

	vnic_intr_return_all_credits(&enic->intr[ENIC_MSIX_NOTIFY]);
	enic_notify_check(enic);

	return IRQ_HANDLED;
}

static inline void enic_queue_wq_skb_cont(struct enic *enic,
	struct vnic_wq *wq, struct sk_buff *skb,
	unsigned int len_left)
{
	skb_frag_t *frag;

	/* Queue additional data fragments */
	for (frag = skb_shinfo(skb)->frags; len_left; frag++) {
		len_left -= frag->size;
		enic_queue_wq_desc_cont(wq, skb,
			pci_map_page(enic->pdev, frag->page,
				frag->page_offset, frag->size,
				PCI_DMA_TODEVICE),
			frag->size,
			(len_left == 0));	/* EOP? */
	}
}

static inline void enic_queue_wq_skb_vlan(struct enic *enic,
	struct vnic_wq *wq, struct sk_buff *skb,
	int vlan_tag_insert, unsigned int vlan_tag)
{
	unsigned int head_len = skb_headlen(skb);
	unsigned int len_left = skb->len - head_len;
	int eop = (len_left == 0);

	/* Queue the main skb fragment */
	enic_queue_wq_desc(wq, skb,
		pci_map_single(enic->pdev, skb->data,
			head_len, PCI_DMA_TODEVICE),
		head_len,
		vlan_tag_insert, vlan_tag,
		eop);

	if (!eop)
		enic_queue_wq_skb_cont(enic, wq, skb, len_left);
}

static inline void enic_queue_wq_skb_csum_l4(struct enic *enic,
	struct vnic_wq *wq, struct sk_buff *skb,
	int vlan_tag_insert, unsigned int vlan_tag)
{
	unsigned int head_len = skb_headlen(skb);
	unsigned int len_left = skb->len - head_len;
	unsigned int hdr_len = skb_transport_offset(skb);
	unsigned int csum_offset = hdr_len + skb->csum_offset;
	int eop = (len_left == 0);

	/* Queue the main skb fragment */
	enic_queue_wq_desc_csum_l4(wq, skb,
		pci_map_single(enic->pdev, skb->data,
			head_len, PCI_DMA_TODEVICE),
		head_len,
		csum_offset,
		hdr_len,
		vlan_tag_insert, vlan_tag,
		eop);

	if (!eop)
		enic_queue_wq_skb_cont(enic, wq, skb, len_left);
}

static inline void enic_queue_wq_skb_tso(struct enic *enic,
	struct vnic_wq *wq, struct sk_buff *skb, unsigned int mss,
	int vlan_tag_insert, unsigned int vlan_tag)
{
	unsigned int head_len = skb_headlen(skb);
	unsigned int len_left = skb->len - head_len;
	unsigned int hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
	int eop = (len_left == 0);

	/* Preload TCP csum field with IP pseudo hdr calculated
	 * with IP length set to zero.  HW will later add in length
	 * to each TCP segment resulting from the TSO.
	 */

	if (skb->protocol == cpu_to_be16(ETH_P_IP)) {
		ip_hdr(skb)->check = 0;
		tcp_hdr(skb)->check = ~csum_tcpudp_magic(ip_hdr(skb)->saddr,
			ip_hdr(skb)->daddr, 0, IPPROTO_TCP, 0);
	} else if (skb->protocol == cpu_to_be16(ETH_P_IPV6)) {
		tcp_hdr(skb)->check = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
			&ipv6_hdr(skb)->daddr, 0, IPPROTO_TCP, 0);
	}

	/* Queue the main skb fragment */
	enic_queue_wq_desc_tso(wq, skb,
		pci_map_single(enic->pdev, skb->data,
			head_len, PCI_DMA_TODEVICE),
		head_len,
		mss, hdr_len,
		vlan_tag_insert, vlan_tag,
		eop);

	if (!eop)
		enic_queue_wq_skb_cont(enic, wq, skb, len_left);
}

static inline void enic_queue_wq_skb(struct enic *enic,
	struct vnic_wq *wq, struct sk_buff *skb)
{
	unsigned int mss = skb_shinfo(skb)->gso_size;
	unsigned int vlan_tag = 0;
	int vlan_tag_insert = 0;

	if (enic->vlan_group && vlan_tx_tag_present(skb)) {
		/* VLAN tag from trunking driver */
		vlan_tag_insert = 1;
		vlan_tag = vlan_tx_tag_get(skb);
	}

	if (mss)
		enic_queue_wq_skb_tso(enic, wq, skb, mss,
			vlan_tag_insert, vlan_tag);
	else if	(skb->ip_summed == CHECKSUM_PARTIAL)
		enic_queue_wq_skb_csum_l4(enic, wq, skb,
			vlan_tag_insert, vlan_tag);
	else
		enic_queue_wq_skb_vlan(enic, wq, skb,
			vlan_tag_insert, vlan_tag);
}

/* netif_tx_lock held, process context with BHs disabled, or BH */
static int enic_hard_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_wq *wq = &enic->wq[0];
	unsigned long flags;

	if (skb->len <= 0) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	/* Non-TSO sends must fit within ENIC_NON_TSO_MAX_DESC descs,
	 * which is very likely.  In the off chance it's going to take
	 * more than * ENIC_NON_TSO_MAX_DESC, linearize the skb.
	 */

	if (skb_shinfo(skb)->gso_size == 0 &&
	    skb_shinfo(skb)->nr_frags + 1 > ENIC_NON_TSO_MAX_DESC &&
	    skb_linearize(skb)) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	spin_lock_irqsave(&enic->wq_lock[0], flags);

	if (vnic_wq_desc_avail(wq) < skb_shinfo(skb)->nr_frags + 1) {
		netif_stop_queue(netdev);
		/* This is a hard error, log it */
		printk(KERN_ERR PFX "%s: BUG! Tx ring full when "
			"queue awake!\n", netdev->name);
		spin_unlock_irqrestore(&enic->wq_lock[0], flags);
		return NETDEV_TX_BUSY;
	}

	enic_queue_wq_skb(enic, wq, skb);

	if (vnic_wq_desc_avail(wq) < MAX_SKB_FRAGS + 1)
		netif_stop_queue(netdev);

	spin_unlock_irqrestore(&enic->wq_lock[0], flags);

	return NETDEV_TX_OK;
}

/* dev_base_lock rwlock held, nominally process context */
static struct net_device_stats *enic_get_stats(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	struct net_device_stats *net_stats = &netdev->stats;
	struct vnic_stats *stats;

	spin_lock(&enic->devcmd_lock);
	vnic_dev_stats_dump(enic->vdev, &stats);
	spin_unlock(&enic->devcmd_lock);

	net_stats->tx_packets = stats->tx.tx_frames_ok;
	net_stats->tx_bytes = stats->tx.tx_bytes_ok;
	net_stats->tx_errors = stats->tx.tx_errors;
	net_stats->tx_dropped = stats->tx.tx_drops;

	net_stats->rx_packets = stats->rx.rx_frames_ok;
	net_stats->rx_bytes = stats->rx.rx_bytes_ok;
	net_stats->rx_errors = stats->rx.rx_errors;
	net_stats->multicast = stats->rx.rx_multicast_frames_ok;
	net_stats->rx_crc_errors = enic->rq_bad_fcs;
	net_stats->rx_dropped = stats->rx.rx_no_bufs;

	return net_stats;
}

static void enic_reset_mcaddrs(struct enic *enic)
{
	enic->mc_count = 0;
}

static int enic_set_mac_addr(struct net_device *netdev, char *addr)
{
	if (!is_valid_ether_addr(addr))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr, netdev->addr_len);

	return 0;
}

/* netif_tx_lock held, BHs disabled */
static void enic_set_multicast_list(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	struct dev_mc_list *list = netdev->mc_list;
	int directed = 1;
	int multicast = (netdev->flags & IFF_MULTICAST) ? 1 : 0;
	int broadcast = (netdev->flags & IFF_BROADCAST) ? 1 : 0;
	int promisc = (netdev->flags & IFF_PROMISC) ? 1 : 0;
	int allmulti = (netdev->flags & IFF_ALLMULTI) ||
	    (netdev->mc_count > ENIC_MULTICAST_PERFECT_FILTERS);
	u8 mc_addr[ENIC_MULTICAST_PERFECT_FILTERS][ETH_ALEN];
	unsigned int mc_count = netdev->mc_count;
	unsigned int i, j;

	if (mc_count > ENIC_MULTICAST_PERFECT_FILTERS)
		mc_count = ENIC_MULTICAST_PERFECT_FILTERS;

	spin_lock(&enic->devcmd_lock);

	vnic_dev_packet_filter(enic->vdev, directed,
		multicast, broadcast, promisc, allmulti);

	/* Is there an easier way?  Trying to minimize to
	 * calls to add/del multicast addrs.  We keep the
	 * addrs from the last call in enic->mc_addr and
	 * look for changes to add/del.
	 */

	for (i = 0; list && i < mc_count; i++) {
		memcpy(mc_addr[i], list->dmi_addr, ETH_ALEN);
		list = list->next;
	}

	for (i = 0; i < enic->mc_count; i++) {
		for (j = 0; j < mc_count; j++)
			if (compare_ether_addr(enic->mc_addr[i],
				mc_addr[j]) == 0)
				break;
		if (j == mc_count)
			enic_del_multicast_addr(enic, enic->mc_addr[i]);
	}

	for (i = 0; i < mc_count; i++) {
		for (j = 0; j < enic->mc_count; j++)
			if (compare_ether_addr(mc_addr[i],
				enic->mc_addr[j]) == 0)
				break;
		if (j == enic->mc_count)
			enic_add_multicast_addr(enic, mc_addr[i]);
	}

	/* Save the list to compare against next time
	 */

	for (i = 0; i < mc_count; i++)
		memcpy(enic->mc_addr[i], mc_addr[i], ETH_ALEN);

	enic->mc_count = mc_count;

	spin_unlock(&enic->devcmd_lock);
}

/* rtnl lock is held */
static void enic_vlan_rx_register(struct net_device *netdev,
	struct vlan_group *vlan_group)
{
	struct enic *enic = netdev_priv(netdev);
	enic->vlan_group = vlan_group;
}

/* rtnl lock is held */
static void enic_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
{
	struct enic *enic = netdev_priv(netdev);

	spin_lock(&enic->devcmd_lock);
	enic_add_vlan(enic, vid);
	spin_unlock(&enic->devcmd_lock);
}

/* rtnl lock is held */
static void enic_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
{
	struct enic *enic = netdev_priv(netdev);

	spin_lock(&enic->devcmd_lock);
	enic_del_vlan(enic, vid);
	spin_unlock(&enic->devcmd_lock);
}

/* netif_tx_lock held, BHs disabled */
static void enic_tx_timeout(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	schedule_work(&enic->reset);
}

static void enic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf)
{
	struct enic *enic = vnic_dev_priv(rq->vdev);

	if (!buf->os_buf)
		return;

	pci_unmap_single(enic->pdev, buf->dma_addr,
		buf->len, PCI_DMA_FROMDEVICE);
	dev_kfree_skb_any(buf->os_buf);
}

static inline struct sk_buff *enic_rq_alloc_skb(unsigned int size)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(size + NET_IP_ALIGN);

	if (skb)
		skb_reserve(skb, NET_IP_ALIGN);

	return skb;
}

static int enic_rq_alloc_buf(struct vnic_rq *rq)
{
	struct enic *enic = vnic_dev_priv(rq->vdev);
	struct sk_buff *skb;
	unsigned int len = enic->netdev->mtu + ETH_HLEN;
	unsigned int os_buf_index = 0;
	dma_addr_t dma_addr;

	skb = enic_rq_alloc_skb(len);
	if (!skb)
		return -ENOMEM;

	dma_addr = pci_map_single(enic->pdev, skb->data,
		len, PCI_DMA_FROMDEVICE);

	enic_queue_rq_desc(rq, skb, os_buf_index,
		dma_addr, len);

	return 0;
}

static int enic_get_skb_header(struct sk_buff *skb, void **iphdr,
	void **tcph, u64 *hdr_flags, void *priv)
{
	struct cq_enet_rq_desc *cq_desc = priv;
	unsigned int ip_len;
	struct iphdr *iph;

	u8 type, color, eop, sop, ingress_port, vlan_stripped;
	u8 fcoe, fcoe_sof, fcoe_fc_crc_ok, fcoe_enc_error, fcoe_eof;
	u8 tcp_udp_csum_ok, udp, tcp, ipv4_csum_ok;
	u8 ipv6, ipv4, ipv4_fragment, fcs_ok, rss_type, csum_not_calc;
	u8 packet_error;
	u16 q_number, completed_index, bytes_written, vlan, checksum;
	u32 rss_hash;

	cq_enet_rq_desc_dec(cq_desc,
		&type, &color, &q_number, &completed_index,
		&ingress_port, &fcoe, &eop, &sop, &rss_type,
		&csum_not_calc, &rss_hash, &bytes_written,
		&packet_error, &vlan_stripped, &vlan, &checksum,
		&fcoe_sof, &fcoe_fc_crc_ok, &fcoe_enc_error,
		&fcoe_eof, &tcp_udp_csum_ok, &udp, &tcp,
		&ipv4_csum_ok, &ipv6, &ipv4, &ipv4_fragment,
		&fcs_ok);

	if (!(ipv4 && tcp && !ipv4_fragment))
		return -1;

	skb_reset_network_header(skb);
	iph = ip_hdr(skb);

	ip_len = ip_hdrlen(skb);
	skb_set_transport_header(skb, ip_len);

	/* check if ip header and tcp header are complete */
	if (ntohs(iph->tot_len) < ip_len + tcp_hdrlen(skb))
		return -1;

	*hdr_flags = LRO_IPV4 | LRO_TCP;
	*tcph = tcp_hdr(skb);
	*iphdr = iph;

	return 0;
}

static void enic_rq_indicate_buf(struct vnic_rq *rq,
	struct cq_desc *cq_desc, struct vnic_rq_buf *buf,
	int skipped, void *opaque)
{
	struct enic *enic = vnic_dev_priv(rq->vdev);
	struct net_device *netdev = enic->netdev;
	struct sk_buff *skb;

	u8 type, color, eop, sop, ingress_port, vlan_stripped;
	u8 fcoe, fcoe_sof, fcoe_fc_crc_ok, fcoe_enc_error, fcoe_eof;
	u8 tcp_udp_csum_ok, udp, tcp, ipv4_csum_ok;
	u8 ipv6, ipv4, ipv4_fragment, fcs_ok, rss_type, csum_not_calc;
	u8 packet_error;
	u16 q_number, completed_index, bytes_written, vlan, checksum;
	u32 rss_hash;

	if (skipped)
		return;

	skb = buf->os_buf;
	prefetch(skb->data - NET_IP_ALIGN);
	pci_unmap_single(enic->pdev, buf->dma_addr,
		buf->len, PCI_DMA_FROMDEVICE);

	cq_enet_rq_desc_dec((struct cq_enet_rq_desc *)cq_desc,
		&type, &color, &q_number, &completed_index,
		&ingress_port, &fcoe, &eop, &sop, &rss_type,
		&csum_not_calc, &rss_hash, &bytes_written,
		&packet_error, &vlan_stripped, &vlan, &checksum,
		&fcoe_sof, &fcoe_fc_crc_ok, &fcoe_enc_error,
		&fcoe_eof, &tcp_udp_csum_ok, &udp, &tcp,
		&ipv4_csum_ok, &ipv6, &ipv4, &ipv4_fragment,
		&fcs_ok);

	if (packet_error) {

		if (bytes_written > 0 && !fcs_ok)
			enic->rq_bad_fcs++;

		dev_kfree_skb_any(skb);

		return;
	}

	if (eop && bytes_written > 0) {

		/* Good receive
		 */

		skb_put(skb, bytes_written);
		skb->protocol = eth_type_trans(skb, netdev);

		if (enic->csum_rx_enabled && !csum_not_calc) {
			skb->csum = htons(checksum);
			skb->ip_summed = CHECKSUM_COMPLETE;
		}

		skb->dev = netdev;

		if (enic->vlan_group && vlan_stripped) {

			if ((netdev->features & NETIF_F_LRO) && ipv4)
				lro_vlan_hwaccel_receive_skb(&enic->lro_mgr,
					skb, enic->vlan_group,
					vlan, cq_desc);
			else
				vlan_hwaccel_receive_skb(skb,
					enic->vlan_group, vlan);

		} else {

			if ((netdev->features & NETIF_F_LRO) && ipv4)
				lro_receive_skb(&enic->lro_mgr, skb, cq_desc);
			else
				netif_receive_skb(skb);

		}

	} else {

		/* Buffer overflow
		 */

		dev_kfree_skb_any(skb);
	}
}

static int enic_rq_service(struct vnic_dev *vdev, struct cq_desc *cq_desc,
	u8 type, u16 q_number, u16 completed_index, void *opaque)
{
	struct enic *enic = vnic_dev_priv(vdev);

	vnic_rq_service(&enic->rq[q_number], cq_desc,
		completed_index, VNIC_RQ_RETURN_DESC,
		enic_rq_indicate_buf, opaque);

	return 0;
}

static void enic_rq_drop_buf(struct vnic_rq *rq,
	struct cq_desc *cq_desc, struct vnic_rq_buf *buf,
	int skipped, void *opaque)
{
	struct enic *enic = vnic_dev_priv(rq->vdev);
	struct sk_buff *skb = buf->os_buf;

	if (skipped)
		return;

	pci_unmap_single(enic->pdev, buf->dma_addr,
		buf->len, PCI_DMA_FROMDEVICE);

	dev_kfree_skb_any(skb);
}

static int enic_rq_service_drop(struct vnic_dev *vdev, struct cq_desc *cq_desc,
	u8 type, u16 q_number, u16 completed_index, void *opaque)
{
	struct enic *enic = vnic_dev_priv(vdev);

	vnic_rq_service(&enic->rq[q_number], cq_desc,
		completed_index, VNIC_RQ_RETURN_DESC,
		enic_rq_drop_buf, opaque);

	return 0;
}

static int enic_poll(struct napi_struct *napi, int budget)
{
	struct enic *enic = container_of(napi, struct enic, napi);
	struct net_device *netdev = enic->netdev;
	unsigned int rq_work_to_do = budget;
	unsigned int wq_work_to_do = -1; /* no limit */
	unsigned int  work_done, rq_work_done, wq_work_done;

	/* Service RQ (first) and WQ
	 */

	rq_work_done = vnic_cq_service(&enic->cq[ENIC_CQ_RQ],
		rq_work_to_do, enic_rq_service, NULL);

	wq_work_done = vnic_cq_service(&enic->cq[ENIC_CQ_WQ],
		wq_work_to_do, enic_wq_service, NULL);

	/* Accumulate intr event credits for this polling
	 * cycle.  An intr event is the completion of a
	 * a WQ or RQ packet.
	 */

	work_done = rq_work_done + wq_work_done;

	if (work_done > 0)
		vnic_intr_return_credits(&enic->intr[ENIC_INTX_WQ_RQ],
			work_done,
			0 /* don't unmask intr */,
			0 /* don't reset intr timer */);

	if (rq_work_done > 0) {

		/* Replenish RQ
		 */

		vnic_rq_fill(&enic->rq[0], enic_rq_alloc_buf);

	} else {

		/* If no work done, flush all LROs and exit polling
		 */

		if (netdev->features & NETIF_F_LRO)
			lro_flush_all(&enic->lro_mgr);

		napi_complete(napi);
		vnic_intr_unmask(&enic->intr[ENIC_INTX_WQ_RQ]);
	}

	return rq_work_done;
}

static int enic_poll_msix(struct napi_struct *napi, int budget)
{
	struct enic *enic = container_of(napi, struct enic, napi);
	struct net_device *netdev = enic->netdev;
	unsigned int work_to_do = budget;
	unsigned int work_done;

	/* Service RQ
	 */

	work_done = vnic_cq_service(&enic->cq[ENIC_CQ_RQ],
		work_to_do, enic_rq_service, NULL);

	if (work_done > 0) {

		/* Replenish RQ
		 */

		vnic_rq_fill(&enic->rq[0], enic_rq_alloc_buf);

		/* Return intr event credits for this polling
		 * cycle.  An intr event is the completion of a
		 * RQ packet.
		 */

		vnic_intr_return_credits(&enic->intr[ENIC_MSIX_RQ],
			work_done,
			0 /* don't unmask intr */,
			0 /* don't reset intr timer */);
	} else {

		/* If no work done, flush all LROs and exit polling
		 */

		if (netdev->features & NETIF_F_LRO)
			lro_flush_all(&enic->lro_mgr);

		napi_complete(napi);
		vnic_intr_unmask(&enic->intr[ENIC_MSIX_RQ]);
	}

	return work_done;
}

static void enic_notify_timer(unsigned long data)
{
	struct enic *enic = (struct enic *)data;

	enic_notify_check(enic);

	mod_timer(&enic->notify_timer,
		round_jiffies(jiffies + ENIC_NOTIFY_TIMER_PERIOD));
}

static void enic_free_intr(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
	unsigned int i;

	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		free_irq(enic->pdev->irq, netdev);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		free_irq(enic->pdev->irq, enic);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < ARRAY_SIZE(enic->msix); i++)
			if (enic->msix[i].requested)
				free_irq(enic->msix_entry[i].vector,
					enic->msix[i].devid);
		break;
	default:
		break;
	}
}

static int enic_request_intr(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
	unsigned int i;
	int err = 0;

	switch (vnic_dev_get_intr_mode(enic->vdev)) {

	case VNIC_DEV_INTR_MODE_INTX:

		err = request_irq(enic->pdev->irq, enic_isr_legacy,
			IRQF_SHARED, netdev->name, netdev);
		break;

	case VNIC_DEV_INTR_MODE_MSI:

		err = request_irq(enic->pdev->irq, enic_isr_msi,
			0, netdev->name, enic);
		break;

	case VNIC_DEV_INTR_MODE_MSIX:

		sprintf(enic->msix[ENIC_MSIX_RQ].devname,
			"%.11s-rx-0", netdev->name);
		enic->msix[ENIC_MSIX_RQ].isr = enic_isr_msix_rq;
		enic->msix[ENIC_MSIX_RQ].devid = enic;

		sprintf(enic->msix[ENIC_MSIX_WQ].devname,
			"%.11s-tx-0", netdev->name);
		enic->msix[ENIC_MSIX_WQ].isr = enic_isr_msix_wq;
		enic->msix[ENIC_MSIX_WQ].devid = enic;

		sprintf(enic->msix[ENIC_MSIX_ERR].devname,
			"%.11s-err", netdev->name);
		enic->msix[ENIC_MSIX_ERR].isr = enic_isr_msix_err;
		enic->msix[ENIC_MSIX_ERR].devid = enic;

		sprintf(enic->msix[ENIC_MSIX_NOTIFY].devname,
			"%.11s-notify", netdev->name);
		enic->msix[ENIC_MSIX_NOTIFY].isr = enic_isr_msix_notify;
		enic->msix[ENIC_MSIX_NOTIFY].devid = enic;

		for (i = 0; i < ARRAY_SIZE(enic->msix); i++) {
			err = request_irq(enic->msix_entry[i].vector,
				enic->msix[i].isr, 0,
				enic->msix[i].devname,
				enic->msix[i].devid);
			if (err) {
				enic_free_intr(enic);
				break;
			}
			enic->msix[i].requested = 1;
		}

		break;

	default:
		break;
	}

	return err;
}

static int enic_notify_set(struct enic *enic)
{
	int err;

	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		err = vnic_dev_notify_set(enic->vdev, ENIC_INTX_NOTIFY);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		err = vnic_dev_notify_set(enic->vdev, ENIC_MSIX_NOTIFY);
		break;
	default:
		err = vnic_dev_notify_set(enic->vdev, -1 /* no intr */);
		break;
	}

	return err;
}

static void enic_notify_timer_start(struct enic *enic)
{
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSI:
		mod_timer(&enic->notify_timer, jiffies);
		break;
	default:
		/* Using intr for notification for INTx/MSI-X */
		break;
	};
}

/* rtnl lock is held, process context */
static int enic_open(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	unsigned int i;
	int err;

	err = enic_request_intr(enic);
	if (err) {
		printk(KERN_ERR PFX "%s: Unable to request irq.\n",
			netdev->name);
		return err;
	}

	err = enic_notify_set(enic);
	if (err) {
		printk(KERN_ERR PFX
			"%s: Failed to alloc notify buffer, aborting.\n",
			netdev->name);
		goto err_out_free_intr;
	}

	for (i = 0; i < enic->rq_count; i++) {
		err = vnic_rq_fill(&enic->rq[i], enic_rq_alloc_buf);
		if (err) {
			printk(KERN_ERR PFX
				"%s: Unable to alloc receive buffers.\n",
				netdev->name);
			goto err_out_notify_unset;
		}
	}

	for (i = 0; i < enic->wq_count; i++)
		vnic_wq_enable(&enic->wq[i]);
	for (i = 0; i < enic->rq_count; i++)
		vnic_rq_enable(&enic->rq[i]);

	enic_add_station_addr(enic);
	enic_set_multicast_list(netdev);

	netif_wake_queue(netdev);
	napi_enable(&enic->napi);
	vnic_dev_enable(enic->vdev);

	for (i = 0; i < enic->intr_count; i++)
		vnic_intr_unmask(&enic->intr[i]);

	enic_notify_timer_start(enic);

	return 0;

err_out_notify_unset:
	vnic_dev_notify_unset(enic->vdev);
err_out_free_intr:
	enic_free_intr(enic);

	return err;
}

/* rtnl lock is held, process context */
static int enic_stop(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	unsigned int i;
	int err;

	del_timer_sync(&enic->notify_timer);

	vnic_dev_disable(enic->vdev);
	napi_disable(&enic->napi);
	netif_stop_queue(netdev);

	for (i = 0; i < enic->intr_count; i++)
		vnic_intr_mask(&enic->intr[i]);

	for (i = 0; i < enic->wq_count; i++) {
		err = vnic_wq_disable(&enic->wq[i]);
		if (err)
			return err;
	}
	for (i = 0; i < enic->rq_count; i++) {
		err = vnic_rq_disable(&enic->rq[i]);
		if (err)
			return err;
	}

	vnic_dev_notify_unset(enic->vdev);
	enic_free_intr(enic);

	(void)vnic_cq_service(&enic->cq[ENIC_CQ_RQ],
		-1, enic_rq_service_drop, NULL);
	(void)vnic_cq_service(&enic->cq[ENIC_CQ_WQ],
		-1, enic_wq_service, NULL);

	for (i = 0; i < enic->wq_count; i++)
		vnic_wq_clean(&enic->wq[i], enic_free_wq_buf);
	for (i = 0; i < enic->rq_count; i++)
		vnic_rq_clean(&enic->rq[i], enic_free_rq_buf);
	for (i = 0; i < enic->cq_count; i++)
		vnic_cq_clean(&enic->cq[i]);
	for (i = 0; i < enic->intr_count; i++)
		vnic_intr_clean(&enic->intr[i]);

	return 0;
}

static int enic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct enic *enic = netdev_priv(netdev);
	int running = netif_running(netdev);

	if (new_mtu < ENIC_MIN_MTU || new_mtu > ENIC_MAX_MTU)
		return -EINVAL;

	if (running)
		enic_stop(netdev);

	netdev->mtu = new_mtu;

	if (netdev->mtu > enic->port_mtu)
		printk(KERN_WARNING PFX
			"%s: interface MTU (%d) set higher "
			"than port MTU (%d)\n",
			netdev->name, netdev->mtu, enic->port_mtu);

	if (running)
		enic_open(netdev);

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void enic_poll_controller(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_dev *vdev = enic->vdev;

	switch (vnic_dev_get_intr_mode(vdev)) {
	case VNIC_DEV_INTR_MODE_MSIX:
		enic_isr_msix_rq(enic->pdev->irq, enic);
		enic_isr_msix_wq(enic->pdev->irq, enic);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		enic_isr_msi(enic->pdev->irq, enic);
		break;
	case VNIC_DEV_INTR_MODE_INTX:
		enic_isr_legacy(enic->pdev->irq, netdev);
		break;
	default:
		break;
	}
}
#endif

static int enic_dev_wait(struct vnic_dev *vdev,
	int (*start)(struct vnic_dev *, int),
	int (*finished)(struct vnic_dev *, int *),
	int arg)
{
	unsigned long time;
	int done;
	int err;

	BUG_ON(in_interrupt());

	err = start(vdev, arg);
	if (err)
		return err;

	/* Wait for func to complete...2 seconds max
	 */

	time = jiffies + (HZ * 2);
	do {

		err = finished(vdev, &done);
		if (err)
			return err;

		if (done)
			return 0;

		schedule_timeout_uninterruptible(HZ / 10);

	} while (time_after(time, jiffies));

	return -ETIMEDOUT;
}

static int enic_dev_open(struct enic *enic)
{
	int err;

	err = enic_dev_wait(enic->vdev, vnic_dev_open,
		vnic_dev_open_done, 0);
	if (err)
		printk(KERN_ERR PFX
			"vNIC device open failed, err %d.\n", err);

	return err;
}

static int enic_dev_soft_reset(struct enic *enic)
{
	int err;

	err = enic_dev_wait(enic->vdev, vnic_dev_soft_reset,
		vnic_dev_soft_reset_done, 0);
	if (err)
		printk(KERN_ERR PFX
			"vNIC soft reset failed, err %d.\n", err);

	return err;
}

static int enic_set_niccfg(struct enic *enic)
{
	const u8 rss_default_cpu = 0;
	const u8 rss_hash_type = 0;
	const u8 rss_hash_bits = 0;
	const u8 rss_base_cpu = 0;
	const u8 rss_enable = 0;
	const u8 tso_ipid_split_en = 0;
	const u8 ig_vlan_strip_en = 1;

	/* Enable VLAN tag stripping.  RSS not enabled (yet).
	*/

	return enic_set_nic_cfg(enic,
		rss_default_cpu, rss_hash_type,
		rss_hash_bits, rss_base_cpu,
		rss_enable, tso_ipid_split_en,
		ig_vlan_strip_en);
}

static void enic_reset(struct work_struct *work)
{
	struct enic *enic = container_of(work, struct enic, reset);

	if (!netif_running(enic->netdev))
		return;

	rtnl_lock();

	spin_lock(&enic->devcmd_lock);
	vnic_dev_hang_notify(enic->vdev);
	spin_unlock(&enic->devcmd_lock);

	enic_stop(enic->netdev);
	enic_dev_soft_reset(enic);
	vnic_dev_init(enic->vdev, 0);
	enic_reset_mcaddrs(enic);
	enic_init_vnic_resources(enic);
	enic_set_niccfg(enic);
	enic_open(enic->netdev);

	rtnl_unlock();
}

static int enic_set_intr_mode(struct enic *enic)
{
	unsigned int n = ARRAY_SIZE(enic->rq);
	unsigned int m = ARRAY_SIZE(enic->wq);
	unsigned int i;

	/* Set interrupt mode (INTx, MSI, MSI-X) depending
	 * system capabilities.
	 *
	 * Try MSI-X first
	 *
	 * We need n RQs, m WQs, n+m CQs, and n+m+2 INTRs
	 * (the second to last INTR is used for WQ/RQ errors)
	 * (the last INTR is used for notifications)
	 */

	BUG_ON(ARRAY_SIZE(enic->msix_entry) < n + m + 2);
	for (i = 0; i < n + m + 2; i++)
		enic->msix_entry[i].entry = i;

	if (enic->config.intr_mode < 1 &&
	    enic->rq_count >= n &&
	    enic->wq_count >= m &&
	    enic->cq_count >= n + m &&
	    enic->intr_count >= n + m + 2 &&
	    !pci_enable_msix(enic->pdev, enic->msix_entry, n + m + 2)) {

		enic->rq_count = n;
		enic->wq_count = m;
		enic->cq_count = n + m;
		enic->intr_count = n + m + 2;

		vnic_dev_set_intr_mode(enic->vdev, VNIC_DEV_INTR_MODE_MSIX);

		return 0;
	}

	/* Next try MSI
	 *
	 * We need 1 RQ, 1 WQ, 2 CQs, and 1 INTR
	 */

	if (enic->config.intr_mode < 2 &&
	    enic->rq_count >= 1 &&
	    enic->wq_count >= 1 &&
	    enic->cq_count >= 2 &&
	    enic->intr_count >= 1 &&
	    !pci_enable_msi(enic->pdev)) {

		enic->rq_count = 1;
		enic->wq_count = 1;
		enic->cq_count = 2;
		enic->intr_count = 1;

		vnic_dev_set_intr_mode(enic->vdev, VNIC_DEV_INTR_MODE_MSI);

		return 0;
	}

	/* Next try INTx
	 *
	 * We need 1 RQ, 1 WQ, 2 CQs, and 3 INTRs
	 * (the first INTR is used for WQ/RQ)
	 * (the second INTR is used for WQ/RQ errors)
	 * (the last INTR is used for notifications)
	 */

	if (enic->config.intr_mode < 3 &&
	    enic->rq_count >= 1 &&
	    enic->wq_count >= 1 &&
	    enic->cq_count >= 2 &&
	    enic->intr_count >= 3) {

		enic->rq_count = 1;
		enic->wq_count = 1;
		enic->cq_count = 2;
		enic->intr_count = 3;

		vnic_dev_set_intr_mode(enic->vdev, VNIC_DEV_INTR_MODE_INTX);

		return 0;
	}

	vnic_dev_set_intr_mode(enic->vdev, VNIC_DEV_INTR_MODE_UNKNOWN);

	return -EINVAL;
}

static void enic_clear_intr_mode(struct enic *enic)
{
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_MSIX:
		pci_disable_msix(enic->pdev);
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		pci_disable_msi(enic->pdev);
		break;
	default:
		break;
	}

	vnic_dev_set_intr_mode(enic->vdev, VNIC_DEV_INTR_MODE_UNKNOWN);
}

static void enic_iounmap(struct enic *enic)
{
	if (enic->bar0.vaddr)
		iounmap(enic->bar0.vaddr);
}

static const struct net_device_ops enic_netdev_ops = {
	.ndo_open		= enic_open,
	.ndo_stop		= enic_stop,
	.ndo_start_xmit		= enic_hard_start_xmit,
	.ndo_get_stats		= enic_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_set_multicast_list	= enic_set_multicast_list,
	.ndo_change_mtu		= enic_change_mtu,
	.ndo_vlan_rx_register	= enic_vlan_rx_register,
	.ndo_vlan_rx_add_vid	= enic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= enic_vlan_rx_kill_vid,
	.ndo_tx_timeout		= enic_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= enic_poll_controller,
#endif
};

static int __devinit enic_probe(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct enic *enic;
	int using_dac = 0;
	unsigned int i;
	int err;

	/* Allocate net device structure and initialize.  Private
	 * instance data is initialized to zero.
	 */

	netdev = alloc_etherdev(sizeof(struct enic));
	if (!netdev) {
		printk(KERN_ERR PFX "Etherdev alloc failed, aborting.\n");
		return -ENOMEM;
	}

	pci_set_drvdata(pdev, netdev);

	SET_NETDEV_DEV(netdev, &pdev->dev);

	enic = netdev_priv(netdev);
	enic->netdev = netdev;
	enic->pdev = pdev;

	/* Setup PCI resources
	 */

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR PFX
			"Cannot enable PCI device, aborting.\n");
		goto err_out_free_netdev;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		printk(KERN_ERR PFX
			"Cannot request PCI regions, aborting.\n");
		goto err_out_disable_device;
	}

	pci_set_master(pdev);

	/* Query PCI controller on system for DMA addressing
	 * limitation for the device.  Try 40-bit first, and
	 * fail to 32-bit.
	 */

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(40));
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			printk(KERN_ERR PFX
				"No usable DMA configuration, aborting.\n");
			goto err_out_release_regions;
		}
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			printk(KERN_ERR PFX
				"Unable to obtain 32-bit DMA "
				"for consistent allocations, aborting.\n");
			goto err_out_release_regions;
		}
	} else {
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(40));
		if (err) {
			printk(KERN_ERR PFX
				"Unable to obtain 40-bit DMA "
				"for consistent allocations, aborting.\n");
			goto err_out_release_regions;
		}
		using_dac = 1;
	}

	/* Map vNIC resources from BAR0
	 */

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		printk(KERN_ERR PFX
			"BAR0 not memory-map'able, aborting.\n");
		err = -ENODEV;
		goto err_out_release_regions;
	}

	enic->bar0.vaddr = pci_iomap(pdev, 0, enic->bar0.len);
	enic->bar0.bus_addr = pci_resource_start(pdev, 0);
	enic->bar0.len = pci_resource_len(pdev, 0);

	if (!enic->bar0.vaddr) {
		printk(KERN_ERR PFX
			"Cannot memory-map BAR0 res hdr, aborting.\n");
		err = -ENODEV;
		goto err_out_release_regions;
	}

	/* Register vNIC device
	 */

	enic->vdev = vnic_dev_register(NULL, enic, pdev, &enic->bar0);
	if (!enic->vdev) {
		printk(KERN_ERR PFX
			"vNIC registration failed, aborting.\n");
		err = -ENODEV;
		goto err_out_iounmap;
	}

	/* Issue device open to get device in known state
	 */

	err = enic_dev_open(enic);
	if (err) {
		printk(KERN_ERR PFX
			"vNIC dev open failed, aborting.\n");
		goto err_out_vnic_unregister;
	}

	/* Issue device init to initialize the vnic-to-switch link.
	 * We'll start with carrier off and wait for link UP
	 * notification later to turn on carrier.  We don't need
	 * to wait here for the vnic-to-switch link initialization
	 * to complete; link UP notification is the indication that
	 * the process is complete.
	 */

	netif_carrier_off(netdev);

	err = vnic_dev_init(enic->vdev, 0);
	if (err) {
		printk(KERN_ERR PFX
			"vNIC dev init failed, aborting.\n");
		goto err_out_dev_close;
	}

	/* Get vNIC configuration
	 */

	err = enic_get_vnic_config(enic);
	if (err) {
		printk(KERN_ERR PFX
			"Get vNIC configuration failed, aborting.\n");
		goto err_out_dev_close;
	}

	/* Get available resource counts
	 */

	enic_get_res_counts(enic);

	/* Set interrupt mode based on resource counts and system
	 * capabilities
	 */

	err = enic_set_intr_mode(enic);
	if (err) {
		printk(KERN_ERR PFX
			"Failed to set intr mode, aborting.\n");
		goto err_out_dev_close;
	}

	/* Allocate and configure vNIC resources
	 */

	err = enic_alloc_vnic_resources(enic);
	if (err) {
		printk(KERN_ERR PFX
			"Failed to alloc vNIC resources, aborting.\n");
		goto err_out_free_vnic_resources;
	}

	enic_init_vnic_resources(enic);

	err = enic_set_niccfg(enic);
	if (err) {
		printk(KERN_ERR PFX
			"Failed to config nic, aborting.\n");
		goto err_out_free_vnic_resources;
	}

	/* Setup notification timer, HW reset task, and locks
	 */

	init_timer(&enic->notify_timer);
	enic->notify_timer.function = enic_notify_timer;
	enic->notify_timer.data = (unsigned long)enic;

	INIT_WORK(&enic->reset, enic_reset);

	for (i = 0; i < enic->wq_count; i++)
		spin_lock_init(&enic->wq_lock[i]);

	spin_lock_init(&enic->devcmd_lock);

	/* Register net device
	 */

	enic->port_mtu = enic->config.mtu;
	(void)enic_change_mtu(netdev, enic->port_mtu);

	err = enic_set_mac_addr(netdev, enic->mac_addr);
	if (err) {
		printk(KERN_ERR PFX
			"Invalid MAC address, aborting.\n");
		goto err_out_free_vnic_resources;
	}

	netdev->netdev_ops = &enic_netdev_ops;
	netdev->watchdog_timeo = 2 * HZ;
	netdev->ethtool_ops = &enic_ethtool_ops;

	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	default:
		netif_napi_add(netdev, &enic->napi, enic_poll, 64);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		netif_napi_add(netdev, &enic->napi, enic_poll_msix, 64);
		break;
	}

	netdev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
	if (ENIC_SETTING(enic, TXCSUM))
		netdev->features |= NETIF_F_SG | NETIF_F_HW_CSUM;
	if (ENIC_SETTING(enic, TSO))
		netdev->features |= NETIF_F_TSO |
			NETIF_F_TSO6 | NETIF_F_TSO_ECN;
	if (ENIC_SETTING(enic, LRO))
		netdev->features |= NETIF_F_LRO;
	if (using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	enic->csum_rx_enabled = ENIC_SETTING(enic, RXCSUM);

	enic->lro_mgr.max_aggr = ENIC_LRO_MAX_AGGR;
	enic->lro_mgr.max_desc = ENIC_LRO_MAX_DESC;
	enic->lro_mgr.lro_arr = enic->lro_desc;
	enic->lro_mgr.get_skb_header = enic_get_skb_header;
	enic->lro_mgr.features	= LRO_F_NAPI | LRO_F_EXTRACT_VLAN_ID;
	enic->lro_mgr.dev = netdev;
	enic->lro_mgr.ip_summed = CHECKSUM_COMPLETE;
	enic->lro_mgr.ip_summed_aggr = CHECKSUM_UNNECESSARY;

	err = register_netdev(netdev);
	if (err) {
		printk(KERN_ERR PFX
			"Cannot register net device, aborting.\n");
		goto err_out_free_vnic_resources;
	}

	return 0;

err_out_free_vnic_resources:
	enic_free_vnic_resources(enic);
err_out_dev_close:
	vnic_dev_close(enic->vdev);
err_out_vnic_unregister:
	enic_clear_intr_mode(enic);
	vnic_dev_unregister(enic->vdev);
err_out_iounmap:
	enic_iounmap(enic);
err_out_release_regions:
	pci_release_regions(pdev);
err_out_disable_device:
	pci_disable_device(pdev);
err_out_free_netdev:
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);

	return err;
}

static void __devexit enic_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);

	if (netdev) {
		struct enic *enic = netdev_priv(netdev);

		flush_scheduled_work();
		unregister_netdev(netdev);
		enic_free_vnic_resources(enic);
		vnic_dev_close(enic->vdev);
		enic_clear_intr_mode(enic);
		vnic_dev_unregister(enic->vdev);
		enic_iounmap(enic);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
		free_netdev(netdev);
	}
}

static struct pci_driver enic_driver = {
	.name = DRV_NAME,
	.id_table = enic_id_table,
	.probe = enic_probe,
	.remove = __devexit_p(enic_remove),
};

static int __init enic_init_module(void)
{
	printk(KERN_INFO PFX "%s, ver %s\n", DRV_DESCRIPTION, DRV_VERSION);

	return pci_register_driver(&enic_driver);
}

static void __exit enic_cleanup_module(void)
{
	pci_unregister_driver(&enic_driver);
}

module_init(enic_init_module);
module_exit(enic_cleanup_module);
