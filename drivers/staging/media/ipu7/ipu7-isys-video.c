// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#include <linux/align.h>
#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>

#include "abi/ipu7_fw_isys_abi.h"

#include "ipu7.h"
#include "ipu7-bus.h"
#include "ipu7-buttress-regs.h"
#include "ipu7-fw-isys.h"
#include "ipu7-isys.h"
#include "ipu7-isys-video.h"
#include "ipu7-platform-regs.h"

const struct ipu7_isys_pixelformat ipu7_isys_pfmts[] = {
	{V4L2_PIX_FMT_SBGGR12, 16, 12, MEDIA_BUS_FMT_SBGGR12_1X12,
	 IPU_INSYS_FRAME_FORMAT_RAW16},
	{V4L2_PIX_FMT_SGBRG12, 16, 12, MEDIA_BUS_FMT_SGBRG12_1X12,
	 IPU_INSYS_FRAME_FORMAT_RAW16},
	{V4L2_PIX_FMT_SGRBG12, 16, 12, MEDIA_BUS_FMT_SGRBG12_1X12,
	 IPU_INSYS_FRAME_FORMAT_RAW16},
	{V4L2_PIX_FMT_SRGGB12, 16, 12, MEDIA_BUS_FMT_SRGGB12_1X12,
	 IPU_INSYS_FRAME_FORMAT_RAW16},
	{V4L2_PIX_FMT_SBGGR10, 16, 10, MEDIA_BUS_FMT_SBGGR10_1X10,
	 IPU_INSYS_FRAME_FORMAT_RAW16},
	{V4L2_PIX_FMT_SGBRG10, 16, 10, MEDIA_BUS_FMT_SGBRG10_1X10,
	 IPU_INSYS_FRAME_FORMAT_RAW16},
	{V4L2_PIX_FMT_SGRBG10, 16, 10, MEDIA_BUS_FMT_SGRBG10_1X10,
	 IPU_INSYS_FRAME_FORMAT_RAW16},
	{V4L2_PIX_FMT_SRGGB10, 16, 10, MEDIA_BUS_FMT_SRGGB10_1X10,
	 IPU_INSYS_FRAME_FORMAT_RAW16},
	{V4L2_PIX_FMT_SBGGR8, 8, 8, MEDIA_BUS_FMT_SBGGR8_1X8,
	 IPU_INSYS_FRAME_FORMAT_RAW8},
	{V4L2_PIX_FMT_SGBRG8, 8, 8, MEDIA_BUS_FMT_SGBRG8_1X8,
	 IPU_INSYS_FRAME_FORMAT_RAW8},
	{V4L2_PIX_FMT_SGRBG8, 8, 8, MEDIA_BUS_FMT_SGRBG8_1X8,
	 IPU_INSYS_FRAME_FORMAT_RAW8},
	{V4L2_PIX_FMT_SRGGB8, 8, 8, MEDIA_BUS_FMT_SRGGB8_1X8,
	 IPU_INSYS_FRAME_FORMAT_RAW8},
	{V4L2_PIX_FMT_SBGGR12P, 12, 12, MEDIA_BUS_FMT_SBGGR12_1X12,
	 IPU_INSYS_FRAME_FORMAT_RAW12},
	{V4L2_PIX_FMT_SGBRG12P, 12, 12, MEDIA_BUS_FMT_SGBRG12_1X12,
	 IPU_INSYS_FRAME_FORMAT_RAW12},
	{V4L2_PIX_FMT_SGRBG12P, 12, 12, MEDIA_BUS_FMT_SGRBG12_1X12,
	 IPU_INSYS_FRAME_FORMAT_RAW12},
	{V4L2_PIX_FMT_SRGGB12P, 12, 12, MEDIA_BUS_FMT_SRGGB12_1X12,
	 IPU_INSYS_FRAME_FORMAT_RAW12},
	{V4L2_PIX_FMT_SBGGR10P, 10, 10, MEDIA_BUS_FMT_SBGGR10_1X10,
	 IPU_INSYS_FRAME_FORMAT_RAW10},
	{V4L2_PIX_FMT_SGBRG10P, 10, 10, MEDIA_BUS_FMT_SGBRG10_1X10,
	 IPU_INSYS_FRAME_FORMAT_RAW10},
	{V4L2_PIX_FMT_SGRBG10P, 10, 10, MEDIA_BUS_FMT_SGRBG10_1X10,
	 IPU_INSYS_FRAME_FORMAT_RAW10},
	{V4L2_PIX_FMT_SRGGB10P, 10, 10, MEDIA_BUS_FMT_SRGGB10_1X10,
	 IPU_INSYS_FRAME_FORMAT_RAW10},
	{V4L2_PIX_FMT_UYVY, 16, 16, MEDIA_BUS_FMT_UYVY8_1X16,
	 IPU_INSYS_FRAME_FORMAT_UYVY},
	{V4L2_PIX_FMT_YUYV, 16, 16, MEDIA_BUS_FMT_YUYV8_1X16,
	 IPU_INSYS_FRAME_FORMAT_YUYV},
	{V4L2_PIX_FMT_RGB565, 16, 16, MEDIA_BUS_FMT_RGB565_1X16,
	 IPU_INSYS_FRAME_FORMAT_RGB565},
	{V4L2_PIX_FMT_BGR24, 24, 24, MEDIA_BUS_FMT_RGB888_1X24,
	 IPU_INSYS_FRAME_FORMAT_RGBA888},
};

static int video_open(struct file *file)
{
	return v4l2_fh_open(file);
}

const struct ipu7_isys_pixelformat *ipu7_isys_get_isys_format(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ipu7_isys_pfmts); i++) {
		const struct ipu7_isys_pixelformat *pfmt = &ipu7_isys_pfmts[i];

		if (pfmt->pixelformat == pixelformat)
			return pfmt;
	}

	return &ipu7_isys_pfmts[0];
}

static int ipu7_isys_vidioc_querycap(struct file *file, void *fh,
				     struct v4l2_capability *cap)
{
	struct ipu7_isys_video *av = video_drvdata(file);

	strscpy(cap->driver, IPU_ISYS_NAME, sizeof(cap->driver));
	strscpy(cap->card, av->isys->media_dev.model, sizeof(cap->card));

	return 0;
}

static int ipu7_isys_vidioc_enum_fmt(struct file *file, void *fh,
				     struct v4l2_fmtdesc *f)
{
	unsigned int i, num_found;

	for (i = 0, num_found = 0; i < ARRAY_SIZE(ipu7_isys_pfmts); i++) {
		if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			continue;

		if (f->mbus_code && f->mbus_code != ipu7_isys_pfmts[i].code)
			continue;

		if (num_found < f->index) {
			num_found++;
			continue;
		}

		f->flags = 0;
		f->pixelformat = ipu7_isys_pfmts[i].pixelformat;

		return 0;
	}

	return -EINVAL;
}

static int ipu7_isys_vidioc_enum_framesizes(struct file *file, void *fh,
					    struct v4l2_frmsizeenum *fsize)
{
	unsigned int i;

	if (fsize->index > 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ipu7_isys_pfmts); i++) {
		if (fsize->pixel_format != ipu7_isys_pfmts[i].pixelformat)
			continue;

		fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
		fsize->stepwise.min_width = IPU_ISYS_MIN_WIDTH;
		fsize->stepwise.max_width = IPU_ISYS_MAX_WIDTH;
		fsize->stepwise.min_height = IPU_ISYS_MIN_HEIGHT;
		fsize->stepwise.max_height = IPU_ISYS_MAX_HEIGHT;
		fsize->stepwise.step_width = 2;
		fsize->stepwise.step_height = 2;

		return 0;
	}

	return -EINVAL;
}

static int ipu7_isys_vidioc_g_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_format *f)
{
	struct ipu7_isys_video *av = video_drvdata(file);

	f->fmt.pix = av->pix_fmt;

	return 0;
}

static void ipu7_isys_try_fmt_cap(struct ipu7_isys_video *av, u32 type,
				  u32 *format, u32 *width, u32 *height,
				  u32 *bytesperline, u32 *sizeimage)
{
	const struct ipu7_isys_pixelformat *pfmt =
		ipu7_isys_get_isys_format(*format);

	*format = pfmt->pixelformat;
	*width = clamp(*width, IPU_ISYS_MIN_WIDTH, IPU_ISYS_MAX_WIDTH);
	*height = clamp(*height, IPU_ISYS_MIN_HEIGHT, IPU_ISYS_MAX_HEIGHT);

	if (pfmt->bpp != pfmt->bpp_packed)
		*bytesperline = *width * DIV_ROUND_UP(pfmt->bpp, BITS_PER_BYTE);
	else
		*bytesperline = DIV_ROUND_UP(*width * pfmt->bpp, BITS_PER_BYTE);

	*bytesperline = ALIGN(*bytesperline, 64U);

	/*
	 * (height + 1) * bytesperline due to a hardware issue: the DMA unit
	 * is a power of two, and a line should be transferred as few units
	 * as possible. The result is that up to line length more data than
	 * the image size may be transferred to memory after the image.
	 * Another limitation is the GDA allocation unit size. For low
	 * resolution it gives a bigger number. Use larger one to avoid
	 * memory corruption.
	 */
	*sizeimage = *bytesperline * *height +
		max(*bytesperline,
		    av->isys->pdata->ipdata->isys_dma_overshoot);
}

static void __ipu_isys_vidioc_try_fmt_vid_cap(struct ipu7_isys_video *av,
					      struct v4l2_format *f)
{
	ipu7_isys_try_fmt_cap(av, f->type, &f->fmt.pix.pixelformat,
			      &f->fmt.pix.width, &f->fmt.pix.height,
			      &f->fmt.pix.bytesperline, &f->fmt.pix.sizeimage);

	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_RAW;
	f->fmt.pix.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix.quantization = V4L2_QUANTIZATION_DEFAULT;
	f->fmt.pix.xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int ipu7_isys_vidioc_try_fmt_vid_cap(struct file *file, void *fh,
					    struct v4l2_format *f)
{
	struct ipu7_isys_video *av = video_drvdata(file);

	if (vb2_is_busy(&av->aq.vbq))
		return -EBUSY;

	__ipu_isys_vidioc_try_fmt_vid_cap(av, f);

	return 0;
}

static int ipu7_isys_vidioc_s_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_format *f)
{
	struct ipu7_isys_video *av = video_drvdata(file);

	ipu7_isys_vidioc_try_fmt_vid_cap(file, fh, f);
	av->pix_fmt = f->fmt.pix;

	return 0;
}

static int ipu7_isys_vidioc_reqbufs(struct file *file, void *priv,
				    struct v4l2_requestbuffers *p)
{
	struct ipu7_isys_video *av = video_drvdata(file);
	int ret;

	av->aq.vbq.is_multiplanar = V4L2_TYPE_IS_MULTIPLANAR(p->type);
	av->aq.vbq.is_output = V4L2_TYPE_IS_OUTPUT(p->type);

	ret = vb2_queue_change_type(&av->aq.vbq, p->type);
	if (ret)
		return ret;

	return vb2_ioctl_reqbufs(file, priv, p);
}

static int ipu7_isys_vidioc_create_bufs(struct file *file, void *priv,
					struct v4l2_create_buffers *p)
{
	struct ipu7_isys_video *av = video_drvdata(file);
	int ret;

	av->aq.vbq.is_multiplanar = V4L2_TYPE_IS_MULTIPLANAR(p->format.type);
	av->aq.vbq.is_output = V4L2_TYPE_IS_OUTPUT(p->format.type);

	ret = vb2_queue_change_type(&av->aq.vbq, p->format.type);
	if (ret)
		return ret;

	return vb2_ioctl_create_bufs(file, priv, p);
}

static int link_validate(struct media_link *link)
{
	struct ipu7_isys_video *av =
		container_of(link->sink, struct ipu7_isys_video, pad);
	struct device *dev = &av->isys->adev->auxdev.dev;
	struct v4l2_subdev_state *s_state;
	struct v4l2_mbus_framefmt *s_fmt;
	struct v4l2_subdev *s_sd;
	struct media_pad *s_pad;
	u32 s_stream = 0, code;
	int ret = -EPIPE;

	if (!link->source->entity)
		return ret;

	s_sd = media_entity_to_v4l2_subdev(link->source->entity);
	s_state = v4l2_subdev_get_unlocked_active_state(s_sd);
	if (!s_state)
		return ret;

	dev_dbg(dev, "validating link \"%s\":%u -> \"%s\"\n",
		link->source->entity->name, link->source->index,
		link->sink->entity->name);

	s_pad = media_pad_remote_pad_first(&av->pad);

	v4l2_subdev_lock_state(s_state);

	s_fmt = v4l2_subdev_state_get_format(s_state, s_pad->index, s_stream);
	if (!s_fmt) {
		dev_err(dev, "failed to get source pad format\n");
		goto unlock;
	}

	code = ipu7_isys_get_isys_format(av->pix_fmt.pixelformat)->code;

	if (s_fmt->width != av->pix_fmt.width ||
	    s_fmt->height != av->pix_fmt.height || s_fmt->code != code) {
		dev_dbg(dev, "format mismatch %dx%d,%x != %dx%d,%x\n",
			s_fmt->width, s_fmt->height, s_fmt->code,
			av->pix_fmt.width, av->pix_fmt.height, code);
		goto unlock;
	}

	v4l2_subdev_unlock_state(s_state);

	return 0;
unlock:
	v4l2_subdev_unlock_state(s_state);

	return ret;
}

static void get_stream_opened(struct ipu7_isys_video *av)
{
	unsigned long flags;

	spin_lock_irqsave(&av->isys->streams_lock, flags);
	av->isys->stream_opened++;
	spin_unlock_irqrestore(&av->isys->streams_lock, flags);
}

static void put_stream_opened(struct ipu7_isys_video *av)
{
	unsigned long flags;

	spin_lock_irqsave(&av->isys->streams_lock, flags);
	av->isys->stream_opened--;
	spin_unlock_irqrestore(&av->isys->streams_lock, flags);
}

static int ipu7_isys_fw_pin_cfg(struct ipu7_isys_video *av,
				struct ipu7_insys_stream_cfg *cfg)
{
	struct media_pad *src_pad = media_pad_remote_pad_first(&av->pad);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(src_pad->entity);
	struct ipu7_isys_stream *stream = av->stream;
	const struct ipu7_isys_pixelformat *pfmt =
		ipu7_isys_get_isys_format(av->pix_fmt.pixelformat);
	struct ipu7_insys_output_pin *output_pin;
	struct ipu7_insys_input_pin *input_pin;
	int input_pins = cfg->nof_input_pins++;
	struct ipu7_isys_queue *aq = &av->aq;
	struct ipu7_isys *isys = av->isys;
	struct device *dev = &isys->adev->auxdev.dev;
	struct v4l2_mbus_framefmt fmt;
	int output_pins;
	u32 src_stream = 0;
	int ret;

	ret = ipu7_isys_get_stream_pad_fmt(sd, src_pad->index, src_stream,
					   &fmt);
	if (ret < 0) {
		dev_err(dev, "can't get stream format (%d)\n", ret);
		return ret;
	}

	input_pin = &cfg->input_pins[input_pins];
	input_pin->input_res.width = fmt.width;
	input_pin->input_res.height = fmt.height;
	input_pin->dt = av->dt;
	input_pin->disable_mipi_unpacking = 0;
	pfmt = ipu7_isys_get_isys_format(av->pix_fmt.pixelformat);
	if (pfmt->bpp == pfmt->bpp_packed && pfmt->bpp % BITS_PER_BYTE)
		input_pin->disable_mipi_unpacking = 1;
	input_pin->mapped_dt = N_IPU_INSYS_MIPI_DATA_TYPE;
	input_pin->dt_rename_mode = IPU_INSYS_MIPI_DT_NO_RENAME;
	/* if enable polling isys interrupt, the follow values maybe set */
	input_pin->sync_msg_map = IPU_INSYS_STREAM_SYNC_MSG_SEND_RESP_SOF |
		IPU_INSYS_STREAM_SYNC_MSG_SEND_RESP_SOF_DISCARDED |
		IPU_INSYS_STREAM_SYNC_MSG_SEND_IRQ_SOF |
		IPU_INSYS_STREAM_SYNC_MSG_SEND_IRQ_SOF_DISCARDED;

	output_pins = cfg->nof_output_pins++;
	aq->fw_output = output_pins;
	stream->output_pins[output_pins].pin_ready = ipu7_isys_queue_buf_ready;
	stream->output_pins[output_pins].aq = aq;

	output_pin = &cfg->output_pins[output_pins];
	/* output pin msg link */
	output_pin->link.buffer_lines = 0;
	output_pin->link.foreign_key = IPU_MSG_LINK_FOREIGN_KEY_NONE;
	output_pin->link.granularity_pointer_update = 0;
	output_pin->link.msg_link_streaming_mode =
		IA_GOFO_MSG_LINK_STREAMING_MODE_SOFF;

	output_pin->link.pbk_id = IPU_MSG_LINK_PBK_ID_DONT_CARE;
	output_pin->link.pbk_slot_id = IPU_MSG_LINK_PBK_SLOT_ID_DONT_CARE;
	output_pin->link.dest = IPU_INSYS_OUTPUT_LINK_DEST_MEM;
	output_pin->link.use_sw_managed = 1;
	/* TODO: set the snoop bit for metadata capture */
	output_pin->link.is_snoop = 0;

	/* output pin crop */
	output_pin->crop.line_top = 0;
	output_pin->crop.line_bottom = 0;

	/* output de-compression */
	output_pin->dpcm.enable = 0;

	/* frame format type */
	pfmt = ipu7_isys_get_isys_format(av->pix_fmt.pixelformat);
	output_pin->ft = (u16)pfmt->css_pixelformat;

	/* stride in bytes */
	output_pin->stride = av->pix_fmt.bytesperline;
	output_pin->send_irq = 1;
	output_pin->early_ack_en = 0;

	/* input pin id */
	output_pin->input_pin_id = input_pins;

	return 0;
}

/* Create stream and start it using the CSS FW ABI. */
static int start_stream_firmware(struct ipu7_isys_video *av,
				 struct ipu7_isys_buffer_list *bl)
{
	struct device *dev = &av->isys->adev->auxdev.dev;
	struct ipu7_isys_stream *stream = av->stream;
	struct ipu7_insys_stream_cfg *stream_cfg;
	struct ipu7_insys_buffset *buf = NULL;
	struct isys_fw_msgs *msg = NULL;
	struct ipu7_isys_queue *aq;
	int ret, retout, tout;
	u16 send_type;

	if (WARN_ON(!bl))
		return -EIO;

	msg = ipu7_get_fw_msg_buf(stream);
	if (!msg)
		return -ENOMEM;

	stream_cfg = &msg->fw_msg.stream;
	stream_cfg->port_id = stream->stream_source;
	stream_cfg->vc = stream->vc;
	stream_cfg->stream_msg_map = IPU_INSYS_STREAM_ENABLE_MSG_SEND_RESP |
				     IPU_INSYS_STREAM_ENABLE_MSG_SEND_IRQ;

	list_for_each_entry(aq, &stream->queues, node) {
		struct ipu7_isys_video *__av = ipu7_isys_queue_to_video(aq);

		ret = ipu7_isys_fw_pin_cfg(__av, stream_cfg);
		if (ret < 0) {
			ipu7_put_fw_msg_buf(av->isys, (uintptr_t)stream_cfg);
			return ret;
		}
	}

	ipu7_fw_isys_dump_stream_cfg(dev, stream_cfg);

	stream->nr_output_pins = stream_cfg->nof_output_pins;

	reinit_completion(&stream->stream_open_completion);

	ret = ipu7_fw_isys_complex_cmd(av->isys, stream->stream_handle,
				       stream_cfg, msg->dma_addr,
				       sizeof(*stream_cfg),
				       IPU_INSYS_SEND_TYPE_STREAM_OPEN);
	if (ret < 0) {
		dev_err(dev, "can't open stream (%d)\n", ret);
		ipu7_put_fw_msg_buf(av->isys, (uintptr_t)stream_cfg);
		return ret;
	}

	get_stream_opened(av);

	tout = wait_for_completion_timeout(&stream->stream_open_completion,
					   FW_CALL_TIMEOUT_JIFFIES);

	ipu7_put_fw_msg_buf(av->isys, (uintptr_t)stream_cfg);

	if (!tout) {
		dev_err(dev, "stream open time out\n");
		ret = -ETIMEDOUT;
		goto out_put_stream_opened;
	}
	if (stream->error) {
		dev_err(dev, "stream open error: %d\n", stream->error);
		ret = -EIO;
		goto out_put_stream_opened;
	}
	dev_dbg(dev, "start stream: open complete\n");

	msg = ipu7_get_fw_msg_buf(stream);
	if (!msg) {
		ret = -ENOMEM;
		goto out_put_stream_opened;
	}
	buf = &msg->fw_msg.frame;

	ipu7_isys_buffer_to_fw_frame_buff(buf, stream, bl);
	ipu7_isys_buffer_list_queue(bl, IPU_ISYS_BUFFER_LIST_FL_ACTIVE, 0);

	reinit_completion(&stream->stream_start_completion);

	send_type = IPU_INSYS_SEND_TYPE_STREAM_START_AND_CAPTURE;
	ipu7_fw_isys_dump_frame_buff_set(dev, buf,
					 stream_cfg->nof_output_pins);
	ret = ipu7_fw_isys_complex_cmd(av->isys, stream->stream_handle, buf,
				       msg->dma_addr, sizeof(*buf),
				       send_type);
	if (ret < 0) {
		dev_err(dev, "can't start streaming (%d)\n", ret);
		goto out_stream_close;
	}

	tout = wait_for_completion_timeout(&stream->stream_start_completion,
					   FW_CALL_TIMEOUT_JIFFIES);
	if (!tout) {
		dev_err(dev, "stream start time out\n");
		ret = -ETIMEDOUT;
		goto out_stream_close;
	}
	if (stream->error) {
		dev_err(dev, "stream start error: %d\n", stream->error);
		ret = -EIO;
		goto out_stream_close;
	}
	dev_dbg(dev, "start stream: complete\n");

	return 0;

out_stream_close:
	reinit_completion(&stream->stream_close_completion);

	retout = ipu7_fw_isys_simple_cmd(av->isys, stream->stream_handle,
					 IPU_INSYS_SEND_TYPE_STREAM_CLOSE);
	if (retout < 0) {
		dev_dbg(dev, "can't close stream (%d)\n", retout);
		goto out_put_stream_opened;
	}

	tout = wait_for_completion_timeout(&stream->stream_close_completion,
					   FW_CALL_TIMEOUT_JIFFIES);
	if (!tout)
		dev_err(dev, "stream close time out with error %d\n",
			stream->error);
	else
		dev_dbg(dev, "stream close complete\n");

out_put_stream_opened:
	put_stream_opened(av);

	return ret;
}

static void stop_streaming_firmware(struct ipu7_isys_video *av)
{
	struct device *dev = &av->isys->adev->auxdev.dev;
	struct ipu7_isys_stream *stream = av->stream;
	int ret, tout;

	reinit_completion(&stream->stream_stop_completion);

	ret = ipu7_fw_isys_simple_cmd(av->isys, stream->stream_handle,
				      IPU_INSYS_SEND_TYPE_STREAM_FLUSH);
	if (ret < 0) {
		dev_err(dev, "can't stop stream (%d)\n", ret);
		return;
	}

	tout = wait_for_completion_timeout(&stream->stream_stop_completion,
					   FW_CALL_TIMEOUT_JIFFIES);
	if (!tout)
		dev_warn(dev, "stream stop time out\n");
	else if (stream->error)
		dev_warn(dev, "stream stop error: %d\n", stream->error);
	else
		dev_dbg(dev, "stop stream: complete\n");
}

static void close_streaming_firmware(struct ipu7_isys_video *av)
{
	struct device *dev = &av->isys->adev->auxdev.dev;
	struct ipu7_isys_stream *stream =  av->stream;
	int ret, tout;

	reinit_completion(&stream->stream_close_completion);

	ret = ipu7_fw_isys_simple_cmd(av->isys, stream->stream_handle,
				      IPU_INSYS_SEND_TYPE_STREAM_CLOSE);
	if (ret < 0) {
		dev_err(dev, "can't close stream (%d)\n", ret);
		return;
	}

	tout = wait_for_completion_timeout(&stream->stream_close_completion,
					   FW_CALL_TIMEOUT_JIFFIES);
	if (!tout)
		dev_warn(dev, "stream close time out\n");
	else if (stream->error)
		dev_warn(dev, "stream close error: %d\n", stream->error);
	else
		dev_dbg(dev, "close stream: complete\n");

	put_stream_opened(av);
}

int ipu7_isys_video_prepare_stream(struct ipu7_isys_video *av,
				   struct media_entity *source_entity,
				   int nr_queues)
{
	struct ipu7_isys_stream *stream = av->stream;
	struct ipu7_isys_csi2 *csi2;

	if (WARN_ON(stream->nr_streaming))
		return -EINVAL;

	stream->nr_queues = nr_queues;
	atomic_set(&stream->sequence, 0);
	atomic_set(&stream->buf_id, 0);

	stream->seq_index = 0;
	memset(stream->seq, 0, sizeof(stream->seq));

	if (WARN_ON(!list_empty(&stream->queues)))
		return -EINVAL;

	stream->stream_source = stream->asd->source;

	csi2 = ipu7_isys_subdev_to_csi2(stream->asd);
	csi2->receiver_errors = 0;
	stream->source_entity = source_entity;

	dev_dbg(&av->isys->adev->auxdev.dev,
		"prepare stream: external entity %s\n",
		stream->source_entity->name);

	return 0;
}

void ipu7_isys_put_stream(struct ipu7_isys_stream *stream)
{
	unsigned long flags;
	struct device *dev;
	unsigned int i;

	if (!stream) {
		pr_err("ipu7-isys: no available stream\n");
		return;
	}

	dev = &stream->isys->adev->auxdev.dev;

	spin_lock_irqsave(&stream->isys->streams_lock, flags);
	for (i = 0; i < IPU_ISYS_MAX_STREAMS; i++) {
		if (&stream->isys->streams[i] == stream) {
			if (stream->isys->streams_ref_count[i] > 0)
				stream->isys->streams_ref_count[i]--;
			else
				dev_warn(dev, "invalid stream %d\n", i);

			break;
		}
	}
	spin_unlock_irqrestore(&stream->isys->streams_lock, flags);
}

static struct ipu7_isys_stream *
ipu7_isys_get_stream(struct ipu7_isys_video *av, struct ipu7_isys_subdev *asd)
{
	struct ipu7_isys_stream *stream = NULL;
	struct ipu7_isys *isys = av->isys;
	unsigned long flags;
	unsigned int i;
	u8 vc = av->vc;

	if (!isys)
		return NULL;

	spin_lock_irqsave(&isys->streams_lock, flags);
	for (i = 0; i < IPU_ISYS_MAX_STREAMS; i++) {
		if (isys->streams_ref_count[i] && isys->streams[i].vc == vc &&
		    isys->streams[i].asd == asd) {
			isys->streams_ref_count[i]++;
			stream = &isys->streams[i];
			break;
		}
	}

	if (!stream) {
		for (i = 0; i < IPU_ISYS_MAX_STREAMS; i++) {
			if (!isys->streams_ref_count[i]) {
				isys->streams_ref_count[i]++;
				stream = &isys->streams[i];
				stream->vc = vc;
				stream->asd = asd;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&isys->streams_lock, flags);

	return stream;
}

struct ipu7_isys_stream *
ipu7_isys_query_stream_by_handle(struct ipu7_isys *isys, u8 stream_handle)
{
	unsigned long flags;
	struct ipu7_isys_stream *stream = NULL;

	if (!isys)
		return NULL;

	if (stream_handle >= IPU_ISYS_MAX_STREAMS) {
		dev_err(&isys->adev->auxdev.dev,
			"stream_handle %d is invalid\n", stream_handle);
		return NULL;
	}

	spin_lock_irqsave(&isys->streams_lock, flags);
	if (isys->streams_ref_count[stream_handle] > 0) {
		isys->streams_ref_count[stream_handle]++;
		stream = &isys->streams[stream_handle];
	}
	spin_unlock_irqrestore(&isys->streams_lock, flags);

	return stream;
}

struct ipu7_isys_stream *
ipu7_isys_query_stream_by_source(struct ipu7_isys *isys, int source, u8 vc)
{
	struct ipu7_isys_stream *stream = NULL;
	unsigned long flags;
	unsigned int i;

	if (!isys)
		return NULL;

	if (source < 0) {
		dev_err(&isys->adev->auxdev.dev,
			"query stream with invalid port number\n");
		return NULL;
	}

	spin_lock_irqsave(&isys->streams_lock, flags);
	for (i = 0; i < IPU_ISYS_MAX_STREAMS; i++) {
		if (!isys->streams_ref_count[i])
			continue;

		if (isys->streams[i].stream_source == source &&
		    isys->streams[i].vc == vc) {
			stream = &isys->streams[i];
			isys->streams_ref_count[i]++;
			break;
		}
	}
	spin_unlock_irqrestore(&isys->streams_lock, flags);

	return stream;
}

int ipu7_isys_video_set_streaming(struct ipu7_isys_video *av, int state,
				  struct ipu7_isys_buffer_list *bl)
{
	struct ipu7_isys_stream *stream = av->stream;
	struct device *dev = &av->isys->adev->auxdev.dev;
	struct media_pad *r_pad;
	struct v4l2_subdev *sd;
	u32 r_stream = 0;
	int ret = 0;

	dev_dbg(dev, "set stream: %d\n", state);

	if (WARN(!stream->source_entity, "No source entity for stream\n"))
		return -ENODEV;

	sd = &stream->asd->sd;
	r_pad = media_pad_remote_pad_first(&av->pad);
	if (!state) {
		stop_streaming_firmware(av);

		/* stop sub-device which connects with video */
		dev_dbg(dev, "disable streams %s pad:%d mask:0x%llx\n",
			sd->name, r_pad->index, BIT_ULL(r_stream));
		ret = v4l2_subdev_disable_streams(sd, r_pad->index,
						  BIT_ULL(r_stream));
		if (ret) {
			dev_err(dev, "disable streams %s failed with %d\n",
				sd->name, ret);
			return ret;
		}

		close_streaming_firmware(av);
	} else {
		ret = start_stream_firmware(av, bl);
		if (ret) {
			dev_err(dev, "start stream of firmware failed\n");
			return ret;
		}

		/* start sub-device which connects with video */
		dev_dbg(dev, "enable streams %s pad: %d mask:0x%llx\n",
			sd->name, r_pad->index, BIT_ULL(r_stream));
		ret = v4l2_subdev_enable_streams(sd, r_pad->index,
						 BIT_ULL(r_stream));
		if (ret) {
			dev_err(dev, "enable streams %s failed with %d\n",
				sd->name, ret);
			goto out_media_entity_stop_streaming_firmware;
		}
	}

	av->streaming = state;

	return 0;

out_media_entity_stop_streaming_firmware:
	stop_streaming_firmware(av);

	return ret;
}

static const struct v4l2_ioctl_ops ipu7_v4l2_ioctl_ops = {
	.vidioc_querycap = ipu7_isys_vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = ipu7_isys_vidioc_enum_fmt,
	.vidioc_enum_framesizes = ipu7_isys_vidioc_enum_framesizes,
	.vidioc_g_fmt_vid_cap = ipu7_isys_vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = ipu7_isys_vidioc_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = ipu7_isys_vidioc_try_fmt_vid_cap,
	.vidioc_reqbufs = ipu7_isys_vidioc_reqbufs,
	.vidioc_create_bufs = ipu7_isys_vidioc_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_expbuf = vb2_ioctl_expbuf,
};

static const struct media_entity_operations entity_ops = {
	.link_validate = link_validate,
};

static const struct v4l2_file_operations isys_fops = {
	.owner = THIS_MODULE,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
	.open = video_open,
	.release = vb2_fop_release,
};

int ipu7_isys_fw_open(struct ipu7_isys *isys)
{
	struct ipu7_bus_device *adev = isys->adev;
	int ret;

	ret = pm_runtime_resume_and_get(&adev->auxdev.dev);
	if (ret < 0)
		return ret;

	mutex_lock(&isys->mutex);

	if (isys->ref_count++)
		goto unlock;

	/*
	 * Buffers could have been left to wrong queue at last closure.
	 * Move them now back to empty buffer queue.
	 */
	ipu7_cleanup_fw_msg_bufs(isys);

	ret = ipu7_fw_isys_open(isys);
	if (ret < 0)
		goto out;

unlock:
	mutex_unlock(&isys->mutex);

	return 0;
out:
	isys->ref_count--;
	mutex_unlock(&isys->mutex);
	pm_runtime_put(&adev->auxdev.dev);

	return ret;
}

void ipu7_isys_fw_close(struct ipu7_isys *isys)
{
	mutex_lock(&isys->mutex);

	isys->ref_count--;

	if (!isys->ref_count)
		ipu7_fw_isys_close(isys);

	mutex_unlock(&isys->mutex);
	pm_runtime_put(&isys->adev->auxdev.dev);
}

int ipu7_isys_setup_video(struct ipu7_isys_video *av,
			  struct media_entity **source_entity, int *nr_queues)
{
	const struct ipu7_isys_pixelformat *pfmt =
		ipu7_isys_get_isys_format(av->pix_fmt.pixelformat);
	struct device *dev = &av->isys->adev->auxdev.dev;
	struct media_pad *source_pad, *remote_pad;
	struct v4l2_mbus_frame_desc_entry entry;
	struct v4l2_subdev_route *route = NULL;
	struct v4l2_subdev_route *r;
	struct v4l2_subdev_state *state;
	struct ipu7_isys_subdev *asd;
	struct v4l2_subdev *remote_sd;
	struct media_pipeline *pipeline;
	int ret = -EINVAL;

	*nr_queues = 0;

	remote_pad = media_pad_remote_pad_unique(&av->pad);
	if (IS_ERR(remote_pad)) {
		dev_dbg(dev, "failed to get remote pad\n");
		return PTR_ERR(remote_pad);
	}

	remote_sd = media_entity_to_v4l2_subdev(remote_pad->entity);
	asd = to_ipu7_isys_subdev(remote_sd);

	source_pad = media_pad_remote_pad_first(&remote_pad->entity->pads[0]);
	if (!source_pad) {
		dev_dbg(dev, "No external source entity\n");
		return -ENODEV;
	}

	*source_entity = source_pad->entity;

	state = v4l2_subdev_lock_and_get_active_state(remote_sd);
	for_each_active_route(&state->routing, r) {
		if (r->source_pad == remote_pad->index)
			route = r;
	}

	if (!route) {
		v4l2_subdev_unlock_state(state);
		dev_dbg(dev, "Failed to find route\n");
		return -ENODEV;
	}

	v4l2_subdev_unlock_state(state);

	ret = ipu7_isys_csi2_get_remote_desc(route->sink_stream,
					     to_ipu7_isys_csi2(asd),
					     *source_entity, &entry,
					     nr_queues);
	if (ret == -ENOIOCTLCMD) {
		av->vc = 0;
		av->dt = ipu7_isys_mbus_code_to_mipi(pfmt->code);
		if (av->dt == 0xff)
			return -EINVAL;
		*nr_queues = 1;
	} else if (*nr_queues && !ret) {
		dev_dbg(dev, "Framedesc: stream %u, len %u, vc %u, dt %#x\n",
			entry.stream, entry.length, entry.bus.csi2.vc,
			entry.bus.csi2.dt);

		av->vc = entry.bus.csi2.vc;
		av->dt = entry.bus.csi2.dt;
	} else {
		dev_err(dev, "failed to get remote frame desc\n");
		return ret;
	}

	pipeline = media_entity_pipeline(&av->vdev.entity);
	if (!pipeline)
		ret = video_device_pipeline_alloc_start(&av->vdev);
	else
		ret = video_device_pipeline_start(&av->vdev, pipeline);
	if (ret < 0) {
		dev_dbg(dev, "media pipeline start failed\n");
		return ret;
	}

	av->stream = ipu7_isys_get_stream(av, asd);
	if (!av->stream) {
		video_device_pipeline_stop(&av->vdev);
		dev_err(dev, "no available stream for firmware\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * Do everything that's needed to initialise things related to video
 * buffer queue, video node, and the related media entity. The caller
 * is expected to assign isys field and set the name of the video
 * device.
 */
int ipu7_isys_video_init(struct ipu7_isys_video *av)
{
	struct v4l2_format format = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix = {
			.width = 1920,
			.height = 1080,
		},
	};
	int ret;

	mutex_init(&av->mutex);
	av->vdev.device_caps = V4L2_CAP_STREAMING | V4L2_CAP_IO_MC |
		V4L2_CAP_VIDEO_CAPTURE;
	av->vdev.vfl_dir = VFL_DIR_RX;

	ret = ipu7_isys_queue_init(&av->aq);
	if (ret)
		goto out_mutex_destroy;

	av->pad.flags = MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	ret = media_entity_pads_init(&av->vdev.entity, 1, &av->pad);
	if (ret)
		goto out_vb2_queue_cleanup;

	av->vdev.entity.ops = &entity_ops;
	av->vdev.release = video_device_release_empty;
	av->vdev.fops = &isys_fops;
	av->vdev.v4l2_dev = &av->isys->v4l2_dev;
	av->vdev.dev_parent = &av->isys->adev->isp->pdev->dev;
	av->vdev.ioctl_ops = &ipu7_v4l2_ioctl_ops;
	av->vdev.queue = &av->aq.vbq;
	av->vdev.lock = &av->mutex;

	__ipu_isys_vidioc_try_fmt_vid_cap(av, &format);
	av->pix_fmt = format.fmt.pix;

	video_set_drvdata(&av->vdev, av);

	ret = video_register_device(&av->vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto out_media_entity_cleanup;

	return ret;

out_media_entity_cleanup:
	vb2_video_unregister_device(&av->vdev);
	media_entity_cleanup(&av->vdev.entity);

out_vb2_queue_cleanup:
	vb2_queue_release(&av->aq.vbq);

out_mutex_destroy:
	mutex_destroy(&av->mutex);

	return ret;
}

void ipu7_isys_video_cleanup(struct ipu7_isys_video *av)
{
	vb2_video_unregister_device(&av->vdev);
	media_entity_cleanup(&av->vdev.entity);
	mutex_destroy(&av->mutex);
}
