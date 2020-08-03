/*
 * 32-bit ioctl compatibility routines for the i915 DRM.
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
 *
 * Author: Alan Hourihane <alanh@fairlite.demon.co.uk>
 */
#include <linux/compat.h>

#include <drm/drm_ioctl.h>

#include "i915_drv.h"
#include "i915_ioc32.h"

struct drm_i915_getparam32 {
	s32 param;
	/*
	 * We screwed up the generic ioctl struct here and used a variable-sized
	 * pointer. Use u32 in the compat struct to match the 32bit pointer
	 * userspace expects.
	 */
	u32 value;
};

static int compat_i915_getparam(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct drm_i915_getparam32 req32;
	struct drm_i915_getparam req;

	if (copy_from_user(&req32, (void __user *)arg, sizeof(req32)))
		return -EFAULT;

	req.param = req32.param;
	req.value = compat_ptr(req32.value);

	return drm_ioctl_kernel(file, i915_getparam_ioctl, &req,
				DRM_RENDER_ALLOW);
}

static drm_ioctl_compat_t *i915_compat_ioctls[] = {
	[DRM_I915_GETPARAM] = compat_i915_getparam,
};

/**
 * i915_ioc32_compat_ioctl - handle the mistakes of the past
 * @filp: the file pointer
 * @cmd: the ioctl command (and encoded flags)
 * @arg: the ioctl argument (from userspace)
 *
 * Called whenever a 32-bit process running under a 64-bit kernel
 * performs an ioctl on /dev/dri/card<n>.
 */
long i915_ioc32_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);
	drm_ioctl_compat_t *fn = NULL;
	int ret;

	if (nr < DRM_COMMAND_BASE || nr >= DRM_COMMAND_END)
		return drm_compat_ioctl(filp, cmd, arg);

	if (nr < DRM_COMMAND_BASE + ARRAY_SIZE(i915_compat_ioctls))
		fn = i915_compat_ioctls[nr - DRM_COMMAND_BASE];

	if (fn != NULL)
		ret = (*fn) (filp, cmd, arg);
	else
		ret = drm_ioctl(filp, cmd, arg);

	return ret;
}
