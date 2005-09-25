/**
 * \file r128_ioc32.c
 *
 * 32-bit ioctl compatibility routines for the R128 DRM.
 *
 * \author Dave Airlie <airlied@linux.ie> with code from patches by Egbert Eich
 *
 * Copyright (C) Paul Mackerras 2005
 * Copyright (C) Egbert Eich 2003,2004
 * Copyright (C) Dave Airlie 2005
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
#include "r128_drm.h"

typedef struct drm_r128_init32 {
	int func;
	unsigned int sarea_priv_offset;
	int is_pci;
	int cce_mode;
	int cce_secure;
	int ring_size;
	int usec_timeout;

	unsigned int fb_bpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;
	unsigned int depth_bpp;
	unsigned int depth_offset, depth_pitch;
	unsigned int span_offset;

	unsigned int fb_offset;
	unsigned int mmio_offset;
	unsigned int ring_offset;
	unsigned int ring_rptr_offset;
	unsigned int buffers_offset;
	unsigned int agp_textures_offset;
} drm_r128_init32_t;

static int compat_r128_init(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	drm_r128_init32_t init32;
	drm_r128_init_t __user *init;

	if (copy_from_user(&init32, (void __user *)arg, sizeof(init32)))
		return -EFAULT;

	init = compat_alloc_user_space(sizeof(*init));
	if (!access_ok(VERIFY_WRITE, init, sizeof(*init))
	    || __put_user(init32.func, &init->func)
	    || __put_user(init32.sarea_priv_offset, &init->sarea_priv_offset)
	    || __put_user(init32.is_pci, &init->is_pci)
	    || __put_user(init32.cce_mode, &init->cce_mode)
	    || __put_user(init32.cce_secure, &init->cce_secure)
	    || __put_user(init32.ring_size, &init->ring_size)
	    || __put_user(init32.usec_timeout, &init->usec_timeout)
	    || __put_user(init32.fb_bpp, &init->fb_bpp)
	    || __put_user(init32.front_offset, &init->front_offset)
	    || __put_user(init32.front_pitch, &init->front_pitch)
	    || __put_user(init32.back_offset, &init->back_offset)
	    || __put_user(init32.back_pitch, &init->back_pitch)
	    || __put_user(init32.depth_bpp, &init->depth_bpp)
	    || __put_user(init32.depth_offset, &init->depth_offset)
	    || __put_user(init32.depth_pitch, &init->depth_pitch)
	    || __put_user(init32.span_offset, &init->span_offset)
	    || __put_user(init32.fb_offset, &init->fb_offset)
	    || __put_user(init32.mmio_offset, &init->mmio_offset)
	    || __put_user(init32.ring_offset, &init->ring_offset)
	    || __put_user(init32.ring_rptr_offset, &init->ring_rptr_offset)
	    || __put_user(init32.buffers_offset, &init->buffers_offset)
	    || __put_user(init32.agp_textures_offset,
			  &init->agp_textures_offset))
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_R128_INIT, (unsigned long)init);
}

typedef struct drm_r128_depth32 {
	int func;
	int n;
	u32 x;
	u32 y;
	u32 buffer;
	u32 mask;
} drm_r128_depth32_t;

static int compat_r128_depth(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	drm_r128_depth32_t depth32;
	drm_r128_depth_t __user *depth;

	if (copy_from_user(&depth32, (void __user *)arg, sizeof(depth32)))
		return -EFAULT;

	depth = compat_alloc_user_space(sizeof(*depth));
	if (!access_ok(VERIFY_WRITE, depth, sizeof(*depth))
	    || __put_user(depth32.func, &depth->func)
	    || __put_user(depth32.n, &depth->n)
	    || __put_user((int __user *)(unsigned long)depth32.x, &depth->x)
	    || __put_user((int __user *)(unsigned long)depth32.y, &depth->y)
	    || __put_user((unsigned int __user *)(unsigned long)depth32.buffer,
			  &depth->buffer)
	    || __put_user((unsigned char __user *)(unsigned long)depth32.mask,
			  &depth->mask))
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_R128_DEPTH, (unsigned long)depth);

}

typedef struct drm_r128_stipple32 {
	u32 mask;
} drm_r128_stipple32_t;

static int compat_r128_stipple(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_r128_stipple32_t stipple32;
	drm_r128_stipple_t __user *stipple;

	if (copy_from_user(&stipple32, (void __user *)arg, sizeof(stipple32)))
		return -EFAULT;

	stipple = compat_alloc_user_space(sizeof(*stipple));
	if (!access_ok(VERIFY_WRITE, stipple, sizeof(*stipple))
	    || __put_user((unsigned int __user *)(unsigned long)stipple32.mask,
			  &stipple->mask))
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_R128_STIPPLE, (unsigned long)stipple);
}

typedef struct drm_r128_getparam32 {
	int param;
	u32 value;
} drm_r128_getparam32_t;

static int compat_r128_getparam(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	drm_r128_getparam32_t getparam32;
	drm_r128_getparam_t __user *getparam;

	if (copy_from_user(&getparam32, (void __user *)arg, sizeof(getparam32)))
		return -EFAULT;

	getparam = compat_alloc_user_space(sizeof(*getparam));
	if (!access_ok(VERIFY_WRITE, getparam, sizeof(*getparam))
	    || __put_user(getparam32.param, &getparam->param)
	    || __put_user((void __user *)(unsigned long)getparam32.value,
			  &getparam->value))
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_R128_GETPARAM, (unsigned long)getparam);
}

drm_ioctl_compat_t *r128_compat_ioctls[] = {
	[DRM_R128_INIT] = compat_r128_init,
	[DRM_R128_DEPTH] = compat_r128_depth,
	[DRM_R128_STIPPLE] = compat_r128_stipple,
	[DRM_R128_GETPARAM] = compat_r128_getparam,
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
long r128_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);
	drm_ioctl_compat_t *fn = NULL;
	int ret;

	if (nr < DRM_COMMAND_BASE)
		return drm_compat_ioctl(filp, cmd, arg);

	if (nr < DRM_COMMAND_BASE + DRM_ARRAY_SIZE(r128_compat_ioctls))
		fn = r128_compat_ioctls[nr - DRM_COMMAND_BASE];

	lock_kernel();		/* XXX for now */
	if (fn != NULL)
		ret = (*fn) (filp, cmd, arg);
	else
		ret = drm_ioctl(filp->f_dentry->d_inode, filp, cmd, arg);
	unlock_kernel();

	return ret;
}
