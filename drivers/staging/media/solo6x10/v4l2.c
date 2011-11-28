/*
 * Copyright (C) 2010 Bluecherry, LLC www.bluecherrydvr.com
 * Copyright (C) 2010 Ben Collins <bcollins@bluecherry.net>
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
#include <media/videobuf-dma-sg.h>
#include "solo6x10.h"
#include "tw28.h"

#define SOLO_HW_BPL		2048
#define SOLO_DISP_PIX_FIELD	V4L2_FIELD_INTERLACED

/* Image size is two fields, SOLO_HW_BPL is one horizontal line */
#define solo_vlines(__solo)	(__solo->video_vsize * 2)
#define solo_image_size(__solo) (solo_bytesperline(__solo) * \
				 solo_vlines(__solo))
#define solo_bytesperline(__solo) (__solo->video_hsize * 2)

#define MIN_VID_BUFFERS		4

/* Simple file handle */
struct solo_filehandle {
	struct solo_dev		*solo_dev;
	struct videobuf_queue	vidq;
	struct task_struct      *kthread;
	spinlock_t		slock;
	int			old_write;
	struct list_head	vidq_active;
	struct p2m_desc		desc[SOLO_NR_P2M_DESC];
	int			desc_idx;
};

unsigned video_nr = -1;
module_param(video_nr, uint, 0644);
MODULE_PARM_DESC(video_nr, "videoX start number, -1 is autodetect (default)");

static void erase_on(struct solo_dev *solo_dev)
{
	solo_reg_write(solo_dev, SOLO_VO_DISP_ERASE, SOLO_VO_DISP_ERASE_ON);
	solo_dev->erasing = 1;
	solo_dev->frame_blank = 0;
}

static int erase_off(struct solo_dev *solo_dev)
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
	solo_reg_write(solo_dev, SOLO_IRQ_STAT, SOLO_IRQ_VIDEO_IN);
	wake_up_interruptible(&solo_dev->disp_thread_wait);
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

static void disp_reset_desc(struct solo_filehandle *fh)
{
	/* We use desc mode, which ignores desc 0 */
	memset(fh->desc, 0, sizeof(*fh->desc));
	fh->desc_idx = 1;
}

static int disp_flush_descs(struct solo_filehandle *fh)
{
	int ret;

	if (!fh->desc_idx)
		return 0;

	ret = solo_p2m_dma_desc(fh->solo_dev, SOLO_P2M_DMA_ID_DISP,
				fh->desc, fh->desc_idx);
	disp_reset_desc(fh);

	return ret;
}

static int disp_push_desc(struct solo_filehandle *fh, dma_addr_t dma_addr,
		      u32 ext_addr, int size, int repeat, int ext_size)
{
	if (fh->desc_idx >= SOLO_NR_P2M_DESC) {
		int ret = disp_flush_descs(fh);
		if (ret)
			return ret;
	}

	solo_p2m_push_desc(&fh->desc[fh->desc_idx], 0, dma_addr, ext_addr,
			   size, repeat, ext_size);
	fh->desc_idx++;

	return 0;
}

static void solo_fillbuf(struct solo_filehandle *fh,
			 struct videobuf_buffer *vb)
{
	struct solo_dev *solo_dev = fh->solo_dev;
	struct videobuf_dmabuf *vbuf;
	unsigned int fdma_addr;
	int error = 1;
	int i;
	struct scatterlist *sg;
	dma_addr_t sg_dma;
	int sg_size_left;

	vbuf = videobuf_to_dma(vb);
	if (!vbuf)
		goto finish_buf;

	if (erase_off(solo_dev)) {
		int i;

		/* Just blit to the entire sg list, ignoring size */
		for_each_sg(vbuf->sglist, sg, vbuf->sglen, i) {
			void *p = sg_virt(sg);
			size_t len = sg_dma_len(sg);

			for (i = 0; i < len; i += 2) {
				((u8 *)p)[i] = 0x80;
				((u8 *)p)[i + 1] = 0x00;
			}
		}

		error = 0;
		goto finish_buf;
	}

	disp_reset_desc(fh);
	sg = vbuf->sglist;
	sg_dma = sg_dma_address(sg);
	sg_size_left = sg_dma_len(sg);

	fdma_addr = SOLO_DISP_EXT_ADDR + (fh->old_write *
			(SOLO_HW_BPL * solo_vlines(solo_dev)));

	for (i = 0; i < solo_vlines(solo_dev); i++) {
		int line_len = solo_bytesperline(solo_dev);
		int lines;

		if (!sg_size_left) {
			sg = sg_next(sg);
			if (sg == NULL)
				goto finish_buf;
			sg_dma = sg_dma_address(sg);
			sg_size_left = sg_dma_len(sg);
		}

		/* No room for an entire line, so chunk it up */
		if (sg_size_left < line_len) {
			int this_addr = fdma_addr;

			while (line_len > 0) {
				int this_write;

				if (!sg_size_left) {
					sg = sg_next(sg);
					if (sg == NULL)
						goto finish_buf;
					sg_dma = sg_dma_address(sg);
					sg_size_left = sg_dma_len(sg);
				}

				this_write = min(sg_size_left, line_len);

				if (disp_push_desc(fh, sg_dma, this_addr,
						   this_write, 0, 0))
					goto finish_buf;

				line_len -= this_write;
				sg_size_left -= this_write;
				sg_dma += this_write;
				this_addr += this_write;
			}

			fdma_addr += SOLO_HW_BPL;
			continue;
		}

		/* Shove as many lines into a repeating descriptor as possible */
		lines = min(sg_size_left / line_len,
			    solo_vlines(solo_dev) - i);

		if (disp_push_desc(fh, sg_dma, fdma_addr, line_len,
				   lines - 1, SOLO_HW_BPL))
			goto finish_buf;

		i += lines - 1;
		fdma_addr += SOLO_HW_BPL * lines;
		sg_dma += lines * line_len;
		sg_size_left -= lines * line_len;
	}

	error = disp_flush_descs(fh);

finish_buf:
	if (error) {
		vb->state = VIDEOBUF_ERROR;
	} else {
		vb->size = solo_vlines(solo_dev) * solo_bytesperline(solo_dev);
		vb->state = VIDEOBUF_DONE;
		vb->field_count++;
		do_gettimeofday(&vb->ts);
	}

	wake_up(&vb->done);

	return;
}

static void solo_thread_try(struct solo_filehandle *fh)
{
	struct videobuf_buffer *vb;
	unsigned int cur_write;

	for (;;) {
		spin_lock(&fh->slock);

		if (list_empty(&fh->vidq_active))
			break;

		vb = list_first_entry(&fh->vidq_active, struct videobuf_buffer,
				      queue);

		if (!waitqueue_active(&vb->done))
			break;

		cur_write = SOLO_VI_STATUS0_PAGE(solo_reg_read(fh->solo_dev,
						 SOLO_VI_STATUS0));
		if (cur_write == fh->old_write)
			break;

		fh->old_write = cur_write;
		list_del(&vb->queue);

		spin_unlock(&fh->slock);

		solo_fillbuf(fh, vb);
	}

	assert_spin_locked(&fh->slock);
	spin_unlock(&fh->slock);
}

static int solo_thread(void *data)
{
	struct solo_filehandle *fh = data;
	struct solo_dev *solo_dev = fh->solo_dev;
	DECLARE_WAITQUEUE(wait, current);

	set_freezable();
	add_wait_queue(&solo_dev->disp_thread_wait, &wait);

	for (;;) {
		long timeout = schedule_timeout_interruptible(HZ);
		if (timeout == -ERESTARTSYS || kthread_should_stop())
			break;
		solo_thread_try(fh);
		try_to_freeze();
	}

	remove_wait_queue(&solo_dev->disp_thread_wait, &wait);

	return 0;
}

static int solo_start_thread(struct solo_filehandle *fh)
{
	fh->kthread = kthread_run(solo_thread, fh, SOLO6X10_NAME "_disp");

	if (IS_ERR(fh->kthread))
		return PTR_ERR(fh->kthread);

	return 0;
}

static void solo_stop_thread(struct solo_filehandle *fh)
{
	if (fh->kthread) {
		kthread_stop(fh->kthread);
		fh->kthread = NULL;
	}
}

static int solo_buf_setup(struct videobuf_queue *vq, unsigned int *count,
			  unsigned int *size)
{
	struct solo_filehandle *fh = vq->priv_data;
	struct solo_dev *solo_dev  = fh->solo_dev;

	*size = solo_image_size(solo_dev);

	if (*count < MIN_VID_BUFFERS)
		*count = MIN_VID_BUFFERS;

	return 0;
}

static int solo_buf_prepare(struct videobuf_queue *vq,
			    struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct solo_filehandle *fh  = vq->priv_data;
	struct solo_dev *solo_dev = fh->solo_dev;

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
			struct videobuf_dmabuf *dma = videobuf_to_dma(vb);
			videobuf_dma_unmap(vq->dev, dma);
			videobuf_dma_free(dma);
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
	struct solo_filehandle *fh = vq->priv_data;
	struct solo_dev *solo_dev = fh->solo_dev;

	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &fh->vidq_active);
	wake_up_interruptible(&solo_dev->disp_thread_wait);
}

static void solo_buf_release(struct videobuf_queue *vq,
			     struct videobuf_buffer *vb)
{
	struct videobuf_dmabuf *dma = videobuf_to_dma(vb);

	videobuf_dma_unmap(vq->dev, dma);
	videobuf_dma_free(dma);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static struct videobuf_queue_ops solo_video_qops = {
	.buf_setup	= solo_buf_setup,
	.buf_prepare	= solo_buf_prepare,
	.buf_queue	= solo_buf_queue,
	.buf_release	= solo_buf_release,
};

static unsigned int solo_v4l2_poll(struct file *file,
				   struct poll_table_struct *wait)
{
	struct solo_filehandle *fh = file->private_data;

	return videobuf_poll_stream(file, &fh->vidq, wait);
}

static int solo_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct solo_filehandle *fh = file->private_data;

	return videobuf_mmap_mapper(&fh->vidq, vma);
}

static int solo_v4l2_open(struct file *file)
{
	struct solo_dev *solo_dev = video_drvdata(file);
	struct solo_filehandle *fh;
	int ret;

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (fh == NULL)
		return -ENOMEM;

	spin_lock_init(&fh->slock);
	INIT_LIST_HEAD(&fh->vidq_active);
	fh->solo_dev = solo_dev;
	file->private_data = fh;

	ret = solo_start_thread(fh);
	if (ret) {
		kfree(fh);
		return ret;
	}

	videobuf_queue_sg_init(&fh->vidq, &solo_video_qops,
			       &solo_dev->pdev->dev, &fh->slock,
			       V4L2_BUF_TYPE_VIDEO_CAPTURE,
			       SOLO_DISP_PIX_FIELD,
			       sizeof(struct videobuf_buffer), fh, NULL);

	return 0;
}

static ssize_t solo_v4l2_read(struct file *file, char __user *data,
			      size_t count, loff_t *ppos)
{
	struct solo_filehandle *fh = file->private_data;

	return videobuf_read_stream(&fh->vidq, data, count, ppos, 0,
				    file->f_flags & O_NONBLOCK);
}

static int solo_v4l2_release(struct file *file)
{
	struct solo_filehandle *fh = file->private_data;

	videobuf_stop(&fh->vidq);
	videobuf_mmap_free(&fh->vidq);
	solo_stop_thread(fh);
	kfree(fh);

	return 0;
}

static int solo_querycap(struct file *file, void  *priv,
			 struct v4l2_capability *cap)
{
	struct solo_filehandle  *fh  = priv;
	struct solo_dev *solo_dev = fh->solo_dev;

	strcpy(cap->driver, SOLO6X10_NAME);
	strcpy(cap->card, "Softlogic 6x10");
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI %s",
		 pci_name(solo_dev->pdev));
	cap->version = SOLO6X10_VER_NUM;
	cap->capabilities =     V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_READWRITE |
				V4L2_CAP_STREAMING;
	return 0;
}

static int solo_enum_ext_input(struct solo_dev *solo_dev,
			       struct v4l2_input *input)
{
	static const char *dispnames_1[] = { "4UP" };
	static const char *dispnames_2[] = { "4UP-1", "4UP-2" };
	static const char *dispnames_5[] = {
		"4UP-1", "4UP-2", "4UP-3", "4UP-4", "16UP"
	};
	const char **dispnames;

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
	struct solo_filehandle *fh  = priv;
	struct solo_dev *solo_dev = fh->solo_dev;

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
	struct solo_filehandle *fh = priv;

	return solo_v4l2_set_ch(fh->solo_dev, index);
}

static int solo_get_input(struct file *file, void *priv, unsigned int *index)
{
	struct solo_filehandle *fh = priv;

	*index = fh->solo_dev->cur_disp_ch;

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
	struct solo_filehandle *fh = priv;
	struct solo_dev *solo_dev = fh->solo_dev;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int image_size = solo_image_size(solo_dev);

	/* Check supported sizes */
	if (pix->width != solo_dev->video_hsize)
		pix->width = solo_dev->video_hsize;
	if (pix->height != solo_vlines(solo_dev))
		pix->height = solo_vlines(solo_dev);
	if (pix->sizeimage != image_size)
		pix->sizeimage = image_size;

	/* Check formats */
	if (pix->field == V4L2_FIELD_ANY)
		pix->field = SOLO_DISP_PIX_FIELD;

	if (pix->pixelformat != V4L2_PIX_FMT_UYVY ||
	    pix->field       != SOLO_DISP_PIX_FIELD ||
	    pix->colorspace  != V4L2_COLORSPACE_SMPTE170M)
		return -EINVAL;

	return 0;
}

static int solo_set_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct solo_filehandle *fh = priv;

	if (videobuf_queue_is_busy(&fh->vidq))
		return -EBUSY;

	/* For right now, if it doesn't match our running config,
	 * then fail */
	return solo_try_fmt_cap(file, priv, f);
}

static int solo_get_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct solo_filehandle *fh = priv;
	struct solo_dev *solo_dev = fh->solo_dev;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	pix->width = solo_dev->video_hsize;
	pix->height = solo_vlines(solo_dev);
	pix->pixelformat = V4L2_PIX_FMT_UYVY;
	pix->field = SOLO_DISP_PIX_FIELD;
	pix->sizeimage = solo_image_size(solo_dev);
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pix->bytesperline = solo_bytesperline(solo_dev);

	return 0;
}

static int solo_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *req)
{
	struct solo_filehandle *fh = priv;

	return videobuf_reqbufs(&fh->vidq, req);
}

static int solo_querybuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct solo_filehandle *fh = priv;

	return videobuf_querybuf(&fh->vidq, buf);
}

static int solo_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct solo_filehandle *fh = priv;

	return videobuf_qbuf(&fh->vidq, buf);
}

static int solo_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct solo_filehandle *fh = priv;

	return videobuf_dqbuf(&fh->vidq, buf, file->f_flags & O_NONBLOCK);
}

static int solo_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct solo_filehandle *fh = priv;

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return videobuf_streamon(&fh->vidq);
}

static int solo_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct solo_filehandle *fh = priv;

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return videobuf_streamoff(&fh->vidq);
}

static int solo_s_std(struct file *file, void *priv, v4l2_std_id *i)
{
	return 0;
}

static const u32 solo_motion_ctrls[] = {
	V4L2_CID_MOTION_TRACE,
	0
};

static const u32 *solo_ctrl_classes[] = {
	solo_motion_ctrls,
	NULL
};

static int solo_disp_queryctrl(struct file *file, void *priv,
			       struct v4l2_queryctrl *qc)
{
	qc->id = v4l2_ctrl_next(solo_ctrl_classes, qc->id);
	if (!qc->id)
		return -EINVAL;

	switch (qc->id) {
#ifdef PRIVATE_CIDS
	case V4L2_CID_MOTION_TRACE:
		qc->type = V4L2_CTRL_TYPE_BOOLEAN;
		qc->minimum = 0;
		qc->maximum = qc->step = 1;
		qc->default_value = 0;
		strlcpy(qc->name, "Motion Detection Trace", sizeof(qc->name));
		return 0;
#else
	case V4L2_CID_MOTION_TRACE:
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 0);
#endif
	}
	return -EINVAL;
}

static int solo_disp_g_ctrl(struct file *file, void *priv,
			    struct v4l2_control *ctrl)
{
	struct solo_filehandle *fh = priv;
	struct solo_dev *solo_dev = fh->solo_dev;

	switch (ctrl->id) {
	case V4L2_CID_MOTION_TRACE:
		ctrl->value = solo_reg_read(solo_dev, SOLO_VI_MOTION_BAR)
			? 1 : 0;
		return 0;
	}
	return -EINVAL;
}

static int solo_disp_s_ctrl(struct file *file, void *priv,
			    struct v4l2_control *ctrl)
{
	struct solo_filehandle *fh = priv;
	struct solo_dev *solo_dev = fh->solo_dev;

	switch (ctrl->id) {
	case V4L2_CID_MOTION_TRACE:
		if (ctrl->value) {
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
	}
	return -EINVAL;
}

static const struct v4l2_file_operations solo_v4l2_fops = {
	.owner			= THIS_MODULE,
	.open			= solo_v4l2_open,
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
	/* Controls */
	.vidioc_queryctrl		= solo_disp_queryctrl,
	.vidioc_g_ctrl			= solo_disp_g_ctrl,
	.vidioc_s_ctrl			= solo_disp_s_ctrl,
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

int solo_v4l2_init(struct solo_dev *solo_dev)
{
	int ret;
	int i;

	init_waitqueue_head(&solo_dev->disp_thread_wait);

	solo_dev->vfd = video_device_alloc();
	if (!solo_dev->vfd)
		return -ENOMEM;

	*solo_dev->vfd = solo_v4l2_template;
	solo_dev->vfd->parent = &solo_dev->pdev->dev;

	ret = video_register_device(solo_dev->vfd, VFL_TYPE_GRABBER, video_nr);
	if (ret < 0) {
		video_device_release(solo_dev->vfd);
		solo_dev->vfd = NULL;
		return ret;
	}

	video_set_drvdata(solo_dev->vfd, solo_dev);

	snprintf(solo_dev->vfd->name, sizeof(solo_dev->vfd->name), "%s (%i)",
		 SOLO6X10_NAME, solo_dev->vfd->num);

	if (video_nr != -1)
		video_nr++;

	dev_info(&solo_dev->pdev->dev, "Display as /dev/video%d with "
		 "%d inputs (%d extended)\n", solo_dev->vfd->num,
		 solo_dev->nr_chans, solo_dev->nr_ext);

	/* Cycle all the channels and clear */
	for (i = 0; i < solo_dev->nr_chans; i++) {
		solo_v4l2_set_ch(solo_dev, i);
		while (erase_off(solo_dev))
			;/* Do nothing */
	}

	/* Set the default display channel */
	solo_v4l2_set_ch(solo_dev, 0);
	while (erase_off(solo_dev))
		;/* Do nothing */

	solo_irq_on(solo_dev, SOLO_IRQ_VIDEO_IN);

	return 0;
}

void solo_v4l2_exit(struct solo_dev *solo_dev)
{
	solo_irq_off(solo_dev, SOLO_IRQ_VIDEO_IN);
	if (solo_dev->vfd) {
		video_unregister_device(solo_dev->vfd);
		solo_dev->vfd = NULL;
	}
}
