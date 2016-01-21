/*
 * Copyright (C) 2015 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#ifndef NICVF_QUEUES_H
#define NICVF_QUEUES_H

#include <linux/netdevice.h>
#include "q_struct.h"

#define MAX_QUEUE_SET			128
#define MAX_RCV_QUEUES_PER_QS		8
#define MAX_RCV_BUF_DESC_RINGS_PER_QS	2
#define MAX_SND_QUEUES_PER_QS		8
#define MAX_CMP_QUEUES_PER_QS		8

/* VF's queue interrupt ranges */
#define	NICVF_INTR_ID_CQ		0
#define	NICVF_INTR_ID_SQ		8
#define	NICVF_INTR_ID_RBDR		16
#define	NICVF_INTR_ID_MISC		18
#define	NICVF_INTR_ID_QS_ERR		19

#define	for_each_cq_irq(irq)	\
	for (irq = NICVF_INTR_ID_CQ; irq < NICVF_INTR_ID_SQ; irq++)
#define	for_each_sq_irq(irq)	\
	for (irq = NICVF_INTR_ID_SQ; irq < NICVF_INTR_ID_RBDR; irq++)
#define	for_each_rbdr_irq(irq)	\
	for (irq = NICVF_INTR_ID_RBDR; irq < NICVF_INTR_ID_MISC; irq++)

#define RBDR_SIZE0		0ULL /* 8K entries */
#define RBDR_SIZE1		1ULL /* 16K entries */
#define RBDR_SIZE2		2ULL /* 32K entries */
#define RBDR_SIZE3		3ULL /* 64K entries */
#define RBDR_SIZE4		4ULL /* 126K entries */
#define RBDR_SIZE5		5ULL /* 256K entries */
#define RBDR_SIZE6		6ULL /* 512K entries */

#define SND_QUEUE_SIZE0		0ULL /* 1K entries */
#define SND_QUEUE_SIZE1		1ULL /* 2K entries */
#define SND_QUEUE_SIZE2		2ULL /* 4K entries */
#define SND_QUEUE_SIZE3		3ULL /* 8K entries */
#define SND_QUEUE_SIZE4		4ULL /* 16K entries */
#define SND_QUEUE_SIZE5		5ULL /* 32K entries */
#define SND_QUEUE_SIZE6		6ULL /* 64K entries */

#define CMP_QUEUE_SIZE0		0ULL /* 1K entries */
#define CMP_QUEUE_SIZE1		1ULL /* 2K entries */
#define CMP_QUEUE_SIZE2		2ULL /* 4K entries */
#define CMP_QUEUE_SIZE3		3ULL /* 8K entries */
#define CMP_QUEUE_SIZE4		4ULL /* 16K entries */
#define CMP_QUEUE_SIZE5		5ULL /* 32K entries */
#define CMP_QUEUE_SIZE6		6ULL /* 64K entries */

/* Default queue count per QS, its lengths and threshold values */
#define RBDR_CNT		1
#define RCV_QUEUE_CNT		8
#define SND_QUEUE_CNT		8
#define CMP_QUEUE_CNT		8 /* Max of RCV and SND qcount */

#define SND_QSIZE		SND_QUEUE_SIZE2
#define SND_QUEUE_LEN		(1ULL << (SND_QSIZE + 10))
#define MAX_SND_QUEUE_LEN	(1ULL << (SND_QUEUE_SIZE6 + 10))
#define SND_QUEUE_THRESH	2ULL
#define MIN_SQ_DESC_PER_PKT_XMIT	2
/* Since timestamp not enabled, otherwise 2 */
#define MAX_CQE_PER_PKT_XMIT		1

/* Keep CQ and SQ sizes same, if timestamping
 * is enabled this equation will change.
 */
#define CMP_QSIZE		CMP_QUEUE_SIZE2
#define CMP_QUEUE_LEN		(1ULL << (CMP_QSIZE + 10))
#define CMP_QUEUE_CQE_THRESH	0
#define CMP_QUEUE_TIMER_THRESH	80 /* ~2usec */

#define RBDR_SIZE		RBDR_SIZE0
#define RCV_BUF_COUNT		(1ULL << (RBDR_SIZE + 13))
#define MAX_RCV_BUF_COUNT	(1ULL << (RBDR_SIZE6 + 13))
#define RBDR_THRESH		(RCV_BUF_COUNT / 2)
#define DMA_BUFFER_LEN		2048 /* In multiples of 128bytes */
#define RCV_FRAG_LEN	(SKB_DATA_ALIGN(DMA_BUFFER_LEN + NET_SKB_PAD) + \
			 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) + \
			 (NICVF_RCV_BUF_ALIGN_BYTES * 2))
#define RCV_DATA_OFFSET		NICVF_RCV_BUF_ALIGN_BYTES

#define MAX_CQES_FOR_TX		((SND_QUEUE_LEN / MIN_SQ_DESC_PER_PKT_XMIT) * \
				 MAX_CQE_PER_PKT_XMIT)
/* Calculate number of CQEs to reserve for all SQEs.
 * Its 1/256th level of CQ size.
 * '+ 1' to account for pipelining
 */
#define RQ_CQ_DROP		((256 / (CMP_QUEUE_LEN / \
				 (CMP_QUEUE_LEN - MAX_CQES_FOR_TX))) + 1)

/* Descriptor size in bytes */
#define SND_QUEUE_DESC_SIZE	16
#define CMP_QUEUE_DESC_SIZE	512

/* Buffer / descriptor alignments */
#define NICVF_RCV_BUF_ALIGN		7
#define NICVF_RCV_BUF_ALIGN_BYTES	(1ULL << NICVF_RCV_BUF_ALIGN)
#define NICVF_CQ_BASE_ALIGN_BYTES	512  /* 9 bits */
#define NICVF_SQ_BASE_ALIGN_BYTES	128  /* 7 bits */

#define NICVF_ALIGNED_ADDR(ADDR, ALIGN_BYTES)	ALIGN(ADDR, ALIGN_BYTES)
#define NICVF_ADDR_ALIGN_LEN(ADDR, BYTES)\
	(NICVF_ALIGNED_ADDR(ADDR, BYTES) - BYTES)
#define NICVF_RCV_BUF_ALIGN_LEN(X)\
	(NICVF_ALIGNED_ADDR(X, NICVF_RCV_BUF_ALIGN_BYTES) - X)

/* Queue enable/disable */
#define NICVF_SQ_EN		BIT_ULL(19)

/* Queue reset */
#define NICVF_CQ_RESET		BIT_ULL(41)
#define NICVF_SQ_RESET		BIT_ULL(17)
#define NICVF_RBDR_RESET	BIT_ULL(43)

enum CQ_RX_ERRLVL_E {
	CQ_ERRLVL_MAC,
	CQ_ERRLVL_L2,
	CQ_ERRLVL_L3,
	CQ_ERRLVL_L4,
};

enum CQ_RX_ERROP_E {
	CQ_RX_ERROP_RE_NONE = 0x0,
	CQ_RX_ERROP_RE_PARTIAL = 0x1,
	CQ_RX_ERROP_RE_JABBER = 0x2,
	CQ_RX_ERROP_RE_FCS = 0x7,
	CQ_RX_ERROP_RE_TERMINATE = 0x9,
	CQ_RX_ERROP_RE_RX_CTL = 0xb,
	CQ_RX_ERROP_PREL2_ERR = 0x1f,
	CQ_RX_ERROP_L2_FRAGMENT = 0x20,
	CQ_RX_ERROP_L2_OVERRUN = 0x21,
	CQ_RX_ERROP_L2_PFCS = 0x22,
	CQ_RX_ERROP_L2_PUNY = 0x23,
	CQ_RX_ERROP_L2_MAL = 0x24,
	CQ_RX_ERROP_L2_OVERSIZE = 0x25,
	CQ_RX_ERROP_L2_UNDERSIZE = 0x26,
	CQ_RX_ERROP_L2_LENMISM = 0x27,
	CQ_RX_ERROP_L2_PCLP = 0x28,
	CQ_RX_ERROP_IP_NOT = 0x41,
	CQ_RX_ERROP_IP_CSUM_ERR = 0x42,
	CQ_RX_ERROP_IP_MAL = 0x43,
	CQ_RX_ERROP_IP_MALD = 0x44,
	CQ_RX_ERROP_IP_HOP = 0x45,
	CQ_RX_ERROP_L3_ICRC = 0x46,
	CQ_RX_ERROP_L3_PCLP = 0x47,
	CQ_RX_ERROP_L4_MAL = 0x61,
	CQ_RX_ERROP_L4_CHK = 0x62,
	CQ_RX_ERROP_UDP_LEN = 0x63,
	CQ_RX_ERROP_L4_PORT = 0x64,
	CQ_RX_ERROP_TCP_FLAG = 0x65,
	CQ_RX_ERROP_TCP_OFFSET = 0x66,
	CQ_RX_ERROP_L4_PCLP = 0x67,
	CQ_RX_ERROP_RBDR_TRUNC = 0x70,
};

enum CQ_TX_ERROP_E {
	CQ_TX_ERROP_GOOD = 0x0,
	CQ_TX_ERROP_DESC_FAULT = 0x10,
	CQ_TX_ERROP_HDR_CONS_ERR = 0x11,
	CQ_TX_ERROP_SUBDC_ERR = 0x12,
	CQ_TX_ERROP_IMM_SIZE_OFLOW = 0x80,
	CQ_TX_ERROP_DATA_SEQUENCE_ERR = 0x81,
	CQ_TX_ERROP_MEM_SEQUENCE_ERR = 0x82,
	CQ_TX_ERROP_LOCK_VIOL = 0x83,
	CQ_TX_ERROP_DATA_FAULT = 0x84,
	CQ_TX_ERROP_TSTMP_CONFLICT = 0x85,
	CQ_TX_ERROP_TSTMP_TIMEOUT = 0x86,
	CQ_TX_ERROP_MEM_FAULT = 0x87,
	CQ_TX_ERROP_CK_OVERLAP = 0x88,
	CQ_TX_ERROP_CK_OFLOW = 0x89,
	CQ_TX_ERROP_ENUM_LAST = 0x8a,
};

struct cmp_queue_stats {
	struct tx_stats {
		u64 good;
		u64 desc_fault;
		u64 hdr_cons_err;
		u64 subdesc_err;
		u64 imm_size_oflow;
		u64 data_seq_err;
		u64 mem_seq_err;
		u64 lock_viol;
		u64 data_fault;
		u64 tstmp_conflict;
		u64 tstmp_timeout;
		u64 mem_fault;
		u64 csum_overlap;
		u64 csum_overflow;
	} tx;
} ____cacheline_aligned_in_smp;

enum RQ_SQ_STATS {
	RQ_SQ_STATS_OCTS,
	RQ_SQ_STATS_PKTS,
};

struct rx_tx_queue_stats {
	u64	bytes;
	u64	pkts;
} ____cacheline_aligned_in_smp;

struct q_desc_mem {
	dma_addr_t	dma;
	u64		size;
	u16		q_len;
	dma_addr_t	phys_base;
	void		*base;
	void		*unalign_base;
};

struct rbdr {
	bool		enable;
	u32		dma_size;
	u32		frag_len;
	u32		thresh;		/* Threshold level for interrupt */
	void		*desc;
	u32		head;
	u32		tail;
	struct q_desc_mem   dmem;
} ____cacheline_aligned_in_smp;

struct rcv_queue {
	bool		enable;
	struct	rbdr	*rbdr_start;
	struct	rbdr	*rbdr_cont;
	bool		en_tcp_reassembly;
	u8		cq_qs;  /* CQ's QS to which this RQ is assigned */
	u8		cq_idx; /* CQ index (0 to 7) in the QS */
	u8		cont_rbdr_qs;      /* Continue buffer ptrs - QS num */
	u8		cont_qs_rbdr_idx;  /* RBDR idx in the cont QS */
	u8		start_rbdr_qs;     /* First buffer ptrs - QS num */
	u8		start_qs_rbdr_idx; /* RBDR idx in the above QS */
	u8		caching;
	struct		rx_tx_queue_stats stats;
} ____cacheline_aligned_in_smp;

struct cmp_queue {
	bool		enable;
	u16		thresh;
	spinlock_t	lock;  /* lock to serialize processing CQEs */
	void		*desc;
	struct q_desc_mem   dmem;
	struct cmp_queue_stats	stats;
	int		irq;
} ____cacheline_aligned_in_smp;

struct snd_queue {
	bool		enable;
	u8		cq_qs;  /* CQ's QS to which this SQ is pointing */
	u8		cq_idx; /* CQ index (0 to 7) in the above QS */
	u16		thresh;
	atomic_t	free_cnt;
	u32		head;
	u32		tail;
	u64		*skbuff;
	void		*desc;

#define	TSO_HEADER_SIZE	128
	/* For TSO segment's header */
	char		*tso_hdrs;
	dma_addr_t	tso_hdrs_phys;

	cpumask_t	affinity_mask;
	struct q_desc_mem   dmem;
	struct rx_tx_queue_stats stats;
} ____cacheline_aligned_in_smp;

struct queue_set {
	bool		enable;
	bool		be_en;
	u8		vnic_id;
	u8		rq_cnt;
	u8		cq_cnt;
	u64		cq_len;
	u8		sq_cnt;
	u64		sq_len;
	u8		rbdr_cnt;
	u64		rbdr_len;
	struct	rcv_queue	rq[MAX_RCV_QUEUES_PER_QS];
	struct	cmp_queue	cq[MAX_CMP_QUEUES_PER_QS];
	struct	snd_queue	sq[MAX_SND_QUEUES_PER_QS];
	struct	rbdr		rbdr[MAX_RCV_BUF_DESC_RINGS_PER_QS];
} ____cacheline_aligned_in_smp;

#define GET_RBDR_DESC(RING, idx)\
		(&(((struct rbdr_entry_t *)((RING)->desc))[idx]))
#define GET_SQ_DESC(RING, idx)\
		(&(((struct sq_hdr_subdesc *)((RING)->desc))[idx]))
#define GET_CQ_DESC(RING, idx)\
		(&(((union cq_desc_t *)((RING)->desc))[idx]))

/* CQ status bits */
#define	CQ_WR_FULL	BIT(26)
#define	CQ_WR_DISABLE	BIT(25)
#define	CQ_WR_FAULT	BIT(24)
#define	CQ_CQE_COUNT	(0xFFFF << 0)

#define	CQ_ERR_MASK	(CQ_WR_FULL | CQ_WR_DISABLE | CQ_WR_FAULT)

void nicvf_config_vlan_stripping(struct nicvf *nic,
				 netdev_features_t features);
int nicvf_set_qset_resources(struct nicvf *nic);
int nicvf_config_data_transfer(struct nicvf *nic, bool enable);
void nicvf_qset_config(struct nicvf *nic, bool enable);
void nicvf_cmp_queue_config(struct nicvf *nic, struct queue_set *qs,
			    int qidx, bool enable);

void nicvf_sq_enable(struct nicvf *nic, struct snd_queue *sq, int qidx);
void nicvf_sq_disable(struct nicvf *nic, int qidx);
void nicvf_put_sq_desc(struct snd_queue *sq, int desc_cnt);
void nicvf_sq_free_used_descs(struct net_device *netdev,
			      struct snd_queue *sq, int qidx);
int nicvf_sq_append_skb(struct nicvf *nic, struct sk_buff *skb);

struct sk_buff *nicvf_get_rcv_skb(struct nicvf *nic, struct cqe_rx_t *cqe_rx);
void nicvf_rbdr_task(unsigned long data);
void nicvf_rbdr_work(struct work_struct *work);

void nicvf_enable_intr(struct nicvf *nic, int int_type, int q_idx);
void nicvf_disable_intr(struct nicvf *nic, int int_type, int q_idx);
void nicvf_clear_intr(struct nicvf *nic, int int_type, int q_idx);
int nicvf_is_intr_enabled(struct nicvf *nic, int int_type, int q_idx);

/* Register access APIs */
void nicvf_reg_write(struct nicvf *nic, u64 offset, u64 val);
u64  nicvf_reg_read(struct nicvf *nic, u64 offset);
void nicvf_qset_reg_write(struct nicvf *nic, u64 offset, u64 val);
u64 nicvf_qset_reg_read(struct nicvf *nic, u64 offset);
void nicvf_queue_reg_write(struct nicvf *nic, u64 offset,
			   u64 qidx, u64 val);
u64  nicvf_queue_reg_read(struct nicvf *nic,
			  u64 offset, u64 qidx);

/* Stats */
void nicvf_update_rq_stats(struct nicvf *nic, int rq_idx);
void nicvf_update_sq_stats(struct nicvf *nic, int sq_idx);
int nicvf_check_cqe_rx_errs(struct nicvf *nic,
			    struct cmp_queue *cq, struct cqe_rx_t *cqe_rx);
int nicvf_check_cqe_tx_errs(struct nicvf *nic,
			    struct cmp_queue *cq, struct cqe_send_t *cqe_tx);
#endif /* NICVF_QUEUES_H */
