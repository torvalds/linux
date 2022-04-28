/*
 * @File
 * @Title       Linux DRM definitions shared between kernel and user space.
 * @Codingstyle LinuxKernel
 * @Copyright   1999 Precision Insight, Inc., Cedar Park, Texas.
 *              2000 VA Linux Systems, Inc., Sunnyvale, California.
 *              All rights reserved.
 * @Description This header contains a subset of the Linux kernel DRM uapi
 *              and is designed to be used in kernel and user mode. When
 *              included from kernel mode, it pulls in the full version of
 *              drm.h. Whereas, when included from user mode, it defines a
 *              minimal version of drm.h (as found in libdrm). As such, the
 *              structures and ioctl commands must exactly match those found
 *              in the Linux kernel/libdrm.
 * @License     MIT
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#if !defined(__PVR_DRM_CORE_H__)
#define __PVR_DRM_CORE_H__

#if defined(__KERNEL__)
#include <drm/drm.h>
#else
#include <asm/ioctl.h>
#include <linux/types.h>

#define DRM_IOCTL_BASE			'd'
#define DRM_COMMAND_BASE                0x40

#define DRM_IOWR(nr, type)		_IOWR(DRM_IOCTL_BASE, nr, type)

struct drm_version {
	int version_major;
	int version_minor;
	int version_patchlevel;
	__kernel_size_t name_len;
	char *name;
	__kernel_size_t date_len;
	char *date;
	__kernel_size_t desc_len;
	char *desc;
};

struct drm_set_version {
	int drm_di_major;
	int drm_di_minor;
	int drm_dd_major;
	int drm_dd_minor;
};

#define DRM_IOCTL_VERSION		DRM_IOWR(0x00, struct drm_version)
#define DRM_IOCTL_SET_VERSION		DRM_IOWR(0x07, struct drm_set_version)
#endif

#endif
