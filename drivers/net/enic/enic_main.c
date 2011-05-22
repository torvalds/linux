/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
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
#include <linux/rtnetlink.h>
#include <linux/prefetch.h>
#include <net/ip6_checksum.h>

#include "cq_enet_desc.h"
#include "vnic_dev.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "vnic_vic.h"
#include "enic_res.h"
#include "enic.h"
#include "enic_dev.h"
#include "enic_pp.h"

#define ENIC_NOTIFY_TIMER_PERIOD	(2 * HZ)
#define WQ_ENET_MAX_DESC_LEN		(1 << WQ_ENET_LEN_BITS)
#define MAX_TSO				(1 << 16)
#define ENIC_DESC_MAX_SPLITS		(MAX_TSO / WQ_ENET_MAX_DESC_LEN + 1)

#define PCI_DEVICE_ID_CISCO_VIC_ENET         0x0043  /* ethernet vnic */
#define PCI_DEVICE_ID_CISCO_VIC_ENET_DYN     0x0044  /* enet dynamic vnic */

/* Supported devices */
static DEFINE_PCI_DEVICE_TABLE(enic_id_table) = {
	{ PCI_VDEVICE(CISCO, PCI_DEVICE_ID_CISCO_VIC_ENET) },
	{ PCI_VDEVICE(CISCO, PCI_DEVICE_ID_CISCO_VIC_ENET_DYN) },
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

static int enic_is_dynamic(struct enic *enic)
{
	return enic->pdev->device == PCI_DEVICE_ID_CISCO_VIC_ENET_DYN;
}

static inline unsigned int enic_cq_rq(struct enic *enic, unsigned int rq)
{
	return rq;
}

static inline unsigned int enic_cq_wq(struct enic *enic, unsigned int wq)
{
	return enic->rq_count + wq;
}

static inline unsigned int enic_legacy_io_intr(void)
{
	return 0;
}

static inline unsigned int enic_legacy_err_intr(void)
{
	return 1;
}

static inline unsigned int enic_legacy_notify_intr(void)
{
	return 2;
}

static inline unsigned int enic_msix_rq_intr(struct enic *enic, unsigned int rq)
{
	return rq;
}

static inline unsigned int enic_msix_wq_intr(struct enic *enic, unsigned int wq)
{
	return enic->rq_count + wq;
}

static inline unsigned int enic_msix_err_intr(struct enic *enic)
{
	return enic->rq_count + enic->wq_count;
}

static inline unsigned int enic_msix_notify_intr(struct enic *enic)
{
	return enic->rq_count + enic->wq_count + 1;
}

static int enic_get_settings(struct net_device *netdev,
	struct ethtool_cmd *ecmd)
{
	struct enic *enic = netdev_priv(netdev);

	ecmd->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
	ecmd->advertising = (ADVERTISED_10000baseT_Full | ADVERTISED_FIBRE);
	ecmd->port = PORT_FIBRE;
	ecmd->transceiver = XCVR_EXTERNAL;

	if (netif_carrier_ok(netdev)) {
		ethtool_cmd_speed_set(ecmd, vnic_dev_port_speed(enic->vdev));
		ecmd->duplex = DUPLEX_FULL;
	} else {
		ethtool_cmd_speed_set(ecmd, -1);
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

	enic_dev_fw_info(enic, &fw_info);

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

	enic_dev_stats_dump(enic, &vstats);

	for (i = 0; i < enic_n_tx_stats; i++)
		*(data++) = ((u64 *)&vstats->tx)[enic_tx_stats[i].offset];
	for (i = 0; i < enic_n_rx_stats; i++)
		*(data++) = ((u64 *)&vstats->rx)[enic_rx_stats[i].offset];
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

static int enic_get_coalesce(struct net_device *netdev,
	struct ethtool_coalesce *ecmd)
{
	struct enic *enic = netdev_priv(netdev);

	ecmd->tx_coalesce_usecs = enic->tx_coalesce_usecs;
	ecmd->rx_coalesce_usecs = enic->rx_coalesce_usecs;

	return 0;
}

static int enic_set_coalesce(struct net_device *netdev,
	struct ethtool_coalesce *ecmd)
{
	struct enic *enic = netdev_priv(netdev);
	u32 tx_coalesce_usecs;
	u32 rx_coalesce_usecs;
	unsigned int i, intr;

	tx_coalesce_usecs = min_t(u32,
		INTR_COALESCE_HW_TO_USEC(VNIC_INTR_TIMER_MAX),
		ecmd->tx_coalesce_usecs);
	rx_coalesce_usecs = min_t(u32,
		INTR_COALESCE_HW_TO_USEC(VNIC_INTR_TIMER_MAX),
		ecmd->rx_coalesce_usecs);

	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		if (tx_coalesce_usecs != rx_coalesce_usecs)
			return -EINVAL;

		intr = enic_legacy_io_intr();
		vnic_intr_coalescing_timer_set(&enic->intr[intr],
			INTR_COALESCE_USEC_TO_HW(tx_coalesce_usecs));
		break;
	case VNIC_DEV_INTR_MODE_MSI:
		if (tx_coalesce_usecs != rx_coalesce_usecs)
			return -EINVAL;

		vnic_intr_coalescing_timer_set(&enic->intr[0],
			INTR_COALESCE_USEC_TO_HW(tx_coalesce_usecs));
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < enic->wq_count; i++) {
			intr = enic_msix_wq_intr(enic, i);
			vnic_intr_coalescing_timer_set(&enic->intr[intr],
				INTR_COALESCE_USEC_TO_HW(tx_coalesce_usecs));
		}

		for (i = 0; i < enic->rq_count; i++) {
			intr = enic_msix_rq_intr(enic, i);
			vnic_intr_coalescing_timer_set(&enic->intr[intr],
				INTR_COALESCE_USEC_TO_HW(rx_coalesce_usecs));
		}

		break;
	default:
		break;
	}

	enic->tx_coalesce_usecs = tx_coalesce_usecs;
	enic->rx_coalesce_usecs = rx_coalesce_usecs;

	return 0;
}

static const struct ethtool_ops enic_ethtool_ops = {
	.get_settings = enic_get_settings,
	.get_drvinfo = enic_get_drvinfo,
	.get_msglevel = enic_get_msglevel,
	.set_msglevel = enic_set_msglevel,
	.get_link = ethtool_op_get_link,
	.get_strings = enic_get_strings,
	.get_sset_count = enic_get_sset_count,
	.get_ethtool_stats = enic_get_ethtool_stats,
	.get_coalesce = enic_get_coalesce,
	.set_coalesce = enic_set_coalesce,
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
	    vnic_wq_desc_avail(&enic->wq[q_number]) >=
	    (MAX_SKB_FRAGS + ENIC_DESC_MAX_SPLITS))
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
			netdev_err(enic->netdev, "WQ[%d] error_status %d\n",
				i, error_status);
	}

	for (i = 0; i < enic->rq_count; i++) {
		error_status = vnic_rq_error_status(&enic->rq[i]);
		if (error_status)
			netdev_err(enic->netdev, "RQ[%d] error_status %d\n",
				i, error_status);
	}
}

static void enic_msglvl_check(struct enic *enic)
{
	u32 msg_enable = vnic_dev_msg_lvl(enic->vdev);

	if (msg_enable != enic->msg_enable) {
		netdev_info(enic->netdev, "msg lvl changed from 0x%x to 0x%x\n",
			enic->msg_enable, msg_enable);
		enic->msg_enable = msg_enable;
	}
}

static void enic_mtu_check(struct enic *enic)
{
	u32 mtu = vnic_dev_mtu(enic->vdev);
	struct net_device *netdev = enic->netdev;

	if (mtu && mtu != enic->port_mtu) {
		enic->port_mtu = mtu;
		if (mtu < netdev->mtu)
			netdev_warn(netdev,
				"interface MTU (%d) set higher "
				"than switch port MTU (%d)\n",
				netdev->mtu, mtu);
	}
}

static void enic_link_check(struct enic *enic)
{
	int link_status = vnic_dev_link_status(enic->vdev);
	int carrier_ok = netif_carrier_ok(enic->netdev);

	if (link_status && !carrier_ok) {
		netdev_info(enic->netdev, "Link UP\n");
		netif_carrier_on(enic->netdev);
	} else if (!link_status && carrier_ok) {
		netdev_info(enic->netdev, "Link DOWN\n");
		netif_carrier_off(enic->netdev);
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
	unsigned int io_intr = enic_legacy_io_intr();
	unsigned int err_intr = enic_legacy_err_intr();
	unsigned int notify_intr = enic_legacy_notify_intr();
	u32 pba;

	vnic_intr_mask(&enic->intr[io_intr]);

	pba = vnic_intr_legacy_pba(enic->legacy_pba);
	if (!pba) {
		vnic_intr_unmask(&enic->intr[io_intr]);
		return IRQ_NONE;	/* not our interrupt */
	}

	if (ENIC_TEST_INTR(pba, notify_intr)) {
		vnic_intr_return_all_credits(&enic->intr[notify_intr]);
		enic_notify_check(enic);
	}

	if (ENIC_TEST_INTR(pba, err_intr)) {
		vnic_intr_return_all_credits(&enic->intr[err_intr]);
		enic_log_q_error(enic);
		/* schedule recovery from WQ/RQ error */
		schedule_work(&enic->reset);
		return IRQ_HANDLED;
	}

	if (ENIC_TEST_INTR(pba, io_intr)) {
		if (napi_schedule_prep(&enic->napi[0]))
			__napi_schedule(&enic->napi[0]);
	} else {
		vnic_intr_unmask(&enic->intr[io_intr]);
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

	napi_schedule(&enic->napi[0]);

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msix_rq(int irq, void *data)
{
	struct napi_struct *napi = data;

	/* schedule NAPI polling for RQ cleanup */
	napi_schedule(napi);

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msix_wq(int irq, void *data)
{
	struct enic *enic = data;
	unsigned int cq = enic_cq_wq(enic, 0);
	unsigned int intr = enic_msix_wq_intr(enic, 0);
	unsigned int wq_work_to_do = -1; /* no limit */
	unsigned int wq_work_done;

	wq_work_done = vnic_cq_service(&enic->cq[cq],
		wq_work_to_do, enic_wq_service, NULL);

	vnic_intr_return_credits(&enic->intr[intr],
		wq_work_done,
		1 /* unmask intr */,
		1 /* reset intr timer */);

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msix_err(int irq, void *data)
{
	struct enic *enic = data;
	unsigned int intr = enic_msix_err_intr(enic);

	vnic_intr_return_all_credits(&enic->intr[intr]);

	enic_log_q_error(enic);

	/* schedule recovery from WQ/RQ error */
	schedule_work(&enic->reset);

	return IRQ_HANDLED;
}

static irqreturn_t enic_isr_msix_notify(int irq, void *data)
{
	struct enic *enic = data;
	unsigned int intr = enic_msix_notify_intr(enic);

	vnic_intr_return_all_credits(&enic->intr[intr]);
	enic_notify_check(enic);

	return IRQ_HANDLED;
}

static inline void enic_queue_wq_skb_cont(struct enic *enic,
	struct vnic_wq *wq, struct sk_buff *skb,
	unsigned int len_left, int loopback)
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
			(len_left == 0),	/* EOP? */
			loopback);
	}
}

static inline void enic_queue_wq_skb_vlan(struct enic *enic,
	struct vnic_wq *wq, struct sk_buff *skb,
	int vlan_tag_insert, unsigned int vlan_tag, int loopback)
{
	unsigned int head_len = skb_headlen(skb);
	unsigned int len_left = skb->len - head_len;
	int eop = (len_left == 0);

	/* Queue the main skb fragment. The fragments are no larger
	 * than max MTU(9000)+ETH_HDR_LEN(14) bytes, which is less
	 * than WQ_ENET_MAX_DESC_LEN length. So only one descriptor
	 * per fragment is queued.
	 */
	enic_queue_wq_desc(wq, skb,
		pci_map_single(enic->pdev, skb->data,
			head_len, PCI_DMA_TODEVICE),
		head_len,
		vlan_tag_insert, vlan_tag,
		eop, loopback);

	if (!eop)
		enic_queue_wq_skb_cont(enic, wq, skb, len_left, loopback);
}

static inline void enic_queue_wq_skb_csum_l4(struct enic *enic,
	struct vnic_wq *wq, struct sk_buff *skb,
	int vlan_tag_insert, unsigned int vlan_tag, int loopback)
{
	unsigned int head_len = skb_headlen(skb);
	unsigned int len_left = skb->len - head_len;
	unsigned int hdr_len = skb_checksum_start_offset(skb);
	unsigned int csum_offset = hdr_len + skb->csum_offset;
	int eop = (len_left == 0);

	/* Queue the main skb fragment. The fragments are no larger
	 * than max MTU(9000)+ETH_HDR_LEN(14) bytes, which is less
	 * than WQ_ENET_MAX_DESC_LEN length. So only one descriptor
	 * per fragment is queued.
	 */
	enic_queue_wq_desc_csum_l4(wq, skb,
		pci_map_single(enic->pdev, skb->data,
			head_len, PCI_DMA_TODEVICE),
		head_len,
		csum_offset,
		hdr_len,
		vlan_tag_insert, vlan_tag,
		eop, loopback);

	if (!eop)
		enic_queue_wq_skb_cont(enic, wq, skb, len_left, loopback);
}

static inline void enic_queue_wq_skb_tso(struct enic *enic,
	struct vnic_wq *wq, struct sk_buff *skb, unsigned int mss,
	int vlan_tag_insert, unsigned int vlan_tag, int loopback)
{
	unsigned int frag_len_left = skb_headlen(skb);
	unsigned int len_left = skb->len - frag_len_left;
	unsigned int hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
	int eop = (len_left == 0);
	unsigned int len;
	dma_addr_t dma_addr;
	unsigned int offset = 0;
	skb_frag_t *frag;

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

	/* Queue WQ_ENET_MAX_DESC_LEN length descriptors
	 * for the main skb fragment
	 */
	while (frag_len_left) {
		len = min(frag_len_left, (unsigned int)WQ_ENET_MAX_DESC_LEN);
		dma_addr = pci_map_single(enic->pdev, skb->data + offset,
				len, PCI_DMA_TODEVICE);
		enic_queue_wq_desc_tso(wq, skb,
			dma_addr,
			len,
			mss, hdr_len,
			vlan_tag_insert, vlan_tag,
			eop && (len == frag_len_left), loopback);
		frag_len_left -= len;
		offset += len;
	}

	if (eop)
		return;

	/* Queue WQ_ENET_MAX_DESC_LEN length descriptors
	 * for additional data fragments
	 */
	for (frag = skb_shinfo(skb)->frags; len_left; frag++) {
		len_left -= frag->size;
		frag_len_left = frag->size;
		offset = frag->page_offset;

		while (frag_len_left) {
			len = min(frag_len_left,
				(unsigned int)WQ_ENET_MAX_DESC_LEN);
			dma_addr = pci_map_page(enic->pdev, frag->page,
				offset, len,
				PCI_DMA_TODEVICE);
			enic_queue_wq_desc_cont(wq, skb,
				dma_addr,
				len,
				(len_left == 0) &&
				(len == frag_len_left),		/* EOP? */
				loopback);
			frag_len_left -= len;
			offset += len;
		}
	}
}

static inline void enic_queue_wq_skb(struct enic *enic,
	struct vnic_wq *wq, struct sk_buff *skb)
{
	unsigned int mss = skb_shinfo(skb)->gso_size;
	unsigned int vlan_tag = 0;
	int vlan_tag_insert = 0;
	int loopback = 0;

	if (vlan_tx_tag_present(skb)) {
		/* VLAN tag from trunking driver */
		vlan_tag_insert = 1;
		vlan_tag = vlan_tx_tag_get(skb);
	} else if (enic->loop_enable) {
		vlan_tag = enic->loop_tag;
		loopback = 1;
	}

	if (mss)
		enic_queue_wq_skb_tso(enic, wq, skb, mss,
			vlan_tag_insert, vlan_tag, loopback);
	else if	(skb->ip_summed == CHECKSUM_PARTIAL)
		enic_queue_wq_skb_csum_l4(enic, wq, skb,
			vlan_tag_insert, vlan_tag, loopback);
	else
		enic_queue_wq_skb_vlan(enic, wq, skb,
			vlan_tag_insert, vlan_tag, loopback);
}

/* netif_tx_lock held, process context with BHs disabled, or BH */
static netdev_tx_t enic_hard_start_xmit(struct sk_buff *skb,
	struct net_device *netdev)
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

	if (vnic_wq_desc_avail(wq) <
	    skb_shinfo(skb)->nr_frags + ENIC_DESC_MAX_SPLITS) {
		netif_stop_queue(netdev);
		/* This is a hard error, log it */
		netdev_err(netdev, "BUG! Tx ring full when queue awake!\n");
		spin_unlock_irqrestore(&enic->wq_lock[0], flags);
		return NETDEV_TX_BUSY;
	}

	enic_queue_wq_skb(enic, wq, skb);

	if (vnic_wq_desc_avail(wq) < MAX_SKB_FRAGS + ENIC_DESC_MAX_SPLITS)
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

	enic_dev_stats_dump(enic, &stats);

	net_stats->tx_packets = stats->tx.tx_frames_ok;
	net_stats->tx_bytes = stats->tx.tx_bytes_ok;
	net_stats->tx_errors = stats->tx.tx_errors;
	net_stats->tx_dropped = stats->tx.tx_drops;

	net_stats->rx_packets = stats->rx.rx_frames_ok;
	net_stats->rx_bytes = stats->rx.rx_bytes_ok;
	net_stats->rx_errors = stats->rx.rx_errors;
	net_stats->multicast = stats->rx.rx_multicast_frames_ok;
	net_stats->rx_over_errors = enic->rq_truncated_pkts;
	net_stats->rx_crc_errors = enic->rq_bad_fcs;
	net_stats->rx_dropped = stats->rx.rx_no_bufs + stats->rx.rx_drop;

	return net_stats;
}

void enic_reset_addr_lists(struct enic *enic)
{
	enic->mc_count = 0;
	enic->uc_count = 0;
	enic->flags = 0;
}

static int enic_set_mac_addr(struct net_device *netdev, char *addr)
{
	struct enic *enic = netdev_priv(netdev);

	if (enic_is_dynamic(enic)) {
		if (!is_valid_ether_addr(addr) && !is_zero_ether_addr(addr))
			return -EADDRNOTAVAIL;
	} else {
		if (!is_valid_ether_addr(addr))
			return -EADDRNOTAVAIL;
	}

	memcpy(netdev->dev_addr, addr, netdev->addr_len);

	return 0;
}

static int enic_set_mac_address_dynamic(struct net_device *netdev, void *p)
{
	struct enic *enic = netdev_priv(netdev);
	struct sockaddr *saddr = p;
	char *addr = saddr->sa_data;
	int err;

	if (netif_running(enic->netdev)) {
		err = enic_dev_del_station_addr(enic);
		if (err)
			return err;
	}

	err = enic_set_mac_addr(netdev, addr);
	if (err)
		return err;

	if (netif_running(enic->netdev)) {
		err = enic_dev_add_station_addr(enic);
		if (err)
			return err;
	}

	return err;
}

static int enic_set_mac_address(struct net_device *netdev, void *p)
{
	struct sockaddr *saddr = p;
	char *addr = saddr->sa_data;
	struct enic *enic = netdev_priv(netdev);
	int err;

	err = enic_dev_del_station_addr(enic);
	if (err)
		return err;

	err = enic_set_mac_addr(netdev, addr);
	if (err)
		return err;

	return enic_dev_add_station_addr(enic);
}

static void enic_update_multicast_addr_list(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
	struct netdev_hw_addr *ha;
	unsigned int mc_count = netdev_mc_count(netdev);
	u8 mc_addr[ENIC_MULTICAST_PERFECT_FILTERS][ETH_ALEN];
	unsigned int i, j;

	if (mc_count > ENIC_MULTICAST_PERFECT_FILTERS) {
		netdev_warn(netdev, "Registering only %d out of %d "
			"multicast addresses\n",
			ENIC_MULTICAST_PERFECT_FILTERS, mc_count);
		mc_count = ENIC_MULTICAST_PERFECT_FILTERS;
	}

	/* Is there an easier way?  Trying to minimize to
	 * calls to add/del multicast addrs.  We keep the
	 * addrs from the last call in enic->mc_addr and
	 * look for changes to add/del.
	 */

	i = 0;
	netdev_for_each_mc_addr(ha, netdev) {
		if (i == mc_count)
			break;
		memcpy(mc_addr[i++], ha->addr, ETH_ALEN);
	}

	for (i = 0; i < enic->mc_count; i++) {
		for (j = 0; j < mc_count; j++)
			if (compare_ether_addr(enic->mc_addr[i],
				mc_addr[j]) == 0)
				break;
		if (j == mc_count)
			enic_dev_del_addr(enic, enic->mc_addr[i]);
	}

	for (i = 0; i < mc_count; i++) {
		for (j = 0; j < enic->mc_count; j++)
			if (compare_ether_addr(mc_addr[i],
				enic->mc_addr[j]) == 0)
				break;
		if (j == enic->mc_count)
			enic_dev_add_addr(enic, mc_addr[i]);
	}

	/* Save the list to compare against next time
	 */

	for (i = 0; i < mc_count; i++)
		memcpy(enic->mc_addr[i], mc_addr[i], ETH_ALEN);

	enic->mc_count = mc_count;
}

static void enic_update_unicast_addr_list(struct enic *enic)
{
	struct net_device *netdev = enic->netdev;
	struct netdev_hw_addr *ha;
	unsigned int uc_count = netdev_uc_count(netdev);
	u8 uc_addr[ENIC_UNICAST_PERFECT_FILTERS][ETH_ALEN];
	unsigned int i, j;

	if (uc_count > ENIC_UNICAST_PERFECT_FILTERS) {
		netdev_warn(netdev, "Registering only %d out of %d "
			"unicast addresses\n",
			ENIC_UNICAST_PERFECT_FILTERS, uc_count);
		uc_count = ENIC_UNICAST_PERFECT_FILTERS;
	}

	/* Is there an easier way?  Trying to minimize to
	 * calls to add/del unicast addrs.  We keep the
	 * addrs from the last call in enic->uc_addr and
	 * look for changes to add/del.
	 */

	i = 0;
	netdev_for_each_uc_addr(ha, netdev) {
		if (i == uc_count)
			break;
		memcpy(uc_addr[i++], ha->addr, ETH_ALEN);
	}

	for (i = 0; i < enic->uc_count; i++) {
		for (j = 0; j < uc_count; j++)
			if (compare_ether_addr(enic->uc_addr[i],
				uc_addr[j]) == 0)
				break;
		if (j == uc_count)
			enic_dev_del_addr(enic, enic->uc_addr[i]);
	}

	for (i = 0; i < uc_count; i++) {
		for (j = 0; j < enic->uc_count; j++)
			if (compare_ether_addr(uc_addr[i],
				enic->uc_addr[j]) == 0)
				break;
		if (j == enic->uc_count)
			enic_dev_add_addr(enic, uc_addr[i]);
	}

	/* Save the list to compare against next time
	 */

	for (i = 0; i < uc_count; i++)
		memcpy(enic->uc_addr[i], uc_addr[i], ETH_ALEN);

	enic->uc_count = uc_count;
}

/* netif_tx_lock held, BHs disabled */
static void enic_set_rx_mode(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	int directed = 1;
	int multicast = (netdev->flags & IFF_MULTICAST) ? 1 : 0;
	int broadcast = (netdev->flags & IFF_BROADCAST) ? 1 : 0;
	int promisc = (netdev->flags & IFF_PROMISC) ||
		netdev_uc_count(netdev) > ENIC_UNICAST_PERFECT_FILTERS;
	int allmulti = (netdev->flags & IFF_ALLMULTI) ||
		netdev_mc_count(netdev) > ENIC_MULTICAST_PERFECT_FILTERS;
	unsigned int flags = netdev->flags |
		(allmulti ? IFF_ALLMULTI : 0) |
		(promisc ? IFF_PROMISC : 0);

	if (enic->flags != flags) {
		enic->flags = flags;
		enic_dev_packet_filter(enic, directed,
			multicast, broadcast, promisc, allmulti);
	}

	if (!promisc) {
		enic_update_unicast_addr_list(enic);
		if (!allmulti)
			enic_update_multicast_addr_list(enic);
	}
}

/* rtnl lock is held */
static void enic_vlan_rx_register(struct net_device *netdev,
	struct vlan_group *vlan_group)
{
	struct enic *enic = netdev_priv(netdev);
	enic->vlan_group = vlan_group;
}

/* netif_tx_lock held, BHs disabled */
static void enic_tx_timeout(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	schedule_work(&enic->reset);
}

static int enic_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct enic *enic = netdev_priv(netdev);

	if (vf != PORT_SELF_VF)
		return -EOPNOTSUPP;

	/* Ignore the vf argument for now. We can assume the request
	 * is coming on a vf.
	 */
	if (is_valid_ether_addr(mac)) {
		memcpy(enic->pp.vf_mac, mac, ETH_ALEN);
		return 0;
	} else
		return -EINVAL;
}

static int enic_set_vf_port(struct net_device *netdev, int vf,
	struct nlattr *port[])
{
	struct enic *enic = netdev_priv(netdev);
	struct enic_port_profile prev_pp;
	int err = 0, restore_pp = 1;

	/* don't support VFs, yet */
	if (vf != PORT_SELF_VF)
		return -EOPNOTSUPP;

	if (!port[IFLA_PORT_REQUEST])
		return -EOPNOTSUPP;

	memcpy(&prev_pp, &enic->pp, sizeof(enic->pp));
	memset(&enic->pp, 0, sizeof(enic->pp));

	enic->pp.set |= ENIC_SET_REQUEST;
	enic->pp.request = nla_get_u8(port[IFLA_PORT_REQUEST]);

	if (port[IFLA_PORT_PROFILE]) {
		enic->pp.set |= ENIC_SET_NAME;
		memcpy(enic->pp.name, nla_data(port[IFLA_PORT_PROFILE]),
			PORT_PROFILE_MAX);
	}

	if (port[IFLA_PORT_INSTANCE_UUID]) {
		enic->pp.set |= ENIC_SET_INSTANCE;
		memcpy(enic->pp.instance_uuid,
			nla_data(port[IFLA_PORT_INSTANCE_UUID]), PORT_UUID_MAX);
	}

	if (port[IFLA_PORT_HOST_UUID]) {
		enic->pp.set |= ENIC_SET_HOST;
		memcpy(enic->pp.host_uuid,
			nla_data(port[IFLA_PORT_HOST_UUID]), PORT_UUID_MAX);
	}

	/* Special case handling: mac came from IFLA_VF_MAC */
	if (!is_zero_ether_addr(prev_pp.vf_mac))
		memcpy(enic->pp.mac_addr, prev_pp.vf_mac, ETH_ALEN);

		if (is_zero_ether_addr(netdev->dev_addr))
			random_ether_addr(netdev->dev_addr);

	err = enic_process_set_pp_request(enic, &prev_pp, &restore_pp);
	if (err) {
		if (restore_pp) {
			/* Things are still the way they were: Implicit
			 * DISASSOCIATE failed
			 */
			memcpy(&enic->pp, &prev_pp, sizeof(enic->pp));
		} else {
			memset(&enic->pp, 0, sizeof(enic->pp));
			memset(netdev->dev_addr, 0, ETH_ALEN);
		}
	} else {
		/* Set flag to indicate that the port assoc/disassoc
		 * request has been sent out to fw
		 */
		enic->pp.set |= ENIC_PORT_REQUEST_APPLIED;

		/* If DISASSOCIATE, clean up all assigned/saved macaddresses */
		if (enic->pp.request == PORT_REQUEST_DISASSOCIATE) {
			memset(enic->pp.mac_addr, 0, ETH_ALEN);
			memset(netdev->dev_addr, 0, ETH_ALEN);
		}
	}

	memset(enic->pp.vf_mac, 0, ETH_ALEN);

	return err;
}

static int enic_get_vf_port(struct net_device *netdev, int vf,
	struct sk_buff *skb)
{
	struct enic *enic = netdev_priv(netdev);
	u16 response = PORT_PROFILE_RESPONSE_SUCCESS;
	int err;

	if (!(enic->pp.set & ENIC_PORT_REQUEST_APPLIED))
		return -ENODATA;

	err = enic_process_get_pp_request(enic, enic->pp.request, &response);
	if (err)
		return err;

	NLA_PUT_U16(skb, IFLA_PORT_REQUEST, enic->pp.request);
	NLA_PUT_U16(skb, IFLA_PORT_RESPONSE, response);
	if (enic->pp.set & ENIC_SET_NAME)
		NLA_PUT(skb, IFLA_PORT_PROFILE, PORT_PROFILE_MAX,
			enic->pp.name);
	if (enic->pp.set & ENIC_SET_INSTANCE)
		NLA_PUT(skb, IFLA_PORT_INSTANCE_UUID, PORT_UUID_MAX,
			enic->pp.instance_uuid);
	if (enic->pp.set & ENIC_SET_HOST)
		NLA_PUT(skb, IFLA_PORT_HOST_UUID, PORT_UUID_MAX,
			enic->pp.host_uuid);

	return 0;

nla_put_failure:
	return -EMSGSIZE;
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

static int enic_rq_alloc_buf(struct vnic_rq *rq)
{
	struct enic *enic = vnic_dev_priv(rq->vdev);
	struct net_device *netdev = enic->netdev;
	struct sk_buff *skb;
	unsigned int len = netdev->mtu + VLAN_ETH_HLEN;
	unsigned int os_buf_index = 0;
	dma_addr_t dma_addr;

	skb = netdev_alloc_skb_ip_align(netdev, len);
	if (!skb)
		return -ENOMEM;

	dma_addr = pci_map_single(enic->pdev, skb->data,
		len, PCI_DMA_FROMDEVICE);

	enic_queue_rq_desc(rq, skb, os_buf_index,
		dma_addr, len);

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
	u16 q_number, completed_index, bytes_written, vlan_tci, checksum;
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
		&packet_error, &vlan_stripped, &vlan_tci, &checksum,
		&fcoe_sof, &fcoe_fc_crc_ok, &fcoe_enc_error,
		&fcoe_eof, &tcp_udp_csum_ok, &udp, &tcp,
		&ipv4_csum_ok, &ipv6, &ipv4, &ipv4_fragment,
		&fcs_ok);

	if (packet_error) {

		if (!fcs_ok) {
			if (bytes_written > 0)
				enic->rq_bad_fcs++;
			else if (bytes_written == 0)
				enic->rq_truncated_pkts++;
		}

		dev_kfree_skb_any(skb);

		return;
	}

	if (eop && bytes_written > 0) {

		/* Good receive
		 */

		skb_put(skb, bytes_written);
		skb->protocol = eth_type_trans(skb, netdev);

		if ((netdev->features & NETIF_F_RXCSUM) && !csum_not_calc) {
			skb->csum = htons(checksum);
			skb->ip_summed = CHECKSUM_COMPLETE;
		}

		skb->dev = netdev;

		if (enic->vlan_group && vlan_stripped &&
			(vlan_tci & CQ_ENET_RQ_DESC_VLAN_TCI_VLAN_MASK)) {

			if (netdev->features & NETIF_F_GRO)
				vlan_gro_receive(&enic->napi[q_number],
					enic->vlan_group, vlan_tci, skb);
			else
				vlan_hwaccel_receive_skb(skb,
					enic->vlan_group, vlan_tci);

		} else {

			if (netdev->features & NETIF_F_GRO)
				napi_gro_receive(&enic->napi[q_number], skb);
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

static int enic_poll(struct napi_struct *napi, int budget)
{
	struct net_device *netdev = napi->dev;
	struct enic *enic = netdev_priv(netdev);
	unsigned int cq_rq = enic_cq_rq(enic, 0);
	unsigned int cq_wq = enic_cq_wq(enic, 0);
	unsigned int intr = enic_legacy_io_intr();
	unsigned int rq_work_to_do = budget;
	unsigned int wq_work_to_do = -1; /* no limit */
	unsigned int  work_done, rq_work_done, wq_work_done;
	int err;

	/* Service RQ (first) and WQ
	 */

	rq_work_done = vnic_cq_service(&enic->cq[cq_rq],
		rq_work_to_do, enic_rq_service, NULL);

	wq_work_done = vnic_cq_service(&enic->cq[cq_wq],
		wq_work_to_do, enic_wq_service, NULL);

	/* Accumulate intr event credits for this polling
	 * cycle.  An intr event is the completion of a
	 * a WQ or RQ packet.
	 */

	work_done = rq_work_done + wq_work_done;

	if (work_done > 0)
		vnic_intr_return_credits(&enic->intr[intr],
			work_done,
			0 /* don't unmask intr */,
			0 /* don't reset intr timer */);

	err = vnic_rq_fill(&enic->rq[0], enic_rq_alloc_buf);

	/* Buffer allocation failed. Stay in polling
	 * mode so we can try to fill the ring again.
	 */

	if (err)
		rq_work_done = rq_work_to_do;

	if (rq_work_done < rq_work_to_do) {

		/* Some work done, but not enough to stay in polling,
		 * exit polling
		 */

		napi_complete(napi);
		vnic_intr_unmask(&enic->intr[intr]);
	}

	return rq_work_done;
}

static int enic_poll_msix(struct napi_struct *napi, int budget)
{
	struct net_device *netdev = napi->dev;
	struct enic *enic = netdev_priv(netdev);
	unsigned int rq = (napi - &enic->napi[0]);
	unsigned int cq = enic_cq_rq(enic, rq);
	unsigned int intr = enic_msix_rq_intr(enic, rq);
	unsigned int work_to_do = budget;
	unsigned int work_done;
	int err;

	/* Service RQ
	 */

	work_done = vnic_cq_service(&enic->cq[cq],
		work_to_do, enic_rq_service, NULL);

	/* Return intr event credits for this polling
	 * cycle.  An intr event is the completion of a
	 * RQ packet.
	 */

	if (work_done > 0)
		vnic_intr_return_credits(&enic->intr[intr],
			work_done,
			0 /* don't unmask intr */,
			0 /* don't reset intr timer */);

	err = vnic_rq_fill(&enic->rq[rq], enic_rq_alloc_buf);

	/* Buffer allocation failed. Stay in polling mode
	 * so we can try to fill the ring again.
	 */

	if (err)
		work_done = work_to_do;

	if (work_done < work_to_do) {

		/* Some work done, but not enough to stay in polling,
		 * exit polling
		 */

		napi_complete(napi);
		vnic_intr_unmask(&enic->intr[intr]);
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
	unsigned int i, intr;
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

		for (i = 0; i < enic->rq_count; i++) {
			intr = enic_msix_rq_intr(enic, i);
			sprintf(enic->msix[intr].devname,
				"%.11s-rx-%d", netdev->name, i);
			enic->msix[intr].isr = enic_isr_msix_rq;
			enic->msix[intr].devid = &enic->napi[i];
		}

		for (i = 0; i < enic->wq_count; i++) {
			intr = enic_msix_wq_intr(enic, i);
			sprintf(enic->msix[intr].devname,
				"%.11s-tx-%d", netdev->name, i);
			enic->msix[intr].isr = enic_isr_msix_wq;
			enic->msix[intr].devid = enic;
		}

		intr = enic_msix_err_intr(enic);
		sprintf(enic->msix[intr].devname,
			"%.11s-err", netdev->name);
		enic->msix[intr].isr = enic_isr_msix_err;
		enic->msix[intr].devid = enic;

		intr = enic_msix_notify_intr(enic);
		sprintf(enic->msix[intr].devname,
			"%.11s-notify", netdev->name);
		enic->msix[intr].isr = enic_isr_msix_notify;
		enic->msix[intr].devid = enic;

		for (i = 0; i < ARRAY_SIZE(enic->msix); i++)
			enic->msix[i].requested = 0;

		for (i = 0; i < enic->intr_count; i++) {
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

static void enic_synchronize_irqs(struct enic *enic)
{
	unsigned int i;

	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
	case VNIC_DEV_INTR_MODE_MSI:
		synchronize_irq(enic->pdev->irq);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < enic->intr_count; i++)
			synchronize_irq(enic->msix_entry[i].vector);
		break;
	default:
		break;
	}
}

static int enic_dev_notify_set(struct enic *enic)
{
	int err;

	spin_lock(&enic->devcmd_lock);
	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	case VNIC_DEV_INTR_MODE_INTX:
		err = vnic_dev_notify_set(enic->vdev,
			enic_legacy_notify_intr());
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		err = vnic_dev_notify_set(enic->vdev,
			enic_msix_notify_intr(enic));
		break;
	default:
		err = vnic_dev_notify_set(enic->vdev, -1 /* no intr */);
		break;
	}
	spin_unlock(&enic->devcmd_lock);

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
		netdev_err(netdev, "Unable to request irq.\n");
		return err;
	}

	err = enic_dev_notify_set(enic);
	if (err) {
		netdev_err(netdev,
			"Failed to alloc notify buffer, aborting.\n");
		goto err_out_free_intr;
	}

	for (i = 0; i < enic->rq_count; i++) {
		vnic_rq_fill(&enic->rq[i], enic_rq_alloc_buf);
		/* Need at least one buffer on ring to get going */
		if (vnic_rq_desc_used(&enic->rq[i]) == 0) {
			netdev_err(netdev, "Unable to alloc receive buffers\n");
			err = -ENOMEM;
			goto err_out_notify_unset;
		}
	}

	for (i = 0; i < enic->wq_count; i++)
		vnic_wq_enable(&enic->wq[i]);
	for (i = 0; i < enic->rq_count; i++)
		vnic_rq_enable(&enic->rq[i]);

	if (enic_is_dynamic(enic) && !is_zero_ether_addr(enic->pp.mac_addr))
		enic_dev_add_addr(enic, enic->pp.mac_addr);
	else
		enic_dev_add_station_addr(enic);
	enic_set_rx_mode(netdev);

	netif_wake_queue(netdev);

	for (i = 0; i < enic->rq_count; i++)
		napi_enable(&enic->napi[i]);

	enic_dev_enable(enic);

	for (i = 0; i < enic->intr_count; i++)
		vnic_intr_unmask(&enic->intr[i]);

	enic_notify_timer_start(enic);

	return 0;

err_out_notify_unset:
	enic_dev_notify_unset(enic);
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

	for (i = 0; i < enic->intr_count; i++) {
		vnic_intr_mask(&enic->intr[i]);
		(void)vnic_intr_masked(&enic->intr[i]); /* flush write */
	}

	enic_synchronize_irqs(enic);

	del_timer_sync(&enic->notify_timer);

	enic_dev_disable(enic);

	for (i = 0; i < enic->rq_count; i++)
		napi_disable(&enic->napi[i]);

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);
	if (enic_is_dynamic(enic) && !is_zero_ether_addr(enic->pp.mac_addr))
		enic_dev_del_addr(enic, enic->pp.mac_addr);
	else
		enic_dev_del_station_addr(enic);

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

	enic_dev_notify_unset(enic);
	enic_free_intr(enic);

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
		netdev_warn(netdev,
			"interface MTU (%d) set higher than port MTU (%d)\n",
			netdev->mtu, enic->port_mtu);

	if (running)
		enic_open(netdev);

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void enic_poll_controller(struct net_device *netdev)
{
	struct enic *enic = netdev_priv(netdev);
	struct vnic_dev *vdev = enic->vdev;
	unsigned int i, intr;

	switch (vnic_dev_get_intr_mode(vdev)) {
	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < enic->rq_count; i++) {
			intr = enic_msix_rq_intr(enic, i);
			enic_isr_msix_rq(enic->msix_entry[intr].vector,
				&enic->napi[i]);
		}
		intr = enic_msix_wq_intr(enic, i);
		enic_isr_msix_wq(enic->msix_entry[intr].vector, enic);
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
		dev_err(enic_get_dev(enic), "vNIC device open failed, err %d\n",
			err);

	return err;
}

static int enic_dev_hang_reset(struct enic *enic)
{
	int err;

	err = enic_dev_wait(enic->vdev, vnic_dev_hang_reset,
		vnic_dev_hang_reset_done, 0);
	if (err)
		netdev_err(enic->netdev, "vNIC hang reset failed, err %d\n",
			err);

	return err;
}

static int enic_set_rsskey(struct enic *enic)
{
	dma_addr_t rss_key_buf_pa;
	union vnic_rss_key *rss_key_buf_va = NULL;
	union vnic_rss_key rss_key = {
		.key[0].b = {85, 67, 83, 97, 119, 101, 115, 111, 109, 101},
		.key[1].b = {80, 65, 76, 79, 117, 110, 105, 113, 117, 101},
		.key[2].b = {76, 73, 78, 85, 88, 114, 111, 99, 107, 115},
		.key[3].b = {69, 78, 73, 67, 105, 115, 99, 111, 111, 108},
	};
	int err;

	rss_key_buf_va = pci_alloc_consistent(enic->pdev,
		sizeof(union vnic_rss_key), &rss_key_buf_pa);
	if (!rss_key_buf_va)
		return -ENOMEM;

	memcpy(rss_key_buf_va, &rss_key, sizeof(union vnic_rss_key));

	spin_lock(&enic->devcmd_lock);
	err = enic_set_rss_key(enic,
		rss_key_buf_pa,
		sizeof(union vnic_rss_key));
	spin_unlock(&enic->devcmd_lock);

	pci_free_consistent(enic->pdev, sizeof(union vnic_rss_key),
		rss_key_buf_va, rss_key_buf_pa);

	return err;
}

static int enic_set_rsscpu(struct enic *enic, u8 rss_hash_bits)
{
	dma_addr_t rss_cpu_buf_pa;
	union vnic_rss_cpu *rss_cpu_buf_va = NULL;
	unsigned int i;
	int err;

	rss_cpu_buf_va = pci_alloc_consistent(enic->pdev,
		sizeof(union vnic_rss_cpu), &rss_cpu_buf_pa);
	if (!rss_cpu_buf_va)
		return -ENOMEM;

	for (i = 0; i < (1 << rss_hash_bits); i++)
		(*rss_cpu_buf_va).cpu[i/4].b[i%4] = i % enic->rq_count;

	spin_lock(&enic->devcmd_lock);
	err = enic_set_rss_cpu(enic,
		rss_cpu_buf_pa,
		sizeof(union vnic_rss_cpu));
	spin_unlock(&enic->devcmd_lock);

	pci_free_consistent(enic->pdev, sizeof(union vnic_rss_cpu),
		rss_cpu_buf_va, rss_cpu_buf_pa);

	return err;
}

static int enic_set_niccfg(struct enic *enic, u8 rss_default_cpu,
	u8 rss_hash_type, u8 rss_hash_bits, u8 rss_base_cpu, u8 rss_enable)
{
	const u8 tso_ipid_split_en = 0;
	const u8 ig_vlan_strip_en = 1;
	int err;

	/* Enable VLAN tag stripping.
	*/

	spin_lock(&enic->devcmd_lock);
	err = enic_set_nic_cfg(enic,
		rss_default_cpu, rss_hash_type,
		rss_hash_bits, rss_base_cpu,
		rss_enable, tso_ipid_split_en,
		ig_vlan_strip_en);
	spin_unlock(&enic->devcmd_lock);

	return err;
}

static int enic_set_rss_nic_cfg(struct enic *enic)
{
	struct device *dev = enic_get_dev(enic);
	const u8 rss_default_cpu = 0;
	const u8 rss_hash_type = NIC_CFG_RSS_HASH_TYPE_IPV4 |
		NIC_CFG_RSS_HASH_TYPE_TCP_IPV4 |
		NIC_CFG_RSS_HASH_TYPE_IPV6 |
		NIC_CFG_RSS_HASH_TYPE_TCP_IPV6;
	const u8 rss_hash_bits = 7;
	const u8 rss_base_cpu = 0;
	u8 rss_enable = ENIC_SETTING(enic, RSS) && (enic->rq_count > 1);

	if (rss_enable) {
		if (!enic_set_rsskey(enic)) {
			if (enic_set_rsscpu(enic, rss_hash_bits)) {
				rss_enable = 0;
				dev_warn(dev, "RSS disabled, "
					"Failed to set RSS cpu indirection table.");
			}
		} else {
			rss_enable = 0;
			dev_warn(dev, "RSS disabled, Failed to set RSS key.\n");
		}
	}

	return enic_set_niccfg(enic, rss_default_cpu, rss_hash_type,
		rss_hash_bits, rss_base_cpu, rss_enable);
}

static void enic_reset(struct work_struct *work)
{
	struct enic *enic = container_of(work, struct enic, reset);

	if (!netif_running(enic->netdev))
		return;

	rtnl_lock();

	enic_dev_hang_notify(enic);
	enic_stop(enic->netdev);
	enic_dev_hang_reset(enic);
	enic_reset_addr_lists(enic);
	enic_init_vnic_resources(enic);
	enic_set_rss_nic_cfg(enic);
	enic_dev_set_ig_vlan_rewrite_mode(enic);
	enic_open(enic->netdev);

	rtnl_unlock();
}

static int enic_set_intr_mode(struct enic *enic)
{
	unsigned int n = min_t(unsigned int, enic->rq_count, ENIC_RQ_MAX);
	unsigned int m = min_t(unsigned int, enic->wq_count, ENIC_WQ_MAX);
	unsigned int i;

	/* Set interrupt mode (INTx, MSI, MSI-X) depending
	 * on system capabilities.
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

	/* Use multiple RQs if RSS is enabled
	 */

	if (ENIC_SETTING(enic, RSS) &&
	    enic->config.intr_mode < 1 &&
	    enic->rq_count >= n &&
	    enic->wq_count >= m &&
	    enic->cq_count >= n + m &&
	    enic->intr_count >= n + m + 2) {

		if (!pci_enable_msix(enic->pdev, enic->msix_entry, n + m + 2)) {

			enic->rq_count = n;
			enic->wq_count = m;
			enic->cq_count = n + m;
			enic->intr_count = n + m + 2;

			vnic_dev_set_intr_mode(enic->vdev,
				VNIC_DEV_INTR_MODE_MSIX);

			return 0;
		}
	}

	if (enic->config.intr_mode < 1 &&
	    enic->rq_count >= 1 &&
	    enic->wq_count >= m &&
	    enic->cq_count >= 1 + m &&
	    enic->intr_count >= 1 + m + 2) {
		if (!pci_enable_msix(enic->pdev, enic->msix_entry, 1 + m + 2)) {

			enic->rq_count = 1;
			enic->wq_count = m;
			enic->cq_count = 1 + m;
			enic->intr_count = 1 + m + 2;

			vnic_dev_set_intr_mode(enic->vdev,
				VNIC_DEV_INTR_MODE_MSIX);

			return 0;
		}
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

static const struct net_device_ops enic_netdev_dynamic_ops = {
	.ndo_open		= enic_open,
	.ndo_stop		= enic_stop,
	.ndo_start_xmit		= enic_hard_start_xmit,
	.ndo_get_stats		= enic_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= enic_set_rx_mode,
	.ndo_set_multicast_list	= enic_set_rx_mode,
	.ndo_set_mac_address	= enic_set_mac_address_dynamic,
	.ndo_change_mtu		= enic_change_mtu,
	.ndo_vlan_rx_register	= enic_vlan_rx_register,
	.ndo_vlan_rx_add_vid	= enic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= enic_vlan_rx_kill_vid,
	.ndo_tx_timeout		= enic_tx_timeout,
	.ndo_set_vf_port	= enic_set_vf_port,
	.ndo_get_vf_port	= enic_get_vf_port,
	.ndo_set_vf_mac		= enic_set_vf_mac,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= enic_poll_controller,
#endif
};

static const struct net_device_ops enic_netdev_ops = {
	.ndo_open		= enic_open,
	.ndo_stop		= enic_stop,
	.ndo_start_xmit		= enic_hard_start_xmit,
	.ndo_get_stats		= enic_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= enic_set_mac_address,
	.ndo_set_rx_mode	= enic_set_rx_mode,
	.ndo_set_multicast_list	= enic_set_rx_mode,
	.ndo_change_mtu		= enic_change_mtu,
	.ndo_vlan_rx_register	= enic_vlan_rx_register,
	.ndo_vlan_rx_add_vid	= enic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= enic_vlan_rx_kill_vid,
	.ndo_tx_timeout		= enic_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= enic_poll_controller,
#endif
};

static void enic_dev_deinit(struct enic *enic)
{
	unsigned int i;

	for (i = 0; i < enic->rq_count; i++)
		netif_napi_del(&enic->napi[i]);

	enic_free_vnic_resources(enic);
	enic_clear_intr_mode(enic);
}

static int enic_dev_init(struct enic *enic)
{
	struct device *dev = enic_get_dev(enic);
	struct net_device *netdev = enic->netdev;
	unsigned int i;
	int err;

	/* Get vNIC configuration
	 */

	err = enic_get_vnic_config(enic);
	if (err) {
		dev_err(dev, "Get vNIC configuration failed, aborting\n");
		return err;
	}

	/* Get available resource counts
	 */

	enic_get_res_counts(enic);

	/* Set interrupt mode based on resource counts and system
	 * capabilities
	 */

	err = enic_set_intr_mode(enic);
	if (err) {
		dev_err(dev, "Failed to set intr mode based on resource "
			"counts and system capabilities, aborting\n");
		return err;
	}

	/* Allocate and configure vNIC resources
	 */

	err = enic_alloc_vnic_resources(enic);
	if (err) {
		dev_err(dev, "Failed to alloc vNIC resources, aborting\n");
		goto err_out_free_vnic_resources;
	}

	enic_init_vnic_resources(enic);

	err = enic_set_rss_nic_cfg(enic);
	if (err) {
		dev_err(dev, "Failed to config nic, aborting\n");
		goto err_out_free_vnic_resources;
	}

	switch (vnic_dev_get_intr_mode(enic->vdev)) {
	default:
		netif_napi_add(netdev, &enic->napi[0], enic_poll, 64);
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		for (i = 0; i < enic->rq_count; i++)
			netif_napi_add(netdev, &enic->napi[i],
				enic_poll_msix, 64);
		break;
	}

	return 0;

err_out_free_vnic_resources:
	enic_clear_intr_mode(enic);
	enic_free_vnic_resources(enic);

	return err;
}

static void enic_iounmap(struct enic *enic)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(enic->bar); i++)
		if (enic->bar[i].vaddr)
			iounmap(enic->bar[i].vaddr);
}

static int __devinit enic_probe(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
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
		pr_err("Etherdev alloc failed, aborting\n");
		return -ENOMEM;
	}

	pci_set_drvdata(pdev, netdev);

	SET_NETDEV_DEV(netdev, &pdev->dev);

	enic = netdev_priv(netdev);
	enic->netdev = netdev;
	enic->pdev = pdev;

	/* Setup PCI resources
	 */

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(dev, "Cannot enable PCI device, aborting\n");
		goto err_out_free_netdev;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "Cannot request PCI regions, aborting\n");
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
			dev_err(dev, "No usable DMA configuration, aborting\n");
			goto err_out_release_regions;
		}
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(dev, "Unable to obtain %u-bit DMA "
				"for consistent allocations, aborting\n", 32);
			goto err_out_release_regions;
		}
	} else {
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(40));
		if (err) {
			dev_err(dev, "Unable to obtain %u-bit DMA "
				"for consistent allocations, aborting\n", 40);
			goto err_out_release_regions;
		}
		using_dac = 1;
	}

	/* Map vNIC resources from BAR0-5
	 */

	for (i = 0; i < ARRAY_SIZE(enic->bar); i++) {
		if (!(pci_resource_flags(pdev, i) & IORESOURCE_MEM))
			continue;
		enic->bar[i].len = pci_resource_len(pdev, i);
		enic->bar[i].vaddr = pci_iomap(pdev, i, enic->bar[i].len);
		if (!enic->bar[i].vaddr) {
			dev_err(dev, "Cannot memory-map BAR %d, aborting\n", i);
			err = -ENODEV;
			goto err_out_iounmap;
		}
		enic->bar[i].bus_addr = pci_resource_start(pdev, i);
	}

	/* Register vNIC device
	 */

	enic->vdev = vnic_dev_register(NULL, enic, pdev, enic->bar,
		ARRAY_SIZE(enic->bar));
	if (!enic->vdev) {
		dev_err(dev, "vNIC registration failed, aborting\n");
		err = -ENODEV;
		goto err_out_iounmap;
	}

	/* Issue device open to get device in known state
	 */

	err = enic_dev_open(enic);
	if (err) {
		dev_err(dev, "vNIC dev open failed, aborting\n");
		goto err_out_vnic_unregister;
	}

	/* Setup devcmd lock
	 */

	spin_lock_init(&enic->devcmd_lock);

	/*
	 * Set ingress vlan rewrite mode before vnic initialization
	 */

	err = enic_dev_set_ig_vlan_rewrite_mode(enic);
	if (err) {
		dev_err(dev,
			"Failed to set ingress vlan rewrite mode, aborting.\n");
		goto err_out_dev_close;
	}

	/* Issue device init to initialize the vnic-to-switch link.
	 * We'll start with carrier off and wait for link UP
	 * notification later to turn on carrier.  We don't need
	 * to wait here for the vnic-to-switch link initialization
	 * to complete; link UP notification is the indication that
	 * the process is complete.
	 */

	netif_carrier_off(netdev);

	/* Do not call dev_init for a dynamic vnic.
	 * For a dynamic vnic, init_prov_info will be
	 * called later by an upper layer.
	 */

	if (!enic_is_dynamic(enic)) {
		err = vnic_dev_init(enic->vdev, 0);
		if (err) {
			dev_err(dev, "vNIC dev init failed, aborting\n");
			goto err_out_dev_close;
		}
	}

	err = enic_dev_init(enic);
	if (err) {
		dev_err(dev, "Device initialization failed, aborting\n");
		goto err_out_dev_close;
	}

	/* Setup notification timer, HW reset task, and wq locks
	 */

	init_timer(&enic->notify_timer);
	enic->notify_timer.function = enic_notify_timer;
	enic->notify_timer.data = (unsigned long)enic;

	INIT_WORK(&enic->reset, enic_reset);

	for (i = 0; i < enic->wq_count; i++)
		spin_lock_init(&enic->wq_lock[i]);

	/* Register net device
	 */

	enic->port_mtu = enic->config.mtu;
	(void)enic_change_mtu(netdev, enic->port_mtu);

	err = enic_set_mac_addr(netdev, enic->mac_addr);
	if (err) {
		dev_err(dev, "Invalid MAC address, aborting\n");
		goto err_out_dev_deinit;
	}

	enic->tx_coalesce_usecs = enic->config.intr_timer_usec;
	enic->rx_coalesce_usecs = enic->tx_coalesce_usecs;

	if (enic_is_dynamic(enic))
		netdev->netdev_ops = &enic_netdev_dynamic_ops;
	else
		netdev->netdev_ops = &enic_netdev_ops;

	netdev->watchdog_timeo = 2 * HZ;
	netdev->ethtool_ops = &enic_ethtool_ops;

	netdev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
	if (ENIC_SETTING(enic, LOOP)) {
		netdev->features &= ~NETIF_F_HW_VLAN_TX;
		enic->loop_enable = 1;
		enic->loop_tag = enic->config.loop_tag;
		dev_info(dev, "loopback tag=0x%04x\n", enic->loop_tag);
	}
	if (ENIC_SETTING(enic, TXCSUM))
		netdev->hw_features |= NETIF_F_SG | NETIF_F_HW_CSUM;
	if (ENIC_SETTING(enic, TSO))
		netdev->hw_features |= NETIF_F_TSO |
			NETIF_F_TSO6 | NETIF_F_TSO_ECN;
	if (ENIC_SETTING(enic, RXCSUM))
		netdev->hw_features |= NETIF_F_RXCSUM;

	netdev->features |= netdev->hw_features;

	if (using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	err = register_netdev(netdev);
	if (err) {
		dev_err(dev, "Cannot register net device, aborting\n");
		goto err_out_dev_deinit;
	}

	return 0;

err_out_dev_deinit:
	enic_dev_deinit(enic);
err_out_dev_close:
	vnic_dev_close(enic->vdev);
err_out_vnic_unregister:
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

		cancel_work_sync(&enic->reset);
		unregister_netdev(netdev);
		enic_dev_deinit(enic);
		vnic_dev_close(enic->vdev);
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
	pr_info("%s, ver %s\n", DRV_DESCRIPTION, DRV_VERSION);

	return pci_register_driver(&enic_driver);
}

static void __exit enic_cleanup_module(void)
{
	pci_unregister_driver(&enic_driver);
}

module_init(enic_init_module);
module_exit(enic_cleanup_module);
