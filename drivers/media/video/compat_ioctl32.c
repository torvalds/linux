/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *	Separated from fs stuff by Arnd Bergmann <arnd@arndb.de>
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2001,2002  Andi Kleen, SuSE Labs
 * Copyright (C) 2003       Pavel Machek (pavel@suse.cz)
 * Copyright (C) 2005       Philippe De Muyter (phdm@macqel.be)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#include <linux/config.h>
#include <linux/compat.h>
#include <linux/videodev.h>
#include <linux/videodev2.h>
#include <linux/module.h>
#include <linux/smp_lock.h>

#ifdef CONFIG_COMPAT
struct video_tuner32 {
	compat_int_t tuner;
	char name[32];
	compat_ulong_t rangelow, rangehigh;
	u32 flags;	/* It is really u32 in videodev.h */
	u16 mode, signal;
};

static int get_video_tuner32(struct video_tuner *kp, struct video_tuner32 __user *up)
{
	if(get_user(kp->tuner, &up->tuner))
		return -EFAULT;
	__copy_from_user(kp->name, up->name, 32);
	__get_user(kp->rangelow, &up->rangelow);
	__get_user(kp->rangehigh, &up->rangehigh);
	__get_user(kp->flags, &up->flags);
	__get_user(kp->mode, &up->mode);
	__get_user(kp->signal, &up->signal);
	return 0;
}

static int put_video_tuner32(struct video_tuner *kp, struct video_tuner32 __user *up)
{
	if(put_user(kp->tuner, &up->tuner))
		return -EFAULT;
	__copy_to_user(up->name, kp->name, 32);
	__put_user(kp->rangelow, &up->rangelow);
	__put_user(kp->rangehigh, &up->rangehigh);
	__put_user(kp->flags, &up->flags);
	__put_user(kp->mode, &up->mode);
	__put_user(kp->signal, &up->signal);
	return 0;
}

struct video_buffer32 {
	compat_caddr_t base;
	compat_int_t height, width, depth, bytesperline;
};

static int get_video_buffer32(struct video_buffer *kp, struct video_buffer32 __user *up)
{
	u32 tmp;

	if (get_user(tmp, &up->base))
		return -EFAULT;

	/* This is actually a physical address stored
	 * as a void pointer.
	 */
	kp->base = (void *)(unsigned long) tmp;

	__get_user(kp->height, &up->height);
	__get_user(kp->width, &up->width);
	__get_user(kp->depth, &up->depth);
	__get_user(kp->bytesperline, &up->bytesperline);
	return 0;
}

static int put_video_buffer32(struct video_buffer *kp, struct video_buffer32 __user *up)
{
	u32 tmp = (u32)((unsigned long)kp->base);

	if(put_user(tmp, &up->base))
		return -EFAULT;
	__put_user(kp->height, &up->height);
	__put_user(kp->width, &up->width);
	__put_user(kp->depth, &up->depth);
	__put_user(kp->bytesperline, &up->bytesperline);
	return 0;
}

struct video_clip32 {
	s32 x, y, width, height;	/* Its really s32 in videodev.h */
	compat_caddr_t next;
};

struct video_window32 {
	u32 x, y, width, height, chromakey, flags;
	compat_caddr_t clips;
	compat_int_t clipcount;
};

static int native_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = -ENOIOCTLCMD;

	if (file->f_ops->unlocked_ioctl)
		ret = file->f_ops->unlocked_ioctl(file, cmd, arg);
	else if (file->f_ops->ioctl) {
		lock_kernel();
		ret = file->f_ops->ioctl(file->f_dentry->d_inode, file, cmd, arg);
		unlock_kernel();
	}

	return ret;
}


/* You get back everything except the clips... */
static int put_video_window32(struct video_window *kp, struct video_window32 __user *up)
{
	if(put_user(kp->x, &up->x))
		return -EFAULT;
	__put_user(kp->y, &up->y);
	__put_user(kp->width, &up->width);
	__put_user(kp->height, &up->height);
	__put_user(kp->chromakey, &up->chromakey);
	__put_user(kp->flags, &up->flags);
	__put_user(kp->clipcount, &up->clipcount);
	return 0;
}

struct v4l2_clip32
{
	struct v4l2_rect        c;
	compat_caddr_t 		next;
};

struct v4l2_window32
{
	struct v4l2_rect        w;
	enum v4l2_field  	field;
	__u32			chromakey;
	compat_caddr_t		clips; /* actually struct v4l2_clip32 * */
	__u32			clipcount;
	compat_caddr_t		bitmap;
};

static int get_v4l2_window32(struct v4l2_window *kp, struct v4l2_window32 __user *up)
{
	if (copy_from_user(&kp->w, &up->w, sizeof(up->w)))
		return -EFAULT;
	__get_user(kp->field, &up->field);
	__get_user(kp->chromakey, &up->chromakey);
	__get_user(kp->clipcount, &up->clipcount);
	if (kp->clipcount > 2048)
		return -EINVAL;
	if (kp->clipcount) {
		struct v4l2_clip32 *uclips = compat_ptr(up->clips);
		struct v4l2_clip *kclips;
		int n = kp->clipcount;

		kclips = compat_alloc_user_space(n * sizeof(struct v4l2_clip));
		kp->clips = kclips;
		while (--n >= 0) {
			copy_from_user(&kclips->c, &uclips->c, sizeof(uclips->c));
			kclips->next = n ? kclips + 1 : 0;
			uclips += 1;
			kclips += 1;
		}
	} else
		kp->clips = 0;
	return 0;
}

static int put_v4l2_window32(struct v4l2_window *kp, struct v4l2_window32 __user *up)
{
	if (copy_to_user(&up->w, &kp->w, sizeof(up->w)))
		return -EFAULT;
	__put_user(kp->field, &up->field);
	__put_user(kp->chromakey, &up->chromakey);
	__put_user(kp->clipcount, &up->clipcount);
	return 0;
}

static inline int get_v4l2_pix_format(struct v4l2_pix_format *kp, struct v4l2_pix_format __user *up)
{
	return copy_from_user(kp, up, sizeof(struct v4l2_pix_format));
}

static inline int put_v4l2_pix_format(struct v4l2_pix_format *kp, struct v4l2_pix_format __user *up)
{
	return copy_to_user(up, kp, sizeof(struct v4l2_pix_format));
}

static inline int get_v4l2_vbi_format(struct v4l2_vbi_format *kp, struct v4l2_vbi_format __user *up)
{
	return copy_from_user(kp, up, sizeof(struct v4l2_vbi_format));
}

static inline int put_v4l2_vbi_format(struct v4l2_vbi_format *kp, struct v4l2_vbi_format __user *up)
{
	return copy_to_user(up, kp, sizeof(struct v4l2_vbi_format));
}

struct v4l2_format32
{
	enum v4l2_buf_type type;
	union
	{
		struct v4l2_pix_format	pix;  // V4L2_BUF_TYPE_VIDEO_CAPTURE
		struct v4l2_window32	win;  // V4L2_BUF_TYPE_VIDEO_OVERLAY
		struct v4l2_vbi_format	vbi;  // V4L2_BUF_TYPE_VBI_CAPTURE
		__u8	raw_data[200];        // user-defined
	} fmt;
};

static int get_v4l2_format32(struct v4l2_format *kp, struct v4l2_format32 __user *up)
{
	if(get_user(kp->type, &up->type))
		return -EFAULT;
	switch (kp->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return get_v4l2_pix_format(&kp->fmt.pix, &up->fmt.pix);
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		return get_v4l2_window32(&kp->fmt.win, &up->fmt.win);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return get_v4l2_vbi_format(&kp->fmt.vbi, &up->fmt.vbi);
	default:
		printk("compat_ioctl : unexpected VIDIOC_FMT type %d\n",
								kp->type);
		return -ENXIO;
	}
}

static int put_v4l2_format32(struct v4l2_format *kp, struct v4l2_format32 __user *up)
{
	if(put_user(kp->type, &up->type))
		return -EFAULT;
	switch (kp->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return put_v4l2_pix_format(&kp->fmt.pix, &up->fmt.pix);
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		return put_v4l2_window32(&kp->fmt.win, &up->fmt.win);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return put_v4l2_vbi_format(&kp->fmt.vbi, &up->fmt.vbi);
	default:
		return -ENXIO;
	}
}

struct v4l2_standard32
{
	__u32		     index;
	__u32		     id[2]; /* __u64 would get the alignment wrong */
	__u8		     name[24];
	struct v4l2_fract    frameperiod; /* Frames, not fields */
	__u32		     framelines;
	__u32		     reserved[4];
};

static int get_v4l2_standard32(struct v4l2_standard *kp, struct v4l2_standard32 __user *up)
{
	/* other fields are not set by the user, nor used by the driver */
	return get_user(kp->index, &up->index);
}

static int put_v4l2_standard32(struct v4l2_standard *kp, struct v4l2_standard32 __user *up)
{
	if(put_user(kp->index, &up->index))
		return -EFAULT;
	__copy_to_user(up->id, &kp->id, sizeof(__u64));
	__copy_to_user(up->name, kp->name, 24);
	__put_user(kp->frameperiod, &up->frameperiod);
	__put_user(kp->framelines, &up->framelines);
	__copy_to_user(up->reserved, kp->reserved, 4 * sizeof(__u32));
	return 0;
}

struct v4l2_buffer32
{
	__u32			index;
	enum v4l2_buf_type      type;
	__u32			bytesused;
	__u32			flags;
	enum v4l2_field		field;
	struct compat_timeval	timestamp;
	struct v4l2_timecode	timecode;
	__u32			sequence;

	/* memory location */
	enum v4l2_memory        memory;
	union {
		__u32           offset;
		compat_long_t   userptr;
	} m;
	__u32			length;
	__u32			input;
	__u32			reserved;
};

static int get_v4l2_buffer32(struct v4l2_buffer *kp, struct v4l2_buffer32 __user *up)
{

	if (get_user(kp->index, &up->index))
		return -EFAULT;
	__get_user(kp->type, &up->type);
	__get_user(kp->flags, &up->flags);
	__get_user(kp->memory, &up->memory);
	__get_user(kp->input, &up->input);
	switch(kp->memory) {
	case V4L2_MEMORY_MMAP:
		break;
	case V4L2_MEMORY_USERPTR:
		{
		unsigned long tmp = (unsigned long)compat_ptr(up->m.userptr);

		__get_user(kp->length, &up->length);
		__get_user(kp->m.userptr, &tmp);
		}
		break;
	case V4L2_MEMORY_OVERLAY:
		__get_user(kp->m.offset, &up->m.offset);
		break;
	}
	return 0;
}

static int put_v4l2_buffer32(struct v4l2_buffer *kp, struct v4l2_buffer32 __user *up)
{
	if (put_user(kp->index, &up->index))
		return -EFAULT;
	__put_user(kp->type, &up->type);
	__put_user(kp->flags, &up->flags);
	__put_user(kp->memory, &up->memory);
	__put_user(kp->input, &up->input);
	switch(kp->memory) {
	case V4L2_MEMORY_MMAP:
		__put_user(kp->length, &up->length);
		__put_user(kp->m.offset, &up->m.offset);
		break;
	case V4L2_MEMORY_USERPTR:
		__put_user(kp->length, &up->length);
		__put_user(kp->m.userptr, &up->m.userptr);
		break;
	case V4L2_MEMORY_OVERLAY:
		__put_user(kp->m.offset, &up->m.offset);
		break;
	}
	__put_user(kp->bytesused, &up->bytesused);
	__put_user(kp->field, &up->field);
	__put_user(kp->timestamp.tv_sec, &up->timestamp.tv_sec);
	__put_user(kp->timestamp.tv_usec, &up->timestamp.tv_usec);
	__copy_to_user(&up->timecode, &kp->timecode, sizeof(struct v4l2_timecode));
	__put_user(kp->sequence, &up->sequence);
	__put_user(kp->reserved, &up->reserved);
	return 0;
}

struct v4l2_framebuffer32
{
	__u32			capability;
	__u32			flags;
	compat_caddr_t 		base;
	struct v4l2_pix_format	fmt;
};

static int put_v4l2_framebuffer32(struct v4l2_framebuffer *kp, struct v4l2_framebuffer32 __user *up)
{
	u32 tmp = (u32)((unsigned long)kp->base);

	if(put_user(tmp, &up->base))
		return -EFAULT;
	__put_user(kp->capability, &up->capability);
	__put_user(kp->flags, &up->flags);
	put_v4l2_pix_format(&kp->fmt, &up->fmt);
	return 0;
}

struct v4l2_input32 	/* identical layout, but different size */
{
	__u32	     index;		/*  Which input */
	__u8	     name[32];		/*  Label */
	__u32	     type;		/*  Type of input */
	__u32	     audioset;		/*  Associated audios (bitfield) */
	__u32        tuner;             /*  Associated tuner */
	__u32	     std[2];		/* __u64 would get the padding wrong */
	__u32	     status;
	__u32	     reserved[4];
};

#define VIDIOCGTUNER32		_IOWR('v',4, struct video_tuner32)
#define VIDIOCSTUNER32		_IOW('v',5, struct video_tuner32)
#define VIDIOCGWIN32		_IOR('v',9, struct video_window32)
#define VIDIOCSWIN32		_IOW('v',10, struct video_window32)
#define VIDIOCGFBUF32		_IOR('v',11, struct video_buffer32)
#define VIDIOCSFBUF32		_IOW('v',12, struct video_buffer32)
#define VIDIOCGFREQ32		_IOR('v',14, u32)
#define VIDIOCSFREQ32		_IOW('v',15, u32)

#define VIDIOC_G_FMT32		_IOWR ('V',  4, struct v4l2_format32)
#define VIDIOC_S_FMT32		_IOWR ('V',  5, struct v4l2_format32)
#define VIDIOC_QUERYBUF32	_IOWR ('V',  9, struct v4l2_buffer32)
#define VIDIOC_G_FBUF32		_IOR  ('V', 10, struct v4l2_framebuffer32)
/* VIDIOC_OVERLAY is now _IOW, but was _IOWR */
#define VIDIOC_OVERLAY32	_IOWR ('V', 14, compat_int_t)
#define VIDIOC_QBUF32		_IOWR ('V', 15, struct v4l2_buffer32)
#define VIDIOC_DQBUF32		_IOWR ('V', 17, struct v4l2_buffer32)
#define VIDIOC_STREAMON32	_IOW  ('V', 18, compat_int_t)
#define VIDIOC_STREAMOFF32	_IOW  ('V', 19, compat_int_t)
#define VIDIOC_ENUMSTD32	_IOWR ('V', 25, struct v4l2_standard32)
#define VIDIOC_ENUMINPUT32	_IOWR ('V', 26, struct v4l2_input32)
/* VIDIOC_S_CTRL is now _IOWR, but was _IOW */
#define VIDIOC_S_CTRL32		_IOW  ('V', 28, struct v4l2_control)
#define VIDIOC_G_INPUT32	_IOR  ('V', 38, compat_int_t)
#define VIDIOC_S_INPUT32	_IOWR ('V', 39, compat_int_t)
#define VIDIOC_TRY_FMT32      	_IOWR ('V', 64, struct v4l2_format32)

enum {
	MaxClips = (~0U-sizeof(struct video_window))/sizeof(struct video_clip)
};

static int do_set_window(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct video_window32 __user *up = compat_ptr(arg);
	struct video_window __user *vw;
	struct video_clip __user *p;
	int nclips;
	u32 n;

	if (get_user(nclips, &up->clipcount))
		return -EFAULT;

	/* Peculiar interface... */
	if (nclips < 0)
		nclips = VIDEO_CLIPMAP_SIZE;

	if (nclips > MaxClips)
		return -ENOMEM;

	vw = compat_alloc_user_space(sizeof(struct video_window) +
				    nclips * sizeof(struct video_clip));

	p = nclips ? (struct video_clip __user *)(vw + 1) : NULL;

	if (get_user(n, &up->x) || put_user(n, &vw->x) ||
	    get_user(n, &up->y) || put_user(n, &vw->y) ||
	    get_user(n, &up->width) || put_user(n, &vw->width) ||
	    get_user(n, &up->height) || put_user(n, &vw->height) ||
	    get_user(n, &up->chromakey) || put_user(n, &vw->chromakey) ||
	    get_user(n, &up->flags) || put_user(n, &vw->flags) ||
	    get_user(n, &up->clipcount) || put_user(n, &vw->clipcount) ||
	    get_user(n, &up->clips) || put_user(p, &vw->clips))
		return -EFAULT;

	if (nclips) {
		struct video_clip32 __user *u = compat_ptr(n);
		int i;
		if (!u)
			return -EINVAL;
		for (i = 0; i < nclips; i++, u++, p++) {
			s32 v;
			if (get_user(v, &u->x) ||
			    put_user(v, &p->x) ||
			    get_user(v, &u->y) ||
			    put_user(v, &p->y) ||
			    get_user(v, &u->width) ||
			    put_user(v, &p->width) ||
			    get_user(v, &u->height) ||
			    put_user(v, &p->height) ||
			    put_user(NULL, &p->next))
				return -EFAULT;
		}
	}

	return native_ioctl(file, VIDIOCSWIN, (unsigned long)p);
}

static int do_video_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	union {
		struct video_tuner vt;
		struct video_buffer vb;
		struct video_window vw;
		struct v4l2_format v2f;
		struct v4l2_buffer v2b;
		struct v4l2_framebuffer v2fb;
		struct v4l2_standard v2s;
		unsigned long vx;
	} karg;
	void __user *up = compat_ptr(arg);
	int compatible_arg = 1;
	int err = 0;

	/* First, convert the command. */
	switch(cmd) {
	case VIDIOCGTUNER32: cmd = VIDIOCGTUNER; break;
	case VIDIOCSTUNER32: cmd = VIDIOCSTUNER; break;
	case VIDIOCGWIN32: cmd = VIDIOCGWIN; break;
	case VIDIOCGFBUF32: cmd = VIDIOCGFBUF; break;
	case VIDIOCSFBUF32: cmd = VIDIOCSFBUF; break;
	case VIDIOCGFREQ32: cmd = VIDIOCGFREQ; break;
	case VIDIOCSFREQ32: cmd = VIDIOCSFREQ; break;
	case VIDIOC_G_FMT32: cmd = VIDIOC_G_FMT; break;
	case VIDIOC_S_FMT32: cmd = VIDIOC_S_FMT; break;
	case VIDIOC_QUERYBUF32: cmd = VIDIOC_QUERYBUF; break;
	case VIDIOC_QBUF32: cmd = VIDIOC_QBUF; break;
	case VIDIOC_DQBUF32: cmd = VIDIOC_DQBUF; break;
	case VIDIOC_STREAMON32: cmd = VIDIOC_STREAMON; break;
	case VIDIOC_STREAMOFF32: cmd = VIDIOC_STREAMOFF; break;
	case VIDIOC_G_FBUF32: cmd = VIDIOC_G_FBUF; break;
	case VIDIOC_OVERLAY32: cmd = VIDIOC_OVERLAY; break;
	case VIDIOC_ENUMSTD32: cmd = VIDIOC_ENUMSTD; break;
	case VIDIOC_ENUMINPUT32: cmd = VIDIOC_ENUMINPUT; break;
	case VIDIOC_S_CTRL32: cmd = VIDIOC_S_CTRL; break;
	case VIDIOC_G_INPUT32: cmd = VIDIOC_G_INPUT; break;
	case VIDIOC_S_INPUT32: cmd = VIDIOC_S_INPUT; break;
	case VIDIOC_TRY_FMT32: cmd = VIDIOC_TRY_FMT; break;
	};

	switch(cmd) {
	case VIDIOCSTUNER:
	case VIDIOCGTUNER:
		err = get_video_tuner32(&karg.vt, up);
		compatible_arg = 0;

		break;

	case VIDIOCSFBUF:
		err = get_video_buffer32(&karg.vb, up);
		compatible_arg = 0;
		break;

	case VIDIOCSFREQ:
	case VIDIOC_S_INPUT:
	case VIDIOC_OVERLAY:
		err = get_user(karg.vx, (u32 __user *)up);
		compatible_arg = 0;
		break;
	};

	case VIDIOC_G_FMT:
	case VIDIOC_S_FMT:
	case VIDIOC_TRY_FMT:
		err = get_v4l2_format32(&karg.v2f, up);
		compatible_arg = 0;
		break;

	case VIDIOC_QUERYBUF:
	case VIDIOC_QBUF:
	case VIDIOC_DQBUF:
		err = get_v4l2_buffer32(&karg.v2b, up);
		compatible_arg = 0;
		break;

	case VIDIOC_ENUMSTD:
		err = get_v4l2_standard32(&karg.v2s, up);
		compatible_arg = 0;
		break;

	case VIDIOCGWIN:
	case VIDIOCGFBUF:
	case VIDIOCGFREQ:
	case VIDIOC_G_FBUF:
	case VIDIOC_G_INPUT:
		compatible_arg = 0;

	if(err)
		goto out;

	if(compatible_arg)
		err = sys_ioctl(fd, cmd, (unsigned long)up);
	else {
		mm_segment_t old_fs = get_fs();

		set_fs(KERNEL_DS);
		err = sys_ioctl(fd, cmd, (unsigned long)&karg);
		set_fs(old_fs);
	}
	if(err == 0) {
		switch(cmd) {
		case VIDIOCGTUNER:
			err = put_video_tuner32(&karg.vt, up);
			break;

		case VIDIOCGWIN:
			err = put_video_window32(&karg.vw, up);
			break;

		case VIDIOCGFBUF:
			err = put_video_buffer32(&karg.vb, up);
			break;

		case VIDIOC_G_FBUF:
			err = put_v4l2_framebuffer32(&karg.v2fb, up);
			break;

		case VIDIOC_G_FMT:
		case VIDIOC_S_FMT:
		case VIDIOC_TRY_FMT:
			err = put_v4l2_format32(&karg.v2f, up);
			break;

		case VIDIOC_QUERYBUF:
		case VIDIOC_QBUF:
		case VIDIOC_DQBUF:
			err = put_v4l2_buffer32(&karg.v2b, up);
			break;

		case VIDIOC_ENUMSTD:
			err = put_v4l2_standard32(&karg.v2s, up);
			break;

		case VIDIOCGFREQ:
		case VIDIOC_G_INPUT:
			err = put_user(((u32)karg.vx), (u32 __user *)up);
			break;
		};
	}
out:
	return err;
}

long v4l_compat_ioctl32(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = -ENOIOCTLCMD;

	if (!file->f_ops->ioctl)
		return ret;

	switch (cmd) {
	case VIDIOCSWIN32:
		ret = do_set_window(file, cmd, arg);
		break;
	case VIDIOCGTUNER32:
	case VIDIOCSTUNER32:
	case VIDIOCGWIN32:
	case VIDIOCGFBUF32:
	case VIDIOCSFBUF32:
	case VIDIOCGFREQ32:
	case VIDIOCSFREQ32:
		ret = do_video_ioctl(file, cmd, arg);
		break;

	/* Little v, the video4linux ioctls (conflict?) */
	case VIDIOCGCAP:
	case VIDIOCGCHAN:
	case VIDIOCSCHAN:
	case VIDIOCGPICT:
	case VIDIOCSPICT:
	case VIDIOCCAPTURE:
	case VIDIOCKEY:
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
	case VIDIOCSYNC:
	case VIDIOCMCAPTURE:
	case VIDIOCGMBUF:
	case VIDIOCGUNIT:
	case VIDIOCGCAPTURE:
	case VIDIOCSCAPTURE:

	/* BTTV specific... */
	case _IOW('v',  BASE_VIDIOCPRIVATE+0, char [256]):
	case _IOR('v',  BASE_VIDIOCPRIVATE+1, char [256]):
	case _IOR('v' , BASE_VIDIOCPRIVATE+2, unsigned int):
	case _IOW('v' , BASE_VIDIOCPRIVATE+3, char [16]): /* struct bttv_pll_info */
	case _IOR('v' , BASE_VIDIOCPRIVATE+4, int):
	case _IOR('v' , BASE_VIDIOCPRIVATE+5, int):
	case _IOR('v' , BASE_VIDIOCPRIVATE+6, int):
	case _IOR('v' , BASE_VIDIOCPRIVATE+7, int):
		ret = native_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
		break;
	}
	return ret;
}
#else
long v4l_compat_ioctl32(struct file *file, unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}
#endif
EXPORT_SYMBOL_GPL(v4l_compat_ioctl32);

MODULE_LICENSE("GPL");
