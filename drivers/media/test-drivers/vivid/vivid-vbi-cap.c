// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-vbi-cap.c - vbi capture support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>

#include "vivid-core.h"
#include "vivid-kthread-cap.h"
#include "vivid-vbi-cap.h"
#include "vivid-vbi-gen.h"

static void vivid_sliced_vbi_cap_fill(struct vivid_dev *dev, unsigned seqnr)
{
	struct vivid_vbi_gen_data *vbi_gen = &dev->vbi_gen;
	bool is_60hz = dev->std_cap[dev->input] & V4L2_STD_525_60;

	vivid_vbi_gen_sliced(vbi_gen, is_60hz, seqnr);

	if (!is_60hz) {
		if (dev->loop_video) {
			if (dev->vbi_out_have_wss) {
				vbi_gen->data[12].data[0] = dev->vbi_out_wss[0];
				vbi_gen->data[12].data[1] = dev->vbi_out_wss[1];
			} else {
				vbi_gen->data[12].id = 0;
			}
		} else {
			switch (tpg_g_video_aspect(&dev->tpg)) {
			case TPG_VIDEO_ASPECT_14X9_CENTRE:
				vbi_gen->data[12].data[0] = 0x01;
				break;
			case TPG_VIDEO_ASPECT_16X9_CENTRE:
				vbi_gen->data[12].data[0] = 0x0b;
				break;
			case TPG_VIDEO_ASPECT_16X9_ANAMORPHIC:
				vbi_gen->data[12].data[0] = 0x07;
				break;
			case TPG_VIDEO_ASPECT_4X3:
			default:
				vbi_gen->data[12].data[0] = 0x08;
				break;
			}
		}
	} else if (dev->loop_video && is_60hz) {
		if (dev->vbi_out_have_cc[0]) {
			vbi_gen->data[0].data[0] = dev->vbi_out_cc[0][0];
			vbi_gen->data[0].data[1] = dev->vbi_out_cc[0][1];
		} else {
			vbi_gen->data[0].id = 0;
		}
		if (dev->vbi_out_have_cc[1]) {
			vbi_gen->data[1].data[0] = dev->vbi_out_cc[1][0];
			vbi_gen->data[1].data[1] = dev->vbi_out_cc[1][1];
		} else {
			vbi_gen->data[1].id = 0;
		}
	}
}

static void vivid_g_fmt_vbi_cap(struct vivid_dev *dev, struct v4l2_vbi_format *vbi)
{
	bool is_60hz = dev->std_cap[dev->input] & V4L2_STD_525_60;

	vbi->sampling_rate = 27000000;
	vbi->offset = 24;
	vbi->samples_per_line = 1440;
	vbi->sample_format = V4L2_PIX_FMT_GREY;
	vbi->start[0] = is_60hz ? V4L2_VBI_ITU_525_F1_START + 9 : V4L2_VBI_ITU_625_F1_START + 5;
	vbi->start[1] = is_60hz ? V4L2_VBI_ITU_525_F2_START + 9 : V4L2_VBI_ITU_625_F2_START + 5;
	vbi->count[0] = vbi->count[1] = is_60hz ? 12 : 18;
	vbi->flags = dev->vbi_cap_interlaced ? V4L2_VBI_INTERLACED : 0;
	vbi->reserved[0] = 0;
	vbi->reserved[1] = 0;
}

void vivid_raw_vbi_cap_process(struct vivid_dev *dev, struct vivid_buffer *buf)
{
	struct v4l2_vbi_format vbi;
	u8 *vbuf = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

	vivid_g_fmt_vbi_cap(dev, &vbi);
	buf->vb.sequence = dev->vbi_cap_seq_count;
	if (dev->field_cap == V4L2_FIELD_ALTERNATE)
		buf->vb.sequence /= 2;

	vivid_sliced_vbi_cap_fill(dev, buf->vb.sequence);

	memset(vbuf, 0x10, vb2_plane_size(&buf->vb.vb2_buf, 0));

	if (!VIVID_INVALID_SIGNAL(dev->std_signal_mode[dev->input]))
		vivid_vbi_gen_raw(&dev->vbi_gen, &vbi, vbuf);
}


void vivid_sliced_vbi_cap_process(struct vivid_dev *dev,
			struct vivid_buffer *buf)
{
	struct v4l2_sliced_vbi_data *vbuf =
			vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

	buf->vb.sequence = dev->vbi_cap_seq_count;
	if (dev->field_cap == V4L2_FIELD_ALTERNATE)
		buf->vb.sequence /= 2;

	vivid_sliced_vbi_cap_fill(dev, buf->vb.sequence);

	memset(vbuf, 0, vb2_plane_size(&buf->vb.vb2_buf, 0));
	if (!VIVID_INVALID_SIGNAL(dev->std_signal_mode[dev->input])) {
		unsigned i;

		for (i = 0; i < 25; i++)
			vbuf[i] = dev->vbi_gen.data[i];
	}
}

static int vbi_cap_queue_setup(struct vb2_queue *vq,
		       unsigned *nbuffers, unsigned *nplanes,
		       unsigned sizes[], struct device *alloc_devs[])
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);
	bool is_60hz = dev->std_cap[dev->input] & V4L2_STD_525_60;
	unsigned size = vq->type == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE ?
		36 * sizeof(struct v4l2_sliced_vbi_data) :
		1440 * 2 * (is_60hz ? 12 : 18);

	if (!vivid_is_sdtv_cap(dev))
		return -EINVAL;

	sizes[0] = size;

	*nplanes = 1;
	return 0;
}

static int vbi_cap_buf_prepare(struct vb2_buffer *vb)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	bool is_60hz = dev->std_cap[dev->input] & V4L2_STD_525_60;
	unsigned size = vb->vb2_queue->type == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE ?
		36 * sizeof(struct v4l2_sliced_vbi_data) :
		1440 * 2 * (is_60hz ? 12 : 18);

	dprintk(dev, 1, "%s\n", __func__);

	if (dev->buf_prepare_error) {
		/*
		 * Error injection: test what happens if buf_prepare() returns
		 * an error.
		 */
		dev->buf_prepare_error = false;
		return -EINVAL;
	}
	if (vb2_plane_size(vb, 0) < size) {
		dprintk(dev, 1, "%s data will not fit into plane (%lu < %u)\n",
				__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	vb2_set_plane_payload(vb, 0, size);

	return 0;
}

static void vbi_cap_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vivid_buffer *buf = container_of(vbuf, struct vivid_buffer, vb);

	dprintk(dev, 1, "%s\n", __func__);

	spin_lock(&dev->slock);
	list_add_tail(&buf->list, &dev->vbi_cap_active);
	spin_unlock(&dev->slock);
}

static int vbi_cap_start_streaming(struct vb2_queue *vq, unsigned count)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);
	int err;

	dprintk(dev, 1, "%s\n", __func__);
	dev->vbi_cap_seq_count = 0;
	if (dev->start_streaming_error) {
		dev->start_streaming_error = false;
		err = -EINVAL;
	} else {
		err = vivid_start_generating_vid_cap(dev, &dev->vbi_cap_streaming);
	}
	if (err) {
		struct vivid_buffer *buf, *tmp;

		list_for_each_entry_safe(buf, tmp, &dev->vbi_cap_active, list) {
			list_del(&buf->list);
			vb2_buffer_done(&buf->vb.vb2_buf,
					VB2_BUF_STATE_QUEUED);
		}
	}
	return err;
}

/* abort streaming and wait for last buffer */
static void vbi_cap_stop_streaming(struct vb2_queue *vq)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);

	dprintk(dev, 1, "%s\n", __func__);
	vivid_stop_generating_vid_cap(dev, &dev->vbi_cap_streaming);
}

static void vbi_cap_buf_request_complete(struct vb2_buffer *vb)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &dev->ctrl_hdl_vbi_cap);
}

const struct vb2_ops vivid_vbi_cap_qops = {
	.queue_setup		= vbi_cap_queue_setup,
	.buf_prepare		= vbi_cap_buf_prepare,
	.buf_queue		= vbi_cap_buf_queue,
	.start_streaming	= vbi_cap_start_streaming,
	.stop_streaming		= vbi_cap_stop_streaming,
	.buf_request_complete	= vbi_cap_buf_request_complete,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

int vidioc_g_fmt_vbi_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_vbi_format *vbi = &f->fmt.vbi;

	if (!vivid_is_sdtv_cap(dev) || !dev->has_raw_vbi_cap)
		return -EINVAL;

	vivid_g_fmt_vbi_cap(dev, vbi);
	return 0;
}

int vidioc_s_fmt_vbi_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct vivid_dev *dev = video_drvdata(file);
	int ret = vidioc_g_fmt_vbi_cap(file, priv, f);

	if (ret)
		return ret;
	if (f->type != V4L2_BUF_TYPE_VBI_CAPTURE && vb2_is_busy(&dev->vb_vbi_cap_q))
		return -EBUSY;
	return 0;
}

void vivid_fill_service_lines(struct v4l2_sliced_vbi_format *vbi, u32 service_set)
{
	vbi->io_size = sizeof(struct v4l2_sliced_vbi_data) * 36;
	vbi->service_set = service_set;
	memset(vbi->service_lines, 0, sizeof(vbi->service_lines));
	memset(vbi->reserved, 0, sizeof(vbi->reserved));

	if (vbi->service_set == 0)
		return;

	if (vbi->service_set & V4L2_SLICED_CAPTION_525) {
		vbi->service_lines[0][21] = V4L2_SLICED_CAPTION_525;
		vbi->service_lines[1][21] = V4L2_SLICED_CAPTION_525;
	}
	if (vbi->service_set & V4L2_SLICED_WSS_625) {
		unsigned i;

		for (i = 7; i <= 18; i++)
			vbi->service_lines[0][i] =
			vbi->service_lines[1][i] = V4L2_SLICED_TELETEXT_B;
		vbi->service_lines[0][23] = V4L2_SLICED_WSS_625;
	}
}

int vidioc_g_fmt_sliced_vbi_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_sliced_vbi_format *vbi = &fmt->fmt.sliced;

	if (!vivid_is_sdtv_cap(dev) || !dev->has_sliced_vbi_cap)
		return -EINVAL;

	vivid_fill_service_lines(vbi, dev->service_set_cap);
	return 0;
}

int vidioc_try_fmt_sliced_vbi_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_sliced_vbi_format *vbi = &fmt->fmt.sliced;
	bool is_60hz = dev->std_cap[dev->input] & V4L2_STD_525_60;
	u32 service_set = vbi->service_set;

	if (!vivid_is_sdtv_cap(dev) || !dev->has_sliced_vbi_cap)
		return -EINVAL;

	service_set &= is_60hz ? V4L2_SLICED_CAPTION_525 :
				 V4L2_SLICED_WSS_625 | V4L2_SLICED_TELETEXT_B;
	vivid_fill_service_lines(vbi, service_set);
	return 0;
}

int vidioc_s_fmt_sliced_vbi_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_sliced_vbi_format *vbi = &fmt->fmt.sliced;
	int ret = vidioc_try_fmt_sliced_vbi_cap(file, fh, fmt);

	if (ret)
		return ret;
	if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE && vb2_is_busy(&dev->vb_vbi_cap_q))
		return -EBUSY;
	dev->service_set_cap = vbi->service_set;
	return 0;
}

int vidioc_g_sliced_vbi_cap(struct file *file, void *fh, struct v4l2_sliced_vbi_cap *cap)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);
	bool is_60hz;

	if (vdev->vfl_dir == VFL_DIR_RX) {
		is_60hz = dev->std_cap[dev->input] & V4L2_STD_525_60;
		if (!vivid_is_sdtv_cap(dev) || !dev->has_sliced_vbi_cap ||
		    cap->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
			return -EINVAL;
	} else {
		is_60hz = dev->std_out & V4L2_STD_525_60;
		if (!vivid_is_svid_out(dev) || !dev->has_sliced_vbi_out ||
		    cap->type != V4L2_BUF_TYPE_SLICED_VBI_OUTPUT)
			return -EINVAL;
	}

	cap->service_set = is_60hz ? V4L2_SLICED_CAPTION_525 :
				     V4L2_SLICED_WSS_625 | V4L2_SLICED_TELETEXT_B;
	if (is_60hz) {
		cap->service_lines[0][21] = V4L2_SLICED_CAPTION_525;
		cap->service_lines[1][21] = V4L2_SLICED_CAPTION_525;
	} else {
		unsigned i;

		for (i = 7; i <= 18; i++)
			cap->service_lines[0][i] =
			cap->service_lines[1][i] = V4L2_SLICED_TELETEXT_B;
		cap->service_lines[0][23] = V4L2_SLICED_WSS_625;
	}
	return 0;
}
