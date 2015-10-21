/*
 * Copyright (C) 2005 - 2015 Emulex
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
#include <linux/if_bridge.h>
#include <net/busy_poll.h>
#include <net/vxlan.h>

MODULE_VERSION(DRV_VER);
MODULE_DESCRIPTION(DRV_DESC " " DRV_VER);
MODULE_AUTHOR("Emulex Corporation");
MODULE_LICENSE("GPL");

/* num_vfs module param is obsolete.
 * Use sysfs method to enable/disable VFs.
 */
static unsigned int num_vfs;
module_param(num_vfs, uint, S_IRUGO);
MODULE_PARM_DESC(num_vfs, "Number of PCI VFs to initialize");

static ushort rx_frag_size = 2048;
module_param(rx_frag_size, ushort, S_IRUGO);
MODULE_PARM_DESC(rx_frag_size, "Size of a fragment that holds rcvd data.");

static const struct pci_device_id be_dev_ids[] = {
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
	"ERX2 ",
	"SPARE ",
	"JTAG ",
	"MPU_INTPEND "
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
	"ECRC",
	"Poison TLP",
	"NETC",
	"PERIPH",
	"LLTXULP",
	"D2P",
	"RCON",
	"LDMA",
	"LLTXP",
	"LLTXPB",
	"Unknown"
};

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
	mem->va = dma_zalloc_coherent(&adapter->pdev->dev, mem->size, &mem->dma,
				      GFP_KERNEL);
	if (!mem->va)
		return -ENOMEM;
	return 0;
}

static void be_reg_intr_set(struct be_adapter *adapter, bool enable)
{
	u32 reg, enabled;

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

static void be_intr_set(struct be_adapter *adapter, bool enable)
{
	int status = 0;

	/* On lancer interrupts can't be controlled via this register */
	if (lancer_chip(adapter))
		return;

	if (be_check_error(adapter, BE_ERROR_EEH))
		return;

	status = be_cmd_intr_set(adapter, enable);
	if (status)
		be_reg_intr_set(adapter, enable);
}

static void be_rxq_notify(struct be_adapter *adapter, u16 qid, u16 posted)
{
	u32 val = 0;

	if (be_check_error(adapter, BE_ERROR_HW))
		return;

	val |= qid & DB_RQ_RING_ID_MASK;
	val |= posted << DB_RQ_NUM_POSTED_SHIFT;

	wmb();
	iowrite32(val, adapter->db + DB_RQ_OFFSET);
}

static void be_txq_notify(struct be_adapter *adapter, struct be_tx_obj *txo,
			  u16 posted)
{
	u32 val = 0;

	if (be_check_error(adapter, BE_ERROR_HW))
		return;

	val |= txo->q.id & DB_TXULP_RING_ID_MASK;
	val |= (posted & DB_TXULP_NUM_POSTED_MASK) << DB_TXULP_NUM_POSTED_SHIFT;

	wmb();
	iowrite32(val, adapter->db + txo->db_offset);
}

static void be_eq_notify(struct be_adapter *adapter, u16 qid,
			 bool arm, bool clear_int, u16 num_popped,
			 u32 eq_delay_mult_enc)
{
	u32 val = 0;

	val |= qid & DB_EQ_RING_ID_MASK;
	val |= ((qid & DB_EQ_RING_ID_EXT_MASK) << DB_EQ_RING_ID_EXT_MASK_SHIFT);

	if (be_check_error(adapter, BE_ERROR_HW))
		return;

	if (arm)
		val |= 1 << DB_EQ_REARM_SHIFT;
	if (clear_int)
		val |= 1 << DB_EQ_CLR_SHIFT;
	val |= 1 << DB_EQ_EVNT_SHIFT;
	val |= num_popped << DB_EQ_NUM_POPPED_SHIFT;
	val |= eq_delay_mult_enc << DB_EQ_R2I_DLY_SHIFT;
	iowrite32(val, adapter->db + DB_EQ_OFFSET);
}

void be_cq_notify(struct be_adapter *adapter, u16 qid, bool arm, u16 num_popped)
{
	u32 val = 0;

	val |= qid & DB_CQ_RING_ID_MASK;
	val |= ((qid & DB_CQ_RING_ID_EXT_MASK) <<
			DB_CQ_RING_ID_EXT_MASK_SHIFT);

	if (be_check_error(adapter, BE_ERROR_HW))
		return;

	if (arm)
		val |= 1 << DB_CQ_REARM_SHIFT;
	val |= num_popped << DB_CQ_NUM_POPPED_SHIFT;
	iowrite32(val, adapter->db + DB_CQ_OFFSET);
}

static int be_mac_addr_set(struct net_device *netdev, void *p)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct device *dev = &adapter->pdev->dev;
	struct sockaddr *addr = p;
	int status;
	u8 mac[ETH_ALEN];
	u32 old_pmac_id = adapter->pmac_id[0], curr_pmac_id = 0;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* Proceed further only if, User provided MAC is different
	 * from active MAC
	 */
	if (ether_addr_equal(addr->sa_data, netdev->dev_addr))
		return 0;

	/* if device is not running, copy MAC to netdev->dev_addr */
	if (!netif_running(netdev))
		goto done;

	/* The PMAC_ADD cmd may fail if the VF doesn't have FILTMGMT
	 * privilege or if PF did not provision the new MAC address.
	 * On BE3, this cmd will always fail if the VF doesn't have the
	 * FILTMGMT privilege. This failure is OK, only if the PF programmed
	 * the MAC for the VF.
	 */
	status = be_cmd_pmac_add(adapter, (u8 *)addr->sa_data,
				 adapter->if_handle, &adapter->pmac_id[0], 0);
	if (!status) {
		curr_pmac_id = adapter->pmac_id[0];

		/* Delete the old programmed MAC. This call may fail if the
		 * old MAC was already deleted by the PF driver.
		 */
		if (adapter->pmac_id[0] != old_pmac_id)
			be_cmd_pmac_del(adapter, adapter->if_handle,
					old_pmac_id, 0);
	}

	/* Decide if the new MAC is successfully activated only after
	 * querying the FW
	 */
	status = be_cmd_get_active_mac(adapter, curr_pmac_id, mac,
				       adapter->if_handle, true, 0);
	if (status)
		goto err;

	/* The MAC change did not happen, either due to lack of privilege
	 * or PF didn't pre-provision.
	 */
	if (!ether_addr_equal(addr->sa_data, mac)) {
		status = -EPERM;
		goto err;
	}
done:
	ether_addr_copy(netdev->dev_addr, addr->sa_data);
	dev_info(dev, "MAC address changed to %pM\n", addr->sa_data);
	return 0;
err:
	dev_warn(dev, "MAC address change to %pM failed\n", addr->sa_data);
	return status;
}

/* BE2 supports only v0 cmd */
static void *hw_stats_from_cmd(struct be_adapter *adapter)
{
	if (BE2_chip(adapter)) {
		struct be_cmd_resp_get_stats_v0 *cmd = adapter->stats_cmd.va;

		return &cmd->hw_stats;
	} else if (BE3_chip(adapter)) {
		struct be_cmd_resp_get_stats_v1 *cmd = adapter->stats_cmd.va;

		return &cmd->hw_stats;
	} else {
		struct be_cmd_resp_get_stats_v2 *cmd = adapter->stats_cmd.va;

		return &cmd->hw_stats;
	}
}

/* BE2 supports only v0 cmd */
static void *be_erx_stats_from_cmd(struct be_adapter *adapter)
{
	if (BE2_chip(adapter)) {
		struct be_hw_stats_v0 *hw_stats = hw_stats_from_cmd(adapter);

		return &hw_stats->erx;
	} else if (BE3_chip(adapter)) {
		struct be_hw_stats_v1 *hw_stats = hw_stats_from_cmd(adapter);

		return &hw_stats->erx;
	} else {
		struct be_hw_stats_v2 *hw_stats = hw_stats_from_cmd(adapter);

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
	drvs->rx_address_filtered =
					port_stats->rx_address_filtered +
					port_stats->rx_vlan_filtered;
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
	drvs->rx_address_filtered = port_stats->rx_address_filtered;
	drvs->rx_alignment_symbol_errors =
		port_stats->rx_alignment_symbol_errors;
	drvs->rxpp_fifo_overflow_drop = port_stats->rxpp_fifo_overflow_drop;
	drvs->tx_pauseframes = port_stats->tx_pauseframes;
	drvs->tx_controlframes = port_stats->tx_controlframes;
	drvs->tx_priority_pauseframes = port_stats->tx_priority_pauseframes;
	drvs->jabber_events = port_stats->jabber_events;
	drvs->rx_drops_no_pbuf = rxf_stats->rx_drops_no_pbuf;
	drvs->rx_drops_no_erx_descr = rxf_stats->rx_drops_no_erx_descr;
	drvs->forwarded_packets = rxf_stats->forwarded_packets;
	drvs->rx_drops_mtu = rxf_stats->rx_drops_mtu;
	drvs->rx_drops_no_tpre_descr = rxf_stats->rx_drops_no_tpre_descr;
	drvs->rx_drops_too_many_frags = rxf_stats->rx_drops_too_many_frags;
	adapter->drv_stats.eth_red_drops = pmem_sts->eth_red_drops;
}

static void populate_be_v2_stats(struct be_adapter *adapter)
{
	struct be_hw_stats_v2 *hw_stats = hw_stats_from_cmd(adapter);
	struct be_pmem_stats *pmem_sts = &hw_stats->pmem;
	struct be_rxf_stats_v2 *rxf_stats = &hw_stats->rxf;
	struct be_port_rxf_stats_v2 *port_stats =
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
	drvs->rx_address_filtered = port_stats->rx_address_filtered;
	drvs->rx_alignment_symbol_errors =
		port_stats->rx_alignment_symbol_errors;
	drvs->rxpp_fifo_overflow_drop = port_stats->rxpp_fifo_overflow_drop;
	drvs->tx_pauseframes = port_stats->tx_pauseframes;
	drvs->tx_controlframes = port_stats->tx_controlframes;
	drvs->tx_priority_pauseframes = port_stats->tx_priority_pauseframes;
	drvs->jabber_events = port_stats->jabber_events;
	drvs->rx_drops_no_pbuf = rxf_stats->rx_drops_no_pbuf;
	drvs->rx_drops_no_erx_descr = rxf_stats->rx_drops_no_erx_descr;
	drvs->forwarded_packets = rxf_stats->forwarded_packets;
	drvs->rx_drops_mtu = rxf_stats->rx_drops_mtu;
	drvs->rx_drops_no_tpre_descr = rxf_stats->rx_drops_no_tpre_descr;
	drvs->rx_drops_too_many_frags = rxf_stats->rx_drops_too_many_frags;
	adapter->drv_stats.eth_red_drops = pmem_sts->eth_red_drops;
	if (be_roce_supported(adapter)) {
		drvs->rx_roce_bytes_lsd = port_stats->roce_bytes_received_lsd;
		drvs->rx_roce_bytes_msd = port_stats->roce_bytes_received_msd;
		drvs->rx_roce_frames = port_stats->roce_frames_received;
		drvs->roce_drops_crc = port_stats->roce_drops_crc;
		drvs->roce_drops_payload_len =
			port_stats->roce_drops_payload_len;
	}
}

static void populate_lancer_stats(struct be_adapter *adapter)
{
	struct be_drv_stats *drvs = &adapter->drv_stats;
	struct lancer_pport_stats *pport_stats = pport_stats_from_cmd(adapter);

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
	drvs->rx_address_filtered =
					pport_stats->rx_address_filtered +
					pport_stats->rx_vlan_filtered;
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

static void populate_erx_stats(struct be_adapter *adapter,
			       struct be_rx_obj *rxo, u32 erx_stat)
{
	if (!BEx_chip(adapter))
		rx_stats(rxo)->rx_drops_no_frags = erx_stat;
	else
		/* below erx HW counter can actually wrap around after
		 * 65535. Driver accumulates a 32-bit value
		 */
		accumulate_16bit_val(&rx_stats(rxo)->rx_drops_no_frags,
				     (u16)erx_stat);
}

void be_parse_stats(struct be_adapter *adapter)
{
	struct be_erx_stats_v2 *erx = be_erx_stats_from_cmd(adapter);
	struct be_rx_obj *rxo;
	int i;
	u32 erx_stat;

	if (lancer_chip(adapter)) {
		populate_lancer_stats(adapter);
	} else {
		if (BE2_chip(adapter))
			populate_be_v0_stats(adapter);
		else if (BE3_chip(adapter))
			/* for BE3 */
			populate_be_v1_stats(adapter);
		else
			populate_be_v2_stats(adapter);

		/* erx_v2 is longer than v0, v1. use v2 for v0, v1 access */
		for_all_rx_queues(adapter, rxo, i) {
			erx_stat = erx->rx_drops_no_fragments[rxo->q.id];
			populate_erx_stats(adapter, rxo, erx_stat);
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
			start = u64_stats_fetch_begin_irq(&rx_stats->sync);
			pkts = rx_stats(rxo)->rx_pkts;
			bytes = rx_stats(rxo)->rx_bytes;
		} while (u64_stats_fetch_retry_irq(&rx_stats->sync, start));
		stats->rx_packets += pkts;
		stats->rx_bytes += bytes;
		stats->multicast += rx_stats(rxo)->rx_mcast_pkts;
		stats->rx_dropped += rx_stats(rxo)->rx_drops_no_skbs +
					rx_stats(rxo)->rx_drops_no_frags;
	}

	for_all_tx_queues(adapter, txo, i) {
		const struct be_tx_stats *tx_stats = tx_stats(txo);

		do {
			start = u64_stats_fetch_begin_irq(&tx_stats->sync);
			pkts = tx_stats(txo)->tx_pkts;
			bytes = tx_stats(txo)->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&tx_stats->sync, start));
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

	if (link_status)
		netif_carrier_on(netdev);
	else
		netif_carrier_off(netdev);

	netdev_info(netdev, "Link is %s\n", link_status ? "Up" : "Down");
}

static void be_tx_stats_update(struct be_tx_obj *txo, struct sk_buff *skb)
{
	struct be_tx_stats *stats = tx_stats(txo);
	u64 tx_pkts = skb_shinfo(skb)->gso_segs ? : 1;

	u64_stats_update_begin(&stats->sync);
	stats->tx_reqs++;
	stats->tx_bytes += skb->len;
	stats->tx_pkts += tx_pkts;
	if (skb->encapsulation && skb->ip_summed == CHECKSUM_PARTIAL)
		stats->tx_vxlan_offload_pkts += tx_pkts;
	u64_stats_update_end(&stats->sync);
}

/* Returns number of WRBs needed for the skb */
static u32 skb_wrb_cnt(struct sk_buff *skb)
{
	/* +1 for the header wrb */
	return 1 + (skb_headlen(skb) ? 1 : 0) + skb_shinfo(skb)->nr_frags;
}

static inline void wrb_fill(struct be_eth_wrb *wrb, u64 addr, int len)
{
	wrb->frag_pa_hi = cpu_to_le32(upper_32_bits(addr));
	wrb->frag_pa_lo = cpu_to_le32(lower_32_bits(addr));
	wrb->frag_len = cpu_to_le32(len & ETH_WRB_FRAG_LEN_MASK);
	wrb->rsvd0 = 0;
}

/* A dummy wrb is just all zeros. Using a separate routine for dummy-wrb
 * to avoid the swap and shift/mask operations in wrb_fill().
 */
static inline void wrb_fill_dummy(struct be_eth_wrb *wrb)
{
	wrb->frag_pa_hi = 0;
	wrb->frag_pa_lo = 0;
	wrb->frag_len = 0;
	wrb->rsvd0 = 0;
}

static inline u16 be_get_tx_vlan_tag(struct be_adapter *adapter,
				     struct sk_buff *skb)
{
	u8 vlan_prio;
	u16 vlan_tag;

	vlan_tag = skb_vlan_tag_get(skb);
	vlan_prio = (vlan_tag & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
	/* If vlan priority provided by OS is NOT in available bmap */
	if (!(adapter->vlan_prio_bmap & (1 << vlan_prio)))
		vlan_tag = (vlan_tag & ~VLAN_PRIO_MASK) |
				adapter->recommended_prio;

	return vlan_tag;
}

/* Used only for IP tunnel packets */
static u16 skb_inner_ip_proto(struct sk_buff *skb)
{
	return (inner_ip_hdr(skb)->version == 4) ?
		inner_ip_hdr(skb)->protocol : inner_ipv6_hdr(skb)->nexthdr;
}

static u16 skb_ip_proto(struct sk_buff *skb)
{
	return (ip_hdr(skb)->version == 4) ?
		ip_hdr(skb)->protocol : ipv6_hdr(skb)->nexthdr;
}

static inline bool be_is_txq_full(struct be_tx_obj *txo)
{
	return atomic_read(&txo->q.used) + BE_MAX_TX_FRAG_COUNT >= txo->q.len;
}

static inline bool be_can_txq_wake(struct be_tx_obj *txo)
{
	return atomic_read(&txo->q.used) < txo->q.len / 2;
}

static inline bool be_is_tx_compl_pending(struct be_tx_obj *txo)
{
	return atomic_read(&txo->q.used) > txo->pend_wrb_cnt;
}

static void be_get_wrb_params_from_skb(struct be_adapter *adapter,
				       struct sk_buff *skb,
				       struct be_wrb_params *wrb_params)
{
	u16 proto;

	if (skb_is_gso(skb)) {
		BE_WRB_F_SET(wrb_params->features, LSO, 1);
		wrb_params->lso_mss = skb_shinfo(skb)->gso_size;
		if (skb_is_gso_v6(skb) && !lancer_chip(adapter))
			BE_WRB_F_SET(wrb_params->features, LSO6, 1);
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (skb->encapsulation) {
			BE_WRB_F_SET(wrb_params->features, IPCS, 1);
			proto = skb_inner_ip_proto(skb);
		} else {
			proto = skb_ip_proto(skb);
		}
		if (proto == IPPROTO_TCP)
			BE_WRB_F_SET(wrb_params->features, TCPCS, 1);
		else if (proto == IPPROTO_UDP)
			BE_WRB_F_SET(wrb_params->features, UDPCS, 1);
	}

	if (skb_vlan_tag_present(skb)) {
		BE_WRB_F_SET(wrb_params->features, VLAN, 1);
		wrb_params->vlan_tag = be_get_tx_vlan_tag(adapter, skb);
	}

	BE_WRB_F_SET(wrb_params->features, CRC, 1);
}

static void wrb_fill_hdr(struct be_adapter *adapter,
			 struct be_eth_hdr_wrb *hdr,
			 struct be_wrb_params *wrb_params,
			 struct sk_buff *skb)
{
	memset(hdr, 0, sizeof(*hdr));

	SET_TX_WRB_HDR_BITS(crc, hdr,
			    BE_WRB_F_GET(wrb_params->features, CRC));
	SET_TX_WRB_HDR_BITS(ipcs, hdr,
			    BE_WRB_F_GET(wrb_params->features, IPCS));
	SET_TX_WRB_HDR_BITS(tcpcs, hdr,
			    BE_WRB_F_GET(wrb_params->features, TCPCS));
	SET_TX_WRB_HDR_BITS(udpcs, hdr,
			    BE_WRB_F_GET(wrb_params->features, UDPCS));

	SET_TX_WRB_HDR_BITS(lso, hdr,
			    BE_WRB_F_GET(wrb_params->features, LSO));
	SET_TX_WRB_HDR_BITS(lso6, hdr,
			    BE_WRB_F_GET(wrb_params->features, LSO6));
	SET_TX_WRB_HDR_BITS(lso_mss, hdr, wrb_params->lso_mss);

	/* Hack to skip HW VLAN tagging needs evt = 1, compl = 0. When this
	 * hack is not needed, the evt bit is set while ringing DB.
	 */
	SET_TX_WRB_HDR_BITS(event, hdr,
			    BE_WRB_F_GET(wrb_params->features, VLAN_SKIP_HW));
	SET_TX_WRB_HDR_BITS(vlan, hdr,
			    BE_WRB_F_GET(wrb_params->features, VLAN));
	SET_TX_WRB_HDR_BITS(vlan_tag, hdr, wrb_params->vlan_tag);

	SET_TX_WRB_HDR_BITS(num_wrb, hdr, skb_wrb_cnt(skb));
	SET_TX_WRB_HDR_BITS(len, hdr, skb->len);
	SET_TX_WRB_HDR_BITS(mgmt, hdr,
			    BE_WRB_F_GET(wrb_params->features, OS2BMC));
}

static void unmap_tx_frag(struct device *dev, struct be_eth_wrb *wrb,
			  bool unmap_single)
{
	dma_addr_t dma;
	u32 frag_len = le32_to_cpu(wrb->frag_len);


	dma = (u64)le32_to_cpu(wrb->frag_pa_hi) << 32 |
		(u64)le32_to_cpu(wrb->frag_pa_lo);
	if (frag_len) {
		if (unmap_single)
			dma_unmap_single(dev, dma, frag_len, DMA_TO_DEVICE);
		else
			dma_unmap_page(dev, dma, frag_len, DMA_TO_DEVICE);
	}
}

/* Grab a WRB header for xmit */
static u16 be_tx_get_wrb_hdr(struct be_tx_obj *txo)
{
	u16 head = txo->q.head;

	queue_head_inc(&txo->q);
	return head;
}

/* Set up the WRB header for xmit */
static void be_tx_setup_wrb_hdr(struct be_adapter *adapter,
				struct be_tx_obj *txo,
				struct be_wrb_params *wrb_params,
				struct sk_buff *skb, u16 head)
{
	u32 num_frags = skb_wrb_cnt(skb);
	struct be_queue_info *txq = &txo->q;
	struct be_eth_hdr_wrb *hdr = queue_index_node(txq, head);

	wrb_fill_hdr(adapter, hdr, wrb_params, skb);
	be_dws_cpu_to_le(hdr, sizeof(*hdr));

	BUG_ON(txo->sent_skb_list[head]);
	txo->sent_skb_list[head] = skb;
	txo->last_req_hdr = head;
	atomic_add(num_frags, &txq->used);
	txo->last_req_wrb_cnt = num_frags;
	txo->pend_wrb_cnt += num_frags;
}

/* Setup a WRB fragment (buffer descriptor) for xmit */
static void be_tx_setup_wrb_frag(struct be_tx_obj *txo, dma_addr_t busaddr,
				 int len)
{
	struct be_eth_wrb *wrb;
	struct be_queue_info *txq = &txo->q;

	wrb = queue_head_node(txq);
	wrb_fill(wrb, busaddr, len);
	queue_head_inc(txq);
}

/* Bring the queue back to the state it was in before be_xmit_enqueue() routine
 * was invoked. The producer index is restored to the previous packet and the
 * WRBs of the current packet are unmapped. Invoked to handle tx setup errors.
 */
static void be_xmit_restore(struct be_adapter *adapter,
			    struct be_tx_obj *txo, u16 head, bool map_single,
			    u32 copied)
{
	struct device *dev;
	struct be_eth_wrb *wrb;
	struct be_queue_info *txq = &txo->q;

	dev = &adapter->pdev->dev;
	txq->head = head;

	/* skip the first wrb (hdr); it's not mapped */
	queue_head_inc(txq);
	while (copied) {
		wrb = queue_head_node(txq);
		unmap_tx_frag(dev, wrb, map_single);
		map_single = false;
		copied -= le32_to_cpu(wrb->frag_len);
		queue_head_inc(txq);
	}

	txq->head = head;
}

/* Enqueue the given packet for transmit. This routine allocates WRBs for the
 * packet, dma maps the packet buffers and sets up the WRBs. Returns the number
 * of WRBs used up by the packet.
 */
static u32 be_xmit_enqueue(struct be_adapter *adapter, struct be_tx_obj *txo,
			   struct sk_buff *skb,
			   struct be_wrb_params *wrb_params)
{
	u32 i, copied = 0, wrb_cnt = skb_wrb_cnt(skb);
	struct device *dev = &adapter->pdev->dev;
	struct be_queue_info *txq = &txo->q;
	bool map_single = false;
	u16 head = txq->head;
	dma_addr_t busaddr;
	int len;

	head = be_tx_get_wrb_hdr(txo);

	if (skb->len > skb->data_len) {
		len = skb_headlen(skb);

		busaddr = dma_map_single(dev, skb->data, len, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, busaddr))
			goto dma_err;
		map_single = true;
		be_tx_setup_wrb_frag(txo, busaddr, len);
		copied += len;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i];
		len = skb_frag_size(frag);

		busaddr = skb_frag_dma_map(dev, frag, 0, len, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, busaddr))
			goto dma_err;
		be_tx_setup_wrb_frag(txo, busaddr, len);
		copied += len;
	}

	be_tx_setup_wrb_hdr(adapter, txo, wrb_params, skb, head);

	be_tx_stats_update(txo, skb);
	return wrb_cnt;

dma_err:
	adapter->drv_stats.dma_map_errors++;
	be_xmit_restore(adapter, txo, head, map_single, copied);
	return 0;
}

static inline int qnq_async_evt_rcvd(struct be_adapter *adapter)
{
	return adapter->flags & BE_FLAGS_QNQ_ASYNC_EVT_RCVD;
}

static struct sk_buff *be_insert_vlan_in_pkt(struct be_adapter *adapter,
					     struct sk_buff *skb,
					     struct be_wrb_params
					     *wrb_params)
{
	u16 vlan_tag = 0;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		return skb;

	if (skb_vlan_tag_present(skb))
		vlan_tag = be_get_tx_vlan_tag(adapter, skb);

	if (qnq_async_evt_rcvd(adapter) && adapter->pvid) {
		if (!vlan_tag)
			vlan_tag = adapter->pvid;
		/* f/w workaround to set skip_hw_vlan = 1, informs the F/W to
		 * skip VLAN insertion
		 */
		BE_WRB_F_SET(wrb_params->features, VLAN_SKIP_HW, 1);
	}

	if (vlan_tag) {
		skb = vlan_insert_tag_set_proto(skb, htons(ETH_P_8021Q),
						vlan_tag);
		if (unlikely(!skb))
			return skb;
		skb->vlan_tci = 0;
	}

	/* Insert the outer VLAN, if any */
	if (adapter->qnq_vid) {
		vlan_tag = adapter->qnq_vid;
		skb = vlan_insert_tag_set_proto(skb, htons(ETH_P_8021Q),
						vlan_tag);
		if (unlikely(!skb))
			return skb;
		BE_WRB_F_SET(wrb_params->features, VLAN_SKIP_HW, 1);
	}

	return skb;
}

static bool be_ipv6_exthdr_check(struct sk_buff *skb)
{
	struct ethhdr *eh = (struct ethhdr *)skb->data;
	u16 offset = ETH_HLEN;

	if (eh->h_proto == htons(ETH_P_IPV6)) {
		struct ipv6hdr *ip6h = (struct ipv6hdr *)(skb->data + offset);

		offset += sizeof(struct ipv6hdr);
		if (ip6h->nexthdr != NEXTHDR_TCP &&
		    ip6h->nexthdr != NEXTHDR_UDP) {
			struct ipv6_opt_hdr *ehdr =
				(struct ipv6_opt_hdr *)(skb->data + offset);

			/* offending pkt: 2nd byte following IPv6 hdr is 0xff */
			if (ehdr->hdrlen == 0xff)
				return true;
		}
	}
	return false;
}

static int be_vlan_tag_tx_chk(struct be_adapter *adapter, struct sk_buff *skb)
{
	return skb_vlan_tag_present(skb) || adapter->pvid || adapter->qnq_vid;
}

static int be_ipv6_tx_stall_chk(struct be_adapter *adapter, struct sk_buff *skb)
{
	return BE3_chip(adapter) && be_ipv6_exthdr_check(skb);
}

static struct sk_buff *be_lancer_xmit_workarounds(struct be_adapter *adapter,
						  struct sk_buff *skb,
						  struct be_wrb_params
						  *wrb_params)
{
	struct vlan_ethhdr *veh = (struct vlan_ethhdr *)skb->data;
	unsigned int eth_hdr_len;
	struct iphdr *ip;

	/* For padded packets, BE HW modifies tot_len field in IP header
	 * incorrecly when VLAN tag is inserted by HW.
	 * For padded packets, Lancer computes incorrect checksum.
	 */
	eth_hdr_len = ntohs(skb->protocol) == ETH_P_8021Q ?
						VLAN_ETH_HLEN : ETH_HLEN;
	if (skb->len <= 60 &&
	    (lancer_chip(adapter) || skb_vlan_tag_present(skb)) &&
	    is_ipv4_pkt(skb)) {
		ip = (struct iphdr *)ip_hdr(skb);
		pskb_trim(skb, eth_hdr_len + ntohs(ip->tot_len));
	}

	/* If vlan tag is already inlined in the packet, skip HW VLAN
	 * tagging in pvid-tagging mode
	 */
	if (be_pvid_tagging_enabled(adapter) &&
	    veh->h_vlan_proto == htons(ETH_P_8021Q))
		BE_WRB_F_SET(wrb_params->features, VLAN_SKIP_HW, 1);

	/* HW has a bug wherein it will calculate CSUM for VLAN
	 * pkts even though it is disabled.
	 * Manually insert VLAN in pkt.
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL &&
	    skb_vlan_tag_present(skb)) {
		skb = be_insert_vlan_in_pkt(adapter, skb, wrb_params);
		if (unlikely(!skb))
			goto err;
	}

	/* HW may lockup when VLAN HW tagging is requested on
	 * certain ipv6 packets. Drop such pkts if the HW workaround to
	 * skip HW tagging is not enabled by FW.
	 */
	if (unlikely(be_ipv6_tx_stall_chk(adapter, skb) &&
		     (adapter->pvid || adapter->qnq_vid) &&
		     !qnq_async_evt_rcvd(adapter)))
		goto tx_drop;

	/* Manual VLAN tag insertion to prevent:
	 * ASIC lockup when the ASIC inserts VLAN tag into
	 * certain ipv6 packets. Insert VLAN tags in driver,
	 * and set event, completion, vlan bits accordingly
	 * in the Tx WRB.
	 */
	if (be_ipv6_tx_stall_chk(adapter, skb) &&
	    be_vlan_tag_tx_chk(adapter, skb)) {
		skb = be_insert_vlan_in_pkt(adapter, skb, wrb_params);
		if (unlikely(!skb))
			goto err;
	}

	return skb;
tx_drop:
	dev_kfree_skb_any(skb);
err:
	return NULL;
}

static struct sk_buff *be_xmit_workarounds(struct be_adapter *adapter,
					   struct sk_buff *skb,
					   struct be_wrb_params *wrb_params)
{
	/* Lancer, SH and BE3 in SRIOV mode have a bug wherein
	 * packets that are 32b or less may cause a transmit stall
	 * on that port. The workaround is to pad such packets
	 * (len <= 32 bytes) to a minimum length of 36b.
	 */
	if (skb->len <= 32) {
		if (skb_put_padto(skb, 36))
			return NULL;
	}

	if (BEx_chip(adapter) || lancer_chip(adapter)) {
		skb = be_lancer_xmit_workarounds(adapter, skb, wrb_params);
		if (!skb)
			return NULL;
	}

	return skb;
}

static void be_xmit_flush(struct be_adapter *adapter, struct be_tx_obj *txo)
{
	struct be_queue_info *txq = &txo->q;
	struct be_eth_hdr_wrb *hdr = queue_index_node(txq, txo->last_req_hdr);

	/* Mark the last request eventable if it hasn't been marked already */
	if (!(hdr->dw[2] & cpu_to_le32(TX_HDR_WRB_EVT)))
		hdr->dw[2] |= cpu_to_le32(TX_HDR_WRB_EVT | TX_HDR_WRB_COMPL);

	/* compose a dummy wrb if there are odd set of wrbs to notify */
	if (!lancer_chip(adapter) && (txo->pend_wrb_cnt & 1)) {
		wrb_fill_dummy(queue_head_node(txq));
		queue_head_inc(txq);
		atomic_inc(&txq->used);
		txo->pend_wrb_cnt++;
		hdr->dw[2] &= ~cpu_to_le32(TX_HDR_WRB_NUM_MASK <<
					   TX_HDR_WRB_NUM_SHIFT);
		hdr->dw[2] |= cpu_to_le32((txo->last_req_wrb_cnt + 1) <<
					  TX_HDR_WRB_NUM_SHIFT);
	}
	be_txq_notify(adapter, txo, txo->pend_wrb_cnt);
	txo->pend_wrb_cnt = 0;
}

/* OS2BMC related */

#define DHCP_CLIENT_PORT	68
#define DHCP_SERVER_PORT	67
#define NET_BIOS_PORT1		137
#define NET_BIOS_PORT2		138
#define DHCPV6_RAS_PORT		547

#define is_mc_allowed_on_bmc(adapter, eh)	\
	(!is_multicast_filt_enabled(adapter) &&	\
	 is_multicast_ether_addr(eh->h_dest) &&	\
	 !is_broadcast_ether_addr(eh->h_dest))

#define is_bc_allowed_on_bmc(adapter, eh)	\
	(!is_broadcast_filt_enabled(adapter) &&	\
	 is_broadcast_ether_addr(eh->h_dest))

#define is_arp_allowed_on_bmc(adapter, skb)	\
	(is_arp(skb) && is_arp_filt_enabled(adapter))

#define is_broadcast_packet(eh, adapter)	\
		(is_multicast_ether_addr(eh->h_dest) && \
		!compare_ether_addr(eh->h_dest, adapter->netdev->broadcast))

#define is_arp(skb)	(skb->protocol == htons(ETH_P_ARP))

#define is_arp_filt_enabled(adapter)	\
		(adapter->bmc_filt_mask & (BMC_FILT_BROADCAST_ARP))

#define is_dhcp_client_filt_enabled(adapter)	\
		(adapter->bmc_filt_mask & BMC_FILT_BROADCAST_DHCP_CLIENT)

#define is_dhcp_srvr_filt_enabled(adapter)	\
		(adapter->bmc_filt_mask & BMC_FILT_BROADCAST_DHCP_SERVER)

#define is_nbios_filt_enabled(adapter)	\
		(adapter->bmc_filt_mask & BMC_FILT_BROADCAST_NET_BIOS)

#define is_ipv6_na_filt_enabled(adapter)	\
		(adapter->bmc_filt_mask &	\
			BMC_FILT_MULTICAST_IPV6_NEIGH_ADVER)

#define is_ipv6_ra_filt_enabled(adapter)	\
		(adapter->bmc_filt_mask & BMC_FILT_MULTICAST_IPV6_RA)

#define is_ipv6_ras_filt_enabled(adapter)	\
		(adapter->bmc_filt_mask & BMC_FILT_MULTICAST_IPV6_RAS)

#define is_broadcast_filt_enabled(adapter)	\
		(adapter->bmc_filt_mask & BMC_FILT_BROADCAST)

#define is_multicast_filt_enabled(adapter)	\
		(adapter->bmc_filt_mask & BMC_FILT_MULTICAST)

static bool be_send_pkt_to_bmc(struct be_adapter *adapter,
			       struct sk_buff **skb)
{
	struct ethhdr *eh = (struct ethhdr *)(*skb)->data;
	bool os2bmc = false;

	if (!be_is_os2bmc_enabled(adapter))
		goto done;

	if (!is_multicast_ether_addr(eh->h_dest))
		goto done;

	if (is_mc_allowed_on_bmc(adapter, eh) ||
	    is_bc_allowed_on_bmc(adapter, eh) ||
	    is_arp_allowed_on_bmc(adapter, (*skb))) {
		os2bmc = true;
		goto done;
	}

	if ((*skb)->protocol == htons(ETH_P_IPV6)) {
		struct ipv6hdr *hdr = ipv6_hdr((*skb));
		u8 nexthdr = hdr->nexthdr;

		if (nexthdr == IPPROTO_ICMPV6) {
			struct icmp6hdr *icmp6 = icmp6_hdr((*skb));

			switch (icmp6->icmp6_type) {
			case NDISC_ROUTER_ADVERTISEMENT:
				os2bmc = is_ipv6_ra_filt_enabled(adapter);
				goto done;
			case NDISC_NEIGHBOUR_ADVERTISEMENT:
				os2bmc = is_ipv6_na_filt_enabled(adapter);
				goto done;
			default:
				break;
			}
		}
	}

	if (is_udp_pkt((*skb))) {
		struct udphdr *udp = udp_hdr((*skb));

		switch (ntohs(udp->dest)) {
		case DHCP_CLIENT_PORT:
			os2bmc = is_dhcp_client_filt_enabled(adapter);
			goto done;
		case DHCP_SERVER_PORT:
			os2bmc = is_dhcp_srvr_filt_enabled(adapter);
			goto done;
		case NET_BIOS_PORT1:
		case NET_BIOS_PORT2:
			os2bmc = is_nbios_filt_enabled(adapter);
			goto done;
		case DHCPV6_RAS_PORT:
			os2bmc = is_ipv6_ras_filt_enabled(adapter);
			goto done;
		default:
			break;
		}
	}
done:
	/* For packets over a vlan, which are destined
	 * to BMC, asic expects the vlan to be inline in the packet.
	 */
	if (os2bmc)
		*skb = be_insert_vlan_in_pkt(adapter, *skb, NULL);

	return os2bmc;
}

static netdev_tx_t be_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	u16 q_idx = skb_get_queue_mapping(skb);
	struct be_tx_obj *txo = &adapter->tx_obj[q_idx];
	struct be_wrb_params wrb_params = { 0 };
	bool flush = !skb->xmit_more;
	u16 wrb_cnt;

	skb = be_xmit_workarounds(adapter, skb, &wrb_params);
	if (unlikely(!skb))
		goto drop;

	be_get_wrb_params_from_skb(adapter, skb, &wrb_params);

	wrb_cnt = be_xmit_enqueue(adapter, txo, skb, &wrb_params);
	if (unlikely(!wrb_cnt)) {
		dev_kfree_skb_any(skb);
		goto drop;
	}

	/* if os2bmc is enabled and if the pkt is destined to bmc,
	 * enqueue the pkt a 2nd time with mgmt bit set.
	 */
	if (be_send_pkt_to_bmc(adapter, &skb)) {
		BE_WRB_F_SET(wrb_params.features, OS2BMC, 1);
		wrb_cnt = be_xmit_enqueue(adapter, txo, skb, &wrb_params);
		if (unlikely(!wrb_cnt))
			goto drop;
		else
			skb_get(skb);
	}

	if (be_is_txq_full(txo)) {
		netif_stop_subqueue(netdev, q_idx);
		tx_stats(txo)->tx_stops++;
	}

	if (flush || __netif_subqueue_stopped(netdev, q_idx))
		be_xmit_flush(adapter, txo);

	return NETDEV_TX_OK;
drop:
	tx_stats(txo)->tx_drv_drops++;
	/* Flush the already enqueued tx requests */
	if (flush && txo->pend_wrb_cnt)
		be_xmit_flush(adapter, txo);

	return NETDEV_TX_OK;
}

static int be_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct device *dev = &adapter->pdev->dev;

	if (new_mtu < BE_MIN_MTU || new_mtu > BE_MAX_MTU) {
		dev_info(dev, "MTU must be between %d and %d bytes\n",
			 BE_MIN_MTU, BE_MAX_MTU);
		return -EINVAL;
	}

	dev_info(dev, "MTU changed from %d to %d bytes\n",
		 netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;
	return 0;
}

static inline bool be_in_all_promisc(struct be_adapter *adapter)
{
	return (adapter->if_flags & BE_IF_FLAGS_ALL_PROMISCUOUS) ==
			BE_IF_FLAGS_ALL_PROMISCUOUS;
}

static int be_set_vlan_promisc(struct be_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	int status;

	if (adapter->if_flags & BE_IF_FLAGS_VLAN_PROMISCUOUS)
		return 0;

	status = be_cmd_rx_filter(adapter, BE_IF_FLAGS_VLAN_PROMISCUOUS, ON);
	if (!status) {
		dev_info(dev, "Enabled VLAN promiscuous mode\n");
		adapter->if_flags |= BE_IF_FLAGS_VLAN_PROMISCUOUS;
	} else {
		dev_err(dev, "Failed to enable VLAN promiscuous mode\n");
	}
	return status;
}

static int be_clear_vlan_promisc(struct be_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	int status;

	status = be_cmd_rx_filter(adapter, BE_IF_FLAGS_VLAN_PROMISCUOUS, OFF);
	if (!status) {
		dev_info(dev, "Disabling VLAN promiscuous mode\n");
		adapter->if_flags &= ~BE_IF_FLAGS_VLAN_PROMISCUOUS;
	}
	return status;
}

/*
 * A max of 64 (BE_NUM_VLANS_SUPPORTED) vlans can be configured in BE.
 * If the user configures more, place BE in vlan promiscuous mode.
 */
static int be_vid_config(struct be_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	u16 vids[BE_NUM_VLANS_SUPPORTED];
	u16 num = 0, i = 0;
	int status = 0;

	/* No need to further configure vids if in promiscuous mode */
	if (be_in_all_promisc(adapter))
		return 0;

	if (adapter->vlans_added > be_max_vlans(adapter))
		return be_set_vlan_promisc(adapter);

	/* Construct VLAN Table to give to HW */
	for_each_set_bit(i, adapter->vids, VLAN_N_VID)
		vids[num++] = cpu_to_le16(i);

	status = be_cmd_vlan_config(adapter, adapter->if_handle, vids, num, 0);
	if (status) {
		dev_err(dev, "Setting HW VLAN filtering failed\n");
		/* Set to VLAN promisc mode as setting VLAN filter failed */
		if (addl_status(status) == MCC_ADDL_STATUS_INSUFFICIENT_VLANS ||
		    addl_status(status) ==
				MCC_ADDL_STATUS_INSUFFICIENT_RESOURCES)
			return be_set_vlan_promisc(adapter);
	} else if (adapter->if_flags & BE_IF_FLAGS_VLAN_PROMISCUOUS) {
		status = be_clear_vlan_promisc(adapter);
	}
	return status;
}

static int be_vlan_add_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int status = 0;

	/* Packets with VID 0 are always received by Lancer by default */
	if (lancer_chip(adapter) && vid == 0)
		return status;

	if (test_bit(vid, adapter->vids))
		return status;

	set_bit(vid, adapter->vids);
	adapter->vlans_added++;

	status = be_vid_config(adapter);
	if (status) {
		adapter->vlans_added--;
		clear_bit(vid, adapter->vids);
	}

	return status;
}

static int be_vlan_rem_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	/* Packets with VID 0 are always received by Lancer by default */
	if (lancer_chip(adapter) && vid == 0)
		return 0;

	clear_bit(vid, adapter->vids);
	adapter->vlans_added--;

	return be_vid_config(adapter);
}

static void be_clear_all_promisc(struct be_adapter *adapter)
{
	be_cmd_rx_filter(adapter, BE_IF_FLAGS_ALL_PROMISCUOUS, OFF);
	adapter->if_flags &= ~BE_IF_FLAGS_ALL_PROMISCUOUS;
}

static void be_set_all_promisc(struct be_adapter *adapter)
{
	be_cmd_rx_filter(adapter, BE_IF_FLAGS_ALL_PROMISCUOUS, ON);
	adapter->if_flags |= BE_IF_FLAGS_ALL_PROMISCUOUS;
}

static void be_set_mc_promisc(struct be_adapter *adapter)
{
	int status;

	if (adapter->if_flags & BE_IF_FLAGS_MCAST_PROMISCUOUS)
		return;

	status = be_cmd_rx_filter(adapter, BE_IF_FLAGS_MCAST_PROMISCUOUS, ON);
	if (!status)
		adapter->if_flags |= BE_IF_FLAGS_MCAST_PROMISCUOUS;
}

static void be_set_mc_list(struct be_adapter *adapter)
{
	int status;

	status = be_cmd_rx_filter(adapter, BE_IF_FLAGS_MULTICAST, ON);
	if (!status)
		adapter->if_flags &= ~BE_IF_FLAGS_MCAST_PROMISCUOUS;
	else
		be_set_mc_promisc(adapter);
}

static void be_set_uc_list(struct be_adapter *adapter)
{
	struct netdev_hw_addr *ha;
	int i = 1; /* First slot is claimed by the Primary MAC */

	for (; adapter->uc_macs > 0; adapter->uc_macs--, i++)
		be_cmd_pmac_del(adapter, adapter->if_handle,
				adapter->pmac_id[i], 0);

	if (netdev_uc_count(adapter->netdev) > be_max_uc(adapter)) {
		be_set_all_promisc(adapter);
		return;
	}

	netdev_for_each_uc_addr(ha, adapter->netdev) {
		adapter->uc_macs++; /* First slot is for Primary MAC */
		be_cmd_pmac_add(adapter, (u8 *)ha->addr, adapter->if_handle,
				&adapter->pmac_id[adapter->uc_macs], 0);
	}
}

static void be_clear_uc_list(struct be_adapter *adapter)
{
	int i;

	for (i = 1; i < (adapter->uc_macs + 1); i++)
		be_cmd_pmac_del(adapter, adapter->if_handle,
				adapter->pmac_id[i], 0);
	adapter->uc_macs = 0;
}

static void be_set_rx_mode(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (netdev->flags & IFF_PROMISC) {
		be_set_all_promisc(adapter);
		return;
	}

	/* Interface was previously in promiscuous mode; disable it */
	if (be_in_all_promisc(adapter)) {
		be_clear_all_promisc(adapter);
		if (adapter->vlans_added)
			be_vid_config(adapter);
	}

	/* Enable multicast promisc if num configured exceeds what we support */
	if (netdev->flags & IFF_ALLMULTI ||
	    netdev_mc_count(netdev) > be_max_mc(adapter)) {
		be_set_mc_promisc(adapter);
		return;
	}

	if (netdev_uc_count(netdev) != adapter->uc_macs)
		be_set_uc_list(adapter);

	be_set_mc_list(adapter);
}

static int be_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_vf_cfg *vf_cfg = &adapter->vf_cfg[vf];
	int status;

	if (!sriov_enabled(adapter))
		return -EPERM;

	if (!is_valid_ether_addr(mac) || vf >= adapter->num_vfs)
		return -EINVAL;

	/* Proceed further only if user provided MAC is different
	 * from active MAC
	 */
	if (ether_addr_equal(mac, vf_cfg->mac_addr))
		return 0;

	if (BEx_chip(adapter)) {
		be_cmd_pmac_del(adapter, vf_cfg->if_handle, vf_cfg->pmac_id,
				vf + 1);

		status = be_cmd_pmac_add(adapter, mac, vf_cfg->if_handle,
					 &vf_cfg->pmac_id, vf + 1);
	} else {
		status = be_cmd_set_mac(adapter, mac, vf_cfg->if_handle,
					vf + 1);
	}

	if (status) {
		dev_err(&adapter->pdev->dev, "MAC %pM set on VF %d Failed: %#x",
			mac, vf, status);
		return be_cmd_status(status);
	}

	ether_addr_copy(vf_cfg->mac_addr, mac);

	return 0;
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
	vi->max_tx_rate = vf_cfg->tx_rate;
	vi->min_tx_rate = 0;
	vi->vlan = vf_cfg->vlan_tag & VLAN_VID_MASK;
	vi->qos = vf_cfg->vlan_tag >> VLAN_PRIO_SHIFT;
	memcpy(&vi->mac, vf_cfg->mac_addr, ETH_ALEN);
	vi->linkstate = adapter->vf_cfg[vf].plink_tracking;
	vi->spoofchk = adapter->vf_cfg[vf].spoofchk;

	return 0;
}

static int be_set_vf_tvt(struct be_adapter *adapter, int vf, u16 vlan)
{
	struct be_vf_cfg *vf_cfg = &adapter->vf_cfg[vf];
	u16 vids[BE_NUM_VLANS_SUPPORTED];
	int vf_if_id = vf_cfg->if_handle;
	int status;

	/* Enable Transparent VLAN Tagging */
	status = be_cmd_set_hsw_config(adapter, vlan, vf + 1, vf_if_id, 0, 0);
	if (status)
		return status;

	/* Clear pre-programmed VLAN filters on VF if any, if TVT is enabled */
	vids[0] = 0;
	status = be_cmd_vlan_config(adapter, vf_if_id, vids, 1, vf + 1);
	if (!status)
		dev_info(&adapter->pdev->dev,
			 "Cleared guest VLANs on VF%d", vf);

	/* After TVT is enabled, disallow VFs to program VLAN filters */
	if (vf_cfg->privileges & BE_PRIV_FILTMGMT) {
		status = be_cmd_set_fn_privileges(adapter, vf_cfg->privileges &
						  ~BE_PRIV_FILTMGMT, vf + 1);
		if (!status)
			vf_cfg->privileges &= ~BE_PRIV_FILTMGMT;
	}
	return 0;
}

static int be_clear_vf_tvt(struct be_adapter *adapter, int vf)
{
	struct be_vf_cfg *vf_cfg = &adapter->vf_cfg[vf];
	struct device *dev = &adapter->pdev->dev;
	int status;

	/* Reset Transparent VLAN Tagging. */
	status = be_cmd_set_hsw_config(adapter, BE_RESET_VLAN_TAG_ID, vf + 1,
				       vf_cfg->if_handle, 0, 0);
	if (status)
		return status;

	/* Allow VFs to program VLAN filtering */
	if (!(vf_cfg->privileges & BE_PRIV_FILTMGMT)) {
		status = be_cmd_set_fn_privileges(adapter, vf_cfg->privileges |
						  BE_PRIV_FILTMGMT, vf + 1);
		if (!status) {
			vf_cfg->privileges |= BE_PRIV_FILTMGMT;
			dev_info(dev, "VF%d: FILTMGMT priv enabled", vf);
		}
	}

	dev_info(dev,
		 "Disable/re-enable i/f in VM to clear Transparent VLAN tag");
	return 0;
}

static int be_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_vf_cfg *vf_cfg = &adapter->vf_cfg[vf];
	int status;

	if (!sriov_enabled(adapter))
		return -EPERM;

	if (vf >= adapter->num_vfs || vlan > 4095 || qos > 7)
		return -EINVAL;

	if (vlan || qos) {
		vlan |= qos << VLAN_PRIO_SHIFT;
		status = be_set_vf_tvt(adapter, vf, vlan);
	} else {
		status = be_clear_vf_tvt(adapter, vf);
	}

	if (status) {
		dev_err(&adapter->pdev->dev,
			"VLAN %d config on VF %d failed : %#x\n", vlan, vf,
			status);
		return be_cmd_status(status);
	}

	vf_cfg->vlan_tag = vlan;
	return 0;
}

static int be_set_vf_tx_rate(struct net_device *netdev, int vf,
			     int min_tx_rate, int max_tx_rate)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct device *dev = &adapter->pdev->dev;
	int percent_rate, status = 0;
	u16 link_speed = 0;
	u8 link_status;

	if (!sriov_enabled(adapter))
		return -EPERM;

	if (vf >= adapter->num_vfs)
		return -EINVAL;

	if (min_tx_rate)
		return -EINVAL;

	if (!max_tx_rate)
		goto config_qos;

	status = be_cmd_link_status_query(adapter, &link_speed,
					  &link_status, 0);
	if (status)
		goto err;

	if (!link_status) {
		dev_err(dev, "TX-rate setting not allowed when link is down\n");
		status = -ENETDOWN;
		goto err;
	}

	if (max_tx_rate < 100 || max_tx_rate > link_speed) {
		dev_err(dev, "TX-rate must be between 100 and %d Mbps\n",
			link_speed);
		status = -EINVAL;
		goto err;
	}

	/* On Skyhawk the QOS setting must be done only as a % value */
	percent_rate = link_speed / 100;
	if (skyhawk_chip(adapter) && (max_tx_rate % percent_rate)) {
		dev_err(dev, "TX-rate must be a multiple of %d Mbps\n",
			percent_rate);
		status = -EINVAL;
		goto err;
	}

config_qos:
	status = be_cmd_config_qos(adapter, max_tx_rate, link_speed, vf + 1);
	if (status)
		goto err;

	adapter->vf_cfg[vf].tx_rate = max_tx_rate;
	return 0;

err:
	dev_err(dev, "TX-rate setting of %dMbps on VF%d failed\n",
		max_tx_rate, vf);
	return be_cmd_status(status);
}

static int be_set_vf_link_state(struct net_device *netdev, int vf,
				int link_state)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	int status;

	if (!sriov_enabled(adapter))
		return -EPERM;

	if (vf >= adapter->num_vfs)
		return -EINVAL;

	status = be_cmd_set_logical_link_config(adapter, link_state, vf+1);
	if (status) {
		dev_err(&adapter->pdev->dev,
			"Link state change on VF %d failed: %#x\n", vf, status);
		return be_cmd_status(status);
	}

	adapter->vf_cfg[vf].plink_tracking = link_state;

	return 0;
}

static int be_set_vf_spoofchk(struct net_device *netdev, int vf, bool enable)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_vf_cfg *vf_cfg = &adapter->vf_cfg[vf];
	u8 spoofchk;
	int status;

	if (!sriov_enabled(adapter))
		return -EPERM;

	if (vf >= adapter->num_vfs)
		return -EINVAL;

	if (BEx_chip(adapter))
		return -EOPNOTSUPP;

	if (enable == vf_cfg->spoofchk)
		return 0;

	spoofchk = enable ? ENABLE_MAC_SPOOFCHK : DISABLE_MAC_SPOOFCHK;

	status = be_cmd_set_hsw_config(adapter, 0, vf + 1, vf_cfg->if_handle,
				       0, spoofchk);
	if (status) {
		dev_err(&adapter->pdev->dev,
			"Spoofchk change on VF %d failed: %#x\n", vf, status);
		return be_cmd_status(status);
	}

	vf_cfg->spoofchk = enable;
	return 0;
}

static void be_aic_update(struct be_aic_obj *aic, u64 rx_pkts, u64 tx_pkts,
			  ulong now)
{
	aic->rx_pkts_prev = rx_pkts;
	aic->tx_reqs_prev = tx_pkts;
	aic->jiffies = now;
}

static int be_get_new_eqd(struct be_eq_obj *eqo)
{
	struct be_adapter *adapter = eqo->adapter;
	int eqd, start;
	struct be_aic_obj *aic;
	struct be_rx_obj *rxo;
	struct be_tx_obj *txo;
	u64 rx_pkts = 0, tx_pkts = 0;
	ulong now;
	u32 pps, delta;
	int i;

	aic = &adapter->aic_obj[eqo->idx];
	if (!aic->enable) {
		if (aic->jiffies)
			aic->jiffies = 0;
		eqd = aic->et_eqd;
		return eqd;
	}

	for_all_rx_queues_on_eq(adapter, eqo, rxo, i) {
		do {
			start = u64_stats_fetch_begin_irq(&rxo->stats.sync);
			rx_pkts += rxo->stats.rx_pkts;
		} while (u64_stats_fetch_retry_irq(&rxo->stats.sync, start));
	}

	for_all_tx_queues_on_eq(adapter, eqo, txo, i) {
		do {
			start = u64_stats_fetch_begin_irq(&txo->stats.sync);
			tx_pkts += txo->stats.tx_reqs;
		} while (u64_stats_fetch_retry_irq(&txo->stats.sync, start));
	}

	/* Skip, if wrapped around or first calculation */
	now = jiffies;
	if (!aic->jiffies || time_before(now, aic->jiffies) ||
	    rx_pkts < aic->rx_pkts_prev ||
	    tx_pkts < aic->tx_reqs_prev) {
		be_aic_update(aic, rx_pkts, tx_pkts, now);
		return aic->prev_eqd;
	}

	delta = jiffies_to_msecs(now - aic->jiffies);
	if (delta == 0)
		return aic->prev_eqd;

	pps = (((u32)(rx_pkts - aic->rx_pkts_prev) * 1000) / delta) +
		(((u32)(tx_pkts - aic->tx_reqs_prev) * 1000) / delta);
	eqd = (pps / 15000) << 2;

	if (eqd < 8)
		eqd = 0;
	eqd = min_t(u32, eqd, aic->max_eqd);
	eqd = max_t(u32, eqd, aic->min_eqd);

	be_aic_update(aic, rx_pkts, tx_pkts, now);

	return eqd;
}

/* For Skyhawk-R only */
static u32 be_get_eq_delay_mult_enc(struct be_eq_obj *eqo)
{
	struct be_adapter *adapter = eqo->adapter;
	struct be_aic_obj *aic = &adapter->aic_obj[eqo->idx];
	ulong now = jiffies;
	int eqd;
	u32 mult_enc;

	if (!aic->enable)
		return 0;

	if (time_before_eq(now, aic->jiffies) ||
	    jiffies_to_msecs(now - aic->jiffies) < 1)
		eqd = aic->prev_eqd;
	else
		eqd = be_get_new_eqd(eqo);

	if (eqd > 100)
		mult_enc = R2I_DLY_ENC_1;
	else if (eqd > 60)
		mult_enc = R2I_DLY_ENC_2;
	else if (eqd > 20)
		mult_enc = R2I_DLY_ENC_3;
	else
		mult_enc = R2I_DLY_ENC_0;

	aic->prev_eqd = eqd;

	return mult_enc;
}

void be_eqd_update(struct be_adapter *adapter, bool force_update)
{
	struct be_set_eqd set_eqd[MAX_EVT_QS];
	struct be_aic_obj *aic;
	struct be_eq_obj *eqo;
	int i, num = 0, eqd;

	for_all_evt_queues(adapter, eqo, i) {
		aic = &adapter->aic_obj[eqo->idx];
		eqd = be_get_new_eqd(eqo);
		if (force_update || eqd != aic->prev_eqd) {
			set_eqd[num].delay_multiplier = (eqd * 65)/100;
			set_eqd[num].eq_id = eqo->q.id;
			aic->prev_eqd = eqd;
			num++;
		}
	}

	if (num)
		be_cmd_modify_eqd(adapter, set_eqd, num);
}

static void be_rx_stats_update(struct be_rx_obj *rxo,
			       struct be_rx_compl_info *rxcp)
{
	struct be_rx_stats *stats = rx_stats(rxo);

	u64_stats_update_begin(&stats->sync);
	stats->rx_compl++;
	stats->rx_bytes += rxcp->pkt_size;
	stats->rx_pkts++;
	if (rxcp->tunneled)
		stats->rx_vxlan_offload_pkts++;
	if (rxcp->pkt_type == BE_MULTICAST_PACKET)
		stats->rx_mcast_pkts++;
	if (rxcp->err)
		stats->rx_compl_err++;
	u64_stats_update_end(&stats->sync);
}

static inline bool csum_passed(struct be_rx_compl_info *rxcp)
{
	/* L4 checksum is not reliable for non TCP/UDP packets.
	 * Also ignore ipcksm for ipv6 pkts
	 */
	return (rxcp->tcpf || rxcp->udpf) && rxcp->l4_csum &&
		(rxcp->ip_csum || rxcp->ipv6) && !rxcp->err;
}

static struct be_rx_page_info *get_rx_page_info(struct be_rx_obj *rxo)
{
	struct be_adapter *adapter = rxo->adapter;
	struct be_rx_page_info *rx_page_info;
	struct be_queue_info *rxq = &rxo->q;
	u16 frag_idx = rxq->tail;

	rx_page_info = &rxo->page_info_tbl[frag_idx];
	BUG_ON(!rx_page_info->page);

	if (rx_page_info->last_frag) {
		dma_unmap_page(&adapter->pdev->dev,
			       dma_unmap_addr(rx_page_info, bus),
			       adapter->big_page_size, DMA_FROM_DEVICE);
		rx_page_info->last_frag = false;
	} else {
		dma_sync_single_for_cpu(&adapter->pdev->dev,
					dma_unmap_addr(rx_page_info, bus),
					rx_frag_size, DMA_FROM_DEVICE);
	}

	queue_tail_inc(rxq);
	atomic_dec(&rxq->used);
	return rx_page_info;
}

/* Throwaway the data in the Rx completion */
static void be_rx_compl_discard(struct be_rx_obj *rxo,
				struct be_rx_compl_info *rxcp)
{
	struct be_rx_page_info *page_info;
	u16 i, num_rcvd = rxcp->num_rcvd;

	for (i = 0; i < num_rcvd; i++) {
		page_info = get_rx_page_info(rxo);
		put_page(page_info->page);
		memset(page_info, 0, sizeof(*page_info));
	}
}

/*
 * skb_fill_rx_data forms a complete skb for an ether frame
 * indicated by rxcp.
 */
static void skb_fill_rx_data(struct be_rx_obj *rxo, struct sk_buff *skb,
			     struct be_rx_compl_info *rxcp)
{
	struct be_rx_page_info *page_info;
	u16 i, j;
	u16 hdr_len, curr_frag_len, remaining;
	u8 *start;

	page_info = get_rx_page_info(rxo);
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
		skb_frag_size_set(&skb_shinfo(skb)->frags[0],
				  curr_frag_len - hdr_len);
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
	remaining = rxcp->pkt_size - curr_frag_len;
	for (i = 1, j = 0; i < rxcp->num_rcvd; i++) {
		page_info = get_rx_page_info(rxo);
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
		page_info->page = NULL;
	}
	BUG_ON(j > MAX_SKB_FRAGS);
}

/* Process the RX completion indicated by rxcp when GRO is disabled */
static void be_rx_compl_process(struct be_rx_obj *rxo, struct napi_struct *napi,
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
		skb_set_hash(skb, rxcp->rss_hash, PKT_HASH_TYPE_L3);

	skb->csum_level = rxcp->tunneled;
	skb_mark_napi_id(skb, napi);

	if (rxcp->vlanf)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), rxcp->vlan_tag);

	netif_receive_skb(skb);
}

/* Process the RX completion indicated by rxcp when GRO is enabled */
static void be_rx_compl_process_gro(struct be_rx_obj *rxo,
				    struct napi_struct *napi,
				    struct be_rx_compl_info *rxcp)
{
	struct be_adapter *adapter = rxo->adapter;
	struct be_rx_page_info *page_info;
	struct sk_buff *skb = NULL;
	u16 remaining, curr_frag_len;
	u16 i, j;

	skb = napi_get_frags(napi);
	if (!skb) {
		be_rx_compl_discard(rxo, rxcp);
		return;
	}

	remaining = rxcp->pkt_size;
	for (i = 0, j = -1; i < rxcp->num_rcvd; i++) {
		page_info = get_rx_page_info(rxo);

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
		memset(page_info, 0, sizeof(*page_info));
	}
	BUG_ON(j > MAX_SKB_FRAGS);

	skb_shinfo(skb)->nr_frags = j + 1;
	skb->len = rxcp->pkt_size;
	skb->data_len = rxcp->pkt_size;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_record_rx_queue(skb, rxo - &adapter->rx_obj[0]);
	if (adapter->netdev->features & NETIF_F_RXHASH)
		skb_set_hash(skb, rxcp->rss_hash, PKT_HASH_TYPE_L3);

	skb->csum_level = rxcp->tunneled;
	skb_mark_napi_id(skb, napi);

	if (rxcp->vlanf)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), rxcp->vlan_tag);

	napi_gro_frags(napi);
}

static void be_parse_rx_compl_v1(struct be_eth_rx_compl *compl,
				 struct be_rx_compl_info *rxcp)
{
	rxcp->pkt_size = GET_RX_COMPL_V1_BITS(pktsize, compl);
	rxcp->vlanf = GET_RX_COMPL_V1_BITS(vtp, compl);
	rxcp->err = GET_RX_COMPL_V1_BITS(err, compl);
	rxcp->tcpf = GET_RX_COMPL_V1_BITS(tcpf, compl);
	rxcp->udpf = GET_RX_COMPL_V1_BITS(udpf, compl);
	rxcp->ip_csum = GET_RX_COMPL_V1_BITS(ipcksm, compl);
	rxcp->l4_csum = GET_RX_COMPL_V1_BITS(l4_cksm, compl);
	rxcp->ipv6 = GET_RX_COMPL_V1_BITS(ip_version, compl);
	rxcp->num_rcvd = GET_RX_COMPL_V1_BITS(numfrags, compl);
	rxcp->pkt_type = GET_RX_COMPL_V1_BITS(cast_enc, compl);
	rxcp->rss_hash = GET_RX_COMPL_V1_BITS(rsshash, compl);
	if (rxcp->vlanf) {
		rxcp->qnq = GET_RX_COMPL_V1_BITS(qnq, compl);
		rxcp->vlan_tag = GET_RX_COMPL_V1_BITS(vlan_tag, compl);
	}
	rxcp->port = GET_RX_COMPL_V1_BITS(port, compl);
	rxcp->tunneled =
		GET_RX_COMPL_V1_BITS(tunneled, compl);
}

static void be_parse_rx_compl_v0(struct be_eth_rx_compl *compl,
				 struct be_rx_compl_info *rxcp)
{
	rxcp->pkt_size = GET_RX_COMPL_V0_BITS(pktsize, compl);
	rxcp->vlanf = GET_RX_COMPL_V0_BITS(vtp, compl);
	rxcp->err = GET_RX_COMPL_V0_BITS(err, compl);
	rxcp->tcpf = GET_RX_COMPL_V0_BITS(tcpf, compl);
	rxcp->udpf = GET_RX_COMPL_V0_BITS(udpf, compl);
	rxcp->ip_csum = GET_RX_COMPL_V0_BITS(ipcksm, compl);
	rxcp->l4_csum = GET_RX_COMPL_V0_BITS(l4_cksm, compl);
	rxcp->ipv6 = GET_RX_COMPL_V0_BITS(ip_version, compl);
	rxcp->num_rcvd = GET_RX_COMPL_V0_BITS(numfrags, compl);
	rxcp->pkt_type = GET_RX_COMPL_V0_BITS(cast_enc, compl);
	rxcp->rss_hash = GET_RX_COMPL_V0_BITS(rsshash, compl);
	if (rxcp->vlanf) {
		rxcp->qnq = GET_RX_COMPL_V0_BITS(qnq, compl);
		rxcp->vlan_tag = GET_RX_COMPL_V0_BITS(vlan_tag, compl);
	}
	rxcp->port = GET_RX_COMPL_V0_BITS(port, compl);
	rxcp->ip_frag = GET_RX_COMPL_V0_BITS(ip_frag, compl);
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

	if (rxcp->ip_frag)
		rxcp->l4_csum = 0;

	if (rxcp->vlanf) {
		/* In QNQ modes, if qnq bit is not set, then the packet was
		 * tagged only with the transparent outer vlan-tag and must
		 * not be treated as a vlan packet by host
		 */
		if (be_is_qnq_mode(adapter) && !rxcp->qnq)
			rxcp->vlanf = 0;

		if (!lancer_chip(adapter))
			rxcp->vlan_tag = swab16(rxcp->vlan_tag);

		if (adapter->pvid == (rxcp->vlan_tag & VLAN_VID_MASK) &&
		    !test_bit(rxcp->vlan_tag, adapter->vids))
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
static void be_post_rx_frags(struct be_rx_obj *rxo, gfp_t gfp, u32 frags_needed)
{
	struct be_adapter *adapter = rxo->adapter;
	struct be_rx_page_info *page_info = NULL, *prev_page_info = NULL;
	struct be_queue_info *rxq = &rxo->q;
	struct page *pagep = NULL;
	struct device *dev = &adapter->pdev->dev;
	struct be_eth_rx_d *rxd;
	u64 page_dmaaddr = 0, frag_dmaaddr;
	u32 posted, page_offset = 0, notify = 0;

	page_info = &rxo->page_info_tbl[rxq->head];
	for (posted = 0; posted < frags_needed && !page_info->page; posted++) {
		if (!pagep) {
			pagep = be_alloc_pages(adapter->big_page_size, gfp);
			if (unlikely(!pagep)) {
				rx_stats(rxo)->rx_post_fail++;
				break;
			}
			page_dmaaddr = dma_map_page(dev, pagep, 0,
						    adapter->big_page_size,
						    DMA_FROM_DEVICE);
			if (dma_mapping_error(dev, page_dmaaddr)) {
				put_page(pagep);
				pagep = NULL;
				adapter->drv_stats.dma_map_errors++;
				break;
			}
			page_offset = 0;
		} else {
			get_page(pagep);
			page_offset += rx_frag_size;
		}
		page_info->page_offset = page_offset;
		page_info->page = pagep;

		rxd = queue_head_node(rxq);
		frag_dmaaddr = page_dmaaddr + page_info->page_offset;
		rxd->fragpa_lo = cpu_to_le32(frag_dmaaddr & 0xFFFFFFFF);
		rxd->fragpa_hi = cpu_to_le32(upper_32_bits(frag_dmaaddr));

		/* Any space left in the current big page for another frag? */
		if ((page_offset + rx_frag_size + rx_frag_size) >
					adapter->big_page_size) {
			pagep = NULL;
			page_info->last_frag = true;
			dma_unmap_addr_set(page_info, bus, page_dmaaddr);
		} else {
			dma_unmap_addr_set(page_info, bus, frag_dmaaddr);
		}

		prev_page_info = page_info;
		queue_head_inc(rxq);
		page_info = &rxo->page_info_tbl[rxq->head];
	}

	/* Mark the last frag of a page when we break out of the above loop
	 * with no more slots available in the RXQ
	 */
	if (pagep) {
		prev_page_info->last_frag = true;
		dma_unmap_addr_set(prev_page_info, bus, page_dmaaddr);
	}

	if (posted) {
		atomic_add(posted, &rxq->used);
		if (rxo->rx_post_starved)
			rxo->rx_post_starved = false;
		do {
			notify = min(MAX_NUM_POST_ERX_DB, posted);
			be_rxq_notify(adapter, rxq->id, notify);
			posted -= notify;
		} while (posted);
	} else if (atomic_read(&rxq->used) == 0) {
		/* Let be_worker replenish when memory is available */
		rxo->rx_post_starved = true;
	}
}

static struct be_tx_compl_info *be_tx_compl_get(struct be_tx_obj *txo)
{
	struct be_queue_info *tx_cq = &txo->cq;
	struct be_tx_compl_info *txcp = &txo->txcp;
	struct be_eth_tx_compl *compl = queue_tail_node(tx_cq);

	if (compl->dw[offsetof(struct amap_eth_tx_compl, valid) / 32] == 0)
		return NULL;

	/* Ensure load ordering of valid bit dword and other dwords below */
	rmb();
	be_dws_le_to_cpu(compl, sizeof(*compl));

	txcp->status = GET_TX_COMPL_BITS(status, compl);
	txcp->end_index = GET_TX_COMPL_BITS(wrb_index, compl);

	compl->dw[offsetof(struct amap_eth_tx_compl, valid) / 32] = 0;
	queue_tail_inc(tx_cq);
	return txcp;
}

static u16 be_tx_compl_process(struct be_adapter *adapter,
			       struct be_tx_obj *txo, u16 last_index)
{
	struct sk_buff **sent_skbs = txo->sent_skb_list;
	struct be_queue_info *txq = &txo->q;
	u16 frag_index, num_wrbs = 0;
	struct sk_buff *skb = NULL;
	bool unmap_skb_hdr = false;
	struct be_eth_wrb *wrb;

	do {
		if (sent_skbs[txq->tail]) {
			/* Free skb from prev req */
			if (skb)
				dev_consume_skb_any(skb);
			skb = sent_skbs[txq->tail];
			sent_skbs[txq->tail] = NULL;
			queue_tail_inc(txq);  /* skip hdr wrb */
			num_wrbs++;
			unmap_skb_hdr = true;
		}
		wrb = queue_tail_node(txq);
		frag_index = txq->tail;
		unmap_tx_frag(&adapter->pdev->dev, wrb,
			      (unmap_skb_hdr && skb_headlen(skb)));
		unmap_skb_hdr = false;
		queue_tail_inc(txq);
		num_wrbs++;
	} while (frag_index != last_index);
	dev_consume_skb_any(skb);

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

/* Leaves the EQ is disarmed state */
static void be_eq_clean(struct be_eq_obj *eqo)
{
	int num = events_get(eqo);

	be_eq_notify(eqo->adapter, eqo->q.id, false, true, num, 0);
}

/* Free posted rx buffers that were not used */
static void be_rxq_clean(struct be_rx_obj *rxo)
{
	struct be_queue_info *rxq = &rxo->q;
	struct be_rx_page_info *page_info;

	while (atomic_read(&rxq->used) > 0) {
		page_info = get_rx_page_info(rxo);
		put_page(page_info->page);
		memset(page_info, 0, sizeof(*page_info));
	}
	BUG_ON(atomic_read(&rxq->used));
	rxq->tail = 0;
	rxq->head = 0;
}

static void be_rx_cq_clean(struct be_rx_obj *rxo)
{
	struct be_queue_info *rx_cq = &rxo->cq;
	struct be_rx_compl_info *rxcp;
	struct be_adapter *adapter = rxo->adapter;
	int flush_wait = 0;

	/* Consume pending rx completions.
	 * Wait for the flush completion (identified by zero num_rcvd)
	 * to arrive. Notify CQ even when there are no more CQ entries
	 * for HW to flush partially coalesced CQ entries.
	 * In Lancer, there is no need to wait for flush compl.
	 */
	for (;;) {
		rxcp = be_rx_compl_get(rxo);
		if (!rxcp) {
			if (lancer_chip(adapter))
				break;

			if (flush_wait++ > 50 ||
			    be_check_error(adapter,
					   BE_ERROR_HW)) {
				dev_warn(&adapter->pdev->dev,
					 "did not receive flush compl\n");
				break;
			}
			be_cq_notify(adapter, rx_cq->id, true, 0);
			mdelay(1);
		} else {
			be_rx_compl_discard(rxo, rxcp);
			be_cq_notify(adapter, rx_cq->id, false, 1);
			if (rxcp->num_rcvd == 0)
				break;
		}
	}

	/* After cleanup, leave the CQ in unarmed state */
	be_cq_notify(adapter, rx_cq->id, false, 0);
}

static void be_tx_compl_clean(struct be_adapter *adapter)
{
	u16 end_idx, notified_idx, cmpl = 0, timeo = 0, num_wrbs = 0;
	struct device *dev = &adapter->pdev->dev;
	struct be_tx_compl_info *txcp;
	struct be_queue_info *txq;
	struct be_tx_obj *txo;
	int i, pending_txqs;

	/* Stop polling for compls when HW has been silent for 10ms */
	do {
		pending_txqs = adapter->num_tx_qs;

		for_all_tx_queues(adapter, txo, i) {
			cmpl = 0;
			num_wrbs = 0;
			txq = &txo->q;
			while ((txcp = be_tx_compl_get(txo))) {
				num_wrbs +=
					be_tx_compl_process(adapter, txo,
							    txcp->end_index);
				cmpl++;
			}
			if (cmpl) {
				be_cq_notify(adapter, txo->cq.id, false, cmpl);
				atomic_sub(num_wrbs, &txq->used);
				timeo = 0;
			}
			if (!be_is_tx_compl_pending(txo))
				pending_txqs--;
		}

		if (pending_txqs == 0 || ++timeo > 10 ||
		    be_check_error(adapter, BE_ERROR_HW))
			break;

		mdelay(1);
	} while (true);

	/* Free enqueued TX that was never notified to HW */
	for_all_tx_queues(adapter, txo, i) {
		txq = &txo->q;

		if (atomic_read(&txq->used)) {
			dev_info(dev, "txq%d: cleaning %d pending tx-wrbs\n",
				 i, atomic_read(&txq->used));
			notified_idx = txq->tail;
			end_idx = txq->tail;
			index_adv(&end_idx, atomic_read(&txq->used) - 1,
				  txq->len);
			/* Use the tx-compl process logic to handle requests
			 * that were not sent to the HW.
			 */
			num_wrbs = be_tx_compl_process(adapter, txo, end_idx);
			atomic_sub(num_wrbs, &txq->used);
			BUG_ON(atomic_read(&txq->used));
			txo->pend_wrb_cnt = 0;
			/* Since hw was never notified of these requests,
			 * reset TXQ indices
			 */
			txq->head = notified_idx;
			txq->tail = notified_idx;
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
			napi_hash_del(&eqo->napi);
			netif_napi_del(&eqo->napi);
			free_cpumask_var(eqo->affinity_mask);
		}
		be_queue_free(adapter, &eqo->q);
	}
}

static int be_evt_queues_create(struct be_adapter *adapter)
{
	struct be_queue_info *eq;
	struct be_eq_obj *eqo;
	struct be_aic_obj *aic;
	int i, rc;

	adapter->num_evt_qs = min_t(u16, num_irqs(adapter),
				    adapter->cfg_num_qs);

	for_all_evt_queues(adapter, eqo, i) {
		int numa_node = dev_to_node(&adapter->pdev->dev);

		aic = &adapter->aic_obj[i];
		eqo->adapter = adapter;
		eqo->idx = i;
		aic->max_eqd = BE_MAX_EQD;
		aic->enable = true;

		eq = &eqo->q;
		rc = be_queue_alloc(adapter, eq, EVNT_Q_LEN,
				    sizeof(struct be_eq_entry));
		if (rc)
			return rc;

		rc = be_cmd_eq_create(adapter, eqo);
		if (rc)
			return rc;

		if (!zalloc_cpumask_var(&eqo->affinity_mask, GFP_KERNEL))
			return -ENOMEM;
		cpumask_set_cpu(cpumask_local_spread(i, numa_node),
				eqo->affinity_mask);
		netif_napi_add(adapter->netdev, &eqo->napi, be_poll,
			       BE_NAPI_WEIGHT);
		napi_hash_add(&eqo->napi);
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

static int be_tx_qs_create(struct be_adapter *adapter)
{
	struct be_queue_info *cq;
	struct be_tx_obj *txo;
	struct be_eq_obj *eqo;
	int status, i;

	adapter->num_tx_qs = min(adapter->num_evt_qs, be_max_txqs(adapter));

	for_all_tx_queues(adapter, txo, i) {
		cq = &txo->cq;
		status = be_queue_alloc(adapter, cq, TX_CQ_LEN,
					sizeof(struct be_eth_tx_compl));
		if (status)
			return status;

		u64_stats_init(&txo->stats.sync);
		u64_stats_init(&txo->stats.sync_compl);

		/* If num_evt_qs is less than num_tx_qs, then more than
		 * one txq share an eq
		 */
		eqo = &adapter->eq_obj[i % adapter->num_evt_qs];
		status = be_cmd_cq_create(adapter, cq, &eqo->q, false, 3);
		if (status)
			return status;

		status = be_queue_alloc(adapter, &txo->q, TX_Q_LEN,
					sizeof(struct be_eth_wrb));
		if (status)
			return status;

		status = be_cmd_txq_create(adapter, txo);
		if (status)
			return status;

		netif_set_xps_queue(adapter->netdev, eqo->affinity_mask,
				    eqo->idx);
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

	/* We can create as many RSS rings as there are EQs. */
	adapter->num_rss_qs = adapter->num_evt_qs;

	/* We'll use RSS only if atleast 2 RSS rings are supported. */
	if (adapter->num_rss_qs <= 1)
		adapter->num_rss_qs = 0;

	adapter->num_rx_qs = adapter->num_rss_qs + adapter->need_def_rxq;

	/* When the interface is not capable of RSS rings (and there is no
	 * need to create a default RXQ) we'll still need one RXQ
	 */
	if (adapter->num_rx_qs == 0)
		adapter->num_rx_qs = 1;

	adapter->big_page_size = (1 << get_order(rx_frag_size)) * PAGE_SIZE;
	for_all_rx_queues(adapter, rxo, i) {
		rxo->adapter = adapter;
		cq = &rxo->cq;
		rc = be_queue_alloc(adapter, cq, RX_CQ_LEN,
				    sizeof(struct be_eth_rx_compl));
		if (rc)
			return rc;

		u64_stats_init(&rxo->stats.sync);
		eq = &adapter->eq_obj[i % adapter->num_evt_qs].q;
		rc = be_cmd_cq_create(adapter, cq, eq, false, 3);
		if (rc)
			return rc;
	}

	dev_info(&adapter->pdev->dev,
		 "created %d RX queue(s)\n", adapter->num_rx_qs);
	return 0;
}

static irqreturn_t be_intx(int irq, void *dev)
{
	struct be_eq_obj *eqo = dev;
	struct be_adapter *adapter = eqo->adapter;
	int num_evts = 0;

	/* IRQ is not expected when NAPI is scheduled as the EQ
	 * will not be armed.
	 * But, this can happen on Lancer INTx where it takes
	 * a while to de-assert INTx or in BE2 where occasionaly
	 * an interrupt may be raised even when EQ is unarmed.
	 * If NAPI is already scheduled, then counting & notifying
	 * events will orphan them.
	 */
	if (napi_schedule_prep(&eqo->napi)) {
		num_evts = events_get(eqo);
		__napi_schedule(&eqo->napi);
		if (num_evts)
			eqo->spurious_intr = 0;
	}
	be_eq_notify(adapter, eqo->q.id, false, true, num_evts, 0);

	/* Return IRQ_HANDLED only for the the first spurious intr
	 * after a valid intr to stop the kernel from branding
	 * this irq as a bad one!
	 */
	if (num_evts || eqo->spurious_intr++ == 0)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static irqreturn_t be_msix(int irq, void *dev)
{
	struct be_eq_obj *eqo = dev;

	be_eq_notify(eqo->adapter, eqo->q.id, false, true, 0, 0);
	napi_schedule(&eqo->napi);
	return IRQ_HANDLED;
}

static inline bool do_gro(struct be_rx_compl_info *rxcp)
{
	return (rxcp->tcpf && !rxcp->err && rxcp->l4_csum) ? true : false;
}

static int be_process_rx(struct be_rx_obj *rxo, struct napi_struct *napi,
			 int budget, int polling)
{
	struct be_adapter *adapter = rxo->adapter;
	struct be_queue_info *rx_cq = &rxo->cq;
	struct be_rx_compl_info *rxcp;
	u32 work_done;
	u32 frags_consumed = 0;

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

		/* Don't do gro when we're busy_polling */
		if (do_gro(rxcp) && polling != BUSY_POLLING)
			be_rx_compl_process_gro(rxo, napi, rxcp);
		else
			be_rx_compl_process(rxo, napi, rxcp);

loop_continue:
		frags_consumed += rxcp->num_rcvd;
		be_rx_stats_update(rxo, rxcp);
	}

	if (work_done) {
		be_cq_notify(adapter, rx_cq->id, true, work_done);

		/* When an rx-obj gets into post_starved state, just
		 * let be_worker do the posting.
		 */
		if (atomic_read(&rxo->q.used) < RX_FRAGS_REFILL_WM &&
		    !rxo->rx_post_starved)
			be_post_rx_frags(rxo, GFP_ATOMIC,
					 max_t(u32, MAX_RX_POST,
					       frags_consumed));
	}

	return work_done;
}

static inline void be_update_tx_err(struct be_tx_obj *txo, u8 status)
{
	switch (status) {
	case BE_TX_COMP_HDR_PARSE_ERR:
		tx_stats(txo)->tx_hdr_parse_err++;
		break;
	case BE_TX_COMP_NDMA_ERR:
		tx_stats(txo)->tx_dma_err++;
		break;
	case BE_TX_COMP_ACL_ERR:
		tx_stats(txo)->tx_spoof_check_err++;
		break;
	}
}

static inline void lancer_update_tx_err(struct be_tx_obj *txo, u8 status)
{
	switch (status) {
	case LANCER_TX_COMP_LSO_ERR:
		tx_stats(txo)->tx_tso_err++;
		break;
	case LANCER_TX_COMP_HSW_DROP_MAC_ERR:
	case LANCER_TX_COMP_HSW_DROP_VLAN_ERR:
		tx_stats(txo)->tx_spoof_check_err++;
		break;
	case LANCER_TX_COMP_QINQ_ERR:
		tx_stats(txo)->tx_qinq_err++;
		break;
	case LANCER_TX_COMP_PARITY_ERR:
		tx_stats(txo)->tx_internal_parity_err++;
		break;
	case LANCER_TX_COMP_DMA_ERR:
		tx_stats(txo)->tx_dma_err++;
		break;
	}
}

static void be_process_tx(struct be_adapter *adapter, struct be_tx_obj *txo,
			  int idx)
{
	int num_wrbs = 0, work_done = 0;
	struct be_tx_compl_info *txcp;

	while ((txcp = be_tx_compl_get(txo))) {
		num_wrbs += be_tx_compl_process(adapter, txo, txcp->end_index);
		work_done++;

		if (txcp->status) {
			if (lancer_chip(adapter))
				lancer_update_tx_err(txo, txcp->status);
			else
				be_update_tx_err(txo, txcp->status);
		}
	}

	if (work_done) {
		be_cq_notify(adapter, txo->cq.id, true, work_done);
		atomic_sub(num_wrbs, &txo->q.used);

		/* As Tx wrbs have been freed up, wake up netdev queue
		 * if it was stopped due to lack of tx wrbs.  */
		if (__netif_subqueue_stopped(adapter->netdev, idx) &&
		    be_can_txq_wake(txo)) {
			netif_wake_subqueue(adapter->netdev, idx);
		}

		u64_stats_update_begin(&tx_stats(txo)->sync_compl);
		tx_stats(txo)->tx_compl += work_done;
		u64_stats_update_end(&tx_stats(txo)->sync_compl);
	}
}

#ifdef CONFIG_NET_RX_BUSY_POLL
static inline bool be_lock_napi(struct be_eq_obj *eqo)
{
	bool status = true;

	spin_lock(&eqo->lock); /* BH is already disabled */
	if (eqo->state & BE_EQ_LOCKED) {
		WARN_ON(eqo->state & BE_EQ_NAPI);
		eqo->state |= BE_EQ_NAPI_YIELD;
		status = false;
	} else {
		eqo->state = BE_EQ_NAPI;
	}
	spin_unlock(&eqo->lock);
	return status;
}

static inline void be_unlock_napi(struct be_eq_obj *eqo)
{
	spin_lock(&eqo->lock); /* BH is already disabled */

	WARN_ON(eqo->state & (BE_EQ_POLL | BE_EQ_NAPI_YIELD));
	eqo->state = BE_EQ_IDLE;

	spin_unlock(&eqo->lock);
}

static inline bool be_lock_busy_poll(struct be_eq_obj *eqo)
{
	bool status = true;

	spin_lock_bh(&eqo->lock);
	if (eqo->state & BE_EQ_LOCKED) {
		eqo->state |= BE_EQ_POLL_YIELD;
		status = false;
	} else {
		eqo->state |= BE_EQ_POLL;
	}
	spin_unlock_bh(&eqo->lock);
	return status;
}

static inline void be_unlock_busy_poll(struct be_eq_obj *eqo)
{
	spin_lock_bh(&eqo->lock);

	WARN_ON(eqo->state & (BE_EQ_NAPI));
	eqo->state = BE_EQ_IDLE;

	spin_unlock_bh(&eqo->lock);
}

static inline void be_enable_busy_poll(struct be_eq_obj *eqo)
{
	spin_lock_init(&eqo->lock);
	eqo->state = BE_EQ_IDLE;
}

static inline void be_disable_busy_poll(struct be_eq_obj *eqo)
{
	local_bh_disable();

	/* It's enough to just acquire napi lock on the eqo to stop
	 * be_busy_poll() from processing any queueus.
	 */
	while (!be_lock_napi(eqo))
		mdelay(1);

	local_bh_enable();
}

#else /* CONFIG_NET_RX_BUSY_POLL */

static inline bool be_lock_napi(struct be_eq_obj *eqo)
{
	return true;
}

static inline void be_unlock_napi(struct be_eq_obj *eqo)
{
}

static inline bool be_lock_busy_poll(struct be_eq_obj *eqo)
{
	return false;
}

static inline void be_unlock_busy_poll(struct be_eq_obj *eqo)
{
}

static inline void be_enable_busy_poll(struct be_eq_obj *eqo)
{
}

static inline void be_disable_busy_poll(struct be_eq_obj *eqo)
{
}
#endif /* CONFIG_NET_RX_BUSY_POLL */

int be_poll(struct napi_struct *napi, int budget)
{
	struct be_eq_obj *eqo = container_of(napi, struct be_eq_obj, napi);
	struct be_adapter *adapter = eqo->adapter;
	int max_work = 0, work, i, num_evts;
	struct be_rx_obj *rxo;
	struct be_tx_obj *txo;
	u32 mult_enc = 0;

	num_evts = events_get(eqo);

	for_all_tx_queues_on_eq(adapter, eqo, txo, i)
		be_process_tx(adapter, txo, i);

	if (be_lock_napi(eqo)) {
		/* This loop will iterate twice for EQ0 in which
		 * completions of the last RXQ (default one) are also processed
		 * For other EQs the loop iterates only once
		 */
		for_all_rx_queues_on_eq(adapter, eqo, rxo, i) {
			work = be_process_rx(rxo, napi, budget, NAPI_POLLING);
			max_work = max(work, max_work);
		}
		be_unlock_napi(eqo);
	} else {
		max_work = budget;
	}

	if (is_mcc_eqo(eqo))
		be_process_mcc(adapter);

	if (max_work < budget) {
		napi_complete(napi);

		/* Skyhawk EQ_DB has a provision to set the rearm to interrupt
		 * delay via a delay multiplier encoding value
		 */
		if (skyhawk_chip(adapter))
			mult_enc = be_get_eq_delay_mult_enc(eqo);

		be_eq_notify(adapter, eqo->q.id, true, false, num_evts,
			     mult_enc);
	} else {
		/* As we'll continue in polling mode, count and clear events */
		be_eq_notify(adapter, eqo->q.id, false, false, num_evts, 0);
	}
	return max_work;
}

#ifdef CONFIG_NET_RX_BUSY_POLL
static int be_busy_poll(struct napi_struct *napi)
{
	struct be_eq_obj *eqo = container_of(napi, struct be_eq_obj, napi);
	struct be_adapter *adapter = eqo->adapter;
	struct be_rx_obj *rxo;
	int i, work = 0;

	if (!be_lock_busy_poll(eqo))
		return LL_FLUSH_BUSY;

	for_all_rx_queues_on_eq(adapter, eqo, rxo, i) {
		work = be_process_rx(rxo, napi, 4, BUSY_POLLING);
		if (work)
			break;
	}

	be_unlock_busy_poll(eqo);
	return work;
}
#endif

void be_detect_error(struct be_adapter *adapter)
{
	u32 ue_lo = 0, ue_hi = 0, ue_lo_mask = 0, ue_hi_mask = 0;
	u32 sliport_status = 0, sliport_err1 = 0, sliport_err2 = 0;
	u32 i;
	struct device *dev = &adapter->pdev->dev;

	if (be_check_error(adapter, BE_ERROR_HW))
		return;

	if (lancer_chip(adapter)) {
		sliport_status = ioread32(adapter->db + SLIPORT_STATUS_OFFSET);
		if (sliport_status & SLIPORT_STATUS_ERR_MASK) {
			be_set_error(adapter, BE_ERROR_UE);
			sliport_err1 = ioread32(adapter->db +
						SLIPORT_ERROR1_OFFSET);
			sliport_err2 = ioread32(adapter->db +
						SLIPORT_ERROR2_OFFSET);
			/* Do not log error messages if its a FW reset */
			if (sliport_err1 == SLIPORT_ERROR_FW_RESET1 &&
			    sliport_err2 == SLIPORT_ERROR_FW_RESET2) {
				dev_info(dev, "Firmware update in progress\n");
			} else {
				dev_err(dev, "Error detected in the card\n");
				dev_err(dev, "ERR: sliport status 0x%x\n",
					sliport_status);
				dev_err(dev, "ERR: sliport error1 0x%x\n",
					sliport_err1);
				dev_err(dev, "ERR: sliport error2 0x%x\n",
					sliport_err2);
			}
		}
	} else {
		ue_lo = ioread32(adapter->pcicfg + PCICFG_UE_STATUS_LOW);
		ue_hi = ioread32(adapter->pcicfg + PCICFG_UE_STATUS_HIGH);
		ue_lo_mask = ioread32(adapter->pcicfg +
				      PCICFG_UE_STATUS_LOW_MASK);
		ue_hi_mask = ioread32(adapter->pcicfg +
				      PCICFG_UE_STATUS_HI_MASK);

		ue_lo = (ue_lo & ~ue_lo_mask);
		ue_hi = (ue_hi & ~ue_hi_mask);

		/* On certain platforms BE hardware can indicate spurious UEs.
		 * Allow HW to stop working completely in case of a real UE.
		 * Hence not setting the hw_error for UE detection.
		 */

		if (ue_lo || ue_hi) {
			dev_err(dev,
				"Unrecoverable Error detected in the adapter");
			dev_err(dev, "Please reboot server to recover");
			if (skyhawk_chip(adapter))
				be_set_error(adapter, BE_ERROR_UE);

			for (i = 0; ue_lo; ue_lo >>= 1, i++) {
				if (ue_lo & 1)
					dev_err(dev, "UE: %s bit set\n",
						ue_status_low_desc[i]);
			}
			for (i = 0; ue_hi; ue_hi >>= 1, i++) {
				if (ue_hi & 1)
					dev_err(dev, "UE: %s bit set\n",
						ue_status_hi_desc[i]);
			}
		}
	}
}

static void be_msix_disable(struct be_adapter *adapter)
{
	if (msix_enabled(adapter)) {
		pci_disable_msix(adapter->pdev);
		adapter->num_msix_vec = 0;
		adapter->num_msix_roce_vec = 0;
	}
}

static int be_msix_enable(struct be_adapter *adapter)
{
	int i, num_vec;
	struct device *dev = &adapter->pdev->dev;

	/* If RoCE is supported, program the max number of NIC vectors that
	 * may be configured via set-channels, along with vectors needed for
	 * RoCe. Else, just program the number we'll use initially.
	 */
	if (be_roce_supported(adapter))
		num_vec = min_t(int, 2 * be_max_eqs(adapter),
				2 * num_online_cpus());
	else
		num_vec = adapter->cfg_num_qs;

	for (i = 0; i < num_vec; i++)
		adapter->msix_entries[i].entry = i;

	num_vec = pci_enable_msix_range(adapter->pdev, adapter->msix_entries,
					MIN_MSIX_VECTORS, num_vec);
	if (num_vec < 0)
		goto fail;

	if (be_roce_supported(adapter) && num_vec > MIN_MSIX_VECTORS) {
		adapter->num_msix_roce_vec = num_vec / 2;
		dev_info(dev, "enabled %d MSI-x vector(s) for RoCE\n",
			 adapter->num_msix_roce_vec);
	}

	adapter->num_msix_vec = num_vec - adapter->num_msix_roce_vec;

	dev_info(dev, "enabled %d MSI-x vector(s) for NIC\n",
		 adapter->num_msix_vec);
	return 0;

fail:
	dev_warn(dev, "MSIx enable failed\n");

	/* INTx is not supported in VFs, so fail probe if enable_msix fails */
	if (be_virtfn(adapter))
		return num_vec;
	return 0;
}

static inline int be_msix_vec_get(struct be_adapter *adapter,
				  struct be_eq_obj *eqo)
{
	return adapter->msix_entries[eqo->msix_idx].vector;
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

		irq_set_affinity_hint(vec, eqo->affinity_mask);
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
		if (be_virtfn(adapter))
			return status;
	}

	/* INTx: only the first EQ is used */
	netdev->irq = adapter->pdev->irq;
	status = request_irq(netdev->irq, be_intx, IRQF_SHARED, netdev->name,
			     &adapter->eq_obj[0]);
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
	int i, vec;

	if (!adapter->isr_registered)
		return;

	/* INTx */
	if (!msix_enabled(adapter)) {
		free_irq(netdev->irq, &adapter->eq_obj[0]);
		goto done;
	}

	/* MSIx */
	for_all_evt_queues(adapter, eqo, i) {
		vec = be_msix_vec_get(adapter, eqo);
		irq_set_affinity_hint(vec, NULL);
		free_irq(vec, eqo);
	}

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
			/* If RXQs are destroyed while in an "out of buffer"
			 * state, there is a possibility of an HW stall on
			 * Lancer. So, post 64 buffers to each queue to relieve
			 * the "out of buffer" condition.
			 * Make sure there's space in the RXQ before posting.
			 */
			if (lancer_chip(adapter)) {
				be_rx_cq_clean(rxo);
				if (atomic_read(&q->used) == 0)
					be_post_rx_frags(rxo, GFP_KERNEL,
							 MAX_RX_POST);
			}

			be_cmd_rxq_destroy(adapter, q);
			be_rx_cq_clean(rxo);
			be_rxq_clean(rxo);
		}
		be_queue_free(adapter, q);
	}
}

static void be_disable_if_filters(struct be_adapter *adapter)
{
	be_cmd_pmac_del(adapter, adapter->if_handle,
			adapter->pmac_id[0], 0);

	be_clear_uc_list(adapter);

	/* The IFACE flags are enabled in the open path and cleared
	 * in the close path. When a VF gets detached from the host and
	 * assigned to a VM the following happens:
	 *	- VF's IFACE flags get cleared in the detach path
	 *	- IFACE create is issued by the VF in the attach path
	 * Due to a bug in the BE3/Skyhawk-R FW
	 * (Lancer FW doesn't have the bug), the IFACE capability flags
	 * specified along with the IFACE create cmd issued by a VF are not
	 * honoured by FW.  As a consequence, if a *new* driver
	 * (that enables/disables IFACE flags in open/close)
	 * is loaded in the host and an *old* driver is * used by a VM/VF,
	 * the IFACE gets created *without* the needed flags.
	 * To avoid this, disable RX-filter flags only for Lancer.
	 */
	if (lancer_chip(adapter)) {
		be_cmd_rx_filter(adapter, BE_IF_ALL_FILT_FLAGS, OFF);
		adapter->if_flags &= ~BE_IF_ALL_FILT_FLAGS;
	}
}

static int be_close(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct be_eq_obj *eqo;
	int i;

	/* This protection is needed as be_close() may be called even when the
	 * adapter is in cleared state (after eeh perm failure)
	 */
	if (!(adapter->flags & BE_FLAGS_SETUP_DONE))
		return 0;

	be_disable_if_filters(adapter);

	be_roce_dev_close(adapter);

	if (adapter->flags & BE_FLAGS_NAPI_ENABLED) {
		for_all_evt_queues(adapter, eqo, i) {
			napi_disable(&eqo->napi);
			be_disable_busy_poll(eqo);
		}
		adapter->flags &= ~BE_FLAGS_NAPI_ENABLED;
	}

	be_async_mcc_disable(adapter);

	/* Wait for all pending tx completions to arrive so that
	 * all tx skbs are freed.
	 */
	netif_tx_disable(netdev);
	be_tx_compl_clean(adapter);

	be_rx_qs_destroy(adapter);

	for_all_evt_queues(adapter, eqo, i) {
		if (msix_enabled(adapter))
			synchronize_irq(be_msix_vec_get(adapter, eqo));
		else
			synchronize_irq(netdev->irq);
		be_eq_clean(eqo);
	}

	be_irq_unregister(adapter);

	return 0;
}

static int be_rx_qs_create(struct be_adapter *adapter)
{
	struct rss_info *rss = &adapter->rss_info;
	u8 rss_key[RSS_HASH_KEY_LEN];
	struct be_rx_obj *rxo;
	int rc, i, j;

	for_all_rx_queues(adapter, rxo, i) {
		rc = be_queue_alloc(adapter, &rxo->q, RX_Q_LEN,
				    sizeof(struct be_eth_rx_d));
		if (rc)
			return rc;
	}

	if (adapter->need_def_rxq || !adapter->num_rss_qs) {
		rxo = default_rxo(adapter);
		rc = be_cmd_rxq_create(adapter, &rxo->q, rxo->cq.id,
				       rx_frag_size, adapter->if_handle,
				       false, &rxo->rss_id);
		if (rc)
			return rc;
	}

	for_all_rss_queues(adapter, rxo, i) {
		rc = be_cmd_rxq_create(adapter, &rxo->q, rxo->cq.id,
				       rx_frag_size, adapter->if_handle,
				       true, &rxo->rss_id);
		if (rc)
			return rc;
	}

	if (be_multi_rxq(adapter)) {
		for (j = 0; j < RSS_INDIR_TABLE_LEN; j += adapter->num_rss_qs) {
			for_all_rss_queues(adapter, rxo, i) {
				if ((j + i) >= RSS_INDIR_TABLE_LEN)
					break;
				rss->rsstable[j + i] = rxo->rss_id;
				rss->rss_queue[j + i] = i;
			}
		}
		rss->rss_flags = RSS_ENABLE_TCP_IPV4 | RSS_ENABLE_IPV4 |
			RSS_ENABLE_TCP_IPV6 | RSS_ENABLE_IPV6;

		if (!BEx_chip(adapter))
			rss->rss_flags |= RSS_ENABLE_UDP_IPV4 |
				RSS_ENABLE_UDP_IPV6;
	} else {
		/* Disable RSS, if only default RX Q is created */
		rss->rss_flags = RSS_ENABLE_NONE;
	}

	netdev_rss_key_fill(rss_key, RSS_HASH_KEY_LEN);
	rc = be_cmd_rss_config(adapter, rss->rsstable, rss->rss_flags,
			       128, rss_key);
	if (rc) {
		rss->rss_flags = RSS_ENABLE_NONE;
		return rc;
	}

	memcpy(rss->rss_hkey, rss_key, RSS_HASH_KEY_LEN);

	/* Post 1 less than RXQ-len to avoid head being equal to tail,
	 * which is a queue empty condition
	 */
	for_all_rx_queues(adapter, rxo, i)
		be_post_rx_frags(rxo, GFP_KERNEL, RX_Q_LEN - 1);

	return 0;
}

static int be_enable_if_filters(struct be_adapter *adapter)
{
	int status;

	status = be_cmd_rx_filter(adapter, BE_IF_EN_FLAGS, ON);
	if (status)
		return status;

	/* For BE3 VFs, the PF programs the initial MAC address */
	if (!(BEx_chip(adapter) && be_virtfn(adapter))) {
		status = be_cmd_pmac_add(adapter, adapter->netdev->dev_addr,
					 adapter->if_handle,
					 &adapter->pmac_id[0], 0);
		if (status)
			return status;
	}

	if (adapter->vlans_added)
		be_vid_config(adapter);

	be_set_rx_mode(adapter->netdev);

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

	status = be_enable_if_filters(adapter);
	if (status)
		goto err;

	status = be_irq_register(adapter);
	if (status)
		goto err;

	for_all_rx_queues(adapter, rxo, i)
		be_cq_notify(adapter, rxo->cq.id, true, 0);

	for_all_tx_queues(adapter, txo, i)
		be_cq_notify(adapter, txo->cq.id, true, 0);

	be_async_mcc_enable(adapter);

	for_all_evt_queues(adapter, eqo, i) {
		napi_enable(&eqo->napi);
		be_enable_busy_poll(eqo);
		be_eq_notify(adapter, eqo->q.id, true, true, 0, 0);
	}
	adapter->flags |= BE_FLAGS_NAPI_ENABLED;

	status = be_cmd_link_status_query(adapter, NULL, &link_status, 0);
	if (!status)
		be_link_status_update(adapter, link_status);

	netif_tx_start_all_queues(netdev);
	be_roce_dev_open(adapter);

#ifdef CONFIG_BE2NET_VXLAN
	if (skyhawk_chip(adapter))
		vxlan_get_rx_port(netdev);
#endif

	return 0;
err:
	be_close(adapter->netdev);
	return -EIO;
}

static int be_setup_wol(struct be_adapter *adapter, bool enable)
{
	struct device *dev = &adapter->pdev->dev;
	struct be_dma_mem cmd;
	u8 mac[ETH_ALEN];
	int status;

	eth_zero_addr(mac);

	cmd.size = sizeof(struct be_cmd_req_acpi_wol_magic_config);
	cmd.va = dma_zalloc_coherent(dev, cmd.size, &cmd.dma, GFP_KERNEL);
	if (!cmd.va)
		return -ENOMEM;

	if (enable) {
		status = pci_write_config_dword(adapter->pdev,
						PCICFG_PM_CONTROL_OFFSET,
						PCICFG_PM_CONTROL_MASK);
		if (status) {
			dev_err(dev, "Could not enable Wake-on-lan\n");
			goto err;
		}
	} else {
		ether_addr_copy(mac, adapter->netdev->dev_addr);
	}

	status = be_cmd_enable_magic_wol(adapter, mac, &cmd);
	pci_enable_wake(adapter->pdev, PCI_D3hot, enable);
	pci_enable_wake(adapter->pdev, PCI_D3cold, enable);
err:
	dma_free_coherent(dev, cmd.size, cmd.va, cmd.dma);
	return status;
}

static void be_vf_eth_addr_generate(struct be_adapter *adapter, u8 *mac)
{
	u32 addr;

	addr = jhash(adapter->netdev->dev_addr, ETH_ALEN, 0);

	mac[5] = (u8)(addr & 0xFF);
	mac[4] = (u8)((addr >> 8) & 0xFF);
	mac[3] = (u8)((addr >> 16) & 0xFF);
	/* Use the OUI from the current MAC address */
	memcpy(mac, adapter->netdev->dev_addr, 3);
}

/*
 * Generate a seed MAC address from the PF MAC Address using jhash.
 * MAC Address for VFs are assigned incrementally starting from the seed.
 * These addresses are programmed in the ASIC by the PF and the VF driver
 * queries for the MAC address during its probe.
 */
static int be_vf_eth_addr_config(struct be_adapter *adapter)
{
	u32 vf;
	int status = 0;
	u8 mac[ETH_ALEN];
	struct be_vf_cfg *vf_cfg;

	be_vf_eth_addr_generate(adapter, mac);

	for_all_vfs(adapter, vf_cfg, vf) {
		if (BEx_chip(adapter))
			status = be_cmd_pmac_add(adapter, mac,
						 vf_cfg->if_handle,
						 &vf_cfg->pmac_id, vf + 1);
		else
			status = be_cmd_set_mac(adapter, mac, vf_cfg->if_handle,
						vf + 1);

		if (status)
			dev_err(&adapter->pdev->dev,
				"Mac address assignment failed for VF %d\n",
				vf);
		else
			memcpy(vf_cfg->mac_addr, mac, ETH_ALEN);

		mac[5] += 1;
	}
	return status;
}

static int be_vfs_mac_query(struct be_adapter *adapter)
{
	int status, vf;
	u8 mac[ETH_ALEN];
	struct be_vf_cfg *vf_cfg;

	for_all_vfs(adapter, vf_cfg, vf) {
		status = be_cmd_get_active_mac(adapter, vf_cfg->pmac_id,
					       mac, vf_cfg->if_handle,
					       false, vf+1);
		if (status)
			return status;
		memcpy(vf_cfg->mac_addr, mac, ETH_ALEN);
	}
	return 0;
}

static void be_vf_clear(struct be_adapter *adapter)
{
	struct be_vf_cfg *vf_cfg;
	u32 vf;

	if (pci_vfs_assigned(adapter->pdev)) {
		dev_warn(&adapter->pdev->dev,
			 "VFs are assigned to VMs: not disabling VFs\n");
		goto done;
	}

	pci_disable_sriov(adapter->pdev);

	for_all_vfs(adapter, vf_cfg, vf) {
		if (BEx_chip(adapter))
			be_cmd_pmac_del(adapter, vf_cfg->if_handle,
					vf_cfg->pmac_id, vf + 1);
		else
			be_cmd_set_mac(adapter, NULL, vf_cfg->if_handle,
				       vf + 1);

		be_cmd_if_destroy(adapter, vf_cfg->if_handle, vf + 1);
	}
done:
	kfree(adapter->vf_cfg);
	adapter->num_vfs = 0;
	adapter->flags &= ~BE_FLAGS_SRIOV_ENABLED;
}

static void be_clear_queues(struct be_adapter *adapter)
{
	be_mcc_queues_destroy(adapter);
	be_rx_cqs_destroy(adapter);
	be_tx_queues_destroy(adapter);
	be_evt_queues_destroy(adapter);
}

static void be_cancel_worker(struct be_adapter *adapter)
{
	if (adapter->flags & BE_FLAGS_WORKER_SCHEDULED) {
		cancel_delayed_work_sync(&adapter->work);
		adapter->flags &= ~BE_FLAGS_WORKER_SCHEDULED;
	}
}

static void be_cancel_err_detection(struct be_adapter *adapter)
{
	if (adapter->flags & BE_FLAGS_ERR_DETECTION_SCHEDULED) {
		cancel_delayed_work_sync(&adapter->be_err_detection_work);
		adapter->flags &= ~BE_FLAGS_ERR_DETECTION_SCHEDULED;
	}
}

#ifdef CONFIG_BE2NET_VXLAN
static void be_disable_vxlan_offloads(struct be_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (adapter->flags & BE_FLAGS_VXLAN_OFFLOADS)
		be_cmd_manage_iface(adapter, adapter->if_handle,
				    OP_CONVERT_TUNNEL_TO_NORMAL);

	if (adapter->vxlan_port)
		be_cmd_set_vxlan_port(adapter, 0);

	adapter->flags &= ~BE_FLAGS_VXLAN_OFFLOADS;
	adapter->vxlan_port = 0;

	netdev->hw_enc_features = 0;
	netdev->hw_features &= ~(NETIF_F_GSO_UDP_TUNNEL);
	netdev->features &= ~(NETIF_F_GSO_UDP_TUNNEL);
}
#endif

static u16 be_calculate_vf_qs(struct be_adapter *adapter, u16 num_vfs)
{
	struct be_resources res = adapter->pool_res;
	u16 num_vf_qs = 1;

	/* Distribute the queue resources equally among the PF and it's VFs
	 * Do not distribute queue resources in multi-channel configuration.
	 */
	if (num_vfs && !be_is_mc(adapter)) {
		/* If number of VFs requested is 8 less than max supported,
		 * assign 8 queue pairs to the PF and divide the remaining
		 * resources evenly among the VFs
		 */
		if (num_vfs < (be_max_vfs(adapter) - 8))
			num_vf_qs = (res.max_rss_qs - 8) / num_vfs;
		else
			num_vf_qs = res.max_rss_qs / num_vfs;

		/* Skyhawk-R chip supports only MAX_RSS_IFACES RSS capable
		 * interfaces per port. Provide RSS on VFs, only if number
		 * of VFs requested is less than MAX_RSS_IFACES limit.
		 */
		if (num_vfs >= MAX_RSS_IFACES)
			num_vf_qs = 1;
	}
	return num_vf_qs;
}

static int be_clear(struct be_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	u16 num_vf_qs;

	be_cancel_worker(adapter);

	if (sriov_enabled(adapter))
		be_vf_clear(adapter);

	/* Re-configure FW to distribute resources evenly across max-supported
	 * number of VFs, only when VFs are not already enabled.
	 */
	if (skyhawk_chip(adapter) && be_physfn(adapter) &&
	    !pci_vfs_assigned(pdev)) {
		num_vf_qs = be_calculate_vf_qs(adapter,
					       pci_sriov_get_totalvfs(pdev));
		be_cmd_set_sriov_config(adapter, adapter->pool_res,
					pci_sriov_get_totalvfs(pdev),
					num_vf_qs);
	}

#ifdef CONFIG_BE2NET_VXLAN
	be_disable_vxlan_offloads(adapter);
#endif
	kfree(adapter->pmac_id);
	adapter->pmac_id = NULL;

	be_cmd_if_destroy(adapter, adapter->if_handle,  0);

	be_clear_queues(adapter);

	be_msix_disable(adapter);
	adapter->flags &= ~BE_FLAGS_SETUP_DONE;
	return 0;
}

static int be_vfs_if_create(struct be_adapter *adapter)
{
	struct be_resources res = {0};
	u32 cap_flags, en_flags, vf;
	struct be_vf_cfg *vf_cfg;
	int status;

	/* If a FW profile exists, then cap_flags are updated */
	cap_flags = BE_IF_FLAGS_UNTAGGED | BE_IF_FLAGS_BROADCAST |
		    BE_IF_FLAGS_MULTICAST | BE_IF_FLAGS_PASS_L3L4_ERRORS;

	for_all_vfs(adapter, vf_cfg, vf) {
		if (!BE3_chip(adapter)) {
			status = be_cmd_get_profile_config(adapter, &res,
							   RESOURCE_LIMITS,
							   vf + 1);
			if (!status) {
				cap_flags = res.if_cap_flags;
				/* Prevent VFs from enabling VLAN promiscuous
				 * mode
				 */
				cap_flags &= ~BE_IF_FLAGS_VLAN_PROMISCUOUS;
			}
		}

		en_flags = cap_flags & (BE_IF_FLAGS_UNTAGGED |
					BE_IF_FLAGS_BROADCAST |
					BE_IF_FLAGS_MULTICAST |
					BE_IF_FLAGS_PASS_L3L4_ERRORS);
		status = be_cmd_if_create(adapter, cap_flags, en_flags,
					  &vf_cfg->if_handle, vf + 1);
		if (status)
			return status;
	}

	return 0;
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
	struct device *dev = &adapter->pdev->dev;
	struct be_vf_cfg *vf_cfg;
	int status, old_vfs, vf;
	bool spoofchk;

	old_vfs = pci_num_vf(adapter->pdev);

	status = be_vf_setup_init(adapter);
	if (status)
		goto err;

	if (old_vfs) {
		for_all_vfs(adapter, vf_cfg, vf) {
			status = be_cmd_get_if_id(adapter, vf_cfg, vf);
			if (status)
				goto err;
		}

		status = be_vfs_mac_query(adapter);
		if (status)
			goto err;
	} else {
		status = be_vfs_if_create(adapter);
		if (status)
			goto err;

		status = be_vf_eth_addr_config(adapter);
		if (status)
			goto err;
	}

	for_all_vfs(adapter, vf_cfg, vf) {
		/* Allow VFs to programs MAC/VLAN filters */
		status = be_cmd_get_fn_privileges(adapter, &vf_cfg->privileges,
						  vf + 1);
		if (!status && !(vf_cfg->privileges & BE_PRIV_FILTMGMT)) {
			status = be_cmd_set_fn_privileges(adapter,
							  vf_cfg->privileges |
							  BE_PRIV_FILTMGMT,
							  vf + 1);
			if (!status) {
				vf_cfg->privileges |= BE_PRIV_FILTMGMT;
				dev_info(dev, "VF%d has FILTMGMT privilege\n",
					 vf);
			}
		}

		/* Allow full available bandwidth */
		if (!old_vfs)
			be_cmd_config_qos(adapter, 0, 0, vf + 1);

		status = be_cmd_get_hsw_config(adapter, NULL, vf + 1,
					       vf_cfg->if_handle, NULL,
					       &spoofchk);
		if (!status)
			vf_cfg->spoofchk = spoofchk;

		if (!old_vfs) {
			be_cmd_enable_vf(adapter, vf + 1);
			be_cmd_set_logical_link_config(adapter,
						       IFLA_VF_LINK_STATE_AUTO,
						       vf+1);
		}
	}

	if (!old_vfs) {
		status = pci_enable_sriov(adapter->pdev, adapter->num_vfs);
		if (status) {
			dev_err(dev, "SRIOV enable failed\n");
			adapter->num_vfs = 0;
			goto err;
		}
	}

	adapter->flags |= BE_FLAGS_SRIOV_ENABLED;
	return 0;
err:
	dev_err(dev, "VF setup failed\n");
	be_vf_clear(adapter);
	return status;
}

/* Converting function_mode bits on BE3 to SH mc_type enums */

static u8 be_convert_mc_type(u32 function_mode)
{
	if (function_mode & VNIC_MODE && function_mode & QNQ_MODE)
		return vNIC1;
	else if (function_mode & QNQ_MODE)
		return FLEX10;
	else if (function_mode & VNIC_MODE)
		return vNIC2;
	else if (function_mode & UMC_ENABLED)
		return UMC;
	else
		return MC_NONE;
}

/* On BE2/BE3 FW does not suggest the supported limits */
static void BEx_get_resources(struct be_adapter *adapter,
			      struct be_resources *res)
{
	bool use_sriov = adapter->num_vfs ? 1 : 0;

	if (be_physfn(adapter))
		res->max_uc_mac = BE_UC_PMAC_COUNT;
	else
		res->max_uc_mac = BE_VF_UC_PMAC_COUNT;

	adapter->mc_type = be_convert_mc_type(adapter->function_mode);

	if (be_is_mc(adapter)) {
		/* Assuming that there are 4 channels per port,
		 * when multi-channel is enabled
		 */
		if (be_is_qnq_mode(adapter))
			res->max_vlans = BE_NUM_VLANS_SUPPORTED/8;
		else
			/* In a non-qnq multichannel mode, the pvid
			 * takes up one vlan entry
			 */
			res->max_vlans = (BE_NUM_VLANS_SUPPORTED / 4) - 1;
	} else {
		res->max_vlans = BE_NUM_VLANS_SUPPORTED;
	}

	res->max_mcast_mac = BE_MAX_MC;

	/* 1) For BE3 1Gb ports, FW does not support multiple TXQs
	 * 2) Create multiple TX rings on a BE3-R multi-channel interface
	 *    *only* if it is RSS-capable.
	 */
	if (BE2_chip(adapter) || use_sriov ||  (adapter->port_num > 1) ||
	    be_virtfn(adapter) ||
	    (be_is_mc(adapter) &&
	     !(adapter->function_caps & BE_FUNCTION_CAPS_RSS))) {
		res->max_tx_qs = 1;
	} else if (adapter->function_caps & BE_FUNCTION_CAPS_SUPER_NIC) {
		struct be_resources super_nic_res = {0};

		/* On a SuperNIC profile, the driver needs to use the
		 * GET_PROFILE_CONFIG cmd to query the per-function TXQ limits
		 */
		be_cmd_get_profile_config(adapter, &super_nic_res,
					  RESOURCE_LIMITS, 0);
		/* Some old versions of BE3 FW don't report max_tx_qs value */
		res->max_tx_qs = super_nic_res.max_tx_qs ? : BE3_MAX_TX_QS;
	} else {
		res->max_tx_qs = BE3_MAX_TX_QS;
	}

	if ((adapter->function_caps & BE_FUNCTION_CAPS_RSS) &&
	    !use_sriov && be_physfn(adapter))
		res->max_rss_qs = (adapter->be3_native) ?
					   BE3_MAX_RSS_QS : BE2_MAX_RSS_QS;
	res->max_rx_qs = res->max_rss_qs + 1;

	if (be_physfn(adapter))
		res->max_evt_qs = (be_max_vfs(adapter) > 0) ?
					BE3_SRIOV_MAX_EVT_QS : BE3_MAX_EVT_QS;
	else
		res->max_evt_qs = 1;

	res->if_cap_flags = BE_IF_CAP_FLAGS_WANT;
	res->if_cap_flags &= ~BE_IF_FLAGS_DEFQ_RSS;
	if (!(adapter->function_caps & BE_FUNCTION_CAPS_RSS))
		res->if_cap_flags &= ~BE_IF_FLAGS_RSS;
}

static void be_setup_init(struct be_adapter *adapter)
{
	adapter->vlan_prio_bmap = 0xff;
	adapter->phy.link_speed = -1;
	adapter->if_handle = -1;
	adapter->be3_native = false;
	adapter->if_flags = 0;
	if (be_physfn(adapter))
		adapter->cmd_privileges = MAX_PRIVILEGES;
	else
		adapter->cmd_privileges = MIN_PRIVILEGES;
}

static int be_get_sriov_config(struct be_adapter *adapter)
{
	struct be_resources res = {0};
	int max_vfs, old_vfs;

	be_cmd_get_profile_config(adapter, &res, RESOURCE_LIMITS, 0);

	/* Some old versions of BE3 FW don't report max_vfs value */
	if (BE3_chip(adapter) && !res.max_vfs) {
		max_vfs = pci_sriov_get_totalvfs(adapter->pdev);
		res.max_vfs = max_vfs > 0 ? min(MAX_VFS, max_vfs) : 0;
	}

	adapter->pool_res = res;

	/* If during previous unload of the driver, the VFs were not disabled,
	 * then we cannot rely on the PF POOL limits for the TotalVFs value.
	 * Instead use the TotalVFs value stored in the pci-dev struct.
	 */
	old_vfs = pci_num_vf(adapter->pdev);
	if (old_vfs) {
		dev_info(&adapter->pdev->dev, "%d VFs are already enabled\n",
			 old_vfs);

		adapter->pool_res.max_vfs =
			pci_sriov_get_totalvfs(adapter->pdev);
		adapter->num_vfs = old_vfs;
	}

	return 0;
}

static void be_alloc_sriov_res(struct be_adapter *adapter)
{
	int old_vfs = pci_num_vf(adapter->pdev);
	u16 num_vf_qs;
	int status;

	be_get_sriov_config(adapter);

	if (!old_vfs)
		pci_sriov_set_totalvfs(adapter->pdev, be_max_vfs(adapter));

	/* When the HW is in SRIOV capable configuration, the PF-pool
	 * resources are given to PF during driver load, if there are no
	 * old VFs. This facility is not available in BE3 FW.
	 * Also, this is done by FW in Lancer chip.
	 */
	if (skyhawk_chip(adapter) && be_max_vfs(adapter) && !old_vfs) {
		num_vf_qs = be_calculate_vf_qs(adapter, 0);
		status = be_cmd_set_sriov_config(adapter, adapter->pool_res, 0,
						 num_vf_qs);
		if (status)
			dev_err(&adapter->pdev->dev,
				"Failed to optimize SRIOV resources\n");
	}
}

static int be_get_resources(struct be_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	struct be_resources res = {0};
	int status;

	if (BEx_chip(adapter)) {
		BEx_get_resources(adapter, &res);
		adapter->res = res;
	}

	/* For Lancer, SH etc read per-function resource limits from FW.
	 * GET_FUNC_CONFIG returns per function guaranteed limits.
	 * GET_PROFILE_CONFIG returns PCI-E related limits PF-pool limits
	 */
	if (!BEx_chip(adapter)) {
		status = be_cmd_get_func_config(adapter, &res);
		if (status)
			return status;

		/* If a deafault RXQ must be created, we'll use up one RSSQ*/
		if (res.max_rss_qs && res.max_rss_qs == res.max_rx_qs &&
		    !(res.if_cap_flags & BE_IF_FLAGS_DEFQ_RSS))
			res.max_rss_qs -= 1;

		/* If RoCE may be enabled stash away half the EQs for RoCE */
		if (be_roce_supported(adapter))
			res.max_evt_qs /= 2;
		adapter->res = res;
	}

	/* If FW supports RSS default queue, then skip creating non-RSS
	 * queue for non-IP traffic.
	 */
	adapter->need_def_rxq = (be_if_cap_flags(adapter) &
				 BE_IF_FLAGS_DEFQ_RSS) ? 0 : 1;

	dev_info(dev, "Max: txqs %d, rxqs %d, rss %d, eqs %d, vfs %d\n",
		 be_max_txqs(adapter), be_max_rxqs(adapter),
		 be_max_rss(adapter), be_max_eqs(adapter),
		 be_max_vfs(adapter));
	dev_info(dev, "Max: uc-macs %d, mc-macs %d, vlans %d\n",
		 be_max_uc(adapter), be_max_mc(adapter),
		 be_max_vlans(adapter));

	/* Sanitize cfg_num_qs based on HW and platform limits */
	adapter->cfg_num_qs = min_t(u16, netif_get_num_default_rss_queues(),
				    be_max_qs(adapter));
	return 0;
}

static int be_get_config(struct be_adapter *adapter)
{
	int status, level;
	u16 profile_id;

	status = be_cmd_query_fw_cfg(adapter);
	if (status)
		return status;

	if (BEx_chip(adapter)) {
		level = be_cmd_get_fw_log_level(adapter);
		adapter->msg_enable =
			level <= FW_LOG_LEVEL_DEFAULT ? NETIF_MSG_HW : 0;
	}

	be_cmd_get_acpi_wol_cap(adapter);

	be_cmd_query_port_name(adapter);

	if (be_physfn(adapter)) {
		status = be_cmd_get_active_profile(adapter, &profile_id);
		if (!status)
			dev_info(&adapter->pdev->dev,
				 "Using profile 0x%x\n", profile_id);
	}

	status = be_get_resources(adapter);
	if (status)
		return status;

	adapter->pmac_id = kcalloc(be_max_uc(adapter),
				   sizeof(*adapter->pmac_id), GFP_KERNEL);
	if (!adapter->pmac_id)
		return -ENOMEM;

	return 0;
}

static int be_mac_setup(struct be_adapter *adapter)
{
	u8 mac[ETH_ALEN];
	int status;

	if (is_zero_ether_addr(adapter->netdev->dev_addr)) {
		status = be_cmd_get_perm_mac(adapter, mac);
		if (status)
			return status;

		memcpy(adapter->netdev->dev_addr, mac, ETH_ALEN);
		memcpy(adapter->netdev->perm_addr, mac, ETH_ALEN);
	}

	return 0;
}

static void be_schedule_worker(struct be_adapter *adapter)
{
	schedule_delayed_work(&adapter->work, msecs_to_jiffies(1000));
	adapter->flags |= BE_FLAGS_WORKER_SCHEDULED;
}

static void be_schedule_err_detection(struct be_adapter *adapter)
{
	schedule_delayed_work(&adapter->be_err_detection_work,
			      msecs_to_jiffies(1000));
	adapter->flags |= BE_FLAGS_ERR_DETECTION_SCHEDULED;
}

static int be_setup_queues(struct be_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int status;

	status = be_evt_queues_create(adapter);
	if (status)
		goto err;

	status = be_tx_qs_create(adapter);
	if (status)
		goto err;

	status = be_rx_cqs_create(adapter);
	if (status)
		goto err;

	status = be_mcc_queues_create(adapter);
	if (status)
		goto err;

	status = netif_set_real_num_rx_queues(netdev, adapter->num_rx_qs);
	if (status)
		goto err;

	status = netif_set_real_num_tx_queues(netdev, adapter->num_tx_qs);
	if (status)
		goto err;

	return 0;
err:
	dev_err(&adapter->pdev->dev, "queue_setup failed\n");
	return status;
}

int be_update_queues(struct be_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int status;

	if (netif_running(netdev))
		be_close(netdev);

	be_cancel_worker(adapter);

	/* If any vectors have been shared with RoCE we cannot re-program
	 * the MSIx table.
	 */
	if (!adapter->num_msix_roce_vec)
		be_msix_disable(adapter);

	be_clear_queues(adapter);

	if (!msix_enabled(adapter)) {
		status = be_msix_enable(adapter);
		if (status)
			return status;
	}

	status = be_setup_queues(adapter);
	if (status)
		return status;

	be_schedule_worker(adapter);

	if (netif_running(netdev))
		status = be_open(netdev);

	return status;
}

static inline int fw_major_num(const char *fw_ver)
{
	int fw_major = 0, i;

	i = sscanf(fw_ver, "%d.", &fw_major);
	if (i != 1)
		return 0;

	return fw_major;
}

/* If any VFs are already enabled don't FLR the PF */
static bool be_reset_required(struct be_adapter *adapter)
{
	return pci_num_vf(adapter->pdev) ? false : true;
}

/* Wait for the FW to be ready and perform the required initialization */
static int be_func_init(struct be_adapter *adapter)
{
	int status;

	status = be_fw_wait_ready(adapter);
	if (status)
		return status;

	if (be_reset_required(adapter)) {
		status = be_cmd_reset_function(adapter);
		if (status)
			return status;

		/* Wait for interrupts to quiesce after an FLR */
		msleep(100);

		/* We can clear all errors when function reset succeeds */
		be_clear_error(adapter, BE_CLEAR_ALL);
	}

	/* Tell FW we're ready to fire cmds */
	status = be_cmd_fw_init(adapter);
	if (status)
		return status;

	/* Allow interrupts for other ULPs running on NIC function */
	be_intr_set(adapter, true);

	return 0;
}

static int be_setup(struct be_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	u32 en_flags;
	int status;

	status = be_func_init(adapter);
	if (status)
		return status;

	be_setup_init(adapter);

	if (!lancer_chip(adapter))
		be_cmd_req_native_mode(adapter);

	/* Need to invoke this cmd first to get the PCI Function Number */
	status = be_cmd_get_cntl_attributes(adapter);
	if (status)
		return status;

	if (!BE2_chip(adapter) && be_physfn(adapter))
		be_alloc_sriov_res(adapter);

	status = be_get_config(adapter);
	if (status)
		goto err;

	status = be_msix_enable(adapter);
	if (status)
		goto err;

	/* will enable all the needed filter flags in be_open() */
	en_flags = BE_IF_FLAGS_RSS | BE_IF_FLAGS_DEFQ_RSS;
	en_flags = en_flags & be_if_cap_flags(adapter);
	status = be_cmd_if_create(adapter, be_if_cap_flags(adapter), en_flags,
				  &adapter->if_handle, 0);
	if (status)
		goto err;

	/* Updating real_num_tx/rx_queues() requires rtnl_lock() */
	rtnl_lock();
	status = be_setup_queues(adapter);
	rtnl_unlock();
	if (status)
		goto err;

	be_cmd_get_fn_privileges(adapter, &adapter->cmd_privileges, 0);

	status = be_mac_setup(adapter);
	if (status)
		goto err;

	be_cmd_get_fw_ver(adapter);
	dev_info(dev, "FW version is %s\n", adapter->fw_ver);

	if (BE2_chip(adapter) && fw_major_num(adapter->fw_ver) < 4) {
		dev_err(dev, "Firmware on card is old(%s), IRQs may not work",
			adapter->fw_ver);
		dev_err(dev, "Please upgrade firmware to version >= 4.0\n");
	}

	status = be_cmd_set_flow_control(adapter, adapter->tx_fc,
					 adapter->rx_fc);
	if (status)
		be_cmd_get_flow_control(adapter, &adapter->tx_fc,
					&adapter->rx_fc);

	dev_info(&adapter->pdev->dev, "HW Flow control - TX:%d RX:%d\n",
		 adapter->tx_fc, adapter->rx_fc);

	if (be_physfn(adapter))
		be_cmd_set_logical_link_config(adapter,
					       IFLA_VF_LINK_STATE_AUTO, 0);

	if (adapter->num_vfs)
		be_vf_setup(adapter);

	status = be_cmd_get_phy_info(adapter);
	if (!status && be_pause_supported(adapter))
		adapter->phy.fc_autoneg = 1;

	be_schedule_worker(adapter);
	adapter->flags |= BE_FLAGS_SETUP_DONE;
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

	for_all_evt_queues(adapter, eqo, i) {
		be_eq_notify(eqo->adapter, eqo->q.id, false, true, 0, 0);
		napi_schedule(&eqo->napi);
	}
}
#endif

static char flash_cookie[2][16] = {"*** SE FLAS", "H DIRECTORY *** "};

static bool phy_flashing_required(struct be_adapter *adapter)
{
	return (adapter->phy.phy_type == PHY_TYPE_TN_8022 &&
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

static struct flash_section_info *get_fsec_info(struct be_adapter *adapter,
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

static int be_check_flash_crc(struct be_adapter *adapter, const u8 *p,
			      u32 img_offset, u32 img_size, int hdr_size,
			      u16 img_optype, bool *crc_match)
{
	u32 crc_offset;
	int status;
	u8 crc[4];

	status = be_cmd_get_flash_crc(adapter, crc, img_optype, img_offset,
				      img_size - 4);
	if (status)
		return status;

	crc_offset = hdr_size + img_offset + img_size - 4;

	/* Skip flashing, if crc of flashed region matches */
	if (!memcmp(crc, p + crc_offset, 4))
		*crc_match = true;
	else
		*crc_match = false;

	return status;
}

static int be_flash(struct be_adapter *adapter, const u8 *img,
		    struct be_dma_mem *flash_cmd, int optype, int img_size,
		    u32 img_offset)
{
	u32 flash_op, num_bytes, total_bytes = img_size, bytes_sent = 0;
	struct be_cmd_write_flashrom *req = flash_cmd->va;
	int status;

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
					       flash_op, img_offset +
					       bytes_sent, num_bytes);
		if (base_status(status) == MCC_STATUS_ILLEGAL_REQUEST &&
		    optype == OPTYPE_PHY_FW)
			break;
		else if (status)
			return status;

		bytes_sent += num_bytes;
	}
	return 0;
}

/* For BE2, BE3 and BE3-R */
static int be_flash_BEx(struct be_adapter *adapter,
			const struct firmware *fw,
			struct be_dma_mem *flash_cmd, int num_of_images)
{
	int img_hdrs_size = (num_of_images * sizeof(struct image_hdr));
	struct device *dev = &adapter->pdev->dev;
	struct flash_section_info *fsec = NULL;
	int status, i, filehdr_size, num_comp;
	const struct flash_comp *pflashcomp;
	bool crc_match;
	const u8 *p;

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
		img_hdrs_size = 0;
	}

	/* Get flash section info*/
	fsec = get_fsec_info(adapter, filehdr_size + img_hdrs_size, fw);
	if (!fsec) {
		dev_err(dev, "Invalid Cookie. FW image may be corrupted\n");
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
			status = be_check_flash_crc(adapter, fw->data,
						    pflashcomp[i].offset,
						    pflashcomp[i].size,
						    filehdr_size +
						    img_hdrs_size,
						    OPTYPE_REDBOOT, &crc_match);
			if (status) {
				dev_err(dev,
					"Could not get CRC for 0x%x region\n",
					pflashcomp[i].optype);
				continue;
			}

			if (crc_match)
				continue;
		}

		p = fw->data + filehdr_size + pflashcomp[i].offset +
			img_hdrs_size;
		if (p + pflashcomp[i].size > fw->data + fw->size)
			return -1;

		status = be_flash(adapter, p, flash_cmd, pflashcomp[i].optype,
				  pflashcomp[i].size, 0);
		if (status) {
			dev_err(dev, "Flashing section type 0x%x failed\n",
				pflashcomp[i].img_type);
			return status;
		}
	}
	return 0;
}

static u16 be_get_img_optype(struct flash_section_entry fsec_entry)
{
	u32 img_type = le32_to_cpu(fsec_entry.type);
	u16 img_optype = le16_to_cpu(fsec_entry.optype);

	if (img_optype != 0xFFFF)
		return img_optype;

	switch (img_type) {
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
	case IMAGE_FLASHISM_JUMPVECTOR:
		img_optype = OPTYPE_FLASHISM_JUMPVECTOR;
		break;
	case IMAGE_FIRMWARE_PHY:
		img_optype = OPTYPE_SH_PHY_FW;
		break;
	case IMAGE_REDBOOT_DIR:
		img_optype = OPTYPE_REDBOOT_DIR;
		break;
	case IMAGE_REDBOOT_CONFIG:
		img_optype = OPTYPE_REDBOOT_CONFIG;
		break;
	case IMAGE_UFI_DIR:
		img_optype = OPTYPE_UFI_DIR;
		break;
	default:
		break;
	}

	return img_optype;
}

static int be_flash_skyhawk(struct be_adapter *adapter,
			    const struct firmware *fw,
			    struct be_dma_mem *flash_cmd, int num_of_images)
{
	int img_hdrs_size = num_of_images * sizeof(struct image_hdr);
	bool crc_match, old_fw_img, flash_offset_support = true;
	struct device *dev = &adapter->pdev->dev;
	struct flash_section_info *fsec = NULL;
	u32 img_offset, img_size, img_type;
	u16 img_optype, flash_optype;
	int status, i, filehdr_size;
	const u8 *p;

	filehdr_size = sizeof(struct flash_file_hdr_g3);
	fsec = get_fsec_info(adapter, filehdr_size + img_hdrs_size, fw);
	if (!fsec) {
		dev_err(dev, "Invalid Cookie. FW image may be corrupted\n");
		return -EINVAL;
	}

retry_flash:
	for (i = 0; i < le32_to_cpu(fsec->fsec_hdr.num_images); i++) {
		img_offset = le32_to_cpu(fsec->fsec_entry[i].offset);
		img_size   = le32_to_cpu(fsec->fsec_entry[i].pad_size);
		img_type   = le32_to_cpu(fsec->fsec_entry[i].type);
		img_optype = be_get_img_optype(fsec->fsec_entry[i]);
		old_fw_img = fsec->fsec_entry[i].optype == 0xFFFF;

		if (img_optype == 0xFFFF)
			continue;

		if (flash_offset_support)
			flash_optype = OPTYPE_OFFSET_SPECIFIED;
		else
			flash_optype = img_optype;

		/* Don't bother verifying CRC if an old FW image is being
		 * flashed
		 */
		if (old_fw_img)
			goto flash;

		status = be_check_flash_crc(adapter, fw->data, img_offset,
					    img_size, filehdr_size +
					    img_hdrs_size, flash_optype,
					    &crc_match);
		if (base_status(status) == MCC_STATUS_ILLEGAL_REQUEST ||
		    base_status(status) == MCC_STATUS_ILLEGAL_FIELD) {
			/* The current FW image on the card does not support
			 * OFFSET based flashing. Retry using older mechanism
			 * of OPTYPE based flashing
			 */
			if (flash_optype == OPTYPE_OFFSET_SPECIFIED) {
				flash_offset_support = false;
				goto retry_flash;
			}

			/* The current FW image on the card does not recognize
			 * the new FLASH op_type. The FW download is partially
			 * complete. Reboot the server now to enable FW image
			 * to recognize the new FLASH op_type. To complete the
			 * remaining process, download the same FW again after
			 * the reboot.
			 */
			dev_err(dev, "Flash incomplete. Reset the server\n");
			dev_err(dev, "Download FW image again after reset\n");
			return -EAGAIN;
		} else if (status) {
			dev_err(dev, "Could not get CRC for 0x%x region\n",
				img_optype);
			return -EFAULT;
		}

		if (crc_match)
			continue;

flash:
		p = fw->data + filehdr_size + img_offset + img_hdrs_size;
		if (p + img_size > fw->data + fw->size)
			return -1;

		status = be_flash(adapter, p, flash_cmd, flash_optype, img_size,
				  img_offset);

		/* The current FW image on the card does not support OFFSET
		 * based flashing. Retry using older mechanism of OPTYPE based
		 * flashing
		 */
		if (base_status(status) == MCC_STATUS_ILLEGAL_FIELD &&
		    flash_optype == OPTYPE_OFFSET_SPECIFIED) {
			flash_offset_support = false;
			goto retry_flash;
		}

		/* For old FW images ignore ILLEGAL_FIELD error or errors on
		 * UFI_DIR region
		 */
		if (old_fw_img &&
		    (base_status(status) == MCC_STATUS_ILLEGAL_FIELD ||
		     (img_optype == OPTYPE_UFI_DIR &&
		      base_status(status) == MCC_STATUS_FAILED))) {
			continue;
		} else if (status) {
			dev_err(dev, "Flashing section type 0x%x failed\n",
				img_type);
			return -EFAULT;
		}
	}
	return 0;
}

static int lancer_fw_download(struct be_adapter *adapter,
			      const struct firmware *fw)
{
#define LANCER_FW_DOWNLOAD_CHUNK      (32 * 1024)
#define LANCER_FW_DOWNLOAD_LOCATION   "/prg"
	struct device *dev = &adapter->pdev->dev;
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
		dev_err(dev, "FW image size should be multiple of 4\n");
		return -EINVAL;
	}

	flash_cmd.size = sizeof(struct lancer_cmd_req_write_object)
				+ LANCER_FW_DOWNLOAD_CHUNK;
	flash_cmd.va = dma_zalloc_coherent(dev, flash_cmd.size,
					   &flash_cmd.dma, GFP_KERNEL);
	if (!flash_cmd.va)
		return -ENOMEM;

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

	dma_free_coherent(dev, flash_cmd.size, flash_cmd.va, flash_cmd.dma);
	if (status) {
		dev_err(dev, "Firmware load error\n");
		return be_cmd_status(status);
	}

	dev_info(dev, "Firmware flashed successfully\n");

	if (change_status == LANCER_FW_RESET_NEEDED) {
		dev_info(dev, "Resetting adapter to activate new FW\n");
		status = lancer_physdev_ctrl(adapter,
					     PHYSDEV_CONTROL_FW_RESET_MASK);
		if (status) {
			dev_err(dev, "Adapter busy, could not reset FW\n");
			dev_err(dev, "Reboot server to activate new FW\n");
		}
	} else if (change_status != LANCER_NO_RESET_NEEDED) {
		dev_info(dev, "Reboot server to activate new FW\n");
	}

	return 0;
}

/* Check if the flash image file is compatible with the adapter that
 * is being flashed.
 */
static bool be_check_ufi_compatibility(struct be_adapter *adapter,
				       struct flash_file_hdr_g3 *fhdr)
{
	if (!fhdr) {
		dev_err(&adapter->pdev->dev, "Invalid FW UFI file");
		return false;
	}

	/* First letter of the build version is used to identify
	 * which chip this image file is meant for.
	 */
	switch (fhdr->build[0]) {
	case BLD_STR_UFI_TYPE_SH:
		if (!skyhawk_chip(adapter))
			return false;
		break;
	case BLD_STR_UFI_TYPE_BE3:
		if (!BE3_chip(adapter))
			return false;
		break;
	case BLD_STR_UFI_TYPE_BE2:
		if (!BE2_chip(adapter))
			return false;
		break;
	default:
		return false;
	}

	/* In BE3 FW images the "asic_type_rev" field doesn't track the
	 * asic_rev of the chips it is compatible with.
	 * When asic_type_rev is 0 the image is compatible only with
	 * pre-BE3-R chips (asic_rev < 0x10)
	 */
	if (BEx_chip(adapter) && fhdr->asic_type_rev == 0)
		return adapter->asic_rev < 0x10;
	else
		return (fhdr->asic_type_rev >= adapter->asic_rev);
}

static int be_fw_download(struct be_adapter *adapter, const struct firmware* fw)
{
	struct device *dev = &adapter->pdev->dev;
	struct flash_file_hdr_g3 *fhdr3;
	struct image_hdr *img_hdr_ptr;
	int status = 0, i, num_imgs;
	struct be_dma_mem flash_cmd;

	fhdr3 = (struct flash_file_hdr_g3 *)fw->data;
	if (!be_check_ufi_compatibility(adapter, fhdr3)) {
		dev_err(dev, "Flash image is not compatible with adapter\n");
		return -EINVAL;
	}

	flash_cmd.size = sizeof(struct be_cmd_write_flashrom);
	flash_cmd.va = dma_zalloc_coherent(dev, flash_cmd.size, &flash_cmd.dma,
					   GFP_KERNEL);
	if (!flash_cmd.va)
		return -ENOMEM;

	num_imgs = le32_to_cpu(fhdr3->num_imgs);
	for (i = 0; i < num_imgs; i++) {
		img_hdr_ptr = (struct image_hdr *)(fw->data +
				(sizeof(struct flash_file_hdr_g3) +
				 i * sizeof(struct image_hdr)));
		if (!BE2_chip(adapter) &&
		    le32_to_cpu(img_hdr_ptr->imageid) != 1)
			continue;

		if (skyhawk_chip(adapter))
			status = be_flash_skyhawk(adapter, fw, &flash_cmd,
						  num_imgs);
		else
			status = be_flash_BEx(adapter, fw, &flash_cmd,
					      num_imgs);
	}

	dma_free_coherent(dev, flash_cmd.size, flash_cmd.va, flash_cmd.dma);
	if (!status)
		dev_info(dev, "Firmware flashed successfully\n");

	return status;
}

int be_load_fw(struct be_adapter *adapter, u8 *fw_file)
{
	const struct firmware *fw;
	int status;

	if (!netif_running(adapter->netdev)) {
		dev_err(&adapter->pdev->dev,
			"Firmware load not allowed (interface is down)\n");
		return -ENETDOWN;
	}

	status = request_firmware(&fw, fw_file, &adapter->pdev->dev);
	if (status)
		goto fw_exit;

	dev_info(&adapter->pdev->dev, "Flashing firmware file %s\n", fw_file);

	if (lancer_chip(adapter))
		status = lancer_fw_download(adapter, fw);
	else
		status = be_fw_download(adapter, fw);

	if (!status)
		be_cmd_get_fw_ver(adapter);

fw_exit:
	release_firmware(fw);
	return status;
}

static int be_ndo_bridge_setlink(struct net_device *dev, struct nlmsghdr *nlh,
				 u16 flags)
{
	struct be_adapter *adapter = netdev_priv(dev);
	struct nlattr *attr, *br_spec;
	int rem;
	int status = 0;
	u16 mode = 0;

	if (!sriov_enabled(adapter))
		return -EOPNOTSUPP;

	br_spec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (!br_spec)
		return -EINVAL;

	nla_for_each_nested(attr, br_spec, rem) {
		if (nla_type(attr) != IFLA_BRIDGE_MODE)
			continue;

		if (nla_len(attr) < sizeof(mode))
			return -EINVAL;

		mode = nla_get_u16(attr);
		if (mode != BRIDGE_MODE_VEPA && mode != BRIDGE_MODE_VEB)
			return -EINVAL;

		status = be_cmd_set_hsw_config(adapter, 0, 0,
					       adapter->if_handle,
					       mode == BRIDGE_MODE_VEPA ?
					       PORT_FWD_TYPE_VEPA :
					       PORT_FWD_TYPE_VEB, 0);
		if (status)
			goto err;

		dev_info(&adapter->pdev->dev, "enabled switch mode: %s\n",
			 mode == BRIDGE_MODE_VEPA ? "VEPA" : "VEB");

		return status;
	}
err:
	dev_err(&adapter->pdev->dev, "Failed to set switch mode %s\n",
		mode == BRIDGE_MODE_VEPA ? "VEPA" : "VEB");

	return status;
}

static int be_ndo_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
				 struct net_device *dev, u32 filter_mask,
				 int nlflags)
{
	struct be_adapter *adapter = netdev_priv(dev);
	int status = 0;
	u8 hsw_mode;

	/* BE and Lancer chips support VEB mode only */
	if (BEx_chip(adapter) || lancer_chip(adapter)) {
		hsw_mode = PORT_FWD_TYPE_VEB;
	} else {
		status = be_cmd_get_hsw_config(adapter, NULL, 0,
					       adapter->if_handle, &hsw_mode,
					       NULL);
		if (status)
			return 0;

		if (hsw_mode == PORT_FWD_TYPE_PASSTHRU)
			return 0;
	}

	return ndo_dflt_bridge_getlink(skb, pid, seq, dev,
				       hsw_mode == PORT_FWD_TYPE_VEPA ?
				       BRIDGE_MODE_VEPA : BRIDGE_MODE_VEB,
				       0, 0, nlflags, filter_mask, NULL);
}

#ifdef CONFIG_BE2NET_VXLAN
/* VxLAN offload Notes:
 *
 * The stack defines tunnel offload flags (hw_enc_features) for IP and doesn't
 * distinguish various types of transports (VxLAN, GRE, NVGRE ..). So, offload
 * is expected to work across all types of IP tunnels once exported. Skyhawk
 * supports offloads for either VxLAN or NVGRE, exclusively. So we export VxLAN
 * offloads in hw_enc_features only when a VxLAN port is added. If other (non
 * VxLAN) tunnels are configured while VxLAN offloads are enabled, offloads for
 * those other tunnels are unexported on the fly through ndo_features_check().
 *
 * Skyhawk supports VxLAN offloads only for one UDP dport. So, if the stack
 * adds more than one port, disable offloads and don't re-enable them again
 * until after all the tunnels are removed.
 */
static void be_add_vxlan_port(struct net_device *netdev, sa_family_t sa_family,
			      __be16 port)
{
	struct be_adapter *adapter = netdev_priv(netdev);
	struct device *dev = &adapter->pdev->dev;
	int status;

	if (lancer_chip(adapter) || BEx_chip(adapter) || be_is_mc(adapter))
		return;

	if (adapter->vxlan_port == port && adapter->vxlan_port_count) {
		adapter->vxlan_port_aliases++;
		return;
	}

	if (adapter->flags & BE_FLAGS_VXLAN_OFFLOADS) {
		dev_info(dev,
			 "Only one UDP port supported for VxLAN offloads\n");
		dev_info(dev, "Disabling VxLAN offloads\n");
		adapter->vxlan_port_count++;
		goto err;
	}

	if (adapter->vxlan_port_count++ >= 1)
		return;

	status = be_cmd_manage_iface(adapter, adapter->if_handle,
				     OP_CONVERT_NORMAL_TO_TUNNEL);
	if (status) {
		dev_warn(dev, "Failed to convert normal interface to tunnel\n");
		goto err;
	}

	status = be_cmd_set_vxlan_port(adapter, port);
	if (status) {
		dev_warn(dev, "Failed to add VxLAN port\n");
		goto err;
	}
	adapter->flags |= BE_FLAGS_VXLAN_OFFLOADS;
	adapter->vxlan_port = port;

	netdev->hw_enc_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
				   NETIF_F_TSO | NETIF_F_TSO6 |
				   NETIF_F_GSO_UDP_TUNNEL;
	netdev->hw_features |= NETIF_F_GSO_UDP_TUNNEL;
	netdev->features |= NETIF_F_GSO_UDP_TUNNEL;

	dev_info(dev, "Enabled VxLAN offloads for UDP port %d\n",
		 be16_to_cpu(port));
	return;
err:
	be_disable_vxlan_offloads(adapter);
}

static void be_del_vxlan_port(struct net_device *netdev, sa_family_t sa_family,
			      __be16 port)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	if (lancer_chip(adapter) || BEx_chip(adapter) || be_is_mc(adapter))
		return;

	if (adapter->vxlan_port != port)
		goto done;

	if (adapter->vxlan_port_aliases) {
		adapter->vxlan_port_aliases--;
		return;
	}

	be_disable_vxlan_offloads(adapter);

	dev_info(&adapter->pdev->dev,
		 "Disabled VxLAN offloads for UDP port %d\n",
		 be16_to_cpu(port));
done:
	adapter->vxlan_port_count--;
}

static netdev_features_t be_features_check(struct sk_buff *skb,
					   struct net_device *dev,
					   netdev_features_t features)
{
	struct be_adapter *adapter = netdev_priv(dev);
	u8 l4_hdr = 0;

	/* The code below restricts offload features for some tunneled packets.
	 * Offload features for normal (non tunnel) packets are unchanged.
	 */
	if (!skb->encapsulation ||
	    !(adapter->flags & BE_FLAGS_VXLAN_OFFLOADS))
		return features;

	/* It's an encapsulated packet and VxLAN offloads are enabled. We
	 * should disable tunnel offload features if it's not a VxLAN packet,
	 * as tunnel offloads have been enabled only for VxLAN. This is done to
	 * allow other tunneled traffic like GRE work fine while VxLAN
	 * offloads are configured in Skyhawk-R.
	 */
	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IP):
		l4_hdr = ip_hdr(skb)->protocol;
		break;
	case htons(ETH_P_IPV6):
		l4_hdr = ipv6_hdr(skb)->nexthdr;
		break;
	default:
		return features;
	}

	if (l4_hdr != IPPROTO_UDP ||
	    skb->inner_protocol_type != ENCAP_TYPE_ETHER ||
	    skb->inner_protocol != htons(ETH_P_TEB) ||
	    skb_inner_mac_header(skb) - skb_transport_header(skb) !=
	    sizeof(struct udphdr) + sizeof(struct vxlanhdr))
		return features & ~(NETIF_F_ALL_CSUM | NETIF_F_GSO_MASK);

	return features;
}
#endif

static int be_get_phys_port_id(struct net_device *dev,
			       struct netdev_phys_item_id *ppid)
{
	int i, id_len = CNTL_SERIAL_NUM_WORDS * CNTL_SERIAL_NUM_WORD_SZ + 1;
	struct be_adapter *adapter = netdev_priv(dev);
	u8 *id;

	if (MAX_PHYS_ITEM_ID_LEN < id_len)
		return -ENOSPC;

	ppid->id[0] = adapter->hba_port_num + 1;
	id = &ppid->id[1];
	for (i = CNTL_SERIAL_NUM_WORDS - 1; i >= 0;
	     i--, id += CNTL_SERIAL_NUM_WORD_SZ)
		memcpy(id, &adapter->serial_num[i], CNTL_SERIAL_NUM_WORD_SZ);

	ppid->id_len = id_len;

	return 0;
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
	.ndo_set_vf_rate	= be_set_vf_tx_rate,
	.ndo_get_vf_config	= be_get_vf_config,
	.ndo_set_vf_link_state  = be_set_vf_link_state,
	.ndo_set_vf_spoofchk    = be_set_vf_spoofchk,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= be_netpoll,
#endif
	.ndo_bridge_setlink	= be_ndo_bridge_setlink,
	.ndo_bridge_getlink	= be_ndo_bridge_getlink,
#ifdef CONFIG_NET_RX_BUSY_POLL
	.ndo_busy_poll		= be_busy_poll,
#endif
#ifdef CONFIG_BE2NET_VXLAN
	.ndo_add_vxlan_port	= be_add_vxlan_port,
	.ndo_del_vxlan_port	= be_del_vxlan_port,
	.ndo_features_check	= be_features_check,
#endif
	.ndo_get_phys_port_id   = be_get_phys_port_id,
};

static void be_netdev_init(struct net_device *netdev)
{
	struct be_adapter *adapter = netdev_priv(netdev);

	netdev->hw_features |= NETIF_F_SG | NETIF_F_TSO | NETIF_F_TSO6 |
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM |
		NETIF_F_HW_VLAN_CTAG_TX;
	if (be_multi_rxq(adapter))
		netdev->hw_features |= NETIF_F_RXHASH;

	netdev->features |= netdev->hw_features |
		NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_CTAG_FILTER;

	netdev->vlan_features |= NETIF_F_SG | NETIF_F_TSO | NETIF_F_TSO6 |
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;

	netdev->priv_flags |= IFF_UNICAST_FLT;

	netdev->flags |= IFF_MULTICAST;

	netif_set_gso_max_size(netdev, 65535 - ETH_HLEN);

	netdev->netdev_ops = &be_netdev_ops;

	netdev->ethtool_ops = &be_ethtool_ops;
}

static void be_cleanup(struct be_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	rtnl_lock();
	netif_device_detach(netdev);
	if (netif_running(netdev))
		be_close(netdev);
	rtnl_unlock();

	be_clear(adapter);
}

static int be_resume(struct be_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int status;

	status = be_setup(adapter);
	if (status)
		return status;

	if (netif_running(netdev)) {
		status = be_open(netdev);
		if (status)
			return status;
	}

	netif_device_attach(netdev);

	return 0;
}

static int be_err_recover(struct be_adapter *adapter)
{
	struct device *dev = &adapter->pdev->dev;
	int status;

	status = be_resume(adapter);
	if (status)
		goto err;

	dev_info(dev, "Adapter recovery successful\n");
	return 0;
err:
	if (be_physfn(adapter))
		dev_err(dev, "Adapter recovery failed\n");
	else
		dev_err(dev, "Re-trying adapter recovery\n");

	return status;
}

static void be_err_detection_task(struct work_struct *work)
{
	struct be_adapter *adapter =
				container_of(work, struct be_adapter,
					     be_err_detection_work.work);
	int status = 0;

	be_detect_error(adapter);

	if (be_check_error(adapter, BE_ERROR_HW)) {
		be_cleanup(adapter);

		/* As of now error recovery support is in Lancer only */
		if (lancer_chip(adapter))
			status = be_err_recover(adapter);
	}

	/* Always attempt recovery on VFs */
	if (!status || be_virtfn(adapter))
		be_schedule_err_detection(adapter);
}

static void be_log_sfp_info(struct be_adapter *adapter)
{
	int status;

	status = be_cmd_query_sfp_info(adapter);
	if (!status) {
		dev_err(&adapter->pdev->dev,
			"Unqualified SFP+ detected on %c from %s part no: %s",
			adapter->port_name, adapter->phy.vendor_name,
			adapter->phy.vendor_pn);
	}
	adapter->flags &= ~BE_FLAGS_EVT_INCOMPATIBLE_SFP;
}

static void be_worker(struct work_struct *work)
{
	struct be_adapter *adapter =
		container_of(work, struct be_adapter, work.work);
	struct be_rx_obj *rxo;
	int i;

	/* when interrupts are not yet enabled, just reap any pending
	 * mcc completions
	 */
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

	if (be_physfn(adapter) &&
	    MODULO(adapter->work_counter, adapter->be_get_temp_freq) == 0)
		be_cmd_get_die_temperature(adapter);

	for_all_rx_queues(adapter, rxo, i) {
		/* Replenish RX-queues starved due to memory
		 * allocation failures.
		 */
		if (rxo->rx_post_starved)
			be_post_rx_frags(rxo, GFP_KERNEL, MAX_RX_POST);
	}

	/* EQ-delay update for Skyhawk is done while notifying EQ */
	if (!skyhawk_chip(adapter))
		be_eqd_update(adapter, false);

	if (adapter->flags & BE_FLAGS_EVT_INCOMPATIBLE_SFP)
		be_log_sfp_info(adapter);

reschedule:
	adapter->work_counter++;
	schedule_delayed_work(&adapter->work, msecs_to_jiffies(1000));
}

static void be_unmap_pci_bars(struct be_adapter *adapter)
{
	if (adapter->csr)
		pci_iounmap(adapter->pdev, adapter->csr);
	if (adapter->db)
		pci_iounmap(adapter->pdev, adapter->db);
}

static int db_bar(struct be_adapter *adapter)
{
	if (lancer_chip(adapter) || be_virtfn(adapter))
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
	struct pci_dev *pdev = adapter->pdev;
	u8 __iomem *addr;
	u32 sli_intf;

	pci_read_config_dword(adapter->pdev, SLI_INTF_REG_OFFSET, &sli_intf);
	adapter->sli_family = (sli_intf & SLI_INTF_FAMILY_MASK) >>
				SLI_INTF_FAMILY_SHIFT;
	adapter->virtfn = (sli_intf & SLI_INTF_FT_MASK) ? 1 : 0;

	if (BEx_chip(adapter) && be_physfn(adapter)) {
		adapter->csr = pci_iomap(pdev, 2, 0);
		if (!adapter->csr)
			return -ENOMEM;
	}

	addr = pci_iomap(pdev, db_bar(adapter), 0);
	if (!addr)
		goto pci_map_err;
	adapter->db = addr;

	if (skyhawk_chip(adapter) || BEx_chip(adapter)) {
		if (be_physfn(adapter)) {
			/* PCICFG is the 2nd BAR in BE2 */
			addr = pci_iomap(pdev, BE2_chip(adapter) ? 1 : 0, 0);
			if (!addr)
				goto pci_map_err;
			adapter->pcicfg = addr;
		} else {
			adapter->pcicfg = adapter->db + SRIOV_VF_PCICFG_OFFSET;
		}
	}

	be_roce_map_pci_bars(adapter);
	return 0;

pci_map_err:
	dev_err(&pdev->dev, "Error in mapping PCI BARs\n");
	be_unmap_pci_bars(adapter);
	return -ENOMEM;
}

static void be_drv_cleanup(struct be_adapter *adapter)
{
	struct be_dma_mem *mem = &adapter->mbox_mem_alloced;
	struct device *dev = &adapter->pdev->dev;

	if (mem->va)
		dma_free_coherent(dev, mem->size, mem->va, mem->dma);

	mem = &adapter->rx_filter;
	if (mem->va)
		dma_free_coherent(dev, mem->size, mem->va, mem->dma);

	mem = &adapter->stats_cmd;
	if (mem->va)
		dma_free_coherent(dev, mem->size, mem->va, mem->dma);
}

/* Allocate and initialize various fields in be_adapter struct */
static int be_drv_init(struct be_adapter *adapter)
{
	struct be_dma_mem *mbox_mem_alloc = &adapter->mbox_mem_alloced;
	struct be_dma_mem *mbox_mem_align = &adapter->mbox_mem;
	struct be_dma_mem *rx_filter = &adapter->rx_filter;
	struct be_dma_mem *stats_cmd = &adapter->stats_cmd;
	struct device *dev = &adapter->pdev->dev;
	int status = 0;

	mbox_mem_alloc->size = sizeof(struct be_mcc_mailbox) + 16;
	mbox_mem_alloc->va = dma_zalloc_coherent(dev, mbox_mem_alloc->size,
						 &mbox_mem_alloc->dma,
						 GFP_KERNEL);
	if (!mbox_mem_alloc->va)
		return -ENOMEM;

	mbox_mem_align->size = sizeof(struct be_mcc_mailbox);
	mbox_mem_align->va = PTR_ALIGN(mbox_mem_alloc->va, 16);
	mbox_mem_align->dma = PTR_ALIGN(mbox_mem_alloc->dma, 16);

	rx_filter->size = sizeof(struct be_cmd_req_rx_filter);
	rx_filter->va = dma_zalloc_coherent(dev, rx_filter->size,
					    &rx_filter->dma, GFP_KERNEL);
	if (!rx_filter->va) {
		status = -ENOMEM;
		goto free_mbox;
	}

	if (lancer_chip(adapter))
		stats_cmd->size = sizeof(struct lancer_cmd_req_pport_stats);
	else if (BE2_chip(adapter))
		stats_cmd->size = sizeof(struct be_cmd_req_get_stats_v0);
	else if (BE3_chip(adapter))
		stats_cmd->size = sizeof(struct be_cmd_req_get_stats_v1);
	else
		stats_cmd->size = sizeof(struct be_cmd_req_get_stats_v2);
	stats_cmd->va = dma_zalloc_coherent(dev, stats_cmd->size,
					    &stats_cmd->dma, GFP_KERNEL);
	if (!stats_cmd->va) {
		status = -ENOMEM;
		goto free_rx_filter;
	}

	mutex_init(&adapter->mbox_lock);
	spin_lock_init(&adapter->mcc_lock);
	spin_lock_init(&adapter->mcc_cq_lock);
	init_completion(&adapter->et_cmd_compl);

	pci_save_state(adapter->pdev);

	INIT_DELAYED_WORK(&adapter->work, be_worker);
	INIT_DELAYED_WORK(&adapter->be_err_detection_work,
			  be_err_detection_task);

	adapter->rx_fc = true;
	adapter->tx_fc = true;

	/* Must be a power of 2 or else MODULO will BUG_ON */
	adapter->be_get_temp_freq = 64;

	return 0;

free_rx_filter:
	dma_free_coherent(dev, rx_filter->size, rx_filter->va, rx_filter->dma);
free_mbox:
	dma_free_coherent(dev, mbox_mem_alloc->size, mbox_mem_alloc->va,
			  mbox_mem_alloc->dma);
	return status;
}

static void be_remove(struct pci_dev *pdev)
{
	struct be_adapter *adapter = pci_get_drvdata(pdev);

	if (!adapter)
		return;

	be_roce_dev_remove(adapter);
	be_intr_set(adapter, false);

	be_cancel_err_detection(adapter);

	unregister_netdev(adapter->netdev);

	be_clear(adapter);

	/* tell fw we're done with firing cmds */
	be_cmd_fw_clean(adapter);

	be_unmap_pci_bars(adapter);
	be_drv_cleanup(adapter);

	pci_disable_pcie_error_reporting(pdev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	free_netdev(adapter->netdev);
}

static ssize_t be_hwmon_show_temp(struct device *dev,
				  struct device_attribute *dev_attr,
				  char *buf)
{
	struct be_adapter *adapter = dev_get_drvdata(dev);

	/* Unit: millidegree Celsius */
	if (adapter->hwmon_info.be_on_die_temp == BE_INVALID_DIE_TEMP)
		return -EIO;
	else
		return sprintf(buf, "%u\n",
			       adapter->hwmon_info.be_on_die_temp * 1000);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO,
			  be_hwmon_show_temp, NULL, 1);

static struct attribute *be_hwmon_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(be_hwmon);

static char *mc_name(struct be_adapter *adapter)
{
	char *str = "";	/* default */

	switch (adapter->mc_type) {
	case UMC:
		str = "UMC";
		break;
	case FLEX10:
		str = "FLEX10";
		break;
	case vNIC1:
		str = "vNIC-1";
		break;
	case nPAR:
		str = "nPAR";
		break;
	case UFP:
		str = "UFP";
		break;
	case vNIC2:
		str = "vNIC-2";
		break;
	default:
		str = "";
	}

	return str;
}

static inline char *func_name(struct be_adapter *adapter)
{
	return be_physfn(adapter) ? "PF" : "VF";
}

static inline char *nic_name(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case OC_DEVICE_ID1:
		return OC_NAME;
	case OC_DEVICE_ID2:
		return OC_NAME_BE;
	case OC_DEVICE_ID3:
	case OC_DEVICE_ID4:
		return OC_NAME_LANCER;
	case BE_DEVICE_ID2:
		return BE3_NAME;
	case OC_DEVICE_ID5:
	case OC_DEVICE_ID6:
		return OC_NAME_SH;
	default:
		return BE_NAME;
	}
}

static int be_probe(struct pci_dev *pdev, const struct pci_device_id *pdev_id)
{
	struct be_adapter *adapter;
	struct net_device *netdev;
	int status = 0;

	dev_info(&pdev->dev, "%s version is %s\n", DRV_NAME, DRV_VER);

	status = pci_enable_device(pdev);
	if (status)
		goto do_none;

	status = pci_request_regions(pdev, DRV_NAME);
	if (status)
		goto disable_dev;
	pci_set_master(pdev);

	netdev = alloc_etherdev_mqs(sizeof(*adapter), MAX_TX_QS, MAX_RX_QS);
	if (!netdev) {
		status = -ENOMEM;
		goto rel_reg;
	}
	adapter = netdev_priv(netdev);
	adapter->pdev = pdev;
	pci_set_drvdata(pdev, adapter);
	adapter->netdev = netdev;
	SET_NETDEV_DEV(netdev, &pdev->dev);

	status = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (!status) {
		netdev->features |= NETIF_F_HIGHDMA;
	} else {
		status = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (status) {
			dev_err(&pdev->dev, "Could not set PCI DMA Mask\n");
			goto free_netdev;
		}
	}

	status = pci_enable_pcie_error_reporting(pdev);
	if (!status)
		dev_info(&pdev->dev, "PCIe error reporting enabled\n");

	status = be_map_pci_bars(adapter);
	if (status)
		goto free_netdev;

	status = be_drv_init(adapter);
	if (status)
		goto unmap_bars;

	status = be_setup(adapter);
	if (status)
		goto drv_cleanup;

	be_netdev_init(netdev);
	status = register_netdev(netdev);
	if (status != 0)
		goto unsetup;

	be_roce_dev_add(adapter);

	be_schedule_err_detection(adapter);

	/* On Die temperature not supported for VF. */
	if (be_physfn(adapter) && IS_ENABLED(CONFIG_BE2NET_HWMON)) {
		adapter->hwmon_info.hwmon_dev =
			devm_hwmon_device_register_with_groups(&pdev->dev,
							       DRV_NAME,
							       adapter,
							       be_hwmon_groups);
		adapter->hwmon_info.be_on_die_temp = BE_INVALID_DIE_TEMP;
	}

	dev_info(&pdev->dev, "%s: %s %s port %c\n", nic_name(pdev),
		 func_name(adapter), mc_name(adapter), adapter->port_name);

	return 0;

unsetup:
	be_clear(adapter);
drv_cleanup:
	be_drv_cleanup(adapter);
unmap_bars:
	be_unmap_pci_bars(adapter);
free_netdev:
	free_netdev(netdev);
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

	if (adapter->wol_en)
		be_setup_wol(adapter, true);

	be_intr_set(adapter, false);
	be_cancel_err_detection(adapter);

	be_cleanup(adapter);

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int be_pci_resume(struct pci_dev *pdev)
{
	struct be_adapter *adapter = pci_get_drvdata(pdev);
	int status = 0;

	status = pci_enable_device(pdev);
	if (status)
		return status;

	pci_restore_state(pdev);

	status = be_resume(adapter);
	if (status)
		return status;

	be_schedule_err_detection(adapter);

	if (adapter->wol_en)
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

	be_roce_dev_shutdown(adapter);
	cancel_delayed_work_sync(&adapter->work);
	be_cancel_err_detection(adapter);

	netif_device_detach(adapter->netdev);

	be_cmd_reset_function(adapter);

	pci_disable_device(pdev);
}

static pci_ers_result_t be_eeh_err_detected(struct pci_dev *pdev,
					    pci_channel_state_t state)
{
	struct be_adapter *adapter = pci_get_drvdata(pdev);

	dev_err(&adapter->pdev->dev, "EEH error detected\n");

	if (!be_check_error(adapter, BE_ERROR_EEH)) {
		be_set_error(adapter, BE_ERROR_EEH);

		be_cancel_err_detection(adapter);

		be_cleanup(adapter);
	}

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

	status = pci_enable_device(pdev);
	if (status)
		return PCI_ERS_RESULT_DISCONNECT;

	pci_set_master(pdev);
	pci_restore_state(pdev);

	/* Check if card is ok and fw is ready */
	dev_info(&adapter->pdev->dev,
		 "Waiting for FW to be ready after EEH reset\n");
	status = be_fw_wait_ready(adapter);
	if (status)
		return PCI_ERS_RESULT_DISCONNECT;

	pci_cleanup_aer_uncorrect_error_status(pdev);
	be_clear_error(adapter, BE_CLEAR_ALL);
	return PCI_ERS_RESULT_RECOVERED;
}

static void be_eeh_resume(struct pci_dev *pdev)
{
	int status = 0;
	struct be_adapter *adapter = pci_get_drvdata(pdev);

	dev_info(&adapter->pdev->dev, "EEH resume\n");

	pci_save_state(pdev);

	status = be_resume(adapter);
	if (status)
		goto err;

	be_schedule_err_detection(adapter);
	return;
err:
	dev_err(&adapter->pdev->dev, "EEH resume failed\n");
}

static int be_pci_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct be_adapter *adapter = pci_get_drvdata(pdev);
	u16 num_vf_qs;
	int status;

	if (!num_vfs)
		be_vf_clear(adapter);

	adapter->num_vfs = num_vfs;

	if (adapter->num_vfs == 0 && pci_vfs_assigned(pdev)) {
		dev_warn(&pdev->dev,
			 "Cannot disable VFs while they are assigned\n");
		return -EBUSY;
	}

	/* When the HW is in SRIOV capable configuration, the PF-pool resources
	 * are equally distributed across the max-number of VFs. The user may
	 * request only a subset of the max-vfs to be enabled.
	 * Based on num_vfs, redistribute the resources across num_vfs so that
	 * each VF will have access to more number of resources.
	 * This facility is not available in BE3 FW.
	 * Also, this is done by FW in Lancer chip.
	 */
	if (skyhawk_chip(adapter) && !pci_num_vf(pdev)) {
		num_vf_qs = be_calculate_vf_qs(adapter, adapter->num_vfs);
		status = be_cmd_set_sriov_config(adapter, adapter->pool_res,
						 adapter->num_vfs, num_vf_qs);
		if (status)
			dev_err(&pdev->dev,
				"Failed to optimize SR-IOV resources\n");
	}

	status = be_get_resources(adapter);
	if (status)
		return be_cmd_status(status);

	/* Updating real_num_tx/rx_queues() requires rtnl_lock() */
	rtnl_lock();
	status = be_update_queues(adapter);
	rtnl_unlock();
	if (status)
		return be_cmd_status(status);

	if (adapter->num_vfs)
		status = be_vf_setup(adapter);

	if (!status)
		return adapter->num_vfs;

	return 0;
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
	.resume = be_pci_resume,
	.shutdown = be_shutdown,
	.sriov_configure = be_pci_sriov_configure,
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

	if (num_vfs > 0) {
		pr_info(DRV_NAME " : Module param num_vfs is obsolete.");
		pr_info(DRV_NAME " : Use sysfs method to enable VFs\n");
	}

	return pci_register_driver(&be_driver);
}
module_init(be_init_module);

static void __exit be_exit_module(void)
{
	pci_unregister_driver(&be_driver);
}
module_exit(be_exit_module);
