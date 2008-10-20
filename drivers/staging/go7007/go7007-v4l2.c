/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <linux/i2c.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <asm/system.h>

#include "go7007.h"
#include "go7007-priv.h"
#include "wis-i2c.h"

static void deactivate_buffer(struct go7007_buffer *gobuf)
{
	int i;

	if (gobuf->state != BUF_STATE_IDLE) {
		list_del(&gobuf->stream);
		gobuf->state = BUF_STATE_IDLE;
	}
	if (gobuf->page_count > 0) {
		for (i = 0; i < gobuf->page_count; ++i)
			page_cache_release(gobuf->pages[i]);
		gobuf->page_count = 0;
	}
}

static void abort_queued(struct go7007 *go)
{
	struct go7007_buffer *gobuf, *next;

	list_for_each_entry_safe(gobuf, next, &go->stream, stream) {
		deactivate_buffer(gobuf);
	}
}

static int go7007_streamoff(struct go7007 *go)
{
	int retval = -EINVAL;
	unsigned long flags;

	down(&go->hw_lock);
	if (go->streaming) {
		go->streaming = 0;
		go7007_stream_stop(go);
		spin_lock_irqsave(&go->spinlock, flags);
		abort_queued(go);
		spin_unlock_irqrestore(&go->spinlock, flags);
		go7007_reset_encoder(go);
		retval = 0;
	}
	up(&go->hw_lock);
	return 0;
}

static int go7007_open(struct inode *inode, struct file *file)
{
	struct go7007 *go = video_get_drvdata(video_devdata(file));
	struct go7007_file *gofh;

	if (go->status != STATUS_ONLINE)
		return -EBUSY;
	gofh = kmalloc(sizeof(struct go7007_file), GFP_KERNEL);
	if (gofh == NULL)
		return -ENOMEM;
	++go->ref_count;
	gofh->go = go;
	init_MUTEX(&gofh->lock);
	gofh->buf_count = 0;
	file->private_data = gofh;
	return 0;
}

static int go7007_release(struct inode *inode, struct file *file)
{
	struct go7007_file *gofh = file->private_data;
	struct go7007 *go = gofh->go;

	if (gofh->buf_count > 0) {
		go7007_streamoff(go);
		go->in_use = 0;
		kfree(gofh->bufs);
		gofh->buf_count = 0;
	}
	kfree(gofh);
	if (--go->ref_count == 0)
		kfree(go);
	file->private_data = NULL;
	return 0;
}

static u32 get_frame_type_flag(struct go7007_buffer *gobuf, int format)
{
	u8 *f = page_address(gobuf->pages[0]);

	switch (format) {
	case GO7007_FORMAT_MJPEG:
		return V4L2_BUF_FLAG_KEYFRAME;
	case GO7007_FORMAT_MPEG4:
		switch ((f[gobuf->frame_offset + 4] >> 6) & 0x3) {
		case 0:
			return V4L2_BUF_FLAG_KEYFRAME;
		case 1:
			return V4L2_BUF_FLAG_PFRAME;
		case 2:
			return V4L2_BUF_FLAG_BFRAME;
		default:
			return 0;
		}
	case GO7007_FORMAT_MPEG1:
	case GO7007_FORMAT_MPEG2:
		switch ((f[gobuf->frame_offset + 5] >> 3) & 0x7) {
		case 1:
			return V4L2_BUF_FLAG_KEYFRAME;
		case 2:
			return V4L2_BUF_FLAG_PFRAME;
		case 3:
			return V4L2_BUF_FLAG_BFRAME;
		default:
			return 0;
		}
	}

	return 0;
}

static int set_capture_size(struct go7007 *go, struct v4l2_format *fmt, int try)
{
	int sensor_height = 0, sensor_width = 0;
	int width, height, i;

	if (fmt != NULL && fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG &&
			fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_MPEG &&
			fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_MPEG4)
		return -EINVAL;

	switch (go->standard) {
	case GO7007_STD_NTSC:
		sensor_width = 720;
		sensor_height = 480;
		break;
	case GO7007_STD_PAL:
		sensor_width = 720;
		sensor_height = 576;
		break;
	case GO7007_STD_OTHER:
		sensor_width = go->board_info->sensor_width;
		sensor_height = go->board_info->sensor_height;
		break;
	}

	if (fmt == NULL) {
		width = sensor_width;
		height = sensor_height;
	} else if (go->board_info->sensor_flags & GO7007_SENSOR_SCALING) {
		if (fmt->fmt.pix.width > sensor_width)
			width = sensor_width;
		else if (fmt->fmt.pix.width < 144)
			width = 144;
		else
			width = fmt->fmt.pix.width & ~0x0f;

		if (fmt->fmt.pix.height > sensor_height)
			height = sensor_height;
		else if (fmt->fmt.pix.height < 96)
			height = 96;
		else
			height = fmt->fmt.pix.height & ~0x0f;
	} else {
		int requested_size = fmt->fmt.pix.width * fmt->fmt.pix.height;
		int sensor_size = sensor_width * sensor_height;

		if (64 * requested_size < 9 * sensor_size) {
			width = sensor_width / 4;
			height = sensor_height / 4;
		} else if (64 * requested_size < 36 * sensor_size) {
			width = sensor_width / 2;
			height = sensor_height / 2;
		} else {
			width = sensor_width;
			height = sensor_height;
		}
		width &= ~0xf;
		height &= ~0xf;
	}

	if (fmt != NULL) {
		u32 pixelformat = fmt->fmt.pix.pixelformat;

		memset(fmt, 0, sizeof(*fmt));
		fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmt->fmt.pix.width = width;
		fmt->fmt.pix.height = height;
		fmt->fmt.pix.pixelformat = pixelformat;
		fmt->fmt.pix.field = V4L2_FIELD_NONE;
		fmt->fmt.pix.bytesperline = 0;
		fmt->fmt.pix.sizeimage = GO7007_BUF_SIZE;
		fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M; /* ?? */
	}

	if (try)
		return 0;

	go->width = width;
	go->height = height;
	go->encoder_h_offset = go->board_info->sensor_h_offset;
	go->encoder_v_offset = go->board_info->sensor_v_offset;
	for (i = 0; i < 4; ++i)
		go->modet[i].enable = 0;
	for (i = 0; i < 1624; ++i)
		go->modet_map[i] = 0;

	if (go->board_info->sensor_flags & GO7007_SENSOR_SCALING) {
		struct video_decoder_resolution res;

		res.width = width;
		if (height > sensor_height / 2) {
			res.height = height / 2;
			go->encoder_v_halve = 0;
		} else {
			res.height = height;
			go->encoder_v_halve = 1;
		}
		if (go->i2c_adapter_online)
			i2c_clients_command(&go->i2c_adapter,
					DECODER_SET_RESOLUTION, &res);
	} else {
		if (width <= sensor_width / 4) {
			go->encoder_h_halve = 1;
			go->encoder_v_halve = 1;
			go->encoder_subsample = 1;
		} else if (width <= sensor_width / 2) {
			go->encoder_h_halve = 1;
			go->encoder_v_halve = 1;
			go->encoder_subsample = 0;
		} else {
			go->encoder_h_halve = 0;
			go->encoder_v_halve = 0;
			go->encoder_subsample = 0;
		}
	}

	if (fmt == NULL)
		return 0;

	switch (fmt->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_MPEG:
		if (go->format == GO7007_FORMAT_MPEG1 ||
				go->format == GO7007_FORMAT_MPEG2 ||
				go->format == GO7007_FORMAT_MPEG4)
			break;
		go->format = GO7007_FORMAT_MPEG1;
		go->pali = 0;
		go->aspect_ratio = GO7007_RATIO_1_1;
		go->gop_size = go->sensor_framerate / 1000;
		go->ipb = 0;
		go->closed_gop = 1;
		go->repeat_seqhead = 1;
		go->seq_header_enable = 1;
		go->gop_header_enable = 1;
		go->dvd_mode = 0;
		break;
	/* Backwards compatibility only! */
	case V4L2_PIX_FMT_MPEG4:
		if (go->format == GO7007_FORMAT_MPEG4)
			break;
		go->format = GO7007_FORMAT_MPEG4;
		go->pali = 0xf5;
		go->aspect_ratio = GO7007_RATIO_1_1;
		go->gop_size = go->sensor_framerate / 1000;
		go->ipb = 0;
		go->closed_gop = 1;
		go->repeat_seqhead = 1;
		go->seq_header_enable = 1;
		go->gop_header_enable = 1;
		go->dvd_mode = 0;
		break;
	case V4L2_PIX_FMT_MJPEG:
		go->format = GO7007_FORMAT_MJPEG;
		go->pali = 0;
		go->aspect_ratio = GO7007_RATIO_1_1;
		go->gop_size = 0;
		go->ipb = 0;
		go->closed_gop = 0;
		go->repeat_seqhead = 0;
		go->seq_header_enable = 0;
		go->gop_header_enable = 0;
		go->dvd_mode = 0;
		break;
	}
	return 0;
}

static int clip_to_modet_map(struct go7007 *go, int region,
		struct v4l2_clip *clip_list)
{
	struct v4l2_clip clip, *clip_ptr;
	int x, y, mbnum;

	/* Check if coordinates are OK and if any macroblocks are already
	 * used by other regions (besides 0) */
	clip_ptr = clip_list;
	while (clip_ptr) {
		if (copy_from_user(&clip, clip_ptr, sizeof(clip)))
			return -EFAULT;
		if (clip.c.left < 0 || (clip.c.left & 0xF) ||
				clip.c.width <= 0 || (clip.c.width & 0xF))
			return -EINVAL;
		if (clip.c.left + clip.c.width > go->width)
			return -EINVAL;
		if (clip.c.top < 0 || (clip.c.top & 0xF) ||
				clip.c.height <= 0 || (clip.c.height & 0xF))
			return -EINVAL;
		if (clip.c.top + clip.c.height > go->height)
			return -EINVAL;
		for (y = 0; y < clip.c.height; y += 16)
			for (x = 0; x < clip.c.width; x += 16) {
				mbnum = (go->width >> 4) *
						((clip.c.top + y) >> 4) +
					((clip.c.left + x) >> 4);
				if (go->modet_map[mbnum] != 0 &&
						go->modet_map[mbnum] != region)
					return -EBUSY;
			}
		clip_ptr = clip.next;
	}

	/* Clear old region macroblocks */
	for (mbnum = 0; mbnum < 1624; ++mbnum)
		if (go->modet_map[mbnum] == region)
			go->modet_map[mbnum] = 0;

	/* Claim macroblocks in this list */
	clip_ptr = clip_list;
	while (clip_ptr) {
		if (copy_from_user(&clip, clip_ptr, sizeof(clip)))
			return -EFAULT;
		for (y = 0; y < clip.c.height; y += 16)
			for (x = 0; x < clip.c.width; x += 16) {
				mbnum = (go->width >> 4) *
						((clip.c.top + y) >> 4) +
					((clip.c.left + x) >> 4);
				go->modet_map[mbnum] = region;
			}
		clip_ptr = clip.next;
	}
	return 0;
}

static int go7007_do_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, void *arg)
{
	struct go7007_file *gofh = file->private_data;
	struct go7007 *go = gofh->go;
	unsigned long flags;
	int retval = 0;

	switch (cmd) {
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		memset(cap, 0, sizeof(*cap));
		strcpy(cap->driver, "go7007");
		strncpy(cap->card, go->name, sizeof(cap->card));
		cap->version = KERNEL_VERSION(0, 9, 8);
		cap->capabilities = V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_STREAMING; /* | V4L2_CAP_AUDIO; */
		if (go->board_info->flags & GO7007_BOARD_HAS_TUNER)
			cap->capabilities |= V4L2_CAP_TUNER;
		return 0;
	}
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *fmt = arg;
		unsigned int index;
		char *desc;
		u32 pixelformat;

		if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		switch (fmt->index) {
		case 0:
			pixelformat = V4L2_PIX_FMT_MJPEG;
			desc = "Motion-JPEG";
			break;
		case 1:
			pixelformat = V4L2_PIX_FMT_MPEG;
			desc = "MPEG1/MPEG2/MPEG4";
			break;
		default:
			return -EINVAL;
		}
		index = fmt->index;
		memset(fmt, 0, sizeof(*fmt));
		fmt->index = index;
		fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmt->flags = V4L2_FMT_FLAG_COMPRESSED;
		strncpy(fmt->description, desc, sizeof(fmt->description));
		fmt->pixelformat = pixelformat;

		return 0;
	}
	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *fmt = arg;

		if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		return set_capture_size(go, fmt, 1);
	}
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *fmt = arg;

		if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		memset(fmt, 0, sizeof(*fmt));
		fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmt->fmt.pix.width = go->width;
		fmt->fmt.pix.height = go->height;
		fmt->fmt.pix.pixelformat = go->format == GO7007_FORMAT_MJPEG ?
			V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_MPEG;
		fmt->fmt.pix.field = V4L2_FIELD_NONE;
		fmt->fmt.pix.bytesperline = 0;
		fmt->fmt.pix.sizeimage = GO7007_BUF_SIZE;
		fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M; /* ?? */
		return 0;
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *fmt = arg;

		if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		if (go->streaming)
			return -EBUSY;
		return set_capture_size(go, fmt, 0);
	}
	case VIDIOC_G_FBUF:
	case VIDIOC_S_FBUF:
		return -EINVAL;
	case VIDIOC_REQBUFS:
	{
		struct v4l2_requestbuffers *req = arg;
		unsigned int count, i;

		if (go->streaming)
			return -EBUSY;
		if (req->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
				req->memory != V4L2_MEMORY_MMAP)
			return -EINVAL;

		down(&gofh->lock);
		retval = -EBUSY;
		for (i = 0; i < gofh->buf_count; ++i)
			if (gofh->bufs[i].mapped > 0)
				goto unlock_and_return;
		down(&go->hw_lock);
		if (go->in_use > 0 && gofh->buf_count == 0) {
			up(&go->hw_lock);
			goto unlock_and_return;
		}
		if (gofh->buf_count > 0)
			kfree(gofh->bufs);
		retval = -ENOMEM;
		count = req->count;
		if (count > 0) {
			if (count < 2)
				count = 2;
			if (count > 32)
				count = 32;
			gofh->bufs = kmalloc(count *
						sizeof(struct go7007_buffer),
					GFP_KERNEL);
			if (gofh->bufs == NULL) {
				up(&go->hw_lock);
				goto unlock_and_return;
			}
			memset(gofh->bufs, 0,
					count * sizeof(struct go7007_buffer));
			for (i = 0; i < count; ++i) {
				gofh->bufs[i].go = go;
				gofh->bufs[i].index = i;
				gofh->bufs[i].state = BUF_STATE_IDLE;
				gofh->bufs[i].mapped = 0;
			}
			go->in_use = 1;
		} else {
			go->in_use = 0;
		}
		gofh->buf_count = count;
		up(&go->hw_lock);
		up(&gofh->lock);
		memset(req, 0, sizeof(*req));
		req->count = count;
		req->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		req->memory = V4L2_MEMORY_MMAP;
		return 0;
	}
	case VIDIOC_QUERYBUF:
	{
		struct v4l2_buffer *buf = arg;
		unsigned int index;

		retval = -EINVAL;
		if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		index = buf->index;
		down(&gofh->lock);
		if (index >= gofh->buf_count)
			goto unlock_and_return;
		memset(buf, 0, sizeof(*buf));
		buf->index = index;
		buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		switch (gofh->bufs[index].state) {
		case BUF_STATE_QUEUED:
			buf->flags = V4L2_BUF_FLAG_QUEUED;
			break;
		case BUF_STATE_DONE:
			buf->flags = V4L2_BUF_FLAG_DONE;
			break;
		default:
			buf->flags = 0;
		}
		if (gofh->bufs[index].mapped)
			buf->flags |= V4L2_BUF_FLAG_MAPPED;
		buf->memory = V4L2_MEMORY_MMAP;
		buf->m.offset = index * GO7007_BUF_SIZE;
		buf->length = GO7007_BUF_SIZE;
		up(&gofh->lock);

		return 0;
	}
	case VIDIOC_QBUF:
	{
		struct v4l2_buffer *buf = arg;
		struct go7007_buffer *gobuf;
		int ret;

		retval = -EINVAL;
		if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
				buf->memory != V4L2_MEMORY_MMAP)
			return -EINVAL;
		down(&gofh->lock);
		if (buf->index < 0 || buf->index >= gofh->buf_count)
			goto unlock_and_return;
		gobuf = &gofh->bufs[buf->index];
		if (gobuf->mapped == 0)
			goto unlock_and_return;
		retval = -EBUSY;
		if (gobuf->state != BUF_STATE_IDLE)
			goto unlock_and_return;
		/* offset will be 0 until we really support USERPTR streaming */
		gobuf->offset = gobuf->user_addr & ~PAGE_MASK;
		gobuf->bytesused = 0;
		gobuf->frame_offset = 0;
		gobuf->modet_active = 0;
		if (gobuf->offset > 0)
			gobuf->page_count = GO7007_BUF_PAGES + 1;
		else
			gobuf->page_count = GO7007_BUF_PAGES;
		retval = -ENOMEM;
		down_read(&current->mm->mmap_sem);
		ret = get_user_pages(current, current->mm,
				gobuf->user_addr & PAGE_MASK, gobuf->page_count,
				1, 1, gobuf->pages, NULL);
		up_read(&current->mm->mmap_sem);
		if (ret != gobuf->page_count) {
			int i;
			for (i = 0; i < ret; ++i)
				page_cache_release(gobuf->pages[i]);
			gobuf->page_count = 0;
			goto unlock_and_return;
		}
		gobuf->state = BUF_STATE_QUEUED;
		spin_lock_irqsave(&go->spinlock, flags);
		list_add_tail(&gobuf->stream, &go->stream);
		spin_unlock_irqrestore(&go->spinlock, flags);
		up(&gofh->lock);
		return 0;
	}
	case VIDIOC_DQBUF:
	{
		struct v4l2_buffer *buf = arg;
		struct go7007_buffer *gobuf;
		u32 frame_type_flag;
		DEFINE_WAIT(wait);

		if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		if (buf->memory != V4L2_MEMORY_MMAP)
			return -EINVAL;
		down(&gofh->lock);
		retval = -EINVAL;
		if (list_empty(&go->stream))
			goto unlock_and_return;
		gobuf = list_entry(go->stream.next,
				struct go7007_buffer, stream);
		retval = -EAGAIN;
		if (gobuf->state != BUF_STATE_DONE &&
				!(file->f_flags & O_NONBLOCK)) {
			for (;;) {
				prepare_to_wait(&go->frame_waitq, &wait,
						TASK_INTERRUPTIBLE);
				if (gobuf->state == BUF_STATE_DONE)
					break;
				if (signal_pending(current)) {
					retval = -ERESTARTSYS;
					break;
				}
				schedule();
			}
			finish_wait(&go->frame_waitq, &wait);
		}
		if (gobuf->state != BUF_STATE_DONE)
			goto unlock_and_return;
		spin_lock_irqsave(&go->spinlock, flags);
		deactivate_buffer(gobuf);
		spin_unlock_irqrestore(&go->spinlock, flags);
		frame_type_flag = get_frame_type_flag(gobuf, go->format);
		gobuf->state = BUF_STATE_IDLE;
		memset(buf, 0, sizeof(*buf));
		buf->index = gobuf->index;
		buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf->bytesused = gobuf->bytesused;
		buf->flags = V4L2_BUF_FLAG_MAPPED | frame_type_flag;
		buf->field = V4L2_FIELD_NONE;
		buf->timestamp = gobuf->timestamp;
		buf->sequence = gobuf->seq;
		buf->memory = V4L2_MEMORY_MMAP;
		buf->m.offset = gobuf->index * GO7007_BUF_SIZE;
		buf->length = GO7007_BUF_SIZE;
		buf->reserved = gobuf->modet_active;
		up(&gofh->lock);
		return 0;
	}
	case VIDIOC_STREAMON:
	{
		unsigned int *type = arg;

		if (*type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		down(&gofh->lock);
		down(&go->hw_lock);
		if (!go->streaming) {
			go->streaming = 1;
			go->next_seq = 0;
			go->active_buf = NULL;
			if (go7007_start_encoder(go) < 0)
				retval = -EIO;
			else
				retval = 0;
		}
		up(&go->hw_lock);
		up(&gofh->lock);
		return retval;
	}
	case VIDIOC_STREAMOFF:
	{
		unsigned int *type = arg;

		if (*type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		down(&gofh->lock);
		go7007_streamoff(go);
		up(&gofh->lock);
		return 0;
	}
	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *ctrl = arg;
		u32 id;

		if (!go->i2c_adapter_online)
			return -EIO;
		id = ctrl->id;
		memset(ctrl, 0, sizeof(*ctrl));
		ctrl->id = id;
		i2c_clients_command(&go->i2c_adapter, VIDIOC_QUERYCTRL, arg);
		return ctrl->name[0] == 0 ? -EINVAL : 0;
	}
	case VIDIOC_G_CTRL:
	{
		struct v4l2_control *ctrl = arg;
		struct v4l2_queryctrl query;

		if (!go->i2c_adapter_online)
			return -EIO;
		memset(&query, 0, sizeof(query));
		query.id = ctrl->id;
		i2c_clients_command(&go->i2c_adapter, VIDIOC_QUERYCTRL, &query);
		if (query.name[0] == 0)
			return -EINVAL;
		i2c_clients_command(&go->i2c_adapter, VIDIOC_G_CTRL, arg);
		return 0;
	}
	case VIDIOC_S_CTRL:
	{
		struct v4l2_control *ctrl = arg;
		struct v4l2_queryctrl query;

		if (!go->i2c_adapter_online)
			return -EIO;
		memset(&query, 0, sizeof(query));
		query.id = ctrl->id;
		i2c_clients_command(&go->i2c_adapter, VIDIOC_QUERYCTRL, &query);
		if (query.name[0] == 0)
			return -EINVAL;
		i2c_clients_command(&go->i2c_adapter, VIDIOC_S_CTRL, arg);
		return 0;
	}
	case VIDIOC_G_PARM:
	{
		struct v4l2_streamparm *parm = arg;
		struct v4l2_fract timeperframe = {
			.numerator = 1001 *  go->fps_scale,
			.denominator = go->sensor_framerate,
		};

		if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		memset(parm, 0, sizeof(*parm));
		parm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		parm->parm.capture.capability |= V4L2_CAP_TIMEPERFRAME;
		parm->parm.capture.timeperframe = timeperframe;
		return 0;
	}
	case VIDIOC_S_PARM:
	{
		struct v4l2_streamparm *parm = arg;
		unsigned int n, d;

		if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		if (parm->parm.capture.capturemode != 0)
			return -EINVAL;
		n = go->sensor_framerate *
			parm->parm.capture.timeperframe.numerator;
		d = 1001 * parm->parm.capture.timeperframe.denominator;
		if (n != 0 && d != 0 && n > d)
			go->fps_scale = (n + d/2) / d;
		else
			go->fps_scale = 1;
		return 0;
	}
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *std = arg;

		if ((go->board_info->flags & GO7007_BOARD_HAS_TUNER) &&
				go->input == go->board_info->num_inputs - 1) {
			if (!go->i2c_adapter_online)
				return -EIO;
			i2c_clients_command(&go->i2c_adapter,
						VIDIOC_ENUMSTD, arg);
			if (!std->id) /* hack to indicate EINVAL from tuner */
				return -EINVAL;
		} else if (go->board_info->sensor_flags & GO7007_SENSOR_TV) {
			switch (std->index) {
			case 0:
				v4l2_video_std_construct(std,
						V4L2_STD_NTSC, "NTSC");
				break;
			case 1:
				v4l2_video_std_construct(std,
						V4L2_STD_PAL | V4L2_STD_SECAM,
						"PAL/SECAM");
				break;
			default:
				return -EINVAL;
			}
		} else {
			if (std->index != 0)
				return -EINVAL;
			memset(std, 0, sizeof(*std));
			snprintf(std->name, sizeof(std->name), "%dx%d, %dfps",
				go->board_info->sensor_width,
				go->board_info->sensor_height,
				go->board_info->sensor_framerate / 1000);
			std->frameperiod.numerator = 1001;
			std->frameperiod.denominator =
					go->board_info->sensor_framerate;
		}
		return 0;
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *std = arg;

		if ((go->board_info->flags & GO7007_BOARD_HAS_TUNER) &&
				go->input == go->board_info->num_inputs - 1) {
			if (!go->i2c_adapter_online)
				return -EIO;
			i2c_clients_command(&go->i2c_adapter,
						VIDIOC_G_STD, arg);
		} else if (go->board_info->sensor_flags & GO7007_SENSOR_TV) {
			if (go->standard == GO7007_STD_NTSC)
				*std = V4L2_STD_NTSC;
			else
				*std = V4L2_STD_PAL | V4L2_STD_SECAM;
		} else
			*std = 0;
		return 0;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *std = arg;

		if (go->streaming)
			return -EBUSY;
		if (!(go->board_info->sensor_flags & GO7007_SENSOR_TV) &&
				*std != 0)
			return -EINVAL;
		if (*std == 0)
			return -EINVAL;
		if ((go->board_info->flags & GO7007_BOARD_HAS_TUNER) &&
				go->input == go->board_info->num_inputs - 1) {
			if (!go->i2c_adapter_online)
				return -EIO;
			i2c_clients_command(&go->i2c_adapter,
						VIDIOC_S_STD, arg);
			if (!*std) /* hack to indicate EINVAL from tuner */
				return -EINVAL;
		}
		if (*std & V4L2_STD_NTSC) {
			go->standard = GO7007_STD_NTSC;
			go->sensor_framerate = 30000;
		} else if (*std & V4L2_STD_PAL) {
			go->standard = GO7007_STD_PAL;
			go->sensor_framerate = 25025;
		} else if (*std & V4L2_STD_SECAM) {
			go->standard = GO7007_STD_PAL;
			go->sensor_framerate = 25025;
		} else
			return -EINVAL;
		if (go->i2c_adapter_online)
			i2c_clients_command(&go->i2c_adapter,
					    VIDIOC_S_STD, std);
		set_capture_size(go, NULL, 0);
		return 0;
	}
	case VIDIOC_QUERYSTD:
	{
		v4l2_std_id *std = arg;

		if ((go->board_info->flags & GO7007_BOARD_HAS_TUNER) &&
				go->input == go->board_info->num_inputs - 1) {
			if (!go->i2c_adapter_online)
				return -EIO;
			i2c_clients_command(&go->i2c_adapter,
						VIDIOC_QUERYSTD, arg);
		} else if (go->board_info->sensor_flags & GO7007_SENSOR_TV)
			*std = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM;
		else
			*std = 0;
		return 0;
	}
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *inp = arg;
		int index;

		if (inp->index >= go->board_info->num_inputs)
			return -EINVAL;
		index = inp->index;
		memset(inp, 0, sizeof(*inp));
		inp->index = index;
		strncpy(inp->name, go->board_info->inputs[index].name,
				sizeof(inp->name));
		/* If this board has a tuner, it will be the last input */
		if ((go->board_info->flags & GO7007_BOARD_HAS_TUNER) &&
				index == go->board_info->num_inputs - 1)
			inp->type = V4L2_INPUT_TYPE_TUNER;
		else
			inp->type = V4L2_INPUT_TYPE_CAMERA;
		inp->audioset = 0;
		inp->tuner = 0;
		if (go->board_info->sensor_flags & GO7007_SENSOR_TV)
			inp->std = V4L2_STD_NTSC | V4L2_STD_PAL |
							V4L2_STD_SECAM;
		else
			inp->std = 0;
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *input = arg;

		*input = go->input;
		return 0;
	}
	case VIDIOC_S_INPUT:
	{
		int *input = arg;

		if (*input >= go->board_info->num_inputs)
			return -EINVAL;
		if (go->streaming)
			return -EBUSY;
		go->input = *input;
		if (go->i2c_adapter_online) {
			i2c_clients_command(&go->i2c_adapter, VIDIOC_S_INPUT,
				&go->board_info->inputs[*input].video_input);
			i2c_clients_command(&go->i2c_adapter, VIDIOC_S_AUDIO,
				&go->board_info->inputs[*input].audio_input);
		}
		return 0;
	}
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *t = arg;

		if (!(go->board_info->flags & GO7007_BOARD_HAS_TUNER))
			return -EINVAL;
		if (t->index != 0)
			return -EINVAL;
		if (!go->i2c_adapter_online)
			return -EIO;
		i2c_clients_command(&go->i2c_adapter, VIDIOC_G_TUNER, arg);
		t->index = 0;
		return 0;
	}
	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *t = arg;

		if (!(go->board_info->flags & GO7007_BOARD_HAS_TUNER))
			return -EINVAL;
		if (t->index != 0)
			return -EINVAL;
		if (!go->i2c_adapter_online)
			return -EIO;
		switch (go->board_id) {
		case GO7007_BOARDID_PX_TV402U_NA:
		case GO7007_BOARDID_PX_TV402U_JP:
			/* No selectable options currently */
			if (t->audmode != V4L2_TUNER_MODE_STEREO)
				return -EINVAL;
			break;
		}
		i2c_clients_command(&go->i2c_adapter, VIDIOC_S_TUNER, arg);
		return 0;
	}
	case VIDIOC_G_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		if (!(go->board_info->flags & GO7007_BOARD_HAS_TUNER))
			return -EINVAL;
		if (!go->i2c_adapter_online)
			return -EIO;
		memset(f, 0, sizeof(*f));
		f->type = V4L2_TUNER_ANALOG_TV;
		i2c_clients_command(&go->i2c_adapter, VIDIOC_G_FREQUENCY, arg);
		return 0;
	}
	case VIDIOC_S_FREQUENCY:
	{
		if (!(go->board_info->flags & GO7007_BOARD_HAS_TUNER))
			return -EINVAL;
		if (!go->i2c_adapter_online)
			return -EIO;
		i2c_clients_command(&go->i2c_adapter, VIDIOC_S_FREQUENCY, arg);
		return 0;
	}
	case VIDIOC_CROPCAP:
	{
		struct v4l2_cropcap *cropcap = arg;

		if (cropcap->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		memset(cropcap, 0, sizeof(*cropcap));
		cropcap->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		/* These specify the raw input of the sensor */
		switch (go->standard) {
		case GO7007_STD_NTSC:
			cropcap->bounds.top = 0;
			cropcap->bounds.left = 0;
			cropcap->bounds.width = 720;
			cropcap->bounds.height = 480;
			cropcap->defrect.top = 0;
			cropcap->defrect.left = 0;
			cropcap->defrect.width = 720;
			cropcap->defrect.height = 480;
			break;
		case GO7007_STD_PAL:
			cropcap->bounds.top = 0;
			cropcap->bounds.left = 0;
			cropcap->bounds.width = 720;
			cropcap->bounds.height = 576;
			cropcap->defrect.top = 0;
			cropcap->defrect.left = 0;
			cropcap->defrect.width = 720;
			cropcap->defrect.height = 576;
			break;
		case GO7007_STD_OTHER:
			cropcap->bounds.top = 0;
			cropcap->bounds.left = 0;
			cropcap->bounds.width = go->board_info->sensor_width;
			cropcap->bounds.height = go->board_info->sensor_height;
			cropcap->defrect.top = 0;
			cropcap->defrect.left = 0;
			cropcap->defrect.width = go->board_info->sensor_width;
			cropcap->defrect.height = go->board_info->sensor_height;
			break;
		}

		return 0;
	}
	case VIDIOC_G_CROP:
	{
		struct v4l2_crop *crop = arg;

		if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		memset(crop, 0, sizeof(*crop));
		crop->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		/* These specify the raw input of the sensor */
		switch (go->standard) {
		case GO7007_STD_NTSC:
			crop->c.top = 0;
			crop->c.left = 0;
			crop->c.width = 720;
			crop->c.height = 480;
			break;
		case GO7007_STD_PAL:
			crop->c.top = 0;
			crop->c.left = 0;
			crop->c.width = 720;
			crop->c.height = 576;
			break;
		case GO7007_STD_OTHER:
			crop->c.top = 0;
			crop->c.left = 0;
			crop->c.width = go->board_info->sensor_width;
			crop->c.height = go->board_info->sensor_height;
			break;
		}

		return 0;
	}
	case VIDIOC_S_CROP:
	{
		struct v4l2_crop *crop = arg;

		if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		return 0;
	}
	case VIDIOC_G_JPEGCOMP:
	{
		struct v4l2_jpegcompression *params = arg;

		memset(params, 0, sizeof(*params));
		params->quality = 50; /* ?? */
		params->jpeg_markers = V4L2_JPEG_MARKER_DHT |
					V4L2_JPEG_MARKER_DQT;

		return 0;
	}
	case VIDIOC_S_JPEGCOMP:
	{
		struct v4l2_jpegcompression *params = arg;

		if (params->quality != 50 ||
				params->jpeg_markers != (V4L2_JPEG_MARKER_DHT |
							V4L2_JPEG_MARKER_DQT))
			return -EINVAL;
		return 0;
	}
	/* Temporary ioctls for controlling compression characteristics */
	case GO7007IOC_S_BITRATE:
	{
		int *bitrate = arg;

		if (go->streaming)
			return -EINVAL;
		/* Upper bound is kind of arbitrary here */
		if (*bitrate < 64000 || *bitrate > 10000000)
			return -EINVAL;
		go->bitrate = *bitrate;
		return 0;
	}
	case GO7007IOC_G_BITRATE:
	{
		int *bitrate = arg;

		*bitrate = go->bitrate;
		return 0;
	}
	case GO7007IOC_S_COMP_PARAMS:
	{
		struct go7007_comp_params *comp = arg;

		if (go->format == GO7007_FORMAT_MJPEG)
			return -EINVAL;
		if (comp->gop_size > 0)
			go->gop_size = comp->gop_size;
		else
			go->gop_size = go->sensor_framerate / 1000;
		if (go->gop_size != 15)
			go->dvd_mode = 0;
		/*go->ipb = comp->max_b_frames > 0;*/ /* completely untested */
		if (go->board_info->sensor_flags & GO7007_SENSOR_TV) {
			switch (comp->aspect_ratio) {
			case GO7007_ASPECT_RATIO_4_3_NTSC:
			case GO7007_ASPECT_RATIO_4_3_PAL:
				go->aspect_ratio = GO7007_RATIO_4_3;
				break;
			case GO7007_ASPECT_RATIO_16_9_NTSC:
			case GO7007_ASPECT_RATIO_16_9_PAL:
				go->aspect_ratio = GO7007_RATIO_16_9;
				break;
			default:
				go->aspect_ratio = GO7007_RATIO_1_1;
				break;
			}
		}
		if (comp->flags & GO7007_COMP_OMIT_SEQ_HEADER) {
			go->dvd_mode = 0;
			go->seq_header_enable = 0;
		} else {
			go->seq_header_enable = 1;
		}
		/* fall-through */
	}
	case GO7007IOC_G_COMP_PARAMS:
	{
		struct go7007_comp_params *comp = arg;

		if (go->format == GO7007_FORMAT_MJPEG)
			return -EINVAL;
		memset(comp, 0, sizeof(*comp));
		comp->gop_size = go->gop_size;
		comp->max_b_frames = go->ipb ? 2 : 0;
		switch (go->aspect_ratio) {
		case GO7007_RATIO_4_3:
			if (go->standard == GO7007_STD_NTSC)
				comp->aspect_ratio =
					GO7007_ASPECT_RATIO_4_3_NTSC;
			else
				comp->aspect_ratio =
					GO7007_ASPECT_RATIO_4_3_PAL;
			break;
		case GO7007_RATIO_16_9:
			if (go->standard == GO7007_STD_NTSC)
				comp->aspect_ratio =
					GO7007_ASPECT_RATIO_16_9_NTSC;
			else
				comp->aspect_ratio =
					GO7007_ASPECT_RATIO_16_9_PAL;
			break;
		default:
			comp->aspect_ratio = GO7007_ASPECT_RATIO_1_1;
			break;
		}
		if (go->closed_gop)
			comp->flags |= GO7007_COMP_CLOSED_GOP;
		if (!go->seq_header_enable)
			comp->flags |= GO7007_COMP_OMIT_SEQ_HEADER;
		return 0;
	}
	case GO7007IOC_S_MPEG_PARAMS:
	{
		struct go7007_mpeg_params *mpeg = arg;

		if (go->format != GO7007_FORMAT_MPEG1 &&
				go->format != GO7007_FORMAT_MPEG2 &&
				go->format != GO7007_FORMAT_MPEG4)
			return -EINVAL;

		if (mpeg->flags & GO7007_MPEG_FORCE_DVD_MODE) {
			go->format = GO7007_FORMAT_MPEG2;
			go->bitrate = 9800000;
			go->gop_size = 15;
			go->pali = 0x48;
			go->closed_gop = 1;
			go->repeat_seqhead = 0;
			go->seq_header_enable = 1;
			go->gop_header_enable = 1;
			go->dvd_mode = 1;
		} else {
			switch (mpeg->mpeg_video_standard) {
			case GO7007_MPEG_VIDEO_MPEG1:
				go->format = GO7007_FORMAT_MPEG1;
				go->pali = 0;
				break;
			case GO7007_MPEG_VIDEO_MPEG2:
				go->format = GO7007_FORMAT_MPEG2;
				if (mpeg->pali >> 24 == 2)
					go->pali = mpeg->pali & 0xff;
				else
					go->pali = 0x48;
				break;
			case GO7007_MPEG_VIDEO_MPEG4:
				go->format = GO7007_FORMAT_MPEG4;
				if (mpeg->pali >> 24 == 4)
					go->pali = mpeg->pali & 0xff;
				else
					go->pali = 0xf5;
				break;
			default:
				return -EINVAL;
			}
			go->gop_header_enable =
				mpeg->flags & GO7007_MPEG_OMIT_GOP_HEADER
				? 0 : 1;
			if (mpeg->flags & GO7007_MPEG_REPEAT_SEQHEADER)
				go->repeat_seqhead = 1;
			else
				go->repeat_seqhead = 0;
			go->dvd_mode = 0;
		}
		/* fall-through */
	}
	case GO7007IOC_G_MPEG_PARAMS:
	{
		struct go7007_mpeg_params *mpeg = arg;

		memset(mpeg, 0, sizeof(*mpeg));
		switch (go->format) {
		case GO7007_FORMAT_MPEG1:
			mpeg->mpeg_video_standard = GO7007_MPEG_VIDEO_MPEG1;
			mpeg->pali = 0;
			break;
		case GO7007_FORMAT_MPEG2:
			mpeg->mpeg_video_standard = GO7007_MPEG_VIDEO_MPEG2;
			mpeg->pali = GO7007_MPEG_PROFILE(2, go->pali);
			break;
		case GO7007_FORMAT_MPEG4:
			mpeg->mpeg_video_standard = GO7007_MPEG_VIDEO_MPEG4;
			mpeg->pali = GO7007_MPEG_PROFILE(4, go->pali);
			break;
		default:
			return -EINVAL;
		}
		if (!go->gop_header_enable)
			mpeg->flags |= GO7007_MPEG_OMIT_GOP_HEADER;
		if (go->repeat_seqhead)
			mpeg->flags |= GO7007_MPEG_REPEAT_SEQHEADER;
		if (go->dvd_mode)
			mpeg->flags |= GO7007_MPEG_FORCE_DVD_MODE;
		return 0;
	}
	case GO7007IOC_S_MD_PARAMS:
	{
		struct go7007_md_params *mdp = arg;

		if (mdp->region > 3)
			return -EINVAL;
		if (mdp->trigger > 0) {
			go->modet[mdp->region].pixel_threshold =
					mdp->pixel_threshold >> 1;
			go->modet[mdp->region].motion_threshold =
					mdp->motion_threshold >> 1;
			go->modet[mdp->region].mb_threshold =
					mdp->trigger >> 1;
			go->modet[mdp->region].enable = 1;
		} else
			go->modet[mdp->region].enable = 0;
		/* fall-through */
	}
	case GO7007IOC_G_MD_PARAMS:
	{
		struct go7007_md_params *mdp = arg;
		int region = mdp->region;

		if (mdp->region > 3)
			return -EINVAL;
		memset(mdp, 0, sizeof(struct go7007_md_params));
		mdp->region = region;
		if (!go->modet[region].enable)
			return 0;
		mdp->pixel_threshold =
			(go->modet[region].pixel_threshold << 1) + 1;
		mdp->motion_threshold =
			(go->modet[region].motion_threshold << 1) + 1;
		mdp->trigger =
			(go->modet[region].mb_threshold << 1) + 1;
		return 0;
	}
	case GO7007IOC_S_MD_REGION:
	{
		struct go7007_md_region *region = arg;

		if (region->region < 1 || region->region > 3)
			return -EINVAL;
		return clip_to_modet_map(go, region->region, region->clips);
	}
	default:
		printk(KERN_DEBUG "go7007: unsupported ioctl %d\n", cmd);
		return -ENOIOCTLCMD;
	}
	return 0;

unlock_and_return:
	up(&gofh->lock);
	return retval;
}

static int go7007_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct go7007_file *gofh = file->private_data;

	if (gofh->go->status != STATUS_ONLINE)
		return -EIO;

	return video_usercopy(inode, file, cmd, arg, go7007_do_ioctl);
}

static ssize_t go7007_read(struct file *file, char __user *data,
		size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static void go7007_vm_open(struct vm_area_struct *vma)
{
	struct go7007_buffer *gobuf = vma->vm_private_data;

	++gobuf->mapped;
}

static void go7007_vm_close(struct vm_area_struct *vma)
{
	struct go7007_buffer *gobuf = vma->vm_private_data;
	unsigned long flags;

	if (--gobuf->mapped == 0) {
		spin_lock_irqsave(&gobuf->go->spinlock, flags);
		deactivate_buffer(gobuf);
		spin_unlock_irqrestore(&gobuf->go->spinlock, flags);
	}
}

/* Copied from videobuf-dma-sg.c */
static int go7007_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page;

	page = alloc_page(GFP_USER | __GFP_DMA32);
	if (!page)
		return VM_FAULT_OOM;
	clear_user_page(page_address(page), (unsigned long)vmf->virtual_address,
			page);
	vmf->page = page;
	return 0;
}

static struct vm_operations_struct go7007_vm_ops = {
	.open	= go7007_vm_open,
	.close	= go7007_vm_close,
	.fault	= go7007_vm_fault,
};

static int go7007_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct go7007_file *gofh = file->private_data;
	unsigned int index;

	if (gofh->go->status != STATUS_ONLINE)
		return -EIO;
	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL; /* only support VM_SHARED mapping */
	if (vma->vm_end - vma->vm_start != GO7007_BUF_SIZE)
		return -EINVAL; /* must map exactly one full buffer */
	down(&gofh->lock);
	index = vma->vm_pgoff / GO7007_BUF_PAGES;
	if (index >= gofh->buf_count) {
		up(&gofh->lock);
		return -EINVAL; /* trying to map beyond requested buffers */
	}
	if (index * GO7007_BUF_PAGES != vma->vm_pgoff) {
		up(&gofh->lock);
		return -EINVAL; /* offset is not aligned on buffer boundary */
	}
	if (gofh->bufs[index].mapped > 0) {
		up(&gofh->lock);
		return -EBUSY;
	}
	gofh->bufs[index].mapped = 1;
	gofh->bufs[index].user_addr = vma->vm_start;
	vma->vm_ops = &go7007_vm_ops;
	vma->vm_flags |= VM_DONTEXPAND;
	vma->vm_flags &= ~VM_IO;
	vma->vm_private_data = &gofh->bufs[index];
	up(&gofh->lock);
	return 0;
}

static unsigned int go7007_poll(struct file *file, poll_table *wait)
{
	struct go7007_file *gofh = file->private_data;
	struct go7007_buffer *gobuf;

	if (list_empty(&gofh->go->stream))
		return POLLERR;
	gobuf = list_entry(gofh->go->stream.next, struct go7007_buffer, stream);
	poll_wait(file, &gofh->go->frame_waitq, wait);
	if (gobuf->state == BUF_STATE_DONE)
		return POLLIN | POLLRDNORM;
	return 0;
}

static void go7007_vfl_release(struct video_device *vfd)
{
	struct go7007 *go = video_get_drvdata(vfd);

	video_device_release(vfd);
	if (--go->ref_count == 0)
		kfree(go);
}

static struct file_operations go7007_fops = {
	.owner		= THIS_MODULE,
	.open		= go7007_open,
	.release	= go7007_release,
	.ioctl		= go7007_ioctl,
	.llseek		= no_llseek,
	.read		= go7007_read,
	.mmap		= go7007_mmap,
	.poll		= go7007_poll,
};

static struct video_device go7007_template = {
	.name		= "go7007",
	.vfl_type	= VID_TYPE_CAPTURE,
	.fops		= &go7007_fops,
	.minor		= -1,
	.release	= go7007_vfl_release,
};

int go7007_v4l2_init(struct go7007 *go)
{
	int rv;

	go->video_dev = video_device_alloc();
	if (go->video_dev == NULL)
		return -ENOMEM;
	memcpy(go->video_dev, &go7007_template, sizeof(go7007_template));
	go->video_dev->parent = go->dev;
	rv = video_register_device(go->video_dev, VFL_TYPE_GRABBER, -1);
	if (rv < 0) {
		video_device_release(go->video_dev);
		go->video_dev = NULL;
		return rv;
	}
	video_set_drvdata(go->video_dev, go);
	++go->ref_count;

	return 0;
}

void go7007_v4l2_remove(struct go7007 *go)
{
	unsigned long flags;

	down(&go->hw_lock);
	if (go->streaming) {
		go->streaming = 0;
		go7007_stream_stop(go);
		spin_lock_irqsave(&go->spinlock, flags);
		abort_queued(go);
		spin_unlock_irqrestore(&go->spinlock, flags);
	}
	up(&go->hw_lock);
	if (go->video_dev)
		video_unregister_device(go->video_dev);
}
