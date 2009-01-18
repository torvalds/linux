/**
 * ps3vram - Use extra PS3 video ram as MTD block device.
 *
 * Copyright (c) 2007-2008 Jim Paris <jim@jtan.com>
 * Added support RSX DMA Vivien Chappelier <vivien.chappelier@free.fr>
 */

#include <linux/io.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>

#include <asm/lv1call.h>
#include <asm/ps3.h>

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

#define L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT 0x601

struct mtd_info ps3vram_mtd;

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
};

struct ps3vram_priv {
	u64 memory_handle;
	u64 context_handle;
	u32 *ctrl;
	u32 *reports;
	u8 __iomem *ddr_base;
	u8 *xdr_buf;

	u32 *fifo_base;
	u32 *fifo_ptr;

	struct device *dev;
	struct ps3vram_cache cache;

	/* Used to serialize cache/DMA operations */
	struct mutex lock;
};

#define DMA_NOTIFIER_HANDLE_BASE 0x66604200 /* first DMA notifier handle */
#define DMA_NOTIFIER_OFFSET_BASE 0x1000     /* first DMA notifier offset */
#define DMA_NOTIFIER_SIZE        0x40
#define NOTIFIER 7	/* notifier used for completion report */

/* A trailing '-' means to subtract off ps3fb_videomemory.size */
char *size = "256M-";
module_param(size, charp, 0);
MODULE_PARM_DESC(size, "memory size");

static u32 *ps3vram_get_notifier(u32 *reports, int notifier)
{
	return (void *) reports +
		DMA_NOTIFIER_OFFSET_BASE +
		DMA_NOTIFIER_SIZE * notifier;
}

static void ps3vram_notifier_reset(struct mtd_info *mtd)
{
	int i;

	struct ps3vram_priv *priv = mtd->priv;
	u32 *notify = ps3vram_get_notifier(priv->reports, NOTIFIER);
	for (i = 0; i < 4; i++)
		notify[i] = 0xffffffff;
}

static int ps3vram_notifier_wait(struct mtd_info *mtd, unsigned int timeout_ms)
{
	struct ps3vram_priv *priv = mtd->priv;
	u32 *notify = ps3vram_get_notifier(priv->reports, NOTIFIER);
	unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		if (!notify[3])
			return 0;
		msleep(1);
	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

static void ps3vram_init_ring(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;

	priv->ctrl[CTRL_PUT] = FIFO_BASE + FIFO_OFFSET;
	priv->ctrl[CTRL_GET] = FIFO_BASE + FIFO_OFFSET;
}

static int ps3vram_wait_ring(struct mtd_info *mtd, unsigned int timeout_ms)
{
	struct ps3vram_priv *priv = mtd->priv;
	unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		if (priv->ctrl[CTRL_PUT] == priv->ctrl[CTRL_GET])
			return 0;
		msleep(1);
	} while (time_before(jiffies, timeout));

	dev_dbg(priv->dev, "%s:%d: FIFO timeout (%08x/%08x/%08x)\n", __func__,
		__LINE__, priv->ctrl[CTRL_PUT], priv->ctrl[CTRL_GET],
		priv->ctrl[CTRL_TOP]);

	return -ETIMEDOUT;
}

static void ps3vram_out_ring(struct ps3vram_priv *priv, u32 data)
{
	*(priv->fifo_ptr)++ = data;
}

static void ps3vram_begin_ring(struct ps3vram_priv *priv, u32 chan,
				      u32 tag, u32 size)
{
	ps3vram_out_ring(priv, (size << 18) | (chan << 13) | tag);
}

static void ps3vram_rewind_ring(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;
	u64 status;

	ps3vram_out_ring(priv, 0x20000000 | (FIFO_BASE + FIFO_OFFSET));

	priv->ctrl[CTRL_PUT] = FIFO_BASE + FIFO_OFFSET;

	/* asking the HV for a blit will kick the fifo */
	status = lv1_gpu_context_attribute(priv->context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT,
					   0, 0, 0, 0);
	if (status)
		dev_err(priv->dev, "%s:%d: lv1_gpu_context_attribute failed\n",
			__func__, __LINE__);

	priv->fifo_ptr = priv->fifo_base;
}

static void ps3vram_fire_ring(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;
	u64 status;

	mutex_lock(&ps3_gpu_mutex);

	priv->ctrl[CTRL_PUT] = FIFO_BASE + FIFO_OFFSET +
		(priv->fifo_ptr - priv->fifo_base) * sizeof(u32);

	/* asking the HV for a blit will kick the fifo */
	status = lv1_gpu_context_attribute(priv->context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT,
					   0, 0, 0, 0);
	if (status)
		dev_err(priv->dev, "%s:%d: lv1_gpu_context_attribute failed\n",
			__func__, __LINE__);

	if ((priv->fifo_ptr - priv->fifo_base) * sizeof(u32) >
		FIFO_SIZE - 1024) {
		dev_dbg(priv->dev, "%s:%d: fifo full, rewinding\n", __func__,
			__LINE__);
		ps3vram_wait_ring(mtd, 200);
		ps3vram_rewind_ring(mtd);
	}

	mutex_unlock(&ps3_gpu_mutex);
}

static void ps3vram_bind(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;

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

	ps3vram_fire_ring(mtd);
}

static int ps3vram_upload(struct mtd_info *mtd, unsigned int src_offset,
			  unsigned int dst_offset, int len, int count)
{
	struct ps3vram_priv *priv = mtd->priv;

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

	ps3vram_notifier_reset(mtd);
	ps3vram_begin_ring(priv, UPLOAD_SUBCH,
			   NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY, 1);
	ps3vram_out_ring(priv, 0);
	ps3vram_begin_ring(priv, UPLOAD_SUBCH, 0x100, 1);
	ps3vram_out_ring(priv, 0);
	ps3vram_fire_ring(mtd);
	if (ps3vram_notifier_wait(mtd, 200) < 0) {
		dev_dbg(priv->dev, "%s:%d: notifier timeout\n", __func__,
			__LINE__);
		return -1;
	}

	return 0;
}

static int ps3vram_download(struct mtd_info *mtd, unsigned int src_offset,
			    unsigned int dst_offset, int len, int count)
{
	struct ps3vram_priv *priv = mtd->priv;

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

	ps3vram_notifier_reset(mtd);
	ps3vram_begin_ring(priv, DOWNLOAD_SUBCH,
			   NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY, 1);
	ps3vram_out_ring(priv, 0);
	ps3vram_begin_ring(priv, DOWNLOAD_SUBCH, 0x100, 1);
	ps3vram_out_ring(priv, 0);
	ps3vram_fire_ring(mtd);
	if (ps3vram_notifier_wait(mtd, 200) < 0) {
		dev_dbg(priv->dev, "%s:%d: notifier timeout\n", __func__,
			__LINE__);
		return -1;
	}

	return 0;
}

static void ps3vram_cache_evict(struct mtd_info *mtd, int entry)
{
	struct ps3vram_priv *priv = mtd->priv;
	struct ps3vram_cache *cache = &priv->cache;

	if (cache->tags[entry].flags & CACHE_PAGE_DIRTY) {
		dev_dbg(priv->dev, "%s:%d: flushing %d : 0x%08x\n", __func__,
			__LINE__, entry, cache->tags[entry].address);
		if (ps3vram_upload(mtd,
				   CACHE_OFFSET + entry * cache->page_size,
				   cache->tags[entry].address,
				   DMA_PAGE_SIZE,
				   cache->page_size / DMA_PAGE_SIZE) < 0) {
			dev_dbg(priv->dev, "%s:%d: failed to upload from "
				"0x%x to 0x%x size 0x%x\n", __func__, __LINE__,
				entry * cache->page_size,
				cache->tags[entry].address, cache->page_size);
		}
		cache->tags[entry].flags &= ~CACHE_PAGE_DIRTY;
	}
}

static void ps3vram_cache_load(struct mtd_info *mtd, int entry,
			       unsigned int address)
{
	struct ps3vram_priv *priv = mtd->priv;
	struct ps3vram_cache *cache = &priv->cache;

	dev_dbg(priv->dev, "%s:%d: fetching %d : 0x%08x\n", __func__, __LINE__,
		entry, address);
	if (ps3vram_download(mtd,
			     address,
			     CACHE_OFFSET + entry * cache->page_size,
			     DMA_PAGE_SIZE,
			     cache->page_size / DMA_PAGE_SIZE) < 0) {
		dev_err(priv->dev, "%s:%d: failed to download from "
			"0x%x to 0x%x size 0x%x\n", __func__, __LINE__, address,
			entry * cache->page_size, cache->page_size);
	}

	cache->tags[entry].address = address;
	cache->tags[entry].flags |= CACHE_PAGE_PRESENT;
}


static void ps3vram_cache_flush(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;
	struct ps3vram_cache *cache = &priv->cache;
	int i;

	dev_dbg(priv->dev, "%s:%d: FLUSH\n", __func__, __LINE__);
	for (i = 0; i < cache->page_count; i++) {
		ps3vram_cache_evict(mtd, i);
		cache->tags[i].flags = 0;
	}
}

static unsigned int ps3vram_cache_match(struct mtd_info *mtd, loff_t address)
{
	struct ps3vram_priv *priv = mtd->priv;
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
			dev_dbg(priv->dev, "%s:%d: found entry %d : 0x%08x\n",
				__func__, __LINE__, i, cache->tags[i].address);
			return i;
		}
	}

	/* choose a random entry */
	i = (jiffies + (counter++)) % cache->page_count;
	dev_dbg(priv->dev, "%s:%d: using entry %d\n", __func__, __LINE__, i);

	ps3vram_cache_evict(mtd, i);
	ps3vram_cache_load(mtd, i, base);

	return i;
}

static int ps3vram_cache_init(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;

	priv->cache.page_count = CACHE_PAGE_COUNT;
	priv->cache.page_size = CACHE_PAGE_SIZE;
	priv->cache.tags = kzalloc(sizeof(struct ps3vram_tag) *
				   CACHE_PAGE_COUNT, GFP_KERNEL);
	if (priv->cache.tags == NULL) {
		dev_err(priv->dev, "%s:%d: could not allocate cache tags\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	dev_info(priv->dev, "created ram cache: %d entries, %d KiB each\n",
		CACHE_PAGE_COUNT, CACHE_PAGE_SIZE / 1024);

	return 0;
}

static void ps3vram_cache_cleanup(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;

	ps3vram_cache_flush(mtd);
	kfree(priv->cache.tags);
}

static int ps3vram_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct ps3vram_priv *priv = mtd->priv;

	if (instr->addr + instr->len > mtd->size)
		return -EINVAL;

	mutex_lock(&priv->lock);

	ps3vram_cache_flush(mtd);

	/* Set bytes to 0xFF */
	memset_io(priv->ddr_base + instr->addr, 0xFF, instr->len);

	mutex_unlock(&priv->lock);

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}

static int ps3vram_read(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	struct ps3vram_priv *priv = mtd->priv;
	unsigned int cached, count;

	dev_dbg(priv->dev, "%s:%d: from=0x%08x len=0x%zx\n", __func__, __LINE__,
		(unsigned int)from, len);

	if (from >= mtd->size)
		return -EINVAL;

	if (len > mtd->size - from)
		len = mtd->size - from;

	/* Copy from vram to buf */
	count = len;
	while (count) {
		unsigned int offset, avail;
		unsigned int entry;

		offset = (unsigned int) (from & (priv->cache.page_size - 1));
		avail  = priv->cache.page_size - offset;

		mutex_lock(&priv->lock);

		entry = ps3vram_cache_match(mtd, from);
		cached = CACHE_OFFSET + entry * priv->cache.page_size + offset;

		dev_dbg(priv->dev, "%s:%d: from=%08x cached=%08x offset=%08x "
			"avail=%08x count=%08x\n", __func__, __LINE__,
			(unsigned int)from, cached, offset, avail, count);

		if (avail > count)
			avail = count;
		memcpy(buf, priv->xdr_buf + cached, avail);

		mutex_unlock(&priv->lock);

		buf += avail;
		count -= avail;
		from += avail;
	}

	*retlen = len;
	return 0;
}

static int ps3vram_write(struct mtd_info *mtd, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	struct ps3vram_priv *priv = mtd->priv;
	unsigned int cached, count;

	if (to >= mtd->size)
		return -EINVAL;

	if (len > mtd->size - to)
		len = mtd->size - to;

	/* Copy from buf to vram */
	count = len;
	while (count) {
		unsigned int offset, avail;
		unsigned int entry;

		offset = (unsigned int) (to & (priv->cache.page_size - 1));
		avail  = priv->cache.page_size - offset;

		mutex_lock(&priv->lock);

		entry = ps3vram_cache_match(mtd, to);
		cached = CACHE_OFFSET + entry * priv->cache.page_size + offset;

		dev_dbg(priv->dev, "%s:%d: to=%08x cached=%08x offset=%08x "
			"avail=%08x count=%08x\n", __func__, __LINE__,
			(unsigned int)to, cached, offset, avail, count);

		if (avail > count)
			avail = count;
		memcpy(priv->xdr_buf + cached, buf, avail);

		priv->cache.tags[entry].flags |= CACHE_PAGE_DIRTY;

		mutex_unlock(&priv->lock);

		buf += avail;
		count -= avail;
		to += avail;
	}

	*retlen = len;
	return 0;
}

static int __devinit ps3vram_probe(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv;
	int status;
	u64 ddr_lpar;
	u64 ctrl_lpar;
	u64 info_lpar;
	u64 reports_lpar;
	u64 ddr_size;
	u64 reports_size;
	int ret = -ENOMEM;
	char *rest;

	ret = -EIO;
	ps3vram_mtd.priv = kzalloc(sizeof(struct ps3vram_priv), GFP_KERNEL);
	if (!ps3vram_mtd.priv)
		goto out;
	priv = ps3vram_mtd.priv;

	mutex_init(&priv->lock);
	priv->dev = &dev->core;

	/* Allocate XDR buffer (1MiB aligned) */
	priv->xdr_buf = (void *)__get_free_pages(GFP_KERNEL,
		get_order(XDR_BUF_SIZE));
	if (priv->xdr_buf == NULL) {
		dev_dbg(&dev->core, "%s:%d: could not allocate XDR buffer\n",
			__func__, __LINE__);
		ret = -ENOMEM;
		goto out_free_priv;
	}

	/* Put FIFO at begginning of XDR buffer */
	priv->fifo_base = (u32 *) (priv->xdr_buf + FIFO_OFFSET);
	priv->fifo_ptr = priv->fifo_base;

	/* XXX: Need to open GPU, in case ps3fb or snd_ps3 aren't loaded */
	if (ps3_open_hv_device(dev)) {
		dev_err(&dev->core, "%s:%d: ps3_open_hv_device failed\n",
			__func__, __LINE__);
		ret = -EAGAIN;
		goto out_close_gpu;
	}

	/* Request memory */
	status = -1;
	ddr_size = memparse(size, &rest);
	if (*rest == '-')
		ddr_size -= ps3fb_videomemory.size;
	ddr_size = ALIGN(ddr_size, 1024*1024);
	if (ddr_size <= 0) {
		dev_err(&dev->core, "%s:%d: specified size is too small\n",
			__func__, __LINE__);
		ret = -EINVAL;
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
	if (status || ddr_size <= 0) {
		dev_err(&dev->core, "%s:%d: lv1_gpu_memory_allocate failed\n",
			__func__, __LINE__);
		ret = -ENOMEM;
		goto out_free_xdr_buf;
	}

	/* Request context */
	status = lv1_gpu_context_allocate(priv->memory_handle,
					  0,
					  &priv->context_handle,
					  &ctrl_lpar,
					  &info_lpar,
					  &reports_lpar,
					  &reports_size);
	if (status) {
		dev_err(&dev->core, "%s:%d: lv1_gpu_context_allocate failed\n",
			__func__, __LINE__);
		ret = -ENOMEM;
		goto out_free_memory;
	}

	/* Map XDR buffer to RSX */
	status = lv1_gpu_context_iomap(priv->context_handle, XDR_IOIF,
				       ps3_mm_phys_to_lpar(__pa(priv->xdr_buf)),
				       XDR_BUF_SIZE, 0);
	if (status) {
		dev_err(&dev->core, "%s:%d: lv1_gpu_context_iomap failed\n",
			__func__, __LINE__);
		ret = -ENOMEM;
		goto out_free_context;
	}

	priv->ddr_base = ioremap_flags(ddr_lpar, ddr_size, _PAGE_NO_CACHE);

	if (!priv->ddr_base) {
		dev_err(&dev->core, "%s:%d: ioremap failed\n", __func__,
			__LINE__);
		ret = -ENOMEM;
		goto out_free_context;
	}

	priv->ctrl = ioremap(ctrl_lpar, 64 * 1024);
	if (!priv->ctrl) {
		dev_err(&dev->core, "%s:%d: ioremap failed\n", __func__,
			__LINE__);
		ret = -ENOMEM;
		goto out_unmap_vram;
	}

	priv->reports = ioremap(reports_lpar, reports_size);
	if (!priv->reports) {
		dev_err(&dev->core, "%s:%d: ioremap failed\n", __func__,
			__LINE__);
		ret = -ENOMEM;
		goto out_unmap_ctrl;
	}

	mutex_lock(&ps3_gpu_mutex);
	ps3vram_init_ring(&ps3vram_mtd);
	mutex_unlock(&ps3_gpu_mutex);

	ps3vram_mtd.name = "ps3vram";
	ps3vram_mtd.size = ddr_size;
	ps3vram_mtd.flags = MTD_CAP_RAM;
	ps3vram_mtd.erase = ps3vram_erase;
	ps3vram_mtd.point = NULL;
	ps3vram_mtd.unpoint = NULL;
	ps3vram_mtd.read = ps3vram_read;
	ps3vram_mtd.write = ps3vram_write;
	ps3vram_mtd.owner = THIS_MODULE;
	ps3vram_mtd.type = MTD_RAM;
	ps3vram_mtd.erasesize = CACHE_PAGE_SIZE;
	ps3vram_mtd.writesize = 1;

	ps3vram_bind(&ps3vram_mtd);

	mutex_lock(&ps3_gpu_mutex);
	ret = ps3vram_wait_ring(&ps3vram_mtd, 100);
	mutex_unlock(&ps3_gpu_mutex);
	if (ret < 0) {
		dev_err(&dev->core, "%s:%d: failed to initialize channels\n",
			__func__, __LINE__);
		ret = -ETIMEDOUT;
		goto out_unmap_reports;
	}

	ps3vram_cache_init(&ps3vram_mtd);

	if (add_mtd_device(&ps3vram_mtd)) {
		dev_err(&dev->core, "%s:%d: add_mtd_device failed\n",
			__func__, __LINE__);
		ret = -EAGAIN;
		goto out_cache_cleanup;
	}

	dev_info(&dev->core, "reserved %u MiB of gpu memory\n",
		(unsigned int)(ddr_size / 1024 / 1024));

	return 0;

out_cache_cleanup:
	ps3vram_cache_cleanup(&ps3vram_mtd);
out_unmap_reports:
	iounmap(priv->reports);
out_unmap_ctrl:
	iounmap(priv->ctrl);
out_unmap_vram:
	iounmap(priv->ddr_base);
out_free_context:
	lv1_gpu_context_free(priv->context_handle);
out_free_memory:
	lv1_gpu_memory_free(priv->memory_handle);
out_close_gpu:
	ps3_close_hv_device(dev);
out_free_xdr_buf:
	free_pages((unsigned long) priv->xdr_buf, get_order(XDR_BUF_SIZE));
out_free_priv:
	kfree(ps3vram_mtd.priv);
	ps3vram_mtd.priv = NULL;
out:
	return ret;
}

static int ps3vram_shutdown(struct ps3_system_bus_device *dev)
{
	struct ps3vram_priv *priv;

	priv = ps3vram_mtd.priv;

	del_mtd_device(&ps3vram_mtd);
	ps3vram_cache_cleanup(&ps3vram_mtd);
	iounmap(priv->reports);
	iounmap(priv->ctrl);
	iounmap(priv->ddr_base);
	lv1_gpu_context_free(priv->context_handle);
	lv1_gpu_memory_free(priv->memory_handle);
	ps3_close_hv_device(dev);
	free_pages((unsigned long) priv->xdr_buf, get_order(XDR_BUF_SIZE));
	kfree(priv);
	return 0;
}

static struct ps3_system_bus_driver ps3vram_driver = {
	.match_id	= PS3_MATCH_ID_GPU,
	.match_sub_id	= PS3_MATCH_SUB_ID_GPU_RAMDISK,
	.core.name	= DEVICE_NAME,
	.core.owner	= THIS_MODULE,
	.probe		= ps3vram_probe,
	.remove		= ps3vram_shutdown,
	.shutdown	= ps3vram_shutdown,
};

static int __init ps3vram_init(void)
{
	return ps3_system_bus_driver_register(&ps3vram_driver);
}

static void __exit ps3vram_exit(void)
{
	ps3_system_bus_driver_unregister(&ps3vram_driver);
}

module_init(ps3vram_init);
module_exit(ps3vram_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jim Paris <jim@jtan.com>");
MODULE_DESCRIPTION("MTD driver for PS3 video RAM");
MODULE_ALIAS(PS3_MODULE_ALIAS_GPU_RAMDISK);
