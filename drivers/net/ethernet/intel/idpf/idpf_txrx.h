/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_TXRX_H_
#define _IDPF_TXRX_H_

#include <net/page_pool/helpers.h>

#define IDPF_LARGE_MAX_Q			256
#define IDPF_MAX_Q				16
#define IDPF_MIN_Q				2

#define IDPF_MIN_TXQ_COMPLQ_DESC		256
#define IDPF_MAX_QIDS				256

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

#define IDPF_RX_BUF_2048			2048
#define IDPF_RX_BUF_4096			4096
#define IDPF_RX_BUF_STRIDE			32
#define IDPF_LOW_WATERMARK			64
#define IDPF_HDR_BUF_SIZE			256
#define IDPF_PACKET_HDR_PAD	\
	(ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN * 2)

#define IDPF_SINGLEQ_RX_BUF_DESC(rxq, i)	\
	(&(((struct virtchnl2_singleq_rx_buf_desc *)((rxq)->desc_ring))[i]))
#define IDPF_SPLITQ_RX_BUF_DESC(rxq, i)	\
	(&(((struct virtchnl2_splitq_rx_buf_desc *)((rxq)->desc_ring))[i]))

#define IDPF_TX_SPLITQ_COMPL_TAG_WIDTH	16
#define IDPF_SPLITQ_TX_INVAL_COMPL_TAG	-1

#define IDPF_TX_MIN_PKT_LEN		17

/**
 * struct idpf_tx_buf
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
	union {
		int compl_tag;

		bool ctx_entry;
	};
};

struct idpf_tx_stash {
	/* stub */
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

#define IDPF_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

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
 * @__IDPF_Q_FLAGS_NBITS: Must be last
 */
enum idpf_queue_flags_t {
	__IDPF_Q_GEN_CHK,
	__IDPF_RFLQ_GEN_CHK,
	__IDPF_Q_FLOW_SCH_EN,

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
 * @tx_itr_value: TX interrupt throttling rate
 * @tx_intr_mode: Dynamic ITR or not
 * @tx_itr_idx: TX ITR index
 * @num_rxq: Number of RX queues
 * @rx: Array of RX queues to service
 * @rx_itr_value: RX interrupt throttling rate
 * @rx_intr_mode: Dynamic ITR or not
 * @rx_itr_idx: RX ITR index
 * @num_bufq: Number of buffer queues
 * @bufq: Array of buffer queues to service
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
	u16 tx_itr_value;
	bool tx_intr_mode;
	u32 tx_itr_idx;

	u16 num_rxq;
	struct idpf_queue **rx;
	u16 rx_itr_value;
	bool rx_intr_mode;
	u32 rx_itr_idx;

	u16 num_bufq;
	struct idpf_queue **bufq;

	char *name;
};

#define IDPF_ITR_DYNAMIC	1
#define IDPF_ITR_20K		0x0032
#define IDPF_ITR_TX_DEF		IDPF_ITR_20K
#define IDPF_ITR_RX_DEF		IDPF_ITR_20K
#define IDPF_ITR_IDX_SPACING(spacing, dflt)	(spacing ? spacing : dflt)

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

	struct idpf_buf_lifo buf_stack;

	u16 compl_tag_bufid_m;
	u16 compl_tag_gen_s;

	u16 compl_tag_cur_gen;
	u16 compl_tag_gen_max;

	DECLARE_HASHTABLE(sched_buf_hash, 12);
} ____cacheline_internodealigned_in_smp;

/**
 * struct idpf_sw_queue
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
void idpf_vport_intr_deinit(struct idpf_vport *vport);
int idpf_vport_intr_init(struct idpf_vport *vport);
int idpf_config_rss(struct idpf_vport *vport);
int idpf_init_rss(struct idpf_vport *vport);
void idpf_deinit_rss(struct idpf_vport *vport);
int idpf_rx_bufs_init_all(struct idpf_vport *vport);
bool idpf_init_rx_buf_hw_alloc(struct idpf_queue *rxq, struct idpf_rx_buf *buf);
void idpf_rx_buf_hw_update(struct idpf_queue *rxq, u32 val);
bool idpf_rx_singleq_buf_hw_alloc_all(struct idpf_queue *rxq,
				      u16 cleaned_count);

#endif /* !_IDPF_TXRX_H_ */
