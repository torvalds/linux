/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2016-2017 Hisilicon Limited.

#ifndef __HNS3_ENET_H
#define __HNS3_ENET_H

#include <linux/dim.h>
#include <linux/if_vlan.h>
#include <net/page_pool/types.h>
#include <asm/barrier.h>

#include "hnae3.h"

struct iphdr;
struct ipv6hdr;

enum hns3_nic_state {
	HNS3_NIC_STATE_TESTING,
	HNS3_NIC_STATE_RESETTING,
	HNS3_NIC_STATE_INITED,
	HNS3_NIC_STATE_DOWN,
	HNS3_NIC_STATE_DISABLED,
	HNS3_NIC_STATE_REMOVING,
	HNS3_NIC_STATE_SERVICE_INITED,
	HNS3_NIC_STATE_SERVICE_SCHED,
	HNS3_NIC_STATE2_RESET_REQUESTED,
	HNS3_NIC_STATE_HW_TX_CSUM_ENABLE,
	HNS3_NIC_STATE_RXD_ADV_LAYOUT_ENABLE,
	HNS3_NIC_STATE_TX_PUSH_ENABLE,
	HNS3_NIC_STATE_MAX
};

#define HNS3_MAX_PUSH_BD_NUM		2

#define HNS3_RING_RX_RING_BASEADDR_L_REG	0x00000
#define HNS3_RING_RX_RING_BASEADDR_H_REG	0x00004
#define HNS3_RING_RX_RING_BD_NUM_REG		0x00008
#define HNS3_RING_RX_RING_BD_LEN_REG		0x0000C
#define HNS3_RING_RX_RING_TAIL_REG		0x00018
#define HNS3_RING_RX_RING_HEAD_REG		0x0001C
#define HNS3_RING_RX_RING_FBDNUM_REG		0x00020
#define HNS3_RING_RX_RING_PKTNUM_RECORD_REG	0x0002C

#define HNS3_RING_TX_RING_BASEADDR_L_REG	0x00040
#define HNS3_RING_TX_RING_BASEADDR_H_REG	0x00044
#define HNS3_RING_TX_RING_BD_NUM_REG		0x00048
#define HNS3_RING_TX_RING_TC_REG		0x00050
#define HNS3_RING_TX_RING_TAIL_REG		0x00058
#define HNS3_RING_TX_RING_HEAD_REG		0x0005C
#define HNS3_RING_TX_RING_FBDNUM_REG		0x00060
#define HNS3_RING_TX_RING_OFFSET_REG		0x00064
#define HNS3_RING_TX_RING_EBDNUM_REG		0x00068
#define HNS3_RING_TX_RING_PKTNUM_RECORD_REG	0x0006C
#define HNS3_RING_TX_RING_EBD_OFFSET_REG	0x00070
#define HNS3_RING_TX_RING_BD_ERR_REG		0x00074
#define HNS3_RING_EN_REG			0x00090
#define HNS3_RING_RX_EN_REG			0x00098
#define HNS3_RING_TX_EN_REG			0x000D4

#define HNS3_RX_HEAD_SIZE			256

#define HNS3_TX_TIMEOUT (5 * HZ)
#define HNS3_RING_NAME_LEN			16
#define HNS3_BUFFER_SIZE_2048			2048
#define HNS3_RING_MAX_PENDING			32760
#define HNS3_RING_MIN_PENDING			72
#define HNS3_RING_BD_MULTIPLE			8
/* max frame size of mac */
#define HNS3_MAX_MTU(max_frm_size) \
	((max_frm_size) - (ETH_HLEN + ETH_FCS_LEN + 2 * VLAN_HLEN))

#define HNS3_BD_SIZE_512_TYPE			0
#define HNS3_BD_SIZE_1024_TYPE			1
#define HNS3_BD_SIZE_2048_TYPE			2
#define HNS3_BD_SIZE_4096_TYPE			3

#define HNS3_RX_FLAG_VLAN_PRESENT		0x1
#define HNS3_RX_FLAG_L3ID_IPV4			0x0
#define HNS3_RX_FLAG_L3ID_IPV6			0x1
#define HNS3_RX_FLAG_L4ID_UDP			0x0
#define HNS3_RX_FLAG_L4ID_TCP			0x1

#define HNS3_RXD_DMAC_S				0
#define HNS3_RXD_DMAC_M				(0x3 << HNS3_RXD_DMAC_S)
#define HNS3_RXD_VLAN_S				2
#define HNS3_RXD_VLAN_M				(0x3 << HNS3_RXD_VLAN_S)
#define HNS3_RXD_L3ID_S				4
#define HNS3_RXD_L3ID_M				(0xf << HNS3_RXD_L3ID_S)
#define HNS3_RXD_L4ID_S				8
#define HNS3_RXD_L4ID_M				(0xf << HNS3_RXD_L4ID_S)
#define HNS3_RXD_FRAG_B				12
#define HNS3_RXD_STRP_TAGP_S			13
#define HNS3_RXD_STRP_TAGP_M			(0x3 << HNS3_RXD_STRP_TAGP_S)

#define HNS3_RXD_L2E_B				16
#define HNS3_RXD_L3E_B				17
#define HNS3_RXD_L4E_B				18
#define HNS3_RXD_TRUNCAT_B			19
#define HNS3_RXD_HOI_B				20
#define HNS3_RXD_DOI_B				21
#define HNS3_RXD_OL3E_B				22
#define HNS3_RXD_OL4E_B				23
#define HNS3_RXD_GRO_COUNT_S			24
#define HNS3_RXD_GRO_COUNT_M			(0x3f << HNS3_RXD_GRO_COUNT_S)
#define HNS3_RXD_GRO_FIXID_B			30
#define HNS3_RXD_GRO_ECN_B			31

#define HNS3_RXD_ODMAC_S			0
#define HNS3_RXD_ODMAC_M			(0x3 << HNS3_RXD_ODMAC_S)
#define HNS3_RXD_OVLAN_S			2
#define HNS3_RXD_OVLAN_M			(0x3 << HNS3_RXD_OVLAN_S)
#define HNS3_RXD_OL3ID_S			4
#define HNS3_RXD_OL3ID_M			(0xf << HNS3_RXD_OL3ID_S)
#define HNS3_RXD_OL4ID_S			8
#define HNS3_RXD_OL4ID_M			(0xf << HNS3_RXD_OL4ID_S)
#define HNS3_RXD_FBHI_S				12
#define HNS3_RXD_FBHI_M				(0x3 << HNS3_RXD_FBHI_S)
#define HNS3_RXD_FBLI_S				14
#define HNS3_RXD_FBLI_M				(0x3 << HNS3_RXD_FBLI_S)

#define HNS3_RXD_PTYPE_S			4
#define HNS3_RXD_PTYPE_M			GENMASK(11, 4)

#define HNS3_RXD_BDTYPE_S			0
#define HNS3_RXD_BDTYPE_M			(0xf << HNS3_RXD_BDTYPE_S)
#define HNS3_RXD_VLD_B				4
#define HNS3_RXD_UDP0_B				5
#define HNS3_RXD_EXTEND_B			7
#define HNS3_RXD_FE_B				8
#define HNS3_RXD_LUM_B				9
#define HNS3_RXD_CRCP_B				10
#define HNS3_RXD_L3L4P_B			11
#define HNS3_RXD_TSIDX_S			12
#define HNS3_RXD_TSIDX_M			(0x3 << HNS3_RXD_TSIDX_S)
#define HNS3_RXD_TS_VLD_B			14
#define HNS3_RXD_LKBK_B				15
#define HNS3_RXD_GRO_SIZE_S			16
#define HNS3_RXD_GRO_SIZE_M			(0x3fff << HNS3_RXD_GRO_SIZE_S)

#define HNS3_TXD_L3T_S				0
#define HNS3_TXD_L3T_M				(0x3 << HNS3_TXD_L3T_S)
#define HNS3_TXD_L4T_S				2
#define HNS3_TXD_L4T_M				(0x3 << HNS3_TXD_L4T_S)
#define HNS3_TXD_L3CS_B				4
#define HNS3_TXD_L4CS_B				5
#define HNS3_TXD_VLAN_B				6
#define HNS3_TXD_TSO_B				7

#define HNS3_TXD_L2LEN_S			8
#define HNS3_TXD_L2LEN_M			(0xff << HNS3_TXD_L2LEN_S)
#define HNS3_TXD_L3LEN_S			16
#define HNS3_TXD_L3LEN_M			(0xff << HNS3_TXD_L3LEN_S)
#define HNS3_TXD_L4LEN_S			24
#define HNS3_TXD_L4LEN_M			(0xff << HNS3_TXD_L4LEN_S)

#define HNS3_TXD_CSUM_START_S		8
#define HNS3_TXD_CSUM_START_M		(0xffff << HNS3_TXD_CSUM_START_S)

#define HNS3_TXD_OL3T_S				0
#define HNS3_TXD_OL3T_M				(0x3 << HNS3_TXD_OL3T_S)
#define HNS3_TXD_OVLAN_B			2
#define HNS3_TXD_MACSEC_B			3
#define HNS3_TXD_TUNTYPE_S			4
#define HNS3_TXD_TUNTYPE_M			(0xf << HNS3_TXD_TUNTYPE_S)

#define HNS3_TXD_CSUM_OFFSET_S		8
#define HNS3_TXD_CSUM_OFFSET_M		(0xffff << HNS3_TXD_CSUM_OFFSET_S)

#define HNS3_TXD_BDTYPE_S			0
#define HNS3_TXD_BDTYPE_M			(0xf << HNS3_TXD_BDTYPE_S)
#define HNS3_TXD_FE_B				4
#define HNS3_TXD_SC_S				5
#define HNS3_TXD_SC_M				(0x3 << HNS3_TXD_SC_S)
#define HNS3_TXD_EXTEND_B			7
#define HNS3_TXD_VLD_B				8
#define HNS3_TXD_RI_B				9
#define HNS3_TXD_RA_B				10
#define HNS3_TXD_TSYN_B				11
#define HNS3_TXD_DECTTL_S			12
#define HNS3_TXD_DECTTL_M			(0xf << HNS3_TXD_DECTTL_S)

#define HNS3_TXD_OL4CS_B			22

#define HNS3_TXD_MSS_S				0
#define HNS3_TXD_MSS_M				(0x3fff << HNS3_TXD_MSS_S)
#define HNS3_TXD_HW_CS_B			14

#define HNS3_VECTOR_TX_IRQ			BIT_ULL(0)
#define HNS3_VECTOR_RX_IRQ			BIT_ULL(1)

#define HNS3_VECTOR_NOT_INITED			0
#define HNS3_VECTOR_INITED			1

#define HNS3_MAX_BD_SIZE			65535
#define HNS3_MAX_TSO_BD_NUM			63U
#define HNS3_MAX_TSO_SIZE			1048576U
#define HNS3_MAX_NON_TSO_SIZE			9728U

#define HNS3_VECTOR_GL_MASK			GENMASK(11, 0)
#define HNS3_VECTOR_GL0_OFFSET			0x100
#define HNS3_VECTOR_GL1_OFFSET			0x200
#define HNS3_VECTOR_GL2_OFFSET			0x300
#define HNS3_VECTOR_RL_OFFSET			0x900
#define HNS3_VECTOR_RL_EN_B			6
#define HNS3_VECTOR_QL_MASK			GENMASK(9, 0)
#define HNS3_VECTOR_TX_QL_OFFSET		0xe00
#define HNS3_VECTOR_RX_QL_OFFSET		0xf00

#define HNS3_RING_EN_B				0

#define HNS3_GL0_CQ_MODE_REG			0x20d00
#define HNS3_GL1_CQ_MODE_REG			0x20d04
#define HNS3_GL2_CQ_MODE_REG			0x20d08
#define HNS3_CQ_MODE_EQE			1U
#define HNS3_CQ_MODE_CQE			0U

#define HNS3_RESCHED_BD_NUM			1024

enum hns3_pkt_l2t_type {
	HNS3_L2_TYPE_UNICAST,
	HNS3_L2_TYPE_MULTICAST,
	HNS3_L2_TYPE_BROADCAST,
	HNS3_L2_TYPE_INVALID,
};

enum hns3_pkt_l3t_type {
	HNS3_L3T_NONE,
	HNS3_L3T_IPV6,
	HNS3_L3T_IPV4,
	HNS3_L3T_RESERVED
};

enum hns3_pkt_l4t_type {
	HNS3_L4T_UNKNOWN,
	HNS3_L4T_TCP,
	HNS3_L4T_UDP,
	HNS3_L4T_SCTP
};

enum hns3_pkt_ol3t_type {
	HNS3_OL3T_NONE,
	HNS3_OL3T_IPV6,
	HNS3_OL3T_IPV4_NO_CSUM,
	HNS3_OL3T_IPV4_CSUM
};

enum hns3_pkt_tun_type {
	HNS3_TUN_NONE,
	HNS3_TUN_MAC_IN_UDP,
	HNS3_TUN_NVGRE,
	HNS3_TUN_OTHER
};

/* hardware spec ring buffer format */
struct __packed hns3_desc {
	union {
		__le64 addr;
		__le16 csum;
		struct {
			__le32 ts_nsec;
			__le32 ts_sec;
		};
	};
	union {
		struct {
			__le16 vlan_tag;
			__le16 send_size;
			union {
				__le32 type_cs_vlan_tso_len;
				struct {
					__u8 type_cs_vlan_tso;
					__u8 l2_len;
					__u8 l3_len;
					__u8 l4_len;
				};
			};
			__le16 outer_vlan_tag;
			__le16 tv;

		union {
			__le32 ol_type_vlan_len_msec;
			struct {
				__u8 ol_type_vlan_msec;
				__u8 ol2_len;
				__u8 ol3_len;
				__u8 ol4_len;
			};
		};

			__le32 paylen_ol4cs;
			__le16 bdtp_fe_sc_vld_ra_ri;
			__le16 mss_hw_csum;
		} tx;

		struct {
			__le32 l234_info;
			__le16 pkt_len;
			__le16 size;

			__le32 rss_hash;
			__le16 fd_id;
			__le16 vlan_tag;

			union {
				__le32 ol_info;
				struct {
					__le16 o_dm_vlan_id_fb;
					__le16 ot_vlan_tag;
				};
			};

			__le32 bd_base_info;
		} rx;
	};
};

enum hns3_desc_type {
	DESC_TYPE_UNKNOWN		= 0,
	DESC_TYPE_SKB			= 1 << 0,
	DESC_TYPE_FRAGLIST_SKB		= 1 << 1,
	DESC_TYPE_PAGE			= 1 << 2,
	DESC_TYPE_BOUNCE_ALL		= 1 << 3,
	DESC_TYPE_BOUNCE_HEAD		= 1 << 4,
	DESC_TYPE_SGL_SKB		= 1 << 5,
	DESC_TYPE_PP_FRAG		= 1 << 6,
};

struct hns3_desc_cb {
	dma_addr_t dma; /* dma address of this desc */
	void *buf;      /* cpu addr for a desc */

	/* priv data for the desc, e.g. skb when use with ip stack */
	void *priv;

	union {
		u32 page_offset;	/* for rx */
		u32 send_bytes;		/* for tx */
	};

	u32 length;     /* length of the buffer */

	u16 reuse_flag;
	u16 refill;

	/* desc type, used by the ring user to mark the type of the priv data */
	u16 type;
	u16 pagecnt_bias;
};

enum hns3_pkt_l3type {
	HNS3_L3_TYPE_IPV4,
	HNS3_L3_TYPE_IPV6,
	HNS3_L3_TYPE_ARP,
	HNS3_L3_TYPE_RARP,
	HNS3_L3_TYPE_IPV4_OPT,
	HNS3_L3_TYPE_IPV6_EXT,
	HNS3_L3_TYPE_LLDP,
	HNS3_L3_TYPE_BPDU,
	HNS3_L3_TYPE_MAC_PAUSE,
	HNS3_L3_TYPE_PFC_PAUSE, /* 0x9 */

	/* reserved for 0xA~0xB */

	HNS3_L3_TYPE_CNM = 0xc,

	/* reserved for 0xD~0xE */

	HNS3_L3_TYPE_PARSE_FAIL	= 0xf /* must be last */
};

enum hns3_pkt_l4type {
	HNS3_L4_TYPE_UDP,
	HNS3_L4_TYPE_TCP,
	HNS3_L4_TYPE_GRE,
	HNS3_L4_TYPE_SCTP,
	HNS3_L4_TYPE_IGMP,
	HNS3_L4_TYPE_ICMP,

	/* reserved for 0x6~0xE */

	HNS3_L4_TYPE_PARSE_FAIL	= 0xf /* must be last */
};

enum hns3_pkt_ol3type {
	HNS3_OL3_TYPE_IPV4 = 0,
	HNS3_OL3_TYPE_IPV6,
	/* reserved for 0x2~0x3 */
	HNS3_OL3_TYPE_IPV4_OPT = 4,
	HNS3_OL3_TYPE_IPV6_EXT,

	/* reserved for 0x6~0xE */

	HNS3_OL3_TYPE_PARSE_FAIL = 0xf	/* must be last */
};

enum hns3_pkt_ol4type {
	HNS3_OL4_TYPE_NO_TUN,
	HNS3_OL4_TYPE_MAC_IN_UDP,
	HNS3_OL4_TYPE_NVGRE,
	HNS3_OL4_TYPE_UNKNOWN
};

struct hns3_rx_ptype {
	u32 ptype : 8;
	u32 csum_level : 2;
	u32 ip_summed : 2;
	u32 l3_type : 4;
	u32 valid : 1;
	u32 hash_type: 3;
};

struct ring_stats {
	u64 sw_err_cnt;
	u64 seg_pkt_cnt;
	union {
		struct {
			u64 tx_pkts;
			u64 tx_bytes;
			u64 tx_more;
			u64 tx_push;
			u64 tx_mem_doorbell;
			u64 restart_queue;
			u64 tx_busy;
			u64 tx_copy;
			u64 tx_vlan_err;
			u64 tx_l4_proto_err;
			u64 tx_l2l3l4_err;
			u64 tx_tso_err;
			u64 over_max_recursion;
			u64 hw_limitation;
			u64 tx_bounce;
			u64 tx_spare_full;
			u64 copy_bits_err;
			u64 tx_sgl;
			u64 skb2sgl_err;
			u64 map_sg_err;
		};
		struct {
			u64 rx_pkts;
			u64 rx_bytes;
			u64 rx_err_cnt;
			u64 reuse_pg_cnt;
			u64 err_pkt_len;
			u64 err_bd_num;
			u64 l2_err;
			u64 l3l4_csum_err;
			u64 csum_complete;
			u64 rx_multicast;
			u64 non_reuse_pg;
			u64 frag_alloc_err;
			u64 frag_alloc;
		};
		__le16 csum;
	};
};

struct hns3_tx_spare {
	dma_addr_t dma;
	void *buf;
	u32 next_to_use;
	u32 next_to_clean;
	u32 last_to_clean;
	u32 len;
};

struct hns3_enet_ring {
	struct hns3_desc *desc; /* dma map address space */
	struct hns3_desc_cb *desc_cb;
	struct hns3_enet_ring *next;
	struct hns3_enet_tqp_vector *tqp_vector;
	struct hnae3_queue *tqp;
	int queue_index;
	struct device *dev; /* will be used for DMA mapping of descriptors */
	struct page_pool *page_pool;

	/* statistic */
	struct ring_stats stats;
	struct u64_stats_sync syncp;

	dma_addr_t desc_dma_addr;
	u32 buf_size;       /* size for hnae_desc->addr, preset by AE */
	u16 desc_num;       /* total number of desc */
	int next_to_use;    /* idx of next spare desc */

	/* idx of lastest sent desc, the ring is empty when equal to
	 * next_to_use
	 */
	int next_to_clean;
	u32 flag;          /* ring attribute */

	int pending_buf;
	union {
		/* for Tx ring */
		struct {
			u32 fd_qb_tx_sample;
			int last_to_use;        /* last idx used by xmit */
			u32 tx_copybreak;
			struct hns3_tx_spare *tx_spare;
		};

		/* for Rx ring */
		struct {
			u32 pull_len;   /* memcpy len for current rx packet */
			u32 rx_copybreak;
			u32 frag_num;
			/* first buffer address for current packet */
			unsigned char *va;
			struct sk_buff *skb;
			struct sk_buff *tail_skb;
		};
	};
} ____cacheline_internodealigned_in_smp;

enum hns3_flow_level_range {
	HNS3_FLOW_LOW = 0,
	HNS3_FLOW_MID = 1,
	HNS3_FLOW_HIGH = 2,
	HNS3_FLOW_ULTRA = 3,
};

#define HNS3_INT_GL_50K			0x0014
#define HNS3_INT_GL_20K			0x0032
#define HNS3_INT_GL_18K			0x0036
#define HNS3_INT_GL_8K			0x007C

#define HNS3_INT_GL_1US			BIT(31)

#define HNS3_INT_RL_MAX			0x00EC
#define HNS3_INT_RL_ENABLE_MASK		0x40

#define HNS3_INT_QL_DEFAULT_CFG		0x20

struct hns3_enet_coalesce {
	u16 int_gl;
	u16 int_ql;
	u16 int_ql_max;
	u8 adapt_enable : 1;
	u8 ql_enable : 1;
	u8 unit_1us : 1;
	enum hns3_flow_level_range flow_level;
};

struct hns3_enet_ring_group {
	/* array of pointers to rings */
	struct hns3_enet_ring *ring;
	u64 total_bytes;	/* total bytes processed this group */
	u64 total_packets;	/* total packets processed this group */
	u16 count;
	struct hns3_enet_coalesce coal;
	struct dim dim;
};

struct hns3_enet_tqp_vector {
	struct hnae3_handle *handle;
	u8 __iomem *mask_addr;
	int vector_irq;
	int irq_init_flag;

	u16 idx;		/* index in the TQP vector array per handle. */

	struct napi_struct napi;

	struct hns3_enet_ring_group rx_group;
	struct hns3_enet_ring_group tx_group;

	cpumask_t affinity_mask;
	u16 num_tqps;	/* total number of tqps in TQP vector */
	struct irq_affinity_notify affinity_notify;

	char name[HNAE3_INT_NAME_LEN];

	u64 event_cnt;
} ____cacheline_internodealigned_in_smp;

struct hns3_nic_priv {
	struct hnae3_handle *ae_handle;
	struct net_device *netdev;
	struct device *dev;

	/**
	 * the cb for nic to manage the ring buffer, the first half of the
	 * array is for tx_ring and vice versa for the second half
	 */
	struct hns3_enet_ring *ring;
	struct hns3_enet_tqp_vector *tqp_vector;
	u16 vector_num;
	u8 max_non_tso_bd_num;

	u64 tx_timeout_count;

	unsigned long state;

	enum dim_cq_period_mode tx_cqe_mode;
	enum dim_cq_period_mode rx_cqe_mode;
	struct hns3_enet_coalesce tx_coal;
	struct hns3_enet_coalesce rx_coal;
	u32 tx_copybreak;
	u32 rx_copybreak;
};

union l3_hdr_info {
	struct iphdr *v4;
	struct ipv6hdr *v6;
	unsigned char *hdr;
};

union l4_hdr_info {
	struct tcphdr *tcp;
	struct udphdr *udp;
	struct gre_base_hdr *gre;
	unsigned char *hdr;
};

struct hns3_hw_error_info {
	enum hnae3_hw_error_type type;
	const char *msg;
};

struct hns3_reset_type_map {
	enum ethtool_reset_flags rst_flags;
	enum hnae3_reset_type rst_type;
};

static inline int ring_space(struct hns3_enet_ring *ring)
{
	/* This smp_load_acquire() pairs with smp_store_release() in
	 * hns3_nic_reclaim_one_desc called by hns3_clean_tx_ring.
	 */
	int begin = smp_load_acquire(&ring->next_to_clean);
	int end = READ_ONCE(ring->next_to_use);

	return ((end >= begin) ? (ring->desc_num - end + begin) :
			(begin - end)) - 1;
}

static inline u32 hns3_tqp_read_reg(struct hns3_enet_ring *ring, u32 reg)
{
	return readl_relaxed(ring->tqp->io_base + reg);
}

static inline u32 hns3_read_reg(void __iomem *base, u32 reg)
{
	return readl(base + reg);
}

static inline void hns3_write_reg(void __iomem *base, u32 reg, u32 value)
{
	u8 __iomem *reg_addr = READ_ONCE(base);

	writel(value, reg_addr + reg);
}

#define hns3_read_dev(a, reg) \
	hns3_read_reg((a)->io_base, reg)

static inline bool hns3_nic_resetting(struct net_device *netdev)
{
	struct hns3_nic_priv *priv = netdev_priv(netdev);

	return test_bit(HNS3_NIC_STATE_RESETTING, &priv->state);
}

#define hns3_write_dev(a, reg, value) \
	hns3_write_reg((a)->io_base, reg, value)

#define ring_to_dev(ring) ((ring)->dev)

#define ring_to_netdev(ring)	((ring)->tqp_vector->napi.dev)

#define ring_to_dma_dir(ring) (HNAE3_IS_TX_RING(ring) ? \
	DMA_TO_DEVICE : DMA_FROM_DEVICE)

#define hns3_buf_size(_ring) ((_ring)->buf_size)

#define hns3_ring_stats_update(ring, cnt) do { \
	typeof(ring) (tmp) = (ring); \
	u64_stats_update_begin(&(tmp)->syncp); \
	((tmp)->stats.cnt)++; \
	u64_stats_update_end(&(tmp)->syncp); \
} while (0) \

static inline unsigned int hns3_page_order(struct hns3_enet_ring *ring)
{
#if (PAGE_SIZE < 8192)
	if (ring->buf_size > (PAGE_SIZE / 2))
		return 1;
#endif
	return 0;
}

#define hns3_page_size(_ring) (PAGE_SIZE << hns3_page_order(_ring))

/* iterator for handling rings in ring group */
#define hns3_for_each_ring(pos, head) \
	for (pos = (head).ring; (pos); pos = (pos)->next)

#define hns3_get_handle(ndev) \
	(((struct hns3_nic_priv *)netdev_priv(ndev))->ae_handle)

#define hns3_get_ae_dev(handle) \
	(pci_get_drvdata((handle)->pdev))

#define hns3_get_ops(handle) \
	((handle)->ae_algo->ops)

#define hns3_gl_usec_to_reg(int_gl) ((int_gl) >> 1)
#define hns3_gl_round_down(int_gl) round_down(int_gl, 2)

#define hns3_rl_usec_to_reg(int_rl) ((int_rl) >> 2)
#define hns3_rl_round_down(int_rl) round_down(int_rl, 4)

void hns3_ethtool_set_ops(struct net_device *netdev);
int hns3_set_channels(struct net_device *netdev,
		      struct ethtool_channels *ch);

void hns3_clean_tx_ring(struct hns3_enet_ring *ring, int budget);
int hns3_init_all_ring(struct hns3_nic_priv *priv);
int hns3_nic_reset_all_ring(struct hnae3_handle *h);
void hns3_fini_ring(struct hns3_enet_ring *ring);
netdev_tx_t hns3_nic_net_xmit(struct sk_buff *skb, struct net_device *netdev);
bool hns3_is_phys_func(struct pci_dev *pdev);
int hns3_clean_rx_ring(
		struct hns3_enet_ring *ring, int budget,
		void (*rx_fn)(struct hns3_enet_ring *, struct sk_buff *));

void hns3_set_vector_coalesce_rx_gl(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 gl_value);
void hns3_set_vector_coalesce_tx_gl(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 gl_value);
void hns3_set_vector_coalesce_rl(struct hns3_enet_tqp_vector *tqp_vector,
				 u32 rl_value);
void hns3_set_vector_coalesce_rx_ql(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 ql_value);
void hns3_set_vector_coalesce_tx_ql(struct hns3_enet_tqp_vector *tqp_vector,
				    u32 ql_value);

void hns3_request_update_promisc_mode(struct hnae3_handle *handle);
int hns3_reset_notify(struct hnae3_handle *handle,
		      enum hnae3_reset_notify_type type);

#ifdef CONFIG_HNS3_DCB
void hns3_dcbnl_setup(struct hnae3_handle *handle);
#else
static inline void hns3_dcbnl_setup(struct hnae3_handle *handle) {}
#endif

int hns3_dbg_init(struct hnae3_handle *handle);
void hns3_dbg_uninit(struct hnae3_handle *handle);
void hns3_dbg_register_debugfs(const char *debugfs_dir_name);
void hns3_dbg_unregister_debugfs(void);
void hns3_shinfo_pack(struct skb_shared_info *shinfo, __u32 *size);
u16 hns3_get_max_available_channels(struct hnae3_handle *h);
void hns3_cq_period_mode_init(struct hns3_nic_priv *priv,
			      enum dim_cq_period_mode tx_mode,
			      enum dim_cq_period_mode rx_mode);

void hns3_external_lb_prepare(struct net_device *ndev, bool if_running);
void hns3_external_lb_restore(struct net_device *ndev, bool if_running);
#endif
