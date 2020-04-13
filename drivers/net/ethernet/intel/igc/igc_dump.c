// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

#include "igc.h"

struct igc_reg_info {
	u32 ofs;
	char *name;
};

static const struct igc_reg_info igc_reg_info_tbl[] = {
	/* General Registers */
	{IGC_CTRL, "CTRL"},
	{IGC_STATUS, "STATUS"},
	{IGC_CTRL_EXT, "CTRL_EXT"},
	{IGC_MDIC, "MDIC"},

	/* Interrupt Registers */
	{IGC_ICR, "ICR"},

	/* RX Registers */
	{IGC_RCTL, "RCTL"},
	{IGC_RDLEN(0), "RDLEN"},
	{IGC_RDH(0), "RDH"},
	{IGC_RDT(0), "RDT"},
	{IGC_RXDCTL(0), "RXDCTL"},
	{IGC_RDBAL(0), "RDBAL"},
	{IGC_RDBAH(0), "RDBAH"},

	/* TX Registers */
	{IGC_TCTL, "TCTL"},
	{IGC_TDBAL(0), "TDBAL"},
	{IGC_TDBAH(0), "TDBAH"},
	{IGC_TDLEN(0), "TDLEN"},
	{IGC_TDH(0), "TDH"},
	{IGC_TDT(0), "TDT"},
	{IGC_TXDCTL(0), "TXDCTL"},
	{IGC_TDFH, "TDFH"},
	{IGC_TDFT, "TDFT"},
	{IGC_TDFHS, "TDFHS"},
	{IGC_TDFPC, "TDFPC"},

	/* List Terminator */
	{}
};

/* igc_regdump - register printout routine */
static void igc_regdump(struct igc_hw *hw, struct igc_reg_info *reginfo)
{
	int n = 0;
	char rname[16];
	u32 regs[8];

	switch (reginfo->ofs) {
	case IGC_RDLEN(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_RDLEN(n));
		break;
	case IGC_RDH(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_RDH(n));
		break;
	case IGC_RDT(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_RDT(n));
		break;
	case IGC_RXDCTL(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_RXDCTL(n));
		break;
	case IGC_RDBAL(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_RDBAL(n));
		break;
	case IGC_RDBAH(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_RDBAH(n));
		break;
	case IGC_TDBAL(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_RDBAL(n));
		break;
	case IGC_TDBAH(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_TDBAH(n));
		break;
	case IGC_TDLEN(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_TDLEN(n));
		break;
	case IGC_TDH(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_TDH(n));
		break;
	case IGC_TDT(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_TDT(n));
		break;
	case IGC_TXDCTL(0):
		for (n = 0; n < 4; n++)
			regs[n] = rd32(IGC_TXDCTL(n));
		break;
	default:
		pr_info("%-15s %08x\n", reginfo->name, rd32(reginfo->ofs));
		return;
	}

	snprintf(rname, 16, "%s%s", reginfo->name, "[0-3]");
	pr_info("%-15s %08x %08x %08x %08x\n", rname, regs[0], regs[1],
		regs[2], regs[3]);
}

/* igc_rings_dump - Tx-rings and Rx-rings */
void igc_rings_dump(struct igc_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct my_u0 { u64 a; u64 b; } *u0;
	union igc_adv_tx_desc *tx_desc;
	union igc_adv_rx_desc *rx_desc;
	struct igc_ring *tx_ring;
	struct igc_ring *rx_ring;
	u32 staterr;
	u16 i, n;

	if (!netif_msg_hw(adapter))
		return;

	/* Print netdevice Info */
	if (netdev) {
		dev_info(&adapter->pdev->dev, "Net device Info\n");
		pr_info("Device Name     state            trans_start\n");
		pr_info("%-15s %016lX %016lX\n", netdev->name,
			netdev->state, dev_trans_start(netdev));
	}

	/* Print TX Ring Summary */
	if (!netdev || !netif_running(netdev))
		goto exit;

	dev_info(&adapter->pdev->dev, "TX Rings Summary\n");
	pr_info("Queue [NTU] [NTC] [bi(ntc)->dma  ] leng ntw timestamp\n");
	for (n = 0; n < adapter->num_tx_queues; n++) {
		struct igc_tx_buffer *buffer_info;

		tx_ring = adapter->tx_ring[n];
		buffer_info = &tx_ring->tx_buffer_info[tx_ring->next_to_clean];

		pr_info(" %5d %5X %5X %016llX %04X %p %016llX\n",
			n, tx_ring->next_to_use, tx_ring->next_to_clean,
			(u64)dma_unmap_addr(buffer_info, dma),
			dma_unmap_len(buffer_info, len),
			buffer_info->next_to_watch,
			(u64)buffer_info->time_stamp);
	}

	/* Print TX Rings */
	if (!netif_msg_tx_done(adapter))
		goto rx_ring_summary;

	dev_info(&adapter->pdev->dev, "TX Rings Dump\n");

	/* Transmit Descriptor Formats
	 *
	 * Advanced Transmit Descriptor
	 *   +--------------------------------------------------------------+
	 * 0 |         Buffer Address [63:0]                                |
	 *   +--------------------------------------------------------------+
	 * 8 | PAYLEN  | PORTS  |CC|IDX | STA | DCMD  |DTYP|MAC|RSV| DTALEN |
	 *   +--------------------------------------------------------------+
	 *   63      46 45    40 39 38 36 35 32 31   24             15       0
	 */

	for (n = 0; n < adapter->num_tx_queues; n++) {
		tx_ring = adapter->tx_ring[n];
		pr_info("------------------------------------\n");
		pr_info("TX QUEUE INDEX = %d\n", tx_ring->queue_index);
		pr_info("------------------------------------\n");
		pr_info("T [desc]     [address 63:0  ] [PlPOCIStDDM Ln] [bi->dma       ] leng  ntw timestamp        bi->skb\n");

		for (i = 0; tx_ring->desc && (i < tx_ring->count); i++) {
			const char *next_desc;
			struct igc_tx_buffer *buffer_info;

			tx_desc = IGC_TX_DESC(tx_ring, i);
			buffer_info = &tx_ring->tx_buffer_info[i];
			u0 = (struct my_u0 *)tx_desc;
			if (i == tx_ring->next_to_use &&
			    i == tx_ring->next_to_clean)
				next_desc = " NTC/U";
			else if (i == tx_ring->next_to_use)
				next_desc = " NTU";
			else if (i == tx_ring->next_to_clean)
				next_desc = " NTC";
			else
				next_desc = "";

			pr_info("T [0x%03X]    %016llX %016llX %016llX %04X  %p %016llX %p%s\n",
				i, le64_to_cpu(u0->a),
				le64_to_cpu(u0->b),
				(u64)dma_unmap_addr(buffer_info, dma),
				dma_unmap_len(buffer_info, len),
				buffer_info->next_to_watch,
				(u64)buffer_info->time_stamp,
				buffer_info->skb, next_desc);

			if (netif_msg_pktdata(adapter) && buffer_info->skb)
				print_hex_dump(KERN_INFO, "",
					       DUMP_PREFIX_ADDRESS,
					       16, 1, buffer_info->skb->data,
					       dma_unmap_len(buffer_info, len),
					       true);
		}
	}

	/* Print RX Rings Summary */
rx_ring_summary:
	dev_info(&adapter->pdev->dev, "RX Rings Summary\n");
	pr_info("Queue [NTU] [NTC]\n");
	for (n = 0; n < adapter->num_rx_queues; n++) {
		rx_ring = adapter->rx_ring[n];
		pr_info(" %5d %5X %5X\n",
			n, rx_ring->next_to_use, rx_ring->next_to_clean);
	}

	/* Print RX Rings */
	if (!netif_msg_rx_status(adapter))
		goto exit;

	dev_info(&adapter->pdev->dev, "RX Rings Dump\n");

	/* Advanced Receive Descriptor (Read) Format
	 *    63                                           1        0
	 *    +-----------------------------------------------------+
	 *  0 |       Packet Buffer Address [63:1]           |A0/NSE|
	 *    +----------------------------------------------+------+
	 *  8 |       Header Buffer Address [63:1]           |  DD  |
	 *    +-----------------------------------------------------+
	 *
	 *
	 * Advanced Receive Descriptor (Write-Back) Format
	 *
	 *   63       48 47    32 31  30      21 20 17 16   4 3     0
	 *   +------------------------------------------------------+
	 * 0 | Packet     IP     |SPH| HDR_LEN   | RSV|Packet|  RSS |
	 *   | Checksum   Ident  |   |           |    | Type | Type |
	 *   +------------------------------------------------------+
	 * 8 | VLAN Tag | Length | Extended Error | Extended Status |
	 *   +------------------------------------------------------+
	 *   63       48 47    32 31            20 19               0
	 */

	for (n = 0; n < adapter->num_rx_queues; n++) {
		rx_ring = adapter->rx_ring[n];
		pr_info("------------------------------------\n");
		pr_info("RX QUEUE INDEX = %d\n", rx_ring->queue_index);
		pr_info("------------------------------------\n");
		pr_info("R  [desc]      [ PktBuf     A0] [  HeadBuf   DD] [bi->dma       ] [bi->skb] <-- Adv Rx Read format\n");
		pr_info("RWB[desc]      [PcsmIpSHl PtRs] [vl er S cks ln] ---------------- [bi->skb] <-- Adv Rx Write-Back format\n");

		for (i = 0; i < rx_ring->count; i++) {
			const char *next_desc;
			struct igc_rx_buffer *buffer_info;

			buffer_info = &rx_ring->rx_buffer_info[i];
			rx_desc = IGC_RX_DESC(rx_ring, i);
			u0 = (struct my_u0 *)rx_desc;
			staterr = le32_to_cpu(rx_desc->wb.upper.status_error);

			if (i == rx_ring->next_to_use)
				next_desc = " NTU";
			else if (i == rx_ring->next_to_clean)
				next_desc = " NTC";
			else
				next_desc = "";

			if (staterr & IGC_RXD_STAT_DD) {
				/* Descriptor Done */
				pr_info("%s[0x%03X]     %016llX %016llX ---------------- %s\n",
					"RWB", i,
					le64_to_cpu(u0->a),
					le64_to_cpu(u0->b),
					next_desc);
			} else {
				pr_info("%s[0x%03X]     %016llX %016llX %016llX %s\n",
					"R  ", i,
					le64_to_cpu(u0->a),
					le64_to_cpu(u0->b),
					(u64)buffer_info->dma,
					next_desc);

				if (netif_msg_pktdata(adapter) &&
				    buffer_info->dma && buffer_info->page) {
					print_hex_dump(KERN_INFO, "",
						       DUMP_PREFIX_ADDRESS,
						       16, 1,
						       page_address
						       (buffer_info->page) +
						       buffer_info->page_offset,
						       igc_rx_bufsz(rx_ring),
						       true);
				}
			}
		}
	}

exit:
	return;
}

/* igc_regs_dump - registers dump */
void igc_regs_dump(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	struct igc_reg_info *reginfo;

	/* Print Registers */
	dev_info(&adapter->pdev->dev, "Register Dump\n");
	pr_info(" Register Name   Value\n");
	for (reginfo = (struct igc_reg_info *)igc_reg_info_tbl;
	     reginfo->name; reginfo++) {
		igc_regdump(hw, reginfo);
	}
}
