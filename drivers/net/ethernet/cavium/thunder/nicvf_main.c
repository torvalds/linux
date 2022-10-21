// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Cavium, Inc.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/log2.h>
#include <linux/prefetch.h>
#include <linux/irq.h>
#include <linux/iommu.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/filter.h>
#include <linux/net_tstamp.h>
#include <linux/workqueue.h>

#include "nic_reg.h"
#include "nic.h"
#include "nicvf_queues.h"
#include "thunder_bgx.h"
#include "../common/cavium_ptp.h"

#define DRV_NAME	"nicvf"
#define DRV_VERSION	"1.0"

/* NOTE: Packets bigger than 1530 are split across multiple pages and XDP needs
 * the buffer to be contiguous. Allow XDP to be set up only if we don't exceed
 * this value, keeping headroom for the 14 byte Ethernet header and two
 * VLAN tags (for QinQ)
 */
#define MAX_XDP_MTU	(1530 - ETH_HLEN - VLAN_HLEN * 2)

/* Supported devices */
static const struct pci_device_id nicvf_id_table[] = {
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM,
			 PCI_DEVICE_ID_THUNDER_NIC_VF,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_88XX_NIC_VF) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM,
			 PCI_DEVICE_ID_THUNDER_PASS1_NIC_VF,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_88XX_PASS1_NIC_VF) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM,
			 PCI_DEVICE_ID_THUNDER_NIC_VF,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_81XX_NIC_VF) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_CAVIUM,
			 PCI_DEVICE_ID_THUNDER_NIC_VF,
			 PCI_VENDOR_ID_CAVIUM,
			 PCI_SUBSYS_DEVID_83XX_NIC_VF) },
	{ 0, }  /* end of table */
};

MODULE_AUTHOR("Sunil Goutham");
MODULE_DESCRIPTION("Cavium Thunder NIC Virtual Function Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, nicvf_id_table);

static int debug = 0x00;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug message level bitmap");

static int cpi_alg = CPI_ALG_NONE;
module_param(cpi_alg, int, 0444);
MODULE_PARM_DESC(cpi_alg,
		 "PFC algorithm (0=none, 1=VLAN, 2=VLAN16, 3=IP Diffserv)");

static inline u8 nicvf_netdev_qidx(struct nicvf *nic, u8 qidx)
{
	if (nic->sqs_mode)
		return qidx + ((nic->sqs_id + 1) * MAX_CMP_QUEUES_PER_QS);
	else
		return qidx;
}

/* The Cavium ThunderX network controller can *only* be found in SoCs
 * containing the ThunderX ARM64 CPU implementation.  All accesses to the device
 * registers on this platform are implicitly strongly ordered with respect
 * to memory accesses. So writeq_relaxed() and readq_relaxed() are safe to use
 * with no memory barriers in this driver.  The readq()/writeq() functions add
 * explicit ordering operation which in this case are redundant, and only
 * add overhead.
 */

/* Register read/write APIs */
void nicvf_reg_write(struct nicvf *nic, u64 offset, u64 val)
{
	writeq_relaxed(val, nic->reg_base + offset);
}

u64 nicvf_reg_read(struct nicvf *nic, u64 offset)
{
	return readq_relaxed(nic->reg_base + offset);
}

void nicvf_queue_reg_write(struct nicvf *nic, u64 offset,
			   u64 qidx, u64 val)
{
	void __iomem *addr = nic->reg_base + offset;

	writeq_relaxed(val, addr + (qidx << NIC_Q_NUM_SHIFT));
}

u64 nicvf_queue_reg_read(struct nicvf *nic, u64 offset, u64 qidx)
{
	void __iomem *addr = nic->reg_base + offset;

	return readq_relaxed(addr + (qidx << NIC_Q_NUM_SHIFT));
}

/* VF -> PF mailbox communication */
static void nicvf_write_to_mbx(struct nicvf *nic, union nic_mbx *mbx)
{
	u64 *msg = (u64 *)mbx;

	nicvf_reg_write(nic, NIC_VF_PF_MAILBOX_0_1 + 0, msg[0]);
	nicvf_reg_write(nic, NIC_VF_PF_MAILBOX_0_1 + 8, msg[1]);
}

int nicvf_send_msg_to_pf(struct nicvf *nic, union nic_mbx *mbx)
{
	unsigned long timeout;
	int ret = 0;

	mutex_lock(&nic->rx_mode_mtx);

	nic->pf_acked = false;
	nic->pf_nacked = false;

	nicvf_write_to_mbx(nic, mbx);

	timeout = jiffies + msecs_to_jiffies(NIC_MBOX_MSG_TIMEOUT);
	/* Wait for previous message to be acked, timeout 2sec */
	while (!nic->pf_acked) {
		if (nic->pf_nacked) {
			netdev_err(nic->netdev,
				   "PF NACK to mbox msg 0x%02x from VF%d\n",
				   (mbx->msg.msg & 0xFF), nic->vf_id);
			ret = -EINVAL;
			break;
		}
		usleep_range(8000, 10000);
		if (nic->pf_acked)
			break;
		if (time_after(jiffies, timeout)) {
			netdev_err(nic->netdev,
				   "PF didn't ACK to mbox msg 0x%02x from VF%d\n",
				   (mbx->msg.msg & 0xFF), nic->vf_id);
			ret = -EBUSY;
			break;
		}
	}
	mutex_unlock(&nic->rx_mode_mtx);
	return ret;
}

/* Checks if VF is able to comminicate with PF
* and also gets the VNIC number this VF is associated to.
*/
static int nicvf_check_pf_ready(struct nicvf *nic)
{
	union nic_mbx mbx = {};

	mbx.msg.msg = NIC_MBOX_MSG_READY;
	if (nicvf_send_msg_to_pf(nic, &mbx)) {
		netdev_err(nic->netdev,
			   "PF didn't respond to READY msg\n");
		return 0;
	}

	return 1;
}

static void nicvf_send_cfg_done(struct nicvf *nic)
{
	union nic_mbx mbx = {};

	mbx.msg.msg = NIC_MBOX_MSG_CFG_DONE;
	if (nicvf_send_msg_to_pf(nic, &mbx)) {
		netdev_err(nic->netdev,
			   "PF didn't respond to CFG DONE msg\n");
	}
}

static void nicvf_read_bgx_stats(struct nicvf *nic, struct bgx_stats_msg *bgx)
{
	if (bgx->rx)
		nic->bgx_stats.rx_stats[bgx->idx] = bgx->stats;
	else
		nic->bgx_stats.tx_stats[bgx->idx] = bgx->stats;
}

static void  nicvf_handle_mbx_intr(struct nicvf *nic)
{
	union nic_mbx mbx = {};
	u64 *mbx_data;
	u64 mbx_addr;
	int i;

	mbx_addr = NIC_VF_PF_MAILBOX_0_1;
	mbx_data = (u64 *)&mbx;

	for (i = 0; i < NIC_PF_VF_MAILBOX_SIZE; i++) {
		*mbx_data = nicvf_reg_read(nic, mbx_addr);
		mbx_data++;
		mbx_addr += sizeof(u64);
	}

	netdev_dbg(nic->netdev, "Mbox message: msg: 0x%x\n", mbx.msg.msg);
	switch (mbx.msg.msg) {
	case NIC_MBOX_MSG_READY:
		nic->pf_acked = true;
		nic->vf_id = mbx.nic_cfg.vf_id & 0x7F;
		nic->tns_mode = mbx.nic_cfg.tns_mode & 0x7F;
		nic->node = mbx.nic_cfg.node_id;
		if (!nic->set_mac_pending)
			eth_hw_addr_set(nic->netdev, mbx.nic_cfg.mac_addr);
		nic->sqs_mode = mbx.nic_cfg.sqs_mode;
		nic->loopback_supported = mbx.nic_cfg.loopback_supported;
		nic->link_up = false;
		nic->duplex = 0;
		nic->speed = 0;
		break;
	case NIC_MBOX_MSG_ACK:
		nic->pf_acked = true;
		break;
	case NIC_MBOX_MSG_NACK:
		nic->pf_nacked = true;
		break;
	case NIC_MBOX_MSG_RSS_SIZE:
		nic->rss_info.rss_size = mbx.rss_size.ind_tbl_size;
		nic->pf_acked = true;
		break;
	case NIC_MBOX_MSG_BGX_STATS:
		nicvf_read_bgx_stats(nic, &mbx.bgx_stats);
		nic->pf_acked = true;
		break;
	case NIC_MBOX_MSG_BGX_LINK_CHANGE:
		nic->pf_acked = true;
		if (nic->link_up != mbx.link_status.link_up) {
			nic->link_up = mbx.link_status.link_up;
			nic->duplex = mbx.link_status.duplex;
			nic->speed = mbx.link_status.speed;
			nic->mac_type = mbx.link_status.mac_type;
			if (nic->link_up) {
				netdev_info(nic->netdev,
					    "Link is Up %d Mbps %s duplex\n",
					    nic->speed,
					    nic->duplex == DUPLEX_FULL ?
					    "Full" : "Half");
				netif_carrier_on(nic->netdev);
				netif_tx_start_all_queues(nic->netdev);
			} else {
				netdev_info(nic->netdev, "Link is Down\n");
				netif_carrier_off(nic->netdev);
				netif_tx_stop_all_queues(nic->netdev);
			}
		}
		break;
	case NIC_MBOX_MSG_ALLOC_SQS:
		nic->sqs_count = mbx.sqs_alloc.qs_count;
		nic->pf_acked = true;
		break;
	case NIC_MBOX_MSG_SNICVF_PTR:
		/* Primary VF: make note of secondary VF's pointer
		 * to be used while packet transmission.
		 */
		nic->snicvf[mbx.nicvf.sqs_id] =
			(struct nicvf *)mbx.nicvf.nicvf;
		nic->pf_acked = true;
		break;
	case NIC_MBOX_MSG_PNICVF_PTR:
		/* Secondary VF/Qset: make note of primary VF's pointer
		 * to be used while packet reception, to handover packet
		 * to primary VF's netdev.
		 */
		nic->pnicvf = (struct nicvf *)mbx.nicvf.nicvf;
		nic->pf_acked = true;
		break;
	case NIC_MBOX_MSG_PFC:
		nic->pfc.autoneg = mbx.pfc.autoneg;
		nic->pfc.fc_rx = mbx.pfc.fc_rx;
		nic->pfc.fc_tx = mbx.pfc.fc_tx;
		nic->pf_acked = true;
		break;
	default:
		netdev_err(nic->netdev,
			   "Invalid message from PF, msg 0x%x\n", mbx.msg.msg);
		break;
	}
	nicvf_clear_intr(nic, NICVF_INTR_MBOX, 0);
}

static int nicvf_hw_set_mac_addr(struct nicvf *nic, struct net_device *netdev)
{
	union nic_mbx mbx = {};

	mbx.mac.msg = NIC_MBOX_MSG_SET_MAC;
	mbx.mac.vf_id = nic->vf_id;
	ether_addr_copy(mbx.mac.mac_addr, netdev->dev_addr);

	return nicvf_send_msg_to_pf(nic, &mbx);
}

static void nicvf_config_cpi(struct nicvf *nic)
{
	union nic_mbx mbx = {};

	mbx.cpi_cfg.msg = NIC_MBOX_MSG_CPI_CFG;
	mbx.cpi_cfg.vf_id = nic->vf_id;
	mbx.cpi_cfg.cpi_alg = nic->cpi_alg;
	mbx.cpi_cfg.rq_cnt = nic->qs->rq_cnt;

	nicvf_send_msg_to_pf(nic, &mbx);
}

static void nicvf_get_rss_size(struct nicvf *nic)
{
	union nic_mbx mbx = {};

	mbx.rss_size.msg = NIC_MBOX_MSG_RSS_SIZE;
	mbx.rss_size.vf_id = nic->vf_id;
	nicvf_send_msg_to_pf(nic, &mbx);
}

void nicvf_config_rss(struct nicvf *nic)
{
	union nic_mbx mbx = {};
	struct nicvf_rss_info *rss = &nic->rss_info;
	int ind_tbl_len = rss->rss_size;
	int i, nextq = 0;

	mbx.rss_cfg.vf_id = nic->vf_id;
	mbx.rss_cfg.hash_bits = rss->hash_bits;
	while (ind_tbl_len) {
		mbx.rss_cfg.tbl_offset = nextq;
		mbx.rss_cfg.tbl_len = min(ind_tbl_len,
					       RSS_IND_TBL_LEN_PER_MBX_MSG);
		mbx.rss_cfg.msg = mbx.rss_cfg.tbl_offset ?
			  NIC_MBOX_MSG_RSS_CFG_CONT : NIC_MBOX_MSG_RSS_CFG;

		for (i = 0; i < mbx.rss_cfg.tbl_len; i++)
			mbx.rss_cfg.ind_tbl[i] = rss->ind_tbl[nextq++];

		nicvf_send_msg_to_pf(nic, &mbx);

		ind_tbl_len -= mbx.rss_cfg.tbl_len;
	}
}

void nicvf_set_rss_key(struct nicvf *nic)
{
	struct nicvf_rss_info *rss = &nic->rss_info;
	u64 key_addr = NIC_VNIC_RSS_KEY_0_4;
	int idx;

	for (idx = 0; idx < RSS_HASH_KEY_SIZE; idx++) {
		nicvf_reg_write(nic, key_addr, rss->key[idx]);
		key_addr += sizeof(u64);
	}
}

static int nicvf_rss_init(struct nicvf *nic)
{
	struct nicvf_rss_info *rss = &nic->rss_info;
	int idx;

	nicvf_get_rss_size(nic);

	if (cpi_alg != CPI_ALG_NONE) {
		rss->enable = false;
		rss->hash_bits = 0;
		return 0;
	}

	rss->enable = true;

	netdev_rss_key_fill(rss->key, RSS_HASH_KEY_SIZE * sizeof(u64));
	nicvf_set_rss_key(nic);

	rss->cfg = RSS_IP_HASH_ENA | RSS_TCP_HASH_ENA | RSS_UDP_HASH_ENA;
	nicvf_reg_write(nic, NIC_VNIC_RSS_CFG, rss->cfg);

	rss->hash_bits =  ilog2(rounddown_pow_of_two(rss->rss_size));

	for (idx = 0; idx < rss->rss_size; idx++)
		rss->ind_tbl[idx] = ethtool_rxfh_indir_default(idx,
							       nic->rx_queues);
	nicvf_config_rss(nic);
	return 1;
}

/* Request PF to allocate additional Qsets */
static void nicvf_request_sqs(struct nicvf *nic)
{
	union nic_mbx mbx = {};
	int sqs;
	int sqs_count = nic->sqs_count;
	int rx_queues = 0, tx_queues = 0;

	/* Only primary VF should request */
	if (nic->sqs_mode ||  !nic->sqs_count)
		return;

	mbx.sqs_alloc.msg = NIC_MBOX_MSG_ALLOC_SQS;
	mbx.sqs_alloc.vf_id = nic->vf_id;
	mbx.sqs_alloc.qs_count = nic->sqs_count;
	if (nicvf_send_msg_to_pf(nic, &mbx)) {
		/* No response from PF */
		nic->sqs_count = 0;
		return;
	}

	/* Return if no Secondary Qsets available */
	if (!nic->sqs_count)
		return;

	if (nic->rx_queues > MAX_RCV_QUEUES_PER_QS)
		rx_queues = nic->rx_queues - MAX_RCV_QUEUES_PER_QS;

	tx_queues = nic->tx_queues + nic->xdp_tx_queues;
	if (tx_queues > MAX_SND_QUEUES_PER_QS)
		tx_queues = tx_queues - MAX_SND_QUEUES_PER_QS;

	/* Set no of Rx/Tx queues in each of the SQsets */
	for (sqs = 0; sqs < nic->sqs_count; sqs++) {
		mbx.nicvf.msg = NIC_MBOX_MSG_SNICVF_PTR;
		mbx.nicvf.vf_id = nic->vf_id;
		mbx.nicvf.sqs_id = sqs;
		nicvf_send_msg_to_pf(nic, &mbx);

		nic->snicvf[sqs]->sqs_id = sqs;
		if (rx_queues > MAX_RCV_QUEUES_PER_QS) {
			nic->snicvf[sqs]->qs->rq_cnt = MAX_RCV_QUEUES_PER_QS;
			rx_queues -= MAX_RCV_QUEUES_PER_QS;
		} else {
			nic->snicvf[sqs]->qs->rq_cnt = rx_queues;
			rx_queues = 0;
		}

		if (tx_queues > MAX_SND_QUEUES_PER_QS) {
			nic->snicvf[sqs]->qs->sq_cnt = MAX_SND_QUEUES_PER_QS;
			tx_queues -= MAX_SND_QUEUES_PER_QS;
		} else {
			nic->snicvf[sqs]->qs->sq_cnt = tx_queues;
			tx_queues = 0;
		}

		nic->snicvf[sqs]->qs->cq_cnt =
		max(nic->snicvf[sqs]->qs->rq_cnt, nic->snicvf[sqs]->qs->sq_cnt);

		/* Initialize secondary Qset's queues and its interrupts */
		nicvf_open(nic->snicvf[sqs]->netdev);
	}

	/* Update stack with actual Rx/Tx queue count allocated */
	if (sqs_count != nic->sqs_count)
		nicvf_set_real_num_queues(nic->netdev,
					  nic->tx_queues, nic->rx_queues);
}

/* Send this Qset's nicvf pointer to PF.
 * PF inturn sends primary VF's nicvf struct to secondary Qsets/VFs
 * so that packets received by these Qsets can use primary VF's netdev
 */
static void nicvf_send_vf_struct(struct nicvf *nic)
{
	union nic_mbx mbx = {};

	mbx.nicvf.msg = NIC_MBOX_MSG_NICVF_PTR;
	mbx.nicvf.sqs_mode = nic->sqs_mode;
	mbx.nicvf.nicvf = (u64)nic;
	nicvf_send_msg_to_pf(nic, &mbx);
}

static void nicvf_get_primary_vf_struct(struct nicvf *nic)
{
	union nic_mbx mbx = {};

	mbx.nicvf.msg = NIC_MBOX_MSG_PNICVF_PTR;
	nicvf_send_msg_to_pf(nic, &mbx);
}

int nicvf_set_real_num_queues(struct net_device *netdev,
			      int tx_queues, int rx_queues)
{
	int err = 0;

	err = netif_set_real_num_tx_queues(netdev, tx_queues);
	if (err) {
		netdev_err(netdev,
			   "Failed to set no of Tx queues: %d\n", tx_queues);
		return err;
	}

	err = netif_set_real_num_rx_queues(netdev, rx_queues);
	if (err)
		netdev_err(netdev,
			   "Failed to set no of Rx queues: %d\n", rx_queues);
	return err;
}

static int nicvf_init_resources(struct nicvf *nic)
{
	int err;

	/* Enable Qset */
	nicvf_qset_config(nic, true);

	/* Initialize queues and HW for data transfer */
	err = nicvf_config_data_transfer(nic, true);
	if (err) {
		netdev_err(nic->netdev,
			   "Failed to alloc/config VF's QSet resources\n");
		return err;
	}

	return 0;
}

static inline bool nicvf_xdp_rx(struct nicvf *nic, struct bpf_prog *prog,
				struct cqe_rx_t *cqe_rx, struct snd_queue *sq,
				struct rcv_queue *rq, struct sk_buff **skb)
{
	unsigned char *hard_start, *data;
	struct xdp_buff xdp;
	struct page *page;
	u32 action;
	u16 len, offset = 0;
	u64 dma_addr, cpu_addr;
	void *orig_data;

	/* Retrieve packet buffer's DMA address and length */
	len = *((u16 *)((void *)cqe_rx + (3 * sizeof(u64))));
	dma_addr = *((u64 *)((void *)cqe_rx + (7 * sizeof(u64))));

	cpu_addr = nicvf_iova_to_phys(nic, dma_addr);
	if (!cpu_addr)
		return false;
	cpu_addr = (u64)phys_to_virt(cpu_addr);
	page = virt_to_page((void *)cpu_addr);

	xdp_init_buff(&xdp, RCV_FRAG_LEN + XDP_PACKET_HEADROOM,
		      &rq->xdp_rxq);
	hard_start = page_address(page);
	data = (unsigned char *)cpu_addr;
	xdp_prepare_buff(&xdp, hard_start, data - hard_start, len, false);
	orig_data = xdp.data;

	action = bpf_prog_run_xdp(prog, &xdp);

	len = xdp.data_end - xdp.data;
	/* Check if XDP program has changed headers */
	if (orig_data != xdp.data) {
		offset = orig_data - xdp.data;
		dma_addr -= offset;
	}

	switch (action) {
	case XDP_PASS:
		/* Check if it's a recycled page, if not
		 * unmap the DMA mapping.
		 *
		 * Recycled page holds an extra reference.
		 */
		if (page_ref_count(page) == 1) {
			dma_addr &= PAGE_MASK;
			dma_unmap_page_attrs(&nic->pdev->dev, dma_addr,
					     RCV_FRAG_LEN + XDP_PACKET_HEADROOM,
					     DMA_FROM_DEVICE,
					     DMA_ATTR_SKIP_CPU_SYNC);
		}

		/* Build SKB and pass on packet to network stack */
		*skb = build_skb(xdp.data,
				 RCV_FRAG_LEN - cqe_rx->align_pad + offset);
		if (!*skb)
			put_page(page);
		else
			skb_put(*skb, len);
		return false;
	case XDP_TX:
		nicvf_xdp_sq_append_pkt(nic, sq, (u64)xdp.data, dma_addr, len);
		return true;
	default:
		bpf_warn_invalid_xdp_action(nic->netdev, prog, action);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(nic->netdev, prog, action);
		fallthrough;
	case XDP_DROP:
		/* Check if it's a recycled page, if not
		 * unmap the DMA mapping.
		 *
		 * Recycled page holds an extra reference.
		 */
		if (page_ref_count(page) == 1) {
			dma_addr &= PAGE_MASK;
			dma_unmap_page_attrs(&nic->pdev->dev, dma_addr,
					     RCV_FRAG_LEN + XDP_PACKET_HEADROOM,
					     DMA_FROM_DEVICE,
					     DMA_ATTR_SKIP_CPU_SYNC);
		}
		put_page(page);
		return true;
	}
	return false;
}

static void nicvf_snd_ptp_handler(struct net_device *netdev,
				  struct cqe_send_t *cqe_tx)
{
	struct nicvf *nic = netdev_priv(netdev);
	struct skb_shared_hwtstamps ts;
	u64 ns;

	nic = nic->pnicvf;

	/* Sync for 'ptp_skb' */
	smp_rmb();

	/* New timestamp request can be queued now */
	atomic_set(&nic->tx_ptp_skbs, 0);

	/* Check for timestamp requested skb */
	if (!nic->ptp_skb)
		return;

	/* Check if timestamping is timedout, which is set to 10us */
	if (cqe_tx->send_status == CQ_TX_ERROP_TSTMP_TIMEOUT ||
	    cqe_tx->send_status == CQ_TX_ERROP_TSTMP_CONFLICT)
		goto no_tstamp;

	/* Get the timestamp */
	memset(&ts, 0, sizeof(ts));
	ns = cavium_ptp_tstamp2time(nic->ptp_clock, cqe_tx->ptp_timestamp);
	ts.hwtstamp = ns_to_ktime(ns);
	skb_tstamp_tx(nic->ptp_skb, &ts);

no_tstamp:
	/* Free the original skb */
	dev_kfree_skb_any(nic->ptp_skb);
	nic->ptp_skb = NULL;
	/* Sync 'ptp_skb' */
	smp_wmb();
}

static void nicvf_snd_pkt_handler(struct net_device *netdev,
				  struct cqe_send_t *cqe_tx,
				  int budget, int *subdesc_cnt,
				  unsigned int *tx_pkts, unsigned int *tx_bytes)
{
	struct sk_buff *skb = NULL;
	struct page *page;
	struct nicvf *nic = netdev_priv(netdev);
	struct snd_queue *sq;
	struct sq_hdr_subdesc *hdr;
	struct sq_hdr_subdesc *tso_sqe;

	sq = &nic->qs->sq[cqe_tx->sq_idx];

	hdr = (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, cqe_tx->sqe_ptr);
	if (hdr->subdesc_type != SQ_DESC_TYPE_HEADER)
		return;

	/* Check for errors */
	if (cqe_tx->send_status)
		nicvf_check_cqe_tx_errs(nic->pnicvf, cqe_tx);

	/* Is this a XDP designated Tx queue */
	if (sq->is_xdp) {
		page = (struct page *)sq->xdp_page[cqe_tx->sqe_ptr];
		/* Check if it's recycled page or else unmap DMA mapping */
		if (page && (page_ref_count(page) == 1))
			nicvf_unmap_sndq_buffers(nic, sq, cqe_tx->sqe_ptr,
						 hdr->subdesc_cnt);

		/* Release page reference for recycling */
		if (page)
			put_page(page);
		sq->xdp_page[cqe_tx->sqe_ptr] = (u64)NULL;
		*subdesc_cnt += hdr->subdesc_cnt + 1;
		return;
	}

	skb = (struct sk_buff *)sq->skbuff[cqe_tx->sqe_ptr];
	if (skb) {
		/* Check for dummy descriptor used for HW TSO offload on 88xx */
		if (hdr->dont_send) {
			/* Get actual TSO descriptors and free them */
			tso_sqe =
			 (struct sq_hdr_subdesc *)GET_SQ_DESC(sq, hdr->rsvd2);
			nicvf_unmap_sndq_buffers(nic, sq, hdr->rsvd2,
						 tso_sqe->subdesc_cnt);
			*subdesc_cnt += tso_sqe->subdesc_cnt + 1;
		} else {
			nicvf_unmap_sndq_buffers(nic, sq, cqe_tx->sqe_ptr,
						 hdr->subdesc_cnt);
		}
		*subdesc_cnt += hdr->subdesc_cnt + 1;
		prefetch(skb);
		(*tx_pkts)++;
		*tx_bytes += skb->len;
		/* If timestamp is requested for this skb, don't free it */
		if (skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS &&
		    !nic->pnicvf->ptp_skb)
			nic->pnicvf->ptp_skb = skb;
		else
			napi_consume_skb(skb, budget);
		sq->skbuff[cqe_tx->sqe_ptr] = (u64)NULL;
	} else {
		/* In case of SW TSO on 88xx, only last segment will have
		 * a SKB attached, so just free SQEs here.
		 */
		if (!nic->hw_tso)
			*subdesc_cnt += hdr->subdesc_cnt + 1;
	}
}

static inline void nicvf_set_rxhash(struct net_device *netdev,
				    struct cqe_rx_t *cqe_rx,
				    struct sk_buff *skb)
{
	u8 hash_type;
	u32 hash;

	if (!(netdev->features & NETIF_F_RXHASH))
		return;

	switch (cqe_rx->rss_alg) {
	case RSS_ALG_TCP_IP:
	case RSS_ALG_UDP_IP:
		hash_type = PKT_HASH_TYPE_L4;
		hash = cqe_rx->rss_tag;
		break;
	case RSS_ALG_IP:
		hash_type = PKT_HASH_TYPE_L3;
		hash = cqe_rx->rss_tag;
		break;
	default:
		hash_type = PKT_HASH_TYPE_NONE;
		hash = 0;
	}

	skb_set_hash(skb, hash, hash_type);
}

static inline void nicvf_set_rxtstamp(struct nicvf *nic, struct sk_buff *skb)
{
	u64 ns;

	if (!nic->ptp_clock || !nic->hw_rx_tstamp)
		return;

	/* The first 8 bytes is the timestamp */
	ns = cavium_ptp_tstamp2time(nic->ptp_clock,
				    be64_to_cpu(*(__be64 *)skb->data));
	skb_hwtstamps(skb)->hwtstamp = ns_to_ktime(ns);

	__skb_pull(skb, 8);
}

static void nicvf_rcv_pkt_handler(struct net_device *netdev,
				  struct napi_struct *napi,
				  struct cqe_rx_t *cqe_rx,
				  struct snd_queue *sq, struct rcv_queue *rq)
{
	struct sk_buff *skb = NULL;
	struct nicvf *nic = netdev_priv(netdev);
	struct nicvf *snic = nic;
	int err = 0;
	int rq_idx;

	rq_idx = nicvf_netdev_qidx(nic, cqe_rx->rq_idx);

	if (nic->sqs_mode) {
		/* Use primary VF's 'nicvf' struct */
		nic = nic->pnicvf;
		netdev = nic->netdev;
	}

	/* Check for errors */
	if (cqe_rx->err_level || cqe_rx->err_opcode) {
		err = nicvf_check_cqe_rx_errs(nic, cqe_rx);
		if (err && !cqe_rx->rb_cnt)
			return;
	}

	/* For XDP, ignore pkts spanning multiple pages */
	if (nic->xdp_prog && (cqe_rx->rb_cnt == 1)) {
		/* Packet consumed by XDP */
		if (nicvf_xdp_rx(snic, nic->xdp_prog, cqe_rx, sq, rq, &skb))
			return;
	} else {
		skb = nicvf_get_rcv_skb(snic, cqe_rx,
					nic->xdp_prog ? true : false);
	}

	if (!skb)
		return;

	if (netif_msg_pktdata(nic)) {
		netdev_info(nic->netdev, "skb 0x%p, len=%d\n", skb, skb->len);
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 1,
			       skb->data, skb->len, true);
	}

	/* If error packet, drop it here */
	if (err) {
		dev_kfree_skb_any(skb);
		return;
	}

	nicvf_set_rxtstamp(nic, skb);
	nicvf_set_rxhash(netdev, cqe_rx, skb);

	skb_record_rx_queue(skb, rq_idx);
	if (netdev->hw_features & NETIF_F_RXCSUM) {
		/* HW by default verifies TCP/UDP/SCTP checksums */
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else {
		skb_checksum_none_assert(skb);
	}

	skb->protocol = eth_type_trans(skb, netdev);

	/* Check for stripped VLAN */
	if (cqe_rx->vlan_found && cqe_rx->vlan_stripped)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       ntohs((__force __be16)cqe_rx->vlan_tci));

	if (napi && (netdev->features & NETIF_F_GRO))
		napi_gro_receive(napi, skb);
	else
		netif_receive_skb(skb);
}

static int nicvf_cq_intr_handler(struct net_device *netdev, u8 cq_idx,
				 struct napi_struct *napi, int budget)
{
	int processed_cqe, work_done = 0, tx_done = 0;
	int cqe_count, cqe_head;
	int subdesc_cnt = 0;
	struct nicvf *nic = netdev_priv(netdev);
	struct queue_set *qs = nic->qs;
	struct cmp_queue *cq = &qs->cq[cq_idx];
	struct cqe_rx_t *cq_desc;
	struct netdev_queue *txq;
	struct snd_queue *sq = &qs->sq[cq_idx];
	struct rcv_queue *rq = &qs->rq[cq_idx];
	unsigned int tx_pkts = 0, tx_bytes = 0, txq_idx;

	spin_lock_bh(&cq->lock);
loop:
	processed_cqe = 0;
	/* Get no of valid CQ entries to process */
	cqe_count = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_STATUS, cq_idx);
	cqe_count &= CQ_CQE_COUNT;
	if (!cqe_count)
		goto done;

	/* Get head of the valid CQ entries */
	cqe_head = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_HEAD, cq_idx) >> 9;
	cqe_head &= 0xFFFF;

	while (processed_cqe < cqe_count) {
		/* Get the CQ descriptor */
		cq_desc = (struct cqe_rx_t *)GET_CQ_DESC(cq, cqe_head);
		cqe_head++;
		cqe_head &= (cq->dmem.q_len - 1);
		/* Initiate prefetch for next descriptor */
		prefetch((struct cqe_rx_t *)GET_CQ_DESC(cq, cqe_head));

		if ((work_done >= budget) && napi &&
		    (cq_desc->cqe_type != CQE_TYPE_SEND)) {
			break;
		}

		switch (cq_desc->cqe_type) {
		case CQE_TYPE_RX:
			nicvf_rcv_pkt_handler(netdev, napi, cq_desc, sq, rq);
			work_done++;
		break;
		case CQE_TYPE_SEND:
			nicvf_snd_pkt_handler(netdev, (void *)cq_desc,
					      budget, &subdesc_cnt,
					      &tx_pkts, &tx_bytes);
			tx_done++;
		break;
		case CQE_TYPE_SEND_PTP:
			nicvf_snd_ptp_handler(netdev, (void *)cq_desc);
		break;
		case CQE_TYPE_INVALID:
		case CQE_TYPE_RX_SPLIT:
		case CQE_TYPE_RX_TCP:
			/* Ignore for now */
		break;
		}
		processed_cqe++;
	}

	/* Ring doorbell to inform H/W to reuse processed CQEs */
	nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_DOOR,
			      cq_idx, processed_cqe);

	if ((work_done < budget) && napi)
		goto loop;

done:
	/* Update SQ's descriptor free count */
	if (subdesc_cnt)
		nicvf_put_sq_desc(sq, subdesc_cnt);

	txq_idx = nicvf_netdev_qidx(nic, cq_idx);
	/* Handle XDP TX queues */
	if (nic->pnicvf->xdp_prog) {
		if (txq_idx < nic->pnicvf->xdp_tx_queues) {
			nicvf_xdp_sq_doorbell(nic, sq, cq_idx);
			goto out;
		}
		nic = nic->pnicvf;
		txq_idx -= nic->pnicvf->xdp_tx_queues;
	}

	/* Wakeup TXQ if its stopped earlier due to SQ full */
	if (tx_done ||
	    (atomic_read(&sq->free_cnt) >= MIN_SQ_DESC_PER_PKT_XMIT)) {
		netdev = nic->pnicvf->netdev;
		txq = netdev_get_tx_queue(netdev, txq_idx);
		if (tx_pkts)
			netdev_tx_completed_queue(txq, tx_pkts, tx_bytes);

		/* To read updated queue and carrier status */
		smp_mb();
		if (netif_tx_queue_stopped(txq) && netif_carrier_ok(netdev)) {
			netif_tx_wake_queue(txq);
			nic = nic->pnicvf;
			this_cpu_inc(nic->drv_stats->txq_wake);
			netif_warn(nic, tx_err, netdev,
				   "Transmit queue wakeup SQ%d\n", txq_idx);
		}
	}

out:
	spin_unlock_bh(&cq->lock);
	return work_done;
}

static int nicvf_poll(struct napi_struct *napi, int budget)
{
	u64  cq_head;
	int  work_done = 0;
	struct net_device *netdev = napi->dev;
	struct nicvf *nic = netdev_priv(netdev);
	struct nicvf_cq_poll *cq;

	cq = container_of(napi, struct nicvf_cq_poll, napi);
	work_done = nicvf_cq_intr_handler(netdev, cq->cq_idx, napi, budget);

	if (work_done < budget) {
		/* Slow packet rate, exit polling */
		napi_complete_done(napi, work_done);
		/* Re-enable interrupts */
		cq_head = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_HEAD,
					       cq->cq_idx);
		nicvf_clear_intr(nic, NICVF_INTR_CQ, cq->cq_idx);
		nicvf_queue_reg_write(nic, NIC_QSET_CQ_0_7_HEAD,
				      cq->cq_idx, cq_head);
		nicvf_enable_intr(nic, NICVF_INTR_CQ, cq->cq_idx);
	}
	return work_done;
}

/* Qset error interrupt handler
 *
 * As of now only CQ errors are handled
 */
static void nicvf_handle_qs_err(struct tasklet_struct *t)
{
	struct nicvf *nic = from_tasklet(nic, t, qs_err_task);
	struct queue_set *qs = nic->qs;
	int qidx;
	u64 status;

	netif_tx_disable(nic->netdev);

	/* Check if it is CQ err */
	for (qidx = 0; qidx < qs->cq_cnt; qidx++) {
		status = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_STATUS,
					      qidx);
		if (!(status & CQ_ERR_MASK))
			continue;
		/* Process already queued CQEs and reconfig CQ */
		nicvf_disable_intr(nic, NICVF_INTR_CQ, qidx);
		nicvf_sq_disable(nic, qidx);
		nicvf_cq_intr_handler(nic->netdev, qidx, NULL, 0);
		nicvf_cmp_queue_config(nic, qs, qidx, true);
		nicvf_sq_free_used_descs(nic->netdev, &qs->sq[qidx], qidx);
		nicvf_sq_enable(nic, &qs->sq[qidx], qidx);

		nicvf_enable_intr(nic, NICVF_INTR_CQ, qidx);
	}

	netif_tx_start_all_queues(nic->netdev);
	/* Re-enable Qset error interrupt */
	nicvf_enable_intr(nic, NICVF_INTR_QS_ERR, 0);
}

static void nicvf_dump_intr_status(struct nicvf *nic)
{
	netif_info(nic, intr, nic->netdev, "interrupt status 0x%llx\n",
		   nicvf_reg_read(nic, NIC_VF_INT));
}

static irqreturn_t nicvf_misc_intr_handler(int irq, void *nicvf_irq)
{
	struct nicvf *nic = (struct nicvf *)nicvf_irq;
	u64 intr;

	nicvf_dump_intr_status(nic);

	intr = nicvf_reg_read(nic, NIC_VF_INT);
	/* Check for spurious interrupt */
	if (!(intr & NICVF_INTR_MBOX_MASK))
		return IRQ_HANDLED;

	nicvf_handle_mbx_intr(nic);

	return IRQ_HANDLED;
}

static irqreturn_t nicvf_intr_handler(int irq, void *cq_irq)
{
	struct nicvf_cq_poll *cq_poll = (struct nicvf_cq_poll *)cq_irq;
	struct nicvf *nic = cq_poll->nicvf;
	int qidx = cq_poll->cq_idx;

	nicvf_dump_intr_status(nic);

	/* Disable interrupts */
	nicvf_disable_intr(nic, NICVF_INTR_CQ, qidx);

	/* Schedule NAPI */
	napi_schedule_irqoff(&cq_poll->napi);

	/* Clear interrupt */
	nicvf_clear_intr(nic, NICVF_INTR_CQ, qidx);

	return IRQ_HANDLED;
}

static irqreturn_t nicvf_rbdr_intr_handler(int irq, void *nicvf_irq)
{
	struct nicvf *nic = (struct nicvf *)nicvf_irq;
	u8 qidx;


	nicvf_dump_intr_status(nic);

	/* Disable RBDR interrupt and schedule softirq */
	for (qidx = 0; qidx < nic->qs->rbdr_cnt; qidx++) {
		if (!nicvf_is_intr_enabled(nic, NICVF_INTR_RBDR, qidx))
			continue;
		nicvf_disable_intr(nic, NICVF_INTR_RBDR, qidx);
		tasklet_hi_schedule(&nic->rbdr_task);
		/* Clear interrupt */
		nicvf_clear_intr(nic, NICVF_INTR_RBDR, qidx);
	}

	return IRQ_HANDLED;
}

static irqreturn_t nicvf_qs_err_intr_handler(int irq, void *nicvf_irq)
{
	struct nicvf *nic = (struct nicvf *)nicvf_irq;

	nicvf_dump_intr_status(nic);

	/* Disable Qset err interrupt and schedule softirq */
	nicvf_disable_intr(nic, NICVF_INTR_QS_ERR, 0);
	tasklet_hi_schedule(&nic->qs_err_task);
	nicvf_clear_intr(nic, NICVF_INTR_QS_ERR, 0);

	return IRQ_HANDLED;
}

static void nicvf_set_irq_affinity(struct nicvf *nic)
{
	int vec, cpu;

	for (vec = 0; vec < nic->num_vec; vec++) {
		if (!nic->irq_allocated[vec])
			continue;

		if (!zalloc_cpumask_var(&nic->affinity_mask[vec], GFP_KERNEL))
			return;
		 /* CQ interrupts */
		if (vec < NICVF_INTR_ID_SQ)
			/* Leave CPU0 for RBDR and other interrupts */
			cpu = nicvf_netdev_qidx(nic, vec) + 1;
		else
			cpu = 0;

		cpumask_set_cpu(cpumask_local_spread(cpu, nic->node),
				nic->affinity_mask[vec]);
		irq_set_affinity_hint(pci_irq_vector(nic->pdev, vec),
				      nic->affinity_mask[vec]);
	}
}

static int nicvf_register_interrupts(struct nicvf *nic)
{
	int irq, ret = 0;

	for_each_cq_irq(irq)
		sprintf(nic->irq_name[irq], "%s-rxtx-%d",
			nic->pnicvf->netdev->name,
			nicvf_netdev_qidx(nic, irq));

	for_each_sq_irq(irq)
		sprintf(nic->irq_name[irq], "%s-sq-%d",
			nic->pnicvf->netdev->name,
			nicvf_netdev_qidx(nic, irq - NICVF_INTR_ID_SQ));

	for_each_rbdr_irq(irq)
		sprintf(nic->irq_name[irq], "%s-rbdr-%d",
			nic->pnicvf->netdev->name,
			nic->sqs_mode ? (nic->sqs_id + 1) : 0);

	/* Register CQ interrupts */
	for (irq = 0; irq < nic->qs->cq_cnt; irq++) {
		ret = request_irq(pci_irq_vector(nic->pdev, irq),
				  nicvf_intr_handler,
				  0, nic->irq_name[irq], nic->napi[irq]);
		if (ret)
			goto err;
		nic->irq_allocated[irq] = true;
	}

	/* Register RBDR interrupt */
	for (irq = NICVF_INTR_ID_RBDR;
	     irq < (NICVF_INTR_ID_RBDR + nic->qs->rbdr_cnt); irq++) {
		ret = request_irq(pci_irq_vector(nic->pdev, irq),
				  nicvf_rbdr_intr_handler,
				  0, nic->irq_name[irq], nic);
		if (ret)
			goto err;
		nic->irq_allocated[irq] = true;
	}

	/* Register QS error interrupt */
	sprintf(nic->irq_name[NICVF_INTR_ID_QS_ERR], "%s-qset-err-%d",
		nic->pnicvf->netdev->name,
		nic->sqs_mode ? (nic->sqs_id + 1) : 0);
	irq = NICVF_INTR_ID_QS_ERR;
	ret = request_irq(pci_irq_vector(nic->pdev, irq),
			  nicvf_qs_err_intr_handler,
			  0, nic->irq_name[irq], nic);
	if (ret)
		goto err;

	nic->irq_allocated[irq] = true;

	/* Set IRQ affinities */
	nicvf_set_irq_affinity(nic);

err:
	if (ret)
		netdev_err(nic->netdev, "request_irq failed, vector %d\n", irq);

	return ret;
}

static void nicvf_unregister_interrupts(struct nicvf *nic)
{
	struct pci_dev *pdev = nic->pdev;
	int irq;

	/* Free registered interrupts */
	for (irq = 0; irq < nic->num_vec; irq++) {
		if (!nic->irq_allocated[irq])
			continue;

		irq_set_affinity_hint(pci_irq_vector(pdev, irq), NULL);
		free_cpumask_var(nic->affinity_mask[irq]);

		if (irq < NICVF_INTR_ID_SQ)
			free_irq(pci_irq_vector(pdev, irq), nic->napi[irq]);
		else
			free_irq(pci_irq_vector(pdev, irq), nic);

		nic->irq_allocated[irq] = false;
	}

	/* Disable MSI-X */
	pci_free_irq_vectors(pdev);
	nic->num_vec = 0;
}

/* Initialize MSIX vectors and register MISC interrupt.
 * Send READY message to PF to check if its alive
 */
static int nicvf_register_misc_interrupt(struct nicvf *nic)
{
	int ret = 0;
	int irq = NICVF_INTR_ID_MISC;

	/* Return if mailbox interrupt is already registered */
	if (nic->pdev->msix_enabled)
		return 0;

	/* Enable MSI-X */
	nic->num_vec = pci_msix_vec_count(nic->pdev);
	ret = pci_alloc_irq_vectors(nic->pdev, nic->num_vec, nic->num_vec,
				    PCI_IRQ_MSIX);
	if (ret < 0) {
		netdev_err(nic->netdev,
			   "Req for #%d msix vectors failed\n", nic->num_vec);
		return ret;
	}

	sprintf(nic->irq_name[irq], "%s Mbox", "NICVF");
	/* Register Misc interrupt */
	ret = request_irq(pci_irq_vector(nic->pdev, irq),
			  nicvf_misc_intr_handler, 0, nic->irq_name[irq], nic);

	if (ret)
		return ret;
	nic->irq_allocated[irq] = true;

	/* Enable mailbox interrupt */
	nicvf_enable_intr(nic, NICVF_INTR_MBOX, 0);

	/* Check if VF is able to communicate with PF */
	if (!nicvf_check_pf_ready(nic)) {
		nicvf_disable_intr(nic, NICVF_INTR_MBOX, 0);
		nicvf_unregister_interrupts(nic);
		return -EIO;
	}

	return 0;
}

static netdev_tx_t nicvf_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct nicvf *nic = netdev_priv(netdev);
	int qid = skb_get_queue_mapping(skb);
	struct netdev_queue *txq = netdev_get_tx_queue(netdev, qid);
	struct nicvf *snic;
	struct snd_queue *sq;
	int tmp;

	/* Check for minimum packet length */
	if (skb->len <= ETH_HLEN) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	/* In XDP case, initial HW tx queues are used for XDP,
	 * but stack's queue mapping starts at '0', so skip the
	 * Tx queues attached to Rx queues for XDP.
	 */
	if (nic->xdp_prog)
		qid += nic->xdp_tx_queues;

	snic = nic;
	/* Get secondary Qset's SQ structure */
	if (qid >= MAX_SND_QUEUES_PER_QS) {
		tmp = qid / MAX_SND_QUEUES_PER_QS;
		snic = (struct nicvf *)nic->snicvf[tmp - 1];
		if (!snic) {
			netdev_warn(nic->netdev,
				    "Secondary Qset#%d's ptr not initialized\n",
				    tmp - 1);
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		}
		qid = qid % MAX_SND_QUEUES_PER_QS;
	}

	sq = &snic->qs->sq[qid];
	if (!netif_tx_queue_stopped(txq) &&
	    !nicvf_sq_append_skb(snic, sq, skb, qid)) {
		netif_tx_stop_queue(txq);

		/* Barrier, so that stop_queue visible to other cpus */
		smp_mb();

		/* Check again, incase another cpu freed descriptors */
		if (atomic_read(&sq->free_cnt) > MIN_SQ_DESC_PER_PKT_XMIT) {
			netif_tx_wake_queue(txq);
		} else {
			this_cpu_inc(nic->drv_stats->txq_stop);
			netif_warn(nic, tx_err, netdev,
				   "Transmit ring full, stopping SQ%d\n", qid);
		}
		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

static inline void nicvf_free_cq_poll(struct nicvf *nic)
{
	struct nicvf_cq_poll *cq_poll;
	int qidx;

	for (qidx = 0; qidx < nic->qs->cq_cnt; qidx++) {
		cq_poll = nic->napi[qidx];
		if (!cq_poll)
			continue;
		nic->napi[qidx] = NULL;
		kfree(cq_poll);
	}
}

int nicvf_stop(struct net_device *netdev)
{
	int irq, qidx;
	struct nicvf *nic = netdev_priv(netdev);
	struct queue_set *qs = nic->qs;
	struct nicvf_cq_poll *cq_poll = NULL;
	union nic_mbx mbx = {};

	/* wait till all queued set_rx_mode tasks completes */
	if (nic->nicvf_rx_mode_wq) {
		cancel_delayed_work_sync(&nic->link_change_work);
		drain_workqueue(nic->nicvf_rx_mode_wq);
	}

	mbx.msg.msg = NIC_MBOX_MSG_SHUTDOWN;
	nicvf_send_msg_to_pf(nic, &mbx);

	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(nic->netdev);
	nic->link_up = false;

	/* Teardown secondary qsets first */
	if (!nic->sqs_mode) {
		for (qidx = 0; qidx < nic->sqs_count; qidx++) {
			if (!nic->snicvf[qidx])
				continue;
			nicvf_stop(nic->snicvf[qidx]->netdev);
			nic->snicvf[qidx] = NULL;
		}
	}

	/* Disable RBDR & QS error interrupts */
	for (qidx = 0; qidx < qs->rbdr_cnt; qidx++) {
		nicvf_disable_intr(nic, NICVF_INTR_RBDR, qidx);
		nicvf_clear_intr(nic, NICVF_INTR_RBDR, qidx);
	}
	nicvf_disable_intr(nic, NICVF_INTR_QS_ERR, 0);
	nicvf_clear_intr(nic, NICVF_INTR_QS_ERR, 0);

	/* Wait for pending IRQ handlers to finish */
	for (irq = 0; irq < nic->num_vec; irq++)
		synchronize_irq(pci_irq_vector(nic->pdev, irq));

	tasklet_kill(&nic->rbdr_task);
	tasklet_kill(&nic->qs_err_task);
	if (nic->rb_work_scheduled)
		cancel_delayed_work_sync(&nic->rbdr_work);

	for (qidx = 0; qidx < nic->qs->cq_cnt; qidx++) {
		cq_poll = nic->napi[qidx];
		if (!cq_poll)
			continue;
		napi_synchronize(&cq_poll->napi);
		/* CQ intr is enabled while napi_complete,
		 * so disable it now
		 */
		nicvf_disable_intr(nic, NICVF_INTR_CQ, qidx);
		nicvf_clear_intr(nic, NICVF_INTR_CQ, qidx);
		napi_disable(&cq_poll->napi);
		netif_napi_del(&cq_poll->napi);
	}

	netif_tx_disable(netdev);

	for (qidx = 0; qidx < netdev->num_tx_queues; qidx++)
		netdev_tx_reset_queue(netdev_get_tx_queue(netdev, qidx));

	/* Free resources */
	nicvf_config_data_transfer(nic, false);

	/* Disable HW Qset */
	nicvf_qset_config(nic, false);

	/* disable mailbox interrupt */
	nicvf_disable_intr(nic, NICVF_INTR_MBOX, 0);

	nicvf_unregister_interrupts(nic);

	nicvf_free_cq_poll(nic);

	/* Free any pending SKB saved to receive timestamp */
	if (nic->ptp_skb) {
		dev_kfree_skb_any(nic->ptp_skb);
		nic->ptp_skb = NULL;
	}

	/* Clear multiqset info */
	nic->pnicvf = nic;

	return 0;
}

static int nicvf_config_hw_rx_tstamp(struct nicvf *nic, bool enable)
{
	union nic_mbx mbx = {};

	mbx.ptp.msg = NIC_MBOX_MSG_PTP_CFG;
	mbx.ptp.enable = enable;

	return nicvf_send_msg_to_pf(nic, &mbx);
}

static int nicvf_update_hw_max_frs(struct nicvf *nic, int mtu)
{
	union nic_mbx mbx = {};

	mbx.frs.msg = NIC_MBOX_MSG_SET_MAX_FRS;
	mbx.frs.max_frs = mtu;
	mbx.frs.vf_id = nic->vf_id;

	return nicvf_send_msg_to_pf(nic, &mbx);
}

static void nicvf_link_status_check_task(struct work_struct *work_arg)
{
	struct nicvf *nic = container_of(work_arg,
					 struct nicvf,
					 link_change_work.work);
	union nic_mbx mbx = {};
	mbx.msg.msg = NIC_MBOX_MSG_BGX_LINK_CHANGE;
	nicvf_send_msg_to_pf(nic, &mbx);
	queue_delayed_work(nic->nicvf_rx_mode_wq,
			   &nic->link_change_work, 2 * HZ);
}

int nicvf_open(struct net_device *netdev)
{
	int cpu, err, qidx;
	struct nicvf *nic = netdev_priv(netdev);
	struct queue_set *qs = nic->qs;
	struct nicvf_cq_poll *cq_poll = NULL;

	/* wait till all queued set_rx_mode tasks completes if any */
	if (nic->nicvf_rx_mode_wq)
		drain_workqueue(nic->nicvf_rx_mode_wq);

	netif_carrier_off(netdev);

	err = nicvf_register_misc_interrupt(nic);
	if (err)
		return err;

	/* Register NAPI handler for processing CQEs */
	for (qidx = 0; qidx < qs->cq_cnt; qidx++) {
		cq_poll = kzalloc(sizeof(*cq_poll), GFP_KERNEL);
		if (!cq_poll) {
			err = -ENOMEM;
			goto napi_del;
		}
		cq_poll->cq_idx = qidx;
		cq_poll->nicvf = nic;
		netif_napi_add(netdev, &cq_poll->napi, nicvf_poll);
		napi_enable(&cq_poll->napi);
		nic->napi[qidx] = cq_poll;
	}

	/* Check if we got MAC address from PF or else generate a radom MAC */
	if (!nic->sqs_mode && is_zero_ether_addr(netdev->dev_addr)) {
		eth_hw_addr_random(netdev);
		nicvf_hw_set_mac_addr(nic, netdev);
	}

	if (nic->set_mac_pending) {
		nic->set_mac_pending = false;
		nicvf_hw_set_mac_addr(nic, netdev);
	}

	/* Init tasklet for handling Qset err interrupt */
	tasklet_setup(&nic->qs_err_task, nicvf_handle_qs_err);

	/* Init RBDR tasklet which will refill RBDR */
	tasklet_setup(&nic->rbdr_task, nicvf_rbdr_task);
	INIT_DELAYED_WORK(&nic->rbdr_work, nicvf_rbdr_work);

	/* Configure CPI alorithm */
	nic->cpi_alg = cpi_alg;
	if (!nic->sqs_mode)
		nicvf_config_cpi(nic);

	nicvf_request_sqs(nic);
	if (nic->sqs_mode)
		nicvf_get_primary_vf_struct(nic);

	/* Configure PTP timestamp */
	if (nic->ptp_clock)
		nicvf_config_hw_rx_tstamp(nic, nic->hw_rx_tstamp);
	atomic_set(&nic->tx_ptp_skbs, 0);
	nic->ptp_skb = NULL;

	/* Configure receive side scaling and MTU */
	if (!nic->sqs_mode) {
		nicvf_rss_init(nic);
		err = nicvf_update_hw_max_frs(nic, netdev->mtu);
		if (err)
			goto cleanup;

		/* Clear percpu stats */
		for_each_possible_cpu(cpu)
			memset(per_cpu_ptr(nic->drv_stats, cpu), 0,
			       sizeof(struct nicvf_drv_stats));
	}

	err = nicvf_register_interrupts(nic);
	if (err)
		goto cleanup;

	/* Initialize the queues */
	err = nicvf_init_resources(nic);
	if (err)
		goto cleanup;

	/* Make sure queue initialization is written */
	wmb();

	nicvf_reg_write(nic, NIC_VF_INT, -1);
	/* Enable Qset err interrupt */
	nicvf_enable_intr(nic, NICVF_INTR_QS_ERR, 0);

	/* Enable completion queue interrupt */
	for (qidx = 0; qidx < qs->cq_cnt; qidx++)
		nicvf_enable_intr(nic, NICVF_INTR_CQ, qidx);

	/* Enable RBDR threshold interrupt */
	for (qidx = 0; qidx < qs->rbdr_cnt; qidx++)
		nicvf_enable_intr(nic, NICVF_INTR_RBDR, qidx);

	/* Send VF config done msg to PF */
	nicvf_send_cfg_done(nic);

	if (nic->nicvf_rx_mode_wq) {
		INIT_DELAYED_WORK(&nic->link_change_work,
				  nicvf_link_status_check_task);
		queue_delayed_work(nic->nicvf_rx_mode_wq,
				   &nic->link_change_work, 0);
	}

	return 0;
cleanup:
	nicvf_disable_intr(nic, NICVF_INTR_MBOX, 0);
	nicvf_unregister_interrupts(nic);
	tasklet_kill(&nic->qs_err_task);
	tasklet_kill(&nic->rbdr_task);
napi_del:
	for (qidx = 0; qidx < qs->cq_cnt; qidx++) {
		cq_poll = nic->napi[qidx];
		if (!cq_poll)
			continue;
		napi_disable(&cq_poll->napi);
		netif_napi_del(&cq_poll->napi);
	}
	nicvf_free_cq_poll(nic);
	return err;
}

static int nicvf_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct nicvf *nic = netdev_priv(netdev);
	int orig_mtu = netdev->mtu;

	/* For now just support only the usual MTU sized frames,
	 * plus some headroom for VLAN, QinQ.
	 */
	if (nic->xdp_prog && new_mtu > MAX_XDP_MTU) {
		netdev_warn(netdev, "Jumbo frames not yet supported with XDP, current MTU %d.\n",
			    netdev->mtu);
		return -EINVAL;
	}

	netdev->mtu = new_mtu;

	if (!netif_running(netdev))
		return 0;

	if (nicvf_update_hw_max_frs(nic, new_mtu)) {
		netdev->mtu = orig_mtu;
		return -EINVAL;
	}

	return 0;
}

static int nicvf_set_mac_address(struct net_device *netdev, void *p)
{
	struct sockaddr *addr = p;
	struct nicvf *nic = netdev_priv(netdev);

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(netdev, addr->sa_data);

	if (nic->pdev->msix_enabled) {
		if (nicvf_hw_set_mac_addr(nic, netdev))
			return -EBUSY;
	} else {
		nic->set_mac_pending = true;
	}

	return 0;
}

void nicvf_update_lmac_stats(struct nicvf *nic)
{
	int stat = 0;
	union nic_mbx mbx = {};

	if (!netif_running(nic->netdev))
		return;

	mbx.bgx_stats.msg = NIC_MBOX_MSG_BGX_STATS;
	mbx.bgx_stats.vf_id = nic->vf_id;
	/* Rx stats */
	mbx.bgx_stats.rx = 1;
	while (stat < BGX_RX_STATS_COUNT) {
		mbx.bgx_stats.idx = stat;
		if (nicvf_send_msg_to_pf(nic, &mbx))
			return;
		stat++;
	}

	stat = 0;

	/* Tx stats */
	mbx.bgx_stats.rx = 0;
	while (stat < BGX_TX_STATS_COUNT) {
		mbx.bgx_stats.idx = stat;
		if (nicvf_send_msg_to_pf(nic, &mbx))
			return;
		stat++;
	}
}

void nicvf_update_stats(struct nicvf *nic)
{
	int qidx, cpu;
	u64 tmp_stats = 0;
	struct nicvf_hw_stats *stats = &nic->hw_stats;
	struct nicvf_drv_stats *drv_stats;
	struct queue_set *qs = nic->qs;

#define GET_RX_STATS(reg) \
	nicvf_reg_read(nic, NIC_VNIC_RX_STAT_0_13 | (reg << 3))
#define GET_TX_STATS(reg) \
	nicvf_reg_read(nic, NIC_VNIC_TX_STAT_0_4 | (reg << 3))

	stats->rx_bytes = GET_RX_STATS(RX_OCTS);
	stats->rx_ucast_frames = GET_RX_STATS(RX_UCAST);
	stats->rx_bcast_frames = GET_RX_STATS(RX_BCAST);
	stats->rx_mcast_frames = GET_RX_STATS(RX_MCAST);
	stats->rx_fcs_errors = GET_RX_STATS(RX_FCS);
	stats->rx_l2_errors = GET_RX_STATS(RX_L2ERR);
	stats->rx_drop_red = GET_RX_STATS(RX_RED);
	stats->rx_drop_red_bytes = GET_RX_STATS(RX_RED_OCTS);
	stats->rx_drop_overrun = GET_RX_STATS(RX_ORUN);
	stats->rx_drop_overrun_bytes = GET_RX_STATS(RX_ORUN_OCTS);
	stats->rx_drop_bcast = GET_RX_STATS(RX_DRP_BCAST);
	stats->rx_drop_mcast = GET_RX_STATS(RX_DRP_MCAST);
	stats->rx_drop_l3_bcast = GET_RX_STATS(RX_DRP_L3BCAST);
	stats->rx_drop_l3_mcast = GET_RX_STATS(RX_DRP_L3MCAST);

	stats->tx_bytes = GET_TX_STATS(TX_OCTS);
	stats->tx_ucast_frames = GET_TX_STATS(TX_UCAST);
	stats->tx_bcast_frames = GET_TX_STATS(TX_BCAST);
	stats->tx_mcast_frames = GET_TX_STATS(TX_MCAST);
	stats->tx_drops = GET_TX_STATS(TX_DROP);

	/* On T88 pass 2.0, the dummy SQE added for TSO notification
	 * via CQE has 'dont_send' set. Hence HW drops the pkt pointed
	 * pointed by dummy SQE and results in tx_drops counter being
	 * incremented. Subtracting it from tx_tso counter will give
	 * exact tx_drops counter.
	 */
	if (nic->t88 && nic->hw_tso) {
		for_each_possible_cpu(cpu) {
			drv_stats = per_cpu_ptr(nic->drv_stats, cpu);
			tmp_stats += drv_stats->tx_tso;
		}
		stats->tx_drops = tmp_stats - stats->tx_drops;
	}
	stats->tx_frames = stats->tx_ucast_frames +
			   stats->tx_bcast_frames +
			   stats->tx_mcast_frames;
	stats->rx_frames = stats->rx_ucast_frames +
			   stats->rx_bcast_frames +
			   stats->rx_mcast_frames;
	stats->rx_drops = stats->rx_drop_red +
			  stats->rx_drop_overrun;

	/* Update RQ and SQ stats */
	for (qidx = 0; qidx < qs->rq_cnt; qidx++)
		nicvf_update_rq_stats(nic, qidx);
	for (qidx = 0; qidx < qs->sq_cnt; qidx++)
		nicvf_update_sq_stats(nic, qidx);
}

static void nicvf_get_stats64(struct net_device *netdev,
			      struct rtnl_link_stats64 *stats)
{
	struct nicvf *nic = netdev_priv(netdev);
	struct nicvf_hw_stats *hw_stats = &nic->hw_stats;

	nicvf_update_stats(nic);

	stats->rx_bytes = hw_stats->rx_bytes;
	stats->rx_packets = hw_stats->rx_frames;
	stats->rx_dropped = hw_stats->rx_drops;
	stats->multicast = hw_stats->rx_mcast_frames;

	stats->tx_bytes = hw_stats->tx_bytes;
	stats->tx_packets = hw_stats->tx_frames;
	stats->tx_dropped = hw_stats->tx_drops;

}

static void nicvf_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct nicvf *nic = netdev_priv(dev);

	netif_warn(nic, tx_err, dev, "Transmit timed out, resetting\n");

	this_cpu_inc(nic->drv_stats->tx_timeout);
	schedule_work(&nic->reset_task);
}

static void nicvf_reset_task(struct work_struct *work)
{
	struct nicvf *nic;

	nic = container_of(work, struct nicvf, reset_task);

	if (!netif_running(nic->netdev))
		return;

	nicvf_stop(nic->netdev);
	nicvf_open(nic->netdev);
	netif_trans_update(nic->netdev);
}

static int nicvf_config_loopback(struct nicvf *nic,
				 netdev_features_t features)
{
	union nic_mbx mbx = {};

	mbx.lbk.msg = NIC_MBOX_MSG_LOOPBACK;
	mbx.lbk.vf_id = nic->vf_id;
	mbx.lbk.enable = (features & NETIF_F_LOOPBACK) != 0;

	return nicvf_send_msg_to_pf(nic, &mbx);
}

static netdev_features_t nicvf_fix_features(struct net_device *netdev,
					    netdev_features_t features)
{
	struct nicvf *nic = netdev_priv(netdev);

	if ((features & NETIF_F_LOOPBACK) &&
	    netif_running(netdev) && !nic->loopback_supported)
		features &= ~NETIF_F_LOOPBACK;

	return features;
}

static int nicvf_set_features(struct net_device *netdev,
			      netdev_features_t features)
{
	struct nicvf *nic = netdev_priv(netdev);
	netdev_features_t changed = features ^ netdev->features;

	if (changed & NETIF_F_HW_VLAN_CTAG_RX)
		nicvf_config_vlan_stripping(nic, features);

	if ((changed & NETIF_F_LOOPBACK) && netif_running(netdev))
		return nicvf_config_loopback(nic, features);

	return 0;
}

static void nicvf_set_xdp_queues(struct nicvf *nic, bool bpf_attached)
{
	u8 cq_count, txq_count;

	/* Set XDP Tx queue count same as Rx queue count */
	if (!bpf_attached)
		nic->xdp_tx_queues = 0;
	else
		nic->xdp_tx_queues = nic->rx_queues;

	/* If queue count > MAX_CMP_QUEUES_PER_QS, then additional qsets
	 * needs to be allocated, check how many.
	 */
	txq_count = nic->xdp_tx_queues + nic->tx_queues;
	cq_count = max(nic->rx_queues, txq_count);
	if (cq_count > MAX_CMP_QUEUES_PER_QS) {
		nic->sqs_count = roundup(cq_count, MAX_CMP_QUEUES_PER_QS);
		nic->sqs_count = (nic->sqs_count / MAX_CMP_QUEUES_PER_QS) - 1;
	} else {
		nic->sqs_count = 0;
	}

	/* Set primary Qset's resources */
	nic->qs->rq_cnt = min_t(u8, nic->rx_queues, MAX_RCV_QUEUES_PER_QS);
	nic->qs->sq_cnt = min_t(u8, txq_count, MAX_SND_QUEUES_PER_QS);
	nic->qs->cq_cnt = max_t(u8, nic->qs->rq_cnt, nic->qs->sq_cnt);

	/* Update stack */
	nicvf_set_real_num_queues(nic->netdev, nic->tx_queues, nic->rx_queues);
}

static int nicvf_xdp_setup(struct nicvf *nic, struct bpf_prog *prog)
{
	struct net_device *dev = nic->netdev;
	bool if_up = netif_running(nic->netdev);
	struct bpf_prog *old_prog;
	bool bpf_attached = false;
	int ret = 0;

	/* For now just support only the usual MTU sized frames,
	 * plus some headroom for VLAN, QinQ.
	 */
	if (prog && dev->mtu > MAX_XDP_MTU) {
		netdev_warn(dev, "Jumbo frames not yet supported with XDP, current MTU %d.\n",
			    dev->mtu);
		return -EOPNOTSUPP;
	}

	/* ALL SQs attached to CQs i.e same as RQs, are treated as
	 * XDP Tx queues and more Tx queues are allocated for
	 * network stack to send pkts out.
	 *
	 * No of Tx queues are either same as Rx queues or whatever
	 * is left in max no of queues possible.
	 */
	if ((nic->rx_queues + nic->tx_queues) > nic->max_queues) {
		netdev_warn(dev,
			    "Failed to attach BPF prog, RXQs + TXQs > Max %d\n",
			    nic->max_queues);
		return -ENOMEM;
	}

	if (if_up)
		nicvf_stop(nic->netdev);

	old_prog = xchg(&nic->xdp_prog, prog);
	/* Detach old prog, if any */
	if (old_prog)
		bpf_prog_put(old_prog);

	if (nic->xdp_prog) {
		/* Attach BPF program */
		bpf_prog_add(nic->xdp_prog, nic->rx_queues - 1);
		bpf_attached = true;
	}

	/* Calculate Tx queues needed for XDP and network stack */
	nicvf_set_xdp_queues(nic, bpf_attached);

	if (if_up) {
		/* Reinitialize interface, clean slate */
		nicvf_open(nic->netdev);
		netif_trans_update(nic->netdev);
	}

	return ret;
}

static int nicvf_xdp(struct net_device *netdev, struct netdev_bpf *xdp)
{
	struct nicvf *nic = netdev_priv(netdev);

	/* To avoid checks while retrieving buffer address from CQE_RX,
	 * do not support XDP for T88 pass1.x silicons which are anyway
	 * not in use widely.
	 */
	if (pass1_silicon(nic->pdev))
		return -EOPNOTSUPP;

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return nicvf_xdp_setup(nic, xdp->prog);
	default:
		return -EINVAL;
	}
}

static int nicvf_config_hwtstamp(struct net_device *netdev, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	struct nicvf *nic = netdev_priv(netdev);

	if (!nic->ptp_clock)
		return -ENODEV;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		nic->hw_rx_tstamp = false;
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		nic->hw_rx_tstamp = true;
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	if (netif_running(netdev))
		nicvf_config_hw_rx_tstamp(nic, nic->hw_rx_tstamp);

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	return 0;
}

static int nicvf_ioctl(struct net_device *netdev, struct ifreq *req, int cmd)
{
	switch (cmd) {
	case SIOCSHWTSTAMP:
		return nicvf_config_hwtstamp(netdev, req);
	default:
		return -EOPNOTSUPP;
	}
}

static void __nicvf_set_rx_mode_task(u8 mode, struct xcast_addr_list *mc_addrs,
				     struct nicvf *nic)
{
	union nic_mbx mbx = {};
	int idx;

	/* From the inside of VM code flow we have only 128 bits memory
	 * available to send message to host's PF, so send all mc addrs
	 * one by one, starting from flush command in case if kernel
	 * requests to configure specific MAC filtering
	 */

	/* flush DMAC filters and reset RX mode */
	mbx.xcast.msg = NIC_MBOX_MSG_RESET_XCAST;
	if (nicvf_send_msg_to_pf(nic, &mbx) < 0)
		goto free_mc;

	if (mode & BGX_XCAST_MCAST_FILTER) {
		/* once enabling filtering, we need to signal to PF to add
		 * its' own LMAC to the filter to accept packets for it.
		 */
		mbx.xcast.msg = NIC_MBOX_MSG_ADD_MCAST;
		mbx.xcast.mac = 0;
		if (nicvf_send_msg_to_pf(nic, &mbx) < 0)
			goto free_mc;
	}

	/* check if we have any specific MACs to be added to PF DMAC filter */
	if (mc_addrs) {
		/* now go through kernel list of MACs and add them one by one */
		for (idx = 0; idx < mc_addrs->count; idx++) {
			mbx.xcast.msg = NIC_MBOX_MSG_ADD_MCAST;
			mbx.xcast.mac = mc_addrs->mc[idx];
			if (nicvf_send_msg_to_pf(nic, &mbx) < 0)
				goto free_mc;
		}
	}

	/* and finally set rx mode for PF accordingly */
	mbx.xcast.msg = NIC_MBOX_MSG_SET_XCAST;
	mbx.xcast.mode = mode;

	nicvf_send_msg_to_pf(nic, &mbx);
free_mc:
	kfree(mc_addrs);
}

static void nicvf_set_rx_mode_task(struct work_struct *work_arg)
{
	struct nicvf_work *vf_work = container_of(work_arg, struct nicvf_work,
						  work);
	struct nicvf *nic = container_of(vf_work, struct nicvf, rx_mode_work);
	u8 mode;
	struct xcast_addr_list *mc;

	/* Save message data locally to prevent them from
	 * being overwritten by next ndo_set_rx_mode call().
	 */
	spin_lock_bh(&nic->rx_mode_wq_lock);
	mode = vf_work->mode;
	mc = vf_work->mc;
	vf_work->mc = NULL;
	spin_unlock_bh(&nic->rx_mode_wq_lock);

	__nicvf_set_rx_mode_task(mode, mc, nic);
}

static void nicvf_set_rx_mode(struct net_device *netdev)
{
	struct nicvf *nic = netdev_priv(netdev);
	struct netdev_hw_addr *ha;
	struct xcast_addr_list *mc_list = NULL;
	u8 mode = 0;

	if (netdev->flags & IFF_PROMISC) {
		mode = BGX_XCAST_BCAST_ACCEPT | BGX_XCAST_MCAST_ACCEPT;
	} else {
		if (netdev->flags & IFF_BROADCAST)
			mode |= BGX_XCAST_BCAST_ACCEPT;

		if (netdev->flags & IFF_ALLMULTI) {
			mode |= BGX_XCAST_MCAST_ACCEPT;
		} else if (netdev->flags & IFF_MULTICAST) {
			mode |= BGX_XCAST_MCAST_FILTER;
			/* here we need to copy mc addrs */
			if (netdev_mc_count(netdev)) {
				mc_list = kmalloc(struct_size(mc_list, mc,
							      netdev_mc_count(netdev)),
						  GFP_ATOMIC);
				if (unlikely(!mc_list))
					return;
				mc_list->count = 0;
				netdev_hw_addr_list_for_each(ha, &netdev->mc) {
					mc_list->mc[mc_list->count] =
						ether_addr_to_u64(ha->addr);
					mc_list->count++;
				}
			}
		}
	}
	spin_lock(&nic->rx_mode_wq_lock);
	kfree(nic->rx_mode_work.mc);
	nic->rx_mode_work.mc = mc_list;
	nic->rx_mode_work.mode = mode;
	queue_work(nic->nicvf_rx_mode_wq, &nic->rx_mode_work.work);
	spin_unlock(&nic->rx_mode_wq_lock);
}

static const struct net_device_ops nicvf_netdev_ops = {
	.ndo_open		= nicvf_open,
	.ndo_stop		= nicvf_stop,
	.ndo_start_xmit		= nicvf_xmit,
	.ndo_change_mtu		= nicvf_change_mtu,
	.ndo_set_mac_address	= nicvf_set_mac_address,
	.ndo_get_stats64	= nicvf_get_stats64,
	.ndo_tx_timeout         = nicvf_tx_timeout,
	.ndo_fix_features       = nicvf_fix_features,
	.ndo_set_features       = nicvf_set_features,
	.ndo_bpf		= nicvf_xdp,
	.ndo_eth_ioctl           = nicvf_ioctl,
	.ndo_set_rx_mode        = nicvf_set_rx_mode,
};

static int nicvf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct net_device *netdev;
	struct nicvf *nic;
	int    err, qcount;
	u16    sdevid;
	struct cavium_ptp *ptp_clock;

	ptp_clock = cavium_ptp_get();
	if (IS_ERR(ptp_clock)) {
		if (PTR_ERR(ptp_clock) == -ENODEV)
			/* In virtualized environment we proceed without ptp */
			ptp_clock = NULL;
		else
			return PTR_ERR(ptp_clock);
	}

	err = pci_enable_device(pdev);
	if (err)
		return dev_err_probe(dev, err, "Failed to enable PCI device\n");

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto err_disable_device;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "Unable to get usable DMA configuration\n");
		goto err_release_regions;
	}

	qcount = netif_get_num_default_rss_queues();

	/* Restrict multiqset support only for host bound VFs */
	if (pdev->is_virtfn) {
		/* Set max number of queues per VF */
		qcount = min_t(int, num_online_cpus(),
			       (MAX_SQS_PER_VF + 1) * MAX_CMP_QUEUES_PER_QS);
	}

	netdev = alloc_etherdev_mqs(sizeof(struct nicvf), qcount, qcount);
	if (!netdev) {
		err = -ENOMEM;
		goto err_release_regions;
	}

	pci_set_drvdata(pdev, netdev);

	SET_NETDEV_DEV(netdev, &pdev->dev);

	nic = netdev_priv(netdev);
	nic->netdev = netdev;
	nic->pdev = pdev;
	nic->pnicvf = nic;
	nic->max_queues = qcount;
	/* If no of CPUs are too low, there won't be any queues left
	 * for XDP_TX, hence double it.
	 */
	if (!nic->t88)
		nic->max_queues *= 2;
	nic->ptp_clock = ptp_clock;

	/* Initialize mutex that serializes usage of VF's mailbox */
	mutex_init(&nic->rx_mode_mtx);

	/* MAP VF's configuration registers */
	nic->reg_base = pcim_iomap(pdev, PCI_CFG_REG_BAR_NUM, 0);
	if (!nic->reg_base) {
		dev_err(dev, "Cannot map config register space, aborting\n");
		err = -ENOMEM;
		goto err_free_netdev;
	}

	nic->drv_stats = netdev_alloc_pcpu_stats(struct nicvf_drv_stats);
	if (!nic->drv_stats) {
		err = -ENOMEM;
		goto err_free_netdev;
	}

	err = nicvf_set_qset_resources(nic);
	if (err)
		goto err_free_netdev;

	/* Check if PF is alive and get MAC address for this VF */
	err = nicvf_register_misc_interrupt(nic);
	if (err)
		goto err_free_netdev;

	nicvf_send_vf_struct(nic);

	if (!pass1_silicon(nic->pdev))
		nic->hw_tso = true;

	/* Get iommu domain for iova to physical addr conversion */
	nic->iommu_domain = iommu_get_domain_for_dev(dev);

	pci_read_config_word(nic->pdev, PCI_SUBSYSTEM_ID, &sdevid);
	if (sdevid == 0xA134)
		nic->t88 = true;

	/* Check if this VF is in QS only mode */
	if (nic->sqs_mode)
		return 0;

	err = nicvf_set_real_num_queues(netdev, nic->tx_queues, nic->rx_queues);
	if (err)
		goto err_unregister_interrupts;

	netdev->hw_features = (NETIF_F_RXCSUM | NETIF_F_SG |
			       NETIF_F_TSO | NETIF_F_GRO | NETIF_F_TSO6 |
			       NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			       NETIF_F_HW_VLAN_CTAG_RX);

	netdev->hw_features |= NETIF_F_RXHASH;

	netdev->features |= netdev->hw_features;
	netdev->hw_features |= NETIF_F_LOOPBACK;

	netdev->vlan_features = NETIF_F_SG | NETIF_F_IP_CSUM |
				NETIF_F_IPV6_CSUM | NETIF_F_TSO | NETIF_F_TSO6;

	netdev->netdev_ops = &nicvf_netdev_ops;
	netdev->watchdog_timeo = NICVF_TX_TIMEOUT;

	/* MTU range: 64 - 9200 */
	netdev->min_mtu = NIC_HW_MIN_FRS;
	netdev->max_mtu = NIC_HW_MAX_FRS;

	INIT_WORK(&nic->reset_task, nicvf_reset_task);

	nic->nicvf_rx_mode_wq = alloc_ordered_workqueue("nicvf_rx_mode_wq_VF%d",
							WQ_MEM_RECLAIM,
							nic->vf_id);
	if (!nic->nicvf_rx_mode_wq) {
		err = -ENOMEM;
		dev_err(dev, "Failed to allocate work queue\n");
		goto err_unregister_interrupts;
	}

	INIT_WORK(&nic->rx_mode_work.work, nicvf_set_rx_mode_task);
	spin_lock_init(&nic->rx_mode_wq_lock);

	err = register_netdev(netdev);
	if (err) {
		dev_err(dev, "Failed to register netdevice\n");
		goto err_unregister_interrupts;
	}

	nic->msg_enable = debug;

	nicvf_set_ethtool_ops(netdev);

	return 0;

err_unregister_interrupts:
	nicvf_unregister_interrupts(nic);
err_free_netdev:
	pci_set_drvdata(pdev, NULL);
	if (nic->drv_stats)
		free_percpu(nic->drv_stats);
	free_netdev(netdev);
err_release_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
	return err;
}

static void nicvf_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct nicvf *nic;
	struct net_device *pnetdev;

	if (!netdev)
		return;

	nic = netdev_priv(netdev);
	pnetdev = nic->pnicvf->netdev;

	/* Check if this Qset is assigned to different VF.
	 * If yes, clean primary and all secondary Qsets.
	 */
	if (pnetdev && (pnetdev->reg_state == NETREG_REGISTERED))
		unregister_netdev(pnetdev);
	if (nic->nicvf_rx_mode_wq) {
		destroy_workqueue(nic->nicvf_rx_mode_wq);
		nic->nicvf_rx_mode_wq = NULL;
	}
	nicvf_unregister_interrupts(nic);
	pci_set_drvdata(pdev, NULL);
	if (nic->drv_stats)
		free_percpu(nic->drv_stats);
	cavium_ptp_put(nic->ptp_clock);
	free_netdev(netdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static void nicvf_shutdown(struct pci_dev *pdev)
{
	nicvf_remove(pdev);
}

static struct pci_driver nicvf_driver = {
	.name = DRV_NAME,
	.id_table = nicvf_id_table,
	.probe = nicvf_probe,
	.remove = nicvf_remove,
	.shutdown = nicvf_shutdown,
};

static int __init nicvf_init_module(void)
{
	pr_info("%s, ver %s\n", DRV_NAME, DRV_VERSION);
	return pci_register_driver(&nicvf_driver);
}

static void __exit nicvf_cleanup_module(void)
{
	pci_unregister_driver(&nicvf_driver);
}

module_init(nicvf_init_module);
module_exit(nicvf_cleanup_module);
