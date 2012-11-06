/*
 * Copyright (C) 2005 - 2011 Emulex
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include <linux/prefetch.h>
#include <linux/module.h>
#include "be.h"
#include "be_cmds.h"
#include <asm/div64.h>
#include <linux/aer.h>

MODULE_VERSION(DRV_VER);
MODULE_DEVICE_TABLE(pci, be_dev_ids);
MODULE_DESCRIPTION(DRV_DESC " " DRV_VER);
MODULE_AUTHOR("ServerEngines Corporation");
MODULE_LICENSE("GPL");

static unsigned int num_vfs;
module_param(num_vfs, uint, S_IRUGO);
MODULE_PARM_DESC(num_vfs, "Number of PCI VFs to initialize");

static ushort rx_frag_size = 2048;
module_param(rx_frag_size, ushort, S_IRUGO);
MODULE_PARM_DESC(rx_frag_size, "Size of a fragment that holds rcvd data.");

static DEFINE_PCI_DEVICE_TABLE(be_dev_ids) = {
	{ PCI_DEVICE(BE_VENDOR_ID, BE_DEVICE_ID1) },
	{ PCI_DEVICE(BE_VENDOR_ID, BE_DEVICE_ID2) },
	{ PCI_DEVICE(BE_VENDOR_ID, OC_DEVICE_ID1) },
	{ PCI_DEVICE(BE_VENDOR_ID, OC_DEVICE_ID2) },
	{ PCI_DEVICE(EMULEX_VENDOR_ID, OC_DEVICE_ID3)},
	{ PCI_DEVICE(EMULEX_VENDOR_ID, OC_DEVICE_ID4)},
	{ PCI_DEVICE(EMULEX_VENDOR_ID, OC_DEVICE_ID5)},
	{ PCI_DEVICE(EMULEX_VENDOR_ID, OC_DEVICE_ID6)},
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, be_dev_ids);
/* UE Status Low CSR */
static const char * const ue_status_low_desc[] = {
	"CEV",
	"CTX",
	"DBUF",
	"ERX",
	"Host",
	"MPU",
	"NDMA",
	"PTC ",
	"RDMA ",
	"RXF ",
	"RXIPS ",
	"RXULP0 ",
	"RXULP1 ",
	"RXULP2 ",
	"TIM ",
	"TPOST ",
	"TPRE ",
	"TXIPS ",
	"TXULP0 ",
	"TXULP1 ",
	"UC ",
	"WDMA ",
	"TXULP2 ",
	"HOST1 ",
	"P0_OB_LINK ",
	"P1_OB_LINK ",
	"HOST_GPIO ",
	"MBOX ",
	"AXGMAC0",
	"AXGMAC1",
	"JTAG",
	"MPU_INTPEND"
};
/* UE Status High CSR */
static const char * const ue_status_hi_desc[] = {
	"LPCMEMHOST",
	"MGMT_MAC",
	"PCS0ONLINE",
	"MPU_IRAM",
	"PCS1ONLINE",
	"PCTL0",
	"PCTL1",
	"PMEM",
	"RR",
	"TXPB",
	"RXPP",
	"XAUI",
	"TXP",
	"ARM",
	"IPC",
	"HOST2",
	"HOST3",
	"HOST4",
	"HOST5",
	"HOST6",
	"HOST7",
	"HOST8",
	"HOST9",
	"NETC",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown",
	"Unknown"
};

/* Is BE in a multi-channel mode */
static inline bool be_is_mc(struct be_adapter *adapter) {
	return (adapter->function_mode & FLEX10_MODE ||
		adapter->function_mode & VNIC_MODE ||
		adapter->function_mode & UMC_ENABLED);
}

static void be_queue_free(struct be_adapter *adapter, struct be_queue_info *q)
{
	struct be_dma_mem *mem = &q->dma_mem;
	if (mem->va) {
		dma_free_coherent(&adapter->pdev->dev, mem->size, mem->va,
				  mem->dma);
		mem->va = NULL;
	}
}

static int be_queue_alloc(struct be_adapter *adapter, struct be_queue_info *q,
		u16 len, u16 entry_size)
{
	struct be_dma_mem *mem = &q->dma_mem;

	memset(q, 0, sizeof(*q));
	q->len = len;
	q->entry_size = entry_size;
	mem->size = len * entry_size;
	mem->va = dma_alloc_coherent(&adapter->pdev->dev, mem->size, &mem->dma,
				     GFP_KERNEL);
	if (!mem->va)
		return -ENOMEM;
	memset(mem->va, 0, mem->size);
	return 0;
}

static void be_intr_set(struct be_adapter *adapter, bool enable)
{
	u32 reg, enabled;

	if (adapter->eeh_error)
		return;

	pci_read_config_dword(adapter->pdev, PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET,
				&reg);
	enabled = reg & MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;

	if (!enabled && enable)
		reg |= MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
	else if (enabled && !enable)
		reg &= ~MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
	else
		return;

	pci_write_config_dword(adapter->pdev,
			PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET, reg);
}

static void be_rxq_notify(struct be_adapter *adapter, u16 qid, u16 posted)
{
	u32 val = 0;
	val |= qid & DB_RQ_RING_ID_MASK;
	val |= posted << DB_RQ_NUM_POSTED_SHIFT;

	wmb();
	iowrite32(val, adapter->db + DB_RQ_OFFSET);
}

static void be_txq_notify(struct be_adapter *adapter, u16 qid, u16 posted)
{
	u32 val = 0;
	val |= qid & DB_TXULP_RING_ID_MASK;
	val |= (posted & DB_TXULP_NUM_POSTED_MASK) << DB_TXULP_NUM_POSTED_SHIFT;

	wmb();
	iowrite32(val, adapter->db + DB_TXULP1_OFFSET);
}

static void be_eq_notify(struct be_adapter *adapter, u16 qid,
		bool arm, bool clear_int, u16 num_popped)
{
	u32 val = 0;
	val |= qid & DB_EQ_RING_ID_MASK;
	val |= ((qid & DB_EQ_RING_ID_EXT_MASK) <<
			DB_EQ_RING_ID_EXT_MASK_SHIFT);

	if (adapter->eeh_error)
		return;

	if (arm)
		val |= 1 << DB_EQ_REARM_SHIFT;
	if (clear_int)
		val |= 1 << DB_EQ_CLR_SHIFT;
	val |= 1 << DB_EQ_EVNT_SHIFT;
	val |= num_popped << DB_EQ_NUM_POPPED_SHIFT;
	iowrite32(val, adapter->db + DB_EQ_OFFSET);
}

void be_cq_notify(struct be_adapter *adapter, u16 qid, bool arm, u16 num_popped)
{
	u32 val = 0;
	val |= qid & DB_CQ_RING_ID_MASK;
	val |= ((qid & DB_CQ_RING_ID_EXT_MASK) <<
			DB_CQ_RING_ID_EXT_MASK_SHIFT);

	if (adapter->eeh_error)
		return;

	if (arm)
		val |= 1 << DB_CQ_REARM_SHIFT;
	val |= num_popped << DB_CQ_NUM_POPPED_SHIFT;
	iowrite32(val, adapter->db + DB_CQ_OFFSET);
}

static int be_mac_addr_set(struct net_device *netdev, void *p)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *addr = p;
	int status = 0;
	u8 current_mac[ETH_ALEN];
	u32 pmac_id = adapter->pmac_id[0];
	bool active_mac = true;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* For BE VF, MAC address is already activated by PF.
	 * Hence only operation left is updating netdev->devaddr.
	 * Update it if user is passing the same MAC which was used
	 * during configuring VF MAC from PF(Hypervisor).
	 */
	if (!lancer_chip(adapter) && !be_physfn(adapter)) {
		status = be_cmd_mac_addr_query(adapter, current_mac,
					       false, adapter->if_handle, 0);
		if (!status && !memcmp(current_mac, addr->sa_data, ETH_ALEN))
			goto done;
		else
			goto err;
	}

	if (!memcmp(addr->sa_data, netdev->dev_addr, ETH_ALEN))
		goto done;

	/* For Lancer check if any MAC is active.
	 * If active, get its mac id.
	 */
	if (lancer_chip(adapter) && !be_physfn(adapter))
		be_cmd_get_mac_from_list(adapter, current_mac, &active_mac,
					 &pmac_id, 0);

	status = be_cmd_pmac_add(adapter, (u8 *)addr->sa_data,
				 adapter->if_handle,
				 &adapter->pmac_id[0], 0);

	if (status)
		goto err;

	if (active_mac)
		be_cmd_pmac_del(adapter, adapter->if_handle,
				pmac_id, 0);
done:
	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	return 0;
err:
	dev_err(&adapter->pdev->dev, "MAC %pM set Failed\n", addr->sa_data);
	return status;
}

/* BE2 supports only v0 cmd */
static void *hw_stats_from_cmd(struct be_adapter *adapter)
{
	if (BE2_chip(adapter)) {
		struct be_cmd_resp_get_stats_v0 *cmd = adapter->stats_cmd.va;

		return &cmd->hw_stats;
	} else  {
		struct be_cmd_resp_get_stats_v1 *cmd = adapter->stats_cmd.va;

		return &cmd->hw_stats;
	}
}

/* BE2 supports only v0 cmd */
static void *be_erx_stats_from_cmd(struct be_adapter *adapter)
{
	if (BE2_chip(adapter)) {
		struct be_hw_stats_v0 *hw_stats = hw_stats_from_cmd(adapter);

		return &hw_stats->erx;
	} else {
		struct be_hw_stats_v1 *hw_stats = hw_stats_from_cmd(adapter);

		return &hw_stats->erx;
	}
}

static void populate_be_v0_stats(struct be_adapter *adapter)
{
	struct be_hw_stats_v0 *hw_stats = hw_stats_from_cmd(adapter);
	struct be_pmem_stats *pmem_sts = &hw_stats->pmem;
	struct be_rxf_stats_v0 *rxf_stats = &hw_stats->rxf;
	struct be_port_rxf_stats_v0 *port_stats =
					&rxf_stats->port[adapter->port_num];
	struct be_drv_stats *drvs = &adapter->drv_stats;

	be_dws_le_to_cpu(hw_stats, sizeof(*hw_stats));
	drvs->rx_pause_frames = port_stats->rx_pause_frames;
	drvs->rx_crc_errors = port_stats->rx_crc_errors;
	drvs->rx_control_frames = port_stats->rx_control_frames;
	drvs->rx_in_range_errors = port_stats->rx_in_range_errors;
	drvs->rx_frame_too_long = port_stats->rx_frame_too_long;
	drvs->rx_dropped_runt = port_stats->rx_dropped_runt;
	drvs->rx_ip_checksum_errs = port_stats->rx_ip_checksum_errs;
	drvs->rx_tcp_checksum_errs = port_stats->rx_tcp_checksum_errs;
	drvs->rx_udp_checksum_errs = port_stats->rx_udp_checksum_errs;
	drvs->rxpp_fifo_overflow_drop = port_stats->rx_fifo_overflow;
	drvs->rx_dropped_tcp_length = port_stats->rx_dropped_tcp_length;
	drvs->rx_dropped_too_small = port_stats->rx_dropped_too_small;
	drvs->rx_dropped_too_short = port_stats->rx_dropped_too_short;
	drvs->rx_out_range_errors = port_stats->rx_out_range_errors;
	drvs->rx_input_fifo_overflow_drop = port_stats->rx_input_fifo_overflow;
	drvs->rx_dropped_header_too_small =
		port_stats->rx_dropped_header_too_small;
	drvs->rx_address_mismatch_drops =
					port_stats->rx_address_mismatch_drops +
					port_stats->rx_vlan_mismatch_drops;
	drvs->rx_alignment_symbol_errors =
		port_stats->rx_alignment_symbol_errors;

	drvs->tx_pauseframes = port_stats->tx_pauseframes;
	drvs->tx_controlframes = port_stats->tx_controlframes;

	if (adapter->port_num)
		drvs->jabber_events = rxf_stats->port1_jabber_events;
	else
		drvs->jabber_events = rxf_stats->port0_jabber_events;
	drvs->rx_drops_no_pbuf = rxf_stats->rx_drops_no_pbuf;
	drvs->rx_drops_no_erx_descr = rxf_stats->rx_drops_no_erx_descr;
	drvs->forwarded_packets = rxf_stats->forwarded_packets;
	drvs->rx_drops_mtu = rxf_stats->rx_drops_mtu;
	drvs->rx_drops_no_tpre_descr = rxf_stats->rx_drops_no_tpre_descr;
	drvs->rx_drops_too_many_frags = rxf_stats->rx_drops_too_many_frags;
	adapter->drv_stats.eth_red_drops = pmem_sts->eth_red_drops;
}

static void populate_be_v1_stats(struct be_adapter *adapter)
{
	struct be_hw_stats_v1 *hw_stats = hw_stats_from_cmd(adapter);
	struct be_pmem_stats *pmem_sts = &hw_stats->pmem;
	struct be_rxf_stats_v1 *rxf_stats = &hw_stats->rxf;
	struct be_port_rxf_stats_v1 *port_stats =
					&rxf_stats->port[adapter->port_num];
	struct be_drv_stats *drvs = &adapter->drv_stats;

	be_dws_le_to_cpu(hw_stats, sizeof(*hw_stats));
	drvs->pmem_fifo_overflow_drop = port_stats->pmem_fifo_overflow_drop;
	drvs->rx_priority_pause_frames = port_stats->rx_priority_pause_frames;
	drvs->rx_pause_frames = port_stats->rx_pause_frames;
	drvs->rx_crc_errors = port_stats->rx_crc_errors;
	drvs->rx_control_frames = port_stats->rx_control_frames;
	drvs->rx_in_range_errors = port_stats->rx_in_range_errors;
	drvs->rx_frame_too_long = port_stats->rx_frame_too_long;
	drvs->rx_dropped_runt = port_stats->rx_dropped_runt;
	drvs->rx_ip_checksum_errs = port_stats->rx_ip_checksum_errs;
	drvs->rx_tcp_checksum_errs = port_stats->rx_tcp_checksum_errs;
	drvs->rx_udp_checksum_errs = port_stats->rx_udp_checksum_errs;
	drvs->rx_dropped_tcp_length = port_stats->rx_dropped_tcp_length;
	drvs->rx_dropped_too_small = port_stats->rx_dropped_too_small;
	drvs->rx_dropped_too_short = port_stats->rx_dropped_too_short;
	drvs->rx_out_range_errors = port_stats->rx_out_range_errors;
	drvs->rx_dropped_header_too_small =
		port_stats->rx_dropped_header_too_small;
	drvs->rx_input_fifo_overflow_drop =
		port_stats->rx_input_fifo_overflow_drop;
	drvs->rx_address_mismatch_drops = port_stats->rx_address_mismatch_drops;
	drvs->rx_alignment_symbol_errors =
		port_stats->rx_alignment_symbol_errors;
	drvs->rxpp_fifo_overflow_drop = port_stats->rxpp_fifo_overflow_drop;
	drvs->tx_pauseframes = port_stats->tx_pauseframes;
	drvs->tx_controlframes = port_stats->tx_controlframes;
	drvs->jabber_events = port_stats->jabber_events;
	drvs->rx_drops_no_pbuf = rxf_stats->rx_drops_no_pbuf;
	drvs->rx_drops_no_erx_descr = rxf_stats->rx_drops_no_erx_descr;
	drvs->forwarded_packets = rxf_stats->forwarded_packets;
	drvs->rx_drops_mtu = rxf_stats->rx_drops_mtu;
	drvs->rx_drops_no_tpre_descr = rxf_stats->rx_drops_no_tpre_descr;
	drvs->rx_drops_too_many_frags = rxf_stats->rx_drops_too_many_frags;
	adapter->drv_stats.eth_red_drops = pmem_sts->eth_red_drops;
}

static void populate_lancer_stats(struct be_adapter *adapter)
{

	struct be_drv_stats *drvs = &adapter->drv_stats;
	struct lancer_pport_stats *pport_stats =
					pport_stats_from_cmd(adapter);

	be_dws_le_to_cpu(pport_stats, sizeof(*pport_stats));
	drvs->rx_pause_frames = pport_stats->rx_pause_frames_lo;
	drvs->rx_crc_errors = pport_stats->rx_crc_errors_lo;
	drvs->rx_control_frames = pport_stats->rx_control_frames_lo;
	drvs->rx_in_range_errors = pport_stats->rx_in_range_errors;
	drvs->rx_frame_too_long = pport_stats->rx_frames_too_long_lo;
	drvs->rx_dropped_runt = pport_stats->rx_dropped_runt;
	drvs->rx_ip_checksum_errs = pport_stats->rx_ip_checksum_errors;
	drvs->rx_tcp_checksum_errs = pport_stats->rx_tcp_checksum_errors;
	drvs->rx_udp_checksum_errs = pport_stats->rx_udp_checksum_errors;
	drvs->rx_dropped_tcp_length =
				pport_stats->rx_dropped_invalid_tcp_length;
	drvs->rx_dropped_too_small = pport_stats->rx_dropped_too_small;
	drvs->rx_dropped_too_short = pport_stats->rx_dropped_too_short;
	drvs->rx_out_range_errors = pport_stats->rx_out_of_range_errors;
	drvs->rx_dropped_header_too_small =
				pport_stats->rx_dropped_header_too_small;
	drvs->rx_input_fifo_overflow_drop = pport_stats->rx_fifo_overflow;
	drvs->rx_address_mismatch_drops =
					pport_stats->rx_address_mismatch_drops +
					pport_stats->rx_vlan_mismatch_drops;
	drvs->rx_alignment_symbol_errors = pport_stats->rx_symbol_errors_lo;
	drvs->rxpp_fifo_overflow_drop = pport_stats->rx_fifo_overflow;
	drvs->tx_pauseframes = pport_stats->tx_pause_frames_lo;
	drvs->tx_controlframes = pport_stats->tx_control_frames_lo;
	drvs->jabber_events = pport_stats->rx_jabbers;
	drvs->forwarded_packets = pport_stats->num_forwards_lo;
	drvs->rx_drops_mtu = pport_stats->rx_drops_mtu_lo;
	drvs->rx_drops_too_many_frags =
				pport_stats->rx_drops_too_many_frags_lo;
}

static void accumulate_16bit_val(u32 *acc, u16 val)
{
#define lo(x)			(x & 0xFFFF)
#define hi(x)			(x & 0xFFFF0000)
	bool wrapped = val < lo(*acc);
	u32 newacc = hi(*acc) + val;

	if (wrapped)
		newacc += 65536;
	ACCESS_ONCE(*acc) = newacc;
}

void be_parse_stats(struct be_adapter *adapter)
{
	struct be_erx_stats_v1 *erx = be_erx_stats_from_cmd(adapter);
	struct be_rx_obj *rxo;
	int i;

	if (lancer_chip(adapter)) {
		populate_lancer_stats(adapter);
	} else {
		if (BE2_chip(adapter))
			populate_be_v0_stats(adapter);
		else
			/* for BE3 and Skyhawk */
			populate_be_v1_stats(adapter);

		/* as erx_v1 is longer than v0, ok to use v1 for v0 access */
		for_all_rx_queues(adapter, rxo, i) {
			/* below erx HW counter can actually wrap around after
			 * 65535. Driver accumulates a 32-bit value
			 */
			accumulate_16bit_val(&rx_stats(rxo)->rx_drops_no_frags,
					     (u16)erx->rx_drops_no_fragments \
					     [rxo->q.id]);
		}
	}
}

static struct rtnl_link_stats64 *be_get_stats64(struct net_device *netdev,
					struct rtnl_link_stats64 *stats)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_drv_stats *drvs = &adapter->drv_stats;
	struct be_rx_obj *rxo;
	struct be_tx_obj *txo;
	u64 pkts, bytes;
	unsigned int start;
	int i;

	for_all_rx_queues(adapter, rxo, i) {
		const struct be_rx_stats *rx_stats = rx_stats(rxo);
		do {
			start = u64_stats_fetch_begin_bh(&rx_stats->sync);
			pkts = rx_stats(rxo)->rx_pkts;
			bytes = rx_stats(rxo)->rx_bytes;
		} while (u64_stats_fetch_retry_bh(&rx_stats->sync, start));
		stats->rx_packets += pkts;
		stats->rx_bytes += bytes;
		stats->multicast += rx_stats(rxo)->rx_mcast_pkts;
		stats->rx_dropped += rx_stats(rxo)->rx_drops_no_skbs +
					rx_stats(rxo)->rx_drops_no_frags;
	}

	for_all_tx_queues(adapter, txo, i) {
		const struct be_tx_stats *tx_stats = tx_stats(txo);
		do {
			start = u64_stats_fetch_begin_bh(&tx_stats->sync);
			pkts = tx_stats(txo)->tx_pkts;
			bytes = tx_stats(txo)->tx_bytes;
		} while (u64_stats_fetch_retry_bh(&tx_stats->sync, start));
		stats->tx_packets += pkts;
		stats->tx_bytes += bytes;
	}

	/* bad pkts received */
	stats->rx_errors = drvs->rx_crc_errors +
		drvs->rx_alignment_symbol_errors +
		drvs->rx_in_range_errors +
		drvs->rx_out_range_errors +
		drvs->rx_frame_too_long +
		drvs->rx_dropped_too_small +
		drvs->rx_dropped_too_short +
		drvs->rx_dropped_header_too_small +
		drvs->rx_dropped_tcp_length +
		drvs->rx_dropped_runt;

	/* detailed rx errors */
	stats->rx_length_errors = drvs->rx_in_range_errors +
		drvs->rx_out_range_errors +
		drvs->rx_frame_too_long;

	stats->rx_crc_errors = drvs->rx_crc_errors;

	/* frame alignment errors */
	stats->rx_frame_errors = drvs->rx_alignment_symbol_errors;

	/* receiver fifo overrun */
	/* drops_no_pbuf is no per i/f, it's per BE card */
	stats->rx_fifo_errors = drvs->rxpp_fifo_overflow_drop +
				drvs->rx_input_fifo_overflow_drop +
				drvs->rx_drops_no_pbuf;
	return stats;
}

void be_link_status_update(struct be_adapter *adapter, u8 link_status)
{
	struct net_device *netdev = adapter->netdev;

	if (!(adapter->flags & BE_FLAGS_LINK_STATUS_INIT)) {
		netif_carrier_off(netdev);
		adapter->flags |= BE_FLAGS_LINK_STATUS_INIT;
	}

	if ((link_status & LINK_STATUS_MASK) == LINK_UP)
		netif_carrier_on(netdev);
	else
		netif_carrier_off(netdev);
}

static void be_tx_stats_update(struct be_tx_obj *txo,
			u32 wrb_cnt, u32 copied, u32 gso_segs, bool stopped)
{
	struct be_tx_stats *stats = tx_stats(txo);

	u64_stats_update_begin(&stats->sync);
	stats->tx_reqs++;
	stats->tx_wrbs += wrb_cnt;
	stats->tx_bytes += copied;
	stats->tx_pkts += (gso_segs ? gso_segs : 1);
	if (stopped)
		stats->tx_stops++;
	u64_stats_update_end(&stats->sync);
}

/* Determine number of WRB entries needed to xmit data in an skb */
static u32 wrb_cnt_for_skb(struct be_adapter *adapter, struct sk_buff *skb,
								bool *dummy)
{
	int cnt = (skb->len > skb->data_len);

	cnt += skb_shinfo(skb)->nr_frags;

	/* to account for hdr wrb */
	cnt++;
	if (lancer_chip(adapter) || !(cnt & 1)) {
		*dummy = false;
	} else {
		/* add a dummy to make it an even num */
		cnt++;
		*dummy = true;
	}
	BUG_ON(cnt > BE_MAX_TX_FRAG_COUNT);
	return cnt;
}

static inline void wrb_fill(struct be_eth_wrb *wrb, u64 addr, int len)
{
	wrb->frag_pa_hi = upper_32_bits(addr);
	wrb->frag_pa_lo = addr & 0xFFFFFFFF;
	wrb->frag_len = len & ETH_WRB_FRAG_LEN_MASK;
	wrb->rsvd0 = 0;
}

static inline u16 be_get_tx_vlan_tag(struct be_adapter *adapter,
					struct sk_buff *skb)
{
	u8 vlan_prio;
	u16 vlan_tag;

	vlan_tag = vlan_tx_tag_get(skb);
	vlan_prio = (vlan_tag & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
	/* If vlan priority provided by OS is NOT in available bmap */
	if (!(adapter->vlan_prio_bmap & (1 << vlan_prio)))
		vlan_tag = (vlan_tag & ~VLAN_PRIO_MASK) |
				adapter->recommended_prio;

	return vlan_tag;
}

static int be_vlan_tag_chk(struct be_adapter *adapter, struct sk_buff *skb)
{
	return vlan_tx_tag_present(skb) || adapter->pvid;
}

static void wrb_fill_hdr(struct be_adapter *adapter, struct be_eth_hdr_wrb *hdr,
		struct sk_buff *skb, u32 wrb_cnt, u32 len)
{
	u16 vlan_tag;

	memset(hdr, 0, sizeof(*hdr));

	AMAP_SET_BITS(struct amap_eth_hdr_wrb, crc, hdr, 1);

	if (skb_is_gso(skb)) {
		AMAP_SET_BITS(struct amap_eth_hdr_wrb, lso, hdr, 1);
		AMAP_SET_BITS(struct amap_eth_hdr_wrb, lso_mss,
			hdr, skb_shinfo(skb)->gso_size);
		if (skb_is_gso_v6(skb) && !lancer_chip(adapter))
			AMAP_SET_BITS(struct amap_eth_hdr_wrb, lso6, hdr, 1);
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (is_tcp_pkt(skb))
			AMAP_SET_BITS(struct amap_eth_hdr_wrb, tcpcs, hdr, 1);
		else if (is_udp_pkt(skb))
			AMAP_SET_BITS(struct amap_eth_hdr_wrb, udpcs, hdr, 1);
	}

	if (vlan_tx_tag_present(skb)) {
		AMAP_SET_BITS(struct amap_eth_hdr_wrb, vlan, hdr, 1);
		vlan_tag = be_get_tx_vlan_tag(adapter, skb);
		AMAP_SET_BITS(struct amap_eth_hdr_wrb, vlan_tag, hdr, vlan_tag);
	}

	AMAP_SET_BITS(struct amap_eth_hdr_wrb, event, hdr, 1);
	AMAP_SET_BITS(struct amap_eth_hdr_wrb, complete, hdr, 1);
	AMAP_SET_BITS(struct amap_eth_hdr_wrb, num_wrb, hdr, wrb_cnt);
	AMAP_SET_BITS(struct amap_eth_hdr_wrb, len, hdr, len);
}

static void unmap_tx_frag(struct device *dev, struct be_eth_wrb *wrb,
		bool unmap_single)
{
	dma_addr_t dma;

	be_dws_le_to_cpu(wrb, sizeof(*wrb));

	dma = (u64)wrb->frag_pa_hi << 32 | (u64)wrb->frag_pa_lo;
	if (wrb->frag_len) {
		if (unmap_single)
			dma_unmap_single(dev, dma, wrb->frag_len,
					 DMA_TO_DEVICE);
		else
			dma_unmap_page(dev, dma, wrb->frag_len, DMA_TO_DEVICE);
	}
}

static int make_tx_wrbs(struct be_adapter *adapter, struct be_queue_info *txq,
		struct sk_buff *skb, u32 wrb_cnt, bool dummy_wrb)
{
	dma_addr_t busaddr;
	int i, copied = 0;
	struct device *dev = &adapter->pdev->dev;
	struct sk_buff *first_skb = skb;
	struct be_eth_wrb *wrb;
	struct be_eth_hdr_wrb *hdr;
	bool map_single = false;
	u16 map_head;

	hdr = queue_head_node(txq);
	queue_head_inc(txq);
	map_head = txq->head;

	if (skb->len > skb->data_len) {
		int len = skb_headlen(skb);
		busaddr = dma_map_single(dev, skb->data, len, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, busaddr))
			goto dma_err;
		map_single = true;
		wrb = queue_head_node(txq);
		wrb_fill(wrb, busaddr, len);
		be_dws_cpu_to_le(wrb, sizeof(*wrb));
		queue_head_inc(txq);
		copied += len;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const struct skb_frag_struct *frag =
			&skb_shinfo(skb)->frags[i];
		busaddr = skb_frag_dma_map(dev, frag, 0,
					   skb_frag_size(frag), DMA_TO_DEVICE);
		if (dma_mapping_error(dev, busaddr))
			goto dma_err;
		wrb = queue_head_node(txq);
		wrb_fill(wrb, busaddr, skb_frag_size(frag));
		be_dws_cpu_to_le(wrb, sizeof(*wrb));
		queue_head_inc(txq);
		copied += skb_frag_size(frag);
	}

	if (dummy_wrb) {
		wrb = queue_head_node(txq);
		wrb_fill(wrb, 0, 0);
		be_dws_cpu_to_le(wrb, sizeof(*wrb));
		queue_head_inc(txq);
	}

	wrb_fill_hdr(adapter, hdr, first_skb, wrb_cnt, copied);
	be_dws_cpu_to_le(hdr, sizeof(*hdr));

	return copied;
dma_err:
	txq->head = map_head;
	while (copied) {
		wrb = queue_head_node(txq);
		unmap_tx_frag(dev, wrb, map_single);
		map_single = false;
		copied -= wrb->frag_len;
		queue_head_inc(txq);
	}
	return 0;
}

static struct sk_buff *be_insert_vlan_in_pkt(struct be_adapter *adapter,
					     struct sk_buff *skb)
{
	u16 vlan_tag = 0;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		return skb;

	if (vlan_tx_tag_present(skb)) {
		vlan_tag = be_get_tx_vlan_tag(adapter, skb);
		__vlan_put_tag(skb, vlan_tag);
		skb->vlan_tci = 0;
	}

	return skb;
}

static netdev_tx_t be_xmit(struct sk_buff *skb,
			struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_tx_obj *txo = &adapter->tx_obj[skb_get_queue_mapping(skb)];
	struct be_queue_info *txq = &txo->q;
	struct iphdr *ip = NULL;
	u32 wrb_cnt = 0, copied = 0;
	u32 start = txq->head, eth_hdr_len;
	bool dummy_wrb, stopped = false;

	eth_hdr_len = ntohs(skb->protocol) == ETH_P_8021Q ?
		VLAN_ETH_HLEN : ETH_HLEN;

	/* HW has a bug which considers padding bytes as legal
	 * and modifies the IPv4 hdr's 'tot_len' field
	 */
	if (skb->len <= 60 && be_vlan_tag_chk(adapter, skb) &&
			is_ipv4_pkt(skb)) {
		ip = (struct iphdr *)ip_hdr(skb);
		pskb_trim(skb, eth_hdr_len + ntohs(ip->tot_len));
	}

	/* HW has a bug wherein it will calculate CSUM for VLAN
	 * pkts even though it is disabled.
	 * Manually insert VLAN in pkt.
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL &&
			be_vlan_tag_chk(adapter, skb)) {
		skb = be_insert_vlan_in_pkt(adapter, skb);
		if (unlikely(!skb))
			goto tx_drop;
	}

	wrb_cnt = wrb_cnt_for_skb(adapter, skb, &dummy_wrb);

	copied = make_tx_wrbs(adapter, txq, skb, wrb_cnt, dummy_wrb);
	if (copied) {
		int gso_segs = skb_shinfo(skb)->gso_segs;

		/* record the sent skb in the sent_skb table */
		BUG_ON(txo->sent_skb_list[start]);
		txo->sent_skb_list[start] = skb;

		/* Ensure txq has space for the next skb; Else stop the queue
		 * *BEFORE* ringing the tx doorbell, so that we serialze the
		 * tx compls of the current transmit which'll wake up the queue
		 */
		atomic_add(wrb_cnt, &txq->used);
		if ((BE_MAX_TX_FRAG_COUNT + atomic_read(&txq->used)) >=
								txq->len) {
			netif_stop_subqueue(netdev, skb_get_queue_mapping(skb));
			stopped = true;
		}

		be_txq_notify(adapter, txq->id, wrb_cnt);

		be_tx_stats_update(txo, wrb_cnt, copied, gso_segs, stopped);
	} else {
		txq->head = start;
		dev_kfree_skb_any(skb);
	}
tx_drop:
	return NETDEV_TX_OK;
}

static int be_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	if (new_mtu < BE_MIN_MTU ||
			new_mtu > (BE_MAX_JUMBO_FRAME_SIZE -
					(ETH_HLEN + ETH_FCS_LEN))) {
		dev_info(&adapter->pdev->dev,
			"MTU must be between %d and %d bytes\n",
			BE_MIN_MTU,
			(BE_MAX_JUMBO_FRAME_SIZE - (ETH_HLEN + ETH_FCS_LEN)));
		return -EINVAL;
	}
	dev_info(&adapter->pdev->dev, "MTU changed from %d to %d bytes\n",
			netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;
	return 0;
}

/*
 * A max of 64 (BE_NUM_VLANS_SUPPORTED) vlans can be configured in BE.
 * If the user configures more, place BE in vlan promiscuous mode.
 */
static int be_vid_config(struct be_adapter *adapter)
{
	u16 vids[BE_NUM_VLANS_SUPPORTED];
	u16 num = 0, i;
	int status = 0;

	/* No need to further configure vids if in promiscuous mode */
	if (adapter->promiscuous)
		return 0;

	if (adapter->vlans_added > adapter->max_vlans)
		goto set_vlan_promisc;

	/* Construct VLAN Table to give to HW */
	for (i = 0; i < VLAN_N_VID; i++)
		if (adapter->vlan_tag[i])
			vids[num++] = cpu_to_le16(i);

	status = be_cmd_vlan_config(adapter, adapter->if_handle,
				    vids, num, 1, 0);

	/* Set to VLAN promisc mode as setting VLAN filter failed */
	if (status) {
		dev_info(&adapter->pdev->dev, "Exhausted VLAN HW filters.\n");
		dev_info(&adapter->pdev->dev, "Disabling HW VLAN filtering.\n");
		goto set_vlan_promisc;
	}

	return status;

set_vlan_promisc:
	status = be_cmd_vlan_config(adapter, adapter->if_handle,
				    NULL, 0, 1, 1);
	return status;
}

static int be_vlan_add_vid(struct net_device *netdev, u16 vid)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int status = 0;

	if (!lancer_chip(adapter) && !be_physfn(adapter)) {
		status = -EINVAL;
		goto ret;
	}

	/* Packets with VID 0 are always received by Lancer by default */
	if (lancer_chip(adapter) && vid == 0)
		goto ret;

	adapter->vlan_tag[vid] = 1;
	if (adapter->vlans_added <= (adapter->max_vlans + 1))
		status = be_vid_config(adapter);

	if (!status)
		adapter->vlans_added++;
	else
		adapter->vlan_tag[vid] = 0;
ret:
	return status;
}

static int be_vlan_rem_vid(struct net_device *netdev, u16 vid)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int status = 0;

	if (!lancer_chip(adapter) && !be_physfn(adapter)) {
		status = -EINVAL;
		goto ret;
	}

	/* Packets with VID 0 are always received by Lancer by default */
	if (lancer_chip(adapter) && vid == 0)
		goto ret;

	adapter->vlan_tag[vid] = 0;
	if (adapter->vlans_added <= adapter->max_vlans)
		status = be_vid_config(adapter);

	if (!status)
		adapter->vlans_added--;
	else
		adapter->vlan_tag[vid] = 1;
ret:
	return status;
}

static void be_set_rx_mode(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int status;

	if (netdev->flags & IFF_PROMISC) {
		be_cmd_rx_filter(adapter, IFF_PROMISC, ON);
		adapter->promiscuous = true;
		goto done;
	}

	/* BE was previously in promiscuous mode; disable it */
	if (adapter->promiscuous) {
		adapter->promiscuous = false;
		be_cmd_rx_filter(adapter, IFF_PROMISC, OFF);

		if (adapter->vlans_added)
			be_vid_config(adapter);
	}

	/* Enable multicast promisc if num configured exceeds what we support */
	if (netdev->flags & IFF_ALLMULTI ||
	    netdev_mc_count(netdev) > adapter->max_mcast_mac) {
		be_cmd_rx_filter(adapter, IFF_ALLMULTI, ON);
		goto done;
	}

	if (netdev_uc_count(netdev) != adapter->uc_macs) {
		struct netdev_hw_addr *ha;
		int i = 1; /* First slot is claimed by the Primary MAC */

		for (; adapter->uc_macs > 0; adapter->uc_macs--, i++) {
			be_cmd_pmac_del(adapter, adapter->if_handle,
					adapter->pmac_id[i], 0);
		}

		if (netdev_uc_count(netdev) > adapter->max_pmac_cnt) {
			be_cmd_rx_filter(adapter, IFF_PROMISC, ON);
			adapter->promiscuous = true;
			goto done;
		}

		netdev_for_each_uc_addr(ha, adapter->netdev) {
			adapter->uc_macs++; /* First slot is for Primary MAC */
			be_cmd_pmac_add(adapter, (u8 *)ha->addr,
					adapter->if_handle,
					&adapter->pmac_id[adapter->uc_macs], 0);
		}
	}

	status = be_cmd_rx_filter(adapter, IFF_MULTICAST, ON);

	/* Set to MCAST promisc mode if setting MULTICAST address fails */
	if (status) {
		dev_info(&adapter->pdev->dev, "Exhausted multicast HW filters.\n");
		dev_info(&adapter->pdev->dev, "Disabling HW multicast filtering.\n");
		be_cmd_rx_filter(adapter, IFF_ALLMULTI, ON);
	}
done:
	return;
}

static int be_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_vf_cfg *vf_cfg = &adapter->vf_cfg[vf];
	int status;
	bool active_mac = false;
	u32 pmac_id;
	u8 old_mac[ETH_ALEN];

	if (!sriov_enabled(adapter))
		return -EPERM;

	if (!is_valid_ether_addr(mac) || vf >= adapter->num_vfs)
		return -EINVAL;

	if (lancer_chip(adapter)) {
		status = be_cmd_get_mac_from_list(adapter, old_mac, &active_mac,
						  &pmac_id, vf + 1);
		if (!status && active_mac)
			be_cmd_pmac_del(adapter, vf_cfg->if_handle,
					pmac_id, vf + 1);

		status = be_cmd_set_mac_list(adapter,  mac, 1, vf + 1);
	} else {
		status = be_cmd_pmac_del(adapter, vf_cfg->if_handle,
					 vf_cfg->pmac_id, vf + 1);

		status = be_cmd_pmac_add(adapter, mac, vf_cfg->if_handle,
					 &vf_cfg->pmac_id, vf + 1);
	}

	if (status)
		dev_err(&adapter->pdev->dev, "MAC %pM set on VF %d Failed\n",
				mac, vf);
	else
		memcpy(vf_cfg->mac_addr, mac, ETH_ALEN);

	return status;
}

static int be_get_vf_config(struct net_device *netdev, int vf,
			struct ifla_vf_info *vi)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_vf_cfg *vf_cfg = &adapter->vf_cfg[vf];

	if (!sriov_enabled(adapter))
		return -EPERM;

	if (vf >= adapter->num_vfs)
		return -EINVAL;

	vi->vf = vf;
	vi->tx_rate = vf_cfg->tx_rate;
	vi->vlan = vf_cfg->vlan_tag;
	vi->qos = 0;
	memcpy(&vi->mac, vf_cfg->mac_addr, ETH_ALEN);

	return 0;
}

static int be_set_vf_vlan(struct net_device *netdev,
			int vf, u16 vlan, u8 qos)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int status = 0;

	if (!sriov_enabled(adapter))
		return -EPERM;

	if (vf >= adapter->num_vfs || vlan > 4095)
		return -EINVAL;

	if (vlan) {
		if (adapter->vf_cfg[vf].vlan_tag != vlan) {
			/* If this is new value, program it. Else skip. */
			adapter->vf_cfg[vf].vlan_tag = vlan;

			status = be_cmd_set_hsw_config(adapter, vlan,
				vf + 1, adapter->vf_cfg[vf].if_handle);
		}
	} else {
		/* Reset Transparent Vlan Tagging. */
		adapter->vf_cfg[vf].vlan_tag = 0;
		vlan = adapter->vf_cfg[vf].def_vid;
		status = be_cmd_set_hsw_config(adapter, vlan, vf + 1,
			adapter->vf_cfg[vf].if_handle);
	}


	if (status)
		dev_info(&adapter->pdev->dev,
				"VLAN %d config on VF %d failed\n", vlan, vf);
	return status;
}

static int be_set_vf_tx_rate(struct net_device *netdev,
			int vf, int rate)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int status = 0;

	if (!sriov_enabled(adapter))
		return -EPERM;

	if (vf >= adapter->num_vfs)
		return -EINVAL;

	if (rate < 100 || rate > 10000) {
		dev_err(&adapter->pdev->dev,
			"tx rate must be between 100 and 10000 Mbps\n");
		return -EINVAL;
	}

	if (lancer_chip(adapter))
		status = be_cmd_set_profile_config(adapter, rate / 10, vf + 1);
	else
		status = be_cmd_set_qos(adapter, rate / 10, vf + 1);

	if (status)
		dev_err(&adapter->pdev->dev,
				"tx rate %d on VF %d failed\n", rate, vf);
	else
		adapter->vf_cfg[vf].tx_rate = rate;
	return status;
}

static int be_find_vfs(struct be_adapter *adapter, int vf_state)
{
	struct pci_dev *dev, *pdev = adapter->pdev;
	int vfs = 0, assigned_vfs = 0, pos;
	u16 offset, stride;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_SRIOV);
	if (!pos)
		return 0;
	pci_read_config_word(pdev, pos + PCI_SRIOV_VF_OFFSET, &offset);
	pci_read_config_word(pdev, pos + PCI_SRIOV_VF_STRIDE, &stride);

	dev = pci_get_device(pdev->vendor, PCI_ANY_ID, NULL);
	while (dev) {
		if (dev->is_virtfn && pci_physfn(dev) == pdev) {
			vfs++;
			if (dev->dev_flags & PCI_DEV_FLAGS_ASSIGNED)
				assigned_vfs++;
		}
		dev = pci_get_device(pdev->vendor, PCI_ANY_ID, dev);
	}
	return (vf_state == ASSIGNED) ? assigned_vfs : vfs;
}

static void be_eqd_update(struct be_adapter *adapter, struct be_eq_obj *eqo)
{
	struct be_rx_stats *stats = rx_stats(&adapter->rx_obj[eqo->idx]);
	ulong now = jiffies;
	ulong delta = now - stats->rx_jiffies;
	u64 pkts;
	unsigned int start, eqd;

	if (!eqo->enable_aic) {
		eqd = eqo->eqd;
		goto modify_eqd;
	}

	if (eqo->idx >= adapter->num_rx_qs)
		return;

	stats = rx_stats(&adapter->rx_obj[eqo->idx]);

	/* Wrapped around */
	if (time_before(now, stats->rx_jiffies)) {
		stats->rx_jiffies = now;
		return;
	}

	/* Update once a second */
	if (delta < HZ)
		return;

	do {
		start = u64_stats_fetch_begin_bh(&stats->sync);
		pkts = stats->rx_pkts;
	} while (u64_stats_fetch_retry_bh(&stats->sync, start));

	stats->rx_pps = (unsigned long)(pkts - stats->rx_pkts_prev) / (delta / HZ);
	stats->rx_pkts_prev = pkts;
	stats->rx_jiffies = now;
	eqd = (stats->rx_pps / 110000) << 3;
	eqd = min(eqd, eqo->max_eqd);
	eqd = max(eqd, eqo->min_eqd);
	if (eqd < 10)
		eqd = 0;

modify_eqd:
	if (eqd != eqo->cur_eqd) {
		be_cmd_modify_eqd(adapter, eqo->q.id, eqd);
		eqo->cur_eqd = eqd;
	}
}

static void be_rx_stats_update(struct be_rx_obj *rxo,
		struct be_rx_compl_info *rxcp)
{
	struct be_rx_stats *stats = rx_stats(rxo);

	u64_stats_update_begin(&stats->sync);
	stats->rx_compl++;
	stats->rx_bytes += rxcp->pkt_size;
	stats->rx_pkts++;
	if (rxcp->pkt_type == BE_MULTICAST_PACKET)
		stats->rx_mcast_pkts++;
	if (rxcp->err)
		stats->rx_compl_err++;
	u64_stats_update_end(&stats->sync);
}

static inline bool csum_passed(struct be_rx_compl_info *rxcp)
{
	/* L4 checksum is not reliable for non TCP/UDP packets.
	 * Also ignore ipcksm for ipv6 pkts */
	return (rxcp->tcpf || rxcp->udpf) && rxcp->l4_csum &&
				(rxcp->ip_csum || rxcp->ipv6);
}

static struct be_rx_page_info *get_rx_page_info(struct be_rx_obj *rxo,
						u16 frag_idx)
{
	struct be_adapter *adapter = rxo->adapter;
	struct be_rx_page_info *rx_page_info;
	struct be_queue_info *rxq = &rxo->q;

	rx_page_info = &rxo->page_info_tbl[frag_idx];
	BUG_ON(!rx_page_info->page);

	if (rx_page_info->last_page_user) {
		dma_unmap_page(&adapter->pdev->dev,
			       dma_unmap_addr(rx_page_info, bus),
			       adapter->big_page_size, DMA_FROM_DEVICE);
		rx_page_info->last_page_user = false;
	}

	atomic_dec(&rxq->used);
	return rx_page_info;
}

/* Throwaway the data in the Rx completion */
static void be_rx_compl_discard(struct be_rx_obj *rxo,
				struct be_rx_compl_info *rxcp)
{
	struct be_queue_info *rxq = &rxo->q;
	struct be_rx_page_info *page_info;
	u16 i, num_rcvd = rxcp->num_rcvd;

	for (i = 0; i < num_rcvd; i++) {
		page_info = get_rx_page_info(rxo, rxcp->rxq_idx);
		put_page(page_info->page);
		memset(page_info, 0, sizeof(*page_info));
		index_inc(&rxcp->rxq_idx, rxq->len);
	}
}

/*
 * skb_fill_rx_data forms a complete skb for an ether frame
 * indicated by rxcp.
 */
static void skb_fill_rx_data(struct be_rx_obj *rxo, struct sk_buff *skb,
			     struct be_rx_compl_info *rxcp)
{
	struct be_queue_info *rxq = &rxo->q;
	struct be_rx_page_info *page_info;
	u16 i, j;
	u16 hdr_len, curr_frag_len, remaining;
	u8 *start;

	page_info = get_rx_page_info(rxo, rxcp->rxq_idx);
	start = page_address(page_info->page) + page_info->page_offset;
	prefetch(start);

	/* Copy data in the first descriptor of this completion */
	curr_frag_len = min(rxcp->pkt_size, rx_frag_size);

	skb->len = curr_frag_len;
	if (curr_frag_len <= BE_HDR_LEN) { /* tiny packet */
		memcpy(skb->data, start, curr_frag_len);
		/* Complete packet has now been moved to data */
		put_page(page_info->page);
		skb->data_len = 0;
		skb->tail += curr_frag_len;
	} else {
		hdr_len = ETH_HLEN;
		memcpy(skb->data, start, hdr_len);
		skb_shinfo(skb)->nr_frags = 1;
		skb_frag_set_page(skb, 0, page_info->page);
		skb_shinfo(skb)->frags[0].page_offset =
					page_info->page_offset + hdr_len;
		skb_frag_size_set(&skb_shinfo(skb)->frags[0], curr_frag_len - hdr_len);
		skb->data_len = curr_frag_len - hdr_len;
		skb->truesize += rx_frag_size;
		skb->tail += hdr_len;
	}
	page_info->page = NULL;

	if (rxcp->pkt_size <= rx_frag_size) {
		BUG_ON(rxcp->num_rcvd != 1);
		return;
	}

	/* More frags present for this completion */
	index_inc(&rxcp->rxq_idx, rxq->len);
	remaining = rxcp->pkt_size - curr_frag_len;
	for (i = 1, j = 0; i < rxcp->num_rcvd; i++) {
		page_info = get_rx_page_info(rxo, rxcp->rxq_idx);
		curr_frag_len = min(remaining, rx_frag_size);

		/* Coalesce all frags from the same physical page in one slot */
		if (page_info->page_offset == 0) {
			/* Fresh page */
			j++;
			skb_frag_set_page(skb, j, page_info->page);
			skb_shinfo(skb)->frags[j].page_offset =
							page_info->page_offset;
			skb_frag_size_set(&skb_shinfo(skb)->frags[j], 0);
			skb_shinfo(skb)->nr_frags++;
		} else {
			put_page(page_info->page);
		}

		skb_frag_size_add(&skb_shinfo(skb)->frags[j], curr_frag_len);
		skb->len += curr_frag_len;
		skb->data_len += curr_frag_len;
		skb->truesize += rx_frag_size;
		remaining -= curr_frag_len;
		index_inc(&rxcp->rxq_idx, rxq->len);
		page_info->page = NULL;
	}
	BUG_ON(j > MAX_SKB_FRAGS);
}

/* Process the RX completion indicated by rxcp when GRO is disabled */
static void be_rx_compl_process(struct be_rx_obj *rxo,
				struct be_rx_compl_info *rxcp)
{
	struct be_adapter *adapter = rxo->adapter;
	struct net_device *netdev = adapter->netdev;
	struct sk_buff *skb;

	skb = netdev_alloc_skb_ip_align(netdev, BE_RX_SKB_ALLOC_SIZE);
	if (unlikely(!skb)) {
		rx_stats(rxo)->rx_drops_no_skbs++;
		be_rx_compl_discard(rxo, rxcp);
		return;
	}

	skb_fill_rx_data(rxo, skb, rxcp);

	if (likely((netdev->features & NETIF_F_RXCSUM) && csum_passed(rxcp)))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb_checksum_none_assert(skb);

	skb->protocol = eth_type_trans(skb, netdev);
	skb_record_rx_queue(skb, rxo - &adapter->rx_obj[0]);
	if (netdev->features & NETIF_F_RXHASH)
		skb->rxhash = rxcp->rss_hash;


	if (rxcp->vlanf)
		__vlan_hwaccel_put_tag(skb, rxcp->vlan_tag);

	netif_receive_skb(skb);
}

/* Process the RX completion indicated by rxcp when GRO is enabled */
void be_rx_compl_process_gro(struct be_rx_obj *rxo, struct napi_struct *napi,
			     struct be_rx_compl_info *rxcp)
{
	struct be_adapter *adapter = rxo->adapter;
	struct be_rx_page_info *page_info;
	struct sk_buff *skb = NULL;
	struct be_queue_info *rxq = &rxo->q;
	u16 remaining, curr_frag_len;
	u16 i, j;

	skb = napi_get_frags(napi);
	if (!skb) {
		be_rx_compl_discard(rxo, rxcp);
		return;
	}

	remaining = rxcp->pkt_size;
	for (i = 0, j = -1; i < rxcp->num_rcvd; i++) {
		page_info = get_rx_page_info(rxo, rxcp->rxq_idx);

		curr_frag_len = min(remaining, rx_frag_size);

		/* Coalesce all frags from the same physical page in one slot */
		if (i == 0 || page_info->page_offset == 0) {
			/* First frag or Fresh page */
			j++;
			skb_frag_set_page(skb, j, page_info->page);
			skb_shinfo(skb)->frags[j].page_offset =
							page_info->page_offset;
			skb_frag_size_set(&skb_shinfo(skb)->frags[j], 0);
		} else {
			put_page(page_info->page);
		}
		skb_frag_size_add(&skb_shinfo(skb)->frags[j], curr_frag_len);
		skb->truesize += rx_frag_size;
		remaining -= curr_frag_len;
		index_inc(&rxcp->rxq_idx, rxq->len);
		memset(page_info, 0, sizeof(*page_info));
	}
	BUG_ON(j > MAX_SKB_FRAGS);

	skb_shinfo(skb)->nr_frags = j + 1;
	skb->len = rxcp->pkt_size;
	skb->data_len = rxcp->pkt_size;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_record_rx_queue(skb, rxo - &adapter->rx_obj[0]);
	if (adapter->netdev->features & NETIF_F_RXHASH)
		skb->rxhash = rxcp->rss_hash;

	if (rxcp->vlanf)
		__vlan_hwaccel_put_tag(skb, rxcp->vlan_tag);

	napi_gro_frags(napi);
}

static void be_parse_rx_compl_v1(struct be_eth_rx_compl *compl,
				 struct be_rx_compl_info *rxcp)
{
	rxcp->pkt_size =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v1, pktsize, compl);
	rxcp->vlanf = AMAP_GET_BITS(struct amap_eth_rx_compl_v1, vtp, compl);
	rxcp->err = AMAP_GET_BITS(struct amap_eth_rx_compl_v1, err, compl);
	rxcp->tcpf = AMAP_GET_BITS(struct amap_eth_rx_compl_v1, tcpf, compl);
	rxcp->udpf = AMAP_GET_BITS(struct amap_eth_rx_compl_v1, udpf, compl);
	rxcp->ip_csum =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v1, ipcksm, compl);
	rxcp->l4_csum =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v1, l4_cksm, compl);
	rxcp->ipv6 =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v1, ip_version, compl);
	rxcp->rxq_idx =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v1, fragndx, compl);
	rxcp->num_rcvd =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v1, numfrags, compl);
	rxcp->pkt_type =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v1, cast_enc, compl);
	rxcp->rss_hash =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v1, rsshash, compl);
	if (rxcp->vlanf) {
		rxcp->vtm = AMAP_GET_BITS(struct amap_eth_rx_compl_v1, vtm,
					  compl);
		rxcp->vlan_tag = AMAP_GET_BITS(struct amap_eth_rx_compl_v1, vlan_tag,
					       compl);
	}
	rxcp->port = AMAP_GET_BITS(struct amap_eth_rx_compl_v1, port, compl);
}

static void be_parse_rx_compl_v0(struct be_eth_rx_compl *compl,
				 struct be_rx_compl_info *rxcp)
{
	rxcp->pkt_size =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v0, pktsize, compl);
	rxcp->vlanf = AMAP_GET_BITS(struct amap_eth_rx_compl_v0, vtp, compl);
	rxcp->err = AMAP_GET_BITS(struct amap_eth_rx_compl_v0, err, compl);
	rxcp->tcpf = AMAP_GET_BITS(struct amap_eth_rx_compl_v0, tcpf, compl);
	rxcp->udpf = AMAP_GET_BITS(struct amap_eth_rx_compl_v0, udpf, compl);
	rxcp->ip_csum =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v0, ipcksm, compl);
	rxcp->l4_csum =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v0, l4_cksm, compl);
	rxcp->ipv6 =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v0, ip_version, compl);
	rxcp->rxq_idx =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v0, fragndx, compl);
	rxcp->num_rcvd =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v0, numfrags, compl);
	rxcp->pkt_type =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v0, cast_enc, compl);
	rxcp->rss_hash =
		AMAP_GET_BITS(struct amap_eth_rx_compl_v0, rsshash, compl);
	if (rxcp->vlanf) {
		rxcp->vtm = AMAP_GET_BITS(struct amap_eth_rx_compl_v0, vtm,
					  compl);
		rxcp->vlan_tag = AMAP_GET_BITS(struct amap_eth_rx_compl_v0, vlan_tag,
					       compl);
	}
	rxcp->port = AMAP_GET_BITS(struct amap_eth_rx_compl_v0, port, compl);
}

static struct be_rx_compl_info *be_rx_compl_get(struct be_rx_obj *rxo)
{
	struct be_eth_rx_compl *compl = queue_tail_node(&rxo->cq);
	struct be_rx_compl_info *rxcp = &rxo->rxcp;
	struct be_adapter *adapter = rxo->adapter;

	/* For checking the valid bit it is Ok to use either definition as the
	 * valid bit is at the same position in both v0 and v1 Rx compl */
	if (compl->dw[offsetof(struct amap_eth_rx_compl_v1, valid) / 32] == 0)
		return NULL;

	rmb();
	be_dws_le_to_cpu(compl, sizeof(*compl));

	if (adapter->be3_native)
		be_parse_rx_compl_v1(compl, rxcp);
	else
		be_parse_rx_compl_v0(compl, rxcp);

	if (rxcp->vlanf) {
		/* vlanf could be wrongly set in some cards.
		 * ignore if vtm is not set */
		if ((adapter->function_mode & FLEX10_MODE) && !rxcp->vtm)
			rxcp->vlanf = 0;

		if (!lancer_chip(adapter))
			rxcp->vlan_tag = swab16(rxcp->vlan_tag);

		if (adapter->pvid == (rxcp->vlan_tag & VLAN_VID_MASK) &&
		    !adapter->vlan_tag[rxcp->vlan_tag])
			rxcp->vlanf = 0;
	}

	/* As the compl has been parsed, reset it; we wont touch it again */
	compl->dw[offsetof(struct amap_eth_rx_compl_v1, valid) / 32] = 0;

	queue_tail_inc(&rxo->cq);
	return rxcp;
}

static inline struct page *be_alloc_pages(u32 size, gfp_t gfp)
{
	u32 order = get_order(size);

	if (order > 0)
		gfp |= __GFP_COMP;
	return  alloc_pages(gfp, order);
}

/*
 * Allocate a page, split it to fragments of size rx_frag_size and post as
 * receive buffers to BE
 */
static void be_post_rx_frags(struct be_rx_obj *rxo, gfp_t gfp)
{
	struct be_adapter *adapter = rxo->adapter;
	struct be_rx_page_info *page_info = NULL, *prev_page_info = NULL;
	struct be_queue_info *rxq = &rxo->q;
	struct page *pagep = NULL;
	struct be_eth_rx_d *rxd;
	u64 page_dmaaddr = 0, frag_dmaaddr;
	u32 posted, page_offset = 0;

	page_info = &rxo->page_info_tbl[rxq->head];
	for (posted = 0; posted < MAX_RX_POST && !page_info->page; posted++) {
		if (!pagep) {
			pagep = be_alloc_pages(adapter->big_page_size, gfp);
			if (unlikely(!pagep)) {
				rx_stats(rxo)->rx_post_fail++;
				break;
			}
			page_dmaaddr = dma_map_page(&adapter->pdev->dev, pagep,
						    0, adapter->big_page_size,
						    DMA_FROM_DEVICE);
			page_info->page_offset = 0;
		} else {
			get_page(pagep);
			page_info->page_offset = page_offset + rx_frag_size;
		}
		page_offset = page_info->page_offset;
		page_info->page = pagep;
		dma_unmap_addr_set(page_info, bus, page_dmaaddr);
		frag_dmaaddr = page_dmaaddr + page_info->page_offset;

		rxd = queue_head_node(rxq);
		rxd->fragpa_lo = cpu_to_le32(frag_dmaaddr & 0xFFFFFFFF);
		rxd->fragpa_hi = cpu_to_le32(upper_32_bits(frag_dmaaddr));

		/* Any space left in the current big page for another frag? */
		if ((page_offset + rx_frag_size + rx_frag_size) >
					adapter->big_page_size) {
			pagep = NULL;
			page_info->last_page_user = true;
		}

		prev_page_info = page_info;
		queue_head_inc(rxq);
		page_info = &rxo->page_info_tbl[rxq->head];
	}
	if (pagep)
		prev_page_info->last_page_user = true;

	if (posted) {
		atomic_add(posted, &rxq->used);
		be_rxq_notify(adapter, rxq->id, posted);
	} else if (atomic_read(&rxq->used) == 0) {
		/* Let be_worker replenish when memory is available */
		rxo->rx_post_starved = true;
	}
}

static struct be_eth_tx_compl *be_tx_compl_get(struct be_queue_info *tx_cq)
{
	struct be_eth_tx_compl *txcp = queue_tail_node(tx_cq);

	if (txcp->dw[offsetof(struct amap_eth_tx_compl, valid) / 32] == 0)
		return NULL;

	rmb();
	be_dws_le_to_cpu(txcp, sizeof(*txcp));

	txcp->dw[offsetof(struct amap_eth_tx_compl, valid) / 32] = 0;

	queue_tail_inc(tx_cq);
	return txcp;
}

static u16 be_tx_compl_process(struct be_adapter *adapter,
		struct be_tx_obj *txo, u16 last_index)
{
	struct be_queue_info *txq = &txo->q;
	struct be_eth_wrb *wrb;
	struct sk_buff **sent_skbs = txo->sent_skb_list;
	struct sk_buff *sent_skb;
	u16 cur_index, num_wrbs = 1; /* account for hdr wrb */
	bool unmap_skb_hdr = true;

	sent_skb = sent_skbs[txq->tail];
	BUG_ON(!sent_skb);
	sent_skbs[txq->tail] = NULL;

	/* skip header wrb */
	queue_tail_inc(txq);

	do {
		cur_index = txq->tail;
		wrb = queue_tail_node(txq);
		unmap_tx_frag(&adapter->pdev->dev, wrb,
			      (unmap_skb_hdr && skb_headlen(sent_skb)));
		unmap_skb_hdr = false;

		num_wrbs++;
		queue_tail_inc(txq);
	} while (cur_index != last_index);

	kfree_skb(sent_skb);
	return num_wrbs;
}

/* Return the number of events in the event queue */
static inline int events_get(struct be_eq_obj *eqo)
{
	struct be_eq_entry *eqe;
	int num = 0;

	do {
		eqe = queue_tail_node(&eqo->q);
		if (eqe->evt == 0)
			break;

		rmb();
		eqe->evt = 0;
		num++;
		queue_tail_inc(&eqo->q);
	} while (true);

	return num;
}

static int event_handle(struct be_eq_obj *eqo)
{
	bool rearm = false;
	int num = events_get(eqo);

	/* Deal with any spurious interrupts that come without events */
	if (!num)
		rearm = true;

	if (num || msix_enabled(eqo->adapter))
		be_eq_notify(eqo->adapter, eqo->q.id, rearm, true, num);

	if (num)
		napi_schedule(&eqo->napi);

	return num;
}

/* Leaves the EQ is disarmed state */
static void be_eq_clean(struct be_eq_obj *eqo)
{
	int num = events_get(eqo);

	be_eq_notify(eqo->adapter, eqo->q.id, false, true, num);
}

static void be_rx_cq_clean(struct be_rx_obj *rxo)
{
	struct be_rx_page_info *page_info;
	struct be_queue_info *rxq = &rxo->q;
	struct be_queue_info *rx_cq = &rxo->cq;
	struct be_rx_compl_info *rxcp;
	u16 tail;

	/* First cleanup pending rx completions */
	while ((rxcp = be_rx_compl_get(rxo)) != NULL) {
		be_rx_compl_discard(rxo, rxcp);
		be_cq_notify(rxo->adapter, rx_cq->id, false, 1);
	}

	/* Then free posted rx buffer that were not used */
	tail = (rxq->head + rxq->len - atomic_read(&rxq->used)) % rxq->len;
	for (; atomic_read(&rxq->used) > 0; index_inc(&tail, rxq->len)) {
		page_info = get_rx_page_info(rxo, tail);
		put_page(page_info->page);
		memset(page_info, 0, sizeof(*page_info));
	}
	BUG_ON(atomic_read(&rxq->used));
	rxq->tail = rxq->head = 0;
}

static void be_tx_compl_clean(struct be_adapter *adapter)
{
	struct be_tx_obj *txo;
	struct be_queue_info *txq;
	struct be_eth_tx_compl *txcp;
	u16 end_idx, cmpl = 0, timeo = 0, num_wrbs = 0;
	struct sk_buff *sent_skb;
	bool dummy_wrb;
	int i, pending_txqs;

	/* Wait for a max of 200ms for all the tx-completions to arrive. */
	do {
		pending_txqs = adapter->num_tx_qs;

		for_all_tx_queues(adapter, txo, i) {
			txq = &txo->q;
			while ((txcp = be_tx_compl_get(&txo->cq))) {
				end_idx =
					AMAP_GET_BITS(struct amap_eth_tx_compl,
						      wrb_index, txcp);
				num_wrbs += be_tx_compl_process(adapter, txo,
								end_idx);
				cmpl++;
			}
			if (cmpl) {
				be_cq_notify(adapter, txo->cq.id, false, cmpl);
				atomic_sub(num_wrbs, &txq->used);
				cmpl = 0;
				num_wrbs = 0;
			}
			if (atomic_read(&txq->used) == 0)
				pending_txqs--;
		}

		if (pending_txqs == 0 || ++timeo > 200)
			break;

		mdelay(1);
	} while (true);

	for_all_tx_queues(adapter, txo, i) {
		txq = &txo->q;
		if (atomic_read(&txq->used))
			dev_err(&adapter->pdev->dev, "%d pending tx-compls\n",
				atomic_read(&txq->used));

		/* free posted tx for which compls will never arrive */
		while (atomic_read(&txq->used)) {
			sent_skb = txo->sent_skb_list[txq->tail];
			end_idx = txq->tail;
			num_wrbs = wrb_cnt_for_skb(adapter, sent_skb,
						   &dummy_wrb);
			index_adv(&end_idx, num_wrbs - 1, txq->len);
			num_wrbs = be_tx_compl_process(adapter, txo, end_idx);
			atomic_sub(num_wrbs, &txq->used);
		}
	}
}

static void be_evt_queues_destroy(struct be_adapter *adapter)
{
	struct be_eq_obj *eqo;
	int i;

	for_all_evt_queues(adapter, eqo, i) {
		if (eqo->q.created) {
			be_eq_clean(eqo);
			be_cmd_q_destroy(adapter, &eqo->q, QTYPE_EQ);
		}
		be_queue_free(adapter, &eqo->q);
	}
}

static int be_evt_queues_create(struct be_adapter *adapter)
{
	struct be_queue_info *eq;
	struct be_eq_obj *eqo;
	int i, rc;

	adapter->num_evt_qs = num_irqs(adapter);

	for_all_evt_queues(adapter, eqo, i) {
		eqo->adapter = adapter;
		eqo->tx_budget = BE_TX_BUDGET;
		eqo->idx = i;
		eqo->max_eqd = BE_MAX_EQD;
		eqo->enable_aic = true;

		eq = &eqo->q;
		rc = be_queue_alloc(adapter, eq, EVNT_Q_LEN,
					sizeof(struct be_eq_entry));
		if (rc)
			return rc;

		rc = be_cmd_eq_create(adapter, eq, eqo->cur_eqd);
		if (rc)
			return rc;
	}
	return 0;
}

static void be_mcc_queues_destroy(struct be_adapter *adapter)
{
	struct be_queue_info *q;

	q = &adapter->mcc_obj.q;
	if (q->created)
		be_cmd_q_destroy(adapter, q, QTYPE_MCCQ);
	be_queue_free(adapter, q);

	q = &adapter->mcc_obj.cq;
	if (q->created)
		be_cmd_q_destroy(adapter, q, QTYPE_CQ);
	be_queue_free(adapter, q);
}

/* Must be called only after TX qs are created as MCC shares TX EQ */
static int be_mcc_queues_create(struct be_adapter *adapter)
{
	struct be_queue_info *q, *cq;

	cq = &adapter->mcc_obj.cq;
	if (be_queue_alloc(adapter, cq, MCC_CQ_LEN,
			sizeof(struct be_mcc_compl)))
		goto err;

	/* Use the default EQ for MCC completions */
	if (be_cmd_cq_create(adapter, cq, &mcc_eqo(adapter)->q, true, 0))
		goto mcc_cq_free;

	q = &adapter->mcc_obj.q;
	if (be_queue_alloc(adapter, q, MCC_Q_LEN, sizeof(struct be_mcc_wrb)))
		goto mcc_cq_destroy;

	if (be_cmd_mccq_create(adapter, q, cq))
		goto mcc_q_free;

	return 0;

mcc_q_free:
	be_queue_free(adapter, q);
mcc_cq_destroy:
	be_cmd_q_destroy(adapter, cq, QTYPE_CQ);
mcc_cq_free:
	be_queue_free(adapter, cq);
err:
	return -1;
}

static void be_tx_queues_destroy(struct be_adapter *adapter)
{
	struct be_queue_info *q;
	struct be_tx_obj *txo;
	u8 i;

	for_all_tx_queues(adapter, txo, i) {
		q = &txo->q;
		if (q->created)
			be_cmd_q_destroy(adapter, q, QTYPE_TXQ);
		be_queue_free(adapter, q);

		q = &txo->cq;
		if (q->created)
			be_cmd_q_destroy(adapter, q, QTYPE_CQ);
		be_queue_free(adapter, q);
	}
}

static int be_num_txqs_want(struct be_adapter *adapter)
{
	if ((!lancer_chip(adapter) && sriov_want(adapter)) ||
	    be_is_mc(adapter) ||
	    (!lancer_chip(adapter) && !be_physfn(adapter)) ||
	    BE2_chip(adapter))
		return 1;
	else
		return adapter->max_tx_queues;
}

static int be_tx_cqs_create(struct be_adapter *adapter)
{
	struct be_queue_info *cq, *eq;
	int status;
	struct be_tx_obj *txo;
	u8 i;

	adapter->num_tx_qs = be_num_txqs_want(adapter);
	if (adapter->num_tx_qs != MAX_TX_QS) {
		rtnl_lock();
		netif_set_real_num_tx_queues(adapter->netdev,
			adapter->num_tx_qs);
		rtnl_unlock();
	}

	for_all_tx_queues(adapter, txo, i) {
		cq = &txo->cq;
		status = be_queue_alloc(adapter, cq, TX_CQ_LEN,
					sizeof(struct be_eth_tx_compl));
		if (status)
			return status;

		/* If num_evt_qs is less than num_tx_qs, then more than
		 * one txq share an eq
		 */
		eq = &adapter->eq_obj[i % adapter->num_evt_qs].q;
		status = be_cmd_cq_create(adapter, cq, eq, false, 3);
		if (status)
			return status;
	}
	return 0;
}

static int be_tx_qs_create(struct be_adapter *adapter)
{
	struct be_tx_obj *txo;
	int i, status;

	for_all_tx_queues(adapter, txo, i) {
		status = be_queue_alloc(adapter, &txo->q, TX_Q_LEN,
					sizeof(struct be_eth_wrb));
		if (status)
			return status;

		status = be_cmd_txq_create(adapter, &txo->q, &txo->cq);
		if (status)
			return status;
	}

	dev_info(&adapter->pdev->dev, "created %d TX queue(s)\n",
		 adapter->num_tx_qs);
	return 0;
}

static void be_rx_cqs_destroy(struct be_adapter *adapter)
{
	struct be_queue_info *q;
	struct be_rx_obj *rxo;
	int i;

	for_all_rx_queues(adapter, rxo, i) {
		q = &rxo->cq;
		if (q->created)
			be_cmd_q_destroy(adapter, q, QTYPE_CQ);
		be_queue_free(adapter, q);
	}
}

static int be_rx_cqs_create(struct be_adapter *adapter)
{
	struct be_queue_info *eq, *cq;
	struct be_rx_obj *rxo;
	int rc, i;

	/* We'll create as many RSS rings as there are irqs.
	 * But when there's only one irq there's no use creating RSS rings
	 */
	adapter->num_rx_qs = (num_irqs(adapter) > 1) ?
				num_irqs(adapter) + 1 : 1;
	if (adapter->num_rx_qs != MAX_RX_QS) {
		rtnl_lock();
		netif_set_real_num_rx_queues(adapter->netdev,
					     adapter->num_rx_qs);
		rtnl_unlock();
	}

	adapter->big_page_size = (1 << get_order(rx_frag_size)) * PAGE_SIZE;
	for_all_rx_queues(adapter, rxo, i) {
		rxo->adapter = adapter;
		cq = &rxo->cq;
		rc = be_queue_alloc(adapter, cq, RX_CQ_LEN,
				sizeof(struct be_eth_rx_compl));
		if (rc)
			return rc;

		eq = &adapter->eq_obj[i % adapter->num_evt_qs].q;
		rc = be_cmd_cq_create(adapter, cq, eq, false, 3);
		if (rc)
			return rc;
	}

	dev_info(&adapter->pdev->dev,
		 "created %d RSS queue(s) and 1 default RX queue\n",
		 adapter->num_rx_qs - 1);
	return 0;
}

static irqreturn_t be_intx(int irq, void *dev)
{
	struct be_adapter *adapter = dev;
	int num_evts;

	/* With INTx only one EQ is used */
	num_evts = event_handle(&adapter->eq_obj[0]);
	if (num_evts)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static irqreturn_t be_msix(int irq, void *dev)
{
	struct be_eq_obj *eqo = dev;

	event_handle(eqo);
	return IRQ_HANDLED;
}

static inline bool do_gro(struct be_rx_compl_info *rxcp)
{
	return (rxcp->tcpf && !rxcp->err) ? true : false;
}

static int be_process_rx(struct be_rx_obj *rxo, struct napi_struct *napi,
			int budget)
{
	struct be_adapter *adapter = rxo->adapter;
	struct be_queue_info *rx_cq = &rxo->cq;
	struct be_rx_compl_info *rxcp;
	u32 work_done;

	for (work_done = 0; work_done < budget; work_done++) {
		rxcp = be_rx_compl_get(rxo);
		if (!rxcp)
			break;

		/* Is it a flush compl that has no data */
		if (unlikely(rxcp->num_rcvd == 0))
			goto loop_continue;

		/* Discard compl with partial DMA Lancer B0 */
		if (unlikely(!rxcp->pkt_size)) {
			be_rx_compl_discard(rxo, rxcp);
			goto loop_continue;
		}

		/* On BE drop pkts that arrive due to imperfect filtering in
		 * promiscuous mode on some skews
		 */
		if (unlikely(rxcp->port != adapter->port_num &&
				!lancer_chip(adapter))) {
			be_rx_compl_discard(rxo, rxcp);
			goto loop_continue;
		}

		if (do_gro(rxcp))
			be_rx_compl_process_gro(rxo, napi, rxcp);
		else
			be_rx_compl_process(rxo, rxcp);
loop_continue:
		be_rx_stats_update(rxo, rxcp);
	}

	if (work_done) {
		be_cq_notify(adapter, rx_cq->id, true, work_done);

		if (atomic_read(&rxo->q.used) < RX_FRAGS_REFILL_WM)
			be_post_rx_frags(rxo, GFP_ATOMIC);
	}

	return work_done;
}

static bool be_process_tx(struct be_adapter *adapter, struct be_tx_obj *txo,
			  int budget, int idx)
{
	struct be_eth_tx_compl *txcp;
	int num_wrbs = 0, work_done;

	for (work_done = 0; work_done < budget; work_done++) {
		txcp = be_tx_compl_get(&txo->cq);
		if (!txcp)
			break;
		num_wrbs += be_tx_compl_process(adapter, txo,
				AMAP_GET_BITS(struct amap_eth_tx_compl,
					wrb_index, txcp));
	}

	if (work_done) {
		be_cq_notify(adapter, txo->cq.id, true, work_done);
		atomic_sub(num_wrbs, &txo->q.used);

		/* As Tx wrbs have been freed up, wake up netdev queue
		 * if it was stopped due to lack of tx wrbs.  */
		if (__netif_subqueue_stopped(adapter->netdev, idx) &&
			atomic_read(&txo->q.used) < txo->q.len / 2) {
			netif_wake_subqueue(adapter->netdev, idx);
		}

		u64_stats_update_begin(&tx_stats(txo)->sync_compl);
		tx_stats(txo)->tx_compl += work_done;
		u64_stats_update_end(&tx_stats(txo)->sync_compl);
	}
	return (work_done < budget); /* Done */
}

int be_poll(struct napi_struct *napi, int budget)
{
	struct be_eq_obj *eqo = container_of(napi, struct be_eq_obj, napi);
	struct be_adapter *adapter = eqo->adapter;
	int max_work = 0, work, i;
	bool tx_done;

	/* Process all TXQs serviced by this EQ */
	for (i = eqo->idx; i < adapter->num_tx_qs; i += adapter->num_evt_qs) {
		tx_done = be_process_tx(adapter, &adapter->tx_obj[i],
					eqo->tx_budget, i);
		if (!tx_done)
			max_work = budget;
	}

	/* This loop will iterate twice for EQ0 in which
	 * completions of the last RXQ (default one) are also processed
	 * For other EQs the loop iterates only once
	 */
	for (i = eqo->idx; i < adapter->num_rx_qs; i += adapter->num_evt_qs) {
		work = be_process_rx(&adapter->rx_obj[i], napi, budget);
		max_work = max(work, max_work);
	}

	if (is_mcc_eqo(eqo))
		be_process_mcc(adapter);

	if (max_work < budget) {
		napi_complete(napi);
		be_eq_notify(adapter, eqo->q.id, true, false, 0);
	} else {
		/* As we'll continue in polling mode, count and clear events */
		be_eq_notify(adapter, eqo->q.id, false, false, events_get(eqo));
	}
	return max_work;
}

void be_detect_error(struct be_adapter *adapter)
{
	u32 ue_lo = 0, ue_hi = 0, ue_lo_mask = 0, ue_hi_mask = 0;
	u32 sliport_status = 0, sliport_err1 = 0, sliport_err2 = 0;
	u32 i;

	if (be_crit_error(adapter))
		return;

	if (lancer_chip(adapter)) {
		sliport_status = ioread32(adapter->db + SLIPORT_STATUS_OFFSET);
		if (sliport_status & SLIPORT_STATUS_ERR_MASK) {
			sliport_err1 = ioread32(adapter->db +
					SLIPORT_ERROR1_OFFSET);
			sliport_err2 = ioread32(adapter->db +
					SLIPORT_ERROR2_OFFSET);
		}
	} else {
		pci_read_config_dword(adapter->pdev,
				PCICFG_UE_STATUS_LOW, &ue_lo);
		pci_read_config_dword(adapter->pdev,
				PCICFG_UE_STATUS_HIGH, &ue_hi);
		pci_read_config_dword(adapter->pdev,
				PCICFG_UE_STATUS_LOW_MASK, &ue_lo_mask);
		pci_read_config_dword(adapter->pdev,
				PCICFG_UE_STATUS_HI_MASK, &ue_hi_mask);

		ue_lo = (ue_lo & ~ue_lo_mask);
		ue_hi = (ue_hi & ~ue_hi_mask);
	}

	/* On certain platforms BE hardware can indicate spurious UEs.
	 * Allow the h/w to stop working completely in case of a real UE.
	 * Hence not setting the hw_error for UE detection.
	 */
	if (sliport_status & SLIPORT_STATUS_ERR_MASK) {
		adapter->hw_error = true;
		dev_err(&adapter->pdev->dev,
			"Error detected in the card\n");
	}

	if (sliport_status & SLIPORT_STATUS_ERR_MASK) {
		dev_err(&adapter->pdev->dev,
			"ERR: sliport status 0x%x\n", sliport_status);
		dev_err(&adapter->pdev->dev,
			"ERR: sliport error1 0x%x\n", sliport_err1);
		dev_err(&adapter->pdev->dev,
			"ERR: sliport error2 0x%x\n", sliport_err2);
	}

	if (ue_lo) {
		for (i = 0; ue_lo; ue_lo >>= 1, i++) {
			if (ue_lo & 1)
				dev_err(&adapter->pdev->dev,
				"UE: %s bit set\n", ue_status_low_desc[i]);
		}
	}

	if (ue_hi) {
		for (i = 0; ue_hi; ue_hi >>= 1, i++) {
			if (ue_hi & 1)
				dev_err(&adapter->pdev->dev,
				"UE: %s bit set\n", ue_status_hi_desc[i]);
		}
	}

}

static void be_msix_disable(struct be_adapter *adapter)
{
	if (msix_enabled(adapter)) {
		pci_disable_msix(adapter->pdev);
		adapter->num_msix_vec = 0;
	}
}

static uint be_num_rss_want(struct be_adapter *adapter)
{
	u32 num = 0;

	if ((adapter->function_caps & BE_FUNCTION_CAPS_RSS) &&
	    (lancer_chip(adapter) ||
	     (!sriov_want(adapter) && be_physfn(adapter)))) {
		num = adapter->max_rss_queues;
		num = min_t(u32, num, (u32)netif_get_num_default_rss_queues());
	}
	return num;
}

static void be_msix_enable(struct be_adapter *adapter)
{
#define BE_MIN_MSIX_VECTORS		1
	int i, status, num_vec, num_roce_vec = 0;
	struct device *dev = &adapter->pdev->dev;

	/* If RSS queues are not used, need a vec for default RX Q */
	num_vec = min(be_num_rss_want(adapter), num_online_cpus());
	if (be_roce_supported(adapter)) {
		num_roce_vec = min_t(u32, MAX_ROCE_MSIX_VECTORS,
					(num_online_cpus() + 1));
		num_roce_vec = min(num_roce_vec, MAX_ROCE_EQS);
		num_vec += num_roce_vec;
		num_vec = min(num_vec, MAX_MSIX_VECTORS);
	}
	num_vec = max(num_vec, BE_MIN_MSIX_VECTORS);

	for (i = 0; i < num_vec; i++)
		adapter->msix_entries[i].entry = i;

	status = pci_enable_msix(adapter->pdev, adapter->msix_entries, num_vec);
	if (status == 0) {
		goto done;
	} else if (status >= BE_MIN_MSIX_VECTORS) {
		num_vec = status;
		if (pci_enable_msix(adapter->pdev, adapter->msix_entries,
				num_vec) == 0)
			goto done;
	}

	dev_warn(dev, "MSIx enable failed\n");
	return;
done:
	if (be_roce_supported(adapter)) {
		if (num_vec > num_roce_vec) {
			adapter->num_msix_vec = num_vec - num_roce_vec;
			adapter->num_msix_roce_vec =
				num_vec - adapter->num_msix_vec;
		} else {
			adapter->num_msix_vec = num_vec;
			adapter->num_msix_roce_vec = 0;
		}
	} else
		adapter->num_msix_vec = num_vec;
	dev_info(dev, "enabled %d MSI-x vector(s)\n", adapter->num_msix_vec);
	return;
}

static inline int be_msix_vec_get(struct be_adapter *adapter,
				struct be_eq_obj *eqo)
{
	return adapter->msix_entries[eqo->idx].vector;
}

static int be_msix_register(struct be_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct be_eq_obj *eqo;
	int status, i, vec;

	for_all_evt_queues(adapter, eqo, i) {
		sprintf(eqo->desc, "%s-q%d", netdev->name, i);
		vec = be_msix_vec_get(adapter, eqo);
		status = request_irq(vec, be_msix, 0, eqo->desc, eqo);
		if (status)
			goto err_msix;
	}

	return 0;
err_msix:
	for (i--, eqo = &adapter->eq_obj[i]; i >= 0; i--, eqo--)
		free_irq(be_msix_vec_get(adapter, eqo), eqo);
	dev_warn(&adapter->pdev->dev, "MSIX Request IRQ failed - err %d\n",
		status);
	be_msix_disable(adapter);
	return status;
}

static int be_irq_register(struct be_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int status;

	if (msix_enabled(adapter)) {
		status = be_msix_register(adapter);
		if (status == 0)
			goto done;
		/* INTx is not supported for VF */
		if (!be_physfn(adapter))
			return status;
	}

	/* INTx */
	netdev->irq = adapter->pdev->irq;
	status = request_irq(netdev->irq, be_intx, IRQF_SHARED, netdev->name,
			adapter);
	if (status) {
		dev_err(&adapter->pdev->dev,
			"INTx request IRQ failed - err %d\n", status);
		return status;
	}
done:
	adapter->isr_registered = true;
	return 0;
}

static void be_irq_unregister(struct be_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct be_eq_obj *eqo;
	int i;

	if (!adapter->isr_registered)
		return;

	/* INTx */
	if (!msix_enabled(adapter)) {
		free_irq(netdev->irq, adapter);
		goto done;
	}

	/* MSIx */
	for_all_evt_queues(adapter, eqo, i)
		free_irq(be_msix_vec_get(adapter, eqo), eqo);

done:
	adapter->isr_registered = false;
}

static void be_rx_qs_destroy(struct be_adapter *adapter)
{
	struct be_queue_info *q;
	struct be_rx_obj *rxo;
	int i;

	for_all_rx_queues(adapter, rxo, i) {
		q = &rxo->q;
		if (q->created) {
			be_cmd_rxq_destroy(adapter, q);
			/* After the rxq is invalidated, wait for a grace time
			 * of 1ms for all dma to end and the flush compl to
			 * arrive
			 */
			mdelay(1);
			be_rx_cq_clean(rxo);
		}
		be_queue_free(adapter, q);
	}
}

static int be_close(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_eq_obj *eqo;
	int i;

	be_roce_dev_close(adapter);

	be_async_mcc_disable(adapter);

	if (!lancer_chip(adapter))
		be_intr_set(adapter, false);

	for_all_evt_queues(adapter, eqo, i) {
		napi_disable(&eqo->napi);
		if (msix_enabled(adapter))
			synchronize_irq(be_msix_vec_get(adapter, eqo));
		else
			synchronize_irq(netdev->irq);
		be_eq_clean(eqo);
	}

	be_irq_unregister(adapter);

	/* Wait for all pending tx completions to arrive so that
	 * all tx skbs are freed.
	 */
	be_tx_compl_clean(adapter);

	be_rx_qs_destroy(adapter);
	return 0;
}

static int be_rx_qs_create(struct be_adapter *adapter)
{
	struct be_rx_obj *rxo;
	int rc, i, j;
	u8 rsstable[128];

	for_all_rx_queues(adapter, rxo, i) {
		rc = be_queue_alloc(adapter, &rxo->q, RX_Q_LEN,
				    sizeof(struct be_eth_rx_d));
		if (rc)
			return rc;
	}

	/* The FW would like the default RXQ to be created first */
	rxo = default_rxo(adapter);
	rc = be_cmd_rxq_create(adapter, &rxo->q, rxo->cq.id, rx_frag_size,
			       adapter->if_handle, false, &rxo->rss_id);
	if (rc)
		return rc;

	for_all_rss_queues(adapter, rxo, i) {
		rc = be_cmd_rxq_create(adapter, &rxo->q, rxo->cq.id,
				       rx_frag_size, adapter->if_handle,
				       true, &rxo->rss_id);
		if (rc)
			return rc;
	}

	if (be_multi_rxq(adapter)) {
		for (j = 0; j < 128; j += adapter->num_rx_qs - 1) {
			for_all_rss_queues(adapter, rxo, i) {
				if ((j + i) >= 128)
					break;
				rsstable[j + i] = rxo->rss_id;
			}
		}
		rc = be_cmd_rss_config(adapter, rsstable, 128);
		if (rc)
			return rc;
	}

	/* First time posting */
	for_all_rx_queues(adapter, rxo, i)
		be_post_rx_frags(rxo, GFP_KERNEL);
	return 0;
}

static int be_open(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_eq_obj *eqo;
	struct be_rx_obj *rxo;
	struct be_tx_obj *txo;
	u8 link_status;
	int status, i;

	status = be_rx_qs_create(adapter);
	if (status)
		goto err;

	be_irq_register(adapter);

	if (!lancer_chip(adapter))
		be_intr_set(adapter, true);

	for_all_rx_queues(adapter, rxo, i)
		be_cq_notify(adapter, rxo->cq.id, true, 0);

	for_all_tx_queues(adapter, txo, i)
		be_cq_notify(adapter, txo->cq.id, true, 0);

	be_async_mcc_enable(adapter);

	for_all_evt_queues(adapter, eqo, i) {
		napi_enable(&eqo->napi);
		be_eq_notify(adapter, eqo->q.id, true, false, 0);
	}

	status = be_cmd_link_status_query(adapter, NULL, &link_status, 0);
	if (!status)
		be_link_status_update(adapter, link_status);

	be_roce_dev_open(adapter);
	return 0;
err:
	be_close(adapter->netdev);
	return -EIO;
}

static int be_setup_wol(struct be_adapter *adapter, bool enable)
{
	struct be_dma_mem cmd;
	int status = 0;
	u8 mac[ETH_ALEN];

	memset(mac, 0, ETH_ALEN);

	cmd.size = sizeof(struct be_cmd_req_acpi_wol_magic_config);
	cmd.va = dma_alloc_coherent(&adapter->pdev->dev, cmd.size, &cmd.dma,
				    GFP_KERNEL);
	if (cmd.va == NULL)
		return -1;
	memset(cmd.va, 0, cmd.size);

	if (enable) {
		status = pci_write_config_dword(adapter->pdev,
			PCICFG_PM_CONTROL_OFFSET, PCICFG_PM_CONTROL_MASK);
		if (status) {
			dev_err(&adapter->pdev->dev,
				"Could not enable Wake-on-lan\n");
			dma_free_coherent(&adapter->pdev->dev, cmd.size, cmd.va,
					  cmd.dma);
			return status;
		}
		status = be_cmd_enable_magic_wol(adapter,
				adapter->netdev->dev_addr, &cmd);
		pci_enable_wake(adapter->pdev, PCI_D3hot, 1);
		pci_enable_wake(adapter->pdev, PCI_D3cold, 1);
	} else {
		status = be_cmd_enable_magic_wol(adapter, mac, &cmd);
		pci_enable_wake(adapter->pdev, PCI_D3hot, 0);
		pci_enable_wake(adapter->pdev, PCI_D3cold, 0);
	}

	dma_free_coherent(&adapter->pdev->dev, cmd.size, cmd.va, cmd.dma);
	return status;
}

/*
 * Generate a seed MAC address from the PF MAC Address using jhash.
 * MAC Address for VFs are assigned incrementally starting from the seed.
 * These addresses are programmed in the ASIC by the PF and the VF driver
 * queries for the MAC address during its probe.
 */
static inline int be_vf_eth_addr_config(struct be_adapter *adapter)
{
	u32 vf;
	int status = 0;
	u8 mac[ETH_ALEN];
	struct be_vf_cfg *vf_cfg;

	be_vf_eth_addr_generate(adapter, mac);

	for_all_vfs(adapter, vf_cfg, vf) {
		if (lancer_chip(adapter)) {
			status = be_cmd_set_mac_list(adapter,  mac, 1, vf + 1);
		} else {
			status = be_cmd_pmac_add(adapter, mac,
						 vf_cfg->if_handle,
						 &vf_cfg->pmac_id, vf + 1);
		}

		if (status)
			dev_err(&adapter->pdev->dev,
			"Mac address assignment failed for VF %d\n", vf);
		else
			memcpy(vf_cfg->mac_addr, mac, ETH_ALEN);

		mac[5] += 1;
	}
	return status;
}

static void be_vf_clear(struct be_adapter *adapter)
{
	struct be_vf_cfg *vf_cfg;
	u32 vf;

	if (be_find_vfs(adapter, ASSIGNED)) {
		dev_warn(&adapter->pdev->dev, "VFs are assigned to VMs\n");
		goto done;
	}

	for_all_vfs(adapter, vf_cfg, vf) {
		if (lancer_chip(adapter))
			be_cmd_set_mac_list(adapter, NULL, 0, vf + 1);
		else
			be_cmd_pmac_del(adapter, vf_cfg->if_handle,
					vf_cfg->pmac_id, vf + 1);

		be_cmd_if_destroy(adapter, vf_cfg->if_handle, vf + 1);
	}
	pci_disable_sriov(adapter->pdev);
done:
	kfree(adapter->vf_cfg);
	adapter->num_vfs = 0;
}

static int be_clear(struct be_adapter *adapter)
{
	int i = 1;

	if (adapter->flags & BE_FLAGS_WORKER_SCHEDULED) {
		cancel_delayed_work_sync(&adapter->work);
		adapter->flags &= ~BE_FLAGS_WORKER_SCHEDULED;
	}

	if (sriov_enabled(adapter))
		be_vf_clear(adapter);

	for (; adapter->uc_macs > 0; adapter->uc_macs--, i++)
		be_cmd_pmac_del(adapter, adapter->if_handle,
			adapter->pmac_id[i], 0);

	be_cmd_if_destroy(adapter, adapter->if_handle,  0);

	be_mcc_queues_destroy(adapter);
	be_rx_cqs_destroy(adapter);
	be_tx_queues_destroy(adapter);
	be_evt_queues_destroy(adapter);

	kfree(adapter->pmac_id);
	adapter->pmac_id = NULL;

	be_msix_disable(adapter);
	return 0;
}

static void be_get_vf_if_cap_flags(struct be_adapter *adapter,
				   u32 *cap_flags, u8 domain)
{
	bool profile_present = false;
	int status;

	if (lancer_chip(adapter)) {
		status = be_cmd_get_profile_config(adapter, cap_flags, domain);
		if (!status)
			profile_present = true;
	}

	if (!profile_present)
		*cap_flags = BE_IF_FLAGS_UNTAGGED | BE_IF_FLAGS_BROADCAST |
			     BE_IF_FLAGS_MULTICAST;
}

static int be_vf_setup_init(struct be_adapter *adapter)
{
	struct be_vf_cfg *vf_cfg;
	int vf;

	adapter->vf_cfg = kcalloc(adapter->num_vfs, sizeof(*vf_cfg),
				  GFP_KERNEL);
	if (!adapter->vf_cfg)
		return -ENOMEM;

	for_all_vfs(adapter, vf_cfg, vf) {
		vf_cfg->if_handle = -1;
		vf_cfg->pmac_id = -1;
	}
	return 0;
}

static int be_vf_setup(struct be_adapter *adapter)
{
	struct be_vf_cfg *vf_cfg;
	struct device *dev = &adapter->pdev->dev;
	u32 cap_flags, en_flags, vf;
	u16 def_vlan, lnk_speed;
	int status, enabled_vfs;

	enabled_vfs = be_find_vfs(adapter, ENABLED);
	if (enabled_vfs) {
		dev_warn(dev, "%d VFs are already enabled\n", enabled_vfs);
		dev_warn(dev, "Ignoring num_vfs=%d setting\n", num_vfs);
		return 0;
	}

	if (num_vfs > adapter->dev_num_vfs) {
		dev_warn(dev, "Device supports %d VFs and not %d\n",
			 adapter->dev_num_vfs, num_vfs);
		num_vfs = adapter->dev_num_vfs;
	}

	status = pci_enable_sriov(adapter->pdev, num_vfs);
	if (!status) {
		adapter->num_vfs = num_vfs;
	} else {
		/* Platform doesn't support SRIOV though device supports it */
		dev_warn(dev, "SRIOV enable failed\n");
		return 0;
	}

	status = be_vf_setup_init(adapter);
	if (status)
		goto err;

	for_all_vfs(adapter, vf_cfg, vf) {
		be_get_vf_if_cap_flags(adapter, &cap_flags, vf + 1);

		en_flags = cap_flags & (BE_IF_FLAGS_UNTAGGED |
					BE_IF_FLAGS_BROADCAST |
					BE_IF_FLAGS_MULTICAST);

		status = be_cmd_if_create(adapter, cap_flags, en_flags,
					  &vf_cfg->if_handle, vf + 1);
		if (status)
			goto err;
	}

	if (!enabled_vfs) {
		status = be_vf_eth_addr_config(adapter);
		if (status)
			goto err;
	}

	for_all_vfs(adapter, vf_cfg, vf) {
		lnk_speed = 1000;
		status = be_cmd_set_qos(adapter, lnk_speed, vf + 1);
		if (status)
			goto err;
		vf_cfg->tx_rate = lnk_speed * 10;

		status = be_cmd_get_hsw_config(adapter, &def_vlan,
				vf + 1, vf_cfg->if_handle);
		if (status)
			goto err;
		vf_cfg->def_vid = def_vlan;

		be_cmd_enable_vf(adapter, vf + 1);
	}
	return 0;
err:
	return status;
}

static void be_setup_init(struct be_adapter *adapter)
{
	adapter->vlan_prio_bmap = 0xff;
	adapter->phy.link_speed = -1;
	adapter->if_handle = -1;
	adapter->be3_native = false;
	adapter->promiscuous = false;
	if (be_physfn(adapter))
		adapter->cmd_privileges = MAX_PRIVILEGES;
	else
		adapter->cmd_privileges = MIN_PRIVILEGES;
}

static int be_get_mac_addr(struct be_adapter *adapter, u8 *mac, u32 if_handle,
			   bool *active_mac, u32 *pmac_id)
{
	int status = 0;

	if (!is_zero_ether_addr(adapter->netdev->perm_addr)) {
		memcpy(mac, adapter->netdev->dev_addr, ETH_ALEN);
		if (!lancer_chip(adapter) && !be_physfn(adapter))
			*active_mac = true;
		else
			*active_mac = false;

		return status;
	}

	if (lancer_chip(adapter)) {
		status = be_cmd_get_mac_from_list(adapter, mac,
						  active_mac, pmac_id, 0);
		if (*active_mac) {
			status = be_cmd_mac_addr_query(adapter, mac, false,
						       if_handle, *pmac_id);
		}
	} else if (be_physfn(adapter)) {
		/* For BE3, for PF get permanent MAC */
		status = be_cmd_mac_addr_query(adapter, mac, true, 0, 0);
		*active_mac = false;
	} else {
		/* For BE3, for VF get soft MAC assigned by PF*/
		status = be_cmd_mac_addr_query(adapter, mac, false,
					       if_handle, 0);
		*active_mac = true;
	}
	return status;
}

static void be_get_resources(struct be_adapter *adapter)
{
	int status;
	bool profile_present = false;

	if (lancer_chip(adapter)) {
		status = be_cmd_get_func_config(adapter);

		if (!status)
			profile_present = true;
	}

	if (profile_present) {
		/* Sanity fixes for Lancer */
		adapter->max_pmac_cnt = min_t(u16, adapter->max_pmac_cnt,
					      BE_UC_PMAC_COUNT);
		adapter->max_vlans = min_t(u16, adapter->max_vlans,
					   BE_NUM_VLANS_SUPPORTED);
		adapter->max_mcast_mac = min_t(u16, adapter->max_mcast_mac,
					       BE_MAX_MC);
		adapter->max_tx_queues = min_t(u16, adapter->max_tx_queues,
					       MAX_TX_QS);
		adapter->max_rss_queues = min_t(u16, adapter->max_rss_queues,
						BE3_MAX_RSS_QS);
		adapter->max_event_queues = min_t(u16,
						  adapter->max_event_queues,
						  BE3_MAX_RSS_QS);

		if (adapter->max_rss_queues &&
		    adapter->max_rss_queues == adapter->max_rx_queues)
			adapter->max_rss_queues -= 1;

		if (adapter->max_event_queues < adapter->max_rss_queues)
			adapter->max_rss_queues = adapter->max_event_queues;

	} else {
		if (be_physfn(adapter))
			adapter->max_pmac_cnt = BE_UC_PMAC_COUNT;
		else
			adapter->max_pmac_cnt = BE_VF_UC_PMAC_COUNT;

		if (adapter->function_mode & FLEX10_MODE)
			adapter->max_vlans = BE_NUM_VLANS_SUPPORTED/8;
		else
			adapter->max_vlans = BE_NUM_VLANS_SUPPORTED;

		adapter->max_mcast_mac = BE_MAX_MC;
		adapter->max_tx_queues = MAX_TX_QS;
		adapter->max_rss_queues = (adapter->be3_native) ?
					   BE3_MAX_RSS_QS : BE2_MAX_RSS_QS;
		adapter->max_event_queues = BE3_MAX_RSS_QS;

		adapter->if_cap_flags = BE_IF_FLAGS_UNTAGGED |
					BE_IF_FLAGS_BROADCAST |
					BE_IF_FLAGS_MULTICAST |
					BE_IF_FLAGS_PASS_L3L4_ERRORS |
					BE_IF_FLAGS_MCAST_PROMISCUOUS |
					BE_IF_FLAGS_VLAN_PROMISCUOUS |
					BE_IF_FLAGS_PROMISCUOUS;

		if (adapter->function_caps & BE_FUNCTION_CAPS_RSS)
			adapter->if_cap_flags |= BE_IF_FLAGS_RSS;
	}
}

/* Routine to query per function resource limits */
static int be_get_config(struct be_adapter *adapter)
{
	int pos, status;
	u16 dev_num_vfs;

	status = be_cmd_query_fw_cfg(adapter, &adapter->port_num,
				     &adapter->function_mode,
				     &adapter->function_caps);
	if (status)
		goto err;

	be_get_resources(adapter);

	/* primary mac needs 1 pmac entry */
	adapter->pmac_id = kcalloc(adapter->max_pmac_cnt + 1,
				   sizeof(u32), GFP_KERNEL);
	if (!adapter->pmac_id) {
		status = -ENOMEM;
		goto err;
	}

	pos = pci_find_ext_capability(adapter->pdev, PCI_EXT_CAP_ID_SRIOV);
	if (pos) {
		pci_read_config_word(adapter->pdev, pos + PCI_SRIOV_TOTAL_VF,
				     &dev_num_vfs);
		if (!lancer_chip(adapter))
			dev_num_vfs = min_t(u16, dev_num_vfs, MAX_VFS);
		adapter->dev_num_vfs = dev_num_vfs;
	}
err:
	return status;
}

static int be_setup(struct be_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	u32 en_flags;
	u32 tx_fc, rx_fc;
	int status;
	u8 mac[ETH_ALEN];
	bool active_mac;

	be_setup_init(adapter);

	if (!lancer_chip(adapter))
		be_cmd_req_native_mode(adapter);

	status = be_get_config(adapter);
	if (status)
		goto err;

	be_msix_enable(adapter);

	status = be_evt_queues_create(adapter);
	if (status)
		goto err;

	status = be_tx_cqs_create(adapter);
	if (status)
		goto err;

	status = be_rx_cqs_create(adapter);
	if (status)
		goto err;

	status = be_mcc_queues_create(adapter);
	if (status)
		goto err;

	be_cmd_get_fn_privileges(adapter, &adapter->cmd_privileges, 0);
	/* In UMC mode FW does not return right privileges.
	 * Override with correct privilege equivalent to PF.
	 */
	if (be_is_mc(adapter))
		adapter->cmd_privileges = MAX_PRIVILEGES;

	en_flags = BE_IF_FLAGS_UNTAGGED | BE_IF_FLAGS_BROADCAST |
			BE_IF_FLAGS_MULTICAST | BE_IF_FLAGS_PASS_L3L4_ERRORS;

	if (adapter->function_caps & BE_FUNCTION_CAPS_RSS)
		en_flags |= BE_IF_FLAGS_RSS;

	en_flags = en_flags & adapter->if_cap_flags;

	status = be_cmd_if_create(adapter, adapter->if_cap_flags, en_flags,
				  &adapter->if_handle, 0);
	if (status != 0)
		goto err;

	memset(mac, 0, ETH_ALEN);
	active_mac = false;
	status = be_get_mac_addr(adapter, mac, adapter->if_handle,
				 &active_mac, &adapter->pmac_id[0]);
	if (status != 0)
		goto err;

	if (!active_mac) {
		status = be_cmd_pmac_add(adapter, mac, adapter->if_handle,
					 &adapter->pmac_id[0], 0);
		if (status != 0)
			goto err;
	}

	if (is_zero_ether_addr(adapter->netdev->dev_addr)) {
		memcpy(adapter->netdev->dev_addr, mac, ETH_ALEN);
		memcpy(adapter->netdev->perm_addr, mac, ETH_ALEN);
	}

	status = be_tx_qs_create(adapter);
	if (status)
		goto err;

	be_cmd_get_fw_ver(adapter, adapter->fw_ver, NULL);

	if (adapter->vlans_added)
		be_vid_config(adapter);

	be_set_rx_mode(adapter->netdev);

	be_cmd_get_flow_control(adapter, &tx_fc, &rx_fc);

	if (rx_fc != adapter->rx_fc || tx_fc != adapter->tx_fc)
		be_cmd_set_flow_control(adapter, adapter->tx_fc,
					adapter->rx_fc);

	if (be_physfn(adapter) && num_vfs) {
		if (adapter->dev_num_vfs)
			be_vf_setup(adapter);
		else
			dev_warn(dev, "device doesn't support SRIOV\n");
	}

	status = be_cmd_get_phy_info(adapter);
	if (!status && be_pause_supported(adapter))
		adapter->phy.fc_autoneg = 1;

	schedule_delayed_work(&adapter->work, msecs_to_jiffies(1000));
	adapter->flags |= BE_FLAGS_WORKER_SCHEDULED;
	return 0;
err:
	be_clear(adapter);
	return status;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void be_netpoll(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_eq_obj *eqo;
	int i;

	for_all_evt_queues(adapter, eqo, i)
		event_handle(eqo);

	return;
}
#endif

#define FW_FILE_HDR_SIGN 	"ServerEngines Corp. "
char flash_cookie[2][16] =      {"*** SE FLAS", "H DIRECTORY *** "};

static bool be_flash_redboot(struct be_adapter *adapter,
			const u8 *p, u32 img_start, int image_size,
			int hdr_size)
{
	u32 crc_offset;
	u8 flashed_crc[4];
	int status;

	crc_offset = hdr_size + img_start + image_size - 4;

	p += crc_offset;

	status = be_cmd_get_flash_crc(adapter, flashed_crc,
			(image_size - 4));
	if (status) {
		dev_err(&adapter->pdev->dev,
		"could not get crc from flash, not flashing redboot\n");
		return false;
	}

	/*update redboot only if crc does not match*/
	if (!memcmp(flashed_crc, p, 4))
		return false;
	else
		return true;
}

static bool phy_flashing_required(struct be_adapter *adapter)
{
	return (adapter->phy.phy_type == TN_8022 &&
		adapter->phy.interface_type == PHY_TYPE_BASET_10GB);
}

static bool is_comp_in_ufi(struct be_adapter *adapter,
			   struct flash_section_info *fsec, int type)
{
	int i = 0, img_type = 0;
	struct flash_section_info_g2 *fsec_g2 = NULL;

	if (BE2_chip(adapter))
		fsec_g2 = (struct flash_section_info_g2 *)fsec;

	for (i = 0; i < MAX_FLASH_COMP; i++) {
		if (fsec_g2)
			img_type = le32_to_cpu(fsec_g2->fsec_entry[i].type);
		else
			img_type = le32_to_cpu(fsec->fsec_entry[i].type);

		if (img_type == type)
			return true;
	}
	return false;

}

struct flash_section_info *get_fsec_info(struct be_adapter *adapter,
					 int header_size,
					 const struct firmware *fw)
{
	struct flash_section_info *fsec = NULL;
	const u8 *p = fw->data;

	p += header_size;
	while (p < (fw->data + fw->size)) {
		fsec = (struct flash_section_info *)p;
		if (!memcmp(flash_cookie, fsec->cookie, sizeof(flash_cookie)))
			return fsec;
		p += 32;
	}
	return NULL;
}

static int be_flash(struct be_adapter *adapter, const u8 *img,
		struct be_dma_mem *flash_cmd, int optype, int img_size)
{
	u32 total_bytes = 0, flash_op, num_bytes = 0;
	int status = 0;
	struct be_cmd_write_flashrom *req = flash_cmd->va;

	total_bytes = img_size;
	while (total_bytes) {
		num_bytes = min_t(u32, 32*1024, total_bytes);

		total_bytes -= num_bytes;

		if (!total_bytes) {
			if (optype == OPTYPE_PHY_FW)
				flash_op = FLASHROM_OPER_PHY_FLASH;
			else
				flash_op = FLASHROM_OPER_FLASH;
		} else {
			if (optype == OPTYPE_PHY_FW)
				flash_op = FLASHROM_OPER_PHY_SAVE;
			else
				flash_op = FLASHROM_OPER_SAVE;
		}

		memcpy(req->data_buf, img, num_bytes);
		img += num_bytes;
		status = be_cmd_write_flashrom(adapter, flash_cmd, optype,
						flash_op, num_bytes);
		if (status) {
			if (status == ILLEGAL_IOCTL_REQ &&
			    optype == OPTYPE_PHY_FW)
				break;
			dev_err(&adapter->pdev->dev,
				"cmd to write to flash rom failed.\n");
			return status;
		}
	}
	return 0;
}

/* For BE2 and BE3 */
static int be_flash_BEx(struct be_adapter *adapter,
			 const struct firmware *fw,
			 struct be_dma_mem *flash_cmd,
			 int num_of_images)

{
	int status = 0, i, filehdr_size = 0;
	int img_hdrs_size = (num_of_images * sizeof(struct image_hdr));
	const u8 *p = fw->data;
	const struct flash_comp *pflashcomp;
	int num_comp, redboot;
	struct flash_section_info *fsec = NULL;

	struct flash_comp gen3_flash_types[] = {
		{ FLASH_iSCSI_PRIMARY_IMAGE_START_g3, OPTYPE_ISCSI_ACTIVE,
			FLASH_IMAGE_MAX_SIZE_g3, IMAGE_FIRMWARE_iSCSI},
		{ FLASH_REDBOOT_START_g3, OPTYPE_REDBOOT,
			FLASH_REDBOOT_IMAGE_MAX_SIZE_g3, IMAGE_BOOT_CODE},
		{ FLASH_iSCSI_BIOS_START_g3, OPTYPE_BIOS,
			FLASH_BIOS_IMAGE_MAX_SIZE_g3, IMAGE_OPTION_ROM_ISCSI},
		{ FLASH_PXE_BIOS_START_g3, OPTYPE_PXE_BIOS,
			FLASH_BIOS_IMAGE_MAX_SIZE_g3, IMAGE_OPTION_ROM_PXE},
		{ FLASH_FCoE_BIOS_START_g3, OPTYPE_FCOE_BIOS,
			FLASH_BIOS_IMAGE_MAX_SIZE_g3, IMAGE_OPTION_ROM_FCoE},
		{ FLASH_iSCSI_BACKUP_IMAGE_START_g3, OPTYPE_ISCSI_BACKUP,
			FLASH_IMAGE_MAX_SIZE_g3, IMAGE_FIRMWARE_BACKUP_iSCSI},
		{ FLASH_FCoE_PRIMARY_IMAGE_START_g3, OPTYPE_FCOE_FW_ACTIVE,
			FLASH_IMAGE_MAX_SIZE_g3, IMAGE_FIRMWARE_FCoE},
		{ FLASH_FCoE_BACKUP_IMAGE_START_g3, OPTYPE_FCOE_FW_BACKUP,
			FLASH_IMAGE_MAX_SIZE_g3, IMAGE_FIRMWARE_BACKUP_FCoE},
		{ FLASH_NCSI_START_g3, OPTYPE_NCSI_FW,
			FLASH_NCSI_IMAGE_MAX_SIZE_g3, IMAGE_NCSI},
		{ FLASH_PHY_FW_START_g3, OPTYPE_PHY_FW,
			FLASH_PHY_FW_IMAGE_MAX_SIZE_g3, IMAGE_FIRMWARE_PHY}
	};

	struct flash_comp gen2_flash_types[] = {
		{ FLASH_iSCSI_PRIMARY_IMAGE_START_g2, OPTYPE_ISCSI_ACTIVE,
			FLASH_IMAGE_MAX_SIZE_g2, IMAGE_FIRMWARE_iSCSI},
		{ FLASH_REDBOOT_START_g2, OPTYPE_REDBOOT,
			FLASH_REDBOOT_IMAGE_MAX_SIZE_g2, IMAGE_BOOT_CODE},
		{ FLASH_iSCSI_BIOS_START_g2, OPTYPE_BIOS,
			FLASH_BIOS_IMAGE_MAX_SIZE_g2, IMAGE_OPTION_ROM_ISCSI},
		{ FLASH_PXE_BIOS_START_g2, OPTYPE_PXE_BIOS,
			FLASH_BIOS_IMAGE_MAX_SIZE_g2, IMAGE_OPTION_ROM_PXE},
		{ FLASH_FCoE_BIOS_START_g2, OPTYPE_FCOE_BIOS,
			FLASH_BIOS_IMAGE_MAX_SIZE_g2, IMAGE_OPTION_ROM_FCoE},
		{ FLASH_iSCSI_BACKUP_IMAGE_START_g2, OPTYPE_ISCSI_BACKUP,
			FLASH_IMAGE_MAX_SIZE_g2, IMAGE_FIRMWARE_BACKUP_iSCSI},
		{ FLASH_FCoE_PRIMARY_IMAGE_START_g2, OPTYPE_FCOE_FW_ACTIVE,
			FLASH_IMAGE_MAX_SIZE_g2, IMAGE_FIRMWARE_FCoE},
		{ FLASH_FCoE_BACKUP_IMAGE_START_g2, OPTYPE_FCOE_FW_BACKUP,
			 FLASH_IMAGE_MAX_SIZE_g2, IMAGE_FIRMWARE_BACKUP_FCoE}
	};

	if (BE3_chip(adapter)) {
		pflashcomp = gen3_flash_types;
		filehdr_size = sizeof(struct flash_file_hdr_g3);
		num_comp = ARRAY_SIZE(gen3_flash_types);
	} else {
		pflashcomp = gen2_flash_types;
		filehdr_size = sizeof(struct flash_file_hdr_g2);
		num_comp = ARRAY_SIZE(gen2_flash_types);
	}

	/* Get flash section info*/
	fsec = get_fsec_info(adapter, filehdr_size + img_hdrs_size, fw);
	if (!fsec) {
		dev_err(&adapter->pdev->dev,
			"Invalid Cookie. UFI corrupted ?\n");
		return -1;
	}
	for (i = 0; i < num_comp; i++) {
		if (!is_comp_in_ufi(adapter, fsec, pflashcomp[i].img_type))
			continue;

		if ((pflashcomp[i].optype == OPTYPE_NCSI_FW) &&
		    memcmp(adapter->fw_ver, "3.102.148.0", 11) < 0)
			continue;

		if (pflashcomp[i].optype == OPTYPE_PHY_FW  &&
		    !phy_flashing_required(adapter))
				continue;

		if (pflashcomp[i].optype == OPTYPE_REDBOOT) {
			redboot = be_flash_redboot(adapter, fw->data,
				pflashcomp[i].offset, pflashcomp[i].size,
				filehdr_size + img_hdrs_size);
			if (!redboot)
				continue;
		}

		p = fw->data;
		p += filehdr_size + pflashcomp[i].offset + img_hdrs_size;
		if (p + pflashcomp[i].size > fw->data + fw->size)
			return -1;

		status = be_flash(adapter, p, flash_cmd, pflashcomp[i].optype,
					pflashcomp[i].size);
		if (status) {
			dev_err(&adapter->pdev->dev,
				"Flashing section type %d failed.\n",
				pflashcomp[i].img_type);
			return status;
		}
	}
	return 0;
}

static int be_flash_skyhawk(struct be_adapter *adapter,
		const struct firmware *fw,
		struct be_dma_mem *flash_cmd, int num_of_images)
{
	int status = 0, i, filehdr_size = 0;
	int img_offset, img_size, img_optype, redboot;
	int img_hdrs_size = num_of_images * sizeof(struct image_hdr);
	const u8 *p = fw->data;
	struct flash_section_info *fsec = NULL;

	filehdr_size = sizeof(struct flash_file_hdr_g3);
	fsec = get_fsec_info(adapter, filehdr_size + img_hdrs_size, fw);
	if (!fsec) {
		dev_err(&adapter->pdev->dev,
			"Invalid Cookie. UFI corrupted ?\n");
		return -1;
	}

	for (i = 0; i < le32_to_cpu(fsec->fsec_hdr.num_images); i++) {
		img_offset = le32_to_cpu(fsec->fsec_entry[i].offset);
		img_size   = le32_to_cpu(fsec->fsec_entry[i].pad_size);

		switch (le32_to_cpu(fsec->fsec_entry[i].type)) {
		case IMAGE_FIRMWARE_iSCSI:
			img_optype = OPTYPE_ISCSI_ACTIVE;
			break;
		case IMAGE_BOOT_CODE:
			img_optype = OPTYPE_REDBOOT;
			break;
		case IMAGE_OPTION_ROM_ISCSI:
			img_optype = OPTYPE_BIOS;
			break;
		case IMAGE_OPTION_ROM_PXE:
			img_optype = OPTYPE_PXE_BIOS;
			break;
		case IMAGE_OPTION_ROM_FCoE:
			img_optype = OPTYPE_FCOE_BIOS;
			break;
		case IMAGE_FIRMWARE_BACKUP_iSCSI:
			img_optype = OPTYPE_ISCSI_BACKUP;
			break;
		case IMAGE_NCSI:
			img_optype = OPTYPE_NCSI_FW;
			break;
		default:
			continue;
		}

		if (img_optype == OPTYPE_REDBOOT) {
			redboot = be_flash_redboot(adapter, fw->data,
					img_offset, img_size,
					filehdr_size + img_hdrs_size);
			if (!redboot)
				continue;
		}

		p = fw->data;
		p += filehdr_size + img_offset + img_hdrs_size;
		if (p + img_size > fw->data + fw->size)
			return -1;

		status = be_flash(adapter, p, flash_cmd, img_optype, img_size);
		if (status) {
			dev_err(&adapter->pdev->dev,
				"Flashing section type %d failed.\n",
				fsec->fsec_entry[i].type);
			return status;
		}
	}
	return 0;
}

static int lancer_wait_idle(struct be_adapter *adapter)
{
#define SLIPORT_IDLE_TIMEOUT 30
	u32 reg_val;
	int status = 0, i;

	for (i = 0; i < SLIPORT_IDLE_TIMEOUT; i++) {
		reg_val = ioread32(adapter->db + PHYSDEV_CONTROL_OFFSET);
		if ((reg_val & PHYSDEV_CONTROL_INP_MASK) == 0)
			break;

		ssleep(1);
	}

	if (i == SLIPORT_IDLE_TIMEOUT)
		status = -1;

	return status;
}

static int lancer_fw_reset(struct be_adapter *adapter)
{
	int status = 0;

	status = lancer_wait_idle(adapter);
	if (status)
		return status;

	iowrite32(PHYSDEV_CONTROL_FW_RESET_MASK, adapter->db +
		  PHYSDEV_CONTROL_OFFSET);

	return status;
}

static int lancer_fw_download(struct be_adapter *adapter,
				const struct firmware *fw)
{
#define LANCER_FW_DOWNLOAD_CHUNK      (32 * 1024)
#define LANCER_FW_DOWNLOAD_LOCATION   "/prg"
	struct be_dma_mem flash_cmd;
	const u8 *data_ptr = NULL;
	u8 *dest_image_ptr = NULL;
	size_t image_size = 0;
	u32 chunk_size = 0;
	u32 data_written = 0;
	u32 offset = 0;
	int status = 0;
	u8 add_status = 0;
	u8 change_status;

	if (!IS_ALIGNED(fw->size, sizeof(u32))) {
		dev_err(&adapter->pdev->dev,
			"FW Image not properly aligned. "
			"Length must be 4 byte aligned.\n");
		status = -EINVAL;
		goto lancer_fw_exit;
	}

	flash_cmd.size = sizeof(struct lancer_cmd_req_write_object)
				+ LANCER_FW_DOWNLOAD_CHUNK;
	flash_cmd.va = dma_alloc_coherent(&adapter->pdev->dev, flash_cmd.size,
						&flash_cmd.dma, GFP_KERNEL);
	if (!flash_cmd.va) {
		status = -ENOMEM;
		dev_err(&adapter->pdev->dev,
			"Memory allocation failure while flashing\n");
		goto lancer_fw_exit;
	}

	dest_image_ptr = flash_cmd.va +
				sizeof(struct lancer_cmd_req_write_object);
	image_size = fw->size;
	data_ptr = fw->data;

	while (image_size) {
		chunk_size = min_t(u32, image_size, LANCER_FW_DOWNLOAD_CHUNK);

		/* Copy the image chunk content. */
		memcpy(dest_image_ptr, data_ptr, chunk_size);

		status = lancer_cmd_write_object(adapter, &flash_cmd,
						 chunk_size, offset,
						 LANCER_FW_DOWNLOAD_LOCATION,
						 &data_written, &change_status,
						 &add_status);
		if (status)
			break;

		offset += data_written;
		data_ptr += data_written;
		image_size -= data_written;
	}

	if (!status) {
		/* Commit the FW written */
		status = lancer_cmd_write_object(adapter, &flash_cmd,
						 0, offset,
						 LANCER_FW_DOWNLOAD_LOCATION,
						 &data_written, &change_status,
						 &add_status);
	}

	dma_free_coherent(&adapter->pdev->dev, flash_cmd.size, flash_cmd.va,
				flash_cmd.dma);
	if (status) {
		dev_err(&adapter->pdev->dev,
			"Firmware load error. "
			"Status code: 0x%x Additional Status: 0x%x\n",
			status, add_status);
		goto lancer_fw_exit;
	}

	if (change_status == LANCER_FW_RESET_NEEDED) {
		status = lancer_fw_reset(adapter);
		if (status) {
			dev_err(&adapter->pdev->dev,
				"Adapter busy for FW reset.\n"
				"New FW will not be active.\n");
			goto lancer_fw_exit;
		}
	} else if (change_status != LANCER_NO_RESET_NEEDED) {
			dev_err(&adapter->pdev->dev,
				"System reboot required for new FW"
				" to be active\n");
	}

	dev_info(&adapter->pdev->dev, "Firmware flashed successfully\n");
lancer_fw_exit:
	return status;
}

#define UFI_TYPE2		2
#define UFI_TYPE3		3
#define UFI_TYPE4		4
static int be_get_ufi_type(struct be_adapter *adapter,
			   struct flash_file_hdr_g2 *fhdr)
{
	if (fhdr == NULL)
		goto be_get_ufi_exit;

	if (skyhawk_chip(adapter) && fhdr->build[0] == '4')
		return UFI_TYPE4;
	else if (BE3_chip(adapter) && fhdr->build[0] == '3')
		return UFI_TYPE3;
	else if (BE2_chip(adapter) && fhdr->build[0] == '2')
		return UFI_TYPE2;

be_get_ufi_exit:
	dev_err(&adapter->pdev->dev,
		"UFI and Interface are not compatible for flashing\n");
	return -1;
}

static int be_fw_download(struct be_adapter *adapter, const struct firmware* fw)
{
	struct flash_file_hdr_g2 *fhdr;
	struct flash_file_hdr_g3 *fhdr3;
	struct image_hdr *img_hdr_ptr = NULL;
	struct be_dma_mem flash_cmd;
	const u8 *p;
	int status = 0, i = 0, num_imgs = 0, ufi_type = 0;

	flash_cmd.size = sizeof(struct be_cmd_write_flashrom);
	flash_cmd.va = dma_alloc_coherent(&adapter->pdev->dev, flash_cmd.size,
					  &flash_cmd.dma, GFP_KERNEL);
	if (!flash_cmd.va) {
		status = -ENOMEM;
		dev_err(&adapter->pdev->dev,
			"Memory allocation failure while flashing\n");
		goto be_fw_exit;
	}

	p = fw->data;
	fhdr = (struct flash_file_hdr_g2 *)p;

	ufi_type = be_get_ufi_type(adapter, fhdr);

	fhdr3 = (struct flash_file_hdr_g3 *)fw->data;
	num_imgs = le32_to_cpu(fhdr3->num_imgs);
	for (i = 0; i < num_imgs; i++) {
		img_hdr_ptr = (struct image_hdr *)(fw->data +
				(sizeof(struct flash_file_hdr_g3) +
				 i * sizeof(struct image_hdr)));
		if (le32_to_cpu(img_hdr_ptr->imageid) == 1) {
			if (ufi_type == UFI_TYPE4)
				status = be_flash_skyhawk(adapter, fw,
							&flash_cmd, num_imgs);
			else if (ufi_type == UFI_TYPE3)
				status = be_flash_BEx(adapter, fw, &flash_cmd,
						      num_imgs);
		}
	}

	if (ufi_type == UFI_TYPE2)
		status = be_flash_BEx(adapter, fw, &flash_cmd, 0);
	else if (ufi_type == -1)
		status = -1;

	dma_free_coherent(&adapter->pdev->dev, flash_cmd.size, flash_cmd.va,
			  flash_cmd.dma);
	if (status) {
		dev_err(&adapter->pdev->dev, "Firmware load error\n");
		goto be_fw_exit;
	}

	dev_info(&adapter->pdev->dev, "Firmware flashed successfully\n");

be_fw_exit:
	return status;
}

int be_load_fw(struct be_adapter *adapter, u8 *fw_file)
{
	const struct firmware *fw;
	int status;

	if (!netif_running(adapter->netdev)) {
		dev_err(&adapter->pdev->dev,
			"Firmware load not allowed (interface is down)\n");
		return -1;
	}

	status = request_firmware(&fw, fw_file, &adapter->pdev->dev);
	if (status)
		goto fw_exit;

	dev_info(&adapter->pdev->dev, "Flashing firmware file %s\n", fw_file);

	if (lancer_chip(adapter))
		status = lancer_fw_download(adapter, fw);
	else
		status = be_fw_download(adapter, fw);

fw_exit:
	release_firmware(fw);
	return status;
}

static const struct net_device_ops be_netdev_ops = {
	.ndo_open		= be_open,
	.ndo_stop		= be_close,
	.ndo_start_xmit		= be_xmit,
	.ndo_set_rx_mode	= be_set_rx_mode,
	.ndo_set_mac_address	= be_mac_addr_set,
	.ndo_change_mtu		= be_change_mtu,
	.ndo_get_stats64	= be_get_stats64,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_vlan_rx_add_vid	= be_vlan_add_vid,
	.ndo_vlan_rx_kill_vid	= be_vlan_rem_vid,
	.ndo_set_vf_mac		= be_set_vf_mac,
	.ndo_set_vf_vlan	= be_set_vf_vlan,
	.ndo_set_vf_tx_rate	= be_set_vf_tx_rate,
	.ndo_get_vf_config	= be_get_vf_config,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= be_netpoll,
#endif
};

static void be_netdev_init(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_eq_obj *eqo;
	int i;

	netdev->hw_features |= NETIF_F_SG | NETIF_F_TSO | NETIF_F_TSO6 |
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM |
		NETIF_F_HW_VLAN_TX;
	if (be_multi_rxq(adapter))
		netdev->hw_features |= NETIF_F_RXHASH;

	netdev->features |= netdev->hw_features |
		NETIF_F_HW_VLAN_RX | NETIF_F_HW_VLAN_FILTER;

	netdev->vlan_features |= NETIF_F_SG | NETIF_F_TSO | NETIF_F_TSO6 |
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;

	netdev->priv_flags |= IFF_UNICAST_FLT;

	netdev->flags |= IFF_MULTICAST;

	netif_set_gso_max_size(netdev, 65535 - ETH_HLEN);

	netdev->netdev_ops = &be_netdev_ops;

	SET_ETHTOOL_OPS(netdev, &be_ethtool_ops);

	for_all_evt_queues(adapter, eqo, i)
		netif_napi_add(netdev, &eqo->napi, be_poll, BE_NAPI_WEIGHT);
}

static void be_unmap_pci_bars(struct be_adapter *adapter)
{
	if (adapter->db)
		pci_iounmap(adapter->pdev, adapter->db);
}

static int db_bar(struct be_adapter *adapter)
{
	if (lancer_chip(adapter) || !be_physfn(adapter))
		return 0;
	else
		return 4;
}

static int be_roce_map_pci_bars(struct be_adapter *adapter)
{
	if (skyhawk_chip(adapter)) {
		adapter->roce_db.size = 4096;
		adapter->roce_db.io_addr = pci_resource_start(adapter->pdev,
							      db_bar(adapter));
		adapter->roce_db.total_size = pci_resource_len(adapter->pdev,
							       db_bar(adapter));
	}
	return 0;
}

static int be_map_pci_bars(struct be_adapter *adapter)
{
	u8 __iomem *addr;
	u32 sli_intf;

	pci_read_config_dword(adapter->pdev, SLI_INTF_REG_OFFSET, &sli_intf);
	adapter->if_type = (sli_intf & SLI_INTF_IF_TYPE_MASK) >>
				SLI_INTF_IF_TYPE_SHIFT;

	addr = pci_iomap(adapter->pdev, db_bar(adapter), 0);
	if (addr == NULL)
		goto pci_map_err;
	adapter->db = addr;

	be_roce_map_pci_bars(adapter);
	return 0;

pci_map_err:
	be_unmap_pci_bars(adapter);
	return -ENOMEM;
}

static void be_ctrl_cleanup(struct be_adapter *adapter)
{
	struct be_dma_mem *mem = &adapter->mbox_mem_alloced;

	be_unmap_pci_bars(adapter);

	if (mem->va)
		dma_free_coherent(&adapter->pdev->dev, mem->size, mem->va,
				  mem->dma);

	mem = &adapter->rx_filter;
	if (mem->va)
		dma_free_coherent(&adapter->pdev->dev, mem->size, mem->va,
				  mem->dma);
}

static int be_ctrl_init(struct be_adapter *adapter)
{
	struct be_dma_mem *mbox_mem_alloc = &adapter->mbox_mem_alloced;
	struct be_dma_mem *mbox_mem_align = &adapter->mbox_mem;
	struct be_dma_mem *rx_filter = &adapter->rx_filter;
	u32 sli_intf;
	int status;

	pci_read_config_dword(adapter->pdev, SLI_INTF_REG_OFFSET, &sli_intf);
	adapter->sli_family = (sli_intf & SLI_INTF_FAMILY_MASK) >>
				 SLI_INTF_FAMILY_SHIFT;
	adapter->virtfn = (sli_intf & SLI_INTF_FT_MASK) ? 1 : 0;

	status = be_map_pci_bars(adapter);
	if (status)
		goto done;

	mbox_mem_alloc->size = sizeof(struct be_mcc_mailbox) + 16;
	mbox_mem_alloc->va = dma_alloc_coherent(&adapter->pdev->dev,
						mbox_mem_alloc->size,
						&mbox_mem_alloc->dma,
						GFP_KERNEL);
	if (!mbox_mem_alloc->va) {
		status = -ENOMEM;
		goto unmap_pci_bars;
	}
	mbox_mem_align->size = sizeof(struct be_mcc_mailbox);
	mbox_mem_align->va = PTR_ALIGN(mbox_mem_alloc->va, 16);
	mbox_mem_align->dma = PTR_ALIGN(mbox_mem_alloc->dma, 16);
	memset(mbox_mem_align->va, 0, sizeof(struct be_mcc_mailbox));

	rx_filter->size = sizeof(struct be_cmd_req_rx_filter);
	rx_filter->va = dma_alloc_coherent(&adapter->pdev->dev, rx_filter->size,
					&rx_filter->dma, GFP_KERNEL);
	if (rx_filter->va == NULL) {
		status = -ENOMEM;
		goto free_mbox;
	}
	memset(rx_filter->va, 0, rx_filter->size);
	mutex_init(&adapter->mbox_lock);
	spin_lock_init(&adapter->mcc_lock);
	spin_lock_init(&adapter->mcc_cq_lock);

	init_completion(&adapter->flash_compl);
	pci_save_state(adapter->pdev);
	return 0;

free_mbox:
	dma_free_coherent(&adapter->pdev->dev, mbox_mem_alloc->size,
			  mbox_mem_alloc->va, mbox_mem_alloc->dma);

unmap_pci_bars:
	be_unmap_pci_bars(adapter);

done:
	return status;
}

static void be_stats_cleanup(struct be_adapter *adapter)
{
	struct be_dma_mem *cmd = &adapter->stats_cmd;

	if (cmd->va)
		dma_free_coherent(&adapter->pdev->dev, cmd->size,
				  cmd->va, cmd->dma);
}

static int be_stats_init(struct be_adapter *adapter)
{
	struct be_dma_mem *cmd = &adapter->stats_cmd;

	if (lancer_chip(adapter))
		cmd->size = sizeof(struct lancer_cmd_req_pport_stats);
	else if (BE2_chip(adapter))
		cmd->size = sizeof(struct be_cmd_req_get_stats_v0);
	else
		/* BE3 and Skyhawk */
		cmd->size = sizeof(struct be_cmd_req_get_stats_v1);

	cmd->va = dma_alloc_coherent(&adapter->pdev->dev, cmd->size, &cmd->dma,
				     GFP_KERNEL);
	if (cmd->va == NULL)
		return -1;
	memset(cmd->va, 0, cmd->size);
	return 0;
}

static void __devexit be_remove(struct pci_dev *pdev)
{
	struct be_adapter *adapter = pci_get_drvdata(pdev);

	if (!adapter)
		return;

	be_roce_dev_remove(adapter);

	cancel_delayed_work_sync(&adapter->func_recovery_work);

	unregister_netdev(adapter->netdev);

	be_clear(adapter);

	/* tell fw we're done with firing cmds */
	be_cmd_fw_clean(adapter);

	be_stats_cleanup(adapter);

	be_ctrl_cleanup(adapter);

	pci_disable_pcie_error_reporting(pdev);

	pci_set_drvdata(pdev, NULL);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	free_netdev(adapter->netdev);
}

bool be_is_wol_supported(struct be_adapter *adapter)
{
	return ((adapter->wol_cap & BE_WOL_CAP) &&
		!be_is_wol_excluded(adapter)) ? true : false;
}

u32 be_get_fw_log_level(struct be_adapter *adapter)
{
	struct be_dma_mem extfat_cmd;
	struct be_fat_conf_params *cfgs;
	int status;
	u32 level = 0;
	int j;

	if (lancer_chip(adapter))
		return 0;

	memset(&extfat_cmd, 0, sizeof(struct be_dma_mem));
	extfat_cmd.size = sizeof(struct be_cmd_resp_get_ext_fat_caps);
	extfat_cmd.va = pci_alloc_consistent(adapter->pdev, extfat_cmd.size,
					     &extfat_cmd.dma);

	if (!extfat_cmd.va) {
		dev_err(&adapter->pdev->dev, "%s: Memory allocation failure\n",
			__func__);
		goto err;
	}

	status = be_cmd_get_ext_fat_capabilites(adapter, &extfat_cmd);
	if (!status) {
		cfgs = (struct be_fat_conf_params *)(extfat_cmd.va +
						sizeof(struct be_cmd_resp_hdr));
		for (j = 0; j < le32_to_cpu(cfgs->module[0].num_modes); j++) {
			if (cfgs->module[0].trace_lvl[j].mode == MODE_UART)
				level = cfgs->module[0].trace_lvl[j].dbg_lvl;
		}
	}
	pci_free_consistent(adapter->pdev, extfat_cmd.size, extfat_cmd.va,
			    extfat_cmd.dma);
err:
	return level;
}

static int be_get_initial_config(struct be_adapter *adapter)
{
	int status;
	u32 level;

	status = be_cmd_get_cntl_attributes(adapter);
	if (status)
		return status;

	status = be_cmd_get_acpi_wol_cap(adapter);
	if (status) {
		/* in case of a failure to get wol capabillities
		 * check the exclusion list to determine WOL capability */
		if (!be_is_wol_excluded(adapter))
			adapter->wol_cap |= BE_WOL_CAP;
	}

	if (be_is_wol_supported(adapter))
		adapter->wol = true;

	/* Must be a power of 2 or else MODULO will BUG_ON */
	adapter->be_get_temp_freq = 64;

	level = be_get_fw_log_level(adapter);
	adapter->msg_enable = level <= FW_LOG_LEVEL_DEFAULT ? NETIF_MSG_HW : 0;

	return 0;
}

static int lancer_recover_func(struct be_adapter *adapter)
{
	int status;

	status = lancer_test_and_set_rdy_state(adapter);
	if (status)
		goto err;

	if (netif_running(adapter->netdev))
		be_close(adapter->netdev);

	be_clear(adapter);

	adapter->hw_error = false;
	adapter->fw_timeout = false;

	status = be_setup(adapter);
	if (status)
		goto err;

	if (netif_running(adapter->netdev)) {
		status = be_open(adapter->netdev);
		if (status)
			goto err;
	}

	dev_err(&adapter->pdev->dev,
		"Adapter SLIPORT recovery succeeded\n");
	return 0;
err:
	if (adapter->eeh_error)
		dev_err(&adapter->pdev->dev,
			"Adapter SLIPORT recovery failed\n");

	return status;
}

static void be_func_recovery_task(struct work_struct *work)
{
	struct be_adapter *adapter =
		container_of(work, struct be_adapter,  func_recovery_work.work);
	int status;

	be_detect_error(adapter);

	if (adapter->hw_error && lancer_chip(adapter)) {

		if (adapter->eeh_error)
			goto out;

		rtnl_lock();
		netif_device_detach(adapter->netdev);
		rtnl_unlock();

		status = lancer_recover_func(adapter);

		if (!status)
			netif_device_attach(adapter->netdev);
	}

out:
	schedule_delayed_work(&adapter->func_recovery_work,
			      msecs_to_jiffies(1000));
}

static void be_worker(struct work_struct *work)
{
	struct be_adapter *adapter =
		container_of(work, struct be_adapter, work.work);
	struct be_rx_obj *rxo;
	struct be_eq_obj *eqo;
	int i;

	/* when interrupts are not yet enabled, just reap any pending
	* mcc completions */
	if (!netif_running(adapter->netdev)) {
		local_bh_disable();
		be_process_mcc(adapter);
		local_bh_enable();
		goto reschedule;
	}

	if (!adapter->stats_cmd_sent) {
		if (lancer_chip(adapter))
			lancer_cmd_get_pport_stats(adapter,
						&adapter->stats_cmd);
		else
			be_cmd_get_stats(adapter, &adapter->stats_cmd);
	}

	if (MODULO(adapter->work_counter, adapter->be_get_temp_freq) == 0)
		be_cmd_get_die_temperature(adapter);

	for_all_rx_queues(adapter, rxo, i) {
		if (rxo->rx_post_starved) {
			rxo->rx_post_starved = false;
			be_post_rx_frags(rxo, GFP_KERNEL);
		}
	}

	for_all_evt_queues(adapter, eqo, i)
		be_eqd_update(adapter, eqo);

reschedule:
	adapter->work_counter++;
	schedule_delayed_work(&adapter->work, msecs_to_jiffies(1000));
}

static bool be_reset_required(struct be_adapter *adapter)
{
	return be_find_vfs(adapter, ENABLED) > 0 ? false : true;
}

static char *mc_name(struct be_adapter *adapter)
{
	if (adapter->function_mode & FLEX10_MODE)
		return "FLEX10";
	else if (adapter->function_mode & VNIC_MODE)
		return "vNIC";
	else if (adapter->function_mode & UMC_ENABLED)
		return "UMC";
	else
		return "";
}

static inline char *func_name(struct be_adapter *adapter)
{
	return be_physfn(adapter) ? "PF" : "VF";
}

static int __devinit be_probe(struct pci_dev *pdev,
			const struct pci_device_id *pdev_id)
{
	int status = 0;
	struct be_adapter *adapter;
	struct net_device *netdev;
	char port_name;

	status = pci_enable_device(pdev);
	if (status)
		goto do_none;

	status = pci_request_regions(pdev, DRV_NAME);
	if (status)
		goto disable_dev;
	pci_set_master(pdev);

	netdev = alloc_etherdev_mqs(sizeof(*adapter), MAX_TX_QS, MAX_RX_QS);
	if (netdev == NULL) {
		status = -ENOMEM;
		goto rel_reg;
	}
	adapter = netdev_priv(netdev);
	adapter->pdev = pdev;
	pci_set_drvdata(pdev, adapter);
	adapter->netdev = netdev;
	SET_NETDEV_DEV(netdev, &pdev->dev);

	status = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (!status) {
		netdev->features |= NETIF_F_HIGHDMA;
	} else {
		status = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (status) {
			dev_err(&pdev->dev, "Could not set PCI DMA Mask\n");
			goto free_netdev;
		}
	}

	status = pci_enable_pcie_error_reporting(pdev);
	if (status)
		dev_err(&pdev->dev, "Could not use PCIe error reporting\n");

	status = be_ctrl_init(adapter);
	if (status)
		goto free_netdev;

	/* sync up with fw's ready state */
	if (be_physfn(adapter)) {
		status = be_fw_wait_ready(adapter);
		if (status)
			goto ctrl_clean;
	}

	/* tell fw we're ready to fire cmds */
	status = be_cmd_fw_init(adapter);
	if (status)
		goto ctrl_clean;

	if (be_reset_required(adapter)) {
		status = be_cmd_reset_function(adapter);
		if (status)
			goto ctrl_clean;
	}

	/* The INTR bit may be set in the card when probed by a kdump kernel
	 * after a crash.
	 */
	if (!lancer_chip(adapter))
		be_intr_set(adapter, false);

	status = be_stats_init(adapter);
	if (status)
		goto ctrl_clean;

	status = be_get_initial_config(adapter);
	if (status)
		goto stats_clean;

	INIT_DELAYED_WORK(&adapter->work, be_worker);
	INIT_DELAYED_WORK(&adapter->func_recovery_work, be_func_recovery_task);
	adapter->rx_fc = adapter->tx_fc = true;

	status = be_setup(adapter);
	if (status)
		goto stats_clean;

	be_netdev_init(netdev);
	status = register_netdev(netdev);
	if (status != 0)
		goto unsetup;

	be_roce_dev_add(adapter);

	schedule_delayed_work(&adapter->func_recovery_work,
			      msecs_to_jiffies(1000));

	be_cmd_query_port_name(adapter, &port_name);

	dev_info(&pdev->dev, "%s: %s %s port %c\n", nic_name(pdev),
		 func_name(adapter), mc_name(adapter), port_name);

	return 0;

unsetup:
	be_clear(adapter);
stats_clean:
	be_stats_cleanup(adapter);
ctrl_clean:
	be_ctrl_cleanup(adapter);
free_netdev:
	free_netdev(netdev);
	pci_set_drvdata(pdev, NULL);
rel_reg:
	pci_release_regions(pdev);
disable_dev:
	pci_disable_device(pdev);
do_none:
	dev_err(&pdev->dev, "%s initialization failed\n", nic_name(pdev));
	return status;
}

static int be_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct be_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev =  adapter->netdev;

	if (adapter->wol)
		be_setup_wol(adapter, true);

	cancel_delayed_work_sync(&adapter->func_recovery_work);

	netif_device_detach(netdev);
	if (netif_running(netdev)) {
		rtnl_lock();
		be_close(netdev);
		rtnl_unlock();
	}
	be_clear(adapter);

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int be_resume(struct pci_dev *pdev)
{
	int status = 0;
	struct be_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev =  adapter->netdev;

	netif_device_detach(netdev);

	status = pci_enable_device(pdev);
	if (status)
		return status;

	pci_set_power_state(pdev, 0);
	pci_restore_state(pdev);

	/* tell fw we're ready to fire cmds */
	status = be_cmd_fw_init(adapter);
	if (status)
		return status;

	be_setup(adapter);
	if (netif_running(netdev)) {
		rtnl_lock();
		be_open(netdev);
		rtnl_unlock();
	}

	schedule_delayed_work(&adapter->func_recovery_work,
			      msecs_to_jiffies(1000));
	netif_device_attach(netdev);

	if (adapter->wol)
		be_setup_wol(adapter, false);

	return 0;
}

/*
 * An FLR will stop BE from DMAing any data.
 */
static void be_shutdown(struct pci_dev *pdev)
{
	struct be_adapter *adapter = pci_get_drvdata(pdev);

	if (!adapter)
		return;

	cancel_delayed_work_sync(&adapter->work);
	cancel_delayed_work_sync(&adapter->func_recovery_work);

	netif_device_detach(adapter->netdev);

	be_cmd_reset_function(adapter);

	pci_disable_device(pdev);
}

static pci_ers_result_t be_eeh_err_detected(struct pci_dev *pdev,
				pci_channel_state_t state)
{
	struct be_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev =  adapter->netdev;

	dev_err(&adapter->pdev->dev, "EEH error detected\n");

	adapter->eeh_error = true;

	cancel_delayed_work_sync(&adapter->func_recovery_work);

	rtnl_lock();
	netif_device_detach(netdev);
	rtnl_unlock();

	if (netif_running(netdev)) {
		rtnl_lock();
		be_close(netdev);
		rtnl_unlock();
	}
	be_clear(adapter);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	pci_disable_device(pdev);

	/* The error could cause the FW to trigger a flash debug dump.
	 * Resetting the card while flash dump is in progress
	 * can cause it not to recover; wait for it to finish.
	 * Wait only for first function as it is needed only once per
	 * adapter.
	 */
	if (pdev->devfn == 0)
		ssleep(30);

	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t be_eeh_reset(struct pci_dev *pdev)
{
	struct be_adapter *adapter = pci_get_drvdata(pdev);
	int status;

	dev_info(&adapter->pdev->dev, "EEH reset\n");
	be_clear_all_error(adapter);

	status = pci_enable_device(pdev);
	if (status)
		return PCI_ERS_RESULT_DISCONNECT;

	pci_set_master(pdev);
	pci_set_power_state(pdev, 0);
	pci_restore_state(pdev);

	/* Check if card is ok and fw is ready */
	status = be_fw_wait_ready(adapter);
	if (status)
		return PCI_ERS_RESULT_DISCONNECT;

	pci_cleanup_aer_uncorrect_error_status(pdev);
	return PCI_ERS_RESULT_RECOVERED;
}

static void be_eeh_resume(struct pci_dev *pdev)
{
	int status = 0;
	struct be_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev =  adapter->netdev;

	dev_info(&adapter->pdev->dev, "EEH resume\n");

	pci_save_state(pdev);

	/* tell fw we're ready to fire cmds */
	status = be_cmd_fw_init(adapter);
	if (status)
		goto err;

	status = be_cmd_reset_function(adapter);
	if (status)
		goto err;

	status = be_setup(adapter);
	if (status)
		goto err;

	if (netif_running(netdev)) {
		status = be_open(netdev);
		if (status)
			goto err;
	}

	schedule_delayed_work(&adapter->func_recovery_work,
			      msecs_to_jiffies(1000));
	netif_device_attach(netdev);
	return;
err:
	dev_err(&adapter->pdev->dev, "EEH resume failed\n");
}

static const struct pci_error_handlers be_eeh_handlers = {
	.error_detected = be_eeh_err_detected,
	.slot_reset = be_eeh_reset,
	.resume = be_eeh_resume,
};

static struct pci_driver be_driver = {
	.name = DRV_NAME,
	.id_table = be_dev_ids,
	.probe = be_probe,
	.remove = be_remove,
	.suspend = be_suspend,
	.resume = be_resume,
	.shutdown = be_shutdown,
	.err_handler = &be_eeh_handlers
};

static int __init be_init_module(void)
{
	if (rx_frag_size != 8192 && rx_frag_size != 4096 &&
	    rx_frag_size != 2048) {
		printk(KERN_WARNING DRV_NAME
			" : Module param rx_frag_size must be 2048/4096/8192."
			" Using 2048\n");
		rx_frag_size = 2048;
	}

	return pci_register_driver(&be_driver);
}
module_init(be_init_module);

static void __exit be_exit_module(void)
{
	pci_unregister_driver(&be_driver);
}
module_exit(be_exit_module);
