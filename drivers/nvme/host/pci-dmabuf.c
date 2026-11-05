/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Code for supporting dma-buf I/O maps in the NVMe driver.
 * Copyright (C) 2026 Pavel Begunkov <asml.silence@gmail.com>
 */

#include <linux/dma-resv.h>

#include "pci-dmabuf.h"

static void nvme_dmabuf_invalidate_mappings(struct dma_buf_attachment *attach)
{
	struct dma_buf_io_ctx *ctx = attach->importer_priv;

	dma_buf_io_invalidate_mappings(ctx);
}

static const struct dma_buf_attach_ops nvme_dmabuf_importer_ops = {
	.invalidate_mappings	= nvme_dmabuf_invalidate_mappings,
	.allow_peer2peer	= true,
};

static struct dma_buf_io_map *nvme_dma_buf_io_map(struct dma_buf_io_ctx *ctx)
{
	unsigned nr_entries = ctx->dmabuf->size / NVME_CTRL_PAGE_SIZE;
	struct nvme_dma_buf_io_ctx *nvme_ctx = ctx->dev_priv;
	struct dma_buf_attachment *attach = nvme_ctx->attach;
	unsigned long tmp, i = 0;
	struct nvme_dmabuf_map *map;
	struct scatterlist *sg;
	struct sg_table *sgt;
	int ret;

	dma_resv_assert_held(ctx->dmabuf->resv);

	if (ctx->dmabuf->size % NVME_CTRL_PAGE_SIZE)
		return ERR_PTR(-EINVAL);

	map = kvmalloc_flex(*map, dma_list, nr_entries);
	if (!map)
		return ERR_PTR(-ENOMEM);
	map->nr_entries = nr_entries;

	sgt = dma_buf_map_attachment(attach, ctx->dir);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		sgt = NULL;
		goto err;
	}

	for_each_sgtable_dma_sg(sgt, sg, tmp) {
		dma_addr_t dma_addr = sg_dma_address(sg);
		unsigned long sg_len = sg_dma_len(sg);

		if ((sg_len % NVME_CTRL_PAGE_SIZE) ||
		    (dma_addr % NVME_CTRL_PAGE_SIZE)) {
			ret = -EINVAL;
			goto err;
		}
		while (sg_len) {
			map->dma_list[i++] = dma_addr;
			dma_addr += NVME_CTRL_PAGE_SIZE;
			sg_len -= NVME_CTRL_PAGE_SIZE;
		}
	}

	ret = dma_buf_io_init_map(ctx, &map->base, sgt);
	if (ret)
		goto err;
	map->sgt = sgt;
	return &map->base;
err:
	if (sgt)
		dma_buf_unmap_attachment(attach, sgt, ctx->dir);
	kvfree(map);
	return ERR_PTR(ret);
}

static void nvme_dma_buf_io_unmap(struct dma_buf_io_ctx *ctx,
		struct dma_buf_io_map *map_base)
{
	struct nvme_dma_buf_io_ctx *nvme_ctx = ctx->dev_priv;
	struct nvme_dmabuf_map *map = to_nvme_dmabuf_map(map_base);

	dma_resv_assert_held(ctx->dmabuf->resv);

	dma_buf_unmap_attachment(nvme_ctx->attach, map->sgt, ctx->dir);
	kvfree(map);
}

static void nvme_dma_buf_io_release(struct dma_buf_io_ctx *base_ctx)
{
	struct nvme_dma_buf_io_ctx *ctx = base_ctx->dev_priv;
	struct nvme_dma_buf_list *buf_list = ctx->buf_list;

	mutex_lock(&buf_list->lock);
	if (!list_empty(&ctx->list)) {
		dma_buf_detach(ctx->base->dmabuf, ctx->attach);
		list_del_init(&ctx->list);
	}
	mutex_unlock(&buf_list->lock);

	nvme_put_ctrl(ctx->ctrl);
	kfree(ctx);
}

void __nvme_pci_remove_dmabuf(struct nvme_dma_buf_list *list)
{
	struct nvme_dma_buf_io_ctx *ctx, *tmp;

	mutex_lock(&list->lock);
	list->dead = true;
	list_for_each_entry_safe(ctx, tmp, &list->ctxs, list) {
		dma_buf_io_detach(ctx->base);
		dma_buf_detach(ctx->base->dmabuf, ctx->attach);
		list_del_init(&ctx->list);
	}
	mutex_unlock(&list->lock);
}

const struct dma_buf_io_ops nvme_dma_buf_io_ops = {
	.map		= nvme_dma_buf_io_map,
	.unmap		= nvme_dma_buf_io_unmap,
	.release	= nvme_dma_buf_io_release,
};

static int nvme_init_io_ctx_locked(struct nvme_ctrl *ctrl,
		struct nvme_dma_buf_list *buf_list,
		struct dma_buf_io_ctx *ctx)
{
	struct nvme_dma_buf_io_ctx *nvme_ctx;
	struct dma_buf_attachment *attach;

	if (buf_list->dead)
		return -ENODEV;

	nvme_ctx = kzalloc_obj(*nvme_ctx);
	if (!nvme_ctx)
		return -ENOMEM;

	attach = dma_buf_dynamic_attach(ctx->dmabuf, ctrl->dev,
					&nvme_dmabuf_importer_ops, ctx);
	if (IS_ERR(attach)) {
		kfree(nvme_ctx);
		return PTR_ERR(attach);
	}

	nvme_get_ctrl(ctrl);
	list_add(&nvme_ctx->list, &buf_list->ctxs);
	nvme_ctx->attach = attach;
	nvme_ctx->base = ctx;
	nvme_ctx->ctrl = ctrl;
	nvme_ctx->buf_list = buf_list;
	ctx->dev_priv = nvme_ctx;
	ctx->dev_ops = &nvme_dma_buf_io_ops;
	return 0;
}

int __nvme_init_dma_buf_io_ctx(struct nvme_ctrl *ctrl,
		struct nvme_dma_buf_list *buf_list,
		struct dma_buf_io_ctx *ctx)
{
	int ret;

	mutex_lock(&buf_list->lock);
	ret = nvme_init_io_ctx_locked(ctrl, buf_list, ctx);
	mutex_unlock(&buf_list->lock);
	return ret;
}
