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
#define CPDMA_RXTHRESH		0x0c0
#define CPDMA_RXFREE		0x0e0
#define CPDMA_TXHDP		0x00
#define CPDMA_RXHDP		0x20
#define CPDMA_TXCP		0x40
#define CPDMA_RXCP		0x60

#define cpsw_dma_regs(base, offset)		\
	(void __iomem *)((base) + (offset))
#define cpsw_dma_rxthresh(base, offset)		\
	(void __iomem *)((base) + (offset) + CPDMA_RXTHRESH)
#define cpsw_dma_rxfree(base, offset)		\
	(void __iomem *)((base) + (offset) + CPDMA_RXFREE)
#define cpsw_dma_txhdp(base, offset)		\
	(void __iomem *)((base) + (offset) + CPDMA_TXHDP)
#define cpsw_dma_rxhdp(base, offset)		\
	(void __iomem *)((base) + (offset) + CPDMA_RXHDP)
#define cpsw_dma_txcp(base, offset)		\
	(void __iomem *)((base) + (offset) + CPDMA_TXCP)
#define cpsw_dma_rxcp(base, offset)		\
	(void __iomem *)((base) + (offset) + CPDMA_RXCP)

#define CPSW_POLL_WEIGHT	64
#define CPSW_MIN_PACKET_SIZE	60
#define CPSW_MAX_PACKET_SIZE	(1500 + 14 + 4 + 4)

#define RX_PRIORITY_MAPPING	0x76543210
#define TX_PRIORITY_MAPPING	0x33221100
#define CPDMA_TX_PRIORITY_MAP	0x76543210

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
	u32	flow_thresh;
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
	struct cpts cpts;
};

#define napi_to_priv(napi)	container_of(napi, struct cpsw_priv, napi)
#define for_each_slave(priv, func, arg...)			\
	do {							\
		int idx;					\
		for (idx = 0; idx < (priv)->data.slaves; idx++)	\
			(func)((priv)->slaves + idx, ##arg);	\
	} while (0)

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
			cpsw_ale_add_mcast(priv->ale, (u8 *)ha->addr,
				ALE_ALL_PORTS << priv->host_port, 0, 0);
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

	if (unlikely(netif_queue_stopped(ndev)))
		netif_start_queue(ndev);
	cpts_tx_timestamp(&priv->cpts, skb);
	priv->stats.tx_packets++;
	priv->stats.tx_bytes += len;
	dev_kfree_skb_any(skb);
}

void cpsw_rx_handler(void *token, int len, int status)
{
	struct sk_buff		*skb = token;
	struct net_device	*ndev = skb->dev;
	struct cpsw_priv	*priv = netdev_priv(ndev);
	int			ret = 0;

	/* free and bail if we are shutting down */
	if (unlikely(!netif_running(ndev)) ||
			unlikely(!netif_carrier_ok(ndev))) {
		dev_kfree_skb_any(skb);
		return;
	}
	if (likely(status >= 0)) {
		skb_put(skb, len);
		cpts_rx_timestamp(&priv->cpts, skb);
		skb->protocol = eth_type_trans(skb, ndev);
		netif_receive_skb(skb);
		priv->stats.rx_bytes += len;
		priv->stats.rx_packets++;
		skb = NULL;
	}

	if (unlikely(!netif_running(ndev))) {
		if (skb)
			dev_kfree_skb_any(skb);
		return;
	}

	if (likely(!skb)) {
		skb = netdev_alloc_skb_ip_align(ndev, priv->rx_packet_max);
		if (WARN_ON(!skb))
			return;

		ret = cpdma_chan_submit(priv->rxch, skb, skb->data,
					skb_tailroom(skb), GFP_KERNEL);
	}
	WARN_ON(ret < 0);
}

static irqreturn_t cpsw_interrupt(int irq, void *dev_id)
{
	struct cpsw_priv *priv = dev_id;

	if (likely(netif_running(priv->ndev))) {
		cpsw_intr_disable(priv);
		cpsw_disable_irq(priv);
		napi_schedule(&priv->napi);
	}
	return IRQ_HANDLED;
}

static inline int cpsw_get_slave_port(struct cpsw_priv *priv, u32 slave_num)
{
	if (priv->host_port == 0)
		return slave_num + 1;
	else
		return slave_num;
}

static int cpsw_poll(struct napi_struct *napi, int budget)
{
	struct cpsw_priv	*priv = napi_to_priv(napi);
	int			num_tx, num_rx;

	num_tx = cpdma_chan_process(priv->txch, 128);
	num_rx = cpdma_chan_process(priv->rxch, budget);

	if (num_rx || num_tx)
		cpsw_dbg(priv, intr, "poll %d rx, %d tx pkts\n",
			 num_rx, num_tx);

	if (num_rx < budget) {
		napi_complete(napi);
		cpsw_intr_enable(priv);
		cpdma_ctlr_eoi(priv->dma);
		cpsw_enable_irq(priv);
	}

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

static inline int __show_stat(char *buf, int maxlen, const char *name, u32 val)
{
	static char *leader = "........................................";

	if (!val)
		return 0;
	else
		return snprintf(buf, maxlen, "%s %s %10d\n", name,
				leader + strlen(name), val);
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

	cpsw_ale_add_mcast(priv->ale, priv->ndev->broadcast,
			   1 << slave_port, 0, ALE_MCAST_FWD_2);

	slave->phy = phy_connect(priv->ndev, slave->data->phy_id,
				 &cpsw_adjust_link, 0, slave->data->phy_if);
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

static void cpsw_init_host_port(struct cpsw_priv *priv)
{
	/* soft reset the controller and initialize ale */
	soft_reset("cpsw", &priv->regs->soft_reset);
	cpsw_ale_start(priv->ale);

	/* switch to vlan unaware mode */
	cpsw_ale_control_set(priv->ale, 0, ALE_VLAN_AWARE, 0);

	/* setup host port priority mapping */
	__raw_writel(CPDMA_TX_PRIORITY_MAP,
		     &priv->host_port_regs->cpdma_tx_pri_map);
	__raw_writel(0, &priv->host_port_regs->cpdma_rx_chan_map);

	cpsw_ale_control_set(priv->ale, priv->host_port,
			     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

	cpsw_ale_add_ucast(priv->ale, priv->mac_addr, priv->host_port, 0);
	cpsw_ale_add_mcast(priv->ale, priv->ndev->broadcast,
			   1 << priv->host_port, 0, ALE_MCAST_FWD_2);
}

static int cpsw_ndo_open(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	int i, ret;
	u32 reg;

	cpsw_intr_disable(priv);
	netif_carrier_off(ndev);

	pm_runtime_get_sync(&priv->pdev->dev);

	reg = __raw_readl(&priv->regs->id_ver);
	priv->version = reg;

	dev_info(priv->dev, "initializing cpsw version %d.%d (%d)\n",
		 CPSW_MAJOR_VERSION(reg), CPSW_MINOR_VERSION(reg),
		 CPSW_RTL_VERSION(reg));

	/* initialize host and slave ports */
	cpsw_init_host_port(priv);
	for_each_slave(priv, cpsw_slave_open, priv);

	/* setup tx dma to fixed prio and zero offset */
	cpdma_control_set(priv->dma, CPDMA_TX_PRIO_FIXED, 1);
	cpdma_control_set(priv->dma, CPDMA_RX_BUFFER_OFFSET, 0);

	/* disable priority elevation and enable statistics on all ports */
	__raw_writel(0, &priv->regs->ptype);

	/* enable statistics collection only on the host port */
	__raw_writel(0x7, &priv->regs->stat_port_en);

	if (WARN_ON(!priv->data.rx_descs))
		priv->data.rx_descs = 128;

	for (i = 0; i < priv->data.rx_descs; i++) {
		struct sk_buff *skb;

		ret = -ENOMEM;
		skb = netdev_alloc_skb_ip_align(priv->ndev,
						priv->rx_packet_max);
		if (!skb)
			break;
		ret = cpdma_chan_submit(priv->rxch, skb, skb->data,
					skb_tailroom(skb), GFP_KERNEL);
		if (WARN_ON(ret < 0))
			break;
	}
	/* continue even if we didn't manage to submit all receive descs */
	cpsw_info(priv, ifup, "submitted %d rx descriptors\n", i);

	cpdma_ctlr_start(priv->dma);
	cpsw_intr_enable(priv);
	napi_enable(&priv->napi);
	cpdma_ctlr_eoi(priv->dma);

	return 0;
}

static void cpsw_slave_stop(struct cpsw_slave *slave, struct cpsw_priv *priv)
{
	if (!slave->phy)
		return;
	phy_stop(slave->phy);
	phy_disconnect(slave->phy);
	slave->phy = NULL;
}

static int cpsw_ndo_stop(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);

	cpsw_info(priv, ifdown, "shutting down cpsw device\n");
	cpsw_intr_disable(priv);
	cpdma_ctlr_int_ctrl(priv->dma, false);
	cpdma_ctlr_stop(priv->dma);
	netif_stop_queue(priv->ndev);
	napi_disable(&priv->napi);
	netif_carrier_off(priv->ndev);
	cpsw_ale_stop(priv->ale);
	for_each_slave(priv, cpsw_slave_stop, priv);
	pm_runtime_put_sync(&priv->pdev->dev);
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

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP && priv->cpts.tx_enable)
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	skb_tx_timestamp(skb);

	ret = cpdma_chan_submit(priv->txch, skb, skb->data,
				skb->len, GFP_KERNEL);
	if (unlikely(ret != 0)) {
		cpsw_err(priv, tx_err, "desc submit failed\n");
		goto fail;
	}

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
	struct cpsw_slave *slave = &priv->slaves[priv->data.cpts_active_slave];
	u32 ts_en, seq_id;

	if (!priv->cpts.tx_enable && !priv->cpts.rx_enable) {
		slave_write(slave, 0, CPSW1_TS_CTL);
		return;
	}

	seq_id = (30 << CPSW_V1_SEQ_ID_OFS_SHIFT) | ETH_P_1588;
	ts_en = EVENT_MSG_BITS << CPSW_V1_MSG_TYPE_OFS;

	if (priv->cpts.tx_enable)
		ts_en |= CPSW_V1_TS_TX_EN;

	if (priv->cpts.rx_enable)
		ts_en |= CPSW_V1_TS_RX_EN;

	slave_write(slave, ts_en, CPSW1_TS_CTL);
	slave_write(slave, seq_id, CPSW1_TS_SEQ_LTYPE);
}

static void cpsw_hwtstamp_v2(struct cpsw_priv *priv)
{
	struct cpsw_slave *slave = &priv->slaves[priv->data.cpts_active_slave];
	u32 ctrl, mtype;

	ctrl = slave_read(slave, CPSW2_CONTROL);
	ctrl &= ~CTRL_ALL_TS_MASK;

	if (priv->cpts.tx_enable)
		ctrl |= CTRL_TX_TS_BITS;

	if (priv->cpts.rx_enable)
		ctrl |= CTRL_RX_TS_BITS;

	mtype = (30 << TS_SEQ_ID_OFFSET_SHIFT) | EVENT_MSG_BITS;

	slave_write(slave, mtype, CPSW2_TS_SEQ_MTYPE);
	slave_write(slave, ctrl, CPSW2_CONTROL);
	__raw_writel(ETH_P_1588, &priv->regs->ts_ltype);
}

static int cpsw_hwtstamp_ioctl(struct cpsw_priv *priv, struct ifreq *ifr)
{
	struct cpts *cpts = &priv->cpts;
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

	if (!netif_running(dev))
		return -EINVAL;

#ifdef CONFIG_TI_CPTS
	if (cmd == SIOCSHWTSTAMP)
		return cpsw_hwtstamp_ioctl(priv, req);
#endif
	return -ENOTSUPP;
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
	cpdma_ctlr_eoi(priv->dma);
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
	cpdma_ctlr_eoi(priv->dma);
}
#endif

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
};

static void cpsw_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	strcpy(info->driver, "TI CPSW Driver v1.0");
	strcpy(info->version, "1.0");
	strcpy(info->bus_info, priv->pdev->name);
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
	info->phc_index = priv->cpts.phc_index;
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

static const struct ethtool_ops cpsw_ethtool_ops = {
	.get_drvinfo	= cpsw_get_drvinfo,
	.get_msglevel	= cpsw_get_msglevel,
	.set_msglevel	= cpsw_set_msglevel,
	.get_link	= ethtool_op_get_link,
	.get_ts_info	= cpsw_get_ts_info,
};

static void cpsw_slave_init(struct cpsw_slave *slave, struct cpsw_priv *priv)
{
	void __iomem		*regs = priv->regs;
	int			slave_num = slave->slave_num;
	struct cpsw_slave_data	*data = priv->data.slave_data + slave_num;

	slave->data	= data;
	slave->regs	= regs + data->slave_reg_ofs;
	slave->sliver	= regs + data->sliver_reg_ofs;
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

	if (of_property_read_u32(node, "cpts_active_slave", &prop)) {
		pr_err("Missing cpts_active_slave property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->cpts_active_slave = prop;

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

	data->slave_data = kzalloc(sizeof(struct cpsw_slave_data) *
				   data->slaves, GFP_KERNEL);
	if (!data->slave_data) {
		pr_err("Could not allocate slave memory.\n");
		return -EINVAL;
	}

	data->no_bd_ram = of_property_read_bool(node, "no_bd_ram");

	if (of_property_read_u32(node, "cpdma_channels", &prop)) {
		pr_err("Missing cpdma_channels property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->channels = prop;

	if (of_property_read_u32(node, "host_port_no", &prop)) {
		pr_err("Missing host_port_no property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->host_port_num = prop;

	if (of_property_read_u32(node, "cpdma_reg_ofs", &prop)) {
		pr_err("Missing cpdma_reg_ofs property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->cpdma_reg_ofs = prop;

	if (of_property_read_u32(node, "cpdma_sram_ofs", &prop)) {
		pr_err("Missing cpdma_sram_ofs property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->cpdma_sram_ofs = prop;

	if (of_property_read_u32(node, "ale_reg_ofs", &prop)) {
		pr_err("Missing ale_reg_ofs property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->ale_reg_ofs = prop;

	if (of_property_read_u32(node, "ale_entries", &prop)) {
		pr_err("Missing ale_entries property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->ale_entries = prop;

	if (of_property_read_u32(node, "host_port_reg_ofs", &prop)) {
		pr_err("Missing host_port_reg_ofs property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->host_port_reg_ofs = prop;

	if (of_property_read_u32(node, "hw_stats_reg_ofs", &prop)) {
		pr_err("Missing hw_stats_reg_ofs property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->hw_stats_reg_ofs = prop;

	if (of_property_read_u32(node, "cpts_reg_ofs", &prop)) {
		pr_err("Missing cpts_reg_ofs property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->cpts_reg_ofs = prop;

	if (of_property_read_u32(node, "bd_ram_ofs", &prop)) {
		pr_err("Missing bd_ram_ofs property in the DT.\n");
		ret = -EINVAL;
		goto error_ret;
	}
	data->bd_ram_ofs = prop;

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

	for_each_node_by_name(slave_node, "slave") {
		struct cpsw_slave_data *slave_data = data->slave_data + i;
		const char *phy_id = NULL;
		const void *mac_addr = NULL;

		if (of_property_read_string(slave_node, "phy_id", &phy_id)) {
			pr_err("Missing slave[%d] phy_id property\n", i);
			ret = -EINVAL;
			goto error_ret;
		}
		slave_data->phy_id = phy_id;

		if (of_property_read_u32(slave_node, "slave_reg_ofs", &prop)) {
			pr_err("Missing slave[%d] slave_reg_ofs property\n", i);
			ret = -EINVAL;
			goto error_ret;
		}
		slave_data->slave_reg_ofs = prop;

		if (of_property_read_u32(slave_node, "sliver_reg_ofs",
					 &prop)) {
			pr_err("Missing slave[%d] sliver_reg_ofs property\n",
				i);
			ret = -EINVAL;
			goto error_ret;
		}
		slave_data->sliver_reg_ofs = prop;

		mac_addr = of_get_mac_address(slave_node);
		if (mac_addr)
			memcpy(slave_data->mac_addr, mac_addr, ETH_ALEN);

		i++;
	}

	/*
	 * Populate all the child nodes here...
	 */
	ret = of_platform_populate(node, NULL, NULL, &pdev->dev);
	/* We do not want to force this, as in some cases may not have child */
	if (ret)
		pr_warn("Doesn't have any child node\n");

	return 0;

error_ret:
	kfree(data->slave_data);
	return ret;
}

static int __devinit cpsw_probe(struct platform_device *pdev)
{
	struct cpsw_platform_data	*data = pdev->dev.platform_data;
	struct net_device		*ndev;
	struct cpsw_priv		*priv;
	struct cpdma_params		dma_params;
	struct cpsw_ale_params		ale_params;
	void __iomem			*regs;
	struct resource			*res;
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

	priv->clk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "fck is not found\n");
		ret = -ENODEV;
		goto clean_slave_ret;
	}

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
	regs = ioremap(priv->cpsw_res->start, resource_size(priv->cpsw_res));
	if (!regs) {
		dev_err(priv->dev, "unable to map i/o region\n");
		goto clean_cpsw_iores_ret;
	}
	priv->regs = regs;
	priv->host_port = data->host_port_num;
	priv->host_port_regs = regs + data->host_port_reg_ofs;
	priv->cpts.reg = regs + data->cpts_reg_ofs;

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
	regs = ioremap(priv->cpsw_wr_res->start,
				resource_size(priv->cpsw_wr_res));
	if (!regs) {
		dev_err(priv->dev, "unable to map i/o region\n");
		goto clean_cpsw_wr_iores_ret;
	}
	priv->wr_regs = regs;

	for_each_slave(priv, cpsw_slave_init, priv);

	memset(&dma_params, 0, sizeof(dma_params));
	dma_params.dev		= &pdev->dev;
	dma_params.dmaregs	= cpsw_dma_regs((u32)priv->regs,
						data->cpdma_reg_ofs);
	dma_params.rxthresh	= cpsw_dma_rxthresh((u32)priv->regs,
						    data->cpdma_reg_ofs);
	dma_params.rxfree	= cpsw_dma_rxfree((u32)priv->regs,
						  data->cpdma_reg_ofs);
	dma_params.txhdp	= cpsw_dma_txhdp((u32)priv->regs,
						 data->cpdma_sram_ofs);
	dma_params.rxhdp	= cpsw_dma_rxhdp((u32)priv->regs,
						 data->cpdma_sram_ofs);
	dma_params.txcp		= cpsw_dma_txcp((u32)priv->regs,
						data->cpdma_sram_ofs);
	dma_params.rxcp		= cpsw_dma_rxcp((u32)priv->regs,
						data->cpdma_sram_ofs);

	dma_params.num_chan		= data->channels;
	dma_params.has_soft_reset	= true;
	dma_params.min_packet_size	= CPSW_MIN_PACKET_SIZE;
	dma_params.desc_mem_size	= data->bd_ram_size;
	dma_params.desc_align		= 16;
	dma_params.has_ext_regs		= true;
	dma_params.desc_mem_phys        = data->no_bd_ram ? 0 :
			(u32 __force)priv->cpsw_res->start + data->bd_ram_ofs;
	dma_params.desc_hw_addr         = data->hw_ram_addr ?
			data->hw_ram_addr : dma_params.desc_mem_phys ;

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

	memset(&ale_params, 0, sizeof(ale_params));
	ale_params.dev			= &ndev->dev;
	ale_params.ale_regs		= (void *)((u32)priv->regs) +
						((u32)data->ale_reg_ofs);
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
			priv->num_irqs = k;
		}
		k++;
	}

	ndev->flags |= IFF_ALLMULTI;	/* see cpsw_ndo_change_rx_flags() */

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

	if (cpts_register(&pdev->dev, &priv->cpts,
			  data->cpts_clock_mult, data->cpts_clock_shift))
		dev_err(priv->dev, "error registering cpts device\n");

	cpsw_notice(priv, probe, "initialized device (regs %x, irq %d)\n",
		  priv->cpsw_res->start, ndev->irq);

	return 0;

clean_irq_ret:
	free_irq(ndev->irq, priv);
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
	free_netdev(ndev);
	return ret;
}

static int __devexit cpsw_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct cpsw_priv *priv = netdev_priv(ndev);

	pr_info("removing device");
	platform_set_drvdata(pdev, NULL);

	cpts_unregister(&priv->cpts);
	free_irq(ndev->irq, priv);
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

static struct platform_driver cpsw_driver = {
	.driver = {
		.name	 = "cpsw",
		.owner	 = THIS_MODULE,
		.pm	 = &cpsw_pm_ops,
		.of_match_table = of_match_ptr(cpsw_of_mtable),
	},
	.probe = cpsw_probe,
	.remove = __devexit_p(cpsw_remove),
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
