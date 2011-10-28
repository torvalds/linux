/*
 * videobuf2-vmalloc.c - vmalloc memory allocator for videobuf2
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Pawel Osciak <pawel@osciak.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <media/videobuf2-core.h>
#include <media/videobuf2-memops.h>

struct vb2_vmalloc_buf {
	void				*vaddr;
	unsigned long			size;
	atomic_t			refcount;
	struct vb2_vmarea_handler	handler;
};

static void vb2_vmalloc_put(void *buf_priv);

static void *vb2_vmalloc_alloc(void *alloc_ctx, unsigned long size)
{
	struct vb2_vmalloc_buf *buf;

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->size = size;
	buf->vaddr = vmalloc_user(buf->size);
	buf->handler.refcount = &buf->refcount;
	buf->handler.put = vb2_vmalloc_put;
	buf->handler.arg = buf;

	if (!buf->vaddr) {
		printk(KERN_ERR "vmalloc of size %ld failed\n", buf->size);
		kfree(buf);
		return NULL;
	}

	atomic_inc(&buf->refcount);
	printk(KERN_DEBUG "Allocated vmalloc buffer of size %ld at vaddr=%p\n",
			buf->size, buf->vaddr);

	return buf;
}

static void vb2_vmalloc_put(void *buf_priv)
{
	struct vb2_vmalloc_buf *buf = buf_priv;

	if (atomic_dec_and_test(&buf->refcount)) {
		printk(KERN_DEBUG "%s: Freeing vmalloc mem at vaddr=%p\n",
			__func__, buf->vaddr);
		vfree(buf->vaddr);
		kfree(buf);
	}
}

static void *vb2_vmalloc_vaddr(void *buf_priv)
{
	struct vb2_vmalloc_buf *buf = buf_priv;

	BUG_ON(!buf);

	if (!buf->vaddr) {
		printk(KERN_ERR "Address of an unallocated plane requested\n");
		return NULL;
	}

	return buf->vaddr;
}

static unsigned int vb2_vmalloc_num_users(void *buf_priv)
{
	struct vb2_vmalloc_buf *buf = buf_priv;
	return atomic_read(&buf->refcount);
}

static int vb2_vmalloc_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct vb2_vmalloc_buf *buf = buf_priv;
	int ret;

	if (!buf) {
		printk(KERN_ERR "No memory to map\n");
		return -EINVAL;
	}

	ret = remap_vmalloc_range(vma, buf->vaddr, 0);
	if (ret) {
		printk(KERN_ERR "Remapping vmalloc memory, error: %d\n", ret);
		return ret;
	}

	/*
	 * Make sure that vm_areas for 2 buffers won't be merged together
	 */
	vma->vm_flags		|= VM_DONTEXPAND;

	/*
	 * Use common vm_area operations to track buffer refcount.
	 */
	vma->vm_private_data	= &buf->handler;
	vma->vm_ops		= &vb2_common_vm_ops;

	vma->vm_ops->open(vma);

	return 0;
}

const struct vb2_mem_ops vb2_vmalloc_memops = {
	.alloc		= vb2_vmalloc_alloc,
	.put		= vb2_vmalloc_put,
	.vaddr		= vb2_vmalloc_vaddr,
	.mmap		= vb2_vmalloc_mmap,
	.num_users	= vb2_vmalloc_num_users,
};
EXPORT_SYMBOL_GPL(vb2_vmalloc_memops);

MODULE_DESCRIPTION("vmalloc memory handling routines for videobuf2");
MODULE_AUTHOR("Pawel Osciak <pawel@osciak.com>");
MODULE_LICENSE("GPL");
