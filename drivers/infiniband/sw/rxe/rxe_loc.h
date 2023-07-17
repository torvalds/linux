/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#ifndef RXE_LOC_H
#define RXE_LOC_H

/* rxe_av.c */
void rxe_init_av(struct rdma_ah_attr *attr, struct rxe_av *av);
int rxe_av_chk_attr(struct rxe_qp *qp, struct rdma_ah_attr *attr);
int rxe_ah_chk_attr(struct rxe_ah *ah, struct rdma_ah_attr *attr);
void rxe_av_from_attr(u8 port_num, struct rxe_av *av,
		     struct rdma_ah_attr *attr);
void rxe_av_to_attr(struct rxe_av *av, struct rdma_ah_attr *attr);
void rxe_av_fill_ip_info(struct rxe_av *av, struct rdma_ah_attr *attr);
struct rxe_av *rxe_get_av(struct rxe_pkt_info *pkt, struct rxe_ah **ahp);

/* rxe_cq.c */
int rxe_cq_chk_attr(struct rxe_dev *rxe, struct rxe_cq *cq,
		    int cqe, int comp_vector);

int rxe_cq_from_init(struct rxe_dev *rxe, struct rxe_cq *cq, int cqe,
		     int comp_vector, struct ib_udata *udata,
		     struct rxe_create_cq_resp __user *uresp);

int rxe_cq_resize_queue(struct rxe_cq *cq, int new_cqe,
			struct rxe_resize_cq_resp __user *uresp,
			struct ib_udata *udata);

int rxe_cq_post(struct rxe_cq *cq, struct rxe_cqe *cqe, int solicited);

void rxe_cq_disable(struct rxe_cq *cq);

void rxe_cq_cleanup(struct rxe_pool_elem *elem);

/* rxe_mcast.c */
struct rxe_mcg *rxe_lookup_mcg(struct rxe_dev *rxe, union ib_gid *mgid);
int rxe_attach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid);
int rxe_detach_mcast(struct ib_qp *ibqp, union ib_gid *mgid, u16 mlid);
void rxe_cleanup_mcg(struct kref *kref);

/* rxe_mmap.c */
struct rxe_mmap_info {
	struct list_head	pending_mmaps;
	struct ib_ucontext	*context;
	struct kref		ref;
	void			*obj;

	struct mminfo info;
};

void rxe_mmap_release(struct kref *ref);

struct rxe_mmap_info *rxe_create_mmap_info(struct rxe_dev *dev, u32 size,
					   struct ib_udata *udata, void *obj);

int rxe_mmap(struct ib_ucontext *context, struct vm_area_struct *vma);

/* rxe_mr.c */
u8 rxe_get_next_key(u32 last_key);
void rxe_mr_init_dma(int access, struct rxe_mr *mr);
int rxe_mr_init_user(struct rxe_dev *rxe, u64 start, u64 length, u64 iova,
		     int access, struct rxe_mr *mr);
int rxe_mr_init_fast(int max_pages, struct rxe_mr *mr);
int rxe_flush_pmem_iova(struct rxe_mr *mr, u64 iova, unsigned int length);
int rxe_mr_copy(struct rxe_mr *mr, u64 iova, void *addr,
		unsigned int length, enum rxe_mr_copy_dir dir);
int copy_data(struct rxe_pd *pd, int access, struct rxe_dma_info *dma,
	      void *addr, int length, enum rxe_mr_copy_dir dir);
int rxe_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
		  int sg_nents, unsigned int *sg_offset);
int rxe_mr_do_atomic_op(struct rxe_mr *mr, u64 iova, int opcode,
			u64 compare, u64 swap_add, u64 *orig_val);
int rxe_mr_do_atomic_write(struct rxe_mr *mr, u64 iova, u64 value);
struct rxe_mr *lookup_mr(struct rxe_pd *pd, int access, u32 key,
			 enum rxe_mr_lookup_type type);
int mr_check_range(struct rxe_mr *mr, u64 iova, size_t length);
int advance_dma_data(struct rxe_dma_info *dma, unsigned int length);
int rxe_invalidate_mr(struct rxe_qp *qp, u32 key);
int rxe_reg_fast_mr(struct rxe_qp *qp, struct rxe_send_wqe *wqe);
void rxe_mr_cleanup(struct rxe_pool_elem *elem);

/* rxe_mw.c */
int rxe_alloc_mw(struct ib_mw *ibmw, struct ib_udata *udata);
int rxe_dealloc_mw(struct ib_mw *ibmw);
int rxe_bind_mw(struct rxe_qp *qp, struct rxe_send_wqe *wqe);
int rxe_invalidate_mw(struct rxe_qp *qp, u32 rkey);
struct rxe_mw *rxe_lookup_mw(struct rxe_qp *qp, int access, u32 rkey);
void rxe_mw_cleanup(struct rxe_pool_elem *elem);

/* rxe_net.c */
struct sk_buff *rxe_init_packet(struct rxe_dev *rxe, struct rxe_av *av,
				int paylen, struct rxe_pkt_info *pkt);
int rxe_prepare(struct rxe_av *av, struct rxe_pkt_info *pkt,
		struct sk_buff *skb);
int rxe_xmit_packet(struct rxe_qp *qp, struct rxe_pkt_info *pkt,
		    struct sk_buff *skb);
const char *rxe_parent_name(struct rxe_dev *rxe, unsigned int port_num);

/* rxe_qp.c */
int rxe_qp_chk_init(struct rxe_dev *rxe, struct ib_qp_init_attr *init);
int rxe_qp_from_init(struct rxe_dev *rxe, struct rxe_qp *qp, struct rxe_pd *pd,
		     struct ib_qp_init_attr *init,
		     struct rxe_create_qp_resp __user *uresp,
		     struct ib_pd *ibpd, struct ib_udata *udata);
int rxe_qp_to_init(struct rxe_qp *qp, struct ib_qp_init_attr *init);
int rxe_qp_chk_attr(struct rxe_dev *rxe, struct rxe_qp *qp,
		    struct ib_qp_attr *attr, int mask);
int rxe_qp_from_attr(struct rxe_qp *qp, struct ib_qp_attr *attr,
		     int mask, struct ib_udata *udata);
int rxe_qp_to_attr(struct rxe_qp *qp, struct ib_qp_attr *attr, int mask);
void rxe_qp_error(struct rxe_qp *qp);
int rxe_qp_chk_destroy(struct rxe_qp *qp);
void rxe_qp_cleanup(struct rxe_pool_elem *elem);

static inline int qp_num(struct rxe_qp *qp)
{
	return qp->ibqp.qp_num;
}

static inline enum ib_qp_type qp_type(struct rxe_qp *qp)
{
	return qp->ibqp.qp_type;
}

static inline enum ib_qp_state qp_state(struct rxe_qp *qp)
{
	return qp->attr.qp_state;
}

static inline int qp_mtu(struct rxe_qp *qp)
{
	if (qp->ibqp.qp_type == IB_QPT_RC || qp->ibqp.qp_type == IB_QPT_UC)
		return qp->attr.path_mtu;
	else
		return IB_MTU_4096;
}

static inline int rcv_wqe_size(int max_sge)
{
	return sizeof(struct rxe_recv_wqe) +
		max_sge * sizeof(struct ib_sge);
}

void free_rd_atomic_resource(struct resp_res *res);

static inline void rxe_advance_resp_resource(struct rxe_qp *qp)
{
	qp->resp.res_head++;
	if (unlikely(qp->resp.res_head == qp->attr.max_dest_rd_atomic))
		qp->resp.res_head = 0;
}

void retransmit_timer(struct timer_list *t);
void rnr_nak_timer(struct timer_list *t);

/* rxe_srq.c */
int rxe_srq_chk_init(struct rxe_dev *rxe, struct ib_srq_init_attr *init);
int rxe_srq_from_init(struct rxe_dev *rxe, struct rxe_srq *srq,
		      struct ib_srq_init_attr *init, struct ib_udata *udata,
		      struct rxe_create_srq_resp __user *uresp);
int rxe_srq_chk_attr(struct rxe_dev *rxe, struct rxe_srq *srq,
		     struct ib_srq_attr *attr, enum ib_srq_attr_mask mask);
int rxe_srq_from_attr(struct rxe_dev *rxe, struct rxe_srq *srq,
		      struct ib_srq_attr *attr, enum ib_srq_attr_mask mask,
		      struct rxe_modify_srq_cmd *ucmd, struct ib_udata *udata);
void rxe_srq_cleanup(struct rxe_pool_elem *elem);

void rxe_dealloc(struct ib_device *ib_dev);

int rxe_completer(struct rxe_qp *qp);
int rxe_requester(struct rxe_qp *qp);
int rxe_responder(struct rxe_qp *qp);

/* rxe_icrc.c */
int rxe_icrc_init(struct rxe_dev *rxe);
int rxe_icrc_check(struct sk_buff *skb, struct rxe_pkt_info *pkt);
void rxe_icrc_generate(struct sk_buff *skb, struct rxe_pkt_info *pkt);

void rxe_resp_queue_pkt(struct rxe_qp *qp, struct sk_buff *skb);

void rxe_comp_queue_pkt(struct rxe_qp *qp, struct sk_buff *skb);

static inline unsigned int wr_opcode_mask(int opcode, struct rxe_qp *qp)
{
	return rxe_wr_opcode_info[opcode].mask[qp->ibqp.qp_type];
}

#endif /* RXE_LOC_H */
