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
#include <linux/moduleparam.h>
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

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/pgtable.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

static unsigned int debug  = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug,"enable debug messages");
MODULE_AUTHOR("Bill Dirks");
MODULE_DESCRIPTION("v4l(1) compatibility layer for v4l2 drivers.");
MODULE_LICENSE("GPL");

#define dprintk(fmt, arg...)	if (debug) \
	printk(KERN_DEBUG "v4l1-compat: " fmt , ## arg)

/*
 *	I O C T L   T R A N S L A T I O N
 *
 *	From here on down is the code for translating the numerous
 *	ioctl commands from the old API to the new API.
 */

static int
get_v4l_control(struct inode            *inode,
		struct file             *file,
		int			cid,
		v4l2_kioctl             drv)
{
	struct v4l2_queryctrl	qctrl2;
	struct v4l2_control	ctrl2;
	int			err;

	qctrl2.id = cid;
	err = drv(inode, file, VIDIOC_QUERYCTRL, &qctrl2);
	if (err < 0)
		dprintk("VIDIOC_QUERYCTRL: %d\n",err);
	if (err == 0 &&
	    !(qctrl2.flags & V4L2_CTRL_FLAG_DISABLED))
	{
		ctrl2.id = qctrl2.id;
		err = drv(inode, file, VIDIOC_G_CTRL, &ctrl2);
		if (err < 0) {
			dprintk("VIDIOC_G_CTRL: %d\n",err);
			return 0;
		}
		return ((ctrl2.value - qctrl2.minimum) * 65535
			 + (qctrl2.maximum - qctrl2.minimum) / 2)
			/ (qctrl2.maximum - qctrl2.minimum);
	}
	return 0;
}

static int
set_v4l_control(struct inode            *inode,
		struct file             *file,
		int			cid,
		int			value,
		v4l2_kioctl             drv)
{
	struct v4l2_queryctrl	qctrl2;
	struct v4l2_control	ctrl2;
	int			err;

	qctrl2.id = cid;
	err = drv(inode, file, VIDIOC_QUERYCTRL, &qctrl2);
	if (err < 0)
		dprintk("VIDIOC_QUERYCTRL: %d\n",err);
	if (err == 0 &&
	    !(qctrl2.flags & V4L2_CTRL_FLAG_DISABLED) &&
	    !(qctrl2.flags & V4L2_CTRL_FLAG_GRABBED))
	{
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
		err = drv(inode, file, VIDIOC_S_CTRL, &ctrl2);
		if (err < 0)
			dprintk("VIDIOC_S_CTRL: %d\n",err);
	}
	return 0;
}

/* ----------------------------------------------------------------- */

const static unsigned int palette2pixelformat[] = {
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

static unsigned int __attribute_pure__
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
	switch (pixelformat)
	{
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

static int poll_one(struct file *file)
{
	int retval = 1;
	poll_table *table;
	struct poll_wqueues pwq;

	poll_initwait(&pwq);
	table = &pwq.pt;
	for (;;) {
		int mask;
		set_current_state(TASK_INTERRUPTIBLE);
		mask = file->f_op->poll(file, table);
		if (mask & POLLIN)
			break;
		table = NULL;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}
	set_current_state(TASK_RUNNING);
	poll_freewait(&pwq);
	return retval;
}

static int count_inputs(struct inode         *inode,
			struct file          *file,
			v4l2_kioctl          drv)
{
	struct v4l2_input input2;
	int i;

	for (i = 0;; i++) {
		memset(&input2,0,sizeof(input2));
		input2.index = i;
		if (0 != drv(inode,file,VIDIOC_ENUMINPUT, &input2))
			break;
	}
	return i;
}

static int check_size(struct inode         *inode,
		      struct file          *file,
		      v4l2_kioctl          drv,
		      int *maxw, int *maxh)
{
	struct v4l2_fmtdesc desc2;
	struct v4l2_format  fmt2;

	memset(&desc2,0,sizeof(desc2));
	memset(&fmt2,0,sizeof(fmt2));

	desc2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (0 != drv(inode,file,VIDIOC_ENUM_FMT, &desc2))
		goto done;

	fmt2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt2.fmt.pix.width       = 10000;
	fmt2.fmt.pix.height      = 10000;
	fmt2.fmt.pix.pixelformat = desc2.pixelformat;
	if (0 != drv(inode,file,VIDIOC_TRY_FMT, &fmt2))
		goto done;

	*maxw = fmt2.fmt.pix.width;
	*maxh = fmt2.fmt.pix.height;

 done:
	return 0;
}

/* ----------------------------------------------------------------- */

/*
 *	This function is exported.
 */
int
v4l_compat_translate_ioctl(struct inode         *inode,
			   struct file		*file,
			   int			cmd,
			   void			*arg,
			   v4l2_kioctl          drv)
{
	struct v4l2_capability  *cap2 = NULL;
	struct v4l2_format	*fmt2 = NULL;
	enum v4l2_buf_type      captype = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	struct v4l2_framebuffer fbuf2;
	struct v4l2_input	input2;
	struct v4l2_tuner	tun2;
	struct v4l2_standard	std2;
	struct v4l2_frequency   freq2;
	struct v4l2_audio	aud2;
	struct v4l2_queryctrl	qctrl2;
	struct v4l2_buffer	buf2;
	v4l2_std_id    		sid;
	int i, err = 0;

	switch (cmd) {
	case VIDIOCGCAP:	/* capability */
	{
		struct video_capability *cap = arg;

		cap2 = kzalloc(sizeof(*cap2),GFP_KERNEL);
		memset(cap, 0, sizeof(*cap));
		memset(&fbuf2, 0, sizeof(fbuf2));

		err = drv(inode, file, VIDIOC_QUERYCAP, cap2);
		if (err < 0) {
			dprintk("VIDIOCGCAP / VIDIOC_QUERYCAP: %d\n",err);
			break;
		}
		if (cap2->capabilities & V4L2_CAP_VIDEO_OVERLAY) {
			err = drv(inode, file, VIDIOC_G_FBUF, &fbuf2);
			if (err < 0) {
				dprintk("VIDIOCGCAP / VIDIOC_G_FBUF: %d\n",err);
				memset(&fbuf2, 0, sizeof(fbuf2));
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
		if (fbuf2.capability & V4L2_FBUF_CAP_LIST_CLIPPING)
			cap->type |= VID_TYPE_CLIPPING;

		cap->channels  = count_inputs(inode,file,drv);
		check_size(inode,file,drv,
			   &cap->maxwidth,&cap->maxheight);
		cap->audios    =  0; /* FIXME */
		cap->minwidth  = 48; /* FIXME */
		cap->minheight = 32; /* FIXME */
		break;
	}
	case VIDIOCGFBUF: /*  get frame buffer  */
	{
		struct video_buffer	*buffer = arg;

		memset(buffer, 0, sizeof(*buffer));
		memset(&fbuf2, 0, sizeof(fbuf2));

		err = drv(inode, file, VIDIOC_G_FBUF, &fbuf2);
		if (err < 0) {
			dprintk("VIDIOCGFBUF / VIDIOC_G_FBUF: %d\n",err);
			break;
		}
		buffer->base   = fbuf2.base;
		buffer->height = fbuf2.fmt.height;
		buffer->width  = fbuf2.fmt.width;

		switch (fbuf2.fmt.pixelformat) {
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
		if (fbuf2.fmt.bytesperline) {
			buffer->bytesperline = fbuf2.fmt.bytesperline;
			if (!buffer->depth && buffer->width)
				buffer->depth   = ((fbuf2.fmt.bytesperline<<3)
						  + (buffer->width-1) )
						  /buffer->width;
		} else {
			buffer->bytesperline =
				(buffer->width * buffer->depth + 7) & 7;
			buffer->bytesperline >>= 3;
		}
		break;
	}
	case VIDIOCSFBUF: /*  set frame buffer  */
	{
		struct video_buffer	*buffer = arg;

		memset(&fbuf2, 0, sizeof(fbuf2));
		fbuf2.base       = buffer->base;
		fbuf2.fmt.height = buffer->height;
		fbuf2.fmt.width  = buffer->width;
		switch (buffer->depth) {
		case 8:
			fbuf2.fmt.pixelformat = V4L2_PIX_FMT_RGB332;
			break;
		case 15:
			fbuf2.fmt.pixelformat = V4L2_PIX_FMT_RGB555;
			break;
		case 16:
			fbuf2.fmt.pixelformat = V4L2_PIX_FMT_RGB565;
			break;
		case 24:
			fbuf2.fmt.pixelformat = V4L2_PIX_FMT_BGR24;
			break;
		case 32:
			fbuf2.fmt.pixelformat = V4L2_PIX_FMT_BGR32;
			break;
		}
		fbuf2.fmt.bytesperline = buffer->bytesperline;
		err = drv(inode, file, VIDIOC_S_FBUF, &fbuf2);
		if (err < 0)
			dprintk("VIDIOCSFBUF / VIDIOC_S_FBUF: %d\n",err);
		break;
	}
	case VIDIOCGWIN: /*  get window or capture dimensions  */
	{
		struct video_window	*win = arg;

		fmt2 = kzalloc(sizeof(*fmt2),GFP_KERNEL);
		memset(win,0,sizeof(*win));

		fmt2->type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
		err = drv(inode, file, VIDIOC_G_FMT, fmt2);
		if (err < 0)
			dprintk("VIDIOCGWIN / VIDIOC_G_WIN: %d\n",err);
		if (err == 0) {
			win->x         = fmt2->fmt.win.w.left;
			win->y         = fmt2->fmt.win.w.top;
			win->width     = fmt2->fmt.win.w.width;
			win->height    = fmt2->fmt.win.w.height;
			win->chromakey = fmt2->fmt.win.chromakey;
			win->clips     = NULL;
			win->clipcount = 0;
			break;
		}

		fmt2->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_G_FMT, fmt2);
		if (err < 0) {
			dprintk("VIDIOCGWIN / VIDIOC_G_FMT: %d\n",err);
			break;
		}
		win->x         = 0;
		win->y         = 0;
		win->width     = fmt2->fmt.pix.width;
		win->height    = fmt2->fmt.pix.height;
		win->chromakey = 0;
		win->clips     = NULL;
		win->clipcount = 0;
		break;
	}
	case VIDIOCSWIN: /*  set window and/or capture dimensions  */
	{
		struct video_window	*win = arg;
		int err1,err2;

		fmt2 = kzalloc(sizeof(*fmt2),GFP_KERNEL);
		fmt2->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		drv(inode, file, VIDIOC_STREAMOFF, &fmt2->type);
		err1 = drv(inode, file, VIDIOC_G_FMT, fmt2);
		if (err1 < 0)
			dprintk("VIDIOCSWIN / VIDIOC_G_FMT: %d\n",err);
		if (err1 == 0) {
			fmt2->fmt.pix.width  = win->width;
			fmt2->fmt.pix.height = win->height;
			fmt2->fmt.pix.field  = V4L2_FIELD_ANY;
			fmt2->fmt.pix.bytesperline = 0;
			err = drv(inode, file, VIDIOC_S_FMT, fmt2);
			if (err < 0)
				dprintk("VIDIOCSWIN / VIDIOC_S_FMT #1: %d\n",
					err);
			win->width  = fmt2->fmt.pix.width;
			win->height = fmt2->fmt.pix.height;
		}

		memset(fmt2,0,sizeof(*fmt2));
		fmt2->type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
		fmt2->fmt.win.w.left    = win->x;
		fmt2->fmt.win.w.top     = win->y;
		fmt2->fmt.win.w.width   = win->width;
		fmt2->fmt.win.w.height  = win->height;
		fmt2->fmt.win.chromakey = win->chromakey;
		fmt2->fmt.win.clips     = (void __user *)win->clips;
		fmt2->fmt.win.clipcount = win->clipcount;
		err2 = drv(inode, file, VIDIOC_S_FMT, fmt2);
		if (err2 < 0)
			dprintk("VIDIOCSWIN / VIDIOC_S_FMT #2: %d\n",err);

		if (err1 != 0 && err2 != 0)
			err = err1;
		break;
	}
	case VIDIOCCAPTURE: /*  turn on/off preview  */
	{
		int *on = arg;

		if (0 == *on) {
			/* dirty hack time.  But v4l1 has no STREAMOFF
			 * equivalent in the API, and this one at
			 * least comes close ... */
			drv(inode, file, VIDIOC_STREAMOFF, &captype);
		}
		err = drv(inode, file, VIDIOC_OVERLAY, arg);
		if (err < 0)
			dprintk("VIDIOCCAPTURE / VIDIOC_PREVIEW: %d\n",err);
		break;
	}
	case VIDIOCGCHAN: /*  get input information  */
	{
		struct video_channel	*chan = arg;

		memset(&input2,0,sizeof(input2));
		input2.index = chan->channel;
		err = drv(inode, file, VIDIOC_ENUMINPUT, &input2);
		if (err < 0) {
			dprintk("VIDIOCGCHAN / VIDIOC_ENUMINPUT: "
				"channel=%d err=%d\n",chan->channel,err);
			break;
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
		err = drv(inode, file, VIDIOC_G_STD, &sid);
		if (err < 0)
			dprintk("VIDIOCGCHAN / VIDIOC_G_STD: %d\n",err);
		if (err == 0) {
			if (sid & V4L2_STD_PAL)
				chan->norm = VIDEO_MODE_PAL;
			if (sid & V4L2_STD_NTSC)
				chan->norm = VIDEO_MODE_NTSC;
			if (sid & V4L2_STD_SECAM)
				chan->norm = VIDEO_MODE_SECAM;
		}
		break;
	}
	case VIDIOCSCHAN: /*  set input  */
	{
		struct video_channel *chan = arg;

		sid = 0;
		err = drv(inode, file, VIDIOC_S_INPUT, &chan->channel);
		if (err < 0)
			dprintk("VIDIOCSCHAN / VIDIOC_S_INPUT: %d\n",err);
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
		}
		if (0 != sid) {
			err = drv(inode, file, VIDIOC_S_STD, &sid);
			if (err < 0)
				dprintk("VIDIOCSCHAN / VIDIOC_S_STD: %d\n",err);
		}
		break;
	}
	case VIDIOCGPICT: /*  get tone controls & partial capture format  */
	{
		struct video_picture	*pict = arg;

		pict->brightness = get_v4l_control(inode, file,
						   V4L2_CID_BRIGHTNESS,drv);
		pict->hue = get_v4l_control(inode, file,
					    V4L2_CID_HUE, drv);
		pict->contrast = get_v4l_control(inode, file,
						 V4L2_CID_CONTRAST, drv);
		pict->colour = get_v4l_control(inode, file,
					       V4L2_CID_SATURATION, drv);
		pict->whiteness = get_v4l_control(inode, file,
						  V4L2_CID_WHITENESS, drv);

		fmt2 = kzalloc(sizeof(*fmt2),GFP_KERNEL);
		fmt2->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_G_FMT, fmt2);
		if (err < 0) {
			dprintk("VIDIOCGPICT / VIDIOC_G_FMT: %d\n",err);
			break;
		}

		pict->depth   = ((fmt2->fmt.pix.bytesperline<<3)
				 + (fmt2->fmt.pix.width-1) )
				 /fmt2->fmt.pix.width;
		pict->palette = pixelformat_to_palette(
			fmt2->fmt.pix.pixelformat);
		break;
	}
	case VIDIOCSPICT: /*  set tone controls & partial capture format  */
	{
		struct video_picture	*pict = arg;
		int mem_err = 0, ovl_err = 0;

		memset(&fbuf2, 0, sizeof(fbuf2));

		set_v4l_control(inode, file,
				V4L2_CID_BRIGHTNESS, pict->brightness, drv);
		set_v4l_control(inode, file,
				V4L2_CID_HUE, pict->hue, drv);
		set_v4l_control(inode, file,
				V4L2_CID_CONTRAST, pict->contrast, drv);
		set_v4l_control(inode, file,
				V4L2_CID_SATURATION, pict->colour, drv);
		set_v4l_control(inode, file,
				V4L2_CID_WHITENESS, pict->whiteness, drv);
		/*
		 * V4L1 uses this ioctl to set both memory capture and overlay
		 * pixel format, while V4L2 has two different ioctls for this.
		 * Some cards may not support one or the other, and may support
		 * different pixel formats for memory vs overlay.
		 */

		fmt2 = kzalloc(sizeof(*fmt2),GFP_KERNEL);
		fmt2->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_G_FMT, fmt2);
		/* If VIDIOC_G_FMT failed, then the driver likely doesn't
		   support memory capture.  Trying to set the memory capture
		   parameters would be pointless.  */
		if (err < 0) {
			dprintk("VIDIOCSPICT / VIDIOC_G_FMT: %d\n",err);
			mem_err = -1000;  /* didn't even try */
		} else if (fmt2->fmt.pix.pixelformat !=
			 palette_to_pixelformat(pict->palette)) {
			fmt2->fmt.pix.pixelformat = palette_to_pixelformat(
				pict->palette);
			mem_err = drv(inode, file, VIDIOC_S_FMT, fmt2);
			if (mem_err < 0)
				dprintk("VIDIOCSPICT / VIDIOC_S_FMT: %d\n",
					mem_err);
		}

		err = drv(inode, file, VIDIOC_G_FBUF, &fbuf2);
		/* If VIDIOC_G_FBUF failed, then the driver likely doesn't
		   support overlay.  Trying to set the overlay parameters
		   would be quite pointless.  */
		if (err < 0) {
			dprintk("VIDIOCSPICT / VIDIOC_G_FBUF: %d\n",err);
			ovl_err = -1000;  /* didn't even try */
		} else if (fbuf2.fmt.pixelformat !=
			 palette_to_pixelformat(pict->palette)) {
			fbuf2.fmt.pixelformat = palette_to_pixelformat(
				pict->palette);
			ovl_err = drv(inode, file, VIDIOC_S_FBUF, &fbuf2);
			if (ovl_err < 0)
				dprintk("VIDIOCSPICT / VIDIOC_S_FBUF: %d\n",
					ovl_err);
		}
		if (ovl_err < 0 && mem_err < 0)
			/* ioctl failed, couldn't set either parameter */
			if (mem_err != -1000) {
			    err = mem_err;
			} else if (ovl_err == -EPERM) {
			    err = 0;
			} else {
			    err = ovl_err;
			}
		else
			err = 0;
		break;
	}
	case VIDIOCGTUNER: /*  get tuner information  */
	{
		struct video_tuner	*tun = arg;

		memset(&tun2,0,sizeof(tun2));
		err = drv(inode, file, VIDIOC_G_TUNER, &tun2);
		if (err < 0) {
			dprintk("VIDIOCGTUNER / VIDIOC_G_TUNER: %d\n",err);
			break;
		}
		memcpy(tun->name, tun2.name,
		       min(sizeof(tun->name), sizeof(tun2.name)));
		tun->name[sizeof(tun->name) - 1] = 0;
		tun->rangelow = tun2.rangelow;
		tun->rangehigh = tun2.rangehigh;
		tun->flags = 0;
		tun->mode = VIDEO_MODE_AUTO;

		for (i = 0; i < 64; i++) {
			memset(&std2,0,sizeof(std2));
			std2.index = i;
			if (0 != drv(inode, file, VIDIOC_ENUMSTD, &std2))
				break;
			if (std2.id & V4L2_STD_PAL)
				tun->flags |= VIDEO_TUNER_PAL;
			if (std2.id & V4L2_STD_NTSC)
				tun->flags |= VIDEO_TUNER_NTSC;
			if (std2.id & V4L2_STD_SECAM)
				tun->flags |= VIDEO_TUNER_SECAM;
		}

		err = drv(inode, file, VIDIOC_G_STD, &sid);
		if (err < 0)
			dprintk("VIDIOCGTUNER / VIDIOC_G_STD: %d\n",err);
		if (err == 0) {
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
		break;
	}
	case VIDIOCSTUNER: /*  select a tuner input  */
	{
		struct video_tuner	*tun = arg;
		struct v4l2_tuner	t;
		memset(&t,0,sizeof(t));

		t.index=tun->tuner;

		err = drv(inode, file, VIDIOC_S_INPUT, &t);
		if (err < 0)
			dprintk("VIDIOCSTUNER / VIDIOC_S_INPUT: %d\n",err);

		break;
	}
	case VIDIOCGFREQ: /*  get frequency  */
	{
		unsigned long *freq = arg;
		memset(&freq2,0,sizeof(freq2));

		freq2.tuner = 0;
		err = drv(inode, file, VIDIOC_G_FREQUENCY, &freq2);
		if (err < 0)
			dprintk("VIDIOCGFREQ / VIDIOC_G_FREQUENCY: %d\n",err);
		if (0 == err)
			*freq = freq2.frequency;
		break;
	}
	case VIDIOCSFREQ: /*  set frequency  */
	{
		unsigned long *freq = arg;
		memset(&freq2,0,sizeof(freq2));

		drv(inode, file, VIDIOC_G_FREQUENCY, &freq2);
		freq2.frequency = *freq;
		err = drv(inode, file, VIDIOC_S_FREQUENCY, &freq2);
		if (err < 0)
			dprintk("VIDIOCSFREQ / VIDIOC_S_FREQUENCY: %d\n",err);
		break;
	}
	case VIDIOCGAUDIO: /*  get audio properties/controls  */
	{
		struct video_audio	*aud = arg;
		memset(&aud2,0,sizeof(aud2));

		err = drv(inode, file, VIDIOC_G_AUDIO, &aud2);
		if (err < 0) {
			dprintk("VIDIOCGAUDIO / VIDIOC_G_AUDIO: %d\n",err);
			break;
		}
		memcpy(aud->name, aud2.name,
		       min(sizeof(aud->name), sizeof(aud2.name)));
		aud->name[sizeof(aud->name) - 1] = 0;
		aud->audio = aud2.index;
		aud->flags = 0;
		i = get_v4l_control(inode, file, V4L2_CID_AUDIO_VOLUME, drv);
		if (i >= 0) {
			aud->volume = i;
			aud->flags |= VIDEO_AUDIO_VOLUME;
		}
		i = get_v4l_control(inode, file, V4L2_CID_AUDIO_BASS, drv);
		if (i >= 0) {
			aud->bass = i;
			aud->flags |= VIDEO_AUDIO_BASS;
		}
		i = get_v4l_control(inode, file, V4L2_CID_AUDIO_TREBLE, drv);
		if (i >= 0) {
			aud->treble = i;
			aud->flags |= VIDEO_AUDIO_TREBLE;
		}
		i = get_v4l_control(inode, file, V4L2_CID_AUDIO_BALANCE, drv);
		if (i >= 0) {
			aud->balance = i;
			aud->flags |= VIDEO_AUDIO_BALANCE;
		}
		i = get_v4l_control(inode, file, V4L2_CID_AUDIO_MUTE, drv);
		if (i >= 0) {
			if (i)
				aud->flags |= VIDEO_AUDIO_MUTE;
			aud->flags |= VIDEO_AUDIO_MUTABLE;
		}
		aud->step = 1;
		qctrl2.id = V4L2_CID_AUDIO_VOLUME;
		if (drv(inode, file, VIDIOC_QUERYCTRL, &qctrl2) == 0 &&
		    !(qctrl2.flags & V4L2_CTRL_FLAG_DISABLED))
			aud->step = qctrl2.step;
		aud->mode = 0;

		memset(&tun2,0,sizeof(tun2));
		err = drv(inode, file, VIDIOC_G_TUNER, &tun2);
		if (err < 0) {
			dprintk("VIDIOCGAUDIO / VIDIOC_G_TUNER: %d\n",err);
			err = 0;
			break;
		}

		if (tun2.rxsubchans & V4L2_TUNER_SUB_LANG2)
			aud->mode = VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
		else if (tun2.rxsubchans & V4L2_TUNER_SUB_STEREO)
			aud->mode = VIDEO_SOUND_STEREO;
		else if (tun2.rxsubchans & V4L2_TUNER_SUB_MONO)
			aud->mode = VIDEO_SOUND_MONO;
		break;
	}
	case VIDIOCSAUDIO: /*  set audio controls  */
	{
		struct video_audio	*aud = arg;

		memset(&aud2,0,sizeof(aud2));
		memset(&tun2,0,sizeof(tun2));

		aud2.index = aud->audio;
		err = drv(inode, file, VIDIOC_S_AUDIO, &aud2);
		if (err < 0) {
			dprintk("VIDIOCSAUDIO / VIDIOC_S_AUDIO: %d\n",err);
			break;
		}

		set_v4l_control(inode, file, V4L2_CID_AUDIO_VOLUME,
				aud->volume, drv);
		set_v4l_control(inode, file, V4L2_CID_AUDIO_BASS,
				aud->bass, drv);
		set_v4l_control(inode, file, V4L2_CID_AUDIO_TREBLE,
				aud->treble, drv);
		set_v4l_control(inode, file, V4L2_CID_AUDIO_BALANCE,
				aud->balance, drv);
		set_v4l_control(inode, file, V4L2_CID_AUDIO_MUTE,
				!!(aud->flags & VIDEO_AUDIO_MUTE), drv);

		err = drv(inode, file, VIDIOC_G_TUNER, &tun2);
		if (err < 0)
			dprintk("VIDIOCSAUDIO / VIDIOC_G_TUNER: %d\n",err);
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
			err = drv(inode, file, VIDIOC_S_TUNER, &tun2);
			if (err < 0)
				dprintk("VIDIOCSAUDIO / VIDIOC_S_TUNER: %d\n",err);
		}
		err = 0;
		break;
	}
	case VIDIOCMCAPTURE: /*  capture a frame  */
	{
		struct video_mmap	*mm = arg;

		fmt2 = kzalloc(sizeof(*fmt2),GFP_KERNEL);
		memset(&buf2,0,sizeof(buf2));

		fmt2->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_G_FMT, fmt2);
		if (err < 0) {
			dprintk("VIDIOCMCAPTURE / VIDIOC_G_FMT: %d\n",err);
			break;
		}
		if (mm->width   != fmt2->fmt.pix.width  ||
		    mm->height  != fmt2->fmt.pix.height ||
		    palette_to_pixelformat(mm->format) !=
		    fmt2->fmt.pix.pixelformat)
		{/* New capture format...  */
			fmt2->fmt.pix.width = mm->width;
			fmt2->fmt.pix.height = mm->height;
			fmt2->fmt.pix.pixelformat =
				palette_to_pixelformat(mm->format);
			fmt2->fmt.pix.field = V4L2_FIELD_ANY;
			fmt2->fmt.pix.bytesperline = 0;
			err = drv(inode, file, VIDIOC_S_FMT, fmt2);
			if (err < 0) {
				dprintk("VIDIOCMCAPTURE / VIDIOC_S_FMT: %d\n",err);
				break;
			}
		}
		buf2.index = mm->frame;
		buf2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_QUERYBUF, &buf2);
		if (err < 0) {
			dprintk("VIDIOCMCAPTURE / VIDIOC_QUERYBUF: %d\n",err);
			break;
		}
		err = drv(inode, file, VIDIOC_QBUF, &buf2);
		if (err < 0) {
			dprintk("VIDIOCMCAPTURE / VIDIOC_QBUF: %d\n",err);
			break;
		}
		err = drv(inode, file, VIDIOC_STREAMON, &captype);
		if (err < 0)
			dprintk("VIDIOCMCAPTURE / VIDIOC_STREAMON: %d\n",err);
		break;
	}
	case VIDIOCSYNC: /*  wait for a frame  */
	{
		int			*i = arg;

		memset(&buf2,0,sizeof(buf2));
		buf2.index = *i;
		buf2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		err = drv(inode, file, VIDIOC_QUERYBUF, &buf2);
		if (err < 0) {
			/*  No such buffer */
			dprintk("VIDIOCSYNC / VIDIOC_QUERYBUF: %d\n",err);
			break;
		}
		if (!(buf2.flags & V4L2_BUF_FLAG_MAPPED)) {
			/* Buffer is not mapped  */
			err = -EINVAL;
			break;
		}

		/* make sure capture actually runs so we don't block forever */
		err = drv(inode, file, VIDIOC_STREAMON, &captype);
		if (err < 0) {
			dprintk("VIDIOCSYNC / VIDIOC_STREAMON: %d\n",err);
			break;
		}

		/*  Loop as long as the buffer is queued, but not done  */
		while ((buf2.flags &
			(V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))
		       == V4L2_BUF_FLAG_QUEUED)
		{
			err = poll_one(file);
			if (err < 0 ||	/* error or sleep was interrupted  */
			    err == 0)	/* timeout? Shouldn't occur.  */
				break;
			err = drv(inode, file, VIDIOC_QUERYBUF, &buf2);
			if (err < 0)
				dprintk("VIDIOCSYNC / VIDIOC_QUERYBUF: %d\n",err);
		}
		if (!(buf2.flags & V4L2_BUF_FLAG_DONE)) /* not done */
			break;
		do {
			err = drv(inode, file, VIDIOC_DQBUF, &buf2);
			if (err < 0)
				dprintk("VIDIOCSYNC / VIDIOC_DQBUF: %d\n",err);
		} while (err == 0 && buf2.index != *i);
		break;
	}

	case VIDIOCGVBIFMT: /* query VBI data capture format */
	{
		struct vbi_format      *fmt = arg;

		fmt2 = kzalloc(sizeof(*fmt2),GFP_KERNEL);
		fmt2->type = V4L2_BUF_TYPE_VBI_CAPTURE;

		err = drv(inode, file, VIDIOC_G_FMT, fmt2);
		if (err < 0) {
			dprintk("VIDIOCGVBIFMT / VIDIOC_G_FMT: %d\n", err);
			break;
		}
		if (fmt2->fmt.vbi.sample_format != V4L2_PIX_FMT_GREY) {
			err = -EINVAL;
			break;
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
		break;
	}
	case VIDIOCSVBIFMT:
	{
		struct vbi_format      *fmt = arg;

		if (VIDEO_PALETTE_RAW != fmt->sample_format) {
			err = -EINVAL;
			break;
		}

		fmt2 = kzalloc(sizeof(*fmt2),GFP_KERNEL);

		fmt2->type = V4L2_BUF_TYPE_VBI_CAPTURE;
		fmt2->fmt.vbi.samples_per_line = fmt->samples_per_line;
		fmt2->fmt.vbi.sampling_rate    = fmt->sampling_rate;
		fmt2->fmt.vbi.sample_format    = V4L2_PIX_FMT_GREY;
		fmt2->fmt.vbi.start[0]         = fmt->start[0];
		fmt2->fmt.vbi.count[0]         = fmt->count[0];
		fmt2->fmt.vbi.start[1]         = fmt->start[1];
		fmt2->fmt.vbi.count[1]         = fmt->count[1];
		fmt2->fmt.vbi.flags            = fmt->flags;
		err = drv(inode, file, VIDIOC_TRY_FMT, fmt2);
		if (err < 0) {
			dprintk("VIDIOCSVBIFMT / VIDIOC_TRY_FMT: %d\n", err);
			break;
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
			break;
		}
		err = drv(inode, file, VIDIOC_S_FMT, fmt2);
		if (err < 0)
			dprintk("VIDIOCSVBIFMT / VIDIOC_S_FMT: %d\n", err);
		break;
	}

	default:
		err = -ENOIOCTLCMD;
		break;
	}

	kfree(cap2);
	kfree(fmt2);
	return err;
}

EXPORT_SYMBOL(v4l_compat_translate_ioctl);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
