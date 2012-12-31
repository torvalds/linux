/*
 * videobuf2-fb.c - FrameBuffer API emulator on top of Videobuf2 framework
 *
 * Copyright (C) 2011 Samsung Electronics
 *
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/fb.h>

#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-fb.h>

static int debug = 1;
module_param(debug, int, 0644);

#define dprintk(level, fmt, arg...)					\
	do {								\
		if (debug >= level)					\
			printk(KERN_DEBUG "vb2: " fmt, ## arg);		\
	} while (0)

struct vb2_fb_data {
	struct video_device *vfd;
	struct vb2_queue *q;
	struct device *dev;
	struct v4l2_requestbuffers req;
	struct v4l2_buffer b;
	struct v4l2_plane p;
	void *vaddr;
	unsigned int size;
	int refcount;
	int blank;
	int streaming;

	struct file fake_file;
	struct dentry fake_dentry;
	struct inode fake_inode;
};

static int vb2_fb_stop(struct fb_info *info);

struct fmt_desc {
	__u32			fourcc;
	__u32			bits_per_pixel;
	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

static struct fmt_desc fmt_conv_table[] = {
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.bits_per_pixel = 16,
		.red = {	.offset = 11,	.length = 5,	},
		.green = {	.offset = 5,	.length = 6,	},
		.blue = {	.offset = 0,	.length = 5,	},
	}, {
		.fourcc = V4L2_PIX_FMT_RGB555,
		.bits_per_pixel = 16,
		.red = {	.offset = 11,	.length = 5,	},
		.green = {	.offset = 5,	.length = 5,	},
		.blue = {	.offset = 0,	.length = 5,	},
	}, {
		.fourcc = V4L2_PIX_FMT_RGB444,
		.bits_per_pixel = 16,
		.red = {	.offset = 8,	.length = 4,	},
		.green = {	.offset = 4,	.length = 4,	},
		.blue = {	.offset = 0,	.length = 4,	},
		.transp = {	.offset = 12,	.length = 4,	},
	}, {
		.fourcc = V4L2_PIX_FMT_BGR32,
		.bits_per_pixel = 32,
		.red = {	.offset = 16,	.length = 4,	},
		.green = {	.offset = 8,	.length = 8,	},
		.blue = {	.offset = 0,	.length = 8,	},
		.transp = {	.offset = 24,	.length = 8,	},
	},
	/* TODO: add more format descriptors */
};

/**
 * vb2_drv_lock() - a shortcut to call driver specific lock()
 * @q:		videobuf2 queue
 */
static inline void vb2_drv_lock(struct vb2_queue *q)
{
	q->ops->wait_finish(q);
}

/**
 * vb2_drv_unlock() - a shortcut to call driver specific unlock()
 * @q:		videobuf2 queue
 */
static inline void vb2_drv_unlock(struct vb2_queue *q)
{
	q->ops->wait_prepare(q);
}

/**
 * vb2_fb_activate() - activate framebuffer emulator
 * @info:	framebuffer vb2 emulator data
 * This function activates framebuffer emulator. The pixel format
 * is acquired from video node, memory is allocated and framebuffer
 * structures are filled with valid data.
 */
static int vb2_fb_activate(struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;
	struct vb2_queue *q = data->q;
	struct fb_var_screeninfo *var;
	struct v4l2_format fmt;
	struct fmt_desc *conv = NULL;
	int width, height, fourcc, bpl, size;
	int i, ret = 0;
	int (*g_fmt)(struct file *file, void *fh, struct v4l2_format *f);

	/*
	 * Check if streaming api has not been already activated.
	 */
	if (q->streaming || q->num_buffers > 0)
		return -EBUSY;

	dprintk(3, "setting up framebuffer\n");

	/*
	 * Open video node.
	 */
	ret = data->vfd->fops->open(&data->fake_file);
	if (ret)
		return ret;

	/*
	 * Get format from the video node.
	 */
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = q->type;
	if (data->vfd->ioctl_ops->vidioc_g_fmt_vid_out) {
		g_fmt = data->vfd->ioctl_ops->vidioc_g_fmt_vid_out;
		ret = g_fmt(&data->fake_file, data->fake_file.private_data, &fmt);
		if (ret)
			goto err;
		width = fmt.fmt.pix.width;
		height = fmt.fmt.pix.height;
		fourcc = fmt.fmt.pix.pixelformat;
		bpl = fmt.fmt.pix.bytesperline;
		size = fmt.fmt.pix.sizeimage;
	} else if (data->vfd->ioctl_ops->vidioc_g_fmt_vid_out_mplane) {
		g_fmt = data->vfd->ioctl_ops->vidioc_g_fmt_vid_out_mplane;
		ret = g_fmt(&data->fake_file, data->fake_file.private_data, &fmt);
		if (ret)
			goto err;
		width = fmt.fmt.pix_mp.width;
		height = fmt.fmt.pix_mp.height;
		fourcc = fmt.fmt.pix_mp.pixelformat;
		bpl = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
		size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	} else {
		ret = -EINVAL;
		goto err;
	}

	dprintk(3, "fb emu: width %d height %d fourcc %08x size %d bpl %d\n",
		width, height, fourcc, size, bpl);

	/*
	 * Find format mapping with fourcc returned by g_fmt().
	 */
	for (i = 0; i < ARRAY_SIZE(fmt_conv_table); i++) {
		if (fmt_conv_table[i].fourcc == fourcc) {
			conv = &fmt_conv_table[i];
			break;
		}
	}

	if (conv == NULL) {
		ret = -EBUSY;
		goto err;
	}

	/*
	 * Request buffers and use MMAP type to force driver
	 * to allocate buffers by itself.
	 */
	data->req.count = 1;
	data->req.memory = V4L2_MEMORY_MMAP;
	data->req.type = q->type;
	ret = vb2_reqbufs(q, &data->req);
	if (ret)
		goto err;

	/*
	 * Check if plane_count is correct,
	 * multiplane buffers are not supported.
	 */
	if (q->bufs[0]->num_planes != 1) {
		data->req.count = 0;
		ret = -EBUSY;
		goto err;
	}

	/*
	 * Get kernel address of the buffer.
	 */
	data->vaddr = vb2_plane_vaddr(q->bufs[0], 0);
	if (data->vaddr == NULL) {
		ret = -EINVAL;
		goto err;
	}
	data->size = size = vb2_plane_size(q->bufs[0], 0);

	/*
	 * Clear the buffer
	 */
	memset(data->vaddr, 0, size);

	/*
	 * Setup framebuffer parameters
	 */
	info->screen_base = data->vaddr;
	info->screen_size = size;
	info->fix.line_length = bpl;
	info->fix.smem_len = info->fix.mmio_len = size;

	var = &info->var;
	var->xres = var->xres_virtual = var->width = width;
	var->yres = var->yres_virtual = var->height = height;
	var->bits_per_pixel = conv->bits_per_pixel;
	var->red = conv->red;
	var->green = conv->green;
	var->blue = conv->blue;
	var->transp = conv->transp;

	return 0;

err:
	data->vfd->fops->release(&data->fake_file);
	return ret;
}

/**
 * vb2_fb_deactivate() - deactivate framebuffer emulator
 * @info:	framebuffer vb2 emulator data
 * Stop displaying video data and close framebuffer emulator.
 */
static int vb2_fb_deactivate(struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;

	info->screen_base = NULL;
	info->screen_size = 0;
	data->blank = 1;
	data->streaming = 0;

	vb2_fb_stop(info);
	return data->vfd->fops->release(&data->fake_file);
}

/**
 * vb2_fb_start() - start displaying the video buffer
 * @info:	framebuffer vb2 emulator data
 * This function queues video buffer to the driver and starts streaming.
 */
static int vb2_fb_start(struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;
	struct v4l2_buffer *b = &data->b;
	struct v4l2_plane *p = &data->p;
	struct vb2_queue *q = data->q;
	int ret;

	if (data->streaming)
		return 0;

	/*
	 * Prepare the buffer and queue it.
	 */
	memset(b, 0, sizeof(*b));
	b->type = q->type;
	b->memory = q->memory;
	b->index = 0;

	if (b->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		b->bytesused = data->size;
		b->length = data->size;
	} else if (b->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		memset(p, 0, sizeof(*p));
		b->m.planes = p;
		b->length = 1;
		p->bytesused = data->size;
		p->length = data->size;
	}
	ret = vb2_qbuf(q, b);
	if (ret)
		return ret;

	/*
	 * Start streaming.
	 */
	ret = vb2_streamon(q, q->type);
	if (ret == 0) {
		data->streaming = 1;
		dprintk(3, "fb emu: enabled streaming\n");
	}
	return ret;
}

/**
 * vb2_fb_start() - stop displaying video buffer
 * @info:	framebuffer vb2 emulator data
 * This function stops streaming on the video driver.
 */
static int vb2_fb_stop(struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;
	struct vb2_queue *q = data->q;
	int ret = 0;

	if (data->streaming) {
		ret = vb2_streamoff(q, q->type);
		data->streaming = 0;
		dprintk(3, "fb emu: disabled streaming\n");
	}

	return ret;
}

/**
 * vb2_fb_open() - open method for emulated framebuffer
 * @info:	framebuffer vb2 emulator data
 * @user:	client type (0 means kernel, 1 mean userspace)
 */
static int vb2_fb_open(struct fb_info *info, int user)
{
	struct vb2_fb_data *data = info->par;
	int ret = 0;
	dprintk(3, "fb emu: open()\n");

	/*
	 * Reject open() call from fb console.
	 */
	if (user == 0)
		return -ENODEV;

	vb2_drv_lock(data->q);

	/*
	 * Activate emulation on the first open.
	 */
	if (data->refcount == 0)
		ret = vb2_fb_activate(info);

	if (ret == 0)
		data->refcount++;

	vb2_drv_unlock(data->q);

	return ret;
}

/**
 * vb2_fb_release() - release method for emulated framebuffer
 * @info:	framebuffer vb2 emulator data
 * @user:	client type (0 means kernel, 1 mean userspace)
 */
static int vb2_fb_release(struct fb_info *info, int user)
{
	struct vb2_fb_data *data = info->par;
	int ret = 0;

	dprintk(3, "fb emu: release()\n");

	vb2_drv_lock(data->q);

	if (--data->refcount == 0)
		ret = vb2_fb_deactivate(info);

	vb2_drv_unlock(data->q);

	return ret;
}

/**
 * vb2_fb_mmap() - mmap method for emulated framebuffer
 * @info:	framebuffer vb2 emulator data
 * @vma:	memory area to map
 */
static int vb2_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct vb2_fb_data *data = info->par;
	int ret = 0;

	dprintk(3, "fb emu: mmap offset %ld\n", vma->vm_pgoff);

	/*
	 * Add flags required by v4l2/vb2
	 */
	vma->vm_flags |= VM_SHARED;

	/*
	 * Only the most common case (mapping the whole framebuffer) is
	 * supported for now.
	 */
	if (vma->vm_pgoff != 0 || (vma->vm_end - vma->vm_start) < data->size)
		return -EINVAL;

	vb2_drv_lock(data->q);
	ret = vb2_mmap(data->q, vma);
	vb2_drv_unlock(data->q);

	return ret;
}

/**
 * vb2_fb_blank() - blank method for emulated framebuffer
 * @blank_mode:	requested blank method
 * @info:	framebuffer vb2 emulator data
 */
static int vb2_fb_blank(int blank_mode, struct fb_info *info)
{
	struct vb2_fb_data *data = info->par;
	int ret = -EBUSY;

	dprintk(3, "fb emu: blank mode %d, blank %d, streaming %d\n",
		blank_mode, data->blank, data->streaming);

	/*
	 * If no blank mode change then return immediately
	 */
	if ((data->blank && blank_mode != FB_BLANK_UNBLANK) ||
	    (!data->blank && blank_mode == FB_BLANK_UNBLANK))
		return 0;

	/*
	 * Currently blank works only if device has been opened first.
	 */
	if (!data->refcount)
		return -EBUSY;

	vb2_drv_lock(data->q);

	/*
	 * Start emulation if user requested mode == FB_BLANK_UNBLANK.
	 */
	if (blank_mode == FB_BLANK_UNBLANK && data->blank) {
		ret = vb2_fb_start(info);
		if (ret == 0)
			data->blank = 0;
	}

	/*
	 * Stop emulation if user requested mode != FB_BLANK_UNBLANK.
	 */
	if (blank_mode != FB_BLANK_UNBLANK && !data->blank) {
		ret = vb2_fb_stop(info);
		if (ret == 0)
			data->blank = 1;
	}

	vb2_drv_unlock(data->q);

	return ret;
}

static struct fb_ops vb2_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= vb2_fb_open,
	.fb_release	= vb2_fb_release,
	.fb_mmap	= vb2_fb_mmap,
	.fb_blank	= vb2_fb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

/**
 * vb2_fb_reqister() - register framebuffer emulation
 * @q:		videobuf2 queue
 * @vfd:	video node
 * This function registers framebuffer emulation for specified
 * videobuf2 queue and video node. It returns a pointer to the registered
 * framebuffer device.
 */
void *vb2_fb_register(struct vb2_queue *q, struct video_device *vfd)
{
	struct vb2_fb_data *data;
	struct fb_info *info;
	int ret;

	BUG_ON(q->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	     q->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	BUG_ON(!q->mem_ops->vaddr);
	BUG_ON(!q->ops->wait_prepare || !q->ops->wait_finish);
	BUG_ON(!vfd->ioctl_ops || !vfd->fops);

	if (!try_module_get(vfd->fops->owner))
		return ERR_PTR(-ENODEV);

	info = framebuffer_alloc(sizeof(struct vb2_fb_data), &vfd->dev);
	if (!info)
		return ERR_PTR(-ENOMEM);

	data = info->par;

	info->fix.type	= FB_TYPE_PACKED_PIXELS;
	info->fix.accel	= FB_ACCEL_NONE;
	info->fix.visual = FB_VISUAL_TRUECOLOR,
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.vmode	= FB_VMODE_NONINTERLACED;
	info->fbops = &vb2_fb_ops;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->screen_base = NULL;

	ret = register_framebuffer(info);
	if (ret)
		return ERR_PTR(ret);

	printk(KERN_INFO "fb%d: registered frame buffer emulation for /dev/%s\n",
	       info->node, dev_name(&vfd->dev));

	data->blank = 1;
	data->vfd = vfd;
	data->q = q;
	data->fake_file.f_path.dentry = &data->fake_dentry;
	data->fake_dentry.d_inode = &data->fake_inode;
	data->fake_inode.i_rdev = vfd->cdev->dev;

	return info;
}
EXPORT_SYMBOL_GPL(vb2_fb_register);

/**
 * vb2_fb_unreqister() - unregister framebuffer emulation
 * @fb_emu:	emulated framebuffer device
 */
int vb2_fb_unregister(void *fb_emu)
{
	struct fb_info *info = fb_emu;
	struct vb2_fb_data *data = info->par;
	struct module *owner = data->vfd->fops->owner;

	unregister_framebuffer(info);
	module_put(owner);
	return 0;
}
EXPORT_SYMBOL_GPL(vb2_fb_unregister);

MODULE_DESCRIPTION("FrameBuffer emulator for Videobuf2 and Video for Linux 2");
MODULE_AUTHOR("Marek Szyprowski");
MODULE_LICENSE("GPL");
