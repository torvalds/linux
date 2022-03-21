// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */

#include <linux/dma-buf.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "stf_isp_ioctl.h"
#include "stf_dmabuf.h"

#define TOTAL_SIZE_LIMIT      (64 * 1024 * 1024)

static size_t total_size;
static struct vb2_queue	vb2_queue = {
	.dma_attrs = 0,
	.gfp_flags = 0,
	.dma_dir = DMA_TO_DEVICE,
};
static struct vb2_buffer vb = {
	.vb2_queue = &vb2_queue,
};

static int dmabuf_create(struct device *dev,
			   struct dmabuf_create *head)
{
	struct dma_buf *dmabuf = NULL;
	void *mem_priv = NULL;
	dma_addr_t *paddr = NULL;
	int ret = 0;

	mem_priv = vb2_dma_contig_memops.alloc(dev, vb.vb2_queue->dma_attrs,
				head->size, vb.vb2_queue->dma_dir, vb.vb2_queue->gfp_flags);
	if (IS_ERR_OR_NULL(mem_priv)) {
		if (mem_priv)
			ret = PTR_ERR(mem_priv);
		goto exit;
	}

	dmabuf = vb2_dma_contig_memops.get_dmabuf(mem_priv, O_RDWR);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free;
	}

	head->fd = dma_buf_fd(dmabuf, O_CLOEXEC);
	if (head->fd < 0) {
		dma_buf_put(dmabuf);
		ret = head->fd;
		goto free;
	}

	paddr = vb2_dma_contig_memops.cookie(mem_priv);
	head->paddr = *paddr;
	return 0;
free:
	vb2_dma_contig_memops.put(mem_priv);
exit:
	return ret;
}

int stf_dmabuf_ioctl_alloc(struct device *dev, void *arg)
{
	struct dmabuf_create *head = arg;
	int ret = -EINVAL;

	if (IS_ERR_OR_NULL(head))
		return -EFAULT;

	head->size = PAGE_ALIGN(head->size);
	if (!head->size)
		return -EINVAL;
	if ((head->size + total_size) > TOTAL_SIZE_LIMIT)
		return -ENOMEM;

	ret = dmabuf_create(dev, head);
	if (ret)
		return -EFAULT;

	total_size += head->size;
	return ret;
}

int stf_dmabuf_ioctl_free(struct device *dev, void *arg)
{
	struct dmabuf_create *head = arg;
	struct dma_buf *dmabuf = NULL;
	int ret = 0;

	if (IS_ERR_OR_NULL(head))
		return -EFAULT;
	if (head->size != PAGE_ALIGN(head->size))
		return -EINVAL;
	if (head->size > total_size)
		return -EINVAL;

	dmabuf = dma_buf_get(head->fd);
	if (IS_ERR_OR_NULL(dmabuf))
		return -EINVAL;

	dma_buf_put(dmabuf);
	vb2_dma_contig_memops.put(dmabuf->priv);
	total_size -= head->size;
	return ret;
}

int stf_dmabuf_ioctl(struct device *dev, unsigned int cmd, void *arg)
{
	int ret = -ENOIOCTLCMD;

	switch (cmd) {
	case VIDIOC_STF_DMABUF_ALLOC:
		ret = stf_dmabuf_ioctl_alloc(dev, arg);
		break;
	case VIDIOC_STF_DMABUF_FREE:
		ret = stf_dmabuf_ioctl_free(dev, arg);
		break;
	default:
		break;
	}
	return ret;
}
