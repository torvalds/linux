/*
 * Copyright (c) 2006-2016 Chelsio, Inc. All rights reserved.
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

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <syslog.h>
#include <infiniband/types.h>
#include <infiniband/udma_barrier.h>
#include <infiniband/endian.h>

/*
 * Try and minimize the changes from the kernel code that is pull in
 * here for kernel bypass ops.
 */
#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t
#define DECLARE_PCI_UNMAP_ADDR(a)
#define __iomem
#define BUG_ON(c) assert(!(c))
#define ROUND_UP(x, n) (((x) + (n) - 1u) & ~((n) - 1u))
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

/* FIXME: Move me to a generic PCI mmio accessor */
#define cpu_to_pci32(val) htole32(val)

#define writel(v, a) do { *((volatile u32 *)(a)) = cpu_to_pci32(v); } while (0)

#include "t4_regs.h"
#include "t4_chip_type.h"
#include "t4fw_api.h"
#include "t4fw_ri_api.h"

#ifdef DEBUG
#define DBGLOG(s)
#define PDBG(fmt, args...) do {syslog(LOG_DEBUG, fmt, ##args); } while (0)
#else
#define DBGLOG(s)
#define PDBG(fmt, args...) do {} while (0)
#endif

#define A_PCIE_MA_SYNC 0x30b4

#define T4_MAX_READ_DEPTH 16
#define T4_QID_BASE 1024
#define T4_MAX_QIDS 256
#define T4_MAX_NUM_PD 65536
#define T4_EQ_STATUS_ENTRIES (L1_CACHE_BYTES > 64 ? 2 : 1)
#define T4_MAX_EQ_SIZE (65520 - T4_EQ_STATUS_ENTRIES)
#define T4_MAX_IQ_SIZE (65520 - 1)
#define T4_MAX_RQ_SIZE (8192 - T4_EQ_STATUS_ENTRIES)
#define T4_MAX_SQ_SIZE (T4_MAX_EQ_SIZE - 1)
#define T4_MAX_QP_DEPTH (T4_MAX_RQ_SIZE - 1)
#define T4_MAX_CQ_DEPTH (T4_MAX_IQ_SIZE - 1)
#define T4_MAX_NUM_STAG (1<<15)
#define T4_MAX_MR_SIZE (~0ULL - 1)
#define T4_PAGESIZE_MASK 0xffffffff000  /* 4KB-8TB */
#define T4_STAG_UNSET 0xffffffff
#define T4_FW_MAJ 0

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
#define T4_MAX_SEND_SGE ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_send_wr) - sizeof(struct fw_ri_isgl)) / sizeof (struct fw_ri_sge))
#define T4_MAX_SEND_INLINE ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_send_wr) - sizeof(struct fw_ri_immd)))
#define T4_MAX_WRITE_INLINE ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_rdma_write_wr) - sizeof(struct fw_ri_immd)))
#define T4_MAX_WRITE_SGE ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_rdma_write_wr) - sizeof(struct fw_ri_isgl)) / sizeof (struct fw_ri_sge))
#define T4_MAX_FR_IMMD ((T4_SQ_NUM_BYTES - sizeof(struct fw_ri_fr_nsmr_wr) - sizeof(struct fw_ri_immd)))
#define T4_MAX_FR_DEPTH 255

#define T4_RQ_NUM_SLOTS 2
#define T4_RQ_NUM_BYTES (T4_EQ_ENTRY_SIZE * T4_RQ_NUM_SLOTS)
#define T4_MAX_RECV_SGE 4

union t4_wr {
	struct fw_ri_res_wr res;
	struct fw_ri_wr init;
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

#define SW_CQE(x)         (G_CQE_SWCQE(be32toh((x)->header)))
#define CQE_QPID(x)       (G_CQE_QPID(be32toh((x)->header)))
#define CQE_TYPE(x)       (G_CQE_TYPE(be32toh((x)->header)))
#define SQ_TYPE(x)	  (CQE_TYPE((x)))
#define RQ_TYPE(x)	  (!CQE_TYPE((x)))
#define CQE_STATUS(x)     (G_CQE_STATUS(be32toh((x)->header)))
#define CQE_OPCODE(x)     (G_CQE_OPCODE(be32toh((x)->header)))

#define CQE_SEND_OPCODE(x)( \
	(G_CQE_OPCODE(be32toh((x)->header)) == FW_RI_SEND) || \
	(G_CQE_OPCODE(be32toh((x)->header)) == FW_RI_SEND_WITH_SE) || \
	(G_CQE_OPCODE(be32toh((x)->header)) == FW_RI_SEND_WITH_INV) || \
	(G_CQE_OPCODE(be32toh((x)->header)) == FW_RI_SEND_WITH_SE_INV))

#define CQE_LEN(x)        (be32toh((x)->len))

/* used for RQ completion processing */
#define CQE_WRID_STAG(x)  (be32toh((x)->u.rcqe.stag))
#define CQE_WRID_MSN(x)   (be32toh((x)->u.rcqe.msn))

/* used for SQ completion processing */
#define CQE_WRID_SQ_IDX(x)	(x)->u.scqe.cidx

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

#define CQE_OVFBIT(x)	((unsigned)G_CQE_OVFBIT(be64toh((x)->bits_type_ts)))
#define CQE_GENBIT(x)	((unsigned)G_CQE_GENBIT(be64toh((x)->bits_type_ts)))
#define CQE_TS(x)	(G_CQE_TS(be64toh((x)->bits_type_ts)))

struct t4_swsqe {
	u64			wr_id;
	struct t4_cqe		cqe;
	__be32			read_len;
	int			opcode;
	int			complete;
	int			signaled;
	u16			idx;
	int			flushed;
};

enum {
	T4_SQ_ONCHIP = (1<<0),
};

struct t4_sq {
	/* queue is either host memory or WC MMIO memory if
	 * t4_sq_onchip(). */
	union t4_wr *queue;
	struct t4_swsqe *sw_sq;
	struct t4_swsqe *oldest_read;
	/* udb is either UC or WC MMIO memory depending on device version. */
	volatile u32 *udb;
	size_t memsize;
	u32 qid;
	u32 bar2_qid;
	void *ma_sync;
	u16 in_use;
	u16 size;
	u16 cidx;
	u16 pidx;
	u16 wq_pidx;
	u16 flags;
	short flush_cidx;
	int wc_reg_available;
};

struct t4_swrqe {
	u64 wr_id;
};

struct t4_rq {
	union  t4_recv_wr *queue;
	struct t4_swrqe *sw_rq;
	volatile u32 *udb;
	size_t memsize;
	u32 qid;
	u32 bar2_qid;
	u32 msn;
	u32 rqt_hwaddr;
	u16 rqt_size;
	u16 in_use;
	u16 size;
	u16 cidx;
	u16 pidx;
	u16 wq_pidx;
	int wc_reg_available;
};

struct t4_wq {
	struct t4_sq sq;
	struct t4_rq rq;
	struct c4iw_rdev *rdev;
	u32 qid_mask;
	int error;
	int flushed;
	u8 *db_offp;
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
	if (!wq->error)
		wq->rq.queue[wq->rq.size].status.host_pidx = wq->rq.pidx;
}

static inline void t4_rq_consume(struct t4_wq *wq)
{
	wq->rq.in_use--;
	wq->rq.msn++;
	if (++wq->rq.cidx == wq->rq.size)
		wq->rq.cidx = 0;
	assert((wq->rq.cidx != wq->rq.pidx) || wq->rq.in_use == 0);
	if (!wq->error)
		wq->rq.queue[wq->rq.size].status.host_cidx = wq->rq.cidx;
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

static inline int t4_sq_onchip(struct t4_wq *wq)
{
	return wq->sq.flags & T4_SQ_ONCHIP;
}

static inline void t4_sq_produce(struct t4_wq *wq, u8 len16)
{
	wq->sq.in_use++;
	if (++wq->sq.pidx == wq->sq.size)
		wq->sq.pidx = 0;
	wq->sq.wq_pidx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	if (wq->sq.wq_pidx >= wq->sq.size * T4_SQ_NUM_SLOTS)
		wq->sq.wq_pidx %= wq->sq.size * T4_SQ_NUM_SLOTS;
	if (!wq->error) {
		/* This write is only for debugging, the value does not matter
		 * for DMA */
		wq->sq.queue[wq->sq.size].status.host_pidx = (wq->sq.pidx);
	}
}

static inline void t4_sq_consume(struct t4_wq *wq)
{
	assert(wq->sq.in_use >= 1);
	if (wq->sq.cidx == wq->sq.flush_cidx)
                wq->sq.flush_cidx = -1;
	wq->sq.in_use--;
	if (++wq->sq.cidx == wq->sq.size)
		wq->sq.cidx = 0;
	assert((wq->sq.cidx != wq->sq.pidx) || wq->sq.in_use == 0);
	if (!wq->error){
		/* This write is only for debugging, the value does not matter
		 * for DMA */
		wq->sq.queue[wq->sq.size].status.host_cidx = wq->sq.cidx;
	}
}

/* Copies to WC MMIO memory */
static void copy_wqe_to_udb(volatile u32 *udb_offset, void *wqe)
{
	u64 *src, *dst;
	int len16 = 4;

	src = (u64 *)wqe;
	dst = (u64 *)udb_offset;

	while (len16) {
		*dst++ = *src++;
		*dst++ = *src++;
		len16--;
	}
}

extern int ma_wr;
extern int t5_en_wc;

static inline void t4_ring_sq_db(struct t4_wq *wq, u16 inc, u8 t4, u8 len16,
				 union t4_wr *wqe)
{
	if (!t4) {
		mmio_wc_start();
		if (t5_en_wc && inc == 1 && wq->sq.wc_reg_available) {
			PDBG("%s: WC wq->sq.pidx = %d; len16=%d\n",
			     __func__, wq->sq.pidx, len16);
			copy_wqe_to_udb(wq->sq.udb + 14, wqe);
		} else {
			PDBG("%s: DB wq->sq.pidx = %d; len16=%d\n",
			     __func__, wq->sq.pidx, len16);
			writel(QID_V(wq->sq.bar2_qid) | PIDX_T5_V(inc),
			       wq->sq.udb);
		}
		/* udb is WC for > t4 devices */
		mmio_flush_writes();
		return;
	}

	udma_to_device_barrier();
	if (ma_wr) {
		if (t4_sq_onchip(wq)) {
			int i;

			mmio_wc_start();
			for (i = 0; i < 16; i++)
				*(volatile u32 *)&wq->sq.queue[wq->sq.size].flits[2+i] = i;
			mmio_flush_writes();
		}
	} else {
		if (t4_sq_onchip(wq)) {
			int i;

			mmio_wc_start();
			for (i = 0; i < 16; i++)
				/* FIXME: What is this supposed to be doing?
				 * Writing to the same address multiple times
				 * with WC memory is not guarenteed to
				 * generate any more than one TLP. Why isn't
				 * writing to WC memory marked volatile? */
				*(u32 *)&wq->sq.queue[wq->sq.size].flits[2] = i;
			mmio_flush_writes();
		}
	}
	/* udb is UC for t4 devices */
	writel(QID_V(wq->sq.qid & wq->qid_mask) | PIDX_V(inc), wq->sq.udb);
}

static inline void t4_ring_rq_db(struct t4_wq *wq, u16 inc, u8 t4, u8 len16,
				 union t4_recv_wr *wqe)
{
	if (!t4) {
		mmio_wc_start();
		if (t5_en_wc && inc == 1 && wq->sq.wc_reg_available) {
			PDBG("%s: WC wq->rq.pidx = %d; len16=%d\n",
			     __func__, wq->rq.pidx, len16);
			copy_wqe_to_udb(wq->rq.udb + 14, wqe);
		} else {
			PDBG("%s: DB wq->rq.pidx = %d; len16=%d\n",
			     __func__, wq->rq.pidx, len16);
			writel(QID_V(wq->rq.bar2_qid) | PIDX_T5_V(inc),
			       wq->rq.udb);
		}
		/* udb is WC for > t4 devices */
		mmio_flush_writes();
		return;
	}
	/* udb is UC for t4 devices */
	udma_to_device_barrier();
	writel(QID_V(wq->rq.qid & wq->qid_mask) | PIDX_V(inc), wq->rq.udb);
}

static inline int t4_wq_in_error(struct t4_wq *wq)
{
	return wq->error || wq->rq.queue[wq->rq.size].status.qp_err;
}

static inline void t4_set_wq_in_error(struct t4_wq *wq)
{
	wq->rq.queue[wq->rq.size].status.qp_err = 1;
}

extern int c4iw_abi_version;

static inline int t4_wq_db_enabled(struct t4_wq *wq)
{
	/*
	 * If iw_cxgb4 driver supports door bell drop recovery then its
	 * c4iw_abi_version would be greater than or equal to 2. In such
	 * case return the status of db_off flag to ring the kernel mode
	 * DB from user mode library.
	 */
	if ( c4iw_abi_version >= 2 )
		return ! *wq->db_offp;
	else
		return 1;
}

struct t4_cq {
	struct t4_cqe *queue;
	struct t4_cqe *sw_queue;
	struct c4iw_rdev *rdev;
	volatile u32 *ugts;
	size_t memsize;
	u64 bits_type_ts;
	u32 cqid;
	u32 qid_mask;
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

	while (cq->cidx_inc > CIDXINC_M) {
		val = SEINTARM_V(0) | CIDXINC_V(CIDXINC_M) | TIMERREG_V(7) |
		      INGRESSQID_V(cq->cqid & cq->qid_mask);
		writel(val, cq->ugts);
		cq->cidx_inc -= CIDXINC_M;
	}
	val = SEINTARM_V(se) | CIDXINC_V(cq->cidx_inc) | TIMERREG_V(6) |
	      INGRESSQID_V(cq->cqid & cq->qid_mask);
	writel(val, cq->ugts);
	cq->cidx_inc = 0;
	return 0;
}

static inline void t4_swcq_produce(struct t4_cq *cq)
{
	cq->sw_in_use++;
	if (cq->sw_in_use == cq->size) {
		syslog(LOG_NOTICE, "cxgb4 sw cq overflow cqid %u\n", cq->cqid);
		cq->error = 1;
		assert(0);
	}
	if (++cq->sw_pidx == cq->size)
		cq->sw_pidx = 0;
}

static inline void t4_swcq_consume(struct t4_cq *cq)
{
	assert(cq->sw_in_use >= 1);
	cq->sw_in_use--;
	if (++cq->sw_cidx == cq->size)
		cq->sw_cidx = 0;
}

static inline void t4_hwcq_consume(struct t4_cq *cq)
{
	cq->bits_type_ts = cq->queue[cq->cidx].bits_type_ts;
	if (++cq->cidx_inc == (cq->size >> 4) || cq->cidx_inc == CIDXINC_M) {
		uint32_t val;

		val = SEINTARM_V(0) | CIDXINC_V(cq->cidx_inc) | TIMERREG_V(7) |
			INGRESSQID_V(cq->cqid & cq->qid_mask);
		writel(val, cq->ugts);
		cq->cidx_inc = 0;
	}
	if (++cq->cidx == cq->size) {
		cq->cidx = 0;
		cq->gen ^= 1;
	}
	((struct t4_status_page *)&cq->queue[cq->size])->host_cidx = cq->cidx;
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
		syslog(LOG_NOTICE, "cxgb4 cq overflow cqid %u\n", cq->cqid);
		cq->error = 1;
		assert(0);
	} else if (t4_valid_cqe(cq, &cq->queue[cq->cidx])) {
		udma_from_device_barrier();
		*cqe = &cq->queue[cq->cidx];
		ret = 0;
	} else
		ret = -ENODATA;
	return ret;
}

static inline struct t4_cqe *t4_next_sw_cqe(struct t4_cq *cq)
{
	if (cq->sw_in_use == cq->size) {
		syslog(LOG_NOTICE, "cxgb4 sw cq overflow cqid %u\n", cq->cqid);
		cq->error = 1;
		assert(0);
		return NULL;
	}
	if (cq->sw_in_use)
		return &cq->sw_queue[cq->sw_cidx];
	return NULL;
}

static inline int t4_cq_notempty(struct t4_cq *cq)
{
	return cq->sw_in_use || t4_valid_cqe(cq, &cq->queue[cq->cidx]);
}

static inline int t4_next_cqe(struct t4_cq *cq, struct t4_cqe **cqe)
{
	int ret = 0;

	if (cq->error)
		ret = -ENODATA;
	else if (cq->sw_in_use)
		*cqe = &cq->sw_queue[cq->sw_cidx];
	else ret = t4_next_hw_cqe(cq, cqe);
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

static inline void t4_reset_cq_in_error(struct t4_cq *cq)
{
	((struct t4_status_page *)&cq->queue[cq->size])->qp_err = 0;
}

struct t4_dev_status_page 
{
	u8 db_off;
	u8 wc_supported;
	u16 pad2;
	u32 pad3;
	u64 qp_start;
	u64 qp_size;
	u64 cq_start;
	u64 cq_size;
};

#endif
