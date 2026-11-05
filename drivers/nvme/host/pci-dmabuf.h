/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Pavel Begunkov <asml.silence@gmail.com>
 */

#ifndef _NVME_PCI_DMABUF_H
#define _NVME_PCI_DMABUF_H

#include <linux/dma-buf-io.h>

#include "nvme.h"

struct nvme_dma_buf_list {
	struct list_head ctxs;
	struct mutex lock;
	bool dead;
};

struct nvme_dma_buf_io_ctx {
	struct dma_buf_attachment *attach;
	struct nvme_dma_buf_list *buf_list;
	struct dma_buf_io_ctx *base;
	struct nvme_ctrl *ctrl;
	struct list_head list;
};

struct nvme_dmabuf_map {
	struct dma_buf_io_map base;
	struct sg_table *sgt;
	unsigned nr_entries;
	dma_addr_t dma_list[] __counted_by(nr_entries);
};

static inline struct nvme_dmabuf_map *to_nvme_dmabuf_map(
	struct dma_buf_io_map *map)
{
	return container_of(map, struct nvme_dmabuf_map, base);
}

static inline struct nvme_dmabuf_map *nvme_req_to_map(struct request *req)
{
	struct dma_buf_io_map *map = req->bio->bi_dmabuf_map;

	return to_nvme_dmabuf_map(map);
}

void __nvme_pci_remove_dmabuf(struct nvme_dma_buf_list *list);
int __nvme_init_dma_buf_io_ctx(struct nvme_ctrl *ctrl,
		struct nvme_dma_buf_list *buf_list,
		struct dma_buf_io_ctx *ctx);

static inline void nvme_pci_remove_dmabuf(struct nvme_dma_buf_list *list)
{
	if (IS_ENABLED(CONFIG_DMA_SHARED_BUFFER))
		__nvme_pci_remove_dmabuf(list);
}

static inline int nvme_init_dma_buf_io_ctx(struct nvme_ctrl *ctrl,
		struct nvme_dma_buf_list *buf_list,
		struct dma_buf_io_ctx *ctx)
{
	if (!IS_ENABLED(CONFIG_DMA_SHARED_BUFFER))
		return -EOPNOTSUPP;
	return __nvme_init_dma_buf_io_ctx(ctrl, buf_list, ctx);
}

static inline void nvme_init_buf_list(struct nvme_dma_buf_list *buf_list)
{
	INIT_LIST_HEAD(&buf_list->ctxs);
	mutex_init(&buf_list->lock);
	buf_list->dead = false;
}


#endif /* _NVME_PCI_DMABUF_H */