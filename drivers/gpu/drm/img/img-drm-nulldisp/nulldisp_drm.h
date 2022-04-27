/*
 * @File
 * @Title       Nulldisp DRM definitions shared between kernel and user space.
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
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
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#if !defined(__NULLDISP_DRM_H__)
#define __NULLDISP_DRM_H__

#if defined(__KERNEL__)
#include <drm/drm.h>
#else
#include <drm.h>
#endif

struct drm_nulldisp_gem_create {
	__u64 size;   /* in */
	__u32 flags;  /* in */
	__u32 handle; /* out */
};

struct drm_nulldisp_gem_mmap {
	__u32 handle; /* in */
	__u32 pad;
	__u64 offset; /* out */
};

#define NULLDISP_GEM_CPU_PREP_READ   (1 << 0)
#define NULLDISP_GEM_CPU_PREP_WRITE  (1 << 1)
#define NULLDISP_GEM_CPU_PREP_NOWAIT (1 << 2)

struct drm_nulldisp_gem_cpu_prep {
	__u32 handle; /* in */
	__u32 flags;  /* in */
};

struct drm_nulldisp_gem_cpu_fini {
	__u32 handle; /* in */
	__u32 pad;
};

/*
 * DRM command numbers, relative to DRM_COMMAND_BASE.
 * These defines must be prefixed with "DRM_".
 */
#define DRM_NULLDISP_GEM_CREATE   0x00
#define DRM_NULLDISP_GEM_MMAP     0x01
#define DRM_NULLDISP_GEM_CPU_PREP 0x02
#define DRM_NULLDISP_GEM_CPU_FINI 0x03

/* These defines must be prefixed with "DRM_IOCTL_". */
#define DRM_IOCTL_NULLDISP_GEM_CREATE \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NULLDISP_GEM_CREATE, \
		 struct drm_nulldisp_gem_create)

#define DRM_IOCTL_NULLDISP_GEM_MMAP \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_NULLDISP_GEM_MMAP, \
		 struct drm_nulldisp_gem_mmap)

#define DRM_IOCTL_NULLDISP_GEM_CPU_PREP \
	DRM_IOW(DRM_COMMAND_BASE + DRM_NULLDISP_GEM_CPU_PREP, \
		struct drm_nulldisp_gem_cpu_prep)

#define DRM_IOCTL_NULLDISP_GEM_CPU_FINI \
	DRM_IOW(DRM_COMMAND_BASE + DRM_NULLDISP_GEM_CPU_FINI, \
		struct drm_nulldisp_gem_cpu_fini)

#endif /* defined(__NULLDISP_DRM_H__) */
