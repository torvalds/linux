/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Yannick Fertre <yannick.fertre@st.com>
 *          Hugues Fruchet <hugues.fruchet@st.com>
 * License terms:  GNU General Public License (GPL), version 2
 */

#include "hva.h"
#include "hva-mem.h"

int hva_mem_alloc(struct hva_ctx *ctx, u32 size, const char *name,
		  struct hva_buffer **buf)
{
	struct device *dev = ctx_to_dev(ctx);
	struct hva_buffer *b;
	dma_addr_t paddr;
	void *base;

	b = devm_kzalloc(dev, sizeof(*b), GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	base = dma_alloc_attrs(dev, size, &paddr, GFP_KERNEL | GFP_DMA,
			       DMA_ATTR_WRITE_COMBINE);
	if (!base) {
		dev_err(dev, "%s %s : dma_alloc_attrs failed for %s (size=%d)\n",
			ctx->name, __func__, name, size);
		devm_kfree(dev, b);
		return -ENOMEM;
	}

	b->size = size;
	b->paddr = paddr;
	b->vaddr = base;
	b->name = name;

	dev_dbg(dev,
		"%s allocate %d bytes of HW memory @(virt=%p, phy=%pad): %s\n",
		ctx->name, size, b->vaddr, &b->paddr, b->name);

	/* return  hva buffer to user */
	*buf = b;

	return 0;
}

void hva_mem_free(struct hva_ctx *ctx, struct hva_buffer *buf)
{
	struct device *dev = ctx_to_dev(ctx);

	dev_dbg(dev,
		"%s free %d bytes of HW memory @(virt=%p, phy=%pad): %s\n",
		ctx->name, buf->size, buf->vaddr, &buf->paddr, buf->name);

	dma_free_attrs(dev, buf->size, buf->vaddr, buf->paddr,
		       DMA_ATTR_WRITE_COMBINE);

	devm_kfree(dev, buf);
}
