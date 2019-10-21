// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF System heap exporter
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <asm/page.h>

#include "heap-helpers.h"

struct dma_heap *sys_heap;

static void system_heap_free(struct heap_helper_buffer *buffer)
{
	pgoff_t pg;

	for (pg = 0; pg < buffer->pagecount; pg++)
		__free_page(buffer->pages[pg]);
	kfree(buffer->pages);
	kfree(buffer);
}

static int system_heap_allocate(struct dma_heap *heap,
				unsigned long len,
				unsigned long fd_flags,
				unsigned long heap_flags)
{
	struct heap_helper_buffer *helper_buffer;
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;
	pgoff_t pg;

	helper_buffer = kzalloc(sizeof(*helper_buffer), GFP_KERNEL);
	if (!helper_buffer)
		return -ENOMEM;

	init_heap_helper_buffer(helper_buffer, system_heap_free);
	helper_buffer->flags = heap_flags;
	helper_buffer->heap = heap;
	helper_buffer->size = len;

	helper_buffer->pagecount = len / PAGE_SIZE;
	helper_buffer->pages = kmalloc_array(helper_buffer->pagecount,
					     sizeof(*helper_buffer->pages),
					     GFP_KERNEL);
	if (!helper_buffer->pages) {
		ret = -ENOMEM;
		goto err0;
	}

	for (pg = 0; pg < helper_buffer->pagecount; pg++) {
		/*
		 * Avoid trying to allocate memory if the process
		 * has been killed by by SIGKILL
		 */
		if (fatal_signal_pending(current))
			goto err1;

		helper_buffer->pages[pg] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!helper_buffer->pages[pg])
			goto err1;
	}

	/* create the dmabuf */
	dmabuf = heap_helper_export_dmabuf(helper_buffer, fd_flags);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto err1;
	}

	helper_buffer->dmabuf = dmabuf;

	ret = dma_buf_fd(dmabuf, fd_flags);
	if (ret < 0) {
		dma_buf_put(dmabuf);
		/* just return, as put will call release and that will free */
		return ret;
	}

	return ret;

err1:
	while (pg > 0)
		__free_page(helper_buffer->pages[--pg]);
	kfree(helper_buffer->pages);
err0:
	kfree(helper_buffer);

	return -ENOMEM;
}

static const struct dma_heap_ops system_heap_ops = {
	.allocate = system_heap_allocate,
};

static int system_heap_create(void)
{
	struct dma_heap_export_info exp_info;
	int ret = 0;

	exp_info.name = "system_heap";
	exp_info.ops = &system_heap_ops;
	exp_info.priv = NULL;

	sys_heap = dma_heap_add(&exp_info);
	if (IS_ERR(sys_heap))
		ret = PTR_ERR(sys_heap);

	return ret;
}
module_init(system_heap_create);
MODULE_LICENSE("GPL v2");
