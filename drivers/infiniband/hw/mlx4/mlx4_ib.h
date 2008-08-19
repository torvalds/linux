/*
 * Copyright (c) 2006, 2007 Cisco Systems.  All rights reserved.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#ifndef MLX4_IB_H
#define MLX4_IB_H

#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>

#include <linux/mlx4/device.h>
#include <linux/mlx4/doorbell.h>

struct mlx4_ib_ucontext {
	struct ib_ucontext	ibucontext;
	struct mlx4_uar		uar;
	struct list_head	db_page_list;
	struct mutex		db_page_mutex;
};

struct mlx4_ib_pd {
	struct ib_pd		ibpd;
	u32			pdn;
};

struct mlx4_ib_cq_buf {
	struct mlx4_buf		buf;
	struct mlx4_mtt		mtt;
};

struct mlx4_ib_cq_resize {
	struct mlx4_ib_cq_buf	buf;
	int			cqe;
};

struct mlx4_ib_cq {
	struct ib_cq		ibcq;
	struct mlx4_cq		mcq;
	struct mlx4_ib_cq_buf	buf;
	struct mlx4_ib_cq_resize *resize_buf;
	struct mlx4_db		db;
	spinlock_t		lock;
	struct mutex		resize_mutex;
	struct ib_umem	       *umem;
	struct ib_umem	       *resize_umem;
};

struct mlx4_ib_mr {
	struct ib_mr		ibmr;
	struct mlx4_mr		mmr;
	struct ib_umem	       *umem;
};

struct mlx4_ib_fast_reg_page_list {
	struct ib_fast_reg_page_list	ibfrpl;
	dma_addr_t			map;
};

struct mlx4_ib_fmr {
	struct ib_fmr           ibfmr;
	struct mlx4_fmr         mfmr;
};

struct mlx4_ib_wq {
	u64		       *wrid;
	spinlock_t		lock;
	int			wqe_cnt;
	int			max_post;
	int			max_gs;
	int			offset;
	int			wqe_shift;
	unsigned		head;
	unsigned		tail;
};

enum mlx4_ib_qp_flags {
	MLX4_IB_QP_LSO				= 1 << 0,
	MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK	= 1 << 1,
};

struct mlx4_ib_qp {
	struct ib_qp		ibqp;
	struct mlx4_qp		mqp;
	struct mlx4_buf		buf;

	struct mlx4_db		db;
	struct mlx4_ib_wq	rq;

	u32			doorbell_qpn;
	__be32			sq_signal_bits;
	unsigned		sq_next_wqe;
	int			sq_max_wqes_per_wr;
	int			sq_spare_wqes;
	struct mlx4_ib_wq	sq;

	struct ib_umem	       *umem;
	struct mlx4_mtt		mtt;
	int			buf_size;
	struct mutex		mutex;
	u32			flags;
	u8			port;
	u8			alt_port;
	u8			atomic_rd_en;
	u8			resp_depth;
	u8			sq_no_prefetch;
	u8			state;
};

struct mlx4_ib_srq {
	struct ib_srq		ibsrq;
	struct mlx4_srq		msrq;
	struct mlx4_buf		buf;
	struct mlx4_db		db;
	u64		       *wrid;
	spinlock_t		lock;
	int			head;
	int			tail;
	u16			wqe_ctr;
	struct ib_umem	       *umem;
	struct mlx4_mtt		mtt;
	struct mutex		mutex;
};

struct mlx4_ib_ah {
	struct ib_ah		ibah;
	struct mlx4_av		av;
};

struct mlx4_ib_dev {
	struct ib_device	ib_dev;
	struct mlx4_dev	       *dev;
	void __iomem	       *uar_map;

	struct mlx4_uar		priv_uar;
	u32			priv_pdn;
	MLX4_DECLARE_DOORBELL_LOCK(uar_lock);

	struct ib_mad_agent    *send_agent[MLX4_MAX_PORTS][2];
	struct ib_ah	       *sm_ah[MLX4_MAX_PORTS];
	spinlock_t		sm_lock;

	struct mutex		cap_mask_mutex;
};

static inline struct mlx4_ib_dev *to_mdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct mlx4_ib_dev, ib_dev);
}

static inline struct mlx4_ib_ucontext *to_mucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct mlx4_ib_ucontext, ibucontext);
}

static inline struct mlx4_ib_pd *to_mpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct mlx4_ib_pd, ibpd);
}

static inline struct mlx4_ib_cq *to_mcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct mlx4_ib_cq, ibcq);
}

static inline struct mlx4_ib_cq *to_mibcq(struct mlx4_cq *mcq)
{
	return container_of(mcq, struct mlx4_ib_cq, mcq);
}

static inline struct mlx4_ib_mr *to_mmr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct mlx4_ib_mr, ibmr);
}

static inline struct mlx4_ib_fast_reg_page_list *to_mfrpl(struct ib_fast_reg_page_list *ibfrpl)
{
	return container_of(ibfrpl, struct mlx4_ib_fast_reg_page_list, ibfrpl);
}

static inline struct mlx4_ib_fmr *to_mfmr(struct ib_fmr *ibfmr)
{
	return container_of(ibfmr, struct mlx4_ib_fmr, ibfmr);
}
static inline struct mlx4_ib_qp *to_mqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct mlx4_ib_qp, ibqp);
}

static inline struct mlx4_ib_qp *to_mibqp(struct mlx4_qp *mqp)
{
	return container_of(mqp, struct mlx4_ib_qp, mqp);
}

static inline struct mlx4_ib_srq *to_msrq(struct ib_srq *ibsrq)
{
	return container_of(ibsrq, struct mlx4_ib_srq, ibsrq);
}

static inline struct mlx4_ib_srq *to_mibsrq(struct mlx4_srq *msrq)
{
	return container_of(msrq, struct mlx4_ib_srq, msrq);
}

static inline struct mlx4_ib_ah *to_mah(struct ib_ah *ibah)
{
	return container_of(ibah, struct mlx4_ib_ah, ibah);
}

int mlx4_ib_db_map_user(struct mlx4_ib_ucontext *context, unsigned long virt,
			struct mlx4_db *db);
void mlx4_ib_db_unmap_user(struct mlx4_ib_ucontext *context, struct mlx4_db *db);

struct ib_mr *mlx4_ib_get_dma_mr(struct ib_pd *pd, int acc);
int mlx4_ib_umem_write_mtt(struct mlx4_ib_dev *dev, struct mlx4_mtt *mtt,
			   struct ib_umem *umem);
struct ib_mr *mlx4_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int access_flags,
				  struct ib_udata *udata);
int mlx4_ib_dereg_mr(struct ib_mr *mr);
struct ib_mr *mlx4_ib_alloc_fast_reg_mr(struct ib_pd *pd,
					int max_page_list_len);
struct ib_fast_reg_page_list *mlx4_ib_alloc_fast_reg_page_list(struct ib_device *ibdev,
							       int page_list_len);
void mlx4_ib_free_fast_reg_page_list(struct ib_fast_reg_page_list *page_list);

int mlx4_ib_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period);
int mlx4_ib_resize_cq(struct ib_cq *ibcq, int entries, struct ib_udata *udata);
struct ib_cq *mlx4_ib_create_cq(struct ib_device *ibdev, int entries, int vector,
				struct ib_ucontext *context,
				struct ib_udata *udata);
int mlx4_ib_destroy_cq(struct ib_cq *cq);
int mlx4_ib_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc);
int mlx4_ib_arm_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);
void __mlx4_ib_cq_clean(struct mlx4_ib_cq *cq, u32 qpn, struct mlx4_ib_srq *srq);
void mlx4_ib_cq_clean(struct mlx4_ib_cq *cq, u32 qpn, struct mlx4_ib_srq *srq);

struct ib_ah *mlx4_ib_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr);
int mlx4_ib_query_ah(struct ib_ah *ibah, struct ib_ah_attr *ah_attr);
int mlx4_ib_destroy_ah(struct ib_ah *ah);

struct ib_srq *mlx4_ib_create_srq(struct ib_pd *pd,
				  struct ib_srq_init_attr *init_attr,
				  struct ib_udata *udata);
int mlx4_ib_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		       enum ib_srq_attr_mask attr_mask, struct ib_udata *udata);
int mlx4_ib_query_srq(struct ib_srq *srq, struct ib_srq_attr *srq_attr);
int mlx4_ib_destroy_srq(struct ib_srq *srq);
void mlx4_ib_free_srq_wqe(struct mlx4_ib_srq *srq, int wqe_index);
int mlx4_ib_post_srq_recv(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
			  struct ib_recv_wr **bad_wr);

struct ib_qp *mlx4_ib_create_qp(struct ib_pd *pd,
				struct ib_qp_init_attr *init_attr,
				struct ib_udata *udata);
int mlx4_ib_destroy_qp(struct ib_qp *qp);
int mlx4_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata);
int mlx4_ib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr, int qp_attr_mask,
		     struct ib_qp_init_attr *qp_init_attr);
int mlx4_ib_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		      struct ib_send_wr **bad_wr);
int mlx4_ib_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		      struct ib_recv_wr **bad_wr);

int mlx4_MAD_IFC(struct mlx4_ib_dev *dev, int ignore_mkey, int ignore_bkey,
		 int port, struct ib_wc *in_wc, struct ib_grh *in_grh,
		 void *in_mad, void *response_mad);
int mlx4_ib_process_mad(struct ib_device *ibdev, int mad_flags,	u8 port_num,
			struct ib_wc *in_wc, struct ib_grh *in_grh,
			struct ib_mad *in_mad, struct ib_mad *out_mad);
int mlx4_ib_mad_init(struct mlx4_ib_dev *dev);
void mlx4_ib_mad_cleanup(struct mlx4_ib_dev *dev);

struct ib_fmr *mlx4_ib_fmr_alloc(struct ib_pd *pd, int mr_access_flags,
				  struct ib_fmr_attr *fmr_attr);
int mlx4_ib_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list, int npages,
			 u64 iova);
int mlx4_ib_unmap_fmr(struct list_head *fmr_list);
int mlx4_ib_fmr_dealloc(struct ib_fmr *fmr);

static inline int mlx4_ib_ah_grh_present(struct mlx4_ib_ah *ah)
{
	return !!(ah->av.g_slid & 0x80);
}

#endif /* MLX4_IB_H */
