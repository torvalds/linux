// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 1999 - 2010 Intel Corporation.
 * Copyright (C) 2010 - 2012 LAPIS SEMICONDUCTOR CO., LTD.
 *
 * This code was derived from the Intel e1000e Linux driver.
 */

#include "pch_gbe.h"
#include "pch_gbe_phy.h"

#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_pch.h>
#include <linux/gpio.h>

#define PCH_GBE_MAR_ENTRIES		16
#define PCH_GBE_SHORT_PKT		64
#define DSC_INIT16			0xC000
#define PCH_GBE_DMA_ALIGN		0
#define PCH_GBE_DMA_PADDING		2
#define PCH_GBE_WATCHDOG_PERIOD		(5 * HZ)	/* watchdog time */
#define PCH_GBE_PCI_BAR			1
#define PCH_GBE_RESERVE_MEMORY		0x200000	/* 2MB */

#define PCI_DEVICE_ID_INTEL_IOH1_GBE		0x8802

#define PCI_DEVICE_ID_ROHM_ML7223_GBE		0x8013
#define PCI_DEVICE_ID_ROHM_ML7831_GBE		0x8802

#define PCH_GBE_RX_BUFFER_WRITE   16

/* Initialize the wake-on-LAN settings */
#define PCH_GBE_WL_INIT_SETTING    (PCH_GBE_WLC_MP)

#define PCH_GBE_MAC_RGMII_CTRL_SETTING ( \
	PCH_GBE_CHIP_TYPE_INTERNAL | \
	PCH_GBE_RGMII_MODE_RGMII     \
	)

/* Ethertype field values */
#define PCH_GBE_MAX_RX_BUFFER_SIZE      0x2880
#define PCH_GBE_MAX_JUMBO_FRAME_SIZE    10318
#define PCH_GBE_FRAME_SIZE_2048         2048
#define PCH_GBE_FRAME_SIZE_4096         4096
#define PCH_GBE_FRAME_SIZE_8192         8192

#define PCH_GBE_GET_DESC(R, i, type)    (&(((struct type *)((R).desc))[i]))
#define PCH_GBE_RX_DESC(R, i)           PCH_GBE_GET_DESC(R, i, pch_gbe_rx_desc)
#define PCH_GBE_TX_DESC(R, i)           PCH_GBE_GET_DESC(R, i, pch_gbe_tx_desc)
#define PCH_GBE_DESC_UNUSED(R) \
	((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	(R)->next_to_clean - (R)->next_to_use - 1)

/* Pause packet value */
#define	PCH_GBE_PAUSE_PKT1_VALUE    0x00C28001
#define	PCH_GBE_PAUSE_PKT2_VALUE    0x00000100
#define	PCH_GBE_PAUSE_PKT4_VALUE    0x01000888
#define	PCH_GBE_PAUSE_PKT5_VALUE    0x0000FFFF


/* This defines the bits that are set in the Interrupt Mask
 * Set/Read Register.  Each bit is documented below:
 *   o RXT0   = Receiver Timer Interrupt (ring 0)
 *   o TXDW   = Transmit Descriptor Written Back
 *   o RXDMT0 = Receive Descriptor Minimum Threshold hit (ring 0)
 *   o RXSEQ  = Receive Sequence Error
 *   o LSC    = Link Status Change
 */
#define PCH_GBE_INT_ENABLE_MASK ( \
	PCH_GBE_INT_RX_DMA_CMPLT |    \
	PCH_GBE_INT_RX_DSC_EMP   |    \
	PCH_GBE_INT_RX_FIFO_ERR  |    \
	PCH_GBE_INT_WOL_DET      |    \
	PCH_GBE_INT_TX_CMPLT          \
	)

#define PCH_GBE_INT_DISABLE_ALL		0

/* Macros for ieee1588 */
/* 0x40 Time Synchronization Channel Control Register Bits */
#define MASTER_MODE   (1<<0)
#define SLAVE_MODE    (0)
#define V2_MODE       (1<<31)
#define CAP_MODE0     (0)
#define CAP_MODE2     (1<<17)

/* 0x44 Time Synchronization Channel Event Register Bits */
#define TX_SNAPSHOT_LOCKED (1<<0)
#define RX_SNAPSHOT_LOCKED (1<<1)

#define PTP_L4_MULTICAST_SA "01:00:5e:00:01:81"
#define PTP_L2_MULTICAST_SA "01:1b:19:00:00:00"

static int pch_gbe_mdio_read(struct net_device *netdev, int addr, int reg);
static void pch_gbe_mdio_write(struct net_device *netdev, int addr, int reg,
			       int data);
static void pch_gbe_set_multi(struct net_device *netdev);

static int pch_ptp_match(struct sk_buff *skb, u16 uid_hi, u32 uid_lo, u16 seqid)
{
	u8 *data = skb->data;
	unsigned int offset;
	u16 hi, id;
	u32 lo;

	if (ptp_classify_raw(skb) == PTP_CLASS_NONE)
		return 0;

	offset = ETH_HLEN + IPV4_HLEN(data) + UDP_HLEN;

	if (skb->len < offset + OFF_PTP_SEQUENCE_ID + sizeof(seqid))
		return 0;

	hi = get_unaligned_be16(data + offset + OFF_PTP_SOURCE_UUID + 0);
	lo = get_unaligned_be32(data + offset + OFF_PTP_SOURCE_UUID + 2);
	id = get_unaligned_be16(data + offset + OFF_PTP_SEQUENCE_ID);

	return (uid_hi == hi && uid_lo == lo && seqid == id);
}

static void
pch_rx_timestamp(struct pch_gbe_adapter *adapter, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps *shhwtstamps;
	struct pci_dev *pdev;
	u64 ns;
	u32 hi, lo, val;

	if (!adapter->hwts_rx_en)
		return;

	/* Get ieee1588's dev information */
	pdev = adapter->ptp_pdev;

	val = pch_ch_event_read(pdev);

	if (!(val & RX_SNAPSHOT_LOCKED))
		return;

	lo = pch_src_uuid_lo_read(pdev);
	hi = pch_src_uuid_hi_read(pdev);

	if (!pch_ptp_match(skb, hi, lo, hi >> 16))
		goto out;

	ns = pch_rx_snap_read(pdev);

	shhwtstamps = skb_hwtstamps(skb);
	memset(shhwtstamps, 0, sizeof(*shhwtstamps));
	shhwtstamps->hwtstamp = ns_to_ktime(ns);
out:
	pch_ch_event_write(pdev, RX_SNAPSHOT_LOCKED);
}

static void
pch_tx_timestamp(struct pch_gbe_adapter *adapter, struct sk_buff *skb)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct pci_dev *pdev;
	struct skb_shared_info *shtx;
	u64 ns;
	u32 cnt, val;

	shtx = skb_shinfo(skb);
	if (likely(!(shtx->tx_flags & SKBTX_HW_TSTAMP && adapter->hwts_tx_en)))
		return;

	shtx->tx_flags |= SKBTX_IN_PROGRESS;

	/* Get ieee1588's dev information */
	pdev = adapter->ptp_pdev;

	/*
	 * This really stinks, but we have to poll for the Tx time stamp.
	 */
	for (cnt = 0; cnt < 100; cnt++) {
		val = pch_ch_event_read(pdev);
		if (val & TX_SNAPSHOT_LOCKED)
			break;
		udelay(1);
	}
	if (!(val & TX_SNAPSHOT_LOCKED)) {
		shtx->tx_flags &= ~SKBTX_IN_PROGRESS;
		return;
	}

	ns = pch_tx_snap_read(pdev);

	memset(&shhwtstamps, 0, sizeof(shhwtstamps));
	shhwtstamps.hwtstamp = ns_to_ktime(ns);
	skb_tstamp_tx(skb, &shhwtstamps);

	pch_ch_event_write(pdev, TX_SNAPSHOT_LOCKED);
}

static int hwtstamp_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct hwtstamp_config cfg;
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pci_dev *pdev;
	u8 station[20];

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	/* Get ieee1588's dev information */
	pdev = adapter->ptp_pdev;

	if (cfg.tx_type != HWTSTAMP_TX_OFF && cfg.tx_type != HWTSTAMP_TX_ON)
		return -ERANGE;

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		adapter->hwts_rx_en = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		adapter->hwts_rx_en = 0;
		pch_ch_control_write(pdev, SLAVE_MODE | CAP_MODE0);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		adapter->hwts_rx_en = 1;
		pch_ch_control_write(pdev, MASTER_MODE | CAP_MODE0);
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
		adapter->hwts_rx_en = 1;
		pch_ch_control_write(pdev, V2_MODE | CAP_MODE2);
		strcpy(station, PTP_L4_MULTICAST_SA);
		pch_set_station_address(station, pdev);
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		adapter->hwts_rx_en = 1;
		pch_ch_control_write(pdev, V2_MODE | CAP_MODE2);
		strcpy(station, PTP_L2_MULTICAST_SA);
		pch_set_station_address(station, pdev);
		break;
	default:
		return -ERANGE;
	}

	adapter->hwts_tx_en = cfg.tx_type == HWTSTAMP_TX_ON;

	/* Clear out any old time stamps. */
	pch_ch_event_write(pdev, TX_SNAPSHOT_LOCKED | RX_SNAPSHOT_LOCKED);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static inline void pch_gbe_mac_load_mac_addr(struct pch_gbe_hw *hw)
{
	iowrite32(0x01, &hw->reg->MAC_ADDR_LOAD);
}

/**
 * pch_gbe_mac_read_mac_addr - Read MAC address
 * @hw:	            Pointer to the HW structure
 * Returns:
 *	0:			Successful.
 */
static s32 pch_gbe_mac_read_mac_addr(struct pch_gbe_hw *hw)
{
	struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);
	u32  adr1a, adr1b;

	adr1a = ioread32(&hw->reg->mac_adr[0].high);
	adr1b = ioread32(&hw->reg->mac_adr[0].low);

	hw->mac.addr[0] = (u8)(adr1a & 0xFF);
	hw->mac.addr[1] = (u8)((adr1a >> 8) & 0xFF);
	hw->mac.addr[2] = (u8)((adr1a >> 16) & 0xFF);
	hw->mac.addr[3] = (u8)((adr1a >> 24) & 0xFF);
	hw->mac.addr[4] = (u8)(adr1b & 0xFF);
	hw->mac.addr[5] = (u8)((adr1b >> 8) & 0xFF);

	netdev_dbg(adapter->netdev, "hw->mac.addr : %pM\n", hw->mac.addr);
	return 0;
}

/**
 * pch_gbe_wait_clr_bit - Wait to clear a bit
 * @reg:	Pointer of register
 * @bit:	Busy bit
 */
static void pch_gbe_wait_clr_bit(void __iomem *reg, u32 bit)
{
	u32 tmp;

	/* wait busy */
	if (readx_poll_timeout_atomic(ioread32, reg, tmp, !(tmp & bit), 0, 10))
		pr_err("Error: busy bit is not cleared\n");
}

/**
 * pch_gbe_mac_mar_set - Set MAC address register
 * @hw:	    Pointer to the HW structure
 * @addr:   Pointer to the MAC address
 * @index:  MAC address array register
 */
static void pch_gbe_mac_mar_set(struct pch_gbe_hw *hw, u8 * addr, u32 index)
{
	struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);
	u32 mar_low, mar_high, adrmask;

	netdev_dbg(adapter->netdev, "index : 0x%x\n", index);

	/*
	 * HW expects these in little endian so we reverse the byte order
	 * from network order (big endian) to little endian
	 */
	mar_high = ((u32) addr[0] | ((u32) addr[1] << 8) |
		   ((u32) addr[2] << 16) | ((u32) addr[3] << 24));
	mar_low = ((u32) addr[4] | ((u32) addr[5] << 8));
	/* Stop the MAC Address of index. */
	adrmask = ioread32(&hw->reg->ADDR_MASK);
	iowrite32((adrmask | (0x0001 << index)), &hw->reg->ADDR_MASK);
	/* wait busy */
	pch_gbe_wait_clr_bit(&hw->reg->ADDR_MASK, PCH_GBE_BUSY);
	/* Set the MAC address to the MAC address 1A/1B register */
	iowrite32(mar_high, &hw->reg->mac_adr[index].high);
	iowrite32(mar_low, &hw->reg->mac_adr[index].low);
	/* Start the MAC address of index */
	iowrite32((adrmask & ~(0x0001 << index)), &hw->reg->ADDR_MASK);
	/* wait busy */
	pch_gbe_wait_clr_bit(&hw->reg->ADDR_MASK, PCH_GBE_BUSY);
}

/**
 * pch_gbe_mac_reset_hw - Reset hardware
 * @hw:	Pointer to the HW structure
 */
static void pch_gbe_mac_reset_hw(struct pch_gbe_hw *hw)
{
	/* Read the MAC address. and store to the private data */
	pch_gbe_mac_read_mac_addr(hw);
	iowrite32(PCH_GBE_ALL_RST, &hw->reg->RESET);
	iowrite32(PCH_GBE_MODE_GMII_ETHER, &hw->reg->MODE);
	pch_gbe_wait_clr_bit(&hw->reg->RESET, PCH_GBE_ALL_RST);
	/* Setup the receive addresses */
	pch_gbe_mac_mar_set(hw, hw->mac.addr, 0);
	return;
}

static void pch_gbe_disable_mac_rx(struct pch_gbe_hw *hw)
{
	u32 rctl;
	/* Disables Receive MAC */
	rctl = ioread32(&hw->reg->MAC_RX_EN);
	iowrite32((rctl & ~PCH_GBE_MRE_MAC_RX_EN), &hw->reg->MAC_RX_EN);
}

static void pch_gbe_enable_mac_rx(struct pch_gbe_hw *hw)
{
	u32 rctl;
	/* Enables Receive MAC */
	rctl = ioread32(&hw->reg->MAC_RX_EN);
	iowrite32((rctl | PCH_GBE_MRE_MAC_RX_EN), &hw->reg->MAC_RX_EN);
}

/**
 * pch_gbe_mac_init_rx_addrs - Initialize receive address's
 * @hw:	Pointer to the HW structure
 * @mar_count: Receive address registers
 */
static void pch_gbe_mac_init_rx_addrs(struct pch_gbe_hw *hw, u16 mar_count)
{
	u32 i;

	/* Setup the receive address */
	pch_gbe_mac_mar_set(hw, hw->mac.addr, 0);

	/* Zero out the other receive addresses */
	for (i = 1; i < mar_count; i++) {
		iowrite32(0, &hw->reg->mac_adr[i].high);
		iowrite32(0, &hw->reg->mac_adr[i].low);
	}
	iowrite32(0xFFFE, &hw->reg->ADDR_MASK);
	/* wait busy */
	pch_gbe_wait_clr_bit(&hw->reg->ADDR_MASK, PCH_GBE_BUSY);
}

/**
 * pch_gbe_mac_force_mac_fc - Force the MAC's flow control settings
 * @hw:	            Pointer to the HW structure
 * Returns:
 *	0:			Successful.
 *	Negative value:		Failed.
 */
s32 pch_gbe_mac_force_mac_fc(struct pch_gbe_hw *hw)
{
	struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);
	struct pch_gbe_mac_info *mac = &hw->mac;
	u32 rx_fctrl;

	netdev_dbg(adapter->netdev, "mac->fc = %u\n", mac->fc);

	rx_fctrl = ioread32(&hw->reg->RX_FCTRL);

	switch (mac->fc) {
	case PCH_GBE_FC_NONE:
		rx_fctrl &= ~PCH_GBE_FL_CTRL_EN;
		mac->tx_fc_enable = false;
		break;
	case PCH_GBE_FC_RX_PAUSE:
		rx_fctrl |= PCH_GBE_FL_CTRL_EN;
		mac->tx_fc_enable = false;
		break;
	case PCH_GBE_FC_TX_PAUSE:
		rx_fctrl &= ~PCH_GBE_FL_CTRL_EN;
		mac->tx_fc_enable = true;
		break;
	case PCH_GBE_FC_FULL:
		rx_fctrl |= PCH_GBE_FL_CTRL_EN;
		mac->tx_fc_enable = true;
		break;
	default:
		netdev_err(adapter->netdev,
			   "Flow control param set incorrectly\n");
		return -EINVAL;
	}
	if (mac->link_duplex == DUPLEX_HALF)
		rx_fctrl &= ~PCH_GBE_FL_CTRL_EN;
	iowrite32(rx_fctrl, &hw->reg->RX_FCTRL);
	netdev_dbg(adapter->netdev,
		   "RX_FCTRL reg : 0x%08x  mac->tx_fc_enable : %d\n",
		   ioread32(&hw->reg->RX_FCTRL), mac->tx_fc_enable);
	return 0;
}

/**
 * pch_gbe_mac_set_wol_event - Set wake-on-lan event
 * @hw:     Pointer to the HW structure
 * @wu_evt: Wake up event
 */
static void pch_gbe_mac_set_wol_event(struct pch_gbe_hw *hw, u32 wu_evt)
{
	struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);
	u32 addr_mask;

	netdev_dbg(adapter->netdev, "wu_evt : 0x%08x  ADDR_MASK reg : 0x%08x\n",
		   wu_evt, ioread32(&hw->reg->ADDR_MASK));

	if (wu_evt) {
		/* Set Wake-On-Lan address mask */
		addr_mask = ioread32(&hw->reg->ADDR_MASK);
		iowrite32(addr_mask, &hw->reg->WOL_ADDR_MASK);
		/* wait busy */
		pch_gbe_wait_clr_bit(&hw->reg->WOL_ADDR_MASK, PCH_GBE_WLA_BUSY);
		iowrite32(0, &hw->reg->WOL_ST);
		iowrite32((wu_evt | PCH_GBE_WLC_WOL_MODE), &hw->reg->WOL_CTRL);
		iowrite32(0x02, &hw->reg->TCPIP_ACC);
		iowrite32(PCH_GBE_INT_ENABLE_MASK, &hw->reg->INT_EN);
	} else {
		iowrite32(0, &hw->reg->WOL_CTRL);
		iowrite32(0, &hw->reg->WOL_ST);
	}
	return;
}

/**
 * pch_gbe_mac_ctrl_miim - Control MIIM interface
 * @hw:   Pointer to the HW structure
 * @addr: Address of PHY
 * @dir:  Operetion. (Write or Read)
 * @reg:  Access register of PHY
 * @data: Write data.
 *
 * Returns: Read date.
 */
u16 pch_gbe_mac_ctrl_miim(struct pch_gbe_hw *hw, u32 addr, u32 dir, u32 reg,
			u16 data)
{
	struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);
	unsigned long flags;
	u32 data_out;

	spin_lock_irqsave(&hw->miim_lock, flags);

	if (readx_poll_timeout_atomic(ioread32, &hw->reg->MIIM, data_out,
				      data_out & PCH_GBE_MIIM_OPER_READY, 20, 2000)) {
		netdev_err(adapter->netdev, "pch-gbe.miim won't go Ready\n");
		spin_unlock_irqrestore(&hw->miim_lock, flags);
		return 0;	/* No way to indicate timeout error */
	}
	iowrite32(((reg << PCH_GBE_MIIM_REG_ADDR_SHIFT) |
		  (addr << PCH_GBE_MIIM_PHY_ADDR_SHIFT) |
		  dir | data), &hw->reg->MIIM);
	readx_poll_timeout_atomic(ioread32, &hw->reg->MIIM, data_out,
				  data_out & PCH_GBE_MIIM_OPER_READY, 20, 2000);
	spin_unlock_irqrestore(&hw->miim_lock, flags);

	netdev_dbg(adapter->netdev, "PHY %s: reg=%d, data=0x%04X\n",
		   dir == PCH_GBE_MIIM_OPER_READ ? "READ" : "WRITE", reg,
		   dir == PCH_GBE_MIIM_OPER_READ ? data_out : data);
	return (u16) data_out;
}

/**
 * pch_gbe_mac_set_pause_packet - Set pause packet
 * @hw:   Pointer to the HW structure
 */
static void pch_gbe_mac_set_pause_packet(struct pch_gbe_hw *hw)
{
	struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);
	unsigned long tmp2, tmp3;

	/* Set Pause packet */
	tmp2 = hw->mac.addr[1];
	tmp2 = (tmp2 << 8) | hw->mac.addr[0];
	tmp2 = PCH_GBE_PAUSE_PKT2_VALUE | (tmp2 << 16);

	tmp3 = hw->mac.addr[5];
	tmp3 = (tmp3 << 8) | hw->mac.addr[4];
	tmp3 = (tmp3 << 8) | hw->mac.addr[3];
	tmp3 = (tmp3 << 8) | hw->mac.addr[2];

	iowrite32(PCH_GBE_PAUSE_PKT1_VALUE, &hw->reg->PAUSE_PKT1);
	iowrite32(tmp2, &hw->reg->PAUSE_PKT2);
	iowrite32(tmp3, &hw->reg->PAUSE_PKT3);
	iowrite32(PCH_GBE_PAUSE_PKT4_VALUE, &hw->reg->PAUSE_PKT4);
	iowrite32(PCH_GBE_PAUSE_PKT5_VALUE, &hw->reg->PAUSE_PKT5);

	/* Transmit Pause Packet */
	iowrite32(PCH_GBE_PS_PKT_RQ, &hw->reg->PAUSE_REQ);

	netdev_dbg(adapter->netdev,
		   "PAUSE_PKT1-5 reg : 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
		   ioread32(&hw->reg->PAUSE_PKT1),
		   ioread32(&hw->reg->PAUSE_PKT2),
		   ioread32(&hw->reg->PAUSE_PKT3),
		   ioread32(&hw->reg->PAUSE_PKT4),
		   ioread32(&hw->reg->PAUSE_PKT5));

	return;
}


/**
 * pch_gbe_alloc_queues - Allocate memory for all rings
 * @adapter:  Board private structure to initialize
 * Returns:
 *	0:	Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_alloc_queues(struct pch_gbe_adapter *adapter)
{
	adapter->tx_ring = devm_kzalloc(&adapter->pdev->dev,
					sizeof(*adapter->tx_ring), GFP_KERNEL);
	if (!adapter->tx_ring)
		return -ENOMEM;

	adapter->rx_ring = devm_kzalloc(&adapter->pdev->dev,
					sizeof(*adapter->rx_ring), GFP_KERNEL);
	if (!adapter->rx_ring)
		return -ENOMEM;
	return 0;
}

/**
 * pch_gbe_init_stats - Initialize status
 * @adapter:  Board private structure to initialize
 */
static void pch_gbe_init_stats(struct pch_gbe_adapter *adapter)
{
	memset(&adapter->stats, 0, sizeof(adapter->stats));
	return;
}

/**
 * pch_gbe_init_phy - Initialize PHY
 * @adapter:  Board private structure to initialize
 * Returns:
 *	0:	Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_init_phy(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u32 addr;
	u16 bmcr, stat;

	/* Discover phy addr by searching addrs in order {1,0,2,..., 31} */
	for (addr = 0; addr < PCH_GBE_PHY_REGS_LEN; addr++) {
		adapter->mii.phy_id = (addr == 0) ? 1 : (addr == 1) ? 0 : addr;
		bmcr = pch_gbe_mdio_read(netdev, adapter->mii.phy_id, MII_BMCR);
		stat = pch_gbe_mdio_read(netdev, adapter->mii.phy_id, MII_BMSR);
		stat = pch_gbe_mdio_read(netdev, adapter->mii.phy_id, MII_BMSR);
		if (!((bmcr == 0xFFFF) || ((stat == 0) && (bmcr == 0))))
			break;
	}
	adapter->hw.phy.addr = adapter->mii.phy_id;
	netdev_dbg(netdev, "phy_addr = %d\n", adapter->mii.phy_id);
	if (addr == PCH_GBE_PHY_REGS_LEN)
		return -EAGAIN;
	/* Selected the phy and isolate the rest */
	for (addr = 0; addr < PCH_GBE_PHY_REGS_LEN; addr++) {
		if (addr != adapter->mii.phy_id) {
			pch_gbe_mdio_write(netdev, addr, MII_BMCR,
					   BMCR_ISOLATE);
		} else {
			bmcr = pch_gbe_mdio_read(netdev, addr, MII_BMCR);
			pch_gbe_mdio_write(netdev, addr, MII_BMCR,
					   bmcr & ~BMCR_ISOLATE);
		}
	}

	/* MII setup */
	adapter->mii.phy_id_mask = 0x1F;
	adapter->mii.reg_num_mask = 0x1F;
	adapter->mii.dev = adapter->netdev;
	adapter->mii.mdio_read = pch_gbe_mdio_read;
	adapter->mii.mdio_write = pch_gbe_mdio_write;
	adapter->mii.supports_gmii = mii_check_gmii_support(&adapter->mii);
	return 0;
}

/**
 * pch_gbe_mdio_read - The read function for mii
 * @netdev: Network interface device structure
 * @addr:   Phy ID
 * @reg:    Access location
 * Returns:
 *	0:	Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_mdio_read(struct net_device *netdev, int addr, int reg)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;

	return pch_gbe_mac_ctrl_miim(hw, addr, PCH_GBE_HAL_MIIM_READ, reg,
				     (u16) 0);
}

/**
 * pch_gbe_mdio_write - The write function for mii
 * @netdev: Network interface device structure
 * @addr:   Phy ID (not used)
 * @reg:    Access location
 * @data:   Write data
 */
static void pch_gbe_mdio_write(struct net_device *netdev,
			       int addr, int reg, int data)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;

	pch_gbe_mac_ctrl_miim(hw, addr, PCH_GBE_HAL_MIIM_WRITE, reg, data);
}

/**
 * pch_gbe_reset_task - Reset processing at the time of transmission timeout
 * @work:  Pointer of board private structure
 */
static void pch_gbe_reset_task(struct work_struct *work)
{
	struct pch_gbe_adapter *adapter;
	adapter = container_of(work, struct pch_gbe_adapter, reset_task);

	rtnl_lock();
	pch_gbe_reinit_locked(adapter);
	rtnl_unlock();
}

/**
 * pch_gbe_reinit_locked- Re-initialization
 * @adapter:  Board private structure
 */
void pch_gbe_reinit_locked(struct pch_gbe_adapter *adapter)
{
	pch_gbe_down(adapter);
	pch_gbe_up(adapter);
}

/**
 * pch_gbe_reset - Reset GbE
 * @adapter:  Board private structure
 */
void pch_gbe_reset(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pch_gbe_hw *hw = &adapter->hw;
	s32 ret_val;

	pch_gbe_mac_reset_hw(hw);
	/* reprogram multicast address register after reset */
	pch_gbe_set_multi(netdev);
	/* Setup the receive address. */
	pch_gbe_mac_init_rx_addrs(hw, PCH_GBE_MAR_ENTRIES);

	ret_val = pch_gbe_phy_get_id(hw);
	if (ret_val) {
		netdev_err(adapter->netdev, "pch_gbe_phy_get_id error\n");
		return;
	}
	pch_gbe_phy_init_setting(hw);
	/* Setup Mac interface option RGMII */
	pch_gbe_phy_set_rgmii(hw);
}

/**
 * pch_gbe_free_irq - Free an interrupt
 * @adapter:  Board private structure
 */
static void pch_gbe_free_irq(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	free_irq(adapter->irq, netdev);
	pci_free_irq_vectors(adapter->pdev);
}

/**
 * pch_gbe_irq_disable - Mask off interrupt generation on the NIC
 * @adapter:  Board private structure
 */
static void pch_gbe_irq_disable(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;

	atomic_inc(&adapter->irq_sem);
	iowrite32(0, &hw->reg->INT_EN);
	ioread32(&hw->reg->INT_ST);
	synchronize_irq(adapter->irq);

	netdev_dbg(adapter->netdev, "INT_EN reg : 0x%08x\n",
		   ioread32(&hw->reg->INT_EN));
}

/**
 * pch_gbe_irq_enable - Enable default interrupt generation settings
 * @adapter:  Board private structure
 */
static void pch_gbe_irq_enable(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;

	if (likely(atomic_dec_and_test(&adapter->irq_sem)))
		iowrite32(PCH_GBE_INT_ENABLE_MASK, &hw->reg->INT_EN);
	ioread32(&hw->reg->INT_ST);
	netdev_dbg(adapter->netdev, "INT_EN reg : 0x%08x\n",
		   ioread32(&hw->reg->INT_EN));
}



/**
 * pch_gbe_setup_tctl - configure the Transmit control registers
 * @adapter:  Board private structure
 */
static void pch_gbe_setup_tctl(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 tx_mode, tcpip;

	tx_mode = PCH_GBE_TM_LONG_PKT |
		PCH_GBE_TM_ST_AND_FD |
		PCH_GBE_TM_SHORT_PKT |
		PCH_GBE_TM_TH_TX_STRT_8 |
		PCH_GBE_TM_TH_ALM_EMP_4 | PCH_GBE_TM_TH_ALM_FULL_8;

	iowrite32(tx_mode, &hw->reg->TX_MODE);

	tcpip = ioread32(&hw->reg->TCPIP_ACC);
	tcpip |= PCH_GBE_TX_TCPIPACC_EN;
	iowrite32(tcpip, &hw->reg->TCPIP_ACC);
	return;
}

/**
 * pch_gbe_configure_tx - Configure Transmit Unit after Reset
 * @adapter:  Board private structure
 */
static void pch_gbe_configure_tx(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 tdba, tdlen, dctrl;

	netdev_dbg(adapter->netdev, "dma addr = 0x%08llx  size = 0x%08x\n",
		   (unsigned long long)adapter->tx_ring->dma,
		   adapter->tx_ring->size);

	/* Setup the HW Tx Head and Tail descriptor pointers */
	tdba = adapter->tx_ring->dma;
	tdlen = adapter->tx_ring->size - 0x10;
	iowrite32(tdba, &hw->reg->TX_DSC_BASE);
	iowrite32(tdlen, &hw->reg->TX_DSC_SIZE);
	iowrite32(tdba, &hw->reg->TX_DSC_SW_P);

	/* Enables Transmission DMA */
	dctrl = ioread32(&hw->reg->DMA_CTRL);
	dctrl |= PCH_GBE_TX_DMA_EN;
	iowrite32(dctrl, &hw->reg->DMA_CTRL);
}

/**
 * pch_gbe_setup_rctl - Configure the receive control registers
 * @adapter:  Board private structure
 */
static void pch_gbe_setup_rctl(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 rx_mode, tcpip;

	rx_mode = PCH_GBE_ADD_FIL_EN | PCH_GBE_MLT_FIL_EN |
	PCH_GBE_RH_ALM_EMP_4 | PCH_GBE_RH_ALM_FULL_4 | PCH_GBE_RH_RD_TRG_8;

	iowrite32(rx_mode, &hw->reg->RX_MODE);

	tcpip = ioread32(&hw->reg->TCPIP_ACC);

	tcpip |= PCH_GBE_RX_TCPIPACC_OFF;
	tcpip &= ~PCH_GBE_RX_TCPIPACC_EN;
	iowrite32(tcpip, &hw->reg->TCPIP_ACC);
	return;
}

/**
 * pch_gbe_configure_rx - Configure Receive Unit after Reset
 * @adapter:  Board private structure
 */
static void pch_gbe_configure_rx(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 rdba, rdlen, rxdma;

	netdev_dbg(adapter->netdev, "dma adr = 0x%08llx  size = 0x%08x\n",
		   (unsigned long long)adapter->rx_ring->dma,
		   adapter->rx_ring->size);

	pch_gbe_mac_force_mac_fc(hw);

	pch_gbe_disable_mac_rx(hw);

	/* Disables Receive DMA */
	rxdma = ioread32(&hw->reg->DMA_CTRL);
	rxdma &= ~PCH_GBE_RX_DMA_EN;
	iowrite32(rxdma, &hw->reg->DMA_CTRL);

	netdev_dbg(adapter->netdev,
		   "MAC_RX_EN reg = 0x%08x  DMA_CTRL reg = 0x%08x\n",
		   ioread32(&hw->reg->MAC_RX_EN),
		   ioread32(&hw->reg->DMA_CTRL));

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring */
	rdba = adapter->rx_ring->dma;
	rdlen = adapter->rx_ring->size - 0x10;
	iowrite32(rdba, &hw->reg->RX_DSC_BASE);
	iowrite32(rdlen, &hw->reg->RX_DSC_SIZE);
	iowrite32((rdba + rdlen), &hw->reg->RX_DSC_SW_P);
}

/**
 * pch_gbe_unmap_and_free_tx_resource - Unmap and free tx socket buffer
 * @adapter:     Board private structure
 * @buffer_info: Buffer information structure
 */
static void pch_gbe_unmap_and_free_tx_resource(
	struct pch_gbe_adapter *adapter, struct pch_gbe_buffer *buffer_info)
{
	if (buffer_info->mapped) {
		dma_unmap_single(&adapter->pdev->dev, buffer_info->dma,
				 buffer_info->length, DMA_TO_DEVICE);
		buffer_info->mapped = false;
	}
	if (buffer_info->skb) {
		dev_kfree_skb_any(buffer_info->skb);
		buffer_info->skb = NULL;
	}
}

/**
 * pch_gbe_unmap_and_free_rx_resource - Unmap and free rx socket buffer
 * @adapter:      Board private structure
 * @buffer_info:  Buffer information structure
 */
static void pch_gbe_unmap_and_free_rx_resource(
					struct pch_gbe_adapter *adapter,
					struct pch_gbe_buffer *buffer_info)
{
	if (buffer_info->mapped) {
		dma_unmap_single(&adapter->pdev->dev, buffer_info->dma,
				 buffer_info->length, DMA_FROM_DEVICE);
		buffer_info->mapped = false;
	}
	if (buffer_info->skb) {
		dev_kfree_skb_any(buffer_info->skb);
		buffer_info->skb = NULL;
	}
}

/**
 * pch_gbe_clean_tx_ring - Free Tx Buffers
 * @adapter:  Board private structure
 * @tx_ring:  Ring to be cleaned
 */
static void pch_gbe_clean_tx_ring(struct pch_gbe_adapter *adapter,
				   struct pch_gbe_tx_ring *tx_ring)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	struct pch_gbe_buffer *buffer_info;
	unsigned long size;
	unsigned int i;

	/* Free all the Tx ring sk_buffs */
	for (i = 0; i < tx_ring->count; i++) {
		buffer_info = &tx_ring->buffer_info[i];
		pch_gbe_unmap_and_free_tx_resource(adapter, buffer_info);
	}
	netdev_dbg(adapter->netdev,
		   "call pch_gbe_unmap_and_free_tx_resource() %d count\n", i);

	size = (unsigned long)sizeof(struct pch_gbe_buffer) * tx_ring->count;
	memset(tx_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);
	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	iowrite32(tx_ring->dma, &hw->reg->TX_DSC_HW_P);
	iowrite32((tx_ring->size - 0x10), &hw->reg->TX_DSC_SIZE);
}

/**
 * pch_gbe_clean_rx_ring - Free Rx Buffers
 * @adapter:  Board private structure
 * @rx_ring:  Ring to free buffers from
 */
static void
pch_gbe_clean_rx_ring(struct pch_gbe_adapter *adapter,
		      struct pch_gbe_rx_ring *rx_ring)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	struct pch_gbe_buffer *buffer_info;
	unsigned long size;
	unsigned int i;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		buffer_info = &rx_ring->buffer_info[i];
		pch_gbe_unmap_and_free_rx_resource(adapter, buffer_info);
	}
	netdev_dbg(adapter->netdev,
		   "call pch_gbe_unmap_and_free_rx_resource() %d count\n", i);
	size = (unsigned long)sizeof(struct pch_gbe_buffer) * rx_ring->count;
	memset(rx_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);
	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
	iowrite32(rx_ring->dma, &hw->reg->RX_DSC_HW_P);
	iowrite32((rx_ring->size - 0x10), &hw->reg->RX_DSC_SIZE);
}

static void pch_gbe_set_rgmii_ctrl(struct pch_gbe_adapter *adapter, u16 speed,
				    u16 duplex)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	unsigned long rgmii = 0;

	/* Set the RGMII control. */
	switch (speed) {
	case SPEED_10:
		rgmii = (PCH_GBE_RGMII_RATE_2_5M |
			 PCH_GBE_MAC_RGMII_CTRL_SETTING);
		break;
	case SPEED_100:
		rgmii = (PCH_GBE_RGMII_RATE_25M |
			 PCH_GBE_MAC_RGMII_CTRL_SETTING);
		break;
	case SPEED_1000:
		rgmii = (PCH_GBE_RGMII_RATE_125M |
			 PCH_GBE_MAC_RGMII_CTRL_SETTING);
		break;
	}
	iowrite32(rgmii, &hw->reg->RGMII_CTRL);
}
static void pch_gbe_set_mode(struct pch_gbe_adapter *adapter, u16 speed,
			      u16 duplex)
{
	struct net_device *netdev = adapter->netdev;
	struct pch_gbe_hw *hw = &adapter->hw;
	unsigned long mode = 0;

	/* Set the communication mode */
	switch (speed) {
	case SPEED_10:
		mode = PCH_GBE_MODE_MII_ETHER;
		netdev->tx_queue_len = 10;
		break;
	case SPEED_100:
		mode = PCH_GBE_MODE_MII_ETHER;
		netdev->tx_queue_len = 100;
		break;
	case SPEED_1000:
		mode = PCH_GBE_MODE_GMII_ETHER;
		break;
	}
	if (duplex == DUPLEX_FULL)
		mode |= PCH_GBE_MODE_FULL_DUPLEX;
	else
		mode |= PCH_GBE_MODE_HALF_DUPLEX;
	iowrite32(mode, &hw->reg->MODE);
}

/**
 * pch_gbe_watchdog - Watchdog process
 * @t:  timer list containing a Board private structure
 */
static void pch_gbe_watchdog(struct timer_list *t)
{
	struct pch_gbe_adapter *adapter = from_timer(adapter, t,
						     watchdog_timer);
	struct net_device *netdev = adapter->netdev;
	struct pch_gbe_hw *hw = &adapter->hw;

	netdev_dbg(netdev, "right now = %ld\n", jiffies);

	pch_gbe_update_stats(adapter);
	if ((mii_link_ok(&adapter->mii)) && (!netif_carrier_ok(netdev))) {
		struct ethtool_cmd cmd = { .cmd = ETHTOOL_GSET };
		netdev->tx_queue_len = adapter->tx_queue_len;
		/* mii library handles link maintenance tasks */
		mii_ethtool_gset(&adapter->mii, &cmd);
		hw->mac.link_speed = ethtool_cmd_speed(&cmd);
		hw->mac.link_duplex = cmd.duplex;
		/* Set the RGMII control. */
		pch_gbe_set_rgmii_ctrl(adapter, hw->mac.link_speed,
						hw->mac.link_duplex);
		/* Set the communication mode */
		pch_gbe_set_mode(adapter, hw->mac.link_speed,
				 hw->mac.link_duplex);
		netdev_dbg(netdev,
			   "Link is Up %d Mbps %s-Duplex\n",
			   hw->mac.link_speed,
			   cmd.duplex == DUPLEX_FULL ? "Full" : "Half");
		netif_carrier_on(netdev);
		netif_wake_queue(netdev);
	} else if ((!mii_link_ok(&adapter->mii)) &&
		   (netif_carrier_ok(netdev))) {
		netdev_dbg(netdev, "NIC Link is Down\n");
		hw->mac.link_speed = SPEED_10;
		hw->mac.link_duplex = DUPLEX_HALF;
		netif_carrier_off(netdev);
		netif_stop_queue(netdev);
	}
	mod_timer(&adapter->watchdog_timer,
		  round_jiffies(jiffies + PCH_GBE_WATCHDOG_PERIOD));
}

/**
 * pch_gbe_tx_queue - Carry out queuing of the transmission data
 * @adapter:  Board private structure
 * @tx_ring:  Tx descriptor ring structure
 * @skb:      Sockt buffer structure
 */
static void pch_gbe_tx_queue(struct pch_gbe_adapter *adapter,
			      struct pch_gbe_tx_ring *tx_ring,
			      struct sk_buff *skb)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	struct pch_gbe_tx_desc *tx_desc;
	struct pch_gbe_buffer *buffer_info;
	struct sk_buff *tmp_skb;
	unsigned int frame_ctrl;
	unsigned int ring_num;

	/*-- Set frame control --*/
	frame_ctrl = 0;
	if (unlikely(skb->len < PCH_GBE_SHORT_PKT))
		frame_ctrl |= PCH_GBE_TXD_CTRL_APAD;
	if (skb->ip_summed == CHECKSUM_NONE)
		frame_ctrl |= PCH_GBE_TXD_CTRL_TCPIP_ACC_OFF;

	/* Performs checksum processing */
	/*
	 * It is because the hardware accelerator does not support a checksum,
	 * when the received data size is less than 64 bytes.
	 */
	if (skb->len < PCH_GBE_SHORT_PKT && skb->ip_summed != CHECKSUM_NONE) {
		frame_ctrl |= PCH_GBE_TXD_CTRL_APAD |
			      PCH_GBE_TXD_CTRL_TCPIP_ACC_OFF;
		if (skb->protocol == htons(ETH_P_IP)) {
			struct iphdr *iph = ip_hdr(skb);
			unsigned int offset;
			offset = skb_transport_offset(skb);
			if (iph->protocol == IPPROTO_TCP) {
				skb->csum = 0;
				tcp_hdr(skb)->check = 0;
				skb->csum = skb_checksum(skb, offset,
							 skb->len - offset, 0);
				tcp_hdr(skb)->check =
					csum_tcpudp_magic(iph->saddr,
							  iph->daddr,
							  skb->len - offset,
							  IPPROTO_TCP,
							  skb->csum);
			} else if (iph->protocol == IPPROTO_UDP) {
				skb->csum = 0;
				udp_hdr(skb)->check = 0;
				skb->csum =
					skb_checksum(skb, offset,
						     skb->len - offset, 0);
				udp_hdr(skb)->check =
					csum_tcpudp_magic(iph->saddr,
							  iph->daddr,
							  skb->len - offset,
							  IPPROTO_UDP,
							  skb->csum);
			}
		}
	}

	ring_num = tx_ring->next_to_use;
	if (unlikely((ring_num + 1) == tx_ring->count))
		tx_ring->next_to_use = 0;
	else
		tx_ring->next_to_use = ring_num + 1;


	buffer_info = &tx_ring->buffer_info[ring_num];
	tmp_skb = buffer_info->skb;

	/* [Header:14][payload] ---> [Header:14][paddong:2][payload]    */
	memcpy(tmp_skb->data, skb->data, ETH_HLEN);
	tmp_skb->data[ETH_HLEN] = 0x00;
	tmp_skb->data[ETH_HLEN + 1] = 0x00;
	tmp_skb->len = skb->len;
	memcpy(&tmp_skb->data[ETH_HLEN + 2], &skb->data[ETH_HLEN],
	       (skb->len - ETH_HLEN));
	/*-- Set Buffer information --*/
	buffer_info->length = tmp_skb->len;
	buffer_info->dma = dma_map_single(&adapter->pdev->dev, tmp_skb->data,
					  buffer_info->length,
					  DMA_TO_DEVICE);
	if (dma_mapping_error(&adapter->pdev->dev, buffer_info->dma)) {
		netdev_err(adapter->netdev, "TX DMA map failed\n");
		buffer_info->dma = 0;
		buffer_info->time_stamp = 0;
		tx_ring->next_to_use = ring_num;
		return;
	}
	buffer_info->mapped = true;
	buffer_info->time_stamp = jiffies;

	/*-- Set Tx descriptor --*/
	tx_desc = PCH_GBE_TX_DESC(*tx_ring, ring_num);
	tx_desc->buffer_addr = (buffer_info->dma);
	tx_desc->length = (tmp_skb->len);
	tx_desc->tx_words_eob = ((tmp_skb->len + 3));
	tx_desc->tx_frame_ctrl = (frame_ctrl);
	tx_desc->gbec_status = (DSC_INIT16);

	if (unlikely(++ring_num == tx_ring->count))
		ring_num = 0;

	/* Update software pointer of TX descriptor */
	iowrite32(tx_ring->dma +
		  (int)sizeof(struct pch_gbe_tx_desc) * ring_num,
		  &hw->reg->TX_DSC_SW_P);

	pch_tx_timestamp(adapter, skb);

	dev_kfree_skb_any(skb);
}

/**
 * pch_gbe_update_stats - Update the board statistics counters
 * @adapter:  Board private structure
 */
void pch_gbe_update_stats(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_hw_stats *stats = &adapter->stats;
	unsigned long flags;

	/*
	 * Prevent stats update while adapter is being reset, or if the pci
	 * connection is down.
	 */
	if ((pdev->error_state) && (pdev->error_state != pci_channel_io_normal))
		return;

	spin_lock_irqsave(&adapter->stats_lock, flags);

	/* Update device status "adapter->stats" */
	stats->rx_errors = stats->rx_crc_errors + stats->rx_frame_errors;
	stats->tx_errors = stats->tx_length_errors +
	    stats->tx_aborted_errors +
	    stats->tx_carrier_errors + stats->tx_timeout_count;

	/* Update network device status "adapter->net_stats" */
	netdev->stats.rx_packets = stats->rx_packets;
	netdev->stats.rx_bytes = stats->rx_bytes;
	netdev->stats.rx_dropped = stats->rx_dropped;
	netdev->stats.tx_packets = stats->tx_packets;
	netdev->stats.tx_bytes = stats->tx_bytes;
	netdev->stats.tx_dropped = stats->tx_dropped;
	/* Fill out the OS statistics structure */
	netdev->stats.multicast = stats->multicast;
	netdev->stats.collisions = stats->collisions;
	/* Rx Errors */
	netdev->stats.rx_errors = stats->rx_errors;
	netdev->stats.rx_crc_errors = stats->rx_crc_errors;
	netdev->stats.rx_frame_errors = stats->rx_frame_errors;
	/* Tx Errors */
	netdev->stats.tx_errors = stats->tx_errors;
	netdev->stats.tx_aborted_errors = stats->tx_aborted_errors;
	netdev->stats.tx_carrier_errors = stats->tx_carrier_errors;

	spin_unlock_irqrestore(&adapter->stats_lock, flags);
}

static void pch_gbe_disable_dma_rx(struct pch_gbe_hw *hw)
{
	u32 rxdma;

	/* Disable Receive DMA */
	rxdma = ioread32(&hw->reg->DMA_CTRL);
	rxdma &= ~PCH_GBE_RX_DMA_EN;
	iowrite32(rxdma, &hw->reg->DMA_CTRL);
}

static void pch_gbe_enable_dma_rx(struct pch_gbe_hw *hw)
{
	u32 rxdma;

	/* Enables Receive DMA */
	rxdma = ioread32(&hw->reg->DMA_CTRL);
	rxdma |= PCH_GBE_RX_DMA_EN;
	iowrite32(rxdma, &hw->reg->DMA_CTRL);
}

/**
 * pch_gbe_intr - Interrupt Handler
 * @irq:   Interrupt number
 * @data:  Pointer to a network interface device structure
 * Returns:
 *	- IRQ_HANDLED:	Our interrupt
 *	- IRQ_NONE:	Not our interrupt
 */
static irqreturn_t pch_gbe_intr(int irq, void *data)
{
	struct net_device *netdev = data;
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 int_st;
	u32 int_en;

	/* Check request status */
	int_st = ioread32(&hw->reg->INT_ST);
	int_st = int_st & ioread32(&hw->reg->INT_EN);
	/* When request status is no interruption factor */
	if (unlikely(!int_st))
		return IRQ_NONE;	/* Not our interrupt. End processing. */
	netdev_dbg(netdev, "%s occur int_st = 0x%08x\n", __func__, int_st);
	if (int_st & PCH_GBE_INT_RX_FRAME_ERR)
		adapter->stats.intr_rx_frame_err_count++;
	if (int_st & PCH_GBE_INT_RX_FIFO_ERR)
		if (!adapter->rx_stop_flag) {
			adapter->stats.intr_rx_fifo_err_count++;
			netdev_dbg(netdev, "Rx fifo over run\n");
			adapter->rx_stop_flag = true;
			int_en = ioread32(&hw->reg->INT_EN);
			iowrite32((int_en & ~PCH_GBE_INT_RX_FIFO_ERR),
				  &hw->reg->INT_EN);
			pch_gbe_disable_dma_rx(&adapter->hw);
			int_st |= ioread32(&hw->reg->INT_ST);
			int_st = int_st & ioread32(&hw->reg->INT_EN);
		}
	if (int_st & PCH_GBE_INT_RX_DMA_ERR)
		adapter->stats.intr_rx_dma_err_count++;
	if (int_st & PCH_GBE_INT_TX_FIFO_ERR)
		adapter->stats.intr_tx_fifo_err_count++;
	if (int_st & PCH_GBE_INT_TX_DMA_ERR)
		adapter->stats.intr_tx_dma_err_count++;
	if (int_st & PCH_GBE_INT_TCPIP_ERR)
		adapter->stats.intr_tcpip_err_count++;
	/* When Rx descriptor is empty  */
	if ((int_st & PCH_GBE_INT_RX_DSC_EMP)) {
		adapter->stats.intr_rx_dsc_empty_count++;
		netdev_dbg(netdev, "Rx descriptor is empty\n");
		int_en = ioread32(&hw->reg->INT_EN);
		iowrite32((int_en & ~PCH_GBE_INT_RX_DSC_EMP), &hw->reg->INT_EN);
		if (hw->mac.tx_fc_enable) {
			/* Set Pause packet */
			pch_gbe_mac_set_pause_packet(hw);
		}
	}

	/* When request status is Receive interruption */
	if ((int_st & (PCH_GBE_INT_RX_DMA_CMPLT | PCH_GBE_INT_TX_CMPLT)) ||
	    (adapter->rx_stop_flag)) {
		if (likely(napi_schedule_prep(&adapter->napi))) {
			/* Enable only Rx Descriptor empty */
			atomic_inc(&adapter->irq_sem);
			int_en = ioread32(&hw->reg->INT_EN);
			int_en &=
			    ~(PCH_GBE_INT_RX_DMA_CMPLT | PCH_GBE_INT_TX_CMPLT);
			iowrite32(int_en, &hw->reg->INT_EN);
			/* Start polling for NAPI */
			__napi_schedule(&adapter->napi);
		}
	}
	netdev_dbg(netdev, "return = 0x%08x  INT_EN reg = 0x%08x\n",
		   IRQ_HANDLED, ioread32(&hw->reg->INT_EN));
	return IRQ_HANDLED;
}

/**
 * pch_gbe_alloc_rx_buffers - Replace used receive buffers; legacy & extended
 * @adapter:       Board private structure
 * @rx_ring:       Rx descriptor ring
 * @cleaned_count: Cleaned count
 */
static void
pch_gbe_alloc_rx_buffers(struct pch_gbe_adapter *adapter,
			 struct pch_gbe_rx_ring *rx_ring, int cleaned_count)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_hw *hw = &adapter->hw;
	struct pch_gbe_rx_desc *rx_desc;
	struct pch_gbe_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	unsigned int bufsz;

	bufsz = adapter->rx_buffer_len + NET_IP_ALIGN;
	i = rx_ring->next_to_use;

	while ((cleaned_count--)) {
		buffer_info = &rx_ring->buffer_info[i];
		skb = netdev_alloc_skb(netdev, bufsz);
		if (unlikely(!skb)) {
			/* Better luck next round */
			adapter->stats.rx_alloc_buff_failed++;
			break;
		}
		/* align */
		skb_reserve(skb, NET_IP_ALIGN);
		buffer_info->skb = skb;

		buffer_info->dma = dma_map_single(&pdev->dev,
						  buffer_info->rx_buffer,
						  buffer_info->length,
						  DMA_FROM_DEVICE);
		if (dma_mapping_error(&adapter->pdev->dev, buffer_info->dma)) {
			dev_kfree_skb(skb);
			buffer_info->skb = NULL;
			buffer_info->dma = 0;
			adapter->stats.rx_alloc_buff_failed++;
			break; /* while !buffer_info->skb */
		}
		buffer_info->mapped = true;
		rx_desc = PCH_GBE_RX_DESC(*rx_ring, i);
		rx_desc->buffer_addr = (buffer_info->dma);
		rx_desc->gbec_status = DSC_INIT16;

		netdev_dbg(netdev,
			   "i = %d  buffer_info->dma = 0x08%llx  buffer_info->length = 0x%x\n",
			   i, (unsigned long long)buffer_info->dma,
			   buffer_info->length);

		if (unlikely(++i == rx_ring->count))
			i = 0;
	}
	if (likely(rx_ring->next_to_use != i)) {
		rx_ring->next_to_use = i;
		if (unlikely(i-- == 0))
			i = (rx_ring->count - 1);
		iowrite32(rx_ring->dma +
			  (int)sizeof(struct pch_gbe_rx_desc) * i,
			  &hw->reg->RX_DSC_SW_P);
	}
	return;
}

static int
pch_gbe_alloc_rx_buffers_pool(struct pch_gbe_adapter *adapter,
			 struct pch_gbe_rx_ring *rx_ring, int cleaned_count)
{
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_buffer *buffer_info;
	unsigned int i;
	unsigned int bufsz;
	unsigned int size;

	bufsz = adapter->rx_buffer_len;

	size = rx_ring->count * bufsz + PCH_GBE_RESERVE_MEMORY;
	rx_ring->rx_buff_pool =
		dma_alloc_coherent(&pdev->dev, size,
				   &rx_ring->rx_buff_pool_logic, GFP_KERNEL);
	if (!rx_ring->rx_buff_pool)
		return -ENOMEM;

	rx_ring->rx_buff_pool_size = size;
	for (i = 0; i < rx_ring->count; i++) {
		buffer_info = &rx_ring->buffer_info[i];
		buffer_info->rx_buffer = rx_ring->rx_buff_pool + bufsz * i;
		buffer_info->length = bufsz;
	}
	return 0;
}

/**
 * pch_gbe_alloc_tx_buffers - Allocate transmit buffers
 * @adapter:   Board private structure
 * @tx_ring:   Tx descriptor ring
 */
static void pch_gbe_alloc_tx_buffers(struct pch_gbe_adapter *adapter,
					struct pch_gbe_tx_ring *tx_ring)
{
	struct pch_gbe_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	unsigned int bufsz;
	struct pch_gbe_tx_desc *tx_desc;

	bufsz =
	    adapter->hw.mac.max_frame_size + PCH_GBE_DMA_ALIGN + NET_IP_ALIGN;

	for (i = 0; i < tx_ring->count; i++) {
		buffer_info = &tx_ring->buffer_info[i];
		skb = netdev_alloc_skb(adapter->netdev, bufsz);
		skb_reserve(skb, PCH_GBE_DMA_ALIGN);
		buffer_info->skb = skb;
		tx_desc = PCH_GBE_TX_DESC(*tx_ring, i);
		tx_desc->gbec_status = (DSC_INIT16);
	}
	return;
}

/**
 * pch_gbe_clean_tx - Reclaim resources after transmit completes
 * @adapter:   Board private structure
 * @tx_ring:   Tx descriptor ring
 * Returns:
 *	true:  Cleaned the descriptor
 *	false: Not cleaned the descriptor
 */
static bool
pch_gbe_clean_tx(struct pch_gbe_adapter *adapter,
		 struct pch_gbe_tx_ring *tx_ring)
{
	struct pch_gbe_tx_desc *tx_desc;
	struct pch_gbe_buffer *buffer_info;
	struct sk_buff *skb;
	unsigned int i;
	unsigned int cleaned_count = 0;
	bool cleaned = false;
	int unused, thresh;

	netdev_dbg(adapter->netdev, "next_to_clean : %d\n",
		   tx_ring->next_to_clean);

	i = tx_ring->next_to_clean;
	tx_desc = PCH_GBE_TX_DESC(*tx_ring, i);
	netdev_dbg(adapter->netdev, "gbec_status:0x%04x  dma_status:0x%04x\n",
		   tx_desc->gbec_status, tx_desc->dma_status);

	unused = PCH_GBE_DESC_UNUSED(tx_ring);
	thresh = tx_ring->count - NAPI_POLL_WEIGHT;
	if ((tx_desc->gbec_status == DSC_INIT16) && (unused < thresh))
	{  /* current marked clean, tx queue filling up, do extra clean */
		int j, k;
		if (unused < 8) {  /* tx queue nearly full */
			netdev_dbg(adapter->netdev,
				   "clean_tx: transmit queue warning (%x,%x) unused=%d\n",
				   tx_ring->next_to_clean, tx_ring->next_to_use,
				   unused);
		}

		/* current marked clean, scan for more that need cleaning. */
		k = i;
		for (j = 0; j < NAPI_POLL_WEIGHT; j++)
		{
			tx_desc = PCH_GBE_TX_DESC(*tx_ring, k);
			if (tx_desc->gbec_status != DSC_INIT16) break; /*found*/
			if (++k >= tx_ring->count) k = 0;  /*increment, wrap*/
		}
		if (j < NAPI_POLL_WEIGHT) {
			netdev_dbg(adapter->netdev,
				   "clean_tx: unused=%d loops=%d found tx_desc[%x,%x:%x].gbec_status=%04x\n",
				   unused, j, i, k, tx_ring->next_to_use,
				   tx_desc->gbec_status);
			i = k;  /*found one to clean, usu gbec_status==2000.*/
		}
	}

	while ((tx_desc->gbec_status & DSC_INIT16) == 0x0000) {
		netdev_dbg(adapter->netdev, "gbec_status:0x%04x\n",
			   tx_desc->gbec_status);
		buffer_info = &tx_ring->buffer_info[i];
		skb = buffer_info->skb;
		cleaned = true;

		if ((tx_desc->gbec_status & PCH_GBE_TXD_GMAC_STAT_ABT)) {
			adapter->stats.tx_aborted_errors++;
			netdev_err(adapter->netdev, "Transfer Abort Error\n");
		} else if ((tx_desc->gbec_status & PCH_GBE_TXD_GMAC_STAT_CRSER)
			  ) {
			adapter->stats.tx_carrier_errors++;
			netdev_err(adapter->netdev,
				   "Transfer Carrier Sense Error\n");
		} else if ((tx_desc->gbec_status & PCH_GBE_TXD_GMAC_STAT_EXCOL)
			  ) {
			adapter->stats.tx_aborted_errors++;
			netdev_err(adapter->netdev,
				   "Transfer Collision Abort Error\n");
		} else if ((tx_desc->gbec_status &
			    (PCH_GBE_TXD_GMAC_STAT_SNGCOL |
			     PCH_GBE_TXD_GMAC_STAT_MLTCOL))) {
			adapter->stats.collisions++;
			adapter->stats.tx_packets++;
			adapter->stats.tx_bytes += skb->len;
			netdev_dbg(adapter->netdev, "Transfer Collision\n");
		} else if ((tx_desc->gbec_status & PCH_GBE_TXD_GMAC_STAT_CMPLT)
			  ) {
			adapter->stats.tx_packets++;
			adapter->stats.tx_bytes += skb->len;
		}
		if (buffer_info->mapped) {
			netdev_dbg(adapter->netdev,
				   "unmap buffer_info->dma : %d\n", i);
			dma_unmap_single(&adapter->pdev->dev, buffer_info->dma,
					 buffer_info->length, DMA_TO_DEVICE);
			buffer_info->mapped = false;
		}
		if (buffer_info->skb) {
			netdev_dbg(adapter->netdev,
				   "trim buffer_info->skb : %d\n", i);
			skb_trim(buffer_info->skb, 0);
		}
		tx_desc->gbec_status = DSC_INIT16;
		if (unlikely(++i == tx_ring->count))
			i = 0;
		tx_desc = PCH_GBE_TX_DESC(*tx_ring, i);

		/* weight of a sort for tx, to avoid endless transmit cleanup */
		if (cleaned_count++ == NAPI_POLL_WEIGHT) {
			cleaned = false;
			break;
		}
	}
	netdev_dbg(adapter->netdev,
		   "called pch_gbe_unmap_and_free_tx_resource() %d count\n",
		   cleaned_count);
	if (cleaned_count > 0)  { /*skip this if nothing cleaned*/
		/* Recover from running out of Tx resources in xmit_frame */
		netif_tx_lock(adapter->netdev);
		if (unlikely(cleaned && (netif_queue_stopped(adapter->netdev))))
		{
			netif_wake_queue(adapter->netdev);
			adapter->stats.tx_restart_count++;
			netdev_dbg(adapter->netdev, "Tx wake queue\n");
		}

		tx_ring->next_to_clean = i;

		netdev_dbg(adapter->netdev, "next_to_clean : %d\n",
			   tx_ring->next_to_clean);
		netif_tx_unlock(adapter->netdev);
	}
	return cleaned;
}

/**
 * pch_gbe_clean_rx - Send received data up the network stack; legacy
 * @adapter:     Board private structure
 * @rx_ring:     Rx descriptor ring
 * @work_done:   Completed count
 * @work_to_do:  Request count
 * Returns:
 *	true:  Cleaned the descriptor
 *	false: Not cleaned the descriptor
 */
static bool
pch_gbe_clean_rx(struct pch_gbe_adapter *adapter,
		 struct pch_gbe_rx_ring *rx_ring,
		 int *work_done, int work_to_do)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_buffer *buffer_info;
	struct pch_gbe_rx_desc *rx_desc;
	u32 length;
	unsigned int i;
	unsigned int cleaned_count = 0;
	bool cleaned = false;
	struct sk_buff *skb;
	u8 dma_status;
	u16 gbec_status;
	u32 tcp_ip_status;

	i = rx_ring->next_to_clean;

	while (*work_done < work_to_do) {
		/* Check Rx descriptor status */
		rx_desc = PCH_GBE_RX_DESC(*rx_ring, i);
		if (rx_desc->gbec_status == DSC_INIT16)
			break;
		cleaned = true;
		cleaned_count++;

		dma_status = rx_desc->dma_status;
		gbec_status = rx_desc->gbec_status;
		tcp_ip_status = rx_desc->tcp_ip_status;
		rx_desc->gbec_status = DSC_INIT16;
		buffer_info = &rx_ring->buffer_info[i];
		skb = buffer_info->skb;
		buffer_info->skb = NULL;

		/* unmap dma */
		dma_unmap_single(&pdev->dev, buffer_info->dma,
				   buffer_info->length, DMA_FROM_DEVICE);
		buffer_info->mapped = false;

		netdev_dbg(netdev,
			   "RxDecNo = 0x%04x  Status[DMA:0x%02x GBE:0x%04x TCP:0x%08x]  BufInf = 0x%p\n",
			   i, dma_status, gbec_status, tcp_ip_status,
			   buffer_info);
		/* Error check */
		if (unlikely(gbec_status & PCH_GBE_RXD_GMAC_STAT_NOTOCTAL)) {
			adapter->stats.rx_frame_errors++;
			netdev_err(netdev, "Receive Not Octal Error\n");
		} else if (unlikely(gbec_status &
				PCH_GBE_RXD_GMAC_STAT_NBLERR)) {
			adapter->stats.rx_frame_errors++;
			netdev_err(netdev, "Receive Nibble Error\n");
		} else if (unlikely(gbec_status &
				PCH_GBE_RXD_GMAC_STAT_CRCERR)) {
			adapter->stats.rx_crc_errors++;
			netdev_err(netdev, "Receive CRC Error\n");
		} else {
			/* get receive length */
			/* length convert[-3], length includes FCS length */
			length = (rx_desc->rx_words_eob) - 3 - ETH_FCS_LEN;
			if (rx_desc->rx_words_eob & 0x02)
				length = length - 4;
			/*
			 * buffer_info->rx_buffer: [Header:14][payload]
			 * skb->data: [Reserve:2][Header:14][payload]
			 */
			memcpy(skb->data, buffer_info->rx_buffer, length);

			/* update status of driver */
			adapter->stats.rx_bytes += length;
			adapter->stats.rx_packets++;
			if ((gbec_status & PCH_GBE_RXD_GMAC_STAT_MARMLT))
				adapter->stats.multicast++;
			/* Write meta date of skb */
			skb_put(skb, length);

			pch_rx_timestamp(adapter, skb);

			skb->protocol = eth_type_trans(skb, netdev);
			if (tcp_ip_status & PCH_GBE_RXD_ACC_STAT_TCPIPOK)
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			else
				skb->ip_summed = CHECKSUM_NONE;

			napi_gro_receive(&adapter->napi, skb);
			(*work_done)++;
			netdev_dbg(netdev,
				   "Receive skb->ip_summed: %d length: %d\n",
				   skb->ip_summed, length);
		}
		/* return some buffers to hardware, one at a time is too slow */
		if (unlikely(cleaned_count >= PCH_GBE_RX_BUFFER_WRITE)) {
			pch_gbe_alloc_rx_buffers(adapter, rx_ring,
						 cleaned_count);
			cleaned_count = 0;
		}
		if (++i == rx_ring->count)
			i = 0;
	}
	rx_ring->next_to_clean = i;
	if (cleaned_count)
		pch_gbe_alloc_rx_buffers(adapter, rx_ring, cleaned_count);
	return cleaned;
}

/**
 * pch_gbe_setup_tx_resources - Allocate Tx resources (Descriptors)
 * @adapter:  Board private structure
 * @tx_ring:  Tx descriptor ring (for a specific queue) to setup
 * Returns:
 *	0:		Successfully
 *	Negative value:	Failed
 */
int pch_gbe_setup_tx_resources(struct pch_gbe_adapter *adapter,
				struct pch_gbe_tx_ring *tx_ring)
{
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_tx_desc *tx_desc;
	int size;
	int desNo;

	size = (int)sizeof(struct pch_gbe_buffer) * tx_ring->count;
	tx_ring->buffer_info = vzalloc(size);
	if (!tx_ring->buffer_info)
		return -ENOMEM;

	tx_ring->size = tx_ring->count * (int)sizeof(struct pch_gbe_tx_desc);

	tx_ring->desc = dma_alloc_coherent(&pdev->dev, tx_ring->size,
					   &tx_ring->dma, GFP_KERNEL);
	if (!tx_ring->desc) {
		vfree(tx_ring->buffer_info);
		return -ENOMEM;
	}

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	for (desNo = 0; desNo < tx_ring->count; desNo++) {
		tx_desc = PCH_GBE_TX_DESC(*tx_ring, desNo);
		tx_desc->gbec_status = DSC_INIT16;
	}
	netdev_dbg(adapter->netdev,
		   "tx_ring->desc = 0x%p  tx_ring->dma = 0x%08llx next_to_clean = 0x%08x  next_to_use = 0x%08x\n",
		   tx_ring->desc, (unsigned long long)tx_ring->dma,
		   tx_ring->next_to_clean, tx_ring->next_to_use);
	return 0;
}

/**
 * pch_gbe_setup_rx_resources - Allocate Rx resources (Descriptors)
 * @adapter:  Board private structure
 * @rx_ring:  Rx descriptor ring (for a specific queue) to setup
 * Returns:
 *	0:		Successfully
 *	Negative value:	Failed
 */
int pch_gbe_setup_rx_resources(struct pch_gbe_adapter *adapter,
				struct pch_gbe_rx_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_rx_desc *rx_desc;
	int size;
	int desNo;

	size = (int)sizeof(struct pch_gbe_buffer) * rx_ring->count;
	rx_ring->buffer_info = vzalloc(size);
	if (!rx_ring->buffer_info)
		return -ENOMEM;

	rx_ring->size = rx_ring->count * (int)sizeof(struct pch_gbe_rx_desc);
	rx_ring->desc =	dma_alloc_coherent(&pdev->dev, rx_ring->size,
						  &rx_ring->dma, GFP_KERNEL);
	if (!rx_ring->desc) {
		vfree(rx_ring->buffer_info);
		return -ENOMEM;
	}
	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
	for (desNo = 0; desNo < rx_ring->count; desNo++) {
		rx_desc = PCH_GBE_RX_DESC(*rx_ring, desNo);
		rx_desc->gbec_status = DSC_INIT16;
	}
	netdev_dbg(adapter->netdev,
		   "rx_ring->desc = 0x%p  rx_ring->dma = 0x%08llx next_to_clean = 0x%08x  next_to_use = 0x%08x\n",
		   rx_ring->desc, (unsigned long long)rx_ring->dma,
		   rx_ring->next_to_clean, rx_ring->next_to_use);
	return 0;
}

/**
 * pch_gbe_free_tx_resources - Free Tx Resources
 * @adapter:  Board private structure
 * @tx_ring:  Tx descriptor ring for a specific queue
 */
void pch_gbe_free_tx_resources(struct pch_gbe_adapter *adapter,
				struct pch_gbe_tx_ring *tx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	pch_gbe_clean_tx_ring(adapter, tx_ring);
	vfree(tx_ring->buffer_info);
	tx_ring->buffer_info = NULL;
	dma_free_coherent(&pdev->dev, tx_ring->size, tx_ring->desc,
			  tx_ring->dma);
	tx_ring->desc = NULL;
}

/**
 * pch_gbe_free_rx_resources - Free Rx Resources
 * @adapter:  Board private structure
 * @rx_ring:  Ring to clean the resources from
 */
void pch_gbe_free_rx_resources(struct pch_gbe_adapter *adapter,
				struct pch_gbe_rx_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	pch_gbe_clean_rx_ring(adapter, rx_ring);
	vfree(rx_ring->buffer_info);
	rx_ring->buffer_info = NULL;
	dma_free_coherent(&pdev->dev, rx_ring->size, rx_ring->desc,
			  rx_ring->dma);
	rx_ring->desc = NULL;
}

/**
 * pch_gbe_request_irq - Allocate an interrupt line
 * @adapter:  Board private structure
 * Returns:
 *	0:		Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_request_irq(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int err;

	err = pci_alloc_irq_vectors(adapter->pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (err < 0)
		return err;

	adapter->irq = pci_irq_vector(adapter->pdev, 0);

	err = request_irq(adapter->irq, &pch_gbe_intr, IRQF_SHARED,
			  netdev->name, netdev);
	if (err)
		netdev_err(netdev, "Unable to allocate interrupt Error: %d\n",
			   err);
	netdev_dbg(netdev, "have_msi : %d  return : 0x%04x\n",
		   pci_dev_msi_enabled(adapter->pdev), err);
	return err;
}

/**
 * pch_gbe_up - Up GbE network device
 * @adapter:  Board private structure
 * Returns:
 *	0:		Successfully
 *	Negative value:	Failed
 */
int pch_gbe_up(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pch_gbe_tx_ring *tx_ring = adapter->tx_ring;
	struct pch_gbe_rx_ring *rx_ring = adapter->rx_ring;
	int err = -EINVAL;

	/* Ensure we have a valid MAC */
	if (!is_valid_ether_addr(adapter->hw.mac.addr)) {
		netdev_err(netdev, "Error: Invalid MAC address\n");
		goto out;
	}

	/* hardware has been reset, we need to reload some things */
	pch_gbe_set_multi(netdev);

	pch_gbe_setup_tctl(adapter);
	pch_gbe_configure_tx(adapter);
	pch_gbe_setup_rctl(adapter);
	pch_gbe_configure_rx(adapter);

	err = pch_gbe_request_irq(adapter);
	if (err) {
		netdev_err(netdev,
			   "Error: can't bring device up - irq request failed\n");
		goto out;
	}
	err = pch_gbe_alloc_rx_buffers_pool(adapter, rx_ring, rx_ring->count);
	if (err) {
		netdev_err(netdev,
			   "Error: can't bring device up - alloc rx buffers pool failed\n");
		goto freeirq;
	}
	pch_gbe_alloc_tx_buffers(adapter, tx_ring);
	pch_gbe_alloc_rx_buffers(adapter, rx_ring, rx_ring->count);
	adapter->tx_queue_len = netdev->tx_queue_len;
	pch_gbe_enable_dma_rx(&adapter->hw);
	pch_gbe_enable_mac_rx(&adapter->hw);

	mod_timer(&adapter->watchdog_timer, jiffies);

	napi_enable(&adapter->napi);
	pch_gbe_irq_enable(adapter);
	netif_start_queue(adapter->netdev);

	return 0;

freeirq:
	pch_gbe_free_irq(adapter);
out:
	return err;
}

/**
 * pch_gbe_down - Down GbE network device
 * @adapter:  Board private structure
 */
void pch_gbe_down(struct pch_gbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct pch_gbe_rx_ring *rx_ring = adapter->rx_ring;

	/* signal that we're down so the interrupt handler does not
	 * reschedule our watchdog timer */
	napi_disable(&adapter->napi);
	atomic_set(&adapter->irq_sem, 0);

	pch_gbe_irq_disable(adapter);
	pch_gbe_free_irq(adapter);

	del_timer_sync(&adapter->watchdog_timer);

	netdev->tx_queue_len = adapter->tx_queue_len;
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	if ((pdev->error_state) && (pdev->error_state != pci_channel_io_normal))
		pch_gbe_reset(adapter);
	pch_gbe_clean_tx_ring(adapter, adapter->tx_ring);
	pch_gbe_clean_rx_ring(adapter, adapter->rx_ring);

	dma_free_coherent(&adapter->pdev->dev, rx_ring->rx_buff_pool_size,
			  rx_ring->rx_buff_pool, rx_ring->rx_buff_pool_logic);
	rx_ring->rx_buff_pool_logic = 0;
	rx_ring->rx_buff_pool_size = 0;
	rx_ring->rx_buff_pool = NULL;
}

/**
 * pch_gbe_sw_init - Initialize general software structures (struct pch_gbe_adapter)
 * @adapter:  Board private structure to initialize
 * Returns:
 *	0:		Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_sw_init(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;

	adapter->rx_buffer_len = PCH_GBE_FRAME_SIZE_2048;
	hw->mac.max_frame_size = netdev->mtu + ETH_HLEN + ETH_FCS_LEN;
	hw->mac.min_frame_size = ETH_ZLEN + ETH_FCS_LEN;
	hw->phy.reset_delay_us = PCH_GBE_PHY_RESET_DELAY_US;

	if (pch_gbe_alloc_queues(adapter)) {
		netdev_err(netdev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}
	spin_lock_init(&adapter->hw.miim_lock);
	spin_lock_init(&adapter->stats_lock);
	spin_lock_init(&adapter->ethtool_lock);
	atomic_set(&adapter->irq_sem, 0);
	pch_gbe_irq_disable(adapter);

	pch_gbe_init_stats(adapter);

	netdev_dbg(netdev,
		   "rx_buffer_len : %d  mac.min_frame_size : %d  mac.max_frame_size : %d\n",
		   (u32) adapter->rx_buffer_len,
		   hw->mac.min_frame_size, hw->mac.max_frame_size);
	return 0;
}

/**
 * pch_gbe_open - Called when a network interface is made active
 * @netdev:	Network interface device structure
 * Returns:
 *	0:		Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_open(struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	int err;

	/* allocate transmit descriptors */
	err = pch_gbe_setup_tx_resources(adapter, adapter->tx_ring);
	if (err)
		goto err_setup_tx;
	/* allocate receive descriptors */
	err = pch_gbe_setup_rx_resources(adapter, adapter->rx_ring);
	if (err)
		goto err_setup_rx;
	pch_gbe_phy_power_up(hw);
	err = pch_gbe_up(adapter);
	if (err)
		goto err_up;
	netdev_dbg(netdev, "Success End\n");
	return 0;

err_up:
	if (!adapter->wake_up_evt)
		pch_gbe_phy_power_down(hw);
	pch_gbe_free_rx_resources(adapter, adapter->rx_ring);
err_setup_rx:
	pch_gbe_free_tx_resources(adapter, adapter->tx_ring);
err_setup_tx:
	pch_gbe_reset(adapter);
	netdev_err(netdev, "Error End\n");
	return err;
}

/**
 * pch_gbe_stop - Disables a network interface
 * @netdev:  Network interface device structure
 * Returns:
 *	0: Successfully
 */
static int pch_gbe_stop(struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;

	pch_gbe_down(adapter);
	if (!adapter->wake_up_evt)
		pch_gbe_phy_power_down(hw);
	pch_gbe_free_tx_resources(adapter, adapter->tx_ring);
	pch_gbe_free_rx_resources(adapter, adapter->rx_ring);
	return 0;
}

/**
 * pch_gbe_xmit_frame - Packet transmitting start
 * @skb:     Socket buffer structure
 * @netdev:  Network interface device structure
 * Returns:
 *	- NETDEV_TX_OK:   Normal end
 *	- NETDEV_TX_BUSY: Error end
 */
static netdev_tx_t pch_gbe_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_tx_ring *tx_ring = adapter->tx_ring;

	if (unlikely(!PCH_GBE_DESC_UNUSED(tx_ring))) {
		netif_stop_queue(netdev);
		netdev_dbg(netdev,
			   "Return : BUSY  next_to use : 0x%08x  next_to clean : 0x%08x\n",
			   tx_ring->next_to_use, tx_ring->next_to_clean);
		return NETDEV_TX_BUSY;
	}

	/* CRC,ITAG no support */
	pch_gbe_tx_queue(adapter, tx_ring, skb);
	return NETDEV_TX_OK;
}

/**
 * pch_gbe_set_multi - Multicast and Promiscuous mode set
 * @netdev:   Network interface device structure
 */
static void pch_gbe_set_multi(struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	struct netdev_hw_addr *ha;
	u32 rctl, adrmask;
	int mc_count, i;

	netdev_dbg(netdev, "netdev->flags : 0x%08x\n", netdev->flags);

	/* By default enable address & multicast filtering */
	rctl = ioread32(&hw->reg->RX_MODE);
	rctl |= PCH_GBE_ADD_FIL_EN | PCH_GBE_MLT_FIL_EN;

	/* Promiscuous mode disables all hardware address filtering */
	if (netdev->flags & IFF_PROMISC)
		rctl &= ~(PCH_GBE_ADD_FIL_EN | PCH_GBE_MLT_FIL_EN);

	/* If we want to monitor more multicast addresses than the hardware can
	 * support then disable hardware multicast filtering.
	 */
	mc_count = netdev_mc_count(netdev);
	if ((netdev->flags & IFF_ALLMULTI) || mc_count >= PCH_GBE_MAR_ENTRIES)
		rctl &= ~PCH_GBE_MLT_FIL_EN;

	iowrite32(rctl, &hw->reg->RX_MODE);

	/* If we're not using multicast filtering then there's no point
	 * configuring the unused MAC address registers.
	 */
	if (!(rctl & PCH_GBE_MLT_FIL_EN))
		return;

	/* Load the first set of multicast addresses into MAC address registers
	 * for use by hardware filtering.
	 */
	i = 1;
	netdev_for_each_mc_addr(ha, netdev)
		pch_gbe_mac_mar_set(hw, ha->addr, i++);

	/* If there are spare MAC registers, mask & clear them */
	for (; i < PCH_GBE_MAR_ENTRIES; i++) {
		/* Clear MAC address mask */
		adrmask = ioread32(&hw->reg->ADDR_MASK);
		iowrite32(adrmask | BIT(i), &hw->reg->ADDR_MASK);
		/* wait busy */
		pch_gbe_wait_clr_bit(&hw->reg->ADDR_MASK, PCH_GBE_BUSY);
		/* Clear MAC address */
		iowrite32(0, &hw->reg->mac_adr[i].high);
		iowrite32(0, &hw->reg->mac_adr[i].low);
	}

	netdev_dbg(netdev,
		 "RX_MODE reg(check bit31,30 ADD,MLT) : 0x%08x  netdev->mc_count : 0x%08x\n",
		 ioread32(&hw->reg->RX_MODE), mc_count);
}

/**
 * pch_gbe_set_mac - Change the Ethernet Address of the NIC
 * @netdev: Network interface device structure
 * @addr:   Pointer to an address structure
 * Returns:
 *	0:		Successfully
 *	-EADDRNOTAVAIL:	Failed
 */
static int pch_gbe_set_mac(struct net_device *netdev, void *addr)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *skaddr = addr;
	int ret_val;

	if (!is_valid_ether_addr(skaddr->sa_data)) {
		ret_val = -EADDRNOTAVAIL;
	} else {
		eth_hw_addr_set(netdev, skaddr->sa_data);
		memcpy(adapter->hw.mac.addr, skaddr->sa_data, netdev->addr_len);
		pch_gbe_mac_mar_set(&adapter->hw, adapter->hw.mac.addr, 0);
		ret_val = 0;
	}
	netdev_dbg(netdev, "ret_val : 0x%08x\n", ret_val);
	netdev_dbg(netdev, "dev_addr : %pM\n", netdev->dev_addr);
	netdev_dbg(netdev, "mac_addr : %pM\n", adapter->hw.mac.addr);
	netdev_dbg(netdev, "MAC_ADR1AB reg : 0x%08x 0x%08x\n",
		   ioread32(&adapter->hw.reg->mac_adr[0].high),
		   ioread32(&adapter->hw.reg->mac_adr[0].low));
	return ret_val;
}

/**
 * pch_gbe_change_mtu - Change the Maximum Transfer Unit
 * @netdev:   Network interface device structure
 * @new_mtu:  New value for maximum frame size
 * Returns:
 *	0:		Successfully
 *	-EINVAL:	Failed
 */
static int pch_gbe_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN;
	unsigned long old_rx_buffer_len = adapter->rx_buffer_len;
	int err;

	if (max_frame <= PCH_GBE_FRAME_SIZE_2048)
		adapter->rx_buffer_len = PCH_GBE_FRAME_SIZE_2048;
	else if (max_frame <= PCH_GBE_FRAME_SIZE_4096)
		adapter->rx_buffer_len = PCH_GBE_FRAME_SIZE_4096;
	else if (max_frame <= PCH_GBE_FRAME_SIZE_8192)
		adapter->rx_buffer_len = PCH_GBE_FRAME_SIZE_8192;
	else
		adapter->rx_buffer_len = PCH_GBE_MAX_RX_BUFFER_SIZE;

	if (netif_running(netdev)) {
		pch_gbe_down(adapter);
		err = pch_gbe_up(adapter);
		if (err) {
			adapter->rx_buffer_len = old_rx_buffer_len;
			pch_gbe_up(adapter);
			return err;
		} else {
			netdev->mtu = new_mtu;
			adapter->hw.mac.max_frame_size = max_frame;
		}
	} else {
		pch_gbe_reset(adapter);
		netdev->mtu = new_mtu;
		adapter->hw.mac.max_frame_size = max_frame;
	}

	netdev_dbg(netdev,
		   "max_frame : %d  rx_buffer_len : %d  mtu : %d  max_frame_size : %d\n",
		   max_frame, (u32) adapter->rx_buffer_len, netdev->mtu,
		   adapter->hw.mac.max_frame_size);
	return 0;
}

/**
 * pch_gbe_set_features - Reset device after features changed
 * @netdev:   Network interface device structure
 * @features:  New features
 * Returns:
 *	0:		HW state updated successfully
 */
static int pch_gbe_set_features(struct net_device *netdev,
	netdev_features_t features)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	netdev_features_t changed = features ^ netdev->features;

	if (!(changed & NETIF_F_RXCSUM))
		return 0;

	if (netif_running(netdev))
		pch_gbe_reinit_locked(adapter);
	else
		pch_gbe_reset(adapter);

	return 0;
}

/**
 * pch_gbe_ioctl - Controls register through a MII interface
 * @netdev:   Network interface device structure
 * @ifr:      Pointer to ifr structure
 * @cmd:      Control command
 * Returns:
 *	0:	Successfully
 *	Negative value:	Failed
 */
static int pch_gbe_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	netdev_dbg(netdev, "cmd : 0x%04x\n", cmd);

	if (cmd == SIOCSHWTSTAMP)
		return hwtstamp_ioctl(netdev, ifr, cmd);

	return generic_mii_ioctl(&adapter->mii, if_mii(ifr), cmd, NULL);
}

/**
 * pch_gbe_tx_timeout - Respond to a Tx Hang
 * @netdev:   Network interface device structure
 * @txqueue: index of hanging queue
 */
static void pch_gbe_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	/* Do the reset outside of interrupt context */
	adapter->stats.tx_timeout_count++;
	schedule_work(&adapter->reset_task);
}

/**
 * pch_gbe_napi_poll - NAPI receive and transfer polling callback
 * @napi:    Pointer of polling device struct
 * @budget:  The maximum number of a packet
 * Returns:
 *	false:  Exit the polling mode
 *	true:   Continue the polling mode
 */
static int pch_gbe_napi_poll(struct napi_struct *napi, int budget)
{
	struct pch_gbe_adapter *adapter =
	    container_of(napi, struct pch_gbe_adapter, napi);
	int work_done = 0;
	bool poll_end_flag = false;
	bool cleaned = false;

	netdev_dbg(adapter->netdev, "budget : %d\n", budget);

	pch_gbe_clean_rx(adapter, adapter->rx_ring, &work_done, budget);
	cleaned = pch_gbe_clean_tx(adapter, adapter->tx_ring);

	if (cleaned)
		work_done = budget;
	/* If no Tx and not enough Rx work done,
	 * exit the polling mode
	 */
	if (work_done < budget)
		poll_end_flag = true;

	if (poll_end_flag) {
		napi_complete_done(napi, work_done);
		pch_gbe_irq_enable(adapter);
	}

	if (adapter->rx_stop_flag) {
		adapter->rx_stop_flag = false;
		pch_gbe_enable_dma_rx(&adapter->hw);
	}

	netdev_dbg(adapter->netdev,
		   "poll_end_flag : %d  work_done : %d  budget : %d\n",
		   poll_end_flag, work_done, budget);

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * pch_gbe_netpoll - Used by things like netconsole to send skbs
 * @netdev:  Network interface device structure
 */
static void pch_gbe_netpoll(struct net_device *netdev)
{
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	disable_irq(adapter->irq);
	pch_gbe_intr(adapter->irq, netdev);
	enable_irq(adapter->irq);
}
#endif

static const struct net_device_ops pch_gbe_netdev_ops = {
	.ndo_open = pch_gbe_open,
	.ndo_stop = pch_gbe_stop,
	.ndo_start_xmit = pch_gbe_xmit_frame,
	.ndo_set_mac_address = pch_gbe_set_mac,
	.ndo_tx_timeout = pch_gbe_tx_timeout,
	.ndo_change_mtu = pch_gbe_change_mtu,
	.ndo_set_features = pch_gbe_set_features,
	.ndo_eth_ioctl = pch_gbe_ioctl,
	.ndo_set_rx_mode = pch_gbe_set_multi,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = pch_gbe_netpoll,
#endif
};

static pci_ers_result_t pch_gbe_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	netif_device_detach(netdev);
	if (netif_running(netdev))
		pch_gbe_down(adapter);
	pci_disable_device(pdev);
	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t pch_gbe_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;

	if (pci_enable_device(pdev)) {
		netdev_err(netdev, "Cannot re-enable PCI device after reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);
	pci_enable_wake(pdev, PCI_D0, 0);
	pch_gbe_phy_power_up(hw);
	pch_gbe_reset(adapter);
	/* Clear wake up status */
	pch_gbe_mac_set_wol_event(hw, 0);

	return PCI_ERS_RESULT_RECOVERED;
}

static void pch_gbe_io_resume(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev)) {
		if (pch_gbe_up(adapter)) {
			netdev_dbg(netdev,
				   "can't bring device back up after reset\n");
			return;
		}
	}
	netif_device_attach(netdev);
}

static int __pch_gbe_suspend(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 wufc = adapter->wake_up_evt;

	netif_device_detach(netdev);
	if (netif_running(netdev))
		pch_gbe_down(adapter);
	if (wufc) {
		pch_gbe_set_multi(netdev);
		pch_gbe_setup_rctl(adapter);
		pch_gbe_configure_rx(adapter);
		pch_gbe_set_rgmii_ctrl(adapter, hw->mac.link_speed,
					hw->mac.link_duplex);
		pch_gbe_set_mode(adapter, hw->mac.link_speed,
					hw->mac.link_duplex);
		pch_gbe_mac_set_wol_event(hw, wufc);
		pci_disable_device(pdev);
	} else {
		pch_gbe_phy_power_down(hw);
		pch_gbe_mac_set_wol_event(hw, wufc);
		pci_disable_device(pdev);
	}
	return 0;
}

#ifdef CONFIG_PM
static int pch_gbe_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);

	return __pch_gbe_suspend(pdev);
}

static int pch_gbe_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);
	struct pch_gbe_hw *hw = &adapter->hw;
	u32 err;

	err = pci_enable_device(pdev);
	if (err) {
		netdev_err(netdev, "Cannot enable PCI device from suspend\n");
		return err;
	}
	pci_set_master(pdev);
	pch_gbe_phy_power_up(hw);
	pch_gbe_reset(adapter);
	/* Clear wake on lan control and status */
	pch_gbe_mac_set_wol_event(hw, 0);

	if (netif_running(netdev))
		pch_gbe_up(adapter);
	netif_device_attach(netdev);

	return 0;
}
#endif /* CONFIG_PM */

static void pch_gbe_shutdown(struct pci_dev *pdev)
{
	__pch_gbe_suspend(pdev);
	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, true);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

static void pch_gbe_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pch_gbe_adapter *adapter = netdev_priv(netdev);

	cancel_work_sync(&adapter->reset_task);
	unregister_netdev(netdev);

	pch_gbe_phy_hw_reset(&adapter->hw);

	free_netdev(netdev);
}

static int pch_gbe_probe(struct pci_dev *pdev,
			  const struct pci_device_id *pci_id)
{
	struct net_device *netdev;
	struct pch_gbe_adapter *adapter;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64))) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(&pdev->dev, "ERR: No usable DMA configuration, aborting\n");
			return ret;
		}
	}

	ret = pcim_iomap_regions(pdev, 1 << PCH_GBE_PCI_BAR, pci_name(pdev));
	if (ret) {
		dev_err(&pdev->dev,
			"ERR: Can't reserve PCI I/O and memory resources\n");
		return ret;
	}
	pci_set_master(pdev);

	netdev = alloc_etherdev((int)sizeof(struct pch_gbe_adapter));
	if (!netdev)
		return -ENOMEM;
	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->hw.back = adapter;
	adapter->hw.reg = pcim_iomap_table(pdev)[PCH_GBE_PCI_BAR];

	adapter->pdata = (struct pch_gbe_privdata *)pci_id->driver_data;
	if (adapter->pdata && adapter->pdata->platform_init) {
		ret = adapter->pdata->platform_init(pdev);
		if (ret)
			goto err_free_netdev;
	}

	adapter->ptp_pdev =
		pci_get_domain_bus_and_slot(pci_domain_nr(adapter->pdev->bus),
					    adapter->pdev->bus->number,
					    PCI_DEVFN(12, 4));

	netdev->netdev_ops = &pch_gbe_netdev_ops;
	netdev->watchdog_timeo = PCH_GBE_WATCHDOG_PERIOD;
	netif_napi_add(netdev, &adapter->napi, pch_gbe_napi_poll);
	netdev->hw_features = NETIF_F_RXCSUM |
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	netdev->features = netdev->hw_features;
	pch_gbe_set_ethtool_ops(netdev);

	/* MTU range: 46 - 10300 */
	netdev->min_mtu = ETH_ZLEN - ETH_HLEN;
	netdev->max_mtu = PCH_GBE_MAX_JUMBO_FRAME_SIZE -
			  (ETH_HLEN + ETH_FCS_LEN);

	pch_gbe_mac_load_mac_addr(&adapter->hw);
	pch_gbe_mac_reset_hw(&adapter->hw);

	/* setup the private structure */
	ret = pch_gbe_sw_init(adapter);
	if (ret)
		goto err_free_netdev;

	/* Initialize PHY */
	ret = pch_gbe_init_phy(adapter);
	if (ret) {
		dev_err(&pdev->dev, "PHY initialize error\n");
		goto err_free_adapter;
	}

	/* Read the MAC address. and store to the private data */
	ret = pch_gbe_mac_read_mac_addr(&adapter->hw);
	if (ret) {
		dev_err(&pdev->dev, "MAC address Read Error\n");
		goto err_free_adapter;
	}

	eth_hw_addr_set(netdev, adapter->hw.mac.addr);
	if (!is_valid_ether_addr(netdev->dev_addr)) {
		/*
		 * If the MAC is invalid (or just missing), display a warning
		 * but do not abort setting up the device. pch_gbe_up will
		 * prevent the interface from being brought up until a valid MAC
		 * is set.
		 */
		dev_err(&pdev->dev, "Invalid MAC address, "
		                    "interface disabled.\n");
	}
	timer_setup(&adapter->watchdog_timer, pch_gbe_watchdog, 0);

	INIT_WORK(&adapter->reset_task, pch_gbe_reset_task);

	pch_gbe_check_options(adapter);

	/* initialize the wol settings based on the eeprom settings */
	adapter->wake_up_evt = PCH_GBE_WL_INIT_SETTING;
	dev_info(&pdev->dev, "MAC address : %pM\n", netdev->dev_addr);

	/* reset the hardware with the new settings */
	pch_gbe_reset(adapter);

	ret = register_netdev(netdev);
	if (ret)
		goto err_free_adapter;
	/* tell the stack to leave us alone until pch_gbe_open() is called */
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	dev_dbg(&pdev->dev, "PCH Network Connection\n");

	/* Disable hibernation on certain platforms */
	if (adapter->pdata && adapter->pdata->phy_disable_hibernate)
		pch_gbe_phy_disable_hibernate(&adapter->hw);

	device_set_wakeup_enable(&pdev->dev, 1);
	return 0;

err_free_adapter:
	pch_gbe_phy_hw_reset(&adapter->hw);
err_free_netdev:
	free_netdev(netdev);
	return ret;
}

static void pch_gbe_gpio_remove_table(void *table)
{
	gpiod_remove_lookup_table(table);
}

static int pch_gbe_gpio_add_table(struct device *dev, void *table)
{
	gpiod_add_lookup_table(table);
	return devm_add_action_or_reset(dev, pch_gbe_gpio_remove_table, table);
}

static struct gpiod_lookup_table pch_gbe_minnow_gpio_table = {
	.dev_id		= "0000:02:00.1",
	.table		= {
		GPIO_LOOKUP("sch_gpio.33158", 13, NULL, GPIO_ACTIVE_LOW),
		{}
	},
};

/* The AR803X PHY on the MinnowBoard requires a physical pin to be toggled to
 * ensure it is awake for probe and init. Request the line and reset the PHY.
 */
static int pch_gbe_minnow_platform_init(struct pci_dev *pdev)
{
	struct gpio_desc *gpiod;
	int ret;

	ret = pch_gbe_gpio_add_table(&pdev->dev, &pch_gbe_minnow_gpio_table);
	if (ret)
		return ret;

	gpiod = devm_gpiod_get(&pdev->dev, NULL, GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod))
		return dev_err_probe(&pdev->dev, PTR_ERR(gpiod),
				     "Can't request PHY reset GPIO line\n");

	gpiod_set_value(gpiod, 1);
	usleep_range(1250, 1500);
	gpiod_set_value(gpiod, 0);
	usleep_range(1250, 1500);

	return ret;
}

static struct pch_gbe_privdata pch_gbe_minnow_privdata = {
	.phy_tx_clk_delay = true,
	.phy_disable_hibernate = true,
	.platform_init = pch_gbe_minnow_platform_init,
};

static const struct pci_device_id pch_gbe_pcidev_id[] = {
	{.vendor = PCI_VENDOR_ID_INTEL,
	 .device = PCI_DEVICE_ID_INTEL_IOH1_GBE,
	 .subvendor = PCI_VENDOR_ID_CIRCUITCO,
	 .subdevice = PCI_SUBSYSTEM_ID_CIRCUITCO_MINNOWBOARD,
	 .class = (PCI_CLASS_NETWORK_ETHERNET << 8),
	 .class_mask = (0xFFFF00),
	 .driver_data = (kernel_ulong_t)&pch_gbe_minnow_privdata
	 },
	{.vendor = PCI_VENDOR_ID_INTEL,
	 .device = PCI_DEVICE_ID_INTEL_IOH1_GBE,
	 .subvendor = PCI_ANY_ID,
	 .subdevice = PCI_ANY_ID,
	 .class = (PCI_CLASS_NETWORK_ETHERNET << 8),
	 .class_mask = (0xFFFF00)
	 },
	{.vendor = PCI_VENDOR_ID_ROHM,
	 .device = PCI_DEVICE_ID_ROHM_ML7223_GBE,
	 .subvendor = PCI_ANY_ID,
	 .subdevice = PCI_ANY_ID,
	 .class = (PCI_CLASS_NETWORK_ETHERNET << 8),
	 .class_mask = (0xFFFF00)
	 },
	{.vendor = PCI_VENDOR_ID_ROHM,
	 .device = PCI_DEVICE_ID_ROHM_ML7831_GBE,
	 .subvendor = PCI_ANY_ID,
	 .subdevice = PCI_ANY_ID,
	 .class = (PCI_CLASS_NETWORK_ETHERNET << 8),
	 .class_mask = (0xFFFF00)
	 },
	/* required last entry */
	{0}
};

#ifdef CONFIG_PM
static const struct dev_pm_ops pch_gbe_pm_ops = {
	.suspend = pch_gbe_suspend,
	.resume = pch_gbe_resume,
	.freeze = pch_gbe_suspend,
	.thaw = pch_gbe_resume,
	.poweroff = pch_gbe_suspend,
	.restore = pch_gbe_resume,
};
#endif

static const struct pci_error_handlers pch_gbe_err_handler = {
	.error_detected = pch_gbe_io_error_detected,
	.slot_reset = pch_gbe_io_slot_reset,
	.resume = pch_gbe_io_resume
};

static struct pci_driver pch_gbe_driver = {
	.name = KBUILD_MODNAME,
	.id_table = pch_gbe_pcidev_id,
	.probe = pch_gbe_probe,
	.remove = pch_gbe_remove,
#ifdef CONFIG_PM
	.driver.pm = &pch_gbe_pm_ops,
#endif
	.shutdown = pch_gbe_shutdown,
	.err_handler = &pch_gbe_err_handler
};
module_pci_driver(pch_gbe_driver);

MODULE_DESCRIPTION("EG20T PCH Gigabit ethernet Driver");
MODULE_AUTHOR("LAPIS SEMICONDUCTOR, <tshimizu818@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, pch_gbe_pcidev_id);

/* pch_gbe_main.c */
