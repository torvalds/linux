// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */
#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-v4l2.h>

#include "ipu6-bus.h"
#include "ipu6-dma.h"
#include "ipu6-fw-isys.h"
#include "ipu6-isys.h"
#include "ipu6-isys-video.h"

static int ipu6_isys_buf_init(struct vb2_buffer *vb)
{
	struct ipu6_isys *isys = vb2_get_drv_priv(vb->vb2_queue);
	struct sg_table *sg = vb2_dma_sg_plane_desc(vb, 0);
	struct vb2_v4l2_buffer *vvb = to_vb2_v4l2_buffer(vb);
	struct ipu6_isys_video_buffer *ivb =
		vb2_buffer_to_ipu6_isys_video_buffer(vvb);
	int ret;

	ret = ipu6_dma_map_sgtable(isys->adev, sg, DMA_TO_DEVICE, 0);
	if (ret)
		return ret;

	ivb->dma_addr = sg_dma_address(sg->sgl);

	return 0;
}

static void ipu6_isys_buf_cleanup(struct vb2_buffer *vb)
{
	struct ipu6_isys *isys = vb2_get_drv_priv(vb->vb2_queue);
	struct sg_table *sg = vb2_dma_sg_plane_desc(vb, 0);
	struct vb2_v4l2_buffer *vvb = to_vb2_v4l2_buffer(vb);
	struct ipu6_isys_video_buffer *ivb =
		vb2_buffer_to_ipu6_isys_video_buffer(vvb);

	ivb->dma_addr = 0;
	ipu6_dma_unmap_sgtable(isys->adev, sg, DMA_TO_DEVICE, 0);
}

static int ipu6_isys_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
				 unsigned int *num_planes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	struct ipu6_isys_queue *aq = vb2_queue_to_isys_queue(q);
	struct ipu6_isys_video *av = ipu6_isys_queue_to_video(aq);
	struct device *dev = &av->isys->adev->auxdev.dev;
	u32 size = ipu6_isys_get_data_size(av);

	/* num_planes == 0: we're being called through VIDIOC_REQBUFS */
	if (!*num_planes) {
		sizes[0] = size;
	} else if (sizes[0] < size) {
		dev_dbg(dev, "%s: queue setup: size %u < %u\n",
			av->vdev.name, sizes[0], size);
		return -EINVAL;
	}

	*num_planes = 1;

	return 0;
}

static int ipu6_isys_buf_prepare(struct vb2_buffer *vb)
{
	struct ipu6_isys_queue *aq = vb2_queue_to_isys_queue(vb->vb2_queue);
	struct ipu6_isys_video *av = ipu6_isys_queue_to_video(aq);
	struct device *dev = &av->isys->adev->auxdev.dev;
	u32 bytesperline = ipu6_isys_get_bytes_per_line(av);
	u32 height = ipu6_isys_get_frame_height(av);
	u32 size = ipu6_isys_get_data_size(av);

	dev_dbg(dev, "buffer: %s: configured size %u, buffer size %lu\n",
		av->vdev.name, size, vb2_plane_size(vb, 0));

	if (size > vb2_plane_size(vb, 0))
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, bytesperline * height);

	return 0;
}

/*
 * Queue a buffer list back to incoming or active queues. The buffers
 * are removed from the buffer list.
 */
void ipu6_isys_buffer_list_queue(struct ipu6_isys_buffer_list *bl,
				 unsigned long op_flags,
				 enum vb2_buffer_state state)
{
	struct ipu6_isys_buffer *ib, *ib_safe;
	unsigned long flags;
	bool first = true;

	if (!bl)
		return;

	WARN_ON_ONCE(!bl->nbufs);
	WARN_ON_ONCE(op_flags & IPU6_ISYS_BUFFER_LIST_FL_ACTIVE &&
		     op_flags & IPU6_ISYS_BUFFER_LIST_FL_INCOMING);

	list_for_each_entry_safe(ib, ib_safe, &bl->head, head) {
		struct ipu6_isys_video *av;
		struct vb2_buffer *vb = ipu6_isys_buffer_to_vb2_buffer(ib);
		struct ipu6_isys_queue *aq =
			vb2_queue_to_isys_queue(vb->vb2_queue);
		struct device *dev;

		av = ipu6_isys_queue_to_video(aq);
		dev = &av->isys->adev->auxdev.dev;
		spin_lock_irqsave(&aq->lock, flags);
		list_del(&ib->head);
		if (op_flags & IPU6_ISYS_BUFFER_LIST_FL_ACTIVE)
			list_add(&ib->head, &aq->active);
		else if (op_flags & IPU6_ISYS_BUFFER_LIST_FL_INCOMING)
			list_add_tail(&ib->head, &aq->incoming);
		spin_unlock_irqrestore(&aq->lock, flags);

		if (op_flags & IPU6_ISYS_BUFFER_LIST_FL_SET_STATE)
			vb2_buffer_done(vb, state);

		if (first) {
			dev_dbg(dev,
				"queue buf list %p flags %lx, s %d, %d bufs\n",
				bl, op_flags, state, bl->nbufs);
			first = false;
		}

		bl->nbufs--;
	}

	WARN_ON(bl->nbufs);
}

/*
 * flush_firmware_streamon_fail() - Flush in cases where requests may
 * have been queued to firmware and the *firmware streamon fails for a
 * reason or another.
 */
static void flush_firmware_streamon_fail(struct ipu6_isys_stream *stream)
{
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct ipu6_isys_queue *aq;
	unsigned long flags;

	lockdep_assert_held(&stream->mutex);

	list_for_each_entry(aq, &stream->queues, node) {
		struct ipu6_isys_video *av = ipu6_isys_queue_to_video(aq);
		struct ipu6_isys_buffer *ib, *ib_safe;

		spin_lock_irqsave(&aq->lock, flags);
		list_for_each_entry_safe(ib, ib_safe, &aq->active, head) {
			struct vb2_buffer *vb =
				ipu6_isys_buffer_to_vb2_buffer(ib);

			list_del(&ib->head);
			if (av->streaming) {
				dev_dbg(dev,
					"%s: queue buffer %u back to incoming\n",
					av->vdev.name, vb->index);
				/* Queue already streaming, return to driver. */
				list_add(&ib->head, &aq->incoming);
				continue;
			}
			/* Queue not yet streaming, return to user. */
			dev_dbg(dev, "%s: return %u back to videobuf2\n",
				av->vdev.name, vb->index);
			vb2_buffer_done(ipu6_isys_buffer_to_vb2_buffer(ib),
					VB2_BUF_STATE_QUEUED);
		}
		spin_unlock_irqrestore(&aq->lock, flags);
	}
}

/*
 * Attempt obtaining a buffer list from the incoming queues, a list of buffers
 * that contains one entry from each video buffer queue. If a buffer can't be
 * obtained from every queue, the buffers are returned back to the queue.
 */
static int buffer_list_get(struct ipu6_isys_stream *stream,
			   struct ipu6_isys_buffer_list *bl)
{
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct ipu6_isys_queue *aq;
	unsigned long flags;
	unsigned long buf_flag = IPU6_ISYS_BUFFER_LIST_FL_INCOMING;

	bl->nbufs = 0;
	INIT_LIST_HEAD(&bl->head);

	list_for_each_entry(aq, &stream->queues, node) {
		struct ipu6_isys_buffer *ib;

		spin_lock_irqsave(&aq->lock, flags);
		if (list_empty(&aq->incoming)) {
			spin_unlock_irqrestore(&aq->lock, flags);
			if (!list_empty(&bl->head))
				ipu6_isys_buffer_list_queue(bl, buf_flag, 0);
			return -ENODATA;
		}

		ib = list_last_entry(&aq->incoming,
				     struct ipu6_isys_buffer, head);

		dev_dbg(dev, "buffer: %s: buffer %u\n",
			ipu6_isys_queue_to_video(aq)->vdev.name,
			ipu6_isys_buffer_to_vb2_buffer(ib)->index);
		list_del(&ib->head);
		list_add(&ib->head, &bl->head);
		spin_unlock_irqrestore(&aq->lock, flags);

		bl->nbufs++;
	}

	dev_dbg(dev, "get buffer list %p, %u buffers\n", bl, bl->nbufs);

	return 0;
}

static void
ipu6_isys_buf_to_fw_frame_buf_pin(struct vb2_buffer *vb,
				  struct ipu6_fw_isys_frame_buff_set_abi *set)
{
	struct ipu6_isys_queue *aq = vb2_queue_to_isys_queue(vb->vb2_queue);
	struct vb2_v4l2_buffer *vvb = to_vb2_v4l2_buffer(vb);
	struct ipu6_isys_video_buffer *ivb =
		vb2_buffer_to_ipu6_isys_video_buffer(vvb);

	set->output_pins[aq->fw_output].addr = ivb->dma_addr;
	set->output_pins[aq->fw_output].out_buf_id = vb->index + 1;
}

/*
 * Convert a buffer list to a isys fw ABI framebuffer set. The
 * buffer list is not modified.
 */
#define IPU6_ISYS_FRAME_NUM_THRESHOLD  (30)
void
ipu6_isys_buf_to_fw_frame_buf(struct ipu6_fw_isys_frame_buff_set_abi *set,
			      struct ipu6_isys_stream *stream,
			      struct ipu6_isys_buffer_list *bl)
{
	struct ipu6_isys_buffer *ib;

	WARN_ON(!bl->nbufs);

	set->send_irq_sof = 1;
	set->send_resp_sof = 1;
	set->send_irq_eof = 0;
	set->send_resp_eof = 0;

	if (stream->streaming)
		set->send_irq_capture_ack = 0;
	else
		set->send_irq_capture_ack = 1;
	set->send_irq_capture_done = 0;

	set->send_resp_capture_ack = 1;
	set->send_resp_capture_done = 1;
	if (atomic_read(&stream->sequence) >= IPU6_ISYS_FRAME_NUM_THRESHOLD) {
		set->send_resp_capture_ack = 0;
		set->send_resp_capture_done = 0;
	}

	list_for_each_entry(ib, &bl->head, head) {
		struct vb2_buffer *vb = ipu6_isys_buffer_to_vb2_buffer(ib);

		ipu6_isys_buf_to_fw_frame_buf_pin(vb, set);
	}
}

/* Start streaming for real. The buffer list must be available. */
static int ipu6_isys_stream_start(struct ipu6_isys_video *av,
				  struct ipu6_isys_buffer_list *bl, bool error)
{
	struct ipu6_isys_stream *stream = av->stream;
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct ipu6_isys_buffer_list __bl;
	int ret;

	mutex_lock(&stream->isys->stream_mutex);
	ret = ipu6_isys_video_set_streaming(av, 1, bl);
	mutex_unlock(&stream->isys->stream_mutex);
	if (ret)
		goto out_requeue;

	stream->streaming = 1;

	bl = &__bl;

	do {
		struct ipu6_fw_isys_frame_buff_set_abi *buf = NULL;
		struct isys_fw_msgs *msg;
		u16 send_type = IPU6_FW_ISYS_SEND_TYPE_STREAM_CAPTURE;

		ret = buffer_list_get(stream, bl);
		if (ret < 0)
			break;

		msg = ipu6_get_fw_msg_buf(stream);
		if (!msg)
			return -ENOMEM;

		buf = &msg->fw_msg.frame;
		ipu6_isys_buf_to_fw_frame_buf(buf, stream, bl);
		ipu6_fw_isys_dump_frame_buff_set(dev, buf,
						 stream->nr_output_pins);
		ipu6_isys_buffer_list_queue(bl, IPU6_ISYS_BUFFER_LIST_FL_ACTIVE,
					    0);
		ret = ipu6_fw_isys_complex_cmd(stream->isys,
					       stream->stream_handle, buf,
					       msg->dma_addr, sizeof(*buf),
					       send_type);
	} while (!WARN_ON(ret));

	return 0;

out_requeue:
	if (bl && bl->nbufs)
		ipu6_isys_buffer_list_queue(bl,
					    IPU6_ISYS_BUFFER_LIST_FL_INCOMING |
					    (error ?
					    IPU6_ISYS_BUFFER_LIST_FL_SET_STATE :
					     0), error ? VB2_BUF_STATE_ERROR :
					    VB2_BUF_STATE_QUEUED);
	flush_firmware_streamon_fail(stream);

	return ret;
}

static void buf_queue(struct vb2_buffer *vb)
{
	struct ipu6_isys_queue *aq = vb2_queue_to_isys_queue(vb->vb2_queue);
	struct ipu6_isys_video *av = ipu6_isys_queue_to_video(aq);
	struct vb2_v4l2_buffer *vvb = to_vb2_v4l2_buffer(vb);
	struct ipu6_isys_video_buffer *ivb =
		vb2_buffer_to_ipu6_isys_video_buffer(vvb);
	struct ipu6_isys_buffer *ib = &ivb->ib;
	struct device *dev = &av->isys->adev->auxdev.dev;
	struct media_pipeline *media_pipe =
		media_entity_pipeline(&av->vdev.entity);
	struct ipu6_fw_isys_frame_buff_set_abi *buf = NULL;
	struct ipu6_isys_stream *stream = av->stream;
	struct ipu6_isys_buffer_list bl;
	struct isys_fw_msgs *msg;
	unsigned long flags;
	dma_addr_t dma;
	int ret;

	dev_dbg(dev, "queue buffer %u for %s\n", vb->index, av->vdev.name);

	dma = ivb->dma_addr;
	dev_dbg(dev, "iova: iova %pad\n", &dma);

	spin_lock_irqsave(&aq->lock, flags);
	list_add(&ib->head, &aq->incoming);
	spin_unlock_irqrestore(&aq->lock, flags);

	if (!media_pipe || !vb->vb2_queue->start_streaming_called) {
		dev_dbg(dev, "media pipeline is not ready for %s\n",
			av->vdev.name);
		return;
	}

	mutex_lock(&stream->mutex);

	if (stream->nr_streaming != stream->nr_queues) {
		dev_dbg(dev, "not streaming yet, adding to incoming\n");
		goto out;
	}

	/*
	 * We just put one buffer to the incoming list of this queue
	 * (above). Let's see whether all queues in the pipeline would
	 * have a buffer.
	 */
	ret = buffer_list_get(stream, &bl);
	if (ret < 0) {
		dev_dbg(dev, "No buffers available\n");
		goto out;
	}

	msg = ipu6_get_fw_msg_buf(stream);
	if (!msg) {
		ret = -ENOMEM;
		goto out;
	}

	buf = &msg->fw_msg.frame;
	ipu6_isys_buf_to_fw_frame_buf(buf, stream, &bl);
	ipu6_fw_isys_dump_frame_buff_set(dev, buf, stream->nr_output_pins);

	if (!stream->streaming) {
		ret = ipu6_isys_stream_start(av, &bl, true);
		if (ret)
			dev_err(dev, "stream start failed.\n");
		goto out;
	}

	/*
	 * We must queue the buffers in the buffer list to the
	 * appropriate video buffer queues BEFORE passing them to the
	 * firmware since we could get a buffer event back before we
	 * have queued them ourselves to the active queue.
	 */
	ipu6_isys_buffer_list_queue(&bl, IPU6_ISYS_BUFFER_LIST_FL_ACTIVE, 0);

	ret = ipu6_fw_isys_complex_cmd(stream->isys, stream->stream_handle,
				       buf, msg->dma_addr, sizeof(*buf),
				       IPU6_FW_ISYS_SEND_TYPE_STREAM_CAPTURE);
	if (ret < 0)
		dev_err(dev, "send stream capture failed\n");

out:
	mutex_unlock(&stream->mutex);
}

static int ipu6_isys_link_fmt_validate(struct ipu6_isys_queue *aq)
{
	struct v4l2_mbus_framefmt format;
	struct ipu6_isys_video *av = ipu6_isys_queue_to_video(aq);
	struct device *dev = &av->isys->adev->auxdev.dev;
	struct media_pad *remote_pad =
		media_pad_remote_pad_first(av->vdev.entity.pads);
	struct v4l2_subdev *sd;
	u32 r_stream, code;
	int ret;

	if (!remote_pad)
		return -ENOTCONN;

	sd = media_entity_to_v4l2_subdev(remote_pad->entity);
	r_stream = ipu6_isys_get_src_stream_by_src_pad(sd, remote_pad->index);

	ret = ipu6_isys_get_stream_pad_fmt(sd, remote_pad->index, r_stream,
					   &format);

	if (ret) {
		dev_dbg(dev, "failed to get %s: pad %d, stream:%d format\n",
			sd->entity.name, remote_pad->index, r_stream);
		return ret;
	}

	if (format.width != ipu6_isys_get_frame_width(av) ||
	    format.height != ipu6_isys_get_frame_height(av)) {
		dev_dbg(dev, "wrong width or height %ux%u (%ux%u expected)\n",
			ipu6_isys_get_frame_width(av),
			ipu6_isys_get_frame_height(av), format.width,
			format.height);
		return -EINVAL;
	}

	code = ipu6_isys_get_isys_format(ipu6_isys_get_format(av), 0)->code;
	if (format.code != code) {
		dev_dbg(dev, "wrong mbus code 0x%8.8x (0x%8.8x expected)\n",
			code, format.code);
		return -EINVAL;
	}

	return 0;
}

static void return_buffers(struct ipu6_isys_queue *aq,
			   enum vb2_buffer_state state)
{
	struct ipu6_isys_video *av = ipu6_isys_queue_to_video(aq);
	struct ipu6_isys_buffer *ib;
	bool need_reset = false;
	unsigned long flags;

	spin_lock_irqsave(&aq->lock, flags);
	while (!list_empty(&aq->incoming)) {
		struct vb2_buffer *vb;

		ib = list_first_entry(&aq->incoming, struct ipu6_isys_buffer,
				      head);
		vb = ipu6_isys_buffer_to_vb2_buffer(ib);
		list_del(&ib->head);
		spin_unlock_irqrestore(&aq->lock, flags);

		vb2_buffer_done(vb, state);

		spin_lock_irqsave(&aq->lock, flags);
	}

	/*
	 * Something went wrong (FW crash / HW hang / not all buffers
	 * returned from isys) if there are still buffers queued in active
	 * queue. We have to clean up places a bit.
	 */
	while (!list_empty(&aq->active)) {
		struct vb2_buffer *vb;

		ib = list_first_entry(&aq->active, struct ipu6_isys_buffer,
				      head);
		vb = ipu6_isys_buffer_to_vb2_buffer(ib);

		list_del(&ib->head);
		spin_unlock_irqrestore(&aq->lock, flags);

		vb2_buffer_done(vb, state);

		spin_lock_irqsave(&aq->lock, flags);
		need_reset = true;
	}

	spin_unlock_irqrestore(&aq->lock, flags);

	if (need_reset) {
		mutex_lock(&av->isys->mutex);
		av->isys->need_reset = true;
		mutex_unlock(&av->isys->mutex);
	}
}

static void ipu6_isys_stream_cleanup(struct ipu6_isys_video *av)
{
	video_device_pipeline_stop(&av->vdev);
	ipu6_isys_put_stream(av->stream);
	av->stream = NULL;
}

static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct ipu6_isys_queue *aq = vb2_queue_to_isys_queue(q);
	struct ipu6_isys_video *av = ipu6_isys_queue_to_video(aq);
	struct device *dev = &av->isys->adev->auxdev.dev;
	const struct ipu6_isys_pixelformat *pfmt =
		ipu6_isys_get_isys_format(ipu6_isys_get_format(av), 0);
	struct ipu6_isys_buffer_list __bl, *bl = NULL;
	struct ipu6_isys_stream *stream;
	struct media_entity *source_entity = NULL;
	int nr_queues, ret;

	dev_dbg(dev, "stream: %s: width %u, height %u, css pixelformat %u\n",
		av->vdev.name, ipu6_isys_get_frame_width(av),
		ipu6_isys_get_frame_height(av), pfmt->css_pixelformat);

	ret = ipu6_isys_setup_video(av, &source_entity, &nr_queues);
	if (ret < 0) {
		dev_dbg(dev, "failed to setup video\n");
		goto out_return_buffers;
	}

	ret = ipu6_isys_link_fmt_validate(aq);
	if (ret) {
		dev_dbg(dev,
			"%s: link format validation failed (%d)\n",
			av->vdev.name, ret);
		goto out_pipeline_stop;
	}

	ret = ipu6_isys_fw_open(av->isys);
	if (ret)
		goto out_pipeline_stop;

	stream = av->stream;
	mutex_lock(&stream->mutex);
	if (!stream->nr_streaming) {
		ret = ipu6_isys_video_prepare_stream(av, source_entity,
						     nr_queues);
		if (ret)
			goto out_fw_close;
	}

	stream->nr_streaming++;
	dev_dbg(dev, "queue %u of %u\n", stream->nr_streaming,
		stream->nr_queues);

	list_add(&aq->node, &stream->queues);
	ipu6_isys_configure_stream_watermark(av, true);
	ipu6_isys_update_stream_watermark(av, true);

	if (stream->nr_streaming != stream->nr_queues)
		goto out;

	bl = &__bl;
	ret = buffer_list_get(stream, bl);
	if (ret < 0) {
		dev_warn(dev, "no buffer available, DRIVER BUG?\n");
		goto out;
	}

	ret = ipu6_isys_stream_start(av, bl, false);
	if (ret)
		goto out_stream_start;

out:
	mutex_unlock(&stream->mutex);

	return 0;

out_stream_start:
	ipu6_isys_update_stream_watermark(av, false);
	list_del(&aq->node);
	stream->nr_streaming--;

out_fw_close:
	mutex_unlock(&stream->mutex);
	ipu6_isys_fw_close(av->isys);

out_pipeline_stop:
	ipu6_isys_stream_cleanup(av);

out_return_buffers:
	return_buffers(aq, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void stop_streaming(struct vb2_queue *q)
{
	struct ipu6_isys_queue *aq = vb2_queue_to_isys_queue(q);
	struct ipu6_isys_video *av = ipu6_isys_queue_to_video(aq);
	struct ipu6_isys_stream *stream = av->stream;

	mutex_lock(&stream->mutex);

	ipu6_isys_update_stream_watermark(av, false);

	mutex_lock(&av->isys->stream_mutex);
	if (stream->nr_streaming == stream->nr_queues && stream->streaming)
		ipu6_isys_video_set_streaming(av, 0, NULL);
	mutex_unlock(&av->isys->stream_mutex);

	stream->nr_streaming--;
	list_del(&aq->node);
	stream->streaming = 0;
	mutex_unlock(&stream->mutex);

	ipu6_isys_stream_cleanup(av);

	return_buffers(aq, VB2_BUF_STATE_ERROR);

	ipu6_isys_fw_close(av->isys);
}

static unsigned int
get_sof_sequence_by_timestamp(struct ipu6_isys_stream *stream,
			      struct ipu6_fw_isys_resp_info_abi *info)
{
	u64 time = (u64)info->timestamp[1] << 32 | info->timestamp[0];
	struct ipu6_isys *isys = stream->isys;
	struct device *dev = &isys->adev->auxdev.dev;
	unsigned int i;

	/*
	 * The timestamp is invalid as no TSC in some FPGA platform,
	 * so get the sequence from pipeline directly in this case.
	 */
	if (time == 0)
		return atomic_read(&stream->sequence) - 1;

	for (i = 0; i < IPU6_ISYS_MAX_PARALLEL_SOF; i++)
		if (time == stream->seq[i].timestamp) {
			dev_dbg(dev, "sof: using seq nr %u for ts %llu\n",
				stream->seq[i].sequence, time);
			return stream->seq[i].sequence;
		}

	for (i = 0; i < IPU6_ISYS_MAX_PARALLEL_SOF; i++)
		dev_dbg(dev, "sof: sequence %u, timestamp value %llu\n",
			stream->seq[i].sequence, stream->seq[i].timestamp);

	return 0;
}

static u64 get_sof_ns_delta(struct ipu6_isys_video *av,
			    struct ipu6_fw_isys_resp_info_abi *info)
{
	struct ipu6_bus_device *adev = av->isys->adev;
	struct ipu6_device *isp = adev->isp;
	u64 delta, tsc_now;

	ipu6_buttress_tsc_read(isp, &tsc_now);
	if (!tsc_now)
		return 0;

	delta = tsc_now - ((u64)info->timestamp[1] << 32 | info->timestamp[0]);

	return ipu6_buttress_tsc_ticks_to_ns(delta, isp);
}

void ipu6_isys_buf_calc_sequence_time(struct ipu6_isys_buffer *ib,
				      struct ipu6_fw_isys_resp_info_abi *info)
{
	struct vb2_buffer *vb = ipu6_isys_buffer_to_vb2_buffer(ib);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct ipu6_isys_queue *aq = vb2_queue_to_isys_queue(vb->vb2_queue);
	struct ipu6_isys_video *av = ipu6_isys_queue_to_video(aq);
	struct device *dev = &av->isys->adev->auxdev.dev;
	struct ipu6_isys_stream *stream = av->stream;
	u64 ns;
	u32 sequence;

	ns = ktime_get_ns() - get_sof_ns_delta(av, info);
	sequence = get_sof_sequence_by_timestamp(stream, info);

	vbuf->vb2_buf.timestamp = ns;
	vbuf->sequence = sequence;

	dev_dbg(dev, "buf: %s: buffer done, CPU-timestamp:%lld, sequence:%d\n",
		av->vdev.name, ktime_get_ns(), sequence);
	dev_dbg(dev, "index:%d, vbuf timestamp:%lld\n", vb->index,
		vbuf->vb2_buf.timestamp);
}

void ipu6_isys_queue_buf_done(struct ipu6_isys_buffer *ib)
{
	struct vb2_buffer *vb = ipu6_isys_buffer_to_vb2_buffer(ib);

	if (atomic_read(&ib->str2mmio_flag)) {
		vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		/*
		 * Operation on buffer is ended with error and will be reported
		 * to the userspace when it is de-queued
		 */
		atomic_set(&ib->str2mmio_flag, 0);
	} else {
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	}
}

void ipu6_isys_queue_buf_ready(struct ipu6_isys_stream *stream,
			       struct ipu6_fw_isys_resp_info_abi *info)
{
	struct ipu6_isys_queue *aq = stream->output_pins[info->pin_id].aq;
	struct ipu6_isys *isys = stream->isys;
	struct device *dev = &isys->adev->auxdev.dev;
	struct ipu6_isys_buffer *ib;
	struct vb2_buffer *vb;
	unsigned long flags;
	bool first = true;
	struct vb2_v4l2_buffer *buf;

	spin_lock_irqsave(&aq->lock, flags);
	if (list_empty(&aq->active)) {
		spin_unlock_irqrestore(&aq->lock, flags);
		dev_err(dev, "active queue empty\n");
		return;
	}

	list_for_each_entry_reverse(ib, &aq->active, head) {
		struct ipu6_isys_video_buffer *ivb;
		struct vb2_v4l2_buffer *vvb;
		dma_addr_t addr;

		vb = ipu6_isys_buffer_to_vb2_buffer(ib);
		vvb = to_vb2_v4l2_buffer(vb);
		ivb = vb2_buffer_to_ipu6_isys_video_buffer(vvb);
		addr = ivb->dma_addr;

		if (info->pin.addr != addr) {
			if (first)
				dev_err(dev, "Unexpected buffer address %pad\n",
					&addr);
			first = false;
			continue;
		}

		if (info->error_info.error ==
		    IPU6_FW_ISYS_ERROR_HW_REPORTED_STR2MMIO) {
			/*
			 * Check for error message:
			 * 'IPU6_FW_ISYS_ERROR_HW_REPORTED_STR2MMIO'
			 */
			atomic_set(&ib->str2mmio_flag, 1);
		}
		dev_dbg(dev, "buffer: found buffer %pad\n", &addr);

		buf = to_vb2_v4l2_buffer(vb);
		buf->field = V4L2_FIELD_NONE;

		list_del(&ib->head);
		spin_unlock_irqrestore(&aq->lock, flags);

		ipu6_isys_buf_calc_sequence_time(ib, info);

		ipu6_isys_queue_buf_done(ib);

		return;
	}

	dev_err(dev, "Failed to find a matching video buffer");

	spin_unlock_irqrestore(&aq->lock, flags);
}

static const struct vb2_ops ipu6_isys_queue_ops = {
	.queue_setup = ipu6_isys_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_init = ipu6_isys_buf_init,
	.buf_prepare = ipu6_isys_buf_prepare,
	.buf_cleanup = ipu6_isys_buf_cleanup,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
	.buf_queue = buf_queue,
};

int ipu6_isys_queue_init(struct ipu6_isys_queue *aq)
{
	struct ipu6_isys *isys = ipu6_isys_queue_to_video(aq)->isys;
	struct ipu6_isys_video *av = ipu6_isys_queue_to_video(aq);
	struct ipu6_bus_device *adev = isys->adev;
	int ret;

	/* no support for userptr */
	if (!aq->vbq.io_modes)
		aq->vbq.io_modes = VB2_MMAP | VB2_DMABUF;

	aq->vbq.drv_priv = isys;
	aq->vbq.ops = &ipu6_isys_queue_ops;
	aq->vbq.lock = &av->mutex;
	aq->vbq.mem_ops = &vb2_dma_sg_memops;
	aq->vbq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	aq->vbq.min_queued_buffers = 1;
	aq->vbq.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	ret = vb2_queue_init(&aq->vbq);
	if (ret)
		return ret;

	aq->dev = &adev->auxdev.dev;
	aq->vbq.dev = &adev->isp->pdev->dev;
	spin_lock_init(&aq->lock);
	INIT_LIST_HEAD(&aq->active);
	INIT_LIST_HEAD(&aq->incoming);

	return 0;
}
