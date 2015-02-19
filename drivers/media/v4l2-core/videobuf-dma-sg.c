/*
 * helper functions for SG DMA video4linux capture buffers
 *
 * The functions expect the hardware being able to scatter gather
 * (i.e. the buffers are not linear in physical memory, but fragmented
 * into PAGE_SIZE chunks).  They also assume the driver does not need
 * to touch the video data.
 *
 * (c) 2007 Mauro Carvalho Chehab, <mchehab@infradead.org>
 *
 * Highly based on video-buf written originally by:
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org>
 * (c) 2006 Mauro Carvalho Chehab, <mchehab@infradead.org>
 * (c) 2006 Ted Walther and John Sokol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <media/videobuf-dma-sg.h>

#define MAGIC_DMABUF 0x19721112
#define MAGIC_SG_MEM 0x17890714

#define MAGIC_CHECK(is, should)						\
	if (unlikely((is) != (should))) {				\
		printk(KERN_ERR "magic mismatch: %x (expected %x)\n",	\
				is, should);				\
		BUG();							\
	}

static int debug;
module_param(debug, int, 0644);

MODULE_DESCRIPTION("helper module to manage video4linux dma sg buffers");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");

#define dprintk(level, fmt, arg...)					\
	if (debug >= level)						\
		printk(KERN_DEBUG "vbuf-sg: " fmt , ## arg)

/* --------------------------------------------------------------------- */

/*
 * Return a scatterlist for some page-aligned vmalloc()'ed memory
 * block (NULL on errors).  Memory for the scatterlist is allocated
 * using kmalloc.  The caller must free the memory.
 */
static struct scatterlist *videobuf_vmalloc_to_sg(unsigned char *virt,
						  int nr_pages)
{
	struct scatterlist *sglist;
	struct page *pg;
	int i;

	sglist = vzalloc(nr_pages * sizeof(*sglist));
	if (NULL == sglist)
		return NULL;
	sg_init_table(sglist, nr_pages);
	for (i = 0; i < nr_pages; i++, virt += PAGE_SIZE) {
		pg = vmalloc_to_page(virt);
		if (NULL == pg)
			goto err;
		BUG_ON(PageHighMem(pg));
		sg_set_page(&sglist[i], pg, PAGE_SIZE, 0);
	}
	return sglist;

err:
	vfree(sglist);
	return NULL;
}

/*
 * Return a scatterlist for a an array of userpages (NULL on errors).
 * Memory for the scatterlist is allocated using kmalloc.  The caller
 * must free the memory.
 */
static struct scatterlist *videobuf_pages_to_sg(struct page **pages,
					int nr_pages, int offset, size_t size)
{
	struct scatterlist *sglist;
	int i;

	if (NULL == pages[0])
		return NULL;
	sglist = vmalloc(nr_pages * sizeof(*sglist));
	if (NULL == sglist)
		return NULL;
	sg_init_table(sglist, nr_pages);

	if (PageHighMem(pages[0]))
		/* DMA to highmem pages might not work */
		goto highmem;
	sg_set_page(&sglist[0], pages[0],
			min_t(size_t, PAGE_SIZE - offset, size), offset);
	size -= min_t(size_t, PAGE_SIZE - offset, size);
	for (i = 1; i < nr_pages; i++) {
		if (NULL == pages[i])
			goto nopage;
		if (PageHighMem(pages[i]))
			goto highmem;
		sg_set_page(&sglist[i], pages[i], min_t(size_t, PAGE_SIZE, size), 0);
		size -= min_t(size_t, PAGE_SIZE, size);
	}
	return sglist;

nopage:
	dprintk(2, "sgl: oops - no page\n");
	vfree(sglist);
	return NULL;

highmem:
	dprintk(2, "sgl: oops - highmem page\n");
	vfree(sglist);
	return NULL;
}

/* --------------------------------------------------------------------- */

struct videobuf_dmabuf *videobuf_to_dma(struct videobuf_buffer *buf)
{
	struct videobuf_dma_sg_memory *mem = buf->priv;
	BUG_ON(!mem);

	MAGIC_CHECK(mem->magic, MAGIC_SG_MEM);

	return &mem->dma;
}
EXPORT_SYMBOL_GPL(videobuf_to_dma);

static void videobuf_dma_init(struct videobuf_dmabuf *dma)
{
	memset(dma, 0, sizeof(*dma));
	dma->magic = MAGIC_DMABUF;
}

static int videobuf_dma_init_user_locked(struct videobuf_dmabuf *dma,
			int direction, unsigned long data, unsigned long size)
{
	unsigned long first, last;
	int err, rw = 0;

	dma->direction = direction;
	switch (dma->direction) {
	case DMA_FROM_DEVICE:
		rw = READ;
		break;
	case DMA_TO_DEVICE:
		rw = WRITE;
		break;
	default:
		BUG();
	}

	first = (data          & PAGE_MASK) >> PAGE_SHIFT;
	last  = ((data+size-1) & PAGE_MASK) >> PAGE_SHIFT;
	dma->offset = data & ~PAGE_MASK;
	dma->size = size;
	dma->nr_pages = last-first+1;
	dma->pages = kmalloc(dma->nr_pages * sizeof(struct page *), GFP_KERNEL);
	if (NULL == dma->pages)
		return -ENOMEM;

	dprintk(1, "init user [0x%lx+0x%lx => %d pages]\n",
		data, size, dma->nr_pages);

	err = get_user_pages(current, current->mm,
			     data & PAGE_MASK, dma->nr_pages,
			     rw == READ, 1, /* force */
			     dma->pages, NULL);

	if (err != dma->nr_pages) {
		dma->nr_pages = (err >= 0) ? err : 0;
		dprintk(1, "get_user_pages: err=%d [%d]\n", err, dma->nr_pages);
		return err < 0 ? err : -EINVAL;
	}
	return 0;
}

static int videobuf_dma_init_user(struct videobuf_dmabuf *dma, int direction,
			   unsigned long data, unsigned long size)
{
	int ret;

	down_read(&current->mm->mmap_sem);
	ret = videobuf_dma_init_user_locked(dma, direction, data, size);
	up_read(&current->mm->mmap_sem);

	return ret;
}

static int videobuf_dma_init_kernel(struct videobuf_dmabuf *dma, int direction,
			     int nr_pages)
{
	int i;

	dprintk(1, "init kernel [%d pages]\n", nr_pages);

	dma->direction = direction;
	dma->vaddr_pages = kcalloc(nr_pages, sizeof(*dma->vaddr_pages),
				   GFP_KERNEL);
	if (!dma->vaddr_pages)
		return -ENOMEM;

	dma->dma_addr = kcalloc(nr_pages, sizeof(*dma->dma_addr), GFP_KERNEL);
	if (!dma->dma_addr) {
		kfree(dma->vaddr_pages);
		return -ENOMEM;
	}
	for (i = 0; i < nr_pages; i++) {
		void *addr;

		addr = dma_alloc_coherent(dma->dev, PAGE_SIZE,
					  &(dma->dma_addr[i]), GFP_KERNEL);
		if (addr == NULL)
			goto out_free_pages;

		dma->vaddr_pages[i] = virt_to_page(addr);
	}
	dma->vaddr = vmap(dma->vaddr_pages, nr_pages, VM_MAP | VM_IOREMAP,
			  PAGE_KERNEL);
	if (NULL == dma->vaddr) {
		dprintk(1, "vmalloc_32(%d pages) failed\n", nr_pages);
		goto out_free_pages;
	}

	dprintk(1, "vmalloc is at addr 0x%08lx, size=%d\n",
				(unsigned long)dma->vaddr,
				nr_pages << PAGE_SHIFT);

	memset(dma->vaddr, 0, nr_pages << PAGE_SHIFT);
	dma->nr_pages = nr_pages;

	return 0;
out_free_pages:
	while (i > 0) {
		void *addr;

		i--;
		addr = page_address(dma->vaddr_pages[i]);
		dma_free_coherent(dma->dev, PAGE_SIZE, addr, dma->dma_addr[i]);
	}
	kfree(dma->dma_addr);
	dma->dma_addr = NULL;
	kfree(dma->vaddr_pages);
	dma->vaddr_pages = NULL;

	return -ENOMEM;

}

static int videobuf_dma_init_overlay(struct videobuf_dmabuf *dma, int direction,
			      dma_addr_t addr, int nr_pages)
{
	dprintk(1, "init overlay [%d pages @ bus 0x%lx]\n",
		nr_pages, (unsigned long)addr);
	dma->direction = direction;

	if (0 == addr)
		return -EINVAL;

	dma->bus_addr = addr;
	dma->nr_pages = nr_pages;

	return 0;
}

static int videobuf_dma_map(struct device *dev, struct videobuf_dmabuf *dma)
{
	MAGIC_CHECK(dma->magic, MAGIC_DMABUF);
	BUG_ON(0 == dma->nr_pages);

	if (dma->pages) {
		dma->sglist = videobuf_pages_to_sg(dma->pages, dma->nr_pages,
						   dma->offset, dma->size);
	}
	if (dma->vaddr) {
		dma->sglist = videobuf_vmalloc_to_sg(dma->vaddr,
						     dma->nr_pages);
	}
	if (dma->bus_addr) {
		dma->sglist = vmalloc(sizeof(*dma->sglist));
		if (NULL != dma->sglist) {
			dma->sglen = 1;
			sg_dma_address(&dma->sglist[0])	= dma->bus_addr
							& PAGE_MASK;
			dma->sglist[0].offset = dma->bus_addr & ~PAGE_MASK;
			sg_dma_len(&dma->sglist[0]) = dma->nr_pages * PAGE_SIZE;
		}
	}
	if (NULL == dma->sglist) {
		dprintk(1, "scatterlist is NULL\n");
		return -ENOMEM;
	}
	if (!dma->bus_addr) {
		dma->sglen = dma_map_sg(dev, dma->sglist,
					dma->nr_pages, dma->direction);
		if (0 == dma->sglen) {
			printk(KERN_WARNING
			       "%s: videobuf_map_sg failed\n", __func__);
			vfree(dma->sglist);
			dma->sglist = NULL;
			dma->sglen = 0;
			return -ENOMEM;
		}
	}

	return 0;
}

int videobuf_dma_unmap(struct device *dev, struct videobuf_dmabuf *dma)
{
	MAGIC_CHECK(dma->magic, MAGIC_DMABUF);

	if (!dma->sglen)
		return 0;

	dma_unmap_sg(dev, dma->sglist, dma->sglen, dma->direction);

	vfree(dma->sglist);
	dma->sglist = NULL;
	dma->sglen = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(videobuf_dma_unmap);

int videobuf_dma_free(struct videobuf_dmabuf *dma)
{
	int i;
	MAGIC_CHECK(dma->magic, MAGIC_DMABUF);
	BUG_ON(dma->sglen);

	if (dma->pages) {
		for (i = 0; i < dma->nr_pages; i++)
			page_cache_release(dma->pages[i]);
		kfree(dma->pages);
		dma->pages = NULL;
	}

	if (dma->dma_addr) {
		for (i = 0; i < dma->nr_pages; i++) {
			void *addr;

			addr = page_address(dma->vaddr_pages[i]);
			dma_free_coherent(dma->dev, PAGE_SIZE, addr,
					  dma->dma_addr[i]);
		}
		kfree(dma->dma_addr);
		dma->dma_addr = NULL;
		kfree(dma->vaddr_pages);
		dma->vaddr_pages = NULL;
		vunmap(dma->vaddr);
		dma->vaddr = NULL;
	}

	if (dma->bus_addr)
		dma->bus_addr = 0;
	dma->direction = DMA_NONE;

	return 0;
}
EXPORT_SYMBOL_GPL(videobuf_dma_free);

/* --------------------------------------------------------------------- */

static void videobuf_vm_open(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;

	dprintk(2, "vm_open %p [count=%d,vma=%08lx-%08lx]\n", map,
		map->count, vma->vm_start, vma->vm_end);

	map->count++;
}

static void videobuf_vm_close(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;
	struct videobuf_queue *q = map->q;
	struct videobuf_dma_sg_memory *mem;
	int i;

	dprintk(2, "vm_close %p [count=%d,vma=%08lx-%08lx]\n", map,
		map->count, vma->vm_start, vma->vm_end);

	map->count--;
	if (0 == map->count) {
		dprintk(1, "munmap %p q=%p\n", map, q);
		videobuf_queue_lock(q);
		for (i = 0; i < VIDEO_MAX_FRAME; i++) {
			if (NULL == q->bufs[i])
				continue;
			mem = q->bufs[i]->priv;
			if (!mem)
				continue;

			MAGIC_CHECK(mem->magic, MAGIC_SG_MEM);

			if (q->bufs[i]->map != map)
				continue;
			q->bufs[i]->map   = NULL;
			q->bufs[i]->baddr = 0;
			q->ops->buf_release(q, q->bufs[i]);
		}
		videobuf_queue_unlock(q);
		kfree(map);
	}
	return;
}

/*
 * Get a anonymous page for the mapping.  Make sure we can DMA to that
 * memory location with 32bit PCI devices (i.e. don't use highmem for
 * now ...).  Bounce buffers don't work very well for the data rates
 * video capture has.
 */
static int videobuf_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page;

	dprintk(3, "fault: fault @ %08lx [vma %08lx-%08lx]\n",
		(unsigned long)vmf->virtual_address,
		vma->vm_start, vma->vm_end);

	page = alloc_page(GFP_USER | __GFP_DMA32);
	if (!page)
		return VM_FAULT_OOM;
	clear_user_highpage(page, (unsigned long)vmf->virtual_address);
	vmf->page = page;

	return 0;
}

static const struct vm_operations_struct videobuf_vm_ops = {
	.open	= videobuf_vm_open,
	.close	= videobuf_vm_close,
	.fault	= videobuf_vm_fault,
};

/* ---------------------------------------------------------------------
 * SG handlers for the generic methods
 */

/* Allocated area consists on 3 parts:
	struct video_buffer
	struct <driver>_buffer (cx88_buffer, saa7134_buf, ...)
	struct videobuf_dma_sg_memory
 */

static struct videobuf_buffer *__videobuf_alloc_vb(size_t size)
{
	struct videobuf_dma_sg_memory *mem;
	struct videobuf_buffer *vb;

	vb = kzalloc(size + sizeof(*mem), GFP_KERNEL);
	if (!vb)
		return vb;

	mem = vb->priv = ((char *)vb) + size;
	mem->magic = MAGIC_SG_MEM;

	videobuf_dma_init(&mem->dma);

	dprintk(1, "%s: allocated at %p(%ld+%ld) & %p(%ld)\n",
		__func__, vb, (long)sizeof(*vb), (long)size - sizeof(*vb),
		mem, (long)sizeof(*mem));

	return vb;
}

static void *__videobuf_to_vaddr(struct videobuf_buffer *buf)
{
	struct videobuf_dma_sg_memory *mem = buf->priv;
	BUG_ON(!mem);

	MAGIC_CHECK(mem->magic, MAGIC_SG_MEM);

	return mem->dma.vaddr;
}

static int __videobuf_iolock(struct videobuf_queue *q,
			     struct videobuf_buffer *vb,
			     struct v4l2_framebuffer *fbuf)
{
	int err, pages;
	dma_addr_t bus;
	struct videobuf_dma_sg_memory *mem = vb->priv;
	BUG_ON(!mem);

	MAGIC_CHECK(mem->magic, MAGIC_SG_MEM);

	if (!mem->dma.dev)
		mem->dma.dev = q->dev;
	else
		WARN_ON(mem->dma.dev != q->dev);

	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_USERPTR:
		if (0 == vb->baddr) {
			/* no userspace addr -- kernel bounce buffer */
			pages = PAGE_ALIGN(vb->size) >> PAGE_SHIFT;
			err = videobuf_dma_init_kernel(&mem->dma,
						       DMA_FROM_DEVICE,
						       pages);
			if (0 != err)
				return err;
		} else if (vb->memory == V4L2_MEMORY_USERPTR) {
			/* dma directly to userspace */
			err = videobuf_dma_init_user(&mem->dma,
						     DMA_FROM_DEVICE,
						     vb->baddr, vb->bsize);
			if (0 != err)
				return err;
		} else {
			/* NOTE: HACK: videobuf_iolock on V4L2_MEMORY_MMAP
			buffers can only be called from videobuf_qbuf
			we take current->mm->mmap_sem there, to prevent
			locking inversion, so don't take it here */

			err = videobuf_dma_init_user_locked(&mem->dma,
						      DMA_FROM_DEVICE,
						      vb->baddr, vb->bsize);
			if (0 != err)
				return err;
		}
		break;
	case V4L2_MEMORY_OVERLAY:
		if (NULL == fbuf)
			return -EINVAL;
		/* FIXME: need sanity checks for vb->boff */
		/*
		 * Using a double cast to avoid compiler warnings when
		 * building for PAE. Compiler doesn't like direct casting
		 * of a 32 bit ptr to 64 bit integer.
		 */
		bus   = (dma_addr_t)(unsigned long)fbuf->base + vb->boff;
		pages = PAGE_ALIGN(vb->size) >> PAGE_SHIFT;
		err = videobuf_dma_init_overlay(&mem->dma, DMA_FROM_DEVICE,
						bus, pages);
		if (0 != err)
			return err;
		break;
	default:
		BUG();
	}
	err = videobuf_dma_map(q->dev, &mem->dma);
	if (0 != err)
		return err;

	return 0;
}

static int __videobuf_sync(struct videobuf_queue *q,
			   struct videobuf_buffer *buf)
{
	struct videobuf_dma_sg_memory *mem = buf->priv;
	BUG_ON(!mem || !mem->dma.sglen);

	MAGIC_CHECK(mem->magic, MAGIC_SG_MEM);
	MAGIC_CHECK(mem->dma.magic, MAGIC_DMABUF);

	dma_sync_sg_for_cpu(q->dev, mem->dma.sglist,
			    mem->dma.sglen, mem->dma.direction);

	return 0;
}

static int __videobuf_mmap_mapper(struct videobuf_queue *q,
				  struct videobuf_buffer *buf,
				  struct vm_area_struct *vma)
{
	struct videobuf_dma_sg_memory *mem = buf->priv;
	struct videobuf_mapping *map;
	unsigned int first, last, size = 0, i;
	int retval;

	retval = -EINVAL;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_SG_MEM);

	/* look for first buffer to map */
	for (first = 0; first < VIDEO_MAX_FRAME; first++) {
		if (buf == q->bufs[first]) {
			size = PAGE_ALIGN(q->bufs[first]->bsize);
			break;
		}
	}

	/* paranoia, should never happen since buf is always valid. */
	if (!size) {
		dprintk(1, "mmap app bug: offset invalid [offset=0x%lx]\n",
				(vma->vm_pgoff << PAGE_SHIFT));
		goto done;
	}

	last = first;

	/* create mapping + update buffer list */
	retval = -ENOMEM;
	map = kmalloc(sizeof(struct videobuf_mapping), GFP_KERNEL);
	if (NULL == map)
		goto done;

	size = 0;
	for (i = first; i <= last; i++) {
		if (NULL == q->bufs[i])
			continue;
		q->bufs[i]->map   = map;
		q->bufs[i]->baddr = vma->vm_start + size;
		size += PAGE_ALIGN(q->bufs[i]->bsize);
	}

	map->count    = 1;
	map->q        = q;
	vma->vm_ops   = &videobuf_vm_ops;
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_flags &= ~VM_IO; /* using shared anonymous pages */
	vma->vm_private_data = map;
	dprintk(1, "mmap %p: q=%p %08lx-%08lx pgoff %08lx bufs %d-%d\n",
		map, q, vma->vm_start, vma->vm_end, vma->vm_pgoff, first, last);
	retval = 0;

done:
	return retval;
}

static struct videobuf_qtype_ops sg_ops = {
	.magic        = MAGIC_QTYPE_OPS,

	.alloc_vb     = __videobuf_alloc_vb,
	.iolock       = __videobuf_iolock,
	.sync         = __videobuf_sync,
	.mmap_mapper  = __videobuf_mmap_mapper,
	.vaddr        = __videobuf_to_vaddr,
};

void *videobuf_sg_alloc(size_t size)
{
	struct videobuf_queue q;

	/* Required to make generic handler to call __videobuf_alloc */
	q.int_ops = &sg_ops;

	q.msize = size;

	return videobuf_alloc_vb(&q);
}
EXPORT_SYMBOL_GPL(videobuf_sg_alloc);

void videobuf_queue_sg_init(struct videobuf_queue *q,
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
				 priv, &sg_ops, ext_lock);
}
EXPORT_SYMBOL_GPL(videobuf_queue_sg_init);

