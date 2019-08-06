// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 * stmmac XGMAC support.
 */

#include "stmmac.h"
#include "dwxgmac2.h"

static void dwxgmac2_core_init(struct mac_device_info *hw,
			       struct net_device *dev)
{
	void __iomem *ioaddr = hw->pcsr;
	int mtu = dev->mtu;
	u32 tx, rx;

	tx = readl(ioaddr + XGMAC_TX_CONFIG);
	rx = readl(ioaddr + XGMAC_RX_CONFIG);

	tx |= XGMAC_CORE_INIT_TX;
	rx |= XGMAC_CORE_INIT_RX;

	if (mtu >= 9000) {
		rx |= XGMAC_CONFIG_GPSLCE;
		rx |= XGMAC_JUMBO_LEN << XGMAC_CONFIG_GPSL_SHIFT;
		rx |= XGMAC_CONFIG_WD;
	} else if (mtu > 2000) {
		rx |= XGMAC_CONFIG_JE;
	} else if (mtu > 1500) {
		rx |= XGMAC_CONFIG_S2KP;
	}

	if (hw->ps) {
		tx |= XGMAC_CONFIG_TE;
		tx &= ~hw->link.speed_mask;

		switch (hw->ps) {
		case SPEED_10000:
			tx |= hw->link.speed10000;
			break;
		case SPEED_2500:
			tx |= hw->link.speed2500;
			break;
		case SPEED_1000:
		default:
			tx |= hw->link.speed1000;
			break;
		}
	}

	writel(tx, ioaddr + XGMAC_TX_CONFIG);
	writel(rx, ioaddr + XGMAC_RX_CONFIG);
	writel(XGMAC_INT_DEFAULT_EN, ioaddr + XGMAC_INT_EN);
}

static void dwxgmac2_set_mac(void __iomem *ioaddr, bool enable)
{
	u32 tx = readl(ioaddr + XGMAC_TX_CONFIG);
	u32 rx = readl(ioaddr + XGMAC_RX_CONFIG);

	if (enable) {
		tx |= XGMAC_CONFIG_TE;
		rx |= XGMAC_CONFIG_RE;
	} else {
		tx &= ~XGMAC_CONFIG_TE;
		rx &= ~XGMAC_CONFIG_RE;
	}

	writel(tx, ioaddr + XGMAC_TX_CONFIG);
	writel(rx, ioaddr + XGMAC_RX_CONFIG);
}

static int dwxgmac2_rx_ipc(struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_RX_CONFIG);
	if (hw->rx_csum)
		value |= XGMAC_CONFIG_IPC;
	else
		value &= ~XGMAC_CONFIG_IPC;
	writel(value, ioaddr + XGMAC_RX_CONFIG);

	return !!(readl(ioaddr + XGMAC_RX_CONFIG) & XGMAC_CONFIG_IPC);
}

static void dwxgmac2_rx_queue_enable(struct mac_device_info *hw, u8 mode,
				     u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_RXQ_CTRL0) & ~XGMAC_RXQEN(queue);
	if (mode == MTL_QUEUE_AVB)
		value |= 0x1 << XGMAC_RXQEN_SHIFT(queue);
	else if (mode == MTL_QUEUE_DCB)
		value |= 0x2 << XGMAC_RXQEN_SHIFT(queue);
	writel(value, ioaddr + XGMAC_RXQ_CTRL0);
}

static void dwxgmac2_rx_queue_prio(struct mac_device_info *hw, u32 prio,
				   u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value, reg;

	reg = (queue < 4) ? XGMAC_RXQ_CTRL2 : XGMAC_RXQ_CTRL3;
	if (queue >= 4)
		queue -= 4;

	value = readl(ioaddr + reg);
	value &= ~XGMAC_PSRQ(queue);
	value |= (prio << XGMAC_PSRQ_SHIFT(queue)) & XGMAC_PSRQ(queue);

	writel(value, ioaddr + reg);
}

static void dwxgmac2_prog_mtl_rx_algorithms(struct mac_device_info *hw,
					    u32 rx_alg)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_MTL_OPMODE);
	value &= ~XGMAC_RAA;

	switch (rx_alg) {
	case MTL_RX_ALGORITHM_SP:
		break;
	case MTL_RX_ALGORITHM_WSP:
		value |= XGMAC_RAA;
		break;
	default:
		break;
	}

	writel(value, ioaddr + XGMAC_MTL_OPMODE);
}

static void dwxgmac2_prog_mtl_tx_algorithms(struct mac_device_info *hw,
					    u32 tx_alg)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_MTL_OPMODE);
	value &= ~XGMAC_ETSALG;

	switch (tx_alg) {
	case MTL_TX_ALGORITHM_WRR:
		value |= XGMAC_WRR;
		break;
	case MTL_TX_ALGORITHM_WFQ:
		value |= XGMAC_WFQ;
		break;
	case MTL_TX_ALGORITHM_DWRR:
		value |= XGMAC_DWRR;
		break;
	default:
		break;
	}

	writel(value, ioaddr + XGMAC_MTL_OPMODE);
}

static void dwxgmac2_map_mtl_to_dma(struct mac_device_info *hw, u32 queue,
				    u32 chan)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value, reg;

	reg = (queue < 4) ? XGMAC_MTL_RXQ_DMA_MAP0 : XGMAC_MTL_RXQ_DMA_MAP1;
	if (queue >= 4)
		queue -= 4;

	value = readl(ioaddr + reg);
	value &= ~XGMAC_QxMDMACH(queue);
	value |= (chan << XGMAC_QxMDMACH_SHIFT(queue)) & XGMAC_QxMDMACH(queue);

	writel(value, ioaddr + reg);
}

static void dwxgmac2_config_cbs(struct mac_device_info *hw,
				u32 send_slope, u32 idle_slope,
				u32 high_credit, u32 low_credit, u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	writel(send_slope, ioaddr + XGMAC_MTL_TCx_SENDSLOPE(queue));
	writel(idle_slope, ioaddr + XGMAC_MTL_TCx_QUANTUM_WEIGHT(queue));
	writel(high_credit, ioaddr + XGMAC_MTL_TCx_HICREDIT(queue));
	writel(low_credit, ioaddr + XGMAC_MTL_TCx_LOCREDIT(queue));

	value = readl(ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(queue));
	value |= XGMAC_CC | XGMAC_CBS;
	writel(value, ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(queue));
}

static int dwxgmac2_host_irq_status(struct mac_device_info *hw,
				    struct stmmac_extra_stats *x)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 stat, en;

	en = readl(ioaddr + XGMAC_INT_EN);
	stat = readl(ioaddr + XGMAC_INT_STATUS);

	stat &= en;

	if (stat & XGMAC_PMTIS) {
		x->irq_receive_pmt_irq_n++;
		readl(ioaddr + XGMAC_PMT);
	}

	return 0;
}

static int dwxgmac2_host_mtl_irq_status(struct mac_device_info *hw, u32 chan)
{
	void __iomem *ioaddr = hw->pcsr;
	int ret = 0;
	u32 status;

	status = readl(ioaddr + XGMAC_MTL_INT_STATUS);
	if (status & BIT(chan)) {
		u32 chan_status = readl(ioaddr + XGMAC_MTL_QINT_STATUS(chan));

		if (chan_status & XGMAC_RXOVFIS)
			ret |= CORE_IRQ_MTL_RX_OVERFLOW;

		writel(~0x0, ioaddr + XGMAC_MTL_QINT_STATUS(chan));
	}

	return ret;
}

static void dwxgmac2_flow_ctrl(struct mac_device_info *hw, unsigned int duplex,
			       unsigned int fc, unsigned int pause_time,
			       u32 tx_cnt)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 i;

	if (fc & FLOW_RX)
		writel(XGMAC_RFE, ioaddr + XGMAC_RX_FLOW_CTRL);
	if (fc & FLOW_TX) {
		for (i = 0; i < tx_cnt; i++) {
			u32 value = XGMAC_TFE;

			if (duplex)
				value |= pause_time << XGMAC_PT_SHIFT;

			writel(value, ioaddr + XGMAC_Qx_TX_FLOW_CTRL(i));
		}
	}
}

static void dwxgmac2_pmt(struct mac_device_info *hw, unsigned long mode)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 val = 0x0;

	if (mode & WAKE_MAGIC)
		val |= XGMAC_PWRDWN | XGMAC_MGKPKTEN;
	if (mode & WAKE_UCAST)
		val |= XGMAC_PWRDWN | XGMAC_GLBLUCAST | XGMAC_RWKPKTEN;
	if (val) {
		u32 cfg = readl(ioaddr + XGMAC_RX_CONFIG);
		cfg |= XGMAC_CONFIG_RE;
		writel(cfg, ioaddr + XGMAC_RX_CONFIG);
	}

	writel(val, ioaddr + XGMAC_PMT);
}

static void dwxgmac2_set_umac_addr(struct mac_device_info *hw,
				   unsigned char *addr, unsigned int reg_n)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = (addr[5] << 8) | addr[4];
	writel(value | XGMAC_AE, ioaddr + XGMAC_ADDR0_HIGH);

	value = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	writel(value, ioaddr + XGMAC_ADDR0_LOW);
}

static void dwxgmac2_get_umac_addr(struct mac_device_info *hw,
				   unsigned char *addr, unsigned int reg_n)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 hi_addr, lo_addr;

	/* Read the MAC address from the hardware */
	hi_addr = readl(ioaddr + XGMAC_ADDR0_HIGH);
	lo_addr = readl(ioaddr + XGMAC_ADDR0_LOW);

	/* Extract the MAC address from the high and low words */
	addr[0] = lo_addr & 0xff;
	addr[1] = (lo_addr >> 8) & 0xff;
	addr[2] = (lo_addr >> 16) & 0xff;
	addr[3] = (lo_addr >> 24) & 0xff;
	addr[4] = hi_addr & 0xff;
	addr[5] = (hi_addr >> 8) & 0xff;
}

static void dwxgmac2_set_filter(struct mac_device_info *hw,
				struct net_device *dev)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	u32 value = XGMAC_FILTER_RA;

	if (dev->flags & IFF_PROMISC) {
		value |= XGMAC_FILTER_PR;
	} else if ((dev->flags & IFF_ALLMULTI) ||
		   (netdev_mc_count(dev) > HASH_TABLE_SIZE)) {
		value |= XGMAC_FILTER_PM;
		writel(~0x0, ioaddr + XGMAC_HASH_TABLE(0));
		writel(~0x0, ioaddr + XGMAC_HASH_TABLE(1));
	}

	writel(value, ioaddr + XGMAC_PACKET_FILTER);
}

const struct stmmac_ops dwxgmac210_ops = {
	.core_init = dwxgmac2_core_init,
	.set_mac = dwxgmac2_set_mac,
	.rx_ipc = dwxgmac2_rx_ipc,
	.rx_queue_enable = dwxgmac2_rx_queue_enable,
	.rx_queue_prio = dwxgmac2_rx_queue_prio,
	.tx_queue_prio = NULL,
	.rx_queue_routing = NULL,
	.prog_mtl_rx_algorithms = dwxgmac2_prog_mtl_rx_algorithms,
	.prog_mtl_tx_algorithms = dwxgmac2_prog_mtl_tx_algorithms,
	.set_mtl_tx_queue_weight = NULL,
	.map_mtl_to_dma = dwxgmac2_map_mtl_to_dma,
	.config_cbs = dwxgmac2_config_cbs,
	.dump_regs = NULL,
	.host_irq_status = dwxgmac2_host_irq_status,
	.host_mtl_irq_status = dwxgmac2_host_mtl_irq_status,
	.flow_ctrl = dwxgmac2_flow_ctrl,
	.pmt = dwxgmac2_pmt,
	.set_umac_addr = dwxgmac2_set_umac_addr,
	.get_umac_addr = dwxgmac2_get_umac_addr,
	.set_eee_mode = NULL,
	.reset_eee_mode = NULL,
	.set_eee_timer = NULL,
	.set_eee_pls = NULL,
	.pcs_ctrl_ane = NULL,
	.pcs_rane = NULL,
	.pcs_get_adv_lp = NULL,
	.debug = NULL,
	.set_filter = dwxgmac2_set_filter,
};

int dwxgmac2_setup(struct stmmac_priv *priv)
{
	struct mac_device_info *mac = priv->hw;

	dev_info(priv->device, "\tXGMAC2\n");

	priv->dev->priv_flags |= IFF_UNICAST_FLT;
	mac->pcsr = priv->ioaddr;
	mac->multicast_filter_bins = priv->plat->multicast_filter_bins;
	mac->unicast_filter_entries = priv->plat->unicast_filter_entries;
	mac->mcast_bits_log2 = 0;

	if (mac->multicast_filter_bins)
		mac->mcast_bits_log2 = ilog2(mac->multicast_filter_bins);

	mac->link.duplex = 0;
	mac->link.speed10 = 0;
	mac->link.speed100 = 0;
	mac->link.speed1000 = XGMAC_CONFIG_SS_1000;
	mac->link.speed2500 = XGMAC_CONFIG_SS_2500;
	mac->link.speed10000 = XGMAC_CONFIG_SS_10000;
	mac->link.speed_mask = XGMAC_CONFIG_SS_MASK;

	mac->mii.addr = XGMAC_MDIO_ADDR;
	mac->mii.data = XGMAC_MDIO_DATA;
	mac->mii.addr_shift = 16;
	mac->mii.addr_mask = GENMASK(20, 16);
	mac->mii.reg_shift = 0;
	mac->mii.reg_mask = GENMASK(15, 0);
	mac->mii.clk_csr_shift = 19;
	mac->mii.clk_csr_mask = GENMASK(21, 19);

	return 0;
}
