// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 1999 - 2018 Intel Corporation. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/tcp.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/pm_qos.h>
#include <linux/pm_runtime.h>
#include <linux/aer.h>
#include <linux/prefetch.h>

#include "e1000.h"

#define DRV_EXTRAVERSION "-k"

#define DRV_VERSION "3.2.6" DRV_EXTRAVERSION
char e1000e_driver_name[] = "e1000e";
const char e1000e_driver_version[] = DRV_VERSION;

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV|NETIF_MSG_PROBE|NETIF_MSG_LINK)
static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

static const struct e1000_info *e1000_info_tbl[] = {
	[board_82571]		= &e1000_82571_info,
	[board_82572]		= &e1000_82572_info,
	[board_82573]		= &e1000_82573_info,
	[board_82574]		= &e1000_82574_info,
	[board_82583]		= &e1000_82583_info,
	[board_80003es2lan]	= &e1000_es2_info,
	[board_ich8lan]		= &e1000_ich8_info,
	[board_ich9lan]		= &e1000_ich9_info,
	[board_ich10lan]	= &e1000_ich10_info,
	[board_pchlan]		= &e1000_pch_info,
	[board_pch2lan]		= &e1000_pch2_info,
	[board_pch_lpt]		= &e1000_pch_lpt_info,
	[board_pch_spt]		= &e1000_pch_spt_info,
	[board_pch_cnp]		= &e1000_pch_cnp_info,
};

struct e1000_reg_info {
	u32 ofs;
	char *name;
};

static const struct e1000_reg_info e1000_reg_info_tbl[] = {
	/* General Registers */
	{E1000_CTRL, "CTRL"},
	{E1000_STATUS, "STATUS"},
	{E1000_CTRL_EXT, "CTRL_EXT"},

	/* Interrupt Registers */
	{E1000_ICR, "ICR"},

	/* Rx Registers */
	{E1000_RCTL, "RCTL"},
	{E1000_RDLEN(0), "RDLEN"},
	{E1000_RDH(0), "RDH"},
	{E1000_RDT(0), "RDT"},
	{E1000_RDTR, "RDTR"},
	{E1000_RXDCTL(0), "RXDCTL"},
	{E1000_ERT, "ERT"},
	{E1000_RDBAL(0), "RDBAL"},
	{E1000_RDBAH(0), "RDBAH"},
	{E1000_RDFH, "RDFH"},
	{E1000_RDFT, "RDFT"},
	{E1000_RDFHS, "RDFHS"},
	{E1000_RDFTS, "RDFTS"},
	{E1000_RDFPC, "RDFPC"},

	/* Tx Registers */
	{E1000_TCTL, "TCTL"},
	{E1000_TDBAL(0), "TDBAL"},
	{E1000_TDBAH(0), "TDBAH"},
	{E1000_TDLEN(0), "TDLEN"},
	{E1000_TDH(0), "TDH"},
	{E1000_TDT(0), "TDT"},
	{E1000_TIDV, "TIDV"},
	{E1000_TXDCTL(0), "TXDCTL"},
	{E1000_TADV, "TADV"},
	{E1000_TARC(0), "TARC"},
	{E1000_TDFH, "TDFH"},
	{E1000_TDFT, "TDFT"},
	{E1000_TDFHS, "TDFHS"},
	{E1000_TDFTS, "TDFTS"},
	{E1000_TDFPC, "TDFPC"},

	/* List Terminator */
	{0, NULL}
};

/**
 * __ew32_prepare - prepare to write to MAC CSR register on certain parts
 * @hw: pointer to the HW structure
 *
 * When updating the MAC CSR registers, the Manageability Engine (ME) could
 * be accessing the registers at the same time.  Normally, this is handled in
 * h/w by an arbiter but on some parts there is a bug that acknowledges Host
 * accesses later than it should which could result in the register to have
 * an incorrect value.  Workaround this by checking the FWSM register which
 * has bit 24 set while ME is accessing MAC CSR registers, wait if it is set
 * and try again a number of times.
 **/
s32 __ew32_prepare(struct e1000_hw *hw)
{
	s32 i = E1000_ICH_FWSM_PCIM2PCI_COUNT;

	while ((er32(FWSM) & E1000_ICH_FWSM_PCIM2PCI) && --i)
		udelay(50);

	return i;
}

void __ew32(struct e1000_hw *hw, unsigned long reg, u32 val)
{
	if (hw->adapter->flags2 & FLAG2_PCIM2PCI_ARBITER_WA)
		__ew32_prepare(hw);

	writel(val, hw->hw_addr + reg);
}

/**
 * e1000_regdump - register printout routine
 * @hw: pointer to the HW structure
 * @reginfo: pointer to the register info table
 **/
static void e1000_regdump(struct e1000_hw *hw, struct e1000_reg_info *reginfo)
{
	int n = 0;
	char rname[16];
	u32 regs[8];

	switch (reginfo->ofs) {
	case E1000_RXDCTL(0):
		for (n = 0; n < 2; n++)
			regs[n] = __er32(hw, E1000_RXDCTL(n));
		break;
	case E1000_TXDCTL(0):
		for (n = 0; n < 2; n++)
			regs[n] = __er32(hw, E1000_TXDCTL(n));
		break;
	case E1000_TARC(0):
		for (n = 0; n < 2; n++)
			regs[n] = __er32(hw, E1000_TARC(n));
		break;
	default:
		pr_info("%-15s %08x\n",
			reginfo->name, __er32(hw, reginfo->ofs));
		return;
	}

	snprintf(rname, 16, "%s%s", reginfo->name, "[0-1]");
	pr_info("%-15s %08x %08x\n", rname, regs[0], regs[1]);
}

static void e1000e_dump_ps_pages(struct e1000_adapter *adapter,
				 struct e1000_buffer *bi)
{
	int i;
	struct e1000_ps_page *ps_page;

	for (i = 0; i < adapter->rx_ps_pages; i++) {
		ps_page = &bi->ps_pages[i];

		if (ps_page->page) {
			pr_info("packet dump for ps_page %d:\n", i);
			print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS,
				       16, 1, page_address(ps_page->page),
				       PAGE_SIZE, true);
		}
	}
}

/**
 * e1000e_dump - Print registers, Tx-ring and Rx-ring
 * @adapter: board private structure
 **/
static void e1000e_dump(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_reg_info *reginfo;
	struct e1000_ring *tx_ring = adapter->tx_ring;
	struct e1000_tx_desc *tx_desc;
	struct my_u0 {
		__le64 a;
		__le64 b;
	} *u0;
	struct e1000_buffer *buffer_info;
	struct e1000_ring *rx_ring = adapter->rx_ring;
	union e1000_rx_desc_packet_split *rx_desc_ps;
	union e1000_rx_desc_extended *rx_desc;
	struct my_u1 {
		__le64 a;
		__le64 b;
		__le64 c;
		__le64 d;
	} *u1;
	u32 staterr;
	int i = 0;

	if (!netif_msg_hw(adapter))
		return;

	/* Print netdevice Info */
	if (netdev) {
		dev_info(&adapter->pdev->dev, "Net device Info\n");
		pr_info("Device Name     state            trans_start\n");
		pr_info("%-15s %016lX %016lX\n", netdev->name,
			netdev->state, dev_trans_start(netdev));
	}

	/* Print Registers */
	dev_info(&adapter->pdev->dev, "Register Dump\n");
	pr_info(" Register Name   Value\n");
	for (reginfo = (struct e1000_reg_info *)e1000_reg_info_tbl;
	     reginfo->name; reginfo++) {
		e1000_regdump(hw, reginfo);
	}

	/* Print Tx Ring Summary */
	if (!netdev || !netif_running(netdev))
		return;

	dev_info(&adapter->pdev->dev, "Tx Ring Summary\n");
	pr_info("Queue [NTU] [NTC] [bi(ntc)->dma  ] leng ntw timestamp\n");
	buffer_info = &tx_ring->buffer_info[tx_ring->next_to_clean];
	pr_info(" %5d %5X %5X %016llX %04X %3X %016llX\n",
		0, tx_ring->next_to_use, tx_ring->next_to_clean,
		(unsigned long long)buffer_info->dma,
		buffer_info->length,
		buffer_info->next_to_watch,
		(unsigned long long)buffer_info->time_stamp);

	/* Print Tx Ring */
	if (!netif_msg_tx_done(adapter))
		goto rx_ring_summary;

	dev_info(&adapter->pdev->dev, "Tx Ring Dump\n");

	/* Transmit Descriptor Formats - DEXT[29] is 0 (Legacy) or 1 (Extended)
	 *
	 * Legacy Transmit Descriptor
	 *   +--------------------------------------------------------------+
	 * 0 |         Buffer Address [63:0] (Reserved on Write Back)       |
	 *   +--------------------------------------------------------------+
	 * 8 | Special  |    CSS     | Status |  CMD    |  CSO   |  Length  |
	 *   +--------------------------------------------------------------+
	 *   63       48 47        36 35    32 31     24 23    16 15        0
	 *
	 * Extended Context Descriptor (DTYP=0x0) for TSO or checksum offload
	 *   63      48 47    40 39       32 31             16 15    8 7      0
	 *   +----------------------------------------------------------------+
	 * 0 |  TUCSE  | TUCS0  |   TUCSS   |     IPCSE       | IPCS0 | IPCSS |
	 *   +----------------------------------------------------------------+
	 * 8 |   MSS   | HDRLEN | RSV | STA | TUCMD | DTYP |      PAYLEN      |
	 *   +----------------------------------------------------------------+
	 *   63      48 47    40 39 36 35 32 31   24 23  20 19                0
	 *
	 * Extended Data Descriptor (DTYP=0x1)
	 *   +----------------------------------------------------------------+
	 * 0 |                     Buffer Address [63:0]                      |
	 *   +----------------------------------------------------------------+
	 * 8 | VLAN tag |  POPTS  | Rsvd | Status | Command | DTYP |  DTALEN  |
	 *   +----------------------------------------------------------------+
	 *   63       48 47     40 39  36 35    32 31     24 23  20 19        0
	 */
	pr_info("Tl[desc]     [address 63:0  ] [SpeCssSCmCsLen] [bi->dma       ] leng  ntw timestamp        bi->skb <-- Legacy format\n");
	pr_info("Tc[desc]     [Ce CoCsIpceCoS] [MssHlRSCm0Plen] [bi->dma       ] leng  ntw timestamp        bi->skb <-- Ext Context format\n");
	pr_info("Td[desc]     [address 63:0  ] [VlaPoRSCm1Dlen] [bi->dma       ] leng  ntw timestamp        bi->skb <-- Ext Data format\n");
	for (i = 0; tx_ring->desc && (i < tx_ring->count); i++) {
		const char *next_desc;
		tx_desc = E1000_TX_DESC(*tx_ring, i);
		buffer_info = &tx_ring->buffer_info[i];
		u0 = (struct my_u0 *)tx_desc;
		if (i == tx_ring->next_to_use && i == tx_ring->next_to_clean)
			next_desc = " NTC/U";
		else if (i == tx_ring->next_to_use)
			next_desc = " NTU";
		else if (i == tx_ring->next_to_clean)
			next_desc = " NTC";
		else
			next_desc = "";
		pr_info("T%c[0x%03X]    %016llX %016llX %016llX %04X  %3X %016llX %p%s\n",
			(!(le64_to_cpu(u0->b) & BIT(29)) ? 'l' :
			 ((le64_to_cpu(u0->b) & BIT(20)) ? 'd' : 'c')),
			i,
			(unsigned long long)le64_to_cpu(u0->a),
			(unsigned long long)le64_to_cpu(u0->b),
			(unsigned long long)buffer_info->dma,
			buffer_info->length, buffer_info->next_to_watch,
			(unsigned long long)buffer_info->time_stamp,
			buffer_info->skb, next_desc);

		if (netif_msg_pktdata(adapter) && buffer_info->skb)
			print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS,
				       16, 1, buffer_info->skb->data,
				       buffer_info->skb->len, true);
	}

	/* Print Rx Ring Summary */
rx_ring_summary:
	dev_info(&adapter->pdev->dev, "Rx Ring Summary\n");
	pr_info("Queue [NTU] [NTC]\n");
	pr_info(" %5d %5X %5X\n",
		0, rx_ring->next_to_use, rx_ring->next_to_clean);

	/* Print Rx Ring */
	if (!netif_msg_rx_status(adapter))
		return;

	dev_info(&adapter->pdev->dev, "Rx Ring Dump\n");
	switch (adapter->rx_ps_pages) {
	case 1:
	case 2:
	case 3:
		/* [Extended] Packet Split Receive Descriptor Format
		 *
		 *    +-----------------------------------------------------+
		 *  0 |                Buffer Address 0 [63:0]              |
		 *    +-----------------------------------------------------+
		 *  8 |                Buffer Address 1 [63:0]              |
		 *    +-----------------------------------------------------+
		 * 16 |                Buffer Address 2 [63:0]              |
		 *    +-----------------------------------------------------+
		 * 24 |                Buffer Address 3 [63:0]              |
		 *    +-----------------------------------------------------+
		 */
		pr_info("R  [desc]      [buffer 0 63:0 ] [buffer 1 63:0 ] [buffer 2 63:0 ] [buffer 3 63:0 ] [bi->dma       ] [bi->skb] <-- Ext Pkt Split format\n");
		/* [Extended] Receive Descriptor (Write-Back) Format
		 *
		 *   63       48 47    32 31     13 12    8 7    4 3        0
		 *   +------------------------------------------------------+
		 * 0 | Packet   | IP     |  Rsvd   | MRQ   | Rsvd | MRQ RSS |
		 *   | Checksum | Ident  |         | Queue |      |  Type   |
		 *   +------------------------------------------------------+
		 * 8 | VLAN Tag | Length | Extended Error | Extended Status |
		 *   +------------------------------------------------------+
		 *   63       48 47    32 31            20 19               0
		 */
		pr_info("RWB[desc]      [ck ipid mrqhsh] [vl   l0 ee  es] [ l3  l2  l1 hs] [reserved      ] ---------------- [bi->skb] <-- Ext Rx Write-Back format\n");
		for (i = 0; i < rx_ring->count; i++) {
			const char *next_desc;
			buffer_info = &rx_ring->buffer_info[i];
			rx_desc_ps = E1000_RX_DESC_PS(*rx_ring, i);
			u1 = (struct my_u1 *)rx_desc_ps;
			staterr =
			    le32_to_cpu(rx_desc_ps->wb.middle.status_error);

			if (i == rx_ring->next_to_use)
				next_desc = " NTU";
			else if (i == rx_ring->next_to_clean)
				next_desc = " NTC";
			else
				next_desc = "";

			if (staterr & E1000_RXD_STAT_DD) {
				/* Descriptor Done */
				pr_info("%s[0x%03X]     %016llX %016llX %016llX %016llX ---------------- %p%s\n",
					"RWB", i,
					(unsigned long long)le64_to_cpu(u1->a),
					(unsigned long long)le64_to_cpu(u1->b),
					(unsigned long long)le64_to_cpu(u1->c),
					(unsigned long long)le64_to_cpu(u1->d),
					buffer_info->skb, next_desc);
			} else {
				pr_info("%s[0x%03X]     %016llX %016llX %016llX %016llX %016llX %p%s\n",
					"R  ", i,
					(unsigned long long)le64_to_cpu(u1->a),
					(unsigned long long)le64_to_cpu(u1->b),
					(unsigned long long)le64_to_cpu(u1->c),
					(unsigned long long)le64_to_cpu(u1->d),
					(unsigned long long)buffer_info->dma,
					buffer_info->skb, next_desc);

				if (netif_msg_pktdata(adapter))
					e1000e_dump_ps_pages(adapter,
							     buffer_info);
			}
		}
		break;
	default:
	case 0:
		/* Extended Receive Descriptor (Read) Format
		 *
		 *   +-----------------------------------------------------+
		 * 0 |                Buffer Address [63:0]                |
		 *   +-----------------------------------------------------+
		 * 8 |                      Reserved                       |
		 *   +-----------------------------------------------------+
		 */
		pr_info("R  [desc]      [buf addr 63:0 ] [reserved 63:0 ] [bi->dma       ] [bi->skb] <-- Ext (Read) format\n");
		/* Extended Receive Descriptor (Write-Back) Format
		 *
		 *   63       48 47    32 31    24 23            4 3        0
		 *   +------------------------------------------------------+
		 *   |     RSS Hash      |        |               |         |
		 * 0 +-------------------+  Rsvd  |   Reserved    | MRQ RSS |
		 *   | Packet   | IP     |        |               |  Type   |
		 *   | Checksum | Ident  |        |               |         |
		 *   +------------------------------------------------------+
		 * 8 | VLAN Tag | Length | Extended Error | Extended Status |
		 *   +------------------------------------------------------+
		 *   63       48 47    32 31            20 19               0
		 */
		pr_info("RWB[desc]      [cs ipid    mrq] [vt   ln xe  xs] [bi->skb] <-- Ext (Write-Back) format\n");

		for (i = 0; i < rx_ring->count; i++) {
			const char *next_desc;

			buffer_info = &rx_ring->buffer_info[i];
			rx_desc = E1000_RX_DESC_EXT(*rx_ring, i);
			u1 = (struct my_u1 *)rx_desc;
			staterr = le32_to_cpu(rx_desc->wb.upper.status_error);

			if (i == rx_ring->next_to_use)
				next_desc = " NTU";
			else if (i == rx_ring->next_to_clean)
				next_desc = " NTC";
			else
				next_desc = "";

			if (staterr & E1000_RXD_STAT_DD) {
				/* Descriptor Done */
				pr_info("%s[0x%03X]     %016llX %016llX ---------------- %p%s\n",
					"RWB", i,
					(unsigned long long)le64_to_cpu(u1->a),
					(unsigned long long)le64_to_cpu(u1->b),
					buffer_info->skb, next_desc);
			} else {
				pr_info("%s[0x%03X]     %016llX %016llX %016llX %p%s\n",
					"R  ", i,
					(unsigned long long)le64_to_cpu(u1->a),
					(unsigned long long)le64_to_cpu(u1->b),
					(unsigned long long)buffer_info->dma,
					buffer_info->skb, next_desc);

				if (netif_msg_pktdata(adapter) &&
				    buffer_info->skb)
					print_hex_dump(KERN_INFO, "",
						       DUMP_PREFIX_ADDRESS, 16,
						       1,
						       buffer_info->skb->data,
						       adapter->rx_buffer_len,
						       true);
			}
		}
	}
}

/**
 * e1000_desc_unused - calculate if we have unused descriptors
 **/
static int e1000_desc_unused(struct e1000_ring *ring)
{
	if (ring->next_to_clean > ring->next_to_use)
		return ring->next_to_clean - ring->next_to_use - 1;

	return ring->count + ring->next_to_clean - ring->next_to_use - 1;
}

/**
 * e1000e_systim_to_hwtstamp - convert system time value to hw time stamp
 * @adapter: board private structure
 * @hwtstamps: time stamp structure to update
 * @systim: unsigned 64bit system time value.
 *
 * Convert the system time value stored in the RX/TXSTMP registers into a
 * hwtstamp which can be used by the upper level time stamping functions.
 *
 * The 'systim_lock' spinlock is used to protect the consistency of the
 * system time value. This is needed because reading the 64 bit time
 * value involves reading two 32 bit registers. The first read latches the
 * value.
 **/
static void e1000e_systim_to_hwtstamp(struct e1000_adapter *adapter,
				      struct skb_shared_hwtstamps *hwtstamps,
				      u64 systim)
{
	u64 ns;
	unsigned long flags;

	spin_lock_irqsave(&adapter->systim_lock, flags);
	ns = timecounter_cyc2time(&adapter->tc, systim);
	spin_unlock_irqrestore(&adapter->systim_lock, flags);

	memset(hwtstamps, 0, sizeof(*hwtstamps));
	hwtstamps->hwtstamp = ns_to_ktime(ns);
}

/**
 * e1000e_rx_hwtstamp - utility function which checks for Rx time stamp
 * @adapter: board private structure
 * @status: descriptor extended error and status field
 * @skb: particular skb to include time stamp
 *
 * If the time stamp is valid, convert it into the timecounter ns value
 * and store that result into the shhwtstamps structure which is passed
 * up the network stack.
 **/
static void e1000e_rx_hwtstamp(struct e1000_adapter *adapter, u32 status,
			       struct sk_buff *skb)
{
	struct e1000_hw *hw = &adapter->hw;
	u64 rxstmp;

	if (!(adapter->flags & FLAG_HAS_HW_TIMESTAMP) ||
	    !(status & E1000_RXDEXT_STATERR_TST) ||
	    !(er32(TSYNCRXCTL) & E1000_TSYNCRXCTL_VALID))
		return;

	/* The Rx time stamp registers contain the time stamp.  No other
	 * received packet will be time stamped until the Rx time stamp
	 * registers are read.  Because only one packet can be time stamped
	 * at a time, the register values must belong to this packet and
	 * therefore none of the other additional attributes need to be
	 * compared.
	 */
	rxstmp = (u64)er32(RXSTMPL);
	rxstmp |= (u64)er32(RXSTMPH) << 32;
	e1000e_systim_to_hwtstamp(adapter, skb_hwtstamps(skb), rxstmp);

	adapter->flags2 &= ~FLAG2_CHECK_RX_HWTSTAMP;
}

/**
 * e1000_receive_skb - helper function to handle Rx indications
 * @adapter: board private structure
 * @staterr: descriptor extended error and status field as written by hardware
 * @vlan: descriptor vlan field as written by hardware (no le/be conversion)
 * @skb: pointer to sk_buff to be indicated to stack
 **/
static void e1000_receive_skb(struct e1000_adapter *adapter,
			      struct net_device *netdev, struct sk_buff *skb,
			      u32 staterr, __le16 vlan)
{
	u16 tag = le16_to_cpu(vlan);

	e1000e_rx_hwtstamp(adapter, staterr, skb);

	skb->protocol = eth_type_trans(skb, netdev);

	if (staterr & E1000_RXD_STAT_VP)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), tag);

	napi_gro_receive(&adapter->napi, skb);
}

/**
 * e1000_rx_checksum - Receive Checksum Offload
 * @adapter: board private structure
 * @status_err: receive descriptor status and error fields
 * @csum: receive descriptor csum field
 * @sk_buff: socket buffer with received data
 **/
static void e1000_rx_checksum(struct e1000_adapter *adapter, u32 status_err,
			      struct sk_buff *skb)
{
	u16 status = (u16)status_err;
	u8 errors = (u8)(status_err >> 24);

	skb_checksum_none_assert(skb);

	/* Rx checksum disabled */
	if (!(adapter->netdev->features & NETIF_F_RXCSUM))
		return;

	/* Ignore Checksum bit is set */
	if (status & E1000_RXD_STAT_IXSM)
		return;

	/* TCP/UDP checksum error bit or IP checksum error bit is set */
	if (errors & (E1000_RXD_ERR_TCPE | E1000_RXD_ERR_IPE)) {
		/* let the stack verify checksum errors */
		adapter->hw_csum_err++;
		return;
	}

	/* TCP/UDP Checksum has not been calculated */
	if (!(status & (E1000_RXD_STAT_TCPCS | E1000_RXD_STAT_UDPCS)))
		return;

	/* It must be a TCP or UDP packet with a valid checksum */
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	adapter->hw_csum_good++;
}

static void e1000e_update_rdt_wa(struct e1000_ring *rx_ring, unsigned int i)
{
	struct e1000_adapter *adapter = rx_ring->adapter;
	struct e1000_hw *hw = &adapter->hw;
	s32 ret_val = __ew32_prepare(hw);

	writel(i, rx_ring->tail);

	if (unlikely(!ret_val && (i != readl(rx_ring->tail)))) {
		u32 rctl = er32(RCTL);

		ew32(RCTL, rctl & ~E1000_RCTL_EN);
		e_err("ME firmware caused invalid RDT - resetting\n");
		schedule_work(&adapter->reset_task);
	}
}

static void e1000e_update_tdt_wa(struct e1000_ring *tx_ring, unsigned int i)
{
	struct e1000_adapter *adapter = tx_ring->adapter;
	struct e1000_hw *hw = &adapter->hw;
	s32 ret_val = __ew32_prepare(hw);

	writel(i, tx_ring->tail);

	if (unlikely(!ret_val && (i != readl(tx_ring->tail)))) {
		u32 tctl = er32(TCTL);

		ew32(TCTL, tctl & ~E1000_TCTL_EN);
		e_err("ME firmware caused invalid TDT - resetting\n");
		schedule_work(&adapter->reset_task);
	}
}

/**
 * e1000_alloc_rx_buffers - Replace used receive buffers
 * @rx_ring: Rx descriptor ring
 **/
static void e1000_alloc_rx_buffers(struct e1000_ring *rx_ring,
				   int cleaned_count, gfp_t gfp)
{
	struct e1000_adapter *adapter = rx_ring->adapter;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union e1000_rx_desc_extended *rx_desc;
	struct e1000_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	unsigned int bufsz = adapter->rx_buffer_len;

	i = rx_ring->next_to_use;
	buffer_info = &rx_ring->buffer_info[i];

	while (cleaned_count--) {
		skb = buffer_info->skb;
		if (skb) {
			skb_trim(skb, 0);
			goto map_skb;
		}

		skb = __netdev_alloc_skb_ip_align(netdev, bufsz, gfp);
		if (!skb) {
			/* Better luck next round */
			adapter->alloc_rx_buff_failed++;
			break;
		}

		buffer_info->skb = skb;
map_skb:
		buffer_info->dma = dma_map_single(&pdev->dev, skb->data,
						  adapter->rx_buffer_len,
						  DMA_FROM_DEVICE);
		if (dma_mapping_error(&pdev->dev, buffer_info->dma)) {
			dev_err(&pdev->dev, "Rx DMA map failed\n");
			adapter->rx_dma_failed++;
			break;
		}

		rx_desc = E1000_RX_DESC_EXT(*rx_ring, i);
		rx_desc->read.buffer_addr = cpu_to_le64(buffer_info->dma);

		if (unlikely(!(i & (E1000_RX_BUFFER_WRITE - 1)))) {
			/* Force memory writes to complete before letting h/w
			 * know there are new descriptors to fetch.  (Only
			 * applicable for weak-ordered memory model archs,
			 * such as IA-64).
			 */
			wmb();
			if (adapter->flags2 & FLAG2_PCIM2PCI_ARBITER_WA)
				e1000e_update_rdt_wa(rx_ring, i);
			else
				writel(i, rx_ring->tail);
		}
		i++;
		if (i == rx_ring->count)
			i = 0;
		buffer_info = &rx_ring->buffer_info[i];
	}

	rx_ring->next_to_use = i;
}

/**
 * e1000_alloc_rx_buffers_ps - Replace used receive buffers; packet split
 * @rx_ring: Rx descriptor ring
 **/
static void e1000_alloc_rx_buffers_ps(struct e1000_ring *rx_ring,
				      int cleaned_count, gfp_t gfp)
{
	struct e1000_adapter *adapter = rx_ring->adapter;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union e1000_rx_desc_packet_split *rx_desc;
	struct e1000_buffer *buffer_info;
	struct e1000_ps_page *ps_page;
	struct sk_buff *skb;
	unsigned int i, j;

	i = rx_ring->next_to_use;
	buffer_info = &rx_ring->buffer_info[i];

	while (cleaned_count--) {
		rx_desc = E1000_RX_DESC_PS(*rx_ring, i);

		for (j = 0; j < PS_PAGE_BUFFERS; j++) {
			ps_page = &buffer_info->ps_pages[j];
			if (j >= adapter->rx_ps_pages) {
				/* all unused desc entries get hw null ptr */
				rx_desc->read.buffer_addr[j + 1] =
				    ~cpu_to_le64(0);
				continue;
			}
			if (!ps_page->page) {
				ps_page->page = alloc_page(gfp);
				if (!ps_page->page) {
					adapter->alloc_rx_buff_failed++;
					goto no_buffers;
				}
				ps_page->dma = dma_map_page(&pdev->dev,
							    ps_page->page,
							    0, PAGE_SIZE,
							    DMA_FROM_DEVICE);
				if (dma_mapping_error(&pdev->dev,
						      ps_page->dma)) {
					dev_err(&adapter->pdev->dev,
						"Rx DMA page map failed\n");
					adapter->rx_dma_failed++;
					goto no_buffers;
				}
			}
			/* Refresh the desc even if buffer_addrs
			 * didn't change because each write-back
			 * erases this info.
			 */
			rx_desc->read.buffer_addr[j + 1] =
			    cpu_to_le64(ps_page->dma);
		}

		skb = __netdev_alloc_skb_ip_align(netdev, adapter->rx_ps_bsize0,
						  gfp);

		if (!skb) {
			adapter->alloc_rx_buff_failed++;
			break;
		}

		buffer_info->skb = skb;
		buffer_info->dma = dma_map_single(&pdev->dev, skb->data,
						  adapter->rx_ps_bsize0,
						  DMA_FROM_DEVICE);
		if (dma_mapping_error(&pdev->dev, buffer_info->dma)) {
			dev_err(&pdev->dev, "Rx DMA map failed\n");
			adapter->rx_dma_failed++;
			/* cleanup skb */
			dev_kfree_skb_any(skb);
			buffer_info->skb = NULL;
			break;
		}

		rx_desc->read.buffer_addr[0] = cpu_to_le64(buffer_info->dma);

		if (unlikely(!(i & (E1000_RX_BUFFER_WRITE - 1)))) {
			/* Force memory writes to complete before letting h/w
			 * know there are new descriptors to fetch.  (Only
			 * applicable for weak-ordered memory model archs,
			 * such as IA-64).
			 */
			wmb();
			if (adapter->flags2 & FLAG2_PCIM2PCI_ARBITER_WA)
				e1000e_update_rdt_wa(rx_ring, i << 1);
			else
				writel(i << 1, rx_ring->tail);
		}

		i++;
		if (i == rx_ring->count)
			i = 0;
		buffer_info = &rx_ring->buffer_info[i];
	}

no_buffers:
	rx_ring->next_to_use = i;
}

/**
 * e1000_alloc_jumbo_rx_buffers - Replace used jumbo receive buffers
 * @rx_ring: Rx descriptor ring
 * @cleaned_count: number of buffers to allocate this pass
 **/

static void e1000_alloc_jumbo_rx_buffers(struct e1000_ring *rx_ring,
					 int cleaned_count, gfp_t gfp)
{
	struct e1000_adapter *adapter = rx_ring->adapter;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union e1000_rx_desc_extended *rx_desc;
	struct e1000_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	unsigned int bufsz = 256 - 16;	/* for skb_reserve */

	i = rx_ring->next_to_use;
	buffer_info = &rx_ring->buffer_info[i];

	while (cleaned_count--) {
		skb = buffer_info->skb;
		if (skb) {
			skb_trim(skb, 0);
			goto check_page;
		}

		skb = __netdev_alloc_skb_ip_align(netdev, bufsz, gfp);
		if (unlikely(!skb)) {
			/* Better luck next round */
			adapter->alloc_rx_buff_failed++;
			break;
		}

		buffer_info->skb = skb;
check_page:
		/* allocate a new page if necessary */
		if (!buffer_info->page) {
			buffer_info->page = alloc_page(gfp);
			if (unlikely(!buffer_info->page)) {
				adapter->alloc_rx_buff_failed++;
				break;
			}
		}

		if (!buffer_info->dma) {
			buffer_info->dma = dma_map_page(&pdev->dev,
							buffer_info->page, 0,
							PAGE_SIZE,
							DMA_FROM_DEVICE);
			if (dma_mapping_error(&pdev->dev, buffer_info->dma)) {
				adapter->alloc_rx_buff_failed++;
				break;
			}
		}

		rx_desc = E1000_RX_DESC_EXT(*rx_ring, i);
		rx_desc->read.buffer_addr = cpu_to_le64(buffer_info->dma);

		if (unlikely(++i == rx_ring->count))
			i = 0;
		buffer_info = &rx_ring->buffer_info[i];
	}

	if (likely(rx_ring->next_to_use != i)) {
		rx_ring->next_to_use = i;
		if (unlikely(i-- == 0))
			i = (rx_ring->count - 1);

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();
		if (adapter->flags2 & FLAG2_PCIM2PCI_ARBITER_WA)
			e1000e_update_rdt_wa(rx_ring, i);
		else
			writel(i, rx_ring->tail);
	}
}

static inline void e1000_rx_hash(struct net_device *netdev, __le32 rss,
				 struct sk_buff *skb)
{
	if (netdev->features & NETIF_F_RXHASH)
		skb_set_hash(skb, le32_to_cpu(rss), PKT_HASH_TYPE_L3);
}

/**
 * e1000_clean_rx_irq - Send received data up the network stack
 * @rx_ring: Rx descriptor ring
 *
 * the return value indicates whether actual cleaning was done, there
 * is no guarantee that everything was cleaned
 **/
static bool e1000_clean_rx_irq(struct e1000_ring *rx_ring, int *work_done,
			       int work_to_do)
{
	struct e1000_adapter *adapter = rx_ring->adapter;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_hw *hw = &adapter->hw;
	union e1000_rx_desc_extended *rx_desc, *next_rxd;
	struct e1000_buffer *buffer_info, *next_buffer;
	u32 length, staterr;
	unsigned int i;
	int cleaned_count = 0;
	bool cleaned = false;
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;

	i = rx_ring->next_to_clean;
	rx_desc = E1000_RX_DESC_EXT(*rx_ring, i);
	staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
	buffer_info = &rx_ring->buffer_info[i];

	while (staterr & E1000_RXD_STAT_DD) {
		struct sk_buff *skb;

		if (*work_done >= work_to_do)
			break;
		(*work_done)++;
		dma_rmb();	/* read descriptor and rx_buffer_info after status DD */

		skb = buffer_info->skb;
		buffer_info->skb = NULL;

		prefetch(skb->data - NET_IP_ALIGN);

		i++;
		if (i == rx_ring->count)
			i = 0;
		next_rxd = E1000_RX_DESC_EXT(*rx_ring, i);
		prefetch(next_rxd);

		next_buffer = &rx_ring->buffer_info[i];

		cleaned = true;
		cleaned_count++;
		dma_unmap_single(&pdev->dev, buffer_info->dma,
				 adapter->rx_buffer_len, DMA_FROM_DEVICE);
		buffer_info->dma = 0;

		length = le16_to_cpu(rx_desc->wb.upper.length);

		/* !EOP means multiple descriptors were used to store a single
		 * packet, if that's the case we need to toss it.  In fact, we
		 * need to toss every packet with the EOP bit clear and the
		 * next frame that _does_ have the EOP bit set, as it is by
		 * definition only a frame fragment
		 */
		if (unlikely(!(staterr & E1000_RXD_STAT_EOP)))
			adapter->flags2 |= FLAG2_IS_DISCARDING;

		if (adapter->flags2 & FLAG2_IS_DISCARDING) {
			/* All receives must fit into a single buffer */
			e_dbg("Receive packet consumed multiple buffers\n");
			/* recycle */
			buffer_info->skb = skb;
			if (staterr & E1000_RXD_STAT_EOP)
				adapter->flags2 &= ~FLAG2_IS_DISCARDING;
			goto next_desc;
		}

		if (unlikely((staterr & E1000_RXDEXT_ERR_FRAME_ERR_MASK) &&
			     !(netdev->features & NETIF_F_RXALL))) {
			/* recycle */
			buffer_info->skb = skb;
			goto next_desc;
		}

		/* adjust length to remove Ethernet CRC */
		if (!(adapter->flags2 & FLAG2_CRC_STRIPPING)) {
			/* If configured to store CRC, don't subtract FCS,
			 * but keep the FCS bytes out of the total_rx_bytes
			 * counter
			 */
			if (netdev->features & NETIF_F_RXFCS)
				total_rx_bytes -= 4;
			else
				length -= 4;
		}

		total_rx_bytes += length;
		total_rx_packets++;

		/* code added for copybreak, this should improve
		 * performance for small packets with large amounts
		 * of reassembly being done in the stack
		 */
		if (length < copybreak) {
			struct sk_buff *new_skb =
				napi_alloc_skb(&adapter->napi, length);
			if (new_skb) {
				skb_copy_to_linear_data_offset(new_skb,
							       -NET_IP_ALIGN,
							       (skb->data -
								NET_IP_ALIGN),
							       (length +
								NET_IP_ALIGN));
				/* save the skb in buffer_info as good */
				buffer_info->skb = skb;
				skb = new_skb;
			}
			/* else just continue with the old one */
		}
		/* end copybreak code */
		skb_put(skb, length);

		/* Receive Checksum Offload */
		e1000_rx_checksum(adapter, staterr, skb);

		e1000_rx_hash(netdev, rx_desc->wb.lower.hi_dword.rss, skb);

		e1000_receive_skb(adapter, netdev, skb, staterr,
				  rx_desc->wb.upper.vlan);

next_desc:
		rx_desc->wb.upper.status_error &= cpu_to_le32(~0xFF);

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= E1000_RX_BUFFER_WRITE) {
			adapter->alloc_rx_buf(rx_ring, cleaned_count,
					      GFP_ATOMIC);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		buffer_info = next_buffer;

		staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
	}
	rx_ring->next_to_clean = i;

	cleaned_count = e1000_desc_unused(rx_ring);
	if (cleaned_count)
		adapter->alloc_rx_buf(rx_ring, cleaned_count, GFP_ATOMIC);

	adapter->total_rx_bytes += total_rx_bytes;
	adapter->total_rx_packets += total_rx_packets;
	return cleaned;
}

static void e1000_put_txbuf(struct e1000_ring *tx_ring,
			    struct e1000_buffer *buffer_info,
			    bool drop)
{
	struct e1000_adapter *adapter = tx_ring->adapter;

	if (buffer_info->dma) {
		if (buffer_info->mapped_as_page)
			dma_unmap_page(&adapter->pdev->dev, buffer_info->dma,
				       buffer_info->length, DMA_TO_DEVICE);
		else
			dma_unmap_single(&adapter->pdev->dev, buffer_info->dma,
					 buffer_info->length, DMA_TO_DEVICE);
		buffer_info->dma = 0;
	}
	if (buffer_info->skb) {
		if (drop)
			dev_kfree_skb_any(buffer_info->skb);
		else
			dev_consume_skb_any(buffer_info->skb);
		buffer_info->skb = NULL;
	}
	buffer_info->time_stamp = 0;
}

static void e1000_print_hw_hang(struct work_struct *work)
{
	struct e1000_adapter *adapter = container_of(work,
						     struct e1000_adapter,
						     print_hang_task);
	struct net_device *netdev = adapter->netdev;
	struct e1000_ring *tx_ring = adapter->tx_ring;
	unsigned int i = tx_ring->next_to_clean;
	unsigned int eop = tx_ring->buffer_info[i].next_to_watch;
	struct e1000_tx_desc *eop_desc = E1000_TX_DESC(*tx_ring, eop);
	struct e1000_hw *hw = &adapter->hw;
	u16 phy_status, phy_1000t_status, phy_ext_status;
	u16 pci_status;

	if (test_bit(__E1000_DOWN, &adapter->state))
		return;

	if (!adapter->tx_hang_recheck && (adapter->flags2 & FLAG2_DMA_BURST)) {
		/* May be block on write-back, flush and detect again
		 * flush pending descriptor writebacks to memory
		 */
		ew32(TIDV, adapter->tx_int_delay | E1000_TIDV_FPD);
		/* execute the writes immediately */
		e1e_flush();
		/* Due to rare timing issues, write to TIDV again to ensure
		 * the write is successful
		 */
		ew32(TIDV, adapter->tx_int_delay | E1000_TIDV_FPD);
		/* execute the writes immediately */
		e1e_flush();
		adapter->tx_hang_recheck = true;
		return;
	}
	adapter->tx_hang_recheck = false;

	if (er32(TDH(0)) == er32(TDT(0))) {
		e_dbg("false hang detected, ignoring\n");
		return;
	}

	/* Real hang detected */
	netif_stop_queue(netdev);

	e1e_rphy(hw, MII_BMSR, &phy_status);
	e1e_rphy(hw, MII_STAT1000, &phy_1000t_status);
	e1e_rphy(hw, MII_ESTATUS, &phy_ext_status);

	pci_read_config_word(adapter->pdev, PCI_STATUS, &pci_status);

	/* detected Hardware unit hang */
	e_err("Detected Hardware Unit Hang:\n"
	      "  TDH                  <%x>\n"
	      "  TDT                  <%x>\n"
	      "  next_to_use          <%x>\n"
	      "  next_to_clean        <%x>\n"
	      "buffer_info[next_to_clean]:\n"
	      "  time_stamp           <%lx>\n"
	      "  next_to_watch        <%x>\n"
	      "  jiffies              <%lx>\n"
	      "  next_to_watch.status <%x>\n"
	      "MAC Status             <%x>\n"
	      "PHY Status             <%x>\n"
	      "PHY 1000BASE-T Status  <%x>\n"
	      "PHY Extended Status    <%x>\n"
	      "PCI Status             <%x>\n",
	      readl(tx_ring->head), readl(tx_ring->tail), tx_ring->next_to_use,
	      tx_ring->next_to_clean, tx_ring->buffer_info[eop].time_stamp,
	      eop, jiffies, eop_desc->upper.fields.status, er32(STATUS),
	      phy_status, phy_1000t_status, phy_ext_status, pci_status);

	e1000e_dump(adapter);

	/* Suggest workaround for known h/w issue */
	if ((hw->mac.type == e1000_pchlan) && (er32(CTRL) & E1000_CTRL_TFCE))
		e_err("Try turning off Tx pause (flow control) via ethtool\n");
}

/**
 * e1000e_tx_hwtstamp_work - check for Tx time stamp
 * @work: pointer to work struct
 *
 * This work function polls the TSYNCTXCTL valid bit to determine when a
 * timestamp has been taken for the current stored skb.  The timestamp must
 * be for this skb because only one such packet is allowed in the queue.
 */
static void e1000e_tx_hwtstamp_work(struct work_struct *work)
{
	struct e1000_adapter *adapter = container_of(work, struct e1000_adapter,
						     tx_hwtstamp_work);
	struct e1000_hw *hw = &adapter->hw;

	if (er32(TSYNCTXCTL) & E1000_TSYNCTXCTL_VALID) {
		struct sk_buff *skb = adapter->tx_hwtstamp_skb;
		struct skb_shared_hwtstamps shhwtstamps;
		u64 txstmp;

		txstmp = er32(TXSTMPL);
		txstmp |= (u64)er32(TXSTMPH) << 32;

		e1000e_systim_to_hwtstamp(adapter, &shhwtstamps, txstmp);

		/* Clear the global tx_hwtstamp_skb pointer and force writes
		 * prior to notifying the stack of a Tx timestamp.
		 */
		adapter->tx_hwtstamp_skb = NULL;
		wmb(); /* force write prior to skb_tstamp_tx */

		skb_tstamp_tx(skb, &shhwtstamps);
		dev_consume_skb_any(skb);
	} else if (time_after(jiffies, adapter->tx_hwtstamp_start
			      + adapter->tx_timeout_factor * HZ)) {
		dev_kfree_skb_any(adapter->tx_hwtstamp_skb);
		adapter->tx_hwtstamp_skb = NULL;
		adapter->tx_hwtstamp_timeouts++;
		e_warn("clearing Tx timestamp hang\n");
	} else {
		/* reschedule to check later */
		schedule_work(&adapter->tx_hwtstamp_work);
	}
}

/**
 * e1000_clean_tx_irq - Reclaim resources after transmit completes
 * @tx_ring: Tx descriptor ring
 *
 * the return value indicates whether actual cleaning was done, there
 * is no guarantee that everything was cleaned
 **/
static bool e1000_clean_tx_irq(struct e1000_ring *tx_ring)
{
	struct e1000_adapter *adapter = tx_ring->adapter;
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_tx_desc *tx_desc, *eop_desc;
	struct e1000_buffer *buffer_info;
	unsigned int i, eop;
	unsigned int count = 0;
	unsigned int total_tx_bytes = 0, total_tx_packets = 0;
	unsigned int bytes_compl = 0, pkts_compl = 0;

	i = tx_ring->next_to_clean;
	eop = tx_ring->buffer_info[i].next_to_watch;
	eop_desc = E1000_TX_DESC(*tx_ring, eop);

	while ((eop_desc->upper.data & cpu_to_le32(E1000_TXD_STAT_DD)) &&
	       (count < tx_ring->count)) {
		bool cleaned = false;

		dma_rmb();		/* read buffer_info after eop_desc */
		for (; !cleaned; count++) {
			tx_desc = E1000_TX_DESC(*tx_ring, i);
			buffer_info = &tx_ring->buffer_info[i];
			cleaned = (i == eop);

			if (cleaned) {
				total_tx_packets += buffer_info->segs;
				total_tx_bytes += buffer_info->bytecount;
				if (buffer_info->skb) {
					bytes_compl += buffer_info->skb->len;
					pkts_compl++;
				}
			}

			e1000_put_txbuf(tx_ring, buffer_info, false);
			tx_desc->upper.data = 0;

			i++;
			if (i == tx_ring->count)
				i = 0;
		}

		if (i == tx_ring->next_to_use)
			break;
		eop = tx_ring->buffer_info[i].next_to_watch;
		eop_desc = E1000_TX_DESC(*tx_ring, eop);
	}

	tx_ring->next_to_clean = i;

	netdev_completed_queue(netdev, pkts_compl, bytes_compl);

#define TX_WAKE_THRESHOLD 32
	if (count && netif_carrier_ok(netdev) &&
	    e1000_desc_unused(tx_ring) >= TX_WAKE_THRESHOLD) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();

		if (netif_queue_stopped(netdev) &&
		    !(test_bit(__E1000_DOWN, &adapter->state))) {
			netif_wake_queue(netdev);
			++adapter->restart_queue;
		}
	}

	if (adapter->detect_tx_hung) {
		/* Detect a transmit hang in hardware, this serializes the
		 * check with the clearing of time_stamp and movement of i
		 */
		adapter->detect_tx_hung = false;
		if (tx_ring->buffer_info[i].time_stamp &&
		    time_after(jiffies, tx_ring->buffer_info[i].time_stamp
			       + (adapter->tx_timeout_factor * HZ)) &&
		    !(er32(STATUS) & E1000_STATUS_TXOFF))
			schedule_work(&adapter->print_hang_task);
		else
			adapter->tx_hang_recheck = false;
	}
	adapter->total_tx_bytes += total_tx_bytes;
	adapter->total_tx_packets += total_tx_packets;
	return count < tx_ring->count;
}

/**
 * e1000_clean_rx_irq_ps - Send received data up the network stack; packet split
 * @rx_ring: Rx descriptor ring
 *
 * the return value indicates whether actual cleaning was done, there
 * is no guarantee that everything was cleaned
 **/
static bool e1000_clean_rx_irq_ps(struct e1000_ring *rx_ring, int *work_done,
				  int work_to_do)
{
	struct e1000_adapter *adapter = rx_ring->adapter;
	struct e1000_hw *hw = &adapter->hw;
	union e1000_rx_desc_packet_split *rx_desc, *next_rxd;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_buffer *buffer_info, *next_buffer;
	struct e1000_ps_page *ps_page;
	struct sk_buff *skb;
	unsigned int i, j;
	u32 length, staterr;
	int cleaned_count = 0;
	bool cleaned = false;
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;

	i = rx_ring->next_to_clean;
	rx_desc = E1000_RX_DESC_PS(*rx_ring, i);
	staterr = le32_to_cpu(rx_desc->wb.middle.status_error);
	buffer_info = &rx_ring->buffer_info[i];

	while (staterr & E1000_RXD_STAT_DD) {
		if (*work_done >= work_to_do)
			break;
		(*work_done)++;
		skb = buffer_info->skb;
		dma_rmb();	/* read descriptor and rx_buffer_info after status DD */

		/* in the packet split case this is header only */
		prefetch(skb->data - NET_IP_ALIGN);

		i++;
		if (i == rx_ring->count)
			i = 0;
		next_rxd = E1000_RX_DESC_PS(*rx_ring, i);
		prefetch(next_rxd);

		next_buffer = &rx_ring->buffer_info[i];

		cleaned = true;
		cleaned_count++;
		dma_unmap_single(&pdev->dev, buffer_info->dma,
				 adapter->rx_ps_bsize0, DMA_FROM_DEVICE);
		buffer_info->dma = 0;

		/* see !EOP comment in other Rx routine */
		if (!(staterr & E1000_RXD_STAT_EOP))
			adapter->flags2 |= FLAG2_IS_DISCARDING;

		if (adapter->flags2 & FLAG2_IS_DISCARDING) {
			e_dbg("Packet Split buffers didn't pick up the full packet\n");
			dev_kfree_skb_irq(skb);
			if (staterr & E1000_RXD_STAT_EOP)
				adapter->flags2 &= ~FLAG2_IS_DISCARDING;
			goto next_desc;
		}

		if (unlikely((staterr & E1000_RXDEXT_ERR_FRAME_ERR_MASK) &&
			     !(netdev->features & NETIF_F_RXALL))) {
			dev_kfree_skb_irq(skb);
			goto next_desc;
		}

		length = le16_to_cpu(rx_desc->wb.middle.length0);

		if (!length) {
			e_dbg("Last part of the packet spanning multiple descriptors\n");
			dev_kfree_skb_irq(skb);
			goto next_desc;
		}

		/* Good Receive */
		skb_put(skb, length);

		{
			/* this looks ugly, but it seems compiler issues make
			 * it more efficient than reusing j
			 */
			int l1 = le16_to_cpu(rx_desc->wb.upper.length[0]);

			/* page alloc/put takes too long and effects small
			 * packet throughput, so unsplit small packets and
			 * save the alloc/put only valid in softirq (napi)
			 * context to call kmap_*
			 */
			if (l1 && (l1 <= copybreak) &&
			    ((length + l1) <= adapter->rx_ps_bsize0)) {
				u8 *vaddr;

				ps_page = &buffer_info->ps_pages[0];

				/* there is no documentation about how to call
				 * kmap_atomic, so we can't hold the mapping
				 * very long
				 */
				dma_sync_single_for_cpu(&pdev->dev,
							ps_page->dma,
							PAGE_SIZE,
							DMA_FROM_DEVICE);
				vaddr = kmap_atomic(ps_page->page);
				memcpy(skb_tail_pointer(skb), vaddr, l1);
				kunmap_atomic(vaddr);
				dma_sync_single_for_device(&pdev->dev,
							   ps_page->dma,
							   PAGE_SIZE,
							   DMA_FROM_DEVICE);

				/* remove the CRC */
				if (!(adapter->flags2 & FLAG2_CRC_STRIPPING)) {
					if (!(netdev->features & NETIF_F_RXFCS))
						l1 -= 4;
				}

				skb_put(skb, l1);
				goto copydone;
			}	/* if */
		}

		for (j = 0; j < PS_PAGE_BUFFERS; j++) {
			length = le16_to_cpu(rx_desc->wb.upper.length[j]);
			if (!length)
				break;

			ps_page = &buffer_info->ps_pages[j];
			dma_unmap_page(&pdev->dev, ps_page->dma, PAGE_SIZE,
				       DMA_FROM_DEVICE);
			ps_page->dma = 0;
			skb_fill_page_desc(skb, j, ps_page->page, 0, length);
			ps_page->page = NULL;
			skb->len += length;
			skb->data_len += length;
			skb->truesize += PAGE_SIZE;
		}

		/* strip the ethernet crc, problem is we're using pages now so
		 * this whole operation can get a little cpu intensive
		 */
		if (!(adapter->flags2 & FLAG2_CRC_STRIPPING)) {
			if (!(netdev->features & NETIF_F_RXFCS))
				pskb_trim(skb, skb->len - 4);
		}

copydone:
		total_rx_bytes += skb->len;
		total_rx_packets++;

		e1000_rx_checksum(adapter, staterr, skb);

		e1000_rx_hash(netdev, rx_desc->wb.lower.hi_dword.rss, skb);

		if (rx_desc->wb.upper.header_status &
		    cpu_to_le16(E1000_RXDPS_HDRSTAT_HDRSP))
			adapter->rx_hdr_split++;

		e1000_receive_skb(adapter, netdev, skb, staterr,
				  rx_desc->wb.middle.vlan);

next_desc:
		rx_desc->wb.middle.status_error &= cpu_to_le32(~0xFF);
		buffer_info->skb = NULL;

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= E1000_RX_BUFFER_WRITE) {
			adapter->alloc_rx_buf(rx_ring, cleaned_count,
					      GFP_ATOMIC);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		buffer_info = next_buffer;

		staterr = le32_to_cpu(rx_desc->wb.middle.status_error);
	}
	rx_ring->next_to_clean = i;

	cleaned_count = e1000_desc_unused(rx_ring);
	if (cleaned_count)
		adapter->alloc_rx_buf(rx_ring, cleaned_count, GFP_ATOMIC);

	adapter->total_rx_bytes += total_rx_bytes;
	adapter->total_rx_packets += total_rx_packets;
	return cleaned;
}

/**
 * e1000_consume_page - helper function
 **/
static void e1000_consume_page(struct e1000_buffer *bi, struct sk_buff *skb,
			       u16 length)
{
	bi->page = NULL;
	skb->len += length;
	skb->data_len += length;
	skb->truesize += PAGE_SIZE;
}

/**
 * e1000_clean_jumbo_rx_irq - Send received data up the network stack; legacy
 * @adapter: board private structure
 *
 * the return value indicates whether actual cleaning was done, there
 * is no guarantee that everything was cleaned
 **/
static bool e1000_clean_jumbo_rx_irq(struct e1000_ring *rx_ring, int *work_done,
				     int work_to_do)
{
	struct e1000_adapter *adapter = rx_ring->adapter;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	union e1000_rx_desc_extended *rx_desc, *next_rxd;
	struct e1000_buffer *buffer_info, *next_buffer;
	u32 length, staterr;
	unsigned int i;
	int cleaned_count = 0;
	bool cleaned = false;
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;
	struct skb_shared_info *shinfo;

	i = rx_ring->next_to_clean;
	rx_desc = E1000_RX_DESC_EXT(*rx_ring, i);
	staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
	buffer_info = &rx_ring->buffer_info[i];

	while (staterr & E1000_RXD_STAT_DD) {
		struct sk_buff *skb;

		if (*work_done >= work_to_do)
			break;
		(*work_done)++;
		dma_rmb();	/* read descriptor and rx_buffer_info after status DD */

		skb = buffer_info->skb;
		buffer_info->skb = NULL;

		++i;
		if (i == rx_ring->count)
			i = 0;
		next_rxd = E1000_RX_DESC_EXT(*rx_ring, i);
		prefetch(next_rxd);

		next_buffer = &rx_ring->buffer_info[i];

		cleaned = true;
		cleaned_count++;
		dma_unmap_page(&pdev->dev, buffer_info->dma, PAGE_SIZE,
			       DMA_FROM_DEVICE);
		buffer_info->dma = 0;

		length = le16_to_cpu(rx_desc->wb.upper.length);

		/* errors is only valid for DD + EOP descriptors */
		if (unlikely((staterr & E1000_RXD_STAT_EOP) &&
			     ((staterr & E1000_RXDEXT_ERR_FRAME_ERR_MASK) &&
			      !(netdev->features & NETIF_F_RXALL)))) {
			/* recycle both page and skb */
			buffer_info->skb = skb;
			/* an error means any chain goes out the window too */
			if (rx_ring->rx_skb_top)
				dev_kfree_skb_irq(rx_ring->rx_skb_top);
			rx_ring->rx_skb_top = NULL;
			goto next_desc;
		}
#define rxtop (rx_ring->rx_skb_top)
		if (!(staterr & E1000_RXD_STAT_EOP)) {
			/* this descriptor is only the beginning (or middle) */
			if (!rxtop) {
				/* this is the beginning of a chain */
				rxtop = skb;
				skb_fill_page_desc(rxtop, 0, buffer_info->page,
						   0, length);
			} else {
				/* this is the middle of a chain */
				shinfo = skb_shinfo(rxtop);
				skb_fill_page_desc(rxtop, shinfo->nr_frags,
						   buffer_info->page, 0,
						   length);
				/* re-use the skb, only consumed the page */
				buffer_info->skb = skb;
			}
			e1000_consume_page(buffer_info, rxtop, length);
			goto next_desc;
		} else {
			if (rxtop) {
				/* end of the chain */
				shinfo = skb_shinfo(rxtop);
				skb_fill_page_desc(rxtop, shinfo->nr_frags,
						   buffer_info->page, 0,
						   length);
				/* re-use the current skb, we only consumed the
				 * page
				 */
				buffer_info->skb = skb;
				skb = rxtop;
				rxtop = NULL;
				e1000_consume_page(buffer_info, skb, length);
			} else {
				/* no chain, got EOP, this buf is the packet
				 * copybreak to save the put_page/alloc_page
				 */
				if (length <= copybreak &&
				    skb_tailroom(skb) >= length) {
					u8 *vaddr;
					vaddr = kmap_atomic(buffer_info->page);
					memcpy(skb_tail_pointer(skb), vaddr,
					       length);
					kunmap_atomic(vaddr);
					/* re-use the page, so don't erase
					 * buffer_info->page
					 */
					skb_put(skb, length);
				} else {
					skb_fill_page_desc(skb, 0,
							   buffer_info->page, 0,
							   length);
					e1000_consume_page(buffer_info, skb,
							   length);
				}
			}
		}

		/* Receive Checksum Offload */
		e1000_rx_checksum(adapter, staterr, skb);

		e1000_rx_hash(netdev, rx_desc->wb.lower.hi_dword.rss, skb);

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += skb->len;
		total_rx_packets++;

		/* eth type trans needs skb->data to point to something */
		if (!pskb_may_pull(skb, ETH_HLEN)) {
			e_err("pskb_may_pull failed.\n");
			dev_kfree_skb_irq(skb);
			goto next_desc;
		}

		e1000_receive_skb(adapter, netdev, skb, staterr,
				  rx_desc->wb.upper.vlan);

next_desc:
		rx_desc->wb.upper.status_error &= cpu_to_le32(~0xFF);

		/* return some buffers to hardware, one at a time is too slow */
		if (unlikely(cleaned_count >= E1000_RX_BUFFER_WRITE)) {
			adapter->alloc_rx_buf(rx_ring, cleaned_count,
					      GFP_ATOMIC);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		buffer_info = next_buffer;

		staterr = le32_to_cpu(rx_desc->wb.upper.status_error);
	}
	rx_ring->next_to_clean = i;

	cleaned_count = e1000_desc_unused(rx_ring);
	if (cleaned_count)
		adapter->alloc_rx_buf(rx_ring, cleaned_count, GFP_ATOMIC);

	adapter->total_rx_bytes += total_rx_bytes;
	adapter->total_rx_packets += total_rx_packets;
	return cleaned;
}

/**
 * e1000_clean_rx_ring - Free Rx Buffers per Queue
 * @rx_ring: Rx descriptor ring
 **/
static void e1000_clean_rx_ring(struct e1000_ring *rx_ring)
{
	struct e1000_adapter *adapter = rx_ring->adapter;
	struct e1000_buffer *buffer_info;
	struct e1000_ps_page *ps_page;
	struct pci_dev *pdev = adapter->pdev;
	unsigned int i, j;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		buffer_info = &rx_ring->buffer_info[i];
		if (buffer_info->dma) {
			if (adapter->clean_rx == e1000_clean_rx_irq)
				dma_unmap_single(&pdev->dev, buffer_info->dma,
						 adapter->rx_buffer_len,
						 DMA_FROM_DEVICE);
			else if (adapter->clean_rx == e1000_clean_jumbo_rx_irq)
				dma_unmap_page(&pdev->dev, buffer_info->dma,
					       PAGE_SIZE, DMA_FROM_DEVICE);
			else if (adapter->clean_rx == e1000_clean_rx_irq_ps)
				dma_unmap_single(&pdev->dev, buffer_info->dma,
						 adapter->rx_ps_bsize0,
						 DMA_FROM_DEVICE);
			buffer_info->dma = 0;
		}

		if (buffer_info->page) {
			put_page(buffer_info->page);
			buffer_info->page = NULL;
		}

		if (buffer_info->skb) {
			dev_kfree_skb(buffer_info->skb);
			buffer_info->skb = NULL;
		}

		for (j = 0; j < PS_PAGE_BUFFERS; j++) {
			ps_page = &buffer_info->ps_pages[j];
			if (!ps_page->page)
				break;
			dma_unmap_page(&pdev->dev, ps_page->dma, PAGE_SIZE,
				       DMA_FROM_DEVICE);
			ps_page->dma = 0;
			put_page(ps_page->page);
			ps_page->page = NULL;
		}
	}

	/* there also may be some cached data from a chained receive */
	if (rx_ring->rx_skb_top) {
		dev_kfree_skb(rx_ring->rx_skb_top);
		rx_ring->rx_skb_top = NULL;
	}

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
	adapter->flags2 &= ~FLAG2_IS_DISCARDING;
}

static void e1000e_downshift_workaround(struct work_struct *work)
{
	struct e1000_adapter *adapter = container_of(work,
						     struct e1000_adapter,
						     downshift_task);

	if (test_bit(__E1000_DOWN, &adapter->state))
		return;

	e1000e_gig_downshift_workaround_ich8lan(&adapter->hw);
}

/**
 * e1000_intr_msi - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/
static irqreturn_t e1000_intr_msi(int __always_unused irq, void *data)
{
	struct net_device *netdev = data;
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 icr = er32(ICR);

	/* read ICR disables interrupts using IAM */
	if (icr & E1000_ICR_LSC) {
		hw->mac.get_link_status = true;
		/* ICH8 workaround-- Call gig speed drop workaround on cable
		 * disconnect (LSC) before accessing any PHY registers
		 */
		if ((adapter->flags & FLAG_LSC_GIG_SPEED_DROP) &&
		    (!(er32(STATUS) & E1000_STATUS_LU)))
			schedule_work(&adapter->downshift_task);

		/* 80003ES2LAN workaround-- For packet buffer work-around on
		 * link down event; disable receives here in the ISR and reset
		 * adapter in watchdog
		 */
		if (netif_carrier_ok(netdev) &&
		    adapter->flags & FLAG_RX_NEEDS_RESTART) {
			/* disable receives */
			u32 rctl = er32(RCTL);

			ew32(RCTL, rctl & ~E1000_RCTL_EN);
			adapter->flags |= FLAG_RESTART_NOW;
		}
		/* guard against interrupt when we're going down */
		if (!test_bit(__E1000_DOWN, &adapter->state))
			mod_timer(&adapter->watchdog_timer, jiffies + 1);
	}

	/* Reset on uncorrectable ECC error */
	if ((icr & E1000_ICR_ECCER) && (hw->mac.type >= e1000_pch_lpt)) {
		u32 pbeccsts = er32(PBECCSTS);

		adapter->corr_errors +=
		    pbeccsts & E1000_PBECCSTS_CORR_ERR_CNT_MASK;
		adapter->uncorr_errors +=
		    (pbeccsts & E1000_PBECCSTS_UNCORR_ERR_CNT_MASK) >>
		    E1000_PBECCSTS_UNCORR_ERR_CNT_SHIFT;

		/* Do the reset outside of interrupt context */
		schedule_work(&adapter->reset_task);

		/* return immediately since reset is imminent */
		return IRQ_HANDLED;
	}

	if (napi_schedule_prep(&adapter->napi)) {
		adapter->total_tx_bytes = 0;
		adapter->total_tx_packets = 0;
		adapter->total_rx_bytes = 0;
		adapter->total_rx_packets = 0;
		__napi_schedule(&adapter->napi);
	}

	return IRQ_HANDLED;
}

/**
 * e1000_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/
static irqreturn_t e1000_intr(int __always_unused irq, void *data)
{
	struct net_device *netdev = data;
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl, icr = er32(ICR);

	if (!icr || test_bit(__E1000_DOWN, &adapter->state))
		return IRQ_NONE;	/* Not our interrupt */

	/* IMS will not auto-mask if INT_ASSERTED is not set, and if it is
	 * not set, then the adapter didn't send an interrupt
	 */
	if (!(icr & E1000_ICR_INT_ASSERTED))
		return IRQ_NONE;

	/* Interrupt Auto-Mask...upon reading ICR,
	 * interrupts are masked.  No need for the
	 * IMC write
	 */

	if (icr & E1000_ICR_LSC) {
		hw->mac.get_link_status = true;
		/* ICH8 workaround-- Call gig speed drop workaround on cable
		 * disconnect (LSC) before accessing any PHY registers
		 */
		if ((adapter->flags & FLAG_LSC_GIG_SPEED_DROP) &&
		    (!(er32(STATUS) & E1000_STATUS_LU)))
			schedule_work(&adapter->downshift_task);

		/* 80003ES2LAN workaround--
		 * For packet buffer work-around on link down event;
		 * disable receives here in the ISR and
		 * reset adapter in watchdog
		 */
		if (netif_carrier_ok(netdev) &&
		    (adapter->flags & FLAG_RX_NEEDS_RESTART)) {
			/* disable receives */
			rctl = er32(RCTL);
			ew32(RCTL, rctl & ~E1000_RCTL_EN);
			adapter->flags |= FLAG_RESTART_NOW;
		}
		/* guard against interrupt when we're going down */
		if (!test_bit(__E1000_DOWN, &adapter->state))
			mod_timer(&adapter->watchdog_timer, jiffies + 1);
	}

	/* Reset on uncorrectable ECC error */
	if ((icr & E1000_ICR_ECCER) && (hw->mac.type >= e1000_pch_lpt)) {
		u32 pbeccsts = er32(PBECCSTS);

		adapter->corr_errors +=
		    pbeccsts & E1000_PBECCSTS_CORR_ERR_CNT_MASK;
		adapter->uncorr_errors +=
		    (pbeccsts & E1000_PBECCSTS_UNCORR_ERR_CNT_MASK) >>
		    E1000_PBECCSTS_UNCORR_ERR_CNT_SHIFT;

		/* Do the reset outside of interrupt context */
		schedule_work(&adapter->reset_task);

		/* return immediately since reset is imminent */
		return IRQ_HANDLED;
	}

	if (napi_schedule_prep(&adapter->napi)) {
		adapter->total_tx_bytes = 0;
		adapter->total_tx_packets = 0;
		adapter->total_rx_bytes = 0;
		adapter->total_rx_packets = 0;
		__napi_schedule(&adapter->napi);
	}

	return IRQ_HANDLED;
}

static irqreturn_t e1000_msix_other(int __always_unused irq, void *data)
{
	struct net_device *netdev = data;
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 icr = er32(ICR);

	if (icr & adapter->eiac_mask)
		ew32(ICS, (icr & adapter->eiac_mask));

	if (icr & E1000_ICR_LSC) {
		hw->mac.get_link_status = true;
		/* guard against interrupt when we're going down */
		if (!test_bit(__E1000_DOWN, &adapter->state))
			mod_timer(&adapter->watchdog_timer, jiffies + 1);
	}

	if (!test_bit(__E1000_DOWN, &adapter->state))
		ew32(IMS, E1000_IMS_OTHER | IMS_OTHER_MASK);

	return IRQ_HANDLED;
}

static irqreturn_t e1000_intr_msix_tx(int __always_unused irq, void *data)
{
	struct net_device *netdev = data;
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_ring *tx_ring = adapter->tx_ring;

	adapter->total_tx_bytes = 0;
	adapter->total_tx_packets = 0;

	if (!e1000_clean_tx_irq(tx_ring))
		/* Ring was not completely cleaned, so fire another interrupt */
		ew32(ICS, tx_ring->ims_val);

	if (!test_bit(__E1000_DOWN, &adapter->state))
		ew32(IMS, adapter->tx_ring->ims_val);

	return IRQ_HANDLED;
}

static irqreturn_t e1000_intr_msix_rx(int __always_unused irq, void *data)
{
	struct net_device *netdev = data;
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_ring *rx_ring = adapter->rx_ring;

	/* Write the ITR value calculated at the end of the
	 * previous interrupt.
	 */
	if (rx_ring->set_itr) {
		u32 itr = rx_ring->itr_val ?
			  1000000000 / (rx_ring->itr_val * 256) : 0;

		writel(itr, rx_ring->itr_register);
		rx_ring->set_itr = 0;
	}

	if (napi_schedule_prep(&adapter->napi)) {
		adapter->total_rx_bytes = 0;
		adapter->total_rx_packets = 0;
		__napi_schedule(&adapter->napi);
	}
	return IRQ_HANDLED;
}

/**
 * e1000_configure_msix - Configure MSI-X hardware
 *
 * e1000_configure_msix sets up the hardware to properly
 * generate MSI-X interrupts.
 **/
static void e1000_configure_msix(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_ring *rx_ring = adapter->rx_ring;
	struct e1000_ring *tx_ring = adapter->tx_ring;
	int vector = 0;
	u32 ctrl_ext, ivar = 0;

	adapter->eiac_mask = 0;

	/* Workaround issue with spurious interrupts on 82574 in MSI-X mode */
	if (hw->mac.type == e1000_82574) {
		u32 rfctl = er32(RFCTL);

		rfctl |= E1000_RFCTL_ACK_DIS;
		ew32(RFCTL, rfctl);
	}

	/* Configure Rx vector */
	rx_ring->ims_val = E1000_IMS_RXQ0;
	adapter->eiac_mask |= rx_ring->ims_val;
	if (rx_ring->itr_val)
		writel(1000000000 / (rx_ring->itr_val * 256),
		       rx_ring->itr_register);
	else
		writel(1, rx_ring->itr_register);
	ivar = E1000_IVAR_INT_ALLOC_VALID | vector;

	/* Configure Tx vector */
	tx_ring->ims_val = E1000_IMS_TXQ0;
	vector++;
	if (tx_ring->itr_val)
		writel(1000000000 / (tx_ring->itr_val * 256),
		       tx_ring->itr_register);
	else
		writel(1, tx_ring->itr_register);
	adapter->eiac_mask |= tx_ring->ims_val;
	ivar |= ((E1000_IVAR_INT_ALLOC_VALID | vector) << 8);

	/* set vector for Other Causes, e.g. link changes */
	vector++;
	ivar |= ((E1000_IVAR_INT_ALLOC_VALID | vector) << 16);
	if (rx_ring->itr_val)
		writel(1000000000 / (rx_ring->itr_val * 256),
		       hw->hw_addr + E1000_EITR_82574(vector));
	else
		writel(1, hw->hw_addr + E1000_EITR_82574(vector));

	/* Cause Tx interrupts on every write back */
	ivar |= BIT(31);

	ew32(IVAR, ivar);

	/* enable MSI-X PBA support */
	ctrl_ext = er32(CTRL_EXT) & ~E1000_CTRL_EXT_IAME;
	ctrl_ext |= E1000_CTRL_EXT_PBA_CLR | E1000_CTRL_EXT_EIAME;
	ew32(CTRL_EXT, ctrl_ext);
	e1e_flush();
}

void e1000e_reset_interrupt_capability(struct e1000_adapter *adapter)
{
	if (adapter->msix_entries) {
		pci_disable_msix(adapter->pdev);
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
	} else if (adapter->flags & FLAG_MSI_ENABLED) {
		pci_disable_msi(adapter->pdev);
		adapter->flags &= ~FLAG_MSI_ENABLED;
	}
}

/**
 * e1000e_set_interrupt_capability - set MSI or MSI-X if supported
 *
 * Attempt to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
void e1000e_set_interrupt_capability(struct e1000_adapter *adapter)
{
	int err;
	int i;

	switch (adapter->int_mode) {
	case E1000E_INT_MODE_MSIX:
		if (adapter->flags & FLAG_HAS_MSIX) {
			adapter->num_vectors = 3; /* RxQ0, TxQ0 and other */
			adapter->msix_entries = kcalloc(adapter->num_vectors,
							sizeof(struct
							       msix_entry),
							GFP_KERNEL);
			if (adapter->msix_entries) {
				struct e1000_adapter *a = adapter;

				for (i = 0; i < adapter->num_vectors; i++)
					adapter->msix_entries[i].entry = i;

				err = pci_enable_msix_range(a->pdev,
							    a->msix_entries,
							    a->num_vectors,
							    a->num_vectors);
				if (err > 0)
					return;
			}
			/* MSI-X failed, so fall through and try MSI */
			e_err("Failed to initialize MSI-X interrupts.  Falling back to MSI interrupts.\n");
			e1000e_reset_interrupt_capability(adapter);
		}
		adapter->int_mode = E1000E_INT_MODE_MSI;
		/* Fall through */
	case E1000E_INT_MODE_MSI:
		if (!pci_enable_msi(adapter->pdev)) {
			adapter->flags |= FLAG_MSI_ENABLED;
		} else {
			adapter->int_mode = E1000E_INT_MODE_LEGACY;
			e_err("Failed to initialize MSI interrupts.  Falling back to legacy interrupts.\n");
		}
		/* Fall through */
	case E1000E_INT_MODE_LEGACY:
		/* Don't do anything; this is the system default */
		break;
	}

	/* store the number of vectors being used */
	adapter->num_vectors = 1;
}

/**
 * e1000_request_msix - Initialize MSI-X interrupts
 *
 * e1000_request_msix allocates MSI-X vectors and requests interrupts from the
 * kernel.
 **/
static int e1000_request_msix(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err = 0, vector = 0;

	if (strlen(netdev->name) < (IFNAMSIZ - 5))
		snprintf(adapter->rx_ring->name,
			 sizeof(adapter->rx_ring->name) - 1,
			 "%s-rx-0", netdev->name);
	else
		memcpy(adapter->rx_ring->name, netdev->name, IFNAMSIZ);
	err = request_irq(adapter->msix_entries[vector].vector,
			  e1000_intr_msix_rx, 0, adapter->rx_ring->name,
			  netdev);
	if (err)
		return err;
	adapter->rx_ring->itr_register = adapter->hw.hw_addr +
	    E1000_EITR_82574(vector);
	adapter->rx_ring->itr_val = adapter->itr;
	vector++;

	if (strlen(netdev->name) < (IFNAMSIZ - 5))
		snprintf(adapter->tx_ring->name,
			 sizeof(adapter->tx_ring->name) - 1,
			 "%s-tx-0", netdev->name);
	else
		memcpy(adapter->tx_ring->name, netdev->name, IFNAMSIZ);
	err = request_irq(adapter->msix_entries[vector].vector,
			  e1000_intr_msix_tx, 0, adapter->tx_ring->name,
			  netdev);
	if (err)
		return err;
	adapter->tx_ring->itr_register = adapter->hw.hw_addr +
	    E1000_EITR_82574(vector);
	adapter->tx_ring->itr_val = adapter->itr;
	vector++;

	err = request_irq(adapter->msix_entries[vector].vector,
			  e1000_msix_other, 0, netdev->name, netdev);
	if (err)
		return err;

	e1000_configure_msix(adapter);

	return 0;
}

/**
 * e1000_request_irq - initialize interrupts
 *
 * Attempts to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 **/
static int e1000_request_irq(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;

	if (adapter->msix_entries) {
		err = e1000_request_msix(adapter);
		if (!err)
			return err;
		/* fall back to MSI */
		e1000e_reset_interrupt_capability(adapter);
		adapter->int_mode = E1000E_INT_MODE_MSI;
		e1000e_set_interrupt_capability(adapter);
	}
	if (adapter->flags & FLAG_MSI_ENABLED) {
		err = request_irq(adapter->pdev->irq, e1000_intr_msi, 0,
				  netdev->name, netdev);
		if (!err)
			return err;

		/* fall back to legacy interrupt */
		e1000e_reset_interrupt_capability(adapter);
		adapter->int_mode = E1000E_INT_MODE_LEGACY;
	}

	err = request_irq(adapter->pdev->irq, e1000_intr, IRQF_SHARED,
			  netdev->name, netdev);
	if (err)
		e_err("Unable to allocate interrupt, Error: %d\n", err);

	return err;
}

static void e1000_free_irq(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (adapter->msix_entries) {
		int vector = 0;

		free_irq(adapter->msix_entries[vector].vector, netdev);
		vector++;

		free_irq(adapter->msix_entries[vector].vector, netdev);
		vector++;

		/* Other Causes interrupt vector */
		free_irq(adapter->msix_entries[vector].vector, netdev);
		return;
	}

	free_irq(adapter->pdev->irq, netdev);
}

/**
 * e1000_irq_disable - Mask off interrupt generation on the NIC
 **/
static void e1000_irq_disable(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	ew32(IMC, ~0);
	if (adapter->msix_entries)
		ew32(EIAC_82574, 0);
	e1e_flush();

	if (adapter->msix_entries) {
		int i;

		for (i = 0; i < adapter->num_vectors; i++)
			synchronize_irq(adapter->msix_entries[i].vector);
	} else {
		synchronize_irq(adapter->pdev->irq);
	}
}

/**
 * e1000_irq_enable - Enable default interrupt generation settings
 **/
static void e1000_irq_enable(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	if (adapter->msix_entries) {
		ew32(EIAC_82574, adapter->eiac_mask & E1000_EIAC_MASK_82574);
		ew32(IMS, adapter->eiac_mask | E1000_IMS_OTHER |
		     IMS_OTHER_MASK);
	} else if (hw->mac.type >= e1000_pch_lpt) {
		ew32(IMS, IMS_ENABLE_MASK | E1000_IMS_ECCER);
	} else {
		ew32(IMS, IMS_ENABLE_MASK);
	}
	e1e_flush();
}

/**
 * e1000e_get_hw_control - get control of the h/w from f/w
 * @adapter: address of board private structure
 *
 * e1000e_get_hw_control sets {CTRL_EXT|SWSM}:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that
 * the driver is loaded. For AMT version (only with 82573)
 * of the f/w this means that the network i/f is open.
 **/
void e1000e_get_hw_control(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl_ext;
	u32 swsm;

	/* Let firmware know the driver has taken over */
	if (adapter->flags & FLAG_HAS_SWSM_ON_LOAD) {
		swsm = er32(SWSM);
		ew32(SWSM, swsm | E1000_SWSM_DRV_LOAD);
	} else if (adapter->flags & FLAG_HAS_CTRLEXT_ON_LOAD) {
		ctrl_ext = er32(CTRL_EXT);
		ew32(CTRL_EXT, ctrl_ext | E1000_CTRL_EXT_DRV_LOAD);
	}
}

/**
 * e1000e_release_hw_control - release control of the h/w to f/w
 * @adapter: address of board private structure
 *
 * e1000e_release_hw_control resets {CTRL_EXT|SWSM}:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that the
 * driver is no longer loaded. For AMT version (only with 82573) i
 * of the f/w this means that the network i/f is closed.
 *
 **/
void e1000e_release_hw_control(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl_ext;
	u32 swsm;

	/* Let firmware taken over control of h/w */
	if (adapter->flags & FLAG_HAS_SWSM_ON_LOAD) {
		swsm = er32(SWSM);
		ew32(SWSM, swsm & ~E1000_SWSM_DRV_LOAD);
	} else if (adapter->flags & FLAG_HAS_CTRLEXT_ON_LOAD) {
		ctrl_ext = er32(CTRL_EXT);
		ew32(CTRL_EXT, ctrl_ext & ~E1000_CTRL_EXT_DRV_LOAD);
	}
}

/**
 * e1000_alloc_ring_dma - allocate memory for a ring structure
 **/
static int e1000_alloc_ring_dma(struct e1000_adapter *adapter,
				struct e1000_ring *ring)
{
	struct pci_dev *pdev = adapter->pdev;

	ring->desc = dma_zalloc_coherent(&pdev->dev, ring->size, &ring->dma,
					 GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	return 0;
}

/**
 * e1000e_setup_tx_resources - allocate Tx resources (Descriptors)
 * @tx_ring: Tx descriptor ring
 *
 * Return 0 on success, negative on failure
 **/
int e1000e_setup_tx_resources(struct e1000_ring *tx_ring)
{
	struct e1000_adapter *adapter = tx_ring->adapter;
	int err = -ENOMEM, size;

	size = sizeof(struct e1000_buffer) * tx_ring->count;
	tx_ring->buffer_info = vzalloc(size);
	if (!tx_ring->buffer_info)
		goto err;

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(struct e1000_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);

	err = e1000_alloc_ring_dma(adapter, tx_ring);
	if (err)
		goto err;

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	return 0;
err:
	vfree(tx_ring->buffer_info);
	e_err("Unable to allocate memory for the transmit descriptor ring\n");
	return err;
}

/**
 * e1000e_setup_rx_resources - allocate Rx resources (Descriptors)
 * @rx_ring: Rx descriptor ring
 *
 * Returns 0 on success, negative on failure
 **/
int e1000e_setup_rx_resources(struct e1000_ring *rx_ring)
{
	struct e1000_adapter *adapter = rx_ring->adapter;
	struct e1000_buffer *buffer_info;
	int i, size, desc_len, err = -ENOMEM;

	size = sizeof(struct e1000_buffer) * rx_ring->count;
	rx_ring->buffer_info = vzalloc(size);
	if (!rx_ring->buffer_info)
		goto err;

	for (i = 0; i < rx_ring->count; i++) {
		buffer_info = &rx_ring->buffer_info[i];
		buffer_info->ps_pages = kcalloc(PS_PAGE_BUFFERS,
						sizeof(struct e1000_ps_page),
						GFP_KERNEL);
		if (!buffer_info->ps_pages)
			goto err_pages;
	}

	desc_len = sizeof(union e1000_rx_desc_packet_split);

	/* Round up to nearest 4K */
	rx_ring->size = rx_ring->count * desc_len;
	rx_ring->size = ALIGN(rx_ring->size, 4096);

	err = e1000_alloc_ring_dma(adapter, rx_ring);
	if (err)
		goto err_pages;

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
	rx_ring->rx_skb_top = NULL;

	return 0;

err_pages:
	for (i = 0; i < rx_ring->count; i++) {
		buffer_info = &rx_ring->buffer_info[i];
		kfree(buffer_info->ps_pages);
	}
err:
	vfree(rx_ring->buffer_info);
	e_err("Unable to allocate memory for the receive descriptor ring\n");
	return err;
}

/**
 * e1000_clean_tx_ring - Free Tx Buffers
 * @tx_ring: Tx descriptor ring
 **/
static void e1000_clean_tx_ring(struct e1000_ring *tx_ring)
{
	struct e1000_adapter *adapter = tx_ring->adapter;
	struct e1000_buffer *buffer_info;
	unsigned long size;
	unsigned int i;

	for (i = 0; i < tx_ring->count; i++) {
		buffer_info = &tx_ring->buffer_info[i];
		e1000_put_txbuf(tx_ring, buffer_info, false);
	}

	netdev_reset_queue(adapter->netdev);
	size = sizeof(struct e1000_buffer) * tx_ring->count;
	memset(tx_ring->buffer_info, 0, size);

	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
}

/**
 * e1000e_free_tx_resources - Free Tx Resources per Queue
 * @tx_ring: Tx descriptor ring
 *
 * Free all transmit software resources
 **/
void e1000e_free_tx_resources(struct e1000_ring *tx_ring)
{
	struct e1000_adapter *adapter = tx_ring->adapter;
	struct pci_dev *pdev = adapter->pdev;

	e1000_clean_tx_ring(tx_ring);

	vfree(tx_ring->buffer_info);
	tx_ring->buffer_info = NULL;

	dma_free_coherent(&pdev->dev, tx_ring->size, tx_ring->desc,
			  tx_ring->dma);
	tx_ring->desc = NULL;
}

/**
 * e1000e_free_rx_resources - Free Rx Resources
 * @rx_ring: Rx descriptor ring
 *
 * Free all receive software resources
 **/
void e1000e_free_rx_resources(struct e1000_ring *rx_ring)
{
	struct e1000_adapter *adapter = rx_ring->adapter;
	struct pci_dev *pdev = adapter->pdev;
	int i;

	e1000_clean_rx_ring(rx_ring);

	for (i = 0; i < rx_ring->count; i++)
		kfree(rx_ring->buffer_info[i].ps_pages);

	vfree(rx_ring->buffer_info);
	rx_ring->buffer_info = NULL;

	dma_free_coherent(&pdev->dev, rx_ring->size, rx_ring->desc,
			  rx_ring->dma);
	rx_ring->desc = NULL;
}

/**
 * e1000_update_itr - update the dynamic ITR value based on statistics
 * @adapter: pointer to adapter
 * @itr_setting: current adapter->itr
 * @packets: the number of packets during this measurement interval
 * @bytes: the number of bytes during this measurement interval
 *
 *      Stores a new ITR value based on packets and byte
 *      counts during the last interrupt.  The advantage of per interrupt
 *      computation is faster updates and more accurate ITR for the current
 *      traffic pattern.  Constants in this function were computed
 *      based on theoretical maximum wire speed and thresholds were set based
 *      on testing data as well as attempting to minimize response time
 *      while increasing bulk throughput.  This functionality is controlled
 *      by the InterruptThrottleRate module parameter.
 **/
static unsigned int e1000_update_itr(u16 itr_setting, int packets, int bytes)
{
	unsigned int retval = itr_setting;

	if (packets == 0)
		return itr_setting;

	switch (itr_setting) {
	case lowest_latency:
		/* handle TSO and jumbo frames */
		if (bytes / packets > 8000)
			retval = bulk_latency;
		else if ((packets < 5) && (bytes > 512))
			retval = low_latency;
		break;
	case low_latency:	/* 50 usec aka 20000 ints/s */
		if (bytes > 10000) {
			/* this if handles the TSO accounting */
			if (bytes / packets > 8000)
				retval = bulk_latency;
			else if ((packets < 10) || ((bytes / packets) > 1200))
				retval = bulk_latency;
			else if ((packets > 35))
				retval = lowest_latency;
		} else if (bytes / packets > 2000) {
			retval = bulk_latency;
		} else if (packets <= 2 && bytes < 512) {
			retval = lowest_latency;
		}
		break;
	case bulk_latency:	/* 250 usec aka 4000 ints/s */
		if (bytes > 25000) {
			if (packets > 35)
				retval = low_latency;
		} else if (bytes < 6000) {
			retval = low_latency;
		}
		break;
	}

	return retval;
}

static void e1000_set_itr(struct e1000_adapter *adapter)
{
	u16 current_itr;
	u32 new_itr = adapter->itr;

	/* for non-gigabit speeds, just fix the interrupt rate at 4000 */
	if (adapter->link_speed != SPEED_1000) {
		current_itr = 0;
		new_itr = 4000;
		goto set_itr_now;
	}

	if (adapter->flags2 & FLAG2_DISABLE_AIM) {
		new_itr = 0;
		goto set_itr_now;
	}

	adapter->tx_itr = e1000_update_itr(adapter->tx_itr,
					   adapter->total_tx_packets,
					   adapter->total_tx_bytes);
	/* conservative mode (itr 3) eliminates the lowest_latency setting */
	if (adapter->itr_setting == 3 && adapter->tx_itr == lowest_latency)
		adapter->tx_itr = low_latency;

	adapter->rx_itr = e1000_update_itr(adapter->rx_itr,
					   adapter->total_rx_packets,
					   adapter->total_rx_bytes);
	/* conservative mode (itr 3) eliminates the lowest_latency setting */
	if (adapter->itr_setting == 3 && adapter->rx_itr == lowest_latency)
		adapter->rx_itr = low_latency;

	current_itr = max(adapter->rx_itr, adapter->tx_itr);

	/* counts and packets in update_itr are dependent on these numbers */
	switch (current_itr) {
	case lowest_latency:
		new_itr = 70000;
		break;
	case low_latency:
		new_itr = 20000;	/* aka hwitr = ~200 */
		break;
	case bulk_latency:
		new_itr = 4000;
		break;
	default:
		break;
	}

set_itr_now:
	if (new_itr != adapter->itr) {
		/* this attempts to bias the interrupt rate towards Bulk
		 * by adding intermediate steps when interrupt rate is
		 * increasing
		 */
		new_itr = new_itr > adapter->itr ?
		    min(adapter->itr + (new_itr >> 2), new_itr) : new_itr;
		adapter->itr = new_itr;
		adapter->rx_ring->itr_val = new_itr;
		if (adapter->msix_entries)
			adapter->rx_ring->set_itr = 1;
		else
			e1000e_write_itr(adapter, new_itr);
	}
}

/**
 * e1000e_write_itr - write the ITR value to the appropriate registers
 * @adapter: address of board private structure
 * @itr: new ITR value to program
 *
 * e1000e_write_itr determines if the adapter is in MSI-X mode
 * and, if so, writes the EITR registers with the ITR value.
 * Otherwise, it writes the ITR value into the ITR register.
 **/
void e1000e_write_itr(struct e1000_adapter *adapter, u32 itr)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 new_itr = itr ? 1000000000 / (itr * 256) : 0;

	if (adapter->msix_entries) {
		int vector;

		for (vector = 0; vector < adapter->num_vectors; vector++)
			writel(new_itr, hw->hw_addr + E1000_EITR_82574(vector));
	} else {
		ew32(ITR, new_itr);
	}
}

/**
 * e1000_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 **/
static int e1000_alloc_queues(struct e1000_adapter *adapter)
{
	int size = sizeof(struct e1000_ring);

	adapter->tx_ring = kzalloc(size, GFP_KERNEL);
	if (!adapter->tx_ring)
		goto err;
	adapter->tx_ring->count = adapter->tx_ring_count;
	adapter->tx_ring->adapter = adapter;

	adapter->rx_ring = kzalloc(size, GFP_KERNEL);
	if (!adapter->rx_ring)
		goto err;
	adapter->rx_ring->count = adapter->rx_ring_count;
	adapter->rx_ring->adapter = adapter;

	return 0;
err:
	e_err("Unable to allocate memory for queues\n");
	kfree(adapter->rx_ring);
	kfree(adapter->tx_ring);
	return -ENOMEM;
}

/**
 * e1000e_poll - NAPI Rx polling callback
 * @napi: struct associated with this polling callback
 * @budget: number of packets driver is allowed to process this poll
 **/
static int e1000e_poll(struct napi_struct *napi, int budget)
{
	struct e1000_adapter *adapter = container_of(napi, struct e1000_adapter,
						     napi);
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *poll_dev = adapter->netdev;
	int tx_cleaned = 1, work_done = 0;

	adapter = netdev_priv(poll_dev);

	if (!adapter->msix_entries ||
	    (adapter->rx_ring->ims_val & adapter->tx_ring->ims_val))
		tx_cleaned = e1000_clean_tx_irq(adapter->tx_ring);

	adapter->clean_rx(adapter->rx_ring, &work_done, budget);

	if (!tx_cleaned || work_done == budget)
		return budget;

	/* Exit the polling mode, but don't re-enable interrupts if stack might
	 * poll us due to busy-polling
	 */
	if (likely(napi_complete_done(napi, work_done))) {
		if (adapter->itr_setting & 3)
			e1000_set_itr(adapter);
		if (!test_bit(__E1000_DOWN, &adapter->state)) {
			if (adapter->msix_entries)
				ew32(IMS, adapter->rx_ring->ims_val);
			else
				e1000_irq_enable(adapter);
		}
	}

	return work_done;
}

static int e1000_vlan_rx_add_vid(struct net_device *netdev,
				 __always_unused __be16 proto, u16 vid)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 vfta, index;

	/* don't update vlan cookie if already programmed */
	if ((adapter->hw.mng_cookie.status &
	     E1000_MNG_DHCP_COOKIE_STATUS_VLAN) &&
	    (vid == adapter->mng_vlan_id))
		return 0;

	/* add VID to filter table */
	if (adapter->flags & FLAG_HAS_HW_VLAN_FILTER) {
		index = (vid >> 5) & 0x7F;
		vfta = E1000_READ_REG_ARRAY(hw, E1000_VFTA, index);
		vfta |= BIT((vid & 0x1F));
		hw->mac.ops.write_vfta(hw, index, vfta);
	}

	set_bit(vid, adapter->active_vlans);

	return 0;
}

static int e1000_vlan_rx_kill_vid(struct net_device *netdev,
				  __always_unused __be16 proto, u16 vid)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 vfta, index;

	if ((adapter->hw.mng_cookie.status &
	     E1000_MNG_DHCP_COOKIE_STATUS_VLAN) &&
	    (vid == adapter->mng_vlan_id)) {
		/* release control to f/w */
		e1000e_release_hw_control(adapter);
		return 0;
	}

	/* remove VID from filter table */
	if (adapter->flags & FLAG_HAS_HW_VLAN_FILTER) {
		index = (vid >> 5) & 0x7F;
		vfta = E1000_READ_REG_ARRAY(hw, E1000_VFTA, index);
		vfta &= ~BIT((vid & 0x1F));
		hw->mac.ops.write_vfta(hw, index, vfta);
	}

	clear_bit(vid, adapter->active_vlans);

	return 0;
}

/**
 * e1000e_vlan_filter_disable - helper to disable hw VLAN filtering
 * @adapter: board private structure to initialize
 **/
static void e1000e_vlan_filter_disable(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;

	if (adapter->flags & FLAG_HAS_HW_VLAN_FILTER) {
		/* disable VLAN receive filtering */
		rctl = er32(RCTL);
		rctl &= ~(E1000_RCTL_VFE | E1000_RCTL_CFIEN);
		ew32(RCTL, rctl);

		if (adapter->mng_vlan_id != (u16)E1000_MNG_VLAN_NONE) {
			e1000_vlan_rx_kill_vid(netdev, htons(ETH_P_8021Q),
					       adapter->mng_vlan_id);
			adapter->mng_vlan_id = E1000_MNG_VLAN_NONE;
		}
	}
}

/**
 * e1000e_vlan_filter_enable - helper to enable HW VLAN filtering
 * @adapter: board private structure to initialize
 **/
static void e1000e_vlan_filter_enable(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;

	if (adapter->flags & FLAG_HAS_HW_VLAN_FILTER) {
		/* enable VLAN receive filtering */
		rctl = er32(RCTL);
		rctl |= E1000_RCTL_VFE;
		rctl &= ~E1000_RCTL_CFIEN;
		ew32(RCTL, rctl);
	}
}

/**
 * e1000e_vlan_strip_disable - helper to disable HW VLAN stripping
 * @adapter: board private structure to initialize
 **/
static void e1000e_vlan_strip_disable(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl;

	/* disable VLAN tag insert/strip */
	ctrl = er32(CTRL);
	ctrl &= ~E1000_CTRL_VME;
	ew32(CTRL, ctrl);
}

/**
 * e1000e_vlan_strip_enable - helper to enable HW VLAN stripping
 * @adapter: board private structure to initialize
 **/
static void e1000e_vlan_strip_enable(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl;

	/* enable VLAN tag insert/strip */
	ctrl = er32(CTRL);
	ctrl |= E1000_CTRL_VME;
	ew32(CTRL, ctrl);
}

static void e1000_update_mng_vlan(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u16 vid = adapter->hw.mng_cookie.vlan_id;
	u16 old_vid = adapter->mng_vlan_id;

	if (adapter->hw.mng_cookie.status & E1000_MNG_DHCP_COOKIE_STATUS_VLAN) {
		e1000_vlan_rx_add_vid(netdev, htons(ETH_P_8021Q), vid);
		adapter->mng_vlan_id = vid;
	}

	if ((old_vid != (u16)E1000_MNG_VLAN_NONE) && (vid != old_vid))
		e1000_vlan_rx_kill_vid(netdev, htons(ETH_P_8021Q), old_vid);
}

static void e1000_restore_vlan(struct e1000_adapter *adapter)
{
	u16 vid;

	e1000_vlan_rx_add_vid(adapter->netdev, htons(ETH_P_8021Q), 0);

	for_each_set_bit(vid, adapter->active_vlans, VLAN_N_VID)
	    e1000_vlan_rx_add_vid(adapter->netdev, htons(ETH_P_8021Q), vid);
}

static void e1000_init_manageability_pt(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 manc, manc2h, mdef, i, j;

	if (!(adapter->flags & FLAG_MNG_PT_ENABLED))
		return;

	manc = er32(MANC);

	/* enable receiving management packets to the host. this will probably
	 * generate destination unreachable messages from the host OS, but
	 * the packets will be handled on SMBUS
	 */
	manc |= E1000_MANC_EN_MNG2HOST;
	manc2h = er32(MANC2H);

	switch (hw->mac.type) {
	default:
		manc2h |= (E1000_MANC2H_PORT_623 | E1000_MANC2H_PORT_664);
		break;
	case e1000_82574:
	case e1000_82583:
		/* Check if IPMI pass-through decision filter already exists;
		 * if so, enable it.
		 */
		for (i = 0, j = 0; i < 8; i++) {
			mdef = er32(MDEF(i));

			/* Ignore filters with anything other than IPMI ports */
			if (mdef & ~(E1000_MDEF_PORT_623 | E1000_MDEF_PORT_664))
				continue;

			/* Enable this decision filter in MANC2H */
			if (mdef)
				manc2h |= BIT(i);

			j |= mdef;
		}

		if (j == (E1000_MDEF_PORT_623 | E1000_MDEF_PORT_664))
			break;

		/* Create new decision filter in an empty filter */
		for (i = 0, j = 0; i < 8; i++)
			if (er32(MDEF(i)) == 0) {
				ew32(MDEF(i), (E1000_MDEF_PORT_623 |
					       E1000_MDEF_PORT_664));
				manc2h |= BIT(1);
				j++;
				break;
			}

		if (!j)
			e_warn("Unable to create IPMI pass-through filter\n");
		break;
	}

	ew32(MANC2H, manc2h);
	ew32(MANC, manc);
}

/**
 * e1000_configure_tx - Configure Transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/
static void e1000_configure_tx(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_ring *tx_ring = adapter->tx_ring;
	u64 tdba;
	u32 tdlen, tctl, tarc;

	/* Setup the HW Tx Head and Tail descriptor pointers */
	tdba = tx_ring->dma;
	tdlen = tx_ring->count * sizeof(struct e1000_tx_desc);
	ew32(TDBAL(0), (tdba & DMA_BIT_MASK(32)));
	ew32(TDBAH(0), (tdba >> 32));
	ew32(TDLEN(0), tdlen);
	ew32(TDH(0), 0);
	ew32(TDT(0), 0);
	tx_ring->head = adapter->hw.hw_addr + E1000_TDH(0);
	tx_ring->tail = adapter->hw.hw_addr + E1000_TDT(0);

	writel(0, tx_ring->head);
	if (adapter->flags2 & FLAG2_PCIM2PCI_ARBITER_WA)
		e1000e_update_tdt_wa(tx_ring, 0);
	else
		writel(0, tx_ring->tail);

	/* Set the Tx Interrupt Delay register */
	ew32(TIDV, adapter->tx_int_delay);
	/* Tx irq moderation */
	ew32(TADV, adapter->tx_abs_int_delay);

	if (adapter->flags2 & FLAG2_DMA_BURST) {
		u32 txdctl = er32(TXDCTL(0));

		txdctl &= ~(E1000_TXDCTL_PTHRESH | E1000_TXDCTL_HTHRESH |
			    E1000_TXDCTL_WTHRESH);
		/* set up some performance related parameters to encourage the
		 * hardware to use the bus more efficiently in bursts, depends
		 * on the tx_int_delay to be enabled,
		 * wthresh = 1 ==> burst write is disabled to avoid Tx stalls
		 * hthresh = 1 ==> prefetch when one or more available
		 * pthresh = 0x1f ==> prefetch if internal cache 31 or less
		 * BEWARE: this seems to work but should be considered first if
		 * there are Tx hangs or other Tx related bugs
		 */
		txdctl |= E1000_TXDCTL_DMA_BURST_ENABLE;
		ew32(TXDCTL(0), txdctl);
	}
	/* erratum work around: set txdctl the same for both queues */
	ew32(TXDCTL(1), er32(TXDCTL(0)));

	/* Program the Transmit Control Register */
	tctl = er32(TCTL);
	tctl &= ~E1000_TCTL_CT;
	tctl |= E1000_TCTL_PSP | E1000_TCTL_RTLC |
		(E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

	if (adapter->flags & FLAG_TARC_SPEED_MODE_BIT) {
		tarc = er32(TARC(0));
		/* set the speed mode bit, we'll clear it if we're not at
		 * gigabit link later
		 */
#define SPEED_MODE_BIT BIT(21)
		tarc |= SPEED_MODE_BIT;
		ew32(TARC(0), tarc);
	}

	/* errata: program both queues to unweighted RR */
	if (adapter->flags & FLAG_TARC_SET_BIT_ZERO) {
		tarc = er32(TARC(0));
		tarc |= 1;
		ew32(TARC(0), tarc);
		tarc = er32(TARC(1));
		tarc |= 1;
		ew32(TARC(1), tarc);
	}

	/* Setup Transmit Descriptor Settings for eop descriptor */
	adapter->txd_cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS;

	/* only set IDE if we are delaying interrupts using the timers */
	if (adapter->tx_int_delay)
		adapter->txd_cmd |= E1000_TXD_CMD_IDE;

	/* enable Report Status bit */
	adapter->txd_cmd |= E1000_TXD_CMD_RS;

	ew32(TCTL, tctl);

	hw->mac.ops.config_collision_dist(hw);

	/* SPT and KBL Si errata workaround to avoid data corruption */
	if (hw->mac.type == e1000_pch_spt) {
		u32 reg_val;

		reg_val = er32(IOSFPC);
		reg_val |= E1000_RCTL_RDMTS_HEX;
		ew32(IOSFPC, reg_val);

		reg_val = er32(TARC(0));
		/* SPT and KBL Si errata workaround to avoid Tx hang.
		 * Dropping the number of outstanding requests from
		 * 3 to 2 in order to avoid a buffer overrun.
		 */
		reg_val &= ~E1000_TARC0_CB_MULTIQ_3_REQ;
		reg_val |= E1000_TARC0_CB_MULTIQ_2_REQ;
		ew32(TARC(0), reg_val);
	}
}

/**
 * e1000_setup_rctl - configure the receive control registers
 * @adapter: Board private structure
 **/
#define PAGE_USE_COUNT(S) (((S) >> PAGE_SHIFT) + \
			   (((S) & (PAGE_SIZE - 1)) ? 1 : 0))
static void e1000_setup_rctl(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl, rfctl;
	u32 pages = 0;

	/* Workaround Si errata on PCHx - configure jumbo frame flow.
	 * If jumbo frames not set, program related MAC/PHY registers
	 * to h/w defaults
	 */
	if (hw->mac.type >= e1000_pch2lan) {
		s32 ret_val;

		if (adapter->netdev->mtu > ETH_DATA_LEN)
			ret_val = e1000_lv_jumbo_workaround_ich8lan(hw, true);
		else
			ret_val = e1000_lv_jumbo_workaround_ich8lan(hw, false);

		if (ret_val)
			e_dbg("failed to enable|disable jumbo frame workaround mode\n");
	}

	/* Program MC offset vector base */
	rctl = er32(RCTL);
	rctl &= ~(3 << E1000_RCTL_MO_SHIFT);
	rctl |= E1000_RCTL_EN | E1000_RCTL_BAM |
	    E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
	    (adapter->hw.mac.mc_filter_type << E1000_RCTL_MO_SHIFT);

	/* Do not Store bad packets */
	rctl &= ~E1000_RCTL_SBP;

	/* Enable Long Packet receive */
	if (adapter->netdev->mtu <= ETH_DATA_LEN)
		rctl &= ~E1000_RCTL_LPE;
	else
		rctl |= E1000_RCTL_LPE;

	/* Some systems expect that the CRC is included in SMBUS traffic. The
	 * hardware strips the CRC before sending to both SMBUS (BMC) and to
	 * host memory when this is enabled
	 */
	if (adapter->flags2 & FLAG2_CRC_STRIPPING)
		rctl |= E1000_RCTL_SECRC;

	/* Workaround Si errata on 82577 PHY - configure IPG for jumbos */
	if ((hw->phy.type == e1000_phy_82577) && (rctl & E1000_RCTL_LPE)) {
		u16 phy_data;

		e1e_rphy(hw, PHY_REG(770, 26), &phy_data);
		phy_data &= 0xfff8;
		phy_data |= BIT(2);
		e1e_wphy(hw, PHY_REG(770, 26), phy_data);

		e1e_rphy(hw, 22, &phy_data);
		phy_data &= 0x0fff;
		phy_data |= BIT(14);
		e1e_wphy(hw, 0x10, 0x2823);
		e1e_wphy(hw, 0x11, 0x0003);
		e1e_wphy(hw, 22, phy_data);
	}

	/* Setup buffer sizes */
	rctl &= ~E1000_RCTL_SZ_4096;
	rctl |= E1000_RCTL_BSEX;
	switch (adapter->rx_buffer_len) {
	case 2048:
	default:
		rctl |= E1000_RCTL_SZ_2048;
		rctl &= ~E1000_RCTL_BSEX;
		break;
	case 4096:
		rctl |= E1000_RCTL_SZ_4096;
		break;
	case 8192:
		rctl |= E1000_RCTL_SZ_8192;
		break;
	case 16384:
		rctl |= E1000_RCTL_SZ_16384;
		break;
	}

	/* Enable Extended Status in all Receive Descriptors */
	rfctl = er32(RFCTL);
	rfctl |= E1000_RFCTL_EXTEN;
	ew32(RFCTL, rfctl);

	/* 82571 and greater support packet-split where the protocol
	 * header is placed in skb->data and the packet data is
	 * placed in pages hanging off of skb_shinfo(skb)->nr_frags.
	 * In the case of a non-split, skb->data is linearly filled,
	 * followed by the page buffers.  Therefore, skb->data is
	 * sized to hold the largest protocol header.
	 *
	 * allocations using alloc_page take too long for regular MTU
	 * so only enable packet split for jumbo frames
	 *
	 * Using pages when the page size is greater than 16k wastes
	 * a lot of memory, since we allocate 3 pages at all times
	 * per packet.
	 */
	pages = PAGE_USE_COUNT(adapter->netdev->mtu);
	if ((pages <= 3) && (PAGE_SIZE <= 16384) && (rctl & E1000_RCTL_LPE))
		adapter->rx_ps_pages = pages;
	else
		adapter->rx_ps_pages = 0;

	if (adapter->rx_ps_pages) {
		u32 psrctl = 0;

		/* Enable Packet split descriptors */
		rctl |= E1000_RCTL_DTYP_PS;

		psrctl |= adapter->rx_ps_bsize0 >> E1000_PSRCTL_BSIZE0_SHIFT;

		switch (adapter->rx_ps_pages) {
		case 3:
			psrctl |= PAGE_SIZE << E1000_PSRCTL_BSIZE3_SHIFT;
			/* fall-through */
		case 2:
			psrctl |= PAGE_SIZE << E1000_PSRCTL_BSIZE2_SHIFT;
			/* fall-through */
		case 1:
			psrctl |= PAGE_SIZE >> E1000_PSRCTL_BSIZE1_SHIFT;
			break;
		}

		ew32(PSRCTL, psrctl);
	}

	/* This is useful for sniffing bad packets. */
	if (adapter->netdev->features & NETIF_F_RXALL) {
		/* UPE and MPE will be handled by normal PROMISC logic
		 * in e1000e_set_rx_mode
		 */
		rctl |= (E1000_RCTL_SBP |	/* Receive bad packets */
			 E1000_RCTL_BAM |	/* RX All Bcast Pkts */
			 E1000_RCTL_PMCF);	/* RX All MAC Ctrl Pkts */

		rctl &= ~(E1000_RCTL_VFE |	/* Disable VLAN filter */
			  E1000_RCTL_DPF |	/* Allow filtered pause */
			  E1000_RCTL_CFIEN);	/* Dis VLAN CFIEN Filter */
		/* Do not mess with E1000_CTRL_VME, it affects transmit as well,
		 * and that breaks VLANs.
		 */
	}

	ew32(RCTL, rctl);
	/* just started the receive unit, no need to restart */
	adapter->flags &= ~FLAG_RESTART_NOW;
}

/**
 * e1000_configure_rx - Configure Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/
static void e1000_configure_rx(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_ring *rx_ring = adapter->rx_ring;
	u64 rdba;
	u32 rdlen, rctl, rxcsum, ctrl_ext;

	if (adapter->rx_ps_pages) {
		/* this is a 32 byte descriptor */
		rdlen = rx_ring->count *
		    sizeof(union e1000_rx_desc_packet_split);
		adapter->clean_rx = e1000_clean_rx_irq_ps;
		adapter->alloc_rx_buf = e1000_alloc_rx_buffers_ps;
	} else if (adapter->netdev->mtu > ETH_FRAME_LEN + ETH_FCS_LEN) {
		rdlen = rx_ring->count * sizeof(union e1000_rx_desc_extended);
		adapter->clean_rx = e1000_clean_jumbo_rx_irq;
		adapter->alloc_rx_buf = e1000_alloc_jumbo_rx_buffers;
	} else {
		rdlen = rx_ring->count * sizeof(union e1000_rx_desc_extended);
		adapter->clean_rx = e1000_clean_rx_irq;
		adapter->alloc_rx_buf = e1000_alloc_rx_buffers;
	}

	/* disable receives while setting up the descriptors */
	rctl = er32(RCTL);
	if (!(adapter->flags2 & FLAG2_NO_DISABLE_RX))
		ew32(RCTL, rctl & ~E1000_RCTL_EN);
	e1e_flush();
	usleep_range(10000, 20000);

	if (adapter->flags2 & FLAG2_DMA_BURST) {
		/* set the writeback threshold (only takes effect if the RDTR
		 * is set). set GRAN=1 and write back up to 0x4 worth, and
		 * enable prefetching of 0x20 Rx descriptors
		 * granularity = 01
		 * wthresh = 04,
		 * hthresh = 04,
		 * pthresh = 0x20
		 */
		ew32(RXDCTL(0), E1000_RXDCTL_DMA_BURST_ENABLE);
		ew32(RXDCTL(1), E1000_RXDCTL_DMA_BURST_ENABLE);
	}

	/* set the Receive Delay Timer Register */
	ew32(RDTR, adapter->rx_int_delay);

	/* irq moderation */
	ew32(RADV, adapter->rx_abs_int_delay);
	if ((adapter->itr_setting != 0) && (adapter->itr != 0))
		e1000e_write_itr(adapter, adapter->itr);

	ctrl_ext = er32(CTRL_EXT);
	/* Auto-Mask interrupts upon ICR access */
	ctrl_ext |= E1000_CTRL_EXT_IAME;
	ew32(IAM, 0xffffffff);
	ew32(CTRL_EXT, ctrl_ext);
	e1e_flush();

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring
	 */
	rdba = rx_ring->dma;
	ew32(RDBAL(0), (rdba & DMA_BIT_MASK(32)));
	ew32(RDBAH(0), (rdba >> 32));
	ew32(RDLEN(0), rdlen);
	ew32(RDH(0), 0);
	ew32(RDT(0), 0);
	rx_ring->head = adapter->hw.hw_addr + E1000_RDH(0);
	rx_ring->tail = adapter->hw.hw_addr + E1000_RDT(0);

	writel(0, rx_ring->head);
	if (adapter->flags2 & FLAG2_PCIM2PCI_ARBITER_WA)
		e1000e_update_rdt_wa(rx_ring, 0);
	else
		writel(0, rx_ring->tail);

	/* Enable Receive Checksum Offload for TCP and UDP */
	rxcsum = er32(RXCSUM);
	if (adapter->netdev->features & NETIF_F_RXCSUM)
		rxcsum |= E1000_RXCSUM_TUOFL;
	else
		rxcsum &= ~E1000_RXCSUM_TUOFL;
	ew32(RXCSUM, rxcsum);

	/* With jumbo frames, excessive C-state transition latencies result
	 * in dropped transactions.
	 */
	if (adapter->netdev->mtu > ETH_DATA_LEN) {
		u32 lat =
		    ((er32(PBA) & E1000_PBA_RXA_MASK) * 1024 -
		     adapter->max_frame_size) * 8 / 1000;

		if (adapter->flags & FLAG_IS_ICH) {
			u32 rxdctl = er32(RXDCTL(0));

			ew32(RXDCTL(0), rxdctl | 0x3 | BIT(8));
		}

		dev_info(&adapter->pdev->dev,
			 "Some CPU C-states have been disabled in order to enable jumbo frames\n");
		pm_qos_update_request(&adapter->pm_qos_req, lat);
	} else {
		pm_qos_update_request(&adapter->pm_qos_req,
				      PM_QOS_DEFAULT_VALUE);
	}

	/* Enable Receives */
	ew32(RCTL, rctl);
}

/**
 * e1000e_write_mc_addr_list - write multicast addresses to MTA
 * @netdev: network interface device structure
 *
 * Writes multicast address list to the MTA hash table.
 * Returns: -ENOMEM on failure
 *                0 on no addresses written
 *                X on writing X addresses to MTA
 */
static int e1000e_write_mc_addr_list(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct netdev_hw_addr *ha;
	u8 *mta_list;
	int i;

	if (netdev_mc_empty(netdev)) {
		/* nothing to program, so clear mc list */
		hw->mac.ops.update_mc_addr_list(hw, NULL, 0);
		return 0;
	}

	mta_list = kcalloc(netdev_mc_count(netdev), ETH_ALEN, GFP_ATOMIC);
	if (!mta_list)
		return -ENOMEM;

	/* update_mc_addr_list expects a packed array of only addresses. */
	i = 0;
	netdev_for_each_mc_addr(ha, netdev)
	    memcpy(mta_list + (i++ * ETH_ALEN), ha->addr, ETH_ALEN);

	hw->mac.ops.update_mc_addr_list(hw, mta_list, i);
	kfree(mta_list);

	return netdev_mc_count(netdev);
}

/**
 * e1000e_write_uc_addr_list - write unicast addresses to RAR table
 * @netdev: network interface device structure
 *
 * Writes unicast address list to the RAR table.
 * Returns: -ENOMEM on failure/insufficient address space
 *                0 on no addresses written
 *                X on writing X addresses to the RAR table
 **/
static int e1000e_write_uc_addr_list(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	unsigned int rar_entries;
	int count = 0;

	rar_entries = hw->mac.ops.rar_get_count(hw);

	/* save a rar entry for our hardware address */
	rar_entries--;

	/* save a rar entry for the LAA workaround */
	if (adapter->flags & FLAG_RESET_OVERWRITES_LAA)
		rar_entries--;

	/* return ENOMEM indicating insufficient memory for addresses */
	if (netdev_uc_count(netdev) > rar_entries)
		return -ENOMEM;

	if (!netdev_uc_empty(netdev) && rar_entries) {
		struct netdev_hw_addr *ha;

		/* write the addresses in reverse order to avoid write
		 * combining
		 */
		netdev_for_each_uc_addr(ha, netdev) {
			int ret_val;

			if (!rar_entries)
				break;
			ret_val = hw->mac.ops.rar_set(hw, ha->addr, rar_entries--);
			if (ret_val < 0)
				return -ENOMEM;
			count++;
		}
	}

	/* zero out the remaining RAR entries not used above */
	for (; rar_entries > 0; rar_entries--) {
		ew32(RAH(rar_entries), 0);
		ew32(RAL(rar_entries), 0);
	}
	e1e_flush();

	return count;
}

/**
 * e1000e_set_rx_mode - secondary unicast, Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The ndo_set_rx_mode entry point is called whenever the unicast or multicast
 * address list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper unicast, multicast,
 * promiscuous mode, and all-multi behavior.
 **/
static void e1000e_set_rx_mode(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;

	if (pm_runtime_suspended(netdev->dev.parent))
		return;

	/* Check for Promiscuous and All Multicast modes */
	rctl = er32(RCTL);

	/* clear the affected bits */
	rctl &= ~(E1000_RCTL_UPE | E1000_RCTL_MPE);

	if (netdev->flags & IFF_PROMISC) {
		rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
		/* Do not hardware filter VLANs in promisc mode */
		e1000e_vlan_filter_disable(adapter);
	} else {
		int count;

		if (netdev->flags & IFF_ALLMULTI) {
			rctl |= E1000_RCTL_MPE;
		} else {
			/* Write addresses to the MTA, if the attempt fails
			 * then we should just turn on promiscuous mode so
			 * that we can at least receive multicast traffic
			 */
			count = e1000e_write_mc_addr_list(netdev);
			if (count < 0)
				rctl |= E1000_RCTL_MPE;
		}
		e1000e_vlan_filter_enable(adapter);
		/* Write addresses to available RAR registers, if there is not
		 * sufficient space to store all the addresses then enable
		 * unicast promiscuous mode
		 */
		count = e1000e_write_uc_addr_list(netdev);
		if (count < 0)
			rctl |= E1000_RCTL_UPE;
	}

	ew32(RCTL, rctl);

	if (netdev->features & NETIF_F_HW_VLAN_CTAG_RX)
		e1000e_vlan_strip_enable(adapter);
	else
		e1000e_vlan_strip_disable(adapter);
}

static void e1000e_setup_rss_hash(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 mrqc, rxcsum;
	u32 rss_key[10];
	int i;

	netdev_rss_key_fill(rss_key, sizeof(rss_key));
	for (i = 0; i < 10; i++)
		ew32(RSSRK(i), rss_key[i]);

	/* Direct all traffic to queue 0 */
	for (i = 0; i < 32; i++)
		ew32(RETA(i), 0);

	/* Disable raw packet checksumming so that RSS hash is placed in
	 * descriptor on writeback.
	 */
	rxcsum = er32(RXCSUM);
	rxcsum |= E1000_RXCSUM_PCSD;

	ew32(RXCSUM, rxcsum);

	mrqc = (E1000_MRQC_RSS_FIELD_IPV4 |
		E1000_MRQC_RSS_FIELD_IPV4_TCP |
		E1000_MRQC_RSS_FIELD_IPV6 |
		E1000_MRQC_RSS_FIELD_IPV6_TCP |
		E1000_MRQC_RSS_FIELD_IPV6_TCP_EX);

	ew32(MRQC, mrqc);
}

/**
 * e1000e_get_base_timinca - get default SYSTIM time increment attributes
 * @adapter: board private structure
 * @timinca: pointer to returned time increment attributes
 *
 * Get attributes for incrementing the System Time Register SYSTIML/H at
 * the default base frequency, and set the cyclecounter shift value.
 **/
s32 e1000e_get_base_timinca(struct e1000_adapter *adapter, u32 *timinca)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 incvalue, incperiod, shift;

	/* Make sure clock is enabled on I217/I218/I219  before checking
	 * the frequency
	 */
	if ((hw->mac.type >= e1000_pch_lpt) &&
	    !(er32(TSYNCTXCTL) & E1000_TSYNCTXCTL_ENABLED) &&
	    !(er32(TSYNCRXCTL) & E1000_TSYNCRXCTL_ENABLED)) {
		u32 fextnvm7 = er32(FEXTNVM7);

		if (!(fextnvm7 & BIT(0))) {
			ew32(FEXTNVM7, fextnvm7 | BIT(0));
			e1e_flush();
		}
	}

	switch (hw->mac.type) {
	case e1000_pch2lan:
		/* Stable 96MHz frequency */
		incperiod = INCPERIOD_96MHZ;
		incvalue = INCVALUE_96MHZ;
		shift = INCVALUE_SHIFT_96MHZ;
		adapter->cc.shift = shift + INCPERIOD_SHIFT_96MHZ;
		break;
	case e1000_pch_lpt:
		if (er32(TSYNCRXCTL) & E1000_TSYNCRXCTL_SYSCFI) {
			/* Stable 96MHz frequency */
			incperiod = INCPERIOD_96MHZ;
			incvalue = INCVALUE_96MHZ;
			shift = INCVALUE_SHIFT_96MHZ;
			adapter->cc.shift = shift + INCPERIOD_SHIFT_96MHZ;
		} else {
			/* Stable 25MHz frequency */
			incperiod = INCPERIOD_25MHZ;
			incvalue = INCVALUE_25MHZ;
			shift = INCVALUE_SHIFT_25MHZ;
			adapter->cc.shift = shift;
		}
		break;
	case e1000_pch_spt:
		/* Stable 24MHz frequency */
		incperiod = INCPERIOD_24MHZ;
		incvalue = INCVALUE_24MHZ;
		shift = INCVALUE_SHIFT_24MHZ;
		adapter->cc.shift = shift;
		break;
	case e1000_pch_cnp:
		if (er32(TSYNCRXCTL) & E1000_TSYNCRXCTL_SYSCFI) {
			/* Stable 24MHz frequency */
			incperiod = INCPERIOD_24MHZ;
			incvalue = INCVALUE_24MHZ;
			shift = INCVALUE_SHIFT_24MHZ;
			adapter->cc.shift = shift;
		} else {
			/* Stable 38400KHz frequency */
			incperiod = INCPERIOD_38400KHZ;
			incvalue = INCVALUE_38400KHZ;
			shift = INCVALUE_SHIFT_38400KHZ;
			adapter->cc.shift = shift;
		}
		break;
	case e1000_82574:
	case e1000_82583:
		/* Stable 25MHz frequency */
		incperiod = INCPERIOD_25MHZ;
		incvalue = INCVALUE_25MHZ;
		shift = INCVALUE_SHIFT_25MHZ;
		adapter->cc.shift = shift;
		break;
	default:
		return -EINVAL;
	}

	*timinca = ((incperiod << E1000_TIMINCA_INCPERIOD_SHIFT) |
		    ((incvalue << shift) & E1000_TIMINCA_INCVALUE_MASK));

	return 0;
}

/**
 * e1000e_config_hwtstamp - configure the hwtstamp registers and enable/disable
 * @adapter: board private structure
 *
 * Outgoing time stamping can be enabled and disabled. Play nice and
 * disable it when requested, although it shouldn't cause any overhead
 * when no packet needs it. At most one packet in the queue may be
 * marked for time stamping, otherwise it would be impossible to tell
 * for sure to which packet the hardware time stamp belongs.
 *
 * Incoming time stamping has to be configured via the hardware filters.
 * Not all combinations are supported, in particular event type has to be
 * specified. Matching the kind of event packet is not supported, with the
 * exception of "all V2 events regardless of level 2 or 4".
 **/
static int e1000e_config_hwtstamp(struct e1000_adapter *adapter,
				  struct hwtstamp_config *config)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 tsync_tx_ctl = E1000_TSYNCTXCTL_ENABLED;
	u32 tsync_rx_ctl = E1000_TSYNCRXCTL_ENABLED;
	u32 rxmtrl = 0;
	u16 rxudp = 0;
	bool is_l4 = false;
	bool is_l2 = false;
	u32 regval;

	if (!(adapter->flags & FLAG_HAS_HW_TIMESTAMP))
		return -EINVAL;

	/* flags reserved for future extensions - must be zero */
	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		tsync_tx_ctl = 0;
		break;
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tsync_rx_ctl = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_L4_V1;
		rxmtrl = E1000_RXMTRL_PTP_V1_SYNC_MESSAGE;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_L4_V1;
		rxmtrl = E1000_RXMTRL_PTP_V1_DELAY_REQ_MESSAGE;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
		/* Also time stamps V2 L2 Path Delay Request/Response */
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_L2_V2;
		rxmtrl = E1000_RXMTRL_PTP_V2_SYNC_MESSAGE;
		is_l2 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		/* Also time stamps V2 L2 Path Delay Request/Response. */
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_L2_V2;
		rxmtrl = E1000_RXMTRL_PTP_V2_DELAY_REQ_MESSAGE;
		is_l2 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
		/* Hardware cannot filter just V2 L4 Sync messages;
		 * fall-through to V2 (both L2 and L4) Sync.
		 */
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
		/* Also time stamps V2 Path Delay Request/Response. */
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_L2_L4_V2;
		rxmtrl = E1000_RXMTRL_PTP_V2_SYNC_MESSAGE;
		is_l2 = true;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		/* Hardware cannot filter just V2 L4 Delay Request messages;
		 * fall-through to V2 (both L2 and L4) Delay Request.
		 */
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		/* Also time stamps V2 Path Delay Request/Response. */
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_L2_L4_V2;
		rxmtrl = E1000_RXMTRL_PTP_V2_DELAY_REQ_MESSAGE;
		is_l2 = true;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		/* Hardware cannot filter just V2 L4 or L2 Event messages;
		 * fall-through to all V2 (both L2 and L4) Events.
		 */
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_EVENT_V2;
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		is_l2 = true;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
		/* For V1, the hardware can only filter Sync messages or
		 * Delay Request messages but not both so fall-through to
		 * time stamp all packets.
		 */
	case HWTSTAMP_FILTER_NTP_ALL:
	case HWTSTAMP_FILTER_ALL:
		is_l2 = true;
		is_l4 = true;
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_ALL;
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	adapter->hwtstamp_config = *config;

	/* enable/disable Tx h/w time stamping */
	regval = er32(TSYNCTXCTL);
	regval &= ~E1000_TSYNCTXCTL_ENABLED;
	regval |= tsync_tx_ctl;
	ew32(TSYNCTXCTL, regval);
	if ((er32(TSYNCTXCTL) & E1000_TSYNCTXCTL_ENABLED) !=
	    (regval & E1000_TSYNCTXCTL_ENABLED)) {
		e_err("Timesync Tx Control register not set as expected\n");
		return -EAGAIN;
	}

	/* enable/disable Rx h/w time stamping */
	regval = er32(TSYNCRXCTL);
	regval &= ~(E1000_TSYNCRXCTL_ENABLED | E1000_TSYNCRXCTL_TYPE_MASK);
	regval |= tsync_rx_ctl;
	ew32(TSYNCRXCTL, regval);
	if ((er32(TSYNCRXCTL) & (E1000_TSYNCRXCTL_ENABLED |
				 E1000_TSYNCRXCTL_TYPE_MASK)) !=
	    (regval & (E1000_TSYNCRXCTL_ENABLED |
		       E1000_TSYNCRXCTL_TYPE_MASK))) {
		e_err("Timesync Rx Control register not set as expected\n");
		return -EAGAIN;
	}

	/* L2: define ethertype filter for time stamped packets */
	if (is_l2)
		rxmtrl |= ETH_P_1588;

	/* define which PTP packets get time stamped */
	ew32(RXMTRL, rxmtrl);

	/* Filter by destination port */
	if (is_l4) {
		rxudp = PTP_EV_PORT;
		cpu_to_be16s(&rxudp);
	}
	ew32(RXUDP, rxudp);

	e1e_flush();

	/* Clear TSYNCRXCTL_VALID & TSYNCTXCTL_VALID bit */
	er32(RXSTMPH);
	er32(TXSTMPH);

	return 0;
}

/**
 * e1000_configure - configure the hardware for Rx and Tx
 * @adapter: private board structure
 **/
static void e1000_configure(struct e1000_adapter *adapter)
{
	struct e1000_ring *rx_ring = adapter->rx_ring;

	e1000e_set_rx_mode(adapter->netdev);

	e1000_restore_vlan(adapter);
	e1000_init_manageability_pt(adapter);

	e1000_configure_tx(adapter);

	if (adapter->netdev->features & NETIF_F_RXHASH)
		e1000e_setup_rss_hash(adapter);
	e1000_setup_rctl(adapter);
	e1000_configure_rx(adapter);
	adapter->alloc_rx_buf(rx_ring, e1000_desc_unused(rx_ring), GFP_KERNEL);
}

/**
 * e1000e_power_up_phy - restore link in case the phy was powered down
 * @adapter: address of board private structure
 *
 * The phy may be powered down to save power and turn off link when the
 * driver is unloaded and wake on lan is not enabled (among others)
 * *** this routine MUST be followed by a call to e1000e_reset ***
 **/
void e1000e_power_up_phy(struct e1000_adapter *adapter)
{
	if (adapter->hw.phy.ops.power_up)
		adapter->hw.phy.ops.power_up(&adapter->hw);

	adapter->hw.mac.ops.setup_link(&adapter->hw);
}

/**
 * e1000_power_down_phy - Power down the PHY
 *
 * Power down the PHY so no link is implied when interface is down.
 * The PHY cannot be powered down if management or WoL is active.
 */
static void e1000_power_down_phy(struct e1000_adapter *adapter)
{
	if (adapter->hw.phy.ops.power_down)
		adapter->hw.phy.ops.power_down(&adapter->hw);
}

/**
 * e1000_flush_tx_ring - remove all descriptors from the tx_ring
 *
 * We want to clear all pending descriptors from the TX ring.
 * zeroing happens when the HW reads the regs. We  assign the ring itself as
 * the data of the next descriptor. We don't care about the data we are about
 * to reset the HW.
 */
static void e1000_flush_tx_ring(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_ring *tx_ring = adapter->tx_ring;
	struct e1000_tx_desc *tx_desc = NULL;
	u32 tdt, tctl, txd_lower = E1000_TXD_CMD_IFCS;
	u16 size = 512;

	tctl = er32(TCTL);
	ew32(TCTL, tctl | E1000_TCTL_EN);
	tdt = er32(TDT(0));
	BUG_ON(tdt != tx_ring->next_to_use);
	tx_desc =  E1000_TX_DESC(*tx_ring, tx_ring->next_to_use);
	tx_desc->buffer_addr = tx_ring->dma;

	tx_desc->lower.data = cpu_to_le32(txd_lower | size);
	tx_desc->upper.data = 0;
	/* flush descriptors to memory before notifying the HW */
	wmb();
	tx_ring->next_to_use++;
	if (tx_ring->next_to_use == tx_ring->count)
		tx_ring->next_to_use = 0;
	ew32(TDT(0), tx_ring->next_to_use);
	mmiowb();
	usleep_range(200, 250);
}

/**
 * e1000_flush_rx_ring - remove all descriptors from the rx_ring
 *
 * Mark all descriptors in the RX ring as consumed and disable the rx ring
 */
static void e1000_flush_rx_ring(struct e1000_adapter *adapter)
{
	u32 rctl, rxdctl;
	struct e1000_hw *hw = &adapter->hw;

	rctl = er32(RCTL);
	ew32(RCTL, rctl & ~E1000_RCTL_EN);
	e1e_flush();
	usleep_range(100, 150);

	rxdctl = er32(RXDCTL(0));
	/* zero the lower 14 bits (prefetch and host thresholds) */
	rxdctl &= 0xffffc000;

	/* update thresholds: prefetch threshold to 31, host threshold to 1
	 * and make sure the granularity is "descriptors" and not "cache lines"
	 */
	rxdctl |= (0x1F | BIT(8) | E1000_RXDCTL_THRESH_UNIT_DESC);

	ew32(RXDCTL(0), rxdctl);
	/* momentarily enable the RX ring for the changes to take effect */
	ew32(RCTL, rctl | E1000_RCTL_EN);
	e1e_flush();
	usleep_range(100, 150);
	ew32(RCTL, rctl & ~E1000_RCTL_EN);
}

/**
 * e1000_flush_desc_rings - remove all descriptors from the descriptor rings
 *
 * In i219, the descriptor rings must be emptied before resetting the HW
 * or before changing the device state to D3 during runtime (runtime PM).
 *
 * Failure to do this will cause the HW to enter a unit hang state which can
 * only be released by PCI reset on the device
 *
 */

static void e1000_flush_desc_rings(struct e1000_adapter *adapter)
{
	u16 hang_state;
	u32 fext_nvm11, tdlen;
	struct e1000_hw *hw = &adapter->hw;

	/* First, disable MULR fix in FEXTNVM11 */
	fext_nvm11 = er32(FEXTNVM11);
	fext_nvm11 |= E1000_FEXTNVM11_DISABLE_MULR_FIX;
	ew32(FEXTNVM11, fext_nvm11);
	/* do nothing if we're not in faulty state, or if the queue is empty */
	tdlen = er32(TDLEN(0));
	pci_read_config_word(adapter->pdev, PCICFG_DESC_RING_STATUS,
			     &hang_state);
	if (!(hang_state & FLUSH_DESC_REQUIRED) || !tdlen)
		return;
	e1000_flush_tx_ring(adapter);
	/* recheck, maybe the fault is caused by the rx ring */
	pci_read_config_word(adapter->pdev, PCICFG_DESC_RING_STATUS,
			     &hang_state);
	if (hang_state & FLUSH_DESC_REQUIRED)
		e1000_flush_rx_ring(adapter);
}

/**
 * e1000e_systim_reset - reset the timesync registers after a hardware reset
 * @adapter: board private structure
 *
 * When the MAC is reset, all hardware bits for timesync will be reset to the
 * default values. This function will restore the settings last in place.
 * Since the clock SYSTIME registers are reset, we will simply restore the
 * cyclecounter to the kernel real clock time.
 **/
static void e1000e_systim_reset(struct e1000_adapter *adapter)
{
	struct ptp_clock_info *info = &adapter->ptp_clock_info;
	struct e1000_hw *hw = &adapter->hw;
	unsigned long flags;
	u32 timinca;
	s32 ret_val;

	if (!(adapter->flags & FLAG_HAS_HW_TIMESTAMP))
		return;

	if (info->adjfreq) {
		/* restore the previous ptp frequency delta */
		ret_val = info->adjfreq(info, adapter->ptp_delta);
	} else {
		/* set the default base frequency if no adjustment possible */
		ret_val = e1000e_get_base_timinca(adapter, &timinca);
		if (!ret_val)
			ew32(TIMINCA, timinca);
	}

	if (ret_val) {
		dev_warn(&adapter->pdev->dev,
			 "Failed to restore TIMINCA clock rate delta: %d\n",
			 ret_val);
		return;
	}

	/* reset the systim ns time counter */
	spin_lock_irqsave(&adapter->systim_lock, flags);
	timecounter_init(&adapter->tc, &adapter->cc,
			 ktime_to_ns(ktime_get_real()));
	spin_unlock_irqrestore(&adapter->systim_lock, flags);

	/* restore the previous hwtstamp configuration settings */
	e1000e_config_hwtstamp(adapter, &adapter->hwtstamp_config);
}

/**
 * e1000e_reset - bring the hardware into a known good state
 *
 * This function boots the hardware and enables some settings that
 * require a configuration cycle of the hardware - those cannot be
 * set/changed during runtime. After reset the device needs to be
 * properly configured for Rx, Tx etc.
 */
void e1000e_reset(struct e1000_adapter *adapter)
{
	struct e1000_mac_info *mac = &adapter->hw.mac;
	struct e1000_fc_info *fc = &adapter->hw.fc;
	struct e1000_hw *hw = &adapter->hw;
	u32 tx_space, min_tx_space, min_rx_space;
	u32 pba = adapter->pba;
	u16 hwm;

	/* reset Packet Buffer Allocation to default */
	ew32(PBA, pba);

	if (adapter->max_frame_size > (VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)) {
		/* To maintain wire speed transmits, the Tx FIFO should be
		 * large enough to accommodate two full transmit packets,
		 * rounded up to the next 1KB and expressed in KB.  Likewise,
		 * the Rx FIFO should be large enough to accommodate at least
		 * one full receive packet and is similarly rounded up and
		 * expressed in KB.
		 */
		pba = er32(PBA);
		/* upper 16 bits has Tx packet buffer allocation size in KB */
		tx_space = pba >> 16;
		/* lower 16 bits has Rx packet buffer allocation size in KB */
		pba &= 0xffff;
		/* the Tx fifo also stores 16 bytes of information about the Tx
		 * but don't include ethernet FCS because hardware appends it
		 */
		min_tx_space = (adapter->max_frame_size +
				sizeof(struct e1000_tx_desc) - ETH_FCS_LEN) * 2;
		min_tx_space = ALIGN(min_tx_space, 1024);
		min_tx_space >>= 10;
		/* software strips receive CRC, so leave room for it */
		min_rx_space = adapter->max_frame_size;
		min_rx_space = ALIGN(min_rx_space, 1024);
		min_rx_space >>= 10;

		/* If current Tx allocation is less than the min Tx FIFO size,
		 * and the min Tx FIFO size is less than the current Rx FIFO
		 * allocation, take space away from current Rx allocation
		 */
		if ((tx_space < min_tx_space) &&
		    ((min_tx_space - tx_space) < pba)) {
			pba -= min_tx_space - tx_space;

			/* if short on Rx space, Rx wins and must trump Tx
			 * adjustment
			 */
			if (pba < min_rx_space)
				pba = min_rx_space;
		}

		ew32(PBA, pba);
	}

	/* flow control settings
	 *
	 * The high water mark must be low enough to fit one full frame
	 * (or the size used for early receive) above it in the Rx FIFO.
	 * Set it to the lower of:
	 * - 90% of the Rx FIFO size, and
	 * - the full Rx FIFO size minus one full frame
	 */
	if (adapter->flags & FLAG_DISABLE_FC_PAUSE_TIME)
		fc->pause_time = 0xFFFF;
	else
		fc->pause_time = E1000_FC_PAUSE_TIME;
	fc->send_xon = true;
	fc->current_mode = fc->requested_mode;

	switch (hw->mac.type) {
	case e1000_ich9lan:
	case e1000_ich10lan:
		if (adapter->netdev->mtu > ETH_DATA_LEN) {
			pba = 14;
			ew32(PBA, pba);
			fc->high_water = 0x2800;
			fc->low_water = fc->high_water - 8;
			break;
		}
		/* fall-through */
	default:
		hwm = min(((pba << 10) * 9 / 10),
			  ((pba << 10) - adapter->max_frame_size));

		fc->high_water = hwm & E1000_FCRTH_RTH;	/* 8-byte granularity */
		fc->low_water = fc->high_water - 8;
		break;
	case e1000_pchlan:
		/* Workaround PCH LOM adapter hangs with certain network
		 * loads.  If hangs persist, try disabling Tx flow control.
		 */
		if (adapter->netdev->mtu > ETH_DATA_LEN) {
			fc->high_water = 0x3500;
			fc->low_water = 0x1500;
		} else {
			fc->high_water = 0x5000;
			fc->low_water = 0x3000;
		}
		fc->refresh_time = 0x1000;
		break;
	case e1000_pch2lan:
	case e1000_pch_lpt:
	case e1000_pch_spt:
	case e1000_pch_cnp:
		fc->refresh_time = 0x0400;

		if (adapter->netdev->mtu <= ETH_DATA_LEN) {
			fc->high_water = 0x05C20;
			fc->low_water = 0x05048;
			fc->pause_time = 0x0650;
			break;
		}

		pba = 14;
		ew32(PBA, pba);
		fc->high_water = ((pba << 10) * 9 / 10) & E1000_FCRTH_RTH;
		fc->low_water = ((pba << 10) * 8 / 10) & E1000_FCRTL_RTL;
		break;
	}

	/* Alignment of Tx data is on an arbitrary byte boundary with the
	 * maximum size per Tx descriptor limited only to the transmit
	 * allocation of the packet buffer minus 96 bytes with an upper
	 * limit of 24KB due to receive synchronization limitations.
	 */
	adapter->tx_fifo_limit = min_t(u32, ((er32(PBA) >> 16) << 10) - 96,
				       24 << 10);

	/* Disable Adaptive Interrupt Moderation if 2 full packets cannot
	 * fit in receive buffer.
	 */
	if (adapter->itr_setting & 0x3) {
		if ((adapter->max_frame_size * 2) > (pba << 10)) {
			if (!(adapter->flags2 & FLAG2_DISABLE_AIM)) {
				dev_info(&adapter->pdev->dev,
					 "Interrupt Throttle Rate off\n");
				adapter->flags2 |= FLAG2_DISABLE_AIM;
				e1000e_write_itr(adapter, 0);
			}
		} else if (adapter->flags2 & FLAG2_DISABLE_AIM) {
			dev_info(&adapter->pdev->dev,
				 "Interrupt Throttle Rate on\n");
			adapter->flags2 &= ~FLAG2_DISABLE_AIM;
			adapter->itr = 20000;
			e1000e_write_itr(adapter, adapter->itr);
		}
	}

	if (hw->mac.type >= e1000_pch_spt)
		e1000_flush_desc_rings(adapter);
	/* Allow time for pending master requests to run */
	mac->ops.reset_hw(hw);

	/* For parts with AMT enabled, let the firmware know
	 * that the network interface is in control
	 */
	if (adapter->flags & FLAG_HAS_AMT)
		e1000e_get_hw_control(adapter);

	ew32(WUC, 0);

	if (mac->ops.init_hw(hw))
		e_err("Hardware Error\n");

	e1000_update_mng_vlan(adapter);

	/* Enable h/w to recognize an 802.1Q VLAN Ethernet packet */
	ew32(VET, ETH_P_8021Q);

	e1000e_reset_adaptive(hw);

	/* restore systim and hwtstamp settings */
	e1000e_systim_reset(adapter);

	/* Set EEE advertisement as appropriate */
	if (adapter->flags2 & FLAG2_HAS_EEE) {
		s32 ret_val;
		u16 adv_addr;

		switch (hw->phy.type) {
		case e1000_phy_82579:
			adv_addr = I82579_EEE_ADVERTISEMENT;
			break;
		case e1000_phy_i217:
			adv_addr = I217_EEE_ADVERTISEMENT;
			break;
		default:
			dev_err(&adapter->pdev->dev,
				"Invalid PHY type setting EEE advertisement\n");
			return;
		}

		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val) {
			dev_err(&adapter->pdev->dev,
				"EEE advertisement - unable to acquire PHY\n");
			return;
		}

		e1000_write_emi_reg_locked(hw, adv_addr,
					   hw->dev_spec.ich8lan.eee_disable ?
					   0 : adapter->eee_advert);

		hw->phy.ops.release(hw);
	}

	if (!netif_running(adapter->netdev) &&
	    !test_bit(__E1000_TESTING, &adapter->state))
		e1000_power_down_phy(adapter);

	e1000_get_phy_info(hw);

	if ((adapter->flags & FLAG_HAS_SMART_POWER_DOWN) &&
	    !(adapter->flags & FLAG_SMART_POWER_DOWN)) {
		u16 phy_data = 0;
		/* speed up time to link by disabling smart power down, ignore
		 * the return value of this function because there is nothing
		 * different we would do if it failed
		 */
		e1e_rphy(hw, IGP02E1000_PHY_POWER_MGMT, &phy_data);
		phy_data &= ~IGP02E1000_PM_SPD;
		e1e_wphy(hw, IGP02E1000_PHY_POWER_MGMT, phy_data);
	}
	if (hw->mac.type >= e1000_pch_spt && adapter->int_mode == 0) {
		u32 reg;

		/* Fextnvm7 @ 0xe4[2] = 1 */
		reg = er32(FEXTNVM7);
		reg |= E1000_FEXTNVM7_SIDE_CLK_UNGATE;
		ew32(FEXTNVM7, reg);
		/* Fextnvm9 @ 0x5bb4[13:12] = 11 */
		reg = er32(FEXTNVM9);
		reg |= E1000_FEXTNVM9_IOSFSB_CLKGATE_DIS |
		       E1000_FEXTNVM9_IOSFSB_CLKREQ_DIS;
		ew32(FEXTNVM9, reg);
	}

}

/**
 * e1000e_trigger_lsc - trigger an LSC interrupt
 * @adapter: 
 *
 * Fire a link status change interrupt to start the watchdog.
 **/
static void e1000e_trigger_lsc(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	if (adapter->msix_entries)
		ew32(ICS, E1000_ICS_LSC | E1000_ICS_OTHER);
	else
		ew32(ICS, E1000_ICS_LSC);
}

void e1000e_up(struct e1000_adapter *adapter)
{
	/* hardware has been reset, we need to reload some things */
	e1000_configure(adapter);

	clear_bit(__E1000_DOWN, &adapter->state);

	if (adapter->msix_entries)
		e1000_configure_msix(adapter);
	e1000_irq_enable(adapter);

	netif_start_queue(adapter->netdev);

	e1000e_trigger_lsc(adapter);
}

static void e1000e_flush_descriptors(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	if (!(adapter->flags2 & FLAG2_DMA_BURST))
		return;

	/* flush pending descriptor writebacks to memory */
	ew32(TIDV, adapter->tx_int_delay | E1000_TIDV_FPD);
	ew32(RDTR, adapter->rx_int_delay | E1000_RDTR_FPD);

	/* execute the writes immediately */
	e1e_flush();

	/* due to rare timing issues, write to TIDV/RDTR again to ensure the
	 * write is successful
	 */
	ew32(TIDV, adapter->tx_int_delay | E1000_TIDV_FPD);
	ew32(RDTR, adapter->rx_int_delay | E1000_RDTR_FPD);

	/* execute the writes immediately */
	e1e_flush();
}

static void e1000e_update_stats(struct e1000_adapter *adapter);

/**
 * e1000e_down - quiesce the device and optionally reset the hardware
 * @adapter: board private structure
 * @reset: boolean flag to reset the hardware or not
 */
void e1000e_down(struct e1000_adapter *adapter, bool reset)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	u32 tctl, rctl;

	/* signal that we're down so the interrupt handler does not
	 * reschedule our watchdog timer
	 */
	set_bit(__E1000_DOWN, &adapter->state);

	netif_carrier_off(netdev);

	/* disable receives in the hardware */
	rctl = er32(RCTL);
	if (!(adapter->flags2 & FLAG2_NO_DISABLE_RX))
		ew32(RCTL, rctl & ~E1000_RCTL_EN);
	/* flush and sleep below */

	netif_stop_queue(netdev);

	/* disable transmits in the hardware */
	tctl = er32(TCTL);
	tctl &= ~E1000_TCTL_EN;
	ew32(TCTL, tctl);

	/* flush both disables and wait for them to finish */
	e1e_flush();
	usleep_range(10000, 20000);

	e1000_irq_disable(adapter);

	napi_synchronize(&adapter->napi);

	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_info_timer);

	spin_lock(&adapter->stats64_lock);
	e1000e_update_stats(adapter);
	spin_unlock(&adapter->stats64_lock);

	e1000e_flush_descriptors(adapter);

	adapter->link_speed = 0;
	adapter->link_duplex = 0;

	/* Disable Si errata workaround on PCHx for jumbo frame flow */
	if ((hw->mac.type >= e1000_pch2lan) &&
	    (adapter->netdev->mtu > ETH_DATA_LEN) &&
	    e1000_lv_jumbo_workaround_ich8lan(hw, false))
		e_dbg("failed to disable jumbo frame workaround mode\n");

	if (!pci_channel_offline(adapter->pdev)) {
		if (reset)
			e1000e_reset(adapter);
		else if (hw->mac.type >= e1000_pch_spt)
			e1000_flush_desc_rings(adapter);
	}
	e1000_clean_tx_ring(adapter->tx_ring);
	e1000_clean_rx_ring(adapter->rx_ring);
}

void e1000e_reinit_locked(struct e1000_adapter *adapter)
{
	might_sleep();
	while (test_and_set_bit(__E1000_RESETTING, &adapter->state))
		usleep_range(1000, 2000);
	e1000e_down(adapter, true);
	e1000e_up(adapter);
	clear_bit(__E1000_RESETTING, &adapter->state);
}

/**
 * e1000e_sanitize_systim - sanitize raw cycle counter reads
 * @hw: pointer to the HW structure
 * @systim: PHC time value read, sanitized and returned
 * @sts: structure to hold system time before and after reading SYSTIML,
 * may be NULL
 *
 * Errata for 82574/82583 possible bad bits read from SYSTIMH/L:
 * check to see that the time is incrementing at a reasonable
 * rate and is a multiple of incvalue.
 **/
static u64 e1000e_sanitize_systim(struct e1000_hw *hw, u64 systim,
				  struct ptp_system_timestamp *sts)
{
	u64 time_delta, rem, temp;
	u64 systim_next;
	u32 incvalue;
	int i;

	incvalue = er32(TIMINCA) & E1000_TIMINCA_INCVALUE_MASK;
	for (i = 0; i < E1000_MAX_82574_SYSTIM_REREADS; i++) {
		/* latch SYSTIMH on read of SYSTIML */
		ptp_read_system_prets(sts);
		systim_next = (u64)er32(SYSTIML);
		ptp_read_system_postts(sts);
		systim_next |= (u64)er32(SYSTIMH) << 32;

		time_delta = systim_next - systim;
		temp = time_delta;
		/* VMWare users have seen incvalue of zero, don't div / 0 */
		rem = incvalue ? do_div(temp, incvalue) : (time_delta != 0);

		systim = systim_next;

		if ((time_delta < E1000_82574_SYSTIM_EPSILON) && (rem == 0))
			break;
	}

	return systim;
}

/**
 * e1000e_read_systim - read SYSTIM register
 * @adapter: board private structure
 * @sts: structure which will contain system time before and after reading
 * SYSTIML, may be NULL
 **/
u64 e1000e_read_systim(struct e1000_adapter *adapter,
		       struct ptp_system_timestamp *sts)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 systimel, systimel_2, systimeh;
	u64 systim;
	/* SYSTIMH latching upon SYSTIML read does not work well.
	 * This means that if SYSTIML overflows after we read it but before
	 * we read SYSTIMH, the value of SYSTIMH has been incremented and we
	 * will experience a huge non linear increment in the systime value
	 * to fix that we test for overflow and if true, we re-read systime.
	 */
	ptp_read_system_prets(sts);
	systimel = er32(SYSTIML);
	ptp_read_system_postts(sts);
	systimeh = er32(SYSTIMH);
	/* Is systimel is so large that overflow is possible? */
	if (systimel >= (u32)0xffffffff - E1000_TIMINCA_INCVALUE_MASK) {
		ptp_read_system_prets(sts);
		systimel_2 = er32(SYSTIML);
		ptp_read_system_postts(sts);
		if (systimel > systimel_2) {
			/* There was an overflow, read again SYSTIMH, and use
			 * systimel_2
			 */
			systimeh = er32(SYSTIMH);
			systimel = systimel_2;
		}
	}
	systim = (u64)systimel;
	systim |= (u64)systimeh << 32;

	if (adapter->flags2 & FLAG2_CHECK_SYSTIM_OVERFLOW)
		systim = e1000e_sanitize_systim(hw, systim, sts);

	return systim;
}

/**
 * e1000e_cyclecounter_read - read raw cycle counter (used by time counter)
 * @cc: cyclecounter structure
 **/
static u64 e1000e_cyclecounter_read(const struct cyclecounter *cc)
{
	struct e1000_adapter *adapter = container_of(cc, struct e1000_adapter,
						     cc);

	return e1000e_read_systim(adapter, NULL);
}

/**
 * e1000_sw_init - Initialize general software structures (struct e1000_adapter)
 * @adapter: board private structure to initialize
 *
 * e1000_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/
static int e1000_sw_init(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	adapter->rx_buffer_len = VLAN_ETH_FRAME_LEN + ETH_FCS_LEN;
	adapter->rx_ps_bsize0 = 128;
	adapter->max_frame_size = netdev->mtu + VLAN_ETH_HLEN + ETH_FCS_LEN;
	adapter->min_frame_size = ETH_ZLEN + ETH_FCS_LEN;
	adapter->tx_ring_count = E1000_DEFAULT_TXD;
	adapter->rx_ring_count = E1000_DEFAULT_RXD;

	spin_lock_init(&adapter->stats64_lock);

	e1000e_set_interrupt_capability(adapter);

	if (e1000_alloc_queues(adapter))
		return -ENOMEM;

	/* Setup hardware time stamping cyclecounter */
	if (adapter->flags & FLAG_HAS_HW_TIMESTAMP) {
		adapter->cc.read = e1000e_cyclecounter_read;
		adapter->cc.mask = CYCLECOUNTER_MASK(64);
		adapter->cc.mult = 1;
		/* cc.shift set in e1000e_get_base_tininca() */

		spin_lock_init(&adapter->systim_lock);
		INIT_WORK(&adapter->tx_hwtstamp_work, e1000e_tx_hwtstamp_work);
	}

	/* Explicitly disable IRQ since the NIC can be in any state. */
	e1000_irq_disable(adapter);

	set_bit(__E1000_DOWN, &adapter->state);
	return 0;
}

/**
 * e1000_intr_msi_test - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/
static irqreturn_t e1000_intr_msi_test(int __always_unused irq, void *data)
{
	struct net_device *netdev = data;
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 icr = er32(ICR);

	e_dbg("icr is %08X\n", icr);
	if (icr & E1000_ICR_RXSEQ) {
		adapter->flags &= ~FLAG_MSI_TEST_FAILED;
		/* Force memory writes to complete before acknowledging the
		 * interrupt is handled.
		 */
		wmb();
	}

	return IRQ_HANDLED;
}

/**
 * e1000_test_msi_interrupt - Returns 0 for successful test
 * @adapter: board private struct
 *
 * code flow taken from tg3.c
 **/
static int e1000_test_msi_interrupt(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	int err;

	/* poll_enable hasn't been called yet, so don't need disable */
	/* clear any pending events */
	er32(ICR);

	/* free the real vector and request a test handler */
	e1000_free_irq(adapter);
	e1000e_reset_interrupt_capability(adapter);

	/* Assume that the test fails, if it succeeds then the test
	 * MSI irq handler will unset this flag
	 */
	adapter->flags |= FLAG_MSI_TEST_FAILED;

	err = pci_enable_msi(adapter->pdev);
	if (err)
		goto msi_test_failed;

	err = request_irq(adapter->pdev->irq, e1000_intr_msi_test, 0,
			  netdev->name, netdev);
	if (err) {
		pci_disable_msi(adapter->pdev);
		goto msi_test_failed;
	}

	/* Force memory writes to complete before enabling and firing an
	 * interrupt.
	 */
	wmb();

	e1000_irq_enable(adapter);

	/* fire an unusual interrupt on the test handler */
	ew32(ICS, E1000_ICS_RXSEQ);
	e1e_flush();
	msleep(100);

	e1000_irq_disable(adapter);

	rmb();			/* read flags after interrupt has been fired */

	if (adapter->flags & FLAG_MSI_TEST_FAILED) {
		adapter->int_mode = E1000E_INT_MODE_LEGACY;
		e_info("MSI interrupt test failed, using legacy interrupt.\n");
	} else {
		e_dbg("MSI interrupt test succeeded!\n");
	}

	free_irq(adapter->pdev->irq, netdev);
	pci_disable_msi(adapter->pdev);

msi_test_failed:
	e1000e_set_interrupt_capability(adapter);
	return e1000_request_irq(adapter);
}

/**
 * e1000_test_msi - Returns 0 if MSI test succeeds or INTx mode is restored
 * @adapter: board private struct
 *
 * code flow taken from tg3.c, called with e1000 interrupts disabled.
 **/
static int e1000_test_msi(struct e1000_adapter *adapter)
{
	int err;
	u16 pci_cmd;

	if (!(adapter->flags & FLAG_MSI_ENABLED))
		return 0;

	/* disable SERR in case the MSI write causes a master abort */
	pci_read_config_word(adapter->pdev, PCI_COMMAND, &pci_cmd);
	if (pci_cmd & PCI_COMMAND_SERR)
		pci_write_config_word(adapter->pdev, PCI_COMMAND,
				      pci_cmd & ~PCI_COMMAND_SERR);

	err = e1000_test_msi_interrupt(adapter);

	/* re-enable SERR */
	if (pci_cmd & PCI_COMMAND_SERR) {
		pci_read_config_word(adapter->pdev, PCI_COMMAND, &pci_cmd);
		pci_cmd |= PCI_COMMAND_SERR;
		pci_write_config_word(adapter->pdev, PCI_COMMAND, pci_cmd);
	}

	return err;
}

/**
 * e1000e_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/
int e1000e_open(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	int err;

	/* disallow open during test */
	if (test_bit(__E1000_TESTING, &adapter->state))
		return -EBUSY;

	pm_runtime_get_sync(&pdev->dev);

	netif_carrier_off(netdev);

	/* allocate transmit descriptors */
	err = e1000e_setup_tx_resources(adapter->tx_ring);
	if (err)
		goto err_setup_tx;

	/* allocate receive descriptors */
	err = e1000e_setup_rx_resources(adapter->rx_ring);
	if (err)
		goto err_setup_rx;

	/* If AMT is enabled, let the firmware know that the network
	 * interface is now open and reset the part to a known state.
	 */
	if (adapter->flags & FLAG_HAS_AMT) {
		e1000e_get_hw_control(adapter);
		e1000e_reset(adapter);
	}

	e1000e_power_up_phy(adapter);

	adapter->mng_vlan_id = E1000_MNG_VLAN_NONE;
	if ((adapter->hw.mng_cookie.status & E1000_MNG_DHCP_COOKIE_STATUS_VLAN))
		e1000_update_mng_vlan(adapter);

	/* DMA latency requirement to workaround jumbo issue */
	pm_qos_add_request(&adapter->pm_qos_req, PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_DEFAULT_VALUE);

	/* before we allocate an interrupt, we must be ready to handle it.
	 * Setting DEBUG_SHIRQ in the kernel makes it fire an interrupt
	 * as soon as we call pci_request_irq, so we have to setup our
	 * clean_rx handler before we do so.
	 */
	e1000_configure(adapter);

	err = e1000_request_irq(adapter);
	if (err)
		goto err_req_irq;

	/* Work around PCIe errata with MSI interrupts causing some chipsets to
	 * ignore e1000e MSI messages, which means we need to test our MSI
	 * interrupt now
	 */
	if (adapter->int_mode != E1000E_INT_MODE_LEGACY) {
		err = e1000_test_msi(adapter);
		if (err) {
			e_err("Interrupt allocation failed\n");
			goto err_req_irq;
		}
	}

	/* From here on the code is the same as e1000e_up() */
	clear_bit(__E1000_DOWN, &adapter->state);

	napi_enable(&adapter->napi);

	e1000_irq_enable(adapter);

	adapter->tx_hang_recheck = false;
	netif_start_queue(netdev);

	hw->mac.get_link_status = true;
	pm_runtime_put(&pdev->dev);

	e1000e_trigger_lsc(adapter);

	return 0;

err_req_irq:
	pm_qos_remove_request(&adapter->pm_qos_req);
	e1000e_release_hw_control(adapter);
	e1000_power_down_phy(adapter);
	e1000e_free_rx_resources(adapter->rx_ring);
err_setup_rx:
	e1000e_free_tx_resources(adapter->tx_ring);
err_setup_tx:
	e1000e_reset(adapter);
	pm_runtime_put_sync(&pdev->dev);

	return err;
}

/**
 * e1000e_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/
int e1000e_close(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;
	int count = E1000_CHECK_RESET_COUNT;

	while (test_bit(__E1000_RESETTING, &adapter->state) && count--)
		usleep_range(10000, 20000);

	WARN_ON(test_bit(__E1000_RESETTING, &adapter->state));

	pm_runtime_get_sync(&pdev->dev);

	if (!test_bit(__E1000_DOWN, &adapter->state)) {
		e1000e_down(adapter, true);
		e1000_free_irq(adapter);

		/* Link status message must follow this format */
		pr_info("%s NIC Link is Down\n", adapter->netdev->name);
	}

	napi_disable(&adapter->napi);

	e1000e_free_tx_resources(adapter->tx_ring);
	e1000e_free_rx_resources(adapter->rx_ring);

	/* kill manageability vlan ID if supported, but not if a vlan with
	 * the same ID is registered on the host OS (let 8021q kill it)
	 */
	if (adapter->hw.mng_cookie.status & E1000_MNG_DHCP_COOKIE_STATUS_VLAN)
		e1000_vlan_rx_kill_vid(netdev, htons(ETH_P_8021Q),
				       adapter->mng_vlan_id);

	/* If AMT is enabled, let the firmware know that the network
	 * interface is now closed
	 */
	if ((adapter->flags & FLAG_HAS_AMT) &&
	    !test_bit(__E1000_TESTING, &adapter->state))
		e1000e_release_hw_control(adapter);

	pm_qos_remove_request(&adapter->pm_qos_req);

	pm_runtime_put_sync(&pdev->dev);

	return 0;
}

/**
 * e1000_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int e1000_set_mac(struct net_device *netdev, void *p)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(adapter->hw.mac.addr, addr->sa_data, netdev->addr_len);

	hw->mac.ops.rar_set(&adapter->hw, adapter->hw.mac.addr, 0);

	if (adapter->flags & FLAG_RESET_OVERWRITES_LAA) {
		/* activate the work around */
		e1000e_set_laa_state_82571(&adapter->hw, 1);

		/* Hold a copy of the LAA in RAR[14] This is done so that
		 * between the time RAR[0] gets clobbered  and the time it
		 * gets fixed (in e1000_watchdog), the actual LAA is in one
		 * of the RARs and no incoming packets directed to this port
		 * are dropped. Eventually the LAA will be in RAR[0] and
		 * RAR[14]
		 */
		hw->mac.ops.rar_set(&adapter->hw, adapter->hw.mac.addr,
				    adapter->hw.mac.rar_entry_count - 1);
	}

	return 0;
}

/**
 * e1000e_update_phy_task - work thread to update phy
 * @work: pointer to our work struct
 *
 * this worker thread exists because we must acquire a
 * semaphore to read the phy, which we could msleep while
 * waiting for it, and we can't msleep in a timer.
 **/
static void e1000e_update_phy_task(struct work_struct *work)
{
	struct e1000_adapter *adapter = container_of(work,
						     struct e1000_adapter,
						     update_phy_task);
	struct e1000_hw *hw = &adapter->hw;

	if (test_bit(__E1000_DOWN, &adapter->state))
		return;

	e1000_get_phy_info(hw);

	/* Enable EEE on 82579 after link up */
	if (hw->phy.type >= e1000_phy_82579)
		e1000_set_eee_pchlan(hw);
}

/**
 * e1000_update_phy_info - timre call-back to update PHY info
 * @data: pointer to adapter cast into an unsigned long
 *
 * Need to wait a few seconds after link up to get diagnostic information from
 * the phy
 **/
static void e1000_update_phy_info(struct timer_list *t)
{
	struct e1000_adapter *adapter = from_timer(adapter, t, phy_info_timer);

	if (test_bit(__E1000_DOWN, &adapter->state))
		return;

	schedule_work(&adapter->update_phy_task);
}

/**
 * e1000e_update_phy_stats - Update the PHY statistics counters
 * @adapter: board private structure
 *
 * Read/clear the upper 16-bit PHY registers and read/accumulate lower
 **/
static void e1000e_update_phy_stats(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	s32 ret_val;
	u16 phy_data;

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		return;

	/* A page set is expensive so check if already on desired page.
	 * If not, set to the page with the PHY status registers.
	 */
	hw->phy.addr = 1;
	ret_val = e1000e_read_phy_reg_mdic(hw, IGP01E1000_PHY_PAGE_SELECT,
					   &phy_data);
	if (ret_val)
		goto release;
	if (phy_data != (HV_STATS_PAGE << IGP_PAGE_SHIFT)) {
		ret_val = hw->phy.ops.set_page(hw,
					       HV_STATS_PAGE << IGP_PAGE_SHIFT);
		if (ret_val)
			goto release;
	}

	/* Single Collision Count */
	hw->phy.ops.read_reg_page(hw, HV_SCC_UPPER, &phy_data);
	ret_val = hw->phy.ops.read_reg_page(hw, HV_SCC_LOWER, &phy_data);
	if (!ret_val)
		adapter->stats.scc += phy_data;

	/* Excessive Collision Count */
	hw->phy.ops.read_reg_page(hw, HV_ECOL_UPPER, &phy_data);
	ret_val = hw->phy.ops.read_reg_page(hw, HV_ECOL_LOWER, &phy_data);
	if (!ret_val)
		adapter->stats.ecol += phy_data;

	/* Multiple Collision Count */
	hw->phy.ops.read_reg_page(hw, HV_MCC_UPPER, &phy_data);
	ret_val = hw->phy.ops.read_reg_page(hw, HV_MCC_LOWER, &phy_data);
	if (!ret_val)
		adapter->stats.mcc += phy_data;

	/* Late Collision Count */
	hw->phy.ops.read_reg_page(hw, HV_LATECOL_UPPER, &phy_data);
	ret_val = hw->phy.ops.read_reg_page(hw, HV_LATECOL_LOWER, &phy_data);
	if (!ret_val)
		adapter->stats.latecol += phy_data;

	/* Collision Count - also used for adaptive IFS */
	hw->phy.ops.read_reg_page(hw, HV_COLC_UPPER, &phy_data);
	ret_val = hw->phy.ops.read_reg_page(hw, HV_COLC_LOWER, &phy_data);
	if (!ret_val)
		hw->mac.collision_delta = phy_data;

	/* Defer Count */
	hw->phy.ops.read_reg_page(hw, HV_DC_UPPER, &phy_data);
	ret_val = hw->phy.ops.read_reg_page(hw, HV_DC_LOWER, &phy_data);
	if (!ret_val)
		adapter->stats.dc += phy_data;

	/* Transmit with no CRS */
	hw->phy.ops.read_reg_page(hw, HV_TNCRS_UPPER, &phy_data);
	ret_val = hw->phy.ops.read_reg_page(hw, HV_TNCRS_LOWER, &phy_data);
	if (!ret_val)
		adapter->stats.tncrs += phy_data;

release:
	hw->phy.ops.release(hw);
}

/**
 * e1000e_update_stats - Update the board statistics counters
 * @adapter: board private structure
 **/
static void e1000e_update_stats(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;

	/* Prevent stats update while adapter is being reset, or if the pci
	 * connection is down.
	 */
	if (adapter->link_speed == 0)
		return;
	if (pci_channel_offline(pdev))
		return;

	adapter->stats.crcerrs += er32(CRCERRS);
	adapter->stats.gprc += er32(GPRC);
	adapter->stats.gorc += er32(GORCL);
	er32(GORCH);		/* Clear gorc */
	adapter->stats.bprc += er32(BPRC);
	adapter->stats.mprc += er32(MPRC);
	adapter->stats.roc += er32(ROC);

	adapter->stats.mpc += er32(MPC);

	/* Half-duplex statistics */
	if (adapter->link_duplex == HALF_DUPLEX) {
		if (adapter->flags2 & FLAG2_HAS_PHY_STATS) {
			e1000e_update_phy_stats(adapter);
		} else {
			adapter->stats.scc += er32(SCC);
			adapter->stats.ecol += er32(ECOL);
			adapter->stats.mcc += er32(MCC);
			adapter->stats.latecol += er32(LATECOL);
			adapter->stats.dc += er32(DC);

			hw->mac.collision_delta = er32(COLC);

			if ((hw->mac.type != e1000_82574) &&
			    (hw->mac.type != e1000_82583))
				adapter->stats.tncrs += er32(TNCRS);
		}
		adapter->stats.colc += hw->mac.collision_delta;
	}

	adapter->stats.xonrxc += er32(XONRXC);
	adapter->stats.xontxc += er32(XONTXC);
	adapter->stats.xoffrxc += er32(XOFFRXC);
	adapter->stats.xofftxc += er32(XOFFTXC);
	adapter->stats.gptc += er32(GPTC);
	adapter->stats.gotc += er32(GOTCL);
	er32(GOTCH);		/* Clear gotc */
	adapter->stats.rnbc += er32(RNBC);
	adapter->stats.ruc += er32(RUC);

	adapter->stats.mptc += er32(MPTC);
	adapter->stats.bptc += er32(BPTC);

	/* used for adaptive IFS */

	hw->mac.tx_packet_delta = er32(TPT);
	adapter->stats.tpt += hw->mac.tx_packet_delta;

	adapter->stats.algnerrc += er32(ALGNERRC);
	adapter->stats.rxerrc += er32(RXERRC);
	adapter->stats.cexterr += er32(CEXTERR);
	adapter->stats.tsctc += er32(TSCTC);
	adapter->stats.tsctfc += er32(TSCTFC);

	/* Fill out the OS statistics structure */
	netdev->stats.multicast = adapter->stats.mprc;
	netdev->stats.collisions = adapter->stats.colc;

	/* Rx Errors */

	/* RLEC on some newer hardware can be incorrect so build
	 * our own version based on RUC and ROC
	 */
	netdev->stats.rx_errors = adapter->stats.rxerrc +
	    adapter->stats.crcerrs + adapter->stats.algnerrc +
	    adapter->stats.ruc + adapter->stats.roc + adapter->stats.cexterr;
	netdev->stats.rx_length_errors = adapter->stats.ruc +
	    adapter->stats.roc;
	netdev->stats.rx_crc_errors = adapter->stats.crcerrs;
	netdev->stats.rx_frame_errors = adapter->stats.algnerrc;
	netdev->stats.rx_missed_errors = adapter->stats.mpc;

	/* Tx Errors */
	netdev->stats.tx_errors = adapter->stats.ecol + adapter->stats.latecol;
	netdev->stats.tx_aborted_errors = adapter->stats.ecol;
	netdev->stats.tx_window_errors = adapter->stats.latecol;
	netdev->stats.tx_carrier_errors = adapter->stats.tncrs;

	/* Tx Dropped needs to be maintained elsewhere */

	/* Management Stats */
	adapter->stats.mgptc += er32(MGTPTC);
	adapter->stats.mgprc += er32(MGTPRC);
	adapter->stats.mgpdc += er32(MGTPDC);

	/* Correctable ECC Errors */
	if (hw->mac.type >= e1000_pch_lpt) {
		u32 pbeccsts = er32(PBECCSTS);

		adapter->corr_errors +=
		    pbeccsts & E1000_PBECCSTS_CORR_ERR_CNT_MASK;
		adapter->uncorr_errors +=
		    (pbeccsts & E1000_PBECCSTS_UNCORR_ERR_CNT_MASK) >>
		    E1000_PBECCSTS_UNCORR_ERR_CNT_SHIFT;
	}
}

/**
 * e1000_phy_read_status - Update the PHY register status snapshot
 * @adapter: board private structure
 **/
static void e1000_phy_read_status(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_phy_regs *phy = &adapter->phy_regs;

	if (!pm_runtime_suspended((&adapter->pdev->dev)->parent) &&
	    (er32(STATUS) & E1000_STATUS_LU) &&
	    (adapter->hw.phy.media_type == e1000_media_type_copper)) {
		int ret_val;

		ret_val = e1e_rphy(hw, MII_BMCR, &phy->bmcr);
		ret_val |= e1e_rphy(hw, MII_BMSR, &phy->bmsr);
		ret_val |= e1e_rphy(hw, MII_ADVERTISE, &phy->advertise);
		ret_val |= e1e_rphy(hw, MII_LPA, &phy->lpa);
		ret_val |= e1e_rphy(hw, MII_EXPANSION, &phy->expansion);
		ret_val |= e1e_rphy(hw, MII_CTRL1000, &phy->ctrl1000);
		ret_val |= e1e_rphy(hw, MII_STAT1000, &phy->stat1000);
		ret_val |= e1e_rphy(hw, MII_ESTATUS, &phy->estatus);
		if (ret_val)
			e_warn("Error reading PHY register\n");
	} else {
		/* Do not read PHY registers if link is not up
		 * Set values to typical power-on defaults
		 */
		phy->bmcr = (BMCR_SPEED1000 | BMCR_ANENABLE | BMCR_FULLDPLX);
		phy->bmsr = (BMSR_100FULL | BMSR_100HALF | BMSR_10FULL |
			     BMSR_10HALF | BMSR_ESTATEN | BMSR_ANEGCAPABLE |
			     BMSR_ERCAP);
		phy->advertise = (ADVERTISE_PAUSE_ASYM | ADVERTISE_PAUSE_CAP |
				  ADVERTISE_ALL | ADVERTISE_CSMA);
		phy->lpa = 0;
		phy->expansion = EXPANSION_ENABLENPAGE;
		phy->ctrl1000 = ADVERTISE_1000FULL;
		phy->stat1000 = 0;
		phy->estatus = (ESTATUS_1000_TFULL | ESTATUS_1000_THALF);
	}
}

static void e1000_print_link_info(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl = er32(CTRL);

	/* Link status message must follow this format for user tools */
	pr_info("%s NIC Link is Up %d Mbps %s Duplex, Flow Control: %s\n",
		adapter->netdev->name, adapter->link_speed,
		adapter->link_duplex == FULL_DUPLEX ? "Full" : "Half",
		(ctrl & E1000_CTRL_TFCE) && (ctrl & E1000_CTRL_RFCE) ? "Rx/Tx" :
		(ctrl & E1000_CTRL_RFCE) ? "Rx" :
		(ctrl & E1000_CTRL_TFCE) ? "Tx" : "None");
}

static bool e1000e_has_link(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	bool link_active = false;
	s32 ret_val = 0;

	/* get_link_status is set on LSC (link status) interrupt or
	 * Rx sequence error interrupt.  get_link_status will stay
	 * true until the check_for_link establishes link
	 * for copper adapters ONLY
	 */
	switch (hw->phy.media_type) {
	case e1000_media_type_copper:
		if (hw->mac.get_link_status) {
			ret_val = hw->mac.ops.check_for_link(hw);
			link_active = !hw->mac.get_link_status;
		} else {
			link_active = true;
		}
		break;
	case e1000_media_type_fiber:
		ret_val = hw->mac.ops.check_for_link(hw);
		link_active = !!(er32(STATUS) & E1000_STATUS_LU);
		break;
	case e1000_media_type_internal_serdes:
		ret_val = hw->mac.ops.check_for_link(hw);
		link_active = hw->mac.serdes_has_link;
		break;
	default:
	case e1000_media_type_unknown:
		break;
	}

	if ((ret_val == -E1000_ERR_PHY) && (hw->phy.type == e1000_phy_igp_3) &&
	    (er32(CTRL) & E1000_PHY_CTRL_GBE_DISABLE)) {
		/* See e1000_kmrn_lock_loss_workaround_ich8lan() */
		e_info("Gigabit has been disabled, downgrading speed\n");
	}

	return link_active;
}

static void e1000e_enable_receives(struct e1000_adapter *adapter)
{
	/* make sure the receive unit is started */
	if ((adapter->flags & FLAG_RX_NEEDS_RESTART) &&
	    (adapter->flags & FLAG_RESTART_NOW)) {
		struct e1000_hw *hw = &adapter->hw;
		u32 rctl = er32(RCTL);

		ew32(RCTL, rctl | E1000_RCTL_EN);
		adapter->flags &= ~FLAG_RESTART_NOW;
	}
}

static void e1000e_check_82574_phy_workaround(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	/* With 82574 controllers, PHY needs to be checked periodically
	 * for hung state and reset, if two calls return true
	 */
	if (e1000_check_phy_82574(hw))
		adapter->phy_hang_count++;
	else
		adapter->phy_hang_count = 0;

	if (adapter->phy_hang_count > 1) {
		adapter->phy_hang_count = 0;
		e_dbg("PHY appears hung - resetting\n");
		schedule_work(&adapter->reset_task);
	}
}

/**
 * e1000_watchdog - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/
static void e1000_watchdog(struct timer_list *t)
{
	struct e1000_adapter *adapter = from_timer(adapter, t, watchdog_timer);

	/* Do the rest outside of interrupt context */
	schedule_work(&adapter->watchdog_task);

	/* TODO: make this use queue_delayed_work() */
}

static void e1000_watchdog_task(struct work_struct *work)
{
	struct e1000_adapter *adapter = container_of(work,
						     struct e1000_adapter,
						     watchdog_task);
	struct net_device *netdev = adapter->netdev;
	struct e1000_mac_info *mac = &adapter->hw.mac;
	struct e1000_phy_info *phy = &adapter->hw.phy;
	struct e1000_ring *tx_ring = adapter->tx_ring;
	struct e1000_hw *hw = &adapter->hw;
	u32 link, tctl;

	if (test_bit(__E1000_DOWN, &adapter->state))
		return;

	link = e1000e_has_link(adapter);
	if ((netif_carrier_ok(netdev)) && link) {
		/* Cancel scheduled suspend requests. */
		pm_runtime_resume(netdev->dev.parent);

		e1000e_enable_receives(adapter);
		goto link_up;
	}

	if ((e1000e_enable_tx_pkt_filtering(hw)) &&
	    (adapter->mng_vlan_id != adapter->hw.mng_cookie.vlan_id))
		e1000_update_mng_vlan(adapter);

	if (link) {
		if (!netif_carrier_ok(netdev)) {
			bool txb2b = true;

			/* Cancel scheduled suspend requests. */
			pm_runtime_resume(netdev->dev.parent);

			/* update snapshot of PHY registers on LSC */
			e1000_phy_read_status(adapter);
			mac->ops.get_link_up_info(&adapter->hw,
						  &adapter->link_speed,
						  &adapter->link_duplex);
			e1000_print_link_info(adapter);

			/* check if SmartSpeed worked */
			e1000e_check_downshift(hw);
			if (phy->speed_downgraded)
				netdev_warn(netdev,
					    "Link Speed was downgraded by SmartSpeed\n");

			/* On supported PHYs, check for duplex mismatch only
			 * if link has autonegotiated at 10/100 half
			 */
			if ((hw->phy.type == e1000_phy_igp_3 ||
			     hw->phy.type == e1000_phy_bm) &&
			    hw->mac.autoneg &&
			    (adapter->link_speed == SPEED_10 ||
			     adapter->link_speed == SPEED_100) &&
			    (adapter->link_duplex == HALF_DUPLEX)) {
				u16 autoneg_exp;

				e1e_rphy(hw, MII_EXPANSION, &autoneg_exp);

				if (!(autoneg_exp & EXPANSION_NWAY))
					e_info("Autonegotiated half duplex but link partner cannot autoneg.  Try forcing full duplex if link gets many collisions.\n");
			}

			/* adjust timeout factor according to speed/duplex */
			adapter->tx_timeout_factor = 1;
			switch (adapter->link_speed) {
			case SPEED_10:
				txb2b = false;
				adapter->tx_timeout_factor = 16;
				break;
			case SPEED_100:
				txb2b = false;
				adapter->tx_timeout_factor = 10;
				break;
			}

			/* workaround: re-program speed mode bit after
			 * link-up event
			 */
			if ((adapter->flags & FLAG_TARC_SPEED_MODE_BIT) &&
			    !txb2b) {
				u32 tarc0;

				tarc0 = er32(TARC(0));
				tarc0 &= ~SPEED_MODE_BIT;
				ew32(TARC(0), tarc0);
			}

			/* disable TSO for pcie and 10/100 speeds, to avoid
			 * some hardware issues
			 */
			if (!(adapter->flags & FLAG_TSO_FORCE)) {
				switch (adapter->link_speed) {
				case SPEED_10:
				case SPEED_100:
					e_info("10/100 speed: disabling TSO\n");
					netdev->features &= ~NETIF_F_TSO;
					netdev->features &= ~NETIF_F_TSO6;
					break;
				case SPEED_1000:
					netdev->features |= NETIF_F_TSO;
					netdev->features |= NETIF_F_TSO6;
					break;
				default:
					/* oops */
					break;
				}
			}

			/* enable transmits in the hardware, need to do this
			 * after setting TARC(0)
			 */
			tctl = er32(TCTL);
			tctl |= E1000_TCTL_EN;
			ew32(TCTL, tctl);

			/* Perform any post-link-up configuration before
			 * reporting link up.
			 */
			if (phy->ops.cfg_on_link_up)
				phy->ops.cfg_on_link_up(hw);

			netif_carrier_on(netdev);

			if (!test_bit(__E1000_DOWN, &adapter->state))
				mod_timer(&adapter->phy_info_timer,
					  round_jiffies(jiffies + 2 * HZ));
		}
	} else {
		if (netif_carrier_ok(netdev)) {
			adapter->link_speed = 0;
			adapter->link_duplex = 0;
			/* Link status message must follow this format */
			pr_info("%s NIC Link is Down\n", adapter->netdev->name);
			netif_carrier_off(netdev);
			if (!test_bit(__E1000_DOWN, &adapter->state))
				mod_timer(&adapter->phy_info_timer,
					  round_jiffies(jiffies + 2 * HZ));

			/* 8000ES2LAN requires a Rx packet buffer work-around
			 * on link down event; reset the controller to flush
			 * the Rx packet buffer.
			 */
			if (adapter->flags & FLAG_RX_NEEDS_RESTART)
				adapter->flags |= FLAG_RESTART_NOW;
			else
				pm_schedule_suspend(netdev->dev.parent,
						    LINK_TIMEOUT);
		}
	}

link_up:
	spin_lock(&adapter->stats64_lock);
	e1000e_update_stats(adapter);

	mac->tx_packet_delta = adapter->stats.tpt - adapter->tpt_old;
	adapter->tpt_old = adapter->stats.tpt;
	mac->collision_delta = adapter->stats.colc - adapter->colc_old;
	adapter->colc_old = adapter->stats.colc;

	adapter->gorc = adapter->stats.gorc - adapter->gorc_old;
	adapter->gorc_old = adapter->stats.gorc;
	adapter->gotc = adapter->stats.gotc - adapter->gotc_old;
	adapter->gotc_old = adapter->stats.gotc;
	spin_unlock(&adapter->stats64_lock);

	/* If the link is lost the controller stops DMA, but
	 * if there is queued Tx work it cannot be done.  So
	 * reset the controller to flush the Tx packet buffers.
	 */
	if (!netif_carrier_ok(netdev) &&
	    (e1000_desc_unused(tx_ring) + 1 < tx_ring->count))
		adapter->flags |= FLAG_RESTART_NOW;

	/* If reset is necessary, do it outside of interrupt context. */
	if (adapter->flags & FLAG_RESTART_NOW) {
		schedule_work(&adapter->reset_task);
		/* return immediately since reset is imminent */
		return;
	}

	e1000e_update_adaptive(&adapter->hw);

	/* Simple mode for Interrupt Throttle Rate (ITR) */
	if (adapter->itr_setting == 4) {
		/* Symmetric Tx/Rx gets a reduced ITR=2000;
		 * Total asymmetrical Tx or Rx gets ITR=8000;
		 * everyone else is between 2000-8000.
		 */
		u32 goc = (adapter->gotc + adapter->gorc) / 10000;
		u32 dif = (adapter->gotc > adapter->gorc ?
			   adapter->gotc - adapter->gorc :
			   adapter->gorc - adapter->gotc) / 10000;
		u32 itr = goc > 0 ? (dif * 6000 / goc + 2000) : 8000;

		e1000e_write_itr(adapter, itr);
	}

	/* Cause software interrupt to ensure Rx ring is cleaned */
	if (adapter->msix_entries)
		ew32(ICS, adapter->rx_ring->ims_val);
	else
		ew32(ICS, E1000_ICS_RXDMT0);

	/* flush pending descriptors to memory before detecting Tx hang */
	e1000e_flush_descriptors(adapter);

	/* Force detection of hung controller every watchdog period */
	adapter->detect_tx_hung = true;

	/* With 82571 controllers, LAA may be overwritten due to controller
	 * reset from the other port. Set the appropriate LAA in RAR[0]
	 */
	if (e1000e_get_laa_state_82571(hw))
		hw->mac.ops.rar_set(hw, adapter->hw.mac.addr, 0);

	if (adapter->flags2 & FLAG2_CHECK_PHY_HANG)
		e1000e_check_82574_phy_workaround(adapter);

	/* Clear valid timestamp stuck in RXSTMPL/H due to a Rx error */
	if (adapter->hwtstamp_config.rx_filter != HWTSTAMP_FILTER_NONE) {
		if ((adapter->flags2 & FLAG2_CHECK_RX_HWTSTAMP) &&
		    (er32(TSYNCRXCTL) & E1000_TSYNCRXCTL_VALID)) {
			er32(RXSTMPH);
			adapter->rx_hwtstamp_cleared++;
		} else {
			adapter->flags2 |= FLAG2_CHECK_RX_HWTSTAMP;
		}
	}

	/* Reset the timer */
	if (!test_bit(__E1000_DOWN, &adapter->state))
		mod_timer(&adapter->watchdog_timer,
			  round_jiffies(jiffies + 2 * HZ));
}

#define E1000_TX_FLAGS_CSUM		0x00000001
#define E1000_TX_FLAGS_VLAN		0x00000002
#define E1000_TX_FLAGS_TSO		0x00000004
#define E1000_TX_FLAGS_IPV4		0x00000008
#define E1000_TX_FLAGS_NO_FCS		0x00000010
#define E1000_TX_FLAGS_HWTSTAMP		0x00000020
#define E1000_TX_FLAGS_VLAN_MASK	0xffff0000
#define E1000_TX_FLAGS_VLAN_SHIFT	16

static int e1000_tso(struct e1000_ring *tx_ring, struct sk_buff *skb,
		     __be16 protocol)
{
	struct e1000_context_desc *context_desc;
	struct e1000_buffer *buffer_info;
	unsigned int i;
	u32 cmd_length = 0;
	u16 ipcse = 0, mss;
	u8 ipcss, ipcso, tucss, tucso, hdr_len;
	int err;

	if (!skb_is_gso(skb))
		return 0;

	err = skb_cow_head(skb, 0);
	if (err < 0)
		return err;

	hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
	mss = skb_shinfo(skb)->gso_size;
	if (protocol == htons(ETH_P_IP)) {
		struct iphdr *iph = ip_hdr(skb);
		iph->tot_len = 0;
		iph->check = 0;
		tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr, iph->daddr,
							 0, IPPROTO_TCP, 0);
		cmd_length = E1000_TXD_CMD_IP;
		ipcse = skb_transport_offset(skb) - 1;
	} else if (skb_is_gso_v6(skb)) {
		ipv6_hdr(skb)->payload_len = 0;
		tcp_hdr(skb)->check = ~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						       &ipv6_hdr(skb)->daddr,
						       0, IPPROTO_TCP, 0);
		ipcse = 0;
	}
	ipcss = skb_network_offset(skb);
	ipcso = (void *)&(ip_hdr(skb)->check) - (void *)skb->data;
	tucss = skb_transport_offset(skb);
	tucso = (void *)&(tcp_hdr(skb)->check) - (void *)skb->data;

	cmd_length |= (E1000_TXD_CMD_DEXT | E1000_TXD_CMD_TSE |
		       E1000_TXD_CMD_TCP | (skb->len - (hdr_len)));

	i = tx_ring->next_to_use;
	context_desc = E1000_CONTEXT_DESC(*tx_ring, i);
	buffer_info = &tx_ring->buffer_info[i];

	context_desc->lower_setup.ip_fields.ipcss = ipcss;
	context_desc->lower_setup.ip_fields.ipcso = ipcso;
	context_desc->lower_setup.ip_fields.ipcse = cpu_to_le16(ipcse);
	context_desc->upper_setup.tcp_fields.tucss = tucss;
	context_desc->upper_setup.tcp_fields.tucso = tucso;
	context_desc->upper_setup.tcp_fields.tucse = 0;
	context_desc->tcp_seg_setup.fields.mss = cpu_to_le16(mss);
	context_desc->tcp_seg_setup.fields.hdr_len = hdr_len;
	context_desc->cmd_and_length = cpu_to_le32(cmd_length);

	buffer_info->time_stamp = jiffies;
	buffer_info->next_to_watch = i;

	i++;
	if (i == tx_ring->count)
		i = 0;
	tx_ring->next_to_use = i;

	return 1;
}

static bool e1000_tx_csum(struct e1000_ring *tx_ring, struct sk_buff *skb,
			  __be16 protocol)
{
	struct e1000_adapter *adapter = tx_ring->adapter;
	struct e1000_context_desc *context_desc;
	struct e1000_buffer *buffer_info;
	unsigned int i;
	u8 css;
	u32 cmd_len = E1000_TXD_CMD_DEXT;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return false;

	switch (protocol) {
	case cpu_to_be16(ETH_P_IP):
		if (ip_hdr(skb)->protocol == IPPROTO_TCP)
			cmd_len |= E1000_TXD_CMD_TCP;
		break;
	case cpu_to_be16(ETH_P_IPV6):
		/* XXX not handling all IPV6 headers */
		if (ipv6_hdr(skb)->nexthdr == IPPROTO_TCP)
			cmd_len |= E1000_TXD_CMD_TCP;
		break;
	default:
		if (unlikely(net_ratelimit()))
			e_warn("checksum_partial proto=%x!\n",
			       be16_to_cpu(protocol));
		break;
	}

	css = skb_checksum_start_offset(skb);

	i = tx_ring->next_to_use;
	buffer_info = &tx_ring->buffer_info[i];
	context_desc = E1000_CONTEXT_DESC(*tx_ring, i);

	context_desc->lower_setup.ip_config = 0;
	context_desc->upper_setup.tcp_fields.tucss = css;
	context_desc->upper_setup.tcp_fields.tucso = css + skb->csum_offset;
	context_desc->upper_setup.tcp_fields.tucse = 0;
	context_desc->tcp_seg_setup.data = 0;
	context_desc->cmd_and_length = cpu_to_le32(cmd_len);

	buffer_info->time_stamp = jiffies;
	buffer_info->next_to_watch = i;

	i++;
	if (i == tx_ring->count)
		i = 0;
	tx_ring->next_to_use = i;

	return true;
}

static int e1000_tx_map(struct e1000_ring *tx_ring, struct sk_buff *skb,
			unsigned int first, unsigned int max_per_txd,
			unsigned int nr_frags)
{
	struct e1000_adapter *adapter = tx_ring->adapter;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_buffer *buffer_info;
	unsigned int len = skb_headlen(skb);
	unsigned int offset = 0, size, count = 0, i;
	unsigned int f, bytecount, segs;

	i = tx_ring->next_to_use;

	while (len) {
		buffer_info = &tx_ring->buffer_info[i];
		size = min(len, max_per_txd);

		buffer_info->length = size;
		buffer_info->time_stamp = jiffies;
		buffer_info->next_to_watch = i;
		buffer_info->dma = dma_map_single(&pdev->dev,
						  skb->data + offset,
						  size, DMA_TO_DEVICE);
		buffer_info->mapped_as_page = false;
		if (dma_mapping_error(&pdev->dev, buffer_info->dma))
			goto dma_error;

		len -= size;
		offset += size;
		count++;

		if (len) {
			i++;
			if (i == tx_ring->count)
				i = 0;
		}
	}

	for (f = 0; f < nr_frags; f++) {
		const struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[f];
		len = skb_frag_size(frag);
		offset = 0;

		while (len) {
			i++;
			if (i == tx_ring->count)
				i = 0;

			buffer_info = &tx_ring->buffer_info[i];
			size = min(len, max_per_txd);

			buffer_info->length = size;
			buffer_info->time_stamp = jiffies;
			buffer_info->next_to_watch = i;
			buffer_info->dma = skb_frag_dma_map(&pdev->dev, frag,
							    offset, size,
							    DMA_TO_DEVICE);
			buffer_info->mapped_as_page = true;
			if (dma_mapping_error(&pdev->dev, buffer_info->dma))
				goto dma_error;

			len -= size;
			offset += size;
			count++;
		}
	}

	segs = skb_shinfo(skb)->gso_segs ? : 1;
	/* multiply data chunks by size of headers */
	bytecount = ((segs - 1) * skb_headlen(skb)) + skb->len;

	tx_ring->buffer_info[i].skb = skb;
	tx_ring->buffer_info[i].segs = segs;
	tx_ring->buffer_info[i].bytecount = bytecount;
	tx_ring->buffer_info[first].next_to_watch = i;

	return count;

dma_error:
	dev_err(&pdev->dev, "Tx DMA map failed\n");
	buffer_info->dma = 0;
	if (count)
		count--;

	while (count--) {
		if (i == 0)
			i += tx_ring->count;
		i--;
		buffer_info = &tx_ring->buffer_info[i];
		e1000_put_txbuf(tx_ring, buffer_info, true);
	}

	return 0;
}

static void e1000_tx_queue(struct e1000_ring *tx_ring, int tx_flags, int count)
{
	struct e1000_adapter *adapter = tx_ring->adapter;
	struct e1000_tx_desc *tx_desc = NULL;
	struct e1000_buffer *buffer_info;
	u32 txd_upper = 0, txd_lower = E1000_TXD_CMD_IFCS;
	unsigned int i;

	if (tx_flags & E1000_TX_FLAGS_TSO) {
		txd_lower |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D |
		    E1000_TXD_CMD_TSE;
		txd_upper |= E1000_TXD_POPTS_TXSM << 8;

		if (tx_flags & E1000_TX_FLAGS_IPV4)
			txd_upper |= E1000_TXD_POPTS_IXSM << 8;
	}

	if (tx_flags & E1000_TX_FLAGS_CSUM) {
		txd_lower |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;
		txd_upper |= E1000_TXD_POPTS_TXSM << 8;
	}

	if (tx_flags & E1000_TX_FLAGS_VLAN) {
		txd_lower |= E1000_TXD_CMD_VLE;
		txd_upper |= (tx_flags & E1000_TX_FLAGS_VLAN_MASK);
	}

	if (unlikely(tx_flags & E1000_TX_FLAGS_NO_FCS))
		txd_lower &= ~(E1000_TXD_CMD_IFCS);

	if (unlikely(tx_flags & E1000_TX_FLAGS_HWTSTAMP)) {
		txd_lower |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;
		txd_upper |= E1000_TXD_EXTCMD_TSTAMP;
	}

	i = tx_ring->next_to_use;

	do {
		buffer_info = &tx_ring->buffer_info[i];
		tx_desc = E1000_TX_DESC(*tx_ring, i);
		tx_desc->buffer_addr = cpu_to_le64(buffer_info->dma);
		tx_desc->lower.data = cpu_to_le32(txd_lower |
						  buffer_info->length);
		tx_desc->upper.data = cpu_to_le32(txd_upper);

		i++;
		if (i == tx_ring->count)
			i = 0;
	} while (--count > 0);

	tx_desc->lower.data |= cpu_to_le32(adapter->txd_cmd);

	/* txd_cmd re-enables FCS, so we'll re-disable it here as desired. */
	if (unlikely(tx_flags & E1000_TX_FLAGS_NO_FCS))
		tx_desc->lower.data &= ~(cpu_to_le32(E1000_TXD_CMD_IFCS));

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();

	tx_ring->next_to_use = i;
}

#define MINIMUM_DHCP_PACKET_SIZE 282
static int e1000_transfer_dhcp_info(struct e1000_adapter *adapter,
				    struct sk_buff *skb)
{
	struct e1000_hw *hw = &adapter->hw;
	u16 length, offset;

	if (skb_vlan_tag_present(skb) &&
	    !((skb_vlan_tag_get(skb) == adapter->hw.mng_cookie.vlan_id) &&
	      (adapter->hw.mng_cookie.status &
	       E1000_MNG_DHCP_COOKIE_STATUS_VLAN)))
		return 0;

	if (skb->len <= MINIMUM_DHCP_PACKET_SIZE)
		return 0;

	if (((struct ethhdr *)skb->data)->h_proto != htons(ETH_P_IP))
		return 0;

	{
		const struct iphdr *ip = (struct iphdr *)((u8 *)skb->data + 14);
		struct udphdr *udp;

		if (ip->protocol != IPPROTO_UDP)
			return 0;

		udp = (struct udphdr *)((u8 *)ip + (ip->ihl << 2));
		if (ntohs(udp->dest) != 67)
			return 0;

		offset = (u8 *)udp + 8 - skb->data;
		length = skb->len - offset;
		return e1000e_mng_write_dhcp_info(hw, (u8 *)udp + 8, length);
	}

	return 0;
}

static int __e1000_maybe_stop_tx(struct e1000_ring *tx_ring, int size)
{
	struct e1000_adapter *adapter = tx_ring->adapter;

	netif_stop_queue(adapter->netdev);
	/* Herbert's original patch had:
	 *  smp_mb__after_netif_stop_queue();
	 * but since that doesn't exist yet, just open code it.
	 */
	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available.
	 */
	if (e1000_desc_unused(tx_ring) < size)
		return -EBUSY;

	/* A reprieve! */
	netif_start_queue(adapter->netdev);
	++adapter->restart_queue;
	return 0;
}

static int e1000_maybe_stop_tx(struct e1000_ring *tx_ring, int size)
{
	BUG_ON(size > tx_ring->count);

	if (e1000_desc_unused(tx_ring) >= size)
		return 0;
	return __e1000_maybe_stop_tx(tx_ring, size);
}

static netdev_tx_t e1000_xmit_frame(struct sk_buff *skb,
				    struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_ring *tx_ring = adapter->tx_ring;
	unsigned int first;
	unsigned int tx_flags = 0;
	unsigned int len = skb_headlen(skb);
	unsigned int nr_frags;
	unsigned int mss;
	int count = 0;
	int tso;
	unsigned int f;
	__be16 protocol = vlan_get_protocol(skb);

	if (test_bit(__E1000_DOWN, &adapter->state)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (skb->len <= 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* The minimum packet size with TCTL.PSP set is 17 bytes so
	 * pad skb in order to meet this minimum size requirement
	 */
	if (skb_put_padto(skb, 17))
		return NETDEV_TX_OK;

	mss = skb_shinfo(skb)->gso_size;
	if (mss) {
		u8 hdr_len;

		/* TSO Workaround for 82571/2/3 Controllers -- if skb->data
		 * points to just header, pull a few bytes of payload from
		 * frags into skb->data
		 */
		hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		/* we do this workaround for ES2LAN, but it is un-necessary,
		 * avoiding it could save a lot of cycles
		 */
		if (skb->data_len && (hdr_len == len)) {
			unsigned int pull_size;

			pull_size = min_t(unsigned int, 4, skb->data_len);
			if (!__pskb_pull_tail(skb, pull_size)) {
				e_err("__pskb_pull_tail failed.\n");
				dev_kfree_skb_any(skb);
				return NETDEV_TX_OK;
			}
			len = skb_headlen(skb);
		}
	}

	/* reserve a descriptor for the offload context */
	if ((mss) || (skb->ip_summed == CHECKSUM_PARTIAL))
		count++;
	count++;

	count += DIV_ROUND_UP(len, adapter->tx_fifo_limit);

	nr_frags = skb_shinfo(skb)->nr_frags;
	for (f = 0; f < nr_frags; f++)
		count += DIV_ROUND_UP(skb_frag_size(&skb_shinfo(skb)->frags[f]),
				      adapter->tx_fifo_limit);

	if (adapter->hw.mac.tx_pkt_filtering)
		e1000_transfer_dhcp_info(adapter, skb);

	/* need: count + 2 desc gap to keep tail from touching
	 * head, otherwise try next time
	 */
	if (e1000_maybe_stop_tx(tx_ring, count + 2))
		return NETDEV_TX_BUSY;

	if (skb_vlan_tag_present(skb)) {
		tx_flags |= E1000_TX_FLAGS_VLAN;
		tx_flags |= (skb_vlan_tag_get(skb) <<
			     E1000_TX_FLAGS_VLAN_SHIFT);
	}

	first = tx_ring->next_to_use;

	tso = e1000_tso(tx_ring, skb, protocol);
	if (tso < 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (tso)
		tx_flags |= E1000_TX_FLAGS_TSO;
	else if (e1000_tx_csum(tx_ring, skb, protocol))
		tx_flags |= E1000_TX_FLAGS_CSUM;

	/* Old method was to assume IPv4 packet by default if TSO was enabled.
	 * 82571 hardware supports TSO capabilities for IPv6 as well...
	 * no longer assume, we must.
	 */
	if (protocol == htons(ETH_P_IP))
		tx_flags |= E1000_TX_FLAGS_IPV4;

	if (unlikely(skb->no_fcs))
		tx_flags |= E1000_TX_FLAGS_NO_FCS;

	/* if count is 0 then mapping error has occurred */
	count = e1000_tx_map(tx_ring, skb, first, adapter->tx_fifo_limit,
			     nr_frags);
	if (count) {
		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		    (adapter->flags & FLAG_HAS_HW_TIMESTAMP)) {
			if (!adapter->tx_hwtstamp_skb) {
				skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
				tx_flags |= E1000_TX_FLAGS_HWTSTAMP;
				adapter->tx_hwtstamp_skb = skb_get(skb);
				adapter->tx_hwtstamp_start = jiffies;
				schedule_work(&adapter->tx_hwtstamp_work);
			} else {
				adapter->tx_hwtstamp_skipped++;
			}
		}

		skb_tx_timestamp(skb);

		netdev_sent_queue(netdev, skb->len);
		e1000_tx_queue(tx_ring, tx_flags, count);
		/* Make sure there is space in the ring for the next send. */
		e1000_maybe_stop_tx(tx_ring,
				    (MAX_SKB_FRAGS *
				     DIV_ROUND_UP(PAGE_SIZE,
						  adapter->tx_fifo_limit) + 2));

		if (!skb->xmit_more ||
		    netif_xmit_stopped(netdev_get_tx_queue(netdev, 0))) {
			if (adapter->flags2 & FLAG2_PCIM2PCI_ARBITER_WA)
				e1000e_update_tdt_wa(tx_ring,
						     tx_ring->next_to_use);
			else
				writel(tx_ring->next_to_use, tx_ring->tail);

			/* we need this if more than one processor can write
			 * to our tail at a time, it synchronizes IO on
			 *IA64/Altix systems
			 */
			mmiowb();
		}
	} else {
		dev_kfree_skb_any(skb);
		tx_ring->buffer_info[first].time_stamp = 0;
		tx_ring->next_to_use = first;
	}

	return NETDEV_TX_OK;
}

/**
 * e1000_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/
static void e1000_tx_timeout(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	/* Do the reset outside of interrupt context */
	adapter->tx_timeout_count++;
	schedule_work(&adapter->reset_task);
}

static void e1000_reset_task(struct work_struct *work)
{
	struct e1000_adapter *adapter;
	adapter = container_of(work, struct e1000_adapter, reset_task);

	/* don't run the task if already down */
	if (test_bit(__E1000_DOWN, &adapter->state))
		return;

	if (!(adapter->flags & FLAG_RESTART_NOW)) {
		e1000e_dump(adapter);
		e_err("Reset adapter unexpectedly\n");
	}
	e1000e_reinit_locked(adapter);
}

/**
 * e1000_get_stats64 - Get System Network Statistics
 * @netdev: network interface device structure
 * @stats: rtnl_link_stats64 pointer
 *
 * Returns the address of the device statistics structure.
 **/
void e1000e_get_stats64(struct net_device *netdev,
			struct rtnl_link_stats64 *stats)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	spin_lock(&adapter->stats64_lock);
	e1000e_update_stats(adapter);
	/* Fill out the OS statistics structure */
	stats->rx_bytes = adapter->stats.gorc;
	stats->rx_packets = adapter->stats.gprc;
	stats->tx_bytes = adapter->stats.gotc;
	stats->tx_packets = adapter->stats.gptc;
	stats->multicast = adapter->stats.mprc;
	stats->collisions = adapter->stats.colc;

	/* Rx Errors */

	/* RLEC on some newer hardware can be incorrect so build
	 * our own version based on RUC and ROC
	 */
	stats->rx_errors = adapter->stats.rxerrc +
	    adapter->stats.crcerrs + adapter->stats.algnerrc +
	    adapter->stats.ruc + adapter->stats.roc + adapter->stats.cexterr;
	stats->rx_length_errors = adapter->stats.ruc + adapter->stats.roc;
	stats->rx_crc_errors = adapter->stats.crcerrs;
	stats->rx_frame_errors = adapter->stats.algnerrc;
	stats->rx_missed_errors = adapter->stats.mpc;

	/* Tx Errors */
	stats->tx_errors = adapter->stats.ecol + adapter->stats.latecol;
	stats->tx_aborted_errors = adapter->stats.ecol;
	stats->tx_window_errors = adapter->stats.latecol;
	stats->tx_carrier_errors = adapter->stats.tncrs;

	/* Tx Dropped needs to be maintained elsewhere */

	spin_unlock(&adapter->stats64_lock);
}

/**
 * e1000_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/
static int e1000_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	int max_frame = new_mtu + VLAN_ETH_HLEN + ETH_FCS_LEN;

	/* Jumbo frame support */
	if ((new_mtu > ETH_DATA_LEN) &&
	    !(adapter->flags & FLAG_HAS_JUMBO_FRAMES)) {
		e_err("Jumbo Frames not supported.\n");
		return -EINVAL;
	}

	/* Jumbo frame workaround on 82579 and newer requires CRC be stripped */
	if ((adapter->hw.mac.type >= e1000_pch2lan) &&
	    !(adapter->flags2 & FLAG2_CRC_STRIPPING) &&
	    (new_mtu > ETH_DATA_LEN)) {
		e_err("Jumbo Frames not supported on this device when CRC stripping is disabled.\n");
		return -EINVAL;
	}

	while (test_and_set_bit(__E1000_RESETTING, &adapter->state))
		usleep_range(1000, 2000);
	/* e1000e_down -> e1000e_reset dependent on max_frame_size & mtu */
	adapter->max_frame_size = max_frame;
	e_info("changing MTU from %d to %d\n", netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;

	pm_runtime_get_sync(netdev->dev.parent);

	if (netif_running(netdev))
		e1000e_down(adapter, true);

	/* NOTE: netdev_alloc_skb reserves 16 bytes, and typically NET_IP_ALIGN
	 * means we reserve 2 more, this pushes us to allocate from the next
	 * larger slab size.
	 * i.e. RXBUFFER_2048 --> size-4096 slab
	 * However with the new *_jumbo_rx* routines, jumbo receives will use
	 * fragmented skbs
	 */

	if (max_frame <= 2048)
		adapter->rx_buffer_len = 2048;
	else
		adapter->rx_buffer_len = 4096;

	/* adjust allocation if LPE protects us, and we aren't using SBP */
	if (max_frame <= (VLAN_ETH_FRAME_LEN + ETH_FCS_LEN))
		adapter->rx_buffer_len = VLAN_ETH_FRAME_LEN + ETH_FCS_LEN;

	if (netif_running(netdev))
		e1000e_up(adapter);
	else
		e1000e_reset(adapter);

	pm_runtime_put_sync(netdev->dev.parent);

	clear_bit(__E1000_RESETTING, &adapter->state);

	return 0;
}

static int e1000_mii_ioctl(struct net_device *netdev, struct ifreq *ifr,
			   int cmd)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct mii_ioctl_data *data = if_mii(ifr);

	if (adapter->hw.phy.media_type != e1000_media_type_copper)
		return -EOPNOTSUPP;

	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = adapter->hw.phy.addr;
		break;
	case SIOCGMIIREG:
		e1000_phy_read_status(adapter);

		switch (data->reg_num & 0x1F) {
		case MII_BMCR:
			data->val_out = adapter->phy_regs.bmcr;
			break;
		case MII_BMSR:
			data->val_out = adapter->phy_regs.bmsr;
			break;
		case MII_PHYSID1:
			data->val_out = (adapter->hw.phy.id >> 16);
			break;
		case MII_PHYSID2:
			data->val_out = (adapter->hw.phy.id & 0xFFFF);
			break;
		case MII_ADVERTISE:
			data->val_out = adapter->phy_regs.advertise;
			break;
		case MII_LPA:
			data->val_out = adapter->phy_regs.lpa;
			break;
		case MII_EXPANSION:
			data->val_out = adapter->phy_regs.expansion;
			break;
		case MII_CTRL1000:
			data->val_out = adapter->phy_regs.ctrl1000;
			break;
		case MII_STAT1000:
			data->val_out = adapter->phy_regs.stat1000;
			break;
		case MII_ESTATUS:
			data->val_out = adapter->phy_regs.estatus;
			break;
		default:
			return -EIO;
		}
		break;
	case SIOCSMIIREG:
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

/**
 * e1000e_hwtstamp_ioctl - control hardware time stamping
 * @netdev: network interface device structure
 * @ifreq: interface request
 *
 * Outgoing time stamping can be enabled and disabled. Play nice and
 * disable it when requested, although it shouldn't cause any overhead
 * when no packet needs it. At most one packet in the queue may be
 * marked for time stamping, otherwise it would be impossible to tell
 * for sure to which packet the hardware time stamp belongs.
 *
 * Incoming time stamping has to be configured via the hardware filters.
 * Not all combinations are supported, in particular event type has to be
 * specified. Matching the kind of event packet is not supported, with the
 * exception of "all V2 events regardless of level 2 or 4".
 **/
static int e1000e_hwtstamp_set(struct net_device *netdev, struct ifreq *ifr)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct hwtstamp_config config;
	int ret_val;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	ret_val = e1000e_config_hwtstamp(adapter, &config);
	if (ret_val)
		return ret_val;

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		/* With V2 type filters which specify a Sync or Delay Request,
		 * Path Delay Request/Response messages are also time stamped
		 * by hardware so notify the caller the requested packets plus
		 * some others are time stamped.
		 */
		config.rx_filter = HWTSTAMP_FILTER_SOME;
		break;
	default:
		break;
	}

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
}

static int e1000e_hwtstamp_get(struct net_device *netdev, struct ifreq *ifr)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	return copy_to_user(ifr->ifr_data, &adapter->hwtstamp_config,
			    sizeof(adapter->hwtstamp_config)) ? -EFAULT : 0;
}

static int e1000_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return e1000_mii_ioctl(netdev, ifr, cmd);
	case SIOCSHWTSTAMP:
		return e1000e_hwtstamp_set(netdev, ifr);
	case SIOCGHWTSTAMP:
		return e1000e_hwtstamp_get(netdev, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

static int e1000_init_phy_wakeup(struct e1000_adapter *adapter, u32 wufc)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 i, mac_reg, wuc;
	u16 phy_reg, wuc_enable;
	int retval;

	/* copy MAC RARs to PHY RARs */
	e1000_copy_rx_addrs_to_phy_ich8lan(hw);

	retval = hw->phy.ops.acquire(hw);
	if (retval) {
		e_err("Could not acquire PHY\n");
		return retval;
	}

	/* Enable access to wakeup registers on and set page to BM_WUC_PAGE */
	retval = e1000_enable_phy_wakeup_reg_access_bm(hw, &wuc_enable);
	if (retval)
		goto release;

	/* copy MAC MTA to PHY MTA - only needed for pchlan */
	for (i = 0; i < adapter->hw.mac.mta_reg_count; i++) {
		mac_reg = E1000_READ_REG_ARRAY(hw, E1000_MTA, i);
		hw->phy.ops.write_reg_page(hw, BM_MTA(i),
					   (u16)(mac_reg & 0xFFFF));
		hw->phy.ops.write_reg_page(hw, BM_MTA(i) + 1,
					   (u16)((mac_reg >> 16) & 0xFFFF));
	}

	/* configure PHY Rx Control register */
	hw->phy.ops.read_reg_page(&adapter->hw, BM_RCTL, &phy_reg);
	mac_reg = er32(RCTL);
	if (mac_reg & E1000_RCTL_UPE)
		phy_reg |= BM_RCTL_UPE;
	if (mac_reg & E1000_RCTL_MPE)
		phy_reg |= BM_RCTL_MPE;
	phy_reg &= ~(BM_RCTL_MO_MASK);
	if (mac_reg & E1000_RCTL_MO_3)
		phy_reg |= (((mac_reg & E1000_RCTL_MO_3) >> E1000_RCTL_MO_SHIFT)
			    << BM_RCTL_MO_SHIFT);
	if (mac_reg & E1000_RCTL_BAM)
		phy_reg |= BM_RCTL_BAM;
	if (mac_reg & E1000_RCTL_PMCF)
		phy_reg |= BM_RCTL_PMCF;
	mac_reg = er32(CTRL);
	if (mac_reg & E1000_CTRL_RFCE)
		phy_reg |= BM_RCTL_RFCE;
	hw->phy.ops.write_reg_page(&adapter->hw, BM_RCTL, phy_reg);

	wuc = E1000_WUC_PME_EN;
	if (wufc & (E1000_WUFC_MAG | E1000_WUFC_LNKC))
		wuc |= E1000_WUC_APME;

	/* enable PHY wakeup in MAC register */
	ew32(WUFC, wufc);
	ew32(WUC, (E1000_WUC_PHY_WAKE | E1000_WUC_APMPME |
		   E1000_WUC_PME_STATUS | wuc));

	/* configure and enable PHY wakeup in PHY registers */
	hw->phy.ops.write_reg_page(&adapter->hw, BM_WUFC, wufc);
	hw->phy.ops.write_reg_page(&adapter->hw, BM_WUC, wuc);

	/* activate PHY wakeup */
	wuc_enable |= BM_WUC_ENABLE_BIT | BM_WUC_HOST_WU_BIT;
	retval = e1000_disable_phy_wakeup_reg_access_bm(hw, &wuc_enable);
	if (retval)
		e_err("Could not set PHY Host Wakeup bit\n");
release:
	hw->phy.ops.release(hw);

	return retval;
}

static void e1000e_flush_lpic(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 ret_val;

	pm_runtime_get_sync(netdev->dev.parent);

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto fl_out;

	pr_info("EEE TX LPI TIMER: %08X\n",
		er32(LPIC) >> E1000_LPIC_LPIET_SHIFT);

	hw->phy.ops.release(hw);

fl_out:
	pm_runtime_put_sync(netdev->dev.parent);
}

static int e1000e_pm_freeze(struct device *dev)
{
	struct net_device *netdev = pci_get_drvdata(to_pci_dev(dev));
	struct e1000_adapter *adapter = netdev_priv(netdev);

	netif_device_detach(netdev);

	if (netif_running(netdev)) {
		int count = E1000_CHECK_RESET_COUNT;

		while (test_bit(__E1000_RESETTING, &adapter->state) && count--)
			usleep_range(10000, 20000);

		WARN_ON(test_bit(__E1000_RESETTING, &adapter->state));

		/* Quiesce the device without resetting the hardware */
		e1000e_down(adapter, false);
		e1000_free_irq(adapter);
	}
	e1000e_reset_interrupt_capability(adapter);

	/* Allow time for pending master requests to run */
	e1000e_disable_pcie_master(&adapter->hw);

	return 0;
}

static int __e1000_shutdown(struct pci_dev *pdev, bool runtime)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl, ctrl_ext, rctl, status;
	/* Runtime suspend should only enable wakeup for link changes */
	u32 wufc = runtime ? E1000_WUFC_LNKC : adapter->wol;
	int retval = 0;

	status = er32(STATUS);
	if (status & E1000_STATUS_LU)
		wufc &= ~E1000_WUFC_LNKC;

	if (wufc) {
		e1000_setup_rctl(adapter);
		e1000e_set_rx_mode(netdev);

		/* turn on all-multi mode if wake on multicast is enabled */
		if (wufc & E1000_WUFC_MC) {
			rctl = er32(RCTL);
			rctl |= E1000_RCTL_MPE;
			ew32(RCTL, rctl);
		}

		ctrl = er32(CTRL);
		ctrl |= E1000_CTRL_ADVD3WUC;
		if (!(adapter->flags2 & FLAG2_HAS_PHY_WAKEUP))
			ctrl |= E1000_CTRL_EN_PHY_PWR_MGMT;
		ew32(CTRL, ctrl);

		if (adapter->hw.phy.media_type == e1000_media_type_fiber ||
		    adapter->hw.phy.media_type ==
		    e1000_media_type_internal_serdes) {
			/* keep the laser running in D3 */
			ctrl_ext = er32(CTRL_EXT);
			ctrl_ext |= E1000_CTRL_EXT_SDP3_DATA;
			ew32(CTRL_EXT, ctrl_ext);
		}

		if (!runtime)
			e1000e_power_up_phy(adapter);

		if (adapter->flags & FLAG_IS_ICH)
			e1000_suspend_workarounds_ich8lan(&adapter->hw);

		if (adapter->flags2 & FLAG2_HAS_PHY_WAKEUP) {
			/* enable wakeup by the PHY */
			retval = e1000_init_phy_wakeup(adapter, wufc);
			if (retval)
				return retval;
		} else {
			/* enable wakeup by the MAC */
			ew32(WUFC, wufc);
			ew32(WUC, E1000_WUC_PME_EN);
		}
	} else {
		ew32(WUC, 0);
		ew32(WUFC, 0);

		e1000_power_down_phy(adapter);
	}

	if (adapter->hw.phy.type == e1000_phy_igp_3) {
		e1000e_igp3_phy_powerdown_workaround_ich8lan(&adapter->hw);
	} else if (hw->mac.type >= e1000_pch_lpt) {
		if (!(wufc & (E1000_WUFC_EX | E1000_WUFC_MC | E1000_WUFC_BC)))
			/* ULP does not support wake from unicast, multicast
			 * or broadcast.
			 */
			retval = e1000_enable_ulp_lpt_lp(hw, !runtime);

		if (retval)
			return retval;
	}

	/* Ensure that the appropriate bits are set in LPI_CTRL
	 * for EEE in Sx
	 */
	if ((hw->phy.type >= e1000_phy_i217) &&
	    adapter->eee_advert && hw->dev_spec.ich8lan.eee_lp_ability) {
		u16 lpi_ctrl = 0;

		retval = hw->phy.ops.acquire(hw);
		if (!retval) {
			retval = e1e_rphy_locked(hw, I82579_LPI_CTRL,
						 &lpi_ctrl);
			if (!retval) {
				if (adapter->eee_advert &
				    hw->dev_spec.ich8lan.eee_lp_ability &
				    I82579_EEE_100_SUPPORTED)
					lpi_ctrl |= I82579_LPI_CTRL_100_ENABLE;
				if (adapter->eee_advert &
				    hw->dev_spec.ich8lan.eee_lp_ability &
				    I82579_EEE_1000_SUPPORTED)
					lpi_ctrl |= I82579_LPI_CTRL_1000_ENABLE;

				retval = e1e_wphy_locked(hw, I82579_LPI_CTRL,
							 lpi_ctrl);
			}
		}
		hw->phy.ops.release(hw);
	}

	/* Release control of h/w to f/w.  If f/w is AMT enabled, this
	 * would have already happened in close and is redundant.
	 */
	e1000e_release_hw_control(adapter);

	pci_clear_master(pdev);

	/* The pci-e switch on some quad port adapters will report a
	 * correctable error when the MAC transitions from D0 to D3.  To
	 * prevent this we need to mask off the correctable errors on the
	 * downstream port of the pci-e switch.
	 *
	 * We don't have the associated upstream bridge while assigning
	 * the PCI device into guest. For example, the KVM on power is
	 * one of the cases.
	 */
	if (adapter->flags & FLAG_IS_QUAD_PORT) {
		struct pci_dev *us_dev = pdev->bus->self;
		u16 devctl;

		if (!us_dev)
			return 0;

		pcie_capability_read_word(us_dev, PCI_EXP_DEVCTL, &devctl);
		pcie_capability_write_word(us_dev, PCI_EXP_DEVCTL,
					   (devctl & ~PCI_EXP_DEVCTL_CERE));

		pci_save_state(pdev);
		pci_prepare_to_sleep(pdev);

		pcie_capability_write_word(us_dev, PCI_EXP_DEVCTL, devctl);
	}

	return 0;
}

/**
 * __e1000e_disable_aspm - Disable ASPM states
 * @pdev: pointer to PCI device struct
 * @state: bit-mask of ASPM states to disable
 * @locked: indication if this context holds pci_bus_sem locked.
 *
 * Some devices *must* have certain ASPM states disabled per hardware errata.
 **/
static void __e1000e_disable_aspm(struct pci_dev *pdev, u16 state, int locked)
{
	struct pci_dev *parent = pdev->bus->self;
	u16 aspm_dis_mask = 0;
	u16 pdev_aspmc, parent_aspmc;

	switch (state) {
	case PCIE_LINK_STATE_L0S:
	case PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1:
		aspm_dis_mask |= PCI_EXP_LNKCTL_ASPM_L0S;
		/* fall-through - can't have L1 without L0s */
	case PCIE_LINK_STATE_L1:
		aspm_dis_mask |= PCI_EXP_LNKCTL_ASPM_L1;
		break;
	default:
		return;
	}

	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &pdev_aspmc);
	pdev_aspmc &= PCI_EXP_LNKCTL_ASPMC;

	if (parent) {
		pcie_capability_read_word(parent, PCI_EXP_LNKCTL,
					  &parent_aspmc);
		parent_aspmc &= PCI_EXP_LNKCTL_ASPMC;
	}

	/* Nothing to do if the ASPM states to be disabled already are */
	if (!(pdev_aspmc & aspm_dis_mask) &&
	    (!parent || !(parent_aspmc & aspm_dis_mask)))
		return;

	dev_info(&pdev->dev, "Disabling ASPM %s %s\n",
		 (aspm_dis_mask & pdev_aspmc & PCI_EXP_LNKCTL_ASPM_L0S) ?
		 "L0s" : "",
		 (aspm_dis_mask & pdev_aspmc & PCI_EXP_LNKCTL_ASPM_L1) ?
		 "L1" : "");

#ifdef CONFIG_PCIEASPM
	if (locked)
		pci_disable_link_state_locked(pdev, state);
	else
		pci_disable_link_state(pdev, state);

	/* Double-check ASPM control.  If not disabled by the above, the
	 * BIOS is preventing that from happening (or CONFIG_PCIEASPM is
	 * not enabled); override by writing PCI config space directly.
	 */
	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &pdev_aspmc);
	pdev_aspmc &= PCI_EXP_LNKCTL_ASPMC;

	if (!(aspm_dis_mask & pdev_aspmc))
		return;
#endif

	/* Both device and parent should have the same ASPM setting.
	 * Disable ASPM in downstream component first and then upstream.
	 */
	pcie_capability_clear_word(pdev, PCI_EXP_LNKCTL, aspm_dis_mask);

	if (parent)
		pcie_capability_clear_word(parent, PCI_EXP_LNKCTL,
					   aspm_dis_mask);
}

/**
 * e1000e_disable_aspm - Disable ASPM states.
 * @pdev: pointer to PCI device struct
 * @state: bit-mask of ASPM states to disable
 *
 * This function acquires the pci_bus_sem!
 * Some devices *must* have certain ASPM states disabled per hardware errata.
 **/
static void e1000e_disable_aspm(struct pci_dev *pdev, u16 state)
{
	__e1000e_disable_aspm(pdev, state, 0);
}

/**
 * e1000e_disable_aspm_locked   Disable ASPM states.
 * @pdev: pointer to PCI device struct
 * @state: bit-mask of ASPM states to disable
 *
 * This function must be called with pci_bus_sem acquired!
 * Some devices *must* have certain ASPM states disabled per hardware errata.
 **/
static void e1000e_disable_aspm_locked(struct pci_dev *pdev, u16 state)
{
	__e1000e_disable_aspm(pdev, state, 1);
}

#ifdef CONFIG_PM
static int __e1000_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u16 aspm_disable_flag = 0;

	if (adapter->flags2 & FLAG2_DISABLE_ASPM_L0S)
		aspm_disable_flag = PCIE_LINK_STATE_L0S;
	if (adapter->flags2 & FLAG2_DISABLE_ASPM_L1)
		aspm_disable_flag |= PCIE_LINK_STATE_L1;
	if (aspm_disable_flag)
		e1000e_disable_aspm(pdev, aspm_disable_flag);

	pci_set_master(pdev);

	if (hw->mac.type >= e1000_pch2lan)
		e1000_resume_workarounds_pchlan(&adapter->hw);

	e1000e_power_up_phy(adapter);

	/* report the system wakeup cause from S3/S4 */
	if (adapter->flags2 & FLAG2_HAS_PHY_WAKEUP) {
		u16 phy_data;

		e1e_rphy(&adapter->hw, BM_WUS, &phy_data);
		if (phy_data) {
			e_info("PHY Wakeup cause - %s\n",
			       phy_data & E1000_WUS_EX ? "Unicast Packet" :
			       phy_data & E1000_WUS_MC ? "Multicast Packet" :
			       phy_data & E1000_WUS_BC ? "Broadcast Packet" :
			       phy_data & E1000_WUS_MAG ? "Magic Packet" :
			       phy_data & E1000_WUS_LNKC ?
			       "Link Status Change" : "other");
		}
		e1e_wphy(&adapter->hw, BM_WUS, ~0);
	} else {
		u32 wus = er32(WUS);

		if (wus) {
			e_info("MAC Wakeup cause - %s\n",
			       wus & E1000_WUS_EX ? "Unicast Packet" :
			       wus & E1000_WUS_MC ? "Multicast Packet" :
			       wus & E1000_WUS_BC ? "Broadcast Packet" :
			       wus & E1000_WUS_MAG ? "Magic Packet" :
			       wus & E1000_WUS_LNKC ? "Link Status Change" :
			       "other");
		}
		ew32(WUS, ~0);
	}

	e1000e_reset(adapter);

	e1000_init_manageability_pt(adapter);

	/* If the controller has AMT, do not set DRV_LOAD until the interface
	 * is up.  For all other cases, let the f/w know that the h/w is now
	 * under the control of the driver.
	 */
	if (!(adapter->flags & FLAG_HAS_AMT))
		e1000e_get_hw_control(adapter);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int e1000e_pm_thaw(struct device *dev)
{
	struct net_device *netdev = pci_get_drvdata(to_pci_dev(dev));
	struct e1000_adapter *adapter = netdev_priv(netdev);

	e1000e_set_interrupt_capability(adapter);
	if (netif_running(netdev)) {
		u32 err = e1000_request_irq(adapter);

		if (err)
			return err;

		e1000e_up(adapter);
	}

	netif_device_attach(netdev);

	return 0;
}

static int e1000e_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int rc;

	e1000e_flush_lpic(pdev);

	e1000e_pm_freeze(dev);

	rc = __e1000_shutdown(pdev, false);
	if (rc)
		e1000e_pm_thaw(dev);

	return rc;
}

static int e1000e_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int rc;

	rc = __e1000_resume(pdev);
	if (rc)
		return rc;

	return e1000e_pm_thaw(dev);
}
#endif /* CONFIG_PM_SLEEP */

static int e1000e_pm_runtime_idle(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	u16 eee_lp;

	eee_lp = adapter->hw.dev_spec.ich8lan.eee_lp_ability;

	if (!e1000e_has_link(adapter)) {
		adapter->hw.dev_spec.ich8lan.eee_lp_ability = eee_lp;
		pm_schedule_suspend(dev, 5 * MSEC_PER_SEC);
	}

	return -EBUSY;
}

static int e1000e_pm_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	int rc;

	rc = __e1000_resume(pdev);
	if (rc)
		return rc;

	if (netdev->flags & IFF_UP)
		e1000e_up(adapter);

	return rc;
}

static int e1000e_pm_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);

	if (netdev->flags & IFF_UP) {
		int count = E1000_CHECK_RESET_COUNT;

		while (test_bit(__E1000_RESETTING, &adapter->state) && count--)
			usleep_range(10000, 20000);

		WARN_ON(test_bit(__E1000_RESETTING, &adapter->state));

		/* Down the device without resetting the hardware */
		e1000e_down(adapter, false);
	}

	if (__e1000_shutdown(pdev, true)) {
		e1000e_pm_runtime_resume(dev);
		return -EBUSY;
	}

	return 0;
}
#endif /* CONFIG_PM */

static void e1000_shutdown(struct pci_dev *pdev)
{
	e1000e_flush_lpic(pdev);

	e1000e_pm_freeze(&pdev->dev);

	__e1000_shutdown(pdev, false);
}

#ifdef CONFIG_NET_POLL_CONTROLLER

static irqreturn_t e1000_intr_msix(int __always_unused irq, void *data)
{
	struct net_device *netdev = data;
	struct e1000_adapter *adapter = netdev_priv(netdev);

	if (adapter->msix_entries) {
		int vector, msix_irq;

		vector = 0;
		msix_irq = adapter->msix_entries[vector].vector;
		if (disable_hardirq(msix_irq))
			e1000_intr_msix_rx(msix_irq, netdev);
		enable_irq(msix_irq);

		vector++;
		msix_irq = adapter->msix_entries[vector].vector;
		if (disable_hardirq(msix_irq))
			e1000_intr_msix_tx(msix_irq, netdev);
		enable_irq(msix_irq);

		vector++;
		msix_irq = adapter->msix_entries[vector].vector;
		if (disable_hardirq(msix_irq))
			e1000_msix_other(msix_irq, netdev);
		enable_irq(msix_irq);
	}

	return IRQ_HANDLED;
}

/**
 * e1000_netpoll
 * @netdev: network interface device structure
 *
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void e1000_netpoll(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	switch (adapter->int_mode) {
	case E1000E_INT_MODE_MSIX:
		e1000_intr_msix(adapter->pdev->irq, netdev);
		break;
	case E1000E_INT_MODE_MSI:
		if (disable_hardirq(adapter->pdev->irq))
			e1000_intr_msi(adapter->pdev->irq, netdev);
		enable_irq(adapter->pdev->irq);
		break;
	default:		/* E1000E_INT_MODE_LEGACY */
		if (disable_hardirq(adapter->pdev->irq))
			e1000_intr(adapter->pdev->irq, netdev);
		enable_irq(adapter->pdev->irq);
		break;
	}
}
#endif

/**
 * e1000_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t e1000_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);

	netif_device_detach(netdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (netif_running(netdev))
		e1000e_down(adapter, true);
	pci_disable_device(pdev);

	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * e1000_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot. Implementation
 * resembles the first-half of the e1000e_pm_resume routine.
 */
static pci_ers_result_t e1000_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u16 aspm_disable_flag = 0;
	int err;
	pci_ers_result_t result;

	if (adapter->flags2 & FLAG2_DISABLE_ASPM_L0S)
		aspm_disable_flag = PCIE_LINK_STATE_L0S;
	if (adapter->flags2 & FLAG2_DISABLE_ASPM_L1)
		aspm_disable_flag |= PCIE_LINK_STATE_L1;
	if (aspm_disable_flag)
		e1000e_disable_aspm_locked(pdev, aspm_disable_flag);

	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(&pdev->dev,
			"Cannot re-enable PCI device after reset.\n");
		result = PCI_ERS_RESULT_DISCONNECT;
	} else {
		pdev->state_saved = true;
		pci_restore_state(pdev);
		pci_set_master(pdev);

		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_enable_wake(pdev, PCI_D3cold, 0);

		e1000e_reset(adapter);
		ew32(WUS, ~0);
		result = PCI_ERS_RESULT_RECOVERED;
	}

	return result;
}

/**
 * e1000_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation. Implementation resembles the
 * second-half of the e1000e_pm_resume routine.
 */
static void e1000_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);

	e1000_init_manageability_pt(adapter);

	if (netif_running(netdev))
		e1000e_up(adapter);

	netif_device_attach(netdev);

	/* If the controller has AMT, do not set DRV_LOAD until the interface
	 * is up.  For all other cases, let the f/w know that the h/w is now
	 * under the control of the driver.
	 */
	if (!(adapter->flags & FLAG_HAS_AMT))
		e1000e_get_hw_control(adapter);
}

static void e1000_print_device_info(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 ret_val;
	u8 pba_str[E1000_PBANUM_LENGTH];

	/* print bus type/speed/width info */
	e_info("(PCI Express:2.5GT/s:%s) %pM\n",
	       /* bus width */
	       ((hw->bus.width == e1000_bus_width_pcie_x4) ? "Width x4" :
		"Width x1"),
	       /* MAC address */
	       netdev->dev_addr);
	e_info("Intel(R) PRO/%s Network Connection\n",
	       (hw->phy.type == e1000_phy_ife) ? "10/100" : "1000");
	ret_val = e1000_read_pba_string_generic(hw, pba_str,
						E1000_PBANUM_LENGTH);
	if (ret_val)
		strlcpy((char *)pba_str, "Unknown", sizeof(pba_str));
	e_info("MAC: %d, PHY: %d, PBA No: %s\n",
	       hw->mac.type, hw->phy.type, pba_str);
}

static void e1000_eeprom_checks(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	int ret_val;
	u16 buf = 0;

	if (hw->mac.type != e1000_82573)
		return;

	ret_val = e1000_read_nvm(hw, NVM_INIT_CONTROL2_REG, 1, &buf);
	le16_to_cpus(&buf);
	if (!ret_val && (!(buf & BIT(0)))) {
		/* Deep Smart Power Down (DSPD) */
		dev_warn(&adapter->pdev->dev,
			 "Warning: detected DSPD enabled in EEPROM\n");
	}
}

static netdev_features_t e1000_fix_features(struct net_device *netdev,
					    netdev_features_t features)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	/* Jumbo frame workaround on 82579 and newer requires CRC be stripped */
	if ((hw->mac.type >= e1000_pch2lan) && (netdev->mtu > ETH_DATA_LEN))
		features &= ~NETIF_F_RXFCS;

	/* Since there is no support for separate Rx/Tx vlan accel
	 * enable/disable make sure Tx flag is always in same state as Rx.
	 */
	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		features |= NETIF_F_HW_VLAN_CTAG_TX;
	else
		features &= ~NETIF_F_HW_VLAN_CTAG_TX;

	return features;
}

static int e1000_set_features(struct net_device *netdev,
			      netdev_features_t features)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	netdev_features_t changed = features ^ netdev->features;

	if (changed & (NETIF_F_TSO | NETIF_F_TSO6))
		adapter->flags |= FLAG_TSO_FORCE;

	if (!(changed & (NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_CTAG_TX |
			 NETIF_F_RXCSUM | NETIF_F_RXHASH | NETIF_F_RXFCS |
			 NETIF_F_RXALL)))
		return 0;

	if (changed & NETIF_F_RXFCS) {
		if (features & NETIF_F_RXFCS) {
			adapter->flags2 &= ~FLAG2_CRC_STRIPPING;
		} else {
			/* We need to take it back to defaults, which might mean
			 * stripping is still disabled at the adapter level.
			 */
			if (adapter->flags2 & FLAG2_DFLT_CRC_STRIPPING)
				adapter->flags2 |= FLAG2_CRC_STRIPPING;
			else
				adapter->flags2 &= ~FLAG2_CRC_STRIPPING;
		}
	}

	netdev->features = features;

	if (netif_running(netdev))
		e1000e_reinit_locked(adapter);
	else
		e1000e_reset(adapter);

	return 0;
}

static const struct net_device_ops e1000e_netdev_ops = {
	.ndo_open		= e1000e_open,
	.ndo_stop		= e1000e_close,
	.ndo_start_xmit		= e1000_xmit_frame,
	.ndo_get_stats64	= e1000e_get_stats64,
	.ndo_set_rx_mode	= e1000e_set_rx_mode,
	.ndo_set_mac_address	= e1000_set_mac,
	.ndo_change_mtu		= e1000_change_mtu,
	.ndo_do_ioctl		= e1000_ioctl,
	.ndo_tx_timeout		= e1000_tx_timeout,
	.ndo_validate_addr	= eth_validate_addr,

	.ndo_vlan_rx_add_vid	= e1000_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= e1000_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= e1000_netpoll,
#endif
	.ndo_set_features = e1000_set_features,
	.ndo_fix_features = e1000_fix_features,
	.ndo_features_check	= passthru_features_check,
};

/**
 * e1000_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in e1000_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * e1000_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int e1000_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct e1000_adapter *adapter;
	struct e1000_hw *hw;
	const struct e1000_info *ei = e1000_info_tbl[ent->driver_data];
	resource_size_t mmio_start, mmio_len;
	resource_size_t flash_start, flash_len;
	static int cards_found;
	u16 aspm_disable_flag = 0;
	int bars, i, err, pci_using_dac;
	u16 eeprom_data = 0;
	u16 eeprom_apme_mask = E1000_EEPROM_APME;
	s32 ret_val = 0;

	if (ei->flags2 & FLAG2_DISABLE_ASPM_L0S)
		aspm_disable_flag = PCIE_LINK_STATE_L0S;
	if (ei->flags2 & FLAG2_DISABLE_ASPM_L1)
		aspm_disable_flag |= PCIE_LINK_STATE_L1;
	if (aspm_disable_flag)
		e1000e_disable_aspm(pdev, aspm_disable_flag);

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	pci_using_dac = 0;
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (!err) {
		pci_using_dac = 1;
	} else {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev,
				"No usable DMA configuration, aborting\n");
			goto err_dma;
		}
	}

	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	err = pci_request_selected_regions_exclusive(pdev, bars,
						     e1000e_driver_name);
	if (err)
		goto err_pci_reg;

	/* AER (Advanced Error Reporting) hooks */
	pci_enable_pcie_error_reporting(pdev);

	pci_set_master(pdev);
	/* PCI config space info */
	err = pci_save_state(pdev);
	if (err)
		goto err_alloc_etherdev;

	err = -ENOMEM;
	netdev = alloc_etherdev(sizeof(struct e1000_adapter));
	if (!netdev)
		goto err_alloc_etherdev;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	netdev->irq = pdev->irq;

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);
	hw = &adapter->hw;
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->ei = ei;
	adapter->pba = ei->pba;
	adapter->flags = ei->flags;
	adapter->flags2 = ei->flags2;
	adapter->hw.adapter = adapter;
	adapter->hw.mac.type = ei->mac;
	adapter->max_hw_frame_size = ei->max_hw_frame_size;
	adapter->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

	mmio_start = pci_resource_start(pdev, 0);
	mmio_len = pci_resource_len(pdev, 0);

	err = -EIO;
	adapter->hw.hw_addr = ioremap(mmio_start, mmio_len);
	if (!adapter->hw.hw_addr)
		goto err_ioremap;

	if ((adapter->flags & FLAG_HAS_FLASH) &&
	    (pci_resource_flags(pdev, 1) & IORESOURCE_MEM) &&
	    (hw->mac.type < e1000_pch_spt)) {
		flash_start = pci_resource_start(pdev, 1);
		flash_len = pci_resource_len(pdev, 1);
		adapter->hw.flash_address = ioremap(flash_start, flash_len);
		if (!adapter->hw.flash_address)
			goto err_flashmap;
	}

	/* Set default EEE advertisement */
	if (adapter->flags2 & FLAG2_HAS_EEE)
		adapter->eee_advert = MDIO_EEE_100TX | MDIO_EEE_1000T;

	/* construct the net_device struct */
	netdev->netdev_ops = &e1000e_netdev_ops;
	e1000e_set_ethtool_ops(netdev);
	netdev->watchdog_timeo = 5 * HZ;
	netif_napi_add(netdev, &adapter->napi, e1000e_poll, 64);
	strlcpy(netdev->name, pci_name(pdev), sizeof(netdev->name));

	netdev->mem_start = mmio_start;
	netdev->mem_end = mmio_start + mmio_len;

	adapter->bd_number = cards_found++;

	e1000e_check_options(adapter);

	/* setup adapter struct */
	err = e1000_sw_init(adapter);
	if (err)
		goto err_sw_init;

	memcpy(&hw->mac.ops, ei->mac_ops, sizeof(hw->mac.ops));
	memcpy(&hw->nvm.ops, ei->nvm_ops, sizeof(hw->nvm.ops));
	memcpy(&hw->phy.ops, ei->phy_ops, sizeof(hw->phy.ops));

	err = ei->get_variants(adapter);
	if (err)
		goto err_hw_init;

	if ((adapter->flags & FLAG_IS_ICH) &&
	    (adapter->flags & FLAG_READ_ONLY_NVM) &&
	    (hw->mac.type < e1000_pch_spt))
		e1000e_write_protect_nvm_ich8lan(&adapter->hw);

	hw->mac.ops.get_bus_info(&adapter->hw);

	adapter->hw.phy.autoneg_wait_to_complete = 0;

	/* Copper options */
	if (adapter->hw.phy.media_type == e1000_media_type_copper) {
		adapter->hw.phy.mdix = AUTO_ALL_MODES;
		adapter->hw.phy.disable_polarity_correction = 0;
		adapter->hw.phy.ms_type = e1000_ms_hw_default;
	}

	if (hw->phy.ops.check_reset_block && hw->phy.ops.check_reset_block(hw))
		dev_info(&pdev->dev,
			 "PHY reset is blocked due to SOL/IDER session.\n");

	/* Set initial default active device features */
	netdev->features = (NETIF_F_SG |
			    NETIF_F_HW_VLAN_CTAG_RX |
			    NETIF_F_HW_VLAN_CTAG_TX |
			    NETIF_F_TSO |
			    NETIF_F_TSO6 |
			    NETIF_F_RXHASH |
			    NETIF_F_RXCSUM |
			    NETIF_F_HW_CSUM);

	/* Set user-changeable features (subset of all device features) */
	netdev->hw_features = netdev->features;
	netdev->hw_features |= NETIF_F_RXFCS;
	netdev->priv_flags |= IFF_SUPP_NOFCS;
	netdev->hw_features |= NETIF_F_RXALL;

	if (adapter->flags & FLAG_HAS_HW_VLAN_FILTER)
		netdev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	netdev->vlan_features |= (NETIF_F_SG |
				  NETIF_F_TSO |
				  NETIF_F_TSO6 |
				  NETIF_F_HW_CSUM);

	netdev->priv_flags |= IFF_UNICAST_FLT;

	if (pci_using_dac) {
		netdev->features |= NETIF_F_HIGHDMA;
		netdev->vlan_features |= NETIF_F_HIGHDMA;
	}

	/* MTU range: 68 - max_hw_frame_size */
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = adapter->max_hw_frame_size -
			  (VLAN_ETH_HLEN + ETH_FCS_LEN);

	if (e1000e_enable_mng_pass_thru(&adapter->hw))
		adapter->flags |= FLAG_MNG_PT_ENABLED;

	/* before reading the NVM, reset the controller to
	 * put the device in a known good starting state
	 */
	adapter->hw.mac.ops.reset_hw(&adapter->hw);

	/* systems with ASPM and others may see the checksum fail on the first
	 * attempt. Let's give it a few tries
	 */
	for (i = 0;; i++) {
		if (e1000_validate_nvm_checksum(&adapter->hw) >= 0)
			break;
		if (i == 2) {
			dev_err(&pdev->dev, "The NVM Checksum Is Not Valid\n");
			err = -EIO;
			goto err_eeprom;
		}
	}

	e1000_eeprom_checks(adapter);

	/* copy the MAC address */
	if (e1000e_read_mac_addr(&adapter->hw))
		dev_err(&pdev->dev,
			"NVM Read Error while reading MAC address\n");

	memcpy(netdev->dev_addr, adapter->hw.mac.addr, netdev->addr_len);

	if (!is_valid_ether_addr(netdev->dev_addr)) {
		dev_err(&pdev->dev, "Invalid MAC Address: %pM\n",
			netdev->dev_addr);
		err = -EIO;
		goto err_eeprom;
	}

	timer_setup(&adapter->watchdog_timer, e1000_watchdog, 0);
	timer_setup(&adapter->phy_info_timer, e1000_update_phy_info, 0);

	INIT_WORK(&adapter->reset_task, e1000_reset_task);
	INIT_WORK(&adapter->watchdog_task, e1000_watchdog_task);
	INIT_WORK(&adapter->downshift_task, e1000e_downshift_workaround);
	INIT_WORK(&adapter->update_phy_task, e1000e_update_phy_task);
	INIT_WORK(&adapter->print_hang_task, e1000_print_hw_hang);

	/* Initialize link parameters. User can change them with ethtool */
	adapter->hw.mac.autoneg = 1;
	adapter->fc_autoneg = true;
	adapter->hw.fc.requested_mode = e1000_fc_default;
	adapter->hw.fc.current_mode = e1000_fc_default;
	adapter->hw.phy.autoneg_advertised = 0x2f;

	/* Initial Wake on LAN setting - If APM wake is enabled in
	 * the EEPROM, enable the ACPI Magic Packet filter
	 */
	if (adapter->flags & FLAG_APME_IN_WUC) {
		/* APME bit in EEPROM is mapped to WUC.APME */
		eeprom_data = er32(WUC);
		eeprom_apme_mask = E1000_WUC_APME;
		if ((hw->mac.type > e1000_ich10lan) &&
		    (eeprom_data & E1000_WUC_PHY_WAKE))
			adapter->flags2 |= FLAG2_HAS_PHY_WAKEUP;
	} else if (adapter->flags & FLAG_APME_IN_CTRL3) {
		if (adapter->flags & FLAG_APME_CHECK_PORT_B &&
		    (adapter->hw.bus.func == 1))
			ret_val = e1000_read_nvm(&adapter->hw,
					      NVM_INIT_CONTROL3_PORT_B,
					      1, &eeprom_data);
		else
			ret_val = e1000_read_nvm(&adapter->hw,
					      NVM_INIT_CONTROL3_PORT_A,
					      1, &eeprom_data);
	}

	/* fetch WoL from EEPROM */
	if (ret_val)
		e_dbg("NVM read error getting WoL initial values: %d\n", ret_val);
	else if (eeprom_data & eeprom_apme_mask)
		adapter->eeprom_wol |= E1000_WUFC_MAG;

	/* now that we have the eeprom settings, apply the special cases
	 * where the eeprom may be wrong or the board simply won't support
	 * wake on lan on a particular port
	 */
	if (!(adapter->flags & FLAG_HAS_WOL))
		adapter->eeprom_wol = 0;

	/* initialize the wol settings based on the eeprom settings */
	adapter->wol = adapter->eeprom_wol;

	/* make sure adapter isn't asleep if manageability is enabled */
	if (adapter->wol || (adapter->flags & FLAG_MNG_PT_ENABLED) ||
	    (hw->mac.ops.check_mng_mode(hw)))
		device_wakeup_enable(&pdev->dev);

	/* save off EEPROM version number */
	ret_val = e1000_read_nvm(&adapter->hw, 5, 1, &adapter->eeprom_vers);

	if (ret_val) {
		e_dbg("NVM read error getting EEPROM version: %d\n", ret_val);
		adapter->eeprom_vers = 0;
	}

	/* init PTP hardware clock */
	e1000e_ptp_init(adapter);

	/* reset the hardware with the new settings */
	e1000e_reset(adapter);

	/* If the controller has AMT, do not set DRV_LOAD until the interface
	 * is up.  For all other cases, let the f/w know that the h/w is now
	 * under the control of the driver.
	 */
	if (!(adapter->flags & FLAG_HAS_AMT))
		e1000e_get_hw_control(adapter);

	strlcpy(netdev->name, "eth%d", sizeof(netdev->name));
	err = register_netdev(netdev);
	if (err)
		goto err_register;

	/* carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	e1000_print_device_info(adapter);

	if (pci_dev_run_wake(pdev))
		pm_runtime_put_noidle(&pdev->dev);

	return 0;

err_register:
	if (!(adapter->flags & FLAG_HAS_AMT))
		e1000e_release_hw_control(adapter);
err_eeprom:
	if (hw->phy.ops.check_reset_block && !hw->phy.ops.check_reset_block(hw))
		e1000_phy_hw_reset(&adapter->hw);
err_hw_init:
	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);
err_sw_init:
	if ((adapter->hw.flash_address) && (hw->mac.type < e1000_pch_spt))
		iounmap(adapter->hw.flash_address);
	e1000e_reset_interrupt_capability(adapter);
err_flashmap:
	iounmap(adapter->hw.hw_addr);
err_ioremap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_mem_regions(pdev);
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * e1000_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * e1000_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void e1000_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	bool down = test_bit(__E1000_DOWN, &adapter->state);

	e1000e_ptp_remove(adapter);

	/* The timers may be rescheduled, so explicitly disable them
	 * from being rescheduled.
	 */
	if (!down)
		set_bit(__E1000_DOWN, &adapter->state);
	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_info_timer);

	cancel_work_sync(&adapter->reset_task);
	cancel_work_sync(&adapter->watchdog_task);
	cancel_work_sync(&adapter->downshift_task);
	cancel_work_sync(&adapter->update_phy_task);
	cancel_work_sync(&adapter->print_hang_task);

	if (adapter->flags & FLAG_HAS_HW_TIMESTAMP) {
		cancel_work_sync(&adapter->tx_hwtstamp_work);
		if (adapter->tx_hwtstamp_skb) {
			dev_consume_skb_any(adapter->tx_hwtstamp_skb);
			adapter->tx_hwtstamp_skb = NULL;
		}
	}

	/* Don't lie to e1000_close() down the road. */
	if (!down)
		clear_bit(__E1000_DOWN, &adapter->state);
	unregister_netdev(netdev);

	if (pci_dev_run_wake(pdev))
		pm_runtime_get_noresume(&pdev->dev);

	/* Release control of h/w to f/w.  If f/w is AMT enabled, this
	 * would have already happened in close and is redundant.
	 */
	e1000e_release_hw_control(adapter);

	e1000e_reset_interrupt_capability(adapter);
	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);

	iounmap(adapter->hw.hw_addr);
	if ((adapter->hw.flash_address) &&
	    (adapter->hw.mac.type < e1000_pch_spt))
		iounmap(adapter->hw.flash_address);
	pci_release_mem_regions(pdev);

	free_netdev(netdev);

	/* AER disable */
	pci_disable_pcie_error_reporting(pdev);

	pci_disable_device(pdev);
}

/* PCI Error Recovery (ERS) */
static const struct pci_error_handlers e1000_err_handler = {
	.error_detected = e1000_io_error_detected,
	.slot_reset = e1000_io_slot_reset,
	.resume = e1000_io_resume,
};

static const struct pci_device_id e1000_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82571EB_COPPER), board_82571 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82571EB_FIBER), board_82571 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82571EB_QUAD_COPPER), board_82571 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82571EB_QUAD_COPPER_LP),
	  board_82571 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82571EB_QUAD_FIBER), board_82571 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82571EB_SERDES), board_82571 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82571EB_SERDES_DUAL), board_82571 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82571EB_SERDES_QUAD), board_82571 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82571PT_QUAD_COPPER), board_82571 },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82572EI), board_82572 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82572EI_COPPER), board_82572 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82572EI_FIBER), board_82572 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82572EI_SERDES), board_82572 },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82573E), board_82573 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82573E_IAMT), board_82573 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82573L), board_82573 },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82574L), board_82574 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82574LA), board_82574 },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_82583V), board_82583 },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_80003ES2LAN_COPPER_DPT),
	  board_80003es2lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_80003ES2LAN_COPPER_SPT),
	  board_80003es2lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_80003ES2LAN_SERDES_DPT),
	  board_80003es2lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_80003ES2LAN_SERDES_SPT),
	  board_80003es2lan },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH8_IFE), board_ich8lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH8_IFE_G), board_ich8lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH8_IFE_GT), board_ich8lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH8_IGP_AMT), board_ich8lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH8_IGP_C), board_ich8lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH8_IGP_M), board_ich8lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH8_IGP_M_AMT), board_ich8lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH8_82567V_3), board_ich8lan },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH9_IFE), board_ich9lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH9_IFE_G), board_ich9lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH9_IFE_GT), board_ich9lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH9_IGP_AMT), board_ich9lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH9_IGP_C), board_ich9lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH9_BM), board_ich9lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH9_IGP_M), board_ich9lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH9_IGP_M_AMT), board_ich9lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH9_IGP_M_V), board_ich9lan },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH10_R_BM_LM), board_ich9lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH10_R_BM_LF), board_ich9lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH10_R_BM_V), board_ich9lan },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH10_D_BM_LM), board_ich10lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH10_D_BM_LF), board_ich10lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_ICH10_D_BM_V), board_ich10lan },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_M_HV_LM), board_pchlan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_M_HV_LC), board_pchlan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_D_HV_DM), board_pchlan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_D_HV_DC), board_pchlan },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH2_LV_LM), board_pch2lan },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH2_LV_V), board_pch2lan },

	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_LPT_I217_LM), board_pch_lpt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_LPT_I217_V), board_pch_lpt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_LPTLP_I218_LM), board_pch_lpt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_LPTLP_I218_V), board_pch_lpt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_I218_LM2), board_pch_lpt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_I218_V2), board_pch_lpt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_I218_LM3), board_pch_lpt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_I218_V3), board_pch_lpt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_SPT_I219_LM), board_pch_spt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_SPT_I219_V), board_pch_spt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_SPT_I219_LM2), board_pch_spt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_SPT_I219_V2), board_pch_spt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_LBG_I219_LM3), board_pch_spt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_SPT_I219_LM4), board_pch_spt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_SPT_I219_V4), board_pch_spt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_SPT_I219_LM5), board_pch_spt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_SPT_I219_V5), board_pch_spt },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_CNP_I219_LM6), board_pch_cnp },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_CNP_I219_V6), board_pch_cnp },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_CNP_I219_LM7), board_pch_cnp },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_CNP_I219_V7), board_pch_cnp },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_ICP_I219_LM8), board_pch_cnp },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_ICP_I219_V8), board_pch_cnp },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_ICP_I219_LM9), board_pch_cnp },
	{ PCI_VDEVICE(INTEL, E1000_DEV_ID_PCH_ICP_I219_V9), board_pch_cnp },

	{ 0, 0, 0, 0, 0, 0, 0 }	/* terminate list */
};
MODULE_DEVICE_TABLE(pci, e1000_pci_tbl);

static const struct dev_pm_ops e1000_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend	= e1000e_pm_suspend,
	.resume		= e1000e_pm_resume,
	.freeze		= e1000e_pm_freeze,
	.thaw		= e1000e_pm_thaw,
	.poweroff	= e1000e_pm_suspend,
	.restore	= e1000e_pm_resume,
#endif
	SET_RUNTIME_PM_OPS(e1000e_pm_runtime_suspend, e1000e_pm_runtime_resume,
			   e1000e_pm_runtime_idle)
};

/* PCI Device API Driver */
static struct pci_driver e1000_driver = {
	.name     = e1000e_driver_name,
	.id_table = e1000_pci_tbl,
	.probe    = e1000_probe,
	.remove   = e1000_remove,
	.driver   = {
		.pm = &e1000_pm_ops,
	},
	.shutdown = e1000_shutdown,
	.err_handler = &e1000_err_handler
};

/**
 * e1000_init_module - Driver Registration Routine
 *
 * e1000_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/
static int __init e1000_init_module(void)
{
	pr_info("Intel(R) PRO/1000 Network Driver - %s\n",
		e1000e_driver_version);
	pr_info("Copyright(c) 1999 - 2015 Intel Corporation.\n");

	return pci_register_driver(&e1000_driver);
}
module_init(e1000_init_module);

/**
 * e1000_exit_module - Driver Exit Cleanup Routine
 *
 * e1000_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit e1000_exit_module(void)
{
	pci_unregister_driver(&e1000_driver);
}
module_exit(e1000_exit_module);

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) PRO/1000 Network Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);

/* netdev.c */
