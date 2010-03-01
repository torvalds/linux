/**************************************************************************
 *
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
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
#include "vmwgfx_drm.h"

int vmw_getparam_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_vmw_getparam_arg *param =
	    (struct drm_vmw_getparam_arg *)data;

	switch (param->param) {
	case DRM_VMW_PARAM_NUM_STREAMS:
		param->value = vmw_overlay_num_overlays(dev_priv);
		break;
	case DRM_VMW_PARAM_NUM_FREE_STREAMS:
		param->value = vmw_overlay_num_free_overlays(dev_priv);
		break;
	case DRM_VMW_PARAM_3D:
		param->value = dev_priv->capabilities & SVGA_CAP_3D ? 1 : 0;
		break;
	case DRM_VMW_PARAM_FIFO_OFFSET:
		param->value = dev_priv->mmio_start;
		break;
	default:
		DRM_ERROR("Illegal vmwgfx get param request: %d\n",
			  param->param);
		return -EINVAL;
	}

	return 0;
}

int vmw_fifo_debug_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_fifo_state *fifo_state = &dev_priv->fifo;
	struct drm_vmw_fifo_debug_arg *arg =
	    (struct drm_vmw_fifo_debug_arg *)data;
	__le32 __user *buffer = (__le32 __user *)
	    (unsigned long)arg->debug_buffer;

	if (unlikely(fifo_state->last_buffer == NULL))
		return -EINVAL;

	if (arg->debug_buffer_size < fifo_state->last_data_size) {
		arg->used_size = arg->debug_buffer_size;
		arg->did_not_fit = 1;
	} else {
		arg->used_size = fifo_state->last_data_size;
		arg->did_not_fit = 0;
	}
	return copy_to_user(buffer, fifo_state->last_buffer, arg->used_size);
}
