// SPDX-License-Identifier: GPL-2.0-only
/*
 * ps3vram - Use extra PS3 video ram as block device.
 *
 * Copyright 2009 Sony Corporation
 *
 * Based on the MTD ps3vram driver, which is
 * Copyright (c) 2007-2008 Jim Paris <jim@jtan.com>
 * Added support RSX DMA Vivien Chappelier <vivien.chappelier@free.fr>
 */

#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <asm/cell-regs.h>
#include <asm/firmware.h>
#include <asm/lv1call.h>
#include <asm/ps3.h>
#include <asm/ps3gpu.h>


#define DEVICE_NAME		"ps3vram"


#define XDR_BUF_SIZE (2 * 1024 * 1024) /* XDR buffer (must be 1MiB aligned) */
#define XDR_IOIF 0x0c000000

#define FIFO_BASE XDR_IOIF
#define FIFO_SIZE (64 * 1024)

#define DMA_PAGE_SIZE (4 * 1024)

#define CACHE_PAGE_SIZE (256 * 1024)
#define CACHE_PAGE_COUNT ((XDR_BUF_SIZE - FIFO_SIZE) / CACHE_PAGE_SIZE)

#define CACHE_OFFSET CACHE_PAGE_SIZE
#define FIFO_OFFSET 0

#define CTRL_PUT 0x10
#define CTRL_GET 0x11
#define CTRL_TOP 0x15

#define UPLOAD_SUBCH	1
#define DOWNLOAD_SUBCH	2

#define NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN	0x0000030c
#define NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY	0x00000104

#define CACHE_PAGE_PRESENT 1
#define CACHE_PAGE_DIRTY   2

struct ps3vram_tag {
	unsigned int address;
	unsigned int flags;
};

struct ps3vram_cache {
	unsigned int page_count;
	unsigned int page_size;
	struct ps3vram_tag *tags;
	unsigned int hit;
	unsigned int miss;
};

struct ps3vram_priv {
	struct gendisk *gendisk;

	u64 size;

	u64 memory_handle;
	u64 context_handle;
	u32 __iomem *ctrl;
	void __iomem *reports;
	u8 *xdr_buf;

	u32 *fifo_base;
	u32 *fifo_ptr;

	struct ps3vram_cache cache;

	spinlock_t lock;	/* protecting list of bios */
	struct bio_list list;
};


static int ps3vram_major;

#define DMA_NOTIFIER_HANDLE_BASE 0x66604200 /* first DMA notifier handle */
#define DMA_NOTIFIER_OFFSET_BASE 0x1000     /* first DMA notifier offset */
#define DMA_NOTIFIER_SIZE        0x40
#define NOTIFIER 7	/* notifier used for completion report */

static char *size = "256M";
module_param(size, charp, 0);
MODULE_PARM_DESC(size, "memory size");

static u32 __iomem *ps3vram_get_notifier(void __iomem *reports, int notifier)
{
	return reports + DMA_NOTIFIER_OFFSET_BASE +
	       DMA_NOTIFIER_SIZE * notifier;
}

static void ps3vram_notifier_reset(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	u32 __iomem *notify = ps3vram_get_notifier(priv->reports, NOTIFIER);
	int i;

	for (i = 0; i < 4; i++)
		iowrite32be(0xffffffff, notify + i);
}

static int ps3vram_notifier_wait(struct ps3_system_bus_device *dev,
				 unsigned int timeout_ms)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	u32 __iomem *notify = ps3vram_get_notifier(priv->reports, NOTIFIER);
	unsigned long timeout;

	for (timeout = 20; timeout; timeout--) {
		if (!ioread32be(notify + 3))
			return 0;
		udelay(10);
	}

	timeout = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		if (!ioread32be(notify + 3))
			return 0;
		msleep(1);
	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

static void ps3vram_init_ring(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);

	iowrite32be(FIFO_BASE + FIFO_OFFSET, priv->ctrl + CTRL_PUT);
	iowrite32be(FIFO_BASE + FIFO_OFFSET, priv->ctrl + CTRL_GET);
}

static int ps3vram_wait_ring(struct ps3_system_bus_device *dev,
			     unsigned int timeout_ms)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		if (ioread32be(priv->ctrl + CTRL_PUT) == ioread32be(priv->ctrl + CTRL_GET))
			return 0;
		msleep(1);
	} while (time_before(jiffies, timeout));

	dev_warn(&dev->core, "FIFO timeout (%08x/%08x/%08x)\n",
		 ioread32be(priv->ctrl + CTRL_PUT), ioread32be(priv->ctrl + CTRL_GET),
		 ioread32be(priv->ctrl + CTRL_TOP));

	return -ETIMEDOUT;
}

static void ps3vram_out_ring(struct ps3vram_priv *priv, u32 data)
{
	*(priv->fifo_ptr)++ = data;
}

static void ps3vram_begin_ring(struct ps3vram_priv *priv, u32 chan, u32 tag,
			       u32 size)
{
	ps3vram_out_ring(priv, (size << 18) | (chan << 13) | tag);
}

static void ps3vram_rewind_ring(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	int status;

	ps3vram_out_ring(priv, 0x20000000 | (FIFO_BASE + FIFO_OFFSET));

	iowrite32be(FIFO_BASE + FIFO_OFFSET, priv->ctrl + CTRL_PUT);

	/* asking the HV for a blit will kick the FIFO */
	status = lv1_gpu_fb_blit(priv->context_handle, 0, 0, 0, 0);
	if (status)
		dev_err(&dev->core, "%s: lv1_gpu_fb_blit failed %d\n",
			__func__, status);

	priv->fifo_ptr = priv->fifo_base;
}

static void ps3vram_fire_ring(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	int status;

	mutex_lock(&ps3_gpu_mutex);

	iowrite32be(FIFO_BASE + FIFO_OFFSET + (priv->fifo_ptr - priv->fifo_base)
		* sizeof(u32), priv->ctrl + CTRL_PUT);

	/* asking the HV for a blit will kick the FIFO */
	status = lv1_gpu_fb_blit(priv->context_handle, 0, 0, 0, 0);
	if (status)
		dev_err(&dev->core, "%s: lv1_gpu_fb_blit failed %d\n",
			__func__, status);

	if ((priv->fifo_ptr - priv->fifo_base) * sizeof(u32) >
	    FIFO_SIZE - 1024) {
		dev_dbg(&dev->core, "FIFO full, rewinding\n");
		ps3vram_wait_ring(dev, 200);
		ps3vram_rewind_ring(dev);
	}

	mutex_unlock(&ps3_gpu_mutex);
}

static void ps3vram_bind(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);

	ps3vram_begin_ring(priv, UPLOAD_SUBCH, 0, 1);
	ps3vram_out_ring(priv, 0x31337303);
	ps3vram_begin_ring(priv, UPLOAD_SUBCH, 0x180, 3);
	ps3vram_out_ring(priv, DMA_NOTIFIER_HANDLE_BASE + NOTIFIER);
	ps3vram_out_ring(priv, 0xfeed0001);	/* DMA system RAM instance */
	ps3vram_out_ring(priv, 0xfeed0000);     /* DMA video RAM instance */

	ps3vram_begin_ring(priv, DOWNLOAD_SUBCH, 0, 1);
	ps3vram_out_ring(priv, 0x3137c0de);
	ps3vram_begin_ring(priv, DOWNLOAD_SUBCH, 0x180, 3);
	ps3vram_out_ring(priv, DMA_NOTIFIER_HANDLE_BASE + NOTIFIER);
	ps3vram_out_ring(priv, 0xfeed0000);	/* DMA video RAM instance */
	ps3vram_out_ring(priv, 0xfeed0001);	/* DMA system RAM instance */

	ps3vram_fire_ring(dev);
}

static int ps3vram_upload(struct ps3_system_bus_device *dev,
			  unsigned int src_offset, unsigned int dst_offset,
			  int len, int count)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);

	ps3vram_begin_ring(priv, UPLOAD_SUBCH,
			   NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN, 8);
	ps3vram_out_ring(priv, XDR_IOIF + src_offset);
	ps3vram_out_ring(priv, dst_offset);
	ps3vram_out_ring(priv, len);
	ps3vram_out_ring(priv, len);
	ps3vram_out_ring(priv, len);
	ps3vram_out_ring(priv, count);
	ps3vram_out_ring(priv, (1 << 8) | 1);
	ps3vram_out_ring(priv, 0);

	ps3vram_notifier_reset(dev);
	ps3vram_begin_ring(priv, UPLOAD_SUBCH,
			   NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY, 1);
	ps3vram_out_ring(priv, 0);
	ps3vram_begin_ring(priv, UPLOAD_SUBCH, 0x100, 1);
	ps3vram_out_ring(priv, 0);
	ps3vram_fire_ring(dev);
	if (ps3vram_notifier_wait(dev, 200) < 0) {
		dev_warn(&dev->core, "%s: Notifier timeout\n", __func__);
		return -1;
	}

	return 0;
}

static int ps3vram_download(struct ps3_system_bus_device *dev,
			    unsigned int src_offset, unsigned int dst_offset,
			    int len, int count)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);

	ps3vram_begin_ring(priv, DOWNLOAD_SUBCH,
			   NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN, 8);
	ps3vram_out_ring(priv, src_offset);
	ps3vram_out_ring(priv, XDR_IOIF + dst_offset);
	ps3vram_out_ring(priv, len);
	ps3vram_out_ring(priv, len);
	ps3vram_out_ring(priv, len);
	ps3vram_out_ring(priv, count);
	ps3vram_out_ring(priv, (1 << 8) | 1);
	ps3vram_out_ring(priv, 0);

	ps3vram_notifier_reset(dev);
	ps3vram_begin_ring(priv, DOWNLOAD_SUBCH,
			   NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY, 1);
	ps3vram_out_ring(priv, 0);
	ps3vram_begin_ring(priv, DOWNLOAD_SUBCH, 0x100, 1);
	ps3vram_out_ring(priv, 0);
	ps3vram_fire_ring(dev);
	if (ps3vram_notifier_wait(dev, 200) < 0) {
		dev_warn(&dev->core, "%s: Notifier timeout\n", __func__);
		return -1;
	}

	return 0;
}

static void ps3vram_cache_evict(struct ps3_system_bus_device *dev, int entry)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	struct ps3vram_cache *cache = &priv->cache;

	if (!(cache->tags[entry].flags & CACHE_PAGE_DIRTY))
		return;

	dev_dbg(&dev->core, "Flushing %d: 0x%08x\n", entry,
		cache->tags[entry].address);
	if (ps3vram_upload(dev, CACHE_OFFSET + entry * cache->page_size,
			   cache->tags[entry].address, DMA_PAGE_SIZE,
			   cache->page_size / DMA_PAGE_SIZE) < 0) {
		dev_err(&dev->core,
			"Failed to upload from 0x%x to " "0x%x size 0x%x\n",
			entry * cache->page_size, cache->tags[entry].address,
			cache->page_size);
	}
	cache->tags[entry].flags &= ~CACHE_PAGE_DIRTY;
}

static void ps3vram_cache_load(struct ps3_system_bus_device *dev, int entry,
			       unsigned int address)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	struct ps3vram_cache *cache = &priv->cache;

	dev_dbg(&dev->core, "Fetching %d: 0x%08x\n", entry, address);
	if (ps3vram_download(dev, address,
			     CACHE_OFFSET + entry * cache->page_size,
			     DMA_PAGE_SIZE,
			     cache->page_size / DMA_PAGE_SIZE) < 0) {
		dev_err(&dev->core,
			"Failed to download from 0x%x to 0x%x size 0x%x\n",
			address, entry * cache->page_size, cache->page_size);
	}

	cache->tags[entry].address = address;
	cache->tags[entry].flags |= CACHE_PAGE_PRESENT;
}


static void ps3vram_cache_flush(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	struct ps3vram_cache *cache = &priv->cache;
	int i;

	dev_dbg(&dev->core, "FLUSH\n");
	for (i = 0; i < cache->page_count; i++) {
		ps3vram_cache_evict(dev, i);
		cache->tags[i].flags = 0;
	}
}

static unsigned int ps3vram_cache_match(struct ps3_system_bus_device *dev,
					loff_t address)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	struct ps3vram_cache *cache = &priv->cache;
	unsigned int base;
	unsigned int offset;
	int i;
	static int counter;

	offset = (unsigned int) (address & (cache->page_size - 1));
	base = (unsigned int) (address - offset);

	/* fully associative check */
	for (i = 0; i < cache->page_count; i++) {
		if ((cache->tags[i].flags & CACHE_PAGE_PRESENT) &&
		    cache->tags[i].address == base) {
			cache->hit++;
			dev_dbg(&dev->core, "Found entry %d: 0x%08x\n", i,
				cache->tags[i].address);
			return i;
		}
	}

	/* choose a random entry */
	i = (jiffies + (counter++)) % cache->page_count;
	dev_dbg(&dev->core, "Using entry %d\n", i);

	ps3vram_cache_evict(dev, i);
	ps3vram_cache_load(dev, i, base);

	cache->miss++;
	return i;
}

static int ps3vram_cache_init(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);

	priv->cache.page_count = CACHE_PAGE_COUNT;
	priv->cache.page_size = CACHE_PAGE_SIZE;
	priv->cache.tags = kcalloc(CACHE_PAGE_COUNT,
				   sizeof(struct ps3vram_tag),
				   GFP_KERNEL);
	if (!priv->cache.tags)
		return -ENOMEM;

	dev_info(&dev->core, "Created ram cache: %d entries, %d KiB each\n",
		CACHE_PAGE_COUNT, CACHE_PAGE_SIZE / 1024);

	return 0;
}

static void ps3vram_cache_cleanup(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);

	ps3vram_cache_flush(dev);
	kfree(priv->cache.tags);
}

static blk_status_t ps3vram_read(struct ps3_system_bus_device *dev, loff_t from,
			size_t len, size_t *retlen, u_char *buf)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	unsigned int cached, count;

	dev_dbg(&dev->core, "%s: from=0x%08x len=0x%zx\n", __func__,
		(unsigned int)from, len);

	if (from >= priv->size)
		return BLK_STS_IOERR;

	if (len > priv->size - from)
		len = priv->size - from;

	/* Copy from vram to buf */
	count = len;
	while (count) {
		unsigned int offset, avail;
		unsigned int entry;

		offset = (unsigned int) (from & (priv->cache.page_size - 1));
		avail  = priv->cache.page_size - offset;

		entry = ps3vram_cache_match(dev, from);
		cached = CACHE_OFFSET + entry * priv->cache.page_size + offset;

		dev_dbg(&dev->core, "%s: from=%08x cached=%08x offset=%08x "
			"avail=%08x count=%08x\n", __func__,
			(unsigned int)from, cached, offset, avail, count);

		if (avail > count)
			avail = count;
		memcpy(buf, priv->xdr_buf + cached, avail);

		buf += avail;
		count -= avail;
		from += avail;
	}

	*retlen = len;
	return 0;
}

static blk_status_t ps3vram_write(struct ps3_system_bus_device *dev, loff_t to,
			 size_t len, size_t *retlen, const u_char *buf)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	unsigned int cached, count;

	if (to >= priv->size)
		return BLK_STS_IOERR;

	if (len > priv->size - to)
		len = priv->size - to;

	/* Copy from buf to vram */
	count = len;
	while (count) {
		unsigned int offset, avail;
		unsigned int entry;

		offset = (unsigned int) (to & (priv->cache.page_size - 1));
		avail  = priv->cache.page_size - offset;

		entry = ps3vram_cache_match(dev, to);
		cached = CACHE_OFFSET + entry * priv->cache.page_size + offset;

		dev_dbg(&dev->core, "%s: to=%08x cached=%08x offset=%08x "
			"avail=%08x count=%08x\n", __func__, (unsigned int)to,
			cached, offset, avail, count);

		if (avail > count)
			avail = count;
		memcpy(priv->xdr_buf + cached, buf, avail);

		priv->cache.tags[entry].flags |= CACHE_PAGE_DIRTY;

		buf += avail;
		count -= avail;
		to += avail;
	}

	*retlen = len;
	return 0;
}

static int ps3vram_proc_show(struct seq_file *m, void *v)
{
	struct ps3vram_priv *priv = m->private;

	seq_printf(m, "hit:%u\nmiss:%u\n", priv->cache.hit, priv->cache.miss);
	return 0;
}

static void ps3vram_proc_init(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	struct proc_dir_entry *pde;

	pde = proc_create_single_data(DEVICE_NAME, 0444, NULL,
			ps3vram_proc_show, priv);
	if (!pde)
		dev_warn(&dev->core, "failed to create /proc entry\n");
}

static struct bio *ps3vram_do_bio(struct ps3_system_bus_device *dev,
				  struct bio *bio)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	int write = bio_data_dir(bio) == WRITE;
	const char *op = write ? "write" : "read";
	loff_t offset = bio->bi_iter.bi_sector << 9;
	blk_status_t error = 0;
	struct bio_vec bvec;
	struct bvec_iter iter;
	struct bio *next;

	bio_for_each_segment(bvec, bio, iter) {
		/* PS3 is ppc64, so we don't handle highmem */
		char *ptr = bvec_virt(&bvec);
		size_t len = bvec.bv_len, retlen;

		dev_dbg(&dev->core, "    %s %zu bytes at offset %llu\n", op,
			len, offset);
		if (write)
			error = ps3vram_write(dev, offset, len, &retlen, ptr);
		else
			error = ps3vram_read(dev, offset, len, &retlen, ptr);

		if (error) {
			dev_err(&dev->core, "%s failed\n", op);
			goto out;
		}

		if (retlen != len) {
			dev_err(&dev->core, "Short %s\n", op);
			error = BLK_STS_IOERR;
			goto out;
		}

		offset += len;
	}

	dev_dbg(&dev->core, "%s completed\n", op);

out:
	spin_lock_irq(&priv->lock);
	bio_list_pop(&priv->list);
	next = bio_list_peek(&priv->list);
	spin_unlock_irq(&priv->lock);

	bio->bi_status = error;
	bio_endio(bio);
	return next;
}

static void ps3vram_submit_bio(struct bio *bio)
{
	struct ps3_system_bus_device *dev = bio->bi_bdev->bd_disk->private_data;
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);
	int busy;

	dev_dbg(&dev->core, "%s\n", __func__);

	blk_queue_split(&bio);

	spin_lock_irq(&priv->lock);
	busy = !bio_list_empty(&priv->list);
	bio_list_add(&priv->list, bio);
	spin_unlock_irq(&priv->lock);

	if (busy)
		return;

	do {
		bio = ps3vram_do_bio(dev, bio);
	} while (bio);
}

static const struct block_device_operations ps3vram_fops = {
	.owner		= THIS_MODULE,
	.submit_bio	= ps3vram_submit_bio,
};

static int ps3vram_probe(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv;
	int error, status;
	struct gendisk *gendisk;
	u64 ddr_size, ddr_lpar, ctrl_lpar, info_lpar, reports_lpar,
	    reports_size, xdr_lpar;
	char *rest;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		error = -ENOMEM;
		goto fail;
	}

	spin_lock_init(&priv->lock);
	bio_list_init(&priv->list);
	ps3_system_bus_set_drvdata(dev, priv);

	/* Allocate XDR buffer (1MiB aligned) */
	priv->xdr_buf = (void *)__get_free_pages(GFP_KERNEL,
		get_order(XDR_BUF_SIZE));
	if (priv->xdr_buf == NULL) {
		dev_err(&dev->core, "Could not allocate XDR buffer\n");
		error = -ENOMEM;
		goto fail_free_priv;
	}

	/* Put FIFO at begginning of XDR buffer */
	priv->fifo_base = (u32 *) (priv->xdr_buf + FIFO_OFFSET);
	priv->fifo_ptr = priv->fifo_base;

	/* XXX: Need to open GPU, in case ps3fb or snd_ps3 aren't loaded */
	if (ps3_open_hv_device(dev)) {
		dev_err(&dev->core, "ps3_open_hv_device failed\n");
		error = -EAGAIN;
		goto out_free_xdr_buf;
	}

	/* Request memory */
	status = -1;
	ddr_size = ALIGN(memparse(size, &rest), 1024*1024);
	if (!ddr_size) {
		dev_err(&dev->core, "Specified size is too small\n");
		error = -EINVAL;
		goto out_close_gpu;
	}

	while (ddr_size > 0) {
		status = lv1_gpu_memory_allocate(ddr_size, 0, 0, 0, 0,
						 &priv->memory_handle,
						 &ddr_lpar);
		if (!status)
			break;
		ddr_size -= 1024*1024;
	}
	if (status) {
		dev_err(&dev->core, "lv1_gpu_memory_allocate failed %d\n",
			status);
		error = -ENOMEM;
		goto out_close_gpu;
	}

	/* Request context */
	status = lv1_gpu_context_allocate(priv->memory_handle, 0,
					  &priv->context_handle, &ctrl_lpar,
					  &info_lpar, &reports_lpar,
					  &reports_size);
	if (status) {
		dev_err(&dev->core, "lv1_gpu_context_allocate failed %d\n",
			status);
		error = -ENOMEM;
		goto out_free_memory;
	}

	/* Map XDR buffer to RSX */
	xdr_lpar = ps3_mm_phys_to_lpar(__pa(priv->xdr_buf));
	status = lv1_gpu_context_iomap(priv->context_handle, XDR_IOIF,
				       xdr_lpar, XDR_BUF_SIZE,
				       CBE_IOPTE_PP_W | CBE_IOPTE_PP_R |
				       CBE_IOPTE_M);
	if (status) {
		dev_err(&dev->core, "lv1_gpu_context_iomap failed %d\n",
			status);
		error = -ENOMEM;
		goto out_free_context;
	}

	priv->ctrl = ioremap(ctrl_lpar, 64 * 1024);
	if (!priv->ctrl) {
		dev_err(&dev->core, "ioremap CTRL failed\n");
		error = -ENOMEM;
		goto out_unmap_context;
	}

	priv->reports = ioremap(reports_lpar, reports_size);
	if (!priv->reports) {
		dev_err(&dev->core, "ioremap REPORTS failed\n");
		error = -ENOMEM;
		goto out_unmap_ctrl;
	}

	mutex_lock(&ps3_gpu_mutex);
	ps3vram_init_ring(dev);
	mutex_unlock(&ps3_gpu_mutex);

	priv->size = ddr_size;

	ps3vram_bind(dev);

	mutex_lock(&ps3_gpu_mutex);
	error = ps3vram_wait_ring(dev, 100);
	mutex_unlock(&ps3_gpu_mutex);
	if (error < 0) {
		dev_err(&dev->core, "Failed to initialize channels\n");
		error = -ETIMEDOUT;
		goto out_unmap_reports;
	}

	error = ps3vram_cache_init(dev);
	if (error < 0) {
		goto out_unmap_reports;
	}

	ps3vram_proc_init(dev);

	gendisk = blk_alloc_disk(NUMA_NO_NODE);
	if (!gendisk) {
		dev_err(&dev->core, "blk_alloc_disk failed\n");
		error = -ENOMEM;
		goto out_cache_cleanup;
	}

	priv->gendisk = gendisk;
	gendisk->major = ps3vram_major;
	gendisk->minors = 1;
	gendisk->fops = &ps3vram_fops;
	gendisk->private_data = dev;
	strlcpy(gendisk->disk_name, DEVICE_NAME, sizeof(gendisk->disk_name));
	set_capacity(gendisk, priv->size >> 9);
	blk_queue_max_segments(gendisk->queue, BLK_MAX_SEGMENTS);
	blk_queue_max_segment_size(gendisk->queue, BLK_MAX_SEGMENT_SIZE);
	blk_queue_max_hw_sectors(gendisk->queue, BLK_SAFE_MAX_SECTORS);

	dev_info(&dev->core, "%s: Using %llu MiB of GPU memory\n",
		 gendisk->disk_name, get_capacity(gendisk) >> 11);

	device_add_disk(&dev->core, gendisk, NULL);
	return 0;

out_cache_cleanup:
	remove_proc_entry(DEVICE_NAME, NULL);
	ps3vram_cache_cleanup(dev);
out_unmap_reports:
	iounmap(priv->reports);
out_unmap_ctrl:
	iounmap(priv->ctrl);
out_unmap_context:
	lv1_gpu_context_iomap(priv->context_handle, XDR_IOIF, xdr_lpar,
			      XDR_BUF_SIZE, CBE_IOPTE_M);
out_free_context:
	lv1_gpu_context_free(priv->context_handle);
out_free_memory:
	lv1_gpu_memory_free(priv->memory_handle);
out_close_gpu:
	ps3_close_hv_device(dev);
out_free_xdr_buf:
	free_pages((unsigned long) priv->xdr_buf, get_order(XDR_BUF_SIZE));
fail_free_priv:
	kfree(priv);
	ps3_system_bus_set_drvdata(dev, NULL);
fail:
	return error;
}

static void ps3vram_remove(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv = ps3_system_bus_get_drvdata(dev);

	del_gendisk(priv->gendisk);
	blk_cleanup_disk(priv->gendisk);
	remove_proc_entry(DEVICE_NAME, NULL);
	ps3vram_cache_cleanup(dev);
	iounmap(priv->reports);
	iounmap(priv->ctrl);
	lv1_gpu_context_iomap(priv->context_handle, XDR_IOIF,
			      ps3_mm_phys_to_lpar(__pa(priv->xdr_buf)),
			      XDR_BUF_SIZE, CBE_IOPTE_M);
	lv1_gpu_context_free(priv->context_handle);
	lv1_gpu_memory_free(priv->memory_handle);
	ps3_close_hv_device(dev);
	free_pages((unsigned long) priv->xdr_buf, get_order(XDR_BUF_SIZE));
	kfree(priv);
	ps3_system_bus_set_drvdata(dev, NULL);
}

static struct ps3_system_bus_driver ps3vram = {
	.match_id	= PS3_MATCH_ID_GPU,
	.match_sub_id	= PS3_MATCH_SUB_ID_GPU_RAMDISK,
	.core.name	= DEVICE_NAME,
	.core.owner	= THIS_MODULE,
	.probe		= ps3vram_probe,
	.remove		= ps3vram_remove,
	.shutdown	= ps3vram_remove,
};


static int __init ps3vram_init(void)
{
	int error;

	if (!firmware_has_feature(FW_FEATURE_PS3_LV1))
		return -ENODEV;

	error = register_blkdev(0, DEVICE_NAME);
	if (error <= 0) {
		pr_err("%s: register_blkdev failed %d\n", DEVICE_NAME, error);
		return error;
	}
	ps3vram_major = error;

	pr_info("%s: registered block device major %d\n", DEVICE_NAME,
		ps3vram_major);

	error = ps3_system_bus_driver_register(&ps3vram);
	if (error)
		unregister_blkdev(ps3vram_major, DEVICE_NAME);

	return error;
}

static void __exit ps3vram_exit(void)
{
	ps3_system_bus_driver_unregister(&ps3vram);
	unregister_blkdev(ps3vram_major, DEVICE_NAME);
}

module_init(ps3vram_init);
module_exit(ps3vram_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PS3 Video RAM Storage Driver");
MODULE_AUTHOR("Sony Corporation");
MODULE_ALIAS(PS3_MODULE_ALIAS_GPU_RAMDISK);
