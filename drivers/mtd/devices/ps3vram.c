/**
 * ps3vram - Use extra PS3 video ram as MTD block device.
 *
 * Copyright (c) 2007-2008 Jim Paris <jim@jtan.com>
 * Added support RSX DMA Vivien Chappelier <vivien.chappelier@free.fr>
 */

#include <linux/io.h>
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

#define dbg(fmt, args...)	\
	pr_debug("%s:%d " fmt "\n", __func__, __LINE__, ## args)

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
	uint64_t memory_handle;
	uint64_t context_handle;
	uint8_t *base;
	uint32_t *ctrl;
	uint32_t *reports;
	uint8_t *xdr_buf;

	uint32_t *fifo_base;
	uint32_t *fifo_ptr;

	struct ps3vram_cache cache;

	/* Used to serialize cache/DMA operations */
	struct mutex lock;
};

#define DMA_NOTIFIER_HANDLE_BASE 0x66604200 /* first DMA notifier handle */
#define DMA_NOTIFIER_OFFSET_BASE 0x1000     /* first DMA notifier offset */
#define DMA_NOTIFIER_SIZE        0x40

#define NUM_NOTIFIERS		16

#define NOTIFIER 7	/* notifier used for completion report */

/* A trailing '-' means to subtract off ps3fb_videomemory.size */
char *size = "256M-";
module_param(size, charp, 0);
MODULE_PARM_DESC(size, "memory size");

static inline uint32_t *ps3vram_get_notifier(uint32_t *reports, int notifier)
{
	return (void *) reports +
		DMA_NOTIFIER_OFFSET_BASE +
		DMA_NOTIFIER_SIZE * notifier;
}

static void ps3vram_notifier_reset(struct mtd_info *mtd)
{
	int i;
	struct ps3vram_priv *priv = mtd->priv;
	uint32_t *notify = ps3vram_get_notifier(priv->reports, NOTIFIER);
	for (i = 0; i < 4; i++)
		notify[i] = 0xffffffff;
}

static int ps3vram_notifier_wait(struct mtd_info *mtd, int timeout_ms)
{
	struct ps3vram_priv *priv = mtd->priv;
	uint32_t *notify = ps3vram_get_notifier(priv->reports, NOTIFIER);

	timeout_ms *= 1000;

	do {
		if (notify[3] == 0)
			return 0;

		if (timeout_ms)
			udelay(1);
	} while (timeout_ms--);

	return -1;
}

static void ps3vram_dump_ring(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;
	uint32_t *fifo;

	pr_info("PUT = %08x GET = %08x\n", priv->ctrl[CTRL_PUT],
		priv->ctrl[CTRL_GET]);
	for (fifo = priv->fifo_base; fifo < priv->fifo_ptr; fifo++)
		pr_info("%p: %08x\n", fifo, *fifo);
}

static void ps3vram_dump_reports(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;
	int i;

	for (i = 0; i < NUM_NOTIFIERS; i++) {
		uint32_t *n = ps3vram_get_notifier(priv->reports, i);
		pr_info("%p: %08x\n", n, *n);
	}
}

static void ps3vram_init_ring(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;

	priv->ctrl[CTRL_PUT] = FIFO_BASE + FIFO_OFFSET;
	priv->ctrl[CTRL_GET] = FIFO_BASE + FIFO_OFFSET;
}

static int ps3vram_wait_ring(struct mtd_info *mtd, int timeout)
{
	struct ps3vram_priv *priv = mtd->priv;

	/* wait until setup commands are processed */
	timeout *= 1000;
	while (--timeout) {
		if (priv->ctrl[CTRL_PUT] == priv->ctrl[CTRL_GET])
			break;
		udelay(1);
	}
	if (timeout == 0) {
		pr_err("FIFO timeout (%08x/%08x/%08x)\n", priv->ctrl[CTRL_PUT],
		       priv->ctrl[CTRL_GET], priv->ctrl[CTRL_TOP]);
		return -ETIMEDOUT;
	}

	return 0;
}

static inline void ps3vram_out_ring(struct ps3vram_priv *priv, uint32_t data)
{
	*(priv->fifo_ptr)++ = data;
}

static inline void ps3vram_begin_ring(struct ps3vram_priv *priv, uint32_t chan,
				      uint32_t tag, uint32_t size)
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
		pr_err("ps3vram: lv1_gpu_context_attribute FB_BLIT failed\n");

	priv->fifo_ptr = priv->fifo_base;
}

static void ps3vram_fire_ring(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;
	u64 status;

	mutex_lock(&ps3_gpu_mutex);

	priv->ctrl[CTRL_PUT] = FIFO_BASE + FIFO_OFFSET +
		(priv->fifo_ptr - priv->fifo_base) * sizeof(uint32_t);

	/* asking the HV for a blit will kick the fifo */
	status = lv1_gpu_context_attribute(priv->context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT,
					   0, 0, 0, 0);
	if (status)
		pr_err("ps3vram: lv1_gpu_context_attribute FB_BLIT failed\n");

	if ((priv->fifo_ptr - priv->fifo_base) * sizeof(uint32_t) >
	    FIFO_SIZE - 1024) {
		dbg("fifo full, rewinding");
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
		pr_err("notifier timeout\n");
		ps3vram_dump_ring(mtd);
		ps3vram_dump_reports(mtd);
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
		pr_err("notifier timeout\n");
		ps3vram_dump_ring(mtd);
		ps3vram_dump_reports(mtd);
		return -1;
	}

	return 0;
}

static void ps3vram_cache_evict(struct mtd_info *mtd, int entry)
{
	struct ps3vram_priv *priv = mtd->priv;
	struct ps3vram_cache *cache = &priv->cache;

	if (cache->tags[entry].flags & CACHE_PAGE_DIRTY) {
		dbg("flushing %d : 0x%08x", entry, cache->tags[entry].address);
		if (ps3vram_upload(mtd,
				   CACHE_OFFSET + entry * cache->page_size,
				   cache->tags[entry].address,
				   DMA_PAGE_SIZE,
				   cache->page_size / DMA_PAGE_SIZE) < 0) {
			pr_err("failed to upload from 0x%x to 0x%x size 0x%x\n",
			       entry * cache->page_size,
			       cache->tags[entry].address,
			       cache->page_size);
		}
		cache->tags[entry].flags &= ~CACHE_PAGE_DIRTY;
	}
}

static void ps3vram_cache_load(struct mtd_info *mtd, int entry,
			       unsigned int address)
{
	struct ps3vram_priv *priv = mtd->priv;
	struct ps3vram_cache *cache = &priv->cache;

	dbg("fetching %d : 0x%08x", entry, address);
	if (ps3vram_download(mtd,
			     address,
			     CACHE_OFFSET + entry * cache->page_size,
			     DMA_PAGE_SIZE,
			     cache->page_size / DMA_PAGE_SIZE) < 0) {
		pr_err("failed to download from 0x%x to 0x%x size 0x%x\n",
		       address,
		       entry * cache->page_size,
		       cache->page_size);
	}

	cache->tags[entry].address = address;
	cache->tags[entry].flags |= CACHE_PAGE_PRESENT;
}


static void ps3vram_cache_flush(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;
	struct ps3vram_cache *cache = &priv->cache;
	int i;

	dbg("FLUSH");
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
			dbg("found entry %d : 0x%08x",
			    i, cache->tags[i].address);
			return i;
		}
	}

	/* choose a random entry */
	i = (jiffies + (counter++)) % cache->page_count;
	dbg("using cache entry %d", i);

	ps3vram_cache_evict(mtd, i);
	ps3vram_cache_load(mtd, i, base);

	return i;
}

static int ps3vram_cache_init(struct mtd_info *mtd)
{
	struct ps3vram_priv *priv = mtd->priv;

	pr_info("creating cache: %d entries, %d bytes pages\n",
	       CACHE_PAGE_COUNT, CACHE_PAGE_SIZE);

	priv->cache.page_count = CACHE_PAGE_COUNT;
	priv->cache.page_size = CACHE_PAGE_SIZE;
	priv->cache.tags = kzalloc(sizeof(struct ps3vram_tag) *
				   CACHE_PAGE_COUNT, GFP_KERNEL);
	if (priv->cache.tags == NULL) {
		pr_err("could not allocate cache tags\n");
		return -ENOMEM;
	}

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
	memset(priv->base + instr->addr, 0xFF, instr->len);

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

	dbg("from = 0x%08x len = 0x%zx", (unsigned int) from, len);

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

		dbg("from=%08x cached=%08x offset=%08x avail=%08x count=%08x",
		    (unsigned)from, cached, offset, avail, count);

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

		dbg("to=%08x cached=%08x offset=%08x avail=%08x count=%08x",
		    (unsigned) to, cached, offset, avail, count);

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
	uint64_t status;
	uint64_t ddr_lpar, ctrl_lpar, info_lpar, reports_lpar;
	int64_t ddr_size;
	uint64_t reports_size;
	int ret = -ENOMEM;
	char *rest;

	ret = -EIO;
	ps3vram_mtd.priv = kzalloc(sizeof(struct ps3vram_priv), GFP_KERNEL);
	if (!ps3vram_mtd.priv)
		goto out;
	priv = ps3vram_mtd.priv;

	mutex_init(&priv->lock);

	/* Allocate XDR buffer (1MiB aligned) */
	priv->xdr_buf = (uint8_t *) __get_free_pages(GFP_KERNEL,
						     get_order(XDR_BUF_SIZE));
	if (priv->xdr_buf == NULL) {
		pr_err("ps3vram: could not allocate XDR buffer\n");
		ret = -ENOMEM;
		goto out_free_priv;
	}

	/* Put FIFO at begginning of XDR buffer */
	priv->fifo_base = (uint32_t *) (priv->xdr_buf + FIFO_OFFSET);
	priv->fifo_ptr = priv->fifo_base;

	/* XXX: Need to open GPU, in case ps3fb or snd_ps3 aren't loaded */
	if (ps3_open_hv_device(dev)) {
		pr_err("ps3vram: ps3_open_hv_device failed\n");
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
		printk(KERN_ERR "ps3vram: specified size is too small\n");
		ret = -EINVAL;
		goto out_close_gpu;
	}

	while (ddr_size > 0) {
		status = lv1_gpu_memory_allocate(ddr_size, 0, 0, 0, 0,
						 &priv->memory_handle,
						 &ddr_lpar);
		if (status == 0)
			break;
		ddr_size -= 1024*1024;
	}
	if (status != 0 || ddr_size <= 0) {
		pr_err("ps3vram: lv1_gpu_memory_allocate failed\n");
		ret = -ENOMEM;
		goto out_free_xdr_buf;
	}
	pr_info("ps3vram: allocated %u MiB of DDR memory\n",
		(unsigned int) (ddr_size / 1024 / 1024));

	/* Request context */
	status = lv1_gpu_context_allocate(priv->memory_handle,
					  0,
					  &priv->context_handle,
					  &ctrl_lpar,
					  &info_lpar,
					  &reports_lpar,
					  &reports_size);
	if (status) {
		pr_err("ps3vram: lv1_gpu_context_allocate failed\n");
		ret = -ENOMEM;
		goto out_free_memory;
	}

	/* Map XDR buffer to RSX */
	status = lv1_gpu_context_iomap(priv->context_handle, XDR_IOIF,
				       ps3_mm_phys_to_lpar(__pa(priv->xdr_buf)),
				       XDR_BUF_SIZE, 0);
	if (status) {
		pr_err("ps3vram: lv1_gpu_context_iomap failed\n");
		ret = -ENOMEM;
		goto out_free_context;
	}

	priv->base = ioremap(ddr_lpar, ddr_size);
	if (!priv->base) {
		pr_err("ps3vram: ioremap failed\n");
		ret = -ENOMEM;
		goto out_free_context;
	}

	priv->ctrl = ioremap(ctrl_lpar, 64 * 1024);
	if (!priv->ctrl) {
		pr_err("ps3vram: ioremap failed\n");
		ret = -ENOMEM;
		goto out_unmap_vram;
	}

	priv->reports = ioremap(reports_lpar, reports_size);
	if (!priv->reports) {
		pr_err("ps3vram: ioremap failed\n");
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
		pr_err("failed to initialize channels\n");
		ret = -ETIMEDOUT;
		goto out_unmap_reports;
	}

	ps3vram_cache_init(&ps3vram_mtd);

	if (add_mtd_device(&ps3vram_mtd)) {
		pr_err("ps3vram: failed to register device\n");
		ret = -EAGAIN;
		goto out_cache_cleanup;
	}

	pr_info("ps3vram mtd device registered, %lu bytes\n", ddr_size);
	return 0;

out_cache_cleanup:
	ps3vram_cache_cleanup(&ps3vram_mtd);
out_unmap_reports:
	iounmap(priv->reports);
out_unmap_ctrl:
	iounmap(priv->ctrl);
out_unmap_vram:
	iounmap(priv->base);
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
	iounmap(priv->base);
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
