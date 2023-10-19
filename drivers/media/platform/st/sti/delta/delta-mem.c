// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2015
 * Author: Hugues Fruchet <hugues.fruchet@st.com> for STMicroelectronics.
 */

#include "delta.h"
#include "delta-mem.h"

int hw_alloc(struct delta_ctx *ctx, u32 size, const char *name,
	     struct delta_buf *buf)
{
	struct delta_dev *delta = ctx->dev;
	dma_addr_t dma_addr;
	void *addr;
	unsigned long attrs = DMA_ATTR_WRITE_COMBINE;

	addr = dma_alloc_attrs(delta->dev, size, &dma_addr,
			       GFP_KERNEL | __GFP_NOWARN, attrs);
	if (!addr) {
		dev_err(delta->dev,
			"%s hw_alloc:dma_alloc_coherent failed for %s (size=%d)\n",
			ctx->name, name, size);
		ctx->sys_errors++;
		return -ENOMEM;
	}

	buf->size = size;
	buf->paddr = dma_addr;
	buf->vaddr = addr;
	buf->name = name;
	buf->attrs = attrs;

	dev_dbg(delta->dev,
		"%s allocate %d bytes of HW memory @(virt=0x%p, phy=0x%pad): %s\n",
		ctx->name, size, buf->vaddr, &buf->paddr, buf->name);

	return 0;
}

void hw_free(struct delta_ctx *ctx, struct delta_buf *buf)
{
	struct delta_dev *delta = ctx->dev;

	dev_dbg(delta->dev,
		"%s     free %d bytes of HW memory @(virt=0x%p, phy=0x%pad): %s\n",
		ctx->name, buf->size, buf->vaddr, &buf->paddr, buf->name);

	dma_free_attrs(delta->dev, buf->size,
		       buf->vaddr, buf->paddr, buf->attrs);
}
