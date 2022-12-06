// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Rockchip Electronics Co., Ltd */

#define pr_fmt(fmt) "isp_rockit: %s:%d " fmt, __func__, __LINE__

#include <linux/of.h>
#include <linux/of_platform.h>
#include <soc/rockchip/rockchip_rockit.h>

#include "dev.h"
#include "capture.h"
#include "regs.h"

static struct rockit_cfg *rockit_cfg;

struct rkisp_rockit_buffer {
	struct rkisp_buffer isp_buf;
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

static struct rkisp_stream *rkisp_rockit_get_stream(struct rockit_cfg *input_rockit_cfg)
{
	struct rkisp_device *ispdev = NULL;
	struct rkisp_stream *stream = NULL;
	u8 i;

	if (!rockit_cfg) {
		pr_err("rockit_cfg is null get stream failed\n");
		return NULL;
	}
	if (!input_rockit_cfg) {
		pr_err("input is null get stream failed\n");
		return NULL;
	}

	for (i = 0; i < rockit_cfg->isp_num; i++) {
		if (!strcmp(rockit_cfg->rkisp_dev_cfg[i].isp_name,
			    input_rockit_cfg->current_name)) {
			ispdev = rockit_cfg->rkisp_dev_cfg[i].isp_dev;
			break;
		}
	}

	if (ispdev == NULL) {
		pr_err("Can not find ispdev!");
		return NULL;
	}

	switch (input_rockit_cfg->nick_id) {
	case 0:
		stream = &ispdev->cap_dev.stream[RKISP_STREAM_MP];
		break;
	case 1:
		stream = &ispdev->cap_dev.stream[RKISP_STREAM_SP];
		break;
	case 2:
		stream = &ispdev->cap_dev.stream[RKISP_STREAM_BP];
		break;
	case 3:
		stream = &ispdev->cap_dev.stream[RKISP_STREAM_MPDS];
		break;
	case 4:
		stream = &ispdev->cap_dev.stream[RKISP_STREAM_BPDS];
		break;
	case 5:
		stream = &ispdev->cap_dev.stream[RKISP_STREAM_LUMA];
		break;
	default:
		stream = NULL;
		break;
	}

	return stream;
}

int rkisp_rockit_buf_queue(struct rockit_cfg *input_rockit_cfg)
{
	struct rkisp_stream *stream = NULL;
	struct rkisp_rockit_buffer *isprk_buf = NULL;
	struct rkisp_device *ispdev = NULL;
	const struct vb2_mem_ops *g_ops = NULL;
	int i, ret, height, offset, dev_id;
	struct rkisp_stream_cfg *stream_cfg = NULL;
	void *mem = NULL;
	struct sg_table  *sg_tbl;
	unsigned long lock_flags = 0;

	stream = rkisp_rockit_get_stream(input_rockit_cfg);

	if (stream == NULL) {
		pr_err("the stream is NULL");
		return -EINVAL;
	}

	dev_id = stream->ispdev->dev_id;
	ispdev = stream->ispdev;
	g_ops = ispdev->hw_dev->mem_ops;

	if (stream->id >= ROCKIT_STREAM_NUM_MAX)
		return -EINVAL;

	stream_cfg = &rockit_cfg->rkisp_dev_cfg[dev_id].rkisp_stream_cfg[stream->id];
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
				stream_cfg->rkisp_buff[i] =
					kzalloc(sizeof(struct rkisp_rockit_buffer), GFP_KERNEL);
				if (stream_cfg->rkisp_buff[i] == NULL) {
					pr_err("rkisp_buff alloc failed!\n");
					return -EINVAL;
				}
				stream_cfg->buff_id[i] = input_rockit_cfg->mpi_id;
				break;
			}
		}
		if (i == ROCKIT_BUF_NUM_MAX)
			return -EINVAL;

		isprk_buf = stream_cfg->rkisp_buff[i];
		isprk_buf->mpi_buf = input_rockit_cfg->mpibuf;

		mem = g_ops->attach_dmabuf(stream->ispdev->hw_dev->dev,
					   input_rockit_cfg->buf,
					   input_rockit_cfg->buf->size,
					   DMA_BIDIRECTIONAL);
		if (IS_ERR(mem))
			pr_err("the g_ops->attach_dmabuf is error!\n");

		isprk_buf->mpi_mem = mem;
		isprk_buf->dmabuf = input_rockit_cfg->buf;

		ret = g_ops->map_dmabuf(mem);
		if (ret)
			pr_err("the g_ops->map_dmabuf is error!\n");

		if (stream->ispdev->hw_dev->is_dma_sg_ops) {
			sg_tbl = (struct sg_table *)g_ops->cookie(mem);
			isprk_buf->buff_addr = sg_dma_address(sg_tbl->sgl);
		} else {
			isprk_buf->buff_addr = *((u32 *)g_ops->cookie(mem));
		}
		get_dma_buf(input_rockit_cfg->buf);
	} else {
		for (i = 0; i < ROCKIT_BUF_NUM_MAX; i++) {
			isprk_buf = stream_cfg->rkisp_buff[i];
			if (stream_cfg->buff_id[i] == input_rockit_cfg->mpi_id)
				break;
		}
	}

	for (i = 0; i < stream->out_isp_fmt.mplanes; i++)
		isprk_buf->isp_buf.buff_addr[i] = isprk_buf->buff_addr;

	if (stream->out_isp_fmt.mplanes == 1) {
		for (i = 0; i < stream->out_isp_fmt.cplanes - 1; i++) {
			height = stream->out_fmt.height;
			offset = (i == 0) ?
				stream->out_fmt.plane_fmt[i].bytesperline * height :
				stream->out_fmt.plane_fmt[i].sizeimage;
			isprk_buf->isp_buf.buff_addr[i + 1] =
				isprk_buf->isp_buf.buff_addr[i] + offset;
		}
	}

	v4l2_dbg(2, rkisp_debug, &ispdev->v4l2_dev,
		 "stream:%d rockit_queue buf:0x%x\n",
		 stream->id, isprk_buf->isp_buf.buff_addr[0]);

	if (stream_cfg->is_discard && stream->streaming)
		return -EINVAL;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	/* single sensor with pingpong buf, update next if need */
	if (stream->ispdev->hw_dev->is_single &&
	    stream->id != RKISP_STREAM_VIR &&
	    stream->id != RKISP_STREAM_LUMA &&
	    stream->streaming && !stream->next_buf) {
		stream->next_buf = &isprk_buf->isp_buf;
		stream->ops->update_mi(stream);
	} else {
		list_add_tail(&isprk_buf->isp_buf.queue, &stream->buf_queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	return 0;
}

int rkisp_rockit_buf_done(struct rkisp_stream *stream, int cmd)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_rockit_buffer *isprk_buf = NULL;
	struct rkisp_stream_cfg *stream_cfg = NULL;
	u32 seq, dev_id = stream->ispdev->dev_id;
	u64 ns = 0;

	if (!rockit_cfg ||
	    !rockit_cfg->rkisp_rockit_mpibuf_done ||
	    stream->id >= ROCKIT_STREAM_NUM_MAX)
		return -EINVAL;

	stream_cfg = &rockit_cfg->rkisp_dev_cfg[dev_id].rkisp_stream_cfg[stream->id];
	if (cmd == ROCKIT_DVBM_END) {
		isprk_buf =
			container_of(stream->curr_buf, struct rkisp_rockit_buffer, isp_buf);

		rockit_cfg->mpibuf = isprk_buf->mpi_buf;

		rockit_cfg->frame.u64PTS = stream->curr_buf->vb.vb2_buf.timestamp;

		rockit_cfg->frame.u32TimeRef = stream->curr_buf->vb.sequence;
	} else {
		if (stream->ispdev->cap_dev.wrap_line &&
		    stream->id == RKISP_STREAM_MP) {
			if (stream_cfg->is_discard || stream->ops->is_stream_stopped(stream))
				return 0;
		} else if (stream_cfg->dst_fps) {
			if (!stream_cfg->is_discard && !stream->curr_buf) {
				rockit_cfg->is_qbuf = true;
			} else {
				rockit_cfg->is_qbuf = false;
				return 0;
			}
		} else {
			return 0;
		}

		rkisp_dmarx_get_frame(stream->ispdev, &seq, NULL, &ns, true);

		if (!ns)
			ns = ktime_get_ns();

		rockit_cfg->frame.u64PTS = ns;

		rockit_cfg->frame.u32TimeRef = seq;
	}

	rockit_cfg->is_color = !rkisp_read(dev, ISP3X_IMG_EFF_CTRL, true);

	rockit_cfg->frame.u32Height = stream->out_fmt.height;

	rockit_cfg->frame.u32Width = stream->out_fmt.width;

	rockit_cfg->frame.enPixelFormat = stream->out_fmt.pixelformat;

	rockit_cfg->frame.u32VirWidth = stream->out_fmt.width;

	rockit_cfg->frame.u32VirHeight = stream->out_fmt.height;

	rockit_cfg->current_name = dev->name;

	rockit_cfg->node = stream_cfg->node;

	rockit_cfg->event = cmd;

	if (list_empty(&stream->buf_queue))
		rockit_cfg->is_empty = true;
	else
		rockit_cfg->is_empty = false;

	if (rockit_cfg->rkisp_rockit_mpibuf_done)
		rockit_cfg->rkisp_rockit_mpibuf_done(rockit_cfg);

	return 0;
}

int rkisp_rockit_pause_stream(struct rockit_cfg *input_rockit_cfg)
{
	struct rkisp_stream *stream = NULL;

	stream = rkisp_rockit_get_stream(input_rockit_cfg);

	if (stream == NULL) {
		pr_err("the stream is NULL");
		return -EINVAL;
	}

	rockit_isp_ops.rkisp_stream_stop(stream);

	return 0;
}
EXPORT_SYMBOL(rkisp_rockit_pause_stream);

int rkisp_rockit_config_stream(struct rockit_cfg *input_rockit_cfg,
				int width, int height, int wrap_line)
{
	struct rkisp_stream *stream = NULL;
	struct rkisp_buffer *isp_buf;
	int offset, i, ret;

	stream = rkisp_rockit_get_stream(input_rockit_cfg);

	if (stream == NULL) {
		pr_err("the stream is NULL");
		return -EINVAL;
	}
	stream->ispdev->cap_dev.wrap_line = wrap_line;
	stream->out_fmt.width = width;
	stream->out_fmt.height = height;
	stream->out_fmt.plane_fmt[0].bytesperline = 0;
	ret = rockit_isp_ops.rkisp_set_fmt(stream, &stream->out_fmt, false);
	if (ret < 0) {
		pr_err("stream id %d config failed\n", stream->id);
		return -EINVAL;
	}
	if (stream->ispdev->cap_dev.wrap_line && stream->id == RKISP_STREAM_MP)
		rkisp_dvbm_init(stream);

	if (stream->curr_buf) {
		list_add_tail(&stream->curr_buf->queue, &stream->buf_queue);
		stream->curr_buf = NULL;
	}
	if (stream->next_buf) {
		list_add_tail(&stream->next_buf->queue, &stream->buf_queue);
		stream->next_buf = NULL;
	}

	list_for_each_entry(isp_buf, &stream->buf_queue, queue) {
		if (stream->out_isp_fmt.mplanes == 1) {
			for (i = 0; i < stream->out_isp_fmt.cplanes - 1; i++) {
				height = stream->out_fmt.height;
				offset = (i == 0) ?
					stream->out_fmt.plane_fmt[i].bytesperline * height :
					stream->out_fmt.plane_fmt[i].sizeimage;
				isp_buf->buff_addr[i + 1] =
					isp_buf->buff_addr[i] + offset;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(rkisp_rockit_config_stream);

int rkisp_rockit_resume_stream(struct rockit_cfg *input_rockit_cfg)
{
	struct rkisp_stream *stream = NULL;
	int ret = 0;

	stream = rkisp_rockit_get_stream(input_rockit_cfg);

	if (stream == NULL) {
		pr_err("the stream is NULL");
		return -EINVAL;
	}

	stream->streaming = true;
	ret = rockit_isp_ops.rkisp_stream_start(stream);
	if (ret < 0) {
		pr_err("stream id %d start failed\n", stream->id);
		return -EINVAL;
	}
	if (stream->ispdev->isp_state == ISP_STOP) {
		stream->ispdev->isp_state = ISP_START;
		rkisp_rdbk_trigger_event(stream->ispdev, T_CMD_QUEUE, NULL);
	}

	return 0;
}
EXPORT_SYMBOL(rkisp_rockit_resume_stream);

int rkisp_rockit_get_tb_stream_info(struct rockit_cfg *input_rockit_cfg,
				    struct rkisp_tb_stream_info *info)
{
	struct rkisp_stream *stream;

	if (!input_rockit_cfg || !info)
		return -EINVAL;

	stream = rkisp_rockit_get_stream(input_rockit_cfg);
	if (!stream)
		return -EINVAL;
	if (info->buf[0].timestamp)
		stream->is_using_resmem = false;
	return rkisp_get_tb_stream_info(stream, info);
}
EXPORT_SYMBOL(rkisp_rockit_get_tb_stream_info);

int rkisp_rockit_free_tb_stream_buf(struct rockit_cfg *input_rockit_cfg)
{
	struct rkisp_stream *stream;

	if (!input_rockit_cfg)
		return -EINVAL;
	stream = rkisp_rockit_get_stream(input_rockit_cfg);
	if (!stream)
		return -EINVAL;

	return rkisp_free_tb_stream_buf(stream);
}
EXPORT_SYMBOL(rkisp_rockit_free_tb_stream_buf);

int rkisp_rockit_buf_free(struct rkisp_stream *stream)
{
	struct rkisp_rockit_buffer *isprk_buf = NULL;
	u32 i = 0, dev_id = stream->ispdev->dev_id;
	const struct vb2_mem_ops *g_ops = stream->ispdev->hw_dev->mem_ops;
	struct rkisp_stream_cfg *stream_cfg = NULL;

	if (!rockit_cfg || stream->id >= ROCKIT_STREAM_NUM_MAX)
		return -EINVAL;

	stream_cfg = &rockit_cfg->rkisp_dev_cfg[dev_id].rkisp_stream_cfg[stream->id];
	stream_cfg->is_discard = false;
	for (i = 0; i < ROCKIT_BUF_NUM_MAX; i++) {
		if (stream_cfg->rkisp_buff[i]) {
			isprk_buf = (struct rkisp_rockit_buffer *)stream_cfg->rkisp_buff[i];
			if (isprk_buf->mpi_mem) {
				g_ops->unmap_dmabuf(isprk_buf->mpi_mem);
				g_ops->detach_dmabuf(isprk_buf->mpi_mem);
				dma_buf_put(isprk_buf->dmabuf);
			}
			kfree(stream_cfg->rkisp_buff[i]);
			stream_cfg->rkisp_buff[i] = NULL;
			stream_cfg->buff_id[i] = 0;
		}
	}
	return 0;
}

void rkisp_rockit_dev_init(struct rkisp_device *dev)
{
	int i;

	if (rockit_cfg == NULL) {
		rockit_cfg = kzalloc(sizeof(struct rockit_cfg), GFP_KERNEL);
		if (rockit_cfg == NULL)
			return;
	}
	rockit_cfg->isp_num = dev->hw_dev->dev_num;
	for (i = 0; i < rockit_cfg->isp_num; i++) {
		if (dev->hw_dev->isp[i]) {
			rockit_cfg->rkisp_dev_cfg[i].isp_name =
				dev->hw_dev->isp[i]->name;
			rockit_cfg->rkisp_dev_cfg[i].isp_dev =
				dev->hw_dev->isp[i];
		}
	}
}

void rkisp_rockit_dev_deinit(void)
{
	if (rockit_cfg) {
		kfree(rockit_cfg);
		rockit_cfg = NULL;
	}
}

int rkisp_rockit_fps_set(int *dst_fps, struct rkisp_stream *stream)
{
	int dev_id = stream->ispdev->dev_id;
	int id = stream->id;

	if (stream->id >= ROCKIT_STREAM_NUM_MAX) {
		pr_err("fps_set stream id %u exceeds maximum\n", id);
		return -EINVAL;
	}

	if (dst_fps == NULL) {
		pr_err("fps_set dst_fps is null\n");
		return -EINVAL;
	}

	if (!rockit_cfg)
		return -EINVAL;

	rockit_cfg->rkisp_dev_cfg[dev_id].rkisp_stream_cfg[id].dst_fps = *dst_fps;
	rockit_cfg->rkisp_dev_cfg[dev_id].rkisp_stream_cfg[id].fps_cnt = *dst_fps;
	return 0;
}

int rkisp_rockit_fps_get(int *dst_fps, struct rkisp_stream *stream)
{
	int dev_id = stream->ispdev->dev_id;
	int id = stream->id;

	if (id >= ROCKIT_STREAM_NUM_MAX) {
		pr_err("fps_get stream id %u exceeds maximum\n", id);
		return -EINVAL;
	}

	if (dst_fps == NULL) {
		pr_err("fps_get dst_fps is null\n");
		return -EINVAL;
	}

	if (!rockit_cfg)
		return -EINVAL;

	*dst_fps = rockit_cfg->rkisp_dev_cfg[dev_id].rkisp_stream_cfg[id].cur_fps;
	return 0;
}

bool rkisp_rockit_ctrl_fps(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_sensor_info *sensor = NULL;
	int dev_id = stream->ispdev->dev_id, id = stream->id;
	struct rkisp_stream_cfg *stream_cfg;
	int ret, dst_fps, *fps_cnt;
	static int fps_in, cur_fps[ROCKIT_STREAM_NUM_MAX];
	u32 denominator = 0, numerator = 0;
	bool *is_discard;
	u64 cur_time, *old_time;

	if (!rockit_cfg || stream->id >= ROCKIT_STREAM_NUM_MAX)
		return false;
	stream_cfg = &rockit_cfg->rkisp_dev_cfg[dev_id].rkisp_stream_cfg[id];
	fps_cnt = &stream_cfg->fps_cnt;
	is_discard = &stream_cfg->is_discard;
	old_time = &stream_cfg->old_time;
	dst_fps = stream_cfg->dst_fps;
	if (dst_fps == 0 || !stream->streaming) {
		*is_discard  = false;
		return false;
	}

	if (dev->active_sensor == NULL) {
		*is_discard  = false;
		pr_err("the sensor is not found\n");
		return false;
	}

	sensor = dev->active_sensor;

	ret = v4l2_subdev_call(sensor->sd, video, g_frame_interval, &sensor->fi);
	if (!ret) {
		denominator = sensor->fi.interval.denominator;
		numerator = sensor->fi.interval.numerator;
		if (numerator)
			fps_in = denominator / numerator;
		else {
			*is_discard  = false;
			pr_err("the numerator is 0\n");
			return false;
		}
	}

	if (dst_fps >= fps_in)
		return false;

	if ((fps_in > 0) && (dst_fps > 0)) {
		if (*fps_cnt < 0)
			*fps_cnt = fps_in - dst_fps;
		*fps_cnt += dst_fps;

		if (*fps_cnt < fps_in)
			*is_discard = true;
		else {
			*fps_cnt -= fps_in;
			*is_discard = false;
			++cur_fps[stream->id];
			cur_time = ktime_get_ns();
			if (cur_time - *old_time >= 1000000000) {
				*old_time = cur_time;
				stream_cfg->cur_fps = cur_fps[stream->id];
				cur_fps[stream->id] = 0;
			}
		}
	} else {
		*is_discard  = false;
	}
	return true;
}

void *rkisp_rockit_function_register(void *function, int cmd)
{
	if (rockit_cfg == NULL) {
		pr_err("rockit_cfg is null function register failed");
		return NULL;
	}

	switch (cmd) {
	case ROCKIT_BUF_QUE:
		function = rkisp_rockit_buf_queue;
		break;
	case ROCKIT_MPIBUF_DONE:
		rockit_cfg->rkisp_rockit_mpibuf_done = function;
		if (!rockit_cfg->rkisp_rockit_mpibuf_done)
			pr_err("get rkisp_rockit_buf_queue failed!");
		break;
	default:
		break;
	}
	return function;
}
EXPORT_SYMBOL(rkisp_rockit_function_register);

int rkisp_rockit_get_ispdev(char **name)
{
	int i = 0;

	if (rockit_cfg == NULL) {
		pr_err("rockit_cfg is null");
		return -EINVAL;
	}

	if (name == NULL) {
		pr_err("the name is null");
		return -EINVAL;
	}

	for (i = 0; i < rockit_cfg->isp_num; i++)
		name[i] = rockit_cfg->rkisp_dev_cfg[i].isp_name;
	if (name[0] == NULL)
		return -EINVAL;
	else
		return 0;
}
EXPORT_SYMBOL(rkisp_rockit_get_ispdev);
