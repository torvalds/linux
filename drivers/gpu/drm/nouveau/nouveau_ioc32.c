/**
 * \file mga_ioc32.c
 *
 * 32-bit ioctl compatibility routines for the MGA DRM.
 *
 * \author Dave Airlie <airlied@linux.ie> with code from patches by Egbert Eich
 *
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

#include <drm/drmP.h>

#include "nouveau_ioctl.h"

/**
 * Called whenever a 32-bit process running under a 64-bit kernel
 * performs an ioctl on /dev/dri/card<n>.
 *
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument.
 * \return zero on success or negative number on failure.
 */
long nouveau_compat_ioctl(struct file *filp, unsigned int cmd,
			 unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);
	drm_ioctl_compat_t *fn = NULL;
	int ret;

	if (nr < DRM_COMMAND_BASE)
		return drm_compat_ioctl(filp, cmd, arg);

#if 0
	if (nr < DRM_COMMAND_BASE + DRM_ARRAY_SIZE(mga_compat_ioctls))
		fn = nouveau_compat_ioctls[nr - DRM_COMMAND_BASE];
#endif
	if (fn != NULL)
		ret = (*fn)(filp, cmd, arg);
	else
		ret = nouveau_drm_ioctl(filp, cmd, arg);

	return ret;
}
