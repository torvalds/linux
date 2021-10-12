// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright (c) 2020 Intel Corporation. All rights reserved.
 */

#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/dma-mapping.h>

#include "uverbs.h"

int ib_umem_dmabuf_map_pages(struct ib_umem_dmabuf *umem_dmabuf)
{
	struct sg_table *sgt;
	struct scatterlist *sg;
	struct dma_fence *fence;
	unsigned long start, end, cur = 0;
	unsigned int nmap = 0;
	int i;

	dma_resv_assert_held(umem_dmabuf->attach->dmabuf->resv);

	if (umem_dmabuf->sgt)
		goto wait_fence;

	sgt = dma_buf_map_attachment(umem_dmabuf->attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt))
		return PTR_ERR(sgt);

	/* modify the sg list in-place to match umem address and length */

	start = ALIGN_DOWN(umem_dmabuf->umem.address, PAGE_SIZE);
	end = ALIGN(umem_dmabuf->umem.address + umem_dmabuf->umem.length,
		    PAGE_SIZE);
	for_each_sgtable_dma_sg(sgt, sg, i) {
		if (start < cur + sg_dma_len(sg) && cur < end)
			nmap++;
		if (cur <= start && start < cur + sg_dma_len(sg)) {
			unsigned long offset = start - cur;

			umem_dmabuf->first_sg = sg;
			umem_dmabuf->first_sg_offset = offset;
			sg_dma_address(sg) += offset;
			sg_dma_len(sg) -= offset;
			cur += offset;
		}
		if (cur < end && end <= cur + sg_dma_len(sg)) {
			unsigned long trim = cur + sg_dma_len(sg) - end;

			umem_dmabuf->last_sg = sg;
			umem_dmabuf->last_sg_trim = trim;
			sg_dma_len(sg) -= trim;
			break;
		}
		cur += sg_dma_len(sg);
	}

	umem_dmabuf->umem.sgt_append.sgt.sgl = umem_dmabuf->first_sg;
	umem_dmabuf->umem.sgt_append.sgt.nents = nmap;
	umem_dmabuf->sgt = sgt;

wait_fence:
	/*
	 * Although the sg list is valid now, the content of the pages
	 * may be not up-to-date. Wait for the exporter to finish
	 * the migration.
	 */
	fence = dma_resv_excl_fence(umem_dmabuf->attach->dmabuf->resv);
	if (fence)
		return dma_fence_wait(fence, false);

	return 0;
}
EXPORT_SYMBOL(ib_umem_dmabuf_map_pages);

void ib_umem_dmabuf_unmap_pages(struct ib_umem_dmabuf *umem_dmabuf)
{
	dma_resv_assert_held(umem_dmabuf->attach->dmabuf->resv);

	if (!umem_dmabuf->sgt)
		return;

	/* retore the original sg list */
	if (umem_dmabuf->first_sg) {
		sg_dma_address(umem_dmabuf->first_sg) -=
			umem_dmabuf->first_sg_offset;
		sg_dma_len(umem_dmabuf->first_sg) +=
			umem_dmabuf->first_sg_offset;
		umem_dmabuf->first_sg = NULL;
		umem_dmabuf->first_sg_offset = 0;
	}
	if (umem_dmabuf->last_sg) {
		sg_dma_len(umem_dmabuf->last_sg) +=
			umem_dmabuf->last_sg_trim;
		umem_dmabuf->last_sg = NULL;
		umem_dmabuf->last_sg_trim = 0;
	}

	dma_buf_unmap_attachment(umem_dmabuf->attach, umem_dmabuf->sgt,
				 DMA_BIDIRECTIONAL);

	umem_dmabuf->sgt = NULL;
}
EXPORT_SYMBOL(ib_umem_dmabuf_unmap_pages);

struct ib_umem_dmabuf *ib_umem_dmabuf_get(struct ib_device *device,
					  unsigned long offset, size_t size,
					  int fd, int access,
					  const struct dma_buf_attach_ops *ops)
{
	struct dma_buf *dmabuf;
	struct ib_umem_dmabuf *umem_dmabuf;
	struct ib_umem *umem;
	unsigned long end;
	struct ib_umem_dmabuf *ret = ERR_PTR(-EINVAL);

	if (check_add_overflow(offset, (unsigned long)size, &end))
		return ret;

	if (unlikely(!ops || !ops->move_notify))
		return ret;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return ERR_CAST(dmabuf);

	if (dmabuf->size < end)
		goto out_release_dmabuf;

	umem_dmabuf = kzalloc(sizeof(*umem_dmabuf), GFP_KERNEL);
	if (!umem_dmabuf) {
		ret = ERR_PTR(-ENOMEM);
		goto out_release_dmabuf;
	}

	umem = &umem_dmabuf->umem;
	umem->ibdev = device;
	umem->length = size;
	umem->address = offset;
	umem->writable = ib_access_writable(access);
	umem->is_dmabuf = 1;

	if (!ib_umem_num_pages(umem))
		goto out_free_umem;

	umem_dmabuf->attach = dma_buf_dynamic_attach(
					dmabuf,
					device->dma_device,
					ops,
					umem_dmabuf);
	if (IS_ERR(umem_dmabuf->attach)) {
		ret = ERR_CAST(umem_dmabuf->attach);
		goto out_free_umem;
	}
	return umem_dmabuf;

out_free_umem:
	kfree(umem_dmabuf);

out_release_dmabuf:
	dma_buf_put(dmabuf);
	return ret;
}
EXPORT_SYMBOL(ib_umem_dmabuf_get);

static void
ib_umem_dmabuf_unsupported_move_notify(struct dma_buf_attachment *attach)
{
	struct ib_umem_dmabuf *umem_dmabuf = attach->importer_priv;

	ibdev_warn_ratelimited(umem_dmabuf->umem.ibdev,
			       "Invalidate callback should not be called when memory is pinned\n");
}

static struct dma_buf_attach_ops ib_umem_dmabuf_attach_pinned_ops = {
	.allow_peer2peer = true,
	.move_notify = ib_umem_dmabuf_unsupported_move_notify,
};

struct ib_umem_dmabuf *ib_umem_dmabuf_get_pinned(struct ib_device *device,
						 unsigned long offset,
						 size_t size, int fd,
						 int access)
{
	struct ib_umem_dmabuf *umem_dmabuf;
	int err;

	umem_dmabuf = ib_umem_dmabuf_get(device, offset, size, fd, access,
					 &ib_umem_dmabuf_attach_pinned_ops);
	if (IS_ERR(umem_dmabuf))
		return umem_dmabuf;

	dma_resv_lock(umem_dmabuf->attach->dmabuf->resv, NULL);
	err = dma_buf_pin(umem_dmabuf->attach);
	if (err)
		goto err_release;
	umem_dmabuf->pinned = 1;

	err = ib_umem_dmabuf_map_pages(umem_dmabuf);
	if (err)
		goto err_unpin;
	dma_resv_unlock(umem_dmabuf->attach->dmabuf->resv);

	return umem_dmabuf;

err_unpin:
	dma_buf_unpin(umem_dmabuf->attach);
err_release:
	dma_resv_unlock(umem_dmabuf->attach->dmabuf->resv);
	ib_umem_release(&umem_dmabuf->umem);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(ib_umem_dmabuf_get_pinned);

void ib_umem_dmabuf_release(struct ib_umem_dmabuf *umem_dmabuf)
{
	struct dma_buf *dmabuf = umem_dmabuf->attach->dmabuf;

	dma_resv_lock(dmabuf->resv, NULL);
	ib_umem_dmabuf_unmap_pages(umem_dmabuf);
	if (umem_dmabuf->pinned)
		dma_buf_unpin(umem_dmabuf->attach);
	dma_resv_unlock(dmabuf->resv);

	dma_buf_detach(dmabuf, umem_dmabuf->attach);
	dma_buf_put(dmabuf);
	kfree(umem_dmabuf);
}
