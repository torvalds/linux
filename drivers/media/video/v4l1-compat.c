/*
 *
 *	Video for Linux Two
 *	Backward Compatibility Layer
 *
 *	Support subroutines for providing V4L2 drivers with backward
 *	compatibility with applications using the old API.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Author:	Bill Dirks <bill@thedirks.org>
 *		et al.
 *
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/pgtable.h>

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");
MODULE_AUTHOR("Bill Dirks");
MODULE_DESCRIPTION("v4l(1) compatibility layer for v4l2 drivers.");
MODULE_LICENSE("GPL");

#define dprintk(fmt, arg...) \
	do { \
		if (debug) \
			printk(KERN_DEBUG "v4l1-compat: " fmt , ## arg);\
	} while (0)

/*
 *	I O C T L   T R A N S L A T I O N
 *
 *	From here on down is the code for translating the numerous
 *	ioctl commands from the old API to the new API.
 */

static int
get_v4l_control(struct file             *file,
		int			cid,
		v4l2_kioctl             drv)
{
	struct v4l2_queryctrl	qctrl2;
	struct v4l2_control	ctrl2;
	int			err;

	qctrl2.id = cid;
	err = drv(file, VIDIOC_QUERYCTRL, &qctrl2);
	if (err < 0)
		dprintk("VIDIOC_QUERYCTRL: %d\n", err);
	if (err == 0 && !(qctrl2.flags & V4L2_CTRL_FLAG_DISABLED)) {
		ctrl2.id = qctrl2.id;
		err = drv(file, VIDIOC_G_CTRL, &ctrl2);
		if (err < 0) {
			dprintk("VIDIOC_G_CTRL: %d\n", err);
			return 0;
		}
		return DIV_ROUND_CLOSEST((ctrl2.value-qctrl2.minimum) * 65535,
					 qctrl2.maximum - qctrl2.minimum);
	}
	return 0;
}

static int
set_v4l_control(struct file             *file,
		int			cid,
		int			value,
		v4l2_kioctl             drv)
{
	struct v4l2_queryctrl	qctrl2;
	struct v4l2_control	ctrl2;
	int			err;

	qctrl2.id = cid;
	err = drv(file, VIDIOC_QUERYCTRL, &qctrl2);
	if (err < 0)
		dprintk("VIDIOC_QUERYCTRL: %d\n", err);
	if (err == 0 &&
	    !(qctrl2.flags & V4L2_CTRL_FLAG_DISABLED) &&
	    !(qctrl2.flags & V4L2_CTRL_FLAG_GRABBED)) {
		if (value < 0)
			value = 0;
		if (value > 65535)
			value = 65535;
		if (value && qctrl2.type == V4L2_CTRL_TYPE_BOOLEAN)
			value = 65535;
		ctrl2.id = qctrl2.id;
		ctrl2.value =
			(value * (qctrl2.maximum - qctrl2.minimum)
			 + 32767)
			/ 65535;
		ctrl2.value += qctrl2.minimum;
		err = drv(file, VIDIOC_S_CTRL, &ctrl2);
		if (err < 0)
			dprintk("VIDIOC_S_CTRL: %d\n", err);
	}
	return 0;
}

/* ----------------------------------------------------------------- */

static const unsigned int palette2pixelformat[] = {
	[VIDEO_PALETTE_GREY]    = V4L2_PIX_FMT_GREY,
	[VIDEO_PALETTE_RGB555]  = V4L2_PIX_FMT_RGB555,
	[VIDEO_PALETTE_RGB565]  = V4L2_PIX_FMT_RGB565,
	[VIDEO_PALETTE_RGB24]   = V4L2_PIX_FMT_BGR24,
	[VIDEO_PALETTE_RGB32]   = V4L2_PIX_FMT_BGR32,
	/* yuv packed pixel */
	[VIDEO_PALETTE_YUYV]    = V4L2_PIX_FMT_YUYV,
	[VIDEO_PALETTE_YUV422]  = V4L2_PIX_FMT_YUYV,
	[VIDEO_PALETTE_UYVY]    = V4L2_PIX_FMT_UYVY,
	/* yuv planar */
	[VIDEO_PALETTE_YUV410P] = V4L2_PIX_FMT_YUV410,
	[VIDEO_PALETTE_YUV420]  = V4L2_PIX_FMT_YUV420,
	[VIDEO_PALETTE_YUV420P] = V4L2_PIX_FMT_YUV420,
	[VIDEO_PALETTE_YUV411P] = V4L2_PIX_FMT_YUV411P,
	[VIDEO_PALETTE_YUV422P] = V4L2_PIX_FMT_YUV422P,
};

static unsigned int __pure
palette_to_pixelformat(unsigned int palette)
{
	if (palette < ARRAY_SIZE(palette2pixelformat))
		return palette2pixelformat[palette];
	else
		return 0;
}

static unsigned int __attribute_const__
pixelformat_to_palette(unsigned int pixelformat)
{
	int	palette = 0;
	switch (pixelformat) {
	case V4L2_PIX_FMT_GREY:
		palette = VIDEO_PALETTE_GREY;
		break;
	case V4L2_PIX_FMT_RGB555:
		palette = VIDEO_PALETTE_RGB555;
		break;
	case V4L2_PIX_FMT_RGB565:
		palette = VIDEO_PALETTE_RGB565;
		break;
	case V4L2_PIX_FMT_BGR24:
		palette = VIDEO_PALETTE_RGB24;
		break;
	case V4L2_PIX_FMT_BGR32:
		palette = VIDEO_PALETTE_RGB32;
		break;
	/* yuv packed pixel */
	case V4L2_PIX_FMT_YUYV:
		palette = VIDEO_PALETTE_YUYV;
		break;
	case V4L2_PIX_FMT_UYVY:
		palette = VIDEO_PALETTE_UYVY;
		break;
	/* yuv planar */
	case V4L2_PIX_FMT_YUV410:
		palette = VIDEO_PALETTE_YUV420;
		break;
	case V4L2_PIX_FMT_YUV420:
		palette = VIDEO_PALETTE_YUV420;
		break;
	case V4L2_PIX_FMT_YUV411P:
		palette = VIDEO_PALETTE_YUV411P;
		break;
	case V4L2_PIX_FMT_YUV422P:
		palette = VIDEO_PALETTE_YUV422P;
		break;
	}
	return palette;
}

/* ----------------------------------------------------------------- */

static int poll_one(struct file *file, struct poll_wqueues *pwq)
{
	int retval = 1;
	poll_table *table;

	poll_initwait(pwq);
	table = &pwq->pt;
	for (;;) {
		int mask;
		mask = file->f_op->poll(file, table);
		if (mask & POLLIN)
			break;
		table = NULL;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		poll_schedule(pwq, TASK_INTERRUPTIBLE);
	}
	poll_freewait(pwq);
	return retval;
}

static int count_inputs(
			struct file *file,
			v4l2_kioctl drv)
{
	struct v4l2_input input2;
	int i;

	for (i = 0;; i++) {
		memset(&input2, 0, sizeof(input2));
		input2.index = i;
		if (0 != drv(file, VIDIOC_ENUMINPUT, &input2))
			break;
	}
	return i;
}

static int check_size(
		struct file *file,
		v4l2_kioctl drv,
		int *maxw,
		int *maxh)
{
	struct v4l2_fmtdesc desc2;
	struct v4l2_format  fmt2;

	memset(&desc2, 0, sizeof(desc2));
	memset(&fmt2, 0, sizeof(fmt2));

	desc2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (0 != drv(file, VIDIOC_ENUM_FMT, &desc2))
		goto done;

	fmt2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt2.fmt.pix.width       = 10000;
	fmt2.fmt.pix.height      = 10000;
	fmt2.fmt.pix.pixelformat = desc2.pixelformat;
	if (0 != drv(file, VIDIOC_TRY_FMT, &fmt2))
		goto done;

	*maxw = fmt2.fmt.pix.width;
	*maxh = fmt2.fmt.pix.height;

done:
	return 0;
}

/* ----------------------------------------------------------------- */

static noinline long v4l1_compat_get_capabilities(
					struct video_capability *cap,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_framebuffer fbuf;
	struct v4l2_capability *cap2;

	cap2 = kzalloc(sizeof(*cap2), GFP_KERNEL);
	if (!cap2) {
		err = -ENOMEM;
		return err;
	}
	memset(cap, 0, sizeof(*cap));
	memset(&fbuf, 0, sizeof(fbuf));

	err = drv(file, VIDIOC_QUERYCAP, cap2);
	if (err < 0) {
		dprintk("VIDIOCGCAP / VIDIOC_QUERYCAP: %ld\n", err);
		goto done;
	}
	if (cap2->capabilities & V4L2_CAP_VIDEO_OVERLAY) {
		err = drv(file, VIDIOC_G_FBUF, &fbuf);
		if (err < 0) {
			dprintk("VIDIOCGCAP / VIDIOC_G_FBUF: %ld\n", err);
			memset(&fbuf, 0, sizeof(fbuf));
		}
		err = 0;
	}

	memcpy(cap->name, cap2->card,
	       min(sizeof(cap->name), sizeof(cap2->card)));
	cap->name[sizeof(cap->name) - 1] = 0;
	if (cap2->capabilities & V4L2_CAP_VIDEO_CAPTURE)
		cap->type |= VID_TYPE_CAPTURE;
	if (cap2->capabilities & V4L2_CAP_TUNER)
		cap->type |= VID_TYPE_TUNER;
	if (cap2->capabilities & V4L2_CAP_VBI_CAPTURE)
		cap->type |= VID_TYPE_TELETEXT;
	if (cap2->capabilities & V4L2_CAP_VIDEO_OVERLAY)
		cap->type |= VID_TYPE_OVERLAY;
	if (fbuf.capability & V4L2_FBUF_CAP_LIST_CLIPPING)
		cap->type |= VID_TYPE_CLIPPING;

	cap->channels  = count_inputs(file, drv);
	check_size(file, drv,
		   &cap->maxwidth, &cap->maxheight);
	cap->audios    =  0; /* FIXME */
	cap->minwidth  = 48; /* FIXME */
	cap->minheight = 32; /* FIXME */

done:
	kfree(cap2);
	return err;
}

static noinline long v4l1_compat_get_frame_buffer(
					struct video_buffer *buffer,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_framebuffer fbuf;

	memset(buffer, 0, sizeof(*buffer));
	memset(&fbuf, 0, sizeof(fbuf));

	err = drv(file, VIDIOC_G_FBUF, &fbuf);
	if (err < 0) {
		dprintk("VIDIOCGFBUF / VIDIOC_G_FBUF: %ld\n", err);
		goto done;
	}
	buffer->base   = fbuf.base;
	buffer->height = fbuf.fmt.height;
	buffer->width  = fbuf.fmt.width;

	switch (fbuf.fmt.pixelformat) {
	case V4L2_PIX_FMT_RGB332:
		buffer->depth = 8;
		break;
	case V4L2_PIX_FMT_RGB555:
		buffer->depth = 15;
		break;
	case V4L2_PIX_FMT_RGB565:
		buffer->depth = 16;
		break;
	case V4L2_PIX_FMT_BGR24:
		buffer->depth = 24;
		break;
	case V4L2_PIX_FMT_BGR32:
		buffer->depth = 32;
		break;
	default:
		buffer->depth = 0;
	}
	if (fbuf.fmt.bytesperline) {
		buffer->bytesperline = fbuf.fmt.bytesperline;
		if (!buffer->depth && buffer->width)
			buffer->depth   = ((fbuf.fmt.bytesperline<<3)
					  + (buffer->width-1))
					  / buffer->width;
	} else {
		buffer->bytesperline =
			(buffer->width * buffer->depth + 7) & 7;
		buffer->bytesperline >>= 3;
	}
done:
	return err;
}

static noinline long v4l1_compat_set_frame_buffer(
					struct video_buffer *buffer,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_framebuffer fbuf;

	memset(&fbuf, 0, sizeof(fbuf));
	fbuf.base       = buffer->base;
	fbuf.fmt.height = buffer->height;
	fbuf.fmt.width  = buffer->width;
	switch (buffer->depth) {
	case 8:
		fbuf.fmt.pixelformat = V4L2_PIX_FMT_RGB332;
		break;
	case 15:
		fbuf.fmt.pixelformat = V4L2_PIX_FMT_RGB555;
		break;
	case 16:
		fbuf.fmt.pixelformat = V4L2_PIX_FMT_RGB565;
		break;
	case 24:
		fbuf.fmt.pixelformat = V4L2_PIX_FMT_BGR24;
		break;
	case 32:
		fbuf.fmt.pixelformat = V4L2_PIX_FMT_BGR32;
		break;
	}
	fbuf.fmt.bytesperline = buffer->bytesperline;
	err = drv(file, VIDIOC_S_FBUF, &fbuf);
	if (err < 0)
		dprintk("VIDIOCSFBUF / VIDIOC_S_FBUF: %ld\n", err);
	return err;
}

static noinline long v4l1_compat_get_win_cap_dimensions(
					struct video_window *win,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_format *fmt;

	fmt = kzalloc(sizeof(*fmt), GFP_KERNEL);
	if (!fmt) {
		err = -ENOMEM;
		return err;
	}
	memset(win, 0, sizeof(*win));

	fmt->type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	err = drv(file, VIDIOC_G_FMT, fmt);
	if (err < 0)
		dprintk("VIDIOCGWIN / VIDIOC_G_WIN: %ld\n", err);
	if (err == 0) {
		win->x         = fmt->fmt.win.w.left;
		win->y         = fmt->fmt.win.w.top;
		win->width     = fmt->fmt.win.w.width;
		win->height    = fmt->fmt.win.w.height;
		win->chromakey = fmt->fmt.win.chromakey;
		win->clips     = NULL;
		win->clipcount = 0;
		goto done;
	}

	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	err = drv(file, VIDIOC_G_FMT, fmt);
	if (err < 0) {
		dprintk("VIDIOCGWIN / VIDIOC_G_FMT: %ld\n", err);
		goto done;
	}
	win->x         = 0;
	win->y         = 0;
	win->width     = fmt->fmt.pix.width;
	win->height    = fmt->fmt.pix.height;
	win->chromakey = 0;
	win->clips     = NULL;
	win->clipcount = 0;
done:
	kfree(fmt);
	return err;
}

static noinline long v4l1_compat_set_win_cap_dimensions(
					struct video_window *win,
					struct file *file,
					v4l2_kioctl drv)
{
	long err, err1, err2;
	struct v4l2_format *fmt;

	fmt = kzalloc(sizeof(*fmt), GFP_KERNEL);
	if (!fmt) {
		err = -ENOMEM;
		return err;
	}
	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	drv(file, VIDIOC_STREAMOFF, &fmt->type);
	err1 = drv(file, VIDIOC_G_FMT, fmt);
	if (err1 < 0)
		dprintk("VIDIOCSWIN / VIDIOC_G_FMT: %ld\n", err1);
	if (err1 == 0) {
		fmt->fmt.pix.width  = win->width;
		fmt->fmt.pix.height = win->height;
		fmt->fmt.pix.field  = V4L2_FIELD_ANY;
		fmt->fmt.pix.bytesperline = 0;
		err = drv(file, VIDIOC_S_FMT, fmt);
		if (err < 0)
			dprintk("VIDIOCSWIN / VIDIOC_S_FMT #1: %ld\n",
				err);
		win->width  = fmt->fmt.pix.width;
		win->height = fmt->fmt.pix.height;
	}

	memset(fmt, 0, sizeof(*fmt));
	fmt->type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	fmt->fmt.win.w.left    = win->x;
	fmt->fmt.win.w.top     = win->y;
	fmt->fmt.win.w.width   = win->width;
	fmt->fmt.win.w.height  = win->height;
	fmt->fmt.win.chromakey = win->chromakey;
	fmt->fmt.win.clips     = (void __user *)win->clips;
	fmt->fmt.win.clipcount = win->clipcount;
	err2 = drv(file, VIDIOC_S_FMT, fmt);
	if (err2 < 0)
		dprintk("VIDIOCSWIN / VIDIOC_S_FMT #2: %ld\n", err2);

	if (err1 != 0 && err2 != 0)
		err = err1;
	else
		err = 0;
	kfree(fmt);
	return err;
}

static noinline long v4l1_compat_turn_preview_on_off(
					int *on,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	enum v4l2_buf_type captype = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (0 == *on) {
		/* dirty hack time.  But v4l1 has no STREAMOFF
		 * equivalent in the API, and this one at
		 * least comes close ... */
		drv(file, VIDIOC_STREAMOFF, &captype);
	}
	err = drv(file, VIDIOC_OVERLAY, on);
	if (err < 0)
		dprintk("VIDIOCCAPTURE / VIDIOC_PREVIEW: %ld\n", err);
	return err;
}

static noinline long v4l1_compat_get_input_info(
					struct video_channel *chan,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_input	input2;
	v4l2_std_id    		sid;

	memset(&input2, 0, sizeof(input2));
	input2.index = chan->channel;
	err = drv(file, VIDIOC_ENUMINPUT, &input2);
	if (err < 0) {
		dprintk("VIDIOCGCHAN / VIDIOC_ENUMINPUT: "
			"channel=%d err=%ld\n", chan->channel, err);
		goto done;
	}
	chan->channel = input2.index;
	memcpy(chan->name, input2.name,
	       min(sizeof(chan->name), sizeof(input2.name)));
	chan->name[sizeof(chan->name) - 1] = 0;
	chan->tuners = (input2.type == V4L2_INPUT_TYPE_TUNER) ? 1 : 0;
	chan->flags = (chan->tuners) ? VIDEO_VC_TUNER : 0;
	switch (input2.type) {
	case V4L2_INPUT_TYPE_TUNER:
		chan->type = VIDEO_TYPE_TV;
		break;
	default:
	case V4L2_INPUT_TYPE_CAMERA:
		chan->type = VIDEO_TYPE_CAMERA;
		break;
	}
	chan->norm = 0;
	/* Note: G_STD might not be present for radio receivers,
	 * so we should ignore any errors. */
	if (drv(file, VIDIOC_G_STD, &sid) == 0) {
		if (sid & V4L2_STD_PAL)
			chan->norm = VIDEO_MODE_PAL;
		if (sid & V4L2_STD_NTSC)
			chan->norm = VIDEO_MODE_NTSC;
		if (sid & V4L2_STD_SECAM)
			chan->norm = VIDEO_MODE_SECAM;
		if (sid == V4L2_STD_ALL)
			chan->norm = VIDEO_MODE_AUTO;
	}
done:
	return err;
}

static noinline long v4l1_compat_set_input(
					struct video_channel *chan,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	v4l2_std_id sid = 0;

	err = drv(file, VIDIOC_S_INPUT, &chan->channel);
	if (err < 0)
		dprintk("VIDIOCSCHAN / VIDIOC_S_INPUT: %ld\n", err);
	switch (chan->norm) {
	case VIDEO_MODE_PAL:
		sid = V4L2_STD_PAL;
		break;
	case VIDEO_MODE_NTSC:
		sid = V4L2_STD_NTSC;
		break;
	case VIDEO_MODE_SECAM:
		sid = V4L2_STD_SECAM;
		break;
	case VIDEO_MODE_AUTO:
		sid = V4L2_STD_ALL;
		break;
	}
	if (0 != sid) {
		err = drv(file, VIDIOC_S_STD, &sid);
		if (err < 0)
			dprintk("VIDIOCSCHAN / VIDIOC_S_STD: %ld\n", err);
	}
	return err;
}

static noinline long v4l1_compat_get_picture(
					struct video_picture *pict,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_format *fmt;

	fmt = kzalloc(sizeof(*fmt), GFP_KERNEL);
	if (!fmt) {
		err = -ENOMEM;
		return err;
	}

	pict->brightness = get_v4l_control(file,
					   V4L2_CID_BRIGHTNESS, drv);
	pict->hue = get_v4l_control(file,
				    V4L2_CID_HUE, drv);
	pict->contrast = get_v4l_control(file,
					 V4L2_CID_CONTRAST, drv);
	pict->colour = get_v4l_control(file,
				       V4L2_CID_SATURATION, drv);
	pict->whiteness = get_v4l_control(file,
					  V4L2_CID_WHITENESS, drv);

	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	err = drv(file, VIDIOC_G_FMT, fmt);
	if (err < 0) {
		dprintk("VIDIOCGPICT / VIDIOC_G_FMT: %ld\n", err);
		goto done;
	}

	if (fmt->fmt.pix.width)
	{
		pict->depth   = ((fmt->fmt.pix.bytesperline << 3)
				 + (fmt->fmt.pix.width - 1))
				 / fmt->fmt.pix.width;
	} else {
		err = -EINVAL;
		goto done;
	}

	pict->palette = pixelformat_to_palette(
		fmt->fmt.pix.pixelformat);
done:
	kfree(fmt);
	return err;
}

static noinline long v4l1_compat_set_picture(
					struct video_picture *pict,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_framebuffer fbuf;
	int mem_err = 0, ovl_err = 0;
	struct v4l2_format *fmt;

	fmt = kzalloc(sizeof(*fmt), GFP_KERNEL);
	if (!fmt) {
		err = -ENOMEM;
		return err;
	}
	memset(&fbuf, 0, sizeof(fbuf));

	set_v4l_control(file,
			V4L2_CID_BRIGHTNESS, pict->brightness, drv);
	set_v4l_control(file,
			V4L2_CID_HUE, pict->hue, drv);
	set_v4l_control(file,
			V4L2_CID_CONTRAST, pict->contrast, drv);
	set_v4l_control(file,
			V4L2_CID_SATURATION, pict->colour, drv);
	set_v4l_control(file,
			V4L2_CID_WHITENESS, pict->whiteness, drv);
	/*
	 * V4L1 uses this ioctl to set both memory capture and overlay
	 * pixel format, while V4L2 has two different ioctls for this.
	 * Some cards may not support one or the other, and may support
	 * different pixel formats for memory vs overlay.
	 */

	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	err = drv(file, VIDIOC_G_FMT, fmt);
	/* If VIDIOC_G_FMT failed, then the driver likely doesn't
	   support memory capture.  Trying to set the memory capture
	   parameters would be pointless.  */
	if (err < 0) {
		dprintk("VIDIOCSPICT / VIDIOC_G_FMT: %ld\n", err);
		mem_err = -1000;  /* didn't even try */
	} else if (fmt->fmt.pix.pixelformat !=
		 palette_to_pixelformat(pict->palette)) {
		fmt->fmt.pix.pixelformat = palette_to_pixelformat(
			pict->palette);
		mem_err = drv(file, VIDIOC_S_FMT, fmt);
		if (mem_err < 0)
			dprintk("VIDIOCSPICT / VIDIOC_S_FMT: %d\n",
				mem_err);
	}

	err = drv(file, VIDIOC_G_FBUF, &fbuf);
	/* If VIDIOC_G_FBUF failed, then the driver likely doesn't
	   support overlay.  Trying to set the overlay parameters
	   would be quite pointless.  */
	if (err < 0) {
		dprintk("VIDIOCSPICT / VIDIOC_G_FBUF: %ld\n", err);
		ovl_err = -1000;  /* didn't even try */
	} else if (fbuf.fmt.pixelformat !=
		 palette_to_pixelformat(pict->palette)) {
		fbuf.fmt.pixelformat = palette_to_pixelformat(
			pict->palette);
		ovl_err = drv(file, VIDIOC_S_FBUF, &fbuf);
		if (ovl_err < 0)
			dprintk("VIDIOCSPICT / VIDIOC_S_FBUF: %d\n",
				ovl_err);
	}
	if (ovl_err < 0 && mem_err < 0) {
		/* ioctl failed, couldn't set either parameter */
		if (mem_err != -1000)
			err = mem_err;
		else if (ovl_err == -EPERM)
			err = 0;
		else
			err = ovl_err;
	} else
		err = 0;
	kfree(fmt);
	return err;
}

static noinline long v4l1_compat_get_tuner(
					struct video_tuner *tun,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	int i;
	struct v4l2_tuner	tun2;
	struct v4l2_standard	std2;
	v4l2_std_id    		sid;

	memset(&tun2, 0, sizeof(tun2));
	err = drv(file, VIDIOC_G_TUNER, &tun2);
	if (err < 0) {
		dprintk("VIDIOCGTUNER / VIDIOC_G_TUNER: %ld\n", err);
		goto done;
	}
	memcpy(tun->name, tun2.name,
	       min(sizeof(tun->name), sizeof(tun2.name)));
	tun->name[sizeof(tun->name) - 1] = 0;
	tun->rangelow = tun2.rangelow;
	tun->rangehigh = tun2.rangehigh;
	tun->flags = 0;
	tun->mode = VIDEO_MODE_AUTO;

	for (i = 0; i < 64; i++) {
		memset(&std2, 0, sizeof(std2));
		std2.index = i;
		if (0 != drv(file, VIDIOC_ENUMSTD, &std2))
			break;
		if (std2.id & V4L2_STD_PAL)
			tun->flags |= VIDEO_TUNER_PAL;
		if (std2.id & V4L2_STD_NTSC)
			tun->flags |= VIDEO_TUNER_NTSC;
		if (std2.id & V4L2_STD_SECAM)
			tun->flags |= VIDEO_TUNER_SECAM;
	}

	/* Note: G_STD might not be present for radio receivers,
	 * so we should ignore any errors. */
	if (drv(file, VIDIOC_G_STD, &sid) == 0) {
		if (sid & V4L2_STD_PAL)
			tun->mode = VIDEO_MODE_PAL;
		if (sid & V4L2_STD_NTSC)
			tun->mode = VIDEO_MODE_NTSC;
		if (sid & V4L2_STD_SECAM)
			tun->mode = VIDEO_MODE_SECAM;
	}

	if (tun2.capability & V4L2_TUNER_CAP_LOW)
		tun->flags |= VIDEO_TUNER_LOW;
	if (tun2.rxsubchans & V4L2_TUNER_SUB_STEREO)
		tun->flags |= VIDEO_TUNER_STEREO_ON;
	tun->signal = tun2.signal;
done:
	return err;
}

static noinline long v4l1_compat_select_tuner(
					struct video_tuner *tun,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_tuner	t;/*84 bytes on x86_64*/
	memset(&t, 0, sizeof(t));

	t.index = tun->tuner;

	err = drv(file, VIDIOC_S_TUNER, &t);
	if (err < 0)
		dprintk("VIDIOCSTUNER / VIDIOC_S_TUNER: %ld\n", err);
	return err;
}

static noinline long v4l1_compat_get_frequency(
					unsigned long *freq,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_frequency   freq2;
	memset(&freq2, 0, sizeof(freq2));

	freq2.tuner = 0;
	err = drv(file, VIDIOC_G_FREQUENCY, &freq2);
	if (err < 0)
		dprintk("VIDIOCGFREQ / VIDIOC_G_FREQUENCY: %ld\n", err);
	if (0 == err)
		*freq = freq2.frequency;
	return err;
}

static noinline long v4l1_compat_set_frequency(
					unsigned long *freq,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_frequency   freq2;
	memset(&freq2, 0, sizeof(freq2));

	drv(file, VIDIOC_G_FREQUENCY, &freq2);
	freq2.frequency = *freq;
	err = drv(file, VIDIOC_S_FREQUENCY, &freq2);
	if (err < 0)
		dprintk("VIDIOCSFREQ / VIDIOC_S_FREQUENCY: %ld\n", err);
	return err;
}

static noinline long v4l1_compat_get_audio(
					struct video_audio *aud,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	int i;
	struct v4l2_queryctrl	qctrl2;
	struct v4l2_audio	aud2;
	struct v4l2_tuner	tun2;
	memset(&aud2, 0, sizeof(aud2));

	err = drv(file, VIDIOC_G_AUDIO, &aud2);
	if (err < 0) {
		dprintk("VIDIOCGAUDIO / VIDIOC_G_AUDIO: %ld\n", err);
		goto done;
	}
	memcpy(aud->name, aud2.name,
	       min(sizeof(aud->name), sizeof(aud2.name)));
	aud->name[sizeof(aud->name) - 1] = 0;
	aud->audio = aud2.index;
	aud->flags = 0;
	i = get_v4l_control(file, V4L2_CID_AUDIO_VOLUME, drv);
	if (i >= 0) {
		aud->volume = i;
		aud->flags |= VIDEO_AUDIO_VOLUME;
	}
	i = get_v4l_control(file, V4L2_CID_AUDIO_BASS, drv);
	if (i >= 0) {
		aud->bass = i;
		aud->flags |= VIDEO_AUDIO_BASS;
	}
	i = get_v4l_control(file, V4L2_CID_AUDIO_TREBLE, drv);
	if (i >= 0) {
		aud->treble = i;
		aud->flags |= VIDEO_AUDIO_TREBLE;
	}
	i = get_v4l_control(file, V4L2_CID_AUDIO_BALANCE, drv);
	if (i >= 0) {
		aud->balance = i;
		aud->flags |= VIDEO_AUDIO_BALANCE;
	}
	i = get_v4l_control(file, V4L2_CID_AUDIO_MUTE, drv);
	if (i >= 0) {
		if (i)
			aud->flags |= VIDEO_AUDIO_MUTE;
		aud->flags |= VIDEO_AUDIO_MUTABLE;
	}
	aud->step = 1;
	qctrl2.id = V4L2_CID_AUDIO_VOLUME;
	if (drv(file, VIDIOC_QUERYCTRL, &qctrl2) == 0 &&
	    !(qctrl2.flags & V4L2_CTRL_FLAG_DISABLED))
		aud->step = qctrl2.step;
	aud->mode = 0;

	memset(&tun2, 0, sizeof(tun2));
	err = drv(file, VIDIOC_G_TUNER, &tun2);
	if (err < 0) {
		dprintk("VIDIOCGAUDIO / VIDIOC_G_TUNER: %ld\n", err);
		err = 0;
		goto done;
	}

	if (tun2.rxsubchans & V4L2_TUNER_SUB_LANG2)
		aud->mode = VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	else if (tun2.rxsubchans & V4L2_TUNER_SUB_STEREO)
		aud->mode = VIDEO_SOUND_STEREO;
	else if (tun2.rxsubchans & V4L2_TUNER_SUB_MONO)
		aud->mode = VIDEO_SOUND_MONO;
done:
	return err;
}

static noinline long v4l1_compat_set_audio(
					struct video_audio *aud,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	struct v4l2_audio	aud2;
	struct v4l2_tuner	tun2;

	memset(&aud2, 0, sizeof(aud2));
	memset(&tun2, 0, sizeof(tun2));

	aud2.index = aud->audio;
	err = drv(file, VIDIOC_S_AUDIO, &aud2);
	if (err < 0) {
		dprintk("VIDIOCSAUDIO / VIDIOC_S_AUDIO: %ld\n", err);
		goto done;
	}

	set_v4l_control(file, V4L2_CID_AUDIO_VOLUME,
			aud->volume, drv);
	set_v4l_control(file, V4L2_CID_AUDIO_BASS,
			aud->bass, drv);
	set_v4l_control(file, V4L2_CID_AUDIO_TREBLE,
			aud->treble, drv);
	set_v4l_control(file, V4L2_CID_AUDIO_BALANCE,
			aud->balance, drv);
	set_v4l_control(file, V4L2_CID_AUDIO_MUTE,
			!!(aud->flags & VIDEO_AUDIO_MUTE), drv);

	err = drv(file, VIDIOC_G_TUNER, &tun2);
	if (err < 0)
		dprintk("VIDIOCSAUDIO / VIDIOC_G_TUNER: %ld\n", err);
	if (err == 0) {
		switch (aud->mode) {
		default:
		case VIDEO_SOUND_MONO:
		case VIDEO_SOUND_LANG1:
			tun2.audmode = V4L2_TUNER_MODE_MONO;
			break;
		case VIDEO_SOUND_STEREO:
			tun2.audmode = V4L2_TUNER_MODE_STEREO;
			break;
		case VIDEO_SOUND_LANG2:
			tun2.audmode = V4L2_TUNER_MODE_LANG2;
			break;
		}
		err = drv(file, VIDIOC_S_TUNER, &tun2);
		if (err < 0)
			dprintk("VIDIOCSAUDIO / VIDIOC_S_TUNER: %ld\n", err);
	}
	err = 0;
done:
	return err;
}

static noinline long v4l1_compat_capture_frame(
					struct video_mmap *mm,
					struct file *file,
					v4l2_kioctl drv)
{
	long err;
	enum v4l2_buf_type      captype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	struct v4l2_buffer	buf;
	struct v4l2_format	*fmt;

	fmt = kzalloc(sizeof(*fmt), GFP_KERNEL);
	if (!fmt) {
		err = -ENOMEM;
		return err;
	}
	memset(&buf, 0, sizeof(buf));

	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	err = drv(file, VIDIOC_G_FMT, fmt);
	if (err < 0) {
		dprintk("VIDIOCMCAPTURE / VIDIOC_G_FMT: %ld\n", err);
		goto done;
	}
	if (mm->width   != fmt->fmt.pix.width  ||
	    mm->height  != fmt->fmt.pix.height ||
	    palette_to_pixelformat(mm->format) !=
	    fmt->fmt.pix.pixelformat) {
		/* New capture format...  */
		fmt->fmt.pix.width = mm->width;
		fmt->fmt.pix.height = mm->height;
		fmt->fmt.pix.pixelformat =
			palette_to_pixelformat(mm->format);
		fmt->fmt.pix.field = V4L2_FIELD_ANY;
		fmt->fmt.pix.bytesperline = 0;
		err = drv(file, VIDIOC_S_FMT, fmt);
		if (err < 0) {
			dprintk("VIDIOCMCAPTURE / VIDIOC_S_FMT: %ld\n", err);
			goto done;
		}
	}
	buf.index = mm->frame;
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	err = drv(file, VIDIOC_QUERYBUF, &buf);
	if (err < 0) {
		dprintk("VIDIOCMCAPTURE / VIDIOC_QUERYBUF: %ld\n", err);
		goto done;
	}
	err = drv(file, VIDIOC_QBUF, &buf);
	if (err < 0) {
		dprintk("VIDIOCMCAPTURE / VIDIOC_QBUF: %ld\n", err);
		goto done;
	}
	err = drv(file, VIDIOC_STREAMON, &captype);
	if (err < 0)
		dprintk("VIDIOCMCAPTURE / VIDIOC_STREAMON: %ld\n", err);
done:
	kfree(fmt);
	return err;
}

static noinline long v4l1_compat_sync(
				int *i,
				struct file *file,
				v4l2_kioctl drv)
{
	long err;
	enum v4l2_buf_type captype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	struct v4l2_buffer buf;
	struct poll_wqueues *pwq;

	memset(&buf, 0, sizeof(buf));
	buf.index = *i;
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	err = drv(file, VIDIOC_QUERYBUF, &buf);
	if (err < 0) {
		/*  No such buffer */
		dprintk("VIDIOCSYNC / VIDIOC_QUERYBUF: %ld\n", err);
		goto done;
	}
	if (!(buf.flags & V4L2_BUF_FLAG_MAPPED)) {
		/* Buffer is not mapped  */
		err = -EINVAL;
		goto done;
	}

	/* make sure capture actually runs so we don't block forever */
	err = drv(file, VIDIOC_STREAMON, &captype);
	if (err < 0) {
		dprintk("VIDIOCSYNC / VIDIOC_STREAMON: %ld\n", err);
		goto done;
	}

	pwq = kmalloc(sizeof(*pwq), GFP_KERNEL);
	/*  Loop as long as the buffer is queued, but not done  */
	while ((buf.flags & (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))
						== V4L2_BUF_FLAG_QUEUED) {
		err = poll_one(file, pwq);
		if (err < 0 ||	/* error or sleep was interrupted  */
		    err == 0)	/* timeout? Shouldn't occur.  */
			break;
		err = drv(file, VIDIOC_QUERYBUF, &buf);
		if (err < 0)
			dprintk("VIDIOCSYNC / VIDIOC_QUERYBUF: %ld\n", err);
	}
	kfree(pwq);
	if (!(buf.flags & V4L2_BUF_FLAG_DONE)) /* not done */
		goto done;
	do {
		err = drv(file, VIDIOC_DQBUF, &buf);
		if (err < 0)
			dprintk("VIDIOCSYNC / VIDIOC_DQBUF: %ld\n", err);
	} while (err == 0 && buf.index != *i);
done:
	return err;
}

static noinline long v4l1_compat_get_vbi_format(
				struct vbi_format *fmt,
				struct file *file,
				v4l2_kioctl drv)
{
	long err;
	struct v4l2_format *fmt2;

	fmt2 = kzalloc(sizeof(*fmt2), GFP_KERNEL);
	if (!fmt2) {
		err = -ENOMEM;
		return err;
	}
	fmt2->type = V4L2_BUF_TYPE_VBI_CAPTURE;

	err = drv(file, VIDIOC_G_FMT, fmt2);
	if (err < 0) {
		dprintk("VIDIOCGVBIFMT / VIDIOC_G_FMT: %ld\n", err);
		goto done;
	}
	if (fmt2->fmt.vbi.sample_format != V4L2_PIX_FMT_GREY) {
		err = -EINVAL;
		goto done;
	}
	memset(fmt, 0, sizeof(*fmt));
	fmt->samples_per_line = fmt2->fmt.vbi.samples_per_line;
	fmt->sampling_rate    = fmt2->fmt.vbi.sampling_rate;
	fmt->sample_format    = VIDEO_PALETTE_RAW;
	fmt->start[0]         = fmt2->fmt.vbi.start[0];
	fmt->count[0]         = fmt2->fmt.vbi.count[0];
	fmt->start[1]         = fmt2->fmt.vbi.start[1];
	fmt->count[1]         = fmt2->fmt.vbi.count[1];
	fmt->flags            = fmt2->fmt.vbi.flags & 0x03;
done:
	kfree(fmt2);
	return err;
}

static noinline long v4l1_compat_set_vbi_format(
				struct vbi_format *fmt,
				struct file *file,
				v4l2_kioctl drv)
{
	long err;
	struct v4l2_format	*fmt2 = NULL;

	if (VIDEO_PALETTE_RAW != fmt->sample_format) {
		err = -EINVAL;
		return err;
	}

	fmt2 = kzalloc(sizeof(*fmt2), GFP_KERNEL);
	if (!fmt2) {
		err = -ENOMEM;
		return err;
	}
	fmt2->type = V4L2_BUF_TYPE_VBI_CAPTURE;
	fmt2->fmt.vbi.samples_per_line = fmt->samples_per_line;
	fmt2->fmt.vbi.sampling_rate    = fmt->sampling_rate;
	fmt2->fmt.vbi.sample_format    = V4L2_PIX_FMT_GREY;
	fmt2->fmt.vbi.start[0]         = fmt->start[0];
	fmt2->fmt.vbi.count[0]         = fmt->count[0];
	fmt2->fmt.vbi.start[1]         = fmt->start[1];
	fmt2->fmt.vbi.count[1]         = fmt->count[1];
	fmt2->fmt.vbi.flags            = fmt->flags;
	err = drv(file, VIDIOC_TRY_FMT, fmt2);
	if (err < 0) {
		dprintk("VIDIOCSVBIFMT / VIDIOC_TRY_FMT: %ld\n", err);
		goto done;
	}

	if (fmt2->fmt.vbi.samples_per_line != fmt->samples_per_line ||
	    fmt2->fmt.vbi.sampling_rate    != fmt->sampling_rate    ||
	    fmt2->fmt.vbi.sample_format    != V4L2_PIX_FMT_GREY     ||
	    fmt2->fmt.vbi.start[0]         != fmt->start[0]         ||
	    fmt2->fmt.vbi.count[0]         != fmt->count[0]         ||
	    fmt2->fmt.vbi.start[1]         != fmt->start[1]         ||
	    fmt2->fmt.vbi.count[1]         != fmt->count[1]         ||
	    fmt2->fmt.vbi.flags            != fmt->flags) {
		err = -EINVAL;
		goto done;
	}
	err = drv(file, VIDIOC_S_FMT, fmt2);
	if (err < 0)
		dprintk("VIDIOCSVBIFMT / VIDIOC_S_FMT: %ld\n", err);
done:
	kfree(fmt2);
	return err;
}

/*
 *	This function is exported.
 */
long
v4l_compat_translate_ioctl(struct file		*file,
			   int			cmd,
			   void			*arg,
			   v4l2_kioctl          drv)
{
	long err;

	switch (cmd) {
	case VIDIOCGCAP:	/* capability */
		err = v4l1_compat_get_capabilities(arg, file, drv);
		break;
	case VIDIOCGFBUF: /*  get frame buffer  */
		err = v4l1_compat_get_frame_buffer(arg, file, drv);
		break;
	case VIDIOCSFBUF: /*  set frame buffer  */
		err = v4l1_compat_set_frame_buffer(arg, file, drv);
		break;
	case VIDIOCGWIN: /*  get window or capture dimensions  */
		err = v4l1_compat_get_win_cap_dimensions(arg, file, drv);
		break;
	case VIDIOCSWIN: /*  set window and/or capture dimensions  */
		err = v4l1_compat_set_win_cap_dimensions(arg, file, drv);
		break;
	case VIDIOCCAPTURE: /*  turn on/off preview  */
		err = v4l1_compat_turn_preview_on_off(arg, file, drv);
		break;
	case VIDIOCGCHAN: /*  get input information  */
		err = v4l1_compat_get_input_info(arg, file, drv);
		break;
	case VIDIOCSCHAN: /*  set input  */
		err = v4l1_compat_set_input(arg, file, drv);
		break;
	case VIDIOCGPICT: /*  get tone controls & partial capture format  */
		err = v4l1_compat_get_picture(arg, file, drv);
		break;
	case VIDIOCSPICT: /*  set tone controls & partial capture format  */
		err = v4l1_compat_set_picture(arg, file, drv);
		break;
	case VIDIOCGTUNER: /*  get tuner information  */
		err = v4l1_compat_get_tuner(arg, file, drv);
		break;
	case VIDIOCSTUNER: /*  select a tuner input  */
		err = v4l1_compat_select_tuner(arg, file, drv);
		break;
	case VIDIOCGFREQ: /*  get frequency  */
		err = v4l1_compat_get_frequency(arg, file, drv);
		break;
	case VIDIOCSFREQ: /*  set frequency  */
		err = v4l1_compat_set_frequency(arg, file, drv);
		break;
	case VIDIOCGAUDIO: /*  get audio properties/controls  */
		err = v4l1_compat_get_audio(arg, file, drv);
		break;
	case VIDIOCSAUDIO: /*  set audio controls  */
		err = v4l1_compat_set_audio(arg, file, drv);
		break;
	case VIDIOCMCAPTURE: /*  capture a frame  */
		err = v4l1_compat_capture_frame(arg, file, drv);
		break;
	case VIDIOCSYNC: /*  wait for a frame  */
		err = v4l1_compat_sync(arg, file, drv);
		break;
	case VIDIOCGVBIFMT: /* query VBI data capture format */
		err = v4l1_compat_get_vbi_format(arg, file, drv);
		break;
	case VIDIOCSVBIFMT:
		err = v4l1_compat_set_vbi_format(arg, file, drv);
		break;
	default:
		err = -ENOIOCTLCMD;
		break;
	}

	return err;
}
EXPORT_SYMBOL(v4l_compat_translate_ioctl);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
