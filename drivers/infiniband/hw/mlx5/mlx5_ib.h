/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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

#ifndef MLX5_IB_H
#define MLX5_IB_H

#include <linux/kernel.h>
#include <linux/sched.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_smi.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/srq.h>
#include <linux/types.h>

#define mlx5_ib_dbg(dev, format, arg...)				\
pr_debug("%s:%s:%d:(pid %d): " format, (dev)->ib_dev.name, __func__,	\
	 __LINE__, current->pid, ##arg)

#define mlx5_ib_err(dev, format, arg...)				\
pr_err("%s:%s:%d:(pid %d): " format, (dev)->ib_dev.name, __func__,	\
	__LINE__, current->pid, ##arg)

#define mlx5_ib_warn(dev, format, arg...)				\
pr_warn("%s:%s:%d:(pid %d): " format, (dev)->ib_dev.name, __func__,	\
	__LINE__, current->pid, ##arg)

enum {
	MLX5_IB_MMAP_CMD_SHIFT	= 8,
	MLX5_IB_MMAP_CMD_MASK	= 0xff,
};

enum mlx5_ib_mmap_cmd {
	MLX5_IB_MMAP_REGULAR_PAGE		= 0,
	MLX5_IB_MMAP_GET_CONTIGUOUS_PAGES	= 1, /* always last */
};

enum {
	MLX5_RES_SCAT_DATA32_CQE	= 0x1,
	MLX5_RES_SCAT_DATA64_CQE	= 0x2,
	MLX5_REQ_SCAT_DATA32_CQE	= 0x11,
	MLX5_REQ_SCAT_DATA64_CQE	= 0x22,
};

enum mlx5_ib_latency_class {
	MLX5_IB_LATENCY_CLASS_LOW,
	MLX5_IB_LATENCY_CLASS_MEDIUM,
	MLX5_IB_LATENCY_CLASS_HIGH,
	MLX5_IB_LATENCY_CLASS_FAST_PATH
};

enum mlx5_ib_mad_ifc_flags {
	MLX5_MAD_IFC_IGNORE_MKEY	= 1,
	MLX5_MAD_IFC_IGNORE_BKEY	= 2,
	MLX5_MAD_IFC_NET_VIEW		= 4,
};

struct mlx5_ib_ucontext {
	struct ib_ucontext	ibucontext;
	struct list_head	db_page_list;

	/* protect doorbell record alloc/free
	 */
	struct mutex		db_page_mutex;
	struct mlx5_uuar_info	uuari;
};

static inline struct mlx5_ib_ucontext *to_mucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct mlx5_ib_ucontext, ibucontext);
}

struct mlx5_ib_pd {
	struct ib_pd		ibpd;
	u32			pdn;
	u32			pa_lkey;
};

/* Use macros here so that don't have to duplicate
 * enum ib_send_flags and enum ib_qp_type for low-level driver
 */

#define MLX5_IB_SEND_UMR_UNREG	IB_SEND_RESERVED_START
#define MLX5_IB_SEND_UMR_FAIL_IF_FREE (IB_SEND_RESERVED_START << 1)
#define MLX5_IB_SEND_UMR_UPDATE_MTT (IB_SEND_RESERVED_START << 2)
#define MLX5_IB_QPT_REG_UMR	IB_QPT_RESERVED1
#define MLX5_IB_WR_UMR		IB_WR_RESERVED1

struct wr_list {
	u16	opcode;
	u16	next;
};

struct mlx5_ib_wq {
	u64		       *wrid;
	u32		       *wr_data;
	struct wr_list	       *w_list;
	unsigned	       *wqe_head;
	u16		        unsig_count;

	/* serialize post to the work queue
	 */
	spinlock_t		lock;
	int			wqe_cnt;
	int			max_post;
	int			max_gs;
	int			offset;
	int			wqe_shift;
	unsigned		head;
	unsigned		tail;
	u16			cur_post;
	u16			last_poll;
	void		       *qend;
};

enum {
	MLX5_QP_USER,
	MLX5_QP_KERNEL,
	MLX5_QP_EMPTY
};

/*
 * Connect-IB can trigger up to four concurrent pagefaults
 * per-QP.
 */
enum mlx5_ib_pagefault_context {
	MLX5_IB_PAGEFAULT_RESPONDER_READ,
	MLX5_IB_PAGEFAULT_REQUESTOR_READ,
	MLX5_IB_PAGEFAULT_RESPONDER_WRITE,
	MLX5_IB_PAGEFAULT_REQUESTOR_WRITE,
	MLX5_IB_PAGEFAULT_CONTEXTS
};

static inline enum mlx5_ib_pagefault_context
	mlx5_ib_get_pagefault_context(struct mlx5_pagefault *pagefault)
{
	return pagefault->flags & (MLX5_PFAULT_REQUESTOR | MLX5_PFAULT_WRITE);
}

struct mlx5_ib_pfault {
	struct work_struct	work;
	struct mlx5_pagefault	mpfault;
};

struct mlx5_ib_qp {
	struct ib_qp		ibqp;
	struct mlx5_core_qp	mqp;
	struct mlx5_buf		buf;

	struct mlx5_db		db;
	struct mlx5_ib_wq	rq;

	u32			doorbell_qpn;
	u8			sq_signal_bits;
	u8			fm_cache;
	int			sq_max_wqes_per_wr;
	int			sq_spare_wqes;
	struct mlx5_ib_wq	sq;

	struct ib_umem	       *umem;
	int			buf_size;

	/* serialize qp state modifications
	 */
	struct mutex		mutex;
	u16			xrcdn;
	u32			flags;
	u8			port;
	u8			alt_port;
	u8			atomic_rd_en;
	u8			resp_depth;
	u8			state;
	int			mlx_type;
	int			wq_sig;
	int			scat_cqe;
	int			max_inline_data;
	struct mlx5_bf	       *bf;
	int			has_rq;

	/* only for user space QPs. For kernel
	 * we have it from the bf object
	 */
	int			uuarn;

	int			create_type;
	u32			pa_lkey;

	/* Store signature errors */
	bool			signature_en;

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	/*
	 * A flag that is true for QP's that are in a state that doesn't
	 * allow page faults, and shouldn't schedule any more faults.
	 */
	int                     disable_page_faults;
	/*
	 * The disable_page_faults_lock protects a QP's disable_page_faults
	 * field, allowing for a thread to atomically check whether the QP
	 * allows page faults, and if so schedule a page fault.
	 */
	spinlock_t              disable_page_faults_lock;
	struct mlx5_ib_pfault	pagefaults[MLX5_IB_PAGEFAULT_CONTEXTS];
#endif
};

struct mlx5_ib_cq_buf {
	struct mlx5_buf		buf;
	struct ib_umem		*umem;
	int			cqe_size;
	int			nent;
};

enum mlx5_ib_qp_flags {
	MLX5_IB_QP_BLOCK_MULTICAST_LOOPBACK     = 1 << 0,
	MLX5_IB_QP_SIGNATURE_HANDLING           = 1 << 1,
};

struct mlx5_umr_wr {
	union {
		u64			virt_addr;
		u64			offset;
	} target;
	struct ib_pd		       *pd;
	unsigned int			page_shift;
	unsigned int			npages;
	u32				length;
	int				access_flags;
	u32				mkey;
};

struct mlx5_shared_mr_info {
	int mr_id;
	struct ib_umem		*umem;
};

struct mlx5_ib_cq {
	struct ib_cq		ibcq;
	struct mlx5_core_cq	mcq;
	struct mlx5_ib_cq_buf	buf;
	struct mlx5_db		db;

	/* serialize access to the CQ
	 */
	spinlock_t		lock;

	/* protect resize cq
	 */
	struct mutex		resize_mutex;
	struct mlx5_ib_cq_buf  *resize_buf;
	struct ib_umem	       *resize_umem;
	int			cqe_size;
};

struct mlx5_ib_srq {
	struct ib_srq		ibsrq;
	struct mlx5_core_srq	msrq;
	struct mlx5_buf		buf;
	struct mlx5_db		db;
	u64		       *wrid;
	/* protect SRQ hanlding
	 */
	spinlock_t		lock;
	int			head;
	int			tail;
	u16			wqe_ctr;
	struct ib_umem	       *umem;
	/* serialize arming a SRQ
	 */
	struct mutex		mutex;
	int			wq_sig;
};

struct mlx5_ib_xrcd {
	struct ib_xrcd		ibxrcd;
	u32			xrcdn;
};

enum mlx5_ib_mtt_access_flags {
	MLX5_IB_MTT_READ  = (1 << 0),
	MLX5_IB_MTT_WRITE = (1 << 1),
};

#define MLX5_IB_MTT_PRESENT (MLX5_IB_MTT_READ | MLX5_IB_MTT_WRITE)

struct mlx5_ib_mr {
	struct ib_mr		ibmr;
	struct mlx5_core_mr	mmr;
	struct ib_umem	       *umem;
	struct mlx5_shared_mr_info	*smr_info;
	struct list_head	list;
	int			order;
	int			umred;
	int			npages;
	struct mlx5_ib_dev     *dev;
	struct mlx5_create_mkey_mbox_out out;
	struct mlx5_core_sig_ctx    *sig;
	int			live;
};

struct mlx5_ib_fast_reg_page_list {
	struct ib_fast_reg_page_list	ibfrpl;
	__be64			       *mapped_page_list;
	dma_addr_t			map;
};

struct mlx5_ib_umr_context {
	enum ib_wc_status	status;
	struct completion	done;
};

static inline void mlx5_ib_init_umr_context(struct mlx5_ib_umr_context *context)
{
	context->status = -1;
	init_completion(&context->done);
}

struct umr_common {
	struct ib_pd	*pd;
	struct ib_cq	*cq;
	struct ib_qp	*qp;
	struct ib_mr	*mr;
	/* control access to UMR QP
	 */
	struct semaphore	sem;
};

enum {
	MLX5_FMR_INVALID,
	MLX5_FMR_VALID,
	MLX5_FMR_BUSY,
};

struct mlx5_ib_fmr {
	struct ib_fmr			ibfmr;
	struct mlx5_core_mr		mr;
	int				access_flags;
	int				state;
	/* protect fmr state
	 */
	spinlock_t			lock;
	u64				wrid;
	struct ib_send_wr		wr[2];
	u8				page_shift;
	struct ib_fast_reg_page_list	page_list;
};

struct mlx5_cache_ent {
	struct list_head	head;
	/* sync access to the cahce entry
	 */
	spinlock_t		lock;


	struct dentry	       *dir;
	char                    name[4];
	u32                     order;
	u32			size;
	u32                     cur;
	u32                     miss;
	u32			limit;

	struct dentry          *fsize;
	struct dentry          *fcur;
	struct dentry          *fmiss;
	struct dentry          *flimit;

	struct mlx5_ib_dev     *dev;
	struct work_struct	work;
	struct delayed_work	dwork;
	int			pending;
};

struct mlx5_mr_cache {
	struct workqueue_struct *wq;
	struct mlx5_cache_ent	ent[MAX_MR_CACHE_ENTRIES];
	int			stopped;
	struct dentry		*root;
	unsigned long		last_add;
};

struct mlx5_ib_resources {
	struct ib_cq	*c0;
	struct ib_xrcd	*x0;
	struct ib_xrcd	*x1;
	struct ib_pd	*p0;
	struct ib_srq	*s0;
};

struct mlx5_ib_dev {
	struct ib_device		ib_dev;
	struct mlx5_core_dev		*mdev;
	MLX5_DECLARE_DOORBELL_LOCK(uar_lock);
	struct list_head		eqs_list;
	int				num_ports;
	int				num_comp_vectors;
	/* serialize update of capability mask
	 */
	struct mutex			cap_mask_mutex;
	bool				ib_active;
	struct umr_common		umrc;
	/* sync used page count stats
	 */
	struct mlx5_ib_resources	devr;
	struct mlx5_mr_cache		cache;
	struct timer_list		delay_timer;
	int				fill_delay;
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	struct ib_odp_caps	odp_caps;
	/*
	 * Sleepable RCU that prevents destruction of MRs while they are still
	 * being used by a page fault handler.
	 */
	struct srcu_struct      mr_srcu;
#endif
};

static inline struct mlx5_ib_cq *to_mibcq(struct mlx5_core_cq *mcq)
{
	return container_of(mcq, struct mlx5_ib_cq, mcq);
}

static inline struct mlx5_ib_xrcd *to_mxrcd(struct ib_xrcd *ibxrcd)
{
	return container_of(ibxrcd, struct mlx5_ib_xrcd, ibxrcd);
}

static inline struct mlx5_ib_dev *to_mdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct mlx5_ib_dev, ib_dev);
}

static inline struct mlx5_ib_fmr *to_mfmr(struct ib_fmr *ibfmr)
{
	return container_of(ibfmr, struct mlx5_ib_fmr, ibfmr);
}

static inline struct mlx5_ib_cq *to_mcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct mlx5_ib_cq, ibcq);
}

static inline struct mlx5_ib_qp *to_mibqp(struct mlx5_core_qp *mqp)
{
	return container_of(mqp, struct mlx5_ib_qp, mqp);
}

static inline struct mlx5_ib_mr *to_mibmr(struct mlx5_core_mr *mmr)
{
	return container_of(mmr, struct mlx5_ib_mr, mmr);
}

static inline struct mlx5_ib_pd *to_mpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct mlx5_ib_pd, ibpd);
}

static inline struct mlx5_ib_srq *to_msrq(struct ib_srq *ibsrq)
{
	return container_of(ibsrq, struct mlx5_ib_srq, ibsrq);
}

static inline struct mlx5_ib_qp *to_mqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct mlx5_ib_qp, ibqp);
}

static inline struct mlx5_ib_srq *to_mibsrq(struct mlx5_core_srq *msrq)
{
	return container_of(msrq, struct mlx5_ib_srq, msrq);
}

static inline struct mlx5_ib_mr *to_mmr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct mlx5_ib_mr, ibmr);
}

static inline struct mlx5_ib_fast_reg_page_list *to_mfrpl(struct ib_fast_reg_page_list *ibfrpl)
{
	return container_of(ibfrpl, struct mlx5_ib_fast_reg_page_list, ibfrpl);
}

struct mlx5_ib_ah {
	struct ib_ah		ibah;
	struct mlx5_av		av;
};

static inline struct mlx5_ib_ah *to_mah(struct ib_ah *ibah)
{
	return container_of(ibah, struct mlx5_ib_ah, ibah);
}

int mlx5_ib_db_map_user(struct mlx5_ib_ucontext *context, unsigned long virt,
			struct mlx5_db *db);
void mlx5_ib_db_unmap_user(struct mlx5_ib_ucontext *context, struct mlx5_db *db);
void __mlx5_ib_cq_clean(struct mlx5_ib_cq *cq, u32 qpn, struct mlx5_ib_srq *srq);
void mlx5_ib_cq_clean(struct mlx5_ib_cq *cq, u32 qpn, struct mlx5_ib_srq *srq);
void mlx5_ib_free_srq_wqe(struct mlx5_ib_srq *srq, int wqe_index);
int mlx5_MAD_IFC(struct mlx5_ib_dev *dev, int ignore_mkey, int ignore_bkey,
		 u8 port, struct ib_wc *in_wc, struct ib_grh *in_grh,
		 void *in_mad, void *response_mad);
struct ib_ah *create_ib_ah(struct ib_ah_attr *ah_attr,
			   struct mlx5_ib_ah *ah);
struct ib_ah *mlx5_ib_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr);
int mlx5_ib_query_ah(struct ib_ah *ibah, struct ib_ah_attr *ah_attr);
int mlx5_ib_destroy_ah(struct ib_ah *ah);
struct ib_srq *mlx5_ib_create_srq(struct ib_pd *pd,
				  struct ib_srq_init_attr *init_attr,
				  struct ib_udata *udata);
int mlx5_ib_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		       enum ib_srq_attr_mask attr_mask, struct ib_udata *udata);
int mlx5_ib_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *srq_attr);
int mlx5_ib_destroy_srq(struct ib_srq *srq);
int mlx5_ib_post_srq_recv(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
			  struct ib_recv_wr **bad_wr);
struct ib_qp *mlx5_ib_create_qp(struct ib_pd *pd,
				struct ib_qp_init_attr *init_attr,
				struct ib_udata *udata);
int mlx5_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata);
int mlx5_ib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr, int qp_attr_mask,
		     struct ib_qp_init_attr *qp_init_attr);
int mlx5_ib_destroy_qp(struct ib_qp *qp);
int mlx5_ib_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		      struct ib_send_wr **bad_wr);
int mlx5_ib_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		      struct ib_recv_wr **bad_wr);
void *mlx5_get_send_wqe(struct mlx5_ib_qp *qp, int n);
int mlx5_ib_read_user_wqe(struct mlx5_ib_qp *qp, int send, int wqe_index,
			  void *buffer, u32 length);
struct ib_cq *mlx5_ib_create_cq(struct ib_device *ibdev, int entries,
				int vector, struct ib_ucontext *context,
				struct ib_udata *udata);
int mlx5_ib_destroy_cq(struct ib_cq *cq);
int mlx5_ib_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc);
int mlx5_ib_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags);
int mlx5_ib_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period);
int mlx5_ib_resize_cq(struct ib_cq *ibcq, int entries, struct ib_udata *udata);
struct ib_mr *mlx5_ib_get_dma_mr(struct ib_pd *pd, int acc);
struct ib_mr *mlx5_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int access_flags,
				  struct ib_udata *udata);
int mlx5_ib_update_mtt(struct mlx5_ib_mr *mr, u64 start_page_index,
		       int npages, int zap);
int mlx5_ib_dereg_mr(struct ib_mr *ibmr);
int mlx5_ib_destroy_mr(struct ib_mr *ibmr);
struct ib_mr *mlx5_ib_create_mr(struct ib_pd *pd,
				struct ib_mr_init_attr *mr_init_attr);
struct ib_mr *mlx5_ib_alloc_fast_reg_mr(struct ib_pd *pd,
					int max_page_list_len);
struct ib_fast_reg_page_list *mlx5_ib_alloc_fast_reg_page_list(struct ib_device *ibdev,
							       int page_list_len);
void mlx5_ib_free_fast_reg_page_list(struct ib_fast_reg_page_list *page_list);
struct ib_fmr *mlx5_ib_fmr_alloc(struct ib_pd *pd, int acc,
				 struct ib_fmr_attr *fmr_attr);
int mlx5_ib_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
		      int npages, u64 iova);
int mlx5_ib_unmap_fmr(struct list_head *fmr_list);
int mlx5_ib_fmr_dealloc(struct ib_fmr *ibfmr);
int mlx5_ib_process_mad(struct ib_device *ibdev, int mad_flags, u8 port_num,
			struct ib_wc *in_wc, struct ib_grh *in_grh,
			struct ib_mad *in_mad, struct ib_mad *out_mad);
struct ib_xrcd *mlx5_ib_alloc_xrcd(struct ib_device *ibdev,
					  struct ib_ucontext *context,
					  struct ib_udata *udata);
int mlx5_ib_dealloc_xrcd(struct ib_xrcd *xrcd);
int mlx5_vector2eqn(struct mlx5_ib_dev *dev, int vector, int *eqn, int *irqn);
int mlx5_ib_get_buf_offset(u64 addr, int page_shift, u32 *offset);
int mlx5_query_ext_port_caps(struct mlx5_ib_dev *dev, u8 port);
int mlx5_ib_query_port(struct ib_device *ibdev, u8 port,
		       struct ib_port_attr *props);
int mlx5_ib_init_fmr(struct mlx5_ib_dev *dev);
void mlx5_ib_cleanup_fmr(struct mlx5_ib_dev *dev);
void mlx5_ib_cont_pages(struct ib_umem *umem, u64 addr, int *count, int *shift,
			int *ncont, int *order);
void __mlx5_ib_populate_pas(struct mlx5_ib_dev *dev, struct ib_umem *umem,
			    int page_shift, size_t offset, size_t num_pages,
			    __be64 *pas, int access_flags);
void mlx5_ib_populate_pas(struct mlx5_ib_dev *dev, struct ib_umem *umem,
			  int page_shift, __be64 *pas, int access_flags);
void mlx5_ib_copy_pas(u64 *old, u64 *new, int step, int num);
int mlx5_ib_get_cqe_size(struct mlx5_ib_dev *dev, struct ib_cq *ibcq);
int mlx5_mr_cache_init(struct mlx5_ib_dev *dev);
int mlx5_mr_cache_cleanup(struct mlx5_ib_dev *dev);
int mlx5_mr_ib_cont_pages(struct ib_umem *umem, u64 addr, int *count, int *shift);
void mlx5_umr_cq_handler(struct ib_cq *cq, void *cq_context);
int mlx5_ib_check_mr_status(struct ib_mr *ibmr, u32 check_mask,
			    struct ib_mr_status *mr_status);

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
extern struct workqueue_struct *mlx5_ib_page_fault_wq;

int mlx5_ib_internal_query_odp_caps(struct mlx5_ib_dev *dev);
void mlx5_ib_mr_pfault_handler(struct mlx5_ib_qp *qp,
			       struct mlx5_ib_pfault *pfault);
void mlx5_ib_odp_create_qp(struct mlx5_ib_qp *qp);
int mlx5_ib_odp_init_one(struct mlx5_ib_dev *ibdev);
void mlx5_ib_odp_remove_one(struct mlx5_ib_dev *ibdev);
int __init mlx5_ib_odp_init(void);
void mlx5_ib_odp_cleanup(void);
void mlx5_ib_qp_disable_pagefaults(struct mlx5_ib_qp *qp);
void mlx5_ib_qp_enable_pagefaults(struct mlx5_ib_qp *qp);
void mlx5_ib_invalidate_range(struct ib_umem *umem, unsigned long start,
			      unsigned long end);

#else /* CONFIG_INFINIBAND_ON_DEMAND_PAGING */
static inline int mlx5_ib_internal_query_odp_caps(struct mlx5_ib_dev *dev)
{
	return 0;
}

static inline void mlx5_ib_odp_create_qp(struct mlx5_ib_qp *qp)		{}
static inline int mlx5_ib_odp_init_one(struct mlx5_ib_dev *ibdev) { return 0; }
static inline void mlx5_ib_odp_remove_one(struct mlx5_ib_dev *ibdev)	{}
static inline int mlx5_ib_odp_init(void) { return 0; }
static inline void mlx5_ib_odp_cleanup(void)				{}
static inline void mlx5_ib_qp_disable_pagefaults(struct mlx5_ib_qp *qp) {}
static inline void mlx5_ib_qp_enable_pagefaults(struct mlx5_ib_qp *qp)  {}

#endif /* CONFIG_INFINIBAND_ON_DEMAND_PAGING */

static inline void init_query_mad(struct ib_smp *mad)
{
	mad->base_version  = 1;
	mad->mgmt_class    = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	mad->class_version = 1;
	mad->method	   = IB_MGMT_METHOD_GET;
}

static inline u8 convert_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_ATOMIC ? MLX5_PERM_ATOMIC       : 0) |
	       (acc & IB_ACCESS_REMOTE_WRITE  ? MLX5_PERM_REMOTE_WRITE : 0) |
	       (acc & IB_ACCESS_REMOTE_READ   ? MLX5_PERM_REMOTE_READ  : 0) |
	       (acc & IB_ACCESS_LOCAL_WRITE   ? MLX5_PERM_LOCAL_WRITE  : 0) |
	       MLX5_PERM_LOCAL_READ;
}

#define MLX5_MAX_UMR_SHIFT 16
#define MLX5_MAX_UMR_PAGES (1 << MLX5_MAX_UMR_SHIFT)

#endif /* MLX5_IB_H */
