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

	struct vmw_surface     *display_srf;
	enum stdu_content_type content_fb_type;

	bool defined;
};



static void vmw_stdu_destroy(struct vmw_screen_target_display_unit *stdu);



/******************************************************************************
 * Screen Target Display Unit helper Functions
 *****************************************************************************/

/**
 * vmw_stdu_unpin_display - unpins the resource associated with display surface
 *
 * @stdu: contains the display surface
 *
 * If the display surface was privatedly allocated by
 * vmw_surface_gb_priv_define() and not registered as a framebuffer, then it
 * won't be automatically cleaned up when all the framebuffers are freed.  As
 * such, we have to explicitly call vmw_resource_unreference() to get it freed.
 */
static void vmw_stdu_unpin_display(struct vmw_screen_target_display_unit *stdu)
{
	if (stdu->display_srf) {
		struct vmw_resource *res = &stdu->display_srf->res;

		vmw_resource_unpin(res);
		vmw_surface_unreference(&stdu->display_srf);
	}
}



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
			    struct vmw_resource *res)
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
	struct drm_crtc *crtc = &stdu->base.crtc;

	if (!stdu->defined) {
		DRM_ERROR("No screen target defined");
		return -EINVAL;
	}

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Out of FIFO space updating a Screen Target\n");
		return -ENOMEM;
	}

	vmw_stdu_populate_update(cmd, stdu->base.unit, 0, crtc->mode.hdisplay,
				 0, crtc->mode.vdisplay);

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

	return ret;
}

/**
 * vmw_stdu_bind_fb - Bind an fb to a defined screen target
 *
 * @dev_priv: Pointer to a device private struct.
 * @crtc: The crtc holding the screen target.
 * @mode: The mode currently used by the screen target. Must be non-NULL.
 * @new_fb: The new framebuffer to bind. Must be non-NULL.
 *
 * RETURNS:
 * 0 on success, error code on failure.
 */
static int vmw_stdu_bind_fb(struct vmw_private *dev_priv,
			    struct drm_crtc *crtc,
			    struct drm_display_mode *mode,
			    struct drm_framebuffer *new_fb)
{
	struct vmw_screen_target_display_unit *stdu = vmw_crtc_to_stdu(crtc);
	struct vmw_framebuffer *vfb = vmw_framebuffer_to_vfb(new_fb);
	struct vmw_surface *new_display_srf = NULL;
	enum stdu_content_type new_content_type;
	struct vmw_framebuffer_surface *new_vfbs;
	int ret;

	WARN_ON_ONCE(!stdu->defined);

	new_vfbs = (vfb->dmabuf) ? NULL : vmw_framebuffer_to_vfbs(new_fb);

	if (new_vfbs && new_vfbs->surface->base_size.width == mode->hdisplay &&
	    new_vfbs->surface->base_size.height == mode->vdisplay)
		new_content_type = SAME_AS_DISPLAY;
	else if (vfb->dmabuf)
		new_content_type = SEPARATE_DMA;
	else
		new_content_type = SEPARATE_SURFACE;

	if (new_content_type != SAME_AS_DISPLAY &&
	    !stdu->display_srf) {
		struct vmw_surface content_srf;
		struct drm_vmw_size display_base_size = {0};

		display_base_size.width  = mode->hdisplay;
		display_base_size.height = mode->vdisplay;
		display_base_size.depth  = 1;

		/*
		 * If content buffer is a DMA buf, then we have to construct
		 * surface info
		 */
		if (new_content_type == SEPARATE_DMA) {

			switch (new_fb->format->cpp[0] * 8) {
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


		ret = vmw_surface_gb_priv_define(crtc->dev,
				0, /* because kernel visible only */
				content_srf.flags,
				content_srf.format,
				true, /* a scanout buffer */
				content_srf.mip_levels[0],
				content_srf.multisample_count,
				0,
				display_base_size,
				&new_display_srf);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Could not allocate screen target surface.\n");
			return ret;
		}
	} else if (new_content_type == SAME_AS_DISPLAY) {
		new_display_srf = vmw_surface_reference(new_vfbs->surface);
	}

	if (new_display_srf) {
		/* Pin new surface before flipping */
		ret = vmw_resource_pin(&new_display_srf->res, false);
		if (ret)
			goto out_srf_unref;

		ret = vmw_stdu_bind_st(dev_priv, stdu, &new_display_srf->res);
		if (ret)
			goto out_srf_unpin;

		/* Unpin and unreference old surface */
		vmw_stdu_unpin_display(stdu);

		/* Transfer the reference */
		stdu->display_srf = new_display_srf;
		new_display_srf = NULL;
	}

	crtc->primary->fb = new_fb;
	stdu->content_fb_type = new_content_type;
	return 0;

out_srf_unpin:
	vmw_resource_unpin(&new_display_srf->res);
out_srf_unref:
	vmw_surface_unreference(&new_display_srf);
	return ret;
}

/**
 * vmw_stdu_crtc_set_config - Sets a mode
 *
 * @set:  mode parameters
 *
 * This function is the device-specific portion of the DRM CRTC mode set.
 * For the SVGA device, we do this by defining a Screen Target, binding a
 * GB Surface to that target, and finally update the screen target.
 *
 * RETURNS:
 * 0 on success, error code otherwise
 */
static int vmw_stdu_crtc_set_config(struct drm_mode_set *set)
{
	struct vmw_private *dev_priv;
	struct vmw_framebuffer *vfb;
	struct vmw_screen_target_display_unit *stdu;
	struct drm_display_mode *mode;
	struct drm_framebuffer  *new_fb;
	struct drm_crtc      *crtc;
	struct drm_encoder   *encoder;
	struct drm_connector *connector;
	bool turning_off;
	int    ret;


	if (!set || !set->crtc)
		return -EINVAL;

	crtc     = set->crtc;
	stdu     = vmw_crtc_to_stdu(crtc);
	mode     = set->mode;
	new_fb   = set->fb;
	dev_priv = vmw_priv(crtc->dev);
	turning_off = set->num_connectors == 0 || !mode || !new_fb;
	vfb = (new_fb) ? vmw_framebuffer_to_vfb(new_fb) : NULL;

	if (set->num_connectors > 1) {
		DRM_ERROR("Too many connectors\n");
		return -EINVAL;
	}

	if (set->num_connectors == 1 &&
	    set->connectors[0] != &stdu->base.connector) {
		DRM_ERROR("Connectors don't match %p %p\n",
			set->connectors[0], &stdu->base.connector);
		return -EINVAL;
	}

	if (!turning_off && (set->x + mode->hdisplay > new_fb->width ||
			     set->y + mode->vdisplay > new_fb->height)) {
		DRM_ERROR("Set outside of framebuffer\n");
		return -EINVAL;
	}

	/* Only one active implicit frame-buffer at a time. */
	mutex_lock(&dev_priv->global_kms_state_mutex);
	if (!turning_off && stdu->base.is_implicit && dev_priv->implicit_fb &&
	    !(dev_priv->num_implicit == 1 && stdu->base.active_implicit)
	    && dev_priv->implicit_fb != vfb) {
		mutex_unlock(&dev_priv->global_kms_state_mutex);
		DRM_ERROR("Multiple implicit framebuffers not supported.\n");
		return -EINVAL;
	}
	mutex_unlock(&dev_priv->global_kms_state_mutex);

	/* Since they always map one to one these are safe */
	connector = &stdu->base.connector;
	encoder   = &stdu->base.encoder;

	if (stdu->defined) {
		ret = vmw_stdu_bind_st(dev_priv, stdu, NULL);
		if (ret)
			return ret;

		vmw_stdu_unpin_display(stdu);
		(void) vmw_stdu_update_st(dev_priv, stdu);
		vmw_kms_del_active(dev_priv, &stdu->base);

		ret = vmw_stdu_destroy_st(dev_priv, stdu);
		if (ret)
			return ret;

		crtc->primary->fb = NULL;
		crtc->enabled = false;
		encoder->crtc = NULL;
		connector->encoder = NULL;
		stdu->content_fb_type = SAME_AS_DISPLAY;
		crtc->x = set->x;
		crtc->y = set->y;
	}

	if (turning_off)
		return 0;

	/*
	 * Steps to displaying a surface, assume surface is already
	 * bound:
	 *   1.  define a screen target
	 *   2.  bind a fb to the screen target
	 *   3.  update that screen target (this is done later by
	 *       vmw_kms_stdu_do_surface_dirty_or_present)
	 */
	/*
	 * Note on error handling: We can't really restore the crtc to
	 * it's original state on error, but we at least update the
	 * current state to what's submitted to hardware to enable
	 * future recovery.
	 */
	vmw_svga_enable(dev_priv);
	ret = vmw_stdu_define_st(dev_priv, stdu, mode, set->x, set->y);
	if (ret)
		return ret;

	crtc->x = set->x;
	crtc->y = set->y;
	crtc->mode = *mode;

	ret = vmw_stdu_bind_fb(dev_priv, crtc, mode, new_fb);
	if (ret)
		return ret;

	vmw_kms_add_active(dev_priv, &stdu->base, vfb);
	crtc->enabled = true;
	connector->encoder = encoder;
	encoder->crtc      = crtc;

	return 0;
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
				   uint32_t flags)

{
	struct vmw_private *dev_priv = vmw_priv(crtc->dev);
	struct vmw_screen_target_display_unit *stdu;
	struct drm_vmw_rect vclips;
	struct vmw_framebuffer *vfb = vmw_framebuffer_to_vfb(new_fb);
	int ret;

	dev_priv          = vmw_priv(crtc->dev);
	stdu              = vmw_crtc_to_stdu(crtc);

	if (!stdu->defined || !vmw_kms_crtc_flippable(dev_priv, crtc))
		return -EINVAL;

	ret = vmw_stdu_bind_fb(dev_priv, crtc, &crtc->mode, new_fb);
	if (ret)
		return ret;

	if (stdu->base.is_implicit)
		vmw_kms_update_implicit_fb(dev_priv, crtc);

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
	if (ret)
		return ret;

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
		vmw_fifo_flush(dev_priv, false);
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
	.cursor_set2 = vmw_du_crtc_cursor_set2,
	.cursor_move = vmw_du_crtc_cursor_move,
	.gamma_set = vmw_du_crtc_gamma_set,
	.destroy = vmw_stdu_crtc_destroy,
	.set_config = vmw_stdu_crtc_set_config,
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
	struct drm_crtc *crtc;


	stdu = kzalloc(sizeof(*stdu), GFP_KERNEL);
	if (!stdu)
		return -ENOMEM;

	stdu->base.unit = unit;
	crtc = &stdu->base.crtc;
	encoder = &stdu->base.encoder;
	connector = &stdu->base.connector;

	stdu->base.pref_active = (unit == 0);
	stdu->base.pref_width  = dev_priv->initial_width;
	stdu->base.pref_height = dev_priv->initial_height;
	stdu->base.is_implicit = false;

	drm_connector_init(dev, connector, &vmw_stdu_connector_funcs,
			   DRM_MODE_CONNECTOR_VIRTUAL);
	connector->status = vmw_du_connector_detect(connector, false);

	drm_encoder_init(dev, encoder, &vmw_stdu_encoder_funcs,
			 DRM_MODE_ENCODER_VIRTUAL, NULL);
	drm_mode_connector_attach_encoder(connector, encoder);
	encoder->possible_crtcs = (1 << unit);
	encoder->possible_clones = 0;

	(void) drm_connector_register(connector);

	drm_crtc_init(dev, crtc, &vmw_stdu_crtc_funcs);

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
	vmw_stdu_unpin_display(stdu);

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
