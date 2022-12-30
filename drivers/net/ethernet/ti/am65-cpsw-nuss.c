// SPDX-License-Identifier: GPL-2.0
/* Texas Instruments K3 AM65 Ethernet Switch SubSystem Driver
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com/
 *
 */

#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/kmemleak.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_device.h>
#include <linux/phylink.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/rtnetlink.h>
#include <linux/mfd/syscon.h>
#include <linux/sys_soc.h>
#include <linux/dma/ti-cppi5.h>
#include <linux/dma/k3-udma-glue.h>
#include <net/switchdev.h>

#include "cpsw_ale.h"
#include "cpsw_sl.h"
#include "am65-cpsw-nuss.h"
#include "am65-cpsw-switchdev.h"
#include "k3-cppi-desc-pool.h"
#include "am65-cpts.h"

#define AM65_CPSW_SS_BASE	0x0
#define AM65_CPSW_SGMII_BASE	0x100
#define AM65_CPSW_XGMII_BASE	0x2100
#define AM65_CPSW_CPSW_NU_BASE	0x20000
#define AM65_CPSW_NU_PORTS_BASE	0x1000
#define AM65_CPSW_NU_FRAM_BASE	0x12000
#define AM65_CPSW_NU_STATS_BASE	0x1a000
#define AM65_CPSW_NU_ALE_BASE	0x1e000
#define AM65_CPSW_NU_CPTS_BASE	0x1d000

#define AM65_CPSW_NU_PORTS_OFFSET	0x1000
#define AM65_CPSW_NU_STATS_PORT_OFFSET	0x200
#define AM65_CPSW_NU_FRAM_PORT_OFFSET	0x200

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

#define AM65_CPSW_SGMII_CONTROL_REG		0x010
#define AM65_CPSW_SGMII_CONTROL_MR_AN_ENABLE	BIT(0)

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
		 "initializing am65 cpsw nuss version 0x%08X, cpsw version 0x%08X Ports: %u quirks:%08x\n",
		common->nuss_ver,
		common->cpsw_ver,
		common->port_num + 1,
		common->pdata.quirks);
}

static int am65_cpsw_nuss_ndo_slave_add_vid(struct net_device *ndev,
					    __be16 proto, u16 vid)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	u32 port_mask, unreg_mcast = 0;
	int ret;

	if (!common->is_emac_mode)
		return 0;

	if (!netif_running(ndev) || !vid)
		return 0;

	ret = pm_runtime_resume_and_get(common->dev);
	if (ret < 0)
		return ret;

	port_mask = BIT(port->port_id) | ALE_PORT_HOST;
	if (!vid)
		unreg_mcast = port_mask;
	dev_info(common->dev, "Adding vlan %d to vlan filter\n", vid);
	ret = cpsw_ale_vlan_add_modify(common->ale, vid, port_mask,
				       unreg_mcast, port_mask, 0);

	pm_runtime_put(common->dev);
	return ret;
}

static int am65_cpsw_nuss_ndo_slave_kill_vid(struct net_device *ndev,
					     __be16 proto, u16 vid)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	int ret;

	if (!common->is_emac_mode)
		return 0;

	if (!netif_running(ndev) || !vid)
		return 0;

	ret = pm_runtime_resume_and_get(common->dev);
	if (ret < 0)
		return ret;

	dev_info(common->dev, "Removing vlan %d from vlan filter\n", vid);
	ret = cpsw_ale_del_vlan(common->ale, vid,
				BIT(port->port_id) | ALE_PORT_HOST);

	pm_runtime_put(common->dev);
	return ret;
}

static void am65_cpsw_slave_set_promisc(struct am65_cpsw_port *port,
					bool promisc)
{
	struct am65_cpsw_common *common = port->common;

	if (promisc && !common->is_emac_mode) {
		dev_dbg(common->dev, "promisc mode requested in switch mode");
		return;
	}

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
	am65_cpsw_slave_set_promisc(port, promisc);

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
	trans_start = READ_ONCE(netif_txq->trans_start);

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

	buf_dma = dma_map_single(rx_chn->dma_dev, skb->data, pkt_len,
				 DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(rx_chn->dma_dev, buf_dma))) {
		k3_cppi_desc_pool_free(rx_chn->desc_pool, desc_rx);
		dev_err(dev, "Failed to map rx skb buffer\n");
		return -EINVAL;
	}

	cppi5_hdesc_init(desc_rx, CPPI5_INFO0_HDESC_EPIB_PRESENT,
			 AM65_CPSW_NAV_PS_DATA_SIZE);
	k3_udma_glue_rx_dma_to_cppi5_addr(rx_chn->rx_chn, &buf_dma);
	cppi5_hdesc_attach_buf(desc_rx, buf_dma, skb_tailroom(skb), buf_dma, skb_tailroom(skb));
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

static void am65_cpsw_init_host_port_switch(struct am65_cpsw_common *common);
static void am65_cpsw_init_host_port_emac(struct am65_cpsw_common *common);
static void am65_cpsw_init_port_switch_ale(struct am65_cpsw_port *port);
static void am65_cpsw_init_port_emac_ale(struct am65_cpsw_port *port);

static int am65_cpsw_nuss_common_open(struct am65_cpsw_common *common)
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
	writel(AM65_CPSW_P0_REG_CTL_RX_CHECKSUM_EN, host_p->port_base + AM65_CPSW_P0_REG_CTL);

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

	if (common->is_emac_mode)
		am65_cpsw_init_host_port_emac(common);
	else
		am65_cpsw_init_host_port_switch(common);

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
	if (common->rx_irq_disabled) {
		common->rx_irq_disabled = false;
		enable_irq(common->rx_chns.irq);
	}

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

	phylink_stop(port->slave.phylink);

	netif_tx_stop_all_queues(ndev);

	phylink_disconnect_phy(port->slave.phylink);

	ret = am65_cpsw_nuss_common_stop(common);
	if (ret)
		return ret;

	common->usage_count--;
	pm_runtime_put(common->dev);
	return 0;
}

static int cpsw_restore_vlans(struct net_device *vdev, int vid, void *arg)
{
	struct am65_cpsw_port *port = arg;

	if (!vdev)
		return 0;

	return am65_cpsw_nuss_ndo_slave_add_vid(port->ndev, 0, vid);
}

static int am65_cpsw_nuss_ndo_slave_open(struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	int ret, i;
	u32 reg;

	ret = pm_runtime_resume_and_get(common->dev);
	if (ret < 0)
		return ret;

	/* Idle MAC port */
	cpsw_sl_ctl_set(port->slave.mac_sl, CPSW_SL_CTL_CMD_IDLE);
	cpsw_sl_wait_for_idle(port->slave.mac_sl, 100);
	cpsw_sl_ctl_reset(port->slave.mac_sl);

	/* soft reset MAC */
	cpsw_sl_reg_write(port->slave.mac_sl, CPSW_SL_SOFT_RESET, 1);
	mdelay(1);
	reg = cpsw_sl_reg_read(port->slave.mac_sl, CPSW_SL_SOFT_RESET);
	if (reg) {
		dev_err(common->dev, "soft RESET didn't complete\n");
		ret = -ETIMEDOUT;
		goto runtime_put;
	}

	/* Notify the stack of the actual queue counts. */
	ret = netif_set_real_num_tx_queues(ndev, common->tx_ch_num);
	if (ret) {
		dev_err(common->dev, "cannot set real number of tx queues\n");
		goto runtime_put;
	}

	ret = netif_set_real_num_rx_queues(ndev, AM65_CPSW_MAX_RX_QUEUES);
	if (ret) {
		dev_err(common->dev, "cannot set real number of rx queues\n");
		goto runtime_put;
	}

	for (i = 0; i < common->tx_ch_num; i++)
		netdev_tx_reset_queue(netdev_get_tx_queue(ndev, i));

	ret = am65_cpsw_nuss_common_open(common);
	if (ret)
		goto runtime_put;

	common->usage_count++;

	am65_cpsw_port_set_sl_mac(port, ndev->dev_addr);

	if (common->is_emac_mode)
		am65_cpsw_init_port_emac_ale(port);
	else
		am65_cpsw_init_port_switch_ale(port);

	/* mac_sl should be configured via phy-link interface */
	am65_cpsw_sl_ctl_reset(port);

	ret = phylink_of_phy_connect(port->slave.phylink, port->slave.phy_node, 0);
	if (ret)
		goto error_cleanup;

	/* restore vlan configurations */
	vlan_for_each(ndev, cpsw_restore_vlans, port);

	phylink_start(port->slave.phylink);

	return 0;

error_cleanup:
	am65_cpsw_nuss_ndo_slave_stop(ndev);
	return ret;

runtime_put:
	pm_runtime_put(common->dev);
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
	k3_udma_glue_rx_cppi5_to_dma_addr(rx_chn->rx_chn, &buf_dma);

	dma_unmap_single(rx_chn->dma_dev, buf_dma, buf_dma_len, DMA_FROM_DEVICE);
	k3_cppi_desc_pool_free(rx_chn->desc_pool, desc_rx);

	dev_kfree_skb_any(skb);
}

static void am65_cpsw_nuss_rx_ts(struct sk_buff *skb, u32 *psdata)
{
	struct skb_shared_hwtstamps *ssh;
	u64 ns;

	ns = ((u64)psdata[1] << 32) | psdata[0];

	ssh = skb_hwtstamps(skb);
	memset(ssh, 0, sizeof(*ssh));
	ssh->hwtstamp = ns_to_ktime(ns);
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

	if (cppi5_desc_is_tdcm(desc_dma)) {
		dev_dbg(dev, "%s RX tdown flow: %u\n", __func__, flow_idx);
		return 0;
	}

	desc_rx = k3_cppi_desc_pool_dma2virt(rx_chn->desc_pool, desc_dma);
	dev_dbg(dev, "%s flow_idx: %u desc %pad\n",
		__func__, flow_idx, &desc_dma);

	swdata = cppi5_hdesc_get_swdata(desc_rx);
	skb = *swdata;
	cppi5_hdesc_get_obuf(desc_rx, &buf_dma, &buf_dma_len);
	k3_udma_glue_rx_cppi5_to_dma_addr(rx_chn->rx_chn, &buf_dma);
	pkt_len = cppi5_hdesc_get_pktlen(desc_rx);
	cppi5_desc_get_tags_ids(&desc_rx->hdr, &port_id, NULL);
	dev_dbg(dev, "%s rx port_id:%d\n", __func__, port_id);
	port = am65_common_get_port(common, port_id);
	ndev = port->ndev;
	skb->dev = ndev;

	psdata = cppi5_hdesc_get_psdata(desc_rx);
	/* add RX timestamp */
	if (port->rx_ts_enabled)
		am65_cpsw_nuss_rx_ts(skb, psdata);
	csum_info = psdata[2];
	dev_dbg(dev, "%s rx csum_info:%#x\n", __func__, csum_info);

	dma_unmap_single(rx_chn->dma_dev, buf_dma, buf_dma_len, DMA_FROM_DEVICE);

	k3_cppi_desc_pool_free(rx_chn->desc_pool, desc_rx);

	new_skb = netdev_alloc_skb_ip_align(ndev, AM65_CPSW_MAX_PACKET_SIZE);
	if (new_skb) {
		ndev_priv = netdev_priv(ndev);
		am65_cpsw_nuss_set_offload_fwd_mark(skb, ndev_priv->offload_fwd_mark);
		skb_put(skb, pkt_len);
		skb->protocol = eth_type_trans(skb, ndev);
		am65_cpsw_nuss_rx_csum(skb, csum_info);
		napi_gro_receive(&common->napi_rx, skb);

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

	if (num_rx < budget && napi_complete_done(napi_rx, num_rx)) {
		if (common->rx_irq_disabled) {
			common->rx_irq_disabled = false;
			enable_irq(common->rx_chns.irq);
		}
	}

	return num_rx;
}

static void am65_cpsw_nuss_xmit_free(struct am65_cpsw_tx_chn *tx_chn,
				     struct cppi5_host_desc_t *desc)
{
	struct cppi5_host_desc_t *first_desc, *next_desc;
	dma_addr_t buf_dma, next_desc_dma;
	u32 buf_dma_len;

	first_desc = desc;
	next_desc = first_desc;

	cppi5_hdesc_get_obuf(first_desc, &buf_dma, &buf_dma_len);
	k3_udma_glue_tx_cppi5_to_dma_addr(tx_chn->tx_chn, &buf_dma);

	dma_unmap_single(tx_chn->dma_dev, buf_dma, buf_dma_len, DMA_TO_DEVICE);

	next_desc_dma = cppi5_hdesc_get_next_hbdesc(first_desc);
	k3_udma_glue_tx_cppi5_to_dma_addr(tx_chn->tx_chn, &next_desc_dma);
	while (next_desc_dma) {
		next_desc = k3_cppi_desc_pool_dma2virt(tx_chn->desc_pool,
						       next_desc_dma);
		cppi5_hdesc_get_obuf(next_desc, &buf_dma, &buf_dma_len);
		k3_udma_glue_tx_cppi5_to_dma_addr(tx_chn->tx_chn, &buf_dma);

		dma_unmap_page(tx_chn->dma_dev, buf_dma, buf_dma_len,
			       DMA_TO_DEVICE);

		next_desc_dma = cppi5_hdesc_get_next_hbdesc(next_desc);
		k3_udma_glue_tx_cppi5_to_dma_addr(tx_chn->tx_chn, &next_desc_dma);

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
	am65_cpsw_nuss_xmit_free(tx_chn, desc_tx);

	dev_kfree_skb_any(skb);
}

static struct sk_buff *
am65_cpsw_nuss_tx_compl_packet(struct am65_cpsw_tx_chn *tx_chn,
			       dma_addr_t desc_dma)
{
	struct am65_cpsw_ndev_priv *ndev_priv;
	struct am65_cpsw_ndev_stats *stats;
	struct cppi5_host_desc_t *desc_tx;
	struct net_device *ndev;
	struct sk_buff *skb;
	void **swdata;

	desc_tx = k3_cppi_desc_pool_dma2virt(tx_chn->desc_pool,
					     desc_dma);
	swdata = cppi5_hdesc_get_swdata(desc_tx);
	skb = *(swdata);
	am65_cpsw_nuss_xmit_free(tx_chn, desc_tx);

	ndev = skb->dev;

	am65_cpts_tx_timestamp(tx_chn->common->cpts, skb);

	ndev_priv = netdev_priv(ndev);
	stats = this_cpu_ptr(ndev_priv->stats);
	u64_stats_update_begin(&stats->syncp);
	stats->tx_packets++;
	stats->tx_bytes += skb->len;
	u64_stats_update_end(&stats->syncp);

	return skb;
}

static void am65_cpsw_nuss_tx_wake(struct am65_cpsw_tx_chn *tx_chn, struct net_device *ndev,
				   struct netdev_queue *netif_txq)
{
	if (netif_tx_queue_stopped(netif_txq)) {
		/* Check whether the queue is stopped due to stalled
		 * tx dma, if the queue is stopped then wake the queue
		 * as we have free desc for tx
		 */
		__netif_tx_lock(netif_txq, smp_processor_id());
		if (netif_running(ndev) &&
		    (k3_cppi_desc_pool_avail(tx_chn->desc_pool) >= MAX_SKB_FRAGS))
			netif_tx_wake_queue(netif_txq);

		__netif_tx_unlock(netif_txq);
	}
}

static int am65_cpsw_nuss_tx_compl_packets(struct am65_cpsw_common *common,
					   int chn, unsigned int budget)
{
	struct device *dev = common->dev;
	struct am65_cpsw_tx_chn *tx_chn;
	struct netdev_queue *netif_txq;
	unsigned int total_bytes = 0;
	struct net_device *ndev;
	struct sk_buff *skb;
	dma_addr_t desc_dma;
	int res, num_tx = 0;

	tx_chn = &common->tx_chns[chn];

	while (true) {
		spin_lock(&tx_chn->lock);
		res = k3_udma_glue_pop_tx_chn(tx_chn->tx_chn, &desc_dma);
		spin_unlock(&tx_chn->lock);
		if (res == -ENODATA)
			break;

		if (cppi5_desc_is_tdcm(desc_dma)) {
			if (atomic_dec_and_test(&common->tdown_cnt))
				complete(&common->tdown_complete);
			break;
		}

		skb = am65_cpsw_nuss_tx_compl_packet(tx_chn, desc_dma);
		total_bytes = skb->len;
		ndev = skb->dev;
		napi_consume_skb(skb, budget);
		num_tx++;

		netif_txq = netdev_get_tx_queue(ndev, chn);

		netdev_tx_completed_queue(netif_txq, num_tx, total_bytes);

		am65_cpsw_nuss_tx_wake(tx_chn, ndev, netif_txq);
	}

	dev_dbg(dev, "%s:%u pkt:%d\n", __func__, chn, num_tx);

	return num_tx;
}

static int am65_cpsw_nuss_tx_compl_packets_2g(struct am65_cpsw_common *common,
					      int chn, unsigned int budget)
{
	struct device *dev = common->dev;
	struct am65_cpsw_tx_chn *tx_chn;
	struct netdev_queue *netif_txq;
	unsigned int total_bytes = 0;
	struct net_device *ndev;
	struct sk_buff *skb;
	dma_addr_t desc_dma;
	int res, num_tx = 0;

	tx_chn = &common->tx_chns[chn];

	while (true) {
		res = k3_udma_glue_pop_tx_chn(tx_chn->tx_chn, &desc_dma);
		if (res == -ENODATA)
			break;

		if (cppi5_desc_is_tdcm(desc_dma)) {
			if (atomic_dec_and_test(&common->tdown_cnt))
				complete(&common->tdown_complete);
			break;
		}

		skb = am65_cpsw_nuss_tx_compl_packet(tx_chn, desc_dma);

		ndev = skb->dev;
		total_bytes += skb->len;
		napi_consume_skb(skb, budget);
		num_tx++;
	}

	if (!num_tx)
		return 0;

	netif_txq = netdev_get_tx_queue(ndev, chn);

	netdev_tx_completed_queue(netif_txq, num_tx, total_bytes);

	am65_cpsw_nuss_tx_wake(tx_chn, ndev, netif_txq);

	dev_dbg(dev, "%s:%u pkt:%d\n", __func__, chn, num_tx);

	return num_tx;
}

static int am65_cpsw_nuss_tx_poll(struct napi_struct *napi_tx, int budget)
{
	struct am65_cpsw_tx_chn *tx_chn = am65_cpsw_napi_to_tx_chn(napi_tx);
	int num_tx;

	if (AM65_CPSW_IS_CPSW2G(tx_chn->common))
		num_tx = am65_cpsw_nuss_tx_compl_packets_2g(tx_chn->common, tx_chn->id, budget);
	else
		num_tx = am65_cpsw_nuss_tx_compl_packets(tx_chn->common, tx_chn->id, budget);

	if (num_tx >= budget)
		return budget;

	if (napi_complete_done(napi_tx, num_tx))
		enable_irq(tx_chn->irq);

	return 0;
}

static irqreturn_t am65_cpsw_nuss_rx_irq(int irq, void *dev_id)
{
	struct am65_cpsw_common *common = dev_id;

	common->rx_irq_disabled = true;
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

	/* SKB TX timestamp */
	if (port->tx_ts_enabled)
		am65_cpts_prep_tx_timestamp(common->cpts, skb);

	q_idx = skb_get_queue_mapping(skb);
	dev_dbg(dev, "%s skb_queue:%d\n", __func__, q_idx);

	tx_chn = &common->tx_chns[q_idx];
	netif_txq = netdev_get_tx_queue(ndev, q_idx);

	/* Map the linear buffer */
	buf_dma = dma_map_single(tx_chn->dma_dev, skb->data, pkt_len,
				 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(tx_chn->dma_dev, buf_dma))) {
		dev_err(dev, "Failed to map tx skb buffer\n");
		ndev->stats.tx_errors++;
		goto err_free_skb;
	}

	first_desc = k3_cppi_desc_pool_alloc(tx_chn->desc_pool);
	if (!first_desc) {
		dev_dbg(dev, "Failed to allocate descriptor\n");
		dma_unmap_single(tx_chn->dma_dev, buf_dma, pkt_len,
				 DMA_TO_DEVICE);
		goto busy_stop_q;
	}

	cppi5_hdesc_init(first_desc, CPPI5_INFO0_HDESC_EPIB_PRESENT,
			 AM65_CPSW_NAV_PS_DATA_SIZE);
	cppi5_desc_set_pktids(&first_desc->hdr, 0, 0x3FFF);
	cppi5_hdesc_set_pkttype(first_desc, 0x7);
	cppi5_desc_set_tags_ids(&first_desc->hdr, 0, port->port_id);

	k3_udma_glue_tx_dma_to_cppi5_addr(tx_chn->tx_chn, &buf_dma);
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

		buf_dma = skb_frag_dma_map(tx_chn->dma_dev, frag, 0, frag_size,
					   DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(tx_chn->dma_dev, buf_dma))) {
			dev_err(dev, "Failed to map tx skb page\n");
			k3_cppi_desc_pool_free(tx_chn->desc_pool, next_desc);
			ndev->stats.tx_errors++;
			goto err_free_descs;
		}

		cppi5_hdesc_reset_hbdesc(next_desc);
		k3_udma_glue_tx_dma_to_cppi5_addr(tx_chn->tx_chn, &buf_dma);
		cppi5_hdesc_attach_buf(next_desc,
				       buf_dma, frag_size, buf_dma, frag_size);

		desc_dma = k3_cppi_desc_pool_virt2dma(tx_chn->desc_pool,
						      next_desc);
		k3_udma_glue_tx_dma_to_cppi5_addr(tx_chn->tx_chn, &desc_dma);
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
	if (AM65_CPSW_IS_CPSW2G(common)) {
		ret = k3_udma_glue_push_tx_chn(tx_chn->tx_chn, first_desc, desc_dma);
	} else {
		spin_lock_bh(&tx_chn->lock);
		ret = k3_udma_glue_push_tx_chn(tx_chn->tx_chn, first_desc, desc_dma);
		spin_unlock_bh(&tx_chn->lock);
	}
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
	am65_cpsw_nuss_xmit_free(tx_chn, first_desc);
err_free_skb:
	ndev->stats.tx_dropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;

busy_free_descs:
	am65_cpsw_nuss_xmit_free(tx_chn, first_desc);
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

	ret = pm_runtime_resume_and_get(common->dev);
	if (ret < 0)
		return ret;

	cpsw_ale_del_ucast(common->ale, ndev->dev_addr,
			   HOST_PORT_NUM, 0, 0);
	cpsw_ale_add_ucast(common->ale, sockaddr->sa_data,
			   HOST_PORT_NUM, ALE_SECURE, 0);

	am65_cpsw_port_set_sl_mac(port, addr);
	eth_commit_mac_addr_change(ndev, sockaddr);

	pm_runtime_put(common->dev);

	return 0;
}

static int am65_cpsw_nuss_hwtstamp_set(struct net_device *ndev,
				       struct ifreq *ifr)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	u32 ts_ctrl, seq_id, ts_ctrl_ltype2, ts_vlan_ltype;
	struct hwtstamp_config cfg;

	if (!IS_ENABLED(CONFIG_TI_K3_AM65_CPTS))
		return -EOPNOTSUPP;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	/* TX HW timestamp */
	switch (cfg.tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		port->rx_ts_enabled = false;
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_NTP_ALL:
		port->rx_ts_enabled = true;
		cfg.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	port->tx_ts_enabled = (cfg.tx_type == HWTSTAMP_TX_ON);

	/* cfg TX timestamp */
	seq_id = (AM65_CPSW_TS_SEQ_ID_OFFSET <<
		  AM65_CPSW_PN_TS_SEQ_ID_OFFSET_SHIFT) | ETH_P_1588;

	ts_vlan_ltype = ETH_P_8021Q;

	ts_ctrl_ltype2 = ETH_P_1588 |
			 AM65_CPSW_PN_TS_CTL_LTYPE2_TS_107 |
			 AM65_CPSW_PN_TS_CTL_LTYPE2_TS_129 |
			 AM65_CPSW_PN_TS_CTL_LTYPE2_TS_130 |
			 AM65_CPSW_PN_TS_CTL_LTYPE2_TS_131 |
			 AM65_CPSW_PN_TS_CTL_LTYPE2_TS_132 |
			 AM65_CPSW_PN_TS_CTL_LTYPE2_TS_319 |
			 AM65_CPSW_PN_TS_CTL_LTYPE2_TS_320 |
			 AM65_CPSW_PN_TS_CTL_LTYPE2_TS_TTL_NONZERO;

	ts_ctrl = AM65_CPSW_TS_EVENT_MSG_TYPE_BITS <<
		  AM65_CPSW_PN_TS_CTL_MSG_TYPE_EN_SHIFT;

	if (port->tx_ts_enabled)
		ts_ctrl |= AM65_CPSW_TS_TX_ANX_ALL_EN |
			   AM65_CPSW_PN_TS_CTL_TX_VLAN_LT1_EN;

	writel(seq_id, port->port_base + AM65_CPSW_PORTN_REG_TS_SEQ_LTYPE_REG);
	writel(ts_vlan_ltype, port->port_base +
	       AM65_CPSW_PORTN_REG_TS_VLAN_LTYPE_REG);
	writel(ts_ctrl_ltype2, port->port_base +
	       AM65_CPSW_PORTN_REG_TS_CTL_LTYPE2);
	writel(ts_ctrl, port->port_base + AM65_CPSW_PORTN_REG_TS_CTL);

	/* en/dis RX timestamp */
	am65_cpts_rx_enable(common->cpts, port->rx_ts_enabled);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static int am65_cpsw_nuss_hwtstamp_get(struct net_device *ndev,
				       struct ifreq *ifr)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);
	struct hwtstamp_config cfg;

	if (!IS_ENABLED(CONFIG_TI_K3_AM65_CPTS))
		return -EOPNOTSUPP;

	cfg.flags = 0;
	cfg.tx_type = port->tx_ts_enabled ?
		      HWTSTAMP_TX_ON : HWTSTAMP_TX_OFF;
	cfg.rx_filter = port->rx_ts_enabled ?
			HWTSTAMP_FILTER_ALL : HWTSTAMP_FILTER_NONE;

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static int am65_cpsw_nuss_ndo_slave_ioctl(struct net_device *ndev,
					  struct ifreq *req, int cmd)
{
	struct am65_cpsw_port *port = am65_ndev_to_port(ndev);

	if (!netif_running(ndev))
		return -EINVAL;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return am65_cpsw_nuss_hwtstamp_set(ndev, req);
	case SIOCGHWTSTAMP:
		return am65_cpsw_nuss_hwtstamp_get(ndev, req);
	}

	return phylink_mii_ioctl(port->slave.phylink, req, cmd);
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
			start = u64_stats_fetch_begin(&cpu_stats->syncp);
			rx_packets = cpu_stats->rx_packets;
			rx_bytes   = cpu_stats->rx_bytes;
			tx_packets = cpu_stats->tx_packets;
			tx_bytes   = cpu_stats->tx_bytes;
		} while (u64_stats_fetch_retry(&cpu_stats->syncp, start));

		stats->rx_packets += rx_packets;
		stats->rx_bytes   += rx_bytes;
		stats->tx_packets += tx_packets;
		stats->tx_bytes   += tx_bytes;
	}

	stats->rx_errors	= dev->stats.rx_errors;
	stats->rx_dropped	= dev->stats.rx_dropped;
	stats->tx_dropped	= dev->stats.tx_dropped;
}

static const struct net_device_ops am65_cpsw_nuss_netdev_ops = {
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
	.ndo_eth_ioctl		= am65_cpsw_nuss_ndo_slave_ioctl,
	.ndo_setup_tc           = am65_cpsw_qos_ndo_setup_tc,
};

static void am65_cpsw_nuss_mac_config(struct phylink_config *config, unsigned int mode,
				      const struct phylink_link_state *state)
{
	struct am65_cpsw_slave_data *slave = container_of(config, struct am65_cpsw_slave_data,
							  phylink_config);
	struct am65_cpsw_port *port = container_of(slave, struct am65_cpsw_port, slave);
	struct am65_cpsw_common *common = port->common;

	if (common->pdata.extra_modes & BIT(state->interface))
		writel(AM65_CPSW_SGMII_CONTROL_MR_AN_ENABLE,
		       port->sgmii_base + AM65_CPSW_SGMII_CONTROL_REG);
}

static void am65_cpsw_nuss_mac_link_down(struct phylink_config *config, unsigned int mode,
					 phy_interface_t interface)
{
	struct am65_cpsw_slave_data *slave = container_of(config, struct am65_cpsw_slave_data,
							  phylink_config);
	struct am65_cpsw_port *port = container_of(slave, struct am65_cpsw_port, slave);
	struct am65_cpsw_common *common = port->common;
	struct net_device *ndev = port->ndev;
	int tmo;

	/* disable forwarding */
	cpsw_ale_control_set(common->ale, port->port_id, ALE_PORT_STATE, ALE_PORT_STATE_DISABLE);

	cpsw_sl_ctl_set(port->slave.mac_sl, CPSW_SL_CTL_CMD_IDLE);

	tmo = cpsw_sl_wait_for_idle(port->slave.mac_sl, 100);
	dev_dbg(common->dev, "down msc_sl %08x tmo %d\n",
		cpsw_sl_reg_read(port->slave.mac_sl, CPSW_SL_MACSTATUS), tmo);

	cpsw_sl_ctl_reset(port->slave.mac_sl);

	am65_cpsw_qos_link_down(ndev);
	netif_tx_stop_all_queues(ndev);
}

static void am65_cpsw_nuss_mac_link_up(struct phylink_config *config, struct phy_device *phy,
				       unsigned int mode, phy_interface_t interface, int speed,
				       int duplex, bool tx_pause, bool rx_pause)
{
	struct am65_cpsw_slave_data *slave = container_of(config, struct am65_cpsw_slave_data,
							  phylink_config);
	struct am65_cpsw_port *port = container_of(slave, struct am65_cpsw_port, slave);
	struct am65_cpsw_common *common = port->common;
	u32 mac_control = CPSW_SL_CTL_GMII_EN;
	struct net_device *ndev = port->ndev;

	if (speed == SPEED_1000)
		mac_control |= CPSW_SL_CTL_GIG;
	if (speed == SPEED_10 && phy_interface_mode_is_rgmii(interface))
		/* Can be used with in band mode only */
		mac_control |= CPSW_SL_CTL_EXT_EN;
	if (speed == SPEED_100 && interface == PHY_INTERFACE_MODE_RMII)
		mac_control |= CPSW_SL_CTL_IFCTL_A;
	if (duplex)
		mac_control |= CPSW_SL_CTL_FULLDUPLEX;

	/* rx_pause/tx_pause */
	if (rx_pause)
		mac_control |= CPSW_SL_CTL_RX_FLOW_EN;

	if (tx_pause)
		mac_control |= CPSW_SL_CTL_TX_FLOW_EN;

	cpsw_sl_ctl_set(port->slave.mac_sl, mac_control);

	/* enable forwarding */
	cpsw_ale_control_set(common->ale, port->port_id, ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

	am65_cpsw_qos_link_up(ndev, speed);
	netif_tx_wake_all_queues(ndev);
}

static const struct phylink_mac_ops am65_cpsw_phylink_mac_ops = {
	.mac_config = am65_cpsw_nuss_mac_config,
	.mac_link_down = am65_cpsw_nuss_mac_link_down,
	.mac_link_up = am65_cpsw_nuss_mac_link_up,
};

static void am65_cpsw_nuss_slave_disable_unused(struct am65_cpsw_port *port)
{
	struct am65_cpsw_common *common = port->common;

	if (!port->disabled)
		return;

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

		if (!IS_ERR_OR_NULL(tx_chn->desc_pool))
			k3_cppi_desc_pool_destroy(tx_chn->desc_pool);

		if (!IS_ERR_OR_NULL(tx_chn->tx_chn))
			k3_udma_glue_release_tx_chn(tx_chn->tx_chn);

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

		if (!IS_ERR_OR_NULL(tx_chn->desc_pool))
			k3_cppi_desc_pool_destroy(tx_chn->desc_pool);

		if (!IS_ERR_OR_NULL(tx_chn->tx_chn))
			k3_udma_glue_release_tx_chn(tx_chn->tx_chn);

		memset(tx_chn, 0, sizeof(*tx_chn));
	}
}

static int am65_cpsw_nuss_ndev_add_tx_napi(struct am65_cpsw_common *common)
{
	struct device *dev = common->dev;
	int i, ret = 0;

	for (i = 0; i < common->tx_ch_num; i++) {
		struct am65_cpsw_tx_chn *tx_chn = &common->tx_chns[i];

		netif_napi_add_tx(common->dma_ndev, &tx_chn->napi_tx,
				  am65_cpsw_nuss_tx_poll);

		ret = devm_request_irq(dev, tx_chn->irq,
				       am65_cpsw_nuss_tx_irq,
				       IRQF_TRIGGER_HIGH,
				       tx_chn->tx_chn_name, tx_chn);
		if (ret) {
			dev_err(dev, "failure requesting tx%u irq %u, %d\n",
				tx_chn->id, tx_chn->irq, ret);
			goto err;
		}
	}

err:
	return ret;
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

		spin_lock_init(&tx_chn->lock);
		tx_chn->common = common;
		tx_chn->id = i;
		tx_chn->descs_num = max_desc_num;

		tx_chn->tx_chn =
			k3_udma_glue_request_tx_chn(dev,
						    tx_chn->tx_chn_name,
						    &tx_cfg);
		if (IS_ERR(tx_chn->tx_chn)) {
			ret = dev_err_probe(dev, PTR_ERR(tx_chn->tx_chn),
					    "Failed to request tx dma channel\n");
			goto err;
		}
		tx_chn->dma_dev = k3_udma_glue_tx_get_dma_device(tx_chn->tx_chn);

		tx_chn->desc_pool = k3_cppi_desc_pool_create_name(tx_chn->dma_dev,
								  tx_chn->descs_num,
								  hdesc_size,
								  tx_chn->tx_chn_name);
		if (IS_ERR(tx_chn->desc_pool)) {
			ret = PTR_ERR(tx_chn->desc_pool);
			dev_err(dev, "Failed to create poll %d\n", ret);
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

	ret = am65_cpsw_nuss_ndev_add_tx_napi(common);
	if (ret) {
		dev_err(dev, "Failed to add tx NAPI %d\n", ret);
		goto err;
	}

err:
	i = devm_add_action(dev, am65_cpsw_nuss_free_tx_chns, common);
	if (i) {
		dev_err(dev, "Failed to add free_tx_chns action %d\n", i);
		return i;
	}

	return ret;
}

static void am65_cpsw_nuss_free_rx_chns(void *data)
{
	struct am65_cpsw_common *common = data;
	struct am65_cpsw_rx_chn *rx_chn;

	rx_chn = &common->rx_chns;

	if (!IS_ERR_OR_NULL(rx_chn->desc_pool))
		k3_cppi_desc_pool_destroy(rx_chn->desc_pool);

	if (!IS_ERR_OR_NULL(rx_chn->rx_chn))
		k3_udma_glue_release_rx_chn(rx_chn->rx_chn);
}

static void am65_cpsw_nuss_remove_rx_chns(void *data)
{
	struct am65_cpsw_common *common = data;
	struct am65_cpsw_rx_chn *rx_chn;
	struct device *dev = common->dev;

	rx_chn = &common->rx_chns;
	devm_remove_action(dev, am65_cpsw_nuss_free_rx_chns, common);

	if (!(rx_chn->irq < 0))
		devm_free_irq(dev, rx_chn->irq, common);

	netif_napi_del(&common->napi_rx);

	if (!IS_ERR_OR_NULL(rx_chn->desc_pool))
		k3_cppi_desc_pool_destroy(rx_chn->desc_pool);

	if (!IS_ERR_OR_NULL(rx_chn->rx_chn))
		k3_udma_glue_release_rx_chn(rx_chn->rx_chn);

	common->rx_flow_id_base = -1;
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

	rx_chn->rx_chn = k3_udma_glue_request_rx_chn(dev, "rx", &rx_cfg);
	if (IS_ERR(rx_chn->rx_chn)) {
		ret = dev_err_probe(dev, PTR_ERR(rx_chn->rx_chn),
				    "Failed to request rx dma channel\n");
		goto err;
	}
	rx_chn->dma_dev = k3_udma_glue_rx_get_dma_device(rx_chn->rx_chn);

	rx_chn->desc_pool = k3_cppi_desc_pool_create_name(rx_chn->dma_dev,
							  rx_chn->descs_num,
							  hdesc_size, "rx");
	if (IS_ERR(rx_chn->desc_pool)) {
		ret = PTR_ERR(rx_chn->desc_pool);
		dev_err(dev, "Failed to create rx poll %d\n", ret);
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
		rx_flow_cfg.rxfdq_cfg.mode = common->pdata.fdqring_mode;

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

	netif_napi_add(common->dma_ndev, &common->napi_rx,
		       am65_cpsw_nuss_rx_poll);

	ret = devm_request_irq(dev, rx_chn->irq,
			       am65_cpsw_nuss_rx_irq,
			       IRQF_TRIGGER_HIGH, dev_name(dev), common);
	if (ret) {
		dev_err(dev, "failure requesting rx irq %u, %d\n",
			rx_chn->irq, ret);
		goto err;
	}

err:
	i = devm_add_action(dev, am65_cpsw_nuss_free_rx_chns, common);
	if (i) {
		dev_err(dev, "Failed to add free_rx_chns action %d\n", i);
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

static int am65_cpsw_init_cpts(struct am65_cpsw_common *common)
{
	struct device *dev = common->dev;
	struct device_node *node;
	struct am65_cpts *cpts;
	void __iomem *reg_base;

	if (!IS_ENABLED(CONFIG_TI_K3_AM65_CPTS))
		return 0;

	node = of_get_child_by_name(dev->of_node, "cpts");
	if (!node) {
		dev_err(dev, "%s cpts not found\n", __func__);
		return -ENOENT;
	}

	reg_base = common->cpsw_base + AM65_CPSW_NU_CPTS_BASE;
	cpts = am65_cpts_create(dev, reg_base, node);
	if (IS_ERR(cpts)) {
		int ret = PTR_ERR(cpts);

		of_node_put(node);
		if (ret == -EOPNOTSUPP) {
			dev_info(dev, "cpts disabled\n");
			return 0;
		}

		dev_err(dev, "cpts create err %d\n", ret);
		return ret;
	}
	common->cpts = cpts;
	/* Forbid PM runtime if CPTS is running.
	 * K3 CPSWxG modules may completely lose context during ON->OFF
	 * transitions depending on integration.
	 * AM65x/J721E MCU CPSW2G: false
	 * J721E MAIN_CPSW9G: true
	 */
	pm_runtime_forbid(dev);

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
		u32 port_id;

		/* it is not a slave port node, continue */
		if (strcmp(port_np->name, "port"))
			continue;

		ret = of_property_read_u32(port_np, "reg", &port_id);
		if (ret < 0) {
			dev_err(dev, "%pOF error reading port_id %d\n",
				port_np, ret);
			goto of_node_put;
		}

		if (!port_id || port_id > common->port_num) {
			dev_err(dev, "%pOF has invalid port_id %u %s\n",
				port_np, port_id, port_np->name);
			ret = -EINVAL;
			goto of_node_put;
		}

		port = am65_common_get_port(common, port_id);
		port->port_id = port_id;
		port->common = common;
		port->port_base = common->cpsw_base + AM65_CPSW_NU_PORTS_BASE +
				  AM65_CPSW_NU_PORTS_OFFSET * (port_id);
		if (common->pdata.extra_modes)
			port->sgmii_base = common->ss_base + AM65_CPSW_SGMII_BASE * (port_id);
		port->stat_base = common->cpsw_base + AM65_CPSW_NU_STATS_BASE +
				  (AM65_CPSW_NU_STATS_PORT_OFFSET * port_id);
		port->name = of_get_property(port_np, "label", NULL);
		port->fetch_ram_base =
				common->cpsw_base + AM65_CPSW_NU_FRAM_BASE +
				(AM65_CPSW_NU_FRAM_PORT_OFFSET * (port_id - 1));

		port->slave.mac_sl = cpsw_sl_get("am65", dev, port->port_base);
		if (IS_ERR(port->slave.mac_sl)) {
			ret = PTR_ERR(port->slave.mac_sl);
			goto of_node_put;
		}

		port->disabled = !of_device_is_available(port_np);
		if (port->disabled) {
			common->disabled_ports_mask |= BIT(port->port_id);
			continue;
		}

		port->slave.ifphy = devm_of_phy_get(dev, port_np, NULL);
		if (IS_ERR(port->slave.ifphy)) {
			ret = PTR_ERR(port->slave.ifphy);
			dev_err(dev, "%pOF error retrieving port phy: %d\n",
				port_np, ret);
			goto of_node_put;
		}

		port->slave.mac_only =
				of_property_read_bool(port_np, "ti,mac-only");

		/* get phy/link info */
		port->slave.phy_node = port_np;
		ret = of_get_phy_mode(port_np, &port->slave.phy_if);
		if (ret) {
			dev_err(dev, "%pOF read phy-mode err %d\n",
				port_np, ret);
			goto of_node_put;
		}

		ret = phy_set_mode_ext(port->slave.ifphy, PHY_MODE_ETHERNET, port->slave.phy_if);
		if (ret)
			goto of_node_put;

		ret = of_get_mac_address(port_np, port->slave.mac_addr);
		if (ret) {
			am65_cpsw_am654_get_efuse_macid(port_np,
							port->port_id,
							port->slave.mac_addr);
			if (!is_valid_ether_addr(port->slave.mac_addr)) {
				eth_random_addr(port->slave.mac_addr);
				dev_err(dev, "Use random MAC address\n");
			}
		}
	}
	of_node_put(node);

	/* is there at least one ext.port */
	if (!(~common->disabled_ports_mask & GENMASK(common->port_num, 1))) {
		dev_err(dev, "No Ext. port are available\n");
		return -ENODEV;
	}

	return 0;

of_node_put:
	of_node_put(port_np);
	of_node_put(node);
	return ret;
}

static void am65_cpsw_pcpu_stats_free(void *data)
{
	struct am65_cpsw_ndev_stats __percpu *stats = data;

	free_percpu(stats);
}

static void am65_cpsw_nuss_phylink_cleanup(struct am65_cpsw_common *common)
{
	struct am65_cpsw_port *port;
	int i;

	for (i = 0; i < common->port_num; i++) {
		port = &common->ports[i];
		if (port->slave.phylink)
			phylink_destroy(port->slave.phylink);
	}
}

static int
am65_cpsw_nuss_init_port_ndev(struct am65_cpsw_common *common, u32 port_idx)
{
	struct am65_cpsw_ndev_priv *ndev_priv;
	struct device *dev = common->dev;
	struct am65_cpsw_port *port;
	struct phylink *phylink;
	int ret;

	port = &common->ports[port_idx];

	if (port->disabled)
		return 0;

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

	eth_hw_addr_set(port->ndev, port->slave.mac_addr);

	port->ndev->min_mtu = AM65_CPSW_MIN_PACKET_SIZE;
	port->ndev->max_mtu = AM65_CPSW_MAX_PACKET_SIZE;
	port->ndev->hw_features = NETIF_F_SG |
				  NETIF_F_RXCSUM |
				  NETIF_F_HW_CSUM |
				  NETIF_F_HW_TC;
	port->ndev->features = port->ndev->hw_features |
			       NETIF_F_HW_VLAN_CTAG_FILTER;
	port->ndev->vlan_features |=  NETIF_F_SG;
	port->ndev->netdev_ops = &am65_cpsw_nuss_netdev_ops;
	port->ndev->ethtool_ops = &am65_cpsw_ethtool_ops_slave;

	/* Configuring Phylink */
	port->slave.phylink_config.dev = &port->ndev->dev;
	port->slave.phylink_config.type = PHYLINK_NETDEV;
	port->slave.phylink_config.mac_capabilities = MAC_SYM_PAUSE | MAC_10 | MAC_100 | MAC_1000FD;
	port->slave.phylink_config.mac_managed_pm = true; /* MAC does PM */

	if (phy_interface_mode_is_rgmii(port->slave.phy_if)) {
		phy_interface_set_rgmii(port->slave.phylink_config.supported_interfaces);
	} else if (port->slave.phy_if == PHY_INTERFACE_MODE_RMII) {
		__set_bit(PHY_INTERFACE_MODE_RMII,
			  port->slave.phylink_config.supported_interfaces);
	} else if (common->pdata.extra_modes & BIT(port->slave.phy_if)) {
		__set_bit(PHY_INTERFACE_MODE_QSGMII,
			  port->slave.phylink_config.supported_interfaces);
	} else {
		dev_err(dev, "selected phy-mode is not supported\n");
		return -EOPNOTSUPP;
	}

	phylink = phylink_create(&port->slave.phylink_config,
				 of_node_to_fwnode(port->slave.phy_node),
				 port->slave.phy_if,
				 &am65_cpsw_phylink_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	port->slave.phylink = phylink;

	/* Disable TX checksum offload by default due to HW bug */
	if (common->pdata.quirks & AM65_CPSW_QUIRK_I2027_NO_TX_CSUM)
		port->ndev->features &= ~NETIF_F_HW_CSUM;

	ndev_priv->stats = netdev_alloc_pcpu_stats(struct am65_cpsw_ndev_stats);
	if (!ndev_priv->stats)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, am65_cpsw_pcpu_stats_free,
				       ndev_priv->stats);
	if (ret)
		dev_err(dev, "failed to add percpu stat free action %d\n", ret);

	if (!common->dma_ndev)
		common->dma_ndev = port->ndev;

	return ret;
}

static int am65_cpsw_nuss_init_ndevs(struct am65_cpsw_common *common)
{
	int ret;
	int i;

	for (i = 0; i < common->port_num; i++) {
		ret = am65_cpsw_nuss_init_port_ndev(common, i);
		if (ret)
			return ret;
	}

	return ret;
}

static void am65_cpsw_nuss_cleanup_ndev(struct am65_cpsw_common *common)
{
	struct am65_cpsw_port *port;
	int i;

	for (i = 0; i < common->port_num; i++) {
		port = &common->ports[i];
		if (port->ndev && port->ndev->reg_state == NETREG_REGISTERED)
			unregister_netdev(port->ndev);
	}
}

static void am65_cpsw_port_offload_fwd_mark_update(struct am65_cpsw_common *common)
{
	int set_val = 0;
	int i;

	if (common->br_members == (GENMASK(common->port_num, 1) & ~common->disabled_ports_mask))
		set_val = 1;

	dev_dbg(common->dev, "set offload_fwd_mark %d\n", set_val);

	for (i = 1; i <= common->port_num; i++) {
		struct am65_cpsw_port *port = am65_common_get_port(common, i);
		struct am65_cpsw_ndev_priv *priv;

		if (!port->ndev)
			continue;

		priv = am65_ndev_to_priv(port->ndev);
		priv->offload_fwd_mark = set_val;
	}
}

bool am65_cpsw_port_dev_check(const struct net_device *ndev)
{
	if (ndev->netdev_ops == &am65_cpsw_nuss_netdev_ops) {
		struct am65_cpsw_common *common = am65_ndev_to_common(ndev);

		return !common->is_emac_mode;
	}

	return false;
}

static int am65_cpsw_netdevice_port_link(struct net_device *ndev,
					 struct net_device *br_ndev,
					 struct netlink_ext_ack *extack)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_ndev_priv *priv = am65_ndev_to_priv(ndev);
	int err;

	if (!common->br_members) {
		common->hw_bridge_dev = br_ndev;
	} else {
		/* This is adding the port to a second bridge, this is
		 * unsupported
		 */
		if (common->hw_bridge_dev != br_ndev)
			return -EOPNOTSUPP;
	}

	err = switchdev_bridge_port_offload(ndev, ndev, NULL, NULL, NULL,
					    false, extack);
	if (err)
		return err;

	common->br_members |= BIT(priv->port->port_id);

	am65_cpsw_port_offload_fwd_mark_update(common);

	return NOTIFY_DONE;
}

static void am65_cpsw_netdevice_port_unlink(struct net_device *ndev)
{
	struct am65_cpsw_common *common = am65_ndev_to_common(ndev);
	struct am65_cpsw_ndev_priv *priv = am65_ndev_to_priv(ndev);

	switchdev_bridge_port_unoffload(ndev, NULL, NULL, NULL);

	common->br_members &= ~BIT(priv->port->port_id);

	am65_cpsw_port_offload_fwd_mark_update(common);

	if (!common->br_members)
		common->hw_bridge_dev = NULL;
}

/* netdev notifier */
static int am65_cpsw_netdevice_event(struct notifier_block *unused,
				     unsigned long event, void *ptr)
{
	struct netlink_ext_ack *extack = netdev_notifier_info_to_extack(ptr);
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info;
	int ret = NOTIFY_DONE;

	if (!am65_cpsw_port_dev_check(ndev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		info = ptr;

		if (netif_is_bridge_master(info->upper_dev)) {
			if (info->linking)
				ret = am65_cpsw_netdevice_port_link(ndev,
								    info->upper_dev,
								    extack);
			else
				am65_cpsw_netdevice_port_unlink(ndev);
		}
		break;
	default:
		return NOTIFY_DONE;
	}

	return notifier_from_errno(ret);
}

static int am65_cpsw_register_notifiers(struct am65_cpsw_common *cpsw)
{
	int ret = 0;

	if (AM65_CPSW_IS_CPSW2G(cpsw) ||
	    !IS_REACHABLE(CONFIG_TI_K3_AM65_CPSW_SWITCHDEV))
		return 0;

	cpsw->am65_cpsw_netdevice_nb.notifier_call = &am65_cpsw_netdevice_event;
	ret = register_netdevice_notifier(&cpsw->am65_cpsw_netdevice_nb);
	if (ret) {
		dev_err(cpsw->dev, "can't register netdevice notifier\n");
		return ret;
	}

	ret = am65_cpsw_switchdev_register_notifiers(cpsw);
	if (ret)
		unregister_netdevice_notifier(&cpsw->am65_cpsw_netdevice_nb);

	return ret;
}

static void am65_cpsw_unregister_notifiers(struct am65_cpsw_common *cpsw)
{
	if (AM65_CPSW_IS_CPSW2G(cpsw) ||
	    !IS_REACHABLE(CONFIG_TI_K3_AM65_CPSW_SWITCHDEV))
		return;

	am65_cpsw_switchdev_unregister_notifiers(cpsw);
	unregister_netdevice_notifier(&cpsw->am65_cpsw_netdevice_nb);
}

static const struct devlink_ops am65_cpsw_devlink_ops = {};

static void am65_cpsw_init_stp_ale_entry(struct am65_cpsw_common *cpsw)
{
	cpsw_ale_add_mcast(cpsw->ale, eth_stp_addr, ALE_PORT_HOST, ALE_SUPER, 0,
			   ALE_MCAST_BLOCK_LEARN_FWD);
}

static void am65_cpsw_init_host_port_switch(struct am65_cpsw_common *common)
{
	struct am65_cpsw_host *host = am65_common_get_host(common);

	writel(common->default_vlan, host->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);

	am65_cpsw_init_stp_ale_entry(common);

	cpsw_ale_control_set(common->ale, HOST_PORT_NUM, ALE_P0_UNI_FLOOD, 1);
	dev_dbg(common->dev, "Set P0_UNI_FLOOD\n");
	cpsw_ale_control_set(common->ale, HOST_PORT_NUM, ALE_PORT_NOLEARN, 0);
}

static void am65_cpsw_init_host_port_emac(struct am65_cpsw_common *common)
{
	struct am65_cpsw_host *host = am65_common_get_host(common);

	writel(0, host->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);

	cpsw_ale_control_set(common->ale, HOST_PORT_NUM, ALE_P0_UNI_FLOOD, 0);
	dev_dbg(common->dev, "unset P0_UNI_FLOOD\n");

	/* learning make no sense in multi-mac mode */
	cpsw_ale_control_set(common->ale, HOST_PORT_NUM, ALE_PORT_NOLEARN, 1);
}

static int am65_cpsw_dl_switch_mode_get(struct devlink *dl, u32 id,
					struct devlink_param_gset_ctx *ctx)
{
	struct am65_cpsw_devlink *dl_priv = devlink_priv(dl);
	struct am65_cpsw_common *common = dl_priv->common;

	dev_dbg(common->dev, "%s id:%u\n", __func__, id);

	if (id != AM65_CPSW_DL_PARAM_SWITCH_MODE)
		return -EOPNOTSUPP;

	ctx->val.vbool = !common->is_emac_mode;

	return 0;
}

static void am65_cpsw_init_port_emac_ale(struct  am65_cpsw_port *port)
{
	struct am65_cpsw_slave_data *slave = &port->slave;
	struct am65_cpsw_common *common = port->common;
	u32 port_mask;

	writel(slave->port_vlan, port->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);

	if (slave->mac_only)
		/* enable mac-only mode on port */
		cpsw_ale_control_set(common->ale, port->port_id,
				     ALE_PORT_MACONLY, 1);

	cpsw_ale_control_set(common->ale, port->port_id, ALE_PORT_NOLEARN, 1);

	port_mask = BIT(port->port_id) | ALE_PORT_HOST;

	cpsw_ale_add_ucast(common->ale, port->ndev->dev_addr,
			   HOST_PORT_NUM, ALE_SECURE, slave->port_vlan);
	cpsw_ale_add_mcast(common->ale, port->ndev->broadcast,
			   port_mask, ALE_VLAN, slave->port_vlan, ALE_MCAST_FWD_2);
}

static void am65_cpsw_init_port_switch_ale(struct am65_cpsw_port *port)
{
	struct am65_cpsw_slave_data *slave = &port->slave;
	struct am65_cpsw_common *cpsw = port->common;
	u32 port_mask;

	cpsw_ale_control_set(cpsw->ale, port->port_id,
			     ALE_PORT_NOLEARN, 0);

	cpsw_ale_add_ucast(cpsw->ale, port->ndev->dev_addr,
			   HOST_PORT_NUM, ALE_SECURE | ALE_BLOCKED | ALE_VLAN,
			   slave->port_vlan);

	port_mask = BIT(port->port_id) | ALE_PORT_HOST;

	cpsw_ale_add_mcast(cpsw->ale, port->ndev->broadcast,
			   port_mask, ALE_VLAN, slave->port_vlan,
			   ALE_MCAST_FWD_2);

	writel(slave->port_vlan, port->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);

	cpsw_ale_control_set(cpsw->ale, port->port_id,
			     ALE_PORT_MACONLY, 0);
}

static int am65_cpsw_dl_switch_mode_set(struct devlink *dl, u32 id,
					struct devlink_param_gset_ctx *ctx)
{
	struct am65_cpsw_devlink *dl_priv = devlink_priv(dl);
	struct am65_cpsw_common *cpsw = dl_priv->common;
	bool switch_en = ctx->val.vbool;
	bool if_running = false;
	int i;

	dev_dbg(cpsw->dev, "%s id:%u\n", __func__, id);

	if (id != AM65_CPSW_DL_PARAM_SWITCH_MODE)
		return -EOPNOTSUPP;

	if (switch_en == !cpsw->is_emac_mode)
		return 0;

	if (!switch_en && cpsw->br_members) {
		dev_err(cpsw->dev, "Remove ports from bridge before disabling switch mode\n");
		return -EINVAL;
	}

	rtnl_lock();

	cpsw->is_emac_mode = !switch_en;

	for (i = 0; i < cpsw->port_num; i++) {
		struct net_device *sl_ndev = cpsw->ports[i].ndev;

		if (!sl_ndev || !netif_running(sl_ndev))
			continue;

		if_running = true;
	}

	if (!if_running) {
		/* all ndevs are down */
		for (i = 0; i < cpsw->port_num; i++) {
			struct net_device *sl_ndev = cpsw->ports[i].ndev;
			struct am65_cpsw_slave_data *slave;

			if (!sl_ndev)
				continue;

			slave = am65_ndev_to_slave(sl_ndev);
			if (switch_en)
				slave->port_vlan = cpsw->default_vlan;
			else
				slave->port_vlan = 0;
		}

		goto exit;
	}

	cpsw_ale_control_set(cpsw->ale, 0, ALE_BYPASS, 1);
	/* clean up ALE table */
	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM, ALE_CLEAR, 1);
	cpsw_ale_control_get(cpsw->ale, HOST_PORT_NUM, ALE_AGEOUT);

	if (switch_en) {
		dev_info(cpsw->dev, "Enable switch mode\n");

		am65_cpsw_init_host_port_switch(cpsw);

		for (i = 0; i < cpsw->port_num; i++) {
			struct net_device *sl_ndev = cpsw->ports[i].ndev;
			struct am65_cpsw_slave_data *slave;
			struct am65_cpsw_port *port;

			if (!sl_ndev)
				continue;

			port = am65_ndev_to_port(sl_ndev);
			slave = am65_ndev_to_slave(sl_ndev);
			slave->port_vlan = cpsw->default_vlan;

			if (netif_running(sl_ndev))
				am65_cpsw_init_port_switch_ale(port);
		}

	} else {
		dev_info(cpsw->dev, "Disable switch mode\n");

		am65_cpsw_init_host_port_emac(cpsw);

		for (i = 0; i < cpsw->port_num; i++) {
			struct net_device *sl_ndev = cpsw->ports[i].ndev;
			struct am65_cpsw_port *port;

			if (!sl_ndev)
				continue;

			port = am65_ndev_to_port(sl_ndev);
			port->slave.port_vlan = 0;
			if (netif_running(sl_ndev))
				am65_cpsw_init_port_emac_ale(port);
		}
	}
	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM, ALE_BYPASS, 0);
exit:
	rtnl_unlock();

	return 0;
}

static const struct devlink_param am65_cpsw_devlink_params[] = {
	DEVLINK_PARAM_DRIVER(AM65_CPSW_DL_PARAM_SWITCH_MODE, "switch_mode",
			     DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     am65_cpsw_dl_switch_mode_get,
			     am65_cpsw_dl_switch_mode_set, NULL),
};

static int am65_cpsw_nuss_register_devlink(struct am65_cpsw_common *common)
{
	struct devlink_port_attrs attrs = {};
	struct am65_cpsw_devlink *dl_priv;
	struct device *dev = common->dev;
	struct devlink_port *dl_port;
	struct am65_cpsw_port *port;
	int ret = 0;
	int i;

	common->devlink =
		devlink_alloc(&am65_cpsw_devlink_ops, sizeof(*dl_priv), dev);
	if (!common->devlink)
		return -ENOMEM;

	dl_priv = devlink_priv(common->devlink);
	dl_priv->common = common;

	/* Provide devlink hook to switch mode when multiple external ports
	 * are present NUSS switchdev driver is enabled.
	 */
	if (!AM65_CPSW_IS_CPSW2G(common) &&
	    IS_ENABLED(CONFIG_TI_K3_AM65_CPSW_SWITCHDEV)) {
		ret = devlink_params_register(common->devlink,
					      am65_cpsw_devlink_params,
					      ARRAY_SIZE(am65_cpsw_devlink_params));
		if (ret) {
			dev_err(dev, "devlink params reg fail ret:%d\n", ret);
			goto dl_unreg;
		}
	}

	for (i = 1; i <= common->port_num; i++) {
		port = am65_common_get_port(common, i);
		dl_port = &port->devlink_port;

		if (port->ndev)
			attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
		else
			attrs.flavour = DEVLINK_PORT_FLAVOUR_UNUSED;
		attrs.phys.port_number = port->port_id;
		attrs.switch_id.id_len = sizeof(resource_size_t);
		memcpy(attrs.switch_id.id, common->switch_id, attrs.switch_id.id_len);
		devlink_port_attrs_set(dl_port, &attrs);

		ret = devlink_port_register(common->devlink, dl_port, port->port_id);
		if (ret) {
			dev_err(dev, "devlink_port reg fail for port %d, ret:%d\n",
				port->port_id, ret);
			goto dl_port_unreg;
		}
	}
	devlink_register(common->devlink);
	return ret;

dl_port_unreg:
	for (i = i - 1; i >= 1; i--) {
		port = am65_common_get_port(common, i);
		dl_port = &port->devlink_port;

		devlink_port_unregister(dl_port);
	}
dl_unreg:
	devlink_free(common->devlink);
	return ret;
}

static void am65_cpsw_unregister_devlink(struct am65_cpsw_common *common)
{
	struct devlink_port *dl_port;
	struct am65_cpsw_port *port;
	int i;

	devlink_unregister(common->devlink);

	for (i = 1; i <= common->port_num; i++) {
		port = am65_common_get_port(common, i);
		dl_port = &port->devlink_port;

		devlink_port_unregister(dl_port);
	}

	if (!AM65_CPSW_IS_CPSW2G(common) &&
	    IS_ENABLED(CONFIG_TI_K3_AM65_CPSW_SWITCHDEV))
		devlink_params_unregister(common->devlink,
					  am65_cpsw_devlink_params,
					  ARRAY_SIZE(am65_cpsw_devlink_params));

	devlink_free(common->devlink);
}

static int am65_cpsw_nuss_register_ndevs(struct am65_cpsw_common *common)
{
	struct device *dev = common->dev;
	struct am65_cpsw_port *port;
	int ret = 0, i;

	/* init tx channels */
	ret = am65_cpsw_nuss_init_tx_chns(common);
	if (ret)
		return ret;
	ret = am65_cpsw_nuss_init_rx_chns(common);
	if (ret)
		return ret;

	ret = am65_cpsw_nuss_register_devlink(common);
	if (ret)
		return ret;

	for (i = 0; i < common->port_num; i++) {
		port = &common->ports[i];

		if (!port->ndev)
			continue;

		SET_NETDEV_DEVLINK_PORT(port->ndev, &port->devlink_port);

		ret = register_netdev(port->ndev);
		if (ret) {
			dev_err(dev, "error registering slave net device%i %d\n",
				i, ret);
			goto err_cleanup_ndev;
		}
	}

	ret = am65_cpsw_register_notifiers(common);
	if (ret)
		goto err_cleanup_ndev;

	/* can't auto unregister ndev using devm_add_action() due to
	 * devres release sequence in DD core for DMA
	 */

	return 0;

err_cleanup_ndev:
	am65_cpsw_nuss_cleanup_ndev(common);
	am65_cpsw_unregister_devlink(common);

	return ret;
}

int am65_cpsw_nuss_update_tx_chns(struct am65_cpsw_common *common, int num_tx)
{
	int ret;

	common->tx_ch_num = num_tx;
	ret = am65_cpsw_nuss_init_tx_chns(common);

	return ret;
}

struct am65_cpsw_soc_pdata {
	u32	quirks_dis;
};

static const struct am65_cpsw_soc_pdata am65x_soc_sr2_0 = {
	.quirks_dis = AM65_CPSW_QUIRK_I2027_NO_TX_CSUM,
};

static const struct soc_device_attribute am65_cpsw_socinfo[] = {
	{ .family = "AM65X",
	  .revision = "SR2.0",
	  .data = &am65x_soc_sr2_0
	},
	{/* sentinel */}
};

static const struct am65_cpsw_pdata am65x_sr1_0 = {
	.quirks = AM65_CPSW_QUIRK_I2027_NO_TX_CSUM,
	.ale_dev_id = "am65x-cpsw2g",
	.fdqring_mode = K3_RINGACC_RING_MODE_MESSAGE,
};

static const struct am65_cpsw_pdata j721e_pdata = {
	.quirks = 0,
	.ale_dev_id = "am65x-cpsw2g",
	.fdqring_mode = K3_RINGACC_RING_MODE_MESSAGE,
};

static const struct am65_cpsw_pdata am64x_cpswxg_pdata = {
	.quirks = 0,
	.ale_dev_id = "am64-cpswxg",
	.fdqring_mode = K3_RINGACC_RING_MODE_RING,
};

static const struct am65_cpsw_pdata j7200_cpswxg_pdata = {
	.quirks = 0,
	.ale_dev_id = "am64-cpswxg",
	.fdqring_mode = K3_RINGACC_RING_MODE_RING,
	.extra_modes = BIT(PHY_INTERFACE_MODE_QSGMII),
};

static const struct of_device_id am65_cpsw_nuss_of_mtable[] = {
	{ .compatible = "ti,am654-cpsw-nuss", .data = &am65x_sr1_0},
	{ .compatible = "ti,j721e-cpsw-nuss", .data = &j721e_pdata},
	{ .compatible = "ti,am642-cpsw-nuss", .data = &am64x_cpswxg_pdata},
	{ .compatible = "ti,j7200-cpswxg-nuss", .data = &j7200_cpswxg_pdata},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, am65_cpsw_nuss_of_mtable);

static void am65_cpsw_nuss_apply_socinfo(struct am65_cpsw_common *common)
{
	const struct soc_device_attribute *soc;

	soc = soc_device_match(am65_cpsw_socinfo);
	if (soc && soc->data) {
		const struct am65_cpsw_soc_pdata *socdata = soc->data;

		/* disable quirks */
		common->pdata.quirks &= ~socdata->quirks_dis;
	}
}

static int am65_cpsw_nuss_probe(struct platform_device *pdev)
{
	struct cpsw_ale_params ale_params = { 0 };
	const struct of_device_id *of_id;
	struct device *dev = &pdev->dev;
	struct am65_cpsw_common *common;
	struct device_node *node;
	struct resource *res;
	struct clk *clk;
	u64 id_temp;
	int ret, i;
	int ale_entries;

	common = devm_kzalloc(dev, sizeof(struct am65_cpsw_common), GFP_KERNEL);
	if (!common)
		return -ENOMEM;
	common->dev = dev;

	of_id = of_match_device(am65_cpsw_nuss_of_mtable, dev);
	if (!of_id)
		return -EINVAL;
	common->pdata = *(const struct am65_cpsw_pdata *)of_id->data;

	am65_cpsw_nuss_apply_socinfo(common);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cpsw_nuss");
	common->ss_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(common->ss_base))
		return PTR_ERR(common->ss_base);
	common->cpsw_base = common->ss_base + AM65_CPSW_CPSW_NU_BASE;
	/* Use device's physical base address as switch id */
	id_temp = cpu_to_be64(res->start);
	memcpy(common->switch_id, &id_temp, sizeof(res->start));

	node = of_get_child_by_name(dev->of_node, "ethernet-ports");
	if (!node)
		return -ENOENT;
	common->port_num = of_get_child_count(node);
	of_node_put(node);
	if (common->port_num < 1 || common->port_num > AM65_CPSW_MAX_PORTS)
		return -ENOENT;

	common->rx_flow_id_base = -1;
	init_completion(&common->tdown_complete);
	common->tx_ch_num = 1;
	common->pf_p0_rx_ptype_rrobin = false;
	common->default_vlan = 1;

	common->ports = devm_kcalloc(dev, common->port_num,
				     sizeof(*common->ports),
				     GFP_KERNEL);
	if (!common->ports)
		return -ENOMEM;

	clk = devm_clk_get(dev, "fck");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "getting fck clock\n");
	common->bus_freq = clk_get_rate(clk);

	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		pm_runtime_disable(dev);
		return ret;
	}

	node = of_get_child_by_name(dev->of_node, "mdio");
	if (!node) {
		dev_warn(dev, "MDIO node not found\n");
	} else if (of_device_is_available(node)) {
		struct platform_device *mdio_pdev;

		mdio_pdev = of_platform_device_create(node, NULL, dev);
		if (!mdio_pdev) {
			ret = -ENODEV;
			goto err_pm_clear;
		}

		common->mdio_dev =  &mdio_pdev->dev;
	}
	of_node_put(node);

	am65_cpsw_nuss_get_ver(common);

	ret = am65_cpsw_nuss_init_host_p(common);
	if (ret)
		goto err_of_clear;

	ret = am65_cpsw_nuss_init_slave_ports(common);
	if (ret)
		goto err_of_clear;

	/* init common data */
	ale_params.dev = dev;
	ale_params.ale_ageout = AM65_CPSW_ALE_AGEOUT_DEFAULT;
	ale_params.ale_ports = common->port_num + 1;
	ale_params.ale_regs = common->cpsw_base + AM65_CPSW_NU_ALE_BASE;
	ale_params.dev_id = common->pdata.ale_dev_id;
	ale_params.bus_freq = common->bus_freq;

	common->ale = cpsw_ale_create(&ale_params);
	if (IS_ERR(common->ale)) {
		dev_err(dev, "error initializing ale engine\n");
		ret = PTR_ERR(common->ale);
		goto err_of_clear;
	}

	ale_entries = common->ale->params.ale_entries;
	common->ale_context = devm_kzalloc(dev,
					   ale_entries * ALE_ENTRY_WORDS * sizeof(u32),
					   GFP_KERNEL);
	ret = am65_cpsw_init_cpts(common);
	if (ret)
		goto err_of_clear;

	/* init ports */
	for (i = 0; i < common->port_num; i++)
		am65_cpsw_nuss_slave_disable_unused(&common->ports[i]);

	dev_set_drvdata(dev, common);

	common->is_emac_mode = true;

	ret = am65_cpsw_nuss_init_ndevs(common);
	if (ret)
		goto err_free_phylink;

	ret = am65_cpsw_nuss_register_ndevs(common);
	if (ret)
		goto err_free_phylink;

	pm_runtime_put(dev);
	return 0;

err_free_phylink:
	am65_cpsw_nuss_phylink_cleanup(common);
err_of_clear:
	of_platform_device_destroy(common->mdio_dev, NULL);
err_pm_clear:
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

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		return ret;

	am65_cpsw_unregister_devlink(common);
	am65_cpsw_unregister_notifiers(common);

	/* must unregister ndevs here because DD release_driver routine calls
	 * dma_deconfigure(dev) before devres_release_all(dev)
	 */
	am65_cpsw_nuss_cleanup_ndev(common);
	am65_cpsw_nuss_phylink_cleanup(common);

	of_platform_device_destroy(common->mdio_dev, NULL);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int am65_cpsw_nuss_suspend(struct device *dev)
{
	struct am65_cpsw_common *common = dev_get_drvdata(dev);
	struct am65_cpsw_host *host_p = am65_common_get_host(common);
	struct am65_cpsw_port *port;
	struct net_device *ndev;
	int i, ret;

	cpsw_ale_dump(common->ale, common->ale_context);
	host_p->vid_context = readl(host_p->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);
	for (i = 0; i < common->port_num; i++) {
		port = &common->ports[i];
		ndev = port->ndev;

		if (!ndev)
			continue;

		port->vid_context = readl(port->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);
		netif_device_detach(ndev);
		if (netif_running(ndev)) {
			rtnl_lock();
			ret = am65_cpsw_nuss_ndo_slave_stop(ndev);
			rtnl_unlock();
			if (ret < 0) {
				netdev_err(ndev, "failed to stop: %d", ret);
				return ret;
			}
		}
	}

	am65_cpts_suspend(common->cpts);

	am65_cpsw_nuss_remove_rx_chns(common);
	am65_cpsw_nuss_remove_tx_chns(common);

	return 0;
}

static int am65_cpsw_nuss_resume(struct device *dev)
{
	struct am65_cpsw_common *common = dev_get_drvdata(dev);
	struct am65_cpsw_port *port;
	struct net_device *ndev;
	int i, ret;
	struct am65_cpsw_host *host_p = am65_common_get_host(common);

	ret = am65_cpsw_nuss_init_tx_chns(common);
	if (ret)
		return ret;
	ret = am65_cpsw_nuss_init_rx_chns(common);
	if (ret)
		return ret;

	/* If RX IRQ was disabled before suspend, keep it disabled */
	if (common->rx_irq_disabled)
		disable_irq(common->rx_chns.irq);

	am65_cpts_resume(common->cpts);

	for (i = 0; i < common->port_num; i++) {
		port = &common->ports[i];
		ndev = port->ndev;

		if (!ndev)
			continue;

		if (netif_running(ndev)) {
			rtnl_lock();
			ret = am65_cpsw_nuss_ndo_slave_open(ndev);
			rtnl_unlock();
			if (ret < 0) {
				netdev_err(ndev, "failed to start: %d", ret);
				return ret;
			}
		}

		netif_device_attach(ndev);
		writel(port->vid_context, port->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);
	}

	writel(host_p->vid_context, host_p->port_base + AM65_CPSW_PORT_VLAN_REG_OFFSET);
	cpsw_ale_restore(common->ale, common->ale_context);

	return 0;
}

static const struct dev_pm_ops am65_cpsw_nuss_dev_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(am65_cpsw_nuss_suspend, am65_cpsw_nuss_resume)
};

static struct platform_driver am65_cpsw_nuss_driver = {
	.driver = {
		.name	 = AM65_CPSW_DRV_NAME,
		.of_match_table = am65_cpsw_nuss_of_mtable,
		.pm = &am65_cpsw_nuss_dev_pm_ops,
	},
	.probe = am65_cpsw_nuss_probe,
	.remove = am65_cpsw_nuss_remove,
};

module_platform_driver(am65_cpsw_nuss_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Grygorii Strashko <grygorii.strashko@ti.com>");
MODULE_DESCRIPTION("TI AM65 CPSW Ethernet driver");
