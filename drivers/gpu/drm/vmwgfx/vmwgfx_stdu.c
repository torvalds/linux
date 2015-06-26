/******************************************************************************
 *
 * Copyright © 2014 VMware, Inc., Palo Alto, CA., USA
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
#include "svga3d_surfacedefs.h"
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
 * struct vmw_screen_target_display_unit
 *
 * @base: VMW specific DU structure
 * @display_srf: surface to be displayed.  The dimension of this will always
 *               match the display mode.  If the display mode matches
 *               content_vfbs dimensions, then this is a pointer into the
 *               corresponding field in content_vfbs.  If not, then this
 *               is a separate buffer to which content_vfbs will blit to.
 * @content_fb: holds the rendered content, can be a surface or DMA buffer
 * @content_type:  content_fb type
 * @defined:  true if the current display unit has been initialized
 */
struct vmw_screen_target_display_unit {
	struct vmw_display_unit base;

	struct vmw_surface     *display_srf;
	struct drm_framebuffer *content_fb;

	enum stdu_content_type content_fb_type;

	bool defined;
};



static void vmw_stdu_destroy(struct vmw_screen_target_display_unit *stdu);



/******************************************************************************
 * Screen Target Display Unit helper Functions
 *****************************************************************************/

/**
 * vmw_stdu_pin_display - pins the resource associated with the display surface
 *
 * @stdu: contains the display surface
 *
 * Since the display surface can either be a private surface allocated by us,
 * or it can point to the content surface, we use this function to not pin the
 * same resource twice.
 */
static int vmw_stdu_pin_display(struct vmw_screen_target_display_unit *stdu)
{
	return vmw_resource_pin(&stdu->display_srf->res, false);
}



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

		if (stdu->content_fb_type != SAME_AS_DISPLAY) {
			vmw_resource_unreference(&res);
			stdu->content_fb_type = SAME_AS_DISPLAY;
		}

		stdu->display_srf = NULL;
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
 * vmw_stdu_dma_update - Update DMA buf dirty region on the SVGA device
 *
 * @dev_priv:  VMW DRM device
 * @file_priv: Pointer to a drm file private structure
 * @vfbs: VMW framebuffer surface that may need a DMA buf update
 * @x: top/left corner of the content area to blit from
 * @y: top/left corner of the content area to blit from
 * @width: width of the blit area
 * @height: height of the blit area
 *
 * The SVGA device may have the DMA buf cached, so before letting the
 * device use it as the source image for a subsequent operation, we
 * update the cached copy.
 *
 * RETURNs:
 * 0 on success, error code on failure
 */
static int vmw_stdu_dma_update(struct vmw_private *dev_priv,
			       struct drm_file *file_priv,
			       struct vmw_framebuffer_surface *vfbs,
			       uint32_t x, uint32_t y,
			       uint32_t width, uint32_t height)
{
	size_t fifo_size;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdUpdateGBImage body;
	} img_update_cmd;


	/* Only need to do this if the surface is a DMA buf proxy */
	if (!vfbs->is_dmabuf_proxy)
		return 0;

	fifo_size = sizeof(img_update_cmd);

	memset(&img_update_cmd, 0, fifo_size);
	img_update_cmd.header.id   = SVGA_3D_CMD_UPDATE_GB_IMAGE;
	img_update_cmd.header.size = sizeof(img_update_cmd.body);

	img_update_cmd.body.image.sid = vfbs->surface->res.id;

	img_update_cmd.body.box.x = x;
	img_update_cmd.body.box.y = y;
	img_update_cmd.body.box.w = width;
	img_update_cmd.body.box.h = height;
	img_update_cmd.body.box.d = 1;

	return vmw_execbuf_process(file_priv, dev_priv, NULL,
				   (void *) &img_update_cmd,
				   fifo_size, 0, VMW_QUIRK_SRC_SID_OK,
				   NULL, NULL);
}



/**
 * vmw_stdu_content_copy - copies an area from the content to display surface
 *
 * @dev_priv:  VMW DRM device
 * @file_priv: Pointer to a drm file private structure
 * @stdu: STDU whose display surface will be blitted to
 * @content_x: top/left corner of the content area to blit from
 * @content_y: top/left corner of the content area to blit from
 * @width: width of the blit area
 * @height: height of the blit area
 * @display_x: top/left corner of the display area to blit to
 * @display_y: top/left corner of the display area to blit to
 *
 * Copies an area from the content surface to the display surface.
 *
 * RETURNs:
 * 0 on success, error code on failure
 */
static int vmw_stdu_content_copy(struct vmw_private *dev_priv,
				 struct drm_file *file_priv,
				 struct vmw_screen_target_display_unit *stdu,
				 uint32_t content_x, uint32_t content_y,
				 uint32_t width, uint32_t height,
				 uint32_t display_x, uint32_t display_y)
{
	struct vmw_framebuffer_surface *content_vfbs;
	size_t fifo_size;	
	int ret;
	void *cmd;
	u32 quirks = VMW_QUIRK_DST_SID_OK;

	struct {
		SVGA3dCmdHeader     header;
		SVGA3dCmdSurfaceDMA body;
		SVGA3dCopyBox       area;
		SVGA3dCmdSurfaceDMASuffix suffix;
	} surface_dma_cmd;

	struct {
		SVGA3dCmdHeader      header;
		SVGA3dCmdSurfaceCopy body;
		SVGA3dCopyBox        area;
	} surface_cpy_cmd;


	/*
	 * Can only copy if content and display surfaces exist and are not
	 * the same surface
	 */
	if (stdu->display_srf == NULL || stdu->content_fb == NULL ||
	    stdu->content_fb_type == SAME_AS_DISPLAY) {
		return -EINVAL;
	}


	if (stdu->content_fb_type == SEPARATE_DMA) {
		struct vmw_framebuffer *content_vfb;
		struct drm_vmw_size cur_size = {0};
		const struct svga3d_surface_desc *desc;
		enum SVGA3dSurfaceFormat format;
		SVGA3dCmdSurfaceDMASuffix *suffix;
		SVGAGuestPtr ptr;


		content_vfb  = vmw_framebuffer_to_vfb(stdu->content_fb);

		cur_size.width  = width;
		cur_size.height = height;
		cur_size.depth  = 1;

		/* Derive a SVGA3dSurfaceFormat for the DMA buf */
		switch (content_vfb->base.bits_per_pixel) {
		case 32:
			format = SVGA3D_A8R8G8B8;
			break;
		case 24:
			format = SVGA3D_X8R8G8B8;
			break;
		case 16:
			format = SVGA3D_R5G6B5;
			break;
		case 15:
			format = SVGA3D_A1R5G5B5;
			break;
		default:
			DRM_ERROR("Invalid color depth: %d\n",
					content_vfb->base.depth);
			return -EINVAL;
		}

		desc = svga3dsurface_get_desc(format);


		fifo_size = sizeof(surface_dma_cmd);

		memset(&surface_dma_cmd, 0, fifo_size);

		ptr.gmrId  = content_vfb->user_handle;
		ptr.offset = 0;

		surface_dma_cmd.header.id   = SVGA_3D_CMD_SURFACE_DMA;
		surface_dma_cmd.header.size = sizeof(surface_dma_cmd.body) +
					      sizeof(surface_dma_cmd.area) +
					      sizeof(surface_dma_cmd.suffix);

		surface_dma_cmd.body.guest.ptr   = ptr;
		surface_dma_cmd.body.guest.pitch = stdu->content_fb->pitches[0];
		surface_dma_cmd.body.host.sid    = stdu->display_srf->res.id;
		surface_dma_cmd.body.host.face   = 0;
		surface_dma_cmd.body.host.mipmap = 0;
		surface_dma_cmd.body.transfer    = SVGA3D_WRITE_HOST_VRAM;

		surface_dma_cmd.area.srcx = content_x;
		surface_dma_cmd.area.srcy = content_y;
		surface_dma_cmd.area.x    = display_x;
		surface_dma_cmd.area.y    = display_y;
		surface_dma_cmd.area.d    = 1;
		surface_dma_cmd.area.w    = width;
		surface_dma_cmd.area.h    = height;

		suffix = &surface_dma_cmd.suffix;

		suffix->suffixSize    = sizeof(*suffix);
		suffix->maximumOffset = svga3dsurface_get_image_buffer_size(
						desc,
						&cur_size,
						stdu->content_fb->pitches[0]);

		cmd = (void *) &surface_dma_cmd;
	} else {
		u32 src_id;


		content_vfbs = vmw_framebuffer_to_vfbs(stdu->content_fb);

		if (content_vfbs->is_dmabuf_proxy) {
			ret = vmw_stdu_dma_update(dev_priv, file_priv,
						  content_vfbs,
						  content_x, content_y,
						  width, height);

			if (ret != 0) {
				DRM_ERROR("Failed to update cached DMA buf\n");
				return ret;
			}

			quirks |= VMW_QUIRK_SRC_SID_OK;
			src_id = content_vfbs->surface->res.id;
		} else {
			struct vmw_framebuffer *content_vfb;

			content_vfb = vmw_framebuffer_to_vfb(stdu->content_fb);
			src_id = content_vfb->user_handle;
		}
 
		fifo_size = sizeof(surface_cpy_cmd);

		memset(&surface_cpy_cmd, 0, fifo_size);

		surface_cpy_cmd.header.id   = SVGA_3D_CMD_SURFACE_COPY;
		surface_cpy_cmd.header.size = sizeof(surface_cpy_cmd.body) +
					      sizeof(surface_cpy_cmd.area);

		surface_cpy_cmd.body.src.sid  = src_id;
		surface_cpy_cmd.body.dest.sid = stdu->display_srf->res.id;

		surface_cpy_cmd.area.srcx = content_x;
		surface_cpy_cmd.area.srcy = content_y;
		surface_cpy_cmd.area.x    = display_x;
		surface_cpy_cmd.area.y    = display_y;
		surface_cpy_cmd.area.d    = 1;
		surface_cpy_cmd.area.w    = width;
		surface_cpy_cmd.area.h    = height;

		cmd = (void *) &surface_cpy_cmd;
	}



	ret = vmw_execbuf_process(file_priv, dev_priv, NULL,
				  (void *) cmd,
				  fifo_size, 0, quirks,
				  NULL, NULL);

	return ret;
}



/**
 * vmw_stdu_define_st - Defines a Screen Target
 *
 * @dev_priv:  VMW DRM device
 * @stdu: display unit to create a Screen Target for
 *
 * Creates a STDU that we can used later.  This function is called whenever the
 * framebuffer size changes.
 *
 * RETURNs:
 * 0 on success, error code on failure
 */
static int vmw_stdu_define_st(struct vmw_private *dev_priv,
			      struct vmw_screen_target_display_unit *stdu)
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
	cmd->body.width  = stdu->display_srf->base_size.width;
	cmd->body.height = stdu->display_srf->base_size.height;
	cmd->body.flags  = (0 == cmd->body.stid) ? SVGA_STFLAG_PRIMARY : 0;
	cmd->body.dpi    = 0;
	cmd->body.xRoot  = stdu->base.crtc.x;
	cmd->body.yRoot  = stdu->base.crtc.y;

	if (!stdu->base.is_implicit) {
		cmd->body.xRoot  = stdu->base.gui_x;
		cmd->body.yRoot  = stdu->base.gui_y;
	}

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
 * vmw_stdu_update_st - Updates a Screen Target
 *
 * @dev_priv: VMW DRM device
 * @file_priv: Pointer to DRM file private structure.  Set to NULL when
 *             we want to blank display.
 * @stdu: display unit affected
 * @update_area: area that needs to be updated
 *
 * This function needs to be called whenever the content of a screen
 * target changes.
 * If the display and content buffers are different, then this function does
 * a blit first from the content buffer to the display buffer before issuing
 * the Screen Target update command.
 *
 * RETURNS:
 * 0 on success, error code on failure
 */
static int vmw_stdu_update_st(struct vmw_private *dev_priv,
			      struct drm_file *file_priv,
			      struct vmw_screen_target_display_unit *stdu,
			      struct drm_clip_rect *update_area)
{
	u32 width, height;
	u32 display_update_x, display_update_y;
	unsigned short display_x1, display_y1, display_x2, display_y2;
	int ret;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdUpdateGBScreenTarget body;
	} *cmd;


	if (!stdu->defined) {
		DRM_ERROR("No screen target defined");
		return -EINVAL;
	}

	/* Display coordinates relative to its position in content surface */
	display_x1 = stdu->base.crtc.x;
	display_y1 = stdu->base.crtc.y;
	display_x2 = display_x1 + stdu->display_srf->base_size.width;
	display_y2 = display_y1 + stdu->display_srf->base_size.height;

	/* Do nothing if the update area is outside of the display surface */
	if (update_area->x2 <= display_x1 || update_area->x1 >= display_x2 ||
	    update_area->y2 <= display_y1 || update_area->y1 >= display_y2)
		return 0;

	/* The top-left hand corner of the update area in display surface */
	display_update_x = max(update_area->x1 - display_x1, 0);
	display_update_y = max(update_area->y1 - display_y1, 0);

	width  = min(update_area->x2, display_x2) -
		 max(update_area->x1, display_x1);
	height = min(update_area->y2, display_y2) -
		 max(update_area->y1, display_y1);

	/*
	 * If content is on a separate surface, then copy the dirty area to
	 * the display surface
	 */
	if (file_priv && stdu->content_fb_type != SAME_AS_DISPLAY) {

		ret = vmw_stdu_content_copy(dev_priv, file_priv,
					    stdu,
					    max(update_area->x1, display_x1),
					    max(update_area->y1, display_y1),
					    width, height,
					    display_update_x, display_update_y);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Failed to blit content\n");
			return ret;
		}
	}


	/*
	 * If the display surface is the same as the content surface, then
	 * it may be backed by a DMA buf.  If it is then we need to update
	 * the device's cached copy of the DMA buf before issuing the screen
	 * target update.
	 */
	if (file_priv && stdu->content_fb_type == SAME_AS_DISPLAY) {
		struct vmw_framebuffer_surface *vfbs;

		vfbs = vmw_framebuffer_to_vfbs(stdu->content_fb);
		ret = vmw_stdu_dma_update(dev_priv, file_priv,
					  vfbs,
					  max(update_area->x1, display_x1),
					  max(update_area->y1, display_y1),
					  width, height);

		if (ret != 0) {
			DRM_ERROR("Failed to update cached DMA buffer\n");
			return ret;
		}
	}

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Out of FIFO space updating a Screen Target\n");
		return -ENOMEM;
	}

	cmd->header.id   = SVGA_3D_CMD_UPDATE_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;
	cmd->body.rect.x = display_update_x;
	cmd->body.rect.y = display_update_y;
	cmd->body.rect.w = width;
	cmd->body.rect.h = height;

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
	struct vmw_screen_target_display_unit *stdu;
	struct vmw_framebuffer *vfb;
	struct vmw_framebuffer_surface *new_vfbs;
	struct drm_display_mode *mode;
	struct drm_framebuffer  *new_fb;
	struct drm_crtc      *crtc;
	struct drm_encoder   *encoder;
	struct drm_connector *connector;
	struct drm_clip_rect update_area = {0};
	int    ret;


	if (!set || !set->crtc)
		return -EINVAL;

	crtc     = set->crtc;
	crtc->x  = set->x;
	crtc->y  = set->y;
	stdu     = vmw_crtc_to_stdu(crtc);
	mode     = set->mode;
	new_fb   = set->fb;
	dev_priv = vmw_priv(crtc->dev);


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


	/* Since they always map one to one these are safe */
	connector = &stdu->base.connector;
	encoder   = &stdu->base.encoder;


	/*
	 * After this point the CRTC will be considered off unless a new fb
	 * is bound
	 */
	if (stdu->defined) {
		/* Unbind current surface by binding an invalid one */
		ret = vmw_stdu_bind_st(dev_priv, stdu, NULL);
		if (unlikely(ret != 0))
			return ret;

		/* Update Screen Target, display will now be blank */
		if (crtc->primary->fb) {
			update_area.x2 = crtc->primary->fb->width;
			update_area.y2 = crtc->primary->fb->height;

			ret = vmw_stdu_update_st(dev_priv, NULL,
						 stdu,
						 &update_area);
			if (unlikely(ret != 0))
				return ret;
		}

		crtc->primary->fb  = NULL;
		crtc->enabled      = false;
		encoder->crtc      = NULL;
		connector->encoder = NULL;

		vmw_stdu_unpin_display(stdu);
		stdu->content_fb      = NULL;
		stdu->content_fb_type = SAME_AS_DISPLAY;

		ret = vmw_stdu_destroy_st(dev_priv, stdu);
		/* The hardware is hung, give up */
		if (unlikely(ret != 0))
			return ret;
	}


	/* Any of these conditions means the caller wants CRTC off */
	if (set->num_connectors == 0 || !mode || !new_fb)
		return 0;


	if (set->x + mode->hdisplay > new_fb->width ||
	    set->y + mode->vdisplay > new_fb->height) {
		DRM_ERROR("Set outside of framebuffer\n");
		return -EINVAL;
	}

	stdu->content_fb = new_fb;
	vfb = vmw_framebuffer_to_vfb(stdu->content_fb);

	if (vfb->dmabuf)
		stdu->content_fb_type = SEPARATE_DMA;

	/*
	 * If the requested mode is different than the width and height
	 * of the FB or if the content buffer is a DMA buf, then allocate
	 * a display FB that matches the dimension of the mode
	 */
	if (mode->hdisplay != new_fb->width  ||
	    mode->vdisplay != new_fb->height ||
	    stdu->content_fb_type != SAME_AS_DISPLAY) {
		struct vmw_surface content_srf;
		struct drm_vmw_size display_base_size = {0};
		struct vmw_surface *display_srf;


		display_base_size.width  = mode->hdisplay;
		display_base_size.height = mode->vdisplay;
		display_base_size.depth  = 1;

		/*
		 * If content buffer is a DMA buf, then we have to construct
		 * surface info
		 */
		if (stdu->content_fb_type == SEPARATE_DMA) {

			switch (new_fb->bits_per_pixel) {
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
				ret = -EINVAL;
				goto err_unref_content;
			}

			content_srf.flags             = 0;
			content_srf.mip_levels[0]     = 1;
			content_srf.multisample_count = 0;
		} else {

			stdu->content_fb_type = SEPARATE_SURFACE;

			new_vfbs = vmw_framebuffer_to_vfbs(new_fb);
			content_srf = *new_vfbs->surface;
		}


		ret = vmw_surface_gb_priv_define(crtc->dev,
				0, /* because kernel visible only */
				content_srf.flags,
				content_srf.format,
				true, /* a scanout buffer */
				content_srf.mip_levels[0],
				content_srf.multisample_count,
				display_base_size,
				&display_srf);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Cannot allocate a display FB.\n");
			goto err_unref_content;
		}

		stdu->display_srf = display_srf;
	} else {
		new_vfbs = vmw_framebuffer_to_vfbs(new_fb);
		stdu->display_srf = new_vfbs->surface;
	}


	ret = vmw_stdu_pin_display(stdu);
	if (unlikely(ret != 0)) {
		stdu->display_srf = NULL;
		goto err_unref_content;
	}

	vmw_fb_off(dev_priv);
	vmw_svga_enable(dev_priv);

	/*
	 * Steps to displaying a surface, assume surface is already
	 * bound:
	 *   1.  define a screen target
	 *   2.  bind a fb to the screen target
	 *   3.  update that screen target (this is done later by
	 *       vmw_kms_stdu_do_surface_dirty_or_present)
	 */
	ret = vmw_stdu_define_st(dev_priv, stdu);
	if (unlikely(ret != 0))
		goto err_unpin_display_and_content;

	ret = vmw_stdu_bind_st(dev_priv, stdu, &stdu->display_srf->res);
	if (unlikely(ret != 0))
		goto err_unpin_destroy_st;


	connector->encoder = encoder;
	encoder->crtc      = crtc;

	crtc->mode    = *mode;
	crtc->primary->fb = new_fb;
	crtc->enabled = true;

	return ret;

err_unpin_destroy_st:
	vmw_stdu_destroy_st(dev_priv, stdu);
err_unpin_display_and_content:
	vmw_stdu_unpin_display(stdu);
err_unref_content:
	stdu->content_fb = NULL;
	return ret;
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
	struct drm_file *file_priv;
	struct drm_clip_rect update_area = {0};
	int ret;

	/*
	 * Temporarily don't support event == NULL. We need the
	 * @file_priv pointer!
	 */
	if (event == NULL)
		return -EINVAL;

	if (crtc == NULL)
		return -EINVAL;

	dev_priv          = vmw_priv(crtc->dev);
	stdu              = vmw_crtc_to_stdu(crtc);
	crtc->primary->fb = new_fb;
	stdu->content_fb  = new_fb;

	if (stdu->display_srf) {
		update_area.x2 = stdu->display_srf->base_size.width;
		update_area.y2 = stdu->display_srf->base_size.height;

		/*
		 * If the display surface is the same as the content surface
		 * then remove the reference
		 */
		if (stdu->content_fb_type == SAME_AS_DISPLAY) {
			if (stdu->defined) {
				/* Unbind the current surface */
				ret = vmw_stdu_bind_st(dev_priv, stdu, NULL);
				if (unlikely(ret != 0))
					goto err_out;
			}
			vmw_stdu_unpin_display(stdu);
			stdu->display_srf = NULL;
		}
	}


	if (!new_fb) {
		/* Blanks the display */
		(void) vmw_stdu_update_st(dev_priv, NULL, stdu, &update_area);

		return 0;
	}


	if (stdu->content_fb_type == SAME_AS_DISPLAY) {
		stdu->display_srf = vmw_framebuffer_to_vfbs(new_fb)->surface;
		ret = vmw_stdu_pin_display(stdu);
		if (ret) {
			stdu->display_srf = NULL;
			goto err_out;
		}

		/* Bind display surface */
		ret = vmw_stdu_bind_st(dev_priv, stdu, &stdu->display_srf->res);
		if (unlikely(ret != 0))
			goto err_unpin_display_and_content;
	}

	/* Update display surface: after this point everything is bound */
	update_area.x2 = stdu->display_srf->base_size.width;
	update_area.y2 = stdu->display_srf->base_size.height;

	file_priv = event->base.file_priv;
	ret = vmw_stdu_update_st(dev_priv, file_priv, stdu, &update_area);
	if (unlikely(ret != 0))
		return ret;

	if (event) {
		struct vmw_fence_obj *fence = NULL;

		vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
		if (!fence)
			return -ENOMEM;

		ret = vmw_event_fence_action_queue(file_priv, fence,
						   &event->base,
						   &event->event.tv_sec,
						   &event->event.tv_usec,
						   true);
		vmw_fence_obj_unreference(&fence);
	}

	return ret;

err_unpin_display_and_content:
	vmw_stdu_unpin_display(stdu);
err_out:
	crtc->primary->fb = NULL;
	stdu->content_fb = NULL;
	return ret;
}



/*
 *  Screen Target CRTC dispatch table
 */
static struct drm_crtc_funcs vmw_stdu_crtc_funcs = {
	.save = vmw_du_crtc_save,
	.restore = vmw_du_crtc_restore,
	.cursor_set = vmw_du_crtc_cursor_set,
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

static struct drm_encoder_funcs vmw_stdu_encoder_funcs = {
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



static struct drm_connector_funcs vmw_stdu_connector_funcs = {
	.dpms = vmw_du_connector_dpms,
	.save = vmw_du_connector_save,
	.restore = vmw_du_connector_restore,
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
	stdu->base.pref_mode   = NULL;
	stdu->base.is_implicit = true;

	drm_connector_init(dev, connector, &vmw_stdu_connector_funcs,
			   DRM_MODE_CONNECTOR_VIRTUAL);
	connector->status = vmw_du_connector_detect(connector, false);

	drm_encoder_init(dev, encoder, &vmw_stdu_encoder_funcs,
			 DRM_MODE_ENCODER_VIRTUAL);
	drm_mode_connector_attach_encoder(connector, encoder);
	encoder->possible_crtcs = (1 << unit);
	encoder->possible_clones = 0;

	(void) drm_connector_register(connector);

	drm_crtc_init(dev, crtc, &vmw_stdu_crtc_funcs);

	drm_mode_crtc_set_gamma_size(crtc, 256);

	drm_object_attach_property(&connector->base,
				   dev->mode_config.dirty_info_property,
				   1);

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

	ret = drm_mode_create_dirty_info_property(dev);
	if (unlikely(ret != 0))
		goto err_vblank_cleanup;

	for (i = 0; i < VMWGFX_NUM_DISPLAY_UNITS; ++i) {
		ret = vmw_stdu_init(dev_priv, i);

		if (unlikely(ret != 0)) {
			DRM_ERROR("Failed to initialize STDU %d", i);
			goto err_vblank_cleanup;
		}
	}

	dev_priv->active_display_unit = vmw_du_screen_target;

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



/**
 * vmw_kms_stdu_do_surface_dirty - updates a dirty rectange to SVGA device
 *
 * @dev_priv: VMW DRM device
 * @file_priv: Pointer to a drm file private structure
 * @framebuffer: FB with the new content to be copied to SVGA device
 * @clip_rects: array of dirty rectanges
 * @num_of_clip_rects: number of rectanges in @clips
 * @increment: increment to the next dirty rect in @clips
 *
 * This function sends an Update command to the SVGA device.  This will notify
 * the device that a region needs to be copied to the screen.  At this time
 * we are not coalescing clip rects into one large clip rect because the SVGA
 * device will do it for us.
 *
 * RETURNS:
 * 0 on success, error code otherwise
 */
int vmw_kms_stdu_do_surface_dirty(struct vmw_private *dev_priv,
				  struct drm_file *file_priv,
				  struct vmw_framebuffer *framebuffer,
				  struct drm_clip_rect *clip_rects,
				  unsigned num_of_clip_rects, int increment)
{
	struct vmw_screen_target_display_unit *stdu[VMWGFX_NUM_DISPLAY_UNITS];
	struct drm_clip_rect *cur_rect;
	struct drm_crtc *crtc;

	unsigned num_of_du = 0, cur_du, count = 0;
	int      ret = 0;


	BUG_ON(!clip_rects || !num_of_clip_rects);

	/* Figure out all the DU affected by this surface */
	list_for_each_entry(crtc, &dev_priv->dev->mode_config.crtc_list,
			    head) {
		if (crtc->primary->fb != &framebuffer->base)
			continue;

		stdu[num_of_du++] = vmw_crtc_to_stdu(crtc);
	}

	for (cur_du = 0; cur_du < num_of_du; cur_du++)
		for (cur_rect = clip_rects, count = 0;
		     count < num_of_clip_rects && ret == 0;
		     cur_rect += increment, count++) {
			ret = vmw_stdu_update_st(dev_priv, file_priv,
						 stdu[cur_du],
						 cur_rect);
		}

	return ret;
}



/**
 * vmw_kms_stdu_present - present a surface to the display surface
 *
 * @dev_priv: VMW DRM device
 * @file_priv: Pointer to a drm file private structure
 * @vfb: Used to pick which STDU(s) is affected
 * @user_handle: user handle for the source surface
 * @dest_x: top/left corner of the display area to blit to
 * @dest_y: top/left corner of the display area to blit to
 * @clip_rects: array of dirty rectanges
 * @num_of_clip_rects: number of rectanges in @clips
 *
 * This function copies a surface onto the display surface, and
 * updates the screen target.  Strech blit is currently not
 * supported.
 *
 * RETURNS:
 * 0 on success, error code otherwise
 */
int vmw_kms_stdu_present(struct vmw_private *dev_priv,
			 struct drm_file *file_priv,
			 struct vmw_framebuffer *vfb,
			 uint32_t user_handle,
			 int32_t dest_x, int32_t dest_y,
			 struct drm_vmw_rect *clip_rects,
			 uint32_t num_of_clip_rects)
{
	struct vmw_screen_target_display_unit *stdu[VMWGFX_NUM_DISPLAY_UNITS];
	struct drm_clip_rect *update_area;
	struct drm_crtc *crtc;
	size_t fifo_size;
	int num_of_du = 0, cur_du, i;
	int ret = 0;
	struct vmw_clip_rect src_bb;

	struct {
		SVGA3dCmdHeader      header;
		SVGA3dCmdSurfaceCopy body;
	} *cmd;
	SVGA3dCopyBox *blits;


	BUG_ON(!clip_rects || !num_of_clip_rects);

	list_for_each_entry(crtc, &dev_priv->dev->mode_config.crtc_list, head) {
		if (crtc->primary->fb != &vfb->base)
			continue;

		stdu[num_of_du++] = vmw_crtc_to_stdu(crtc);
	}


	update_area = kcalloc(num_of_clip_rects, sizeof(*update_area),
			      GFP_KERNEL);
	if (unlikely(update_area == NULL)) {
		DRM_ERROR("Temporary clip rect memory alloc failed.\n");
		return -ENOMEM;
	}


	fifo_size = sizeof(*cmd) + sizeof(SVGA3dCopyBox) * num_of_clip_rects;

	cmd = kmalloc(fifo_size, GFP_KERNEL);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed to allocate memory for surface copy.\n");
		ret = -ENOMEM;
		goto out_free_update_area;
	}

	memset(cmd, 0, fifo_size);
	cmd->header.id = SVGA_3D_CMD_SURFACE_COPY;

	blits = (SVGA3dCopyBox *)&cmd[1];


	/* Figure out the source bounding box */
	src_bb.x1 = clip_rects->x;
	src_bb.y1 = clip_rects->y;
	src_bb.x2 = clip_rects->x + clip_rects->w;
	src_bb.y2 = clip_rects->y + clip_rects->h;

	for (i = 1; i < num_of_clip_rects; i++) {
		src_bb.x1 = min_t(int, src_bb.x1, clip_rects[i].x);
		src_bb.x2 = max_t(int, src_bb.x2,
				  clip_rects[i].x + (int) clip_rects[i].w);
		src_bb.y1 = min_t(int, src_bb.y1, clip_rects[i].y);
		src_bb.y2 = max_t(int, src_bb.y2,
				  clip_rects[i].y + (int) clip_rects[i].h);
	}

	for (i = 0; i < num_of_clip_rects; i++) {
		update_area[i].x1 = clip_rects[i].x - src_bb.x1;
		update_area[i].x2 = update_area[i].x1 + clip_rects[i].w;
		update_area[i].y1 = clip_rects[i].y - src_bb.y1;
		update_area[i].y2 = update_area[i].y1 + clip_rects[i].h;
	}


	for (cur_du = 0; cur_du < num_of_du; cur_du++) {
		struct vmw_clip_rect dest_bb;
		int num_of_blits;

		crtc = &stdu[cur_du]->base.crtc;

		dest_bb.x1 = src_bb.x1 + dest_x - crtc->x;
		dest_bb.y1 = src_bb.y1 + dest_y - crtc->y;
		dest_bb.x2 = src_bb.x2 + dest_x - crtc->x;
		dest_bb.y2 = src_bb.y2 + dest_y - crtc->y;

		/* Skip any STDU outside of the destination bounding box */
		if (dest_bb.x1 >= crtc->mode.hdisplay ||
		    dest_bb.y1 >= crtc->mode.vdisplay ||
		    dest_bb.x2 <= 0 || dest_bb.y2 <= 0)
			continue;

		/* Normalize to top-left of src bounding box in dest coord */
		dest_bb.x2 = crtc->mode.hdisplay - dest_bb.x1;
		dest_bb.y2 = crtc->mode.vdisplay - dest_bb.y1;
		dest_bb.x1 = 0 - dest_bb.x1;
		dest_bb.y1 = 0 - dest_bb.y1;

		for (i = 0, num_of_blits = 0; i < num_of_clip_rects; i++) {
			int x1 = max_t(int, dest_bb.x1, (int)update_area[i].x1);
			int y1 = max_t(int, dest_bb.y1, (int)update_area[i].y1);
			int x2 = min_t(int, dest_bb.x2, (int)update_area[i].x2);
			int y2 = min_t(int, dest_bb.y2, (int)update_area[i].y2);

			if (x1 >= x2)
				continue;

			if (y1 >= y2)
				continue;

			blits[num_of_blits].srcx =  src_bb.x1  + x1;
			blits[num_of_blits].srcy =  src_bb.y1  + y1;
			blits[num_of_blits].x    = -dest_bb.x1 + x1;
			blits[num_of_blits].y    = -dest_bb.y1 + y1;
			blits[num_of_blits].d    = 1;
			blits[num_of_blits].w    = x2 - x1;
			blits[num_of_blits].h    = y2 - y1;
			num_of_blits++;
		}

		if (num_of_blits == 0)
			continue;

		/* Calculate new command size */
		fifo_size = sizeof(*cmd) + sizeof(SVGA3dCopyBox) * num_of_blits;

		cmd->header.size = cpu_to_le32(fifo_size - sizeof(cmd->header));

		cmd->body.src.sid  = user_handle;
		cmd->body.dest.sid = stdu[cur_du]->display_srf->res.id;

		ret = vmw_execbuf_process(file_priv, dev_priv, NULL, cmd,
					  fifo_size, 0, VMW_QUIRK_DST_SID_OK,
					  NULL, NULL);

		if (unlikely(ret != 0))
			break;

		for (i = 0; i < num_of_blits; i++) {
			struct drm_clip_rect blit_area;

			/*
			 * Add crtc offset because vmw_stdu_update_st expects
			 * desktop coordinates
			 */
			blit_area.x1 = blits[i].x + crtc->x;
			blit_area.x2 = blit_area.x1 + blits[i].w;
			blit_area.y1 = blits[i].y + crtc->y;
			blit_area.y2 = blit_area.y1 + blits[i].h;
			(void) vmw_stdu_update_st(dev_priv, NULL, stdu[cur_du],
						  &blit_area);
		}
	}

	kfree(cmd);

out_free_update_area:
	kfree(update_area);

	return ret;
}
