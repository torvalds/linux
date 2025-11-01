/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 AIROHA Inc
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 */

#ifndef AIROHA_ETH_H
#define AIROHA_ETH_H

#include <linux/debugfs.h>
#include <linux/etherdevice.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/reset.h>
#include <linux/soc/airoha/airoha_offload.h>
#include <net/dsa.h>

#define AIROHA_MAX_NUM_GDM_PORTS	4
#define AIROHA_MAX_NUM_QDMA		2
#define AIROHA_MAX_NUM_IRQ_BANKS	4
#define AIROHA_MAX_DSA_PORTS		7
#define AIROHA_MAX_NUM_RSTS		3
#define AIROHA_MAX_NUM_XSI_RSTS		5
#define AIROHA_MAX_MTU			9216
#define AIROHA_MAX_PACKET_SIZE		2048
#define AIROHA_NUM_QOS_CHANNELS		4
#define AIROHA_NUM_QOS_QUEUES		8
#define AIROHA_NUM_TX_RING		32
#define AIROHA_NUM_RX_RING		32
#define AIROHA_NUM_NETDEV_TX_RINGS	(AIROHA_NUM_TX_RING + \
					 AIROHA_NUM_QOS_CHANNELS)
#define AIROHA_FE_MC_MAX_VLAN_TABLE	64
#define AIROHA_FE_MC_MAX_VLAN_PORT	16
#define AIROHA_NUM_TX_IRQ		2
#define HW_DSCP_NUM			2048
#define IRQ_QUEUE_LEN(_n)		((_n) ? 1024 : 2048)
#define TX_DSCP_NUM			1024
#define RX_DSCP_NUM(_n)			\
	((_n) ==  2 ? 128 :		\
	 (_n) == 11 ? 128 :		\
	 (_n) == 15 ? 128 :		\
	 (_n) ==  0 ? 1024 : 16)

#define PSE_RSV_PAGES			128
#define PSE_QUEUE_RSV_PAGES		64

#define QDMA_METER_IDX(_n)		((_n) & 0xff)
#define QDMA_METER_GROUP(_n)		(((_n) >> 8) & 0x3)

#define PPE_NUM				2
#define PPE1_SRAM_NUM_ENTRIES		(8 * 1024)
#define PPE_SRAM_NUM_ENTRIES		(2 * PPE1_SRAM_NUM_ENTRIES)
#ifdef CONFIG_NET_AIROHA_FLOW_STATS
#define PPE1_STATS_NUM_ENTRIES		(4 * 1024)
#else
#define PPE1_STATS_NUM_ENTRIES		0
#endif /* CONFIG_NET_AIROHA_FLOW_STATS */
#define PPE_STATS_NUM_ENTRIES		(2 * PPE1_STATS_NUM_ENTRIES)
#define PPE1_SRAM_NUM_DATA_ENTRIES	(PPE1_SRAM_NUM_ENTRIES - PPE1_STATS_NUM_ENTRIES)
#define PPE_SRAM_NUM_DATA_ENTRIES	(2 * PPE1_SRAM_NUM_DATA_ENTRIES)
#define PPE_DRAM_NUM_ENTRIES		(16 * 1024)
#define PPE_NUM_ENTRIES			(PPE_SRAM_NUM_ENTRIES + PPE_DRAM_NUM_ENTRIES)
#define PPE_HASH_MASK			(PPE_NUM_ENTRIES - 1)
#define PPE_ENTRY_SIZE			80
#define PPE_RAM_NUM_ENTRIES_SHIFT(_n)	(__ffs((_n) >> 10))

#define MTK_HDR_LEN			4
#define MTK_HDR_XMIT_TAGGED_TPID_8100	1
#define MTK_HDR_XMIT_TAGGED_TPID_88A8	2

enum {
	QDMA_INT_REG_IDX0,
	QDMA_INT_REG_IDX1,
	QDMA_INT_REG_IDX2,
	QDMA_INT_REG_IDX3,
	QDMA_INT_REG_IDX4,
	QDMA_INT_REG_MAX
};

enum {
	HSGMII_LAN_PCIE0_SRCPORT = 0x16,
	HSGMII_LAN_PCIE1_SRCPORT,
	HSGMII_LAN_ETH_SRCPORT,
	HSGMII_LAN_USB_SRCPORT,
};

enum {
	XSI_PCIE0_VIP_PORT_MASK	= BIT(22),
	XSI_PCIE1_VIP_PORT_MASK	= BIT(23),
	XSI_USB_VIP_PORT_MASK	= BIT(25),
	XSI_ETH_VIP_PORT_MASK	= BIT(24),
};

enum {
	DEV_STATE_INITIALIZED,
};

enum {
	CDM_CRSN_QSEL_Q1 = 1,
	CDM_CRSN_QSEL_Q5 = 5,
	CDM_CRSN_QSEL_Q6 = 6,
	CDM_CRSN_QSEL_Q15 = 15,
};

enum {
	CRSN_08 = 0x8,
	CRSN_21 = 0x15, /* KA */
	CRSN_22 = 0x16, /* hit bind and force route to CPU */
	CRSN_24 = 0x18,
	CRSN_25 = 0x19,
};

enum {
	FE_PSE_PORT_CDM1,
	FE_PSE_PORT_GDM1,
	FE_PSE_PORT_GDM2,
	FE_PSE_PORT_GDM3,
	FE_PSE_PORT_PPE1,
	FE_PSE_PORT_CDM2,
	FE_PSE_PORT_CDM3,
	FE_PSE_PORT_CDM4,
	FE_PSE_PORT_PPE2,
	FE_PSE_PORT_GDM4,
	FE_PSE_PORT_CDM5,
	FE_PSE_PORT_DROP = 0xf,
};

enum tx_sched_mode {
	TC_SCH_WRR8,
	TC_SCH_SP,
	TC_SCH_WRR7,
	TC_SCH_WRR6,
	TC_SCH_WRR5,
	TC_SCH_WRR4,
	TC_SCH_WRR3,
	TC_SCH_WRR2,
};

enum trtcm_unit_type {
	TRTCM_BYTE_UNIT,
	TRTCM_PACKET_UNIT,
};

enum trtcm_param_type {
	TRTCM_MISC_MODE, /* meter_en, pps_mode, tick_sel */
	TRTCM_TOKEN_RATE_MODE,
	TRTCM_BUCKETSIZE_SHIFT_MODE,
	TRTCM_BUCKET_COUNTER_MODE,
};

enum trtcm_mode_type {
	TRTCM_COMMIT_MODE,
	TRTCM_PEAK_MODE,
};

enum trtcm_param {
	TRTCM_TICK_SEL = BIT(0),
	TRTCM_PKT_MODE = BIT(1),
	TRTCM_METER_MODE = BIT(2),
};

#define MIN_TOKEN_SIZE				4096
#define MAX_TOKEN_SIZE_OFFSET			17
#define TRTCM_TOKEN_RATE_MASK			GENMASK(23, 6)
#define TRTCM_TOKEN_RATE_FRACTION_MASK		GENMASK(5, 0)

struct airoha_queue_entry {
	union {
		void *buf;
		struct sk_buff *skb;
	};
	dma_addr_t dma_addr;
	u16 dma_len;
};

struct airoha_queue {
	struct airoha_qdma *qdma;

	/* protect concurrent queue accesses */
	spinlock_t lock;
	struct airoha_queue_entry *entry;
	struct airoha_qdma_desc *desc;
	u16 head;
	u16 tail;

	int queued;
	int ndesc;
	int free_thr;
	int buf_size;

	struct napi_struct napi;
	struct page_pool *page_pool;
	struct sk_buff *skb;
};

struct airoha_tx_irq_queue {
	struct airoha_qdma *qdma;

	struct napi_struct napi;

	int size;
	u32 *q;
};

struct airoha_hw_stats {
	/* protect concurrent hw_stats accesses */
	spinlock_t lock;
	struct u64_stats_sync syncp;

	/* get_stats64 */
	u64 rx_ok_pkts;
	u64 tx_ok_pkts;
	u64 rx_ok_bytes;
	u64 tx_ok_bytes;
	u64 rx_multicast;
	u64 rx_errors;
	u64 rx_drops;
	u64 tx_drops;
	u64 rx_crc_error;
	u64 rx_over_errors;
	/* ethtool stats */
	u64 tx_broadcast;
	u64 tx_multicast;
	u64 tx_len[7];
	u64 rx_broadcast;
	u64 rx_fragment;
	u64 rx_jabber;
	u64 rx_len[7];
};

enum {
	AIROHA_FOE_STATE_INVALID,
	AIROHA_FOE_STATE_UNBIND,
	AIROHA_FOE_STATE_BIND,
	AIROHA_FOE_STATE_FIN
};

enum {
	PPE_PKT_TYPE_IPV4_HNAPT = 0,
	PPE_PKT_TYPE_IPV4_ROUTE = 1,
	PPE_PKT_TYPE_BRIDGE = 2,
	PPE_PKT_TYPE_IPV4_DSLITE = 3,
	PPE_PKT_TYPE_IPV6_ROUTE_3T = 4,
	PPE_PKT_TYPE_IPV6_ROUTE_5T = 5,
	PPE_PKT_TYPE_IPV6_6RD = 7,
};

#define AIROHA_FOE_MAC_SMAC_ID		GENMASK(20, 16)
#define AIROHA_FOE_MAC_PPPOE_ID		GENMASK(15, 0)

#define AIROHA_FOE_MAC_WDMA_QOS		GENMASK(15, 12)
#define AIROHA_FOE_MAC_WDMA_BAND	BIT(11)
#define AIROHA_FOE_MAC_WDMA_WCID	GENMASK(10, 0)

struct airoha_foe_mac_info_common {
	u16 vlan1;
	u16 etype;

	u32 dest_mac_hi;

	u16 vlan2;
	u16 dest_mac_lo;

	u32 src_mac_hi;
};

struct airoha_foe_mac_info {
	struct airoha_foe_mac_info_common common;

	u16 pppoe_id;
	u16 src_mac_lo;

	u32 meter;
};

#define AIROHA_FOE_IB1_UNBIND_PREBIND		BIT(24)
#define AIROHA_FOE_IB1_UNBIND_PACKETS		GENMASK(23, 8)
#define AIROHA_FOE_IB1_UNBIND_TIMESTAMP		GENMASK(7, 0)

#define AIROHA_FOE_IB1_BIND_STATIC		BIT(31)
#define AIROHA_FOE_IB1_BIND_UDP			BIT(30)
#define AIROHA_FOE_IB1_BIND_STATE		GENMASK(29, 28)
#define AIROHA_FOE_IB1_BIND_PACKET_TYPE		GENMASK(27, 25)
#define AIROHA_FOE_IB1_BIND_TTL			BIT(24)
#define AIROHA_FOE_IB1_BIND_TUNNEL_DECAP	BIT(23)
#define AIROHA_FOE_IB1_BIND_PPPOE		BIT(22)
#define AIROHA_FOE_IB1_BIND_VPM			GENMASK(21, 20)
#define AIROHA_FOE_IB1_BIND_VLAN_LAYER		GENMASK(19, 16)
#define AIROHA_FOE_IB1_BIND_KEEPALIVE		BIT(15)
#define AIROHA_FOE_IB1_BIND_TIMESTAMP		GENMASK(14, 0)

#define AIROHA_FOE_IB2_DSCP			GENMASK(31, 24)
#define AIROHA_FOE_IB2_PORT_AG			GENMASK(23, 13)
#define AIROHA_FOE_IB2_PCP			BIT(12)
#define AIROHA_FOE_IB2_MULTICAST		BIT(11)
#define AIROHA_FOE_IB2_FAST_PATH		BIT(10)
#define AIROHA_FOE_IB2_PSE_QOS			BIT(9)
#define AIROHA_FOE_IB2_PSE_PORT			GENMASK(8, 5)
#define AIROHA_FOE_IB2_NBQ			GENMASK(4, 0)

#define AIROHA_FOE_ACTDP			GENMASK(31, 24)
#define AIROHA_FOE_SHAPER_ID			GENMASK(23, 16)
#define AIROHA_FOE_CHANNEL			GENMASK(15, 11)
#define AIROHA_FOE_QID				GENMASK(10, 8)
#define AIROHA_FOE_DPI				BIT(7)
#define AIROHA_FOE_TUNNEL			BIT(6)
#define AIROHA_FOE_TUNNEL_ID			GENMASK(5, 0)

#define AIROHA_FOE_TUNNEL_MTU			GENMASK(31, 16)
#define AIROHA_FOE_ACNT_GRP3			GENMASK(15, 9)
#define AIROHA_FOE_METER_GRP3			GENMASK(8, 5)
#define AIROHA_FOE_METER_GRP2			GENMASK(4, 0)

struct airoha_foe_bridge {
	u32 dest_mac_hi;

	u16 src_mac_hi;
	u16 dest_mac_lo;

	u32 src_mac_lo;

	u32 ib2;

	u32 rsv[5];

	u32 data;

	struct airoha_foe_mac_info l2;
};

struct airoha_foe_ipv4_tuple {
	u32 src_ip;
	u32 dest_ip;
	union {
		struct {
			u16 dest_port;
			u16 src_port;
		};
		struct {
			u8 protocol;
			u8 _pad[3]; /* fill with 0xa5a5a5 */
		};
		u32 ports;
	};
};

struct airoha_foe_ipv4 {
	struct airoha_foe_ipv4_tuple orig_tuple;

	u32 ib2;

	struct airoha_foe_ipv4_tuple new_tuple;

	u32 rsv[2];

	u32 data;

	struct airoha_foe_mac_info l2;
};

struct airoha_foe_ipv4_dslite {
	struct airoha_foe_ipv4_tuple ip4;

	u32 ib2;

	u8 flow_label[3];
	u8 priority;

	u32 rsv[4];

	u32 data;

	struct airoha_foe_mac_info l2;
};

struct airoha_foe_ipv6 {
	u32 src_ip[4];
	u32 dest_ip[4];

	union {
		struct {
			u16 dest_port;
			u16 src_port;
		};
		struct {
			u8 protocol;
			u8 pad[3];
		};
		u32 ports;
	};

	u32 data;

	u32 ib2;

	struct airoha_foe_mac_info_common l2;

	u32 meter;
};

struct airoha_foe_entry {
	union {
		struct {
			u32 ib1;
			union {
				struct airoha_foe_bridge bridge;
				struct airoha_foe_ipv4 ipv4;
				struct airoha_foe_ipv4_dslite dslite;
				struct airoha_foe_ipv6 ipv6;
				DECLARE_FLEX_ARRAY(u32, d);
			};
		};
		u8 data[PPE_ENTRY_SIZE];
	};
};

struct airoha_foe_stats {
	u32 bytes;
	u32 packets;
};

struct airoha_foe_stats64 {
	u64 bytes;
	u64 packets;
};

struct airoha_flow_data {
	struct ethhdr eth;

	union {
		struct {
			__be32 src_addr;
			__be32 dst_addr;
		} v4;

		struct {
			struct in6_addr src_addr;
			struct in6_addr dst_addr;
		} v6;
	};

	__be16 src_port;
	__be16 dst_port;

	struct {
		struct {
			u16 id;
			__be16 proto;
		} hdr[2];
		u8 num;
	} vlan;
	struct {
		u16 sid;
		u8 num;
	} pppoe;
};

enum airoha_flow_entry_type {
	FLOW_TYPE_L4,
	FLOW_TYPE_L2,
	FLOW_TYPE_L2_SUBFLOW,
};

struct airoha_flow_table_entry {
	union {
		struct hlist_node list; /* PPE L3 flow entry */
		struct {
			struct rhash_head l2_node;  /* L2 flow entry */
			struct hlist_head l2_flows; /* PPE L2 subflows list */
		};
	};

	struct hlist_node l2_subflow_node; /* PPE L2 subflow entry */
	u32 hash;

	struct airoha_foe_stats64 stats;
	enum airoha_flow_entry_type type;

	struct rhash_head node;
	unsigned long cookie;

	/* Must be last --ends in a flexible-array member. */
	struct airoha_foe_entry data;
};

struct airoha_wdma_info {
	u8 idx;
	u8 queue;
	u16 wcid;
	u8 bss;
};

/* RX queue to IRQ mapping: BIT(q) in IRQ(n) */
#define RX_IRQ0_BANK_PIN_MASK			0x839f
#define RX_IRQ1_BANK_PIN_MASK			0x7fe00000
#define RX_IRQ2_BANK_PIN_MASK			0x20
#define RX_IRQ3_BANK_PIN_MASK			0x40
#define RX_IRQ_BANK_PIN_MASK(_n)		\
	(((_n) == 3) ? RX_IRQ3_BANK_PIN_MASK :	\
	 ((_n) == 2) ? RX_IRQ2_BANK_PIN_MASK :	\
	 ((_n) == 1) ? RX_IRQ1_BANK_PIN_MASK :	\
	 RX_IRQ0_BANK_PIN_MASK)

struct airoha_irq_bank {
	struct airoha_qdma *qdma;

	/* protect concurrent irqmask accesses */
	spinlock_t irq_lock;
	u32 irqmask[QDMA_INT_REG_MAX];
	int irq;
};

struct airoha_qdma {
	struct airoha_eth *eth;
	void __iomem *regs;

	atomic_t users;

	struct airoha_irq_bank irq_banks[AIROHA_MAX_NUM_IRQ_BANKS];

	struct airoha_tx_irq_queue q_tx_irq[AIROHA_NUM_TX_IRQ];

	struct airoha_queue q_tx[AIROHA_NUM_TX_RING];
	struct airoha_queue q_rx[AIROHA_NUM_RX_RING];
};

struct airoha_gdm_port {
	struct airoha_qdma *qdma;
	struct net_device *dev;
	int id;

	struct airoha_hw_stats stats;

	DECLARE_BITMAP(qos_sq_bmap, AIROHA_NUM_QOS_CHANNELS);

	/* qos stats counters */
	u64 cpu_tx_packets;
	u64 fwd_tx_packets;

	struct metadata_dst *dsa_meta[AIROHA_MAX_DSA_PORTS];
};

#define AIROHA_RXD4_PPE_CPU_REASON	GENMASK(20, 16)
#define AIROHA_RXD4_FOE_ENTRY		GENMASK(15, 0)

struct airoha_ppe {
	struct airoha_ppe_dev dev;
	struct airoha_eth *eth;

	void *foe;
	dma_addr_t foe_dma;

	struct rhashtable l2_flows;

	struct hlist_head *foe_flow;
	u16 foe_check_time[PPE_NUM_ENTRIES];

	struct airoha_foe_stats *foe_stats;
	dma_addr_t foe_stats_dma;

	struct dentry *debugfs_dir;
};

struct airoha_eth {
	struct device *dev;

	unsigned long state;
	void __iomem *fe_regs;

	struct airoha_npu __rcu *npu;

	struct airoha_ppe *ppe;
	struct rhashtable flow_table;

	struct reset_control_bulk_data rsts[AIROHA_MAX_NUM_RSTS];
	struct reset_control_bulk_data xsi_rsts[AIROHA_MAX_NUM_XSI_RSTS];

	struct net_device *napi_dev;

	struct airoha_qdma qdma[AIROHA_MAX_NUM_QDMA];
	struct airoha_gdm_port *ports[AIROHA_MAX_NUM_GDM_PORTS];
};

u32 airoha_rr(void __iomem *base, u32 offset);
void airoha_wr(void __iomem *base, u32 offset, u32 val);
u32 airoha_rmw(void __iomem *base, u32 offset, u32 mask, u32 val);

#define airoha_fe_rr(eth, offset)				\
	airoha_rr((eth)->fe_regs, (offset))
#define airoha_fe_wr(eth, offset, val)				\
	airoha_wr((eth)->fe_regs, (offset), (val))
#define airoha_fe_rmw(eth, offset, mask, val)			\
	airoha_rmw((eth)->fe_regs, (offset), (mask), (val))
#define airoha_fe_set(eth, offset, val)				\
	airoha_rmw((eth)->fe_regs, (offset), 0, (val))
#define airoha_fe_clear(eth, offset, val)			\
	airoha_rmw((eth)->fe_regs, (offset), (val), 0)

#define airoha_qdma_rr(qdma, offset)				\
	airoha_rr((qdma)->regs, (offset))
#define airoha_qdma_wr(qdma, offset, val)			\
	airoha_wr((qdma)->regs, (offset), (val))
#define airoha_qdma_rmw(qdma, offset, mask, val)		\
	airoha_rmw((qdma)->regs, (offset), (mask), (val))
#define airoha_qdma_set(qdma, offset, val)			\
	airoha_rmw((qdma)->regs, (offset), 0, (val))
#define airoha_qdma_clear(qdma, offset, val)			\
	airoha_rmw((qdma)->regs, (offset), (val), 0)

static inline bool airhoa_is_lan_gdm_port(struct airoha_gdm_port *port)
{
	/* GDM1 port on EN7581 SoC is connected to the lan dsa switch.
	 * GDM{2,3,4} can be used as wan port connected to an external
	 * phy module.
	 */
	return port->id == 1;
}

bool airoha_is_valid_gdm_port(struct airoha_eth *eth,
			      struct airoha_gdm_port *port);

void airoha_ppe_check_skb(struct airoha_ppe_dev *dev, struct sk_buff *skb,
			  u16 hash, bool rx_wlan);
int airoha_ppe_setup_tc_block_cb(struct airoha_ppe_dev *dev, void *type_data);
int airoha_ppe_init(struct airoha_eth *eth);
void airoha_ppe_deinit(struct airoha_eth *eth);
void airoha_ppe_init_upd_mem(struct airoha_gdm_port *port);
struct airoha_foe_entry *airoha_ppe_foe_get_entry(struct airoha_ppe *ppe,
						  u32 hash);
void airoha_ppe_foe_entry_get_stats(struct airoha_ppe *ppe, u32 hash,
				    struct airoha_foe_stats64 *stats);

#ifdef CONFIG_DEBUG_FS
int airoha_ppe_debugfs_init(struct airoha_ppe *ppe);
#else
static inline int airoha_ppe_debugfs_init(struct airoha_ppe *ppe)
{
	return 0;
}
#endif

#endif /* AIROHA_ETH_H */
