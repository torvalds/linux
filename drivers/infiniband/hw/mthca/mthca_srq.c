/*
 * Copyright (c) 2005 Cisco Systems. All rights reserved.
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
 *
 * $Id: mthca_srq.c 3047 2005-08-10 03:59:35Z roland $
 */

#include <linux/slab.h>
#include <linux/string.h>

#include "mthca_dev.h"
#include "mthca_cmd.h"
#include "mthca_memfree.h"
#include "mthca_wqe.h"

enum {
	MTHCA_MAX_DIRECT_SRQ_SIZE = 4 * PAGE_SIZE
};

struct mthca_tavor_srq_context {
	__be64 wqe_base_ds;	/* low 6 bits is descriptor size */
	__be32 state_pd;
	__be32 lkey;
	__be32 uar;
	__be16 limit_watermark;
	__be16 wqe_cnt;
	u32    reserved[2];
};

struct mthca_arbel_srq_context {
	__be32 state_logsize_srqn;
	__be32 lkey;
	__be32 db_index;
	__be32 logstride_usrpage;
	__be64 wqe_base;
	__be32 eq_pd;
	__be16 limit_watermark;
	__be16 wqe_cnt;
	u16    reserved1;
	__be16 wqe_counter;
	u32    reserved2[3];
};

static void *get_wqe(struct mthca_srq *srq, int n)
{
	if (srq->is_direct)
		return srq->queue.direct.buf + (n << srq->wqe_shift);
	else
		return srq->queue.page_list[(n << srq->wqe_shift) >> PAGE_SHIFT].buf +
			((n << srq->wqe_shift) & (PAGE_SIZE - 1));
}

/*
 * Return a pointer to the location within a WQE that we're using as a
 * link when the WQE is in the free list.  We use the imm field
 * because in the Tavor case, posting a WQE may overwrite the next
 * segment of the previous WQE, but a receive WQE will never touch the
 * imm field.  This avoids corrupting our free list if the previous
 * WQE has already completed and been put on the free list when we
 * post the next WQE.
 */
static inline int *wqe_to_link(void *wqe)
{
	return (int *) (wqe + offsetof(struct mthca_next_seg, imm));
}

static void mthca_tavor_init_srq_context(struct mthca_dev *dev,
					 struct mthca_pd *pd,
					 struct mthca_srq *srq,
					 struct mthca_tavor_srq_context *context)
{
	memset(context, 0, sizeof *context);

	context->wqe_base_ds = cpu_to_be64(1 << (srq->wqe_shift - 4));
	context->state_pd    = cpu_to_be32(pd->pd_num);
	context->lkey        = cpu_to_be32(srq->mr.ibmr.lkey);

	if (pd->ibpd.uobject)
		context->uar =
			cpu_to_be32(to_mucontext(pd->ibpd.uobject->context)->uar.index);
	else
		context->uar = cpu_to_be32(dev->driver_uar.index);
}

static void mthca_arbel_init_srq_context(struct mthca_dev *dev,
					 struct mthca_pd *pd,
					 struct mthca_srq *srq,
					 struct mthca_arbel_srq_context *context)
{
	int logsize;

	memset(context, 0, sizeof *context);

	logsize = long_log2(srq->max) + srq->wqe_shift;
	context->state_logsize_srqn = cpu_to_be32(logsize << 24 | srq->srqn);
	context->lkey = cpu_to_be32(srq->mr.ibmr.lkey);
	context->db_index = cpu_to_be32(srq->db_index);
	context->logstride_usrpage = cpu_to_be32((srq->wqe_shift - 4) << 29);
	if (pd->ibpd.uobject)
		context->logstride_usrpage |=
			cpu_to_be32(to_mucontext(pd->ibpd.uobject->context)->uar.index);
	else
		context->logstride_usrpage |= cpu_to_be32(dev->driver_uar.index);
	context->eq_pd = cpu_to_be32(MTHCA_EQ_ASYNC << 24 | pd->pd_num);
}

static void mthca_free_srq_buf(struct mthca_dev *dev, struct mthca_srq *srq)
{
	mthca_buf_free(dev, srq->max << srq->wqe_shift, &srq->queue,
		       srq->is_direct, &srq->mr);
	kfree(srq->wrid);
}

static int mthca_alloc_srq_buf(struct mthca_dev *dev, struct mthca_pd *pd,
			       struct mthca_srq *srq)
{
	struct mthca_data_seg *scatter;
	void *wqe;
	int err;
	int i;

	if (pd->ibpd.uobject)
		return 0;

	srq->wrid = kmalloc(srq->max * sizeof (u64), GFP_KERNEL);
	if (!srq->wrid)
		return -ENOMEM;

	err = mthca_buf_alloc(dev, srq->max << srq->wqe_shift,
			      MTHCA_MAX_DIRECT_SRQ_SIZE,
			      &srq->queue, &srq->is_direct, pd, 1, &srq->mr);
	if (err) {
		kfree(srq->wrid);
		return err;
	}

	/*
	 * Now initialize the SRQ buffer so that all of the WQEs are
	 * linked into the list of free WQEs.  In addition, set the
	 * scatter list L_Keys to the sentry value of 0x100.
	 */
	for (i = 0; i < srq->max; ++i) {
		wqe = get_wqe(srq, i);

		*wqe_to_link(wqe) = i < srq->max - 1 ? i + 1 : -1;

		for (scatter = wqe + sizeof (struct mthca_next_seg);
		     (void *) scatter < wqe + (1 << srq->wqe_shift);
		     ++scatter)
			scatter->lkey = cpu_to_be32(MTHCA_INVAL_LKEY);
	}

	srq->last = get_wqe(srq, srq->max - 1);

	return 0;
}

int mthca_alloc_srq(struct mthca_dev *dev, struct mthca_pd *pd,
		    struct ib_srq_attr *attr, struct mthca_srq *srq)
{
	struct mthca_mailbox *mailbox;
	u8 status;
	int ds;
	int err;

	/* Sanity check SRQ size before proceeding */
	if (attr->max_wr  > dev->limits.max_srq_wqes ||
	    attr->max_sge > dev->limits.max_sg)
		return -EINVAL;

	srq->max      = attr->max_wr;
	srq->max_gs   = attr->max_sge;
	srq->counter  = 0;

	if (mthca_is_memfree(dev))
		srq->max = roundup_pow_of_two(srq->max + 1);

	ds = max(64UL,
		 roundup_pow_of_two(sizeof (struct mthca_next_seg) +
				    srq->max_gs * sizeof (struct mthca_data_seg)));

	if (!mthca_is_memfree(dev) && (ds > dev->limits.max_desc_sz))
		return -EINVAL;

	srq->wqe_shift = long_log2(ds);

	srq->srqn = mthca_alloc(&dev->srq_table.alloc);
	if (srq->srqn == -1)
		return -ENOMEM;

	if (mthca_is_memfree(dev)) {
		err = mthca_table_get(dev, dev->srq_table.table, srq->srqn);
		if (err)
			goto err_out;

		if (!pd->ibpd.uobject) {
			srq->db_index = mthca_alloc_db(dev, MTHCA_DB_TYPE_SRQ,
						       srq->srqn, &srq->db);
			if (srq->db_index < 0) {
				err = -ENOMEM;
				goto err_out_icm;
			}
		}
	}

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto err_out_db;
	}

	err = mthca_alloc_srq_buf(dev, pd, srq);
	if (err)
		goto err_out_mailbox;

	spin_lock_init(&srq->lock);
	atomic_set(&srq->refcount, 1);
	init_waitqueue_head(&srq->wait);

	if (mthca_is_memfree(dev))
		mthca_arbel_init_srq_context(dev, pd, srq, mailbox->buf);
	else
		mthca_tavor_init_srq_context(dev, pd, srq, mailbox->buf);

	err = mthca_SW2HW_SRQ(dev, mailbox, srq->srqn, &status);

	if (err) {
		mthca_warn(dev, "SW2HW_SRQ failed (%d)\n", err);
		goto err_out_free_buf;
	}
	if (status) {
		mthca_warn(dev, "SW2HW_SRQ returned status 0x%02x\n",
			   status);
		err = -EINVAL;
		goto err_out_free_buf;
	}

	spin_lock_irq(&dev->srq_table.lock);
	if (mthca_array_set(&dev->srq_table.srq,
			    srq->srqn & (dev->limits.num_srqs - 1),
			    srq)) {
		spin_unlock_irq(&dev->srq_table.lock);
		goto err_out_free_srq;
	}
	spin_unlock_irq(&dev->srq_table.lock);

	mthca_free_mailbox(dev, mailbox);

	srq->first_free = 0;
	srq->last_free  = srq->max - 1;

	attr->max_wr    = (mthca_is_memfree(dev)) ? srq->max - 1 : srq->max;
	attr->max_sge   = srq->max_gs;

	return 0;

err_out_free_srq:
	err = mthca_HW2SW_SRQ(dev, mailbox, srq->srqn, &status);
	if (err)
		mthca_warn(dev, "HW2SW_SRQ failed (%d)\n", err);
	else if (status)
		mthca_warn(dev, "HW2SW_SRQ returned status 0x%02x\n", status);

err_out_free_buf:
	if (!pd->ibpd.uobject)
		mthca_free_srq_buf(dev, srq);

err_out_mailbox:
	mthca_free_mailbox(dev, mailbox);

err_out_db:
	if (!pd->ibpd.uobject && mthca_is_memfree(dev))
		mthca_free_db(dev, MTHCA_DB_TYPE_SRQ, srq->db_index);

err_out_icm:
	mthca_table_put(dev, dev->srq_table.table, srq->srqn);

err_out:
	mthca_free(&dev->srq_table.alloc, srq->srqn);

	return err;
}

void mthca_free_srq(struct mthca_dev *dev, struct mthca_srq *srq)
{
	struct mthca_mailbox *mailbox;
	int err;
	u8 status;

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox)) {
		mthca_warn(dev, "No memory for mailbox to free SRQ.\n");
		return;
	}

	err = mthca_HW2SW_SRQ(dev, mailbox, srq->srqn, &status);
	if (err)
		mthca_warn(dev, "HW2SW_SRQ failed (%d)\n", err);
	else if (status)
		mthca_warn(dev, "HW2SW_SRQ returned status 0x%02x\n", status);

	spin_lock_irq(&dev->srq_table.lock);
	mthca_array_clear(&dev->srq_table.srq,
			  srq->srqn & (dev->limits.num_srqs - 1));
	spin_unlock_irq(&dev->srq_table.lock);

	atomic_dec(&srq->refcount);
	wait_event(srq->wait, !atomic_read(&srq->refcount));

	if (!srq->ibsrq.uobject) {
		mthca_free_srq_buf(dev, srq);
		if (mthca_is_memfree(dev))
			mthca_free_db(dev, MTHCA_DB_TYPE_SRQ, srq->db_index);
	}

	mthca_table_put(dev, dev->srq_table.table, srq->srqn);
	mthca_free(&dev->srq_table.alloc, srq->srqn);
	mthca_free_mailbox(dev, mailbox);
}

int mthca_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		     enum ib_srq_attr_mask attr_mask)
{
	struct mthca_dev *dev = to_mdev(ibsrq->device);
	struct mthca_srq *srq = to_msrq(ibsrq);
	int ret;
	u8 status;

	/* We don't support resizing SRQs (yet?) */
	if (attr_mask & IB_SRQ_MAX_WR)
		return -EINVAL;

	if (attr_mask & IB_SRQ_LIMIT) {
		if (attr->srq_limit > srq->max)
			return -EINVAL;
		ret = mthca_ARM_SRQ(dev, srq->srqn, attr->srq_limit, &status);
		if (ret)
			return ret;
		if (status)
			return -EINVAL;
	}

	return 0;
}

int mthca_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *srq_attr)
{
	struct mthca_dev *dev = to_mdev(ibsrq->device);
	struct mthca_srq *srq = to_msrq(ibsrq);
	struct mthca_mailbox *mailbox;
	struct mthca_arbel_srq_context *arbel_ctx;
	struct mthca_tavor_srq_context *tavor_ctx;
	u8 status;
	int err;

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	err = mthca_QUERY_SRQ(dev, srq->srqn, mailbox, &status);
	if (err)
		goto out;

	if (mthca_is_memfree(dev)) {
		arbel_ctx = mailbox->buf;
		srq_attr->srq_limit = be16_to_cpu(arbel_ctx->limit_watermark);
	} else {
		tavor_ctx = mailbox->buf;
		srq_attr->srq_limit = be16_to_cpu(tavor_ctx->limit_watermark);
	}

	srq_attr->max_wr  = (mthca_is_memfree(dev)) ? srq->max - 1 : srq->max;
	srq_attr->max_sge = srq->max_gs;

out:
	mthca_free_mailbox(dev, mailbox);

	return err;
}

void mthca_srq_event(struct mthca_dev *dev, u32 srqn,
		     enum ib_event_type event_type)
{
	struct mthca_srq *srq;
	struct ib_event event;

	spin_lock(&dev->srq_table.lock);
	srq = mthca_array_get(&dev->srq_table.srq, srqn & (dev->limits.num_srqs - 1));
	if (srq)
		atomic_inc(&srq->refcount);
	spin_unlock(&dev->srq_table.lock);

	if (!srq) {
		mthca_warn(dev, "Async event for bogus SRQ %08x\n", srqn);
		return;
	}

	if (!srq->ibsrq.event_handler)
		goto out;

	event.device      = &dev->ib_dev;
	event.event       = event_type;
	event.element.srq = &srq->ibsrq;
	srq->ibsrq.event_handler(&event, srq->ibsrq.srq_context);

out:
	if (atomic_dec_and_test(&srq->refcount))
		wake_up(&srq->wait);
}

/*
 * This function must be called with IRQs disabled.
 */
void mthca_free_srq_wqe(struct mthca_srq *srq, u32 wqe_addr)
{
	int ind;

	ind = wqe_addr >> srq->wqe_shift;

	spin_lock(&srq->lock);

	if (likely(srq->first_free >= 0))
		*wqe_to_link(get_wqe(srq, srq->last_free)) = ind;
	else
		srq->first_free = ind;

	*wqe_to_link(get_wqe(srq, ind)) = -1;
	srq->last_free = ind;

	spin_unlock(&srq->lock);
}

int mthca_tavor_post_srq_recv(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
			      struct ib_recv_wr **bad_wr)
{
	struct mthca_dev *dev = to_mdev(ibsrq->device);
	struct mthca_srq *srq = to_msrq(ibsrq);
	__be32 doorbell[2];
	unsigned long flags;
	int err = 0;
	int first_ind;
	int ind;
	int next_ind;
	int nreq;
	int i;
	void *wqe;
	void *prev_wqe;

	spin_lock_irqsave(&srq->lock, flags);

	first_ind = srq->first_free;

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (unlikely(nreq == MTHCA_TAVOR_MAX_WQES_PER_RECV_DB)) {
			nreq = 0;

			doorbell[0] = cpu_to_be32(first_ind << srq->wqe_shift);
			doorbell[1] = cpu_to_be32(srq->srqn << 8);

			/*
			 * Make sure that descriptors are written
			 * before doorbell is rung.
			 */
			wmb();

			mthca_write64(doorbell,
				      dev->kar + MTHCA_RECEIVE_DOORBELL,
				      MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));

			first_ind = srq->first_free;
		}

		ind = srq->first_free;

		if (ind < 0) {
			mthca_err(dev, "SRQ %06x full\n", srq->srqn);
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		wqe       = get_wqe(srq, ind);
		next_ind  = *wqe_to_link(wqe);

		if (next_ind < 0) {
			mthca_err(dev, "SRQ %06x full\n", srq->srqn);
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		prev_wqe  = srq->last;
		srq->last = wqe;

		((struct mthca_next_seg *) wqe)->nda_op = 0;
		((struct mthca_next_seg *) wqe)->ee_nds = 0;
		/* flags field will always remain 0 */

		wqe += sizeof (struct mthca_next_seg);

		if (unlikely(wr->num_sge > srq->max_gs)) {
			err = -EINVAL;
			*bad_wr = wr;
			srq->last = prev_wqe;
			break;
		}

		for (i = 0; i < wr->num_sge; ++i) {
			((struct mthca_data_seg *) wqe)->byte_count =
				cpu_to_be32(wr->sg_list[i].length);
			((struct mthca_data_seg *) wqe)->lkey =
				cpu_to_be32(wr->sg_list[i].lkey);
			((struct mthca_data_seg *) wqe)->addr =
				cpu_to_be64(wr->sg_list[i].addr);
			wqe += sizeof (struct mthca_data_seg);
		}

		if (i < srq->max_gs) {
			((struct mthca_data_seg *) wqe)->byte_count = 0;
			((struct mthca_data_seg *) wqe)->lkey = cpu_to_be32(MTHCA_INVAL_LKEY);
			((struct mthca_data_seg *) wqe)->addr = 0;
		}

		((struct mthca_next_seg *) prev_wqe)->nda_op =
			cpu_to_be32((ind << srq->wqe_shift) | 1);
		wmb();
		((struct mthca_next_seg *) prev_wqe)->ee_nds =
			cpu_to_be32(MTHCA_NEXT_DBD);

		srq->wrid[ind]  = wr->wr_id;
		srq->first_free = next_ind;
	}

	if (likely(nreq)) {
		doorbell[0] = cpu_to_be32(first_ind << srq->wqe_shift);
		doorbell[1] = cpu_to_be32((srq->srqn << 8) | nreq);

		/*
		 * Make sure that descriptors are written before
		 * doorbell is rung.
		 */
		wmb();

		mthca_write64(doorbell,
			      dev->kar + MTHCA_RECEIVE_DOORBELL,
			      MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));
	}

	spin_unlock_irqrestore(&srq->lock, flags);
	return err;
}

int mthca_arbel_post_srq_recv(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
			      struct ib_recv_wr **bad_wr)
{
	struct mthca_dev *dev = to_mdev(ibsrq->device);
	struct mthca_srq *srq = to_msrq(ibsrq);
	unsigned long flags;
	int err = 0;
	int ind;
	int next_ind;
	int nreq;
	int i;
	void *wqe;

	spin_lock_irqsave(&srq->lock, flags);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		ind = srq->first_free;

		if (ind < 0) {
			mthca_err(dev, "SRQ %06x full\n", srq->srqn);
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		wqe       = get_wqe(srq, ind);
		next_ind  = *wqe_to_link(wqe);

		if (next_ind < 0) {
			mthca_err(dev, "SRQ %06x full\n", srq->srqn);
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		((struct mthca_next_seg *) wqe)->nda_op =
			cpu_to_be32((next_ind << srq->wqe_shift) | 1);
		((struct mthca_next_seg *) wqe)->ee_nds = 0;
		/* flags field will always remain 0 */

		wqe += sizeof (struct mthca_next_seg);

		if (unlikely(wr->num_sge > srq->max_gs)) {
			err = -EINVAL;
			*bad_wr = wr;
			break;
		}

		for (i = 0; i < wr->num_sge; ++i) {
			((struct mthca_data_seg *) wqe)->byte_count =
				cpu_to_be32(wr->sg_list[i].length);
			((struct mthca_data_seg *) wqe)->lkey =
				cpu_to_be32(wr->sg_list[i].lkey);
			((struct mthca_data_seg *) wqe)->addr =
				cpu_to_be64(wr->sg_list[i].addr);
			wqe += sizeof (struct mthca_data_seg);
		}

		if (i < srq->max_gs) {
			((struct mthca_data_seg *) wqe)->byte_count = 0;
			((struct mthca_data_seg *) wqe)->lkey = cpu_to_be32(MTHCA_INVAL_LKEY);
			((struct mthca_data_seg *) wqe)->addr = 0;
		}

		srq->wrid[ind]  = wr->wr_id;
		srq->first_free = next_ind;
	}

	if (likely(nreq)) {
		srq->counter += nreq;

		/*
		 * Make sure that descriptors are written before
		 * we write doorbell record.
		 */
		wmb();
		*srq->db = cpu_to_be32(srq->counter);
	}

	spin_unlock_irqrestore(&srq->lock, flags);
	return err;
}

int __devinit mthca_init_srq_table(struct mthca_dev *dev)
{
	int err;

	if (!(dev->mthca_flags & MTHCA_FLAG_SRQ))
		return 0;

	spin_lock_init(&dev->srq_table.lock);

	err = mthca_alloc_init(&dev->srq_table.alloc,
			       dev->limits.num_srqs,
			       dev->limits.num_srqs - 1,
			       dev->limits.reserved_srqs);
	if (err)
		return err;

	err = mthca_array_init(&dev->srq_table.srq,
			       dev->limits.num_srqs);
	if (err)
		mthca_alloc_cleanup(&dev->srq_table.alloc);

	return err;
}

void mthca_cleanup_srq_table(struct mthca_dev *dev)
{
	if (!(dev->mthca_flags & MTHCA_FLAG_SRQ))
		return;

	mthca_array_cleanup(&dev->srq_table.srq, dev->limits.num_srqs);
	mthca_alloc_cleanup(&dev->srq_table.alloc);
}
