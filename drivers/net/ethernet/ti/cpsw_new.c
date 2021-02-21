// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments Ethernet Switch Driver
 *
 * Copyright (C) 2019 Texas Instruments
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_device.h>
#include <linux/if_vlan.h>
#include <linux/kmemleak.h>
#include <linux/sys_soc.h>

#include <net/page_pool.h>
#include <net/pkt_cls.h>
#include <net/devlink.h>

#include "cpsw.h"
#include "cpsw_ale.h"
#include "cpsw_priv.h"
#include "cpsw_sl.h"
#include "cpsw_switchdev.h"
#include "cpts.h"
#include "davinci_cpdma.h"

#include <net/pkt_sched.h>

static int debug_level;
static int ale_ageout = CPSW_ALE_AGEOUT_DEFAULT;
static int rx_packet_max = CPSW_MAX_PACKET_SIZE;
static int descs_pool_size = CPSW_CPDMA_DESCS_POOL_SIZE_DEFAULT;

struct cpsw_devlink {
	struct cpsw_common *cpsw;
};

enum cpsw_devlink_param_id {
	CPSW_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	CPSW_DL_PARAM_SWITCH_MODE,
	CPSW_DL_PARAM_ALE_BYPASS,
};

/* struct cpsw_common is not needed, kept here for compatibility
 * reasons witrh the old driver
 */
static int cpsw_slave_index_priv(struct cpsw_common *cpsw,
				 struct cpsw_priv *priv)
{
	if (priv->emac_port == HOST_PORT_NUM)
		return -1;

	return priv->emac_port - 1;
}

static bool cpsw_is_switch_en(struct cpsw_common *cpsw)
{
	return !cpsw->data.dual_emac;
}

static void cpsw_set_promiscious(struct net_device *ndev, bool enable)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);
	bool enable_uni = false;
	int i;

	if (cpsw_is_switch_en(cpsw))
		return;

	/* Enabling promiscuous mode for one interface will be
	 * common for both the interface as the interface shares
	 * the same hardware resource.
	 */
	for (i = 0; i < cpsw->data.slaves; i++)
		if (cpsw->slaves[i].ndev &&
		    (cpsw->slaves[i].ndev->flags & IFF_PROMISC))
			enable_uni = true;

	if (!enable && enable_uni) {
		enable = enable_uni;
		dev_dbg(cpsw->dev, "promiscuity not disabled as the other interface is still in promiscuity mode\n");
	}

	if (enable) {
		/* Enable unknown unicast, reg/unreg mcast */
		cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM,
				     ALE_P0_UNI_FLOOD, 1);

		dev_dbg(cpsw->dev, "promiscuity enabled\n");
	} else {
		/* Disable unknown unicast */
		cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM,
				     ALE_P0_UNI_FLOOD, 0);
		dev_dbg(cpsw->dev, "promiscuity disabled\n");
	}
}

/**
 * cpsw_set_mc - adds multicast entry to the table if it's not added or deletes
 * if it's not deleted
 * @ndev: device to sync
 * @addr: address to be added or deleted
 * @vid: vlan id, if vid < 0 set/unset address for real device
 * @add: add address if the flag is set or remove otherwise
 */
static int cpsw_set_mc(struct net_device *ndev, const u8 *addr,
		       int vid, int add)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int mask, flags, ret, slave_no;

	slave_no = cpsw_slave_index(cpsw, priv);
	if (vid < 0)
		vid = cpsw->slaves[slave_no].port_vlan;

	mask =  ALE_PORT_HOST;
	flags = vid ? ALE_VLAN : 0;

	if (add)
		ret = cpsw_ale_add_mcast(cpsw->ale, addr, mask, flags, vid, 0);
	else
		ret = cpsw_ale_del_mcast(cpsw->ale, addr, 0, flags, vid);

	return ret;
}

static int cpsw_update_vlan_mc(struct net_device *vdev, int vid, void *ctx)
{
	struct addr_sync_ctx *sync_ctx = ctx;
	struct netdev_hw_addr *ha;
	int found = 0, ret = 0;

	if (!vdev || !(vdev->flags & IFF_UP))
		return 0;

	/* vlan address is relevant if its sync_cnt != 0 */
	netdev_for_each_mc_addr(ha, vdev) {
		if (ether_addr_equal(ha->addr, sync_ctx->addr)) {
			found = ha->sync_cnt;
			break;
		}
	}

	if (found)
		sync_ctx->consumed++;

	if (sync_ctx->flush) {
		if (!found)
			cpsw_set_mc(sync_ctx->ndev, sync_ctx->addr, vid, 0);
		return 0;
	}

	if (found)
		ret = cpsw_set_mc(sync_ctx->ndev, sync_ctx->addr, vid, 1);

	return ret;
}

static int cpsw_add_mc_addr(struct net_device *ndev, const u8 *addr, int num)
{
	struct addr_sync_ctx sync_ctx;
	int ret;

	sync_ctx.consumed = 0;
	sync_ctx.addr = addr;
	sync_ctx.ndev = ndev;
	sync_ctx.flush = 0;

	ret = vlan_for_each(ndev, cpsw_update_vlan_mc, &sync_ctx);
	if (sync_ctx.consumed < num && !ret)
		ret = cpsw_set_mc(ndev, addr, -1, 1);

	return ret;
}

static int cpsw_del_mc_addr(struct net_device *ndev, const u8 *addr, int num)
{
	struct addr_sync_ctx sync_ctx;

	sync_ctx.consumed = 0;
	sync_ctx.addr = addr;
	sync_ctx.ndev = ndev;
	sync_ctx.flush = 1;

	vlan_for_each(ndev, cpsw_update_vlan_mc, &sync_ctx);
	if (sync_ctx.consumed == num)
		cpsw_set_mc(ndev, addr, -1, 0);

	return 0;
}

static int cpsw_purge_vlan_mc(struct net_device *vdev, int vid, void *ctx)
{
	struct addr_sync_ctx *sync_ctx = ctx;
	struct netdev_hw_addr *ha;
	int found = 0;

	if (!vdev || !(vdev->flags & IFF_UP))
		return 0;

	/* vlan address is relevant if its sync_cnt != 0 */
	netdev_for_each_mc_addr(ha, vdev) {
		if (ether_addr_equal(ha->addr, sync_ctx->addr)) {
			found = ha->sync_cnt;
			break;
		}
	}

	if (!found)
		return 0;

	sync_ctx->consumed++;
	cpsw_set_mc(sync_ctx->ndev, sync_ctx->addr, vid, 0);
	return 0;
}

static int cpsw_purge_all_mc(struct net_device *ndev, const u8 *addr, int num)
{
	struct addr_sync_ctx sync_ctx;

	sync_ctx.addr = addr;
	sync_ctx.ndev = ndev;
	sync_ctx.consumed = 0;

	vlan_for_each(ndev, cpsw_purge_vlan_mc, &sync_ctx);
	if (sync_ctx.consumed < num)
		cpsw_set_mc(ndev, addr, -1, 0);

	return 0;
}

static void cpsw_ndo_set_rx_mode(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;

	if (ndev->flags & IFF_PROMISC) {
		/* Enable promiscuous mode */
		cpsw_set_promiscious(ndev, true);
		cpsw_ale_set_allmulti(cpsw->ale, IFF_ALLMULTI, priv->emac_port);
		return;
	}

	/* Disable promiscuous mode */
	cpsw_set_promiscious(ndev, false);

	/* Restore allmulti on vlans if necessary */
	cpsw_ale_set_allmulti(cpsw->ale,
			      ndev->flags & IFF_ALLMULTI, priv->emac_port);

	/* add/remove mcast address either for real netdev or for vlan */
	__hw_addr_ref_sync_dev(&ndev->mc, ndev, cpsw_add_mc_addr,
			       cpsw_del_mc_addr);
}

static unsigned int cpsw_rxbuf_total_len(unsigned int len)
{
	len += CPSW_HEADROOM;
	len += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	return SKB_DATA_ALIGN(len);
}

static void cpsw_rx_handler(void *token, int len, int status)
{
	struct page *new_page, *page = token;
	void *pa = page_address(page);
	int headroom = CPSW_HEADROOM;
	struct cpsw_meta_xdp *xmeta;
	struct cpsw_common *cpsw;
	struct net_device *ndev;
	int port, ch, pkt_size;
	struct cpsw_priv *priv;
	struct page_pool *pool;
	struct sk_buff *skb;
	struct xdp_buff xdp;
	int ret = 0;
	dma_addr_t dma;

	xmeta = pa + CPSW_XMETA_OFFSET;
	cpsw = ndev_to_cpsw(xmeta->ndev);
	ndev = xmeta->ndev;
	pkt_size = cpsw->rx_packet_max;
	ch = xmeta->ch;

	if (status >= 0) {
		port = CPDMA_RX_SOURCE_PORT(status);
		if (port)
			ndev = cpsw->slaves[--port].ndev;
	}

	priv = netdev_priv(ndev);
	pool = cpsw->page_pool[ch];

	if (unlikely(status < 0) || unlikely(!netif_running(ndev))) {
		/* In dual emac mode check for all interfaces */
		if (cpsw->usage_count && status >= 0) {
			/* The packet received is for the interface which
			 * is already down and the other interface is up
			 * and running, instead of freeing which results
			 * in reducing of the number of rx descriptor in
			 * DMA engine, requeue page back to cpdma.
			 */
			new_page = page;
			goto requeue;
		}

		/* the interface is going down, pages are purged */
		page_pool_recycle_direct(pool, page);
		return;
	}

	new_page = page_pool_dev_alloc_pages(pool);
	if (unlikely(!new_page)) {
		new_page = page;
		ndev->stats.rx_dropped++;
		goto requeue;
	}

	if (priv->xdp_prog) {
		int headroom = CPSW_HEADROOM, size = len;

		xdp_init_buff(&xdp, PAGE_SIZE, &priv->xdp_rxq[ch]);
		if (status & CPDMA_RX_VLAN_ENCAP) {
			headroom += CPSW_RX_VLAN_ENCAP_HDR_SIZE;
			size -= CPSW_RX_VLAN_ENCAP_HDR_SIZE;
		}

		xdp_prepare_buff(&xdp, pa, headroom, size, false);

		ret = cpsw_run_xdp(priv, ch, &xdp, page, priv->emac_port, &len);
		if (ret != CPSW_XDP_PASS)
			goto requeue;

		headroom = xdp.data - xdp.data_hard_start;

		/* XDP prog can modify vlan tag, so can't use encap header */
		status &= ~CPDMA_RX_VLAN_ENCAP;
	}

	/* pass skb to netstack if no XDP prog or returned XDP_PASS */
	skb = build_skb(pa, cpsw_rxbuf_total_len(pkt_size));
	if (!skb) {
		ndev->stats.rx_dropped++;
		page_pool_recycle_direct(pool, page);
		goto requeue;
	}

	skb->offload_fwd_mark = priv->offload_fwd_mark;
	skb_reserve(skb, headroom);
	skb_put(skb, len);
	skb->dev = ndev;
	if (status & CPDMA_RX_VLAN_ENCAP)
		cpsw_rx_vlan_encap(skb);
	if (priv->rx_ts_enabled)
		cpts_rx_timestamp(cpsw->cpts, skb);
	skb->protocol = eth_type_trans(skb, ndev);

	/* unmap page as no netstack skb page recycling */
	page_pool_release_page(pool, page);
	netif_receive_skb(skb);

	ndev->stats.rx_bytes += len;
	ndev->stats.rx_packets++;

requeue:
	xmeta = page_address(new_page) + CPSW_XMETA_OFFSET;
	xmeta->ndev = ndev;
	xmeta->ch = ch;

	dma = page_pool_get_dma_addr(new_page) + CPSW_HEADROOM;
	ret = cpdma_chan_submit_mapped(cpsw->rxv[ch].ch, new_page, dma,
				       pkt_size, 0);
	if (ret < 0) {
		WARN_ON(ret == -ENOMEM);
		page_pool_recycle_direct(pool, new_page);
	}
}

static int cpsw_add_vlan_ale_entry(struct cpsw_priv *priv,
				   unsigned short vid)
{
	struct cpsw_common *cpsw = priv->cpsw;
	int unreg_mcast_mask = 0;
	int mcast_mask;
	u32 port_mask;
	int ret;

	port_mask = (1 << priv->emac_port) | ALE_PORT_HOST;

	mcast_mask = ALE_PORT_HOST;
	if (priv->ndev->flags & IFF_ALLMULTI)
		unreg_mcast_mask = mcast_mask;

	ret = cpsw_ale_add_vlan(cpsw->ale, vid, port_mask, 0, port_mask,
				unreg_mcast_mask);
	if (ret != 0)
		return ret;

	ret = cpsw_ale_add_ucast(cpsw->ale, priv->mac_addr,
				 HOST_PORT_NUM, ALE_VLAN, vid);
	if (ret != 0)
		goto clean_vid;

	ret = cpsw_ale_add_mcast(cpsw->ale, priv->ndev->broadcast,
				 mcast_mask, ALE_VLAN, vid, 0);
	if (ret != 0)
		goto clean_vlan_ucast;
	return 0;

clean_vlan_ucast:
	cpsw_ale_del_ucast(cpsw->ale, priv->mac_addr,
			   HOST_PORT_NUM, ALE_VLAN, vid);
clean_vid:
	cpsw_ale_del_vlan(cpsw->ale, vid, 0);
	return ret;
}

static int cpsw_ndo_vlan_rx_add_vid(struct net_device *ndev,
				    __be16 proto, u16 vid)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int ret, i;

	if (cpsw_is_switch_en(cpsw)) {
		dev_dbg(cpsw->dev, ".ndo_vlan_rx_add_vid called in switch mode\n");
		return 0;
	}

	if (vid == cpsw->data.default_vlan)
		return 0;

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	/* In dual EMAC, reserved VLAN id should not be used for
	 * creating VLAN interfaces as this can break the dual
	 * EMAC port separation
	 */
	for (i = 0; i < cpsw->data.slaves; i++) {
		if (cpsw->slaves[i].ndev &&
		    vid == cpsw->slaves[i].port_vlan) {
			ret = -EINVAL;
			goto err;
		}
	}

	dev_dbg(priv->dev, "Adding vlanid %d to vlan filter\n", vid);
	ret = cpsw_add_vlan_ale_entry(priv, vid);
err:
	pm_runtime_put(cpsw->dev);
	return ret;
}

static int cpsw_restore_vlans(struct net_device *vdev, int vid, void *arg)
{
	struct cpsw_priv *priv = arg;

	if (!vdev || !vid)
		return 0;

	cpsw_ndo_vlan_rx_add_vid(priv->ndev, 0, vid);
	return 0;
}

/* restore resources after port reset */
static void cpsw_restore(struct cpsw_priv *priv)
{
	struct cpsw_common *cpsw = priv->cpsw;

	/* restore vlan configurations */
	vlan_for_each(priv->ndev, cpsw_restore_vlans, priv);

	/* restore MQPRIO offload */
	cpsw_mqprio_resume(&cpsw->slaves[priv->emac_port - 1], priv);

	/* restore CBS offload */
	cpsw_cbs_resume(&cpsw->slaves[priv->emac_port - 1], priv);
}

static void cpsw_init_stp_ale_entry(struct cpsw_common *cpsw)
{
	char stpa[] = {0x01, 0x80, 0xc2, 0x0, 0x0, 0x0};

	cpsw_ale_add_mcast(cpsw->ale, stpa,
			   ALE_PORT_HOST, ALE_SUPER, 0,
			   ALE_MCAST_BLOCK_LEARN_FWD);
}

static void cpsw_init_host_port_switch(struct cpsw_common *cpsw)
{
	int vlan = cpsw->data.default_vlan;

	writel(CPSW_FIFO_NORMAL_MODE, &cpsw->host_port_regs->tx_in_ctl);

	writel(vlan, &cpsw->host_port_regs->port_vlan);

	cpsw_ale_add_vlan(cpsw->ale, vlan, ALE_ALL_PORTS,
			  ALE_ALL_PORTS, ALE_ALL_PORTS,
			  ALE_PORT_1 | ALE_PORT_2);

	cpsw_init_stp_ale_entry(cpsw);

	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM, ALE_P0_UNI_FLOOD, 1);
	dev_dbg(cpsw->dev, "Set P0_UNI_FLOOD\n");
	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM, ALE_PORT_NOLEARN, 0);
}

static void cpsw_init_host_port_dual_mac(struct cpsw_common *cpsw)
{
	int vlan = cpsw->data.default_vlan;

	writel(CPSW_FIFO_DUAL_MAC_MODE, &cpsw->host_port_regs->tx_in_ctl);

	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM, ALE_P0_UNI_FLOOD, 0);
	dev_dbg(cpsw->dev, "unset P0_UNI_FLOOD\n");

	writel(vlan, &cpsw->host_port_regs->port_vlan);

	cpsw_ale_add_vlan(cpsw->ale, vlan, ALE_ALL_PORTS, ALE_ALL_PORTS, 0, 0);
	/* learning make no sense in dual_mac mode */
	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM, ALE_PORT_NOLEARN, 1);
}

static void cpsw_init_host_port(struct cpsw_priv *priv)
{
	struct cpsw_common *cpsw = priv->cpsw;
	u32 control_reg;

	/* soft reset the controller and initialize ale */
	soft_reset("cpsw", &cpsw->regs->soft_reset);
	cpsw_ale_start(cpsw->ale);

	/* switch to vlan unaware mode */
	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM, ALE_VLAN_AWARE,
			     CPSW_ALE_VLAN_AWARE);
	control_reg = readl(&cpsw->regs->control);
	control_reg |= CPSW_VLAN_AWARE | CPSW_RX_VLAN_ENCAP;
	writel(control_reg, &cpsw->regs->control);

	/* setup host port priority mapping */
	writel_relaxed(CPDMA_TX_PRIORITY_MAP,
		       &cpsw->host_port_regs->cpdma_tx_pri_map);
	writel_relaxed(0, &cpsw->host_port_regs->cpdma_rx_chan_map);

	/* disable priority elevation */
	writel_relaxed(0, &cpsw->regs->ptype);

	/* enable statistics collection only on all ports */
	writel_relaxed(0x7, &cpsw->regs->stat_port_en);

	/* Enable internal fifo flow control */
	writel(0x7, &cpsw->regs->flow_control);

	if (cpsw_is_switch_en(cpsw))
		cpsw_init_host_port_switch(cpsw);
	else
		cpsw_init_host_port_dual_mac(cpsw);

	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM,
			     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);
}

static void cpsw_port_add_dual_emac_def_ale_entries(struct cpsw_priv *priv,
						    struct cpsw_slave *slave)
{
	u32 port_mask = 1 << priv->emac_port | ALE_PORT_HOST;
	struct cpsw_common *cpsw = priv->cpsw;
	u32 reg;

	reg = (cpsw->version == CPSW_VERSION_1) ? CPSW1_PORT_VLAN :
	       CPSW2_PORT_VLAN;
	slave_write(slave, slave->port_vlan, reg);

	cpsw_ale_add_vlan(cpsw->ale, slave->port_vlan, port_mask,
			  port_mask, port_mask, 0);
	cpsw_ale_add_mcast(cpsw->ale, priv->ndev->broadcast,
			   ALE_PORT_HOST, ALE_VLAN, slave->port_vlan,
			   ALE_MCAST_FWD);
	cpsw_ale_add_ucast(cpsw->ale, priv->mac_addr,
			   HOST_PORT_NUM, ALE_VLAN |
			   ALE_SECURE, slave->port_vlan);
	cpsw_ale_control_set(cpsw->ale, priv->emac_port,
			     ALE_PORT_DROP_UNKNOWN_VLAN, 1);
	/* learning make no sense in dual_mac mode */
	cpsw_ale_control_set(cpsw->ale, priv->emac_port,
			     ALE_PORT_NOLEARN, 1);
}

static void cpsw_port_add_switch_def_ale_entries(struct cpsw_priv *priv,
						 struct cpsw_slave *slave)
{
	u32 port_mask = 1 << priv->emac_port | ALE_PORT_HOST;
	struct cpsw_common *cpsw = priv->cpsw;
	u32 reg;

	cpsw_ale_control_set(cpsw->ale, priv->emac_port,
			     ALE_PORT_DROP_UNKNOWN_VLAN, 0);
	cpsw_ale_control_set(cpsw->ale, priv->emac_port,
			     ALE_PORT_NOLEARN, 0);
	/* disabling SA_UPDATE required to make stp work, without this setting
	 * Host MAC addresses will jump between ports.
	 * As per TRM MAC address can be defined as unicast supervisory (super)
	 * by setting both (ALE_BLOCKED | ALE_SECURE) which should prevent
	 * SA_UPDATE, but HW seems works incorrectly and setting ALE_SECURE
	 * causes STP packets to be dropped due to ingress filter
	 *	if (source address found) and (secure) and
	 *	   (receive port number != port_number))
	 *	   then discard the packet
	 */
	cpsw_ale_control_set(cpsw->ale, priv->emac_port,
			     ALE_PORT_NO_SA_UPDATE, 1);

	cpsw_ale_add_mcast(cpsw->ale, priv->ndev->broadcast,
			   port_mask, ALE_VLAN, slave->port_vlan,
			   ALE_MCAST_FWD_2);
	cpsw_ale_add_ucast(cpsw->ale, priv->mac_addr,
			   HOST_PORT_NUM, ALE_VLAN, slave->port_vlan);

	reg = (cpsw->version == CPSW_VERSION_1) ? CPSW1_PORT_VLAN :
	       CPSW2_PORT_VLAN;
	slave_write(slave, slave->port_vlan, reg);
}

static void cpsw_adjust_link(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpsw_slave *slave;
	struct phy_device *phy;
	u32 mac_control = 0;

	slave = &cpsw->slaves[priv->emac_port - 1];
	phy = slave->phy;

	if (!phy)
		return;

	if (phy->link) {
		mac_control = CPSW_SL_CTL_GMII_EN;

		if (phy->speed == 1000)
			mac_control |= CPSW_SL_CTL_GIG;
		if (phy->duplex)
			mac_control |= CPSW_SL_CTL_FULLDUPLEX;

		/* set speed_in input in case RMII mode is used in 100Mbps */
		if (phy->speed == 100)
			mac_control |= CPSW_SL_CTL_IFCTL_A;
		/* in band mode only works in 10Mbps RGMII mode */
		else if ((phy->speed == 10) && phy_interface_is_rgmii(phy))
			mac_control |= CPSW_SL_CTL_EXT_EN; /* In Band mode */

		if (priv->rx_pause)
			mac_control |= CPSW_SL_CTL_RX_FLOW_EN;

		if (priv->tx_pause)
			mac_control |= CPSW_SL_CTL_TX_FLOW_EN;

		if (mac_control != slave->mac_control)
			cpsw_sl_ctl_set(slave->mac_sl, mac_control);

		/* enable forwarding */
		cpsw_ale_control_set(cpsw->ale, priv->emac_port,
				     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

		netif_tx_wake_all_queues(ndev);

		if (priv->shp_cfg_speed &&
		    priv->shp_cfg_speed != slave->phy->speed &&
		    !cpsw_shp_is_off(priv))
			dev_warn(priv->dev, "Speed was changed, CBS shaper speeds are changed!");
	} else {
		netif_tx_stop_all_queues(ndev);

		mac_control = 0;
		/* disable forwarding */
		cpsw_ale_control_set(cpsw->ale, priv->emac_port,
				     ALE_PORT_STATE, ALE_PORT_STATE_DISABLE);

		cpsw_sl_wait_for_idle(slave->mac_sl, 100);

		cpsw_sl_ctl_reset(slave->mac_sl);
	}

	if (mac_control != slave->mac_control)
		phy_print_status(phy);

	slave->mac_control = mac_control;

	if (phy->link && cpsw_need_resplit(cpsw))
		cpsw_split_res(cpsw);
}

static void cpsw_slave_open(struct cpsw_slave *slave, struct cpsw_priv *priv)
{
	struct cpsw_common *cpsw = priv->cpsw;
	struct phy_device *phy;

	cpsw_sl_reset(slave->mac_sl, 100);
	cpsw_sl_ctl_reset(slave->mac_sl);

	/* setup priority mapping */
	cpsw_sl_reg_write(slave->mac_sl, CPSW_SL_RX_PRI_MAP,
			  RX_PRIORITY_MAPPING);

	switch (cpsw->version) {
	case CPSW_VERSION_1:
		slave_write(slave, TX_PRIORITY_MAPPING, CPSW1_TX_PRI_MAP);
		/* Increase RX FIFO size to 5 for supporting fullduplex
		 * flow control mode
		 */
		slave_write(slave,
			    (CPSW_MAX_BLKS_TX << CPSW_MAX_BLKS_TX_SHIFT) |
			    CPSW_MAX_BLKS_RX, CPSW1_MAX_BLKS);
		break;
	case CPSW_VERSION_2:
	case CPSW_VERSION_3:
	case CPSW_VERSION_4:
		slave_write(slave, TX_PRIORITY_MAPPING, CPSW2_TX_PRI_MAP);
		/* Increase RX FIFO size to 5 for supporting fullduplex
		 * flow control mode
		 */
		slave_write(slave,
			    (CPSW_MAX_BLKS_TX << CPSW_MAX_BLKS_TX_SHIFT) |
			    CPSW_MAX_BLKS_RX, CPSW2_MAX_BLKS);
		break;
	}

	/* setup max packet size, and mac address */
	cpsw_sl_reg_write(slave->mac_sl, CPSW_SL_RX_MAXLEN,
			  cpsw->rx_packet_max);
	cpsw_set_slave_mac(slave, priv);

	slave->mac_control = 0;	/* no link yet */

	if (cpsw_is_switch_en(cpsw))
		cpsw_port_add_switch_def_ale_entries(priv, slave);
	else
		cpsw_port_add_dual_emac_def_ale_entries(priv, slave);

	if (!slave->data->phy_node)
		dev_err(priv->dev, "no phy found on slave %d\n",
			slave->slave_num);
	phy = of_phy_connect(priv->ndev, slave->data->phy_node,
			     &cpsw_adjust_link, 0, slave->data->phy_if);
	if (!phy) {
		dev_err(priv->dev, "phy \"%pOF\" not found on slave %d\n",
			slave->data->phy_node,
			slave->slave_num);
		return;
	}
	slave->phy = phy;

	phy_attached_info(slave->phy);

	phy_start(slave->phy);

	/* Configure GMII_SEL register */
	phy_set_mode_ext(slave->data->ifphy, PHY_MODE_ETHERNET,
			 slave->data->phy_if);
}

static int cpsw_ndo_stop(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpsw_slave *slave;

	cpsw_info(priv, ifdown, "shutting down ndev\n");
	slave = &cpsw->slaves[priv->emac_port - 1];
	if (slave->phy)
		phy_stop(slave->phy);

	netif_tx_stop_all_queues(priv->ndev);

	if (slave->phy) {
		phy_disconnect(slave->phy);
		slave->phy = NULL;
	}

	__hw_addr_ref_unsync_dev(&ndev->mc, ndev, cpsw_purge_all_mc);

	if (cpsw->usage_count <= 1) {
		napi_disable(&cpsw->napi_rx);
		napi_disable(&cpsw->napi_tx);
		cpts_unregister(cpsw->cpts);
		cpsw_intr_disable(cpsw);
		cpdma_ctlr_stop(cpsw->dma);
		cpsw_ale_stop(cpsw->ale);
		cpsw_destroy_xdp_rxqs(cpsw);
	}

	if (cpsw_need_resplit(cpsw))
		cpsw_split_res(cpsw);

	cpsw->usage_count--;
	pm_runtime_put_sync(cpsw->dev);
	return 0;
}

static int cpsw_ndo_open(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int ret;

	dev_info(priv->dev, "starting ndev. mode: %s\n",
		 cpsw_is_switch_en(cpsw) ? "switch" : "dual_mac");
	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	/* Notify the stack of the actual queue counts. */
	ret = netif_set_real_num_tx_queues(ndev, cpsw->tx_ch_num);
	if (ret) {
		dev_err(priv->dev, "cannot set real number of tx queues\n");
		goto pm_cleanup;
	}

	ret = netif_set_real_num_rx_queues(ndev, cpsw->rx_ch_num);
	if (ret) {
		dev_err(priv->dev, "cannot set real number of rx queues\n");
		goto pm_cleanup;
	}

	/* Initialize host and slave ports */
	if (!cpsw->usage_count)
		cpsw_init_host_port(priv);
	cpsw_slave_open(&cpsw->slaves[priv->emac_port - 1], priv);

	/* initialize shared resources for every ndev */
	if (!cpsw->usage_count) {
		/* create rxqs for both infs in dual mac as they use same pool
		 * and must be destroyed together when no users.
		 */
		ret = cpsw_create_xdp_rxqs(cpsw);
		if (ret < 0)
			goto err_cleanup;

		ret = cpsw_fill_rx_channels(priv);
		if (ret < 0)
			goto err_cleanup;

		if (cpsw->cpts) {
			if (cpts_register(cpsw->cpts))
				dev_err(priv->dev, "error registering cpts device\n");
			else
				writel(0x10, &cpsw->wr_regs->misc_en);
		}

		napi_enable(&cpsw->napi_rx);
		napi_enable(&cpsw->napi_tx);

		if (cpsw->tx_irq_disabled) {
			cpsw->tx_irq_disabled = false;
			enable_irq(cpsw->irqs_table[1]);
		}

		if (cpsw->rx_irq_disabled) {
			cpsw->rx_irq_disabled = false;
			enable_irq(cpsw->irqs_table[0]);
		}
	}

	cpsw_restore(priv);

	/* Enable Interrupt pacing if configured */
	if (cpsw->coal_intvl != 0) {
		struct ethtool_coalesce coal;

		coal.rx_coalesce_usecs = cpsw->coal_intvl;
		cpsw_set_coalesce(ndev, &coal);
	}

	cpdma_ctlr_start(cpsw->dma);
	cpsw_intr_enable(cpsw);
	cpsw->usage_count++;

	return 0;

err_cleanup:
	cpsw_ndo_stop(ndev);

pm_cleanup:
	pm_runtime_put_sync(cpsw->dev);
	return ret;
}

static netdev_tx_t cpsw_ndo_start_xmit(struct sk_buff *skb,
				       struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpts *cpts = cpsw->cpts;
	struct netdev_queue *txq;
	struct cpdma_chan *txch;
	int ret, q_idx;

	if (skb_padto(skb, CPSW_MIN_PACKET_SIZE)) {
		cpsw_err(priv, tx_err, "packet pad failed\n");
		ndev->stats.tx_dropped++;
		return NET_XMIT_DROP;
	}

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
	    priv->tx_ts_enabled && cpts_can_timestamp(cpts, skb))
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	q_idx = skb_get_queue_mapping(skb);
	if (q_idx >= cpsw->tx_ch_num)
		q_idx = q_idx % cpsw->tx_ch_num;

	txch = cpsw->txv[q_idx].ch;
	txq = netdev_get_tx_queue(ndev, q_idx);
	skb_tx_timestamp(skb);
	ret = cpdma_chan_submit(txch, skb, skb->data, skb->len,
				priv->emac_port);
	if (unlikely(ret != 0)) {
		cpsw_err(priv, tx_err, "desc submit failed\n");
		goto fail;
	}

	/* If there is no more tx desc left free then we need to
	 * tell the kernel to stop sending us tx frames.
	 */
	if (unlikely(!cpdma_check_free_tx_desc(txch))) {
		netif_tx_stop_queue(txq);

		/* Barrier, so that stop_queue visible to other cpus */
		smp_mb__after_atomic();

		if (cpdma_check_free_tx_desc(txch))
			netif_tx_wake_queue(txq);
	}

	return NETDEV_TX_OK;
fail:
	ndev->stats.tx_dropped++;
	netif_tx_stop_queue(txq);

	/* Barrier, so that stop_queue visible to other cpus */
	smp_mb__after_atomic();

	if (cpdma_check_free_tx_desc(txch))
		netif_tx_wake_queue(txq);

	return NETDEV_TX_BUSY;
}

static int cpsw_ndo_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = (struct sockaddr *)p;
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int ret, slave_no;
	int flags = 0;
	u16 vid = 0;

	slave_no = cpsw_slave_index(cpsw, priv);
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	vid = cpsw->slaves[slave_no].port_vlan;
	flags = ALE_VLAN | ALE_SECURE;

	cpsw_ale_del_ucast(cpsw->ale, priv->mac_addr, HOST_PORT_NUM,
			   flags, vid);
	cpsw_ale_add_ucast(cpsw->ale, addr->sa_data, HOST_PORT_NUM,
			   flags, vid);

	ether_addr_copy(priv->mac_addr, addr->sa_data);
	ether_addr_copy(ndev->dev_addr, priv->mac_addr);
	cpsw_set_slave_mac(&cpsw->slaves[slave_no], priv);

	pm_runtime_put(cpsw->dev);

	return 0;
}

static int cpsw_ndo_vlan_rx_kill_vid(struct net_device *ndev,
				     __be16 proto, u16 vid)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int ret;
	int i;

	if (cpsw_is_switch_en(cpsw)) {
		dev_dbg(cpsw->dev, "ndo del vlan is called in switch mode\n");
		return 0;
	}

	if (vid == cpsw->data.default_vlan)
		return 0;

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	/* reset the return code as pm_runtime_get_sync() can return
	 * non zero values as well.
	 */
	ret = 0;
	for (i = 0; i < cpsw->data.slaves; i++) {
		if (cpsw->slaves[i].ndev &&
		    vid == cpsw->slaves[i].port_vlan) {
			ret = -EINVAL;
			goto err;
		}
	}

	dev_dbg(priv->dev, "removing vlanid %d from vlan filter\n", vid);
	ret = cpsw_ale_del_vlan(cpsw->ale, vid, 0);
	if (ret)
		dev_err(priv->dev, "cpsw_ale_del_vlan() failed: ret %d\n", ret);
	ret = cpsw_ale_del_ucast(cpsw->ale, priv->mac_addr,
				 HOST_PORT_NUM, ALE_VLAN, vid);
	if (ret)
		dev_err(priv->dev, "cpsw_ale_del_ucast() failed: ret %d\n",
			ret);
	ret = cpsw_ale_del_mcast(cpsw->ale, priv->ndev->broadcast,
				 0, ALE_VLAN, vid);
	if (ret)
		dev_err(priv->dev, "cpsw_ale_del_mcast failed. ret %d\n",
			ret);
	cpsw_ale_flush_multicast(cpsw->ale, ALE_PORT_HOST, vid);
	ret = 0;
err:
	pm_runtime_put(cpsw->dev);
	return ret;
}

static int cpsw_ndo_get_phys_port_name(struct net_device *ndev, char *name,
				       size_t len)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	int err;

	err = snprintf(name, len, "p%d", priv->emac_port);

	if (err >= len)
		return -EINVAL;

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void cpsw_ndo_poll_controller(struct net_device *ndev)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);

	cpsw_intr_disable(cpsw);
	cpsw_rx_interrupt(cpsw->irqs_table[0], cpsw);
	cpsw_tx_interrupt(cpsw->irqs_table[1], cpsw);
	cpsw_intr_enable(cpsw);
}
#endif

static int cpsw_ndo_xdp_xmit(struct net_device *ndev, int n,
			     struct xdp_frame **frames, u32 flags)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct xdp_frame *xdpf;
	int i, drops = 0;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	for (i = 0; i < n; i++) {
		xdpf = frames[i];
		if (xdpf->len < CPSW_MIN_PACKET_SIZE) {
			xdp_return_frame_rx_napi(xdpf);
			drops++;
			continue;
		}

		if (cpsw_xdp_tx_frame(priv, xdpf, NULL, priv->emac_port))
			drops++;
	}

	return n - drops;
}

static int cpsw_get_port_parent_id(struct net_device *ndev,
				   struct netdev_phys_item_id *ppid)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);

	ppid->id_len = sizeof(cpsw->base_mac);
	memcpy(&ppid->id, &cpsw->base_mac, ppid->id_len);

	return 0;
}

static const struct net_device_ops cpsw_netdev_ops = {
	.ndo_open		= cpsw_ndo_open,
	.ndo_stop		= cpsw_ndo_stop,
	.ndo_start_xmit		= cpsw_ndo_start_xmit,
	.ndo_set_mac_address	= cpsw_ndo_set_mac_address,
	.ndo_do_ioctl		= cpsw_ndo_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= cpsw_ndo_tx_timeout,
	.ndo_set_rx_mode	= cpsw_ndo_set_rx_mode,
	.ndo_set_tx_maxrate	= cpsw_ndo_set_tx_maxrate,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= cpsw_ndo_poll_controller,
#endif
	.ndo_vlan_rx_add_vid	= cpsw_ndo_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= cpsw_ndo_vlan_rx_kill_vid,
	.ndo_setup_tc           = cpsw_ndo_setup_tc,
	.ndo_get_phys_port_name = cpsw_ndo_get_phys_port_name,
	.ndo_bpf		= cpsw_ndo_bpf,
	.ndo_xdp_xmit		= cpsw_ndo_xdp_xmit,
	.ndo_get_port_parent_id	= cpsw_get_port_parent_id,
};

static void cpsw_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);
	struct platform_device *pdev;

	pdev = to_platform_device(cpsw->dev);
	strlcpy(info->driver, "cpsw-switch", sizeof(info->driver));
	strlcpy(info->version, "2.0", sizeof(info->version));
	strlcpy(info->bus_info, pdev->name, sizeof(info->bus_info));
}

static int cpsw_set_pauseparam(struct net_device *ndev,
			       struct ethtool_pauseparam *pause)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);
	struct cpsw_priv *priv = netdev_priv(ndev);
	int slave_no;

	slave_no = cpsw_slave_index(cpsw, priv);
	if (!cpsw->slaves[slave_no].phy)
		return -EINVAL;

	if (!phy_validate_pause(cpsw->slaves[slave_no].phy, pause))
		return -EINVAL;

	priv->rx_pause = pause->rx_pause ? true : false;
	priv->tx_pause = pause->tx_pause ? true : false;

	phy_set_asym_pause(cpsw->slaves[slave_no].phy,
			   priv->rx_pause, priv->tx_pause);

	return 0;
}

static int cpsw_set_channels(struct net_device *ndev,
			     struct ethtool_channels *chs)
{
	return cpsw_set_channels_common(ndev, chs, cpsw_rx_handler);
}

static const struct ethtool_ops cpsw_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS,
	.get_drvinfo		= cpsw_get_drvinfo,
	.get_msglevel		= cpsw_get_msglevel,
	.set_msglevel		= cpsw_set_msglevel,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= cpsw_get_ts_info,
	.get_coalesce		= cpsw_get_coalesce,
	.set_coalesce		= cpsw_set_coalesce,
	.get_sset_count		= cpsw_get_sset_count,
	.get_strings		= cpsw_get_strings,
	.get_ethtool_stats	= cpsw_get_ethtool_stats,
	.get_pauseparam		= cpsw_get_pauseparam,
	.set_pauseparam		= cpsw_set_pauseparam,
	.get_wol		= cpsw_get_wol,
	.set_wol		= cpsw_set_wol,
	.get_regs_len		= cpsw_get_regs_len,
	.get_regs		= cpsw_get_regs,
	.begin			= cpsw_ethtool_op_begin,
	.complete		= cpsw_ethtool_op_complete,
	.get_channels		= cpsw_get_channels,
	.set_channels		= cpsw_set_channels,
	.get_link_ksettings	= cpsw_get_link_ksettings,
	.set_link_ksettings	= cpsw_set_link_ksettings,
	.get_eee		= cpsw_get_eee,
	.set_eee		= cpsw_set_eee,
	.nway_reset		= cpsw_nway_reset,
	.get_ringparam		= cpsw_get_ringparam,
	.set_ringparam		= cpsw_set_ringparam,
};

static int cpsw_probe_dt(struct cpsw_common *cpsw)
{
	struct device_node *node = cpsw->dev->of_node, *tmp_node, *port_np;
	struct cpsw_platform_data *data = &cpsw->data;
	struct device *dev = cpsw->dev;
	int ret;
	u32 prop;

	if (!node)
		return -EINVAL;

	tmp_node = of_get_child_by_name(node, "ethernet-ports");
	if (!tmp_node)
		return -ENOENT;
	data->slaves = of_get_child_count(tmp_node);
	if (data->slaves != CPSW_SLAVE_PORTS_NUM) {
		of_node_put(tmp_node);
		return -ENOENT;
	}

	data->active_slave = 0;
	data->channels = CPSW_MAX_QUEUES;
	data->dual_emac = true;
	data->bd_ram_size = CPSW_BD_RAM_SIZE;
	data->mac_control = 0;

	data->slave_data = devm_kcalloc(dev, CPSW_SLAVE_PORTS_NUM,
					sizeof(struct cpsw_slave_data),
					GFP_KERNEL);
	if (!data->slave_data)
		return -ENOMEM;

	/* Populate all the child nodes here...
	 */
	ret = devm_of_platform_populate(dev);
	/* We do not want to force this, as in some cases may not have child */
	if (ret)
		dev_warn(dev, "Doesn't have any child node\n");

	for_each_child_of_node(tmp_node, port_np) {
		struct cpsw_slave_data *slave_data;
		const void *mac_addr;
		u32 port_id;

		ret = of_property_read_u32(port_np, "reg", &port_id);
		if (ret < 0) {
			dev_err(dev, "%pOF error reading port_id %d\n",
				port_np, ret);
			goto err_node_put;
		}

		if (!port_id || port_id > CPSW_SLAVE_PORTS_NUM) {
			dev_err(dev, "%pOF has invalid port_id %u\n",
				port_np, port_id);
			ret = -EINVAL;
			goto err_node_put;
		}

		slave_data = &data->slave_data[port_id - 1];

		slave_data->disabled = !of_device_is_available(port_np);
		if (slave_data->disabled)
			continue;

		slave_data->slave_node = port_np;
		slave_data->ifphy = devm_of_phy_get(dev, port_np, NULL);
		if (IS_ERR(slave_data->ifphy)) {
			ret = PTR_ERR(slave_data->ifphy);
			dev_err(dev, "%pOF: Error retrieving port phy: %d\n",
				port_np, ret);
			goto err_node_put;
		}

		if (of_phy_is_fixed_link(port_np)) {
			ret = of_phy_register_fixed_link(port_np);
			if (ret) {
				if (ret != -EPROBE_DEFER)
					dev_err(dev, "%pOF failed to register fixed-link phy: %d\n",
						port_np, ret);
				goto err_node_put;
			}
			slave_data->phy_node = of_node_get(port_np);
		} else {
			slave_data->phy_node =
				of_parse_phandle(port_np, "phy-handle", 0);
		}

		if (!slave_data->phy_node) {
			dev_err(dev, "%pOF no phy found\n", port_np);
			ret = -ENODEV;
			goto err_node_put;
		}

		ret = of_get_phy_mode(port_np, &slave_data->phy_if);
		if (ret) {
			dev_err(dev, "%pOF read phy-mode err %d\n",
				port_np, ret);
			goto err_node_put;
		}

		mac_addr = of_get_mac_address(port_np);
		if (!IS_ERR(mac_addr)) {
			ether_addr_copy(slave_data->mac_addr, mac_addr);
		} else {
			ret = ti_cm_get_macid(dev, port_id - 1,
					      slave_data->mac_addr);
			if (ret)
				goto err_node_put;
		}

		if (of_property_read_u32(port_np, "ti,dual-emac-pvid",
					 &prop)) {
			dev_err(dev, "%pOF Missing dual_emac_res_vlan in DT.\n",
				port_np);
			slave_data->dual_emac_res_vlan = port_id;
			dev_err(dev, "%pOF Using %d as Reserved VLAN\n",
				port_np, slave_data->dual_emac_res_vlan);
		} else {
			slave_data->dual_emac_res_vlan = prop;
		}
	}

	of_node_put(tmp_node);
	return 0;

err_node_put:
	of_node_put(port_np);
	return ret;
}

static void cpsw_remove_dt(struct cpsw_common *cpsw)
{
	struct cpsw_platform_data *data = &cpsw->data;
	int i = 0;

	for (i = 0; i < cpsw->data.slaves; i++) {
		struct cpsw_slave_data *slave_data = &data->slave_data[i];
		struct device_node *port_np = slave_data->phy_node;

		if (port_np) {
			if (of_phy_is_fixed_link(port_np))
				of_phy_deregister_fixed_link(port_np);

			of_node_put(port_np);
		}
	}
}

static int cpsw_create_ports(struct cpsw_common *cpsw)
{
	struct cpsw_platform_data *data = &cpsw->data;
	struct net_device *ndev, *napi_ndev = NULL;
	struct device *dev = cpsw->dev;
	struct cpsw_priv *priv;
	int ret = 0, i = 0;

	for (i = 0; i < cpsw->data.slaves; i++) {
		struct cpsw_slave_data *slave_data = &data->slave_data[i];

		if (slave_data->disabled)
			continue;

		ndev = devm_alloc_etherdev_mqs(dev, sizeof(struct cpsw_priv),
					       CPSW_MAX_QUEUES,
					       CPSW_MAX_QUEUES);
		if (!ndev) {
			dev_err(dev, "error allocating net_device\n");
			return -ENOMEM;
		}

		priv = netdev_priv(ndev);
		priv->cpsw = cpsw;
		priv->ndev = ndev;
		priv->dev  = dev;
		priv->msg_enable = netif_msg_init(debug_level, CPSW_DEBUG);
		priv->emac_port = i + 1;

		if (is_valid_ether_addr(slave_data->mac_addr)) {
			ether_addr_copy(priv->mac_addr, slave_data->mac_addr);
			dev_info(cpsw->dev, "Detected MACID = %pM\n",
				 priv->mac_addr);
		} else {
			eth_random_addr(slave_data->mac_addr);
			dev_info(cpsw->dev, "Random MACID = %pM\n",
				 priv->mac_addr);
		}
		ether_addr_copy(ndev->dev_addr, slave_data->mac_addr);
		ether_addr_copy(priv->mac_addr, slave_data->mac_addr);

		cpsw->slaves[i].ndev = ndev;

		ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER |
				  NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_NETNS_LOCAL;

		ndev->netdev_ops = &cpsw_netdev_ops;
		ndev->ethtool_ops = &cpsw_ethtool_ops;
		SET_NETDEV_DEV(ndev, dev);

		if (!napi_ndev) {
			/* CPSW Host port CPDMA interface is shared between
			 * ports and there is only one TX and one RX IRQs
			 * available for all possible TX and RX channels
			 * accordingly.
			 */
			netif_napi_add(ndev, &cpsw->napi_rx,
				       cpsw->quirk_irq ?
				       cpsw_rx_poll : cpsw_rx_mq_poll,
				       CPSW_POLL_WEIGHT);
			netif_tx_napi_add(ndev, &cpsw->napi_tx,
					  cpsw->quirk_irq ?
					  cpsw_tx_poll : cpsw_tx_mq_poll,
					  CPSW_POLL_WEIGHT);
		}

		napi_ndev = ndev;
	}

	return ret;
}

static void cpsw_unregister_ports(struct cpsw_common *cpsw)
{
	int i = 0;

	for (i = 0; i < cpsw->data.slaves; i++) {
		if (!cpsw->slaves[i].ndev)
			continue;

		unregister_netdev(cpsw->slaves[i].ndev);
	}
}

static int cpsw_register_ports(struct cpsw_common *cpsw)
{
	int ret = 0, i = 0;

	for (i = 0; i < cpsw->data.slaves; i++) {
		if (!cpsw->slaves[i].ndev)
			continue;

		/* register the network device */
		ret = register_netdev(cpsw->slaves[i].ndev);
		if (ret) {
			dev_err(cpsw->dev,
				"cpsw: err registering net device%d\n", i);
			cpsw->slaves[i].ndev = NULL;
			break;
		}
	}

	if (ret)
		cpsw_unregister_ports(cpsw);
	return ret;
}

bool cpsw_port_dev_check(const struct net_device *ndev)
{
	if (ndev->netdev_ops == &cpsw_netdev_ops) {
		struct cpsw_common *cpsw = ndev_to_cpsw(ndev);

		return !cpsw->data.dual_emac;
	}

	return false;
}

static void cpsw_port_offload_fwd_mark_update(struct cpsw_common *cpsw)
{
	int set_val = 0;
	int i;

	if (!cpsw->ale_bypass &&
	    (cpsw->br_members == (ALE_PORT_1 | ALE_PORT_2)))
		set_val = 1;

	dev_dbg(cpsw->dev, "set offload_fwd_mark %d\n", set_val);

	for (i = 0; i < cpsw->data.slaves; i++) {
		struct net_device *sl_ndev = cpsw->slaves[i].ndev;
		struct cpsw_priv *priv = netdev_priv(sl_ndev);

		priv->offload_fwd_mark = set_val;
	}
}

static int cpsw_netdevice_port_link(struct net_device *ndev,
				    struct net_device *br_ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;

	if (!cpsw->br_members) {
		cpsw->hw_bridge_dev = br_ndev;
	} else {
		/* This is adding the port to a second bridge, this is
		 * unsupported
		 */
		if (cpsw->hw_bridge_dev != br_ndev)
			return -EOPNOTSUPP;
	}

	cpsw->br_members |= BIT(priv->emac_port);

	cpsw_port_offload_fwd_mark_update(cpsw);

	return NOTIFY_DONE;
}

static void cpsw_netdevice_port_unlink(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;

	cpsw->br_members &= ~BIT(priv->emac_port);

	cpsw_port_offload_fwd_mark_update(cpsw);

	if (!cpsw->br_members)
		cpsw->hw_bridge_dev = NULL;
}

/* netdev notifier */
static int cpsw_netdevice_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info;
	int ret = NOTIFY_DONE;

	if (!cpsw_port_dev_check(ndev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		info = ptr;

		if (netif_is_bridge_master(info->upper_dev)) {
			if (info->linking)
				ret = cpsw_netdevice_port_link(ndev,
							       info->upper_dev);
			else
				cpsw_netdevice_port_unlink(ndev);
		}
		break;
	default:
		return NOTIFY_DONE;
	}

	return notifier_from_errno(ret);
}

static struct notifier_block cpsw_netdevice_nb __read_mostly = {
	.notifier_call = cpsw_netdevice_event,
};

static int cpsw_register_notifiers(struct cpsw_common *cpsw)
{
	int ret = 0;

	ret = register_netdevice_notifier(&cpsw_netdevice_nb);
	if (ret) {
		dev_err(cpsw->dev, "can't register netdevice notifier\n");
		return ret;
	}

	ret = cpsw_switchdev_register_notifiers(cpsw);
	if (ret)
		unregister_netdevice_notifier(&cpsw_netdevice_nb);

	return ret;
}

static void cpsw_unregister_notifiers(struct cpsw_common *cpsw)
{
	cpsw_switchdev_unregister_notifiers(cpsw);
	unregister_netdevice_notifier(&cpsw_netdevice_nb);
}

static const struct devlink_ops cpsw_devlink_ops = {
};

static int cpsw_dl_switch_mode_get(struct devlink *dl, u32 id,
				   struct devlink_param_gset_ctx *ctx)
{
	struct cpsw_devlink *dl_priv = devlink_priv(dl);
	struct cpsw_common *cpsw = dl_priv->cpsw;

	dev_dbg(cpsw->dev, "%s id:%u\n", __func__, id);

	if (id != CPSW_DL_PARAM_SWITCH_MODE)
		return  -EOPNOTSUPP;

	ctx->val.vbool = !cpsw->data.dual_emac;

	return 0;
}

static int cpsw_dl_switch_mode_set(struct devlink *dl, u32 id,
				   struct devlink_param_gset_ctx *ctx)
{
	struct cpsw_devlink *dl_priv = devlink_priv(dl);
	struct cpsw_common *cpsw = dl_priv->cpsw;
	int vlan = cpsw->data.default_vlan;
	bool switch_en = ctx->val.vbool;
	bool if_running = false;
	int i;

	dev_dbg(cpsw->dev, "%s id:%u\n", __func__, id);

	if (id != CPSW_DL_PARAM_SWITCH_MODE)
		return  -EOPNOTSUPP;

	if (switch_en == !cpsw->data.dual_emac)
		return 0;

	if (!switch_en && cpsw->br_members) {
		dev_err(cpsw->dev, "Remove ports from BR before disabling switch mode\n");
		return -EINVAL;
	}

	rtnl_lock();

	for (i = 0; i < cpsw->data.slaves; i++) {
		struct cpsw_slave *slave = &cpsw->slaves[i];
		struct net_device *sl_ndev = slave->ndev;

		if (!sl_ndev || !netif_running(sl_ndev))
			continue;

		if_running = true;
	}

	if (!if_running) {
		/* all ndevs are down */
		cpsw->data.dual_emac = !switch_en;
		for (i = 0; i < cpsw->data.slaves; i++) {
			struct cpsw_slave *slave = &cpsw->slaves[i];
			struct net_device *sl_ndev = slave->ndev;

			if (!sl_ndev)
				continue;

			if (switch_en)
				vlan = cpsw->data.default_vlan;
			else
				vlan = slave->data->dual_emac_res_vlan;
			slave->port_vlan = vlan;
		}
		goto exit;
	}

	if (switch_en) {
		dev_info(cpsw->dev, "Enable switch mode\n");

		/* enable bypass - no forwarding; all traffic goes to Host */
		cpsw_ale_control_set(cpsw->ale, 0, ALE_BYPASS, 1);

		/* clean up ALE table */
		cpsw_ale_control_set(cpsw->ale, 0, ALE_CLEAR, 1);
		cpsw_ale_control_get(cpsw->ale, 0, ALE_AGEOUT);

		cpsw_init_host_port_switch(cpsw);

		for (i = 0; i < cpsw->data.slaves; i++) {
			struct cpsw_slave *slave = &cpsw->slaves[i];
			struct net_device *sl_ndev = slave->ndev;
			struct cpsw_priv *priv;

			if (!sl_ndev)
				continue;

			priv = netdev_priv(sl_ndev);
			slave->port_vlan = vlan;
			if (netif_running(sl_ndev))
				cpsw_port_add_switch_def_ale_entries(priv,
								     slave);
		}

		cpsw_ale_control_set(cpsw->ale, 0, ALE_BYPASS, 0);
		cpsw->data.dual_emac = false;
	} else {
		dev_info(cpsw->dev, "Disable switch mode\n");

		/* enable bypass - no forwarding; all traffic goes to Host */
		cpsw_ale_control_set(cpsw->ale, 0, ALE_BYPASS, 1);

		cpsw_ale_control_set(cpsw->ale, 0, ALE_CLEAR, 1);
		cpsw_ale_control_get(cpsw->ale, 0, ALE_AGEOUT);

		cpsw_init_host_port_dual_mac(cpsw);

		for (i = 0; i < cpsw->data.slaves; i++) {
			struct cpsw_slave *slave = &cpsw->slaves[i];
			struct net_device *sl_ndev = slave->ndev;
			struct cpsw_priv *priv;

			if (!sl_ndev)
				continue;

			priv = netdev_priv(slave->ndev);
			slave->port_vlan = slave->data->dual_emac_res_vlan;
			cpsw_port_add_dual_emac_def_ale_entries(priv, slave);
		}

		cpsw_ale_control_set(cpsw->ale, 0, ALE_BYPASS, 0);
		cpsw->data.dual_emac = true;
	}
exit:
	rtnl_unlock();

	return 0;
}

static int cpsw_dl_ale_ctrl_get(struct devlink *dl, u32 id,
				struct devlink_param_gset_ctx *ctx)
{
	struct cpsw_devlink *dl_priv = devlink_priv(dl);
	struct cpsw_common *cpsw = dl_priv->cpsw;

	dev_dbg(cpsw->dev, "%s id:%u\n", __func__, id);

	switch (id) {
	case CPSW_DL_PARAM_ALE_BYPASS:
		ctx->val.vbool = cpsw_ale_control_get(cpsw->ale, 0, ALE_BYPASS);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int cpsw_dl_ale_ctrl_set(struct devlink *dl, u32 id,
				struct devlink_param_gset_ctx *ctx)
{
	struct cpsw_devlink *dl_priv = devlink_priv(dl);
	struct cpsw_common *cpsw = dl_priv->cpsw;
	int ret = -EOPNOTSUPP;

	dev_dbg(cpsw->dev, "%s id:%u\n", __func__, id);

	switch (id) {
	case CPSW_DL_PARAM_ALE_BYPASS:
		ret = cpsw_ale_control_set(cpsw->ale, 0, ALE_BYPASS,
					   ctx->val.vbool);
		if (!ret) {
			cpsw->ale_bypass = ctx->val.vbool;
			cpsw_port_offload_fwd_mark_update(cpsw);
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct devlink_param cpsw_devlink_params[] = {
	DEVLINK_PARAM_DRIVER(CPSW_DL_PARAM_SWITCH_MODE,
			     "switch_mode", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     cpsw_dl_switch_mode_get, cpsw_dl_switch_mode_set,
			     NULL),
	DEVLINK_PARAM_DRIVER(CPSW_DL_PARAM_ALE_BYPASS,
			     "ale_bypass", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     cpsw_dl_ale_ctrl_get, cpsw_dl_ale_ctrl_set, NULL),
};

static int cpsw_register_devlink(struct cpsw_common *cpsw)
{
	struct device *dev = cpsw->dev;
	struct cpsw_devlink *dl_priv;
	int ret = 0;

	cpsw->devlink = devlink_alloc(&cpsw_devlink_ops, sizeof(*dl_priv));
	if (!cpsw->devlink)
		return -ENOMEM;

	dl_priv = devlink_priv(cpsw->devlink);
	dl_priv->cpsw = cpsw;

	ret = devlink_register(cpsw->devlink, dev);
	if (ret) {
		dev_err(dev, "DL reg fail ret:%d\n", ret);
		goto dl_free;
	}

	ret = devlink_params_register(cpsw->devlink, cpsw_devlink_params,
				      ARRAY_SIZE(cpsw_devlink_params));
	if (ret) {
		dev_err(dev, "DL params reg fail ret:%d\n", ret);
		goto dl_unreg;
	}

	devlink_params_publish(cpsw->devlink);
	return ret;

dl_unreg:
	devlink_unregister(cpsw->devlink);
dl_free:
	devlink_free(cpsw->devlink);
	return ret;
}

static void cpsw_unregister_devlink(struct cpsw_common *cpsw)
{
	devlink_params_unpublish(cpsw->devlink);
	devlink_params_unregister(cpsw->devlink, cpsw_devlink_params,
				  ARRAY_SIZE(cpsw_devlink_params));
	devlink_unregister(cpsw->devlink);
	devlink_free(cpsw->devlink);
}

static const struct of_device_id cpsw_of_mtable[] = {
	{ .compatible = "ti,cpsw-switch"},
	{ .compatible = "ti,am335x-cpsw-switch"},
	{ .compatible = "ti,am4372-cpsw-switch"},
	{ .compatible = "ti,dra7-cpsw-switch"},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cpsw_of_mtable);

static const struct soc_device_attribute cpsw_soc_devices[] = {
	{ .family = "AM33xx", .revision = "ES1.0"},
	{ /* sentinel */ }
};

static int cpsw_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *soc;
	struct device *dev = &pdev->dev;
	struct cpsw_common *cpsw;
	struct resource *ss_res;
	struct gpio_descs *mode;
	void __iomem *ss_regs;
	int ret = 0, ch;
	struct clk *clk;
	int irq;

	cpsw = devm_kzalloc(dev, sizeof(struct cpsw_common), GFP_KERNEL);
	if (!cpsw)
		return -ENOMEM;

	cpsw_slave_index = cpsw_slave_index_priv;

	cpsw->dev = dev;

	cpsw->slaves = devm_kcalloc(dev,
				    CPSW_SLAVE_PORTS_NUM,
				    sizeof(struct cpsw_slave),
				    GFP_KERNEL);
	if (!cpsw->slaves)
		return -ENOMEM;

	mode = devm_gpiod_get_array_optional(dev, "mode", GPIOD_OUT_LOW);
	if (IS_ERR(mode)) {
		ret = PTR_ERR(mode);
		dev_err(dev, "gpio request failed, ret %d\n", ret);
		return ret;
	}

	clk = devm_clk_get(dev, "fck");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(dev, "fck is not found %d\n", ret);
		return ret;
	}
	cpsw->bus_freq_mhz = clk_get_rate(clk) / 1000000;

	ss_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ss_regs = devm_ioremap_resource(dev, ss_res);
	if (IS_ERR(ss_regs)) {
		ret = PTR_ERR(ss_regs);
		return ret;
	}
	cpsw->regs = ss_regs;

	irq = platform_get_irq_byname(pdev, "rx");
	if (irq < 0)
		return irq;
	cpsw->irqs_table[0] = irq;

	irq = platform_get_irq_byname(pdev, "tx");
	if (irq < 0)
		return irq;
	cpsw->irqs_table[1] = irq;

	irq = platform_get_irq_byname(pdev, "misc");
	if (irq <= 0)
		return irq;
	cpsw->misc_irq = irq;

	platform_set_drvdata(pdev, cpsw);
	/* This may be required here for child devices. */
	pm_runtime_enable(dev);

	/* Need to enable clocks with runtime PM api to access module
	 * registers
	 */
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		pm_runtime_disable(dev);
		return ret;
	}

	ret = cpsw_probe_dt(cpsw);
	if (ret)
		goto clean_dt_ret;

	soc = soc_device_match(cpsw_soc_devices);
	if (soc)
		cpsw->quirk_irq = true;

	cpsw->rx_packet_max = rx_packet_max;
	cpsw->descs_pool_size = descs_pool_size;
	eth_random_addr(cpsw->base_mac);

	ret = cpsw_init_common(cpsw, ss_regs, ale_ageout,
			       (u32 __force)ss_res->start + CPSW2_BD_OFFSET,
			       descs_pool_size);
	if (ret)
		goto clean_dt_ret;

	cpsw->wr_regs = cpsw->version == CPSW_VERSION_1 ?
			ss_regs + CPSW1_WR_OFFSET :
			ss_regs + CPSW2_WR_OFFSET;

	ch = cpsw->quirk_irq ? 0 : 7;
	cpsw->txv[0].ch = cpdma_chan_create(cpsw->dma, ch, cpsw_tx_handler, 0);
	if (IS_ERR(cpsw->txv[0].ch)) {
		dev_err(dev, "error initializing tx dma channel\n");
		ret = PTR_ERR(cpsw->txv[0].ch);
		goto clean_cpts;
	}

	cpsw->rxv[0].ch = cpdma_chan_create(cpsw->dma, 0, cpsw_rx_handler, 1);
	if (IS_ERR(cpsw->rxv[0].ch)) {
		dev_err(dev, "error initializing rx dma channel\n");
		ret = PTR_ERR(cpsw->rxv[0].ch);
		goto clean_cpts;
	}
	cpsw_split_res(cpsw);

	/* setup netdevs */
	ret = cpsw_create_ports(cpsw);
	if (ret)
		goto clean_unregister_netdev;

	/* Grab RX and TX IRQs. Note that we also have RX_THRESHOLD and
	 * MISC IRQs which are always kept disabled with this driver so
	 * we will not request them.
	 *
	 * If anyone wants to implement support for those, make sure to
	 * first request and append them to irqs_table array.
	 */

	ret = devm_request_irq(dev, cpsw->irqs_table[0], cpsw_rx_interrupt,
			       0, dev_name(dev), cpsw);
	if (ret < 0) {
		dev_err(dev, "error attaching irq (%d)\n", ret);
		goto clean_unregister_netdev;
	}

	ret = devm_request_irq(dev, cpsw->irqs_table[1], cpsw_tx_interrupt,
			       0, dev_name(dev), cpsw);
	if (ret < 0) {
		dev_err(dev, "error attaching irq (%d)\n", ret);
		goto clean_unregister_netdev;
	}

	if (!cpsw->cpts)
		goto skip_cpts;

	ret = devm_request_irq(dev, cpsw->misc_irq, cpsw_misc_interrupt,
			       0, dev_name(&pdev->dev), cpsw);
	if (ret < 0) {
		dev_err(dev, "error attaching misc irq (%d)\n", ret);
		goto clean_unregister_netdev;
	}

	/* Enable misc CPTS evnt_pend IRQ */
	cpts_set_irqpoll(cpsw->cpts, false);

skip_cpts:
	ret = cpsw_register_notifiers(cpsw);
	if (ret)
		goto clean_unregister_netdev;

	ret = cpsw_register_devlink(cpsw);
	if (ret)
		goto clean_unregister_notifiers;

	ret = cpsw_register_ports(cpsw);
	if (ret)
		goto clean_unregister_notifiers;

	dev_notice(dev, "initialized (regs %pa, pool size %d) hw_ver:%08X %d.%d (%d)\n",
		   &ss_res->start, descs_pool_size,
		   cpsw->version, CPSW_MAJOR_VERSION(cpsw->version),
		   CPSW_MINOR_VERSION(cpsw->version),
		   CPSW_RTL_VERSION(cpsw->version));

	pm_runtime_put(dev);

	return 0;

clean_unregister_notifiers:
	cpsw_unregister_notifiers(cpsw);
clean_unregister_netdev:
	cpsw_unregister_ports(cpsw);
clean_cpts:
	cpts_release(cpsw->cpts);
	cpdma_ctlr_destroy(cpsw->dma);
clean_dt_ret:
	cpsw_remove_dt(cpsw);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	return ret;
}

static int cpsw_remove(struct platform_device *pdev)
{
	struct cpsw_common *cpsw = platform_get_drvdata(pdev);
	int ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&pdev->dev);
		return ret;
	}

	cpsw_unregister_notifiers(cpsw);
	cpsw_unregister_devlink(cpsw);
	cpsw_unregister_ports(cpsw);

	cpts_release(cpsw->cpts);
	cpdma_ctlr_destroy(cpsw->dma);
	cpsw_remove_dt(cpsw);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int __maybe_unused cpsw_suspend(struct device *dev)
{
	struct cpsw_common *cpsw = dev_get_drvdata(dev);
	int i;

	rtnl_lock();

	for (i = 0; i < cpsw->data.slaves; i++) {
		struct net_device *ndev = cpsw->slaves[i].ndev;

		if (!(ndev && netif_running(ndev)))
			continue;

		cpsw_ndo_stop(ndev);
	}

	rtnl_unlock();

	/* Select sleep pin state */
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused cpsw_resume(struct device *dev)
{
	struct cpsw_common *cpsw = dev_get_drvdata(dev);
	int i;

	/* Select default pin state */
	pinctrl_pm_select_default_state(dev);

	/* shut up ASSERT_RTNL() warning in netif_set_real_num_tx/rx_queues */
	rtnl_lock();

	for (i = 0; i < cpsw->data.slaves; i++) {
		struct net_device *ndev = cpsw->slaves[i].ndev;

		if (!(ndev && netif_running(ndev)))
			continue;

		cpsw_ndo_open(ndev);
	}

	rtnl_unlock();

	return 0;
}

static SIMPLE_DEV_PM_OPS(cpsw_pm_ops, cpsw_suspend, cpsw_resume);

static struct platform_driver cpsw_driver = {
	.driver = {
		.name	 = "cpsw-switch",
		.pm	 = &cpsw_pm_ops,
		.of_match_table = cpsw_of_mtable,
	},
	.probe = cpsw_probe,
	.remove = cpsw_remove,
};

module_platform_driver(cpsw_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TI CPSW switchdev Ethernet driver");
