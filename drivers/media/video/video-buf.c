/*
 *
 * generic helper functions for video4linux capture buffers, to handle
 * memory management and PCI DMA.
 * Right now, bttv, saa7134, saa7146 and cx88 use it.
 *
 * The functions expect the hardware being able to scatter gatter
 * (i.e. the buffers are not linear in physical memory, but fragmented
 * into PAGE_SIZE chunks).  They also assume the driver does not need
 * to touch the video data.
 *
 * device specific map/unmap/sync stuff now are mapped as operations
 * to allow its usage by USB and virtual devices.
 *
 * (c) 2001-2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]
 * (c) 2006 Mauro Carvalho Chehab <mchehab@infradead.org>
 * (c) 2006 Ted Walther and John Sokol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <media/video-buf.h>

#define MAGIC_DMABUF 0x19721112
#define MAGIC_BUFFER 0x20040302
#define MAGIC_CHECK(is,should)	if (unlikely((is) != (should))) \
	{ printk(KERN_ERR "magic mismatch: %x (expected %x)\n",is,should); BUG(); }

static int debug = 0;
module_param(debug, int, 0644);

MODULE_DESCRIPTION("helper module to manage video4linux pci dma buffers");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

#define dprintk(level, fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "vbuf: " fmt , ## arg)

struct scatterlist*
videobuf_vmalloc_to_sg(unsigned char *virt, int nr_pages)
{
	struct scatterlist *sglist;
	struct page *pg;
	int i;

	sglist = kcalloc(nr_pages, sizeof(struct scatterlist), GFP_KERNEL);
	if (NULL == sglist)
		return NULL;
	for (i = 0; i < nr_pages; i++, virt += PAGE_SIZE) {
		pg = vmalloc_to_page(virt);
		if (NULL == pg)
			goto err;
		BUG_ON(PageHighMem(pg));
		sglist[i].page   = pg;
		sglist[i].length = PAGE_SIZE;
	}
	return sglist;

 err:
	kfree(sglist);
	return NULL;
}

struct scatterlist*
videobuf_pages_to_sg(struct page **pages, int nr_pages, int offset)
{
	struct scatterlist *sglist;
	int i = 0;

	if (NULL == pages[0])
		return NULL;
	sglist = kcalloc(nr_pages, sizeof(*sglist), GFP_KERNEL);
	if (NULL == sglist)
		return NULL;

	if (NULL == pages[0])
		goto nopage;
	if (PageHighMem(pages[0]))
		/* DMA to highmem pages might not work */
		goto highmem;
	sglist[0].page   = pages[0];
	sglist[0].offset = offset;
	sglist[0].length = PAGE_SIZE - offset;
	for (i = 1; i < nr_pages; i++) {
		if (NULL == pages[i])
			goto nopage;
		if (PageHighMem(pages[i]))
			goto highmem;
		sglist[i].page   = pages[i];
		sglist[i].length = PAGE_SIZE;
	}
	return sglist;

 nopage:
	dprintk(2,"sgl: oops - no page\n");
	kfree(sglist);
	return NULL;

 highmem:
	dprintk(2,"sgl: oops - highmem page\n");
	kfree(sglist);
	return NULL;
}

/* --------------------------------------------------------------------- */

void videobuf_dma_init(struct videobuf_dmabuf *dma)
{
	memset(dma,0,sizeof(*dma));
	dma->magic = MAGIC_DMABUF;
}

int videobuf_dma_init_user(struct videobuf_dmabuf *dma, int direction,
			   unsigned long data, unsigned long size)
{
	unsigned long first,last;
	int err, rw = 0;

	dma->direction = direction;
	switch (dma->direction) {
	case PCI_DMA_FROMDEVICE: rw = READ;  break;
	case PCI_DMA_TODEVICE:   rw = WRITE; break;
	default:                 BUG();
	}

	first = (data          & PAGE_MASK) >> PAGE_SHIFT;
	last  = ((data+size-1) & PAGE_MASK) >> PAGE_SHIFT;
	dma->offset   = data & ~PAGE_MASK;
	dma->nr_pages = last-first+1;
	dma->pages = kmalloc(dma->nr_pages * sizeof(struct page*),
			     GFP_KERNEL);
	if (NULL == dma->pages)
		return -ENOMEM;
	dprintk(1,"init user [0x%lx+0x%lx => %d pages]\n",
		data,size,dma->nr_pages);

	dma->varea = (void *) data;

	down_read(&current->mm->mmap_sem);
	err = get_user_pages(current,current->mm,
			     data & PAGE_MASK, dma->nr_pages,
			     rw == READ, 1, /* force */
			     dma->pages, NULL);
	up_read(&current->mm->mmap_sem);
	if (err != dma->nr_pages) {
		dma->nr_pages = (err >= 0) ? err : 0;
		dprintk(1,"get_user_pages: err=%d [%d]\n",err,dma->nr_pages);
		return err < 0 ? err : -EINVAL;
	}
	return 0;
}

int videobuf_dma_init_kernel(struct videobuf_dmabuf *dma, int direction,
			     int nr_pages)
{
	dprintk(1,"init kernel [%d pages]\n",nr_pages);
	dma->direction = direction;
	dma->vmalloc = vmalloc_32(nr_pages << PAGE_SHIFT);
	if (NULL == dma->vmalloc) {
		dprintk(1,"vmalloc_32(%d pages) failed\n",nr_pages);
		return -ENOMEM;
	}
	dprintk(1,"vmalloc is at addr 0x%08lx, size=%d\n",
				(unsigned long)dma->vmalloc,
				nr_pages << PAGE_SHIFT);
	memset(dma->vmalloc,0,nr_pages << PAGE_SHIFT);
	dma->nr_pages = nr_pages;
	return 0;
}

int videobuf_dma_init_overlay(struct videobuf_dmabuf *dma, int direction,
			      dma_addr_t addr, int nr_pages)
{
	dprintk(1,"init overlay [%d pages @ bus 0x%lx]\n",
		nr_pages,(unsigned long)addr);
	dma->direction = direction;
	if (0 == addr)
		return -EINVAL;

	dma->bus_addr = addr;
	dma->nr_pages = nr_pages;
	return 0;
}

int videobuf_dma_map(struct videobuf_queue* q,struct videobuf_dmabuf *dma)
{
	void                   *dev=q->dev;

	MAGIC_CHECK(dma->magic,MAGIC_DMABUF);
	BUG_ON(0 == dma->nr_pages);

	if (dma->pages) {
		dma->sglist = videobuf_pages_to_sg(dma->pages, dma->nr_pages,
						   dma->offset);
	}
	if (dma->vmalloc) {
		dma->sglist = videobuf_vmalloc_to_sg
						(dma->vmalloc,dma->nr_pages);
	}
	if (dma->bus_addr) {
		dma->sglist = kmalloc(sizeof(struct scatterlist), GFP_KERNEL);
		if (NULL != dma->sglist) {
			dma->sglen  = 1;
			sg_dma_address(&dma->sglist[0]) = dma->bus_addr & PAGE_MASK;
			dma->sglist[0].offset           = dma->bus_addr & ~PAGE_MASK;
			sg_dma_len(&dma->sglist[0])     = dma->nr_pages * PAGE_SIZE;
		}
	}
	if (NULL == dma->sglist) {
		dprintk(1,"scatterlist is NULL\n");
		return -ENOMEM;
	}
	if (!dma->bus_addr) {
		if (q->ops->vb_map_sg) {
			dma->sglen = q->ops->vb_map_sg(dev,dma->sglist,
					dma->nr_pages, dma->direction);
		}
		if (0 == dma->sglen) {
			printk(KERN_WARNING
			       "%s: videobuf_map_sg failed\n",__FUNCTION__);
			kfree(dma->sglist);
			dma->sglist = NULL;
			dma->sglen = 0;
			return -EIO;
		}
	}
	return 0;
}

int videobuf_dma_sync(struct videobuf_queue* q,struct videobuf_dmabuf *dma)
{
	void                   *dev=q->dev;

	MAGIC_CHECK(dma->magic,MAGIC_DMABUF);
	BUG_ON(!dma->sglen);

	if (!dma->bus_addr && q->ops->vb_dma_sync_sg)
		q->ops->vb_dma_sync_sg(dev,dma->sglist,dma->nr_pages,
							dma->direction);

	return 0;
}

int videobuf_dma_unmap(struct videobuf_queue* q,struct videobuf_dmabuf *dma)
{
	void                   *dev=q->dev;

	MAGIC_CHECK(dma->magic,MAGIC_DMABUF);
	if (!dma->sglen)
		return 0;

	if (!dma->bus_addr && q->ops->vb_unmap_sg)
			q->ops->vb_unmap_sg(dev,dma->sglist,dma->nr_pages,
							dma->direction);
	kfree(dma->sglist);
	dma->sglist = NULL;
	dma->sglen = 0;
	return 0;
}

int videobuf_dma_free(struct videobuf_dmabuf *dma)
{
	MAGIC_CHECK(dma->magic,MAGIC_DMABUF);
	BUG_ON(dma->sglen);

	if (dma->pages) {
		int i;
		for (i=0; i < dma->nr_pages; i++)
			page_cache_release(dma->pages[i]);
		kfree(dma->pages);
		dma->pages = NULL;
	}

	vfree(dma->vmalloc);
	dma->vmalloc = NULL;
	dma->varea = NULL;

	if (dma->bus_addr) {
		dma->bus_addr = 0;
	}
	dma->direction = PCI_DMA_NONE;
	return 0;
}

/* --------------------------------------------------------------------- */

void* videobuf_alloc(unsigned int size)
{
	struct videobuf_buffer *vb;

	vb = kzalloc(size,GFP_KERNEL);
	if (NULL != vb) {
		videobuf_dma_init(&vb->dma);
		init_waitqueue_head(&vb->done);
		vb->magic     = MAGIC_BUFFER;
	}
	return vb;
}

int videobuf_waiton(struct videobuf_buffer *vb, int non_blocking, int intr)
{
	int retval = 0;
	DECLARE_WAITQUEUE(wait, current);

	MAGIC_CHECK(vb->magic,MAGIC_BUFFER);
	add_wait_queue(&vb->done, &wait);
	while (vb->state == STATE_ACTIVE || vb->state == STATE_QUEUED) {
		if (non_blocking) {
			retval = -EAGAIN;
			break;
		}
		set_current_state(intr  ? TASK_INTERRUPTIBLE
					: TASK_UNINTERRUPTIBLE);
		if (vb->state == STATE_ACTIVE || vb->state == STATE_QUEUED)
			schedule();
		set_current_state(TASK_RUNNING);
		if (intr && signal_pending(current)) {
			dprintk(1,"buffer waiton: -EINTR\n");
			retval = -EINTR;
			break;
		}
	}
	remove_wait_queue(&vb->done, &wait);
	return retval;
}

int
videobuf_iolock(struct videobuf_queue* q, struct videobuf_buffer *vb,
		struct v4l2_framebuffer *fbuf)
{
	int err,pages;
	dma_addr_t bus;

	MAGIC_CHECK(vb->magic,MAGIC_BUFFER);
	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_USERPTR:
		if (0 == vb->baddr) {
			/* no userspace addr -- kernel bounce buffer */
			pages = PAGE_ALIGN(vb->size) >> PAGE_SHIFT;
			err = videobuf_dma_init_kernel(&vb->dma,PCI_DMA_FROMDEVICE,
						       pages);
			if (0 != err)
				return err;
		} else {
			/* dma directly to userspace */
			err = videobuf_dma_init_user(&vb->dma,PCI_DMA_FROMDEVICE,
						     vb->baddr,vb->bsize);
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
		err = videobuf_dma_init_overlay(&vb->dma,PCI_DMA_FROMDEVICE,
						bus, pages);
		if (0 != err)
			return err;
		break;
	default:
		BUG();
	}
	err = videobuf_dma_map(q,&vb->dma);
	if (0 != err)
		return err;

	return 0;
}

/* --------------------------------------------------------------------- */

void videobuf_queue_pci(struct videobuf_queue* q)
{
	/* If not specified, defaults to PCI map sg */
	if (!q->ops->vb_map_sg)
		q->ops->vb_map_sg=(vb_map_sg_t *)pci_map_sg;

	if (!q->ops->vb_dma_sync_sg)
		q->ops->vb_dma_sync_sg=(vb_map_sg_t *)pci_dma_sync_sg_for_cpu;
	if (!q->ops->vb_unmap_sg)
		q->ops->vb_unmap_sg=(vb_map_sg_t *)pci_unmap_sg;
}

int videobuf_pci_dma_map(struct pci_dev *pci,struct videobuf_dmabuf *dma)
{
	struct videobuf_queue q;
	struct videobuf_queue_ops qops;

	q.dev=pci;
	qops.vb_map_sg=(vb_map_sg_t *)pci_map_sg;
	qops.vb_unmap_sg=(vb_map_sg_t *)pci_unmap_sg;
	q.ops = &qops;

	return (videobuf_dma_map(&q,dma));
}

int videobuf_pci_dma_unmap(struct pci_dev *pci,struct videobuf_dmabuf *dma)
{
	struct videobuf_queue q;
	struct videobuf_queue_ops qops;

	q.dev=pci;
	qops.vb_map_sg=(vb_map_sg_t *)pci_map_sg;
	qops.vb_unmap_sg=(vb_map_sg_t *)pci_unmap_sg;
	q.ops = &qops;

	return (videobuf_dma_unmap(&q,dma));
}

void videobuf_queue_init(struct videobuf_queue* q,
			 struct videobuf_queue_ops *ops,
			 void *dev,
			 spinlock_t *irqlock,
			 enum v4l2_buf_type type,
			 enum v4l2_field field,
			 unsigned int msize,
			 void *priv)
{
	memset(q,0,sizeof(*q));
	q->irqlock = irqlock;
	q->dev     = dev;
	q->type    = type;
	q->field   = field;
	q->msize   = msize;
	q->ops     = ops;
	q->priv_data = priv;

	videobuf_queue_pci(q);

	mutex_init(&q->lock);
	INIT_LIST_HEAD(&q->stream);
}

int
videobuf_queue_is_busy(struct videobuf_queue *q)
{
	int i;

	if (q->streaming) {
		dprintk(1,"busy: streaming active\n");
		return 1;
	}
	if (q->reading) {
		dprintk(1,"busy: pending read #1\n");
		return 1;
	}
	if (q->read_buf) {
		dprintk(1,"busy: pending read #2\n");
		return 1;
	}
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		if (q->bufs[i]->map) {
			dprintk(1,"busy: buffer #%d mapped\n",i);
			return 1;
		}
		if (q->bufs[i]->state == STATE_QUEUED) {
			dprintk(1,"busy: buffer #%d queued\n",i);
			return 1;
		}
		if (q->bufs[i]->state == STATE_ACTIVE) {
			dprintk(1,"busy: buffer #%d avtive\n",i);
			return 1;
		}
	}
	return 0;
}

void
videobuf_queue_cancel(struct videobuf_queue *q)
{
	unsigned long flags=0;
	int i;

	/* remove queued buffers from list */
	if (q->irqlock)
		spin_lock_irqsave(q->irqlock,flags);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		if (q->bufs[i]->state == STATE_QUEUED) {
			list_del(&q->bufs[i]->queue);
			q->bufs[i]->state = STATE_ERROR;
		}
	}
	if (q->irqlock)
		spin_unlock_irqrestore(q->irqlock,flags);

	/* free all buffers + clear queue */
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		q->ops->buf_release(q,q->bufs[i]);
	}
	INIT_LIST_HEAD(&q->stream);
}

/* --------------------------------------------------------------------- */

enum v4l2_field
videobuf_next_field(struct videobuf_queue *q)
{
	enum v4l2_field field = q->field;

	BUG_ON(V4L2_FIELD_ANY == field);

	if (V4L2_FIELD_ALTERNATE == field) {
		if (V4L2_FIELD_TOP == q->last) {
			field   = V4L2_FIELD_BOTTOM;
			q->last = V4L2_FIELD_BOTTOM;
		} else {
			field   = V4L2_FIELD_TOP;
			q->last = V4L2_FIELD_TOP;
		}
	}
	return field;
}

void
videobuf_status(struct v4l2_buffer *b, struct videobuf_buffer *vb,
		enum v4l2_buf_type type)
{
	MAGIC_CHECK(vb->magic,MAGIC_BUFFER);

	b->index    = vb->i;
	b->type     = type;

	b->memory   = vb->memory;
	switch (b->memory) {
	case V4L2_MEMORY_MMAP:
		b->m.offset  = vb->boff;
		b->length    = vb->bsize;
		break;
	case V4L2_MEMORY_USERPTR:
		b->m.userptr = vb->baddr;
		b->length    = vb->bsize;
		break;
	case V4L2_MEMORY_OVERLAY:
		b->m.offset  = vb->boff;
		break;
	}

	b->flags    = 0;
	if (vb->map)
		b->flags |= V4L2_BUF_FLAG_MAPPED;

	switch (vb->state) {
	case STATE_PREPARED:
	case STATE_QUEUED:
	case STATE_ACTIVE:
		b->flags |= V4L2_BUF_FLAG_QUEUED;
		break;
	case STATE_DONE:
	case STATE_ERROR:
		b->flags |= V4L2_BUF_FLAG_DONE;
		break;
	case STATE_NEEDS_INIT:
	case STATE_IDLE:
		/* nothing */
		break;
	}

	if (vb->input != UNSET) {
		b->flags |= V4L2_BUF_FLAG_INPUT;
		b->input  = vb->input;
	}

	b->field     = vb->field;
	b->timestamp = vb->ts;
	b->bytesused = vb->size;
	b->sequence  = vb->field_count >> 1;
}

int
videobuf_reqbufs(struct videobuf_queue *q,
		 struct v4l2_requestbuffers *req)
{
	unsigned int size,count;
	int retval;

	if (req->type != q->type) {
		dprintk(1,"reqbufs: queue type invalid\n");
		return -EINVAL;
	}
	if (req->count < 1) {
		dprintk(1,"reqbufs: count invalid (%d)\n",req->count);
		return -EINVAL;
	}
	if (req->memory != V4L2_MEMORY_MMAP     &&
	    req->memory != V4L2_MEMORY_USERPTR  &&
	    req->memory != V4L2_MEMORY_OVERLAY) {
		dprintk(1,"reqbufs: memory type invalid\n");
		return -EINVAL;
	}

	if (q->streaming) {
		dprintk(1,"reqbufs: streaming already exists\n");
		return -EBUSY;
	}
	if (!list_empty(&q->stream)) {
		dprintk(1,"reqbufs: stream running\n");
		return -EBUSY;
	}

	mutex_lock(&q->lock);
	count = req->count;
	if (count > VIDEO_MAX_FRAME)
		count = VIDEO_MAX_FRAME;
	size = 0;
	q->ops->buf_setup(q,&count,&size);
	size = PAGE_ALIGN(size);
	dprintk(1,"reqbufs: bufs=%d, size=0x%x [%d pages total]\n",
		count, size, (count*size)>>PAGE_SHIFT);

	retval = videobuf_mmap_setup(q,count,size,req->memory);
	if (retval < 0) {
		dprintk(1,"reqbufs: mmap setup returned %d\n",retval);
		goto done;
	}

	req->count = count;

 done:
	mutex_unlock(&q->lock);
	return retval;
}

int
videobuf_querybuf(struct videobuf_queue *q, struct v4l2_buffer *b)
{
	if (unlikely(b->type != q->type)) {
		dprintk(1,"querybuf: Wrong type.\n");
		return -EINVAL;
	}
	if (unlikely(b->index < 0 || b->index >= VIDEO_MAX_FRAME)) {
		dprintk(1,"querybuf: index out of range.\n");
		return -EINVAL;
	}
	if (unlikely(NULL == q->bufs[b->index])) {
		dprintk(1,"querybuf: buffer is null.\n");
		return -EINVAL;
	}
	videobuf_status(b,q->bufs[b->index],q->type);
	return 0;
}

int
videobuf_qbuf(struct videobuf_queue *q,
	      struct v4l2_buffer *b)
{
	struct videobuf_buffer *buf;
	enum v4l2_field field;
	unsigned long flags=0;
	int retval;

	mutex_lock(&q->lock);
	retval = -EBUSY;
	if (q->reading) {
		dprintk(1,"qbuf: Reading running...\n");
		goto done;
	}
	retval = -EINVAL;
	if (b->type != q->type) {
		dprintk(1,"qbuf: Wrong type.\n");
		goto done;
	}
	if (b->index < 0 || b->index >= VIDEO_MAX_FRAME) {
		dprintk(1,"qbuf: index out of range.\n");
		goto done;
	}
	buf = q->bufs[b->index];
	if (NULL == buf) {
		dprintk(1,"qbuf: buffer is null.\n");
		goto done;
	}
	MAGIC_CHECK(buf->magic,MAGIC_BUFFER);
	if (buf->memory != b->memory) {
		dprintk(1,"qbuf: memory type is wrong.\n");
		goto done;
	}
	if (buf->state != STATE_NEEDS_INIT && buf->state != STATE_IDLE) {
		dprintk(1,"qbuf: buffer is already queued or active.\n");
		goto done;
	}

	if (b->flags & V4L2_BUF_FLAG_INPUT) {
		if (b->input >= q->inputs) {
			dprintk(1,"qbuf: wrong input.\n");
			goto done;
		}
		buf->input = b->input;
	} else {
		buf->input = UNSET;
	}

	switch (b->memory) {
	case V4L2_MEMORY_MMAP:
		if (0 == buf->baddr) {
			dprintk(1,"qbuf: mmap requested but buffer addr is zero!\n");
			goto done;
		}
		break;
	case V4L2_MEMORY_USERPTR:
		if (b->length < buf->bsize) {
			dprintk(1,"qbuf: buffer length is not enough\n");
			goto done;
		}
		if (STATE_NEEDS_INIT != buf->state && buf->baddr != b->m.userptr)
			q->ops->buf_release(q,buf);
		buf->baddr = b->m.userptr;
		break;
	case V4L2_MEMORY_OVERLAY:
		buf->boff = b->m.offset;
		break;
	default:
		dprintk(1,"qbuf: wrong memory type\n");
		goto done;
	}

	dprintk(1,"qbuf: requesting next field\n");
	field = videobuf_next_field(q);
	retval = q->ops->buf_prepare(q,buf,field);
	if (0 != retval) {
		dprintk(1,"qbuf: buffer_prepare returned %d\n",retval);
		goto done;
	}

	list_add_tail(&buf->stream,&q->stream);
	if (q->streaming) {
		if (q->irqlock)
			spin_lock_irqsave(q->irqlock,flags);
		q->ops->buf_queue(q,buf);
		if (q->irqlock)
			spin_unlock_irqrestore(q->irqlock,flags);
	}
	dprintk(1,"qbuf: succeded\n");
	retval = 0;

 done:
	mutex_unlock(&q->lock);
	return retval;
}

int
videobuf_dqbuf(struct videobuf_queue *q,
	       struct v4l2_buffer *b, int nonblocking)
{
	struct videobuf_buffer *buf;
	int retval;

	mutex_lock(&q->lock);
	retval = -EBUSY;
	if (q->reading) {
		dprintk(1,"dqbuf: Reading running...\n");
		goto done;
	}
	retval = -EINVAL;
	if (b->type != q->type) {
		dprintk(1,"dqbuf: Wrong type.\n");
		goto done;
	}
	if (list_empty(&q->stream)) {
		dprintk(1,"dqbuf: stream running\n");
		goto done;
	}
	buf = list_entry(q->stream.next, struct videobuf_buffer, stream);
	retval = videobuf_waiton(buf, nonblocking, 1);
	if (retval < 0) {
		dprintk(1,"dqbuf: waiton returned %d\n",retval);
		goto done;
	}
	switch (buf->state) {
	case STATE_ERROR:
		dprintk(1,"dqbuf: state is error\n");
		retval = -EIO;
		videobuf_dma_sync(q,&buf->dma);
		buf->state = STATE_IDLE;
		break;
	case STATE_DONE:
		dprintk(1,"dqbuf: state is done\n");
		videobuf_dma_sync(q,&buf->dma);
		buf->state = STATE_IDLE;
		break;
	default:
		dprintk(1,"dqbuf: state invalid\n");
		retval = -EINVAL;
		goto done;
	}
	list_del(&buf->stream);
	memset(b,0,sizeof(*b));
	videobuf_status(b,buf,q->type);

 done:
	mutex_unlock(&q->lock);
	return retval;
}

int videobuf_streamon(struct videobuf_queue *q)
{
	struct videobuf_buffer *buf;
	struct list_head *list;
	unsigned long flags=0;
	int retval;

	mutex_lock(&q->lock);
	retval = -EBUSY;
	if (q->reading)
		goto done;
	retval = 0;
	if (q->streaming)
		goto done;
	q->streaming = 1;
	if (q->irqlock)
		spin_lock_irqsave(q->irqlock,flags);
	list_for_each(list,&q->stream) {
		buf = list_entry(list, struct videobuf_buffer, stream);
		if (buf->state == STATE_PREPARED)
			q->ops->buf_queue(q,buf);
	}
	if (q->irqlock)
		spin_unlock_irqrestore(q->irqlock,flags);

 done:
	mutex_unlock(&q->lock);
	return retval;
}

int videobuf_streamoff(struct videobuf_queue *q)
{
	int retval = -EINVAL;

	mutex_lock(&q->lock);
	if (!q->streaming)
		goto done;
	videobuf_queue_cancel(q);
	q->streaming = 0;
	retval = 0;

 done:
	mutex_unlock(&q->lock);
	return retval;
}

static ssize_t
videobuf_read_zerocopy(struct videobuf_queue *q, char __user *data,
		       size_t count, loff_t *ppos)
{
	enum v4l2_field field;
	unsigned long flags=0;
	int retval;

	/* setup stuff */
	q->read_buf = videobuf_alloc(q->msize);
	if (NULL == q->read_buf)
		return -ENOMEM;

	q->read_buf->memory = V4L2_MEMORY_USERPTR;
	q->read_buf->baddr  = (unsigned long)data;
	q->read_buf->bsize  = count;
	field = videobuf_next_field(q);
	retval = q->ops->buf_prepare(q,q->read_buf,field);
	if (0 != retval)
		goto done;

	/* start capture & wait */
	if (q->irqlock)
		spin_lock_irqsave(q->irqlock,flags);
	q->ops->buf_queue(q,q->read_buf);
	if (q->irqlock)
		spin_unlock_irqrestore(q->irqlock,flags);
	retval = videobuf_waiton(q->read_buf,0,0);
	if (0 == retval) {
		videobuf_dma_sync(q,&q->read_buf->dma);
		if (STATE_ERROR == q->read_buf->state)
			retval = -EIO;
		else
			retval = q->read_buf->size;
	}

 done:
	/* cleanup */
	q->ops->buf_release(q,q->read_buf);
	kfree(q->read_buf);
	q->read_buf = NULL;
	return retval;
}

ssize_t videobuf_read_one(struct videobuf_queue *q,
			  char __user *data, size_t count, loff_t *ppos,
			  int nonblocking)
{
	enum v4l2_field field;
	unsigned long flags=0;
	unsigned size, nbufs, bytes;
	int retval;

	mutex_lock(&q->lock);

	nbufs = 1; size = 0;
	q->ops->buf_setup(q,&nbufs,&size);
	if (NULL == q->read_buf  &&
	    count >= size        &&
	    !nonblocking) {
		retval = videobuf_read_zerocopy(q,data,count,ppos);
		if (retval >= 0  ||  retval == -EIO)
			/* ok, all done */
			goto done;
		/* fallback to kernel bounce buffer on failures */
	}

	if (NULL == q->read_buf) {
		/* need to capture a new frame */
		retval = -ENOMEM;
		q->read_buf = videobuf_alloc(q->msize);
		dprintk(1,"video alloc=0x%p\n", q->read_buf);
		if (NULL == q->read_buf)
			goto done;
		q->read_buf->memory = V4L2_MEMORY_USERPTR;
		q->read_buf->bsize = count; /* preferred size */
		field = videobuf_next_field(q);
		retval = q->ops->buf_prepare(q,q->read_buf,field);
		if (0 != retval) {
			kfree (q->read_buf);
			q->read_buf = NULL;
			goto done;
		}
		if (q->irqlock)
			spin_lock_irqsave(q->irqlock,flags);
		q->ops->buf_queue(q,q->read_buf);
		if (q->irqlock)
			spin_unlock_irqrestore(q->irqlock,flags);
		q->read_off = 0;
	}

	/* wait until capture is done */
	retval = videobuf_waiton(q->read_buf, nonblocking, 1);
	if (0 != retval)
		goto done;
	videobuf_dma_sync(q,&q->read_buf->dma);

	if (STATE_ERROR == q->read_buf->state) {
		/* catch I/O errors */
		q->ops->buf_release(q,q->read_buf);
		kfree(q->read_buf);
		q->read_buf = NULL;
		retval = -EIO;
		goto done;
	}

	/* copy to userspace */
	bytes = count;
	if (bytes > q->read_buf->size - q->read_off)
		bytes = q->read_buf->size - q->read_off;
	retval = -EFAULT;
	if (copy_to_user(data, q->read_buf->dma.vmalloc+q->read_off, bytes))
		goto done;

	retval = bytes;
	q->read_off += bytes;
	if (q->read_off == q->read_buf->size) {
		/* all data copied, cleanup */
		q->ops->buf_release(q,q->read_buf);
		kfree(q->read_buf);
		q->read_buf = NULL;
	}

 done:
	mutex_unlock(&q->lock);
	return retval;
}

int videobuf_read_start(struct videobuf_queue *q)
{
	enum v4l2_field field;
	unsigned long flags=0;
	int count = 0, size = 0;
	int err, i;

	q->ops->buf_setup(q,&count,&size);
	if (count < 2)
		count = 2;
	if (count > VIDEO_MAX_FRAME)
		count = VIDEO_MAX_FRAME;
	size = PAGE_ALIGN(size);

	err = videobuf_mmap_setup(q, count, size, V4L2_MEMORY_USERPTR);
	if (err)
		return err;
	for (i = 0; i < count; i++) {
		field = videobuf_next_field(q);
		err = q->ops->buf_prepare(q,q->bufs[i],field);
		if (err)
			return err;
		list_add_tail(&q->bufs[i]->stream, &q->stream);
	}
	if (q->irqlock)
		spin_lock_irqsave(q->irqlock,flags);
	for (i = 0; i < count; i++)
		q->ops->buf_queue(q,q->bufs[i]);
	if (q->irqlock)
		spin_unlock_irqrestore(q->irqlock,flags);
	q->reading = 1;
	return 0;
}

void videobuf_read_stop(struct videobuf_queue *q)
{
	int i;

	videobuf_queue_cancel(q);
	videobuf_mmap_free(q);
	INIT_LIST_HEAD(&q->stream);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		kfree(q->bufs[i]);
		q->bufs[i] = NULL;
	}
	q->read_buf = NULL;
	q->reading  = 0;
}

ssize_t videobuf_read_stream(struct videobuf_queue *q,
			     char __user *data, size_t count, loff_t *ppos,
			     int vbihack, int nonblocking)
{
	unsigned int *fc, bytes;
	int err, retval;
	unsigned long flags=0;

	dprintk(2,"%s\n",__FUNCTION__);
	mutex_lock(&q->lock);
	retval = -EBUSY;
	if (q->streaming)
		goto done;
	if (!q->reading) {
		retval = videobuf_read_start(q);
		if (retval < 0)
			goto done;
	}

	retval = 0;
	while (count > 0) {
		/* get / wait for data */
		if (NULL == q->read_buf) {
			q->read_buf = list_entry(q->stream.next,
						 struct videobuf_buffer,
						 stream);
			list_del(&q->read_buf->stream);
			q->read_off = 0;
		}
		err = videobuf_waiton(q->read_buf, nonblocking, 1);
		if (err < 0) {
			if (0 == retval)
				retval = err;
			break;
		}

		if (q->read_buf->state == STATE_DONE) {
			if (vbihack) {
				/* dirty, undocumented hack -- pass the frame counter
				 * within the last four bytes of each vbi data block.
				 * We need that one to maintain backward compatibility
				 * to all vbi decoding software out there ... */
				fc  = (unsigned int*)q->read_buf->dma.vmalloc;
				fc += (q->read_buf->size>>2) -1;
				*fc = q->read_buf->field_count >> 1;
				dprintk(1,"vbihack: %d\n",*fc);
			}

			/* copy stuff */
			bytes = count;
			if (bytes > q->read_buf->size - q->read_off)
				bytes = q->read_buf->size - q->read_off;
			if (copy_to_user(data + retval,
					 q->read_buf->dma.vmalloc + q->read_off,
					 bytes)) {
				if (0 == retval)
					retval = -EFAULT;
				break;
			}
			count       -= bytes;
			retval      += bytes;
			q->read_off += bytes;
		} else {
			/* some error */
			q->read_off = q->read_buf->size;
			if (0 == retval)
				retval = -EIO;
		}

		/* requeue buffer when done with copying */
		if (q->read_off == q->read_buf->size) {
			list_add_tail(&q->read_buf->stream,
				      &q->stream);
			if (q->irqlock)
				spin_lock_irqsave(q->irqlock,flags);
			q->ops->buf_queue(q,q->read_buf);
			if (q->irqlock)
				spin_unlock_irqrestore(q->irqlock,flags);
			q->read_buf = NULL;
		}
		if (retval < 0)
			break;
	}

 done:
	mutex_unlock(&q->lock);
	return retval;
}

unsigned int videobuf_poll_stream(struct file *file,
				  struct videobuf_queue *q,
				  poll_table *wait)
{
	struct videobuf_buffer *buf = NULL;
	unsigned int rc = 0;

	mutex_lock(&q->lock);
	if (q->streaming) {
		if (!list_empty(&q->stream))
			buf = list_entry(q->stream.next,
					 struct videobuf_buffer, stream);
	} else {
		if (!q->reading)
			videobuf_read_start(q);
		if (!q->reading) {
			rc = POLLERR;
		} else if (NULL == q->read_buf) {
			q->read_buf = list_entry(q->stream.next,
						 struct videobuf_buffer,
						 stream);
			list_del(&q->read_buf->stream);
			q->read_off = 0;
		}
		buf = q->read_buf;
	}
	if (!buf)
		rc = POLLERR;

	if (0 == rc) {
		poll_wait(file, &buf->done, wait);
		if (buf->state == STATE_DONE ||
		    buf->state == STATE_ERROR)
			rc = POLLIN|POLLRDNORM;
	}
	mutex_unlock(&q->lock);
	return rc;
}

/* --------------------------------------------------------------------- */

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
			if (q->bufs[i])
				;
			if (q->bufs[i]->map != map)
				continue;
			q->bufs[i]->map   = NULL;
			q->bufs[i]->baddr = 0;
			q->ops->buf_release(q,q->bufs[i]);
		}
		mutex_unlock(&q->lock);
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
static struct page*
videobuf_vm_nopage(struct vm_area_struct *vma, unsigned long vaddr,
		   int *type)
{
	struct page *page;

	dprintk(3,"nopage: fault @ %08lx [vma %08lx-%08lx]\n",
		vaddr,vma->vm_start,vma->vm_end);
	if (vaddr > vma->vm_end)
		return NOPAGE_SIGBUS;
	page = alloc_page(GFP_USER | __GFP_DMA32);
	if (!page)
		return NOPAGE_OOM;
	clear_user_page(page_address(page), vaddr, page);
	if (type)
		*type = VM_FAULT_MINOR;
	return page;
}

static struct vm_operations_struct videobuf_vm_ops =
{
	.open     = videobuf_vm_open,
	.close    = videobuf_vm_close,
	.nopage   = videobuf_vm_nopage,
};

int videobuf_mmap_setup(struct videobuf_queue *q,
			unsigned int bcount, unsigned int bsize,
			enum v4l2_memory memory)
{
	unsigned int i;
	int err;

	err = videobuf_mmap_free(q);
	if (0 != err)
		return err;

	for (i = 0; i < bcount; i++) {
		q->bufs[i] = videobuf_alloc(q->msize);
		q->bufs[i]->i      = i;
		q->bufs[i]->input  = UNSET;
		q->bufs[i]->memory = memory;
		q->bufs[i]->bsize  = bsize;
		switch (memory) {
		case V4L2_MEMORY_MMAP:
			q->bufs[i]->boff  = bsize * i;
			break;
		case V4L2_MEMORY_USERPTR:
		case V4L2_MEMORY_OVERLAY:
			/* nothing */
			break;
		}
	}
	dprintk(1,"mmap setup: %d buffers, %d bytes each\n",
		bcount,bsize);
	return 0;
}

int videobuf_mmap_free(struct videobuf_queue *q)
{
	int i;

	for (i = 0; i < VIDEO_MAX_FRAME; i++)
		if (q->bufs[i] && q->bufs[i]->map)
			return -EBUSY;
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		q->ops->buf_release(q,q->bufs[i]);
		kfree(q->bufs[i]);
		q->bufs[i] = NULL;
	}
	return 0;
}

int videobuf_mmap_mapper(struct videobuf_queue *q,
			 struct vm_area_struct *vma)
{
	struct videobuf_mapping *map;
	unsigned int first,last,size,i;
	int retval;

	mutex_lock(&q->lock);
	retval = -EINVAL;
	if (!(vma->vm_flags & VM_WRITE)) {
		dprintk(1,"mmap app bug: PROT_WRITE please\n");
		goto done;
	}
	if (!(vma->vm_flags & VM_SHARED)) {
		dprintk(1,"mmap app bug: MAP_SHARED please\n");
		goto done;
	}

	/* look for first buffer to map */
	for (first = 0; first < VIDEO_MAX_FRAME; first++) {
		if (NULL == q->bufs[first])
			continue;
		if (V4L2_MEMORY_MMAP != q->bufs[first]->memory)
			continue;
		if (q->bufs[first]->boff == (vma->vm_pgoff << PAGE_SHIFT))
			break;
	}
	if (VIDEO_MAX_FRAME == first) {
		dprintk(1,"mmap app bug: offset invalid [offset=0x%lx]\n",
			(vma->vm_pgoff << PAGE_SHIFT));
		goto done;
	}

	/* look for last buffer to map */
	for (size = 0, last = first; last < VIDEO_MAX_FRAME; last++) {
		if (NULL == q->bufs[last])
			continue;
		if (V4L2_MEMORY_MMAP != q->bufs[last]->memory)
			continue;
		if (q->bufs[last]->map) {
			retval = -EBUSY;
			goto done;
		}
		size += q->bufs[last]->bsize;
		if (size == (vma->vm_end - vma->vm_start))
			break;
	}
	if (VIDEO_MAX_FRAME == last) {
		dprintk(1,"mmap app bug: size invalid [size=0x%lx]\n",
			(vma->vm_end - vma->vm_start));
		goto done;
	}

	/* create mapping + update buffer list */
	retval = -ENOMEM;
	map = kmalloc(sizeof(struct videobuf_mapping),GFP_KERNEL);
	if (NULL == map)
		goto done;
	for (size = 0, i = first; i <= last; size += q->bufs[i++]->bsize) {
		q->bufs[i]->map   = map;
		q->bufs[i]->baddr = vma->vm_start + size;
	}
	map->count    = 1;
	map->start    = vma->vm_start;
	map->end      = vma->vm_end;
	map->q        = q;
	vma->vm_ops   = &videobuf_vm_ops;
	vma->vm_flags |= VM_DONTEXPAND | VM_RESERVED;
	vma->vm_flags &= ~VM_IO; /* using shared anonymous pages */
	vma->vm_private_data = map;
	dprintk(1,"mmap %p: q=%p %08lx-%08lx pgoff %08lx bufs %d-%d\n",
		map,q,vma->vm_start,vma->vm_end,vma->vm_pgoff,first,last);
	retval = 0;

 done:
	mutex_unlock(&q->lock);
	return retval;
}

/* --------------------------------------------------------------------- */

EXPORT_SYMBOL_GPL(videobuf_vmalloc_to_sg);

EXPORT_SYMBOL_GPL(videobuf_dma_init);
EXPORT_SYMBOL_GPL(videobuf_dma_init_user);
EXPORT_SYMBOL_GPL(videobuf_dma_init_kernel);
EXPORT_SYMBOL_GPL(videobuf_dma_init_overlay);
EXPORT_SYMBOL_GPL(videobuf_dma_map);
EXPORT_SYMBOL_GPL(videobuf_dma_sync);
EXPORT_SYMBOL_GPL(videobuf_dma_unmap);
EXPORT_SYMBOL_GPL(videobuf_dma_free);

EXPORT_SYMBOL_GPL(videobuf_pci_dma_map);
EXPORT_SYMBOL_GPL(videobuf_pci_dma_unmap);

EXPORT_SYMBOL_GPL(videobuf_alloc);
EXPORT_SYMBOL_GPL(videobuf_waiton);
EXPORT_SYMBOL_GPL(videobuf_iolock);

EXPORT_SYMBOL_GPL(videobuf_queue_init);
EXPORT_SYMBOL_GPL(videobuf_queue_cancel);
EXPORT_SYMBOL_GPL(videobuf_queue_is_busy);

EXPORT_SYMBOL_GPL(videobuf_next_field);
EXPORT_SYMBOL_GPL(videobuf_status);
EXPORT_SYMBOL_GPL(videobuf_reqbufs);
EXPORT_SYMBOL_GPL(videobuf_querybuf);
EXPORT_SYMBOL_GPL(videobuf_qbuf);
EXPORT_SYMBOL_GPL(videobuf_dqbuf);
EXPORT_SYMBOL_GPL(videobuf_streamon);
EXPORT_SYMBOL_GPL(videobuf_streamoff);

EXPORT_SYMBOL_GPL(videobuf_read_start);
EXPORT_SYMBOL_GPL(videobuf_read_stop);
EXPORT_SYMBOL_GPL(videobuf_read_stream);
EXPORT_SYMBOL_GPL(videobuf_read_one);
EXPORT_SYMBOL_GPL(videobuf_poll_stream);

EXPORT_SYMBOL_GPL(videobuf_mmap_setup);
EXPORT_SYMBOL_GPL(videobuf_mmap_free);
EXPORT_SYMBOL_GPL(videobuf_mmap_mapper);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
