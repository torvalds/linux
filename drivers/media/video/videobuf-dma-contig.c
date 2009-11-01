/*
 * helper functions for physically contiguous capture buffers
 *
 * The functions support hardware lacking scatter gather support
 * (i.e. the buffers must be linear in physical memory)
 *
 * Copyright (c) 2008 Magnus Damm
 *
 * Based on videobuf-vmalloc.c,
 * (c) 2007 Mauro Carvalho Chehab, <mchehab@infradead.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/dma-mapping.h>
#include <media/videobuf-dma-contig.h>

struct videobuf_dma_contig_memory {
	u32 magic;
	void *vaddr;
	dma_addr_t dma_handle;
	unsigned long size;
	int is_userptr;
};

#define MAGIC_DC_MEM 0x0733ac61
#define MAGIC_CHECK(is, should)						    \
	if (unlikely((is) != (should)))	{				    \
		pr_err("magic mismatch: %x expected %x\n", (is), (should)); \
		BUG();							    \
	}

static void
videobuf_vm_open(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;

	dev_dbg(map->q->dev, "vm_open %p [count=%u,vma=%08lx-%08lx]\n",
		map, map->count, vma->vm_start, vma->vm_end);

	map->count++;
}

static void videobuf_vm_close(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;
	struct videobuf_queue *q = map->q;
	int i;

	dev_dbg(map->q->dev, "vm_close %p [count=%u,vma=%08lx-%08lx]\n",
		map, map->count, vma->vm_start, vma->vm_end);

	map->count--;
	if (0 == map->count) {
		struct videobuf_dma_contig_memory *mem;

		dev_dbg(map->q->dev, "munmap %p q=%p\n", map, q);
		mutex_lock(&q->vb_lock);

		/* We need first to cancel streams, before unmapping */
		if (q->streaming)
			videobuf_queue_cancel(q);

		for (i = 0; i < VIDEO_MAX_FRAME; i++) {
			if (NULL == q->bufs[i])
				continue;

			if (q->bufs[i]->map != map)
				continue;

			mem = q->bufs[i]->priv;
			if (mem) {
				/* This callback is called only if kernel has
				   allocated memory and this memory is mmapped.
				   In this case, memory should be freed,
				   in order to do memory unmap.
				 */

				MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

				/* vfree is not atomic - can't be
				   called with IRQ's disabled
				 */
				dev_dbg(map->q->dev, "buf[%d] freeing %p\n",
					i, mem->vaddr);

				dma_free_coherent(q->dev, mem->size,
						  mem->vaddr, mem->dma_handle);
				mem->vaddr = NULL;
			}

			q->bufs[i]->map   = NULL;
			q->bufs[i]->baddr = 0;
		}

		kfree(map);

		mutex_unlock(&q->vb_lock);
	}
}

static const struct vm_operations_struct videobuf_vm_ops = {
	.open     = videobuf_vm_open,
	.close    = videobuf_vm_close,
};

/**
 * videobuf_dma_contig_user_put() - reset pointer to user space buffer
 * @mem: per-buffer private videobuf-dma-contig data
 *
 * This function resets the user space pointer
 */
static void videobuf_dma_contig_user_put(struct videobuf_dma_contig_memory *mem)
{
	mem->is_userptr = 0;
	mem->dma_handle = 0;
	mem->size = 0;
}

/**
 * videobuf_dma_contig_user_get() - setup user space memory pointer
 * @mem: per-buffer private videobuf-dma-contig data
 * @vb: video buffer to map
 *
 * This function validates and sets up a pointer to user space memory.
 * Only physically contiguous pfn-mapped memory is accepted.
 *
 * Returns 0 if successful.
 */
static int videobuf_dma_contig_user_get(struct videobuf_dma_contig_memory *mem,
					struct videobuf_buffer *vb)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long prev_pfn, this_pfn;
	unsigned long pages_done, user_address;
	int ret;

	mem->size = PAGE_ALIGN(vb->size);
	mem->is_userptr = 0;
	ret = -EINVAL;

	down_read(&mm->mmap_sem);

	vma = find_vma(mm, vb->baddr);
	if (!vma)
		goto out_up;

	if ((vb->baddr + mem->size) > vma->vm_end)
		goto out_up;

	pages_done = 0;
	prev_pfn = 0; /* kill warning */
	user_address = vb->baddr;

	while (pages_done < (mem->size >> PAGE_SHIFT)) {
		ret = follow_pfn(vma, user_address, &this_pfn);
		if (ret)
			break;

		if (pages_done == 0)
			mem->dma_handle = this_pfn << PAGE_SHIFT;
		else if (this_pfn != (prev_pfn + 1))
			ret = -EFAULT;

		if (ret)
			break;

		prev_pfn = this_pfn;
		user_address += PAGE_SIZE;
		pages_done++;
	}

	if (!ret)
		mem->is_userptr = 1;

 out_up:
	up_read(&current->mm->mmap_sem);

	return ret;
}

static void *__videobuf_alloc(size_t size)
{
	struct videobuf_dma_contig_memory *mem;
	struct videobuf_buffer *vb;

	vb = kzalloc(size + sizeof(*mem), GFP_KERNEL);
	if (vb) {
		mem = vb->priv = ((char *)vb) + size;
		mem->magic = MAGIC_DC_MEM;
	}

	return vb;
}

static void *__videobuf_to_vmalloc(struct videobuf_buffer *buf)
{
	struct videobuf_dma_contig_memory *mem = buf->priv;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

	return mem->vaddr;
}

static int __videobuf_iolock(struct videobuf_queue *q,
			     struct videobuf_buffer *vb,
			     struct v4l2_framebuffer *fbuf)
{
	struct videobuf_dma_contig_memory *mem = vb->priv;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
		dev_dbg(q->dev, "%s memory method MMAP\n", __func__);

		/* All handling should be done by __videobuf_mmap_mapper() */
		if (!mem->vaddr) {
			dev_err(q->dev, "memory is not alloced/mmapped.\n");
			return -EINVAL;
		}
		break;
	case V4L2_MEMORY_USERPTR:
		dev_dbg(q->dev, "%s memory method USERPTR\n", __func__);

		/* handle pointer from user space */
		if (vb->baddr)
			return videobuf_dma_contig_user_get(mem, vb);

		/* allocate memory for the read() method */
		mem->size = PAGE_ALIGN(vb->size);
		mem->vaddr = dma_alloc_coherent(q->dev, mem->size,
						&mem->dma_handle, GFP_KERNEL);
		if (!mem->vaddr) {
			dev_err(q->dev, "dma_alloc_coherent %ld failed\n",
					 mem->size);
			return -ENOMEM;
		}

		dev_dbg(q->dev, "dma_alloc_coherent data is at %p (%ld)\n",
			mem->vaddr, mem->size);
		break;
	case V4L2_MEMORY_OVERLAY:
	default:
		dev_dbg(q->dev, "%s memory method OVERLAY/unknown\n",
			__func__);
		return -EINVAL;
	}

	return 0;
}

static int __videobuf_mmap_free(struct videobuf_queue *q)
{
	unsigned int i;

	dev_dbg(q->dev, "%s\n", __func__);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (q->bufs[i] && q->bufs[i]->map)
			return -EBUSY;
	}

	return 0;
}

static int __videobuf_mmap_mapper(struct videobuf_queue *q,
				  struct vm_area_struct *vma)
{
	struct videobuf_dma_contig_memory *mem;
	struct videobuf_mapping *map;
	unsigned int first;
	int retval;
	unsigned long size, offset = vma->vm_pgoff << PAGE_SHIFT;

	dev_dbg(q->dev, "%s\n", __func__);
	if (!(vma->vm_flags & VM_WRITE) || !(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	/* look for first buffer to map */
	for (first = 0; first < VIDEO_MAX_FRAME; first++) {
		if (!q->bufs[first])
			continue;

		if (V4L2_MEMORY_MMAP != q->bufs[first]->memory)
			continue;
		if (q->bufs[first]->boff == offset)
			break;
	}
	if (VIDEO_MAX_FRAME == first) {
		dev_dbg(q->dev, "invalid user space offset [offset=0x%lx]\n",
			offset);
		return -EINVAL;
	}

	/* create mapping + update buffer list */
	map = kzalloc(sizeof(struct videobuf_mapping), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	q->bufs[first]->map = map;
	map->start = vma->vm_start;
	map->end = vma->vm_end;
	map->q = q;

	q->bufs[first]->baddr = vma->vm_start;

	mem = q->bufs[first]->priv;
	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

	mem->size = PAGE_ALIGN(q->bufs[first]->bsize);
	mem->vaddr = dma_alloc_coherent(q->dev, mem->size,
					&mem->dma_handle, GFP_KERNEL);
	if (!mem->vaddr) {
		dev_err(q->dev, "dma_alloc_coherent size %ld failed\n",
			mem->size);
		goto error;
	}
	dev_dbg(q->dev, "dma_alloc_coherent data is at addr %p (size %ld)\n",
		mem->vaddr, mem->size);

	/* Try to remap memory */

	size = vma->vm_end - vma->vm_start;
	size = (size < mem->size) ? size : mem->size;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	retval = remap_pfn_range(vma, vma->vm_start,
				 mem->dma_handle >> PAGE_SHIFT,
				 size, vma->vm_page_prot);
	if (retval) {
		dev_err(q->dev, "mmap: remap failed with error %d. ", retval);
		dma_free_coherent(q->dev, mem->size,
				  mem->vaddr, mem->dma_handle);
		goto error;
	}

	vma->vm_ops          = &videobuf_vm_ops;
	vma->vm_flags       |= VM_DONTEXPAND;
	vma->vm_private_data = map;

	dev_dbg(q->dev, "mmap %p: q=%p %08lx-%08lx (%lx) pgoff %08lx buf %d\n",
		map, q, vma->vm_start, vma->vm_end,
		(long int) q->bufs[first]->bsize,
		vma->vm_pgoff, first);

	videobuf_vm_open(vma);

	return 0;

error:
	kfree(map);
	return -ENOMEM;
}

static int __videobuf_copy_to_user(struct videobuf_queue *q,
				   char __user *data, size_t count,
				   int nonblocking)
{
	struct videobuf_dma_contig_memory *mem = q->read_buf->priv;
	void *vaddr;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);
	BUG_ON(!mem->vaddr);

	/* copy to userspace */
	if (count > q->read_buf->size - q->read_off)
		count = q->read_buf->size - q->read_off;

	vaddr = mem->vaddr;

	if (copy_to_user(data, vaddr + q->read_off, count))
		return -EFAULT;

	return count;
}

static int __videobuf_copy_stream(struct videobuf_queue *q,
				  char __user *data, size_t count, size_t pos,
				  int vbihack, int nonblocking)
{
	unsigned int  *fc;
	struct videobuf_dma_contig_memory *mem = q->read_buf->priv;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

	if (vbihack) {
		/* dirty, undocumented hack -- pass the frame counter
			* within the last four bytes of each vbi data block.
			* We need that one to maintain backward compatibility
			* to all vbi decoding software out there ... */
		fc = (unsigned int *)mem->vaddr;
		fc += (q->read_buf->size >> 2) - 1;
		*fc = q->read_buf->field_count >> 1;
		dev_dbg(q->dev, "vbihack: %d\n", *fc);
	}

	/* copy stuff using the common method */
	count = __videobuf_copy_to_user(q, data, count, nonblocking);

	if ((count == -EFAULT) && (pos == 0))
		return -EFAULT;

	return count;
}

static struct videobuf_qtype_ops qops = {
	.magic        = MAGIC_QTYPE_OPS,

	.alloc        = __videobuf_alloc,
	.iolock       = __videobuf_iolock,
	.mmap_free    = __videobuf_mmap_free,
	.mmap_mapper  = __videobuf_mmap_mapper,
	.video_copy_to_user = __videobuf_copy_to_user,
	.copy_stream  = __videobuf_copy_stream,
	.vmalloc      = __videobuf_to_vmalloc,
};

void videobuf_queue_dma_contig_init(struct videobuf_queue *q,
				    struct videobuf_queue_ops *ops,
				    struct device *dev,
				    spinlock_t *irqlock,
				    enum v4l2_buf_type type,
				    enum v4l2_field field,
				    unsigned int msize,
				    void *priv)
{
	videobuf_queue_core_init(q, ops, dev, irqlock, type, field, msize,
				 priv, &qops);
}
EXPORT_SYMBOL_GPL(videobuf_queue_dma_contig_init);

dma_addr_t videobuf_to_dma_contig(struct videobuf_buffer *buf)
{
	struct videobuf_dma_contig_memory *mem = buf->priv;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

	return mem->dma_handle;
}
EXPORT_SYMBOL_GPL(videobuf_to_dma_contig);

void videobuf_dma_contig_free(struct videobuf_queue *q,
			      struct videobuf_buffer *buf)
{
	struct videobuf_dma_contig_memory *mem = buf->priv;

	/* mmapped memory can't be freed here, otherwise mmapped region
	   would be released, while still needed. In this case, the memory
	   release should happen inside videobuf_vm_close().
	   So, it should free memory only if the memory were allocated for
	   read() operation.
	 */
	if (buf->memory != V4L2_MEMORY_USERPTR)
		return;

	if (!mem)
		return;

	MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

	/* handle user space pointer case */
	if (buf->baddr) {
		videobuf_dma_contig_user_put(mem);
		return;
	}

	/* read() method */
	dma_free_coherent(q->dev, mem->size, mem->vaddr, mem->dma_handle);
	mem->vaddr = NULL;
}
EXPORT_SYMBOL_GPL(videobuf_dma_contig_free);

MODULE_DESCRIPTION("helper module to manage video4linux dma contig buffers");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL");
