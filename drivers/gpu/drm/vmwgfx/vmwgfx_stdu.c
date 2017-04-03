/******************************************************************************
 *
 * COPYRIGHT © 2014-2015 VMware, Inc., Palo Alto, CA., USA
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
 ******************************************************************************/

#include "vmwgfx_kms.h"
#include "device_include/svga3d_surfacedefs.h"
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>


#define vmw_crtc_to_stdu(x) \
	container_of(x, struct vmw_screen_target_display_unit, base.crtc)
#define vmw_encoder_to_stdu(x) \
	container_of(x, struct vmw_screen_target_display_unit, base.encoder)
#define vmw_connector_to_stdu(x) \
	container_of(x, struct vmw_screen_target_display_unit, base.connector)



enum stdu_content_type {
	SAME_AS_DISPLAY = 0,
	SEPARATE_SURFACE,
	SEPARATE_DMA
};

/**
 * struct vmw_stdu_dirty - closure structure for the update functions
 *
 * @base: The base type we derive from. Used by vmw_kms_helper_dirty().
 * @transfer: Transfer direction for DMA command.
 * @left: Left side of bounding box.
 * @right: Right side of bounding box.
 * @top: Top side of bounding box.
 * @bottom: Bottom side of bounding box.
 * @buf: DMA buffer when DMA-ing between buffer and screen targets.
 * @sid: Surface ID when copying between surface and screen targets.
 */
struct vmw_stdu_dirty {
	struct vmw_kms_dirty base;
	SVGA3dTransferType  transfer;
	s32 left, right, top, bottom;
	u32 pitch;
	union {
		struct vmw_dma_buffer *buf;
		u32 sid;
	};
};

/*
 * SVGA commands that are used by this code. Please see the device headers
 * for explanation.
 */
struct vmw_stdu_update {
	SVGA3dCmdHeader header;
	SVGA3dCmdUpdateGBScreenTarget body;
};

struct vmw_stdu_dma {
	SVGA3dCmdHeader     header;
	SVGA3dCmdSurfaceDMA body;
};

struct vmw_stdu_surface_copy {
	SVGA3dCmdHeader      header;
	SVGA3dCmdSurfaceCopy body;
};


/**
 * struct vmw_screen_target_display_unit
 *
 * @base: VMW specific DU structure
 * @display_srf: surface to be displayed.  The dimension of this will always
 *               match the display mode.  If the display mode matches
 *               content_vfbs dimensions, then this is a pointer into the
 *               corresponding field in content_vfbs.  If not, then this
 *               is a separate buffer to which content_vfbs will blit to.
 * @content_type:  content_fb type
 * @defined:  true if the current display unit has been initialized
 */
struct vmw_screen_target_display_unit {
	struct vmw_display_unit base;
	const struct vmw_surface *display_srf;
	enum stdu_content_type content_fb_type;
	s32 display_width, display_height;

	bool defined;

	/* For CPU Blit */
	struct ttm_bo_kmap_obj host_map, guest_map;
	unsigned int cpp;
};



static void vmw_stdu_destroy(struct vmw_screen_target_display_unit *stdu);



/******************************************************************************
 * Screen Target Display Unit CRTC Functions
 *****************************************************************************/


/**
 * vmw_stdu_crtc_destroy - cleans up the STDU
 *
 * @crtc: used to get a reference to the containing STDU
 */
static void vmw_stdu_crtc_destroy(struct drm_crtc *crtc)
{
	vmw_stdu_destroy(vmw_crtc_to_stdu(crtc));
}

/**
 * vmw_stdu_define_st - Defines a Screen Target
 *
 * @dev_priv:  VMW DRM device
 * @stdu: display unit to create a Screen Target for
 * @mode: The mode to set.
 * @crtc_x: X coordinate of screen target relative to framebuffer origin.
 * @crtc_y: Y coordinate of screen target relative to framebuffer origin.
 *
 * Creates a STDU that we can used later.  This function is called whenever the
 * framebuffer size changes.
 *
 * RETURNs:
 * 0 on success, error code on failure
 */
static int vmw_stdu_define_st(struct vmw_private *dev_priv,
			      struct vmw_screen_target_display_unit *stdu,
			      struct drm_display_mode *mode,
			      int crtc_x, int crtc_y)
{
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineGBScreenTarget body;
	} *cmd;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Out of FIFO space defining Screen Target\n");
		return -ENOMEM;
	}

	cmd->header.id   = SVGA_3D_CMD_DEFINE_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;
	cmd->body.width  = mode->hdisplay;
	cmd->body.height = mode->vdisplay;
	cmd->body.flags  = (0 == cmd->body.stid) ? SVGA_STFLAG_PRIMARY : 0;
	cmd->body.dpi    = 0;
	if (stdu->base.is_implicit) {
		cmd->body.xRoot  = crtc_x;
		cmd->body.yRoot  = crtc_y;
	} else {
		cmd->body.xRoot  = stdu->base.gui_x;
		cmd->body.yRoot  = stdu->base.gui_y;
	}
	stdu->base.set_gui_x = cmd->body.xRoot;
	stdu->base.set_gui_y = cmd->body.yRoot;

	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	stdu->defined = true;
	stdu->display_width  = mode->hdisplay;
	stdu->display_height = mode->vdisplay;

	return 0;
}



/**
 * vmw_stdu_bind_st - Binds a surface to a Screen Target
 *
 * @dev_priv: VMW DRM device
 * @stdu: display unit affected
 * @res: Buffer to bind to the screen target.  Set to NULL to blank screen.
 *
 * Binding a surface to a Screen Target the same as flipping
 */
static int vmw_stdu_bind_st(struct vmw_private *dev_priv,
			    struct vmw_screen_target_display_unit *stdu,
			    const struct vmw_resource *res)
{
	SVGA3dSurfaceImageId image;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdBindGBScreenTarget body;
	} *cmd;


	if (!stdu->defined) {
		DRM_ERROR("No screen target defined\n");
		return -EINVAL;
	}

	/* Set up image using information in vfb */
	memset(&image, 0, sizeof(image));
	image.sid = res ? res->id : SVGA3D_INVALID_ID;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Out of FIFO space binding a screen target\n");
		return -ENOMEM;
	}

	cmd->header.id   = SVGA_3D_CMD_BIND_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;
	cmd->body.image  = image;

	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_stdu_populate_update - populate an UPDATE_GB_SCREENTARGET command with a
 * bounding box.
 *
 * @cmd: Pointer to command stream.
 * @unit: Screen target unit.
 * @left: Left side of bounding box.
 * @right: Right side of bounding box.
 * @top: Top side of bounding box.
 * @bottom: Bottom side of bounding box.
 */
static void vmw_stdu_populate_update(void *cmd, int unit,
				     s32 left, s32 right, s32 top, s32 bottom)
{
	struct vmw_stdu_update *update = cmd;

	update->header.id   = SVGA_3D_CMD_UPDATE_GB_SCREENTARGET;
	update->header.size = sizeof(update->body);

	update->body.stid   = unit;
	update->body.rect.x = left;
	update->body.rect.y = top;
	update->body.rect.w = right - left;
	update->body.rect.h = bottom - top;
}

/**
 * vmw_stdu_update_st - Full update of a Screen Target
 *
 * @dev_priv: VMW DRM device
 * @stdu: display unit affected
 *
 * This function needs to be called whenever the content of a screen
 * target has changed completely. Typically as a result of a backing
 * surface change.
 *
 * RETURNS:
 * 0 on success, error code on failure
 */
static int vmw_stdu_update_st(struct vmw_private *dev_priv,
			      struct vmw_screen_target_display_unit *stdu)
{
	struct vmw_stdu_update *cmd;

	if (!stdu->defined) {
		DRM_ERROR("No screen target defined");
		return -EINVAL;
	}

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Out of FIFO space updating a Screen Target\n");
		return -ENOMEM;
	}

	vmw_stdu_populate_update(cmd, stdu->base.unit,
				 0, stdu->display_width,
				 0, stdu->display_height);

	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;
}



/**
 * vmw_stdu_destroy_st - Destroy a Screen Target
 *
 * @dev_priv:  VMW DRM device
 * @stdu: display unit to destroy
 */
static int vmw_stdu_destroy_st(struct vmw_private *dev_priv,
			       struct vmw_screen_target_display_unit *stdu)
{
	int    ret;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroyGBScreenTarget body;
	} *cmd;


	/* Nothing to do if not successfully defined */
	if (unlikely(!stdu->defined))
		return 0;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Out of FIFO space, screen target not destroyed\n");
		return -ENOMEM;
	}

	cmd->header.id   = SVGA_3D_CMD_DESTROY_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;

	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	/* Force sync */
	ret = vmw_fallback_wait(dev_priv, false, true, 0, false, 3*HZ);
	if (unlikely(ret != 0))
		DRM_ERROR("Failed to sync with HW");

	stdu->defined = false;
	stdu->display_width  = 0;
	stdu->display_height = 0;

	return ret;
}


/**
 * vmw_stdu_crtc_mode_set_nofb - Updates screen target size
 *
 * @crtc: CRTC associated with the screen target
 *
 * This function defines/destroys a screen target
 *
 */
static void vmw_stdu_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct vmw_private *dev_priv;
	struct vmw_screen_target_display_unit *stdu;
	int ret;


	stdu     = vmw_crtc_to_stdu(crtc);
	dev_priv = vmw_priv(crtc->dev);

	if (stdu->defined) {
		ret = vmw_stdu_bind_st(dev_priv, stdu, NULL);
		if (ret)
			DRM_ERROR("Failed to blank CRTC\n");

		(void) vmw_stdu_update_st(dev_priv, stdu);

		ret = vmw_stdu_destroy_st(dev_priv, stdu);
		if (ret)
			DRM_ERROR("Failed to destroy Screen Target\n");

		stdu->content_fb_type = SAME_AS_DISPLAY;
	}

	if (!crtc->state->enable)
		return;

	vmw_svga_enable(dev_priv);
	ret = vmw_stdu_define_st(dev_priv, stdu, &crtc->mode, crtc->x, crtc->y);

	if (ret)
		DRM_ERROR("Failed to define Screen Target of size %dx%d\n",
			  crtc->x, crtc->y);
}


static void vmw_stdu_crtc_helper_prepare(struct drm_crtc *crtc)
{
}


static void vmw_stdu_crtc_helper_commit(struct drm_crtc *crtc)
{
	struct vmw_private *dev_priv;
	struct vmw_screen_target_display_unit *stdu;
	struct vmw_framebuffer *vfb;
	struct drm_framebuffer *fb;


	stdu     = vmw_crtc_to_stdu(crtc);
	dev_priv = vmw_priv(crtc->dev);
	fb       = crtc->primary->fb;

	vfb = (fb) ? vmw_framebuffer_to_vfb(fb) : NULL;

	if (vfb)
		vmw_kms_add_active(dev_priv, &stdu->base, vfb);
	else
		vmw_kms_del_active(dev_priv, &stdu->base);
}

static void vmw_stdu_crtc_helper_disable(struct drm_crtc *crtc)
{
	struct vmw_private *dev_priv;
	struct vmw_screen_target_display_unit *stdu;
	int ret;


	if (!crtc) {
		DRM_ERROR("CRTC is NULL\n");
		return;
	}

	stdu     = vmw_crtc_to_stdu(crtc);
	dev_priv = vmw_priv(crtc->dev);

	if (stdu->defined) {
		ret = vmw_stdu_bind_st(dev_priv, stdu, NULL);
		if (ret)
			DRM_ERROR("Failed to blank CRTC\n");

		(void) vmw_stdu_update_st(dev_priv, stdu);

		ret = vmw_stdu_destroy_st(dev_priv, stdu);
		if (ret)
			DRM_ERROR("Failed to destroy Screen Target\n");

		stdu->content_fb_type = SAME_AS_DISPLAY;
	}
}

/**
 * vmw_stdu_crtc_page_flip - Binds a buffer to a screen target
 *
 * @crtc: CRTC to attach FB to
 * @fb: FB to attach
 * @event: Event to be posted. This event should've been alloced
 *         using k[mz]alloc, and should've been completely initialized.
 * @page_flip_flags: Input flags.
 *
 * If the STDU uses the same display and content buffers, i.e. a true flip,
 * this function will replace the existing display buffer with the new content
 * buffer.
 *
 * If the STDU uses different display and content buffers, i.e. a blit, then
 * only the content buffer will be updated.
 *
 * RETURNS:
 * 0 on success, error code on failure
 */
static int vmw_stdu_crtc_page_flip(struct drm_crtc *crtc,
				   struct drm_framebuffer *new_fb,
				   struct drm_pending_vblank_event *event,
				   uint32_t flags,
				   struct drm_modeset_acquire_ctx *ctx)

{
	struct vmw_private *dev_priv = vmw_priv(crtc->dev);
	struct vmw_screen_target_display_unit *stdu = vmw_crtc_to_stdu(crtc);
	struct vmw_framebuffer *vfb = vmw_framebuffer_to_vfb(new_fb);
	struct drm_vmw_rect vclips;
	int ret;

	dev_priv          = vmw_priv(crtc->dev);
	stdu              = vmw_crtc_to_stdu(crtc);

	if (!stdu->defined || !vmw_kms_crtc_flippable(dev_priv, crtc))
		return -EINVAL;

	/*
	 * We're always async, but the helper doesn't know how to set async
	 * so lie to the helper. Also, the helper expects someone
	 * to pick the event up from the crtc state, and if nobody does,
	 * it will free it. Since we handle the event in this function,
	 * don't hand it to the helper.
	 */
	flags &= ~DRM_MODE_PAGE_FLIP_ASYNC;
	ret = drm_atomic_helper_page_flip(crtc, new_fb, NULL, flags, ctx);
	if (ret) {
		DRM_ERROR("Page flip error %d.\n", ret);
		return ret;
	}

	if (stdu->base.is_implicit)
		vmw_kms_update_implicit_fb(dev_priv, crtc);

	/*
	 * Now that we've bound a new surface to the screen target,
	 * update the contents.
	 */
	vclips.x = crtc->x;
	vclips.y = crtc->y;
	vclips.w = crtc->mode.hdisplay;
	vclips.h = crtc->mode.vdisplay;

	if (vfb->dmabuf)
		ret = vmw_kms_stdu_dma(dev_priv, NULL, vfb, NULL, NULL, &vclips,
				       1, 1, true, false);
	else
		ret = vmw_kms_stdu_surface_dirty(dev_priv, vfb, NULL, &vclips,
						 NULL, 0, 0, 1, 1, NULL);
	if (ret) {
		DRM_ERROR("Page flip update error %d.\n", ret);
		return ret;
	}

	if (event) {
		struct vmw_fence_obj *fence = NULL;
		struct drm_file *file_priv = event->base.file_priv;

		vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
		if (!fence)
			return -ENOMEM;

		ret = vmw_event_fence_action_queue(file_priv, fence,
						   &event->base,
						   &event->event.tv_sec,
						   &event->event.tv_usec,
						   true);
		vmw_fence_obj_unreference(&fence);
	} else {
		(void) vmw_fifo_flush(dev_priv, false);
	}

	return 0;
}


/**
 * vmw_stdu_dmabuf_clip - Callback to encode a suface DMA command cliprect
 *
 * @dirty: The closure structure.
 *
 * Encodes a surface DMA command cliprect and updates the bounding box
 * for the DMA.
 */
static void vmw_stdu_dmabuf_clip(struct vmw_kms_dirty *dirty)
{
	struct vmw_stdu_dirty *ddirty =
		container_of(dirty, struct vmw_stdu_dirty, base);
	struct vmw_stdu_dma *cmd = dirty->cmd;
	struct SVGA3dCopyBox *blit = (struct SVGA3dCopyBox *) &cmd[1];

	blit += dirty->num_hits;
	blit->srcx = dirty->fb_x;
	blit->srcy = dirty->fb_y;
	blit->x = dirty->unit_x1;
	blit->y = dirty->unit_y1;
	blit->d = 1;
	blit->w = dirty->unit_x2 - dirty->unit_x1;
	blit->h = dirty->unit_y2 - dirty->unit_y1;
	dirty->num_hits++;

	if (ddirty->transfer != SVGA3D_WRITE_HOST_VRAM)
		return;

	/* Destination bounding box */
	ddirty->left = min_t(s32, ddirty->left, dirty->unit_x1);
	ddirty->top = min_t(s32, ddirty->top, dirty->unit_y1);
	ddirty->right = max_t(s32, ddirty->right, dirty->unit_x2);
	ddirty->bottom = max_t(s32, ddirty->bottom, dirty->unit_y2);
}

/**
 * vmw_stdu_dmabuf_fifo_commit - Callback to fill in and submit a DMA command.
 *
 * @dirty: The closure structure.
 *
 * Fills in the missing fields in a DMA command, and optionally encodes
 * a screen target update command, depending on transfer direction.
 */
static void vmw_stdu_dmabuf_fifo_commit(struct vmw_kms_dirty *dirty)
{
	struct vmw_stdu_dirty *ddirty =
		container_of(dirty, struct vmw_stdu_dirty, base);
	struct vmw_screen_target_display_unit *stdu =
		container_of(dirty->unit, typeof(*stdu), base);
	struct vmw_stdu_dma *cmd = dirty->cmd;
	struct SVGA3dCopyBox *blit = (struct SVGA3dCopyBox *) &cmd[1];
	SVGA3dCmdSurfaceDMASuffix *suffix =
		(SVGA3dCmdSurfaceDMASuffix *) &blit[dirty->num_hits];
	size_t blit_size = sizeof(*blit) * dirty->num_hits + sizeof(*suffix);

	if (!dirty->num_hits) {
		vmw_fifo_commit(dirty->dev_priv, 0);
		return;
	}

	cmd->header.id = SVGA_3D_CMD_SURFACE_DMA;
	cmd->header.size = sizeof(cmd->body) + blit_size;
	vmw_bo_get_guest_ptr(&ddirty->buf->base, &cmd->body.guest.ptr);
	cmd->body.guest.pitch = ddirty->pitch;
	cmd->body.host.sid = stdu->display_srf->res.id;
	cmd->body.host.face = 0;
	cmd->body.host.mipmap = 0;
	cmd->body.transfer = ddirty->transfer;
	suffix->suffixSize = sizeof(*suffix);
	suffix->maximumOffset = ddirty->buf->base.num_pages * PAGE_SIZE;

	if (ddirty->transfer == SVGA3D_WRITE_HOST_VRAM) {
		blit_size += sizeof(struct vmw_stdu_update);

		vmw_stdu_populate_update(&suffix[1], stdu->base.unit,
					 ddirty->left, ddirty->right,
					 ddirty->top, ddirty->bottom);
	}

	vmw_fifo_commit(dirty->dev_priv, sizeof(*cmd) + blit_size);

	ddirty->left = ddirty->top = S32_MAX;
	ddirty->right = ddirty->bottom = S32_MIN;
}


/**
 * vmw_stdu_dmabuf_cpu_clip - Callback to encode a CPU blit
 *
 * @dirty: The closure structure.
 *
 * This function calculates the bounding box for all the incoming clips
 */
static void vmw_stdu_dmabuf_cpu_clip(struct vmw_kms_dirty *dirty)
{
	struct vmw_stdu_dirty *ddirty =
		container_of(dirty, struct vmw_stdu_dirty, base);

	dirty->num_hits = 1;

	/* Calculate bounding box */
	ddirty->left = min_t(s32, ddirty->left, dirty->unit_x1);
	ddirty->top = min_t(s32, ddirty->top, dirty->unit_y1);
	ddirty->right = max_t(s32, ddirty->right, dirty->unit_x2);
	ddirty->bottom = max_t(s32, ddirty->bottom, dirty->unit_y2);
}


/**
 * vmw_stdu_dmabuf_cpu_commit - Callback to do a CPU blit from DMAbuf
 *
 * @dirty: The closure structure.
 *
 * For the special case when we cannot create a proxy surface in a
 * 2D VM, we have to do a CPU blit ourselves.
 */
static void vmw_stdu_dmabuf_cpu_commit(struct vmw_kms_dirty *dirty)
{
	struct vmw_stdu_dirty *ddirty =
		container_of(dirty, struct vmw_stdu_dirty, base);
	struct vmw_screen_target_display_unit *stdu =
		container_of(dirty->unit, typeof(*stdu), base);
	s32 width, height;
	s32 src_pitch, dst_pitch;
	u8 *src, *dst;
	bool not_used;


	if (!dirty->num_hits)
		return;

	width = ddirty->right - ddirty->left;
	height = ddirty->bottom - ddirty->top;

	if (width == 0 || height == 0)
		return;


	/* Assume we are blitting from Host (display_srf) to Guest (dmabuf) */
	src_pitch = stdu->display_srf->base_size.width * stdu->cpp;
	src = ttm_kmap_obj_virtual(&stdu->host_map, &not_used);
	src += dirty->unit_y1 * src_pitch + dirty->unit_x1 * stdu->cpp;

	dst_pitch = ddirty->pitch;
	dst = ttm_kmap_obj_virtual(&stdu->guest_map, &not_used);
	dst += dirty->fb_y * dst_pitch + dirty->fb_x * stdu->cpp;


	/* Figure out the real direction */
	if (ddirty->transfer == SVGA3D_WRITE_HOST_VRAM) {
		u8 *tmp;
		s32 tmp_pitch;

		tmp = src;
		tmp_pitch = src_pitch;

		src = dst;
		src_pitch = dst_pitch;

		dst = tmp;
		dst_pitch = tmp_pitch;
	}

	/* CPU Blit */
	while (height-- > 0) {
		memcpy(dst, src, width * stdu->cpp);
		dst += dst_pitch;
		src += src_pitch;
	}

	if (ddirty->transfer == SVGA3D_WRITE_HOST_VRAM) {
		struct vmw_private *dev_priv;
		struct vmw_stdu_update *cmd;
		struct drm_clip_rect region;
		int ret;

		/* We are updating the actual surface, not a proxy */
		region.x1 = ddirty->left;
		region.x2 = ddirty->right;
		region.y1 = ddirty->top;
		region.y2 = ddirty->bottom;
		ret = vmw_kms_update_proxy(
			(struct vmw_resource *) &stdu->display_srf->res,
			(const struct drm_clip_rect *) &region, 1, 1);
		if (ret)
			goto out_cleanup;


		dev_priv = vmw_priv(stdu->base.crtc.dev);
		cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

		if (!cmd) {
			DRM_ERROR("Cannot reserve FIFO space to update STDU");
			goto out_cleanup;
		}

		vmw_stdu_populate_update(cmd, stdu->base.unit,
					 ddirty->left, ddirty->right,
					 ddirty->top, ddirty->bottom);

		vmw_fifo_commit(dev_priv, sizeof(*cmd));
	}

out_cleanup:
	ddirty->left = ddirty->top = S32_MAX;
	ddirty->right = ddirty->bottom = S32_MIN;
}

/**
 * vmw_kms_stdu_dma - Perform a DMA transfer between a dma-buffer backed
 * framebuffer and the screen target system.
 *
 * @dev_priv: Pointer to the device private structure.
 * @file_priv: Pointer to a struct drm-file identifying the caller. May be
 * set to NULL, but then @user_fence_rep must also be set to NULL.
 * @vfb: Pointer to the dma-buffer backed framebuffer.
 * @clips: Array of clip rects. Either @clips or @vclips must be NULL.
 * @vclips: Alternate array of clip rects. Either @clips or @vclips must
 * be NULL.
 * @num_clips: Number of clip rects in @clips or @vclips.
 * @increment: Increment to use when looping over @clips or @vclips.
 * @to_surface: Whether to DMA to the screen target system as opposed to
 * from the screen target system.
 * @interruptible: Whether to perform waits interruptible if possible.
 *
 * If DMA-ing till the screen target system, the function will also notify
 * the screen target system that a bounding box of the cliprects has been
 * updated.
 * Returns 0 on success, negative error code on failure. -ERESTARTSYS if
 * interrupted.
 */
int vmw_kms_stdu_dma(struct vmw_private *dev_priv,
		     struct drm_file *file_priv,
		     struct vmw_framebuffer *vfb,
		     struct drm_vmw_fence_rep __user *user_fence_rep,
		     struct drm_clip_rect *clips,
		     struct drm_vmw_rect *vclips,
		     uint32_t num_clips,
		     int increment,
		     bool to_surface,
		     bool interruptible)
{
	struct vmw_dma_buffer *buf =
		container_of(vfb, struct vmw_framebuffer_dmabuf, base)->buffer;
	struct vmw_stdu_dirty ddirty;
	int ret;

	ret = vmw_kms_helper_buffer_prepare(dev_priv, buf, interruptible,
					    false);
	if (ret)
		return ret;

	ddirty.transfer = (to_surface) ? SVGA3D_WRITE_HOST_VRAM :
		SVGA3D_READ_HOST_VRAM;
	ddirty.left = ddirty.top = S32_MAX;
	ddirty.right = ddirty.bottom = S32_MIN;
	ddirty.pitch = vfb->base.pitches[0];
	ddirty.buf = buf;
	ddirty.base.fifo_commit = vmw_stdu_dmabuf_fifo_commit;
	ddirty.base.clip = vmw_stdu_dmabuf_clip;
	ddirty.base.fifo_reserve_size = sizeof(struct vmw_stdu_dma) +
		num_clips * sizeof(SVGA3dCopyBox) +
		sizeof(SVGA3dCmdSurfaceDMASuffix);
	if (to_surface)
		ddirty.base.fifo_reserve_size += sizeof(struct vmw_stdu_update);

	/* 2D VMs cannot use SVGA_3D_CMD_SURFACE_DMA so do CPU blit instead */
	if (!(dev_priv->capabilities & SVGA_CAP_3D)) {
		ddirty.base.fifo_commit = vmw_stdu_dmabuf_cpu_commit;
		ddirty.base.clip = vmw_stdu_dmabuf_cpu_clip;
		ddirty.base.fifo_reserve_size = 0;
	}

	ret = vmw_kms_helper_dirty(dev_priv, vfb, clips, vclips,
				   0, 0, num_clips, increment, &ddirty.base);
	vmw_kms_helper_buffer_finish(dev_priv, file_priv, buf, NULL,
				     user_fence_rep);

	return ret;
}

/**
 * vmw_stdu_surface_clip - Callback to encode a surface copy command cliprect
 *
 * @dirty: The closure structure.
 *
 * Encodes a surface copy command cliprect and updates the bounding box
 * for the copy.
 */
static void vmw_kms_stdu_surface_clip(struct vmw_kms_dirty *dirty)
{
	struct vmw_stdu_dirty *sdirty =
		container_of(dirty, struct vmw_stdu_dirty, base);
	struct vmw_stdu_surface_copy *cmd = dirty->cmd;
	struct vmw_screen_target_display_unit *stdu =
		container_of(dirty->unit, typeof(*stdu), base);

	if (sdirty->sid != stdu->display_srf->res.id) {
		struct SVGA3dCopyBox *blit = (struct SVGA3dCopyBox *) &cmd[1];

		blit += dirty->num_hits;
		blit->srcx = dirty->fb_x;
		blit->srcy = dirty->fb_y;
		blit->x = dirty->unit_x1;
		blit->y = dirty->unit_y1;
		blit->d = 1;
		blit->w = dirty->unit_x2 - dirty->unit_x1;
		blit->h = dirty->unit_y2 - dirty->unit_y1;
	}

	dirty->num_hits++;

	/* Destination bounding box */
	sdirty->left = min_t(s32, sdirty->left, dirty->unit_x1);
	sdirty->top = min_t(s32, sdirty->top, dirty->unit_y1);
	sdirty->right = max_t(s32, sdirty->right, dirty->unit_x2);
	sdirty->bottom = max_t(s32, sdirty->bottom, dirty->unit_y2);
}

/**
 * vmw_stdu_surface_fifo_commit - Callback to fill in and submit a surface
 * copy command.
 *
 * @dirty: The closure structure.
 *
 * Fills in the missing fields in a surface copy command, and encodes a screen
 * target update command.
 */
static void vmw_kms_stdu_surface_fifo_commit(struct vmw_kms_dirty *dirty)
{
	struct vmw_stdu_dirty *sdirty =
		container_of(dirty, struct vmw_stdu_dirty, base);
	struct vmw_screen_target_display_unit *stdu =
		container_of(dirty->unit, typeof(*stdu), base);
	struct vmw_stdu_surface_copy *cmd = dirty->cmd;
	struct vmw_stdu_update *update;
	size_t blit_size = sizeof(SVGA3dCopyBox) * dirty->num_hits;
	size_t commit_size;

	if (!dirty->num_hits) {
		vmw_fifo_commit(dirty->dev_priv, 0);
		return;
	}

	if (sdirty->sid != stdu->display_srf->res.id) {
		struct SVGA3dCopyBox *blit = (struct SVGA3dCopyBox *) &cmd[1];

		cmd->header.id = SVGA_3D_CMD_SURFACE_COPY;
		cmd->header.size = sizeof(cmd->body) + blit_size;
		cmd->body.src.sid = sdirty->sid;
		cmd->body.dest.sid = stdu->display_srf->res.id;
		update = (struct vmw_stdu_update *) &blit[dirty->num_hits];
		commit_size = sizeof(*cmd) + blit_size + sizeof(*update);
	} else {
		update = dirty->cmd;
		commit_size = sizeof(*update);
	}

	vmw_stdu_populate_update(update, stdu->base.unit, sdirty->left,
				 sdirty->right, sdirty->top, sdirty->bottom);

	vmw_fifo_commit(dirty->dev_priv, commit_size);

	sdirty->left = sdirty->top = S32_MAX;
	sdirty->right = sdirty->bottom = S32_MIN;
}

/**
 * vmw_kms_stdu_surface_dirty - Dirty part of a surface backed framebuffer
 *
 * @dev_priv: Pointer to the device private structure.
 * @framebuffer: Pointer to the surface-buffer backed framebuffer.
 * @clips: Array of clip rects. Either @clips or @vclips must be NULL.
 * @vclips: Alternate array of clip rects. Either @clips or @vclips must
 * be NULL.
 * @srf: Pointer to surface to blit from. If NULL, the surface attached
 * to @framebuffer will be used.
 * @dest_x: X coordinate offset to align @srf with framebuffer coordinates.
 * @dest_y: Y coordinate offset to align @srf with framebuffer coordinates.
 * @num_clips: Number of clip rects in @clips.
 * @inc: Increment to use when looping over @clips.
 * @out_fence: If non-NULL, will return a ref-counted pointer to a
 * struct vmw_fence_obj. The returned fence pointer may be NULL in which
 * case the device has already synchronized.
 *
 * Returns 0 on success, negative error code on failure. -ERESTARTSYS if
 * interrupted.
 */
int vmw_kms_stdu_surface_dirty(struct vmw_private *dev_priv,
			       struct vmw_framebuffer *framebuffer,
			       struct drm_clip_rect *clips,
			       struct drm_vmw_rect *vclips,
			       struct vmw_resource *srf,
			       s32 dest_x,
			       s32 dest_y,
			       unsigned num_clips, int inc,
			       struct vmw_fence_obj **out_fence)
{
	struct vmw_framebuffer_surface *vfbs =
		container_of(framebuffer, typeof(*vfbs), base);
	struct vmw_stdu_dirty sdirty;
	int ret;

	if (!srf)
		srf = &vfbs->surface->res;

	ret = vmw_kms_helper_resource_prepare(srf, true);
	if (ret)
		return ret;

	if (vfbs->is_dmabuf_proxy) {
		ret = vmw_kms_update_proxy(srf, clips, num_clips, inc);
		if (ret)
			goto out_finish;
	}

	sdirty.base.fifo_commit = vmw_kms_stdu_surface_fifo_commit;
	sdirty.base.clip = vmw_kms_stdu_surface_clip;
	sdirty.base.fifo_reserve_size = sizeof(struct vmw_stdu_surface_copy) +
		sizeof(SVGA3dCopyBox) * num_clips +
		sizeof(struct vmw_stdu_update);
	sdirty.sid = srf->id;
	sdirty.left = sdirty.top = S32_MAX;
	sdirty.right = sdirty.bottom = S32_MIN;

	ret = vmw_kms_helper_dirty(dev_priv, framebuffer, clips, vclips,
				   dest_x, dest_y, num_clips, inc,
				   &sdirty.base);
out_finish:
	vmw_kms_helper_resource_finish(srf, out_fence);

	return ret;
}


/*
 *  Screen Target CRTC dispatch table
 */
static const struct drm_crtc_funcs vmw_stdu_crtc_funcs = {
	.gamma_set = vmw_du_crtc_gamma_set,
	.destroy = vmw_stdu_crtc_destroy,
	.reset = vmw_du_crtc_reset,
	.atomic_duplicate_state = vmw_du_crtc_duplicate_state,
	.atomic_destroy_state = vmw_du_crtc_destroy_state,
	.set_config = vmw_kms_set_config,
	.page_flip = vmw_stdu_crtc_page_flip,
};



/******************************************************************************
 * Screen Target Display Unit Encoder Functions
 *****************************************************************************/

/**
 * vmw_stdu_encoder_destroy - cleans up the STDU
 *
 * @encoder: used the get the containing STDU
 *
 * vmwgfx cleans up crtc/encoder/connector all at the same time so technically
 * this can be a no-op.  Nevertheless, it doesn't hurt of have this in case
 * the common KMS code changes and somehow vmw_stdu_crtc_destroy() doesn't
 * get called.
 */
static void vmw_stdu_encoder_destroy(struct drm_encoder *encoder)
{
	vmw_stdu_destroy(vmw_encoder_to_stdu(encoder));
}

static const struct drm_encoder_funcs vmw_stdu_encoder_funcs = {
	.destroy = vmw_stdu_encoder_destroy,
};



/******************************************************************************
 * Screen Target Display Unit Connector Functions
 *****************************************************************************/

/**
 * vmw_stdu_connector_destroy - cleans up the STDU
 *
 * @connector: used to get the containing STDU
 *
 * vmwgfx cleans up crtc/encoder/connector all at the same time so technically
 * this can be a no-op.  Nevertheless, it doesn't hurt of have this in case
 * the common KMS code changes and somehow vmw_stdu_crtc_destroy() doesn't
 * get called.
 */
static void vmw_stdu_connector_destroy(struct drm_connector *connector)
{
	vmw_stdu_destroy(vmw_connector_to_stdu(connector));
}



static const struct drm_connector_funcs vmw_stdu_connector_funcs = {
	.dpms = vmw_du_connector_dpms,
	.detect = vmw_du_connector_detect,
	.fill_modes = vmw_du_connector_fill_modes,
	.set_property = vmw_du_connector_set_property,
	.destroy = vmw_stdu_connector_destroy,
	.reset = vmw_du_connector_reset,
	.atomic_duplicate_state = vmw_du_connector_duplicate_state,
	.atomic_destroy_state = vmw_du_connector_destroy_state,
	.atomic_set_property = vmw_du_connector_atomic_set_property,
	.atomic_get_property = vmw_du_connector_atomic_get_property,
};


static const struct
drm_connector_helper_funcs vmw_stdu_connector_helper_funcs = {
	.best_encoder = drm_atomic_helper_best_encoder,
};



/******************************************************************************
 * Screen Target Display Plane Functions
 *****************************************************************************/



/**
 * vmw_stdu_primary_plane_cleanup_fb - Unpins the display surface
 *
 * @plane:  display plane
 * @old_state: Contains the FB to clean up
 *
 * Unpins the display surface
 *
 * Returns 0 on success
 */
static void
vmw_stdu_primary_plane_cleanup_fb(struct drm_plane *plane,
				  struct drm_plane_state *old_state)
{
	struct vmw_plane_state *vps = vmw_plane_state_to_vps(old_state);

	if (vps->guest_map.virtual)
		ttm_bo_kunmap(&vps->guest_map);

	if (vps->host_map.virtual)
		ttm_bo_kunmap(&vps->host_map);

	if (vps->surf)
		WARN_ON(!vps->pinned);

	vmw_du_plane_cleanup_fb(plane, old_state);

	vps->content_fb_type = SAME_AS_DISPLAY;
	vps->cpp = 0;
}



/**
 * vmw_stdu_primary_plane_prepare_fb - Readies the display surface
 *
 * @plane:  display plane
 * @new_state: info on the new plane state, including the FB
 *
 * This function allocates a new display surface if the content is
 * backed by a DMA.  The display surface is pinned here, and it'll
 * be unpinned in .cleanup_fb()
 *
 * Returns 0 on success
 */
static int
vmw_stdu_primary_plane_prepare_fb(struct drm_plane *plane,
				  struct drm_plane_state *new_state)
{
	struct vmw_private *dev_priv = vmw_priv(plane->dev);
	struct drm_framebuffer *new_fb = new_state->fb;
	struct vmw_framebuffer *vfb;
	struct vmw_plane_state *vps = vmw_plane_state_to_vps(new_state);
	enum stdu_content_type new_content_type;
	struct vmw_framebuffer_surface *new_vfbs;
	struct drm_crtc *crtc = new_state->crtc;
	uint32_t hdisplay = new_state->crtc_w, vdisplay = new_state->crtc_h;
	int ret;

	/* No FB to prepare */
	if (!new_fb) {
		if (vps->surf) {
			WARN_ON(vps->pinned != 0);
			vmw_surface_unreference(&vps->surf);
		}

		return 0;
	}

	vfb = vmw_framebuffer_to_vfb(new_fb);
	new_vfbs = (vfb->dmabuf) ? NULL : vmw_framebuffer_to_vfbs(new_fb);

	if (new_vfbs && new_vfbs->surface->base_size.width == hdisplay &&
	    new_vfbs->surface->base_size.height == vdisplay)
		new_content_type = SAME_AS_DISPLAY;
	else if (vfb->dmabuf)
		new_content_type = SEPARATE_DMA;
	else
		new_content_type = SEPARATE_SURFACE;

	if (new_content_type != SAME_AS_DISPLAY) {
		struct vmw_surface content_srf;
		struct drm_vmw_size display_base_size = {0};

		display_base_size.width  = hdisplay;
		display_base_size.height = vdisplay;
		display_base_size.depth  = 1;

		/*
		 * If content buffer is a DMA buf, then we have to construct
		 * surface info
		 */
		if (new_content_type == SEPARATE_DMA) {

			switch (new_fb->format->cpp[0]*8) {
			case 32:
				content_srf.format = SVGA3D_X8R8G8B8;
				break;

			case 16:
				content_srf.format = SVGA3D_R5G6B5;
				break;

			case 8:
				content_srf.format = SVGA3D_P8;
				break;

			default:
				DRM_ERROR("Invalid format\n");
				return -EINVAL;
			}

			content_srf.flags             = 0;
			content_srf.mip_levels[0]     = 1;
			content_srf.multisample_count = 0;
		} else {
			content_srf = *new_vfbs->surface;
		}

		if (vps->surf) {
			struct drm_vmw_size cur_base_size = vps->surf->base_size;

			if (cur_base_size.width != display_base_size.width ||
			    cur_base_size.height != display_base_size.height ||
			    vps->surf->format != content_srf.format) {
				WARN_ON(vps->pinned != 0);
				vmw_surface_unreference(&vps->surf);
			}

		}

		if (!vps->surf) {
			ret = vmw_surface_gb_priv_define
				(crtc->dev,
				 /* Kernel visible only */
				 0,
				 content_srf.flags,
				 content_srf.format,
				 true,  /* a scanout buffer */
				 content_srf.mip_levels[0],
				 content_srf.multisample_count,
				 0,
				 display_base_size,
				 &vps->surf);
			if (ret != 0) {
				DRM_ERROR("Couldn't allocate STDU surface.\n");
				return ret;
			}
		}
	} else {
		/*
		 * prepare_fb and clean_fb should only take care of pinning
		 * and unpinning.  References are tracked by state objects.
		 * The only time we add a reference in prepare_fb is if the
		 * state object doesn't have a reference to begin with
		 */
		if (vps->surf) {
			WARN_ON(vps->pinned != 0);
			vmw_surface_unreference(&vps->surf);
		}

		vps->surf = vmw_surface_reference(new_vfbs->surface);
	}

	if (vps->surf) {

		/* Pin new surface before flipping */
		ret = vmw_resource_pin(&vps->surf->res, false);
		if (ret)
			goto out_srf_unref;

		vps->pinned++;
	}

	vps->content_fb_type = new_content_type;

	/*
	 * This should only happen if the DMA buf is too large to create a
	 * proxy surface for.
	 * If we are a 2D VM with a DMA buffer then we have to use CPU blit
	 * so cache these mappings
	 */
	if (vps->content_fb_type == SEPARATE_DMA &&
	    !(dev_priv->capabilities & SVGA_CAP_3D)) {

		struct vmw_framebuffer_dmabuf *new_vfbd;

		new_vfbd = vmw_framebuffer_to_vfbd(new_fb);

		ret = ttm_bo_reserve(&new_vfbd->buffer->base, false, false,
				     NULL);
		if (ret)
			goto out_srf_unpin;

		ret = ttm_bo_kmap(&new_vfbd->buffer->base, 0,
				  new_vfbd->buffer->base.num_pages,
				  &vps->guest_map);

		ttm_bo_unreserve(&new_vfbd->buffer->base);

		if (ret) {
			DRM_ERROR("Failed to map content buffer to CPU\n");
			goto out_srf_unpin;
		}

		ret = ttm_bo_kmap(&vps->surf->res.backup->base, 0,
				  vps->surf->res.backup->base.num_pages,
				  &vps->host_map);
		if (ret) {
			DRM_ERROR("Failed to map display buffer to CPU\n");
			ttm_bo_kunmap(&vps->guest_map);
			goto out_srf_unpin;
		}

		vps->cpp = new_fb->pitches[0] / new_fb->width;
	}

	return 0;

out_srf_unpin:
	vmw_resource_unpin(&vps->surf->res);
	vps->pinned--;

out_srf_unref:
	vmw_surface_unreference(&vps->surf);
	return ret;
}



/**
 * vmw_stdu_primary_plane_atomic_update - formally switches STDU to new plane
 *
 * @plane: display plane
 * @old_state: Only used to get crtc info
 *
 * Formally update stdu->display_srf to the new plane, and bind the new
 * plane STDU.  This function is called during the commit phase when
 * all the preparation have been done and all the configurations have
 * been checked.
 */
static void
vmw_stdu_primary_plane_atomic_update(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct vmw_private *dev_priv;
	struct vmw_screen_target_display_unit *stdu;
	struct vmw_plane_state *vps = vmw_plane_state_to_vps(plane->state);
	struct drm_crtc *crtc = plane->state->crtc ?: old_state->crtc;
	int ret;

	stdu     = vmw_crtc_to_stdu(crtc);
	dev_priv = vmw_priv(crtc->dev);

	stdu->display_srf = vps->surf;
	stdu->content_fb_type = vps->content_fb_type;
	stdu->cpp = vps->cpp;
	memcpy(&stdu->guest_map, &vps->guest_map, sizeof(vps->guest_map));
	memcpy(&stdu->host_map, &vps->host_map, sizeof(vps->host_map));

	if (!stdu->defined)
		return;

	if (plane->state->fb)
		ret = vmw_stdu_bind_st(dev_priv, stdu, &stdu->display_srf->res);
	else
		ret = vmw_stdu_bind_st(dev_priv, stdu, NULL);

	/*
	 * We cannot really fail this function, so if we do, then output an
	 * error and quit
	 */
	if (ret)
		DRM_ERROR("Failed to bind surface to STDU.\n");
	else
		crtc->primary->fb = plane->state->fb;
}


static const struct drm_plane_funcs vmw_stdu_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = vmw_du_primary_plane_destroy,
	.reset = vmw_du_plane_reset,
	.atomic_duplicate_state = vmw_du_plane_duplicate_state,
	.atomic_destroy_state = vmw_du_plane_destroy_state,
};

static const struct drm_plane_funcs vmw_stdu_cursor_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = vmw_du_cursor_plane_destroy,
	.reset = vmw_du_plane_reset,
	.atomic_duplicate_state = vmw_du_plane_duplicate_state,
	.atomic_destroy_state = vmw_du_plane_destroy_state,
};


/*
 * Atomic Helpers
 */
static const struct
drm_plane_helper_funcs vmw_stdu_cursor_plane_helper_funcs = {
	.atomic_check = vmw_du_cursor_plane_atomic_check,
	.atomic_update = vmw_du_cursor_plane_atomic_update,
	.prepare_fb = vmw_du_cursor_plane_prepare_fb,
	.cleanup_fb = vmw_du_plane_cleanup_fb,
};

static const struct
drm_plane_helper_funcs vmw_stdu_primary_plane_helper_funcs = {
	.atomic_check = vmw_du_primary_plane_atomic_check,
	.atomic_update = vmw_stdu_primary_plane_atomic_update,
	.prepare_fb = vmw_stdu_primary_plane_prepare_fb,
	.cleanup_fb = vmw_stdu_primary_plane_cleanup_fb,
};

static const struct drm_crtc_helper_funcs vmw_stdu_crtc_helper_funcs = {
	.prepare = vmw_stdu_crtc_helper_prepare,
	.commit = vmw_stdu_crtc_helper_commit,
	.disable = vmw_stdu_crtc_helper_disable,
	.mode_set_nofb = vmw_stdu_crtc_mode_set_nofb,
	.atomic_check = vmw_du_crtc_atomic_check,
	.atomic_begin = vmw_du_crtc_atomic_begin,
	.atomic_flush = vmw_du_crtc_atomic_flush,
};


/**
 * vmw_stdu_init - Sets up a Screen Target Display Unit
 *
 * @dev_priv: VMW DRM device
 * @unit: unit number range from 0 to VMWGFX_NUM_DISPLAY_UNITS
 *
 * This function is called once per CRTC, and allocates one Screen Target
 * display unit to represent that CRTC.  Since the SVGA device does not separate
 * out encoder and connector, they are represented as part of the STDU as well.
 */
static int vmw_stdu_init(struct vmw_private *dev_priv, unsigned unit)
{
	struct vmw_screen_target_display_unit *stdu;
	struct drm_device *dev = dev_priv->dev;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_plane *primary, *cursor;
	struct drm_crtc *crtc;
	int    ret;


	stdu = kzalloc(sizeof(*stdu), GFP_KERNEL);
	if (!stdu)
		return -ENOMEM;

	stdu->base.unit = unit;
	crtc = &stdu->base.crtc;
	encoder = &stdu->base.encoder;
	connector = &stdu->base.connector;
	primary = &stdu->base.primary;
	cursor = &stdu->base.cursor;

	stdu->base.pref_active = (unit == 0);
	stdu->base.pref_width  = dev_priv->initial_width;
	stdu->base.pref_height = dev_priv->initial_height;

	/*
	 * Remove this after enabling atomic because property values can
	 * only exist in a state object
	 */
	stdu->base.is_implicit = false;

	/* Initialize primary plane */
	vmw_du_plane_reset(primary);

	ret = drm_universal_plane_init(dev, primary,
				       0, &vmw_stdu_plane_funcs,
				       vmw_primary_plane_formats,
				       ARRAY_SIZE(vmw_primary_plane_formats),
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize primary plane");
		goto err_free;
	}

	drm_plane_helper_add(primary, &vmw_stdu_primary_plane_helper_funcs);

	/* Initialize cursor plane */
	vmw_du_plane_reset(cursor);

	ret = drm_universal_plane_init(dev, cursor,
			0, &vmw_stdu_cursor_funcs,
			vmw_cursor_plane_formats,
			ARRAY_SIZE(vmw_cursor_plane_formats),
			DRM_PLANE_TYPE_CURSOR, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize cursor plane");
		drm_plane_cleanup(&stdu->base.primary);
		goto err_free;
	}

	drm_plane_helper_add(cursor, &vmw_stdu_cursor_plane_helper_funcs);

	vmw_du_connector_reset(connector);

	ret = drm_connector_init(dev, connector, &vmw_stdu_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		goto err_free;
	}

	drm_connector_helper_add(connector, &vmw_stdu_connector_helper_funcs);
	connector->status = vmw_du_connector_detect(connector, false);
	vmw_connector_state_to_vcs(connector->state)->is_implicit = false;

	ret = drm_encoder_init(dev, encoder, &vmw_stdu_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize encoder\n");
		goto err_free_connector;
	}

	(void) drm_mode_connector_attach_encoder(connector, encoder);
	encoder->possible_crtcs = (1 << unit);
	encoder->possible_clones = 0;

	ret = drm_connector_register(connector);
	if (ret) {
		DRM_ERROR("Failed to register connector\n");
		goto err_free_encoder;
	}

	vmw_du_crtc_reset(crtc);
	ret = drm_crtc_init_with_planes(dev, crtc, &stdu->base.primary,
					&stdu->base.cursor,
					&vmw_stdu_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize CRTC\n");
		goto err_free_unregister;
	}

	drm_crtc_helper_add(crtc, &vmw_stdu_crtc_helper_funcs);

	drm_mode_crtc_set_gamma_size(crtc, 256);

	drm_object_attach_property(&connector->base,
				   dev_priv->hotplug_mode_update_property, 1);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_x_property, 0);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.suggested_y_property, 0);
	if (dev_priv->implicit_placement_property)
		drm_object_attach_property
			(&connector->base,
			 dev_priv->implicit_placement_property,
			 stdu->base.is_implicit);
	return 0;

err_free_unregister:
	drm_connector_unregister(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
err_free_connector:
	drm_connector_cleanup(connector);
err_free:
	kfree(stdu);
	return ret;
}



/**
 *  vmw_stdu_destroy - Cleans up a vmw_screen_target_display_unit
 *
 *  @stdu:  Screen Target Display Unit to be destroyed
 *
 *  Clean up after vmw_stdu_init
 */
static void vmw_stdu_destroy(struct vmw_screen_target_display_unit *stdu)
{
	vmw_du_cleanup(&stdu->base);
	kfree(stdu);
}



/******************************************************************************
 * Screen Target Display KMS Functions
 *
 * These functions are called by the common KMS code in vmwgfx_kms.c
 *****************************************************************************/

/**
 * vmw_kms_stdu_init_display - Initializes a Screen Target based display
 *
 * @dev_priv: VMW DRM device
 *
 * This function initialize a Screen Target based display device.  It checks
 * the capability bits to make sure the underlying hardware can support
 * screen targets, and then creates the maximum number of CRTCs, a.k.a Display
 * Units, as supported by the display hardware.
 *
 * RETURNS:
 * 0 on success, error code otherwise
 */
int vmw_kms_stdu_init_display(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	int i, ret;


	/* Do nothing if Screen Target support is turned off */
	if (!VMWGFX_ENABLE_SCREEN_TARGET_OTABLE)
		return -ENOSYS;

	if (!(dev_priv->capabilities & SVGA_CAP_GBOBJECTS))
		return -ENOSYS;

	ret = drm_vblank_init(dev, VMWGFX_NUM_DISPLAY_UNITS);
	if (unlikely(ret != 0))
		return ret;

	dev_priv->active_display_unit = vmw_du_screen_target;

	if (dev_priv->capabilities & SVGA_CAP_3D) {
		/*
		 * For 3D VMs, display (scanout) buffer size is the smaller of
		 * max texture and max STDU
		 */
		uint32_t max_width, max_height;

		max_width = min(dev_priv->texture_max_width,
				dev_priv->stdu_max_width);
		max_height = min(dev_priv->texture_max_height,
				 dev_priv->stdu_max_height);

		dev->mode_config.max_width = max_width;
		dev->mode_config.max_height = max_height;
	} else {
		/*
		 * Given various display aspect ratios, there's no way to
		 * estimate these using prim_bb_mem.  So just set these to
		 * something arbitrarily large and we will reject any layout
		 * that doesn't fit prim_bb_mem later
		 */
		dev->mode_config.max_width = 16384;
		dev->mode_config.max_height = 16384;
	}

	vmw_kms_create_implicit_placement_property(dev_priv, false);

	for (i = 0; i < VMWGFX_NUM_DISPLAY_UNITS; ++i) {
		ret = vmw_stdu_init(dev_priv, i);

		if (unlikely(ret != 0)) {
			DRM_ERROR("Failed to initialize STDU %d", i);
			goto err_vblank_cleanup;
		}
	}

	DRM_INFO("Screen Target Display device initialized\n");

	return 0;

err_vblank_cleanup:
	drm_vblank_cleanup(dev);
	return ret;
}



/**
 * vmw_kms_stdu_close_display - Cleans up after vmw_kms_stdu_init_display
 *
 * @dev_priv: VMW DRM device
 *
 * Frees up any resources allocated by vmw_kms_stdu_init_display
 *
 * RETURNS:
 * 0 on success
 */
int vmw_kms_stdu_close_display(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	drm_vblank_cleanup(dev);

	return 0;
}
