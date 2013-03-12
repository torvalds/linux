/*
 * Copyright (C) 2001-2006 Silicon Graphics, Inc.  All rights
 * reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

/*
 * SN Platform Special Memory (mspec) Support
 *
 * This driver exports the SN special memory (mspec) facility to user
 * processes.
 * There are three types of memory made available thru this driver:
 * fetchops, uncached and cached.
 *
 * Fetchops are atomic memory operations that are implemented in the
 * memory controller on SGI SN hardware.
 *
 * Uncached are used for memory write combining feature of the ia64
 * cpu.
 *
 * Cached are used for areas of memory that are used as cached addresses
 * on our partition and used as uncached addresses from other partitions.
 * Due to a design constraint of the SN2 Shub, you can not have processors
 * on the same FSB perform both a cached and uncached reference to the
 * same cache line.  These special memory cached regions prevent the
 * kernel from ever dropping in a TLB entry and therefore prevent the
 * processor from ever speculating a cache line from this page.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/numa.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <linux/atomic.h>
#include <asm/tlbflush.h>
#include <asm/uncached.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/mspec.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/io.h>
#include <asm/sn/bte.h>
#include <asm/sn/shubio.h>


#define FETCHOP_ID	"SGI Fetchop,"
#define CACHED_ID	"Cached,"
#define UNCACHED_ID	"Uncached"
#define REVISION	"4.0"
#define MSPEC_BASENAME	"mspec"

/*
 * Page types allocated by the device.
 */
enum mspec_page_type {
	MSPEC_FETCHOP = 1,
	MSPEC_CACHED,
	MSPEC_UNCACHED
};

#ifdef CONFIG_SGI_SN
static int is_sn2;
#else
#define is_sn2		0
#endif

/*
 * One of these structures is allocated when an mspec region is mmaped. The
 * structure is pointed to by the vma->vm_private_data field in the vma struct.
 * This structure is used to record the addresses of the mspec pages.
 * This structure is shared by all vma's that are split off from the
 * original vma when split_vma()'s are done.
 *
 * The refcnt is incremented atomically because mm->mmap_sem does not
 * protect in fork case where multiple tasks share the vma_data.
 */
struct vma_data {
	atomic_t refcnt;	/* Number of vmas sharing the data. */
	spinlock_t lock;	/* Serialize access to this structure. */
	int count;		/* Number of pages allocated. */
	enum mspec_page_type type; /* Type of pages allocated. */
	int flags;		/* See VMD_xxx below. */
	unsigned long vm_start;	/* Original (unsplit) base. */
	unsigned long vm_end;	/* Original (unsplit) end. */
	unsigned long maddr[0];	/* Array of MSPEC addresses. */
};

#define VMD_VMALLOCED 0x1	/* vmalloc'd rather than kmalloc'd */

/* used on shub2 to clear FOP cache in the HUB */
static unsigned long scratch_page[MAX_NUMNODES];
#define SH2_AMO_CACHE_ENTRIES	4

static inline int
mspec_zero_block(unsigned long addr, int len)
{
	int status;

	if (is_sn2) {
		if (is_shub2()) {
			int nid;
			void *p;
			int i;

			nid = nasid_to_cnodeid(get_node_number(__pa(addr)));
			p = (void *)TO_AMO(scratch_page[nid]);

			for (i=0; i < SH2_AMO_CACHE_ENTRIES; i++) {
				FETCHOP_LOAD_OP(p, FETCHOP_LOAD);
				p += FETCHOP_VAR_SIZE;
			}
		}

		status = bte_copy(0, addr & ~__IA64_UNCACHED_OFFSET, len,
				  BTE_WACQUIRE | BTE_ZERO_FILL, NULL);
	} else {
		memset((char *) addr, 0, len);
		status = 0;
	}
	return status;
}

/*
 * mspec_open
 *
 * Called when a device mapping is created by a means other than mmap
 * (via fork, munmap, etc.).  Increments the reference count on the
 * underlying mspec data so it is not freed prematurely.
 */
static void
mspec_open(struct vm_area_struct *vma)
{
	struct vma_data *vdata;

	vdata = vma->vm_private_data;
	atomic_inc(&vdata->refcnt);
}

/*
 * mspec_close
 *
 * Called when unmapping a device mapping. Frees all mspec pages
 * belonging to all the vma's sharing this vma_data structure.
 */
static void
mspec_close(struct vm_area_struct *vma)
{
	struct vma_data *vdata;
	int index, last_index;
	unsigned long my_page;

	vdata = vma->vm_private_data;

	if (!atomic_dec_and_test(&vdata->refcnt))
		return;

	last_index = (vdata->vm_end - vdata->vm_start) >> PAGE_SHIFT;
	for (index = 0; index < last_index; index++) {
		if (vdata->maddr[index] == 0)
			continue;
		/*
		 * Clear the page before sticking it back
		 * into the pool.
		 */
		my_page = vdata->maddr[index];
		vdata->maddr[index] = 0;
		if (!mspec_zero_block(my_page, PAGE_SIZE))
			uncached_free_page(my_page, 1);
		else
			printk(KERN_WARNING "mspec_close(): "
			       "failed to zero page %ld\n", my_page);
	}

	if (vdata->flags & VMD_VMALLOCED)
		vfree(vdata);
	else
		kfree(vdata);
}

/*
 * mspec_fault
 *
 * Creates a mspec page and maps it to user space.
 */
static int
mspec_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	unsigned long paddr, maddr;
	unsigned long pfn;
	pgoff_t index = vmf->pgoff;
	struct vma_data *vdata = vma->vm_private_data;

	maddr = (volatile unsigned long) vdata->maddr[index];
	if (maddr == 0) {
		maddr = uncached_alloc_page(numa_node_id(), 1);
		if (maddr == 0)
			return VM_FAULT_OOM;

		spin_lock(&vdata->lock);
		if (vdata->maddr[index] == 0) {
			vdata->count++;
			vdata->maddr[index] = maddr;
		} else {
			uncached_free_page(maddr, 1);
			maddr = vdata->maddr[index];
		}
		spin_unlock(&vdata->lock);
	}

	if (vdata->type == MSPEC_FETCHOP)
		paddr = TO_AMO(maddr);
	else
		paddr = maddr & ~__IA64_UNCACHED_OFFSET;

	pfn = paddr >> PAGE_SHIFT;

	/*
	 * vm_insert_pfn can fail with -EBUSY, but in that case it will
	 * be because another thread has installed the pte first, so it
	 * is no problem.
	 */
	vm_insert_pfn(vma, (unsigned long)vmf->virtual_address, pfn);

	return VM_FAULT_NOPAGE;
}

static const struct vm_operations_struct mspec_vm_ops = {
	.open = mspec_open,
	.close = mspec_close,
	.fault = mspec_fault,
};

/*
 * mspec_mmap
 *
 * Called when mmapping the device.  Initializes the vma with a fault handler
 * and private data structure necessary to allocate, track, and free the
 * underlying pages.
 */
static int
mspec_mmap(struct file *file, struct vm_area_struct *vma,
					enum mspec_page_type type)
{
	struct vma_data *vdata;
	int pages, vdata_size, flags = 0;

	if (vma->vm_pgoff != 0)
		return -EINVAL;

	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;

	if ((vma->vm_flags & VM_WRITE) == 0)
		return -EPERM;

	pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	vdata_size = sizeof(struct vma_data) + pages * sizeof(long);
	if (vdata_size <= PAGE_SIZE)
		vdata = kzalloc(vdata_size, GFP_KERNEL);
	else {
		vdata = vzalloc(vdata_size);
		flags = VMD_VMALLOCED;
	}
	if (!vdata)
		return -ENOMEM;

	vdata->vm_start = vma->vm_start;
	vdata->vm_end = vma->vm_end;
	vdata->flags = flags;
	vdata->type = type;
	spin_lock_init(&vdata->lock);
	atomic_set(&vdata->refcnt, 1);
	vma->vm_private_data = vdata;

	vma->vm_flags |= (VM_IO | VM_RESERVED | VM_PFNMAP | VM_DONTEXPAND);
	if (vdata->type == MSPEC_FETCHOP || vdata->type == MSPEC_UNCACHED)
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_ops = &mspec_vm_ops;

	return 0;
}

static int
fetchop_mmap(struct file *file, struct vm_area_struct *vma)
{
	return mspec_mmap(file, vma, MSPEC_FETCHOP);
}

static int
cached_mmap(struct file *file, struct vm_area_struct *vma)
{
	return mspec_mmap(file, vma, MSPEC_CACHED);
}

static int
uncached_mmap(struct file *file, struct vm_area_struct *vma)
{
	return mspec_mmap(file, vma, MSPEC_UNCACHED);
}

static const struct file_operations fetchop_fops = {
	.owner = THIS_MODULE,
	.mmap = fetchop_mmap,
	.llseek = noop_llseek,
};

static struct miscdevice fetchop_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sgi_fetchop",
	.fops = &fetchop_fops
};

static const struct file_operations cached_fops = {
	.owner = THIS_MODULE,
	.mmap = cached_mmap,
	.llseek = noop_llseek,
};

static struct miscdevice cached_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mspec_cached",
	.fops = &cached_fops
};

static const struct file_operations uncached_fops = {
	.owner = THIS_MODULE,
	.mmap = uncached_mmap,
	.llseek = noop_llseek,
};

static struct miscdevice uncached_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mspec_uncached",
	.fops = &uncached_fops
};

/*
 * mspec_init
 *
 * Called at boot time to initialize the mspec facility.
 */
static int __init
mspec_init(void)
{
	int ret;
	int nid;

	/*
	 * The fetchop device only works on SN2 hardware, uncached and cached
	 * memory drivers should both be valid on all ia64 hardware
	 */
#ifdef CONFIG_SGI_SN
	if (ia64_platform_is("sn2")) {
		is_sn2 = 1;
		if (is_shub2()) {
			ret = -ENOMEM;
			for_each_node_state(nid, N_ONLINE) {
				int actual_nid;
				int nasid;
				unsigned long phys;

				scratch_page[nid] = uncached_alloc_page(nid, 1);
				if (scratch_page[nid] == 0)
					goto free_scratch_pages;
				phys = __pa(scratch_page[nid]);
				nasid = get_node_number(phys);
				actual_nid = nasid_to_cnodeid(nasid);
				if (actual_nid != nid)
					goto free_scratch_pages;
			}
		}

		ret = misc_register(&fetchop_miscdev);
		if (ret) {
			printk(KERN_ERR
			       "%s: failed to register device %i\n",
			       FETCHOP_ID, ret);
			goto free_scratch_pages;
		}
	}
#endif
	ret = misc_register(&cached_miscdev);
	if (ret) {
		printk(KERN_ERR "%s: failed to register device %i\n",
		       CACHED_ID, ret);
		if (is_sn2)
			misc_deregister(&fetchop_miscdev);
		goto free_scratch_pages;
	}
	ret = misc_register(&uncached_miscdev);
	if (ret) {
		printk(KERN_ERR "%s: failed to register device %i\n",
		       UNCACHED_ID, ret);
		misc_deregister(&cached_miscdev);
		if (is_sn2)
			misc_deregister(&fetchop_miscdev);
		goto free_scratch_pages;
	}

	printk(KERN_INFO "%s %s initialized devices: %s %s %s\n",
	       MSPEC_BASENAME, REVISION, is_sn2 ? FETCHOP_ID : "",
	       CACHED_ID, UNCACHED_ID);

	return 0;

 free_scratch_pages:
	for_each_node(nid) {
		if (scratch_page[nid] != 0)
			uncached_free_page(scratch_page[nid], 1);
	}
	return ret;
}

static void __exit
mspec_exit(void)
{
	int nid;

	misc_deregister(&uncached_miscdev);
	misc_deregister(&cached_miscdev);
	if (is_sn2) {
		misc_deregister(&fetchop_miscdev);

		for_each_node(nid) {
			if (scratch_page[nid] != 0)
				uncached_free_page(scratch_page[nid], 1);
		}
	}
}

module_init(mspec_init);
module_exit(mspec_exit);

MODULE_AUTHOR("Silicon Graphics, Inc. <linux-altix@sgi.com>");
MODULE_DESCRIPTION("Driver for SGI SN special memory operations");
MODULE_LICENSE("GPL");
