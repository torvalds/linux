// SPDX-License-Identifier: GPL-2.0 OR MIT
/******************************************************************************
 *
 * Copyright (c) 2014-2024 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
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

#include "vmwgfx_bo.h"
#include "vmwgfx_kms.h"
#include "vmwgfx_vkms.h"
#include "vmw_surface_cache.h"
#include <linux/fsnotify.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_vblank.h>

#define vmw_crtc_to_stdu(x) \
	container_of(x, struct vmw_screen_target_display_unit, base.crtc)
#define vmw_encoder_to_stdu(x) \
	container_of(x, struct vmw_screen_target_display_unit, base.encoder)
#define vmw_connector_to_stdu(x) \
	container_of(x, struct vmw_screen_target_display_unit, base.connector)

/*
 * Some renderers such as llvmpipe will align the width and height of their
 * buffers to match their tile size. We need to keep this in mind when exposing
 * modes to userspace so that this possible over-allocation will not exceed
 * graphics memory. 64x64 pixels seems to be a reasonable upper bound for the
 * tile size of current renderers.
 */
#define GPU_TILE_SIZE 64

enum stdu_content_type {
	SAME_AS_DISPLAY = 0,
	SEPARATE_SURFACE,
	SEPARATE_BO
};

/**
 * struct vmw_stdu_dirty - closure structure for the update functions
 *
 * @base: The base type we derive from. Used by vmw_kms_helper_dirty().
 * @left: Left side of bounding box.
 * @right: Right side of bounding box.
 * @top: Top side of bounding box.
 * @bottom: Bottom side of bounding box.
 * @fb_left: Left side of the framebuffer/content bounding box
 * @fb_top: Top of the framebuffer/content bounding box
 * @pitch: framebuffer pitch (stride)
 * @buf: buffer object when DMA-ing between buffer and screen targets.
 * @sid: Surface ID when copying between surface and screen targets.
 */
struct vmw_stdu_dirty {
	struct vmw_kms_dirty base;
	s32 left, right, top, bottom;
	s32 fb_left, fb_top;
	u32 pitch;
	union {
		struct vmw_bo *buf;
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

struct vmw_stdu_surface_copy {
	SVGA3dCmdHeader      header;
	SVGA3dCmdSurfaceCopy body;
};

struct vmw_stdu_update_gb_image {
	SVGA3dCmdHeader header;
	SVGA3dCmdUpdateGBImage body;
};

/**
 * struct vmw_screen_target_display_unit - conglomerated STDU structure
 *
 * @base: VMW specific DU structure
 * @display_srf: surface to be displayed.  The dimension of this will always
 *               match the display mode.  If the display mode matches
 *               content_vfbs dimensions, then this is a pointer into the
 *               corresponding field in content_vfbs.  If not, then this
 *               is a separate buffer to which content_vfbs will blit to.
 * @content_fb_type: content_fb type
 * @display_width:  display width
 * @display_height: display height
 * @defined:     true if the current display unit has been initialized
 * @cpp:         Bytes per pixel
 */
struct vmw_screen_target_display_unit {
	struct vmw_display_unit base;
	struct vmw_surface *display_srf;
	enum stdu_content_type content_fb_type;
	s32 display_width, display_height;

	bool defined;

	/* For CPU Blit */
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

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id   = SVGA_3D_CMD_DEFINE_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;
	cmd->body.width  = mode->hdisplay;
	cmd->body.height = mode->vdisplay;
	cmd->body.flags  = (0 == cmd->body.stid) ? SVGA_STFLAG_PRIMARY : 0;
	cmd->body.dpi    = 0;
	cmd->body.xRoot  = crtc_x;
	cmd->body.yRoot  = crtc_y;

	stdu->base.set_gui_x = cmd->body.xRoot;
	stdu->base.set_gui_y = cmd->body.yRoot;

	vmw_cmd_commit(dev_priv, sizeof(*cmd));

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
 *
 * Returns: %0 on success or -errno code on failure
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

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id   = SVGA_3D_CMD_BIND_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;
	cmd->body.image  = image;

	vmw_cmd_commit(dev_priv, sizeof(*cmd));

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

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	vmw_stdu_populate_update(cmd, stdu->base.unit,
				 0, stdu->display_width,
				 0, stdu->display_height);

	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}



/**
 * vmw_stdu_destroy_st - Destroy a Screen Target
 *
 * @dev_priv:  VMW DRM device
 * @stdu: display unit to destroy
 *
 * Returns: %0 on success, negative error code on failure. -ERESTARTSYS if
 * interrupted.
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

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id   = SVGA_3D_CMD_DESTROY_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;

	vmw_cmd_commit(dev_priv, sizeof(*cmd));

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
	struct drm_connector_state *conn_state;
	struct vmw_connector_state *vmw_conn_state;
	int x, y, ret;

	stdu = vmw_crtc_to_stdu(crtc);
	dev_priv = vmw_priv(crtc->dev);
	conn_state = stdu->base.connector.state;
	vmw_conn_state = vmw_connector_state_to_vcs(conn_state);

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

	x = vmw_conn_state->gui_x;
	y = vmw_conn_state->gui_y;

	vmw_svga_enable(dev_priv);
	ret = vmw_stdu_define_st(dev_priv, stdu, &crtc->mode, x, y);

	if (ret)
		DRM_ERROR("Failed to define Screen Target of size %dx%d\n",
			  crtc->x, crtc->y);
}

static void vmw_stdu_crtc_atomic_disable(struct drm_crtc *crtc,
					 struct drm_atomic_state *state)
{
	struct vmw_private *dev_priv;
	struct vmw_screen_target_display_unit *stdu;
	struct drm_crtc_state *new_crtc_state;
	int ret;

	if (!crtc) {
		DRM_ERROR("CRTC is NULL\n");
		return;
	}

	stdu     = vmw_crtc_to_stdu(crtc);
	dev_priv = vmw_priv(crtc->dev);
	new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	if (dev_priv->vkms_enabled)
		drm_crtc_vblank_off(crtc);

	if (stdu->defined) {
		ret = vmw_stdu_bind_st(dev_priv, stdu, NULL);
		if (ret)
			DRM_ERROR("Failed to blank CRTC\n");

		(void) vmw_stdu_update_st(dev_priv, stdu);

		/* Don't destroy the Screen Target if we are only setting the
		 * display as inactive
		 */
		if (new_crtc_state->enable &&
		    !new_crtc_state->active &&
		    !new_crtc_state->mode_changed)
			return;

		ret = vmw_stdu_destroy_st(dev_priv, stdu);
		if (ret)
			DRM_ERROR("Failed to destroy Screen Target\n");

		stdu->content_fb_type = SAME_AS_DISPLAY;
	}
}

/**
 * vmw_stdu_bo_cpu_clip - Callback to encode a CPU blit
 *
 * @dirty: The closure structure.
 *
 * This function calculates the bounding box for all the incoming clips.
 */
static void vmw_stdu_bo_cpu_clip(struct vmw_kms_dirty *dirty)
{
	struct vmw_stdu_dirty *ddirty =
		container_of(dirty, struct vmw_stdu_dirty, base);

	dirty->num_hits = 1;

	/* Calculate destination bounding box */
	ddirty->left = min_t(s32, ddirty->left, dirty->unit_x1);
	ddirty->top = min_t(s32, ddirty->top, dirty->unit_y1);
	ddirty->right = max_t(s32, ddirty->right, dirty->unit_x2);
	ddirty->bottom = max_t(s32, ddirty->bottom, dirty->unit_y2);

	/*
	 * Calculate content bounding box.  We only need the top-left
	 * coordinate because width and height will be the same as the
	 * destination bounding box above
	 */
	ddirty->fb_left = min_t(s32, ddirty->fb_left, dirty->fb_x);
	ddirty->fb_top  = min_t(s32, ddirty->fb_top, dirty->fb_y);
}


/**
 * vmw_stdu_bo_cpu_commit - Callback to do a CPU blit from buffer object
 *
 * @dirty: The closure structure.
 *
 * For the special case when we cannot create a proxy surface in a
 * 2D VM, we have to do a CPU blit ourselves.
 */
static void vmw_stdu_bo_cpu_commit(struct vmw_kms_dirty *dirty)
{
	struct vmw_stdu_dirty *ddirty =
		container_of(dirty, struct vmw_stdu_dirty, base);
	struct vmw_screen_target_display_unit *stdu =
		container_of(dirty->unit, typeof(*stdu), base);
	s32 width, height;
	s32 src_pitch, dst_pitch;
	struct ttm_buffer_object *src_bo, *dst_bo;
	u32 src_offset, dst_offset;
	struct vmw_diff_cpy diff = VMW_CPU_BLIT_DIFF_INITIALIZER(stdu->cpp);

	if (!dirty->num_hits)
		return;

	width = ddirty->right - ddirty->left;
	height = ddirty->bottom - ddirty->top;

	if (width == 0 || height == 0)
		return;

	/* Assume we are blitting from Guest (bo) to Host (display_srf) */
	src_pitch = stdu->display_srf->metadata.base_size.width * stdu->cpp;
	src_bo = &stdu->display_srf->res.guest_memory_bo->tbo;
	src_offset = ddirty->top * src_pitch + ddirty->left * stdu->cpp;

	dst_pitch = ddirty->pitch;
	dst_bo = &ddirty->buf->tbo;
	dst_offset = ddirty->fb_top * dst_pitch + ddirty->fb_left * stdu->cpp;

	(void) vmw_bo_cpu_blit(dst_bo, dst_offset, dst_pitch,
			       src_bo, src_offset, src_pitch,
			       width * stdu->cpp, height, &diff);
}

/**
 * vmw_kms_stdu_readback - Perform a readback from a buffer-object backed
 * framebuffer and the screen target system.
 *
 * @dev_priv: Pointer to the device private structure.
 * @file_priv: Pointer to a struct drm-file identifying the caller. May be
 * set to NULL, but then @user_fence_rep must also be set to NULL.
 * @vfb: Pointer to the buffer-object backed framebuffer.
 * @user_fence_rep: User-space provided structure for fence information.
 * @clips: Array of clip rects. Either @clips or @vclips must be NULL.
 * @vclips: Alternate array of clip rects. Either @clips or @vclips must
 * be NULL.
 * @num_clips: Number of clip rects in @clips or @vclips.
 * @increment: Increment to use when looping over @clips or @vclips.
 * @crtc: If crtc is passed, perform stdu dma on that crtc only.
 *
 * If DMA-ing till the screen target system, the function will also notify
 * the screen target system that a bounding box of the cliprects has been
 * updated.
 *
 * Returns: %0 on success, negative error code on failure. -ERESTARTSYS if
 * interrupted.
 */
int vmw_kms_stdu_readback(struct vmw_private *dev_priv,
			  struct drm_file *file_priv,
			  struct vmw_framebuffer *vfb,
			  struct drm_vmw_fence_rep __user *user_fence_rep,
			  struct drm_clip_rect *clips,
			  struct drm_vmw_rect *vclips,
			  uint32_t num_clips,
			  int increment,
			  struct drm_crtc *crtc)
{
	struct vmw_bo *buf =
		container_of(vfb, struct vmw_framebuffer_bo, base)->buffer;
	struct vmw_stdu_dirty ddirty;
	int ret;
	DECLARE_VAL_CONTEXT(val_ctx, NULL, 0);

	/*
	 * The GMR domain might seem confusing because it might seem like it should
	 * never happen with screen targets but e.g. the xorg vmware driver issues
	 * CMD_SURFACE_DMA for various pixmap updates which might transition our bo to
	 * a GMR. Instead of forcing another transition we can optimize the readback
	 * by reading directly from the GMR.
	 */
	vmw_bo_placement_set(buf,
			     VMW_BO_DOMAIN_MOB | VMW_BO_DOMAIN_SYS | VMW_BO_DOMAIN_GMR,
			     VMW_BO_DOMAIN_MOB | VMW_BO_DOMAIN_SYS | VMW_BO_DOMAIN_GMR);
	ret = vmw_validation_add_bo(&val_ctx, buf);
	if (ret)
		return ret;

	ret = vmw_validation_prepare(&val_ctx, NULL, true);
	if (ret)
		goto out_unref;

	ddirty.left = ddirty.top = S32_MAX;
	ddirty.right = ddirty.bottom = S32_MIN;
	ddirty.fb_left = ddirty.fb_top = S32_MAX;
	ddirty.pitch = vfb->base.pitches[0];
	ddirty.buf = buf;

	ddirty.base.fifo_commit = vmw_stdu_bo_cpu_commit;
	ddirty.base.clip = vmw_stdu_bo_cpu_clip;
	ddirty.base.fifo_reserve_size = 0;

	ddirty.base.crtc = crtc;

	ret = vmw_kms_helper_dirty(dev_priv, vfb, clips, vclips,
				   0, 0, num_clips, increment, &ddirty.base);

	vmw_kms_helper_validation_finish(dev_priv, file_priv, &val_ctx, NULL,
					 user_fence_rep);
	return ret;

out_unref:
	vmw_validation_unref_lists(&val_ctx);
	return ret;
}

/**
 * vmw_kms_stdu_surface_clip - Callback to encode a surface copy command cliprect
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
 * vmw_kms_stdu_surface_fifo_commit - Callback to fill in and submit a surface
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
		vmw_cmd_commit(dirty->dev_priv, 0);
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
		stdu->display_srf->res.res_dirty = true;
	} else {
		update = dirty->cmd;
		commit_size = sizeof(*update);
	}

	vmw_stdu_populate_update(update, stdu->base.unit, sdirty->left,
				 sdirty->right, sdirty->top, sdirty->bottom);

	vmw_cmd_commit(dirty->dev_priv, commit_size);

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
 * @crtc: If crtc is passed, perform surface dirty on that crtc only.
 *
 * Returns: %0 on success, negative error code on failure. -ERESTARTSYS if
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
			       struct vmw_fence_obj **out_fence,
			       struct drm_crtc *crtc)
{
	struct vmw_framebuffer_surface *vfbs =
		container_of(framebuffer, typeof(*vfbs), base);
	struct vmw_stdu_dirty sdirty;
	DECLARE_VAL_CONTEXT(val_ctx, NULL, 0);
	int ret;

	if (!srf)
		srf = &vmw_user_object_surface(&vfbs->uo)->res;

	ret = vmw_validation_add_resource(&val_ctx, srf, 0, VMW_RES_DIRTY_NONE,
					  NULL, NULL);
	if (ret)
		return ret;

	ret = vmw_validation_prepare(&val_ctx, &dev_priv->cmdbuf_mutex, true);
	if (ret)
		goto out_unref;

	sdirty.base.fifo_commit = vmw_kms_stdu_surface_fifo_commit;
	sdirty.base.clip = vmw_kms_stdu_surface_clip;
	sdirty.base.fifo_reserve_size = sizeof(struct vmw_stdu_surface_copy) +
		sizeof(SVGA3dCopyBox) * num_clips +
		sizeof(struct vmw_stdu_update);
	sdirty.base.crtc = crtc;
	sdirty.sid = srf->id;
	sdirty.left = sdirty.top = S32_MAX;
	sdirty.right = sdirty.bottom = S32_MIN;

	ret = vmw_kms_helper_dirty(dev_priv, framebuffer, clips, vclips,
				   dest_x, dest_y, num_clips, inc,
				   &sdirty.base);

	vmw_kms_helper_validation_finish(dev_priv, NULL, &val_ctx, out_fence,
					 NULL);

	return ret;

out_unref:
	vmw_validation_unref_lists(&val_ctx);
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
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.enable_vblank		= vmw_vkms_enable_vblank,
	.disable_vblank		= vmw_vkms_disable_vblank,
	.get_vblank_timestamp	= vmw_vkms_get_vblank_timestamp,
	.get_crc_sources	= vmw_vkms_get_crc_sources,
	.set_crc_source		= vmw_vkms_set_crc_source,
	.verify_crc_source	= vmw_vkms_verify_crc_source,
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

static enum drm_mode_status
vmw_stdu_connector_mode_valid(struct drm_connector *connector,
			      struct drm_display_mode *mode)
{
	enum drm_mode_status ret;
	struct drm_device *dev = connector->dev;
	struct vmw_private *dev_priv = vmw_priv(dev);
	u64 assumed_cpp = dev_priv->assume_16bpp ? 2 : 4;
	/* Align width and height to account for GPU tile over-alignment */
	u64 required_mem = ALIGN(mode->hdisplay, GPU_TILE_SIZE) *
			   ALIGN(mode->vdisplay, GPU_TILE_SIZE) *
			   assumed_cpp;
	required_mem = ALIGN(required_mem, PAGE_SIZE);

	ret = drm_mode_validate_size(mode, dev_priv->stdu_max_width,
				     dev_priv->stdu_max_height);
	if (ret != MODE_OK)
		return ret;

	ret = drm_mode_validate_size(mode, dev_priv->texture_max_width,
				     dev_priv->texture_max_height);
	if (ret != MODE_OK)
		return ret;

	if (required_mem > dev_priv->max_primary_mem)
		return MODE_MEM;

	if (required_mem > dev_priv->max_mob_pages * PAGE_SIZE)
		return MODE_MEM;

	if (required_mem > dev_priv->max_mob_size)
		return MODE_MEM;

	return MODE_OK;
}

/*
 * Trigger a modeset if the X,Y position of the Screen Target changes.
 * This is needed when multi-mon is cycled. The original Screen Target will have
 * the same mode but its relative X,Y position in the topology will change.
 */
static int vmw_stdu_connector_atomic_check(struct drm_connector *conn,
					   struct drm_atomic_state *state)
{
	struct drm_connector_state *conn_state;
	struct vmw_screen_target_display_unit *du;
	struct drm_crtc_state *new_crtc_state;

	conn_state = drm_atomic_get_connector_state(state, conn);
	du = vmw_connector_to_stdu(conn);

	if (!conn_state->crtc)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (du->base.gui_x != du->base.set_gui_x ||
	    du->base.gui_y != du->base.set_gui_y)
		new_crtc_state->mode_changed = true;

	return 0;
}

static const struct drm_connector_funcs vmw_stdu_connector_funcs = {
	.dpms = vmw_du_connector_dpms,
	.detect = vmw_du_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vmw_stdu_connector_destroy,
	.reset = vmw_du_connector_reset,
	.atomic_duplicate_state = vmw_du_connector_duplicate_state,
	.atomic_destroy_state = vmw_du_connector_destroy_state,
};


static const struct
drm_connector_helper_funcs vmw_stdu_connector_helper_funcs = {
	.get_modes = vmw_connector_get_modes,
	.mode_valid = vmw_stdu_connector_mode_valid,
	.atomic_check = vmw_stdu_connector_atomic_check,
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

	if (vmw_user_object_surface(&vps->uo))
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
 * backed by a buffer object.  The display surface is pinned here, and it'll
 * be unpinned in .cleanup_fb()
 *
 * Returns: %0 on success
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
	uint32_t hdisplay = new_state->crtc_w, vdisplay = new_state->crtc_h;
	struct drm_plane_state *old_state = plane->state;
	struct drm_rect rect;
	int ret;

	/* No FB to prepare */
	if (!new_fb) {
		if (vmw_user_object_surface(&vps->uo)) {
			WARN_ON(vps->pinned != 0);
			vmw_user_object_unref(&vps->uo);
		}

		return 0;
	}

	vfb = vmw_framebuffer_to_vfb(new_fb);
	new_vfbs = (vfb->bo) ? NULL : vmw_framebuffer_to_vfbs(new_fb);

	if (new_vfbs &&
	    vmw_user_object_surface(&new_vfbs->uo)->metadata.base_size.width == hdisplay &&
	    vmw_user_object_surface(&new_vfbs->uo)->metadata.base_size.height == vdisplay)
		new_content_type = SAME_AS_DISPLAY;
	else if (vfb->bo)
		new_content_type = SEPARATE_BO;
	else
		new_content_type = SEPARATE_SURFACE;

	if (new_content_type != SAME_AS_DISPLAY) {
		struct vmw_surface_metadata metadata = {0};

		/*
		 * If content buffer is a buffer object, then we have to
		 * construct surface info
		 */
		if (new_content_type == SEPARATE_BO) {

			switch (new_fb->format->cpp[0]*8) {
			case 32:
				metadata.format = SVGA3D_X8R8G8B8;
				break;

			case 16:
				metadata.format = SVGA3D_R5G6B5;
				break;

			case 8:
				metadata.format = SVGA3D_P8;
				break;

			default:
				DRM_ERROR("Invalid format\n");
				return -EINVAL;
			}

			metadata.mip_levels[0] = 1;
			metadata.num_sizes = 1;
			metadata.scanout = true;
		} else {
			metadata = vmw_user_object_surface(&new_vfbs->uo)->metadata;
		}

		metadata.base_size.width = hdisplay;
		metadata.base_size.height = vdisplay;
		metadata.base_size.depth = 1;

		if (vmw_user_object_surface(&vps->uo)) {
			struct drm_vmw_size cur_base_size =
				vmw_user_object_surface(&vps->uo)->metadata.base_size;

			if (cur_base_size.width != metadata.base_size.width ||
			    cur_base_size.height != metadata.base_size.height ||
			    vmw_user_object_surface(&vps->uo)->metadata.format != metadata.format) {
				WARN_ON(vps->pinned != 0);
				vmw_user_object_unref(&vps->uo);
			}

		}

		if (!vmw_user_object_surface(&vps->uo)) {
			ret = vmw_gb_surface_define(dev_priv, &metadata,
						    &vps->uo.surface);
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
		if (vmw_user_object_surface(&vps->uo)) {
			WARN_ON(vps->pinned != 0);
			vmw_user_object_unref(&vps->uo);
		}

		memcpy(&vps->uo, &new_vfbs->uo, sizeof(vps->uo));
		vmw_user_object_ref(&vps->uo);
	}

	if (vmw_user_object_surface(&vps->uo)) {

		/* Pin new surface before flipping */
		ret = vmw_resource_pin(&vmw_user_object_surface(&vps->uo)->res, false);
		if (ret)
			goto out_srf_unref;

		vps->pinned++;
	}

	vps->content_fb_type = new_content_type;

	/*
	 * The drm fb code will do blit's via the vmap interface, which doesn't
	 * trigger vmw_bo page dirty tracking due to being kernel side (and thus
	 * doesn't require mmap'ing) so we have to update the surface's dirty
	 * regions by hand but we want to be careful to not overwrite the
	 * resource if it has been written to by the gpu (res_dirty).
	 */
	if (vps->uo.buffer && vps->uo.buffer->is_dumb) {
		struct vmw_surface *surf = vmw_user_object_surface(&vps->uo);
		struct vmw_resource *res = &surf->res;

		if (!res->res_dirty && drm_atomic_helper_damage_merged(old_state,
								       new_state,
								       &rect)) {
			/*
			 * At some point it might be useful to actually translate
			 * (rect.x1, rect.y1) => start, and (rect.x2, rect.y2) => end,
			 * but currently the fb code will just report the entire fb
			 * dirty so in practice it doesn't matter.
			 */
			pgoff_t start = res->guest_memory_offset >> PAGE_SHIFT;
			pgoff_t end = __KERNEL_DIV_ROUND_UP(res->guest_memory_offset +
							    res->guest_memory_size,
							    PAGE_SIZE);
			vmw_resource_dirty_update(res, start, end);
		}
	}

	/*
	 * This should only happen if the buffer object is too large to create a
	 * proxy surface for.
	 */
	if (vps->content_fb_type == SEPARATE_BO)
		vps->cpp = new_fb->pitches[0] / new_fb->width;

	return 0;

out_srf_unref:
	vmw_user_object_unref(&vps->uo);
	return ret;
}

static uint32_t vmw_stdu_bo_fifo_size_cpu(struct vmw_du_update_plane *update,
					  uint32_t num_hits)
{
	return sizeof(struct vmw_stdu_update_gb_image) +
		sizeof(struct vmw_stdu_update);
}

static uint32_t vmw_stdu_bo_pre_clip_cpu(struct vmw_du_update_plane  *update,
					 void *cmd, uint32_t num_hits)
{
	struct vmw_du_update_plane_buffer *bo_update =
		container_of(update, typeof(*bo_update), base);

	bo_update->fb_left = INT_MAX;
	bo_update->fb_top = INT_MAX;

	return 0;
}

static uint32_t vmw_stdu_bo_clip_cpu(struct vmw_du_update_plane  *update,
				     void *cmd, struct drm_rect *clip,
				     uint32_t fb_x, uint32_t fb_y)
{
	struct vmw_du_update_plane_buffer *bo_update =
		container_of(update, typeof(*bo_update), base);

	bo_update->fb_left = min_t(int, bo_update->fb_left, fb_x);
	bo_update->fb_top = min_t(int, bo_update->fb_top, fb_y);

	return 0;
}

static uint32_t
vmw_stdu_bo_populate_update_cpu(struct vmw_du_update_plane  *update, void *cmd,
				struct drm_rect *bb)
{
	struct vmw_du_update_plane_buffer *bo_update;
	struct vmw_screen_target_display_unit *stdu;
	struct vmw_framebuffer_bo *vfbbo;
	struct vmw_diff_cpy diff = VMW_CPU_BLIT_DIFF_INITIALIZER(0);
	struct vmw_stdu_update_gb_image *cmd_img = cmd;
	struct vmw_stdu_update *cmd_update;
	struct ttm_buffer_object *src_bo, *dst_bo;
	u32 src_offset, dst_offset;
	s32 src_pitch, dst_pitch;
	s32 width, height;

	bo_update = container_of(update, typeof(*bo_update), base);
	stdu = container_of(update->du, typeof(*stdu), base);
	vfbbo = container_of(update->vfb, typeof(*vfbbo), base);

	width = bb->x2 - bb->x1;
	height = bb->y2 - bb->y1;

	diff.cpp = stdu->cpp;

	dst_bo = &stdu->display_srf->res.guest_memory_bo->tbo;
	dst_pitch = stdu->display_srf->metadata.base_size.width * stdu->cpp;
	dst_offset = bb->y1 * dst_pitch + bb->x1 * stdu->cpp;

	src_bo = &vfbbo->buffer->tbo;
	src_pitch = update->vfb->base.pitches[0];
	src_offset = bo_update->fb_top * src_pitch + bo_update->fb_left *
		stdu->cpp;

	(void) vmw_bo_cpu_blit(dst_bo, dst_offset, dst_pitch, src_bo,
			       src_offset, src_pitch, width * stdu->cpp, height,
			       &diff);

	if (drm_rect_visible(&diff.rect)) {
		SVGA3dBox *box = &cmd_img->body.box;

		cmd_img->header.id = SVGA_3D_CMD_UPDATE_GB_IMAGE;
		cmd_img->header.size = sizeof(cmd_img->body);
		cmd_img->body.image.sid = stdu->display_srf->res.id;
		cmd_img->body.image.face = 0;
		cmd_img->body.image.mipmap = 0;

		box->x = diff.rect.x1;
		box->y = diff.rect.y1;
		box->z = 0;
		box->w = drm_rect_width(&diff.rect);
		box->h = drm_rect_height(&diff.rect);
		box->d = 1;

		cmd_update = (struct vmw_stdu_update *)&cmd_img[1];
		vmw_stdu_populate_update(cmd_update, stdu->base.unit,
					 diff.rect.x1, diff.rect.x2,
					 diff.rect.y1, diff.rect.y2);

		return sizeof(*cmd_img) + sizeof(*cmd_update);
	}

	return 0;
}

/**
 * vmw_stdu_plane_update_bo - Update display unit for bo backed fb.
 * @dev_priv: device private.
 * @plane: plane state.
 * @old_state: old plane state.
 * @vfb: framebuffer which is blitted to display unit.
 * @out_fence: If non-NULL, will return a ref-counted pointer to vmw_fence_obj.
 *             The returned fence pointer may be NULL in which case the device
 *             has already synchronized.
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int vmw_stdu_plane_update_bo(struct vmw_private *dev_priv,
				    struct drm_plane *plane,
				    struct drm_plane_state *old_state,
				    struct vmw_framebuffer *vfb,
				    struct vmw_fence_obj **out_fence)
{
	struct vmw_du_update_plane_buffer bo_update;

	memset(&bo_update, 0, sizeof(struct vmw_du_update_plane_buffer));
	bo_update.base.plane = plane;
	bo_update.base.old_state = old_state;
	bo_update.base.dev_priv = dev_priv;
	bo_update.base.du = vmw_crtc_to_du(plane->state->crtc);
	bo_update.base.vfb = vfb;
	bo_update.base.out_fence = out_fence;
	bo_update.base.mutex = NULL;
	bo_update.base.intr = false;

	bo_update.base.calc_fifo_size = vmw_stdu_bo_fifo_size_cpu;
	bo_update.base.pre_clip = vmw_stdu_bo_pre_clip_cpu;
	bo_update.base.clip = vmw_stdu_bo_clip_cpu;
	bo_update.base.post_clip = vmw_stdu_bo_populate_update_cpu;

	return vmw_du_helper_plane_update(&bo_update.base);
}

static uint32_t
vmw_stdu_surface_fifo_size_same_display(struct vmw_du_update_plane *update,
					uint32_t num_hits)
{
	uint32_t size = 0;

	size += sizeof(struct vmw_stdu_update);

	return size;
}

static uint32_t vmw_stdu_surface_fifo_size(struct vmw_du_update_plane *update,
					   uint32_t num_hits)
{
	uint32_t size = 0;

	size += sizeof(struct vmw_stdu_surface_copy) + sizeof(SVGA3dCopyBox) *
		num_hits + sizeof(struct vmw_stdu_update);

	return size;
}

static uint32_t
vmw_stdu_surface_populate_copy(struct vmw_du_update_plane  *update, void *cmd,
			       uint32_t num_hits)
{
	struct vmw_screen_target_display_unit *stdu;
	struct vmw_framebuffer_surface *vfbs;
	struct vmw_stdu_surface_copy *cmd_copy = cmd;

	stdu = container_of(update->du, typeof(*stdu), base);
	vfbs = container_of(update->vfb, typeof(*vfbs), base);

	cmd_copy->header.id = SVGA_3D_CMD_SURFACE_COPY;
	cmd_copy->header.size = sizeof(cmd_copy->body) + sizeof(SVGA3dCopyBox) *
		num_hits;
	cmd_copy->body.src.sid = vmw_user_object_surface(&vfbs->uo)->res.id;
	cmd_copy->body.dest.sid = stdu->display_srf->res.id;

	return sizeof(*cmd_copy);
}

static uint32_t
vmw_stdu_surface_populate_clip(struct vmw_du_update_plane  *update, void *cmd,
			       struct drm_rect *clip, uint32_t fb_x,
			       uint32_t fb_y)
{
	struct SVGA3dCopyBox *box = cmd;

	box->srcx = fb_x;
	box->srcy = fb_y;
	box->srcz = 0;
	box->x = clip->x1;
	box->y = clip->y1;
	box->z = 0;
	box->w = drm_rect_width(clip);
	box->h = drm_rect_height(clip);
	box->d = 1;

	return sizeof(*box);
}

static uint32_t
vmw_stdu_surface_populate_update(struct vmw_du_update_plane  *update, void *cmd,
				 struct drm_rect *bb)
{
	vmw_stdu_populate_update(cmd, update->du->unit, bb->x1, bb->x2, bb->y1,
				 bb->y2);

	return sizeof(struct vmw_stdu_update);
}

/**
 * vmw_stdu_plane_update_surface - Update display unit for surface backed fb
 * @dev_priv: Device private
 * @plane: Plane state
 * @old_state: Old plane state
 * @vfb: Framebuffer which is blitted to display unit
 * @out_fence: If non-NULL, will return a ref-counted pointer to vmw_fence_obj.
 *             The returned fence pointer may be NULL in which case the device
 *             has already synchronized.
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int vmw_stdu_plane_update_surface(struct vmw_private *dev_priv,
					 struct drm_plane *plane,
					 struct drm_plane_state *old_state,
					 struct vmw_framebuffer *vfb,
					 struct vmw_fence_obj **out_fence)
{
	struct vmw_du_update_plane srf_update;
	struct vmw_screen_target_display_unit *stdu;
	struct vmw_framebuffer_surface *vfbs;

	stdu = vmw_crtc_to_stdu(plane->state->crtc);
	vfbs = container_of(vfb, typeof(*vfbs), base);

	memset(&srf_update, 0, sizeof(struct vmw_du_update_plane));
	srf_update.plane = plane;
	srf_update.old_state = old_state;
	srf_update.dev_priv = dev_priv;
	srf_update.du = vmw_crtc_to_du(plane->state->crtc);
	srf_update.vfb = vfb;
	srf_update.out_fence = out_fence;
	srf_update.mutex = &dev_priv->cmdbuf_mutex;
	srf_update.intr = true;

	if (vmw_user_object_surface(&vfbs->uo)->res.id != stdu->display_srf->res.id) {
		srf_update.calc_fifo_size = vmw_stdu_surface_fifo_size;
		srf_update.pre_clip = vmw_stdu_surface_populate_copy;
		srf_update.clip = vmw_stdu_surface_populate_clip;
	} else {
		srf_update.calc_fifo_size =
			vmw_stdu_surface_fifo_size_same_display;
	}

	srf_update.post_clip = vmw_stdu_surface_populate_update;

	return vmw_du_helper_plane_update(&srf_update);
}

/**
 * vmw_stdu_primary_plane_atomic_update - formally switches STDU to new plane
 * @plane: display plane
 * @state: Only used to get crtc info
 *
 * Formally update stdu->display_srf to the new plane, and bind the new
 * plane STDU.  This function is called during the commit phase when
 * all the preparation have been done and all the configurations have
 * been checked.
 */
static void
vmw_stdu_primary_plane_atomic_update(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state, plane);
	struct vmw_plane_state *vps = vmw_plane_state_to_vps(new_state);
	struct drm_crtc *crtc = new_state->crtc;
	struct vmw_screen_target_display_unit *stdu;
	struct vmw_fence_obj *fence = NULL;
	struct vmw_private *dev_priv;
	int ret;

	/* If case of device error, maintain consistent atomic state */
	if (crtc && new_state->fb) {
		struct vmw_framebuffer *vfb =
			vmw_framebuffer_to_vfb(new_state->fb);
		stdu = vmw_crtc_to_stdu(crtc);
		dev_priv = vmw_priv(crtc->dev);

		stdu->display_srf = vmw_user_object_surface(&vps->uo);
		stdu->content_fb_type = vps->content_fb_type;
		stdu->cpp = vps->cpp;

		ret = vmw_stdu_bind_st(dev_priv, stdu, &stdu->display_srf->res);
		if (ret)
			DRM_ERROR("Failed to bind surface to STDU.\n");

		if (vfb->bo)
			ret = vmw_stdu_plane_update_bo(dev_priv, plane,
						       old_state, vfb, &fence);
		else
			ret = vmw_stdu_plane_update_surface(dev_priv, plane,
							    old_state, vfb,
							    &fence);
		if (ret)
			DRM_ERROR("Failed to update STDU.\n");
	} else {
		crtc = old_state->crtc;
		stdu = vmw_crtc_to_stdu(crtc);
		dev_priv = vmw_priv(crtc->dev);

		/* Blank STDU when fb and crtc are NULL */
		if (!stdu->defined)
			return;

		ret = vmw_stdu_bind_st(dev_priv, stdu, NULL);
		if (ret)
			DRM_ERROR("Failed to blank STDU\n");

		ret = vmw_stdu_update_st(dev_priv, stdu);
		if (ret)
			DRM_ERROR("Failed to update STDU.\n");

		return;
	}

	if (fence)
		vmw_fence_obj_unreference(&fence);
}

static void
vmw_stdu_crtc_atomic_flush(struct drm_crtc *crtc,
			   struct drm_atomic_state *state)
{
	struct vmw_private *vmw = vmw_priv(crtc->dev);
	struct vmw_screen_target_display_unit *stdu = vmw_crtc_to_stdu(crtc);

	if (vmw->vkms_enabled)
		vmw_vkms_set_crc_surface(crtc, stdu->display_srf);
	vmw_vkms_crtc_atomic_flush(crtc, state);
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
	.cleanup_fb = vmw_du_cursor_plane_cleanup_fb,
};

static const struct
drm_plane_helper_funcs vmw_stdu_primary_plane_helper_funcs = {
	.atomic_check = vmw_du_primary_plane_atomic_check,
	.atomic_update = vmw_stdu_primary_plane_atomic_update,
	.prepare_fb = vmw_stdu_primary_plane_prepare_fb,
	.cleanup_fb = vmw_stdu_primary_plane_cleanup_fb,
};

static const struct drm_crtc_helper_funcs vmw_stdu_crtc_helper_funcs = {
	.mode_set_nofb = vmw_stdu_crtc_mode_set_nofb,
	.atomic_check = vmw_du_crtc_atomic_check,
	.atomic_begin = vmw_du_crtc_atomic_begin,
	.atomic_flush = vmw_stdu_crtc_atomic_flush,
	.atomic_enable = vmw_vkms_crtc_atomic_enable,
	.atomic_disable = vmw_stdu_crtc_atomic_disable,
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
 *
 * Returns: %0 on success or -errno code on failure
 */
static int vmw_stdu_init(struct vmw_private *dev_priv, unsigned unit)
{
	struct vmw_screen_target_display_unit *stdu;
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_plane *primary;
	struct vmw_cursor_plane *cursor;
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
	stdu->base.is_implicit = false;

	/* Initialize primary plane */
	ret = drm_universal_plane_init(dev, primary,
				       0, &vmw_stdu_plane_funcs,
				       vmw_primary_plane_formats,
				       ARRAY_SIZE(vmw_primary_plane_formats),
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize primary plane");
		goto err_free;
	}

	drm_plane_helper_add(primary, &vmw_stdu_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary);

	/* Initialize cursor plane */
	ret = drm_universal_plane_init(dev, &cursor->base,
			0, &vmw_stdu_cursor_funcs,
			vmw_cursor_plane_formats,
			ARRAY_SIZE(vmw_cursor_plane_formats),
			NULL, DRM_PLANE_TYPE_CURSOR, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize cursor plane");
		drm_plane_cleanup(&stdu->base.primary);
		goto err_free;
	}

	drm_plane_helper_add(&cursor->base, &vmw_stdu_cursor_plane_helper_funcs);

	ret = drm_connector_init(dev, connector, &vmw_stdu_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		goto err_free;
	}

	drm_connector_helper_add(connector, &vmw_stdu_connector_helper_funcs);
	connector->status = vmw_du_connector_detect(connector, false);

	ret = drm_encoder_init(dev, encoder, &vmw_stdu_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret) {
		DRM_ERROR("Failed to initialize encoder\n");
		goto err_free_connector;
	}

	(void) drm_connector_attach_encoder(connector, encoder);
	encoder->possible_crtcs = (1 << unit);
	encoder->possible_clones = 0;

	ret = drm_connector_register(connector);
	if (ret) {
		DRM_ERROR("Failed to register connector\n");
		goto err_free_encoder;
	}

	ret = drm_crtc_init_with_planes(dev, crtc, primary,
					&cursor->base,
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

	vmw_du_init(&stdu->base);

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
	struct drm_device *dev = &dev_priv->drm;
	int i, ret;


	/* Do nothing if there's no support for MOBs */
	if (!dev_priv->has_mob)
		return -ENOSYS;

	if (!(dev_priv->capabilities & SVGA_CAP_GBOBJECTS))
		return -ENOSYS;

	dev_priv->active_display_unit = vmw_du_screen_target;

	for (i = 0; i < VMWGFX_NUM_DISPLAY_UNITS; ++i) {
		ret = vmw_stdu_init(dev_priv, i);

		if (unlikely(ret != 0)) {
			drm_err(&dev_priv->drm,
				"Failed to initialize STDU %d", i);
			return ret;
		}
	}

	drm_mode_config_reset(dev);

	return 0;
}
