/* linux/arch/arm/mm/cache-perf.c
 *
 * Copyright 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>

#include <asm/outercache.h>
#include <asm/cacheflush.h>
#include <linux/string.h>

enum memtype {
	MT_WBWA = 1,
	MT_NC,
	MT_SO,
	MT_MAX,
};

static unsigned int try_cnt = 100;
module_param(try_cnt, uint, S_IRUGO);
MODULE_PARM_DESC(try_cnt, "Try count to test");

static bool l1 = 1;
module_param(l1, bool, S_IRUGO);
MODULE_PARM_DESC(l1, "Set for L1 check");

static bool l2 = 1;
module_param(l2, bool, S_IRUGO);
MODULE_PARM_DESC(l2, "Set for L2 check");

static unsigned int  mcpy = MT_WBWA;
module_param(mcpy, uint, S_IRUGO);
MODULE_PARM_DESC(mcpy, "Set for mcpy");

static bool cm = 1;
module_param(cm, bool, S_IRUGO);
MODULE_PARM_DESC(cm, "Set for cache maintenance");

static unsigned int mcpy_size = SZ_4M;
module_param(mcpy_size, uint, S_IRUGO);
MODULE_PARM_DESC(mcpy_size, "Set for mcpy size");

struct task_struct *cacheperf_task;
static bool thread_running;

#define START_SIZE (64)
#define END_SIZE (SZ_4M)
#define OUT_TRY_CNT 100
#define SCRAMBLE_SIZE (128+64+32+16+8+4+2+1)
#define SMALL_SIZE (7)

enum cachemaintenance {
	CM_CLEAN,
	CM_INV,
	CM_FLUSH,
	CM_FLUSHALL,
};

static long update_timeval(struct timespec lhs, struct timespec rhs)
{
	long val;
	struct timespec ts;

	ts = timespec_sub(rhs, lhs);
	val = ts.tv_sec*NSEC_PER_SEC + ts.tv_nsec;

	return val;
}

bool buf_compare(u8 src[], u8 dst[], unsigned int bytes)
{
	unsigned int i;

	for (i = 0; i < bytes; i++) {
		if (src[i] != dst[i]) {
			printk(KERN_ERR "Failed to compare: %d, %x:%x-%x:%x\n",
			       i, (u32)src, src[i], (u32)dst, dst[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static void *remap_vm(dma_addr_t phys, u32 size, pgprot_t pgprot)
{
	unsigned long num_pages, i;
	struct page **pages;
	void *virt;

	num_pages = size >> PAGE_SHIFT;
	pages = kmalloc(num_pages * sizeof(struct page *), GFP_KERNEL);

	if (!pages)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < num_pages; i++)
		pages[i] = pfn_to_page((phys >> PAGE_SHIFT) + i);

	virt = vmap(pages, num_pages, VM_MAP, pgprot);

	if (!virt) {
		kfree(pages);
		return ERR_PTR(-ENOMEM);
	}

	kfree(pages);

	return virt;
}

static void memcpyperf(void *src, void *dst, u32 size, u32 cnt)
{
	struct timespec beforets;
	struct timespec afterts;
	long val[OUT_TRY_CNT];
	long sum = 0;
	u32 i, j;

	memset(src, 0xab, size);
	memset(dst, 0x00, size);

	for (j = 0; j < OUT_TRY_CNT; j++) {
		getnstimeofday(&beforets);
		for (i = 0; i < cnt; i++)
			memcpy(dst, src, size);
		getnstimeofday(&afterts);
		mdelay(100);
		val[j] = update_timeval(beforets, afterts)/cnt;
	}

	for (j = 0; j < OUT_TRY_CNT; j++)
		sum += val[j];

	printk(KERN_ERR "%lu\n", sum/OUT_TRY_CNT);

	if (buf_compare(src, dst, size))
		printk(KERN_ERR "copy err\n");
}

static void cacheperf(void *vbuf, enum cachemaintenance id)
{
	struct timespec beforets;
	struct timespec afterts;
	phys_addr_t pbuf = virt_to_phys(vbuf);
	u32 pbufend, xfer_size, i;
	long timeval;

	xfer_size = START_SIZE;
	while (xfer_size <= END_SIZE) {
		pbufend = pbuf + xfer_size;
		timeval = 0;

		for (i = 0; i < try_cnt; i++) {
			memset(vbuf, i, xfer_size);
			getnstimeofday(&beforets);

			switch (id) {
			case CM_CLEAN:
				if (l1)
					dmac_map_area(vbuf, xfer_size,
							DMA_TO_DEVICE);
				if (l2)
					outer_clean_range(pbuf, pbufend);
				break;
			case CM_INV:
				if (l2)
					outer_inv_range(pbuf, pbufend);
				if (l1)
					dmac_unmap_area(vbuf, xfer_size,
							DMA_FROM_DEVICE);
				break;
			case CM_FLUSH:
				if (l1)
					dmac_flush_range(vbuf,
					(void *)((u32) vbuf + xfer_size));
				if (l2)
					outer_flush_range(pbuf, pbufend);
				break;
			case CM_FLUSHALL:
				if (l1)
					flush_cache_all();
				if (l2)
					outer_flush_all();
				break;
			}
			getnstimeofday(&afterts);
			timeval += update_timeval(beforets, afterts);
		}
		printk(KERN_INFO "%lu\n", timeval/try_cnt);
		xfer_size *= 2;
	}
}

static int perfmain(void)
{
	phys_addr_t dmasrc, dmadst;
	void *srcbuf[MT_MAX];
	void *dstbuf[MT_MAX];
	u32 xfer_size;

	srcbuf[MT_WBWA] = kmalloc(END_SIZE, GFP_KERNEL);
	dstbuf[MT_WBWA] = kmalloc(END_SIZE, GFP_KERNEL);
	srcbuf[MT_NC] = dma_alloc_writecombine(
			NULL, mcpy_size, &dmasrc, GFP_KERNEL);
	dstbuf[MT_NC] = dma_alloc_writecombine(
			NULL, mcpy_size, &dmadst, GFP_KERNEL);
	if (!srcbuf[MT_WBWA] && !srcbuf[MT_NC] &&
			!dstbuf[MT_WBWA] && !dstbuf[MT_NC]) {
		printk(KERN_ERR "Memory allocation error!\n");
		dma_free_coherent(NULL, mcpy_size, srcbuf[MT_NC], dmasrc);
		dma_free_coherent(NULL, mcpy_size, dstbuf[MT_NC], dmadst);
		kfree(srcbuf[MT_WBWA]);
		kfree(dstbuf[MT_WBWA]);
		return 0;
	}

	if (mcpy) {
		printk(KERN_INFO "## Memcpy perf (ns, unit tr size: %dKB)\n",
				mcpy_size/SZ_1K);

		if (mcpy >= MT_SO) {
			printk(KERN_INFO "1. SO type\n");
			srcbuf[MT_SO] = remap_vm(dmasrc, mcpy_size,
					pgprot_noncached(PAGE_KERNEL));
			dstbuf[MT_SO] = remap_vm(dmadst, mcpy_size,
					pgprot_noncached(PAGE_KERNEL));
			xfer_size = START_SIZE;
			while (xfer_size <= END_SIZE) {
				memcpyperf(srcbuf[MT_SO], dstbuf[MT_SO],
					xfer_size, 10);
				xfer_size *= 2;
			}
			vunmap(srcbuf[MT_SO]);
			vunmap(dstbuf[MT_SO]);
		}

		if (mcpy >= MT_NC) {
			printk(KERN_INFO "2. Normal NCNB type\n");
			xfer_size = START_SIZE;
			while (xfer_size <= END_SIZE) {
				memcpyperf(srcbuf[MT_NC], dstbuf[MT_NC],
					xfer_size, 10);
				xfer_size *= 2;
			}
		}

		printk(KERN_INFO "3. Cache memcpy\n");
		printk(KERN_INFO "scramble size:");
		memcpyperf(srcbuf[MT_WBWA], dstbuf[MT_WBWA],
			SCRAMBLE_SIZE, try_cnt);
		memset(dstbuf[MT_WBWA], 0x0, SCRAMBLE_SIZE);

		printk(KERN_INFO "small size:");
		memcpyperf(srcbuf[MT_WBWA], dstbuf[MT_WBWA],
			SMALL_SIZE, try_cnt);
		memset(dstbuf[MT_WBWA], 0x0, SMALL_SIZE);

		printk(KERN_INFO "size (%d ~ %d)\n ", START_SIZE, END_SIZE);
		xfer_size = START_SIZE;
		while (xfer_size <= END_SIZE) {
			memcpyperf(srcbuf[MT_WBWA], dstbuf[MT_WBWA],
				xfer_size, try_cnt);
			xfer_size *= 2;
		}
	}

	if (cm) {
		printk(KERN_INFO "## Memcpy perf (ns)\n");

		printk(KERN_INFO "1. Clean perf\n");
		cacheperf(srcbuf[MT_WBWA], CM_CLEAN);

		printk(KERN_INFO "2. Invalidate perf\n");
		cacheperf(srcbuf[MT_WBWA], CM_INV);

		printk(KERN_INFO "3. Flush perf\n");
		cacheperf(srcbuf[MT_WBWA], CM_FLUSH);

		printk(KERN_INFO "4. Flush all perf\n");
		cacheperf(srcbuf[MT_WBWA], CM_FLUSHALL);
	}

	dma_free_coherent(NULL, mcpy_size, srcbuf[MT_NC], dmasrc);
	dma_free_coherent(NULL, mcpy_size, dstbuf[MT_NC], dmadst);
	kfree(srcbuf[MT_WBWA]);
	kfree(dstbuf[MT_WBWA]);

	return 0;
}

static int thread_func(void *data)
{
	thread_running = 1;
	perfmain();
	thread_running = 0;

	return 0;
}

int __init cacheperf_init(void)
{
#ifndef CONFIG_OUTER_CACHE
	l2 = 0;
#endif

	printk(KERN_ERR "Test condition: l1: %d, l2: %d, try_cnt:%d, (%dB ~ %dMB)\n",
				l1, l2, try_cnt, START_SIZE, END_SIZE/SZ_1M);

	cacheperf_task = kzalloc(sizeof(struct task_struct), GFP_KERNEL);
	cacheperf_task = kthread_run(thread_func, NULL, "cacheperf_thread");
	if (IS_ERR(cacheperf_task))
			printk(KERN_INFO "Failed to create module\n");

	return 0;
}
module_init(cacheperf_init)

void cacheperf_exit(void)
{
	printk(KERN_ERR "Exit module: thread_running: %d\n", thread_running);

	if (thread_running)
		kthread_stop(cacheperf_task);

	kfree(cacheperf_task);
}
module_exit(cacheperf_exit);
MODULE_LICENSE("GPL");
