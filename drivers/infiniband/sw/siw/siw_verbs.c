// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/xarray.h>
#include <net/addrconf.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/uverbs_ioctl.h>

#include "siw.h"
#include "siw_verbs.h"
#include "siw_mem.h"

static int siw_qp_state_to_ib_qp_state[SIW_QP_STATE_COUNT] = {
	[SIW_QP_STATE_IDLE] = IB_QPS_INIT,
	[SIW_QP_STATE_RTR] = IB_QPS_RTR,
	[SIW_QP_STATE_RTS] = IB_QPS_RTS,
	[SIW_QP_STATE_CLOSING] = IB_QPS_SQD,
	[SIW_QP_STATE_TERMINATE] = IB_QPS_SQE,
	[SIW_QP_STATE_ERROR] = IB_QPS_ERR
};

static int ib_qp_state_to_siw_qp_state[IB_QPS_ERR + 1] = {
	[IB_QPS_RESET] = SIW_QP_STATE_IDLE,
	[IB_QPS_INIT] = SIW_QP_STATE_IDLE,
	[IB_QPS_RTR] = SIW_QP_STATE_RTR,
	[IB_QPS_RTS] = SIW_QP_STATE_RTS,
	[IB_QPS_SQD] = SIW_QP_STATE_CLOSING,
	[IB_QPS_SQE] = SIW_QP_STATE_TERMINATE,
	[IB_QPS_ERR] = SIW_QP_STATE_ERROR
};

static char ib_qp_state_to_string[IB_QPS_ERR + 1][sizeof("RESET")] = {
	[IB_QPS_RESET] = "RESET", [IB_QPS_INIT] = "INIT", [IB_QPS_RTR] = "RTR",
	[IB_QPS_RTS] = "RTS",     [IB_QPS_SQD] = "SQD",   [IB_QPS_SQE] = "SQE",
	[IB_QPS_ERR] = "ERR"
};

void siw_mmap_free(struct rdma_user_mmap_entry *rdma_entry)
{
	struct siw_user_mmap_entry *entry = to_siw_mmap_entry(rdma_entry);

	kfree(entry);
}

int siw_mmap(struct ib_ucontext *ctx, struct vm_area_struct *vma)
{
	struct siw_ucontext *uctx = to_siw_ctx(ctx);
	size_t size = vma->vm_end - vma->vm_start;
	struct rdma_user_mmap_entry *rdma_entry;
	struct siw_user_mmap_entry *entry;
	int rv = -EINVAL;

	/*
	 * Must be page aligned
	 */
	if (vma->vm_start & (PAGE_SIZE - 1)) {
		pr_warn("siw: mmap not page aligned\n");
		return -EINVAL;
	}
	rdma_entry = rdma_user_mmap_entry_get(&uctx->base_ucontext, vma);
	if (!rdma_entry) {
		siw_dbg(&uctx->sdev->base_dev, "mmap lookup failed: %lu, %#zx\n",
			vma->vm_pgoff, size);
		return -EINVAL;
	}
	entry = to_siw_mmap_entry(rdma_entry);

	rv = remap_vmalloc_range(vma, entry->address, 0);
	if (rv)
		pr_warn("remap_vmalloc_range failed: %lu, %zu\n", vma->vm_pgoff,
			size);
	rdma_user_mmap_entry_put(rdma_entry);

	return rv;
}

int siw_alloc_ucontext(struct ib_ucontext *base_ctx, struct ib_udata *udata)
{
	struct siw_device *sdev = to_siw_dev(base_ctx->device);
	struct siw_ucontext *ctx = to_siw_ctx(base_ctx);
	struct siw_uresp_alloc_ctx uresp = {};
	int rv;

	if (atomic_inc_return(&sdev->num_ctx) > SIW_MAX_CONTEXT) {
		rv = -ENOMEM;
		goto err_out;
	}
	ctx->sdev = sdev;

	uresp.dev_id = sdev->vendor_part_id;

	if (udata->outlen < sizeof(uresp)) {
		rv = -EINVAL;
		goto err_out;
	}
	rv = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (rv)
		goto err_out;

	siw_dbg(base_ctx->device, "success. now %d context(s)\n",
		atomic_read(&sdev->num_ctx));

	return 0;

err_out:
	atomic_dec(&sdev->num_ctx);
	siw_dbg(base_ctx->device, "failure %d. now %d context(s)\n", rv,
		atomic_read(&sdev->num_ctx));

	return rv;
}

void siw_dealloc_ucontext(struct ib_ucontext *base_ctx)
{
	struct siw_ucontext *uctx = to_siw_ctx(base_ctx);

	atomic_dec(&uctx->sdev->num_ctx);
}

int siw_query_device(struct ib_device *base_dev, struct ib_device_attr *attr,
		     struct ib_udata *udata)
{
	struct siw_device *sdev = to_siw_dev(base_dev);

	if (udata->inlen || udata->outlen)
		return -EINVAL;

	memset(attr, 0, sizeof(*attr));

	/* Revisit atomic caps if RFC 7306 gets supported */
	attr->atomic_cap = 0;
	attr->device_cap_flags = IB_DEVICE_MEM_MGT_EXTENSIONS;
	attr->kernel_cap_flags = IBK_ALLOW_USER_UNREG;
	attr->max_cq = sdev->attrs.max_cq;
	attr->max_cqe = sdev->attrs.max_cqe;
	attr->max_fast_reg_page_list_len = SIW_MAX_SGE_PBL;
	attr->max_mr = sdev->attrs.max_mr;
	attr->max_mw = sdev->attrs.max_mw;
	attr->max_mr_size = ~0ull;
	attr->max_pd = sdev->attrs.max_pd;
	attr->max_qp = sdev->attrs.max_qp;
	attr->max_qp_init_rd_atom = sdev->attrs.max_ird;
	attr->max_qp_rd_atom = sdev->attrs.max_ord;
	attr->max_qp_wr = sdev->attrs.max_qp_wr;
	attr->max_recv_sge = sdev->attrs.max_sge;
	attr->max_res_rd_atom = sdev->attrs.max_qp * sdev->attrs.max_ird;
	attr->max_send_sge = sdev->attrs.max_sge;
	attr->max_sge_rd = sdev->attrs.max_sge_rd;
	attr->max_srq = sdev->attrs.max_srq;
	attr->max_srq_sge = sdev->attrs.max_srq_sge;
	attr->max_srq_wr = sdev->attrs.max_srq_wr;
	attr->page_size_cap = PAGE_SIZE;
	attr->vendor_id = SIW_VENDOR_ID;
	attr->vendor_part_id = sdev->vendor_part_id;

	addrconf_addr_eui48((u8 *)&attr->sys_image_guid,
			    sdev->raw_gid);

	return 0;
}

int siw_query_port(struct ib_device *base_dev, u32 port,
		   struct ib_port_attr *attr)
{
	struct siw_device *sdev = to_siw_dev(base_dev);
	int rv;

	memset(attr, 0, sizeof(*attr));

	rv = ib_get_eth_speed(base_dev, port, &attr->active_speed,
			 &attr->active_width);
	attr->gid_tbl_len = 1;
	attr->max_msg_sz = -1;
	attr->max_mtu = ib_mtu_int_to_enum(sdev->netdev->mtu);
	attr->active_mtu = ib_mtu_int_to_enum(sdev->netdev->mtu);
	attr->phys_state = sdev->state == IB_PORT_ACTIVE ?
		IB_PORT_PHYS_STATE_LINK_UP : IB_PORT_PHYS_STATE_DISABLED;
	attr->port_cap_flags = IB_PORT_CM_SUP | IB_PORT_DEVICE_MGMT_SUP;
	attr->state = sdev->state;
	/*
	 * All zero
	 *
	 * attr->lid = 0;
	 * attr->bad_pkey_cntr = 0;
	 * attr->qkey_viol_cntr = 0;
	 * attr->sm_lid = 0;
	 * attr->lmc = 0;
	 * attr->max_vl_num = 0;
	 * attr->sm_sl = 0;
	 * attr->subnet_timeout = 0;
	 * attr->init_type_repy = 0;
	 */
	return rv;
}

int siw_get_port_immutable(struct ib_device *base_dev, u32 port,
			   struct ib_port_immutable *port_immutable)
{
	struct ib_port_attr attr;
	int rv = siw_query_port(base_dev, port, &attr);

	if (rv)
		return rv;

	port_immutable->gid_tbl_len = attr.gid_tbl_len;
	port_immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;

	return 0;
}

int siw_query_gid(struct ib_device *base_dev, u32 port, int idx,
		  union ib_gid *gid)
{
	struct siw_device *sdev = to_siw_dev(base_dev);

	/* subnet_prefix == interface_id == 0; */
	memset(gid, 0, sizeof(*gid));
	memcpy(gid->raw, sdev->raw_gid, ETH_ALEN);

	return 0;
}

int siw_alloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
	struct siw_device *sdev = to_siw_dev(pd->device);

	if (atomic_inc_return(&sdev->num_pd) > SIW_MAX_PD) {
		atomic_dec(&sdev->num_pd);
		return -ENOMEM;
	}
	siw_dbg_pd(pd, "now %d PD's(s)\n", atomic_read(&sdev->num_pd));

	return 0;
}

int siw_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
	struct siw_device *sdev = to_siw_dev(pd->device);

	siw_dbg_pd(pd, "free PD\n");
	atomic_dec(&sdev->num_pd);
	return 0;
}

void siw_qp_get_ref(struct ib_qp *base_qp)
{
	siw_qp_get(to_siw_qp(base_qp));
}

void siw_qp_put_ref(struct ib_qp *base_qp)
{
	siw_qp_put(to_siw_qp(base_qp));
}

static struct rdma_user_mmap_entry *
siw_mmap_entry_insert(struct siw_ucontext *uctx,
		      void *address, size_t length,
		      u64 *offset)
{
	struct siw_user_mmap_entry *entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	int rv;

	*offset = SIW_INVAL_UOBJ_KEY;
	if (!entry)
		return NULL;

	entry->address = address;

	rv = rdma_user_mmap_entry_insert(&uctx->base_ucontext,
					 &entry->rdma_entry,
					 length);
	if (rv) {
		kfree(entry);
		return NULL;
	}

	*offset = rdma_user_mmap_get_offset(&entry->rdma_entry);

	return &entry->rdma_entry;
}

/*
 * siw_create_qp()
 *
 * Create QP of requested size on given device.
 *
 * @qp:		Queue pait
 * @attrs:	Initial QP attributes.
 * @udata:	used to provide QP ID, SQ and RQ size back to user.
 */

int siw_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *attrs,
		  struct ib_udata *udata)
{
	struct ib_pd *pd = ibqp->pd;
	struct siw_qp *qp = to_siw_qp(ibqp);
	struct ib_device *base_dev = pd->device;
	struct siw_device *sdev = to_siw_dev(base_dev);
	struct siw_ucontext *uctx =
		rdma_udata_to_drv_context(udata, struct siw_ucontext,
					  base_ucontext);
	unsigned long flags;
	int num_sqe, num_rqe, rv = 0;
	size_t length;

	siw_dbg(base_dev, "create new QP\n");

	if (attrs->create_flags)
		return -EOPNOTSUPP;

	if (atomic_inc_return(&sdev->num_qp) > SIW_MAX_QP) {
		siw_dbg(base_dev, "too many QP's\n");
		rv = -ENOMEM;
		goto err_atomic;
	}
	if (attrs->qp_type != IB_QPT_RC) {
		siw_dbg(base_dev, "only RC QP's supported\n");
		rv = -EOPNOTSUPP;
		goto err_atomic;
	}
	if ((attrs->cap.max_send_wr > SIW_MAX_QP_WR) ||
	    (attrs->cap.max_recv_wr > SIW_MAX_QP_WR) ||
	    (attrs->cap.max_send_sge > SIW_MAX_SGE) ||
	    (attrs->cap.max_recv_sge > SIW_MAX_SGE)) {
		siw_dbg(base_dev, "QP size error\n");
		rv = -EINVAL;
		goto err_atomic;
	}
	if (attrs->cap.max_inline_data > SIW_MAX_INLINE) {
		siw_dbg(base_dev, "max inline send: %d > %d\n",
			attrs->cap.max_inline_data, (int)SIW_MAX_INLINE);
		rv = -EINVAL;
		goto err_atomic;
	}
	/*
	 * NOTE: we don't allow for a QP unable to hold any SQ WQE
	 */
	if (attrs->cap.max_send_wr == 0) {
		siw_dbg(base_dev, "QP must have send queue\n");
		rv = -EINVAL;
		goto err_atomic;
	}

	if (!attrs->send_cq || (!attrs->recv_cq && !attrs->srq)) {
		siw_dbg(base_dev, "send CQ or receive CQ invalid\n");
		rv = -EINVAL;
		goto err_atomic;
	}

	init_rwsem(&qp->state_lock);
	spin_lock_init(&qp->sq_lock);
	spin_lock_init(&qp->rq_lock);
	spin_lock_init(&qp->orq_lock);

	rv = siw_qp_add(sdev, qp);
	if (rv)
		goto err_atomic;


	/* All queue indices are derived from modulo operations
	 * on a free running 'get' (consumer) and 'put' (producer)
	 * unsigned counter. Having queue sizes at power of two
	 * avoids handling counter wrap around.
	 */
	num_sqe = roundup_pow_of_two(attrs->cap.max_send_wr);
	num_rqe = attrs->cap.max_recv_wr;
	if (num_rqe)
		num_rqe = roundup_pow_of_two(num_rqe);

	if (udata)
		qp->sendq = vmalloc_user(num_sqe * sizeof(struct siw_sqe));
	else
		qp->sendq = vcalloc(num_sqe, sizeof(struct siw_sqe));

	if (qp->sendq == NULL) {
		rv = -ENOMEM;
		goto err_out_xa;
	}
	if (attrs->sq_sig_type != IB_SIGNAL_REQ_WR) {
		if (attrs->sq_sig_type == IB_SIGNAL_ALL_WR)
			qp->attrs.flags |= SIW_SIGNAL_ALL_WR;
		else {
			rv = -EINVAL;
			goto err_out_xa;
		}
	}
	qp->pd = pd;
	qp->scq = to_siw_cq(attrs->send_cq);
	qp->rcq = to_siw_cq(attrs->recv_cq);

	if (attrs->srq) {
		/*
		 * SRQ support.
		 * Verbs 6.3.7: ignore RQ size, if SRQ present
		 * Verbs 6.3.5: do not check PD of SRQ against PD of QP
		 */
		qp->srq = to_siw_srq(attrs->srq);
		qp->attrs.rq_size = 0;
		siw_dbg(base_dev, "QP [%u]: SRQ attached\n",
			qp->base_qp.qp_num);
	} else if (num_rqe) {
		if (udata)
			qp->recvq =
				vmalloc_user(num_rqe * sizeof(struct siw_rqe));
		else
			qp->recvq = vcalloc(num_rqe, sizeof(struct siw_rqe));

		if (qp->recvq == NULL) {
			rv = -ENOMEM;
			goto err_out_xa;
		}
		qp->attrs.rq_size = num_rqe;
	}
	qp->attrs.sq_size = num_sqe;
	qp->attrs.sq_max_sges = attrs->cap.max_send_sge;
	qp->attrs.rq_max_sges = attrs->cap.max_recv_sge;

	/* Make those two tunables fixed for now. */
	qp->tx_ctx.gso_seg_limit = 1;
	qp->tx_ctx.zcopy_tx = zcopy_tx;

	qp->attrs.state = SIW_QP_STATE_IDLE;

	if (udata) {
		struct siw_uresp_create_qp uresp = {};

		uresp.num_sqe = num_sqe;
		uresp.num_rqe = num_rqe;
		uresp.qp_id = qp_id(qp);

		if (qp->sendq) {
			length = num_sqe * sizeof(struct siw_sqe);
			qp->sq_entry =
				siw_mmap_entry_insert(uctx, qp->sendq,
						      length, &uresp.sq_key);
			if (!qp->sq_entry) {
				rv = -ENOMEM;
				goto err_out_xa;
			}
		}

		if (qp->recvq) {
			length = num_rqe * sizeof(struct siw_rqe);
			qp->rq_entry =
				siw_mmap_entry_insert(uctx, qp->recvq,
						      length, &uresp.rq_key);
			if (!qp->rq_entry) {
				uresp.sq_key = SIW_INVAL_UOBJ_KEY;
				rv = -ENOMEM;
				goto err_out_xa;
			}
		}

		if (udata->outlen < sizeof(uresp)) {
			rv = -EINVAL;
			goto err_out_xa;
		}
		rv = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (rv)
			goto err_out_xa;
	}
	qp->tx_cpu = siw_get_tx_cpu(sdev);
	if (qp->tx_cpu < 0) {
		rv = -EINVAL;
		goto err_out_xa;
	}
	INIT_LIST_HEAD(&qp->devq);
	spin_lock_irqsave(&sdev->lock, flags);
	list_add_tail(&qp->devq, &sdev->qp_list);
	spin_unlock_irqrestore(&sdev->lock, flags);

	init_completion(&qp->qp_free);

	return 0;

err_out_xa:
	xa_erase(&sdev->qp_xa, qp_id(qp));
	if (uctx) {
		rdma_user_mmap_entry_remove(qp->sq_entry);
		rdma_user_mmap_entry_remove(qp->rq_entry);
	}
	vfree(qp->sendq);
	vfree(qp->recvq);

err_atomic:
	atomic_dec(&sdev->num_qp);
	return rv;
}

/*
 * Minimum siw_query_qp() verb interface.
 *
 * @qp_attr_mask is not used but all available information is provided
 */
int siw_query_qp(struct ib_qp *base_qp, struct ib_qp_attr *qp_attr,
		 int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	struct siw_qp *qp;
	struct siw_device *sdev;

	if (base_qp && qp_attr && qp_init_attr) {
		qp = to_siw_qp(base_qp);
		sdev = to_siw_dev(base_qp->device);
	} else {
		return -EINVAL;
	}
	qp_attr->qp_state = siw_qp_state_to_ib_qp_state[qp->attrs.state];
	qp_attr->cap.max_inline_data = SIW_MAX_INLINE;
	qp_attr->cap.max_send_wr = qp->attrs.sq_size;
	qp_attr->cap.max_send_sge = qp->attrs.sq_max_sges;
	qp_attr->cap.max_recv_wr = qp->attrs.rq_size;
	qp_attr->cap.max_recv_sge = qp->attrs.rq_max_sges;
	qp_attr->path_mtu = ib_mtu_int_to_enum(sdev->netdev->mtu);
	qp_attr->max_rd_atomic = qp->attrs.irq_size;
	qp_attr->max_dest_rd_atomic = qp->attrs.orq_size;

	qp_attr->qp_access_flags = IB_ACCESS_LOCAL_WRITE |
				   IB_ACCESS_REMOTE_WRITE |
				   IB_ACCESS_REMOTE_READ;

	qp_init_attr->qp_type = base_qp->qp_type;
	qp_init_attr->send_cq = base_qp->send_cq;
	qp_init_attr->recv_cq = base_qp->recv_cq;
	qp_init_attr->srq = base_qp->srq;

	qp_init_attr->cap = qp_attr->cap;

	return 0;
}

int siw_verbs_modify_qp(struct ib_qp *base_qp, struct ib_qp_attr *attr,
			int attr_mask, struct ib_udata *udata)
{
	struct siw_qp_attrs new_attrs;
	enum siw_qp_attr_mask siw_attr_mask = 0;
	struct siw_qp *qp = to_siw_qp(base_qp);
	int rv = 0;

	if (!attr_mask)
		return 0;

	if (attr_mask & ~IB_QP_ATTR_STANDARD_BITS)
		return -EOPNOTSUPP;

	memset(&new_attrs, 0, sizeof(new_attrs));

	if (attr_mask & IB_QP_ACCESS_FLAGS) {
		siw_attr_mask = SIW_QP_ATTR_ACCESS_FLAGS;

		if (attr->qp_access_flags & IB_ACCESS_REMOTE_READ)
			new_attrs.flags |= SIW_RDMA_READ_ENABLED;
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_WRITE)
			new_attrs.flags |= SIW_RDMA_WRITE_ENABLED;
		if (attr->qp_access_flags & IB_ACCESS_MW_BIND)
			new_attrs.flags |= SIW_RDMA_BIND_ENABLED;
	}
	if (attr_mask & IB_QP_STATE) {
		siw_dbg_qp(qp, "desired IB QP state: %s\n",
			   ib_qp_state_to_string[attr->qp_state]);

		new_attrs.state = ib_qp_state_to_siw_qp_state[attr->qp_state];

		if (new_attrs.state > SIW_QP_STATE_RTS)
			qp->tx_ctx.tx_suspend = 1;

		siw_attr_mask |= SIW_QP_ATTR_STATE;
	}
	if (!siw_attr_mask)
		goto out;

	down_write(&qp->state_lock);

	rv = siw_qp_modify(qp, &new_attrs, siw_attr_mask);

	up_write(&qp->state_lock);
out:
	return rv;
}

int siw_destroy_qp(struct ib_qp *base_qp, struct ib_udata *udata)
{
	struct siw_qp *qp = to_siw_qp(base_qp);
	struct siw_ucontext *uctx =
		rdma_udata_to_drv_context(udata, struct siw_ucontext,
					  base_ucontext);
	struct siw_qp_attrs qp_attrs;

	siw_dbg_qp(qp, "state %d\n", qp->attrs.state);

	/*
	 * Mark QP as in process of destruction to prevent from
	 * any async callbacks to RDMA core
	 */
	qp->attrs.flags |= SIW_QP_IN_DESTROY;
	qp->rx_stream.rx_suspend = 1;

	if (uctx) {
		rdma_user_mmap_entry_remove(qp->sq_entry);
		rdma_user_mmap_entry_remove(qp->rq_entry);
	}

	down_write(&qp->state_lock);

	qp_attrs.state = SIW_QP_STATE_ERROR;
	siw_qp_modify(qp, &qp_attrs, SIW_QP_ATTR_STATE);

	if (qp->cep) {
		siw_cep_put(qp->cep);
		qp->cep = NULL;
	}
	up_write(&qp->state_lock);

	kfree(qp->tx_ctx.mpa_crc_hd);
	kfree(qp->rx_stream.mpa_crc_hd);

	qp->scq = qp->rcq = NULL;

	siw_qp_put(qp);
	wait_for_completion(&qp->qp_free);

	return 0;
}

/*
 * siw_copy_inline_sgl()
 *
 * Prepare sgl of inlined data for sending. For userland callers
 * function checks if given buffer addresses and len's are within
 * process context bounds.
 * Data from all provided sge's are copied together into the wqe,
 * referenced by a single sge.
 */
static int siw_copy_inline_sgl(const struct ib_send_wr *core_wr,
			       struct siw_sqe *sqe)
{
	struct ib_sge *core_sge = core_wr->sg_list;
	void *kbuf = &sqe->sge[1];
	int num_sge = core_wr->num_sge, bytes = 0;

	sqe->sge[0].laddr = (uintptr_t)kbuf;
	sqe->sge[0].lkey = 0;

	while (num_sge--) {
		if (!core_sge->length) {
			core_sge++;
			continue;
		}
		bytes += core_sge->length;
		if (bytes > SIW_MAX_INLINE) {
			bytes = -EINVAL;
			break;
		}
		memcpy(kbuf, ib_virt_dma_to_ptr(core_sge->addr),
		       core_sge->length);

		kbuf += core_sge->length;
		core_sge++;
	}
	sqe->sge[0].length = max(bytes, 0);
	sqe->num_sge = bytes > 0 ? 1 : 0;

	return bytes;
}

/* Complete SQ WR's without processing */
static int siw_sq_flush_wr(struct siw_qp *qp, const struct ib_send_wr *wr,
			   const struct ib_send_wr **bad_wr)
{
	int rv = 0;

	while (wr) {
		struct siw_sqe sqe = {};

		switch (wr->opcode) {
		case IB_WR_RDMA_WRITE:
			sqe.opcode = SIW_OP_WRITE;
			break;
		case IB_WR_RDMA_READ:
			sqe.opcode = SIW_OP_READ;
			break;
		case IB_WR_RDMA_READ_WITH_INV:
			sqe.opcode = SIW_OP_READ_LOCAL_INV;
			break;
		case IB_WR_SEND:
			sqe.opcode = SIW_OP_SEND;
			break;
		case IB_WR_SEND_WITH_IMM:
			sqe.opcode = SIW_OP_SEND_WITH_IMM;
			break;
		case IB_WR_SEND_WITH_INV:
			sqe.opcode = SIW_OP_SEND_REMOTE_INV;
			break;
		case IB_WR_LOCAL_INV:
			sqe.opcode = SIW_OP_INVAL_STAG;
			break;
		case IB_WR_REG_MR:
			sqe.opcode = SIW_OP_REG_MR;
			break;
		default:
			rv = -EINVAL;
			break;
		}
		if (!rv) {
			sqe.id = wr->wr_id;
			rv = siw_sqe_complete(qp, &sqe, 0,
					      SIW_WC_WR_FLUSH_ERR);
		}
		if (rv) {
			if (bad_wr)
				*bad_wr = wr;
			break;
		}
		wr = wr->next;
	}
	return rv;
}

/* Complete RQ WR's without processing */
static int siw_rq_flush_wr(struct siw_qp *qp, const struct ib_recv_wr *wr,
			   const struct ib_recv_wr **bad_wr)
{
	struct siw_rqe rqe = {};
	int rv = 0;

	while (wr) {
		rqe.id = wr->wr_id;
		rv = siw_rqe_complete(qp, &rqe, 0, 0, SIW_WC_WR_FLUSH_ERR);
		if (rv) {
			if (bad_wr)
				*bad_wr = wr;
			break;
		}
		wr = wr->next;
	}
	return rv;
}

/*
 * siw_post_send()
 *
 * Post a list of S-WR's to a SQ.
 *
 * @base_qp:	Base QP contained in siw QP
 * @wr:		Null terminated list of user WR's
 * @bad_wr:	Points to failing WR in case of synchronous failure.
 */
int siw_post_send(struct ib_qp *base_qp, const struct ib_send_wr *wr,
		  const struct ib_send_wr **bad_wr)
{
	struct siw_qp *qp = to_siw_qp(base_qp);
	struct siw_wqe *wqe = tx_wqe(qp);

	unsigned long flags;
	int rv = 0;

	if (wr && !rdma_is_kernel_res(&qp->base_qp.res)) {
		siw_dbg_qp(qp, "wr must be empty for user mapped sq\n");
		*bad_wr = wr;
		return -EINVAL;
	}

	/*
	 * Try to acquire QP state lock. Must be non-blocking
	 * to accommodate kernel clients needs.
	 */
	if (!down_read_trylock(&qp->state_lock)) {
		if (qp->attrs.state == SIW_QP_STATE_ERROR) {
			/*
			 * ERROR state is final, so we can be sure
			 * this state will not change as long as the QP
			 * exists.
			 *
			 * This handles an ib_drain_sq() call with
			 * a concurrent request to set the QP state
			 * to ERROR.
			 */
			rv = siw_sq_flush_wr(qp, wr, bad_wr);
		} else {
			siw_dbg_qp(qp, "QP locked, state %d\n",
				   qp->attrs.state);
			*bad_wr = wr;
			rv = -ENOTCONN;
		}
		return rv;
	}
	if (unlikely(qp->attrs.state != SIW_QP_STATE_RTS)) {
		if (qp->attrs.state == SIW_QP_STATE_ERROR) {
			/*
			 * Immediately flush this WR to CQ, if QP
			 * is in ERROR state. SQ is guaranteed to
			 * be empty, so WR complets in-order.
			 *
			 * Typically triggered by ib_drain_sq().
			 */
			rv = siw_sq_flush_wr(qp, wr, bad_wr);
		} else {
			siw_dbg_qp(qp, "QP out of state %d\n",
				   qp->attrs.state);
			*bad_wr = wr;
			rv = -ENOTCONN;
		}
		up_read(&qp->state_lock);
		return rv;
	}
	spin_lock_irqsave(&qp->sq_lock, flags);

	while (wr) {
		u32 idx = qp->sq_put % qp->attrs.sq_size;
		struct siw_sqe *sqe = &qp->sendq[idx];

		if (sqe->flags) {
			siw_dbg_qp(qp, "sq full\n");
			rv = -ENOMEM;
			break;
		}
		if (wr->num_sge > qp->attrs.sq_max_sges) {
			siw_dbg_qp(qp, "too many sge's: %d\n", wr->num_sge);
			rv = -EINVAL;
			break;
		}
		sqe->id = wr->wr_id;

		if ((wr->send_flags & IB_SEND_SIGNALED) ||
		    (qp->attrs.flags & SIW_SIGNAL_ALL_WR))
			sqe->flags |= SIW_WQE_SIGNALLED;

		if (wr->send_flags & IB_SEND_FENCE)
			sqe->flags |= SIW_WQE_READ_FENCE;

		switch (wr->opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_INV:
			if (wr->send_flags & IB_SEND_SOLICITED)
				sqe->flags |= SIW_WQE_SOLICITED;

			if (!(wr->send_flags & IB_SEND_INLINE)) {
				siw_copy_sgl(wr->sg_list, sqe->sge,
					     wr->num_sge);
				sqe->num_sge = wr->num_sge;
			} else {
				rv = siw_copy_inline_sgl(wr, sqe);
				if (rv <= 0) {
					rv = -EINVAL;
					break;
				}
				sqe->flags |= SIW_WQE_INLINE;
				sqe->num_sge = 1;
			}
			if (wr->opcode == IB_WR_SEND)
				sqe->opcode = SIW_OP_SEND;
			else {
				sqe->opcode = SIW_OP_SEND_REMOTE_INV;
				sqe->rkey = wr->ex.invalidate_rkey;
			}
			break;

		case IB_WR_RDMA_READ_WITH_INV:
		case IB_WR_RDMA_READ:
			/*
			 * iWarp restricts RREAD sink to SGL containing
			 * 1 SGE only. we could relax to SGL with multiple
			 * elements referring the SAME ltag or even sending
			 * a private per-rreq tag referring to a checked
			 * local sgl with MULTIPLE ltag's.
			 */
			if (unlikely(wr->num_sge != 1)) {
				rv = -EINVAL;
				break;
			}
			siw_copy_sgl(wr->sg_list, &sqe->sge[0], 1);
			/*
			 * NOTE: zero length RREAD is allowed!
			 */
			sqe->raddr = rdma_wr(wr)->remote_addr;
			sqe->rkey = rdma_wr(wr)->rkey;
			sqe->num_sge = 1;

			if (wr->opcode == IB_WR_RDMA_READ)
				sqe->opcode = SIW_OP_READ;
			else
				sqe->opcode = SIW_OP_READ_LOCAL_INV;
			break;

		case IB_WR_RDMA_WRITE:
			if (!(wr->send_flags & IB_SEND_INLINE)) {
				siw_copy_sgl(wr->sg_list, &sqe->sge[0],
					     wr->num_sge);
				sqe->num_sge = wr->num_sge;
			} else {
				rv = siw_copy_inline_sgl(wr, sqe);
				if (unlikely(rv < 0)) {
					rv = -EINVAL;
					break;
				}
				sqe->flags |= SIW_WQE_INLINE;
				sqe->num_sge = 1;
			}
			sqe->raddr = rdma_wr(wr)->remote_addr;
			sqe->rkey = rdma_wr(wr)->rkey;
			sqe->opcode = SIW_OP_WRITE;
			break;

		case IB_WR_REG_MR:
			sqe->base_mr = (uintptr_t)reg_wr(wr)->mr;
			sqe->rkey = reg_wr(wr)->key;
			sqe->access = reg_wr(wr)->access & IWARP_ACCESS_MASK;
			sqe->opcode = SIW_OP_REG_MR;
			break;

		case IB_WR_LOCAL_INV:
			sqe->rkey = wr->ex.invalidate_rkey;
			sqe->opcode = SIW_OP_INVAL_STAG;
			break;

		default:
			siw_dbg_qp(qp, "ib wr type %d unsupported\n",
				   wr->opcode);
			rv = -EINVAL;
			break;
		}
		siw_dbg_qp(qp, "opcode %d, flags 0x%x, wr_id 0x%pK\n",
			   sqe->opcode, sqe->flags,
			   (void *)(uintptr_t)sqe->id);

		if (unlikely(rv < 0))
			break;

		/* make SQE only valid after completely written */
		smp_wmb();
		sqe->flags |= SIW_WQE_VALID;

		qp->sq_put++;
		wr = wr->next;
	}

	/*
	 * Send directly if SQ processing is not in progress.
	 * Eventual immediate errors (rv < 0) do not affect the involved
	 * RI resources (Verbs, 8.3.1) and thus do not prevent from SQ
	 * processing, if new work is already pending. But rv must be passed
	 * to caller.
	 */
	if (wqe->wr_status != SIW_WR_IDLE) {
		spin_unlock_irqrestore(&qp->sq_lock, flags);
		goto skip_direct_sending;
	}
	rv = siw_activate_tx(qp);
	spin_unlock_irqrestore(&qp->sq_lock, flags);

	if (rv <= 0)
		goto skip_direct_sending;

	if (rdma_is_kernel_res(&qp->base_qp.res)) {
		rv = siw_sq_start(qp);
	} else {
		qp->tx_ctx.in_syscall = 1;

		if (siw_qp_sq_process(qp) != 0 && !(qp->tx_ctx.tx_suspend))
			siw_qp_cm_drop(qp, 0);

		qp->tx_ctx.in_syscall = 0;
	}
skip_direct_sending:

	up_read(&qp->state_lock);

	if (rv >= 0)
		return 0;
	/*
	 * Immediate error
	 */
	siw_dbg_qp(qp, "error %d\n", rv);

	*bad_wr = wr;
	return rv;
}

/*
 * siw_post_receive()
 *
 * Post a list of R-WR's to a RQ.
 *
 * @base_qp:	Base QP contained in siw QP
 * @wr:		Null terminated list of user WR's
 * @bad_wr:	Points to failing WR in case of synchronous failure.
 */
int siw_post_receive(struct ib_qp *base_qp, const struct ib_recv_wr *wr,
		     const struct ib_recv_wr **bad_wr)
{
	struct siw_qp *qp = to_siw_qp(base_qp);
	unsigned long flags;
	int rv = 0;

	if (qp->srq || qp->attrs.rq_size == 0) {
		*bad_wr = wr;
		return -EINVAL;
	}
	if (!rdma_is_kernel_res(&qp->base_qp.res)) {
		siw_dbg_qp(qp, "no kernel post_recv for user mapped rq\n");
		*bad_wr = wr;
		return -EINVAL;
	}

	/*
	 * Try to acquire QP state lock. Must be non-blocking
	 * to accommodate kernel clients needs.
	 */
	if (!down_read_trylock(&qp->state_lock)) {
		if (qp->attrs.state == SIW_QP_STATE_ERROR) {
			/*
			 * ERROR state is final, so we can be sure
			 * this state will not change as long as the QP
			 * exists.
			 *
			 * This handles an ib_drain_rq() call with
			 * a concurrent request to set the QP state
			 * to ERROR.
			 */
			rv = siw_rq_flush_wr(qp, wr, bad_wr);
		} else {
			siw_dbg_qp(qp, "QP locked, state %d\n",
				   qp->attrs.state);
			*bad_wr = wr;
			rv = -ENOTCONN;
		}
		return rv;
	}
	if (qp->attrs.state > SIW_QP_STATE_RTS) {
		if (qp->attrs.state == SIW_QP_STATE_ERROR) {
			/*
			 * Immediately flush this WR to CQ, if QP
			 * is in ERROR state. RQ is guaranteed to
			 * be empty, so WR complets in-order.
			 *
			 * Typically triggered by ib_drain_rq().
			 */
			rv = siw_rq_flush_wr(qp, wr, bad_wr);
		} else {
			siw_dbg_qp(qp, "QP out of state %d\n",
				   qp->attrs.state);
			*bad_wr = wr;
			rv = -ENOTCONN;
		}
		up_read(&qp->state_lock);
		return rv;
	}
	/*
	 * Serialize potentially multiple producers.
	 * Not needed for single threaded consumer side.
	 */
	spin_lock_irqsave(&qp->rq_lock, flags);

	while (wr) {
		u32 idx = qp->rq_put % qp->attrs.rq_size;
		struct siw_rqe *rqe = &qp->recvq[idx];

		if (rqe->flags) {
			siw_dbg_qp(qp, "RQ full\n");
			rv = -ENOMEM;
			break;
		}
		if (wr->num_sge > qp->attrs.rq_max_sges) {
			siw_dbg_qp(qp, "too many sge's: %d\n", wr->num_sge);
			rv = -EINVAL;
			break;
		}
		rqe->id = wr->wr_id;
		rqe->num_sge = wr->num_sge;
		siw_copy_sgl(wr->sg_list, rqe->sge, wr->num_sge);

		/* make sure RQE is completely written before valid */
		smp_wmb();

		rqe->flags = SIW_WQE_VALID;

		qp->rq_put++;
		wr = wr->next;
	}
	spin_unlock_irqrestore(&qp->rq_lock, flags);

	up_read(&qp->state_lock);

	if (rv < 0) {
		siw_dbg_qp(qp, "error %d\n", rv);
		*bad_wr = wr;
	}
	return rv > 0 ? 0 : rv;
}

int siw_destroy_cq(struct ib_cq *base_cq, struct ib_udata *udata)
{
	struct siw_cq *cq = to_siw_cq(base_cq);
	struct siw_device *sdev = to_siw_dev(base_cq->device);
	struct siw_ucontext *ctx =
		rdma_udata_to_drv_context(udata, struct siw_ucontext,
					  base_ucontext);

	siw_dbg_cq(cq, "free CQ resources\n");

	siw_cq_flush(cq);

	if (ctx)
		rdma_user_mmap_entry_remove(cq->cq_entry);

	atomic_dec(&sdev->num_cq);

	vfree(cq->queue);
	return 0;
}

/*
 * siw_create_cq()
 *
 * Populate CQ of requested size
 *
 * @base_cq: CQ as allocated by RDMA midlayer
 * @attr: Initial CQ attributes
 * @udata: relates to user context
 */

int siw_create_cq(struct ib_cq *base_cq, const struct ib_cq_init_attr *attr,
		  struct ib_udata *udata)
{
	struct siw_device *sdev = to_siw_dev(base_cq->device);
	struct siw_cq *cq = to_siw_cq(base_cq);
	int rv, size = attr->cqe;

	if (attr->flags)
		return -EOPNOTSUPP;

	if (atomic_inc_return(&sdev->num_cq) > SIW_MAX_CQ) {
		siw_dbg(base_cq->device, "too many CQ's\n");
		rv = -ENOMEM;
		goto err_out;
	}
	if (size < 1 || size > sdev->attrs.max_cqe) {
		siw_dbg(base_cq->device, "CQ size error: %d\n", size);
		rv = -EINVAL;
		goto err_out;
	}
	size = roundup_pow_of_two(size);
	cq->base_cq.cqe = size;
	cq->num_cqe = size;

	if (udata)
		cq->queue = vmalloc_user(size * sizeof(struct siw_cqe) +
					 sizeof(struct siw_cq_ctrl));
	else
		cq->queue = vzalloc(size * sizeof(struct siw_cqe) +
				    sizeof(struct siw_cq_ctrl));

	if (cq->queue == NULL) {
		rv = -ENOMEM;
		goto err_out;
	}
	get_random_bytes(&cq->id, 4);
	siw_dbg(base_cq->device, "new CQ [%u]\n", cq->id);

	spin_lock_init(&cq->lock);

	cq->notify = (struct siw_cq_ctrl *)&cq->queue[size];

	if (udata) {
		struct siw_uresp_create_cq uresp = {};
		struct siw_ucontext *ctx =
			rdma_udata_to_drv_context(udata, struct siw_ucontext,
						  base_ucontext);
		size_t length = size * sizeof(struct siw_cqe) +
			sizeof(struct siw_cq_ctrl);

		cq->cq_entry =
			siw_mmap_entry_insert(ctx, cq->queue,
					      length, &uresp.cq_key);
		if (!cq->cq_entry) {
			rv = -ENOMEM;
			goto err_out;
		}

		uresp.cq_id = cq->id;
		uresp.num_cqe = size;

		if (udata->outlen < sizeof(uresp)) {
			rv = -EINVAL;
			goto err_out;
		}
		rv = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (rv)
			goto err_out;
	}
	return 0;

err_out:
	siw_dbg(base_cq->device, "CQ creation failed: %d", rv);

	if (cq->queue) {
		struct siw_ucontext *ctx =
			rdma_udata_to_drv_context(udata, struct siw_ucontext,
						  base_ucontext);
		if (ctx)
			rdma_user_mmap_entry_remove(cq->cq_entry);
		vfree(cq->queue);
	}
	atomic_dec(&sdev->num_cq);

	return rv;
}

/*
 * siw_poll_cq()
 *
 * Reap CQ entries if available and copy work completion status into
 * array of WC's provided by caller. Returns number of reaped CQE's.
 *
 * @base_cq:	Base CQ contained in siw CQ.
 * @num_cqe:	Maximum number of CQE's to reap.
 * @wc:		Array of work completions to be filled by siw.
 */
int siw_poll_cq(struct ib_cq *base_cq, int num_cqe, struct ib_wc *wc)
{
	struct siw_cq *cq = to_siw_cq(base_cq);
	int i;

	for (i = 0; i < num_cqe; i++) {
		if (!siw_reap_cqe(cq, wc))
			break;
		wc++;
	}
	return i;
}

/*
 * siw_req_notify_cq()
 *
 * Request notification for new CQE's added to that CQ.
 * Defined flags:
 * o SIW_CQ_NOTIFY_SOLICITED lets siw trigger a notification
 *   event if a WQE with notification flag set enters the CQ
 * o SIW_CQ_NOTIFY_NEXT_COMP lets siw trigger a notification
 *   event if a WQE enters the CQ.
 * o IB_CQ_REPORT_MISSED_EVENTS: return value will provide the
 *   number of not reaped CQE's regardless of its notification
 *   type and current or new CQ notification settings.
 *
 * @base_cq:	Base CQ contained in siw CQ.
 * @flags:	Requested notification flags.
 */
int siw_req_notify_cq(struct ib_cq *base_cq, enum ib_cq_notify_flags flags)
{
	struct siw_cq *cq = to_siw_cq(base_cq);

	siw_dbg_cq(cq, "flags: 0x%02x\n", flags);

	if ((flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED)
		/*
		 * Enable CQ event for next solicited completion.
		 * and make it visible to all associated producers.
		 */
		smp_store_mb(cq->notify->flags, SIW_NOTIFY_SOLICITED);
	else
		/*
		 * Enable CQ event for any signalled completion.
		 * and make it visible to all associated producers.
		 */
		smp_store_mb(cq->notify->flags, SIW_NOTIFY_ALL);

	if (flags & IB_CQ_REPORT_MISSED_EVENTS)
		return cq->cq_put - cq->cq_get;

	return 0;
}

/*
 * siw_dereg_mr()
 *
 * Release Memory Region.
 *
 * @base_mr: Base MR contained in siw MR.
 * @udata: points to user context, unused.
 */
int siw_dereg_mr(struct ib_mr *base_mr, struct ib_udata *udata)
{
	struct siw_mr *mr = to_siw_mr(base_mr);
	struct siw_device *sdev = to_siw_dev(base_mr->device);

	siw_dbg_mem(mr->mem, "deregister MR\n");

	atomic_dec(&sdev->num_mr);

	siw_mr_drop_mem(mr);
	kfree_rcu(mr, rcu);

	return 0;
}

/*
 * siw_reg_user_mr()
 *
 * Register Memory Region.
 *
 * @pd:		Protection Domain
 * @start:	starting address of MR (virtual address)
 * @len:	len of MR
 * @rnic_va:	not used by siw
 * @rights:	MR access rights
 * @udata:	user buffer to communicate STag and Key.
 */
struct ib_mr *siw_reg_user_mr(struct ib_pd *pd, u64 start, u64 len,
			      u64 rnic_va, int rights, struct ib_udata *udata)
{
	struct siw_mr *mr = NULL;
	struct siw_umem *umem = NULL;
	struct siw_ureq_reg_mr ureq;
	struct siw_device *sdev = to_siw_dev(pd->device);
	int rv;

	siw_dbg_pd(pd, "start: 0x%pK, va: 0x%pK, len: %llu\n",
		   (void *)(uintptr_t)start, (void *)(uintptr_t)rnic_va,
		   (unsigned long long)len);

	if (atomic_inc_return(&sdev->num_mr) > SIW_MAX_MR) {
		siw_dbg_pd(pd, "too many mr's\n");
		rv = -ENOMEM;
		goto err_out;
	}
	if (!len) {
		rv = -EINVAL;
		goto err_out;
	}
	umem = siw_umem_get(pd->device, start, len, rights);
	if (IS_ERR(umem)) {
		rv = PTR_ERR(umem);
		siw_dbg_pd(pd, "getting user memory failed: %d\n", rv);
		umem = NULL;
		goto err_out;
	}
	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		rv = -ENOMEM;
		goto err_out;
	}
	rv = siw_mr_add_mem(mr, pd, umem, start, len, rights);
	if (rv)
		goto err_out;

	if (udata) {
		struct siw_uresp_reg_mr uresp = {};
		struct siw_mem *mem = mr->mem;

		if (udata->inlen < sizeof(ureq)) {
			rv = -EINVAL;
			goto err_out;
		}
		rv = ib_copy_from_udata(&ureq, udata, sizeof(ureq));
		if (rv)
			goto err_out;

		mr->base_mr.lkey |= ureq.stag_key;
		mr->base_mr.rkey |= ureq.stag_key;
		mem->stag |= ureq.stag_key;
		uresp.stag = mem->stag;

		if (udata->outlen < sizeof(uresp)) {
			rv = -EINVAL;
			goto err_out;
		}
		rv = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (rv)
			goto err_out;
	}
	mr->mem->stag_valid = 1;

	return &mr->base_mr;

err_out:
	atomic_dec(&sdev->num_mr);
	if (mr) {
		if (mr->mem)
			siw_mr_drop_mem(mr);
		kfree_rcu(mr, rcu);
	} else {
		if (umem)
			siw_umem_release(umem);
	}
	return ERR_PTR(rv);
}

struct ib_mr *siw_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
			   u32 max_sge)
{
	struct siw_device *sdev = to_siw_dev(pd->device);
	struct siw_mr *mr = NULL;
	struct siw_pbl *pbl = NULL;
	int rv;

	if (atomic_inc_return(&sdev->num_mr) > SIW_MAX_MR) {
		siw_dbg_pd(pd, "too many mr's\n");
		rv = -ENOMEM;
		goto err_out;
	}
	if (mr_type != IB_MR_TYPE_MEM_REG) {
		siw_dbg_pd(pd, "mr type %d unsupported\n", mr_type);
		rv = -EOPNOTSUPP;
		goto err_out;
	}
	if (max_sge > SIW_MAX_SGE_PBL) {
		siw_dbg_pd(pd, "too many sge's: %d\n", max_sge);
		rv = -ENOMEM;
		goto err_out;
	}
	pbl = siw_pbl_alloc(max_sge);
	if (IS_ERR(pbl)) {
		rv = PTR_ERR(pbl);
		siw_dbg_pd(pd, "pbl allocation failed: %d\n", rv);
		pbl = NULL;
		goto err_out;
	}
	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		rv = -ENOMEM;
		goto err_out;
	}
	rv = siw_mr_add_mem(mr, pd, pbl, 0, max_sge * PAGE_SIZE, 0);
	if (rv)
		goto err_out;

	mr->mem->is_pbl = 1;

	siw_dbg_pd(pd, "[MEM %u]: success\n", mr->mem->stag);

	return &mr->base_mr;

err_out:
	atomic_dec(&sdev->num_mr);

	if (!mr) {
		kfree(pbl);
	} else {
		if (mr->mem)
			siw_mr_drop_mem(mr);
		kfree_rcu(mr, rcu);
	}
	siw_dbg_pd(pd, "failed: %d\n", rv);

	return ERR_PTR(rv);
}

/* Just used to count number of pages being mapped */
static int siw_set_pbl_page(struct ib_mr *base_mr, u64 buf_addr)
{
	return 0;
}

int siw_map_mr_sg(struct ib_mr *base_mr, struct scatterlist *sl, int num_sle,
		  unsigned int *sg_off)
{
	struct scatterlist *slp;
	struct siw_mr *mr = to_siw_mr(base_mr);
	struct siw_mem *mem = mr->mem;
	struct siw_pbl *pbl = mem->pbl;
	struct siw_pble *pble;
	unsigned long pbl_size;
	int i, rv;

	if (!pbl) {
		siw_dbg_mem(mem, "no PBL allocated\n");
		return -EINVAL;
	}
	pble = pbl->pbe;

	if (pbl->max_buf < num_sle) {
		siw_dbg_mem(mem, "too many SGE's: %d > %d\n",
			    num_sle, pbl->max_buf);
		return -ENOMEM;
	}
	for_each_sg(sl, slp, num_sle, i) {
		if (sg_dma_len(slp) == 0) {
			siw_dbg_mem(mem, "empty SGE\n");
			return -EINVAL;
		}
		if (i == 0) {
			pble->addr = sg_dma_address(slp);
			pble->size = sg_dma_len(slp);
			pble->pbl_off = 0;
			pbl_size = pble->size;
			pbl->num_buf = 1;
		} else {
			/* Merge PBL entries if adjacent */
			if (pble->addr + pble->size == sg_dma_address(slp)) {
				pble->size += sg_dma_len(slp);
			} else {
				pble++;
				pbl->num_buf++;
				pble->addr = sg_dma_address(slp);
				pble->size = sg_dma_len(slp);
				pble->pbl_off = pbl_size;
			}
			pbl_size += sg_dma_len(slp);
		}
		siw_dbg_mem(mem,
			"sge[%d], size %u, addr 0x%p, total %lu\n",
			i, pble->size, ib_virt_dma_to_ptr(pble->addr),
			pbl_size);
	}
	rv = ib_sg_to_pages(base_mr, sl, num_sle, sg_off, siw_set_pbl_page);
	if (rv > 0) {
		mem->len = base_mr->length;
		mem->va = base_mr->iova;
		siw_dbg_mem(mem,
			"%llu bytes, start 0x%pK, %u SLE to %u entries\n",
			mem->len, (void *)(uintptr_t)mem->va, num_sle,
			pbl->num_buf);
	}
	return rv;
}

/*
 * siw_get_dma_mr()
 *
 * Create a (empty) DMA memory region, where no umem is attached.
 */
struct ib_mr *siw_get_dma_mr(struct ib_pd *pd, int rights)
{
	struct siw_device *sdev = to_siw_dev(pd->device);
	struct siw_mr *mr = NULL;
	int rv;

	if (atomic_inc_return(&sdev->num_mr) > SIW_MAX_MR) {
		siw_dbg_pd(pd, "too many mr's\n");
		rv = -ENOMEM;
		goto err_out;
	}
	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		rv = -ENOMEM;
		goto err_out;
	}
	rv = siw_mr_add_mem(mr, pd, NULL, 0, ULONG_MAX, rights);
	if (rv)
		goto err_out;

	mr->mem->stag_valid = 1;

	siw_dbg_pd(pd, "[MEM %u]: success\n", mr->mem->stag);

	return &mr->base_mr;

err_out:
	if (rv)
		kfree(mr);

	atomic_dec(&sdev->num_mr);

	return ERR_PTR(rv);
}

/*
 * siw_create_srq()
 *
 * Create Shared Receive Queue of attributes @init_attrs
 * within protection domain given by @pd.
 *
 * @base_srq:	Base SRQ contained in siw SRQ.
 * @init_attrs:	SRQ init attributes.
 * @udata:	points to user context
 */
int siw_create_srq(struct ib_srq *base_srq,
		   struct ib_srq_init_attr *init_attrs, struct ib_udata *udata)
{
	struct siw_srq *srq = to_siw_srq(base_srq);
	struct ib_srq_attr *attrs = &init_attrs->attr;
	struct siw_device *sdev = to_siw_dev(base_srq->device);
	struct siw_ucontext *ctx =
		rdma_udata_to_drv_context(udata, struct siw_ucontext,
					  base_ucontext);
	int rv;

	if (init_attrs->srq_type != IB_SRQT_BASIC)
		return -EOPNOTSUPP;

	if (atomic_inc_return(&sdev->num_srq) > SIW_MAX_SRQ) {
		siw_dbg_pd(base_srq->pd, "too many SRQ's\n");
		rv = -ENOMEM;
		goto err_out;
	}
	if (attrs->max_wr == 0 || attrs->max_wr > SIW_MAX_SRQ_WR ||
	    attrs->max_sge > SIW_MAX_SGE || attrs->srq_limit > attrs->max_wr) {
		rv = -EINVAL;
		goto err_out;
	}
	srq->max_sge = attrs->max_sge;
	srq->num_rqe = roundup_pow_of_two(attrs->max_wr);
	srq->limit = attrs->srq_limit;
	if (srq->limit)
		srq->armed = true;

	srq->is_kernel_res = !udata;

	if (udata)
		srq->recvq =
			vmalloc_user(srq->num_rqe * sizeof(struct siw_rqe));
	else
		srq->recvq = vcalloc(srq->num_rqe, sizeof(struct siw_rqe));

	if (srq->recvq == NULL) {
		rv = -ENOMEM;
		goto err_out;
	}
	if (udata) {
		struct siw_uresp_create_srq uresp = {};
		size_t length = srq->num_rqe * sizeof(struct siw_rqe);

		srq->srq_entry =
			siw_mmap_entry_insert(ctx, srq->recvq,
					      length, &uresp.srq_key);
		if (!srq->srq_entry) {
			rv = -ENOMEM;
			goto err_out;
		}

		uresp.num_rqe = srq->num_rqe;

		if (udata->outlen < sizeof(uresp)) {
			rv = -EINVAL;
			goto err_out;
		}
		rv = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (rv)
			goto err_out;
	}
	spin_lock_init(&srq->lock);

	siw_dbg_pd(base_srq->pd, "[SRQ]: success\n");

	return 0;

err_out:
	if (srq->recvq) {
		if (ctx)
			rdma_user_mmap_entry_remove(srq->srq_entry);
		vfree(srq->recvq);
	}
	atomic_dec(&sdev->num_srq);

	return rv;
}

/*
 * siw_modify_srq()
 *
 * Modify SRQ. The caller may resize SRQ and/or set/reset notification
 * limit and (re)arm IB_EVENT_SRQ_LIMIT_REACHED notification.
 *
 * NOTE: it is unclear if RDMA core allows for changing the MAX_SGE
 * parameter. siw_modify_srq() does not check the attrs->max_sge param.
 */
int siw_modify_srq(struct ib_srq *base_srq, struct ib_srq_attr *attrs,
		   enum ib_srq_attr_mask attr_mask, struct ib_udata *udata)
{
	struct siw_srq *srq = to_siw_srq(base_srq);
	unsigned long flags;
	int rv = 0;

	spin_lock_irqsave(&srq->lock, flags);

	if (attr_mask & IB_SRQ_MAX_WR) {
		/* resize request not yet supported */
		rv = -EOPNOTSUPP;
		goto out;
	}
	if (attr_mask & IB_SRQ_LIMIT) {
		if (attrs->srq_limit) {
			if (unlikely(attrs->srq_limit > srq->num_rqe)) {
				rv = -EINVAL;
				goto out;
			}
			srq->armed = true;
		} else {
			srq->armed = false;
		}
		srq->limit = attrs->srq_limit;
	}
out:
	spin_unlock_irqrestore(&srq->lock, flags);

	return rv;
}

/*
 * siw_query_srq()
 *
 * Query SRQ attributes.
 */
int siw_query_srq(struct ib_srq *base_srq, struct ib_srq_attr *attrs)
{
	struct siw_srq *srq = to_siw_srq(base_srq);
	unsigned long flags;

	spin_lock_irqsave(&srq->lock, flags);

	attrs->max_wr = srq->num_rqe;
	attrs->max_sge = srq->max_sge;
	attrs->srq_limit = srq->limit;

	spin_unlock_irqrestore(&srq->lock, flags);

	return 0;
}

/*
 * siw_destroy_srq()
 *
 * Destroy SRQ.
 * It is assumed that the SRQ is not referenced by any
 * QP anymore - the code trusts the RDMA core environment to keep track
 * of QP references.
 */
int siw_destroy_srq(struct ib_srq *base_srq, struct ib_udata *udata)
{
	struct siw_srq *srq = to_siw_srq(base_srq);
	struct siw_device *sdev = to_siw_dev(base_srq->device);
	struct siw_ucontext *ctx =
		rdma_udata_to_drv_context(udata, struct siw_ucontext,
					  base_ucontext);

	if (ctx)
		rdma_user_mmap_entry_remove(srq->srq_entry);
	vfree(srq->recvq);
	atomic_dec(&sdev->num_srq);
	return 0;
}

/*
 * siw_post_srq_recv()
 *
 * Post a list of receive queue elements to SRQ.
 * NOTE: The function does not check or lock a certain SRQ state
 *       during the post operation. The code simply trusts the
 *       RDMA core environment.
 *
 * @base_srq:	Base SRQ contained in siw SRQ
 * @wr:		List of R-WR's
 * @bad_wr:	Updated to failing WR if posting fails.
 */
int siw_post_srq_recv(struct ib_srq *base_srq, const struct ib_recv_wr *wr,
		      const struct ib_recv_wr **bad_wr)
{
	struct siw_srq *srq = to_siw_srq(base_srq);
	unsigned long flags;
	int rv = 0;

	if (unlikely(!srq->is_kernel_res)) {
		siw_dbg_pd(base_srq->pd,
			   "[SRQ]: no kernel post_recv for mapped srq\n");
		rv = -EINVAL;
		goto out;
	}
	/*
	 * Serialize potentially multiple producers.
	 * Also needed to serialize potentially multiple
	 * consumers.
	 */
	spin_lock_irqsave(&srq->lock, flags);

	while (wr) {
		u32 idx = srq->rq_put % srq->num_rqe;
		struct siw_rqe *rqe = &srq->recvq[idx];

		if (rqe->flags) {
			siw_dbg_pd(base_srq->pd, "SRQ full\n");
			rv = -ENOMEM;
			break;
		}
		if (unlikely(wr->num_sge > srq->max_sge)) {
			siw_dbg_pd(base_srq->pd,
				   "[SRQ]: too many sge's: %d\n", wr->num_sge);
			rv = -EINVAL;
			break;
		}
		rqe->id = wr->wr_id;
		rqe->num_sge = wr->num_sge;
		siw_copy_sgl(wr->sg_list, rqe->sge, wr->num_sge);

		/* Make sure S-RQE is completely written before valid */
		smp_wmb();

		rqe->flags = SIW_WQE_VALID;

		srq->rq_put++;
		wr = wr->next;
	}
	spin_unlock_irqrestore(&srq->lock, flags);
out:
	if (unlikely(rv < 0)) {
		siw_dbg_pd(base_srq->pd, "[SRQ]: error %d\n", rv);
		*bad_wr = wr;
	}
	return rv;
}

void siw_qp_event(struct siw_qp *qp, enum ib_event_type etype)
{
	struct ib_event event;
	struct ib_qp *base_qp = &qp->base_qp;

	/*
	 * Do not report asynchronous errors on QP which gets
	 * destroyed via verbs interface (siw_destroy_qp())
	 */
	if (qp->attrs.flags & SIW_QP_IN_DESTROY)
		return;

	event.event = etype;
	event.device = base_qp->device;
	event.element.qp = base_qp;

	if (base_qp->event_handler) {
		siw_dbg_qp(qp, "reporting event %d\n", etype);
		base_qp->event_handler(&event, base_qp->qp_context);
	}
}

void siw_cq_event(struct siw_cq *cq, enum ib_event_type etype)
{
	struct ib_event event;
	struct ib_cq *base_cq = &cq->base_cq;

	event.event = etype;
	event.device = base_cq->device;
	event.element.cq = base_cq;

	if (base_cq->event_handler) {
		siw_dbg_cq(cq, "reporting CQ event %d\n", etype);
		base_cq->event_handler(&event, base_cq->cq_context);
	}
}

void siw_srq_event(struct siw_srq *srq, enum ib_event_type etype)
{
	struct ib_event event;
	struct ib_srq *base_srq = &srq->base_srq;

	event.event = etype;
	event.device = base_srq->device;
	event.element.srq = base_srq;

	if (base_srq->event_handler) {
		siw_dbg_pd(srq->base_srq.pd,
			   "reporting SRQ event %d\n", etype);
		base_srq->event_handler(&event, base_srq->srq_context);
	}
}

void siw_port_event(struct siw_device *sdev, u32 port, enum ib_event_type etype)
{
	struct ib_event event;

	event.event = etype;
	event.device = &sdev->base_dev;
	event.element.port_num = port;

	siw_dbg(&sdev->base_dev, "reporting port event %d\n", etype);

	ib_dispatch_event(&event);
}
