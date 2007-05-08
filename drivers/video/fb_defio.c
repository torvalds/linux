/*
 *  linux/drivers/video/fb_defio.c
 *
 *  Copyright (C) 2006 Jaya Kumar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/list.h>
#include <asm/uaccess.h>

/* to support deferred IO */
#include <linux/rmap.h>
#include <linux/pagemap.h>

/* this is to find and return the vmalloc-ed fb pages */
static struct page* fb_deferred_io_nopage(struct vm_area_struct *vma,
					unsigned long vaddr, int *type)
{
	unsigned long offset;
	struct page *page;
	struct fb_info *info = vma->vm_private_data;

	offset = (vaddr - vma->vm_start) + (vma->vm_pgoff << PAGE_SHIFT);
	if (offset >= info->fix.smem_len)
		return NOPAGE_SIGBUS;

	page = vmalloc_to_page(info->screen_base + offset);
	if (!page)
		return NOPAGE_OOM;

	get_page(page);
	if (type)
		*type = VM_FAULT_MINOR;
	return page;
}

int fb_deferred_io_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	struct fb_info *info = file->private_data;

	/* Kill off the delayed work */
	cancel_rearming_delayed_work(&info->deferred_work);

	/* Run it immediately */
	return schedule_delayed_work(&info->deferred_work, 0);
}
EXPORT_SYMBOL_GPL(fb_deferred_io_fsync);

/* vm_ops->page_mkwrite handler */
static int fb_deferred_io_mkwrite(struct vm_area_struct *vma,
				  struct page *page)
{
	struct fb_info *info = vma->vm_private_data;
	struct fb_deferred_io *fbdefio = info->fbdefio;

	/* this is a callback we get when userspace first tries to
	write to the page. we schedule a workqueue. that workqueue
	will eventually mkclean the touched pages and execute the
	deferred framebuffer IO. then if userspace touches a page
	again, we repeat the same scheme */

	/* protect against the workqueue changing the page list */
	mutex_lock(&fbdefio->lock);
	list_add(&page->lru, &fbdefio->pagelist);
	mutex_unlock(&fbdefio->lock);

	/* come back after delay to process the deferred IO */
	schedule_delayed_work(&info->deferred_work, fbdefio->delay);
	return 0;
}

static struct vm_operations_struct fb_deferred_io_vm_ops = {
	.nopage   	= fb_deferred_io_nopage,
	.page_mkwrite	= fb_deferred_io_mkwrite,
};

static int fb_deferred_io_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	vma->vm_ops = &fb_deferred_io_vm_ops;
	vma->vm_flags |= ( VM_IO | VM_RESERVED | VM_DONTEXPAND );
	vma->vm_private_data = info;
	return 0;
}

/* workqueue callback */
static void fb_deferred_io_work(struct work_struct *work)
{
	struct fb_info *info = container_of(work, struct fb_info,
						deferred_work.work);
	struct list_head *node, *next;
	struct page *cur;
	struct fb_deferred_io *fbdefio = info->fbdefio;

	/* here we mkclean the pages, then do all deferred IO */
	mutex_lock(&fbdefio->lock);
	list_for_each_entry(cur, &fbdefio->pagelist, lru) {
		lock_page(cur);
		page_mkclean(cur);
		unlock_page(cur);
	}

	/* driver's callback with pagelist */
	fbdefio->deferred_io(info, &fbdefio->pagelist);

	/* clear the list */
	list_for_each_safe(node, next, &fbdefio->pagelist) {
		list_del(node);
	}
	mutex_unlock(&fbdefio->lock);
}

void fb_deferred_io_init(struct fb_info *info)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;

	BUG_ON(!fbdefio);
	mutex_init(&fbdefio->lock);
	info->fbops->fb_mmap = fb_deferred_io_mmap;
	INIT_DELAYED_WORK(&info->deferred_work, fb_deferred_io_work);
	INIT_LIST_HEAD(&fbdefio->pagelist);
	if (fbdefio->delay == 0) /* set a default of 1 s */
		fbdefio->delay = HZ;
}
EXPORT_SYMBOL_GPL(fb_deferred_io_init);

void fb_deferred_io_cleanup(struct fb_info *info)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;

	BUG_ON(!fbdefio);
	cancel_delayed_work(&info->deferred_work);
	flush_scheduled_work();
}
EXPORT_SYMBOL_GPL(fb_deferred_io_cleanup);

MODULE_LICENSE("GPL");
