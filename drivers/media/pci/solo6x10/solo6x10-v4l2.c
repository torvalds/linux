// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010-2013 Bluecherry, LLC <https://www.bluecherrydvr.com>
 *
 * Original author:
 * Ben Collins <bcollins@ubuntu.com>
 *
 * Additional work by:
 * John Brooks <john.brooks@bluecherry.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "solo6x10.h"
#include "solo6x10-tw28.h"

/* Image size is two fields, SOLO_HW_BPL is one horizontal line in hardware */
#define SOLO_HW_BPL		2048
#define solo_vlines(__solo)	(__solo->video_vsize * 2)
#define solo_image_size(__solo) (solo_bytesperline(__solo) * \
				 solo_vlines(__solo))
#define solo_bytesperline(__solo) (__solo->video_hsize * 2)

#define MIN_VID_BUFFERS		2

static inline void erase_on(struct solo_dev *solo_dev)
{
	solo_reg_write(solo_dev, SOLO_VO_DISP_ERASE, SOLO_VO_DISP_ERASE_ON);
	solo_dev->erasing = 1;
	solo_dev->frame_blank = 0;
}

static inline int erase_off(struct solo_dev *solo_dev)
{
	if (!solo_dev->erasing)
		return 0;

	/* First time around, assert erase off */
	if (!solo_dev->frame_blank)
		solo_reg_write(solo_dev, SOLO_VO_DISP_ERASE, 0);
	/* Keep the erasing flag on for 8 frames minimum */
	if (solo_dev->frame_blank++ >= 8)
		solo_dev->erasing = 0;

	return 1;
}

void solo_video_in_isr(struct solo_dev *solo_dev)
{
	wake_up_interruptible_all(&solo_dev->disp_thread_wait);
}

static void solo_win_setup(struct solo_dev *solo_dev, u8 ch,
			   int sx, int sy, int ex, int ey, int scale)
{
	if (ch >= solo_dev->nr_chans)
		return;

	/* Here, we just keep window/channel the same */
	solo_reg_write(solo_dev, SOLO_VI_WIN_CTRL0(ch),
		       SOLO_VI_WIN_CHANNEL(ch) |
		       SOLO_VI_WIN_SX(sx) |
		       SOLO_VI_WIN_EX(ex) |
		       SOLO_VI_WIN_SCALE(scale));

	solo_reg_write(solo_dev, SOLO_VI_WIN_CTRL1(ch),
		       SOLO_VI_WIN_SY(sy) |
		       SOLO_VI_WIN_EY(ey));
}

static int solo_v4l2_ch_ext_4up(struct solo_dev *solo_dev, u8 idx, int on)
{
	u8 ch = idx * 4;

	if (ch >= solo_dev->nr_chans)
		return -EINVAL;

	if (!on) {
		u8 i;

		for (i = ch; i < ch + 4; i++)
			solo_win_setup(solo_dev, i, solo_dev->video_hsize,
				       solo_vlines(solo_dev),
				       solo_dev->video_hsize,
				       solo_vlines(solo_dev), 0);
		return 0;
	}

	/* Row 1 */
	solo_win_setup(solo_dev, ch, 0, 0, solo_dev->video_hsize / 2,
		       solo_vlines(solo_dev) / 2, 3);
	solo_win_setup(solo_dev, ch + 1, solo_dev->video_hsize / 2, 0,
		       solo_dev->video_hsize, solo_vlines(solo_dev) / 2, 3);
	/* Row 2 */
	solo_win_setup(solo_dev, ch + 2, 0, solo_vlines(solo_dev) / 2,
		       solo_dev->video_hsize / 2, solo_vlines(solo_dev), 3);
	solo_win_setup(solo_dev, ch + 3, solo_dev->video_hsize / 2,
		       solo_vlines(solo_dev) / 2, solo_dev->video_hsize,
		       solo_vlines(solo_dev), 3);

	return 0;
}

static int solo_v4l2_ch_ext_16up(struct solo_dev *solo_dev, int on)
{
	int sy, ysize, hsize, i;

	if (!on) {
		for (i = 0; i < 16; i++)
			solo_win_setup(solo_dev, i, solo_dev->video_hsize,
				       solo_vlines(solo_dev),
				       solo_dev->video_hsize,
				       solo_vlines(solo_dev), 0);
		return 0;
	}

	ysize = solo_vlines(solo_dev) / 4;
	hsize = solo_dev->video_hsize / 4;

	for (sy = 0, i = 0; i < 4; i++, sy += ysize) {
		solo_win_setup(solo_dev, i * 4, 0, sy, hsize,
			       sy + ysize, 5);
		solo_win_setup(solo_dev, (i * 4) + 1, hsize, sy,
			       hsize * 2, sy + ysize, 5);
		solo_win_setup(solo_dev, (i * 4) + 2, hsize * 2, sy,
			       hsize * 3, sy + ysize, 5);
		solo_win_setup(solo_dev, (i * 4) + 3, hsize * 3, sy,
			       solo_dev->video_hsize, sy + ysize, 5);
	}

	return 0;
}

static int solo_v4l2_ch(struct solo_dev *solo_dev, u8 ch, int on)
{
	u8 ext_ch;

	if (ch < solo_dev->nr_chans) {
		solo_win_setup(solo_dev, ch, on ? 0 : solo_dev->video_hsize,
			       on ? 0 : solo_vlines(solo_dev),
			       solo_dev->video_hsize, solo_vlines(solo_dev),
			       on ? 1 : 0);
		return 0;
	}

	if (ch >= solo_dev->nr_chans + solo_dev->nr_ext)
		return -EINVAL;

	ext_ch = ch - solo_dev->nr_chans;

	/* 4up's first */
	if (ext_ch < 4)
		return solo_v4l2_ch_ext_4up(solo_dev, ext_ch, on);

	/* Remaining case is 16up for 16-port */
	return solo_v4l2_ch_ext_16up(solo_dev, on);
}

static int solo_v4l2_set_ch(struct solo_dev *solo_dev, u8 ch)
{
	if (ch >= solo_dev->nr_chans + solo_dev->nr_ext)
		return -EINVAL;

	erase_on(solo_dev);

	solo_v4l2_ch(solo_dev, solo_dev->cur_disp_ch, 0);
	solo_v4l2_ch(solo_dev, ch, 1);

	solo_dev->cur_disp_ch = ch;

	return 0;
}

static void solo_fillbuf(struct solo_dev *solo_dev,
			 struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	dma_addr_t addr;
	unsigned int fdma_addr;
	int error = -1;
	int i;

	addr = vb2_dma_contig_plane_dma_addr(vb, 0);
	if (!addr)
		goto finish_buf;

	if (erase_off(solo_dev)) {
		void *p = vb2_plane_vaddr(vb, 0);
		int image_size = solo_image_size(solo_dev);

		for (i = 0; i < image_size; i += 2) {
			((u8 *)p)[i] = 0x80;
			((u8 *)p)[i + 1] = 0x00;
		}
		error = 0;
	} else {
		fdma_addr = SOLO_DISP_EXT_ADDR + (solo_dev->old_write *
				(SOLO_HW_BPL * solo_vlines(solo_dev)));

		error = solo_p2m_dma_t(solo_dev, 0, addr, fdma_addr,
				       solo_bytesperline(solo_dev),
				       solo_vlines(solo_dev), SOLO_HW_BPL);
	}

finish_buf:
	if (!error) {
		vb2_set_plane_payload(vb, 0,
			solo_vlines(solo_dev) * solo_bytesperline(solo_dev));
		vbuf->sequence = solo_dev->sequence++;
		vb->timestamp = ktime_get_ns();
	}

	vb2_buffer_done(vb, error ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
}

static void solo_thread_try(struct solo_dev *solo_dev)
{
	struct solo_vb2_buf *vb;

	/* Only "break" from this loop if slock is held, otherwise
	 * just return. */
	for (;;) {
		unsigned int cur_write;

		cur_write = SOLO_VI_STATUS0_PAGE(
			solo_reg_read(solo_dev, SOLO_VI_STATUS0));
		if (cur_write == solo_dev->old_write)
			return;

		spin_lock(&solo_dev->slock);

		if (list_empty(&solo_dev->vidq_active))
			break;

		vb = list_first_entry(&solo_dev->vidq_active, struct solo_vb2_buf,
				      list);

		solo_dev->old_write = cur_write;
		list_del(&vb->list);

		spin_unlock(&solo_dev->slock);

		solo_fillbuf(solo_dev, &vb->vb.vb2_buf);
	}

	assert_spin_locked(&solo_dev->slock);
	spin_unlock(&solo_dev->slock);
}

static int solo_thread(void *data)
{
	struct solo_dev *solo_dev = data;
	DECLARE_WAITQUEUE(wait, current);

	set_freezable();
	add_wait_queue(&solo_dev->disp_thread_wait, &wait);

	for (;;) {
		long timeout = schedule_timeout_interruptible(HZ);

		if (timeout == -ERESTARTSYS || kthread_should_stop())
			break;
		solo_thread_try(solo_dev);
		try_to_freeze();
	}

	remove_wait_queue(&solo_dev->disp_thread_wait, &wait);

	return 0;
}

static int solo_start_thread(struct solo_dev *solo_dev)
{
	int ret = 0;

	solo_dev->kthread = kthread_run(solo_thread, solo_dev, SOLO6X10_NAME "_disp");

	if (IS_ERR(solo_dev->kthread)) {
		ret = PTR_ERR(solo_dev->kthread);
		solo_dev->kthread = NULL;
		return ret;
	}
	solo_irq_on(solo_dev, SOLO_IRQ_VIDEO_IN);

	return ret;
}

static void solo_stop_thread(struct solo_dev *solo_dev)
{
	if (!solo_dev->kthread)
		return;

	solo_irq_off(solo_dev, SOLO_IRQ_VIDEO_IN);
	kthread_stop(solo_dev->kthread);
	solo_dev->kthread = NULL;
}

static int solo_queue_setup(struct vb2_queue *q,
			   unsigned int *num_buffers, unsigned int *num_planes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct solo_dev *solo_dev = vb2_get_drv_priv(q);

	sizes[0] = solo_image_size(solo_dev);
	*num_planes = 1;

	if (*num_buffers < MIN_VID_BUFFERS)
		*num_buffers = MIN_VID_BUFFERS;

	return 0;
}

static int solo_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct solo_dev *solo_dev = vb2_get_drv_priv(q);

	solo_dev->sequence = 0;
	return solo_start_thread(solo_dev);
}

static void solo_stop_streaming(struct vb2_queue *q)
{
	struct solo_dev *solo_dev = vb2_get_drv_priv(q);

	solo_stop_thread(solo_dev);

	spin_lock(&solo_dev->slock);
	while (!list_empty(&solo_dev->vidq_active)) {
		struct solo_vb2_buf *buf = list_entry(
				solo_dev->vidq_active.next,
				struct solo_vb2_buf, list);

		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock(&solo_dev->slock);
	INIT_LIST_HEAD(&solo_dev->vidq_active);
}

static void solo_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct solo_dev *solo_dev = vb2_get_drv_priv(vq);
	struct solo_vb2_buf *solo_vb =
		container_of(vbuf, struct solo_vb2_buf, vb);

	spin_lock(&solo_dev->slock);
	list_add_tail(&solo_vb->list, &solo_dev->vidq_active);
	spin_unlock(&solo_dev->slock);
	wake_up_interruptible(&solo_dev->disp_thread_wait);
}

static const struct vb2_ops solo_video_qops = {
	.queue_setup	= solo_queue_setup,
	.buf_queue	= solo_buf_queue,
	.start_streaming = solo_start_streaming,
	.stop_streaming = solo_stop_streaming,
};

static int solo_querycap(struct file *file, void  *priv,
			 struct v4l2_capability *cap)
{
	strscpy(cap->driver, SOLO6X10_NAME, sizeof(cap->driver));
	strscpy(cap->card, "Softlogic 6x10", sizeof(cap->card));
	return 0;
}

static int solo_enum_ext_input(struct solo_dev *solo_dev,
			       struct v4l2_input *input)
{
	int ext = input->index - solo_dev->nr_chans;
	unsigned int nup, first;

	if (ext >= solo_dev->nr_ext)
		return -EINVAL;

	nup   = (ext == 4) ? 16 : 4;
	first = (ext & 3) << 2; /* first channel in the n-up */
	snprintf(input->name, sizeof(input->name),
		 "Multi %d-up (cameras %d-%d)",
		 nup, first + 1, first + nup);
	/* Possible outputs:
	 *  Multi 4-up (cameras 1-4)
	 *  Multi 4-up (cameras 5-8)
	 *  Multi 4-up (cameras 9-12)
	 *  Multi 4-up (cameras 13-16)
	 *  Multi 16-up (cameras 1-16)
	 */
	return 0;
}

static int solo_enum_input(struct file *file, void *priv,
			   struct v4l2_input *input)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	if (input->index >= solo_dev->nr_chans) {
		int ret = solo_enum_ext_input(solo_dev, input);

		if (ret < 0)
			return ret;
	} else {
		snprintf(input->name, sizeof(input->name), "Camera %d",
			 input->index + 1);

		/* We can only check this for normal inputs */
		if (!tw28_get_video_status(solo_dev, input->index))
			input->status = V4L2_IN_ST_NO_SIGNAL;
	}

	input->type = V4L2_INPUT_TYPE_CAMERA;
	input->std = solo_dev->vfd->tvnorms;
	return 0;
}

static int solo_set_input(struct file *file, void *priv, unsigned int index)
{
	struct solo_dev *solo_dev = video_drvdata(file);
	int ret = solo_v4l2_set_ch(solo_dev, index);

	if (!ret) {
		while (erase_off(solo_dev))
			/* Do nothing */;
	}

	return ret;
}

static int solo_get_input(struct file *file, void *priv, unsigned int *index)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	*index = solo_dev->cur_disp_ch;

	return 0;
}

static int solo_enum_fmt_cap(struct file *file, void *priv,
			     struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_UYVY;
	return 0;
}

static int solo_try_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct solo_dev *solo_dev = video_drvdata(file);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int image_size = solo_image_size(solo_dev);

	if (pix->pixelformat != V4L2_PIX_FMT_UYVY)
		return -EINVAL;

	pix->width = solo_dev->video_hsize;
	pix->height = solo_vlines(solo_dev);
	pix->sizeimage = image_size;
	pix->field = V4L2_FIELD_INTERLACED;
	pix->pixelformat = V4L2_PIX_FMT_UYVY;
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	return 0;
}

static int solo_set_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	if (vb2_is_busy(&solo_dev->vidq))
		return -EBUSY;

	/* For right now, if it doesn't match our running config,
	 * then fail */
	return solo_try_fmt_cap(file, priv, f);
}

static int solo_get_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct solo_dev *solo_dev = video_drvdata(file);
	struct v4l2_pix_format *pix = &f->fmt.pix;

	pix->width = solo_dev->video_hsize;
	pix->height = solo_vlines(solo_dev);
	pix->pixelformat = V4L2_PIX_FMT_UYVY;
	pix->field = V4L2_FIELD_INTERLACED;
	pix->sizeimage = solo_image_size(solo_dev);
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pix->bytesperline = solo_bytesperline(solo_dev);

	return 0;
}

static int solo_g_std(struct file *file, void *priv, v4l2_std_id *i)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	if (solo_dev->video_type == SOLO_VO_FMT_TYPE_NTSC)
		*i = V4L2_STD_NTSC_M;
	else
		*i = V4L2_STD_PAL;
	return 0;
}

int solo_set_video_type(struct solo_dev *solo_dev, bool is_50hz)
{
	int i;

	/* Make sure all video nodes are idle */
	if (vb2_is_busy(&solo_dev->vidq))
		return -EBUSY;
	for (i = 0; i < solo_dev->nr_chans; i++)
		if (vb2_is_busy(&solo_dev->v4l2_enc[i]->vidq))
			return -EBUSY;
	solo_dev->video_type = is_50hz ? SOLO_VO_FMT_TYPE_PAL :
					 SOLO_VO_FMT_TYPE_NTSC;
	/* Reconfigure for the new standard */
	solo_disp_init(solo_dev);
	solo_enc_init(solo_dev);
	solo_tw28_init(solo_dev);
	for (i = 0; i < solo_dev->nr_chans; i++)
		solo_update_mode(solo_dev->v4l2_enc[i]);
	return solo_v4l2_set_ch(solo_dev, solo_dev->cur_disp_ch);
}

static int solo_s_std(struct file *file, void *priv, v4l2_std_id std)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	return solo_set_video_type(solo_dev, std & V4L2_STD_625_50);
}

static int solo_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct solo_dev *solo_dev =
		container_of(ctrl->handler, struct solo_dev, disp_hdl);

	switch (ctrl->id) {
	case V4L2_CID_MOTION_TRACE:
		if (ctrl->val) {
			solo_reg_write(solo_dev, SOLO_VI_MOTION_BORDER,
					SOLO_VI_MOTION_Y_ADD |
					SOLO_VI_MOTION_Y_VALUE(0x20) |
					SOLO_VI_MOTION_CB_VALUE(0x10) |
					SOLO_VI_MOTION_CR_VALUE(0x10));
			solo_reg_write(solo_dev, SOLO_VI_MOTION_BAR,
					SOLO_VI_MOTION_CR_ADD |
					SOLO_VI_MOTION_Y_VALUE(0x10) |
					SOLO_VI_MOTION_CB_VALUE(0x80) |
					SOLO_VI_MOTION_CR_VALUE(0x10));
		} else {
			solo_reg_write(solo_dev, SOLO_VI_MOTION_BORDER, 0);
			solo_reg_write(solo_dev, SOLO_VI_MOTION_BAR, 0);
		}
		return 0;
	default:
		break;
	}
	return -EINVAL;
}

static const struct v4l2_file_operations solo_v4l2_fops = {
	.owner			= THIS_MODULE,
	.open			= v4l2_fh_open,
	.release		= vb2_fop_release,
	.read			= vb2_fop_read,
	.poll			= vb2_fop_poll,
	.mmap			= vb2_fop_mmap,
	.unlocked_ioctl		= video_ioctl2,
};

static const struct v4l2_ioctl_ops solo_v4l2_ioctl_ops = {
	.vidioc_querycap		= solo_querycap,
	.vidioc_s_std			= solo_s_std,
	.vidioc_g_std			= solo_g_std,
	/* Input callbacks */
	.vidioc_enum_input		= solo_enum_input,
	.vidioc_s_input			= solo_set_input,
	.vidioc_g_input			= solo_get_input,
	/* Video capture format callbacks */
	.vidioc_enum_fmt_vid_cap	= solo_enum_fmt_cap,
	.vidioc_try_fmt_vid_cap		= solo_try_fmt_cap,
	.vidioc_s_fmt_vid_cap		= solo_set_fmt_cap,
	.vidioc_g_fmt_vid_cap		= solo_get_fmt_cap,
	/* Streaming I/O */
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	/* Logging and events */
	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static const struct video_device solo_v4l2_template = {
	.name			= SOLO6X10_NAME,
	.fops			= &solo_v4l2_fops,
	.ioctl_ops		= &solo_v4l2_ioctl_ops,
	.minor			= -1,
	.release		= video_device_release,
	.tvnorms		= V4L2_STD_NTSC_M | V4L2_STD_PAL,
	.device_caps		= V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
				  V4L2_CAP_STREAMING,
};

static const struct v4l2_ctrl_ops solo_ctrl_ops = {
	.s_ctrl = solo_s_ctrl,
};

static const struct v4l2_ctrl_config solo_motion_trace_ctrl = {
	.ops = &solo_ctrl_ops,
	.id = V4L2_CID_MOTION_TRACE,
	.name = "Motion Detection Trace",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.max = 1,
	.step = 1,
};

int solo_v4l2_init(struct solo_dev *solo_dev, unsigned nr)
{
	int ret;
	int i;

	init_waitqueue_head(&solo_dev->disp_thread_wait);
	spin_lock_init(&solo_dev->slock);
	mutex_init(&solo_dev->lock);
	INIT_LIST_HEAD(&solo_dev->vidq_active);

	solo_dev->vfd = video_device_alloc();
	if (!solo_dev->vfd)
		return -ENOMEM;

	*solo_dev->vfd = solo_v4l2_template;
	solo_dev->vfd->v4l2_dev = &solo_dev->v4l2_dev;
	solo_dev->vfd->queue = &solo_dev->vidq;
	solo_dev->vfd->lock = &solo_dev->lock;
	v4l2_ctrl_handler_init(&solo_dev->disp_hdl, 1);
	v4l2_ctrl_new_custom(&solo_dev->disp_hdl, &solo_motion_trace_ctrl, NULL);
	if (solo_dev->disp_hdl.error) {
		ret = solo_dev->disp_hdl.error;
		goto fail;
	}
	solo_dev->vfd->ctrl_handler = &solo_dev->disp_hdl;

	video_set_drvdata(solo_dev->vfd, solo_dev);

	solo_dev->vidq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	solo_dev->vidq.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	solo_dev->vidq.ops = &solo_video_qops;
	solo_dev->vidq.mem_ops = &vb2_dma_contig_memops;
	solo_dev->vidq.drv_priv = solo_dev;
	solo_dev->vidq.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	solo_dev->vidq.gfp_flags = __GFP_DMA32 | __GFP_KSWAPD_RECLAIM;
	solo_dev->vidq.buf_struct_size = sizeof(struct solo_vb2_buf);
	solo_dev->vidq.lock = &solo_dev->lock;
	solo_dev->vidq.dev = &solo_dev->pdev->dev;
	ret = vb2_queue_init(&solo_dev->vidq);
	if (ret < 0)
		goto fail;

	/* Cycle all the channels and clear */
	for (i = 0; i < solo_dev->nr_chans; i++) {
		solo_v4l2_set_ch(solo_dev, i);
		while (erase_off(solo_dev))
			/* Do nothing */;
	}

	/* Set the default display channel */
	solo_v4l2_set_ch(solo_dev, 0);
	while (erase_off(solo_dev))
		/* Do nothing */;

	ret = video_register_device(solo_dev->vfd, VFL_TYPE_VIDEO, nr);
	if (ret < 0)
		goto fail;

	snprintf(solo_dev->vfd->name, sizeof(solo_dev->vfd->name), "%s (%i)",
		 SOLO6X10_NAME, solo_dev->vfd->num);

	dev_info(&solo_dev->pdev->dev, "Display as /dev/video%d with %d inputs (%d extended)\n",
		 solo_dev->vfd->num,
		 solo_dev->nr_chans, solo_dev->nr_ext);

	return 0;

fail:
	video_device_release(solo_dev->vfd);
	v4l2_ctrl_handler_free(&solo_dev->disp_hdl);
	solo_dev->vfd = NULL;
	return ret;
}

void solo_v4l2_exit(struct solo_dev *solo_dev)
{
	if (solo_dev->vfd == NULL)
		return;

	video_unregister_device(solo_dev->vfd);
	v4l2_ctrl_handler_free(&solo_dev->disp_hdl);
	solo_dev->vfd = NULL;
}
