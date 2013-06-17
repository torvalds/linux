/*
 * Texas Instruments Ethernet Switch Driver
 *
 * Copyright (C) 2012 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_device.h>
#include <linux/if_vlan.h>

#include <linux/platform_data/cpsw.h>

#include "cpsw_ale.h"
#include "cpts.h"
#include "davinci_cpdma.h"

#define CPSW_DEBUG	(NETIF_MSG_HW		| NETIF_MSG_WOL		| \
			 NETIF_MSG_DRV		| NETIF_MSG_LINK	| \
			 NETIF_MSG_IFUP		| NETIF_MSG_INTR	| \
			 NETIF_MSG_PROBE	| NETIF_MSG_TIMER	| \
			 NETIF_MSG_IFDOWN	| NETIF_MSG_RX_ERR	| \
			 NETIF_MSG_TX_ERR	| NETIF_MSG_TX_DONE	| \
			 NETIF_MSG_PKTDATA	| NETIF_MSG_TX_QUEUED	| \
			 NETIF_MSG_RX_STATUS)

#define cpsw_info(priv, type, format, ...)		\
do {								\
	if (netif_msg_##type(priv) && net_ratelimit())		\
		dev_info(priv->dev, format, ## __VA_ARGS__);	\
} while (0)

#define cpsw_err(priv, type, format, ...)		\
do {								\
	if (netif_msg_##type(priv) && net_ratelimit())		\
		dev_err(priv->dev, format, ## __VA_ARGS__);	\
} while (0)

#define cpsw_dbg(priv, type, format, ...)		\
do {								\
	if (netif_msg_##type(priv) && net_ratelimit())		\
		dev_dbg(priv->dev, format, ## __VA_ARGS__);	\
} while (0)

#define cpsw_notice(priv, type, format, ...)		\
do {								\
	if (netif_msg_##type(priv) && net_ratelimit())		\
		dev_notice(priv->dev, format, ## __VA_ARGS__);	\
} while (0)

#define ALE_ALL_PORTS		0x7

#define CPSW_MAJOR_VERSION(reg)		(reg >> 8 & 0x7)
#define CPSW_MINOR_VERSION(reg)		(reg & 0xff)
#define CPSW_RTL_VERSION(reg)		((reg >> 11) & 0x1f)

#define CPSW_VERSION_1		0x19010a
#define CPSW_VERSION_2		0x19010c

#define HOST_PORT_NUM		0
#define SLIVER_SIZE		0x40

#define CPSW1_HOST_PORT_OFFSET	0x028
#define CPSW1_SLAVE_OFFSET	0x050
#define CPSW1_SLAVE_SIZE	0x040
#define CPSW1_CPDMA_OFFSET	0x100
#define CPSW1_STATERAM_OFFSET	0x200
#define CPSW1_CPTS_OFFSET	0x500
#define CPSW1_ALE_OFFSET	0x600
#define CPSW1_SLIVER_OFFSET	0x700

#define CPSW2_HOST_PORT_OFFSET	0x108
#define CPSW2_SLAVE_OFFSET	0x200
#define CPSW2_SLAVE_SIZE	0x100
#define CPSW2_CPDMA_OFFSET	0x800
#define CPSW2_STATERAM_OFFSET	0xa00
#define CPSW2_CPTS_OFFSET	0xc00
#define CPSW2_ALE_OFFSET	0xd00
#define CPSW2_SLIVER_OFFSET	0xd80
#define CPSW2_BD_OFFSET		0x2000

#define CPDMA_RXTHRESH		0x0c0
#define CPDMA_RXFREE		0x0e0
#define CPDMA_TXHDP		0x00
#define CPDMA_RXHDP		0x20
#define CPDMA_TXCP		0x40
#define CPDMA_RXCP		0x60

#define CPSW_POLL_WEIGHT	64
#define CPSW_MIN_PACKET_SIZE	60
#define CPSW_MAX_PACKET_SIZE	(1500 + 14 + 4 + 4)

#define RX_PRIORITY_MAPPING	0x76543210
#define TX_PRIORITY_MAPPING	0x33221100
#define CPDMA_TX_PRIORITY_MAP	0x76543210

#define CPSW_VLAN_AWARE		BIT(1)
#define CPSW_ALE_VLAN_AWARE	1

#define CPSW_FIFO_NORMAL_MODE		(0 << 15)
#define CPSW_FIFO_DUAL_MAC_MODE		(1 << 15)
#define CPSW_FIFO_RATE_LIMIT_MODE	(2 << 15)

#define CPSW_INTPACEEN		(0x3f << 16)
#define CPSW_INTPRESCALE_MASK	(0x7FF << 0)
#define CPSW_CMINTMAX_CNT	63
#define CPSW_CMINTMIN_CNT	2
#define CPSW_CMINTMAX_INTVL	(1000 / CPSW_CMINTMIN_CNT)
#define CPSW_CMINTMIN_INTVL	((1000 / CPSW_CMINTMAX_CNT) + 1)

#define cpsw_enable_irq(priv)	\
	do {			\
		u32 i;		\
		for (i = 0; i < priv->num_irqs; i++) \
			enable_irq(priv->irqs_table[i]); \
	} while (0);
#define cpsw_disable_irq(priv)	\
	do {			\
		u32 i;		\
		for (i = 0; i < priv->num_irqs; i++) \
			disable_irq_nosync(priv->irqs_table[i]); \
	} while (0);

#define cpsw_slave_index(priv)				\
		((priv->data.dual_emac) ? priv->emac_port :	\
		priv->data.active_slave)

static int debug_level;
module_param(debug_level, int, 0);
MODULE_PARM_DESC(debug_level, "cpsw debug level (NETIF_MSG bits)");

static int ale_ageout = 10;
module_param(ale_ageout, int, 0);
MODULE_PARM_DESC(ale_ageout, "cpsw ale ageout interval (seconds)");

static int rx_packet_max = CPSW_MAX_PACKET_SIZE;
module_param(rx_packet_max, int, 0);
MODULE_PARM_DESC(rx_packet_max, "maximum receive packet size (bytes)");

struct cpsw_wr_regs {
	u32	id_ver;
	u32	soft_reset;
	u32	control;
	u32	int_control;
	u32	rx_thresh_en;
	u32	rx_en;
	u32	tx_en;
	u32	misc_en;
	u32	mem_allign1[8];
	u32	rx_thresh_stat;
	u32	rx_stat;
	u32	tx_stat;
	u32	misc_stat;
	u32	mem_allign2[8];
	u32	rx_imax;
	u32	tx_imax;

};

struct cpsw_ss_regs {
	u32	id_ver;
	u32	control;
	u32	soft_reset;
	u32	stat_port_en;
	u32	ptype;
	u32	soft_idle;
	u32	thru_rate;
	u32	gap_thresh;
	u32	tx_start_wds;
	u32	flow_control;
	u32	vlan_ltype;
	u32	ts_ltype;
	u32	dlr_ltype;
};

/* CPSW_PORT_V1 */
#define CPSW1_MAX_BLKS      0x00 /* Maximum FIFO Blocks */
#define CPSW1_BLK_CNT       0x04 /* FIFO Block Usage Count (Read Only) */
#define CPSW1_TX_IN_CTL     0x08 /* Transmit FIFO Control */
#define CPSW1_PORT_VLAN     0x0c /* VLAN Register */
#define CPSW1_TX_PRI_MAP    0x10 /* Tx Header Priority to Switch Pri Mapping */
#define CPSW1_TS_CTL        0x14 /* Time Sync Control */
#define CPSW1_TS_SEQ_LTYPE  0x18 /* Time Sync Sequence ID Offset and Msg Type */
#define CPSW1_TS_VLAN       0x1c /* Time Sync VLAN1 and VLAN2 */

/* CPSW_PORT_V2 */
#define CPSW2_CONTROL       0x00 /* Control Register */
#define CPSW2_MAX_BLKS      0x08 /* Maximum FIFO Blocks */
#define CPSW2_BLK_CNT       0x0c /* FIFO Block Usage Count (Read Only) */
#define CPSW2_TX_IN_CTL     0x10 /* Transmit FIFO Control */
#define CPSW2_PORT_VLAN     0x14 /* VLAN Register */
#define CPSW2_TX_PRI_MAP    0x18 /* Tx Header Priority to Switch Pri Mapping */
#define CPSW2_TS_SEQ_MTYPE  0x1c /* Time Sync Sequence ID Offset and Msg Type */

/* CPSW_PORT_V1 and V2 */
#define SA_LO               0x20 /* CPGMAC_SL Source Address Low */
#define SA_HI               0x24 /* CPGMAC_SL Source Address High */
#define SEND_PERCENT        0x28 /* Transmit Queue Send Percentages */

/* CPSW_PORT_V2 only */
#define RX_DSCP_PRI_MAP0    0x30 /* Rx DSCP Priority to Rx Packet Mapping */
#define RX_DSCP_PRI_MAP1    0x34 /* Rx DSCP Priority to Rx Packet Mapping */
#define RX_DSCP_PRI_MAP2    0x38 /* Rx DSCP Priority to Rx Packet Mapping */
#define RX_DSCP_PRI_MAP3    0x3c /* Rx DSCP Priority to Rx Packet Mapping */
#define RX_DSCP_PRI_MAP4    0x40 /* Rx DSCP Priority to Rx Packet Mapping */
#define RX_DSCP_PRI_MAP5    0x44 /* Rx DSCP Priority to Rx Packet Mapping */
#define RX_DSCP_PRI_MAP6    0x48 /* Rx DSCP Priority to Rx Packet Mapping */
#define RX_DSCP_PRI_MAP7    0x4c /* Rx DSCP Priority to Rx Packet Mapping */

/* Bit definitions for the CPSW2_CONTROL register */
#define PASS_PRI_TAGGED     (1<<24) /* Pass Priority Tagged */
#define VLAN_LTYPE2_EN      (1<<21) /* VLAN LTYPE 2 enable */
#define VLAN_LTYPE1_EN      (1<<20) /* VLAN LTYPE 1 enable */
#define DSCP_PRI_EN         (1<<16) /* DSCP Priority Enable */
#define TS_320              (1<<14) /* Time Sync Dest Port 320 enable */
#define TS_319              (1<<13) /* Time Sync Dest Port 319 enable */
#define TS_132              (1<<12) /* Time Sync Dest IP Addr 132 enable */
#define TS_131              (1<<11) /* Time Sync Dest IP Addr 131 enable */
#define TS_130              (1<<10) /* Time Sync Dest IP Addr 130 enable */
#define TS_129              (1<<9)  /* Time Sync Dest IP Addr 129 enable */
#define TS_BIT8             (1<<8)  /* ts_ttl_nonzero? */
#define TS_ANNEX_D_EN       (1<<4)  /* Time Sync Annex D enable */
#define TS_LTYPE2_EN        (1<<3)  /* Time Sync LTYPE 2 enable */
#define TS_LTYPE1_EN        (1<<2)  /* Time Sync LTYPE 1 enable */
#define TS_TX_EN            (1<<1)  /* Time Sync Transmit Enable */
#define TS_RX_EN            (1<<0)  /* Time Sync Receive Enable */

#define CTRL_TS_BITS \
	(TS_320 | TS_319 | TS_132 | TS_131 | TS_130 | TS_129 | TS_BIT8 | \
	 TS_ANNEX_D_EN | TS_LTYPE1_EN)

#define CTRL_ALL_TS_MASK (CTRL_TS_BITS | TS_TX_EN | TS_RX_EN)
#define CTRL_TX_TS_BITS  (CTRL_TS_BITS | TS_TX_EN)
#define CTRL_RX_TS_BITS  (CTRL_TS_BITS | TS_RX_EN)

/* Bit definitions for the CPSW2_TS_SEQ_MTYPE register */
#define TS_SEQ_ID_OFFSET_SHIFT   (16)    /* Time Sync Sequence ID Offset */
#define TS_SEQ_ID_OFFSET_MASK    (0x3f)
#define TS_MSG_TYPE_EN_SHIFT     (0)     /* Time Sync Message Type Enable */
#define TS_MSG_TYPE_EN_MASK      (0xffff)

/* The PTP event messages - Sync, Delay_Req, Pdelay_Req, and Pdelay_Resp. */
#define EVENT_MSG_BITS ((1<<0) | (1<<1) | (1<<2) | (1<<3))

/* Bit definitions for the CPSW1_TS_CTL register */
#define CPSW_V1_TS_RX_EN		BIT(0)
#define CPSW_V1_TS_TX_EN		BIT(4)
#define CPSW_V1_MSG_TYPE_OFS		16

/* Bit definitions for the CPSW1_TS_SEQ_LTYPE register */
#define CPSW_V1_SEQ_ID_OFS_SHIFT	16

struct cpsw_host_regs {
	u32	max_blks;
	u32	blk_cnt;
	u32	tx_in_ctl;
	u32	port_vlan;
	u32	tx_pri_map;
	u32	cpdma_tx_pri_map;
	u32	cpdma_rx_chan_map;
};

struct cpsw_sliver_regs {
	u32	id_ver;
	u32	mac_control;
	u32	mac_status;
	u32	soft_reset;
	u32	rx_maxlen;
	u32	__reserved_0;
	u32	rx_pause;
	u32	tx_pause;
	u32	__reserved_1;
	u32	rx_pri_map;
};

struct cpsw_slave {
	void __iomem			*regs;
	struct cpsw_sliver_regs __iomem	*sliver;
	int				slave_num;
	u32				mac_control;
	struct cpsw_slave_data		*data;
	struct phy_device		*phy;
	struct net_device		*ndev;
	u32				port_vlan;
	u32				open_stat;
};

static inline u32 slave_read(struct cpsw_slave *slave, u32 offset)
{
	return __raw_readl(slave->regs + offset);
}

static inline void slave_write(struct cpsw_slave *slave, u32 val, u32 offset)
{
	__raw_writel(val, slave->regs + offset);
}

struct cpsw_priv {
	spinlock_t			lock;
	struct platform_device		*pdev;
	struct net_device		*ndev;
	struct resource			*cpsw_res;
	struct resource			*cpsw_wr_res;
	struct napi_struct		napi;
	struct device			*dev;
	struct cpsw_platform_data	data;
	struct cpsw_ss_regs __iomem	*regs;
	struct cpsw_wr_regs __iomem	*wr_regs;
	struct cpsw_host_regs __iomem	*host_port_regs;
	u32				msg_enable;
	u32				version;
	u32				coal_intvl;
	u32				bus_freq_mhz;
	struct net_device_stats		stats;
	int				rx_packet_max;
	int				host_port;
	struct clk			*clk;
	u8				mac_addr[ETH_ALEN];
	struct cpsw_slave		*slaves;
	struct cpdma_ctlr		*dma;
	struct cpdma_chan		*txch, *rxch;
	struct cpsw_ale			*ale;
	/* snapshot of IRQ numbers */
	u32 irqs_table[4];
	u32 num_irqs;
	bool irq_enabled;
	struct cpts *cpts;
	u32 emac_port;
};

#define napi_to_priv(napi)	container_of(napi, struct cpsw_priv, napi)
#define for_each_slave(priv, func, arg...)				\
	do {								\
		struct cpsw_slave *slave;				\
		int n;							\
		if (priv->data.dual_emac)				\
			(func)((priv)->slaves + priv->emac_port, ##arg);\
		else							\
			for (n = (priv)->data.slaves,			\
					slave = (priv)->slaves;		\
					n; n--)				\
				(func)(slave++, ##arg);			\
	} while (0)
#define cpsw_get_slave_ndev(priv, __slave_no__)				\
	(priv->slaves[__slave_no__].ndev)
#define cpsw_get_slave_priv(priv, __slave_no__)				\
	((priv->slaves[__slave_no__].ndev) ?				\
		netdev_priv(priv->slaves[__slave_no__].ndev) : NULL)	\

#define cpsw_dual_emac_src_port_detect(status, priv, ndev, skb)		\
	do {								\
		if (!priv->data.dual_emac)				\
			break;						\
		if (CPDMA_RX_SOURCE_PORT(status) == 1) {		\
			ndev = cpsw_get_slave_ndev(priv, 0);		\
			priv = netdev_priv(ndev);			\
			skb->dev = ndev;				\
		} else if (CPDMA_RX_SOURCE_PORT(status) == 2) {		\
			ndev = cpsw_get_slave_ndev(priv, 1);		\
			priv = netdev_priv(ndev);			\
			skb->dev = ndev;				\
		}							\
	} while (0)
#define cpsw_add_mcast(priv, addr)					\
	do {								\
		if (priv->data.dual_emac) {				\
			struct cpsw_slave *slave = priv->slaves +	\
						priv->emac_port;	\
			int slave_port = cpsw_get_slave_port(priv,	\
						slave->slave_num);	\
			cpsw_ale_add_mcast(priv->ale, addr,		\
				1 << slave_port | 1 << priv->host_port,	\
				ALE_VLAN, slave->port_vlan, 0);		\
		} else {						\
			cpsw_ale_add_mcast(priv->ale, addr,		\
				ALE_ALL_PORTS << priv->host_port,	\
				0, 0, 0);				\
		}							\
	} while (0)

static inline int cpsw_get_slave_port(struct cpsw_priv *priv, u32 slave_num)
{
	if (priv->host_port == 0)
		return slave_num + 1;
	else
		return slave_num;
}

static void cpsw_ndo_set_rx_mode(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);

	if (ndev->flags & IFF_PROMISC) {
		/* Enable promiscuous mode */
		dev_err(priv->dev, "Ignoring Promiscuous mode\n");
		return;
	}

	/* Clear all mcast from ALE */
	cpsw_ale_flush_multicast(priv->ale, ALE_ALL_PORTS << priv->host_port);

	if (!netdev_mc_empty(ndev)) {
		struct netdev_hw_addr *ha;

		/* program multicast address list into ALE register */
		netdev_for_each_mc_addr(ha, ndev) {
			cpsw_add_mcast(priv, (u8 *)ha->addr);
		}
	}
}

static void cpsw_intr_enable(struct cpsw_priv *priv)
{
	__raw_writel(0xFF, &priv->wr_regs->tx_en);
	__raw_writel(0xFF, &priv->wr_regs->rx_en);

	cpdma_ctlr_int_ctrl(priv->dma, true);
	return;
}

static void cpsw_intr_disable(struct cpsw_priv *priv)
{
	__raw_writel(0, &priv->wr_regs->tx_en);
	__raw_writel(0, &priv->wr_regs->rx_en);

	cpdma_ctlr_int_ctrl(priv->dma, false);
	return;
}

void cpsw_tx_handler(void *token, int len, int status)
{
	struct sk_buff		*skb = token;
	struct net_device	*ndev = skb->dev;
	struct cpsw_priv	*priv = netdev_priv(ndev);

	/* Check whether the queue is stopped due to stalled tx dma, if the
	 * queue is stopped then start the queue as we have free desc for tx
	 */
	if (unlikely(netif_queue_stopped(ndev)))
		netif_wake_queue(ndev);
	cpts_tx_timestamp(priv->cpts, skb);
	priv->stats.tx_packets++;
	priv->stats.tx_bytes += len;
	dev_kfree_skb_any(skb);
}

void cpsw_rx_handler(void *token, int len, int status)
{
	struct sk_buff		*skb = token;
	struct sk_buff		*new_skb;
	struct net_device	*ndev = skb->dev;
	struct cpsw_priv	*priv = netdev_priv(ndev);
	int			ret = 0;

	cpsw_dual_emac_src_port_detect(status, priv, ndev, skb);

	if (unlikely(status < 0)) {
		/* the interface is going down, skbs are purged */
		dev_kfree_skb_any(skb);
		return;
	}

	new_skb = netdev_alloc_skb_ip_align(ndev, priv->rx_packet_max);
	if (new_skb) {
		skb_put(skb, len);
		cpts_rx_timestamp(priv->cpts, skb);
		skb->protocol = eth_type_trans(skb, ndev);
		netif_receive_skb(skb);
		priv->stats.rx_bytes += len;
		priv->stats.rx_packets++;
	} else {
		priv->stats.rx_dropped++;
		new_skb = skb;
	}

	ret = cpdma_chan_submit(priv->rxch, new_skb, new_skb->data,
			skb_tailroom(new_skb), 0);
	if (WARN_ON(ret < 0))
		dev_kfree_skb_any(new_skb);
}

static irqreturn_t cpsw_interrupt(int irq, void *dev_id)
{
	struct cpsw_priv *priv = dev_id;
	u32 rx, tx, rx_thresh;

	rx_thresh = __raw_readl(&priv->wr_regs->rx_thresh_stat);
	rx = __raw_readl(&priv->wr_regs->rx_stat);
	tx = __raw_readl(&priv->wr_regs->tx_stat);
	if (!rx_thresh && !rx && !tx)
		return IRQ_NONE;

	cpsw_intr_disable(priv);
	if (priv->irq_enabled == true) {
		cpsw_disable_irq(priv);
		priv->irq_enabled = false;
	}

	if (netif_running(priv->ndev)) {
		napi_schedule(&priv->napi);
		return IRQ_HANDLED;
	}

	priv = cpsw_get_slave_priv(priv, 1);
	if (!priv)
		return IRQ_NONE;

	if (netif_running(priv->ndev)) {
		napi_schedule(&priv->napi);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int cpsw_poll(struct napi_struct *napi, int budget)
{
	struct cpsw_priv	*priv = napi_to_priv(napi);
	int			num_tx, num_rx;

	num_tx = cpdma_chan_process(priv->txch, 128);
	if (num_tx)
		cpdma_ctlr_eoi(priv->dma, CPDMA_EOI_TX);

	num_rx = cpdma_chan_process(priv->rxch, budget);
	if (num_rx < budget) {
		struct cpsw_priv *prim_cpsw;

		napi_complete(napi);
		cpsw_intr_enable(priv);
		cpdma_ctlr_eoi(priv->dma, CPDMA_EOI_RX);
		prim_cpsw = cpsw_get_slave_priv(priv, 0);
		if (prim_cpsw->irq_enabled == false) {
			prim_cpsw->irq_enabled = true;
			cpsw_enable_irq(priv);
		}
	}

	if (num_rx || num_tx)
		cpsw_dbg(priv, intr, "poll %d rx, %d tx pkts\n",
			 num_rx, num_tx);

	return num_rx;
}

static inline void soft_reset(const char *module, void __iomem *reg)
{
	unsigned long timeout = jiffies + HZ;

	__raw_writel(1, reg);
	do {
		cpu_relax();
	} while ((__raw_readl(reg) & 1) && time_after(timeout, jiffies));

	WARN(__raw_readl(reg) & 1, "failed to soft-reset %s\n", module);
}

#define mac_hi(mac)	(((mac)[0] << 0) | ((mac)[1] << 8) |	\
			 ((mac)[2] << 16) | ((mac)[3] << 24))
#define mac_lo(mac)	(((mac)[4] << 0) | ((mac)[5] << 8))

static void cpsw_set_slave_mac(struct cpsw_slave *slave,
			       struct cpsw_priv *priv)
{
	slave_write(slave, mac_hi(priv->mac_addr), SA_HI);
	slave_write(slave, mac_lo(priv->mac_addr), SA_LO);
}

static void _cpsw_adjust_link(struct cpsw_slave *slave,
			      struct cpsw_priv *priv, bool *link)
{
	struct phy_device	*phy = slave->phy;
	u32			mac_control = 0;
	u32			slave_port;

	if (!phy)
		return;

	slave_port = cpsw_get_slave_port(priv, slave->slave_num);

	if (phy->link) {
		mac_control = priv->data.mac_control;

		/* enable forwarding */
		cpsw_ale_control_set(priv->ale, slave_port,
				     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

		if (phy->speed == 1000)
			mac_control |= BIT(7);	/* GIGABITEN	*/
		if (phy->duplex)
			mac_control |= BIT(0);	/* FULLDUPLEXEN	*/

		/* set speed_in input in case RMII mode is used in 100Mbps */
		if (phy->speed == 100)
			mac_control |= BIT(15);

		*link = true;
	} else {
		mac_control = 0;
		/* disable forwarding */
		cpsw_ale_control_set(priv->ale, slave_port,
				     ALE_PORT_STATE, ALE_PORT_STATE_DISABLE);
	}

	if (mac_control != slave->mac_control) {
		phy_print_status(phy);
		__raw_writel(mac_control, &slave->sliver->mac_control);
	}

	slave->mac_control = mac_control;
}

static void cpsw_adjust_link(struct net_device *ndev)
{
	struct cpsw_priv	*priv = netdev_priv(ndev);
	bool			link = false;

	for_each_slave(priv, _cpsw_adjust_link, priv, &link);

	if (link) {
		netif_carrier_on(ndev);
		if (netif_running(ndev))
			netif_wake_queue(ndev);
	} else {
		netif_carrier_off(ndev);
		netif_stop_queue(ndev);
	}
}

static int cpsw_get_coalesce(struct net_device *ndev,
				struct ethtool_coalesce *coal)
{
	struct cpsw_priv *priv = netdev_priv(ndev);

	coal->rx_coalesce_usecs = priv->coal_intvl;
	return 0;
}

static int cpsw_set_coalesce(struct net_device *ndev,
				struct ethtool_coalesce *coal)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	u32 int_ctrl;
	u32 num_interrupts = 0;
	u32 prescale = 0;
	u32 addnl_dvdr = 1;
	u32 coal_intvl = 0;

	if (!coal->rx_coalesce_usecs)
		return -EINVAL;

	coal_intvl = coal->rx_coalesce_usecs;

	int_ctrl =  readl(&priv->wr_regs->int_control);
	prescale = priv->bus_freq_mhz * 4;

	if (coal_intvl < CPSW_CMINTMIN_INTVL)
		coal_intvl = CPSW_CMINTMIN_INTVL;

	if (coal_intvl > CPSW_CMINTMAX_INTVL) {
		/* Interrupt pacer works with 4us Pulse, we can
		 * throttle further by dilating the 4us pulse.
		 */
		addnl_dvdr = CPSW_INTPRESCALE_MASK / prescale;

		if (addnl_dvdr > 1) {
			prescale *= addnl_dvdr;
			if (coal_intvl > (CPSW_CMINTMAX_INTVL * addnl_dvdr))
				coal_intvl = (CPSW_CMINTMAX_INTVL
						* addnl_dvdr);
		} else {
			addnl_dvdr = 1;
			coal_intvl = CPSW_CMINTMAX_INTVL;
		}
	}

	num_interrupts = (1000 * addnl_dvdr) / coal_intvl;
	writel(num_interrupts, &priv->wr_regs->rx_imax);
	writel(num_interrupts, &priv->wr_regs->tx_imax);

	int_ctrl |= CPSW_INTPACEEN;
	int_ctrl &= (~CPSW_INTPRESCALE_MASK);
	int_ctrl |= (prescale & CPSW_INTPRESCALE_MASK);
	writel(int_ctrl, &priv->wr_regs->int_control);

	cpsw_notice(priv, timer, "Set coalesce to %d usecs.\n", coal_intvl);
	if (priv->data.dual_emac) {
		int i;

		for (i = 0; i < priv->data.slaves; i++) {
			priv = netdev_priv(priv->slaves[i].ndev);
			priv->coal_intvl = coal_intvl;
		}
	} else {
		priv->coal_intvl = coal_intvl;
	}

	return 0;
}

static inline int __show_stat(char *buf, int maxlen, const char *name, u32 val)
{
	static char *leader = "........................................";

	if (!val)
		return 0;
	else
		return snprintf(buf, maxlen, "%s %s %10d\n", name,
				leader + strlen(name), val);
}

static int cpsw_common_res_usage_state(struct cpsw_priv *priv)
{
	u32 i;
	u32 usage_count = 0;

	if (!priv->data.dual_emac)
		return 0;

	for (i = 0; i < priv->data.slaves; i++)
		if (priv->slaves[i].open_stat)
			usage_count++;

	return usage_count;
}

static inline int cpsw_tx_packet_submit(struct net_device *ndev,
			struct cpsw_priv *priv, struct sk_buff *skb)
{
	if (!priv->data.dual_emac)
		return cpdma_chan_submit(priv->txch, skb, skb->data,
				  skb->len, 0);

	if (ndev == cpsw_get_slave_ndev(priv, 0))
		return cpdma_chan_submit(priv->txch, skb, skb->data,
				  skb->len, 1);
	else
		return cpdma_chan_submit(priv->txch, skb, skb->data,
				  skb->len, 2);
}

static inline void cpsw_add_dual_emac_def_ale_entries(
		struct cpsw_priv *priv, struct cpsw_slave *slave,
		u32 slave_port)
{
	u32 port_mask = 1 << slave_port | 1 << priv->host_port;

	if (priv->version == CPSW_VERSION_1)
		slave_write(slave, slave->port_vlan, CPSW1_PORT_VLAN);
	else
		slave_write(slave, slave->port_vlan, CPSW2_PORT_VLAN);
	cpsw_ale_add_vlan(priv->ale, slave->port_vlan, port_mask,
			  port_mask, port_mask, 0);
	cpsw_ale_add_mcast(priv->ale, priv->ndev->broadcast,
			   port_mask, ALE_VLAN, slave->port_vlan, 0);
	cpsw_ale_add_ucast(priv->ale, priv->mac_addr,
		priv->host_port, ALE_VLAN, slave->port_vlan);
}

static void cpsw_slave_open(struct cpsw_slave *slave, struct cpsw_priv *priv)
{
	char name[32];
	u32 slave_port;

	sprintf(name, "slave-%d", slave->slave_num);

	soft_reset(name, &slave->sliver->soft_reset);

	/* setup priority mapping */
	__raw_writel(RX_PRIORITY_MAPPING, &slave->sliver->rx_pri_map);

	switch (priv->version) {
	case CPSW_VERSION_1:
		slave_write(slave, TX_PRIORITY_MAPPING, CPSW1_TX_PRI_MAP);
		break;
	case CPSW_VERSION_2:
		slave_write(slave, TX_PRIORITY_MAPPING, CPSW2_TX_PRI_MAP);
		break;
	}

	/* setup max packet size, and mac address */
	__raw_writel(priv->rx_packet_max, &slave->sliver->rx_maxlen);
	cpsw_set_slave_mac(slave, priv);

	slave->mac_control = 0;	/* no link yet */

	slave_port = cpsw_get_slave_port(priv, slave->slave_num);

	if (priv->data.dual_emac)
		cpsw_add_dual_emac_def_ale_entries(priv, slave, slave_port);
	else
		cpsw_ale_add_mcast(priv->ale, priv->ndev->broadcast,
				   1 << slave_port, 0, 0, ALE_MCAST_FWD_2);

	slave->phy = phy_connect(priv->ndev, slave->data->phy_id,
				 &cpsw_adjust_link, slave->data->phy_if);
	if (IS_ERR(slave->phy)) {
		dev_err(priv->dev, "phy %s not found on slave %d\n",
			slave->data->phy_id, slave->slave_num);
		slave->phy = NULL;
	} else {
		dev_info(priv->dev, "phy found : id is : 0x%x\n",
			 slave->phy->phy_id);
		phy_start(slave->phy);
	}
}

static inline void cpsw_add_default_vlan(struct cpsw_priv *priv)
{
	const int vlan = priv->data.default_vlan;
	const int port = priv->host_port;
	u32 reg;
	int i;

	reg = (priv->version == CPSW_VERSION_1) ? CPSW1_PORT_VLAN :
	       CPSW2_PORT_VLAN;

	writel(vlan, &priv->host_port_regs->port_vlan);

	for (i = 0; i < priv->data.slaves; i++)
		slave_write(priv->slaves + i, vlan, reg);

	cpsw_ale_add_vlan(priv->ale, vlan, ALE_ALL_PORTS << port,
			  ALE_ALL_PORTS << port, ALE_ALL_PORTS << port,
			  (ALE_PORT_1 | ALE_PORT_2) << port);
}

static void cpsw_init_host_port(struct cpsw_priv *priv)
{
	u32 control_reg;
	u32 fifo_mode;

	/* soft reset the controller and initialize ale */
	soft_reset("cpsw", &priv->regs->soft_reset);
	cpsw_ale_start(priv->ale);

	/* switch to vlan unaware mode */
	cpsw_ale_control_set(priv->ale, priv->host_port, ALE_VLAN_AWARE,
			     CPSW_ALE_VLAN_AWARE);
	control_reg = readl(&priv->regs->control);
	control_reg |= CPSW_VLAN_AWARE;
	writel(control_reg, &priv->regs->control);
	fifo_mode = (priv->data.dual_emac) ? CPSW_FIFO_DUAL_MAC_MODE :
		     CPSW_FIFO_NORMAL_MODE;
	writel(fifo_mode, &priv->host_port_regs->tx_in_ctl);

	/* setup host port priority mapping */
	__raw_writel(CPDMA_TX_PRIORITY_MAP,
		     &priv->host_port_regs->cpdma_tx_pri_map);
	__raw_writel(0, &priv->host_port_regs->cpdma_rx_chan_map);

	cpsw_ale_control_set(priv->ale, priv->host_port,
			     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

	if (!priv->data.dual_emac) {
		cpsw_ale_add_ucast(priv->ale, priv->mac_addr, priv->host_port,
				   0, 0);
		cpsw_ale_add_mcast(priv->ale, priv->ndev->broadcast,
				   1 << priv->host_port, 0, 0, ALE_MCAST_FWD_2);
	}
}

static void cpsw_slave_stop(struct cpsw_slave *slave, struct cpsw_priv *priv)
{
	if (!slave->phy)
		return;
	phy_stop(slave->phy);
	phy_disconnect(slave->phy);
	slave->phy = NULL;
}

static int cpsw_ndo_open(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_priv *prim_cpsw;
	int i, ret;
	u32 reg;

	if (!cpsw_common_res_usage_state(priv))
		cpsw_intr_disable(priv);
	netif_carrier_off(ndev);

	pm_runtime_get_sync(&priv->pdev->dev);

	reg = priv->version;

	dev_info(priv->dev, "initializing cpsw version %d.%d (%d)\n",
		 CPSW_MAJOR_VERSION(reg), CPSW_MINOR_VERSION(reg),
		 CPSW_RTL_VERSION(reg));

	/* initialize host and slave ports */
	if (!cpsw_common_res_usage_state(priv))
		cpsw_init_host_port(priv);
	for_each_slave(priv, cpsw_slave_open, priv);

	/* Add default VLAN */
	if (!priv->data.dual_emac)
		cpsw_add_default_vlan(priv);

	if (!cpsw_common_res_usage_state(priv)) {
		/* setup tx dma to fixed prio and zero offset */
		cpdma_control_set(priv->dma, CPDMA_TX_PRIO_FIXED, 1);
		cpdma_control_set(priv->dma, CPDMA_RX_BUFFER_OFFSET, 0);

		/* disable priority elevation */
		__raw_writel(0, &priv->regs->ptype);

		/* enable statistics collection only on all ports */
		__raw_writel(0x7, &priv->regs->stat_port_en);

		if (WARN_ON(!priv->data.rx_descs))
			priv->data.rx_descs = 128;

		for (i = 0; i < priv->data.rx_descs; i++) {
			struct sk_buff *skb;

			ret = -ENOMEM;
			skb = __netdev_alloc_skb_ip_align(priv->ndev,
					priv->rx_packet_max, GFP_KERNEL);
			if (!skb)
				goto err_cleanup;
			ret = cpdma_chan_submit(priv->rxch, skb, skb->data,
					skb_tailroom(skb), 0);
			if (ret < 0) {
				kfree_skb(skb);
				goto err_cleanup;
			}
		}
		/* continue even if we didn't manage to submit all
		 * receive descs
		 */
		cpsw_info(priv, ifup, "submitted %d rx descriptors\n", i);
	}

	/* Enable Interrupt pacing if configured */
	if (priv->coal_intvl != 0) {
		struct ethtool_coalesce coal;

		coal.rx_coalesce_usecs = (priv->coal_intvl << 4);
		cpsw_set_coalesce(ndev, &coal);
	}

	prim_cpsw = cpsw_get_slave_priv(priv, 0);
	if (prim_cpsw->irq_enabled == false) {
		if ((priv == prim_cpsw) || !netif_running(prim_cpsw->ndev)) {
			prim_cpsw->irq_enabled = true;
			cpsw_enable_irq(prim_cpsw);
		}
	}

	cpdma_ctlr_start(priv->dma);
	cpsw_intr_enable(priv);
	napi_enable(&priv->napi);
	cpdma_ctlr_eoi(priv->dma, CPDMA_EOI_RX);
	cpdma_ctlr_eoi(priv->dma, CPDMA_EOI_TX);

	if (priv->data.dual_emac)
		priv->slaves[priv->emac_port].open_stat = true;
	return 0;

err_cleanup:
	cpdma_ctlr_stop(priv->dma);
	for_each_slave(priv, cpsw_slave_stop, priv);
	pm_runtime_put_sync(&priv->pdev->dev);
	netif_carrier_off(priv->ndev);
	return ret;
}

static int cpsw_ndo_stop(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);

	cpsw_info(priv, ifdown, "shutting down cpsw device\n");
	netif_stop_queue(priv->ndev);
	napi_disable(&priv->napi);
	netif_carrier_off(priv->ndev);

	if (cpsw_common_res_usage_state(priv) <= 1) {
		cpsw_intr_disable(priv);
		cpdma_ctlr_int_ctrl(priv->dma, false);
		cpdma_ctlr_stop(priv->dma);
		cpsw_ale_stop(priv->ale);
	}
	for_each_slave(priv, cpsw_slave_stop, priv);
	pm_runtime_put_sync(&priv->pdev->dev);
	if (priv->data.dual_emac)
		priv->slaves[priv->emac_port].open_stat = false;
	return 0;
}

static netdev_tx_t cpsw_ndo_start_xmit(struct sk_buff *skb,
				       struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	int ret;

	ndev->trans_start = jiffies;

	if (skb_padto(skb, CPSW_MIN_PACKET_SIZE)) {
		cpsw_err(priv, tx_err, "packet pad failed\n");
		priv->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
				priv->cpts->tx_enable)
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	skb_tx_timestamp(skb);

	ret = cpsw_tx_packet_submit(ndev, priv, skb);
	if (unlikely(ret != 0)) {
		cpsw_err(priv, tx_err, "desc submit failed\n");
		goto fail;
	}

	/* If there is no more tx desc left free then we need to
	 * tell the kernel to stop sending us tx frames.
	 */
	if (unlikely(!cpdma_check_free_tx_desc(priv->txch)))
		netif_stop_queue(ndev);

	return NETDEV_TX_OK;
fail:
	priv->stats.tx_dropped++;
	netif_stop_queue(ndev);
	return NETDEV_TX_BUSY;
}

static void cpsw_ndo_change_rx_flags(struct net_device *ndev, int flags)
{
	/*
	 * The switch cannot operate in promiscuous mode without substantial
	 * headache.  For promiscuous mode to work, we would need to put the
	 * ALE in bypass mode and route all traffic to the host port.
	 * Subsequently, the host will need to operate as a "bridge", learn,
	 * and flood as needed.  For now, we simply complain here and
	 * do nothing about it :-)
	 */
	if ((flags & IFF_PROMISC) && (ndev->flags & IFF_PROMISC))
		dev_err(&ndev->dev, "promiscuity ignored!\n");

	/*
	 * The switch cannot filter multicast traffic unless it is configured
	 * in "VLAN Aware" mode.  Unfortunately, VLAN awareness requires a
	 * whole bunch of additional logic that this driver does not implement
	 * at present.
	 */
	if ((flags & IFF_ALLMULTI) && !(ndev->flags & IFF_ALLMULTI))
		dev_err(&ndev->dev, "multicast traffic cannot be filtered!\n");
}

#ifdef CONFIG_TI_CPTS

static void cpsw_hwtstamp_v1(struct cpsw_priv *priv)
{
	struct cpsw_slave *slave = &priv->slaves[priv->data.active_slave];
	u32 ts_en, seq_id;

	if (!priv->cpts->tx_enable && !priv->cpts->rx_enable) {
		slave_write(slave, 0, CPSW1_TS_CTL);
		return;
	}

	seq_id = (30 << CPSW_V1_SEQ_ID_OFS_SHIFT) | ETH_P_1588;
	ts_en = EVENT_MSG_BITS << CPSW_V1_MSG_TYPE_OFS;

	if (priv->cpts->tx_enable)
		ts_en |= CPSW_V1_TS_TX_EN;

	if (priv->cpts->rx_enable)
		ts_en |= CPSW_V1_TS_RX_EN;

	slave_write(slave, ts_en, CPSW1_TS_CTL);
	slave_write(slave, seq_id, CPSW1_TS_SEQ_LTYPE);
}

static void cpsw_hwtstamp_v2(struct cpsw_priv *priv)
{
	struct cpsw_slave *slave;
	u32 ctrl, mtype;

	if (priv->data.dual_emac)
		slave = &priv->slaves[priv->emac_port];
	else
		slave = &priv->slaves[priv->data.active_slave];

	ctrl = slave_read(slave, CPSW2_CONTROL);
	ctrl &= ~CTRL_ALL_TS_MASK;

	if (priv->cpts->tx_enable)
		ctrl |= CTRL_TX_TS_BITS;

	if (priv->cpts->rx_enable)
		ctrl |= CTRL_RX_TS_BITS;

	mtype = (30 << TS_SEQ_ID_OFFSET_SHIFT) | EVENT_MSG_BITS;

	slave_write(slave, mtype, CPSW2_TS_SEQ_MTYPE);
	slave_write(slave, ctrl, CPSW2_CONTROL);
	__raw_writel(ETH_P_1588, &priv->regs->ts_ltype);
}

static int cpsw_hwtstamp_ioctl(struct net_device *dev, struct ifreq *ifr)
{
	struct cpsw_priv *priv = netdev_priv(dev);
	struct cpts *cpts = priv->cpts;
	struct hwtstamp_config cfg;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	/* reserved for future extensions */
	if (cfg.flags)
		return -EINVAL;

	switch (cfg.tx_type) {
	case HWTSTAMP_TX_OFF:
		cpts->tx_enable = 0;
		break;
	case HWTSTAMP_TX_ON:
		cpts->tx_enable = 1;
		break;
	default:
		return -ERANGE;
	}

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		cpts->rx_enable = 0;
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		return -ERANGE;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		cpts->rx_enable = 1;
		cfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	default:
		return -ERANGE;
	}

	switch (priv->version) {
	case CPSW_VERSION_1:
		cpsw_hwtstamp_v1(priv);
		break;
	case CPSW_VERSION_2:
		cpsw_hwtstamp_v2(priv);
		break;
	default:
		return -ENOTSUPP;
	}

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

#endif /*CONFIG_TI_CPTS*/

static int cpsw_ndo_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	struct cpsw_priv *priv = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(req);
	int slave_no = cpsw_slave_index(priv);

	if (!netif_running(dev))
		return -EINVAL;

	switch (cmd) {
#ifdef CONFIG_TI_CPTS
	case SIOCSHWTSTAMP:
		return cpsw_hwtstamp_ioctl(dev, req);
#endif
	case SIOCGMIIPHY:
		data->phy_id = priv->slaves[slave_no].phy->addr;
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static void cpsw_ndo_tx_timeout(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);

	cpsw_err(priv, tx_err, "transmit timeout, restarting dma\n");
	priv->stats.tx_errors++;
	cpsw_intr_disable(priv);
	cpdma_ctlr_int_ctrl(priv->dma, false);
	cpdma_chan_stop(priv->txch);
	cpdma_chan_start(priv->txch);
	cpdma_ctlr_int_ctrl(priv->dma, true);
	cpsw_intr_enable(priv);
	cpdma_ctlr_eoi(priv->dma, CPDMA_EOI_RX);
	cpdma_ctlr_eoi(priv->dma, CPDMA_EOI_TX);

}

static struct net_device_stats *cpsw_ndo_get_stats(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	return &priv->stats;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void cpsw_ndo_poll_controller(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);

	cpsw_intr_disable(priv);
	cpdma_ctlr_int_ctrl(priv->dma, false);
	cpsw_interrupt(ndev->irq, priv);
	cpdma_ctlr_int_ctrl(priv->dma, true);
	cpsw_intr_enable(priv);
	cpdma_ctlr_eoi(priv->dma, CPDMA_EOI_RX);
	cpdma_ctlr_eoi(priv->dma, CPDMA_EOI_TX);

}
#endif

static inline int cpsw_add_vlan_ale_entry(struct cpsw_priv *priv,
				unsigned short vid)
{
	int ret;

	ret = cpsw_ale_add_vlan(priv->ale, vid,
				ALE_ALL_PORTS << priv->host_port,
				0, ALE_ALL_PORTS << priv->host_port,
				(ALE_PORT_1 | ALE_PORT_2) << priv->host_port);
	if (ret != 0)
		return ret;

	ret = cpsw_ale_add_ucast(priv->ale, priv->mac_addr,
				 priv->host_port, ALE_VLAN, vid);
	if (ret != 0)
		goto clean_vid;

	ret = cpsw_ale_add_mcast(priv->ale, priv->ndev->broadcast,
				 ALE_ALL_PORTS << priv->host_port,
				 ALE_VLAN, vid, 0);
	if (ret != 0)
		goto clean_vlan_ucast;
	return 0;

clean_vlan_ucast:
	cpsw_ale_del_ucast(priv->ale, priv->mac_addr,
			    priv->host_port, ALE_VLAN, vid);
clean_vid:
	cpsw_ale_del_vlan(priv->ale, vid, 0);
	return ret;
}

static int cpsw_ndo_vlan_rx_add_vid(struct net_device *ndev,
				    __be16 proto, u16 vid)
{
	struct cpsw_priv *priv = netdev_priv(ndev);

	if (vid == priv->data.default_vlan)
		return 0;

	dev_info(priv->dev, "Adding vlanid %d to vlan filter\n", vid);
	return cpsw_add_vlan_ale_entry(priv, vid);
}

static int cpsw_ndo_vlan_rx_kill_vid(struct net_device *ndev,
				     __be16 proto, u16 vid)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	int ret;

	if (vid == priv->data.default_vlan)
		return 0;

	dev_info(priv->dev, "removing vlanid %d from vlan filter\n", vid);
	ret = cpsw_ale_del_vlan(priv->ale, vid, 0);
	if (ret != 0)
		return ret;

	ret = cpsw_ale_del_ucast(priv->ale, priv->mac_addr,
				 priv->host_port, ALE_VLAN, vid);
	if (ret != 0)
		return ret;

	return cpsw_ale_del_mcast(priv->ale, priv->ndev->broadcast,
				  0, ALE_VLAN, vid);
}

static const struct net_device_ops cpsw_netdev_ops = {
	.ndo_open		= cpsw_ndo_open,
	.ndo_stop		= cpsw_ndo_stop,
	.ndo_start_xmit		= cpsw_ndo_start_xmit,
	.ndo_change_rx_flags	= cpsw_ndo_change_rx_flags,
	.ndo_do_ioctl		= cpsw_ndo_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_tx_timeout		= cpsw_ndo_tx_timeout,
	.ndo_get_stats		= cpsw_ndo_get_stats,
	.ndo_set_rx_mode	= cpsw_ndo_set_rx_mode,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= cpsw_ndo_poll_controller,
#endif
	.ndo_vlan_rx_add_vid	= cpsw_ndo_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= cpsw_ndo_vlan_rx_kill_vid,
};

static void cpsw_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	struct cpsw_priv *priv = netdev_priv(ndev);

	strlcpy(info->driver, "TI CPSW Driver v1.0", sizeof(info->driver));
	strlcpy(info->version, "1.0", sizeof(info->version));
	strlcpy(info->bus_info, priv->pdev->name, sizeof(info->bus_info));
}

static u32 cpsw_get_msglevel(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	return priv->msg_enable;
}

static void cpsw_set_msglevel(struct net_device *ndev, u32 value)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	priv->msg_enable = value;
}

static int cpsw_get_ts_info(struct net_device *ndev,
			    struct ethtool_ts_info *info)
{
#ifdef CONFIG_TI_CPTS
	struct cpsw_priv *priv = netdev_priv(ndev);

	info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->phc_index = priv->cpts->phc_index;
	info->tx_types =
		(1 << HWTSTAMP_TX_OFF) |
		(1 << HWTSTAMP_TX_ON);
	info->rx_filters =
		(1 << HWTSTAMP_FILTER_NONE) |
		(1 << HWTSTAMP_FILTER_PTP_V2_EVENT);
#else
	info->so_timestamping =
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE;
	info->phc_index = -1;
	info->tx_types = 0;
	info->rx_filters = 0;
#endif
	return 0;
}

static int cpsw_get_settings(struct net_device *ndev,
			     struct ethtool_cmd *ecmd)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	int slave_no = cpsw_slave_index(priv);

	if (priv->slaves[slave_no].phy)
		return phy_ethtool_gset(priv->slaves[slave_no].phy, ecmd);
	else
		return -EOPNOTSUPP;
}

static int cpsw_set_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	int slave_no = cpsw_slave_index(priv);

	if (priv->slaves[slave_no].phy)
		return phy_ethtool_sset(priv->slaves[slave_no].phy, ecmd);
	else
		return -EOPNOTSUPP;
}

static const struct ethtool_ops cpsw_ethtool_ops = {
	.get_drvinfo	= cpsw_get_drvinfo,
	.get_msglevel	= cpsw_get_msglevel,
	.set_msglevel	= cpsw_set_msglevel,
	.get_link	= ethtool_op_get_link,
	.get_ts_info	= cpsw_get_ts_info,
	.get_settings	= cpsw_get_settings,
	.set_settings	= cpsw_set_settings,
	.get_coalesce	= cpsw_get_coalesce,
	.set_coalesce	= cpsw_set_coalesce,
};

static void cpsw_slave_init(struct cpsw_slave *slave, struct cpsw_priv *priv,
			    u32 slave_reg_ofs, u32 sliver_reg_ofs)
{
	void __iomem		*regs = priv->regs;
	int			slave_num = slave->slave_num;
	struct cpsw_slave_data	*data = priv->data.slave_data + slave_num;

	slave->data	= data;
	slave->regs	= regs + slave_reg_ofs;
	slave->sliver	= regs + sliver_reg_ofs;
	slave->port_vlan = data->dual_emac_res_vlan;
}

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
		pr_err("Missing slaves property in the DT.\n");
		return -EINVAL;
	}
	data->slaves = prop;

	if (of_property_read_u32(node, "active_slave", &prop)) {
		pr_err("Missing active_slave property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->active_slave = prop;

	if (of_property_read_u32(node, "cpts_clock_mult", &prop)) {
		pr_err("Missing cpts_clock_mult property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->cpts_clock_mult = prop;

	if (of_property_read_u32(node, "cpts_clock_shift", &prop)) {
		pr_err("Missing cpts_clock_shift property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->cpts_clock_shift = prop;

	data->slave_data = kcalloc(data->slaves, sizeof(struct cpsw_slave_data),
				   GFP_KERNEL);
	if (!data->slave_data)
		return -EINVAL;

	if (of_property_read_u32(node, "cpdma_channels", &prop)) {
		pr_err("Missing cpdma_channels property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->channels = prop;

	if (of_property_read_u32(node, "ale_entries", &prop)) {
		pr_err("Missing ale_entries property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->ale_entries = prop;

	if (of_property_read_u32(node, "bd_ram_size", &prop)) {
		pr_err("Missing bd_ram_size property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->bd_ram_size = prop;

	if (of_property_read_u32(node, "rx_descs", &prop)) {
		pr_err("Missing rx_descs property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->rx_descs = prop;

	if (of_property_read_u32(node, "mac_control", &prop)) {
		pr_err("Missing mac_control property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->mac_control = prop;

	if (!of_property_read_u32(node, "dual_emac", &prop))
		data->dual_emac = prop;

	/*
	 * Populate all the child nodes here...
	 */
	ret = of_platform_populate(node, NULL, NULL, &pdev->dev);
	/* We do not want to force this, as in some cases may not have child */
	if (ret)
		pr_warn("Doesn't have any child node\n");

	for_each_node_by_name(slave_node, "slave") {
		struct cpsw_slave_data *slave_data = data->slave_data + i;
		const void *mac_addr = NULL;
		u32 phyid;
		int lenp;
		const __be32 *parp;
		struct device_node *mdio_node;
		struct platform_device *mdio;

		parp = of_get_property(slave_node, "phy_id", &lenp);
		if ((parp == NULL) || (lenp != (sizeof(void *) * 2))) {
			pr_err("Missing slave[%d] phy_id property\n", i);
			ret = -EINVAL;
			goto error_ret;
		}
		mdio_node = of_find_node_by_phandle(be32_to_cpup(parp));
		phyid = be32_to_cpup(parp+1);
		mdio = of_find_device_by_node(mdio_node);
		snprintf(slave_data->phy_id, sizeof(slave_data->phy_id),
			 PHY_ID_FMT, mdio->name, phyid);

		mac_addr = of_get_mac_address(slave_node);
		if (mac_addr)
			memcpy(slave_data->mac_addr, mac_addr, ETH_ALEN);

		if (data->dual_emac) {
			if (of_property_read_u32(slave_node, "dual_emac_res_vlan",
						 &prop)) {
				pr_err("Missing dual_emac_res_vlan in DT.\n");
				slave_data->dual_emac_res_vlan = i+1;
				pr_err("Using %d as Reserved VLAN for %d slave\n",
				       slave_data->dual_emac_res_vlan, i);
			} else {
				slave_data->dual_emac_res_vlan = prop;
			}
		}

		i++;
	}

	return 0;

error_ret:
	kfree(data->slave_data);
	return ret;
}

static int cpsw_probe_dual_emac(struct platform_device *pdev,
				struct cpsw_priv *priv)
{
	struct cpsw_platform_data	*data = &priv->data;
	struct net_device		*ndev;
	struct cpsw_priv		*priv_sl2;
	int ret = 0, i;

	ndev = alloc_etherdev(sizeof(struct cpsw_priv));
	if (!ndev) {
		pr_err("cpsw: error allocating net_device\n");
		return -ENOMEM;
	}

	priv_sl2 = netdev_priv(ndev);
	spin_lock_init(&priv_sl2->lock);
	priv_sl2->data = *data;
	priv_sl2->pdev = pdev;
	priv_sl2->ndev = ndev;
	priv_sl2->dev  = &ndev->dev;
	priv_sl2->msg_enable = netif_msg_init(debug_level, CPSW_DEBUG);
	priv_sl2->rx_packet_max = max(rx_packet_max, 128);

	if (is_valid_ether_addr(data->slave_data[1].mac_addr)) {
		memcpy(priv_sl2->mac_addr, data->slave_data[1].mac_addr,
			ETH_ALEN);
		pr_info("cpsw: Detected MACID = %pM\n", priv_sl2->mac_addr);
	} else {
		random_ether_addr(priv_sl2->mac_addr);
		pr_info("cpsw: Random MACID = %pM\n", priv_sl2->mac_addr);
	}
	memcpy(ndev->dev_addr, priv_sl2->mac_addr, ETH_ALEN);

	priv_sl2->slaves = priv->slaves;
	priv_sl2->clk = priv->clk;

	priv_sl2->coal_intvl = 0;
	priv_sl2->bus_freq_mhz = priv->bus_freq_mhz;

	priv_sl2->cpsw_res = priv->cpsw_res;
	priv_sl2->regs = priv->regs;
	priv_sl2->host_port = priv->host_port;
	priv_sl2->host_port_regs = priv->host_port_regs;
	priv_sl2->wr_regs = priv->wr_regs;
	priv_sl2->dma = priv->dma;
	priv_sl2->txch = priv->txch;
	priv_sl2->rxch = priv->rxch;
	priv_sl2->ale = priv->ale;
	priv_sl2->emac_port = 1;
	priv->slaves[1].ndev = ndev;
	priv_sl2->cpts = priv->cpts;
	priv_sl2->version = priv->version;

	for (i = 0; i < priv->num_irqs; i++) {
		priv_sl2->irqs_table[i] = priv->irqs_table[i];
		priv_sl2->num_irqs = priv->num_irqs;
	}
	ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	ndev->netdev_ops = &cpsw_netdev_ops;
	SET_ETHTOOL_OPS(ndev, &cpsw_ethtool_ops);
	netif_napi_add(ndev, &priv_sl2->napi, cpsw_poll, CPSW_POLL_WEIGHT);

	/* register the network device */
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ret = register_netdev(ndev);
	if (ret) {
		pr_err("cpsw: error registering net device\n");
		free_netdev(ndev);
		ret = -ENODEV;
	}

	return ret;
}

static int cpsw_probe(struct platform_device *pdev)
{
	struct cpsw_platform_data	*data;
	struct net_device		*ndev;
	struct cpsw_priv		*priv;
	struct cpdma_params		dma_params;
	struct cpsw_ale_params		ale_params;
	void __iomem			*ss_regs, *wr_regs;
	struct resource			*res;
	u32 slave_offset, sliver_offset, slave_size;
	int ret = 0, i, k = 0;

	ndev = alloc_etherdev(sizeof(struct cpsw_priv));
	if (!ndev) {
		pr_err("error allocating net_device\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, ndev);
	priv = netdev_priv(ndev);
	spin_lock_init(&priv->lock);
	priv->pdev = pdev;
	priv->ndev = ndev;
	priv->dev  = &ndev->dev;
	priv->msg_enable = netif_msg_init(debug_level, CPSW_DEBUG);
	priv->rx_packet_max = max(rx_packet_max, 128);
	priv->cpts = devm_kzalloc(&pdev->dev, sizeof(struct cpts), GFP_KERNEL);
	priv->irq_enabled = true;
	if (!priv->cpts) {
		pr_err("error allocating cpts\n");
		goto clean_ndev_ret;
	}

	/*
	 * This may be required here for child devices.
	 */
	pm_runtime_enable(&pdev->dev);

	if (cpsw_probe_dt(&priv->data, pdev)) {
		pr_err("cpsw: platform data missing\n");
		ret = -ENODEV;
		goto clean_ndev_ret;
	}
	data = &priv->data;

	if (is_valid_ether_addr(data->slave_data[0].mac_addr)) {
		memcpy(priv->mac_addr, data->slave_data[0].mac_addr, ETH_ALEN);
		pr_info("Detected MACID = %pM", priv->mac_addr);
	} else {
		eth_random_addr(priv->mac_addr);
		pr_info("Random MACID = %pM", priv->mac_addr);
	}

	memcpy(ndev->dev_addr, priv->mac_addr, ETH_ALEN);

	priv->slaves = kzalloc(sizeof(struct cpsw_slave) * data->slaves,
			       GFP_KERNEL);
	if (!priv->slaves) {
		ret = -EBUSY;
		goto clean_ndev_ret;
	}
	for (i = 0; i < data->slaves; i++)
		priv->slaves[i].slave_num = i;

	priv->slaves[0].ndev = ndev;
	priv->emac_port = 0;

	priv->clk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "fck is not found\n");
		ret = -ENODEV;
		goto clean_slave_ret;
	}
	priv->coal_intvl = 0;
	priv->bus_freq_mhz = clk_get_rate(priv->clk) / 1000000;

	priv->cpsw_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!priv->cpsw_res) {
		dev_err(priv->dev, "error getting i/o resource\n");
		ret = -ENOENT;
		goto clean_clk_ret;
	}
	if (!request_mem_region(priv->cpsw_res->start,
				resource_size(priv->cpsw_res), ndev->name)) {
		dev_err(priv->dev, "failed request i/o region\n");
		ret = -ENXIO;
		goto clean_clk_ret;
	}
	ss_regs = ioremap(priv->cpsw_res->start, resource_size(priv->cpsw_res));
	if (!ss_regs) {
		dev_err(priv->dev, "unable to map i/o region\n");
		goto clean_cpsw_iores_ret;
	}
	priv->regs = ss_regs;
	priv->version = __raw_readl(&priv->regs->id_ver);
	priv->host_port = HOST_PORT_NUM;

	priv->cpsw_wr_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!priv->cpsw_wr_res) {
		dev_err(priv->dev, "error getting i/o resource\n");
		ret = -ENOENT;
		goto clean_iomap_ret;
	}
	if (!request_mem_region(priv->cpsw_wr_res->start,
			resource_size(priv->cpsw_wr_res), ndev->name)) {
		dev_err(priv->dev, "failed request i/o region\n");
		ret = -ENXIO;
		goto clean_iomap_ret;
	}
	wr_regs = ioremap(priv->cpsw_wr_res->start,
				resource_size(priv->cpsw_wr_res));
	if (!wr_regs) {
		dev_err(priv->dev, "unable to map i/o region\n");
		goto clean_cpsw_wr_iores_ret;
	}
	priv->wr_regs = wr_regs;

	memset(&dma_params, 0, sizeof(dma_params));
	memset(&ale_params, 0, sizeof(ale_params));

	switch (priv->version) {
	case CPSW_VERSION_1:
		priv->host_port_regs = ss_regs + CPSW1_HOST_PORT_OFFSET;
		priv->cpts->reg       = ss_regs + CPSW1_CPTS_OFFSET;
		dma_params.dmaregs   = ss_regs + CPSW1_CPDMA_OFFSET;
		dma_params.txhdp     = ss_regs + CPSW1_STATERAM_OFFSET;
		ale_params.ale_regs  = ss_regs + CPSW1_ALE_OFFSET;
		slave_offset         = CPSW1_SLAVE_OFFSET;
		slave_size           = CPSW1_SLAVE_SIZE;
		sliver_offset        = CPSW1_SLIVER_OFFSET;
		dma_params.desc_mem_phys = 0;
		break;
	case CPSW_VERSION_2:
		priv->host_port_regs = ss_regs + CPSW2_HOST_PORT_OFFSET;
		priv->cpts->reg       = ss_regs + CPSW2_CPTS_OFFSET;
		dma_params.dmaregs   = ss_regs + CPSW2_CPDMA_OFFSET;
		dma_params.txhdp     = ss_regs + CPSW2_STATERAM_OFFSET;
		ale_params.ale_regs  = ss_regs + CPSW2_ALE_OFFSET;
		slave_offset         = CPSW2_SLAVE_OFFSET;
		slave_size           = CPSW2_SLAVE_SIZE;
		sliver_offset        = CPSW2_SLIVER_OFFSET;
		dma_params.desc_mem_phys =
			(u32 __force) priv->cpsw_res->start + CPSW2_BD_OFFSET;
		break;
	default:
		dev_err(priv->dev, "unknown version 0x%08x\n", priv->version);
		ret = -ENODEV;
		goto clean_cpsw_wr_iores_ret;
	}
	for (i = 0; i < priv->data.slaves; i++) {
		struct cpsw_slave *slave = &priv->slaves[i];
		cpsw_slave_init(slave, priv, slave_offset, sliver_offset);
		slave_offset  += slave_size;
		sliver_offset += SLIVER_SIZE;
	}

	dma_params.dev		= &pdev->dev;
	dma_params.rxthresh	= dma_params.dmaregs + CPDMA_RXTHRESH;
	dma_params.rxfree	= dma_params.dmaregs + CPDMA_RXFREE;
	dma_params.rxhdp	= dma_params.txhdp + CPDMA_RXHDP;
	dma_params.txcp		= dma_params.txhdp + CPDMA_TXCP;
	dma_params.rxcp		= dma_params.txhdp + CPDMA_RXCP;

	dma_params.num_chan		= data->channels;
	dma_params.has_soft_reset	= true;
	dma_params.min_packet_size	= CPSW_MIN_PACKET_SIZE;
	dma_params.desc_mem_size	= data->bd_ram_size;
	dma_params.desc_align		= 16;
	dma_params.has_ext_regs		= true;
	dma_params.desc_hw_addr         = dma_params.desc_mem_phys;

	priv->dma = cpdma_ctlr_create(&dma_params);
	if (!priv->dma) {
		dev_err(priv->dev, "error initializing dma\n");
		ret = -ENOMEM;
		goto clean_wr_iomap_ret;
	}

	priv->txch = cpdma_chan_create(priv->dma, tx_chan_num(0),
				       cpsw_tx_handler);
	priv->rxch = cpdma_chan_create(priv->dma, rx_chan_num(0),
				       cpsw_rx_handler);

	if (WARN_ON(!priv->txch || !priv->rxch)) {
		dev_err(priv->dev, "error initializing dma channels\n");
		ret = -ENOMEM;
		goto clean_dma_ret;
	}

	ale_params.dev			= &ndev->dev;
	ale_params.ale_ageout		= ale_ageout;
	ale_params.ale_entries		= data->ale_entries;
	ale_params.ale_ports		= data->slaves;

	priv->ale = cpsw_ale_create(&ale_params);
	if (!priv->ale) {
		dev_err(priv->dev, "error initializing ale engine\n");
		ret = -ENODEV;
		goto clean_dma_ret;
	}

	ndev->irq = platform_get_irq(pdev, 0);
	if (ndev->irq < 0) {
		dev_err(priv->dev, "error getting irq resource\n");
		ret = -ENOENT;
		goto clean_ale_ret;
	}

	while ((res = platform_get_resource(priv->pdev, IORESOURCE_IRQ, k))) {
		for (i = res->start; i <= res->end; i++) {
			if (request_irq(i, cpsw_interrupt, IRQF_DISABLED,
					dev_name(&pdev->dev), priv)) {
				dev_err(priv->dev, "error attaching irq\n");
				goto clean_ale_ret;
			}
			priv->irqs_table[k] = i;
			priv->num_irqs = k + 1;
		}
		k++;
	}

	ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	ndev->netdev_ops = &cpsw_netdev_ops;
	SET_ETHTOOL_OPS(ndev, &cpsw_ethtool_ops);
	netif_napi_add(ndev, &priv->napi, cpsw_poll, CPSW_POLL_WEIGHT);

	/* register the network device */
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ret = register_netdev(ndev);
	if (ret) {
		dev_err(priv->dev, "error registering net device\n");
		ret = -ENODEV;
		goto clean_irq_ret;
	}

	if (cpts_register(&pdev->dev, priv->cpts,
			  data->cpts_clock_mult, data->cpts_clock_shift))
		dev_err(priv->dev, "error registering cpts device\n");

	cpsw_notice(priv, probe, "initialized device (regs %x, irq %d)\n",
		  priv->cpsw_res->start, ndev->irq);

	if (priv->data.dual_emac) {
		ret = cpsw_probe_dual_emac(pdev, priv);
		if (ret) {
			cpsw_err(priv, probe, "error probe slave 2 emac interface\n");
			goto clean_irq_ret;
		}
	}

	return 0;

clean_irq_ret:
	for (i = 0; i < priv->num_irqs; i++)
		free_irq(priv->irqs_table[i], priv);
clean_ale_ret:
	cpsw_ale_destroy(priv->ale);
clean_dma_ret:
	cpdma_chan_destroy(priv->txch);
	cpdma_chan_destroy(priv->rxch);
	cpdma_ctlr_destroy(priv->dma);
clean_wr_iomap_ret:
	iounmap(priv->wr_regs);
clean_cpsw_wr_iores_ret:
	release_mem_region(priv->cpsw_wr_res->start,
			   resource_size(priv->cpsw_wr_res));
clean_iomap_ret:
	iounmap(priv->regs);
clean_cpsw_iores_ret:
	release_mem_region(priv->cpsw_res->start,
			   resource_size(priv->cpsw_res));
clean_clk_ret:
	clk_put(priv->clk);
clean_slave_ret:
	pm_runtime_disable(&pdev->dev);
	kfree(priv->slaves);
clean_ndev_ret:
	kfree(priv->data.slave_data);
	free_netdev(priv->ndev);
	return ret;
}

static int cpsw_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct cpsw_priv *priv = netdev_priv(ndev);
	int i;

	platform_set_drvdata(pdev, NULL);
	if (priv->data.dual_emac)
		unregister_netdev(cpsw_get_slave_ndev(priv, 1));
	unregister_netdev(ndev);

	cpts_unregister(priv->cpts);
	for (i = 0; i < priv->num_irqs; i++)
		free_irq(priv->irqs_table[i], priv);

	cpsw_ale_destroy(priv->ale);
	cpdma_chan_destroy(priv->txch);
	cpdma_chan_destroy(priv->rxch);
	cpdma_ctlr_destroy(priv->dma);
	iounmap(priv->regs);
	release_mem_region(priv->cpsw_res->start,
			   resource_size(priv->cpsw_res));
	iounmap(priv->wr_regs);
	release_mem_region(priv->cpsw_wr_res->start,
			   resource_size(priv->cpsw_wr_res));
	pm_runtime_disable(&pdev->dev);
	clk_put(priv->clk);
	kfree(priv->slaves);
	kfree(priv->data.slave_data);
	if (priv->data.dual_emac)
		free_netdev(cpsw_get_slave_ndev(priv, 1));
	free_netdev(ndev);
	return 0;
}

static int cpsw_suspend(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct net_device	*ndev = platform_get_drvdata(pdev);

	if (netif_running(ndev))
		cpsw_ndo_stop(ndev);
	pm_runtime_put_sync(&pdev->dev);

	return 0;
}

static int cpsw_resume(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct net_device	*ndev = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);
	if (netif_running(ndev))
		cpsw_ndo_open(ndev);
	return 0;
}

static const struct dev_pm_ops cpsw_pm_ops = {
	.suspend	= cpsw_suspend,
	.resume		= cpsw_resume,
};

static const struct of_device_id cpsw_of_mtable[] = {
	{ .compatible = "ti,cpsw", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cpsw_of_mtable);

static struct platform_driver cpsw_driver = {
	.driver = {
		.name	 = "cpsw",
		.owner	 = THIS_MODULE,
		.pm	 = &cpsw_pm_ops,
		.of_match_table = of_match_ptr(cpsw_of_mtable),
	},
	.probe = cpsw_probe,
	.remove = cpsw_remove,
};

static int __init cpsw_init(void)
{
	return platform_driver_register(&cpsw_driver);
}
late_initcall(cpsw_init);

static void __exit cpsw_exit(void)
{
	platform_driver_unregister(&cpsw_driver);
}
module_exit(cpsw_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cyril Chemparathy <cyril@ti.com>");
MODULE_AUTHOR("Mugunthan V N <mugunthanvnm@ti.com>");
MODULE_DESCRIPTION("TI CPSW Ethernet driver");
