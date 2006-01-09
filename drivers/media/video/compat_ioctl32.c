#include <linux/config.h>
#include <linux/compat.h>
#include <linux/videodev.h>
#include <linux/module.h>

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
	int i;

	if(get_user(kp->tuner, &up->tuner))
		return -EFAULT;
	for(i = 0; i < 32; i++)
		__get_user(kp->name[i], &up->name[i]);
	__get_user(kp->rangelow, &up->rangelow);
	__get_user(kp->rangehigh, &up->rangehigh);
	__get_user(kp->flags, &up->flags);
	__get_user(kp->mode, &up->mode);
	__get_user(kp->signal, &up->signal);
	return 0;
}

static int put_video_tuner32(struct video_tuner *kp, struct video_tuner32 __user *up)
{
	int i;

	if(put_user(kp->tuner, &up->tuner))
		return -EFAULT;
	for(i = 0; i < 32; i++)
		__put_user(kp->name[i], &up->name[i]);
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

#define VIDIOCGTUNER32		_IOWR('v',4, struct video_tuner32)
#define VIDIOCSTUNER32		_IOW('v',5, struct video_tuner32)
#define VIDIOCGWIN32		_IOR('v',9, struct video_window32)
#define VIDIOCSWIN32		_IOW('v',10, struct video_window32)
#define VIDIOCGFBUF32		_IOR('v',11, struct video_buffer32)
#define VIDIOCSFBUF32		_IOW('v',12, struct video_buffer32)
#define VIDIOCGFREQ32		_IOR('v',14, u32)
#define VIDIOCSFREQ32		_IOW('v',15, u32)

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
		unsigned long vx;
	} karg;
	mm_segment_t old_fs = get_fs();
	void __user *up = compat_ptr(arg);
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
	};

	switch(cmd) {
	case VIDIOCSTUNER:
	case VIDIOCGTUNER:
		err = get_video_tuner32(&karg.vt, up);
		break;

	case VIDIOCSFBUF:
		err = get_video_buffer32(&karg.vb, up);
		break;

	case VIDIOCSFREQ:
		err = get_user(karg.vx, (u32 __user *)up);
		break;
	};
	if(err)
		goto out;

	set_fs(KERNEL_DS);
	err = native_ioctl(file, cmd, (unsigned long)&karg);
	set_fs(old_fs);

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

		case VIDIOCGFREQ:
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
