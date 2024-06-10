/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_TXRX_H_
#define _IDPF_TXRX_H_

#include <net/page_pool/helpers.h>
#include <net/tcp.h>
#include <net/netdev_queues.h>

#include "virtchnl2_lan_desc.h"

#define IDPF_LARGE_MAX_Q			256
#define IDPF_MAX_Q				16
#define IDPF_MIN_Q				2
/* Mailbox Queue */
#define IDPF_MAX_MBXQ				1

#define IDPF_MIN_TXQ_DESC			64
#define IDPF_MIN_RXQ_DESC			64
#define IDPF_MIN_TXQ_COMPLQ_DESC		256
#define IDPF_MAX_QIDS				256

/* Number of descriptors in a queue should be a multiple of 32. RX queue
 * descriptors alone should be a multiple of IDPF_REQ_RXQ_DESC_MULTIPLE
 * to achieve BufQ descriptors aligned to 32
 */
#define IDPF_REQ_DESC_MULTIPLE			32
#define IDPF_REQ_RXQ_DESC_MULTIPLE (IDPF_MAX_BUFQS_PER_RXQ_GRP * 32)
#define IDPF_MIN_TX_DESC_NEEDED (MAX_SKB_FRAGS + 6)
#define IDPF_TX_WAKE_THRESH ((u16)IDPF_MIN_TX_DESC_NEEDED * 2)

#define IDPF_MAX_DESCS				8160
#define IDPF_MAX_TXQ_DESC ALIGN_DOWN(IDPF_MAX_DESCS, IDPF_REQ_DESC_MULTIPLE)
#define IDPF_MAX_RXQ_DESC ALIGN_DOWN(IDPF_MAX_DESCS, IDPF_REQ_RXQ_DESC_MULTIPLE)
#define MIN_SUPPORT_TXDID (\
	VIRTCHNL2_TXDID_FLEX_FLOW_SCHED |\
	VIRTCHNL2_TXDID_FLEX_TSO_CTX)

#define IDPF_DFLT_SINGLEQ_TX_Q_GROUPS		1
#define IDPF_DFLT_SINGLEQ_RX_Q_GROUPS		1
#define IDPF_DFLT_SINGLEQ_TXQ_PER_GROUP		4
#define IDPF_DFLT_SINGLEQ_RXQ_PER_GROUP		4

#define IDPF_COMPLQ_PER_GROUP			1
#define IDPF_SINGLE_BUFQ_PER_RXQ_GRP		1
#define IDPF_MAX_BUFQS_PER_RXQ_GRP		2
#define IDPF_BUFQ2_ENA				1
#define IDPF_NUMQ_PER_CHUNK			1

#define IDPF_DFLT_SPLITQ_TXQ_PER_GROUP		1
#define IDPF_DFLT_SPLITQ_RXQ_PER_GROUP		1

/* Default vector sharing */
#define IDPF_MBX_Q_VEC		1
#define IDPF_MIN_Q_VEC		1

#define IDPF_DFLT_TX_Q_DESC_COUNT		512
#define IDPF_DFLT_TX_COMPLQ_DESC_COUNT		512
#define IDPF_DFLT_RX_Q_DESC_COUNT		512

/* IMPORTANT: We absolutely _cannot_ have more buffers in the system than a
 * given RX completion queue has descriptors. This includes _ALL_ buffer
 * queues. E.g.: If you have two buffer queues of 512 descriptors and buffers,
 * you have a total of 1024 buffers so your RX queue _must_ have at least that
 * many descriptors. This macro divides a given number of RX descriptors by
 * number of buffer queues to calculate how many descriptors each buffer queue
 * can have without overrunning the RX queue.
 *
 * If you give hardware more buffers than completion descriptors what will
 * happen is that if hardware gets a chance to post more than ring wrap of
 * descriptors before SW gets an interrupt and overwrites SW head, the gen bit
 * in the descriptor will be wrong. Any overwritten descriptors' buffers will
 * be gone forever and SW has no reasonable way to tell that this has happened.
 * From SW perspective, when we finally get an interrupt, it looks like we're
 * still waiting for descriptor to be done, stalling forever.
 */
#define IDPF_RX_BUFQ_DESC_COUNT(RXD, NUM_BUFQ)	((RXD) / (NUM_BUFQ))

#define IDPF_RX_BUFQ_WORKING_SET(rxq)		((rxq)->desc_count - 1)

#define IDPF_RX_BUMP_NTC(rxq, ntc)				\
do {								\
	if (unlikely(++(ntc) == (rxq)->desc_count)) {		\
		ntc = 0;					\
		change_bit(__IDPF_Q_GEN_CHK, (rxq)->flags);	\
	}							\
} while (0)

#define IDPF_SINGLEQ_BUMP_RING_IDX(q, idx)			\
do {								\
	if (unlikely(++(idx) == (q)->desc_count))		\
		idx = 0;					\
} while (0)

#define IDPF_RX_HDR_SIZE			256
#define IDPF_RX_BUF_2048			2048
#define IDPF_RX_BUF_4096			4096
#define IDPF_RX_BUF_STRIDE			32
#define IDPF_RX_BUF_POST_STRIDE			16
#define IDPF_LOW_WATERMARK			64
/* Size of header buffer specifically for header split */
#define IDPF_HDR_BUF_SIZE			256
#define IDPF_PACKET_HDR_PAD	\
	(ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN * 2)
#define IDPF_TX_TSO_MIN_MSS			88

/* Minimum number of descriptors between 2 descriptors with the RE bit set;
 * only relevant in flow scheduling mode
 */
#define IDPF_TX_SPLITQ_RE_MIN_GAP	64

#define IDPF_RX_BI_BUFID_S		0
#define IDPF_RX_BI_BUFID_M		GENMASK(14, 0)
#define IDPF_RX_BI_GEN_S		15
#define IDPF_RX_BI_GEN_M		BIT(IDPF_RX_BI_GEN_S)
#define IDPF_RXD_EOF_SPLITQ		VIRTCHNL2_RX_FLEX_DESC_ADV_STATUS0_EOF_M
#define IDPF_RXD_EOF_SINGLEQ		VIRTCHNL2_RX_BASE_DESC_STATUS_EOF_M

#define IDPF_SINGLEQ_RX_BUF_DESC(rxq, i)	\
	(&(((struct virtchnl2_singleq_rx_buf_desc *)((rxq)->desc_ring))[i]))
#define IDPF_SPLITQ_RX_BUF_DESC(rxq, i)	\
	(&(((struct virtchnl2_splitq_rx_buf_desc *)((rxq)->desc_ring))[i]))
#define IDPF_SPLITQ_RX_BI_DESC(rxq, i) ((((rxq)->ring))[i])

#define IDPF_BASE_TX_DESC(txq, i)	\
	(&(((struct idpf_base_tx_desc *)((txq)->desc_ring))[i]))
#define IDPF_BASE_TX_CTX_DESC(txq, i) \
	(&(((struct idpf_base_tx_ctx_desc *)((txq)->desc_ring))[i]))
#define IDPF_SPLITQ_TX_COMPLQ_DESC(txcq, i)	\
	(&(((struct idpf_splitq_tx_compl_desc *)((txcq)->desc_ring))[i]))

#define IDPF_FLEX_TX_DESC(txq, i) \
	(&(((union idpf_tx_flex_desc *)((txq)->desc_ring))[i]))
#define IDPF_FLEX_TX_CTX_DESC(txq, i)	\
	(&(((struct idpf_flex_tx_ctx_desc *)((txq)->desc_ring))[i]))

#define IDPF_DESC_UNUSED(txq)     \
	((((txq)->next_to_clean > (txq)->next_to_use) ? 0 : (txq)->desc_count) + \
	(txq)->next_to_clean - (txq)->next_to_use - 1)

#define IDPF_TX_BUF_RSV_UNUSED(txq)	((txq)->buf_stack.top)
#define IDPF_TX_BUF_RSV_LOW(txq)	(IDPF_TX_BUF_RSV_UNUSED(txq) < \
					 (txq)->desc_count >> 2)

#define IDPF_TX_COMPLQ_OVERFLOW_THRESH(txcq)	((txcq)->desc_count >> 1)
/* Determine the absolute number of completions pending, i.e. the number of
 * completions that are expected to arrive on the TX completion queue.
 */
#define IDPF_TX_COMPLQ_PENDING(txq)	\
	(((txq)->num_completions_pending >= (txq)->complq->num_completions ? \
	0 : U64_MAX) + \
	(txq)->num_completions_pending - (txq)->complq->num_completions)

#define IDPF_TX_SPLITQ_COMPL_TAG_WIDTH	16
#define IDPF_SPLITQ_TX_INVAL_COMPL_TAG	-1
/* Adjust the generation for the completion tag and wrap if necessary */
#define IDPF_TX_ADJ_COMPL_TAG_GEN(txq) \
	((++(txq)->compl_tag_cur_gen) >= (txq)->compl_tag_gen_max ? \
	0 : (txq)->compl_tag_cur_gen)

#define IDPF_TXD_LAST_DESC_CMD (IDPF_TX_DESC_CMD_EOP | IDPF_TX_DESC_CMD_RS)

#define IDPF_TX_FLAGS_TSO		BIT(0)
#define IDPF_TX_FLAGS_IPV4		BIT(1)
#define IDPF_TX_FLAGS_IPV6		BIT(2)
#define IDPF_TX_FLAGS_TUNNEL		BIT(3)

union idpf_tx_flex_desc {
	struct idpf_flex_tx_desc q; /* queue based scheduling */
	struct idpf_flex_tx_sched_desc flow; /* flow based scheduling */
};

/**
 * struct idpf_tx_buf
 * @next_to_watch: Next descriptor to clean
 * @skb: Pointer to the skb
 * @dma: DMA address
 * @len: DMA length
 * @bytecount: Number of bytes
 * @gso_segs: Number of GSO segments
 * @compl_tag: Splitq only, unique identifier for a buffer. Used to compare
 *	       with completion tag returned in buffer completion event.
 *	       Because the completion tag is expected to be the same in all
 *	       data descriptors for a given packet, and a single packet can
 *	       span multiple buffers, we need this field to track all
 *	       buffers associated with this completion tag independently of
 *	       the buf_id. The tag consists of a N bit buf_id and M upper
 *	       order "generation bits". See compl_tag_bufid_m and
 *	       compl_tag_gen_s in struct idpf_queue. We'll use a value of -1
 *	       to indicate the tag is not valid.
 * @ctx_entry: Singleq only. Used to indicate the corresponding entry
 *	       in the descriptor ring was used for a context descriptor and
 *	       this buffer entry should be skipped.
 */
struct idpf_tx_buf {
	void *next_to_watch;
	struct sk_buff *skb;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	unsigned int bytecount;
	unsigned short gso_segs;

	union {
		int compl_tag;

		bool ctx_entry;
	};
};

struct idpf_tx_stash {
	struct hlist_node hlist;
	struct idpf_tx_buf buf;
};

/**
 * struct idpf_buf_lifo - LIFO for managing OOO completions
 * @top: Used to know how many buffers are left
 * @size: Total size of LIFO
 * @bufs: Backing array
 */
struct idpf_buf_lifo {
	u16 top;
	u16 size;
	struct idpf_tx_stash **bufs;
};

/**
 * struct idpf_tx_offload_params - Offload parameters for a given packet
 * @tx_flags: Feature flags enabled for this packet
 * @hdr_offsets: Offset parameter for single queue model
 * @cd_tunneling: Type of tunneling enabled for single queue model
 * @tso_len: Total length of payload to segment
 * @mss: Segment size
 * @tso_segs: Number of segments to be sent
 * @tso_hdr_len: Length of headers to be duplicated
 * @td_cmd: Command field to be inserted into descriptor
 */
struct idpf_tx_offload_params {
	u32 tx_flags;

	u32 hdr_offsets;
	u32 cd_tunneling;

	u32 tso_len;
	u16 mss;
	u16 tso_segs;
	u16 tso_hdr_len;

	u16 td_cmd;
};

/**
 * struct idpf_tx_splitq_params
 * @dtype: General descriptor info
 * @eop_cmd: Type of EOP
 * @compl_tag: Associated tag for completion
 * @td_tag: Descriptor tunneling tag
 * @offload: Offload parameters
 */
struct idpf_tx_splitq_params {
	enum idpf_tx_desc_dtype_value dtype;
	u16 eop_cmd;
	union {
		u16 compl_tag;
		u16 td_tag;
	};

	struct idpf_tx_offload_params offload;
};

enum idpf_tx_ctx_desc_eipt_offload {
	IDPF_TX_CTX_EXT_IP_NONE         = 0x0,
	IDPF_TX_CTX_EXT_IP_IPV6         = 0x1,
	IDPF_TX_CTX_EXT_IP_IPV4_NO_CSUM = 0x2,
	IDPF_TX_CTX_EXT_IP_IPV4         = 0x3
};

/* Checksum offload bits decoded from the receive descriptor. */
struct idpf_rx_csum_decoded {
	u32 l3l4p : 1;
	u32 ipe : 1;
	u32 eipe : 1;
	u32 eudpe : 1;
	u32 ipv6exadd : 1;
	u32 l4e : 1;
	u32 pprs : 1;
	u32 nat : 1;
	u32 raw_csum_inv : 1;
	u32 raw_csum : 16;
};

struct idpf_rx_extracted {
	unsigned int size;
	u16 rx_ptype;
};

#define IDPF_TX_COMPLQ_CLEAN_BUDGET	256
#define IDPF_TX_MIN_PKT_LEN		17
#define IDPF_TX_DESCS_FOR_SKB_DATA_PTR	1
#define IDPF_TX_DESCS_PER_CACHE_LINE	(L1_CACHE_BYTES / \
					 sizeof(struct idpf_flex_tx_desc))
#define IDPF_TX_DESCS_FOR_CTX		1
/* TX descriptors needed, worst case */
#define IDPF_TX_DESC_NEEDED (MAX_SKB_FRAGS + IDPF_TX_DESCS_FOR_CTX + \
			     IDPF_TX_DESCS_PER_CACHE_LINE + \
			     IDPF_TX_DESCS_FOR_SKB_DATA_PTR)

/* The size limit for a transmit buffer in a descriptor is (16K - 1).
 * In order to align with the read requests we will align the value to
 * the nearest 4K which represents our maximum read request size.
 */
#define IDPF_TX_MAX_READ_REQ_SIZE	SZ_4K
#define IDPF_TX_MAX_DESC_DATA		(SZ_16K - 1)
#define IDPF_TX_MAX_DESC_DATA_ALIGNED \
	ALIGN_DOWN(IDPF_TX_MAX_DESC_DATA, IDPF_TX_MAX_READ_REQ_SIZE)

#define IDPF_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)
#define IDPF_RX_DESC(rxq, i)	\
	(&(((union virtchnl2_rx_desc *)((rxq)->desc_ring))[i]))

struct idpf_rx_buf {
	struct page *page;
	unsigned int page_offset;
	u16 truesize;
};

#define IDPF_RX_MAX_PTYPE_PROTO_IDS    32
#define IDPF_RX_MAX_PTYPE_SZ	(sizeof(struct virtchnl2_ptype) + \
				 (sizeof(u16) * IDPF_RX_MAX_PTYPE_PROTO_IDS))
#define IDPF_RX_PTYPE_HDR_SZ	sizeof(struct virtchnl2_get_ptype_info)
#define IDPF_RX_MAX_PTYPES_PER_BUF	\
	DIV_ROUND_DOWN_ULL((IDPF_CTLQ_MAX_BUF_LEN - IDPF_RX_PTYPE_HDR_SZ), \
			   IDPF_RX_MAX_PTYPE_SZ)

#define IDPF_GET_PTYPE_SIZE(p) struct_size((p), proto_id, (p)->proto_id_count)

#define IDPF_TUN_IP_GRE (\
	IDPF_PTYPE_TUNNEL_IP |\
	IDPF_PTYPE_TUNNEL_IP_GRENAT)

#define IDPF_TUN_IP_GRE_MAC (\
	IDPF_TUN_IP_GRE |\
	IDPF_PTYPE_TUNNEL_IP_GRENAT_MAC)

#define IDPF_RX_MAX_PTYPE	1024
#define IDPF_RX_MAX_BASE_PTYPE	256
#define IDPF_INVALID_PTYPE_ID	0xFFFF

/* Packet type non-ip values */
enum idpf_rx_ptype_l2 {
	IDPF_RX_PTYPE_L2_RESERVED	= 0,
	IDPF_RX_PTYPE_L2_MAC_PAY2	= 1,
	IDPF_RX_PTYPE_L2_TIMESYNC_PAY2	= 2,
	IDPF_RX_PTYPE_L2_FIP_PAY2	= 3,
	IDPF_RX_PTYPE_L2_OUI_PAY2	= 4,
	IDPF_RX_PTYPE_L2_MACCNTRL_PAY2	= 5,
	IDPF_RX_PTYPE_L2_LLDP_PAY2	= 6,
	IDPF_RX_PTYPE_L2_ECP_PAY2	= 7,
	IDPF_RX_PTYPE_L2_EVB_PAY2	= 8,
	IDPF_RX_PTYPE_L2_QCN_PAY2	= 9,
	IDPF_RX_PTYPE_L2_EAPOL_PAY2	= 10,
	IDPF_RX_PTYPE_L2_ARP		= 11,
};

enum idpf_rx_ptype_outer_ip {
	IDPF_RX_PTYPE_OUTER_L2	= 0,
	IDPF_RX_PTYPE_OUTER_IP	= 1,
};

#define IDPF_RX_PTYPE_TO_IPV(ptype, ipv)			\
	(((ptype)->outer_ip == IDPF_RX_PTYPE_OUTER_IP) &&	\
	 ((ptype)->outer_ip_ver == (ipv)))

enum idpf_rx_ptype_outer_ip_ver {
	IDPF_RX_PTYPE_OUTER_NONE	= 0,
	IDPF_RX_PTYPE_OUTER_IPV4	= 1,
	IDPF_RX_PTYPE_OUTER_IPV6	= 2,
};

enum idpf_rx_ptype_outer_fragmented {
	IDPF_RX_PTYPE_NOT_FRAG	= 0,
	IDPF_RX_PTYPE_FRAG	= 1,
};

enum idpf_rx_ptype_tunnel_type {
	IDPF_RX_PTYPE_TUNNEL_NONE		= 0,
	IDPF_RX_PTYPE_TUNNEL_IP_IP		= 1,
	IDPF_RX_PTYPE_TUNNEL_IP_GRENAT		= 2,
	IDPF_RX_PTYPE_TUNNEL_IP_GRENAT_MAC	= 3,
	IDPF_RX_PTYPE_TUNNEL_IP_GRENAT_MAC_VLAN	= 4,
};

enum idpf_rx_ptype_tunnel_end_prot {
	IDPF_RX_PTYPE_TUNNEL_END_NONE	= 0,
	IDPF_RX_PTYPE_TUNNEL_END_IPV4	= 1,
	IDPF_RX_PTYPE_TUNNEL_END_IPV6	= 2,
};

enum idpf_rx_ptype_inner_prot {
	IDPF_RX_PTYPE_INNER_PROT_NONE		= 0,
	IDPF_RX_PTYPE_INNER_PROT_UDP		= 1,
	IDPF_RX_PTYPE_INNER_PROT_TCP		= 2,
	IDPF_RX_PTYPE_INNER_PROT_SCTP		= 3,
	IDPF_RX_PTYPE_INNER_PROT_ICMP		= 4,
	IDPF_RX_PTYPE_INNER_PROT_TIMESYNC	= 5,
};

enum idpf_rx_ptype_payload_layer {
	IDPF_RX_PTYPE_PAYLOAD_LAYER_NONE	= 0,
	IDPF_RX_PTYPE_PAYLOAD_LAYER_PAY2	= 1,
	IDPF_RX_PTYPE_PAYLOAD_LAYER_PAY3	= 2,
	IDPF_RX_PTYPE_PAYLOAD_LAYER_PAY4	= 3,
};

enum idpf_tunnel_state {
	IDPF_PTYPE_TUNNEL_IP                    = BIT(0),
	IDPF_PTYPE_TUNNEL_IP_GRENAT             = BIT(1),
	IDPF_PTYPE_TUNNEL_IP_GRENAT_MAC         = BIT(2),
};

struct idpf_ptype_state {
	bool outer_ip;
	bool outer_frag;
	u8 tunnel_state;
};

struct idpf_rx_ptype_decoded {
	u32 ptype:10;
	u32 known:1;
	u32 outer_ip:1;
	u32 outer_ip_ver:2;
	u32 outer_frag:1;
	u32 tunnel_type:3;
	u32 tunnel_end_prot:2;
	u32 tunnel_end_frag:1;
	u32 inner_prot:4;
	u32 payload_layer:3;
};

/**
 * enum idpf_queue_flags_t
 * @__IDPF_Q_GEN_CHK: Queues operating in splitq mode use a generation bit to
 *		      identify new descriptor writebacks on the ring. HW sets
 *		      the gen bit to 1 on the first writeback of any given
 *		      descriptor. After the ring wraps, HW sets the gen bit of
 *		      those descriptors to 0, and continues flipping
 *		      0->1 or 1->0 on each ring wrap. SW maintains its own
 *		      gen bit to know what value will indicate writebacks on
 *		      the next pass around the ring. E.g. it is initialized
 *		      to 1 and knows that reading a gen bit of 1 in any
 *		      descriptor on the initial pass of the ring indicates a
 *		      writeback. It also flips on every ring wrap.
 * @__IDPF_RFLQ_GEN_CHK: Refill queues are SW only, so Q_GEN acts as the HW bit
 *			 and RFLGQ_GEN is the SW bit.
 * @__IDPF_Q_FLOW_SCH_EN: Enable flow scheduling
 * @__IDPF_Q_SW_MARKER: Used to indicate TX queue marker completions
 * @__IDPF_Q_POLL_MODE: Enable poll mode
 * @__IDPF_Q_FLAGS_NBITS: Must be last
 */
enum idpf_queue_flags_t {
	__IDPF_Q_GEN_CHK,
	__IDPF_RFLQ_GEN_CHK,
	__IDPF_Q_FLOW_SCH_EN,
	__IDPF_Q_SW_MARKER,
	__IDPF_Q_POLL_MODE,

	__IDPF_Q_FLAGS_NBITS,
};

/**
 * struct idpf_vec_regs
 * @dyn_ctl_reg: Dynamic control interrupt register offset
 * @itrn_reg: Interrupt Throttling Rate register offset
 * @itrn_index_spacing: Register spacing between ITR registers of the same
 *			vector
 */
struct idpf_vec_regs {
	u32 dyn_ctl_reg;
	u32 itrn_reg;
	u32 itrn_index_spacing;
};

/**
 * struct idpf_intr_reg
 * @dyn_ctl: Dynamic control interrupt register
 * @dyn_ctl_intena_m: Mask for dyn_ctl interrupt enable
 * @dyn_ctl_itridx_s: Register bit offset for ITR index
 * @dyn_ctl_itridx_m: Mask for ITR index
 * @dyn_ctl_intrvl_s: Register bit offset for ITR interval
 * @rx_itr: RX ITR register
 * @tx_itr: TX ITR register
 * @icr_ena: Interrupt cause register offset
 * @icr_ena_ctlq_m: Mask for ICR
 */
struct idpf_intr_reg {
	void __iomem *dyn_ctl;
	u32 dyn_ctl_intena_m;
	u32 dyn_ctl_itridx_s;
	u32 dyn_ctl_itridx_m;
	u32 dyn_ctl_intrvl_s;
	void __iomem *rx_itr;
	void __iomem *tx_itr;
	void __iomem *icr_ena;
	u32 icr_ena_ctlq_m;
};

/**
 * struct idpf_q_vector
 * @vport: Vport back pointer
 * @affinity_mask: CPU affinity mask
 * @napi: napi handler
 * @v_idx: Vector index
 * @intr_reg: See struct idpf_intr_reg
 * @num_txq: Number of TX queues
 * @tx: Array of TX queues to service
 * @tx_dim: Data for TX net_dim algorithm
 * @tx_itr_value: TX interrupt throttling rate
 * @tx_intr_mode: Dynamic ITR or not
 * @tx_itr_idx: TX ITR index
 * @num_rxq: Number of RX queues
 * @rx: Array of RX queues to service
 * @rx_dim: Data for RX net_dim algorithm
 * @rx_itr_value: RX interrupt throttling rate
 * @rx_intr_mode: Dynamic ITR or not
 * @rx_itr_idx: RX ITR index
 * @num_bufq: Number of buffer queues
 * @bufq: Array of buffer queues to service
 * @total_events: Number of interrupts processed
 * @name: Queue vector name
 */
struct idpf_q_vector {
	struct idpf_vport *vport;
	cpumask_t affinity_mask;
	struct napi_struct napi;
	u16 v_idx;
	struct idpf_intr_reg intr_reg;

	u16 num_txq;
	struct idpf_queue **tx;
	struct dim tx_dim;
	u16 tx_itr_value;
	bool tx_intr_mode;
	u32 tx_itr_idx;

	u16 num_rxq;
	struct idpf_queue **rx;
	struct dim rx_dim;
	u16 rx_itr_value;
	bool rx_intr_mode;
	u32 rx_itr_idx;

	u16 num_bufq;
	struct idpf_queue **bufq;

	u16 total_events;
	char *name;
};

struct idpf_rx_queue_stats {
	u64_stats_t packets;
	u64_stats_t bytes;
	u64_stats_t rsc_pkts;
	u64_stats_t hw_csum_err;
	u64_stats_t hsplit_pkts;
	u64_stats_t hsplit_buf_ovf;
	u64_stats_t bad_descs;
};

struct idpf_tx_queue_stats {
	u64_stats_t packets;
	u64_stats_t bytes;
	u64_stats_t lso_pkts;
	u64_stats_t linearize;
	u64_stats_t q_busy;
	u64_stats_t skb_drops;
	u64_stats_t dma_map_errs;
};

struct idpf_cleaned_stats {
	u32 packets;
	u32 bytes;
};

union idpf_queue_stats {
	struct idpf_rx_queue_stats rx;
	struct idpf_tx_queue_stats tx;
};

#define IDPF_ITR_DYNAMIC	1
#define IDPF_ITR_MAX		0x1FE0
#define IDPF_ITR_20K		0x0032
#define IDPF_ITR_GRAN_S		1	/* Assume ITR granularity is 2us */
#define IDPF_ITR_MASK		0x1FFE  /* ITR register value alignment mask */
#define ITR_REG_ALIGN(setting)	((setting) & IDPF_ITR_MASK)
#define IDPF_ITR_IS_DYNAMIC(itr_mode) (itr_mode)
#define IDPF_ITR_TX_DEF		IDPF_ITR_20K
#define IDPF_ITR_RX_DEF		IDPF_ITR_20K
/* Index used for 'No ITR' update in DYN_CTL register */
#define IDPF_NO_ITR_UPDATE_IDX	3
#define IDPF_ITR_IDX_SPACING(spacing, dflt)	(spacing ? spacing : dflt)
#define IDPF_DIM_DEFAULT_PROFILE_IX		1

/**
 * struct idpf_queue
 * @dev: Device back pointer for DMA mapping
 * @vport: Back pointer to associated vport
 * @txq_grp: See struct idpf_txq_group
 * @rxq_grp: See struct idpf_rxq_group
 * @idx: For buffer queue, it is used as group id, either 0 or 1. On clean,
 *	 buffer queue uses this index to determine which group of refill queues
 *	 to clean.
 *	 For TX queue, it is used as index to map between TX queue group and
 *	 hot path TX pointers stored in vport. Used in both singleq/splitq.
 *	 For RX queue, it is used to index to total RX queue across groups and
 *	 used for skb reporting.
 * @tail: Tail offset. Used for both queue models single and split. In splitq
 *	  model relevant only for TX queue and RX queue.
 * @tx_buf: See struct idpf_tx_buf
 * @rx_buf: Struct with RX buffer related members
 * @rx_buf.buf: See struct idpf_rx_buf
 * @rx_buf.hdr_buf_pa: DMA handle
 * @rx_buf.hdr_buf_va: Virtual address
 * @pp: Page pool pointer
 * @skb: Pointer to the skb
 * @q_type: Queue type (TX, RX, TX completion, RX buffer)
 * @q_id: Queue id
 * @desc_count: Number of descriptors
 * @next_to_use: Next descriptor to use. Relevant in both split & single txq
 *		 and bufq.
 * @next_to_clean: Next descriptor to clean. In split queue model, only
 *		   relevant to TX completion queue and RX queue.
 * @next_to_alloc: RX buffer to allocate at. Used only for RX. In splitq model
 *		   only relevant to RX queue.
 * @flags: See enum idpf_queue_flags_t
 * @q_stats: See union idpf_queue_stats
 * @stats_sync: See struct u64_stats_sync
 * @cleaned_bytes: Splitq only, TXQ only: When a TX completion is received on
 *		   the TX completion queue, it can be for any TXQ associated
 *		   with that completion queue. This means we can clean up to
 *		   N TXQs during a single call to clean the completion queue.
 *		   cleaned_bytes|pkts tracks the clean stats per TXQ during
 *		   that single call to clean the completion queue. By doing so,
 *		   we can update BQL with aggregate cleaned stats for each TXQ
 *		   only once at the end of the cleaning routine.
 * @cleaned_pkts: Number of packets cleaned for the above said case
 * @rx_hsplit_en: RX headsplit enable
 * @rx_hbuf_size: Header buffer size
 * @rx_buf_size: Buffer size
 * @rx_max_pkt_size: RX max packet size
 * @rx_buf_stride: RX buffer stride
 * @rx_buffer_low_watermark: RX buffer low watermark
 * @rxdids: Supported RX descriptor ids
 * @q_vector: Backreference to associated vector
 * @size: Length of descriptor ring in bytes
 * @dma: Physical address of ring
 * @desc_ring: Descriptor ring memory
 * @tx_max_bufs: Max buffers that can be transmitted with scatter-gather
 * @tx_min_pkt_len: Min supported packet length
 * @num_completions: Only relevant for TX completion queue. It tracks the
 *		     number of completions received to compare against the
 *		     number of completions pending, as accumulated by the
 *		     TX queues.
 * @buf_stack: Stack of empty buffers to store buffer info for out of order
 *	       buffer completions. See struct idpf_buf_lifo.
 * @compl_tag_bufid_m: Completion tag buffer id mask
 * @compl_tag_gen_s: Completion tag generation bit
 *	The format of the completion tag will change based on the TXQ
 *	descriptor ring size so that we can maintain roughly the same level
 *	of "uniqueness" across all descriptor sizes. For example, if the
 *	TXQ descriptor ring size is 64 (the minimum size supported), the
 *	completion tag will be formatted as below:
 *	15                 6 5         0
 *	--------------------------------
 *	|    GEN=0-1023     |IDX = 0-63|
 *	--------------------------------
 *
 *	This gives us 64*1024 = 65536 possible unique values. Similarly, if
 *	the TXQ descriptor ring size is 8160 (the maximum size supported),
 *	the completion tag will be formatted as below:
 *	15 13 12                       0
 *	--------------------------------
 *	|GEN |       IDX = 0-8159      |
 *	--------------------------------
 *
 *	This gives us 8*8160 = 65280 possible unique values.
 * @compl_tag_cur_gen: Used to keep track of current completion tag generation
 * @compl_tag_gen_max: To determine when compl_tag_cur_gen should be reset
 * @sched_buf_hash: Hash table to stores buffers
 */
struct idpf_queue {
	struct device *dev;
	struct idpf_vport *vport;
	union {
		struct idpf_txq_group *txq_grp;
		struct idpf_rxq_group *rxq_grp;
	};
	u16 idx;
	void __iomem *tail;
	union {
		struct idpf_tx_buf *tx_buf;
		struct {
			struct idpf_rx_buf *buf;
			dma_addr_t hdr_buf_pa;
			void *hdr_buf_va;
		} rx_buf;
	};
	struct page_pool *pp;
	struct sk_buff *skb;
	u16 q_type;
	u32 q_id;
	u16 desc_count;

	u16 next_to_use;
	u16 next_to_clean;
	u16 next_to_alloc;
	DECLARE_BITMAP(flags, __IDPF_Q_FLAGS_NBITS);

	union idpf_queue_stats q_stats;
	struct u64_stats_sync stats_sync;

	u32 cleaned_bytes;
	u16 cleaned_pkts;

	bool rx_hsplit_en;
	u16 rx_hbuf_size;
	u16 rx_buf_size;
	u16 rx_max_pkt_size;
	u16 rx_buf_stride;
	u8 rx_buffer_low_watermark;
	u64 rxdids;
	struct idpf_q_vector *q_vector;
	unsigned int size;
	dma_addr_t dma;
	void *desc_ring;

	u16 tx_max_bufs;
	u8 tx_min_pkt_len;

	u32 num_completions;

	struct idpf_buf_lifo buf_stack;

	u16 compl_tag_bufid_m;
	u16 compl_tag_gen_s;

	u16 compl_tag_cur_gen;
	u16 compl_tag_gen_max;

	DECLARE_HASHTABLE(sched_buf_hash, 12);
} ____cacheline_internodealigned_in_smp;

/**
 * struct idpf_sw_queue
 * @next_to_clean: Next descriptor to clean
 * @next_to_alloc: Buffer to allocate at
 * @flags: See enum idpf_queue_flags_t
 * @ring: Pointer to the ring
 * @desc_count: Descriptor count
 * @dev: Device back pointer for DMA mapping
 *
 * Software queues are used in splitq mode to manage buffers between rxq
 * producer and the bufq consumer.  These are required in order to maintain a
 * lockless buffer management system and are strictly software only constructs.
 */
struct idpf_sw_queue {
	u16 next_to_clean;
	u16 next_to_alloc;
	DECLARE_BITMAP(flags, __IDPF_Q_FLAGS_NBITS);
	u16 *ring;
	u16 desc_count;
	struct device *dev;
} ____cacheline_internodealigned_in_smp;

/**
 * struct idpf_rxq_set
 * @rxq: RX queue
 * @refillq0: Pointer to refill queue 0
 * @refillq1: Pointer to refill queue 1
 *
 * Splitq only.  idpf_rxq_set associates an rxq with at an array of refillqs.
 * Each rxq needs a refillq to return used buffers back to the respective bufq.
 * Bufqs then clean these refillqs for buffers to give to hardware.
 */
struct idpf_rxq_set {
	struct idpf_queue rxq;
	struct idpf_sw_queue *refillq0;
	struct idpf_sw_queue *refillq1;
};

/**
 * struct idpf_bufq_set
 * @bufq: Buffer queue
 * @num_refillqs: Number of refill queues. This is always equal to num_rxq_sets
 *		  in idpf_rxq_group.
 * @refillqs: Pointer to refill queues array.
 *
 * Splitq only. idpf_bufq_set associates a bufq to an array of refillqs.
 * In this bufq_set, there will be one refillq for each rxq in this rxq_group.
 * Used buffers received by rxqs will be put on refillqs which bufqs will
 * clean to return new buffers back to hardware.
 *
 * Buffers needed by some number of rxqs associated in this rxq_group are
 * managed by at most two bufqs (depending on performance configuration).
 */
struct idpf_bufq_set {
	struct idpf_queue bufq;
	int num_refillqs;
	struct idpf_sw_queue *refillqs;
};

/**
 * struct idpf_rxq_group
 * @vport: Vport back pointer
 * @singleq: Struct with single queue related members
 * @singleq.num_rxq: Number of RX queues associated
 * @singleq.rxqs: Array of RX queue pointers
 * @splitq: Struct with split queue related members
 * @splitq.num_rxq_sets: Number of RX queue sets
 * @splitq.rxq_sets: Array of RX queue sets
 * @splitq.bufq_sets: Buffer queue set pointer
 *
 * In singleq mode, an rxq_group is simply an array of rxqs.  In splitq, a
 * rxq_group contains all the rxqs, bufqs and refillqs needed to
 * manage buffers in splitq mode.
 */
struct idpf_rxq_group {
	struct idpf_vport *vport;

	union {
		struct {
			u16 num_rxq;
			struct idpf_queue *rxqs[IDPF_LARGE_MAX_Q];
		} singleq;
		struct {
			u16 num_rxq_sets;
			struct idpf_rxq_set *rxq_sets[IDPF_LARGE_MAX_Q];
			struct idpf_bufq_set *bufq_sets;
		} splitq;
	};
};

/**
 * struct idpf_txq_group
 * @vport: Vport back pointer
 * @num_txq: Number of TX queues associated
 * @txqs: Array of TX queue pointers
 * @complq: Associated completion queue pointer, split queue only
 * @num_completions_pending: Total number of completions pending for the
 *			     completion queue, acculumated for all TX queues
 *			     associated with that completion queue.
 *
 * Between singleq and splitq, a txq_group is largely the same except for the
 * complq. In splitq a single complq is responsible for handling completions
 * for some number of txqs associated in this txq_group.
 */
struct idpf_txq_group {
	struct idpf_vport *vport;

	u16 num_txq;
	struct idpf_queue *txqs[IDPF_LARGE_MAX_Q];

	struct idpf_queue *complq;

	u32 num_completions_pending;
};

/**
 * idpf_size_to_txd_count - Get number of descriptors needed for large Tx frag
 * @size: transmit request size in bytes
 *
 * In the case where a large frag (>= 16K) needs to be split across multiple
 * descriptors, we need to assume that we can have no more than 12K of data
 * per descriptor due to hardware alignment restrictions (4K alignment).
 */
static inline u32 idpf_size_to_txd_count(unsigned int size)
{
	return DIV_ROUND_UP(size, IDPF_TX_MAX_DESC_DATA_ALIGNED);
}

/**
 * idpf_tx_singleq_build_ctob - populate command tag offset and size
 * @td_cmd: Command to be filled in desc
 * @td_offset: Offset to be filled in desc
 * @size: Size of the buffer
 * @td_tag: td tag to be filled
 *
 * Returns the 64 bit value populated with the input parameters
 */
static inline __le64 idpf_tx_singleq_build_ctob(u64 td_cmd, u64 td_offset,
						unsigned int size, u64 td_tag)
{
	return cpu_to_le64(IDPF_TX_DESC_DTYPE_DATA |
			   (td_cmd << IDPF_TXD_QW1_CMD_S) |
			   (td_offset << IDPF_TXD_QW1_OFFSET_S) |
			   ((u64)size << IDPF_TXD_QW1_TX_BUF_SZ_S) |
			   (td_tag << IDPF_TXD_QW1_L2TAG1_S));
}

void idpf_tx_splitq_build_ctb(union idpf_tx_flex_desc *desc,
			      struct idpf_tx_splitq_params *params,
			      u16 td_cmd, u16 size);
void idpf_tx_splitq_build_flow_desc(union idpf_tx_flex_desc *desc,
				    struct idpf_tx_splitq_params *params,
				    u16 td_cmd, u16 size);
/**
 * idpf_tx_splitq_build_desc - determine which type of data descriptor to build
 * @desc: descriptor to populate
 * @params: pointer to tx params struct
 * @td_cmd: command to be filled in desc
 * @size: size of buffer
 */
static inline void idpf_tx_splitq_build_desc(union idpf_tx_flex_desc *desc,
					     struct idpf_tx_splitq_params *params,
					     u16 td_cmd, u16 size)
{
	if (params->dtype == IDPF_TX_DESC_DTYPE_FLEX_L2TAG1_L2TAG2)
		idpf_tx_splitq_build_ctb(desc, params, td_cmd, size);
	else
		idpf_tx_splitq_build_flow_desc(desc, params, td_cmd, size);
}

/**
 * idpf_alloc_page - Allocate a new RX buffer from the page pool
 * @pool: page_pool to allocate from
 * @buf: metadata struct to populate with page info
 * @buf_size: 2K or 4K
 *
 * Returns &dma_addr_t to be passed to HW for Rx, %DMA_MAPPING_ERROR otherwise.
 */
static inline dma_addr_t idpf_alloc_page(struct page_pool *pool,
					 struct idpf_rx_buf *buf,
					 unsigned int buf_size)
{
	if (buf_size == IDPF_RX_BUF_2048)
		buf->page = page_pool_dev_alloc_frag(pool, &buf->page_offset,
						     buf_size);
	else
		buf->page = page_pool_dev_alloc_pages(pool);

	if (!buf->page)
		return DMA_MAPPING_ERROR;

	buf->truesize = buf_size;

	return page_pool_get_dma_addr(buf->page) + buf->page_offset +
	       pool->p.offset;
}

/**
 * idpf_rx_put_page - Return RX buffer page to pool
 * @rx_buf: RX buffer metadata struct
 */
static inline void idpf_rx_put_page(struct idpf_rx_buf *rx_buf)
{
	page_pool_put_page(rx_buf->page->pp, rx_buf->page,
			   rx_buf->truesize, true);
	rx_buf->page = NULL;
}

/**
 * idpf_rx_sync_for_cpu - Synchronize DMA buffer
 * @rx_buf: RX buffer metadata struct
 * @len: frame length from descriptor
 */
static inline void idpf_rx_sync_for_cpu(struct idpf_rx_buf *rx_buf, u32 len)
{
	struct page *page = rx_buf->page;
	struct page_pool *pp = page->pp;

	dma_sync_single_range_for_cpu(pp->p.dev,
				      page_pool_get_dma_addr(page),
				      rx_buf->page_offset + pp->p.offset, len,
				      page_pool_get_dma_dir(pp));
}

int idpf_vport_singleq_napi_poll(struct napi_struct *napi, int budget);
void idpf_vport_init_num_qs(struct idpf_vport *vport,
			    struct virtchnl2_create_vport *vport_msg);
void idpf_vport_calc_num_q_desc(struct idpf_vport *vport);
int idpf_vport_calc_total_qs(struct idpf_adapter *adapter, u16 vport_index,
			     struct virtchnl2_create_vport *vport_msg,
			     struct idpf_vport_max_q *max_q);
void idpf_vport_calc_num_q_groups(struct idpf_vport *vport);
int idpf_vport_queues_alloc(struct idpf_vport *vport);
void idpf_vport_queues_rel(struct idpf_vport *vport);
void idpf_vport_intr_rel(struct idpf_vport *vport);
int idpf_vport_intr_alloc(struct idpf_vport *vport);
void idpf_vport_intr_update_itr_ena_irq(struct idpf_q_vector *q_vector);
void idpf_vport_intr_deinit(struct idpf_vport *vport);
int idpf_vport_intr_init(struct idpf_vport *vport);
void idpf_vport_intr_ena(struct idpf_vport *vport);
enum pkt_hash_types idpf_ptype_to_htype(const struct idpf_rx_ptype_decoded *decoded);
int idpf_config_rss(struct idpf_vport *vport);
int idpf_init_rss(struct idpf_vport *vport);
void idpf_deinit_rss(struct idpf_vport *vport);
int idpf_rx_bufs_init_all(struct idpf_vport *vport);
void idpf_rx_add_frag(struct idpf_rx_buf *rx_buf, struct sk_buff *skb,
		      unsigned int size);
struct sk_buff *idpf_rx_construct_skb(struct idpf_queue *rxq,
				      struct idpf_rx_buf *rx_buf,
				      unsigned int size);
bool idpf_init_rx_buf_hw_alloc(struct idpf_queue *rxq, struct idpf_rx_buf *buf);
void idpf_rx_buf_hw_update(struct idpf_queue *rxq, u32 val);
void idpf_tx_buf_hw_update(struct idpf_queue *tx_q, u32 val,
			   bool xmit_more);
unsigned int idpf_size_to_txd_count(unsigned int size);
netdev_tx_t idpf_tx_drop_skb(struct idpf_queue *tx_q, struct sk_buff *skb);
void idpf_tx_dma_map_error(struct idpf_queue *txq, struct sk_buff *skb,
			   struct idpf_tx_buf *first, u16 ring_idx);
unsigned int idpf_tx_desc_count_required(struct idpf_queue *txq,
					 struct sk_buff *skb);
bool idpf_chk_linearize(struct sk_buff *skb, unsigned int max_bufs,
			unsigned int count);
int idpf_tx_maybe_stop_common(struct idpf_queue *tx_q, unsigned int size);
void idpf_tx_timeout(struct net_device *netdev, unsigned int txqueue);
netdev_tx_t idpf_tx_splitq_start(struct sk_buff *skb,
				 struct net_device *netdev);
netdev_tx_t idpf_tx_singleq_start(struct sk_buff *skb,
				  struct net_device *netdev);
bool idpf_rx_singleq_buf_hw_alloc_all(struct idpf_queue *rxq,
				      u16 cleaned_count);
int idpf_tso(struct sk_buff *skb, struct idpf_tx_offload_params *off);

#endif /* !_IDPF_TXRX_H_ */
