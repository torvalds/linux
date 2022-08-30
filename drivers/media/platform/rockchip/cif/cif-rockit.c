// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Rockchip Electronics Co., Ltd */

#define pr_fmt(fmt) "isp_rockit: %s:%d " fmt, __func__, __LINE__

#include <linux/of.h>
#include <linux/of_platform.h>
#include <soc/rockchip/rockchip_rockit.h>

#include "dev.h"

static struct rockit_rkcif_cfg *rockit_rkcif_cfg;

struct rkcif_rockit_buffer {
	struct rkcif_buffer cif_buf;
	struct dma_buf *dmabuf;
	void *mpi_mem;
	void *mpi_buf;
	struct list_head queue;
	int buf_id;
	union {
		u32 buff_addr;
		void *vaddr;
	};
};

static struct rkcif_stream *rkcif_rockit_get_stream(struct rockit_rkcif_cfg *input_rockit_cfg)
{
	struct rkcif_device *cif_dev = NULL;
	struct rkcif_stream *stream = NULL;
	u8 i;

	if (!rockit_rkcif_cfg) {
		pr_err("rockit_rkcif_cfg is null get stream failed\n");
		return NULL;
	}
	if (!input_rockit_cfg) {
		pr_err("input is null get stream failed\n");
		return NULL;
	}

	for (i = 0; i < rockit_rkcif_cfg->cif_num; i++) {
		if (!strcmp(rockit_rkcif_cfg->rkcif_dev_cfg[i].cif_name,
			    input_rockit_cfg->cur_name)) {
			cif_dev = rockit_rkcif_cfg->rkcif_dev_cfg[i].cif_dev;
			break;
		}
	}

	if (cif_dev == NULL) {
		pr_err("Can not find cif_dev!");
		return NULL;
	}

	switch (input_rockit_cfg->nick_id) {
	case 0:
		stream = &cif_dev->stream[RKCIF_STREAM_MIPI_ID0];
		break;
	case 1:
		stream = &cif_dev->stream[RKCIF_STREAM_MIPI_ID1];
		break;
	case 2:
		stream = &cif_dev->stream[RKCIF_STREAM_MIPI_ID2];
		break;
	case 3:
		stream = &cif_dev->stream[RKCIF_STREAM_MIPI_ID3];
		break;
	default:
		stream = NULL;
		break;
	}

	return stream;
}

int rkcif_rockit_buf_queue(struct rockit_rkcif_cfg *input_rockit_cfg)
{
	struct rkcif_stream *stream = NULL;
	struct rkcif_rockit_buffer *rkcif_buf = NULL;
	struct rkcif_device *cif_dev = NULL;
	const struct vb2_mem_ops *g_ops = NULL;
	int i, ret, offset, dev_id;
	struct rkcif_stream_cfg *stream_cfg = NULL;
	void *mem = NULL;
	struct sg_table  *sg_tbl;
	unsigned long lock_flags = 0;

	stream = rkcif_rockit_get_stream(input_rockit_cfg);

	if (stream == NULL) {
		pr_err("the stream is NULL");
		return -EINVAL;
	}

	cif_dev = stream->cifdev;
	dev_id = cif_dev->csi_host_idx;
	g_ops = cif_dev->hw_dev->mem_ops;

	if (stream->id >= RKCIF_MAX_STREAM_MIPI)
		return -EINVAL;

	stream_cfg = &rockit_rkcif_cfg->rkcif_dev_cfg[dev_id].rkcif_stream_cfg[stream->id];

	stream_cfg->node = input_rockit_cfg->node;

	if (!input_rockit_cfg->buf)
		return -EINVAL;

	for (i = 0; i < ROCKIT_BUF_NUM_MAX; i++) {
		if (stream_cfg->buff_id[i] == input_rockit_cfg->mpi_id) {
			input_rockit_cfg->is_alloc = 0;
			break;
		}
	}

	if (input_rockit_cfg->is_alloc) {
		for (i = 0; i < ROCKIT_BUF_NUM_MAX; i++) {
			if (stream_cfg->buff_id[i] == 0) {
				stream_cfg->rkcif_buff[i] =
					kzalloc(sizeof(struct rkcif_rockit_buffer), GFP_KERNEL);
				if (stream_cfg->rkcif_buff[i] == NULL) {
					pr_err("rkisp_buff alloc failed!\n");
					return -EINVAL;
				}
				stream_cfg->buff_id[i] = input_rockit_cfg->mpi_id;
				break;
			}
		}
		if (i == ROCKIT_BUF_NUM_MAX)
			return -EINVAL;

		rkcif_buf = stream_cfg->rkcif_buff[i];
		rkcif_buf->mpi_buf = input_rockit_cfg->mpibuf;

		mem = g_ops->attach_dmabuf(stream->cifdev->hw_dev->dev,
					   input_rockit_cfg->buf,
					   input_rockit_cfg->buf->size,
					   DMA_BIDIRECTIONAL);
		if (IS_ERR(mem))
			pr_err("the g_ops->attach_dmabuf is error!\n");

		rkcif_buf->mpi_mem = mem;
		rkcif_buf->dmabuf = input_rockit_cfg->buf;

		ret = g_ops->map_dmabuf(mem);
		if (ret)
			pr_err("the g_ops->map_dmabuf is error!\n");

		if (stream->cifdev->hw_dev->is_dma_sg_ops) {
			sg_tbl = (struct sg_table *)g_ops->cookie(mem);
			rkcif_buf->buff_addr = sg_dma_address(sg_tbl->sgl);
		} else {
			rkcif_buf->buff_addr = *((u32 *)g_ops->cookie(mem));
		}
		get_dma_buf(input_rockit_cfg->buf);
	} else {
		for (i = 0; i < ROCKIT_BUF_NUM_MAX; i++) {
			rkcif_buf = stream_cfg->rkcif_buff[i];
			if (stream_cfg->buff_id[i] == input_rockit_cfg->mpi_id)
				break;
		}
	}

	for (i = 0; i < stream->cif_fmt_out->mplanes; i++)
		rkcif_buf->cif_buf.buff_addr[i] = rkcif_buf->buff_addr;

	if (stream->cif_fmt_out->mplanes == 1) {
		for (i = 0; i < stream->cif_fmt_out->cplanes - 1; i++) {
			offset = stream->pixm.plane_fmt[i].bytesperline * stream->pixm.height;
			rkcif_buf->cif_buf.buff_addr[i + 1] =
				rkcif_buf->cif_buf.buff_addr[i] + offset;
		}
	}

	v4l2_dbg(2, rkcif_debug, &cif_dev->v4l2_dev,
		 "stream:%d rockit_queue buf:0x%x\n",
		 stream->id, rkcif_buf->cif_buf.buff_addr[0]);

	if (stream_cfg->is_discard)
		return -EINVAL;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	list_add_tail(&rkcif_buf->cif_buf.queue, &stream->rockit_buf_head);
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	return 0;
}

int rkcif_rockit_buf_done(struct rkcif_stream *stream, struct rkcif_buffer *buf)
{
	struct rkcif_device *dev = stream->cifdev;
	struct rkcif_rockit_buffer *rkcif_buf = NULL;
	struct rkcif_stream_cfg *stream_cfg = NULL;
	u32 dev_id = stream->cifdev->csi_host_idx;

	if (!rockit_rkcif_cfg ||
	    !rockit_rkcif_cfg->rkcif_rockit_mpibuf_done ||
	    stream->id >= RKCIF_MAX_STREAM_MIPI)
		return -EINVAL;

	stream_cfg = &rockit_rkcif_cfg->rkcif_dev_cfg[dev_id].rkcif_stream_cfg[stream->id];
	rkcif_buf =
		container_of(buf, struct rkcif_rockit_buffer, cif_buf);

	rockit_rkcif_cfg->mpibuf = rkcif_buf->mpi_buf;
	rockit_rkcif_cfg->buf = rkcif_buf->dmabuf;

	rockit_rkcif_cfg->frame.u64PTS = buf->vb.vb2_buf.timestamp;

	rockit_rkcif_cfg->frame.u32TimeRef = buf->vb.sequence;

	rockit_rkcif_cfg->frame.u32Height = stream->pixm.height;

	rockit_rkcif_cfg->frame.u32Width = stream->pixm.width;

	rockit_rkcif_cfg->frame.enPixelFormat = stream->pixm.pixelformat;

	rockit_rkcif_cfg->frame.u32VirWidth = stream->pixm.width;

	rockit_rkcif_cfg->frame.u32VirHeight = stream->pixm.height;

	rockit_rkcif_cfg->cur_name = dev_name(dev->dev);

	rockit_rkcif_cfg->node = stream_cfg->node;

	if (list_empty(&stream->rockit_buf_head))
		rockit_rkcif_cfg->is_empty = true;
	else
		rockit_rkcif_cfg->is_empty = false;

	if (rockit_rkcif_cfg->rkcif_rockit_mpibuf_done)
		rockit_rkcif_cfg->rkcif_rockit_mpibuf_done(rockit_rkcif_cfg);

	return 0;
}

static int rkcif_rockit_buf_free(struct rkcif_stream *stream)
{
	struct rkcif_rockit_buffer *rkcif_buf = NULL;
	u32 i = 0, dev_id = stream->cifdev->csi_host_idx;
	const struct vb2_mem_ops *g_ops = stream->cifdev->hw_dev->mem_ops;
	struct rkcif_stream_cfg *stream_cfg = NULL;

	if (!rockit_rkcif_cfg || stream->id >= RKCIF_MAX_STREAM_MIPI)
		return -EINVAL;

	stream_cfg = &rockit_rkcif_cfg->rkcif_dev_cfg[dev_id].rkcif_stream_cfg[stream->id];
	stream_cfg->is_discard = false;
	for (i = 0; i < ROCKIT_BUF_NUM_MAX; i++) {
		if (stream_cfg->rkcif_buff[i]) {
			rkcif_buf = (struct rkcif_rockit_buffer *)stream_cfg->rkcif_buff[i];
			if (rkcif_buf->mpi_mem) {
				g_ops->unmap_dmabuf(rkcif_buf->mpi_mem);
				g_ops->detach_dmabuf(rkcif_buf->mpi_mem);
				dma_buf_put(rkcif_buf->dmabuf);
			}
			kfree(stream_cfg->rkcif_buff[i]);
			stream_cfg->rkcif_buff[i] = NULL;
			stream_cfg->buff_id[i] = 0;
		}
	}
	stream->curr_buf_rockit = NULL;
	stream->next_buf_rockit = NULL;
	INIT_LIST_HEAD(&stream->rockit_buf_head);
	return 0;
}

int rkcif_rockit_pause_stream(struct rockit_rkcif_cfg *input_rockit_cfg)
{
	struct rkcif_stream *stream = NULL;

	stream = rkcif_rockit_get_stream(input_rockit_cfg);

	if (stream == NULL) {
		pr_err("the stream is NULL");
		return -EINVAL;
	}

	rkcif_do_stop_stream(stream, RKCIF_STREAM_MODE_ROCKIT);
	rkcif_rockit_buf_free(stream);
	return 0;
}
EXPORT_SYMBOL(rkcif_rockit_pause_stream);

int rkcif_rockit_config_stream(struct rockit_rkcif_cfg *input_rockit_cfg,
				int width, int height, int v4l2_fmt)
{
	struct rkcif_stream *stream = NULL;
	int ret;

	stream = rkcif_rockit_get_stream(input_rockit_cfg);

	if (stream == NULL) {
		pr_err("the stream is NULL");
		return -EINVAL;
	}
	stream->pixm.pixelformat = v4l2_fmt;
	stream->pixm.width = width;
	stream->pixm.height = height;
	stream->pixm.plane_fmt[0].bytesperline = 0;
	ret = rkcif_set_fmt(stream, &stream->pixm, false);
	if (ret < 0) {
		pr_err("stream id %d config failed\n", stream->id);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(rkcif_rockit_config_stream);

int rkcif_rockit_resume_stream(struct rockit_rkcif_cfg *input_rockit_cfg)
{
	struct rkcif_stream *stream = NULL;
	int ret = 0;

	stream = rkcif_rockit_get_stream(input_rockit_cfg);

	if (stream == NULL) {
		pr_err("the stream is NULL");
		return -EINVAL;
	}

	ret = rkcif_do_start_stream(stream, RKCIF_STREAM_MODE_ROCKIT);
	if (ret < 0) {
		pr_err("stream id %d start failed\n", stream->id);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(rkcif_rockit_resume_stream);

void rkcif_rockit_dev_init(struct rkcif_device *dev)
{
	int i;

	if (rockit_rkcif_cfg == NULL) {
		rockit_rkcif_cfg = kzalloc(sizeof(struct rockit_rkcif_cfg), GFP_KERNEL);
		if (rockit_rkcif_cfg == NULL)
			return;
	}
	rockit_rkcif_cfg->cif_num = dev->hw_dev->dev_num;
	for (i = 0; i < rockit_rkcif_cfg->cif_num; i++) {
		if (dev->hw_dev->cif_dev[i]) {
			rockit_rkcif_cfg->rkcif_dev_cfg[i].cif_name = dev_name(dev->hw_dev->cif_dev[i]->dev);
			rockit_rkcif_cfg->rkcif_dev_cfg[i].cif_dev =
				dev->hw_dev->cif_dev[i];
		}
	}
}

void rkcif_rockit_dev_deinit(void)
{
	kfree(rockit_rkcif_cfg);
	rockit_rkcif_cfg = NULL;
}

void *rkcif_rockit_function_register(void *function, int cmd)
{
	if (rockit_rkcif_cfg == NULL) {
		pr_err("rockit_cfg is null function register failed");
		return NULL;
	}

	switch (cmd) {
	case ROCKIT_BUF_QUE:
		function = rkcif_rockit_buf_queue;
		break;
	case ROCKIT_MPIBUF_DONE:
		rockit_rkcif_cfg->rkcif_rockit_mpibuf_done = function;
		if (!rockit_rkcif_cfg->rkcif_rockit_mpibuf_done)
			pr_err("get rkcif_rockit_mpibuf_done failed!");
		break;
	default:
		break;
	}
	return function;
}
EXPORT_SYMBOL(rkcif_rockit_function_register);

int rkcif_rockit_get_cifdev(char **name)
{
	int i = 0;

	if (rockit_rkcif_cfg == NULL) {
		pr_err("rockit_cfg is null");
		return -EINVAL;
	}

	if (name == NULL) {
		pr_err("the name is null");
		return -EINVAL;
	}

	for (i = 0; i < rockit_rkcif_cfg->cif_num; i++)
		name[i] = (char *)rockit_rkcif_cfg->rkcif_dev_cfg[i].cif_name;
	if (name[0] == NULL)
		return -EINVAL;
	else
		return 0;
}
EXPORT_SYMBOL(rkcif_rockit_get_cifdev);
