/*
 * TC956X ethernet driver.
 *
 * dwxgmac2_core.c
 *
 * Copyright (C) 2018 Synopsys, Inc. and/or its affiliates.
 * Copyright (C) 2023 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  17 July 2020 : 1. Filtering updates
 *  VERSION	 : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *
 *  05 Jul 2021 : 1. Used Systick handler instead of Driver kernel timer to process transmitted Tx descriptors.
 *                2. XFI interface support and module parameters for selection of Port0 and Port1 interface
 *  VERSION     : 01-00-01
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  22 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported with module parameters
 *  VERSION     : 01-00-04
 *  14 Sep 2021 : 1. Synchronization between ethtool vlan features
 *  		  "rx-vlan-offload", "rx-vlan-filter", "tx-vlan-offload" output and register settings.
 * 		  2. Added ethtool support to update "rx-vlan-offload", "rx-vlan-filter",
 *  		  and "tx-vlan-offload".
 * 		  3. Removed IOCTL TC956XMAC_VLAN_STRIP_CONFIG.
 * 		  4. Removed "Disable VLAN Filter" option in IOCTL TC956XMAC_VLAN_FILTERING.
 *  VERSION     : 01-00-13
 *  23 Sep 2021 : 1. Filtering All pause frames by default
 *  VERSION     : 01-00-14
 *  14 Oct 2021 : 1. Configuring pause frame control using kernel module parameter also forwarding
 *  		  only Link partner pause frames to Application and filtering PHY pause frames using FRP
 *  VERSION     : 01-00-16
 *  26 Oct 2021 : 1. Added EEE print in host IRQ and updated EEE configuration.
 *  VERSION     : 01-00-19
 *  04 Nov 2021 : 1. Added separate control functons for MAC TX and RX start/stop
 *  VERSION     : 01-00-20
 *  24 Nov 2021 : 1. EEE update for runtime configuration and LPI interrupt disabled.
 		  2. USXGMII support during link change
 *  VERSION     : 01-00-24
 *  08 Dec 2021 : 1. Renamed pause frames module parameter
 *  VERSION     : 01-00-30
 *  10 Nov 2023 : 1. Kernel 6.1 Porting changes
 *  VERSION     : 01-02-59
 */

#include <linux/bitrev.h>
#include <linux/crc32.h>
#include <linux/iopoll.h>
#include "tc956xmac.h"
#include "tc956xmac_ptp.h"
#include "dwxgmac2.h"

#ifndef TC956X_SRIOV_VF
extern unsigned int mac0_filter_phy_pause;
extern unsigned int mac1_filter_phy_pause;
#endif

#ifdef TC956X_SRIOV_DEBUG
void tc956x_filter_debug(struct tc956xmac_priv *priv);
#endif
#ifdef TC956X_SRIOV_PF
int tc956x_pf_set_mac_filter(struct net_device *dev, int vf, const u8 *mac);
void tc956x_pf_del_mac_filter(struct net_device *dev, int vf, const u8 *mac);
void tc956x_pf_del_umac_addr(struct tc956xmac_priv *priv, int index, int vf);
void tc956x_pf_set_vlan_filter(struct net_device *dev, u16 vf, u16 vid);
void tc956x_pf_del_vlan_filter(struct net_device *dev, u16 vf, u16 vid);
#endif

static void tc956x_set_mac_addr(struct tc956xmac_priv *priv, struct mac_device_info *hw,
				const u8 *mac, int index, int vf);
#ifdef TC956X
static void dwxgmac2_disable_tx_vlan(struct tc956xmac_priv *priv,
				struct mac_device_info *hw);
static void dwxgmac2_enable_rx_vlan_stripping(struct tc956xmac_priv *priv,
				struct mac_device_info *hw);
static void dwxgmac2_disable_rx_vlan_stripping(struct tc956xmac_priv *priv,
				struct mac_device_info *hw);
static void dwxgmac2_enable_rx_vlan_filtering(struct tc956xmac_priv *priv,
				struct mac_device_info *hw);
static void dwxgmac2_disable_rx_vlan_filtering(struct tc956xmac_priv *priv,
				struct mac_device_info *hw);
#endif

static void dwxgmac2_core_init(struct tc956xmac_priv *priv,
				struct mac_device_info *hw, struct net_device *dev)
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
		case SPEED_5000:
			if (priv->plat->interface == PHY_INTERFACE_MODE_USXGMII)
				tx |= hw->link.xgmii.speed5000;
			break;
		case SPEED_2500:
			if (priv->plat->interface == PHY_INTERFACE_MODE_USXGMII)
				tx |= hw->link.xgmii.speed2500;
			else
				tx |= hw->link.speed2500;
			break;
		case SPEED_1000:
		default:
			tx |= hw->link.speed1000;
			break;
		}
	}
#ifndef TC956X
	if (priv->plat->interface == PHY_INTERFACE_MODE_RGMII)
		tx |= hw->link.speed1000;
	else if (priv->plat->interface == PHY_INTERFACE_MODE_SGMII)
		tx |= hw->link.speed2500;
	else if ((priv->plat->interface == PHY_INTERFACE_MODE_USXGMII) ||
		(priv->plat->interface == PHY_INTERFACE_MODE_10GKR))
		tx |= hw->link.xgmii.speed10000;
#endif
	writel(tx, ioaddr + XGMAC_TX_CONFIG);
	writel(rx, ioaddr + XGMAC_RX_CONFIG);
#ifdef TC956X_LPI_INTERRUPT
	writel(XGMAC_INT_DEFAULT_EN, ioaddr + XGMAC_INT_EN);
#endif
	netdev_dbg(priv->dev, "%s: MAC TX Config = 0x%x", __func__,
			readl(ioaddr + XGMAC_TX_CONFIG));

	netdev_dbg(priv->dev, "%s: MAC RX Config = 0x%x", __func__,
			readl(ioaddr + XGMAC_RX_CONFIG));
}

static void dwxgmac2_set_mac(struct tc956xmac_priv *priv, void __iomem *ioaddr,
					bool enable)
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

static void dwxgmac2_set_mac_tx(struct tc956xmac_priv *priv, void __iomem *ioaddr,
					bool enable)
{
	u32 tx = readl(ioaddr + XGMAC_TX_CONFIG);

	if (enable) {
		tx |= XGMAC_CONFIG_TE;
	} else {
		tx &= ~XGMAC_CONFIG_TE;
	}

	writel(tx, ioaddr + XGMAC_TX_CONFIG);
}

static void dwxgmac2_set_mac_rx(struct tc956xmac_priv *priv, void __iomem *ioaddr,
					bool enable)
{
	u32 rx = readl(ioaddr + XGMAC_RX_CONFIG);

	if (enable) {
		rx |= XGMAC_CONFIG_RE;
	} else {
		rx &= ~XGMAC_CONFIG_RE;
	}

	writel(rx, ioaddr + XGMAC_RX_CONFIG);
}

static int dwxgmac2_rx_ipc(struct tc956xmac_priv *priv, struct mac_device_info *hw)
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

static void dwxgmac2_rx_queue_enable(struct tc956xmac_priv *priv,
					struct mac_device_info *hw, u8 mode,
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

	netdev_dbg(priv->dev, "%s: MAC RxQ%d Control = 0x%x", __func__,
			queue, readl(ioaddr + XGMAC_RXQ_CTRL0));
}

static void dwxgmac2_rx_queue_prio(struct tc956xmac_priv *priv,
				   struct mac_device_info *hw, u32 prio,
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

	netdev_dbg(priv->dev, "%s: MAC RxQ%d Control = 0x%x", __func__,
			queue, readl(ioaddr + reg));
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static void dwxgmac2_tx_queue_prio(struct tc956xmac_priv *priv,
				   struct mac_device_info *hw, u32 prio,
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
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static void tc956x_rx_queue_routing(struct tc956xmac_priv *priv,
				    struct mac_device_info *hw,
				    u8 packet, u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	static const struct tc956xmac_rx_routing route_possibilities[] = {
		{ XGMAC_RXQCTRL_AVCPQ_MASK, XGMAC_RXQCTRL_AVCPQ_SHIFT },
		{ XGMAC_RXQCTRL_PTPQ_MASK, XGMAC_RXQCTRL_PTPQ_SHIFT },
		{ XGMAC_RXQCTRL_DCBCPQ_MASK, XGMAC_RXQCTRL_DCBCPQ_SHIFT },
		{ XGMAC_RXQCTRL_UPQ_MASK, XGMAC_RXQCTRL_UPQ_SHIFT },
		{ XGMAC_RXQCTRL_MCBCQ_MASK, XGMAC_RXQCTRL_MCBCQ_SHIFT },
		{ XGMAC_RXQCTRL_FPRQ_MASK, XGMAC_RXQCTRL_FPRQ_SHIFT },
	};
#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	/* Fail packet routing for UC, MC, VLAN */
	if (packet == PACKET_FILTER_FAIL) {
		value = readl(ioaddr + XGMAC_RXQ_CTRL4);

		value &= (~(XGMAC_VFFQ_MASK | XGMAC_MFFQ_MASK | XGMAC_UFFQ_MASK));

		value |= ((queue << XGMAC_UFFQ_SHIFT) | \
				(queue << XGMAC_MFFQ_SHIFT) | \
				(queue << XGMAC_VFFQ_SHIFT));

		value |= XGMAC_UFFQE | XGMAC_MFFQE | XGMAC_VFFQE;

		writel(value, ioaddr + XGMAC_RXQ_CTRL4);

		return;
	}
#endif
	value = readl(ioaddr + XGMAC_RXQ_CTRL1);

	/* routing configuration */
	value &= ~route_possibilities[packet - 1].reg_mask;
	value |= (queue << route_possibilities[packet-1].reg_shift) &
		 route_possibilities[packet - 1].reg_mask;

	/* some packets require extra ops */
	if (packet == PACKET_MCBCQ) {
		value &= ~XGMAC_RXQCTRL_MCBCQEN;
		value |= 0x1 << XGMAC_RXQCTRL_MCBCQEN_SHIFT;

		/* If multicast/broadcast queue is enabled, then set OMCBCQ bit */
		value &= ~XGMAC_RXQCTRL_OMCBCQ;
		value |= 0x1 << XGMAC_RXQCTRL_OMCBCQ_SHIFT;
	}

	writel(value, ioaddr + XGMAC_RXQ_CTRL1);
}

static void dwxgmac2_prog_mtl_rx_algorithms(struct tc956xmac_priv *priv,
					    struct mac_device_info *hw,
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

static void dwxgmac2_prog_mtl_tx_algorithms(struct tc956xmac_priv *priv,
					    struct mac_device_info *hw,
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
	for (i = 0; i < MTL_MAX_TX_TC; i++) {
		value = readl(ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(i));
		value &= ~XGMAC_TSA;
		if (ets)
			value |= XGMAC_ETS;
		writel(value, ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(i));
	}
}

static void dwxgmac2_set_mtl_tx_queue_weight(struct tc956xmac_priv *priv,
					     struct mac_device_info *hw,
					     u32 weight, u32 tc)
{
	void __iomem *ioaddr = hw->pcsr;

	writel(weight, ioaddr + XGMAC_MTL_TCx_QUANTUM_WEIGHT(tc));

	netdev_dbg(priv->dev, "%s: MTL_TC%d weight = %d", __func__, tc, weight);
}

static void dwxgmac2_map_mtl_to_dma(struct tc956xmac_priv *priv,
				    struct mac_device_info *hw, u32 queue,
				    u32 chan)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value, reg, rx_q;

	rx_q = queue;
	reg = (queue < 4) ? XGMAC_MTL_RXQ_DMA_MAP0 : XGMAC_MTL_RXQ_DMA_MAP1;
	if (queue >= 4)
		queue -= 4;

	value = readl(ioaddr + reg);
#ifdef TC956X_SRIOV_PF
	/* Set QxDDMACH to enable Rx Q-ch mapping based on DA Filter Value */
	if (priv->plat->rx_queues_cfg[rx_q].chan == TC956X_DA_MAP) {
		value |= 1 << XGMAC_QxDDMACH_SHIFT(queue);
	} else {
#endif
		value &= ~XGMAC_QxMDMACH(queue);
		value |= (chan << XGMAC_QxMDMACH_SHIFT(queue)) & XGMAC_QxMDMACH(queue);
#ifdef TC956X_SRIOV_PF
	}
#endif

	writel(value, ioaddr + reg);

	netdev_dbg(priv->dev, "%s: MTLQ%d DMA mapping = 0x%x", __func__, queue,
			readl(ioaddr + reg));
}

static void dwxgmac2_config_cbs(struct tc956xmac_priv *priv,
				struct mac_device_info *hw,
				u32 send_slope, u32 idle_slope,
				u32 high_credit, u32 low_credit, u32 queue)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	u8 traffic_class;

	traffic_class = priv->plat->tx_queues_cfg[queue].traffic_class;

	writel(send_slope & GENMASK(15, 0), ioaddr + XGMAC_MTL_TCx_SENDSLOPE(traffic_class));
	writel(idle_slope & GENMASK(20, 0), ioaddr + XGMAC_MTL_TCx_QUANTUM_WEIGHT(traffic_class));
	writel(high_credit & GENMASK(28, 0), ioaddr + XGMAC_MTL_TCx_HICREDIT(traffic_class));
	writel(low_credit & GENMASK(28, 0), ioaddr + XGMAC_MTL_TCx_LOCREDIT(traffic_class));

	value = readl(ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(traffic_class));
	/* CC is always set 0 */
	value &= ~(XGMAC_TSA | XGMAC_CC);
	value |= XGMAC_CBS;
	writel(value, ioaddr + XGMAC_MTL_TCx_ETS_CONTROL(traffic_class));

	netdev_dbg(priv->dev, "%s: MTL_TC%d Send Slope Register = 0x%x", __func__, traffic_class,
			readl(ioaddr + XGMAC_MTL_TCx_SENDSLOPE(traffic_class)));
	netdev_dbg(priv->dev, "%s: MTL_TC%d Idle Slope Register = 0x%x", __func__, traffic_class,
			readl(ioaddr + XGMAC_MTL_TCx_QUANTUM_WEIGHT(traffic_class)));
	netdev_dbg(priv->dev, "%s: MTL_TC%d High Credit Register = 0x%x", __func__, traffic_class,
			readl(ioaddr + XGMAC_MTL_TCx_HICREDIT(traffic_class)));
	netdev_dbg(priv->dev, "%s: MTL_TC%d Lo Credit Register = 0x%x", __func__, traffic_class,
			readl(ioaddr + XGMAC_MTL_TCx_LOCREDIT(traffic_class)));

}

static void dwxgmac2_dump_regs(struct tc956xmac_priv *priv,
				struct mac_device_info *hw, u32 *reg_space)
{
	void __iomem *ioaddr = hw->pcsr;
	int i, ch, tc, k;

	KPRINT_DEBUG1("************************************EMAC Dump***********************************************");
	for (i = ETH_CORE_DUMP_OFFSET1; i <= ETH_CORE_DUMP_OFFSET1_END; i++) {/*MAC reg*/
		reg_space[i] = readl(ioaddr + MAC_OFFSET + (i * 4));
		KPRINT_DEBUG1("%04x : %08x\n", i*4, reg_space[i]);
	}

	for (i = ETH_CORE_DUMP_OFFSET2; i <= ETH_CORE_DUMP_OFFSET2_END; i++) { /*MAC reg*/
		reg_space[i] = readl(ioaddr + MAC_OFFSET + (i * 4));
		KPRINT_DEBUG1("%04x : %08x\n", i*4, reg_space[i]);
	}

	for (i = ETH_CORE_DUMP_OFFSET3; i <= ETH_CORE_DUMP_OFFSET3_END; i++) {/*MTL reg*/
		reg_space[i] = readl(ioaddr + MAC_OFFSET + (i * 4));
		KPRINT_DEBUG1("%04x : %08x\n", i*4, reg_space[i]);
	}

	for (i = ETH_CORE_DUMP_OFFSET4; i <= ETH_CORE_DUMP_OFFSET4_END; i++) {/*MTL TX reg*/
		for (ch = 0; ch < 8; ch++) {
			k = i + (0x20 * ch);
			reg_space[k] = readl(ioaddr + MAC_OFFSET + ((0x0080 * ch) + (i * 4)));
			KPRINT_DEBUG1("%04x : %08x\n", (0x0080 * ch) + (i * 4), reg_space[k]);
		}
	}

	for (i = ETH_CORE_DUMP_OFFSET5; i <= ETH_CORE_DUMP_OFFSET5_END; i++) {/*MTL TCQ reg*/
		for (tc = 0; tc < 5; tc++) {
			k = i + (0x20 * tc);
			reg_space[k] = readl(ioaddr + MAC_OFFSET + ((0x0080 * tc) + (i * 4)));
			KPRINT_DEBUG1("%04x : %08x\n", (0x0080 * tc) + (i * 4), reg_space[k]);
		}
	}

	for (i = ETH_CORE_DUMP_OFFSET6; i <= ETH_CORE_DUMP_OFFSET6_END; i++) {/*MTL RX reg*/
		for (ch = 0; ch < 8; ch++) {
			k = i + (0x20 * ch);
			reg_space[k] = readl(ioaddr + MAC_OFFSET + (0x0080*ch) + (i * 4));
			KPRINT_DEBUG1("%04x : %08x\n", (0x0080 * ch) + (i * 4), reg_space[k]);
		}
	}
}

static int dwxgmac2_host_irq_status(struct tc956xmac_priv *priv,
				    struct mac_device_info *hw,
				    struct tc956xmac_extra_stats *x)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 stat, en;
	int ret = 0;
	int val = 0;

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
			KPRINT_INFO("Transmit LPI Entry.....\n");
			ret |= CORE_IRQ_TX_PATH_IN_LPI_MODE;
			x->irq_tx_path_in_lpi_mode_n++;
		}
		if (lpi & XGMAC_TLPIEX) {
			KPRINT_INFO("Transmit LPI Exit.....\n");
			ret |= CORE_IRQ_TX_PATH_EXIT_LPI_MODE;
			x->irq_tx_path_exit_lpi_mode_n++;
		}
		if (lpi & XGMAC_RLPIEN) {
			KPRINT_INFO("Receive LPI Entry.......\n");
			x->irq_rx_path_in_lpi_mode_n++;
		}
		if (lpi & XGMAC_RLPIEX) {
			KPRINT_INFO("Receive LPI Exit......\n");
			x->irq_rx_path_exit_lpi_mode_n++;
		}

#ifdef EEE
		val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_VR_XS_PCS_DIG_STS);
		KPRINT_INFO("XPCS LPI status : %x........\n", val);
		if (val & XGMAC_LTX_LRX_STATE) {
			if (val & XGMAC_LPI_RECEIVE_STATE)
				KPRINT_INFO("XPCS LPI Receive State.........\n");
			if (val & XGMAC_LPI_TRANSMIT_STATE)
				KPRINT_INFO("XPCS LPI transmit state.....\n");
		}

		val = tc956x_xpcs_read(priv->xpcsaddr, XGMAC_SR_XS_PCS_STS1);
		if (val & XGMAC_RX_LPI_RECEIVE)
			KPRINT_INFO("XPCS RX LPI Received......");
		if (val & XGAMC_TX_LPI_RECEIVE)
			KPRINT_INFO("XPCS TX LPI Received......");
#endif
	}
	if (stat & XGMAC_TSIS) {
		val = readl(ioaddr + PTP_XGMAC_OFFSET + PTP_TS_STATUS);
		if (val & XGMAC_AUXTSTRIG) {
			KPRINT_INFO("\n");
			KPRINT_INFO("second: %x\n",	readl(ioaddr + PTP_XGMAC_OFFSET + PTP_ATS_SEC));
			KPRINT_INFO("subsec(ns): %x\n", readl(ioaddr + PTP_XGMAC_OFFSET + PTP_ATS_NSEC));
		}
	}

	return ret;
}

static int dwxgmac2_host_mtl_irq_status(struct tc956xmac_priv *priv,
					struct mac_device_info *hw, u32 chan)
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

static void dwxgmac2_flow_ctrl(struct tc956xmac_priv *priv,
			       struct mac_device_info *hw, unsigned int duplex,
			       unsigned int fc, unsigned int pause_time,
			       u32 tx_cnt)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 i, flow;

	flow = 0;
	if (fc & FLOW_RX)
#ifdef TC956X_SRIOV_PF
		flow |= XGMAC_RFE;
#elif defined TC956X_SRIOV_VF
		flow |= XGMAC_RFE;
#endif
	writel(flow, ioaddr + XGMAC_RX_FLOW_CTRL);

	flow = 0;
	if (fc & FLOW_TX) {
		flow |= XGMAC_TFE;

		if (duplex)
			flow |= pause_time << XGMAC_PT_SHIFT;
	}

	for (i = 0; i < tx_cnt; i++)
		writel(flow, ioaddr + XGMAC_Qx_TX_FLOW_CTRL(i));
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static void dwxgmac2_pmt(struct tc956xmac_priv *priv,
				struct mac_device_info *hw, unsigned long mode)
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
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */


static void dwxgmac2_set_umac_addr(struct tc956xmac_priv *priv,
				   struct mac_device_info *hw,
				   unsigned char *addr, unsigned int reg_n,
				   unsigned int vf)
{

	tc956x_set_mac_addr(priv, hw, addr, reg_n, vf);
}

static void dwxgmac2_get_umac_addr(struct tc956xmac_priv *priv,
				   struct mac_device_info *hw,
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

static void dwxgmac2_set_eee_mode(struct tc956xmac_priv *priv,
				  struct mac_device_info *hw,
				  bool en_tx_lpi_clockgating)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_LPI_CTRL);

	value |= XGMAC_LPITXEN | XGMAC_LPITXA;
	if (en_tx_lpi_clockgating)
		value |= XGMAC_TXCGE;
#ifdef EEE_MAC_CONTROLLED_MODE
	value |= XGMAC_PLS | XGMAC_PLSDIS | XGMAC_LPIATE;
#endif
	writel(value, ioaddr + XGMAC_LPI_CTRL);
}

static void dwxgmac2_reset_eee_mode(struct tc956xmac_priv *priv,
						struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_LPI_CTRL);
	value &= ~(XGMAC_LPITXEN | XGMAC_LPITXA | XGMAC_TXCGE);
#ifdef EEE_MAC_CONTROLLED_MODE
	value &= ~(XGMAC_PLS | XGMAC_PLSDIS | XGMAC_LPIATE);
#endif
	writel(value, ioaddr + XGMAC_LPI_CTRL);
}

static void dwxgmac2_set_eee_pls(struct tc956xmac_priv *priv,
				struct mac_device_info *hw, int link)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_LPI_CTRL);
	if (link)
		value |= XGMAC_PLS;
	else
		value &= ~XGMAC_PLS;
	writel(value, ioaddr + XGMAC_LPI_CTRL);
	KPRINT_DEBUG1("LPI Control status register PLS bit: 0x%X\n", ((value & 0x20000) >> 17));
}

static void dwxgmac2_set_eee_timer(struct tc956xmac_priv *priv,
				struct mac_device_info *hw, int ls, int tw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = (tw & 0xffff) | ((ls & 0x3ff) << 16);
	writel(value, ioaddr + XGMAC_LPI_TIMER_CTRL);
}

static void dwxgmac2_set_mchash(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 *mcfilterbits,
				int mcbitslog2)
{
	int numhashregs, regs, data = 0;

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

	for (regs = 0; regs < numhashregs; regs++) {
		data = readl(ioaddr + XGMAC_HASH_TABLE(regs));

		data |= mcfilterbits[regs];
		writel(data, ioaddr + XGMAC_HASH_TABLE(regs));
	}
}

#ifdef TC956X_SRIOV_PF
static s32 tc956x_mac_ind_acc_wr_rd(struct tc956xmac_priv *priv, u8 wr_rd, u32 msel, u32 offset, u32 *val)
{
	u32 reg_data, limit;
	void __iomem *ioaddr = priv->ioaddr;

	/* Wait till previous operation is complete */
	limit = 1000;
	while (limit--) {
		if (!(readl(ioaddr + XGMAC_INDR_ACC_CTRL) & XGMAC_INDR_ACC_CTRL_OB))
			break;
		udelay(1);
	}
	if (limit < 0)
		return -EBUSY;

	/* Clear previous values */
	reg_data = readl(ioaddr + XGMAC_INDR_ACC_CTRL);
	reg_data &= (~(XGMAC_INDR_ACC_CTRL_RSVD | XGMAC_INDR_ACC_CTRL_MSEL | \
			XGMAC_INDR_ACC_CTRL_AOFF | XGMAC_INDR_ACC_CTRL_COM | \
			XGMAC_INDR_ACC_CTRL_AUTO));

	if (wr_rd) {
		/* Indirect Read sequence */

		/* Set read params */
		reg_data |= (msel << XGMAC_INDR_ACC_CTRL_MSEL_SHIFT) | \
				(wr_rd << XGMAC_INDR_ACC_CTRL_COM_SHIFT) | \
				(offset << XGMAC_INDR_ACC_CTRL_AOFF_SHIFT);

		writel(reg_data, ioaddr + XGMAC_INDR_ACC_CTRL);


		/* Start Operation */
		reg_data |= XGMAC_INDR_ACC_CTRL_OB;

		writel(reg_data, ioaddr + XGMAC_INDR_ACC_CTRL);

		/* Wait for completion */
		limit = 1000;
		while (limit--) {
			if (!(readl(ioaddr + XGMAC_INDR_ACC_CTRL) & XGMAC_INDR_ACC_CTRL_OB))
				break;
			udelay(1);
		}
		if (limit < 0)
			return -EBUSY;

		/* Read value */
		*val = readl(ioaddr + XGMAC_INDR_ACC_DATA);


	} else {
		/* Indirect Write sequence */

		/* Set write value and params */
		writel(*val, ioaddr + XGMAC_INDR_ACC_DATA);

		reg_data |= (msel << XGMAC_INDR_ACC_CTRL_MSEL_SHIFT) | \
				(offset << XGMAC_INDR_ACC_CTRL_AOFF_SHIFT);

		writel(reg_data, ioaddr + XGMAC_INDR_ACC_CTRL);

		/* Start Operation */
		reg_data |= XGMAC_INDR_ACC_CTRL_OB;

		writel(reg_data, ioaddr + XGMAC_INDR_ACC_CTRL);

		/* Wait for completion */
		limit = 1000;//10000;
		while (limit--) {
			if (!(readl(ioaddr + XGMAC_INDR_ACC_CTRL) & XGMAC_INDR_ACC_CTRL_OB))
				break;
			udelay(1);
		}
		if (limit < 0)
			return -EBUSY;

	}

	return 0;

}

#ifdef TC956X_SRIOV_DEBUG
void tc956x_filter_debug(struct tc956xmac_priv *priv)
{
	u32 reg_data = 0, offset;
	void __iomem *ioaddr = priv->ioaddr;

	for (offset = 0; offset < 32; offset++) {
		if (tc956x_mac_ind_acc_wr_rd(priv, XGMAC_COM_READ, XGMAC_MSEL_DCHSEL, offset, &reg_data)) {
			netdev_err(priv->dev, "Setting XDCS Failed\n");
			return;
		}
		KPRINT_INFO("%d %08x %08x %01x\n", offset, readl(ioaddr + XGMAC_ADDRx_HIGH(offset)), readl(ioaddr + XGMAC_ADDRx_LOW(offset)), reg_data);
	}
}
#endif
#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
static void tc956x_set_dma_ch(struct tc956xmac_priv *priv, struct mac_device_info *hw, int index, int vf, bool mc_addr)
#else
static void tc956x_set_dma_ch(struct tc956xmac_priv *priv, struct mac_device_info *hw, int index, int vf)
#endif
{
	u32 data = 0, reg_data = 0;

	reg_data = 0;

	if (tc956x_mac_ind_acc_wr_rd(priv, XGMAC_COM_READ, XGMAC_MSEL_DCHSEL, index, &reg_data)) {
		netdev_err(priv->dev, "Setting XDCS Failed\n");
		return;
	}

	if (vf == TC956XMAC_VF_IVI) /* IVI */
		data |= TC956XMAC_CHA_IVI;
	else if (vf == TC956XMAC_VF_TCU) /* TCU */
		data |= TC956XMAC_CHA_TCU;
	else if (vf == TC956XMAC_VF_ADAS) /* ADAS */
		data |= TC956XMAC_CHA_ADAS;
	else if (vf == PF_DRIVER) {/* PF */
#if defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
		if (mc_addr)
			data |= TC956XMAC_CHA_M2M;
		data |= TC956XMAC_CHA_PF;

#else
		data |= TC956XMAC_CHA_PF;
#endif
	} else
		data |= TC956XMAC_CHA_NO_0;

	reg_data |= data;

	if (tc956x_mac_ind_acc_wr_rd(priv, XGMAC_COM_WRITE, XGMAC_MSEL_DCHSEL, index, &reg_data)) {
		netdev_err(priv->dev, "Setting XDCS Failed\n");
		return;
	}

}

static void tc956x_del_dma_ch(struct tc956xmac_priv *priv, struct mac_device_info *hw,
				int index, int vf)
{
	u32 data = 0, reg_data = 0;


	if (tc956x_mac_ind_acc_wr_rd(priv, XGMAC_COM_READ, XGMAC_MSEL_DCHSEL, index, &reg_data)) {
		netdev_err(priv->dev, "Setting XDCS Failed\n");
		return;
	}

	if (vf == TC956XMAC_VF_IVI) /* IVI */
		data |= TC956XMAC_CHA_IVI;
	else if (vf == TC956XMAC_VF_TCU) /* TCU */
		data |= TC956XMAC_CHA_TCU;
	else if (vf == TC956XMAC_VF_ADAS) /* ADAS */
		data |= TC956XMAC_CHA_ADAS;
	else if (vf == PF_DRIVER) /* PF */
		data |= TC956XMAC_CHA_PF;
	else
		data |= TC956XMAC_CHA_NO_0;

	reg_data &= ~data;


	if (tc956x_mac_ind_acc_wr_rd(priv, XGMAC_COM_WRITE, XGMAC_MSEL_DCHSEL, index, &reg_data)) {
		netdev_err(priv->dev, "Setting XDCS Failed\n");
		return;
	}
}

#endif

static void tc956x_set_mac_addr(struct tc956xmac_priv *priv, struct mac_device_info *hw,
				const u8 *mac, int index, int vf)
{
	void __iomem *ioaddr = hw->pcsr;
	unsigned long data = 0;
	u32 value;
#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
	u8 flow_ctrl_addr[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x01};
	bool multicast_addr = false;
#endif

	data = (mac[5] << 8) | mac[4];
	/* For MAC Addr registers se have to set the Address Enable (AE)
	 * bit that has no effect on the High Reg 0 where the bit 31 (MO)
	 * is RO.
	 */
	value = readl(ioaddr + XGMAC_PACKET_FILTER);
	if ((value & XGMAC_PACKET_FILTER_SA) || (value & XGMAC_PACKET_FILTER_SAIF))
		writel(data | XGMAC_AE | XGMAC_SA, ioaddr + XGMAC_ADDRx_HIGH(index));
	else
		writel(data | XGMAC_AE, ioaddr + XGMAC_ADDRx_HIGH(index));

	data = 0;
	data = (mac[3] << 24) | (mac[2] << 16) | (mac[1] << 8) | mac[0];
	writel(data, ioaddr + XGMAC_ADDRx_LOW(index));

#ifdef TC956X_SRIOV_PF
#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
	if (is_multicast_ether_addr(mac))
		multicast_addr = true;

	/* Route all multicast Flow control packets to PCI path */
	if (!memcmp(&flow_ctrl_addr[0], &mac[0], TC956X_SIX))
		multicast_addr = false;

	tc956x_set_dma_ch(priv, hw, index, vf, multicast_addr);
#else
	tc956x_set_dma_ch(priv, hw, index, vf);
#endif
#endif
}

static void tc956x_del_mac_addr(struct tc956xmac_priv *priv, struct mac_device_info *hw,
				int index, int vf)
{
	void __iomem *ioaddr = hw->pcsr;
	unsigned long data = 0;

	writel(data, ioaddr + XGMAC_ADDRx_HIGH(index));
	writel(data, ioaddr + XGMAC_ADDRx_LOW(index));

#ifdef TC956X_SRIOV_PF
	/*clearing the dma indirect XDCS register
	 * when the mac address is completely deleted from the sw table
	 */

	if (tc956x_mac_ind_acc_wr_rd(priv, XGMAC_COM_WRITE, XGMAC_MSEL_DCHSEL, index, (u32 *)&data)) {
		netdev_err(priv->dev, "Setting XDCS Failed\n");
		return;
	}
#endif

}

static void tc956x_del_sw_mac_helper(struct tc956x_mac_addr *mac_table, int vf)
{
	int vf_number;

	for (vf_number = 0; vf_number < 4; vf_number++) {
		if (mac_table->vf[vf_number] == vf) {
			mac_table->vf[vf_number] = 0;
			mac_table->counter--;
			if (mac_table->counter == 0)
				mac_table->status = TC956X_MAC_STATE_VACANT;
			break;
		}
	}
}

static void tc956x_del_sw_mac_table(struct net_device *dev,
						const u8 *mac, int vf)
{
	int i;
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct mac_device_info *hw = priv->hw;
	struct tc956x_mac_addr *mac_table = &priv->mac_table[0];
	u32 mc_filter[2];
	u32 nr;
	u32 data1, data2;

	void __iomem *ioaddr = (void __iomem *)dev->base_addr;

	for (i = XGMAC_ADDR_ADD_SKIP_OFST; i < (TC956X_MAX_PERFECT_ADDRESSES);
	     i++, mac_table++) {
		if (mac_table->status == TC956X_MAC_STATE_OCCUPIED) {
			if (ether_addr_equal(mac, mac_table->mac_address)) {
				tc956x_del_sw_mac_helper(mac_table, vf);
				break;
			}
		}
	}
	if (mac_table->counter == 0) {
		/* deleting the crc from hast table*/
		if (priv->l2_filtering_mode == 1) {
			memset(mc_filter, 0, sizeof(mc_filter));
			nr = (bitrev32(~crc32_le(~0, mac_table->mac_address, 6)) >> 26);
			/* The most significant bit determines the register
			 * to use while the other 5 bits determines the bit
			 * within the selected register
			 */
			mc_filter[nr >> 5] |= (1 << (nr & 0x1F));

			data1 = 0;
			data1 = readl(ioaddr + XGMAC_HASH_TAB_0_31);

			data2 = readl(ioaddr + XGMAC_HASH_TAB_32_63);

			data1 &= ~mc_filter[0];
			data2 &= ~mc_filter[1];

			writel(data1, ioaddr + XGMAC_HASH_TAB_0_31);
			writel(data2, ioaddr + XGMAC_HASH_TAB_32_63);
		}
		tc956x_del_mac_addr(priv, hw, i, vf);
#ifdef TC956X_SRIOV_PF
	} else {
		tc956x_del_dma_ch(priv, hw, i, vf);
#endif
	}
}

static int tc956x_add_actual_mac_table(struct net_device *dev,
							const u8 *mac, int vf)
{
	int i;
	int ret_value = -1;
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct mac_device_info *hw = priv->hw;
	int mcbitslog2 = hw->mcast_bits_log2;
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	u32 mc_filter[2];
	u32 nr, flag = 0;
	u32 value;

	struct tc956x_mac_addr *mac_table = &priv->mac_table[0];

	for (i = XGMAC_ADDR_ADD_SKIP_OFST; i < (TC956X_MAX_PERFECT_ADDRESSES);
	     i++, mac_table++) {
		if (mac_table->status & TC956X_MAC_STATE_OCCUPIED)
			continue;

		flag = 1;
		ether_addr_copy(mac_table->mac_address, mac);
		mac_table->status = TC956X_MAC_STATE_OCCUPIED;
		mac_table->counter++;
		mac_table->vf[0] = vf;
		ret_value = 0;
		break;
	}
	if (flag == 1) {
		if (priv->l2_filtering_mode == 1) {
			memset(mc_filter, 0, sizeof(mc_filter));
			nr = (bitrev32(~crc32_le(~0, mac, 6))
				>> (32 - mcbitslog2));
			mc_filter[nr >> 5] |= (1 << (nr & 0x1F));

			dwxgmac2_set_mchash(priv, ioaddr, mc_filter, mcbitslog2);
		}
		tc956x_set_mac_addr(priv, hw, mac, i, vf);
	} else {

		KPRINT_INFO("Space is not available in MAC_Table\n");
		KPRINT_INFO("Enabling the promisc mode\n");
		value = readl(ioaddr + XGMAC_PACKET_FILTER);
#if defined(TC956X_SRIOV_PF) || defined(TC956X_SRIOV_VF) || defined(TC956X_AUTOMOTIVE_CONFIG) || defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
		value |= XGMAC_FILTER_RA;
#else
		value |= XGMAC_FILTER_PR;
#endif
		writel(value, ioaddr + XGMAC_PACKET_FILTER);
	}
	return ret_value;
}


static int tc956x_mac_duplication(struct tc956xmac_priv *priv,
					    struct mac_device_info *hw,
					    struct tc956x_mac_addr *mac_table,
					    const u8 *mac, int vf)
{
	int i, vf_no;
	int free_index = 0, ret_value = -1;

	for (i = XGMAC_ADDR_ADD_SKIP_OFST; i < (TC956X_MAX_PERFECT_ADDRESSES);
	     i++, mac_table++) {
		if (mac_table->status == TC956X_MAC_STATE_OCCUPIED) {
			if (ether_addr_equal(mac, mac_table->mac_address)) {
				if (is_unicast_ether_addr(mac)) {
					KPRINT_DEBUG1("%pM mac is unicast address and it cannot be duplicated\n", mac);
					return -1;
				}
				for (vf_no = 0; vf_no < 4; vf_no++) {
					if (mac_table->vf[vf_no] == 0) {
						free_index = vf_no;
					} else if (
						mac_table->vf[vf_no] == vf) {
						return -1;
					}
				}
#ifdef TC956X_SRIOV_PF
					/*if vf is not found in vf[],
					 *than add vf no, in free index
					 *of vf[]
					 */
					KPRINT_DEBUG1(KERN_DEBUG "%d offset %pM mac duplication\n", i, mac);
					mac_table->vf[free_index] = vf;
					mac_table->counter++;
#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
					tc956x_set_dma_ch(priv, hw, i, vf, false);
#else
					tc956x_set_dma_ch(priv, hw, i, vf);
#endif
					ret_value = TC956X_MAC_STATE_MODIFIED;
					return ret_value;
#elif defined TC956X_SRIOV_VF
				/*if vf is not found in vf[],
				 *than add vf no, in free index
				 *of vf[]
				 */
				mac_table->vf[free_index] = vf;
				mac_table->counter++;
				ret_value = TC956X_MAC_STATE_MODIFIED;
				return ret_value;
#endif
			}
		}
	}
			ret_value = TC956X_MAC_STATE_NEW;
	return ret_value;

}

static int tc956x_check_mac_duplication(struct net_device *dev, const u8 *mac, int vf)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct tc956x_mac_addr *mac_table = &priv->mac_table[0];
	struct mac_device_info *hw = priv->hw;
	int ret_value = -1;

	if (is_zero_ether_addr(mac)) {
		;
	} else {
		ret_value =
			tc956x_mac_duplication(priv, hw,
							mac_table, mac, vf);
	}
	return ret_value;
}

static int tc956x_add_sw_mac_table(struct net_device *dev, const u8 *mac, int vf)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);
	unsigned int reg_value;
	int ret_value = 0;
#ifdef TC956X_SRIOV_PF
	/*u32 value_extended = readl(ioaddr + XGMAC_EXTENDED_REG);*/
#endif

#ifndef TC956X_ENABLE_MAC2MAC_BRIDGE
	value &= ~(XGMAC_FILTER_PR | XGMAC_FILTER_HMC | XGMAC_FILTER_PM
				| XGMAC_FILTER_RA);
#endif
	value |= XGMAC_FILTER_HPF;
	writel(value, ioaddr + XGMAC_PACKET_FILTER);

	if (priv->l2_filtering_mode == 1) {
		value |= XGMAC_FILTER_HMC;
		value |= XGMAC_FILTER_HUC;
	} else {
	}

	ret_value = tc956x_check_mac_duplication(dev, mac, vf);
	if (ret_value == TC956X_MAC_STATE_NEW)
		ret_value = tc956x_add_actual_mac_table(dev, mac, vf);

	reg_value = readl(ioaddr + XGMAC_PACKET_FILTER);
	if (reg_value & XGMAC_FILTER_RA)
		reg_value &= XGMAC_PACKET_FILTER_MASK_PR_EN;
	else
		reg_value &= XGMAC_PACKET_FILTER_MASK_PR_DIS;
	reg_value |= value;
	writel(reg_value, ioaddr + XGMAC_PACKET_FILTER);
	value = readl(ioaddr + XGMAC_PACKET_FILTER);

	return ret_value;
}

#ifdef TC956X_SRIOV_PF
int tc956x_pf_set_mac_filter(struct net_device *dev,
				int vf, const u8 *mac)
{

	if (tc956x_add_sw_mac_table(dev, mac, vf) >= 0)
		return 0;
	else
		return -EPERM;
}

void tc956x_pf_del_mac_filter(struct net_device *dev,
	int vf, const u8 *mac)
{
	tc956x_del_sw_mac_table(dev, mac, vf);
}

void tc956x_pf_del_umac_addr(struct tc956xmac_priv *priv,
				int index, int vf)
{
	struct mac_device_info *hw = priv->hw;

	tc956x_del_mac_addr(priv, hw, index, vf);
	tc956x_del_dma_ch(priv, hw, 0, vf);
}
#endif

#ifdef TC956X_SRIOV_PF
int tc956x_add_mac_addr(struct net_device *dev, const unsigned char *mac)
#elif defined TC956X_SRIOV_VF
static int tc956x_add_mac_addr(struct net_device *dev, const unsigned char *mac)
#endif
{
	int ret_value;
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	struct tc956xmac_priv *priv = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif
	ret_value = tc956x_add_sw_mac_table(dev, mac, PF_DRIVER);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif

	return ret_value;
}

static int tc956x_delete_mac_addr(struct net_device *dev,
	const unsigned char *mac)
{
#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	unsigned long flags;
	struct tc956xmac_priv *priv = netdev_priv(dev);

	spin_lock_irqsave(&priv->spn_lock.mac_filter, flags);
#endif

	tc956x_del_sw_mac_table(dev, mac, PF_DRIVER);

#if defined(TC956X_SRIOV_PF) && defined(TC956X_SRIOV_LOCK)
	spin_unlock_irqrestore(&priv->spn_lock.mac_filter, flags);
#endif


	return 0;
}
static void dwxgmac2_set_filter(struct tc956xmac_priv *priv, struct mac_device_info *hw,
				struct net_device *dev)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	u32 value = readl(ioaddr + XGMAC_PACKET_FILTER);
	u32 i;

#ifndef TC956X_ENABLE_MAC2MAC_BRIDGE
	value &= ~(XGMAC_FILTER_PR | XGMAC_FILTER_HMC | XGMAC_FILTER_PM |
		   XGMAC_FILTER_RA);
#endif
	value |= XGMAC_FILTER_HPF;
#ifndef TC956X_SRIOV_VF
	/* Configuring to Pass all pause frames to application, PHY pause frames will be filtered by FRP */
	if ((mac0_filter_phy_pause == ENABLE && priv->port_num == RM_PF0_ID) ||
	   (mac1_filter_phy_pause == ENABLE && priv->port_num == RM_PF1_ID)) {
		/* setting pcf to 0b10 i.e. pass pause frames of address filter fail to Application */
		value |= 0x80;
	}
#endif
	writel(value, ioaddr + XGMAC_PACKET_FILTER);
	if (dev->flags & IFF_PROMISC) {
#if defined(TC956X_SRIOV_PF) || defined(TC956X_SRIOV_VF) || defined(TC956X_AUTOMOTIVE_CONFIG) || defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
		value |= XGMAC_FILTER_RA;
#else
		value |= XGMAC_FILTER_PR;
#endif
		writel(value, ioaddr + XGMAC_PACKET_FILTER);
	} else if (dev->flags & IFF_ALLMULTI) {
		value |= XGMAC_FILTER_PM;
		writel(value, ioaddr + XGMAC_PACKET_FILTER);
		for (i = 0; i < XGMAC_MAX_HASH_TABLE; i++)
			writel(~0x0, ioaddr + XGMAC_HASH_TABLE(i));
	} else {
		__dev_uc_sync(dev, tc956x_add_mac_addr, tc956x_delete_mac_addr);

		__dev_mc_sync(dev, tc956x_add_mac_addr, tc956x_delete_mac_addr);
	}
#ifdef TC956X
	if (dev->features & NETIF_F_HW_VLAN_CTAG_FILTER)
		dwxgmac2_enable_rx_vlan_filtering(priv, hw);
	else
		dwxgmac2_disable_rx_vlan_filtering(priv, hw);

	if (dev->features & NETIF_F_HW_VLAN_CTAG_RX)
		dwxgmac2_enable_rx_vlan_stripping(priv, hw);
	else
		dwxgmac2_disable_rx_vlan_stripping(priv, hw);
#endif
}

static void dwxgmac2_set_mac_loopback(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, bool enable)
{
	u32 value = readl(ioaddr + XGMAC_RX_CONFIG);

	if (enable)
		value |= XGMAC_CONFIG_LM;
	else
		value &= ~XGMAC_CONFIG_LM;

	writel(value, ioaddr + XGMAC_RX_CONFIG);
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int dwxgmac2_rss_write_reg(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool is_key, int idx,
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

static int dwxgmac2_rss_configure(struct tc956xmac_priv *priv,
				  struct mac_device_info *hw,
				  struct tc956xmac_rss *cfg, u32 num_rxq)
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
		ret = dwxgmac2_rss_write_reg(priv, ioaddr, true, i, key[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(cfg->table); i++) {
		ret = dwxgmac2_rss_write_reg(priv, ioaddr, false, i, cfg->table[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < num_rxq; i++)
		dwxgmac2_map_mtl_to_dma(priv, hw, i, XGMAC_QDDMACH);

	value |= XGMAC_UDP4TE | XGMAC_TCP4TE | XGMAC_IP2TE | XGMAC_RSSE;
	writel(value, ioaddr + XGMAC_RSS_CTRL);
	return 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
static void tc956x_del_vlan_addr(struct tc956xmac_priv *priv, struct mac_device_info *hw, int count)
{
	void __iomem *ioaddr = hw->pcsr;
	unsigned long data = 0;
	int err;

	data = readl(ioaddr + XGMAC_VLAN_TAG_DATA);

	data &= ~XGMAC_VLAN_VID;
	data &= ~XGMAC_VLAN_DMACHE;
	/*writting 0 in vid will pass all the packets*/
	writel(data, ioaddr + XGMAC_VLAN_TAG_DATA);

	data = 0;
	data |= ((count << XGMAC_ADDR_OFFSET_LPOS) & XGMAC_ADDR_OFFSET);
	data &= ~XGMAC_VLAN_CT;/*write command*/
	data |= XGMAC_VLAN_OB;

	writel(data, ioaddr + XGMAC_VLAN_TAG_CTRL);

	err = readl_poll_timeout_atomic(ioaddr + XGMAC_VLAN_TAG_CTRL, data,
									!(data & XGMAC_VLAN_OB), 1, 10);
	if (err < 0)
		netdev_err(priv->dev, "Timeout\n");

}

static void tc956x_vlan_addr_reg(struct tc956xmac_priv *priv, struct mac_device_info *hw, int count,
				 u16 VLAN)
{
	void __iomem *ioaddr = hw->pcsr;
	unsigned long data = 0;

#if defined(TC956X_SRIOV_PF) | defined(TC956X_SRIOV_VF)
	data = 0;
	data |= VLAN;
	data |= XGMAC_VLAN_EN;
	data |= XGMAC_VLAN_ETV_DATA;
	data &= ~XGMAC_VLAN_DMACHE;
	writel(data, ioaddr + XGMAC_VLAN_TAG_DATA);

	data = 0;
#ifdef TC956X_SRIOV_PF
	data = readl(ioaddr + XGMAC_VLAN_TAG_CTRL);
#endif
	data &= ~XGMAC_ADDR_OFFSET;
	data |= (count << XGMAC_ADDR_OFFSET_LPOS);
	data &= ~XGMAC_VLAN_CT;/*write command*/
	data |= XGMAC_VLAN_OB;

	writel(data, ioaddr + XGMAC_VLAN_TAG_CTRL);

	do {
		data = readl(ioaddr + XGMAC_VLAN_TAG_CTRL) & XGMAC_VLAN_OB;
	} while (data);

#else
	data = readl(ioaddr + XGMAC_VLAN_TAG_DATA);
	data |= VLAN;
	data |= XGMAC_VLAN_EN;
	data |= XGMAC_VLAN_DMACHE;
	data |= TC956X_VLAN_DMACH;/*DMAC Channel = 1*/
	writel(data, ioaddr + XGMAC_VLAN_TAG_DATA);

	data = 0;
	data = readl(ioaddr + XGMAC_VLAN_TAG_CTRL);
	data &= ~XGMAC_ADDR_OFFSET;
	data |= (count << XGMAC_ADDR_OFFSET_LPOS);
	data &= ~XGMAC_VLAN_CT;/*write command*/
	data |= XGMAC_VLAN_OB;

	writel(data, ioaddr + XGMAC_VLAN_TAG_CTRL);

	do {
		data = readl(ioaddr + XGMAC_VLAN_TAG_CTRL) & XGMAC_VLAN_OB;
	} while (data);

#endif

}

static void tc956x_del_sw_vlan_helper(struct tc956x_vlan_id *vlan_table,
	u16 vid, u16 vf)
{
	int vf_number;

	for (vf_number = 0; vf_number < 4;
	vf_number++) {
		if (vlan_table->vf[vf_number].vf_number == vf) {
			vlan_table->vf[vf_number].loc_counter--;

			vlan_table->glo_counter--;

			if (vlan_table->vf[vf_number].loc_counter == 0)
				vlan_table->vf[vf_number].vf_number = 0;

			if (vlan_table->glo_counter == 0)
				vlan_table->status = TC956X_MAC_STATE_VACANT;

			break;
		}
	}
}

static void tc956x_del_sw_vlan_table(struct tc956xmac_priv *priv, struct net_device *dev, u16 vid, u16 vf)
{
	int i;
	struct mac_device_info *hw = priv->hw;
	struct tc956x_vlan_id *vlan_table = &priv->vlan_table[0];
	unsigned short new_index, old_index;
	int crc32_val = 0;
	unsigned int enb_12bit_vhash;
	u32 flag = 0;

	for (i = 0; i < (TC956X_MAX_PERFECT_VLAN);
	     i++, vlan_table++) {
		if (vlan_table->status == TC956X_MAC_STATE_OCCUPIED) {
			if (vlan_table->vid == vid) {
				flag = 1;
				tc956x_del_sw_vlan_helper
					(vlan_table, vid, vf);
				break;
			}
		}
	}
	if (flag == 1) {
		if (vlan_table->glo_counter == 0) {
			/* deleting the crc from hast table
			 */
			if (priv->vlan_hash_filtering == 1) {
				/* The upper 4 bits of the calculated CRC
				 * are used to index the content of the VLAN
				 * Hash Table Reg.
				 */
				enb_12bit_vhash = (readl(priv->ioaddr +
				XGMAC_VLAN_TAG) & XGMAC_VLAN_ETV)
				>> XGMAC_VLAN_ETV_LPOS;

				if (enb_12bit_vhash)
					vid = vid & 0xFFF;

				crc32_val = (bitrev32(~crc32_le(~0,
				(unsigned char *)&vid, 2)) >> 28);
				/* These 4(0xF) bits determines the
				 * bit within the VLAN Hash Table Reg 0
				 */

				if (enb_12bit_vhash)
					new_index = (1 << (crc32_val & 0xF));
				else
					new_index = (1 << (~crc32_val & 0xF));

				old_index =
				(readl(priv->ioaddr +
				XGMAC_VLAN_HASH_TABLE) &
				XGMAC_VLAN_VID) >> XGMAC_VLAN_VL_LPOS;
				old_index &= (~new_index);

				writel(old_index, priv->ioaddr +
				XGMAC_VLAN_HASH_TABLE);
			}
			tc956x_del_vlan_addr(priv, hw, i);
		}
	} else {
		KPRINT_INFO("Passed id is not present\n");
	}
}

static int tc956x_add_actual_vlan_table(struct net_device *dev, u16 vid, int vf)
{
	int i;
	int ret_value = -1;
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct mac_device_info *hw = priv->hw;
	unsigned short new_index, old_index;
	int crc32_val = 0;
	unsigned int enb_12bit_vhash;
	u32 flag = 0;
	struct tc956x_vlan_id *vlan_table = &priv->vlan_table[0];

	for (i = 0; i < (TC956X_MAX_PERFECT_VLAN);
	     i++, vlan_table++) {
		if (vlan_table->status & TC956X_MAC_STATE_OCCUPIED)
			continue;

		flag = 1;
		vlan_table->vid = vid;
		vlan_table->status = TC956X_MAC_STATE_OCCUPIED;
		vlan_table->glo_counter++;
		vlan_table->vf[0].vf_number = vf;
		vlan_table->vf[0].loc_counter++;

		ret_value = 0;
		break;
	}
	if (flag == 1) {
		if (priv->vlan_hash_filtering) {
		/* The upper 4 bits of the calculated CRC are used to
		 * index the content of the VLAN Hash Table Reg.
		 */
			enb_12bit_vhash = (readl(priv->ioaddr +
			XGMAC_VLAN_TAG) & XGMAC_VLAN_ETV)
			>> XGMAC_VLAN_ETV_LPOS;

			if (enb_12bit_vhash)
				vid = vid & 0xFFF;
			crc32_val =
			(bitrev32(~crc32_le(~0, (unsigned char *)&vid, 2))
				>> 28);

			if (enb_12bit_vhash)
				new_index = (1 << (crc32_val & 0xF));
			else
				new_index = (1 << (~crc32_val & 0xF));

			old_index = (readl(priv->ioaddr + XGMAC_VLAN_HASH_TABLE) &
				XGMAC_VLAN_VID) >> XGMAC_VLAN_VL_LPOS;

			old_index |= new_index;
			writel(old_index, priv->ioaddr + XGMAC_VLAN_HASH_TABLE);
		}
		tc956x_vlan_addr_reg(priv, hw, i, vid);
	} else {
		KPRINT_INFO("VLAN table is full\n");
	}
	return ret_value;
}


static int tc956x_vlan_duplication_helper(struct tc956x_vlan_id *vlan_table,
						u16 vid, int vf)
{
	int vf_no, vm_found = 0;
	int free_index = 0, ret_value = -1;

	for (vf_no = 0; vf_no < 4; vf_no++) {
		if (vlan_table->vf[vf_no].vf_number == 0) {
			free_index = vf_no;
		} else if (vlan_table->vf[vf_no].vf_number == vf) {
			vm_found = 1;

			vlan_table->vf[vf_no].loc_counter++;

			vlan_table->glo_counter++;

			ret_value =
			TC956X_MAC_STATE_MODIFIED;
			break;
		}
	}

	if (vm_found != 1) {
		vlan_table->vf[free_index].vf_number = vf;

		vlan_table->vf[free_index].loc_counter++;

		vlan_table->glo_counter++;

		ret_value = TC956X_MAC_STATE_MODIFIED;
	}

	return ret_value;
}

static int tc956x_check_vlan_duplication(struct net_device *dev, u16 vid, int vf)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);
	struct tc956x_vlan_id *vlan_table = &priv->vlan_table[0];
	int i;
	int ret_value = -1;

	if (vid == 0) {

		KPRINT_INFO("Zero vlan id\n");

	} else {
		for (i = 0; i < (TC956X_MAX_PERFECT_VLAN); i++, vlan_table++) {
			if (vlan_table->status == TC956X_MAC_STATE_OCCUPIED) {
				if (vid == vlan_table->vid) {
					ret_value =
					tc956x_vlan_duplication_helper
					(vlan_table, vid, vf);
					break;
				} else
					ret_value = TC956X_MAC_STATE_OCCUPIED;
			} else
				ret_value = TC956X_MAC_STATE_NEW;
		}
	}
	return ret_value;
}

static void dwxgmac2_update_vlan_hash(struct tc956xmac_priv *priv,
				      struct net_device *dev, bool is_double,
				      u16 vid, u16 vf)
{
	void __iomem *ioaddr = (void __iomem *)dev->base_addr;
	u32 value, ret_value;

	if (priv->vlan_hash_filtering) {
		value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value |= XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = 0;
		value = readl(ioaddr + XGMAC_VLAN_TAG);
		value |= (XGMAC_VLAN_VTHM | XGMAC_VLAN_ETV);
		if (is_double) {
			value |= XGMAC_VLAN_EDVLP;
			value |= XGMAC_VLAN_ESVL;
			value |= XGMAC_VLAN_DOVLTC;
		}
		writel(value, ioaddr + XGMAC_VLAN_TAG);
	} else {
		value = readl(ioaddr + XGMAC_PACKET_FILTER);

		value |= XGMAC_FILTER_VTFE;

		writel(value, ioaddr + XGMAC_PACKET_FILTER);

		value = 0;
		value = readl(ioaddr + XGMAC_VLAN_TAG);
		value |= XGMAC_VLAN_ETV;
		if (is_double) {
			value |= XGMAC_VLAN_EDVLP;
			value |= XGMAC_VLAN_ESVL;
			value |= XGMAC_VLAN_DOVLTC;
		}
		writel(value, ioaddr + XGMAC_VLAN_TAG);
	}

	ret_value = tc956x_check_vlan_duplication(dev, vid, vf);
	if (ret_value == TC956X_MAC_STATE_NEW)
		ret_value = tc956x_add_actual_vlan_table(dev, vid, vf);

}

#ifdef TC956X_SRIOV_PF

void tc956x_pf_set_vlan_filter(struct net_device *dev, u16 vf, u16 vid)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	dwxgmac2_update_vlan_hash(priv, dev, 0, vid, vf);
}

void tc956x_pf_del_vlan_filter(struct net_device *dev, u16 vf, u16 vid)
{
	struct tc956xmac_priv *priv = netdev_priv(dev);

	tc956x_del_sw_vlan_table(priv, dev, vid, vf);
}
#endif

struct dwxgmac3_error_desc {
	bool valid;
	const char *desc;
	const char *detailed_desc;
};

#define STAT_OFF(field)	offsetof(struct tc956xmac_safety_stats, field)

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static void dwxgmac3_log_error(struct net_device *ndev, u32 value, bool corr,
			       const char *module_name,
			       const struct dwxgmac3_error_desc *desc,
			       unsigned long field_offset,
			       struct tc956xmac_safety_stats *stats)
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

static const struct dwxgmac3_error_desc dwxgmac3_mac_errors[32] = {
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
				    struct tc956xmac_safety_stats *stats)
{
	u32 value;
	struct tc956xmac_priv *priv = netdev_priv(ndev);

	value = readl(ioaddr + XGMAC_MAC_DPP_FSM_INT_STATUS);
	writel(value, ioaddr + XGMAC_MAC_DPP_FSM_INT_STATUS);

	dwxgmac3_log_error(ndev, value, correctable, "MAC",
			   dwxgmac3_mac_errors, STAT_OFF(mac_errors), stats);
}

static const struct dwxgmac3_error_desc dwxgmac3_mtl_errors[32] = {
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
				    struct tc956xmac_safety_stats *stats)
{
	u32 value;
	struct tc956xmac_priv *priv = netdev_priv(ndev);

	value = readl(ioaddr + XGMAC_MTL_ECC_INT_STATUS);
	writel(value, ioaddr + XGMAC_MTL_ECC_INT_STATUS);

	dwxgmac3_log_error(ndev, value, correctable, "MTL",
			   dwxgmac3_mtl_errors, STAT_OFF(mtl_errors), stats);
}

static const struct dwxgmac3_error_desc dwxgmac3_dma_errors[32] = {
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

static void dwxgmac3_handle_dma_err(struct net_device *ndev,
				    void __iomem *ioaddr, bool correctable,
				    struct tc956xmac_safety_stats *stats)
{
	u32 value;
	struct tc956xmac_priv *priv = netdev_priv(ndev);

	value = readl(ioaddr + XGMAC_DMA_ECC_INT_STATUS);
	writel(value, ioaddr + XGMAC_DMA_ECC_INT_STATUS);

	dwxgmac3_log_error(ndev, value, correctable, "DMA",
			   dwxgmac3_dma_errors, STAT_OFF(dma_errors), stats);
}

static int dwxgmac3_safety_feat_config(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, unsigned int asp)
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

	return 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static int dwxgmac3_safety_feat_irq_status(struct tc956xmac_priv *priv,
					   struct net_device *ndev,
					   void __iomem *ioaddr,
					   unsigned int asp,
					   struct tc956xmac_safety_stats *stats)
{
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	bool err, corr;
	u32 mtl, dma;
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	int ret = 0;

	if (!asp)
		return -EINVAL;

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE

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

	err = dma & (XGMAC_DEUIS | XGMAC_DECIS);
	corr = dma & XGMAC_DECIS;
	if (err) {
		dwxgmac3_handle_dma_err(ndev, ioaddr, corr, stats);
		ret |= !corr;
	}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

	return ret;
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static const struct dwxgmac3_error {
	const struct dwxgmac3_error_desc *desc;
} dwxgmac3_all_errors[] = {
	{ dwxgmac3_mac_errors },
	{ dwxgmac3_mtl_errors },
	{ dwxgmac3_dma_errors },
};

static int dwxgmac3_safety_feat_dump(struct tc956xmac_priv *priv,
				     struct tc956xmac_safety_stats *stats,
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
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
static int dwxgmac3_rxp_disable(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	int limit;
	u32 val = readl(ioaddr + XGMAC_MTL_OPMODE);

	val &= ~XGMAC_FRPE;
	writel(val, ioaddr + XGMAC_MTL_OPMODE);

	limit = 10000;
	while (limit--) {
		if (!(readl(ioaddr + XGMAC_MTL_RXP_CONTROL_STATUS) & XGMAC_RXPI))
			break;
		udelay(1);
	}

	if (limit < 0)
		return -EBUSY;

	return 0;
}

static void dwxgmac3_rxp_enable(struct tc956xmac_priv *priv, void __iomem *ioaddr)
{
	u32 val;

	val = readl(ioaddr + XGMAC_MTL_OPMODE);
	val |= XGMAC_FRPE;
	writel(val, ioaddr + XGMAC_MTL_OPMODE);
}

static int dwxgmac3_rxp_update_single_entry(struct tc956xmac_priv *priv, void __iomem *ioaddr,
					    struct tc956xmac_tc_entry *entry,
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

static struct tc956xmac_tc_entry *
dwxgmac3_rxp_get_next_entry(struct tc956xmac_tc_entry *entries,
			    unsigned int count, u32 curr_prio)
{
	struct tc956xmac_tc_entry *entry;
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

static int dwxgmac2_rx_parser_write_entry(struct tc956xmac_priv *priv, struct mac_device_info *hw,
		struct tc956xmac_rx_parser_entry *entry, int entry_pos)
{
	void __iomem *ioaddr = hw->pcsr;
	int limit;
	int i;

	for (i = 0; i < (sizeof(*entry) / sizeof(u32)); i++) {
		int real_pos = entry_pos * (sizeof(*entry) / sizeof(u32)) + i;
		u32 value = *((u32 *)entry + i);

		writel(value, ioaddr + XGMAC_MTL_RXP_IACC_DATA);

		value = real_pos & XGMAC_ADDR;
		writel(value, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);

		limit = 10000;
		while (limit--) {
			if (!(readl(ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST) &
			      XGMAC_STARTBUSY))
				break;
			udelay(1);
		}
		if (limit < 0)
			return -EBUSY;
		/* Write op */
		value |= XGMAC_WRRDN;
		writel(value, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);

		/* Start write */
		value |= XGMAC_STARTBUSY;
		writel(value, ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST);

		/* Wait for done */
		limit = 10000;
		while (limit--) {
			if (!(readl(ioaddr + XGMAC_MTL_RXP_IACC_CTRL_ST) &
			      XGMAC_STARTBUSY))
				break;
			udelay(1);
		}
		if (limit < 0)
			return -EBUSY;
	}

	return 0;
}

static int dwxgmac2_rx_parser_config(struct tc956xmac_priv *priv, struct mac_device_info *hw,
				     struct tc956xmac_rx_parser_cfg *cfg)
{
	void __iomem *ioaddr = hw->pcsr;
	int i, ret;
	u32 value;

	if (cfg->npe <= 0 || cfg->nve <= 0)
		return -EINVAL;

	value = (cfg->npe - 1) << 16;
	value |= (cfg->nve - 1) << 0;
	writel(value, ioaddr + XGMAC_MTL_RXP_CONTROL_STATUS);

	for (i = 0; i < cfg->nve; i++) {
		ret = dwxgmac2_rx_parser_write_entry(priv, hw, &cfg->entries[i], i);
		if (ret)
			return ret;
	}

	return 0;
}

static int dwxgmac2_rx_parser_init(struct tc956xmac_priv *priv,
			struct net_device *ndev, struct mac_device_info *hw,
			unsigned int spram, unsigned int frpsel, unsigned int frpes,
			struct tc956xmac_rx_parser_cfg *cfg)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value, old_value = readl(ioaddr + XGMAC_RX_CONFIG);
	int ret, max_entries = frpes;

	if (!cfg || !frpsel)
		return -EINVAL;
	if (cfg->nve > max_entries || cfg->npe > max_entries) {
		netdev_err(ndev, "Invalid RX Parser configuration supplied\n");
		return -EINVAL;
	}

	/* Force disable RX */
	value = old_value & ~XGMAC_CONFIG_RE;
	writel(value, ioaddr + XGMAC_RX_CONFIG);

	/* Disable RX Parser */
	ret = dwxgmac3_rxp_disable(priv, ioaddr);
	if (ret) {
		netdev_err(ndev, "Failed to disable RX Parser\n");
		return ret;
	}

	/* Nothing to do if we don't want to enable RX Parser */
	if (cfg->nve <= 0 || cfg->npe <= 0) {
		/* Restore RX to previous state */
		writel(old_value, ioaddr + XGMAC_RX_CONFIG);
		return -EINVAL;
	}

	/* Store table */
	ret = dwxgmac2_rx_parser_config(priv, hw, cfg);
	if (ret) {
		netdev_err(ndev, "Failed to configure RX Parser\n");
		return ret;
	}

	/* Enable RX Parser */
	dwxgmac3_rxp_enable(priv, ioaddr);

	/* Restore RX to previous state */
	writel(old_value, ioaddr + XGMAC_RX_CONFIG);

	netdev_info(ndev, "Enabling RX Parser\n");
	return 0;
}
static int dwxgmac3_rxp_config(struct tc956xmac_priv *priv, void __iomem *ioaddr,
			       struct tc956xmac_tc_entry *entries,
			       unsigned int count)
{
	struct tc956xmac_tc_entry *entry, *frag;
	int i, ret, nve = 0;
	u32 curr_prio = 0;
	u32 old_val, val;

	/* Force disable RX */
	old_val = readl(ioaddr + XGMAC_RX_CONFIG);
	val = old_val & ~XGMAC_CONFIG_RE;
	writel(val, ioaddr + XGMAC_RX_CONFIG);

	/* Disable RX Parser */
	ret = dwxgmac3_rxp_disable(priv, ioaddr);
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

		ret = dwxgmac3_rxp_update_single_entry(priv, ioaddr, entry, nve);
		if (ret)
			goto re_enable;

		entry->table_pos = nve++;
		entry->in_hw = true;

		if (frag && !frag->in_hw) {
			ret = dwxgmac3_rxp_update_single_entry(priv, ioaddr, frag, nve);
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

		ret = dwxgmac3_rxp_update_single_entry(priv, ioaddr, entry, nve);
		if (ret)
			goto re_enable;

		entry->table_pos = nve++;
	}

	/* Assume n. of parsable entries == n. of valid entries */
	val = (nve << 16) & XGMAC_NPE;
	val |= nve & XGMAC_NVE;
	writel(val, ioaddr + XGMAC_MTL_RXP_CONTROL_STATUS);

	/* Enable RX Parser */
	dwxgmac3_rxp_enable(priv, ioaddr);

re_enable:
	/* Re-enable RX */
	writel(old_val, ioaddr + XGMAC_RX_CONFIG);
	return ret;
}

static int dwxgmac2_get_mac_tx_timestamp(struct tc956xmac_priv *priv,
					struct mac_device_info *hw, u64 *ts)
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

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int dwxgmac2_flex_pps_config(struct tc956xmac_priv *priv,
				    void __iomem *ioaddr, int index,
				    struct tc956xmac_pps_cfg *cfg, bool enable,
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
	val |= XGMAC_PPSEN0;

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

static void dwxgmac2_sarc_configure(struct tc956xmac_priv *priv, void __iomem *ioaddr, int val)
{
	u32 value = readl(ioaddr + XGMAC_TX_CONFIG);

	value &= ~XGMAC_CONFIG_SARC;
	value |= val << XGMAC_CONFIG_SARC_SHIFT;

	writel(value, ioaddr + XGMAC_TX_CONFIG);
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static void dwxgmac2_enable_vlan(struct tc956xmac_priv *priv,
				struct mac_device_info *hw, u32 type)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_VLAN_INCL);
	value |= XGMAC_VLAN_VLTI;
	value &= ~XGMAC_VLAN_CSVL; /* Only use CVLAN */
	if (priv->dev->features & NETIF_F_HW_VLAN_STAG_TX)
		value |= XGMAC_VLAN_CSVL; /* Only use SVLAN */
	value &= ~XGMAC_VLAN_VLC;
	value |= (type << XGMAC_VLAN_VLC_SHIFT) & XGMAC_VLAN_VLC;
	writel(value, ioaddr + XGMAC_VLAN_INCL);
}
#ifdef TC956X
static void dwxgmac2_disable_tx_vlan(struct tc956xmac_priv *priv,
				struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_VLAN_INCL);
	value &= ~XGMAC_VLAN_VLTI;
	value &= ~XGMAC_VLAN_VLC;
	writel(value, ioaddr + XGMAC_VLAN_INCL);
}

static void dwxgmac2_enable_rx_vlan_stripping(struct tc956xmac_priv *priv,
				struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_VLAN_TAG_CTRL);
	/* Put the VLAN tag in the Rx descriptor */
	value |= XGMAC_VLAN_EVLRXS;

	/* Don't check the VLAN type */
	value |= XGMAC_VLAN_DOVLTC;

	/* Check only C-TAG (0x8100) packets */
	value &= ~XGMAC_VLAN_ERSVLM;

	/* Don't consider an S-TAG (0x88A8) packet as a VLAN packet */
	value &= ~XGMAC_VLAN_ESVL;

	/* Enable VLAN tag stripping */
	value |= XGMAC_VLAN_EVLS;
	writel(value, ioaddr + XGMAC_VLAN_TAG_CTRL);
}

static void dwxgmac2_disable_rx_vlan_stripping(struct tc956xmac_priv *priv,
				struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_VLAN_TAG_CTRL);
	/* Disable VLAN tag stripping */
	value &= ~XGMAC_VLAN_EVLS;
	writel(value, ioaddr + XGMAC_VLAN_TAG_CTRL);
}

static void dwxgmac2_enable_rx_vlan_filtering(struct tc956xmac_priv *priv,
				struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_PACKET_FILTER);
	/* Enable VLAN filtering */
	value |= XGMAC_FILTER_VTFE;
	writel(value, ioaddr + XGMAC_PACKET_FILTER);

	writel(value, ioaddr + XGMAC_VLAN_TAG_CTRL);
}

static void dwxgmac2_disable_rx_vlan_filtering(struct tc956xmac_priv *priv,
				struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	value = readl(ioaddr + XGMAC_PACKET_FILTER);
	/* Enable VLAN filtering */
	value &= ~XGMAC_FILTER_VTFE;
	writel(value, ioaddr + XGMAC_PACKET_FILTER);
}
#endif

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int dwxgmac2_filter_wait(struct tc956xmac_priv *priv, struct mac_device_info *hw)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;

	if (readl_poll_timeout(ioaddr + XGMAC_L3L4_ADDR_CTRL, value,
			       !(value & XGMAC_XB), 100, 10000))
		return -EBUSY;
	return 0;
}

static int dwxgmac2_filter_read(struct tc956xmac_priv *priv, struct mac_device_info *hw,
				u32 filter_no, u8 reg, u32 *data)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	int ret;

	ret = dwxgmac2_filter_wait(priv, hw);
	if (ret)
		return ret;

	value = ((filter_no << XGMAC_IDDR_FNUM) | reg) << XGMAC_IDDR_SHIFT;
	value |= XGMAC_TT | XGMAC_XB;
	writel(value, ioaddr + XGMAC_L3L4_ADDR_CTRL);

	ret = dwxgmac2_filter_wait(priv, hw);
	if (ret)
		return ret;

	*data = readl(ioaddr + XGMAC_L3L4_DATA);
	return 0;
}

static int dwxgmac2_filter_write(struct tc956xmac_priv *priv, struct mac_device_info *hw, u32 filter_no,
				 u8 reg, u32 data)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	int ret;

	ret = dwxgmac2_filter_wait(priv, hw);
	if (ret)
		return ret;

	writel(data, ioaddr + XGMAC_L3L4_DATA);

	value = ((filter_no << XGMAC_IDDR_FNUM) | reg) << XGMAC_IDDR_SHIFT;
	value |= XGMAC_XB;
	writel(value, ioaddr + XGMAC_L3L4_ADDR_CTRL);

	return dwxgmac2_filter_wait(priv, hw);
}

static int dwxgmac2_config_l3_filter(struct tc956xmac_priv *priv,
				     struct mac_device_info *hw, u32 filter_no,
				     bool en, bool ipv6, bool sa, bool inv,
				     u32 match)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	int ret;

	value = readl(ioaddr + XGMAC_PACKET_FILTER);
	value |= XGMAC_FILTER_IPFE;
	writel(value, ioaddr + XGMAC_PACKET_FILTER);

	ret = dwxgmac2_filter_read(priv, hw, filter_no, XGMAC_L3L4_CTRL, &value);
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

	ret = dwxgmac2_filter_write(priv, hw, filter_no, XGMAC_L3L4_CTRL, value);
	if (ret)
		return ret;

	if (sa) {
		ret = dwxgmac2_filter_write(priv, hw, filter_no, XGMAC_L3_ADDR0, match);
		if (ret)
			return ret;
	} else {
		ret = dwxgmac2_filter_write(priv, hw, filter_no, XGMAC_L3_ADDR1, match);
		if (ret)
			return ret;
	}

	if (!en)
		return dwxgmac2_filter_write(priv, hw, filter_no, XGMAC_L3L4_CTRL, 0);

	return 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
static void dwxgmac2_debug(struct tc956xmac_priv *priv, void __iomem *ioaddr,
			 struct tc956xmac_extra_stats *x,
			 u32 rx_queues, u32 tx_queues)
{
	u32 value;
	u32 queue;

	for (queue = 0; queue < tx_queues; queue++) {
#ifdef TC956X_SRIOV_VF
		if (priv->plat->tx_q_in_use[queue] == 0)
			continue;
#endif
		value = readl(ioaddr + XGMAC_MTL_TXQ_Debug(queue));

		if (value & XGMAC_MTL_DEBUG_TXQSTS)
			x->mtl_tx_fifo_not_empty[queue]++;
		if (value & XGMAC_MTL_DEBUG_TWCSTS)
			x->mmtl_fifo_ctrl[queue]++;
		if (value & XGMAC_MTL_DEBUG_TRCSTS_MASK) {
			u32 trcsts = (value & XGMAC_MTL_DEBUG_TRCSTS_MASK)
				     >> XGMAC_MTL_DEBUG_TRCSTS_SHIFT;
			if (trcsts == XGMAC_MTL_DEBUG_TRCSTS_WRITE)
				x->mtl_tx_fifo_read_ctrl_write[queue]++;
			else if (trcsts == XGMAC_MTL_DEBUG_TRCSTS_TXW)
				x->mtl_tx_fifo_read_ctrl_wait[queue]++;
			else if (trcsts == XGMAC_MTL_DEBUG_TRCSTS_READ)
				x->mtl_tx_fifo_read_ctrl_read[queue]++;
			else
				x->mtl_tx_fifo_read_ctrl_idle[queue]++;
		}
		if (value & XGMAC_MTL_DEBUG_TCPAUSED)
			x->mac_tx_in_pause[queue]++;
	}

	for (queue = 0; queue < rx_queues; queue++) {
#ifdef TC956X_SRIOV_VF
		if (priv->plat->rx_q_in_use[queue] == 0)
			continue;
#endif
		value = readl(ioaddr + XGMAC_MTL_RXQ_Debug(queue));

		if (value & XGMAC_MTL_DEBUG_RXQSTS_MASK) {
			u32 rxfsts = (value & XGMAC_MTL_DEBUG_RXQSTS_MASK)
				     >> XGMAC_MTL_DEBUG_RXQSTS_SHIFT;

			if (rxfsts == XGMAC_MTL_DEBUG_RXQSTS_FULL)
				x->mtl_rx_fifo_fill_level_full[queue]++;
			else if (rxfsts == XGMAC_MTL_DEBUG_RXQSTS_AT)
				x->mtl_rx_fifo_fill_above_thresh[queue]++;
			else if (rxfsts == XGMAC_MTL_DEBUG_RXQSTS_BT)
				x->mtl_rx_fifo_fill_below_thresh[queue]++;
			else
				x->mtl_rx_fifo_fill_level_empty[queue]++;
		}
		if (value & XGMAC_MTL_DEBUG_RRCSTS_MASK) {
			u32 rrcsts = (value & XGMAC_MTL_DEBUG_RRCSTS_MASK) >>
				     XGMAC_MTL_DEBUG_RRCSTS_SHIFT;

			if (rrcsts == XGMAC_MTL_DEBUG_RRCSTS_FLUSH)
				x->mtl_rx_fifo_read_ctrl_flush[queue]++;
			else if (rrcsts == XGMAC_MTL_DEBUG_RRCSTS_RSTAT)
				x->mtl_rx_fifo_read_ctrl_read[queue]++;
			else if (rrcsts == XGMAC_MTL_DEBUG_RRCSTS_RDATA)
				x->mtl_rx_fifo_read_ctrl_status[queue]++;
			else
				x->mtl_rx_fifo_read_ctrl_idle[queue]++;
		}
		if (value & XGMAC_MTL_DEBUG_RWCSTS)
			x->mtl_rx_fifo_ctrl_active[queue]++;
	}

	/* GMAC debug */
	value = readl(ioaddr + XGMAC_DEBUG);

	if (value & XGMAC_DEBUG_TFCSTS_MASK) {
		u32 tfcsts = (value & XGMAC_DEBUG_TFCSTS_MASK)
			      >> XGMAC_DEBUG_TFCSTS_SHIFT;

		if (tfcsts == XGMAC_DEBUG_TFCSTS_XFER)
			x->mac_tx_frame_ctrl_xfer++;
		else if (tfcsts == XGMAC_DEBUG_TFCSTS_GEN_PAUSE)
			x->mac_tx_frame_ctrl_pause++;
		else if (tfcsts == XGMAC_DEBUG_TFCSTS_WAIT)
			x->mac_tx_frame_ctrl_wait++;
		else
			x->mac_tx_frame_ctrl_idle++;
	}
	if (value & XGMAC_DEBUG_TPESTS)
		x->mac_gmii_tx_proto_engine++;
	if (value & XGMAC_DEBUG_RFCFCSTS_MASK)
		x->mac_rx_frame_ctrl_fifo = (value & XGMAC_DEBUG_RFCFCSTS_MASK);
	if (value & XGMAC_DEBUG_RPESTS)
		x->mac_gmii_rx_proto_engine++;

}
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int dwxgmac2_config_l4_filter(struct tc956xmac_priv *priv,
				     struct mac_device_info *hw, u32 filter_no,
				     bool en, bool udp, bool sa, bool inv,
				     u32 match)
{
	void __iomem *ioaddr = hw->pcsr;
	u32 value;
	int ret;

	value = readl(ioaddr + XGMAC_PACKET_FILTER);
	value |= XGMAC_FILTER_IPFE;
	writel(value, ioaddr + XGMAC_PACKET_FILTER);

	ret = dwxgmac2_filter_read(priv, hw, filter_no, XGMAC_L3L4_CTRL, &value);
	if (ret)
		return ret;

	if (udp)
		value |= XGMAC_L4PEN0;
	else
		value &= ~XGMAC_L4PEN0;

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

	ret = dwxgmac2_filter_write(priv, hw, filter_no, XGMAC_L3L4_CTRL, value);
	if (ret)
		return ret;

	if (sa) {
		value = match & XGMAC_L4SP0;

		ret = dwxgmac2_filter_write(priv, hw, filter_no, XGMAC_L4_ADDR, value);
		if (ret)
			return ret;
	} else {
		value = (match << XGMAC_L4DP0_SHIFT) & XGMAC_L4DP0;

		ret = dwxgmac2_filter_write(priv, hw, filter_no, XGMAC_L4_ADDR, value);
		if (ret)
			return ret;
	}

	if (!en)
		return dwxgmac2_filter_write(priv, hw, filter_no, XGMAC_L3L4_CTRL, 0);

	return 0;
}

static void dwxgmac2_set_arp_offload(struct tc956xmac_priv *priv,
				     struct mac_device_info *hw, bool en, u32 addr)
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
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

#ifdef DEBUG_TSN
static int dwxgmac3_est_read(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 reg, u32 *val, bool gcl)
{
	u32 ctrl = 0x0;

#ifdef GCL_PRINT
	static unsigned int count;

	if (count % 8000 == 0) {
		if (readl(ioaddr + XGMAC_MTL_EST_STATUS) & XGMAC_SWOL)
			pr_info("GCL0:hw\n");
		else
			pr_info("GCL1:hw\n");
	}
	count++;
#endif

	if (readl(ioaddr + XGMAC_MTL_EST_STATUS) & XGMAC_SWOL)
		ctrl &= (~(1 << 5));    /* Forcing to read bank 0 */
	else
		ctrl |= (1 << 5);	/* Forcing to read bank 1  */


	ctrl |= XGMAC_DBGM;	/* Debug mode enable   */
	ctrl |= reg;
	ctrl |= gcl ? 0x0 : XGMAC_GCRR;
	ctrl |= XGMAC_R1W0;

	ctrl |= XGMAC_SRWO;
	writel(ctrl, ioaddr + XGMAC_MTL_EST_GCL_CONTROL);

	if (readl_poll_timeout_atomic(ioaddr + XGMAC_MTL_EST_GCL_CONTROL,
				      ctrl, !(ctrl & XGMAC_SRWO), 100, 5000))
		return -ETIMEDOUT;

	*val = readl(ioaddr + XGMAC_MTL_EST_GCL_DATA);
	return 0;
}
#endif

static int dwxgmac3_est_write(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 reg, u32 val, bool gcl)
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

static int dwxgmac3_est_configure(struct tc956xmac_priv *priv,
				  void __iomem *ioaddr, struct tc956xmac_est *cfg,
				  unsigned int ptp_rate)
{
	int i, ret = 0x0;
	u32 ctrl, reg_data;

#if defined(TX_LOGGING_TRACE)
	int j;
	u64 read_btr = 0, read_ctr = 0;
	static u32 switch_cnt;
	char *qptr = NULL, *pptr = NULL;
	int char_buff_size = 100*100;

	pptr = (char *)kzalloc(100*100, GFP_KERNEL);
	if (!pptr) {
		pr_err("Malloc pptr Error\n");
		return -ENOMEM;
	}
	qptr = (char *)kzalloc(100*100, GFP_KERNEL);
	if (!qptr) {
		pr_err("Malloc qptr Error\n");
		kfree(pptr);
		return -ENOMEM;
	}
#endif

	ret |= dwxgmac3_est_write(priv, ioaddr, XGMAC_BTR_LOW, cfg->btr[0], false);
	ret |= dwxgmac3_est_write(priv, ioaddr, XGMAC_BTR_HIGH, cfg->btr[1], false);
	ret |= dwxgmac3_est_write(priv, ioaddr, XGMAC_TER, cfg->ter, false);
	ret |= dwxgmac3_est_write(priv, ioaddr, XGMAC_LLR, cfg->gcl_size, false);
	ret |= dwxgmac3_est_write(priv, ioaddr, XGMAC_CTR_LOW, cfg->ctr[0], false);
	ret |= dwxgmac3_est_write(priv, ioaddr, XGMAC_CTR_HIGH, cfg->ctr[1], false);
	if (ret)
		return ret;

	netdev_dbg(priv->dev, "%s: EST BTR Low = 0x%x", __func__, cfg->btr[0]);
	netdev_dbg(priv->dev, "%s: EST BTR High = 0x%x", __func__, cfg->btr[1]);
	netdev_dbg(priv->dev, "%s: EST TER = 0x%x", __func__, cfg->ter);
	netdev_dbg(priv->dev, "%s: EST LLR = 0x%x", __func__, cfg->gcl_size);
	netdev_dbg(priv->dev, "%s: EST CTR Low = 0x%x", __func__, cfg->ctr[0]);
	netdev_dbg(priv->dev, "%s: EST CTR High = 0x%x", __func__, cfg->ctr[1]);

	for (i = 0; i < cfg->gcl_size; i++) {
		ret = dwxgmac3_est_write(priv, ioaddr, i, cfg->gcl[i], true);
		if (ret)
			return ret;
		netdev_dbg(priv->dev, "%s: EST GCL[%d] = 0x%x", __func__, i, cfg->gcl[i]);
	}

#if defined(TX_LOGGING_TRACE) /* Log the actual time of BTR to be used for comparision */
	{
		read_btr = ((u64)cfg->btr[1] * 1000000000ULL) + cfg->btr[0];
		read_ctr = ((u64)((cfg->ctr[1] & 0xff) * 1000000000ULL)) + cfg->ctr[0];

		if (read_btr != 0) {
			/* [EST]TS,<BTR Value>,<CTR Value>,<IPG of class A>,<IPG of class B> */
			trace_printk("[EST[%d]]TS,%llu,%llu,%d,%d\n",
					switch_cnt, read_btr, read_ctr,
						PACKET_IPG, PACKET_CDT_IPG);
			for (i = 0; i < cfg->gcl_size; i++) {
				scnprintf((pptr+(i*11)), char_buff_size - (i*11), ",%010d", (cfg->gcl[i]&0xffffff));
				for (j = 0; j < MTL_MAX_TX_TC; j++) {
					scnprintf((qptr+(i*5*2)+(j*2)), char_buff_size - (i*5*2) + (j*2), ",%d",
						((cfg->gcl[i]>>(24+j))&0x1));
				}
			}
			trace_printk("[GCL_TI[%d]]TS%s\n", switch_cnt, pptr);
			trace_printk("[GCL_ROW[%d]]TS%s\n", switch_cnt, qptr);
		}
		switch_cnt++;
	}
#endif


	if (readl(ioaddr + XGMAC_MTL_EST_STATUS) & XGMAC_SWOL)
		pr_alert("GCL 1 is used by Software\n");
	else
		pr_alert("GCL 0 is used by Software\n");



	ctrl = readl(ioaddr + XGMAC_MTL_EST_CONTROL);
	ctrl &= ~XGMAC_PTOV;
	ctrl |= ((1000000000 / ptp_rate) * 9) << XGMAC_PTOV_SHIFT;
	if (cfg->enable)
		ctrl |= XGMAC_EEST | XGMAC_SSWL;
	else
		ctrl &= ~XGMAC_EEST;

	writel(ctrl, ioaddr + XGMAC_MTL_EST_CONTROL);

	/* Use Normal/Absolute mode for Launch Time */
	reg_data = readl(ioaddr + XGMAC_MTL_TBS_CTRL);
	writel(reg_data & (~XGMAC_ESTM),  ioaddr + XGMAC_MTL_TBS_CTRL);


	reg_data = readl(ioaddr + XGMAC_MTL_TBS_CTRL);
	reg_data &= ~XGMAC_LEOS;

	if ((readl(ioaddr + XGMAC_MTL_TBS_CTRL)) & XGMAC_ESTM)
		/* Launch expiry offset (~124us) in units of 256ns, Launch Expiry Offset Valid bit */
		reg_data |=  (EST_LEOS << XGMAC_LEOS_SHIFT);
	else
		/* Launch expiry offset (~50ms) in units of 256ns, Launch Expiry Offset Valid bit */
		reg_data |=  (ABSOLUTE_LEOS << XGMAC_LEOS_SHIFT);

	writel(reg_data, ioaddr + XGMAC_MTL_TBS_CTRL);


#if defined(TX_LOGGING_TRACE)
	kfree(qptr);
	kfree(pptr);
#endif
	return 0;
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static void dwxgmac3_fpe_configure(struct tc956xmac_priv *priv,
				   void __iomem *ioaddr, u32 num_txq,
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

static void dwxgmac3_set_ptp_offload(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, bool en)
{
	u32 value;

	value = readl(ioaddr + XGMAC_PTO_CTRL);
	if (en)
		value |= XGMAC_PTOEN | XGMAC_APDREQEN | XGMAC_ASYNCEN;
	else
		value &= ~XGMAC_PTOEN;
	writel(value, ioaddr + XGMAC_PTO_CTRL);
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

/**
 * tc956x_enable_jumbo_frm - Enable jumbo frame support for Tx and Rx in EMAC.
 *
 * @dev: pointer to net_device structure
 * @en: Enable/Disable
 */
static void tc956x_enable_jumbo_frm(struct tc956xmac_priv *priv,
					struct net_device *dev, u32 en)
{
	u32 value_tx = readl(priv->ioaddr + XGMAC_TX_CONFIG);
	u32 value_rx = readl(priv->ioaddr + XGMAC_RX_CONFIG);

	if (en) {

		value_tx |= XGMAC_CONFIG_JD;
		netdev_dbg(priv->dev, "%s: Jumbo frame enabled with size = %d",
			__func__, dev->mtu);
	} else {

		value_tx &= ~XGMAC_CONFIG_JD;
		value_rx &= ~XGMAC_CONFIG_JE;

		netdev_dbg(priv->dev, "%s: Jumbo frame disabled with size = %d",
			__func__, dev->mtu);
	}

	writel(value_tx, priv->ioaddr + XGMAC_TX_CONFIG);
	writel(value_rx, priv->ioaddr + XGMAC_RX_CONFIG);

}

const struct tc956xmac_ops dwxgmac210_ops = {
	.core_init = dwxgmac2_core_init,
	.set_mac = dwxgmac2_set_mac,
	.set_mac_tx = dwxgmac2_set_mac_tx,
	.set_mac_rx = dwxgmac2_set_mac_rx,
	.rx_ipc = dwxgmac2_rx_ipc,
	.rx_queue_enable = dwxgmac2_rx_queue_enable,
	.rx_queue_prio = dwxgmac2_rx_queue_prio,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.tx_queue_prio = dwxgmac2_tx_queue_prio,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.rx_queue_routing = tc956x_rx_queue_routing,
	.prog_mtl_rx_algorithms = dwxgmac2_prog_mtl_rx_algorithms,
	.prog_mtl_tx_algorithms = dwxgmac2_prog_mtl_tx_algorithms,
	.set_mtl_tx_queue_weight = dwxgmac2_set_mtl_tx_queue_weight,
	.map_mtl_to_dma = dwxgmac2_map_mtl_to_dma,
	.config_cbs = dwxgmac2_config_cbs,
	.dump_regs = dwxgmac2_dump_regs,
	.host_irq_status = dwxgmac2_host_irq_status,
	.host_mtl_irq_status = dwxgmac2_host_mtl_irq_status,
	.flow_ctrl = dwxgmac2_flow_ctrl,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.pmt = dwxgmac2_pmt,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.set_umac_addr = dwxgmac2_set_umac_addr,
	.get_umac_addr = dwxgmac2_get_umac_addr,
	.set_eee_mode = dwxgmac2_set_eee_mode,
	.reset_eee_mode = dwxgmac2_reset_eee_mode,
	.set_eee_timer = dwxgmac2_set_eee_timer,
	.set_eee_pls = dwxgmac2_set_eee_pls,
	.pcs_ctrl_ane = NULL,
	.pcs_rane = NULL,
	.pcs_get_adv_lp = NULL,
#ifndef TC956X_SRIOV_VF
#ifdef TC956X
	.xpcs_init = tc956x_xpcs_init,
	.xpcs_ctrl_ane = tc956x_xpcs_ctrl_ane,
#endif
#endif
	.debug = dwxgmac2_debug,
	.set_filter = dwxgmac2_set_filter,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.safety_feat_config = dwxgmac3_safety_feat_config,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.safety_feat_irq_status = dwxgmac3_safety_feat_irq_status,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.safety_feat_dump = dwxgmac3_safety_feat_dump,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.set_mac_loopback = dwxgmac2_set_mac_loopback,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.rss_configure = dwxgmac2_rss_configure,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.update_vlan_hash = dwxgmac2_update_vlan_hash,
	.delete_vlan = tc956x_del_sw_vlan_table,
	.rx_parser_init = dwxgmac2_rx_parser_init,
	.rxp_config = dwxgmac3_rxp_config,
	.get_mac_tx_timestamp = dwxgmac2_get_mac_tx_timestamp,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.flex_pps_config = dwxgmac2_flex_pps_config,
	.sarc_configure = dwxgmac2_sarc_configure,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.enable_vlan = dwxgmac2_enable_vlan,
#ifdef TC956X
	.disable_tx_vlan = dwxgmac2_disable_tx_vlan,
	.enable_rx_vlan_stripping = dwxgmac2_enable_rx_vlan_stripping,
	.disable_rx_vlan_stripping = dwxgmac2_disable_rx_vlan_stripping,
	.enable_rx_vlan_filtering = dwxgmac2_enable_rx_vlan_filtering,
	.disable_rx_vlan_filtering = dwxgmac2_disable_rx_vlan_filtering,
#endif
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.config_l3_filter = dwxgmac2_config_l3_filter,
	.config_l4_filter = dwxgmac2_config_l4_filter,
	.set_arp_offload = dwxgmac2_set_arp_offload,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.est_configure = dwxgmac3_est_configure,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.fpe_configure = dwxgmac3_fpe_configure,
	.set_ptp_offload = dwxgmac3_set_ptp_offload,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.jumbo_en = tc956x_enable_jumbo_frm,
};

int dwxgmac2_setup(struct tc956xmac_priv *priv)
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
