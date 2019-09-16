// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments Ethernet Switch Driver
 *
 * Copyright (C) 2012 Texas Instruments
 *
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
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
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/filter.h>

#include <linux/pinctrl/consumer.h>
#include <net/pkt_cls.h>

#include "cpsw.h"
#include "cpsw_ale.h"
#include "cpsw_priv.h"
#include "cpsw_sl.h"
#include "cpts.h"
#include "davinci_cpdma.h"

#include <net/pkt_sched.h>

static int debug_level;
module_param(debug_level, int, 0);
MODULE_PARM_DESC(debug_level, "cpsw debug level (NETIF_MSG bits)");

static int ale_ageout = 10;
module_param(ale_ageout, int, 0);
MODULE_PARM_DESC(ale_ageout, "cpsw ale ageout interval (seconds)");

static int rx_packet_max = CPSW_MAX_PACKET_SIZE;
module_param(rx_packet_max, int, 0);
MODULE_PARM_DESC(rx_packet_max, "maximum receive packet size (bytes)");

static int descs_pool_size = CPSW_CPDMA_DESCS_POOL_SIZE_DEFAULT;
module_param(descs_pool_size, int, 0444);
MODULE_PARM_DESC(descs_pool_size, "Number of CPDMA CPPI descriptors in pool");

/* The buf includes headroom compatible with both skb and xdpf */
#define CPSW_HEADROOM_NA (max(XDP_PACKET_HEADROOM, NET_SKB_PAD) + NET_IP_ALIGN)
#define CPSW_HEADROOM  ALIGN(CPSW_HEADROOM_NA, sizeof(long))

#define for_each_slave(priv, func, arg...)				\
	do {								\
		struct cpsw_slave *slave;				\
		struct cpsw_common *cpsw = (priv)->cpsw;		\
		int n;							\
		if (cpsw->data.dual_emac)				\
			(func)((cpsw)->slaves + priv->emac_port, ##arg);\
		else							\
			for (n = cpsw->data.slaves,			\
					slave = cpsw->slaves;		\
					n; n--)				\
				(func)(slave++, ##arg);			\
	} while (0)

#define CPSW_XMETA_OFFSET	ALIGN(sizeof(struct xdp_frame), sizeof(long))

#define CPSW_XDP_CONSUMED		1
#define CPSW_XDP_PASS			0

static int cpsw_ndo_vlan_rx_add_vid(struct net_device *ndev,
				    __be16 proto, u16 vid);

static void cpsw_set_promiscious(struct net_device *ndev, bool enable)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);
	struct cpsw_ale *ale = cpsw->ale;
	int i;

	if (cpsw->data.dual_emac) {
		bool flag = false;

		/* Enabling promiscuous mode for one interface will be
		 * common for both the interface as the interface shares
		 * the same hardware resource.
		 */
		for (i = 0; i < cpsw->data.slaves; i++)
			if (cpsw->slaves[i].ndev->flags & IFF_PROMISC)
				flag = true;

		if (!enable && flag) {
			enable = true;
			dev_err(&ndev->dev, "promiscuity not disabled as the other interface is still in promiscuity mode\n");
		}

		if (enable) {
			/* Enable Bypass */
			cpsw_ale_control_set(ale, 0, ALE_BYPASS, 1);

			dev_dbg(&ndev->dev, "promiscuity enabled\n");
		} else {
			/* Disable Bypass */
			cpsw_ale_control_set(ale, 0, ALE_BYPASS, 0);
			dev_dbg(&ndev->dev, "promiscuity disabled\n");
		}
	} else {
		if (enable) {
			unsigned long timeout = jiffies + HZ;

			/* Disable Learn for all ports (host is port 0 and slaves are port 1 and up */
			for (i = 0; i <= cpsw->data.slaves; i++) {
				cpsw_ale_control_set(ale, i,
						     ALE_PORT_NOLEARN, 1);
				cpsw_ale_control_set(ale, i,
						     ALE_PORT_NO_SA_UPDATE, 1);
			}

			/* Clear All Untouched entries */
			cpsw_ale_control_set(ale, 0, ALE_AGEOUT, 1);
			do {
				cpu_relax();
				if (cpsw_ale_control_get(ale, 0, ALE_AGEOUT))
					break;
			} while (time_after(timeout, jiffies));
			cpsw_ale_control_set(ale, 0, ALE_AGEOUT, 1);

			/* Clear all mcast from ALE */
			cpsw_ale_flush_multicast(ale, ALE_ALL_PORTS, -1);
			__hw_addr_ref_unsync_dev(&ndev->mc, ndev, NULL);

			/* Flood All Unicast Packets to Host port */
			cpsw_ale_control_set(ale, 0, ALE_P0_UNI_FLOOD, 1);
			dev_dbg(&ndev->dev, "promiscuity enabled\n");
		} else {
			/* Don't Flood All Unicast Packets to Host port */
			cpsw_ale_control_set(ale, 0, ALE_P0_UNI_FLOOD, 0);

			/* Enable Learn for all ports (host is port 0 and slaves are port 1 and up */
			for (i = 0; i <= cpsw->data.slaves; i++) {
				cpsw_ale_control_set(ale, i,
						     ALE_PORT_NOLEARN, 0);
				cpsw_ale_control_set(ale, i,
						     ALE_PORT_NO_SA_UPDATE, 0);
			}
			dev_dbg(&ndev->dev, "promiscuity disabled\n");
		}
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
	int mask, flags, ret;

	if (vid < 0) {
		if (cpsw->data.dual_emac)
			vid = cpsw->slaves[priv->emac_port].port_vlan;
		else
			vid = 0;
	}

	mask = cpsw->data.dual_emac ? ALE_PORT_HOST : ALE_ALL_PORTS;
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
	int slave_port = -1;

	if (cpsw->data.dual_emac)
		slave_port = priv->emac_port + 1;

	if (ndev->flags & IFF_PROMISC) {
		/* Enable promiscuous mode */
		cpsw_set_promiscious(ndev, true);
		cpsw_ale_set_allmulti(cpsw->ale, IFF_ALLMULTI, slave_port);
		return;
	} else {
		/* Disable promiscuous mode */
		cpsw_set_promiscious(ndev, false);
	}

	/* Restore allmulti on vlans if necessary */
	cpsw_ale_set_allmulti(cpsw->ale,
			      ndev->flags & IFF_ALLMULTI, slave_port);

	/* add/remove mcast address either for real netdev or for vlan */
	__hw_addr_ref_sync_dev(&ndev->mc, ndev, cpsw_add_mc_addr,
			       cpsw_del_mc_addr);
}

void cpsw_intr_enable(struct cpsw_common *cpsw)
{
	writel_relaxed(0xFF, &cpsw->wr_regs->tx_en);
	writel_relaxed(0xFF, &cpsw->wr_regs->rx_en);

	cpdma_ctlr_int_ctrl(cpsw->dma, true);
	return;
}

void cpsw_intr_disable(struct cpsw_common *cpsw)
{
	writel_relaxed(0, &cpsw->wr_regs->tx_en);
	writel_relaxed(0, &cpsw->wr_regs->rx_en);

	cpdma_ctlr_int_ctrl(cpsw->dma, false);
	return;
}

static int cpsw_is_xdpf_handle(void *handle)
{
	return (unsigned long)handle & BIT(0);
}

static void *cpsw_xdpf_to_handle(struct xdp_frame *xdpf)
{
	return (void *)((unsigned long)xdpf | BIT(0));
}

static struct xdp_frame *cpsw_handle_to_xdpf(void *handle)
{
	return (struct xdp_frame *)((unsigned long)handle & ~BIT(0));
}

struct __aligned(sizeof(long)) cpsw_meta_xdp {
	struct net_device *ndev;
	int ch;
};

void cpsw_tx_handler(void *token, int len, int status)
{
	struct cpsw_meta_xdp	*xmeta;
	struct xdp_frame	*xdpf;
	struct net_device	*ndev;
	struct netdev_queue	*txq;
	struct sk_buff		*skb;
	int			ch;

	if (cpsw_is_xdpf_handle(token)) {
		xdpf = cpsw_handle_to_xdpf(token);
		xmeta = (void *)xdpf + CPSW_XMETA_OFFSET;
		ndev = xmeta->ndev;
		ch = xmeta->ch;
		xdp_return_frame(xdpf);
	} else {
		skb = token;
		ndev = skb->dev;
		ch = skb_get_queue_mapping(skb);
		cpts_tx_timestamp(ndev_to_cpsw(ndev)->cpts, skb);
		dev_kfree_skb_any(skb);
	}

	/* Check whether the queue is stopped due to stalled tx dma, if the
	 * queue is stopped then start the queue as we have free desc for tx
	 */
	txq = netdev_get_tx_queue(ndev, ch);
	if (unlikely(netif_tx_queue_stopped(txq)))
		netif_tx_wake_queue(txq);

	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += len;
}

static void cpsw_rx_vlan_encap(struct sk_buff *skb)
{
	struct cpsw_priv *priv = netdev_priv(skb->dev);
	struct cpsw_common *cpsw = priv->cpsw;
	u32 rx_vlan_encap_hdr = *((u32 *)skb->data);
	u16 vtag, vid, prio, pkt_type;

	/* Remove VLAN header encapsulation word */
	skb_pull(skb, CPSW_RX_VLAN_ENCAP_HDR_SIZE);

	pkt_type = (rx_vlan_encap_hdr >>
		    CPSW_RX_VLAN_ENCAP_HDR_PKT_TYPE_SHIFT) &
		    CPSW_RX_VLAN_ENCAP_HDR_PKT_TYPE_MSK;
	/* Ignore unknown & Priority-tagged packets*/
	if (pkt_type == CPSW_RX_VLAN_ENCAP_HDR_PKT_RESERV ||
	    pkt_type == CPSW_RX_VLAN_ENCAP_HDR_PKT_PRIO_TAG)
		return;

	vid = (rx_vlan_encap_hdr >>
	       CPSW_RX_VLAN_ENCAP_HDR_VID_SHIFT) &
	       VLAN_VID_MASK;
	/* Ignore vid 0 and pass packet as is */
	if (!vid)
		return;
	/* Ignore default vlans in dual mac mode */
	if (cpsw->data.dual_emac &&
	    vid == cpsw->slaves[priv->emac_port].port_vlan)
		return;

	prio = (rx_vlan_encap_hdr >>
		CPSW_RX_VLAN_ENCAP_HDR_PRIO_SHIFT) &
		CPSW_RX_VLAN_ENCAP_HDR_PRIO_MSK;

	vtag = (prio << VLAN_PRIO_SHIFT) | vid;
	__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vtag);

	/* strip vlan tag for VLAN-tagged packet */
	if (pkt_type == CPSW_RX_VLAN_ENCAP_HDR_PKT_VLAN_TAG) {
		memmove(skb->data + VLAN_HLEN, skb->data, 2 * ETH_ALEN);
		skb_pull(skb, VLAN_HLEN);
	}
}

static int cpsw_xdp_tx_frame(struct cpsw_priv *priv, struct xdp_frame *xdpf,
			     struct page *page)
{
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpsw_meta_xdp *xmeta;
	struct cpdma_chan *txch;
	dma_addr_t dma;
	int ret, port;

	xmeta = (void *)xdpf + CPSW_XMETA_OFFSET;
	xmeta->ndev = priv->ndev;
	xmeta->ch = 0;
	txch = cpsw->txv[0].ch;

	port = priv->emac_port + cpsw->data.dual_emac;
	if (page) {
		dma = page_pool_get_dma_addr(page);
		dma += xdpf->headroom + sizeof(struct xdp_frame);
		ret = cpdma_chan_submit_mapped(txch, cpsw_xdpf_to_handle(xdpf),
					       dma, xdpf->len, port);
	} else {
		if (sizeof(*xmeta) > xdpf->headroom) {
			xdp_return_frame_rx_napi(xdpf);
			return -EINVAL;
		}

		ret = cpdma_chan_submit(txch, cpsw_xdpf_to_handle(xdpf),
					xdpf->data, xdpf->len, port);
	}

	if (ret) {
		priv->ndev->stats.tx_dropped++;
		xdp_return_frame_rx_napi(xdpf);
	}

	return ret;
}

static int cpsw_run_xdp(struct cpsw_priv *priv, int ch, struct xdp_buff *xdp,
			struct page *page)
{
	struct cpsw_common *cpsw = priv->cpsw;
	struct net_device *ndev = priv->ndev;
	int ret = CPSW_XDP_CONSUMED;
	struct xdp_frame *xdpf;
	struct bpf_prog *prog;
	u32 act;

	rcu_read_lock();

	prog = READ_ONCE(priv->xdp_prog);
	if (!prog) {
		ret = CPSW_XDP_PASS;
		goto out;
	}

	act = bpf_prog_run_xdp(prog, xdp);
	switch (act) {
	case XDP_PASS:
		ret = CPSW_XDP_PASS;
		break;
	case XDP_TX:
		xdpf = convert_to_xdp_frame(xdp);
		if (unlikely(!xdpf))
			goto drop;

		cpsw_xdp_tx_frame(priv, xdpf, page);
		break;
	case XDP_REDIRECT:
		if (xdp_do_redirect(ndev, xdp, prog))
			goto drop;

		/*  Have to flush here, per packet, instead of doing it in bulk
		 *  at the end of the napi handler. The RX devices on this
		 *  particular hardware is sharing a common queue, so the
		 *  incoming device might change per packet.
		 */
		xdp_do_flush_map();
		break;
	default:
		bpf_warn_invalid_xdp_action(act);
		/* fall through */
	case XDP_ABORTED:
		trace_xdp_exception(ndev, prog, act);
		/* fall through -- handle aborts by dropping packet */
	case XDP_DROP:
		goto drop;
	}
out:
	rcu_read_unlock();
	return ret;
drop:
	rcu_read_unlock();
	page_pool_recycle_direct(cpsw->page_pool[ch], page);
	return ret;
}

static unsigned int cpsw_rxbuf_total_len(unsigned int len)
{
	len += CPSW_HEADROOM;
	len += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	return SKB_DATA_ALIGN(len);
}

static struct page_pool *cpsw_create_page_pool(struct cpsw_common *cpsw,
					       int size)
{
	struct page_pool_params pp_params;
	struct page_pool *pool;

	pp_params.order = 0;
	pp_params.flags = PP_FLAG_DMA_MAP;
	pp_params.pool_size = size;
	pp_params.nid = NUMA_NO_NODE;
	pp_params.dma_dir = DMA_BIDIRECTIONAL;
	pp_params.dev = cpsw->dev;

	pool = page_pool_create(&pp_params);
	if (IS_ERR(pool))
		dev_err(cpsw->dev, "cannot create rx page pool\n");

	return pool;
}

static int cpsw_ndev_create_xdp_rxq(struct cpsw_priv *priv, int ch)
{
	struct cpsw_common *cpsw = priv->cpsw;
	struct xdp_rxq_info *rxq;
	struct page_pool *pool;
	int ret;

	pool = cpsw->page_pool[ch];
	rxq = &priv->xdp_rxq[ch];

	ret = xdp_rxq_info_reg(rxq, priv->ndev, ch);
	if (ret)
		return ret;

	ret = xdp_rxq_info_reg_mem_model(rxq, MEM_TYPE_PAGE_POOL, pool);
	if (ret)
		xdp_rxq_info_unreg(rxq);

	return ret;
}

static void cpsw_ndev_destroy_xdp_rxq(struct cpsw_priv *priv, int ch)
{
	struct xdp_rxq_info *rxq = &priv->xdp_rxq[ch];

	if (!xdp_rxq_info_is_reg(rxq))
		return;

	xdp_rxq_info_unreg(rxq);
}

static int cpsw_create_rx_pool(struct cpsw_common *cpsw, int ch)
{
	struct page_pool *pool;
	int ret = 0, pool_size;

	pool_size = cpdma_chan_get_rx_buf_num(cpsw->rxv[ch].ch);
	pool = cpsw_create_page_pool(cpsw, pool_size);
	if (IS_ERR(pool))
		ret = PTR_ERR(pool);
	else
		cpsw->page_pool[ch] = pool;

	return ret;
}

void cpsw_destroy_xdp_rxqs(struct cpsw_common *cpsw)
{
	struct net_device *ndev;
	int i, ch;

	for (ch = 0; ch < cpsw->rx_ch_num; ch++) {
		for (i = 0; i < cpsw->data.slaves; i++) {
			ndev = cpsw->slaves[i].ndev;
			if (!ndev)
				continue;

			cpsw_ndev_destroy_xdp_rxq(netdev_priv(ndev), ch);
		}

		page_pool_destroy(cpsw->page_pool[ch]);
		cpsw->page_pool[ch] = NULL;
	}
}

int cpsw_create_xdp_rxqs(struct cpsw_common *cpsw)
{
	struct net_device *ndev;
	int i, ch, ret;

	for (ch = 0; ch < cpsw->rx_ch_num; ch++) {
		ret = cpsw_create_rx_pool(cpsw, ch);
		if (ret)
			goto err_cleanup;

		/* using same page pool is allowed as no running rx handlers
		 * simultaneously for both ndevs
		 */
		for (i = 0; i < cpsw->data.slaves; i++) {
			ndev = cpsw->slaves[i].ndev;
			if (!ndev)
				continue;

			ret = cpsw_ndev_create_xdp_rxq(netdev_priv(ndev), ch);
			if (ret)
				goto err_cleanup;
		}
	}

	return 0;

err_cleanup:
	cpsw_destroy_xdp_rxqs(cpsw);

	return ret;
}

static void cpsw_rx_handler(void *token, int len, int status)
{
	struct page		*new_page, *page = token;
	void			*pa = page_address(page);
	struct cpsw_meta_xdp	*xmeta = pa + CPSW_XMETA_OFFSET;
	struct cpsw_common	*cpsw = ndev_to_cpsw(xmeta->ndev);
	int			pkt_size = cpsw->rx_packet_max;
	int			ret = 0, port, ch = xmeta->ch;
	int			headroom = CPSW_HEADROOM;
	struct net_device	*ndev = xmeta->ndev;
	struct cpsw_priv	*priv;
	struct page_pool	*pool;
	struct sk_buff		*skb;
	struct xdp_buff		xdp;
	dma_addr_t		dma;

	if (cpsw->data.dual_emac && status >= 0) {
		port = CPDMA_RX_SOURCE_PORT(status);
		if (port)
			ndev = cpsw->slaves[--port].ndev;
	}

	priv = netdev_priv(ndev);
	pool = cpsw->page_pool[ch];
	if (unlikely(status < 0) || unlikely(!netif_running(ndev))) {
		/* In dual emac mode check for all interfaces */
		if (cpsw->data.dual_emac && cpsw->usage_count &&
		    (status >= 0)) {
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
		if (status & CPDMA_RX_VLAN_ENCAP) {
			xdp.data = pa + CPSW_HEADROOM +
				   CPSW_RX_VLAN_ENCAP_HDR_SIZE;
			xdp.data_end = xdp.data + len -
				       CPSW_RX_VLAN_ENCAP_HDR_SIZE;
		} else {
			xdp.data = pa + CPSW_HEADROOM;
			xdp.data_end = xdp.data + len;
		}

		xdp_set_data_meta_invalid(&xdp);

		xdp.data_hard_start = pa;
		xdp.rxq = &priv->xdp_rxq[ch];

		ret = cpsw_run_xdp(priv, ch, &xdp, page);
		if (ret != CPSW_XDP_PASS)
			goto requeue;

		/* XDP prog might have changed packet data and boundaries */
		len = xdp.data_end - xdp.data;
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

void cpsw_split_res(struct cpsw_common *cpsw)
{
	u32 consumed_rate = 0, bigest_rate = 0;
	struct cpsw_vector *txv = cpsw->txv;
	int i, ch_weight, rlim_ch_num = 0;
	int budget, bigest_rate_ch = 0;
	u32 ch_rate, max_rate;
	int ch_budget = 0;

	for (i = 0; i < cpsw->tx_ch_num; i++) {
		ch_rate = cpdma_chan_get_rate(txv[i].ch);
		if (!ch_rate)
			continue;

		rlim_ch_num++;
		consumed_rate += ch_rate;
	}

	if (cpsw->tx_ch_num == rlim_ch_num) {
		max_rate = consumed_rate;
	} else if (!rlim_ch_num) {
		ch_budget = CPSW_POLL_WEIGHT / cpsw->tx_ch_num;
		bigest_rate = 0;
		max_rate = consumed_rate;
	} else {
		max_rate = cpsw->speed * 1000;

		/* if max_rate is less then expected due to reduced link speed,
		 * split proportionally according next potential max speed
		 */
		if (max_rate < consumed_rate)
			max_rate *= 10;

		if (max_rate < consumed_rate)
			max_rate *= 10;

		ch_budget = (consumed_rate * CPSW_POLL_WEIGHT) / max_rate;
		ch_budget = (CPSW_POLL_WEIGHT - ch_budget) /
			    (cpsw->tx_ch_num - rlim_ch_num);
		bigest_rate = (max_rate - consumed_rate) /
			      (cpsw->tx_ch_num - rlim_ch_num);
	}

	/* split tx weight/budget */
	budget = CPSW_POLL_WEIGHT;
	for (i = 0; i < cpsw->tx_ch_num; i++) {
		ch_rate = cpdma_chan_get_rate(txv[i].ch);
		if (ch_rate) {
			txv[i].budget = (ch_rate * CPSW_POLL_WEIGHT) / max_rate;
			if (!txv[i].budget)
				txv[i].budget++;
			if (ch_rate > bigest_rate) {
				bigest_rate_ch = i;
				bigest_rate = ch_rate;
			}

			ch_weight = (ch_rate * 100) / max_rate;
			if (!ch_weight)
				ch_weight++;
			cpdma_chan_set_weight(cpsw->txv[i].ch, ch_weight);
		} else {
			txv[i].budget = ch_budget;
			if (!bigest_rate_ch)
				bigest_rate_ch = i;
			cpdma_chan_set_weight(cpsw->txv[i].ch, 0);
		}

		budget -= txv[i].budget;
	}

	if (budget)
		txv[bigest_rate_ch].budget += budget;

	/* split rx budget */
	budget = CPSW_POLL_WEIGHT;
	ch_budget = budget / cpsw->rx_ch_num;
	for (i = 0; i < cpsw->rx_ch_num; i++) {
		cpsw->rxv[i].budget = ch_budget;
		budget -= ch_budget;
	}

	if (budget)
		cpsw->rxv[0].budget += budget;
}

static irqreturn_t cpsw_tx_interrupt(int irq, void *dev_id)
{
	struct cpsw_common *cpsw = dev_id;

	writel(0, &cpsw->wr_regs->tx_en);
	cpdma_ctlr_eoi(cpsw->dma, CPDMA_EOI_TX);

	if (cpsw->quirk_irq) {
		disable_irq_nosync(cpsw->irqs_table[1]);
		cpsw->tx_irq_disabled = true;
	}

	napi_schedule(&cpsw->napi_tx);
	return IRQ_HANDLED;
}

static irqreturn_t cpsw_rx_interrupt(int irq, void *dev_id)
{
	struct cpsw_common *cpsw = dev_id;

	cpdma_ctlr_eoi(cpsw->dma, CPDMA_EOI_RX);
	writel(0, &cpsw->wr_regs->rx_en);

	if (cpsw->quirk_irq) {
		disable_irq_nosync(cpsw->irqs_table[0]);
		cpsw->rx_irq_disabled = true;
	}

	napi_schedule(&cpsw->napi_rx);
	return IRQ_HANDLED;
}

static int cpsw_tx_mq_poll(struct napi_struct *napi_tx, int budget)
{
	u32			ch_map;
	int			num_tx, cur_budget, ch;
	struct cpsw_common	*cpsw = napi_to_cpsw(napi_tx);
	struct cpsw_vector	*txv;

	/* process every unprocessed channel */
	ch_map = cpdma_ctrl_txchs_state(cpsw->dma);
	for (ch = 0, num_tx = 0; ch_map & 0xff; ch_map <<= 1, ch++) {
		if (!(ch_map & 0x80))
			continue;

		txv = &cpsw->txv[ch];
		if (unlikely(txv->budget > budget - num_tx))
			cur_budget = budget - num_tx;
		else
			cur_budget = txv->budget;

		num_tx += cpdma_chan_process(txv->ch, cur_budget);
		if (num_tx >= budget)
			break;
	}

	if (num_tx < budget) {
		napi_complete(napi_tx);
		writel(0xff, &cpsw->wr_regs->tx_en);
	}

	return num_tx;
}

static int cpsw_tx_poll(struct napi_struct *napi_tx, int budget)
{
	struct cpsw_common *cpsw = napi_to_cpsw(napi_tx);
	int num_tx;

	num_tx = cpdma_chan_process(cpsw->txv[0].ch, budget);
	if (num_tx < budget) {
		napi_complete(napi_tx);
		writel(0xff, &cpsw->wr_regs->tx_en);
		if (cpsw->tx_irq_disabled) {
			cpsw->tx_irq_disabled = false;
			enable_irq(cpsw->irqs_table[1]);
		}
	}

	return num_tx;
}

static int cpsw_rx_mq_poll(struct napi_struct *napi_rx, int budget)
{
	u32			ch_map;
	int			num_rx, cur_budget, ch;
	struct cpsw_common	*cpsw = napi_to_cpsw(napi_rx);
	struct cpsw_vector	*rxv;

	/* process every unprocessed channel */
	ch_map = cpdma_ctrl_rxchs_state(cpsw->dma);
	for (ch = 0, num_rx = 0; ch_map; ch_map >>= 1, ch++) {
		if (!(ch_map & 0x01))
			continue;

		rxv = &cpsw->rxv[ch];
		if (unlikely(rxv->budget > budget - num_rx))
			cur_budget = budget - num_rx;
		else
			cur_budget = rxv->budget;

		num_rx += cpdma_chan_process(rxv->ch, cur_budget);
		if (num_rx >= budget)
			break;
	}

	if (num_rx < budget) {
		napi_complete_done(napi_rx, num_rx);
		writel(0xff, &cpsw->wr_regs->rx_en);
	}

	return num_rx;
}

static int cpsw_rx_poll(struct napi_struct *napi_rx, int budget)
{
	struct cpsw_common *cpsw = napi_to_cpsw(napi_rx);
	int num_rx;

	num_rx = cpdma_chan_process(cpsw->rxv[0].ch, budget);
	if (num_rx < budget) {
		napi_complete_done(napi_rx, num_rx);
		writel(0xff, &cpsw->wr_regs->rx_en);
		if (cpsw->rx_irq_disabled) {
			cpsw->rx_irq_disabled = false;
			enable_irq(cpsw->irqs_table[0]);
		}
	}

	return num_rx;
}

static inline void soft_reset(const char *module, void __iomem *reg)
{
	unsigned long timeout = jiffies + HZ;

	writel_relaxed(1, reg);
	do {
		cpu_relax();
	} while ((readl_relaxed(reg) & 1) && time_after(timeout, jiffies));

	WARN(readl_relaxed(reg) & 1, "failed to soft-reset %s\n", module);
}

static void cpsw_set_slave_mac(struct cpsw_slave *slave,
			       struct cpsw_priv *priv)
{
	slave_write(slave, mac_hi(priv->mac_addr), SA_HI);
	slave_write(slave, mac_lo(priv->mac_addr), SA_LO);
}

static bool cpsw_shp_is_off(struct cpsw_priv *priv)
{
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpsw_slave *slave;
	u32 shift, mask, val;

	val = readl_relaxed(&cpsw->regs->ptype);

	slave = &cpsw->slaves[cpsw_slave_index(cpsw, priv)];
	shift = CPSW_FIFO_SHAPE_EN_SHIFT + 3 * slave->slave_num;
	mask = 7 << shift;
	val = val & mask;

	return !val;
}

static void cpsw_fifo_shp_on(struct cpsw_priv *priv, int fifo, int on)
{
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpsw_slave *slave;
	u32 shift, mask, val;

	val = readl_relaxed(&cpsw->regs->ptype);

	slave = &cpsw->slaves[cpsw_slave_index(cpsw, priv)];
	shift = CPSW_FIFO_SHAPE_EN_SHIFT + 3 * slave->slave_num;
	mask = (1 << --fifo) << shift;
	val = on ? val | mask : val & ~mask;

	writel_relaxed(val, &cpsw->regs->ptype);
}

static void _cpsw_adjust_link(struct cpsw_slave *slave,
			      struct cpsw_priv *priv, bool *link)
{
	struct phy_device	*phy = slave->phy;
	u32			mac_control = 0;
	u32			slave_port;
	struct cpsw_common *cpsw = priv->cpsw;

	if (!phy)
		return;

	slave_port = cpsw_get_slave_port(slave->slave_num);

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
		cpsw_ale_control_set(cpsw->ale, slave_port,
				     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

		*link = true;

		if (priv->shp_cfg_speed &&
		    priv->shp_cfg_speed != slave->phy->speed &&
		    !cpsw_shp_is_off(priv))
			dev_warn(priv->dev,
				 "Speed was changed, CBS shaper speeds are changed!");
	} else {
		mac_control = 0;
		/* disable forwarding */
		cpsw_ale_control_set(cpsw->ale, slave_port,
				     ALE_PORT_STATE, ALE_PORT_STATE_DISABLE);

		cpsw_sl_wait_for_idle(slave->mac_sl, 100);

		cpsw_sl_ctl_reset(slave->mac_sl);
	}

	if (mac_control != slave->mac_control)
		phy_print_status(phy);

	slave->mac_control = mac_control;
}

static int cpsw_get_common_speed(struct cpsw_common *cpsw)
{
	int i, speed;

	for (i = 0, speed = 0; i < cpsw->data.slaves; i++)
		if (cpsw->slaves[i].phy && cpsw->slaves[i].phy->link)
			speed += cpsw->slaves[i].phy->speed;

	return speed;
}

static int cpsw_need_resplit(struct cpsw_common *cpsw)
{
	int i, rlim_ch_num;
	int speed, ch_rate;

	/* re-split resources only in case speed was changed */
	speed = cpsw_get_common_speed(cpsw);
	if (speed == cpsw->speed || !speed)
		return 0;

	cpsw->speed = speed;

	for (i = 0, rlim_ch_num = 0; i < cpsw->tx_ch_num; i++) {
		ch_rate = cpdma_chan_get_rate(cpsw->txv[i].ch);
		if (!ch_rate)
			break;

		rlim_ch_num++;
	}

	/* cases not dependent on speed */
	if (!rlim_ch_num || rlim_ch_num == cpsw->tx_ch_num)
		return 0;

	return 1;
}

static void cpsw_adjust_link(struct net_device *ndev)
{
	struct cpsw_priv	*priv = netdev_priv(ndev);
	struct cpsw_common	*cpsw = priv->cpsw;
	bool			link = false;

	for_each_slave(priv, _cpsw_adjust_link, priv, &link);

	if (link) {
		if (cpsw_need_resplit(cpsw))
			cpsw_split_res(cpsw);

		netif_carrier_on(ndev);
		if (netif_running(ndev))
			netif_tx_wake_all_queues(ndev);
	} else {
		netif_carrier_off(ndev);
		netif_tx_stop_all_queues(ndev);
	}
}

static inline void cpsw_add_dual_emac_def_ale_entries(
		struct cpsw_priv *priv, struct cpsw_slave *slave,
		u32 slave_port)
{
	struct cpsw_common *cpsw = priv->cpsw;
	u32 port_mask = 1 << slave_port | ALE_PORT_HOST;

	if (cpsw->version == CPSW_VERSION_1)
		slave_write(slave, slave->port_vlan, CPSW1_PORT_VLAN);
	else
		slave_write(slave, slave->port_vlan, CPSW2_PORT_VLAN);
	cpsw_ale_add_vlan(cpsw->ale, slave->port_vlan, port_mask,
			  port_mask, port_mask, 0);
	cpsw_ale_add_mcast(cpsw->ale, priv->ndev->broadcast,
			   ALE_PORT_HOST, ALE_VLAN, slave->port_vlan, 0);
	cpsw_ale_add_ucast(cpsw->ale, priv->mac_addr,
			   HOST_PORT_NUM, ALE_VLAN |
			   ALE_SECURE, slave->port_vlan);
	cpsw_ale_control_set(cpsw->ale, slave_port,
			     ALE_PORT_DROP_UNKNOWN_VLAN, 1);
}

static void cpsw_slave_open(struct cpsw_slave *slave, struct cpsw_priv *priv)
{
	u32 slave_port;
	struct phy_device *phy;
	struct cpsw_common *cpsw = priv->cpsw;

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

	slave_port = cpsw_get_slave_port(slave->slave_num);

	if (cpsw->data.dual_emac)
		cpsw_add_dual_emac_def_ale_entries(priv, slave, slave_port);
	else
		cpsw_ale_add_mcast(cpsw->ale, priv->ndev->broadcast,
				   1 << slave_port, 0, 0, ALE_MCAST_FWD_2);

	if (slave->data->phy_node) {
		phy = of_phy_connect(priv->ndev, slave->data->phy_node,
				 &cpsw_adjust_link, 0, slave->data->phy_if);
		if (!phy) {
			dev_err(priv->dev, "phy \"%pOF\" not found on slave %d\n",
				slave->data->phy_node,
				slave->slave_num);
			return;
		}
	} else {
		phy = phy_connect(priv->ndev, slave->data->phy_id,
				 &cpsw_adjust_link, slave->data->phy_if);
		if (IS_ERR(phy)) {
			dev_err(priv->dev,
				"phy \"%s\" not found on slave %d, err %ld\n",
				slave->data->phy_id, slave->slave_num,
				PTR_ERR(phy));
			return;
		}
	}

	slave->phy = phy;

	phy_attached_info(slave->phy);

	phy_start(slave->phy);

	/* Configure GMII_SEL register */
	if (!IS_ERR(slave->data->ifphy))
		phy_set_mode_ext(slave->data->ifphy, PHY_MODE_ETHERNET,
				 slave->data->phy_if);
	else
		cpsw_phy_sel(cpsw->dev, slave->phy->interface,
			     slave->slave_num);
}

static inline void cpsw_add_default_vlan(struct cpsw_priv *priv)
{
	struct cpsw_common *cpsw = priv->cpsw;
	const int vlan = cpsw->data.default_vlan;
	u32 reg;
	int i;
	int unreg_mcast_mask;

	reg = (cpsw->version == CPSW_VERSION_1) ? CPSW1_PORT_VLAN :
	       CPSW2_PORT_VLAN;

	writel(vlan, &cpsw->host_port_regs->port_vlan);

	for (i = 0; i < cpsw->data.slaves; i++)
		slave_write(cpsw->slaves + i, vlan, reg);

	if (priv->ndev->flags & IFF_ALLMULTI)
		unreg_mcast_mask = ALE_ALL_PORTS;
	else
		unreg_mcast_mask = ALE_PORT_1 | ALE_PORT_2;

	cpsw_ale_add_vlan(cpsw->ale, vlan, ALE_ALL_PORTS,
			  ALE_ALL_PORTS, ALE_ALL_PORTS,
			  unreg_mcast_mask);
}

static void cpsw_init_host_port(struct cpsw_priv *priv)
{
	u32 fifo_mode;
	u32 control_reg;
	struct cpsw_common *cpsw = priv->cpsw;

	/* soft reset the controller and initialize ale */
	soft_reset("cpsw", &cpsw->regs->soft_reset);
	cpsw_ale_start(cpsw->ale);

	/* switch to vlan unaware mode */
	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM, ALE_VLAN_AWARE,
			     CPSW_ALE_VLAN_AWARE);
	control_reg = readl(&cpsw->regs->control);
	control_reg |= CPSW_VLAN_AWARE | CPSW_RX_VLAN_ENCAP;
	writel(control_reg, &cpsw->regs->control);
	fifo_mode = (cpsw->data.dual_emac) ? CPSW_FIFO_DUAL_MAC_MODE :
		     CPSW_FIFO_NORMAL_MODE;
	writel(fifo_mode, &cpsw->host_port_regs->tx_in_ctl);

	/* setup host port priority mapping */
	writel_relaxed(CPDMA_TX_PRIORITY_MAP,
		       &cpsw->host_port_regs->cpdma_tx_pri_map);
	writel_relaxed(0, &cpsw->host_port_regs->cpdma_rx_chan_map);

	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM,
			     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

	if (!cpsw->data.dual_emac) {
		cpsw_ale_add_ucast(cpsw->ale, priv->mac_addr, HOST_PORT_NUM,
				   0, 0);
		cpsw_ale_add_mcast(cpsw->ale, priv->ndev->broadcast,
				   ALE_PORT_HOST, 0, 0, ALE_MCAST_FWD_2);
	}
}

int cpsw_fill_rx_channels(struct cpsw_priv *priv)
{
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpsw_meta_xdp *xmeta;
	struct page_pool *pool;
	struct page *page;
	int ch_buf_num;
	int ch, i, ret;
	dma_addr_t dma;

	for (ch = 0; ch < cpsw->rx_ch_num; ch++) {
		pool = cpsw->page_pool[ch];
		ch_buf_num = cpdma_chan_get_rx_buf_num(cpsw->rxv[ch].ch);
		for (i = 0; i < ch_buf_num; i++) {
			page = page_pool_dev_alloc_pages(pool);
			if (!page) {
				cpsw_err(priv, ifup, "allocate rx page err\n");
				return -ENOMEM;
			}

			xmeta = page_address(page) + CPSW_XMETA_OFFSET;
			xmeta->ndev = priv->ndev;
			xmeta->ch = ch;

			dma = page_pool_get_dma_addr(page) + CPSW_HEADROOM;
			ret = cpdma_chan_idle_submit_mapped(cpsw->rxv[ch].ch,
							    page, dma,
							    cpsw->rx_packet_max,
							    0);
			if (ret < 0) {
				cpsw_err(priv, ifup,
					 "cannot submit page to channel %d rx, error %d\n",
					 ch, ret);
				page_pool_recycle_direct(pool, page);
				return ret;
			}
		}

		cpsw_info(priv, ifup, "ch %d rx, submitted %d descriptors\n",
			  ch, ch_buf_num);
	}

	return 0;
}

static void cpsw_slave_stop(struct cpsw_slave *slave, struct cpsw_common *cpsw)
{
	u32 slave_port;

	slave_port = cpsw_get_slave_port(slave->slave_num);

	if (!slave->phy)
		return;
	phy_stop(slave->phy);
	phy_disconnect(slave->phy);
	slave->phy = NULL;
	cpsw_ale_control_set(cpsw->ale, slave_port,
			     ALE_PORT_STATE, ALE_PORT_STATE_DISABLE);
	cpsw_sl_reset(slave->mac_sl, 100);
	cpsw_sl_ctl_reset(slave->mac_sl);
}

static int cpsw_tc_to_fifo(int tc, int num_tc)
{
	if (tc == num_tc - 1)
		return 0;

	return CPSW_FIFO_SHAPERS_NUM - tc;
}

static int cpsw_set_fifo_bw(struct cpsw_priv *priv, int fifo, int bw)
{
	struct cpsw_common *cpsw = priv->cpsw;
	u32 val = 0, send_pct, shift;
	struct cpsw_slave *slave;
	int pct = 0, i;

	if (bw > priv->shp_cfg_speed * 1000)
		goto err;

	/* shaping has to stay enabled for highest fifos linearly
	 * and fifo bw no more then interface can allow
	 */
	slave = &cpsw->slaves[cpsw_slave_index(cpsw, priv)];
	send_pct = slave_read(slave, SEND_PERCENT);
	for (i = CPSW_FIFO_SHAPERS_NUM; i > 0; i--) {
		if (!bw) {
			if (i >= fifo || !priv->fifo_bw[i])
				continue;

			dev_warn(priv->dev, "Prev FIFO%d is shaped", i);
			continue;
		}

		if (!priv->fifo_bw[i] && i > fifo) {
			dev_err(priv->dev, "Upper FIFO%d is not shaped", i);
			return -EINVAL;
		}

		shift = (i - 1) * 8;
		if (i == fifo) {
			send_pct &= ~(CPSW_PCT_MASK << shift);
			val = DIV_ROUND_UP(bw, priv->shp_cfg_speed * 10);
			if (!val)
				val = 1;

			send_pct |= val << shift;
			pct += val;
			continue;
		}

		if (priv->fifo_bw[i])
			pct += (send_pct >> shift) & CPSW_PCT_MASK;
	}

	if (pct >= 100)
		goto err;

	slave_write(slave, send_pct, SEND_PERCENT);
	priv->fifo_bw[fifo] = bw;

	dev_warn(priv->dev, "set FIFO%d bw = %d\n", fifo,
		 DIV_ROUND_CLOSEST(val * priv->shp_cfg_speed, 100));

	return 0;
err:
	dev_err(priv->dev, "Bandwidth doesn't fit in tc configuration");
	return -EINVAL;
}

static int cpsw_set_fifo_rlimit(struct cpsw_priv *priv, int fifo, int bw)
{
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpsw_slave *slave;
	u32 tx_in_ctl_rg, val;
	int ret;

	ret = cpsw_set_fifo_bw(priv, fifo, bw);
	if (ret)
		return ret;

	slave = &cpsw->slaves[cpsw_slave_index(cpsw, priv)];
	tx_in_ctl_rg = cpsw->version == CPSW_VERSION_1 ?
		       CPSW1_TX_IN_CTL : CPSW2_TX_IN_CTL;

	if (!bw)
		cpsw_fifo_shp_on(priv, fifo, bw);

	val = slave_read(slave, tx_in_ctl_rg);
	if (cpsw_shp_is_off(priv)) {
		/* disable FIFOs rate limited queues */
		val &= ~(0xf << CPSW_FIFO_RATE_EN_SHIFT);

		/* set type of FIFO queues to normal priority mode */
		val &= ~(3 << CPSW_FIFO_QUEUE_TYPE_SHIFT);

		/* set type of FIFO queues to be rate limited */
		if (bw)
			val |= 2 << CPSW_FIFO_QUEUE_TYPE_SHIFT;
		else
			priv->shp_cfg_speed = 0;
	}

	/* toggle a FIFO rate limited queue */
	if (bw)
		val |= BIT(fifo + CPSW_FIFO_RATE_EN_SHIFT);
	else
		val &= ~BIT(fifo + CPSW_FIFO_RATE_EN_SHIFT);
	slave_write(slave, val, tx_in_ctl_rg);

	/* FIFO transmit shape enable */
	cpsw_fifo_shp_on(priv, fifo, bw);
	return 0;
}

/* Defaults:
 * class A - prio 3
 * class B - prio 2
 * shaping for class A should be set first
 */
static int cpsw_set_cbs(struct net_device *ndev,
			struct tc_cbs_qopt_offload *qopt)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpsw_slave *slave;
	int prev_speed = 0;
	int tc, ret, fifo;
	u32 bw = 0;

	tc = netdev_txq_to_tc(priv->ndev, qopt->queue);

	/* enable channels in backward order, as highest FIFOs must be rate
	 * limited first and for compliance with CPDMA rate limited channels
	 * that also used in bacward order. FIFO0 cannot be rate limited.
	 */
	fifo = cpsw_tc_to_fifo(tc, ndev->num_tc);
	if (!fifo) {
		dev_err(priv->dev, "Last tc%d can't be rate limited", tc);
		return -EINVAL;
	}

	/* do nothing, it's disabled anyway */
	if (!qopt->enable && !priv->fifo_bw[fifo])
		return 0;

	/* shapers can be set if link speed is known */
	slave = &cpsw->slaves[cpsw_slave_index(cpsw, priv)];
	if (slave->phy && slave->phy->link) {
		if (priv->shp_cfg_speed &&
		    priv->shp_cfg_speed != slave->phy->speed)
			prev_speed = priv->shp_cfg_speed;

		priv->shp_cfg_speed = slave->phy->speed;
	}

	if (!priv->shp_cfg_speed) {
		dev_err(priv->dev, "Link speed is not known");
		return -1;
	}

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	bw = qopt->enable ? qopt->idleslope : 0;
	ret = cpsw_set_fifo_rlimit(priv, fifo, bw);
	if (ret) {
		priv->shp_cfg_speed = prev_speed;
		prev_speed = 0;
	}

	if (bw && prev_speed)
		dev_warn(priv->dev,
			 "Speed was changed, CBS shaper speeds are changed!");

	pm_runtime_put_sync(cpsw->dev);
	return ret;
}

static void cpsw_cbs_resume(struct cpsw_slave *slave, struct cpsw_priv *priv)
{
	int fifo, bw;

	for (fifo = CPSW_FIFO_SHAPERS_NUM; fifo > 0; fifo--) {
		bw = priv->fifo_bw[fifo];
		if (!bw)
			continue;

		cpsw_set_fifo_rlimit(priv, fifo, bw);
	}
}

static void cpsw_mqprio_resume(struct cpsw_slave *slave, struct cpsw_priv *priv)
{
	struct cpsw_common *cpsw = priv->cpsw;
	u32 tx_prio_map = 0;
	int i, tc, fifo;
	u32 tx_prio_rg;

	if (!priv->mqprio_hw)
		return;

	for (i = 0; i < 8; i++) {
		tc = netdev_get_prio_tc_map(priv->ndev, i);
		fifo = CPSW_FIFO_SHAPERS_NUM - tc;
		tx_prio_map |= fifo << (4 * i);
	}

	tx_prio_rg = cpsw->version == CPSW_VERSION_1 ?
		     CPSW1_TX_PRI_MAP : CPSW2_TX_PRI_MAP;

	slave_write(slave, tx_prio_map, tx_prio_rg);
}

static int cpsw_restore_vlans(struct net_device *vdev, int vid, void *arg)
{
	struct cpsw_priv *priv = arg;

	if (!vdev)
		return 0;

	cpsw_ndo_vlan_rx_add_vid(priv->ndev, 0, vid);
	return 0;
}

/* restore resources after port reset */
static void cpsw_restore(struct cpsw_priv *priv)
{
	/* restore vlan configurations */
	vlan_for_each(priv->ndev, cpsw_restore_vlans, priv);

	/* restore MQPRIO offload */
	for_each_slave(priv, cpsw_mqprio_resume, priv);

	/* restore CBS offload */
	for_each_slave(priv, cpsw_cbs_resume, priv);
}

static int cpsw_ndo_open(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int ret;
	u32 reg;

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	netif_carrier_off(ndev);

	/* Notify the stack of the actual queue counts. */
	ret = netif_set_real_num_tx_queues(ndev, cpsw->tx_ch_num);
	if (ret) {
		dev_err(priv->dev, "cannot set real number of tx queues\n");
		goto err_cleanup;
	}

	ret = netif_set_real_num_rx_queues(ndev, cpsw->rx_ch_num);
	if (ret) {
		dev_err(priv->dev, "cannot set real number of rx queues\n");
		goto err_cleanup;
	}

	reg = cpsw->version;

	dev_info(priv->dev, "initializing cpsw version %d.%d (%d)\n",
		 CPSW_MAJOR_VERSION(reg), CPSW_MINOR_VERSION(reg),
		 CPSW_RTL_VERSION(reg));

	/* Initialize host and slave ports */
	if (!cpsw->usage_count)
		cpsw_init_host_port(priv);
	for_each_slave(priv, cpsw_slave_open, priv);

	/* Add default VLAN */
	if (!cpsw->data.dual_emac)
		cpsw_add_default_vlan(priv);
	else
		cpsw_ale_add_vlan(cpsw->ale, cpsw->data.default_vlan,
				  ALE_ALL_PORTS, ALE_ALL_PORTS, 0, 0);

	/* initialize shared resources for every ndev */
	if (!cpsw->usage_count) {
		/* disable priority elevation */
		writel_relaxed(0, &cpsw->regs->ptype);

		/* enable statistics collection only on all ports */
		writel_relaxed(0x7, &cpsw->regs->stat_port_en);

		/* Enable internal fifo flow control */
		writel(0x7, &cpsw->regs->flow_control);

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

		/* create rxqs for both infs in dual mac as they use same pool
		 * and must be destroyed together when no users.
		 */
		ret = cpsw_create_xdp_rxqs(cpsw);
		if (ret < 0)
			goto err_cleanup;

		ret = cpsw_fill_rx_channels(priv);
		if (ret < 0)
			goto err_cleanup;

		if (cpts_register(cpsw->cpts))
			dev_err(priv->dev, "error registering cpts device\n");

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
	if (!cpsw->usage_count) {
		cpdma_ctlr_stop(cpsw->dma);
		cpsw_destroy_xdp_rxqs(cpsw);
	}

	for_each_slave(priv, cpsw_slave_stop, cpsw);
	pm_runtime_put_sync(cpsw->dev);
	netif_carrier_off(priv->ndev);
	return ret;
}

static int cpsw_ndo_stop(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;

	cpsw_info(priv, ifdown, "shutting down cpsw device\n");
	__hw_addr_ref_unsync_dev(&ndev->mc, ndev, cpsw_purge_all_mc);
	netif_tx_stop_all_queues(priv->ndev);
	netif_carrier_off(priv->ndev);

	if (cpsw->usage_count <= 1) {
		napi_disable(&cpsw->napi_rx);
		napi_disable(&cpsw->napi_tx);
		cpts_unregister(cpsw->cpts);
		cpsw_intr_disable(cpsw);
		cpdma_ctlr_stop(cpsw->dma);
		cpsw_ale_stop(cpsw->ale);
		cpsw_destroy_xdp_rxqs(cpsw);
	}
	for_each_slave(priv, cpsw_slave_stop, cpsw);

	if (cpsw_need_resplit(cpsw))
		cpsw_split_res(cpsw);

	cpsw->usage_count--;
	pm_runtime_put_sync(cpsw->dev);
	return 0;
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
				priv->emac_port + cpsw->data.dual_emac);
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

#if IS_ENABLED(CONFIG_TI_CPTS)

static void cpsw_hwtstamp_v1(struct cpsw_priv *priv)
{
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpsw_slave *slave = &cpsw->slaves[cpsw->data.active_slave];
	u32 ts_en, seq_id;

	if (!priv->tx_ts_enabled && !priv->rx_ts_enabled) {
		slave_write(slave, 0, CPSW1_TS_CTL);
		return;
	}

	seq_id = (30 << CPSW_V1_SEQ_ID_OFS_SHIFT) | ETH_P_1588;
	ts_en = EVENT_MSG_BITS << CPSW_V1_MSG_TYPE_OFS;

	if (priv->tx_ts_enabled)
		ts_en |= CPSW_V1_TS_TX_EN;

	if (priv->rx_ts_enabled)
		ts_en |= CPSW_V1_TS_RX_EN;

	slave_write(slave, ts_en, CPSW1_TS_CTL);
	slave_write(slave, seq_id, CPSW1_TS_SEQ_LTYPE);
}

static void cpsw_hwtstamp_v2(struct cpsw_priv *priv)
{
	struct cpsw_slave *slave;
	struct cpsw_common *cpsw = priv->cpsw;
	u32 ctrl, mtype;

	slave = &cpsw->slaves[cpsw_slave_index(cpsw, priv)];

	ctrl = slave_read(slave, CPSW2_CONTROL);
	switch (cpsw->version) {
	case CPSW_VERSION_2:
		ctrl &= ~CTRL_V2_ALL_TS_MASK;

		if (priv->tx_ts_enabled)
			ctrl |= CTRL_V2_TX_TS_BITS;

		if (priv->rx_ts_enabled)
			ctrl |= CTRL_V2_RX_TS_BITS;
		break;
	case CPSW_VERSION_3:
	default:
		ctrl &= ~CTRL_V3_ALL_TS_MASK;

		if (priv->tx_ts_enabled)
			ctrl |= CTRL_V3_TX_TS_BITS;

		if (priv->rx_ts_enabled)
			ctrl |= CTRL_V3_RX_TS_BITS;
		break;
	}

	mtype = (30 << TS_SEQ_ID_OFFSET_SHIFT) | EVENT_MSG_BITS;

	slave_write(slave, mtype, CPSW2_TS_SEQ_MTYPE);
	slave_write(slave, ctrl, CPSW2_CONTROL);
	writel_relaxed(ETH_P_1588, &cpsw->regs->ts_ltype);
	writel_relaxed(ETH_P_8021Q, &cpsw->regs->vlan_ltype);
}

static int cpsw_hwtstamp_set(struct net_device *dev, struct ifreq *ifr)
{
	struct cpsw_priv *priv = netdev_priv(dev);
	struct hwtstamp_config cfg;
	struct cpsw_common *cpsw = priv->cpsw;

	if (cpsw->version != CPSW_VERSION_1 &&
	    cpsw->version != CPSW_VERSION_2 &&
	    cpsw->version != CPSW_VERSION_3)
		return -EOPNOTSUPP;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	/* reserved for future extensions */
	if (cfg.flags)
		return -EINVAL;

	if (cfg.tx_type != HWTSTAMP_TX_OFF && cfg.tx_type != HWTSTAMP_TX_ON)
		return -ERANGE;

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		priv->rx_ts_enabled = 0;
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_NTP_ALL:
		return -ERANGE;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		priv->rx_ts_enabled = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		priv->rx_ts_enabled = HWTSTAMP_FILTER_PTP_V2_EVENT;
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	default:
		return -ERANGE;
	}

	priv->tx_ts_enabled = cfg.tx_type == HWTSTAMP_TX_ON;

	switch (cpsw->version) {
	case CPSW_VERSION_1:
		cpsw_hwtstamp_v1(priv);
		break;
	case CPSW_VERSION_2:
	case CPSW_VERSION_3:
		cpsw_hwtstamp_v2(priv);
		break;
	default:
		WARN_ON(1);
	}

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static int cpsw_hwtstamp_get(struct net_device *dev, struct ifreq *ifr)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(dev);
	struct cpsw_priv *priv = netdev_priv(dev);
	struct hwtstamp_config cfg;

	if (cpsw->version != CPSW_VERSION_1 &&
	    cpsw->version != CPSW_VERSION_2 &&
	    cpsw->version != CPSW_VERSION_3)
		return -EOPNOTSUPP;

	cfg.flags = 0;
	cfg.tx_type = priv->tx_ts_enabled ? HWTSTAMP_TX_ON : HWTSTAMP_TX_OFF;
	cfg.rx_filter = priv->rx_ts_enabled;

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}
#else
static int cpsw_hwtstamp_get(struct net_device *dev, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static int cpsw_hwtstamp_set(struct net_device *dev, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}
#endif /*CONFIG_TI_CPTS*/

static int cpsw_ndo_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	struct cpsw_priv *priv = netdev_priv(dev);
	struct cpsw_common *cpsw = priv->cpsw;
	int slave_no = cpsw_slave_index(cpsw, priv);

	if (!netif_running(dev))
		return -EINVAL;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return cpsw_hwtstamp_set(dev, req);
	case SIOCGHWTSTAMP:
		return cpsw_hwtstamp_get(dev, req);
	}

	if (!cpsw->slaves[slave_no].phy)
		return -EOPNOTSUPP;
	return phy_mii_ioctl(cpsw->slaves[slave_no].phy, req, cmd);
}

static void cpsw_ndo_tx_timeout(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int ch;

	cpsw_err(priv, tx_err, "transmit timeout, restarting dma\n");
	ndev->stats.tx_errors++;
	cpsw_intr_disable(cpsw);
	for (ch = 0; ch < cpsw->tx_ch_num; ch++) {
		cpdma_chan_stop(cpsw->txv[ch].ch);
		cpdma_chan_start(cpsw->txv[ch].ch);
	}

	cpsw_intr_enable(cpsw);
	netif_trans_update(ndev);
	netif_tx_wake_all_queues(ndev);
}

static int cpsw_ndo_set_mac_address(struct net_device *ndev, void *p)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct sockaddr *addr = (struct sockaddr *)p;
	struct cpsw_common *cpsw = priv->cpsw;
	int flags = 0;
	u16 vid = 0;
	int ret;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	if (cpsw->data.dual_emac) {
		vid = cpsw->slaves[priv->emac_port].port_vlan;
		flags = ALE_VLAN;
	}

	cpsw_ale_del_ucast(cpsw->ale, priv->mac_addr, HOST_PORT_NUM,
			   flags, vid);
	cpsw_ale_add_ucast(cpsw->ale, addr->sa_data, HOST_PORT_NUM,
			   flags, vid);

	memcpy(priv->mac_addr, addr->sa_data, ETH_ALEN);
	memcpy(ndev->dev_addr, priv->mac_addr, ETH_ALEN);
	for_each_slave(priv, cpsw_set_slave_mac, priv);

	pm_runtime_put(cpsw->dev);

	return 0;
}

static inline int cpsw_add_vlan_ale_entry(struct cpsw_priv *priv,
				unsigned short vid)
{
	int ret;
	int unreg_mcast_mask = 0;
	int mcast_mask;
	u32 port_mask;
	struct cpsw_common *cpsw = priv->cpsw;

	if (cpsw->data.dual_emac) {
		port_mask = (1 << (priv->emac_port + 1)) | ALE_PORT_HOST;

		mcast_mask = ALE_PORT_HOST;
		if (priv->ndev->flags & IFF_ALLMULTI)
			unreg_mcast_mask = mcast_mask;
	} else {
		port_mask = ALE_ALL_PORTS;
		mcast_mask = port_mask;

		if (priv->ndev->flags & IFF_ALLMULTI)
			unreg_mcast_mask = ALE_ALL_PORTS;
		else
			unreg_mcast_mask = ALE_PORT_1 | ALE_PORT_2;
	}

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
	int ret;

	if (vid == cpsw->data.default_vlan)
		return 0;

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	if (cpsw->data.dual_emac) {
		/* In dual EMAC, reserved VLAN id should not be used for
		 * creating VLAN interfaces as this can break the dual
		 * EMAC port separation
		 */
		int i;

		for (i = 0; i < cpsw->data.slaves; i++) {
			if (vid == cpsw->slaves[i].port_vlan) {
				ret = -EINVAL;
				goto err;
			}
		}
	}

	dev_info(priv->dev, "Adding vlanid %d to vlan filter\n", vid);
	ret = cpsw_add_vlan_ale_entry(priv, vid);
err:
	pm_runtime_put(cpsw->dev);
	return ret;
}

static int cpsw_ndo_vlan_rx_kill_vid(struct net_device *ndev,
				     __be16 proto, u16 vid)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int ret;

	if (vid == cpsw->data.default_vlan)
		return 0;

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	if (cpsw->data.dual_emac) {
		int i;

		for (i = 0; i < cpsw->data.slaves; i++) {
			if (vid == cpsw->slaves[i].port_vlan)
				goto err;
		}
	}

	dev_info(priv->dev, "removing vlanid %d from vlan filter\n", vid);
	ret = cpsw_ale_del_vlan(cpsw->ale, vid, 0);
	ret |= cpsw_ale_del_ucast(cpsw->ale, priv->mac_addr,
				  HOST_PORT_NUM, ALE_VLAN, vid);
	ret |= cpsw_ale_del_mcast(cpsw->ale, priv->ndev->broadcast,
				  0, ALE_VLAN, vid);
	ret |= cpsw_ale_flush_multicast(cpsw->ale, 0, vid);
err:
	pm_runtime_put(cpsw->dev);
	return ret;
}

static int cpsw_ndo_set_tx_maxrate(struct net_device *ndev, int queue, u32 rate)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpsw_slave *slave;
	u32 min_rate;
	u32 ch_rate;
	int i, ret;

	ch_rate = netdev_get_tx_queue(ndev, queue)->tx_maxrate;
	if (ch_rate == rate)
		return 0;

	ch_rate = rate * 1000;
	min_rate = cpdma_chan_get_min_rate(cpsw->dma);
	if ((ch_rate < min_rate && ch_rate)) {
		dev_err(priv->dev, "The channel rate cannot be less than %dMbps",
			min_rate);
		return -EINVAL;
	}

	if (rate > cpsw->speed) {
		dev_err(priv->dev, "The channel rate cannot be more than 2Gbps");
		return -EINVAL;
	}

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	ret = cpdma_chan_set_rate(cpsw->txv[queue].ch, ch_rate);
	pm_runtime_put(cpsw->dev);

	if (ret)
		return ret;

	/* update rates for slaves tx queues */
	for (i = 0; i < cpsw->data.slaves; i++) {
		slave = &cpsw->slaves[i];
		if (!slave->ndev)
			continue;

		netdev_get_tx_queue(slave->ndev, queue)->tx_maxrate = rate;
	}

	cpsw_split_res(cpsw);
	return ret;
}

static int cpsw_set_mqprio(struct net_device *ndev, void *type_data)
{
	struct tc_mqprio_qopt_offload *mqprio = type_data;
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int fifo, num_tc, count, offset;
	struct cpsw_slave *slave;
	u32 tx_prio_map = 0;
	int i, tc, ret;

	num_tc = mqprio->qopt.num_tc;
	if (num_tc > CPSW_TC_NUM)
		return -EINVAL;

	if (mqprio->mode != TC_MQPRIO_MODE_DCB)
		return -EINVAL;

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(cpsw->dev);
		return ret;
	}

	if (num_tc) {
		for (i = 0; i < 8; i++) {
			tc = mqprio->qopt.prio_tc_map[i];
			fifo = cpsw_tc_to_fifo(tc, num_tc);
			tx_prio_map |= fifo << (4 * i);
		}

		netdev_set_num_tc(ndev, num_tc);
		for (i = 0; i < num_tc; i++) {
			count = mqprio->qopt.count[i];
			offset = mqprio->qopt.offset[i];
			netdev_set_tc_queue(ndev, i, count, offset);
		}
	}

	if (!mqprio->qopt.hw) {
		/* restore default configuration */
		netdev_reset_tc(ndev);
		tx_prio_map = TX_PRIORITY_MAPPING;
	}

	priv->mqprio_hw = mqprio->qopt.hw;

	offset = cpsw->version == CPSW_VERSION_1 ?
		 CPSW1_TX_PRI_MAP : CPSW2_TX_PRI_MAP;

	slave = &cpsw->slaves[cpsw_slave_index(cpsw, priv)];
	slave_write(slave, tx_prio_map, offset);

	pm_runtime_put_sync(cpsw->dev);

	return 0;
}

static int cpsw_ndo_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			     void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_CBS:
		return cpsw_set_cbs(ndev, type_data);

	case TC_SETUP_QDISC_MQPRIO:
		return cpsw_set_mqprio(ndev, type_data);

	default:
		return -EOPNOTSUPP;
	}
}

static int cpsw_xdp_prog_setup(struct cpsw_priv *priv, struct netdev_bpf *bpf)
{
	struct bpf_prog *prog = bpf->prog;

	if (!priv->xdpi.prog && !prog)
		return 0;

	if (!xdp_attachment_flags_ok(&priv->xdpi, bpf))
		return -EBUSY;

	WRITE_ONCE(priv->xdp_prog, prog);

	xdp_attachment_setup(&priv->xdpi, bpf);

	return 0;
}

static int cpsw_ndo_bpf(struct net_device *ndev, struct netdev_bpf *bpf)
{
	struct cpsw_priv *priv = netdev_priv(ndev);

	switch (bpf->command) {
	case XDP_SETUP_PROG:
		return cpsw_xdp_prog_setup(priv, bpf);

	case XDP_QUERY_PROG:
		return xdp_attachment_query(&priv->xdpi, bpf);

	default:
		return -EINVAL;
	}
}

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

		if (cpsw_xdp_tx_frame(priv, xdpf, NULL))
			drops++;
	}

	return n - drops;
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
	.ndo_bpf		= cpsw_ndo_bpf,
	.ndo_xdp_xmit		= cpsw_ndo_xdp_xmit,
};

static void cpsw_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);
	struct platform_device	*pdev = to_platform_device(cpsw->dev);

	strlcpy(info->driver, "cpsw", sizeof(info->driver));
	strlcpy(info->version, "1.0", sizeof(info->version));
	strlcpy(info->bus_info, pdev->name, sizeof(info->bus_info));
}

static int cpsw_set_pauseparam(struct net_device *ndev,
			       struct ethtool_pauseparam *pause)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	bool link;

	priv->rx_pause = pause->rx_pause ? true : false;
	priv->tx_pause = pause->tx_pause ? true : false;

	for_each_slave(priv, _cpsw_adjust_link, priv, &link);
	return 0;
}

static int cpsw_set_channels(struct net_device *ndev,
			     struct ethtool_channels *chs)
{
	return cpsw_set_channels_common(ndev, chs, cpsw_rx_handler);
}

static const struct ethtool_ops cpsw_ethtool_ops = {
	.get_drvinfo	= cpsw_get_drvinfo,
	.get_msglevel	= cpsw_get_msglevel,
	.set_msglevel	= cpsw_set_msglevel,
	.get_link	= ethtool_op_get_link,
	.get_ts_info	= cpsw_get_ts_info,
	.get_coalesce	= cpsw_get_coalesce,
	.set_coalesce	= cpsw_set_coalesce,
	.get_sset_count		= cpsw_get_sset_count,
	.get_strings		= cpsw_get_strings,
	.get_ethtool_stats	= cpsw_get_ethtool_stats,
	.get_pauseparam		= cpsw_get_pauseparam,
	.set_pauseparam		= cpsw_set_pauseparam,
	.get_wol	= cpsw_get_wol,
	.set_wol	= cpsw_set_wol,
	.get_regs_len	= cpsw_get_regs_len,
	.get_regs	= cpsw_get_regs,
	.begin		= cpsw_ethtool_op_begin,
	.complete	= cpsw_ethtool_op_complete,
	.get_channels	= cpsw_get_channels,
	.set_channels	= cpsw_set_channels,
	.get_link_ksettings	= cpsw_get_link_ksettings,
	.set_link_ksettings	= cpsw_set_link_ksettings,
	.get_eee	= cpsw_get_eee,
	.set_eee	= cpsw_set_eee,
	.nway_reset	= cpsw_nway_reset,
	.get_ringparam = cpsw_get_ringparam,
	.set_ringparam = cpsw_set_ringparam,
};

static int cpsw_probe_dt(struct cpsw_platform_data *data,
			 struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *slave_node;
	int i = 0, ret;
	u32 prop;

	if (!node)
		return -EINVAL;

	if (of_property_read_u32(node, "slaves", &prop)) {
		dev_err(&pdev->dev, "Missing slaves property in the DT.\n");
		return -EINVAL;
	}
	data->slaves = prop;

	if (of_property_read_u32(node, "active_slave", &prop)) {
		dev_err(&pdev->dev, "Missing active_slave property in the DT.\n");
		return -EINVAL;
	}
	data->active_slave = prop;

	data->slave_data = devm_kcalloc(&pdev->dev,
					data->slaves,
					sizeof(struct cpsw_slave_data),
					GFP_KERNEL);
	if (!data->slave_data)
		return -ENOMEM;

	if (of_property_read_u32(node, "cpdma_channels", &prop)) {
		dev_err(&pdev->dev, "Missing cpdma_channels property in the DT.\n");
		return -EINVAL;
	}
	data->channels = prop;

	if (of_property_read_u32(node, "ale_entries", &prop)) {
		dev_err(&pdev->dev, "Missing ale_entries property in the DT.\n");
		return -EINVAL;
	}
	data->ale_entries = prop;

	if (of_property_read_u32(node, "bd_ram_size", &prop)) {
		dev_err(&pdev->dev, "Missing bd_ram_size property in the DT.\n");
		return -EINVAL;
	}
	data->bd_ram_size = prop;

	if (of_property_read_u32(node, "mac_control", &prop)) {
		dev_err(&pdev->dev, "Missing mac_control property in the DT.\n");
		return -EINVAL;
	}
	data->mac_control = prop;

	if (of_property_read_bool(node, "dual_emac"))
		data->dual_emac = 1;

	/*
	 * Populate all the child nodes here...
	 */
	ret = of_platform_populate(node, NULL, NULL, &pdev->dev);
	/* We do not want to force this, as in some cases may not have child */
	if (ret)
		dev_warn(&pdev->dev, "Doesn't have any child node\n");

	for_each_available_child_of_node(node, slave_node) {
		struct cpsw_slave_data *slave_data = data->slave_data + i;
		const void *mac_addr = NULL;
		int lenp;
		const __be32 *parp;

		/* This is no slave child node, continue */
		if (!of_node_name_eq(slave_node, "slave"))
			continue;

		slave_data->ifphy = devm_of_phy_get(&pdev->dev, slave_node,
						    NULL);
		if (!IS_ENABLED(CONFIG_TI_CPSW_PHY_SEL) &&
		    IS_ERR(slave_data->ifphy)) {
			ret = PTR_ERR(slave_data->ifphy);
			dev_err(&pdev->dev,
				"%d: Error retrieving port phy: %d\n", i, ret);
			goto err_node_put;
		}

		slave_data->slave_node = slave_node;
		slave_data->phy_node = of_parse_phandle(slave_node,
							"phy-handle", 0);
		parp = of_get_property(slave_node, "phy_id", &lenp);
		if (slave_data->phy_node) {
			dev_dbg(&pdev->dev,
				"slave[%d] using phy-handle=\"%pOF\"\n",
				i, slave_data->phy_node);
		} else if (of_phy_is_fixed_link(slave_node)) {
			/* In the case of a fixed PHY, the DT node associated
			 * to the PHY is the Ethernet MAC DT node.
			 */
			ret = of_phy_register_fixed_link(slave_node);
			if (ret) {
				if (ret != -EPROBE_DEFER)
					dev_err(&pdev->dev, "failed to register fixed-link phy: %d\n", ret);
				goto err_node_put;
			}
			slave_data->phy_node = of_node_get(slave_node);
		} else if (parp) {
			u32 phyid;
			struct device_node *mdio_node;
			struct platform_device *mdio;

			if (lenp != (sizeof(__be32) * 2)) {
				dev_err(&pdev->dev, "Invalid slave[%d] phy_id property\n", i);
				goto no_phy_slave;
			}
			mdio_node = of_find_node_by_phandle(be32_to_cpup(parp));
			phyid = be32_to_cpup(parp+1);
			mdio = of_find_device_by_node(mdio_node);
			of_node_put(mdio_node);
			if (!mdio) {
				dev_err(&pdev->dev, "Missing mdio platform device\n");
				ret = -EINVAL;
				goto err_node_put;
			}
			snprintf(slave_data->phy_id, sizeof(slave_data->phy_id),
				 PHY_ID_FMT, mdio->name, phyid);
			put_device(&mdio->dev);
		} else {
			dev_err(&pdev->dev,
				"No slave[%d] phy_id, phy-handle, or fixed-link property\n",
				i);
			goto no_phy_slave;
		}
		slave_data->phy_if = of_get_phy_mode(slave_node);
		if (slave_data->phy_if < 0) {
			dev_err(&pdev->dev, "Missing or malformed slave[%d] phy-mode property\n",
				i);
			ret = slave_data->phy_if;
			goto err_node_put;
		}

no_phy_slave:
		mac_addr = of_get_mac_address(slave_node);
		if (!IS_ERR(mac_addr)) {
			ether_addr_copy(slave_data->mac_addr, mac_addr);
		} else {
			ret = ti_cm_get_macid(&pdev->dev, i,
					      slave_data->mac_addr);
			if (ret)
				goto err_node_put;
		}
		if (data->dual_emac) {
			if (of_property_read_u32(slave_node, "dual_emac_res_vlan",
						 &prop)) {
				dev_err(&pdev->dev, "Missing dual_emac_res_vlan in DT.\n");
				slave_data->dual_emac_res_vlan = i+1;
				dev_err(&pdev->dev, "Using %d as Reserved VLAN for %d slave\n",
					slave_data->dual_emac_res_vlan, i);
			} else {
				slave_data->dual_emac_res_vlan = prop;
			}
		}

		i++;
		if (i == data->slaves) {
			ret = 0;
			goto err_node_put;
		}
	}

	return 0;

err_node_put:
	of_node_put(slave_node);
	return ret;
}

static void cpsw_remove_dt(struct platform_device *pdev)
{
	struct cpsw_common *cpsw = platform_get_drvdata(pdev);
	struct cpsw_platform_data *data = &cpsw->data;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *slave_node;
	int i = 0;

	for_each_available_child_of_node(node, slave_node) {
		struct cpsw_slave_data *slave_data = &data->slave_data[i];

		if (!of_node_name_eq(slave_node, "slave"))
			continue;

		if (of_phy_is_fixed_link(slave_node))
			of_phy_deregister_fixed_link(slave_node);

		of_node_put(slave_data->phy_node);

		i++;
		if (i == data->slaves) {
			of_node_put(slave_node);
			break;
		}
	}

	of_platform_depopulate(&pdev->dev);
}

static int cpsw_probe_dual_emac(struct cpsw_priv *priv)
{
	struct cpsw_common		*cpsw = priv->cpsw;
	struct cpsw_platform_data	*data = &cpsw->data;
	struct net_device		*ndev;
	struct cpsw_priv		*priv_sl2;
	int ret = 0;

	ndev = devm_alloc_etherdev_mqs(cpsw->dev, sizeof(struct cpsw_priv),
				       CPSW_MAX_QUEUES, CPSW_MAX_QUEUES);
	if (!ndev) {
		dev_err(cpsw->dev, "cpsw: error allocating net_device\n");
		return -ENOMEM;
	}

	priv_sl2 = netdev_priv(ndev);
	priv_sl2->cpsw = cpsw;
	priv_sl2->ndev = ndev;
	priv_sl2->dev  = &ndev->dev;
	priv_sl2->msg_enable = netif_msg_init(debug_level, CPSW_DEBUG);

	if (is_valid_ether_addr(data->slave_data[1].mac_addr)) {
		memcpy(priv_sl2->mac_addr, data->slave_data[1].mac_addr,
			ETH_ALEN);
		dev_info(cpsw->dev, "cpsw: Detected MACID = %pM\n",
			 priv_sl2->mac_addr);
	} else {
		eth_random_addr(priv_sl2->mac_addr);
		dev_info(cpsw->dev, "cpsw: Random MACID = %pM\n",
			 priv_sl2->mac_addr);
	}
	memcpy(ndev->dev_addr, priv_sl2->mac_addr, ETH_ALEN);

	priv_sl2->emac_port = 1;
	cpsw->slaves[1].ndev = ndev;
	ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_CTAG_RX;

	ndev->netdev_ops = &cpsw_netdev_ops;
	ndev->ethtool_ops = &cpsw_ethtool_ops;

	/* register the network device */
	SET_NETDEV_DEV(ndev, cpsw->dev);
	ndev->dev.of_node = cpsw->slaves[1].data->slave_node;
	ret = register_netdev(ndev);
	if (ret)
		dev_err(cpsw->dev, "cpsw: error registering net device\n");

	return ret;
}

static const struct of_device_id cpsw_of_mtable[] = {
	{ .compatible = "ti,cpsw"},
	{ .compatible = "ti,am335x-cpsw"},
	{ .compatible = "ti,am4372-cpsw"},
	{ .compatible = "ti,dra7-cpsw"},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cpsw_of_mtable);

static const struct soc_device_attribute cpsw_soc_devices[] = {
	{ .family = "AM33xx", .revision = "ES1.0"},
	{ /* sentinel */ }
};

static int cpsw_probe(struct platform_device *pdev)
{
	struct device			*dev = &pdev->dev;
	struct clk			*clk;
	struct cpsw_platform_data	*data;
	struct net_device		*ndev;
	struct cpsw_priv		*priv;
	void __iomem			*ss_regs;
	struct resource			*res, *ss_res;
	struct gpio_descs		*mode;
	const struct soc_device_attribute *soc;
	struct cpsw_common		*cpsw;
	int ret = 0, ch;
	int irq;

	cpsw = devm_kzalloc(dev, sizeof(struct cpsw_common), GFP_KERNEL);
	if (!cpsw)
		return -ENOMEM;

	cpsw->dev = dev;

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
	if (IS_ERR(ss_regs))
		return PTR_ERR(ss_regs);
	cpsw->regs = ss_regs;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	cpsw->wr_regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(cpsw->wr_regs))
		return PTR_ERR(cpsw->wr_regs);

	/* RX IRQ */
	irq = platform_get_irq(pdev, 1);
	if (irq < 0)
		return irq;
	cpsw->irqs_table[0] = irq;

	/* TX IRQ */
	irq = platform_get_irq(pdev, 2);
	if (irq < 0)
		return irq;
	cpsw->irqs_table[1] = irq;

	/*
	 * This may be required here for child devices.
	 */
	pm_runtime_enable(dev);

	/* Need to enable clocks with runtime PM api to access module
	 * registers
	 */
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		goto clean_runtime_disable_ret;
	}

	ret = cpsw_probe_dt(&cpsw->data, pdev);
	if (ret)
		goto clean_dt_ret;

	soc = soc_device_match(cpsw_soc_devices);
	if (soc)
		cpsw->quirk_irq = 1;

	data = &cpsw->data;
	cpsw->slaves = devm_kcalloc(dev,
				    data->slaves, sizeof(struct cpsw_slave),
				    GFP_KERNEL);
	if (!cpsw->slaves) {
		ret = -ENOMEM;
		goto clean_dt_ret;
	}

	cpsw->rx_packet_max = max(rx_packet_max, CPSW_MAX_PACKET_SIZE);
	cpsw->descs_pool_size = descs_pool_size;

	ret = cpsw_init_common(cpsw, ss_regs, ale_ageout,
			       ss_res->start + CPSW2_BD_OFFSET,
			       descs_pool_size);
	if (ret)
		goto clean_dt_ret;

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

	/* setup netdev */
	ndev = devm_alloc_etherdev_mqs(dev, sizeof(struct cpsw_priv),
				       CPSW_MAX_QUEUES, CPSW_MAX_QUEUES);
	if (!ndev) {
		dev_err(dev, "error allocating net_device\n");
		goto clean_cpts;
	}

	platform_set_drvdata(pdev, cpsw);
	priv = netdev_priv(ndev);
	priv->cpsw = cpsw;
	priv->ndev = ndev;
	priv->dev  = dev;
	priv->msg_enable = netif_msg_init(debug_level, CPSW_DEBUG);
	priv->emac_port = 0;

	if (is_valid_ether_addr(data->slave_data[0].mac_addr)) {
		memcpy(priv->mac_addr, data->slave_data[0].mac_addr, ETH_ALEN);
		dev_info(dev, "Detected MACID = %pM\n", priv->mac_addr);
	} else {
		eth_random_addr(priv->mac_addr);
		dev_info(dev, "Random MACID = %pM\n", priv->mac_addr);
	}

	memcpy(ndev->dev_addr, priv->mac_addr, ETH_ALEN);

	cpsw->slaves[0].ndev = ndev;

	ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_CTAG_RX;

	ndev->netdev_ops = &cpsw_netdev_ops;
	ndev->ethtool_ops = &cpsw_ethtool_ops;
	netif_napi_add(ndev, &cpsw->napi_rx,
		       cpsw->quirk_irq ? cpsw_rx_poll : cpsw_rx_mq_poll,
		       CPSW_POLL_WEIGHT);
	netif_tx_napi_add(ndev, &cpsw->napi_tx,
			  cpsw->quirk_irq ? cpsw_tx_poll : cpsw_tx_mq_poll,
			  CPSW_POLL_WEIGHT);

	/* register the network device */
	SET_NETDEV_DEV(ndev, dev);
	ndev->dev.of_node = cpsw->slaves[0].data->slave_node;
	ret = register_netdev(ndev);
	if (ret) {
		dev_err(dev, "error registering net device\n");
		ret = -ENODEV;
		goto clean_cpts;
	}

	if (cpsw->data.dual_emac) {
		ret = cpsw_probe_dual_emac(priv);
		if (ret) {
			cpsw_err(priv, probe, "error probe slave 2 emac interface\n");
			goto clean_unregister_netdev_ret;
		}
	}

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
		goto clean_unregister_netdev_ret;
	}


	ret = devm_request_irq(dev, cpsw->irqs_table[1], cpsw_tx_interrupt,
			       0, dev_name(&pdev->dev), cpsw);
	if (ret < 0) {
		dev_err(dev, "error attaching irq (%d)\n", ret);
		goto clean_unregister_netdev_ret;
	}

	cpsw_notice(priv, probe,
		    "initialized device (regs %pa, irq %d, pool size %d)\n",
		    &ss_res->start, cpsw->irqs_table[0], descs_pool_size);

	pm_runtime_put(&pdev->dev);

	return 0;

clean_unregister_netdev_ret:
	unregister_netdev(ndev);
clean_cpts:
	cpts_release(cpsw->cpts);
	cpdma_ctlr_destroy(cpsw->dma);
clean_dt_ret:
	cpsw_remove_dt(pdev);
	pm_runtime_put_sync(&pdev->dev);
clean_runtime_disable_ret:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int cpsw_remove(struct platform_device *pdev)
{
	struct cpsw_common *cpsw = platform_get_drvdata(pdev);
	int i, ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&pdev->dev);
		return ret;
	}

	for (i = 0; i < cpsw->data.slaves; i++)
		if (cpsw->slaves[i].ndev)
			unregister_netdev(cpsw->slaves[i].ndev);

	cpts_release(cpsw->cpts);
	cpdma_ctlr_destroy(cpsw->dma);
	cpsw_remove_dt(pdev);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cpsw_suspend(struct device *dev)
{
	struct cpsw_common *cpsw = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < cpsw->data.slaves; i++)
		if (cpsw->slaves[i].ndev)
			if (netif_running(cpsw->slaves[i].ndev))
				cpsw_ndo_stop(cpsw->slaves[i].ndev);

	/* Select sleep pin state */
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int cpsw_resume(struct device *dev)
{
	struct cpsw_common *cpsw = dev_get_drvdata(dev);
	int i;

	/* Select default pin state */
	pinctrl_pm_select_default_state(dev);

	/* shut up ASSERT_RTNL() warning in netif_set_real_num_tx/rx_queues */
	rtnl_lock();

	for (i = 0; i < cpsw->data.slaves; i++)
		if (cpsw->slaves[i].ndev)
			if (netif_running(cpsw->slaves[i].ndev))
				cpsw_ndo_open(cpsw->slaves[i].ndev);

	rtnl_unlock();

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cpsw_pm_ops, cpsw_suspend, cpsw_resume);

static struct platform_driver cpsw_driver = {
	.driver = {
		.name	 = "cpsw",
		.pm	 = &cpsw_pm_ops,
		.of_match_table = cpsw_of_mtable,
	},
	.probe = cpsw_probe,
	.remove = cpsw_remove,
};

module_platform_driver(cpsw_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cyril Chemparathy <cyril@ti.com>");
MODULE_AUTHOR("Mugunthan V N <mugunthanvnm@ti.com>");
MODULE_DESCRIPTION("TI CPSW Ethernet driver");
