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

#include "vmwgfx_kms.h"

/* Might need a hrtimer here? */
#define VMWGFX_PRESENT_RATE ((HZ / 60 > 0) ? HZ / 60 : 1)


void vmw_display_unit_cleanup(struct vmw_display_unit *du)
{
	if (du->cursor_surface)
		vmw_surface_unreference(&du->cursor_surface);
	if (du->cursor_dmabuf)
		vmw_dmabuf_unreference(&du->cursor_dmabuf);
	drm_crtc_cleanup(&du->crtc);
	drm_encoder_cleanup(&du->encoder);
	drm_connector_cleanup(&du->connector);
}

/*
 * Display Unit Cursor functions
 */

int vmw_cursor_update_image(struct vmw_private *dev_priv,
			    u32 *image, u32 width, u32 height,
			    u32 hotspotX, u32 hotspotY)
{
	struct {
		u32 cmd;
		SVGAFifoCmdDefineAlphaCursor cursor;
	} *cmd;
	u32 image_size = width * height * 4;
	u32 cmd_size = sizeof(*cmd) + image_size;

	if (!image)
		return -EINVAL;

	cmd = vmw_fifo_reserve(dev_priv, cmd_size);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Fifo reserve failed.\n");
		return -ENOMEM;
	}

	memset(cmd, 0, sizeof(*cmd));

	memcpy(&cmd[1], image, image_size);

	cmd->cmd = cpu_to_le32(SVGA_CMD_DEFINE_ALPHA_CURSOR);
	cmd->cursor.id = cpu_to_le32(0);
	cmd->cursor.width = cpu_to_le32(width);
	cmd->cursor.height = cpu_to_le32(height);
	cmd->cursor.hotspotX = cpu_to_le32(hotspotX);
	cmd->cursor.hotspotY = cpu_to_le32(hotspotY);

	vmw_fifo_commit(dev_priv, cmd_size);

	return 0;
}

void vmw_cursor_update_position(struct vmw_private *dev_priv,
				bool show, int x, int y)
{
	__le32 __iomem *fifo_mem = dev_priv->mmio_virt;
	uint32_t count;

	iowrite32(show ? 1 : 0, fifo_mem + SVGA_FIFO_CURSOR_ON);
	iowrite32(x, fifo_mem + SVGA_FIFO_CURSOR_X);
	iowrite32(y, fifo_mem + SVGA_FIFO_CURSOR_Y);
	count = ioread32(fifo_mem + SVGA_FIFO_CURSOR_COUNT);
	iowrite32(++count, fifo_mem + SVGA_FIFO_CURSOR_COUNT);
}

int vmw_du_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
			   uint32_t handle, uint32_t width, uint32_t height)
{
	struct vmw_private *dev_priv = vmw_priv(crtc->dev);
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
	struct vmw_surface *surface = NULL;
	struct vmw_dma_buffer *dmabuf = NULL;
	int ret;

	if (handle) {
		ret = vmw_user_surface_lookup_handle(dev_priv, tfile,
						     handle, &surface);
		if (!ret) {
			if (!surface->snooper.image) {
				DRM_ERROR("surface not suitable for cursor\n");
				return -EINVAL;
			}
		} else {
			ret = vmw_user_dmabuf_lookup(tfile,
						     handle, &dmabuf);
			if (ret) {
				DRM_ERROR("failed to find surface or dmabuf: %i\n", ret);
				return -EINVAL;
			}
		}
	}

	/* takedown old cursor */
	if (du->cursor_surface) {
		du->cursor_surface->snooper.crtc = NULL;
		vmw_surface_unreference(&du->cursor_surface);
	}
	if (du->cursor_dmabuf)
		vmw_dmabuf_unreference(&du->cursor_dmabuf);

	/* setup new image */
	if (surface) {
		/* vmw_user_surface_lookup takes one reference */
		du->cursor_surface = surface;

		du->cursor_surface->snooper.crtc = crtc;
		du->cursor_age = du->cursor_surface->snooper.age;
		vmw_cursor_update_image(dev_priv, surface->snooper.image,
					64, 64, du->hotspot_x, du->hotspot_y);
	} else if (dmabuf) {
		struct ttm_bo_kmap_obj map;
		unsigned long kmap_offset;
		unsigned long kmap_num;
		void *virtual;
		bool dummy;

		/* vmw_user_surface_lookup takes one reference */
		du->cursor_dmabuf = dmabuf;

		kmap_offset = 0;
		kmap_num = (64*64*4) >> PAGE_SHIFT;

		ret = ttm_bo_reserve(&dmabuf->base, true, false, false, 0);
		if (unlikely(ret != 0)) {
			DRM_ERROR("reserve failed\n");
			return -EINVAL;
		}

		ret = ttm_bo_kmap(&dmabuf->base, kmap_offset, kmap_num, &map);
		if (unlikely(ret != 0))
			goto err_unreserve;

		virtual = ttm_kmap_obj_virtual(&map, &dummy);
		vmw_cursor_update_image(dev_priv, virtual, 64, 64,
					du->hotspot_x, du->hotspot_y);

		ttm_bo_kunmap(&map);
err_unreserve:
		ttm_bo_unreserve(&dmabuf->base);

	} else {
		vmw_cursor_update_position(dev_priv, false, 0, 0);
		return 0;
	}

	vmw_cursor_update_position(dev_priv, true, du->cursor_x, du->cursor_y);

	return 0;
}

int vmw_du_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct vmw_private *dev_priv = vmw_priv(crtc->dev);
	struct vmw_display_unit *du = vmw_crtc_to_du(crtc);
	bool shown = du->cursor_surface || du->cursor_dmabuf ? true : false;

	du->cursor_x = x + crtc->x;
	du->cursor_y = y + crtc->y;

	vmw_cursor_update_position(dev_priv, shown,
				   du->cursor_x, du->cursor_y);

	return 0;
}

void vmw_kms_cursor_snoop(struct vmw_surface *srf,
			  struct ttm_object_file *tfile,
			  struct ttm_buffer_object *bo,
			  SVGA3dCmdHeader *header)
{
	struct ttm_bo_kmap_obj map;
	unsigned long kmap_offset;
	unsigned long kmap_num;
	SVGA3dCopyBox *box;
	unsigned box_count;
	void *virtual;
	bool dummy;
	struct vmw_dma_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSurfaceDMA dma;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_dma_cmd, header);

	/* No snooper installed */
	if (!srf->snooper.image)
		return;

	if (cmd->dma.host.face != 0 || cmd->dma.host.mipmap != 0) {
		DRM_ERROR("face and mipmap for cursors should never != 0\n");
		return;
	}

	if (cmd->header.size < 64) {
		DRM_ERROR("at least one full copy box must be given\n");
		return;
	}

	box = (SVGA3dCopyBox *)&cmd[1];
	box_count = (cmd->header.size - sizeof(SVGA3dCmdSurfaceDMA)) /
			sizeof(SVGA3dCopyBox);

	if (cmd->dma.guest.pitch != (64 * 4) ||
	    cmd->dma.guest.ptr.offset % PAGE_SIZE ||
	    box->x != 0    || box->y != 0    || box->z != 0    ||
	    box->srcx != 0 || box->srcy != 0 || box->srcz != 0 ||
	    box->w != 64   || box->h != 64   || box->d != 1    ||
	    box_count != 1) {
		/* TODO handle none page aligned offsets */
		/* TODO handle partial uploads and pitch != 256 */
		/* TODO handle more then one copy (size != 64) */
		DRM_ERROR("lazy programer, cant handle wierd stuff\n");
		return;
	}

	kmap_offset = cmd->dma.guest.ptr.offset >> PAGE_SHIFT;
	kmap_num = (64*64*4) >> PAGE_SHIFT;

	ret = ttm_bo_reserve(bo, true, false, false, 0);
	if (unlikely(ret != 0)) {
		DRM_ERROR("reserve failed\n");
		return;
	}

	ret = ttm_bo_kmap(bo, kmap_offset, kmap_num, &map);
	if (unlikely(ret != 0))
		goto err_unreserve;

	virtual = ttm_kmap_obj_virtual(&map, &dummy);

	memcpy(srf->snooper.image, virtual, 64*64*4);
	srf->snooper.age++;

	/* we can't call this function from this function since execbuf has
	 * reserved fifo space.
	 *
	 * if (srf->snooper.crtc)
	 *	vmw_ldu_crtc_cursor_update_image(dev_priv,
	 *					 srf->snooper.image, 64, 64,
	 *					 du->hotspot_x, du->hotspot_y);
	 */

	ttm_bo_kunmap(&map);
err_unreserve:
	ttm_bo_unreserve(bo);
}

void vmw_kms_cursor_post_execbuf(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct vmw_display_unit *du;
	struct drm_crtc *crtc;

	mutex_lock(&dev->mode_config.mutex);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		du = vmw_crtc_to_du(crtc);
		if (!du->cursor_surface ||
		    du->cursor_age == du->cursor_surface->snooper.age)
			continue;

		du->cursor_age = du->cursor_surface->snooper.age;
		vmw_cursor_update_image(dev_priv,
					du->cursor_surface->snooper.image,
					64, 64, du->hotspot_x, du->hotspot_y);
	}

	mutex_unlock(&dev->mode_config.mutex);
}

/*
 * Generic framebuffer code
 */

int vmw_framebuffer_create_handle(struct drm_framebuffer *fb,
				  struct drm_file *file_priv,
				  unsigned int *handle)
{
	if (handle)
		handle = 0;

	return 0;
}

/*
 * Surface framebuffer code
 */

#define vmw_framebuffer_to_vfbs(x) \
	container_of(x, struct vmw_framebuffer_surface, base.base)

struct vmw_framebuffer_surface {
	struct vmw_framebuffer base;
	struct vmw_surface *surface;
	struct delayed_work d_work;
	struct mutex work_lock;
	bool present_fs;
};

void vmw_framebuffer_surface_destroy(struct drm_framebuffer *framebuffer)
{
	struct vmw_framebuffer_surface *vfb =
		vmw_framebuffer_to_vfbs(framebuffer);

	cancel_delayed_work_sync(&vfb->d_work);
	drm_framebuffer_cleanup(framebuffer);
	vmw_surface_unreference(&vfb->surface);

	kfree(framebuffer);
}

static void vmw_framebuffer_present_fs_callback(struct work_struct *work)
{
	struct delayed_work *d_work =
		container_of(work, struct delayed_work, work);
	struct vmw_framebuffer_surface *vfbs =
		container_of(d_work, struct vmw_framebuffer_surface, d_work);
	struct vmw_surface *surf = vfbs->surface;
	struct drm_framebuffer *framebuffer = &vfbs->base.base;
	struct vmw_private *dev_priv = vmw_priv(framebuffer->dev);

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdPresent body;
		SVGA3dCopyRect cr;
	} *cmd;

	mutex_lock(&vfbs->work_lock);
	if (!vfbs->present_fs)
		goto out_unlock;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		goto out_resched;

	cmd->header.id = cpu_to_le32(SVGA_3D_CMD_PRESENT);
	cmd->header.size = cpu_to_le32(sizeof(cmd->body) + sizeof(cmd->cr));
	cmd->body.sid = cpu_to_le32(surf->res.id);
	cmd->cr.x = cpu_to_le32(0);
	cmd->cr.y = cpu_to_le32(0);
	cmd->cr.srcx = cmd->cr.x;
	cmd->cr.srcy = cmd->cr.y;
	cmd->cr.w = cpu_to_le32(framebuffer->width);
	cmd->cr.h = cpu_to_le32(framebuffer->height);
	vfbs->present_fs = false;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));
out_resched:
	/**
	 * Will not re-add if already pending.
	 */
	schedule_delayed_work(&vfbs->d_work, VMWGFX_PRESENT_RATE);
out_unlock:
	mutex_unlock(&vfbs->work_lock);
}


int vmw_framebuffer_surface_dirty(struct drm_framebuffer *framebuffer,
				  unsigned flags, unsigned color,
				  struct drm_clip_rect *clips,
				  unsigned num_clips)
{
	struct vmw_private *dev_priv = vmw_priv(framebuffer->dev);
	struct vmw_framebuffer_surface *vfbs =
		vmw_framebuffer_to_vfbs(framebuffer);
	struct vmw_surface *surf = vfbs->surface;
	struct drm_clip_rect norect;
	SVGA3dCopyRect *cr;
	int i, inc = 1;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdPresent body;
		SVGA3dCopyRect cr;
	} *cmd;

	if (!num_clips ||
	    !(dev_priv->fifo.capabilities &
	      SVGA_FIFO_CAP_SCREEN_OBJECT)) {
		int ret;

		mutex_lock(&vfbs->work_lock);
		vfbs->present_fs = true;
		ret = schedule_delayed_work(&vfbs->d_work, VMWGFX_PRESENT_RATE);
		mutex_unlock(&vfbs->work_lock);
		if (ret) {
			/**
			 * No work pending, Force immediate present.
			 */
			vmw_framebuffer_present_fs_callback(&vfbs->d_work.work);
		}
		return 0;
	}

	if (!num_clips) {
		num_clips = 1;
		clips = &norect;
		norect.x1 = norect.y1 = 0;
		norect.x2 = framebuffer->width;
		norect.y2 = framebuffer->height;
	} else if (flags & DRM_MODE_FB_DIRTY_ANNOTATE_COPY) {
		num_clips /= 2;
		inc = 2; /* skip source rects */
	}

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd) + (num_clips - 1) * sizeof(cmd->cr));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Fifo reserve failed.\n");
		return -ENOMEM;
	}

	memset(cmd, 0, sizeof(*cmd));

	cmd->header.id = cpu_to_le32(SVGA_3D_CMD_PRESENT);
	cmd->header.size = cpu_to_le32(sizeof(cmd->body) + num_clips * sizeof(cmd->cr));
	cmd->body.sid = cpu_to_le32(surf->res.id);

	for (i = 0, cr = &cmd->cr; i < num_clips; i++, cr++, clips += inc) {
		cr->x = cpu_to_le16(clips->x1);
		cr->y = cpu_to_le16(clips->y1);
		cr->srcx = cr->x;
		cr->srcy = cr->y;
		cr->w = cpu_to_le16(clips->x2 - clips->x1);
		cr->h = cpu_to_le16(clips->y2 - clips->y1);
	}

	vmw_fifo_commit(dev_priv, sizeof(*cmd) + (num_clips - 1) * sizeof(cmd->cr));

	return 0;
}

static struct drm_framebuffer_funcs vmw_framebuffer_surface_funcs = {
	.destroy = vmw_framebuffer_surface_destroy,
	.dirty = vmw_framebuffer_surface_dirty,
	.create_handle = vmw_framebuffer_create_handle,
};

int vmw_kms_new_framebuffer_surface(struct vmw_private *dev_priv,
				    struct vmw_surface *surface,
				    struct vmw_framebuffer **out,
				    unsigned width, unsigned height)

{
	struct drm_device *dev = dev_priv->dev;
	struct vmw_framebuffer_surface *vfbs;
	int ret;

	vfbs = kzalloc(sizeof(*vfbs), GFP_KERNEL);
	if (!vfbs) {
		ret = -ENOMEM;
		goto out_err1;
	}

	ret = drm_framebuffer_init(dev, &vfbs->base.base,
				   &vmw_framebuffer_surface_funcs);
	if (ret)
		goto out_err2;

	if (!vmw_surface_reference(surface)) {
		DRM_ERROR("failed to reference surface %p\n", surface);
		goto out_err3;
	}

	/* XXX get the first 3 from the surface info */
	vfbs->base.base.bits_per_pixel = 32;
	vfbs->base.base.pitch = width * 32 / 4;
	vfbs->base.base.depth = 24;
	vfbs->base.base.width = width;
	vfbs->base.base.height = height;
	vfbs->base.pin = NULL;
	vfbs->base.unpin = NULL;
	vfbs->surface = surface;
	mutex_init(&vfbs->work_lock);
	INIT_DELAYED_WORK(&vfbs->d_work, &vmw_framebuffer_present_fs_callback);
	*out = &vfbs->base;

	return 0;

out_err3:
	drm_framebuffer_cleanup(&vfbs->base.base);
out_err2:
	kfree(vfbs);
out_err1:
	return ret;
}

/*
 * Dmabuf framebuffer code
 */

#define vmw_framebuffer_to_vfbd(x) \
	container_of(x, struct vmw_framebuffer_dmabuf, base.base)

struct vmw_framebuffer_dmabuf {
	struct vmw_framebuffer base;
	struct vmw_dma_buffer *buffer;
};

void vmw_framebuffer_dmabuf_destroy(struct drm_framebuffer *framebuffer)
{
	struct vmw_framebuffer_dmabuf *vfbd =
		vmw_framebuffer_to_vfbd(framebuffer);

	drm_framebuffer_cleanup(framebuffer);
	vmw_dmabuf_unreference(&vfbd->buffer);

	kfree(vfbd);
}

int vmw_framebuffer_dmabuf_dirty(struct drm_framebuffer *framebuffer,
				 unsigned flags, unsigned color,
				 struct drm_clip_rect *clips,
				 unsigned num_clips)
{
	struct vmw_private *dev_priv = vmw_priv(framebuffer->dev);
	struct drm_clip_rect norect;
	struct {
		uint32_t header;
		SVGAFifoCmdUpdate body;
	} *cmd;
	int i, increment = 1;

	if (!num_clips ||
	    !(dev_priv->fifo.capabilities &
	      SVGA_FIFO_CAP_SCREEN_OBJECT)) {
		num_clips = 1;
		clips = &norect;
		norect.x1 = norect.y1 = 0;
		norect.x2 = framebuffer->width;
		norect.y2 = framebuffer->height;
	} else if (flags & DRM_MODE_FB_DIRTY_ANNOTATE_COPY) {
		num_clips /= 2;
		increment = 2;
	}

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd) * num_clips);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Fifo reserve failed.\n");
		return -ENOMEM;
	}

	for (i = 0; i < num_clips; i++, clips += increment) {
		cmd[i].header = cpu_to_le32(SVGA_CMD_UPDATE);
		cmd[i].body.x = cpu_to_le32(clips[i].x1);
		cmd[i].body.y = cpu_to_le32(clips[i].y1);
		cmd[i].body.width = cpu_to_le32(clips[i].x2 - clips[i].x1);
		cmd[i].body.height = cpu_to_le32(clips[i].y2 - clips[i].y1);
	}

	vmw_fifo_commit(dev_priv, sizeof(*cmd) * num_clips);

	return 0;
}

static struct drm_framebuffer_funcs vmw_framebuffer_dmabuf_funcs = {
	.destroy = vmw_framebuffer_dmabuf_destroy,
	.dirty = vmw_framebuffer_dmabuf_dirty,
	.create_handle = vmw_framebuffer_create_handle,
};

static int vmw_framebuffer_dmabuf_pin(struct vmw_framebuffer *vfb)
{
	struct vmw_private *dev_priv = vmw_priv(vfb->base.dev);
	struct vmw_framebuffer_dmabuf *vfbd =
		vmw_framebuffer_to_vfbd(&vfb->base);
	int ret;

	vmw_overlay_pause_all(dev_priv);

	ret = vmw_dmabuf_to_start_of_vram(dev_priv, vfbd->buffer);

	if (dev_priv->capabilities & SVGA_CAP_MULTIMON) {
		vmw_write(dev_priv, SVGA_REG_NUM_GUEST_DISPLAYS, 1);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_ID, 0);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_IS_PRIMARY, true);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_POSITION_X, 0);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_POSITION_Y, 0);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_WIDTH, 0);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_HEIGHT, 0);
		vmw_write(dev_priv, SVGA_REG_DISPLAY_ID, SVGA_ID_INVALID);

		vmw_write(dev_priv, SVGA_REG_ENABLE, 1);
		vmw_write(dev_priv, SVGA_REG_WIDTH, vfb->base.width);
		vmw_write(dev_priv, SVGA_REG_HEIGHT, vfb->base.height);
		vmw_write(dev_priv, SVGA_REG_BITS_PER_PIXEL, vfb->base.bits_per_pixel);
		vmw_write(dev_priv, SVGA_REG_DEPTH, vfb->base.depth);
		vmw_write(dev_priv, SVGA_REG_RED_MASK, 0x00ff0000);
		vmw_write(dev_priv, SVGA_REG_GREEN_MASK, 0x0000ff00);
		vmw_write(dev_priv, SVGA_REG_BLUE_MASK, 0x000000ff);
	} else
		WARN_ON(true);

	vmw_overlay_resume_all(dev_priv);

	return 0;
}

static int vmw_framebuffer_dmabuf_unpin(struct vmw_framebuffer *vfb)
{
	struct vmw_private *dev_priv = vmw_priv(vfb->base.dev);
	struct vmw_framebuffer_dmabuf *vfbd =
		vmw_framebuffer_to_vfbd(&vfb->base);

	if (!vfbd->buffer) {
		WARN_ON(!vfbd->buffer);
		return 0;
	}

	return vmw_dmabuf_from_vram(dev_priv, vfbd->buffer);
}

int vmw_kms_new_framebuffer_dmabuf(struct vmw_private *dev_priv,
				   struct vmw_dma_buffer *dmabuf,
				   struct vmw_framebuffer **out,
				   unsigned width, unsigned height)

{
	struct drm_device *dev = dev_priv->dev;
	struct vmw_framebuffer_dmabuf *vfbd;
	int ret;

	vfbd = kzalloc(sizeof(*vfbd), GFP_KERNEL);
	if (!vfbd) {
		ret = -ENOMEM;
		goto out_err1;
	}

	ret = drm_framebuffer_init(dev, &vfbd->base.base,
				   &vmw_framebuffer_dmabuf_funcs);
	if (ret)
		goto out_err2;

	if (!vmw_dmabuf_reference(dmabuf)) {
		DRM_ERROR("failed to reference dmabuf %p\n", dmabuf);
		goto out_err3;
	}

	/* XXX get the first 3 from the surface info */
	vfbd->base.base.bits_per_pixel = 32;
	vfbd->base.base.pitch = width * 32 / 4;
	vfbd->base.base.depth = 24;
	vfbd->base.base.width = width;
	vfbd->base.base.height = height;
	vfbd->base.pin = vmw_framebuffer_dmabuf_pin;
	vfbd->base.unpin = vmw_framebuffer_dmabuf_unpin;
	vfbd->buffer = dmabuf;
	*out = &vfbd->base;

	return 0;

out_err3:
	drm_framebuffer_cleanup(&vfbd->base.base);
out_err2:
	kfree(vfbd);
out_err1:
	return ret;
}

/*
 * Generic Kernel modesetting functions
 */

static struct drm_framebuffer *vmw_kms_fb_create(struct drm_device *dev,
						 struct drm_file *file_priv,
						 struct drm_mode_fb_cmd *mode_cmd)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_framebuffer *vfb = NULL;
	struct vmw_surface *surface = NULL;
	struct vmw_dma_buffer *bo = NULL;
	int ret;

	ret = vmw_user_surface_lookup_handle(dev_priv, tfile,
					     mode_cmd->handle, &surface);
	if (ret)
		goto try_dmabuf;

	ret = vmw_kms_new_framebuffer_surface(dev_priv, surface, &vfb,
					      mode_cmd->width, mode_cmd->height);

	/* vmw_user_surface_lookup takes one ref so does new_fb */
	vmw_surface_unreference(&surface);

	if (ret) {
		DRM_ERROR("failed to create vmw_framebuffer: %i\n", ret);
		return NULL;
	}
	return &vfb->base;

try_dmabuf:
	DRM_INFO("%s: trying buffer\n", __func__);

	ret = vmw_user_dmabuf_lookup(tfile, mode_cmd->handle, &bo);
	if (ret) {
		DRM_ERROR("failed to find buffer: %i\n", ret);
		return NULL;
	}

	ret = vmw_kms_new_framebuffer_dmabuf(dev_priv, bo, &vfb,
					     mode_cmd->width, mode_cmd->height);

	/* vmw_user_dmabuf_lookup takes one ref so does new_fb */
	vmw_dmabuf_unreference(&bo);

	if (ret) {
		DRM_ERROR("failed to create vmw_framebuffer: %i\n", ret);
		return NULL;
	}

	return &vfb->base;
}

static int vmw_kms_fb_changed(struct drm_device *dev)
{
	return 0;
}

static struct drm_mode_config_funcs vmw_kms_funcs = {
	.fb_create = vmw_kms_fb_create,
	.fb_changed = vmw_kms_fb_changed,
};

int vmw_kms_init(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	int ret;

	drm_mode_config_init(dev);
	dev->mode_config.funcs = &vmw_kms_funcs;
	dev->mode_config.min_width = 640;
	dev->mode_config.min_height = 480;
	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;

	ret = vmw_kms_init_legacy_display_system(dev_priv);

	return 0;
}

int vmw_kms_close(struct vmw_private *dev_priv)
{
	/*
	 * Docs says we should take the lock before calling this function
	 * but since it destroys encoders and our destructor calls
	 * drm_encoder_cleanup which takes the lock we deadlock.
	 */
	drm_mode_config_cleanup(dev_priv->dev);
	vmw_kms_close_legacy_display_system(dev_priv);
	return 0;
}

int vmw_kms_cursor_bypass_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_vmw_cursor_bypass_arg *arg = data;
	struct vmw_display_unit *du;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	int ret = 0;


	mutex_lock(&dev->mode_config.mutex);
	if (arg->flags & DRM_VMW_CURSOR_BYPASS_ALL) {

		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			du = vmw_crtc_to_du(crtc);
			du->hotspot_x = arg->xhot;
			du->hotspot_y = arg->yhot;
		}

		mutex_unlock(&dev->mode_config.mutex);
		return 0;
	}

	obj = drm_mode_object_find(dev, arg->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}

	crtc = obj_to_crtc(obj);
	du = vmw_crtc_to_du(crtc);

	du->hotspot_x = arg->xhot;
	du->hotspot_y = arg->yhot;

out:
	mutex_unlock(&dev->mode_config.mutex);

	return ret;
}

int vmw_kms_save_vga(struct vmw_private *vmw_priv)
{
	/*
	 * setup a single multimon monitor with the size
	 * of 0x0, this stops the UI from resizing when we
	 * change the framebuffer size
	 */
	if (vmw_priv->capabilities & SVGA_CAP_MULTIMON) {
		vmw_write(vmw_priv, SVGA_REG_NUM_GUEST_DISPLAYS, 1);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_ID, 0);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_IS_PRIMARY, true);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_POSITION_X, 0);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_POSITION_Y, 0);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_WIDTH, 0);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_HEIGHT, 0);
		vmw_write(vmw_priv, SVGA_REG_DISPLAY_ID, SVGA_ID_INVALID);
	}

	vmw_priv->vga_width = vmw_read(vmw_priv, SVGA_REG_WIDTH);
	vmw_priv->vga_height = vmw_read(vmw_priv, SVGA_REG_HEIGHT);
	vmw_priv->vga_bpp = vmw_read(vmw_priv, SVGA_REG_BITS_PER_PIXEL);
	vmw_priv->vga_depth = vmw_read(vmw_priv, SVGA_REG_DEPTH);
	vmw_priv->vga_pseudo = vmw_read(vmw_priv, SVGA_REG_PSEUDOCOLOR);
	vmw_priv->vga_red_mask = vmw_read(vmw_priv, SVGA_REG_RED_MASK);
	vmw_priv->vga_green_mask = vmw_read(vmw_priv, SVGA_REG_GREEN_MASK);
	vmw_priv->vga_blue_mask = vmw_read(vmw_priv, SVGA_REG_BLUE_MASK);

	return 0;
}

int vmw_kms_restore_vga(struct vmw_private *vmw_priv)
{
	vmw_write(vmw_priv, SVGA_REG_WIDTH, vmw_priv->vga_width);
	vmw_write(vmw_priv, SVGA_REG_HEIGHT, vmw_priv->vga_height);
	vmw_write(vmw_priv, SVGA_REG_BITS_PER_PIXEL, vmw_priv->vga_bpp);
	vmw_write(vmw_priv, SVGA_REG_DEPTH, vmw_priv->vga_depth);
	vmw_write(vmw_priv, SVGA_REG_PSEUDOCOLOR, vmw_priv->vga_pseudo);
	vmw_write(vmw_priv, SVGA_REG_RED_MASK, vmw_priv->vga_red_mask);
	vmw_write(vmw_priv, SVGA_REG_GREEN_MASK, vmw_priv->vga_green_mask);
	vmw_write(vmw_priv, SVGA_REG_BLUE_MASK, vmw_priv->vga_blue_mask);

	/* TODO check for multimon */
	vmw_write(vmw_priv, SVGA_REG_NUM_GUEST_DISPLAYS, 0);

	return 0;
}
