// SPDX-License-Identifier: GPL-2.0
/* Texas Instruments K3 AM65 Ethernet Switch SubSystem Driver
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
 *
 */

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kmemleak.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_device.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/dma/ti-cppi5.h>
#include <linux/dma/k3-udma-glue.h>

#include "cpsw_ale.h"
#include "cpsw_sl.h"
#include "am65-cpsw-nuss.h"
#include "k3-cppi-desc-pool.h"

#define AM65_CPSW_SS_BASE	0x0
#define AM65_CPSW_SGMII_BASE	0x100
#define AM65_CPSW_XGMII_BASE	0x2100
#define AM65_CPSW_CPSW_NU_BASE	0x20000
#define AM65_CPSW_NU_PORTS_BASE	0x1000
#define AM65_CPSW_NU_STATS_BASE	0x1a000
#define AM65_CPSW_NU_ALE_BASE	0x1e000
#define AM65_CPSW_NU_CPTS_BASE	0x1d000

#define AM65_CPSW_NU_PORTS_OFFSET	0x1000
#define AM65_CPSW_NU_STATS_PORT_OFFSET	0x200

#define AM65_CPSW_MAX_PORTS	8

#define AM65_CPSW_MIN_PACKET_SIZE	VLAN_ETH_ZLEN
#define AM65_CPSW_MAX_PACKET_SIZE	(VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)

#define AM65_CPSW_REG_CTL		0x004
#define AM65_CPSW_REG_STAT_PORT_EN	0x014
#define AM65_CPSW_REG_PTYPE		0x018

#define AM65_CPSW_P0_REG_CTL			0x004
#define AM65_CPSW_PORT0_REG_FLOW_ID_OFFSET	0x008

#define AM65_CPSW_PORT_REG_PRI_CTL		0x01c
#define AM65_CPSW_PORT_REG_RX_PRI_MAP		0x020
#define AM65_CPSW_PORT_REG_RX_MAXLEN		0x024

#define AM65_CPSW_PORTN_REG_SA_L		0x308
#define AM65_CPSW_PORTN_REG_SA_H		0x30c
#define AM65_CPSW_PORTN_REG_TS_CTL              0x310
#define AM65_CPSW_PORTN_REG_TS_SEQ_LTYPE_REG	0x314
#define AM65_CPSW_PORTN_REG_TS_VLAN_LTYPE_REG	0x318
#define AM65_CPSW_PORTN_REG_TS_CTL_LTYPE2       0x31C

#define AM65_CPSW_CTL_VLAN_AWARE		BIT(1)
#define AM65_CPSW_CTL_P0_ENABLE			BIT(2)
#define AM65_CPSW_CTL_P0_TX_CRC_REMOVE		BIT(13)
#define AM65_CPSW_CTL_P0_RX_PAD			BIT(14)

/* AM65_CPSW_P0_REG_CTL */
#define AM65_CPSW_P0_REG_CTL_RX_CHECKSUM_EN	BIT(0)

/* AM65_CPSW_PORT_REG_PRI_CTL */
#define AM65_CPSW_PORT_REG_PRI_CTL_RX_PTYPE_RROBIN	BIT(8)

/* AM65_CPSW_PN_TS_CTL register fields */
#define AM65_CPSW_PN_TS_CTL_TX_ANX_F_EN		BIT(4)
#define AM65_CPSW_PN_TS_CTL_TX_VLAN_LT1_EN	BIT(5)
#define AM65_CPSW_PN_TS_CTL_TX_VLAN_LT2_EN	BIT(6)
#define AM65_CPSW_PN_TS_CTL_TX_ANX_D_EN		BIT(7)
#define AM65_CPSW_PN_TS_CTL_TX_ANX_E_EN		BIT(10)
#define AM65_CPSW_PN_TS_CTL_TX_HOST_TS_EN	BIT(11)
#define AM65_CPSW_PN_TS_CTL_MSG_TYPE_EN_SHIFT	16

/* AM65_CPSW_PORTN_REG_TS_SEQ_LTYPE_REG register fields */
#define AM65_CPSW_PN_TS_SEQ_ID_OFFSET_SHIFT	16

/* AM65_CPSW_PORTN_REG_TS_CTL_LTYPE2 */
#define AM65_CPSW_PN_TS_CTL_LTYPE2_TS_107	BIT(16)
#define AM65_CPSW_PN_TS_CTL_LTYPE2_TS_129	BIT(17)
#define AM65_CPSW_PN_TS_CTL_LTYPE2_TS_130	BIT(18)
#define AM65_CPSW_PN_TS_CTL_LTYPE2_TS_131	BIT(19)
#define AM65_CPSW_PN_TS_CTL_LTYPE2_TS_132	BIT(20)
#define AM65_CPSW_PN_TS_CTL_LTYPE2_TS_319	BIT(21)
#define AM65_CPSW_PN_TS_CTL_LTYPE2_TS_320	BIT(22)
#define AM65_CPSW_PN_TS_CTL_LTYPE2_TS_TTL_NONZERO BIT(23)

/* The PTP event messages - Sync, Delay_Req, Pdelay_Req, and Pdelay_Resp. */
#define AM65_CPSW_TS_EVENT_MSG_TYPE_BITS (BIT(0) | BIT(1) | BIT(2) | BIT(3))

#define AM65_CPSW_TS_SEQ_ID_OFFSET (0x1e)

#define AM65_CPSW_TS_TX_ANX_ALL_EN		\
	(AM65_CPSW_PN_TS_CTL_TX_ANX_D_EN |	\
	 AM65_CPSW_PN_TS_CTL_TX_ANX_E_EN |	\
	 AM65_CPSW_PN_TS_CTL_TX_ANX_F_EN)

#define AM65_CPSW_ALE_AGEOUT_DEFAULT	30
/* Number of TX/RX descriptors */
#define AM65_CPSW_MAX_TX_DESC	500
#define AM65_CPSW_MAX_RX_DESC	500

#define AM65_CPSW_NAV_PS_DATA_SIZE 16
#define AM65_CPSW_NAV_SW_DATA_SIZE 16

#define AM65_CPSW_DEBUG	(NETIF_MSG_HW | NETIF_MSG_DRV | NETIF_MSG_LINK | \
			 NETIF_MSG_IFUP	| NETIF_MSG_PROBE | NETIF_MSG_IFDOWN | \
			 NETIF_MSG_RX_ERR | NETIF_MSG_TX_ERR)

static void am65_cpsw_port_set_sl_mac(struct am65_cpsw_port *slave,
				      const u8 *dev_addr)
{
	u32 mac_hi = (dev_addr[0] << 0) | (dev_addr[1] << 8) |
		     (dev_addr[2] << 16) | (dev_addr[3] << 24);
	u32 mac_lo = (dev_addr[4] << 0) | (dev_addr[5] << 8);

	writel(mac_hi, slave->port_base + AM65_CPSW_PORTN_REG_SA_H);
	writel(mac_lo, slave->port_base + AM65_CPSW_PORTN_REG_SA_L);
}

static void am65_cpsw_sl_ctl_reset(struct am65_cpsw_port *port)
{
	cpsw_sl_reset(port->slave.mac_sl, 100);
	/* Max length register has to be restored after MAC SL reset */
	writel(AM65_CPSW_MAX_PACKET_SIZE,
	       port->port_base + AM65_CPSW_PORT_REG_RX_MAXLEN);
}

static void am65_cpsw_nuss_get_ver(struct am65_cpsw_common *common)
{
	common->nuss_ver = readl(common->ss_base);
	common->cpsw_ver = readl(common->cpsw_base);
	dev_info(common->dev,
		 "initializing am65 cpsw nuss version 0x%08X, cpsw version 0x%08X Ports: %u\n",
		common->nuss_ver,
		common->cpsw_ver,
		common->port_num + 1);
}

void am65_cpsw_nuss_adjust_link(struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct phy_device *phy = port->slave.phy;
	u32 mac_control = 0;

	if (!phy)
		return;

	if (phy->link) {
		mac_control = CPSW_SL_CTL_GMII_EN;

		if (phy->speed == 1000)
			mac_control |= CPSW_SL_CTL_GIG;
		if (phy->speed == 10 && phy_interface_is_rgmii(phy))
			/* Can be used with in band mode only */
			mac_control |= CPSW_SL_CTL_EXT_EN;
		if (phy->duplex)
			mac_control |= CPSW_SL_CTL_FULLDUPLEX;

		/* RGMII speed is 100M if !CPSW_SL_CTL_GIG*/

		/* rx_pause/tx_pause */
		if (port->slave.rx_pause)
			mac_control |= CPSW_SL_CTL_RX_FLOW_EN;

		if (port->slave.tx_pause)
			mac_control |= CPSW_SL_CTL_TX_FLOW_EN;

		cpsw_sl_ctl_set(port->slave.mac_sl, mac_control);

		/* enable forwarding */
		cpsw_ale_control_set(common->ale, port->port_id,
				     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

		netif_tx_wake_all_queues(ndev);
	} else {
		int tmo;
		/* disable forwarding */
		cpsw_ale_control_set(common->ale, port->port_id,
				     ALE_PORT_STATE, ALE_PORT_STATE_DISABLE);

		cpsw_sl_ctl_set(port->slave.mac_sl, CPSW_SL_CTL_CMD_IDLE);

		tmo = cpsw_sl_wait_for_idle(port->slave.mac_sl, 100);
		dev_dbg(common->dev, "donw msc_sl %08x tmo %d\n",
			cpsw_sl_reg_read(port->slave.mac_sl, CPSW_SL_MACSTATUS),
			tmo);

		cpsw_sl_ctl_reset(port->slave.mac_sl);

		netif_tx_stop_all_queues(ndev);
	}

	phy_print_status(phy);
}

static int am65_cpsw_nuss_ndo_slave_add_vid(struct net_device *ndev,
					    __be16 proto, u16 vid)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	u32 port_mask, unreg_mcast = 0;
	int ret;

	ret = pm_runtime_get_sync(common->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(common->dev);
		return ret;
	}

	port_mask = BIT(port->port_id) | ALE_PORT_HOST;
	if (!vid)
		unreg_mcast = port_mask;
	dev_info(common->dev, "Adding vlan %d to vlan filter\n", vid);
	ret = cpsw_ale_add_vlan(common->ale, vid, port_mask,
				unreg_mcast, port_mask, 0);

	pm_runtime_put(common->dev);
	return ret;
}

static int am65_cpsw_nuss_ndo_slave_kill_vid(struct net_device *ndev,
					     __be16 proto, u16 vid)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	int ret;

	ret = pm_runtime_get_sync(common->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(common->dev);
		return ret;
	}

	dev_info(common->dev, "Removing vlan %d from vlan filter\n", vid);
	ret = cpsw_ale_del_vlan(common->ale, vid, 0);

	pm_runtime_put(common->dev);
	return ret;
}

static void am65_cpsw_slave_set_promisc_2g(struct am65_cpsw_port *port,
					   bool promisc)
{
	struct am65_cpsw_common *common = port->common;

	if (promisc) {
		/* Enable promiscuous mode */
		cpsw_ale_control_set(common->ale, port->port_id,
				     ALE_PORT_MACONLY_CAF, 1);
		dev_dbg(common->dev, "promisc enabled\n");
	} else {
		/* Disable promiscuous mode */
		cpsw_ale_control_set(common->ale, port->port_id,
				     ALE_PORT_MACONLY_CAF, 0);
		dev_dbg(common->dev, "promisc disabled\n");
	}
}

static void am65_cpsw_nuss_ndo_slave_set_rx_mode(struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	u32 port_mask;
	bool promisc;

	promisc = !!(ndev->flags & IFF_PROMISC);
	am65_cpsw_slave_set_promisc_2g(port, promisc);

	if (promisc)
		return;

	/* Restore allmulti on vlans if necessary */
	cpsw_ale_set_allmulti(common->ale,
			      ndev->flags & IFF_ALLMULTI, port->port_id);

	port_mask = ALE_PORT_HOST;
	/* Clear all mcast from ALE */
	cpsw_ale_flush_multicast(common->ale, port_mask, -1);

	if (!netdev_mc_empty(ndev)) {
		struct netdev_hw_addr *ha;

		/* program multicast address list into ALE register */
		netdev_for_each_mc_addr(ha, ndev) {
			cpsw_ale_add_mcast(common->ale, ha->addr,
					   port_mask, 0, 0, 0);
		}
	}
}

static void am65_cpsw_nuss_ndo_host_tx_timeout(struct net_device *ndev,
					       unsigned int txqueue)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_tx_chn *tx_chn;
	struct netdev_queue *netif_txq;
	unsigned long trans_start;

	netif_txq = netdev_get_tx_queue(ndev, txqueue);
	tx_chn = &common->tx_chns[txqueue];
	trans_start = netif_txq->trans_start;

	netdev_err(ndev, "txq:%d DRV_XOFF:%d tmo:%u dql_avail:%d free_desc:%zu\n",
		   txqueue,
		   netif_tx_queue_stopped(netif_txq),
		   jiffies_to_msecs(jiffies - trans_start),
		   dql_avail(&netif_txq->dql),
		   k3_cppi_desc_pool_avail(tx_chn->desc_pool));

	if (netif_tx_queue_stopped(netif_txq)) {
		/* try recover if stopped by us */
		txq_trans_update(netif_txq);
		netif_tx_wake_queue(netif_txq);
	}
}

static int am65_cpsw_nuss_rx_push(struct am65_cpsw_common *common,
				  struct sk_buff *skb)
{
	struct am65_cpsw_rx_chn *rx_chn = &common->rx_chns;
	struct cppi5_host_desc_t *desc_rx;
	struct device *dev = common->dev;
	u32 pkt_len = skb_tailroom(skb);
	dma_addr_t desc_dma;
	dma_addr_t buf_dma;
	void *swdata;

	desc_rx = k3_cppi_desc_pool_alloc(rx_chn->desc_pool);
	if (!desc_rx) {
		dev_err(dev, "Failed to allocate RXFDQ descriptor\n");
		return -ENOMEM;
	}
	desc_dma = k3_cppi_desc_pool_virt2dma(rx_chn->desc_pool, desc_rx);

	buf_dma = dma_map_single(dev, skb->data, pkt_len, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(dev, buf_dma))) {
		k3_cppi_desc_pool_free(rx_chn->desc_pool, desc_rx);
		dev_err(dev, "Failed to map rx skb buffer\n");
		return -EINVAL;
	}

	cppi5_hdesc_init(desc_rx, CPPI5_INFO0_HDESC_EPIB_PRESENT,
			 AM65_CPSW_NAV_PS_DATA_SIZE);
	cppi5_hdesc_attach_buf(desc_rx, 0, 0, buf_dma, skb_tailroom(skb));
	swdata = cppi5_hdesc_get_swdata(desc_rx);
	*((void **)swdata) = skb;

	return k3_udma_glue_push_rx_chn(rx_chn->rx_chn, 0, desc_rx, desc_dma);
}

void am65_cpsw_nuss_set_p0_ptype(struct am65_cpsw_common *common)
{
	struct am65_cpsw_host *host_p = am65_common_get_host(common);
	u32 val, pri_map;

	/* P0 set Receive Priority Type */
	val = readl(host_p->port_base + AM65_CPSW_PORT_REG_PRI_CTL);

	if (common->pf_p0_rx_ptype_rrobin) {
		val |= AM65_CPSW_PORT_REG_PRI_CTL_RX_PTYPE_RROBIN;
		/* Enet Ports fifos works in fixed priority mode only, so
		 * reset P0_Rx_Pri_Map so all packet will go in Enet fifo 0
		 */
		pri_map = 0x0;
	} else {
		val &= ~AM65_CPSW_PORT_REG_PRI_CTL_RX_PTYPE_RROBIN;
		/* restore P0_Rx_Pri_Map */
		pri_map = 0x76543210;
	}

	writel(pri_map, host_p->port_base + AM65_CPSW_PORT_REG_RX_PRI_MAP);
	writel(val, host_p->port_base + AM65_CPSW_PORT_REG_PRI_CTL);
}

static int am65_cpsw_nuss_common_open(struct am65_cpsw_common *common,
				      netdev_features_t features)
{
	struct am65_cpsw_host *host_p = am65_common_get_host(common);
	int port_idx, i, ret;
	struct sk_buff *skb;
	u32 val, port_mask;

	if (common->usage_count)
		return 0;

	/* Control register */
	writel(AM65_CPSW_CTL_P0_ENABLE | AM65_CPSW_CTL_P0_TX_CRC_REMOVE |
	       AM65_CPSW_CTL_VLAN_AWARE | AM65_CPSW_CTL_P0_RX_PAD,
	       common->cpsw_base + AM65_CPSW_REG_CTL);
	/* Max length register */
	writel(AM65_CPSW_MAX_PACKET_SIZE,
	       host_p->port_base + AM65_CPSW_PORT_REG_RX_MAXLEN);
	/* set base flow_id */
	writel(common->rx_flow_id_base,
	       host_p->port_base + AM65_CPSW_PORT0_REG_FLOW_ID_OFFSET);
	/* en tx crc offload */
	if (features & NETIF_F_HW_CSUM)
		writel(AM65_CPSW_P0_REG_CTL_RX_CHECKSUM_EN,
		       host_p->port_base + AM65_CPSW_P0_REG_CTL);

	am65_cpsw_nuss_set_p0_ptype(common);

	/* enable statistic */
	val = BIT(HOST_PORT_NUM);
	for (port_idx = 0; port_idx < common->port_num; port_idx++) {
		struct am65_cpsw_port *port = &common->ports[port_idx];

		if (!port->disabled)
			val |=  BIT(port->port_id);
	}
	writel(val, common->cpsw_base + AM65_CPSW_REG_STAT_PORT_EN);

	/* disable priority elevation */
	writel(0, common->cpsw_base + AM65_CPSW_REG_PTYPE);

	cpsw_ale_start(common->ale);

	/* limit to one RX flow only */
	cpsw_ale_control_set(common->ale, HOST_PORT_NUM,
			     ALE_DEFAULT_THREAD_ID, 0);
	cpsw_ale_control_set(common->ale, HOST_PORT_NUM,
			     ALE_DEFAULT_THREAD_ENABLE, 1);
	if (AM65_CPSW_IS_CPSW2G(common))
		cpsw_ale_control_set(common->ale, HOST_PORT_NUM,
				     ALE_PORT_NOLEARN, 1);
	/* switch to vlan unaware mode */
	cpsw_ale_control_set(common->ale, HOST_PORT_NUM, ALE_VLAN_AWARE, 1);
	cpsw_ale_control_set(common->ale, HOST_PORT_NUM,
			     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

	/* default vlan cfg: create mask based on enabled ports */
	port_mask = GENMASK(common->port_num, 0) &
		    ~common->disabled_ports_mask;

	cpsw_ale_add_vlan(common->ale, 0, port_mask,
			  port_mask, port_mask,
			  port_mask & ~ALE_PORT_HOST);

	for (i = 0; i < common->rx_chns.descs_num; i++) {
		skb = __netdev_alloc_skb_ip_align(NULL,
						  AM65_CPSW_MAX_PACKET_SIZE,
						  GFP_KERNEL);
		if (!skb) {
			dev_err(common->dev, "cannot allocate skb\n");
			return -ENOMEM;
		}

		ret = am65_cpsw_nuss_rx_push(common, skb);
		if (ret < 0) {
			dev_err(common->dev,
				"cannot submit skb to channel rx, error %d\n",
				ret);
			kfree_skb(skb);
			return ret;
		}
		kmemleak_not_leak(skb);
	}
	k3_udma_glue_enable_rx_chn(common->rx_chns.rx_chn);

	for (i = 0; i < common->tx_ch_num; i++) {
		ret = k3_udma_glue_enable_tx_chn(common->tx_chns[i].tx_chn);
		if (ret)
			return ret;
		napi_enable(&common->tx_chns[i].napi_tx);
	}

	napi_enable(&common->napi_rx);

	dev_dbg(common->dev, "cpsw_nuss started\n");
	return 0;
}

static void am65_cpsw_nuss_tx_cleanup(void *data, dma_addr_t desc_dma);
static void am65_cpsw_nuss_rx_cleanup(void *data, dma_addr_t desc_dma);

static int am65_cpsw_nuss_common_stop(struct am65_cpsw_common *common)
{
	int i;

	if (common->usage_count != 1)
		return 0;

	cpsw_ale_control_set(common->ale, HOST_PORT_NUM,
			     ALE_PORT_STATE, ALE_PORT_STATE_DISABLE);

	/* shutdown tx channels */
	atomic_set(&common->tdown_cnt, common->tx_ch_num);
	/* ensure new tdown_cnt value is visible */
	smp_mb__after_atomic();
	reinit_completion(&common->tdown_complete);

	for (i = 0; i < common->tx_ch_num; i++)
		k3_udma_glue_tdown_tx_chn(common->tx_chns[i].tx_chn, false);

	i = wait_for_completion_timeout(&common->tdown_complete,
					msecs_to_jiffies(1000));
	if (!i)
		dev_err(common->dev, "tx timeout\n");
	for (i = 0; i < common->tx_ch_num; i++)
		napi_disable(&common->tx_chns[i].napi_tx);

	for (i = 0; i < common->tx_ch_num; i++) {
		k3_udma_glue_reset_tx_chn(common->tx_chns[i].tx_chn,
					  &common->tx_chns[i],
					  am65_cpsw_nuss_tx_cleanup);
		k3_udma_glue_disable_tx_chn(common->tx_chns[i].tx_chn);
	}

	k3_udma_glue_tdown_rx_chn(common->rx_chns.rx_chn, true);
	napi_disable(&common->napi_rx);

	for (i = 0; i < AM65_CPSW_MAX_RX_FLOWS; i++)
		k3_udma_glue_reset_rx_chn(common->rx_chns.rx_chn, i,
					  &common->rx_chns,
					  am65_cpsw_nuss_rx_cleanup, !!i);

	k3_udma_glue_disable_rx_chn(common->rx_chns.rx_chn);

	cpsw_ale_stop(common->ale);

	writel(0, common->cpsw_base + AM65_CPSW_REG_CTL);
	writel(0, common->cpsw_base + AM65_CPSW_REG_STAT_PORT_EN);

	dev_dbg(common->dev, "cpsw_nuss stopped\n");
	return 0;
}

static int am65_cpsw_nuss_ndo_slave_stop(struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	int ret;

	if (port->slave.phy)
		phy_stop(port->slave.phy);

	netif_tx_stop_all_queues(ndev);

	if (port->slave.phy) {
		phy_disconnect(port->slave.phy);
		port->slave.phy = NULL;
	}

	ret = am65_cpsw_nuss_common_stop(common);
	if (ret)
		return ret;

	common->usage_count--;
	pm_runtime_put(common->dev);
	return 0;
}

static int am65_cpsw_nuss_ndo_slave_open(struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	u32 port_mask;
	int ret, i;

	ret = pm_runtime_get_sync(common->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(common->dev);
		return ret;
	}

	/* Notify the stack of the actual queue counts. */
	ret = netif_set_real_num_tx_queues(ndev, common->tx_ch_num);
	if (ret) {
		dev_err(common->dev, "cannot set real number of tx queues\n");
		return ret;
	}

	ret = netif_set_real_num_rx_queues(ndev, AM65_CPSW_MAX_RX_QUEUES);
	if (ret) {
		dev_err(common->dev, "cannot set real number of rx queues\n");
		return ret;
	}

	for (i = 0; i < common->tx_ch_num; i++)
		netdev_tx_reset_queue(netdev_get_tx_queue(ndev, i));

	ret = am65_cpsw_nuss_common_open(common, ndev->features);
	if (ret)
		return ret;

	common->usage_count++;

	am65_cpsw_port_set_sl_mac(port, ndev->dev_addr);

	if (port->slave.mac_only)
		/* enable mac-only mode on port */
		cpsw_ale_control_set(common->ale, port->port_id,
				     ALE_PORT_MACONLY, 1);
	if (AM65_CPSW_IS_CPSW2G(common))
		cpsw_ale_control_set(common->ale, port->port_id,
				     ALE_PORT_NOLEARN, 1);

	port_mask = BIT(port->port_id) | ALE_PORT_HOST;
	cpsw_ale_add_ucast(common->ale, ndev->dev_addr,
			   HOST_PORT_NUM, ALE_SECURE, 0);
	cpsw_ale_add_mcast(common->ale, ndev->broadcast,
			   port_mask, 0, 0, ALE_MCAST_FWD_2);

	/* mac_sl should be configured via phy-link interface */
	am65_cpsw_sl_ctl_reset(port);

	ret = phy_set_mode_ext(port->slave.ifphy, PHY_MODE_ETHERNET,
			       port->slave.phy_if);
	if (ret)
		goto error_cleanup;

	if (port->slave.phy_node) {
		port->slave.phy = of_phy_connect(ndev,
						 port->slave.phy_node,
						 &am65_cpsw_nuss_adjust_link,
						 0, port->slave.phy_if);
		if (!port->slave.phy) {
			dev_err(common->dev, "phy %pOF not found on slave %d\n",
				port->slave.phy_node,
				port->port_id);
			ret = -ENODEV;
			goto error_cleanup;
		}
	}

	phy_attached_info(port->slave.phy);
	phy_start(port->slave.phy);

	return 0;

error_cleanup:
	am65_cpsw_nuss_ndo_slave_stop(ndev);
	return ret;
}

static void am65_cpsw_nuss_rx_cleanup(void *data, dma_addr_t desc_dma)
{
	struct am65_cpsw_rx_chn *rx_chn = data;
	struct cppi5_host_desc_t *desc_rx;
	struct sk_buff *skb;
	dma_addr_t buf_dma;
	u32 buf_dma_len;
	void **swdata;

	desc_rx = k3_cppi_desc_pool_dma2virt(rx_chn->desc_pool, desc_dma);
	swdata = cppi5_hdesc_get_swdata(desc_rx);
	skb = *swdata;
	cppi5_hdesc_get_obuf(desc_rx, &buf_dma, &buf_dma_len);

	dma_unmap_single(rx_chn->dev, buf_dma, buf_dma_len, DMA_FROM_DEVICE);
	k3_cppi_desc_pool_free(rx_chn->desc_pool, desc_rx);

	dev_kfree_skb_any(skb);
}

/* RX psdata[2] word format - checksum information */
#define AM65_CPSW_RX_PSD_CSUM_ADD	GENMASK(15, 0)
#define AM65_CPSW_RX_PSD_CSUM_ERR	BIT(16)
#define AM65_CPSW_RX_PSD_IS_FRAGMENT	BIT(17)
#define AM65_CPSW_RX_PSD_IS_TCP		BIT(18)
#define AM65_CPSW_RX_PSD_IPV6_VALID	BIT(19)
#define AM65_CPSW_RX_PSD_IPV4_VALID	BIT(20)

static void am65_cpsw_nuss_rx_csum(struct sk_buff *skb, u32 csum_info)
{
	/* HW can verify IPv4/IPv6 TCP/UDP packets checksum
	 * csum information provides in psdata[2] word:
	 * AM65_CPSW_RX_PSD_CSUM_ERR bit - indicates csum error
	 * AM65_CPSW_RX_PSD_IPV6_VALID and AM65_CPSW_RX_PSD_IPV4_VALID
	 * bits - indicates IPv4/IPv6 packet
	 * AM65_CPSW_RX_PSD_IS_FRAGMENT bit - indicates fragmented packet
	 * AM65_CPSW_RX_PSD_CSUM_ADD has value 0xFFFF for non fragmented packets
	 * or csum value for fragmented packets if !AM65_CPSW_RX_PSD_CSUM_ERR
	 */
	skb_checksum_none_assert(skb);

	if (unlikely(!(skb->dev->features & NETIF_F_RXCSUM)))
		return;

	if ((csum_info & (AM65_CPSW_RX_PSD_IPV6_VALID |
			  AM65_CPSW_RX_PSD_IPV4_VALID)) &&
			  !(csum_info & AM65_CPSW_RX_PSD_CSUM_ERR)) {
		/* csum for fragmented packets is unsupported */
		if (!(csum_info & AM65_CPSW_RX_PSD_IS_FRAGMENT))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
}

static int am65_cpsw_nuss_rx_packets(struct am65_cpsw_common *common,
				     u32 flow_idx)
{
	struct am65_cpsw_rx_chn *rx_chn = &common->rx_chns;
	u32 buf_dma_len, pkt_len, port_id = 0, csum_info;
	struct am65_cpsw_ndev_priv *ndev_priv;
	struct am65_cpsw_ndev_stats *stats;
	struct cppi5_host_desc_t *desc_rx;
	struct device *dev = common->dev;
	struct sk_buff *skb, *new_skb;
	dma_addr_t desc_dma, buf_dma;
	struct am65_cpsw_port *port;
	struct net_device *ndev;
	void **swdata;
	u32 *psdata;
	int ret = 0;

	ret = k3_udma_glue_pop_rx_chn(rx_chn->rx_chn, flow_idx, &desc_dma);
	if (ret) {
		if (ret != -ENODATA)
			dev_err(dev, "RX: pop chn fail %d\n", ret);
		return ret;
	}

	if (desc_dma & 0x1) {
		dev_dbg(dev, "%s RX tdown flow: %u\n", __func__, flow_idx);
		return 0;
	}

	desc_rx = k3_cppi_desc_pool_dma2virt(rx_chn->desc_pool, desc_dma);
	dev_dbg(dev, "%s flow_idx: %u desc %pad\n",
		__func__, flow_idx, &desc_dma);

	swdata = cppi5_hdesc_get_swdata(desc_rx);
	skb = *swdata;
	cppi5_hdesc_get_obuf(desc_rx, &buf_dma, &buf_dma_len);
	pkt_len = cppi5_hdesc_get_pktlen(desc_rx);
	cppi5_desc_get_tags_ids(&desc_rx->hdr, &port_id, NULL);
	dev_dbg(dev, "%s rx port_id:%d\n", __func__, port_id);
	port = am65_common_get_port(common, port_id);
	ndev = port->ndev;
	skb->dev = ndev;

	psdata = cppi5_hdesc_get_psdata(desc_rx);
	csum_info = psdata[2];
	dev_dbg(dev, "%s rx csum_info:%#x\n", __func__, csum_info);

	dma_unmap_single(dev, buf_dma, buf_dma_len, DMA_FROM_DEVICE);

	k3_cppi_desc_pool_free(rx_chn->desc_pool, desc_rx);

	new_skb = netdev_alloc_skb_ip_align(ndev, AM65_CPSW_MAX_PACKET_SIZE);
	if (new_skb) {
		skb_put(skb, pkt_len);
		skb->protocol = eth_type_trans(skb, ndev);
		am65_cpsw_nuss_rx_csum(skb, csum_info);
		napi_gro_receive(&common->napi_rx, skb);

		ndev_priv = netdev_priv(ndev);
		stats = this_cpu_ptr(ndev_priv->stats);

		u64_stats_update_begin(&stats->syncp);
		stats->rx_packets++;
		stats->rx_bytes += pkt_len;
		u64_stats_update_end(&stats->syncp);
		kmemleak_not_leak(new_skb);
	} else {
		ndev->stats.rx_dropped++;
		new_skb = skb;
	}

	if (netif_dormant(ndev)) {
		dev_kfree_skb_any(new_skb);
		ndev->stats.rx_dropped++;
		return 0;
	}

	ret = am65_cpsw_nuss_rx_push(common, new_skb);
	if (WARN_ON(ret < 0)) {
		dev_kfree_skb_any(new_skb);
		ndev->stats.rx_errors++;
		ndev->stats.rx_dropped++;
	}

	return ret;
}

static int am65_cpsw_nuss_rx_poll(struct napi_struct *napi_rx, int budget)
{
	struct am65_cpsw_common *common = am65_cpsw_napi_to_common(napi_rx);
	int flow = AM65_CPSW_MAX_RX_FLOWS;
	int cur_budget, ret;
	int num_rx = 0;

	/* process every flow */
	while (flow--) {
		cur_budget = budget - num_rx;

		while (cur_budget--) {
			ret = am65_cpsw_nuss_rx_packets(common, flow);
			if (ret)
				break;
			num_rx++;
		}

		if (num_rx >= budget)
			break;
	}

	dev_dbg(common->dev, "%s num_rx:%d %d\n", __func__, num_rx, budget);

	if (num_rx < budget && napi_complete_done(napi_rx, num_rx))
		enable_irq(common->rx_chns.irq);

	return num_rx;
}

static void am65_cpsw_nuss_xmit_free(struct am65_cpsw_tx_chn *tx_chn,
				     struct device *dev,
				     struct cppi5_host_desc_t *desc)
{
	struct cppi5_host_desc_t *first_desc, *next_desc;
	dma_addr_t buf_dma, next_desc_dma;
	u32 buf_dma_len;

	first_desc = desc;
	next_desc = first_desc;

	cppi5_hdesc_get_obuf(first_desc, &buf_dma, &buf_dma_len);

	dma_unmap_single(dev, buf_dma, buf_dma_len,
			 DMA_TO_DEVICE);

	next_desc_dma = cppi5_hdesc_get_next_hbdesc(first_desc);
	while (next_desc_dma) {
		next_desc = k3_cppi_desc_pool_dma2virt(tx_chn->desc_pool,
						       next_desc_dma);
		cppi5_hdesc_get_obuf(next_desc, &buf_dma, &buf_dma_len);

		dma_unmap_page(dev, buf_dma, buf_dma_len,
			       DMA_TO_DEVICE);

		next_desc_dma = cppi5_hdesc_get_next_hbdesc(next_desc);

		k3_cppi_desc_pool_free(tx_chn->desc_pool, next_desc);
	}

	k3_cppi_desc_pool_free(tx_chn->desc_pool, first_desc);
}

static void am65_cpsw_nuss_tx_cleanup(void *data, dma_addr_t desc_dma)
{
	struct am65_cpsw_tx_chn *tx_chn = data;
	struct cppi5_host_desc_t *desc_tx;
	struct sk_buff *skb;
	void **swdata;

	desc_tx = k3_cppi_desc_pool_dma2virt(tx_chn->desc_pool, desc_dma);
	swdata = cppi5_hdesc_get_swdata(desc_tx);
	skb = *(swdata);
	am65_cpsw_nuss_xmit_free(tx_chn, tx_chn->common->dev, desc_tx);

	dev_kfree_skb_any(skb);
}

static int am65_cpsw_nuss_tx_compl_packets(struct am65_cpsw_common *common,
					   int chn, unsigned int budget)
{
	struct cppi5_host_desc_t *desc_tx;
	struct device *dev = common->dev;
	struct am65_cpsw_tx_chn *tx_chn;
	struct netdev_queue *netif_txq;
	unsigned int total_bytes = 0;
	struct net_device *ndev;
	struct sk_buff *skb;
	dma_addr_t desc_dma;
	int res, num_tx = 0;
	void **swdata;

	tx_chn = &common->tx_chns[chn];

	while (true) {
		struct am65_cpsw_ndev_priv *ndev_priv;
		struct am65_cpsw_ndev_stats *stats;

		res = k3_udma_glue_pop_tx_chn(tx_chn->tx_chn, &desc_dma);
		if (res == -ENODATA)
			break;

		if (desc_dma & 0x1) {
			if (atomic_dec_and_test(&common->tdown_cnt))
				complete(&common->tdown_complete);
			break;
		}

		desc_tx = k3_cppi_desc_pool_dma2virt(tx_chn->desc_pool,
						     desc_dma);
		swdata = cppi5_hdesc_get_swdata(desc_tx);
		skb = *(swdata);
		am65_cpsw_nuss_xmit_free(tx_chn, dev, desc_tx);

		ndev = skb->dev;

		ndev_priv = netdev_priv(ndev);
		stats = this_cpu_ptr(ndev_priv->stats);
		u64_stats_update_begin(&stats->syncp);
		stats->tx_packets++;
		stats->tx_bytes += skb->len;
		u64_stats_update_end(&stats->syncp);

		total_bytes += skb->len;
		napi_consume_skb(skb, budget);
		num_tx++;
	}

	if (!num_tx)
		return 0;

	netif_txq = netdev_get_tx_queue(ndev, chn);

	netdev_tx_completed_queue(netif_txq, num_tx, total_bytes);

	if (netif_tx_queue_stopped(netif_txq)) {
		/* Check whether the queue is stopped due to stalled tx dma,
		 * if the queue is stopped then wake the queue as
		 * we have free desc for tx
		 */
		__netif_tx_lock(netif_txq, smp_processor_id());
		if (netif_running(ndev) &&
		    (k3_cppi_desc_pool_avail(tx_chn->desc_pool) >=
		     MAX_SKB_FRAGS))
			netif_tx_wake_queue(netif_txq);

		__netif_tx_unlock(netif_txq);
	}
	dev_dbg(dev, "%s:%u pkt:%d\n", __func__, chn, num_tx);

	return num_tx;
}

static int am65_cpsw_nuss_tx_poll(struct napi_struct *napi_tx, int budget)
{
	struct am65_cpsw_tx_chn *tx_chn = am65_cpsw_napi_to_tx_chn(napi_tx);
	int num_tx;

	num_tx = am65_cpsw_nuss_tx_compl_packets(tx_chn->common, tx_chn->id,
						 budget);
	num_tx = min(num_tx, budget);
	if (num_tx < budget) {
		napi_complete(napi_tx);
		enable_irq(tx_chn->irq);
	}

	return num_tx;
}

static irqreturn_t am65_cpsw_nuss_rx_irq(int irq, void *dev_id)
{
	struct am65_cpsw_common *common = dev_id;

	disable_irq_nosync(irq);
	napi_schedule(&common->napi_rx);

	return IRQ_HANDLED;
}

static irqreturn_t am65_cpsw_nuss_tx_irq(int irq, void *dev_id)
{
	struct am65_cpsw_tx_chn *tx_chn = dev_id;

	disable_irq_nosync(irq);
	napi_schedule(&tx_chn->napi_tx);

	return IRQ_HANDLED;
}

static netdev_tx_t am65_cpsw_nuss_ndo_slave_xmit(struct sk_buff *skb,
						 struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct cppi5_host_desc_t *first_desc, *next_desc, *cur_desc;
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct device *dev = common->dev;
	struct am65_cpsw_tx_chn *tx_chn;
	struct netdev_queue *netif_txq;
	dma_addr_t desc_dma, buf_dma;
	int ret, q_idx, i;
	void **swdata;
	u32 *psdata;
	u32 pkt_len;

	/* padding enabled in hw */
	pkt_len = skb_headlen(skb);

	q_idx = skb_get_queue_mapping(skb);
	dev_dbg(dev, "%s skb_queue:%d\n", __func__, q_idx);

	tx_chn = &common->tx_chns[q_idx];
	netif_txq = netdev_get_tx_queue(ndev, q_idx);

	/* Map the linear buffer */
	buf_dma = dma_map_single(dev, skb->data, pkt_len,
				 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, buf_dma))) {
		dev_err(dev, "Failed to map tx skb buffer\n");
		ndev->stats.tx_errors++;
		goto err_free_skb;
	}

	first_desc = k3_cppi_desc_pool_alloc(tx_chn->desc_pool);
	if (!first_desc) {
		dev_dbg(dev, "Failed to allocate descriptor\n");
		dma_unmap_single(dev, buf_dma, pkt_len, DMA_TO_DEVICE);
		goto busy_stop_q;
	}

	cppi5_hdesc_init(first_desc, CPPI5_INFO0_HDESC_EPIB_PRESENT,
			 AM65_CPSW_NAV_PS_DATA_SIZE);
	cppi5_desc_set_pktids(&first_desc->hdr, 0, 0x3FFF);
	cppi5_hdesc_set_pkttype(first_desc, 0x7);
	cppi5_desc_set_tags_ids(&first_desc->hdr, 0, port->port_id);

	cppi5_hdesc_attach_buf(first_desc, buf_dma, pkt_len, buf_dma, pkt_len);
	swdata = cppi5_hdesc_get_swdata(first_desc);
	*(swdata) = skb;
	psdata = cppi5_hdesc_get_psdata(first_desc);

	/* HW csum offload if enabled */
	psdata[2] = 0;
	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		unsigned int cs_start, cs_offset;

		cs_start = skb_transport_offset(skb);
		cs_offset = cs_start + skb->csum_offset;
		/* HW numerates bytes starting from 1 */
		psdata[2] = ((cs_offset + 1) << 24) |
			    ((cs_start + 1) << 16) | (skb->len - cs_start);
		dev_dbg(dev, "%s tx psdata:%#x\n", __func__, psdata[2]);
	}

	if (!skb_is_nonlinear(skb))
		goto done_tx;

	dev_dbg(dev, "fragmented SKB\n");

	/* Handle the case where skb is fragmented in pages */
	cur_desc = first_desc;
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		u32 frag_size = skb_frag_size(frag);

		next_desc = k3_cppi_desc_pool_alloc(tx_chn->desc_pool);
		if (!next_desc) {
			dev_err(dev, "Failed to allocate descriptor\n");
			goto busy_free_descs;
		}

		buf_dma = skb_frag_dma_map(dev, frag, 0, frag_size,
					   DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev, buf_dma))) {
			dev_err(dev, "Failed to map tx skb page\n");
			k3_cppi_desc_pool_free(tx_chn->desc_pool, next_desc);
			ndev->stats.tx_errors++;
			goto err_free_descs;
		}

		cppi5_hdesc_reset_hbdesc(next_desc);
		cppi5_hdesc_attach_buf(next_desc,
				       buf_dma, frag_size, buf_dma, frag_size);

		desc_dma = k3_cppi_desc_pool_virt2dma(tx_chn->desc_pool,
						      next_desc);
		cppi5_hdesc_link_hbdesc(cur_desc, desc_dma);

		pkt_len += frag_size;
		cur_desc = next_desc;
	}
	WARN_ON(pkt_len != skb->len);

done_tx:
	skb_tx_timestamp(skb);

	/* report bql before sending packet */
	netdev_tx_sent_queue(netif_txq, pkt_len);

	cppi5_hdesc_set_pktlen(first_desc, pkt_len);
	desc_dma = k3_cppi_desc_pool_virt2dma(tx_chn->desc_pool, first_desc);
	ret = k3_udma_glue_push_tx_chn(tx_chn->tx_chn, first_desc, desc_dma);
	if (ret) {
		dev_err(dev, "can't push desc %d\n", ret);
		/* inform bql */
		netdev_tx_completed_queue(netif_txq, 1, pkt_len);
		ndev->stats.tx_errors++;
		goto err_free_descs;
	}

	if (k3_cppi_desc_pool_avail(tx_chn->desc_pool) < MAX_SKB_FRAGS) {
		netif_tx_stop_queue(netif_txq);
		/* Barrier, so that stop_queue visible to other cpus */
		smp_mb__after_atomic();
		dev_dbg(dev, "netif_tx_stop_queue %d\n", q_idx);

		/* re-check for smp */
		if (k3_cppi_desc_pool_avail(tx_chn->desc_pool) >=
		    MAX_SKB_FRAGS) {
			netif_tx_wake_queue(netif_txq);
			dev_dbg(dev, "netif_tx_wake_queue %d\n", q_idx);
		}
	}

	return NETDEV_TX_OK;

err_free_descs:
	am65_cpsw_nuss_xmit_free(tx_chn, dev, first_desc);
err_free_skb:
	ndev->stats.tx_dropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;

busy_free_descs:
	am65_cpsw_nuss_xmit_free(tx_chn, dev, first_desc);
busy_stop_q:
	netif_tx_stop_queue(netif_txq);
	return NETDEV_TX_BUSY;
}

static int am65_cpsw_nuss_ndo_slave_set_mac_address(struct net_device *ndev,
						    void *addr)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct sockaddr *sockaddr = (struct sockaddr *)addr;
	int ret;

	ret = eth_prepare_mac_addr_change(ndev, addr);
	if (ret < 0)
		return ret;

	ret = pm_runtime_get_sync(common->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(common->dev);
		return ret;
	}

	cpsw_ale_del_ucast(common->ale, ndev->dev_addr,
			   HOST_PORT_NUM, 0, 0);
	cpsw_ale_add_ucast(common->ale, sockaddr->sa_data,
			   HOST_PORT_NUM, ALE_SECURE, 0);

	am65_cpsw_port_set_sl_mac(port, addr);
	eth_commit_mac_addr_change(ndev, sockaddr);

	pm_runtime_put(common->dev);

	return 0;
}

static int am65_cpsw_nuss_ndo_slave_ioctl(struct net_device *ndev,
					  struct ifreq *req, int cmd)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);

	if (!netif_running(ndev))
		return -EINVAL;

	if (!port->slave.phy)
		return -EOPNOTSUPP;

	return phy_mii_ioctl(port->slave.phy, req, cmd);
}

static void am65_cpsw_nuss_ndo_get_stats(struct net_device *dev,
					 struct rtnl_link_stats64 *stats)
{
	struct am65_cpsw_ndev_priv *ndev_priv = netdev_priv(dev);
	unsigned int start;
	int cpu;

	for_each_possible_cpu(cpu) {
		struct am65_cpsw_ndev_stats *cpu_stats;
		u64 rx_packets;
		u64 rx_bytes;
		u64 tx_packets;
		u64 tx_bytes;

		cpu_stats = per_cpu_ptr(ndev_priv->stats, cpu);
		do {
			start = u64_stats_fetch_begin_irq(&cpu_stats->syncp);
			rx_packets = cpu_stats->rx_packets;
			rx_bytes   = cpu_stats->rx_bytes;
			tx_packets = cpu_stats->tx_packets;
			tx_bytes   = cpu_stats->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&cpu_stats->syncp, start));

		stats->rx_packets += rx_packets;
		stats->rx_bytes   += rx_bytes;
		stats->tx_packets += tx_packets;
		stats->tx_bytes   += tx_bytes;
	}

	stats->rx_errors	= dev->stats.rx_errors;
	stats->rx_dropped	= dev->stats.rx_dropped;
	stats->tx_dropped	= dev->stats.tx_dropped;
}

static int am65_cpsw_nuss_ndo_slave_set_features(struct net_device *ndev,
						 netdev_features_t features)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	netdev_features_t changes = features ^ ndev->features;
	struct am65_cpsw_host *host_p;

	host_p = am65_common_get_host(common);

	if (changes & NETIF_F_HW_CSUM) {
		bool enable = !!(features & NETIF_F_HW_CSUM);

		dev_info(common->dev, "Turn %s tx-checksum-ip-generic\n",
			 enable ? "ON" : "OFF");
		if (enable)
			writel(AM65_CPSW_P0_REG_CTL_RX_CHECKSUM_EN,
			       host_p->port_base + AM65_CPSW_P0_REG_CTL);
		else
			writel(0,
			       host_p->port_base + AM65_CPSW_P0_REG_CTL);
	}

	return 0;
}

static const struct net_device_ops am65_cpsw_nuss_netdev_ops_2g = {
	.ndo_open		= am65_cpsw_nuss_ndo_slave_open,
	.ndo_stop		= am65_cpsw_nuss_ndo_slave_stop,
	.ndo_start_xmit		= am65_cpsw_nuss_ndo_slave_xmit,
	.ndo_set_rx_mode	= am65_cpsw_nuss_ndo_slave_set_rx_mode,
	.ndo_get_stats64        = am65_cpsw_nuss_ndo_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= am65_cpsw_nuss_ndo_slave_set_mac_address,
	.ndo_tx_timeout		= am65_cpsw_nuss_ndo_host_tx_timeout,
	.ndo_vlan_rx_add_vid	= am65_cpsw_nuss_ndo_slave_add_vid,
	.ndo_vlan_rx_kill_vid	= am65_cpsw_nuss_ndo_slave_kill_vid,
	.ndo_do_ioctl		= am65_cpsw_nuss_ndo_slave_ioctl,
	.ndo_set_features	= am65_cpsw_nuss_ndo_slave_set_features,
};

static void am65_cpsw_nuss_slave_disable_unused(struct am65_cpsw_port *port)
{
	struct am65_cpsw_common *common = port->common;

	if (!port->disabled)
		return;

	common->disabled_ports_mask |= BIT(port->port_id);
	cpsw_ale_control_set(common->ale, port->port_id,
			     ALE_PORT_STATE, ALE_PORT_STATE_DISABLE);

	cpsw_sl_reset(port->slave.mac_sl, 100);
	cpsw_sl_ctl_reset(port->slave.mac_sl);
}

static void am65_cpsw_nuss_free_tx_chns(void *data)
{
	struct am65_cpsw_common *common = data;
	int i;

	for (i = 0; i < common->tx_ch_num; i++) {
		struct am65_cpsw_tx_chn *tx_chn = &common->tx_chns[i];

		if (!IS_ERR_OR_NULL(tx_chn->tx_chn))
			k3_udma_glue_release_tx_chn(tx_chn->tx_chn);

		if (!IS_ERR_OR_NULL(tx_chn->desc_pool))
			k3_cppi_desc_pool_destroy(tx_chn->desc_pool);

		memset(tx_chn, 0, sizeof(*tx_chn));
	}
}

void am65_cpsw_nuss_remove_tx_chns(struct am65_cpsw_common *common)
{
	struct device *dev = common->dev;
	int i;

	devm_remove_action(dev, am65_cpsw_nuss_free_tx_chns, common);

	for (i = 0; i < common->tx_ch_num; i++) {
		struct am65_cpsw_tx_chn *tx_chn = &common->tx_chns[i];

		if (tx_chn->irq)
			devm_free_irq(dev, tx_chn->irq, tx_chn);

		netif_napi_del(&tx_chn->napi_tx);

		if (!IS_ERR_OR_NULL(tx_chn->tx_chn))
			k3_udma_glue_release_tx_chn(tx_chn->tx_chn);

		if (!IS_ERR_OR_NULL(tx_chn->desc_pool))
			k3_cppi_desc_pool_destroy(tx_chn->desc_pool);

		memset(tx_chn, 0, sizeof(*tx_chn));
	}
}

static int am65_cpsw_nuss_init_tx_chns(struct am65_cpsw_common *common)
{
	u32  max_desc_num = ALIGN(AM65_CPSW_MAX_TX_DESC, MAX_SKB_FRAGS);
	struct k3_udma_glue_tx_channel_cfg tx_cfg = { 0 };
	struct device *dev = common->dev;
	struct k3_ring_cfg ring_cfg = {
		.elm_size = K3_RINGACC_RING_ELSIZE_8,
		.mode = K3_RINGACC_RING_MODE_RING,
		.flags = 0
	};
	u32 hdesc_size;
	int i, ret = 0;

	hdesc_size = cppi5_hdesc_calc_size(true, AM65_CPSW_NAV_PS_DATA_SIZE,
					   AM65_CPSW_NAV_SW_DATA_SIZE);

	tx_cfg.swdata_size = AM65_CPSW_NAV_SW_DATA_SIZE;
	tx_cfg.tx_cfg = ring_cfg;
	tx_cfg.txcq_cfg = ring_cfg;
	tx_cfg.tx_cfg.size = max_desc_num;
	tx_cfg.txcq_cfg.size = max_desc_num;

	for (i = 0; i < common->tx_ch_num; i++) {
		struct am65_cpsw_tx_chn *tx_chn = &common->tx_chns[i];

		snprintf(tx_chn->tx_chn_name,
			 sizeof(tx_chn->tx_chn_name), "tx%d", i);

		tx_chn->common = common;
		tx_chn->id = i;
		tx_chn->descs_num = max_desc_num;
		tx_chn->desc_pool =
			k3_cppi_desc_pool_create_name(dev,
						      tx_chn->descs_num,
						      hdesc_size,
						      tx_chn->tx_chn_name);
		if (IS_ERR(tx_chn->desc_pool)) {
			ret = PTR_ERR(tx_chn->desc_pool);
			dev_err(dev, "Failed to create poll %d\n", ret);
			goto err;
		}

		tx_chn->tx_chn =
			k3_udma_glue_request_tx_chn(dev,
						    tx_chn->tx_chn_name,
						    &tx_cfg);
		if (IS_ERR(tx_chn->tx_chn)) {
			ret = PTR_ERR(tx_chn->tx_chn);
			dev_err(dev, "Failed to request tx dma channel %d\n",
				ret);
			goto err;
		}

		tx_chn->irq = k3_udma_glue_tx_get_irq(tx_chn->tx_chn);
		if (tx_chn->irq <= 0) {
			dev_err(dev, "Failed to get tx dma irq %d\n",
				tx_chn->irq);
			goto err;
		}

		snprintf(tx_chn->tx_chn_name,
			 sizeof(tx_chn->tx_chn_name), "%s-tx%d",
			 dev_name(dev), tx_chn->id);
	}

err:
	i = devm_add_action(dev, am65_cpsw_nuss_free_tx_chns, common);
	if (i) {
		dev_err(dev, "failed to add free_tx_chns action %d", i);
		return i;
	}

	return ret;
}

static void am65_cpsw_nuss_free_rx_chns(void *data)
{
	struct am65_cpsw_common *common = data;
	struct am65_cpsw_rx_chn *rx_chn;

	rx_chn = &common->rx_chns;

	if (!IS_ERR_OR_NULL(rx_chn->rx_chn))
		k3_udma_glue_release_rx_chn(rx_chn->rx_chn);

	if (!IS_ERR_OR_NULL(rx_chn->desc_pool))
		k3_cppi_desc_pool_destroy(rx_chn->desc_pool);
}

static int am65_cpsw_nuss_init_rx_chns(struct am65_cpsw_common *common)
{
	struct am65_cpsw_rx_chn *rx_chn = &common->rx_chns;
	struct k3_udma_glue_rx_channel_cfg rx_cfg = { 0 };
	u32  max_desc_num = AM65_CPSW_MAX_RX_DESC;
	struct device *dev = common->dev;
	u32 hdesc_size;
	u32 fdqring_id;
	int i, ret = 0;

	hdesc_size = cppi5_hdesc_calc_size(true, AM65_CPSW_NAV_PS_DATA_SIZE,
					   AM65_CPSW_NAV_SW_DATA_SIZE);

	rx_cfg.swdata_size = AM65_CPSW_NAV_SW_DATA_SIZE;
	rx_cfg.flow_id_num = AM65_CPSW_MAX_RX_FLOWS;
	rx_cfg.flow_id_base = common->rx_flow_id_base;

	/* init all flows */
	rx_chn->dev = dev;
	rx_chn->descs_num = max_desc_num;
	rx_chn->desc_pool = k3_cppi_desc_pool_create_name(dev,
							  rx_chn->descs_num,
							  hdesc_size, "rx");
	if (IS_ERR(rx_chn->desc_pool)) {
		ret = PTR_ERR(rx_chn->desc_pool);
		dev_err(dev, "Failed to create rx poll %d\n", ret);
		goto err;
	}

	rx_chn->rx_chn = k3_udma_glue_request_rx_chn(dev, "rx", &rx_cfg);
	if (IS_ERR(rx_chn->rx_chn)) {
		ret = PTR_ERR(rx_chn->rx_chn);
		dev_err(dev, "Failed to request rx dma channel %d\n", ret);
		goto err;
	}

	common->rx_flow_id_base =
			k3_udma_glue_rx_get_flow_id_base(rx_chn->rx_chn);
	dev_info(dev, "set new flow-id-base %u\n", common->rx_flow_id_base);

	fdqring_id = K3_RINGACC_RING_ID_ANY;
	for (i = 0; i < rx_cfg.flow_id_num; i++) {
		struct k3_ring_cfg rxring_cfg = {
			.elm_size = K3_RINGACC_RING_ELSIZE_8,
			.mode = K3_RINGACC_RING_MODE_RING,
			.flags = 0,
		};
		struct k3_ring_cfg fdqring_cfg = {
			.elm_size = K3_RINGACC_RING_ELSIZE_8,
			.mode = K3_RINGACC_RING_MODE_MESSAGE,
			.flags = K3_RINGACC_RING_SHARED,
		};
		struct k3_udma_glue_rx_flow_cfg rx_flow_cfg = {
			.rx_cfg = rxring_cfg,
			.rxfdq_cfg = fdqring_cfg,
			.ring_rxq_id = K3_RINGACC_RING_ID_ANY,
			.src_tag_lo_sel =
				K3_UDMA_GLUE_SRC_TAG_LO_USE_REMOTE_SRC_TAG,
		};

		rx_flow_cfg.ring_rxfdq0_id = fdqring_id;
		rx_flow_cfg.rx_cfg.size = max_desc_num;
		rx_flow_cfg.rxfdq_cfg.size = max_desc_num;

		ret = k3_udma_glue_rx_flow_init(rx_chn->rx_chn,
						i, &rx_flow_cfg);
		if (ret) {
			dev_err(dev, "Failed to init rx flow%d %d\n", i, ret);
			goto err;
		}
		if (!i)
			fdqring_id =
				k3_udma_glue_rx_flow_get_fdq_id(rx_chn->rx_chn,
								i);

		rx_chn->irq = k3_udma_glue_rx_get_irq(rx_chn->rx_chn, i);

		if (rx_chn->irq <= 0) {
			dev_err(dev, "Failed to get rx dma irq %d\n",
				rx_chn->irq);
			ret = -ENXIO;
			goto err;
		}
	}

err:
	i = devm_add_action(dev, am65_cpsw_nuss_free_rx_chns, common);
	if (i) {
		dev_err(dev, "failed to add free_rx_chns action %d", i);
		return i;
	}

	return ret;
}

static int am65_cpsw_nuss_init_host_p(struct am65_cpsw_common *common)
{
	struct am65_cpsw_host *host_p = am65_common_get_host(common);

	host_p->common = common;
	host_p->port_base = common->cpsw_base + AM65_CPSW_NU_PORTS_BASE;
	host_p->stat_base = common->cpsw_base + AM65_CPSW_NU_STATS_BASE;

	return 0;
}

static int am65_cpsw_am654_get_efuse_macid(struct device_node *of_node,
					   int slave, u8 *mac_addr)
{
	u32 mac_lo, mac_hi, offset;
	struct regmap *syscon;
	int ret;

	syscon = syscon_regmap_lookup_by_phandle(of_node, "ti,syscon-efuse");
	if (IS_ERR(syscon)) {
		if (PTR_ERR(syscon) == -ENODEV)
			return 0;
		return PTR_ERR(syscon);
	}

	ret = of_property_read_u32_index(of_node, "ti,syscon-efuse", 1,
					 &offset);
	if (ret)
		return ret;

	regmap_read(syscon, offset, &mac_lo);
	regmap_read(syscon, offset + 4, &mac_hi);

	mac_addr[0] = (mac_hi >> 8) & 0xff;
	mac_addr[1] = mac_hi & 0xff;
	mac_addr[2] = (mac_lo >> 24) & 0xff;
	mac_addr[3] = (mac_lo >> 16) & 0xff;
	mac_addr[4] = (mac_lo >> 8) & 0xff;
	mac_addr[5] = mac_lo & 0xff;

	return 0;
}

static int am65_cpsw_nuss_init_slave_ports(struct am65_cpsw_common *common)
{
	struct device_node *node, *port_np;
	struct device *dev = common->dev;
	int ret;

	node = of_get_child_by_name(dev->of_node, "ethernet-ports");
	if (!node)
		return -ENOENT;

	for_each_child_of_node(node, port_np) {
		struct am65_cpsw_port *port;
		const void *mac_addr;
		u32 port_id;

		/* it is not a slave port node, continue */
		if (strcmp(port_np->name, "port"))
			continue;

		ret = of_property_read_u32(port_np, "reg", &port_id);
		if (ret < 0) {
			dev_err(dev, "%pOF error reading port_id %d\n",
				port_np, ret);
			return ret;
		}

		if (!port_id || port_id > common->port_num) {
			dev_err(dev, "%pOF has invalid port_id %u %s\n",
				port_np, port_id, port_np->name);
			return -EINVAL;
		}

		port = am65_common_get_port(common, port_id);
		port->port_id = port_id;
		port->common = common;
		port->port_base = common->cpsw_base + AM65_CPSW_NU_PORTS_BASE +
				  AM65_CPSW_NU_PORTS_OFFSET * (port_id);
		port->stat_base = common->cpsw_base + AM65_CPSW_NU_STATS_BASE +
				  (AM65_CPSW_NU_STATS_PORT_OFFSET * port_id);
		port->name = of_get_property(port_np, "label", NULL);

		port->disabled = !of_device_is_available(port_np);
		if (port->disabled)
			continue;

		port->slave.ifphy = devm_of_phy_get(dev, port_np, NULL);
		if (IS_ERR(port->slave.ifphy)) {
			ret = PTR_ERR(port->slave.ifphy);
			dev_err(dev, "%pOF error retrieving port phy: %d\n",
				port_np, ret);
			return ret;
		}

		port->slave.mac_only =
				of_property_read_bool(port_np, "ti,mac-only");

		/* get phy/link info */
		if (of_phy_is_fixed_link(port_np)) {
			ret = of_phy_register_fixed_link(port_np);
			if (ret) {
				if (ret != -EPROBE_DEFER)
					dev_err(dev, "%pOF failed to register fixed-link phy: %d\n",
						port_np, ret);
				return ret;
			}
			port->slave.phy_node = of_node_get(port_np);
		} else {
			port->slave.phy_node =
				of_parse_phandle(port_np, "phy-handle", 0);
		}

		if (!port->slave.phy_node) {
			dev_err(dev,
				"slave[%d] no phy found\n", port_id);
			return -ENODEV;
		}

		ret = of_get_phy_mode(port_np, &port->slave.phy_if);
		if (ret) {
			dev_err(dev, "%pOF read phy-mode err %d\n",
				port_np, ret);
			return ret;
		}

		port->slave.mac_sl = cpsw_sl_get("am65", dev, port->port_base);
		if (IS_ERR(port->slave.mac_sl))
			return PTR_ERR(port->slave.mac_sl);

		mac_addr = of_get_mac_address(port_np);
		if (!IS_ERR(mac_addr)) {
			ether_addr_copy(port->slave.mac_addr, mac_addr);
		} else if (am65_cpsw_am654_get_efuse_macid(port_np,
							   port->port_id,
							   port->slave.mac_addr) ||
			   !is_valid_ether_addr(port->slave.mac_addr)) {
			random_ether_addr(port->slave.mac_addr);
			dev_err(dev, "Use random MAC address\n");
		}
	}
	of_node_put(node);

	return 0;
}

static void am65_cpsw_pcpu_stats_free(void *data)
{
	struct am65_cpsw_ndev_stats __percpu *stats = data;

	free_percpu(stats);
}

static int am65_cpsw_nuss_init_ndev_2g(struct am65_cpsw_common *common)
{
	struct am65_cpsw_ndev_priv *ndev_priv;
	struct device *dev = common->dev;
	struct am65_cpsw_port *port;
	int ret;

	port = am65_common_get_port(common, 1);

	/* alloc netdev */
	port->ndev = devm_alloc_etherdev_mqs(common->dev,
					     sizeof(struct am65_cpsw_ndev_priv),
					     AM65_CPSW_MAX_TX_QUEUES,
					     AM65_CPSW_MAX_RX_QUEUES);
	if (!port->ndev) {
		dev_err(dev, "error allocating slave net_device %u\n",
			port->port_id);
		return -ENOMEM;
	}

	ndev_priv = netdev_priv(port->ndev);
	ndev_priv->port = port;
	ndev_priv->msg_enable = AM65_CPSW_DEBUG;
	SET_NETDEV_DEV(port->ndev, dev);

	ether_addr_copy(port->ndev->dev_addr, port->slave.mac_addr);

	port->ndev->min_mtu = AM65_CPSW_MIN_PACKET_SIZE;
	port->ndev->max_mtu = AM65_CPSW_MAX_PACKET_SIZE;
	port->ndev->hw_features = NETIF_F_SG |
				  NETIF_F_RXCSUM |
				  NETIF_F_HW_CSUM;
	port->ndev->features = port->ndev->hw_features |
			       NETIF_F_HW_VLAN_CTAG_FILTER;
	port->ndev->vlan_features |=  NETIF_F_SG;
	port->ndev->netdev_ops = &am65_cpsw_nuss_netdev_ops_2g;
	port->ndev->ethtool_ops = &am65_cpsw_ethtool_ops_slave;

	/* Disable TX checksum offload by default due to HW bug */
	if (common->pdata->quirks & AM65_CPSW_QUIRK_I2027_NO_TX_CSUM)
		port->ndev->features &= ~NETIF_F_HW_CSUM;

	ndev_priv->stats = netdev_alloc_pcpu_stats(struct am65_cpsw_ndev_stats);
	if (!ndev_priv->stats)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, am65_cpsw_pcpu_stats_free,
				       ndev_priv->stats);
	if (ret) {
		dev_err(dev, "failed to add percpu stat free action %d", ret);
		return ret;
	}

	netif_napi_add(port->ndev, &common->napi_rx,
		       am65_cpsw_nuss_rx_poll, NAPI_POLL_WEIGHT);

	common->pf_p0_rx_ptype_rrobin = false;

	return ret;
}

static int am65_cpsw_nuss_ndev_add_napi_2g(struct am65_cpsw_common *common)
{
	struct device *dev = common->dev;
	struct am65_cpsw_port *port;
	int i, ret = 0;

	port = am65_common_get_port(common, 1);

	for (i = 0; i < common->tx_ch_num; i++) {
		struct am65_cpsw_tx_chn *tx_chn = &common->tx_chns[i];

		netif_tx_napi_add(port->ndev, &tx_chn->napi_tx,
				  am65_cpsw_nuss_tx_poll, NAPI_POLL_WEIGHT);

		ret = devm_request_irq(dev, tx_chn->irq,
				       am65_cpsw_nuss_tx_irq,
				       0, tx_chn->tx_chn_name, tx_chn);
		if (ret) {
			dev_err(dev, "failure requesting tx%u irq %u, %d\n",
				tx_chn->id, tx_chn->irq, ret);
			goto err;
		}
	}

err:
	return ret;
}

static int am65_cpsw_nuss_ndev_reg_2g(struct am65_cpsw_common *common)
{
	struct device *dev = common->dev;
	struct am65_cpsw_port *port;
	int ret = 0;

	port = am65_common_get_port(common, 1);
	ret = am65_cpsw_nuss_ndev_add_napi_2g(common);
	if (ret)
		goto err;

	ret = devm_request_irq(dev, common->rx_chns.irq,
			       am65_cpsw_nuss_rx_irq,
			       0, dev_name(dev), common);
	if (ret) {
		dev_err(dev, "failure requesting rx irq %u, %d\n",
			common->rx_chns.irq, ret);
		goto err;
	}

	ret = register_netdev(port->ndev);
	if (ret)
		dev_err(dev, "error registering slave net device %d\n", ret);

	/* can't auto unregister ndev using devm_add_action() due to
	 * devres release sequence in DD core for DMA
	 */
err:
	return ret;
}

int am65_cpsw_nuss_update_tx_chns(struct am65_cpsw_common *common, int num_tx)
{
	int ret;

	common->tx_ch_num = num_tx;
	ret = am65_cpsw_nuss_init_tx_chns(common);
	if (ret)
		return ret;

	return am65_cpsw_nuss_ndev_add_napi_2g(common);
}

static void am65_cpsw_nuss_cleanup_ndev(struct am65_cpsw_common *common)
{
	struct am65_cpsw_port *port;
	int i;

	for (i = 0; i < common->port_num; i++) {
		port = &common->ports[i];
		if (port->ndev)
			unregister_netdev(port->ndev);
	}
}

static const struct am65_cpsw_pdata am65x_sr1_0 = {
	.quirks = AM65_CPSW_QUIRK_I2027_NO_TX_CSUM,
};

static const struct am65_cpsw_pdata j721e_sr1_0 = {
	.quirks = 0,
};

static const struct of_device_id am65_cpsw_nuss_of_mtable[] = {
	{ .compatible = "ti,am654-cpsw-nuss", .data = &am65x_sr1_0 },
	{ .compatible = "ti,j721e-cpsw-nuss", .data = &j721e_sr1_0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, am65_cpsw_nuss_of_mtable);

static int am65_cpsw_nuss_probe(struct platform_device *pdev)
{
	struct cpsw_ale_params ale_params;
	const struct of_device_id *of_id;
	struct device *dev = &pdev->dev;
	struct am65_cpsw_common *common;
	struct device_node *node;
	struct resource *res;
	int ret, i;

	common = devm_kzalloc(dev, sizeof(struct am65_cpsw_common), GFP_KERNEL);
	if (!common)
		return -ENOMEM;
	common->dev = dev;

	of_id = of_match_device(am65_cpsw_nuss_of_mtable, dev);
	if (!of_id)
		return -EINVAL;
	common->pdata = of_id->data;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cpsw_nuss");
	common->ss_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(common->ss_base))
		return PTR_ERR(common->ss_base);
	common->cpsw_base = common->ss_base + AM65_CPSW_CPSW_NU_BASE;

	node = of_get_child_by_name(dev->of_node, "ethernet-ports");
	if (!node)
		return -ENOENT;
	common->port_num = of_get_child_count(node);
	if (common->port_num < 1 || common->port_num > AM65_CPSW_MAX_PORTS)
		return -ENOENT;
	of_node_put(node);

	if (common->port_num != 1)
		return -EOPNOTSUPP;

	common->rx_flow_id_base = -1;
	init_completion(&common->tdown_complete);
	common->tx_ch_num = 1;

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (ret) {
		dev_err(dev, "error setting dma mask: %d\n", ret);
		return ret;
	}

	common->ports = devm_kcalloc(dev, common->port_num,
				     sizeof(*common->ports),
				     GFP_KERNEL);
	if (!common->ports)
		return -ENOMEM;

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		pm_runtime_disable(dev);
		return ret;
	}

	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	/* We do not want to force this, as in some cases may not have child */
	if (ret)
		dev_warn(dev, "populating child nodes err:%d\n", ret);

	am65_cpsw_nuss_get_ver(common);

	/* init tx channels */
	ret = am65_cpsw_nuss_init_tx_chns(common);
	if (ret)
		goto err_of_clear;
	ret = am65_cpsw_nuss_init_rx_chns(common);
	if (ret)
		goto err_of_clear;

	ret = am65_cpsw_nuss_init_host_p(common);
	if (ret)
		goto err_of_clear;

	ret = am65_cpsw_nuss_init_slave_ports(common);
	if (ret)
		goto err_of_clear;

	/* init common data */
	ale_params.dev = dev;
	ale_params.ale_ageout = AM65_CPSW_ALE_AGEOUT_DEFAULT;
	ale_params.ale_entries = 0;
	ale_params.ale_ports = common->port_num + 1;
	ale_params.ale_regs = common->cpsw_base + AM65_CPSW_NU_ALE_BASE;
	ale_params.nu_switch_ale = true;

	common->ale = cpsw_ale_create(&ale_params);
	if (!common->ale) {
		dev_err(dev, "error initializing ale engine\n");
		goto err_of_clear;
	}

	/* init ports */
	for (i = 0; i < common->port_num; i++)
		am65_cpsw_nuss_slave_disable_unused(&common->ports[i]);

	dev_set_drvdata(dev, common);

	ret = am65_cpsw_nuss_init_ndev_2g(common);
	if (ret)
		goto err_of_clear;

	ret = am65_cpsw_nuss_ndev_reg_2g(common);
	if (ret)
		goto err_of_clear;

	pm_runtime_put(dev);
	return 0;

err_of_clear:
	of_platform_depopulate(dev);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	return ret;
}

static int am65_cpsw_nuss_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct am65_cpsw_common *common;
	int ret;

	common = dev_get_drvdata(dev);

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&pdev->dev);
		return ret;
	}

	/* must unregister ndevs here because DD release_driver routine calls
	 * dma_deconfigure(dev) before devres_release_all(dev)
	 */
	am65_cpsw_nuss_cleanup_ndev(common);

	of_platform_depopulate(dev);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static struct platform_driver am65_cpsw_nuss_driver = {
	.driver = {
		.name	 = AM65_CPSW_DRV_NAME,
		.of_match_table = am65_cpsw_nuss_of_mtable,
	},
	.probe = am65_cpsw_nuss_probe,
	.remove = am65_cpsw_nuss_remove,
};

module_platform_driver(am65_cpsw_nuss_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Grygorii Strashko <grygorii.strashko@ti.com>");
MODULE_DESCRIPTION("TI AM65 CPSW Ethernet driver");
