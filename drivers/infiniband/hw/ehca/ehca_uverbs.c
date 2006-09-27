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

struct page *ehca_nopage(struct vm_area_struct *vma,
			 unsigned long address, int *type)
{
	struct page *mypage = NULL;
	u64 fileoffset = vma->vm_pgoff << PAGE_SHIFT;
	u32 idr_handle = fileoffset >> 32;
	u32 q_type = (fileoffset >> 28) & 0xF;	  /* CQ, QP,...        */
	u32 rsrc_type = (fileoffset >> 24) & 0xF; /* sq,rq,cmnd_window */
	u32 cur_pid = current->tgid;
	unsigned long flags;
	struct ehca_cq *cq;
	struct ehca_qp *qp;
	struct ehca_pd *pd;
	u64 offset;
	void *vaddr;

	switch (q_type) {
	case 1: /* CQ */
		spin_lock_irqsave(&ehca_cq_idr_lock, flags);
		cq = idr_find(&ehca_cq_idr, idr_handle);
		spin_unlock_irqrestore(&ehca_cq_idr_lock, flags);

		/* make sure this mmap really belongs to the authorized user */
		if (!cq) {
			ehca_gen_err("cq is NULL ret=NOPAGE_SIGBUS");
			return NOPAGE_SIGBUS;
		}

		if (cq->ownpid != cur_pid) {
			ehca_err(cq->ib_cq.device,
				 "Invalid caller pid=%x ownpid=%x",
				 cur_pid, cq->ownpid);
			return NOPAGE_SIGBUS;
		}

		if (rsrc_type == 2) {
			ehca_dbg(cq->ib_cq.device, "cq=%p cq queuearea", cq);
			offset = address - vma->vm_start;
			vaddr = ipz_qeit_calc(&cq->ipz_queue, offset);
			ehca_dbg(cq->ib_cq.device, "offset=%lx vaddr=%p",
				 offset, vaddr);
			mypage = virt_to_page(vaddr);
		}
		break;

	case 2: /* QP */
		spin_lock_irqsave(&ehca_qp_idr_lock, flags);
		qp = idr_find(&ehca_qp_idr, idr_handle);
		spin_unlock_irqrestore(&ehca_qp_idr_lock, flags);

		/* make sure this mmap really belongs to the authorized user */
		if (!qp) {
			ehca_gen_err("qp is NULL ret=NOPAGE_SIGBUS");
			return NOPAGE_SIGBUS;
		}

		pd = container_of(qp->ib_qp.pd, struct ehca_pd, ib_pd);
		if (pd->ownpid != cur_pid) {
			ehca_err(qp->ib_qp.device,
				 "Invalid caller pid=%x ownpid=%x",
				 cur_pid, pd->ownpid);
			return NOPAGE_SIGBUS;
		}

		if (rsrc_type == 2) {	/* rqueue */
			ehca_dbg(qp->ib_qp.device, "qp=%p qp rqueuearea", qp);
			offset = address - vma->vm_start;
			vaddr = ipz_qeit_calc(&qp->ipz_rqueue, offset);
			ehca_dbg(qp->ib_qp.device, "offset=%lx vaddr=%p",
				 offset, vaddr);
			mypage = virt_to_page(vaddr);
		} else if (rsrc_type == 3) {	/* squeue */
			ehca_dbg(qp->ib_qp.device, "qp=%p qp squeuearea", qp);
			offset = address - vma->vm_start;
			vaddr = ipz_qeit_calc(&qp->ipz_squeue, offset);
			ehca_dbg(qp->ib_qp.device, "offset=%lx vaddr=%p",
				 offset, vaddr);
			mypage = virt_to_page(vaddr);
		}
		break;

	default:
		ehca_gen_err("bad queue type %x", q_type);
		return NOPAGE_SIGBUS;
	}

	if (!mypage) {
		ehca_gen_err("Invalid page adr==NULL ret=NOPAGE_SIGBUS");
		return NOPAGE_SIGBUS;
	}
	get_page(mypage);

	return mypage;
}

static struct vm_operations_struct ehcau_vm_ops = {
	.nopage = ehca_nopage,
};

int ehca_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	u64 fileoffset = vma->vm_pgoff << PAGE_SHIFT;
	u32 idr_handle = fileoffset >> 32;
	u32 q_type = (fileoffset >> 28) & 0xF;	  /* CQ, QP,...        */
	u32 rsrc_type = (fileoffset >> 24) & 0xF; /* sq,rq,cmnd_window */
	u32 cur_pid = current->tgid;
	u32 ret;
	u64 vsize, physical;
	unsigned long flags;
	struct ehca_cq *cq;
	struct ehca_qp *qp;
	struct ehca_pd *pd;

	switch (q_type) {
	case  1: /* CQ */
		spin_lock_irqsave(&ehca_cq_idr_lock, flags);
		cq = idr_find(&ehca_cq_idr, idr_handle);
		spin_unlock_irqrestore(&ehca_cq_idr_lock, flags);

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

		switch (rsrc_type) {
		case 1: /* galpa fw handle */
			ehca_dbg(cq->ib_cq.device, "cq=%p cq triggerarea", cq);
			vma->vm_flags |= VM_RESERVED;
			vsize = vma->vm_end - vma->vm_start;
			if (vsize != EHCA_PAGESIZE) {
				ehca_err(cq->ib_cq.device, "invalid vsize=%lx",
					 vma->vm_end - vma->vm_start);
				return -EINVAL;
			}

			physical = cq->galpas.user.fw_handle;
			vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
			vma->vm_flags |= VM_IO | VM_RESERVED;

			ehca_dbg(cq->ib_cq.device,
				 "vsize=%lx physical=%lx", vsize, physical);
			ret = remap_pfn_range(vma, vma->vm_start,
					      physical >> PAGE_SHIFT, vsize,
					      vma->vm_page_prot);
			if (ret) {
				ehca_err(cq->ib_cq.device,
					 "remap_pfn_range() failed ret=%x",
					 ret);
				return -ENOMEM;
			}
			break;

		case 2: /* cq queue_addr */
			ehca_dbg(cq->ib_cq.device, "cq=%p cq q_addr", cq);
			vma->vm_flags |= VM_RESERVED;
			vma->vm_ops = &ehcau_vm_ops;
			break;

		default:
			ehca_err(cq->ib_cq.device, "bad resource type %x",
				 rsrc_type);
			return -EINVAL;
		}
		break;

	case 2: /* QP */
		spin_lock_irqsave(&ehca_qp_idr_lock, flags);
		qp = idr_find(&ehca_qp_idr, idr_handle);
		spin_unlock_irqrestore(&ehca_qp_idr_lock, flags);

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

		if (!qp->ib_qp.uobject || qp->ib_qp.uobject->context != context)
			return -EINVAL;

		switch (rsrc_type) {
		case 1: /* galpa fw handle */
			ehca_dbg(qp->ib_qp.device, "qp=%p qp triggerarea", qp);
			vma->vm_flags |= VM_RESERVED;
			vsize = vma->vm_end - vma->vm_start;
			if (vsize != EHCA_PAGESIZE) {
				ehca_err(qp->ib_qp.device, "invalid vsize=%lx",
					 vma->vm_end - vma->vm_start);
				return -EINVAL;
			}

			physical = qp->galpas.user.fw_handle;
			vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
			vma->vm_flags |= VM_IO | VM_RESERVED;

			ehca_dbg(qp->ib_qp.device, "vsize=%lx physical=%lx",
				 vsize, physical);
			ret = remap_pfn_range(vma, vma->vm_start,
					      physical >> PAGE_SHIFT, vsize,
					      vma->vm_page_prot);
			if (ret) {
				ehca_err(qp->ib_qp.device,
					 "remap_pfn_range() failed ret=%x",
					 ret);
				return -ENOMEM;
			}
			break;

		case 2: /* qp rqueue_addr */
			ehca_dbg(qp->ib_qp.device, "qp=%p qp rqueue_addr", qp);
			vma->vm_flags |= VM_RESERVED;
			vma->vm_ops = &ehcau_vm_ops;
			break;

		case 3: /* qp squeue_addr */
			ehca_dbg(qp->ib_qp.device, "qp=%p qp squeue_addr", qp);
			vma->vm_flags |= VM_RESERVED;
			vma->vm_ops = &ehcau_vm_ops;
			break;

		default:
			ehca_err(qp->ib_qp.device, "bad resource type %x",
				 rsrc_type);
			return -EINVAL;
		}
		break;

	default:
		ehca_gen_err("bad queue type %x", q_type);
		return -EINVAL;
	}

	return 0;
}

int ehca_mmap_nopage(u64 foffset, u64 length, void **mapped,
		     struct vm_area_struct **vma)
{
	down_write(&current->mm->mmap_sem);
	*mapped = (void*)do_mmap(NULL,0, length, PROT_WRITE,
				 MAP_SHARED | MAP_ANONYMOUS,
				 foffset);
	up_write(&current->mm->mmap_sem);
	if (!(*mapped)) {
		ehca_gen_err("couldn't mmap foffset=%lx length=%lx",
			     foffset, length);
		return -EINVAL;
	}

	*vma = find_vma(current->mm, (u64)*mapped);
	if (!(*vma)) {
		down_write(&current->mm->mmap_sem);
		do_munmap(current->mm, 0, length);
		up_write(&current->mm->mmap_sem);
		ehca_gen_err("couldn't find vma queue=%p", *mapped);
		return -EINVAL;
	}
	(*vma)->vm_flags |= VM_RESERVED;
	(*vma)->vm_ops = &ehcau_vm_ops;

	return 0;
}

int ehca_mmap_register(u64 physical, void **mapped,
		       struct vm_area_struct **vma)
{
	int ret;
	unsigned long vsize;
	/* ehca hw supports only 4k page */
	ret = ehca_mmap_nopage(0, EHCA_PAGESIZE, mapped, vma);
	if (ret) {
		ehca_gen_err("could'nt mmap physical=%lx", physical);
		return ret;
	}

	(*vma)->vm_flags |= VM_RESERVED;
	vsize = (*vma)->vm_end - (*vma)->vm_start;
	if (vsize != EHCA_PAGESIZE) {
		ehca_gen_err("invalid vsize=%lx",
			     (*vma)->vm_end - (*vma)->vm_start);
		return -EINVAL;
	}

	(*vma)->vm_page_prot = pgprot_noncached((*vma)->vm_page_prot);
	(*vma)->vm_flags |= VM_IO | VM_RESERVED;

	ret = remap_pfn_range((*vma), (*vma)->vm_start,
			      physical >> PAGE_SHIFT, vsize,
			      (*vma)->vm_page_prot);
	if (ret) {
		ehca_gen_err("remap_pfn_range() failed ret=%x", ret);
		return -ENOMEM;
	}

	return 0;

}

int ehca_munmap(unsigned long addr, size_t len) {
	int ret = 0;
	struct mm_struct *mm = current->mm;
	if (mm) {
		down_write(&mm->mmap_sem);
		ret = do_munmap(mm, addr, len);
		up_write(&mm->mmap_sem);
	}
	return ret;
}
