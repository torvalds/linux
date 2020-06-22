// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-meta-out.c - meta output support functions.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <linux/usb/video.h>

#include "vivid-core.h"
#include "vivid-kthread-out.h"
#include "vivid-meta-out.h"

static int meta_out_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
				unsigned int *nplanes, unsigned int sizes[],
				struct device *alloc_devs[])
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);
	unsigned int size =  sizeof(struct vivid_meta_out_buf);

	if (!vivid_is_webcam(dev))
		return -EINVAL;

	if (*nplanes) {
		if (sizes[0] < size)
			return -EINVAL;
	} else {
		sizes[0] = size;
	}

	if (vq->num_buffers + *nbuffers < 2)
		*nbuffers = 2 - vq->num_buffers;

	*nplanes = 1;
	return 0;
}

static int meta_out_buf_prepare(struct vb2_buffer *vb)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	unsigned int size = sizeof(struct vivid_meta_out_buf);

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

static void meta_out_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vivid_buffer *buf = container_of(vbuf, struct vivid_buffer, vb);

	dprintk(dev, 1, "%s\n", __func__);

	spin_lock(&dev->slock);
	list_add_tail(&buf->list, &dev->meta_out_active);
	spin_unlock(&dev->slock);
}

static int meta_out_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);
	int err;

	dprintk(dev, 1, "%s\n", __func__);
	dev->meta_out_seq_count = 0;
	if (dev->start_streaming_error) {
		dev->start_streaming_error = false;
		err = -EINVAL;
	} else {
		err = vivid_start_generating_vid_out(dev,
						     &dev->meta_out_streaming);
	}
	if (err) {
		struct vivid_buffer *buf, *tmp;

		list_for_each_entry_safe(buf, tmp,
					 &dev->meta_out_active, list) {
			list_del(&buf->list);
			vb2_buffer_done(&buf->vb.vb2_buf,
					VB2_BUF_STATE_QUEUED);
		}
	}
	return err;
}

/* abort streaming and wait for last buffer */
static void meta_out_stop_streaming(struct vb2_queue *vq)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vq);

	dprintk(dev, 1, "%s\n", __func__);
	vivid_stop_generating_vid_out(dev, &dev->meta_out_streaming);
}

static void meta_out_buf_request_complete(struct vb2_buffer *vb)
{
	struct vivid_dev *dev = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &dev->ctrl_hdl_meta_out);
}

const struct vb2_ops vivid_meta_out_qops = {
	.queue_setup            = meta_out_queue_setup,
	.buf_prepare            = meta_out_buf_prepare,
	.buf_queue              = meta_out_buf_queue,
	.start_streaming        = meta_out_start_streaming,
	.stop_streaming         = meta_out_stop_streaming,
	.buf_request_complete   = meta_out_buf_request_complete,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
};

int vidioc_enum_fmt_meta_out(struct file *file, void  *priv,
			     struct v4l2_fmtdesc *f)
{
	struct vivid_dev *dev = video_drvdata(file);

	if (!vivid_is_webcam(dev))
		return -EINVAL;

	if (f->index > 0)
		return -EINVAL;

	f->type = V4L2_BUF_TYPE_META_OUTPUT;
	f->pixelformat = V4L2_META_FMT_VIVID;
	return 0;
}

int vidioc_g_fmt_meta_out(struct file *file, void *priv,
			  struct v4l2_format *f)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (!vivid_is_webcam(dev) || !dev->has_meta_out)
		return -EINVAL;

	meta->dataformat = V4L2_META_FMT_VIVID;
	meta->buffersize = sizeof(struct vivid_meta_out_buf);
	return 0;
}

void vivid_meta_out_process(struct vivid_dev *dev,
			    struct vivid_buffer *buf)
{
	struct vivid_meta_out_buf *meta = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

	tpg_s_brightness(&dev->tpg, meta->brightness);
	tpg_s_contrast(&dev->tpg, meta->contrast);
	tpg_s_saturation(&dev->tpg, meta->saturation);
	tpg_s_hue(&dev->tpg, meta->hue);
	dprintk(dev, 2, " %s brightness %u contrast %u saturation %u hue %d\n",
		__func__, meta->brightness, meta->contrast,
		meta->saturation, meta->hue);
}
