/*
 * This is the driver for the GMAC on-chip Ethernet controller for ST SoCs.
 * DWC Ether MAC version 4.00  has been used for developing this code.
 *
 * This only implements the mac core functions for this chip.
 *
 * Copyright (C) 2015  STMicroelectronics Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
 */

#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/ethtool.h>
#include <linux/io.h>
#include "dwmac4.h"

static void dwmac4_core_init(struct mac_device_info *hw, int mtu)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + GMAC_CONFIG);

	value |= GMAC_CORE_INIT;

	if (mtu > 1500)
		value |= GMAC_CONFIG_2K;
	if (mtu > 2000)
		value |= GMAC_CONFIG_JE;

	writel(value, ioaddr + GMAC_CONFIG);

	/* Mask GMAC interrupts */
	writel(GMAC_INT_PMT_EN, ioaddr + GMAC_INT_EN);
}

static void dwmac4_dump_regs(struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	int i;

	pr_debug("\tDWMAC4 regs (base addr = 0x%p)\n", ioaddr);

	for (i = 0; i < GMAC_REG_NUM; i++) {
		int offset = i * 4;

		pr_debug("\tReg No. %d (offset 0x%x): 0x%08x\n", i,
			 offset, readl(ioaddr + offset));
	}
}

static int dwmac4_rx_ipc_enable(struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + GMAC_CONFIG);

	if (hw->rx_csum)
		value |= GMAC_CONFIG_IPC;
	else
		value &= ~GMAC_CONFIG_IPC;

	writel(value, ioaddr + GMAC_CONFIG);

	value = readl(ioaddr + GMAC_CONFIG);

	return !!(value & GMAC_CONFIG_IPC);
}

static void dwmac4_pmt(struct mac_device_info *hw, unsigned long mode)
{
	void __iomem *ioaddr = hw->pcsr;
	unsigned int pmt = 0;

	if (mode & WAKE_MAGIC) {
		pr_debug("GMAC: WOL Magic frame\n");
		pmt |= power_down | magic_pkt_en;
	}
	if (mode & WAKE_UCAST) {
		pr_debug("GMAC: WOL on global unicast\n");
		pmt |= global_unicast;
	}

	writel(pmt, ioaddr + GMAC_PMT);
}

static void dwmac4_set_umac_addr(struct mac_device_info *hw,
				 unsigned char *addr, unsigned int reg_n)
{
	void __iomem *ioaddr = hw->pcsr;

	stmmac_dwmac4_set_mac_addr(ioaddr, addr, GMAC_ADDR_HIGH(reg_n),
				   GMAC_ADDR_LOW(reg_n));
}

static void dwmac4_get_umac_addr(struct mac_device_info *hw,
				 unsigned char *addr, unsigned int reg_n)
{
	void __iomem *ioaddr = hw->pcsr;

	stmmac_dwmac4_get_mac_addr(ioaddr, addr, GMAC_ADDR_HIGH(reg_n),
				   GMAC_ADDR_LOW(reg_n));
}

static void dwmac4_set_filter(struct mac_device_info *hw,
			      struct net_device *dev)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	unsigned int value = 0;

	if (dev->flags & IFF_PROMISC) {
		value = GMAC_PACKET_FILTER_PR;
	} else if ((dev->flags & IFF_ALLMULTI) ||
			(netdev_mc_count(dev) > HASH_TABLE_SIZE)) {
		/* Pass all multi */
		value = GMAC_PACKET_FILTER_PM;
		/* Set the 64 bits of the HASH tab. To be updated if taller
		 * hash table is used
		 */
		writel(0xffffffff, ioaddr + GMAC_HASH_TAB_0_31);
		writel(0xffffffff, ioaddr + GMAC_HASH_TAB_32_63);
	} else if (!netdev_mc_empty(dev)) {
		u32 mc_filter[2];
		struct netdev_hw_addr *ha;

		/* Hash filter for multicast */
		value = GMAC_PACKET_FILTER_HMC;

		memset(mc_filter, 0, sizeof(mc_filter));
		netdev_for_each_mc_addr(ha, dev) {
			/* The upper 6 bits of the calculated CRC are used to
			 * index the content of the Hash Table Reg 0 and 1.
			 */
			int bit_nr =
				(bitrev32(~crc32_le(~0, ha->addr, 6)) >> 26);
			/* The most significant bit determines the register
			 * to use while the other 5 bits determines the bit
			 * within the selected register
			 */
			mc_filter[bit_nr >> 5] |= (1 << (bit_nr & 0x1F));
		}
		writel(mc_filter[0], ioaddr + GMAC_HASH_TAB_0_31);
		writel(mc_filter[1], ioaddr + GMAC_HASH_TAB_32_63);
	}

	/* Handle multiple unicast addresses */
	if (netdev_uc_count(dev) > GMAC_MAX_PERFECT_ADDRESSES) {
		/* Switch to promiscuous mode if more than 128 addrs
		 * are required
		 */
		value |= GMAC_PACKET_FILTER_PR;
	} else if (!netdev_uc_empty(dev)) {
		int reg = 1;
		struct netdev_hw_addr *ha;

		netdev_for_each_uc_addr(ha, dev) {
			dwmac4_set_umac_addr(ioaddr, ha->addr, reg);
			reg++;
		}
	}

	writel(value, ioaddr + GMAC_PACKET_FILTER);
}

static void dwmac4_flow_ctrl(struct mac_device_info *hw, unsigned int duplex,
			     unsigned int fc, unsigned int pause_time)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 channel = STMMAC_CHAN0;	/* FIXME */
	unsigned int flow = 0;

	pr_debug("GMAC Flow-Control:\n");
	if (fc & FLOW_RX) {
		pr_debug("\tReceive Flow-Control ON\n");
		flow |= GMAC_RX_FLOW_CTRL_RFE;
		writel(flow, ioaddr + GMAC_RX_FLOW_CTRL);
	}
	if (fc & FLOW_TX) {
		pr_debug("\tTransmit Flow-Control ON\n");
		flow |= GMAC_TX_FLOW_CTRL_TFE;
		writel(flow, ioaddr + GMAC_QX_TX_FLOW_CTRL(channel));

		if (duplex) {
			pr_debug("\tduplex mode: PAUSE %d\n", pause_time);
			flow |= (pause_time << GMAC_TX_FLOW_CTRL_PT_SHIFT);
			writel(flow, ioaddr + GMAC_QX_TX_FLOW_CTRL(channel));
		}
	}
}

static void dwmac4_ctrl_ane(struct mac_device_info *hw, bool restart)
{
	void __iomem *ioaddr = hw->pcsr;

	/* auto negotiation enable and External Loopback enable */
	u32 value = GMAC_AN_CTRL_ANE | GMAC_AN_CTRL_ELE;

	if (restart)
		value |= GMAC_AN_CTRL_RAN;

	writel(value, ioaddr + GMAC_AN_CTRL);
}

static void dwmac4_get_adv(struct mac_device_info *hw, struct rgmii_adv *adv)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + GMAC_AN_ADV);

	if (value & GMAC_AN_FD)
		adv->duplex = DUPLEX_FULL;
	if (value & GMAC_AN_HD)
		adv->duplex |= DUPLEX_HALF;

	adv->pause = (value & GMAC_AN_PSE_MASK) >> GMAC_AN_PSE_SHIFT;

	value = readl(ioaddr + GMAC_AN_LPA);

	if (value & GMAC_AN_FD)
		adv->lp_duplex = DUPLEX_FULL;
	if (value & GMAC_AN_HD)
		adv->lp_duplex = DUPLEX_HALF;

	adv->lp_pause = (value & GMAC_AN_PSE_MASK) >> GMAC_AN_PSE_SHIFT;
}

static int dwmac4_irq_status(struct mac_device_info *hw,
			     struct stmmac_extra_stats *x)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 mtl_int_qx_status;
	u32 intr_status;
	int ret = 0;

	intr_status = readl(ioaddr + GMAC_INT_STATUS);

	/* Not used events (e.g. MMC interrupts) are not handled. */
	if ((intr_status & mmc_tx_irq))
		x->mmc_tx_irq_n++;
	if (unlikely(intr_status & mmc_rx_irq))
		x->mmc_rx_irq_n++;
	if (unlikely(intr_status & mmc_rx_csum_offload_irq))
		x->mmc_rx_csum_offload_irq_n++;
	/* Clear the PMT bits 5 and 6 by reading the PMT status reg */
	if (unlikely(intr_status & pmt_irq)) {
		readl(ioaddr + GMAC_PMT);
		x->irq_receive_pmt_irq_n++;
	}

	if ((intr_status & pcs_ane_irq) || (intr_status & pcs_link_irq)) {
		readl(ioaddr + GMAC_AN_STATUS);
		x->irq_pcs_ane_n++;
	}

	mtl_int_qx_status = readl(ioaddr + MTL_INT_STATUS);
	/* Check MTL Interrupt: Currently only one queue is used: Q0. */
	if (mtl_int_qx_status & MTL_INT_Q0) {
		/* read Queue 0 Interrupt status */
		u32 status = readl(ioaddr + MTL_CHAN_INT_CTRL(STMMAC_CHAN0));

		if (status & MTL_RX_OVERFLOW_INT) {
			/*  clear Interrupt */
			writel(status | MTL_RX_OVERFLOW_INT,
			       ioaddr + MTL_CHAN_INT_CTRL(STMMAC_CHAN0));
			ret = CORE_IRQ_MTL_RX_OVERFLOW;
		}
	}

	return ret;
}

static void dwmac4_debug(void __iomem *ioaddr, struct stmmac_extra_stats *x)
{
	u32 value;

	/*  Currently only channel 0 is supported */
	value = readl(ioaddr + MTL_CHAN_TX_DEBUG(STMMAC_CHAN0));

	if (value & MTL_DEBUG_TXSTSFSTS)
		x->mtl_tx_status_fifo_full++;
	if (value & MTL_DEBUG_TXFSTS)
		x->mtl_tx_fifo_not_empty++;
	if (value & MTL_DEBUG_TWCSTS)
		x->mmtl_fifo_ctrl++;
	if (value & MTL_DEBUG_TRCSTS_MASK) {
		u32 trcsts = (value & MTL_DEBUG_TRCSTS_MASK)
			     >> MTL_DEBUG_TRCSTS_SHIFT;
		if (trcsts == MTL_DEBUG_TRCSTS_WRITE)
			x->mtl_tx_fifo_read_ctrl_write++;
		else if (trcsts == MTL_DEBUG_TRCSTS_TXW)
			x->mtl_tx_fifo_read_ctrl_wait++;
		else if (trcsts == MTL_DEBUG_TRCSTS_READ)
			x->mtl_tx_fifo_read_ctrl_read++;
		else
			x->mtl_tx_fifo_read_ctrl_idle++;
	}
	if (value & MTL_DEBUG_TXPAUSED)
		x->mac_tx_in_pause++;

	value = readl(ioaddr + MTL_CHAN_RX_DEBUG(STMMAC_CHAN0));

	if (value & MTL_DEBUG_RXFSTS_MASK) {
		u32 rxfsts = (value & MTL_DEBUG_RXFSTS_MASK)
			     >> MTL_DEBUG_RRCSTS_SHIFT;

		if (rxfsts == MTL_DEBUG_RXFSTS_FULL)
			x->mtl_rx_fifo_fill_level_full++;
		else if (rxfsts == MTL_DEBUG_RXFSTS_AT)
			x->mtl_rx_fifo_fill_above_thresh++;
		else if (rxfsts == MTL_DEBUG_RXFSTS_BT)
			x->mtl_rx_fifo_fill_below_thresh++;
		else
			x->mtl_rx_fifo_fill_level_empty++;
	}
	if (value & MTL_DEBUG_RRCSTS_MASK) {
		u32 rrcsts = (value & MTL_DEBUG_RRCSTS_MASK) >>
			     MTL_DEBUG_RRCSTS_SHIFT;

		if (rrcsts == MTL_DEBUG_RRCSTS_FLUSH)
			x->mtl_rx_fifo_read_ctrl_flush++;
		else if (rrcsts == MTL_DEBUG_RRCSTS_RSTAT)
			x->mtl_rx_fifo_read_ctrl_read_data++;
		else if (rrcsts == MTL_DEBUG_RRCSTS_RDATA)
			x->mtl_rx_fifo_read_ctrl_status++;
		else
			x->mtl_rx_fifo_read_ctrl_idle++;
	}
	if (value & MTL_DEBUG_RWCSTS)
		x->mtl_rx_fifo_ctrl_active++;

	/* GMAC debug */
	value = readl(ioaddr + GMAC_DEBUG);

	if (value & GMAC_DEBUG_TFCSTS_MASK) {
		u32 tfcsts = (value & GMAC_DEBUG_TFCSTS_MASK)
			      >> GMAC_DEBUG_TFCSTS_SHIFT;

		if (tfcsts == GMAC_DEBUG_TFCSTS_XFER)
			x->mac_tx_frame_ctrl_xfer++;
		else if (tfcsts == GMAC_DEBUG_TFCSTS_GEN_PAUSE)
			x->mac_tx_frame_ctrl_pause++;
		else if (tfcsts == GMAC_DEBUG_TFCSTS_WAIT)
			x->mac_tx_frame_ctrl_wait++;
		else
			x->mac_tx_frame_ctrl_idle++;
	}
	if (value & GMAC_DEBUG_TPESTS)
		x->mac_gmii_tx_proto_engine++;
	if (value & GMAC_DEBUG_RFCFCSTS_MASK)
		x->mac_rx_frame_ctrl_fifo = (value & GMAC_DEBUG_RFCFCSTS_MASK)
					    >> GMAC_DEBUG_RFCFCSTS_SHIFT;
	if (value & GMAC_DEBUG_RPESTS)
		x->mac_gmii_rx_proto_engine++;
}

static const struct stmmac_ops dwmac4_ops = {
	.core_init = dwmac4_core_init,
	.rx_ipc = dwmac4_rx_ipc_enable,
	.dump_regs = dwmac4_dump_regs,
	.host_irq_status = dwmac4_irq_status,
	.flow_ctrl = dwmac4_flow_ctrl,
	.pmt = dwmac4_pmt,
	.set_umac_addr = dwmac4_set_umac_addr,
	.get_umac_addr = dwmac4_get_umac_addr,
	.ctrl_ane = dwmac4_ctrl_ane,
	.get_adv = dwmac4_get_adv,
	.debug = dwmac4_debug,
	.set_filter = dwmac4_set_filter,
};

struct mac_device_info *dwmac4_setup(void __iomem *ioaddr, int mcbins,
				     int perfect_uc_entries, int *synopsys_id)
{
	struct mac_device_info *mac;
	u32 hwid = readl(ioaddr + GMAC_VERSION);

	mac = kzalloc(sizeof(const struct mac_device_info), GFP_KERNEL);
	if (!mac)
		return NULL;

	mac->pcsr = ioaddr;
	mac->multicast_filter_bins = mcbins;
	mac->unicast_filter_entries = perfect_uc_entries;
	mac->mcast_bits_log2 = 0;

	if (mac->multicast_filter_bins)
		mac->mcast_bits_log2 = ilog2(mac->multicast_filter_bins);

	mac->mac = &dwmac4_ops;

	mac->link.port = GMAC_CONFIG_PS;
	mac->link.duplex = GMAC_CONFIG_DM;
	mac->link.speed = GMAC_CONFIG_FES;
	mac->mii.addr = GMAC_MDIO_ADDR;
	mac->mii.data = GMAC_MDIO_DATA;

	/* Get and dump the chip ID */
	*synopsys_id = stmmac_get_synopsys_id(hwid);

	if (*synopsys_id > DWMAC_CORE_4_00)
		mac->dma = &dwmac410_dma_ops;
	else
		mac->dma = &dwmac4_dma_ops;

	return mac;
}
