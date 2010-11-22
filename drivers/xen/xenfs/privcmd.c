/******************************************************************************
 * privcmd.c
 *
 * Interface to privileged domain-0 commands.
 *
 * Copyright (c) 2002-2004, K A Fraser, B Dragovic
 */

#include <linux/kernel.h>
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

#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/privcmd.h>
#include <xen/interface/xen.h>
#include <xen/features.h>
#include <xen/page.h>
#include <xen/xen-ops.h>

#ifndef HAVE_ARCH_PRIVCMD_MMAP
static int privcmd_enforce_singleshot_mapping(struct vm_area_struct *vma);
#endif

static long privcmd_ioctl_hypercall(void __user *udata)
{
	struct privcmd_hypercall hypercall;
	long ret;

	if (copy_from_user(&hypercall, udata, sizeof(hypercall)))
		return -EFAULT;

	ret = privcmd_call(hypercall.op,
			   hypercall.arg[0], hypercall.arg[1],
			   hypercall.arg[2], hypercall.arg[3],
			   hypercall.arg[4]);

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
			void __user *data)
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

struct mmap_mfn_state {
	unsigned long va;
	struct vm_area_struct *vma;
	domid_t domain;
};

static int mmap_mfn_range(void *data, void *state)
{
	struct privcmd_mmap_entry *msg = data;
	struct mmap_mfn_state *st = state;
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

	rc = xen_remap_domain_mfn_range(vma,
					msg->va & PAGE_MASK,
					msg->mfn, msg->npages,
					vma->vm_page_prot,
					st->domain);
	if (rc < 0)
		return rc;

	st->va += msg->npages << PAGE_SHIFT;

	return 0;
}

static long privcmd_ioctl_mmap(void __user *udata)
{
	struct privcmd_mmap mmapcmd;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int rc;
	LIST_HEAD(pagelist);
	struct mmap_mfn_state state;

	if (!xen_initial_domain())
		return -EPERM;

	if (copy_from_user(&mmapcmd, udata, sizeof(mmapcmd)))
		return -EFAULT;

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

		if (!vma || (msg->va != vma->vm_start) ||
		    !privcmd_enforce_singleshot_mapping(vma))
			goto out_up;
	}

	state.va = vma->vm_start;
	state.vma = vma;
	state.domain = mmapcmd.dom;

	rc = traverse_pages(mmapcmd.num, sizeof(struct privcmd_mmap_entry),
			    &pagelist,
			    mmap_mfn_range, &state);


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
	int err;

	xen_pfn_t __user *user;
};

static int mmap_batch_fn(void *data, void *state)
{
	xen_pfn_t *mfnp = data;
	struct mmap_batch_state *st = state;

	if (xen_remap_domain_mfn_range(st->vma, st->va & PAGE_MASK, *mfnp, 1,
				       st->vma->vm_page_prot, st->domain) < 0) {
		*mfnp |= 0xf0000000U;
		st->err++;
	}
	st->va += PAGE_SIZE;

	return 0;
}

static int mmap_return_errors(void *data, void *state)
{
	xen_pfn_t *mfnp = data;
	struct mmap_batch_state *st = state;

	put_user(*mfnp, st->user++);

	return 0;
}

static struct vm_operations_struct privcmd_vm_ops;

static long privcmd_ioctl_mmap_batch(void __user *udata)
{
	int ret;
	struct privcmd_mmapbatch m;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long nr_pages;
	LIST_HEAD(pagelist);
	struct mmap_batch_state state;

	if (!xen_initial_domain())
		return -EPERM;

	if (copy_from_user(&m, udata, sizeof(m)))
		return -EFAULT;

	nr_pages = m.num;
	if ((m.num <= 0) || (nr_pages > (LONG_MAX >> PAGE_SHIFT)))
		return -EINVAL;

	ret = gather_array(&pagelist, m.num, sizeof(xen_pfn_t),
			   m.arr);

	if (ret || list_empty(&pagelist))
		goto out;

	down_write(&mm->mmap_sem);

	vma = find_vma(mm, m.addr);
	ret = -EINVAL;
	if (!vma ||
	    vma->vm_ops != &privcmd_vm_ops ||
	    (m.addr != vma->vm_start) ||
	    ((m.addr + (nr_pages << PAGE_SHIFT)) != vma->vm_end) ||
	    !privcmd_enforce_singleshot_mapping(vma)) {
		up_write(&mm->mmap_sem);
		goto out;
	}

	state.domain = m.dom;
	state.vma = vma;
	state.va = m.addr;
	state.err = 0;

	ret = traverse_pages(m.num, sizeof(xen_pfn_t),
			     &pagelist, mmap_batch_fn, &state);

	up_write(&mm->mmap_sem);

	if (state.err > 0) {
		ret = 0;

		state.user = m.arr;
		traverse_pages(m.num, sizeof(xen_pfn_t),
			       &pagelist,
			       mmap_return_errors, &state);
	}

out:
	free_page_list(&pagelist);

	return ret;
}

static long privcmd_ioctl(struct file *file,
			  unsigned int cmd, unsigned long data)
{
	int ret = -ENOSYS;
	void __user *udata = (void __user *) data;

	switch (cmd) {
	case IOCTL_PRIVCMD_HYPERCALL:
		ret = privcmd_ioctl_hypercall(udata);
		break;

	case IOCTL_PRIVCMD_MMAP:
		ret = privcmd_ioctl_mmap(udata);
		break;

	case IOCTL_PRIVCMD_MMAPBATCH:
		ret = privcmd_ioctl_mmap_batch(udata);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifndef HAVE_ARCH_PRIVCMD_MMAP
static int privcmd_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	printk(KERN_DEBUG "privcmd_fault: vma=%p %lx-%lx, pgoff=%lx, uv=%p\n",
	       vma, vma->vm_start, vma->vm_end,
	       vmf->pgoff, vmf->virtual_address);

	return VM_FAULT_SIGBUS;
}

static struct vm_operations_struct privcmd_vm_ops = {
	.fault = privcmd_fault
};

static int privcmd_mmap(struct file *file, struct vm_area_struct *vma)
{
	/* Unsupported for auto-translate guests. */
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return -ENOSYS;

	/* DONTCOPY is essential for Xen as copy_page_range is broken. */
	vma->vm_flags |= VM_RESERVED | VM_IO | VM_DONTCOPY;
	vma->vm_ops = &privcmd_vm_ops;
	vma->vm_private_data = NULL;

	return 0;
}

static int privcmd_enforce_singleshot_mapping(struct vm_area_struct *vma)
{
	return (xchg(&vma->vm_private_data, (void *)1) == NULL);
}
#endif

const struct file_operations privcmd_file_ops = {
	.unlocked_ioctl = privcmd_ioctl,
	.mmap = privcmd_mmap,
};
