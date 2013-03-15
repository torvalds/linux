/*
 * Copyright (C) 2010-2013 Bluecherry, LLC <http://www.bluecherrydvr.com>
 *
 * Original author:
 * Ben Collins <bcollins@ubuntu.com>
 *
 * Additional work by:
 * John Brooks <john.brooks@bluecherry.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/videobuf-dma-contig.h>

#include "solo6x10.h"
#include "tw28.h"

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
			 struct videobuf_buffer *vb)
{
	dma_addr_t vbuf;
	unsigned int fdma_addr;
	int error = -1;
	int i;

	vbuf = videobuf_to_dma_contig(vb);
	if (!vbuf)
		goto finish_buf;

	if (erase_off(solo_dev)) {
		void *p = videobuf_queue_to_vaddr(&solo_dev->vidq, vb);
		int image_size = solo_image_size(solo_dev);
		for (i = 0; i < image_size; i += 2) {
			((u8 *)p)[i] = 0x80;
			((u8 *)p)[i + 1] = 0x00;
		}
		error = 0;
	} else {
		fdma_addr = SOLO_DISP_EXT_ADDR + (solo_dev->old_write *
				(SOLO_HW_BPL * solo_vlines(solo_dev)));

		error = solo_p2m_dma_t(solo_dev, 0, vbuf, fdma_addr,
				       solo_bytesperline(solo_dev),
				       solo_vlines(solo_dev), SOLO_HW_BPL);
	}

finish_buf:
	if (error) {
		vb->state = VIDEOBUF_ERROR;
	} else {
		vb->state = VIDEOBUF_DONE;
		vb->field_count++;
	}

	wake_up(&vb->done);
}

static void solo_thread_try(struct solo_dev *solo_dev)
{
	struct videobuf_buffer *vb;

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

		vb = list_first_entry(&solo_dev->vidq_active, struct videobuf_buffer,
				      queue);

		if (!waitqueue_active(&vb->done))
			break;

		solo_dev->old_write = cur_write;
		list_del(&vb->queue);
		vb->state = VIDEOBUF_ACTIVE;

		spin_unlock(&solo_dev->slock);

		solo_fillbuf(solo_dev, vb);
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

	if (atomic_inc_return(&solo_dev->disp_users) == 1)
		solo_irq_on(solo_dev, SOLO_IRQ_VIDEO_IN);

	solo_dev->kthread = kthread_run(solo_thread, solo_dev, SOLO6X10_NAME "_disp");

	if (IS_ERR(solo_dev->kthread)) {
		ret = PTR_ERR(solo_dev->kthread);
		solo_dev->kthread = NULL;
	}

	return ret;
}

static void solo_stop_thread(struct solo_dev *solo_dev)
{
	if (!solo_dev->kthread)
		return;

	kthread_stop(solo_dev->kthread);
	solo_dev->kthread = NULL;

	if (atomic_dec_return(&solo_dev->disp_users) == 0)
		solo_irq_off(solo_dev, SOLO_IRQ_VIDEO_IN);
}

static int solo_buf_setup(struct videobuf_queue *vq, unsigned int *count,
			  unsigned int *size)
{
	struct solo_dev *solo_dev = vq->priv_data;

	*size = solo_image_size(solo_dev);

	if (*count < MIN_VID_BUFFERS)
		*count = MIN_VID_BUFFERS;

	return 0;
}

static int solo_buf_prepare(struct videobuf_queue *vq,
			    struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct solo_dev *solo_dev = vq->priv_data;

	vb->size = solo_image_size(solo_dev);
	if (vb->baddr != 0 && vb->bsize < vb->size)
		return -EINVAL;

	/* XXX: These properties only change when queue is idle */
	vb->width  = solo_dev->video_hsize;
	vb->height = solo_vlines(solo_dev);
	vb->bytesperline = solo_bytesperline(solo_dev);
	vb->field  = field;

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		int rc = videobuf_iolock(vq, vb, NULL);
		if (rc < 0) {
			videobuf_dma_contig_free(vq, vb);
			vb->state = VIDEOBUF_NEEDS_INIT;
			return rc;
		}
	}
	vb->state = VIDEOBUF_PREPARED;

	return 0;
}

static void solo_buf_queue(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct solo_dev *solo_dev = vq->priv_data;

	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &solo_dev->vidq_active);
	wake_up_interruptible(&solo_dev->disp_thread_wait);
}

static void solo_buf_release(struct videobuf_queue *vq,
			     struct videobuf_buffer *vb)
{
	videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static const struct videobuf_queue_ops solo_video_qops = {
	.buf_setup	= solo_buf_setup,
	.buf_prepare	= solo_buf_prepare,
	.buf_queue	= solo_buf_queue,
	.buf_release	= solo_buf_release,
};

static unsigned int solo_v4l2_poll(struct file *file,
				   struct poll_table_struct *wait)
{
	struct solo_dev *solo_dev = video_drvdata(file);
	unsigned long req_events = poll_requested_events(wait);
	unsigned res = v4l2_ctrl_poll(file, wait);

	if (!(req_events & (POLLIN | POLLRDNORM)))
		return res;
	return res | videobuf_poll_stream(file, &solo_dev->vidq, wait);
}

static int solo_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	return videobuf_mmap_mapper(&solo_dev->vidq, vma);
}

static ssize_t solo_v4l2_read(struct file *file, char __user *data,
			      size_t count, loff_t *ppos)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	return videobuf_read_stream(&solo_dev->vidq, data, count, ppos, 0,
				    file->f_flags & O_NONBLOCK);
}

static int solo_v4l2_release(struct file *file)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	solo_stop_thread(solo_dev);
	videobuf_stop(&solo_dev->vidq);
	videobuf_mmap_free(&solo_dev->vidq);
	return v4l2_fh_release(file);
}

static int solo_querycap(struct file *file, void  *priv,
			 struct v4l2_capability *cap)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	strcpy(cap->driver, SOLO6X10_NAME);
	strcpy(cap->card, "Softlogic 6x10");
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s",
		 pci_name(solo_dev->pdev));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int solo_enum_ext_input(struct solo_dev *solo_dev,
			       struct v4l2_input *input)
{
	static const char * const dispnames_1[] = { "4UP" };
	static const char * const dispnames_2[] = { "4UP-1", "4UP-2" };
	static const char * const dispnames_5[] = {
		"4UP-1", "4UP-2", "4UP-3", "4UP-4", "16UP"
	};
	const char * const *dispnames;

	if (input->index >= (solo_dev->nr_chans + solo_dev->nr_ext))
		return -EINVAL;

	if (solo_dev->nr_ext == 5)
		dispnames = dispnames_5;
	else if (solo_dev->nr_ext == 2)
		dispnames = dispnames_2;
	else
		dispnames = dispnames_1;

	snprintf(input->name, sizeof(input->name), "Multi %s",
		 dispnames[input->index - solo_dev->nr_chans]);

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

	if (solo_dev->video_type == SOLO_VO_FMT_TYPE_NTSC)
		input->std = V4L2_STD_NTSC_M;
	else
		input->std = V4L2_STD_PAL_B;

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
	strlcpy(f->description, "UYUV 4:2:2 Packed", sizeof(f->description));

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
	pix->priv = 0;
	return 0;
}

static int solo_set_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	if (videobuf_queue_is_busy(&solo_dev->vidq))
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
	pix->priv = 0;

	return 0;
}

static int solo_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *req)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	return videobuf_reqbufs(&solo_dev->vidq, req);
}

static int solo_querybuf(struct file *file, void *priv,
			 struct v4l2_buffer *buf)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	return videobuf_querybuf(&solo_dev->vidq, buf);
}

static int solo_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	return videobuf_qbuf(&solo_dev->vidq, buf);
}

static int solo_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	return videobuf_dqbuf(&solo_dev->vidq, buf, file->f_flags & O_NONBLOCK);
}

static int solo_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct solo_dev *solo_dev = video_drvdata(file);
	int ret;

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	ret = solo_start_thread(solo_dev);
	if (ret)
		return ret;

	return videobuf_streamon(&solo_dev->vidq);
}

static int solo_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct solo_dev *solo_dev = video_drvdata(file);

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return videobuf_streamoff(&solo_dev->vidq);
}

static int solo_s_std(struct file *file, void *priv, v4l2_std_id i)
{
	return 0;
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
	.release		= solo_v4l2_release,
	.read			= solo_v4l2_read,
	.poll			= solo_v4l2_poll,
	.mmap			= solo_v4l2_mmap,
	.ioctl			= video_ioctl2,
};

static const struct v4l2_ioctl_ops solo_v4l2_ioctl_ops = {
	.vidioc_querycap		= solo_querycap,
	.vidioc_s_std			= solo_s_std,
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
	.vidioc_reqbufs			= solo_reqbufs,
	.vidioc_querybuf		= solo_querybuf,
	.vidioc_qbuf			= solo_qbuf,
	.vidioc_dqbuf			= solo_dqbuf,
	.vidioc_streamon		= solo_streamon,
	.vidioc_streamoff		= solo_streamoff,
	/* Logging and events */
	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static struct video_device solo_v4l2_template = {
	.name			= SOLO6X10_NAME,
	.fops			= &solo_v4l2_fops,
	.ioctl_ops		= &solo_v4l2_ioctl_ops,
	.minor			= -1,
	.release		= video_device_release,

	.tvnorms		= V4L2_STD_NTSC_M | V4L2_STD_PAL_B,
	.current_norm		= V4L2_STD_NTSC_M,
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

	atomic_set(&solo_dev->disp_users, 0);
	init_waitqueue_head(&solo_dev->disp_thread_wait);

	solo_dev->vfd = video_device_alloc();
	if (!solo_dev->vfd)
		return -ENOMEM;

	*solo_dev->vfd = solo_v4l2_template;
	solo_dev->vfd->v4l2_dev = &solo_dev->v4l2_dev;
	v4l2_ctrl_handler_init(&solo_dev->disp_hdl, 1);
	v4l2_ctrl_new_custom(&solo_dev->disp_hdl, &solo_motion_trace_ctrl, NULL);
	if (solo_dev->disp_hdl.error)
		return solo_dev->disp_hdl.error;
	solo_dev->vfd->ctrl_handler = &solo_dev->disp_hdl;
	set_bit(V4L2_FL_USE_FH_PRIO, &solo_dev->vfd->flags);

	video_set_drvdata(solo_dev->vfd, solo_dev);

	spin_lock_init(&solo_dev->slock);
	INIT_LIST_HEAD(&solo_dev->vidq_active);

	videobuf_queue_dma_contig_init(&solo_dev->vidq, &solo_video_qops,
				       &solo_dev->pdev->dev, &solo_dev->slock,
				       V4L2_BUF_TYPE_VIDEO_CAPTURE,
				       V4L2_FIELD_INTERLACED,
				       sizeof(struct videobuf_buffer),
				       solo_dev, NULL);

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

	ret = video_register_device(solo_dev->vfd, VFL_TYPE_GRABBER, nr);
	if (ret < 0) {
		video_device_release(solo_dev->vfd);
		solo_dev->vfd = NULL;
		return ret;
	}

	snprintf(solo_dev->vfd->name, sizeof(solo_dev->vfd->name), "%s (%i)",
		 SOLO6X10_NAME, solo_dev->vfd->num);

	dev_info(&solo_dev->pdev->dev, "Display as /dev/video%d with "
		 "%d inputs (%d extended)\n", solo_dev->vfd->num,
		 solo_dev->nr_chans, solo_dev->nr_ext);

	return 0;
}

void solo_v4l2_exit(struct solo_dev *solo_dev)
{
	if (solo_dev->vfd == NULL)
		return;

	video_unregister_device(solo_dev->vfd);
	v4l2_ctrl_handler_free(&solo_dev->disp_hdl);
	solo_dev->vfd = NULL;
}
