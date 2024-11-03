// SPDX-License-Identifier: GPL-2.0-only
/*
 * This is the driver for the GMAC on-chip Ethernet controller for ST SoCs.
 * DWC Ether MAC version 4.00  has been used for developing this code.
 *
 * This only implements the mac core functions for this chip.
 *
 * Copyright (C) 2015  STMicroelectronics Ltd
 *
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
 */

#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/ethtool.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include "stmmac.h"
#include "stmmac_fpe.h"
#include "stmmac_pcs.h"
#include "dwmac4.h"
#include "dwmac5.h"

static void dwmac4_core_init(struct mac_device_info *hw,
			     struct net_device *dev)
{
	struct stmmac_priv *priv = netdev_priv(dev);
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + GMAC_CONFIG);
	u32 clk_rate;

	value |= GMAC_CORE_INIT;

	if (hw->ps) {
		value |= GMAC_CONFIG_TE;

		value &= hw->link.speed_mask;
		switch (hw->ps) {
		case SPEED_1000:
			value |= hw->link.speed1000;
			break;
		case SPEED_100:
			value |= hw->link.speed100;
			break;
		case SPEED_10:
			value |= hw->link.speed10;
			break;
		}
	}

	writel(value, ioaddr + GMAC_CONFIG);

	/* Configure LPI 1us counter to number of CSR clock ticks in 1us - 1 */
	clk_rate = clk_get_rate(priv->plat->stmmac_clk);
	writel((clk_rate / 1000000) - 1, ioaddr + GMAC4_MAC_ONEUS_TIC_COUNTER);

	/* Enable GMAC interrupts */
	value = GMAC_INT_DEFAULT_ENABLE;

	if (hw->pcs)
		value |= GMAC_PCS_IRQ_DEFAULT;

	writel(value, ioaddr + GMAC_INT_EN);

	if (GMAC_INT_DEFAULT_ENABLE & GMAC_INT_TSIE)
		init_waitqueue_head(&priv->tstamp_busy_wait);
}

static void dwmac4_update_caps(struct stmmac_priv *priv)
{
	if (priv->plat->tx_queues_to_use > 1)
		priv->hw->link.caps &= ~(MAC_10HD | MAC_100HD | MAC_1000HD);
	else
		priv->hw->link.caps |= (MAC_10HD | MAC_100HD | MAC_1000HD);
}

static void dwmac4_rx_queue_enable(struct mac_device_info *hw,
				   u8 mode, u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + GMAC_RXQ_CTRL0);

	value &= GMAC_RX_QUEUE_CLEAR(queue);
	if (mode == MTL_QUEUE_AVB)
		value |= GMAC_RX_AV_QUEUE_ENABLE(queue);
	else if (mode == MTL_QUEUE_DCB)
		value |= GMAC_RX_DCB_QUEUE_ENABLE(queue);

	writel(value, ioaddr + GMAC_RXQ_CTRL0);
}

static void dwmac4_rx_queue_priority(struct mac_device_info *hw,
				     u32 prio, u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 clear_mask = 0;
	u32 ctrl2, ctrl3;
	int i;

	ctrl2 = readl(ioaddr + GMAC_RXQ_CTRL2);
	ctrl3 = readl(ioaddr + GMAC_RXQ_CTRL3);

	/* The software must ensure that the same priority
	 * is not mapped to multiple Rx queues
	 */
	for (i = 0; i < 4; i++)
		clear_mask |= ((prio << GMAC_RXQCTRL_PSRQX_SHIFT(i)) &
						GMAC_RXQCTRL_PSRQX_MASK(i));

	ctrl2 &= ~clear_mask;
	ctrl3 &= ~clear_mask;

	/* First assign new priorities to a queue, then
	 * clear them from others queues
	 */
	if (queue < 4) {
		ctrl2 |= (prio << GMAC_RXQCTRL_PSRQX_SHIFT(queue)) &
						GMAC_RXQCTRL_PSRQX_MASK(queue);

		writel(ctrl2, ioaddr + GMAC_RXQ_CTRL2);
		writel(ctrl3, ioaddr + GMAC_RXQ_CTRL3);
	} else {
		queue -= 4;

		ctrl3 |= (prio << GMAC_RXQCTRL_PSRQX_SHIFT(queue)) &
						GMAC_RXQCTRL_PSRQX_MASK(queue);

		writel(ctrl3, ioaddr + GMAC_RXQ_CTRL3);
		writel(ctrl2, ioaddr + GMAC_RXQ_CTRL2);
	}
}

static void dwmac4_tx_queue_priority(struct mac_device_info *hw,
				     u32 prio, u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 base_register;
	u32 value;

	base_register = (queue < 4) ? GMAC_TXQ_PRTY_MAP0 : GMAC_TXQ_PRTY_MAP1;
	if (queue >= 4)
		queue -= 4;

	value = readl(ioaddr + base_register);

	value &= ~GMAC_TXQCTRL_PSTQX_MASK(queue);
	value |= (prio << GMAC_TXQCTRL_PSTQX_SHIFT(queue)) &
						GMAC_TXQCTRL_PSTQX_MASK(queue);

	writel(value, ioaddr + base_register);
}

static void dwmac4_rx_queue_routing(struct mac_device_info *hw,
				    u8 packet, u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	static const struct stmmac_rx_routing route_possibilities[] = {
		{ GMAC_RXQCTRL_AVCPQ_MASK, GMAC_RXQCTRL_AVCPQ_SHIFT },
		{ GMAC_RXQCTRL_PTPQ_MASK, GMAC_RXQCTRL_PTPQ_SHIFT },
		{ GMAC_RXQCTRL_DCBCPQ_MASK, GMAC_RXQCTRL_DCBCPQ_SHIFT },
		{ GMAC_RXQCTRL_UPQ_MASK, GMAC_RXQCTRL_UPQ_SHIFT },
		{ GMAC_RXQCTRL_MCBCQ_MASK, GMAC_RXQCTRL_MCBCQ_SHIFT },
	};

	value = readl(ioaddr + GMAC_RXQ_CTRL1);

	/* routing configuration */
	value &= ~route_possibilities[packet - 1].reg_mask;
	value |= (queue << route_possibilities[packet-1].reg_shift) &
		 route_possibilities[packet - 1].reg_mask;

	/* some packets require extra ops */
	if (packet == PACKET_AVCPQ) {
		value &= ~GMAC_RXQCTRL_TACPQE;
		value |= 0x1 << GMAC_RXQCTRL_TACPQE_SHIFT;
	} else if (packet == PACKET_MCBCQ) {
		value &= ~GMAC_RXQCTRL_MCBCQEN;
		value |= 0x1 << GMAC_RXQCTRL_MCBCQEN_SHIFT;
	}

	writel(value, ioaddr + GMAC_RXQ_CTRL1);
}

static void dwmac4_prog_mtl_rx_algorithms(struct mac_device_info *hw,
					  u32 rx_alg)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + MTL_OPERATION_MODE);

	value &= ~MTL_OPERATION_RAA;
	switch (rx_alg) {
	case MTL_RX_ALGORITHM_SP:
		value |= MTL_OPERATION_RAA_SP;
		break;
	case MTL_RX_ALGORITHM_WSP:
		value |= MTL_OPERATION_RAA_WSP;
		break;
	default:
		break;
	}

	writel(value, ioaddr + MTL_OPERATION_MODE);
}

static void dwmac4_prog_mtl_tx_algorithms(struct mac_device_info *hw,
					  u32 tx_alg)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + MTL_OPERATION_MODE);

	value &= ~MTL_OPERATION_SCHALG_MASK;
	switch (tx_alg) {
	case MTL_TX_ALGORITHM_WRR:
		value |= MTL_OPERATION_SCHALG_WRR;
		break;
	case MTL_TX_ALGORITHM_WFQ:
		value |= MTL_OPERATION_SCHALG_WFQ;
		break;
	case MTL_TX_ALGORITHM_DWRR:
		value |= MTL_OPERATION_SCHALG_DWRR;
		break;
	case MTL_TX_ALGORITHM_SP:
		value |= MTL_OPERATION_SCHALG_SP;
		break;
	default:
		break;
	}

	writel(value, ioaddr + MTL_OPERATION_MODE);
}

static void dwmac4_set_mtl_tx_queue_weight(struct stmmac_priv *priv,
					   struct mac_device_info *hw,
					   u32 weight, u32 queue)
{
	const struct dwmac4_addrs *dwmac4_addrs = priv->plat->dwmac4_addrs;
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + mtl_txqx_weight_base_addr(dwmac4_addrs,
							     queue));

	value &= ~MTL_TXQ_WEIGHT_ISCQW_MASK;
	value |= weight & MTL_TXQ_WEIGHT_ISCQW_MASK;
	writel(value, ioaddr + mtl_txqx_weight_base_addr(dwmac4_addrs, queue));
}

static void dwmac4_map_mtl_dma(struct mac_device_info *hw, u32 queue, u32 chan)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	if (queue < 4) {
		value = readl(ioaddr + MTL_RXQ_DMA_MAP0);
		value &= ~MTL_RXQ_DMA_QXMDMACH_MASK(queue);
		value |= MTL_RXQ_DMA_QXMDMACH(chan, queue);
		writel(value, ioaddr + MTL_RXQ_DMA_MAP0);
	} else {
		value = readl(ioaddr + MTL_RXQ_DMA_MAP1);
		value &= ~MTL_RXQ_DMA_QXMDMACH_MASK(queue - 4);
		value |= MTL_RXQ_DMA_QXMDMACH(chan, queue - 4);
		writel(value, ioaddr + MTL_RXQ_DMA_MAP1);
	}
}

static void dwmac4_config_cbs(struct stmmac_priv *priv,
			      struct mac_device_info *hw,
			      u32 send_slope, u32 idle_slope,
			      u32 high_credit, u32 low_credit, u32 queue)
{
	const struct dwmac4_addrs *dwmac4_addrs = priv->plat->dwmac4_addrs;
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	pr_debug("Queue %d configured as AVB. Parameters:\n", queue);
	pr_debug("\tsend_slope: 0x%08x\n", send_slope);
	pr_debug("\tidle_slope: 0x%08x\n", idle_slope);
	pr_debug("\thigh_credit: 0x%08x\n", high_credit);
	pr_debug("\tlow_credit: 0x%08x\n", low_credit);

	/* enable AV algorithm */
	value = readl(ioaddr + mtl_etsx_ctrl_base_addr(dwmac4_addrs, queue));
	value |= MTL_ETS_CTRL_AVALG;
	value |= MTL_ETS_CTRL_CC;
	writel(value, ioaddr + mtl_etsx_ctrl_base_addr(dwmac4_addrs, queue));

	/* configure send slope */
	value = readl(ioaddr + mtl_send_slp_credx_base_addr(dwmac4_addrs,
							    queue));
	value &= ~MTL_SEND_SLP_CRED_SSC_MASK;
	value |= send_slope & MTL_SEND_SLP_CRED_SSC_MASK;
	writel(value, ioaddr + mtl_send_slp_credx_base_addr(dwmac4_addrs,
							    queue));

	/* configure idle slope (same register as tx weight) */
	dwmac4_set_mtl_tx_queue_weight(priv, hw, idle_slope, queue);

	/* configure high credit */
	value = readl(ioaddr + mtl_high_credx_base_addr(dwmac4_addrs, queue));
	value &= ~MTL_HIGH_CRED_HC_MASK;
	value |= high_credit & MTL_HIGH_CRED_HC_MASK;
	writel(value, ioaddr + mtl_high_credx_base_addr(dwmac4_addrs, queue));

	/* configure high credit */
	value = readl(ioaddr + mtl_low_credx_base_addr(dwmac4_addrs, queue));
	value &= ~MTL_HIGH_CRED_LC_MASK;
	value |= low_credit & MTL_HIGH_CRED_LC_MASK;
	writel(value, ioaddr + mtl_low_credx_base_addr(dwmac4_addrs, queue));
}

static void dwmac4_dump_regs(struct mac_device_info *hw, u32 *reg_space)
{
	void __iomem *ioaddr = hw->pcsr;
	int i;

	for (i = 0; i < GMAC_REG_NUM; i++)
		reg_space[i] = readl(ioaddr + i * 4);
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
	u32 config;

	if (mode & WAKE_MAGIC) {
		pr_debug("GMAC: WOL Magic frame\n");
		pmt |= power_down | magic_pkt_en;
	}
	if (mode & WAKE_UCAST) {
		pr_debug("GMAC: WOL on global unicast\n");
		pmt |= power_down | global_unicast | wake_up_frame_en;
	}

	if (pmt) {
		/* The receiver must be enabled for WOL before powering down */
		config = readl(ioaddr + GMAC_CONFIG);
		config |= GMAC_CONFIG_RE;
		writel(config, ioaddr + GMAC_CONFIG);
	}
	writel(pmt, ioaddr + GMAC_PMT);
}

static void dwmac4_set_umac_addr(struct mac_device_info *hw,
				 const unsigned char *addr, unsigned int reg_n)
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

static void dwmac4_set_eee_mode(struct mac_device_info *hw,
				bool en_tx_lpi_clockgating)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	/* Enable the link status receive on RGMII, SGMII ore SMII
	 * receive path and instruct the transmit to enter in LPI
	 * state.
	 */
	value = readl(ioaddr + GMAC4_LPI_CTRL_STATUS);
	value |= GMAC4_LPI_CTRL_STATUS_LPIEN | GMAC4_LPI_CTRL_STATUS_LPITXA;

	if (en_tx_lpi_clockgating)
		value |= GMAC4_LPI_CTRL_STATUS_LPITCSE;

	writel(value, ioaddr + GMAC4_LPI_CTRL_STATUS);
}

static void dwmac4_reset_eee_mode(struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + GMAC4_LPI_CTRL_STATUS);
	value &= ~(GMAC4_LPI_CTRL_STATUS_LPIEN | GMAC4_LPI_CTRL_STATUS_LPITXA);
	writel(value, ioaddr + GMAC4_LPI_CTRL_STATUS);
}

static void dwmac4_set_eee_pls(struct mac_device_info *hw, int link)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + GMAC4_LPI_CTRL_STATUS);

	if (link)
		value |= GMAC4_LPI_CTRL_STATUS_PLS;
	else
		value &= ~GMAC4_LPI_CTRL_STATUS_PLS;

	writel(value, ioaddr + GMAC4_LPI_CTRL_STATUS);
}

static void dwmac4_set_eee_lpi_entry_timer(struct mac_device_info *hw, int et)
{
	void __iomem *ioaddr = hw->pcsr;
	int value = et & STMMAC_ET_MAX;
	int regval;

	/* Program LPI entry timer value into register */
	writel(value, ioaddr + GMAC4_LPI_ENTRY_TIMER);

	/* Enable/disable LPI entry timer */
	regval = readl(ioaddr + GMAC4_LPI_CTRL_STATUS);
	regval |= GMAC4_LPI_CTRL_STATUS_LPIEN | GMAC4_LPI_CTRL_STATUS_LPITXA;

	if (et)
		regval |= GMAC4_LPI_CTRL_STATUS_LPIATE;
	else
		regval &= ~GMAC4_LPI_CTRL_STATUS_LPIATE;

	writel(regval, ioaddr + GMAC4_LPI_CTRL_STATUS);
}

static void dwmac4_set_eee_timer(struct mac_device_info *hw, int ls, int tw)
{
	void __iomem *ioaddr = hw->pcsr;
	int value = ((tw & 0xffff)) | ((ls & 0x3ff) << 16);

	/* Program the timers in the LPI timer control register:
	 * LS: minimum time (ms) for which the link
	 *  status from PHY should be ok before transmitting
	 *  the LPI pattern.
	 * TW: minimum time (us) for which the core waits
	 *  after it has stopped transmitting the LPI pattern.
	 */
	writel(value, ioaddr + GMAC4_LPI_TIMER_CTRL);
}

static void dwmac4_write_single_vlan(struct net_device *dev, u16 vid)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	u32 val;

	val = readl(ioaddr + GMAC_VLAN_TAG);
	val &= ~GMAC_VLAN_TAG_VID;
	val |= GMAC_VLAN_TAG_ETV | vid;

	writel(val, ioaddr + GMAC_VLAN_TAG);
}

static int dwmac4_write_vlan_filter(struct net_device *dev,
				    struct mac_device_info *hw,
				    u8 index, u32 data)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	int ret;
	u32 val;

	if (index >= hw->num_vlan)
		return -EINVAL;

	writel(data, ioaddr + GMAC_VLAN_TAG_DATA);

	val = readl(ioaddr + GMAC_VLAN_TAG);
	val &= ~(GMAC_VLAN_TAG_CTRL_OFS_MASK |
		GMAC_VLAN_TAG_CTRL_CT |
		GMAC_VLAN_TAG_CTRL_OB);
	val |= (index << GMAC_VLAN_TAG_CTRL_OFS_SHIFT) | GMAC_VLAN_TAG_CTRL_OB;

	writel(val, ioaddr + GMAC_VLAN_TAG);

	ret = readl_poll_timeout(ioaddr + GMAC_VLAN_TAG, val,
				 !(val & GMAC_VLAN_TAG_CTRL_OB),
				 1000, 500000);
	if (ret) {
		netdev_err(dev, "Timeout accessing MAC_VLAN_Tag_Filter\n");
		return -EBUSY;
	}

	return 0;
}

static int dwmac4_add_hw_vlan_rx_fltr(struct net_device *dev,
				      struct mac_device_info *hw,
				      __be16 proto, u16 vid)
{
	int index = -1;
	u32 val = 0;
	int i, ret;

	if (vid > 4095)
		return -EINVAL;

	/* Single Rx VLAN Filter */
	if (hw->num_vlan == 1) {
		/* For single VLAN filter, VID 0 means VLAN promiscuous */
		if (vid == 0) {
			netdev_warn(dev, "Adding VLAN ID 0 is not supported\n");
			return -EPERM;
		}

		if (hw->vlan_filter[0] & GMAC_VLAN_TAG_VID) {
			netdev_err(dev, "Only single VLAN ID supported\n");
			return -EPERM;
		}

		hw->vlan_filter[0] = vid;
		dwmac4_write_single_vlan(dev, vid);

		return 0;
	}

	/* Extended Rx VLAN Filter Enable */
	val |= GMAC_VLAN_TAG_DATA_ETV | GMAC_VLAN_TAG_DATA_VEN | vid;

	for (i = 0; i < hw->num_vlan; i++) {
		if (hw->vlan_filter[i] == val)
			return 0;
		else if (!(hw->vlan_filter[i] & GMAC_VLAN_TAG_DATA_VEN))
			index = i;
	}

	if (index == -1) {
		netdev_err(dev, "MAC_VLAN_Tag_Filter full (size: %0u)\n",
			   hw->num_vlan);
		return -EPERM;
	}

	ret = dwmac4_write_vlan_filter(dev, hw, index, val);

	if (!ret)
		hw->vlan_filter[index] = val;

	return ret;
}

static int dwmac4_del_hw_vlan_rx_fltr(struct net_device *dev,
				      struct mac_device_info *hw,
				      __be16 proto, u16 vid)
{
	int i, ret = 0;

	/* Single Rx VLAN Filter */
	if (hw->num_vlan == 1) {
		if ((hw->vlan_filter[0] & GMAC_VLAN_TAG_VID) == vid) {
			hw->vlan_filter[0] = 0;
			dwmac4_write_single_vlan(dev, 0);
		}
		return 0;
	}

	/* Extended Rx VLAN Filter Enable */
	for (i = 0; i < hw->num_vlan; i++) {
		if ((hw->vlan_filter[i] & GMAC_VLAN_TAG_DATA_VID) == vid) {
			ret = dwmac4_write_vlan_filter(dev, hw, i, 0);

			if (!ret)
				hw->vlan_filter[i] = 0;
			else
				return ret;
		}
	}

	return ret;
}

static void dwmac4_restore_hw_vlan_rx_fltr(struct net_device *dev,
					   struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	u32 hash;
	u32 val;
	int i;

	/* Single Rx VLAN Filter */
	if (hw->num_vlan == 1) {
		dwmac4_write_single_vlan(dev, hw->vlan_filter[0]);
		return;
	}

	/* Extended Rx VLAN Filter Enable */
	for (i = 0; i < hw->num_vlan; i++) {
		if (hw->vlan_filter[i] & GMAC_VLAN_TAG_DATA_VEN) {
			val = hw->vlan_filter[i];
			dwmac4_write_vlan_filter(dev, hw, i, val);
		}
	}

	hash = readl(ioaddr + GMAC_VLAN_HASH_TABLE);
	if (hash & GMAC_VLAN_VLHT) {
		value = readl(ioaddr + GMAC_VLAN_TAG);
		value |= GMAC_VLAN_VTHM;
		writel(value, ioaddr + GMAC_VLAN_TAG);
	}
}

static void dwmac4_set_filter(struct mac_device_info *hw,
			      struct net_device *dev)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	int numhashregs = (hw->multicast_filter_bins >> 5);
	int mcbitslog2 = hw->mcast_bits_log2;
	unsigned int value;
	u32 mc_filter[8];
	int i;

	memset(mc_filter, 0, sizeof(mc_filter));

	value = readl(ioaddr + GMAC_PACKET_FILTER);
	value &= ~GMAC_PACKET_FILTER_HMC;
	value &= ~GMAC_PACKET_FILTER_HPF;
	value &= ~GMAC_PACKET_FILTER_PCF;
	value &= ~GMAC_PACKET_FILTER_PM;
	value &= ~GMAC_PACKET_FILTER_PR;
	value &= ~GMAC_PACKET_FILTER_RA;
	if (dev->flags & IFF_PROMISC) {
		/* VLAN Tag Filter Fail Packets Queuing */
		if (hw->vlan_fail_q_en) {
			value = readl(ioaddr + GMAC_RXQ_CTRL4);
			value &= ~GMAC_RXQCTRL_VFFQ_MASK;
			value |= GMAC_RXQCTRL_VFFQE |
				 (hw->vlan_fail_q << GMAC_RXQCTRL_VFFQ_SHIFT);
			writel(value, ioaddr + GMAC_RXQ_CTRL4);
			value = GMAC_PACKET_FILTER_PR | GMAC_PACKET_FILTER_RA;
		} else {
			value = GMAC_PACKET_FILTER_PR | GMAC_PACKET_FILTER_PCF;
		}

	} else if ((dev->flags & IFF_ALLMULTI) ||
		   (netdev_mc_count(dev) > hw->multicast_filter_bins)) {
		/* Pass all multi */
		value |= GMAC_PACKET_FILTER_PM;
		/* Set all the bits of the HASH tab */
		memset(mc_filter, 0xff, sizeof(mc_filter));
	} else if (!netdev_mc_empty(dev) && (dev->flags & IFF_MULTICAST)) {
		struct netdev_hw_addr *ha;

		/* Hash filter for multicast */
		value |= GMAC_PACKET_FILTER_HMC;

		netdev_for_each_mc_addr(ha, dev) {
			/* The upper n bits of the calculated CRC are used to
			 * index the contents of the hash table. The number of
			 * bits used depends on the hardware configuration
			 * selected at core configuration time.
			 */
			u32 bit_nr = bitrev32(~crc32_le(~0, ha->addr,
					ETH_ALEN)) >> (32 - mcbitslog2);
			/* The most significant bit determines the register to
			 * use (H/L) while the other 5 bits determine the bit
			 * within the register.
			 */
			mc_filter[bit_nr >> 5] |= (1 << (bit_nr & 0x1f));
		}
	}

	for (i = 0; i < numhashregs; i++)
		writel(mc_filter[i], ioaddr + GMAC_HASH_TAB(i));

	value |= GMAC_PACKET_FILTER_HPF;

	/* Handle multiple unicast addresses */
	if (netdev_uc_count(dev) > hw->unicast_filter_entries) {
		/* Switch to promiscuous mode if more than 128 addrs
		 * are required
		 */
		value |= GMAC_PACKET_FILTER_PR;
	} else {
		struct netdev_hw_addr *ha;
		int reg = 1;

		netdev_for_each_uc_addr(ha, dev) {
			dwmac4_set_umac_addr(hw, ha->addr, reg);
			reg++;
		}

		while (reg < GMAC_MAX_PERFECT_ADDRESSES) {
			writel(0, ioaddr + GMAC_ADDR_HIGH(reg));
			writel(0, ioaddr + GMAC_ADDR_LOW(reg));
			reg++;
		}
	}

	/* VLAN filtering */
	if (dev->flags & IFF_PROMISC && !hw->vlan_fail_q_en)
		value &= ~GMAC_PACKET_FILTER_VTFE;
	else if (dev->features & NETIF_F_HW_VLAN_CTAG_FILTER)
		value |= GMAC_PACKET_FILTER_VTFE;

	writel(value, ioaddr + GMAC_PACKET_FILTER);
}

static void dwmac4_flow_ctrl(struct mac_device_info *hw, unsigned int duplex,
			     unsigned int fc, unsigned int pause_time,
			     u32 tx_cnt)
{
	void __iomem *ioaddr = hw->pcsr;
	unsigned int flow = 0;
	u32 queue = 0;

	pr_debug("GMAC Flow-Control:\n");
	if (fc & FLOW_RX) {
		pr_debug("\tReceive Flow-Control ON\n");
		flow |= GMAC_RX_FLOW_CTRL_RFE;
	} else {
		pr_debug("\tReceive Flow-Control OFF\n");
	}
	writel(flow, ioaddr + GMAC_RX_FLOW_CTRL);

	if (fc & FLOW_TX) {
		pr_debug("\tTransmit Flow-Control ON\n");

		if (duplex)
			pr_debug("\tduplex mode: PAUSE %d\n", pause_time);

		for (queue = 0; queue < tx_cnt; queue++) {
			flow = GMAC_TX_FLOW_CTRL_TFE;

			if (duplex)
				flow |=
				(pause_time << GMAC_TX_FLOW_CTRL_PT_SHIFT);

			writel(flow, ioaddr + GMAC_QX_TX_FLOW_CTRL(queue));
		}
	} else {
		for (queue = 0; queue < tx_cnt; queue++)
			writel(0, ioaddr + GMAC_QX_TX_FLOW_CTRL(queue));
	}
}

static void dwmac4_ctrl_ane(void __iomem *ioaddr, bool ane, bool srgmi_ral,
			    bool loopback)
{
	dwmac_ctrl_ane(ioaddr, GMAC_PCS_BASE, ane, srgmi_ral, loopback);
}

static void dwmac4_get_adv_lp(void __iomem *ioaddr, struct rgmii_adv *adv)
{
	dwmac_get_adv_lp(ioaddr, GMAC_PCS_BASE, adv);
}

/* RGMII or SMII interface */
static void dwmac4_phystatus(void __iomem *ioaddr, struct stmmac_extra_stats *x)
{
	u32 status;

	status = readl(ioaddr + GMAC_PHYIF_CONTROL_STATUS);
	x->irq_rgmii_n++;

	/* Check the link status */
	if (status & GMAC_PHYIF_CTRLSTATUS_LNKSTS) {
		int speed_value;

		x->pcs_link = 1;

		speed_value = ((status & GMAC_PHYIF_CTRLSTATUS_SPEED) >>
			       GMAC_PHYIF_CTRLSTATUS_SPEED_SHIFT);
		if (speed_value == GMAC_PHYIF_CTRLSTATUS_SPEED_125)
			x->pcs_speed = SPEED_1000;
		else if (speed_value == GMAC_PHYIF_CTRLSTATUS_SPEED_25)
			x->pcs_speed = SPEED_100;
		else
			x->pcs_speed = SPEED_10;

		x->pcs_duplex = (status & GMAC_PHYIF_CTRLSTATUS_LNKMOD);

		pr_info("Link is Up - %d/%s\n", (int)x->pcs_speed,
			x->pcs_duplex ? "Full" : "Half");
	} else {
		x->pcs_link = 0;
		pr_info("Link is Down\n");
	}
}

static int dwmac4_irq_mtl_status(struct stmmac_priv *priv,
				 struct mac_device_info *hw, u32 chan)
{
	const struct dwmac4_addrs *dwmac4_addrs = priv->plat->dwmac4_addrs;
	void __iomem *ioaddr = hw->pcsr;
	u32 mtl_int_qx_status;
	int ret = 0;

	mtl_int_qx_status = readl(ioaddr + MTL_INT_STATUS);

	/* Check MTL Interrupt */
	if (mtl_int_qx_status & MTL_INT_QX(chan)) {
		/* read Queue x Interrupt status */
		u32 status = readl(ioaddr + MTL_CHAN_INT_CTRL(dwmac4_addrs,
							      chan));

		if (status & MTL_RX_OVERFLOW_INT) {
			/*  clear Interrupt */
			writel(status | MTL_RX_OVERFLOW_INT,
			       ioaddr + MTL_CHAN_INT_CTRL(dwmac4_addrs, chan));
			ret = CORE_IRQ_MTL_RX_OVERFLOW;
		}
	}

	return ret;
}

static int dwmac4_irq_status(struct mac_device_info *hw,
			     struct stmmac_extra_stats *x)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 intr_status = readl(ioaddr + GMAC_INT_STATUS);
	u32 intr_enable = readl(ioaddr + GMAC_INT_EN);
	int ret = 0;

	/* Discard disabled bits */
	intr_status &= intr_enable;

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

	/* MAC tx/rx EEE LPI entry/exit interrupts */
	if (intr_status & lpi_irq) {
		/* Clear LPI interrupt by reading MAC_LPI_Control_Status */
		u32 status = readl(ioaddr + GMAC4_LPI_CTRL_STATUS);

		if (status & GMAC4_LPI_CTRL_STATUS_TLPIEN) {
			ret |= CORE_IRQ_TX_PATH_IN_LPI_MODE;
			x->irq_tx_path_in_lpi_mode_n++;
		}
		if (status & GMAC4_LPI_CTRL_STATUS_TLPIEX) {
			ret |= CORE_IRQ_TX_PATH_EXIT_LPI_MODE;
			x->irq_tx_path_exit_lpi_mode_n++;
		}
		if (status & GMAC4_LPI_CTRL_STATUS_RLPIEN)
			x->irq_rx_path_in_lpi_mode_n++;
		if (status & GMAC4_LPI_CTRL_STATUS_RLPIEX)
			x->irq_rx_path_exit_lpi_mode_n++;
	}

	dwmac_pcs_isr(ioaddr, GMAC_PCS_BASE, intr_status, x);
	if (intr_status & PCS_RGSMIIIS_IRQ)
		dwmac4_phystatus(ioaddr, x);

	return ret;
}

static void dwmac4_debug(struct stmmac_priv *priv, void __iomem *ioaddr,
			 struct stmmac_extra_stats *x,
			 u32 rx_queues, u32 tx_queues)
{
	const struct dwmac4_addrs *dwmac4_addrs = priv->plat->dwmac4_addrs;
	u32 value;
	u32 queue;

	for (queue = 0; queue < tx_queues; queue++) {
		value = readl(ioaddr + MTL_CHAN_TX_DEBUG(dwmac4_addrs, queue));

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
	}

	for (queue = 0; queue < rx_queues; queue++) {
		value = readl(ioaddr + MTL_CHAN_RX_DEBUG(dwmac4_addrs, queue));

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
	}

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

static void dwmac4_set_mac_loopback(void __iomem *ioaddr, bool enable)
{
	u32 value = readl(ioaddr + GMAC_CONFIG);

	if (enable)
		value |= GMAC_CONFIG_LM;
	else
		value &= ~GMAC_CONFIG_LM;

	writel(value, ioaddr + GMAC_CONFIG);
}

static void dwmac4_update_vlan_hash(struct mac_device_info *hw, u32 hash,
				    u16 perfect_match, bool is_double)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	writel(hash, ioaddr + GMAC_VLAN_HASH_TABLE);

	value = readl(ioaddr + GMAC_VLAN_TAG);

	if (hash) {
		value |= GMAC_VLAN_VTHM | GMAC_VLAN_ETV;
		if (is_double) {
			value |= GMAC_VLAN_EDVLP;
			value |= GMAC_VLAN_ESVL;
			value |= GMAC_VLAN_DOVLTC;
		}

		writel(value, ioaddr + GMAC_VLAN_TAG);
	} else if (perfect_match) {
		u32 value = GMAC_VLAN_ETV;

		if (is_double) {
			value |= GMAC_VLAN_EDVLP;
			value |= GMAC_VLAN_ESVL;
			value |= GMAC_VLAN_DOVLTC;
		}

		writel(value | perfect_match, ioaddr + GMAC_VLAN_TAG);
	} else {
		value &= ~(GMAC_VLAN_VTHM | GMAC_VLAN_ETV);
		value &= ~(GMAC_VLAN_EDVLP | GMAC_VLAN_ESVL);
		value &= ~GMAC_VLAN_DOVLTC;
		value &= ~GMAC_VLAN_VID;

		writel(value, ioaddr + GMAC_VLAN_TAG);
	}
}

static void dwmac4_sarc_configure(void __iomem *ioaddr, int val)
{
	u32 value = readl(ioaddr + GMAC_CONFIG);

	value &= ~GMAC_CONFIG_SARC;
	value |= val << GMAC_CONFIG_SARC_SHIFT;

	writel(value, ioaddr + GMAC_CONFIG);
}

static void dwmac4_enable_vlan(struct mac_device_info *hw, u32 type)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + GMAC_VLAN_INCL);
	value |= GMAC_VLAN_VLTI;
	value |= GMAC_VLAN_CSVL; /* Only use SVLAN */
	value &= ~GMAC_VLAN_VLC;
	value |= (type << GMAC_VLAN_VLC_SHIFT) & GMAC_VLAN_VLC;
	writel(value, ioaddr + GMAC_VLAN_INCL);
}

static void dwmac4_set_arp_offload(struct mac_device_info *hw, bool en,
				   u32 addr)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	writel(addr, ioaddr + GMAC_ARP_ADDR);

	value = readl(ioaddr + GMAC_CONFIG);
	if (en)
		value |= GMAC_CONFIG_ARPEN;
	else
		value &= ~GMAC_CONFIG_ARPEN;
	writel(value, ioaddr + GMAC_CONFIG);
}

static int dwmac4_config_l3_filter(struct mac_device_info *hw, u32 filter_no,
				   bool en, bool ipv6, bool sa, bool inv,
				   u32 match)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + GMAC_PACKET_FILTER);
	value |= GMAC_PACKET_FILTER_IPFE;
	writel(value, ioaddr + GMAC_PACKET_FILTER);

	value = readl(ioaddr + GMAC_L3L4_CTRL(filter_no));

	/* For IPv6 not both SA/DA filters can be active */
	if (ipv6) {
		value |= GMAC_L3PEN0;
		value &= ~(GMAC_L3SAM0 | GMAC_L3SAIM0);
		value &= ~(GMAC_L3DAM0 | GMAC_L3DAIM0);
		if (sa) {
			value |= GMAC_L3SAM0;
			if (inv)
				value |= GMAC_L3SAIM0;
		} else {
			value |= GMAC_L3DAM0;
			if (inv)
				value |= GMAC_L3DAIM0;
		}
	} else {
		value &= ~GMAC_L3PEN0;
		if (sa) {
			value |= GMAC_L3SAM0;
			if (inv)
				value |= GMAC_L3SAIM0;
		} else {
			value |= GMAC_L3DAM0;
			if (inv)
				value |= GMAC_L3DAIM0;
		}
	}

	writel(value, ioaddr + GMAC_L3L4_CTRL(filter_no));

	if (sa) {
		writel(match, ioaddr + GMAC_L3_ADDR0(filter_no));
	} else {
		writel(match, ioaddr + GMAC_L3_ADDR1(filter_no));
	}

	if (!en)
		writel(0, ioaddr + GMAC_L3L4_CTRL(filter_no));

	return 0;
}

static int dwmac4_config_l4_filter(struct mac_device_info *hw, u32 filter_no,
				   bool en, bool udp, bool sa, bool inv,
				   u32 match)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + GMAC_PACKET_FILTER);
	value |= GMAC_PACKET_FILTER_IPFE;
	writel(value, ioaddr + GMAC_PACKET_FILTER);

	value = readl(ioaddr + GMAC_L3L4_CTRL(filter_no));
	if (udp) {
		value |= GMAC_L4PEN0;
	} else {
		value &= ~GMAC_L4PEN0;
	}

	value &= ~(GMAC_L4SPM0 | GMAC_L4SPIM0);
	value &= ~(GMAC_L4DPM0 | GMAC_L4DPIM0);
	if (sa) {
		value |= GMAC_L4SPM0;
		if (inv)
			value |= GMAC_L4SPIM0;
	} else {
		value |= GMAC_L4DPM0;
		if (inv)
			value |= GMAC_L4DPIM0;
	}

	writel(value, ioaddr + GMAC_L3L4_CTRL(filter_no));

	if (sa) {
		value = match & GMAC_L4SP0;
	} else {
		value = (match << GMAC_L4DP0_SHIFT) & GMAC_L4DP0;
	}

	writel(value, ioaddr + GMAC_L4_ADDR(filter_no));

	if (!en)
		writel(0, ioaddr + GMAC_L3L4_CTRL(filter_no));

	return 0;
}

static void dwmac4_rx_hw_vlan(struct mac_device_info *hw,
			      struct dma_desc *rx_desc, struct sk_buff *skb)
{
	if (hw->desc->get_rx_vlan_valid(rx_desc)) {
		u16 vid = hw->desc->get_rx_vlan_tci(rx_desc);

		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);
	}
}

static void dwmac4_set_hw_vlan_mode(struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value = readl(ioaddr + GMAC_VLAN_TAG);

	value &= ~GMAC_VLAN_TAG_CTRL_EVLS_MASK;

	if (hw->hw_vlan_en)
		/* Always strip VLAN on Receive */
		value |= GMAC_VLAN_TAG_STRIP_ALL;
	else
		/* Do not strip VLAN on Receive */
		value |= GMAC_VLAN_TAG_STRIP_NONE;

	/* Enable outer VLAN Tag in Rx DMA descriptor */
	value |= GMAC_VLAN_TAG_CTRL_EVLRXS;
	writel(value, ioaddr + GMAC_VLAN_TAG);
}

const struct stmmac_ops dwmac4_ops = {
	.core_init = dwmac4_core_init,
	.update_caps = dwmac4_update_caps,
	.set_mac = stmmac_set_mac,
	.rx_ipc = dwmac4_rx_ipc_enable,
	.rx_queue_enable = dwmac4_rx_queue_enable,
	.rx_queue_prio = dwmac4_rx_queue_priority,
	.tx_queue_prio = dwmac4_tx_queue_priority,
	.rx_queue_routing = dwmac4_rx_queue_routing,
	.prog_mtl_rx_algorithms = dwmac4_prog_mtl_rx_algorithms,
	.prog_mtl_tx_algorithms = dwmac4_prog_mtl_tx_algorithms,
	.set_mtl_tx_queue_weight = dwmac4_set_mtl_tx_queue_weight,
	.map_mtl_to_dma = dwmac4_map_mtl_dma,
	.config_cbs = dwmac4_config_cbs,
	.dump_regs = dwmac4_dump_regs,
	.host_irq_status = dwmac4_irq_status,
	.host_mtl_irq_status = dwmac4_irq_mtl_status,
	.flow_ctrl = dwmac4_flow_ctrl,
	.pmt = dwmac4_pmt,
	.set_umac_addr = dwmac4_set_umac_addr,
	.get_umac_addr = dwmac4_get_umac_addr,
	.set_eee_mode = dwmac4_set_eee_mode,
	.reset_eee_mode = dwmac4_reset_eee_mode,
	.set_eee_lpi_entry_timer = dwmac4_set_eee_lpi_entry_timer,
	.set_eee_timer = dwmac4_set_eee_timer,
	.set_eee_pls = dwmac4_set_eee_pls,
	.pcs_ctrl_ane = dwmac4_ctrl_ane,
	.pcs_get_adv_lp = dwmac4_get_adv_lp,
	.debug = dwmac4_debug,
	.set_filter = dwmac4_set_filter,
	.set_mac_loopback = dwmac4_set_mac_loopback,
	.update_vlan_hash = dwmac4_update_vlan_hash,
	.sarc_configure = dwmac4_sarc_configure,
	.enable_vlan = dwmac4_enable_vlan,
	.set_arp_offload = dwmac4_set_arp_offload,
	.config_l3_filter = dwmac4_config_l3_filter,
	.config_l4_filter = dwmac4_config_l4_filter,
	.add_hw_vlan_rx_fltr = dwmac4_add_hw_vlan_rx_fltr,
	.del_hw_vlan_rx_fltr = dwmac4_del_hw_vlan_rx_fltr,
	.restore_hw_vlan_rx_fltr = dwmac4_restore_hw_vlan_rx_fltr,
	.rx_hw_vlan = dwmac4_rx_hw_vlan,
	.set_hw_vlan_mode = dwmac4_set_hw_vlan_mode,
};

const struct stmmac_ops dwmac410_ops = {
	.core_init = dwmac4_core_init,
	.update_caps = dwmac4_update_caps,
	.set_mac = stmmac_dwmac4_set_mac,
	.rx_ipc = dwmac4_rx_ipc_enable,
	.rx_queue_enable = dwmac4_rx_queue_enable,
	.rx_queue_prio = dwmac4_rx_queue_priority,
	.tx_queue_prio = dwmac4_tx_queue_priority,
	.rx_queue_routing = dwmac4_rx_queue_routing,
	.prog_mtl_rx_algorithms = dwmac4_prog_mtl_rx_algorithms,
	.prog_mtl_tx_algorithms = dwmac4_prog_mtl_tx_algorithms,
	.set_mtl_tx_queue_weight = dwmac4_set_mtl_tx_queue_weight,
	.map_mtl_to_dma = dwmac4_map_mtl_dma,
	.config_cbs = dwmac4_config_cbs,
	.dump_regs = dwmac4_dump_regs,
	.host_irq_status = dwmac4_irq_status,
	.host_mtl_irq_status = dwmac4_irq_mtl_status,
	.flow_ctrl = dwmac4_flow_ctrl,
	.pmt = dwmac4_pmt,
	.set_umac_addr = dwmac4_set_umac_addr,
	.get_umac_addr = dwmac4_get_umac_addr,
	.set_eee_mode = dwmac4_set_eee_mode,
	.reset_eee_mode = dwmac4_reset_eee_mode,
	.set_eee_lpi_entry_timer = dwmac4_set_eee_lpi_entry_timer,
	.set_eee_timer = dwmac4_set_eee_timer,
	.set_eee_pls = dwmac4_set_eee_pls,
	.pcs_ctrl_ane = dwmac4_ctrl_ane,
	.pcs_get_adv_lp = dwmac4_get_adv_lp,
	.debug = dwmac4_debug,
	.set_filter = dwmac4_set_filter,
	.flex_pps_config = dwmac5_flex_pps_config,
	.set_mac_loopback = dwmac4_set_mac_loopback,
	.update_vlan_hash = dwmac4_update_vlan_hash,
	.sarc_configure = dwmac4_sarc_configure,
	.enable_vlan = dwmac4_enable_vlan,
	.set_arp_offload = dwmac4_set_arp_offload,
	.config_l3_filter = dwmac4_config_l3_filter,
	.config_l4_filter = dwmac4_config_l4_filter,
	.fpe_map_preemption_class = dwmac5_fpe_map_preemption_class,
	.add_hw_vlan_rx_fltr = dwmac4_add_hw_vlan_rx_fltr,
	.del_hw_vlan_rx_fltr = dwmac4_del_hw_vlan_rx_fltr,
	.restore_hw_vlan_rx_fltr = dwmac4_restore_hw_vlan_rx_fltr,
	.rx_hw_vlan = dwmac4_rx_hw_vlan,
	.set_hw_vlan_mode = dwmac4_set_hw_vlan_mode,
};

const struct stmmac_ops dwmac510_ops = {
	.core_init = dwmac4_core_init,
	.update_caps = dwmac4_update_caps,
	.set_mac = stmmac_dwmac4_set_mac,
	.rx_ipc = dwmac4_rx_ipc_enable,
	.rx_queue_enable = dwmac4_rx_queue_enable,
	.rx_queue_prio = dwmac4_rx_queue_priority,
	.tx_queue_prio = dwmac4_tx_queue_priority,
	.rx_queue_routing = dwmac4_rx_queue_routing,
	.prog_mtl_rx_algorithms = dwmac4_prog_mtl_rx_algorithms,
	.prog_mtl_tx_algorithms = dwmac4_prog_mtl_tx_algorithms,
	.set_mtl_tx_queue_weight = dwmac4_set_mtl_tx_queue_weight,
	.map_mtl_to_dma = dwmac4_map_mtl_dma,
	.config_cbs = dwmac4_config_cbs,
	.dump_regs = dwmac4_dump_regs,
	.host_irq_status = dwmac4_irq_status,
	.host_mtl_irq_status = dwmac4_irq_mtl_status,
	.flow_ctrl = dwmac4_flow_ctrl,
	.pmt = dwmac4_pmt,
	.set_umac_addr = dwmac4_set_umac_addr,
	.get_umac_addr = dwmac4_get_umac_addr,
	.set_eee_mode = dwmac4_set_eee_mode,
	.reset_eee_mode = dwmac4_reset_eee_mode,
	.set_eee_lpi_entry_timer = dwmac4_set_eee_lpi_entry_timer,
	.set_eee_timer = dwmac4_set_eee_timer,
	.set_eee_pls = dwmac4_set_eee_pls,
	.pcs_ctrl_ane = dwmac4_ctrl_ane,
	.pcs_get_adv_lp = dwmac4_get_adv_lp,
	.debug = dwmac4_debug,
	.set_filter = dwmac4_set_filter,
	.safety_feat_config = dwmac5_safety_feat_config,
	.safety_feat_irq_status = dwmac5_safety_feat_irq_status,
	.safety_feat_dump = dwmac5_safety_feat_dump,
	.rxp_config = dwmac5_rxp_config,
	.flex_pps_config = dwmac5_flex_pps_config,
	.set_mac_loopback = dwmac4_set_mac_loopback,
	.update_vlan_hash = dwmac4_update_vlan_hash,
	.sarc_configure = dwmac4_sarc_configure,
	.enable_vlan = dwmac4_enable_vlan,
	.set_arp_offload = dwmac4_set_arp_offload,
	.config_l3_filter = dwmac4_config_l3_filter,
	.config_l4_filter = dwmac4_config_l4_filter,
	.fpe_map_preemption_class = dwmac5_fpe_map_preemption_class,
	.add_hw_vlan_rx_fltr = dwmac4_add_hw_vlan_rx_fltr,
	.del_hw_vlan_rx_fltr = dwmac4_del_hw_vlan_rx_fltr,
	.restore_hw_vlan_rx_fltr = dwmac4_restore_hw_vlan_rx_fltr,
	.rx_hw_vlan = dwmac4_rx_hw_vlan,
	.set_hw_vlan_mode = dwmac4_set_hw_vlan_mode,
};

static u32 dwmac4_get_num_vlan(void __iomem *ioaddr)
{
	u32 val, num_vlan;

	val = readl(ioaddr + GMAC_HW_FEATURE3);
	switch (val & GMAC_HW_FEAT_NRVF) {
	case 0:
		num_vlan = 1;
		break;
	case 1:
		num_vlan = 4;
		break;
	case 2:
		num_vlan = 8;
		break;
	case 3:
		num_vlan = 16;
		break;
	case 4:
		num_vlan = 24;
		break;
	case 5:
		num_vlan = 32;
		break;
	default:
		num_vlan = 1;
	}

	return num_vlan;
}

int dwmac4_setup(struct stmmac_priv *priv)
{
	struct mac_device_info *mac = priv->hw;

	dev_info(priv->device, "\tDWMAC4/5\n");

	priv->dev->priv_flags |= IFF_UNICAST_FLT;
	mac->pcsr = priv->ioaddr;
	mac->multicast_filter_bins = priv->plat->multicast_filter_bins;
	mac->unicast_filter_entries = priv->plat->unicast_filter_entries;
	mac->mcast_bits_log2 = 0;

	if (mac->multicast_filter_bins)
		mac->mcast_bits_log2 = ilog2(mac->multicast_filter_bins);

	mac->link.caps = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
			 MAC_10 | MAC_100 | MAC_1000 | MAC_2500FD;
	mac->link.duplex = GMAC_CONFIG_DM;
	mac->link.speed10 = GMAC_CONFIG_PS;
	mac->link.speed100 = GMAC_CONFIG_FES | GMAC_CONFIG_PS;
	mac->link.speed1000 = 0;
	mac->link.speed2500 = GMAC_CONFIG_FES;
	mac->link.speed_mask = GMAC_CONFIG_FES | GMAC_CONFIG_PS;
	mac->mii.addr = GMAC_MDIO_ADDR;
	mac->mii.data = GMAC_MDIO_DATA;
	mac->mii.addr_shift = 21;
	mac->mii.addr_mask = GENMASK(25, 21);
	mac->mii.reg_shift = 16;
	mac->mii.reg_mask = GENMASK(20, 16);
	mac->mii.clk_csr_shift = 8;
	mac->mii.clk_csr_mask = GENMASK(11, 8);
	mac->num_vlan = dwmac4_get_num_vlan(priv->ioaddr);

	return 0;
}
