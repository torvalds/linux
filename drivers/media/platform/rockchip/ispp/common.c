// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Rockchip Electronics Co., Ltd */

#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-mc.h>
#include "dev.h"
#include "regs.h"

int rkispp_fh_open(struct file *filp)
{
	struct rkispp_stream *stream = video_drvdata(filp);
	struct rkispp_device *isppdev = stream->isppdev;
	int ret;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&stream->vnode.vdev.entity, 1);
		if (ret < 0) {
			v4l2_err(&isppdev->v4l2_dev,
				 "pipeline power on failed %d\n", ret);
			vb2_fop_release(filp);
		}
	}
	return ret;
}

int rkispp_fh_release(struct file *filp)
{
	struct rkispp_stream *stream = video_drvdata(filp);
	struct rkispp_device *isppdev = stream->isppdev;
	int ret;

	ret = vb2_fop_release(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&stream->vnode.vdev.entity, 0);
		if (ret < 0)
			v4l2_err(&isppdev->v4l2_dev,
				 "pipeline power off failed %d\n", ret);
	}
	return ret;
}

int rkispp_allow_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf)
{
	const struct vb2_mem_ops *ops = &vb2_dma_contig_memops;
	void *mem_priv;
	int ret = 0;

	if (!buf->size) {
		ret = -EINVAL;
		goto err;
	}

	mem_priv = ops->alloc(dev->dev, 0, buf->size,
			DMA_BIDIRECTIONAL, GFP_KERNEL);
	if (IS_ERR_OR_NULL(mem_priv)) {
		ret = -ENOMEM;
		goto err;
	}

	buf->mem_priv = mem_priv;
	buf->dma_addr = *((dma_addr_t *)ops->cookie(mem_priv));
	buf->vaddr = ops->vaddr(mem_priv);
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s buf:0x%x~0x%x size:%d\n", __func__,
		 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size, buf->size);
	return ret;
err:
	dev_err(dev->dev, "%s failed ret:%d\n", __func__, ret);
	return ret;
}

void rkispp_free_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf)
{
	const struct vb2_mem_ops *ops = &vb2_dma_contig_memops;

	if (buf && buf->mem_priv) {
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
			 "%s buf:0x%x~0x%x\n", __func__,
			 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size);
		ops->put(buf->mem_priv);
		buf->size = 0;
		buf->vaddr = NULL;
		buf->mem_priv = NULL;
	}
}
