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

/**
 * assign_in_user() - Copy from one __user var to another one
 *
 * @to: __user var where data will be stored
 * @from: __user var where data will be retrieved.
 *
 * As this code very often needs to allocate userspace memory, it is easier
 * to have a macro that will do both get_user() and put_user() at once.
 *
 * This function complements the macros defined at asm-generic/uaccess.h.
 * It uses the same argument order as copy_in_user()
 */
#define assign_in_user(to, from)					\
({									\
	typeof(*from) __assign_tmp;					\
									\
	get_user(__assign_tmp, from) || put_user(__assign_tmp, to);	\
})

/**
 * get_user_cast() - Stores at a kernelspace local var the contents from a
 *		pointer with userspace data that is not tagged with __user.
 *
 * @__x: var where data will be stored
 * @__ptr: var where data will be retrieved.
 *
 * Sometimes we need to declare a pointer without __user because it
 * comes from a pointer struct field that will be retrieved from userspace
 * by the 64-bit native ioctl handler. This function ensures that the
 * @__ptr will be cast to __user before calling get_user() in order to
 * avoid warnings with static code analyzers like smatch.
 */
#define get_user_cast(__x, __ptr)					\
({									\
	get_user(__x, (typeof(*__ptr) __user *)(__ptr));		\
})

/**
 * put_user_force() - Stores the contents of a kernelspace local var
 *		      into a userspace pointer, removing any __user cast.
 *
 * @__x: var where data will be stored
 * @__ptr: var where data will be retrieved.
 *
 * Sometimes we need to remove the __user attribute from some data,
 * by passing the __force macro. This function ensures that the
 * @__ptr will be cast with __force before calling put_user(), in order to
 * avoid warnings with static code analyzers like smatch.
 */
#define put_user_force(__x, __ptr)					\
({									\
	put_user((typeof(*__x) __force *)(__x), __ptr);			\
})

/**
 * assign_in_user_cast() - Copy from one __user var to another one
 *
 * @to: __user var where data will be stored
 * @from: var where data will be retrieved that needs to be cast to __user.
 *
 * As this code very often needs to allocate userspace memory, it is easier
 * to have a macro that will do both get_user_cast() and put_user() at once.
 *
 * This function should be used instead of assign_in_user() when the @from
 * variable was not declared as __user. See get_user_cast() for more details.
 *
 * This function complements the macros defined at asm-generic/uaccess.h.
 * It uses the same argument order as copy_in_user()
 */
#define assign_in_user_cast(to, from)					\
({									\
	typeof(*from) __assign_tmp;					\
									\
	get_user_cast(__assign_tmp, from) || put_user(__assign_tmp, to);\
})

/**
 * native_ioctl - Ancillary function that calls the native 64 bits ioctl
 * handler.
 *
 * @file: pointer to &struct file with the file handler
 * @cmd: ioctl to be called
 * @arg: arguments passed from/to the ioctl handler
 *
 * This function calls the native ioctl handler at v4l2-dev, e. g. v4l2_ioctl()
 */
static long native_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -ENOIOCTLCMD;

	if (file->f_op->unlocked_ioctl)
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);

	return ret;
}


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

static int get_v4l2_window32(struct v4l2_window __user *p64,
			     struct v4l2_window32 __user *p32,
			     void __user *aux_buf, u32 aux_space)
{
	struct v4l2_clip32 __user *uclips;
	struct v4l2_clip __user *kclips;
	compat_caddr_t p;
	u32 clipcount;

	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
	    copy_in_user(&p64->w, &p32->w, sizeof(p32->w)) ||
	    assign_in_user(&p64->field, &p32->field) ||
	    assign_in_user(&p64->chromakey, &p32->chromakey) ||
	    assign_in_user(&p64->global_alpha, &p32->global_alpha) ||
	    get_user(clipcount, &p32->clipcount) ||
	    put_user(clipcount, &p64->clipcount))
		return -EFAULT;
	if (clipcount > 2048)
		return -EINVAL;
	if (!clipcount)
		return put_user(NULL, &p64->clips);

	if (get_user(p, &p32->clips))
		return -EFAULT;
	uclips = compat_ptr(p);
	if (aux_space < clipcount * sizeof(*kclips))
		return -EFAULT;
	kclips = aux_buf;
	if (put_user(kclips, &p64->clips))
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

static int put_v4l2_window32(struct v4l2_window __user *p64,
			     struct v4l2_window32 __user *p32)
{
	struct v4l2_clip __user *kclips;
	struct v4l2_clip32 __user *uclips;
	compat_caddr_t p;
	u32 clipcount;

	if (copy_in_user(&p32->w, &p64->w, sizeof(p64->w)) ||
	    assign_in_user(&p32->field, &p64->field) ||
	    assign_in_user(&p32->chromakey, &p64->chromakey) ||
	    assign_in_user(&p32->global_alpha, &p64->global_alpha) ||
	    get_user(clipcount, &p64->clipcount) ||
	    put_user(clipcount, &p32->clipcount))
		return -EFAULT;
	if (!clipcount)
		return 0;

	if (get_user(kclips, &p64->clips))
		return -EFAULT;
	if (get_user(p, &p32->clips))
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

static int __bufsize_v4l2_format(struct v4l2_format32 __user *p32, u32 *size)
{
	u32 type;

	if (get_user(type, &p32->type))
		return -EFAULT;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY: {
		u32 clipcount;

		if (get_user(clipcount, &p32->fmt.win.clipcount))
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

static int bufsize_v4l2_format(struct v4l2_format32 __user *p32, u32 *size)
{
	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)))
		return -EFAULT;
	return __bufsize_v4l2_format(p32, size);
}

static int __get_v4l2_format32(struct v4l2_format __user *p64,
			       struct v4l2_format32 __user *p32,
			       void __user *aux_buf, u32 aux_space)
{
	u32 type;

	if (get_user(type, &p32->type) || put_user(type, &p64->type))
		return -EFAULT;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return copy_in_user(&p64->fmt.pix, &p32->fmt.pix,
				    sizeof(p64->fmt.pix)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return copy_in_user(&p64->fmt.pix_mp, &p32->fmt.pix_mp,
				    sizeof(p64->fmt.pix_mp)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		return get_v4l2_window32(&p64->fmt.win, &p32->fmt.win,
					 aux_buf, aux_space);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		return copy_in_user(&p64->fmt.vbi, &p32->fmt.vbi,
				    sizeof(p64->fmt.vbi)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		return copy_in_user(&p64->fmt.sliced, &p32->fmt.sliced,
				    sizeof(p64->fmt.sliced)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SDR_CAPTURE:
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		return copy_in_user(&p64->fmt.sdr, &p32->fmt.sdr,
				    sizeof(p64->fmt.sdr)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_META_CAPTURE:
	case V4L2_BUF_TYPE_META_OUTPUT:
		return copy_in_user(&p64->fmt.meta, &p32->fmt.meta,
				    sizeof(p64->fmt.meta)) ? -EFAULT : 0;
	default:
		return -EINVAL;
	}
}

static int get_v4l2_format32(struct v4l2_format __user *p64,
			     struct v4l2_format32 __user *p32,
			     void __user *aux_buf, u32 aux_space)
{
	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)))
		return -EFAULT;
	return __get_v4l2_format32(p64, p32, aux_buf, aux_space);
}

static int bufsize_v4l2_create(struct v4l2_create_buffers32 __user *p32,
			       u32 *size)
{
	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)))
		return -EFAULT;
	return __bufsize_v4l2_format(&p32->format, size);
}

static int get_v4l2_create32(struct v4l2_create_buffers __user *p64,
			     struct v4l2_create_buffers32 __user *p32,
			     void __user *aux_buf, u32 aux_space)
{
	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
	    copy_in_user(p64, p32,
			 offsetof(struct v4l2_create_buffers32, format)))
		return -EFAULT;
	return __get_v4l2_format32(&p64->format, &p32->format,
				   aux_buf, aux_space);
}

static int __put_v4l2_format32(struct v4l2_format __user *p64,
			       struct v4l2_format32 __user *p32)
{
	u32 type;

	if (get_user(type, &p64->type))
		return -EFAULT;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return copy_in_user(&p32->fmt.pix, &p64->fmt.pix,
				    sizeof(p64->fmt.pix)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return copy_in_user(&p32->fmt.pix_mp, &p64->fmt.pix_mp,
				    sizeof(p64->fmt.pix_mp)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		return put_v4l2_window32(&p64->fmt.win, &p32->fmt.win);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		return copy_in_user(&p32->fmt.vbi, &p64->fmt.vbi,
				    sizeof(p64->fmt.vbi)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		return copy_in_user(&p32->fmt.sliced, &p64->fmt.sliced,
				    sizeof(p64->fmt.sliced)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_SDR_CAPTURE:
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		return copy_in_user(&p32->fmt.sdr, &p64->fmt.sdr,
				    sizeof(p64->fmt.sdr)) ? -EFAULT : 0;
	case V4L2_BUF_TYPE_META_CAPTURE:
	case V4L2_BUF_TYPE_META_OUTPUT:
		return copy_in_user(&p32->fmt.meta, &p64->fmt.meta,
				    sizeof(p64->fmt.meta)) ? -EFAULT : 0;
	default:
		return -EINVAL;
	}
}

static int put_v4l2_format32(struct v4l2_format __user *p64,
			     struct v4l2_format32 __user *p32)
{
	if (!access_ok(VERIFY_WRITE, p32, sizeof(*p32)))
		return -EFAULT;
	return __put_v4l2_format32(p64, p32);
}

static int put_v4l2_create32(struct v4l2_create_buffers __user *p64,
			     struct v4l2_create_buffers32 __user *p32)
{
	if (!access_ok(VERIFY_WRITE, p32, sizeof(*p32)) ||
	    copy_in_user(p32, p64,
			 offsetof(struct v4l2_create_buffers32, format)) ||
	    copy_in_user(p32->reserved, p64->reserved, sizeof(p64->reserved)))
		return -EFAULT;
	return __put_v4l2_format32(&p64->format, &p32->format);
}

struct v4l2_standard32 {
	__u32		     index;
	compat_u64	     id;
	__u8		     name[24];
	struct v4l2_fract    frameperiod; /* Frames, not fields */
	__u32		     framelines;
	__u32		     reserved[4];
};

static int get_v4l2_standard32(struct v4l2_standard __user *p64,
			       struct v4l2_standard32 __user *p32)
{
	/* other fields are not set by the user, nor used by the driver */
	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
	    assign_in_user(&p64->index, &p32->index))
		return -EFAULT;
	return 0;
}

static int put_v4l2_standard32(struct v4l2_standard __user *p64,
			       struct v4l2_standard32 __user *p32)
{
	if (!access_ok(VERIFY_WRITE, p32, sizeof(*p32)) ||
	    assign_in_user(&p32->index, &p64->index) ||
	    assign_in_user(&p32->id, &p64->id) ||
	    copy_in_user(p32->name, p64->name, sizeof(p32->name)) ||
	    copy_in_user(&p32->frameperiod, &p64->frameperiod,
			 sizeof(p32->frameperiod)) ||
	    assign_in_user(&p32->framelines, &p64->framelines) ||
	    copy_in_user(p32->reserved, p64->reserved, sizeof(p32->reserved)))
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

static int get_v4l2_plane32(struct v4l2_plane __user *p64,
			    struct v4l2_plane32 __user *p32,
			    enum v4l2_memory memory)
{
	compat_ulong_t p;

	if (copy_in_user(p64, p32, 2 * sizeof(__u32)) ||
	    copy_in_user(&p64->data_offset, &p32->data_offset,
			 sizeof(p64->data_offset)))
		return -EFAULT;

	switch (memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_OVERLAY:
		if (copy_in_user(&p64->m.mem_offset, &p32->m.mem_offset,
				 sizeof(p32->m.mem_offset)))
			return -EFAULT;
		break;
	case V4L2_MEMORY_USERPTR:
		if (get_user(p, &p32->m.userptr) ||
		    put_user((unsigned long)compat_ptr(p), &p64->m.userptr))
			return -EFAULT;
		break;
	case V4L2_MEMORY_DMABUF:
		if (copy_in_user(&p64->m.fd, &p32->m.fd, sizeof(p32->m.fd)))
			return -EFAULT;
		break;
	}

	return 0;
}

static int put_v4l2_plane32(struct v4l2_plane __user *p64,
			    struct v4l2_plane32 __user *p32,
			    enum v4l2_memory memory)
{
	unsigned long p;

	if (copy_in_user(p32, p64, 2 * sizeof(__u32)) ||
	    copy_in_user(&p32->data_offset, &p64->data_offset,
			 sizeof(p64->data_offset)))
		return -EFAULT;

	switch (memory) {
	case V4L2_MEMORY_MMAP:
	case V4L2_MEMORY_OVERLAY:
		if (copy_in_user(&p32->m.mem_offset, &p64->m.mem_offset,
				 sizeof(p64->m.mem_offset)))
			return -EFAULT;
		break;
	case V4L2_MEMORY_USERPTR:
		if (get_user(p, &p64->m.userptr) ||
		    put_user((compat_ulong_t)ptr_to_compat((void __user *)p),
			     &p32->m.userptr))
			return -EFAULT;
		break;
	case V4L2_MEMORY_DMABUF:
		if (copy_in_user(&p32->m.fd, &p64->m.fd, sizeof(p64->m.fd)))
			return -EFAULT;
		break;
	}

	return 0;
}

static int bufsize_v4l2_buffer(struct v4l2_buffer32 __user *p32, u32 *size)
{
	u32 type;
	u32 length;

	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
	    get_user(type, &p32->type) ||
	    get_user(length, &p32->length))
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

static int get_v4l2_buffer32(struct v4l2_buffer __user *p64,
			     struct v4l2_buffer32 __user *p32,
			     void __user *aux_buf, u32 aux_space)
{
	u32 type;
	u32 length;
	enum v4l2_memory memory;
	struct v4l2_plane32 __user *uplane32;
	struct v4l2_plane __user *uplane;
	compat_caddr_t p;
	int ret;

	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
	    assign_in_user(&p64->index, &p32->index) ||
	    get_user(type, &p32->type) ||
	    put_user(type, &p64->type) ||
	    assign_in_user(&p64->flags, &p32->flags) ||
	    get_user(memory, &p32->memory) ||
	    put_user(memory, &p64->memory) ||
	    get_user(length, &p32->length) ||
	    put_user(length, &p64->length))
		return -EFAULT;

	if (V4L2_TYPE_IS_OUTPUT(type))
		if (assign_in_user(&p64->bytesused, &p32->bytesused) ||
		    assign_in_user(&p64->field, &p32->field) ||
		    assign_in_user(&p64->timestamp.tv_sec,
				   &p32->timestamp.tv_sec) ||
		    assign_in_user(&p64->timestamp.tv_usec,
				   &p32->timestamp.tv_usec))
			return -EFAULT;

	if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
		u32 num_planes = length;

		if (num_planes == 0) {
			/*
			 * num_planes == 0 is legal, e.g. when userspace doesn't
			 * need planes array on DQBUF
			 */
			return put_user(NULL, &p64->m.planes);
		}
		if (num_planes > VIDEO_MAX_PLANES)
			return -EINVAL;

		if (get_user(p, &p32->m.planes))
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
		if (put_user_force(uplane, &p64->m.planes))
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
			if (assign_in_user(&p64->m.offset, &p32->m.offset))
				return -EFAULT;
			break;
		case V4L2_MEMORY_USERPTR: {
			compat_ulong_t userptr;

			if (get_user(userptr, &p32->m.userptr) ||
			    put_user((unsigned long)compat_ptr(userptr),
				     &p64->m.userptr))
				return -EFAULT;
			break;
		}
		case V4L2_MEMORY_DMABUF:
			if (assign_in_user(&p64->m.fd, &p32->m.fd))
				return -EFAULT;
			break;
		}
	}

	return 0;
}

static int put_v4l2_buffer32(struct v4l2_buffer __user *p64,
			     struct v4l2_buffer32 __user *p32)
{
	u32 type;
	u32 length;
	enum v4l2_memory memory;
	struct v4l2_plane32 __user *uplane32;
	struct v4l2_plane *uplane;
	compat_caddr_t p;
	int ret;

	if (!access_ok(VERIFY_WRITE, p32, sizeof(*p32)) ||
	    assign_in_user(&p32->index, &p64->index) ||
	    get_user(type, &p64->type) ||
	    put_user(type, &p32->type) ||
	    assign_in_user(&p32->flags, &p64->flags) ||
	    get_user(memory, &p64->memory) ||
	    put_user(memory, &p32->memory))
		return -EFAULT;

	if (assign_in_user(&p32->bytesused, &p64->bytesused) ||
	    assign_in_user(&p32->field, &p64->field) ||
	    assign_in_user(&p32->timestamp.tv_sec, &p64->timestamp.tv_sec) ||
	    assign_in_user(&p32->timestamp.tv_usec, &p64->timestamp.tv_usec) ||
	    copy_in_user(&p32->timecode, &p64->timecode, sizeof(p64->timecode)) ||
	    assign_in_user(&p32->sequence, &p64->sequence) ||
	    assign_in_user(&p32->reserved2, &p64->reserved2) ||
	    assign_in_user(&p32->reserved, &p64->reserved) ||
	    get_user(length, &p64->length) ||
	    put_user(length, &p32->length))
		return -EFAULT;

	if (V4L2_TYPE_IS_MULTIPLANAR(type)) {
		u32 num_planes = length;

		if (num_planes == 0)
			return 0;
		/* We need to define uplane without __user, even though
		 * it does point to data in userspace here. The reason is
		 * that v4l2-ioctl.c copies it from userspace to kernelspace,
		 * so its definition in videodev2.h doesn't have a
		 * __user markup. Defining uplane with __user causes
		 * smatch warnings, so instead declare it without __user
		 * and cast it as a userspace pointer to put_v4l2_plane32().
		 */
		if (get_user(uplane, &p64->m.planes))
			return -EFAULT;
		if (get_user(p, &p32->m.planes))
			return -EFAULT;
		uplane32 = compat_ptr(p);

		while (num_planes--) {
			ret = put_v4l2_plane32((void __user *)uplane,
					       uplane32, memory);
			if (ret)
				return ret;
			++uplane;
			++uplane32;
		}
	} else {
		switch (memory) {
		case V4L2_MEMORY_MMAP:
		case V4L2_MEMORY_OVERLAY:
			if (assign_in_user(&p32->m.offset, &p64->m.offset))
				return -EFAULT;
			break;
		case V4L2_MEMORY_USERPTR:
			if (assign_in_user(&p32->m.userptr, &p64->m.userptr))
				return -EFAULT;
			break;
		case V4L2_MEMORY_DMABUF:
			if (assign_in_user(&p32->m.fd, &p64->m.fd))
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

static int get_v4l2_framebuffer32(struct v4l2_framebuffer __user *p64,
				  struct v4l2_framebuffer32 __user *p32)
{
	compat_caddr_t tmp;

	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
	    get_user(tmp, &p32->base) ||
	    put_user_force(compat_ptr(tmp), &p64->base) ||
	    assign_in_user(&p64->capability, &p32->capability) ||
	    assign_in_user(&p64->flags, &p32->flags) ||
	    copy_in_user(&p64->fmt, &p32->fmt, sizeof(p64->fmt)))
		return -EFAULT;
	return 0;
}

static int put_v4l2_framebuffer32(struct v4l2_framebuffer __user *p64,
				  struct v4l2_framebuffer32 __user *p32)
{
	void *base;

	if (!access_ok(VERIFY_WRITE, p32, sizeof(*p32)) ||
	    get_user(base, &p64->base) ||
	    put_user(ptr_to_compat((void __user *)base), &p32->base) ||
	    assign_in_user(&p32->capability, &p64->capability) ||
	    assign_in_user(&p32->flags, &p64->flags) ||
	    copy_in_user(&p32->fmt, &p64->fmt, sizeof(p64->fmt)))
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
static inline int get_v4l2_input32(struct v4l2_input __user *p64,
				   struct v4l2_input32 __user *p32)
{
	if (copy_in_user(p64, p32, sizeof(*p32)))
		return -EFAULT;
	return 0;
}

static inline int put_v4l2_input32(struct v4l2_input __user *p64,
				   struct v4l2_input32 __user *p32)
{
	if (copy_in_user(p32, p64, sizeof(*p32)))
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

static int bufsize_v4l2_ext_controls(struct v4l2_ext_controls32 __user *p32,
				     u32 *size)
{
	u32 count;

	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
	    get_user(count, &p32->count))
		return -EFAULT;
	if (count > V4L2_CID_MAX_CTRLS)
		return -EINVAL;
	*size = count * sizeof(struct v4l2_ext_control);
	return 0;
}

static int get_v4l2_ext_controls32(struct file *file,
				   struct v4l2_ext_controls __user *p64,
				   struct v4l2_ext_controls32 __user *p32,
				   void __user *aux_buf, u32 aux_space)
{
	struct v4l2_ext_control32 __user *ucontrols;
	struct v4l2_ext_control __user *kcontrols;
	u32 count;
	u32 n;
	compat_caddr_t p;

	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
	    assign_in_user(&p64->which, &p32->which) ||
	    get_user(count, &p32->count) ||
	    put_user(count, &p64->count) ||
	    assign_in_user(&p64->error_idx, &p32->error_idx) ||
	    copy_in_user(p64->reserved, p32->reserved, sizeof(p64->reserved)))
		return -EFAULT;

	if (count == 0)
		return put_user(NULL, &p64->controls);
	if (count > V4L2_CID_MAX_CTRLS)
		return -EINVAL;
	if (get_user(p, &p32->controls))
		return -EFAULT;
	ucontrols = compat_ptr(p);
	if (!access_ok(VERIFY_READ, ucontrols, count * sizeof(*ucontrols)))
		return -EFAULT;
	if (aux_space < count * sizeof(*kcontrols))
		return -EFAULT;
	kcontrols = aux_buf;
	if (put_user_force(kcontrols, &p64->controls))
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
				   struct v4l2_ext_controls __user *p64,
				   struct v4l2_ext_controls32 __user *p32)
{
	struct v4l2_ext_control32 __user *ucontrols;
	struct v4l2_ext_control *kcontrols;
	u32 count;
	u32 n;
	compat_caddr_t p;

	/*
	 * We need to define kcontrols without __user, even though it does
	 * point to data in userspace here. The reason is that v4l2-ioctl.c
	 * copies it from userspace to kernelspace, so its definition in
	 * videodev2.h doesn't have a __user markup. Defining kcontrols
	 * with __user causes smatch warnings, so instead declare it
	 * without __user and cast it as a userspace pointer where needed.
	 */
	if (!access_ok(VERIFY_WRITE, p32, sizeof(*p32)) ||
	    assign_in_user(&p32->which, &p64->which) ||
	    get_user(count, &p64->count) ||
	    put_user(count, &p32->count) ||
	    assign_in_user(&p32->error_idx, &p64->error_idx) ||
	    copy_in_user(p32->reserved, p64->reserved, sizeof(p32->reserved)) ||
	    get_user(kcontrols, &p64->controls))
		return -EFAULT;

	if (!count || count > (U32_MAX/sizeof(*ucontrols)))
		return 0;
	if (get_user(p, &p32->controls))
		return -EFAULT;
	ucontrols = compat_ptr(p);
	if (!access_ok(VERIFY_WRITE, ucontrols, count * sizeof(*ucontrols)))
		return -EFAULT;

	for (n = 0; n < count; n++) {
		unsigned int size = sizeof(*ucontrols);
		u32 id;

		if (get_user_cast(id, &kcontrols->id) ||
		    put_user(id, &ucontrols->id) ||
		    assign_in_user_cast(&ucontrols->size, &kcontrols->size) ||
		    copy_in_user(&ucontrols->reserved2,
				 (void __user *)&kcontrols->reserved2,
				 sizeof(ucontrols->reserved2)))
			return -EFAULT;

		/*
		 * Do not modify the pointer when copying a pointer control.
		 * The contents of the pointer was changed, not the pointer
		 * itself.
		 */
		if (ctrl_is_pointer(file, id))
			size -= sizeof(ucontrols->value64);

		if (copy_in_user(ucontrols,
				 (void __user *)kcontrols, size))
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

static int put_v4l2_event32(struct v4l2_event __user *p64,
			    struct v4l2_event32 __user *p32)
{
	if (!access_ok(VERIFY_WRITE, p32, sizeof(*p32)) ||
	    assign_in_user(&p32->type, &p64->type) ||
	    copy_in_user(&p32->u, &p64->u, sizeof(p64->u)) ||
	    assign_in_user(&p32->pending, &p64->pending) ||
	    assign_in_user(&p32->sequence, &p64->sequence) ||
	    assign_in_user(&p32->timestamp.tv_sec, &p64->timestamp.tv_sec) ||
	    assign_in_user(&p32->timestamp.tv_nsec, &p64->timestamp.tv_nsec) ||
	    assign_in_user(&p32->id, &p64->id) ||
	    copy_in_user(p32->reserved, p64->reserved, sizeof(p32->reserved)))
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

static int get_v4l2_edid32(struct v4l2_edid __user *p64,
			   struct v4l2_edid32 __user *p32)
{
	compat_uptr_t tmp;

	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
	    assign_in_user(&p64->pad, &p32->pad) ||
	    assign_in_user(&p64->start_block, &p32->start_block) ||
	    assign_in_user_cast(&p64->blocks, &p32->blocks) ||
	    get_user(tmp, &p32->edid) ||
	    put_user_force(compat_ptr(tmp), &p64->edid) ||
	    copy_in_user(p64->reserved, p32->reserved, sizeof(p64->reserved)))
		return -EFAULT;
	return 0;
}

static int put_v4l2_edid32(struct v4l2_edid __user *p64,
			   struct v4l2_edid32 __user *p32)
{
	void *edid;

	if (!access_ok(VERIFY_WRITE, p32, sizeof(*p32)) ||
	    assign_in_user(&p32->pad, &p64->pad) ||
	    assign_in_user(&p32->start_block, &p64->start_block) ||
	    assign_in_user(&p32->blocks, &p64->blocks) ||
	    get_user(edid, &p64->edid) ||
	    put_user(ptr_to_compat((void __user *)edid), &p32->edid) ||
	    copy_in_user(p32->reserved, p64->reserved, sizeof(p32->reserved)))
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

#define VIDIOC_OVERLAY32	_IOW ('V', 14, s32)
#define VIDIOC_STREAMON32	_IOW ('V', 18, s32)
#define VIDIOC_STREAMOFF32	_IOW ('V', 19, s32)
#define VIDIOC_G_INPUT32	_IOR ('V', 38, s32)
#define VIDIOC_S_INPUT32	_IOWR('V', 39, s32)
#define VIDIOC_G_OUTPUT32	_IOR ('V', 46, s32)
#define VIDIOC_S_OUTPUT32	_IOWR('V', 47, s32)

/**
 * alloc_userspace() - Allocates a 64-bits userspace pointer compatible
 *	for calling the native 64-bits version of an ioctl.
 *
 * @size:	size of the structure itself to be allocated.
 * @aux_space:	extra size needed to store "extra" data, e.g. space for
 *		other __user data that is pointed to fields inside the
 *		structure.
 * @new_p64:	pointer to a pointer to be filled with the allocated struct.
 *
 * Return:
 *
 * if it can't allocate memory, either -ENOMEM or -EFAULT will be returned.
 * Zero otherwise.
 */
static int alloc_userspace(unsigned int size, u32 aux_space,
			   void __user **new_p64)
{
	*new_p64 = compat_alloc_user_space(size + aux_space);
	if (!*new_p64)
		return -ENOMEM;
	if (clear_user(*new_p64, size))
		return -EFAULT;
	return 0;
}

/**
 * do_video_ioctl() - Ancillary function with handles a compat32 ioctl call
 *
 * @file: pointer to &struct file with the file handler
 * @cmd: ioctl to be called
 * @arg: arguments passed from/to the ioctl handler
 *
 * This function is called when a 32 bits application calls a V4L2 ioctl
 * and the Kernel is compiled with 64 bits.
 *
 * This function is called by v4l2_compat_ioctl32() when the function is
 * not private to some specific driver.
 *
 * It converts a 32-bits struct into a 64 bits one, calls the native 64-bits
 * ioctl handler and fills back the 32-bits struct with the results of the
 * native call.
 */
static long do_video_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *p32 = compat_ptr(arg);
	void __user *new_p64 = NULL;
	void __user *aux_buf;
	u32 aux_space;
	int compatible_arg = 1;
	long err = 0;
	unsigned int ncmd;

	/*
	 * 1. When struct size is different, converts the command.
	 */
	switch (cmd) {
	case VIDIOC_G_FMT32: ncmd = VIDIOC_G_FMT; break;
	case VIDIOC_S_FMT32: ncmd = VIDIOC_S_FMT; break;
	case VIDIOC_QUERYBUF32: ncmd = VIDIOC_QUERYBUF; break;
	case VIDIOC_G_FBUF32: ncmd = VIDIOC_G_FBUF; break;
	case VIDIOC_S_FBUF32: ncmd = VIDIOC_S_FBUF; break;
	case VIDIOC_QBUF32: ncmd = VIDIOC_QBUF; break;
	case VIDIOC_DQBUF32: ncmd = VIDIOC_DQBUF; break;
	case VIDIOC_ENUMSTD32: ncmd = VIDIOC_ENUMSTD; break;
	case VIDIOC_ENUMINPUT32: ncmd = VIDIOC_ENUMINPUT; break;
	case VIDIOC_TRY_FMT32: ncmd = VIDIOC_TRY_FMT; break;
	case VIDIOC_G_EXT_CTRLS32: ncmd = VIDIOC_G_EXT_CTRLS; break;
	case VIDIOC_S_EXT_CTRLS32: ncmd = VIDIOC_S_EXT_CTRLS; break;
	case VIDIOC_TRY_EXT_CTRLS32: ncmd = VIDIOC_TRY_EXT_CTRLS; break;
	case VIDIOC_DQEVENT32: ncmd = VIDIOC_DQEVENT; break;
	case VIDIOC_OVERLAY32: ncmd = VIDIOC_OVERLAY; break;
	case VIDIOC_STREAMON32: ncmd = VIDIOC_STREAMON; break;
	case VIDIOC_STREAMOFF32: ncmd = VIDIOC_STREAMOFF; break;
	case VIDIOC_G_INPUT32: ncmd = VIDIOC_G_INPUT; break;
	case VIDIOC_S_INPUT32: ncmd = VIDIOC_S_INPUT; break;
	case VIDIOC_G_OUTPUT32: ncmd = VIDIOC_G_OUTPUT; break;
	case VIDIOC_S_OUTPUT32: ncmd = VIDIOC_S_OUTPUT; break;
	case VIDIOC_CREATE_BUFS32: ncmd = VIDIOC_CREATE_BUFS; break;
	case VIDIOC_PREPARE_BUF32: ncmd = VIDIOC_PREPARE_BUF; break;
	case VIDIOC_G_EDID32: ncmd = VIDIOC_G_EDID; break;
	case VIDIOC_S_EDID32: ncmd = VIDIOC_S_EDID; break;
	default: ncmd = cmd; break;
	}

	/*
	 * 2. Allocates a 64-bits userspace pointer to store the
	 * values of the ioctl and copy data from the 32-bits __user
	 * argument into it.
	 */
	switch (cmd) {
	case VIDIOC_OVERLAY32:
	case VIDIOC_STREAMON32:
	case VIDIOC_STREAMOFF32:
	case VIDIOC_S_INPUT32:
	case VIDIOC_S_OUTPUT32:
		err = alloc_userspace(sizeof(unsigned int), 0, &new_p64);
		if (!err && assign_in_user((unsigned int __user *)new_p64,
					   (compat_uint_t __user *)p32))
			err = -EFAULT;
		compatible_arg = 0;
		break;

	case VIDIOC_G_INPUT32:
	case VIDIOC_G_OUTPUT32:
		err = alloc_userspace(sizeof(unsigned int), 0, &new_p64);
		compatible_arg = 0;
		break;

	case VIDIOC_G_EDID32:
	case VIDIOC_S_EDID32:
		err = alloc_userspace(sizeof(struct v4l2_edid), 0, &new_p64);
		if (!err)
			err = get_v4l2_edid32(new_p64, p32);
		compatible_arg = 0;
		break;

	case VIDIOC_G_FMT32:
	case VIDIOC_S_FMT32:
	case VIDIOC_TRY_FMT32:
		err = bufsize_v4l2_format(p32, &aux_space);
		if (!err)
			err = alloc_userspace(sizeof(struct v4l2_format),
					      aux_space, &new_p64);
		if (!err) {
			aux_buf = new_p64 + sizeof(struct v4l2_format);
			err = get_v4l2_format32(new_p64, p32,
						aux_buf, aux_space);
		}
		compatible_arg = 0;
		break;

	case VIDIOC_CREATE_BUFS32:
		err = bufsize_v4l2_create(p32, &aux_space);
		if (!err)
			err = alloc_userspace(sizeof(struct v4l2_create_buffers),
					      aux_space, &new_p64);
		if (!err) {
			aux_buf = new_p64 + sizeof(struct v4l2_create_buffers);
			err = get_v4l2_create32(new_p64, p32,
						aux_buf, aux_space);
		}
		compatible_arg = 0;
		break;

	case VIDIOC_PREPARE_BUF32:
	case VIDIOC_QUERYBUF32:
	case VIDIOC_QBUF32:
	case VIDIOC_DQBUF32:
		err = bufsize_v4l2_buffer(p32, &aux_space);
		if (!err)
			err = alloc_userspace(sizeof(struct v4l2_buffer),
					      aux_space, &new_p64);
		if (!err) {
			aux_buf = new_p64 + sizeof(struct v4l2_buffer);
			err = get_v4l2_buffer32(new_p64, p32,
						aux_buf, aux_space);
		}
		compatible_arg = 0;
		break;

	case VIDIOC_S_FBUF32:
		err = alloc_userspace(sizeof(struct v4l2_framebuffer), 0,
				      &new_p64);
		if (!err)
			err = get_v4l2_framebuffer32(new_p64, p32);
		compatible_arg = 0;
		break;

	case VIDIOC_G_FBUF32:
		err = alloc_userspace(sizeof(struct v4l2_framebuffer), 0,
				      &new_p64);
		compatible_arg = 0;
		break;

	case VIDIOC_ENUMSTD32:
		err = alloc_userspace(sizeof(struct v4l2_standard), 0,
				      &new_p64);
		if (!err)
			err = get_v4l2_standard32(new_p64, p32);
		compatible_arg = 0;
		break;

	case VIDIOC_ENUMINPUT32:
		err = alloc_userspace(sizeof(struct v4l2_input), 0, &new_p64);
		if (!err)
			err = get_v4l2_input32(new_p64, p32);
		compatible_arg = 0;
		break;

	case VIDIOC_G_EXT_CTRLS32:
	case VIDIOC_S_EXT_CTRLS32:
	case VIDIOC_TRY_EXT_CTRLS32:
		err = bufsize_v4l2_ext_controls(p32, &aux_space);
		if (!err)
			err = alloc_userspace(sizeof(struct v4l2_ext_controls),
					      aux_space, &new_p64);
		if (!err) {
			aux_buf = new_p64 + sizeof(struct v4l2_ext_controls);
			err = get_v4l2_ext_controls32(file, new_p64, p32,
						      aux_buf, aux_space);
		}
		compatible_arg = 0;
		break;
	case VIDIOC_DQEVENT32:
		err = alloc_userspace(sizeof(struct v4l2_event), 0, &new_p64);
		compatible_arg = 0;
		break;
	}
	if (err)
		return err;

	/*
	 * 3. Calls the native 64-bits ioctl handler.
	 *
	 * For the functions where a conversion was not needed,
	 * compatible_arg is true, and it will call it with the arguments
	 * provided by userspace and stored at @p32 var.
	 *
	 * Otherwise, it will pass the newly allocated @new_p64 argument.
	 */
	if (compatible_arg)
		err = native_ioctl(file, ncmd, (unsigned long)p32);
	else
		err = native_ioctl(file, ncmd, (unsigned long)new_p64);

	if (err == -ENOTTY)
		return err;

	/*
	 * 4. Special case: even after an error we need to put the
	 * results back for some ioctls.
	 *
	 * In the case of EXT_CTRLS, the error_idx will contain information
	 * on which control failed.
	 *
	 * In the case of S_EDID, the driver can return E2BIG and set
	 * the blocks to maximum allowed value.
	 */
	switch (cmd) {
	case VIDIOC_G_EXT_CTRLS32:
	case VIDIOC_S_EXT_CTRLS32:
	case VIDIOC_TRY_EXT_CTRLS32:
		if (put_v4l2_ext_controls32(file, new_p64, p32))
			err = -EFAULT;
		break;
	case VIDIOC_S_EDID32:
		if (put_v4l2_edid32(new_p64, p32))
			err = -EFAULT;
		break;
	}
	if (err)
		return err;

	/*
	 * 5. Copy the data returned at the 64 bits userspace pointer to
	 * the original 32 bits structure.
	 */
	switch (cmd) {
	case VIDIOC_S_INPUT32:
	case VIDIOC_S_OUTPUT32:
	case VIDIOC_G_INPUT32:
	case VIDIOC_G_OUTPUT32:
		if (assign_in_user((compat_uint_t __user *)p32,
				   ((unsigned int __user *)new_p64)))
			err = -EFAULT;
		break;

	case VIDIOC_G_FBUF32:
		err = put_v4l2_framebuffer32(new_p64, p32);
		break;

	case VIDIOC_DQEVENT32:
		err = put_v4l2_event32(new_p64, p32);
		break;

	case VIDIOC_G_EDID32:
		err = put_v4l2_edid32(new_p64, p32);
		break;

	case VIDIOC_G_FMT32:
	case VIDIOC_S_FMT32:
	case VIDIOC_TRY_FMT32:
		err = put_v4l2_format32(new_p64, p32);
		break;

	case VIDIOC_CREATE_BUFS32:
		err = put_v4l2_create32(new_p64, p32);
		break;

	case VIDIOC_PREPARE_BUF32:
	case VIDIOC_QUERYBUF32:
	case VIDIOC_QBUF32:
	case VIDIOC_DQBUF32:
		err = put_v4l2_buffer32(new_p64, p32);
		break;

	case VIDIOC_ENUMSTD32:
		err = put_v4l2_standard32(new_p64, p32);
		break;

	case VIDIOC_ENUMINPUT32:
		err = put_v4l2_input32(new_p64, p32);
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
