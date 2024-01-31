// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 * stmmac XGMAC support.
 */

#include <linux/bitrev.h>
#include <linux/crc32.h>
#include <linux/iopoll.h>
#include "stmmac.h"
#include "stmmac_ptp.h"
#include "dwxlgmac2.h"
#include "dwxgmac2.h"

static void dwxgmac2_core_init(struct mac_device_info *hw,
			       struct net_device *dev)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 tx, rx;

	tx = readl(ioaddr + XGMAC_TX_CONFIG);
	rx = readl(ioaddr + XGMAC_RX_CONFIG);

	tx |= XGMAC_CORE_INIT_TX;
	rx |= XGMAC_CORE_INIT_RX;

	if (hw->ps) {
		tx |= XGMAC_CONFIG_TE;
		tx &= ~hw->link.speed_mask;

		switch (hw->ps) {
		case SPEED_10000:
			tx |= hw->link.xgmii.speed10000;
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

static void dwxgmac2_tx_queue_prio(struct mac_device_info *hw, u32 prio,
				   u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value, reg;

	reg = (queue < 4) ? XGMAC_TC_PRTY_MAP0 : XGMAC_TC_PRTY_MAP1;
	if (queue >= 4)
		queue -= 4;

	value = readl(ioaddr + reg);
	value &= ~XGMAC_PSTC(queue);
	value |= (prio << XGMAC_PSTC_SHIFT(queue)) & XGMAC_PSTC(queue);

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
	bool ets = true;
	u32 value;
	int i;

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
		ets = false;
		break;
	}

	writel(value, ioaddr + XGMAC_MTL_OPMODE);

	/* Set ETS if desired */
	for (i = 0; i < MTL_MAX_TX_QUEUES; i++) {
		value = readl(ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(i));
		value &= ~XGMAC_TSA;
		if (ets)
			value |= XGMAC_ETS;
		writel(value, ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(i));
	}
}

static void dwxgmac2_set_mtl_tx_queue_weight(struct mac_device_info *hw,
					     u32 weight, u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;

	writel(weight, ioaddr + XGMAC_MTL_TCx_QUANTUM_WEIGHT(queue));
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
	value &= ~XGMAC_TSA;
	value |= XGMAC_CC | XGMAC_CBS;
	writel(value, ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(queue));
}

static void dwxgmac2_dump_regs(struct mac_device_info *hw, u32 *reg_space)
{
	void __iomem *ioaddr = hw->pcsr;
	int i;

	for (i = 0; i < XGMAC_MAC_REGSIZE; i++)
		reg_space[i] = readl(ioaddr + i * 4);
}

static int dwxgmac2_host_irq_status(struct mac_device_info *hw,
				    struct stmmac_extra_stats *x)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 stat, en;
	int ret = 0;

	en = readl(ioaddr + XGMAC_INT_EN);
	stat = readl(ioaddr + XGMAC_INT_STATUS);

	stat &= en;

	if (stat & XGMAC_PMTIS) {
		x->irq_receive_pmt_irq_n++;
		readl(ioaddr + XGMAC_PMT);
	}

	if (stat & XGMAC_LPIIS) {
		u32 lpi = readl(ioaddr + XGMAC_LPI_CTRL);

		if (lpi & XGMAC_TLPIEN) {
			ret |= CORE_IRQ_TX_PATH_IN_LPI_MODE;
			x->irq_tx_path_in_lpi_mode_n++;
		}
		if (lpi & XGMAC_TLPIEX) {
			ret |= CORE_IRQ_TX_PATH_EXIT_LPI_MODE;
			x->irq_tx_path_exit_lpi_mode_n++;
		}
		if (lpi & XGMAC_RLPIEN)
			x->irq_rx_path_in_lpi_mode_n++;
		if (lpi & XGMAC_RLPIEX)
			x->irq_rx_path_exit_lpi_mode_n++;
	}

	return ret;
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
				   const unsigned char *addr,
				   unsigned int reg_n)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = (addr[5] << 8) | addr[4];
	writel(value | XGMAC_AE, ioaddr + XGMAC_ADDRx_HIGH(reg_n));

	value = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	writel(value, ioaddr + XGMAC_ADDRx_LOW(reg_n));
}

static void dwxgmac2_get_umac_addr(struct mac_device_info *hw,
				   unsigned char *addr, unsigned int reg_n)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 hi_addr, lo_addr;

	/* Read the MAC address from the hardware */
	hi_addr = readl(ioaddr + XGMAC_ADDRx_HIGH(reg_n));
	lo_addr = readl(ioaddr + XGMAC_ADDRx_LOW(reg_n));

	/* Extract the MAC address from the high and low words */
	addr[0] = lo_addr & 0xff;
	addr[1] = (lo_addr >> 8) & 0xff;
	addr[2] = (lo_addr >> 16) & 0xff;
	addr[3] = (lo_addr >> 24) & 0xff;
	addr[4] = hi_addr & 0xff;
	addr[5] = (hi_addr >> 8) & 0xff;
}

static void dwxgmac2_set_eee_mode(struct mac_device_info *hw,
				  bool en_tx_lpi_clockgating)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_LPI_CTRL);

	value |= XGMAC_LPITXEN | XGMAC_LPITXA;
	if (en_tx_lpi_clockgating)
		value |= XGMAC_TXCGE;

	writel(value, ioaddr + XGMAC_LPI_CTRL);
}

static void dwxgmac2_reset_eee_mode(struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_LPI_CTRL);
	value &= ~(XGMAC_LPITXEN | XGMAC_LPITXA | XGMAC_TXCGE);
	writel(value, ioaddr + XGMAC_LPI_CTRL);
}

static void dwxgmac2_set_eee_pls(struct mac_device_info *hw, int link)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_LPI_CTRL);
	if (link)
		value |= XGMAC_PLS;
	else
		value &= ~XGMAC_PLS;
	writel(value, ioaddr + XGMAC_LPI_CTRL);
}

static void dwxgmac2_set_eee_timer(struct mac_device_info *hw, int ls, int tw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = (tw & 0xffff) | ((ls & 0x3ff) << 16);
	writel(value, ioaddr + XGMAC_LPI_TIMER_CTRL);
}

static void dwxgmac2_set_mchash(void __iomem *ioaddr, u32 *mcfilterbits,
				int mcbitslog2)
{
	int numhashregs, regs;

	switch (mcbitslog2) {
	case 6:
		numhashregs = 2;
		break;
	case 7:
		numhashregs = 4;
		break;
	case 8:
		numhashregs = 8;
		break;
	default:
		return;
	}

	for (regs = 0; regs < numhashregs; regs++)
		writel(mcfilterbits[regs], ioaddr + XGMAC_HASH_TABLE(regs));
}

static void dwxgmac2_set_filter(struct mac_device_info *hw,
				struct net_device *dev)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);
	int mcbitslog2 = hw->mcast_bits_log2;
	u32 mc_filter[8];
	int i;

	value &= ~(XGMAC_FILTER_PR | XGMAC_FILTER_HMC | XGMAC_FILTER_PM);
	value |= XGMAC_FILTER_HPF;

	memset(mc_filter, 0, sizeof(mc_filter));

	if (dev->flags & IFF_PROMISC) {
		value |= XGMAC_FILTER_PR;
		value |= XGMAC_FILTER_PCF;
	} else if ((dev->flags & IFF_ALLMULTI) ||
		   (netdev_mc_count(dev) > hw->multicast_filter_bins)) {
		value |= XGMAC_FILTER_PM;

		for (i = 0; i < XGMAC_MAX_HASH_TABLE; i++)
			writel(~0x0, ioaddr + XGMAC_HASH_TABLE(i));
	} else if (!netdev_mc_empty(dev) && (dev->flags & IFF_MULTICAST)) {
		struct netdev_hw_addr *ha;

		value |= XGMAC_FILTER_HMC;

		netdev_for_each_mc_addr(ha, dev) {
			u32 nr = (bitrev32(~crc32_le(~0, ha->addr, 6)) >>
					(32 - mcbitslog2));
			mc_filter[nr >> 5] |= (1 << (nr & 0x1F));
		}
	}

	dwxgmac2_set_mchash(ioaddr, mc_filter, mcbitslog2);

	/* Handle multiple unicast addresses */
	if (netdev_uc_count(dev) > hw->unicast_filter_entries) {
		value |= XGMAC_FILTER_PR;
	} else {
		struct netdev_hw_addr *ha;
		int reg = 1;

		netdev_for_each_uc_addr(ha, dev) {
			dwxgmac2_set_umac_addr(hw, ha->addr, reg);
			reg++;
		}

		for ( ; reg < XGMAC_ADDR_MAX; reg++) {
			writel(0, ioaddr + XGMAC_ADDRx_HIGH(reg));
			writel(0, ioaddr + XGMAC_ADDRx_LOW(reg));
		}
	}

	writel(value, ioaddr + XGMAC_PACKET_FILTER);
}

static void dwxgmac2_set_mac_loopback(void __iomem *ioaddr, bool enable)
{
	u32 value = readl(ioaddr + XGMAC_RX_CONFIG);

	if (enable)
		value |= XGMAC_CONFIG_LM;
	else
		value &= ~XGMAC_CONFIG_LM;

	writel(value, ioaddr + XGMAC_RX_CONFIG);
}

static int dwxgmac2_rss_write_reg(void __iomem *ioaddr, bool is_key, int idx,
				  u32 val)
{
	u32 ctrl = 0;

	writel(val, ioaddr + XGMAC_RSS_DATA);
	ctrl |= idx << XGMAC_RSSIA_SHIFT;
	ctrl |= is_key ? XGMAC_ADDRT : 0x0;
	ctrl |= XGMAC_OB;
	writel(ctrl, ioaddr + XGMAC_RSS_ADDR);

	return readl_poll_timeout(ioaddr + XGMAC_RSS_ADDR, ctrl,
				  !(ctrl & XGMAC_OB), 100, 10000);
}

static int dwxgmac2_rss_configure(struct mac_device_info *hw,
				  struct stmmac_rss *cfg, u32 num_rxq)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value, *key;
	int i, ret;

	value = readl(ioaddr + XGMAC_RSS_CTRL);
	if (!cfg || !cfg->enable) {
		value &= ~XGMAC_RSSE;
		writel(value, ioaddr + XGMAC_RSS_CTRL);
		return 0;
	}

	key = (u32 *)cfg->key;
	for (i = 0; i < (ARRAY_SIZE(cfg->key) / sizeof(u32)); i++) {
		ret = dwxgmac2_rss_write_reg(ioaddr, true, i, key[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(cfg->table); i++) {
		ret = dwxgmac2_rss_write_reg(ioaddr, false, i, cfg->table[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < num_rxq; i++)
		dwxgmac2_map_mtl_to_dma(hw, i, XGMAC_QDDMACH);

	value |= XGMAC_UDP4TE | XGMAC_TCP4TE | XGMAC_IP2TE | XGMAC_RSSE;
	writel(value, ioaddr + XGMAC_RSS_CTRL);
	return 0;
}

static void dwxgmac2_update_vlan_hash(struct mac_device_info *hw, u32 hash,
				      __le16 perfect_match, bool is_double)
{
	void __iomem *ioaddr = hw->pcsr;

	writel(hash, ioaddr + XGMAC_VLAN_HASH_TABLE);

	if (hash) {
		u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value |= XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = readl(ioaddr + XGMAC_VLAN_TAG);

		value |= XGMAC_VLAN_VTHM | XGMAC_VLAN_ETV;
		if (is_double) {
			value |= XGMAC_VLAN_EDVLP;
			value |= XGMAC_VLAN_ESVL;
			value |= XGMAC_VLAN_DOVLTC;
		} else {
			value &= ~XGMAC_VLAN_EDVLP;
			value &= ~XGMAC_VLAN_ESVL;
			value &= ~XGMAC_VLAN_DOVLTC;
		}

		value &= ~XGMAC_VLAN_VID;
		writel(value, ioaddr + XGMAC_VLAN_TAG);
	} else if (perfect_match) {
		u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value |= XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = readl(ioaddr + XGMAC_VLAN_TAG);

		value &= ~XGMAC_VLAN_VTHM;
		value |= XGMAC_VLAN_ETV;
		if (is_double) {
			value |= XGMAC_VLAN_EDVLP;
			value |= XGMAC_VLAN_ESVL;
			value |= XGMAC_VLAN_DOVLTC;
		} else {
			value &= ~XGMAC_VLAN_EDVLP;
			value &= ~XGMAC_VLAN_ESVL;
			value &= ~XGMAC_VLAN_DOVLTC;
		}

		value &= ~XGMAC_VLAN_VID;
		writel(value | perfect_match, ioaddr + XGMAC_VLAN_TAG);
	} else {
		u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value &= ~XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = readl(ioaddr + XGMAC_VLAN_TAG);

		value &= ~(XGMAC_VLAN_VTHM | XGMAC_VLAN_ETV);
		value &= ~(XGMAC_VLAN_EDVLP | XGMAC_VLAN_ESVL);
		value &= ~XGMAC_VLAN_DOVLTC;
		value &= ~XGMAC_VLAN_VID;

		writel(value, ioaddr + XGMAC_VLAN_TAG);
	}
}

struct dwxgmac3_error_desc {
	bool valid;
	const char *desc;
	const char *detailed_desc;
};

#define STAT_OFF(field)		offsetof(struct stmmac_safety_stats, field)

static void dwxgmac3_log_error(struct net_device *ndev, u32 value, bool corr,
			       const char *module_name,
			       const struct dwxgmac3_error_desc *desc,
			       unsigned long field_offset,
			       struct stmmac_safety_stats *stats)
{
	unsigned long loc, mask;
	u8 *bptr = (u8 *)stats;
	unsigned long *ptr;

	ptr = (unsigned long *)(bptr + field_offset);

	mask = value;
	for_each_set_bit(loc, &mask, 32) {
		netdev_err(ndev, "Found %s error in %s: '%s: %s'\n", corr ?
				"correctable" : "uncorrectable", module_name,
				desc[loc].desc, desc[loc].detailed_desc);

		/* Update counters */
		ptr[loc]++;
	}
}

static const struct dwxgmac3_error_desc dwxgmac3_mac_errors[32]= {
	{ true, "ATPES", "Application Transmit Interface Parity Check Error" },
	{ true, "DPES", "Descriptor Cache Data Path Parity Check Error" },
	{ true, "TPES", "TSO Data Path Parity Check Error" },
	{ true, "TSOPES", "TSO Header Data Path Parity Check Error" },
	{ true, "MTPES", "MTL Data Path Parity Check Error" },
	{ true, "MTSPES", "MTL TX Status Data Path Parity Check Error" },
	{ true, "MTBUPES", "MAC TBU Data Path Parity Check Error" },
	{ true, "MTFCPES", "MAC TFC Data Path Parity Check Error" },
	{ true, "ARPES", "Application Receive Interface Data Path Parity Check Error" },
	{ true, "MRWCPES", "MTL RWC Data Path Parity Check Error" },
	{ true, "MRRCPES", "MTL RCC Data Path Parity Check Error" },
	{ true, "CWPES", "CSR Write Data Path Parity Check Error" },
	{ true, "ASRPES", "AXI Slave Read Data Path Parity Check Error" },
	{ true, "TTES", "TX FSM Timeout Error" },
	{ true, "RTES", "RX FSM Timeout Error" },
	{ true, "CTES", "CSR FSM Timeout Error" },
	{ true, "ATES", "APP FSM Timeout Error" },
	{ true, "PTES", "PTP FSM Timeout Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 18 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 19 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 20 */
	{ true, "MSTTES", "Master Read/Write Timeout Error" },
	{ true, "SLVTES", "Slave Read/Write Timeout Error" },
	{ true, "ATITES", "Application Timeout on ATI Interface Error" },
	{ true, "ARITES", "Application Timeout on ARI Interface Error" },
	{ true, "FSMPES", "FSM State Parity Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 26 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 27 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 28 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 29 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 30 */
	{ true, "CPI", "Control Register Parity Check Error" },
};

static void dwxgmac3_handle_mac_err(struct net_device *ndev,
				    void __iomem *ioaddr, bool correctable,
				    struct stmmac_safety_stats *stats)
{
	u32 value;

	value = readl(ioaddr + XGMAC_MAC_DPP_FSM_INT_STATUS);
	writel(value, ioaddr + XGMAC_MAC_DPP_FSM_INT_STATUS);

	dwxgmac3_log_error(ndev, value, correctable, "MAC",
			   dwxgmac3_mac_errors, STAT_OFF(mac_errors), stats);
}

static const struct dwxgmac3_error_desc dwxgmac3_mtl_errors[32]= {
	{ true, "TXCES", "MTL TX Memory Error" },
	{ true, "TXAMS", "MTL TX Memory Address Mismatch Error" },
	{ true, "TXUES", "MTL TX Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 3 */
	{ true, "RXCES", "MTL RX Memory Error" },
	{ true, "RXAMS", "MTL RX Memory Address Mismatch Error" },
	{ true, "RXUES", "MTL RX Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 7 */
	{ true, "ECES", "MTL EST Memory Error" },
	{ true, "EAMS", "MTL EST Memory Address Mismatch Error" },
	{ true, "EUES", "MTL EST Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 11 */
	{ true, "RPCES", "MTL RX Parser Memory Error" },
	{ true, "RPAMS", "MTL RX Parser Memory Address Mismatch Error" },
	{ true, "RPUES", "MTL RX Parser Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 15 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 16 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 17 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 18 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 19 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 20 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 21 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 22 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 23 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 24 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 25 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 26 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 27 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 28 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 29 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 30 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 31 */
};

static void dwxgmac3_handle_mtl_err(struct net_device *ndev,
				    void __iomem *ioaddr, bool correctable,
				    struct stmmac_safety_stats *stats)
{
	u32 value;

	value = readl(ioaddr + XGMAC_MTL_ECC_INT_STATUS);
	writel(value, ioaddr + XGMAC_MTL_ECC_INT_STATUS);

	dwxgmac3_log_error(ndev, value, correctable, "MTL",
			   dwxgmac3_mtl_errors, STAT_OFF(mtl_errors), stats);
}

static const struct dwxgmac3_error_desc dwxgmac3_dma_errors[32]= {
	{ true, "TCES", "DMA TSO Memory Error" },
	{ true, "TAMS", "DMA TSO Memory Address Mismatch Error" },
	{ true, "TUES", "DMA TSO Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 3 */
	{ true, "DCES", "DMA DCACHE Memory Error" },
	{ true, "DAMS", "DMA DCACHE Address Mismatch Error" },
	{ true, "DUES", "DMA DCACHE Memory Error" },
	{ false, "UNKNOWN", "Unknown Error" }, /* 7 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 8 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 9 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 10 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 11 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 12 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 13 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 14 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 15 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 16 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 17 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 18 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 19 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 20 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 21 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 22 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 23 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 24 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 25 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 26 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 27 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 28 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 29 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 30 */
	{ false, "UNKNOWN", "Unknown Error" }, /* 31 */
};

static const char * const dpp_rx_err = "Read Rx Descriptor Parity checker Error";
static const char * const dpp_tx_err = "Read Tx Descriptor Parity checker Error";
static const struct dwxgmac3_error_desc dwxgmac3_dma_dpp_errors[32] = {
	{ true, "TDPES0", dpp_tx_err },
	{ true, "TDPES1", dpp_tx_err },
	{ true, "TDPES2", dpp_tx_err },
	{ true, "TDPES3", dpp_tx_err },
	{ true, "TDPES4", dpp_tx_err },
	{ true, "TDPES5", dpp_tx_err },
	{ true, "TDPES6", dpp_tx_err },
	{ true, "TDPES7", dpp_tx_err },
	{ true, "TDPES8", dpp_tx_err },
	{ true, "TDPES9", dpp_tx_err },
	{ true, "TDPES10", dpp_tx_err },
	{ true, "TDPES11", dpp_tx_err },
	{ true, "TDPES12", dpp_tx_err },
	{ true, "TDPES13", dpp_tx_err },
	{ true, "TDPES14", dpp_tx_err },
	{ true, "TDPES15", dpp_tx_err },
	{ true, "RDPES0", dpp_rx_err },
	{ true, "RDPES1", dpp_rx_err },
	{ true, "RDPES2", dpp_rx_err },
	{ true, "RDPES3", dpp_rx_err },
	{ true, "RDPES4", dpp_rx_err },
	{ true, "RDPES5", dpp_rx_err },
	{ true, "RDPES6", dpp_rx_err },
	{ true, "RDPES7", dpp_rx_err },
	{ true, "RDPES8", dpp_rx_err },
	{ true, "RDPES9", dpp_rx_err },
	{ true, "RDPES10", dpp_rx_err },
	{ true, "RDPES11", dpp_rx_err },
	{ true, "RDPES12", dpp_rx_err },
	{ true, "RDPES13", dpp_rx_err },
	{ true, "RDPES14", dpp_rx_err },
	{ true, "RDPES15", dpp_rx_err },
};

static void dwxgmac3_handle_dma_err(struct net_device *ndev,
				    void __iomem *ioaddr, bool correctable,
				    struct stmmac_safety_stats *stats)
{
	u32 value;

	value = readl(ioaddr + XGMAC_DMA_ECC_INT_STATUS);
	writel(value, ioaddr + XGMAC_DMA_ECC_INT_STATUS);

	dwxgmac3_log_error(ndev, value, correctable, "DMA",
			   dwxgmac3_dma_errors, STAT_OFF(dma_errors), stats);

	value = readl(ioaddr + XGMAC_DMA_DPP_INT_STATUS);
	writel(value, ioaddr + XGMAC_DMA_DPP_INT_STATUS);

	dwxgmac3_log_error(ndev, value, false, "DMA_DPP",
			   dwxgmac3_dma_dpp_errors,
			   STAT_OFF(dma_dpp_errors), stats);
}

static int
dwxgmac3_safety_feat_config(void __iomem *ioaddr, unsigned int asp,
			    struct stmmac_safety_feature_cfg *safety_cfg)
{
	u32 value;

	if (!asp)
		return -EINVAL;

	/* 1. Enable Safety Features */
	writel(0x0, ioaddr + XGMAC_MTL_ECC_CONTROL);

	/* 2. Enable MTL Safety Interrupts */
	value = readl(ioaddr + XGMAC_MTL_ECC_INT_ENABLE);
	value |= XGMAC_RPCEIE; /* RX Parser Memory Correctable Error */
	value |= XGMAC_ECEIE; /* EST Memory Correctable Error */
	value |= XGMAC_RXCEIE; /* RX Memory Correctable Error */
	value |= XGMAC_TXCEIE; /* TX Memory Correctable Error */
	writel(value, ioaddr + XGMAC_MTL_ECC_INT_ENABLE);

	/* 3. Enable DMA Safety Interrupts */
	value = readl(ioaddr + XGMAC_DMA_ECC_INT_ENABLE);
	value |= XGMAC_DCEIE; /* Descriptor Cache Memory Correctable Error */
	value |= XGMAC_TCEIE; /* TSO Memory Correctable Error */
	writel(value, ioaddr + XGMAC_DMA_ECC_INT_ENABLE);

	/* Only ECC Protection for External Memory feature is selected */
	if (asp <= 0x1)
		return 0;

	/* 4. Enable Parity and Timeout for FSM */
	value = readl(ioaddr + XGMAC_MAC_FSM_CONTROL);
	value |= XGMAC_PRTYEN; /* FSM Parity Feature */
	value |= XGMAC_TMOUTEN; /* FSM Timeout Feature */
	writel(value, ioaddr + XGMAC_MAC_FSM_CONTROL);

	/* 5. Enable Data Path Parity Protection */
	value = readl(ioaddr + XGMAC_MTL_DPP_CONTROL);
	/* already enabled by default, explicit enable it again */
	value &= ~XGMAC_DDPP_DISABLE;
	writel(value, ioaddr + XGMAC_MTL_DPP_CONTROL);

	return 0;
}

static int dwxgmac3_safety_feat_irq_status(struct net_device *ndev,
					   void __iomem *ioaddr,
					   unsigned int asp,
					   struct stmmac_safety_stats *stats)
{
	bool err, corr;
	u32 mtl, dma;
	int ret = 0;

	if (!asp)
		return -EINVAL;

	mtl = readl(ioaddr + XGMAC_MTL_SAFETY_INT_STATUS);
	dma = readl(ioaddr + XGMAC_DMA_SAFETY_INT_STATUS);

	err = (mtl & XGMAC_MCSIS) || (dma & XGMAC_MCSIS);
	corr = false;
	if (err) {
		dwxgmac3_handle_mac_err(ndev, ioaddr, corr, stats);
		ret |= !corr;
	}

	err = (mtl & (XGMAC_MEUIS | XGMAC_MECIS)) ||
	      (dma & (XGMAC_MSUIS | XGMAC_MSCIS));
	corr = (mtl & XGMAC_MECIS) || (dma & XGMAC_MSCIS);
	if (err) {
		dwxgmac3_handle_mtl_err(ndev, ioaddr, corr, stats);
		ret |= !corr;
	}

	/* DMA_DPP_Interrupt_Status is indicated by MCSIS bit in
	 * DMA_Safety_Interrupt_Status, so we handle DMA Data Path
	 * Parity Errors here
	 */
	err = dma & (XGMAC_DEUIS | XGMAC_DECIS | XGMAC_MCSIS);
	corr = dma & XGMAC_DECIS;
	if (err) {
		dwxgmac3_handle_dma_err(ndev, ioaddr, corr, stats);
		ret |= !corr;
	}

	return ret;
}

static const struct dwxgmac3_error {
	const struct dwxgmac3_error_desc *desc;
} dwxgmac3_all_errors[] = {
	{ dwxgmac3_mac_errors },
	{ dwxgmac3_mtl_errors },
	{ dwxgmac3_dma_errors },
	{ dwxgmac3_dma_dpp_errors },
};

static int dwxgmac3_safety_feat_dump(struct stmmac_safety_stats *stats,
				     int index, unsigned long *count,
				     const char **desc)
{
	int module = index / 32, offset = index % 32;
	unsigned long *ptr = (unsigned long *)stats;

	if (module >= ARRAY_SIZE(dwxgmac3_all_errors))
		return -EINVAL;
	if (!dwxgmac3_all_errors[module].desc[offset].valid)
		return -EINVAL;
	if (count)
		*count = *(ptr + index);
	if (desc)
		*desc = dwxgmac3_all_errors[module].desc[offset].desc;
	return 0;
}

static int dwxgmac3_rxp_disable(void __iomem *ioaddr)
{
	u32 val = readl(ioaddr + XGMAC_MTL_OPMODE);

	val &= ~XGMAC_FRPE;
	writel(val, ioaddr + XGMAC_MTL_OPMODE);

	return 0;
}

static void dwxgmac3_rxp_enable(void __iomem *ioaddr)
{
	u32 val;

	val = readl(ioaddr + XGMAC_MTL_OPMODE);
	val |= XGMAC_FRPE;
	writel(val, ioaddr + XGMAC_MTL_OPMODE);
}

static int dwxgmac3_rxp_update_single_entry(void __iomem *ioaddr,
					    struct stmmac_tc_entry *entry,
					    int pos)
{
	int ret, i;

	for (i = 0; i < (sizeof(entry->val) / sizeof(u32)); i++) {
		int real_pos = pos * (sizeof(entry->val) / sizeof(u32)) + i;
		u32 val;

		/* Wait for ready */
		ret = readl_poll_timeout(ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST,
					 val, !(val & XGMAC_STARTBUSY), 1, 10000);
		if (ret)
			return ret;

		/* Write data */
		val = *((u32 *)&entry->val + i);
		writel(val, ioaddr + XGMAC_MTL_RXP_IACC_DATA);

		/* Write pos */
		val = real_pos & XGMAC_ADDR;
		writel(val, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);

		/* Write OP */
		val |= XGMAC_WRRDN;
		writel(val, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);

		/* Start Write */
		val |= XGMAC_STARTBUSY;
		writel(val, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);

		/* Wait for done */
		ret = readl_poll_timeout(ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST,
					 val, !(val & XGMAC_STARTBUSY), 1, 10000);
		if (ret)
			return ret;
	}

	return 0;
}

static struct stmmac_tc_entry *
dwxgmac3_rxp_get_next_entry(struct stmmac_tc_entry *entries,
			    unsigned int count, u32 curr_prio)
{
	struct stmmac_tc_entry *entry;
	u32 min_prio = ~0x0;
	int i, min_prio_idx;
	bool found = false;

	for (i = count - 1; i >= 0; i--) {
		entry = &entries[i];

		/* Do not update unused entries */
		if (!entry->in_use)
			continue;
		/* Do not update already updated entries (i.e. fragments) */
		if (entry->in_hw)
			continue;
		/* Let last entry be updated last */
		if (entry->is_last)
			continue;
		/* Do not return fragments */
		if (entry->is_frag)
			continue;
		/* Check if we already checked this prio */
		if (entry->prio < curr_prio)
			continue;
		/* Check if this is the minimum prio */
		if (entry->prio < min_prio) {
			min_prio = entry->prio;
			min_prio_idx = i;
			found = true;
		}
	}

	if (found)
		return &entries[min_prio_idx];
	return NULL;
}

static int dwxgmac3_rxp_config(void __iomem *ioaddr,
			       struct stmmac_tc_entry *entries,
			       unsigned int count)
{
	struct stmmac_tc_entry *entry, *frag;
	int i, ret, nve = 0;
	u32 curr_prio = 0;
	u32 old_val, val;

	/* Force disable RX */
	old_val = readl(ioaddr + XGMAC_RX_CONFIG);
	val = old_val & ~XGMAC_CONFIG_RE;
	writel(val, ioaddr + XGMAC_RX_CONFIG);

	/* Disable RX Parser */
	ret = dwxgmac3_rxp_disable(ioaddr);
	if (ret)
		goto re_enable;

	/* Set all entries as NOT in HW */
	for (i = 0; i < count; i++) {
		entry = &entries[i];
		entry->in_hw = false;
	}

	/* Update entries by reverse order */
	while (1) {
		entry = dwxgmac3_rxp_get_next_entry(entries, count, curr_prio);
		if (!entry)
			break;

		curr_prio = entry->prio;
		frag = entry->frag_ptr;

		/* Set special fragment requirements */
		if (frag) {
			entry->val.af = 0;
			entry->val.rf = 0;
			entry->val.nc = 1;
			entry->val.ok_index = nve + 2;
		}

		ret = dwxgmac3_rxp_update_single_entry(ioaddr, entry, nve);
		if (ret)
			goto re_enable;

		entry->table_pos = nve++;
		entry->in_hw = true;

		if (frag && !frag->in_hw) {
			ret = dwxgmac3_rxp_update_single_entry(ioaddr, frag, nve);
			if (ret)
				goto re_enable;
			frag->table_pos = nve++;
			frag->in_hw = true;
		}
	}

	if (!nve)
		goto re_enable;

	/* Update all pass entry */
	for (i = 0; i < count; i++) {
		entry = &entries[i];
		if (!entry->is_last)
			continue;

		ret = dwxgmac3_rxp_update_single_entry(ioaddr, entry, nve);
		if (ret)
			goto re_enable;

		entry->table_pos = nve++;
	}

	/* Assume n. of parsable entries == n. of valid entries */
	val = (nve << 16) & XGMAC_NPE;
	val |= nve & XGMAC_NVE;
	writel(val, ioaddr + XGMAC_MTL_RXP_CONTROL_STATUS);

	/* Enable RX Parser */
	dwxgmac3_rxp_enable(ioaddr);

re_enable:
	/* Re-enable RX */
	writel(old_val, ioaddr + XGMAC_RX_CONFIG);
	return ret;
}

static int dwxgmac2_get_mac_tx_timestamp(struct mac_device_info *hw, u64 *ts)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	if (readl_poll_timeout_atomic(ioaddr + XGMAC_TIMESTAMP_STATUS,
				      value, value & XGMAC_TXTSC, 100, 10000))
		return -EBUSY;

	*ts = readl(ioaddr + XGMAC_TXTIMESTAMP_NSEC) & XGMAC_TXTSSTSLO;
	*ts += readl(ioaddr + XGMAC_TXTIMESTAMP_SEC) * 1000000000ULL;
	return 0;
}

static int dwxgmac2_flex_pps_config(void __iomem *ioaddr, int index,
				    struct stmmac_pps_cfg *cfg, bool enable,
				    u32 sub_second_inc, u32 systime_flags)
{
	u32 tnsec = readl(ioaddr + XGMAC_PPSx_TARGET_TIME_NSEC(index));
	u32 val = readl(ioaddr + XGMAC_PPS_CONTROL);
	u64 period;

	if (!cfg->available)
		return -EINVAL;
	if (tnsec & XGMAC_TRGTBUSY0)
		return -EBUSY;
	if (!sub_second_inc || !systime_flags)
		return -EINVAL;

	val &= ~XGMAC_PPSx_MASK(index);

	if (!enable) {
		val |= XGMAC_PPSCMDx(index, XGMAC_PPSCMD_STOP);
		writel(val, ioaddr + XGMAC_PPS_CONTROL);
		return 0;
	}

	val |= XGMAC_PPSCMDx(index, XGMAC_PPSCMD_START);
	val |= XGMAC_TRGTMODSELx(index, XGMAC_PPSCMD_START);

	/* XGMAC Core has 4 PPS outputs at most.
	 *
	 * Prior XGMAC Core 3.20, Fixed mode or Flexible mode are selectable for
	 * PPS0 only via PPSEN0. PPS{1,2,3} are in Flexible mode by default,
	 * and can not be switched to Fixed mode, since PPSEN{1,2,3} are
	 * read-only reserved to 0.
	 * But we always set PPSEN{1,2,3} do not make things worse ;-)
	 *
	 * From XGMAC Core 3.20 and later, PPSEN{0,1,2,3} are writable and must
	 * be set, or the PPS outputs stay in Fixed PPS mode by default.
	 */
	val |= XGMAC_PPSENx(index);

	writel(cfg->start.tv_sec, ioaddr + XGMAC_PPSx_TARGET_TIME_SEC(index));

	if (!(systime_flags & PTP_TCR_TSCTRLSSR))
		cfg->start.tv_nsec = (cfg->start.tv_nsec * 1000) / 465;
	writel(cfg->start.tv_nsec, ioaddr + XGMAC_PPSx_TARGET_TIME_NSEC(index));

	period = cfg->period.tv_sec * 1000000000;
	period += cfg->period.tv_nsec;

	do_div(period, sub_second_inc);

	if (period <= 1)
		return -EINVAL;

	writel(period - 1, ioaddr + XGMAC_PPSx_INTERVAL(index));

	period >>= 1;
	if (period <= 1)
		return -EINVAL;

	writel(period - 1, ioaddr + XGMAC_PPSx_WIDTH(index));

	/* Finally, activate it */
	writel(val, ioaddr + XGMAC_PPS_CONTROL);
	return 0;
}

static void dwxgmac2_sarc_configure(void __iomem *ioaddr, int val)
{
	u32 value = readl(ioaddr + XGMAC_TX_CONFIG);

	value &= ~XGMAC_CONFIG_SARC;
	value |= val << XGMAC_CONFIG_SARC_SHIFT;

	writel(value, ioaddr + XGMAC_TX_CONFIG);
}

static void dwxgmac2_enable_vlan(struct mac_device_info *hw, u32 type)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_VLAN_INCL);
	value |= XGMAC_VLAN_VLTI;
	value |= XGMAC_VLAN_CSVL; /* Only use SVLAN */
	value &= ~XGMAC_VLAN_VLC;
	value |= (type << XGMAC_VLAN_VLC_SHIFT) & XGMAC_VLAN_VLC;
	writel(value, ioaddr + XGMAC_VLAN_INCL);
}

static int dwxgmac2_filter_wait(struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	if (readl_poll_timeout(ioaddr + XGMAC_L3L4_ADDR_CTRL, value,
			       !(value & XGMAC_XB), 100, 10000))
		return -EBUSY;
	return 0;
}

static int dwxgmac2_filter_read(struct mac_device_info *hw, u32 filter_no,
				u8 reg, u32 *data)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	int ret;

	ret = dwxgmac2_filter_wait(hw);
	if (ret)
		return ret;

	value = ((filter_no << XGMAC_IDDR_FNUM) | reg) << XGMAC_IDDR_SHIFT;
	value |= XGMAC_TT | XGMAC_XB;
	writel(value, ioaddr + XGMAC_L3L4_ADDR_CTRL);

	ret = dwxgmac2_filter_wait(hw);
	if (ret)
		return ret;

	*data = readl(ioaddr + XGMAC_L3L4_DATA);
	return 0;
}

static int dwxgmac2_filter_write(struct mac_device_info *hw, u32 filter_no,
				 u8 reg, u32 data)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	int ret;

	ret = dwxgmac2_filter_wait(hw);
	if (ret)
		return ret;

	writel(data, ioaddr + XGMAC_L3L4_DATA);

	value = ((filter_no << XGMAC_IDDR_FNUM) | reg) << XGMAC_IDDR_SHIFT;
	value |= XGMAC_XB;
	writel(value, ioaddr + XGMAC_L3L4_ADDR_CTRL);

	return dwxgmac2_filter_wait(hw);
}

static int dwxgmac2_config_l3_filter(struct mac_device_info *hw, u32 filter_no,
				     bool en, bool ipv6, bool sa, bool inv,
				     u32 match)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	int ret;

	value = readl(ioaddr + XGMAC_PACKET_FILTER);
	value |= XGMAC_FILTER_IPFE;
	writel(value, ioaddr + XGMAC_PACKET_FILTER);

	ret = dwxgmac2_filter_read(hw, filter_no, XGMAC_L3L4_CTRL, &value);
	if (ret)
		return ret;

	/* For IPv6 not both SA/DA filters can be active */
	if (ipv6) {
		value |= XGMAC_L3PEN0;
		value &= ~(XGMAC_L3SAM0 | XGMAC_L3SAIM0);
		value &= ~(XGMAC_L3DAM0 | XGMAC_L3DAIM0);
		if (sa) {
			value |= XGMAC_L3SAM0;
			if (inv)
				value |= XGMAC_L3SAIM0;
		} else {
			value |= XGMAC_L3DAM0;
			if (inv)
				value |= XGMAC_L3DAIM0;
		}
	} else {
		value &= ~XGMAC_L3PEN0;
		if (sa) {
			value |= XGMAC_L3SAM0;
			if (inv)
				value |= XGMAC_L3SAIM0;
		} else {
			value |= XGMAC_L3DAM0;
			if (inv)
				value |= XGMAC_L3DAIM0;
		}
	}

	ret = dwxgmac2_filter_write(hw, filter_no, XGMAC_L3L4_CTRL, value);
	if (ret)
		return ret;

	if (sa) {
		ret = dwxgmac2_filter_write(hw, filter_no, XGMAC_L3_ADDR0, match);
		if (ret)
			return ret;
	} else {
		ret = dwxgmac2_filter_write(hw, filter_no, XGMAC_L3_ADDR1, match);
		if (ret)
			return ret;
	}

	if (!en)
		return dwxgmac2_filter_write(hw, filter_no, XGMAC_L3L4_CTRL, 0);

	return 0;
}

static int dwxgmac2_config_l4_filter(struct mac_device_info *hw, u32 filter_no,
				     bool en, bool udp, bool sa, bool inv,
				     u32 match)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	int ret;

	value = readl(ioaddr + XGMAC_PACKET_FILTER);
	value |= XGMAC_FILTER_IPFE;
	writel(value, ioaddr + XGMAC_PACKET_FILTER);

	ret = dwxgmac2_filter_read(hw, filter_no, XGMAC_L3L4_CTRL, &value);
	if (ret)
		return ret;

	if (udp) {
		value |= XGMAC_L4PEN0;
	} else {
		value &= ~XGMAC_L4PEN0;
	}

	value &= ~(XGMAC_L4SPM0 | XGMAC_L4SPIM0);
	value &= ~(XGMAC_L4DPM0 | XGMAC_L4DPIM0);
	if (sa) {
		value |= XGMAC_L4SPM0;
		if (inv)
			value |= XGMAC_L4SPIM0;
	} else {
		value |= XGMAC_L4DPM0;
		if (inv)
			value |= XGMAC_L4DPIM0;
	}

	ret = dwxgmac2_filter_write(hw, filter_no, XGMAC_L3L4_CTRL, value);
	if (ret)
		return ret;

	if (sa) {
		value = match & XGMAC_L4SP0;

		ret = dwxgmac2_filter_write(hw, filter_no, XGMAC_L4_ADDR, value);
		if (ret)
			return ret;
	} else {
		value = (match << XGMAC_L4DP0_SHIFT) & XGMAC_L4DP0;

		ret = dwxgmac2_filter_write(hw, filter_no, XGMAC_L4_ADDR, value);
		if (ret)
			return ret;
	}

	if (!en)
		return dwxgmac2_filter_write(hw, filter_no, XGMAC_L3L4_CTRL, 0);

	return 0;
}

static void dwxgmac2_set_arp_offload(struct mac_device_info *hw, bool en,
				     u32 addr)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	writel(addr, ioaddr + XGMAC_ARP_ADDR);

	value = readl(ioaddr + XGMAC_RX_CONFIG);
	if (en)
		value |= XGMAC_CONFIG_ARPEN;
	else
		value &= ~XGMAC_CONFIG_ARPEN;
	writel(value, ioaddr + XGMAC_RX_CONFIG);
}

static int dwxgmac3_est_write(void __iomem *ioaddr, u32 reg, u32 val, bool gcl)
{
	u32 ctrl;

	writel(val, ioaddr + XGMAC_MTL_EST_GCL_DATA);

	ctrl = (reg << XGMAC_ADDR_SHIFT);
	ctrl |= gcl ? 0 : XGMAC_GCRR;

	writel(ctrl, ioaddr + XGMAC_MTL_EST_GCL_CONTROL);

	ctrl |= XGMAC_SRWO;
	writel(ctrl, ioaddr + XGMAC_MTL_EST_GCL_CONTROL);

	return readl_poll_timeout_atomic(ioaddr + XGMAC_MTL_EST_GCL_CONTROL,
					 ctrl, !(ctrl & XGMAC_SRWO), 100, 5000);
}

static int dwxgmac3_est_configure(void __iomem *ioaddr, struct stmmac_est *cfg,
				  unsigned int ptp_rate)
{
	int i, ret = 0x0;
	u32 ctrl;

	ret |= dwxgmac3_est_write(ioaddr, XGMAC_BTR_LOW, cfg->btr[0], false);
	ret |= dwxgmac3_est_write(ioaddr, XGMAC_BTR_HIGH, cfg->btr[1], false);
	ret |= dwxgmac3_est_write(ioaddr, XGMAC_TER, cfg->ter, false);
	ret |= dwxgmac3_est_write(ioaddr, XGMAC_LLR, cfg->gcl_size, false);
	ret |= dwxgmac3_est_write(ioaddr, XGMAC_CTR_LOW, cfg->ctr[0], false);
	ret |= dwxgmac3_est_write(ioaddr, XGMAC_CTR_HIGH, cfg->ctr[1], false);
	if (ret)
		return ret;

	for (i = 0; i < cfg->gcl_size; i++) {
		ret = dwxgmac3_est_write(ioaddr, i, cfg->gcl[i], true);
		if (ret)
			return ret;
	}

	ctrl = readl(ioaddr + XGMAC_MTL_EST_CONTROL);
	ctrl &= ~XGMAC_PTOV;
	ctrl |= ((1000000000 / ptp_rate) * 9) << XGMAC_PTOV_SHIFT;
	if (cfg->enable)
		ctrl |= XGMAC_EEST | XGMAC_SSWL;
	else
		ctrl &= ~XGMAC_EEST;

	writel(ctrl, ioaddr + XGMAC_MTL_EST_CONTROL);
	return 0;
}

static void dwxgmac3_fpe_configure(void __iomem *ioaddr, struct stmmac_fpe_cfg *cfg,
				   u32 num_txq,
				   u32 num_rxq, bool enable)
{
	u32 value;

	if (!enable) {
		value = readl(ioaddr + XGMAC_FPE_CTRL_STS);

		value &= ~XGMAC_EFPE;

		writel(value, ioaddr + XGMAC_FPE_CTRL_STS);
		return;
	}

	value = readl(ioaddr + XGMAC_RXQ_CTRL1);
	value &= ~XGMAC_RQ;
	value |= (num_rxq - 1) << XGMAC_RQ_SHIFT;
	writel(value, ioaddr + XGMAC_RXQ_CTRL1);

	value = readl(ioaddr + XGMAC_FPE_CTRL_STS);
	value |= XGMAC_EFPE;
	writel(value, ioaddr + XGMAC_FPE_CTRL_STS);
}

const struct stmmac_ops dwxgmac210_ops = {
	.core_init = dwxgmac2_core_init,
	.set_mac = dwxgmac2_set_mac,
	.rx_ipc = dwxgmac2_rx_ipc,
	.rx_queue_enable = dwxgmac2_rx_queue_enable,
	.rx_queue_prio = dwxgmac2_rx_queue_prio,
	.tx_queue_prio = dwxgmac2_tx_queue_prio,
	.rx_queue_routing = NULL,
	.prog_mtl_rx_algorithms = dwxgmac2_prog_mtl_rx_algorithms,
	.prog_mtl_tx_algorithms = dwxgmac2_prog_mtl_tx_algorithms,
	.set_mtl_tx_queue_weight = dwxgmac2_set_mtl_tx_queue_weight,
	.map_mtl_to_dma = dwxgmac2_map_mtl_to_dma,
	.config_cbs = dwxgmac2_config_cbs,
	.dump_regs = dwxgmac2_dump_regs,
	.host_irq_status = dwxgmac2_host_irq_status,
	.host_mtl_irq_status = dwxgmac2_host_mtl_irq_status,
	.flow_ctrl = dwxgmac2_flow_ctrl,
	.pmt = dwxgmac2_pmt,
	.set_umac_addr = dwxgmac2_set_umac_addr,
	.get_umac_addr = dwxgmac2_get_umac_addr,
	.set_eee_mode = dwxgmac2_set_eee_mode,
	.reset_eee_mode = dwxgmac2_reset_eee_mode,
	.set_eee_timer = dwxgmac2_set_eee_timer,
	.set_eee_pls = dwxgmac2_set_eee_pls,
	.pcs_ctrl_ane = NULL,
	.pcs_rane = NULL,
	.pcs_get_adv_lp = NULL,
	.debug = NULL,
	.set_filter = dwxgmac2_set_filter,
	.safety_feat_config = dwxgmac3_safety_feat_config,
	.safety_feat_irq_status = dwxgmac3_safety_feat_irq_status,
	.safety_feat_dump = dwxgmac3_safety_feat_dump,
	.set_mac_loopback = dwxgmac2_set_mac_loopback,
	.rss_configure = dwxgmac2_rss_configure,
	.update_vlan_hash = dwxgmac2_update_vlan_hash,
	.rxp_config = dwxgmac3_rxp_config,
	.get_mac_tx_timestamp = dwxgmac2_get_mac_tx_timestamp,
	.flex_pps_config = dwxgmac2_flex_pps_config,
	.sarc_configure = dwxgmac2_sarc_configure,
	.enable_vlan = dwxgmac2_enable_vlan,
	.config_l3_filter = dwxgmac2_config_l3_filter,
	.config_l4_filter = dwxgmac2_config_l4_filter,
	.set_arp_offload = dwxgmac2_set_arp_offload,
	.est_configure = dwxgmac3_est_configure,
	.fpe_configure = dwxgmac3_fpe_configure,
};

static void dwxlgmac2_rx_queue_enable(struct mac_device_info *hw, u8 mode,
				      u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XLGMAC_RXQ_ENABLE_CTRL0) & ~XGMAC_RXQEN(queue);
	if (mode == MTL_QUEUE_AVB)
		value |= 0x1 << XGMAC_RXQEN_SHIFT(queue);
	else if (mode == MTL_QUEUE_DCB)
		value |= 0x2 << XGMAC_RXQEN_SHIFT(queue);
	writel(value, ioaddr + XLGMAC_RXQ_ENABLE_CTRL0);
}

const struct stmmac_ops dwxlgmac2_ops = {
	.core_init = dwxgmac2_core_init,
	.set_mac = dwxgmac2_set_mac,
	.rx_ipc = dwxgmac2_rx_ipc,
	.rx_queue_enable = dwxlgmac2_rx_queue_enable,
	.rx_queue_prio = dwxgmac2_rx_queue_prio,
	.tx_queue_prio = dwxgmac2_tx_queue_prio,
	.rx_queue_routing = NULL,
	.prog_mtl_rx_algorithms = dwxgmac2_prog_mtl_rx_algorithms,
	.prog_mtl_tx_algorithms = dwxgmac2_prog_mtl_tx_algorithms,
	.set_mtl_tx_queue_weight = dwxgmac2_set_mtl_tx_queue_weight,
	.map_mtl_to_dma = dwxgmac2_map_mtl_to_dma,
	.config_cbs = dwxgmac2_config_cbs,
	.dump_regs = dwxgmac2_dump_regs,
	.host_irq_status = dwxgmac2_host_irq_status,
	.host_mtl_irq_status = dwxgmac2_host_mtl_irq_status,
	.flow_ctrl = dwxgmac2_flow_ctrl,
	.pmt = dwxgmac2_pmt,
	.set_umac_addr = dwxgmac2_set_umac_addr,
	.get_umac_addr = dwxgmac2_get_umac_addr,
	.set_eee_mode = dwxgmac2_set_eee_mode,
	.reset_eee_mode = dwxgmac2_reset_eee_mode,
	.set_eee_timer = dwxgmac2_set_eee_timer,
	.set_eee_pls = dwxgmac2_set_eee_pls,
	.pcs_ctrl_ane = NULL,
	.pcs_rane = NULL,
	.pcs_get_adv_lp = NULL,
	.debug = NULL,
	.set_filter = dwxgmac2_set_filter,
	.safety_feat_config = dwxgmac3_safety_feat_config,
	.safety_feat_irq_status = dwxgmac3_safety_feat_irq_status,
	.safety_feat_dump = dwxgmac3_safety_feat_dump,
	.set_mac_loopback = dwxgmac2_set_mac_loopback,
	.rss_configure = dwxgmac2_rss_configure,
	.update_vlan_hash = dwxgmac2_update_vlan_hash,
	.rxp_config = dwxgmac3_rxp_config,
	.get_mac_tx_timestamp = dwxgmac2_get_mac_tx_timestamp,
	.flex_pps_config = dwxgmac2_flex_pps_config,
	.sarc_configure = dwxgmac2_sarc_configure,
	.enable_vlan = dwxgmac2_enable_vlan,
	.config_l3_filter = dwxgmac2_config_l3_filter,
	.config_l4_filter = dwxgmac2_config_l4_filter,
	.set_arp_offload = dwxgmac2_set_arp_offload,
	.est_configure = dwxgmac3_est_configure,
	.fpe_configure = dwxgmac3_fpe_configure,
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
	mac->link.speed10 = XGMAC_CONFIG_SS_10_MII;
	mac->link.speed100 = XGMAC_CONFIG_SS_100_MII;
	mac->link.speed1000 = XGMAC_CONFIG_SS_1000_GMII;
	mac->link.speed2500 = XGMAC_CONFIG_SS_2500_GMII;
	mac->link.xgmii.speed2500 = XGMAC_CONFIG_SS_2500;
	mac->link.xgmii.speed5000 = XGMAC_CONFIG_SS_5000;
	mac->link.xgmii.speed10000 = XGMAC_CONFIG_SS_10000;
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

int dwxlgmac2_setup(struct stmmac_priv *priv)
{
	struct mac_device_info *mac = priv->hw;

	dev_info(priv->device, "\tXLGMAC\n");

	priv->dev->priv_flags |= IFF_UNICAST_FLT;
	mac->pcsr = priv->ioaddr;
	mac->multicast_filter_bins = priv->plat->multicast_filter_bins;
	mac->unicast_filter_entries = priv->plat->unicast_filter_entries;
	mac->mcast_bits_log2 = 0;

	if (mac->multicast_filter_bins)
		mac->mcast_bits_log2 = ilog2(mac->multicast_filter_bins);

	mac->link.duplex = 0;
	mac->link.speed1000 = XLGMAC_CONFIG_SS_1000;
	mac->link.speed2500 = XLGMAC_CONFIG_SS_2500;
	mac->link.xgmii.speed10000 = XLGMAC_CONFIG_SS_10G;
	mac->link.xlgmii.speed25000 = XLGMAC_CONFIG_SS_25G;
	mac->link.xlgmii.speed40000 = XLGMAC_CONFIG_SS_40G;
	mac->link.xlgmii.speed50000 = XLGMAC_CONFIG_SS_50G;
	mac->link.xlgmii.speed100000 = XLGMAC_CONFIG_SS_100G;
	mac->link.speed_mask = XLGMAC_CONFIG_SS;

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
