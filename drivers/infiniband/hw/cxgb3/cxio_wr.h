/*
 * Copyright (c) 2006 Chelsio, Inc. All rights reserved.
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
 *
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
#ifndef __CXIO_WR_H__
#define __CXIO_WR_H__

#include <asm/io.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include "firmware_exports.h"

#define T3_MAX_SGE      4

#define Q_EMPTY(rptr,wptr) ((rptr)==(wptr))
#define Q_FULL(rptr,wptr,size_log2)  ( (((wptr)-(rptr))>>(size_log2)) && \
				       ((rptr)!=(wptr)) )
#define Q_GENBIT(ptr,size_log2) (!(((ptr)>>size_log2)&0x1))
#define Q_FREECNT(rptr,wptr,size_log2) ((1UL<<size_log2)-((wptr)-(rptr)))
#define Q_COUNT(rptr,wptr) ((wptr)-(rptr))
#define Q_PTR2IDX(ptr,size_log2) (ptr & ((1UL<<size_log2)-1))

static inline void ring_doorbell(void __iomem *doorbell, u32 qpid)
{
	writel(((1<<31) | qpid), doorbell);
}

#define SEQ32_GE(x,y) (!( (((u32) (x)) - ((u32) (y))) & 0x80000000 ))

enum t3_wr_flags {
	T3_COMPLETION_FLAG = 0x01,
	T3_NOTIFY_FLAG = 0x02,
	T3_SOLICITED_EVENT_FLAG = 0x04,
	T3_READ_FENCE_FLAG = 0x08,
	T3_LOCAL_FENCE_FLAG = 0x10
} __attribute__ ((packed));

enum t3_wr_opcode {
	T3_WR_BP = FW_WROPCODE_RI_BYPASS,
	T3_WR_SEND = FW_WROPCODE_RI_SEND,
	T3_WR_WRITE = FW_WROPCODE_RI_RDMA_WRITE,
	T3_WR_READ = FW_WROPCODE_RI_RDMA_READ,
	T3_WR_INV_STAG = FW_WROPCODE_RI_LOCAL_INV,
	T3_WR_BIND = FW_WROPCODE_RI_BIND_MW,
	T3_WR_RCV = FW_WROPCODE_RI_RECEIVE,
	T3_WR_INIT = FW_WROPCODE_RI_RDMA_INIT,
	T3_WR_QP_MOD = FW_WROPCODE_RI_MODIFY_QP
} __attribute__ ((packed));

enum t3_rdma_opcode {
	T3_RDMA_WRITE,		/* IETF RDMAP v1.0 ... */
	T3_READ_REQ,
	T3_READ_RESP,
	T3_SEND,
	T3_SEND_WITH_INV,
	T3_SEND_WITH_SE,
	T3_SEND_WITH_SE_INV,
	T3_TERMINATE,
	T3_RDMA_INIT,		/* CHELSIO RI specific ... */
	T3_BIND_MW,
	T3_FAST_REGISTER,
	T3_LOCAL_INV,
	T3_QP_MOD,
	T3_BYPASS
} __attribute__ ((packed));

static inline enum t3_rdma_opcode wr2opcode(enum t3_wr_opcode wrop)
{
	switch (wrop) {
		case T3_WR_BP: return T3_BYPASS;
		case T3_WR_SEND: return T3_SEND;
		case T3_WR_WRITE: return T3_RDMA_WRITE;
		case T3_WR_READ: return T3_READ_REQ;
		case T3_WR_INV_STAG: return T3_LOCAL_INV;
		case T3_WR_BIND: return T3_BIND_MW;
		case T3_WR_INIT: return T3_RDMA_INIT;
		case T3_WR_QP_MOD: return T3_QP_MOD;
		default: break;
	}
	return -1;
}


/* Work request id */
union t3_wrid {
	struct {
		u32 hi;
		u32 low;
	} id0;
	u64 id1;
};

#define WRID(wrid)		(wrid.id1)
#define WRID_GEN(wrid)		(wrid.id0.wr_gen)
#define WRID_IDX(wrid)		(wrid.id0.wr_idx)
#define WRID_LO(wrid)		(wrid.id0.wr_lo)

struct fw_riwrh {
	__be32 op_seop_flags;
	__be32 gen_tid_len;
};

#define S_FW_RIWR_OP		24
#define M_FW_RIWR_OP		0xff
#define V_FW_RIWR_OP(x)		((x) << S_FW_RIWR_OP)
#define G_FW_RIWR_OP(x)	((((x) >> S_FW_RIWR_OP)) & M_FW_RIWR_OP)

#define S_FW_RIWR_SOPEOP	22
#define M_FW_RIWR_SOPEOP	0x3
#define V_FW_RIWR_SOPEOP(x)	((x) << S_FW_RIWR_SOPEOP)

#define S_FW_RIWR_FLAGS		8
#define M_FW_RIWR_FLAGS		0x3fffff
#define V_FW_RIWR_FLAGS(x)	((x) << S_FW_RIWR_FLAGS)
#define G_FW_RIWR_FLAGS(x)	((((x) >> S_FW_RIWR_FLAGS)) & M_FW_RIWR_FLAGS)

#define S_FW_RIWR_TID		8
#define V_FW_RIWR_TID(x)	((x) << S_FW_RIWR_TID)

#define S_FW_RIWR_LEN		0
#define V_FW_RIWR_LEN(x)	((x) << S_FW_RIWR_LEN)

#define S_FW_RIWR_GEN           31
#define V_FW_RIWR_GEN(x)        ((x)  << S_FW_RIWR_GEN)

struct t3_sge {
	__be32 stag;
	__be32 len;
	__be64 to;
};

/* If num_sgle is zero, flit 5+ contains immediate data.*/
struct t3_send_wr {
	struct fw_riwrh wrh;	/* 0 */
	union t3_wrid wrid;	/* 1 */

	u8 rdmaop;		/* 2 */
	u8 reserved[3];
	__be32 rem_stag;
	__be32 plen;		/* 3 */
	__be32 num_sgle;
	struct t3_sge sgl[T3_MAX_SGE];	/* 4+ */
};

struct t3_local_inv_wr {
	struct fw_riwrh wrh;	/* 0 */
	union t3_wrid wrid;	/* 1 */
	__be32 stag;		/* 2 */
	__be32 reserved3;
};

struct t3_rdma_write_wr {
	struct fw_riwrh wrh;	/* 0 */
	union t3_wrid wrid;	/* 1 */
	u8 rdmaop;		/* 2 */
	u8 reserved[3];
	__be32 stag_sink;
	__be64 to_sink;		/* 3 */
	__be32 plen;		/* 4 */
	__be32 num_sgle;
	struct t3_sge sgl[T3_MAX_SGE];	/* 5+ */
};

struct t3_rdma_read_wr {
	struct fw_riwrh wrh;	/* 0 */
	union t3_wrid wrid;	/* 1 */
	u8 rdmaop;		/* 2 */
	u8 reserved[3];
	__be32 rem_stag;
	__be64 rem_to;		/* 3 */
	__be32 local_stag;	/* 4 */
	__be32 local_len;
	__be64 local_to;	/* 5 */
};

enum t3_addr_type {
	T3_VA_BASED_TO = 0x0,
	T3_ZERO_BASED_TO = 0x1
} __attribute__ ((packed));

enum t3_mem_perms {
	T3_MEM_ACCESS_LOCAL_READ = 0x1,
	T3_MEM_ACCESS_LOCAL_WRITE = 0x2,
	T3_MEM_ACCESS_REM_READ = 0x4,
	T3_MEM_ACCESS_REM_WRITE = 0x8
} __attribute__ ((packed));

struct t3_bind_mw_wr {
	struct fw_riwrh wrh;	/* 0 */
	union t3_wrid wrid;	/* 1 */
	u16 reserved;		/* 2 */
	u8 type;
	u8 perms;
	__be32 mr_stag;
	__be32 mw_stag;		/* 3 */
	__be32 mw_len;
	__be64 mw_va;		/* 4 */
	__be32 mr_pbl_addr;	/* 5 */
	u8 reserved2[3];
	u8 mr_pagesz;
};

struct t3_receive_wr {
	struct fw_riwrh wrh;	/* 0 */
	union t3_wrid wrid;	/* 1 */
	u8 pagesz[T3_MAX_SGE];
	__be32 num_sgle;		/* 2 */
	struct t3_sge sgl[T3_MAX_SGE];	/* 3+ */
	__be32 pbl_addr[T3_MAX_SGE];
};

struct t3_bypass_wr {
	struct fw_riwrh wrh;
	union t3_wrid wrid;	/* 1 */
};

struct t3_modify_qp_wr {
	struct fw_riwrh wrh;	/* 0 */
	union t3_wrid wrid;	/* 1 */
	__be32 flags;		/* 2 */
	__be32 quiesce;		/* 2 */
	__be32 max_ird;		/* 3 */
	__be32 max_ord;		/* 3 */
	__be64 sge_cmd;		/* 4 */
	__be64 ctx1;		/* 5 */
	__be64 ctx0;		/* 6 */
};

enum t3_modify_qp_flags {
	MODQP_QUIESCE  = 0x01,
	MODQP_MAX_IRD  = 0x02,
	MODQP_MAX_ORD  = 0x04,
	MODQP_WRITE_EC = 0x08,
	MODQP_READ_EC  = 0x10,
};


enum t3_mpa_attrs {
	uP_RI_MPA_RX_MARKER_ENABLE = 0x1,
	uP_RI_MPA_TX_MARKER_ENABLE = 0x2,
	uP_RI_MPA_CRC_ENABLE = 0x4,
	uP_RI_MPA_IETF_ENABLE = 0x8
} __attribute__ ((packed));

enum t3_qp_caps {
	uP_RI_QP_RDMA_READ_ENABLE = 0x01,
	uP_RI_QP_RDMA_WRITE_ENABLE = 0x02,
	uP_RI_QP_BIND_ENABLE = 0x04,
	uP_RI_QP_FAST_REGISTER_ENABLE = 0x08,
	uP_RI_QP_STAG0_ENABLE = 0x10
} __attribute__ ((packed));

struct t3_rdma_init_attr {
	u32 tid;
	u32 qpid;
	u32 pdid;
	u32 scqid;
	u32 rcqid;
	u32 rq_addr;
	u32 rq_size;
	enum t3_mpa_attrs mpaattrs;
	enum t3_qp_caps qpcaps;
	u16 tcp_emss;
	u32 ord;
	u32 ird;
	u64 qp_dma_addr;
	u32 qp_dma_size;
	u32 flags;
};

struct t3_rdma_init_wr {
	struct fw_riwrh wrh;	/* 0 */
	union t3_wrid wrid;	/* 1 */
	__be32 qpid;		/* 2 */
	__be32 pdid;
	__be32 scqid;		/* 3 */
	__be32 rcqid;
	__be32 rq_addr;		/* 4 */
	__be32 rq_size;
	u8 mpaattrs;		/* 5 */
	u8 qpcaps;
	__be16 ulpdu_size;
	__be32 flags;		/* bits 31-1 - reservered */
				/* bit     0 - set if RECV posted */
	__be32 ord;		/* 6 */
	__be32 ird;
	__be64 qp_dma_addr;	/* 7 */
	__be32 qp_dma_size;	/* 8 */
	u32 rsvd;
};

struct t3_genbit {
	u64 flit[15];
	__be64 genbit;
};

enum rdma_init_wr_flags {
	RECVS_POSTED = 1,
};

union t3_wr {
	struct t3_send_wr send;
	struct t3_rdma_write_wr write;
	struct t3_rdma_read_wr read;
	struct t3_receive_wr recv;
	struct t3_local_inv_wr local_inv;
	struct t3_bind_mw_wr bind;
	struct t3_bypass_wr bypass;
	struct t3_rdma_init_wr init;
	struct t3_modify_qp_wr qp_mod;
	struct t3_genbit genbit;
	u64 flit[16];
};

#define T3_SQ_CQE_FLIT	  13
#define T3_SQ_COOKIE_FLIT 14

#define T3_RQ_COOKIE_FLIT 13
#define T3_RQ_CQE_FLIT	  14

static inline enum t3_wr_opcode fw_riwrh_opcode(struct fw_riwrh *wqe)
{
	return G_FW_RIWR_OP(be32_to_cpu(wqe->op_seop_flags));
}

static inline void build_fw_riwrh(struct fw_riwrh *wqe, enum t3_wr_opcode op,
				  enum t3_wr_flags flags, u8 genbit, u32 tid,
				  u8 len)
{
	wqe->op_seop_flags = cpu_to_be32(V_FW_RIWR_OP(op) |
					 V_FW_RIWR_SOPEOP(M_FW_RIWR_SOPEOP) |
					 V_FW_RIWR_FLAGS(flags));
	wmb();
	wqe->gen_tid_len = cpu_to_be32(V_FW_RIWR_GEN(genbit) |
				       V_FW_RIWR_TID(tid) |
				       V_FW_RIWR_LEN(len));
	/* 2nd gen bit... */
	((union t3_wr *)wqe)->genbit.genbit = cpu_to_be64(genbit);
}

/*
 * T3 ULP2_TX commands
 */
enum t3_utx_mem_op {
	T3_UTX_MEM_READ = 2,
	T3_UTX_MEM_WRITE = 3
};

/* T3 MC7 RDMA TPT entry format */

enum tpt_mem_type {
	TPT_NON_SHARED_MR = 0x0,
	TPT_SHARED_MR = 0x1,
	TPT_MW = 0x2,
	TPT_MW_RELAXED_PROTECTION = 0x3
};

enum tpt_addr_type {
	TPT_ZBTO = 0,
	TPT_VATO = 1
};

enum tpt_mem_perm {
	TPT_LOCAL_READ = 0x8,
	TPT_LOCAL_WRITE = 0x4,
	TPT_REMOTE_READ = 0x2,
	TPT_REMOTE_WRITE = 0x1
};

struct tpt_entry {
	__be32 valid_stag_pdid;
	__be32 flags_pagesize_qpid;

	__be32 rsvd_pbl_addr;
	__be32 len;
	__be32 va_hi;
	__be32 va_low_or_fbo;

	__be32 rsvd_bind_cnt_or_pstag;
	__be32 rsvd_pbl_size;
};

#define S_TPT_VALID		31
#define V_TPT_VALID(x)		((x) << S_TPT_VALID)
#define F_TPT_VALID		V_TPT_VALID(1U)

#define S_TPT_STAG_KEY		23
#define M_TPT_STAG_KEY		0xFF
#define V_TPT_STAG_KEY(x)	((x) << S_TPT_STAG_KEY)
#define G_TPT_STAG_KEY(x)	(((x) >> S_TPT_STAG_KEY) & M_TPT_STAG_KEY)

#define S_TPT_STAG_STATE	22
#define V_TPT_STAG_STATE(x)	((x) << S_TPT_STAG_STATE)
#define F_TPT_STAG_STATE	V_TPT_STAG_STATE(1U)

#define S_TPT_STAG_TYPE		20
#define M_TPT_STAG_TYPE		0x3
#define V_TPT_STAG_TYPE(x)	((x) << S_TPT_STAG_TYPE)
#define G_TPT_STAG_TYPE(x)	(((x) >> S_TPT_STAG_TYPE) & M_TPT_STAG_TYPE)

#define S_TPT_PDID		0
#define M_TPT_PDID		0xFFFFF
#define V_TPT_PDID(x)		((x) << S_TPT_PDID)
#define G_TPT_PDID(x)		(((x) >> S_TPT_PDID) & M_TPT_PDID)

#define S_TPT_PERM		28
#define M_TPT_PERM		0xF
#define V_TPT_PERM(x)		((x) << S_TPT_PERM)
#define G_TPT_PERM(x)		(((x) >> S_TPT_PERM) & M_TPT_PERM)

#define S_TPT_REM_INV_DIS	27
#define V_TPT_REM_INV_DIS(x)	((x) << S_TPT_REM_INV_DIS)
#define F_TPT_REM_INV_DIS	V_TPT_REM_INV_DIS(1U)

#define S_TPT_ADDR_TYPE		26
#define V_TPT_ADDR_TYPE(x)	((x) << S_TPT_ADDR_TYPE)
#define F_TPT_ADDR_TYPE		V_TPT_ADDR_TYPE(1U)

#define S_TPT_MW_BIND_ENABLE	25
#define V_TPT_MW_BIND_ENABLE(x)	((x) << S_TPT_MW_BIND_ENABLE)
#define F_TPT_MW_BIND_ENABLE    V_TPT_MW_BIND_ENABLE(1U)

#define S_TPT_PAGE_SIZE		20
#define M_TPT_PAGE_SIZE		0x1F
#define V_TPT_PAGE_SIZE(x)	((x) << S_TPT_PAGE_SIZE)
#define G_TPT_PAGE_SIZE(x)	(((x) >> S_TPT_PAGE_SIZE) & M_TPT_PAGE_SIZE)

#define S_TPT_PBL_ADDR		0
#define M_TPT_PBL_ADDR		0x1FFFFFFF
#define V_TPT_PBL_ADDR(x)	((x) << S_TPT_PBL_ADDR)
#define G_TPT_PBL_ADDR(x)       (((x) >> S_TPT_PBL_ADDR) & M_TPT_PBL_ADDR)

#define S_TPT_QPID		0
#define M_TPT_QPID		0xFFFFF
#define V_TPT_QPID(x)		((x) << S_TPT_QPID)
#define G_TPT_QPID(x)		(((x) >> S_TPT_QPID) & M_TPT_QPID)

#define S_TPT_PSTAG		0
#define M_TPT_PSTAG		0xFFFFFF
#define V_TPT_PSTAG(x)		((x) << S_TPT_PSTAG)
#define G_TPT_PSTAG(x)		(((x) >> S_TPT_PSTAG) & M_TPT_PSTAG)

#define S_TPT_PBL_SIZE		0
#define M_TPT_PBL_SIZE		0xFFFFF
#define V_TPT_PBL_SIZE(x)	((x) << S_TPT_PBL_SIZE)
#define G_TPT_PBL_SIZE(x)	(((x) >> S_TPT_PBL_SIZE) & M_TPT_PBL_SIZE)

/*
 * CQE defs
 */
struct t3_cqe {
	__be32 header;
	__be32 len;
	union {
		struct {
			__be32 stag;
			__be32 msn;
		} rcqe;
		struct {
			u32 wrid_hi;
			u32 wrid_low;
		} scqe;
	} u;
};

#define S_CQE_OOO	  31
#define M_CQE_OOO	  0x1
#define G_CQE_OOO(x)	  ((((x) >> S_CQE_OOO)) & M_CQE_OOO)
#define V_CEQ_OOO(x)	  ((x)<<S_CQE_OOO)

#define S_CQE_QPID        12
#define M_CQE_QPID        0x7FFFF
#define G_CQE_QPID(x)     ((((x) >> S_CQE_QPID)) & M_CQE_QPID)
#define V_CQE_QPID(x)	  ((x)<<S_CQE_QPID)

#define S_CQE_SWCQE       11
#define M_CQE_SWCQE       0x1
#define G_CQE_SWCQE(x)    ((((x) >> S_CQE_SWCQE)) & M_CQE_SWCQE)
#define V_CQE_SWCQE(x)	  ((x)<<S_CQE_SWCQE)

#define S_CQE_GENBIT      10
#define M_CQE_GENBIT      0x1
#define G_CQE_GENBIT(x)   (((x) >> S_CQE_GENBIT) & M_CQE_GENBIT)
#define V_CQE_GENBIT(x)	  ((x)<<S_CQE_GENBIT)

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

#define SW_CQE(x)         (G_CQE_SWCQE(be32_to_cpu((x).header)))
#define CQE_OOO(x)        (G_CQE_OOO(be32_to_cpu((x).header)))
#define CQE_QPID(x)       (G_CQE_QPID(be32_to_cpu((x).header)))
#define CQE_GENBIT(x)     (G_CQE_GENBIT(be32_to_cpu((x).header)))
#define CQE_TYPE(x)       (G_CQE_TYPE(be32_to_cpu((x).header)))
#define SQ_TYPE(x)	  (CQE_TYPE((x)))
#define RQ_TYPE(x)	  (!CQE_TYPE((x)))
#define CQE_STATUS(x)     (G_CQE_STATUS(be32_to_cpu((x).header)))
#define CQE_OPCODE(x)     (G_CQE_OPCODE(be32_to_cpu((x).header)))

#define CQE_LEN(x)        (be32_to_cpu((x).len))

/* used for RQ completion processing */
#define CQE_WRID_STAG(x)  (be32_to_cpu((x).u.rcqe.stag))
#define CQE_WRID_MSN(x)   (be32_to_cpu((x).u.rcqe.msn))

/* used for SQ completion processing */
#define CQE_WRID_SQ_WPTR(x)	((x).u.scqe.wrid_hi)
#define CQE_WRID_WPTR(x)	((x).u.scqe.wrid_low)

/* generic accessor macros */
#define CQE_WRID_HI(x)		((x).u.scqe.wrid_hi)
#define CQE_WRID_LOW(x)		((x).u.scqe.wrid_low)

#define TPT_ERR_SUCCESS                     0x0
#define TPT_ERR_STAG                        0x1	 /* STAG invalid: either the */
						 /* STAG is offlimt, being 0, */
						 /* or STAG_key mismatch */
#define TPT_ERR_PDID                        0x2	 /* PDID mismatch */
#define TPT_ERR_QPID                        0x3	 /* QPID mismatch */
#define TPT_ERR_ACCESS                      0x4	 /* Invalid access right */
#define TPT_ERR_WRAP                        0x5	 /* Wrap error */
#define TPT_ERR_BOUND                       0x6	 /* base and bounds voilation */
#define TPT_ERR_INVALIDATE_SHARED_MR        0x7	 /* attempt to invalidate a  */
						 /* shared memory region */
#define TPT_ERR_INVALIDATE_MR_WITH_MW_BOUND 0x8	 /* attempt to invalidate a  */
						 /* shared memory region */
#define TPT_ERR_ECC                         0x9	 /* ECC error detected */
#define TPT_ERR_ECC_PSTAG                   0xA	 /* ECC error detected when  */
						 /* reading PSTAG for a MW  */
						 /* Invalidate */
#define TPT_ERR_PBL_ADDR_BOUND              0xB	 /* pbl addr out of bounds:  */
						 /* software error */
#define TPT_ERR_SWFLUSH			    0xC	 /* SW FLUSHED */
#define TPT_ERR_CRC                         0x10 /* CRC error */
#define TPT_ERR_MARKER                      0x11 /* Marker error */
#define TPT_ERR_PDU_LEN_ERR                 0x12 /* invalid PDU length */
#define TPT_ERR_OUT_OF_RQE                  0x13 /* out of RQE */
#define TPT_ERR_DDP_VERSION                 0x14 /* wrong DDP version */
#define TPT_ERR_RDMA_VERSION                0x15 /* wrong RDMA version */
#define TPT_ERR_OPCODE                      0x16 /* invalid rdma opcode */
#define TPT_ERR_DDP_QUEUE_NUM               0x17 /* invalid ddp queue number */
#define TPT_ERR_MSN                         0x18 /* MSN error */
#define TPT_ERR_TBIT                        0x19 /* tag bit not set correctly */
#define TPT_ERR_MO                          0x1A /* MO not 0 for TERMINATE  */
						 /* or READ_REQ */
#define TPT_ERR_MSN_GAP                     0x1B
#define TPT_ERR_MSN_RANGE                   0x1C
#define TPT_ERR_IRD_OVERFLOW                0x1D
#define TPT_ERR_RQE_ADDR_BOUND              0x1E /* RQE addr out of bounds:  */
						 /* software error */
#define TPT_ERR_INTERNAL_ERR                0x1F /* internal error (opcode  */
						 /* mismatch) */

struct t3_swsq {
	__u64			wr_id;
	struct t3_cqe		cqe;
	__u32			sq_wptr;
	__be32			read_len;
	int			opcode;
	int			complete;
	int			signaled;
};

/*
 * A T3 WQ implements both the SQ and RQ.
 */
struct t3_wq {
	union t3_wr *queue;		/* DMA accessable memory */
	dma_addr_t dma_addr;		/* DMA address for HW */
	DECLARE_PCI_UNMAP_ADDR(mapping)	/* unmap kruft */
	u32 error;			/* 1 once we go to ERROR */
	u32 qpid;
	u32 wptr;			/* idx to next available WR slot */
	u32 size_log2;			/* total wq size */
	struct t3_swsq *sq;		/* SW SQ */
	struct t3_swsq *oldest_read;	/* tracks oldest pending read */
	u32 sq_wptr;			/* sq_wptr - sq_rptr == count of */
	u32 sq_rptr;			/* pending wrs */
	u32 sq_size_log2;		/* sq size */
	u64 *rq;			/* SW RQ (holds consumer wr_ids */
	u32 rq_wptr;			/* rq_wptr - rq_rptr == count of */
	u32 rq_rptr;			/* pending wrs */
	u64 *rq_oldest_wr;		/* oldest wr on the SW RQ */
	u32 rq_size_log2;		/* rq size */
	u32 rq_addr;			/* rq adapter address */
	void __iomem *doorbell;		/* kernel db */
	u64 udb;			/* user db if any */
};

struct t3_cq {
	u32 cqid;
	u32 rptr;
	u32 wptr;
	u32 size_log2;
	dma_addr_t dma_addr;
	DECLARE_PCI_UNMAP_ADDR(mapping)
	struct t3_cqe *queue;
	struct t3_cqe *sw_queue;
	u32 sw_rptr;
	u32 sw_wptr;
};

#define CQ_VLD_ENTRY(ptr,size_log2,cqe) (Q_GENBIT(ptr,size_log2) == \
					 CQE_GENBIT(*cqe))

static inline void cxio_set_wq_in_error(struct t3_wq *wq)
{
	wq->queue->flit[13] = 1;
}

static inline struct t3_cqe *cxio_next_hw_cqe(struct t3_cq *cq)
{
	struct t3_cqe *cqe;

	cqe = cq->queue + (Q_PTR2IDX(cq->rptr, cq->size_log2));
	if (CQ_VLD_ENTRY(cq->rptr, cq->size_log2, cqe))
		return cqe;
	return NULL;
}

static inline struct t3_cqe *cxio_next_sw_cqe(struct t3_cq *cq)
{
	struct t3_cqe *cqe;

	if (!Q_EMPTY(cq->sw_rptr, cq->sw_wptr)) {
		cqe = cq->sw_queue + (Q_PTR2IDX(cq->sw_rptr, cq->size_log2));
		return cqe;
	}
	return NULL;
}

static inline struct t3_cqe *cxio_next_cqe(struct t3_cq *cq)
{
	struct t3_cqe *cqe;

	if (!Q_EMPTY(cq->sw_rptr, cq->sw_wptr)) {
		cqe = cq->sw_queue + (Q_PTR2IDX(cq->sw_rptr, cq->size_log2));
		return cqe;
	}
	cqe = cq->queue + (Q_PTR2IDX(cq->rptr, cq->size_log2));
	if (CQ_VLD_ENTRY(cq->rptr, cq->size_log2, cqe))
		return cqe;
	return NULL;
}

#endif
