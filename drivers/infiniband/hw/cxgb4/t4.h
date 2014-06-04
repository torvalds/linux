/*
 * Copyright (c) 2009-2010 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __T4_H__
#define __T4_H__

#include "t4_hw.h"
#include "t4_regs.h"
#include "t4_msg.h"
#include "t4fw_ri_api.h"

#define T4_MAX_NUM_QP 65536
#define T4_MAX_NUM_CQ 65536
#define T4_MAX_NUM_PD 65536
#define T4_EQ_STATUS_ENTRIES (L1_CACHE_BYTES > 64 ? 2 : 1)
#define T4_MAX_EQ_SIZE (65520 - T4_EQ_STATUS_ENTRIES)
#define T4_MAX_IQ_SIZE (65520 - 1)
#define T4_MAX_RQ_SIZE (8192 - T4_EQ_STATUS_ENTRIES)
#define T4_MAX_SQ_SIZE (T4_MAX_EQ_SIZE - 1)
#define T4_MAX_QP_DEPTH (T4_MAX_RQ_SIZE - 1)
#define T4_MAX_CQ_DEPTH (T4_MAX_IQ_SIZE - 1)
#define T4_MAX_NUM_STAG (1<<15)
#define T4_MAX_MR_SIZE (~0ULL)
#define T4_PAGESIZE_MASK 0xffff000  /* 4KB-128MB */
#define T4_STAG_UNSET 0xffffffff
#define T4_FW_MAJ 0
#define T4_EQ_STATUS_ENTRIES (L1_CACHE_BYTES > 64 ? 2 : 1)
#define A_PCIE_MA_SYNC 0x30b4

struct t4_status_page {
	__be32 rsvd1;	/* flit 0 - hw owns */
	__be16 rsvd2;
	__be16 qid;
	__be16 cidx;
	__be16 pidx;
	u8 qp_err;	/* flit 1 - sw owns */
	u8 db_off;
	u8 pad;
	u16 host_wq_pidx;
	u16 host_cidx;
	u16 host_pidx;
};

#define T4_EQ_ENTRY_SIZE 64

#define T4_SQ_NUM_SLOTS 5
#define T4_SQ_NUM_BYTES (T4_EQ_ENTRY_SIZE * T4_SQ_NUM_SLOTS)
#define T4_MAX_SEND_SGE ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_send_wr) - \
			sizeof(struct fw_ri_isgl)) / sizeof(struct fw_ri_sge))
#define T4_MAX_SEND_INLINE ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_send_wr) - \
			sizeof(struct fw_ri_immd)))
#define T4_MAX_WRITE_INLINE ((T4_SQ_NUM_BYTES - \
			sizeof(struct fw_ri_rdma_write_wr) - \
			sizeof(struct fw_ri_immd)))
#define T4_MAX_WRITE_SGE ((T4_SQ_NUM_BYTES - \
			sizeof(struct fw_ri_rdma_write_wr) - \
			sizeof(struct fw_ri_isgl)) / sizeof(struct fw_ri_sge))
#define T4_MAX_FR_IMMD ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_fr_nsmr_wr) - \
			sizeof(struct fw_ri_immd)) & ~31UL)
#define T4_MAX_FR_DEPTH (1024 / sizeof(u64))

#define T4_RQ_NUM_SLOTS 2
#define T4_RQ_NUM_BYTES (T4_EQ_ENTRY_SIZE * T4_RQ_NUM_SLOTS)
#define T4_MAX_RECV_SGE 4

union t4_wr {
	struct fw_ri_res_wr res;
	struct fw_ri_wr ri;
	struct fw_ri_rdma_write_wr write;
	struct fw_ri_send_wr send;
	struct fw_ri_rdma_read_wr read;
	struct fw_ri_bind_mw_wr bind;
	struct fw_ri_fr_nsmr_wr fr;
	struct fw_ri_inv_lstag_wr inv;
	struct t4_status_page status;
	__be64 flits[T4_EQ_ENTRY_SIZE / sizeof(__be64) * T4_SQ_NUM_SLOTS];
};

union t4_recv_wr {
	struct fw_ri_recv_wr recv;
	struct t4_status_page status;
	__be64 flits[T4_EQ_ENTRY_SIZE / sizeof(__be64) * T4_RQ_NUM_SLOTS];
};

static inline void init_wr_hdr(union t4_wr *wqe, u16 wrid,
			       enum fw_wr_opcodes opcode, u8 flags, u8 len16)
{
	wqe->send.opcode = (u8)opcode;
	wqe->send.flags = flags;
	wqe->send.wrid = wrid;
	wqe->send.r1[0] = 0;
	wqe->send.r1[1] = 0;
	wqe->send.r1[2] = 0;
	wqe->send.len16 = len16;
}

/* CQE/AE status codes */
#define T4_ERR_SUCCESS                     0x0
#define T4_ERR_STAG                        0x1	/* STAG invalid: either the */
						/* STAG is offlimt, being 0, */
						/* or STAG_key mismatch */
#define T4_ERR_PDID                        0x2	/* PDID mismatch */
#define T4_ERR_QPID                        0x3	/* QPID mismatch */
#define T4_ERR_ACCESS                      0x4	/* Invalid access right */
#define T4_ERR_WRAP                        0x5	/* Wrap error */
#define T4_ERR_BOUND                       0x6	/* base and bounds voilation */
#define T4_ERR_INVALIDATE_SHARED_MR        0x7	/* attempt to invalidate a  */
						/* shared memory region */
#define T4_ERR_INVALIDATE_MR_WITH_MW_BOUND 0x8	/* attempt to invalidate a  */
						/* shared memory region */
#define T4_ERR_ECC                         0x9	/* ECC error detected */
#define T4_ERR_ECC_PSTAG                   0xA	/* ECC error detected when  */
						/* reading PSTAG for a MW  */
						/* Invalidate */
#define T4_ERR_PBL_ADDR_BOUND              0xB	/* pbl addr out of bounds:  */
						/* software error */
#define T4_ERR_SWFLUSH			   0xC	/* SW FLUSHED */
#define T4_ERR_CRC                         0x10 /* CRC error */
#define T4_ERR_MARKER                      0x11 /* Marker error */
#define T4_ERR_PDU_LEN_ERR                 0x12 /* invalid PDU length */
#define T4_ERR_OUT_OF_RQE                  0x13 /* out of RQE */
#define T4_ERR_DDP_VERSION                 0x14 /* wrong DDP version */
#define T4_ERR_RDMA_VERSION                0x15 /* wrong RDMA version */
#define T4_ERR_OPCODE                      0x16 /* invalid rdma opcode */
#define T4_ERR_DDP_QUEUE_NUM               0x17 /* invalid ddp queue number */
#define T4_ERR_MSN                         0x18 /* MSN error */
#define T4_ERR_TBIT                        0x19 /* tag bit not set correctly */
#define T4_ERR_MO                          0x1A /* MO not 0 for TERMINATE  */
						/* or READ_REQ */
#define T4_ERR_MSN_GAP                     0x1B
#define T4_ERR_MSN_RANGE                   0x1C
#define T4_ERR_IRD_OVERFLOW                0x1D
#define T4_ERR_RQE_ADDR_BOUND              0x1E /* RQE addr out of bounds:  */
						/* software error */
#define T4_ERR_INTERNAL_ERR                0x1F /* internal error (opcode  */
						/* mismatch) */
/*
 * CQE defs
 */
struct t4_cqe {
	__be32 header;
	__be32 len;
	union {
		struct {
			__be32 stag;
			__be32 msn;
		} rcqe;
		struct {
			u32 nada1;
			u16 nada2;
			u16 cidx;
		} scqe;
		struct {
			__be32 wrid_hi;
			__be32 wrid_low;
		} gen;
	} u;
	__be64 reserved;
	__be64 bits_type_ts;
};

/* macros for flit 0 of the cqe */

#define S_CQE_QPID        12
#define M_CQE_QPID        0xFFFFF
#define G_CQE_QPID(x)     ((((x) >> S_CQE_QPID)) & M_CQE_QPID)
#define V_CQE_QPID(x)	  ((x)<<S_CQE_QPID)

#define S_CQE_SWCQE       11
#define M_CQE_SWCQE       0x1
#define G_CQE_SWCQE(x)    ((((x) >> S_CQE_SWCQE)) & M_CQE_SWCQE)
#define V_CQE_SWCQE(x)	  ((x)<<S_CQE_SWCQE)

#define S_CQE_STATUS      5
#define M_CQE_STATUS      0x1F
#define G_CQE_STATUS(x)   ((((x) >> S_CQE_STATUS)) & M_CQE_STATUS)
#define V_CQE_STATUS(x)   ((x)<<S_CQE_STATUS)

#define S_CQE_TYPE        4
#define M_CQE_TYPE        0x1
#define G_CQE_TYPE(x)     ((((x) >> S_CQE_TYPE)) & M_CQE_TYPE)
#define V_CQE_TYPE(x)     ((x)<<S_CQE_TYPE)

#define S_CQE_OPCODE      0
#define M_CQE_OPCODE      0xF
#define G_CQE_OPCODE(x)   ((((x) >> S_CQE_OPCODE)) & M_CQE_OPCODE)
#define V_CQE_OPCODE(x)   ((x)<<S_CQE_OPCODE)

#define SW_CQE(x)         (G_CQE_SWCQE(be32_to_cpu((x)->header)))
#define CQE_QPID(x)       (G_CQE_QPID(be32_to_cpu((x)->header)))
#define CQE_TYPE(x)       (G_CQE_TYPE(be32_to_cpu((x)->header)))
#define SQ_TYPE(x)	  (CQE_TYPE((x)))
#define RQ_TYPE(x)	  (!CQE_TYPE((x)))
#define CQE_STATUS(x)     (G_CQE_STATUS(be32_to_cpu((x)->header)))
#define CQE_OPCODE(x)     (G_CQE_OPCODE(be32_to_cpu((x)->header)))

#define CQE_SEND_OPCODE(x)( \
	(G_CQE_OPCODE(be32_to_cpu((x)->header)) == FW_RI_SEND) || \
	(G_CQE_OPCODE(be32_to_cpu((x)->header)) == FW_RI_SEND_WITH_SE) || \
	(G_CQE_OPCODE(be32_to_cpu((x)->header)) == FW_RI_SEND_WITH_INV) || \
	(G_CQE_OPCODE(be32_to_cpu((x)->header)) == FW_RI_SEND_WITH_SE_INV))

#define CQE_LEN(x)        (be32_to_cpu((x)->len))

/* used for RQ completion processing */
#define CQE_WRID_STAG(x)  (be32_to_cpu((x)->u.rcqe.stag))
#define CQE_WRID_MSN(x)   (be32_to_cpu((x)->u.rcqe.msn))

/* used for SQ completion processing */
#define CQE_WRID_SQ_IDX(x)	((x)->u.scqe.cidx)

/* generic accessor macros */
#define CQE_WRID_HI(x)		((x)->u.gen.wrid_hi)
#define CQE_WRID_LOW(x)		((x)->u.gen.wrid_low)

/* macros for flit 3 of the cqe */
#define S_CQE_GENBIT	63
#define M_CQE_GENBIT	0x1
#define G_CQE_GENBIT(x)	(((x) >> S_CQE_GENBIT) & M_CQE_GENBIT)
#define V_CQE_GENBIT(x) ((x)<<S_CQE_GENBIT)

#define S_CQE_OVFBIT	62
#define M_CQE_OVFBIT	0x1
#define G_CQE_OVFBIT(x)	((((x) >> S_CQE_OVFBIT)) & M_CQE_OVFBIT)

#define S_CQE_IQTYPE	60
#define M_CQE_IQTYPE	0x3
#define G_CQE_IQTYPE(x)	((((x) >> S_CQE_IQTYPE)) & M_CQE_IQTYPE)

#define M_CQE_TS	0x0fffffffffffffffULL
#define G_CQE_TS(x)	((x) & M_CQE_TS)

#define CQE_OVFBIT(x)	((unsigned)G_CQE_OVFBIT(be64_to_cpu((x)->bits_type_ts)))
#define CQE_GENBIT(x)	((unsigned)G_CQE_GENBIT(be64_to_cpu((x)->bits_type_ts)))
#define CQE_TS(x)	(G_CQE_TS(be64_to_cpu((x)->bits_type_ts)))

struct t4_swsqe {
	u64			wr_id;
	struct t4_cqe		cqe;
	int			read_len;
	int			opcode;
	int			complete;
	int			signaled;
	u16			idx;
	int                     flushed;
};

static inline pgprot_t t4_pgprot_wc(pgprot_t prot)
{
#if defined(__i386__) || defined(__x86_64__) || defined(CONFIG_PPC64)
	return pgprot_writecombine(prot);
#else
	return pgprot_noncached(prot);
#endif
}

enum {
	T4_SQ_ONCHIP = (1<<0),
};

struct t4_sq {
	union t4_wr *queue;
	dma_addr_t dma_addr;
	DEFINE_DMA_UNMAP_ADDR(mapping);
	unsigned long phys_addr;
	struct t4_swsqe *sw_sq;
	struct t4_swsqe *oldest_read;
	u64 udb;
	size_t memsize;
	u32 qid;
	u16 in_use;
	u16 size;
	u16 cidx;
	u16 pidx;
	u16 wq_pidx;
	u16 wq_pidx_inc;
	u16 flags;
	short flush_cidx;
};

struct t4_swrqe {
	u64 wr_id;
};

struct t4_rq {
	union  t4_recv_wr *queue;
	dma_addr_t dma_addr;
	DEFINE_DMA_UNMAP_ADDR(mapping);
	struct t4_swrqe *sw_rq;
	u64 udb;
	size_t memsize;
	u32 qid;
	u32 msn;
	u32 rqt_hwaddr;
	u16 rqt_size;
	u16 in_use;
	u16 size;
	u16 cidx;
	u16 pidx;
	u16 wq_pidx;
	u16 wq_pidx_inc;
};

struct t4_wq {
	struct t4_sq sq;
	struct t4_rq rq;
	void __iomem *db;
	void __iomem *gts;
	struct c4iw_rdev *rdev;
	int flushed;
};

static inline int t4_rqes_posted(struct t4_wq *wq)
{
	return wq->rq.in_use;
}

static inline int t4_rq_empty(struct t4_wq *wq)
{
	return wq->rq.in_use == 0;
}

static inline int t4_rq_full(struct t4_wq *wq)
{
	return wq->rq.in_use == (wq->rq.size - 1);
}

static inline u32 t4_rq_avail(struct t4_wq *wq)
{
	return wq->rq.size - 1 - wq->rq.in_use;
}

static inline void t4_rq_produce(struct t4_wq *wq, u8 len16)
{
	wq->rq.in_use++;
	if (++wq->rq.pidx == wq->rq.size)
		wq->rq.pidx = 0;
	wq->rq.wq_pidx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	if (wq->rq.wq_pidx >= wq->rq.size * T4_RQ_NUM_SLOTS)
		wq->rq.wq_pidx %= wq->rq.size * T4_RQ_NUM_SLOTS;
}

static inline void t4_rq_consume(struct t4_wq *wq)
{
	wq->rq.in_use--;
	wq->rq.msn++;
	if (++wq->rq.cidx == wq->rq.size)
		wq->rq.cidx = 0;
}

static inline u16 t4_rq_host_wq_pidx(struct t4_wq *wq)
{
	return wq->rq.queue[wq->rq.size].status.host_wq_pidx;
}

static inline u16 t4_rq_wq_size(struct t4_wq *wq)
{
		return wq->rq.size * T4_RQ_NUM_SLOTS;
}

static inline int t4_sq_onchip(struct t4_sq *sq)
{
	return sq->flags & T4_SQ_ONCHIP;
}

static inline int t4_sq_empty(struct t4_wq *wq)
{
	return wq->sq.in_use == 0;
}

static inline int t4_sq_full(struct t4_wq *wq)
{
	return wq->sq.in_use == (wq->sq.size - 1);
}

static inline u32 t4_sq_avail(struct t4_wq *wq)
{
	return wq->sq.size - 1 - wq->sq.in_use;
}

static inline void t4_sq_produce(struct t4_wq *wq, u8 len16)
{
	wq->sq.in_use++;
	if (++wq->sq.pidx == wq->sq.size)
		wq->sq.pidx = 0;
	wq->sq.wq_pidx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	if (wq->sq.wq_pidx >= wq->sq.size * T4_SQ_NUM_SLOTS)
		wq->sq.wq_pidx %= wq->sq.size * T4_SQ_NUM_SLOTS;
}

static inline void t4_sq_consume(struct t4_wq *wq)
{
	BUG_ON(wq->sq.in_use < 1);
	if (wq->sq.cidx == wq->sq.flush_cidx)
		wq->sq.flush_cidx = -1;
	wq->sq.in_use--;
	if (++wq->sq.cidx == wq->sq.size)
		wq->sq.cidx = 0;
}

static inline u16 t4_sq_host_wq_pidx(struct t4_wq *wq)
{
	return wq->sq.queue[wq->sq.size].status.host_wq_pidx;
}

static inline u16 t4_sq_wq_size(struct t4_wq *wq)
{
		return wq->sq.size * T4_SQ_NUM_SLOTS;
}

static inline void t4_ring_sq_db(struct t4_wq *wq, u16 inc)
{
	wmb();
	writel(QID(wq->sq.qid) | PIDX(inc), wq->db);
}

static inline void t4_ring_rq_db(struct t4_wq *wq, u16 inc)
{
	wmb();
	writel(QID(wq->rq.qid) | PIDX(inc), wq->db);
}

static inline int t4_wq_in_error(struct t4_wq *wq)
{
	return wq->rq.queue[wq->rq.size].status.qp_err;
}

static inline void t4_set_wq_in_error(struct t4_wq *wq)
{
	wq->rq.queue[wq->rq.size].status.qp_err = 1;
}

static inline void t4_disable_wq_db(struct t4_wq *wq)
{
	wq->rq.queue[wq->rq.size].status.db_off = 1;
}

static inline void t4_enable_wq_db(struct t4_wq *wq)
{
	wq->rq.queue[wq->rq.size].status.db_off = 0;
}

static inline int t4_wq_db_enabled(struct t4_wq *wq)
{
	return !wq->rq.queue[wq->rq.size].status.db_off;
}

struct t4_cq {
	struct t4_cqe *queue;
	dma_addr_t dma_addr;
	DEFINE_DMA_UNMAP_ADDR(mapping);
	struct t4_cqe *sw_queue;
	void __iomem *gts;
	struct c4iw_rdev *rdev;
	u64 ugts;
	size_t memsize;
	__be64 bits_type_ts;
	u32 cqid;
	u16 size; /* including status page */
	u16 cidx;
	u16 sw_pidx;
	u16 sw_cidx;
	u16 sw_in_use;
	u16 cidx_inc;
	u8 gen;
	u8 error;
};

static inline int t4_arm_cq(struct t4_cq *cq, int se)
{
	u32 val;

	while (cq->cidx_inc > CIDXINC_MASK) {
		val = SEINTARM(0) | CIDXINC(CIDXINC_MASK) | TIMERREG(7) |
		      INGRESSQID(cq->cqid);
		writel(val, cq->gts);
		cq->cidx_inc -= CIDXINC_MASK;
	}
	val = SEINTARM(se) | CIDXINC(cq->cidx_inc) | TIMERREG(6) |
	      INGRESSQID(cq->cqid);
	writel(val, cq->gts);
	cq->cidx_inc = 0;
	return 0;
}

static inline void t4_swcq_produce(struct t4_cq *cq)
{
	cq->sw_in_use++;
	if (cq->sw_in_use == cq->size) {
		PDBG("%s cxgb4 sw cq overflow cqid %u\n", __func__, cq->cqid);
		cq->error = 1;
		BUG_ON(1);
	}
	if (++cq->sw_pidx == cq->size)
		cq->sw_pidx = 0;
}

static inline void t4_swcq_consume(struct t4_cq *cq)
{
	BUG_ON(cq->sw_in_use < 1);
	cq->sw_in_use--;
	if (++cq->sw_cidx == cq->size)
		cq->sw_cidx = 0;
}

static inline void t4_hwcq_consume(struct t4_cq *cq)
{
	cq->bits_type_ts = cq->queue[cq->cidx].bits_type_ts;
	if (++cq->cidx_inc == (cq->size >> 4) || cq->cidx_inc == CIDXINC_MASK) {
		u32 val;

		val = SEINTARM(0) | CIDXINC(cq->cidx_inc) | TIMERREG(7) |
		      INGRESSQID(cq->cqid);
		writel(val, cq->gts);
		cq->cidx_inc = 0;
	}
	if (++cq->cidx == cq->size) {
		cq->cidx = 0;
		cq->gen ^= 1;
	}
}

static inline int t4_valid_cqe(struct t4_cq *cq, struct t4_cqe *cqe)
{
	return (CQE_GENBIT(cqe) == cq->gen);
}

static inline int t4_next_hw_cqe(struct t4_cq *cq, struct t4_cqe **cqe)
{
	int ret;
	u16 prev_cidx;

	if (cq->cidx == 0)
		prev_cidx = cq->size - 1;
	else
		prev_cidx = cq->cidx - 1;

	if (cq->queue[prev_cidx].bits_type_ts != cq->bits_type_ts) {
		ret = -EOVERFLOW;
		cq->error = 1;
		printk(KERN_ERR MOD "cq overflow cqid %u\n", cq->cqid);
		BUG_ON(1);
	} else if (t4_valid_cqe(cq, &cq->queue[cq->cidx])) {
		*cqe = &cq->queue[cq->cidx];
		ret = 0;
	} else
		ret = -ENODATA;
	return ret;
}

static inline struct t4_cqe *t4_next_sw_cqe(struct t4_cq *cq)
{
	if (cq->sw_in_use == cq->size) {
		PDBG("%s cxgb4 sw cq overflow cqid %u\n", __func__, cq->cqid);
		cq->error = 1;
		BUG_ON(1);
		return NULL;
	}
	if (cq->sw_in_use)
		return &cq->sw_queue[cq->sw_cidx];
	return NULL;
}

static inline int t4_next_cqe(struct t4_cq *cq, struct t4_cqe **cqe)
{
	int ret = 0;

	if (cq->error)
		ret = -ENODATA;
	else if (cq->sw_in_use)
		*cqe = &cq->sw_queue[cq->sw_cidx];
	else
		ret = t4_next_hw_cqe(cq, cqe);
	return ret;
}

static inline int t4_cq_in_error(struct t4_cq *cq)
{
	return ((struct t4_status_page *)&cq->queue[cq->size])->qp_err;
}

static inline void t4_set_cq_in_error(struct t4_cq *cq)
{
	((struct t4_status_page *)&cq->queue[cq->size])->qp_err = 1;
}
#endif

struct t4_dev_status_page {
	u8 db_off;
};
