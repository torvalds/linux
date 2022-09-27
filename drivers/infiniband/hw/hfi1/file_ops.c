/*
 * Copyright(c) 2020 Cornelis Networks, Inc.
 * Copyright(c) 2015-2020 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/sched/mm.h>
#include <linux/bitmap.h>

#include <rdma/ib.h>

#include "hfi.h"
#include "pio.h"
#include "device.h"
#include "common.h"
#include "trace.h"
#include "mmu_rb.h"
#include "user_sdma.h"
#include "user_exp_rcv.h"
#include "aspm.h"

#undef pr_fmt
#define pr_fmt(fmt) DRIVER_NAME ": " fmt

#define SEND_CTXT_HALT_TIMEOUT 1000 /* msecs */

/*
 * File operation functions
 */
static int hfi1_file_open(struct inode *inode, struct file *fp);
static int hfi1_file_close(struct inode *inode, struct file *fp);
static ssize_t hfi1_write_iter(struct kiocb *kiocb, struct iov_iter *from);
static __poll_t hfi1_poll(struct file *fp, struct poll_table_struct *pt);
static int hfi1_file_mmap(struct file *fp, struct vm_area_struct *vma);

static u64 kvirt_to_phys(void *addr);
static int assign_ctxt(struct hfi1_filedata *fd, unsigned long arg, u32 len);
static void init_subctxts(struct hfi1_ctxtdata *uctxt,
			  const struct hfi1_user_info *uinfo);
static int init_user_ctxt(struct hfi1_filedata *fd,
			  struct hfi1_ctxtdata *uctxt);
static void user_init(struct hfi1_ctxtdata *uctxt);
static int get_ctxt_info(struct hfi1_filedata *fd, unsigned long arg, u32 len);
static int get_base_info(struct hfi1_filedata *fd, unsigned long arg, u32 len);
static int user_exp_rcv_setup(struct hfi1_filedata *fd, unsigned long arg,
			      u32 len);
static int user_exp_rcv_clear(struct hfi1_filedata *fd, unsigned long arg,
			      u32 len);
static int user_exp_rcv_invalid(struct hfi1_filedata *fd, unsigned long arg,
				u32 len);
static int setup_base_ctxt(struct hfi1_filedata *fd,
			   struct hfi1_ctxtdata *uctxt);
static int setup_subctxt(struct hfi1_ctxtdata *uctxt);

static int find_sub_ctxt(struct hfi1_filedata *fd,
			 const struct hfi1_user_info *uinfo);
static int allocate_ctxt(struct hfi1_filedata *fd, struct hfi1_devdata *dd,
			 struct hfi1_user_info *uinfo,
			 struct hfi1_ctxtdata **cd);
static void deallocate_ctxt(struct hfi1_ctxtdata *uctxt);
static __poll_t poll_urgent(struct file *fp, struct poll_table_struct *pt);
static __poll_t poll_next(struct file *fp, struct poll_table_struct *pt);
static int user_event_ack(struct hfi1_ctxtdata *uctxt, u16 subctxt,
			  unsigned long arg);
static int set_ctxt_pkey(struct hfi1_ctxtdata *uctxt, unsigned long arg);
static int ctxt_reset(struct hfi1_ctxtdata *uctxt);
static int manage_rcvq(struct hfi1_ctxtdata *uctxt, u16 subctxt,
		       unsigned long arg);
static vm_fault_t vma_fault(struct vm_fault *vmf);
static long hfi1_file_ioctl(struct file *fp, unsigned int cmd,
			    unsigned long arg);

static const struct file_operations hfi1_file_ops = {
	.owner = THIS_MODULE,
	.write_iter = hfi1_write_iter,
	.open = hfi1_file_open,
	.release = hfi1_file_close,
	.unlocked_ioctl = hfi1_file_ioctl,
	.poll = hfi1_poll,
	.mmap = hfi1_file_mmap,
	.llseek = noop_llseek,
};

static const struct vm_operations_struct vm_ops = {
	.fault = vma_fault,
};

/*
 * Types of memories mapped into user processes' space
 */
enum mmap_types {
	PIO_BUFS = 1,
	PIO_BUFS_SOP,
	PIO_CRED,
	RCV_HDRQ,
	RCV_EGRBUF,
	UREGS,
	EVENTS,
	STATUS,
	RTAIL,
	SUBCTXT_UREGS,
	SUBCTXT_RCV_HDRQ,
	SUBCTXT_EGRBUF,
	SDMA_COMP
};

/*
 * Masks and offsets defining the mmap tokens
 */
#define HFI1_MMAP_OFFSET_MASK   0xfffULL
#define HFI1_MMAP_OFFSET_SHIFT  0
#define HFI1_MMAP_SUBCTXT_MASK  0xfULL
#define HFI1_MMAP_SUBCTXT_SHIFT 12
#define HFI1_MMAP_CTXT_MASK     0xffULL
#define HFI1_MMAP_CTXT_SHIFT    16
#define HFI1_MMAP_TYPE_MASK     0xfULL
#define HFI1_MMAP_TYPE_SHIFT    24
#define HFI1_MMAP_MAGIC_MASK    0xffffffffULL
#define HFI1_MMAP_MAGIC_SHIFT   32

#define HFI1_MMAP_MAGIC         0xdabbad00

#define HFI1_MMAP_TOKEN_SET(field, val)	\
	(((val) & HFI1_MMAP_##field##_MASK) << HFI1_MMAP_##field##_SHIFT)
#define HFI1_MMAP_TOKEN_GET(field, token) \
	(((token) >> HFI1_MMAP_##field##_SHIFT) & HFI1_MMAP_##field##_MASK)
#define HFI1_MMAP_TOKEN(type, ctxt, subctxt, addr)   \
	(HFI1_MMAP_TOKEN_SET(MAGIC, HFI1_MMAP_MAGIC) | \
	HFI1_MMAP_TOKEN_SET(TYPE, type) | \
	HFI1_MMAP_TOKEN_SET(CTXT, ctxt) | \
	HFI1_MMAP_TOKEN_SET(SUBCTXT, subctxt) | \
	HFI1_MMAP_TOKEN_SET(OFFSET, (offset_in_page(addr))))

#define dbg(fmt, ...)				\
	pr_info(fmt, ##__VA_ARGS__)

static inline int is_valid_mmap(u64 token)
{
	return (HFI1_MMAP_TOKEN_GET(MAGIC, token) == HFI1_MMAP_MAGIC);
}

static int hfi1_file_open(struct inode *inode, struct file *fp)
{
	struct hfi1_filedata *fd;
	struct hfi1_devdata *dd = container_of(inode->i_cdev,
					       struct hfi1_devdata,
					       user_cdev);

	if (!((dd->flags & HFI1_PRESENT) && dd->kregbase1))
		return -EINVAL;

	if (!atomic_inc_not_zero(&dd->user_refcount))
		return -ENXIO;

	/* The real work is performed later in assign_ctxt() */

	fd = kzalloc(sizeof(*fd), GFP_KERNEL);

	if (!fd || init_srcu_struct(&fd->pq_srcu))
		goto nomem;
	spin_lock_init(&fd->pq_rcu_lock);
	spin_lock_init(&fd->tid_lock);
	spin_lock_init(&fd->invalid_lock);
	fd->rec_cpu_num = -1; /* no cpu affinity by default */
	fd->dd = dd;
	fp->private_data = fd;
	return 0;
nomem:
	kfree(fd);
	fp->private_data = NULL;
	if (atomic_dec_and_test(&dd->user_refcount))
		complete(&dd->user_comp);
	return -ENOMEM;
}

static long hfi1_file_ioctl(struct file *fp, unsigned int cmd,
			    unsigned long arg)
{
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	int ret = 0;
	int uval = 0;

	hfi1_cdbg(IOCTL, "IOCTL recv: 0x%x", cmd);
	if (cmd != HFI1_IOCTL_ASSIGN_CTXT &&
	    cmd != HFI1_IOCTL_GET_VERS &&
	    !uctxt)
		return -EINVAL;

	switch (cmd) {
	case HFI1_IOCTL_ASSIGN_CTXT:
		ret = assign_ctxt(fd, arg, _IOC_SIZE(cmd));
		break;

	case HFI1_IOCTL_CTXT_INFO:
		ret = get_ctxt_info(fd, arg, _IOC_SIZE(cmd));
		break;

	case HFI1_IOCTL_USER_INFO:
		ret = get_base_info(fd, arg, _IOC_SIZE(cmd));
		break;

	case HFI1_IOCTL_CREDIT_UPD:
		if (uctxt)
			sc_return_credits(uctxt->sc);
		break;

	case HFI1_IOCTL_TID_UPDATE:
		ret = user_exp_rcv_setup(fd, arg, _IOC_SIZE(cmd));
		break;

	case HFI1_IOCTL_TID_FREE:
		ret = user_exp_rcv_clear(fd, arg, _IOC_SIZE(cmd));
		break;

	case HFI1_IOCTL_TID_INVAL_READ:
		ret = user_exp_rcv_invalid(fd, arg, _IOC_SIZE(cmd));
		break;

	case HFI1_IOCTL_RECV_CTRL:
		ret = manage_rcvq(uctxt, fd->subctxt, arg);
		break;

	case HFI1_IOCTL_POLL_TYPE:
		if (get_user(uval, (int __user *)arg))
			return -EFAULT;
		uctxt->poll_type = (typeof(uctxt->poll_type))uval;
		break;

	case HFI1_IOCTL_ACK_EVENT:
		ret = user_event_ack(uctxt, fd->subctxt, arg);
		break;

	case HFI1_IOCTL_SET_PKEY:
		ret = set_ctxt_pkey(uctxt, arg);
		break;

	case HFI1_IOCTL_CTXT_RESET:
		ret = ctxt_reset(uctxt);
		break;

	case HFI1_IOCTL_GET_VERS:
		uval = HFI1_USER_SWVERSION;
		if (put_user(uval, (int __user *)arg))
			return -EFAULT;
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static ssize_t hfi1_write_iter(struct kiocb *kiocb, struct iov_iter *from)
{
	struct hfi1_filedata *fd = kiocb->ki_filp->private_data;
	struct hfi1_user_sdma_pkt_q *pq;
	struct hfi1_user_sdma_comp_q *cq = fd->cq;
	int done = 0, reqs = 0;
	unsigned long dim = from->nr_segs;
	int idx;

	if (!HFI1_CAP_IS_KSET(SDMA))
		return -EINVAL;
	idx = srcu_read_lock(&fd->pq_srcu);
	pq = srcu_dereference(fd->pq, &fd->pq_srcu);
	if (!cq || !pq) {
		srcu_read_unlock(&fd->pq_srcu, idx);
		return -EIO;
	}

	if (!iter_is_iovec(from) || !dim) {
		srcu_read_unlock(&fd->pq_srcu, idx);
		return -EINVAL;
	}

	trace_hfi1_sdma_request(fd->dd, fd->uctxt->ctxt, fd->subctxt, dim);

	if (atomic_read(&pq->n_reqs) == pq->n_max_reqs) {
		srcu_read_unlock(&fd->pq_srcu, idx);
		return -ENOSPC;
	}

	while (dim) {
		int ret;
		unsigned long count = 0;

		ret = hfi1_user_sdma_process_request(
			fd, (struct iovec *)(from->iov + done),
			dim, &count);
		if (ret) {
			reqs = ret;
			break;
		}
		dim -= count;
		done += count;
		reqs++;
	}

	srcu_read_unlock(&fd->pq_srcu, idx);
	return reqs;
}

static int hfi1_file_mmap(struct file *fp, struct vm_area_struct *vma)
{
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd;
	unsigned long flags;
	u64 token = vma->vm_pgoff << PAGE_SHIFT,
		memaddr = 0;
	void *memvirt = NULL;
	u8 subctxt, mapio = 0, vmf = 0, type;
	ssize_t memlen = 0;
	int ret = 0;
	u16 ctxt;

	if (!is_valid_mmap(token) || !uctxt ||
	    !(vma->vm_flags & VM_SHARED)) {
		ret = -EINVAL;
		goto done;
	}
	dd = uctxt->dd;
	ctxt = HFI1_MMAP_TOKEN_GET(CTXT, token);
	subctxt = HFI1_MMAP_TOKEN_GET(SUBCTXT, token);
	type = HFI1_MMAP_TOKEN_GET(TYPE, token);
	if (ctxt != uctxt->ctxt || subctxt != fd->subctxt) {
		ret = -EINVAL;
		goto done;
	}

	flags = vma->vm_flags;

	switch (type) {
	case PIO_BUFS:
	case PIO_BUFS_SOP:
		memaddr = ((dd->physaddr + TXE_PIO_SEND) +
				/* chip pio base */
			   (uctxt->sc->hw_context * BIT(16))) +
				/* 64K PIO space / ctxt */
			(type == PIO_BUFS_SOP ?
				(TXE_PIO_SIZE / 2) : 0); /* sop? */
		/*
		 * Map only the amount allocated to the context, not the
		 * entire available context's PIO space.
		 */
		memlen = PAGE_ALIGN(uctxt->sc->credits * PIO_BLOCK_SIZE);
		flags &= ~VM_MAYREAD;
		flags |= VM_DONTCOPY | VM_DONTEXPAND;
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		mapio = 1;
		break;
	case PIO_CRED:
		if (flags & VM_WRITE) {
			ret = -EPERM;
			goto done;
		}
		/*
		 * The credit return location for this context could be on the
		 * second or third page allocated for credit returns (if number
		 * of enabled contexts > 64 and 128 respectively).
		 */
		memvirt = dd->cr_base[uctxt->numa_id].va;
		memaddr = virt_to_phys(memvirt) +
			(((u64)uctxt->sc->hw_free -
			  (u64)dd->cr_base[uctxt->numa_id].va) & PAGE_MASK);
		memlen = PAGE_SIZE;
		flags &= ~VM_MAYWRITE;
		flags |= VM_DONTCOPY | VM_DONTEXPAND;
		/*
		 * The driver has already allocated memory for credit
		 * returns and programmed it into the chip. Has that
		 * memory been flagged as non-cached?
		 */
		/* vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot); */
		mapio = 1;
		break;
	case RCV_HDRQ:
		memlen = rcvhdrq_size(uctxt);
		memvirt = uctxt->rcvhdrq;
		break;
	case RCV_EGRBUF: {
		unsigned long addr;
		int i;
		/*
		 * The RcvEgr buffer need to be handled differently
		 * as multiple non-contiguous pages need to be mapped
		 * into the user process.
		 */
		memlen = uctxt->egrbufs.size;
		if ((vma->vm_end - vma->vm_start) != memlen) {
			dd_dev_err(dd, "Eager buffer map size invalid (%lu != %lu)\n",
				   (vma->vm_end - vma->vm_start), memlen);
			ret = -EINVAL;
			goto done;
		}
		if (vma->vm_flags & VM_WRITE) {
			ret = -EPERM;
			goto done;
		}
		vma->vm_flags &= ~VM_MAYWRITE;
		addr = vma->vm_start;
		for (i = 0 ; i < uctxt->egrbufs.numbufs; i++) {
			memlen = uctxt->egrbufs.buffers[i].len;
			memvirt = uctxt->egrbufs.buffers[i].addr;
			ret = remap_pfn_range(
				vma, addr,
				/*
				 * virt_to_pfn() does the same, but
				 * it's not available on x86_64
				 * when CONFIG_MMU is enabled.
				 */
				PFN_DOWN(__pa(memvirt)),
				memlen,
				vma->vm_page_prot);
			if (ret < 0)
				goto done;
			addr += memlen;
		}
		ret = 0;
		goto done;
	}
	case UREGS:
		/*
		 * Map only the page that contains this context's user
		 * registers.
		 */
		memaddr = (unsigned long)
			(dd->physaddr + RXE_PER_CONTEXT_USER)
			+ (uctxt->ctxt * RXE_PER_CONTEXT_SIZE);
		/*
		 * TidFlow table is on the same page as the rest of the
		 * user registers.
		 */
		memlen = PAGE_SIZE;
		flags |= VM_DONTCOPY | VM_DONTEXPAND;
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		mapio = 1;
		break;
	case EVENTS:
		/*
		 * Use the page where this context's flags are. User level
		 * knows where it's own bitmap is within the page.
		 */
		memaddr = (unsigned long)
			(dd->events + uctxt_offset(uctxt)) & PAGE_MASK;
		memlen = PAGE_SIZE;
		/*
		 * v3.7 removes VM_RESERVED but the effect is kept by
		 * using VM_IO.
		 */
		flags |= VM_IO | VM_DONTEXPAND;
		vmf = 1;
		break;
	case STATUS:
		if (flags & VM_WRITE) {
			ret = -EPERM;
			goto done;
		}
		memaddr = kvirt_to_phys((void *)dd->status);
		memlen = PAGE_SIZE;
		flags |= VM_IO | VM_DONTEXPAND;
		break;
	case RTAIL:
		if (!HFI1_CAP_IS_USET(DMA_RTAIL)) {
			/*
			 * If the memory allocation failed, the context alloc
			 * also would have failed, so we would never get here
			 */
			ret = -EINVAL;
			goto done;
		}
		if ((flags & VM_WRITE) || !hfi1_rcvhdrtail_kvaddr(uctxt)) {
			ret = -EPERM;
			goto done;
		}
		memlen = PAGE_SIZE;
		memvirt = (void *)hfi1_rcvhdrtail_kvaddr(uctxt);
		flags &= ~VM_MAYWRITE;
		break;
	case SUBCTXT_UREGS:
		memaddr = (u64)uctxt->subctxt_uregbase;
		memlen = PAGE_SIZE;
		flags |= VM_IO | VM_DONTEXPAND;
		vmf = 1;
		break;
	case SUBCTXT_RCV_HDRQ:
		memaddr = (u64)uctxt->subctxt_rcvhdr_base;
		memlen = rcvhdrq_size(uctxt) * uctxt->subctxt_cnt;
		flags |= VM_IO | VM_DONTEXPAND;
		vmf = 1;
		break;
	case SUBCTXT_EGRBUF:
		memaddr = (u64)uctxt->subctxt_rcvegrbuf;
		memlen = uctxt->egrbufs.size * uctxt->subctxt_cnt;
		flags |= VM_IO | VM_DONTEXPAND;
		flags &= ~VM_MAYWRITE;
		vmf = 1;
		break;
	case SDMA_COMP: {
		struct hfi1_user_sdma_comp_q *cq = fd->cq;

		if (!cq) {
			ret = -EFAULT;
			goto done;
		}
		memaddr = (u64)cq->comps;
		memlen = PAGE_ALIGN(sizeof(*cq->comps) * cq->nentries);
		flags |= VM_IO | VM_DONTEXPAND;
		vmf = 1;
		break;
	}
	default:
		ret = -EINVAL;
		break;
	}

	if ((vma->vm_end - vma->vm_start) != memlen) {
		hfi1_cdbg(PROC, "%u:%u Memory size mismatch %lu:%lu",
			  uctxt->ctxt, fd->subctxt,
			  (vma->vm_end - vma->vm_start), memlen);
		ret = -EINVAL;
		goto done;
	}

	vma->vm_flags = flags;
	hfi1_cdbg(PROC,
		  "%u:%u type:%u io/vf:%d/%d, addr:0x%llx, len:%lu(%lu), flags:0x%lx\n",
		    ctxt, subctxt, type, mapio, vmf, memaddr, memlen,
		    vma->vm_end - vma->vm_start, vma->vm_flags);
	if (vmf) {
		vma->vm_pgoff = PFN_DOWN(memaddr);
		vma->vm_ops = &vm_ops;
		ret = 0;
	} else if (mapio) {
		ret = io_remap_pfn_range(vma, vma->vm_start,
					 PFN_DOWN(memaddr),
					 memlen,
					 vma->vm_page_prot);
	} else if (memvirt) {
		ret = remap_pfn_range(vma, vma->vm_start,
				      PFN_DOWN(__pa(memvirt)),
				      memlen,
				      vma->vm_page_prot);
	} else {
		ret = remap_pfn_range(vma, vma->vm_start,
				      PFN_DOWN(memaddr),
				      memlen,
				      vma->vm_page_prot);
	}
done:
	return ret;
}

/*
 * Local (non-chip) user memory is not mapped right away but as it is
 * accessed by the user-level code.
 */
static vm_fault_t vma_fault(struct vm_fault *vmf)
{
	struct page *page;

	page = vmalloc_to_page((void *)(vmf->pgoff << PAGE_SHIFT));
	if (!page)
		return VM_FAULT_SIGBUS;

	get_page(page);
	vmf->page = page;

	return 0;
}

static __poll_t hfi1_poll(struct file *fp, struct poll_table_struct *pt)
{
	struct hfi1_ctxtdata *uctxt;
	__poll_t pollflag;

	uctxt = ((struct hfi1_filedata *)fp->private_data)->uctxt;
	if (!uctxt)
		pollflag = EPOLLERR;
	else if (uctxt->poll_type == HFI1_POLL_TYPE_URGENT)
		pollflag = poll_urgent(fp, pt);
	else  if (uctxt->poll_type == HFI1_POLL_TYPE_ANYRCV)
		pollflag = poll_next(fp, pt);
	else /* invalid */
		pollflag = EPOLLERR;

	return pollflag;
}

static int hfi1_file_close(struct inode *inode, struct file *fp)
{
	struct hfi1_filedata *fdata = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fdata->uctxt;
	struct hfi1_devdata *dd = container_of(inode->i_cdev,
					       struct hfi1_devdata,
					       user_cdev);
	unsigned long flags, *ev;

	fp->private_data = NULL;

	if (!uctxt)
		goto done;

	hfi1_cdbg(PROC, "closing ctxt %u:%u", uctxt->ctxt, fdata->subctxt);

	flush_wc();
	/* drain user sdma queue */
	hfi1_user_sdma_free_queues(fdata, uctxt);

	/* release the cpu */
	hfi1_put_proc_affinity(fdata->rec_cpu_num);

	/* clean up rcv side */
	hfi1_user_exp_rcv_free(fdata);

	/*
	 * fdata->uctxt is used in the above cleanup.  It is not ready to be
	 * removed until here.
	 */
	fdata->uctxt = NULL;
	hfi1_rcd_put(uctxt);

	/*
	 * Clear any left over, unhandled events so the next process that
	 * gets this context doesn't get confused.
	 */
	ev = dd->events + uctxt_offset(uctxt) + fdata->subctxt;
	*ev = 0;

	spin_lock_irqsave(&dd->uctxt_lock, flags);
	__clear_bit(fdata->subctxt, uctxt->in_use_ctxts);
	if (!bitmap_empty(uctxt->in_use_ctxts, HFI1_MAX_SHARED_CTXTS)) {
		spin_unlock_irqrestore(&dd->uctxt_lock, flags);
		goto done;
	}
	spin_unlock_irqrestore(&dd->uctxt_lock, flags);

	/*
	 * Disable receive context and interrupt available, reset all
	 * RcvCtxtCtrl bits to default values.
	 */
	hfi1_rcvctrl(dd, HFI1_RCVCTRL_CTXT_DIS |
		     HFI1_RCVCTRL_TIDFLOW_DIS |
		     HFI1_RCVCTRL_INTRAVAIL_DIS |
		     HFI1_RCVCTRL_TAILUPD_DIS |
		     HFI1_RCVCTRL_ONE_PKT_EGR_DIS |
		     HFI1_RCVCTRL_NO_RHQ_DROP_DIS |
		     HFI1_RCVCTRL_NO_EGR_DROP_DIS |
		     HFI1_RCVCTRL_URGENT_DIS, uctxt);
	/* Clear the context's J_KEY */
	hfi1_clear_ctxt_jkey(dd, uctxt);
	/*
	 * If a send context is allocated, reset context integrity
	 * checks to default and disable the send context.
	 */
	if (uctxt->sc) {
		sc_disable(uctxt->sc);
		set_pio_integrity(uctxt->sc);
	}

	hfi1_free_ctxt_rcv_groups(uctxt);
	hfi1_clear_ctxt_pkey(dd, uctxt);

	uctxt->event_flags = 0;

	deallocate_ctxt(uctxt);
done:

	if (atomic_dec_and_test(&dd->user_refcount))
		complete(&dd->user_comp);

	cleanup_srcu_struct(&fdata->pq_srcu);
	kfree(fdata);
	return 0;
}

/*
 * Convert kernel *virtual* addresses to physical addresses.
 * This is used to vmalloc'ed addresses.
 */
static u64 kvirt_to_phys(void *addr)
{
	struct page *page;
	u64 paddr = 0;

	page = vmalloc_to_page(addr);
	if (page)
		paddr = page_to_pfn(page) << PAGE_SHIFT;

	return paddr;
}

/**
 * complete_subctxt
 * @fd: valid filedata pointer
 *
 * Sub-context info can only be set up after the base context
 * has been completed.  This is indicated by the clearing of the
 * HFI1_CTXT_BASE_UINIT bit.
 *
 * Wait for the bit to be cleared, and then complete the subcontext
 * initialization.
 *
 */
static int complete_subctxt(struct hfi1_filedata *fd)
{
	int ret;
	unsigned long flags;

	/*
	 * sub-context info can only be set up after the base context
	 * has been completed.
	 */
	ret = wait_event_interruptible(
		fd->uctxt->wait,
		!test_bit(HFI1_CTXT_BASE_UNINIT, &fd->uctxt->event_flags));

	if (test_bit(HFI1_CTXT_BASE_FAILED, &fd->uctxt->event_flags))
		ret = -ENOMEM;

	/* Finish the sub-context init */
	if (!ret) {
		fd->rec_cpu_num = hfi1_get_proc_affinity(fd->uctxt->numa_id);
		ret = init_user_ctxt(fd, fd->uctxt);
	}

	if (ret) {
		spin_lock_irqsave(&fd->dd->uctxt_lock, flags);
		__clear_bit(fd->subctxt, fd->uctxt->in_use_ctxts);
		spin_unlock_irqrestore(&fd->dd->uctxt_lock, flags);
		hfi1_rcd_put(fd->uctxt);
		fd->uctxt = NULL;
	}

	return ret;
}

static int assign_ctxt(struct hfi1_filedata *fd, unsigned long arg, u32 len)
{
	int ret;
	unsigned int swmajor;
	struct hfi1_ctxtdata *uctxt = NULL;
	struct hfi1_user_info uinfo;

	if (fd->uctxt)
		return -EINVAL;

	if (sizeof(uinfo) != len)
		return -EINVAL;

	if (copy_from_user(&uinfo, (void __user *)arg, sizeof(uinfo)))
		return -EFAULT;

	swmajor = uinfo.userversion >> 16;
	if (swmajor != HFI1_USER_SWMAJOR)
		return -ENODEV;

	if (uinfo.subctxt_cnt > HFI1_MAX_SHARED_CTXTS)
		return -EINVAL;

	/*
	 * Acquire the mutex to protect against multiple creations of what
	 * could be a shared base context.
	 */
	mutex_lock(&hfi1_mutex);
	/*
	 * Get a sub context if available  (fd->uctxt will be set).
	 * ret < 0 error, 0 no context, 1 sub-context found
	 */
	ret = find_sub_ctxt(fd, &uinfo);

	/*
	 * Allocate a base context if context sharing is not required or a
	 * sub context wasn't found.
	 */
	if (!ret)
		ret = allocate_ctxt(fd, fd->dd, &uinfo, &uctxt);

	mutex_unlock(&hfi1_mutex);

	/* Depending on the context type, finish the appropriate init */
	switch (ret) {
	case 0:
		ret = setup_base_ctxt(fd, uctxt);
		if (ret)
			deallocate_ctxt(uctxt);
		break;
	case 1:
		ret = complete_subctxt(fd);
		break;
	default:
		break;
	}

	return ret;
}

/**
 * match_ctxt
 * @fd: valid filedata pointer
 * @uinfo: user info to compare base context with
 * @uctxt: context to compare uinfo to.
 *
 * Compare the given context with the given information to see if it
 * can be used for a sub context.
 */
static int match_ctxt(struct hfi1_filedata *fd,
		      const struct hfi1_user_info *uinfo,
		      struct hfi1_ctxtdata *uctxt)
{
	struct hfi1_devdata *dd = fd->dd;
	unsigned long flags;
	u16 subctxt;

	/* Skip dynamically allocated kernel contexts */
	if (uctxt->sc && (uctxt->sc->type == SC_KERNEL))
		return 0;

	/* Skip ctxt if it doesn't match the requested one */
	if (memcmp(uctxt->uuid, uinfo->uuid, sizeof(uctxt->uuid)) ||
	    uctxt->jkey != generate_jkey(current_uid()) ||
	    uctxt->subctxt_id != uinfo->subctxt_id ||
	    uctxt->subctxt_cnt != uinfo->subctxt_cnt)
		return 0;

	/* Verify the sharing process matches the base */
	if (uctxt->userversion != uinfo->userversion)
		return -EINVAL;

	/* Find an unused sub context */
	spin_lock_irqsave(&dd->uctxt_lock, flags);
	if (bitmap_empty(uctxt->in_use_ctxts, HFI1_MAX_SHARED_CTXTS)) {
		/* context is being closed, do not use */
		spin_unlock_irqrestore(&dd->uctxt_lock, flags);
		return 0;
	}

	subctxt = find_first_zero_bit(uctxt->in_use_ctxts,
				      HFI1_MAX_SHARED_CTXTS);
	if (subctxt >= uctxt->subctxt_cnt) {
		spin_unlock_irqrestore(&dd->uctxt_lock, flags);
		return -EBUSY;
	}

	fd->subctxt = subctxt;
	__set_bit(fd->subctxt, uctxt->in_use_ctxts);
	spin_unlock_irqrestore(&dd->uctxt_lock, flags);

	fd->uctxt = uctxt;
	hfi1_rcd_get(uctxt);

	return 1;
}

/**
 * find_sub_ctxt
 * @fd: valid filedata pointer
 * @uinfo: matching info to use to find a possible context to share.
 *
 * The hfi1_mutex must be held when this function is called.  It is
 * necessary to ensure serialized creation of shared contexts.
 *
 * Return:
 *    0      No sub-context found
 *    1      Subcontext found and allocated
 *    errno  EINVAL (incorrect parameters)
 *           EBUSY (all sub contexts in use)
 */
static int find_sub_ctxt(struct hfi1_filedata *fd,
			 const struct hfi1_user_info *uinfo)
{
	struct hfi1_ctxtdata *uctxt;
	struct hfi1_devdata *dd = fd->dd;
	u16 i;
	int ret;

	if (!uinfo->subctxt_cnt)
		return 0;

	for (i = dd->first_dyn_alloc_ctxt; i < dd->num_rcv_contexts; i++) {
		uctxt = hfi1_rcd_get_by_index(dd, i);
		if (uctxt) {
			ret = match_ctxt(fd, uinfo, uctxt);
			hfi1_rcd_put(uctxt);
			/* value of != 0 will return */
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int allocate_ctxt(struct hfi1_filedata *fd, struct hfi1_devdata *dd,
			 struct hfi1_user_info *uinfo,
			 struct hfi1_ctxtdata **rcd)
{
	struct hfi1_ctxtdata *uctxt;
	int ret, numa;

	if (dd->flags & HFI1_FROZEN) {
		/*
		 * Pick an error that is unique from all other errors
		 * that are returned so the user process knows that
		 * it tried to allocate while the SPC was frozen.  It
		 * it should be able to retry with success in a short
		 * while.
		 */
		return -EIO;
	}

	if (!dd->freectxts)
		return -EBUSY;

	/*
	 * If we don't have a NUMA node requested, preference is towards
	 * device NUMA node.
	 */
	fd->rec_cpu_num = hfi1_get_proc_affinity(dd->node);
	if (fd->rec_cpu_num != -1)
		numa = cpu_to_node(fd->rec_cpu_num);
	else
		numa = numa_node_id();
	ret = hfi1_create_ctxtdata(dd->pport, numa, &uctxt);
	if (ret < 0) {
		dd_dev_err(dd, "user ctxtdata allocation failed\n");
		return ret;
	}
	hfi1_cdbg(PROC, "[%u:%u] pid %u assigned to CPU %d (NUMA %u)",
		  uctxt->ctxt, fd->subctxt, current->pid, fd->rec_cpu_num,
		  uctxt->numa_id);

	/*
	 * Allocate and enable a PIO send context.
	 */
	uctxt->sc = sc_alloc(dd, SC_USER, uctxt->rcvhdrqentsize, dd->node);
	if (!uctxt->sc) {
		ret = -ENOMEM;
		goto ctxdata_free;
	}
	hfi1_cdbg(PROC, "allocated send context %u(%u)\n", uctxt->sc->sw_index,
		  uctxt->sc->hw_context);
	ret = sc_enable(uctxt->sc);
	if (ret)
		goto ctxdata_free;

	/*
	 * Setup sub context information if the user-level has requested
	 * sub contexts.
	 * This has to be done here so the rest of the sub-contexts find the
	 * proper base context.
	 * NOTE: _set_bit() can be used here because the context creation is
	 * protected by the mutex (rather than the spin_lock), and will be the
	 * very first instance of this context.
	 */
	__set_bit(0, uctxt->in_use_ctxts);
	if (uinfo->subctxt_cnt)
		init_subctxts(uctxt, uinfo);
	uctxt->userversion = uinfo->userversion;
	uctxt->flags = hfi1_cap_mask; /* save current flag state */
	init_waitqueue_head(&uctxt->wait);
	strlcpy(uctxt->comm, current->comm, sizeof(uctxt->comm));
	memcpy(uctxt->uuid, uinfo->uuid, sizeof(uctxt->uuid));
	uctxt->jkey = generate_jkey(current_uid());
	hfi1_stats.sps_ctxts++;
	/*
	 * Disable ASPM when there are open user/PSM contexts to avoid
	 * issues with ASPM L1 exit latency
	 */
	if (dd->freectxts-- == dd->num_user_contexts)
		aspm_disable_all(dd);

	*rcd = uctxt;

	return 0;

ctxdata_free:
	hfi1_free_ctxt(uctxt);
	return ret;
}

static void deallocate_ctxt(struct hfi1_ctxtdata *uctxt)
{
	mutex_lock(&hfi1_mutex);
	hfi1_stats.sps_ctxts--;
	if (++uctxt->dd->freectxts == uctxt->dd->num_user_contexts)
		aspm_enable_all(uctxt->dd);
	mutex_unlock(&hfi1_mutex);

	hfi1_free_ctxt(uctxt);
}

static void init_subctxts(struct hfi1_ctxtdata *uctxt,
			  const struct hfi1_user_info *uinfo)
{
	uctxt->subctxt_cnt = uinfo->subctxt_cnt;
	uctxt->subctxt_id = uinfo->subctxt_id;
	set_bit(HFI1_CTXT_BASE_UNINIT, &uctxt->event_flags);
}

static int setup_subctxt(struct hfi1_ctxtdata *uctxt)
{
	int ret = 0;
	u16 num_subctxts = uctxt->subctxt_cnt;

	uctxt->subctxt_uregbase = vmalloc_user(PAGE_SIZE);
	if (!uctxt->subctxt_uregbase)
		return -ENOMEM;

	/* We can take the size of the RcvHdr Queue from the master */
	uctxt->subctxt_rcvhdr_base = vmalloc_user(rcvhdrq_size(uctxt) *
						  num_subctxts);
	if (!uctxt->subctxt_rcvhdr_base) {
		ret = -ENOMEM;
		goto bail_ureg;
	}

	uctxt->subctxt_rcvegrbuf = vmalloc_user(uctxt->egrbufs.size *
						num_subctxts);
	if (!uctxt->subctxt_rcvegrbuf) {
		ret = -ENOMEM;
		goto bail_rhdr;
	}

	return 0;

bail_rhdr:
	vfree(uctxt->subctxt_rcvhdr_base);
	uctxt->subctxt_rcvhdr_base = NULL;
bail_ureg:
	vfree(uctxt->subctxt_uregbase);
	uctxt->subctxt_uregbase = NULL;

	return ret;
}

static void user_init(struct hfi1_ctxtdata *uctxt)
{
	unsigned int rcvctrl_ops = 0;

	/* initialize poll variables... */
	uctxt->urgent = 0;
	uctxt->urgent_poll = 0;

	/*
	 * Now enable the ctxt for receive.
	 * For chips that are set to DMA the tail register to memory
	 * when they change (and when the update bit transitions from
	 * 0 to 1.  So for those chips, we turn it off and then back on.
	 * This will (very briefly) affect any other open ctxts, but the
	 * duration is very short, and therefore isn't an issue.  We
	 * explicitly set the in-memory tail copy to 0 beforehand, so we
	 * don't have to wait to be sure the DMA update has happened
	 * (chip resets head/tail to 0 on transition to enable).
	 */
	if (hfi1_rcvhdrtail_kvaddr(uctxt))
		clear_rcvhdrtail(uctxt);

	/* Setup J_KEY before enabling the context */
	hfi1_set_ctxt_jkey(uctxt->dd, uctxt, uctxt->jkey);

	rcvctrl_ops = HFI1_RCVCTRL_CTXT_ENB;
	rcvctrl_ops |= HFI1_RCVCTRL_URGENT_ENB;
	if (HFI1_CAP_UGET_MASK(uctxt->flags, HDRSUPP))
		rcvctrl_ops |= HFI1_RCVCTRL_TIDFLOW_ENB;
	/*
	 * Ignore the bit in the flags for now until proper
	 * support for multiple packet per rcv array entry is
	 * added.
	 */
	if (!HFI1_CAP_UGET_MASK(uctxt->flags, MULTI_PKT_EGR))
		rcvctrl_ops |= HFI1_RCVCTRL_ONE_PKT_EGR_ENB;
	if (HFI1_CAP_UGET_MASK(uctxt->flags, NODROP_EGR_FULL))
		rcvctrl_ops |= HFI1_RCVCTRL_NO_EGR_DROP_ENB;
	if (HFI1_CAP_UGET_MASK(uctxt->flags, NODROP_RHQ_FULL))
		rcvctrl_ops |= HFI1_RCVCTRL_NO_RHQ_DROP_ENB;
	/*
	 * The RcvCtxtCtrl.TailUpd bit has to be explicitly written.
	 * We can't rely on the correct value to be set from prior
	 * uses of the chip or ctxt. Therefore, add the rcvctrl op
	 * for both cases.
	 */
	if (HFI1_CAP_UGET_MASK(uctxt->flags, DMA_RTAIL))
		rcvctrl_ops |= HFI1_RCVCTRL_TAILUPD_ENB;
	else
		rcvctrl_ops |= HFI1_RCVCTRL_TAILUPD_DIS;
	hfi1_rcvctrl(uctxt->dd, rcvctrl_ops, uctxt);
}

static int get_ctxt_info(struct hfi1_filedata *fd, unsigned long arg, u32 len)
{
	struct hfi1_ctxt_info cinfo;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;

	if (sizeof(cinfo) != len)
		return -EINVAL;

	memset(&cinfo, 0, sizeof(cinfo));
	cinfo.runtime_flags = (((uctxt->flags >> HFI1_CAP_MISC_SHIFT) &
				HFI1_CAP_MISC_MASK) << HFI1_CAP_USER_SHIFT) |
			HFI1_CAP_UGET_MASK(uctxt->flags, MASK) |
			HFI1_CAP_KGET_MASK(uctxt->flags, K2U);
	/* adjust flag if this fd is not able to cache */
	if (!fd->use_mn)
		cinfo.runtime_flags |= HFI1_CAP_TID_UNMAP; /* no caching */

	cinfo.num_active = hfi1_count_active_units();
	cinfo.unit = uctxt->dd->unit;
	cinfo.ctxt = uctxt->ctxt;
	cinfo.subctxt = fd->subctxt;
	cinfo.rcvtids = roundup(uctxt->egrbufs.alloced,
				uctxt->dd->rcv_entries.group_size) +
		uctxt->expected_count;
	cinfo.credits = uctxt->sc->credits;
	cinfo.numa_node = uctxt->numa_id;
	cinfo.rec_cpu = fd->rec_cpu_num;
	cinfo.send_ctxt = uctxt->sc->hw_context;

	cinfo.egrtids = uctxt->egrbufs.alloced;
	cinfo.rcvhdrq_cnt = get_hdrq_cnt(uctxt);
	cinfo.rcvhdrq_entsize = get_hdrqentsize(uctxt) << 2;
	cinfo.sdma_ring_size = fd->cq->nentries;
	cinfo.rcvegr_size = uctxt->egrbufs.rcvtid_size;

	trace_hfi1_ctxt_info(uctxt->dd, uctxt->ctxt, fd->subctxt, &cinfo);
	if (copy_to_user((void __user *)arg, &cinfo, len))
		return -EFAULT;

	return 0;
}

static int init_user_ctxt(struct hfi1_filedata *fd,
			  struct hfi1_ctxtdata *uctxt)
{
	int ret;

	ret = hfi1_user_sdma_alloc_queues(uctxt, fd);
	if (ret)
		return ret;

	ret = hfi1_user_exp_rcv_init(fd, uctxt);
	if (ret)
		hfi1_user_sdma_free_queues(fd, uctxt);

	return ret;
}

static int setup_base_ctxt(struct hfi1_filedata *fd,
			   struct hfi1_ctxtdata *uctxt)
{
	struct hfi1_devdata *dd = uctxt->dd;
	int ret = 0;

	hfi1_init_ctxt(uctxt->sc);

	/* Now allocate the RcvHdr queue and eager buffers. */
	ret = hfi1_create_rcvhdrq(dd, uctxt);
	if (ret)
		goto done;

	ret = hfi1_setup_eagerbufs(uctxt);
	if (ret)
		goto done;

	/* If sub-contexts are enabled, do the appropriate setup */
	if (uctxt->subctxt_cnt)
		ret = setup_subctxt(uctxt);
	if (ret)
		goto done;

	ret = hfi1_alloc_ctxt_rcv_groups(uctxt);
	if (ret)
		goto done;

	ret = init_user_ctxt(fd, uctxt);
	if (ret)
		goto done;

	user_init(uctxt);

	/* Now that the context is set up, the fd can get a reference. */
	fd->uctxt = uctxt;
	hfi1_rcd_get(uctxt);

done:
	if (uctxt->subctxt_cnt) {
		/*
		 * On error, set the failed bit so sub-contexts will clean up
		 * correctly.
		 */
		if (ret)
			set_bit(HFI1_CTXT_BASE_FAILED, &uctxt->event_flags);

		/*
		 * Base context is done (successfully or not), notify anybody
		 * using a sub-context that is waiting for this completion.
		 */
		clear_bit(HFI1_CTXT_BASE_UNINIT, &uctxt->event_flags);
		wake_up(&uctxt->wait);
	}

	return ret;
}

static int get_base_info(struct hfi1_filedata *fd, unsigned long arg, u32 len)
{
	struct hfi1_base_info binfo;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	unsigned offset;

	trace_hfi1_uctxtdata(uctxt->dd, uctxt, fd->subctxt);

	if (sizeof(binfo) != len)
		return -EINVAL;

	memset(&binfo, 0, sizeof(binfo));
	binfo.hw_version = dd->revision;
	binfo.sw_version = HFI1_KERN_SWVERSION;
	binfo.bthqp = RVT_KDETH_QP_PREFIX;
	binfo.jkey = uctxt->jkey;
	/*
	 * If more than 64 contexts are enabled the allocated credit
	 * return will span two or three contiguous pages. Since we only
	 * map the page containing the context's credit return address,
	 * we need to calculate the offset in the proper page.
	 */
	offset = ((u64)uctxt->sc->hw_free -
		  (u64)dd->cr_base[uctxt->numa_id].va) % PAGE_SIZE;
	binfo.sc_credits_addr = HFI1_MMAP_TOKEN(PIO_CRED, uctxt->ctxt,
						fd->subctxt, offset);
	binfo.pio_bufbase = HFI1_MMAP_TOKEN(PIO_BUFS, uctxt->ctxt,
					    fd->subctxt,
					    uctxt->sc->base_addr);
	binfo.pio_bufbase_sop = HFI1_MMAP_TOKEN(PIO_BUFS_SOP,
						uctxt->ctxt,
						fd->subctxt,
						uctxt->sc->base_addr);
	binfo.rcvhdr_bufbase = HFI1_MMAP_TOKEN(RCV_HDRQ, uctxt->ctxt,
					       fd->subctxt,
					       uctxt->rcvhdrq);
	binfo.rcvegr_bufbase = HFI1_MMAP_TOKEN(RCV_EGRBUF, uctxt->ctxt,
					       fd->subctxt,
					       uctxt->egrbufs.rcvtids[0].dma);
	binfo.sdma_comp_bufbase = HFI1_MMAP_TOKEN(SDMA_COMP, uctxt->ctxt,
						  fd->subctxt, 0);
	/*
	 * user regs are at
	 * (RXE_PER_CONTEXT_USER + (ctxt * RXE_PER_CONTEXT_SIZE))
	 */
	binfo.user_regbase = HFI1_MMAP_TOKEN(UREGS, uctxt->ctxt,
					     fd->subctxt, 0);
	offset = offset_in_page((uctxt_offset(uctxt) + fd->subctxt) *
				sizeof(*dd->events));
	binfo.events_bufbase = HFI1_MMAP_TOKEN(EVENTS, uctxt->ctxt,
					       fd->subctxt,
					       offset);
	binfo.status_bufbase = HFI1_MMAP_TOKEN(STATUS, uctxt->ctxt,
					       fd->subctxt,
					       dd->status);
	if (HFI1_CAP_IS_USET(DMA_RTAIL))
		binfo.rcvhdrtail_base = HFI1_MMAP_TOKEN(RTAIL, uctxt->ctxt,
							fd->subctxt, 0);
	if (uctxt->subctxt_cnt) {
		binfo.subctxt_uregbase = HFI1_MMAP_TOKEN(SUBCTXT_UREGS,
							 uctxt->ctxt,
							 fd->subctxt, 0);
		binfo.subctxt_rcvhdrbuf = HFI1_MMAP_TOKEN(SUBCTXT_RCV_HDRQ,
							  uctxt->ctxt,
							  fd->subctxt, 0);
		binfo.subctxt_rcvegrbuf = HFI1_MMAP_TOKEN(SUBCTXT_EGRBUF,
							  uctxt->ctxt,
							  fd->subctxt, 0);
	}

	if (copy_to_user((void __user *)arg, &binfo, len))
		return -EFAULT;

	return 0;
}

/**
 * user_exp_rcv_setup - Set up the given tid rcv list
 * @fd: file data of the current driver instance
 * @arg: ioctl argumnent for user space information
 * @len: length of data structure associated with ioctl command
 *
 * Wrapper to validate ioctl information before doing _rcv_setup.
 *
 */
static int user_exp_rcv_setup(struct hfi1_filedata *fd, unsigned long arg,
			      u32 len)
{
	int ret;
	unsigned long addr;
	struct hfi1_tid_info tinfo;

	if (sizeof(tinfo) != len)
		return -EINVAL;

	if (copy_from_user(&tinfo, (void __user *)arg, (sizeof(tinfo))))
		return -EFAULT;

	ret = hfi1_user_exp_rcv_setup(fd, &tinfo);
	if (!ret) {
		/*
		 * Copy the number of tidlist entries we used
		 * and the length of the buffer we registered.
		 */
		addr = arg + offsetof(struct hfi1_tid_info, tidcnt);
		if (copy_to_user((void __user *)addr, &tinfo.tidcnt,
				 sizeof(tinfo.tidcnt)))
			return -EFAULT;

		addr = arg + offsetof(struct hfi1_tid_info, length);
		if (copy_to_user((void __user *)addr, &tinfo.length,
				 sizeof(tinfo.length)))
			ret = -EFAULT;
	}

	return ret;
}

/**
 * user_exp_rcv_clear - Clear the given tid rcv list
 * @fd: file data of the current driver instance
 * @arg: ioctl argumnent for user space information
 * @len: length of data structure associated with ioctl command
 *
 * The hfi1_user_exp_rcv_clear() can be called from the error path.  Because
 * of this, we need to use this wrapper to copy the user space information
 * before doing the clear.
 */
static int user_exp_rcv_clear(struct hfi1_filedata *fd, unsigned long arg,
			      u32 len)
{
	int ret;
	unsigned long addr;
	struct hfi1_tid_info tinfo;

	if (sizeof(tinfo) != len)
		return -EINVAL;

	if (copy_from_user(&tinfo, (void __user *)arg, (sizeof(tinfo))))
		return -EFAULT;

	ret = hfi1_user_exp_rcv_clear(fd, &tinfo);
	if (!ret) {
		addr = arg + offsetof(struct hfi1_tid_info, tidcnt);
		if (copy_to_user((void __user *)addr, &tinfo.tidcnt,
				 sizeof(tinfo.tidcnt)))
			return -EFAULT;
	}

	return ret;
}

/**
 * user_exp_rcv_invalid - Invalidate the given tid rcv list
 * @fd: file data of the current driver instance
 * @arg: ioctl argumnent for user space information
 * @len: length of data structure associated with ioctl command
 *
 * Wrapper to validate ioctl information before doing _rcv_invalid.
 *
 */
static int user_exp_rcv_invalid(struct hfi1_filedata *fd, unsigned long arg,
				u32 len)
{
	int ret;
	unsigned long addr;
	struct hfi1_tid_info tinfo;

	if (sizeof(tinfo) != len)
		return -EINVAL;

	if (!fd->invalid_tids)
		return -EINVAL;

	if (copy_from_user(&tinfo, (void __user *)arg, (sizeof(tinfo))))
		return -EFAULT;

	ret = hfi1_user_exp_rcv_invalid(fd, &tinfo);
	if (ret)
		return ret;

	addr = arg + offsetof(struct hfi1_tid_info, tidcnt);
	if (copy_to_user((void __user *)addr, &tinfo.tidcnt,
			 sizeof(tinfo.tidcnt)))
		ret = -EFAULT;

	return ret;
}

static __poll_t poll_urgent(struct file *fp,
				struct poll_table_struct *pt)
{
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	__poll_t pollflag;

	poll_wait(fp, &uctxt->wait, pt);

	spin_lock_irq(&dd->uctxt_lock);
	if (uctxt->urgent != uctxt->urgent_poll) {
		pollflag = EPOLLIN | EPOLLRDNORM;
		uctxt->urgent_poll = uctxt->urgent;
	} else {
		pollflag = 0;
		set_bit(HFI1_CTXT_WAITING_URG, &uctxt->event_flags);
	}
	spin_unlock_irq(&dd->uctxt_lock);

	return pollflag;
}

static __poll_t poll_next(struct file *fp,
			      struct poll_table_struct *pt)
{
	struct hfi1_filedata *fd = fp->private_data;
	struct hfi1_ctxtdata *uctxt = fd->uctxt;
	struct hfi1_devdata *dd = uctxt->dd;
	__poll_t pollflag;

	poll_wait(fp, &uctxt->wait, pt);

	spin_lock_irq(&dd->uctxt_lock);
	if (hdrqempty(uctxt)) {
		set_bit(HFI1_CTXT_WAITING_RCV, &uctxt->event_flags);
		hfi1_rcvctrl(dd, HFI1_RCVCTRL_INTRAVAIL_ENB, uctxt);
		pollflag = 0;
	} else {
		pollflag = EPOLLIN | EPOLLRDNORM;
	}
	spin_unlock_irq(&dd->uctxt_lock);

	return pollflag;
}

/*
 * Find all user contexts in use, and set the specified bit in their
 * event mask.
 * See also find_ctxt() for a similar use, that is specific to send buffers.
 */
int hfi1_set_uevent_bits(struct hfi1_pportdata *ppd, const int evtbit)
{
	struct hfi1_ctxtdata *uctxt;
	struct hfi1_devdata *dd = ppd->dd;
	u16 ctxt;

	if (!dd->events)
		return -EINVAL;

	for (ctxt = dd->first_dyn_alloc_ctxt; ctxt < dd->num_rcv_contexts;
	     ctxt++) {
		uctxt = hfi1_rcd_get_by_index(dd, ctxt);
		if (uctxt) {
			unsigned long *evs;
			int i;
			/*
			 * subctxt_cnt is 0 if not shared, so do base
			 * separately, first, then remaining subctxt, if any
			 */
			evs = dd->events + uctxt_offset(uctxt);
			set_bit(evtbit, evs);
			for (i = 1; i < uctxt->subctxt_cnt; i++)
				set_bit(evtbit, evs + i);
			hfi1_rcd_put(uctxt);
		}
	}

	return 0;
}

/**
 * manage_rcvq - manage a context's receive queue
 * @uctxt: the context
 * @subctxt: the sub-context
 * @start_stop: action to carry out
 *
 * start_stop == 0 disables receive on the context, for use in queue
 * overflow conditions.  start_stop==1 re-enables, to be used to
 * re-init the software copy of the head register
 */
static int manage_rcvq(struct hfi1_ctxtdata *uctxt, u16 subctxt,
		       unsigned long arg)
{
	struct hfi1_devdata *dd = uctxt->dd;
	unsigned int rcvctrl_op;
	int start_stop;

	if (subctxt)
		return 0;

	if (get_user(start_stop, (int __user *)arg))
		return -EFAULT;

	/* atomically clear receive enable ctxt. */
	if (start_stop) {
		/*
		 * On enable, force in-memory copy of the tail register to
		 * 0, so that protocol code doesn't have to worry about
		 * whether or not the chip has yet updated the in-memory
		 * copy or not on return from the system call. The chip
		 * always resets it's tail register back to 0 on a
		 * transition from disabled to enabled.
		 */
		if (hfi1_rcvhdrtail_kvaddr(uctxt))
			clear_rcvhdrtail(uctxt);
		rcvctrl_op = HFI1_RCVCTRL_CTXT_ENB;
	} else {
		rcvctrl_op = HFI1_RCVCTRL_CTXT_DIS;
	}
	hfi1_rcvctrl(dd, rcvctrl_op, uctxt);
	/* always; new head should be equal to new tail; see above */

	return 0;
}

/*
 * clear the event notifier events for this context.
 * User process then performs actions appropriate to bit having been
 * set, if desired, and checks again in future.
 */
static int user_event_ack(struct hfi1_ctxtdata *uctxt, u16 subctxt,
			  unsigned long arg)
{
	int i;
	struct hfi1_devdata *dd = uctxt->dd;
	unsigned long *evs;
	unsigned long events;

	if (!dd->events)
		return 0;

	if (get_user(events, (unsigned long __user *)arg))
		return -EFAULT;

	evs = dd->events + uctxt_offset(uctxt) + subctxt;

	for (i = 0; i <= _HFI1_MAX_EVENT_BIT; i++) {
		if (!test_bit(i, &events))
			continue;
		clear_bit(i, evs);
	}
	return 0;
}

static int set_ctxt_pkey(struct hfi1_ctxtdata *uctxt, unsigned long arg)
{
	int i;
	struct hfi1_pportdata *ppd = uctxt->ppd;
	struct hfi1_devdata *dd = uctxt->dd;
	u16 pkey;

	if (!HFI1_CAP_IS_USET(PKEY_CHECK))
		return -EPERM;

	if (get_user(pkey, (u16 __user *)arg))
		return -EFAULT;

	if (pkey == LIM_MGMT_P_KEY || pkey == FULL_MGMT_P_KEY)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ppd->pkeys); i++)
		if (pkey == ppd->pkeys[i])
			return hfi1_set_ctxt_pkey(dd, uctxt, pkey);

	return -ENOENT;
}

/**
 * ctxt_reset - Reset the user context
 * @uctxt: valid user context
 */
static int ctxt_reset(struct hfi1_ctxtdata *uctxt)
{
	struct send_context *sc;
	struct hfi1_devdata *dd;
	int ret = 0;

	if (!uctxt || !uctxt->dd || !uctxt->sc)
		return -EINVAL;

	/*
	 * There is no protection here. User level has to guarantee that
	 * no one will be writing to the send context while it is being
	 * re-initialized.  If user level breaks that guarantee, it will
	 * break it's own context and no one else's.
	 */
	dd = uctxt->dd;
	sc = uctxt->sc;

	/*
	 * Wait until the interrupt handler has marked the context as
	 * halted or frozen. Report error if we time out.
	 */
	wait_event_interruptible_timeout(
		sc->halt_wait, (sc->flags & SCF_HALTED),
		msecs_to_jiffies(SEND_CTXT_HALT_TIMEOUT));
	if (!(sc->flags & SCF_HALTED))
		return -ENOLCK;

	/*
	 * If the send context was halted due to a Freeze, wait until the
	 * device has been "unfrozen" before resetting the context.
	 */
	if (sc->flags & SCF_FROZEN) {
		wait_event_interruptible_timeout(
			dd->event_queue,
			!(READ_ONCE(dd->flags) & HFI1_FROZEN),
			msecs_to_jiffies(SEND_CTXT_HALT_TIMEOUT));
		if (dd->flags & HFI1_FROZEN)
			return -ENOLCK;

		if (dd->flags & HFI1_FORCED_FREEZE)
			/*
			 * Don't allow context reset if we are into
			 * forced freeze
			 */
			return -ENODEV;

		sc_disable(sc);
		ret = sc_enable(sc);
		hfi1_rcvctrl(dd, HFI1_RCVCTRL_CTXT_ENB, uctxt);
	} else {
		ret = sc_restart(sc);
	}
	if (!ret)
		sc_return_credits(sc);

	return ret;
}

static void user_remove(struct hfi1_devdata *dd)
{

	hfi1_cdev_cleanup(&dd->user_cdev, &dd->user_device);
}

static int user_add(struct hfi1_devdata *dd)
{
	char name[10];
	int ret;

	snprintf(name, sizeof(name), "%s_%d", class_name(), dd->unit);
	ret = hfi1_cdev_init(dd->unit, name, &hfi1_file_ops,
			     &dd->user_cdev, &dd->user_device,
			     true, &dd->verbs_dev.rdi.ibdev.dev.kobj);
	if (ret)
		user_remove(dd);

	return ret;
}

/*
 * Create per-unit files in /dev
 */
int hfi1_device_create(struct hfi1_devdata *dd)
{
	return user_add(dd);
}

/*
 * Remove per-unit files in /dev
 * void, core kernel returns no errors for this stuff
 */
void hfi1_device_remove(struct hfi1_devdata *dd)
{
	user_remove(dd);
}
