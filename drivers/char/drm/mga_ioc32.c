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
#include <linux/ioctl32.h>

#include "drmP.h"
#include "drm.h"
#include "mga_drm.h"

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
	drm_mga_init_t __user *init;
	int err = 0, i;

	if (copy_from_user(&init32, (void __user *)arg, sizeof(init32)))
		return -EFAULT;

	init = compat_alloc_user_space(sizeof(*init));
	if (!access_ok(VERIFY_WRITE, init, sizeof(*init))
	    || __put_user(init32.func, &init->func)
	    || __put_user(init32.sarea_priv_offset, &init->sarea_priv_offset)
	    || __put_user(init32.chipset, &init->chipset)
	    || __put_user(init32.sgram, &init->sgram)
	    || __put_user(init32.maccess, &init->maccess)
	    || __put_user(init32.fb_cpp, &init->fb_cpp)
	    || __put_user(init32.front_offset, &init->front_offset)
	    || __put_user(init32.front_pitch, &init->front_pitch)
	    || __put_user(init32.back_offset, &init->back_offset)
	    || __put_user(init32.back_pitch, &init->back_pitch)
	    || __put_user(init32.depth_cpp, &init->depth_cpp)
	    || __put_user(init32.depth_offset, &init->depth_offset)
	    || __put_user(init32.depth_pitch, &init->depth_pitch)
	    || __put_user(init32.fb_offset, &init->fb_offset)
	    || __put_user(init32.mmio_offset, &init->mmio_offset)
	    || __put_user(init32.status_offset, &init->status_offset)
	    || __put_user(init32.warp_offset, &init->warp_offset)
	    || __put_user(init32.primary_offset, &init->primary_offset)
	    || __put_user(init32.buffers_offset, &init->buffers_offset))
		return -EFAULT;

	for (i = 0; i < MGA_NR_TEX_HEAPS; i++) {
		err |=
		    __put_user(init32.texture_offset[i],
			       &init->texture_offset[i]);
		err |=
		    __put_user(init32.texture_size[i], &init->texture_size[i]);
	}
	if (err)
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_MGA_INIT, (unsigned long)init);
}

typedef struct drm_mga_getparam32 {
	int param;
	u32 value;
} drm_mga_getparam32_t;

static int compat_mga_getparam(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	drm_mga_getparam32_t getparam32;
	drm_mga_getparam_t __user *getparam;

	if (copy_from_user(&getparam32, (void __user *)arg, sizeof(getparam32)))
		return -EFAULT;

	getparam = compat_alloc_user_space(sizeof(*getparam));
	if (!access_ok(VERIFY_WRITE, getparam, sizeof(*getparam))
	    || __put_user(getparam32.param, &getparam->param)
	    || __put_user((void __user *)(unsigned long)getparam32.value,
			  &getparam->value))
		return -EFAULT;

	return drm_ioctl(file->f_dentry->d_inode, file,
			 DRM_IOCTL_MGA_GETPARAM, (unsigned long)getparam);
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
	drm_mga_dma_bootstrap_t __user *dma_bootstrap;
	int err;

	if (copy_from_user(&dma_bootstrap32, (void __user *)arg,
			   sizeof(dma_bootstrap32)))
		return -EFAULT;

	dma_bootstrap = compat_alloc_user_space(sizeof(*dma_bootstrap));
	if (!access_ok(VERIFY_WRITE, dma_bootstrap, sizeof(*dma_bootstrap))
	    || __put_user(dma_bootstrap32.texture_handle,
			  &dma_bootstrap->texture_handle)
	    || __put_user(dma_bootstrap32.texture_size,
			  &dma_bootstrap->texture_size)
	    || __put_user(dma_bootstrap32.primary_size,
			  &dma_bootstrap->primary_size)
	    || __put_user(dma_bootstrap32.secondary_bin_count,
			  &dma_bootstrap->secondary_bin_count)
	    || __put_user(dma_bootstrap32.secondary_bin_size,
			  &dma_bootstrap->secondary_bin_size)
	    || __put_user(dma_bootstrap32.agp_mode, &dma_bootstrap->agp_mode)
	    || __put_user(dma_bootstrap32.agp_size, &dma_bootstrap->agp_size))
		return -EFAULT;

	err = drm_ioctl(file->f_dentry->d_inode, file,
			DRM_IOCTL_MGA_DMA_BOOTSTRAP,
			(unsigned long)dma_bootstrap);
	if (err)
		return err;

	if (__get_user(dma_bootstrap32.texture_handle,
		       &dma_bootstrap->texture_handle)
	    || __get_user(dma_bootstrap32.texture_size,
			  &dma_bootstrap->texture_size)
	    || __get_user(dma_bootstrap32.primary_size,
			  &dma_bootstrap->primary_size)
	    || __get_user(dma_bootstrap32.secondary_bin_count,
			  &dma_bootstrap->secondary_bin_count)
	    || __get_user(dma_bootstrap32.secondary_bin_size,
			  &dma_bootstrap->secondary_bin_size)
	    || __get_user(dma_bootstrap32.agp_mode, &dma_bootstrap->agp_mode)
	    || __get_user(dma_bootstrap32.agp_size, &dma_bootstrap->agp_size))
		return -EFAULT;

	if (copy_to_user((void __user *)arg, &dma_bootstrap32,
			 sizeof(dma_bootstrap32)))
		return -EFAULT;

	return 0;
}

drm_ioctl_compat_t *mga_compat_ioctls[] = {
	[DRM_MGA_INIT] = compat_mga_init,
	[DRM_MGA_GETPARAM] = compat_mga_getparam,
	[DRM_MGA_DMA_BOOTSTRAP] = compat_mga_dma_bootstrap,
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
long mga_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);
	drm_ioctl_compat_t *fn = NULL;
	int ret;

	if (nr < DRM_COMMAND_BASE)
		return drm_compat_ioctl(filp, cmd, arg);

	if (nr < DRM_COMMAND_BASE + DRM_ARRAY_SIZE(mga_compat_ioctls))
		fn = mga_compat_ioctls[nr - DRM_COMMAND_BASE];

	lock_kernel();		/* XXX for now */
	if (fn != NULL)
		ret = (*fn) (filp, cmd, arg);
	else
		ret = drm_ioctl(filp->f_dentry->d_inode, filp, cmd, arg);
	unlock_kernel();

	return ret;
}
