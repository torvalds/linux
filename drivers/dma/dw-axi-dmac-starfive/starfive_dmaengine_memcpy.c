/*
 * Copyright 2021 StarFive, Inc <samin.guo@starfivetech.com>
 *
 * API|test for dma memcopy.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/acpi_iort.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wait.h>

#include <soc/starfive/vic7100.h>

#define DMATEST_MAX_TIMEOUT_MS		20000

static DECLARE_WAIT_QUEUE_HEAD(wq);

struct dmatest_done {
	int 	timeout;
	bool	done;
};

typedef struct async_dma_parm_t {
	struct device dev;
	dma_addr_t src_dma;
	dma_addr_t dst_dma;
	void *src;
	void *dst;
	size_t size;
} async_dma_parm_t;

dma_addr_t dw_virt_to_phys(void *vaddr)
{
	struct page *pg = virt_to_page(vaddr);
	unsigned long pa_off = offset_in_page(pg);

	 /* dma_map_page */
	return page_to_phys(pg) + pa_off;
}
EXPORT_SYMBOL(dw_virt_to_phys);

void *dw_phys_to_virt(dma_addr_t phys)
{
	struct page *pg = phys_to_page(phys);
	unsigned long pa_off = offset_in_page(phys);

	return page_to_virt(pg) + pa_off;
}
EXPORT_SYMBOL(dw_phys_to_virt);

static void tx_callback(void *arg)
{
	struct dmatest_done *done = arg;

	done->done = true;
	wake_up_interruptible(&wq);
}

static int async_dma_alloc_buf(async_dma_parm_t *dma_parm)
{
	struct device *dev = &dma_parm->dev;

	dev->bus = NULL;
	dev->coherent_dma_mask = 0xffffffff;
	arch_setup_dma_ops(dev, dma_parm->dst_dma, 0, NULL, true);

	dma_parm->dst = dma_alloc_coherent(dev, dma_parm->size,
					&dma_parm->dst_dma, GFP_KERNEL);
	if (!(dma_parm->dst))
		goto _FAILED_ALLOC_DST;

	dma_parm->src = dma_alloc_coherent(dev, dma_parm->size,
					&dma_parm->src_dma, GFP_KERNEL);
	if (!(dma_parm->src))
		goto _FAILED_ALLOC_SRC;

	return 0;

_FAILED_ALLOC_SRC:
	dma_free_coherent(dev, dma_parm->size, dma_parm->dst, dma_parm->dst_dma);
_FAILED_ALLOC_DST:
	dma_free_coherent(dev, dma_parm->size, dma_parm->src, dma_parm->src_dma);
	return -ENOMEM;
}

static int async_dma_free_buf(async_dma_parm_t *dma_parm)
{
	struct device *dev = &dma_parm->dev;

	dma_free_coherent(dev, dma_parm->size, dma_parm->dst, dma_parm->dst_dma);
	dma_free_coherent(dev, dma_parm->size, dma_parm->src, dma_parm->src_dma);

	return 0;
}

static void async_dma_prebuf(void *dst, void *src, size_t size)
{
	memset((u8 *)dst, 0x00, size);
	memset((u8 *)src, 0x5a, size);
}

static int async_dma_check_data(void *dst, void *src, size_t size)
{
	return memcmp(dst, src, size);
}

/*
* phys addr for dma.
*/
int async_memcpy_single(dma_addr_t dst_dma, dma_addr_t src_dma, size_t size)
{
	struct dma_async_tx_descriptor *tx;
	struct dma_chan *chan;
	struct dmatest_done done;
	dma_cap_mask_t mask;
	dma_cookie_t cookie;
	enum dma_status	status;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	chan = dma_request_channel(mask, NULL, NULL);
	if (!chan) {
		pr_err("dma request channel failed\n");
		return -EBUSY;
	}

	tx = chan->device->device_prep_dma_memcpy(chan, dst_dma, src_dma, size,
				DMA_CTRL_ACK | DMA_PREP_INTERRUPT);

	if (!tx) {
		pr_err("Failed to prepare DMA memcpy\n");
		dma_release_channel(chan);
		return -EIO;
	}

	pr_debug("dmatest: dma_src=%#llx dma_dst=%#llx size:%#lx\n",
					src_dma, dst_dma, size);
	done.done = false;
	done.timeout = DMATEST_MAX_TIMEOUT_MS;
	tx->callback_param = &done;
	tx->callback = tx_callback;

	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		pr_err("Failed to dma tx_submit\n");
		return -EBUSY;
	}

	dma_async_issue_pending(chan);
	wait_event_interruptible_timeout(wq, done.done,
			msecs_to_jiffies(done.timeout));

#ifdef CONFIG_SOC_STARFIVE_VIC7100
	starfive_flush_dcache(src_dma, size);
	starfive_flush_dcache(dst_dma, size);
#endif
	status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);
	if (status != DMA_COMPLETE) {
		pr_err("dma: not complete! status:%d \n", status);
		dmaengine_terminate_sync(chan);
		return -EBUSY;
	}

	dma_release_channel(chan);
	return 0;
}
EXPORT_SYMBOL(async_memcpy_single);

/*
*virtl addr for cpu.
*/
int async_memcpy_single_virt(void *dst, void *src, size_t size)
{
	dma_addr_t src_dma, dst_dma;
	int ret;

	src_dma = dw_virt_to_phys(src);
	dst_dma = dw_virt_to_phys(dst);

	ret = async_memcpy_single(dst_dma, src_dma, size);
	return ret;
}
EXPORT_SYMBOL(async_memcpy_single_virt);

int async_memcpy_test(size_t size)
{
	async_dma_parm_t *dma_parm;
	int ret = 0;

	if (size < 0) {
		pr_warn("dmatest: no size input yet.\n");
		return -1;
	}

	dma_parm = kzalloc(sizeof(*dma_parm), GFP_KERNEL);
	if (IS_ERR(dma_parm))
		return PTR_ERR(dma_parm);

	dma_parm->size = size;
	ret = async_dma_alloc_buf(dma_parm);
	if (ret) {
		ret = -ENOMEM;
		goto _ERR_DMA_ALLOC_MEM;
	}

	pr_debug("dmatest: src=%#llx, dst=%#llx\n", (u64)dma_parm->src,
						(u64)dma_parm->dst);
	pr_debug("dmatest: dma_src=%#llx dma_dst=%#llx\n", dma_parm->src_dma,
						dma_parm->dst_dma);

	async_dma_prebuf(dma_parm->dst, dma_parm->src, size);
	ret = async_memcpy_single(dma_parm->dst_dma, dma_parm->src_dma, size);
	if (ret) {
		pr_err("dmatest: async_memcpy test failed. status:%d\n", ret);
		goto _ERR_DMA_MEMCPY;
	}
	ret = async_dma_check_data(dma_parm->dst, dma_parm->src, size);
	if (ret)
		pr_err("dmatest: check data error.\n");

_ERR_DMA_MEMCPY:
	async_dma_free_buf(dma_parm);
_ERR_DMA_ALLOC_MEM:
	kfree(dma_parm);

	return ret;
}
EXPORT_SYMBOL(async_memcpy_test);
