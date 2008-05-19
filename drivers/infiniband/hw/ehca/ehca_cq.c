/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  Completion queue handling
 *
 *  Authors: Waleri Fomin <fomin@de.ibm.com>
 *           Khadija Souissi <souissi@de.ibm.com>
 *           Reinhard Ernst <rernst@de.ibm.com>
 *           Heiko J Schick <schickhj@de.ibm.com>
 *           Hoang-Nam Nguyen <hnguyen@de.ibm.com>
 *
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ehca_iverbs.h"
#include "ehca_classes.h"
#include "ehca_irq.h"
#include "hcp_if.h"

static struct kmem_cache *cq_cache;

int ehca_cq_assign_qp(struct ehca_cq *cq, struct ehca_qp *qp)
{
	unsigned int qp_num = qp->real_qp_num;
	unsigned int key = qp_num & (QP_HASHTAB_LEN-1);
	unsigned long flags;

	spin_lock_irqsave(&cq->spinlock, flags);
	hlist_add_head(&qp->list_entries, &cq->qp_hashtab[key]);
	spin_unlock_irqrestore(&cq->spinlock, flags);

	ehca_dbg(cq->ib_cq.device, "cq_num=%x real_qp_num=%x",
		 cq->cq_number, qp_num);

	return 0;
}

int ehca_cq_unassign_qp(struct ehca_cq *cq, unsigned int real_qp_num)
{
	int ret = -EINVAL;
	unsigned int key = real_qp_num & (QP_HASHTAB_LEN-1);
	struct hlist_node *iter;
	struct ehca_qp *qp;
	unsigned long flags;

	spin_lock_irqsave(&cq->spinlock, flags);
	hlist_for_each(iter, &cq->qp_hashtab[key]) {
		qp = hlist_entry(iter, struct ehca_qp, list_entries);
		if (qp->real_qp_num == real_qp_num) {
			hlist_del(iter);
			ehca_dbg(cq->ib_cq.device,
				 "removed qp from cq .cq_num=%x real_qp_num=%x",
				 cq->cq_number, real_qp_num);
			ret = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&cq->spinlock, flags);
	if (ret)
		ehca_err(cq->ib_cq.device,
			 "qp not found cq_num=%x real_qp_num=%x",
			 cq->cq_number, real_qp_num);

	return ret;
}

struct ehca_qp *ehca_cq_get_qp(struct ehca_cq *cq, int real_qp_num)
{
	struct ehca_qp *ret = NULL;
	unsigned int key = real_qp_num & (QP_HASHTAB_LEN-1);
	struct hlist_node *iter;
	struct ehca_qp *qp;
	hlist_for_each(iter, &cq->qp_hashtab[key]) {
		qp = hlist_entry(iter, struct ehca_qp, list_entries);
		if (qp->real_qp_num == real_qp_num) {
			ret = qp;
			break;
		}
	}
	return ret;
}

struct ib_cq *ehca_create_cq(struct ib_device *device, int cqe, int comp_vector,
			     struct ib_ucontext *context,
			     struct ib_udata *udata)
{
	static const u32 additional_cqe = 20;
	struct ib_cq *cq;
	struct ehca_cq *my_cq;
	struct ehca_shca *shca =
		container_of(device, struct ehca_shca, ib_device);
	struct ipz_adapter_handle adapter_handle;
	struct ehca_alloc_cq_parms param; /* h_call's out parameters */
	struct h_galpa gal;
	void *vpage;
	u32 counter;
	u64 rpage, cqx_fec, h_ret;
	int ipz_rc, ret, i;
	unsigned long flags;

	if (cqe >= 0xFFFFFFFF - 64 - additional_cqe)
		return ERR_PTR(-EINVAL);

	if (!atomic_add_unless(&shca->num_cqs, 1, ehca_max_cq)) {
		ehca_err(device, "Unable to create CQ, max number of %i "
			"CQs reached.", ehca_max_cq);
		ehca_err(device, "To increase the maximum number of CQs "
			"use the number_of_cqs module parameter.\n");
		return ERR_PTR(-ENOSPC);
	}

	my_cq = kmem_cache_zalloc(cq_cache, GFP_KERNEL);
	if (!my_cq) {
		ehca_err(device, "Out of memory for ehca_cq struct device=%p",
			 device);
		atomic_dec(&shca->num_cqs);
		return ERR_PTR(-ENOMEM);
	}

	memset(&param, 0, sizeof(struct ehca_alloc_cq_parms));

	spin_lock_init(&my_cq->spinlock);
	spin_lock_init(&my_cq->cb_lock);
	spin_lock_init(&my_cq->task_lock);
	atomic_set(&my_cq->nr_events, 0);
	init_waitqueue_head(&my_cq->wait_completion);

	cq = &my_cq->ib_cq;

	adapter_handle = shca->ipz_hca_handle;
	param.eq_handle = shca->eq.ipz_eq_handle;

	do {
		if (!idr_pre_get(&ehca_cq_idr, GFP_KERNEL)) {
			cq = ERR_PTR(-ENOMEM);
			ehca_err(device, "Can't reserve idr nr. device=%p",
				 device);
			goto create_cq_exit1;
		}

		write_lock_irqsave(&ehca_cq_idr_lock, flags);
		ret = idr_get_new(&ehca_cq_idr, my_cq, &my_cq->token);
		write_unlock_irqrestore(&ehca_cq_idr_lock, flags);
	} while (ret == -EAGAIN);

	if (ret) {
		cq = ERR_PTR(-ENOMEM);
		ehca_err(device, "Can't allocate new idr entry. device=%p",
			 device);
		goto create_cq_exit1;
	}

	if (my_cq->token > 0x1FFFFFF) {
		cq = ERR_PTR(-ENOMEM);
		ehca_err(device, "Invalid number of cq. device=%p", device);
		goto create_cq_exit2;
	}

	/*
	 * CQs maximum depth is 4GB-64, but we need additional 20 as buffer
	 * for receiving errors CQEs.
	 */
	param.nr_cqe = cqe + additional_cqe;
	h_ret = hipz_h_alloc_resource_cq(adapter_handle, my_cq, &param);

	if (h_ret != H_SUCCESS) {
		ehca_err(device, "hipz_h_alloc_resource_cq() failed "
			 "h_ret=%li device=%p", h_ret, device);
		cq = ERR_PTR(ehca2ib_return_code(h_ret));
		goto create_cq_exit2;
	}

	ipz_rc = ipz_queue_ctor(NULL, &my_cq->ipz_queue, param.act_pages,
				EHCA_PAGESIZE, sizeof(struct ehca_cqe), 0, 0);
	if (!ipz_rc) {
		ehca_err(device, "ipz_queue_ctor() failed ipz_rc=%i device=%p",
			 ipz_rc, device);
		cq = ERR_PTR(-EINVAL);
		goto create_cq_exit3;
	}

	for (counter = 0; counter < param.act_pages; counter++) {
		vpage = ipz_qpageit_get_inc(&my_cq->ipz_queue);
		if (!vpage) {
			ehca_err(device, "ipz_qpageit_get_inc() "
				 "returns NULL device=%p", device);
			cq = ERR_PTR(-EAGAIN);
			goto create_cq_exit4;
		}
		rpage = virt_to_abs(vpage);

		h_ret = hipz_h_register_rpage_cq(adapter_handle,
						 my_cq->ipz_cq_handle,
						 &my_cq->pf,
						 0,
						 0,
						 rpage,
						 1,
						 my_cq->galpas.
						 kernel);

		if (h_ret < H_SUCCESS) {
			ehca_err(device, "hipz_h_register_rpage_cq() failed "
				 "ehca_cq=%p cq_num=%x h_ret=%li counter=%i "
				 "act_pages=%i", my_cq, my_cq->cq_number,
				 h_ret, counter, param.act_pages);
			cq = ERR_PTR(-EINVAL);
			goto create_cq_exit4;
		}

		if (counter == (param.act_pages - 1)) {
			vpage = ipz_qpageit_get_inc(&my_cq->ipz_queue);
			if ((h_ret != H_SUCCESS) || vpage) {
				ehca_err(device, "Registration of pages not "
					 "complete ehca_cq=%p cq_num=%x "
					 "h_ret=%li", my_cq, my_cq->cq_number,
					 h_ret);
				cq = ERR_PTR(-EAGAIN);
				goto create_cq_exit4;
			}
		} else {
			if (h_ret != H_PAGE_REGISTERED) {
				ehca_err(device, "Registration of page failed "
					 "ehca_cq=%p cq_num=%x h_ret=%li "
					 "counter=%i act_pages=%i",
					 my_cq, my_cq->cq_number,
					 h_ret, counter, param.act_pages);
				cq = ERR_PTR(-ENOMEM);
				goto create_cq_exit4;
			}
		}
	}

	ipz_qeit_reset(&my_cq->ipz_queue);

	gal = my_cq->galpas.kernel;
	cqx_fec = hipz_galpa_load(gal, CQTEMM_OFFSET(cqx_fec));
	ehca_dbg(device, "ehca_cq=%p cq_num=%x CQX_FEC=%lx",
		 my_cq, my_cq->cq_number, cqx_fec);

	my_cq->ib_cq.cqe = my_cq->nr_of_entries =
		param.act_nr_of_entries - additional_cqe;
	my_cq->cq_number = (my_cq->ipz_cq_handle.handle) & 0xffff;

	for (i = 0; i < QP_HASHTAB_LEN; i++)
		INIT_HLIST_HEAD(&my_cq->qp_hashtab[i]);

	if (context) {
		struct ipz_queue *ipz_queue = &my_cq->ipz_queue;
		struct ehca_create_cq_resp resp;
		memset(&resp, 0, sizeof(resp));
		resp.cq_number = my_cq->cq_number;
		resp.token = my_cq->token;
		resp.ipz_queue.qe_size = ipz_queue->qe_size;
		resp.ipz_queue.act_nr_of_sg = ipz_queue->act_nr_of_sg;
		resp.ipz_queue.queue_length = ipz_queue->queue_length;
		resp.ipz_queue.pagesize = ipz_queue->pagesize;
		resp.ipz_queue.toggle_state = ipz_queue->toggle_state;
		resp.fw_handle_ofs = (u32)
			(my_cq->galpas.user.fw_handle & (PAGE_SIZE - 1));
		if (ib_copy_to_udata(udata, &resp, sizeof(resp))) {
			ehca_err(device, "Copy to udata failed.");
			goto create_cq_exit4;
		}
	}

	return cq;

create_cq_exit4:
	ipz_queue_dtor(NULL, &my_cq->ipz_queue);

create_cq_exit3:
	h_ret = hipz_h_destroy_cq(adapter_handle, my_cq, 1);
	if (h_ret != H_SUCCESS)
		ehca_err(device, "hipz_h_destroy_cq() failed ehca_cq=%p "
			 "cq_num=%x h_ret=%li", my_cq, my_cq->cq_number, h_ret);

create_cq_exit2:
	write_lock_irqsave(&ehca_cq_idr_lock, flags);
	idr_remove(&ehca_cq_idr, my_cq->token);
	write_unlock_irqrestore(&ehca_cq_idr_lock, flags);

create_cq_exit1:
	kmem_cache_free(cq_cache, my_cq);

	atomic_dec(&shca->num_cqs);
	return cq;
}

int ehca_destroy_cq(struct ib_cq *cq)
{
	u64 h_ret;
	struct ehca_cq *my_cq = container_of(cq, struct ehca_cq, ib_cq);
	int cq_num = my_cq->cq_number;
	struct ib_device *device = cq->device;
	struct ehca_shca *shca = container_of(device, struct ehca_shca,
					      ib_device);
	struct ipz_adapter_handle adapter_handle = shca->ipz_hca_handle;
	unsigned long flags;

	if (cq->uobject) {
		if (my_cq->mm_count_galpa || my_cq->mm_count_queue) {
			ehca_err(device, "Resources still referenced in "
				 "user space cq_num=%x", my_cq->cq_number);
			return -EINVAL;
		}
	}

	/*
	 * remove the CQ from the idr first to make sure
	 * no more interrupt tasklets will touch this CQ
	 */
	write_lock_irqsave(&ehca_cq_idr_lock, flags);
	idr_remove(&ehca_cq_idr, my_cq->token);
	write_unlock_irqrestore(&ehca_cq_idr_lock, flags);

	/* now wait until all pending events have completed */
	wait_event(my_cq->wait_completion, !atomic_read(&my_cq->nr_events));

	/* nobody's using our CQ any longer -- we can destroy it */
	h_ret = hipz_h_destroy_cq(adapter_handle, my_cq, 0);
	if (h_ret == H_R_STATE) {
		/* cq in err: read err data and destroy it forcibly */
		ehca_dbg(device, "ehca_cq=%p cq_num=%x ressource=%lx in err "
			 "state. Try to delete it forcibly.",
			 my_cq, cq_num, my_cq->ipz_cq_handle.handle);
		ehca_error_data(shca, my_cq, my_cq->ipz_cq_handle.handle);
		h_ret = hipz_h_destroy_cq(adapter_handle, my_cq, 1);
		if (h_ret == H_SUCCESS)
			ehca_dbg(device, "cq_num=%x deleted successfully.",
				 cq_num);
	}
	if (h_ret != H_SUCCESS) {
		ehca_err(device, "hipz_h_destroy_cq() failed h_ret=%li "
			 "ehca_cq=%p cq_num=%x", h_ret, my_cq, cq_num);
		return ehca2ib_return_code(h_ret);
	}
	ipz_queue_dtor(NULL, &my_cq->ipz_queue);
	kmem_cache_free(cq_cache, my_cq);

	atomic_dec(&shca->num_cqs);
	return 0;
}

int ehca_resize_cq(struct ib_cq *cq, int cqe, struct ib_udata *udata)
{
	/* TODO: proper resize needs to be done */
	ehca_err(cq->device, "not implemented yet");

	return -EFAULT;
}

int ehca_init_cq_cache(void)
{
	cq_cache = kmem_cache_create("ehca_cache_cq",
				     sizeof(struct ehca_cq), 0,
				     SLAB_HWCACHE_ALIGN,
				     NULL);
	if (!cq_cache)
		return -ENOMEM;
	return 0;
}

void ehca_cleanup_cq_cache(void)
{
	if (cq_cache)
		kmem_cache_destroy(cq_cache);
}
