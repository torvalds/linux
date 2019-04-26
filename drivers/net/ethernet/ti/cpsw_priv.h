/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Texas Instruments Ethernet Switch Driver
 */

#ifndef DRIVERS_NET_ETHERNET_TI_CPSW_PRIV_H_
#define DRIVERS_NET_ETHERNET_TI_CPSW_PRIV_H_

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
#define CPSW_ALE_PORTS_NUM	3
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
#define CPSW_RX_VLAN_ENCAP_HDR_SIZE		4
#define CPSW_MIN_PACKET_SIZE	(VLAN_ETH_ZLEN)
#define CPSW_MAX_PACKET_SIZE	(VLAN_ETH_FRAME_LEN +\
				 ETH_FCS_LEN +\
				 CPSW_RX_VLAN_ENCAP_HDR_SIZE)

#define RX_PRIORITY_MAPPING	0x76543210
#define TX_PRIORITY_MAPPING	0x33221100
#define CPDMA_TX_PRIORITY_MAP	0x76543210

#define CPSW_VLAN_AWARE		BIT(1)
#define CPSW_RX_VLAN_ENCAP	BIT(2)
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

#define IRQ_NUM			2
#define CPSW_MAX_QUEUES		8
#define CPSW_CPDMA_DESCS_POOL_SIZE_DEFAULT 256
#define CPSW_FIFO_QUEUE_TYPE_SHIFT	16
#define CPSW_FIFO_SHAPE_EN_SHIFT	16
#define CPSW_FIFO_RATE_EN_SHIFT		20
#define CPSW_TC_NUM			4
#define CPSW_FIFO_SHAPERS_NUM		(CPSW_TC_NUM - 1)
#define CPSW_PCT_MASK			0x7f

#define CPSW_RX_VLAN_ENCAP_HDR_PRIO_SHIFT	29
#define CPSW_RX_VLAN_ENCAP_HDR_PRIO_MSK		GENMASK(2, 0)
#define CPSW_RX_VLAN_ENCAP_HDR_VID_SHIFT	16
#define CPSW_RX_VLAN_ENCAP_HDR_PKT_TYPE_SHIFT	8
#define CPSW_RX_VLAN_ENCAP_HDR_PKT_TYPE_MSK	GENMASK(1, 0)
enum {
	CPSW_RX_VLAN_ENCAP_HDR_PKT_VLAN_TAG = 0,
	CPSW_RX_VLAN_ENCAP_HDR_PKT_RESERV,
	CPSW_RX_VLAN_ENCAP_HDR_PKT_PRIO_TAG,
	CPSW_RX_VLAN_ENCAP_HDR_PKT_UNTAG,
};

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
#define PASS_PRI_TAGGED     BIT(24) /* Pass Priority Tagged */
#define VLAN_LTYPE2_EN      BIT(21) /* VLAN LTYPE 2 enable */
#define VLAN_LTYPE1_EN      BIT(20) /* VLAN LTYPE 1 enable */
#define DSCP_PRI_EN         BIT(16) /* DSCP Priority Enable */
#define TS_107              BIT(15) /* Tyme Sync Dest IP Address 107 */
#define TS_320              BIT(14) /* Time Sync Dest Port 320 enable */
#define TS_319              BIT(13) /* Time Sync Dest Port 319 enable */
#define TS_132              BIT(12) /* Time Sync Dest IP Addr 132 enable */
#define TS_131              BIT(11) /* Time Sync Dest IP Addr 131 enable */
#define TS_130              BIT(10) /* Time Sync Dest IP Addr 130 enable */
#define TS_129              BIT(9)  /* Time Sync Dest IP Addr 129 enable */
#define TS_TTL_NONZERO      BIT(8)  /* Time Sync Time To Live Non-zero enable */
#define TS_ANNEX_F_EN       BIT(6)  /* Time Sync Annex F enable */
#define TS_ANNEX_D_EN       BIT(4)  /* Time Sync Annex D enable */
#define TS_LTYPE2_EN        BIT(3)  /* Time Sync LTYPE 2 enable */
#define TS_LTYPE1_EN        BIT(2)  /* Time Sync LTYPE 1 enable */
#define TS_TX_EN            BIT(1)  /* Time Sync Transmit Enable */
#define TS_RX_EN            BIT(0)  /* Time Sync Receive Enable */

#define CTRL_V2_TS_BITS \
	(TS_320 | TS_319 | TS_132 | TS_131 | TS_130 | TS_129 |\
	 TS_TTL_NONZERO  | TS_ANNEX_D_EN | TS_LTYPE1_EN | VLAN_LTYPE1_EN)

#define CTRL_V2_ALL_TS_MASK (CTRL_V2_TS_BITS | TS_TX_EN | TS_RX_EN)
#define CTRL_V2_TX_TS_BITS  (CTRL_V2_TS_BITS | TS_TX_EN)
#define CTRL_V2_RX_TS_BITS  (CTRL_V2_TS_BITS | TS_RX_EN)


#define CTRL_V3_TS_BITS \
	(TS_107 | TS_320 | TS_319 | TS_132 | TS_131 | TS_130 | TS_129 |\
	 TS_TTL_NONZERO | TS_ANNEX_F_EN | TS_ANNEX_D_EN |\
	 TS_LTYPE1_EN | VLAN_LTYPE1_EN)

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

#define CPSW_MAX_BLKS_TX		15
#define CPSW_MAX_BLKS_TX_SHIFT		4
#define CPSW_MAX_BLKS_RX		5

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

struct cpsw_slave_data {
	struct device_node *phy_node;
	char		phy_id[MII_BUS_ID_SIZE];
	int		phy_if;
	u8		mac_addr[ETH_ALEN];
	u16		dual_emac_res_vlan;	/* Reserved VLAN for DualEMAC */
	struct phy	*ifphy;
};

struct cpsw_platform_data {
	struct cpsw_slave_data	*slave_data;
	u32	ss_reg_ofs;	/* Subsystem control register offset */
	u32	channels;	/* number of cpdma channels (symmetric) */
	u32	slaves;		/* number of slave cpgmac ports */
	u32	active_slave; /* time stamping, ethtool and SIOCGMIIPHY slave */
	u32	ale_entries;	/* ale table size */
	u32	bd_ram_size;  /*buffer descriptor ram size */
	u32	mac_control;	/* Mac control register */
	u16	default_vlan;	/* Def VLAN for ALE lookup in VLAN aware mode*/
	bool	dual_emac;	/* Enable Dual EMAC mode */
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
};

static inline u32 slave_read(struct cpsw_slave *slave, u32 offset)
{
	return readl_relaxed(slave->regs + offset);
}

static inline void slave_write(struct cpsw_slave *slave, u32 val, u32 offset)
{
	writel_relaxed(val, slave->regs + offset);
}

struct cpsw_vector {
	struct cpdma_chan *ch;
	int budget;
};

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
	struct cpsw_vector		txv[CPSW_MAX_QUEUES];
	struct cpsw_vector		rxv[CPSW_MAX_QUEUES];
	struct cpsw_ale			*ale;
	bool				quirk_irq;
	bool				rx_irq_disabled;
	bool				tx_irq_disabled;
	u32 irqs_table[IRQ_NUM];
	struct cpts			*cpts;
	int				rx_ch_num, tx_ch_num;
	int				speed;
	int				usage_count;
};

struct cpsw_priv {
	struct net_device		*ndev;
	struct device			*dev;
	u32				msg_enable;
	u8				mac_addr[ETH_ALEN];
	bool				rx_pause;
	bool				tx_pause;
	bool				mqprio_hw;
	int				fifo_bw[CPSW_TC_NUM];
	int				shp_cfg_speed;
	int				tx_ts_enabled;
	int				rx_ts_enabled;
	u32 emac_port;
	struct cpsw_common *cpsw;
};

#define CPSW_STATS_COMMON_LEN	ARRAY_SIZE(cpsw_gstrings_stats)
#define CPSW_STATS_CH_LEN	ARRAY_SIZE(cpsw_gstrings_ch_stats)

#define ndev_to_cpsw(ndev) (((struct cpsw_priv *)netdev_priv(ndev))->cpsw)
#define napi_to_cpsw(napi)	container_of(napi, struct cpsw_common, napi)

#define cpsw_slave_index(cpsw, priv)				\
		((cpsw->data.dual_emac) ? priv->emac_port :	\
		cpsw->data.active_slave)

static inline int cpsw_get_slave_port(u32 slave_num)
{
	return slave_num + 1;
}

struct addr_sync_ctx {
	struct net_device *ndev;
	const u8 *addr;		/* address to be synched */
	int consumed;		/* number of address instances */
	int flush;		/* flush flag */
};

#endif /* DRIVERS_NET_ETHERNET_TI_CPSW_PRIV_H_ */
