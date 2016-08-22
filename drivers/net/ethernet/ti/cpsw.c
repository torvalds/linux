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
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_device.h>
#include <linux/if_vlan.h>

#include <linux/pinctrl/consumer.h>

#include "cpsw.h"
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
#define CPSW_VERSION_3		0x19010f
#define CPSW_VERSION_4		0x190112

#define HOST_PORT_NUM		0
#define SLIVER_SIZE		0x40

#define CPSW1_HOST_PORT_OFFSET	0x028
#define CPSW1_SLAVE_OFFSET	0x050
#define CPSW1_SLAVE_SIZE	0x040
#define CPSW1_CPDMA_OFFSET	0x100
#define CPSW1_STATERAM_OFFSET	0x200
#define CPSW1_HW_STATS		0x400
#define CPSW1_CPTS_OFFSET	0x500
#define CPSW1_ALE_OFFSET	0x600
#define CPSW1_SLIVER_OFFSET	0x700

#define CPSW2_HOST_PORT_OFFSET	0x108
#define CPSW2_SLAVE_OFFSET	0x200
#define CPSW2_SLAVE_SIZE	0x100
#define CPSW2_CPDMA_OFFSET	0x800
#define CPSW2_HW_STATS		0x900
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
#define CPDMA_TX_PRIORITY_MAP	0x01234567

#define CPSW_VLAN_AWARE		BIT(1)
#define CPSW_ALE_VLAN_AWARE	1

#define CPSW_FIFO_NORMAL_MODE		(0 << 16)
#define CPSW_FIFO_DUAL_MAC_MODE		(1 << 16)
#define CPSW_FIFO_RATE_LIMIT_MODE	(2 << 16)

#define CPSW_INTPACEEN		(0x3f << 16)
#define CPSW_INTPRESCALE_MASK	(0x7FF << 0)
#define CPSW_CMINTMAX_CNT	63
#define CPSW_CMINTMIN_CNT	2
#define CPSW_CMINTMAX_INTVL	(1000 / CPSW_CMINTMIN_CNT)
#define CPSW_CMINTMIN_INTVL	((1000 / CPSW_CMINTMAX_CNT) + 1)

#define cpsw_slave_index(cpsw, priv)				\
		((cpsw->data.dual_emac) ? priv->emac_port :	\
		cpsw->data.active_slave)
#define IRQ_NUM			2
#define CPSW_MAX_QUEUES		8

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
#define TS_TTL_NONZERO      (1<<8)  /* Time Sync Time To Live Non-zero enable */
#define TS_ANNEX_F_EN       (1<<6)  /* Time Sync Annex F enable */
#define TS_ANNEX_D_EN       (1<<4)  /* Time Sync Annex D enable */
#define TS_LTYPE2_EN        (1<<3)  /* Time Sync LTYPE 2 enable */
#define TS_LTYPE1_EN        (1<<2)  /* Time Sync LTYPE 1 enable */
#define TS_TX_EN            (1<<1)  /* Time Sync Transmit Enable */
#define TS_RX_EN            (1<<0)  /* Time Sync Receive Enable */

#define CTRL_V2_TS_BITS \
	(TS_320 | TS_319 | TS_132 | TS_131 | TS_130 | TS_129 |\
	 TS_TTL_NONZERO  | TS_ANNEX_D_EN | TS_LTYPE1_EN)

#define CTRL_V2_ALL_TS_MASK (CTRL_V2_TS_BITS | TS_TX_EN | TS_RX_EN)
#define CTRL_V2_TX_TS_BITS  (CTRL_V2_TS_BITS | TS_TX_EN)
#define CTRL_V2_RX_TS_BITS  (CTRL_V2_TS_BITS | TS_RX_EN)


#define CTRL_V3_TS_BITS \
	(TS_320 | TS_319 | TS_132 | TS_131 | TS_130 | TS_129 |\
	 TS_TTL_NONZERO | TS_ANNEX_F_EN | TS_ANNEX_D_EN |\
	 TS_LTYPE1_EN)

#define CTRL_V3_ALL_TS_MASK (CTRL_V3_TS_BITS | TS_TX_EN | TS_RX_EN)
#define CTRL_V3_TX_TS_BITS  (CTRL_V3_TS_BITS | TS_TX_EN)
#define CTRL_V3_RX_TS_BITS  (CTRL_V3_TS_BITS | TS_RX_EN)

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

struct cpsw_hw_stats {
	u32	rxgoodframes;
	u32	rxbroadcastframes;
	u32	rxmulticastframes;
	u32	rxpauseframes;
	u32	rxcrcerrors;
	u32	rxaligncodeerrors;
	u32	rxoversizedframes;
	u32	rxjabberframes;
	u32	rxundersizedframes;
	u32	rxfragments;
	u32	__pad_0[2];
	u32	rxoctets;
	u32	txgoodframes;
	u32	txbroadcastframes;
	u32	txmulticastframes;
	u32	txpauseframes;
	u32	txdeferredframes;
	u32	txcollisionframes;
	u32	txsinglecollframes;
	u32	txmultcollframes;
	u32	txexcessivecollisions;
	u32	txlatecollisions;
	u32	txunderrun;
	u32	txcarriersenseerrors;
	u32	txoctets;
	u32	octetframes64;
	u32	octetframes65t127;
	u32	octetframes128t255;
	u32	octetframes256t511;
	u32	octetframes512t1023;
	u32	octetframes1024tup;
	u32	netoctets;
	u32	rxsofoverruns;
	u32	rxmofoverruns;
	u32	rxdmaoverruns;
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

struct cpsw_common {
	struct device			*dev;
	struct cpsw_platform_data	data;
	struct napi_struct		napi_rx;
	struct napi_struct		napi_tx;
	struct cpsw_ss_regs __iomem	*regs;
	struct cpsw_wr_regs __iomem	*wr_regs;
	u8 __iomem			*hw_stats;
	struct cpsw_host_regs __iomem	*host_port_regs;
	u32				version;
	u32				coal_intvl;
	u32				bus_freq_mhz;
	int				rx_packet_max;
	struct cpsw_slave		*slaves;
	struct cpdma_ctlr		*dma;
	struct cpdma_chan		*txch[CPSW_MAX_QUEUES];
	struct cpdma_chan		*rxch[CPSW_MAX_QUEUES];
	struct cpsw_ale			*ale;
	bool				quirk_irq;
	bool				rx_irq_disabled;
	bool				tx_irq_disabled;
	u32 irqs_table[IRQ_NUM];
	struct cpts			*cpts;
	int				rx_ch_num, tx_ch_num;
};

struct cpsw_priv {
	struct net_device		*ndev;
	struct device			*dev;
	u32				msg_enable;
	u8				mac_addr[ETH_ALEN];
	bool				rx_pause;
	bool				tx_pause;
	u32 emac_port;
	struct cpsw_common *cpsw;
};

struct cpsw_stats {
	char stat_string[ETH_GSTRING_LEN];
	int type;
	int sizeof_stat;
	int stat_offset;
};

enum {
	CPSW_STATS,
	CPDMA_RX_STATS,
	CPDMA_TX_STATS,
};

#define CPSW_STAT(m)		CPSW_STATS,				\
				sizeof(((struct cpsw_hw_stats *)0)->m), \
				offsetof(struct cpsw_hw_stats, m)
#define CPDMA_RX_STAT(m)	CPDMA_RX_STATS,				   \
				sizeof(((struct cpdma_chan_stats *)0)->m), \
				offsetof(struct cpdma_chan_stats, m)
#define CPDMA_TX_STAT(m)	CPDMA_TX_STATS,				   \
				sizeof(((struct cpdma_chan_stats *)0)->m), \
				offsetof(struct cpdma_chan_stats, m)

static const struct cpsw_stats cpsw_gstrings_stats[] = {
	{ "Good Rx Frames", CPSW_STAT(rxgoodframes) },
	{ "Broadcast Rx Frames", CPSW_STAT(rxbroadcastframes) },
	{ "Multicast Rx Frames", CPSW_STAT(rxmulticastframes) },
	{ "Pause Rx Frames", CPSW_STAT(rxpauseframes) },
	{ "Rx CRC Errors", CPSW_STAT(rxcrcerrors) },
	{ "Rx Align/Code Errors", CPSW_STAT(rxaligncodeerrors) },
	{ "Oversize Rx Frames", CPSW_STAT(rxoversizedframes) },
	{ "Rx Jabbers", CPSW_STAT(rxjabberframes) },
	{ "Undersize (Short) Rx Frames", CPSW_STAT(rxundersizedframes) },
	{ "Rx Fragments", CPSW_STAT(rxfragments) },
	{ "Rx Octets", CPSW_STAT(rxoctets) },
	{ "Good Tx Frames", CPSW_STAT(txgoodframes) },
	{ "Broadcast Tx Frames", CPSW_STAT(txbroadcastframes) },
	{ "Multicast Tx Frames", CPSW_STAT(txmulticastframes) },
	{ "Pause Tx Frames", CPSW_STAT(txpauseframes) },
	{ "Deferred Tx Frames", CPSW_STAT(txdeferredframes) },
	{ "Collisions", CPSW_STAT(txcollisionframes) },
	{ "Single Collision Tx Frames", CPSW_STAT(txsinglecollframes) },
	{ "Multiple Collision Tx Frames", CPSW_STAT(txmultcollframes) },
	{ "Excessive Collisions", CPSW_STAT(txexcessivecollisions) },
	{ "Late Collisions", CPSW_STAT(txlatecollisions) },
	{ "Tx Underrun", CPSW_STAT(txunderrun) },
	{ "Carrier Sense Errors", CPSW_STAT(txcarriersenseerrors) },
	{ "Tx Octets", CPSW_STAT(txoctets) },
	{ "Rx + Tx 64 Octet Frames", CPSW_STAT(octetframes64) },
	{ "Rx + Tx 65-127 Octet Frames", CPSW_STAT(octetframes65t127) },
	{ "Rx + Tx 128-255 Octet Frames", CPSW_STAT(octetframes128t255) },
	{ "Rx + Tx 256-511 Octet Frames", CPSW_STAT(octetframes256t511) },
	{ "Rx + Tx 512-1023 Octet Frames", CPSW_STAT(octetframes512t1023) },
	{ "Rx + Tx 1024-Up Octet Frames", CPSW_STAT(octetframes1024tup) },
	{ "Net Octets", CPSW_STAT(netoctets) },
	{ "Rx Start of Frame Overruns", CPSW_STAT(rxsofoverruns) },
	{ "Rx Middle of Frame Overruns", CPSW_STAT(rxmofoverruns) },
	{ "Rx DMA Overruns", CPSW_STAT(rxdmaoverruns) },
};

static const struct cpsw_stats cpsw_gstrings_ch_stats[] = {
	{ "head_enqueue", CPDMA_RX_STAT(head_enqueue) },
	{ "tail_enqueue", CPDMA_RX_STAT(tail_enqueue) },
	{ "pad_enqueue", CPDMA_RX_STAT(pad_enqueue) },
	{ "misqueued", CPDMA_RX_STAT(misqueued) },
	{ "desc_alloc_fail", CPDMA_RX_STAT(desc_alloc_fail) },
	{ "pad_alloc_fail", CPDMA_RX_STAT(pad_alloc_fail) },
	{ "runt_receive_buf", CPDMA_RX_STAT(runt_receive_buff) },
	{ "runt_transmit_buf", CPDMA_RX_STAT(runt_transmit_buff) },
	{ "empty_dequeue", CPDMA_RX_STAT(empty_dequeue) },
	{ "busy_dequeue", CPDMA_RX_STAT(busy_dequeue) },
	{ "good_dequeue", CPDMA_RX_STAT(good_dequeue) },
	{ "requeue", CPDMA_RX_STAT(requeue) },
	{ "teardown_dequeue", CPDMA_RX_STAT(teardown_dequeue) },
};

#define CPSW_STATS_COMMON_LEN	ARRAY_SIZE(cpsw_gstrings_stats)
#define CPSW_STATS_CH_LEN	ARRAY_SIZE(cpsw_gstrings_ch_stats)

#define ndev_to_cpsw(ndev) (((struct cpsw_priv *)netdev_priv(ndev))->cpsw)
#define napi_to_cpsw(napi)	container_of(napi, struct cpsw_common, napi)
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

#define cpsw_dual_emac_src_port_detect(cpsw, status, ndev, skb)		\
	do {								\
		if (!cpsw->data.dual_emac)				\
			break;						\
		if (CPDMA_RX_SOURCE_PORT(status) == 1) {		\
			ndev = cpsw->slaves[0].ndev;			\
			skb->dev = ndev;				\
		} else if (CPDMA_RX_SOURCE_PORT(status) == 2) {		\
			ndev = cpsw->slaves[1].ndev;			\
			skb->dev = ndev;				\
		}							\
	} while (0)
#define cpsw_add_mcast(cpsw, priv, addr)				\
	do {								\
		if (cpsw->data.dual_emac) {				\
			struct cpsw_slave *slave = cpsw->slaves +	\
						priv->emac_port;	\
			int slave_port = cpsw_get_slave_port(		\
						slave->slave_num);	\
			cpsw_ale_add_mcast(cpsw->ale, addr,		\
				1 << slave_port | ALE_PORT_HOST,	\
				ALE_VLAN, slave->port_vlan, 0);		\
		} else {						\
			cpsw_ale_add_mcast(cpsw->ale, addr,		\
				ALE_ALL_PORTS,				\
				0, 0, 0);				\
		}							\
	} while (0)

static inline int cpsw_get_slave_port(u32 slave_num)
{
	return slave_num + 1;
}

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

static void cpsw_ndo_set_rx_mode(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int vid;

	if (cpsw->data.dual_emac)
		vid = cpsw->slaves[priv->emac_port].port_vlan;
	else
		vid = cpsw->data.default_vlan;

	if (ndev->flags & IFF_PROMISC) {
		/* Enable promiscuous mode */
		cpsw_set_promiscious(ndev, true);
		cpsw_ale_set_allmulti(cpsw->ale, IFF_ALLMULTI);
		return;
	} else {
		/* Disable promiscuous mode */
		cpsw_set_promiscious(ndev, false);
	}

	/* Restore allmulti on vlans if necessary */
	cpsw_ale_set_allmulti(cpsw->ale, priv->ndev->flags & IFF_ALLMULTI);

	/* Clear all mcast from ALE */
	cpsw_ale_flush_multicast(cpsw->ale, ALE_ALL_PORTS, vid);

	if (!netdev_mc_empty(ndev)) {
		struct netdev_hw_addr *ha;

		/* program multicast address list into ALE register */
		netdev_for_each_mc_addr(ha, ndev) {
			cpsw_add_mcast(cpsw, priv, (u8 *)ha->addr);
		}
	}
}

static void cpsw_intr_enable(struct cpsw_common *cpsw)
{
	__raw_writel(0xFF, &cpsw->wr_regs->tx_en);
	__raw_writel(0xFF, &cpsw->wr_regs->rx_en);

	cpdma_ctlr_int_ctrl(cpsw->dma, true);
	return;
}

static void cpsw_intr_disable(struct cpsw_common *cpsw)
{
	__raw_writel(0, &cpsw->wr_regs->tx_en);
	__raw_writel(0, &cpsw->wr_regs->rx_en);

	cpdma_ctlr_int_ctrl(cpsw->dma, false);
	return;
}

static void cpsw_tx_handler(void *token, int len, int status)
{
	struct netdev_queue	*txq;
	struct sk_buff		*skb = token;
	struct net_device	*ndev = skb->dev;
	struct cpsw_common	*cpsw = ndev_to_cpsw(ndev);

	/* Check whether the queue is stopped due to stalled tx dma, if the
	 * queue is stopped then start the queue as we have free desc for tx
	 */
	txq = netdev_get_tx_queue(ndev, skb_get_queue_mapping(skb));
	if (unlikely(netif_tx_queue_stopped(txq)))
		netif_tx_wake_queue(txq);

	cpts_tx_timestamp(cpsw->cpts, skb);
	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += len;
	dev_kfree_skb_any(skb);
}

static void cpsw_rx_handler(void *token, int len, int status)
{
	struct cpdma_chan	*ch;
	struct sk_buff		*skb = token;
	struct sk_buff		*new_skb;
	struct net_device	*ndev = skb->dev;
	int			ret = 0;
	struct cpsw_common	*cpsw = ndev_to_cpsw(ndev);

	cpsw_dual_emac_src_port_detect(cpsw, status, ndev, skb);

	if (unlikely(status < 0) || unlikely(!netif_running(ndev))) {
		bool ndev_status = false;
		struct cpsw_slave *slave = cpsw->slaves;
		int n;

		if (cpsw->data.dual_emac) {
			/* In dual emac mode check for all interfaces */
			for (n = cpsw->data.slaves; n; n--, slave++)
				if (netif_running(slave->ndev))
					ndev_status = true;
		}

		if (ndev_status && (status >= 0)) {
			/* The packet received is for the interface which
			 * is already down and the other interface is up
			 * and running, instead of freeing which results
			 * in reducing of the number of rx descriptor in
			 * DMA engine, requeue skb back to cpdma.
			 */
			new_skb = skb;
			goto requeue;
		}

		/* the interface is going down, skbs are purged */
		dev_kfree_skb_any(skb);
		return;
	}

	new_skb = netdev_alloc_skb_ip_align(ndev, cpsw->rx_packet_max);
	if (new_skb) {
		skb_copy_queue_mapping(new_skb, skb);
		skb_put(skb, len);
		cpts_rx_timestamp(cpsw->cpts, skb);
		skb->protocol = eth_type_trans(skb, ndev);
		netif_receive_skb(skb);
		ndev->stats.rx_bytes += len;
		ndev->stats.rx_packets++;
		kmemleak_not_leak(new_skb);
	} else {
		ndev->stats.rx_dropped++;
		new_skb = skb;
	}

requeue:
	ch = cpsw->rxch[skb_get_queue_mapping(new_skb)];
	ret = cpdma_chan_submit(ch, new_skb, new_skb->data,
				skb_tailroom(new_skb), 0);
	if (WARN_ON(ret < 0))
		dev_kfree_skb_any(new_skb);
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

static int cpsw_tx_poll(struct napi_struct *napi_tx, int budget)
{
	u32			ch_map;
	int			num_tx, ch;
	struct cpsw_common	*cpsw = napi_to_cpsw(napi_tx);

	/* process every unprocessed channel */
	ch_map = cpdma_ctrl_txchs_state(cpsw->dma);
	for (ch = 0, num_tx = 0; num_tx < budget; ch_map >>= 1, ch++) {
		if (!ch_map) {
			ch_map = cpdma_ctrl_txchs_state(cpsw->dma);
			if (!ch_map)
				break;

			ch = 0;
		}

		if (!(ch_map & 0x01))
			continue;

		num_tx += cpdma_chan_process(cpsw->txch[ch], budget - num_tx);
	}

	if (num_tx < budget) {
		napi_complete(napi_tx);
		writel(0xff, &cpsw->wr_regs->tx_en);
		if (cpsw->quirk_irq && cpsw->tx_irq_disabled) {
			cpsw->tx_irq_disabled = false;
			enable_irq(cpsw->irqs_table[1]);
		}
	}

	return num_tx;
}

static int cpsw_rx_poll(struct napi_struct *napi_rx, int budget)
{
	u32			ch_map;
	int			num_rx, ch;
	struct cpsw_common	*cpsw = napi_to_cpsw(napi_rx);

	/* process every unprocessed channel */
	ch_map = cpdma_ctrl_rxchs_state(cpsw->dma);
	for (ch = 0, num_rx = 0; num_rx < budget; ch_map >>= 1, ch++) {
		if (!ch_map) {
			ch_map = cpdma_ctrl_rxchs_state(cpsw->dma);
			if (!ch_map)
				break;

			ch = 0;
		}

		if (!(ch_map & 0x01))
			continue;

		num_rx += cpdma_chan_process(cpsw->rxch[ch], budget - num_rx);
	}

	if (num_rx < budget) {
		napi_complete(napi_rx);
		writel(0xff, &cpsw->wr_regs->rx_en);
		if (cpsw->quirk_irq && cpsw->rx_irq_disabled) {
			cpsw->rx_irq_disabled = false;
			enable_irq(cpsw->irqs_table[0]);
		}
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
	struct cpsw_common *cpsw = priv->cpsw;

	if (!phy)
		return;

	slave_port = cpsw_get_slave_port(slave->slave_num);

	if (phy->link) {
		mac_control = cpsw->data.mac_control;

		/* enable forwarding */
		cpsw_ale_control_set(cpsw->ale, slave_port,
				     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

		if (phy->speed == 1000)
			mac_control |= BIT(7);	/* GIGABITEN	*/
		if (phy->duplex)
			mac_control |= BIT(0);	/* FULLDUPLEXEN	*/

		/* set speed_in input in case RMII mode is used in 100Mbps */
		if (phy->speed == 100)
			mac_control |= BIT(15);
		else if (phy->speed == 10)
			mac_control |= BIT(18); /* In Band mode */

		if (priv->rx_pause)
			mac_control |= BIT(3);

		if (priv->tx_pause)
			mac_control |= BIT(4);

		*link = true;
	} else {
		mac_control = 0;
		/* disable forwarding */
		cpsw_ale_control_set(cpsw->ale, slave_port,
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
			netif_tx_wake_all_queues(ndev);
	} else {
		netif_carrier_off(ndev);
		netif_tx_stop_all_queues(ndev);
	}
}

static int cpsw_get_coalesce(struct net_device *ndev,
				struct ethtool_coalesce *coal)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);

	coal->rx_coalesce_usecs = cpsw->coal_intvl;
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
	struct cpsw_common *cpsw = priv->cpsw;

	coal_intvl = coal->rx_coalesce_usecs;

	int_ctrl =  readl(&cpsw->wr_regs->int_control);
	prescale = cpsw->bus_freq_mhz * 4;

	if (!coal->rx_coalesce_usecs) {
		int_ctrl &= ~(CPSW_INTPRESCALE_MASK | CPSW_INTPACEEN);
		goto update_return;
	}

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
	writel(num_interrupts, &cpsw->wr_regs->rx_imax);
	writel(num_interrupts, &cpsw->wr_regs->tx_imax);

	int_ctrl |= CPSW_INTPACEEN;
	int_ctrl &= (~CPSW_INTPRESCALE_MASK);
	int_ctrl |= (prescale & CPSW_INTPRESCALE_MASK);

update_return:
	writel(int_ctrl, &cpsw->wr_regs->int_control);

	cpsw_notice(priv, timer, "Set coalesce to %d usecs.\n", coal_intvl);
	cpsw->coal_intvl = coal_intvl;

	return 0;
}

static int cpsw_get_sset_count(struct net_device *ndev, int sset)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);

	switch (sset) {
	case ETH_SS_STATS:
		return (CPSW_STATS_COMMON_LEN +
		       (cpsw->rx_ch_num + cpsw->tx_ch_num) *
		       CPSW_STATS_CH_LEN);
	default:
		return -EOPNOTSUPP;
	}
}

static void cpsw_add_ch_strings(u8 **p, int ch_num, int rx_dir)
{
	int ch_stats_len;
	int line;
	int i;

	ch_stats_len = CPSW_STATS_CH_LEN * ch_num;
	for (i = 0; i < ch_stats_len; i++) {
		line = i % CPSW_STATS_CH_LEN;
		snprintf(*p, ETH_GSTRING_LEN,
			 "%s DMA chan %d: %s", rx_dir ? "Rx" : "Tx",
			 i / CPSW_STATS_CH_LEN,
			 cpsw_gstrings_ch_stats[line].stat_string);
		*p += ETH_GSTRING_LEN;
	}
}

static void cpsw_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < CPSW_STATS_COMMON_LEN; i++) {
			memcpy(p, cpsw_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		cpsw_add_ch_strings(&p, cpsw->rx_ch_num, 1);
		cpsw_add_ch_strings(&p, cpsw->tx_ch_num, 0);
		break;
	}
}

static void cpsw_get_ethtool_stats(struct net_device *ndev,
				    struct ethtool_stats *stats, u64 *data)
{
	u8 *p;
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);
	struct cpdma_chan_stats ch_stats;
	int i, l, ch;

	/* Collect Davinci CPDMA stats for Rx and Tx Channel */
	for (l = 0; l < CPSW_STATS_COMMON_LEN; l++)
		data[l] = readl(cpsw->hw_stats +
				cpsw_gstrings_stats[l].stat_offset);

	for (ch = 0; ch < cpsw->rx_ch_num; ch++) {
		cpdma_chan_get_stats(cpsw->rxch[ch], &ch_stats);
		for (i = 0; i < CPSW_STATS_CH_LEN; i++, l++) {
			p = (u8 *)&ch_stats +
				cpsw_gstrings_ch_stats[i].stat_offset;
			data[l] = *(u32 *)p;
		}
	}

	for (ch = 0; ch < cpsw->tx_ch_num; ch++) {
		cpdma_chan_get_stats(cpsw->txch[ch], &ch_stats);
		for (i = 0; i < CPSW_STATS_CH_LEN; i++, l++) {
			p = (u8 *)&ch_stats +
				cpsw_gstrings_ch_stats[i].stat_offset;
			data[l] = *(u32 *)p;
		}
	}
}

static int cpsw_common_res_usage_state(struct cpsw_common *cpsw)
{
	u32 i;
	u32 usage_count = 0;

	if (!cpsw->data.dual_emac)
		return 0;

	for (i = 0; i < cpsw->data.slaves; i++)
		if (cpsw->slaves[i].open_stat)
			usage_count++;

	return usage_count;
}

static inline int cpsw_tx_packet_submit(struct cpsw_priv *priv,
					struct sk_buff *skb,
					struct cpdma_chan *txch)
{
	struct cpsw_common *cpsw = priv->cpsw;

	return cpdma_chan_submit(txch, skb, skb->data, skb->len,
				 priv->emac_port + cpsw->data.dual_emac);
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
			   port_mask, ALE_VLAN, slave->port_vlan, 0);
	cpsw_ale_add_ucast(cpsw->ale, priv->mac_addr,
			   HOST_PORT_NUM, ALE_VLAN |
			   ALE_SECURE, slave->port_vlan);
}

static void soft_reset_slave(struct cpsw_slave *slave)
{
	char name[32];

	snprintf(name, sizeof(name), "slave-%d", slave->slave_num);
	soft_reset(name, &slave->sliver->soft_reset);
}

static void cpsw_slave_open(struct cpsw_slave *slave, struct cpsw_priv *priv)
{
	u32 slave_port;
	struct cpsw_common *cpsw = priv->cpsw;

	soft_reset_slave(slave);

	/* setup priority mapping */
	__raw_writel(RX_PRIORITY_MAPPING, &slave->sliver->rx_pri_map);

	switch (cpsw->version) {
	case CPSW_VERSION_1:
		slave_write(slave, TX_PRIORITY_MAPPING, CPSW1_TX_PRI_MAP);
		break;
	case CPSW_VERSION_2:
	case CPSW_VERSION_3:
	case CPSW_VERSION_4:
		slave_write(slave, TX_PRIORITY_MAPPING, CPSW2_TX_PRI_MAP);
		break;
	}

	/* setup max packet size, and mac address */
	__raw_writel(cpsw->rx_packet_max, &slave->sliver->rx_maxlen);
	cpsw_set_slave_mac(slave, priv);

	slave->mac_control = 0;	/* no link yet */

	slave_port = cpsw_get_slave_port(slave->slave_num);

	if (cpsw->data.dual_emac)
		cpsw_add_dual_emac_def_ale_entries(priv, slave, slave_port);
	else
		cpsw_ale_add_mcast(cpsw->ale, priv->ndev->broadcast,
				   1 << slave_port, 0, 0, ALE_MCAST_FWD_2);

	if (slave->data->phy_node) {
		slave->phy = of_phy_connect(priv->ndev, slave->data->phy_node,
				 &cpsw_adjust_link, 0, slave->data->phy_if);
		if (!slave->phy) {
			dev_err(priv->dev, "phy \"%s\" not found on slave %d\n",
				slave->data->phy_node->full_name,
				slave->slave_num);
			return;
		}
	} else {
		slave->phy = phy_connect(priv->ndev, slave->data->phy_id,
				 &cpsw_adjust_link, slave->data->phy_if);
		if (IS_ERR(slave->phy)) {
			dev_err(priv->dev,
				"phy \"%s\" not found on slave %d, err %ld\n",
				slave->data->phy_id, slave->slave_num,
				PTR_ERR(slave->phy));
			slave->phy = NULL;
			return;
		}
	}

	phy_attached_info(slave->phy);

	phy_start(slave->phy);

	/* Configure GMII_SEL register */
	cpsw_phy_sel(cpsw->dev, slave->phy->interface, slave->slave_num);
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
	control_reg |= CPSW_VLAN_AWARE;
	writel(control_reg, &cpsw->regs->control);
	fifo_mode = (cpsw->data.dual_emac) ? CPSW_FIFO_DUAL_MAC_MODE :
		     CPSW_FIFO_NORMAL_MODE;
	writel(fifo_mode, &cpsw->host_port_regs->tx_in_ctl);

	/* setup host port priority mapping */
	__raw_writel(CPDMA_TX_PRIORITY_MAP,
		     &cpsw->host_port_regs->cpdma_tx_pri_map);
	__raw_writel(0, &cpsw->host_port_regs->cpdma_rx_chan_map);

	cpsw_ale_control_set(cpsw->ale, HOST_PORT_NUM,
			     ALE_PORT_STATE, ALE_PORT_STATE_FORWARD);

	if (!cpsw->data.dual_emac) {
		cpsw_ale_add_ucast(cpsw->ale, priv->mac_addr, HOST_PORT_NUM,
				   0, 0);
		cpsw_ale_add_mcast(cpsw->ale, priv->ndev->broadcast,
				   ALE_PORT_HOST, 0, 0, ALE_MCAST_FWD_2);
	}
}

static int cpsw_fill_rx_channels(struct cpsw_priv *priv)
{
	struct cpsw_common *cpsw = priv->cpsw;
	struct sk_buff *skb;
	int ch_buf_num;
	int ch, i, ret;

	for (ch = 0; ch < cpsw->rx_ch_num; ch++) {
		ch_buf_num = cpdma_chan_get_rx_buf_num(cpsw->rxch[ch]);
		for (i = 0; i < ch_buf_num; i++) {
			skb = __netdev_alloc_skb_ip_align(priv->ndev,
							  cpsw->rx_packet_max,
							  GFP_KERNEL);
			if (!skb) {
				cpsw_err(priv, ifup, "cannot allocate skb\n");
				return -ENOMEM;
			}

			skb_set_queue_mapping(skb, ch);
			ret = cpdma_chan_submit(cpsw->rxch[ch], skb, skb->data,
						skb_tailroom(skb), 0);
			if (ret < 0) {
				cpsw_err(priv, ifup,
					 "cannot submit skb to channel %d rx, error %d\n",
					 ch, ret);
				kfree_skb(skb);
				return ret;
			}
			kmemleak_not_leak(skb);
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
	soft_reset_slave(slave);
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

	if (!cpsw_common_res_usage_state(cpsw))
		cpsw_intr_disable(cpsw);
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

	/* initialize host and slave ports */
	if (!cpsw_common_res_usage_state(cpsw))
		cpsw_init_host_port(priv);
	for_each_slave(priv, cpsw_slave_open, priv);

	/* Add default VLAN */
	if (!cpsw->data.dual_emac)
		cpsw_add_default_vlan(priv);
	else
		cpsw_ale_add_vlan(cpsw->ale, cpsw->data.default_vlan,
				  ALE_ALL_PORTS, ALE_ALL_PORTS, 0, 0);

	if (!cpsw_common_res_usage_state(cpsw)) {
		/* setup tx dma to fixed prio and zero offset */
		cpdma_control_set(cpsw->dma, CPDMA_TX_PRIO_FIXED, 1);
		cpdma_control_set(cpsw->dma, CPDMA_RX_BUFFER_OFFSET, 0);

		/* disable priority elevation */
		__raw_writel(0, &cpsw->regs->ptype);

		/* enable statistics collection only on all ports */
		__raw_writel(0x7, &cpsw->regs->stat_port_en);

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

		ret = cpsw_fill_rx_channels(priv);
		if (ret < 0)
			goto err_cleanup;

		if (cpts_register(cpsw->dev, cpsw->cpts,
				  cpsw->data.cpts_clock_mult,
				  cpsw->data.cpts_clock_shift))
			dev_err(priv->dev, "error registering cpts device\n");

	}

	/* Enable Interrupt pacing if configured */
	if (cpsw->coal_intvl != 0) {
		struct ethtool_coalesce coal;

		coal.rx_coalesce_usecs = cpsw->coal_intvl;
		cpsw_set_coalesce(ndev, &coal);
	}

	cpdma_ctlr_start(cpsw->dma);
	cpsw_intr_enable(cpsw);

	if (cpsw->data.dual_emac)
		cpsw->slaves[priv->emac_port].open_stat = true;

	netif_tx_start_all_queues(ndev);

	return 0;

err_cleanup:
	cpdma_ctlr_stop(cpsw->dma);
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
	netif_tx_stop_all_queues(priv->ndev);
	netif_carrier_off(priv->ndev);

	if (cpsw_common_res_usage_state(cpsw) <= 1) {
		napi_disable(&cpsw->napi_rx);
		napi_disable(&cpsw->napi_tx);
		cpts_unregister(cpsw->cpts);
		cpsw_intr_disable(cpsw);
		cpdma_ctlr_stop(cpsw->dma);
		cpsw_ale_stop(cpsw->ale);
	}
	for_each_slave(priv, cpsw_slave_stop, cpsw);
	pm_runtime_put_sync(cpsw->dev);
	if (cpsw->data.dual_emac)
		cpsw->slaves[priv->emac_port].open_stat = false;
	return 0;
}

static netdev_tx_t cpsw_ndo_start_xmit(struct sk_buff *skb,
				       struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	struct netdev_queue *txq;
	struct cpdma_chan *txch;
	int ret, q_idx;

	netif_trans_update(ndev);

	if (skb_padto(skb, CPSW_MIN_PACKET_SIZE)) {
		cpsw_err(priv, tx_err, "packet pad failed\n");
		ndev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
				cpsw->cpts->tx_enable)
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	skb_tx_timestamp(skb);

	q_idx = skb_get_queue_mapping(skb);
	if (q_idx >= cpsw->tx_ch_num)
		q_idx = q_idx % cpsw->tx_ch_num;

	txch = cpsw->txch[q_idx];
	ret = cpsw_tx_packet_submit(priv, skb, txch);
	if (unlikely(ret != 0)) {
		cpsw_err(priv, tx_err, "desc submit failed\n");
		goto fail;
	}

	/* If there is no more tx desc left free then we need to
	 * tell the kernel to stop sending us tx frames.
	 */
	if (unlikely(!cpdma_check_free_tx_desc(txch))) {
		txq = netdev_get_tx_queue(ndev, q_idx);
		netif_tx_stop_queue(txq);
	}

	return NETDEV_TX_OK;
fail:
	ndev->stats.tx_dropped++;
	txq = netdev_get_tx_queue(ndev, skb_get_queue_mapping(skb));
	netif_tx_stop_queue(txq);
	return NETDEV_TX_BUSY;
}

#ifdef CONFIG_TI_CPTS

static void cpsw_hwtstamp_v1(struct cpsw_common *cpsw)
{
	struct cpsw_slave *slave = &cpsw->slaves[cpsw->data.active_slave];
	u32 ts_en, seq_id;

	if (!cpsw->cpts->tx_enable && !cpsw->cpts->rx_enable) {
		slave_write(slave, 0, CPSW1_TS_CTL);
		return;
	}

	seq_id = (30 << CPSW_V1_SEQ_ID_OFS_SHIFT) | ETH_P_1588;
	ts_en = EVENT_MSG_BITS << CPSW_V1_MSG_TYPE_OFS;

	if (cpsw->cpts->tx_enable)
		ts_en |= CPSW_V1_TS_TX_EN;

	if (cpsw->cpts->rx_enable)
		ts_en |= CPSW_V1_TS_RX_EN;

	slave_write(slave, ts_en, CPSW1_TS_CTL);
	slave_write(slave, seq_id, CPSW1_TS_SEQ_LTYPE);
}

static void cpsw_hwtstamp_v2(struct cpsw_priv *priv)
{
	struct cpsw_slave *slave;
	struct cpsw_common *cpsw = priv->cpsw;
	u32 ctrl, mtype;

	if (cpsw->data.dual_emac)
		slave = &cpsw->slaves[priv->emac_port];
	else
		slave = &cpsw->slaves[cpsw->data.active_slave];

	ctrl = slave_read(slave, CPSW2_CONTROL);
	switch (cpsw->version) {
	case CPSW_VERSION_2:
		ctrl &= ~CTRL_V2_ALL_TS_MASK;

		if (cpsw->cpts->tx_enable)
			ctrl |= CTRL_V2_TX_TS_BITS;

		if (cpsw->cpts->rx_enable)
			ctrl |= CTRL_V2_RX_TS_BITS;
		break;
	case CPSW_VERSION_3:
	default:
		ctrl &= ~CTRL_V3_ALL_TS_MASK;

		if (cpsw->cpts->tx_enable)
			ctrl |= CTRL_V3_TX_TS_BITS;

		if (cpsw->cpts->rx_enable)
			ctrl |= CTRL_V3_RX_TS_BITS;
		break;
	}

	mtype = (30 << TS_SEQ_ID_OFFSET_SHIFT) | EVENT_MSG_BITS;

	slave_write(slave, mtype, CPSW2_TS_SEQ_MTYPE);
	slave_write(slave, ctrl, CPSW2_CONTROL);
	__raw_writel(ETH_P_1588, &cpsw->regs->ts_ltype);
}

static int cpsw_hwtstamp_set(struct net_device *dev, struct ifreq *ifr)
{
	struct cpsw_priv *priv = netdev_priv(dev);
	struct hwtstamp_config cfg;
	struct cpsw_common *cpsw = priv->cpsw;
	struct cpts *cpts = cpsw->cpts;

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

	cpts->tx_enable = cfg.tx_type == HWTSTAMP_TX_ON;

	switch (cpsw->version) {
	case CPSW_VERSION_1:
		cpsw_hwtstamp_v1(cpsw);
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
	struct cpts *cpts = cpsw->cpts;
	struct hwtstamp_config cfg;

	if (cpsw->version != CPSW_VERSION_1 &&
	    cpsw->version != CPSW_VERSION_2 &&
	    cpsw->version != CPSW_VERSION_3)
		return -EOPNOTSUPP;

	cfg.flags = 0;
	cfg.tx_type = cpts->tx_enable ? HWTSTAMP_TX_ON : HWTSTAMP_TX_OFF;
	cfg.rx_filter = (cpts->rx_enable ?
			 HWTSTAMP_FILTER_PTP_V2_EVENT : HWTSTAMP_FILTER_NONE);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
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
#ifdef CONFIG_TI_CPTS
	case SIOCSHWTSTAMP:
		return cpsw_hwtstamp_set(dev, req);
	case SIOCGHWTSTAMP:
		return cpsw_hwtstamp_get(dev, req);
#endif
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
		cpdma_chan_stop(cpsw->txch[ch]);
		cpdma_chan_start(cpsw->txch[ch]);
	}

	cpsw_intr_enable(cpsw);
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

static inline int cpsw_add_vlan_ale_entry(struct cpsw_priv *priv,
				unsigned short vid)
{
	int ret;
	int unreg_mcast_mask = 0;
	u32 port_mask;
	struct cpsw_common *cpsw = priv->cpsw;

	if (cpsw->data.dual_emac) {
		port_mask = (1 << (priv->emac_port + 1)) | ALE_PORT_HOST;

		if (priv->ndev->flags & IFF_ALLMULTI)
			unreg_mcast_mask = port_mask;
	} else {
		port_mask = ALE_ALL_PORTS;

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
				 port_mask, ALE_VLAN, vid, 0);
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
			if (vid == cpsw->slaves[i].port_vlan)
				return -EINVAL;
		}
	}

	dev_info(priv->dev, "Adding vlanid %d to vlan filter\n", vid);
	ret = cpsw_add_vlan_ale_entry(priv, vid);

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
				return -EINVAL;
		}
	}

	dev_info(priv->dev, "removing vlanid %d from vlan filter\n", vid);
	ret = cpsw_ale_del_vlan(cpsw->ale, vid, 0);
	if (ret != 0)
		return ret;

	ret = cpsw_ale_del_ucast(cpsw->ale, priv->mac_addr,
				 HOST_PORT_NUM, ALE_VLAN, vid);
	if (ret != 0)
		return ret;

	ret = cpsw_ale_del_mcast(cpsw->ale, priv->ndev->broadcast,
				 0, ALE_VLAN, vid);
	pm_runtime_put(cpsw->dev);
	return ret;
}

static const struct net_device_ops cpsw_netdev_ops = {
	.ndo_open		= cpsw_ndo_open,
	.ndo_stop		= cpsw_ndo_stop,
	.ndo_start_xmit		= cpsw_ndo_start_xmit,
	.ndo_set_mac_address	= cpsw_ndo_set_mac_address,
	.ndo_do_ioctl		= cpsw_ndo_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_tx_timeout		= cpsw_ndo_tx_timeout,
	.ndo_set_rx_mode	= cpsw_ndo_set_rx_mode,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= cpsw_ndo_poll_controller,
#endif
	.ndo_vlan_rx_add_vid	= cpsw_ndo_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= cpsw_ndo_vlan_rx_kill_vid,
};

static int cpsw_get_regs_len(struct net_device *ndev)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);

	return cpsw->data.ale_entries * ALE_ENTRY_WORDS * sizeof(u32);
}

static void cpsw_get_regs(struct net_device *ndev,
			  struct ethtool_regs *regs, void *p)
{
	u32 *reg = p;
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);

	/* update CPSW IP version */
	regs->version = cpsw->version;

	cpsw_ale_dump(cpsw->ale, reg);
}

static void cpsw_get_drvinfo(struct net_device *ndev,
			     struct ethtool_drvinfo *info)
{
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);
	struct platform_device	*pdev = to_platform_device(cpsw->dev);

	strlcpy(info->driver, "cpsw", sizeof(info->driver));
	strlcpy(info->version, "1.0", sizeof(info->version));
	strlcpy(info->bus_info, pdev->name, sizeof(info->bus_info));
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
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);

	info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RX_SOFTWARE |
		SOF_TIMESTAMPING_SOFTWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->phc_index = cpsw->cpts->phc_index;
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
	struct cpsw_common *cpsw = priv->cpsw;
	int slave_no = cpsw_slave_index(cpsw, priv);

	if (cpsw->slaves[slave_no].phy)
		return phy_ethtool_gset(cpsw->slaves[slave_no].phy, ecmd);
	else
		return -EOPNOTSUPP;
}

static int cpsw_set_settings(struct net_device *ndev, struct ethtool_cmd *ecmd)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int slave_no = cpsw_slave_index(cpsw, priv);

	if (cpsw->slaves[slave_no].phy)
		return phy_ethtool_sset(cpsw->slaves[slave_no].phy, ecmd);
	else
		return -EOPNOTSUPP;
}

static void cpsw_get_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int slave_no = cpsw_slave_index(cpsw, priv);

	wol->supported = 0;
	wol->wolopts = 0;

	if (cpsw->slaves[slave_no].phy)
		phy_ethtool_get_wol(cpsw->slaves[slave_no].phy, wol);
}

static int cpsw_set_wol(struct net_device *ndev, struct ethtool_wolinfo *wol)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int slave_no = cpsw_slave_index(cpsw, priv);

	if (cpsw->slaves[slave_no].phy)
		return phy_ethtool_set_wol(cpsw->slaves[slave_no].phy, wol);
	else
		return -EOPNOTSUPP;
}

static void cpsw_get_pauseparam(struct net_device *ndev,
				struct ethtool_pauseparam *pause)
{
	struct cpsw_priv *priv = netdev_priv(ndev);

	pause->autoneg = AUTONEG_DISABLE;
	pause->rx_pause = priv->rx_pause ? true : false;
	pause->tx_pause = priv->tx_pause ? true : false;
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

static int cpsw_ethtool_op_begin(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	struct cpsw_common *cpsw = priv->cpsw;
	int ret;

	ret = pm_runtime_get_sync(cpsw->dev);
	if (ret < 0) {
		cpsw_err(priv, drv, "ethtool begin failed %d\n", ret);
		pm_runtime_put_noidle(cpsw->dev);
	}

	return ret;
}

static void cpsw_ethtool_op_complete(struct net_device *ndev)
{
	struct cpsw_priv *priv = netdev_priv(ndev);
	int ret;

	ret = pm_runtime_put(priv->cpsw->dev);
	if (ret < 0)
		cpsw_err(priv, drv, "ethtool complete failed %d\n", ret);
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
};

static void cpsw_slave_init(struct cpsw_slave *slave, struct cpsw_common *cpsw,
			    u32 slave_reg_ofs, u32 sliver_reg_ofs)
{
	void __iomem		*regs = cpsw->regs;
	int			slave_num = slave->slave_num;
	struct cpsw_slave_data	*data = cpsw->data.slave_data + slave_num;

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
		dev_err(&pdev->dev, "Missing slaves property in the DT.\n");
		return -EINVAL;
	}
	data->slaves = prop;

	if (of_property_read_u32(node, "active_slave", &prop)) {
		dev_err(&pdev->dev, "Missing active_slave property in the DT.\n");
		return -EINVAL;
	}
	data->active_slave = prop;

	if (of_property_read_u32(node, "cpts_clock_mult", &prop)) {
		dev_err(&pdev->dev, "Missing cpts_clock_mult property in the DT.\n");
		return -EINVAL;
	}
	data->cpts_clock_mult = prop;

	if (of_property_read_u32(node, "cpts_clock_shift", &prop)) {
		dev_err(&pdev->dev, "Missing cpts_clock_shift property in the DT.\n");
		return -EINVAL;
	}
	data->cpts_clock_shift = prop;

	data->slave_data = devm_kzalloc(&pdev->dev, data->slaves
					* sizeof(struct cpsw_slave_data),
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
		if (strcmp(slave_node->name, "slave"))
			continue;

		slave_data->phy_node = of_parse_phandle(slave_node,
							"phy-handle", 0);
		parp = of_get_property(slave_node, "phy_id", &lenp);
		if (slave_data->phy_node) {
			dev_dbg(&pdev->dev,
				"slave[%d] using phy-handle=\"%s\"\n",
				i, slave_data->phy_node->full_name);
		} else if (of_phy_is_fixed_link(slave_node)) {
			/* In the case of a fixed PHY, the DT node associated
			 * to the PHY is the Ethernet MAC DT node.
			 */
			ret = of_phy_register_fixed_link(slave_node);
			if (ret)
				return ret;
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
				return -EINVAL;
			}
			snprintf(slave_data->phy_id, sizeof(slave_data->phy_id),
				 PHY_ID_FMT, mdio->name, phyid);
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
			return slave_data->phy_if;
		}

no_phy_slave:
		mac_addr = of_get_mac_address(slave_node);
		if (mac_addr) {
			memcpy(slave_data->mac_addr, mac_addr, ETH_ALEN);
		} else {
			ret = ti_cm_get_macid(&pdev->dev, i,
					      slave_data->mac_addr);
			if (ret)
				return ret;
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
		if (i == data->slaves)
			break;
	}

	return 0;
}

static int cpsw_probe_dual_emac(struct cpsw_priv *priv)
{
	struct cpsw_common		*cpsw = priv->cpsw;
	struct cpsw_platform_data	*data = &cpsw->data;
	struct net_device		*ndev;
	struct cpsw_priv		*priv_sl2;
	int ret = 0;

	ndev = alloc_etherdev_mq(sizeof(struct cpsw_priv), CPSW_MAX_QUEUES);
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
		random_ether_addr(priv_sl2->mac_addr);
		dev_info(cpsw->dev, "cpsw: Random MACID = %pM\n",
			 priv_sl2->mac_addr);
	}
	memcpy(ndev->dev_addr, priv_sl2->mac_addr, ETH_ALEN);

	priv_sl2->emac_port = 1;
	cpsw->slaves[1].ndev = ndev;
	ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	ndev->netdev_ops = &cpsw_netdev_ops;
	ndev->ethtool_ops = &cpsw_ethtool_ops;

	/* register the network device */
	SET_NETDEV_DEV(ndev, cpsw->dev);
	ret = register_netdev(ndev);
	if (ret) {
		dev_err(cpsw->dev, "cpsw: error registering net device\n");
		free_netdev(ndev);
		ret = -ENODEV;
	}

	return ret;
}

#define CPSW_QUIRK_IRQ		BIT(0)

static struct platform_device_id cpsw_devtype[] = {
	{
		/* keep it for existing comaptibles */
		.name = "cpsw",
		.driver_data = CPSW_QUIRK_IRQ,
	}, {
		.name = "am335x-cpsw",
		.driver_data = CPSW_QUIRK_IRQ,
	}, {
		.name = "am4372-cpsw",
		.driver_data = 0,
	}, {
		.name = "dra7-cpsw",
		.driver_data = 0,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, cpsw_devtype);

enum ti_cpsw_type {
	CPSW = 0,
	AM335X_CPSW,
	AM4372_CPSW,
	DRA7_CPSW,
};

static const struct of_device_id cpsw_of_mtable[] = {
	{ .compatible = "ti,cpsw", .data = &cpsw_devtype[CPSW], },
	{ .compatible = "ti,am335x-cpsw", .data = &cpsw_devtype[AM335X_CPSW], },
	{ .compatible = "ti,am4372-cpsw", .data = &cpsw_devtype[AM4372_CPSW], },
	{ .compatible = "ti,dra7-cpsw", .data = &cpsw_devtype[DRA7_CPSW], },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cpsw_of_mtable);

static int cpsw_probe(struct platform_device *pdev)
{
	struct clk			*clk;
	struct cpsw_platform_data	*data;
	struct net_device		*ndev;
	struct cpsw_priv		*priv;
	struct cpdma_params		dma_params;
	struct cpsw_ale_params		ale_params;
	void __iomem			*ss_regs;
	struct resource			*res, *ss_res;
	const struct of_device_id	*of_id;
	struct gpio_descs		*mode;
	u32 slave_offset, sliver_offset, slave_size;
	struct cpsw_common		*cpsw;
	int ret = 0, i;
	int irq;

	cpsw = devm_kzalloc(&pdev->dev, sizeof(struct cpsw_common), GFP_KERNEL);
	cpsw->dev = &pdev->dev;

	ndev = alloc_etherdev_mq(sizeof(struct cpsw_priv), CPSW_MAX_QUEUES);
	if (!ndev) {
		dev_err(&pdev->dev, "error allocating net_device\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, ndev);
	priv = netdev_priv(ndev);
	priv->cpsw = cpsw;
	priv->ndev = ndev;
	priv->dev  = &ndev->dev;
	priv->msg_enable = netif_msg_init(debug_level, CPSW_DEBUG);
	cpsw->rx_packet_max = max(rx_packet_max, 128);
	cpsw->cpts = devm_kzalloc(&pdev->dev, sizeof(struct cpts), GFP_KERNEL);
	if (!cpsw->cpts) {
		dev_err(&pdev->dev, "error allocating cpts\n");
		ret = -ENOMEM;
		goto clean_ndev_ret;
	}

	mode = devm_gpiod_get_array_optional(&pdev->dev, "mode", GPIOD_OUT_LOW);
	if (IS_ERR(mode)) {
		ret = PTR_ERR(mode);
		dev_err(&pdev->dev, "gpio request failed, ret %d\n", ret);
		goto clean_ndev_ret;
	}

	/*
	 * This may be required here for child devices.
	 */
	pm_runtime_enable(&pdev->dev);

	/* Select default pin state */
	pinctrl_pm_select_default_state(&pdev->dev);

	if (cpsw_probe_dt(&cpsw->data, pdev)) {
		dev_err(&pdev->dev, "cpsw: platform data missing\n");
		ret = -ENODEV;
		goto clean_runtime_disable_ret;
	}
	data = &cpsw->data;
	cpsw->rx_ch_num = 1;
	cpsw->tx_ch_num = 1;

	if (is_valid_ether_addr(data->slave_data[0].mac_addr)) {
		memcpy(priv->mac_addr, data->slave_data[0].mac_addr, ETH_ALEN);
		dev_info(&pdev->dev, "Detected MACID = %pM\n", priv->mac_addr);
	} else {
		eth_random_addr(priv->mac_addr);
		dev_info(&pdev->dev, "Random MACID = %pM\n", priv->mac_addr);
	}

	memcpy(ndev->dev_addr, priv->mac_addr, ETH_ALEN);

	cpsw->slaves = devm_kzalloc(&pdev->dev,
				    sizeof(struct cpsw_slave) * data->slaves,
				    GFP_KERNEL);
	if (!cpsw->slaves) {
		ret = -ENOMEM;
		goto clean_runtime_disable_ret;
	}
	for (i = 0; i < data->slaves; i++)
		cpsw->slaves[i].slave_num = i;

	cpsw->slaves[0].ndev = ndev;
	priv->emac_port = 0;

	clk = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(clk)) {
		dev_err(priv->dev, "fck is not found\n");
		ret = -ENODEV;
		goto clean_runtime_disable_ret;
	}
	cpsw->bus_freq_mhz = clk_get_rate(clk) / 1000000;

	ss_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ss_regs = devm_ioremap_resource(&pdev->dev, ss_res);
	if (IS_ERR(ss_regs)) {
		ret = PTR_ERR(ss_regs);
		goto clean_runtime_disable_ret;
	}
	cpsw->regs = ss_regs;

	/* Need to enable clocks with runtime PM api to access module
	 * registers
	 */
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&pdev->dev);
		goto clean_runtime_disable_ret;
	}
	cpsw->version = readl(&cpsw->regs->id_ver);
	pm_runtime_put_sync(&pdev->dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	cpsw->wr_regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(cpsw->wr_regs)) {
		ret = PTR_ERR(cpsw->wr_regs);
		goto clean_runtime_disable_ret;
	}

	memset(&dma_params, 0, sizeof(dma_params));
	memset(&ale_params, 0, sizeof(ale_params));

	switch (cpsw->version) {
	case CPSW_VERSION_1:
		cpsw->host_port_regs = ss_regs + CPSW1_HOST_PORT_OFFSET;
		cpsw->cpts->reg      = ss_regs + CPSW1_CPTS_OFFSET;
		cpsw->hw_stats	     = ss_regs + CPSW1_HW_STATS;
		dma_params.dmaregs   = ss_regs + CPSW1_CPDMA_OFFSET;
		dma_params.txhdp     = ss_regs + CPSW1_STATERAM_OFFSET;
		ale_params.ale_regs  = ss_regs + CPSW1_ALE_OFFSET;
		slave_offset         = CPSW1_SLAVE_OFFSET;
		slave_size           = CPSW1_SLAVE_SIZE;
		sliver_offset        = CPSW1_SLIVER_OFFSET;
		dma_params.desc_mem_phys = 0;
		break;
	case CPSW_VERSION_2:
	case CPSW_VERSION_3:
	case CPSW_VERSION_4:
		cpsw->host_port_regs = ss_regs + CPSW2_HOST_PORT_OFFSET;
		cpsw->cpts->reg      = ss_regs + CPSW2_CPTS_OFFSET;
		cpsw->hw_stats	     = ss_regs + CPSW2_HW_STATS;
		dma_params.dmaregs   = ss_regs + CPSW2_CPDMA_OFFSET;
		dma_params.txhdp     = ss_regs + CPSW2_STATERAM_OFFSET;
		ale_params.ale_regs  = ss_regs + CPSW2_ALE_OFFSET;
		slave_offset         = CPSW2_SLAVE_OFFSET;
		slave_size           = CPSW2_SLAVE_SIZE;
		sliver_offset        = CPSW2_SLIVER_OFFSET;
		dma_params.desc_mem_phys =
			(u32 __force) ss_res->start + CPSW2_BD_OFFSET;
		break;
	default:
		dev_err(priv->dev, "unknown version 0x%08x\n", cpsw->version);
		ret = -ENODEV;
		goto clean_runtime_disable_ret;
	}
	for (i = 0; i < cpsw->data.slaves; i++) {
		struct cpsw_slave *slave = &cpsw->slaves[i];

		cpsw_slave_init(slave, cpsw, slave_offset, sliver_offset);
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

	cpsw->dma = cpdma_ctlr_create(&dma_params);
	if (!cpsw->dma) {
		dev_err(priv->dev, "error initializing dma\n");
		ret = -ENOMEM;
		goto clean_runtime_disable_ret;
	}

	cpsw->txch[0] = cpdma_chan_create(cpsw->dma, 0, cpsw_tx_handler, 0);
	cpsw->rxch[0] = cpdma_chan_create(cpsw->dma, 0, cpsw_rx_handler, 1);
	if (WARN_ON(!cpsw->rxch[0] || !cpsw->txch[0])) {
		dev_err(priv->dev, "error initializing dma channels\n");
		ret = -ENOMEM;
		goto clean_dma_ret;
	}

	ale_params.dev			= &ndev->dev;
	ale_params.ale_ageout		= ale_ageout;
	ale_params.ale_entries		= data->ale_entries;
	ale_params.ale_ports		= data->slaves;

	cpsw->ale = cpsw_ale_create(&ale_params);
	if (!cpsw->ale) {
		dev_err(priv->dev, "error initializing ale engine\n");
		ret = -ENODEV;
		goto clean_dma_ret;
	}

	ndev->irq = platform_get_irq(pdev, 1);
	if (ndev->irq < 0) {
		dev_err(priv->dev, "error getting irq resource\n");
		ret = ndev->irq;
		goto clean_ale_ret;
	}

	of_id = of_match_device(cpsw_of_mtable, &pdev->dev);
	if (of_id) {
		pdev->id_entry = of_id->data;
		if (pdev->id_entry->driver_data)
			cpsw->quirk_irq = true;
	}

	/* Grab RX and TX IRQs. Note that we also have RX_THRESHOLD and
	 * MISC IRQs which are always kept disabled with this driver so
	 * we will not request them.
	 *
	 * If anyone wants to implement support for those, make sure to
	 * first request and append them to irqs_table array.
	 */

	/* RX IRQ */
	irq = platform_get_irq(pdev, 1);
	if (irq < 0) {
		ret = irq;
		goto clean_ale_ret;
	}

	cpsw->irqs_table[0] = irq;
	ret = devm_request_irq(&pdev->dev, irq, cpsw_rx_interrupt,
			       0, dev_name(&pdev->dev), cpsw);
	if (ret < 0) {
		dev_err(priv->dev, "error attaching irq (%d)\n", ret);
		goto clean_ale_ret;
	}

	/* TX IRQ */
	irq = platform_get_irq(pdev, 2);
	if (irq < 0) {
		ret = irq;
		goto clean_ale_ret;
	}

	cpsw->irqs_table[1] = irq;
	ret = devm_request_irq(&pdev->dev, irq, cpsw_tx_interrupt,
			       0, dev_name(&pdev->dev), cpsw);
	if (ret < 0) {
		dev_err(priv->dev, "error attaching irq (%d)\n", ret);
		goto clean_ale_ret;
	}

	ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	ndev->netdev_ops = &cpsw_netdev_ops;
	ndev->ethtool_ops = &cpsw_ethtool_ops;
	netif_napi_add(ndev, &cpsw->napi_rx, cpsw_rx_poll, CPSW_POLL_WEIGHT);
	netif_tx_napi_add(ndev, &cpsw->napi_tx, cpsw_tx_poll, CPSW_POLL_WEIGHT);

	/* register the network device */
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ret = register_netdev(ndev);
	if (ret) {
		dev_err(priv->dev, "error registering net device\n");
		ret = -ENODEV;
		goto clean_ale_ret;
	}

	cpsw_notice(priv, probe, "initialized device (regs %pa, irq %d)\n",
		    &ss_res->start, ndev->irq);

	if (cpsw->data.dual_emac) {
		ret = cpsw_probe_dual_emac(priv);
		if (ret) {
			cpsw_err(priv, probe, "error probe slave 2 emac interface\n");
			goto clean_ale_ret;
		}
	}

	return 0;

clean_ale_ret:
	cpsw_ale_destroy(cpsw->ale);
clean_dma_ret:
	cpdma_ctlr_destroy(cpsw->dma);
clean_runtime_disable_ret:
	pm_runtime_disable(&pdev->dev);
clean_ndev_ret:
	free_netdev(priv->ndev);
	return ret;
}

static int cpsw_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct cpsw_common *cpsw = ndev_to_cpsw(ndev);
	int ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&pdev->dev);
		return ret;
	}

	if (cpsw->data.dual_emac)
		unregister_netdev(cpsw->slaves[1].ndev);
	unregister_netdev(ndev);

	cpsw_ale_destroy(cpsw->ale);
	cpdma_ctlr_destroy(cpsw->dma);
	of_platform_depopulate(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	if (cpsw->data.dual_emac)
		free_netdev(cpsw->slaves[1].ndev);
	free_netdev(ndev);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cpsw_suspend(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct net_device	*ndev = platform_get_drvdata(pdev);
	struct cpsw_common	*cpsw = ndev_to_cpsw(ndev);

	if (cpsw->data.dual_emac) {
		int i;

		for (i = 0; i < cpsw->data.slaves; i++) {
			if (netif_running(cpsw->slaves[i].ndev))
				cpsw_ndo_stop(cpsw->slaves[i].ndev);
		}
	} else {
		if (netif_running(ndev))
			cpsw_ndo_stop(ndev);
	}

	/* Select sleep pin state */
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int cpsw_resume(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct net_device	*ndev = platform_get_drvdata(pdev);
	struct cpsw_common	*cpsw = netdev_priv(ndev);

	/* Select default pin state */
	pinctrl_pm_select_default_state(dev);

	if (cpsw->data.dual_emac) {
		int i;

		for (i = 0; i < cpsw->data.slaves; i++) {
			if (netif_running(cpsw->slaves[i].ndev))
				cpsw_ndo_open(cpsw->slaves[i].ndev);
		}
	} else {
		if (netif_running(ndev))
			cpsw_ndo_open(ndev);
	}
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
