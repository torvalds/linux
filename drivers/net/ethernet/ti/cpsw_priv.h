/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Texas Instruments Ethernet Switch Driver
 */

#ifndef DRIVERS_NET_ETHERNET_TI_CPSW_PRIV_H_
#define DRIVERS_NET_ETHERNET_TI_CPSW_PRIV_H_

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
#define CPSW_ALE_PORTS_NUM	3
#define CPSW_SLAVE_PORTS_NUM	2
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
#define CPSW1_WR_OFFSET		0x900

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
#define CPSW2_WR_OFFSET		0x1200

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
#define CPSW_ALE_AGEOUT_DEFAULT		10 /* sec */
#define CPSW_ALE_NUM_ENTRIES		1024
#define CPSW_FIFO_QUEUE_TYPE_SHIFT	16
#define CPSW_FIFO_SHAPE_EN_SHIFT	16
#define CPSW_FIFO_RATE_EN_SHIFT		20
#define CPSW_TC_NUM			4
#define CPSW_FIFO_SHAPERS_NUM		(CPSW_TC_NUM - 1)
#define CPSW_PCT_MASK			0x7f
#define CPSW_BD_RAM_SIZE		0x2000

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

struct cpsw_slave_data {
	struct device_node *slave_node;
	struct device_node *phy_node;
	char		phy_id[MII_BUS_ID_SIZE];
	phy_interface_t	phy_if;
	u8		mac_addr[ETH_ALEN];
	u16		dual_emac_res_vlan;	/* Reserved VLAN for DualEMAC */
	struct phy	*ifphy;
	bool		disabled;
};

struct cpsw_platform_data {
	struct cpsw_slave_data	*slave_data;
	u32	ss_reg_ofs;	/* Subsystem control register offset */
	u32	channels;	/* number of cpdma channels (symmetric) */
	u32	slaves;		/* number of slave cpgmac ports */
	u32	active_slave;/* time stamping, ethtool and SIOCGMIIPHY slave */
	u32	ale_entries;	/* ale table size */
	u32	bd_ram_size;	/*buffer descriptor ram size */
	u32	mac_control;	/* Mac control register */
	u16	default_vlan;	/* Def VLAN for ALE lookup in VLAN aware mode*/
	bool	dual_emac;	/* Enable Dual EMAC mode */
};

struct cpsw_slave {
	void __iomem			*regs;
	int				slave_num;
	u32				mac_control;
	struct cpsw_slave_data		*data;
	struct phy_device		*phy;
	struct net_device		*ndev;
	u32				port_vlan;
	struct cpsw_sl			*mac_sl;
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
	int				descs_pool_size;
	struct cpsw_slave		*slaves;
	struct cpdma_ctlr		*dma;
	struct cpsw_vector		txv[CPSW_MAX_QUEUES];
	struct cpsw_vector		rxv[CPSW_MAX_QUEUES];
	struct cpsw_ale			*ale;
	bool				quirk_irq;
	bool				rx_irq_disabled;
	bool				tx_irq_disabled;
	u32 irqs_table[IRQ_NUM];
	int misc_irq;
	struct cpts			*cpts;
	struct devlink *devlink;
	int				rx_ch_num, tx_ch_num;
	int				speed;
	int				usage_count;
	struct page_pool		*page_pool[CPSW_MAX_QUEUES];
	u8 br_members;
	struct net_device *hw_bridge_dev;
	bool ale_bypass;
	u8 base_mac[ETH_ALEN];
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
	struct bpf_prog			*xdp_prog;
	struct xdp_rxq_info		xdp_rxq[CPSW_MAX_QUEUES];
	struct xdp_attachment_info	xdpi;

	u32 emac_port;
	struct cpsw_common *cpsw;
	int offload_fwd_mark;
};

#define ndev_to_cpsw(ndev) (((struct cpsw_priv *)netdev_priv(ndev))->cpsw)
#define napi_to_cpsw(napi)	container_of(napi, struct cpsw_common, napi)

extern int (*cpsw_slave_index)(struct cpsw_common *cpsw,
			       struct cpsw_priv *priv);

struct addr_sync_ctx {
	struct net_device *ndev;
	const u8 *addr;		/* address to be synched */
	int consumed;		/* number of address instances */
	int flush;		/* flush flag */
};

#define CPSW_XMETA_OFFSET	ALIGN(sizeof(struct xdp_frame), sizeof(long))

#define CPSW_XDP_CONSUMED		1
#define CPSW_XDP_PASS			0

struct __aligned(sizeof(long)) cpsw_meta_xdp {
	struct net_device *ndev;
	int ch;
};

/* The buf includes headroom compatible with both skb and xdpf */
#define CPSW_HEADROOM_NA (max(XDP_PACKET_HEADROOM, NET_SKB_PAD) + NET_IP_ALIGN)
#define CPSW_HEADROOM  ALIGN(CPSW_HEADROOM_NA, sizeof(long))

static inline int cpsw_is_xdpf_handle(void *handle)
{
	return (unsigned long)handle & BIT(0);
}

static inline void *cpsw_xdpf_to_handle(struct xdp_frame *xdpf)
{
	return (void *)((unsigned long)xdpf | BIT(0));
}

static inline struct xdp_frame *cpsw_handle_to_xdpf(void *handle)
{
	return (struct xdp_frame *)((unsigned long)handle & ~BIT(0));
}

int cpsw_init_common(struct cpsw_common *cpsw, void __iomem *ss_regs,
		     int ale_ageout, phys_addr_t desc_mem_phys,
		     int descs_pool_size);
void cpsw_split_res(struct cpsw_common *cpsw);
int cpsw_fill_rx_channels(struct cpsw_priv *priv);
void cpsw_intr_enable(struct cpsw_common *cpsw);
void cpsw_intr_disable(struct cpsw_common *cpsw);
void cpsw_tx_handler(void *token, int len, int status);
int cpsw_create_xdp_rxqs(struct cpsw_common *cpsw);
void cpsw_destroy_xdp_rxqs(struct cpsw_common *cpsw);
int cpsw_ndo_bpf(struct net_device *ndev, struct netdev_bpf *bpf);
int cpsw_xdp_tx_frame(struct cpsw_priv *priv, struct xdp_frame *xdpf,
		      struct page *page, int port);
int cpsw_run_xdp(struct cpsw_priv *priv, int ch, struct xdp_buff *xdp,
		 struct page *page, int port);
irqreturn_t cpsw_tx_interrupt(int irq, void *dev_id);
irqreturn_t cpsw_rx_interrupt(int irq, void *dev_id);
irqreturn_t cpsw_misc_interrupt(int irq, void *dev_id);
int cpsw_tx_mq_poll(struct napi_struct *napi_tx, int budget);
int cpsw_tx_poll(struct napi_struct *napi_tx, int budget);
int cpsw_rx_mq_poll(struct napi_struct *napi_rx, int budget);
int cpsw_rx_poll(struct napi_struct *napi_rx, int budget);
void cpsw_rx_vlan_encap(struct sk_buff *skb);
void soft_reset(const char *module, void __iomem *reg);
void cpsw_set_slave_mac(struct cpsw_slave *slave, struct cpsw_priv *priv);
void cpsw_ndo_tx_timeout(struct net_device *ndev, unsigned int txqueue);
int cpsw_need_resplit(struct cpsw_common *cpsw);
int cpsw_ndo_ioctl(struct net_device *dev, struct ifreq *req, int cmd);
int cpsw_ndo_set_tx_maxrate(struct net_device *ndev, int queue, u32 rate);
int cpsw_ndo_setup_tc(struct net_device *ndev, enum tc_setup_type type,
		      void *type_data);
bool cpsw_shp_is_off(struct cpsw_priv *priv);
void cpsw_cbs_resume(struct cpsw_slave *slave, struct cpsw_priv *priv);
void cpsw_mqprio_resume(struct cpsw_slave *slave, struct cpsw_priv *priv);

/* ethtool */
u32 cpsw_get_msglevel(struct net_device *ndev);
void cpsw_set_msglevel(struct net_device *ndev, u32 value);
int cpsw_get_coalesce(struct net_device *ndev, struct ethtool_coalesce *coal);
int cpsw_set_coalesce(struct net_device *ndev, struct ethtool_coalesce *coal);
int cpsw_get_sset_count(struct net_device *ndev, int sset);
void cpsw_get_strings(struct net_device *ndev, u32 stringset, u8 *data);
void cpsw_get_ethtool_stats(struct net_device *ndev,
			    struct ethtool_stats *stats, u64 *data);
void cpsw_get_pauseparam(struct net_device *ndev,
			 struct ethtool_pauseparam *pause);
void cpsw_get_wol(struct net_device *ndev, struct ethtool_wolinfo *wol);
int cpsw_set_wol(struct net_device *ndev, struct ethtool_wolinfo *wol);
int cpsw_get_regs_len(struct net_device *ndev);
void cpsw_get_regs(struct net_device *ndev, struct ethtool_regs *regs, void *p);
int cpsw_ethtool_op_begin(struct net_device *ndev);
void cpsw_ethtool_op_complete(struct net_device *ndev);
void cpsw_get_channels(struct net_device *ndev, struct ethtool_channels *ch);
int cpsw_get_link_ksettings(struct net_device *ndev,
			    struct ethtool_link_ksettings *ecmd);
int cpsw_set_link_ksettings(struct net_device *ndev,
			    const struct ethtool_link_ksettings *ecmd);
int cpsw_get_eee(struct net_device *ndev, struct ethtool_eee *edata);
int cpsw_set_eee(struct net_device *ndev, struct ethtool_eee *edata);
int cpsw_nway_reset(struct net_device *ndev);
void cpsw_get_ringparam(struct net_device *ndev,
			struct ethtool_ringparam *ering);
int cpsw_set_ringparam(struct net_device *ndev,
		       struct ethtool_ringparam *ering);
int cpsw_set_channels_common(struct net_device *ndev,
			     struct ethtool_channels *chs,
			     cpdma_handler_fn rx_handler);
int cpsw_get_ts_info(struct net_device *ndev, struct ethtool_ts_info *info);

#endif /* DRIVERS_NET_ETHERNET_TI_CPSW_PRIV_H_ */
