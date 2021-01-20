/*
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

#include <drm/r128_drm.h>

#include "r128_drv.h"

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
	drm_r128_init_t init;

	if (copy_from_user(&init32, (void __user *)arg, sizeof(init32)))
		return -EFAULT;

	init.func = init32.func;
	init.sarea_priv_offset = init32.sarea_priv_offset;
	init.is_pci = init32.is_pci;
	init.cce_mode = init32.cce_mode;
	init.cce_secure = init32.cce_secure;
	init.ring_size = init32.ring_size;
	init.usec_timeout = init32.usec_timeout;
	init.fb_bpp = init32.fb_bpp;
	init.front_offset = init32.front_offset;
	init.front_pitch = init32.front_pitch;
	init.back_offset = init32.back_offset;
	init.back_pitch = init32.back_pitch;
	init.depth_bpp = init32.depth_bpp;
	init.depth_offset = init32.depth_offset;
	init.depth_pitch = init32.depth_pitch;
	init.span_offset = init32.span_offset;
	init.fb_offset = init32.fb_offset;
	init.mmio_offset = init32.mmio_offset;
	init.ring_offset = init32.ring_offset;
	init.ring_rptr_offset = init32.ring_rptr_offset;
	init.buffers_offset = init32.buffers_offset;
	init.agp_textures_offset = init32.agp_textures_offset;

	return drm_ioctl_kernel(file, r128_cce_init, &init,
			DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
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
	drm_r128_depth_t depth;

	if (copy_from_user(&depth32, (void __user *)arg, sizeof(depth32)))
		return -EFAULT;

	depth.func = depth32.func;
	depth.n = depth32.n;
	depth.x = compat_ptr(depth32.x);
	depth.y = compat_ptr(depth32.y);
	depth.buffer = compat_ptr(depth32.buffer);
	depth.mask = compat_ptr(depth32.mask);

	return drm_ioctl_kernel(file, r128_cce_depth, &depth, DRM_AUTH);
}

typedef struct drm_r128_stipple32 {
	u32 mask;
} drm_r128_stipple32_t;

static int compat_r128_stipple(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_r128_stipple32_t stipple32;
	drm_r128_stipple_t stipple;

	if (copy_from_user(&stipple32, (void __user *)arg, sizeof(stipple32)))
		return -EFAULT;

	stipple.mask = compat_ptr(stipple32.mask);

	return drm_ioctl_kernel(file, r128_cce_stipple, &stipple, DRM_AUTH);
}

typedef struct drm_r128_getparam32 {
	int param;
	u32 value;
} drm_r128_getparam32_t;

static int compat_r128_getparam(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	drm_r128_getparam32_t getparam32;
	drm_r128_getparam_t getparam;

	if (copy_from_user(&getparam32, (void __user *)arg, sizeof(getparam32)))
		return -EFAULT;

	getparam.param = getparam32.param;
	getparam.value = compat_ptr(getparam32.value);

	return drm_ioctl_kernel(file, r128_getparam, &getparam, DRM_AUTH);
}

drm_ioctl_compat_t *r128_compat_ioctls[] = {
	[DRM_R128_INIT] = compat_r128_init,
	[DRM_R128_DEPTH] = compat_r128_depth,
	[DRM_R128_STIPPLE] = compat_r128_stipple,
	[DRM_R128_GETPARAM] = compat_r128_getparam,
};

/**
 * r128_compat_ioctl - Called whenever a 32-bit process running under
 *                     a 64-bit kernel performs an ioctl on /dev/dri/card<n>.
 *
 * @filp: file pointer.
 * @cmd: command.
 * @arg: user argument.
 * return: zero on success or negative number on failure.
 */
long r128_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);
	drm_ioctl_compat_t *fn = NULL;
	int ret;

	if (nr < DRM_COMMAND_BASE)
		return drm_compat_ioctl(filp, cmd, arg);

	if (nr < DRM_COMMAND_BASE + ARRAY_SIZE(r128_compat_ioctls))
		fn = r128_compat_ioctls[nr - DRM_COMMAND_BASE];

	if (fn != NULL)
		ret = (*fn) (filp, cmd, arg);
	else
		ret = drm_ioctl(filp, cmd, arg);

	return ret;
}
