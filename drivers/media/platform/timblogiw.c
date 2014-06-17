/*
 * timblogiw.c timberdale FPGA LogiWin Video In driver
 * Copyright (c) 2009-2010 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * Timberdale FPGA LogiWin Video In
 */

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/scatterlist.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/videobuf-dma-contig.h>
#include <media/timb_video.h>

#define DRIVER_NAME			"timb-video"

#define TIMBLOGIWIN_NAME		"Timberdale Video-In"
#define TIMBLOGIW_VERSION_CODE		0x04

#define TIMBLOGIW_LINES_PER_DESC	44
#define TIMBLOGIW_MAX_VIDEO_MEM		16

#define TIMBLOGIW_HAS_DECODER(lw)	(lw->pdata.encoder.module_name)


struct timblogiw {
	struct video_device		video_dev;
	struct v4l2_device		v4l2_dev; /* mutual exclusion */
	struct mutex			lock;
	struct device			*dev;
	struct timb_video_platform_data pdata;
	struct v4l2_subdev		*sd_enc;	/* encoder */
	bool				opened;
};

struct timblogiw_tvnorm {
	v4l2_std_id std;
	u16     width;
	u16     height;
	u8	fps;
};

struct timblogiw_fh {
	struct videobuf_queue		vb_vidq;
	struct timblogiw_tvnorm const	*cur_norm;
	struct list_head		capture;
	struct dma_chan			*chan;
	spinlock_t			queue_lock; /* mutual exclusion */
	unsigned int			frame_count;
};

struct timblogiw_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer	vb;
	struct scatterlist	sg[16];
	dma_cookie_t		cookie;
	struct timblogiw_fh	*fh;
};

static const struct timblogiw_tvnorm timblogiw_tvnorms[] = {
	{
		.std			= V4L2_STD_PAL,
		.width			= 720,
		.height			= 576,
		.fps			= 25
	},
	{
		.std			= V4L2_STD_NTSC,
		.width			= 720,
		.height			= 480,
		.fps			= 30
	}
};

static int timblogiw_bytes_per_line(const struct timblogiw_tvnorm *norm)
{
	return norm->width * 2;
}


static int timblogiw_frame_size(const struct timblogiw_tvnorm *norm)
{
	return norm->height * timblogiw_bytes_per_line(norm);
}

static const struct timblogiw_tvnorm *timblogiw_get_norm(const v4l2_std_id std)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(timblogiw_tvnorms); i++)
		if (timblogiw_tvnorms[i].std & std)
			return timblogiw_tvnorms + i;

	/* default to first element */
	return timblogiw_tvnorms;
}

static void timblogiw_dma_cb(void *data)
{
	struct timblogiw_buffer *buf = data;
	struct timblogiw_fh *fh = buf->fh;
	struct videobuf_buffer *vb = &buf->vb;

	spin_lock(&fh->queue_lock);

	/* mark the transfer done */
	buf->cookie = -1;

	fh->frame_count++;

	if (vb->state != VIDEOBUF_ERROR) {
		list_del(&vb->queue);
		v4l2_get_timestamp(&vb->ts);
		vb->field_count = fh->frame_count * 2;
		vb->state = VIDEOBUF_DONE;

		wake_up(&vb->done);
	}

	if (!list_empty(&fh->capture)) {
		vb = list_entry(fh->capture.next, struct videobuf_buffer,
			queue);
		vb->state = VIDEOBUF_ACTIVE;
	}

	spin_unlock(&fh->queue_lock);
}

static bool timblogiw_dma_filter_fn(struct dma_chan *chan, void *filter_param)
{
	return chan->chan_id == (uintptr_t)filter_param;
}

/* IOCTL functions */

static int timblogiw_g_fmt(struct file *file, void  *priv,
	struct v4l2_format *format)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw *lw = video_get_drvdata(vdev);
	struct timblogiw_fh *fh = priv;

	dev_dbg(&vdev->dev, "%s entry\n", __func__);

	if (format->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	mutex_lock(&lw->lock);

	format->fmt.pix.width = fh->cur_norm->width;
	format->fmt.pix.height = fh->cur_norm->height;
	format->fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	format->fmt.pix.bytesperline = timblogiw_bytes_per_line(fh->cur_norm);
	format->fmt.pix.sizeimage = timblogiw_frame_size(fh->cur_norm);
	format->fmt.pix.field = V4L2_FIELD_NONE;

	mutex_unlock(&lw->lock);

	return 0;
}

static int timblogiw_try_fmt(struct file *file, void  *priv,
	struct v4l2_format *format)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_pix_format *pix = &format->fmt.pix;

	dev_dbg(&vdev->dev,
		"%s - width=%d, height=%d, pixelformat=%d, field=%d\n"
		"bytes per line %d, size image: %d, colorspace: %d\n",
		__func__,
		pix->width, pix->height, pix->pixelformat, pix->field,
		pix->bytesperline, pix->sizeimage, pix->colorspace);

	if (format->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (pix->field != V4L2_FIELD_NONE)
		return -EINVAL;

	if (pix->pixelformat != V4L2_PIX_FMT_UYVY)
		return -EINVAL;

	return 0;
}

static int timblogiw_s_fmt(struct file *file, void  *priv,
	struct v4l2_format *format)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw *lw = video_get_drvdata(vdev);
	struct timblogiw_fh *fh = priv;
	struct v4l2_pix_format *pix = &format->fmt.pix;
	int err;

	mutex_lock(&lw->lock);

	err = timblogiw_try_fmt(file, priv, format);
	if (err)
		goto out;

	if (videobuf_queue_is_busy(&fh->vb_vidq)) {
		dev_err(&vdev->dev, "%s queue busy\n", __func__);
		err = -EBUSY;
		goto out;
	}

	pix->width = fh->cur_norm->width;
	pix->height = fh->cur_norm->height;

out:
	mutex_unlock(&lw->lock);
	return err;
}

static int timblogiw_querycap(struct file *file, void  *priv,
	struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);

	dev_dbg(&vdev->dev, "%s: Entry\n",  __func__);
	strncpy(cap->card, TIMBLOGIWIN_NAME, sizeof(cap->card)-1);
	strncpy(cap->driver, DRIVER_NAME, sizeof(cap->driver) - 1);
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s", vdev->name);
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
		V4L2_CAP_READWRITE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int timblogiw_enum_fmt(struct file *file, void  *priv,
	struct v4l2_fmtdesc *fmt)
{
	struct video_device *vdev = video_devdata(file);

	dev_dbg(&vdev->dev, "%s, index: %d\n",  __func__, fmt->index);

	if (fmt->index != 0)
		return -EINVAL;
	memset(fmt, 0, sizeof(*fmt));
	fmt->index = 0;
	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	strncpy(fmt->description, "4:2:2, packed, YUYV",
		sizeof(fmt->description)-1);
	fmt->pixelformat = V4L2_PIX_FMT_UYVY;

	return 0;
}

static int timblogiw_g_parm(struct file *file, void *priv,
	struct v4l2_streamparm *sp)
{
	struct timblogiw_fh *fh = priv;
	struct v4l2_captureparm *cp = &sp->parm.capture;

	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe.numerator = 1;
	cp->timeperframe.denominator = fh->cur_norm->fps;

	return 0;
}

static int timblogiw_reqbufs(struct file *file, void  *priv,
	struct v4l2_requestbuffers *rb)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = priv;

	dev_dbg(&vdev->dev, "%s: entry\n",  __func__);

	return videobuf_reqbufs(&fh->vb_vidq, rb);
}

static int timblogiw_querybuf(struct file *file, void  *priv,
	struct v4l2_buffer *b)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = priv;

	dev_dbg(&vdev->dev, "%s: entry\n",  __func__);

	return videobuf_querybuf(&fh->vb_vidq, b);
}

static int timblogiw_qbuf(struct file *file, void  *priv, struct v4l2_buffer *b)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = priv;

	dev_dbg(&vdev->dev, "%s: entry\n",  __func__);

	return videobuf_qbuf(&fh->vb_vidq, b);
}

static int timblogiw_dqbuf(struct file *file, void  *priv,
	struct v4l2_buffer *b)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = priv;

	dev_dbg(&vdev->dev, "%s: entry\n",  __func__);

	return videobuf_dqbuf(&fh->vb_vidq, b, file->f_flags & O_NONBLOCK);
}

static int timblogiw_g_std(struct file *file, void  *priv, v4l2_std_id *std)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = priv;

	dev_dbg(&vdev->dev, "%s: entry\n",  __func__);

	*std = fh->cur_norm->std;
	return 0;
}

static int timblogiw_s_std(struct file *file, void  *priv, v4l2_std_id std)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw *lw = video_get_drvdata(vdev);
	struct timblogiw_fh *fh = priv;
	int err = 0;

	dev_dbg(&vdev->dev, "%s: entry\n",  __func__);

	mutex_lock(&lw->lock);

	if (TIMBLOGIW_HAS_DECODER(lw))
		err = v4l2_subdev_call(lw->sd_enc, video, s_std, std);

	if (!err)
		fh->cur_norm = timblogiw_get_norm(std);

	mutex_unlock(&lw->lock);

	return err;
}

static int timblogiw_enuminput(struct file *file, void  *priv,
	struct v4l2_input *inp)
{
	struct video_device *vdev = video_devdata(file);
	int i;

	dev_dbg(&vdev->dev, "%s: Entry\n",  __func__);

	if (inp->index != 0)
		return -EINVAL;

	inp->index = 0;

	strncpy(inp->name, "Timb input 1", sizeof(inp->name) - 1);
	inp->type = V4L2_INPUT_TYPE_CAMERA;

	inp->std = 0;
	for (i = 0; i < ARRAY_SIZE(timblogiw_tvnorms); i++)
		inp->std |= timblogiw_tvnorms[i].std;

	return 0;
}

static int timblogiw_g_input(struct file *file, void  *priv,
	unsigned int *input)
{
	struct video_device *vdev = video_devdata(file);

	dev_dbg(&vdev->dev, "%s: Entry\n",  __func__);

	*input = 0;

	return 0;
}

static int timblogiw_s_input(struct file *file, void  *priv, unsigned int input)
{
	struct video_device *vdev = video_devdata(file);

	dev_dbg(&vdev->dev, "%s: Entry\n",  __func__);

	if (input != 0)
		return -EINVAL;
	return 0;
}

static int timblogiw_streamon(struct file *file, void  *priv, enum v4l2_buf_type type)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = priv;

	dev_dbg(&vdev->dev, "%s: entry\n",  __func__);

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		dev_dbg(&vdev->dev, "%s - No capture device\n", __func__);
		return -EINVAL;
	}

	fh->frame_count = 0;
	return videobuf_streamon(&fh->vb_vidq);
}

static int timblogiw_streamoff(struct file *file, void  *priv,
	enum v4l2_buf_type type)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = priv;

	dev_dbg(&vdev->dev, "%s entry\n",  __func__);

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return videobuf_streamoff(&fh->vb_vidq);
}

static int timblogiw_querystd(struct file *file, void  *priv, v4l2_std_id *std)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw *lw = video_get_drvdata(vdev);
	struct timblogiw_fh *fh = priv;

	dev_dbg(&vdev->dev, "%s entry\n",  __func__);

	if (TIMBLOGIW_HAS_DECODER(lw))
		return v4l2_subdev_call(lw->sd_enc, video, querystd, std);
	else {
		*std = fh->cur_norm->std;
		return 0;
	}
}

static int timblogiw_enum_framesizes(struct file *file, void  *priv,
	struct v4l2_frmsizeenum *fsize)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = priv;

	dev_dbg(&vdev->dev, "%s - index: %d, format: %d\n",  __func__,
		fsize->index, fsize->pixel_format);

	if ((fsize->index != 0) ||
		(fsize->pixel_format != V4L2_PIX_FMT_UYVY))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = fh->cur_norm->width;
	fsize->discrete.height = fh->cur_norm->height;

	return 0;
}

/* Video buffer functions */

static int buffer_setup(struct videobuf_queue *vq, unsigned int *count,
	unsigned int *size)
{
	struct timblogiw_fh *fh = vq->priv_data;

	*size = timblogiw_frame_size(fh->cur_norm);

	if (!*count)
		*count = 32;

	while (*size * *count > TIMBLOGIW_MAX_VIDEO_MEM * 1024 * 1024)
		(*count)--;

	return 0;
}

static int buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
	enum v4l2_field field)
{
	struct timblogiw_fh *fh = vq->priv_data;
	struct timblogiw_buffer *buf = container_of(vb, struct timblogiw_buffer,
		vb);
	unsigned int data_size = timblogiw_frame_size(fh->cur_norm);
	int err = 0;

	if (vb->baddr && vb->bsize < data_size)
		/* User provided buffer, but it is too small */
		return -ENOMEM;

	vb->size = data_size;
	vb->width = fh->cur_norm->width;
	vb->height = fh->cur_norm->height;
	vb->field = field;

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		int i;
		unsigned int size;
		unsigned int bytes_per_desc = TIMBLOGIW_LINES_PER_DESC *
			timblogiw_bytes_per_line(fh->cur_norm);
		dma_addr_t addr;

		sg_init_table(buf->sg, ARRAY_SIZE(buf->sg));

		err = videobuf_iolock(vq, vb, NULL);
		if (err)
			goto err;

		addr = videobuf_to_dma_contig(vb);
		for (i = 0, size = 0; size < data_size; i++) {
			sg_dma_address(buf->sg + i) = addr + size;
			size += bytes_per_desc;
			sg_dma_len(buf->sg + i) = (size > data_size) ?
				(bytes_per_desc - (size - data_size)) :
				bytes_per_desc;
		}

		vb->state = VIDEOBUF_PREPARED;
		buf->cookie = -1;
		buf->fh = fh;
	}

	return 0;

err:
	videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
	return err;
}

static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct timblogiw_fh *fh = vq->priv_data;
	struct timblogiw_buffer *buf = container_of(vb, struct timblogiw_buffer,
		vb);
	struct dma_async_tx_descriptor *desc;
	int sg_elems;
	int bytes_per_desc = TIMBLOGIW_LINES_PER_DESC *
		timblogiw_bytes_per_line(fh->cur_norm);

	sg_elems = timblogiw_frame_size(fh->cur_norm) / bytes_per_desc;
	sg_elems +=
		(timblogiw_frame_size(fh->cur_norm) % bytes_per_desc) ? 1 : 0;

	if (list_empty(&fh->capture))
		vb->state = VIDEOBUF_ACTIVE;
	else
		vb->state = VIDEOBUF_QUEUED;

	list_add_tail(&vb->queue, &fh->capture);

	spin_unlock_irq(&fh->queue_lock);

	desc = dmaengine_prep_slave_sg(fh->chan,
		buf->sg, sg_elems, DMA_DEV_TO_MEM,
		DMA_PREP_INTERRUPT);
	if (!desc) {
		spin_lock_irq(&fh->queue_lock);
		list_del_init(&vb->queue);
		vb->state = VIDEOBUF_PREPARED;
		return;
	}

	desc->callback_param = buf;
	desc->callback = timblogiw_dma_cb;

	buf->cookie = desc->tx_submit(desc);

	spin_lock_irq(&fh->queue_lock);
}

static void buffer_release(struct videobuf_queue *vq,
	struct videobuf_buffer *vb)
{
	struct timblogiw_fh *fh = vq->priv_data;
	struct timblogiw_buffer *buf = container_of(vb, struct timblogiw_buffer,
		vb);

	videobuf_waiton(vq, vb, 0, 0);
	if (buf->cookie >= 0)
		dma_sync_wait(fh->chan, buf->cookie);

	videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static struct videobuf_queue_ops timblogiw_video_qops = {
	.buf_setup      = buffer_setup,
	.buf_prepare    = buffer_prepare,
	.buf_queue      = buffer_queue,
	.buf_release    = buffer_release,
};

/* Device Operations functions */

static int timblogiw_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw *lw = video_get_drvdata(vdev);
	struct timblogiw_fh *fh;
	v4l2_std_id std;
	dma_cap_mask_t mask;
	int err = 0;

	dev_dbg(&vdev->dev, "%s: entry\n", __func__);

	mutex_lock(&lw->lock);
	if (lw->opened) {
		err = -EBUSY;
		goto out;
	}

	if (TIMBLOGIW_HAS_DECODER(lw) && !lw->sd_enc) {
		struct i2c_adapter *adapt;

		/* find the video decoder */
		adapt = i2c_get_adapter(lw->pdata.i2c_adapter);
		if (!adapt) {
			dev_err(&vdev->dev, "No I2C bus #%d\n",
				lw->pdata.i2c_adapter);
			err = -ENODEV;
			goto out;
		}

		/* now find the encoder */
		lw->sd_enc = v4l2_i2c_new_subdev_board(&lw->v4l2_dev, adapt,
			lw->pdata.encoder.info, NULL);

		i2c_put_adapter(adapt);

		if (!lw->sd_enc) {
			dev_err(&vdev->dev, "Failed to get encoder: %s\n",
				lw->pdata.encoder.module_name);
			err = -ENODEV;
			goto out;
		}
	}

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh) {
		err = -ENOMEM;
		goto out;
	}

	fh->cur_norm = timblogiw_tvnorms;
	timblogiw_querystd(file, fh, &std);
	fh->cur_norm = timblogiw_get_norm(std);

	INIT_LIST_HEAD(&fh->capture);
	spin_lock_init(&fh->queue_lock);

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);

	/* find the DMA channel */
	fh->chan = dma_request_channel(mask, timblogiw_dma_filter_fn,
			(void *)(uintptr_t)lw->pdata.dma_channel);
	if (!fh->chan) {
		dev_err(&vdev->dev, "Failed to get DMA channel\n");
		kfree(fh);
		err = -ENODEV;
		goto out;
	}

	file->private_data = fh;
	videobuf_queue_dma_contig_init(&fh->vb_vidq,
		&timblogiw_video_qops, lw->dev, &fh->queue_lock,
		V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
		sizeof(struct timblogiw_buffer), fh, NULL);

	lw->opened = true;
out:
	mutex_unlock(&lw->lock);

	return err;
}

static int timblogiw_close(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw *lw = video_get_drvdata(vdev);
	struct timblogiw_fh *fh = file->private_data;

	dev_dbg(&vdev->dev, "%s: Entry\n",  __func__);

	videobuf_stop(&fh->vb_vidq);
	videobuf_mmap_free(&fh->vb_vidq);

	dma_release_channel(fh->chan);

	kfree(fh);

	mutex_lock(&lw->lock);
	lw->opened = false;
	mutex_unlock(&lw->lock);
	return 0;
}

static ssize_t timblogiw_read(struct file *file, char __user *data,
	size_t count, loff_t *ppos)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = file->private_data;

	dev_dbg(&vdev->dev, "%s: entry\n",  __func__);

	return videobuf_read_stream(&fh->vb_vidq, data, count, ppos, 0,
		file->f_flags & O_NONBLOCK);
}

static unsigned int timblogiw_poll(struct file *file,
	struct poll_table_struct *wait)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = file->private_data;

	dev_dbg(&vdev->dev, "%s: entry\n",  __func__);

	return videobuf_poll_stream(file, &fh->vb_vidq, wait);
}

static int timblogiw_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vdev = video_devdata(file);
	struct timblogiw_fh *fh = file->private_data;

	dev_dbg(&vdev->dev, "%s: entry\n", __func__);

	return videobuf_mmap_mapper(&fh->vb_vidq, vma);
}

/* Platform device functions */

static struct v4l2_ioctl_ops timblogiw_ioctl_ops = {
	.vidioc_querycap		= timblogiw_querycap,
	.vidioc_enum_fmt_vid_cap	= timblogiw_enum_fmt,
	.vidioc_g_fmt_vid_cap		= timblogiw_g_fmt,
	.vidioc_try_fmt_vid_cap		= timblogiw_try_fmt,
	.vidioc_s_fmt_vid_cap		= timblogiw_s_fmt,
	.vidioc_g_parm			= timblogiw_g_parm,
	.vidioc_reqbufs			= timblogiw_reqbufs,
	.vidioc_querybuf		= timblogiw_querybuf,
	.vidioc_qbuf			= timblogiw_qbuf,
	.vidioc_dqbuf			= timblogiw_dqbuf,
	.vidioc_g_std			= timblogiw_g_std,
	.vidioc_s_std			= timblogiw_s_std,
	.vidioc_enum_input		= timblogiw_enuminput,
	.vidioc_g_input			= timblogiw_g_input,
	.vidioc_s_input			= timblogiw_s_input,
	.vidioc_streamon		= timblogiw_streamon,
	.vidioc_streamoff		= timblogiw_streamoff,
	.vidioc_querystd		= timblogiw_querystd,
	.vidioc_enum_framesizes		= timblogiw_enum_framesizes,
};

static struct v4l2_file_operations timblogiw_fops = {
	.owner		= THIS_MODULE,
	.open		= timblogiw_open,
	.release	= timblogiw_close,
	.unlocked_ioctl		= video_ioctl2, /* V4L2 ioctl handler */
	.mmap		= timblogiw_mmap,
	.read		= timblogiw_read,
	.poll		= timblogiw_poll,
};

static struct video_device timblogiw_template = {
	.name		= TIMBLOGIWIN_NAME,
	.fops		= &timblogiw_fops,
	.ioctl_ops	= &timblogiw_ioctl_ops,
	.release	= video_device_release_empty,
	.minor		= -1,
	.tvnorms	= V4L2_STD_PAL | V4L2_STD_NTSC
};

static int timblogiw_probe(struct platform_device *pdev)
{
	int err;
	struct timblogiw *lw = NULL;
	struct timb_video_platform_data *pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data\n");
		err = -EINVAL;
		goto err;
	}

	if (!pdata->encoder.module_name)
		dev_info(&pdev->dev, "Running without decoder\n");

	lw = devm_kzalloc(&pdev->dev, sizeof(*lw), GFP_KERNEL);
	if (!lw) {
		err = -ENOMEM;
		goto err;
	}

	if (pdev->dev.parent)
		lw->dev = pdev->dev.parent;
	else
		lw->dev = &pdev->dev;

	memcpy(&lw->pdata, pdata, sizeof(lw->pdata));

	mutex_init(&lw->lock);

	lw->video_dev = timblogiw_template;

	strlcpy(lw->v4l2_dev.name, DRIVER_NAME, sizeof(lw->v4l2_dev.name));
	err = v4l2_device_register(NULL, &lw->v4l2_dev);
	if (err)
		goto err;

	lw->video_dev.v4l2_dev = &lw->v4l2_dev;

	platform_set_drvdata(pdev, lw);
	video_set_drvdata(&lw->video_dev, lw);

	err = video_register_device(&lw->video_dev, VFL_TYPE_GRABBER, 0);
	if (err) {
		dev_err(&pdev->dev, "Error reg video: %d\n", err);
		goto err_request;
	}

	return 0;

err_request:
	v4l2_device_unregister(&lw->v4l2_dev);
err:
	dev_err(&pdev->dev, "Failed to register: %d\n", err);

	return err;
}

static int timblogiw_remove(struct platform_device *pdev)
{
	struct timblogiw *lw = platform_get_drvdata(pdev);

	video_unregister_device(&lw->video_dev);

	v4l2_device_unregister(&lw->v4l2_dev);

	return 0;
}

static struct platform_driver timblogiw_platform_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= timblogiw_probe,
	.remove		= timblogiw_remove,
};

module_platform_driver(timblogiw_platform_driver);

MODULE_DESCRIPTION(TIMBLOGIWIN_NAME);
MODULE_AUTHOR("Pelagicore AB <info@pelagicore.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:"DRIVER_NAME);
