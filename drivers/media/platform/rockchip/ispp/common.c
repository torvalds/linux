// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Rockchip Electronics Co., Ltd */

#include <linux/dma-mapping.h>
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
	buf->vaddr = dma_alloc_coherent(dev->dev, buf->size,
					&buf->dma_addr, GFP_KERNEL);
	if (!buf->vaddr)
		return -ENOMEM;
	return 0;
}

void rkispp_free_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf)
{
	if (buf && buf->vaddr && buf->size) {
		dma_free_coherent(dev->dev, buf->size,
				  buf->vaddr, buf->dma_addr);
		buf->size = 0;
		buf->vaddr = NULL;
	}
}
