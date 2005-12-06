/**
 * \file i915_ioc32.c
 *
 * 32-bit ioctl compatibility routines for the i915 DRM.
 *
 * \author Alan Hourihane <alanh@fairlite.demon.co.uk>
 *
 *
 * Copyright (C) Paul Mackerras 2005
 * Copyright (C) Alan Hourihane 2005
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <linux/compat.h>
#include <linux/ioctl32.h>

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"

typedef struct _drm_i915_batchbuffer32 {
	int start;		/* agp offset */
	int used;		/* nr bytes in use */
	int DR1;		/* hw flags for GFX_OP_DRAWRECT_INFO */
	int DR4;		/* window origin for GFX_OP_DRAWRECT_INFO */
	int num_cliprects;	/* mulitpass with multiple cliprects? */
	u32 cliprects;		/* pointer to userspace cliprects */
} drm_i915_batchbuffer32_t;

static int compat_i915_batchbuffer(struct file *file, unsigned int cmd,
				   unsigned long arg)
{
	drm_i915_batchbuffer32_t batchbuffer32;
	drm_i915_batchbuffer_t __user *batchbuffer;

	if (copy_from_user
	    (&batchbuffer32, (void __user *)arg, sizeof(batchbuffer32)))
		return -EFAULT;

	batchbuffer = compat_alloc_user_space(sizeof(*batchbuffer));
	if (!access_ok(VERIFY_WRITE, batchbuffer, sizeof(*batchbuffer))
	    || __put_user(batchbuffer32.start, &batchbuffer->start)
	    || __put_user(batchbuffer32.used, &batchbuffer->used)
	    || __put_user(batchbuffer32.DR1, &batchbuffer->DR1)
	    || __put_user(batchbuffer32.DR4, &batchbuffer->DR4)
	    || __put_user(batchbuffer32.num_cliprects,
			  &batchbuffer->num_cliprects)
	    || __put_user((int __user *)(unsigned long)batchbuffer32.cliprects,
			  &batchbuffer->cliprects))
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_I915_BATCHBUFFER,
			 (unsigned long)batchbuffer);
}

typedef struct _drm_i915_cmdbuffer32 {
	u32 buf;		/* pointer to userspace command buffer */
	int sz;			/* nr bytes in buf */
	int DR1;		/* hw flags for GFX_OP_DRAWRECT_INFO */
	int DR4;		/* window origin for GFX_OP_DRAWRECT_INFO */
	int num_cliprects;	/* mulitpass with multiple cliprects? */
	u32 cliprects;		/* pointer to userspace cliprects */
} drm_i915_cmdbuffer32_t;

static int compat_i915_cmdbuffer(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	drm_i915_cmdbuffer32_t cmdbuffer32;
	drm_i915_cmdbuffer_t __user *cmdbuffer;

	if (copy_from_user
	    (&cmdbuffer32, (void __user *)arg, sizeof(cmdbuffer32)))
		return -EFAULT;

	cmdbuffer = compat_alloc_user_space(sizeof(*cmdbuffer));
	if (!access_ok(VERIFY_WRITE, cmdbuffer, sizeof(*cmdbuffer))
	    || __put_user((int __user *)(unsigned long)cmdbuffer32.buf,
			  &cmdbuffer->buf)
	    || __put_user(cmdbuffer32.sz, &cmdbuffer->sz)
	    || __put_user(cmdbuffer32.DR1, &cmdbuffer->DR1)
	    || __put_user(cmdbuffer32.DR4, &cmdbuffer->DR4)
	    || __put_user(cmdbuffer32.num_cliprects, &cmdbuffer->num_cliprects)
	    || __put_user((int __user *)(unsigned long)cmdbuffer32.cliprects,
			  &cmdbuffer->cliprects))
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_I915_CMDBUFFER, (unsigned long)cmdbuffer);
}

typedef struct drm_i915_irq_emit32 {
	u32 irq_seq;
} drm_i915_irq_emit32_t;

static int compat_i915_irq_emit(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	drm_i915_irq_emit32_t req32;
	drm_i915_irq_emit_t __user *request;

	if (copy_from_user(&req32, (void __user *)arg, sizeof(req32)))
		return -EFAULT;

	request = compat_alloc_user_space(sizeof(*request));
	if (!access_ok(VERIFY_WRITE, request, sizeof(*request))
	    || __put_user((int __user *)(unsigned long)req32.irq_seq,
			  &request->irq_seq))
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_I915_IRQ_EMIT, (unsigned long)request);
}
typedef struct drm_i915_getparam32 {
	int param;
	u32 value;
} drm_i915_getparam32_t;

static int compat_i915_getparam(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	drm_i915_getparam32_t req32;
	drm_i915_getparam_t __user *request;

	if (copy_from_user(&req32, (void __user *)arg, sizeof(req32)))
		return -EFAULT;

	request = compat_alloc_user_space(sizeof(*request));
	if (!access_ok(VERIFY_WRITE, request, sizeof(*request))
	    || __put_user(req32.param, &request->param)
	    || __put_user((void __user *)(unsigned long)req32.value,
			  &request->value))
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_I915_GETPARAM, (unsigned long)request);
}

typedef struct drm_i915_mem_alloc32 {
	int region;
	int alignment;
	int size;
	u32 region_offset;	/* offset from start of fb or agp */
} drm_i915_mem_alloc32_t;

static int compat_i915_alloc(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	drm_i915_mem_alloc32_t req32;
	drm_i915_mem_alloc_t __user *request;

	if (copy_from_user(&req32, (void __user *)arg, sizeof(req32)))
		return -EFAULT;

	request = compat_alloc_user_space(sizeof(*request));
	if (!access_ok(VERIFY_WRITE, request, sizeof(*request))
	    || __put_user(req32.region, &request->region)
	    || __put_user(req32.alignment, &request->alignment)
	    || __put_user(req32.size, &request->size)
	    || __put_user((void __user *)(unsigned long)req32.region_offset,
			  &request->region_offset))
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_I915_ALLOC, (unsigned long)request);
}

drm_ioctl_compat_t *i915_compat_ioctls[] = {
	[DRM_I915_BATCHBUFFER] = compat_i915_batchbuffer,
	[DRM_I915_CMDBUFFER] = compat_i915_cmdbuffer,
	[DRM_I915_GETPARAM] = compat_i915_getparam,
	[DRM_I915_IRQ_EMIT] = compat_i915_irq_emit,
	[DRM_I915_ALLOC] = compat_i915_alloc
};

/**
 * Called whenever a 32-bit process running under a 64-bit kernel
 * performs an ioctl on /dev/dri/card<n>.
 *
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument.
 * \return zero on success or negative number on failure.
 */
long i915_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);
	drm_ioctl_compat_t *fn = NULL;
	int ret;

	if (nr < DRM_COMMAND_BASE)
		return drm_compat_ioctl(filp, cmd, arg);

	if (nr < DRM_COMMAND_BASE + DRM_ARRAY_SIZE(i915_compat_ioctls))
		fn = i915_compat_ioctls[nr - DRM_COMMAND_BASE];

	lock_kernel();		/* XXX for now */
	if (fn != NULL)
		ret = (*fn) (filp, cmd, arg);
	else
		ret = drm_ioctl(filp->f_dentry->d_inode, filp, cmd, arg);
	unlock_kernel();

	return ret;
}
