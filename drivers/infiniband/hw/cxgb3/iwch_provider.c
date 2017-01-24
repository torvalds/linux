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
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <linux/inetdevice.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>

#include "cxio_hal.h"
#include "iwch.h"
#include "iwch_provider.h"
#include "iwch_cm.h"
#include <rdma/cxgb3-abi.h>
#include "common.h"

static struct ib_ah *iwch_ah_create(struct ib_pd *pd,
				    struct ib_ah_attr *ah_attr,
				    struct ib_udata *udata)
{
	return ERR_PTR(-ENOSYS);
}

static int iwch_ah_destroy(struct ib_ah *ah)
{
	return -ENOSYS;
}

static int iwch_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	return -ENOSYS;
}

static int iwch_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	return -ENOSYS;
}

static int iwch_process_mad(struct ib_device *ibdev,
			    int mad_flags,
			    u8 port_num,
			    const struct ib_wc *in_wc,
			    const struct ib_grh *in_grh,
			    const struct ib_mad_hdr *in_mad,
			    size_t in_mad_size,
			    struct ib_mad_hdr *out_mad,
			    size_t *out_mad_size,
			    u16 *out_mad_pkey_index)
{
	return -ENOSYS;
}

static int iwch_dealloc_ucontext(struct ib_ucontext *context)
{
	struct iwch_dev *rhp = to_iwch_dev(context->device);
	struct iwch_ucontext *ucontext = to_iwch_ucontext(context);
	struct iwch_mm_entry *mm, *tmp;

	PDBG("%s context %p\n", __func__, context);
	list_for_each_entry_safe(mm, tmp, &ucontext->mmaps, entry)
		kfree(mm);
	cxio_release_ucontext(&rhp->rdev, &ucontext->uctx);
	kfree(ucontext);
	return 0;
}

static struct ib_ucontext *iwch_alloc_ucontext(struct ib_device *ibdev,
					struct ib_udata *udata)
{
	struct iwch_ucontext *context;
	struct iwch_dev *rhp = to_iwch_dev(ibdev);

	PDBG("%s ibdev %p\n", __func__, ibdev);
	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return ERR_PTR(-ENOMEM);
	cxio_init_ucontext(&rhp->rdev, &context->uctx);
	INIT_LIST_HEAD(&context->mmaps);
	spin_lock_init(&context->mmap_lock);
	return &context->ibucontext;
}

static int iwch_destroy_cq(struct ib_cq *ib_cq)
{
	struct iwch_cq *chp;

	PDBG("%s ib_cq %p\n", __func__, ib_cq);
	chp = to_iwch_cq(ib_cq);

	remove_handle(chp->rhp, &chp->rhp->cqidr, chp->cq.cqid);
	atomic_dec(&chp->refcnt);
	wait_event(chp->wait, !atomic_read(&chp->refcnt));

	cxio_destroy_cq(&chp->rhp->rdev, &chp->cq);
	kfree(chp);
	return 0;
}

static struct ib_cq *iwch_create_cq(struct ib_device *ibdev,
				    const struct ib_cq_init_attr *attr,
				    struct ib_ucontext *ib_context,
				    struct ib_udata *udata)
{
	int entries = attr->cqe;
	struct iwch_dev *rhp;
	struct iwch_cq *chp;
	struct iwch_create_cq_resp uresp;
	struct iwch_create_cq_req ureq;
	struct iwch_ucontext *ucontext = NULL;
	static int warned;
	size_t resplen;

	PDBG("%s ib_dev %p entries %d\n", __func__, ibdev, entries);
	if (attr->flags)
		return ERR_PTR(-EINVAL);

	rhp = to_iwch_dev(ibdev);
	chp = kzalloc(sizeof(*chp), GFP_KERNEL);
	if (!chp)
		return ERR_PTR(-ENOMEM);

	if (ib_context) {
		ucontext = to_iwch_ucontext(ib_context);
		if (!t3a_device(rhp)) {
			if (ib_copy_from_udata(&ureq, udata, sizeof (ureq))) {
				kfree(chp);
				return ERR_PTR(-EFAULT);
			}
			chp->user_rptr_addr = (u32 __user *)(unsigned long)ureq.user_rptr_addr;
		}
	}

	if (t3a_device(rhp)) {

		/*
		 * T3A: Add some fluff to handle extra CQEs inserted
		 * for various errors.
		 * Additional CQE possibilities:
		 *      TERMINATE,
		 *      incoming RDMA WRITE Failures
		 *      incoming RDMA READ REQUEST FAILUREs
		 * NOTE: We cannot ensure the CQ won't overflow.
		 */
		entries += 16;
	}
	entries = roundup_pow_of_two(entries);
	chp->cq.size_log2 = ilog2(entries);

	if (cxio_create_cq(&rhp->rdev, &chp->cq, !ucontext)) {
		kfree(chp);
		return ERR_PTR(-ENOMEM);
	}
	chp->rhp = rhp;
	chp->ibcq.cqe = 1 << chp->cq.size_log2;
	spin_lock_init(&chp->lock);
	spin_lock_init(&chp->comp_handler_lock);
	atomic_set(&chp->refcnt, 1);
	init_waitqueue_head(&chp->wait);
	if (insert_handle(rhp, &rhp->cqidr, chp, chp->cq.cqid)) {
		cxio_destroy_cq(&chp->rhp->rdev, &chp->cq);
		kfree(chp);
		return ERR_PTR(-ENOMEM);
	}

	if (ucontext) {
		struct iwch_mm_entry *mm;

		mm = kmalloc(sizeof *mm, GFP_KERNEL);
		if (!mm) {
			iwch_destroy_cq(&chp->ibcq);
			return ERR_PTR(-ENOMEM);
		}
		uresp.cqid = chp->cq.cqid;
		uresp.size_log2 = chp->cq.size_log2;
		spin_lock(&ucontext->mmap_lock);
		uresp.key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		spin_unlock(&ucontext->mmap_lock);
		mm->key = uresp.key;
		mm->addr = virt_to_phys(chp->cq.queue);
		if (udata->outlen < sizeof uresp) {
			if (!warned++)
				printk(KERN_WARNING MOD "Warning - "
				       "downlevel libcxgb3 (non-fatal).\n");
			mm->len = PAGE_ALIGN((1UL << uresp.size_log2) *
					     sizeof(struct t3_cqe));
			resplen = sizeof(struct iwch_create_cq_resp_v0);
		} else {
			mm->len = PAGE_ALIGN(((1UL << uresp.size_log2) + 1) *
					     sizeof(struct t3_cqe));
			uresp.memsize = mm->len;
			uresp.reserved = 0;
			resplen = sizeof uresp;
		}
		if (ib_copy_to_udata(udata, &uresp, resplen)) {
			kfree(mm);
			iwch_destroy_cq(&chp->ibcq);
			return ERR_PTR(-EFAULT);
		}
		insert_mmap(ucontext, mm);
	}
	PDBG("created cqid 0x%0x chp %p size 0x%0x, dma_addr 0x%0llx\n",
	     chp->cq.cqid, chp, (1 << chp->cq.size_log2),
	     (unsigned long long) chp->cq.dma_addr);
	return &chp->ibcq;
}

static int iwch_resize_cq(struct ib_cq *cq, int cqe, struct ib_udata *udata)
{
#ifdef notyet
	struct iwch_cq *chp = to_iwch_cq(cq);
	struct t3_cq oldcq, newcq;
	int ret;

	PDBG("%s ib_cq %p cqe %d\n", __func__, cq, cqe);

	/* We don't downsize... */
	if (cqe <= cq->cqe)
		return 0;

	/* create new t3_cq with new size */
	cqe = roundup_pow_of_two(cqe+1);
	newcq.size_log2 = ilog2(cqe);

	/* Dont allow resize to less than the current wce count */
	if (cqe < Q_COUNT(chp->cq.rptr, chp->cq.wptr)) {
		return -ENOMEM;
	}

	/* Quiesce all QPs using this CQ */
	ret = iwch_quiesce_qps(chp);
	if (ret) {
		return ret;
	}

	ret = cxio_create_cq(&chp->rhp->rdev, &newcq);
	if (ret) {
		return ret;
	}

	/* copy CQEs */
	memcpy(newcq.queue, chp->cq.queue, (1 << chp->cq.size_log2) *
				        sizeof(struct t3_cqe));

	/* old iwch_qp gets new t3_cq but keeps old cqid */
	oldcq = chp->cq;
	chp->cq = newcq;
	chp->cq.cqid = oldcq.cqid;

	/* resize new t3_cq to update the HW context */
	ret = cxio_resize_cq(&chp->rhp->rdev, &chp->cq);
	if (ret) {
		chp->cq = oldcq;
		return ret;
	}
	chp->ibcq.cqe = (1<<chp->cq.size_log2) - 1;

	/* destroy old t3_cq */
	oldcq.cqid = newcq.cqid;
	ret = cxio_destroy_cq(&chp->rhp->rdev, &oldcq);
	if (ret) {
		printk(KERN_ERR MOD "%s - cxio_destroy_cq failed %d\n",
			__func__, ret);
	}

	/* add user hooks here */

	/* resume qps */
	ret = iwch_resume_qps(chp);
	return ret;
#else
	return -ENOSYS;
#endif
}

static int iwch_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	struct iwch_dev *rhp;
	struct iwch_cq *chp;
	enum t3_cq_opcode cq_op;
	int err;
	unsigned long flag;
	u32 rptr;

	chp = to_iwch_cq(ibcq);
	rhp = chp->rhp;
	if ((flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED)
		cq_op = CQ_ARM_SE;
	else
		cq_op = CQ_ARM_AN;
	if (chp->user_rptr_addr) {
		if (get_user(rptr, chp->user_rptr_addr))
			return -EFAULT;
		spin_lock_irqsave(&chp->lock, flag);
		chp->cq.rptr = rptr;
	} else
		spin_lock_irqsave(&chp->lock, flag);
	PDBG("%s rptr 0x%x\n", __func__, chp->cq.rptr);
	err = cxio_hal_cq_op(&rhp->rdev, &chp->cq, cq_op, 0);
	spin_unlock_irqrestore(&chp->lock, flag);
	if (err < 0)
		printk(KERN_ERR MOD "Error %d rearming CQID 0x%x\n", err,
		       chp->cq.cqid);
	if (err > 0 && !(flags & IB_CQ_REPORT_MISSED_EVENTS))
		err = 0;
	return err;
}

static int iwch_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	int len = vma->vm_end - vma->vm_start;
	u32 key = vma->vm_pgoff << PAGE_SHIFT;
	struct cxio_rdev *rdev_p;
	int ret = 0;
	struct iwch_mm_entry *mm;
	struct iwch_ucontext *ucontext;
	u64 addr;

	PDBG("%s pgoff 0x%lx key 0x%x len %d\n", __func__, vma->vm_pgoff,
	     key, len);

	if (vma->vm_start & (PAGE_SIZE-1)) {
	        return -EINVAL;
	}

	rdev_p = &(to_iwch_dev(context->device)->rdev);
	ucontext = to_iwch_ucontext(context);

	mm = remove_mmap(ucontext, key, len);
	if (!mm)
		return -EINVAL;
	addr = mm->addr;
	kfree(mm);

	if ((addr >= rdev_p->rnic_info.udbell_physbase) &&
	    (addr < (rdev_p->rnic_info.udbell_physbase +
		       rdev_p->rnic_info.udbell_len))) {

		/*
		 * Map T3 DB register.
		 */
		if (vma->vm_flags & VM_READ) {
			return -EPERM;
		}

		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		vma->vm_flags |= VM_DONTCOPY | VM_DONTEXPAND;
		vma->vm_flags &= ~VM_MAYREAD;
		ret = io_remap_pfn_range(vma, vma->vm_start,
					 addr >> PAGE_SHIFT,
				         len, vma->vm_page_prot);
	} else {

		/*
		 * Map WQ or CQ contig dma memory...
		 */
		ret = remap_pfn_range(vma, vma->vm_start,
				      addr >> PAGE_SHIFT,
				      len, vma->vm_page_prot);
	}

	return ret;
}

static int iwch_deallocate_pd(struct ib_pd *pd)
{
	struct iwch_dev *rhp;
	struct iwch_pd *php;

	php = to_iwch_pd(pd);
	rhp = php->rhp;
	PDBG("%s ibpd %p pdid 0x%x\n", __func__, pd, php->pdid);
	cxio_hal_put_pdid(rhp->rdev.rscp, php->pdid);
	kfree(php);
	return 0;
}

static struct ib_pd *iwch_allocate_pd(struct ib_device *ibdev,
			       struct ib_ucontext *context,
			       struct ib_udata *udata)
{
	struct iwch_pd *php;
	u32 pdid;
	struct iwch_dev *rhp;

	PDBG("%s ibdev %p\n", __func__, ibdev);
	rhp = (struct iwch_dev *) ibdev;
	pdid = cxio_hal_get_pdid(rhp->rdev.rscp);
	if (!pdid)
		return ERR_PTR(-EINVAL);
	php = kzalloc(sizeof(*php), GFP_KERNEL);
	if (!php) {
		cxio_hal_put_pdid(rhp->rdev.rscp, pdid);
		return ERR_PTR(-ENOMEM);
	}
	php->pdid = pdid;
	php->rhp = rhp;
	if (context) {
		if (ib_copy_to_udata(udata, &php->pdid, sizeof (__u32))) {
			iwch_deallocate_pd(&php->ibpd);
			return ERR_PTR(-EFAULT);
		}
	}
	PDBG("%s pdid 0x%0x ptr 0x%p\n", __func__, pdid, php);
	return &php->ibpd;
}

static int iwch_dereg_mr(struct ib_mr *ib_mr)
{
	struct iwch_dev *rhp;
	struct iwch_mr *mhp;
	u32 mmid;

	PDBG("%s ib_mr %p\n", __func__, ib_mr);

	mhp = to_iwch_mr(ib_mr);
	kfree(mhp->pages);
	rhp = mhp->rhp;
	mmid = mhp->attr.stag >> 8;
	cxio_dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		       mhp->attr.pbl_addr);
	iwch_free_pbl(mhp);
	remove_handle(rhp, &rhp->mmidr, mmid);
	if (mhp->kva)
		kfree((void *) (unsigned long) mhp->kva);
	if (mhp->umem)
		ib_umem_release(mhp->umem);
	PDBG("%s mmid 0x%x ptr %p\n", __func__, mmid, mhp);
	kfree(mhp);
	return 0;
}

static struct ib_mr *iwch_get_dma_mr(struct ib_pd *pd, int acc)
{
	const u64 total_size = 0xffffffff;
	const u64 mask = (total_size + PAGE_SIZE - 1) & PAGE_MASK;
	struct iwch_pd *php = to_iwch_pd(pd);
	struct iwch_dev *rhp = php->rhp;
	struct iwch_mr *mhp;
	__be64 *page_list;
	int shift = 26, npages, ret, i;

	PDBG("%s ib_pd %p\n", __func__, pd);

	/*
	 * T3 only supports 32 bits of size.
	 */
	if (sizeof(phys_addr_t) > 4) {
		pr_warn_once(MOD "Cannot support dma_mrs on this platform.\n");
		return ERR_PTR(-ENOTSUPP);
	}

	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);

	mhp->rhp = rhp;

	npages = (total_size + (1ULL << shift) - 1) >> shift;
	if (!npages) {
		ret = -EINVAL;
		goto err;
	}

	page_list = kmalloc_array(npages, sizeof(u64), GFP_KERNEL);
	if (!page_list) {
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < npages; i++)
		page_list[i] = cpu_to_be64((u64)i << shift);

	PDBG("%s mask 0x%llx shift %d len %lld pbl_size %d\n",
		__func__, mask, shift, total_size, npages);

	ret = iwch_alloc_pbl(mhp, npages);
	if (ret) {
		kfree(page_list);
		goto err_pbl;
	}

	ret = iwch_write_pbl(mhp, page_list, npages, 0);
	kfree(page_list);
	if (ret)
		goto err_pbl;

	mhp->attr.pdid = php->pdid;
	mhp->attr.zbva = 0;

	mhp->attr.perms = iwch_ib_to_tpt_access(acc);
	mhp->attr.va_fbo = 0;
	mhp->attr.page_size = shift - 12;

	mhp->attr.len = (u32) total_size;
	mhp->attr.pbl_size = npages;
	ret = iwch_register_mem(rhp, php, mhp, shift);
	if (ret)
		goto err_pbl;

	return &mhp->ibmr;

err_pbl:
	iwch_free_pbl(mhp);

err:
	kfree(mhp);
	return ERR_PTR(ret);
}

static struct ib_mr *iwch_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				      u64 virt, int acc, struct ib_udata *udata)
{
	__be64 *pages;
	int shift, n, len;
	int i, k, entry;
	int err = 0;
	struct iwch_dev *rhp;
	struct iwch_pd *php;
	struct iwch_mr *mhp;
	struct iwch_reg_user_mr_resp uresp;
	struct scatterlist *sg;
	PDBG("%s ib_pd %p\n", __func__, pd);

	php = to_iwch_pd(pd);
	rhp = php->rhp;
	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);

	mhp->rhp = rhp;

	mhp->umem = ib_umem_get(pd->uobject->context, start, length, acc, 0);
	if (IS_ERR(mhp->umem)) {
		err = PTR_ERR(mhp->umem);
		kfree(mhp);
		return ERR_PTR(err);
	}

	shift = ffs(mhp->umem->page_size) - 1;

	n = mhp->umem->nmap;

	err = iwch_alloc_pbl(mhp, n);
	if (err)
		goto err;

	pages = (__be64 *) __get_free_page(GFP_KERNEL);
	if (!pages) {
		err = -ENOMEM;
		goto err_pbl;
	}

	i = n = 0;

	for_each_sg(mhp->umem->sg_head.sgl, sg, mhp->umem->nmap, entry) {
			len = sg_dma_len(sg) >> shift;
			for (k = 0; k < len; ++k) {
				pages[i++] = cpu_to_be64(sg_dma_address(sg) +
					mhp->umem->page_size * k);
				if (i == PAGE_SIZE / sizeof *pages) {
					err = iwch_write_pbl(mhp, pages, i, n);
					if (err)
						goto pbl_done;
					n += i;
					i = 0;
				}
			}
	}

	if (i)
		err = iwch_write_pbl(mhp, pages, i, n);

pbl_done:
	free_page((unsigned long) pages);
	if (err)
		goto err_pbl;

	mhp->attr.pdid = php->pdid;
	mhp->attr.zbva = 0;
	mhp->attr.perms = iwch_ib_to_tpt_access(acc);
	mhp->attr.va_fbo = virt;
	mhp->attr.page_size = shift - 12;
	mhp->attr.len = (u32) length;

	err = iwch_register_mem(rhp, php, mhp, shift);
	if (err)
		goto err_pbl;

	if (udata && !t3a_device(rhp)) {
		uresp.pbl_addr = (mhp->attr.pbl_addr -
				 rhp->rdev.rnic_info.pbl_base) >> 3;
		PDBG("%s user resp pbl_addr 0x%x\n", __func__,
		     uresp.pbl_addr);

		if (ib_copy_to_udata(udata, &uresp, sizeof (uresp))) {
			iwch_dereg_mr(&mhp->ibmr);
			err = -EFAULT;
			goto err;
		}
	}

	return &mhp->ibmr;

err_pbl:
	iwch_free_pbl(mhp);

err:
	ib_umem_release(mhp->umem);
	kfree(mhp);
	return ERR_PTR(err);
}

static struct ib_mw *iwch_alloc_mw(struct ib_pd *pd, enum ib_mw_type type,
				   struct ib_udata *udata)
{
	struct iwch_dev *rhp;
	struct iwch_pd *php;
	struct iwch_mw *mhp;
	u32 mmid;
	u32 stag = 0;
	int ret;

	if (type != IB_MW_TYPE_1)
		return ERR_PTR(-EINVAL);

	php = to_iwch_pd(pd);
	rhp = php->rhp;
	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		return ERR_PTR(-ENOMEM);
	ret = cxio_allocate_window(&rhp->rdev, &stag, php->pdid);
	if (ret) {
		kfree(mhp);
		return ERR_PTR(ret);
	}
	mhp->rhp = rhp;
	mhp->attr.pdid = php->pdid;
	mhp->attr.type = TPT_MW;
	mhp->attr.stag = stag;
	mmid = (stag) >> 8;
	mhp->ibmw.rkey = stag;
	if (insert_handle(rhp, &rhp->mmidr, mhp, mmid)) {
		cxio_deallocate_window(&rhp->rdev, mhp->attr.stag);
		kfree(mhp);
		return ERR_PTR(-ENOMEM);
	}
	PDBG("%s mmid 0x%x mhp %p stag 0x%x\n", __func__, mmid, mhp, stag);
	return &(mhp->ibmw);
}

static int iwch_dealloc_mw(struct ib_mw *mw)
{
	struct iwch_dev *rhp;
	struct iwch_mw *mhp;
	u32 mmid;

	mhp = to_iwch_mw(mw);
	rhp = mhp->rhp;
	mmid = (mw->rkey) >> 8;
	cxio_deallocate_window(&rhp->rdev, mhp->attr.stag);
	remove_handle(rhp, &rhp->mmidr, mmid);
	PDBG("%s ib_mw %p mmid 0x%x ptr %p\n", __func__, mw, mmid, mhp);
	kfree(mhp);
	return 0;
}

static struct ib_mr *iwch_alloc_mr(struct ib_pd *pd,
				   enum ib_mr_type mr_type,
				   u32 max_num_sg)
{
	struct iwch_dev *rhp;
	struct iwch_pd *php;
	struct iwch_mr *mhp;
	u32 mmid;
	u32 stag = 0;
	int ret = 0;

	if (mr_type != IB_MR_TYPE_MEM_REG ||
	    max_num_sg > T3_MAX_FASTREG_DEPTH)
		return ERR_PTR(-EINVAL);

	php = to_iwch_pd(pd);
	rhp = php->rhp;
	mhp = kzalloc(sizeof(*mhp), GFP_KERNEL);
	if (!mhp)
		goto err;

	mhp->pages = kcalloc(max_num_sg, sizeof(u64), GFP_KERNEL);
	if (!mhp->pages) {
		ret = -ENOMEM;
		goto pl_err;
	}

	mhp->rhp = rhp;
	ret = iwch_alloc_pbl(mhp, max_num_sg);
	if (ret)
		goto err1;
	mhp->attr.pbl_size = max_num_sg;
	ret = cxio_allocate_stag(&rhp->rdev, &stag, php->pdid,
				 mhp->attr.pbl_size, mhp->attr.pbl_addr);
	if (ret)
		goto err2;
	mhp->attr.pdid = php->pdid;
	mhp->attr.type = TPT_NON_SHARED_MR;
	mhp->attr.stag = stag;
	mhp->attr.state = 1;
	mmid = (stag) >> 8;
	mhp->ibmr.rkey = mhp->ibmr.lkey = stag;
	if (insert_handle(rhp, &rhp->mmidr, mhp, mmid))
		goto err3;

	PDBG("%s mmid 0x%x mhp %p stag 0x%x\n", __func__, mmid, mhp, stag);
	return &(mhp->ibmr);
err3:
	cxio_dereg_mem(&rhp->rdev, stag, mhp->attr.pbl_size,
		       mhp->attr.pbl_addr);
err2:
	iwch_free_pbl(mhp);
err1:
	kfree(mhp->pages);
pl_err:
	kfree(mhp);
err:
	return ERR_PTR(ret);
}

static int iwch_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct iwch_mr *mhp = to_iwch_mr(ibmr);

	if (unlikely(mhp->npages == mhp->attr.pbl_size))
		return -ENOMEM;

	mhp->pages[mhp->npages++] = addr;

	return 0;
}

static int iwch_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
			  int sg_nents, unsigned int *sg_offset)
{
	struct iwch_mr *mhp = to_iwch_mr(ibmr);

	mhp->npages = 0;

	return ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, iwch_set_page);
}

static int iwch_destroy_qp(struct ib_qp *ib_qp)
{
	struct iwch_dev *rhp;
	struct iwch_qp *qhp;
	struct iwch_qp_attributes attrs;
	struct iwch_ucontext *ucontext;

	qhp = to_iwch_qp(ib_qp);
	rhp = qhp->rhp;

	attrs.next_state = IWCH_QP_STATE_ERROR;
	iwch_modify_qp(rhp, qhp, IWCH_QP_ATTR_NEXT_STATE, &attrs, 0);
	wait_event(qhp->wait, !qhp->ep);

	remove_handle(rhp, &rhp->qpidr, qhp->wq.qpid);

	atomic_dec(&qhp->refcnt);
	wait_event(qhp->wait, !atomic_read(&qhp->refcnt));

	ucontext = ib_qp->uobject ? to_iwch_ucontext(ib_qp->uobject->context)
				  : NULL;
	cxio_destroy_qp(&rhp->rdev, &qhp->wq,
			ucontext ? &ucontext->uctx : &rhp->rdev.uctx);

	PDBG("%s ib_qp %p qpid 0x%0x qhp %p\n", __func__,
	     ib_qp, qhp->wq.qpid, qhp);
	kfree(qhp);
	return 0;
}

static struct ib_qp *iwch_create_qp(struct ib_pd *pd,
			     struct ib_qp_init_attr *attrs,
			     struct ib_udata *udata)
{
	struct iwch_dev *rhp;
	struct iwch_qp *qhp;
	struct iwch_pd *php;
	struct iwch_cq *schp;
	struct iwch_cq *rchp;
	struct iwch_create_qp_resp uresp;
	int wqsize, sqsize, rqsize;
	struct iwch_ucontext *ucontext;

	PDBG("%s ib_pd %p\n", __func__, pd);
	if (attrs->qp_type != IB_QPT_RC)
		return ERR_PTR(-EINVAL);
	php = to_iwch_pd(pd);
	rhp = php->rhp;
	schp = get_chp(rhp, ((struct iwch_cq *) attrs->send_cq)->cq.cqid);
	rchp = get_chp(rhp, ((struct iwch_cq *) attrs->recv_cq)->cq.cqid);
	if (!schp || !rchp)
		return ERR_PTR(-EINVAL);

	/* The RQT size must be # of entries + 1 rounded up to a power of two */
	rqsize = roundup_pow_of_two(attrs->cap.max_recv_wr);
	if (rqsize == attrs->cap.max_recv_wr)
		rqsize = roundup_pow_of_two(attrs->cap.max_recv_wr+1);

	/* T3 doesn't support RQT depth < 16 */
	if (rqsize < 16)
		rqsize = 16;

	if (rqsize > T3_MAX_RQ_SIZE)
		return ERR_PTR(-EINVAL);

	if (attrs->cap.max_inline_data > T3_MAX_INLINE)
		return ERR_PTR(-EINVAL);

	/*
	 * NOTE: The SQ and total WQ sizes don't need to be
	 * a power of two.  However, all the code assumes
	 * they are. EG: Q_FREECNT() and friends.
	 */
	sqsize = roundup_pow_of_two(attrs->cap.max_send_wr);
	wqsize = roundup_pow_of_two(rqsize + sqsize);

	/*
	 * Kernel users need more wq space for fastreg WRs which can take
	 * 2 WR fragments.
	 */
	ucontext = pd->uobject ? to_iwch_ucontext(pd->uobject->context) : NULL;
	if (!ucontext && wqsize < (rqsize + (2 * sqsize)))
		wqsize = roundup_pow_of_two(rqsize +
				roundup_pow_of_two(attrs->cap.max_send_wr * 2));
	PDBG("%s wqsize %d sqsize %d rqsize %d\n", __func__,
	     wqsize, sqsize, rqsize);
	qhp = kzalloc(sizeof(*qhp), GFP_KERNEL);
	if (!qhp)
		return ERR_PTR(-ENOMEM);
	qhp->wq.size_log2 = ilog2(wqsize);
	qhp->wq.rq_size_log2 = ilog2(rqsize);
	qhp->wq.sq_size_log2 = ilog2(sqsize);
	if (cxio_create_qp(&rhp->rdev, !udata, &qhp->wq,
			   ucontext ? &ucontext->uctx : &rhp->rdev.uctx)) {
		kfree(qhp);
		return ERR_PTR(-ENOMEM);
	}

	attrs->cap.max_recv_wr = rqsize - 1;
	attrs->cap.max_send_wr = sqsize;
	attrs->cap.max_inline_data = T3_MAX_INLINE;

	qhp->rhp = rhp;
	qhp->attr.pd = php->pdid;
	qhp->attr.scq = ((struct iwch_cq *) attrs->send_cq)->cq.cqid;
	qhp->attr.rcq = ((struct iwch_cq *) attrs->recv_cq)->cq.cqid;
	qhp->attr.sq_num_entries = attrs->cap.max_send_wr;
	qhp->attr.rq_num_entries = attrs->cap.max_recv_wr;
	qhp->attr.sq_max_sges = attrs->cap.max_send_sge;
	qhp->attr.sq_max_sges_rdma_write = attrs->cap.max_send_sge;
	qhp->attr.rq_max_sges = attrs->cap.max_recv_sge;
	qhp->attr.state = IWCH_QP_STATE_IDLE;
	qhp->attr.next_state = IWCH_QP_STATE_IDLE;

	/*
	 * XXX - These don't get passed in from the openib user
	 * at create time.  The CM sets them via a QP modify.
	 * Need to fix...  I think the CM should
	 */
	qhp->attr.enable_rdma_read = 1;
	qhp->attr.enable_rdma_write = 1;
	qhp->attr.enable_bind = 1;
	qhp->attr.max_ord = 1;
	qhp->attr.max_ird = 1;

	spin_lock_init(&qhp->lock);
	init_waitqueue_head(&qhp->wait);
	atomic_set(&qhp->refcnt, 1);

	if (insert_handle(rhp, &rhp->qpidr, qhp, qhp->wq.qpid)) {
		cxio_destroy_qp(&rhp->rdev, &qhp->wq,
			ucontext ? &ucontext->uctx : &rhp->rdev.uctx);
		kfree(qhp);
		return ERR_PTR(-ENOMEM);
	}

	if (udata) {

		struct iwch_mm_entry *mm1, *mm2;

		mm1 = kmalloc(sizeof *mm1, GFP_KERNEL);
		if (!mm1) {
			iwch_destroy_qp(&qhp->ibqp);
			return ERR_PTR(-ENOMEM);
		}

		mm2 = kmalloc(sizeof *mm2, GFP_KERNEL);
		if (!mm2) {
			kfree(mm1);
			iwch_destroy_qp(&qhp->ibqp);
			return ERR_PTR(-ENOMEM);
		}

		uresp.qpid = qhp->wq.qpid;
		uresp.size_log2 = qhp->wq.size_log2;
		uresp.sq_size_log2 = qhp->wq.sq_size_log2;
		uresp.rq_size_log2 = qhp->wq.rq_size_log2;
		spin_lock(&ucontext->mmap_lock);
		uresp.key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		uresp.db_key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		spin_unlock(&ucontext->mmap_lock);
		if (ib_copy_to_udata(udata, &uresp, sizeof (uresp))) {
			kfree(mm1);
			kfree(mm2);
			iwch_destroy_qp(&qhp->ibqp);
			return ERR_PTR(-EFAULT);
		}
		mm1->key = uresp.key;
		mm1->addr = virt_to_phys(qhp->wq.queue);
		mm1->len = PAGE_ALIGN(wqsize * sizeof (union t3_wr));
		insert_mmap(ucontext, mm1);
		mm2->key = uresp.db_key;
		mm2->addr = qhp->wq.udb & PAGE_MASK;
		mm2->len = PAGE_SIZE;
		insert_mmap(ucontext, mm2);
	}
	qhp->ibqp.qp_num = qhp->wq.qpid;
	init_timer(&(qhp->timer));
	PDBG("%s sq_num_entries %d, rq_num_entries %d "
	     "qpid 0x%0x qhp %p dma_addr 0x%llx size %d rq_addr 0x%x\n",
	     __func__, qhp->attr.sq_num_entries, qhp->attr.rq_num_entries,
	     qhp->wq.qpid, qhp, (unsigned long long) qhp->wq.dma_addr,
	     1 << qhp->wq.size_log2, qhp->wq.rq_addr);
	return &qhp->ibqp;
}

static int iwch_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata)
{
	struct iwch_dev *rhp;
	struct iwch_qp *qhp;
	enum iwch_qp_attr_mask mask = 0;
	struct iwch_qp_attributes attrs;

	PDBG("%s ib_qp %p\n", __func__, ibqp);

	/* iwarp does not support the RTR state */
	if ((attr_mask & IB_QP_STATE) && (attr->qp_state == IB_QPS_RTR))
		attr_mask &= ~IB_QP_STATE;

	/* Make sure we still have something left to do */
	if (!attr_mask)
		return 0;

	memset(&attrs, 0, sizeof attrs);
	qhp = to_iwch_qp(ibqp);
	rhp = qhp->rhp;

	attrs.next_state = iwch_convert_state(attr->qp_state);
	attrs.enable_rdma_read = (attr->qp_access_flags &
			       IB_ACCESS_REMOTE_READ) ?  1 : 0;
	attrs.enable_rdma_write = (attr->qp_access_flags &
				IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	attrs.enable_bind = (attr->qp_access_flags & IB_ACCESS_MW_BIND) ? 1 : 0;


	mask |= (attr_mask & IB_QP_STATE) ? IWCH_QP_ATTR_NEXT_STATE : 0;
	mask |= (attr_mask & IB_QP_ACCESS_FLAGS) ?
			(IWCH_QP_ATTR_ENABLE_RDMA_READ |
			 IWCH_QP_ATTR_ENABLE_RDMA_WRITE |
			 IWCH_QP_ATTR_ENABLE_RDMA_BIND) : 0;

	return iwch_modify_qp(rhp, qhp, mask, &attrs, 0);
}

void iwch_qp_add_ref(struct ib_qp *qp)
{
	PDBG("%s ib_qp %p\n", __func__, qp);
	atomic_inc(&(to_iwch_qp(qp)->refcnt));
}

void iwch_qp_rem_ref(struct ib_qp *qp)
{
	PDBG("%s ib_qp %p\n", __func__, qp);
	if (atomic_dec_and_test(&(to_iwch_qp(qp)->refcnt)))
	        wake_up(&(to_iwch_qp(qp)->wait));
}

static struct ib_qp *iwch_get_qp(struct ib_device *dev, int qpn)
{
	PDBG("%s ib_dev %p qpn 0x%x\n", __func__, dev, qpn);
	return (struct ib_qp *)get_qhp(to_iwch_dev(dev), qpn);
}


static int iwch_query_pkey(struct ib_device *ibdev,
			   u8 port, u16 index, u16 * pkey)
{
	PDBG("%s ibdev %p\n", __func__, ibdev);
	*pkey = 0;
	return 0;
}

static int iwch_query_gid(struct ib_device *ibdev, u8 port,
			  int index, union ib_gid *gid)
{
	struct iwch_dev *dev;

	PDBG("%s ibdev %p, port %d, index %d, gid %p\n",
	       __func__, ibdev, port, index, gid);
	dev = to_iwch_dev(ibdev);
	BUG_ON(port == 0 || port > 2);
	memset(&(gid->raw[0]), 0, sizeof(gid->raw));
	memcpy(&(gid->raw[0]), dev->rdev.port_info.lldevs[port-1]->dev_addr, 6);
	return 0;
}

static u64 fw_vers_string_to_u64(struct iwch_dev *iwch_dev)
{
	struct ethtool_drvinfo info;
	struct net_device *lldev = iwch_dev->rdev.t3cdev_p->lldev;
	char *cp, *next;
	unsigned fw_maj, fw_min, fw_mic;

	lldev->ethtool_ops->get_drvinfo(lldev, &info);

	next = info.fw_version + 1;
	cp = strsep(&next, ".");
	sscanf(cp, "%i", &fw_maj);
	cp = strsep(&next, ".");
	sscanf(cp, "%i", &fw_min);
	cp = strsep(&next, ".");
	sscanf(cp, "%i", &fw_mic);

	return (((u64)fw_maj & 0xffff) << 32) | ((fw_min & 0xffff) << 16) |
	       (fw_mic & 0xffff);
}

static int iwch_query_device(struct ib_device *ibdev, struct ib_device_attr *props,
			     struct ib_udata *uhw)
{

	struct iwch_dev *dev;

	PDBG("%s ibdev %p\n", __func__, ibdev);

	if (uhw->inlen || uhw->outlen)
		return -EINVAL;

	dev = to_iwch_dev(ibdev);
	memset(props, 0, sizeof *props);
	memcpy(&props->sys_image_guid, dev->rdev.t3cdev_p->lldev->dev_addr, 6);
	props->hw_ver = dev->rdev.t3cdev_p->type;
	props->fw_ver = fw_vers_string_to_u64(dev);
	props->device_cap_flags = dev->device_cap_flags;
	props->page_size_cap = dev->attr.mem_pgsizes_bitmask;
	props->vendor_id = (u32)dev->rdev.rnic_info.pdev->vendor;
	props->vendor_part_id = (u32)dev->rdev.rnic_info.pdev->device;
	props->max_mr_size = dev->attr.max_mr_size;
	props->max_qp = dev->attr.max_qps;
	props->max_qp_wr = dev->attr.max_wrs;
	props->max_sge = dev->attr.max_sge_per_wr;
	props->max_sge_rd = 1;
	props->max_qp_rd_atom = dev->attr.max_rdma_reads_per_qp;
	props->max_qp_init_rd_atom = dev->attr.max_rdma_reads_per_qp;
	props->max_cq = dev->attr.max_cqs;
	props->max_cqe = dev->attr.max_cqes_per_cq;
	props->max_mr = dev->attr.max_mem_regs;
	props->max_pd = dev->attr.max_pds;
	props->local_ca_ack_delay = 0;
	props->max_fast_reg_page_list_len = T3_MAX_FASTREG_DEPTH;

	return 0;
}

static int iwch_query_port(struct ib_device *ibdev,
			   u8 port, struct ib_port_attr *props)
{
	struct iwch_dev *dev;
	struct net_device *netdev;
	struct in_device *inetdev;

	PDBG("%s ibdev %p\n", __func__, ibdev);

	dev = to_iwch_dev(ibdev);
	netdev = dev->rdev.port_info.lldevs[port-1];

	/* props being zeroed by the caller, avoid zeroing it here */
	props->max_mtu = IB_MTU_4096;
	if (netdev->mtu >= 4096)
		props->active_mtu = IB_MTU_4096;
	else if (netdev->mtu >= 2048)
		props->active_mtu = IB_MTU_2048;
	else if (netdev->mtu >= 1024)
		props->active_mtu = IB_MTU_1024;
	else if (netdev->mtu >= 512)
		props->active_mtu = IB_MTU_512;
	else
		props->active_mtu = IB_MTU_256;

	if (!netif_carrier_ok(netdev))
		props->state = IB_PORT_DOWN;
	else {
		inetdev = in_dev_get(netdev);
		if (inetdev) {
			if (inetdev->ifa_list)
				props->state = IB_PORT_ACTIVE;
			else
				props->state = IB_PORT_INIT;
			in_dev_put(inetdev);
		} else
			props->state = IB_PORT_INIT;
	}

	props->port_cap_flags =
	    IB_PORT_CM_SUP |
	    IB_PORT_SNMP_TUNNEL_SUP |
	    IB_PORT_REINIT_SUP |
	    IB_PORT_DEVICE_MGMT_SUP |
	    IB_PORT_VENDOR_CLASS_SUP | IB_PORT_BOOT_MGMT_SUP;
	props->gid_tbl_len = 1;
	props->pkey_tbl_len = 1;
	props->active_width = 2;
	props->active_speed = IB_SPEED_DDR;
	props->max_msg_sz = -1;

	return 0;
}

static ssize_t show_rev(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct iwch_dev *iwch_dev = container_of(dev, struct iwch_dev,
						 ibdev.dev);
	PDBG("%s dev 0x%p\n", __func__, dev);
	return sprintf(buf, "%d\n", iwch_dev->rdev.t3cdev_p->type);
}

static ssize_t show_hca(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct iwch_dev *iwch_dev = container_of(dev, struct iwch_dev,
						 ibdev.dev);
	struct ethtool_drvinfo info;
	struct net_device *lldev = iwch_dev->rdev.t3cdev_p->lldev;

	PDBG("%s dev 0x%p\n", __func__, dev);
	lldev->ethtool_ops->get_drvinfo(lldev, &info);
	return sprintf(buf, "%s\n", info.driver);
}

static ssize_t show_board(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct iwch_dev *iwch_dev = container_of(dev, struct iwch_dev,
						 ibdev.dev);
	PDBG("%s dev 0x%p\n", __func__, dev);
	return sprintf(buf, "%x.%x\n", iwch_dev->rdev.rnic_info.pdev->vendor,
		       iwch_dev->rdev.rnic_info.pdev->device);
}

enum counters {
	IPINRECEIVES,
	IPINHDRERRORS,
	IPINADDRERRORS,
	IPINUNKNOWNPROTOS,
	IPINDISCARDS,
	IPINDELIVERS,
	IPOUTREQUESTS,
	IPOUTDISCARDS,
	IPOUTNOROUTES,
	IPREASMTIMEOUT,
	IPREASMREQDS,
	IPREASMOKS,
	IPREASMFAILS,
	TCPACTIVEOPENS,
	TCPPASSIVEOPENS,
	TCPATTEMPTFAILS,
	TCPESTABRESETS,
	TCPCURRESTAB,
	TCPINSEGS,
	TCPOUTSEGS,
	TCPRETRANSSEGS,
	TCPINERRS,
	TCPOUTRSTS,
	TCPRTOMIN,
	TCPRTOMAX,
	NR_COUNTERS
};

static const char * const names[] = {
	[IPINRECEIVES] = "ipInReceives",
	[IPINHDRERRORS] = "ipInHdrErrors",
	[IPINADDRERRORS] = "ipInAddrErrors",
	[IPINUNKNOWNPROTOS] = "ipInUnknownProtos",
	[IPINDISCARDS] = "ipInDiscards",
	[IPINDELIVERS] = "ipInDelivers",
	[IPOUTREQUESTS] = "ipOutRequests",
	[IPOUTDISCARDS] = "ipOutDiscards",
	[IPOUTNOROUTES] = "ipOutNoRoutes",
	[IPREASMTIMEOUT] = "ipReasmTimeout",
	[IPREASMREQDS] = "ipReasmReqds",
	[IPREASMOKS] = "ipReasmOKs",
	[IPREASMFAILS] = "ipReasmFails",
	[TCPACTIVEOPENS] = "tcpActiveOpens",
	[TCPPASSIVEOPENS] = "tcpPassiveOpens",
	[TCPATTEMPTFAILS] = "tcpAttemptFails",
	[TCPESTABRESETS] = "tcpEstabResets",
	[TCPCURRESTAB] = "tcpCurrEstab",
	[TCPINSEGS] = "tcpInSegs",
	[TCPOUTSEGS] = "tcpOutSegs",
	[TCPRETRANSSEGS] = "tcpRetransSegs",
	[TCPINERRS] = "tcpInErrs",
	[TCPOUTRSTS] = "tcpOutRsts",
	[TCPRTOMIN] = "tcpRtoMin",
	[TCPRTOMAX] = "tcpRtoMax",
};

static struct rdma_hw_stats *iwch_alloc_stats(struct ib_device *ibdev,
					      u8 port_num)
{
	BUILD_BUG_ON(ARRAY_SIZE(names) != NR_COUNTERS);

	/* Our driver only supports device level stats */
	if (port_num != 0)
		return NULL;

	return rdma_alloc_hw_stats_struct(names, NR_COUNTERS,
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

static int iwch_get_mib(struct ib_device *ibdev, struct rdma_hw_stats *stats,
			u8 port, int index)
{
	struct iwch_dev *dev;
	struct tp_mib_stats m;
	int ret;

	if (port != 0 || !stats)
		return -ENOSYS;

	PDBG("%s ibdev %p\n", __func__, ibdev);
	dev = to_iwch_dev(ibdev);
	ret = dev->rdev.t3cdev_p->ctl(dev->rdev.t3cdev_p, RDMA_GET_MIB, &m);
	if (ret)
		return -ENOSYS;

	stats->value[IPINRECEIVES] = ((u64)m.ipInReceive_hi << 32) +	m.ipInReceive_lo;
	stats->value[IPINHDRERRORS] = ((u64)m.ipInHdrErrors_hi << 32) + m.ipInHdrErrors_lo;
	stats->value[IPINADDRERRORS] = ((u64)m.ipInAddrErrors_hi << 32) + m.ipInAddrErrors_lo;
	stats->value[IPINUNKNOWNPROTOS] = ((u64)m.ipInUnknownProtos_hi << 32) + m.ipInUnknownProtos_lo;
	stats->value[IPINDISCARDS] = ((u64)m.ipInDiscards_hi << 32) + m.ipInDiscards_lo;
	stats->value[IPINDELIVERS] = ((u64)m.ipInDelivers_hi << 32) + m.ipInDelivers_lo;
	stats->value[IPOUTREQUESTS] = ((u64)m.ipOutRequests_hi << 32) + m.ipOutRequests_lo;
	stats->value[IPOUTDISCARDS] = ((u64)m.ipOutDiscards_hi << 32) + m.ipOutDiscards_lo;
	stats->value[IPOUTNOROUTES] = ((u64)m.ipOutNoRoutes_hi << 32) + m.ipOutNoRoutes_lo;
	stats->value[IPREASMTIMEOUT] = 	m.ipReasmTimeout;
	stats->value[IPREASMREQDS] = m.ipReasmReqds;
	stats->value[IPREASMOKS] = m.ipReasmOKs;
	stats->value[IPREASMFAILS] = m.ipReasmFails;
	stats->value[TCPACTIVEOPENS] =	m.tcpActiveOpens;
	stats->value[TCPPASSIVEOPENS] =	m.tcpPassiveOpens;
	stats->value[TCPATTEMPTFAILS] = m.tcpAttemptFails;
	stats->value[TCPESTABRESETS] = m.tcpEstabResets;
	stats->value[TCPCURRESTAB] = m.tcpOutRsts;
	stats->value[TCPINSEGS] = m.tcpCurrEstab;
	stats->value[TCPOUTSEGS] = ((u64)m.tcpInSegs_hi << 32) + m.tcpInSegs_lo;
	stats->value[TCPRETRANSSEGS] = ((u64)m.tcpOutSegs_hi << 32) + m.tcpOutSegs_lo;
	stats->value[TCPINERRS] = ((u64)m.tcpRetransSeg_hi << 32) + m.tcpRetransSeg_lo,
	stats->value[TCPOUTRSTS] = ((u64)m.tcpInErrs_hi << 32) + m.tcpInErrs_lo;
	stats->value[TCPRTOMIN] = m.tcpRtoMin;
	stats->value[TCPRTOMAX] = m.tcpRtoMax;

	return stats->num_counters;
}

static DEVICE_ATTR(hw_rev, S_IRUGO, show_rev, NULL);
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca, NULL);
static DEVICE_ATTR(board_id, S_IRUGO, show_board, NULL);

static struct device_attribute *iwch_class_attributes[] = {
	&dev_attr_hw_rev,
	&dev_attr_hca_type,
	&dev_attr_board_id,
};

static int iwch_port_immutable(struct ib_device *ibdev, u8 port_num,
			       struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;

	return 0;
}

static void get_dev_fw_ver_str(struct ib_device *ibdev, char *str,
			       size_t str_len)
{
	struct iwch_dev *iwch_dev = to_iwch_dev(ibdev);
	struct ethtool_drvinfo info;
	struct net_device *lldev = iwch_dev->rdev.t3cdev_p->lldev;

	PDBG("%s dev 0x%p\n", __func__, iwch_dev);
	lldev->ethtool_ops->get_drvinfo(lldev, &info);
	snprintf(str, str_len, "%s", info.fw_version);
}

int iwch_register_device(struct iwch_dev *dev)
{
	int ret;
	int i;

	PDBG("%s iwch_dev %p\n", __func__, dev);
	strlcpy(dev->ibdev.name, "cxgb3_%d", IB_DEVICE_NAME_MAX);
	memset(&dev->ibdev.node_guid, 0, sizeof(dev->ibdev.node_guid));
	memcpy(&dev->ibdev.node_guid, dev->rdev.t3cdev_p->lldev->dev_addr, 6);
	dev->ibdev.owner = THIS_MODULE;
	dev->device_cap_flags = IB_DEVICE_LOCAL_DMA_LKEY |
				IB_DEVICE_MEM_WINDOW |
				IB_DEVICE_MEM_MGT_EXTENSIONS;

	/* cxgb3 supports STag 0. */
	dev->ibdev.local_dma_lkey = 0;

	dev->ibdev.uverbs_cmd_mask =
	    (1ull << IB_USER_VERBS_CMD_GET_CONTEXT) |
	    (1ull << IB_USER_VERBS_CMD_QUERY_DEVICE) |
	    (1ull << IB_USER_VERBS_CMD_QUERY_PORT) |
	    (1ull << IB_USER_VERBS_CMD_ALLOC_PD) |
	    (1ull << IB_USER_VERBS_CMD_DEALLOC_PD) |
	    (1ull << IB_USER_VERBS_CMD_REG_MR) |
	    (1ull << IB_USER_VERBS_CMD_DEREG_MR) |
	    (1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
	    (1ull << IB_USER_VERBS_CMD_CREATE_CQ) |
	    (1ull << IB_USER_VERBS_CMD_DESTROY_CQ) |
	    (1ull << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ) |
	    (1ull << IB_USER_VERBS_CMD_CREATE_QP) |
	    (1ull << IB_USER_VERBS_CMD_MODIFY_QP) |
	    (1ull << IB_USER_VERBS_CMD_POLL_CQ) |
	    (1ull << IB_USER_VERBS_CMD_DESTROY_QP) |
	    (1ull << IB_USER_VERBS_CMD_POST_SEND) |
	    (1ull << IB_USER_VERBS_CMD_POST_RECV);
	dev->ibdev.node_type = RDMA_NODE_RNIC;
	BUILD_BUG_ON(sizeof(IWCH_NODE_DESC) > IB_DEVICE_NODE_DESC_MAX);
	memcpy(dev->ibdev.node_desc, IWCH_NODE_DESC, sizeof(IWCH_NODE_DESC));
	dev->ibdev.phys_port_cnt = dev->rdev.port_info.nports;
	dev->ibdev.num_comp_vectors = 1;
	dev->ibdev.dma_device = &(dev->rdev.rnic_info.pdev->dev);
	dev->ibdev.query_device = iwch_query_device;
	dev->ibdev.query_port = iwch_query_port;
	dev->ibdev.query_pkey = iwch_query_pkey;
	dev->ibdev.query_gid = iwch_query_gid;
	dev->ibdev.alloc_ucontext = iwch_alloc_ucontext;
	dev->ibdev.dealloc_ucontext = iwch_dealloc_ucontext;
	dev->ibdev.mmap = iwch_mmap;
	dev->ibdev.alloc_pd = iwch_allocate_pd;
	dev->ibdev.dealloc_pd = iwch_deallocate_pd;
	dev->ibdev.create_ah = iwch_ah_create;
	dev->ibdev.destroy_ah = iwch_ah_destroy;
	dev->ibdev.create_qp = iwch_create_qp;
	dev->ibdev.modify_qp = iwch_ib_modify_qp;
	dev->ibdev.destroy_qp = iwch_destroy_qp;
	dev->ibdev.create_cq = iwch_create_cq;
	dev->ibdev.destroy_cq = iwch_destroy_cq;
	dev->ibdev.resize_cq = iwch_resize_cq;
	dev->ibdev.poll_cq = iwch_poll_cq;
	dev->ibdev.get_dma_mr = iwch_get_dma_mr;
	dev->ibdev.reg_user_mr = iwch_reg_user_mr;
	dev->ibdev.dereg_mr = iwch_dereg_mr;
	dev->ibdev.alloc_mw = iwch_alloc_mw;
	dev->ibdev.dealloc_mw = iwch_dealloc_mw;
	dev->ibdev.alloc_mr = iwch_alloc_mr;
	dev->ibdev.map_mr_sg = iwch_map_mr_sg;
	dev->ibdev.attach_mcast = iwch_multicast_attach;
	dev->ibdev.detach_mcast = iwch_multicast_detach;
	dev->ibdev.process_mad = iwch_process_mad;
	dev->ibdev.req_notify_cq = iwch_arm_cq;
	dev->ibdev.post_send = iwch_post_send;
	dev->ibdev.post_recv = iwch_post_receive;
	dev->ibdev.alloc_hw_stats = iwch_alloc_stats;
	dev->ibdev.get_hw_stats = iwch_get_mib;
	dev->ibdev.uverbs_abi_ver = IWCH_UVERBS_ABI_VERSION;
	dev->ibdev.get_port_immutable = iwch_port_immutable;
	dev->ibdev.get_dev_fw_str = get_dev_fw_ver_str;

	dev->ibdev.iwcm = kmalloc(sizeof(struct iw_cm_verbs), GFP_KERNEL);
	if (!dev->ibdev.iwcm)
		return -ENOMEM;

	dev->ibdev.iwcm->connect = iwch_connect;
	dev->ibdev.iwcm->accept = iwch_accept_cr;
	dev->ibdev.iwcm->reject = iwch_reject_cr;
	dev->ibdev.iwcm->create_listen = iwch_create_listen;
	dev->ibdev.iwcm->destroy_listen = iwch_destroy_listen;
	dev->ibdev.iwcm->add_ref = iwch_qp_add_ref;
	dev->ibdev.iwcm->rem_ref = iwch_qp_rem_ref;
	dev->ibdev.iwcm->get_qp = iwch_get_qp;
	memcpy(dev->ibdev.iwcm->ifname, dev->rdev.t3cdev_p->lldev->name,
	       sizeof(dev->ibdev.iwcm->ifname));

	ret = ib_register_device(&dev->ibdev, NULL);
	if (ret)
		goto bail1;

	for (i = 0; i < ARRAY_SIZE(iwch_class_attributes); ++i) {
		ret = device_create_file(&dev->ibdev.dev,
					 iwch_class_attributes[i]);
		if (ret) {
			goto bail2;
		}
	}
	return 0;
bail2:
	ib_unregister_device(&dev->ibdev);
bail1:
	kfree(dev->ibdev.iwcm);
	return ret;
}

void iwch_unregister_device(struct iwch_dev *dev)
{
	int i;

	PDBG("%s iwch_dev %p\n", __func__, dev);
	for (i = 0; i < ARRAY_SIZE(iwch_class_attributes); ++i)
		device_remove_file(&dev->ibdev.dev,
				   iwch_class_attributes[i]);
	ib_unregister_device(&dev->ibdev);
	kfree(dev->ibdev.iwcm);
	return;
}
