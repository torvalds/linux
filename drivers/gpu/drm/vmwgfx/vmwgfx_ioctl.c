// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2009-2015 VMware, Inc., Palo Alto, CA., USA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "vmwgfx_drv.h"
#include "vmwgfx_devcaps.h"
#include <drm/vmwgfx_drm.h>
#include "vmwgfx_kms.h"

int vmw_getparam_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_vmw_getparam_arg *param =
	    (struct drm_vmw_getparam_arg *)data;
	struct vmw_fpriv *vmw_fp = vmw_fpriv(file_priv);

	switch (param->param) {
	case DRM_VMW_PARAM_NUM_STREAMS:
		param->value = vmw_overlay_num_overlays(dev_priv);
		break;
	case DRM_VMW_PARAM_NUM_FREE_STREAMS:
		param->value = vmw_overlay_num_free_overlays(dev_priv);
		break;
	case DRM_VMW_PARAM_3D:
		param->value = vmw_supports_3d(dev_priv) ? 1 : 0;
		break;
	case DRM_VMW_PARAM_HW_CAPS:
		param->value = dev_priv->capabilities;
		break;
	case DRM_VMW_PARAM_HW_CAPS2:
		param->value = dev_priv->capabilities2;
		break;
	case DRM_VMW_PARAM_FIFO_CAPS:
		param->value = vmw_fifo_caps(dev_priv);
		break;
	case DRM_VMW_PARAM_MAX_FB_SIZE:
		param->value = dev_priv->max_primary_mem;
		break;
	case DRM_VMW_PARAM_FIFO_HW_VERSION:
	{
		if ((dev_priv->capabilities & SVGA_CAP_GBOBJECTS)) {
			param->value = SVGA3D_HWVERSION_WS8_B1;
			break;
		}

		param->value =
			vmw_fifo_mem_read(dev_priv,
					  ((vmw_fifo_caps(dev_priv) &
					    SVGA_FIFO_CAP_3D_HWVERSION_REVISED) ?
						   SVGA_FIFO_3D_HWVERSION_REVISED :
						   SVGA_FIFO_3D_HWVERSION));
		break;
	}
	case DRM_VMW_PARAM_MAX_SURF_MEMORY:
		if ((dev_priv->capabilities & SVGA_CAP_GBOBJECTS) &&
		    !vmw_fp->gb_aware)
			param->value = dev_priv->max_mob_pages * PAGE_SIZE / 2;
		else
			param->value = dev_priv->memory_size;
		break;
	case DRM_VMW_PARAM_3D_CAPS_SIZE:
		param->value = vmw_devcaps_size(dev_priv, vmw_fp->gb_aware);
		break;
	case DRM_VMW_PARAM_MAX_MOB_MEMORY:
		vmw_fp->gb_aware = true;
		param->value = dev_priv->max_mob_pages * PAGE_SIZE;
		break;
	case DRM_VMW_PARAM_MAX_MOB_SIZE:
		param->value = dev_priv->max_mob_size;
		break;
	case DRM_VMW_PARAM_SCREEN_TARGET:
		param->value =
			(dev_priv->active_display_unit == vmw_du_screen_target);
		break;
	case DRM_VMW_PARAM_DX:
		param->value = has_sm4_context(dev_priv);
		break;
	case DRM_VMW_PARAM_SM4_1:
		param->value = has_sm4_1_context(dev_priv);
		break;
	case DRM_VMW_PARAM_SM5:
		param->value = has_sm5_context(dev_priv);
		break;
	case DRM_VMW_PARAM_GL43:
		param->value = has_gl43_context(dev_priv);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


int vmw_get_cap_3d_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_vmw_get_3d_cap_arg *arg =
		(struct drm_vmw_get_3d_cap_arg *) data;
	struct vmw_private *dev_priv = vmw_priv(dev);
	uint32_t size;
	void __user *buffer = (void __user *)((unsigned long)(arg->buffer));
	void *bounce = NULL;
	int ret;
	struct vmw_fpriv *vmw_fp = vmw_fpriv(file_priv);

	if (unlikely(arg->pad64 != 0 || arg->max_size == 0)) {
		VMW_DEBUG_USER("Illegal GET_3D_CAP argument.\n");
		return -EINVAL;
	}

	size = vmw_devcaps_size(dev_priv, vmw_fp->gb_aware);
	if (unlikely(size == 0)) {
		DRM_ERROR("Failed to figure out the devcaps size (no 3D).\n");
		return -ENOMEM;
	}

	if (arg->max_size < size)
		size = arg->max_size;

	bounce = vzalloc(size);
	if (unlikely(bounce == NULL)) {
		DRM_ERROR("Failed to allocate bounce buffer for 3D caps.\n");
		return -ENOMEM;
	}

	ret = vmw_devcaps_copy(dev_priv, vmw_fp->gb_aware, bounce, size);
	if (unlikely (ret != 0))
		goto out_err;

	ret = copy_to_user(buffer, bounce, size);
	if (ret)
		ret = -EFAULT;
out_err:
	vfree(bounce);

	if (unlikely(ret != 0))
		DRM_ERROR("Failed to report 3D caps info.\n");

	return ret;
}

int vmw_present_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_vmw_present_arg *arg =
		(struct drm_vmw_present_arg *)data;
	struct vmw_surface *surface;
	struct drm_vmw_rect __user *clips_ptr;
	struct drm_vmw_rect *clips = NULL;
	struct drm_framebuffer *fb;
	struct vmw_framebuffer *vfb;
	struct vmw_resource *res;
	uint32_t num_clips;
	int ret;

	num_clips = arg->num_clips;
	clips_ptr = (struct drm_vmw_rect __user *)(unsigned long)arg->clips_ptr;

	if (unlikely(num_clips == 0))
		return 0;

	if (clips_ptr == NULL) {
		VMW_DEBUG_USER("Variable clips_ptr must be specified.\n");
		ret = -EINVAL;
		goto out_clips;
	}

	clips = kcalloc(num_clips, sizeof(*clips), GFP_KERNEL);
	if (clips == NULL) {
		DRM_ERROR("Failed to allocate clip rect list.\n");
		ret = -ENOMEM;
		goto out_clips;
	}

	ret = copy_from_user(clips, clips_ptr, num_clips * sizeof(*clips));
	if (ret) {
		DRM_ERROR("Failed to copy clip rects from userspace.\n");
		ret = -EFAULT;
		goto out_no_copy;
	}

	drm_modeset_lock_all(dev);

	fb = drm_framebuffer_lookup(dev, file_priv, arg->fb_id);
	if (!fb) {
		VMW_DEBUG_USER("Invalid framebuffer id.\n");
		ret = -ENOENT;
		goto out_no_fb;
	}
	vfb = vmw_framebuffer_to_vfb(fb);

	ret = vmw_user_resource_lookup_handle(dev_priv, tfile, arg->sid,
					      user_surface_converter,
					      &res);
	if (ret)
		goto out_no_surface;

	surface = vmw_res_to_srf(res);
	ret = vmw_kms_present(dev_priv, file_priv,
			      vfb, surface, arg->sid,
			      arg->dest_x, arg->dest_y,
			      clips, num_clips);

	/* vmw_user_surface_lookup takes one ref so does new_fb */
	vmw_surface_unreference(&surface);

out_no_surface:
	drm_framebuffer_put(fb);
out_no_fb:
	drm_modeset_unlock_all(dev);
out_no_copy:
	kfree(clips);
out_clips:
	return ret;
}

int vmw_present_readback_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_vmw_present_readback_arg *arg =
		(struct drm_vmw_present_readback_arg *)data;
	struct drm_vmw_fence_rep __user *user_fence_rep =
		(struct drm_vmw_fence_rep __user *)
		(unsigned long)arg->fence_rep;
	struct drm_vmw_rect __user *clips_ptr;
	struct drm_vmw_rect *clips = NULL;
	struct drm_framebuffer *fb;
	struct vmw_framebuffer *vfb;
	uint32_t num_clips;
	int ret;

	num_clips = arg->num_clips;
	clips_ptr = (struct drm_vmw_rect __user *)(unsigned long)arg->clips_ptr;

	if (unlikely(num_clips == 0))
		return 0;

	if (clips_ptr == NULL) {
		VMW_DEBUG_USER("Argument clips_ptr must be specified.\n");
		ret = -EINVAL;
		goto out_clips;
	}

	clips = kcalloc(num_clips, sizeof(*clips), GFP_KERNEL);
	if (clips == NULL) {
		DRM_ERROR("Failed to allocate clip rect list.\n");
		ret = -ENOMEM;
		goto out_clips;
	}

	ret = copy_from_user(clips, clips_ptr, num_clips * sizeof(*clips));
	if (ret) {
		DRM_ERROR("Failed to copy clip rects from userspace.\n");
		ret = -EFAULT;
		goto out_no_copy;
	}

	drm_modeset_lock_all(dev);

	fb = drm_framebuffer_lookup(dev, file_priv, arg->fb_id);
	if (!fb) {
		VMW_DEBUG_USER("Invalid framebuffer id.\n");
		ret = -ENOENT;
		goto out_no_fb;
	}

	vfb = vmw_framebuffer_to_vfb(fb);
	if (!vfb->bo) {
		VMW_DEBUG_USER("Framebuffer not buffer backed.\n");
		ret = -EINVAL;
		goto out_no_ttm_lock;
	}

	ret = vmw_kms_readback(dev_priv, file_priv,
			       vfb, user_fence_rep,
			       clips, num_clips);

out_no_ttm_lock:
	drm_framebuffer_put(fb);
out_no_fb:
	drm_modeset_unlock_all(dev);
out_no_copy:
	kfree(clips);
out_clips:
	return ret;
}
