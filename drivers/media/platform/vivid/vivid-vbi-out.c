/*
 * vivid-vbi-out.c - vbi output support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>

#include "vivid-core.h"
#include "vivid-kthread-out.h"
#include "vivid-vbi-out.h"
#include "vivid-vbi-cap.h"

static int vbi_out_queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
		       unsigned *nbuffers, unsigned *nplanes,
		       unsigned sizes[], void *alloc_ctxs[])
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);
	bool is_60hz = dev->std_out & V4L2_STD_525_60;
	unsigned size = vq->type == V4L2_BUF_TYPE_SLICED_VBI_OUTPUT ?
		36 * sizeof(struct v4l2_sliced_vbi_data) :
		1440 * 2 * (is_60hz ? 12 : 18);

	if (!vivid_is_svid_out(dev))
		return -EINVAL;

	sizes[0] = size;

	if (vq->num_buffers + *nbuffers < 2)
		*nbuffers = 2 - vq->num_buffers;

	*nplanes = 1;
	return 0;
}

static int vbi_out_buf_prepare(struct vb2_buffer *vb)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	bool is_60hz = dev->std_out & V4L2_STD_525_60;
	unsigned size = vb->vb2_queue->type == V4L2_BUF_TYPE_SLICED_VBI_OUTPUT ?
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

static void vbi_out_buf_queue(struct vb2_buffer *vb)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vivid_buffer *buf = container_of(vb, struct vivid_buffer, vb);

	dprintk(dev, 1, "%s\n", __func__);

	spin_lock(&dev->slock);
	list_add_tail(&buf->list, &dev->vbi_out_active);
	spin_unlock(&dev->slock);
}

static int vbi_out_start_streaming(struct vb2_queue *vq, unsigned count)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);
	int err;

	dprintk(dev, 1, "%s\n", __func__);
	dev->vbi_out_seq_count = 0;
	if (dev->start_streaming_error) {
		dev->start_streaming_error = false;
		err = -EINVAL;
	} else {
		err = vivid_start_generating_vid_out(dev, &dev->vbi_out_streaming);
	}
	if (err) {
		struct vivid_buffer *buf, *tmp;

		list_for_each_entry_safe(buf, tmp, &dev->vbi_out_active, list) {
			list_del(&buf->list);
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_QUEUED);
		}
	}
	return err;
}

/* abort streaming and wait for last buffer */
static void vbi_out_stop_streaming(struct vb2_queue *vq)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);

	dprintk(dev, 1, "%s\n", __func__);
	vivid_stop_generating_vid_out(dev, &dev->vbi_out_streaming);
	dev->vbi_out_have_wss = false;
	dev->vbi_out_have_cc[0] = false;
	dev->vbi_out_have_cc[1] = false;
}

const struct vb2_ops vivid_vbi_out_qops = {
	.queue_setup		= vbi_out_queue_setup,
	.buf_prepare		= vbi_out_buf_prepare,
	.buf_queue		= vbi_out_buf_queue,
	.start_streaming	= vbi_out_start_streaming,
	.stop_streaming		= vbi_out_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

int vidioc_g_fmt_vbi_out(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_vbi_format *vbi = &f->fmt.vbi;
	bool is_60hz = dev->std_out & V4L2_STD_525_60;

	if (!vivid_is_svid_out(dev) || !dev->has_raw_vbi_out)
		return -EINVAL;

	vbi->sampling_rate = 25000000;
	vbi->offset = 24;
	vbi->samples_per_line = 1440;
	vbi->sample_format = V4L2_PIX_FMT_GREY;
	vbi->start[0] = is_60hz ? V4L2_VBI_ITU_525_F1_START + 9 : V4L2_VBI_ITU_625_F1_START + 5;
	vbi->start[1] = is_60hz ? V4L2_VBI_ITU_525_F2_START + 9 : V4L2_VBI_ITU_625_F2_START + 5;
	vbi->count[0] = vbi->count[1] = is_60hz ? 12 : 18;
	vbi->flags = dev->vbi_cap_interlaced ? V4L2_VBI_INTERLACED : 0;
	vbi->reserved[0] = 0;
	vbi->reserved[1] = 0;
	return 0;
}

int vidioc_s_fmt_vbi_out(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct vivid_dev *dev = video_drvdata(file);
	int ret = vidioc_g_fmt_vbi_out(file, priv, f);

	if (ret)
		return ret;
	if (vb2_is_busy(&dev->vb_vbi_out_q))
		return -EBUSY;
	dev->stream_sliced_vbi_out = false;
	dev->vbi_out_dev.queue->type = V4L2_BUF_TYPE_VBI_OUTPUT;
	return 0;
}

int vidioc_g_fmt_sliced_vbi_out(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_sliced_vbi_format *vbi = &fmt->fmt.sliced;

	if (!vivid_is_svid_out(dev) || !dev->has_sliced_vbi_out)
		return -EINVAL;

	vivid_fill_service_lines(vbi, dev->service_set_out);
	return 0;
}

int vidioc_try_fmt_sliced_vbi_out(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_sliced_vbi_format *vbi = &fmt->fmt.sliced;
	bool is_60hz = dev->std_out & V4L2_STD_525_60;
	u32 service_set = vbi->service_set;

	if (!vivid_is_svid_out(dev) || !dev->has_sliced_vbi_out)
		return -EINVAL;

	service_set &= is_60hz ? V4L2_SLICED_CAPTION_525 :
				 V4L2_SLICED_WSS_625 | V4L2_SLICED_TELETEXT_B;
	vivid_fill_service_lines(vbi, service_set);
	return 0;
}

int vidioc_s_fmt_sliced_vbi_out(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_sliced_vbi_format *vbi = &fmt->fmt.sliced;
	int ret = vidioc_try_fmt_sliced_vbi_out(file, fh, fmt);

	if (ret)
		return ret;
	if (vb2_is_busy(&dev->vb_vbi_out_q))
		return -EBUSY;
	dev->service_set_out = vbi->service_set;
	dev->stream_sliced_vbi_out = true;
	dev->vbi_out_dev.queue->type = V4L2_BUF_TYPE_SLICED_VBI_OUTPUT;
	return 0;
}

void vivid_sliced_vbi_out_process(struct vivid_dev *dev, struct vivid_buffer *buf)
{
	struct v4l2_sliced_vbi_data *vbi = vb2_plane_vaddr(&buf->vb, 0);
	unsigned elems = vb2_get_plane_payload(&buf->vb, 0) / sizeof(*vbi);

	dev->vbi_out_have_cc[0] = false;
	dev->vbi_out_have_cc[1] = false;
	dev->vbi_out_have_wss = false;
	while (elems--) {
		switch (vbi->id) {
		case V4L2_SLICED_CAPTION_525:
			if ((dev->std_out & V4L2_STD_525_60) && vbi->line == 21) {
				dev->vbi_out_have_cc[!!vbi->field] = true;
				dev->vbi_out_cc[!!vbi->field][0] = vbi->data[0];
				dev->vbi_out_cc[!!vbi->field][1] = vbi->data[1];
			}
			break;
		case V4L2_SLICED_WSS_625:
			if ((dev->std_out & V4L2_STD_625_50) &&
			    vbi->field == 0 && vbi->line == 23) {
				dev->vbi_out_have_wss = true;
				dev->vbi_out_wss[0] = vbi->data[0];
				dev->vbi_out_wss[1] = vbi->data[1];
			}
			break;
		}
		vbi++;
	}
}
