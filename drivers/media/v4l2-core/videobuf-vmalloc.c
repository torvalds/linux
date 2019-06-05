// SPDX-License-Identifier: GPL-2.0
/*
 * helper functions for vmalloc video4linux capture buffers
 *
 * The functions expect the hardware being able to scatter gather
 * (i.e. the buffers are not linear in physical memory, but fragmented
 * into PAGE_SIZE chunks).  They also assume the driver does not need
 * to touch the video data.
 *
 * (c) 2007 Mauro Carvalho Chehab <mchehab@kernel.org>
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

#define MAGIC_CHECK(is, should)						\
	if (unlikely((is) != (should))) {				\
		printk(KERN_ERR "magic mismatch: %x (expected %x)\n",	\
				is, should);				\
		BUG();							\
	}

static int debug;
module_param(debug, int, 0644);

MODULE_DESCRIPTION("helper module to manage video4linux vmalloc buffers");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@kernel.org>");
MODULE_LICENSE("GPL");

#define dprintk(level, fmt, arg...)					\
	if (debug >= level)						\
		printk(KERN_DEBUG "vbuf-vmalloc: " fmt , ## arg)


/***************************************************************************/

static void videobuf_vm_open(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;

	dprintk(2, "vm_open %p [count=%u,vma=%08lx-%08lx]\n", map,
		map->count, vma->vm_start, vma->vm_end);

	map->count++;
}

static void videobuf_vm_close(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;
	struct videobuf_queue *q = map->q;
	int i;

	dprintk(2, "vm_close %p [count=%u,vma=%08lx-%08lx]\n", map,
		map->count, vma->vm_start, vma->vm_end);

	map->count--;
	if (0 == map->count) {
		struct videobuf_vmalloc_memory *mem;

		dprintk(1, "munmap %p q=%p\n", map, q);
		videobuf_queue_lock(q);

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
					__func__, i, mem->vaddr);

				vfree(mem->vaddr);
				mem->vaddr = NULL;
			}

			q->bufs[i]->map   = NULL;
			q->bufs[i]->baddr = 0;
		}

		kfree(map);

		videobuf_queue_unlock(q);
	}

	return;
}

static const struct vm_operations_struct videobuf_vm_ops = {
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

static struct videobuf_buffer *__videobuf_alloc_vb(size_t size)
{
	struct videobuf_vmalloc_memory *mem;
	struct videobuf_buffer *vb;

	vb = kzalloc(size + sizeof(*mem), GFP_KERNEL);
	if (!vb)
		return vb;

	mem = vb->priv = ((char *)vb) + size;
	mem->magic = MAGIC_VMAL_MEM;

	dprintk(1, "%s: allocated at %p(%ld+%ld) & %p(%ld)\n",
		__func__, vb, (long)sizeof(*vb), (long)size - sizeof(*vb),
		mem, (long)sizeof(*mem));

	return vb;
}

static int __videobuf_iolock(struct videobuf_queue *q,
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
		if (!mem->vaddr) {
			printk(KERN_ERR "memory is not allocated/mmapped.\n");
			return -EINVAL;
		}
		break;
	case V4L2_MEMORY_USERPTR:
		pages = PAGE_ALIGN(vb->size);

		dprintk(1, "%s memory method USERPTR\n", __func__);

		if (vb->baddr) {
			printk(KERN_ERR "USERPTR is currently not supported\n");
			return -EINVAL;
		}

		/* The only USERPTR currently supported is the one needed for
		 * read() method.
		 */

		mem->vaddr = vmalloc_user(pages);
		if (!mem->vaddr) {
			printk(KERN_ERR "vmalloc (%d pages) failed\n", pages);
			return -ENOMEM;
		}
		dprintk(1, "vmalloc is at addr %p (%d pages)\n",
			mem->vaddr, pages);
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

static int __videobuf_mmap_mapper(struct videobuf_queue *q,
				  struct videobuf_buffer *buf,
				  struct vm_area_struct *vma)
{
	struct videobuf_vmalloc_memory *mem;
	struct videobuf_mapping *map;
	int retval, pages;

	dprintk(1, "%s\n", __func__);

	/* create mapping + update buffer list */
	map = kzalloc(sizeof(struct videobuf_mapping), GFP_KERNEL);
	if (NULL == map)
		return -ENOMEM;

	buf->map = map;
	map->q     = q;

	buf->baddr = vma->vm_start;

	mem = buf->priv;
	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_VMAL_MEM);

	pages = PAGE_ALIGN(vma->vm_end - vma->vm_start);
	mem->vaddr = vmalloc_user(pages);
	if (!mem->vaddr) {
		printk(KERN_ERR "vmalloc (%d pages) failed\n", pages);
		goto error;
	}
	dprintk(1, "vmalloc is at addr %p (%d pages)\n", mem->vaddr, pages);

	/* Try to remap memory */
	retval = remap_vmalloc_range(vma, mem->vaddr, 0);
	if (retval < 0) {
		printk(KERN_ERR "mmap: remap failed with error %d. ", retval);
		vfree(mem->vaddr);
		goto error;
	}

	vma->vm_ops          = &videobuf_vm_ops;
	vma->vm_flags       |= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_private_data = map;

	dprintk(1, "mmap %p: q=%p %08lx-%08lx (%lx) pgoff %08lx buf %d\n",
		map, q, vma->vm_start, vma->vm_end,
		(long int)buf->bsize,
		vma->vm_pgoff, buf->i);

	videobuf_vm_open(vma);

	return 0;

error:
	mem = NULL;
	kfree(map);
	return -ENOMEM;
}

static struct videobuf_qtype_ops qops = {
	.magic        = MAGIC_QTYPE_OPS,

	.alloc_vb     = __videobuf_alloc_vb,
	.iolock       = __videobuf_iolock,
	.mmap_mapper  = __videobuf_mmap_mapper,
	.vaddr        = videobuf_to_vmalloc,
};

void videobuf_queue_vmalloc_init(struct videobuf_queue *q,
			 const struct videobuf_queue_ops *ops,
			 struct device *dev,
			 spinlock_t *irqlock,
			 enum v4l2_buf_type type,
			 enum v4l2_field field,
			 unsigned int msize,
			 void *priv,
			 struct mutex *ext_lock)
{
	videobuf_queue_core_init(q, ops, dev, irqlock, type, field, msize,
				 priv, &qops, ext_lock);
}
EXPORT_SYMBOL_GPL(videobuf_queue_vmalloc_init);

void *videobuf_to_vmalloc(struct videobuf_buffer *buf)
{
	struct videobuf_vmalloc_memory *mem = buf->priv;
	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_VMAL_MEM);

	return mem->vaddr;
}
EXPORT_SYMBOL_GPL(videobuf_to_vmalloc);

void videobuf_vmalloc_free(struct videobuf_buffer *buf)
{
	struct videobuf_vmalloc_memory *mem = buf->priv;

	/* mmapped memory can't be freed here, otherwise mmapped region
	   would be released, while still needed. In this case, the memory
	   release should happen inside videobuf_vm_close().
	   So, it should free memory only if the memory were allocated for
	   read() operation.
	 */
	if ((buf->memory != V4L2_MEMORY_USERPTR) || buf->baddr)
		return;

	if (!mem)
		return;

	MAGIC_CHECK(mem->magic, MAGIC_VMAL_MEM);

	vfree(mem->vaddr);
	mem->vaddr = NULL;

	return;
}
EXPORT_SYMBOL_GPL(videobuf_vmalloc_free);

