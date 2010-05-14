/***************************************************************************
 *   Copyright (C) 2006-2010 by Marin Mitov                                *
 *   mitov@issp.bas.bg                                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <media/v4l2-dev.h>
#include <media/videobuf-core.h>
#include <media/v4l2-ioctl.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/stringify.h>
#include <linux/delay.h>
#include <media/videobuf-dma-contig.h>
#include <linux/kthread.h>

#include "dt3155v4l.h"
#include "dt3155-bufs.h"

#define DT3155_VENDOR_ID 0x8086
#define DT3155_DEVICE_ID 0x1223

/*  global initializers (for all boards)  */
#ifdef CONFIG_DT3155_CCIR
static const u8 csr2_init = VT_50HZ;
#define DT3155_CURRENT_NORM V4L2_STD_625_50
static const unsigned int img_width = 768;
static const unsigned int img_height = 576;
static const unsigned int frames_per_sec = 25;
static const struct v4l2_fmtdesc frame_std[] = {
	{
	.index = 0,
	.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	.flags = 0,
	.description = "CCIR/50Hz 8 bits gray",
	.pixelformat = V4L2_PIX_FMT_GREY,
	},
};
#else
static const u8 csr2_init = VT_60HZ;
#define DT3155_CURRENT_NORM V4L2_STD_525_60
static const unsigned int img_width = 640;
static const unsigned int img_height = 480;
static const unsigned int frames_per_sec = 30;
static const struct v4l2_fmtdesc frame_std[] = {
	{
	.index = 0,
	.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	.flags = 0,
	.description = "RS-170/60Hz 8 bits gray",
	.pixelformat = V4L2_PIX_FMT_GREY,
	},
};
#endif

#define NUM_OF_FORMATS ARRAY_SIZE(frame_std)

static u8 config_init = ACQ_MODE_EVEN;

/**
 * read_i2c_reg - reads an internal i2c register
 *
 * @addr:	dt3155 mmio base address
 * @index:	index (internal address) of register to read
 * @data:	pointer to byte the read data will be placed in
 *
 * returns:	zero on success or error code
 *
 * This function starts reading the specified (by index) register
 * and busy waits for the process to finish. The result is placed
 * in a byte pointed by data.
 */
static int
read_i2c_reg(void __iomem *addr, u8 index, u8 *data)
{
	u32 tmp = index;

	iowrite32((tmp<<17) | IIC_READ, addr + IIC_CSR2);
	mmiowb();
	udelay(45); /* wait at least 43 usec for NEW_CYCLE to clear */
	if (ioread32(addr + IIC_CSR2) & NEW_CYCLE) {
		/* error: NEW_CYCLE not cleared */
		printk(KERN_ERR "dt3155: NEW_CYCLE not cleared\n");
		return -EIO;
	}
	tmp = ioread32(addr + IIC_CSR1);
	if (tmp & DIRECT_ABORT) {
		/* error: DIRECT_ABORT set */
		printk(KERN_ERR "dt3155: DIRECT_ABORT set\n");
		/* reset DIRECT_ABORT bit */
		iowrite32(DIRECT_ABORT, addr + IIC_CSR1);
		return -EIO;
	}
	*data = tmp>>24;
	return 0;
}

/**
 * write_i2c_reg - writes to an internal i2c register
 *
 * @addr:	dt3155 mmio base address
 * @index:	index (internal address) of register to read
 * @data:	data to be written
 *
 * returns:	zero on success or error code
 *
 * This function starts writting the specified (by index) register
 * and busy waits for the process to finish.
 */
static int
write_i2c_reg(void __iomem *addr, u8 index, u8 data)
{
	u32 tmp = index;

	iowrite32((tmp<<17) | IIC_WRITE | data, addr + IIC_CSR2);
	mmiowb();
	udelay(65); /* wait at least 63 usec for NEW_CYCLE to clear */
	if (ioread32(addr + IIC_CSR2) & NEW_CYCLE) {
		/* error: NEW_CYCLE not cleared */
		printk(KERN_ERR "dt3155: NEW_CYCLE not cleared\n");
		return -EIO;
	}
	if (ioread32(addr + IIC_CSR1) & DIRECT_ABORT) {
		/* error: DIRECT_ABORT set */
		printk(KERN_ERR "dt3155: DIRECT_ABORT set\n");
		/* reset DIRECT_ABORT bit */
		iowrite32(DIRECT_ABORT, addr + IIC_CSR1);
		return -EIO;
	}
	return 0;
}

/**
 * write_i2c_reg_nowait - writes to an internal i2c register
 *
 * @addr:	dt3155 mmio base address
 * @index:	index (internal address) of register to read
 * @data:	data to be written
 *
 * This function starts writting the specified (by index) register
 * and then returns.
 */
static void write_i2c_reg_nowait(void __iomem *addr, u8 index, u8 data)
{
	u32 tmp = index;

	iowrite32((tmp<<17) | IIC_WRITE | data, addr + IIC_CSR2);
	mmiowb();
}

/**
 * wait_i2c_reg - waits the read/write to finish
 *
 * @addr:	dt3155 mmio base address
 *
 * returns:	zero on success or error code
 *
 * This function waits reading/writting to finish.
 */
static int wait_i2c_reg(void __iomem *addr)
{
	if (ioread32(addr + IIC_CSR2) & NEW_CYCLE)
		udelay(65); /* wait at least 63 usec for NEW_CYCLE to clear */
	if (ioread32(addr + IIC_CSR2) & NEW_CYCLE) {
		/* error: NEW_CYCLE not cleared */
		printk(KERN_ERR "dt3155: NEW_CYCLE not cleared\n");
		return -EIO;
	}
	if (ioread32(addr + IIC_CSR1) & DIRECT_ABORT) {
		/* error: DIRECT_ABORT set */
		printk(KERN_ERR "dt3155: DIRECT_ABORT set\n");
		/* reset DIRECT_ABORT bit */
		iowrite32(DIRECT_ABORT, addr + IIC_CSR1);
		return -EIO;
	}
	return 0;
}

/*
 * global pointers to a list of 4MB chunks reserved at driver
 * load, broken down to contiguous buffers of 768 * 576 bytes
 * each to form a pool of buffers for allocations
 * FIXME: add spinlock to protect moves between alloc/free lists
 */
static struct dt3155_fifo *dt3155_chunks;	/* list of 4MB chuncks */
static struct dt3155_fifo *dt3155_free_bufs;	/* list of free buffers */
static struct dt3155_fifo *dt3155_alloc_bufs;	/* list of allocated buffers */

/* same as in <drivers/media/video/videobuf-dma-contig.c> */
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

/* helper functions to allocate/free buffers from the pool */
static void *
dt3155_alloc_buffer(struct device *dev, size_t size, dma_addr_t *dma_handle,
								gfp_t flag)
{
	struct dt3155_buf *buf;

	if (size > DT3155_BUF_SIZE)
		return NULL;
	size = DT3155_BUF_SIZE; /* same for CCIR & RS-170 */
	buf = dt3155_get_buf(dt3155_free_bufs);
	if (!buf)
		return NULL;
	buf->dma = dma_map_single(dev, buf->cpu, size, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, buf->dma)) {
		dt3155_put_buf(buf, dt3155_free_bufs);
		return NULL;
	}
	dt3155_put_buf(buf, dt3155_alloc_bufs);
	*dma_handle = buf->dma;
	return buf->cpu;
}

static void
dt3155_free_buffer(struct device *dev, size_t size, void *cpu_addr,
							dma_addr_t dma_handle)
{
	struct dt3155_buf *buf, *last;
	int found = 0;

	if (!cpu_addr) /* to free NULL is OK */
		return;
	last = dt3155_get_buf(dt3155_alloc_bufs);
	if (!last) {
		printk(KERN_ERR "dt3155: %s(): no alloc buffers\n", __func__);
		return;
	}
	dt3155_put_buf(last, dt3155_alloc_bufs);
	do {
		buf = dt3155_get_buf(dt3155_alloc_bufs);
		if (buf->cpu == cpu_addr && buf->dma == dma_handle) {
			found = 1;
			break;
		}
		dt3155_put_buf(buf, dt3155_alloc_bufs);
	} while (buf != last);
	if (!found) {
		printk(KERN_ERR "dt3155: %s(): buffer not found\n", __func__);
		return;
	}
	size = DT3155_BUF_SIZE; /* same for CCIR & RS-170 */
	dma_unmap_single(dev, dma_handle, size, DMA_FROM_DEVICE);
	dt3155_put_buf(buf, dt3155_free_bufs);
}

/* same as videobuf_dma_contig_user_get() */
static int
dt3155_dma_contig_user_get(struct videobuf_dma_contig_memory *mem,
					struct videobuf_buffer *vb)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long prev_pfn, this_pfn;
	unsigned long pages_done, user_address;
	unsigned int offset;
	int ret;

	offset = vb->baddr & ~PAGE_MASK;
	mem->size = PAGE_ALIGN(vb->size + offset);
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
			mem->dma_handle = (this_pfn << PAGE_SHIFT) + offset;
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

/* same as videobuf_dma_contig_user_put() */
static void
dt3155_dma_contig_user_put(struct videobuf_dma_contig_memory *mem)
{
	mem->is_userptr = 0;
	mem->dma_handle = 0;
	mem->size = 0;
}

/* same as videobuf_iolock() but uses allocations from the pool */
static int
dt3155_iolock(struct videobuf_queue *q, struct videobuf_buffer *vb,
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
			return dt3155_dma_contig_user_get(mem, vb);

		/* allocate memory for the read() method */
		mem->size = PAGE_ALIGN(vb->size);
		mem->vaddr = dt3155_alloc_buffer(q->dev, mem->size,
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

/* same as videobuf_dma_contig_free() but uses the pool */
void
dt3155_dma_contig_free(struct videobuf_queue *q, struct videobuf_buffer *buf)
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
		dt3155_dma_contig_user_put(mem);
		return;
	}

	/* read() method */
	dt3155_free_buffer(q->dev, mem->size, mem->vaddr, mem->dma_handle);
	mem->vaddr = NULL;
}

/* same as videobuf_vm_open() */
static void
dt3155_vm_open(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;

	dev_dbg(map->q->dev, "vm_open %p [count=%u,vma=%08lx-%08lx]\n",
		map, map->count, vma->vm_start, vma->vm_end);

	map->count++;
}

/* same as videobuf_vm_close(), but free to the pool */
static void
dt3155_vm_close(struct vm_area_struct *vma)
{
	struct videobuf_mapping *map = vma->vm_private_data;
	struct videobuf_queue *q = map->q;
	int i;

	dev_dbg(q->dev, "vm_close %p [count=%u,vma=%08lx-%08lx]\n",
		map, map->count, vma->vm_start, vma->vm_end);

	map->count--;
	if (0 == map->count) {
		struct videobuf_dma_contig_memory *mem;

		dev_dbg(q->dev, "munmap %p q=%p\n", map, q);
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
				dev_dbg(q->dev, "buf[%d] freeing %p\n",
					i, mem->vaddr);

				dt3155_free_buffer(q->dev, mem->size,
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

static const struct vm_operations_struct dt3155_vm_ops = {
	.open     = dt3155_vm_open,
	.close    = dt3155_vm_close,
};

/* same as videobuf_mmap_mapper(), but allocates from the pool */
static int
dt3155_mmap_mapper(struct videobuf_queue *q, struct videobuf_buffer *buf,
						struct vm_area_struct *vma)
{
	struct videobuf_dma_contig_memory *mem;
	struct videobuf_mapping *map;
	int retval;
	unsigned long size;

	dev_dbg(q->dev, "%s\n", __func__);

	/* create mapping + update buffer list */
	map = kzalloc(sizeof(struct videobuf_mapping), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	buf->map = map;
	map->start = vma->vm_start;
	map->end = vma->vm_end;
	map->q = q;

	buf->baddr = vma->vm_start;

	mem = buf->priv;
	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

	mem->size = PAGE_ALIGN(buf->bsize);
	mem->vaddr = dt3155_alloc_buffer(q->dev, mem->size,
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
		dt3155_free_buffer(q->dev, mem->size,
				  mem->vaddr, mem->dma_handle);
		goto error;
	}

	vma->vm_ops          = &dt3155_vm_ops;
	vma->vm_flags       |= VM_DONTEXPAND;
	vma->vm_private_data = map;

	dev_dbg(q->dev, "mmap %p: q=%p %08lx-%08lx (%lx) pgoff %08lx buf %d\n",
		map, q, vma->vm_start, vma->vm_end,
		(long int)buf->bsize,
		vma->vm_pgoff, buf->i);

	dt3155_vm_open(vma);

	return 0;

error:
	kfree(map);
	return -ENOMEM;
}

static int
dt3155_sync_for_cpu(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct dt3155_priv *pd = q->priv_data;
	struct videobuf_dma_contig_memory *mem = vb->priv;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

	pci_dma_sync_single_for_cpu(pd->pdev, mem->dma_handle,
					mem->size, PCI_DMA_FROMDEVICE);
	return 0;
}

static int
dt3155_sync_for_device(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct dt3155_priv *pd = q->priv_data;
	struct videobuf_dma_contig_memory *mem = vb->priv;

	BUG_ON(!mem);
	MAGIC_CHECK(mem->magic, MAGIC_DC_MEM);

	pci_dma_sync_single_for_device(pd->pdev, mem->dma_handle,
						mem->size, PCI_DMA_FROMDEVICE);
	return 0;
}

/*
 * same as videobuf_queue_dma_contig_init(), but after
 * initialisation overwrites videobuf_iolock() and
 * videobuf_mmap_mapper() with our customized versions
 * as well as adds sync() method
 */
static void
dt3155_queue_dma_contig_init(struct videobuf_queue *q,
					struct videobuf_queue_ops *ops,
					struct device *dev,
					spinlock_t *irqlock,
					enum v4l2_buf_type type,
					enum v4l2_field field,
					unsigned int msize,
					void *priv)
{
	struct dt3155_priv *pd = priv;

	videobuf_queue_dma_contig_init(q, ops, dev, irqlock,
				       type, field, msize, priv);
	/* replace with local copy */
	pd->qt_ops = *q->int_ops;
	q->int_ops = &pd->qt_ops;
	/* and overwrite with our methods */
	q->int_ops->iolock = dt3155_iolock;
	q->int_ops->mmap_mapper = dt3155_mmap_mapper;
	q->int_ops->sync = dt3155_sync_for_cpu;
}

static int
dt3155_start_acq(struct dt3155_priv *pd)
{
	struct videobuf_buffer *vb = pd->curr_buf;
	dma_addr_t dma_addr;

	dma_addr = videobuf_to_dma_contig(vb);
	iowrite32(dma_addr, pd->regs + EVEN_DMA_START);
	iowrite32(dma_addr + vb->width, pd->regs + ODD_DMA_START);
	iowrite32(vb->width, pd->regs + EVEN_DMA_STRIDE);
	iowrite32(vb->width, pd->regs + ODD_DMA_STRIDE);
	/* enable interrupts, clear all irq flags */
	iowrite32(FLD_START_EN | FLD_END_ODD_EN | FLD_START |
			FLD_END_EVEN | FLD_END_ODD, pd->regs + INT_CSR);
	iowrite32(FIFO_EN | SRST | FLD_CRPT_ODD | FLD_CRPT_EVEN |
		  FLD_DN_ODD | FLD_DN_EVEN | CAP_CONT_EVEN | CAP_CONT_ODD,
							pd->regs + CSR1);
	wait_i2c_reg(pd->regs);
	write_i2c_reg(pd->regs, CONFIG, pd->config);
	write_i2c_reg(pd->regs, EVEN_CSR, CSR_ERROR | CSR_DONE);
	write_i2c_reg(pd->regs, ODD_CSR, CSR_ERROR | CSR_DONE);

	/*  start the board  */
	write_i2c_reg(pd->regs, CSR2, pd->csr2 | BUSY_EVEN | BUSY_ODD);
	return 0; /* success  */
}

static int
dt3155_stop_acq(struct dt3155_priv *pd)
{
	int tmp;

	/*  stop the board  */
	wait_i2c_reg(pd->regs);
	write_i2c_reg(pd->regs, CSR2, pd->csr2);

	/* disable all irqs, clear all irq flags */
	iowrite32(FLD_START | FLD_END_EVEN | FLD_END_ODD, pd->regs + INT_CSR);
	write_i2c_reg(pd->regs, EVEN_CSR, CSR_ERROR | CSR_DONE);
	write_i2c_reg(pd->regs, ODD_CSR, CSR_ERROR | CSR_DONE);
	tmp = ioread32(pd->regs + CSR1) & (FLD_CRPT_EVEN | FLD_CRPT_ODD);
	if (tmp)
		printk(KERN_ERR "dt3155: corrupted field %u\n", tmp);
	iowrite32(FIFO_EN | SRST | FLD_CRPT_ODD | FLD_CRPT_EVEN |
		  FLD_DN_ODD | FLD_DN_EVEN | CAP_CONT_EVEN | CAP_CONT_ODD,
							pd->regs + CSR1);
	return 0;
}

/* Locking: Caller holds q->vb_lock */
static int
dt3155_buf_setup(struct videobuf_queue *q, unsigned int *count,
							unsigned int *size)
{
	*size = img_width * img_height;
	return 0;
}

/* Locking: Caller holds q->vb_lock */
static int
dt3155_buf_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
							enum v4l2_field field)
{
	int ret = 0;

	vb->width = img_width;
	vb->height = img_height;
	vb->size = img_width * img_height;
	vb->field = field;
	if (vb->state == VIDEOBUF_NEEDS_INIT)
		ret = videobuf_iolock(q, vb, NULL);
	if (ret) {
		vb->state = VIDEOBUF_ERROR;
		printk(KERN_ERR "ERROR: videobuf_iolock() failed\n");
		videobuf_dma_contig_free(q, vb);
	} else
		vb->state = VIDEOBUF_PREPARED;
	return ret;
}

/* Locking: Caller holds q->vb_lock & q->irqlock */
static void
dt3155_buf_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct dt3155_priv *pd = q->priv_data;

	if (vb->state != VIDEOBUF_NEEDS_INIT) {
		vb->state = VIDEOBUF_QUEUED;
		dt3155_sync_for_device(q, vb);
		list_add_tail(&vb->queue, &pd->dmaq);
		wake_up_interruptible_sync(&pd->do_dma);
	} else
		vb->state = VIDEOBUF_ERROR;
}

/* Locking: Caller holds q->vb_lock */
static void
dt3155_buf_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	if (vb->state == VIDEOBUF_ACTIVE)
		videobuf_waiton(vb, 0, 0); /* FIXME: cannot be interrupted */
	dt3155_dma_contig_free(q, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static struct videobuf_queue_ops vbq_ops = {
	.buf_setup = dt3155_buf_setup,
	.buf_prepare = dt3155_buf_prepare,
	.buf_queue = dt3155_buf_queue,
	.buf_release = dt3155_buf_release,
};

static irqreturn_t
dt3155_irq_handler_even(int irq, void *dev_id)
{
	struct dt3155_priv *ipd = dev_id;
	struct videobuf_buffer *ivb;
	dma_addr_t dma_addr;
	u32 tmp;

	tmp = ioread32(ipd->regs + INT_CSR) & (FLD_START | FLD_END_ODD);
	if (!tmp)
		return IRQ_NONE;  /* not our irq */
	if ((tmp & FLD_START) && !(tmp & FLD_END_ODD)) {
		iowrite32(FLD_START_EN | FLD_END_ODD_EN | FLD_START,
							ipd->regs + INT_CSR);
		ipd->field_count++;
		return IRQ_HANDLED; /* start of field irq */
	}
	if ((tmp & FLD_START) && (tmp & FLD_END_ODD)) {
		if (!ipd->stats.start_before_end++)
			printk(KERN_ERR "dt3155: irq: START before END\n");
	}
	/*	check for corrupted fields     */
/*	write_i2c_reg(ipd->regs, EVEN_CSR, CSR_ERROR | CSR_DONE);	*/
/*	write_i2c_reg(ipd->regs, ODD_CSR, CSR_ERROR | CSR_DONE);	*/
	tmp = ioread32(ipd->regs + CSR1) & (FLD_CRPT_EVEN | FLD_CRPT_ODD);
	if (tmp) {
		if (!ipd->stats.corrupted_fields++)
			printk(KERN_ERR "dt3155: corrupted field %u\n", tmp);
		iowrite32(FIFO_EN | SRST | FLD_CRPT_ODD | FLD_CRPT_EVEN |
						FLD_DN_ODD | FLD_DN_EVEN |
						CAP_CONT_EVEN | CAP_CONT_ODD,
							ipd->regs + CSR1);
		mmiowb();
	}

	spin_lock(&ipd->lock);
	if (ipd->curr_buf && ipd->curr_buf->state == VIDEOBUF_ACTIVE) {
		if (waitqueue_active(&ipd->curr_buf->done)) {
			do_gettimeofday(&ipd->curr_buf->ts);
			ipd->curr_buf->field_count = ipd->field_count;
			ipd->curr_buf->state = VIDEOBUF_DONE;
			wake_up(&ipd->curr_buf->done);
		} else {
			ivb = ipd->curr_buf;
			goto load_dma;
		}
	} else
		goto stop_dma;
	if (list_empty(&ipd->dmaq))
		goto stop_dma;
	ivb = list_first_entry(&ipd->dmaq, typeof(*ivb), queue);
	list_del(&ivb->queue);
	if (ivb->state == VIDEOBUF_QUEUED) {
		ivb->state = VIDEOBUF_ACTIVE;
		ipd->curr_buf = ivb;
	} else
		goto stop_dma;
load_dma:
	dma_addr = videobuf_to_dma_contig(ivb);
	iowrite32(dma_addr, ipd->regs + EVEN_DMA_START);
	iowrite32(dma_addr + ivb->width, ipd->regs + ODD_DMA_START);
	iowrite32(ivb->width, ipd->regs + EVEN_DMA_STRIDE);
	iowrite32(ivb->width, ipd->regs + ODD_DMA_STRIDE);
	mmiowb();
	/* enable interrupts, clear all irq flags */
	iowrite32(FLD_START_EN | FLD_END_ODD_EN | FLD_START |
			FLD_END_EVEN | FLD_END_ODD, ipd->regs + INT_CSR);
	spin_unlock(&ipd->lock);
	return IRQ_HANDLED;

stop_dma:
	ipd->curr_buf = NULL;
	/* stop the board */
	write_i2c_reg_nowait(ipd->regs, CSR2, ipd->csr2);
	/* disable interrupts, clear all irq flags */
	iowrite32(FLD_START | FLD_END_EVEN | FLD_END_ODD, ipd->regs + INT_CSR);
	spin_unlock(&ipd->lock);
	return IRQ_HANDLED;
}

static int
dt3155_threadfn(void *arg)
{
	struct dt3155_priv *pd = arg;
	struct videobuf_buffer *vb;
	unsigned long flags;

	while (1) {
		wait_event_interruptible(pd->do_dma,
			kthread_should_stop() || !list_empty(&pd->dmaq));
		if (kthread_should_stop())
			break;

		spin_lock_irqsave(&pd->lock, flags);
		if (pd->curr_buf) /* dma is active */
			goto done;
		if (list_empty(&pd->dmaq)) /* no empty biffers */
			goto done;
		vb = list_first_entry(&pd->dmaq, typeof(*vb), queue);
		list_del(&vb->queue);
		if (vb->state == VIDEOBUF_QUEUED) {
			vb->state = VIDEOBUF_ACTIVE;
			pd->curr_buf = vb;
			spin_unlock_irqrestore(&pd->lock, flags);
			/* start dma */
			dt3155_start_acq(pd);
			continue;
		} else
			printk(KERN_DEBUG "%s(): This is a BUG\n", __func__);
done:
		spin_unlock_irqrestore(&pd->lock, flags);
	}
	return 0;
}

static int
dt3155_open(struct file *filp)
{
	int ret = 0;
	struct dt3155_priv *pd = video_drvdata(filp);

	printk(KERN_INFO "dt3155: open(): minor: %i\n", pd->vdev->minor);

	if (mutex_lock_interruptible(&pd->mux) == -EINTR)
		return -ERESTARTSYS;
	if (!pd->users) {
		pd->vidq = kzalloc(sizeof(*pd->vidq), GFP_KERNEL);
		if (!pd->vidq) {
			printk(KERN_ERR "dt3155: error: alloc queue\n");
			ret = -ENOMEM;
			goto err_alloc_queue;
		}
		dt3155_queue_dma_contig_init(pd->vidq, &vbq_ops,
				&pd->pdev->dev, &pd->lock,
				V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
				sizeof(struct videobuf_buffer), pd);
		/* disable all irqs, clear all irq flags */
		iowrite32(FLD_START | FLD_END_EVEN | FLD_END_ODD,
						pd->regs + INT_CSR);
		pd->irq_handler = dt3155_irq_handler_even;
		ret = request_irq(pd->pdev->irq, pd->irq_handler,
						IRQF_SHARED, DT3155_NAME, pd);
		if (ret) {
			printk(KERN_ERR "dt3155: error: request_irq\n");
			goto err_request_irq;
		}
		pd->curr_buf = NULL;
		pd->thread = kthread_run(dt3155_threadfn, pd,
					"dt3155_thread_%i", pd->vdev->minor);
		if (IS_ERR(pd->thread)) {
			printk(KERN_ERR "dt3155: kthread_run() failed\n");
			ret = PTR_ERR(pd->thread);
			goto err_thread;
		}
		pd->field_count = 0;
	}
	pd->users++;
	goto done;
err_thread:
	free_irq(pd->pdev->irq, pd);
err_request_irq:
	kfree(pd->vidq);
	pd->vidq = NULL;
err_alloc_queue:
done:
	mutex_unlock(&pd->mux);
	return ret;
}

static int
dt3155_release(struct file *filp)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	struct videobuf_buffer *tmp;
	unsigned long flags;
	int ret = 0;

	printk(KERN_INFO "dt3155: release(): minor: %i\n", pd->vdev->minor);

	if (mutex_lock_interruptible(&pd->mux) == -EINTR)
		return -ERESTARTSYS;
	pd->users--;
	BUG_ON(pd->users < 0);
	if (pd->acq_fp == filp) {
		spin_lock_irqsave(&pd->lock, flags);
		INIT_LIST_HEAD(&pd->dmaq); /* queue is emptied */
		tmp = pd->curr_buf;
		spin_unlock_irqrestore(&pd->lock, flags);
		if (tmp)
			videobuf_waiton(tmp, 0, 1); /* block, interruptible */
		dt3155_stop_acq(pd);
		videobuf_stop(pd->vidq);
		pd->acq_fp = NULL;
		pd->streaming = 0;
	}
	if (!pd->users) {
		kthread_stop(pd->thread);
		free_irq(pd->pdev->irq, pd);
		kfree(pd->vidq);
		pd->vidq = NULL;
	}
	mutex_unlock(&pd->mux);
	return ret;
}

static ssize_t
dt3155_read(struct file *filp, char __user *user, size_t size, loff_t *loff)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	int ret;

	if (mutex_lock_interruptible(&pd->mux) == -EINTR)
		return -ERESTARTSYS;
	if (!pd->acq_fp) {
		pd->acq_fp = filp;
		pd->streaming = 0;
	} else if (pd->acq_fp != filp) {
		ret = -EBUSY;
		goto done;
	} else if (pd->streaming == 1) {
		ret = -EINVAL;
		goto done;
	}
	ret = videobuf_read_stream(pd->vidq, user, size, loff, 0,
						filp->f_flags & O_NONBLOCK);
done:
	mutex_unlock(&pd->mux);
	return ret;
}

static unsigned int
dt3155_poll(struct file *filp, struct poll_table_struct *polltbl)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	return videobuf_poll_stream(filp, pd->vidq, polltbl);
}

static int
dt3155_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	return videobuf_mmap_mapper(pd->vidq, vma);
}

static const struct v4l2_file_operations dt3155_fops = {
	.owner = THIS_MODULE,
	.open = dt3155_open,
	.release = dt3155_release,
	.read = dt3155_read,
	.poll = dt3155_poll,
	.unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
	.mmap = dt3155_mmap,
};

static int
dt3155_ioc_streamon(struct file *filp, void *p, enum v4l2_buf_type type)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	int ret = -ERESTARTSYS;

	if (mutex_lock_interruptible(&pd->mux) == -EINTR)
		return ret;
	if (!pd->acq_fp) {
		ret = videobuf_streamon(pd->vidq);
		if (ret)
			goto unlock;
		pd->acq_fp = filp;
		pd->streaming = 1;
		wake_up_interruptible_sync(&pd->do_dma);
	} else if (pd->acq_fp == filp) {
		pd->streaming = 1;
		ret = videobuf_streamon(pd->vidq);
		if (!ret)
			wake_up_interruptible_sync(&pd->do_dma);
	} else
		ret = -EBUSY;
unlock:
	mutex_unlock(&pd->mux);
	return ret;
}

static int
dt3155_ioc_streamoff(struct file *filp, void *p, enum v4l2_buf_type type)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	struct videobuf_buffer *tmp;
	unsigned long flags;
	int ret;

	ret = videobuf_streamoff(pd->vidq);
	if (ret)
		return ret;
	spin_lock_irqsave(&pd->lock, flags);
	tmp = pd->curr_buf;
	spin_unlock_irqrestore(&pd->lock, flags);
	if (tmp)
		videobuf_waiton(tmp, 0, 1); /* block, interruptible */
	return ret;
}

static int
dt3155_ioc_querycap(struct file *filp, void *p, struct v4l2_capability *cap)
{
	struct dt3155_priv *pd = video_drvdata(filp);

	strcpy(cap->driver, DT3155_NAME);
	strcpy(cap->card, DT3155_NAME " frame grabber");
	sprintf(cap->bus_info, "PCI:%s", pci_name(pd->pdev));
	cap->version =
	       KERNEL_VERSION(DT3155_VER_MAJ, DT3155_VER_MIN, DT3155_VER_EXT);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING |
				V4L2_CAP_READWRITE;
	return 0;
}

static int
dt3155_ioc_enum_fmt_vid_cap(struct file *filp, void *p, struct v4l2_fmtdesc *f)
{
	if (f->index >= NUM_OF_FORMATS)
		return -EINVAL;
	*f = frame_std[f->index];
	return 0;
}

static int
dt3155_ioc_g_fmt_vid_cap(struct file *filp, void *p, struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	f->fmt.pix.width = img_width;
	f->fmt.pix.height = img_height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline = f->fmt.pix.width;
	f->fmt.pix.sizeimage = f->fmt.pix.width * f->fmt.pix.height;
	f->fmt.pix.colorspace = 0;
	f->fmt.pix.priv = 0;
	return 0;
}

static int
dt3155_ioc_try_fmt_vid_cap(struct file *filp, void *p, struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (f->fmt.pix.width == img_width &&
		f->fmt.pix.height == img_height &&
		f->fmt.pix.pixelformat == V4L2_PIX_FMT_GREY &&
		f->fmt.pix.field == V4L2_FIELD_NONE &&
		f->fmt.pix.bytesperline == f->fmt.pix.width &&
		f->fmt.pix.sizeimage == f->fmt.pix.width * f->fmt.pix.height)
			return 0;
	else
		return -EINVAL;
}

static int
dt3155_ioc_s_fmt_vid_cap(struct file *filp, void *p, struct v4l2_format *f)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	int ret =  -ERESTARTSYS;

	if (mutex_lock_interruptible(&pd->mux) == -EINTR)
		return ret;
	if (!pd->acq_fp) {
		pd->acq_fp = filp;
		pd->streaming = 0;
	} else if (pd->acq_fp != filp) {
		ret = -EBUSY;
		goto done;
	}
/*	FIXME: we don't change the format for now
	if (pd->vidq->streaming || pd->vidq->reading || pd->curr_buff) {
		ret = -EBUSY;
		goto done;
	}
*/
	ret = dt3155_ioc_g_fmt_vid_cap(filp, p, f);
done:
	mutex_unlock(&pd->mux);
	return ret;
}

static int
dt3155_ioc_reqbufs(struct file *filp, void *p, struct v4l2_requestbuffers *b)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	struct videobuf_queue *q = pd->vidq;
	int ret = -ERESTARTSYS;

	if (b->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;
	if (mutex_lock_interruptible(&pd->mux) == -EINTR)
		return ret;
	if (!pd->acq_fp)
		pd->acq_fp = filp;
	else if (pd->acq_fp != filp) {
		ret = -EBUSY;
		goto done;
	}
	pd->streaming = 1;
	ret = 0;
done:
	mutex_unlock(&pd->mux);
	if (ret)
		return ret;
	if (b->count)
		ret = videobuf_reqbufs(q, b);
	else { /* FIXME: is it necessary? */
		printk(KERN_DEBUG "dt3155: request to free buffers\n");
		/* ret = videobuf_mmap_free(q); */
		ret = dt3155_ioc_streamoff(filp, p,
						V4L2_BUF_TYPE_VIDEO_CAPTURE);
	}
	return ret;
}

static int
dt3155_ioc_querybuf(struct file *filp, void *p, struct v4l2_buffer *b)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	struct videobuf_queue *q = pd->vidq;

	return videobuf_querybuf(q, b);
}

static int
dt3155_ioc_qbuf(struct file *filp, void *p, struct v4l2_buffer *b)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	struct videobuf_queue *q = pd->vidq;
	int ret;

	ret = videobuf_qbuf(q, b);
	if (ret)
		return ret;
	return videobuf_querybuf(q, b);
}

static int
dt3155_ioc_dqbuf(struct file *filp, void *p, struct v4l2_buffer *b)
{
	struct dt3155_priv *pd = video_drvdata(filp);
	struct videobuf_queue *q = pd->vidq;

	return videobuf_dqbuf(q, b, filp->f_flags & O_NONBLOCK);
}

static int
dt3155_ioc_querystd(struct file *filp, void *p, v4l2_std_id *norm)
{
	*norm = DT3155_CURRENT_NORM;
	return 0;
}

static int
dt3155_ioc_g_std(struct file *filp, void *p, v4l2_std_id *norm)
{
	*norm = DT3155_CURRENT_NORM;
	return 0;
}

static int
dt3155_ioc_s_std(struct file *filp, void *p, v4l2_std_id *norm)
{
	if (*norm & DT3155_CURRENT_NORM)
		return 0;
	return -EINVAL;
}

static int
dt3155_ioc_enum_input(struct file *filp, void *p, struct v4l2_input *input)
{
	if (input->index)
		return -EINVAL;
	strcpy(input->name, "Coax in");
	input->type = V4L2_INPUT_TYPE_CAMERA;
	/*
	 * FIXME: input->std = 0 according to v4l2 API
	 * VIDIOC_G_STD, VIDIOC_S_STD, VIDIOC_QUERYSTD and VIDIOC_ENUMSTD
	 * should return -EINVAL
	 */
	input->std = DT3155_CURRENT_NORM;
	input->status = 0;/* FIXME: add sync detection & V4L2_IN_ST_NO_H_LOCK */
	return 0;
}

static int
dt3155_ioc_g_input(struct file *filp, void *p, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int
dt3155_ioc_s_input(struct file *filp, void *p, unsigned int i)
{
	if (i)
		return -EINVAL;
	return 0;
}

static int
dt3155_ioc_g_parm(struct file *filp, void *p, struct v4l2_streamparm *parms)
{
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	parms->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parms->parm.capture.capturemode = 0;
	parms->parm.capture.timeperframe.numerator = 1001;
	parms->parm.capture.timeperframe.denominator = frames_per_sec * 1000;
	parms->parm.capture.extendedmode = 0;
	parms->parm.capture.readbuffers = 1; /* FIXME: 2 buffers? */
	return 0;
}

static int
dt3155_ioc_s_parm(struct file *filp, void *p, struct v4l2_streamparm *parms)
{
	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	parms->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parms->parm.capture.capturemode = 0;
	parms->parm.capture.timeperframe.numerator = 1001;
	parms->parm.capture.timeperframe.denominator = frames_per_sec * 1000;
	parms->parm.capture.extendedmode = 0;
	parms->parm.capture.readbuffers = 1; /* FIXME: 2 buffers? */
	return 0;
}

static const struct v4l2_ioctl_ops dt3155_ioctl_ops = {
	.vidioc_streamon = dt3155_ioc_streamon,
	.vidioc_streamoff = dt3155_ioc_streamoff,
	.vidioc_querycap = dt3155_ioc_querycap,
/*
	.vidioc_g_priority = dt3155_ioc_g_priority,
	.vidioc_s_priority = dt3155_ioc_s_priority,
*/
	.vidioc_enum_fmt_vid_cap = dt3155_ioc_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = dt3155_ioc_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = dt3155_ioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = dt3155_ioc_s_fmt_vid_cap,
	.vidioc_reqbufs = dt3155_ioc_reqbufs,
	.vidioc_querybuf = dt3155_ioc_querybuf,
	.vidioc_qbuf = dt3155_ioc_qbuf,
	.vidioc_dqbuf = dt3155_ioc_dqbuf,
	.vidioc_querystd = dt3155_ioc_querystd,
	.vidioc_g_std = dt3155_ioc_g_std,
	.vidioc_s_std = dt3155_ioc_s_std,
	.vidioc_enum_input = dt3155_ioc_enum_input,
	.vidioc_g_input = dt3155_ioc_g_input,
	.vidioc_s_input = dt3155_ioc_s_input,
/*
	.vidioc_queryctrl = dt3155_ioc_queryctrl,
	.vidioc_g_ctrl = dt3155_ioc_g_ctrl,
	.vidioc_s_ctrl = dt3155_ioc_s_ctrl,
	.vidioc_querymenu = dt3155_ioc_querymenu,
	.vidioc_g_ext_ctrls = dt3155_ioc_g_ext_ctrls,
	.vidioc_s_ext_ctrls = dt3155_ioc_s_ext_ctrls,
*/
	.vidioc_g_parm = dt3155_ioc_g_parm,
	.vidioc_s_parm = dt3155_ioc_s_parm,
/*
	.vidioc_cropcap = dt3155_ioc_cropcap,
	.vidioc_g_crop = dt3155_ioc_g_crop,
	.vidioc_s_crop = dt3155_ioc_s_crop,
	.vidioc_enum_framesizes = dt3155_ioc_enum_framesizes,
	.vidioc_enum_frameintervals = dt3155_ioc_enum_frameintervals,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf = iocgmbuf,
#endif
*/
};

static int __devinit
dt3155_init_board(struct pci_dev *dev)
{
	int i;
	u8 tmp;
	struct dt3155_buf *buf;
	struct dt3155_priv *pd = pci_get_drvdata(dev);
	pci_set_master(dev); /* dt3155 needs it  */

	/*  resetting the adapter  */
	iowrite32(FLD_CRPT_ODD | FLD_CRPT_EVEN | FLD_DN_ODD | FLD_DN_EVEN,
							pd->regs + CSR1);
	mmiowb();
	msleep(10);

	/*  initializing adaper registers  */
	iowrite32(FIFO_EN | SRST, pd->regs + CSR1);
	mmiowb();
	iowrite32(0xEEEEEE01, pd->regs + EVEN_PIXEL_FMT);
	iowrite32(0xEEEEEE01, pd->regs + ODD_PIXEL_FMT);
	iowrite32(0x00000020, pd->regs + FIFO_TRIGER);
	iowrite32(0x00000103, pd->regs + XFER_MODE);
	iowrite32(0, pd->regs + RETRY_WAIT_CNT);
	iowrite32(0, pd->regs + INT_CSR);
	iowrite32(1, pd->regs + EVEN_FLD_MASK);
	iowrite32(1, pd->regs + ODD_FLD_MASK);
	iowrite32(0, pd->regs + MASK_LENGTH);
	iowrite32(0x0005007C, pd->regs + FIFO_FLAG_CNT);
	iowrite32(0x01010101, pd->regs + IIC_CLK_DUR);
	mmiowb();

	/* verifying that we have a DT3155 board (not just a SAA7116 chip) */
	read_i2c_reg(pd->regs, DT_ID, &tmp);
	if (tmp != DT3155_ID)
		return -ENODEV;

	/* initialize AD LUT */
	write_i2c_reg(pd->regs, AD_ADDR, 0);
	for (i = 0; i < 256; i++)
		write_i2c_reg(pd->regs, AD_LUT, i);

	/* initialize ADC references */
	/* FIXME: pos_ref & neg_ref depend on VT_50HZ */
	write_i2c_reg(pd->regs, AD_ADDR, AD_CMD_REG);
	write_i2c_reg(pd->regs, AD_CMD, VIDEO_CNL_1 | SYNC_CNL_1 | SYNC_LVL_3);
	write_i2c_reg(pd->regs, AD_ADDR, AD_POS_REF);
	write_i2c_reg(pd->regs, AD_CMD, 34);
	write_i2c_reg(pd->regs, AD_ADDR, AD_NEG_REF);
	write_i2c_reg(pd->regs, AD_CMD, 0);

	/* initialize PM LUT */
	write_i2c_reg(pd->regs, CONFIG, pd->config | PM_LUT_PGM);
	for (i = 0; i < 256; i++) {
		write_i2c_reg(pd->regs, PM_LUT_ADDR, i);
		write_i2c_reg(pd->regs, PM_LUT_DATA, i);
	}
	write_i2c_reg(pd->regs, CONFIG, pd->config | PM_LUT_PGM | PM_LUT_SEL);
	for (i = 0; i < 256; i++) {
		write_i2c_reg(pd->regs, PM_LUT_ADDR, i);
		write_i2c_reg(pd->regs, PM_LUT_DATA, i);
	}
	write_i2c_reg(pd->regs, CONFIG, pd->config); /*  ACQ_MODE_EVEN  */

	/* select chanel 1 for input and set sync level */
	write_i2c_reg(pd->regs, AD_ADDR, AD_CMD_REG);
	write_i2c_reg(pd->regs, AD_CMD, VIDEO_CNL_1 | SYNC_CNL_1 | SYNC_LVL_3);

	/* allocate and pci_map memory, and initialize the DMA machine */
	buf = dt3155_get_buf(dt3155_free_bufs);
	if (!buf) {
		printk(KERN_ERR "dt3155: dt3155_get_buf "
					"(in dt3155_init_board) failed\n");
		return -ENOMEM;
	}
	buf->dma = pci_map_single(dev, buf->cpu,
					DT3155_BUF_SIZE, PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(dev, buf->dma)) {
		printk(KERN_ERR "dt3155: pci_map_single failed\n");
		dt3155_put_buf(buf, dt3155_free_bufs);
		return -ENOMEM;
	}
	iowrite32(buf->dma, pd->regs + EVEN_DMA_START);
	iowrite32(buf->dma, pd->regs + ODD_DMA_START);
	iowrite32(0, pd->regs + EVEN_DMA_STRIDE);
	iowrite32(0, pd->regs + ODD_DMA_STRIDE);

	/*  Perform a pseudo even field acquire    */
	iowrite32(FIFO_EN | SRST | CAP_CONT_ODD, pd->regs + CSR1);
	write_i2c_reg(pd->regs, CSR2, pd->csr2 | SYNC_SNTL);
	write_i2c_reg(pd->regs, CONFIG, pd->config);
	write_i2c_reg(pd->regs, EVEN_CSR, CSR_SNGL);
	write_i2c_reg(pd->regs, CSR2, pd->csr2 | BUSY_EVEN | SYNC_SNTL);
	msleep(100);
	read_i2c_reg(pd->regs, CSR2, &tmp);
	write_i2c_reg(pd->regs, EVEN_CSR, CSR_ERROR | CSR_SNGL | CSR_DONE);
	write_i2c_reg(pd->regs, ODD_CSR, CSR_ERROR | CSR_SNGL | CSR_DONE);
	write_i2c_reg(pd->regs, CSR2, pd->csr2);
	iowrite32(FIFO_EN | SRST | FLD_DN_EVEN | FLD_DN_ODD, pd->regs + CSR1);

	/*  pci_unmap and deallocate memory  */
	pci_unmap_single(dev, buf->dma, DT3155_BUF_SIZE, PCI_DMA_FROMDEVICE);
	dt3155_put_buf(buf, dt3155_free_bufs);
	if (tmp & BUSY_EVEN) {
		printk(KERN_ERR "dt3155: BUSY_EVEN not cleared\n");
		return -EIO;
	}
	return 0;
}

static struct video_device dt3155_vdev = {
	.name = DT3155_NAME,
	.fops = &dt3155_fops,
	.ioctl_ops = &dt3155_ioctl_ops,
	.minor = -1,
	.release = video_device_release,
	.tvnorms = DT3155_CURRENT_NORM,
	.current_norm = DT3155_CURRENT_NORM,
};

static int __devinit
dt3155_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int err = -ENODEV;
	struct dt3155_priv *pd;

	printk(KERN_INFO "dt3155: probe()\n");
	if (pci_set_dma_mask(dev, DMA_BIT_MASK(32))) {
		printk(KERN_ERR "dt3155: cannot set dma_mask\n");
		return -ENODEV;
	}
	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		printk(KERN_ERR "dt3155: cannot allocate dt3155_priv\n");
		return -ENOMEM;
	}
	pd->vdev = video_device_alloc();
	if (!pd->vdev) {
		printk(KERN_ERR "dt3155: cannot allocate vdp structure\n");
		goto err_video_device_alloc;
	}
	*pd->vdev = dt3155_vdev;
	pci_set_drvdata(dev, pd);   /* for use in dt3155_remove()  */
	video_set_drvdata(pd->vdev, pd);  /* for use in video_fops  */
	pd->users = 0;
	pd->acq_fp = NULL;
	pd->pdev = dev;
	INIT_LIST_HEAD(&pd->dmaq);
	init_waitqueue_head(&pd->do_dma);
	mutex_init(&pd->mux);
	pd->csr2 = csr2_init;
	pd->config = config_init;
	err = pci_enable_device(pd->pdev);
	if (err) {
		printk(KERN_ERR "dt3155: pci_dev not enabled\n");
		goto err_enable_dev;
	}
	err = pci_request_region(pd->pdev, 0, pci_name(pd->pdev));
	if (err)
		goto err_req_region;
	pd->regs = pci_iomap(pd->pdev, 0, pci_resource_len(pd->pdev, 0));
	if (!pd->regs) {
		err = -ENOMEM;
		printk(KERN_ERR "dt3155: pci_iomap failed\n");
		goto err_pci_iomap;
	}
	err = dt3155_init_board(pd->pdev);
	if (err) {
		printk(KERN_ERR "dt3155: dt3155_init_board failed\n");
		goto err_init_board;
	}
	err = video_register_device(pd->vdev, VFL_TYPE_GRABBER, -1);
	if (err) {
		printk(KERN_ERR "dt3155: Cannot register video device\n");
		goto err_init_board;
	}
	printk(KERN_INFO "dt3155: /dev/video%i is ready\n", pd->vdev->minor);
	return 0;  /*   success   */

err_init_board:
	pci_iounmap(pd->pdev, pd->regs);
err_pci_iomap:
	pci_release_region(pd->pdev, 0);
err_req_region:
	pci_disable_device(pd->pdev);
err_enable_dev:
	video_device_release(pd->vdev);
err_video_device_alloc:
	kfree(pd);
	return err;
}

static void __devexit
dt3155_remove(struct pci_dev *dev)
{
	struct dt3155_priv *pd = pci_get_drvdata(dev);

	printk(KERN_INFO "dt3155: remove()\n");
	video_unregister_device(pd->vdev);
	pci_iounmap(dev, pd->regs);
	pci_release_region(pd->pdev, 0);
	pci_disable_device(pd->pdev);
	/*
	 * video_device_release() is invoked automatically
	 * see: struct video_device dt3155_vdev
	 */
	kfree(pd);
}

static DEFINE_PCI_DEVICE_TABLE(pci_ids) = {
	{ PCI_DEVICE(DT3155_VENDOR_ID, DT3155_DEVICE_ID) },
	{ 0, /* zero marks the end */ },
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static struct pci_driver pci_driver = {
	.name = DT3155_NAME,
	.id_table = pci_ids,
	.probe = dt3155_probe,
	.remove = __devexit_p(dt3155_remove),
};

static int __init
dt3155_init_module(void)
{
	int err;

	printk(KERN_INFO "dt3155: ==================\n");
	printk(KERN_INFO "dt3155: init()\n");
	dt3155_chunks = dt3155_init_chunks_fifo();
	if (!dt3155_chunks) {
		err = -ENOMEM;
		printk(KERN_ERR "dt3155: cannot init dt3155_chunks_fifo\n");
		goto err_init_chunks_fifo;
	}
	dt3155_free_bufs = dt3155_init_ibufs_fifo(dt3155_chunks,
							DT3155_BUF_SIZE);
	if (!dt3155_free_bufs) {
		err = -ENOMEM;
		printk(KERN_ERR "dt3155: cannot dt3155_init_ibufs_fifo\n");
		goto err_init_ibufs_fifo;
	}
	dt3155_alloc_bufs = dt3155_init_fifo();
	if (!dt3155_alloc_bufs) {
		err = -ENOMEM;
		printk(KERN_ERR "dt3155: cannot dt3155_init_fifo\n");
		goto err_init_fifo;
	}
	err = pci_register_driver(&pci_driver);
	if (err) {
		printk(KERN_ERR "dt3155: cannot register pci_driver\n");
		goto err_register_driver;
	}
	return 0; /* succes */
err_register_driver:
	dt3155_free_fifo(dt3155_alloc_bufs);
err_init_fifo:
	dt3155_free_ibufs_fifo(dt3155_free_bufs);
err_init_ibufs_fifo:
	dt3155_free_chunks_fifo(dt3155_chunks);
err_init_chunks_fifo:
	return err;
}

static void __exit
dt3155_exit_module(void)
{
	pci_unregister_driver(&pci_driver);
	dt3155_free_fifo(dt3155_alloc_bufs);
	dt3155_free_ibufs_fifo(dt3155_free_bufs);
	dt3155_free_chunks_fifo(dt3155_chunks);
	printk(KERN_INFO "dt3155: exit()\n");
	printk(KERN_INFO "dt3155: ==================\n");
}

module_init(dt3155_init_module);
module_exit(dt3155_exit_module);

MODULE_DESCRIPTION("video4linux pci-driver for dt3155 frame grabber");
MODULE_AUTHOR("Marin Mitov <mitov@issp.bas.bg>");
MODULE_VERSION(DT3155_VERSION);
MODULE_LICENSE("GPL");
