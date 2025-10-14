/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_TXRX_H_
#define _IDPF_TXRX_H_

#include <linux/dim.h>

#include <net/libeth/cache.h>
#include <net/libeth/types.h>
#include <net/netdev_queues.h>
#include <net/tcp.h>
#include <net/xdp.h>

#include "idpf_lan_txrx.h"
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
#define IDPF_MIN_RDMA_VEC	2
/* Data vector for NOIRQ queues */
#define IDPF_RESERVED_VECS			1

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
		idpf_queue_change(GEN_CHK, rxq);		\
	}							\
} while (0)

#define IDPF_SINGLEQ_BUMP_RING_IDX(q, idx)			\
do {								\
	if (unlikely(++(idx) == (q)->desc_count))		\
		idx = 0;					\
} while (0)

#define IDPF_RX_BUF_STRIDE			32
#define IDPF_RX_BUF_POST_STRIDE			16
#define IDPF_LOW_WATERMARK			64

#define IDPF_TX_TSO_MIN_MSS			88

/* Minimum number of descriptors between 2 descriptors with the RE bit set;
 * only relevant in flow scheduling mode
 */
#define IDPF_TX_SPLITQ_RE_MIN_GAP	64

#define IDPF_RFL_BI_GEN_M		BIT(16)
#define IDPF_RFL_BI_BUFID_M		GENMASK(15, 0)

#define IDPF_RXD_EOF_SPLITQ		VIRTCHNL2_RX_FLEX_DESC_ADV_STATUS0_EOF_M
#define IDPF_RXD_EOF_SINGLEQ		VIRTCHNL2_RX_BASE_DESC_STATUS_EOF_M

#define IDPF_DESC_UNUSED(txq)     \
	((((txq)->next_to_clean > (txq)->next_to_use) ? 0 : (txq)->desc_count) + \
	(txq)->next_to_clean - (txq)->next_to_use - 1)

#define IDPF_TX_COMPLQ_OVERFLOW_THRESH(txcq)	((txcq)->desc_count >> 1)
/* Determine the absolute number of completions pending, i.e. the number of
 * completions that are expected to arrive on the TX completion queue.
 */
#define IDPF_TX_COMPLQ_PENDING(txq)	\
	(((txq)->num_completions_pending >= (txq)->complq->num_completions ? \
	0 : U32_MAX) + \
	(txq)->num_completions_pending - (txq)->complq->num_completions)

#define IDPF_TXBUF_NULL			U32_MAX

#define IDPF_TXD_LAST_DESC_CMD (IDPF_TX_DESC_CMD_EOP | IDPF_TX_DESC_CMD_RS)

#define IDPF_TX_FLAGS_TSO		BIT(0)
#define IDPF_TX_FLAGS_IPV4		BIT(1)
#define IDPF_TX_FLAGS_IPV6		BIT(2)
#define IDPF_TX_FLAGS_TUNNEL		BIT(3)
#define IDPF_TX_FLAGS_TSYN		BIT(4)

struct libeth_rq_napi_stats;

union idpf_tx_flex_desc {
	struct idpf_flex_tx_desc q; /* queue based scheduling */
	struct idpf_flex_tx_sched_desc flow; /* flow based scheduling */
};

#define idpf_tx_buf libeth_sqe

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
 * @prev_ntu: stored TxQ next_to_use in case of rollback
 * @prev_refill_ntc: stored refillq next_to_clean in case of packet rollback
 * @prev_refill_gen: stored refillq generation bit in case of packet rollback
 */
struct idpf_tx_splitq_params {
	enum idpf_tx_desc_dtype_value dtype;
	u16 eop_cmd;
	union {
		u16 compl_tag;
		u16 td_tag;
	};

	struct idpf_tx_offload_params offload;

	u16 prev_ntu;
	u16 prev_refill_ntc;
	bool prev_refill_gen;
};

enum idpf_tx_ctx_desc_eipt_offload {
	IDPF_TX_CTX_EXT_IP_NONE         = 0x0,
	IDPF_TX_CTX_EXT_IP_IPV6         = 0x1,
	IDPF_TX_CTX_EXT_IP_IPV4_NO_CSUM = 0x2,
	IDPF_TX_CTX_EXT_IP_IPV4         = 0x3
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

#define idpf_rx_buf libeth_fqe

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

enum idpf_tunnel_state {
	IDPF_PTYPE_TUNNEL_IP                    = BIT(0),
	IDPF_PTYPE_TUNNEL_IP_GRENAT             = BIT(1),
	IDPF_PTYPE_TUNNEL_IP_GRENAT_MAC         = BIT(2),
};

struct idpf_ptype_state {
	bool outer_ip:1;
	bool outer_frag:1;
	u8 tunnel_state:6;
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
 * @__IDPF_Q_RFL_GEN_CHK: Refill queues are SW only, so Q_GEN acts as the HW
 *			  bit and Q_RFL_GEN is the SW bit.
 * @__IDPF_Q_FLOW_SCH_EN: Enable flow scheduling
 * @__IDPF_Q_SW_MARKER: Used to indicate TX queue marker completions
 * @__IDPF_Q_CRC_EN: enable CRC offload in singleq mode
 * @__IDPF_Q_HSPLIT_EN: enable header split on Rx (splitq)
 * @__IDPF_Q_PTP: indicates whether the Rx timestamping is enabled for the
 *		  queue
 * @__IDPF_Q_NOIRQ: queue is polling-driven and has no interrupt
 * @__IDPF_Q_XDP: this is an XDP queue
 * @__IDPF_Q_XSK: the queue has an XSk pool installed
 * @__IDPF_Q_FLAGS_NBITS: Must be last
 */
enum idpf_queue_flags_t {
	__IDPF_Q_GEN_CHK,
	__IDPF_Q_RFL_GEN_CHK,
	__IDPF_Q_FLOW_SCH_EN,
	__IDPF_Q_SW_MARKER,
	__IDPF_Q_CRC_EN,
	__IDPF_Q_HSPLIT_EN,
	__IDPF_Q_PTP,
	__IDPF_Q_NOIRQ,
	__IDPF_Q_XDP,
	__IDPF_Q_XSK,

	__IDPF_Q_FLAGS_NBITS,
};

#define idpf_queue_set(f, q)		__set_bit(__IDPF_Q_##f, (q)->flags)
#define idpf_queue_clear(f, q)		__clear_bit(__IDPF_Q_##f, (q)->flags)
#define idpf_queue_change(f, q)		__change_bit(__IDPF_Q_##f, (q)->flags)
#define idpf_queue_has(f, q)		test_bit(__IDPF_Q_##f, (q)->flags)

#define idpf_queue_has_clear(f, q)			\
	__test_and_clear_bit(__IDPF_Q_##f, (q)->flags)
#define idpf_queue_assign(f, q, v)			\
	__assign_bit(__IDPF_Q_##f, (q)->flags, v)

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
 * @dyn_ctl_intena_msk_m: Mask for dyn_ctl interrupt enable mask
 * @dyn_ctl_itridx_s: Register bit offset for ITR index
 * @dyn_ctl_itridx_m: Mask for ITR index
 * @dyn_ctl_intrvl_s: Register bit offset for ITR interval
 * @dyn_ctl_wb_on_itr_m: Mask for WB on ITR feature
 * @dyn_ctl_sw_itridx_ena_m: Mask for SW ITR index
 * @dyn_ctl_swint_trig_m: Mask for dyn_ctl SW triggered interrupt enable
 * @rx_itr: RX ITR register
 * @tx_itr: TX ITR register
 * @icr_ena: Interrupt cause register offset
 * @icr_ena_ctlq_m: Mask for ICR
 */
struct idpf_intr_reg {
	void __iomem *dyn_ctl;
	u32 dyn_ctl_intena_m;
	u32 dyn_ctl_intena_msk_m;
	u32 dyn_ctl_itridx_s;
	u32 dyn_ctl_itridx_m;
	u32 dyn_ctl_intrvl_s;
	u32 dyn_ctl_wb_on_itr_m;
	u32 dyn_ctl_sw_itridx_ena_m;
	u32 dyn_ctl_swint_trig_m;
	void __iomem *rx_itr;
	void __iomem *tx_itr;
	void __iomem *icr_ena;
	u32 icr_ena_ctlq_m;
};

/**
 * struct idpf_q_vector
 * @vport: Vport back pointer
 * @num_rxq: Number of RX queues
 * @num_txq: Number of TX queues
 * @num_bufq: Number of buffer queues
 * @num_complq: number of completion queues
 * @num_xsksq: number of XSk send queues
 * @rx: Array of RX queues to service
 * @tx: Array of TX queues to service
 * @bufq: Array of buffer queues to service
 * @complq: array of completion queues
 * @xsksq: array of XSk send queues
 * @intr_reg: See struct idpf_intr_reg
 * @csd: XSk wakeup CSD
 * @total_events: Number of interrupts processed
 * @wb_on_itr: whether WB on ITR is enabled
 * @napi: napi handler
 * @tx_dim: Data for TX net_dim algorithm
 * @tx_itr_value: TX interrupt throttling rate
 * @tx_intr_mode: Dynamic ITR or not
 * @tx_itr_idx: TX ITR index
 * @rx_dim: Data for RX net_dim algorithm
 * @rx_itr_value: RX interrupt throttling rate
 * @rx_intr_mode: Dynamic ITR or not
 * @rx_itr_idx: RX ITR index
 * @v_idx: Vector index
 */
struct idpf_q_vector {
	__cacheline_group_begin_aligned(read_mostly);
	struct idpf_vport *vport;

	u16 num_rxq;
	u16 num_txq;
	u16 num_bufq;
	u16 num_complq;
	u16 num_xsksq;
	struct idpf_rx_queue **rx;
	struct idpf_tx_queue **tx;
	struct idpf_buf_queue **bufq;
	struct idpf_compl_queue **complq;
	struct idpf_tx_queue **xsksq;

	struct idpf_intr_reg intr_reg;
	__cacheline_group_end_aligned(read_mostly);

	__cacheline_group_begin_aligned(read_write);
	call_single_data_t csd;

	u16 total_events;
	bool wb_on_itr;

	struct napi_struct napi;

	struct dim tx_dim;
	u16 tx_itr_value;
	bool tx_intr_mode;
	u32 tx_itr_idx;

	struct dim rx_dim;
	u16 rx_itr_value;
	bool rx_intr_mode;
	u32 rx_itr_idx;
	__cacheline_group_end_aligned(read_write);

	__cacheline_group_begin_aligned(cold);
	u16 v_idx;

	__cacheline_group_end_aligned(cold);
};
libeth_cacheline_set_assert(struct idpf_q_vector, 136,
			    56 + sizeof(struct napi_struct) +
			    2 * sizeof(struct dim),
			    8);

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
	u64_stats_t tstamp_skipped;
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
/* Index used for 'SW ITR' update in DYN_CTL register */
#define IDPF_SW_ITR_UPDATE_IDX	2
/* Index used for 'No ITR' update in DYN_CTL register */
#define IDPF_NO_ITR_UPDATE_IDX	3
#define IDPF_ITR_IDX_SPACING(spacing, dflt)	(spacing ? spacing : dflt)
#define IDPF_DIM_DEFAULT_PROFILE_IX		1

/**
 * struct idpf_rx_queue - software structure representing a receive queue
 * @rx: universal receive descriptor array
 * @single_buf: buffer descriptor array in singleq
 * @desc_ring: virtual descriptor ring address
 * @bufq_sets: Pointer to the array of buffer queues in splitq mode
 * @napi: NAPI instance corresponding to this queue (splitq)
 * @xdp_prog: attached XDP program
 * @rx_buf: See struct &libeth_fqe
 * @pp: Page pool pointer in singleq mode
 * @tail: Tail offset. Used for both queue models single and split.
 * @flags: See enum idpf_queue_flags_t
 * @idx: For RX queue, it is used to index to total RX queue across groups and
 *	 used for skb reporting.
 * @desc_count: Number of descriptors
 * @num_xdp_txq: total number of XDP Tx queues
 * @xdpsqs: shortcut for XDP Tx queues array
 * @rxdids: Supported RX descriptor ids
 * @truesize: data buffer truesize in singleq
 * @rx_ptype_lkup: LUT of Rx ptypes
 * @xdp_rxq: XDP queue info
 * @next_to_use: Next descriptor to use
 * @next_to_clean: Next descriptor to clean
 * @next_to_alloc: RX buffer to allocate at
 * @xdp: XDP buffer with the current frame
 * @xsk: current XDP buffer in XSk mode
 * @pool: XSk pool if installed
 * @cached_phc_time: Cached PHC time for the Rx queue
 * @stats_sync: See struct u64_stats_sync
 * @q_stats: See union idpf_rx_queue_stats
 * @q_id: Queue id
 * @size: Length of descriptor ring in bytes
 * @dma: Physical address of ring
 * @q_vector: Backreference to associated vector
 * @rx_buffer_low_watermark: RX buffer low watermark
 * @rx_hbuf_size: Header buffer size
 * @rx_buf_size: Buffer size
 * @rx_max_pkt_size: RX max packet size
 */
struct idpf_rx_queue {
	__cacheline_group_begin_aligned(read_mostly);
	union {
		union virtchnl2_rx_desc *rx;
		struct virtchnl2_singleq_rx_buf_desc *single_buf;

		void *desc_ring;
	};
	union {
		struct {
			struct idpf_bufq_set *bufq_sets;
			struct napi_struct *napi;
			struct bpf_prog __rcu *xdp_prog;
		};
		struct {
			struct libeth_fqe *rx_buf;
			struct page_pool *pp;
			void __iomem *tail;
		};
	};

	DECLARE_BITMAP(flags, __IDPF_Q_FLAGS_NBITS);
	u16 idx;
	u16 desc_count;

	u32 num_xdp_txq;
	union {
		struct idpf_tx_queue **xdpsqs;
		struct {
			u32 rxdids;
			u32 truesize;
		};
	};
	const struct libeth_rx_pt *rx_ptype_lkup;

	struct xdp_rxq_info xdp_rxq;
	__cacheline_group_end_aligned(read_mostly);

	__cacheline_group_begin_aligned(read_write);
	u32 next_to_use;
	u32 next_to_clean;
	u32 next_to_alloc;

	union {
		struct libeth_xdp_buff_stash xdp;
		struct {
			struct libeth_xdp_buff *xsk;
			struct xsk_buff_pool *pool;
		};
	};
	u64 cached_phc_time;

	struct u64_stats_sync stats_sync;
	struct idpf_rx_queue_stats q_stats;
	__cacheline_group_end_aligned(read_write);

	__cacheline_group_begin_aligned(cold);
	u32 q_id;
	u32 size;
	dma_addr_t dma;

	struct idpf_q_vector *q_vector;

	u16 rx_buffer_low_watermark;
	u16 rx_hbuf_size;
	u16 rx_buf_size;
	u16 rx_max_pkt_size;
	__cacheline_group_end_aligned(cold);
};
libeth_cacheline_set_assert(struct idpf_rx_queue,
			    ALIGN(64, __alignof(struct xdp_rxq_info)) +
			    sizeof(struct xdp_rxq_info),
			    96 + offsetof(struct idpf_rx_queue, q_stats) -
			    offsetofend(struct idpf_rx_queue, cached_phc_time),
			    32);

/**
 * struct idpf_tx_queue - software structure representing a transmit queue
 * @base_tx: base Tx descriptor array
 * @base_ctx: base Tx context descriptor array
 * @flex_tx: flex Tx descriptor array
 * @flex_ctx: flex Tx context descriptor array
 * @desc_ring: virtual descriptor ring address
 * @tx_buf: See struct idpf_tx_buf
 * @txq_grp: See struct idpf_txq_group
 * @complq: corresponding completion queue in XDP mode
 * @dev: Device back pointer for DMA mapping
 * @pool: corresponding XSk pool if installed
 * @tail: Tail offset. Used for both queue models single and split
 * @flags: See enum idpf_queue_flags_t
 * @idx: For TX queue, it is used as index to map between TX queue group and
 *	 hot path TX pointers stored in vport. Used in both singleq/splitq.
 * @desc_count: Number of descriptors
 * @tx_min_pkt_len: Min supported packet length
 * @thresh: XDP queue cleaning threshold
 * @netdev: &net_device corresponding to this queue
 * @next_to_use: Next descriptor to use
 * @next_to_clean: Next descriptor to clean
 * @last_re: last descriptor index that RE bit was set
 * @tx_max_bufs: Max buffers that can be transmitted with scatter-gather
 * @cleaned_bytes: Splitq only, TXQ only: When a TX completion is received on
 *		   the TX completion queue, it can be for any TXQ associated
 *		   with that completion queue. This means we can clean up to
 *		   N TXQs during a single call to clean the completion queue.
 *		   cleaned_bytes|pkts tracks the clean stats per TXQ during
 *		   that single call to clean the completion queue. By doing so,
 *		   we can update BQL with aggregate cleaned stats for each TXQ
 *		   only once at the end of the cleaning routine.
 * @clean_budget: singleq only, queue cleaning budget
 * @cleaned_pkts: Number of packets cleaned for the above said case
 * @refillq: Pointer to refill queue
 * @pending: number of pending descriptors to send in QB
 * @xdp_tx: number of pending &xdp_buff or &xdp_frame buffers
 * @timer: timer for XDP Tx queue cleanup
 * @xdp_lock: lock for XDP Tx queues sharing
 * @cached_tstamp_caps: Tx timestamp capabilities negotiated with the CP
 * @tstamp_task: Work that handles Tx timestamp read
 * @stats_sync: See struct u64_stats_sync
 * @q_stats: See union idpf_tx_queue_stats
 * @q_id: Queue id
 * @size: Length of descriptor ring in bytes
 * @dma: Physical address of ring
 * @q_vector: Backreference to associated vector
 * @buf_pool_size: Total number of idpf_tx_buf
 * @rel_q_id: relative virtchnl queue index
 */
struct idpf_tx_queue {
	__cacheline_group_begin_aligned(read_mostly);
	union {
		struct idpf_base_tx_desc *base_tx;
		struct idpf_base_tx_ctx_desc *base_ctx;
		union idpf_tx_flex_desc *flex_tx;
		union idpf_flex_tx_ctx_desc *flex_ctx;

		void *desc_ring;
	};
	struct libeth_sqe *tx_buf;
	union {
		struct idpf_txq_group *txq_grp;
		struct idpf_compl_queue *complq;
	};
	union {
		struct device *dev;
		struct xsk_buff_pool *pool;
	};
	void __iomem *tail;

	DECLARE_BITMAP(flags, __IDPF_Q_FLAGS_NBITS);
	u16 idx;
	u16 desc_count;

	union {
		u16 tx_min_pkt_len;
		u32 thresh;
	};

	struct net_device *netdev;
	__cacheline_group_end_aligned(read_mostly);

	__cacheline_group_begin_aligned(read_write);
	u32 next_to_use;
	u32 next_to_clean;

	union {
		struct {
			u16 last_re;
			u16 tx_max_bufs;

			union {
				u32 cleaned_bytes;
				u32 clean_budget;
			};
			u16 cleaned_pkts;

			struct idpf_sw_queue *refillq;
		};
		struct {
			u32 pending;
			u32 xdp_tx;

			struct libeth_xdpsq_timer *timer;
			struct libeth_xdpsq_lock xdp_lock;
		};
	};

	struct idpf_ptp_vport_tx_tstamp_caps *cached_tstamp_caps;
	struct work_struct *tstamp_task;

	struct u64_stats_sync stats_sync;
	struct idpf_tx_queue_stats q_stats;
	__cacheline_group_end_aligned(read_write);

	__cacheline_group_begin_aligned(cold);
	u32 q_id;
	u32 size;
	dma_addr_t dma;

	struct idpf_q_vector *q_vector;

	u32 buf_pool_size;
	u32 rel_q_id;
	__cacheline_group_end_aligned(cold);
};
libeth_cacheline_set_assert(struct idpf_tx_queue, 64,
			    104 +
			    offsetof(struct idpf_tx_queue, cached_tstamp_caps) -
			    offsetofend(struct idpf_tx_queue, timer) +
			    offsetof(struct idpf_tx_queue, q_stats) -
			    offsetofend(struct idpf_tx_queue, tstamp_task),
			    32);

/**
 * struct idpf_buf_queue - software structure representing a buffer queue
 * @split_buf: buffer descriptor array
 * @buf: &libeth_fqe for data buffers
 * @pp: &page_pool for data buffers
 * @xsk_buf: &xdp_buff for XSk Rx buffers
 * @pool: &xsk_buff_pool on XSk queues
 * @hdr_buf: &libeth_fqe for header buffers
 * @hdr_pp: &page_pool for header buffers
 * @tail: Tail offset
 * @flags: See enum idpf_queue_flags_t
 * @desc_count: Number of descriptors
 * @thresh: refill threshold in XSk
 * @next_to_use: Next descriptor to use
 * @next_to_clean: Next descriptor to clean
 * @next_to_alloc: RX buffer to allocate at
 * @pending: number of buffers to refill (Xsk)
 * @hdr_truesize: truesize for buffer headers
 * @truesize: truesize for data buffers
 * @q_id: Queue id
 * @size: Length of descriptor ring in bytes
 * @dma: Physical address of ring
 * @q_vector: Backreference to associated vector
 * @rx_buffer_low_watermark: RX buffer low watermark
 * @rx_hbuf_size: Header buffer size
 * @rx_buf_size: Buffer size
 */
struct idpf_buf_queue {
	__cacheline_group_begin_aligned(read_mostly);
	struct virtchnl2_splitq_rx_buf_desc *split_buf;
	union {
		struct {
			struct libeth_fqe *buf;
			struct page_pool *pp;
		};
		struct {
			struct libeth_xdp_buff **xsk_buf;
			struct xsk_buff_pool *pool;
		};
	};
	struct libeth_fqe *hdr_buf;
	struct page_pool *hdr_pp;
	void __iomem *tail;

	DECLARE_BITMAP(flags, __IDPF_Q_FLAGS_NBITS);
	u32 desc_count;

	u32 thresh;
	__cacheline_group_end_aligned(read_mostly);

	__cacheline_group_begin_aligned(read_write);
	u32 next_to_use;
	u32 next_to_clean;
	u32 next_to_alloc;

	u32 pending;
	u32 hdr_truesize;
	u32 truesize;
	__cacheline_group_end_aligned(read_write);

	__cacheline_group_begin_aligned(cold);
	u32 q_id;
	u32 size;
	dma_addr_t dma;

	struct idpf_q_vector *q_vector;

	u16 rx_buffer_low_watermark;
	u16 rx_hbuf_size;
	u16 rx_buf_size;
	__cacheline_group_end_aligned(cold);
};
libeth_cacheline_set_assert(struct idpf_buf_queue, 64, 24, 32);

/**
 * struct idpf_compl_queue - software structure representing a completion queue
 * @comp: 8-byte completion descriptor array
 * @comp_4b: 4-byte completion descriptor array
 * @desc_ring: virtual descriptor ring address
 * @txq_grp: See struct idpf_txq_group
 * @flags: See enum idpf_queue_flags_t
 * @desc_count: Number of descriptors
 * @clean_budget: queue cleaning budget
 * @netdev: &net_device corresponding to this queue
 * @next_to_use: Next descriptor to use. Relevant in both split & single txq
 *		 and bufq.
 * @next_to_clean: Next descriptor to clean
 * @num_completions: Only relevant for TX completion queue. It tracks the
 *		     number of completions received to compare against the
 *		     number of completions pending, as accumulated by the
 *		     TX queues.
 * @q_id: Queue id
 * @size: Length of descriptor ring in bytes
 * @dma: Physical address of ring
 * @q_vector: Backreference to associated vector
 */
struct idpf_compl_queue {
	__cacheline_group_begin_aligned(read_mostly);
	union {
		struct idpf_splitq_tx_compl_desc *comp;
		struct idpf_splitq_4b_tx_compl_desc *comp_4b;

		void *desc_ring;
	};
	struct idpf_txq_group *txq_grp;

	DECLARE_BITMAP(flags, __IDPF_Q_FLAGS_NBITS);
	u32 desc_count;

	u32 clean_budget;
	struct net_device *netdev;
	__cacheline_group_end_aligned(read_mostly);

	__cacheline_group_begin_aligned(read_write);
	u32 next_to_use;
	u32 next_to_clean;

	aligned_u64 num_completions;
	__cacheline_group_end_aligned(read_write);

	__cacheline_group_begin_aligned(cold);
	u32 q_id;
	u32 size;
	dma_addr_t dma;

	struct idpf_q_vector *q_vector;
	__cacheline_group_end_aligned(cold);
};
libeth_cacheline_set_assert(struct idpf_compl_queue, 40, 16, 24);

/**
 * struct idpf_sw_queue
 * @ring: Pointer to the ring
 * @flags: See enum idpf_queue_flags_t
 * @desc_count: Descriptor count
 * @next_to_use: Buffer to allocate at
 * @next_to_clean: Next descriptor to clean
 *
 * Software queues are used in splitq mode to manage buffers between rxq
 * producer and the bufq consumer.  These are required in order to maintain a
 * lockless buffer management system and are strictly software only constructs.
 */
struct idpf_sw_queue {
	__cacheline_group_begin_aligned(read_mostly);
	u32 *ring;

	DECLARE_BITMAP(flags, __IDPF_Q_FLAGS_NBITS);
	u32 desc_count;
	__cacheline_group_end_aligned(read_mostly);

	__cacheline_group_begin_aligned(read_write);
	u32 next_to_use;
	u32 next_to_clean;
	__cacheline_group_end_aligned(read_write);
};
libeth_cacheline_group_assert(struct idpf_sw_queue, read_mostly, 24);
libeth_cacheline_group_assert(struct idpf_sw_queue, read_write, 8);
libeth_cacheline_struct_assert(struct idpf_sw_queue, 24, 8);

/**
 * struct idpf_rxq_set
 * @rxq: RX queue
 * @refillq: pointers to refill queues
 *
 * Splitq only.  idpf_rxq_set associates an rxq with at an array of refillqs.
 * Each rxq needs a refillq to return used buffers back to the respective bufq.
 * Bufqs then clean these refillqs for buffers to give to hardware.
 */
struct idpf_rxq_set {
	struct idpf_rx_queue rxq;
	struct idpf_sw_queue *refillq[IDPF_MAX_BUFQS_PER_RXQ_GRP];
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
	struct idpf_buf_queue bufq;
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
			struct idpf_rx_queue *rxqs[IDPF_LARGE_MAX_Q];
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
	struct idpf_tx_queue *txqs[IDPF_LARGE_MAX_Q];

	struct idpf_compl_queue *complq;

	aligned_u64 num_completions_pending;
};

static inline int idpf_q_vector_to_mem(const struct idpf_q_vector *q_vector)
{
	u32 cpu;

	if (!q_vector)
		return NUMA_NO_NODE;

	cpu = cpumask_first(&q_vector->napi.config->affinity_mask);

	return cpu < nr_cpu_ids ? cpu_to_mem(cpu) : NUMA_NO_NODE;
}

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
 * idpf_vport_intr_set_wb_on_itr - enable descriptor writeback on disabled interrupts
 * @q_vector: pointer to queue vector struct
 */
static inline void idpf_vport_intr_set_wb_on_itr(struct idpf_q_vector *q_vector)
{
	struct idpf_intr_reg *reg;

	if (q_vector->wb_on_itr)
		return;

	q_vector->wb_on_itr = true;
	reg = &q_vector->intr_reg;

	writel(reg->dyn_ctl_wb_on_itr_m | reg->dyn_ctl_intena_msk_m |
	       (IDPF_NO_ITR_UPDATE_IDX << reg->dyn_ctl_itridx_s),
	       reg->dyn_ctl);
}

/**
 * idpf_tx_splitq_get_free_bufs - get number of free buf_ids in refillq
 * @refillq: pointer to refillq containing buf_ids
 */
static inline u32 idpf_tx_splitq_get_free_bufs(struct idpf_sw_queue *refillq)
{
	return (refillq->next_to_use > refillq->next_to_clean ?
		0 : refillq->desc_count) +
	       refillq->next_to_use - refillq->next_to_clean - 1;
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
int idpf_config_rss(struct idpf_vport *vport);
int idpf_init_rss(struct idpf_vport *vport);
void idpf_deinit_rss(struct idpf_vport *vport);
int idpf_rx_bufs_init_all(struct idpf_vport *vport);

struct idpf_q_vector *idpf_find_rxq_vec(const struct idpf_vport *vport,
					u32 q_num);
struct idpf_q_vector *idpf_find_txq_vec(const struct idpf_vport *vport,
					u32 q_num);
int idpf_qp_switch(struct idpf_vport *vport, u32 qid, bool en);

void idpf_tx_buf_hw_update(struct idpf_tx_queue *tx_q, u32 val,
			   bool xmit_more);
unsigned int idpf_size_to_txd_count(unsigned int size);
netdev_tx_t idpf_tx_drop_skb(struct idpf_tx_queue *tx_q, struct sk_buff *skb);
unsigned int idpf_tx_res_count_required(struct idpf_tx_queue *txq,
					struct sk_buff *skb, u32 *buf_count);
void idpf_tx_timeout(struct net_device *netdev, unsigned int txqueue);
netdev_tx_t idpf_tx_singleq_frame(struct sk_buff *skb,
				  struct idpf_tx_queue *tx_q);
netdev_tx_t idpf_tx_start(struct sk_buff *skb, struct net_device *netdev);
bool idpf_rx_singleq_buf_hw_alloc_all(struct idpf_rx_queue *rxq,
				      u16 cleaned_count);
bool idpf_rx_process_skb_fields(struct sk_buff *skb,
				const struct libeth_xdp_buff *xdp,
				struct libeth_rq_napi_stats *rs);
int idpf_tso(struct sk_buff *skb, struct idpf_tx_offload_params *off);

void idpf_wait_for_sw_marker_completion(const struct idpf_tx_queue *txq);

#endif /* !_IDPF_TXRX_H_ */
