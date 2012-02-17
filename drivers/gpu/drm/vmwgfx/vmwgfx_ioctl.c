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
#include "vmwgfx_kms.h"

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
		param->value = vmw_fifo_have_3d(dev_priv) ? 1 : 0;
		break;
	case DRM_VMW_PARAM_HW_CAPS:
		param->value = dev_priv->capabilities;
		break;
	case DRM_VMW_PARAM_FIFO_CAPS:
		param->value = dev_priv->fifo.capabilities;
		break;
	case DRM_VMW_PARAM_MAX_FB_SIZE:
		param->value = dev_priv->vram_size;
		break;
	case DRM_VMW_PARAM_FIFO_HW_VERSION:
	{
		__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
		const struct vmw_fifo_state *fifo = &dev_priv->fifo;

		param->value =
			ioread32(fifo_mem +
				 ((fifo->capabilities &
				   SVGA_FIFO_CAP_3D_HWVERSION_REVISED) ?
				  SVGA_FIFO_3D_HWVERSION_REVISED :
				  SVGA_FIFO_3D_HWVERSION));
		break;
	}
	default:
		DRM_ERROR("Illegal vmwgfx get param request: %d\n",
			  param->param);
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
	__le32 __iomem *fifo_mem;
	void __user *buffer = (void __user *)((unsigned long)(arg->buffer));
	void *bounce;
	int ret;

	if (unlikely(arg->pad64 != 0)) {
		DRM_ERROR("Illegal GET_3D_CAP argument.\n");
		return -EINVAL;
	}

	size = (SVGA_FIFO_3D_CAPS_LAST - SVGA_FIFO_3D_CAPS + 1) << 2;

	if (arg->max_size < size)
		size = arg->max_size;

	bounce = vmalloc(size);
	if (unlikely(bounce == NULL)) {
		DRM_ERROR("Failed to allocate bounce buffer for 3D caps.\n");
		return -ENOMEM;
	}

	fifo_mem = dev_priv->mmio_virt;
	memcpy_fromio(bounce, &fifo_mem[SVGA_FIFO_3D_CAPS], size);

	ret = copy_to_user(buffer, bounce, size);
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
	struct vmw_master *vmaster = vmw_master(file_priv->master);
	struct drm_vmw_rect __user *clips_ptr;
	struct drm_vmw_rect *clips = NULL;
	struct drm_mode_object *obj;
	struct vmw_framebuffer *vfb;
	uint32_t num_clips;
	int ret;

	num_clips = arg->num_clips;
	clips_ptr = (struct drm_vmw_rect *)(unsigned long)arg->clips_ptr;

	if (unlikely(num_clips == 0))
		return 0;

	if (clips_ptr == NULL) {
		DRM_ERROR("Variable clips_ptr must be specified.\n");
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

	ret = mutex_lock_interruptible(&dev->mode_config.mutex);
	if (unlikely(ret != 0)) {
		ret = -ERESTARTSYS;
		goto out_no_mode_mutex;
	}

	obj = drm_mode_object_find(dev, arg->fb_id, DRM_MODE_OBJECT_FB);
	if (!obj) {
		DRM_ERROR("Invalid framebuffer id.\n");
		ret = -EINVAL;
		goto out_no_fb;
	}
	vfb = vmw_framebuffer_to_vfb(obj_to_fb(obj));

	ret = ttm_read_lock(&vmaster->lock, true);
	if (unlikely(ret != 0))
		goto out_no_ttm_lock;

	ret = vmw_user_surface_lookup_handle(dev_priv, tfile, arg->sid,
					     &surface);
	if (ret)
		goto out_no_surface;

	ret = vmw_kms_present(dev_priv, file_priv,
			      vfb, surface, arg->sid,
			      arg->dest_x, arg->dest_y,
			      clips, num_clips);

	/* vmw_user_surface_lookup takes one ref so does new_fb */
	vmw_surface_unreference(&surface);

out_no_surface:
	ttm_read_unlock(&vmaster->lock);
out_no_ttm_lock:
out_no_fb:
	mutex_unlock(&dev->mode_config.mutex);
out_no_mode_mutex:
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
	struct vmw_master *vmaster = vmw_master(file_priv->master);
	struct drm_vmw_rect __user *clips_ptr;
	struct drm_vmw_rect *clips = NULL;
	struct drm_mode_object *obj;
	struct vmw_framebuffer *vfb;
	uint32_t num_clips;
	int ret;

	num_clips = arg->num_clips;
	clips_ptr = (struct drm_vmw_rect *)(unsigned long)arg->clips_ptr;

	if (unlikely(num_clips == 0))
		return 0;

	if (clips_ptr == NULL) {
		DRM_ERROR("Argument clips_ptr must be specified.\n");
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

	ret = mutex_lock_interruptible(&dev->mode_config.mutex);
	if (unlikely(ret != 0)) {
		ret = -ERESTARTSYS;
		goto out_no_mode_mutex;
	}

	obj = drm_mode_object_find(dev, arg->fb_id, DRM_MODE_OBJECT_FB);
	if (!obj) {
		DRM_ERROR("Invalid framebuffer id.\n");
		ret = -EINVAL;
		goto out_no_fb;
	}

	vfb = vmw_framebuffer_to_vfb(obj_to_fb(obj));
	if (!vfb->dmabuf) {
		DRM_ERROR("Framebuffer not dmabuf backed.\n");
		ret = -EINVAL;
		goto out_no_fb;
	}

	ret = ttm_read_lock(&vmaster->lock, true);
	if (unlikely(ret != 0))
		goto out_no_ttm_lock;

	ret = vmw_kms_readback(dev_priv, file_priv,
			       vfb, user_fence_rep,
			       clips, num_clips);

	ttm_read_unlock(&vmaster->lock);
out_no_ttm_lock:
out_no_fb:
	mutex_unlock(&dev->mode_config.mutex);
out_no_mode_mutex:
out_no_copy:
	kfree(clips);
out_clips:
	return ret;
}


/**
 * vmw_fops_poll - wrapper around the drm_poll function
 *
 * @filp: See the linux fops poll documentation.
 * @wait: See the linux fops poll documentation.
 *
 * Wrapper around the drm_poll function that makes sure the device is
 * processing the fifo if drm_poll decides to wait.
 */
unsigned int vmw_fops_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct drm_file *file_priv = filp->private_data;
	struct vmw_private *dev_priv =
		vmw_priv(file_priv->minor->dev);

	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_GENERIC);
	return drm_poll(filp, wait);
}


/**
 * vmw_fops_read - wrapper around the drm_read function
 *
 * @filp: See the linux fops read documentation.
 * @buffer: See the linux fops read documentation.
 * @count: See the linux fops read documentation.
 * offset: See the linux fops read documentation.
 *
 * Wrapper around the drm_read function that makes sure the device is
 * processing the fifo if drm_read decides to wait.
 */
ssize_t vmw_fops_read(struct file *filp, char __user *buffer,
		      size_t count, loff_t *offset)
{
	struct drm_file *file_priv = filp->private_data;
	struct vmw_private *dev_priv =
		vmw_priv(file_priv->minor->dev);

	vmw_fifo_ping_host(dev_priv, SVGA_SYNC_GENERIC);
	return drm_read(filp, buffer, count, offset);
}
