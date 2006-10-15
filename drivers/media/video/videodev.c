/*
 * Video capture interface for Linux version 2
 *
 *	A generic video device interface for the LINUX operating system
 *	using a set of device structures/vectors for low level operations.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Authors:	Alan Cox, <alan@redhat.com> (version 1)
 *              Mauro Carvalho Chehab <mchehab@infradead.org> (version 2)
 *
 * Fixes:	20000516  Claudio Matsuoka <claudio@conectiva.com>
 *		- Added procfs support
 */

#define dbgarg(cmd, fmt, arg...) \
		if (vfd->debug & V4L2_DEBUG_IOCTL_ARG) {		\
			printk (KERN_DEBUG "%s: ",  vfd->name);		\
			v4l_printk_ioctl(cmd);				\
			printk (KERN_DEBUG "%s: " fmt, vfd->name, ## arg); \
		}

#define dbgarg2(fmt, arg...) \
		if (vfd->debug & V4L2_DEBUG_IOCTL_ARG)			\
			printk (KERN_DEBUG "%s: " fmt, vfd->name, ## arg);

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#define __OLD_VIDIOC_ /* To allow fixing old calls*/
#include <linux/videodev2.h>

#ifdef CONFIG_VIDEO_V4L1
#include <linux/videodev.h>
#endif
#include <media/v4l2-common.h>

#define VIDEO_NUM_DEVICES	256
#define VIDEO_NAME              "video4linux"

/*
 *	sysfs stuff
 */

static ssize_t show_name(struct class_device *cd, char *buf)
{
	struct video_device *vfd = container_of(cd, struct video_device,
								class_dev);
	return sprintf(buf,"%.*s\n",(int)sizeof(vfd->name),vfd->name);
}

static CLASS_DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

struct video_device *video_device_alloc(void)
{
	struct video_device *vfd;

	vfd = kzalloc(sizeof(*vfd),GFP_KERNEL);
	return vfd;
}

void video_device_release(struct video_device *vfd)
{
	kfree(vfd);
}

static void video_release(struct class_device *cd)
{
	struct video_device *vfd = container_of(cd, struct video_device,
								class_dev);

#if 1
	/* needed until all drivers are fixed */
	if (!vfd->release)
		return;
#endif
	vfd->release(vfd);
}

static struct class video_class = {
	.name    = VIDEO_NAME,
	.release = video_release,
};

/*
 *	Active devices
 */

static struct video_device *video_device[VIDEO_NUM_DEVICES];
static DEFINE_MUTEX(videodev_lock);

struct video_device* video_devdata(struct file *file)
{
	return video_device[iminor(file->f_dentry->d_inode)];
}

/*
 *	Open a video device - FIXME: Obsoleted
 */
static int video_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	int err = 0;
	struct video_device *vfl;
	const struct file_operations *old_fops;

	if(minor>=VIDEO_NUM_DEVICES)
		return -ENODEV;
	mutex_lock(&videodev_lock);
	vfl=video_device[minor];
	if(vfl==NULL) {
		mutex_unlock(&videodev_lock);
		request_module("char-major-%d-%d", VIDEO_MAJOR, minor);
		mutex_lock(&videodev_lock);
		vfl=video_device[minor];
		if (vfl==NULL) {
			mutex_unlock(&videodev_lock);
			return -ENODEV;
		}
	}
	old_fops = file->f_op;
	file->f_op = fops_get(vfl->fops);
	if(file->f_op->open)
		err = file->f_op->open(inode,file);
	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
	mutex_unlock(&videodev_lock);
	return err;
}

/*
 * helper function -- handles userspace copying for ioctl arguments
 */

#ifdef __OLD_VIDIOC_
static unsigned int
video_fix_command(unsigned int cmd)
{
	switch (cmd) {
	case VIDIOC_OVERLAY_OLD:
		cmd = VIDIOC_OVERLAY;
		break;
	case VIDIOC_S_PARM_OLD:
		cmd = VIDIOC_S_PARM;
		break;
	case VIDIOC_S_CTRL_OLD:
		cmd = VIDIOC_S_CTRL;
		break;
	case VIDIOC_G_AUDIO_OLD:
		cmd = VIDIOC_G_AUDIO;
		break;
	case VIDIOC_G_AUDOUT_OLD:
		cmd = VIDIOC_G_AUDOUT;
		break;
	case VIDIOC_CROPCAP_OLD:
		cmd = VIDIOC_CROPCAP;
		break;
	}
	return cmd;
}
#endif

/*
 * Obsolete usercopy function - Should be removed soon
 */
int
video_usercopy(struct inode *inode, struct file *file,
	       unsigned int cmd, unsigned long arg,
	       int (*func)(struct inode *inode, struct file *file,
			   unsigned int cmd, void *arg))
{
	char	sbuf[128];
	void    *mbuf = NULL;
	void	*parg = NULL;
	int	err  = -EINVAL;
	int     is_ext_ctrl;
	size_t  ctrls_size = 0;
	void __user *user_ptr = NULL;

#ifdef __OLD_VIDIOC_
	cmd = video_fix_command(cmd);
#endif
	is_ext_ctrl = (cmd == VIDIOC_S_EXT_CTRLS || cmd == VIDIOC_G_EXT_CTRLS ||
		       cmd == VIDIOC_TRY_EXT_CTRLS);

	/*  Copy arguments into temp kernel buffer  */
	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:
		parg = NULL;
		break;
	case _IOC_READ:
	case _IOC_WRITE:
	case (_IOC_WRITE | _IOC_READ):
		if (_IOC_SIZE(cmd) <= sizeof(sbuf)) {
			parg = sbuf;
		} else {
			/* too big to allocate from stack */
			mbuf = kmalloc(_IOC_SIZE(cmd),GFP_KERNEL);
			if (NULL == mbuf)
				return -ENOMEM;
			parg = mbuf;
		}

		err = -EFAULT;
		if (_IOC_DIR(cmd) & _IOC_WRITE)
			if (copy_from_user(parg, (void __user *)arg, _IOC_SIZE(cmd)))
				goto out;
		break;
	}
	if (is_ext_ctrl) {
		struct v4l2_ext_controls *p = parg;

		/* In case of an error, tell the caller that it wasn't
		   a specific control that caused it. */
		p->error_idx = p->count;
		user_ptr = (void __user *)p->controls;
		if (p->count) {
			ctrls_size = sizeof(struct v4l2_ext_control) * p->count;
			/* Note: v4l2_ext_controls fits in sbuf[] so mbuf is still NULL. */
			mbuf = kmalloc(ctrls_size, GFP_KERNEL);
			err = -ENOMEM;
			if (NULL == mbuf)
				goto out_ext_ctrl;
			err = -EFAULT;
			if (copy_from_user(mbuf, user_ptr, ctrls_size))
				goto out_ext_ctrl;
			p->controls = mbuf;
		}
	}

	/* call driver */
	err = func(inode, file, cmd, parg);
	if (err == -ENOIOCTLCMD)
		err = -EINVAL;
	if (is_ext_ctrl) {
		struct v4l2_ext_controls *p = parg;

		p->controls = (void *)user_ptr;
		if (p->count && err == 0 && copy_to_user(user_ptr, mbuf, ctrls_size))
			err = -EFAULT;
		goto out_ext_ctrl;
	}
	if (err < 0)
		goto out;

out_ext_ctrl:
	/*  Copy results into user buffer  */
	switch (_IOC_DIR(cmd))
	{
	case _IOC_READ:
	case (_IOC_WRITE | _IOC_READ):
		if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
			err = -EFAULT;
		break;
	}

out:
	kfree(mbuf);
	return err;
}

/*
 * open/release helper functions -- handle exclusive opens
 * Should be removed soon
 */
int video_exclusive_open(struct inode *inode, struct file *file)
{
	struct  video_device *vfl = video_devdata(file);
	int retval = 0;

	mutex_lock(&vfl->lock);
	if (vfl->users) {
		retval = -EBUSY;
	} else {
		vfl->users++;
	}
	mutex_unlock(&vfl->lock);
	return retval;
}

int video_exclusive_release(struct inode *inode, struct file *file)
{
	struct  video_device *vfl = video_devdata(file);

	vfl->users--;
	return 0;
}

static char *v4l2_memory_names[] = {
	[V4L2_MEMORY_MMAP]    = "mmap",
	[V4L2_MEMORY_USERPTR] = "userptr",
	[V4L2_MEMORY_OVERLAY] = "overlay",
};


/* FIXME: Those stuff are replicated also on v4l2-common.c */
static char *v4l2_type_names_FIXME[] = {
	[V4L2_BUF_TYPE_VIDEO_CAPTURE]      = "video-cap",
	[V4L2_BUF_TYPE_VIDEO_OVERLAY]      = "video-over",
	[V4L2_BUF_TYPE_VIDEO_OUTPUT]       = "video-out",
	[V4L2_BUF_TYPE_VBI_CAPTURE]        = "vbi-cap",
	[V4L2_BUF_TYPE_VBI_OUTPUT]         = "vbi-out",
	[V4L2_BUF_TYPE_SLICED_VBI_OUTPUT]  = "sliced-vbi-out",
	[V4L2_BUF_TYPE_SLICED_VBI_CAPTURE] = "sliced-vbi-capture",
	[V4L2_BUF_TYPE_PRIVATE]            = "private",
};

static char *v4l2_field_names_FIXME[] = {
	[V4L2_FIELD_ANY]        = "any",
	[V4L2_FIELD_NONE]       = "none",
	[V4L2_FIELD_TOP]        = "top",
	[V4L2_FIELD_BOTTOM]     = "bottom",
	[V4L2_FIELD_INTERLACED] = "interlaced",
	[V4L2_FIELD_SEQ_TB]     = "seq-tb",
	[V4L2_FIELD_SEQ_BT]     = "seq-bt",
	[V4L2_FIELD_ALTERNATE]  = "alternate",
};

#define prt_names(a,arr) (((a)>=0)&&((a)<ARRAY_SIZE(arr)))?arr[a]:"unknown"

static void dbgbuf(unsigned int cmd, struct video_device *vfd,
					struct v4l2_buffer *p)
{
	struct v4l2_timecode *tc=&p->timecode;

	dbgarg (cmd, "%02ld:%02d:%02d.%08ld index=%d, type=%s, "
		"bytesused=%d, flags=0x%08d, "
		"field=%0d, sequence=%d, memory=%s, offset/userptr=0x%08lx\n",
			(p->timestamp.tv_sec/3600),
			(int)(p->timestamp.tv_sec/60)%60,
			(int)(p->timestamp.tv_sec%60),
			p->timestamp.tv_usec,
			p->index,
			prt_names(p->type,v4l2_type_names_FIXME),
			p->bytesused,p->flags,
			p->field,p->sequence,
			prt_names(p->memory,v4l2_memory_names),
			p->m.userptr);
	dbgarg2 ("timecode= %02d:%02d:%02d type=%d, "
		"flags=0x%08d, frames=%d, userbits=0x%08x\n",
			tc->hours,tc->minutes,tc->seconds,
			tc->type, tc->flags, tc->frames, *(__u32 *) tc->userbits);
}

static inline void dbgrect(struct video_device *vfd, char *s,
							struct v4l2_rect *r)
{
	dbgarg2 ("%sRect start at %dx%d, size= %dx%d\n", s, r->left, r->top,
						r->width, r->height);
};

static inline void v4l_print_pix_fmt (struct video_device *vfd,
						struct v4l2_pix_format *fmt)
{
	dbgarg2 ("width=%d, height=%d, format=0x%08x, field=%s, "
		"bytesperline=%d sizeimage=%d, colorspace=%d\n",
		fmt->width,fmt->height,fmt->pixelformat,
		prt_names(fmt->field,v4l2_field_names_FIXME),
		fmt->bytesperline,fmt->sizeimage,fmt->colorspace);
};


static int check_fmt (struct video_device *vfd, enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (vfd->vidioc_try_fmt_cap)
			return (0);
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (vfd->vidioc_try_fmt_overlay)
			return (0);
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (vfd->vidioc_try_fmt_vbi)
			return (0);
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		if (vfd->vidioc_try_fmt_vbi_output)
			return (0);
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		if (vfd->vidioc_try_fmt_vbi_capture)
			return (0);
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (vfd->vidioc_try_fmt_video_output)
			return (0);
		break;
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		if (vfd->vidioc_try_fmt_vbi_output)
			return (0);
		break;
	case V4L2_BUF_TYPE_PRIVATE:
		if (vfd->vidioc_try_fmt_type_private)
			return (0);
		break;
	}
	return (-EINVAL);
}

static int __video_do_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, void *arg)
{
	struct video_device *vfd = video_devdata(file);
	void                 *fh = file->private_data;
	int                  ret = -EINVAL;

	if ( (vfd->debug & V4L2_DEBUG_IOCTL) &&
				!(vfd->debug | V4L2_DEBUG_IOCTL_ARG)) {
		v4l_print_ioctl(vfd->name, cmd);
	}

	switch(cmd) {
	/* --- capabilities ------------------------------------------ */
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = (struct v4l2_capability*)arg;
		memset(cap, 0, sizeof(*cap));

		if (!vfd->vidioc_querycap)
			break;

		ret=vfd->vidioc_querycap(file, fh, cap);
		if (!ret)
			dbgarg (cmd, "driver=%s, card=%s, bus=%s, "
					"version=0x%08x, "
					"capabilities=0x%08x\n",
					cap->driver,cap->card,cap->bus_info,
					cap->version,
					cap->capabilities);
		break;
	}

	/* --- priority ------------------------------------------ */
	case VIDIOC_G_PRIORITY:
	{
		enum v4l2_priority *p=arg;

		if (!vfd->vidioc_g_priority)
			break;
		ret=vfd->vidioc_g_priority(file, fh, p);
		if (!ret)
			dbgarg(cmd, "priority is %d\n", *p);
		break;
	}
	case VIDIOC_S_PRIORITY:
	{
		enum v4l2_priority *p=arg;

		if (!vfd->vidioc_s_priority)
			break;
		dbgarg(cmd, "setting priority to %d\n", *p);
		ret=vfd->vidioc_s_priority(file, fh, *p);
		break;
	}

	/* --- capture ioctls ---------------------------------------- */
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = arg;
		enum v4l2_buf_type type;
		unsigned int index;

		index = f->index;
		type  = f->type;
		memset(f,0,sizeof(*f));
		f->index = index;
		f->type  = type;

		switch (type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			if (vfd->vidioc_enum_fmt_cap)
				ret=vfd->vidioc_enum_fmt_cap(file, fh, f);
			break;
		case V4L2_BUF_TYPE_VIDEO_OVERLAY:
			if (vfd->vidioc_enum_fmt_overlay)
				ret=vfd->vidioc_enum_fmt_overlay(file, fh, f);
			break;
		case V4L2_BUF_TYPE_VBI_CAPTURE:
			if (vfd->vidioc_enum_fmt_vbi)
				ret=vfd->vidioc_enum_fmt_vbi(file, fh, f);
			break;
		case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
			if (vfd->vidioc_enum_fmt_vbi_output)
				ret=vfd->vidioc_enum_fmt_vbi_output(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
			if (vfd->vidioc_enum_fmt_vbi_capture)
				ret=vfd->vidioc_enum_fmt_vbi_capture(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_VIDEO_OUTPUT:
			if (vfd->vidioc_enum_fmt_video_output)
				ret=vfd->vidioc_enum_fmt_video_output(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_VBI_OUTPUT:
			if (vfd->vidioc_enum_fmt_vbi_output)
				ret=vfd->vidioc_enum_fmt_vbi_output(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_PRIVATE:
			if (vfd->vidioc_enum_fmt_type_private)
				ret=vfd->vidioc_enum_fmt_type_private(file,
								fh, f);
			break;
		}
		if (!ret)
			dbgarg (cmd, "index=%d, type=%d, flags=%d, "
					"description=%s,"
					" pixelformat=0x%8x\n",
					f->index, f->type, f->flags,
					f->description,
					f->pixelformat);

		break;
	}
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = (struct v4l2_format *)arg;
		enum v4l2_buf_type type=f->type;

		memset(&f->fmt.pix,0,sizeof(f->fmt.pix));
		f->type=type;

		/* FIXME: Should be one dump per type */
		dbgarg (cmd, "type=%s\n", prt_names(type,
					v4l2_type_names_FIXME));

		switch (type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			if (vfd->vidioc_g_fmt_cap)
				ret=vfd->vidioc_g_fmt_cap(file, fh, f);
			if (!ret)
				v4l_print_pix_fmt(vfd,&f->fmt.pix);
			break;
		case V4L2_BUF_TYPE_VIDEO_OVERLAY:
			if (vfd->vidioc_g_fmt_overlay)
				ret=vfd->vidioc_g_fmt_overlay(file, fh, f);
			break;
		case V4L2_BUF_TYPE_VBI_CAPTURE:
			if (vfd->vidioc_g_fmt_vbi)
				ret=vfd->vidioc_g_fmt_vbi(file, fh, f);
			break;
		case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
			if (vfd->vidioc_g_fmt_vbi_output)
				ret=vfd->vidioc_g_fmt_vbi_output(file, fh, f);
			break;
		case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
			if (vfd->vidioc_g_fmt_vbi_capture)
				ret=vfd->vidioc_g_fmt_vbi_capture(file, fh, f);
			break;
		case V4L2_BUF_TYPE_VIDEO_OUTPUT:
			if (vfd->vidioc_g_fmt_video_output)
				ret=vfd->vidioc_g_fmt_video_output(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_VBI_OUTPUT:
			if (vfd->vidioc_g_fmt_vbi_output)
				ret=vfd->vidioc_g_fmt_vbi_output(file, fh, f);
			break;
		case V4L2_BUF_TYPE_PRIVATE:
			if (vfd->vidioc_g_fmt_type_private)
				ret=vfd->vidioc_g_fmt_type_private(file,
								fh, f);
			break;
		}

		break;
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = (struct v4l2_format *)arg;

		/* FIXME: Should be one dump per type */
		dbgarg (cmd, "type=%s\n", prt_names(f->type,
					v4l2_type_names_FIXME));

		switch (f->type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			v4l_print_pix_fmt(vfd,&f->fmt.pix);
			if (vfd->vidioc_s_fmt_cap)
				ret=vfd->vidioc_s_fmt_cap(file, fh, f);
			break;
		case V4L2_BUF_TYPE_VIDEO_OVERLAY:
			if (vfd->vidioc_s_fmt_overlay)
				ret=vfd->vidioc_s_fmt_overlay(file, fh, f);
			break;
		case V4L2_BUF_TYPE_VBI_CAPTURE:
			if (vfd->vidioc_s_fmt_vbi)
				ret=vfd->vidioc_s_fmt_vbi(file, fh, f);
			break;
		case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
			if (vfd->vidioc_s_fmt_vbi_output)
				ret=vfd->vidioc_s_fmt_vbi_output(file, fh, f);
			break;
		case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
			if (vfd->vidioc_s_fmt_vbi_capture)
				ret=vfd->vidioc_s_fmt_vbi_capture(file, fh, f);
			break;
		case V4L2_BUF_TYPE_VIDEO_OUTPUT:
			if (vfd->vidioc_s_fmt_video_output)
				ret=vfd->vidioc_s_fmt_video_output(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_VBI_OUTPUT:
			if (vfd->vidioc_s_fmt_vbi_output)
				ret=vfd->vidioc_s_fmt_vbi_output(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_PRIVATE:
			if (vfd->vidioc_s_fmt_type_private)
				ret=vfd->vidioc_s_fmt_type_private(file,
								fh, f);
			break;
		}
		break;
	}
	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *f = (struct v4l2_format *)arg;

		/* FIXME: Should be one dump per type */
		dbgarg (cmd, "type=%s\n", prt_names(f->type,
						v4l2_type_names_FIXME));
		switch (f->type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			if (vfd->vidioc_try_fmt_cap)
				ret=vfd->vidioc_try_fmt_cap(file, fh, f);
			if (!ret)
				v4l_print_pix_fmt(vfd,&f->fmt.pix);
			break;
		case V4L2_BUF_TYPE_VIDEO_OVERLAY:
			if (vfd->vidioc_try_fmt_overlay)
				ret=vfd->vidioc_try_fmt_overlay(file, fh, f);
			break;
		case V4L2_BUF_TYPE_VBI_CAPTURE:
			if (vfd->vidioc_try_fmt_vbi)
				ret=vfd->vidioc_try_fmt_vbi(file, fh, f);
			break;
		case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
			if (vfd->vidioc_try_fmt_vbi_output)
				ret=vfd->vidioc_try_fmt_vbi_output(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
			if (vfd->vidioc_try_fmt_vbi_capture)
				ret=vfd->vidioc_try_fmt_vbi_capture(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_VIDEO_OUTPUT:
			if (vfd->vidioc_try_fmt_video_output)
				ret=vfd->vidioc_try_fmt_video_output(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_VBI_OUTPUT:
			if (vfd->vidioc_try_fmt_vbi_output)
				ret=vfd->vidioc_try_fmt_vbi_output(file,
								fh, f);
			break;
		case V4L2_BUF_TYPE_PRIVATE:
			if (vfd->vidioc_try_fmt_type_private)
				ret=vfd->vidioc_try_fmt_type_private(file,
								fh, f);
			break;
		}

		break;
	}
	/* FIXME: Those buf reqs could be handled here,
	   with some changes on videobuf to allow its header to be included at
	   videodev2.h or being merged at videodev2.
	 */
	case VIDIOC_REQBUFS:
	{
		struct v4l2_requestbuffers *p=arg;

		if (!vfd->vidioc_reqbufs)
			break;
		ret = check_fmt (vfd, p->type);
		if (ret)
			break;

		ret=vfd->vidioc_reqbufs(file, fh, p);
		dbgarg (cmd, "count=%d, type=%s, memory=%s\n",
				p->count,
				prt_names(p->type,v4l2_type_names_FIXME),
				prt_names(p->memory,v4l2_memory_names));
		break;
	}
	case VIDIOC_QUERYBUF:
	{
		struct v4l2_buffer *p=arg;

		if (!vfd->vidioc_querybuf)
			break;
		ret = check_fmt (vfd, p->type);
		if (ret)
			break;

		ret=vfd->vidioc_querybuf(file, fh, p);
		if (!ret)
			dbgbuf(cmd,vfd,p);
		break;
	}
	case VIDIOC_QBUF:
	{
		struct v4l2_buffer *p=arg;

		if (!vfd->vidioc_qbuf)
			break;
		ret = check_fmt (vfd, p->type);
		if (ret)
			break;

		ret=vfd->vidioc_qbuf(file, fh, p);
		if (!ret)
			dbgbuf(cmd,vfd,p);
		break;
	}
	case VIDIOC_DQBUF:
	{
		struct v4l2_buffer *p=arg;
		if (!vfd->vidioc_dqbuf)
			break;
		ret = check_fmt (vfd, p->type);
		if (ret)
			break;

		ret=vfd->vidioc_dqbuf(file, fh, p);
		if (!ret)
			dbgbuf(cmd,vfd,p);
		break;
	}
	case VIDIOC_OVERLAY:
	{
		int *i = arg;

		if (!vfd->vidioc_overlay)
			break;
		dbgarg (cmd, "value=%d\n",*i);
		ret=vfd->vidioc_overlay(file, fh, *i);
		break;
	}
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	/* --- streaming capture ------------------------------------- */
	case VIDIOCGMBUF:
	{
		struct video_mbuf *p=arg;

		memset(p,0,sizeof(p));

		if (!vfd->vidiocgmbuf)
			break;
		ret=vfd->vidiocgmbuf(file, fh, p);
		if (!ret)
			dbgarg (cmd, "size=%d, frames=%d, offsets=0x%08lx\n",
						p->size, p->frames,
						(unsigned long)p->offsets);
		break;
	}
#endif
	case VIDIOC_G_FBUF:
	{
		struct v4l2_framebuffer *p=arg;
		if (!vfd->vidioc_g_fbuf)
			break;
		ret=vfd->vidioc_g_fbuf(file, fh, arg);
		if (!ret) {
			dbgarg (cmd, "capability=%d, flags=%d, base=0x%08lx\n",
					p->capability,p->flags,
					(unsigned long)p->base);
			v4l_print_pix_fmt (vfd, &p->fmt);
		}
		break;
	}
	case VIDIOC_S_FBUF:
	{
		struct v4l2_framebuffer *p=arg;
		if (!vfd->vidioc_s_fbuf)
			break;

		dbgarg (cmd, "capability=%d, flags=%d, base=0x%08lx\n",
				p->capability,p->flags,(unsigned long)p->base);
		v4l_print_pix_fmt (vfd, &p->fmt);
		ret=vfd->vidioc_s_fbuf(file, fh, arg);

		break;
	}
	case VIDIOC_STREAMON:
	{
		enum v4l2_buf_type i = *(int *)arg;
		if (!vfd->vidioc_streamon)
			break;
		dbgarg (cmd, "type=%s\n", prt_names(i,v4l2_type_names_FIXME));
		ret=vfd->vidioc_streamon(file, fh,i);
		break;
	}
	case VIDIOC_STREAMOFF:
	{
		enum v4l2_buf_type i = *(int *)arg;

		if (!vfd->vidioc_streamoff)
			break;
		dbgarg (cmd, "type=%s\n", prt_names(i,v4l2_type_names_FIXME));
		ret=vfd->vidioc_streamoff(file, fh, i);
		break;
	}
	/* ---------- tv norms ---------- */
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *p = arg;
		unsigned int index = p->index;

		if (!vfd->tvnormsize) {
			printk (KERN_WARNING "%s: no TV norms defined!\n",
						vfd->name);
			break;
		}

		if (index<0 || index >= vfd->tvnormsize) {
			ret=-EINVAL;
			break;
		}
		v4l2_video_std_construct(p, vfd->tvnorms[p->index].id,
					 vfd->tvnorms[p->index].name);
		p->index = index;

		dbgarg (cmd, "index=%d, id=%Ld, name=%s, fps=%d/%d, "
				"framelines=%d\n", p->index,
				(unsigned long long)p->id, p->name,
				p->frameperiod.numerator,
				p->frameperiod.denominator,
				p->framelines);

		ret=0;
		break;
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *id = arg;

		*id = vfd->current_norm;

		dbgarg (cmd, "value=%Lu\n", (long long unsigned) *id);

		ret=0;
		break;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;
		unsigned int i;

		if (!vfd->tvnormsize) {
			printk (KERN_WARNING "%s: no TV norms defined!\n",
						vfd->name);
			break;
		}

		dbgarg (cmd, "value=%Lu\n", (long long unsigned) *id);

		/* First search for exact match */
		for (i = 0; i < vfd->tvnormsize; i++)
			if (*id == vfd->tvnorms[i].id)
				break;
		/* Then for a generic video std that contains desired std */
		if (i == vfd->tvnormsize)
			for (i = 0; i < vfd->tvnormsize; i++)
				if (*id & vfd->tvnorms[i].id)
					break;
		if (i == vfd->tvnormsize) {
			break;
		}

		/* Calls the specific handler */
		if (vfd->vidioc_s_std)
			ret=vfd->vidioc_s_std(file, fh, i);
		else
			ret=-EINVAL;

		/* Updates standard information */
		if (!ret)
			vfd->current_norm=*id;

		break;
	}
	case VIDIOC_QUERYSTD:
	{
		v4l2_std_id *p=arg;

		if (!vfd->vidioc_querystd)
			break;
		ret=vfd->vidioc_querystd(file, fh, arg);
		if (!ret)
			dbgarg (cmd, "detected std=%Lu\n",
						(unsigned long long)*p);
		break;
	}
	/* ------ input switching ---------- */
	/* FIXME: Inputs can be handled inside videodev2 */
	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *p=arg;
		int i=p->index;

		if (!vfd->vidioc_enum_input)
			break;
		memset(p, 0, sizeof(*p));
		p->index=i;

		ret=vfd->vidioc_enum_input(file, fh, p);
		if (!ret)
			dbgarg (cmd, "index=%d, name=%s, type=%d, "
					"audioset=%d, "
					"tuner=%d, std=%Ld, status=%d\n",
					p->index,p->name,p->type,p->audioset,
					p->tuner,
					(unsigned long long)p->std,
					p->status);
		break;
	}
	case VIDIOC_G_INPUT:
	{
		unsigned int *i = arg;

		if (!vfd->vidioc_g_input)
			break;
		ret=vfd->vidioc_g_input(file, fh, i);
		if (!ret)
			dbgarg (cmd, "value=%d\n",*i);
		break;
	}
	case VIDIOC_S_INPUT:
	{
		unsigned int *i = arg;

		if (!vfd->vidioc_s_input)
			break;
		dbgarg (cmd, "value=%d\n",*i);
		ret=vfd->vidioc_s_input(file, fh, *i);
		break;
	}

	/* ------ output switching ---------- */
	case VIDIOC_G_OUTPUT:
	{
		unsigned int *i = arg;

		if (!vfd->vidioc_g_output)
			break;
		ret=vfd->vidioc_g_output(file, fh, i);
		if (!ret)
			dbgarg (cmd, "value=%d\n",*i);
		break;
	}
	case VIDIOC_S_OUTPUT:
	{
		unsigned int *i = arg;

		if (!vfd->vidioc_s_output)
			break;
		dbgarg (cmd, "value=%d\n",*i);
		ret=vfd->vidioc_s_output(file, fh, *i);
		break;
	}

	/* --- controls ---------------------------------------------- */
	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *p=arg;

		if (!vfd->vidioc_queryctrl)
			break;
		ret=vfd->vidioc_queryctrl(file, fh, p);

		if (!ret)
			dbgarg (cmd, "id=%d, type=%d, name=%s, "
					"min/max=%d/%d,"
					" step=%d, default=%d, flags=0x%08x\n",
					p->id,p->type,p->name,p->minimum,
					p->maximum,p->step,p->default_value,
					p->flags);
		break;
	}
	case VIDIOC_G_CTRL:
	{
		struct v4l2_control *p = arg;

		if (!vfd->vidioc_g_ctrl)
			break;
		dbgarg(cmd, "Enum for index=%d\n", p->id);

		ret=vfd->vidioc_g_ctrl(file, fh, p);
		if (!ret)
			dbgarg2 ( "id=%d, value=%d\n", p->id, p->value);
		break;
	}
	case VIDIOC_S_CTRL:
	{
		struct v4l2_control *p = arg;

		if (!vfd->vidioc_s_ctrl)
			break;
		dbgarg (cmd, "id=%d, value=%d\n", p->id, p->value);

		ret=vfd->vidioc_s_ctrl(file, fh, p);
		break;
	}
	case VIDIOC_G_EXT_CTRLS:
	{
		struct v4l2_ext_controls *p = arg;

		if (vfd->vidioc_g_ext_ctrls) {
			dbgarg(cmd, "count=%d\n", p->count);

			ret=vfd->vidioc_g_ext_ctrls(file, fh, p);
		}
		break;
	}
	case VIDIOC_S_EXT_CTRLS:
	{
		struct v4l2_ext_controls *p = arg;

		if (vfd->vidioc_s_ext_ctrls) {
			dbgarg(cmd, "count=%d\n", p->count);

			ret=vfd->vidioc_s_ext_ctrls(file, fh, p);
		}
		break;
	}
	case VIDIOC_TRY_EXT_CTRLS:
	{
		struct v4l2_ext_controls *p = arg;

		if (vfd->vidioc_try_ext_ctrls) {
			dbgarg(cmd, "count=%d\n", p->count);

			ret=vfd->vidioc_try_ext_ctrls(file, fh, p);
		}
		break;
	}
	case VIDIOC_QUERYMENU:
	{
		struct v4l2_querymenu *p=arg;
		if (!vfd->vidioc_querymenu)
			break;
		ret=vfd->vidioc_querymenu(file, fh, p);
		if (!ret)
			dbgarg (cmd, "id=%d, index=%d, name=%s\n",
						p->id,p->index,p->name);
		break;
	}
	/* --- audio ---------------------------------------------- */
	case VIDIOC_ENUMAUDIO:
	{
		struct v4l2_audio *p=arg;

		if (!vfd->vidioc_enumaudio)
			break;
		dbgarg(cmd, "Enum for index=%d\n", p->index);
		ret=vfd->vidioc_enumaudio(file, fh, p);
		if (!ret)
			dbgarg2("index=%d, name=%s, capability=%d, "
					"mode=%d\n",p->index,p->name,
					p->capability, p->mode);
		break;
	}
	case VIDIOC_G_AUDIO:
	{
		struct v4l2_audio *p=arg;

		if (!vfd->vidioc_g_audio)
			break;
		dbgarg(cmd, "Get for index=%d\n", p->index);
		ret=vfd->vidioc_g_audio(file, fh, p);
		if (!ret)
			dbgarg2("index=%d, name=%s, capability=%d, "
					"mode=%d\n",p->index,
					p->name,p->capability, p->mode);
		break;
	}
	case VIDIOC_S_AUDIO:
	{
		struct v4l2_audio *p=arg;

		if (!vfd->vidioc_s_audio)
			break;
		dbgarg(cmd, "index=%d, name=%s, capability=%d, "
					"mode=%d\n", p->index, p->name,
					p->capability, p->mode);
		ret=vfd->vidioc_s_audio(file, fh, p);
		break;
	}
	case VIDIOC_ENUMAUDOUT:
	{
		struct v4l2_audioout *p=arg;

		if (!vfd->vidioc_enumaudout)
			break;
		dbgarg(cmd, "Enum for index=%d\n", p->index);
		ret=vfd->vidioc_enumaudout(file, fh, p);
		if (!ret)
			dbgarg2("index=%d, name=%s, capability=%d, "
					"mode=%d\n", p->index, p->name,
					p->capability,p->mode);
		break;
	}
	case VIDIOC_G_AUDOUT:
	{
		struct v4l2_audioout *p=arg;

		if (!vfd->vidioc_g_audout)
			break;
		dbgarg(cmd, "Enum for index=%d\n", p->index);
		ret=vfd->vidioc_g_audout(file, fh, p);
		if (!ret)
			dbgarg2("index=%d, name=%s, capability=%d, "
					"mode=%d\n", p->index, p->name,
					p->capability,p->mode);
		break;
	}
	case VIDIOC_S_AUDOUT:
	{
		struct v4l2_audioout *p=arg;

		if (!vfd->vidioc_s_audout)
			break;
		dbgarg(cmd, "index=%d, name=%s, capability=%d, "
					"mode=%d\n", p->index, p->name,
					p->capability,p->mode);

		ret=vfd->vidioc_s_audout(file, fh, p);
		break;
	}
	case VIDIOC_G_MODULATOR:
	{
		struct v4l2_modulator *p=arg;
		if (!vfd->vidioc_g_modulator)
			break;
		ret=vfd->vidioc_g_modulator(file, fh, p);
		if (!ret)
			dbgarg(cmd, "index=%d, name=%s, "
					"capability=%d, rangelow=%d,"
					" rangehigh=%d, txsubchans=%d\n",
					p->index, p->name,p->capability,
					p->rangelow, p->rangehigh,
					p->txsubchans);
		break;
	}
	case VIDIOC_S_MODULATOR:
	{
		struct v4l2_modulator *p=arg;
		if (!vfd->vidioc_s_modulator)
			break;
		dbgarg(cmd, "index=%d, name=%s, capability=%d, "
				"rangelow=%d, rangehigh=%d, txsubchans=%d\n",
				p->index, p->name,p->capability,p->rangelow,
				p->rangehigh,p->txsubchans);
			ret=vfd->vidioc_s_modulator(file, fh, p);
		break;
	}
	case VIDIOC_G_CROP:
	{
		struct v4l2_crop *p=arg;
		if (!vfd->vidioc_g_crop)
			break;
		ret=vfd->vidioc_g_crop(file, fh, p);
		if (!ret) {
			dbgarg(cmd, "type=%d\n", p->type);
			dbgrect(vfd, "", &p->c);
		}
		break;
	}
	case VIDIOC_S_CROP:
	{
		struct v4l2_crop *p=arg;
		if (!vfd->vidioc_s_crop)
			break;
		dbgarg(cmd, "type=%d\n", p->type);
		dbgrect(vfd, "", &p->c);
		ret=vfd->vidioc_s_crop(file, fh, p);
		break;
	}
	case VIDIOC_CROPCAP:
	{
		struct v4l2_cropcap *p=arg;
		/*FIXME: Should also show v4l2_fract pixelaspect */
		if (!vfd->vidioc_cropcap)
			break;
		dbgarg(cmd, "type=%d\n", p->type);
		dbgrect(vfd, "bounds ", &p->bounds);
		dbgrect(vfd, "defrect ", &p->defrect);
		ret=vfd->vidioc_cropcap(file, fh, p);
		break;
	}
	case VIDIOC_G_MPEGCOMP:
	{
		struct v4l2_mpeg_compression *p=arg;

		/*FIXME: Several fields not shown */
		if (!vfd->vidioc_g_mpegcomp)
			break;
		ret=vfd->vidioc_g_mpegcomp(file, fh, p);
		if (!ret)
			dbgarg (cmd, "ts_pid_pmt=%d, ts_pid_audio=%d,"
					" ts_pid_video=%d, ts_pid_pcr=%d, "
					"ps_size=%d, au_sample_rate=%d, "
					"au_pesid=%c, vi_frame_rate=%d, "
					"vi_frames_per_gop=%d, "
					"vi_bframes_count=%d, vi_pesid=%c\n",
					p->ts_pid_pmt,p->ts_pid_audio,
					p->ts_pid_video,p->ts_pid_pcr,
					p->ps_size, p->au_sample_rate,
					p->au_pesid, p->vi_frame_rate,
					p->vi_frames_per_gop,
					p->vi_bframes_count, p->vi_pesid);
		break;
	}
	case VIDIOC_S_MPEGCOMP:
	{
		struct v4l2_mpeg_compression *p=arg;
		/*FIXME: Several fields not shown */
		if (!vfd->vidioc_s_mpegcomp)
			break;
		dbgarg (cmd, "ts_pid_pmt=%d, ts_pid_audio=%d, "
				"ts_pid_video=%d, ts_pid_pcr=%d, ps_size=%d, "
				"au_sample_rate=%d, au_pesid=%c, "
				"vi_frame_rate=%d, vi_frames_per_gop=%d, "
				"vi_bframes_count=%d, vi_pesid=%c\n",
				p->ts_pid_pmt,p->ts_pid_audio, p->ts_pid_video,
				p->ts_pid_pcr, p->ps_size, p->au_sample_rate,
				p->au_pesid, p->vi_frame_rate,
				p->vi_frames_per_gop, p->vi_bframes_count,
				p->vi_pesid);
		ret=vfd->vidioc_s_mpegcomp(file, fh, p);
		break;
	}
	case VIDIOC_G_JPEGCOMP:
	{
		struct v4l2_jpegcompression *p=arg;
		if (!vfd->vidioc_g_jpegcomp)
			break;
		ret=vfd->vidioc_g_jpegcomp(file, fh, p);
		if (!ret)
			dbgarg (cmd, "quality=%d, APPn=%d, "
						"APP_len=%d, COM_len=%d, "
						"jpeg_markers=%d\n",
						p->quality,p->APPn,p->APP_len,
						p->COM_len,p->jpeg_markers);
		break;
	}
	case VIDIOC_S_JPEGCOMP:
	{
		struct v4l2_jpegcompression *p=arg;
		if (!vfd->vidioc_g_jpegcomp)
			break;
		dbgarg (cmd, "quality=%d, APPn=%d, APP_len=%d, "
					"COM_len=%d, jpeg_markers=%d\n",
					p->quality,p->APPn,p->APP_len,
					p->COM_len,p->jpeg_markers);
			ret=vfd->vidioc_s_jpegcomp(file, fh, p);
		break;
	}
	case VIDIOC_G_PARM:
	{
		struct v4l2_streamparm *p=arg;
		if (vfd->vidioc_g_parm) {
			ret=vfd->vidioc_g_parm(file, fh, p);
		} else {
			struct v4l2_standard s;
			int i;

			if (!vfd->tvnormsize) {
				printk (KERN_WARNING "%s: no TV norms defined!\n",
							vfd->name);
				break;
			}

			if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
				return -EINVAL;

			for (i = 0; i < vfd->tvnormsize; i++)
				if (vfd->tvnorms[i].id == vfd->current_norm)
					break;
			if (i >= vfd->tvnormsize)
				return -EINVAL;

			v4l2_video_std_construct(&s, vfd->current_norm,
						 vfd->tvnorms[i].name);

			memset(p,0,sizeof(*p));

			p->parm.capture.timeperframe = s.frameperiod;
			ret=0;
		}

		dbgarg (cmd, "type=%d\n", p->type);
		break;
	}
	case VIDIOC_S_PARM:
	{
		struct v4l2_streamparm *p=arg;
		if (!vfd->vidioc_s_parm)
			break;
		dbgarg (cmd, "type=%d\n", p->type);
		ret=vfd->vidioc_s_parm(file, fh, p);
		break;
	}
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *p=arg;
		if (!vfd->vidioc_g_tuner)
			break;
		ret=vfd->vidioc_g_tuner(file, fh, p);
		if (!ret)
			dbgarg (cmd, "index=%d, name=%s, type=%d, "
					"capability=%d, rangelow=%d, "
					"rangehigh=%d, signal=%d, afc=%d, "
					"rxsubchans=%d, audmode=%d\n",
					p->index, p->name, p->type,
					p->capability, p->rangelow,
					p->rangehigh, p->rxsubchans,
					p->audmode, p->signal, p->afc);
		break;
	}
	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *p=arg;
		if (!vfd->vidioc_s_tuner)
			break;
		dbgarg (cmd, "index=%d, name=%s, type=%d, "
				"capability=%d, rangelow=%d, rangehigh=%d, "
				"signal=%d, afc=%d, rxsubchans=%d, "
				"audmode=%d\n",p->index, p->name, p->type,
				p->capability, p->rangelow,p->rangehigh,
				p->rxsubchans, p->audmode, p->signal,
				p->afc);
		ret=vfd->vidioc_s_tuner(file, fh, p);
		break;
	}
	case VIDIOC_G_FREQUENCY:
	{
		struct v4l2_frequency *p=arg;
		if (!vfd->vidioc_g_frequency)
			break;
		ret=vfd->vidioc_g_frequency(file, fh, p);
		if (!ret)
			dbgarg (cmd, "tuner=%d, type=%d, frequency=%d\n",
						p->tuner,p->type,p->frequency);
		break;
	}
	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *p=arg;
		if (!vfd->vidioc_s_frequency)
			break;
		dbgarg (cmd, "tuner=%d, type=%d, frequency=%d\n",
				p->tuner,p->type,p->frequency);
		ret=vfd->vidioc_s_frequency(file, fh, p);
		break;
	}
	case VIDIOC_G_SLICED_VBI_CAP:
	{
		struct v4l2_sliced_vbi_cap *p=arg;
		if (!vfd->vidioc_g_sliced_vbi_cap)
			break;
		ret=vfd->vidioc_g_sliced_vbi_cap(file, fh, p);
		if (!ret)
			dbgarg (cmd, "service_set=%d\n", p->service_set);
		break;
	}
	case VIDIOC_LOG_STATUS:
	{
		if (!vfd->vidioc_log_status)
			break;
		ret=vfd->vidioc_log_status(file, fh);
		break;
	}

	/* --- Others --------------------------------------------- */

	default:
		ret=v4l_compat_translate_ioctl(inode,file,cmd,arg,__video_do_ioctl);
	}

	if (vfd->debug & V4L2_DEBUG_IOCTL_ARG) {
		if (ret<0) {
			printk ("%s: err:\n", vfd->name);
			v4l_print_ioctl(vfd->name, cmd);
		}
	}

	return ret;
}

int video_ioctl2 (struct inode *inode, struct file *file,
	       unsigned int cmd, unsigned long arg)
{
	char	sbuf[128];
	void    *mbuf = NULL;
	void	*parg = NULL;
	int	err  = -EINVAL;
	int     is_ext_ctrl;
	size_t  ctrls_size = 0;
	void __user *user_ptr = NULL;

#ifdef __OLD_VIDIOC_
	cmd = video_fix_command(cmd);
#endif
	is_ext_ctrl = (cmd == VIDIOC_S_EXT_CTRLS || cmd == VIDIOC_G_EXT_CTRLS ||
		       cmd == VIDIOC_TRY_EXT_CTRLS);

	/*  Copy arguments into temp kernel buffer  */
	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:
		parg = NULL;
		break;
	case _IOC_READ:
	case _IOC_WRITE:
	case (_IOC_WRITE | _IOC_READ):
		if (_IOC_SIZE(cmd) <= sizeof(sbuf)) {
			parg = sbuf;
		} else {
			/* too big to allocate from stack */
			mbuf = kmalloc(_IOC_SIZE(cmd),GFP_KERNEL);
			if (NULL == mbuf)
				return -ENOMEM;
			parg = mbuf;
		}

		err = -EFAULT;
		if (_IOC_DIR(cmd) & _IOC_WRITE)
			if (copy_from_user(parg, (void __user *)arg, _IOC_SIZE(cmd)))
				goto out;
		break;
	}

	if (is_ext_ctrl) {
		struct v4l2_ext_controls *p = parg;

		/* In case of an error, tell the caller that it wasn't
		   a specific control that caused it. */
		p->error_idx = p->count;
		user_ptr = (void __user *)p->controls;
		if (p->count) {
			ctrls_size = sizeof(struct v4l2_ext_control) * p->count;
			/* Note: v4l2_ext_controls fits in sbuf[] so mbuf is still NULL. */
			mbuf = kmalloc(ctrls_size, GFP_KERNEL);
			err = -ENOMEM;
			if (NULL == mbuf)
				goto out_ext_ctrl;
			err = -EFAULT;
			if (copy_from_user(mbuf, user_ptr, ctrls_size))
				goto out_ext_ctrl;
			p->controls = mbuf;
		}
	}

	/* Handles IOCTL */
	err = __video_do_ioctl(inode, file, cmd, parg);
	if (err == -ENOIOCTLCMD)
		err = -EINVAL;
	if (is_ext_ctrl) {
		struct v4l2_ext_controls *p = parg;

		p->controls = (void *)user_ptr;
		if (p->count && err == 0 && copy_to_user(user_ptr, mbuf, ctrls_size))
			err = -EFAULT;
		goto out_ext_ctrl;
	}
	if (err < 0)
		goto out;

out_ext_ctrl:
	/*  Copy results into user buffer  */
	switch (_IOC_DIR(cmd))
	{
	case _IOC_READ:
	case (_IOC_WRITE | _IOC_READ):
		if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
			err = -EFAULT;
		break;
	}

out:
	kfree(mbuf);
	return err;
}


static struct file_operations video_fops;

/**
 *	video_register_device - register video4linux devices
 *	@vfd:  video device structure we want to register
 *	@type: type of device to register
 *	@nr:   which device number (0 == /dev/video0, 1 == /dev/video1, ...
 *             -1 == first free)
 *
 *	The registration code assigns minor numbers based on the type
 *	requested. -ENFILE is returned in all the device slots for this
 *	category are full. If not then the minor field is set and the
 *	driver initialize function is called (if non %NULL).
 *
 *	Zero is returned on success.
 *
 *	Valid types are
 *
 *	%VFL_TYPE_GRABBER - A frame grabber
 *
 *	%VFL_TYPE_VTX - A teletext device
 *
 *	%VFL_TYPE_VBI - Vertical blank data (undecoded)
 *
 *	%VFL_TYPE_RADIO - A radio card
 */

int video_register_device(struct video_device *vfd, int type, int nr)
{
	int i=0;
	int base;
	int end;
	int ret;
	char *name_base;

	switch(type)
	{
		case VFL_TYPE_GRABBER:
			base=MINOR_VFL_TYPE_GRABBER_MIN;
			end=MINOR_VFL_TYPE_GRABBER_MAX+1;
			name_base = "video";
			break;
		case VFL_TYPE_VTX:
			base=MINOR_VFL_TYPE_VTX_MIN;
			end=MINOR_VFL_TYPE_VTX_MAX+1;
			name_base = "vtx";
			break;
		case VFL_TYPE_VBI:
			base=MINOR_VFL_TYPE_VBI_MIN;
			end=MINOR_VFL_TYPE_VBI_MAX+1;
			name_base = "vbi";
			break;
		case VFL_TYPE_RADIO:
			base=MINOR_VFL_TYPE_RADIO_MIN;
			end=MINOR_VFL_TYPE_RADIO_MAX+1;
			name_base = "radio";
			break;
		default:
			printk(KERN_ERR "%s called with unknown type: %d\n",
			       __FUNCTION__, type);
			return -1;
	}

	/* pick a minor number */
	mutex_lock(&videodev_lock);
	if (nr >= 0  &&  nr < end-base) {
		/* use the one the driver asked for */
		i = base+nr;
		if (NULL != video_device[i]) {
			mutex_unlock(&videodev_lock);
			return -ENFILE;
		}
	} else {
		/* use first free */
		for(i=base;i<end;i++)
			if (NULL == video_device[i])
				break;
		if (i == end) {
			mutex_unlock(&videodev_lock);
			return -ENFILE;
		}
	}
	video_device[i]=vfd;
	vfd->minor=i;
	mutex_unlock(&videodev_lock);
	mutex_init(&vfd->lock);

	/* sysfs class */
	memset(&vfd->class_dev, 0x00, sizeof(vfd->class_dev));
	if (vfd->dev)
		vfd->class_dev.dev = vfd->dev;
	vfd->class_dev.class       = &video_class;
	vfd->class_dev.devt        = MKDEV(VIDEO_MAJOR, vfd->minor);
	sprintf(vfd->class_dev.class_id, "%s%d", name_base, i - base);
	ret = class_device_register(&vfd->class_dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: class_device_register failed\n",
		       __FUNCTION__);
		goto fail_minor;
	}
	ret = class_device_create_file(&vfd->class_dev, &class_device_attr_name);
	if (ret < 0) {
		printk(KERN_ERR "%s: class_device_create_file 'name' failed\n",
		       __FUNCTION__);
		goto fail_classdev;
	}

#if 1
	/* needed until all drivers are fixed */
	if (!vfd->release)
		printk(KERN_WARNING "videodev: \"%s\" has no release callback. "
		       "Please fix your driver for proper sysfs support, see "
		       "http://lwn.net/Articles/36850/\n", vfd->name);
#endif
	return 0;

fail_classdev:
	class_device_unregister(&vfd->class_dev);
fail_minor:
	mutex_lock(&videodev_lock);
	video_device[vfd->minor] = NULL;
	vfd->minor = -1;
	mutex_unlock(&videodev_lock);
	return ret;
}

/**
 *	video_unregister_device - unregister a video4linux device
 *	@vfd: the device to unregister
 *
 *	This unregisters the passed device and deassigns the minor
 *	number. Future open calls will be met with errors.
 */

void video_unregister_device(struct video_device *vfd)
{
	mutex_lock(&videodev_lock);
	if(video_device[vfd->minor]!=vfd)
		panic("videodev: bad unregister");

	video_device[vfd->minor]=NULL;
	class_device_unregister(&vfd->class_dev);
	mutex_unlock(&videodev_lock);
}

/*
 * Video fs operations
 */
static struct file_operations video_fops=
{
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= video_open,
};

/*
 *	Initialise video for linux
 */

static int __init videodev_init(void)
{
	int ret;

	printk(KERN_INFO "Linux video capture interface: v2.00\n");
	if (register_chrdev(VIDEO_MAJOR, VIDEO_NAME, &video_fops)) {
		printk(KERN_WARNING "video_dev: unable to get major %d\n", VIDEO_MAJOR);
		return -EIO;
	}

	ret = class_register(&video_class);
	if (ret < 0) {
		unregister_chrdev(VIDEO_MAJOR, VIDEO_NAME);
		printk(KERN_WARNING "video_dev: class_register failed\n");
		return -EIO;
	}

	return 0;
}

static void __exit videodev_exit(void)
{
	class_unregister(&video_class);
	unregister_chrdev(VIDEO_MAJOR, VIDEO_NAME);
}

module_init(videodev_init)
module_exit(videodev_exit)

EXPORT_SYMBOL(video_register_device);
EXPORT_SYMBOL(video_unregister_device);
EXPORT_SYMBOL(video_devdata);
EXPORT_SYMBOL(video_usercopy);
EXPORT_SYMBOL(video_exclusive_open);
EXPORT_SYMBOL(video_exclusive_release);
EXPORT_SYMBOL(video_ioctl2);
EXPORT_SYMBOL(video_device_alloc);
EXPORT_SYMBOL(video_device_release);

MODULE_AUTHOR("Alan Cox, Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_DESCRIPTION("Device registrar for Video4Linux drivers v2");
MODULE_LICENSE("GPL");


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
