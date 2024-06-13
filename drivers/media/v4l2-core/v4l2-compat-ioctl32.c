// SPDX-License-Identifier: GPL-2.0-only
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

/*
 * Per-ioctl data copy handlers.
 *
 * Those come in pairs, with a get_v4l2_foo() and a put_v4l2_foo() routine,
 * where "v4l2_foo" is the name of the V4L2 struct.
 *
 * They basically get two __user pointers, one with a 32-bits struct that
 * came from the userspace call and a 64-bits struct, also allocated as
 * userspace, but filled internally by do_video_ioctl().
 *
 * For ioctls that have pointers inside it, the functions will also
 * receive an ancillary buffer with extra space, used to pass extra
 * data to the routine.
 */

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

static int get_v4l2_window32(struct v4l2_window *p64,
			     struct v4l2_window32 __user *p32)
{
	struct v4l2_window32 w32;

	if (copy_from_user(&w32, p32, sizeof(w32)))
		return -EFAULT;

	*p64 = (struct v4l2_window) {
		.w		= w32.w,
		.field		= w32.field,
		.chromakey	= w32.chromakey,
		.clips		= (void __force *)compat_ptr(w32.clips),
		.clipcount	= w32.clipcount,
		.bitmap		= compat_ptr(w32.bitmap),
		.global_alpha	= w32.global_alpha,
	};

	if (p64->clipcount > 2048)
		return -EINVAL;
	if (!p64->clipcount)
		p64->clips = NULL;

	return 0;
}

static int put_v4l2_window32(struct v4l2_window *p64,
			     struct v4l2_window32 __user *p32)
{
	struct v4l2_window32 w32;

	memset(&w32, 0, sizeof(w32));
	w32 = (struct v4l2_window32) {
		.w		= p64->w,
		.field		= p64->field,
		.chromakey	= p64->chromakey,
		.clips		= (uintptr_t)p64->clips,
		.clipcount	= p64->clipcount,
		.bitmap		= ptr_to_compat(p64->bitmap),
		.global_alpha	= p64->global_alpha,
	};

	/* copy everything except the clips pointer */
	if (copy_to_user(p32, &w32, offsetof(struct v4l2_window32, clips)) ||
	    copy_to_user(&p32->clipcount, &w32.clipcount,
			 sizeof(w32) - offsetof(struct v4l2_window32, clipcount)))
		return -EFAULT;

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
 * @capabilities: capabilities of this buffer type.
 * @flags:	additional buffer management attributes (ignored unless the
 *		queue has V4L2_BUF_CAP_SUPPORTS_MMAP_CACHE_HINTS capability and
 *		configured for MMAP streaming I/O).
 * @reserved:	future extensions
 */
struct v4l2_create_buffers32 {
	__u32			index;
	__u32			count;
	__u32			memory;	/* enum v4l2_memory */
	struct v4l2_format32	format;
	__u32			capabilities;
	__u32			flags;
	__u32			reserved[6];
};

static int get_v4l2_format32(struct v4l2_format *p64,
			     struct v4l2_format32 __user *p32)
{
	if (get_user(p64->type, &p32->type))
		return -EFAULT;

	switch (p64->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return copy_from_user(&p64->fmt.pix, &p32->fmt.pix,
				      sizeof(p64->fmt.pix)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return copy_from_user(&p64->fmt.pix_mp, &p32->fmt.pix_mp,
				      sizeof(p64->fmt.pix_mp)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		return get_v4l2_window32(&p64->fmt.win, &p32->fmt.win);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		return copy_from_user(&p64->fmt.vbi, &p32->fmt.vbi,
				      sizeof(p64->fmt.vbi)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		return copy_from_user(&p64->fmt.sliced, &p32->fmt.sliced,
				      sizeof(p64->fmt.sliced)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SDR_CAPTURE:
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		return copy_from_user(&p64->fmt.sdr, &p32->fmt.sdr,
				      sizeof(p64->fmt.sdr)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_META_CAPTURE:
	case V4L2_BUF_TYPE_META_OUTPUT:
		return copy_from_user(&p64->fmt.meta, &p32->fmt.meta,
				      sizeof(p64->fmt.meta)) ? -EFAULT : 0;
	default:
		return -EINVAL;
	}
}

static int get_v4l2_create32(struct v4l2_create_buffers *p64,
			     struct v4l2_create_buffers32 __user *p32)
{
	if (copy_from_user(p64, p32,
			   offsetof(struct v4l2_create_buffers32, format)))
		return -EFAULT;
	if (copy_from_user(&p64->flags, &p32->flags, sizeof(p32->flags)))
		return -EFAULT;
	return get_v4l2_format32(&p64->format, &p32->format);
}

static int put_v4l2_format32(struct v4l2_format *p64,
			     struct v4l2_format32 __user *p32)
{
	switch (p64->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return copy_to_user(&p32->fmt.pix, &p64->fmt.pix,
				    sizeof(p64->fmt.pix)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return copy_to_user(&p32->fmt.pix_mp, &p64->fmt.pix_mp,
				    sizeof(p64->fmt.pix_mp)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		return put_v4l2_window32(&p64->fmt.win, &p32->fmt.win);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		return copy_to_user(&p32->fmt.vbi, &p64->fmt.vbi,
				    sizeof(p64->fmt.vbi)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		return copy_to_user(&p32->fmt.sliced, &p64->fmt.sliced,
				    sizeof(p64->fmt.sliced)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SDR_CAPTURE:
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		return copy_to_user(&p32->fmt.sdr, &p64->fmt.sdr,
				    sizeof(p64->fmt.sdr)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_META_CAPTURE:
	case V4L2_BUF_TYPE_META_OUTPUT:
		return copy_to_user(&p32->fmt.meta, &p64->fmt.meta,
				    sizeof(p64->fmt.meta)) ? -EFAULT : 0;
	default:
		return -EINVAL;
	}
}

static int put_v4l2_create32(struct v4l2_create_buffers *p64,
			     struct v4l2_create_buffers32 __user *p32)
{
	if (copy_to_user(p32, p64,
			 offsetof(struct v4l2_create_buffers32, format)) ||
	    put_user(p64->capabilities, &p32->capabilities) ||
	    put_user(p64->flags, &p32->flags) ||
	    copy_to_user(p32->reserved, p64->reserved, sizeof(p64->reserved)))
		return -EFAULT;
	return put_v4l2_format32(&p64->format, &p32->format);
}

struct v4l2_standard32 {
	__u32		     index;
	compat_u64	     id;
	__u8		     name[24];
	struct v4l2_fract    frameperiod; /* Frames, not fields */
	__u32		     framelines;
	__u32		     reserved[4];
};

static int get_v4l2_standard32(struct v4l2_standard *p64,
			       struct v4l2_standard32 __user *p32)
{
	/* other fields are not set by the user, nor used by the driver */
	return get_user(p64->index, &p32->index);
}

static int put_v4l2_standard32(struct v4l2_standard *p64,
			       struct v4l2_standard32 __user *p32)
{
	if (put_user(p64->index, &p32->index) ||
	    put_user(p64->id, &p32->id) ||
	    copy_to_user(p32->name, p64->name, sizeof(p32->name)) ||
	    copy_to_user(&p32->frameperiod, &p64->frameperiod,
			 sizeof(p32->frameperiod)) ||
	    put_user(p64->framelines, &p32->framelines) ||
	    copy_to_user(p32->reserved, p64->reserved, sizeof(p32->reserved)))
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

/*
 * This is correct for all architectures including i386, but not x32,
 * which has different alignment requirements for timestamp
 */
struct v4l2_buffer32 {
	__u32			index;
	__u32			type;	/* enum v4l2_buf_type */
	__u32			bytesused;
	__u32			flags;
	__u32			field;	/* enum v4l2_field */
	struct {
		compat_s64	tv_sec;
		compat_s64	tv_usec;
	}			timestamp;
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
	__s32			request_fd;
};

#ifdef CONFIG_COMPAT_32BIT_TIME
struct v4l2_buffer32_time32 {
	__u32			index;
	__u32			type;	/* enum v4l2_buf_type */
	__u32			bytesused;
	__u32			flags;
	__u32			field;	/* enum v4l2_field */
	struct old_timeval32	timestamp;
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
	__s32			request_fd;
};
#endif

static int get_v4l2_plane32(struct v4l2_plane *p64,
			    struct v4l2_plane32 __user *p32,
			    enum v4l2_memory memory)
{
	struct v4l2_plane32 plane32;
	typeof(p64->m) m = {};

	if (copy_from_user(&plane32, p32, sizeof(plane32)))
		return -EFAULT;

	switch (memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_OVERLAY:
		m.mem_offset = plane32.m.mem_offset;
		break;
	case V4L2_MEMORY_USERPTR:
		m.userptr = (unsigned long)compat_ptr(plane32.m.userptr);
		break;
	case V4L2_MEMORY_DMABUF:
		m.fd = plane32.m.fd;
		break;
	}

	memset(p64, 0, sizeof(*p64));
	*p64 = (struct v4l2_plane) {
		.bytesused	= plane32.bytesused,
		.length		= plane32.length,
		.m		= m,
		.data_offset	= plane32.data_offset,
	};

	return 0;
}

static int put_v4l2_plane32(struct v4l2_plane *p64,
			    struct v4l2_plane32 __user *p32,
			    enum v4l2_memory memory)
{
	struct v4l2_plane32 plane32;

	memset(&plane32, 0, sizeof(plane32));
	plane32 = (struct v4l2_plane32) {
		.bytesused	= p64->bytesused,
		.length		= p64->length,
		.data_offset	= p64->data_offset,
	};

	switch (memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_OVERLAY:
		plane32.m.mem_offset = p64->m.mem_offset;
		break;
	case V4L2_MEMORY_USERPTR:
		plane32.m.userptr = (uintptr_t)(p64->m.userptr);
		break;
	case V4L2_MEMORY_DMABUF:
		plane32.m.fd = p64->m.fd;
		break;
	}

	if (copy_to_user(p32, &plane32, sizeof(plane32)))
		return -EFAULT;

	return 0;
}

static int get_v4l2_buffer32(struct v4l2_buffer *vb,
			     struct v4l2_buffer32 __user *arg)
{
	struct v4l2_buffer32 vb32;

	if (copy_from_user(&vb32, arg, sizeof(vb32)))
		return -EFAULT;

	memset(vb, 0, sizeof(*vb));
	*vb = (struct v4l2_buffer) {
		.index		= vb32.index,
		.type		= vb32.type,
		.bytesused	= vb32.bytesused,
		.flags		= vb32.flags,
		.field		= vb32.field,
		.timestamp.tv_sec	= vb32.timestamp.tv_sec,
		.timestamp.tv_usec	= vb32.timestamp.tv_usec,
		.timecode	= vb32.timecode,
		.sequence	= vb32.sequence,
		.memory		= vb32.memory,
		.m.offset	= vb32.m.offset,
		.length		= vb32.length,
		.request_fd	= vb32.request_fd,
	};

	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_OVERLAY:
		vb->m.offset = vb32.m.offset;
		break;
	case V4L2_MEMORY_USERPTR:
		vb->m.userptr = (unsigned long)compat_ptr(vb32.m.userptr);
		break;
	case V4L2_MEMORY_DMABUF:
		vb->m.fd = vb32.m.fd;
		break;
	}

	if (V4L2_TYPE_IS_MULTIPLANAR(vb->type))
		vb->m.planes = (void __force *)
				compat_ptr(vb32.m.planes);

	return 0;
}

#ifdef CONFIG_COMPAT_32BIT_TIME
static int get_v4l2_buffer32_time32(struct v4l2_buffer *vb,
				    struct v4l2_buffer32_time32 __user *arg)
{
	struct v4l2_buffer32_time32 vb32;

	if (copy_from_user(&vb32, arg, sizeof(vb32)))
		return -EFAULT;

	*vb = (struct v4l2_buffer) {
		.index		= vb32.index,
		.type		= vb32.type,
		.bytesused	= vb32.bytesused,
		.flags		= vb32.flags,
		.field		= vb32.field,
		.timestamp.tv_sec	= vb32.timestamp.tv_sec,
		.timestamp.tv_usec	= vb32.timestamp.tv_usec,
		.timecode	= vb32.timecode,
		.sequence	= vb32.sequence,
		.memory		= vb32.memory,
		.m.offset	= vb32.m.offset,
		.length		= vb32.length,
		.request_fd	= vb32.request_fd,
	};
	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_OVERLAY:
		vb->m.offset = vb32.m.offset;
		break;
	case V4L2_MEMORY_USERPTR:
		vb->m.userptr = (unsigned long)compat_ptr(vb32.m.userptr);
		break;
	case V4L2_MEMORY_DMABUF:
		vb->m.fd = vb32.m.fd;
		break;
	}

	if (V4L2_TYPE_IS_MULTIPLANAR(vb->type))
		vb->m.planes = (void __force *)
				compat_ptr(vb32.m.planes);

	return 0;
}
#endif

static int put_v4l2_buffer32(struct v4l2_buffer *vb,
			     struct v4l2_buffer32 __user *arg)
{
	struct v4l2_buffer32 vb32;

	memset(&vb32, 0, sizeof(vb32));
	vb32 = (struct v4l2_buffer32) {
		.index		= vb->index,
		.type		= vb->type,
		.bytesused	= vb->bytesused,
		.flags		= vb->flags,
		.field		= vb->field,
		.timestamp.tv_sec	= vb->timestamp.tv_sec,
		.timestamp.tv_usec	= vb->timestamp.tv_usec,
		.timecode	= vb->timecode,
		.sequence	= vb->sequence,
		.memory		= vb->memory,
		.m.offset	= vb->m.offset,
		.length		= vb->length,
		.request_fd	= vb->request_fd,
	};

	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_OVERLAY:
		vb32.m.offset = vb->m.offset;
		break;
	case V4L2_MEMORY_USERPTR:
		vb32.m.userptr = (uintptr_t)(vb->m.userptr);
		break;
	case V4L2_MEMORY_DMABUF:
		vb32.m.fd = vb->m.fd;
		break;
	}

	if (V4L2_TYPE_IS_MULTIPLANAR(vb->type))
		vb32.m.planes = (uintptr_t)vb->m.planes;

	if (copy_to_user(arg, &vb32, sizeof(vb32)))
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_COMPAT_32BIT_TIME
static int put_v4l2_buffer32_time32(struct v4l2_buffer *vb,
				    struct v4l2_buffer32_time32 __user *arg)
{
	struct v4l2_buffer32_time32 vb32;

	memset(&vb32, 0, sizeof(vb32));
	vb32 = (struct v4l2_buffer32_time32) {
		.index		= vb->index,
		.type		= vb->type,
		.bytesused	= vb->bytesused,
		.flags		= vb->flags,
		.field		= vb->field,
		.timestamp.tv_sec	= vb->timestamp.tv_sec,
		.timestamp.tv_usec	= vb->timestamp.tv_usec,
		.timecode	= vb->timecode,
		.sequence	= vb->sequence,
		.memory		= vb->memory,
		.m.offset	= vb->m.offset,
		.length		= vb->length,
		.request_fd	= vb->request_fd,
	};
	switch (vb->memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_OVERLAY:
		vb32.m.offset = vb->m.offset;
		break;
	case V4L2_MEMORY_USERPTR:
		vb32.m.userptr = (uintptr_t)(vb->m.userptr);
		break;
	case V4L2_MEMORY_DMABUF:
		vb32.m.fd = vb->m.fd;
		break;
	}

	if (V4L2_TYPE_IS_MULTIPLANAR(vb->type))
		vb32.m.planes = (uintptr_t)vb->m.planes;

	if (copy_to_user(arg, &vb32, sizeof(vb32)))
		return -EFAULT;

	return 0;
}
#endif

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

static int get_v4l2_framebuffer32(struct v4l2_framebuffer *p64,
				  struct v4l2_framebuffer32 __user *p32)
{
	compat_caddr_t tmp;

	if (get_user(tmp, &p32->base) ||
	    get_user(p64->capability, &p32->capability) ||
	    get_user(p64->flags, &p32->flags) ||
	    copy_from_user(&p64->fmt, &p32->fmt, sizeof(p64->fmt)))
		return -EFAULT;
	p64->base = (void __force *)compat_ptr(tmp);

	return 0;
}

static int put_v4l2_framebuffer32(struct v4l2_framebuffer *p64,
				  struct v4l2_framebuffer32 __user *p32)
{
	if (put_user((uintptr_t)p64->base, &p32->base) ||
	    put_user(p64->capability, &p32->capability) ||
	    put_user(p64->flags, &p32->flags) ||
	    copy_to_user(&p32->fmt, &p64->fmt, sizeof(p64->fmt)))
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
static inline int get_v4l2_input32(struct v4l2_input *p64,
				   struct v4l2_input32 __user *p32)
{
	if (copy_from_user(p64, p32, sizeof(*p32)))
		return -EFAULT;
	return 0;
}

static inline int put_v4l2_input32(struct v4l2_input *p64,
				   struct v4l2_input32 __user *p32)
{
	if (copy_to_user(p32, p64, sizeof(*p32)))
		return -EFAULT;
	return 0;
}

struct v4l2_ext_controls32 {
	__u32 which;
	__u32 count;
	__u32 error_idx;
	__s32 request_fd;
	__u32 reserved[1];
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

static int get_v4l2_ext_controls32(struct v4l2_ext_controls *p64,
				   struct v4l2_ext_controls32 __user *p32)
{
	struct v4l2_ext_controls32 ec32;

	if (copy_from_user(&ec32, p32, sizeof(ec32)))
		return -EFAULT;

	*p64 = (struct v4l2_ext_controls) {
		.which		= ec32.which,
		.count		= ec32.count,
		.error_idx	= ec32.error_idx,
		.request_fd	= ec32.request_fd,
		.reserved[0]	= ec32.reserved[0],
		.controls	= (void __force *)compat_ptr(ec32.controls),
	};

	return 0;
}

static int put_v4l2_ext_controls32(struct v4l2_ext_controls *p64,
				   struct v4l2_ext_controls32 __user *p32)
{
	struct v4l2_ext_controls32 ec32;

	memset(&ec32, 0, sizeof(ec32));
	ec32 = (struct v4l2_ext_controls32) {
		.which		= p64->which,
		.count		= p64->count,
		.error_idx	= p64->error_idx,
		.request_fd	= p64->request_fd,
		.reserved[0]	= p64->reserved[0],
		.controls	= (uintptr_t)p64->controls,
	};

	if (copy_to_user(p32, &ec32, sizeof(ec32)))
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_X86_64
/*
 * x86 is the only compat architecture with different struct alignment
 * between 32-bit and 64-bit tasks.
 */
struct v4l2_event32 {
	__u32				type;
	union {
		compat_s64		value64;
		__u8			data[64];
	} u;
	__u32				pending;
	__u32				sequence;
	struct {
		compat_s64		tv_sec;
		compat_s64		tv_nsec;
	} timestamp;
	__u32				id;
	__u32				reserved[8];
};

static int put_v4l2_event32(struct v4l2_event *p64,
			    struct v4l2_event32 __user *p32)
{
	if (put_user(p64->type, &p32->type) ||
	    copy_to_user(&p32->u, &p64->u, sizeof(p64->u)) ||
	    put_user(p64->pending, &p32->pending) ||
	    put_user(p64->sequence, &p32->sequence) ||
	    put_user(p64->timestamp.tv_sec, &p32->timestamp.tv_sec) ||
	    put_user(p64->timestamp.tv_nsec, &p32->timestamp.tv_nsec) ||
	    put_user(p64->id, &p32->id) ||
	    copy_to_user(p32->reserved, p64->reserved, sizeof(p32->reserved)))
		return -EFAULT;
	return 0;
}

#endif

#ifdef CONFIG_COMPAT_32BIT_TIME
struct v4l2_event32_time32 {
	__u32				type;
	union {
		compat_s64		value64;
		__u8			data[64];
	} u;
	__u32				pending;
	__u32				sequence;
	struct old_timespec32		timestamp;
	__u32				id;
	__u32				reserved[8];
};

static int put_v4l2_event32_time32(struct v4l2_event *p64,
				   struct v4l2_event32_time32 __user *p32)
{
	if (put_user(p64->type, &p32->type) ||
	    copy_to_user(&p32->u, &p64->u, sizeof(p64->u)) ||
	    put_user(p64->pending, &p32->pending) ||
	    put_user(p64->sequence, &p32->sequence) ||
	    put_user(p64->timestamp.tv_sec, &p32->timestamp.tv_sec) ||
	    put_user(p64->timestamp.tv_nsec, &p32->timestamp.tv_nsec) ||
	    put_user(p64->id, &p32->id) ||
	    copy_to_user(p32->reserved, p64->reserved, sizeof(p32->reserved)))
		return -EFAULT;
	return 0;
}
#endif

struct v4l2_edid32 {
	__u32 pad;
	__u32 start_block;
	__u32 blocks;
	__u32 reserved[5];
	compat_caddr_t edid;
};

static int get_v4l2_edid32(struct v4l2_edid *p64,
			   struct v4l2_edid32 __user *p32)
{
	compat_uptr_t edid;

	if (copy_from_user(p64, p32, offsetof(struct v4l2_edid32, edid)) ||
	    get_user(edid, &p32->edid))
		return -EFAULT;

	p64->edid = (void __force *)compat_ptr(edid);
	return 0;
}

static int put_v4l2_edid32(struct v4l2_edid *p64,
			   struct v4l2_edid32 __user *p32)
{
	if (copy_to_user(p32, p64, offsetof(struct v4l2_edid32, edid)))
		return -EFAULT;
	return 0;
}

/*
 * List of ioctls that require 32-bits/64-bits conversion
 *
 * The V4L2 ioctls that aren't listed there don't have pointer arguments
 * and the struct size is identical for both 32 and 64 bits versions, so
 * they don't need translations.
 */

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

#ifdef CONFIG_COMPAT_32BIT_TIME
#define VIDIOC_QUERYBUF32_TIME32	_IOWR('V',  9, struct v4l2_buffer32_time32)
#define VIDIOC_QBUF32_TIME32		_IOWR('V', 15, struct v4l2_buffer32_time32)
#define VIDIOC_DQBUF32_TIME32		_IOWR('V', 17, struct v4l2_buffer32_time32)
#define	VIDIOC_DQEVENT32_TIME32		_IOR ('V', 89, struct v4l2_event32_time32)
#define VIDIOC_PREPARE_BUF32_TIME32	_IOWR('V', 93, struct v4l2_buffer32_time32)
#endif

unsigned int v4l2_compat_translate_cmd(unsigned int cmd)
{
	switch (cmd) {
	case VIDIOC_G_FMT32:
		return VIDIOC_G_FMT;
	case VIDIOC_S_FMT32:
		return VIDIOC_S_FMT;
	case VIDIOC_TRY_FMT32:
		return VIDIOC_TRY_FMT;
	case VIDIOC_G_FBUF32:
		return VIDIOC_G_FBUF;
	case VIDIOC_S_FBUF32:
		return VIDIOC_S_FBUF;
#ifdef CONFIG_COMPAT_32BIT_TIME
	case VIDIOC_QUERYBUF32_TIME32:
		return VIDIOC_QUERYBUF;
	case VIDIOC_QBUF32_TIME32:
		return VIDIOC_QBUF;
	case VIDIOC_DQBUF32_TIME32:
		return VIDIOC_DQBUF;
	case VIDIOC_PREPARE_BUF32_TIME32:
		return VIDIOC_PREPARE_BUF;
#endif
	case VIDIOC_QUERYBUF32:
		return VIDIOC_QUERYBUF;
	case VIDIOC_QBUF32:
		return VIDIOC_QBUF;
	case VIDIOC_DQBUF32:
		return VIDIOC_DQBUF;
	case VIDIOC_CREATE_BUFS32:
		return VIDIOC_CREATE_BUFS;
	case VIDIOC_G_EXT_CTRLS32:
		return VIDIOC_G_EXT_CTRLS;
	case VIDIOC_S_EXT_CTRLS32:
		return VIDIOC_S_EXT_CTRLS;
	case VIDIOC_TRY_EXT_CTRLS32:
		return VIDIOC_TRY_EXT_CTRLS;
	case VIDIOC_PREPARE_BUF32:
		return VIDIOC_PREPARE_BUF;
	case VIDIOC_ENUMSTD32:
		return VIDIOC_ENUMSTD;
	case VIDIOC_ENUMINPUT32:
		return VIDIOC_ENUMINPUT;
	case VIDIOC_G_EDID32:
		return VIDIOC_G_EDID;
	case VIDIOC_S_EDID32:
		return VIDIOC_S_EDID;
#ifdef CONFIG_X86_64
	case VIDIOC_DQEVENT32:
		return VIDIOC_DQEVENT;
#endif
#ifdef CONFIG_COMPAT_32BIT_TIME
	case VIDIOC_DQEVENT32_TIME32:
		return VIDIOC_DQEVENT;
#endif
	}
	return cmd;
}

int v4l2_compat_get_user(void __user *arg, void *parg, unsigned int cmd)
{
	switch (cmd) {
	case VIDIOC_G_FMT32:
	case VIDIOC_S_FMT32:
	case VIDIOC_TRY_FMT32:
		return get_v4l2_format32(parg, arg);

	case VIDIOC_S_FBUF32:
		return get_v4l2_framebuffer32(parg, arg);
#ifdef CONFIG_COMPAT_32BIT_TIME
	case VIDIOC_QUERYBUF32_TIME32:
	case VIDIOC_QBUF32_TIME32:
	case VIDIOC_DQBUF32_TIME32:
	case VIDIOC_PREPARE_BUF32_TIME32:
		return get_v4l2_buffer32_time32(parg, arg);
#endif
	case VIDIOC_QUERYBUF32:
	case VIDIOC_QBUF32:
	case VIDIOC_DQBUF32:
	case VIDIOC_PREPARE_BUF32:
		return get_v4l2_buffer32(parg, arg);

	case VIDIOC_G_EXT_CTRLS32:
	case VIDIOC_S_EXT_CTRLS32:
	case VIDIOC_TRY_EXT_CTRLS32:
		return get_v4l2_ext_controls32(parg, arg);

	case VIDIOC_CREATE_BUFS32:
		return get_v4l2_create32(parg, arg);

	case VIDIOC_ENUMSTD32:
		return get_v4l2_standard32(parg, arg);

	case VIDIOC_ENUMINPUT32:
		return get_v4l2_input32(parg, arg);

	case VIDIOC_G_EDID32:
	case VIDIOC_S_EDID32:
		return get_v4l2_edid32(parg, arg);
	}
	return 0;
}

int v4l2_compat_put_user(void __user *arg, void *parg, unsigned int cmd)
{
	switch (cmd) {
	case VIDIOC_G_FMT32:
	case VIDIOC_S_FMT32:
	case VIDIOC_TRY_FMT32:
		return put_v4l2_format32(parg, arg);

	case VIDIOC_G_FBUF32:
		return put_v4l2_framebuffer32(parg, arg);
#ifdef CONFIG_COMPAT_32BIT_TIME
	case VIDIOC_QUERYBUF32_TIME32:
	case VIDIOC_QBUF32_TIME32:
	case VIDIOC_DQBUF32_TIME32:
	case VIDIOC_PREPARE_BUF32_TIME32:
		return put_v4l2_buffer32_time32(parg, arg);
#endif
	case VIDIOC_QUERYBUF32:
	case VIDIOC_QBUF32:
	case VIDIOC_DQBUF32:
	case VIDIOC_PREPARE_BUF32:
		return put_v4l2_buffer32(parg, arg);

	case VIDIOC_G_EXT_CTRLS32:
	case VIDIOC_S_EXT_CTRLS32:
	case VIDIOC_TRY_EXT_CTRLS32:
		return put_v4l2_ext_controls32(parg, arg);

	case VIDIOC_CREATE_BUFS32:
		return put_v4l2_create32(parg, arg);

	case VIDIOC_ENUMSTD32:
		return put_v4l2_standard32(parg, arg);

	case VIDIOC_ENUMINPUT32:
		return put_v4l2_input32(parg, arg);

	case VIDIOC_G_EDID32:
	case VIDIOC_S_EDID32:
		return put_v4l2_edid32(parg, arg);
#ifdef CONFIG_X86_64
	case VIDIOC_DQEVENT32:
		return put_v4l2_event32(parg, arg);
#endif
#ifdef CONFIG_COMPAT_32BIT_TIME
	case VIDIOC_DQEVENT32_TIME32:
		return put_v4l2_event32_time32(parg, arg);
#endif
	}
	return 0;
}

int v4l2_compat_get_array_args(struct file *file, void *mbuf,
			       void __user *user_ptr, size_t array_size,
			       unsigned int cmd, void *arg)
{
	int err = 0;

	memset(mbuf, 0, array_size);

	switch (cmd) {
	case VIDIOC_G_FMT32:
	case VIDIOC_S_FMT32:
	case VIDIOC_TRY_FMT32: {
		struct v4l2_format *f64 = arg;
		struct v4l2_clip *c64 = mbuf;
		struct v4l2_clip32 __user *c32 = user_ptr;
		u32 clipcount = f64->fmt.win.clipcount;

		if ((f64->type != V4L2_BUF_TYPE_VIDEO_OVERLAY &&
		     f64->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY) ||
		    clipcount == 0)
			return 0;
		if (clipcount > 2048)
			return -EINVAL;
		while (clipcount--) {
			if (copy_from_user(c64, c32, sizeof(c64->c)))
				return -EFAULT;
			c64->next = NULL;
			c64++;
			c32++;
		}
		break;
	}
#ifdef CONFIG_COMPAT_32BIT_TIME
	case VIDIOC_QUERYBUF32_TIME32:
	case VIDIOC_QBUF32_TIME32:
	case VIDIOC_DQBUF32_TIME32:
	case VIDIOC_PREPARE_BUF32_TIME32:
#endif
	case VIDIOC_QUERYBUF32:
	case VIDIOC_QBUF32:
	case VIDIOC_DQBUF32:
	case VIDIOC_PREPARE_BUF32: {
		struct v4l2_buffer *b64 = arg;
		struct v4l2_plane *p64 = mbuf;
		struct v4l2_plane32 __user *p32 = user_ptr;

		if (V4L2_TYPE_IS_MULTIPLANAR(b64->type)) {
			u32 num_planes = b64->length;

			if (num_planes == 0)
				return 0;

			while (num_planes--) {
				err = get_v4l2_plane32(p64, p32, b64->memory);
				if (err)
					return err;
				++p64;
				++p32;
			}
		}
		break;
	}
	case VIDIOC_G_EXT_CTRLS32:
	case VIDIOC_S_EXT_CTRLS32:
	case VIDIOC_TRY_EXT_CTRLS32: {
		struct v4l2_ext_controls *ecs64 = arg;
		struct v4l2_ext_control *ec64 = mbuf;
		struct v4l2_ext_control32 __user *ec32 = user_ptr;
		int n;

		for (n = 0; n < ecs64->count; n++) {
			if (copy_from_user(ec64, ec32, sizeof(*ec32)))
				return -EFAULT;

			if (ctrl_is_pointer(file, ec64->id)) {
				compat_uptr_t p;

				if (get_user(p, &ec32->string))
					return -EFAULT;
				ec64->string = compat_ptr(p);
			}
			ec32++;
			ec64++;
		}
		break;
	}
	default:
		if (copy_from_user(mbuf, user_ptr, array_size))
			err = -EFAULT;
		break;
	}

	return err;
}

int v4l2_compat_put_array_args(struct file *file, void __user *user_ptr,
			       void *mbuf, size_t array_size,
			       unsigned int cmd, void *arg)
{
	int err = 0;

	switch (cmd) {
	case VIDIOC_G_FMT32:
	case VIDIOC_S_FMT32:
	case VIDIOC_TRY_FMT32: {
		struct v4l2_format *f64 = arg;
		struct v4l2_clip *c64 = mbuf;
		struct v4l2_clip32 __user *c32 = user_ptr;
		u32 clipcount = f64->fmt.win.clipcount;

		if ((f64->type != V4L2_BUF_TYPE_VIDEO_OVERLAY &&
		     f64->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY) ||
		    clipcount == 0)
			return 0;
		if (clipcount > 2048)
			return -EINVAL;
		while (clipcount--) {
			if (copy_to_user(c32, c64, sizeof(c64->c)))
				return -EFAULT;
			c64++;
			c32++;
		}
		break;
	}
#ifdef CONFIG_COMPAT_32BIT_TIME
	case VIDIOC_QUERYBUF32_TIME32:
	case VIDIOC_QBUF32_TIME32:
	case VIDIOC_DQBUF32_TIME32:
	case VIDIOC_PREPARE_BUF32_TIME32:
#endif
	case VIDIOC_QUERYBUF32:
	case VIDIOC_QBUF32:
	case VIDIOC_DQBUF32:
	case VIDIOC_PREPARE_BUF32: {
		struct v4l2_buffer *b64 = arg;
		struct v4l2_plane *p64 = mbuf;
		struct v4l2_plane32 __user *p32 = user_ptr;

		if (V4L2_TYPE_IS_MULTIPLANAR(b64->type)) {
			u32 num_planes = b64->length;

			if (num_planes == 0)
				return 0;

			while (num_planes--) {
				err = put_v4l2_plane32(p64, p32, b64->memory);
				if (err)
					return err;
				++p64;
				++p32;
			}
		}
		break;
	}
	case VIDIOC_G_EXT_CTRLS32:
	case VIDIOC_S_EXT_CTRLS32:
	case VIDIOC_TRY_EXT_CTRLS32: {
		struct v4l2_ext_controls *ecs64 = arg;
		struct v4l2_ext_control *ec64 = mbuf;
		struct v4l2_ext_control32 __user *ec32 = user_ptr;
		int n;

		for (n = 0; n < ecs64->count; n++) {
			unsigned int size = sizeof(*ec32);
			/*
			 * Do not modify the pointer when copying a pointer
			 * control.  The contents of the pointer was changed,
			 * not the pointer itself.
			 * The structures are otherwise compatible.
			 */
			if (ctrl_is_pointer(file, ec64->id))
				size -= sizeof(ec32->value64);

			if (copy_to_user(ec32, ec64, size))
				return -EFAULT;

			ec32++;
			ec64++;
		}
		break;
	}
	default:
		if (copy_to_user(user_ptr, mbuf, array_size))
			err = -EFAULT;
		break;
	}

	return err;
}

/**
 * v4l2_compat_ioctl32() - Handles a compat32 ioctl call
 *
 * @file: pointer to &struct file with the file handler
 * @cmd: ioctl to be called
 * @arg: arguments passed from/to the ioctl handler
 *
 * This function is meant to be used as .compat_ioctl fops at v4l2-dev.c
 * in order to deal with 32-bit calls on a 64-bits Kernel.
 *
 * This function calls do_video_ioctl() for non-private V4L2 ioctls.
 * If the function is a private one it calls vdev->fops->compat_ioctl32
 * instead.
 */
long v4l2_compat_ioctl32(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct video_device *vdev = video_devdata(file);
	long ret = -ENOIOCTLCMD;

	if (!file->f_op->unlocked_ioctl)
		return ret;

	if (!video_is_registered(vdev))
		return -ENODEV;

	if (_IOC_TYPE(cmd) == 'V' && _IOC_NR(cmd) < BASE_VIDIOC_PRIVATE)
		ret = file->f_op->unlocked_ioctl(file, cmd,
					(unsigned long)compat_ptr(arg));
	else if (vdev->fops->compat_ioctl32)
		ret = vdev->fops->compat_ioctl32(file, cmd, arg);

	if (ret == -ENOIOCTLCMD)
		pr_debug("compat_ioctl32: unknown ioctl '%c', dir=%d, #%d (0x%08x)\n",
			 _IOC_TYPE(cmd), _IOC_DIR(cmd), _IOC_NR(cmd), cmd);
	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_compat_ioctl32);
