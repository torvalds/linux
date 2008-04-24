/*
 * helper functions for vmalloc video4linux capture buffers
 *
 * The functions expect the hardware being able to scatter gatter
 * (i.e. the buffers are not linear in physical memory, but fragmented
 * into PAGE_SIZE chunks).  They also assume the driver does not need
 * to touch the video data.
 *
 * (c) 2007 Mauro Carvalho Chehab, <mchehab@infradead.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <media/videobuf-vmalloc.h>

#define MAGIC_DMABUF   0x17760309
#define MAGIC_VMAL_MEM 0x18221223

#define MAGIC_CHECK(is,should)	if (unlikely((is) != (should))) \
	{ printk(KERN_ERR "magic mismatch: %x (expected %x)\n",is,should); BUG(); }

static int debug;
module_param(debug, int, 0644);

MODULE_DESCRIPTION("helper module to manage video4linux vmalloc buffers");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");

#define dprintk(level, fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "vbuf-vmalloc: " fmt , ## arg)


/***************************************************************************/

static void
videobuf_vm_open(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;

	dprintk(2,"vm_open %p [count=%u,vma=%08lx-%08lx]\n",map,
		map->count,vma->vm_start,vma->vm_end);

	map->count++;
}

static void videobuf_vm_close(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;
	struct videobuf_queue *q = map->q;
	int i;

	dprintk(2,"vm_close %p [count=%u,vma=%08lx-%08lx]\n", map,
		map->count, vma->vm_start, vma->vm_end);

	map->count--;
	if (0 == map->count) {
		struct videobuf_vmalloc_memory *mem;

		dprintk(1, "munmap %p q=%p\n", map, q);
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

				MAGIC_CHECK(mem->magic, MAGIC_VMAL_MEM);

				/* vfree is not atomic - can't be
				   called with IRQ's disabled
				 */
				dprintk(1, "%s: buf[%d] freeing (%p)\n",
					__func__, i, mem->vmalloc);

				vfree(mem->vmalloc);
				mem->vmalloc = NULL;
			}

			q->bufs[i]->map   = NULL;
			q->bufs[i]->baddr = 0;
		}

		kfree(map);

		mutex_unlock(&q->vb_lock);
	}

	return;
}

static struct vm_operations_struct videobuf_vm_ops =
{
	.open     = videobuf_vm_open,
	.close    = videobuf_vm_close,
};

/* ---------------------------------------------------------------------
 * vmalloc handlers for the generic methods
 */

/* Allocated area consists on 3 parts:
	struct video_buffer
	struct <driver>_buffer (cx88_buffer, saa7134_buf, ...)
	struct videobuf_dma_sg_memory
 */

static void *__videobuf_alloc(size_t size)
{
	struct videobuf_vmalloc_memory *mem;
	struct videobuf_buffer *vb;

	vb = kzalloc(size+sizeof(*mem),GFP_KERNEL);

	mem = vb->priv = ((char *)vb)+size;
	mem->magic=MAGIC_VMAL_MEM;

	dprintk(1,"%s: allocated at %p(%ld+%ld) & %p(%ld)\n",
		__func__,vb,(long)sizeof(*vb),(long)size-sizeof(*vb),
		mem,(long)sizeof(*mem));

	return vb;
}

static int __videobuf_iolock (struct videobuf_queue* q,
			      struct videobuf_buffer *vb,
			      struct v4l2_framebuffer *fbuf)
{
	struct videobuf_vmalloc_memory *mem = vb->priv;
	int pages;

	BUG_ON(!mem);

	MAGIC_CHECK(mem->magic, MAGIC_VMAL_MEM);

	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
		dprintk(1, "%s memory method MMAP\n", __func__);

		/* All handling should be done by __videobuf_mmap_mapper() */
		if (!mem->vmalloc) {
			printk(KERN_ERR "memory is not alloced/mmapped.\n");
			return -EINVAL;
		}
		break;
	case V4L2_MEMORY_USERPTR:
		pages = PAGE_ALIGN(vb->size);

		dprintk(1, "%s memory method USERPTR\n", __func__);

#if 1
		if (vb->baddr) {
			printk(KERN_ERR "USERPTR is currently not supported\n");
			return -EINVAL;
		}
#endif

		/* The only USERPTR currently supported is the one needed for
		   read() method.
		 */

		mem->vmalloc = vmalloc_user(pages);
		if (!mem->vmalloc) {
			printk(KERN_ERR "vmalloc (%d pages) failed\n", pages);
			return -ENOMEM;
		}
		dprintk(1, "vmalloc is at addr %p (%d pages)\n",
			mem->vmalloc, pages);

#if 0
		int rc;
		/* Kernel userptr is used also by read() method. In this case,
		   there's no need to remap, since data will be copied to user
		 */
		if (!vb->baddr)
			return 0;

		/* FIXME: to properly support USERPTR, remap should occur.
		   The code bellow won't work, since mem->vma = NULL
		 */
		/* Try to remap memory */
		rc = remap_vmalloc_range(mem->vma, (void *)vb->baddr, 0);
		if (rc < 0) {
			printk(KERN_ERR "mmap: remap failed with error %d. ", rc);
			return -ENOMEM;
		}
#endif

		break;
	case V4L2_MEMORY_OVERLAY:
	default:
		dprintk(1, "%s memory method OVERLAY/unknown\n", __func__);

		/* Currently, doesn't support V4L2_MEMORY_OVERLAY */
		printk(KERN_ERR "Memory method currently unsupported.\n");
		return -EINVAL;
	}

	return 0;
}

static int __videobuf_sync(struct videobuf_queue *q,
			   struct videobuf_buffer *buf)
{
	return 0;
}

static int __videobuf_mmap_free(struct videobuf_queue *q)
{
	unsigned int i;

	dprintk(1, "%s\n", __func__);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (q->bufs[i]) {
			if (q->bufs[i]->map)
				return -EBUSY;
		}
	}

	return 0;
}

static int __videobuf_mmap_mapper(struct videobuf_queue *q,
			 struct vm_area_struct *vma)
{
	struct videobuf_vmalloc_memory *mem;
	struct videobuf_mapping *map;
	unsigned int first;
	int retval, pages;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

	dprintk(1, "%s\n", __func__);
	if (!(vma->vm_flags & VM_WRITE) || !(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	/* look for first buffer to map */
	for (first = 0; first < VIDEO_MAX_FRAME; first++) {
		if (NULL == q->bufs[first])
			continue;

		if (V4L2_MEMORY_MMAP != q->bufs[first]->memory)
			continue;
		if (q->bufs[first]->boff == offset)
			break;
	}
	if (VIDEO_MAX_FRAME == first) {
		dprintk(1,"mmap app bug: offset invalid [offset=0x%lx]\n",
			(vma->vm_pgoff << PAGE_SHIFT));
		return -EINVAL;
	}

	/* create mapping + update buffer list */
	map = kzalloc(sizeof(struct videobuf_mapping), GFP_KERNEL);
	if (NULL == map)
		return -ENOMEM;

	q->bufs[first]->map = map;
	map->start = vma->vm_start;
	map->end   = vma->vm_end;
	map->q     = q;

	q->bufs[first]->baddr = vma->vm_start;

	mem = q->bufs[first]->priv;
	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_VMAL_MEM);

	pages = PAGE_ALIGN(vma->vm_end - vma->vm_start);
	mem->vmalloc = vmalloc_user(pages);
	if (!mem->vmalloc) {
		printk(KERN_ERR "vmalloc (%d pages) failed\n", pages);
		goto error;
	}
	dprintk(1, "vmalloc is at addr %p (%d pages)\n",
		mem->vmalloc, pages);

	/* Try to remap memory */
	retval = remap_vmalloc_range(vma, mem->vmalloc, 0);
	if (retval < 0) {
		printk(KERN_ERR "mmap: remap failed with error %d. ", retval);
		vfree(mem->vmalloc);
		goto error;
	}

	vma->vm_ops          = &videobuf_vm_ops;
	vma->vm_flags       |= VM_DONTEXPAND | VM_RESERVED;
	vma->vm_private_data = map;

	dprintk(1,"mmap %p: q=%p %08lx-%08lx (%lx) pgoff %08lx buf %d\n",
		map, q, vma->vm_start, vma->vm_end,
		(long int) q->bufs[first]->bsize,
		vma->vm_pgoff, first);

	videobuf_vm_open(vma);

	return 0;

error:
	mem = NULL;
	kfree(map);
	return -ENOMEM;
}

static int __videobuf_copy_to_user ( struct videobuf_queue *q,
				char __user *data, size_t count,
				int nonblocking )
{
	struct videobuf_vmalloc_memory *mem=q->read_buf->priv;
	BUG_ON (!mem);
	MAGIC_CHECK(mem->magic,MAGIC_VMAL_MEM);

	BUG_ON (!mem->vmalloc);

	/* copy to userspace */
	if (count > q->read_buf->size - q->read_off)
		count = q->read_buf->size - q->read_off;

	if (copy_to_user(data, mem->vmalloc+q->read_off, count))
		return -EFAULT;

	return count;
}

static int __videobuf_copy_stream ( struct videobuf_queue *q,
				char __user *data, size_t count, size_t pos,
				int vbihack, int nonblocking )
{
	unsigned int  *fc;
	struct videobuf_vmalloc_memory *mem=q->read_buf->priv;
	BUG_ON (!mem);
	MAGIC_CHECK(mem->magic,MAGIC_VMAL_MEM);

	if (vbihack) {
		/* dirty, undocumented hack -- pass the frame counter
			* within the last four bytes of each vbi data block.
			* We need that one to maintain backward compatibility
			* to all vbi decoding software out there ... */
		fc  = (unsigned int*)mem->vmalloc;
		fc += (q->read_buf->size>>2) -1;
		*fc = q->read_buf->field_count >> 1;
		dprintk(1,"vbihack: %d\n",*fc);
	}

	/* copy stuff using the common method */
	count = __videobuf_copy_to_user (q,data,count,nonblocking);

	if ( (count==-EFAULT) && (0 == pos) )
		return -EFAULT;

	return count;
}

static struct videobuf_qtype_ops qops = {
	.magic        = MAGIC_QTYPE_OPS,

	.alloc        = __videobuf_alloc,
	.iolock       = __videobuf_iolock,
	.sync         = __videobuf_sync,
	.mmap_free    = __videobuf_mmap_free,
	.mmap_mapper  = __videobuf_mmap_mapper,
	.video_copy_to_user = __videobuf_copy_to_user,
	.copy_stream  = __videobuf_copy_stream,
	.vmalloc      = videobuf_to_vmalloc,
};

void videobuf_queue_vmalloc_init(struct videobuf_queue* q,
			 struct videobuf_queue_ops *ops,
			 void *dev,
			 spinlock_t *irqlock,
			 enum v4l2_buf_type type,
			 enum v4l2_field field,
			 unsigned int msize,
			 void *priv)
{
	videobuf_queue_core_init(q, ops, dev, irqlock, type, field, msize,
				 priv, &qops);
}

EXPORT_SYMBOL_GPL(videobuf_queue_vmalloc_init);

void *videobuf_to_vmalloc (struct videobuf_buffer *buf)
{
	struct videobuf_vmalloc_memory *mem=buf->priv;
	BUG_ON (!mem);
	MAGIC_CHECK(mem->magic,MAGIC_VMAL_MEM);

	return mem->vmalloc;
}
EXPORT_SYMBOL_GPL(videobuf_to_vmalloc);

void videobuf_vmalloc_free (struct videobuf_buffer *buf)
{
	struct videobuf_vmalloc_memory *mem = buf->priv;

	/* mmapped memory can't be freed here, otherwise mmapped region
	   would be released, while still needed. In this case, the memory
	   release should happen inside videobuf_vm_close().
	   So, it should free memory only if the memory were allocated for
	   read() operation.
	 */
	if ((buf->memory != V4L2_MEMORY_USERPTR) || (buf->baddr == 0))
		return;

	if (!mem)
		return;

	MAGIC_CHECK(mem->magic, MAGIC_VMAL_MEM);

	vfree(mem->vmalloc);
	mem->vmalloc = NULL;

	return;
}
EXPORT_SYMBOL_GPL(videobuf_vmalloc_free);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
