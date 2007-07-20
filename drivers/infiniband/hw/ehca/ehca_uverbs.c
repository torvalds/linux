/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  userspace support verbs
 *
 *  Authors: Christoph Raisch <raisch@de.ibm.com>
 *           Hoang-Nam Nguyen <hnguyen@de.ibm.com>
 *           Heiko J Schick <schickhj@de.ibm.com>
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

#include <asm/current.h>

#include "ehca_classes.h"
#include "ehca_iverbs.h"
#include "ehca_mrmw.h"
#include "ehca_tools.h"
#include "hcp_if.h"

struct ib_ucontext *ehca_alloc_ucontext(struct ib_device *device,
					struct ib_udata *udata)
{
	struct ehca_ucontext *my_context;

	my_context = kzalloc(sizeof *my_context, GFP_KERNEL);
	if (!my_context) {
		ehca_err(device, "Out of memory device=%p", device);
		return ERR_PTR(-ENOMEM);
	}

	return &my_context->ib_ucontext;
}

int ehca_dealloc_ucontext(struct ib_ucontext *context)
{
	kfree(container_of(context, struct ehca_ucontext, ib_ucontext));
	return 0;
}

static void ehca_mm_open(struct vm_area_struct *vma)
{
	u32 *count = (u32 *)vma->vm_private_data;
	if (!count) {
		ehca_gen_err("Invalid vma struct vm_start=%lx vm_end=%lx",
			     vma->vm_start, vma->vm_end);
		return;
	}
	(*count)++;
	if (!(*count))
		ehca_gen_err("Use count overflow vm_start=%lx vm_end=%lx",
			     vma->vm_start, vma->vm_end);
	ehca_gen_dbg("vm_start=%lx vm_end=%lx count=%x",
		     vma->vm_start, vma->vm_end, *count);
}

static void ehca_mm_close(struct vm_area_struct *vma)
{
	u32 *count = (u32 *)vma->vm_private_data;
	if (!count) {
		ehca_gen_err("Invalid vma struct vm_start=%lx vm_end=%lx",
			     vma->vm_start, vma->vm_end);
		return;
	}
	(*count)--;
	ehca_gen_dbg("vm_start=%lx vm_end=%lx count=%x",
		     vma->vm_start, vma->vm_end, *count);
}

static struct vm_operations_struct vm_ops = {
	.open =	ehca_mm_open,
	.close = ehca_mm_close,
};

static int ehca_mmap_fw(struct vm_area_struct *vma, struct h_galpas *galpas,
			u32 *mm_count)
{
	int ret;
	u64 vsize, physical;

	vsize = vma->vm_end - vma->vm_start;
	if (vsize != EHCA_PAGESIZE) {
		ehca_gen_err("invalid vsize=%lx", vma->vm_end - vma->vm_start);
		return -EINVAL;
	}

	physical = galpas->user.fw_handle;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	ehca_gen_dbg("vsize=%lx physical=%lx", vsize, physical);
	/* VM_IO | VM_RESERVED are set by remap_pfn_range() */
	ret = remap_pfn_range(vma, vma->vm_start, physical >> PAGE_SHIFT,
			      vsize, vma->vm_page_prot);
	if (unlikely(ret)) {
		ehca_gen_err("remap_pfn_range() failed ret=%x", ret);
		return -ENOMEM;
	}

	vma->vm_private_data = mm_count;
	(*mm_count)++;
	vma->vm_ops = &vm_ops;

	return 0;
}

static int ehca_mmap_queue(struct vm_area_struct *vma, struct ipz_queue *queue,
			   u32 *mm_count)
{
	int ret;
	u64 start, ofs;
	struct page *page;

	vma->vm_flags |= VM_RESERVED;
	start = vma->vm_start;
	for (ofs = 0; ofs < queue->queue_length; ofs += PAGE_SIZE) {
		u64 virt_addr = (u64)ipz_qeit_calc(queue, ofs);
		page = virt_to_page(virt_addr);
		ret = vm_insert_page(vma, start, page);
		if (unlikely(ret)) {
			ehca_gen_err("vm_insert_page() failed rc=%x", ret);
			return ret;
		}
		start += PAGE_SIZE;
	}
	vma->vm_private_data = mm_count;
	(*mm_count)++;
	vma->vm_ops = &vm_ops;

	return 0;
}

static int ehca_mmap_cq(struct vm_area_struct *vma, struct ehca_cq *cq,
			u32 rsrc_type)
{
	int ret;

	switch (rsrc_type) {
	case 1: /* galpa fw handle */
		ehca_dbg(cq->ib_cq.device, "cq_num=%x fw", cq->cq_number);
		ret = ehca_mmap_fw(vma, &cq->galpas, &cq->mm_count_galpa);
		if (unlikely(ret)) {
			ehca_err(cq->ib_cq.device,
				 "ehca_mmap_fw() failed rc=%x cq_num=%x",
				 ret, cq->cq_number);
			return ret;
		}
		break;

	case 2: /* cq queue_addr */
		ehca_dbg(cq->ib_cq.device, "cq_num=%x queue", cq->cq_number);
		ret = ehca_mmap_queue(vma, &cq->ipz_queue, &cq->mm_count_queue);
		if (unlikely(ret)) {
			ehca_err(cq->ib_cq.device,
				 "ehca_mmap_queue() failed rc=%x cq_num=%x",
				 ret, cq->cq_number);
			return ret;
		}
		break;

	default:
		ehca_err(cq->ib_cq.device, "bad resource type=%x cq_num=%x",
			 rsrc_type, cq->cq_number);
		return -EINVAL;
	}

	return 0;
}

static int ehca_mmap_qp(struct vm_area_struct *vma, struct ehca_qp *qp,
			u32 rsrc_type)
{
	int ret;

	switch (rsrc_type) {
	case 1: /* galpa fw handle */
		ehca_dbg(qp->ib_qp.device, "qp_num=%x fw", qp->ib_qp.qp_num);
		ret = ehca_mmap_fw(vma, &qp->galpas, &qp->mm_count_galpa);
		if (unlikely(ret)) {
			ehca_err(qp->ib_qp.device,
				 "remap_pfn_range() failed ret=%x qp_num=%x",
				 ret, qp->ib_qp.qp_num);
			return -ENOMEM;
		}
		break;

	case 2: /* qp rqueue_addr */
		ehca_dbg(qp->ib_qp.device, "qp_num=%x rqueue",
			 qp->ib_qp.qp_num);
		ret = ehca_mmap_queue(vma, &qp->ipz_rqueue,
				      &qp->mm_count_rqueue);
		if (unlikely(ret)) {
			ehca_err(qp->ib_qp.device,
				 "ehca_mmap_queue(rq) failed rc=%x qp_num=%x",
				 ret, qp->ib_qp.qp_num);
			return ret;
		}
		break;

	case 3: /* qp squeue_addr */
		ehca_dbg(qp->ib_qp.device, "qp_num=%x squeue",
			 qp->ib_qp.qp_num);
		ret = ehca_mmap_queue(vma, &qp->ipz_squeue,
				      &qp->mm_count_squeue);
		if (unlikely(ret)) {
			ehca_err(qp->ib_qp.device,
				 "ehca_mmap_queue(sq) failed rc=%x qp_num=%x",
				 ret, qp->ib_qp.qp_num);
			return ret;
		}
		break;

	default:
		ehca_err(qp->ib_qp.device, "bad resource type=%x qp=num=%x",
			 rsrc_type, qp->ib_qp.qp_num);
		return -EINVAL;
	}

	return 0;
}

int ehca_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	u64 fileoffset = vma->vm_pgoff << PAGE_SHIFT;
	u32 idr_handle = fileoffset >> 32;
	u32 q_type = (fileoffset >> 28) & 0xF;	  /* CQ, QP,...        */
	u32 rsrc_type = (fileoffset >> 24) & 0xF; /* sq,rq,cmnd_window */
	u32 cur_pid = current->tgid;
	u32 ret;
	struct ehca_cq *cq;
	struct ehca_qp *qp;
	struct ehca_pd *pd;
	struct ib_uobject *uobject;

	switch (q_type) {
	case  1: /* CQ */
		read_lock(&ehca_cq_idr_lock);
		cq = idr_find(&ehca_cq_idr, idr_handle);
		read_unlock(&ehca_cq_idr_lock);

		/* make sure this mmap really belongs to the authorized user */
		if (!cq)
			return -EINVAL;

		if (cq->ownpid != cur_pid) {
			ehca_err(cq->ib_cq.device,
				 "Invalid caller pid=%x ownpid=%x",
				 cur_pid, cq->ownpid);
			return -ENOMEM;
		}

		if (!cq->ib_cq.uobject || cq->ib_cq.uobject->context != context)
			return -EINVAL;

		ret = ehca_mmap_cq(vma, cq, rsrc_type);
		if (unlikely(ret)) {
			ehca_err(cq->ib_cq.device,
				 "ehca_mmap_cq() failed rc=%x cq_num=%x",
				 ret, cq->cq_number);
			return ret;
		}
		break;

	case 2: /* QP */
		read_lock(&ehca_qp_idr_lock);
		qp = idr_find(&ehca_qp_idr, idr_handle);
		read_unlock(&ehca_qp_idr_lock);

		/* make sure this mmap really belongs to the authorized user */
		if (!qp)
			return -EINVAL;

		pd = container_of(qp->ib_qp.pd, struct ehca_pd, ib_pd);
		if (pd->ownpid != cur_pid) {
			ehca_err(qp->ib_qp.device,
				 "Invalid caller pid=%x ownpid=%x",
				 cur_pid, pd->ownpid);
			return -ENOMEM;
		}

		uobject = IS_SRQ(qp) ? qp->ib_srq.uobject : qp->ib_qp.uobject;
		if (!uobject || uobject->context != context)
			return -EINVAL;

		ret = ehca_mmap_qp(vma, qp, rsrc_type);
		if (unlikely(ret)) {
			ehca_err(qp->ib_qp.device,
				 "ehca_mmap_qp() failed rc=%x qp_num=%x",
				 ret, qp->ib_qp.qp_num);
			return ret;
		}
		break;

	default:
		ehca_gen_err("bad queue type %x", q_type);
		return -EINVAL;
	}

	return 0;
}
