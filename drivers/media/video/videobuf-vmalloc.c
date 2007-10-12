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

static int debug = 0;
module_param(debug, int, 0644);

MODULE_DESCRIPTION("helper module to manage video4linux vmalloc buffers");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");

#define dprintk(level, fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "vbuf-sg: " fmt , ## arg)


/***************************************************************************/

static void
videobuf_vm_open(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;

	dprintk(2,"vm_open %p [count=%d,vma=%08lx-%08lx]\n",map,
		map->count,vma->vm_start,vma->vm_end);

	map->count++;
}

static void
videobuf_vm_close(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;
	struct videobuf_queue *q = map->q;
	int i;

	dprintk(2,"vm_close %p [count=%d,vma=%08lx-%08lx]\n",map,
		map->count,vma->vm_start,vma->vm_end);

	map->count--;
	if (0 == map->count) {
		dprintk(1,"munmap %p q=%p\n",map,q);
		mutex_lock(&q->lock);
		for (i = 0; i < VIDEO_MAX_FRAME; i++) {
			if (NULL == q->bufs[i])
				continue;

			if (q->bufs[i]->map != map)
				continue;

			q->ops->buf_release(q,q->bufs[i]);

			q->bufs[i]->map   = NULL;
			q->bufs[i]->baddr = 0;
		}
		mutex_unlock(&q->lock);
		kfree(map);
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
	struct videobuf_pci_sg_memory
 */

static void *__videobuf_alloc(size_t size)
{
	struct videbuf_vmalloc_memory *mem;
	struct videobuf_buffer *vb;

	vb = kzalloc(size+sizeof(*mem),GFP_KERNEL);

	mem = vb->priv = ((char *)vb)+size;
	mem->magic=MAGIC_VMAL_MEM;

	dprintk(1,"%s: allocated at %p(%ld+%ld) & %p(%ld)\n",
		__FUNCTION__,vb,(long)sizeof(*vb),(long)size-sizeof(*vb),
		mem,(long)sizeof(*mem));

	return vb;
}

static int __videobuf_iolock (struct videobuf_queue* q,
			      struct videobuf_buffer *vb,
			      struct v4l2_framebuffer *fbuf)
{
	int pages;

	struct videbuf_vmalloc_memory *mem=vb->priv;


	BUG_ON(!mem);

	MAGIC_CHECK(mem->magic,MAGIC_VMAL_MEM);

	pages = PAGE_ALIGN(vb->size) >> PAGE_SHIFT;

	/* Currently, doesn't support V4L2_MEMORY_OVERLAY */
	if ((vb->memory != V4L2_MEMORY_MMAP) &&
				(vb->memory != V4L2_MEMORY_USERPTR) ) {
		printk(KERN_ERR "Method currently unsupported.\n");
		return -EINVAL;
	}

	/* FIXME: should be tested with kernel mmap mem */
	mem->vmalloc=vmalloc_user (PAGE_ALIGN(vb->size));
	if (NULL == mem->vmalloc) {
		printk(KERN_ERR "vmalloc (%d pages) failed\n",pages);
		return -ENOMEM;
	}

	dprintk(1,"vmalloc is at addr 0x%08lx, size=%d\n",
				(unsigned long)mem->vmalloc,
				pages << PAGE_SHIFT);

	/* It seems that some kernel versions need to do remap *after*
	   the mmap() call
	 */
	if (mem->vma) {
		int retval=remap_vmalloc_range(mem->vma, mem->vmalloc,0);
		kfree(mem->vma);
		mem->vma=NULL;
		if (retval<0) {
			dprintk(1,"mmap app bug: remap_vmalloc_range area %p error %d\n",
				mem->vmalloc,retval);
			return retval;
		}
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
	struct videbuf_vmalloc_memory *mem;
	struct videobuf_mapping *map;
	unsigned int first;
	int retval;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

	if (! (vma->vm_flags & VM_WRITE) || ! (vma->vm_flags & VM_SHARED))
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
	map = q->bufs[first]->map = kmalloc(sizeof(struct videobuf_mapping),GFP_KERNEL);
	if (NULL == map)
		return -ENOMEM;

	map->start = vma->vm_start;
	map->end   = vma->vm_end;
	map->q     = q;

	q->bufs[first]->baddr = vma->vm_start;

	vma->vm_ops          = &videobuf_vm_ops;
	vma->vm_flags       |= VM_DONTEXPAND | VM_RESERVED;
	vma->vm_private_data = map;

	mem=q->bufs[first]->priv;
	BUG_ON (!mem);
	MAGIC_CHECK(mem->magic,MAGIC_VMAL_MEM);

	/* Try to remap memory */
	retval=remap_vmalloc_range(vma, mem->vmalloc,0);
	if (retval<0) {
		dprintk(1,"mmap: postponing remap_vmalloc_range\n");

		mem->vma=kmalloc(sizeof(*vma),GFP_KERNEL);
		if (!mem->vma) {
			kfree(map);
			q->bufs[first]->map=NULL;
			return -ENOMEM;
		}
		memcpy(mem->vma,vma,sizeof(*vma));
	}

	dprintk(1,"mmap %p: q=%p %08lx-%08lx (%lx) pgoff %08lx buf %d\n",
		map,q,vma->vm_start,vma->vm_end,
		(long int) q->bufs[first]->bsize,
		vma->vm_pgoff,first);

	videobuf_vm_open(vma);

	return (0);
}

static int __videobuf_copy_to_user ( struct videobuf_queue *q,
				char __user *data, size_t count,
				int nonblocking )
{
	struct videbuf_vmalloc_memory *mem=q->read_buf->priv;
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
	struct videbuf_vmalloc_memory *mem=q->read_buf->priv;
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
	.copy_to_user = __videobuf_copy_to_user,
	.copy_stream  = __videobuf_copy_stream,
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
	struct videbuf_vmalloc_memory *mem=buf->priv;
	BUG_ON (!mem);
	MAGIC_CHECK(mem->magic,MAGIC_VMAL_MEM);

	return mem->vmalloc;
}
EXPORT_SYMBOL_GPL(videobuf_to_vmalloc);

void videobuf_vmalloc_free (struct videobuf_buffer *buf)
{
	struct videbuf_vmalloc_memory *mem=buf->priv;
	BUG_ON (!mem);

	MAGIC_CHECK(mem->magic,MAGIC_VMAL_MEM);

	vfree(mem->vmalloc);
	mem->vmalloc=NULL;

	return;
}
EXPORT_SYMBOL_GPL(videobuf_vmalloc_free);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
