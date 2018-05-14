/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *	Separated from fs stuff by Arnd Bergmann <arnd@arndb.de>
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2001,2002  Andi Kleen, SuSE Labs
 * Copyright (C) 2003       Pavel Machek (pavel@ucw.cz)
 * Copyright (C) 2005       Philippe De Muyter (phdm@macqel.be)
 * Copyright (C) 2008       Hans Verkuil <hverkuil@xs4all.nl>
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#include <linux/compat.h>
#include <linux/module.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-ioctl.h>

/* Use the same argument order as copy_in_user */
#define assign_in_user(to, from)					\
({									\
	typeof(*from) __assign_tmp;					\
									\
	get_user(__assign_tmp, from) || put_user(__assign_tmp, to);	\
})

static long native_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -ENOIOCTLCMD;

	if (file->f_op->unlocked_ioctl)
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);

	return ret;
}


struct v4l2_clip32 {
	struct v4l2_rect        c;
	compat_caddr_t		next;
};

struct v4l2_window32 {
	struct v4l2_rect        w;
	__u32			field;	/* enum v4l2_field */
	__u32			chromakey;
	compat_caddr_t		clips; /* actually struct v4l2_clip32 * */
	__u32			clipcount;
	compat_caddr_t		bitmap;
	__u8                    global_alpha;
};

static int get_v4l2_window32(struct v4l2_window __user *kp,
			     struct v4l2_window32 __user *up,
			     void __user *aux_buf, u32 aux_space)
{
	struct v4l2_clip32 __user *uclips;
	struct v4l2_clip __user *kclips;
	compat_caddr_t p;
	u32 clipcount;

	if (!access_ok(VERIFY_READ, up, sizeof(*up)) ||
	    copy_in_user(&kp->w, &up->w, sizeof(up->w)) ||
	    assign_in_user(&kp->field, &up->field) ||
	    assign_in_user(&kp->chromakey, &up->chromakey) ||
	    assign_in_user(&kp->global_alpha, &up->global_alpha) ||
	    get_user(clipcount, &up->clipcount) ||
	    put_user(clipcount, &kp->clipcount))
		return -EFAULT;
	if (clipcount > 2048)
		return -EINVAL;
	if (!clipcount)
		return put_user(NULL, &kp->clips);

	if (get_user(p, &up->clips))
		return -EFAULT;
	uclips = compat_ptr(p);
	if (aux_space < clipcount * sizeof(*kclips))
		return -EFAULT;
	kclips = aux_buf;
	if (put_user(kclips, &kp->clips))
		return -EFAULT;

	while (clipcount--) {
		if (copy_in_user(&kclips->c, &uclips->c, sizeof(uclips->c)))
			return -EFAULT;
		if (put_user(clipcount ? kclips + 1 : NULL, &kclips->next))
			return -EFAULT;
		uclips++;
		kclips++;
	}
	return 0;
}

static int put_v4l2_window32(struct v4l2_window __user *kp,
			     struct v4l2_window32 __user *up)
{
	struct v4l2_clip __user *kclips = kp->clips;
	struct v4l2_clip32 __user *uclips;
	compat_caddr_t p;
	u32 clipcount;

	if (copy_in_user(&up->w, &kp->w, sizeof(kp->w)) ||
	    assign_in_user(&up->field, &kp->field) ||
	    assign_in_user(&up->chromakey, &kp->chromakey) ||
	    assign_in_user(&up->global_alpha, &kp->global_alpha) ||
	    get_user(clipcount, &kp->clipcount) ||
	    put_user(clipcount, &up->clipcount))
		return -EFAULT;
	if (!clipcount)
		return 0;

	if (get_user(p, &up->clips))
		return -EFAULT;
	uclips = compat_ptr(p);
	while (clipcount--) {
		if (copy_in_user(&uclips->c, &kclips->c, sizeof(uclips->c)))
			return -EFAULT;
		uclips++;
		kclips++;
	}
	return 0;
}

struct v4l2_format32 {
	__u32	type;	/* enum v4l2_buf_type */
	union {
		struct v4l2_pix_format	pix;
		struct v4l2_pix_format_mplane	pix_mp;
		struct v4l2_window32	win;
		struct v4l2_vbi_format	vbi;
		struct v4l2_sliced_vbi_format	sliced;
		struct v4l2_sdr_format	sdr;
		struct v4l2_meta_format	meta;
		__u8	raw_data[200];        /* user-defined */
	} fmt;
};

/**
 * struct v4l2_create_buffers32 - VIDIOC_CREATE_BUFS32 argument
 * @index:	on return, index of the first created buffer
 * @count:	entry: number of requested buffers,
 *		return: number of created buffers
 * @memory:	buffer memory type
 * @format:	frame format, for which buffers are requested
 * @reserved:	future extensions
 */
struct v4l2_create_buffers32 {
	__u32			index;
	__u32			count;
	__u32			memory;	/* enum v4l2_memory */
	struct v4l2_format32	format;
	__u32			reserved[8];
};

static int __bufsize_v4l2_format(struct v4l2_format32 __user *up, u32 *size)
{
	u32 type;

	if (get_user(type, &up->type))
		return -EFAULT;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY: {
		u32 clipcount;

		if (get_user(clipcount, &up->fmt.win.clipcount))
			return -EFAULT;
		if (clipcount > 2048)
			return -EINVAL;
		*size = clipcount * sizeof(struct v4l2_clip);
		return 0;
	}
	default:
		*size = 0;
		return 0;
	}
}

static int bufsize_v4l2_format(struct v4l2_format32 __user *up, u32 *size)
{
	if (!access_ok(VERIFY_READ, up, sizeof(*up)))
		return -EFAULT;
	return __bufsize_v4l2_format(up, size);
}

static int __get_v4l2_format32(struct v4l2_format __user *kp,
			       struct v4l2_format32 __user *up,
			       void __user *aux_buf, u32 aux_space)
{
	u32 type;

	if (get_user(type, &up->type) || put_user(type, &kp->type))
		return -EFAULT;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return copy_in_user(&kp->fmt.pix, &up->fmt.pix,
				    sizeof(kp->fmt.pix)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return copy_in_user(&kp->fmt.pix_mp, &up->fmt.pix_mp,
				    sizeof(kp->fmt.pix_mp)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		return get_v4l2_window32(&kp->fmt.win, &up->fmt.win,
					 aux_buf, aux_space);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		return copy_in_user(&kp->fmt.vbi, &up->fmt.vbi,
				    sizeof(kp->fmt.vbi)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		return copy_in_user(&kp->fmt.sliced, &up->fmt.sliced,
				    sizeof(kp->fmt.sliced)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SDR_CAPTURE:
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		return copy_in_user(&kp->fmt.sdr, &up->fmt.sdr,
				    sizeof(kp->fmt.sdr)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_META_CAPTURE:
		return copy_in_user(&kp->fmt.meta, &up->fmt.meta,
				    sizeof(kp->fmt.meta)) ? -EFAULT : 0;
	default:
		return -EINVAL;
	}
}

static int get_v4l2_format32(struct v4l2_format __user *kp,
			     struct v4l2_format32 __user *up,
			     void __user *aux_buf, u32 aux_space)
{
	if (!access_ok(VERIFY_READ, up, sizeof(*up)))
		return -EFAULT;
	return __get_v4l2_format32(kp, up, aux_buf, aux_space);
}

static int bufsize_v4l2_create(struct v4l2_create_buffers32 __user *up,
			       u32 *size)
{
	if (!access_ok(VERIFY_READ, up, sizeof(*up)))
		return -EFAULT;
	return __bufsize_v4l2_format(&up->format, size);
}

static int get_v4l2_create32(struct v4l2_create_buffers __user *kp,
			     struct v4l2_create_buffers32 __user *up,
			     void __user *aux_buf, u32 aux_space)
{
	if (!access_ok(VERIFY_READ, up, sizeof(*up)) ||
	    copy_in_user(kp, up,
			 offsetof(struct v4l2_create_buffers32, format)))
		return -EFAULT;
	return __get_v4l2_format32(&kp->format, &up->format,
				   aux_buf, aux_space);
}

static int __put_v4l2_format32(struct v4l2_format __user *kp,
			       struct v4l2_format32 __user *up)
{
	u32 type;

	if (get_user(type, &kp->type))
		return -EFAULT;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return copy_in_user(&up->fmt.pix, &kp->fmt.pix,
				    sizeof(kp->fmt.pix)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return copy_in_user(&up->fmt.pix_mp, &kp->fmt.pix_mp,
				    sizeof(kp->fmt.pix_mp)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		return put_v4l2_window32(&kp->fmt.win, &up->fmt.win);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		return copy_in_user(&up->fmt.vbi, &kp->fmt.vbi,
				    sizeof(kp->fmt.vbi)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		return copy_in_user(&up->fmt.sliced, &kp->fmt.sliced,
				    sizeof(kp->fmt.sliced)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SDR_CAPTURE:
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		return copy_in_user(&up->fmt.sdr, &kp->fmt.sdr,
				    sizeof(kp->fmt.sdr)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_META_CAPTURE:
		return copy_in_user(&up->fmt.meta, &kp->fmt.meta,
				    sizeof(kp->fmt.meta)) ? -EFAULT : 0;
	default:
		return -EINVAL;
	}
}

static int put_v4l2_format32(struct v4l2_format __user *kp,
			     struct v4l2_format32 __user *up)
{
	if (!access_ok(VERIFY_WRITE, up, sizeof(*up)))
		return -EFAULT;
	return __put_v4l2_format32(kp, up);
}

static int put_v4l2_create32(struct v4l2_create_buffers __user *kp,
			     struct v4l2_create_buffers32 __user *up)
{
	if (!access_ok(VERIFY_WRITE, up, sizeof(*up)) ||
	    copy_in_user(up, kp,
			 offsetof(struct v4l2_create_buffers32, format)) ||
	    copy_in_user(up->reserved, kp->reserved, sizeof(kp->reserved)))
		return -EFAULT;
	return __put_v4l2_format32(&kp->format, &up->format);
}

struct v4l2_standard32 {
	__u32		     index;
	compat_u64	     id;
	__u8		     name[24];
	struct v4l2_fract    frameperiod; /* Frames, not fields */
	__u32		     framelines;
	__u32		     reserved[4];
};

static int get_v4l2_standard32(struct v4l2_standard __user *kp,
			       struct v4l2_standard32 __user *up)
{
	/* other fields are not set by the user, nor used by the driver */
	if (!access_ok(VERIFY_READ, up, sizeof(*up)) ||
	    assign_in_user(&kp->index, &up->index))
		return -EFAULT;
	return 0;
}

static int put_v4l2_standard32(struct v4l2_standard __user *kp,
			       struct v4l2_standard32 __user *up)
{
	if (!access_ok(VERIFY_WRITE, up, sizeof(*up)) ||
	    assign_in_user(&up->index, &kp->index) ||
	    assign_in_user(&up->id, &kp->id) ||
	    copy_in_user(up->name, kp->name, sizeof(up->name)) ||
	    copy_in_user(&up->frameperiod, &kp->frameperiod,
			 sizeof(up->frameperiod)) ||
	    assign_in_user(&up->framelines, &kp->framelines) ||
	    copy_in_user(up->reserved, kp->reserved, sizeof(up->reserved)))
		return -EFAULT;
	return 0;
}

struct v4l2_plane32 {
	__u32			bytesused;
	__u32			length;
	union {
		__u32		mem_offset;
		compat_long_t	userptr;
		__s32		fd;
	} m;
	__u32			data_offset;
	__u32			reserved[11];
};

struct v4l2_buffer32 {
	__u32			index;
	__u32			type;	/* enum v4l2_buf_type */
	__u32			bytesused;
	__u32			flags;
	__u32			field;	/* enum v4l2_field */
	struct compat_timeval	timestamp;
	struct v4l2_timecode	timecode;
	__u32			sequence;

	/* memory location */
	__u32			memory;	/* enum v4l2_memory */
	union {
		__u32           offset;
		compat_long_t   userptr;
		compat_caddr_t  planes;
		__s32		fd;
	} m;
	__u32			length;
	__u32			reserved2;
	__u32			reserved;
};

static int get_v4l2_plane32(struct v4l2_plane __user *up,
			    struct v4l2_plane32 __user *up32,
			    enum v4l2_memory memory)
{
	compat_ulong_t p;

	if (copy_in_user(up, up32, 2 * sizeof(__u32)) ||
	    copy_in_user(&up->data_offset, &up32->data_offset,
			 sizeof(up->data_offset)))
		return -EFAULT;

	switch (memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_OVERLAY:
		if (copy_in_user(&up->m.mem_offset, &up32->m.mem_offset,
				 sizeof(up32->m.mem_offset)))
			return -EFAULT;
		break;
	case V4L2_MEMORY_USERPTR:
		if (get_user(p, &up32->m.userptr) ||
		    put_user((unsigned long)compat_ptr(p), &up->m.userptr))
			return -EFAULT;
		break;
	case V4L2_MEMORY_DMABUF:
		if (copy_in_user(&up->m.fd, &up32->m.fd, sizeof(up32->m.fd)))
			return -EFAULT;
		break;
	}

	return 0;
}

static int put_v4l2_plane32(struct v4l2_plane __user *up,
			    struct v4l2_plane32 __user *up32,
			    enum v4l2_memory memory)
{
	unsigned long p;

	if (copy_in_user(up32, up, 2 * sizeof(__u32)) ||
	    copy_in_user(&up32->data_offset, &up->data_offset,
			 sizeof(up->data_offset)))
		return -EFAULT;

	switch (memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_OVERLAY:
		if (copy_in_user(&up32->m.mem_offset, &up->m.mem_offset,
				 sizeof(up->m.mem_offset)))
			return -EFAULT;
		break;
	case V4L2_MEMORY_USERPTR:
		if (get_user(p, &up->m.userptr) ||
		    put_user((compat_ulong_t)ptr_to_compat((__force void *)p),
			     &up32->m.userptr))
			return -EFAULT;
		break;
	case V4L2_MEMORY_DMABUF:
		if (copy_in_user(&up32->m.fd, &up->m.fd, sizeof(up->m.fd)))
			return -EFAULT;
		break;
	}

	return 0;
}

static int bufsize_v4l2_buffer(struct v4l2_buffer32 __user *up, u32 *size)
{
	u32 type;
	u32 length;

	if (!access_ok(VERIFY_READ, up, sizeof(*up)) ||
	    get_user(type, &up->type) ||
	    get_user(length, &up->length))
		return -EFAULT;

	if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
		if (length > VIDEO_MAX_PLANES)
			return -EINVAL;

		/*
		 * We don't really care if userspace decides to kill itself
		 * by passing a very big length value
		 */
		*size = length * sizeof(struct v4l2_plane);
	} else {
		*size = 0;
	}
	return 0;
}

static int get_v4l2_buffer32(struct v4l2_buffer __user *kp,
			     struct v4l2_buffer32 __user *up,
			     void __user *aux_buf, u32 aux_space)
{
	u32 type;
	u32 length;
	enum v4l2_memory memory;
	struct v4l2_plane32 __user *uplane32;
	struct v4l2_plane __user *uplane;
	compat_caddr_t p;
	int ret;

	if (!access_ok(VERIFY_READ, up, sizeof(*up)) ||
	    assign_in_user(&kp->index, &up->index) ||
	    get_user(type, &up->type) ||
	    put_user(type, &kp->type) ||
	    assign_in_user(&kp->flags, &up->flags) ||
	    get_user(memory, &up->memory) ||
	    put_user(memory, &kp->memory) ||
	    get_user(length, &up->length) ||
	    put_user(length, &kp->length))
		return -EFAULT;

	if (V4L2_TYPE_IS_OUTPUT(type))
		if (assign_in_user(&kp->bytesused, &up->bytesused) ||
		    assign_in_user(&kp->field, &up->field) ||
		    assign_in_user(&kp->timestamp.tv_sec,
				   &up->timestamp.tv_sec) ||
		    assign_in_user(&kp->timestamp.tv_usec,
				   &up->timestamp.tv_usec))
			return -EFAULT;

	if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
		u32 num_planes = length;

		if (num_planes == 0) {
			/*
			 * num_planes == 0 is legal, e.g. when userspace doesn't
			 * need planes array on DQBUF
			 */
			return put_user(NULL, &kp->m.planes);
		}
		if (num_planes > VIDEO_MAX_PLANES)
			return -EINVAL;

		if (get_user(p, &up->m.planes))
			return -EFAULT;

		uplane32 = compat_ptr(p);
		if (!access_ok(VERIFY_READ, uplane32,
			       num_planes * sizeof(*uplane32)))
			return -EFAULT;

		/*
		 * We don't really care if userspace decides to kill itself
		 * by passing a very big num_planes value
		 */
		if (aux_space < num_planes * sizeof(*uplane))
			return -EFAULT;

		uplane = aux_buf;
		if (put_user((__force struct v4l2_plane *)uplane,
			     &kp->m.planes))
			return -EFAULT;

		while (num_planes--) {
			ret = get_v4l2_plane32(uplane, uplane32, memory);
			if (ret)
				return ret;
			uplane++;
			uplane32++;
		}
	} else {
		switch (memory) {
		case V4L2_MEMORY_MMAP:
		case V4L2_MEMORY_OVERLAY:
			if (assign_in_user(&kp->m.offset, &up->m.offset))
				return -EFAULT;
			break;
		case V4L2_MEMORY_USERPTR: {
			compat_ulong_t userptr;

			if (get_user(userptr, &up->m.userptr) ||
			    put_user((unsigned long)compat_ptr(userptr),
				     &kp->m.userptr))
				return -EFAULT;
			break;
		}
		case V4L2_MEMORY_DMABUF:
			if (assign_in_user(&kp->m.fd, &up->m.fd))
				return -EFAULT;
			break;
		}
	}

	return 0;
}

static int put_v4l2_buffer32(struct v4l2_buffer __user *kp,
			     struct v4l2_buffer32 __user *up)
{
	u32 type;
	u32 length;
	enum v4l2_memory memory;
	struct v4l2_plane32 __user *uplane32;
	struct v4l2_plane __user *uplane;
	compat_caddr_t p;
	int ret;

	if (!access_ok(VERIFY_WRITE, up, sizeof(*up)) ||
	    assign_in_user(&up->index, &kp->index) ||
	    get_user(type, &kp->type) ||
	    put_user(type, &up->type) ||
	    assign_in_user(&up->flags, &kp->flags) ||
	    get_user(memory, &kp->memory) ||
	    put_user(memory, &up->memory))
		return -EFAULT;

	if (assign_in_user(&up->bytesused, &kp->bytesused) ||
	    assign_in_user(&up->field, &kp->field) ||
	    assign_in_user(&up->timestamp.tv_sec, &kp->timestamp.tv_sec) ||
	    assign_in_user(&up->timestamp.tv_usec, &kp->timestamp.tv_usec) ||
	    copy_in_user(&up->timecode, &kp->timecode, sizeof(kp->timecode)) ||
	    assign_in_user(&up->sequence, &kp->sequence) ||
	    assign_in_user(&up->reserved2, &kp->reserved2) ||
	    assign_in_user(&up->reserved, &kp->reserved) ||
	    get_user(length, &kp->length) ||
	    put_user(length, &up->length))
		return -EFAULT;

	if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
		u32 num_planes = length;

		if (num_planes == 0)
			return 0;

		if (get_user(uplane, ((__force struct v4l2_plane __user **)&kp->m.planes)))
			return -EFAULT;
		if (get_user(p, &up->m.planes))
			return -EFAULT;
		uplane32 = compat_ptr(p);

		while (num_planes--) {
			ret = put_v4l2_plane32(uplane, uplane32, memory);
			if (ret)
				return ret;
			++uplane;
			++uplane32;
		}
	} else {
		switch (memory) {
		case V4L2_MEMORY_MMAP:
		case V4L2_MEMORY_OVERLAY:
			if (assign_in_user(&up->m.offset, &kp->m.offset))
				return -EFAULT;
			break;
		case V4L2_MEMORY_USERPTR:
			if (assign_in_user(&up->m.userptr, &kp->m.userptr))
				return -EFAULT;
			break;
		case V4L2_MEMORY_DMABUF:
			if (assign_in_user(&up->m.fd, &kp->m.fd))
				return -EFAULT;
			break;
		}
	}

	return 0;
}

struct v4l2_framebuffer32 {
	__u32			capability;
	__u32			flags;
	compat_caddr_t		base;
	struct {
		__u32		width;
		__u32		height;
		__u32		pixelformat;
		__u32		field;
		__u32		bytesperline;
		__u32		sizeimage;
		__u32		colorspace;
		__u32		priv;
	} fmt;
};

static int get_v4l2_framebuffer32(struct v4l2_framebuffer __user *kp,
				  struct v4l2_framebuffer32 __user *up)
{
	compat_caddr_t tmp;

	if (!access_ok(VERIFY_READ, up, sizeof(*up)) ||
	    get_user(tmp, &up->base) ||
	    put_user((__force void *)compat_ptr(tmp), &kp->base) ||
	    assign_in_user(&kp->capability, &up->capability) ||
	    assign_in_user(&kp->flags, &up->flags) ||
	    copy_in_user(&kp->fmt, &up->fmt, sizeof(kp->fmt)))
		return -EFAULT;
	return 0;
}

static int put_v4l2_framebuffer32(struct v4l2_framebuffer __user *kp,
				  struct v4l2_framebuffer32 __user *up)
{
	void *base;

	if (!access_ok(VERIFY_WRITE, up, sizeof(*up)) ||
	    get_user(base, &kp->base) ||
	    put_user(ptr_to_compat(base), &up->base) ||
	    assign_in_user(&up->capability, &kp->capability) ||
	    assign_in_user(&up->flags, &kp->flags) ||
	    copy_in_user(&up->fmt, &kp->fmt, sizeof(kp->fmt)))
		return -EFAULT;
	return 0;
}

struct v4l2_input32 {
	__u32	     index;		/*  Which input */
	__u8	     name[32];		/*  Label */
	__u32	     type;		/*  Type of input */
	__u32	     audioset;		/*  Associated audios (bitfield) */
	__u32        tuner;             /*  Associated tuner */
	compat_u64   std;
	__u32	     status;
	__u32	     capabilities;
	__u32	     reserved[3];
};

/*
 * The 64-bit v4l2_input struct has extra padding at the end of the struct.
 * Otherwise it is identical to the 32-bit version.
 */
static inline int get_v4l2_input32(struct v4l2_input __user *kp,
				   struct v4l2_input32 __user *up)
{
	if (copy_in_user(kp, up, sizeof(*up)))
		return -EFAULT;
	return 0;
}

static inline int put_v4l2_input32(struct v4l2_input __user *kp,
				   struct v4l2_input32 __user *up)
{
	if (copy_in_user(up, kp, sizeof(*up)))
		return -EFAULT;
	return 0;
}

struct v4l2_ext_controls32 {
	__u32 which;
	__u32 count;
	__u32 error_idx;
	__u32 reserved[2];
	compat_caddr_t controls; /* actually struct v4l2_ext_control32 * */
};

struct v4l2_ext_control32 {
	__u32 id;
	__u32 size;
	__u32 reserved2[1];
	union {
		__s32 value;
		__s64 value64;
		compat_caddr_t string; /* actually char * */
	};
} __attribute__ ((packed));

/* Return true if this control is a pointer type. */
static inline bool ctrl_is_pointer(struct file *file, u32 id)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_fh *fh = NULL;
	struct v4l2_ctrl_handler *hdl = NULL;
	struct v4l2_query_ext_ctrl qec = { id };
	const struct v4l2_ioctl_ops *ops = vdev->ioctl_ops;

	if (test_bit(V4L2_FL_USES_V4L2_FH, &vdev->flags))
		fh = file->private_data;

	if (fh && fh->ctrl_handler)
		hdl = fh->ctrl_handler;
	else if (vdev->ctrl_handler)
		hdl = vdev->ctrl_handler;

	if (hdl) {
		struct v4l2_ctrl *ctrl = v4l2_ctrl_find(hdl, id);

		return ctrl && ctrl->is_ptr;
	}

	if (!ops || !ops->vidioc_query_ext_ctrl)
		return false;

	return !ops->vidioc_query_ext_ctrl(file, fh, &qec) &&
		(qec.flags & V4L2_CTRL_FLAG_HAS_PAYLOAD);
}

static int bufsize_v4l2_ext_controls(struct v4l2_ext_controls32 __user *up,
				     u32 *size)
{
	u32 count;

	if (!access_ok(VERIFY_READ, up, sizeof(*up)) ||
	    get_user(count, &up->count))
		return -EFAULT;
	if (count > V4L2_CID_MAX_CTRLS)
		return -EINVAL;
	*size = count * sizeof(struct v4l2_ext_control);
	return 0;
}

static int get_v4l2_ext_controls32(struct file *file,
				   struct v4l2_ext_controls __user *kp,
				   struct v4l2_ext_controls32 __user *up,
				   void __user *aux_buf, u32 aux_space)
{
	struct v4l2_ext_control32 __user *ucontrols;
	struct v4l2_ext_control __user *kcontrols;
	u32 count;
	u32 n;
	compat_caddr_t p;

	if (!access_ok(VERIFY_READ, up, sizeof(*up)) ||
	    assign_in_user(&kp->which, &up->which) ||
	    get_user(count, &up->count) ||
	    put_user(count, &kp->count) ||
	    assign_in_user(&kp->error_idx, &up->error_idx) ||
	    copy_in_user(kp->reserved, up->reserved, sizeof(kp->reserved)))
		return -EFAULT;

	if (count == 0)
		return put_user(NULL, &kp->controls);
	if (count > V4L2_CID_MAX_CTRLS)
		return -EINVAL;
	if (get_user(p, &up->controls))
		return -EFAULT;
	ucontrols = compat_ptr(p);
	if (!access_ok(VERIFY_READ, ucontrols, count * sizeof(*ucontrols)))
		return -EFAULT;
	if (aux_space < count * sizeof(*kcontrols))
		return -EFAULT;
	kcontrols = aux_buf;
	if (put_user((__force struct v4l2_ext_control *)kcontrols,
		     &kp->controls))
		return -EFAULT;

	for (n = 0; n < count; n++) {
		u32 id;

		if (copy_in_user(kcontrols, ucontrols, sizeof(*ucontrols)))
			return -EFAULT;

		if (get_user(id, &kcontrols->id))
			return -EFAULT;

		if (ctrl_is_pointer(file, id)) {
			void __user *s;

			if (get_user(p, &ucontrols->string))
				return -EFAULT;
			s = compat_ptr(p);
			if (put_user(s, &kcontrols->string))
				return -EFAULT;
		}
		ucontrols++;
		kcontrols++;
	}
	return 0;
}

static int put_v4l2_ext_controls32(struct file *file,
				   struct v4l2_ext_controls __user *kp,
				   struct v4l2_ext_controls32 __user *up)
{
	struct v4l2_ext_control32 __user *ucontrols;
	struct v4l2_ext_control __user *kcontrols;
	u32 count;
	u32 n;
	compat_caddr_t p;

	if (!access_ok(VERIFY_WRITE, up, sizeof(*up)) ||
	    assign_in_user(&up->which, &kp->which) ||
	    get_user(count, &kp->count) ||
	    put_user(count, &up->count) ||
	    assign_in_user(&up->error_idx, &kp->error_idx) ||
	    copy_in_user(up->reserved, kp->reserved, sizeof(up->reserved)) ||
	    get_user(kcontrols, &kp->controls))
		return -EFAULT;

	if (!count)
		return 0;
	if (get_user(p, &up->controls))
		return -EFAULT;
	ucontrols = compat_ptr(p);
	if (!access_ok(VERIFY_WRITE, ucontrols, count * sizeof(*ucontrols)))
		return -EFAULT;

	for (n = 0; n < count; n++) {
		unsigned int size = sizeof(*ucontrols);
		u32 id;

		if (get_user(id, &kcontrols->id) ||
		    put_user(id, &ucontrols->id) ||
		    assign_in_user(&ucontrols->size, &kcontrols->size) ||
		    copy_in_user(&ucontrols->reserved2, &kcontrols->reserved2,
				 sizeof(ucontrols->reserved2)))
			return -EFAULT;

		/*
		 * Do not modify the pointer when copying a pointer control.
		 * The contents of the pointer was changed, not the pointer
		 * itself.
		 */
		if (ctrl_is_pointer(file, id))
			size -= sizeof(ucontrols->value64);

		if (copy_in_user(ucontrols, kcontrols, size))
			return -EFAULT;

		ucontrols++;
		kcontrols++;
	}
	return 0;
}

struct v4l2_event32 {
	__u32				type;
	union {
		compat_s64		value64;
		__u8			data[64];
	} u;
	__u32				pending;
	__u32				sequence;
	struct compat_timespec		timestamp;
	__u32				id;
	__u32				reserved[8];
};

static int put_v4l2_event32(struct v4l2_event __user *kp,
			    struct v4l2_event32 __user *up)
{
	if (!access_ok(VERIFY_WRITE, up, sizeof(*up)) ||
	    assign_in_user(&up->type, &kp->type) ||
	    copy_in_user(&up->u, &kp->u, sizeof(kp->u)) ||
	    assign_in_user(&up->pending, &kp->pending) ||
	    assign_in_user(&up->sequence, &kp->sequence) ||
	    assign_in_user(&up->timestamp.tv_sec, &kp->timestamp.tv_sec) ||
	    assign_in_user(&up->timestamp.tv_nsec, &kp->timestamp.tv_nsec) ||
	    assign_in_user(&up->id, &kp->id) ||
	    copy_in_user(up->reserved, kp->reserved, sizeof(up->reserved)))
		return -EFAULT;
	return 0;
}

struct v4l2_edid32 {
	__u32 pad;
	__u32 start_block;
	__u32 blocks;
	__u32 reserved[5];
	compat_caddr_t edid;
};

static int get_v4l2_edid32(struct v4l2_edid __user *kp,
			   struct v4l2_edid32 __user *up)
{
	compat_uptr_t tmp;

	if (!access_ok(VERIFY_READ, up, sizeof(*up)) ||
	    assign_in_user(&kp->pad, &up->pad) ||
	    assign_in_user(&kp->start_block, &up->start_block) ||
	    assign_in_user(&kp->blocks, &up->blocks) ||
	    get_user(tmp, &up->edid) ||
	    put_user(compat_ptr(tmp), &kp->edid) ||
	    copy_in_user(kp->reserved, up->reserved, sizeof(kp->reserved)))
		return -EFAULT;
	return 0;
}

static int put_v4l2_edid32(struct v4l2_edid __user *kp,
			   struct v4l2_edid32 __user *up)
{
	void *edid;

	if (!access_ok(VERIFY_WRITE, up, sizeof(*up)) ||
	    assign_in_user(&up->pad, &kp->pad) ||
	    assign_in_user(&up->start_block, &kp->start_block) ||
	    assign_in_user(&up->blocks, &kp->blocks) ||
	    get_user(edid, &kp->edid) ||
	    put_user(ptr_to_compat(edid), &up->edid) ||
	    copy_in_user(up->reserved, kp->reserved, sizeof(up->reserved)))
		return -EFAULT;
	return 0;
}


#define VIDIOC_G_FMT32		_IOWR('V',  4, struct v4l2_format32)
#define VIDIOC_S_FMT32		_IOWR('V',  5, struct v4l2_format32)
#define VIDIOC_QUERYBUF32	_IOWR('V',  9, struct v4l2_buffer32)
#define VIDIOC_G_FBUF32		_IOR ('V', 10, struct v4l2_framebuffer32)
#define VIDIOC_S_FBUF32		_IOW ('V', 11, struct v4l2_framebuffer32)
#define VIDIOC_QBUF32		_IOWR('V', 15, struct v4l2_buffer32)
#define VIDIOC_DQBUF32		_IOWR('V', 17, struct v4l2_buffer32)
#define VIDIOC_ENUMSTD32	_IOWR('V', 25, struct v4l2_standard32)
#define VIDIOC_ENUMINPUT32	_IOWR('V', 26, struct v4l2_input32)
#define VIDIOC_G_EDID32		_IOWR('V', 40, struct v4l2_edid32)
#define VIDIOC_S_EDID32		_IOWR('V', 41, struct v4l2_edid32)
#define VIDIOC_TRY_FMT32	_IOWR('V', 64, struct v4l2_format32)
#define VIDIOC_G_EXT_CTRLS32    _IOWR('V', 71, struct v4l2_ext_controls32)
#define VIDIOC_S_EXT_CTRLS32    _IOWR('V', 72, struct v4l2_ext_controls32)
#define VIDIOC_TRY_EXT_CTRLS32  _IOWR('V', 73, struct v4l2_ext_controls32)
#define	VIDIOC_DQEVENT32	_IOR ('V', 89, struct v4l2_event32)
#define VIDIOC_CREATE_BUFS32	_IOWR('V', 92, struct v4l2_create_buffers32)
#define VIDIOC_PREPARE_BUF32	_IOWR('V', 93, struct v4l2_buffer32)

#define VIDIOC_OVERLAY32	_IOW ('V', 14, s32)
#define VIDIOC_STREAMON32	_IOW ('V', 18, s32)
#define VIDIOC_STREAMOFF32	_IOW ('V', 19, s32)
#define VIDIOC_G_INPUT32	_IOR ('V', 38, s32)
#define VIDIOC_S_INPUT32	_IOWR('V', 39, s32)
#define VIDIOC_G_OUTPUT32	_IOR ('V', 46, s32)
#define VIDIOC_S_OUTPUT32	_IOWR('V', 47, s32)

static int alloc_userspace(unsigned int size, u32 aux_space,
			   void __user **up_native)
{
	*up_native = compat_alloc_user_space(size + aux_space);
	if (!*up_native)
		return -ENOMEM;
	if (clear_user(*up_native, size))
		return -EFAULT;
	return 0;
}

static long do_video_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	void __user *up_native = NULL;
	void __user *aux_buf;
	u32 aux_space;
	int compatible_arg = 1;
	long err = 0;

	/* First, convert the command. */
	switch (cmd) {
	case VIDIOC_G_FMT32: cmd = VIDIOC_G_FMT; break;
	case VIDIOC_S_FMT32: cmd = VIDIOC_S_FMT; break;
	case VIDIOC_QUERYBUF32: cmd = VIDIOC_QUERYBUF; break;
	case VIDIOC_G_FBUF32: cmd = VIDIOC_G_FBUF; break;
	case VIDIOC_S_FBUF32: cmd = VIDIOC_S_FBUF; break;
	case VIDIOC_QBUF32: cmd = VIDIOC_QBUF; break;
	case VIDIOC_DQBUF32: cmd = VIDIOC_DQBUF; break;
	case VIDIOC_ENUMSTD32: cmd = VIDIOC_ENUMSTD; break;
	case VIDIOC_ENUMINPUT32: cmd = VIDIOC_ENUMINPUT; break;
	case VIDIOC_TRY_FMT32: cmd = VIDIOC_TRY_FMT; break;
	case VIDIOC_G_EXT_CTRLS32: cmd = VIDIOC_G_EXT_CTRLS; break;
	case VIDIOC_S_EXT_CTRLS32: cmd = VIDIOC_S_EXT_CTRLS; break;
	case VIDIOC_TRY_EXT_CTRLS32: cmd = VIDIOC_TRY_EXT_CTRLS; break;
	case VIDIOC_DQEVENT32: cmd = VIDIOC_DQEVENT; break;
	case VIDIOC_OVERLAY32: cmd = VIDIOC_OVERLAY; break;
	case VIDIOC_STREAMON32: cmd = VIDIOC_STREAMON; break;
	case VIDIOC_STREAMOFF32: cmd = VIDIOC_STREAMOFF; break;
	case VIDIOC_G_INPUT32: cmd = VIDIOC_G_INPUT; break;
	case VIDIOC_S_INPUT32: cmd = VIDIOC_S_INPUT; break;
	case VIDIOC_G_OUTPUT32: cmd = VIDIOC_G_OUTPUT; break;
	case VIDIOC_S_OUTPUT32: cmd = VIDIOC_S_OUTPUT; break;
	case VIDIOC_CREATE_BUFS32: cmd = VIDIOC_CREATE_BUFS; break;
	case VIDIOC_PREPARE_BUF32: cmd = VIDIOC_PREPARE_BUF; break;
	case VIDIOC_G_EDID32: cmd = VIDIOC_G_EDID; break;
	case VIDIOC_S_EDID32: cmd = VIDIOC_S_EDID; break;
	}

	switch (cmd) {
	case VIDIOC_OVERLAY:
	case VIDIOC_STREAMON:
	case VIDIOC_STREAMOFF:
	case VIDIOC_S_INPUT:
	case VIDIOC_S_OUTPUT:
		err = alloc_userspace(sizeof(unsigned int), 0, &up_native);
		if (!err && assign_in_user((unsigned int __user *)up_native,
					   (compat_uint_t __user *)up))
			err = -EFAULT;
		compatible_arg = 0;
		break;

	case VIDIOC_G_INPUT:
	case VIDIOC_G_OUTPUT:
		err = alloc_userspace(sizeof(unsigned int), 0, &up_native);
		compatible_arg = 0;
		break;

	case VIDIOC_G_EDID:
	case VIDIOC_S_EDID:
		err = alloc_userspace(sizeof(struct v4l2_edid), 0, &up_native);
		if (!err)
			err = get_v4l2_edid32(up_native, up);
		compatible_arg = 0;
		break;

	case VIDIOC_G_FMT:
	case VIDIOC_S_FMT:
	case VIDIOC_TRY_FMT:
		err = bufsize_v4l2_format(up, &aux_space);
		if (!err)
			err = alloc_userspace(sizeof(struct v4l2_format),
					      aux_space, &up_native);
		if (!err) {
			aux_buf = up_native + sizeof(struct v4l2_format);
			err = get_v4l2_format32(up_native, up,
						aux_buf, aux_space);
		}
		compatible_arg = 0;
		break;

	case VIDIOC_CREATE_BUFS:
		err = bufsize_v4l2_create(up, &aux_space);
		if (!err)
			err = alloc_userspace(sizeof(struct v4l2_create_buffers),
					      aux_space, &up_native);
		if (!err) {
			aux_buf = up_native + sizeof(struct v4l2_create_buffers);
			err = get_v4l2_create32(up_native, up,
						aux_buf, aux_space);
		}
		compatible_arg = 0;
		break;

	case VIDIOC_PREPARE_BUF:
	case VIDIOC_QUERYBUF:
	case VIDIOC_QBUF:
	case VIDIOC_DQBUF:
		err = bufsize_v4l2_buffer(up, &aux_space);
		if (!err)
			err = alloc_userspace(sizeof(struct v4l2_buffer),
					      aux_space, &up_native);
		if (!err) {
			aux_buf = up_native + sizeof(struct v4l2_buffer);
			err = get_v4l2_buffer32(up_native, up,
						aux_buf, aux_space);
		}
		compatible_arg = 0;
		break;

	case VIDIOC_S_FBUF:
		err = alloc_userspace(sizeof(struct v4l2_framebuffer), 0,
				      &up_native);
		if (!err)
			err = get_v4l2_framebuffer32(up_native, up);
		compatible_arg = 0;
		break;

	case VIDIOC_G_FBUF:
		err = alloc_userspace(sizeof(struct v4l2_framebuffer), 0,
				      &up_native);
		compatible_arg = 0;
		break;

	case VIDIOC_ENUMSTD:
		err = alloc_userspace(sizeof(struct v4l2_standard), 0,
				      &up_native);
		if (!err)
			err = get_v4l2_standard32(up_native, up);
		compatible_arg = 0;
		break;

	case VIDIOC_ENUMINPUT:
		err = alloc_userspace(sizeof(struct v4l2_input), 0, &up_native);
		if (!err)
			err = get_v4l2_input32(up_native, up);
		compatible_arg = 0;
		break;

	case VIDIOC_G_EXT_CTRLS:
	case VIDIOC_S_EXT_CTRLS:
	case VIDIOC_TRY_EXT_CTRLS:
		err = bufsize_v4l2_ext_controls(up, &aux_space);
		if (!err)
			err = alloc_userspace(sizeof(struct v4l2_ext_controls),
					      aux_space, &up_native);
		if (!err) {
			aux_buf = up_native + sizeof(struct v4l2_ext_controls);
			err = get_v4l2_ext_controls32(file, up_native, up,
						      aux_buf, aux_space);
		}
		compatible_arg = 0;
		break;
	case VIDIOC_DQEVENT:
		err = alloc_userspace(sizeof(struct v4l2_event), 0, &up_native);
		compatible_arg = 0;
		break;
	}
	if (err)
		return err;

	if (compatible_arg)
		err = native_ioctl(file, cmd, (unsigned long)up);
	else
		err = native_ioctl(file, cmd, (unsigned long)up_native);

	if (err == -ENOTTY)
		return err;

	/*
	 * Special case: even after an error we need to put the
	 * results back for these ioctls since the error_idx will
	 * contain information on which control failed.
	 */
	switch (cmd) {
	case VIDIOC_G_EXT_CTRLS:
	case VIDIOC_S_EXT_CTRLS:
	case VIDIOC_TRY_EXT_CTRLS:
		if (put_v4l2_ext_controls32(file, up_native, up))
			err = -EFAULT;
		break;
	case VIDIOC_S_EDID:
		if (put_v4l2_edid32(up_native, up))
			err = -EFAULT;
		break;
	}
	if (err)
		return err;

	switch (cmd) {
	case VIDIOC_S_INPUT:
	case VIDIOC_S_OUTPUT:
	case VIDIOC_G_INPUT:
	case VIDIOC_G_OUTPUT:
		if (assign_in_user((compat_uint_t __user *)up,
				   ((unsigned int __user *)up_native)))
			err = -EFAULT;
		break;

	case VIDIOC_G_FBUF:
		err = put_v4l2_framebuffer32(up_native, up);
		break;

	case VIDIOC_DQEVENT:
		err = put_v4l2_event32(up_native, up);
		break;

	case VIDIOC_G_EDID:
		err = put_v4l2_edid32(up_native, up);
		break;

	case VIDIOC_G_FMT:
	case VIDIOC_S_FMT:
	case VIDIOC_TRY_FMT:
		err = put_v4l2_format32(up_native, up);
		break;

	case VIDIOC_CREATE_BUFS:
		err = put_v4l2_create32(up_native, up);
		break;

	case VIDIOC_PREPARE_BUF:
	case VIDIOC_QUERYBUF:
	case VIDIOC_QBUF:
	case VIDIOC_DQBUF:
		err = put_v4l2_buffer32(up_native, up);
		break;

	case VIDIOC_ENUMSTD:
		err = put_v4l2_standard32(up_native, up);
		break;

	case VIDIOC_ENUMINPUT:
		err = put_v4l2_input32(up_native, up);
		break;
	}
	return err;
}

long v4l2_compat_ioctl32(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct video_device *vdev = video_devdata(file);
	long ret = -ENOIOCTLCMD;

	if (!file->f_op->unlocked_ioctl)
		return ret;

	if (_IOC_TYPE(cmd) == 'V' && _IOC_NR(cmd) < BASE_VIDIOC_PRIVATE)
		ret = do_video_ioctl(file, cmd, arg);
	else if (vdev->fops->compat_ioctl32)
		ret = vdev->fops->compat_ioctl32(file, cmd, arg);

	if (ret == -ENOIOCTLCMD)
		pr_debug("compat_ioctl32: unknown ioctl '%c', dir=%d, #%d (0x%08x)\n",
			 _IOC_TYPE(cmd), _IOC_DIR(cmd), _IOC_NR(cmd), cmd);
	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_compat_ioctl32);
