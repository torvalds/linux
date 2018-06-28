/******************************************************************************
 * privcmd.c
 *
 * Interface to privileged domain-0 commands.
 *
 * Copyright (c) 2002-2004, K A Fraser, B Dragovic
 */

#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/uaccess.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/miscdevice.h>
#include <linux/moduleparam.h>

#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/privcmd.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/interface/hvm/dm_op.h>
#include <xen/features.h>
#include <xen/page.h>
#include <xen/xen-ops.h>
#include <xen/balloon.h>

#include "privcmd.h"

MODULE_LICENSE("GPL");

#define PRIV_VMA_LOCKED ((void *)1)

static unsigned int privcmd_dm_op_max_num = 16;
module_param_named(dm_op_max_nr_bufs, privcmd_dm_op_max_num, uint, 0644);
MODULE_PARM_DESC(dm_op_max_nr_bufs,
		 "Maximum number of buffers per dm_op hypercall");

static unsigned int privcmd_dm_op_buf_max_size = 4096;
module_param_named(dm_op_buf_max_size, privcmd_dm_op_buf_max_size, uint,
		   0644);
MODULE_PARM_DESC(dm_op_buf_max_size,
		 "Maximum size of a dm_op hypercall buffer");

struct privcmd_data {
	domid_t domid;
};

static int privcmd_vma_range_is_mapped(
               struct vm_area_struct *vma,
               unsigned long addr,
               unsigned long nr_pages);

static long privcmd_ioctl_hypercall(struct file *file, void __user *udata)
{
	struct privcmd_data *data = file->private_data;
	struct privcmd_hypercall hypercall;
	long ret;

	/* Disallow arbitrary hypercalls if restricted */
	if (data->domid != DOMID_INVALID)
		return -EPERM;

	if (copy_from_user(&hypercall, udata, sizeof(hypercall)))
		return -EFAULT;

	xen_preemptible_hcall_begin();
	ret = privcmd_call(hypercall.op,
			   hypercall.arg[0], hypercall.arg[1],
			   hypercall.arg[2], hypercall.arg[3],
			   hypercall.arg[4]);
	xen_preemptible_hcall_end();

	return ret;
}

static void free_page_list(struct list_head *pages)
{
	struct page *p, *n;

	list_for_each_entry_safe(p, n, pages, lru)
		__free_page(p);

	INIT_LIST_HEAD(pages);
}

/*
 * Given an array of items in userspace, return a list of pages
 * containing the data.  If copying fails, either because of memory
 * allocation failure or a problem reading user memory, return an
 * error code; its up to the caller to dispose of any partial list.
 */
static int gather_array(struct list_head *pagelist,
			unsigned nelem, size_t size,
			const void __user *data)
{
	unsigned pageidx;
	void *pagedata;
	int ret;

	if (size > PAGE_SIZE)
		return 0;

	pageidx = PAGE_SIZE;
	pagedata = NULL;	/* quiet, gcc */
	while (nelem--) {
		if (pageidx > PAGE_SIZE-size) {
			struct page *page = alloc_page(GFP_KERNEL);

			ret = -ENOMEM;
			if (page == NULL)
				goto fail;

			pagedata = page_address(page);

			list_add_tail(&page->lru, pagelist);
			pageidx = 0;
		}

		ret = -EFAULT;
		if (copy_from_user(pagedata + pageidx, data, size))
			goto fail;

		data += size;
		pageidx += size;
	}

	ret = 0;

fail:
	return ret;
}

/*
 * Call function "fn" on each element of the array fragmented
 * over a list of pages.
 */
static int traverse_pages(unsigned nelem, size_t size,
			  struct list_head *pos,
			  int (*fn)(void *data, void *state),
			  void *state)
{
	void *pagedata;
	unsigned pageidx;
	int ret = 0;

	BUG_ON(size > PAGE_SIZE);

	pageidx = PAGE_SIZE;
	pagedata = NULL;	/* hush, gcc */

	while (nelem--) {
		if (pageidx > PAGE_SIZE-size) {
			struct page *page;
			pos = pos->next;
			page = list_entry(pos, struct page, lru);
			pagedata = page_address(page);
			pageidx = 0;
		}

		ret = (*fn)(pagedata + pageidx, state);
		if (ret)
			break;
		pageidx += size;
	}

	return ret;
}

/*
 * Similar to traverse_pages, but use each page as a "block" of
 * data to be processed as one unit.
 */
static int traverse_pages_block(unsigned nelem, size_t size,
				struct list_head *pos,
				int (*fn)(void *data, int nr, void *state),
				void *state)
{
	void *pagedata;
	int ret = 0;

	BUG_ON(size > PAGE_SIZE);

	while (nelem) {
		int nr = (PAGE_SIZE/size);
		struct page *page;
		if (nr > nelem)
			nr = nelem;
		pos = pos->next;
		page = list_entry(pos, struct page, lru);
		pagedata = page_address(page);
		ret = (*fn)(pagedata, nr, state);
		if (ret)
			break;
		nelem -= nr;
	}

	return ret;
}

struct mmap_gfn_state {
	unsigned long va;
	struct vm_area_struct *vma;
	domid_t domain;
};

static int mmap_gfn_range(void *data, void *state)
{
	struct privcmd_mmap_entry *msg = data;
	struct mmap_gfn_state *st = state;
	struct vm_area_struct *vma = st->vma;
	int rc;

	/* Do not allow range to wrap the address space. */
	if ((msg->npages > (LONG_MAX >> PAGE_SHIFT)) ||
	    ((unsigned long)(msg->npages << PAGE_SHIFT) >= -st->va))
		return -EINVAL;

	/* Range chunks must be contiguous in va space. */
	if ((msg->va != st->va) ||
	    ((msg->va+(msg->npages<<PAGE_SHIFT)) > vma->vm_end))
		return -EINVAL;

	rc = xen_remap_domain_gfn_range(vma,
					msg->va & PAGE_MASK,
					msg->mfn, msg->npages,
					vma->vm_page_prot,
					st->domain, NULL);
	if (rc < 0)
		return rc;

	st->va += msg->npages << PAGE_SHIFT;

	return 0;
}

static long privcmd_ioctl_mmap(struct file *file, void __user *udata)
{
	struct privcmd_data *data = file->private_data;
	struct privcmd_mmap mmapcmd;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int rc;
	LIST_HEAD(pagelist);
	struct mmap_gfn_state state;

	/* We only support privcmd_ioctl_mmap_batch for auto translated. */
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return -ENOSYS;

	if (copy_from_user(&mmapcmd, udata, sizeof(mmapcmd)))
		return -EFAULT;

	/* If restriction is in place, check the domid matches */
	if (data->domid != DOMID_INVALID && data->domid != mmapcmd.dom)
		return -EPERM;

	rc = gather_array(&pagelist,
			  mmapcmd.num, sizeof(struct privcmd_mmap_entry),
			  mmapcmd.entry);

	if (rc || list_empty(&pagelist))
		goto out;

	down_write(&mm->mmap_sem);

	{
		struct page *page = list_first_entry(&pagelist,
						     struct page, lru);
		struct privcmd_mmap_entry *msg = page_address(page);

		vma = find_vma(mm, msg->va);
		rc = -EINVAL;

		if (!vma || (msg->va != vma->vm_start) || vma->vm_private_data)
			goto out_up;
		vma->vm_private_data = PRIV_VMA_LOCKED;
	}

	state.va = vma->vm_start;
	state.vma = vma;
	state.domain = mmapcmd.dom;

	rc = traverse_pages(mmapcmd.num, sizeof(struct privcmd_mmap_entry),
			    &pagelist,
			    mmap_gfn_range, &state);


out_up:
	up_write(&mm->mmap_sem);

out:
	free_page_list(&pagelist);

	return rc;
}

struct mmap_batch_state {
	domid_t domain;
	unsigned long va;
	struct vm_area_struct *vma;
	int index;
	/* A tristate:
	 *      0 for no errors
	 *      1 if at least one error has happened (and no
	 *          -ENOENT errors have happened)
	 *      -ENOENT if at least 1 -ENOENT has happened.
	 */
	int global_error;
	int version;

	/* User-space gfn array to store errors in the second pass for V1. */
	xen_pfn_t __user *user_gfn;
	/* User-space int array to store errors in the second pass for V2. */
	int __user *user_err;
};

/* auto translated dom0 note: if domU being created is PV, then gfn is
 * mfn(addr on bus). If it's auto xlated, then gfn is pfn (input to HAP).
 */
static int mmap_batch_fn(void *data, int nr, void *state)
{
	xen_pfn_t *gfnp = data;
	struct mmap_batch_state *st = state;
	struct vm_area_struct *vma = st->vma;
	struct page **pages = vma->vm_private_data;
	struct page **cur_pages = NULL;
	int ret;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		cur_pages = &pages[st->index];

	BUG_ON(nr < 0);
	ret = xen_remap_domain_gfn_array(st->vma, st->va & PAGE_MASK, gfnp, nr,
					 (int *)gfnp, st->vma->vm_page_prot,
					 st->domain, cur_pages);

	/* Adjust the global_error? */
	if (ret != nr) {
		if (ret == -ENOENT)
			st->global_error = -ENOENT;
		else {
			/* Record that at least one error has happened. */
			if (st->global_error == 0)
				st->global_error = 1;
		}
	}
	st->va += XEN_PAGE_SIZE * nr;
	st->index += nr / XEN_PFN_PER_PAGE;

	return 0;
}

static int mmap_return_error(int err, struct mmap_batch_state *st)
{
	int ret;

	if (st->version == 1) {
		if (err) {
			xen_pfn_t gfn;

			ret = get_user(gfn, st->user_gfn);
			if (ret < 0)
				return ret;
			/*
			 * V1 encodes the error codes in the 32bit top
			 * nibble of the gfn (with its known
			 * limitations vis-a-vis 64 bit callers).
			 */
			gfn |= (err == -ENOENT) ?
				PRIVCMD_MMAPBATCH_PAGED_ERROR :
				PRIVCMD_MMAPBATCH_MFN_ERROR;
			return __put_user(gfn, st->user_gfn++);
		} else
			st->user_gfn++;
	} else { /* st->version == 2 */
		if (err)
			return __put_user(err, st->user_err++);
		else
			st->user_err++;
	}

	return 0;
}

static int mmap_return_errors(void *data, int nr, void *state)
{
	struct mmap_batch_state *st = state;
	int *errs = data;
	int i;
	int ret;

	for (i = 0; i < nr; i++) {
		ret = mmap_return_error(errs[i], st);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/* Allocate pfns that are then mapped with gfns from foreign domid. Update
 * the vma with the page info to use later.
 * Returns: 0 if success, otherwise -errno
 */
static int alloc_empty_pages(struct vm_area_struct *vma, int numpgs)
{
	int rc;
	struct page **pages;

	pages = kcalloc(numpgs, sizeof(pages[0]), GFP_KERNEL);
	if (pages == NULL)
		return -ENOMEM;

	rc = alloc_xenballooned_pages(numpgs, pages);
	if (rc != 0) {
		pr_warn("%s Could not alloc %d pfns rc:%d\n", __func__,
			numpgs, rc);
		kfree(pages);
		return -ENOMEM;
	}
	BUG_ON(vma->vm_private_data != NULL);
	vma->vm_private_data = pages;

	return 0;
}

static const struct vm_operations_struct privcmd_vm_ops;

static long privcmd_ioctl_mmap_batch(
	struct file *file, void __user *udata, int version)
{
	struct privcmd_data *data = file->private_data;
	int ret;
	struct privcmd_mmapbatch_v2 m;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long nr_pages;
	LIST_HEAD(pagelist);
	struct mmap_batch_state state;

	switch (version) {
	case 1:
		if (copy_from_user(&m, udata, sizeof(struct privcmd_mmapbatch)))
			return -EFAULT;
		/* Returns per-frame error in m.arr. */
		m.err = NULL;
		if (!access_ok(VERIFY_WRITE, m.arr, m.num * sizeof(*m.arr)))
			return -EFAULT;
		break;
	case 2:
		if (copy_from_user(&m, udata, sizeof(struct privcmd_mmapbatch_v2)))
			return -EFAULT;
		/* Returns per-frame error code in m.err. */
		if (!access_ok(VERIFY_WRITE, m.err, m.num * (sizeof(*m.err))))
			return -EFAULT;
		break;
	default:
		return -EINVAL;
	}

	/* If restriction is in place, check the domid matches */
	if (data->domid != DOMID_INVALID && data->domid != m.dom)
		return -EPERM;

	nr_pages = DIV_ROUND_UP(m.num, XEN_PFN_PER_PAGE);
	if ((m.num <= 0) || (nr_pages > (LONG_MAX >> PAGE_SHIFT)))
		return -EINVAL;

	ret = gather_array(&pagelist, m.num, sizeof(xen_pfn_t), m.arr);

	if (ret)
		goto out;
	if (list_empty(&pagelist)) {
		ret = -EINVAL;
		goto out;
	}

	if (version == 2) {
		/* Zero error array now to only copy back actual errors. */
		if (clear_user(m.err, sizeof(int) * m.num)) {
			ret = -EFAULT;
			goto out;
		}
	}

	down_write(&mm->mmap_sem);

	vma = find_vma(mm, m.addr);
	if (!vma ||
	    vma->vm_ops != &privcmd_vm_ops) {
		ret = -EINVAL;
		goto out_unlock;
	}

	/*
	 * Caller must either:
	 *
	 * Map the whole VMA range, which will also allocate all the
	 * pages required for the auto_translated_physmap case.
	 *
	 * Or
	 *
	 * Map unmapped holes left from a previous map attempt (e.g.,
	 * because those foreign frames were previously paged out).
	 */
	if (vma->vm_private_data == NULL) {
		if (m.addr != vma->vm_start ||
		    m.addr + (nr_pages << PAGE_SHIFT) != vma->vm_end) {
			ret = -EINVAL;
			goto out_unlock;
		}
		if (xen_feature(XENFEAT_auto_translated_physmap)) {
			ret = alloc_empty_pages(vma, nr_pages);
			if (ret < 0)
				goto out_unlock;
		} else
			vma->vm_private_data = PRIV_VMA_LOCKED;
	} else {
		if (m.addr < vma->vm_start ||
		    m.addr + (nr_pages << PAGE_SHIFT) > vma->vm_end) {
			ret = -EINVAL;
			goto out_unlock;
		}
		if (privcmd_vma_range_is_mapped(vma, m.addr, nr_pages)) {
			ret = -EINVAL;
			goto out_unlock;
		}
	}

	state.domain        = m.dom;
	state.vma           = vma;
	state.va            = m.addr;
	state.index         = 0;
	state.global_error  = 0;
	state.version       = version;

	BUILD_BUG_ON(((PAGE_SIZE / sizeof(xen_pfn_t)) % XEN_PFN_PER_PAGE) != 0);
	/* mmap_batch_fn guarantees ret == 0 */
	BUG_ON(traverse_pages_block(m.num, sizeof(xen_pfn_t),
				    &pagelist, mmap_batch_fn, &state));

	up_write(&mm->mmap_sem);

	if (state.global_error) {
		/* Write back errors in second pass. */
		state.user_gfn = (xen_pfn_t *)m.arr;
		state.user_err = m.err;
		ret = traverse_pages_block(m.num, sizeof(xen_pfn_t),
					   &pagelist, mmap_return_errors, &state);
	} else
		ret = 0;

	/* If we have not had any EFAULT-like global errors then set the global
	 * error to -ENOENT if necessary. */
	if ((ret == 0) && (state.global_error == -ENOENT))
		ret = -ENOENT;

out:
	free_page_list(&pagelist);
	return ret;

out_unlock:
	up_write(&mm->mmap_sem);
	goto out;
}

static int lock_pages(
	struct privcmd_dm_op_buf kbufs[], unsigned int num,
	struct page *pages[], unsigned int nr_pages)
{
	unsigned int i;

	for (i = 0; i < num; i++) {
		unsigned int requested;
		int pinned;

		requested = DIV_ROUND_UP(
			offset_in_page(kbufs[i].uptr) + kbufs[i].size,
			PAGE_SIZE);
		if (requested > nr_pages)
			return -ENOSPC;

		pinned = get_user_pages_fast(
			(unsigned long) kbufs[i].uptr,
			requested, FOLL_WRITE, pages);
		if (pinned < 0)
			return pinned;

		nr_pages -= pinned;
		pages += pinned;
	}

	return 0;
}

static void unlock_pages(struct page *pages[], unsigned int nr_pages)
{
	unsigned int i;

	if (!pages)
		return;

	for (i = 0; i < nr_pages; i++) {
		if (pages[i])
			put_page(pages[i]);
	}
}

static long privcmd_ioctl_dm_op(struct file *file, void __user *udata)
{
	struct privcmd_data *data = file->private_data;
	struct privcmd_dm_op kdata;
	struct privcmd_dm_op_buf *kbufs;
	unsigned int nr_pages = 0;
	struct page **pages = NULL;
	struct xen_dm_op_buf *xbufs = NULL;
	unsigned int i;
	long rc;

	if (copy_from_user(&kdata, udata, sizeof(kdata)))
		return -EFAULT;

	/* If restriction is in place, check the domid matches */
	if (data->domid != DOMID_INVALID && data->domid != kdata.dom)
		return -EPERM;

	if (kdata.num == 0)
		return 0;

	if (kdata.num > privcmd_dm_op_max_num)
		return -E2BIG;

	kbufs = kcalloc(kdata.num, sizeof(*kbufs), GFP_KERNEL);
	if (!kbufs)
		return -ENOMEM;

	if (copy_from_user(kbufs, kdata.ubufs,
			   sizeof(*kbufs) * kdata.num)) {
		rc = -EFAULT;
		goto out;
	}

	for (i = 0; i < kdata.num; i++) {
		if (kbufs[i].size > privcmd_dm_op_buf_max_size) {
			rc = -E2BIG;
			goto out;
		}

		if (!access_ok(VERIFY_WRITE, kbufs[i].uptr,
			       kbufs[i].size)) {
			rc = -EFAULT;
			goto out;
		}

		nr_pages += DIV_ROUND_UP(
			offset_in_page(kbufs[i].uptr) + kbufs[i].size,
			PAGE_SIZE);
	}

	pages = kcalloc(nr_pages, sizeof(*pages), GFP_KERNEL);
	if (!pages) {
		rc = -ENOMEM;
		goto out;
	}

	xbufs = kcalloc(kdata.num, sizeof(*xbufs), GFP_KERNEL);
	if (!xbufs) {
		rc = -ENOMEM;
		goto out;
	}

	rc = lock_pages(kbufs, kdata.num, pages, nr_pages);
	if (rc)
		goto out;

	for (i = 0; i < kdata.num; i++) {
		set_xen_guest_handle(xbufs[i].h, kbufs[i].uptr);
		xbufs[i].size = kbufs[i].size;
	}

	xen_preemptible_hcall_begin();
	rc = HYPERVISOR_dm_op(kdata.dom, kdata.num, xbufs);
	xen_preemptible_hcall_end();

out:
	unlock_pages(pages, nr_pages);
	kfree(xbufs);
	kfree(pages);
	kfree(kbufs);

	return rc;
}

static long privcmd_ioctl_restrict(struct file *file, void __user *udata)
{
	struct privcmd_data *data = file->private_data;
	domid_t dom;

	if (copy_from_user(&dom, udata, sizeof(dom)))
		return -EFAULT;

	/* Set restriction to the specified domain, or check it matches */
	if (data->domid == DOMID_INVALID)
		data->domid = dom;
	else if (data->domid != dom)
		return -EINVAL;

	return 0;
}

struct remap_pfn {
	struct mm_struct *mm;
	struct page **pages;
	pgprot_t prot;
	unsigned long i;
};

static int remap_pfn_fn(pte_t *ptep, pgtable_t token, unsigned long addr,
			void *data)
{
	struct remap_pfn *r = data;
	struct page *page = r->pages[r->i];
	pte_t pte = pte_mkspecial(pfn_pte(page_to_pfn(page), r->prot));

	set_pte_at(r->mm, addr, ptep, pte);
	r->i++;

	return 0;
}

static long privcmd_ioctl_mmap_resource(struct file *file, void __user *udata)
{
	struct privcmd_data *data = file->private_data;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct privcmd_mmap_resource kdata;
	xen_pfn_t *pfns = NULL;
	struct xen_mem_acquire_resource xdata;
	int rc;

	if (copy_from_user(&kdata, udata, sizeof(kdata)))
		return -EFAULT;

	/* If restriction is in place, check the domid matches */
	if (data->domid != DOMID_INVALID && data->domid != kdata.dom)
		return -EPERM;

	down_write(&mm->mmap_sem);

	vma = find_vma(mm, kdata.addr);
	if (!vma || vma->vm_ops != &privcmd_vm_ops) {
		rc = -EINVAL;
		goto out;
	}

	pfns = kcalloc(kdata.num, sizeof(*pfns), GFP_KERNEL);
	if (!pfns) {
		rc = -ENOMEM;
		goto out;
	}

	if (xen_feature(XENFEAT_auto_translated_physmap)) {
		unsigned int nr = DIV_ROUND_UP(kdata.num, XEN_PFN_PER_PAGE);
		struct page **pages;
		unsigned int i;

		rc = alloc_empty_pages(vma, nr);
		if (rc < 0)
			goto out;

		pages = vma->vm_private_data;
		for (i = 0; i < kdata.num; i++) {
			xen_pfn_t pfn =
				page_to_xen_pfn(pages[i / XEN_PFN_PER_PAGE]);

			pfns[i] = pfn + (i % XEN_PFN_PER_PAGE);
		}
	} else
		vma->vm_private_data = PRIV_VMA_LOCKED;

	memset(&xdata, 0, sizeof(xdata));
	xdata.domid = kdata.dom;
	xdata.type = kdata.type;
	xdata.id = kdata.id;
	xdata.frame = kdata.idx;
	xdata.nr_frames = kdata.num;
	set_xen_guest_handle(xdata.frame_list, pfns);

	xen_preemptible_hcall_begin();
	rc = HYPERVISOR_memory_op(XENMEM_acquire_resource, &xdata);
	xen_preemptible_hcall_end();

	if (rc)
		goto out;

	if (xen_feature(XENFEAT_auto_translated_physmap)) {
		struct remap_pfn r = {
			.mm = vma->vm_mm,
			.pages = vma->vm_private_data,
			.prot = vma->vm_page_prot,
		};

		rc = apply_to_page_range(r.mm, kdata.addr,
					 kdata.num << PAGE_SHIFT,
					 remap_pfn_fn, &r);
	} else {
		unsigned int domid =
			(xdata.flags & XENMEM_rsrc_acq_caller_owned) ?
			DOMID_SELF : kdata.dom;
		int num;

		num = xen_remap_domain_mfn_array(vma,
						 kdata.addr & PAGE_MASK,
						 pfns, kdata.num, (int *)pfns,
						 vma->vm_page_prot,
						 domid,
						 vma->vm_private_data);
		if (num < 0)
			rc = num;
		else if (num != kdata.num) {
			unsigned int i;

			for (i = 0; i < num; i++) {
				rc = pfns[i];
				if (rc < 0)
					break;
			}
		} else
			rc = 0;
	}

out:
	up_write(&mm->mmap_sem);
	kfree(pfns);

	return rc;
}

static long privcmd_ioctl(struct file *file,
			  unsigned int cmd, unsigned long data)
{
	int ret = -ENOTTY;
	void __user *udata = (void __user *) data;

	switch (cmd) {
	case IOCTL_PRIVCMD_HYPERCALL:
		ret = privcmd_ioctl_hypercall(file, udata);
		break;

	case IOCTL_PRIVCMD_MMAP:
		ret = privcmd_ioctl_mmap(file, udata);
		break;

	case IOCTL_PRIVCMD_MMAPBATCH:
		ret = privcmd_ioctl_mmap_batch(file, udata, 1);
		break;

	case IOCTL_PRIVCMD_MMAPBATCH_V2:
		ret = privcmd_ioctl_mmap_batch(file, udata, 2);
		break;

	case IOCTL_PRIVCMD_DM_OP:
		ret = privcmd_ioctl_dm_op(file, udata);
		break;

	case IOCTL_PRIVCMD_RESTRICT:
		ret = privcmd_ioctl_restrict(file, udata);
		break;

	case IOCTL_PRIVCMD_MMAP_RESOURCE:
		ret = privcmd_ioctl_mmap_resource(file, udata);
		break;

	default:
		break;
	}

	return ret;
}

static int privcmd_open(struct inode *ino, struct file *file)
{
	struct privcmd_data *data = kzalloc(sizeof(*data), GFP_KERNEL);

	if (!data)
		return -ENOMEM;

	/* DOMID_INVALID implies no restriction */
	data->domid = DOMID_INVALID;

	file->private_data = data;
	return 0;
}

static int privcmd_release(struct inode *ino, struct file *file)
{
	struct privcmd_data *data = file->private_data;

	kfree(data);
	return 0;
}

static void privcmd_close(struct vm_area_struct *vma)
{
	struct page **pages = vma->vm_private_data;
	int numpgs = vma_pages(vma);
	int numgfns = (vma->vm_end - vma->vm_start) >> XEN_PAGE_SHIFT;
	int rc;

	if (!xen_feature(XENFEAT_auto_translated_physmap) || !numpgs || !pages)
		return;

	rc = xen_unmap_domain_gfn_range(vma, numgfns, pages);
	if (rc == 0)
		free_xenballooned_pages(numpgs, pages);
	else
		pr_crit("unable to unmap MFN range: leaking %d pages. rc=%d\n",
			numpgs, rc);
	kfree(pages);
}

static vm_fault_t privcmd_fault(struct vm_fault *vmf)
{
	printk(KERN_DEBUG "privcmd_fault: vma=%p %lx-%lx, pgoff=%lx, uv=%p\n",
	       vmf->vma, vmf->vma->vm_start, vmf->vma->vm_end,
	       vmf->pgoff, (void *)vmf->address);

	return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct privcmd_vm_ops = {
	.close = privcmd_close,
	.fault = privcmd_fault
};

static int privcmd_mmap(struct file *file, struct vm_area_struct *vma)
{
	/* DONTCOPY is essential for Xen because copy_page_range doesn't know
	 * how to recreate these mappings */
	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTCOPY |
			 VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_ops = &privcmd_vm_ops;
	vma->vm_private_data = NULL;

	return 0;
}

/*
 * For MMAPBATCH*. This allows asserting the singleshot mapping
 * on a per pfn/pte basis. Mapping calls that fail with ENOENT
 * can be then retried until success.
 */
static int is_mapped_fn(pte_t *pte, struct page *pmd_page,
	                unsigned long addr, void *data)
{
	return pte_none(*pte) ? 0 : -EBUSY;
}

static int privcmd_vma_range_is_mapped(
	           struct vm_area_struct *vma,
	           unsigned long addr,
	           unsigned long nr_pages)
{
	return apply_to_page_range(vma->vm_mm, addr, nr_pages << PAGE_SHIFT,
				   is_mapped_fn, NULL) != 0;
}

const struct file_operations xen_privcmd_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = privcmd_ioctl,
	.open = privcmd_open,
	.release = privcmd_release,
	.mmap = privcmd_mmap,
};
EXPORT_SYMBOL_GPL(xen_privcmd_fops);

static struct miscdevice privcmd_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xen/privcmd",
	.fops = &xen_privcmd_fops,
};

static int __init privcmd_init(void)
{
	int err;

	if (!xen_domain())
		return -ENODEV;

	err = misc_register(&privcmd_dev);
	if (err != 0) {
		pr_err("Could not register Xen privcmd device\n");
		return err;
	}

	err = misc_register(&xen_privcmdbuf_dev);
	if (err != 0) {
		pr_err("Could not register Xen hypercall-buf device\n");
		misc_deregister(&privcmd_dev);
		return err;
	}

	return 0;
}

static void __exit privcmd_exit(void)
{
	misc_deregister(&privcmd_dev);
	misc_deregister(&xen_privcmdbuf_dev);
}

module_init(privcmd_init);
module_exit(privcmd_exit);
