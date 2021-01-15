/*
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

#include "mga_drv.h"

typedef struct drm32_mga_init {
	int func;
	u32 sarea_priv_offset;
	int chipset;
	int sgram;
	unsigned int maccess;
	unsigned int fb_cpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;
	unsigned int depth_cpp;
	unsigned int depth_offset, depth_pitch;
	unsigned int texture_offset[MGA_NR_TEX_HEAPS];
	unsigned int texture_size[MGA_NR_TEX_HEAPS];
	u32 fb_offset;
	u32 mmio_offset;
	u32 status_offset;
	u32 warp_offset;
	u32 primary_offset;
	u32 buffers_offset;
} drm_mga_init32_t;

static int compat_mga_init(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	drm_mga_init32_t init32;
	drm_mga_init_t init;

	if (copy_from_user(&init32, (void __user *)arg, sizeof(init32)))
		return -EFAULT;

	init.func = init32.func;
	init.sarea_priv_offset = init32.sarea_priv_offset;
	memcpy(&init.chipset, &init32.chipset,
		offsetof(drm_mga_init_t, fb_offset) -
		offsetof(drm_mga_init_t, chipset));
	init.fb_offset = init32.fb_offset;
	init.mmio_offset = init32.mmio_offset;
	init.status_offset = init32.status_offset;
	init.warp_offset = init32.warp_offset;
	init.primary_offset = init32.primary_offset;
	init.buffers_offset = init32.buffers_offset;

	return drm_ioctl_kernel(file, mga_dma_init, &init,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
}

typedef struct drm_mga_getparam32 {
	int param;
	u32 value;
} drm_mga_getparam32_t;

static int compat_mga_getparam(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_mga_getparam32_t getparam32;
	drm_mga_getparam_t getparam;

	if (copy_from_user(&getparam32, (void __user *)arg, sizeof(getparam32)))
		return -EFAULT;

	getparam.param = getparam32.param;
	getparam.value = compat_ptr(getparam32.value);
	return drm_ioctl_kernel(file, mga_getparam, &getparam, DRM_AUTH);
}

typedef struct drm_mga_drm_bootstrap32 {
	u32 texture_handle;
	u32 texture_size;
	u32 primary_size;
	u32 secondary_bin_count;
	u32 secondary_bin_size;
	u32 agp_mode;
	u8 agp_size;
} drm_mga_dma_bootstrap32_t;

static int compat_mga_dma_bootstrap(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	drm_mga_dma_bootstrap32_t dma_bootstrap32;
	drm_mga_dma_bootstrap_t dma_bootstrap;
	int err;

	if (copy_from_user(&dma_bootstrap32, (void __user *)arg,
			   sizeof(dma_bootstrap32)))
		return -EFAULT;

	dma_bootstrap.texture_handle = dma_bootstrap32.texture_handle;
	dma_bootstrap.texture_size = dma_bootstrap32.texture_size;
	dma_bootstrap.primary_size = dma_bootstrap32.primary_size;
	dma_bootstrap.secondary_bin_count = dma_bootstrap32.secondary_bin_count;
	dma_bootstrap.secondary_bin_size = dma_bootstrap32.secondary_bin_size;
	dma_bootstrap.agp_mode = dma_bootstrap32.agp_mode;
	dma_bootstrap.agp_size = dma_bootstrap32.agp_size;

	err = drm_ioctl_kernel(file, mga_dma_bootstrap, &dma_bootstrap,
				DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY);
	if (err)
		return err;

	dma_bootstrap32.texture_handle = dma_bootstrap.texture_handle;
	dma_bootstrap32.texture_size = dma_bootstrap.texture_size;
	dma_bootstrap32.primary_size = dma_bootstrap.primary_size;
	dma_bootstrap32.secondary_bin_count = dma_bootstrap.secondary_bin_count;
	dma_bootstrap32.secondary_bin_size = dma_bootstrap.secondary_bin_size;
	dma_bootstrap32.agp_mode = dma_bootstrap.agp_mode;
	dma_bootstrap32.agp_size = dma_bootstrap.agp_size;
	if (copy_to_user((void __user *)arg, &dma_bootstrap32,
			 sizeof(dma_bootstrap32)))
		return -EFAULT;

	return 0;
}

static struct {
	drm_ioctl_compat_t *fn;
	char *name;
} mga_compat_ioctls[] = {
#define DRM_IOCTL32_DEF(n, f)[DRM_##n] = {.fn = f, .name = #n}
	DRM_IOCTL32_DEF(MGA_INIT, compat_mga_init),
	DRM_IOCTL32_DEF(MGA_GETPARAM, compat_mga_getparam),
	DRM_IOCTL32_DEF(MGA_DMA_BOOTSTRAP, compat_mga_dma_bootstrap),
};

/**
 * mga_compat_ioctl - Called whenever a 32-bit process running under
 *                    a 64-bit kernel performs an ioctl on /dev/dri/card<n>.
 *
 * @filp: file pointer.
 * @cmd:  command.
 * @arg:  user argument.
 * return: zero on success or negative number on failure.
 */
long mga_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);
	struct drm_file *file_priv = filp->private_data;
	drm_ioctl_compat_t *fn = NULL;
	int ret;

	if (nr < DRM_COMMAND_BASE)
		return drm_compat_ioctl(filp, cmd, arg);

	if (nr >= DRM_COMMAND_BASE + ARRAY_SIZE(mga_compat_ioctls))
		return drm_ioctl(filp, cmd, arg);

	fn = mga_compat_ioctls[nr - DRM_COMMAND_BASE].fn;
	if (!fn)
		return drm_ioctl(filp, cmd, arg);

	DRM_DEBUG("pid=%d, dev=0x%lx, auth=%d, %s\n",
		  task_pid_nr(current),
		  (long)old_encode_dev(file_priv->minor->kdev->devt),
		  file_priv->authenticated,
		  mga_compat_ioctls[nr - DRM_COMMAND_BASE].name);
	ret = (*fn) (filp, cmd, arg);
	if (ret)
		DRM_DEBUG("ret = %d\n", ret);
	return ret;
}
