/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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

#include <linux/mlx4/cq.h>
#include <linux/mlx4/qp.h>
#include <linux/mlx4/srq.h>
#include <linux/slab.h>

#include "mlx4_ib.h"
#include "user.h"

static void mlx4_ib_cq_comp(struct mlx4_cq *cq)
{
	struct ib_cq *ibcq = &to_mibcq(cq)->ibcq;
	ibcq->comp_handler(ibcq, ibcq->cq_context);
}

static void mlx4_ib_cq_event(struct mlx4_cq *cq, enum mlx4_event type)
{
	struct ib_event event;
	struct ib_cq *ibcq;

	if (type != MLX4_EVENT_TYPE_CQ_ERROR) {
		pr_warn("Unexpected event type %d "
		       "on CQ %06x\n", type, cq->cqn);
		return;
	}

	ibcq = &to_mibcq(cq)->ibcq;
	if (ibcq->event_handler) {
		event.device     = ibcq->device;
		event.event      = IB_EVENT_CQ_ERR;
		event.element.cq = ibcq;
		ibcq->event_handler(&event, ibcq->cq_context);
	}
}

static void *get_cqe_from_buf(struct mlx4_ib_cq_buf *buf, int n)
{
	return mlx4_buf_offset(&buf->buf, n * buf->entry_size);
}

static void *get_cqe(struct mlx4_ib_cq *cq, int n)
{
	return get_cqe_from_buf(&cq->buf, n);
}

static void *get_sw_cqe(struct mlx4_ib_cq *cq, int n)
{
	struct mlx4_cqe *cqe = get_cqe(cq, n & cq->ibcq.cqe);
	struct mlx4_cqe *tcqe = ((cq->buf.entry_size == 64) ? (cqe + 1) : cqe);

	return (!!(tcqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK) ^
		!!(n & (cq->ibcq.cqe + 1))) ? NULL : cqe;
}

static struct mlx4_cqe *next_cqe_sw(struct mlx4_ib_cq *cq)
{
	return get_sw_cqe(cq, cq->mcq.cons_index);
}

int mlx4_ib_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period)
{
	struct mlx4_ib_cq *mcq = to_mcq(cq);
	struct mlx4_ib_dev *dev = to_mdev(cq->device);

	return mlx4_cq_modify(dev->dev, &mcq->mcq, cq_count, cq_period);
}

static int mlx4_ib_alloc_cq_buf(struct mlx4_ib_dev *dev, struct mlx4_ib_cq_buf *buf, int nent)
{
	int err;

	err = mlx4_buf_alloc(dev->dev, nent * dev->dev->caps.cqe_size,
			     PAGE_SIZE * 2, &buf->buf, GFP_KERNEL);

	if (err)
		goto out;

	buf->entry_size = dev->dev->caps.cqe_size;
	err = mlx4_mtt_init(dev->dev, buf->buf.npages, buf->buf.page_shift,
				    &buf->mtt);
	if (err)
		goto err_buf;

	err = mlx4_buf_write_mtt(dev->dev, &buf->mtt, &buf->buf, GFP_KERNEL);
	if (err)
		goto err_mtt;

	return 0;

err_mtt:
	mlx4_mtt_cleanup(dev->dev, &buf->mtt);

err_buf:
	mlx4_buf_free(dev->dev, nent * buf->entry_size, &buf->buf);

out:
	return err;
}

static void mlx4_ib_free_cq_buf(struct mlx4_ib_dev *dev, struct mlx4_ib_cq_buf *buf, int cqe)
{
	mlx4_buf_free(dev->dev, (cqe + 1) * buf->entry_size, &buf->buf);
}

static int mlx4_ib_get_cq_umem(struct mlx4_ib_dev *dev, struct ib_ucontext *context,
			       struct mlx4_ib_cq_buf *buf, struct ib_umem **umem,
			       u64 buf_addr, int cqe)
{
	int err;
	int cqe_size = dev->dev->caps.cqe_size;

	*umem = ib_umem_get(context, buf_addr, cqe * cqe_size,
			    IB_ACCESS_LOCAL_WRITE, 1);
	if (IS_ERR(*umem))
		return PTR_ERR(*umem);

	err = mlx4_mtt_init(dev->dev, ib_umem_page_count(*umem),
			    ilog2((*umem)->page_size), &buf->mtt);
	if (err)
		goto err_buf;

	err = mlx4_ib_umem_write_mtt(dev, &buf->mtt, *umem);
	if (err)
		goto err_mtt;

	return 0;

err_mtt:
	mlx4_mtt_cleanup(dev->dev, &buf->mtt);

err_buf:
	ib_umem_release(*umem);

	return err;
}

#define CQ_CREATE_FLAGS_SUPPORTED IB_CQ_FLAGS_TIMESTAMP_COMPLETION
struct ib_cq *mlx4_ib_create_cq(struct ib_device *ibdev,
				const struct ib_cq_init_attr *attr,
				struct ib_ucontext *context,
				struct ib_udata *udata)
{
	int entries = attr->cqe;
	int vector = attr->comp_vector;
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_ib_cq *cq;
	struct mlx4_uar *uar;
	int err;

	if (entries < 1 || entries > dev->dev->caps.max_cqes)
		return ERR_PTR(-EINVAL);

	if (attr->flags & ~CQ_CREATE_FLAGS_SUPPORTED)
		return ERR_PTR(-EINVAL);

	cq = kmalloc(sizeof *cq, GFP_KERNEL);
	if (!cq)
		return ERR_PTR(-ENOMEM);

	entries      = roundup_pow_of_two(entries + 1);
	cq->ibcq.cqe = entries - 1;
	mutex_init(&cq->resize_mutex);
	spin_lock_init(&cq->lock);
	cq->resize_buf = NULL;
	cq->resize_umem = NULL;
	cq->create_flags = attr->flags;
	INIT_LIST_HEAD(&cq->send_qp_list);
	INIT_LIST_HEAD(&cq->recv_qp_list);

	if (context) {
		struct mlx4_ib_create_cq ucmd;

		if (ib_copy_from_udata(&ucmd, udata, sizeof ucmd)) {
			err = -EFAULT;
			goto err_cq;
		}

		err = mlx4_ib_get_cq_umem(dev, context, &cq->buf, &cq->umem,
					  ucmd.buf_addr, entries);
		if (err)
			goto err_cq;

		err = mlx4_ib_db_map_user(to_mucontext(context), ucmd.db_addr,
					  &cq->db);
		if (err)
			goto err_mtt;

		uar = &to_mucontext(context)->uar;
	} else {
		err = mlx4_db_alloc(dev->dev, &cq->db, 1, GFP_KERNEL);
		if (err)
			goto err_cq;

		cq->mcq.set_ci_db  = cq->db.db;
		cq->mcq.arm_db     = cq->db.db + 1;
		*cq->mcq.set_ci_db = 0;
		*cq->mcq.arm_db    = 0;

		err = mlx4_ib_alloc_cq_buf(dev, &cq->buf, entries);
		if (err)
			goto err_db;

		uar = &dev->priv_uar;
	}

	if (dev->eq_table)
		vector = dev->eq_table[vector % ibdev->num_comp_vectors];

	err = mlx4_cq_alloc(dev->dev, entries, &cq->buf.mtt, uar,
			    cq->db.dma, &cq->mcq, vector, 0,
			    !!(cq->create_flags & IB_CQ_FLAGS_TIMESTAMP_COMPLETION));
	if (err)
		goto err_dbmap;

	if (context)
		cq->mcq.tasklet_ctx.comp = mlx4_ib_cq_comp;
	else
		cq->mcq.comp = mlx4_ib_cq_comp;
	cq->mcq.event = mlx4_ib_cq_event;

	if (context)
		if (ib_copy_to_udata(udata, &cq->mcq.cqn, sizeof (__u32))) {
			err = -EFAULT;
			goto err_dbmap;
		}

	return &cq->ibcq;

err_dbmap:
	if (context)
		mlx4_ib_db_unmap_user(to_mucontext(context), &cq->db);

err_mtt:
	mlx4_mtt_cleanup(dev->dev, &cq->buf.mtt);

	if (context)
		ib_umem_release(cq->umem);
	else
		mlx4_ib_free_cq_buf(dev, &cq->buf, cq->ibcq.cqe);

err_db:
	if (!context)
		mlx4_db_free(dev->dev, &cq->db);

err_cq:
	kfree(cq);

	return ERR_PTR(err);
}

static int mlx4_alloc_resize_buf(struct mlx4_ib_dev *dev, struct mlx4_ib_cq *cq,
				  int entries)
{
	int err;

	if (cq->resize_buf)
		return -EBUSY;

	cq->resize_buf = kmalloc(sizeof *cq->resize_buf, GFP_ATOMIC);
	if (!cq->resize_buf)
		return -ENOMEM;

	err = mlx4_ib_alloc_cq_buf(dev, &cq->resize_buf->buf, entries);
	if (err) {
		kfree(cq->resize_buf);
		cq->resize_buf = NULL;
		return err;
	}

	cq->resize_buf->cqe = entries - 1;

	return 0;
}

static int mlx4_alloc_resize_umem(struct mlx4_ib_dev *dev, struct mlx4_ib_cq *cq,
				   int entries, struct ib_udata *udata)
{
	struct mlx4_ib_resize_cq ucmd;
	int err;

	if (cq->resize_umem)
		return -EBUSY;

	if (ib_copy_from_udata(&ucmd, udata, sizeof ucmd))
		return -EFAULT;

	cq->resize_buf = kmalloc(sizeof *cq->resize_buf, GFP_ATOMIC);
	if (!cq->resize_buf)
		return -ENOMEM;

	err = mlx4_ib_get_cq_umem(dev, cq->umem->context, &cq->resize_buf->buf,
				  &cq->resize_umem, ucmd.buf_addr, entries);
	if (err) {
		kfree(cq->resize_buf);
		cq->resize_buf = NULL;
		return err;
	}

	cq->resize_buf->cqe = entries - 1;

	return 0;
}

static int mlx4_ib_get_outstanding_cqes(struct mlx4_ib_cq *cq)
{
	u32 i;

	i = cq->mcq.cons_index;
	while (get_sw_cqe(cq, i))
		++i;

	return i - cq->mcq.cons_index;
}

static void mlx4_ib_cq_resize_copy_cqes(struct mlx4_ib_cq *cq)
{
	struct mlx4_cqe *cqe, *new_cqe;
	int i;
	int cqe_size = cq->buf.entry_size;
	int cqe_inc = cqe_size == 64 ? 1 : 0;

	i = cq->mcq.cons_index;
	cqe = get_cqe(cq, i & cq->ibcq.cqe);
	cqe += cqe_inc;

	while ((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) != MLX4_CQE_OPCODE_RESIZE) {
		new_cqe = get_cqe_from_buf(&cq->resize_buf->buf,
					   (i + 1) & cq->resize_buf->cqe);
		memcpy(new_cqe, get_cqe(cq, i & cq->ibcq.cqe), cqe_size);
		new_cqe += cqe_inc;

		new_cqe->owner_sr_opcode = (cqe->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK) |
			(((i + 1) & (cq->resize_buf->cqe + 1)) ? MLX4_CQE_OWNER_MASK : 0);
		cqe = get_cqe(cq, ++i & cq->ibcq.cqe);
		cqe += cqe_inc;
	}
	++cq->mcq.cons_index;
}

int mlx4_ib_resize_cq(struct ib_cq *ibcq, int entries, struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibcq->device);
	struct mlx4_ib_cq *cq = to_mcq(ibcq);
	struct mlx4_mtt mtt;
	int outst_cqe;
	int err;

	mutex_lock(&cq->resize_mutex);
	if (entries < 1 || entries > dev->dev->caps.max_cqes) {
		err = -EINVAL;
		goto out;
	}

	entries = roundup_pow_of_two(entries + 1);
	if (entries == ibcq->cqe + 1) {
		err = 0;
		goto out;
	}

	if (entries > dev->dev->caps.max_cqes + 1) {
		err = -EINVAL;
		goto out;
	}

	if (ibcq->uobject) {
		err = mlx4_alloc_resize_umem(dev, cq, entries, udata);
		if (err)
			goto out;
	} else {
		/* Can't be smaller than the number of outstanding CQEs */
		outst_cqe = mlx4_ib_get_outstanding_cqes(cq);
		if (entries < outst_cqe + 1) {
			err = -EINVAL;
			goto out;
		}

		err = mlx4_alloc_resize_buf(dev, cq, entries);
		if (err)
			goto out;
	}

	mtt = cq->buf.mtt;

	err = mlx4_cq_resize(dev->dev, &cq->mcq, entries, &cq->resize_buf->buf.mtt);
	if (err)
		goto err_buf;

	mlx4_mtt_cleanup(dev->dev, &mtt);
	if (ibcq->uobject) {
		cq->buf      = cq->resize_buf->buf;
		cq->ibcq.cqe = cq->resize_buf->cqe;
		ib_umem_release(cq->umem);
		cq->umem     = cq->resize_umem;

		kfree(cq->resize_buf);
		cq->resize_buf = NULL;
		cq->resize_umem = NULL;
	} else {
		struct mlx4_ib_cq_buf tmp_buf;
		int tmp_cqe = 0;

		spin_lock_irq(&cq->lock);
		if (cq->resize_buf) {
			mlx4_ib_cq_resize_copy_cqes(cq);
			tmp_buf = cq->buf;
			tmp_cqe = cq->ibcq.cqe;
			cq->buf      = cq->resize_buf->buf;
			cq->ibcq.cqe = cq->resize_buf->cqe;

			kfree(cq->resize_buf);
			cq->resize_buf = NULL;
		}
		spin_unlock_irq(&cq->lock);

		if (tmp_cqe)
			mlx4_ib_free_cq_buf(dev, &tmp_buf, tmp_cqe);
	}

	goto out;

err_buf:
	mlx4_mtt_cleanup(dev->dev, &cq->resize_buf->buf.mtt);
	if (!ibcq->uobject)
		mlx4_ib_free_cq_buf(dev, &cq->resize_buf->buf,
				    cq->resize_buf->cqe);

	kfree(cq->resize_buf);
	cq->resize_buf = NULL;

	if (cq->resize_umem) {
		ib_umem_release(cq->resize_umem);
		cq->resize_umem = NULL;
	}

out:
	mutex_unlock(&cq->resize_mutex);

	return err;
}

int mlx4_ib_destroy_cq(struct ib_cq *cq)
{
	struct mlx4_ib_dev *dev = to_mdev(cq->device);
	struct mlx4_ib_cq *mcq = to_mcq(cq);

	mlx4_cq_free(dev->dev, &mcq->mcq);
	mlx4_mtt_cleanup(dev->dev, &mcq->buf.mtt);

	if (cq->uobject) {
		mlx4_ib_db_unmap_user(to_mucontext(cq->uobject->context), &mcq->db);
		ib_umem_release(mcq->umem);
	} else {
		mlx4_ib_free_cq_buf(dev, &mcq->buf, cq->cqe);
		mlx4_db_free(dev->dev, &mcq->db);
	}

	kfree(mcq);

	return 0;
}

static void dump_cqe(void *cqe)
{
	__be32 *buf = cqe;

	pr_debug("CQE contents %08x %08x %08x %08x %08x %08x %08x %08x\n",
	       be32_to_cpu(buf[0]), be32_to_cpu(buf[1]), be32_to_cpu(buf[2]),
	       be32_to_cpu(buf[3]), be32_to_cpu(buf[4]), be32_to_cpu(buf[5]),
	       be32_to_cpu(buf[6]), be32_to_cpu(buf[7]));
}

static void mlx4_ib_handle_error_cqe(struct mlx4_err_cqe *cqe,
				     struct ib_wc *wc)
{
	if (cqe->syndrome == MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR) {
		pr_debug("local QP operation err "
		       "(QPN %06x, WQE index %x, vendor syndrome %02x, "
		       "opcode = %02x)\n",
		       be32_to_cpu(cqe->my_qpn), be16_to_cpu(cqe->wqe_index),
		       cqe->vendor_err_syndrome,
		       cqe->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK);
		dump_cqe(cqe);
	}

	switch (cqe->syndrome) {
	case MLX4_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		wc->status = IB_WC_LOC_LEN_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		wc->status = IB_WC_LOC_QP_OP_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_PROT_ERR:
		wc->status = IB_WC_LOC_PROT_ERR;
		break;
	case MLX4_CQE_SYNDROME_WR_FLUSH_ERR:
		wc->status = IB_WC_WR_FLUSH_ERR;
		break;
	case MLX4_CQE_SYNDROME_MW_BIND_ERR:
		wc->status = IB_WC_MW_BIND_ERR;
		break;
	case MLX4_CQE_SYNDROME_BAD_RESP_ERR:
		wc->status = IB_WC_BAD_RESP_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		wc->status = IB_WC_LOC_ACCESS_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		wc->status = IB_WC_REM_INV_REQ_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		wc->status = IB_WC_REM_ACCESS_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_OP_ERR:
		wc->status = IB_WC_REM_OP_ERR;
		break;
	case MLX4_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		wc->status = IB_WC_RETRY_EXC_ERR;
		break;
	case MLX4_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		wc->status = IB_WC_RNR_RETRY_EXC_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_ABORTED_ERR:
		wc->status = IB_WC_REM_ABORT_ERR;
		break;
	default:
		wc->status = IB_WC_GENERAL_ERR;
		break;
	}

	wc->vendor_err = cqe->vendor_err_syndrome;
}

static int mlx4_ib_ipoib_csum_ok(__be16 status, __be16 checksum)
{
	return ((status & cpu_to_be16(MLX4_CQE_STATUS_IPV4      |
				      MLX4_CQE_STATUS_IPV4F     |
				      MLX4_CQE_STATUS_IPV4OPT   |
				      MLX4_CQE_STATUS_IPV6      |
				      MLX4_CQE_STATUS_IPOK)) ==
		cpu_to_be16(MLX4_CQE_STATUS_IPV4        |
			    MLX4_CQE_STATUS_IPOK))              &&
		(status & cpu_to_be16(MLX4_CQE_STATUS_UDP       |
				      MLX4_CQE_STATUS_TCP))     &&
		checksum == cpu_to_be16(0xffff);
}

static int use_tunnel_data(struct mlx4_ib_qp *qp, struct mlx4_ib_cq *cq, struct ib_wc *wc,
			   unsigned tail, struct mlx4_cqe *cqe, int is_eth)
{
	struct mlx4_ib_proxy_sqp_hdr *hdr;

	ib_dma_sync_single_for_cpu(qp->ibqp.device,
				   qp->sqp_proxy_rcv[tail].map,
				   sizeof (struct mlx4_ib_proxy_sqp_hdr),
				   DMA_FROM_DEVICE);
	hdr = (struct mlx4_ib_proxy_sqp_hdr *) (qp->sqp_proxy_rcv[tail].addr);
	wc->pkey_index	= be16_to_cpu(hdr->tun.pkey_index);
	wc->src_qp	= be32_to_cpu(hdr->tun.flags_src_qp) & 0xFFFFFF;
	wc->wc_flags   |= (hdr->tun.g_ml_path & 0x80) ? (IB_WC_GRH) : 0;
	wc->dlid_path_bits = 0;

	if (is_eth) {
		wc->vlan_id = be16_to_cpu(hdr->tun.sl_vid);
		memcpy(&(wc->smac[0]), (char *)&hdr->tun.mac_31_0, 4);
		memcpy(&(wc->smac[4]), (char *)&hdr->tun.slid_mac_47_32, 2);
		wc->wc_flags |= (IB_WC_WITH_VLAN | IB_WC_WITH_SMAC);
	} else {
		wc->slid        = be16_to_cpu(hdr->tun.slid_mac_47_32);
		wc->sl          = (u8) (be16_to_cpu(hdr->tun.sl_vid) >> 12);
	}

	return 0;
}

static void mlx4_ib_qp_sw_comp(struct mlx4_ib_qp *qp, int num_entries,
			       struct ib_wc *wc, int *npolled, int is_send)
{
	struct mlx4_ib_wq *wq;
	unsigned cur;
	int i;

	wq = is_send ? &qp->sq : &qp->rq;
	cur = wq->head - wq->tail;

	if (cur == 0)
		return;

	for (i = 0;  i < cur && *npolled < num_entries; i++) {
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		wc->status = IB_WC_WR_FLUSH_ERR;
		wc->vendor_err = MLX4_CQE_SYNDROME_WR_FLUSH_ERR;
		wq->tail++;
		(*npolled)++;
		wc->qp = &qp->ibqp;
		wc++;
	}
}

static void mlx4_ib_poll_sw_comp(struct mlx4_ib_cq *cq, int num_entries,
				 struct ib_wc *wc, int *npolled)
{
	struct mlx4_ib_qp *qp;

	*npolled = 0;
	/* Find uncompleted WQEs belonging to that cq and retrun
	 * simulated FLUSH_ERR completions
	 */
	list_for_each_entry(qp, &cq->send_qp_list, cq_send_list) {
		mlx4_ib_qp_sw_comp(qp, num_entries, wc + *npolled, npolled, 1);
		if (*npolled >= num_entries)
			goto out;
	}

	list_for_each_entry(qp, &cq->recv_qp_list, cq_recv_list) {
		mlx4_ib_qp_sw_comp(qp, num_entries, wc + *npolled, npolled, 0);
		if (*npolled >= num_entries)
			goto out;
	}

out:
	return;
}

static int mlx4_ib_poll_one(struct mlx4_ib_cq *cq,
			    struct mlx4_ib_qp **cur_qp,
			    struct ib_wc *wc)
{
	struct mlx4_cqe *cqe;
	struct mlx4_qp *mqp;
	struct mlx4_ib_wq *wq;
	struct mlx4_ib_srq *srq;
	struct mlx4_srq *msrq = NULL;
	int is_send;
	int is_error;
	int is_eth;
	u32 g_mlpath_rqpn;
	u16 wqe_ctr;
	unsigned tail = 0;

repoll:
	cqe = next_cqe_sw(cq);
	if (!cqe)
		return -EAGAIN;

	if (cq->buf.entry_size == 64)
		cqe++;

	++cq->mcq.cons_index;

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

	is_send  = cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK;
	is_error = (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
		MLX4_CQE_OPCODE_ERROR;

	if (unlikely((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) == MLX4_OPCODE_NOP &&
		     is_send)) {
		pr_warn("Completion for NOP opcode detected!\n");
		return -EINVAL;
	}

	/* Resize CQ in progress */
	if (unlikely((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) == MLX4_CQE_OPCODE_RESIZE)) {
		if (cq->resize_buf) {
			struct mlx4_ib_dev *dev = to_mdev(cq->ibcq.device);

			mlx4_ib_free_cq_buf(dev, &cq->buf, cq->ibcq.cqe);
			cq->buf      = cq->resize_buf->buf;
			cq->ibcq.cqe = cq->resize_buf->cqe;

			kfree(cq->resize_buf);
			cq->resize_buf = NULL;
		}

		goto repoll;
	}

	if (!*cur_qp ||
	    (be32_to_cpu(cqe->vlan_my_qpn) & MLX4_CQE_QPN_MASK) != (*cur_qp)->mqp.qpn) {
		/*
		 * We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
		mqp = __mlx4_qp_lookup(to_mdev(cq->ibcq.device)->dev,
				       be32_to_cpu(cqe->vlan_my_qpn));
		if (unlikely(!mqp)) {
			pr_warn("CQ %06x with entry for unknown QPN %06x\n",
			       cq->mcq.cqn, be32_to_cpu(cqe->vlan_my_qpn) & MLX4_CQE_QPN_MASK);
			return -EINVAL;
		}

		*cur_qp = to_mibqp(mqp);
	}

	wc->qp = &(*cur_qp)->ibqp;

	if (wc->qp->qp_type == IB_QPT_XRC_TGT) {
		u32 srq_num;
		g_mlpath_rqpn = be32_to_cpu(cqe->g_mlpath_rqpn);
		srq_num       = g_mlpath_rqpn & 0xffffff;
		/* SRQ is also in the radix tree */
		msrq = mlx4_srq_lookup(to_mdev(cq->ibcq.device)->dev,
				       srq_num);
		if (unlikely(!msrq)) {
			pr_warn("CQ %06x with entry for unknown SRQN %06x\n",
				cq->mcq.cqn, srq_num);
			return -EINVAL;
		}
	}

	if (is_send) {
		wq = &(*cur_qp)->sq;
		if (!(*cur_qp)->sq_signal_bits) {
			wqe_ctr = be16_to_cpu(cqe->wqe_index);
			wq->tail += (u16) (wqe_ctr - (u16) wq->tail);
		}
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	} else if ((*cur_qp)->ibqp.srq) {
		srq = to_msrq((*cur_qp)->ibqp.srq);
		wqe_ctr = be16_to_cpu(cqe->wqe_index);
		wc->wr_id = srq->wrid[wqe_ctr];
		mlx4_ib_free_srq_wqe(srq, wqe_ctr);
	} else if (msrq) {
		srq = to_mibsrq(msrq);
		wqe_ctr = be16_to_cpu(cqe->wqe_index);
		wc->wr_id = srq->wrid[wqe_ctr];
		mlx4_ib_free_srq_wqe(srq, wqe_ctr);
	} else {
		wq	  = &(*cur_qp)->rq;
		tail	  = wq->tail & (wq->wqe_cnt - 1);
		wc->wr_id = wq->wrid[tail];
		++wq->tail;
	}

	if (unlikely(is_error)) {
		mlx4_ib_handle_error_cqe((struct mlx4_err_cqe *) cqe, wc);
		return 0;
	}

	wc->status = IB_WC_SUCCESS;

	if (is_send) {
		wc->wc_flags = 0;
		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_OPCODE_RDMA_WRITE_IMM:
			wc->wc_flags |= IB_WC_WITH_IMM;
		case MLX4_OPCODE_RDMA_WRITE:
			wc->opcode    = IB_WC_RDMA_WRITE;
			break;
		case MLX4_OPCODE_SEND_IMM:
			wc->wc_flags |= IB_WC_WITH_IMM;
		case MLX4_OPCODE_SEND:
		case MLX4_OPCODE_SEND_INVAL:
			wc->opcode    = IB_WC_SEND;
			break;
		case MLX4_OPCODE_RDMA_READ:
			wc->opcode    = IB_WC_RDMA_READ;
			wc->byte_len  = be32_to_cpu(cqe->byte_cnt);
			break;
		case MLX4_OPCODE_ATOMIC_CS:
			wc->opcode    = IB_WC_COMP_SWAP;
			wc->byte_len  = 8;
			break;
		case MLX4_OPCODE_ATOMIC_FA:
			wc->opcode    = IB_WC_FETCH_ADD;
			wc->byte_len  = 8;
			break;
		case MLX4_OPCODE_MASKED_ATOMIC_CS:
			wc->opcode    = IB_WC_MASKED_COMP_SWAP;
			wc->byte_len  = 8;
			break;
		case MLX4_OPCODE_MASKED_ATOMIC_FA:
			wc->opcode    = IB_WC_MASKED_FETCH_ADD;
			wc->byte_len  = 8;
			break;
		case MLX4_OPCODE_BIND_MW:
			wc->opcode    = IB_WC_BIND_MW;
			break;
		case MLX4_OPCODE_LSO:
			wc->opcode    = IB_WC_LSO;
			break;
		case MLX4_OPCODE_FMR:
			wc->opcode    = IB_WC_REG_MR;
			break;
		case MLX4_OPCODE_LOCAL_INVAL:
			wc->opcode    = IB_WC_LOCAL_INV;
			break;
		}
	} else {
		wc->byte_len = be32_to_cpu(cqe->byte_cnt);

		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_RECV_OPCODE_RDMA_WRITE_IMM:
			wc->opcode	= IB_WC_RECV_RDMA_WITH_IMM;
			wc->wc_flags	= IB_WC_WITH_IMM;
			wc->ex.imm_data = cqe->immed_rss_invalid;
			break;
		case MLX4_RECV_OPCODE_SEND_INVAL:
			wc->opcode	= IB_WC_RECV;
			wc->wc_flags	= IB_WC_WITH_INVALIDATE;
			wc->ex.invalidate_rkey = be32_to_cpu(cqe->immed_rss_invalid);
			break;
		case MLX4_RECV_OPCODE_SEND:
			wc->opcode   = IB_WC_RECV;
			wc->wc_flags = 0;
			break;
		case MLX4_RECV_OPCODE_SEND_IMM:
			wc->opcode	= IB_WC_RECV;
			wc->wc_flags	= IB_WC_WITH_IMM;
			wc->ex.imm_data = cqe->immed_rss_invalid;
			break;
		}

		is_eth = (rdma_port_get_link_layer(wc->qp->device,
						  (*cur_qp)->port) ==
			  IB_LINK_LAYER_ETHERNET);
		if (mlx4_is_mfunc(to_mdev(cq->ibcq.device)->dev)) {
			if ((*cur_qp)->mlx4_ib_qp_type &
			    (MLX4_IB_QPT_PROXY_SMI_OWNER |
			     MLX4_IB_QPT_PROXY_SMI | MLX4_IB_QPT_PROXY_GSI))
				return use_tunnel_data(*cur_qp, cq, wc, tail,
						       cqe, is_eth);
		}

		wc->slid	   = be16_to_cpu(cqe->rlid);
		g_mlpath_rqpn	   = be32_to_cpu(cqe->g_mlpath_rqpn);
		wc->src_qp	   = g_mlpath_rqpn & 0xffffff;
		wc->dlid_path_bits = (g_mlpath_rqpn >> 24) & 0x7f;
		wc->wc_flags	  |= g_mlpath_rqpn & 0x80000000 ? IB_WC_GRH : 0;
		wc->pkey_index     = be32_to_cpu(cqe->immed_rss_invalid) & 0x7f;
		wc->wc_flags	  |= mlx4_ib_ipoib_csum_ok(cqe->status,
					cqe->checksum) ? IB_WC_IP_CSUM_OK : 0;
		if (is_eth) {
			wc->sl  = be16_to_cpu(cqe->sl_vid) >> 13;
			if (be32_to_cpu(cqe->vlan_my_qpn) &
					MLX4_CQE_CVLAN_PRESENT_MASK) {
				wc->vlan_id = be16_to_cpu(cqe->sl_vid) &
					MLX4_CQE_VID_MASK;
			} else {
				wc->vlan_id = 0xffff;
			}
			memcpy(wc->smac, cqe->smac, ETH_ALEN);
			wc->wc_flags |= (IB_WC_WITH_VLAN | IB_WC_WITH_SMAC);
		} else {
			wc->sl  = be16_to_cpu(cqe->sl_vid) >> 12;
			wc->vlan_id = 0xffff;
		}
	}

	return 0;
}

int mlx4_ib_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct mlx4_ib_cq *cq = to_mcq(ibcq);
	struct mlx4_ib_qp *cur_qp = NULL;
	unsigned long flags;
	int npolled;
	int err = 0;
	struct mlx4_ib_dev *mdev = to_mdev(cq->ibcq.device);

	spin_lock_irqsave(&cq->lock, flags);
	if (mdev->dev->persist->state & MLX4_DEVICE_STATE_INTERNAL_ERROR) {
		mlx4_ib_poll_sw_comp(cq, num_entries, wc, &npolled);
		goto out;
	}

	for (npolled = 0; npolled < num_entries; ++npolled) {
		err = mlx4_ib_poll_one(cq, &cur_qp, wc + npolled);
		if (err)
			break;
	}

	mlx4_cq_set_ci(&cq->mcq);

out:
	spin_unlock_irqrestore(&cq->lock, flags);

	if (err == 0 || err == -EAGAIN)
		return npolled;
	else
		return err;
}

int mlx4_ib_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	mlx4_cq_arm(&to_mcq(ibcq)->mcq,
		    (flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED ?
		    MLX4_CQ_DB_REQ_NOT_SOL : MLX4_CQ_DB_REQ_NOT,
		    to_mdev(ibcq->device)->uar_map,
		    MLX4_GET_DOORBELL_LOCK(&to_mdev(ibcq->device)->uar_lock));

	return 0;
}

void __mlx4_ib_cq_clean(struct mlx4_ib_cq *cq, u32 qpn, struct mlx4_ib_srq *srq)
{
	u32 prod_index;
	int nfreed = 0;
	struct mlx4_cqe *cqe, *dest;
	u8 owner_bit;
	int cqe_inc = cq->buf.entry_size == 64 ? 1 : 0;

	/*
	 * First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	for (prod_index = cq->mcq.cons_index; get_sw_cqe(cq, prod_index); ++prod_index)
		if (prod_index == cq->mcq.cons_index + cq->ibcq.cqe)
			break;

	/*
	 * Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	while ((int) --prod_index - (int) cq->mcq.cons_index >= 0) {
		cqe = get_cqe(cq, prod_index & cq->ibcq.cqe);
		cqe += cqe_inc;

		if ((be32_to_cpu(cqe->vlan_my_qpn) & MLX4_CQE_QPN_MASK) == qpn) {
			if (srq && !(cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK))
				mlx4_ib_free_srq_wqe(srq, be16_to_cpu(cqe->wqe_index));
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe(cq, (prod_index + nfreed) & cq->ibcq.cqe);
			dest += cqe_inc;

			owner_bit = dest->owner_sr_opcode & MLX4_CQE_OWNER_MASK;
			memcpy(dest, cqe, sizeof *cqe);
			dest->owner_sr_opcode = owner_bit |
				(dest->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK);
		}
	}

	if (nfreed) {
		cq->mcq.cons_index += nfreed;
		/*
		 * Make sure update of buffer contents is done before
		 * updating consumer index.
		 */
		wmb();
		mlx4_cq_set_ci(&cq->mcq);
	}
}

void mlx4_ib_cq_clean(struct mlx4_ib_cq *cq, u32 qpn, struct mlx4_ib_srq *srq)
{
	spin_lock_irq(&cq->lock);
	__mlx4_ib_cq_clean(cq, qpn, srq);
	spin_unlock_irq(&cq->lock);
}
