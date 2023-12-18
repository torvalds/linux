/*
 *  linux/drivers/video/fb_defio.c
 *
 *  Copyright (C) 2006 Jaya Kumar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/list.h>

/* to support deferred IO */
#include <linux/rmap.h>
#include <linux/pagemap.h>

static struct page *fb_deferred_io_page(struct fb_info *info, unsigned long offs)
{
	void *screen_base = (void __force *) info->screen_base;
	struct page *page;

	if (is_vmalloc_addr(screen_base + offs))
		page = vmalloc_to_page(screen_base + offs);
	else
		page = pfn_to_page((info->fix.smem_start + offs) >> PAGE_SHIFT);

	return page;
}

static struct fb_deferred_io_pageref *fb_deferred_io_pageref_get(struct fb_info *info,
								 unsigned long offset,
								 struct page *page)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct list_head *pos = &fbdefio->pagereflist;
	unsigned long pgoff = offset >> PAGE_SHIFT;
	struct fb_deferred_io_pageref *pageref, *cur;

	if (WARN_ON_ONCE(pgoff >= info->npagerefs))
		return NULL; /* incorrect allocation size */

	/* 1:1 mapping between pageref and page offset */
	pageref = &info->pagerefs[pgoff];

	/*
	 * This check is to catch the case where a new process could start
	 * writing to the same page through a new PTE. This new access
	 * can cause a call to .page_mkwrite even if the original process'
	 * PTE is marked writable.
	 */
	if (!list_empty(&pageref->list))
		goto pageref_already_added;

	pageref->page = page;
	pageref->offset = pgoff << PAGE_SHIFT;

	if (unlikely(fbdefio->sort_pagereflist)) {
		/*
		 * We loop through the list of pagerefs before adding in
		 * order to keep the pagerefs sorted. This has significant
		 * overhead of O(n^2) with n being the number of written
		 * pages. If possible, drivers should try to work with
		 * unsorted page lists instead.
		 */
		list_for_each_entry(cur, &fbdefio->pagereflist, list) {
			if (cur->offset > pageref->offset)
				break;
		}
		pos = &cur->list;
	}

	list_add_tail(&pageref->list, pos);

pageref_already_added:
	return pageref;
}

static void fb_deferred_io_pageref_put(struct fb_deferred_io_pageref *pageref,
				       struct fb_info *info)
{
	list_del_init(&pageref->list);
}

/* this is to find and return the vmalloc-ed fb pages */
static vm_fault_t fb_deferred_io_fault(struct vm_fault *vmf)
{
	unsigned long offset;
	struct page *page;
	struct fb_info *info = vmf->vma->vm_private_data;

	offset = vmf->pgoff << PAGE_SHIFT;
	if (offset >= info->fix.smem_len)
		return VM_FAULT_SIGBUS;

	page = fb_deferred_io_page(info, offset);
	if (!page)
		return VM_FAULT_SIGBUS;

	get_page(page);

	if (vmf->vma->vm_file)
		page->mapping = vmf->vma->vm_file->f_mapping;
	else
		printk(KERN_ERR "no mapping available\n");

	BUG_ON(!page->mapping);
	page->index = vmf->pgoff; /* for page_mkclean() */

	vmf->page = page;
	return 0;
}

int fb_deferred_io_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct fb_info *info = file->private_data;
	struct inode *inode = file_inode(file);
	int err = file_write_and_wait_range(file, start, end);
	if (err)
		return err;

	/* Skip if deferred io is compiled-in but disabled on this fbdev */
	if (!info->fbdefio)
		return 0;

	inode_lock(inode);
	flush_delayed_work(&info->deferred_work);
	inode_unlock(inode);

	return 0;
}
EXPORT_SYMBOL_GPL(fb_deferred_io_fsync);

/*
 * Adds a page to the dirty list. Call this from struct
 * vm_operations_struct.page_mkwrite.
 */
static vm_fault_t fb_deferred_io_track_page(struct fb_info *info, unsigned long offset,
					    struct page *page)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct fb_deferred_io_pageref *pageref;
	vm_fault_t ret;

	/* protect against the workqueue changing the page list */
	mutex_lock(&fbdefio->lock);

	/* first write in this cycle, notify the driver */
	if (fbdefio->first_io && list_empty(&fbdefio->pagereflist))
		fbdefio->first_io(info);

	pageref = fb_deferred_io_pageref_get(info, offset, page);
	if (WARN_ON_ONCE(!pageref)) {
		ret = VM_FAULT_OOM;
		goto err_mutex_unlock;
	}

	/*
	 * We want the page to remain locked from ->page_mkwrite until
	 * the PTE is marked dirty to avoid page_mkclean() being called
	 * before the PTE is updated, which would leave the page ignored
	 * by defio.
	 * Do this by locking the page here and informing the caller
	 * about it with VM_FAULT_LOCKED.
	 */
	lock_page(pageref->page);

	mutex_unlock(&fbdefio->lock);

	/* come back after delay to process the deferred IO */
	schedule_delayed_work(&info->deferred_work, fbdefio->delay);
	return VM_FAULT_LOCKED;

err_mutex_unlock:
	mutex_unlock(&fbdefio->lock);
	return ret;
}

/*
 * fb_deferred_io_page_mkwrite - Mark a page as written for deferred I/O
 * @fb_info: The fbdev info structure
 * @vmf: The VM fault
 *
 * This is a callback we get when userspace first tries to
 * write to the page. We schedule a workqueue. That workqueue
 * will eventually mkclean the touched pages and execute the
 * deferred framebuffer IO. Then if userspace touches a page
 * again, we repeat the same scheme.
 *
 * Returns:
 * VM_FAULT_LOCKED on success, or a VM_FAULT error otherwise.
 */
static vm_fault_t fb_deferred_io_page_mkwrite(struct fb_info *info, struct vm_fault *vmf)
{
	unsigned long offset = vmf->address - vmf->vma->vm_start;
	struct page *page = vmf->page;

	file_update_time(vmf->vma->vm_file);

	return fb_deferred_io_track_page(info, offset, page);
}

/* vm_ops->page_mkwrite handler */
static vm_fault_t fb_deferred_io_mkwrite(struct vm_fault *vmf)
{
	struct fb_info *info = vmf->vma->vm_private_data;

	return fb_deferred_io_page_mkwrite(info, vmf);
}

static const struct vm_operations_struct fb_deferred_io_vm_ops = {
	.fault		= fb_deferred_io_fault,
	.page_mkwrite	= fb_deferred_io_mkwrite,
};

static const struct address_space_operations fb_deferred_io_aops = {
	.dirty_folio	= noop_dirty_folio,
};

int fb_deferred_io_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	vma->vm_ops = &fb_deferred_io_vm_ops;
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	if (!(info->flags & FBINFO_VIRTFB))
		vma->vm_flags |= VM_IO;
	vma->vm_private_data = info;
	return 0;
}
EXPORT_SYMBOL_GPL(fb_deferred_io_mmap);

/* workqueue callback */
static void fb_deferred_io_work(struct work_struct *work)
{
	struct fb_info *info = container_of(work, struct fb_info, deferred_work.work);
	struct fb_deferred_io_pageref *pageref, *next;
	struct fb_deferred_io *fbdefio = info->fbdefio;

	/* here we mkclean the pages, then do all deferred IO */
	mutex_lock(&fbdefio->lock);
	list_for_each_entry(pageref, &fbdefio->pagereflist, list) {
		struct page *cur = pageref->page;
		lock_page(cur);
		page_mkclean(cur);
		unlock_page(cur);
	}

	/* driver's callback with pagereflist */
	fbdefio->deferred_io(info, &fbdefio->pagereflist);

	/* clear the list */
	list_for_each_entry_safe(pageref, next, &fbdefio->pagereflist, list)
		fb_deferred_io_pageref_put(pageref, info);

	mutex_unlock(&fbdefio->lock);
}

int fb_deferred_io_init(struct fb_info *info)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct fb_deferred_io_pageref *pagerefs;
	unsigned long npagerefs, i;
	int ret;

	BUG_ON(!fbdefio);

	if (WARN_ON(!info->fix.smem_len))
		return -EINVAL;

	mutex_init(&fbdefio->lock);
	INIT_DELAYED_WORK(&info->deferred_work, fb_deferred_io_work);
	INIT_LIST_HEAD(&fbdefio->pagereflist);
	if (fbdefio->delay == 0) /* set a default of 1 s */
		fbdefio->delay = HZ;

	npagerefs = DIV_ROUND_UP(info->fix.smem_len, PAGE_SIZE);

	/* alloc a page ref for each page of the display memory */
	pagerefs = kvcalloc(npagerefs, sizeof(*pagerefs), GFP_KERNEL);
	if (!pagerefs) {
		ret = -ENOMEM;
		goto err;
	}
	for (i = 0; i < npagerefs; ++i)
		INIT_LIST_HEAD(&pagerefs[i].list);
	info->npagerefs = npagerefs;
	info->pagerefs = pagerefs;

	return 0;

err:
	mutex_destroy(&fbdefio->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(fb_deferred_io_init);

void fb_deferred_io_open(struct fb_info *info,
			 struct inode *inode,
			 struct file *file)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;

	file->f_mapping->a_ops = &fb_deferred_io_aops;
	fbdefio->open_count++;
}
EXPORT_SYMBOL_GPL(fb_deferred_io_open);

static void fb_deferred_io_lastclose(struct fb_info *info)
{
	struct page *page;
	int i;

	flush_delayed_work(&info->deferred_work);

	/* clear out the mapping that we setup */
	for (i = 0 ; i < info->fix.smem_len; i += PAGE_SIZE) {
		page = fb_deferred_io_page(info, i);
		page->mapping = NULL;
	}
}

void fb_deferred_io_release(struct fb_info *info)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;

	if (!--fbdefio->open_count)
		fb_deferred_io_lastclose(info);
}
EXPORT_SYMBOL_GPL(fb_deferred_io_release);

void fb_deferred_io_cleanup(struct fb_info *info)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;

	fb_deferred_io_lastclose(info);

	kvfree(info->pagerefs);
	mutex_destroy(&fbdefio->lock);
}
EXPORT_SYMBOL_GPL(fb_deferred_io_cleanup);
